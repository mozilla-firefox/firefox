/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioSink.h"

#include "AudioConverter.h"
#include "AudioDeviceInfo.h"
#include "MediaQueue.h"
#include "Tracing.h"
#include "VideoUtils.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/ProfilerMarkerTypes.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_media.h"
#include "nsPrintfCString.h"

namespace mozilla {

mozilla::LazyLogModule gAudioSinkLog("AudioSink");
#define SINK_LOG(msg, ...)                                         \
  MOZ_LOG_FMT(gAudioSinkLog, LogLevel::Debug, "AudioSink={} " msg, \
              fmt::ptr(this), ##__VA_ARGS__)
#define SINK_LOG_V(msg, ...)                                         \
  MOZ_LOG_FMT(gAudioSinkLog, LogLevel::Verbose, "AudioSink={} " msg, \
              fmt::ptr(this), ##__VA_ARGS__)

// The amount of audio frames that is used to fuzz rounding errors.
static const int64_t AUDIO_FUZZ_FRAMES = 1;

using media::TimeUnit;

AudioSink::AudioSink(AbstractThread* aThread,
                     MediaQueue<AudioData>& aAudioQueue, const AudioInfo& aInfo,
                     bool aShouldResistFingerprinting)
    : mOwnerThread(aThread),
      mOutputRate(
          DecideAudioPlaybackSampleRate(aInfo, aShouldResistFingerprinting)),
      mOutputChannels(AudioPlaybackChannels(aInfo)),
      mAudibilityMonitor(
          mOutputRate,
          StaticPrefs::dom_media_silence_duration_for_audibility()),
      mAudioQueue(aAudioQueue),
      mProcessedQueueThresholdMS(
          StaticPrefs::media_audio_audiosink_threshold_ms()) {
  InitPlaybackState();
  // Not much to initialize here if there's no audio.
  if (!aInfo.IsValid()) {
    mProcessedSPSCQueue = std::make_unique<SPSCQueue<AudioDataValue>>(0);
    return;
  }
  // Twice the limit that trigger a refill.
  double capacitySeconds = mProcessedQueueThresholdMS / 1000.f * 2;
  // Clamp to correct boundaries, and align on the channel count
  int elementCount = static_cast<int>(
      std::clamp(capacitySeconds * mOutputChannels * mOutputRate, 0.,
                 std::numeric_limits<int>::max() - 1.));
  elementCount -= elementCount % mOutputChannels;
  mProcessedSPSCQueue =
      std::make_unique<SPSCQueue<AudioDataValue>>(elementCount);
  SINK_LOG("Ringbuffer has space for {} elements ({} seconds)",
           mProcessedSPSCQueue->Capacity(),
           static_cast<float>(elementCount) / mOutputChannels / mOutputRate);
  // Determine if the data is likely to be audible when the stream will be
  // ready, if possible.
  RefPtr<AudioData> frontPacket = mAudioQueue.PeekFront();
  if (frontPacket) {
    mAudibilityMonitor.ProcessInterleaved(frontPacket->Data(),
                                          frontPacket->mChannels);
    mIsAudioDataAudible = mAudibilityMonitor.RecentlyAudible();
    SINK_LOG("New AudioSink -- audio is likely to be {}",
             mIsAudioDataAudible ? "audible" : "inaudible");
  } else {
    // If no packets are available, consider the audio audible.
    mIsAudioDataAudible = true;
    SINK_LOG(
        "New AudioSink -- no audio packet avaialble, considering the stream "
        "audible");
  }
}

AudioSink::~AudioSink() {
  // Generally instances of AudioSink should be properly shut down manually.
  // The only way deleting an AudioSink without shutdown can happen is if the
  // dispatch back to the MDSM thread after initializing it asynchronously
  // fails. When that's the case, the stream has been initialized but not
  // started. Manually shutdown the AudioStream in this case.
  if (mAudioStream) {
    mAudioStream->ShutDown();
  }
}

nsresult AudioSink::InitializeAudioStream(
    const RefPtr<AudioDeviceInfo>& aAudioDevice,
    AudioSink::InitializationType aInitializationType) {
  if (aInitializationType == AudioSink::InitializationType::UNMUTING) {
    // Consider the stream to be audible immediately, before initialization
    // finishes when unmuting, in case initialization takes some time and it
    // looked audible when the AudioSink was created.
    mAudibleEvent.Notify(mIsAudioDataAudible);
    SINK_LOG("InitializeAudioStream (Unmuting) notifying that audio is {}",
             mIsAudioDataAudible ? "audible" : "inaudible");
  } else {
    // If not unmuting, the audibility event will be dispatched as usual,
    // inspecting the audio content as it's being played and signaling the
    // audibility event when a different in state is detected.
    SINK_LOG("InitializeAudioStream (initial)");
    mIsAudioDataAudible = false;
  }

  // When AudioQueue is empty, there is no way to know the channel layout of
  // the coming audio data, so we use the predefined channel map instead.
  AudioConfig::ChannelLayout::ChannelMap channelMap =
      AudioConfig::ChannelLayout(mOutputChannels).Map();
  // The layout map used here is already processed by mConverter with
  // mOutputChannels into SMPTE format, so there is no need to worry if
  // StaticPrefs::accessibility_monoaudio_enable() or
  // StaticPrefs::media_forcestereo_enabled() is applied.
  MOZ_ASSERT(!mAudioStream);
  mAudioStream =
      new AudioStream(*this, mOutputRate, mOutputChannels, channelMap);
  nsresult rv = mAudioStream->Init(aAudioDevice);
  if (NS_FAILED(rv)) {
    mAudioStream->ShutDown();
    mAudioStream = nullptr;
    return rv;
  }

  return NS_OK;
}

RefPtr<MediaSink::EndedPromise> AudioSink::Start(
    const PlaybackParams& aParams, const media::TimeUnit& aStartTime) {
  MOZ_ASSERT(mOwnerThread->IsCurrentThreadIn());

  // Set playback params before calling Start() so they can take effect
  // as soon as the 1st DataCallback of the AudioStream fires.
  ApplyPlaybackParams(aParams);
  ConnectAudioQueues();

  mStartTime = aStartTime;

  // To ensure at least one audio packet will be popped from AudioQueue and
  // ready to be played.
  NotifyAudioNeeded();

  return mAudioStream->Start();
}

void AudioSink::InitPlaybackState() {
  // The fields reset here are safe to modify while the audio callback thread is
  // running concurrently.
  mPlaying = true;
  mWritten = 0;
  mErrored = false;
  mFramesParsed = 0;
  mIsAudioDataAudible = false;
  mProcessedQueueFinished = false;
}

void AudioSink::ApplyPlaybackParams(const PlaybackParams& aParams) {
  MOZ_ASSERT(mOwnerThread->IsCurrentThreadIn());
  mAudioStream->SetVolume(aParams.mVolume);
  mAudioStream->SetPlaybackRate(aParams.mPlaybackRate);
  mAudioStream->SetPreservesPitch(aParams.mPreservesPitch);
  mAudioStream->SetStreamName(aParams.mStreamName);
}

void AudioSink::ConnectAudioQueues() {
  MOZ_ASSERT(mOwnerThread->IsCurrentThreadIn());
  mAudioQueueListener = mAudioQueue.PushEvent().Connect(
      mOwnerThread, this, &AudioSink::OnAudioPushed);
  mAudioQueueFinishListener = mAudioQueue.FinishEvent().Connect(
      mOwnerThread, this, &AudioSink::NotifyAudioNeeded);
  mProcessedQueueListener =
      mAudioPopped.Connect(mOwnerThread, this, &AudioSink::OnAudioPopped);
}

TimeUnit AudioSink::GetPosition() {
  int64_t tmp;
  if (mAudioStream && (tmp = mAudioStream->GetPosition()) >= 0) {
    TimeUnit pos = TimeUnit::FromMicroseconds(tmp);
    NS_ASSERTION(pos >= mLastGoodPosition,
                 "AudioStream position shouldn't go backward");
    TimeUnit tmp = mStartTime + pos;
    if (!tmp.IsValid()) {
      mErrored = true;
      return mStartTime + mLastGoodPosition;
    }
    // Update the last good position when we got a good one.
    if (pos >= mLastGoodPosition) {
      mLastGoodPosition = pos;
    }
  }

  return mStartTime + mLastGoodPosition;
}

bool AudioSink::HasUnplayedFrames() {
  // Experimentation suggests that GetPositionInFrames() is zero-indexed,
  // so we need to add 1 here before comparing it to mWritten.
  return mProcessedSPSCQueue->AvailableRead() ||
         (mAudioStream && mAudioStream->GetPositionInFrames() + 1 < mWritten);
}

TimeUnit AudioSink::UnplayedDuration() const {
  return TimeUnit::FromMicroseconds(AudioQueuedInRingBufferMS());
}

void AudioSink::ReenqueueUnplayedAudioDataIfNeeded() {
  // This is OK: the AudioStream has been shut down. ShutDown guarantees that
  // the audio callback thread won't call back again.
  mProcessedSPSCQueue->ResetConsumerThreadId();

  // construct an AudioData
  int sampleInRingbuffer = mProcessedSPSCQueue->AvailableRead();

  if (!sampleInRingbuffer) {
    return;
  }

  uint32_t channelCount;
  uint32_t rate;
  if (mConverter) {
    channelCount = mConverter->OutputConfig().Channels();
    rate = mConverter->OutputConfig().Rate();
  } else {
    channelCount = mOutputChannels;
    rate = mOutputRate;
  }

  uint32_t framesRemaining = sampleInRingbuffer / channelCount;

  nsTArray<AlignedAudioBuffer> packetsToReenqueue;
  RefPtr<AudioData> frontPacket = mAudioQueue.PeekFront();
  uint32_t offset;
  TimeUnit time;
  uint32_t typicalPacketFrameCount = 1024;  // typical for e.g. AAC
  // Extrapolate mOffset, mTime from the front of the queue
  // We can't really find a good value for `mOffset`, so we take what we have
  // at the front of the queue.
  // For `mTime`, assume there hasn't been a discontinuity recently.
  if (!frontPacket) {
    // We do our best here, but it's not going to be perfect.
    offset = 0;
    time = GetPosition();
  } else {
    if (frontPacket->Frames()) {
      typicalPacketFrameCount = frontPacket->Frames();
    }
    offset = frontPacket->mOffset;
    time = frontPacket->mTime;
  }

  // Extract all audio data from the ring buffer, we can only read the data from
  // the most recent, so we reenqueue the data, packetized, in a temporary
  // array.
  while (framesRemaining) {
    uint32_t packetFrameCount =
        std::min(framesRemaining, typicalPacketFrameCount);
    framesRemaining -= packetFrameCount;

    int packetSampleCount = packetFrameCount * channelCount;
    AlignedAudioBuffer packetData(packetSampleCount);
    DebugOnly<int> samplesRead =
        mProcessedSPSCQueue->Dequeue(packetData.Data(), packetSampleCount);
    MOZ_ASSERT(samplesRead == packetSampleCount);

    packetsToReenqueue.AppendElement(packetData);
  }
  // Reenqueue in the audio queue in correct order in the audio queue, starting
  // with the end of the temporary array.
  while (!packetsToReenqueue.IsEmpty()) {
    auto packetData = packetsToReenqueue.PopLastElement();
    uint32_t packetFrameCount = packetData.Length() / channelCount;
    auto duration = TimeUnit(packetFrameCount, rate);
    if (!duration.IsValid()) {
      NS_WARNING("Int overflow in AudioSink");
      mErrored = true;
      return;
    }
    time -= duration;
    RefPtr<AudioData> packet =
        new AudioData(offset, time, std::move(packetData), channelCount, rate);
    MOZ_DIAGNOSTIC_ASSERT(duration == packet->mDuration, "must be equal");

    SINK_LOG(
        "Muting: Pushing back {} frames ({}ms) from the ring buffer back into "
        "the audio queue at pts {}",
        packetFrameCount, 1000 * static_cast<float>(packetFrameCount) / rate,
        time.ToSeconds());
    // The audio data's timestamp would be adjusted already if we're in looping,
    // so we don't want to adjust them again.
    mAudioQueue.PushFront(packet,
                          MediaQueue<AudioData>::TimestampAdjustment::Disable);
  }
}

void AudioSink::ShutDown() {
  MOZ_ASSERT(mOwnerThread->IsCurrentThreadIn());

  mAudioQueueListener.DisconnectIfExists();
  mAudioQueueFinishListener.DisconnectIfExists();
  mProcessedQueueListener.DisconnectIfExists();

  if (mAudioStream) {
    mAudioStream->ShutDown();
    mAudioStream = nullptr;
    ReenqueueUnplayedAudioDataIfNeeded();
  }
  mStoppedForSeek = false;
  mProcessedQueueFinished = true;
}

void AudioSink::PrepareForReuse() {
  MOZ_ASSERT(mOwnerThread->IsCurrentThreadIn());
  MOZ_ASSERT(mAudioStream);
  SINK_LOG("PrepareForReuse: dropping queued pre-seek audio for seek reuse");

  // Detach from the audio queues so the sink doesn't react to the decode reset
  // during the seek, and clear the ring buffer by setting
  // mDiscardUpToSampleCount, because that data is pre-seek and the stream must
  // only output silence during seeking.
  mAudioQueueListener.DisconnectIfExists();
  mAudioQueueFinishListener.DisconnectIfExists();
  mProcessedQueueListener.DisconnectIfExists();
  mStoppedForSeek = true;
  mDiscardUpToSampleCount = mTotalSamplesPushed;
}

RefPtr<MediaSink::EndedPromise> AudioSink::ResetForReuse(
    const PlaybackParams& aParams, const media::TimeUnit& aStartTime) {
  MOZ_ASSERT(mOwnerThread->IsCurrentThreadIn());
  MOZ_ASSERT(mAudioStream);
  MOZ_ASSERT(mDiscardUpToSampleCount == mTotalSamplesPushed,
             "Nothing may be pushed while the sink is stopped for a seek");
  SINK_LOG("ResetForReuse to start time {}", aStartTime.ToMicroseconds());

  // This is safe because the sink is shut down, and all event listeners that
  // push to this queue are disconnected at this time and nobody can be writing
  // to it.
  mProcessedSPSCQueue->ResetProducerThreadId();

  ApplyPlaybackParams(aParams);

  // Re-establish the same initial playback accounting as a freshly constructed
  // sink, including resetting audibility so it is re-evaluated from the
  // post-seek audio by the existing CheckIsAudible mechanism. This runs while
  // the audio callback is still active across the seek; see InitPlaybackState.
  InitPlaybackState();

  // Reset the seek-specific position state.
  mStartTime = aStartTime;
  mLastEndTime = TimeUnit::Zero();
  mLastGoodPosition = TimeUnit::Zero();
  mLastProcessedPacket = Nothing();
  mConverter = nullptr;

  mAudioStream->Resume();

  // Must precede ConnectAudioQueues() and NotifyAudioNeeded(); see
  // AudioClock::Rebase.
  mAudioStream->RebaseLive();

  ConnectAudioQueues();

  // Ensure at least one post-seek packet is converted and ready to play.
  NotifyAudioNeeded();

  mStoppedForSeek = false;

  return mAudioStream->ReinitEndedPromise();
}

void AudioSink::SetVolume(double aVolume) {
  if (mAudioStream) {
    mAudioStream->SetVolume(aVolume);
  }
}

void AudioSink::SetStreamName(const nsAString& aStreamName) {
  if (mAudioStream) {
    mAudioStream->SetStreamName(aStreamName);
  }
}

void AudioSink::SetPlaybackRate(double aPlaybackRate) {
  MOZ_ASSERT(aPlaybackRate != 0,
             "Don't set the playbackRate to 0 on AudioStream");
  if (mAudioStream) {
    mAudioStream->SetPlaybackRate(aPlaybackRate);
  }
}

void AudioSink::SetPreservesPitch(bool aPreservesPitch) {
  if (mAudioStream) {
    mAudioStream->SetPreservesPitch(aPreservesPitch);
  }
}

void AudioSink::SetPlaying(bool aPlaying) {
  if (!mAudioStream || mAudioStream->IsPlaybackCompleted() ||
      mPlaying == aPlaying) {
    return;
  }
  // pause/resume AudioStream as necessary.
  if (!aPlaying) {
    mAudioStream->Pause();
  } else if (aPlaying) {
    mAudioStream->Resume();
  }
  mPlaying = aPlaying;
}

TimeUnit AudioSink::GetEndTime() const {
  uint64_t written = mWritten;
  TimeUnit played = media::TimeUnit(written, mOutputRate) + mStartTime;
  if (!played.IsValid()) {
    NS_WARNING("Int overflow calculating audio end time");
    return TimeUnit::Zero();
  }
  // As we may be resampling, rounding errors may occur. Ensure we never get
  // past the original end time.
  return std::min(mLastEndTime, played);
}

uint32_t AudioSink::PopFrames(AudioDataValue* aBuffer, uint32_t aFrames,
                              bool aAudioThreadChanged) {
  // This is safe, because we have the guarantee, by the OS, that audio
  // callbacks are never called concurrently. Audio thread changes can only
  // happen when not using cubeb remoting, and often when changing audio device
  // at the system level.
  if (aAudioThreadChanged) {
    mProcessedSPSCQueue->ResetConsumerThreadId();
  }

  // Drop the stale pre-seek audio still queued when the stream was reused
  // without stopping, so only post-seek audio plays. The boundary is an
  // absolute push count; dropping until this thread's own popped count reaches
  // it is race-free against the concurrent producer (a remaining-count would
  // not be).
  if (int64_t discardUntil = mDiscardUpToSampleCount;
      mTotalSamplesPopped < discardUntil) {
    // Skip without copying: a null destination advances the read index with no
    // buffer or memcpy on the realtime audio thread, capped at what is
    // available.
    int toDrop = static_cast<int>(discardUntil - mTotalSamplesPopped);
    int dropped = mProcessedSPSCQueue->Dequeue(nullptr, toDrop);
    mTotalSamplesPopped += dropped;
    SINK_LOG_V(
        "Dropping stale pre-seek audio: requested {}, dropped {} samples",
        toDrop, dropped);
  }

  TRACE_COMMENT("AudioSink::PopFrames", "%u frames (ringbuffer: %u/%u)",
                aFrames, SampleToFrame(mProcessedSPSCQueue->AvailableRead()),
                SampleToFrame(mProcessedSPSCQueue->Capacity()));

  const int samplesToPop = static_cast<int>(aFrames * mOutputChannels);
  const int samplesRead = mProcessedSPSCQueue->Dequeue(aBuffer, samplesToPop);
  MOZ_ASSERT(samplesRead % mOutputChannels == 0);
  mTotalSamplesPopped += samplesRead;
  mWritten += SampleToFrame(samplesRead);
  if (samplesRead != samplesToPop) {
    if (Ended()) {
      SINK_LOG("Last PopFrames -- Source ended.");
    } else if (IsIntentionallySilent()) {
      SINK_LOG_V("Stopped for a seek, outputting silence.");
    } else {
      SINK_LOG("Underrun when popping samples from audiosink ring buffer.");
      TRACE_COMMENT("AudioSink::PopFrames", "Underrun %u frames missing",
                    SampleToFrame(samplesToPop - samplesRead));
    }
    // silence the rest
    PodZero(aBuffer + samplesRead, samplesToPop - samplesRead);
  }

  mAudioPopped.Notify();

  SINK_LOG_V(
      "Popping {} frames. Remaining in ringbuffer {} / {}, total popped {}\n",
      aFrames, SampleToFrame(mProcessedSPSCQueue->AvailableRead()),
      SampleToFrame(mProcessedSPSCQueue->Capacity()), mTotalSamplesPopped);
  CheckIsAudible(Span(aBuffer, samplesRead), mOutputChannels);

  return SampleToFrame(samplesRead);
}

bool AudioSink::Ended() const {
  // Return true when error encountered so AudioStream can start draining.
  // All atomic so we don't need locking.
  return mErrored || (!mStoppedForSeek && mProcessedQueueFinished);
}

void AudioSink::CheckIsAudible(const Span<AudioDataValue>& aInterleaved,
                               size_t aChannel) {
  mAudibilityMonitor.ProcessInterleaved(aInterleaved, aChannel);
  bool isAudible = mAudibilityMonitor.RecentlyAudible();

  if (isAudible != mIsAudioDataAudible) {
    mIsAudioDataAudible = isAudible;
    SINK_LOG("Notifying that audio is now {}",
             mIsAudioDataAudible ? "audible" : "inaudible");
    mAudibleEvent.Notify(mIsAudioDataAudible);
  }
}

void AudioSink::OnAudioPopped() {
  SINK_LOG_V("AudioStream has used an audio packet.");
  NotifyAudioNeeded();
}

void AudioSink::OnAudioPushed(const RefPtr<AudioData>& aSample) {
  SINK_LOG_V("One new audio packet available.");
  NotifyAudioNeeded();
}

uint32_t AudioSink::AudioQueuedInRingBufferMS() const {
  return static_cast<uint32_t>(
      1000 * SampleToFrame(mProcessedSPSCQueue->AvailableRead()) / mOutputRate);
}

uint32_t AudioSink::SampleToFrame(uint32_t aSamples) const {
  return aSamples / mOutputChannels;
}

void AudioSink::NotifyAudioNeeded() {
  MOZ_ASSERT(mOwnerThread->IsCurrentThreadIn(),
             "Not called from the owner's thread");

  while (mAudioQueue.GetSize() &&
         AudioQueuedInRingBufferMS() <
             static_cast<uint32_t>(mProcessedQueueThresholdMS)) {
    // Check if there's room in our ring buffer.
    if (mAudioQueue.PeekFront()->Frames() >
        SampleToFrame(mProcessedSPSCQueue->AvailableWrite())) {
      SINK_LOG_V("Can't push {} frames. In ringbuffer {} / {}\n",
                 mAudioQueue.PeekFront()->Frames(),
                 SampleToFrame(mProcessedSPSCQueue->AvailableRead()),
                 SampleToFrame(mProcessedSPSCQueue->Capacity()));
      return;
    }
    SINK_LOG_V("Pushing {} frames. In ringbuffer {} / {}\n",
               mAudioQueue.PeekFront()->Frames(),
               SampleToFrame(mProcessedSPSCQueue->AvailableRead()),
               SampleToFrame(mProcessedSPSCQueue->Capacity()));
    RefPtr<AudioData> data = mAudioQueue.PopFront();

    // Ignore the element with 0 frames and try next.
    if (!data->Frames()) {
      continue;
    }

    if (!mConverter ||
        (data->mRate != mConverter->InputConfig().Rate() ||
         data->mChannels != mConverter->InputConfig().Channels())) {
      SINK_LOG_V("Audio format changed from {}@{}Hz to {}@{}Hz",
                 mConverter ? mConverter->InputConfig().Channels() : 0,
                 mConverter ? mConverter->InputConfig().Rate() : 0,
                 data->mChannels, data->mRate);

      DrainConverter(SampleToFrame(mProcessedSPSCQueue->AvailableWrite()));

      // mFramesParsed indicates the current playtime in frames at the current
      // input sampling rate. Recalculate it per the new sampling rate.
      if (mFramesParsed) {
        // We minimize overflow.
        uint32_t oldRate = mConverter->InputConfig().Rate();
        uint32_t newRate = data->mRate;
        CheckedInt64 result = SaferMultDiv(mFramesParsed, newRate, oldRate);
        if (!result.isValid()) {
          NS_WARNING("Int overflow in AudioSink");
          mErrored = true;
          return;
        }
        mFramesParsed = result.value();
      }

      const AudioConfig::ChannelLayout inputLayout =
          data->mChannelMap
              ? AudioConfig::ChannelLayout::SMPTEDefault(data->mChannelMap)
              : AudioConfig::ChannelLayout(data->mChannels);
      const AudioConfig::ChannelLayout outputLayout =
          mOutputChannels == data->mChannels
              ? inputLayout
              : AudioConfig::ChannelLayout(mOutputChannels);
      AudioConfig inConfig =
          AudioConfig(inputLayout, data->mChannels, data->mRate);
      AudioConfig outConfig =
          AudioConfig(outputLayout, mOutputChannels, mOutputRate);
      if (!AudioConverter::CanConvert(inConfig, outConfig)) {
        mErrored = true;
        return;
      }
      mConverter = std::make_unique<AudioConverter>(inConfig, outConfig);
    }

    // See if there's a gap in the audio. If there is, push silence into the
    // audio hardware, so we can play across the gap.
    // Calculate the timestamp of the next chunk of audio in numbers of
    // samples.
    CheckedInt64 sampleTime =
        TimeUnitToFrames(data->mTime - mStartTime, data->mRate);
    // Calculate the number of frames that have been pushed onto the audio
    // hardware.
    CheckedInt64 missingFrames = sampleTime - mFramesParsed;

    if (!missingFrames.isValid() || !sampleTime.isValid()) {
      NS_WARNING("Int overflow in AudioSink");
      mErrored = true;
      return;
    }

    if (missingFrames.value() > AUDIO_FUZZ_FRAMES) {
      // The next audio packet begins some time after the end of the last packet
      // we pushed to the audio hardware. We must push silence into the audio
      // hardware so that the next audio packet begins playback at the correct
      // time. But don't push more than the ring buffer can receive.
      SINK_LOG("Sample time {} > frames parsed {}", sampleTime.value(),
               mFramesParsed);

      // Convert available output frames to input frames to correctly bound the
      // silence buffer size when input and output sample rates differ.
      int64_t inputFramesAvail = static_cast<int64_t>(SampleToFrame(
                                     mProcessedSPSCQueue->AvailableWrite())) *
                                 data->mRate / mOutputRate;
      missingFrames =
          std::min<int64_t>(std::min<int64_t>(INT32_MAX, missingFrames.value()),
                            inputFramesAvail);
      mFramesParsed += missingFrames.value();

      SINK_LOG("Gap in the audio input, push {} frames of silence",
               missingFrames.value());

      RefPtr<AudioData> silenceData;
      AlignedAudioBuffer silenceBuffer(missingFrames.value() * data->mChannels);
      if (!silenceBuffer) {
        NS_WARNING("OOM in AudioSink");
        mErrored = true;
        return;
      }
      if (mConverter->InputConfig() != mConverter->OutputConfig()) {
        AlignedAudioBuffer convertedData =
            mConverter->Process(AudioSampleBuffer(std::move(silenceBuffer)))
                .Forget();
        silenceData = CreateAudioFromBuffer(std::move(convertedData), data);
      } else {
        silenceData = CreateAudioFromBuffer(std::move(silenceBuffer), data);
      }
      TRACE("Pushing silence");
      PushProcessedAudio(silenceData);
    }

    mLastEndTime = data->GetEndTime();
    mFramesParsed += data->Frames();

    if (mConverter->InputConfig() != mConverter->OutputConfig()) {
      AlignedAudioBuffer buffer(data->MoveableData());
      AlignedAudioBuffer convertedData =
          mConverter->Process(AudioSampleBuffer(std::move(buffer))).Forget();
      data = CreateAudioFromBuffer(std::move(convertedData), data);
    }
    if (PushProcessedAudio(data)) {
      mLastProcessedPacket = Some(data);
    }
  }

  if (mAudioQueue.IsFinished() && mAudioQueue.GetSize() == 0) {
    // We have reached the end of the data, drain the resampler.
    DrainConverter(SampleToFrame(mProcessedSPSCQueue->AvailableWrite()));
    mProcessedQueueFinished = true;
  }
}

uint32_t AudioSink::PushProcessedAudio(AudioData* aData) {
  if (!aData || !aData->Frames()) {
    return 0;
  }
  int framesToEnqueue = static_cast<int>(aData->Frames() * aData->mChannels);
  TRACE_COMMENT("AudioSink::PushProcessedAudio", "%u frames (%u/%u)",
                framesToEnqueue,
                SampleToFrame(mProcessedSPSCQueue->AvailableWrite()),
                SampleToFrame(mProcessedSPSCQueue->Capacity()));
  int enqueued =
      mProcessedSPSCQueue->Enqueue(aData->Data().Elements(), framesToEnqueue);
  NS_WARNING_ASSERTION(enqueued == framesToEnqueue,
                       "AudioSink ring buffer over-run, can't push new data");
  // Track the total ever enqueued so a seek reuse can mark everything pushed
  // before it as stale.
  mTotalSamplesPushed += enqueued;
  return aData->Frames();
}

already_AddRefed<AudioData> AudioSink::CreateAudioFromBuffer(
    AlignedAudioBuffer&& aBuffer, AudioData* aReference) {
  uint32_t frames = SampleToFrame(aBuffer.Length());
  if (!frames) {
    return nullptr;
  }
  auto duration = media::TimeUnit(frames, mOutputRate);
  if (!duration.IsValid()) {
    NS_WARNING("Int overflow in AudioSink");
    mErrored = true;
    return nullptr;
  }
  RefPtr<AudioData> data =
      new AudioData(aReference->mOffset, aReference->mTime, std::move(aBuffer),
                    mOutputChannels, mOutputRate);
  MOZ_DIAGNOSTIC_ASSERT(duration == data->mDuration, "must be equal");
  return data.forget();
}

uint32_t AudioSink::DrainConverter(uint32_t aMaxFrames) {
  MOZ_ASSERT(mOwnerThread->IsCurrentThreadIn());

  if (!mConverter || !mLastProcessedPacket || !aMaxFrames) {
    // nothing to drain.
    return 0;
  }

  RefPtr<AudioData> lastPacket = mLastProcessedPacket.ref();
  mLastProcessedPacket.reset();

  // To drain we simply provide an empty packet to the audio converter.
  AlignedAudioBuffer convertedData =
      mConverter->Process(AudioSampleBuffer(AlignedAudioBuffer())).Forget();

  uint32_t frames = SampleToFrame(convertedData.Length());
  if (!convertedData.SetLength(std::min(frames, aMaxFrames) *
                               mOutputChannels)) {
    // This can never happen as we were reducing the length of convertData.
    mErrored = true;
    return 0;
  }

  RefPtr<AudioData> data =
      CreateAudioFromBuffer(std::move(convertedData), lastPacket);
  return PushProcessedAudio(data);
}

void AudioSink::GetDebugInfo(dom::MediaSinkDebugInfo& aInfo) {
  MOZ_ASSERT(mOwnerThread->IsCurrentThreadIn());
  aInfo.mAudioSinkWrapper.mAudioSink.mStartTime = mStartTime.ToMicroseconds();
  aInfo.mAudioSinkWrapper.mAudioSink.mLastGoodPosition =
      mLastGoodPosition.ToMicroseconds();
  aInfo.mAudioSinkWrapper.mAudioSink.mIsPlaying = mPlaying;
  aInfo.mAudioSinkWrapper.mAudioSink.mOutputRate = mOutputRate;
  aInfo.mAudioSinkWrapper.mAudioSink.mWritten = mWritten;
  aInfo.mAudioSinkWrapper.mAudioSink.mHasErrored = mErrored.load();
  aInfo.mAudioSinkWrapper.mAudioSink.mPlaybackComplete =
      mAudioStream ? mAudioStream->IsPlaybackCompleted() : false;
}

}  // namespace mozilla
