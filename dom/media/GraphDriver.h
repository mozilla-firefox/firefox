/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GRAPHDRIVER_H_
#define GRAPHDRIVER_H_

#include "nsAutoRef.h"
#include "nsIThread.h"
#include "AudioBufferUtils.h"
#include "AudioMixer.h"
#include "AudioSegment.h"
#include "SelfRef.h"
#include "mozilla/Atomics.h"
#include "mozilla/dom/AudioContext.h"
#include "mozilla/DataMutex.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/StaticPtr.h"
#include "WavDumper.h"

#include <thread>

struct cubeb_stream;

template <>
class nsAutoRefTraits<cubeb_stream> : public nsPointerRefTraits<cubeb_stream> {
 public:
  static void Release(cubeb_stream* aStream) { cubeb_stream_destroy(aStream); }
};

namespace mozilla {

// A thread pool containing only one thread to execute the cubeb operations. We
// should always use this thread to init, destroy, start, or stop cubeb streams,
// to avoid data racing or deadlock issues across platforms.
#define CUBEB_TASK_THREAD SharedThreadPool::Get("CubebOperation"_ns, 1)

/**
 * Assume we can run an iteration of the MediaTrackGraph loop in this much time
 * or less.
 * We try to run the control loop at this rate.
 */
static const int MEDIA_GRAPH_TARGET_PERIOD_MS = 10;

/**
 * Assume that we might miss our scheduled wakeup of the MediaTrackGraph by
 * this much.
 */
static const int SCHEDULE_SAFETY_MARGIN_MS = 10;

/**
 * Try have this much audio buffered in streams and queued to the hardware.
 * The maximum delay to the end of the next control loop
 * is 2*MEDIA_GRAPH_TARGET_PERIOD_MS + SCHEDULE_SAFETY_MARGIN_MS.
 * There is no point in buffering more audio than this in a stream at any
 * given time (until we add processing).
 * This is not optimal yet.
 */
static const int AUDIO_TARGET_MS =
    2 * MEDIA_GRAPH_TARGET_PERIOD_MS + SCHEDULE_SAFETY_MARGIN_MS;

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
    Variant<Undefined, StillProcessing, Stop, SwitchDriver> mResult;

    explicit IterationResult(StillProcessing&& aArg)
        : mResult(std::move(aArg)) {}
    explicit IterationResult(Stop&& aArg) : mResult(std::move(aArg)) {}
    explicit IterationResult(SwitchDriver&& aArg) : mResult(std::move(aArg)) {}

   public:
    IterationResult() : mResult(Undefined()) {}
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

  /* Called on the graph thread when there is new output data for listeners.
   * This is the mixed audio output of this MediaTrackGraph. */
  virtual void NotifyOutputData(AudioDataValue* aBuffer, size_t aFrames,
                                TrackRate aRate, uint32_t aChannels) = 0;
  /* Called on the graph thread after an AudioCallbackDriver with an input
   * stream has stopped. */
  virtual void NotifyInputStopped() = 0;
  /* Called on the graph thread when there is new input data for listeners. This
   * is the raw audio input for this MediaTrackGraph. */
  virtual void NotifyInputData(const AudioDataValue* aBuffer, size_t aFrames,
                               TrackRate aRate, uint32_t aChannels,
                               uint32_t aAlreadyBuffered) = 0;
  /* Called every time there are changes to input/output audio devices like
   * plug/unplug etc. This can be called on any thread, and posts a message to
   * the main thread so that it can post a message to the graph thread. */
  virtual void DeviceChanged() = 0;
  /* Called by GraphDriver to iterate the graph. Output from the graph gets
   * mixed into aMixer, if it is non-null. */
  virtual IterationResult OneIteration(GraphTime aStateComputedEnd,
                                       GraphTime aIterationEnd,
                                       AudioMixer* aMixer) = 0;
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
  /* Rate at which the GraphDriver runs, in ms. This can either be user
   * controlled (because we are using a {System,Offline}ClockDriver, and decide
   * how often we want to wakeup/how much we want to process per iteration), or
   * it can be indirectly set by the latency of the audio backend, and the
   * number of buffers of this audio backend: say we have four buffers, and 40ms
   * latency, we will get a callback approximately every 10ms. */
  virtual uint32_t IterationDuration() = 0;
  /*
   * Signaled by the graph when it needs another iteration. Goes unhandled for
   * GraphDrivers that are not able to sleep indefinitely (i.e., all drivers but
   * ThreadedDriver). Can be called on any thread.
   */
  virtual void EnsureNextIteration() = 0;

  /* Implement the switching of the driver and the necessary updates */
  void SwitchToDriver(GraphDriver* aDriver);

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
   */
  void SetState(GraphTime aIterationStart, GraphTime aIterationEnd,
                GraphTime aStateComputedTime);

  GraphInterface* Graph() const { return mGraphInterface; }

#ifdef DEBUG
  // True if the current thread is currently iterating the MTG.
  bool InIteration() const;
#endif
  // True if the current thread is the GraphDriver's thread.
  virtual bool OnThread() const = 0;
  // GraphDriver's thread has started and the thread is running.
  virtual bool ThreadRunning() const = 0;

  double MediaTimeToSeconds(GraphTime aTime) const {
    NS_ASSERTION(aTime > -TRACK_TIME_MAX && aTime <= TRACK_TIME_MAX,
                 "Bad time");
    return static_cast<double>(aTime) / mSampleRate;
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
  // Time of the start of this graph iteration.
  GraphTime mIterationStart = 0;
  // Time of the end of this graph iteration.
  GraphTime mIterationEnd = 0;
  // Time until which the graph has processed data.
  GraphTime mStateComputedTime = 0;
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
    Monitor mMonitor MOZ_UNANNOTATED;
    // The below members are guarded by mMonitor.
    bool mNeedAnotherIteration = false;
    TimeStamp mWakeTime;

   public:
    IterationWaitHelper() : mMonitor("IterationWaitHelper::mMonitor") {}

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
  uint32_t IterationDuration() override { return MEDIA_GRAPH_TARGET_PERIOD_MS; }

  nsIThread* Thread() const { return mThread; }

  bool OnThread() const override {
    return !mThread || mThread->IsOnCurrentThread();
  }

  bool ThreadRunning() const override { return mThreadRunning; }

 protected:
  void StartThread(uint32_t aStackSize);
  /* Waits until it's time to process more data. */
  void WaitForNextIteration();
  /* Implementation dependent time the ThreadedDriver should wait between
   * iterations. */
  virtual TimeDuration WaitInterval() = 0;
  /* When the graph wakes up to do an iteration, implementations return the
   * range of time that will be processed.  This is called only once per
   * iteration; it may determine the interval from state in a previous
   * call. */
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
class SystemClockDriver : public ThreadedDriver {
 public:
  SystemClockDriver(GraphInterface* aGraphInterface,
                    GraphDriver* aPreviousDriver, uint32_t aSampleRate);
  virtual ~SystemClockDriver();
  void Start() override;
  SystemClockDriver* AsSystemClockDriver() override { return this; }
  const SystemClockDriver* AsSystemClockDriver() const override { return this; }

 protected:
  /* Return the TimeDuration to wait before the next rendering iteration. */
  TimeDuration WaitInterval() override;
  MediaTime GetIntervalForIteration() override;

 private:
  // Those are only modified (after initialization) on the graph thread. The
  // graph thread does not run during the initialization.
  TimeStamp mInitialTimeStamp;
  TimeStamp mCurrentTimeStamp;
  TimeStamp mLastTimeStamp;
};

/**
 * An OfflineClockDriver runs the graph as fast as possible, without waiting
 * between iteration.
 */
class OfflineClockDriver : public ThreadedDriver {
 public:
  OfflineClockDriver(GraphInterface* aGraphInterface, uint32_t aSampleRate,
                     GraphTime aSlice);
  virtual ~OfflineClockDriver();
  void Start() override;
  OfflineClockDriver* AsOfflineClockDriver() override { return this; }
  const OfflineClockDriver* AsOfflineClockDriver() const override {
    return this;
  }

  void RunThread() override;

 protected:
  TimeDuration WaitInterval() override { return TimeDuration(); }
  MediaTime GetIntervalForIteration() override;

 private:
  // Time, in GraphTime, for each iteration
  GraphTime mSlice;
};

struct TrackAndPromiseForOperation {
  TrackAndPromiseForOperation(
      MediaTrack* aTrack, dom::AudioContextOperation aOperation,
      AbstractThread* aMainThread,
      MozPromiseHolder<MediaTrackGraph::AudioContextOperationPromise>&&
          aHolder);
  TrackAndPromiseForOperation(TrackAndPromiseForOperation&& aOther) noexcept;
  RefPtr<MediaTrack> mTrack;
  dom::AudioContextOperation mOperation;
  RefPtr<AbstractThread> mMainThread;
  MozPromiseHolder<MediaTrackGraph::AudioContextOperationPromise> mHolder;
};

enum class AsyncCubebOperation { INIT, SHUTDOWN };
enum class AudioInputType { Unknown, Voice };

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
class AudioCallbackDriver : public GraphDriver, public MixerCallbackReceiver {
  using IterationResult = GraphInterface::IterationResult;
  enum class FallbackDriverState;
  class FallbackWrapper;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AudioCallbackDriver, override);

  /** If aInputChannelCount is zero, then this driver is output-only. */
  AudioCallbackDriver(GraphInterface* aGraphInterface,
                      GraphDriver* aPreviousDriver, uint32_t aSampleRate,
                      uint32_t aOutputChannelCount, uint32_t aInputChannelCount,
                      CubebUtils::AudioDeviceID aOutputDeviceID,
                      CubebUtils::AudioDeviceID aInputDeviceID,
                      AudioInputType aAudioInputType);

  void Start() override;
  MOZ_CAN_RUN_SCRIPT void Shutdown() override;

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
  uint32_t IterationDuration() override;
  /* If the audio stream has started, this does nothing. There will be another
   * iteration. If there is an active fallback driver, we forward the call so it
   * can wake up. */
  void EnsureNextIteration() override;

  /* This function gets called when the graph has produced the audio frames for
   * this iteration. */
  void MixerCallback(AudioDataValue* aMixedBuffer, AudioSampleFormat aFormat,
                     uint32_t aChannels, uint32_t aFrames,
                     uint32_t aSampleRate) override;

  AudioCallbackDriver* AsAudioCallbackDriver() override { return this; }
  const AudioCallbackDriver* AsAudioCallbackDriver() const override {
    return this;
  }

  uint32_t OutputChannelCount() { return mOutputChannelCount; }

  uint32_t InputChannelCount() { return mInputChannelCount; }

  AudioInputType InputDevicePreference() {
    if (mInputDevicePreference == CUBEB_DEVICE_PREF_VOICE) {
      return AudioInputType::Voice;
    }
    return AudioInputType::Unknown;
  }

  std::thread::id ThreadId() const { return mAudioThreadIdInCb.load(); }

  /* Called when the thread servicing the callback has changed. This can be
   * fairly expensive */
  void OnThreadIdChanged();
  /* Called at the beginning of the audio callback to check if the thread id has
   * changed. */
  bool CheckThreadIdChanged();

  bool OnThread() const override {
    return mAudioThreadIdInCb.load() == std::this_thread::get_id();
  }

  /* Returns true if this audio callback driver has successfully started and not
   * yet stopped. If the fallback driver is active, this returns false. */
  bool ThreadRunning() const override {
    return mAudioStreamState == AudioStreamState::Running;
  }

  /* Whether the underlying cubeb stream has been started and has not stopped
   * or errored. */
  bool IsStarted() { return mAudioStreamState > AudioStreamState::Starting; };

  // Returns the output latency for the current audio output stream.
  TimeDuration AudioOutputLatency();

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
  friend class AsyncCubebTask;
  void Init();
  void Stop();
  /**
   *  Fall back to a SystemClockDriver using a normal thread. If needed,
   *  the graph will try to re-open an audio stream later. */
  void FallbackToSystemClockDriver();
  /* Called by the fallback driver when it has fully stopped, after finishing
   * its last iteration. If it stopped after the audio stream started, aState
   * will be None. If it stopped after the graph told it to stop, or switch,
   * aState will be Stopped. Hands over state to the audio driver that may
   * iterate the graph after this has been called. */
  void FallbackDriverStopped(GraphTime aIterationStart, GraphTime aIterationEnd,
                             GraphTime aStateComputedTime,
                             FallbackDriverState aState);

  /* Called at the end of the fallback driver's iteration to see whether we
   * should attempt to start the AudioStream again. */
  void MaybeStartAudioStream();

  /* This is true when the method is executed on CubebOperation thread pool. */
  bool OnCubebOperationThread() {
    return mInitShutdownThread->IsOnCurrentThreadInfallible();
  }

  /* MediaTrackGraphs are always down/up mixed to output channels. */
  const uint32_t mOutputChannelCount;
  /* The size of this buffer comes from the fact that some audio backends can
   * call back with a number of frames lower than one block (128 frames), so we
   * need to keep at most two block in the SpillBuffer, because we always round
   * up to block boundaries during an iteration.
   * This is only ever accessed on the audio callback thread. */
  SpillBuffer<AudioDataValue, WEBAUDIO_BLOCK_SIZE * 2> mScratchBuffer;
  /* Wrapper to ensure we write exactly the number of frames we need in the
   * audio buffer cubeb passes us. This is only ever accessed on the audio
   * callback thread. */
  AudioCallbackBufferWrapper<AudioDataValue> mBuffer;
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
  /* Approximation of the time between two callbacks. This is used to schedule
   * video frames. This is in milliseconds. Only even used (after
   * inizatialization) on the audio callback thread. */
  uint32_t mIterationDurationMS;

  struct AutoInCallback {
    explicit AutoInCallback(AudioCallbackDriver* aDriver);
    ~AutoInCallback();
    AudioCallbackDriver* mDriver;
  };

  /* Shared thread pool with up to one thread for off-main-thread
   * initialization and shutdown of the audio stream via AsyncCubebTask. */
  const RefPtr<SharedThreadPool> mInitShutdownThread;
  cubeb_device_pref mInputDevicePreference;
  /* The mixer that the graph mixes into during an iteration. Audio thread only.
   */
  AudioMixer mMixer;
  /* Contains the id of the audio thread, from profiler_current_thread_id. */
  std::atomic<ProfilerThreadId> mAudioThreadId;
  /* This allows implementing AutoInCallback. This is equal to the current
   * thread id when in an audio callback, and is an invalid thread id otherwise.
   */
  std::atomic<std::thread::id> mAudioThreadIdInCb;
  /* State of the audio stream, see inline comments. */
  enum class AudioStreamState {
    /* There is no cubeb_stream or mAudioStream is in CUBEB_STATE_ERROR or
     * CUBEB_STATE_STOPPED and no pending AsyncCubebTask exists to INIT a new
     * cubeb_stream. */
    None,
    /* An AsyncCubebTask to INIT a new cubeb_stream is pending. */
    Pending,
    /* cubeb_start_stream() is about to be or has been called on mAudioStream.
     * Any previous cubeb_streams have been destroyed. */
    Starting,
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

class AsyncCubebTask : public Runnable {
 public:
  AsyncCubebTask(AudioCallbackDriver* aDriver, AsyncCubebOperation aOperation);

  nsresult Dispatch(uint32_t aFlags = NS_DISPATCH_NORMAL) {
    return mDriver->mInitShutdownThread->Dispatch(this, aFlags);
  }

  nsresult DispatchAndSpinEventLoopUntilComplete(
      const nsACString& aVeryGoodReasonToDoThis) {
    return NS_DispatchAndSpinEventLoopUntilComplete(
        aVeryGoodReasonToDoThis, mDriver->mInitShutdownThread, do_AddRef(this));
  }

 protected:
  virtual ~AsyncCubebTask();

 private:
  NS_IMETHOD Run() final;

  RefPtr<AudioCallbackDriver> mDriver;
  AsyncCubebOperation mOperation;
  RefPtr<GraphInterface> mShutdownGrip;
};

}  // namespace mozilla

#endif  // GRAPHDRIVER_H_
