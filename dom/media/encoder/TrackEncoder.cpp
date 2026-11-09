/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TrackEncoder.h"

#include "AudioChannelFormat.h"
#include "DriftCompensation.h"
#include "MediaTrackGraph.h"
#include "MediaTrackListener.h"
#include "VideoUtils.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/Logging.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/RollingMean.h"

namespace mozilla {

LazyLogModule gTrackEncoderLog("TrackEncoder");
#define TRACK_LOG(type, msg) \
  MOZ_LOG_FMT(gTrackEncoderLog, type, MOZ_LOG_EXPAND_ARGS msg)

constexpr int DEFAULT_CHANNELS = 1;
constexpr int DEFAULT_FRAME_WIDTH = 640;
constexpr int DEFAULT_FRAME_HEIGHT = 480;
constexpr int DEFAULT_FRAME_RATE = 30;
// 10 second threshold if the audio encoder cannot be initialized.
constexpr int AUDIO_INIT_FAILED_DURATION = 10;
// 30 second threshold if the video encoder cannot be initialized.
constexpr int VIDEO_INIT_FAILED_DURATION = 30;
constexpr int FRAMERATE_DETECTION_ROLLING_WINDOW = 3;
constexpr size_t FRAMERATE_DETECTION_MIN_CHUNKS = 5;
constexpr int FRAMERATE_DETECTION_MAX_DURATION_S = 6;

TrackEncoder::TrackEncoder(TrackRate aTrackRate,
                           MediaQueue<EncodedFrame>& aEncodedDataQueue)
    : mInitialized(false),
      mStarted(false),
      mEndOfStream(false),
      mCanceled(false),
      mInitCounter(0),
      mSuspended(false),
      mTrackRate(aTrackRate),
      mEncodedDataQueue(aEncodedDataQueue) {}

bool TrackEncoder::IsInitialized() {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  return mInitialized;
}

bool TrackEncoder::IsStarted() {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  return mStarted;
}

bool TrackEncoder::IsEncodingComplete() const {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  return mEncodedDataQueue.IsFinished();
}

void TrackEncoder::SetInitialized() {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());

  if (mInitialized) {
    return;
  }

  mInitialized = true;

  for (auto& l : mListeners.Clone()) {
    l->Initialized(this);
  }
}

void TrackEncoder::SetStarted() {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());

  if (mStarted) {
    return;
  }

  mStarted = true;

  for (auto& l : mListeners.Clone()) {
    l->Started(this);
  }
}

void TrackEncoder::OnError() {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());

  Cancel();

  for (auto& l : mListeners.Clone()) {
    l->Error(this);
  }
}

void TrackEncoder::RegisterListener(TrackEncoderListener* aListener) {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  MOZ_ASSERT(!mListeners.Contains(aListener));
  mListeners.AppendElement(aListener);
}

bool TrackEncoder::UnregisterListener(TrackEncoderListener* aListener) {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  return mListeners.RemoveElement(aListener);
}

void TrackEncoder::SetWorkerThread(AbstractThread* aWorkerThread) {
  mWorkerThread = aWorkerThread;
}

void AudioTrackEncoder::Suspend() {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  TRACK_LOG(LogLevel::Info,
            ("[AudioTrackEncoder {}]: Suspend(), was {}", fmt::ptr(this),
             mSuspended ? "suspended" : "live"));

  if (mSuspended) {
    return;
  }

  mSuspended = true;
}

void AudioTrackEncoder::Resume() {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  TRACK_LOG(LogLevel::Info,
            ("[AudioTrackEncoder {}]: Resume(), was {}", fmt::ptr(this),
             mSuspended ? "suspended" : "live"));

  if (!mSuspended) {
    return;
  }

  mSuspended = false;
}

void AudioTrackEncoder::AppendAudioSegment(AudioSegment&& aSegment) {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  AUTO_PROFILER_LABEL("AudioTrackEncoder::AppendAudioSegment", OTHER);
  TRACK_LOG(LogLevel::Verbose,
            ("[AudioTrackEncoder {}]: AppendAudioSegment() duration={}",
             fmt::ptr(this), aSegment.GetDuration()));

  if (mCanceled) {
    return;
  }

  if (mEndOfStream) {
    return;
  }

  TryInit(mOutgoingBuffer, aSegment.GetDuration());

  if (mSuspended) {
    return;
  }

  SetStarted();
  mOutgoingBuffer.AppendFrom(&aSegment);

  if (!mInitialized) {
    return;
  }

  if (NS_FAILED(Encode(&mOutgoingBuffer))) {
    OnError();
    return;
  }

  MOZ_ASSERT_IF(IsEncodingComplete(), mOutgoingBuffer.IsEmpty());
}

void AudioTrackEncoder::TryInit(const AudioSegment& aSegment,
                                TrackTime aDuration) {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());

  if (mInitialized) {
    return;
  }

  mInitCounter++;
  TRACK_LOG(LogLevel::Debug,
            ("[AudioTrackEncoder {}]: Inited the audio encoder {} times",
             fmt::ptr(this), mInitCounter));

  for (AudioSegment::ConstChunkIterator iter(aSegment); !iter.IsEnded();
       iter.Next()) {
    // The number of channels is determined by the first non-null chunk, and
    // thus the audio encoder is initialized at this time.
    if (iter->IsNull()) {
      continue;
    }

    nsresult rv = Init(iter->mChannelData.Length());

    if (NS_SUCCEEDED(rv)) {
      TRACK_LOG(LogLevel::Info,
                ("[AudioTrackEncoder {}]: Successfully initialized!",
                 fmt::ptr(this)));
      return;
    } else {
      TRACK_LOG(LogLevel::Error,
                ("[AudioTrackEncoder {}]: Failed to initialize the encoder!",
                 fmt::ptr(this)));
      OnError();
      return;
    }
    break;
  }

  mNotInitDuration += aDuration;
  if (!mInitialized &&
      ((mNotInitDuration - 1) / mTrackRate >= AUDIO_INIT_FAILED_DURATION) &&
      mInitCounter > 1) {
    // Perform a best effort initialization since we haven't gotten any
    // data yet. Motivated by issues like Bug 1336367
    TRACK_LOG(LogLevel::Warning,
              ("[AudioTrackEncoder]: Initialize failed for {}s. Attempting to "
               "init with {} (default) channels!",
               AUDIO_INIT_FAILED_DURATION, DEFAULT_CHANNELS));
    nsresult rv = Init(DEFAULT_CHANNELS);
    if (NS_FAILED(rv)) {
      TRACK_LOG(LogLevel::Error,
                ("[AudioTrackEncoder {}]: Default-channel-init failed.",
                 fmt::ptr(this)));
      OnError();
      return;
    }
  }
}

void AudioTrackEncoder::Cancel() {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  TRACK_LOG(LogLevel::Info,
            ("[AudioTrackEncoder {}]: Cancel()", fmt::ptr(this)));
  mCanceled = true;
  mEndOfStream = true;
  mOutgoingBuffer.Clear();
  mEncodedDataQueue.Finish();
}

void AudioTrackEncoder::NotifyEndOfStream() {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  TRACK_LOG(LogLevel::Info,
            ("[AudioTrackEncoder {}]: NotifyEndOfStream()", fmt::ptr(this)));

  if (!mCanceled && !mInitialized) {
    // If source audio track is completely silent till the end of encoding,
    // initialize the encoder with a default channel count.
    Init(DEFAULT_CHANNELS);
  }

  if (mEndOfStream) {
    return;
  }

  mEndOfStream = true;

  if (NS_FAILED(Encode(&mOutgoingBuffer))) {
    mOutgoingBuffer.Clear();
    OnError();
  }

  MOZ_ASSERT(mOutgoingBuffer.GetDuration() == 0);
}

/*static*/
void AudioTrackEncoder::InterleaveTrackData(AudioChunk& aChunk,
                                            int32_t aDuration,
                                            uint32_t aOutputChannels,
                                            AudioDataValue* aOutput) {
  uint32_t numChannelsToCopy = std::min(
      aOutputChannels, static_cast<uint32_t>(aChunk.mChannelData.Length()));
  switch (aChunk.mBufferFormat) {
    case AUDIO_FORMAT_S16: {
      AutoTArray<const int16_t*, 2> array;
      array.SetLength(numChannelsToCopy);
      for (uint32_t i = 0; i < array.Length(); i++) {
        array[i] = static_cast<const int16_t*>(aChunk.mChannelData[i]);
      }
      InterleaveTrackData(array, aDuration, aOutputChannels, aOutput,
                          aChunk.mVolume);
      break;
    }
    case AUDIO_FORMAT_FLOAT32: {
      AutoTArray<const float*, 2> array;
      array.SetLength(numChannelsToCopy);
      for (uint32_t i = 0; i < array.Length(); i++) {
        array[i] = static_cast<const float*>(aChunk.mChannelData[i]);
      }
      InterleaveTrackData(array, aDuration, aOutputChannels, aOutput,
                          aChunk.mVolume);
      break;
    }
    case AUDIO_FORMAT_SILENCE: {
      MOZ_ASSERT(false, "To implement.");
    }
  };
}

/*static*/
void AudioTrackEncoder::DeInterleaveTrackData(AudioDataValue* aInput,
                                              int32_t aDuration,
                                              int32_t aChannels,
                                              AudioDataValue* aOutput) {
  for (int32_t i = 0; i < aChannels; ++i) {
    for (int32_t j = 0; j < aDuration; ++j) {
      aOutput[i * aDuration + j] = aInput[i + j * aChannels];
    }
  }
}

size_t AudioTrackEncoder::SizeOfExcludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  return mOutgoingBuffer.SizeOfExcludingThis(aMallocSizeOf);
}

VideoTrackEncoder::VideoTrackEncoder(
    RefPtr<DriftCompensator> aDriftCompensator, TrackRate aTrackRate,
    MediaQueue<EncodedFrame>& aEncodedDataQueue,
    FrameDroppingMode aFrameDroppingMode)
    : TrackEncoder(aTrackRate, aEncodedDataQueue),
      mDriftCompensator(std::move(aDriftCompensator)),
      mEncodedTicks(0),
      mVideoBitrate(0),
      mFrameDroppingMode(aFrameDroppingMode),
      mEnabled(true) {
  mLastChunk.mDuration = 0;
}

void VideoTrackEncoder::Suspend(const TimeStamp& aTime) {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  TRACK_LOG(
      LogLevel::Info,
      ("[VideoTrackEncoder {}]: Suspend() at {:.3f}s, was {}", fmt::ptr(this),
       mStartTime.IsNull() ? 0.0 : (aTime - mStartTime).ToSeconds(),
       mSuspended ? "suspended" : "live"));

  if (mSuspended) {
    return;
  }

  mSuspended = true;
  mSuspendTime = aTime;
}

void VideoTrackEncoder::Resume(const TimeStamp& aTime) {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());

  if (!mSuspended) {
    return;
  }

  TRACK_LOG(
      LogLevel::Info,
      ("[VideoTrackEncoder {}]: Resume() after {:.3f}s, was {}", fmt::ptr(this),
       (aTime - mSuspendTime).ToSeconds(), mSuspended ? "suspended" : "live"));

  mSuspended = false;

  TimeDuration suspendDuration = aTime - mSuspendTime;
  if (!mLastChunk.mTimeStamp.IsNull()) {
    VideoChunk* nextChunk = mIncomingBuffer.FindChunkContaining(aTime);
    MOZ_ASSERT_IF(nextChunk, nextChunk->mTimeStamp <= aTime);
    if (nextChunk) {
      nextChunk->mTimeStamp = aTime;
    }
    mLastChunk.mTimeStamp += suspendDuration;
  }
  if (!mStartTime.IsNull()) {
    mStartTime += suspendDuration;
  }

  mSuspendTime = TimeStamp();
}

void VideoTrackEncoder::Disable(const TimeStamp& aTime) {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  TRACK_LOG(LogLevel::Debug,
            ("[VideoTrackEncoder {}]: Disable()", fmt::ptr(this)));

  if (mStartTime.IsNull()) {
    // We haven't started yet. No need to touch future frames.
    mEnabled = false;
    return;
  }

  // Advancing currentTime to process any frames in mIncomingBuffer between
  // mCurrentTime and aTime.
  AdvanceCurrentTime(aTime);
  if (!mLastChunk.mTimeStamp.IsNull()) {
    // Insert a black frame at t=aTime into mIncomingBuffer, to trigger the
    // shift to black at the right moment.
    VideoSegment tempSegment;
    tempSegment.AppendFrom(&mIncomingBuffer);
    mIncomingBuffer.AppendFrame(do_AddRef(mLastChunk.mFrame.GetImage()),
                                mLastChunk.mFrame.GetIntrinsicSize(),
                                mLastChunk.mFrame.GetPrincipalHandle(), true,
                                aTime);
    mIncomingBuffer.AppendFrom(&tempSegment);
  }
  mEnabled = false;
}

void VideoTrackEncoder::Enable(const TimeStamp& aTime) {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  TRACK_LOG(LogLevel::Debug,
            ("[VideoTrackEncoder {}]: Enable()", fmt::ptr(this)));

  if (mStartTime.IsNull()) {
    // We haven't started yet. No need to touch future frames.
    mEnabled = true;
    return;
  }

  // Advancing currentTime to process any frames in mIncomingBuffer between
  // mCurrentTime and aTime.
  AdvanceCurrentTime(aTime);
  if (!mLastChunk.mTimeStamp.IsNull()) {
    // Insert a real frame at t=aTime into mIncomingBuffer, to trigger the
    // shift from black at the right moment.
    VideoSegment tempSegment;
    tempSegment.AppendFrom(&mIncomingBuffer);
    mIncomingBuffer.AppendFrame(do_AddRef(mLastChunk.mFrame.GetImage()),
                                mLastChunk.mFrame.GetIntrinsicSize(),
                                mLastChunk.mFrame.GetPrincipalHandle(),
                                mLastChunk.mFrame.GetForceBlack(), aTime);
    mIncomingBuffer.AppendFrom(&tempSegment);
  }
  mEnabled = true;
}

void VideoTrackEncoder::AppendVideoSegment(VideoSegment&& aSegment) {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  TRACK_LOG(LogLevel::Verbose,
            ("[VideoTrackEncoder {}]: AppendVideoSegment()", fmt::ptr(this)));

  if (mCanceled) {
    return;
  }

  if (mEndOfStream) {
    return;
  }

  for (VideoSegment::ConstChunkIterator iter(aSegment); !iter.IsEnded();
       iter.Next()) {
    if (iter->IsNull()) {
      // A null image was sent. This is a signal from the source that we should
      // clear any images buffered in the future.
      mIncomingBuffer.Clear();
      continue;  // Don't append iter, as it is null.
    }
    if (VideoChunk* c = mIncomingBuffer.GetLastChunk()) {
      if (iter->mTimeStamp < c->mTimeStamp) {
        // Time went backwards. This can happen when a MediaDecoder seeks.
        // We need to handle this by removing any frames buffered in the future
        // and start over at iter->mTimeStamp.
        mIncomingBuffer.Clear();
      }
    }
    SetStarted();
    mIncomingBuffer.AppendFrame(do_AddRef(iter->mFrame.GetImage()),
                                iter->mFrame.GetIntrinsicSize(),
                                iter->mFrame.GetPrincipalHandle(),
                                iter->mFrame.GetForceBlack(), iter->mTimeStamp);
  }
  aSegment.Clear();
}

void VideoTrackEncoder::Init(const VideoSegment& aSegment,
                             const TimeStamp& aTime,
                             size_t aFrameRateDetectionMinChunks) {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  MOZ_ASSERT(!aTime.IsNull());

  if (mInitialized) {
    return;
  }

  mInitCounter++;
  TRACK_LOG(LogLevel::Debug,
            ("[VideoTrackEncoder {}]: Init the video encoder {} times",
             fmt::ptr(this), mInitCounter));

  Maybe<float> framerate;
  if (!aSegment.IsEmpty()) {
    // The number of whole frames, i.e., with known duration.
    size_t frameCount = 0;
    RollingMean<TimeDuration, TimeDuration> meanDuration(
        FRAMERATE_DETECTION_ROLLING_WINDOW);
    VideoSegment::ConstChunkIterator iter(aSegment);
    TimeStamp previousChunkTime = iter->mTimeStamp;
    iter.Next();
    for (; !iter.IsEnded(); iter.Next(), ++frameCount) {
      meanDuration.insert(iter->mTimeStamp - previousChunkTime);
      previousChunkTime = iter->mTimeStamp;
    }
    TRACK_LOG(LogLevel::Debug, ("[VideoTrackEncoder {}]: Init() frameCount={}",
                                fmt::ptr(this), frameCount));
    if (frameCount >= aFrameRateDetectionMinChunks) {
      if (meanDuration.empty()) {
        // No whole frames available, use aTime as end time.
        framerate = Some(1.0f / (aTime - mStartTime).ToSeconds());
      } else {
        // We want some frames for estimating the framerate.
        framerate = Some(1.0f / meanDuration.mean().ToSeconds());
      }
    } else if ((aTime - mStartTime).ToSeconds() >
               FRAMERATE_DETECTION_MAX_DURATION_S) {
      // Instead of failing init after the fail-timeout, we fallback to a very
      // low rate.
      framerate = Some(static_cast<float>(frameCount) /
                       (aTime - mStartTime).ToSeconds());
    }
  }

  if (framerate) {
    for (VideoSegment::ConstChunkIterator iter(aSegment); !iter.IsEnded();
         iter.Next()) {
      if (iter->IsNull()) {
        continue;
      }

      gfx::IntSize imgsize = iter->mFrame.GetImage()->GetSize();
      gfx::IntSize intrinsicSize = iter->mFrame.GetIntrinsicSize();
      nsresult rv = Init(imgsize.width, imgsize.height, intrinsicSize.width,
                         intrinsicSize.height, *framerate);

      if (NS_SUCCEEDED(rv)) {
        TRACK_LOG(LogLevel::Info,
                  ("[VideoTrackEncoder {}]: Successfully initialized!",
                   fmt::ptr(this)));
        return;
      }

      TRACK_LOG(LogLevel::Error,
                ("[VideoTrackEncoder {}]: Failed to initialize the encoder!",
                 fmt::ptr(this)));
      OnError();
      break;
    }
  }

  if (((aTime - mStartTime).ToSeconds() > VIDEO_INIT_FAILED_DURATION) &&
      mInitCounter > 1) {
    TRACK_LOG(LogLevel::Warning,
              ("[VideoTrackEncoder {}]: No successful init for {}s.",
               fmt::ptr(this), VIDEO_INIT_FAILED_DURATION));
    OnError();
    return;
  }
}

void VideoTrackEncoder::Cancel() {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  TRACK_LOG(LogLevel::Info,
            ("[VideoTrackEncoder {}]: Cancel()", fmt::ptr(this)));
  mCanceled = true;
  mEndOfStream = true;
  mIncomingBuffer.Clear();
  mOutgoingBuffer.Clear();
  mLastChunk.SetNull(0);
  mEncodedDataQueue.Finish();
}

void VideoTrackEncoder::NotifyEndOfStream() {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());

  if (mCanceled) {
    return;
  }

  if (mEndOfStream) {
    // We have already been notified.
    return;
  }

  mEndOfStream = true;
  TRACK_LOG(LogLevel::Info,
            ("[VideoTrackEncoder {}]: NotifyEndOfStream()", fmt::ptr(this)));

  if (!mLastChunk.IsNull()) {
    RefPtr<layers::Image> lastImage = mLastChunk.mFrame.GetImage();
    const TimeStamp now = TimeStamp::Now();
    TimeStamp currentTime = mSuspended ? mSuspendTime : mCurrentTime;
    currentTime = mDriftCompensator->GetVideoTime(now, currentTime);
    TimeDuration absoluteEndTime = currentTime - mStartTime;
    CheckedInt64 duration =
        CheckedInt64(absoluteEndTime.ToTicksAtRate(mTrackRate)) - mEncodedTicks;
    if (duration.isValid() && duration.value() > 0) {
      mEncodedTicks += duration.value();
      TRACK_LOG(LogLevel::Debug,
                ("[VideoTrackEncoder {}]: Appending last video frame {} at pos "
                 "{:.3f}s, "
                 "track-end={:.3f}s",
                 fmt::ptr(this), fmt::ptr(lastImage.get()),
                 (mLastChunk.mTimeStamp - mStartTime).ToSeconds(),
                 absoluteEndTime.ToSeconds()));
      mOutgoingBuffer.AppendFrame(
          lastImage.forget(), mLastChunk.mFrame.GetIntrinsicSize(),
          PRINCIPAL_HANDLE_NONE, mLastChunk.mFrame.GetForceBlack() || !mEnabled,
          mLastChunk.mTimeStamp);
      mOutgoingBuffer.ExtendLastFrameBy(duration.value());
    }

    if (!mInitialized) {
      // Try to init without waiting for an accurate framerate.
      Init(mOutgoingBuffer, currentTime, 0);
    }
  }

  if (mCanceled) {
    // Previous Init failed and we got canceled. Nothing to do here.
    return;
  }

  mIncomingBuffer.Clear();
  mLastChunk.SetNull(0);

  if (NS_WARN_IF(!mInitialized)) {
    // Still not initialized. There was probably no real frame at all, perhaps
    // by muting. Initialize the encoder with default frame width, frame
    // height, and frame rate.
    Init(DEFAULT_FRAME_WIDTH, DEFAULT_FRAME_HEIGHT, DEFAULT_FRAME_WIDTH,
         DEFAULT_FRAME_HEIGHT, DEFAULT_FRAME_RATE);
  }

  if (NS_FAILED(Encode(&mOutgoingBuffer))) {
    OnError();
  }

  MOZ_ASSERT(mOutgoingBuffer.IsEmpty());
}

void VideoTrackEncoder::SetStartOffset(const TimeStamp& aStartOffset) {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  MOZ_ASSERT(mCurrentTime.IsNull());
  TRACK_LOG(LogLevel::Info,
            ("[VideoTrackEncoder {}]: SetStartOffset()", fmt::ptr(this)));
  mStartTime = aStartOffset;
  mCurrentTime = aStartOffset;
}

void VideoTrackEncoder::AdvanceCurrentTime(const TimeStamp& aTime) {
  AUTO_PROFILER_LABEL("VideoTrackEncoder::AdvanceCurrentTime", OTHER);

  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  MOZ_ASSERT(!mStartTime.IsNull());
  MOZ_ASSERT(!mCurrentTime.IsNull());

  if (mCanceled) {
    return;
  }

  if (mEndOfStream) {
    return;
  }

  if (mSuspended) {
    TRACK_LOG(
        LogLevel::Verbose,
        ("[VideoTrackEncoder {}]: AdvanceCurrentTime() suspended at {:.3f}s",
         fmt::ptr(this), (mCurrentTime - mStartTime).ToSeconds()));
    mCurrentTime = aTime;
    mIncomingBuffer.ForgetUpToTime(mCurrentTime);
    return;
  }

  TRACK_LOG(LogLevel::Verbose,
            ("[VideoTrackEncoder {}]: AdvanceCurrentTime() to {:.3f}s",
             fmt::ptr(this), (aTime - mStartTime).ToSeconds()));

  // Grab frames within the currentTime range from the incoming buffer.
  VideoSegment tempSegment;
  {
    VideoChunk* previousChunk = &mLastChunk;
    auto appendDupes = [&](const TimeStamp& aUpTo) {
      while ((aUpTo - previousChunk->mTimeStamp).ToSeconds() > 1.0) {
        // We encode at least one frame per second, even if there are none
        // flowing.
        previousChunk->mTimeStamp += TimeDuration::FromSeconds(1);
        tempSegment.AppendFrame(
            do_AddRef(previousChunk->mFrame.GetImage()),
            previousChunk->mFrame.GetIntrinsicSize(),
            previousChunk->mFrame.GetPrincipalHandle(),
            previousChunk->mFrame.GetForceBlack() || !mEnabled,
            previousChunk->mTimeStamp);
        TRACK_LOG(LogLevel::Verbose,
                  ("[VideoTrackEncoder {}]: Duplicating video frame ({}) at "
                   "pos {:.3f}",
                   fmt::ptr(this), fmt::ptr(previousChunk->mFrame.GetImage()),
                   (previousChunk->mTimeStamp - mStartTime).ToSeconds()));
      }
    };
    for (VideoSegment::ChunkIterator iter(mIncomingBuffer); !iter.IsEnded();
         iter.Next()) {
      MOZ_ASSERT(!iter->IsNull());
      if (!previousChunk->IsNull() &&
          iter->mTimeStamp <= previousChunk->mTimeStamp) {
        // This frame starts earlier than previousChunk. Skip.
        continue;
      }
      if (iter->mTimeStamp >= aTime) {
        // This frame starts in the future. Stop.
        break;
      }
      if (!previousChunk->IsNull()) {
        appendDupes(iter->mTimeStamp);
      }
      tempSegment.AppendFrame(
          do_AddRef(iter->mFrame.GetImage()), iter->mFrame.GetIntrinsicSize(),
          iter->mFrame.GetPrincipalHandle(),
          iter->mFrame.GetForceBlack() || !mEnabled, iter->mTimeStamp);
      TRACK_LOG(
          LogLevel::Verbose,
          ("[VideoTrackEncoder {}]: Taking video frame ({}) at pos {:.3f}",
           fmt::ptr(this), fmt::ptr(iter->mFrame.GetImage()),
           (iter->mTimeStamp - mStartTime).ToSeconds()));
      previousChunk = &*iter;
    }
    if (!previousChunk->IsNull()) {
      appendDupes(aTime);
    }
  }
  mCurrentTime = aTime;
  mIncomingBuffer.ForgetUpToTime(mCurrentTime);

  // Convert tempSegment timestamps to durations and add chunks with known
  // duration to mOutgoingBuffer.
  const TimeStamp now = TimeStamp::Now();
  for (VideoSegment::ConstChunkIterator iter(tempSegment); !iter.IsEnded();
       iter.Next()) {
    VideoChunk chunk = *iter;

    if (mLastChunk.mTimeStamp.IsNull()) {
      // This is the first real chunk in the track. Make it start at the
      // beginning of the track.
      MOZ_ASSERT(!iter->mTimeStamp.IsNull());

      TRACK_LOG(LogLevel::Verbose,
                ("[VideoTrackEncoder {}]: Got the first video frame ({}) at "
                 "pos {:.3f} "
                 "(moving it to beginning)",
                 fmt::ptr(this), fmt::ptr(iter->mFrame.GetImage()),
                 (iter->mTimeStamp - mStartTime).ToSeconds()));

      mLastChunk = *iter;
      mLastChunk.mTimeStamp = mStartTime;
      continue;
    }

    MOZ_ASSERT(!mLastChunk.IsNull());
    MOZ_ASSERT(!chunk.IsNull());

    TimeDuration absoluteEndTime =
        mDriftCompensator->GetVideoTime(now, chunk.mTimeStamp) - mStartTime;
    TRACK_LOG(
        LogLevel::Verbose,
        ("[VideoTrackEncoder {}]: Appending video frame {}, at pos {:.3f}s "
         "until {:.3f}s",
         fmt::ptr(this), fmt::ptr(mLastChunk.mFrame.GetImage()),
         (mDriftCompensator->GetVideoTime(now, mLastChunk.mTimeStamp) -
          mStartTime)
             .ToSeconds(),
         absoluteEndTime.ToSeconds()));
    CheckedInt64 duration =
        CheckedInt64(absoluteEndTime.ToTicksAtRate(mTrackRate)) - mEncodedTicks;
    if (!duration.isValid()) {
      NS_ERROR("Duration overflow");
      return;
    }

    if (duration.value() <= 0) {
      // A frame either started before the last frame (can happen when
      // multiple frames are added before SetStartOffset), or
      // two frames were so close together that they ended up at the same
      // position. We handle both cases by ignoring the previous frame.

      TRACK_LOG(LogLevel::Verbose,
                ("[VideoTrackEncoder {}]: Duration from frame {} to frame {} "
                 "is {}. Ignoring {}",
                 fmt::ptr(this), fmt::ptr(mLastChunk.mFrame.GetImage()),
                 fmt::ptr(iter->mFrame.GetImage()), duration.value(),
                 fmt::ptr(mLastChunk.mFrame.GetImage())));

      TimeStamp t = mLastChunk.mTimeStamp;
      mLastChunk = *iter;
      mLastChunk.mTimeStamp = t;
      continue;
    }

    mEncodedTicks += duration.value();
    mOutgoingBuffer.AppendFrame(
        do_AddRef(mLastChunk.mFrame.GetImage()),
        mLastChunk.mFrame.GetIntrinsicSize(), PRINCIPAL_HANDLE_NONE,
        mLastChunk.mFrame.GetForceBlack() || !mEnabled, mLastChunk.mTimeStamp);
    mOutgoingBuffer.ExtendLastFrameBy(duration.value());
    mLastChunk = chunk;
  }

  if (mOutgoingBuffer.IsEmpty()) {
    return;
  }

  Init(mOutgoingBuffer, mCurrentTime, FRAMERATE_DETECTION_MIN_CHUNKS);

  if (!mInitialized) {
    return;
  }

  if (NS_FAILED(Encode(&mOutgoingBuffer))) {
    OnError();
    return;
  }

  MOZ_ASSERT(mOutgoingBuffer.IsEmpty());
}

size_t VideoTrackEncoder::SizeOfExcludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) {
  MOZ_ASSERT(!mWorkerThread || mWorkerThread->IsCurrentThreadIn());
  return mIncomingBuffer.SizeOfExcludingThis(aMallocSizeOf) +
         mOutgoingBuffer.SizeOfExcludingThis(aMallocSizeOf);
}

}  // namespace mozilla

#undef TRACK_LOG
