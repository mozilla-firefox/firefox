/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if !defined(MediaDecoder_h_)
#  define MediaDecoder_h_

#  include "BackgroundVideoDecodingPermissionObserver.h"
#  include "DecoderDoctorDiagnostics.h"
#  include "MediaContainerType.h"
#  include "MediaDecoderOwner.h"
#  include "MediaEventSource.h"
#  include "MediaMetadataManager.h"
#  include "MediaPromiseDefs.h"
#  include "MediaResource.h"
#  include "MediaStatistics.h"
#  include "SeekTarget.h"
#  include "TelemetryProbesReporter.h"
#  include "TimeUnits.h"
#  include "mozilla/Atomics.h"
#  include "mozilla/CDMProxy.h"
#  include "mozilla/DefineEnum.h"
#  include "mozilla/MozPromise.h"
#  include "mozilla/ReentrantMonitor.h"
#  include "mozilla/StateMirroring.h"
#  include "mozilla/StateWatching.h"
#  include "mozilla/dom/MediaDebugInfoBinding.h"
#  include "nsCOMPtr.h"
#  include "nsIObserver.h"
#  include "nsISupports.h"
#  include "nsITimer.h"

class AudioDeviceInfo;
class nsIPrincipal;

namespace mozilla {

class AbstractThread;
class DOMMediaStream;
class ProcessedMediaTrack;
class FrameStatistics;
class VideoFrameContainer;
class MediaFormatReader;
class MediaDecoderStateMachineBase;
struct MediaPlaybackEvent;
struct SharedDummyTrack;

template <typename T>
struct DurationToType {
  double operator()(double aDouble);
  double operator()(const media::TimeUnit& aTimeUnit);
};

template <>
struct DurationToType<double> {
  double operator()(double aDouble) { return aDouble; }
  double operator()(const media::TimeUnit& aTimeUnit) {
    if (aTimeUnit.IsValid()) {
      if (aTimeUnit.IsPosInf()) {
        return std::numeric_limits<double>::infinity();
      }
      if (aTimeUnit.IsNegInf()) {
        return -std::numeric_limits<double>::infinity();
      }
      return aTimeUnit.ToSeconds();
    }
    return std::numeric_limits<double>::quiet_NaN();
  }
};

using DurationToDouble = DurationToType<double>;

template <>
struct DurationToType<media::TimeUnit> {
  media::TimeUnit operator()(double aDouble) {
    return media::TimeUnit::FromSeconds(aDouble);
  }
  media::TimeUnit operator()(const media::TimeUnit& aTimeUnit) {
    return aTimeUnit;
  }
};

using DurationToTimeUnit = DurationToType<media::TimeUnit>;

struct MOZ_STACK_CLASS MediaDecoderInit {
  MediaDecoderOwner* const mOwner;
  TelemetryProbesReporterOwner* const mReporterOwner;
  const double mVolume;
  const bool mPreservesPitch;
  const double mPlaybackRate;
  const bool mMinimizePreroll;
  const bool mHasSuspendTaint;
  const bool mLooping;
  const MediaContainerType mContainerType;
  const nsAutoString mStreamName;

  MediaDecoderInit(MediaDecoderOwner* aOwner,
                   TelemetryProbesReporterOwner* aReporterOwner, double aVolume,
                   bool aPreservesPitch, double aPlaybackRate,
                   bool aMinimizePreroll, bool aHasSuspendTaint, bool aLooping,
                   const MediaContainerType& aContainerType)
      : mOwner(aOwner),
        mReporterOwner(aReporterOwner),
        mVolume(aVolume),
        mPreservesPitch(aPreservesPitch),
        mPlaybackRate(aPlaybackRate),
        mMinimizePreroll(aMinimizePreroll),
        mHasSuspendTaint(aHasSuspendTaint),
        mLooping(aLooping),
        mContainerType(aContainerType) {}
};

DDLoggedTypeDeclName(MediaDecoder);

class MediaDecoder : public DecoderDoctorLifeLogger<MediaDecoder> {
 public:
  typedef MozPromise<bool /* aIgnored */, bool /* aIgnored */,
                     /* IsExclusive = */ true>
      SeekPromise;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaDecoder)

  // Enumeration for the valid play states (see mPlayState)
  MOZ_DEFINE_ENUM_WITH_TOSTRING_AT_CLASS_SCOPE(
      PlayState, (PLAY_STATE_LOADING, PLAY_STATE_PAUSED, PLAY_STATE_PLAYING,
                  PLAY_STATE_ENDED, PLAY_STATE_SHUTDOWN));

  // Must be called exactly once, on the main thread, during startup.
  static void InitStatics();

  explicit MediaDecoder(MediaDecoderInit& aInit);

  // Returns the container content type of the resource.
  // Safe to call from any thread.
  const MediaContainerType& ContainerType() const { return mContainerType; }

  // Cleanup internal data structures. Must be called on the main
  // thread by the owning object before that object disposes of this object.
  virtual void Shutdown();

  // Notified by the shutdown manager that XPCOM shutdown has begun.
  // The decoder should notify its owner to drop the reference to the decoder
  // to prevent further calls into the decoder.
  void NotifyXPCOMShutdown();

  // Called if the media file encounters a network error.
  void NetworkError(const MediaResult& aError);

  // Return the principal of the current URI being played or downloaded.
  virtual already_AddRefed<nsIPrincipal> GetCurrentPrincipal() = 0;

  // Return true if the loading of this resource required cross-origin
  // redirects.
  virtual bool HadCrossOriginRedirects() = 0;

  // Return the time position in the video stream being
  // played measured in seconds.
  virtual double GetCurrentTime();

  // Seek to the time position in (seconds) from the start of the video.
  // If aDoFastSeek is true, we'll seek to the sync point/keyframe preceeding
  // the seek target.
  void Seek(double aTime, SeekTarget::Type aSeekType);

  // Start playback of a video. 'Load' must have previously been
  // called.
  virtual void Play();

  // Notify activity of the decoder owner is changed.
  void NotifyOwnerActivityChanged(bool aIsOwnerInvisible,
                                  bool aIsOwnerConnected,
                                  bool aIsOwnerInBackground,
                                  bool aHasOwnerPendingCallbacks);

  // Pause video playback.
  virtual void Pause();
  // Adjust the speed of the playback, optionally with pitch correction,
  void SetVolume(double aVolume);

  void SetPlaybackRate(double aPlaybackRate);
  void SetPreservesPitch(bool aPreservesPitch);
  void SetLooping(bool aLooping);
  void SetStreamName(const nsAutoString& aStreamName);

  // Set the given device as the output device.
  RefPtr<GenericPromise> SetSink(AudioDeviceInfo* aSinkDevice);

  bool GetMinimizePreroll() const { return mMinimizePreroll; }

  // When we enable delay seek mode, media decoder won't actually ask MDSM to do
  // seeking. During this period, we would store the latest seeking target and
  // perform the seek to that target when we leave the mode. If we have any
  // delayed seeks stored `IsSeeking()` will return true. E.g. During delay
  // seeking mode, if we get seek target to 5s, 10s, 7s. When we stop delaying
  // seeking, we would only seek to 7s.
  void SetDelaySeekMode(bool aShouldDelaySeek);

  // All MediaStream-related data is protected by mReentrantMonitor.
  // We have at most one DecodedStreamData per MediaDecoder. Its stream
  // is used as the input for each ProcessedMediaTrack created by calls to
  // captureStream(UntilEnded). Seeking creates a new source stream, as does
  // replaying after the input as ended. In the latter case, the new source is
  // not connected to streams created by captureStreamUntilEnded.

  MOZ_DEFINE_ENUM_CLASS_WITH_TOSTRING_AT_CLASS_SCOPE(OutputCaptureState,
                                                     (Capture, Halt, None));

  // Set the output capture state of this decoder.
  // @param aState Capture: Output is captured into output tracks, and
  //                        aDummyTrack must be provided.
  //               Halt:    A capturing media sink is used, but capture is
  //                        halted.
  //               None:    Output is not captured.
  // @param aDummyTrack A SharedDummyTrack the capturing media sink can use to
  //                    access a MediaTrackGraph, so it can create tracks even
  //                    when there are no output tracks available.
  void SetOutputCaptureState(OutputCaptureState aState,
                             SharedDummyTrack* aDummyTrack = nullptr);
  // Add an output track. All decoder output for the track's media type will be
  // sent to the track.
  // Note that only one audio track and one video track is supported by
  // MediaDecoder at this time. Passing in more of one type, or passing in a
  // type that metadata says we are not decoding, is an error.
  void AddOutputTrack(RefPtr<ProcessedMediaTrack> aTrack);
  // Remove an output track added with AddOutputTrack.
  void RemoveOutputTrack(const RefPtr<ProcessedMediaTrack>& aTrack);
  // Update the principal for any output tracks.
  void SetOutputTracksPrincipal(const RefPtr<nsIPrincipal>& aPrincipal);

  // Return the duration of the video in seconds.
  virtual double GetDuration();

  // Return true if the stream is infinite.
  bool IsInfinite() const;

  // Return true if we are currently seeking in the media resource.
  // Call on the main thread only.
  bool IsSeeking() const;

  // Return true if the decoder has reached the end of playback.
  bool IsEnded() const;

  // True if we are playing a MediaSource object.
  virtual bool IsMSE() const { return false; }

  // Return true if the MediaDecoderOwner's error attribute is not null.
  // Must be called before Shutdown().
  bool OwnerHasError() const;

  // Returns true if this media supports random seeking. False for example with
  // chained ogg files.
  bool IsMediaSeekable();
  // Returns true if seeking is supported on a transport level (e.g. the server
  // supports range requests, we are playing a file, etc.).
  virtual bool IsTransportSeekable() = 0;

  // Return the time ranges that can be seeked into, in TimeUnits.
  virtual media::TimeIntervals GetSeekable();
  // Return the time ranges that can be seeked into, in seconds, double
  // precision.
  virtual media::TimeRanges GetSeekableTimeRanges();

  template <typename T>
  T GetSeekableImpl();

  // Set the end time of the media resource. When playback reaches
  // this point the media pauses. aTime is in seconds.
  virtual void SetFragmentEndTime(double aTime);

  // Invalidate the frame.
  void Invalidate();
  void InvalidateWithFlags(uint32_t aFlags);

  // Suspend any media downloads that are in progress. Called by the
  // media element when it is sent to the bfcache, or when we need
  // to throttle the download. Call on the main thread only. This can
  // be called multiple times, there's an internal "suspend count".
  // When it is called the internal system audio resource are cleaned up.
  virtual void Suspend();

  // Resume any media downloads that have been suspended. Called by the
  // media element when it is restored from the bfcache, or when we need
  // to stop throttling the download. Call on the main thread only.
  // The download will only actually resume once as many Resume calls
  // have been made as Suspend calls.
  virtual void Resume();

  // Moves any existing channel loads into or out of background. Background
  // loads don't block the load event. This is called when we stop or restart
  // delaying the load event. This also determines whether any new loads
  // initiated (for example to seek) will be in the background.  This calls
  // SetLoadInBackground() on mResource.
  virtual void SetLoadInBackground(bool aLoadInBackground) {}

  MediaDecoderStateMachineBase* GetStateMachine() const;
  void SetStateMachine(MediaDecoderStateMachineBase* aStateMachine);

  // Constructs the time ranges representing what segments of the media
  // are buffered and playable.
  virtual media::TimeIntervals GetBuffered();

  // Returns the size, in bytes, of the heap memory used by the currently
  // queued decoded video and audio data.
  size_t SizeOfVideoQueue();
  size_t SizeOfAudioQueue();

  // Helper struct for accumulating resource sizes that need to be measured
  // asynchronously. Once all references are dropped the callback will be
  // invoked.
  struct ResourceSizes {
    typedef MozPromise<size_t, size_t, true> SizeOfPromise;
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ResourceSizes)
    explicit ResourceSizes(MallocSizeOf aMallocSizeOf)
        : mMallocSizeOf(aMallocSizeOf), mByteSize(0), mCallback() {}

    mozilla::MallocSizeOf mMallocSizeOf;
    mozilla::Atomic<size_t> mByteSize;

    RefPtr<SizeOfPromise> Promise() { return mCallback.Ensure(__func__); }

   private:
    ~ResourceSizes() { mCallback.ResolveIfExists(mByteSize, __func__); }

    MozPromiseHolder<SizeOfPromise> mCallback;
  };

  virtual void AddSizeOfResources(ResourceSizes* aSizes) = 0;

  VideoFrameContainer* GetVideoFrameContainer() { return mVideoFrameContainer; }

  layers::ImageContainer* GetImageContainer();

  // Returns true if we can play the entire media through without stopping
  // to buffer, given the current download and playback rates.
  bool CanPlayThrough();

  // Called from HTMLMediaElement when owner document activity changes
  void SetElementVisibility(bool aIsOwnerInvisible, bool aIsOwnerConnected,
                            bool aIsOwnerInBackground,
                            bool aHasOwnerPendingCallbacks);

  // Force override the visible state to hidden.
  // Called from HTMLMediaElement when testing of video decode suspend from
  // mochitests.
  void SetForcedHidden(bool aForcedHidden);

  // Mark the decoder as tainted, meaning suspend-video-decoder is disabled.
  void SetSuspendTaint(bool aTaint);

  // Returns true if the decoder can't participate in suspend-video-decoder.
  bool HasSuspendTaint() const;

  void UpdateVideoDecodeMode();

  void SetSecondaryVideoContainer(
      const RefPtr<VideoFrameContainer>& aSecondaryVideoContainer);

  void SetIsBackgroundVideoDecodingAllowed(bool aAllowed);

  bool IsVideoDecodingSuspended() const;

  // The MediaDecoderOwner of this decoder wants to resist fingerprinting.
  bool ShouldResistFingerprinting() const {
    return mShouldResistFingerprinting;
  }

  /******
   * The following methods must only be called on the main
   * thread.
   ******/

  // Change to a new play state. This updates the mState variable and
  // notifies any thread blocking on this object's monitor of the
  // change. Call on the main thread only.
  virtual void ChangeState(PlayState aState);

  // Called when the video has completed playing.
  // Call on the main thread only.
  void PlaybackEnded();

  void OnSeekRejected();
  void OnSeekResolved();

  // Seeking has started. Inform the element on the main thread.
  void SeekingStarted();

  void UpdateLogicalPositionInternal();
  void UpdateLogicalPosition() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
    // Per spec, offical position remains stable during pause and seek.
    if (mPlayState == PLAY_STATE_PAUSED || IsSeeking()) {
      return;
    }
    UpdateLogicalPositionInternal();
  }

  // Find the end of the cached data starting at the current decoder
  // position.
  int64_t GetDownloadPosition();

  // Notifies the element that decoding has failed.
  void DecodeError(const MediaResult& aError);

  // Indicate whether the media is same-origin with the element.
  void UpdateSameOriginStatus(bool aSameOrigin);

  MediaDecoderOwner* GetOwner() const;

  AbstractThread* AbstractMainThread() const { return mAbstractMainThread; }

  RefPtr<SetCDMPromise> SetCDMProxy(CDMProxy* aProxy);

  void EnsureTelemetryReported();

  static bool IsOggEnabled();
  static bool IsOpusEnabled();
  static bool IsWaveEnabled();
  static bool IsWebMEnabled();

  // Return the frame decode/paint related statistics.
  FrameStatistics& GetFrameStatistics() { return *mFrameStats; }

  void UpdateReadyState() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
    GetOwner()->UpdateReadyState();
  }

  MediaDecoderOwner::NextFrameStatus NextFrameStatus() const {
    return mNextFrameStatus;
  }

  virtual MediaDecoderOwner::NextFrameStatus NextFrameBufferedStatus();

  RefPtr<GenericPromise> RequestDebugInfo(dom::MediaDecoderDebugInfo& aInfo);

  void GetDebugInfo(dom::MediaDecoderDebugInfo& aInfo);

  virtual bool IsHLSDecoder() const { return false; }

 protected:
  virtual ~MediaDecoder();

  // Called when the first audio and/or video from the media file has been
  // loaded by the state machine. Call on the main thread only.
  virtual void FirstFrameLoaded(UniquePtr<MediaInfo> aInfo,
                                MediaDecoderEventVisibility aEventVisibility);

  // Return error if fail to init the state machine.
  nsresult CreateAndInitStateMachine(bool aIsLiveStream,
                                     bool aDisableExternalEngine = false);

  // Always return a state machine. If the decoder supports using external
  // engine, `aDisableExternalEngine` can disable the external engine if needed.
  virtual MediaDecoderStateMachineBase* CreateStateMachine(
      bool aDisableExternalEngine) MOZ_NONNULL_RETURN = 0;

  void SetStateMachineParameters();

  // Disconnect any events before shutting down the state machine.
  void DisconnectEvents();
  RefPtr<ShutdownPromise> ShutdownStateMachine();

  // Called when MediaDecoder shutdown is finished. Subclasses use this to clean
  // up internal structures, and unregister potential shutdown blockers when
  // they're done.
  virtual void ShutdownInternal();

  bool IsShutdown() const;

  // Called to notify the decoder that the duration has changed.
  virtual void DurationChanged();

  // State-watching manager.
  WatchManager<MediaDecoder> mWatchManager;

  double ExplicitDuration() { return mExplicitDuration.ref(); }

  void SetExplicitDuration(double aValue) {
    MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
    mExplicitDuration = Some(aValue);

    // We Invoke DurationChanged explicitly, rather than using a watcher, so
    // that it takes effect immediately, rather than at the end of the current
    // task.
    DurationChanged();
  }

  virtual void OnPlaybackEvent(MediaPlaybackEvent&& aEvent);

  // Called when the metadata from the media file has been loaded by the
  // state machine. Call on the main thread only.
  virtual void MetadataLoaded(UniquePtr<MediaInfo> aInfo,
                              UniquePtr<MetadataTags> aTags,
                              MediaDecoderEventVisibility aEventVisibility);

  void SetLogicalPosition(const media::TimeUnit& aNewPosition);

  /******
   * The following members should be accessed with the decoder lock held.
   ******/

  // The logical playback position of the media resource in units of
  // seconds. This corresponds to the "official position" in HTML5. Note that
  // we need to store this as a double, rather than an int64_t (like
  // mCurrentPosition), so that |v.currentTime = foo; v.currentTime == foo|
  // returns true without being affected by rounding errors.
  double mLogicalPosition;

  // The current playback position of the underlying playback infrastructure.
  // This corresponds to the "current position" in HTML5.
  // We allow omx subclasses to substitute an alternative current position for
  // usage with the audio offload player.
  virtual media::TimeUnit CurrentPosition() { return mCurrentPosition.Ref(); }

  already_AddRefed<layers::KnowsCompositor> GetCompositor();

  // Official duration of the media resource as observed by script.
  // This can be a TimeUnit representing the exact duration found by demuxing,
  // as a TimeUnit. This can also be a duration set explicitly by script, as a
  // double.
  Variant<media::TimeUnit, double> mDuration;

#  ifdef MOZ_WMF_MEDIA_ENGINE
  // True when we need to update the newly created MDSM's status to make it
  // consistent with the previous destroyed one.
  bool mPendingStatusUpdateForNewlyCreatedStateMachine = false;
  void SetStatusUpdateForNewlyCreatedStateMachineIfNeeded();
#  endif

  /******
   * The following member variables can be accessed from any thread.
   ******/

  RefPtr<MediaFormatReader> mReader;

  // Amount of buffered data ahead of current time required to consider that
  // the next frame is available.
  // An arbitrary value of 250ms is used.
  static constexpr auto DEFAULT_NEXT_FRAME_AVAILABLE_BUFFERED =
      media::TimeUnit::FromMicroseconds(250000);

 private:
  // Called when the owner's activity changed.
  void NotifyCompositor();

  void OnPlaybackErrorEvent(const MediaResult& aError);

  void OnDecoderDoctorEvent(DecoderDoctorEvent aEvent);

  void OnMediaNotSeekable() { mMediaSeekable = false; }

  void OnNextFrameStatus(MediaDecoderOwner::NextFrameStatus);

  void OnTrackInfoUpdated(const VideoInfo& aVideoInfo,
                          const AudioInfo& aAudioInfo);

  void OnSecondaryVideoContainerInstalled(
      const RefPtr<VideoFrameContainer>& aSecondaryVideoContainer);

  void FinishShutdown();

  void ConnectMirrors(MediaDecoderStateMachineBase* aObject);
  void DisconnectMirrors();
#  ifdef MOZ_WMF_MEDIA_ENGINE
  void SwitchStateMachine(const MediaResult& aError);
#  endif

  virtual bool CanPlayThroughImpl() = 0;

  // The state machine object for handling the decoding. It is safe to
  // call methods of this object from other threads. Its internal data
  // is synchronised on a monitor. The lifetime of this object is
  // after mPlayState is LOADING and before mPlayState is SHUTDOWN. It
  // is safe to access it during this period.
  //
  // Explicitly prievate to force access via accessors.
  RefPtr<MediaDecoderStateMachineBase> mDecoderStateMachine;

 protected:
  void NotifyReaderDataArrived();
  void DiscardOngoingSeekIfExists();
  void CallSeek(const SeekTarget& aTarget);

  // Called by MediaResource when the principal of the resource has
  // changed. Called on main thread only.
  virtual void NotifyPrincipalChanged();

  MozPromiseRequestHolder<SeekPromise> mSeekRequest;

  void OnMetadataUpdate(TimedMetadata&& aMetadata);

  // This should only ever be accessed from the main thread.
  // It is set in the constructor and cleared in Shutdown when the element goes
  // away. The decoder does not add a reference the element.
  MediaDecoderOwner* mOwner;

  // The AbstractThread from mOwner.
  const RefPtr<AbstractThread> mAbstractMainThread;

  // Counters related to decode and presentation of frames.
  const RefPtr<FrameStatistics> mFrameStats;

  RefPtr<VideoFrameContainer> mVideoFrameContainer;

  // True if the decoder has been directed to minimize its preroll before
  // playback starts. After the first time playback starts, we don't attempt
  // to minimize preroll, as we assume the user is likely to keep playing,
  // or play the media again.
  const bool mMinimizePreroll;

  // True if we've already fired metadataloaded.
  bool mFiredMetadataLoaded;

  // True if the media is seekable (i.e. supports random access).
  bool mMediaSeekable = true;

  // True if the media is only seekable within its buffered ranges
  // like WebMs with no cues.
  bool mMediaSeekableOnlyInBufferedRanges = false;

  // Stores media info, including info of audio tracks and video tracks, should
  // only be accessed from main thread.
  UniquePtr<MediaInfo> mInfo;

  // True if the owner element is actually visible to users.
  bool mIsOwnerInvisible;

  // True if the owner element is connected to a document tree.
  // https://dom.spec.whatwg.org/#connected
  bool mIsOwnerConnected;

  // True if the owner element is in a backgrounded tab/window.
  bool mIsOwnerInBackground;

  // True if the owner element has pending rVFC callbacks.
  bool mHasOwnerPendingCallbacks;

  // If true, forces the decoder to be considered hidden.
  bool mForcedHidden;

  // True if the decoder has a suspend taint - meaning suspend-video-decoder is
  // disabled.
  bool mHasSuspendTaint;

  // If true, the decoder should resist fingerprinting.
  const bool mShouldResistFingerprinting;

  MediaDecoderOwner::NextFrameStatus mNextFrameStatus =
      MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE;

  // A listener to receive metadata updates from MDSM.
  MediaEventListener mTimedMetadataListener;

  MediaEventListener mMetadataLoadedListener;
  MediaEventListener mFirstFrameLoadedListener;

  MediaEventListener mOnPlaybackEvent;
  MediaEventListener mOnPlaybackErrorEvent;
  MediaEventListener mOnDecoderDoctorEvent;
  MediaEventListener mOnMediaNotSeekable;
  MediaEventListener mOnEncrypted;
  MediaEventListener mOnWaitingForKey;
  MediaEventListener mOnDecodeWarning;
  MediaEventListener mOnNextFrameStatus;
  MediaEventListener mOnTrackInfoUpdated;
  MediaEventListener mOnSecondaryVideoContainerInstalled;

  // True if we have suspended video decoding.
  bool mIsVideoDecodingSuspended = false;

 protected:
  // PlaybackRate and pitch preservation status we should start at.
  double mPlaybackRate;

  // True if the decoder is seeking.
  Watchable<bool> mLogicallySeeking;

  // Buffered range, mirrored from the reader.
  Mirror<media::TimeIntervals> mBuffered;

  // NB: Don't use mCurrentPosition directly, but rather CurrentPosition().
  Mirror<media::TimeUnit> mCurrentPosition;

  // Duration of the media resource according to the state machine.
  Mirror<media::NullableTimeUnit> mStateMachineDuration;

  // Used to distinguish whether the audio is producing sound.
  Mirror<bool> mIsAudioDataAudible;

  // Volume of playback.  0.0 = muted. 1.0 = full volume.
  Canonical<double> mVolume;

  Canonical<bool> mPreservesPitch;

  Canonical<bool> mLooping;

  Canonical<nsAutoString> mStreamName;

  // The device used with SetSink, or nullptr if no explicit device has been
  // set.
  Canonical<RefPtr<AudioDeviceInfo>> mSinkDevice;

  // Set if the decoder is sending video to a secondary container. While set we
  // should not suspend the decoder.
  Canonical<RefPtr<VideoFrameContainer>> mSecondaryVideoContainer;

  // Whether this MediaDecoder's output is captured, halted or not captured.
  // When captured, all decoded data must be played out through mOutputTracks.
  Canonical<OutputCaptureState> mOutputCaptureState;

  // A dummy track used to access the right MediaTrackGraph instance. Needed
  // since there's no guarantee that output tracks are present.
  Canonical<nsMainThreadPtrHandle<SharedDummyTrack>> mOutputDummyTrack;

  // Tracks that, if set, will get data routed through them.
  Canonical<CopyableTArray<RefPtr<ProcessedMediaTrack>>> mOutputTracks;

  // PrincipalHandle to be used when feeding data into mOutputTracks.
  Canonical<PrincipalHandle> mOutputPrincipal;

  // Media duration set explicitly by JS. At present, this is only ever present
  // for MSE.
  Maybe<double> mExplicitDuration;

  // Set to one of the valid play states.
  // This can only be changed on the main thread while holding the decoder
  // monitor. Thus, it can be safely read while holding the decoder monitor
  // OR on the main thread.
  Canonical<PlayState> mPlayState;

  // This can only be changed on the main thread.
  PlayState mNextState = PLAY_STATE_PAUSED;

  // True if the media is same-origin with the element. Data can only be
  // passed to MediaStreams when this is true.
  bool mSameOriginMedia;

  // We can allow video decoding in background when we match some special
  // conditions, eg. when the cursor is hovering over the tab. This observer is
  // used to listen the related events.
  RefPtr<BackgroundVideoDecodingPermissionObserver> mVideoDecodingOberver;

  // True if we want to resume video decoding even the media element is in the
  // background.
  bool mIsBackgroundVideoDecodingAllowed;

  // True if we want to delay seeking, and and save the latest seeking target to
  // resume to when we stop delaying seeking.
  bool mShouldDelaySeek = false;
  Maybe<SeekTarget> mDelayedSeekTarget;

 public:
  Canonical<double>& CanonicalVolume() { return mVolume; }
  Canonical<bool>& CanonicalPreservesPitch() { return mPreservesPitch; }
  Canonical<bool>& CanonicalLooping() { return mLooping; }
  Canonical<nsAutoString>& CanonicalStreamName() { return mStreamName; }
  Canonical<RefPtr<AudioDeviceInfo>>& CanonicalSinkDevice() {
    return mSinkDevice;
  }
  Canonical<RefPtr<VideoFrameContainer>>& CanonicalSecondaryVideoContainer() {
    return mSecondaryVideoContainer;
  }
  Canonical<OutputCaptureState>& CanonicalOutputCaptureState() {
    return mOutputCaptureState;
  }
  Canonical<nsMainThreadPtrHandle<SharedDummyTrack>>&
  CanonicalOutputDummyTrack() {
    return mOutputDummyTrack;
  }
  Canonical<CopyableTArray<RefPtr<ProcessedMediaTrack>>>&
  CanonicalOutputTracks() {
    return mOutputTracks;
  }
  Canonical<PrincipalHandle>& CanonicalOutputPrincipal() {
    return mOutputPrincipal;
  }
  Canonical<PlayState>& CanonicalPlayState() { return mPlayState; }

  void UpdateTelemetryHelperBasedOnPlayState(PlayState aState) const;

  TelemetryProbesReporter::Visibility OwnerVisibility() const;

  // Those methods exist to report telemetry related metrics.
  double GetTotalVideoPlayTimeInSeconds() const;
  double GetTotalVideoHDRPlayTimeInSeconds() const;
  double GetVisibleVideoPlayTimeInSeconds() const;
  double GetInvisibleVideoPlayTimeInSeconds() const;
  double GetTotalAudioPlayTimeInSeconds() const;
  double GetAudiblePlayTimeInSeconds() const;
  double GetInaudiblePlayTimeInSeconds() const;
  double GetMutedPlayTimeInSeconds() const;

 private:
  /**
   * This enum describes the reason why we need to update the logical position.
   * ePeriodicUpdate : the position grows periodically during playback
   * eSeamlessLoopingSeeking : the position changes due to demuxer level seek.
   * eOther : due to normal seeking or other attributes changes, eg. playstate
   */
  enum class PositionUpdate {
    ePeriodicUpdate,
    eSeamlessLoopingSeeking,
    eOther,
  };
  PositionUpdate GetPositionUpdateReason(double aPrevPos,
                                         const media::TimeUnit& aCurPos) const;

  // Notify owner when the audible state changed
  void NotifyAudibleStateChanged();

  void NotifyVolumeChanged();

  bool mTelemetryReported;
  const MediaContainerType mContainerType;
  bool mCanPlayThrough = false;

  UniquePtr<TelemetryProbesReporter> mTelemetryProbesReporter;

  // The time of creating the media decoder state machine, it's used to record
  // the probe for measuring the first video frame loaded time. Reset after
  // reporting the measurement to avoid a dulpicated report.
  Maybe<TimeStamp> mMDSMCreationTime;
};

}  // namespace mozilla

#endif
