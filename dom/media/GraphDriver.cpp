/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GraphDriver.h"

#include "AudioNodeEngine.h"
#include "CallbackThreadRegistry.h"
#include "CubebDeviceEnumerator.h"
#include "MediaTrackGraphImpl.h"
#include "Tracing.h"
#include "audio_thread_priority.h"
#include "cubeb/cubeb.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/dom/AudioContext.h"
#include "mozilla/dom/AudioDeviceInfo.h"
#include "mozilla/dom/BaseAudioContextBinding.h"

#ifdef MOZ_WEBRTC
#  include "webrtc/MediaEngineWebRTC.h"
#endif

#ifdef XP_MACOSX
#  include <sys/sysctl.h>

#  include "nsCocoaFeatures.h"
#endif

extern mozilla::LazyLogModule gMediaTrackGraphLog;
#ifdef LOG
#  undef LOG
#endif  // LOG
#define LOG(type, ...) \
  MOZ_LOG_FMT(gMediaTrackGraphLog, type, MOZ_LOG_EXPAND_ARGS __VA_ARGS__)

namespace mozilla {

GraphDriver::GraphDriver(GraphInterface* aGraphInterface,
                         GraphDriver* aPreviousDriver, uint32_t aSampleRate)
    : mGraphInterface(aGraphInterface),
      mSampleRate(aSampleRate),
      mPreviousDriver(aPreviousDriver) {}

void GraphDriver::SetStreamName(const nsACString& aStreamName) {
  MOZ_ASSERT(InIteration() || (!ThreadRunning() && NS_IsMainThread()));
  mStreamName = aStreamName;
  LOG(LogLevel::Debug, ("{}: GraphDriver::SetStreamName driver={} {}",
                        fmt::ptr(Graph()), fmt::ptr(this), mStreamName.get()));
}

void GraphDriver::SetState(const nsACString& aStreamName,
                           GraphTime aStateComputedTime,
                           TimeStamp aIterationTimeStamp) {
  MOZ_ASSERT(InIteration() || !ThreadRunning());

  mStreamName = aStreamName;
  mStateComputedTime = aStateComputedTime;
  mTargetIterationTimeStamp = aIterationTimeStamp;
}

#ifdef DEBUG
bool GraphDriver::InIteration() const {
  return OnThread() || Graph()->InDriverIteration(this);
}
#endif

GraphDriver* GraphDriver::PreviousDriver() {
  MOZ_ASSERT(InIteration() || !ThreadRunning());
  return mPreviousDriver;
}

void GraphDriver::SetPreviousDriver(GraphDriver* aPreviousDriver) {
  MOZ_ASSERT(InIteration() || !ThreadRunning());
  mPreviousDriver = aPreviousDriver;
}

ThreadedDriver::ThreadedDriver(GraphInterface* aGraphInterface,
                               GraphDriver* aPreviousDriver,
                               uint32_t aSampleRate)
    : GraphDriver(aGraphInterface, aPreviousDriver, aSampleRate),
      mThreadRunning(false) {}

class MediaTrackGraphShutdownThreadRunnable : public Runnable {
 public:
  explicit MediaTrackGraphShutdownThreadRunnable(
      already_AddRefed<nsIThread> aThread)
      : Runnable("MediaTrackGraphShutdownThreadRunnable"), mThread(aThread) {}
  NS_IMETHOD Run() override {
    TRACE("MediaTrackGraphShutdownThreadRunnable");
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mThread);

    mThread->AsyncShutdown();
    mThread = nullptr;
    return NS_OK;
  }

 private:
  nsCOMPtr<nsIThread> mThread;
};

ThreadedDriver::~ThreadedDriver() {
  if (mThread) {
    nsCOMPtr<nsIRunnable> event =
        new MediaTrackGraphShutdownThreadRunnable(mThread.forget());
    SchedulerGroup::Dispatch(event.forget());
  }
}

class MediaTrackGraphInitThreadRunnable : public Runnable {
 public:
  explicit MediaTrackGraphInitThreadRunnable(ThreadedDriver* aDriver)
      : Runnable("MediaTrackGraphInitThreadRunnable"), mDriver(aDriver) {}
  NS_IMETHOD Run() override {
    TRACE("MediaTrackGraphInitThreadRunnable");
    MOZ_ASSERT(!mDriver->ThreadRunning());
    LOG(LogLevel::Debug, ("Starting a new system driver for graph {}",
                          fmt::ptr(mDriver->mGraphInterface.get())));

    if (GraphDriver* previousDriver = mDriver->PreviousDriver()) {
      LOG(LogLevel::Debug,
          ("{} releasing an AudioCallbackDriver({}), for graph {}",
           fmt::ptr(mDriver.get()), fmt::ptr(previousDriver),
           fmt::ptr(mDriver->Graph())));
      MOZ_ASSERT(!mDriver->AsAudioCallbackDriver());
      AudioCallbackDriver* audioCallbackDriver =
          previousDriver->AsAudioCallbackDriver();
      MOZ_ALWAYS_SUCCEEDS(audioCallbackDriver->mCubebOperationThread->Dispatch(
          NS_NewRunnableFunction(
              "ThreadedDriver previousDriver::Stop()",
              [audioCallbackDriver = RefPtr{audioCallbackDriver}] {
                audioCallbackDriver->Stop();
              })));
      mDriver->SetPreviousDriver(nullptr);
    }

    mDriver->RunThread();
    return NS_OK;
  }

 private:
  RefPtr<ThreadedDriver> mDriver;
};

void ThreadedDriver::Start() {
  MOZ_ASSERT(!ThreadRunning());
  LOG(LogLevel::Debug, ("Starting thread for a SystemClockDriver  {}",
                        fmt::ptr(mGraphInterface.get())));
  (void)NS_WARN_IF(mThread);
  MOZ_ASSERT(!mThread);  // Ensure we haven't already started it

  nsCOMPtr<nsIRunnable> event = new MediaTrackGraphInitThreadRunnable(this);
  // Note: mThread may be null during event->Run() if we pass to NewNamedThread!
  // See AudioInitTask
  nsresult rv = NS_NewNamedThread("MediaTrackGrph", getter_AddRefs(mThread));
  if (NS_SUCCEEDED(rv)) {
    mThread->Dispatch(event.forget(), NS_DISPATCH_NORMAL);
  }
}

void ThreadedDriver::Shutdown() {
  NS_ASSERTION(NS_IsMainThread(), "Must be called on main thread");
  // mGraph's thread is not running so it's OK to do whatever here
  LOG(LogLevel::Debug,
      ("Stopping threads for MediaTrackGraph {}", fmt::ptr(this)));

  if (mThread) {
    LOG(LogLevel::Debug, ("{}: Stopping ThreadedDriver's {} thread",
                          fmt::ptr(Graph()), fmt::ptr(this)));
    mThread->AsyncShutdown();
    mThread = nullptr;
  }
}

SystemClockDriver::SystemClockDriver(GraphInterface* aGraphInterface,
                                     GraphDriver* aPreviousDriver,
                                     uint32_t aSampleRate)
    : ThreadedDriver(aGraphInterface, aPreviousDriver, aSampleRate),
      mInitialTimeStamp(TimeStamp::Now()) {}

SystemClockDriver::~SystemClockDriver() = default;

void SystemClockDriver::RunThread() {
#ifdef XP_MACOSX
  // Timed waits of normal priority threads can be coalesced far beyond their
  // deadline when the process is otherwise idle, real-time threads are exempt.
  atp_handle* handle = atp_promote_current_thread_to_real_time(0, mSampleRate);
#endif

  ThreadedDriver::RunThread();

#ifdef XP_MACOSX
  if (handle) {
    atp_demote_current_thread_from_real_time(handle);
  }
#endif
}

void ThreadedDriver::RunThread() {
  mThreadRunning = true;
  while (true) {
    WaitForNextIteration();

    MediaTime interval = GetIntervalForIteration();
    GraphTime nextStateComputedTime = mStateComputedTime + interval;
    LOG(LogLevel::Verbose,
        ("{}: interval[{}; {}]", fmt::ptr(Graph()), (long)mStateComputedTime,
         (long)nextStateComputedTime));

    mStateComputedTime = nextStateComputedTime;
    IterationResult result = Graph()->OneIteration(mStateComputedTime, nullptr);

    if (result.IsStop()) {
      // Signal that we're done stopping.
      result.Stopped();
      break;
    }
    if (GraphDriver* nextDriver = result.NextDriver()) {
      LOG(LogLevel::Debug,
          ("{}: Switching to AudioCallbackDriver", fmt::ptr(Graph())));
      result.Switched();
      nextDriver->SetState(mStreamName, mStateComputedTime,
                           mTargetIterationTimeStamp);
      nextDriver->Start();
      break;
    }
    MOZ_ASSERT(result.IsStillProcessing());
  }
  mThreadRunning = false;
}

MediaTime SystemClockDriver::GetIntervalForIteration() {
  return MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(
      MillisecondsToMediaTime(MEDIA_GRAPH_TARGET_PERIOD_MS));
}

void ThreadedDriver::EnsureNextIteration() {
  mWaitHelper.EnsureNextIteration();
}

void ThreadedDriver::WaitForNextIteration() {
  MOZ_ASSERT(mThread);
  MOZ_ASSERT(OnThread());
  mWaitHelper.WaitForNextIterationAtLeast(NextIterationWaitDuration());
}

TimeDuration ThreadedDriver::IterationDuration() {
  return MediaTimeToTimeDuration(GetIntervalForIteration());
}

TimeDuration SystemClockDriver::NextIterationWaitDuration() {
  MOZ_ASSERT(mThread);
  MOZ_ASSERT(OnThread());
  TimeStamp now = TimeStamp::Now();
  if (mTargetIterationTimeStamp.IsNull()) {
    // No previous driver with which to synchronize rendering.
    // Start rendering now.
    mTargetIterationTimeStamp = now;
  } else {
    mTargetIterationTimeStamp += IterationDuration();
  }
  TimeDuration timeout = mTargetIterationTimeStamp - now;
  if (timeout <
      TimeDuration::FromMilliseconds(-SYSTEM_CLOCK_BANKRUPTCY_THRESHOLD_MS)) {
    // Don't try to catch up because rendering has fallen so far behind.
    // Instead try to render at consistent time intervals from now.
    LOG(LogLevel::Warning, ("{}: Global underrun detected", fmt::ptr(Graph())));
    mTargetIterationTimeStamp = now;
  }

  LOG(LogLevel::Verbose,
      ("{}: Waiting for next iteration; at {} (real {}), timeout={}",
       fmt::ptr(Graph()), MediaTimeToSeconds(mStateComputedTime),
       (now - mInitialTimeStamp).ToSeconds(), timeout.ToSeconds()));
  return timeout;
}

OfflineClockDriver::OfflineClockDriver(GraphInterface* aGraphInterface,
                                       uint32_t aSampleRate)
    : ThreadedDriver(aGraphInterface, nullptr, aSampleRate) {}

OfflineClockDriver::~OfflineClockDriver() = default;

void OfflineClockDriver::RunThread() {
  nsCOMPtr<nsIThreadInternal> threadInternal = do_QueryInterface(mThread);
  nsCOMPtr<nsIThreadObserver> observer = do_QueryInterface(Graph());
  threadInternal->SetObserver(observer);

  ThreadedDriver::RunThread();
}

MediaTime OfflineClockDriver::GetIntervalForIteration() {
  return MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(std::clamp<MediaTime>(
      mEndTime - mStateComputedTime, 0,
      MillisecondsToMediaTime(MEDIA_GRAPH_TARGET_PERIOD_MS)));
}

/* Helper to proxy the GraphInterface methods used by a running
 * mFallbackDriver. */
class AudioCallbackDriver::FallbackWrapper : public GraphInterface {
 public:
  FallbackWrapper(RefPtr<GraphInterface> aGraph,
                  RefPtr<AudioCallbackDriver> aOwner, uint32_t aSampleRate,
                  const nsACString& aStreamName, GraphTime aStateComputedTime,
                  TimeStamp aIterationTimeStamp)
      : mGraph(std::move(aGraph)),
        mOwner(std::move(aOwner)),
        mFallbackDriver(
            MakeRefPtr<SystemClockDriver>(this, nullptr, aSampleRate)) {
    mFallbackDriver->SetState(aStreamName, aStateComputedTime,
                              aIterationTimeStamp);
  }

  NS_DECL_THREADSAFE_ISUPPORTS

  /* Proxied SystemClockDriver methods */
  void Start() { mFallbackDriver->Start(); }
  MOZ_CAN_RUN_SCRIPT void Shutdown() {
    RefPtr<SystemClockDriver> driver = mFallbackDriver;
    driver->Shutdown();
  }
  void SetStreamName(const nsACString& aStreamName) {
    mFallbackDriver->SetStreamName(aStreamName);
  }
  void EnsureNextIteration() { mFallbackDriver->EnsureNextIteration(); }
#ifdef DEBUG
  bool InIteration() { return mFallbackDriver->InIteration(); }
#endif
  bool OnThread() { return mFallbackDriver->OnThread(); }

  /* GraphInterface methods */
  void NotifyInputStopped() override {
    MOZ_CRASH("Unexpected NotifyInputStopped from fallback SystemClockDriver");
  }
  void NotifyInputData(const AudioDataValue* aBuffer, size_t aFrames,
                       TrackRate aRate, uint32_t aChannels,
                       uint32_t aAlreadyBuffered) override {
    MOZ_CRASH("Unexpected NotifyInputData from fallback SystemClockDriver");
  }
  void NotifySetRequestedInputProcessingParamsResult(
      AudioCallbackDriver* aDriver, int aGeneration,
      Result<cubeb_input_processing_params, int>&& aResult) override {
    MOZ_CRASH(
        "Unexpected processing params result from fallback SystemClockDriver");
  }
  void DeviceChanged() override {
    MOZ_CRASH("Unexpected DeviceChanged from fallback SystemClockDriver");
  }
#ifdef DEBUG
  bool InDriverIteration(const GraphDriver* aDriver) const override {
    return mGraph->InDriverIteration(mOwner) && mOwner->OnFallback();
  }
#endif
  IterationResult OneIteration(GraphTime aStateComputedEnd,
                               MixerCallbackReceiver* aMixerReceiver) override {
    MOZ_ASSERT(!aMixerReceiver);

#ifdef DEBUG
    AutoInCallback aic(mOwner);
#endif

    IterationResult result =
        mGraph->OneIteration(aStateComputedEnd, aMixerReceiver);

    AudioStreamState audioState = mOwner->mAudioStreamState;

    MOZ_ASSERT(audioState != AudioStreamState::Stopping,
               "The audio driver can only enter stopping if it iterated the "
               "graph, which it can only do if there's no fallback driver");

    // After a devicechange event from the audio driver, wait for a five
    // millisecond grace period before handing control to the audio driver. We
    // do this because cubeb leaves no guarantee on audio callbacks coming in
    // after a device change event.
    if (audioState == AudioStreamState::ChangingDevice &&
        mOwner->mChangingDeviceStartTime + TimeDuration::FromMilliseconds(5) <
            TimeStamp::Now()) {
      mOwner->mChangingDeviceStartTime = TimeStamp();
      if (mOwner->mAudioStreamState.compareExchange(
              AudioStreamState::ChangingDevice, AudioStreamState::Starting)) {
        audioState = AudioStreamState::Starting;
        LOG(LogLevel::Debug, ("{}: Fallback driver has started. Waiting for "
                              "audio driver to start.",
                              fmt::ptr(mOwner.get())));
      }
    }

    if (audioState != AudioStreamState::Running && result.IsStillProcessing()) {
      mOwner->MaybeStartAudioStream();
      return result;
    }

    MOZ_ASSERT(result.IsStillProcessing() || result.IsStop() ||
               result.IsSwitchDriver());

    IterationResult stopFallback =
        IterationResult::CreateStop(NS_NewRunnableFunction(
            "AudioCallbackDriver::FallbackDriverStopped",
            [self = RefPtr<FallbackWrapper>(this), this, aStateComputedEnd,
             result = std::move(result)]() mutable {
              FallbackDriverState fallbackState =
                  result.IsStillProcessing() ? FallbackDriverState::None
                                             : FallbackDriverState::Stopped;
              mOwner->FallbackDriverStopped(
                  aStateComputedEnd, mFallbackDriver->IterationTimeStamp(),
                  fallbackState);

              if (fallbackState == FallbackDriverState::Stopped) {
#ifdef DEBUG
                // The AudioCallbackDriver may not iterate the graph, but we'll
                // call into it so we need to be regarded as "in iteration".
                AutoInCallback aic(mOwner);
#endif
                if (GraphDriver* nextDriver = result.NextDriver()) {
                  LOG(LogLevel::Debug,
                      ("{}: Switching from fallback to other driver.",
                       fmt::ptr(mOwner.get())));
                  result.Switched();
                  nextDriver->SetState(mOwner->mStreamName, aStateComputedEnd,
                                       mFallbackDriver->IterationTimeStamp());
                  nextDriver->Start();
                } else if (result.IsStop()) {
                  LOG(LogLevel::Debug, ("{}: Stopping fallback driver.",
                                        fmt::ptr(mOwner.get())));
                  result.Stopped();
                }
              }
              mOwner = nullptr;
              // Proxy the release of the fallback driver to a background
              // thread, so it doesn't perform unexpected suicide.
              NS_DispatchBackgroundTask(NS_NewRunnableFunction(
                  "AudioCallbackDriver::FallbackDriverStopped::Release",
                  [fallback = std::move(self->mFallbackDriver)] {}));
            }));

    return stopFallback;
  }

 private:
  virtual ~FallbackWrapper() = default;

  const RefPtr<GraphInterface> mGraph;
  // Valid until mFallbackDriver has finished its last iteration.
  RefPtr<AudioCallbackDriver> mOwner;
  RefPtr<SystemClockDriver> mFallbackDriver;
};

NS_IMPL_ISUPPORTS0(AudioCallbackDriver::FallbackWrapper)

/* static */
already_AddRefed<TaskQueue> AudioCallbackDriver::CreateTaskQueue() {
  return TaskQueue::Create(CubebUtils::GetCubebOperationThread(),
                           "AudioCallbackDriver cubeb task queue")
      .forget();
}

AudioCallbackDriver::AudioCallbackDriver(
    GraphInterface* aGraphInterface, GraphDriver* aPreviousDriver,
    uint32_t aSampleRate, uint32_t aOutputChannelCount,
    uint32_t aInputChannelCount, CubebUtils::AudioDeviceID aOutputDeviceID,
    CubebUtils::AudioDeviceID aInputDeviceID, AudioInputType aAudioInputType,
    Maybe<AudioInputProcessingParamsRequest> aRequestedInputProcessingParams)
    : GraphDriver(aGraphInterface, aPreviousDriver, aSampleRate),
      mOutputChannelCount(aOutputChannelCount),
      mInputChannelCount(aInputChannelCount),
      mOutputDeviceID(aOutputDeviceID),
      mInputDeviceID(aInputDeviceID),
      mIterationDurationMS(MEDIA_GRAPH_TARGET_PERIOD_MS),
      mCubebOperationThread(CreateTaskQueue()),
      mInputProcessingRequest(aRequestedInputProcessingParams.valueOr(
          AudioInputProcessingParamsRequest{})),
      mAudioThreadId(ProfilerThreadId{}),
      mAudioThreadIdInCb(std::thread::id()),
      mFallback("AudioCallbackDriver::mFallback"),
      mSandboxed(CubebUtils::SandboxEnabled()) {
  LOG(LogLevel::Debug,
      ("{}: AudioCallbackDriver {} ctor - input: device {}, "
       "channel {}, output: device {}, channel {}",
       fmt::ptr(Graph()), fmt::ptr(this), fmt::ptr(mInputDeviceID),
       mInputChannelCount, fmt::ptr(mOutputDeviceID), mOutputChannelCount));

  NS_WARNING_ASSERTION(mOutputChannelCount != 0,
                       "Invalid output channel count");

  if (aAudioInputType == AudioInputType::Voice &&
      StaticPrefs::
          media_getusermedia_microphone_prefer_voice_stream_with_processing_enabled()) {
    LOG(LogLevel::Debug,
        ("{}: AudioCallbackDriver {} ctor - using VOICE and requesting input "
         "processing params {} (Gen {}).",
         fmt::ptr(Graph()), fmt::ptr(this),
         CubebUtils::ProcessingParamsToString(mInputProcessingRequest.mParams)
             .get(),
         mInputProcessingRequest.mGeneration));
    mInputDevicePreference = CUBEB_DEVICE_PREF_VOICE;
    CubebUtils::SetInCommunication(true);
  } else {
    mInputDevicePreference = CUBEB_DEVICE_PREF_ALL;
  }
}

AudioCallbackDriver::~AudioCallbackDriver() {
  if (mInputDevicePreference == CUBEB_DEVICE_PREF_VOICE) {
    CubebUtils::SetInCommunication(false);
  }
}

void AudioCallbackDriver::Init(const nsCString& aStreamName) {
  LOG(LogLevel::Debug, ("{}: AudioCallbackDriver::Init driver={}",
                        fmt::ptr(Graph()), fmt::ptr(this)));
  TRACE("AudioCallbackDriver::Init");
  MOZ_ASSERT(OnCubebOperationThread());
  MOZ_ASSERT(mAudioStreamState == AudioStreamState::Pending);
  if (mFallbackDriverState == FallbackDriverState::Stopped) {
    // The graph has already stopped us.
    return;
  }
  RefPtr<CubebUtils::CubebHandle> handle = CubebUtils::GetCubeb();
  if (!handle) {
    NS_WARNING("Could not get cubeb context.");
    LOG(LogLevel::Warning, ("{}: Could not get cubeb context", __func__));
    mAudioStreamState = AudioStreamState::None;
    if (TryStartingFallbackDriver().isOk()) {
      CubebUtils::ReportCubebStreamInitFailure(true);
    }
    return;
  }

  cubeb_stream_params output;
  cubeb_stream_params input;
  bool firstStream = CubebUtils::GetFirstStream();

  MOZ_ASSERT(!NS_IsMainThread(),
             "This is blocking and should never run on the main thread.");

  output.rate = mSampleRate;
  output.format = CUBEB_SAMPLE_FLOAT32NE;

  if (!mOutputChannelCount) {
    LOG(LogLevel::Warning, ("Output number of channels is 0."));
    mAudioStreamState = AudioStreamState::None;
    if (TryStartingFallbackDriver().isOk()) {
      CubebUtils::ReportCubebStreamInitFailure(firstStream);
    }
    return;
  }

  CubebUtils::AudioDeviceID forcedOutputDeviceId = nullptr;

  char* forcedOutputDeviceName = CubebUtils::GetForcedOutputDevice();
  if (forcedOutputDeviceName) {
    RefPtr<CubebDeviceEnumerator> enumerator = Enumerator::GetInstance();
    RefPtr<AudioDeviceInfo> device = enumerator->DeviceInfoFromName(
        NS_ConvertUTF8toUTF16(forcedOutputDeviceName), EnumeratorSide::OUTPUT);
    if (device && device->DeviceID()) {
      forcedOutputDeviceId = device->DeviceID();
    }
  }

  mBuffer = AudioCallbackBufferWrapper<AudioDataValue>(mOutputChannelCount);
  mScratchBuffer =
      SpillBuffer<AudioDataValue, WEBAUDIO_BLOCK_SIZE * 2>(mOutputChannelCount);

  output.channels = mOutputChannelCount;
  AudioConfig::ChannelLayout::ChannelMap channelMap =
      AudioConfig::ChannelLayout(mOutputChannelCount).Map();

  output.layout = static_cast<uint32_t>(channelMap);
  output.prefs = CubebUtils::GetDefaultStreamPrefs(CUBEB_DEVICE_TYPE_OUTPUT);
  if (mInputDevicePreference == CUBEB_DEVICE_PREF_VOICE &&
      CubebUtils::RouteOutputAsVoice()) {
    output.prefs |= static_cast<cubeb_stream_prefs>(CUBEB_STREAM_PREF_VOICE);
  }
  output.input_params = CUBEB_INPUT_PROCESSING_PARAM_NONE;

  uint32_t latencyFrames = CubebUtils::GetCubebMTGLatencyInFrames(&output);

  LOG(LogLevel::Debug, ("Minimum latency in frames: {}", latencyFrames));

  // Buffer sizes lower than 10ms are nowadays common. It's not very useful
  // when doing voice, because all the WebRTC code that does audio input
  // processing deals in 10ms chunks of audio. Take the first power of two
  // above 10ms at the current rate in this case. It's probably 512, for common
  // rates.
  if (mInputDevicePreference == CUBEB_DEVICE_PREF_VOICE) {
    if (latencyFrames < mSampleRate / 100) {
      latencyFrames = mozilla::RoundUpPow2(mSampleRate / 100);
      LOG(LogLevel::Debug,
          ("AudioProcessing enabled, new latency {}", latencyFrames));
    }
  }

  // It's not useful for the graph to run with a block size lower than the Web
  // Audio API block size, but increasingly devices report that they can do
  // audio latencies lower than that.
  if (latencyFrames < WEBAUDIO_BLOCK_SIZE) {
    LOG(LogLevel::Debug,
        ("Latency clamped to {} from {}", WEBAUDIO_BLOCK_SIZE, latencyFrames));
    latencyFrames = WEBAUDIO_BLOCK_SIZE;
  }
  LOG(LogLevel::Debug, ("Effective latency in frames: {}", latencyFrames));

  input = output;
  input.channels = mInputChannelCount;
  input.layout = CUBEB_LAYOUT_UNDEFINED;
  input.prefs = CubebUtils::GetDefaultStreamPrefs(CUBEB_DEVICE_TYPE_INPUT);
  if (mInputDevicePreference == CUBEB_DEVICE_PREF_VOICE) {
    input.prefs |= static_cast<cubeb_stream_prefs>(CUBEB_STREAM_PREF_VOICE);
  }
  input.input_params = CUBEB_INPUT_PROCESSING_PARAM_NONE;

  cubeb_stream* stream = nullptr;
  const char* streamName =
      aStreamName.IsEmpty() ? "AudioCallbackDriver" : aStreamName.get();
  bool inputWanted = mInputChannelCount > 0;
  CubebUtils::AudioDeviceID outputId = mOutputDeviceID;
  CubebUtils::AudioDeviceID inputId = mInputDeviceID;

  if (CubebUtils::CubebStreamInit(
          handle->Context(), &stream, streamName, inputId,
          inputWanted ? &input : nullptr,
          forcedOutputDeviceId ? forcedOutputDeviceId : outputId, &output,
          latencyFrames, DataCallback_s, StateCallback_s, this) == CUBEB_OK) {
    mCubeb = handle;
    auto streamLocked = mAudioStream.Lock();
    streamLocked->own(stream);
    DebugOnly<int> rv = cubeb_stream_set_volume(streamLocked.ref(),
                                                CubebUtils::GetVolumeScale());
    NS_WARNING_ASSERTION(
        rv == CUBEB_OK,
        "Could not set the audio stream volume in GraphDriver.cpp");
    CubebUtils::ReportCubebBackendUsed();
  } else {
    NS_WARNING(
        "Could not create a cubeb stream for MediaTrackGraph, falling "
        "back to a SystemClockDriver");
    mAudioStreamState = AudioStreamState::None;
    // Only report failures when we're not coming from a driver that was
    // created itself as a fallback driver because of a previous audio driver
    // failure.
    if (TryStartingFallbackDriver().isOk()) {
      CubebUtils::ReportCubebStreamInitFailure(firstStream);
    }
    return;
  }

#ifdef XP_MACOSX
  PanOutputIfNeeded(inputWanted);
#endif

  if (inputWanted && InputDevicePreference() == AudioInputType::Voice) {
    SetInputProcessingParams(mInputProcessingRequest);
  }

  {
    auto streamLocked = mAudioStream.Lock();
    cubeb_stream_register_device_changed_callback(
        streamLocked.ref(), AudioCallbackDriver::DeviceChangedCallback_s);
  }

  // No-op if MOZ_DUMP_AUDIO is not defined as an environment variable. This
  // is intended for diagnosing issues, and only works if the content sandbox is
  // disabled.
  mInputStreamFile.Open("GraphDriverInput", input.channels, input.rate);
  mOutputStreamFile.Open("GraphDriverOutput", output.channels, output.rate);

  if (NS_WARN_IF(!StartStream())) {
    LOG(LogLevel::Warning,
        ("{}: AudioCallbackDriver couldn't start a cubeb stream.",
         fmt::ptr(Graph())));
    return;
  }

  LOG(LogLevel::Debug, ("{}: AudioCallbackDriver started.", fmt::ptr(Graph())));
}

void AudioCallbackDriver::SetCubebStreamName(const nsCString& aStreamName) {
  MOZ_ASSERT(OnCubebOperationThread());
  auto streamLocked = mAudioStream.Lock();
  cubeb_stream_set_name(streamLocked.ref(), aStreamName.get());
}

void AudioCallbackDriver::Start() {
  MOZ_ASSERT(!IsStarted());
  MOZ_ASSERT(mAudioStreamState == AudioStreamState::None);
  MOZ_ASSERT_IF(PreviousDriver(), PreviousDriver()->InIteration());
  MOZ_ASSERT(mFallbackDriverState == FallbackDriverState::None);
  // Starting an audio driver could take a while. We will start running from a
  // fallback system driver in the meantime so that the graph is kept running.
  // mFallback is set up before starting the cubeb stream so that the cubeb
  // callbacks know that the fallback driver is running and can
  // EnsureNextIteration() so the fallback driver can hand graph control back to
  // the cubeb data callback.
  mFallbackDriverState = FallbackDriverState::Running;
  RefPtr fallback = CreateFallbackSystemClockDriver();

  if (mPreviousDriver) {
    if (AudioCallbackDriver* previousAudioCallback =
            mPreviousDriver->AsAudioCallbackDriver()) {
      LOG(LogLevel::Debug, ("Releasing audio driver off main thread."));
      MOZ_ALWAYS_SUCCEEDS(
          previousAudioCallback->mCubebOperationThread->Dispatch(
              NS_NewRunnableFunction(
                  "AudioCallbackDriver previousDriver::Stop()",
                  [previousDriver = RefPtr{previousAudioCallback}] {
                    previousDriver->Stop();
                  })));
    } else {
      LOG(LogLevel::Debug,
          ("Dropping driver reference for SystemClockDriver."));
      MOZ_ASSERT(mPreviousDriver->AsSystemClockDriver());
    }
    mPreviousDriver = nullptr;
  }

  LOG(LogLevel::Debug, ("Starting new audio driver off main thread, "
                        "to ensure it runs after previous shutdown."));
  QueueInitOp();
  // Hand over control of the graph to the fallback driver after init is queued
  // so that future cubeb operations on mAudioStream will be in order.
  fallback->Start();
}

void AudioCallbackDriver::QueueInitOp() {
  MOZ_ASSERT(mAudioStreamState == AudioStreamState::None);
  mAudioStreamState = AudioStreamState::Pending;

  MOZ_ALWAYS_SUCCEEDS(mCubebOperationThread->Dispatch(
      NS_NewRunnableFunction("AudioCallbackDriver Init()",
                             [self = RefPtr{this}, streamName = mStreamName] {
                               self->Init(streamName);
                             })));
}

bool AudioCallbackDriver::StartStream() {
  TRACE("AudioCallbackDriver::StartStream");
  MOZ_ASSERT(!IsStarted() && OnCubebOperationThread());
  // Set STARTING before cubeb_stream_start, since starting the cubeb stream
  // can result in a callback (that may read mAudioStreamState) before
  // mAudioStreamState would otherwise be set.
  mAudioStreamState = AudioStreamState::Starting;
  auto streamLocked = mAudioStream.Lock();
  if (cubeb_stream_start(streamLocked.ref()) != CUBEB_OK) {
    NS_WARNING("Could not start cubeb stream for MTG.");
    return false;
  }

  return true;
}

void AudioCallbackDriver::Stop() {
  LOG(LogLevel::Debug, ("{}: AudioCallbackDriver::Stop driver={}",
                        fmt::ptr(Graph()), fmt::ptr(this)));
  TRACE("AudioCallbackDriver::Stop");
  MOZ_ASSERT(OnCubebOperationThread());
  auto streamLocked = mAudioStream.Lock();
  cubeb_stream_register_device_changed_callback(streamLocked.ref(), nullptr);
  if (cubeb_stream_stop(streamLocked.ref()) != CUBEB_OK) {
    NS_WARNING("Could not stop cubeb stream for MTG.");
  } else {
    mAudioStreamState = AudioStreamState::None;
  }
}

void AudioCallbackDriver::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<FallbackWrapper> fallback;
  {
    auto fallbackLock = mFallback.Lock();
    fallback = fallbackLock.ref();
    fallbackLock.ref() = nullptr;
  }
  if (fallback) {
    LOG(LogLevel::Debug, ("{}: Releasing fallback driver {}.",
                          fmt::ptr(Graph()), fmt::ptr(fallback.get())));
    fallback->Shutdown();
  }

  LOG(LogLevel::Debug,
      ("{}: Releasing audio driver off main thread (GraphDriver::Shutdown).",
       fmt::ptr(Graph())));

  nsLiteralCString reason("AudioCallbackDriver::Shutdown");
  NS_DispatchAndSpinEventLoopUntilComplete(
      reason, mCubebOperationThread,
      NS_NewRunnableFunction(reason.get(),
                             [self = RefPtr{this}] { self->Stop(); }));
}

void AudioCallbackDriver::SetStreamName(const nsACString& aStreamName) {
  MOZ_ASSERT(InIteration() || !ThreadRunning());
  if (aStreamName == mStreamName) {
    return;
  }
  // Record the stream name, which will be passed onto the next driver, if
  // any, either from this driver or the fallback driver.
  GraphDriver::SetStreamName(aStreamName);
  {
    auto fallbackLock = mFallback.Lock();
    FallbackWrapper* fallback = fallbackLock.ref().get();
    if (fallback) {
      MOZ_ASSERT(fallback->InIteration());
      fallback->SetStreamName(aStreamName);
    }
  }
  AudioStreamState streamState = mAudioStreamState;
  if (streamState != AudioStreamState::None &&
      streamState != AudioStreamState::Stopping) {
    MOZ_ALWAYS_SUCCEEDS(mCubebOperationThread->Dispatch(
        NS_NewRunnableFunction("AudioCallbackDriver SetStreamName()",
                               [self = RefPtr{this}, streamName = mStreamName] {
                                 self->SetCubebStreamName(streamName);
                               })));
  }
}

/* static */
long AudioCallbackDriver::DataCallback_s(cubeb_stream* aStream, void* aUser,
                                         const void* aInputBuffer,
                                         void* aOutputBuffer, long aFrames) {
  AudioCallbackDriver* driver = reinterpret_cast<AudioCallbackDriver*>(aUser);
  return driver->DataCallback(static_cast<const AudioDataValue*>(aInputBuffer),
                              static_cast<AudioDataValue*>(aOutputBuffer),
                              aFrames);
}

/* static */
void AudioCallbackDriver::StateCallback_s(cubeb_stream* aStream, void* aUser,
                                          cubeb_state aState) {
  AudioCallbackDriver* driver = reinterpret_cast<AudioCallbackDriver*>(aUser);
  driver->StateCallback(aState);
}

/* static */
void AudioCallbackDriver::DeviceChangedCallback_s(void* aUser) {
  AudioCallbackDriver* driver = reinterpret_cast<AudioCallbackDriver*>(aUser);
  driver->DeviceChangedCallback();
}

AudioCallbackDriver::AutoInCallback::AutoInCallback(
    AudioCallbackDriver* aDriver)
    : mDriver(aDriver) {
  MOZ_ASSERT(mDriver->mAudioThreadIdInCb == std::thread::id());
  mDriver->mAudioThreadIdInCb = std::this_thread::get_id();
}

AudioCallbackDriver::AutoInCallback::~AutoInCallback() {
  MOZ_ASSERT(mDriver->mAudioThreadIdInCb == std::this_thread::get_id());
  mDriver->mAudioThreadIdInCb = std::thread::id();
}

bool AudioCallbackDriver::CheckThreadIdChanged() {
  ProfilerThreadId id = profiler_current_thread_id();
  if (id != mAudioThreadId) {
    mAudioThreadId = id;
    return true;
  }
  return false;
}

long AudioCallbackDriver::DataCallback(const AudioDataValue* aInputBuffer,
                                       AudioDataValue* aOutputBuffer,
                                       long aFrames) {
  TimeStamp iterationStartTimeStamp = TimeStamp::Now();

  if (!mSandboxed && CheckThreadIdChanged()) {
    CallbackThreadRegistry::Get()->Register(mAudioThreadId,
                                            "NativeAudioCallback");
  }

  if (mAudioStreamState.compareExchange(AudioStreamState::Starting,
                                        AudioStreamState::Running)) {
    MOZ_ASSERT(mScratchBuffer.IsEmpty());
    mFirstCallbackIteration = true;
    LOG(LogLevel::Verbose, ("{}: AudioCallbackDriver {} First audio callback "
                            "close the Fallback driver",
                            fmt::ptr(Graph()), fmt::ptr(this)));
  }

  FallbackDriverState fallbackState = mFallbackDriverState;
  if (MOZ_UNLIKELY(fallbackState == FallbackDriverState::Stopped)) {
    // We're supposed to stop.
    PodZero(aOutputBuffer, aFrames * mOutputChannelCount);
    if (!mSandboxed) {
      CallbackThreadRegistry::Get()->Unregister(mAudioThreadId);
    }
    return aFrames - 1;
  }

  AudioStreamState audioStreamState = mAudioStreamState;
  if (MOZ_UNLIKELY(audioStreamState == AudioStreamState::ChangingDevice ||
                   fallbackState == FallbackDriverState::Running)) {
    // Wait for the fallback driver to stop. Wake it up so it can stop if it's
    // sleeping.
    LOG(LogLevel::Verbose,
        ("{}: AudioCallbackDriver {} Waiting for the Fallback driver to stop",
         fmt::ptr(Graph()), fmt::ptr(this)));
    EnsureNextIteration();
    PodZero(aOutputBuffer, aFrames * mOutputChannelCount);
    return aFrames;
  }

  MOZ_ASSERT(audioStreamState == AudioStreamState::Running);
  TRACE_AUDIO_CALLBACK_FRAME_COUNT("AudioCallbackDriver real-time budget",
                                   aFrames, mSampleRate);
  TRACE("AudioCallbackDriver::DataCallback");

#ifdef DEBUG
  AutoInCallback aic(this);
#endif

  uint32_t durationMS = aFrames * 1000 / mSampleRate;

  // For now, simply average the duration with the previous
  // duration so there is some damping against sudden changes.
  if (!mIterationDurationMS) {
    mIterationDurationMS = durationMS;
  } else {
    mIterationDurationMS = (mIterationDurationMS * 3) + durationMS;
    mIterationDurationMS /= 4;
  }

  mBuffer.SetBuffer(aOutputBuffer, aFrames);
  // fill part or all with leftover data from last iteration (since we
  // align to Audio blocks)
  uint32_t prefilledFrameCount = mScratchBuffer.Empty(mBuffer);

  if (mFirstCallbackIteration && !mTargetIterationTimeStamp.IsNull()) {
    // Determine how much would ideally be rendered in this iteration by
    // comparing the time this iteration started with the target start time of
    // the previous iteration.
    //
    // FallbackDriverStopped() will not set mTargetIterationTimeStamp in the
    // future, so mTargetIterationTimeStamp is usually before
    // iterationStartTimeStamp.  This is not guaranteed however as
    // iterationStartTimeStamp may have been recorded before the fallback driver
    // started its iteration at mTargetIterationTimeStamp.
    MediaTime renderingTime =
        MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(SecondsToMediaTime(
            (iterationStartTimeStamp - mTargetIterationTimeStamp).ToSeconds()));
    // There is no previous iteration from which to carry over mScratchBuffer.
    MOZ_ASSERT(prefilledFrameCount == 0);
    if (renderingTime < aFrames) {
      // The audio data callback has occurred soon after the previous driver's
      // rendering time.  Synchronize the rendering times of graph frames
      // under the audio callback with the rendering times under the previous
      // driver by padding the start of the provided buffer with silence.
      prefilledFrameCount = AssertedCast<uint32_t>(
          aFrames - std::max<MediaTime>(0, renderingTime));
      mBuffer.WriteSilence(prefilledFrameCount);
    }
  }
  mFirstCallbackIteration = false;

  // State computed time is decided by the audio callback's buffer length.
  GraphTime bufferEndGraphTime = mStateComputedTime + mBuffer.Available();
  GraphTime nextStateComputedTime =
      MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(bufferEndGraphTime);
  LOG(LogLevel::Verbose,
      ("{}: interval[{}; {}] (frames: {}) (durationMS: {}) "
       "(duration ticks: {})",
       fmt::ptr(Graph()), (long)mStateComputedTime, (long)nextStateComputedTime,
       (long)aFrames, (uint32_t)durationMS,
       (long)(nextStateComputedTime - mStateComputedTime)));

  // mTargetIterationTimeStamp is used to synchronize the timing of rendering
  // when switching to a different driver.  iterationStartTimeStamp, which
  // corresponds to bufferEndGraphTime, is adjusted for the extra rendering by
  // the graph due to rounding to block boundaries, so that
  // mTargetIterationTimeStamp corresponds to nextStateComputedTime.
  // This is calculated for all iterations, not just when the IterationResult
  // indicates a switch, because DeviceChangedCallback() might start a
  // fallback driver for the next iteration.
  mTargetIterationTimeStamp =
      iterationStartTimeStamp +
      MediaTimeToTimeDuration(nextStateComputedTime - bufferEndGraphTime);

  // Process mic data if any/needed
  if (aInputBuffer && mInputChannelCount > 0) {
    Graph()->NotifyInputData(aInputBuffer, static_cast<size_t>(aFrames),
                             mSampleRate, mInputChannelCount,
                             prefilledFrameCount);
  }

  IterationResult result = Graph()->OneIteration(nextStateComputedTime, this);

  mStateComputedTime = nextStateComputedTime;

  MOZ_ASSERT(mBuffer.Available() == 0,
             "The graph should have filled the buffer");

  mBuffer.BufferFilled();

#ifdef MOZ_SAMPLE_TYPE_FLOAT32
  // Prevent returning NaN to the OS mixer, and propagating NaN into the reverse
  // stream of the AEC.
  NaNToZeroInPlace(aOutputBuffer, aFrames * mOutputChannelCount);
#endif

#ifdef XP_MACOSX
  // This only happens when the output is on a macbookpro's external speaker,
  // that are stereo, but let's just be safe.
  if (mNeedsPanning && mOutputChannelCount == 2) {
    // hard pan to the right
    for (uint32_t i = 0; i < aFrames * 2; i += 2) {
      aOutputBuffer[i + 1] += aOutputBuffer[i];
      aOutputBuffer[i] = 0.0;
    }
  }
#endif

  // No-op if MOZ_DUMP_AUDIO is not defined as an environment variable
  if (aInputBuffer) {
    mInputStreamFile.Write(static_cast<const AudioDataValue*>(aInputBuffer),
                           aFrames * mInputChannelCount);
  }
  mOutputStreamFile.Write(static_cast<const AudioDataValue*>(aOutputBuffer),
                          aFrames * mOutputChannelCount);

  if (result.IsStop()) {
    if (mInputDeviceID) {
      mGraphInterface->NotifyInputStopped();
    }
    // Signal that we have stopped.
    result.Stopped();
    // Update the flag before handing over the graph and going to drain.
    mAudioStreamState = AudioStreamState::Stopping;
    if (!mSandboxed) {
      CallbackThreadRegistry::Get()->Unregister(mAudioThreadId);
    }
    return aFrames - 1;
  }

  if (GraphDriver* nextDriver = result.NextDriver()) {
    LOG(LogLevel::Debug,
        ("{}: Switching to {} driver.", fmt::ptr(Graph()),
         nextDriver->AsAudioCallbackDriver() ? "audio" : "system"));
    if (mInputDeviceID) {
      mGraphInterface->NotifyInputStopped();
    }
    result.Switched();
    mAudioStreamState = AudioStreamState::Stopping;
    nextDriver->SetState(mStreamName, mStateComputedTime,
                         iterationStartTimeStamp);
    nextDriver->Start();
    if (!mSandboxed) {
      CallbackThreadRegistry::Get()->Unregister(mAudioThreadId);
    }
    // Returning less than aFrames starts the draining and eventually stops the
    // audio thread. This function will never get called again.
    return aFrames - 1;
  }

  MOZ_ASSERT(result.IsStillProcessing());
  return aFrames;
}

static const char* StateToString(cubeb_state aState) {
  switch (aState) {
    case CUBEB_STATE_STARTED:
      return "STARTED";
    case CUBEB_STATE_STOPPED:
      return "STOPPED";
    case CUBEB_STATE_DRAINED:
      return "DRAINED";
    case CUBEB_STATE_ERROR:
      return "ERROR";
    default:
      MOZ_CRASH("Unexpected state!");
  }
}

void AudioCallbackDriver::StateCallback(cubeb_state aState) {
  MOZ_ASSERT(!InIteration());
  LOG(LogLevel::Debug, ("AudioCallbackDriver({}) State: {}", fmt::ptr(this),
                        StateToString(aState)));

  if (aState == CUBEB_STATE_STARTED || aState == CUBEB_STATE_STOPPED) {
    // Nothing to do for STARTED.
    //
    // For STOPPED, don't reset mAudioStreamState until after
    // cubeb_stream_stop() returns, as wasapi_stream_stop() dispatches
    // CUBEB_STATE_STOPPED before ensuring that data callbacks have finished.
    // https://searchfox.org/mozilla-central/rev/f9beb753a84aa297713d1565dcd0c5e3c66e4174/media/libcubeb/src/cubeb_wasapi.cpp#3009,3012
    return;
  }

  AudioStreamState streamState = mAudioStreamState;
  if (streamState < AudioStreamState::Starting) {
    // mAudioStream has already entered STOPPED, DRAINED, or ERROR.
    // Don't reset a Pending state indicating that a task to destroy
    // mAudioStream and init a new cubeb_stream has already been triggered.
    return;
  }

  // Reset for DRAINED or ERROR.
  streamState = mAudioStreamState.exchange(AudioStreamState::None);

  if (aState == CUBEB_STATE_ERROR) {
    // About to hand over control of the graph.  Do not start a new driver if
    // StateCallback() receives an error for this stream while the main thread
    // or another driver has control of the graph.
    if (streamState == AudioStreamState::Starting ||
        streamState == AudioStreamState::ChangingDevice ||
        streamState == AudioStreamState::Running) {
      if (mFallbackDriverState.compareExchange(FallbackDriverState::None,
                                               FallbackDriverState::Running)) {
        // Only switch to fallback if it's not already running. It could be
        // running with the callback driver having started but not seen a single
        // callback yet. I.e., handover from fallback to callback is not done.
        if (mInputDeviceID) {
#ifdef DEBUG
          // No audio callback after an error. We're calling into the graph here
          // so we need to be regarded as "in iteration".
          AutoInCallback aic(this);
#endif
          mGraphInterface->NotifyInputStopped();
        }
        CreateFallbackSystemClockDriver()->Start();
      }
    }
  }
}

void AudioCallbackDriver::MixerCallback(AudioChunk* aMixedBuffer,
                                        uint32_t aSampleRate) {
  MOZ_ASSERT(InIteration());
  uint32_t toWrite = mBuffer.Available();

  TrackTime frameCount = aMixedBuffer->mDuration;
  if (!mBuffer.Available() && frameCount > 0) {
    NS_WARNING("DataCallback buffer full, expect frame drops.");
  }

  MOZ_ASSERT(mBuffer.Available() <= frameCount);

  mBuffer.WriteFrames(*aMixedBuffer, mBuffer.Available());
  MOZ_ASSERT(mBuffer.Available() == 0,
             "Missing frames to fill audio callback's buffer.");
  if (toWrite == frameCount) {
    return;
  }

  aMixedBuffer->SliceTo(toWrite, frameCount);
  DebugOnly<uint32_t> written = mScratchBuffer.Fill(*aMixedBuffer);
  NS_WARNING_ASSERTION(written == frameCount - toWrite, "Dropping frames.");
};

void AudioCallbackDriver::PanOutputIfNeeded(bool aMicrophoneActive) {
#ifdef XP_MACOSX
  TRACE("AudioCallbackDriver::PanOutputIfNeeded");
  cubeb_device* out = nullptr;
  int rv;
  char name[128];
  size_t length = sizeof(name);

  rv = sysctlbyname("hw.model", name, &length, nullptr, 0);
  if (rv) {
    return;
  }

  int major, minor;
  for (uint32_t i = 0; i < length; i++) {
    // skip the model name
    if (isalpha(name[i])) {
      continue;
    }
    sscanf(name + i, "%d,%d", &major, &minor);
    break;
  }

  enum MacbookModel { MacBook, MacBookPro, MacBookAir, NotAMacbook };

  MacbookModel model;

  if (!strncmp(name, "MacBookPro", length)) {
    model = MacBookPro;
  } else if (strncmp(name, "MacBookAir", length)) {
    model = MacBookAir;
  } else if (strncmp(name, "MacBook", length)) {
    model = MacBook;
  } else {
    model = NotAMacbook;
  }
  // For macbook pro before 2016 model (change of chassis), hard pan the audio
  // to the right if the speakers are in use to avoid feedback.
  if (model == MacBookPro && major <= 12) {
    auto streamLocked = mAudioStream.Lock();
    if (cubeb_stream_get_current_device(streamLocked.ref(), &out) == CUBEB_OK) {
      MOZ_ASSERT(out);
      // Check if we are currently outputing sound on external speakers.
      if (out->output_name && !strcmp(out->output_name, "ispk")) {
        // Pan everything to the right speaker.
        LOG(LogLevel::Debug, ("Using the built-in speakers, with{} audio input",
                              aMicrophoneActive ? "" : "out"));
        mNeedsPanning = aMicrophoneActive;
      } else {
        LOG(LogLevel::Debug, ("Using an external output device"));
        mNeedsPanning = false;
      }
      cubeb_stream_device_destroy(streamLocked.ref(), out);
    }
  }
#endif
}

void AudioCallbackDriver::DeviceChangedCallback() {
  MOZ_ASSERT(!InIteration());
  // Set this before the atomic write.
  mChangingDeviceStartTime = TimeStamp::Now();

  if (mAudioStreamState.compareExchange(AudioStreamState::Running,
                                        AudioStreamState::ChangingDevice)) {
    // Change to ChangingDevice only if we're running, i.e. there has been a
    // data callback and no state callback saying otherwise.
    // - If the audio stream is not running, it has either been stopped or it is
    //   starting. In the latter case we assume there will be no data callback
    //   coming until after the device change is done.
    // - If the audio stream is running here, there is no guarantee from the
    //   cubeb mac backend that no more data callback will occur before the
    //   device change takes place. They will however stop *soon*, and we hope
    //   they stop before the first callback from the fallback driver. If the
    //   fallback driver callback occurs before the last data callback before
    //   the device switch, the worst case is that a long period of time
    //   (seconds) may pass without the graph getting iterated at all.
    Result<bool, FallbackDriverState> res = TryStartingFallbackDriver();

    LOG(LogLevel::Info,
        ("{}: AudioCallbackDriver {} underlying default device is changing. "
         "Fallback {}.",
         fmt::ptr(Graph()), fmt::ptr(this),
         res.isOk() ? "started"
                    : (res.inspectErr() == FallbackDriverState::Running
                           ? "already running"
                           : "has been stopped")));

    if (res.isErr() && res.inspectErr() == FallbackDriverState::Stopped) {
      mChangingDeviceStartTime = TimeStamp();
    }
  }

  // Tell the audio engine the device has changed, it might want to reset some
  // state.
  Graph()->DeviceChanged();
#ifdef XP_MACOSX
  RefPtr<AudioCallbackDriver> self(this);
  bool hasInput = mInputChannelCount;
  NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "PanOutputIfNeeded", [self{std::move(self)}, hasInput]() {
        self->PanOutputIfNeeded(hasInput);
      }));
#endif
}

TimeDuration AudioCallbackDriver::IterationDuration() {
  MOZ_ASSERT(InIteration());
  // The real fix would be to have an API in cubeb to give us the number. Short
  // of that, we approximate it here. bug 1019507
  return TimeDuration::FromMilliseconds(mIterationDurationMS);
}

void AudioCallbackDriver::EnsureNextIteration() {
  if (mFallbackDriverState == FallbackDriverState::Running) {
    auto fallback = mFallback.Lock();
    if (fallback.ref()) {
      fallback.ref()->EnsureNextIteration();
    }
  }
}

TimeDuration AudioCallbackDriver::AudioOutputLatency() {
  TRACE("AudioCallbackDriver::AudioOutputLatency");
  // Called from the main thread, unlike virtually everything else touching
  // mAudioStream, hence the lock: latency can change over the life of the
  // stream, so it must be queried live rather than cached at Init() time.
  uint32_t latencyFrames;
  auto streamLocked = mAudioStream.Lock();
  int rv = cubeb_stream_get_latency(streamLocked.ref(), &latencyFrames);
  if (rv || mSampleRate == 0) {
    return TimeDuration::Zero();
  }

  return TimeDuration::FromSeconds(static_cast<double>(latencyFrames) /
                                   mSampleRate);
}

bool AudioCallbackDriver::HasFallback() const {
  MOZ_ASSERT(InIteration());
  return mFallbackDriverState != FallbackDriverState::None;
}

bool AudioCallbackDriver::OnFallback() const {
  MOZ_ASSERT(InIteration());
  return mFallbackDriverState == FallbackDriverState::Running;
}

Result<bool, AudioCallbackDriver::FallbackDriverState>
AudioCallbackDriver::TryStartingFallbackDriver() {
  FallbackDriverState oldState =
      mFallbackDriverState.exchange(FallbackDriverState::Running);
  switch (oldState) {
    case FallbackDriverState::None:
      // None -> Running: we can start the fallback.
      CreateFallbackSystemClockDriver()->Start();
      return true;
    case FallbackDriverState::Stopped:
      // Stopped -> Running: Invalid edge, the graph has told us to stop.
      // Restore the state.
      mFallbackDriverState = oldState;
      [[fallthrough]];
    case FallbackDriverState::Running:
      // Nothing to do, return the state.
      return Err(oldState);
  }
  MOZ_CRASH("Unexpected fallback state");
}

RefPtr<AudioCallbackDriver::FallbackWrapper>
AudioCallbackDriver::CreateFallbackSystemClockDriver() {
  MOZ_ASSERT(mFallbackDriverState == FallbackDriverState::Running);
  DebugOnly<AudioStreamState> audioStreamState =
      static_cast<AudioStreamState>(mAudioStreamState);
  MOZ_ASSERT(audioStreamState == AudioStreamState::None ||
             audioStreamState == AudioStreamState::Pending ||
             audioStreamState == AudioStreamState::ChangingDevice);
  LOG(LogLevel::Debug,
      ("{}: AudioCallbackDriver {} Falling back to SystemClockDriver.",
       fmt::ptr(Graph()), fmt::ptr(this)));
  // On DeviceChangedCallback() or StateChangeCallback(), mScratchBuffer might
  // not be empty, but switching to a fallback driver is giving up on
  // outputting mScratchBuffer contiguously.
  // Clear the buffer so that it is not output later when an audio callback
  // arrives for a new discontiguous output stream.
  mScratchBuffer.Empty();

  mNextReInitBackoffStep =
      TimeDuration::FromMilliseconds(AUDIO_INITIAL_FALLBACK_BACKOFF_STEP_MS);
  mNextReInitAttempt = TimeStamp::Now() + mNextReInitBackoffStep;
  auto fallback = MakeRefPtr<FallbackWrapper>(Graph(), this, mSampleRate,
                                              mStreamName, mStateComputedTime,
                                              mTargetIterationTimeStamp);
  {
    auto driver = mFallback.Lock();
    MOZ_RELEASE_ASSERT(!driver.ref());
    driver.ref() = fallback;
  }
  return fallback;
}

void AudioCallbackDriver::FallbackDriverStopped(GraphTime aStateComputedTime,
                                                TimeStamp aIterationTimeStamp,
                                                FallbackDriverState aState) {
  LOG(LogLevel::Debug,
      ("{}: AudioCallbackDriver {} Fallback driver has stopped.",
       fmt::ptr(Graph()), fmt::ptr(this)));
  mStateComputedTime = aStateComputedTime;
  mTargetIterationTimeStamp = aIterationTimeStamp;
  mNextReInitAttempt = TimeStamp();
  mNextReInitBackoffStep = TimeDuration();
  {
    auto fallback = mFallback.Lock();
    MOZ_ASSERT(fallback.ref()->OnThread());
    fallback.ref() = nullptr;
  }

  MOZ_ASSERT(aState == FallbackDriverState::None ||
             aState == FallbackDriverState::Stopped);
  mFallbackDriverState = aState;
  AudioStreamState audioState = mAudioStreamState;
  LOG(LogLevel::Debug,
      ("{}: AudioCallbackDriver {} Fallback driver stopped.{}{}",
       fmt::ptr(Graph()), fmt::ptr(this),
       aState == FallbackDriverState::Stopped ? " Draining." : "",
       aState == FallbackDriverState::None &&
               audioState == AudioStreamState::ChangingDevice
           ? " Starting another due to device change."
           : ""));

  if (aState == FallbackDriverState::None) {
    MOZ_ASSERT(audioState == AudioStreamState::Running ||
               audioState == AudioStreamState::ChangingDevice);
    if (audioState == AudioStreamState::ChangingDevice) {
      MOZ_ALWAYS_OK(TryStartingFallbackDriver());
    }
  }
}

void AudioCallbackDriver::MaybeStartAudioStream() {
  AudioStreamState streamState = mAudioStreamState;
  if (streamState != AudioStreamState::None) {
    LOG(LogLevel::Verbose, ("{}: AudioCallbackDriver {} Cannot re-init.",
                            fmt::ptr(Graph()), fmt::ptr(this)));
    return;
  }

  TimeStamp now = TimeStamp::Now();
  if (now < mNextReInitAttempt) {
    LOG(LogLevel::Verbose,
        ("{}: AudioCallbackDriver {} Not time to re-init yet. {:.3f}s left.",
         fmt::ptr(Graph()), fmt::ptr(this),
         (mNextReInitAttempt - now).ToSeconds()));
    return;
  }

  LOG(LogLevel::Debug, ("{}: AudioCallbackDriver {} Attempting to re-init "
                        "audio stream from fallback driver.",
                        fmt::ptr(Graph()), fmt::ptr(this)));
  mNextReInitBackoffStep =
      std::min(mNextReInitBackoffStep * 2,
               TimeDuration::FromMilliseconds(
                   StaticPrefs::media_audio_device_retry_ms()));
  mNextReInitAttempt = now + mNextReInitBackoffStep;
  QueueInitOp();
}

const AudioInputProcessingParamsRequest&
AudioCallbackDriver::RequestedInputProcessingParams() const {
  MOZ_ASSERT(InIteration());
  return mInputProcessingRequest;
}

void AudioCallbackDriver::RequestInputProcessingParams(
    AudioInputProcessingParamsRequest aRequest) {
  MOZ_ASSERT(InIteration());
  MOZ_ASSERT(aRequest.mGeneration > mInputProcessingRequest.mGeneration);
  MOZ_ASSERT(aRequest.mParams != mInputProcessingRequest.mParams);
  LOG(LogLevel::Info,
      ("AudioCallbackDriver {}, Input processing params {} (Gen {}) requested.",
       fmt::ptr(this),
       CubebUtils::ProcessingParamsToString(aRequest.mParams).get(),
       aRequest.mGeneration));
  mInputProcessingRequest = aRequest;
  MOZ_ALWAYS_SUCCEEDS(mCubebOperationThread->Dispatch(
      NS_NewRunnableFunction(__func__, [this, self = RefPtr(this), aRequest] {
        SetInputProcessingParams(aRequest);
      })));
}

void AudioCallbackDriver::SetInputProcessingParams(
    AudioInputProcessingParamsRequest aRequest) {
  MOZ_ASSERT(OnCubebOperationThread());
  const auto requested = aRequest.mParams;
  auto params = aRequest.mParams;
  const auto generation = aRequest.mGeneration;
  auto streamLocked = mAudioStream.Lock();
  auto result = ([&]() -> Maybe<Result<cubeb_input_processing_params, int>> {
    // This function decides how to handle the request.
    // Returning Nothing() does nothing, because either
    //   1) there is no update since the previous state, or
    //   2) handling is deferred to a later time.
    // Returning Some() result will forward that result to
    // AudioDataListener::OnInputProcessingParamsResult on the callback
    // thread.
    if (!streamLocked.ref()) {
      // No Init yet.
      LOG(LogLevel::Debug, ("AudioCallbackDriver {}, has no cubeb stream to "
                            "set processing params on!",
                            fmt::ptr(this)));
      return Nothing();
    }
    if (mAudioStreamState == AudioStreamState::None) {
      // Driver (and cubeb stream) was stopped.
      return Nothing();
    }
    cubeb_input_processing_params supported;
    auto handle = CubebUtils::GetCubeb();
    int r = cubeb_get_supported_input_processing_params(handle->Context(),
                                                        &supported);
    if (r != CUBEB_OK) {
      LOG(LogLevel::Debug,
          ("AudioCallbackDriver {}, no supported processing params",
           fmt::ptr(this)));
      return Some(Err(CUBEB_ERROR_NOT_SUPPORTED));
    }
    params &= supported;
    LOG(LogLevel::Debug,
        ("AudioCallbackDriver {}, requested processing params {} (gen {}) "
         "reduced to {} by supported params {}",
         fmt::ptr(this), CubebUtils::ProcessingParamsToString(requested).get(),
         generation, CubebUtils::ProcessingParamsToString(params).get(),
         CubebUtils::ProcessingParamsToString(supported).get()));
    if (params == mConfiguredInputProcessingParams) {
      LOG(LogLevel::Debug,
          ("AudioCallbackDriver {}, no change in processing params {}. Not "
           "attempting reconfiguration.",
           fmt::ptr(this), CubebUtils::ProcessingParamsToString(params).get()));
      return Some(params);
    }
    mConfiguredInputProcessingParams = params;
    r = cubeb_stream_set_input_processing_params(streamLocked.ref(), params);
    if (r == CUBEB_OK) {
      LOG(LogLevel::Info,
          ("AudioCallbackDriver {}, input processing params set to {}",
           fmt::ptr(this), CubebUtils::ProcessingParamsToString(params).get()));
      return Some(params);
    }
    LOG(LogLevel::Info,
        ("AudioCallbackDriver {}, failed setting input processing params to "
         "{}. r={}",
         fmt::ptr(this), CubebUtils::ProcessingParamsToString(params).get(),
         r));
    return Some(Err(r));
  })();
  if (!result) {
    return;
  }
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(
      NS_NewRunnableFunction(__func__, [this, self = RefPtr(this), generation,
                                        result = result.extract()]() mutable {
        LOG(LogLevel::Debug,
            ("AudioCallbackDriver {}, Notifying of input processing params {} "
             "(Gen {}). r={}",
             fmt::ptr(this),
             CubebUtils::ProcessingParamsToString(
                 result.unwrapOr(CUBEB_INPUT_PROCESSING_PARAM_NONE))
                 .get(),
             generation, result.isErr() ? result.inspectErr() : CUBEB_OK));
        mGraphInterface->NotifySetRequestedInputProcessingParamsResult(
            this, generation, std::move(result));
      })));
}

}  // namespace mozilla

// avoid redefined macro in unified build
#undef LOG
