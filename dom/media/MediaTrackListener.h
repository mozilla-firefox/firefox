/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_MEDIATRACKLISTENER_h_
#define MOZILLA_MEDIATRACKLISTENER_h_

#include "MediaTrackGraph.h"
#include "PrincipalHandle.h"

namespace mozilla {

class MediaTrackGraph;
class MediaStreamVideoSink;
class VideoSegment;

/**
 * This is a base class for media graph thread listener callbacks locked to
 * specific tracks. Override methods to be notified of audio or video data or
 * changes in track state.
 *
 * All notification methods are called from the media graph thread. Overriders
 * of these methods are responsible for all synchronization. Beware!
 * These methods are called without the media graph monitor held, so
 * reentry into media graph methods is possible, although very much discouraged!
 * You should do something non-blocking and non-reentrant (e.g. dispatch an
 * event to some thread) and return.
 * The listener is not allowed to add/remove any listeners from the parent
 * track.
 *
 * If a listener is attached to a track that has already ended, we guarantee
 * to call NotifyEnded.
 */
class MediaTrackListener {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaTrackListener)

 public:
  /**
   * When a SourceMediaTrack has pulling enabled, and the MediaTrackGraph
   * control loop is ready to pull, this gets called for each track in the
   * SourceMediaTrack that is lacking data for the current iteration.
   * A NotifyPull implementation is allowed to call the SourceMediaTrack
   * methods that alter track data.
   *
   * It is not allowed to make other MediaTrack API calls, including
   * calls to add or remove MediaTrackListeners. It is not allowed to
   * block for any length of time.
   *
   * aEndOfAppendedData is the duration of the data that has already been
   * appended to this track, in track time.
   *
   * aDesiredTime is the track time we should append data up to. Data
   * beyond this point will not be played until NotifyPull runs again, so
   * there's not much point in providing it. Note that if the track is blocked
   * for some reason, then data before aDesiredTime may not be played
   * immediately.
   */
  virtual void NotifyPull(MediaTrackGraph* aGraph, TrackTime aEndOfAppendedData,
                          TrackTime aDesiredTime) {}

  virtual void NotifyQueuedChanges(MediaTrackGraph* aGraph,
                                   TrackTime aTrackOffset,
                                   const MediaSegment& aQueuedMedia) {}

  virtual void NotifyPrincipalHandleChanged(
      MediaTrackGraph* aGraph, const PrincipalHandle& aNewPrincipalHandle) {}

  /**
   * Notify that the enabled state for the track this listener is attached to
   * has changed.
   *
   * The enabled state here is referring to whether audio should be audible
   * (enabled) or silent (not enabled); or whether video should be displayed as
   * is (enabled), or black (not enabled).
   */
  virtual void NotifyEnabledStateChanged(MediaTrackGraph* aGraph,
                                         bool aEnabled) {}

  /**
   * Notify that the track output is advancing. aCurrentTrackTime is the number
   * of samples that has been played out for this track in track time.
   */
  virtual void NotifyOutput(MediaTrackGraph* aGraph,
                            TrackTime aCurrentTrackTime) {}

  /**
   * Notify that this track has been ended and all data has been played out.
   */
  virtual void NotifyEnded(MediaTrackGraph* aGraph) {}

  /**
   * Notify that this track listener has been removed from the graph, either
   * after shutdown or through MediaTrack::RemoveListener().
   */
  virtual void NotifyRemoved(MediaTrackGraph* aGraph) {}

 protected:
  virtual ~MediaTrackListener() = default;
};

/**
 * This is a base class for media graph thread listener direct callbacks from
 * within AppendToTrack(). It is bound to a certain track, and should be used
 * only with video tracks. Once added to a track in the graph,
 * the graph will try to install it at that track's source of media data.
 *
 * This works for ForwardedInputTracks, which will forward the listener to the
 * track's input track if it exists, or wait for it to be created before
 * forwarding if it doesn't.
 * Once it reaches a SourceMediaTrack, it can be successfully installed.
 * Other types of tracks will fail installation since they are not supported.
 *
 * Note that this listener and others for the same track will still get
 * NotifyQueuedChanges() callbacks from the MTG tread, so you must be careful
 * to ignore them if this listener was successfully installed.
 */
class DirectMediaTrackListener : public MediaTrackListener {
  friend class SourceMediaTrack;
  friend class ForwardedInputTrack;

 public:
  /*
   * This will be called on any DirectMediaTrackListener added to a
   * SourceMediaTrack when AppendToTrack() is called for the listener's bound
   * track, using the thread of the AppendToTrack() caller. The MediaSegment
   * will be the RawSegment (unresampled) if available in AppendToTrack().
   * If the track is enabled at the source but has been disabled in one of the
   * tracks in between the source and where it was originally added, aMedia
   * will be a disabled version of the one passed to AppendToTrack() as well.
   * Note that NotifyQueuedTrackChanges() calls will also still occur.
   */
  virtual void NotifyRealtimeTrackData(MediaTrackGraph* aGraph,
                                       TrackTime aTrackOffset,
                                       const MediaSegment& aMedia) = 0;

  /**
   * When a direct listener is processed for installation by the
   * MediaTrackGraph it will be notified with whether the installation was
   * successful or not. The results of this installation are the following:
   * TRACK_NOT_SUPPORTED
   *    While looking for the data source of this track, we found a MediaTrack
   *    that is not a SourceMediaTrack or a ForwardedInputTrack.
   * ALREADY_EXISTS
   *    This DirectMediaTrackListener already exists in the
   *    SourceMediaTrack.
   * SUCCESS
   *    Installation was successful and this listener will start receiving
   *    NotifyRealtimeData on the next AppendData().
   */
  enum class InstallationResult {
    TRACK_NOT_SUPPORTED,
    ALREADY_EXISTS,
    SUCCESS
  };
  virtual void NotifyDirectListenerInstalled(InstallationResult aResult) {}
  virtual void NotifyDirectListenerUninstalled() {}

 protected:
  virtual ~DirectMediaTrackListener() = default;

  void MirrorAndDisableSegment(VideoSegment& aFrom, VideoSegment& aTo,
                               DisabledTrackMode aMode);
  void NotifyRealtimeTrackDataAndApplyTrackDisabling(MediaTrackGraph* aGraph,
                                                     TrackTime aTrackOffset,
                                                     MediaSegment& aMedia);

  void IncreaseDisabled(DisabledTrackMode aMode);
  void DecreaseDisabled(DisabledTrackMode aMode);

  // Matches the number of disabled tracks to which this listener is attached.
  // The number of tracks are those between the track where the listener was
  // added and the SourceMediaTrack that is the source of the data reaching
  // this listener.
  Atomic<int32_t> mDisabledFreezeCount;
  Atomic<int32_t> mDisabledBlackCount;
};

}  // namespace mozilla

#endif  // MOZILLA_MEDIATRACKLISTENER_h_
