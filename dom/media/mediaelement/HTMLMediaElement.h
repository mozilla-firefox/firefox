/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef mozilla_dom_HTMLMediaElement_h
#define mozilla_dom_HTMLMediaElement_h

#include <utility>

#include "AudioChannelService.h"
#include "DecoderTraits.h"
#include "MediaDecoderOwner.h"
#include "MediaElementEventRunners.h"
#include "MediaEventSource.h"
#include "MediaPlaybackDelayPolicy.h"
#include "MediaPromiseDefs.h"
#include "MediaSegment.h"  // for PrincipalHandle, GraphTime
#include "MediaTimer.h"
#include "PrincipalChangeObserver.h"
#include "SeekTarget.h"
#include "TelemetryProbesReporter.h"
#include "Visibility.h"
#include "mozilla/Attributes.h"
#include "mozilla/AwakeTimeStamp.h"
#include "mozilla/CORSMode.h"
#include "mozilla/StateWatching.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/DecoderDoctorNotificationBinding.h"
#include "mozilla/dom/HTMLMediaElementBinding.h"
#include "mozilla/dom/MediaDebugInfoBinding.h"
#include "mozilla/dom/MediaKeys.h"
#include "mozilla/dom/TextTrackManager.h"
#include "nsCycleCollectionParticipant.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsStubMutationObserver.h"

// X.h on Linux #defines CurrentTime as 0L, so we have to #undef it here.
#ifdef CurrentTime
#  undef CurrentTime
#endif

// Define to output information on decoding and painting framerate
/* #define DEBUG_FRAME_RATE 1 */

using nsMediaNetworkState = uint16_t;
using nsMediaReadyState = uint16_t;
using SuspendTypes = uint32_t;
using AudibleChangedReasons = uint32_t;

class nsIStreamListener;

namespace mozilla {
class AbstractThread;
class ChannelMediaDecoder;
class DecoderDoctorDiagnostics;
class DOMMediaStream;
class ErrorResult;
class FirstFrameVideoOutput;
class MediaResource;
class MediaDecoder;
class MediaInputPort;
class MediaTrack;
class MediaTrackGraph;
class MediaStreamWindowCapturer;
struct SharedDummyTrack;
class VideoFrameContainer;
class VideoOutput;
namespace dom {
class HTMLSourceElement;
class MediaKeys;
class TextTrack;
class TimeRanges;
class WakeLock;
class MediaStreamTrack;
class MediaStreamTrackSource;
class MediaTrack;
class VideoStreamTrack;
}  // namespace dom
}  // namespace mozilla

class AudioDeviceInfo;
class nsIChannel;
class nsIHttpChannel;
class nsILoadGroup;
class nsIRunnable;
class nsISerialEventTarget;
class nsITimer;

namespace mozilla::dom {

// Number of milliseconds between timeupdate events as defined by spec
#define TIMEUPDATE_MS 250

class HTMLVideoElement;
class MediaError;
class MediaSource;
class PlayPromise;
class Promise;
class TextTrackList;
class AudioTrackList;
class VideoTrackList;

enum class StreamCaptureType : uint8_t { CAPTURE_ALL_TRACKS, CAPTURE_AUDIO };

enum class StreamCaptureBehavior : uint8_t {
  CONTINUE_WHEN_ENDED,
  FINISH_WHEN_ENDED
};

// `NotNeeded` means audio is routed through WebAudio (audio output is
// configured by WebAudio), or audio output configuration is not required.
enum class AudioOutputConfig : bool { NotNeeded = false, Needed = true };

/**
 * Possible values of the 'preload' attribute.
 */
enum MediaPreloadAttrValue : uint8_t {
  PRELOAD_ATTR_NONE,      // set to "none"
  PRELOAD_ATTR_METADATA,  // set to "metadata"
  PRELOAD_ATTR_AUTO       // set to "auto"
};

// Mappings from 'preload' attribute strings to an enumeration.
static const nsAttrValue::EnumTableEntry kPreloadTable[] = {
    {"none", MediaPreloadAttrValue::PRELOAD_ATTR_NONE},
    {"metadata", MediaPreloadAttrValue::PRELOAD_ATTR_METADATA},
    {"auto", MediaPreloadAttrValue::PRELOAD_ATTR_AUTO},
};

static constexpr const nsAttrValue::EnumTableEntry* kPreloadDefaultType =
    &kPreloadTable[std::size(kPreloadTable) - 1];

class HTMLMediaElement : public nsGenericHTMLElement,
                         public MediaDecoderOwner,
                         public PrincipalChangeObserver<MediaStreamTrack>,
                         public SupportsWeakPtr,
                         public nsStubMutationObserver,
                         public TelemetryProbesReporterOwner {
 public:
  using TimeStamp = mozilla::TimeStamp;
  using ImageContainer = mozilla::layers::ImageContainer;
  using VideoFrameContainer = mozilla::VideoFrameContainer;
  using MediaResource = mozilla::MediaResource;
  using MediaDecoderOwner = mozilla::MediaDecoderOwner;
  using MetadataTags = mozilla::MetadataTags;

  // Helper struct to keep track of the MediaStreams returned by
  // mozCaptureStream(). For each OutputMediaStream, dom::MediaTracks get
  // captured into MediaStreamTracks which get added to
  // OutputMediaStream::mStream.
  struct OutputMediaStream {
    OutputMediaStream(RefPtr<DOMMediaStream> aStream, bool aCapturingAudioOnly,
                      bool aFinishWhenEnded);
    ~OutputMediaStream();

    RefPtr<DOMMediaStream> mStream;
    nsTArray<RefPtr<MediaStreamTrack>> mLiveTracks;
    const bool mCapturingAudioOnly;
    const bool mFinishWhenEnded;
    // If mFinishWhenEnded is true, this is the URI of the first resource
    // mStream got tracks for.
    nsCOMPtr<nsIURI> mFinishWhenEndedLoadingSrc;
    // If mFinishWhenEnded is true, this is the first MediaStream mStream got
    // tracks for.
    RefPtr<DOMMediaStream> mFinishWhenEndedAttrStream;
    // If mFinishWhenEnded is true, this is the MediaSource being played.
    RefPtr<MediaSource> mFinishWhenEndedMediaSource;
  };

  NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED

  CORSMode GetCORSMode() { return mCORSMode; }

  explicit HTMLMediaElement(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);
  void Init();

  virtual HTMLVideoElement* AsHTMLVideoElement() { return nullptr; };

  // `eMandatory`: `timeupdate` occurs according to the spec requirement.
  // Eg.
  // https://html.spec.whatwg.org/multipage/media.html#seeking:event-media-timeupdate
  // `ePeriodic` : `timeupdate` occurs regularly and should follow the rule
  // of not dispatching that event within 250 ms. Eg.
  // https://html.spec.whatwg.org/multipage/media.html#offsets-into-the-media-resource:event-media-timeupdate
  enum class TimeupdateType : bool {
    eMandatory = false,
    ePeriodic = true,
  };

  // This is used for event runner creation. Currently only timeupdate needs
  // that, but it can be used to extend for other events in the future if
  // necessary.
  enum class EventFlag : uint8_t {
    eNone = 0,
    eMandatory = 1,
  };

  /**
   * This is used when the browser is constructing a video element to play
   * a channel that we've already started loading. The src attribute and
   * <source> children are ignored.
   * @param aChannel the channel to use
   * @param aListener returns a stream listener that should receive
   * notifications for the stream
   */
  nsresult LoadWithChannel(nsIChannel* aChannel, nsIStreamListener** aListener);

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(HTMLMediaElement,
                                           nsGenericHTMLElement)
  NS_IMPL_FROMNODE_HELPER(HTMLMediaElement,
                          IsAnyOfHTMLElements(nsGkAtoms::video,
                                              nsGkAtoms::audio))

  NS_DECL_ADDSIZEOFEXCLUDINGTHIS

  // EventTarget
  void GetEventTargetParent(EventChainPreVisitor& aVisitor) override;

  void NodeInfoChanged(Document* aOldDoc) override;

  bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                      const nsAString& aValue,
                      nsIPrincipal* aMaybeScriptedPrincipal,
                      nsAttrValue& aResult) override;

  nsresult BindToTree(BindContext&, nsINode& aParent) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void UnbindFromTree(UnbindContext&) override;
  void DoneCreatingElement() override;

  bool IsHTMLFocusable(IsFocusableFlags, bool* aIsFocusable,
                       int32_t* aTabIndex) override;
  int32_t TabIndexDefault() override;

  // Called by the video decoder object, on the main thread,
  // when it has read the metadata containing video dimensions,
  // etc.
  void MetadataLoaded(const MediaInfo* aInfo,
                      UniquePtr<const MetadataTags> aTags) final;

  // Called by the decoder object, on the main thread,
  // when it has read the first frame of the video or audio.
  void FirstFrameLoaded() final;

  // Called by the video decoder object, on the main thread,
  // when the resource has a network error during loading.
  void NetworkError(const MediaResult& aError) final;

  // Called by the video decoder object, on the main thread, when the
  // resource has a decode error during metadata loading or decoding.
  void DecodeError(const MediaResult& aError) final;

  // Called by the decoder object, on the main thread, when the
  // resource has a decode issue during metadata loading or decoding, but can
  // continue decoding.
  void DecodeWarning(const MediaResult& aError) final;

  // Return true if error attribute is not null.
  bool HasError() const final;

  // Called by the video decoder object, on the main thread, when the
  // resource load has been cancelled.
  void LoadAborted() final;

  // Called by the video decoder object, on the main thread,
  // when the video playback has ended.
  void PlaybackEnded() final;

  // Called by the video decoder object, on the main thread,
  // when the resource has started seeking.
  void SeekStarted() final;

  // Called by the video decoder object, on the main thread,
  // when the resource has completed seeking.
  void SeekCompleted() final;

  // Called by the video decoder object, on the main thread, before a seek
  // operation to update the played time ranges.
  void UpdatePlayedRangesBeforeSeek(double aRangeEndTime) final;

  // Called by the video decoder object, on the main thread,
  // when the resource has aborted seeking.
  void SeekAborted() final;

  // Called by the media stream, on the main thread, when the download
  // has been suspended by the cache or because the element itself
  // asked the decoder to suspend the download.
  void DownloadSuspended() final;

  // Called by the media stream, on the main thread, when the download
  // has been resumed by the cache or because the element itself
  // asked the decoder to resumed the download.
  void DownloadResumed();

  // Called to indicate the download is progressing.
  void DownloadProgressed() final;

  // Called by the media decoder to indicate whether the media cache has
  // suspended the channel.
  void NotifySuspendedByCache(bool aSuspendedByCache) final;

  // Return true if the media element is actually invisible to users.
  bool IsActuallyInvisible() const override;

  // Return true if the element is in the view port.
  bool IsInViewPort() const;

  // Called by the media decoder and the video frame to get the
  // ImageContainer containing the video data.
  VideoFrameContainer* GetVideoFrameContainer() final;
  layers::ImageContainer* GetImageContainer();

  /**
   * Call this to reevaluate whether we should start/stop due to our owner
   * document being active, inactive, visible or hidden.
   */
  void NotifyOwnerDocumentActivityChanged();

  // Called when the media element enters or leaves the fullscreen.
  void NotifyFullScreenChanged();

  bool IsInFullScreen() const;

  // From PrincipalChangeObserver<MediaStreamTrack>.
  void PrincipalChanged(MediaStreamTrack* aTrack) override;

  void UpdateSrcStreamVideoPrincipal(const PrincipalHandle& aPrincipalHandle);

  // Called after the MediaStream we're playing rendered a frame to aContainer
  // with a different principalHandle than the previous frame.
  void PrincipalHandleChangedForVideoFrameContainer(
      VideoFrameContainer* aContainer,
      const PrincipalHandle& aNewPrincipalHandle) override;

  // Queue a media element task to fire an event targeted at the media element.
  void QueueEvent(const nsAString& aName) final;
  // Queue a media element task.
  // The task is blocked while the document is in B/F cache.
  void QueueTask(RefPtr<nsMediaEventRunner> aRunner);

  // Triggers a recomputation of readyState.
  void UpdateReadyState() override;

  // Dispatch events that were raised while in the bfcache
  nsresult DispatchPendingMediaEvents();

  // Return true if we can activate autoplay assuming enough data has arrived.
  // https://html.spec.whatwg.org/multipage/media.html#eligible-for-autoplay
  bool IsEligibleForAutoplay();

  // Notify that state has changed that might cause an autoplay element to
  // start playing.
  // If the element is 'autoplay' and is ready to play back (not paused,
  // autoplay pref enabled, etc), it should start playing back.
  void CheckAutoplayDataReady();

  void RunAutoplay();

  // Start listening for async GV autoplay permission request results.
  void StartObservingGVAutoplayIfNeeded();

  // Stops listening for async GV autoplay permissions if observer exists.
  void StopObservingGVAutoplayIfNeeded();

  bool ShouldDelayPlayUntilGVAutoplayRequestResolved() const;

  // Check if the media element had crossorigin set when loading started
  bool ShouldCheckAllowOrigin();

  // Returns true if the currently loaded resource is CORS same-origin with
  // respect to the document.
  bool IsCORSSameOrigin();

  // Is the media element potentially playing as defined by the HTML 5
  // specification.
  // http://www.whatwg.org/specs/web-apps/current-work/#potentially-playing
  bool IsPotentiallyPlaying() const;

  // Has playback ended as defined by the HTML 5 specification.
  // http://www.whatwg.org/specs/web-apps/current-work/#ended
  bool IsPlaybackEnded() const;

  // principal of the currently playing resource. Anything accessing the
  // contents of this element must have a principal that subsumes this
  // principal. Returns null if nothing is playing.
  already_AddRefed<nsIPrincipal> GetCurrentPrincipal();

  // Return true if the loading of this resource required cross-origin
  // redirects.
  bool HadCrossOriginRedirects();

  bool ShouldResistFingerprinting(RFPTarget aTarget) const override;

  // Principal of the currently playing video resource. Anything accessing the
  // image container of this element must have a principal that subsumes this
  // principal. If there are no live video tracks but content has been rendered
  // to the image container, we return the last video principal we had. Should
  // the image container be empty with no live video tracks, we return nullptr.
  already_AddRefed<nsIPrincipal> GetCurrentVideoPrincipal();

  // called to notify that the principal of the decoder's media resource has
  // changed.
  void NotifyDecoderPrincipalChanged() final;

  void GetEMEInfo(dom::EMEDebugInfo& aInfo);

  // Update the visual size and rotation of the media. Called from the
  // decoder on the main thread when/if either changes.
  virtual void UpdateMediaSize(const nsIntSize& aSize, VideoRotation aRotation);

  void Invalidate(ImageSizeChanged aImageSizeChanged,
                  const Maybe<nsIntSize>& aNewIntrinsicSize,
                  const Maybe<VideoRotation>& aNewRotation,
                  ForceInvalidate aForceInvalidate) override;

  // Returns the CanPlayStatus indicating if we can handle the
  // full MIME type including the optional codecs parameter.
  static CanPlayStatus GetCanPlay(const nsAString& aType,
                                  DecoderDoctorDiagnostics* aDiagnostics);

  /**
   * Called when a child source element is added to this media element. This
   * may queue a task to run the select resource algorithm if appropriate.
   */
  void NotifyAddedSource();

  /**
   * Called when there's been an error fetching the resource. This decides
   * whether it's appropriate to fire an error event.
   */
  void NotifyLoadError(const nsACString& aErrorDetails = nsCString());

  /**
   * Called by one of our associated MediaTrackLists (audio/video) when a
   * MediaTrack is added.
   */
  void NotifyMediaTrackAdded(dom::MediaTrack* aTrack);

  /**
   * Called by one of our associated MediaTrackLists (audio/video) when a
   * MediaTrack is removed.
   */
  void NotifyMediaTrackRemoved(dom::MediaTrack* aTrack);

  /**
   * Called by one of our associated MediaTrackLists (audio/video) when an
   * AudioTrack is enabled or a VideoTrack is selected.
   */
  void NotifyMediaTrackEnabled(dom::MediaTrack* aTrack);

  /**
   * Called by one of our associated MediaTrackLists (audio/video) when an
   * AudioTrack is disabled or a VideoTrack is unselected.
   */
  void NotifyMediaTrackDisabled(dom::MediaTrack* aTrack);

  /**
   * Returns the current load ID. Asynchronous events store the ID that was
   * current when they were enqueued, and if it has changed when they come to
   * fire, they consider themselves cancelled, and don't fire.
   */
  uint32_t GetCurrentLoadID() const { return mCurrentLoadID; }

  /**
   * Returns the load group for this media element's owner document.
   * XXX XBL2 issue.
   */
  already_AddRefed<nsILoadGroup> GetDocumentLoadGroup();

  /**
   * Returns true if the media has played or completed a seek.
   * Used by video frame to determine whether to paint the poster.
   */
  bool GetPlayedOrSeeked() const { return mHasPlayedOrSeeked; }

  nsresult CopyInnerTo(Element* aDest);

  /**
   * Sets the Accept header on the HTTP channel to the required
   * video or audio MIME types.
   */
  virtual nsresult SetAcceptHeader(nsIHttpChannel* aChannel) = 0;

  /**
   * Sets the required request headers on the HTTP channel for
   * video or audio requests.
   */
  void SetRequestHeaders(nsIHttpChannel* aChannel);

  /**
   * Asynchronously awaits a stable state, whereupon aRunnable runs on the main
   * thread. This adds an event which run aRunnable to the appshell's list of
   * sections synchronous the next time control returns to the event loop.
   */
  void RunInStableState(nsIRunnable* aRunnable);

  /**
   * Fires a timeupdate event. If aPeriodic is true, the event will only
   * be fired if we've not fired a timeupdate event (for any reason) in the
   * last 250ms, as required by the spec when the current time is periodically
   * increasing during playback.
   */
  void FireTimeUpdate(TimeupdateType aType);

  void MaybeQueueTimeupdateEvent() final {
    FireTimeUpdate(TimeupdateType::ePeriodic);
  }

  const TimeStamp& LastTimeupdateDispatchTime() const;
  void UpdateLastTimeupdateDispatchTime();

  // WebIDL

  MediaError* GetError() const;

  void GetSrc(nsAString& aSrc) { GetURIAttr(nsGkAtoms::src, nullptr, aSrc); }
  void SetSrc(const nsAString& aSrc, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::src, aSrc, aError);
  }
  void SetSrc(const nsAString& aSrc, nsIPrincipal* aTriggeringPrincipal,
              ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::src, aSrc, aTriggeringPrincipal, aError);
  }

  void GetCurrentSrc(nsAString& aCurrentSrc);

  void GetCrossOrigin(nsAString& aResult) {
    // Null for both missing and invalid defaults is ok, since we
    // always parse to an enum value, so we don't need an invalid
    // default, and we _want_ the missing default to be null.
    GetEnumAttr(nsGkAtoms::crossorigin, nullptr, aResult);
  }
  void SetCrossOrigin(const nsAString& aCrossOrigin, ErrorResult& aError) {
    SetOrRemoveNullableStringAttr(nsGkAtoms::crossorigin, aCrossOrigin, aError);
  }

  uint16_t NetworkState() const { return mNetworkState; }

  void NotifyXPCOMShutdown() final;

  // Called by media decoder when the audible state changed or when input is
  // a media stream.
  void SetAudibleState(bool aAudible) final;

  // Notify agent when the MediaElement changes its audible state.
  void NotifyAudioPlaybackChanged(AudibleChangedReasons aReason);

  void GetPreload(nsAString& aValue) {
    if (mSrcAttrStream) {
      nsGkAtoms::none->ToString(aValue);
      return;
    }
    GetEnumAttr(nsGkAtoms::preload, kPreloadDefaultType->tag, aValue);
  }
  void SetPreload(const nsAString& aValue, ErrorResult& aRv) {
    if (mSrcAttrStream) {
      return;
    }
    SetHTMLAttr(nsGkAtoms::preload, aValue, aRv);
  }

  already_AddRefed<TimeRanges> Buffered() const;

  void Load();

  void CanPlayType(const nsAString& aType, nsAString& aResult);

  uint16_t ReadyState() const { return mReadyState; }

  bool Seeking() const;

  double CurrentTime() const;

  void SetCurrentTime(double aCurrentTime, ErrorResult& aRv);
  void SetCurrentTime(double aCurrentTime) {
    SetCurrentTime(aCurrentTime, IgnoreErrors());
  }

  void FastSeek(double aTime, ErrorResult& aRv);

  already_AddRefed<Promise> SeekToNextFrame(ErrorResult& aRv);

  double Duration() const;

  bool HasAudio() const { return mMediaInfo.HasAudio(); }

  virtual bool IsVideo() const { return false; }

  bool IsLazyLoading() const { return mLazyLoading; }

  enum class StartLoad : bool { No, Yes };

  // Starts observing the element for lazy media loading.
  void SetLazyLoading();

  // Stops lazy media loading and optionally starts the deferred load.
  void StopLazyLoading(StartLoad);

  void SetLoading(const nsAString& aLoading, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::loading, aLoading, aError);
  }

  bool HasVideo() const { return mMediaInfo.HasVideo(); }

  bool IsEncrypted() const override { return mIsEncrypted; }

#ifdef MOZ_WMF_CDM
  bool IsUsingWMFCDM() const override;

  CDMProxy* GetCDMProxy() const override;
#endif

  bool Paused() const { return mPaused; }

  double DefaultPlaybackRate() const {
    if (mSrcAttrStream) {
      return 1.0;
    }
    return mDefaultPlaybackRate;
  }

  void SetDefaultPlaybackRate(double aDefaultPlaybackRate, ErrorResult& aRv);

  double PlaybackRate() const {
    if (mSrcAttrStream) {
      return 1.0;
    }
    return mPlaybackRate;
  }

  void SetPlaybackRate(double aPlaybackRate, ErrorResult& aRv);

  already_AddRefed<TimeRanges> Played();

  already_AddRefed<TimeRanges> Seekable() const;

  bool Ended();

  bool Autoplay() const { return GetBoolAttr(nsGkAtoms::autoplay); }

  void SetAutoplay(bool aValue, ErrorResult& aRv) {
    SetHTMLBoolAttr(nsGkAtoms::autoplay, aValue, aRv);
  }

  bool Loop() const { return GetBoolAttr(nsGkAtoms::loop); }

  void SetLoop(bool aValue, ErrorResult& aRv) {
    SetHTMLBoolAttr(nsGkAtoms::loop, aValue, aRv);
  }

  already_AddRefed<Promise> Play(ErrorResult& aRv);
  void Play() {
    IgnoredErrorResult dummy;
    RefPtr<Promise> toBeIgnored = Play(dummy);
  }

  void Pause(ErrorResult& aRv);
  void Pause() { Pause(IgnoreErrors()); }

  bool Controls() const { return GetBoolAttr(nsGkAtoms::controls); }

  void SetControls(bool aValue, ErrorResult& aRv) {
    SetHTMLBoolAttr(nsGkAtoms::controls, aValue, aRv);
  }

  double Volume() const { return mVolume; }

  void SetVolume(double aVolume, ErrorResult& aRv);

  enum MutedReasons {
    MUTED_BY_CONTENT = 0x01,
    MUTED_BY_INVALID_PLAYBACK_RATE = 0x02,
    MUTED_BY_AUDIO_CHANNEL = 0x04,
    MUTED_BY_AUDIO_TRACK = 0x08,
    MUTED_BY_MEDIA_CONTROL = 0x10
  };

  bool Muted() const {
    // https://html.spec.whatwg.org/multipage/media.html#concept-media-muted
    return !!(mMuted & (MUTED_BY_CONTENT | MUTED_BY_INVALID_PLAYBACK_RATE));
  }
  void SetMuted(bool aMuted, MutedReasons aReason = MUTED_BY_CONTENT);

  // Chrome-only accessor exposing which reasons currently contribute to the
  // muted state, so tests can verify muting that does not affect the
  // web-visible muted attribute (e.g. mute via media control).
  uint32_t GetMutedReasons() const { return mMuted; }

  bool DefaultMuted() const { return GetBoolAttr(nsGkAtoms::muted); }

  void SetDefaultMuted(bool aMuted, ErrorResult& aRv) {
    SetHTMLBoolAttr(nsGkAtoms::muted, aMuted, aRv);
  }

  // Returns whether a call to Play() would be rejected with NotAllowedError.
  // This assumes "worst case" for unknowns. So if prompting for permission is
  // enabled and no permission is stored, this behaves as if the user would
  // opt to block.
  bool AllowedToPlay() const;

  already_AddRefed<MediaSource> GetMozMediaSourceObject() const;

  // Returns a promise which will be resolved after collecting debugging
  // data from decoder/reader/MDSM. Used for debugging purposes.
  already_AddRefed<Promise> MozRequestDebugInfo(ErrorResult& aRv);

  // Enables DecoderDoctorLogger logging. Used for debugging purposes.
  static void MozEnableDebugLog(const GlobalObject&);

  // Returns a promise which will be resolved after collecting debugging
  // log associated with this element. Used for debugging purposes.
  already_AddRefed<Promise> MozRequestDebugLog(ErrorResult& aRv);

  // For use by mochitests. Enabling pref "media.test.video-suspend"
  void SetVisible(bool aVisible);

  // For use by mochitests. Enabling pref "media.test.video-suspend"
  bool HasSuspendTaint() const;

  // For use by mochitests.
  bool IsVideoDecodingSuspended() const;

  // These functions return accumulated time, which are used for the telemetry
  // usage. Return -1 for error.
  double TotalVideoPlayTime() const;
  double TotalVideoHDRPlayTime() const;
  double VisiblePlayTime() const;
  double InvisiblePlayTime() const;
  double TotalAudioPlayTime() const;
  double AudiblePlayTime() const;
  double InaudiblePlayTime() const;
  double MutedPlayTime() const;

  // Test methods for decoder doctor.
  void SetFormatDiagnosticsReportForMimeType(const nsAString& aMimeType,
                                             DecoderDoctorReportType aType);
  void SetDecodeError(const nsAString& aError, ErrorResult& aRv);
  void SetAudioSinkFailedStartup();

  // Synchronously, return the next video frame and mark the element unable to
  // participate in decode suspending.
  //
  // This function is synchronous for cases where decoding has been suspended
  // and JS needs a frame to use in, eg., nsLayoutUtils::SurfaceFromElement()
  // via drawImage().
  already_AddRefed<layers::Image> GetCurrentImage();

  already_AddRefed<DOMMediaStream> GetSrcObject() const;
  void SetSrcObject(DOMMediaStream& aValue);
  void SetSrcObject(DOMMediaStream* aValue);

  bool PreservesPitch() const { return mPreservesPitch; }
  void SetPreservesPitch(bool aPreservesPitch);

  MediaKeys* GetMediaKeys() const;

  already_AddRefed<Promise> SetMediaKeys(MediaKeys* mediaKeys,
                                         ErrorResult& aRv);

  mozilla::dom::EventHandlerNonNull* GetOnencrypted();
  void SetOnencrypted(mozilla::dom::EventHandlerNonNull* aCallback);

  mozilla::dom::EventHandlerNonNull* GetOnwaitingforkey();
  void SetOnwaitingforkey(mozilla::dom::EventHandlerNonNull* aCallback);

  void DispatchEncrypted(const nsTArray<uint8_t>& aInitData,
                         const nsAString& aInitDataType) override;

  bool IsEventAttributeNameInternal(nsAtom* aName) override;

  bool ContainsRestrictedContent() const;

  void NotifyWaitingForKey() override;

  already_AddRefed<DOMMediaStream> CaptureAudio(ErrorResult& aRv,
                                                MediaTrackGraph* aGraph);

  already_AddRefed<DOMMediaStream> MozCaptureStream(ErrorResult& aRv);

  already_AddRefed<DOMMediaStream> MozCaptureStreamUntilEnded(ErrorResult& aRv);

  already_AddRefed<DOMMediaStream> CaptureStream(ErrorResult& aRv);

  bool MozAudioCaptured() const;

  void MozGetMetadata(JSContext* aCx, JS::MutableHandle<JSObject*> aResult,
                      ErrorResult& aRv);

  double MozFragmentEnd();

  AudioTrackList* AudioTracks();

  VideoTrackList* VideoTracks();

  TextTrackList* GetTextTracks();

  already_AddRefed<TextTrack> AddTextTrack(TextTrackKind aKind,
                                           const nsAString& aLabel,
                                           const nsAString& aLanguage);

  void AddTextTrack(TextTrack* aTextTrack) {
    GetOrCreateTextTrackManager()->AddTextTrack(aTextTrack);
  }

  void RemoveTextTrack(TextTrack* aTextTrack, bool aPendingListOnly = false) {
    if (mTextTrackManager) {
      mTextTrackManager->RemoveTextTrack(aTextTrack, aPendingListOnly);
    }
  }

  void NotifyCueAdded(TextTrackCue& aCue) {
    if (mTextTrackManager) {
      mTextTrackManager->NotifyCueAdded(aCue);
    }
  }
  void NotifyCueRemoved(TextTrackCue& aCue) {
    if (mTextTrackManager) {
      mTextTrackManager->NotifyCueRemoved(aCue);
    }
  }
  void NotifyCueUpdated(TextTrackCue* aCue) {
    if (mTextTrackManager) {
      mTextTrackManager->NotifyCueUpdated(aCue);
    }
  }

  void NotifyCueDisplayStatesChanged();

  void SetCuesDirty() {
    if (mTextTrackManager) {
      mTextTrackManager->SetCuesDirty();
    }
  }

  void UpdateCueDisplay() {
    if (mTextTrackManager) {
      mTextTrackManager->UpdateCueDisplay();
    }
  }

  bool IsBlessed() const { return mIsBlessed; }

  // A method to check whether we are currently playing.
  bool IsCurrentlyPlaying() const;

  // Returns true if the media element is being destroyed. Used in
  // dormancy checks to prevent dormant processing for an element
  // that will soon be gone.
  bool IsBeingDestroyed();

  virtual void OnVisibilityChange(Visibility aNewVisibility);

  // Return the effective volume, taking mute and other factors that affect the
  // final output volume into account.
  float ComputedVolume() const;

  // Begin testing only methods
  bool ComputedMuted() const;

  // Return true if the media has been suspended media due to an inactive
  // document or prohibiting by the docshell.
  bool IsSuspendedByInactiveDocOrDocShell() const;
  // End testing only methods

  void SetMediaInfo(const MediaInfo& aInfo);
  MediaInfo GetMediaInfo() const override;

  // Gives access to the decoder's frame statistics, if present.
  FrameStatistics* GetFrameStatistics() const override;

  void DispatchAsyncTestingEvent(const nsAString& aName) override;

  // Log the usage of a {visible / invisible} video element as
  // the source of {drawImage(), createPattern(), createImageBitmap() and
  // captureStream()} APIs. This function can be used to collect telemetries for
  // bug 1352007.
  enum class CallerAPI {
    DRAW_IMAGE,
    CREATE_PATTERN,
    CREATE_IMAGEBITMAP,
    CAPTURE_STREAM,
    CREATE_VIDEOFRAME,
  };
  void LogVisibility(CallerAPI aAPI);

  Document* GetDocument() const override;

  already_AddRefed<GMPCrashHelper> CreateGMPCrashHelper() override;

  // Set the sink id (of the output device) that the audio will play. If aSinkId
  // is empty the default device will be set.
  already_AddRefed<Promise> SetSinkId(const nsAString& aSinkId,
                                      ErrorResult& aRv);
  // Get the sink id of the device that audio is being played. Initial value is
  // empty and the default device is being used.
  void GetSinkId(nsString& aSinkId) const {
    MOZ_ASSERT(NS_IsMainThread());
    aSinkId = mSink.first;
  }

  // This is used to notify MediaElementAudioSourceNode that media element is
  // allowed to play when media element is used as a source for web audio, so
  // that we can start AudioContext if it was not allowed to start.
  RefPtr<GenericNonExclusivePromise> GetAllowedToPlayPromise();

  bool GetShowPosterFlag() const { return mShowPoster; }

  bool IsAudible() const;

  // Return key system in use if we have one, otherwise return nothing.
  Maybe<nsAutoString> GetKeySystem() const override;

  MediaEventSource<float>& EffectiveVolumeChangeEvent() {
    return mEffectiveVolumeChangeEvent;
  }

 protected:
  virtual ~HTMLMediaElement();

  class AudioChannelAgentCallback;
  class ChannelLoader;
  class ErrorSink;
  class GVAutoplayObserver;
  class MediaElementTrackSource;
  class MediaLoadListener;
  class MediaStreamRenderer;
  class MediaStreamTrackListener;
  class ShutdownObserver;
  class TitleChangeObserver;
  class MediaControlKeyListener;

  MediaDecoderOwner::NextFrameStatus NextFrameStatus();

  void SetDecoder(MediaDecoder* aDecoder);

  void PlayInternal(bool aHandlingUserInput);

  // See spec, https://html.spec.whatwg.org/#internal-pause-steps
  void PauseInternal();

  /** Use this method to change the mReadyState member, so required
   * events can be fired.
   */
  void ChangeReadyState(nsMediaReadyState aState);

  /**
   * Use this method to change the mNetworkState member, so required
   * actions will be taken during the transition.
   */
  void ChangeNetworkState(nsMediaNetworkState aState);

  /**
   * The MediaElement will be responsible for creating and releasing the audio
   * wakelock depending on the playing and audible state.
   */
  virtual void WakeLockRelease();
  virtual void UpdateWakeLock();

  // This must be called immediately after monitor attributes change, and cannot
  // wait for the Watchable notification, because some pseudo-classes are
  // required to be applied immediately after the change.
  void UpdatePlaybackPseudoClasses();

  void CreateAudioWakeLockIfNeeded();
  void ReleaseAudioWakeLockIfExists();
  void ReleaseAudioWakeLockInternal();
  RefPtr<WakeLock> mWakeLock;

  /**
   * Logs a warning message to the web console to report various failures.
   * aMsg is the localized message identifier, aParams is the parameters to
   * be substituted into the localized message, and aParamCount is the number
   * of parameters in aParams.
   */
  void ReportLoadError(const char* aMsg, const nsTArray<nsString>& aParams =
                                             nsTArray<nsString>());

  /**
   * Log message to web console.
   */
  void ReportToConsole(
      uint32_t aErrorFlags, const char* aMsg,
      const nsTArray<nsString>& aParams = nsTArray<nsString>()) const;

  /**
   * Changes mHasPlayedOrSeeked to aValue. If mHasPlayedOrSeeked changes
   * we'll force a reflow so that the video frame gets reflowed to reflect
   * the poster hiding or showing immediately.
   */
  void SetPlayedOrSeeked(bool aValue);

  /**
   * Initialize the media element for playback of aStream
   */
  void SetupSrcMediaStreamPlayback(DOMMediaStream* aStream);
  /**
   * Stop playback on mSrcStream.
   */
  void EndSrcMediaStreamPlayback();
  /**
   * Ensure we're playing mSrcStream if and only if we're not paused.
   */
  enum { REMOVING_SRC_STREAM = 0x1 };
  void UpdateSrcMediaStreamPlaying(uint32_t aFlags = 0);

  /**
   * Ensure currentTime progresses if and only if we're potentially playing
   * mSrcStream. Called by the watch manager while we're playing mSrcStream, and
   * one of the inputs to the potentially playing algorithm changes.
   */
  void UpdateSrcStreamPotentiallyPlaying();

  /**
   * mSrcStream's graph's CurrentTime() has been updated. It might be time to
   * fire "timeupdate".
   */
  void UpdateSrcStreamTime();

  /**
   * Called after a tail dispatch when playback of mSrcStream ended, to comply
   * with the spec where we must start reporting true for the ended attribute
   * after the event loop returns to step 1. A MediaStream could otherwise be
   * manipulated to end a HTMLMediaElement synchronously.
   */
  void UpdateSrcStreamReportPlaybackEnded();

  /**
   * Called by our DOMMediaStream::TrackListener when a new MediaStreamTrack has
   * been added to the playback stream of |mSrcStream|.
   */
  void NotifyMediaStreamTrackAdded(const RefPtr<MediaStreamTrack>& aTrack);

  /**
   * Called by our DOMMediaStream::TrackListener when a MediaStreamTrack in
   * |mSrcStream|'s playback stream has ended.
   */
  void NotifyMediaStreamTrackRemoved(const RefPtr<MediaStreamTrack>& aTrack);

  /**
   * Convenience method to get in a single list all enabled AudioTracks and, if
   * this is a video element, the selected VideoTrack.
   */
  void GetAllEnabledMediaTracks(nsTArray<RefPtr<MediaTrack>>& aTracks);

  /**
   * Enables or disables all tracks forwarded from mSrcStream to all
   * OutputMediaStreams. We do this for muting the tracks when pausing,
   * and unmuting when playing the media element again.
   */
  void SetCapturedOutputStreamsEnabled(bool aEnabled);

  /**
   * Returns true if output tracks should be muted, based on the state of this
   * media element.
   */
  enum class OutputMuteState { Muted, Unmuted };
  OutputMuteState OutputTracksMuted();

  /**
   * Sets the muted state of all output track sources. They are muted when we're
   * paused and unmuted otherwise.
   */
  void UpdateOutputTracksMuting();

  /**
   * Create a new MediaStreamTrack for the TrackSource corresponding to aTrack
   * and add it to the DOMMediaStream in aOutputStream. This automatically sets
   * the output track to enabled or disabled depending on our current playing
   * state.
   */
  enum class AddTrackMode { ASYNC, SYNC };
  void AddOutputTrackSourceToOutputStream(
      MediaElementTrackSource* aSource, OutputMediaStream& aOutputStream,
      AddTrackMode aMode = AddTrackMode::ASYNC);

  /**
   * Creates output track sources when this media element is captured, tracks
   * exist, playback is not ended and readyState is >= HAVE_METADATA.
   */
  void UpdateOutputTrackSources();

  /**
   * Returns an DOMMediaStream containing the played contents of this
   * element. When aBehavior is FINISH_WHEN_ENDED, when this element ends
   * playback we will finish the stream and not play any more into it.  When
   * aType is CONTINUE_WHEN_ENDED, ending playback does not finish the stream.
   * The stream will never finish.
   *
   * When aType is CAPTURE_AUDIO, we stop playout of audio and instead route it
   * to the DOMMediaStream. Volume and mute state will be applied to the audio
   * reaching the stream. No video tracks will be captured in this case.
   *
   * aAudioOutputConfig determines if we should configure audio output in our
   * media pipeline.
   *
   * aGraph may be null if the stream's tracks do not need to use a
   * specific graph.
   */
  already_AddRefed<DOMMediaStream> CaptureStreamInternal(
      StreamCaptureBehavior aFinishBehavior,
      StreamCaptureType aStreamCaptureType,
      AudioOutputConfig aAudioOutputConfig, MediaTrackGraph* aGraph);

  /**
   * Initialize a decoder as a clone of an existing decoder in another
   * element.
   * mLoadingSrc must already be set.
   */
  nsresult InitializeDecoderAsClone(ChannelMediaDecoder* aOriginal);

  /**
   * Call Load() and FinishDecoderSetup() on the decoder. It also handle
   * resource cloning if DecoderType is ChannelMediaDecoder.
   */
  template <typename DecoderType, typename... LoadArgs>
  nsresult SetupDecoder(DecoderType* aDecoder, LoadArgs&&... aArgs);

  /**
   * Initialize a decoder to load the given channel. The decoder's stream
   * listener is returned via aListener.
   * mLoadingSrc must already be set.
   */
  nsresult InitializeDecoderForChannel(nsIChannel* aChannel,
                                       nsIStreamListener** aListener);

  /**
   * Finish setting up the decoder after Load() has been called on it.
   * Called by InitializeDecoderForChannel/InitializeDecoderAsClone.
   */
  nsresult FinishDecoderSetup(MediaDecoder* aDecoder);

  /**
   * Call this after setting up mLoadingSrc and mDecoder.
   */
  void AddMediaElementToURITable();
  /**
   * Call this before modifying mLoadingSrc.
   */
  void RemoveMediaElementFromURITable();
  /**
   * Call this to find a media element with the same NodePrincipal and
   * mLoadingSrc set to aURI, and with a decoder on which Load() has been
   * called.
   */
  HTMLMediaElement* LookupMediaElementURITable(nsIURI* aURI);

  /**
   * Shutdown and clear mDecoder and maintain associated invariants.
   */
  void ShutdownDecoder();
  /**
   * Execute the initial steps of the load algorithm that ensure existing
   * loads are aborted, the element is emptied, and a new load ID is
   * created.
   */
  void AbortExistingLoads();

  /**
   * This is the dedicated media source failure steps.
   * Called when all potential resources are exhausted. Changes network
   * state to NETWORK_NO_SOURCE, and sends error event with code
   * MEDIA_ERR_SRC_NOT_SUPPORTED.
   */
  void NoSupportedMediaSourceError(const nsACString& aErrorDetails);

  /**
   * Per spec, Failed with elements: Queue a task, using the DOM manipulation
   * task source, to fire a simple event named error at the candidate element.
   * So dispatch |QueueLoadFromSourceTask| to main thread to make sure the task
   * will be executed later than loadstart event.
   */
  void DealWithFailedElement(nsIContent* aSourceElement);

  /**
   * Attempts to load resources from the <source> children. This is a
   * substep of the resource selection algorithm. Do not call this directly,
   * call QueueLoadFromSourceTask() instead.
   */
  void LoadFromSourceChildren(const JSCallingLocation& aCallingLocation);

  /**
   * Asynchronously awaits a stable state, and then causes
   * LoadFromSourceChildren() to be called on the main threads' event loop.
   */
  void QueueLoadFromSourceTask();

  /**
   * Runs the media resource selection algorithm.
   */
  void SelectResource(const JSCallingLocation& aCallingLocation);

  /**
   * A wrapper function that allows us to cleanly reset flags after a call
   * to SelectResource()
   */
  void SelectResourceWrapper(const JSCallingLocation& aCallingLocation);

  /**
   * Asynchronously awaits a stable state, and then causes SelectResource()
   * to be run on the main thread's event loop.
   */
  void QueueSelectResourceTask();

  /**
   * When loading a new source on an existing media element, make sure to reset
   * everything that is accessible using the media element API.
   */
  virtual void ResetState();

  /**
   * The resource-fetch algorithm step of the load algorithm.
   */
  MediaResult LoadResource(const JSCallingLocation& aCallingLocation);

  /**
   * Selects the next <source> child from which to load a resource. Called
   * during the resource selection algorithm. Stores the return value in
   * mSourceLoadCandidate before returning.
   */
  HTMLSourceElement* GetNextSource();

  /**
   * Changes mDelayingLoadEvent, and will call BlockOnLoad()/UnblockOnLoad()
   * on the owning document, so it can delay the load event firing.
   */
  void ChangeDelayLoadStatus(bool aDelay);

  /**
   * If we suspended downloading after the first frame, unsuspend now.
   */
  void StopSuspendingAfterFirstFrame();

  /**
   * Called when our channel is redirected to another channel.
   * Updates our mChannel reference to aNewChannel.
   */
  nsresult OnChannelRedirect(nsIChannel* aChannel, nsIChannel* aNewChannel,
                             uint32_t aFlags);

  /**
   * Call this to reevaluate whether we should be holding a self-reference.
   */
  void AddRemoveSelfReference();

  /**
   * Called when "xpcom-shutdown" event is received.
   */
  void NotifyShutdownEvent();

  /**
   * The preloading action to perform. These dictate how we react to the
   * preload attribute. See mPreloadAction.
   */
  enum PreloadAction {
    PRELOAD_UNDEFINED = 0,  // not determined - used only for initialization
    PRELOAD_NONE = 1,       // do not preload
    PRELOAD_METADATA = 2,   // preload only the metadata (and first frame)
    PRELOAD_ENOUGH = 3      // preload enough data to allow uninterrupted
                            // playback
  };

  /**
   * The guts of Load(). Load() acts as a wrapper around this which sets
   * mIsDoingExplicitLoad to true so that when script calls 'load()'
   * preload-none will be automatically upgraded to preload-metadata.
   */
  void DoLoad();

  /**
   * Suspends the load of mLoadingSrc, so that it can be resumed later
   * by ResumeLoad(). This is called when we have a media with a 'preload'
   * attribute value of 'none', during the resource selection algorithm.
   */
  void SuspendLoad();

  /**
   * Resumes a previously suspended load (suspended by SuspendLoad(uri)).
   * Will continue running the resource selection algorithm.
   * Sets mPreloadAction to aAction.
   */
  void ResumeLoad(PreloadAction aAction,
                  const JSCallingLocation& aCallingLocation);

  /**
   * Handle a change to the preload attribute. Should be called whenever the
   * value (or presence) of the preload attribute changes. The change in
   * attribute value may cause a change in the mPreloadAction of this
   * element. If there is a change then this method will initiate any
   * behaviour that is necessary to implement the action.
   */
  void UpdatePreloadAction(const JSCallingLocation& aCallingLocation);

  /**
   * Fire progress events if needed according to the time and byte constraints
   * outlined in the specification. aHaveNewProgress is true if progress has
   * just been detected.  Otherwise the method is called as a result of the
   * progress timer.
   */
  void CheckProgress(bool aHaveNewProgress);
  static void ProgressTimerCallback(nsITimer* aTimer, void* aClosure);
  /**
   * Start timer to update download progress.
   */
  void StartProgressTimer();
  /**
   * Start sending progress and/or stalled events.
   */
  void StartProgress();
  /**
   * Stop progress information timer and events.
   */
  void StopProgress();

  /**
   * Dispatches an error event to a child source element.
   */
  void DispatchAsyncSourceError(nsIContent* aSourceElement,
                                const nsACString& aErrorDetails);

  /**
   * Resets the media element for an error condition as per aErrorCode.
   * aErrorCode must be one of WebIDL HTMLMediaElement error codes.
   */
  void Error(uint16_t aErrorCode,
             const Maybe<MediaResult>& aResult = Nothing());

  /**
   * Returns the URL spec of the currentSrc.
   **/
  void GetCurrentSpec(nsCString& aString);

  /**
   * Process any media fragment entries in the URI
   */
  void ProcessMediaFragmentURI();

  /**
   * Mute or unmute the audio and change the value that the |muted| map.
   */
  void SetMutedInternal(uint32_t aMuted);
  /**
   * Update the volume of the output audio stream to match the element's
   * current mMuted/mVolume/mAudioChannelFaded state.
   */
  void SetVolumeInternal();

  // Record the glean probe once per resource when a playback that would
  // otherwise be audible is muted only by the muted content attribute added at
  // runtime.
  void MaybeRecordRuntimeMutedContentAttrImpact();

  /**
   * Suspend or resume element playback and resource download.  When we suspend
   * playback, event delivery would also be suspended (and events queued) until
   * the element is resumed.
   */
  void SuspendOrResumeElement(bool aSuspendElement);

  // Get the HTMLMediaElement object if the decoder is being used from an
  // HTML media element, and null otherwise.
  HTMLMediaElement* GetMediaElement() final { return this; }

  // Return true if decoding should be paused
  bool GetPaused() final { return Paused(); }

  // Seeks to aTime seconds. aSeekType can be Exact to seek to exactly the
  // seek target, or PrevSyncPoint if a quicker but less precise seek is
  // desired, and we'll seek to the sync point (keyframe and/or start of the
  // next block of audio samples) preceeding seek target.
  void Seek(double aTime, SeekTarget::Type aSeekType, ErrorResult& aRv);

  // Update the audio channel playing state
  void UpdateAudioChannelPlayingState();

  // Adds to the element's list of pending text tracks each text track
  // in the element's list of text tracks whose text track mode is not disabled
  // and whose text track readiness state is loading.
  void PopulatePendingTextTrackList();

  // Gets a reference to the MediaElement's TextTrackManager. If the
  // MediaElement doesn't yet have one then it will create it.
  TextTrackManager* GetOrCreateTextTrackManager();

  // Recomputes ready state and fires events as necessary based on current
  // state.
  void UpdateReadyStateInternal();

  // Create or destroy the captured stream.
  void AudioCaptureTrackChange(bool aCapture);

  // If the network state is empty and then we would trigger DoLoad().
  void MaybeDoLoad();

  // Anything we need to check after played success and not related with spec.
  void UpdateCustomPolicyAfterPlayed();

  // Returns a StreamCaptureType populated with the right bits, depending on the
  // tracks this HTMLMediaElement has.
  StreamCaptureType CaptureTypeForElement();

  // Returns true if capture is allowed. Returns false and sets aRv if capture
  // is not allowed: NotSupportedError if the element contains restricted
  // content, or NS_ERROR_FAILURE if the document has no window.
  bool CanBeCaptured(StreamCaptureType aCaptureType, ErrorResult& aRv);

  // For nsAsyncEventRunner.
  // The event is blocked while the document is in B/F cache.
  MOZ_CAN_RUN_SCRIPT nsresult FireEvent(const nsAString& aName);

  already_AddRefed<nsMediaEventRunner> GetEventRunner(
      const nsAString& aName, EventFlag aFlag = EventFlag::eNone);

  // This method moves the mPendingPlayPromises into a temperate object. So the
  // mPendingPlayPromises is cleared after this method call.
  nsTArray<RefPtr<PlayPromise>> TakePendingPlayPromises();

  // This method snapshots the mPendingPlayPromises by TakePendingPlayPromises()
  // and queues a task to resolve them.
  void AsyncResolvePendingPlayPromises();

  // This method snapshots the mPendingPlayPromises by TakePendingPlayPromises()
  // and queues a task to reject them.
  void AsyncRejectPendingPlayPromises(nsresult aError);

  // This method snapshots the mPendingPlayPromises by TakePendingPlayPromises()
  // and queues a task to resolve them also to dispatch a "playing" event.
  void NotifyAboutPlaying();

  already_AddRefed<Promise> CreateDOMPromise(ErrorResult& aRv) const;

  // Pass information for deciding the video decode mode to decoder.
  void NotifyDecoderActivityChanges() const;

  // Constructs an AudioTrack in mAudioTrackList if aInfo reports that audio is
  // available, and a VideoTrack in mVideoTrackList if aInfo reports that video
  // is available.
  void ConstructMediaTracks(const MediaInfo* aInfo);

  // Removes all MediaTracks from mAudioTrackList and mVideoTrackList and fires
  // "removetrack" on the lists accordingly.
  // Note that by spec, this should not fire "removetrack". However, it appears
  // other user agents do, per
  // https://wpt.fyi/results/media-source/mediasource-avtracks.html.
  void RemoveMediaTracks();

  // Mark the decoder owned by the element as tainted so that the
  // suspend-video-decoder is disabled.
  void MarkAsTainted();

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void AfterSetAttr(
      int32_t aNameSpaceID, nsAtom* aName, const nsAttrValue* aValue,
      const nsAttrValue* aOldValue, nsIPrincipal* aMaybeScriptedPrincipal,
      bool aNotify) override;
  virtual void OnAttrSetButNotChanged(int32_t aNamespaceID, nsAtom* aName,
                                      const nsAttrValueOrString& aValue,
                                      bool aNotify) override;

  bool DetachExistingMediaKeys();
  bool TryRemoveMediaKeysAssociation();
  void RemoveMediaKeys();
  bool AttachNewMediaKeys();
  bool TryMakeAssociationWithCDM(CDMProxy* aProxy);
  void MakeAssociationWithCDMResolved();
  void SetCDMProxyFailure(const MediaResult& aResult);
  void ResetSetMediaKeysTempVariables();

  void PauseIfShouldNotBePlaying();

  WatchManager<HTMLMediaElement> mWatchManager;

  // When the play is not allowed, dispatch related events which are used for
  // testing or changing control UI.
  void DispatchEventsWhenPlayWasNotAllowed();

  // When the doc is blocked permanantly, we would dispatch event to notify
  // front-end side to show blocking icon.
  void MaybeNotifyAutoplayBlocked();

  // Dispatch event for video control when video gets blocked in order to show
  // the click-to-play icon.
  void DispatchBlockEventForVideoControl();

  // When playing state change, we have to notify MediaControl in the chrome
  // process in order to keep its playing state correct.
  void NotifyMediaControlPlaybackStateChanged();

  // Clear the timer when we want to continue listening to the media control
  // key events.
  void ClearStopMediaControlTimerIfNeeded();

  // Sets a secondary renderer for mSrcStream, so this media element can be
  // rendered in Picture-in-Picture mode when playing a MediaStream. A null
  // aContainer will unset the secondary renderer. aFirstFrameOutput allows
  // for injecting a listener of the callers choice for rendering the first
  // frame.
  void SetSecondaryMediaStreamRenderer(
      VideoFrameContainer* aContainer,
      FirstFrameVideoOutput* aFirstFrameOutput = nullptr);

  // This function is used to update the status of media control when the media
  // changes its status of being used in the Picture-in-Picture mode.
  void UpdateMediaControlAfterPictureInPictureModeChanged();

  // Return true if the element has pending callbacks that should prevent the
  // suspension of video playback.
  virtual bool HasPendingCallbacks() const { return false; }

  // The current decoder. Load() has been called on this decoder.
  // At most one of mDecoder and mSrcStream can be non-null.
  RefPtr<MediaDecoder> mDecoder;

  // A reference to the VideoFrameContainer which contains the current frame
  // of video to display.
  RefPtr<VideoFrameContainer> mVideoFrameContainer;

  // Holds a reference to the MediaStream that has been set in the src
  // attribute.
  RefPtr<DOMMediaStream> mSrcAttrStream;

  // Holds the triggering principal for the src attribute.
  nsCOMPtr<nsIPrincipal> mSrcAttrTriggeringPrincipal;

  // Holds a reference to the MediaStream that we're actually playing.
  // At most one of mDecoder and mSrcStream can be non-null.
  RefPtr<DOMMediaStream> mSrcStream;

  // The MediaStreamRenderer handles rendering of our selected video track, and
  // enabled audio tracks, while mSrcStream is set.
  RefPtr<MediaStreamRenderer> mMediaStreamRenderer;

  // The secondary MediaStreamRenderer handles rendering of our selected video
  // track to a secondary VideoFrameContainer, while mSrcStream is set.
  RefPtr<MediaStreamRenderer> mSecondaryMediaStreamRenderer;

  // True once PlaybackEnded() is called and we're playing a MediaStream.
  // Reset to false if we start playing mSrcStream again.
  Watchable<bool> mSrcStreamPlaybackEnded = {
      false, "HTMLMediaElement::mSrcStreamPlaybackEnded"};

  // Mirrors mSrcStreamPlaybackEnded after a tail dispatch when set to true,
  // but may be be forced to false directly. To accomodate when an application
  // ends playback synchronously by manipulating mSrcStream or its tracks,
  // e.g., through MediaStream.removeTrack(), or MediaStreamTrack.stop().
  bool mSrcStreamReportPlaybackEnded = false;

  // Holds a reference to the stream connecting this stream to the window
  // capture sink.
  RefPtr<MediaStreamWindowCapturer> mStreamWindowCapturer;

  // Holds references to the DOM wrappers for the MediaStreams that we're
  // writing to.
  nsTArray<OutputMediaStream> mOutputStreams;

  // Mapping for output tracks, from dom::MediaTrack ids to the
  // MediaElementTrackSource that represents the source of all corresponding
  // MediaStreamTracks captured from this element.
  nsRefPtrHashtable<nsStringHashKey, MediaElementTrackSource>
      mOutputTrackSources;

  // The currently selected video stream track.
  RefPtr<VideoStreamTrack> mSelectedVideoStreamTrack;

  // Created in Init() and AfterSetAttr(), released in deconstructor
  RefPtr<GVAutoplayObserver> mGVAutoplayObserver;

  const RefPtr<ShutdownObserver> mShutdownObserver;

  const RefPtr<TitleChangeObserver> mTitleChangeObserver;

  // Holds a reference to the MediaSource, if any, referenced by the src
  // attribute on the media element.
  RefPtr<MediaSource> mSrcMediaSource;

  // Holds a reference to the MediaSource supplying data for playback.  This
  // may either match mSrcMediaSource or come from Source element children.
  // This is set when and only when mLoadingSrc corresponds to an object url
  // that resolved to a MediaSource.
  RefPtr<MediaSource> mMediaSource;

  RefPtr<ChannelLoader> mChannelLoader;

  // Points to the child source elements, used to iterate through the children
  // when selecting a resource to load.  This is the previous sibling of the
  // child considered the current 'candidate' in:
  // https://html.spec.whatwg.org/multipage/media.html#concept-media-load-algorithm
  //
  // mSourcePointer == nullptr, we will next try to load |GetFirstChild()|.
  // mSourcePointer == GetLastChild(), we've exhausted all sources, waiting
  // for new elements to be appended.
  nsCOMPtr<nsIContent> mSourcePointer;

  // Points to the document whose load we're blocking. This is the document
  // we're bound to when loading starts.
  nsCOMPtr<Document> mLoadBlockedDoc;

  // This is used to help us block/resume the event delivery.
  class EventBlocker;
  RefPtr<EventBlocker> mEventBlocker;

  // Media loading flags. See:
  //   http://www.whatwg.org/specs/web-apps/current-work/#video)
  nsMediaNetworkState mNetworkState = HTMLMediaElement_Binding::NETWORK_EMPTY;
  Watchable<nsMediaReadyState> mReadyState = {
      HTMLMediaElement_Binding::HAVE_NOTHING, "HTMLMediaElement::mReadyState"};

  enum LoadAlgorithmState {
    // No load algorithm instance is waiting for a source to be added to the
    // media in order to continue loading.
    NOT_WAITING,
    // We've run the load algorithm, and we tried all source children of the
    // media element, and failed to load any successfully. We're waiting for
    // another source element to be added to the media element, and will try
    // to load any such element when its added.
    WAITING_FOR_SOURCE
  };

  // The current media load ID. This is incremented every time we start a
  // new load. Async events note the ID when they're first sent, and only fire
  // if the ID is unchanged when they come to fire.
  uint32_t mCurrentLoadID = 0;

  // Denotes the waiting state of a load algorithm instance. When the load
  // algorithm is waiting for a source element child to be added, this is set
  // to WAITING_FOR_SOURCE, otherwise it's NOT_WAITING.
  LoadAlgorithmState mLoadWaitStatus = NOT_WAITING;

  // Current audio volume
  double mVolume = 1.0;

  // True if the audio track is not silent.
  bool mIsAudioTrackAudible = false;

  uint32_t mMuted = 0;

  // The tristate "muted state". While Default, the muted content attribute is a
  // fallback that determines whether the element is muted; once the muted
  // setter latches the state to True or False, the content attribute no longer
  // applies.
  // https://html.spec.whatwg.org/multipage/media.html#concept-media-muted-state
  enum class MutedState : uint8_t { Default, True, False };
  MutedState mMutedState = MutedState::Default;

  // Whether the muted content attribute added at runtime (while the muted state
  // is "default") is what would mute this element, and whether the resulting
  // impact has already been recorded for the current resource. Used only for
  // the glean probe.
  bool mMutedByRuntimeContentAttr = false;
  bool mRecordedRuntimeContentAttrImpact = false;

  UniquePtr<const MetadataTags> mTags;

  // URI of the resource we're attempting to load. This stores the value we
  // return in the currentSrc attribute. Use GetCurrentSrc() to access the
  // currentSrc attribute.
  // This is always the original URL we're trying to load --- before
  // redirects etc.
  nsCOMPtr<nsIURI> mLoadingSrc;

  // The URI of the resource actually loaded. Starts equal to mLoadingSrc and
  // is updated to the post-redirect URI on each redirect. Used to decide
  // cross-origin load-error redaction; null means we have no captured URI, and
  // is treated as cross-origin.
  nsCOMPtr<nsIURI> mLoadingSrcFinalURI;

  // The triggering principal for the current source.
  nsCOMPtr<nsIPrincipal> mLoadingSrcTriggeringPrincipal;

  // Stores the current preload action for this element. Initially set to
  // PRELOAD_UNDEFINED, its value is changed by calling
  // UpdatePreloadAction().
  PreloadAction mPreloadAction = PRELOAD_UNDEFINED;

  // Time that the last timeupdate event was queued. Read/Write from the
  // main thread only.
  TimeStamp mQueueTimeUpdateRunnerTime;

  // Time that the last timeupdate event was fired. Read/Write from the
  // main thread only.
  TimeStamp mLastTimeUpdateDispatchTime;

  // Time that the last progress event was fired. Read/Write from the
  // main thread only.
  TimeStamp mProgressTime;

  // Time that data was last read from the media resource. Used for
  // computing if the download has stalled and to rate limit progress events
  // when data is arriving slower than PROGRESS_MS.
  // Read/Write from the main thread only.
  TimeStamp mDataTime;

  // Media 'currentTime' value when the last timeupdate event was queued.
  // Read/Write from the main thread only.
  double mLastCurrentTime = 0.0;

  // Logical start time of the media resource in seconds as obtained
  // from any media fragments. A negative value indicates that no
  // fragment time has been set. Read/Write from the main thread only.
  double mFragmentStart = -1.0;

  // Logical end time of the media resource in seconds as obtained
  // from any media fragments. A negative value indicates that no
  // fragment time has been set. Read/Write from the main thread only.
  double mFragmentEnd = -1.0;

  // The defaultPlaybackRate attribute gives the desired speed at which the
  // media resource is to play, as a multiple of its intrinsic speed.
  double mDefaultPlaybackRate = 1.0;

  // The playbackRate attribute gives the speed at which the media resource
  // plays, as a multiple of its intrinsic speed. If it is not equal to the
  // defaultPlaybackRate, then the implication is that the user is using a
  // feature such as fast forward or slow motion playback.
  double mPlaybackRate = 1.0;

  // True if pitch correction is applied when playbackRate is set to a
  // non-intrinsic value.
  bool mPreservesPitch = true;

  // Reference to the source element last returned by GetNextSource().
  // This is the child source element which we're trying to load from.
  nsCOMPtr<nsIContent> mSourceLoadCandidate;

  // Range of time played.
  RefPtr<TimeRanges> mPlayed;

  // Timer used for updating progress events.
  nsCOMPtr<nsITimer> mProgressTimer;

  // Encrypted Media Extension media keys.
  RefPtr<MediaKeys> mMediaKeys;
  RefPtr<MediaKeys> mIncomingMediaKeys;
  // The dom promise is used for HTMLMediaElement::SetMediaKeys.
  RefPtr<DetailedPromise> mSetMediaKeysDOMPromise;
  // Used to indicate if the MediaKeys attaching operation is on-going or not.
  bool mAttachingMediaKey = false;
  MozPromiseRequestHolder<SetCDMPromise> mSetCDMRequest;

  // Stores the time at the start of the current 'played' range, if any.
  Maybe<double> mCurrentPlayRangeStart;

  // True if loadeddata has been fired.
  bool mLoadedDataFired = false;

  // One of the factors determines whether a media element with 'autoplay'
  // attribute is allowed to start playing.
  // https://html.spec.whatwg.org/multipage/media.html#can-autoplay-flag
  bool mCanAutoplayFlag = true;

  // Playback of the video is paused either due to calling the
  // 'Pause' method, or playback not yet having started.
  Watchable<bool> mPaused = {true, "HTMLMediaElement::mPaused"};

  // The following two fields are here for the private storage of the builtin
  // video controls, and control 'casting' of the video to external devices
  // (TVs, projectors etc.)
  // True if casting is currently allowed
  bool mAllowCasting = false;
  // True if currently casting this video
  bool mIsCasting = false;

  // Set while there are some OutputMediaStreams this media element's enabled
  // and selected tracks are captured into. When set, all tracks are captured
  // into the graph of this dummy track.
  // NB: This is a SharedDummyTrack to allow non-default graphs (AudioContexts
  // with an explicit sampleRate defined) to capture this element. When
  // cross-graph tracks are supported, this can become a bool.
  Watchable<RefPtr<SharedDummyTrack>> mTracksCaptured;

  // True if the sound is being captured.
  bool mAudioCaptured = false;

  // If TRUE then the media element was actively playing before the currently
  // in progress seeking. If FALSE then the media element is either not seeking
  // or was not actively playing before the current seek. Used to decide whether
  // to raise the 'waiting' event as per 4.7.1.8 in HTML 5 specification.
  bool mPlayingBeforeSeek = false;

  // True if this element is suspended because the document is inactive or the
  // inactive docshell is not allowing media to play.
  bool mSuspendedByInactiveDocOrDocshell = false;

#if defined(MOZ_WIDGET_ANDROID)
  // Android-only state for the temporary background media playback probe (bug
  // 2066141): true once we have recorded telemetry for the current background
  // episode. Reset when the document becomes visible again.
  bool mRecordedBackgroundAudioPlayback = false;
#endif

  // True if we're running the "load()" method.
  bool mIsRunningLoadMethod = false;

  // True if we're running or waiting to run queued tasks due to an explicit
  // call to "load()".
  bool mIsDoingExplicitLoad = false;

  // True if we're loading the resource from the child source elements.
  bool mIsLoadingFromSourceChildren = false;

  // True if we're delaying the "load" event. They are delayed until either
  // an error occurs, or the first frame is loaded.
  bool mDelayingLoadEvent = false;

  // True when we've got a task queued to call SelectResource(),
  // or while we're running SelectResource().
  bool mIsRunningSelectResource = false;

  // True when we already have select resource call queued
  bool mHaveQueuedSelectResource = false;

  // True if we suspended the decoder because we were paused,
  // preloading metadata is enabled, autoplay was not enabled, and we loaded
  // the first frame.
  bool mSuspendedAfterFirstFrame = false;

  // True if we are allowed to suspend the decoder because we were paused,
  // preloading metdata was enabled, autoplay was not enabled, and we loaded
  // the first frame.
  bool mAllowSuspendAfterFirstFrame = true;

  // True if we've played or completed a seek. We use this to determine
  // when the poster frame should be shown.
  bool mHasPlayedOrSeeked = false;

  // True if we've added a reference to ourselves to keep the element
  // alive while no-one is referencing it but the element may still fire
  // events of its own accord.
  bool mHasSelfReference = false;

  // True if we've received a notification that the engine is shutting
  // down.
  bool mShuttingDown = false;

  // True if we've suspended a load in the resource selection algorithm
  // due to loading a preload:none media. When true, the resource we'll
  // load when the user initiates either playback or an explicit load is
  // stored in mPreloadURI.
  bool mSuspendedForPreloadNone = false;

  // True if we've connected mSrcStream to the media element output.
  bool mSrcStreamIsPlaying = false;

  // True if we should set nsIClassOfService::UrgentStart to the channel to
  // get the response ASAP for better user responsiveness.
  bool mUseUrgentStartForChannel = false;

  // The CORS mode when loading the media element
  CORSMode mCORSMode = CORS_NONE;

  // Info about the played media.
  MediaInfo mMediaInfo;

  // True if the media has encryption information.
  bool mIsEncrypted = false;

  enum WaitingForKeyState {
    NOT_WAITING_FOR_KEY = 0,
    WAITING_FOR_KEY = 1,
    WAITING_FOR_KEY_DISPATCHED = 2
  };

  // True when the CDM cannot decrypt the current block due to lacking a key.
  // Note: the "waitingforkey" event is not dispatched until all decoded data
  // has been rendered.
  WaitingForKeyState mWaitingForKey = NOT_WAITING_FOR_KEY;

  // Listens for waitingForKey events from the owned decoder.
  MediaEventListener mWaitingForKeyListener;

  // Init Data that needs to be sent in 'encrypted' events in MetadataLoaded().
  EncryptionInfo mPendingEncryptedInitData;

  // True if the media's channel's download has been suspended.
  Watchable<bool> mDownloadSuspendedByCache = {
      false, "HTMLMediaElement::mDownloadSuspendedByCache"};

  // Disable the video playback by track selection. This flag might not be
  // enough if we ever expand the ability of supporting multi-tracks video
  // playback.
  bool mDisableVideo = false;

  RefPtr<TextTrackManager> mTextTrackManager;

  RefPtr<AudioTrackList> mAudioTrackList;

  RefPtr<VideoTrackList> mVideoTrackList;

  RefPtr<MediaStreamTrackListener> mMediaStreamTrackListener;

  // The principal guarding mVideoFrameContainer access when playing a
  // MediaStream.
  nsCOMPtr<nsIPrincipal> mSrcStreamVideoPrincipal;

  // True if the autoplay media was blocked because it hadn't loaded metadata
  // yet.
  bool mBlockedAsWithoutMetadata = false;

  // This promise is used to notify MediaElementAudioSourceNode that media
  // element is allowed to play when MediaElement is used as a source for web
  // audio.
  MozPromiseHolder<GenericNonExclusivePromise> mAllowedToPlayPromise;

  // True if media has ever been blocked for autoplay, it's used to notify front
  // end to show the correct blocking icon when the document goes back from
  // bfcache.
  bool mHasEverBeenBlockedForAutoplay = false;

  // True if we have dispatched a task for text track changed, will be unset
  // when we starts processing text track changed.
  // https://html.spec.whatwg.org/multipage/media.html#pending-text-track-change-notification-flag
  bool mPendingTextTrackChanged = false;

  Visibility mVisibilityState = Visibility::Untracked;

  // Internal state for the media lazy-loading steps:
  // https://html.spec.whatwg.org/multipage/media.html#lazy-loading-attributes
  // True while the element is registered for lazy loading and its load is
  // deferred.
  bool mLazyLoading = false;
  // True once lazy loading has been performed; prevents re-activation after
  // seek or explicit load().
  bool mLazyLoadingCompleted = false;

 public:
  // This function will be called whenever a text track that is in a media
  // element's list of text tracks has its text track mode change value
  void NotifyTextTrackModeChanged();

 private:
  friend class nsMediaEventRunner;
  friend class nsResolveOrRejectPendingPlayPromisesRunner;

  already_AddRefed<PlayPromise> CreatePlayPromise(ErrorResult& aRv) const;

  virtual void MaybeBeginCloningVisually() {};

  uint32_t GetPreloadDefault() const;
  uint32_t GetPreloadDefaultAuto() const;

  bool ShouldSuspendDownloadAfterFirstFrameLoaded() const;

  /**
   * This function is called by AfterSetAttr and OnAttrSetButNotChanged.
   * It will not be called if the value is being unset.
   *
   * @param aNamespaceID the namespace of the attr being set
   * @param aName the localname of the attribute being set
   * @param aNotify Whether we plan to notify document observers.
   */
  void AfterMaybeChangeAttr(int32_t aNamespaceID, nsAtom* aName, bool aNotify);

  // True if Init() has been called after construction
  bool mInitialized = false;

  // True if user has called load(), seek() or element has started playing
  // before. It's *only* use for `click-to-play` blocking autoplay policy.
  // In addition, we would reset this once media aborts current load.
  bool mIsBlessed = false;

  // True if the first frame has been successfully loaded.
  Watchable<bool> mFirstFrameLoaded = {false,
                                       "HTMLMediaElement::mFirstFrameLoaded"};

  // Media elements also have a default playback start position, which must
  // initially be set to zero seconds. This time is used to allow the element to
  // be seeked even before the media is loaded.
  double mDefaultPlaybackStartPosition = 0.0;

  // True if media element has been marked as 'tainted' and can't
  // participate in video decoder suspending.
  bool mHasSuspendTaint = false;

  // True if media element has been forced into being considered 'hidden'.
  // For use by mochitests. Enabling pref "media.test.video-suspend"
  bool mForcedHidden = false;

  // https://html.spec.whatwg.org/multipage/media.html#is-currently-stalled
  bool mIsCurrentlyStalled = false;

  UniquePtr<ErrorSink> mErrorSink;

  // This wrapper will handle all audio channel related stuffs, eg. the
  // operations of tab audio indicator, Fennec's media control. Note:
  // mAudioChannelWrapper might be null after GC happened.
  RefPtr<AudioChannelAgentCallback> mAudioChannelWrapper;

  // A list of pending play promises. The elements are pushed during the play()
  // method call and are resolved/rejected during further playback steps.
  nsTArray<RefPtr<PlayPromise>> mPendingPlayPromises;

  // A list of already-dispatched but not yet run
  // nsResolveOrRejectPendingPlayPromisesRunners.
  // Runners whose Run() method is called remove themselves from this list.
  // We keep track of these because the load algorithm resolves/rejects all
  // already-dispatched pending play promises.
  nsTArray<RefPtr<nsResolveOrRejectPendingPlayPromisesRunner>>
      mPendingPlayPromisesRunners;

  // A pending seek promise which is created at Seek() method call and is
  // resolved/rejected at AsyncResolveSeekDOMPromiseIfExists()/
  // AsyncRejectSeekDOMPromiseIfExists() methods.
  RefPtr<dom::Promise> mSeekDOMPromise;

  // Return true if the docshell is inactive and explicitly wants to stop media
  // playing in that shell.
  bool ShouldBeSuspendedByInactiveDocShell() const;

  // For debugging bug 1407148.
  void AssertReadyStateIsNothing();

  // Contains the unique id of the sink device and the device info.
  // The initial value is ("", nullptr) and the default output device is used.
  // It can contain an invalid id and info if the device has been
  // unplugged. It can be set to ("", nullptr). It follows the spec attribute:
  // https://w3c.github.io/mediacapture-output/#htmlmediaelement-extensions
  // Read/Write from the main thread only.
  std::pair<nsString, RefPtr<AudioDeviceInfo>> mSink;

  // This flag is used to control when the user agent is to show a poster frame
  // for a video element instead of showing the video contents.
  // https://html.spec.whatwg.org/multipage/media.html#show-poster-flag
  bool mShowPoster;

  // We may delay starting playback of a media for an unvisited tab until it's
  // going to foreground. We would create ResumeDelayedMediaPlaybackAgent to
  // handle related operations at the time whenever delaying media playback is
  // needed.
  void CreateResumeDelayedMediaPlaybackAgentIfNeeded();
  void ClearResumeDelayedMediaPlaybackAgentIfNeeded();
  RefPtr<ResumeDelayedPlaybackAgent> mResumeDelayedPlaybackAgent;
  MozPromiseRequestHolder<ResumeDelayedPlaybackAgent::ResumePromise>
      mResumePlaybackRequest;

  // Return true if we have already a decoder or a src stream and don't have any
  // error.
  bool IsPlayable() const;

  // Return true if the media source qualifies for full media-key control,
  // meaning the OS media-control interface (media keys, lock-screen widget,
  // etc.) will be activated for this element.
  bool IsControllableMediaSource() const;

  // Start the listener if media fits the requirement of being able to be
  // controlled be media control keys.
  void StartMediaControlKeyListenerIfNeeded();

  // It's used to listen media control key, by which we would play or pause
  // media element.
  RefPtr<MediaControlKeyListener> mMediaControlKeyListener;

  // Method to update audio stream name
  void UpdateStreamName();

  // Return true if the media element is being used in picture in picture mode.
  bool IsBeingUsedInPictureInPictureMode() const;

  // Return true if we should queue a 'timeupdate' event runner to main thread.
  bool ShouldQueueTimeupdateAsyncTask(TimeupdateType aType) const;

  // In order to avoid the back-button intervention resulting in history
  // entries with autoplayed videos from being skipped, we make sure that an
  // entry which has played a video is considered to have been interacted
  // with. See bug 1946547.
  void MaybeMarkSHEntryAsUserInteracted();

  // True if we should have track sources for captured tracks.
  bool ShouldHaveTrackSources() const;

#ifdef MOZ_WMF_CDM
  // It's used to record telemetry probe for WMFCDM playback.
  bool mIsUsingWMFCDM = false;
#endif

  Maybe<DelayedScheduler<AwakeTimeStamp>> mAudioWakelockReleaseScheduler;

  // AudioOutputConfig::Needed means audio is rendered through our own
  // media-pipeline audio backend. Otherwise, audio output configuration is not
  // required because audio is routed to Web Audio’s backend (via
  // MediaElementAudioSourceNode), or is not played through output devices at
  // all (via MozCaptureStreamXXX). The latter will be unsupported and removed
  // soon.
  // Note: Once this becomes NotNeeded, it will never change back. The current
  // API design does not provide a way to revert this change.
  AudioOutputConfig mAudioOutputConfig = AudioOutputConfig::Needed;

  MediaEventProducer<float> mEffectiveVolumeChangeEvent;
};

}  // namespace mozilla::dom

inline nsISupports* ToSupports(mozilla::dom::HTMLMediaElement* aElement) {
  return static_cast<mozilla::dom::EventTarget*>(aElement);
}

#endif  // mozilla_dom_HTMLMediaElement_h
