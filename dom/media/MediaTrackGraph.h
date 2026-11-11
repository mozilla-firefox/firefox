/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_MEDIATRACKGRAPH_H_
#define MOZILLA_MEDIATRACKGRAPH_H_

#include <speex/speex_resampler.h>

#include "AudioSampleFormat.h"
#include "CubebUtils.h"
#include "MainThreadUtils.h"
#include "MediaSegment.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/StateMirroring.h"
#include "mozilla/StateWatching.h"
#include "mozilla/TaskQueue.h"
#include "nsAutoRef.h"
#include "nsIRunnable.h"
#include "nsTArray.h"

class nsIRunnable;
class nsIGlobalObject;
class nsPIDOMWindowInner;

namespace mozilla {
class AsyncLogger;
class AudioCaptureTrack;
class CrossGraphTransmitter;
class CrossGraphReceiver;
class NativeInputTrack;
};  // namespace mozilla

extern mozilla::AsyncLogger gMTGTraceLogger;

template <>
class nsAutoRefTraits<SpeexResamplerState>
    : public nsPointerRefTraits<SpeexResamplerState> {
 public:
  static void Release(SpeexResamplerState* aState) {
    speex_resampler_destroy(aState);
  }
};

namespace mozilla {

extern LazyLogModule gMediaTrackGraphLog;

namespace dom {
enum class AudioContextOperation : uint8_t;
enum class AudioContextOperationFlags;
enum class AudioContextState : uint8_t;
}  // namespace dom

/*
 * MediaTrackGraph is a framework for synchronized audio/video processing
 * and playback. It is designed to be used by other browser components such as
 * HTML media elements, media capture APIs, real-time media streaming APIs,
 * multitrack media APIs, and advanced audio APIs.
 *
 * The MediaTrackGraph uses a dedicated thread to process media --- the media
 * graph thread. This ensures that we can process media through the graph
 * without blocking on main-thread activity. The media graph is only modified
 * on the media graph thread, to ensure graph changes can be processed without
 * interfering with media processing. All interaction with the media graph
 * thread is done with message passing.
 *
 * APIs that modify the graph or its properties are described as "control APIs".
 * These APIs are asynchronous; they queue graph changes internally and
 * those changes are processed all-at-once by the MediaTrackGraph. The
 * MediaTrackGraph monitors the main thread event loop via
 * nsIAppShell::RunInStableState to ensure that graph changes from a single
 * event loop task are always processed all together. Control APIs should only
 * be used on the main thread, currently; we may be able to relax that later.
 *
 * To allow precise synchronization of times in the control API, the
 * MediaTrackGraph maintains a "media timeline". Control APIs that take or
 * return times use that timeline. Those times never advance during
 * an event loop task. This time is returned by
 * MediaTrackGraph::GetCurrentTime().
 *
 * Media decoding, audio processing and media playback use thread-safe APIs to
 * the media graph to ensure they can continue while the main thread is blocked.
 *
 * When the graph is changed, we may need to throw out buffered data and
 * reprocess it. This is triggered automatically by the MediaTrackGraph.
 */

class AudioProcessingTrack;
class AudioNodeEngine;
class AudioNodeExternalInputTrack;
class AudioNodeTrack;
class DirectMediaTrackListener;
class ForwardedInputTrack;
class MediaInputPort;
class MediaTrack;
class MediaTrackGraph;
class MediaTrackGraphImpl;
class MediaTrackListener;
class DeviceInputConsumerTrack;
class DeviceInputTrack;
class ProcessedMediaTrack;
class SourceMediaTrack;

class AudioDataListenerInterface {
 protected:
  // Protected destructor, to discourage deletion outside of Release():
  virtual ~AudioDataListenerInterface() = default;

 public:
  /**
   * Number of audio input channels.
   */
  virtual uint32_t RequestedInputChannelCount(
      MediaTrackGraph* aGraph) const = 0;

  /**
   * The input processing params this listener wants the platform to apply.
   */
  virtual cubeb_input_processing_params RequestedInputProcessingParams(
      MediaTrackGraph* aGraph) const = 0;

  /**
   * Whether the underlying audio device is used for voice input.
   */
  virtual bool IsVoiceInput(MediaTrackGraph* aGraph) const = 0;

  /**
   * Called when the underlying audio device has changed.
   */
  virtual void DeviceChanged(MediaTrackGraph* aGraph) = 0;

  /**
   * Called when the underlying audio device is being closed.
   */
  virtual void Disconnect(MediaTrackGraph* aGraph) = 0;

  /**
   * Called sync when attempting to set the input processing params on the
   * underlying input track. Note that when multiple listeners request distinct
   * parameters, aRequestedParams is the aggregated form of those parameters.
   */
  virtual void NotifySetRequestedInputProcessingParams(
      MediaTrackGraph* aGraph, int aGeneration,
      cubeb_input_processing_params aRequestedParams) = 0;

  /**
   * Called async after an attempt to set the input processing params on the
   * underlying input track.
   */
  virtual void NotifySetRequestedInputProcessingParamsResult(
      MediaTrackGraph* aGraph, int aGeneration,
      const Result<cubeb_input_processing_params, int>& aResult) = 0;
};

class AudioDataListener : public AudioDataListenerInterface {
 protected:
  // Protected destructor, to discourage deletion outside of Release():
  virtual ~AudioDataListener() = default;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AudioDataListener)
};

/**
 * This is a base class for main-thread listener callbacks.
 * This callback is invoked on the main thread when the main-thread-visible
 * state of a track has changed.
 *
 * These methods are called with the media graph monitor held, so
 * reentry into general media graph methods is not possible.
 * You should do something non-blocking and non-reentrant (e.g. dispatch an
 * event) and return. NS_DispatchToCurrentThread would be a good choice.
 * The listener is allowed to synchronously remove itself from the track, but
 * not add or remove any other listeners.
 */
class MainThreadMediaTrackListener {
 public:
  virtual void NotifyMainThreadTrackEnded() = 0;
};

/**
 * Helper struct used to keep track of memory usage by AudioNodes.
 */
struct AudioNodeSizes {
  AudioNodeSizes() : mTrack(0), mEngine(0), mNodeType() {}
  size_t mTrack;
  size_t mEngine;
  const char* mNodeType;
};

/**
 * Describes how a track should be disabled.
 *
 * ENABLED        Not disabled.
 * SILENCE_BLACK  Audio data is turned into silence, video frames are made
 *                black.
 * SILENCE_FREEZE Audio data is turned into silence, video freezes at
 *                last frame.
 */
enum class DisabledTrackMode { ENABLED, SILENCE_BLACK, SILENCE_FREEZE };

/**
 * A track of audio or video data. The media type must be known at construction
 * and cannot change. All tracks progress at the same rate --- "real time".
 * Tracks cannot seek. The only operation readers can perform on a track is to
 * read the next data.
 *
 * Consumers of a track can be reading from it at different offsets, but that
 * should only happen due to the order in which consumers are being run.
 * Those offsets must not diverge in the long term, otherwise we would require
 * unbounded buffering.
 *
 * (DEPRECATED to be removed in bug 1581074)
 * Tracks can be in a "blocked" state. While blocked, a track does not
 * produce data. A track can be explicitly blocked via the control API,
 * or implicitly blocked by whatever's generating it (e.g. an underrun in the
 * source resource), or implicitly blocked because something consuming it
 * blocks, or implicitly because it has ended.
 *
 * A track can be in an "ended" state. "Ended" tracks are permanently blocked.
 * The "ended" state is terminal.
 *
 * Transitions into and out of the "blocked" and "ended" states are managed
 * by the MediaTrackGraph on the media graph thread.
 *
 * We buffer media data ahead of the consumers' reading offsets. It is possible
 * to have buffered data but still be blocked.
 *
 * Any track can have its audio or video playing when requested. The media
 * track graph plays audio by constructing audio output tracks as necessary.
 * Video is played through a DirectMediaTrackListener managed by
 * VideoStreamTrack.
 *
 * The data in a track is managed by mSegment. The segment starts at GraphTime
 * mStartTime and encodes its own TrackTime duration.
 *
 * Tracks are explicitly managed. The client creates them via
 * MediaTrackGraph::Create{Source|ForwardedInput}Track, and releases them by
 * calling Destroy() when no longer needed (actual destruction will be
 * deferred). The actual object is owned by the MediaTrackGraph. The basic idea
 * is that main thread objects will keep Tracks alive as long as necessary
 * (using the cycle collector to clean up whenever needed).
 *
 * We make them refcounted only so that track-related messages with
 * MediaTrack* pointers can be sent to the main thread safely.
 *
 * The lifetimes of MediaTracks are controlled from the main thread.
 * For MediaTracks exposed to the DOM, the lifetime is controlled by the DOM
 * wrapper; the DOM wrappers own their associated MediaTracks. When a DOM
 * wrapper is destroyed, it sends a Destroy message for the associated
 * MediaTrack and clears its reference (the last main-thread reference to
 * the object). When the Destroy message is processed on the graph thread we
 * immediately release the affected objects (disentangling them from other
 * objects as necessary).
 *
 * This could cause problems for media processing if a MediaTrack is destroyed
 * while a downstream MediaTrack is still using it. Therefore the DOM wrappers
 * must keep upstream MediaTracks alive as long as they could be used in the
 * media graph.
 *
 * At any time, however, a set of MediaTrack wrappers could be collected via
 * cycle collection. Destroy messages will be sent for those objects in
 * arbitrary order and the MediaTrackGraph has to be able to handle this.
 */
class MediaTrack : public mozilla::LinkedListElement<MediaTrack> {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaTrack)

  MediaTrack(TrackRate aSampleRate, MediaSegment::Type aType,
             MediaSegment* aSegment);

  // The sample rate of the graph.
  const TrackRate mSampleRate;
  const MediaSegment::Type mType;

  enum class Flag { EnableCanonicals };
  using Flags = EnumSet<Flag>;

 protected:
  // Protected destructor, to discourage deletion outside of Release():
  virtual ~MediaTrack();

 public:
  /**
   * Returns the graph that owns this track.
   */
  MediaTrackGraphImpl* GraphImpl();
  const MediaTrackGraphImpl* GraphImpl() const;
  MediaTrackGraph* Graph() { return mGraph; }
  const MediaTrackGraph* Graph() const { return mGraph; }
  /**
   * Sets the graph that owns this track.  Should only be called once.
   */
  void SetGraphImpl(MediaTrackGraphImpl* aGraph, Flags aFlags);
  void SetGraphImpl(MediaTrackGraph* aGraph, Flags aFlags);

  // Control API.
  void AddAudioOutput(void* aKey, const AudioDeviceInfo* aSink);
  void AddAudioOutput(void* aKey, CubebUtils::AudioDeviceID aDeviceID,
                      TrackRate aPreferredSampleRate);
  void SetAudioOutputVolume(void* aKey, float aVolume);
  void RemoveAudioOutput(void* aKey);
  // Explicitly suspend. Useful for example if a media element is pausing
  // and we need to stop its track emitting its buffered data. As soon as the
  // Suspend message reaches the graph, the track stops processing. It
  // ignores its inputs and produces silence/no video until Resumed. Its
  // current time does not advance.
  void Suspend();
  void Resume();
  // Events will be dispatched by calling methods of aListener.
  virtual void AddListener(MediaTrackListener* aListener);
  virtual RefPtr<GenericPromise> RemoveListener(MediaTrackListener* aListener);

  /**
   * Adds aListener to the source track of this track.
   * When the MediaTrackGraph processes the added listener, it will traverse
   * the graph and add it to the track's source track.
   * Note that the listener will be notified on the MediaTrackGraph thread
   * with whether the installation of it at the source was successful or not.
   */
  void AddDirectListener(DirectMediaTrackListener* aListener);

  /**
   * Removes aListener from the source track of this track.
   * Note that the listener has already been removed if the link between the
   * source and this track has been broken. The caller doesn't have to care
   * about this, removing when the source cannot be found, or when the listener
   * had already been removed does nothing.
   */
  void RemoveDirectListener(DirectMediaTrackListener* aListener);

  // A disabled track has video replaced by black, and audio replaced by
  // silence.
  void SetDisabledTrackMode(DisabledTrackMode aMode);

  // End event will be notified by calling methods of aListener. It is the
  // responsibility of the caller to remove aListener before it is destroyed.
  void AddMainThreadListener(MainThreadMediaTrackListener* aListener);
  // It's safe to call this even if aListener is not currently a listener;
  // the call will be ignored.
  void RemoveMainThreadListener(MainThreadMediaTrackListener* aListener) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aListener);
    mMainThreadListeners.RemoveElement(aListener);
  }

  /**
   * Append to the message queue a control message to execute a given lambda
   * function with no parameters.  The queue is drained during
   * RunInStableState().  The lambda will be executed on the graph thread.
   * The lambda will not be executed if the graph has been forced to shut
   * down.
   **/
  template <typename Function>
  void QueueControlMessageWithNoShutdown(Function&& aFunction) {
    QueueMessage(WrapUnique(
        new ControlMessageWithNoShutdown(std::forward<Function>(aFunction))));
  }

  enum class IsInShutdown { No, Yes };
  /**
   * Append to the message queue a control message to execute a given lambda
   * function with a single IsInShutdown parameter.  A No argument indicates
   * execution on the thread of a graph that is still running.  A Yes argument
   * indicates execution on the main thread when the graph has been forced to
   * shut down.
   **/
  template <typename Function>
  void QueueControlOrShutdownMessage(Function&& aFunction) {
    QueueMessage(WrapUnique(
        new ControlOrShutdownMessage(std::forward<Function>(aFunction))));
  }

  /**
   * Ensure a runnable will run on the main thread after running all pending
   * updates that were sent from the graph thread or will be sent before the
   * graph thread receives the next graph update.
   *
   * If the graph has been shut down or destroyed, then the runnable will be
   * dispatched to the event queue immediately.  (There are no pending updates
   * in this situation.)
   *
   * Main thread only.
   */
  void RunAfterPendingUpdates(already_AddRefed<nsIRunnable> aRunnable);

  // Signal that the client is done with this MediaTrack. It will be deleted
  // later.
  virtual void Destroy();

  // Returns the main-thread's view of how much data has been processed by
  // this track.
  TrackTime GetCurrentTime() const {
    NS_ASSERTION(NS_IsMainThread(), "Call only on main thread");
    return mMainThreadCurrentTime;
  }
  // Return the main thread's view of whether this track has ended.
  bool IsEnded() const {
    NS_ASSERTION(NS_IsMainThread(), "Call only on main thread");
    return mMainThreadEnded;
  }

  /**
   * Canonicals for state that can be mirrored to other threads. They are
   * available from when this track is added to a graph with EnableCanonicals
   * until Destroy(), which disconnects any mirrors. Must not be used after
   * Destroy().
   */
  AbstractCanonical<TrackTime>& CanonicalCurrentTime();
  AbstractCanonical<bool>& CanonicalEnded();

  bool IsDestroyed() const {
    NS_ASSERTION(NS_IsMainThread(), "Call only on main thread");
    return mDestroyed;
  }

  uint64_t GetWindowId() const;

  friend class MediaTrackGraphImpl;
  friend class MediaInputPort;
  friend class AudioNodeExternalInputTrack;

  virtual AudioProcessingTrack* AsAudioProcessingTrack() { return nullptr; }
  virtual SourceMediaTrack* AsSourceTrack() { return nullptr; }
  virtual ProcessedMediaTrack* AsProcessedTrack() { return nullptr; }
  virtual AudioNodeTrack* AsAudioNodeTrack() { return nullptr; }
  virtual ForwardedInputTrack* AsForwardedInputTrack() { return nullptr; }
  virtual CrossGraphTransmitter* AsCrossGraphTransmitter() { return nullptr; }
  virtual CrossGraphReceiver* AsCrossGraphReceiver() { return nullptr; }
  virtual DeviceInputTrack* AsDeviceInputTrack() { return nullptr; }
  virtual DeviceInputConsumerTrack* AsDeviceInputConsumerTrack() {
    return nullptr;
  }

  // These Impl methods perform the core functionality of the control methods
  // above, on the media graph thread.
  /**
   * Stop all track activity and disconnect it from all inputs and outputs.
   * This must be idempotent.
   */
  virtual void DestroyImpl();
  TrackTime GetEnd() const;

  /**
   * Removes all direct listeners and signals to them that they have been
   * uninstalled.
   */
  virtual void RemoveAllDirectListenersImpl() {}
  void RemoveAllResourcesAndListenersImpl();

  virtual void AddListenerImpl(already_AddRefed<MediaTrackListener> aListener);
  virtual void RemoveListenerImpl(MediaTrackListener* aListener);
  virtual void AddDirectListenerImpl(
      already_AddRefed<DirectMediaTrackListener> aListener);
  virtual void RemoveDirectListenerImpl(DirectMediaTrackListener* aListener);
  virtual void SetDisabledTrackModeImpl(DisabledTrackMode aMode);

  void AddConsumer(MediaInputPort* aPort) { mConsumers.AppendElement(aPort); }
  void RemoveConsumer(MediaInputPort* aPort) {
    mConsumers.RemoveElement(aPort);
  }
  GraphTime StartTime() const { return mStartTime; }
  bool Ended() const { return mEnded; }

  // Returns the current number of channels this track contains if it's an audio
  // track. Calling this on a video track will trip assertions. Graph thread
  // only.
  virtual uint32_t NumberOfChannels() const = 0;

  // The DisabledTrackMode after combining the explicit mode and that of the
  // input, if any.
  virtual DisabledTrackMode CombinedDisabledMode() const {
    return mDisabledMode;
  }

  template <class SegmentType>
  SegmentType* GetData() const {
    if (!mSegment) {
      return nullptr;
    }
    if (mSegment->GetType() != SegmentType::StaticType()) {
      return nullptr;
    }
    return static_cast<SegmentType*>(mSegment.get());
  }
  MediaSegment* GetData() const { return mSegment.get(); }

  double TrackTimeToSeconds(TrackTime aTime) const {
    NS_ASSERTION(0 <= aTime && aTime <= TRACK_TIME_MAX, "Bad time");
    return static_cast<double>(aTime) / mSampleRate;
  }
  int64_t TrackTimeToMicroseconds(TrackTime aTime) const {
    NS_ASSERTION(0 <= aTime && aTime <= TRACK_TIME_MAX, "Bad time");
    return (aTime * 1000000) / mSampleRate;
  }
  TrackTime SecondsToNearestTrackTime(double aSeconds) const {
    NS_ASSERTION(0 <= aSeconds && aSeconds <= TRACK_TICKS_MAX / TRACK_RATE_MAX,
                 "Bad seconds");
    return mSampleRate * aSeconds + 0.5;
  }
  TrackTime MicrosecondsToTrackTimeRoundDown(int64_t aMicroseconds) const {
    return (aMicroseconds * mSampleRate) / 1000000;
  }

  TrackTicks TimeToTicksRoundUp(TrackRate aRate, TrackTime aTime) const {
    return RateConvertTicksRoundUp(aRate, mSampleRate, aTime);
  }
  TrackTime TicksToTimeRoundDown(TrackRate aRate, TrackTicks aTicks) const {
    return RateConvertTicksRoundDown(mSampleRate, aRate, aTicks);
  }
  /**
   * Convert graph time to track time. aTime must be <= mStateComputedTime
   * to ensure we know exactly how much time this track will be blocked during
   * the interval.
   */
  TrackTime GraphTimeToTrackTimeWithBlocking(GraphTime aTime) const;
  /**
   * Convert graph time to track time. This assumes there is no blocking time
   * to take account of, which is always true except between a track
   * having its blocking time calculated in UpdateGraph and its blocking time
   * taken account of in UpdateCurrentTimeForTracks.
   */
  TrackTime GraphTimeToTrackTime(GraphTime aTime) const;
  /**
   * Convert track time to graph time. This assumes there is no blocking time
   * to take account of, which is always true except between a track
   * having its blocking time calculated in UpdateGraph and its blocking time
   * taken account of in UpdateCurrentTimeForTracks.
   */
  GraphTime TrackTimeToGraphTime(TrackTime aTime) const;

  virtual void ApplyTrackDisabling(MediaSegment* aSegment,
                                   MediaSegment* aRawSegment = nullptr);

  // Return true if the main thread needs to observe updates from this track.
  virtual bool MainThreadNeedsUpdates() const { return true; }

  virtual size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const;
  virtual size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const;

  bool IsSuspended() const { return mSuspendedCount > 0; }
  /**
   * Increment suspend count and move it to GraphImpl()->mSuspendedTracks if
   * necessary.  Graph thread.
   */
  void IncrementSuspendCount();
  /**
   * Increment suspend count on aTrack and move it to GraphImpl()->mTracks if
   * necessary.  GraphThread.
   */
  virtual void DecrementSuspendCount();

  void AssertOnGraphThread() const;
  void AssertOnGraphThreadOrNotRunning() const;

  /**
   * For use during ProcessedMediaTrack::ProcessInput() or
   * MediaTrackListener callbacks, when graph state cannot be changed.
   * Queues a control message to execute a given lambda function with no
   * parameters after processing, at a time when graph state can be changed.
   * The lambda will always be executed before handing control of the graph
   * to the main thread for shutdown.
   * Graph thread.
   */
  template <typename Function>
  void RunAfterProcessing(Function&& aFunction) {
    RunMessageAfterProcessing(NS_NewRunnableFunction(
        "MediaTrack::RunAfterProcessing", std::forward<Function>(aFunction)));
  }

  class ControlMessageInterface;

 protected:
  // Called on graph thread either during destroy handling or before handing
  // graph control to the main thread to release tracks.
  virtual void OnGraphThreadDone() {}

  // |AdvanceTimeVaryingValuesToCurrentTime| will be override in
  // SourceMediaTrack.
  virtual void AdvanceTimeVaryingValuesToCurrentTime(GraphTime aCurrentTime,
                                                     GraphTime aBlockedTime);

  // Destroy the canonicals, breaking the strong reference they hold to their
  // owner thread, which is this track's graph. Graph thread.
  void DisconnectCanonicals();

 private:
  template <typename Function>
  class ControlMessageWithNoShutdown;
  template <typename Function>
  class ControlOrShutdownMessage;
  class ControlMessageWrapper;

  void QueueMessage(UniquePtr<ControlMessageInterface> aMessage);
  void RunMessageAfterProcessing(already_AddRefed<nsIRunnable> aMessage);

  void NotifyMainThreadListeners() {
    NS_ASSERTION(NS_IsMainThread(), "Call only on main thread");

    for (int32_t i = mMainThreadListeners.Length() - 1; i >= 0; --i) {
      mMainThreadListeners[i]->NotifyMainThreadTrackEnded();
    }
    mMainThreadListeners.Clear();
  }

  bool ShouldNotifyTrackEnded() {
    NS_ASSERTION(NS_IsMainThread(), "Call only on main thread");
    if (!mMainThreadEnded || mEndedNotificationSent) {
      return false;
    }

    mEndedNotificationSent = true;
    return true;
  }

 protected:
  // Notifies listeners and consumers of the change in disabled mode when the
  // current combined mode is different from aMode.
  void NotifyIfDisabledModeChangedFrom(DisabledTrackMode aOldMode);

  // This state is all initialized on the main thread but
  // otherwise modified only on the media graph thread.

  // Buffered data. The start of the buffer corresponds to mStartTime.
  // Conceptually the buffer contains everything this track has ever played,
  // but we forget some prefix of the buffered data to bound the space usage.
  // Note that this may be null for tracks that never contain data, like
  // non-external AudioNodeTracks.
  const UniquePtr<MediaSegment> mSegment;

  // The time when the buffered data could be considered to have started
  // playing. This increases over time to account for time the track was
  // blocked before mCurrentTime.
  GraphTime mStartTime;

  // The time until which we last called mSegment->ForgetUpTo().
  TrackTime mForgottenTime;

  // True once we've processed mSegment until the end and no more data will be
  // added. Note that mSegment might still contain data for the current
  // iteration.
  bool mEnded;

  // True after track listeners have been notified that this track has ended.
  bool mNotifiedEnded;

  // Client-set volume of this track
  nsTArray<RefPtr<MediaTrackListener>> mTrackListeners;
  nsTArray<MainThreadMediaTrackListener*> mMainThreadListeners;
  // This track's associated disabled mode. It can either by disabled by frames
  // being replaced by black, or by retaining the previous frame.
  DisabledTrackMode mDisabledMode;

  // GraphTime at which this track starts blocking.
  // This is only valid up to mStateComputedTime. The track is considered to
  // have not been blocked before mCurrentTime (its mStartTime is
  // increased as necessary to account for that time instead).
  GraphTime mStartBlocking;

  // MediaInputPorts to which this is connected
  nsTArray<MediaInputPort*> mConsumers;

  /**
   * Number of outstanding suspend operations on this track. Track is
   * suspended when this is > 0.
   */
  int32_t mSuspendedCount;

  /**
   * State this track publishes for mirroring, as MediaTrackGraphImpl does.
   * Created when the track joins a graph and destroyed with its graph thread
   * state, since a canonical holds a strong reference to its owner thread --
   * here the graph. State is set on the graph thread.
   */
  struct Canonicals {
    Canonicals(AbstractThread* aGraphThread, TrackTime aCurrentTime)
        : mCurrentTime(aGraphThread, aCurrentTime,
                       "MediaTrack::Canonicals::mCurrentTime"),
          mEnded(aGraphThread, false, "MediaTrack::Canonicals::mEnded") {}

    Canonical<TrackTime> mCurrentTime;
    Canonical<bool> mEnded;
  };
  // Canonical state for mirroring to other threads. Valid for use on any thread
  // until Destroy().
  Maybe<Canonicals> mCanonicals;

  // Main-thread views of state
  TrackTime mMainThreadCurrentTime;
  bool mMainThreadEnded;
  bool mEndedNotificationSent;

  // Set in Destroy(), on a control thread.
  Atomic<bool> mDestroyed;

  // Our media track graph. Valid for use on any thread until Destroy().
  // Valid on the graph thread until cleared when destroyed on the graph thread.
  MediaTrackGraph* mGraph;
};

/**
 * This is a track into which a decoder can write audio or video.
 *
 * Audio or video can be written on any thread, but you probably want to
 * always write from the same thread to avoid unexpected interleavings.
 *
 * For audio the sample rate of the written data can differ from the sample rate
 * of the graph itself. Use SetAppendDataSourceRate to inform the track what
 * rate written audio data will be sampled in.
 */
class SourceMediaTrack : public MediaTrack {
 public:
  SourceMediaTrack(MediaSegment::Type aType, TrackRate aSampleRate);

  SourceMediaTrack* AsSourceTrack() override { return this; }

  // Main thread only

  /**
   * Enable or disable pulling.
   * When pulling is enabled, NotifyPull gets called on the
   * MediaTrackListeners for this track during the MediaTrackGraph
   * control loop. Pulling is initially disabled. Due to unavoidable race
   * conditions, after a call to SetPullingEnabled(false) it is still possible
   * for a NotifyPull to occur.
   */
  void SetPullingEnabled(bool aEnabled);

  // MediaTrackGraph thread only
  void DestroyImpl() override;

  // Call these on any thread.
  /**
   * Call all MediaTrackListeners to request new data via the NotifyPull
   * API (if enabled).
   * aDesiredUpToTime (in): end time of new data requested.
   *
   * Returns true if new data is about to be added.
   */
  bool PullNewData(GraphTime aDesiredUpToTime);

  /**
   * Extract any state updates pending in the track, and apply them.
   */
  void ExtractPendingInput(GraphTime aCurrentTime, GraphTime aDesiredUpToTime);

  /**
   * All data appended with AppendData() from this point on will be resampled
   * from aRate to the graph rate.
   *
   * Resampling for video does not make sense and is forbidden.
   */
  void SetAppendDataSourceRate(TrackRate aRate);

  /**
   * Append media data to this track. Ownership of aSegment remains with the
   * caller, but aSegment is emptied. Returns 0 if the data was not appended
   * because the stream has ended. Returns the duration of the appended data in
   * the graph's track rate otherwise.
   */
#ifdef ENABLE_TESTS
  virtual
#endif
      TrackTime AppendData(MediaSegment* aSegment,
                           MediaSegment* aRawSegment = nullptr);

  /**
   * Clear any data appended with AppendData() that hasn't entered the graph
   * yet. Returns the duration of the cleared data in the graph's track rate.
   */
  TrackTime ClearFutureData();

  /**
   * Indicate that this track has ended. Do not do any more API calls affecting
   * this track.
   */
  void End();

  void SetDisabledTrackModeImpl(DisabledTrackMode aMode) override;

  uint32_t NumberOfChannels() const override;

  void RemoveAllDirectListenersImpl() override;

  // The value set here is applied in MoveToSegment so we can avoid the
  // buffering delay in applying the change. See Bug 1443511.
  void SetVolume(float aVolume);
  float GetVolumeLocked() MOZ_REQUIRES(mMutex);

  Mutex& GetMutex() MOZ_RETURN_CAPABILITY(mMutex) { return mMutex; }

  friend class MediaTrackGraphImpl;

 protected:
  enum TrackCommands : uint32_t;

  virtual ~SourceMediaTrack();

  /**
   * Data to cater for appending media data to this track.
   */
  struct TrackData {
    // Sample rate of the input data.
    TrackRate mInputRate;
    // Resampler if the rate of the input track does not match the
    // MediaTrackGraph's.
    nsAutoRef<SpeexResamplerState> mResampler;
    uint32_t mResamplerChannelCount;
    // Each time the track updates are flushed to the media graph thread,
    // the segment buffer is emptied.
    UniquePtr<MediaSegment> mData;
    // True once the producer has signaled that no more data is coming.
    bool mEnded;
    // True if the producer of this track is having data pulled by the graph.
    bool mPullingEnabled;
    // True if the graph has notified this track that it will not be used
    // again on the graph thread.
    bool mGraphThreadDone;
  };

  bool NeedsMixing();

  void ResampleAudioToGraphSampleRate(MediaSegment* aSegment)
      MOZ_REQUIRES(mMutex);

  void AddDirectListenerImpl(
      already_AddRefed<DirectMediaTrackListener> aListener) override;
  void RemoveDirectListenerImpl(DirectMediaTrackListener* aListener) override;

  /**
   * Notify direct consumers of new data to this track.
   * The data doesn't have to be resampled (though it may be).  This is called
   * from AppendData on the thread providing the data, and will call
   * the Listeners on this thread.
   */
  void NotifyDirectConsumers(MediaSegment* aSegment) MOZ_REQUIRES(mMutex);

  void OnGraphThreadDone() override {
    MutexAutoLock lock(mMutex);
    if (!mUpdateTrack) {
      return;
    }
    mUpdateTrack->mGraphThreadDone = true;
    if (!mUpdateTrack->mData) {
      return;
    }
    mUpdateTrack->mData->Clear();
  }

  virtual void AdvanceTimeVaryingValuesToCurrentTime(
      GraphTime aCurrentTime, GraphTime aBlockedTime) override;

  // This must be acquired *before* MediaTrackGraphImpl's lock, if they are
  // held together.
  Mutex mMutex;
  // protected by mMutex
  float mVolume MOZ_GUARDED_BY(mMutex) = 1.0;
  UniquePtr<TrackData> mUpdateTrack MOZ_GUARDED_BY(mMutex);
  // This track's associated disabled mode for uses on the producing thread.
  // It can either by disabled by frames being replaced by black, or by
  // retaining the previous frame.
  DisabledTrackMode mDirectDisabledMode MOZ_GUARDED_BY(mMutex) =
      DisabledTrackMode::ENABLED;
  nsTArray<RefPtr<DirectMediaTrackListener>> mDirectTrackListeners
      MOZ_GUARDED_BY(mMutex);
};

/**
 * A ref-counted wrapper of a MediaTrack that allows multiple users to share a
 * reference to the same MediaTrack with the purpose of being guaranteed that
 * the graph it is in is kept alive.
 *
 * Automatically suspended on creation and destroyed on destruction. Main thread
 * only.
 */
struct SharedDummyTrack {
  NS_INLINE_DECL_REFCOUNTING(SharedDummyTrack)
  explicit SharedDummyTrack(MediaTrack* aTrack) : mTrack(aTrack) {
    mTrack->Suspend();
  }
  const RefPtr<MediaTrack> mTrack;

 private:
  ~SharedDummyTrack() { mTrack->Destroy(); }
};

/**
 * Represents a connection between a ProcessedMediaTrack and one of its
 * input tracks.
 * We make these refcounted so that track-related messages with MediaInputPort*
 * pointers can be sent to the main thread safely.
 *
 * When a port's source or destination track dies, the track's DestroyImpl
 * calls MediaInputPort::Disconnect to disconnect the port from
 * the source and destination tracks.
 *
 * The lifetimes of MediaInputPort are controlled from the main thread.
 * The media graph adds a reference to the port. When a MediaInputPort is no
 * longer needed, main-thread code sends a Destroy message for the port and
 * clears its reference (the last main-thread reference to the object). When
 * the Destroy message is processed on the graph manager thread we disconnect
 * the port and drop the graph's reference, destroying the object.
 */
class MediaInputPort final {
 private:
  // Do not call this constructor directly. Instead call
  // aDest->AllocateInputPort.
  MediaInputPort(MediaTrackGraphImpl* aGraph, MediaTrack* aSource,
                 ProcessedMediaTrack* aDest, uint16_t aInputNumber,
                 uint16_t aOutputNumber)
      : mSource(aSource),
        mDest(aDest),
        mInputNumber(aInputNumber),
        mOutputNumber(aOutputNumber),
        mGraph(aGraph) {
    MOZ_COUNT_CTOR(MediaInputPort);
  }

  // Private destructor, to discourage deletion outside of Release():
  MOZ_COUNTED_DTOR(MediaInputPort)

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaInputPort)

  /**
   * Disconnects and destroys the port. The caller must not reference this
   * object again. Main thread.
   */
  void Destroy();

  // The remaining methods and members must always be called on the graph thread
  // from within MediaTrackGraph.cpp.

  void Init();
  // Called during message processing to trigger removal of this port's source
  // and destination tracks.
  void Disconnect();

  MediaTrack* GetSource() const;
  ProcessedMediaTrack* GetDestination() const;

  uint16_t InputNumber() const { return mInputNumber; }
  uint16_t OutputNumber() const { return mOutputNumber; }

  struct InputInterval {
    GraphTime mStart;
    GraphTime mEnd;
    bool mInputIsBlocked;
  };
  // Find the next time interval starting at or after aTime during which
  // aPort->mDest is not blocked and aPort->mSource's blocking status does not
  // change. A null aPort returns a blocked interval starting at aTime.
  static InputInterval GetNextInputInterval(MediaInputPort const* aPort,
                                            GraphTime aTime);

  /**
   * Returns the graph that owns this port.
   */
  MediaTrackGraphImpl* GraphImpl() const;
  MediaTrackGraph* Graph() const;

  /**
   * Sets the graph that owns this track.  Should only be called once.
   */
  void SetGraphImpl(MediaTrackGraphImpl* aGraph);

  /**
   * Notify the port that the source MediaTrack has been suspended.
   */
  void Suspended();

  /**
   * Notify the port that the source MediaTrack has been resumed.
   */
  void Resumed();

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
    size_t amount = 0;

    // Not owned:
    // - mSource
    // - mDest
    // - mGraph
    return amount;
  }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
  }

 private:
  friend class ProcessedMediaTrack;
  // Never modified after Init()
  MediaTrack* mSource;
  ProcessedMediaTrack* mDest;
  // The input and output numbers are optional, and are currently only used by
  // Web Audio.
  const uint16_t mInputNumber;
  const uint16_t mOutputNumber;

  // Our media track graph
  MediaTrackGraphImpl* mGraph;
};

/**
 * This track processes zero or more input tracks in parallel to produce
 * its output. The details of how the output is produced are handled by
 * subclasses overriding the ProcessInput method.
 */
class ProcessedMediaTrack : public MediaTrack {
 public:
  ProcessedMediaTrack(TrackRate aSampleRate, MediaSegment::Type aType,
                      MediaSegment* aSegment)
      : MediaTrack(aSampleRate, aType, aSegment),
        mAutoend(true),
        mCycleMarker(0) {}

  // Control API.
  /**
   * Allocates a new input port attached to source aTrack.
   * This port can be removed by calling MediaInputPort::Destroy().
   */
  already_AddRefed<MediaInputPort> AllocateInputPort(
      MediaTrack* aTrack, uint16_t aInputNumber = 0,
      uint16_t aOutputNumber = 0);
  /**
   * Queue a message to set the autoend flag on this track (defaults to
   * true). When this flag is set, and the input track has ended playout
   * (including if there is no input track), this track automatically
   * enters the ended state.
   */
  virtual void QueueSetAutoend(bool aAutoend);

  ProcessedMediaTrack* AsProcessedTrack() override { return this; }

  friend class MediaTrackGraphImpl;

  // Do not call these from outside MediaTrackGraph.cpp!
  virtual void AddInput(MediaInputPort* aPort);
  virtual void RemoveInput(MediaInputPort* aPort) {
    mInputs.RemoveElement(aPort) || mSuspendedInputs.RemoveElement(aPort);
  }
  bool HasInputPort(MediaInputPort* aPort) const {
    return mInputs.Contains(aPort) || mSuspendedInputs.Contains(aPort);
  }
  uint32_t InputPortCount() const {
    return mInputs.Length() + mSuspendedInputs.Length();
  }
  void InputSuspended(MediaInputPort* aPort);
  void InputResumed(MediaInputPort* aPort);
  void DestroyImpl() override;
  void DecrementSuspendCount() override;
  /**
   * This gets called after we've computed the blocking states for all
   * tracks (mBlocked is up to date up to mStateComputedTime).
   * Also, we've produced output for all tracks up to this one. If this track
   * is not in a cycle, then all its source tracks have produced data.
   * Generate output from aFrom to aTo, where aFrom < aTo.
   * This will be called on tracks that have ended. Most track types should
   * just return immediately if they're ended, but some may wish to update
   * internal state (see AudioNodeTrack).
   * ProcessInput is allowed to set mEnded only if ALLOW_END is in aFlags. (This
   * flag will be set when aTo >= mStateComputedTime, i.e. when we've produced
   * the last block of data we need to produce.) Otherwise we can get into a
   * situation where we've determined the track should not block before
   * mStateComputedTime, but the track ends before mStateComputedTime, violating
   * the invariant that ended tracks are blocked.
   */
  enum { ALLOW_END = 0x01 };
  virtual void ProcessInput(GraphTime aFrom, GraphTime aTo,
                            uint32_t aFlags) = 0;
  void SetAutoendImpl(bool aAutoend) { mAutoend = aAutoend; }

  // Only valid after MediaTrackGraphImpl::UpdateTrackOrder() has run.
  // A DelayNode is considered to break a cycle and so this will not return
  // true for echo loops, only for muted cycles.
  bool InMutedCycle() const { return mCycleMarker; }

  // Used by ForwardedInputTrack to propagate the disabled mode along the graph.
  virtual void OnInputDisabledModeChanged(DisabledTrackMode aMode) {}

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const override {
    size_t amount = MediaTrack::SizeOfExcludingThis(aMallocSizeOf);
    // Not owned:
    // - mInputs elements
    // - mSuspendedInputs elements
    amount += mInputs.ShallowSizeOfExcludingThis(aMallocSizeOf);
    amount += mSuspendedInputs.ShallowSizeOfExcludingThis(aMallocSizeOf);
    return amount;
  }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const override {
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
  }

 protected:
  // This state is all accessed only on the media graph thread.

  // The list of all inputs that are not currently suspended.
  nsTArray<MediaInputPort*> mInputs;
  // The list of all inputs that are currently suspended.
  nsTArray<MediaInputPort*> mSuspendedInputs;
  bool mAutoend;
  // After UpdateTrackOrder(), mCycleMarker is either 0 or 1 to indicate
  // whether this track is in a muted cycle.  During ordering it can contain
  // other marker values - see MediaTrackGraphImpl::UpdateTrackOrder().
  uint32_t mCycleMarker;
};

/**
 * There is a single MediaTrackGraph per window.
 * Additionaly, each OfflineAudioContext object creates its own MediaTrackGraph
 * object too.
 */
class MediaTrackGraph {
 public:
  // We ensure that the graph current time advances in multiples of
  // IdealAudioBlockSize()/AudioStream::PreferredSampleRate(). A track that
  // never blocks and has the ideal audio rate will produce audio in multiples
  // of the block size.

  // Initializing a graph that outputs audio can take quite long on some
  // platforms. Code that want to output audio at some point can express the
  // fact that they will need an audio track at some point by passing
  // AUDIO_THREAD_DRIVER when getting an instance of MediaTrackGraph, so that
  // the graph starts with the right driver.
  enum GraphDriverType {
    AUDIO_THREAD_DRIVER,
    SYSTEM_THREAD_DRIVER,
    OFFLINE_THREAD_DRIVER
  };
  // A MediaTrackGraph running an AudioWorklet must always be run from the
  // same thread, in order to run js. To acheive this, create the graph with
  // a SINGLE_THREAD RunType. DIRECT_DRIVER will run the graph directly off
  // the GraphDriver's thread.
  enum GraphRunType {
    DIRECT_DRIVER,
    SINGLE_THREAD,
  };
  static const uint32_t AUDIO_CALLBACK_DRIVER_SHUTDOWN_TIMEOUT = 20 * 1000;
  static const TrackRate REQUEST_DEFAULT_SAMPLE_RATE = 0;
  constexpr static const CubebUtils::AudioDeviceID DEFAULT_OUTPUT_DEVICE =
      nullptr;

  // Main thread only
  static MediaTrackGraph* GetInstanceIfExists(
      nsPIDOMWindowInner* aWindow, TrackRate aSampleRate,
      CubebUtils::AudioDeviceID aPrimaryOutputDeviceID);
  static MediaTrackGraph* GetInstance(
      GraphDriverType aGraphDriverRequested, nsPIDOMWindowInner* aWindow,
      TrackRate aSampleRate, CubebUtils::AudioDeviceID aPrimaryOutputDeviceID);
  static MediaTrackGraph* CreateNonRealtimeInstance(TrackRate aSampleRate);

  // Idempotent
  void ForceShutDown();

  virtual void OpenAudioInput(DeviceInputTrack* aTrack) = 0;
  virtual void CloseAudioInput(DeviceInputTrack* aTrack) = 0;

  // Control API.
  /**
   * Create a track that a media decoder (or some other source of
   * media data, such as a camera) can write to.
   */
  SourceMediaTrack* CreateSourceTrack(MediaSegment::Type aType);
  /**
   * Create a track that will forward data from its input track.
   *
   * A TrackUnionStream can have 0 or 1 input streams. Adding more than that is
   * an error.
   *
   * A TrackUnionStream will end when autoending is enabled (default) and there
   * is no input, or the input's source is ended. If there is no input and
   * autoending is disabled, TrackUnionStream will continue to produce silence
   * for audio or the last video frame for video.
   */
  ProcessedMediaTrack* CreateForwardedInputTrack(MediaSegment::Type aType);
  /**
   * Create a track that will mix all its audio inputs.
   */
  AudioCaptureTrack* CreateAudioCaptureTrack();

  CrossGraphTransmitter* CreateCrossGraphTransmitter(
      CrossGraphReceiver* aReceiver);
  CrossGraphReceiver* CreateCrossGraphReceiver(TrackRate aTransmitterRate);

  /**
   * Add a new track to the graph.  Main thread.
   */
  void AddTrack(MediaTrack* aTrack, MediaTrack::Flags aFlags);

  /* From the main thread, ask the MTG to resolve the returned promise when
   * the device specified has started.
   * A null aDeviceID indicates the default audio output device.
   * The promise is rejected with NS_ERROR_INVALID_ARG if aDeviceID does not
   * correspond to any output devices used by the graph, or
   * NS_ERROR_NOT_AVAILABLE if outputs to the device are removed or
   * NS_ERROR_ILLEGAL_DURING_SHUTDOWN if the graph is force shut down
   * before the promise could be resolved.
   */
  using GraphStartedPromise = GenericPromise;
  virtual RefPtr<GraphStartedPromise> NotifyWhenDeviceStarted(
      CubebUtils::AudioDeviceID aDeviceID) = 0;

  /* From the main thread, suspend, resume or close an AudioContext.  Calls
   * are not counted.  Even Resume calls can be more frequent than Suspend
   * calls.
   *
   * aTracks are the tracks of all the AudioNodes of the AudioContext that
   * need to be suspended or resumed.  Suspend and Resume operations on these
   * tracks are counted.  Resume operations must not outnumber Suspends and a
   * track will not resume until the number of Resume operations matches the
   * number of Suspends.  This array may be empty if, for example, this is a
   * second consecutive suspend call and all the nodes are already suspended.
   *
   * This can possibly pause the graph thread, releasing system resources, if
   * all tracks have been suspended/closed.
   *
   * When the operation is complete, the returned promise is resolved.
   */
  using AudioContextOperationPromise =
      MozPromise<dom::AudioContextState, bool, true>;
  RefPtr<AudioContextOperationPromise> ApplyAudioContextOperation(
      MediaTrack* aDestinationTrack, nsTArray<RefPtr<MediaTrack>> aTracks,
      dom::AudioContextOperation aOperation);

  bool IsNonRealtime() const;
  /**
   * Start processing non-realtime for a specific number of ticks.
   */
  void StartNonRealtimeProcessing(uint32_t aTicksToProcess);

  /**
   * NotifyJSContext() is called on the graph thread before content script
   * runs.
   */
  void NotifyJSContext(JSContext* aCx);

  /**
   * Media graph thread only.
   * Dispatches a runnable that will run on the main thread after all
   * main-thread track state has been updated, i.e., during stable state.
   *
   * Should only be called during MediaTrackListener callbacks or during
   * ProcessedMediaTrack::ProcessInput().
   *
   * Note that if called during shutdown the runnable will be ignored and
   * released on main thread.
   */
  void DispatchToMainThreadStableState(already_AddRefed<nsIRunnable> aRunnable);
  /* Called on the graph thread when the input device settings should be
   * reevaluated, for example, if the channel count of the input track should
   * be changed. */
  void ReevaluateInputDevice(CubebUtils::AudioDeviceID aID);

  /**
   * Returns graph sample rate in Hz.
   */
  TrackRate GraphRate() const { return mSampleRate; }
  /**
   * Returns the ID of the device used for audio output through an
   * AudioCallbackDriver.  This is the device specified when creating the
   * graph.  Can be called on any thread.
   */
  CubebUtils::AudioDeviceID PrimaryOutputDeviceID() const {
    return mPrimaryOutputDeviceID;
  }

  double AudioOutputLatency();
  /* Return whether the clock for the audio output device used for the AEC
   * reverse stream might drift from the clock for this MediaTrackGraph.
   * Graph thread only. */
  bool OutputForAECMightDrift();
  /* Return whether the audio output device used for the aec reverse stream
   * corresponds to the primary output device, explicitly or implicitly,
   * implicitly meaning when the primary output device is the system default
   * output device, and the output device used for the aec reverse stream is
   * explicit and matches the current system default output device.
   * Graph thread only. */
  bool OutputForAECIsPrimary();

  void RegisterCaptureTrackForWindow(uint64_t aWindowId,
                                     ProcessedMediaTrack* aCaptureTrack);
  void UnregisterCaptureTrackForWindow(uint64_t aWindowId);
  already_AddRefed<MediaInputPort> ConnectToCaptureTrack(
      uint64_t aWindowId, MediaTrack* aMediaTrack);

  void AssertOnGraphThread() const { MOZ_ASSERT(OnGraphThread()); }
  void AssertOnGraphThreadOrNotRunning() const {
    MOZ_ASSERT(OnGraphThreadOrNotRunning());
  }

  /**
   * Returns a watchable of the graph's main-thread observable graph time.
   * Main thread only.
   */
  virtual Watchable<GraphTime>& CurrentTime() = 0;

  /**
   * Graph thread function to return the time at which all processing has been
   * completed.  Some tracks may have performed processing beyond this time.
   */
  GraphTime ProcessedTime() const;
  /**
   * For Graph thread logging.
   */
  void* CurrentDriver() const;

  /* Do not call this directly. For users who need to get a DeviceInputTrack,
   * use DeviceInputTrack::OpenAudio() instead. This should only be used in
   * DeviceInputTrack to get the existing DeviceInputTrack paired with the given
   * device in this graph. Main thread only.*/
  DeviceInputTrack* GetDeviceInputTrackMainThread(
      CubebUtils::AudioDeviceID aID);

  /* Do not call this directly. This should only be used in DeviceInputTrack to
   * get the existing NativeInputTrackMain thread only.*/
  NativeInputTrack* GetNativeInputTrackMainThread();

  /* Return a positive monotonically increasing graph-unique generation id for
   * AudioInputProcessingParamsRequest::mGeneration. */
  int ProcessingParamsGeneration() { return ++mProcessingParamsGeneration; }

 protected:
  explicit MediaTrackGraph(TrackRate aSampleRate,
                           CubebUtils::AudioDeviceID aPrimaryOutputDeviceID)
      : mSampleRate(aSampleRate),
        mPrimaryOutputDeviceID(aPrimaryOutputDeviceID) {
    MOZ_COUNT_CTOR(MediaTrackGraph);
  }
  MOZ_COUNTED_DTOR_VIRTUAL(MediaTrackGraph)

  // Intended only for assertions, either on graph thread or not running (in
  // which case we must be on the main thread).
  virtual bool OnGraphThreadOrNotRunning() const = 0;
  virtual bool OnGraphThread() const = 0;

  // Intended only for internal assertions. Main thread only.
  virtual bool Destroyed() const = 0;

  /**
   * Sample rate at which this graph runs. For real time graphs, this is
   * the rate of the audio mixer. For offline graphs, this is the rate specified
   * at construction.
   */
  const TrackRate mSampleRate;
  /**
   * Device to use for audio output through an AudioCallbackDriver.
   * This is the device specified when creating the graph.
   */
  const CubebUtils::AudioDeviceID mPrimaryOutputDeviceID;

  /* A monotonically increasing graph-unique generation for
   * AudioInputProcessingParamsRequest::mGeneration. */
  int mProcessingParamsGeneration = 0;
};

inline void MediaTrack::AssertOnGraphThread() const {
  Graph()->AssertOnGraphThread();
}
inline void MediaTrack::AssertOnGraphThreadOrNotRunning() const {
  Graph()->AssertOnGraphThreadOrNotRunning();
}

/**
 * This represents a message run on the graph thread to modify track or graph
 * state.  These are passed from main thread to graph thread by
 * QueueControlMessageWithNoShutdown() or QueueControlOrShutdownMessage()
 * through AppendMessage(), or scheduled on the graph thread with
 * RunAfterProcessing().
 */
class MediaTrack::ControlMessageInterface {
 public:
  MOZ_COUNTED_DEFAULT_CTOR(ControlMessageInterface)
  // All these run on the graph thread unless the graph has been forced to
  // shut down.
  MOZ_COUNTED_DTOR_VIRTUAL(ControlMessageInterface)
  // Do the action of this message on the MediaTrackGraph thread. Any actions
  // affecting graph processing should take effect at mProcessedTime.
  // All track data for times < mProcessedTime has already been
  // computed.
  virtual void Run() = 0;
  // RunDuringShutdown() is only relevant to messages generated on the main
  // thread by QueueControlOrShutdownMessage() or for AppendMessage().
  // When we're shutting down the application, most messages are ignored but
  // some cleanup messages should still be processed (on the main thread).
  // This must not add new control messages to the graph.
  virtual void RunDuringShutdown() {}
};

template <typename Function>
class MediaTrack::ControlMessageWithNoShutdown
    : public ControlMessageInterface {
 public:
  explicit ControlMessageWithNoShutdown(Function&& aFunction)
      : mFunction(std::move(aFunction)) {}

  void Run() override {
    static_assert(std::is_void_v<decltype(mFunction())>,
                  "The lambda must return void!");
    mFunction();
  }

 private:
  using StoredFunction = std::decay_t<Function>;
  StoredFunction mFunction;
};

template <typename Function>
class MediaTrack::ControlOrShutdownMessage : public ControlMessageInterface {
 public:
  explicit ControlOrShutdownMessage(Function&& aFunction)
      : mFunction(std::move(aFunction)) {}

  void Run() override {
    static_assert(std::is_void_v<decltype(mFunction(IsInShutdown()))>,
                  "The lambda must return void!");
    mFunction(IsInShutdown::No);
  }

  void RunDuringShutdown() override { mFunction(IsInShutdown::Yes); }

 private:
  // The same lambda is used whether or not in shutdown so that captured
  // variables are available in both cases.
  using StoredFunction = std::decay_t<Function>;
  StoredFunction mFunction;
};

class MediaTrack::ControlMessageWrapper final : public DiscardableRunnable {
  std::unique_ptr<ControlMessageInterface> mMessage;

 public:
  explicit ControlMessageWrapper(
      std::unique_ptr<ControlMessageInterface>&& aMessage)
      : DiscardableRunnable("ControlMessageWrapper"),
        mMessage(std::move(aMessage)) {}

  NS_IMETHODIMP Run() override {
    if (mMessage) {
      mMessage->Run();
      mMessage = nullptr;
    }
    return NS_OK;
  }

  void OnDiscard() override {
    if (mMessage) {
      mMessage->RunDuringShutdown();
      mMessage = nullptr;
    }
  }
};

}  // namespace mozilla

#endif /* MOZILLA_MEDIATRACKGRAPH_H_ */
