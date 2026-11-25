/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GRAPHDRIVER_H_
#define GRAPHDRIVER_H_

#include <thread>

#include "AudioBufferUtils.h"
#include "AudioMixer.h"
#include "AudioSegment.h"
#include "SelfRef.h"
#include "WavDumper.h"
#include "mozilla/Atomics.h"
#include "mozilla/DataMutex.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/dom/AudioContext.h"
#include "nsAutoRef.h"
#include "nsIThread.h"

struct cubeb_stream;

template <>
class nsAutoRefTraits<cubeb_stream> : public nsPointerRefTraits<cubeb_stream> {
 public:
  static void Release(cubeb_stream* aStream) { cubeb_stream_destroy(aStream); }
};

namespace mozilla {
/**
 * Assume we can run an iteration of the MediaTrackGraph loop in this much time
 * or less.
 * We try to run the control loop at this rate.
 */
static const int MEDIA_GRAPH_TARGET_PERIOD_MS = 10;
/**
 * The SystemClockDriver does not necessarily wake up as precisely as an
 * AudioCallbackDriver.  SleepConditionVariableSRW() has been observed to wake
 * almost 30ms late on Windows 10 2009 systems, which implies a lower timer
 * resolution than the 15.6ms default system-wide timer resolution in Windows.
 * https://download.microsoft.com/download/3/0/2/3027d574-c433-412a-a8b6-5e0a75d5b237/timer-resolution.docx
 *
 * Allow a SystemClockDriver to try to catch up when rendering is up to this
 * many milliseconds late, so that rendered time is close to clock time.  When
 * later than this threshold, SystemClockDriver will declare bankruptcy and
 * re-sync target render time with a new clock time.
 */
static const int SYSTEM_CLOCK_BANKRUPTCY_THRESHOLD_MS = 30;
/**
 * After starting a fallback driver, wait this long before attempting to re-init
 * the audio stream the first time.
 */
static const int AUDIO_INITIAL_FALLBACK_BACKOFF_STEP_MS = 10;

/**
 * The backoff step duration for when to next attempt to re-init the audio
 * stream is capped at this value.
 */
static const int AUDIO_MAX_FALLBACK_BACKOFF_STEP_MS = 1000;

class AudioCallbackDriver;
class GraphDriver;
class MediaTrack;
class OfflineClockDriver;
class SystemClockDriver;

namespace dom {
enum class AudioContextOperation : uint8_t;
}

struct GraphInterface : public nsISupports {
  /**
   * Object returned from OneIteration() instructing the iterating GraphDriver
   * what to do.
   *
   * - If the result is StillProcessing: keep the iterations coming.
   * - If the result is Stop: the driver potentially updates its internal state
   *   and interacts with the graph (e.g., NotifyOutputData), then it must call
   *   Stopped() exactly once.
   * - If the result is SwitchDriver: the driver updates internal state as for
   *   the Stop result, then it must call Switched() exactly once and start
   *   NextDriver().
   */
  class IterationResult final {
    struct Undefined {};
    struct StillProcessing {};
    struct Stop {
      explicit Stop(RefPtr<Runnable> aStoppedRunnable)
          : mStoppedRunnable(std::move(aStoppedRunnable)) {}
      Stop(const Stop&) = delete;
      Stop(Stop&& aOther) noexcept
          : mStoppedRunnable(std::move(aOther.mStoppedRunnable)) {}
      ~Stop() { MOZ_ASSERT(!mStoppedRunnable); }
      RefPtr<Runnable> mStoppedRunnable;
      void Stopped() {
        mStoppedRunnable->Run();
        mStoppedRunnable = nullptr;
      }
    };
    struct SwitchDriver {
      SwitchDriver(RefPtr<GraphDriver> aDriver,
                   RefPtr<Runnable> aSwitchedRunnable)
          : mDriver(std::move(aDriver)),
            mSwitchedRunnable(std::move(aSwitchedRunnable)) {}
      SwitchDriver(const SwitchDriver&) = delete;
      SwitchDriver(SwitchDriver&& aOther) noexcept
          : mDriver(std::move(aOther.mDriver)),
            mSwitchedRunnable(std::move(aOther.mSwitchedRunnable)) {}
      ~SwitchDriver() { MOZ_ASSERT(!mSwitchedRunnable); }
      RefPtr<GraphDriver> mDriver;
      RefPtr<Runnable> mSwitchedRunnable;
      void Switched() {
        mSwitchedRunnable->Run();
        mSwitchedRunnable = nullptr;
      }
    };
    Variant<Undefined, StillProcessing, Stop, SwitchDriver> mResult{
        Undefined()};

    explicit IterationResult(StillProcessing&& aArg)
        : mResult(std::move(aArg)) {}
    explicit IterationResult(Stop&& aArg) : mResult(std::move(aArg)) {}
    explicit IterationResult(SwitchDriver&& aArg) : mResult(std::move(aArg)) {}

   public:
    IterationResult() = default;
    IterationResult(const IterationResult&) = delete;
    IterationResult(IterationResult&&) = default;

    IterationResult& operator=(const IterationResult&) = delete;
    IterationResult& operator=(IterationResult&&) = default;

    static IterationResult CreateStillProcessing() {
      return IterationResult(StillProcessing());
    }
    static IterationResult CreateStop(RefPtr<Runnable> aStoppedRunnable) {
      return IterationResult(Stop(std::move(aStoppedRunnable)));
    }
    static IterationResult CreateSwitchDriver(
        RefPtr<GraphDriver> aDriver, RefPtr<Runnable> aSwitchedRunnable) {
      return IterationResult(
          SwitchDriver(std::move(aDriver), std::move(aSwitchedRunnable)));
    }

    bool IsStillProcessing() const { return mResult.is<StillProcessing>(); }
    bool IsStop() const { return mResult.is<Stop>(); }
    bool IsSwitchDriver() const { return mResult.is<SwitchDriver>(); }

    void Stopped() {
      MOZ_ASSERT(IsStop());
      mResult.as<Stop>().Stopped();
    }

    GraphDriver* NextDriver() const {
      if (!IsSwitchDriver()) {
        return nullptr;
      }
      return mResult.as<SwitchDriver>().mDriver;
    }

    void Switched() {
      MOZ_ASSERT(IsSwitchDriver());
      mResult.as<SwitchDriver>().Switched();
    }
  };

  /* Called on the graph thread after an AudioCallbackDriver with an input
   * stream has stopped. */
  virtual void NotifyInputStopped() = 0;
  /* Called on the graph thread when there is new input data for listeners. This
   * is the raw audio input for this MediaTrackGraph. */
  virtual void NotifyInputData(const AudioDataValue* aBuffer, size_t aFrames,
                               TrackRate aRate, uint32_t aChannels,
                               uint32_t aAlreadyBuffered) = 0;
  /* Called on the main thread after an AudioCallbackDriver has attempted an
   * operation to set the processing params matching aGeneration on the cubeb
   * stream. */
  virtual void NotifySetRequestedInputProcessingParamsResult(
      AudioCallbackDriver* aDriver, int aGeneration,
      Result<cubeb_input_processing_params, int>&& aResult) = 0;
  /* Called every time there are changes to input/output audio devices like
   * plug/unplug etc. This can be called on any thread, and posts a message to
   * the main thread so that it can post a message to the graph thread. */
  virtual void DeviceChanged() = 0;
  /* Called by GraphDriver to iterate the graph. Mixed audio output from the
   * graph is passed into aMixerReceiver, if it is non-null. */
  virtual IterationResult OneIteration(
      GraphTime aStateComputedEnd, MixerCallbackReceiver* aMixerReceiver) = 0;
#ifdef DEBUG
  /* True if we're on aDriver's thread, or if we're on mGraphRunner's thread
   * and mGraphRunner is currently run by aDriver. */
  virtual bool InDriverIteration(const GraphDriver* aDriver) const = 0;
#endif
};

/**
 * A driver is responsible for the scheduling of the processing, the thread
 * management, and give the different clocks to a MediaTrackGraph. This is an
 * abstract base class. A MediaTrackGraph can be driven by an
 * OfflineClockDriver, if the graph is offline, or a SystemClockDriver or an
 * AudioCallbackDriver, if the graph is real time.
 * A MediaTrackGraph holds an owning reference to its driver.
 *
 * The lifetime of drivers is a complicated affair. Here are the different
 * scenarii that can happen:
 *
 * Starting a MediaTrackGraph with an AudioCallbackDriver
 * - A new thread T is created, from the main thread.
 * - On this thread T, cubeb is initialized if needed, and a cubeb_stream is
 *   created and started
 * - The thread T posts a message to the main thread to terminate itself.
 * - The graph runs off the audio thread
 *
 * Starting a MediaTrackGraph with a SystemClockDriver:
 * - A new thread T is created from the main thread.
 * - The graph runs off this thread.
 *
 * Switching from a SystemClockDriver to an AudioCallbackDriver:
 * - At the end of the MTG iteration, the graph tells the current driver to
 *   switch to an AudioCallbackDriver, which is created and initialized on the
 *   graph thread.
 * - At the end of the MTG iteration, the SystemClockDriver transfers its timing
 *   info and a reference to itself to the AudioCallbackDriver. It then starts
 *   the AudioCallbackDriver.
 * - When the AudioCallbackDriver starts, it:
 *   - Starts a fallback SystemClockDriver that runs until the
 *     AudioCallbackDriver is running, in case it takes a long time to start (it
 *     could block on I/O, e.g., negotiating a bluetooth connection).
 *   - Checks if it has been switched from a SystemClockDriver, and if that is
 *     the case, sends a message to the main thread to shut the
 *     SystemClockDriver thread down.
 * - When the AudioCallbackDriver is running, data callbacks are blocked. The
 *   fallback driver detects this in its callback and stops itself. The first
 *   DataCallback after the fallback driver had stopped goes through.
 * - The graph now runs off an audio callback.
 *
 * Switching from an AudioCallbackDriver to a SystemClockDriver:
 * - At the end of the MTG iteration, the graph tells the current driver to
 *   switch to a SystemClockDriver.
 * - the AudioCallbackDriver transfers its timing info and a reference to itself
 *   to the SystemClockDriver. A new SystemClockDriver is started from the
 *   current audio thread.
 * - When starting, the SystemClockDriver checks if it has been switched from an
 *   AudioCallbackDriver. If yes, it creates a new temporary thread to release
 *   the cubeb_streams. This temporary thread closes the cubeb_stream, and then
 *   dispatches a message to the main thread to be terminated.
 * - The graph now runs off a normal thread.
 *
 * Two drivers cannot run at the same time for the same graph. The thread safety
 * of the different members of drivers, and their access pattern is documented
 * next to the members themselves.
 */
class GraphDriver {
 public:
  using IterationResult = GraphInterface::IterationResult;

  GraphDriver(GraphInterface* aGraphInterface, GraphDriver* aPreviousDriver,
              uint32_t aSampleRate);

  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  /* Start the graph, init the driver, start the thread.
   * A driver cannot be started twice, it must be shutdown
   * before being started again. */
  virtual void Start() = 0;
  /* Shutdown GraphDriver */
  MOZ_CAN_RUN_SCRIPT virtual void Shutdown() = 0;
  /* Set the UTF-8 name for system audio streams.
   * Graph thread, or main thread if the graph is not running. */
  virtual void SetStreamName(const nsACString& aStreamName);
  /* Rate at which the GraphDriver runs, in ms. This can either be user
   * controlled (because we are using a {System,Offline}ClockDriver, and decide
   * how often we want to wakeup/how much we want to process per iteration), or
   * it can be indirectly set by the latency of the audio backend, and the
   * number of buffers of this audio backend: say we have four buffers, and 40ms
   * latency, we will get a callback approximately every 10ms. */
  virtual TimeDuration IterationDuration() = 0;
  /*
   * Signaled by the graph when it needs another iteration. Goes unhandled for
   * GraphDrivers that are not able to sleep indefinitely (i.e., all drivers but
   * ThreadedDriver). Can be called on any thread.
   */
  virtual void EnsureNextIteration() = 0;

  // Those are simply for accessing the associated pointer. Graph thread only,
  // or if one is not running, main thread.
  GraphDriver* PreviousDriver();
  void SetPreviousDriver(GraphDriver* aPreviousDriver);

  virtual AudioCallbackDriver* AsAudioCallbackDriver() { return nullptr; }
  virtual const AudioCallbackDriver* AsAudioCallbackDriver() const {
    return nullptr;
  }

  virtual OfflineClockDriver* AsOfflineClockDriver() { return nullptr; }
  virtual const OfflineClockDriver* AsOfflineClockDriver() const {
    return nullptr;
  }

  virtual SystemClockDriver* AsSystemClockDriver() { return nullptr; }
  virtual const SystemClockDriver* AsSystemClockDriver() const {
    return nullptr;
  }

  /**
   * Set the state of the driver so it can start at the right point in time,
   * after switching from another driver.
   *
   * aIterationTimeStamp is the system clock time from when rendering began in
   * the most recent iteration.  An audio callback is assumed invoked soon
   * after the end of the provided portion of the platform audio buffer
   * becomes available for reading or writing, and so provides a clock edge
   * such that aCurrentTimeStamp advances consistently with the output time of
   * the end of the set of frames rendered in each iteration.  Actual audio
   * output time of the last data written would be at least a platform buffer
   * length after a write buffer portion becomes available.
   */
  void SetState(const nsACString& aStreamName, GraphTime aStateComputedTime,
                TimeStamp aIterationTimeStamp);

  GraphInterface* Graph() const { return mGraphInterface; }

#ifdef DEBUG
  // True if the current thread is currently iterating the MTG.
  bool InIteration() const;
#endif
  // True if the current thread is the GraphDriver's thread.
  virtual bool OnThread() const = 0;
  // GraphDriver's thread has started and the thread is running.
  virtual bool ThreadRunning() const = 0;

  double MediaTimeToSeconds(MediaTime aTime) const {
    NS_ASSERTION(aTime > -TRACK_TIME_MAX && aTime <= TRACK_TIME_MAX,
                 "Bad time");
    return static_cast<double>(aTime) / mSampleRate;
  }

  TimeDuration MediaTimeToTimeDuration(MediaTime aTime) const {
    return TimeDuration::FromSeconds(MediaTimeToSeconds(aTime));
  }

  GraphTime SecondsToMediaTime(double aS) const {
    NS_ASSERTION(0 <= aS && aS <= TRACK_TICKS_MAX / TRACK_RATE_MAX,
                 "Bad seconds");
    return mSampleRate * aS;
  }

  GraphTime MillisecondsToMediaTime(int32_t aMS) const {
    return RateConvertTicksRoundDown(mSampleRate, 1000, aMS);
  }

 protected:
  // The UTF-8 name for system audio streams.  Graph thread.
  nsCString mStreamName;
  // Time until which the graph has processed data.
  GraphTime mStateComputedTime = 0;
  // The system clock time when the iteration should or would start if these
  // start times advance consistently with the number of frames rendered by
  // the graph in each iteration.
  // Initially null, if no previous driver exists to provide a reference time
  // through SetState().
  // SystemClockDriver advances this before waiting to render the next
  // iteration.
  // AudioCallbackDriver sets this to approximately now at the start of each
  // iteration when !HasFallback().
  // Unused by OfflineClockDriver.
  TimeStamp mTargetIterationTimeStamp;
  // The GraphInterface this driver is currently iterating.
  const RefPtr<GraphInterface> mGraphInterface;
  // The sample rate for the graph, and in case of an audio driver, also for the
  // cubeb stream.
  const uint32_t mSampleRate;

  // This is non-null only when this driver has recently switched from an other
  // driver, and has not cleaned it up yet (for example because the audio stream
  // is currently calling the callback during initialization).
  //
  // This is written to when changing driver, from the previous driver's thread,
  // or a thread created for the occasion. This is read each time we need to
  // check whether we're changing driver (in Switching()), from the graph
  // thread.
  // This must be accessed using the {Set,Get}PreviousDriver methods.
  RefPtr<GraphDriver> mPreviousDriver;

  virtual ~GraphDriver() = default;
};

class MediaTrackGraphInitThreadRunnable;

/**
 * This class is a driver that manages its own thread.
 */
class ThreadedDriver : public GraphDriver {
  class IterationWaitHelper {
    Monitor mMonitor MOZ_UNANNOTATED{"IterationWaitHelper::mMonitor"};
    // The below members are guarded by mMonitor.

    // Whether another iteration is required either to process control
    // messages or to render.
    // Drivers do not pass on this state when switching to another driver,
    // so always perform at least one iteration.
    bool mNeedAnotherIteration = true;
    TimeStamp mWakeTime;

   public:
    IterationWaitHelper() = default;

    /**
     * If another iteration is needed we wait for aDuration, otherwise we wait
     * for a wake-up. If a wake-up occurs before aDuration time has passed, we
     * wait for aDuration nonetheless.
     */
    void WaitForNextIterationAtLeast(TimeDuration aDuration) {
      MonitorAutoLock lock(mMonitor);
      TimeStamp now = TimeStamp::Now();
      mWakeTime = now + aDuration;
      while (true) {
        if (mNeedAnotherIteration && now >= mWakeTime) {
          break;
        }
        if (mNeedAnotherIteration) {
          lock.Wait(mWakeTime - now);
        } else {
          lock.Wait(TimeDuration::Forever());
        }
        now = TimeStamp::Now();
      }
      mWakeTime = TimeStamp();
      mNeedAnotherIteration = false;
    }

    /**
     * Sets mNeedAnotherIteration to true and notifies the monitor, in case a
     * driver is currently waiting.
     */
    void EnsureNextIteration() {
      MonitorAutoLock lock(mMonitor);
      mNeedAnotherIteration = true;
      lock.Notify();
    }
  };

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ThreadedDriver, override);

  ThreadedDriver(GraphInterface* aGraphInterface, GraphDriver* aPreviousDriver,
                 uint32_t aSampleRate);

  void EnsureNextIteration() override;
  MOZ_CAN_RUN_SCRIPT void Shutdown() override;
  /**
   * Runs main control loop on the graph thread. Normally a single invocation
   * of this runs for the entire lifetime of the graph thread.
   */
  virtual void RunThread();
  friend class MediaTrackGraphInitThreadRunnable;
  TimeDuration IterationDuration() override;

  nsIThread* Thread() const { return mThread; }

  bool OnThread() const override {
    return !mThread || mThread->IsOnCurrentThread();
  }

  bool ThreadRunning() const override { return mThreadRunning; }

 protected:
  void StartThread(uint32_t aStackSize);
  /* Waits until it's time to process more data. */
  void WaitForNextIteration();
  /* Return the implementation-dependent time that the ThreadedDriver should
   * wait for the next iteration.  Called only once per iteration;
   * SystemClockDriver advances it's target iteration time stamp.*/
  virtual TimeDuration NextIterationWaitDuration() = 0;
  /* When the graph wakes up to do an iteration, implementations return the
   * range of time that will be processed. */
  virtual MediaTime GetIntervalForIteration() = 0;

  virtual ~ThreadedDriver();

  nsCOMPtr<nsIThread> mThread;

 private:
  // This is true if the thread is running. It is false
  // before starting the thread and after stopping it.
  Atomic<bool> mThreadRunning;

  // Any thread.
  IterationWaitHelper mWaitHelper;
};

/**
 * A SystemClockDriver drives a GraphInterface using a system clock, and waits
 * using a monitor, between each iteration.
 */
class SystemClockDriver final : public ThreadedDriver {
 public:
  SystemClockDriver(GraphInterface* aGraphInterface,
                    GraphDriver* aPreviousDriver, uint32_t aSampleRate);
  virtual ~SystemClockDriver();
  void Start() override;
  void RunThread() override;
  SystemClockDriver* AsSystemClockDriver() override { return this; }
  const SystemClockDriver* AsSystemClockDriver() const override { return this; }
  const TimeStamp& IterationTimeStamp() const {
    return mTargetIterationTimeStamp;
  }

 protected:
  /* Return the TimeDuration to wait before the next rendering iteration. */
  TimeDuration NextIterationWaitDuration() override;
  MediaTime GetIntervalForIteration() override;

 private:
  // Those are only modified (after initialization) on the graph thread. The
  // graph thread does not run during the initialization.
  TimeStamp mInitialTimeStamp;
};

/**
 * An OfflineClockDriver runs the graph as fast as possible, without waiting
 * between iteration.
 */
class OfflineClockDriver final : public ThreadedDriver {
 public:
  OfflineClockDriver(GraphInterface* aGraphInterface, uint32_t aSampleRate);
  virtual ~OfflineClockDriver();
  void Start() override;
  OfflineClockDriver* AsOfflineClockDriver() override { return this; }
  const OfflineClockDriver* AsOfflineClockDriver() const override {
    return this;
  }

  void RunThread() override;

  void SetTickCountToRender(uint32_t aTicksToProcess) {
    MOZ_ASSERT(InIteration());
    MOZ_ASSERT(mEndTime == 0);
    mEndTime = aTicksToProcess;
  }

 protected:
  TimeDuration NextIterationWaitDuration() override { return TimeDuration(); }
  MediaTime GetIntervalForIteration() override;

 private:
  // The graph will advance up to this time.  Graph thread.
  GraphTime mEndTime = 0;
};

enum class AudioInputType { Unknown, Voice };

struct AudioInputProcessingParamsRequest {
  int mGeneration{};
  cubeb_input_processing_params mParams{};
};

/**
 * This is a graph driver that is based on callback functions called by the
 * audio api. This ensures minimal audio latency, because it means there is no
 * buffering happening: the audio is generated inside the callback.
 *
 * This design is less flexible than running our own thread:
 * - We have no control over the thread:
 * - It cannot block, and it has to run for a shorter amount of time than the
 *   buffer it is going to fill, or an under-run is going to occur (short burst
 *   of silence in the final audio output).
 * - We can't know for sure when the callback function is going to be called
 *   (although we compute an estimation so we can schedule video frames)
 * - Creating and shutting the thread down is a blocking operation, that can
 *   take _seconds_ in some cases (because IPC has to be set up, and
 *   sometimes hardware components are involved and need to be warmed up)
 * - We have no control on how much audio we generate, we have to return exactly
 *   the number of frames asked for by the callback. Since for the Web Audio
 *   API, we have to do block processing at 128 frames per block, we need to
 *   keep a little spill buffer to store the extra frames.
 */
class AudioCallbackDriver final : public GraphDriver,
                                  public MixerCallbackReceiver {
  using IterationResult = GraphInterface::IterationResult;
  enum class FallbackDriverState;
  class FallbackWrapper;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DELETE_ON_EVENT_TARGET(
      AudioCallbackDriver, mCubebOperationThread, override);

  /** If aInputChannelCount is zero, then this driver is output-only. */
  AudioCallbackDriver(
      GraphInterface* aGraphInterface, GraphDriver* aPreviousDriver,
      uint32_t aSampleRate, uint32_t aOutputChannelCount,
      uint32_t aInputChannelCount, CubebUtils::AudioDeviceID aOutputDeviceID,
      CubebUtils::AudioDeviceID aInputDeviceID, AudioInputType aAudioInputType,
      Maybe<AudioInputProcessingParamsRequest> aRequestedInputProcessingParams);

  void Start() override;
  MOZ_CAN_RUN_SCRIPT void Shutdown() override;
  void SetStreamName(const nsACString& aStreamName) override;

  /* Static wrapper function cubeb calls back. */
  static long DataCallback_s(cubeb_stream* aStream, void* aUser,
                             const void* aInputBuffer, void* aOutputBuffer,
                             long aFrames);
  static void StateCallback_s(cubeb_stream* aStream, void* aUser,
                              cubeb_state aState);
  static void DeviceChangedCallback_s(void* aUser);

  /* This function is called by the underlying audio backend when a refill is
   * needed. This is what drives the whole graph when it is used to output
   * audio. If the return value is exactly aFrames, this function will get
   * called again. If it is less than aFrames, the stream will go in draining
   * mode, and this function will not be called again. */
  long DataCallback(const AudioDataValue* aInputBuffer,
                    AudioDataValue* aOutputBuffer, long aFrames);
  /* This function is called by the underlying audio backend, but is only used
   * for informational purposes at the moment. */
  void StateCallback(cubeb_state aState);
  /* This is an approximation of the number of millisecond there are between two
   * iterations of the graph. */
  TimeDuration IterationDuration() override;
  /* If the audio stream has started, this does nothing. There will be another
   * iteration. If there is an active fallback driver, we forward the call so it
   * can wake up. */
  void EnsureNextIteration() override;

  /* This function gets called when the graph has produced the audio frames for
   * this iteration. */
  void MixerCallback(AudioChunk* aMixedBuffer, uint32_t aSampleRate) override;

  AudioCallbackDriver* AsAudioCallbackDriver() override { return this; }
  const AudioCallbackDriver* AsAudioCallbackDriver() const override {
    return this;
  }

  uint32_t OutputChannelCount() const { return mOutputChannelCount; }

  uint32_t InputChannelCount() const { return mInputChannelCount; }

  CubebUtils::AudioDeviceID InputDeviceID() const { return mInputDeviceID; }

  AudioInputType InputDevicePreference() const {
    if (mInputDevicePreference == CUBEB_DEVICE_PREF_VOICE) {
      return AudioInputType::Voice;
    }
    return AudioInputType::Unknown;
  }

  /* Get the latest input processing params request from this driver, so
   * that an external caller can decide whether it is necessary to call the
   * setter, since it may allocate or dispatch. */
  const AudioInputProcessingParamsRequest& RequestedInputProcessingParams()
      const;

  /* Set the input processing params requested from this driver. */
  void RequestInputProcessingParams(AudioInputProcessingParamsRequest);

  std::thread::id ThreadId() const { return mAudioThreadIdInCb.load(); }

  /* Called at the beginning of the audio callback to check if the thread id has
   * changed. */
  bool CheckThreadIdChanged();

  bool OnThread() const override {
    return mAudioThreadIdInCb.load() == std::this_thread::get_id();
  }

  /* Returns true if this driver has started (perhaps with a fallback driver)
   * and not yet stopped. */
  bool ThreadRunning() const override {
    return mAudioStreamState == AudioStreamState::Running ||
           mFallbackDriverState == FallbackDriverState::Running;
  }

  /* Whether the underlying cubeb stream has been started and has not stopped
   * or errored. */
  bool IsStarted() { return mAudioStreamState > AudioStreamState::Starting; };

  // Returns the output latency for the current audio output stream.
  TimeDuration AudioOutputLatency();

  /* Returns true if this driver has a fallback driver and handover to the audio
   * callback has not been completed. */
  bool HasFallback() const;
  /* Returns true if this driver is currently driven by the fallback driver. */
  bool OnFallback() const;

 private:
  /**
   * On certain MacBookPro, the microphone is located near the left speaker.
   * We need to pan the sound output to the right speaker if we are using the
   * mic and the built-in speaker, or we will have terrible echo.  */
  void PanOutputIfNeeded(bool aMicrophoneActive);
  /**
   * This is called when the output device used by the cubeb stream changes. */
  void DeviceChangedCallback();
  /* Start the cubeb stream */
  bool StartStream();
  friend class MediaTrackGraphInitThreadRunnable;
  void QueueInitOp();
  void Init(const nsCString& aStreamName);
  void SetCubebStreamName(const nsCString& aStreamName);
  void Stop();
  /* After the requested input processing params has changed, this applies them
   * on the cubeb stream. */
  void SetInputProcessingParams(AudioInputProcessingParamsRequest aRequest);
  /* If in FallbackDriverState::None, create the mFallback driver and start its
   * SystemClockDriver.
   * Returns Ok(true) if the fallback driver was started, or the old
   * FallbackDriverState in an Err otherwise. */
  Result<bool, FallbackDriverState> TryStartingFallbackDriver();
  /* Create the mFallback driver with a SystemClockDriver to use a normal
   * thread.  Start() should be called on the returned FallbackWrapper, when
   * ready to hand over control of the graph.  If needed, the fallback graph
   * wrapper will try to re-open an audio stream later. */
  [[nodiscard]] RefPtr<FallbackWrapper> CreateFallbackSystemClockDriver();
  void FallbackToSystemClockDriver();
  /* Called by the fallback driver when it has fully stopped, after finishing
   * its last iteration. If it stopped after the audio stream started, aState
   * will be None. If it stopped after the graph told it to stop, or switch,
   * aState will be Stopped. Hands over state to the audio driver that may
   * iterate the graph after this has been called. */
  void FallbackDriverStopped(GraphTime aStateComputedTime,
                             TimeStamp aIterationTimeStamp,
                             FallbackDriverState aState);

  /* Called at the end of the fallback driver's iteration to see whether we
   * should attempt to start the AudioStream again. */
  void MaybeStartAudioStream();

  /* This is true when the method is executed on CubebOperation thread pool. */
  bool OnCubebOperationThread() {
    return mCubebOperationThread->IsOnCurrentThreadInfallible();
  }

  /* MediaTrackGraphs are always down/up mixed to output channels. */
  const uint32_t mOutputChannelCount;
  /* The size of this buffer comes from the fact that some audio backends can
   * call back with a number of frames lower than one block (128 frames), so we
   * need to keep at most two block in the SpillBuffer, because we always round
   * up to block boundaries during an iteration.
   * Initialized before starting the cubeb stream and then accessed from
   * cubeb callbacks. */
  SpillBuffer<AudioDataValue, WEBAUDIO_BLOCK_SIZE * 2> mScratchBuffer;
  /* Wrapper to ensure we write exactly the number of frames we need in the
   * audio buffer cubeb passes us. This is only ever accessed on the audio
   * callback thread. */
  AudioCallbackBufferWrapper<AudioDataValue> mBuffer;
  // mAudioStream (a cubeb_stream) has a bare pointer to the cubeb context, so
  // we hold a strong reference on its behalf.
  RefPtr<CubebUtils::CubebHandle> mCubeb;
  /* cubeb stream for this graph. This is non-null after a successful
   * cubeb_stream_init(). Written and read on the CubebOperation thread only,
   * except for AudioOutputLatency(), which reads it from the main thread.
   * Behind a lock because of that cross-thread read; never touched by the
   * real-time audio callback thread. */
  DataMutex<nsAutoRef<cubeb_stream>> mAudioStream{
      "AudioCallbackDriver::mAudioStream"};
  /* The number of input channels from cubeb. Set before opening cubeb. If it is
   * zero then the driver is output-only. */
  const uint32_t mInputChannelCount;
  /**
   * Devices to use for cubeb input & output, or nullptr for default device.
   */
  const CubebUtils::AudioDeviceID mOutputDeviceID;
  const CubebUtils::AudioDeviceID mInputDeviceID;
  /* Whether the current or a future audio callback will be the first callback
   * to iterate the graph.  Used only from DataCallback().
   * Initialized on transition from AudioStreamState::Starting to Running. */
  MOZ_INIT_OUTSIDE_CTOR bool mFirstCallbackIteration;
  /* Approximation of the time between two callbacks. This is used to schedule
   * video frames. This is in milliseconds. Only even used (after
   * inizatialization) on the audio callback thread. */
  uint32_t mIterationDurationMS;

  struct AutoInCallback {
    explicit AutoInCallback(AudioCallbackDriver* aDriver);
    ~AutoInCallback();
    AudioCallbackDriver* mDriver;
  };

  static already_AddRefed<TaskQueue> CreateTaskQueue();

  /* Shared thread pool with up to one thread for off-main-thread
   * initialization and shutdown of the audio stream and for other tasks that
   * must run serially for access to mAudioStream. */
  const RefPtr<TaskQueue> mCubebOperationThread;
  cubeb_device_pref mInputDevicePreference = CUBEB_DEVICE_PREF_NONE;
  /* Params that have been attempted to set on mAudioStream, after filtering by
   * supported processing params. Cubeb operation thread only. */
  cubeb_input_processing_params mConfiguredInputProcessingParams =
      CUBEB_INPUT_PROCESSING_PARAM_NONE;
  /* The input processing params and generation requested from this audio
   * driver. Once started, audio callback thread only. */
  AudioInputProcessingParamsRequest mInputProcessingRequest;
  /* Contains the id of the audio thread, from profiler_current_thread_id. */
  std::atomic<ProfilerThreadId> mAudioThreadId;
  /* This allows implementing AutoInCallback. This is equal to the current
   * thread id when in an audio callback, and is an invalid thread id otherwise.
   */
  std::atomic<std::thread::id> mAudioThreadIdInCb;
  /* State of the audio stream, see inline comments. */
  enum class AudioStreamState {
    /* There is no cubeb_stream or mAudioStream is in CUBEB_STATE_ERROR or
     * CUBEB_STATE_STOPPED and no pending task exists to Init() a new
     * cubeb_stream. */
    None,
    /* A task to Init() a new cubeb_stream is pending. */
    Pending,
    /* cubeb_start_stream() is about to be or has been called on mAudioStream.
     * Any previous cubeb_streams have been destroyed. */
    Starting,
    /* mAudioStream has advertised it will change device. In this state we
       ignore all data callbacks until the fallback driver has started. */
    ChangingDevice,
    /* mAudioStream is running. */
    Running,
    /* mAudioStream is draining, and will soon stop. */
    Stopping
  };
  Atomic<AudioStreamState> mAudioStreamState{AudioStreamState::None};
  /* State of the fallback driver, see inline comments. */
  enum class FallbackDriverState {
    /* There is no fallback driver. */
    None,
    /* There is a fallback driver trying to iterate us. */
    Running,
    /* There was a fallback driver and the graph stopped it. No audio callback
       may iterate the graph. */
    Stopped,
  };
  Atomic<FallbackDriverState> mFallbackDriverState{FallbackDriverState::None};
  /* SystemClockDriver used as fallback if this AudioCallbackDriver fails to
   * init or start. */
  DataMutex<RefPtr<FallbackWrapper>> mFallback;
  /* If using a fallback driver, this is the duration to wait after failing to
   * start it before attempting to start it again. */
  TimeDuration mNextReInitBackoffStep;
  /* If using a fallback driver, this is the next time we'll try to start the
   * audio stream. */
  TimeStamp mNextReInitAttempt;
  /* The time mAudioStreamState was changed to ChangingDevice.
   * Synchronized by the mAudioStreamState atomic, i.e. written *before* writing
   * the atomic, and read *after* reading the atomic. */
  TimeStamp mChangingDeviceStartTime;
#ifdef XP_MACOSX
  /* When using the built-in speakers on macbook pro (13 and 15, all models),
   * it's best to hard pan the audio on the right, to avoid feedback into the
   * microphone that is located next to the left speaker.  */
  Atomic<bool> mNeedsPanning;
#endif

  WavDumper mInputStreamFile;
  WavDumper mOutputStreamFile;

  virtual ~AudioCallbackDriver();
  const bool mSandboxed = false;
};

}  // namespace mozilla

#endif  // GRAPHDRIVER_H_
