/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "AudioStream.h"

#include <math.h>

#include <algorithm>
#include <mutex>

#include "AudioConverter.h"
#include "CubebUtils.h"
#include "UnderrunHandler.h"
#include "VideoUtils.h"
#include "mozilla/Logging.h"
#include "mozilla/Monitor.h"
#include "mozilla/Mutex.h"
#include "mozilla/Sprintf.h"
#include "mozilla/dom/AudioDeviceInfo.h"
#include "nsNativeCharsetUtils.h"
#include "nsPrintfCString.h"
#include "prdtoa.h"
#if defined(XP_WIN)
#  include "nsXULAppAPI.h"
#endif
#include "CallbackThreadRegistry.h"
#include "RLBoxSoundTouch.h"
#include "Tracing.h"
#include "mozilla/StaticPrefs_media.h"
#include "rlbox_wasm2c_thread_locals.h"
#include "webaudio/blink/DenormalDisabler.h"

namespace mozilla {

LazyLogModule gAudioStreamLog("AudioStream");
// For simple logs
#define LOG(x, ...)                                               \
  MOZ_LOG_FMT(gAudioStreamLog, mozilla::LogLevel::Debug, "{} " x, \
              fmt::ptr(this), ##__VA_ARGS__)
#define LOGW(x, ...)                                                \
  MOZ_LOG_FMT(gAudioStreamLog, mozilla::LogLevel::Warning, "{} " x, \
              fmt::ptr(this), ##__VA_ARGS__)
#define LOGE(x, ...)                                                          \
  NS_DebugBreak(NS_DEBUG_WARNING,                                             \
                nsPrintfCString("%p " x, this, ##__VA_ARGS__).get(), nullptr, \
                __FILE__, __LINE__)

/**
 * Keep a list of frames sent to the audio engine in each DataCallback along
 * with the playback rate at the moment. Since the playback rate and number of
 * underrun frames can vary in each callback. We need to keep the whole history
 * in order to calculate the playback position of the audio engine correctly.
 */
class FrameHistory {
  struct Chunk {
    uint32_t servicedFrames;
    uint32_t totalFrames;
    uint32_t rate;
  };

  template <typename T>
  static T FramesToUs(uint32_t frames, uint32_t rate) {
    return static_cast<T>(frames) * USECS_PER_S / rate;
  }

 public:
  FrameHistory() : mBaseOffset(0), mBasePosition(0) {}

  void Append(uint32_t aServiced, uint32_t aUnderrun, uint32_t aRate) {
    /* In most case where playback rate stays the same and we don't underrun
     * frames, we are able to merge chunks to avoid lose of precision to add up
     * in compressing chunks into |mBaseOffset| and |mBasePosition|.
     */
    if (!mChunks.IsEmpty()) {
      Chunk& c = mChunks.LastElement();
      // 2 chunks (c1 and c2) can be merged when rate is the same and
      // adjacent frames are zero. That is, underrun frames in c1 are zero
      // or serviced frames in c2 are zero.
      if (c.rate == aRate &&
          (c.servicedFrames == c.totalFrames || aServiced == 0)) {
        c.servicedFrames += aServiced;
        c.totalFrames += aServiced + aUnderrun;
        return;
      }
    }
    Chunk* p = mChunks.AppendElement();
    p->servicedFrames = aServiced;
    p->totalFrames = aServiced + aUnderrun;
    p->rate = aRate;
  }

  /**
   * @param frames The playback position in frames of the audio engine.
   * @return The playback position in microseconds of the audio engine,
   *         adjusted by playback rate changes and underrun frames.
   */
  int64_t GetPosition(int64_t frames) {
    // playback position should not go backward.
    MOZ_ASSERT(frames >= mBaseOffset);
    while (true) {
      if (mChunks.IsEmpty()) {
        return static_cast<int64_t>(mBasePosition);
      }
      const Chunk& c = mChunks[0];
      if (frames <= mBaseOffset + c.totalFrames) {
        uint32_t delta = frames - mBaseOffset;
        delta = std::min(delta, c.servicedFrames);
        return static_cast<int64_t>(mBasePosition) +
               FramesToUs<int64_t>(delta, c.rate);
      }
      // Since the playback position of the audio engine will not go backward,
      // we are able to compress chunks so that |mChunks| won't grow
      // unlimitedly. Note that we lose precision in converting integers into
      // floats and inaccuracy will accumulate over time. However, for a 24hr
      // long, sample rate = 44.1k file, the error will be less than 1
      // microsecond after playing 24 hours. So we are fine with that.
      mBaseOffset += c.totalFrames;
      mBasePosition += FramesToUs<double>(c.servicedFrames, c.rate);
      mChunks.RemoveElementAt(0);
    }
  }

  // Re-zero the reported playback position for a stream reused across a seek.
  // The audio engine's frame counter keeps climbing across the seek, so without
  // this the position would keep reporting the pre-seek time; rebasing lets the
  // position resume from zero at the seek target while the same stream keeps
  // running.
  //
  // |aUnplayed| is the audio handed to the engine but not yet played. It
  // belongs to the position being left behind, so it is carried over as a chunk
  // that services nothing: the position holds until the engine plays past it.
  void Rebase(int64_t aBaseOffset, uint32_t aUnplayed, uint32_t aRate) {
    LOG("FrameHistory::Rebase to base offset {}", aBaseOffset);
    mChunks.Clear();
    mBaseOffset = aBaseOffset;
    mBasePosition = 0;
    if (aUnplayed > 0) {
      Append(0, aUnplayed, aRate);
    }
  }

 private:
  AutoTArray<Chunk, 7> mChunks;
  // The engine frame count (mBaseOffset) and the media position in microseconds
  // (mBasePosition) of the oldest chunk still tracked in mChunks. They diverge
  // because playback-rate changes and underruns make engine frames and media
  // time advance at different rates; GetPosition walks mChunks starting from
  // mBaseOffset to turn a raw engine count into a media position, and Rebase
  // moves mBaseOffset (resetting mBasePosition to 0) to re-zero a reused
  // stream.
  int64_t mBaseOffset;
  double mBasePosition;
};

AudioStream::AudioStream(DataSource& aSource, uint32_t aInRate,
                         uint32_t aOutputChannels,
                         AudioConfig::ChannelLayout::ChannelMap aChannelMap)
    : mTimeStretcher(nullptr),
      mAudioClock(aInRate),
      mChannelMap(aChannelMap),
      mMonitor("AudioStream"),
      mOutChannels(aOutputChannels),
      mState(INITIALIZED),
      mDataSource(aSource),
      mAudioThreadId(ProfilerThreadId{}),
      mSandboxed(CubebUtils::SandboxEnabled()),
      mPlaybackComplete(false),
      mPlaybackRate(1.0f),
      mPreservesPitch(true),
      mCallbacksStarted(false) {}

AudioStream::~AudioStream() {
  LOG("deleted, state {}", static_cast<int>(mState.load()));
  MOZ_ASSERT(mState == SHUTDOWN && !mCubebStream,
             "Should've called ShutDown() before deleting an AudioStream");
}

size_t AudioStream::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t amount = aMallocSizeOf(this);

  // Possibly add in the future:
  // - mTimeStretcher
  // - mCubebStream

  return amount;
}

#ifdef MOZ_WASM_SANDBOXING_SOUNDTOUCH
static std::once_flag gSetupSoundtouchReporting;
#endif

nsresult AudioStream::EnsureTimeStretcherInitialized() {
  AssertIsOnAudioThread();
  if (!mTimeStretcher) {
#ifdef MOZ_WASM_SANDBOXING_SOUNDTOUCH
    // Redirect all soundtouch Wasm OOM reporting to main process reporting.
    // This only has to be done once for the Wasm runtime (although repeated
    // calls are also ok).
    std::call_once(gSetupSoundtouchReporting, []() {
      auto current_handler = &moz_wasm2c_memgrow_failed;
      RLBoxSoundTouch::redirectRLBoxSbxGrowFail(current_handler);
    });
#endif

    auto timestretcher = std::make_unique<RLBoxSoundTouch>();
    if (!timestretcher || !timestretcher->Init()) {
      return NS_ERROR_FAILURE;
    }
    mTimeStretcher = timestretcher.release();

    mTimeStretcher->setSampleRate(mAudioClock.GetInputRate());
    mTimeStretcher->setChannels(mOutChannels);
    mTimeStretcher->setPitch(1.0);

    // SoundTouch v2.1.2 uses automatic time-stretch settings with the following
    // values:
    // Tempo 0.5: 90ms sequence, 20ms seekwindow, 8ms overlap
    // Tempo 2.0: 40ms sequence, 15ms seekwindow, 8ms overlap
    // We are going to use a smaller 10ms sequence size to improve speech
    // clarity, giving more resolution at high tempo and less reverb at low
    // tempo. Maintain 15ms seekwindow and 8ms overlap for smoothness.
    mTimeStretcher->setSetting(
        SETTING_SEQUENCE_MS,
        StaticPrefs::media_audio_playbackrate_soundtouch_sequence_ms());
    mTimeStretcher->setSetting(
        SETTING_SEEKWINDOW_MS,
        StaticPrefs::media_audio_playbackrate_soundtouch_seekwindow_ms());
    mTimeStretcher->setSetting(
        SETTING_OVERLAP_MS,
        StaticPrefs::media_audio_playbackrate_soundtouch_overlap_ms());

    const uint32_t primeFrames =
        (StaticPrefs::media_audio_playbackrate_soundtouch_sequence_ms() +
         StaticPrefs::media_audio_playbackrate_soundtouch_seekwindow_ms()) *
        mAudioClock.GetInputRate() / 1000;
    AutoTArray<AudioDataValue, 2048> silence;
    silence.SetLength(primeFrames * mOutChannels);
    std::fill(silence.begin(), silence.end(), AudioDataValue(0));
    mTimeStretcher->putSamples(silence.Elements(), primeFrames);
    const uint32_t numOut =
        mTimeStretcher->numSamples().unverified_safe_because(
            "Bounded by the number of frames used to prime the stretcher.");
    MOZ_ASSERT(numOut <= primeFrames);
    silence.SetLength(numOut * mOutChannels);
    mTimeStretcher->receiveSamples(silence.Elements(), numOut);
  }
  return NS_OK;
}

nsresult AudioStream::SetPlaybackRate(double aPlaybackRate) {
  TRACE_COMMENT("AudioStream::SetPlaybackRate", "%f", aPlaybackRate);
  NS_ASSERTION(
      aPlaybackRate > 0.0,
      "Can't handle negative or null playbackrate in the AudioStream.");
  if (aPlaybackRate == mPlaybackRate) {
    return NS_OK;
  }

  mPlaybackRate = static_cast<float>(aPlaybackRate);

  return NS_OK;
}

nsresult AudioStream::SetPreservesPitch(bool aPreservesPitch) {
  TRACE_COMMENT("AudioStream::SetPreservesPitch", "%d", aPreservesPitch);
  if (aPreservesPitch == mPreservesPitch) {
    return NS_OK;
  }

  mPreservesPitch = aPreservesPitch;

  return NS_OK;
}

template <typename Function, typename... Args>
int AudioStream::InvokeCubeb(Function aFunction, Args&&... aArgs) {
  mMonitor.AssertCurrentThreadOwns();
  MonitorAutoUnlock mon(mMonitor);
  return aFunction(mCubebStream.get(), std::forward<Args>(aArgs)...);
}

nsresult AudioStream::Init(AudioDeviceInfo* aSinkInfo)
    MOZ_NO_THREAD_SAFETY_ANALYSIS {
  auto startTime = TimeStamp::Now();
  TRACE("AudioStream::Init");

  LOG("{} channels: {}, rate: {}", __FUNCTION__, mOutChannels,
      mAudioClock.GetInputRate());

  mSinkInfo = aSinkInfo;

  cubeb_stream_params params;
  params.rate = mAudioClock.GetInputRate();
  params.channels = mOutChannels;
  params.layout = static_cast<uint32_t>(mChannelMap);
  params.format = CubebUtils::ToCubebFormat<AUDIO_OUTPUT_FORMAT>::value;
  params.prefs = CubebUtils::GetDefaultStreamPrefs(CUBEB_DEVICE_TYPE_OUTPUT);
  params.input_params = CUBEB_INPUT_PROCESSING_PARAM_NONE;

  // This is noop if MOZ_DUMP_AUDIO is not set.
  mDumpFile.Open("AudioStream", mOutChannels, mAudioClock.GetInputRate());

  RefPtr<CubebUtils::CubebHandle> handle = CubebUtils::GetCubeb();
  if (!handle) {
    LOGE("Can't get cubeb context!");
    CubebUtils::ReportCubebStreamInitFailure(true);
    return NS_ERROR_DOM_MEDIA_CUBEB_INITIALIZATION_ERR;
  }

  mCubeb = handle;
  return OpenCubeb(handle->Context(), params, startTime,
                   CubebUtils::GetFirstStream());
}

nsresult AudioStream::OpenCubeb(cubeb* aContext, cubeb_stream_params& aParams,
                                TimeStamp aStartTime, bool aIsFirst) {
  TRACE("AudioStream::OpenCubeb");
  MOZ_ASSERT(aContext);

  cubeb_stream* stream = nullptr;
  /* Convert from milliseconds to frames. */
  uint32_t latency_frames =
      CubebUtils::GetCubebPlaybackLatencyInMilliseconds() * aParams.rate / 1000;
  cubeb_devid deviceID = nullptr;
  if (mSinkInfo && mSinkInfo->DeviceID()) {
    deviceID = mSinkInfo->DeviceID();
  }
  if (CubebUtils::CubebStreamInit(aContext, &stream, "AudioStream", nullptr,
                                  nullptr, deviceID, &aParams, latency_frames,
                                  DataCallback_S, StateCallback_S,
                                  this) == CUBEB_OK) {
    mCubebStream.reset(stream);
    CubebUtils::ReportCubebBackendUsed();
  } else {
    LOGE("OpenCubeb() failed to init cubeb");
    CubebUtils::ReportCubebStreamInitFailure(aIsFirst);
    return NS_ERROR_FAILURE;
  }

  TimeDuration timeDelta = TimeStamp::Now() - aStartTime;
  LOG("creation time {}first: {} ms", aIsFirst ? "" : "not ",
      (uint32_t)timeDelta.ToMilliseconds());

  return NS_OK;
}

void AudioStream::SetVolume(double aVolume) {
  TRACE_COMMENT("AudioStream::SetVolume", "%f", aVolume);
  MOZ_ASSERT(aVolume >= 0.0 && aVolume <= 1.0, "Invalid volume");

  MOZ_ASSERT(mState != SHUTDOWN, "Don't set volume after shutdown.");
  if (mState == ERRORED) {
    return;
  }

  MonitorAutoLock mon(mMonitor);
  if (InvokeCubeb(cubeb_stream_set_volume,
                  aVolume * CubebUtils::GetVolumeScale()) != CUBEB_OK) {
    LOGE("Could not change volume on cubeb stream.");
  }
}

void AudioStream::SetStreamName(const nsAString& aStreamName) {
  TRACE("AudioStream::SetStreamName");

  nsAutoCString aRawStreamName;
  nsresult rv = NS_CopyUnicodeToNative(aStreamName, aRawStreamName);

  if (NS_FAILED(rv) || aStreamName.IsEmpty()) {
    return;
  }

  MonitorAutoLock mon(mMonitor);
  int r = InvokeCubeb(cubeb_stream_set_name, aRawStreamName.get());
  if (r && r != CUBEB_ERROR_NOT_SUPPORTED) {
    LOGE("Could not set cubeb stream name.");
  }
}

RefPtr<MediaSink::EndedPromise> AudioStream::Start() {
  TRACE("AudioStream::Start");
  MOZ_ASSERT(mState == INITIALIZED);
  mState = STARTED;
  RefPtr<MediaSink::EndedPromise> promise;
  {
    MonitorAutoLock mon(mMonitor);
    // As cubeb might call audio stream's state callback very soon after we
    // start cubeb, we have to create the promise beforehand in order to handle
    // the case where we immediately get `drained`.
    promise = mEndedPromise.Ensure(__func__);
    mPlaybackComplete = false;

    if (InvokeCubeb(cubeb_stream_start) != CUBEB_OK) {
      mState = ERRORED;
      mEndedPromise.RejectIfExists(NS_ERROR_FAILURE, __func__);
    } else {
      mCubebStarted = true;
    }

    LOG("started, state {}", mState == STARTED   ? "STARTED"
                             : mState == DRAINED ? "DRAINED"
                                                 : "ERRORED");
  }
  return promise;
}

void AudioStream::Pause() {
  TRACE("AudioStream::Pause");
  MOZ_ASSERT(mState != INITIALIZED, "Must be Start()ed.");
  MOZ_ASSERT(mState != SHUTDOWN, "Already ShutDown()ed.");

  // Do nothing if we are already drained or errored.
  if (mState == DRAINED || mState == ERRORED) {
    return;
  }

  // In keep-running mode a pause is a seek pause: leave the cubeb stream
  // running and only track the logical stop, so a following Resume() can
  // restart playback without paying the stream restart cost. If cubeb is not
  // currently running there is nothing to keep alive, so fall through to the
  // normal stop.
  if (mKeepRunning && mCubebStarted) {
    LOG("Pause: keep-running mode, tracking logical STOPPED without stopping "
        "cubeb");
    mState = STOPPED;
    return;
  }

  MOZ_ASSERT(mState != STOPPED, "Already Pause()ed.");

  MonitorAutoLock mon(mMonitor);
  mCubebStarted = false;
  if (InvokeCubeb(cubeb_stream_stop) != CUBEB_OK) {
    mState = ERRORED;
  } else if (mState != DRAINED && mState != ERRORED) {
    // Don't transition to other states if we are already
    // drained or errored.
    mState = STOPPED;
  }
}

void AudioStream::Resume() {
  TRACE("AudioStream::Resume");
  MOZ_ASSERT(mState != INITIALIZED, "Must be Start()ed.");
  MOZ_ASSERT(mState != SHUTDOWN, "Already ShutDown()ed.");

  // Do nothing if we are already drained or errored. Such a backend will not
  // play what it was handed, so the carry decision must not be left reading an
  // earlier seek's answer.
  if (mState == DRAINED || mState == ERRORED) {
    mResumeKeptCubebRunning = false;
    return;
  }

  // In keep-running mode, if cubeb was left running by a keep-running pause we
  // only restore the logical playing state. Otherwise cubeb is stopped, because
  // the stream was paused before the mode was entered, and must be started for
  // real.
  mResumeKeptCubebRunning = mKeepRunning && mCubebStarted;
  if (mResumeKeptCubebRunning) {
    LOG("Resume: keep-running mode, tracking logical STARTED without starting "
        "cubeb");
    mState = STARTED;
    return;
  }

  MOZ_ASSERT(mState != STARTED, "Already Start()ed.");

  MonitorAutoLock mon(mMonitor);
  if (InvokeCubeb(cubeb_stream_start) != CUBEB_OK) {
    mState = ERRORED;
  } else if (mState != DRAINED && mState != ERRORED) {
    // Don't transition to other states if we are already
    // drained or errored.
    mCubebStarted = true;
    mState = STARTED;
  }
}

void AudioStream::SetKeepRunningMode(bool aKeepRunning) {
  if (aKeepRunning == mKeepRunning) {
    return;
  }
  LOG("SetKeepRunningMode: {}", aKeepRunning);
  mKeepRunning = aKeepRunning;
}

void AudioStream::ShutDown() {
  TRACE("AudioStream::ShutDown");
  LOG("ShutDown, state {}", static_cast<int>(mState.load()));

  MonitorAutoLock mon(mMonitor);
  mCubebStarted = false;
  if (mCubebStream) {
    // Force stop to put the cubeb stream in a stable state before deletion.
    InvokeCubeb(cubeb_stream_stop);
    // Must not try to shut down cubeb from within the lock!  wasapi may still
    // call our callback after Pause()/stop()!?! Bug 996162
    cubeb_stream* cubeb = mCubebStream.release();
    MonitorAutoUnlock unlock(mMonitor);
    cubeb_stream_destroy(cubeb);
  }

  // After `cubeb_stream_stop` has been called, there is no audio thread
  // anymore. We can delete the time stretcher.
  if (mTimeStretcher) {
    delete mTimeStretcher;
    mTimeStretcher = nullptr;
  }

  mState = SHUTDOWN;
  mEndedPromise.ResolveIfExists(true, __func__);
}

void AudioStream::RebaseLive() {
  TRACE("AudioStream::RebaseLive");
  int64_t rawFrames;
  {
#ifndef XP_MACOSX
    MonitorAutoLock mon(mMonitor);
#endif
    rawFrames = GetPositionInFramesUnlocked();
  }
  // The engine counter keeps climbing across the seek while media time
  // restarts, so re-anchor the clock rather than reset the counter. Carry the
  // unplayed audio only when the device will still play it, checked live rather
  // than trusted from Resume(): a restarted or since-dead backend will not, and
  // a failed read leaves no cursor to measure from.
  AudioClock::CarryUnplayed carry = AudioClock::CarryUnplayed::Yes;
  if (rawFrames < 0) {
    rawFrames = 0;
    carry = AudioClock::CarryUnplayed::No;
  } else if (!mResumeKeptCubebRunning || mState != STARTED || !mCubebStarted) {
    carry = AudioClock::CarryUnplayed::No;
  }
  LOG("RebaseLive: raw frame count {}, carrying unplayed audio {}", rawFrames,
      carry == AudioClock::CarryUnplayed::Yes);
  mAudioClock.Rebase(rawFrames, carry);
}

RefPtr<MediaSink::EndedPromise> AudioStream::ReinitEndedPromise() {
  MonitorAutoLock mon(mMonitor);
  // The outstanding promise here is the one from the original Start(), still
  // unsettled because the stream kept running across the seek. Its consumer was
  // already disconnected before the stream was set aside (the reuse caller
  // asserts this), so settling it reaches no one; it only clears the holder so
  // a fresh promise can be handed to the resumed sink. Reject rather than
  // resolve: were a consumer ever still attached by mistake, an error surfaces
  // the bug instead of silently ending playback, which a false "ended" would
  // do.
  mEndedPromise.RejectIfExists(NS_ERROR_ABORT, __func__);
  if (mState == ERRORED) {
    return MediaSink::EndedPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }
  return mEndedPromise.Ensure(__func__);
}

int64_t AudioStream::GetPosition() {
  TRACE("AudioStream::GetPosition");
#ifndef XP_MACOSX
  MonitorAutoLock mon(mMonitor);
#endif
  int64_t frames = GetPositionInFramesUnlocked();
  return frames >= 0 ? mAudioClock.GetPosition(frames) : -1;
}

int64_t AudioStream::GetPositionInFrames() {
  TRACE("AudioStream::GetPositionInFrames");
#ifndef XP_MACOSX
  MonitorAutoLock mon(mMonitor);
#endif
  int64_t frames = GetPositionInFramesUnlocked();

  return frames >= 0 ? mAudioClock.GetPositionInFrames(frames) : -1;
}

int64_t AudioStream::GetPositionInFramesUnlocked() {
  TRACE("AudioStream::GetPositionInFramesUnlocked");
#ifndef XP_MACOSX
  mMonitor.AssertCurrentThreadOwns();
#endif

  if (mState == ERRORED) {
    return -1;
  }

  uint64_t position = 0;
  int rv;

#ifndef XP_MACOSX
  rv = InvokeCubeb(cubeb_stream_get_position, &position);
#else
  rv = cubeb_stream_get_position(mCubebStream.get(), &position);
#endif

  if (rv != CUBEB_OK) {
    return -1;
  }
  return static_cast<int64_t>(std::min<uint64_t>(position, INT64_MAX));
}

bool AudioStream::IsValidAudioFormat(Chunk* aChunk) {
  if (aChunk->Rate() != mAudioClock.GetInputRate()) {
    LOGW("mismatched sample {}, mInRate={}", aChunk->Rate(),
         mAudioClock.GetInputRate());
    return false;
  }

  return aChunk->Channels() <= 8;
}

void AudioStream::GetUnprocessed(AudioBufferWriter& aWriter) {
  TRACE("AudioStream::GetUnprocessed");
  AssertIsOnAudioThread();
  // Flush the timestretcher pipeline, if we were playing using a playback rate
  // other than 1.0.
  if (mTimeStretcher) {
    // Get number of samples and based on this either receive samples or write
    // silence.  At worst, the attacker can supply weird sound samples or
    // result in us writing silence.
    auto numSamples = mTimeStretcher->numSamples().unverified_safe_because(
        "We only use this to decide whether to receive samples or write "
        "silence.");
    if (numSamples) {
      RLBoxSoundTouch* timeStretcher = mTimeStretcher;
      aWriter.Write(
          [timeStretcher](AudioDataValue* aPtr, uint32_t aFrames) {
            return timeStretcher->receiveSamples(aPtr, aFrames);
          },
          aWriter.Available());
    }
  }

  while (aWriter.Available() > 0) {
    uint32_t count = mDataSource.PopFrames(aWriter.Ptr(), aWriter.Available(),
                                           mAudioThreadChanged);
    if (count == 0) {
      break;
    }
    aWriter.Advance(count);
  }
}

void AudioStream::GetTimeStretched(AudioBufferWriter& aWriter) {
  TRACE("AudioStream::GetTimeStretched");
  AssertIsOnAudioThread();
  if (EnsureTimeStretcherInitialized() != NS_OK) {
    return;
  }

  uint32_t toPopFrames =
      ceil(aWriter.Available() * mAudioClock.GetPlaybackRate());

  if (!mTimeStretcher) {
    return;
  }

  // At each iteration, get number of samples and (based on this) write from
  // the data source or silence. At worst, if the number of samples is a lie
  // (i.e., under attacker control) we'll either not write anything or keep
  // writing noise. This is safe because all the memory operations within the
  // loop (and after) are checked.
  while (mTimeStretcher->numSamples().unverified_safe_because(
             "Only used to decide whether to put samples.") <
         aWriter.Available()) {
    // pop into a temp buffer, and put into the stretcher.
    AutoTArray<AudioDataValue, 1000> buf;
    auto size = CheckedUint32(mOutChannels) * toPopFrames;
    if (!size.isValid()) {
      // The overflow should not happen in normal case.
      LOGW("Invalid member data: {} channels, {} frames", mOutChannels,
           toPopFrames);
      return;
    }
    buf.SetLength(size.value());
    // ensure no variable channel count or something like that
    uint32_t count =
        mDataSource.PopFrames(buf.Elements(), toPopFrames, mAudioThreadChanged);
    if (count == 0) {
      break;
    }
    mTimeStretcher->putSamples(buf.Elements(), count);
  }

  auto* timeStretcher = mTimeStretcher;
  aWriter.Write(
      [timeStretcher](AudioDataValue* aPtr, uint32_t aFrames) {
        return timeStretcher->receiveSamples(aPtr, aFrames);
      },
      aWriter.Available());
}

bool AudioStream::CheckThreadIdChanged() {
  ProfilerThreadId id = profiler_current_thread_id();
  if (id != mAudioThreadId) {
    mAudioThreadId = id;
    mAudioThreadChanged = true;
    return true;
  }
  mAudioThreadChanged = false;
  return false;
}

void AudioStream::AssertIsOnAudioThread() const {
  // This can be called right after CheckThreadIdChanged, because the audio
  // thread can change when not sandboxed.
  MOZ_ASSERT(mAudioThreadId.load() == profiler_current_thread_id());
}

void AudioStream::UpdatePlaybackRateIfNeeded() {
  AssertIsOnAudioThread();
  if (mAudioClock.GetPreservesPitch() == mPreservesPitch &&
      mAudioClock.GetPlaybackRate() == mPlaybackRate) {
    return;
  }

  EnsureTimeStretcherInitialized();

  if (mTimeStretcher && mAudioClock.GetPlaybackRate() != 1.0f &&
      static_cast<float>(mPlaybackRate) == 1.0f) {
    mTimeStretcher->flush();
  }

  mAudioClock.SetPlaybackRate(mPlaybackRate);
  mAudioClock.SetPreservesPitch(mPreservesPitch);

  if (!mTimeStretcher) {
    return;
  }

  if (mPreservesPitch) {
    mTimeStretcher->setTempo(mPlaybackRate);
    mTimeStretcher->setRate(1.0f);
  } else {
    mTimeStretcher->setTempo(1.0f);
    mTimeStretcher->setRate(mPlaybackRate);
  }
}

long AudioStream::DataCallback(void* aBuffer, long aFrames) {
  if (CheckThreadIdChanged() && !mSandboxed) {
    CallbackThreadRegistry::Get()->Register(mAudioThreadId,
                                            "NativeAudioCallback");
  }
  WebCore::DenormalDisabler disabler;
  if (!mCallbacksStarted) {
    mCallbacksStarted = true;
  }

  TRACE_AUDIO_CALLBACK_FRAME_COUNT("AudioStream real-time budget", aFrames,
                                   mAudioClock.GetInputRate());
  TRACE("AudioStream::DataCallback");
  MOZ_ASSERT(mState != SHUTDOWN, "No data callback after shutdown");

  if (SoftRealTimeLimitReached()) {
    DemoteThreadFromRealTime();
  }

  UpdatePlaybackRateIfNeeded();

  auto writer = AudioBufferWriter(
      Span<AudioDataValue>(reinterpret_cast<AudioDataValue*>(aBuffer),
                           mOutChannels * aFrames),
      mOutChannels, aFrames);

  if (mAudioClock.GetInputRate() == mAudioClock.GetOutputRate()) {
    GetUnprocessed(writer);
  } else {
    GetTimeStretched(writer);
  }

  // Always send audible frames first, and silent frames later.
  // Otherwise it will break the assumption of FrameHistory.
  if (!mDataSource.Ended()) {
#ifndef XP_MACOSX
    MonitorAutoLock mon(mMonitor);
#endif
    mAudioClock.UpdateFrameHistory(aFrames - writer.Available(),
                                   writer.Available(), mAudioThreadChanged);
    if (writer.Available() > 0) {
      if (!mDataSource.IsIntentionallySilent()) {
        TRACE_COMMENT("AudioStream::DataCallback",
                      "Underrun: %d frames missing", writer.Available());
        LOGW("lost {} frames", writer.Available());
      }
      writer.WriteZeros(writer.Available());
    }
  } else {
    // No more new data in the data source, and the drain has completed. We
    // don't need the time stretcher anymore at this point.
    if (mTimeStretcher && writer.Available()) {
      delete mTimeStretcher;
      mTimeStretcher = nullptr;
    }
#ifndef XP_MACOSX
    MonitorAutoLock mon(mMonitor);
#endif
    mAudioClock.UpdateFrameHistory(aFrames - writer.Available(), 0,
                                   mAudioThreadChanged);
  }

  mDumpFile.Write(static_cast<const AudioDataValue*>(aBuffer),
                  aFrames * mOutChannels);

  if (!mSandboxed && writer.Available() != 0) {
    CallbackThreadRegistry::Get()->Unregister(mAudioThreadId);
  }
  return aFrames - writer.Available();
}

void AudioStream::StateCallback(cubeb_state aState) {
  MOZ_ASSERT(mState != SHUTDOWN, "No state callback after shutdown");
  LOG("StateCallback, mState={} cubeb_state={}",
      static_cast<int>(mState.load()), static_cast<int>(aState));

  MonitorAutoLock mon(mMonitor);
  if (aState == CUBEB_STATE_DRAINED) {
    LOG("Drained");
    mState = DRAINED;
    mPlaybackComplete = true;
    mEndedPromise.ResolveIfExists(true, __func__);
  } else if (aState == CUBEB_STATE_ERROR) {
    LOGE("StateCallback() state %d cubeb error", mState.load());
    mState = ERRORED;
    mPlaybackComplete = true;
    mEndedPromise.RejectIfExists(NS_ERROR_FAILURE, __func__);
  }
}

bool AudioStream::IsPlaybackCompleted() const { return mPlaybackComplete; }

AudioClock::AudioClock(uint32_t aInRate)
    : mOutRate(aInRate),
      mInRate(aInRate),
      mPreservesPitch(true),
      mFrameHistory(new FrameHistory()) {}

AudioClock::~AudioClock() = default;

// Audio thread only
void AudioClock::UpdateFrameHistory(uint32_t aServiced, uint32_t aUnderrun,
                                    bool aAudioThreadChanged) {
#ifdef XP_MACOSX
  mHandoff.mTotal += aServiced + aUnderrun;
  if (aAudioThreadChanged) {
    mCallbackInfoQueue.ResetProducerThreadId();
  }
  // Flush the local items, if any, and then attempt to enqueue the current
  // item. This is only a fallback mechanism, under non-critical load this is
  // just going to enqueue an item in the queue.
  while (!mAudioThreadCallbackInfo.IsEmpty()) {
    CallbackInfo& info = mAudioThreadCallbackInfo[0];
    // If still full, keep it audio-thread side for now.
    if (mCallbackInfoQueue.Enqueue(info) != 1) {
      break;
    }
    mAudioThreadCallbackInfo.RemoveElementAt(0);
  }
  CallbackInfo info(aServiced, aUnderrun, mOutRate);
  if (mCallbackInfoQueue.Enqueue(info) != 1) {
    NS_WARNING(
        "mCallbackInfoQueue full, storing the values in the audio thread.");
    mAudioThreadCallbackInfo.AppendElement(info);
  }
#else
  // Publish the counter and the history together, so a write cursor read under
  // mMutex always matches the history it will be compared against.
  MutexAutoLock lock(mMutex);
  mHandoff.mTotal += aServiced + aUnderrun;
  mFrameHistory->Append(aServiced, aUnderrun, mOutRate);
#endif
}

void AudioClock::Rebase(int64_t aBaseOffset, CarryUnplayed aCarry) {
#ifdef XP_MACOSX
  // Draining here only bounds the queue. What keeps pre-seek items out of the
  // post-rebase history is mRebasedThrough, set below.
  ApplyQueuedCallbackInfo();
#else
  MutexAutoLock lock(mMutex);
#endif

  // Read after synchronising with the audio thread: an earlier read misses a
  // racing callback and leaves the position leading the audio.
  const uint64_t writeCursor = mHandoff.mTotal;

  uint32_t unplayed = 0;
  if (aCarry == CarryUnplayed::Yes) {
    MOZ_ASSERT(aBaseOffset >= 0, "a negative play cursor is not a position");
    const uint64_t playCursor = static_cast<uint64_t>(aBaseOffset);
    uint64_t gap = 0;
    if (writeCursor >= playCursor) {
      gap = writeCursor - playCursor;
    } else {
      // Some backends estimate the position from the wall clock since the last
      // callback, so it can read above the frames handed over.
      LOGW("Rebase: play cursor {} above write cursor {}, carrying nothing",
           playCursor, writeCursor);
    }
    // The carry narrows into a chunk's uint32_t counts, and a later
    // underrun-only append merges into that chunk by adding to them, so the
    // bound must leave headroom rather than take the whole range. Half is an
    // arbitrary choice of how much, and leaves over twelve hours at 48 kHz.
    constexpr uint64_t kMaxUnplayed = UINT32_MAX / 2;
    if (gap > kMaxUnplayed) {
      LOGW("Rebase: {} unplayed frames exceed the carry limit, carrying {}",
           gap, kMaxUnplayed);
      gap = kMaxUnplayed;
    }
    unplayed = static_cast<uint32_t>(gap);
  }

#ifdef XP_MACOSX
  // Everything at or below the write cursor is either carried above or
  // deliberately discarded, so a queued or audio-thread-stranded item at or
  // below it must be dropped rather than appended after the rebase. This has to
  // hold on both branches: the history is cleared either way.
  mHandoff.mRebasedThrough = writeCursor;
#endif

  LOG("Rebase: play cursor {}, write cursor {}, carrying {} unplayed frames",
      aBaseOffset, writeCursor, unplayed);
  mFrameHistory->Rebase(aBaseOffset, unplayed, mOutRate);
}

int64_t AudioClock::GetPositionInFrames(int64_t aFrames) {
  CheckedInt64 v = UsecsToFrames(GetPosition(aFrames), mInRate);
  return v.isValid() ? v.value() : -1;
}

#ifdef XP_MACOSX
void AudioClock::ApplyQueuedCallbackInfo() {
  CallbackInfo info;
  while (mCallbackInfoQueue.Dequeue(&info, 1)) {
    // Count first, and count every item, including the ones dropped below: this
    // running total is only comparable to the producer's if nothing is missed.
    mHandoff.mSeen += info.TotalFrames();
    if (mHandoff.mSeen <= mHandoff.mRebasedThrough) {
      // Handed over before the last rebase, which already accounted for these
      // frames. Appending them would count them twice, and an underrun-only
      // item merges into the preceding chunk, extending a window that advances
      // no media time by however long the seek took.
      continue;
    }
    mFrameHistory->Append(info.mServiced, info.mUnderrun, info.mOutputRate);
  }
}
#endif

int64_t AudioClock::GetPosition(int64_t frames) {
#ifdef XP_MACOSX
  ApplyQueuedCallbackInfo();
#else
  MutexAutoLock lock(mMutex);
#endif
  return mFrameHistory->GetPosition(frames);
}

void AudioClock::SetPlaybackRate(double aPlaybackRate) {
  mOutRate = static_cast<uint32_t>(mInRate / aPlaybackRate);
}

double AudioClock::GetPlaybackRate() const {
  return static_cast<double>(mInRate) / mOutRate;
}

void AudioClock::SetPreservesPitch(bool aPreservesPitch) {
  mPreservesPitch = aPreservesPitch;
}

bool AudioClock::GetPreservesPitch() const { return mPreservesPitch; }

#undef LOG
#undef LOGW
#undef LOGE

}  // namespace mozilla
