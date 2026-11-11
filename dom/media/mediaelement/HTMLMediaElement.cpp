/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLMediaElement.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

#include "AudioDeviceInfo.h"
#include "AudioStreamTrack.h"
#include "AutoplayPolicy.h"
#include "ChannelMediaDecoder.h"
#include "CrossGraphPort.h"
#include "DOMMediaStream.h"
#include "DecoderDoctorDiagnostics.h"
#include "DecoderDoctorLogger.h"
#include "DecoderTraits.h"
#include "FrameStatistics.h"
#include "GMPCrashHelper.h"
#include "GVAutoplayPermissionRequest.h"
#include "nsString.h"
#ifdef MOZ_ANDROID_HLS_SUPPORT
#  include "HLSDecoder.h"
#endif
#include "HTMLMediaElement.h"
#include "ImageContainer.h"
#include "MP4Decoder.h"
#include "MediaContainerType.h"
#include "MediaError.h"
#include "MediaManager.h"
#include "MediaMetadataManager.h"
#include "MediaProfilerMarkers.h"
#include "MediaResource.h"
#include "MediaShutdownManager.h"
#include "MediaSourceDecoder.h"
#include "MediaStreamError.h"
#include "MediaStreamWindowCapturer.h"
#include "MediaTrack.h"
#include "MediaTrackGraphImpl.h"
#include "MediaTrackList.h"
#include "MediaTrackListener.h"
#include "Navigator.h"
#include "ReferrerInfo.h"
#include "TimeRanges.h"
#include "TimeUnits.h"
#include "VideoFrameContainer.h"
#include "VideoOutput.h"
#include "VideoStreamTrack.h"
#include "base/basictypes.h"
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "jsapi.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/EMEUtils.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/MediaFragmentURIParser.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/AudioTrack.h"
#include "mozilla/dom/AudioTrackList.h"
#include "mozilla/dom/BlobURLProtocolHandler.h"
#include "mozilla/dom/ContentMediaController.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/HTMLAudioElement.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLMediaElementBinding.h"
#include "mozilla/dom/HTMLSourceElement.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/MediaControlUtils.h"
#include "mozilla/dom/MediaDevices.h"
#include "mozilla/dom/MediaEncryptedEvent.h"
#include "mozilla/dom/MediaErrorBinding.h"
#include "mozilla/dom/MediaSource.h"
#include "mozilla/dom/PermissionsPolicyUtils.h"
#include "mozilla/dom/PlayPromise.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/Range.h"
#include "mozilla/dom/TextTrack.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/VideoPlaybackQuality.h"
#include "mozilla/dom/VideoTrack.h"
#include "mozilla/dom/VideoTrackList.h"
#include "mozilla/dom/WakeLock.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/power/PowerManagerService.h"
#include "mozilla/glean/DomMediaMetrics.h"
#include "mozilla/net/ChannelClassifierUtils.h"
#include "mozilla/nsVideoFrame.h"
#include "nsAttrValueInlines.h"
#include "nsAttrValueOrString.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDisplayList.h"
#include "nsDocShell.h"
#include "nsError.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsGlobalWindowInner.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsICachingChannel.h"
#include "nsIClassOfService.h"
#include "nsIContentPolicy.h"
#include "nsIDocShell.h"
#include "nsIFrame.h"
#include "nsIHttpChannel.h"
#include "nsIObserverService.h"
#include "nsIRequest.h"
#include "nsIScriptError.h"
#include "nsISupportsPrimitives.h"
#include "nsIThreadRetargetableStreamListener.h"
#include "nsITimer.h"
#include "nsJSUtils.h"
#include "nsLayoutUtils.h"
#include "nsMimeTypes.h"
#include "nsNetUtil.h"
#include "nsNodeInfoManager.h"
#include "nsPresContext.h"
#include "nsQueryObject.h"
#include "nsSize.h"
#include "nsThreadUtils.h"
#include "nsURIHashKey.h"
#include "nsURLHelper.h"
#ifdef XP_WIN
#  include "objbase.h"
#endif
#include "xpcpublic.h"

mozilla::LazyLogModule gMediaElementLog("HTMLMediaElement");
mozilla::LazyLogModule gMediaElementEventsLog("HTMLMediaElementEvents");

extern mozilla::LazyLogModule gAutoplayPermissionLog;
#define AUTOPLAY_LOG(msg, ...) \
  MOZ_LOG_FMT(gAutoplayPermissionLog, LogLevel::Debug, msg, ##__VA_ARGS__)

// avoid redefined macro in unified build
#undef MEDIACONTROL_LOG
#define MEDIACONTROL_LOG(msg, ...)                                            \
  MOZ_LOG_FMT(gMediaControlLog, LogLevel::Debug, "HTMLMediaElement={}, " msg, \
              fmt::ptr(this), ##__VA_ARGS__)

#undef CONTROLLER_TIMER_LOG
#define CONTROLLER_TIMER_LOG(element, msg, ...)                               \
  MOZ_LOG_FMT(gMediaControlLog, LogLevel::Debug, "HTMLMediaElement={}, " msg, \
              fmt::ptr(element), ##__VA_ARGS__)

#define LOG(type, ...) \
  MOZ_LOG_FMT(gMediaElementLog, type, MOZ_LOG_EXPAND_ARGS __VA_ARGS__)
#define LOG_EVENT(type, ...) \
  MOZ_LOG_FMT(gMediaElementEventsLog, type, MOZ_LOG_EXPAND_ARGS __VA_ARGS__)

using namespace mozilla::layers;
using namespace mozilla::dom::HTMLMediaElement_Binding;

namespace mozilla::dom {

using AudibleState = AudioChannelService::AudibleState;
using SinkInfoPromise = MediaDevices::SinkInfoPromise;

// Number of milliseconds between progress events as defined by spec
static const uint32_t PROGRESS_MS = 350;

// Number of milliseconds of no data before a stall event is fired as defined by
// spec
static const uint32_t STALL_MS = 3000;

// Used by AudioChannel for suppresssing the volume to this ratio.
#define FADED_VOLUME_RATIO 0.25

// These constants are arbitrary
// Minimum playbackRate for a media
static const double MIN_PLAYBACKRATE = 1.0 / 16;
// Maximum playbackRate for a media
static const double MAX_PLAYBACKRATE = 16.0;

static double ClampPlaybackRate(double aPlaybackRate) {
  MOZ_ASSERT(aPlaybackRate >= 0.0);
  MOZ_ASSERT(std::isfinite(aPlaybackRate));

  if (aPlaybackRate == 0.0) {
    return aPlaybackRate;
  }
  if (aPlaybackRate < MIN_PLAYBACKRATE) {
    return MIN_PLAYBACKRATE;
  }
  if (aPlaybackRate > MAX_PLAYBACKRATE) {
    return MAX_PLAYBACKRATE;
  }
  return aPlaybackRate;
}

// Media error values.  These need to match the ones in MediaError.webidl.
static const unsigned short MEDIA_ERR_ABORTED = 1;
static const unsigned short MEDIA_ERR_NETWORK = 2;
static const unsigned short MEDIA_ERR_DECODE = 3;
static const unsigned short MEDIA_ERR_SRC_NOT_SUPPORTED = 4;

/**
 * EventBlocker helps media element to postpone the event delivery by storing
 * the event runner, and execute them once media element decides not to postpone
 * the event delivery. If media element never resumes the event delivery, then
 * those runner would be cancelled.
 * For example, we postpone the event delivery when media element entering to
 * the bf-cache.
 */
class HTMLMediaElement::EventBlocker final : public nsISupports {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_CLASS(EventBlocker)

  explicit EventBlocker(HTMLMediaElement* aElement) : mElement(aElement) {}

  void SetBlockEventDelivery(bool aShouldBlock) {
    MOZ_ASSERT(NS_IsMainThread());
    if (mShouldBlockEventDelivery == aShouldBlock) {
      return;
    }
    LOG_EVENT(LogLevel::Debug,
              ("{} {} event delivery", fmt::ptr(mElement.get()),
               mShouldBlockEventDelivery ? "block" : "unblock"));
    mShouldBlockEventDelivery = aShouldBlock;
    if (!mShouldBlockEventDelivery) {
      DispatchPendingMediaEvents();
    }
  }

  void PostponeEvent(nsMediaEventRunner* aRunner) {
    MOZ_ASSERT(NS_IsMainThread());
    // Element has been CCed, which would break the weak pointer.
    if (!mElement) {
      return;
    }
    MOZ_ASSERT(mShouldBlockEventDelivery);
    MOZ_ASSERT(mElement);
    LOG_EVENT(
        LogLevel::Debug,
        ("{} postpone runner {} for {}", fmt::ptr(mElement.get()),
         aRunner->Name(), NS_ConvertUTF16toUTF8(aRunner->EventName()).get()));
    mPendingEventRunners.AppendElement(aRunner);
  }

  void Shutdown() {
    MOZ_ASSERT(NS_IsMainThread());
    for (auto& runner : mPendingEventRunners) {
      runner->Cancel();
    }
    mPendingEventRunners.Clear();
  }

  bool ShouldBlockEventDelivery() const {
    MOZ_ASSERT(NS_IsMainThread());
    return mShouldBlockEventDelivery;
  }

  size_t SizeOfExcludingThis(MallocSizeOf& aMallocSizeOf) const {
    MOZ_ASSERT(NS_IsMainThread());
    size_t total = 0;
    for (const auto& runner : mPendingEventRunners) {
      total += aMallocSizeOf(runner);
    }
    return total;
  }

 private:
  ~EventBlocker() = default;

  void DispatchPendingMediaEvents() {
    MOZ_ASSERT(mElement);
    for (auto& runner : mPendingEventRunners) {
      LOG_EVENT(
          LogLevel::Debug,
          ("{} execute runner {} for {}", fmt::ptr(mElement.get()),
           runner->Name(), NS_ConvertUTF16toUTF8(runner->EventName()).get()));
      GetMainThreadSerialEventTarget()->Dispatch(runner.forget());
    }
    mPendingEventRunners.Clear();
  }

  WeakPtr<HTMLMediaElement> mElement;
  bool mShouldBlockEventDelivery = false;
  // Contains event runners which should not be run for now because we want
  // to block all events delivery. They would be dispatched once media element
  // decides unblocking them.
  nsTArray<RefPtr<nsMediaEventRunner>> mPendingEventRunners;
};

NS_IMPL_CYCLE_COLLECTION(HTMLMediaElement::EventBlocker, mPendingEventRunners)
NS_IMPL_CYCLE_COLLECTING_ADDREF(HTMLMediaElement::EventBlocker)
NS_IMPL_CYCLE_COLLECTING_RELEASE(HTMLMediaElement::EventBlocker)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(HTMLMediaElement::EventBlocker)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

/**
 * MediaControlKeyListener registers each HTMLMediaElement with the
 * ContentMediaAgent so it can report playback and audible state to the
 * parent-process MediaController, and receive media control keys.  The
 * listener is always started for playable media so all elements — controllable
 * or not — can receive keys such as Mute/Unmute that affect any source.
 * Playback-state notifications (eStarted/ePlayed/ePaused/eStopped) and
 * full-control keys (Play/Pause/Seek/Stop) are only issued when the source is
 * controllable; audibility is always reported regardless.  Doing so causes the
 * OS media-control interface (media keys, lock-screen widget, etc.) to appear
 * only for media that qualifies for full control.
 *
 * The listener starts when media first becomes audible (for controllable
 * sources — e.g. a video longer than the duration threshold that is not a
 * real-time stream) or immediately on play() (for non-controllable sources and
 * PiP/fullscreen media).  An inaudible video that is used purely as a
 * background gif, for example, would never trigger Start() because it never
 * produces sound and does not meet the controllable threshold.  The listener
 * stops when StopIfNeeded() is called, which happens when the element stops
 * playing or its browsing context changes.
 *
 * Use `Start()` to begin and `StopIfNeeded()` to end; any state notifications
 * to the controller MUST be made after a successful `Start()`.
 */
class HTMLMediaElement::MediaControlKeyListener final
    : public ContentMediaControlKeyReceiver {
 public:
  NS_INLINE_DECL_REFCOUNTING(MediaControlKeyListener, override)

  MOZ_INIT_OUTSIDE_CTOR explicit MediaControlKeyListener(
      HTMLMediaElement* aElement)
      : mElement(aElement), mElementId(nsID::GenerateUUID()) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aElement);
  }

  /**
   * Start listening to the media control keys which would make media being able
   * to be controlled via pressing media control keys.
   */
  void Start() {
    MOZ_ASSERT(NS_IsMainThread());
    if (IsStarted()) {
      // We have already been started, do not notify start twice.
      return;
    }

    // Fail to init media agent, we are not able to notify the media controller
    // any update and also are not able to receive media control key events.
    if (!InitMediaAgent()) {
      MEDIACONTROL_LOG("Failed to start due to not able to init media agent!");
      return;
    }

    if (mControlType == ControlType::eUncontrollable) {
      MEDIACONTROL_LOG("Non-controllable source; reporting audibility only");
      if (mIsOwnerAudible) {
        NotifyAudibleStateChanged(MediaAudibleState::eAudible);
      }
      return;
    }

    NotifyPlaybackStateChanged(MediaPlaybackState::eStarted);
    // If owner has started playing before the listener starts, we should update
    // the playing state as well. Eg. media starts inaudibly and becomes audible
    // later.
    if (!Owner()->Paused()) {
      // This may have been cleared on pause so resync it.
      mIsOwnerAudible = Owner()->IsAudible();
      NotifyMediaStartedPlaying();
    }
    if (StaticPrefs::media_mediacontrol_testingevents_enabled()) {
      auto dispatcher = MakeRefPtr<AsyncEventDispatcher>(
          Owner(), u"MozStartMediaControl"_ns, CanBubble::eYes,
          ChromeOnlyDispatch::eYes);
      dispatcher->PostDOMEvent();
    }
  }

  /**
   * Stop listening to the media control keys which would make media not be able
   * to be controlled via pressing media control keys. If we haven't started
   * listening to the media control keys, then nothing would happen.
   */
  void StopIfNeeded() {
    MOZ_ASSERT(NS_IsMainThread());
    if (!IsStarted()) {
      // We have already been stopped, do not notify stop twice.
      return;
    }
    if (mControlType == ControlType::eUncontrollable) {
      MEDIACONTROL_LOG("Stopping non-controllable source");
      if (mIsOwnerAudible) {
        NotifyAudibleStateChanged(MediaAudibleState::eInaudible);
      }
    } else {
      MEDIACONTROL_LOG("Stopping controllable source");
      NotifyMediaStoppedPlaying();
      NotifyPlaybackStateChanged(MediaPlaybackState::eStopped);
    }
    // A media-control mute can only be lifted by an Unmute key delivered to a
    // registered receiver, so drop it here; otherwise the element would stay
    // muted with no way to receive that key once it deregisters.
    Owner()->SetMuted(false, HTMLMediaElement::MUTED_BY_MEDIA_CONTROL);
    // Remove ourselves from media agent, which would stop receiving event.
    mControlAgent->RemoveReceiver(this, mControlType);
    mControlAgent = nullptr;
  }

  bool IsStarted() const { return mControlAgent != nullptr; }

  bool IsPlaying() const override {
    return Owner() ? !Owner()->Paused() : false;
  }

  /**
   * Following methods should only be used after starting listener.
   */
  void NotifyMediaStartedPlaying() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(IsStarted());
    if (mState == MediaPlaybackState::eStarted ||
        mState == MediaPlaybackState::ePaused) {
      NotifyPlaybackStateChanged(MediaPlaybackState::ePlayed);
      // If media is `inaudible` in the beginning, then we don't need to notify
      // the state, because notifying `inaudible` should always come after
      // notifying `audible`.
      if (mIsOwnerAudible) {
        NotifyAudibleStateChanged(MediaAudibleState::eAudible);
      }
    }
  }

  void NotifyMediaStoppedPlaying() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(IsStarted());
    if (mState == MediaPlaybackState::ePlayed) {
      NotifyPlaybackStateChanged(MediaPlaybackState::ePaused);
      // As media are going to be paused, so no sound is possible to be heard.
      if (mIsOwnerAudible) {
        NotifyAudibleStateChanged(MediaAudibleState::eInaudible);
      }
    }
  }

  void NotifyMediaPositionState() {
    if (!IsStarted()) {
      return;
    }

    MOZ_ASSERT(mControlAgent);
    auto* owner = Owner();
    // `Duration()` is NaN while the element has neither a decoder nor a
    // stream, and infinite for a stream. Neither is a length that a platform's
    // media controls can lay a timeline out from, so report no position state
    // at all instead.
    const double duration = owner->Duration();
    if (!std::isfinite(duration)) {
      MEDIACONTROL_LOG("Clear media position state (duration={})", duration);
      mControlAgent->UpdateGuessedPositionState(mOwnerBrowsingContextId,
                                                mElementId, Nothing());
      return;
    }
    PositionState state(duration, owner->Paused() ? 0.0 : owner->PlaybackRate(),
                        owner->CurrentTime(), TimeStamp::Now());
    MEDIACONTROL_LOG(
        "Notify media position state (duration={}, playbackRate={}, "
        "position={})",
        state.mDuration, state.mPlaybackRate,
        state.mLastReportedPlaybackPosition);
    mControlAgent->UpdateGuessedPositionState(mOwnerBrowsingContextId,
                                              mElementId, Some(state));
  }

  void Shutdown() {
    StopIfNeeded();
    if (!mControlAgent) {
      return;
    }
    mControlAgent->UpdateGuessedPositionState(mOwnerBrowsingContextId,
                                              mElementId, Nothing());
  }

  // This method can be called before the listener starts, which would cache
  // the audible state and update after the listener starts.
  void UpdateMediaAudibleState(bool aIsOwnerAudible) {
    MOZ_ASSERT(NS_IsMainThread());
    if (mIsOwnerAudible == aIsOwnerAudible) {
      return;
    }
    mIsOwnerAudible = aIsOwnerAudible;
    MEDIACONTROL_LOG("Media becomes {}",
                     mIsOwnerAudible ? "audible" : "inaudible");
    const MediaAudibleState newState = mIsOwnerAudible
                                           ? MediaAudibleState::eAudible
                                           : MediaAudibleState::eInaudible;
    // Controllable sources only report audibility once they reach the playing
    // state; non-controllable sources report it whenever the listener is
    // started, since they never reach the playing state.
    if (mState == MediaPlaybackState::ePlayed ||
        (IsStarted() && mControlType == ControlType::eUncontrollable)) {
      NotifyAudibleStateChanged(newState);
    }
  }

  void SetPictureInPictureModeEnabled(bool aIsEnabled) {
    MOZ_ASSERT(NS_IsMainThread());
    if (mIsPictureInPictureEnabled == aIsEnabled) {
      return;
    }
    // PIP state changes might happen before the listener starts or stops where
    // we haven't call `InitMediaAgent()` yet. Eg. Reset the PIP video's src,
    // then cancel the PIP. In addition, not like playback and audible state
    // which should be restricted to update via the same agent in order to keep
    // those states correct in each `ContextMediaInfo`, PIP state can be updated
    // through any browsing context, so we would use `ContentMediaAgent::Get()`
    // directly to update PIP state.
    mIsPictureInPictureEnabled = aIsEnabled;
    if (RefPtr<IMediaInfoUpdater> updater =
            ContentMediaAgent::Get(GetCurrentBrowsingContext())) {
      updater->SetIsInPictureInPictureMode(mOwnerBrowsingContextId,
                                           mIsPictureInPictureEnabled);
    }
  }

  void HandleMediaKey(MediaControlKey aKey,
                      const MediaControlActionParams& aParams) override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(IsStarted());
    MEDIACONTROL_LOG("HandleEvent '{}'", GetEnumString(aKey).get());
    switch (aKey) {
      case MediaControlKey::Play:
        Owner()->Play();
        break;
      case MediaControlKey::Pause:
        Owner()->Pause();
        break;
      case MediaControlKey::Stop:
        Owner()->Pause();
        StopIfNeeded();
        break;
      case MediaControlKey::Seekto:
        MOZ_ASSERT(aParams.mAbsolute);
        if (aParams.mAbsolute->mFastSeek) {
          Owner()->FastSeek(aParams.mAbsolute->mSeekTime, IgnoreErrors());
        } else {
          Owner()->SetCurrentTime(aParams.mAbsolute->mSeekTime);
        }
        break;
      case MediaControlKey::Seekforward:
        MOZ_ASSERT(aParams.mRelativeSeekOffset);
        Owner()->SetCurrentTime(Owner()->CurrentTime() +
                                aParams.mRelativeSeekOffset.value());
        break;
      case MediaControlKey::Seekbackward:
        MOZ_ASSERT(aParams.mRelativeSeekOffset);
        Owner()->SetCurrentTime(Owner()->CurrentTime() -
                                aParams.mRelativeSeekOffset.value());
        break;
      case MediaControlKey::Setvolume:
        MOZ_ASSERT(aParams.mVolume);
        Owner()->SetVolume(aParams.mVolume.value(), IgnoreErrors());
        break;
      case MediaControlKey::Mute:
        Owner()->SetMuted(true, HTMLMediaElement::MUTED_BY_MEDIA_CONTROL);
        break;
      case MediaControlKey::Unmute:
        Owner()->SetMuted(false, HTMLMediaElement::MUTED_BY_MEDIA_CONTROL);
        break;
      default:
        MOZ_ASSERT_UNREACHABLE(
            "Unsupported media control key for media element!");
    }
  }

  void SuspendForInterrupt(AudioSessionInterruptKind aKind) override {
    MOZ_ASSERT(NS_IsMainThread());
    // Only an audible, playing element yields to an interruption; a paused or
    // inaudible element (muted, volume 0, or no audio track) is left alone.
    const bool willPause = Owner() && !Owner()->Paused() && mIsOwnerAudible;
    MEDIACONTROL_LOG("SuspendForInterrupt {}, pause={}",
                     dom::EnumValueToString(aKind), willPause);
    if (willPause) {
      // Pausing clears mSuspendedByInterrupt (see NotifyPaused); set it back
      // right after our own pause so ResumeFromInterrupt knows this element is
      // the one the interruption suspended.
      Owner()->Pause();
      mSuspendedByInterrupt = true;
    }
    if (aKind == AudioSessionInterruptKind::Permanent) {
      mSuspendedByInterrupt = false;
    }
  }

  void ResumeFromInterrupt() override {
    MOZ_ASSERT(NS_IsMainThread());
    const bool willResume =
        mSuspendedByInterrupt && Owner() && Owner()->Paused();
    MEDIACONTROL_LOG("ResumeFromInterrupt, resume={}", willResume);
    if (willResume) {
      Owner()->Play();
      glean::media_audio_focus::resume_decision.Get("media"_ns).Add(1);
    }
    mSuspendedByInterrupt = false;
  }

  void NotifyPaused() {
    MEDIACONTROL_LOG("NotifyPaused, clearing suspended-by-interrupt");
    mSuspendedByInterrupt = false;
  }

  void UpdateOwnerBrowsingContextIfNeeded() {
    // Has not notified any information about the owner context yet.
    if (!IsStarted()) {
      return;
    }

    BrowsingContext* currentBC = GetCurrentBrowsingContext();
    MOZ_ASSERT(currentBC);
    // Still in the same browsing context, no need to update.
    if (currentBC->Id() == mOwnerBrowsingContextId) {
      return;
    }
    MEDIACONTROL_LOG("Change browsing context from {} to {}",
                     mOwnerBrowsingContextId, currentBC->Id());
    // This situation would happen when we start a media in an original browsing
    // context, then we move it to another browsing context, such as an iframe,
    // so its owner browsing context would be changed. Therefore, we should
    // reset the media status for the previous browsing context by calling
    // `Stop()`, in which the listener would notify `ePaused` (if it's playing)
    // and `eStop`. Then calls `Start()`, in which the listener would notify
    // `eStart` to the new browsing context. If the media was playing before,
    // we would also notify `ePlayed`.
    bool wasInPlayingState = mState == MediaPlaybackState::ePlayed;
    StopIfNeeded();
    Start();
    if (wasInPlayingState) {
      NotifyMediaStartedPlaying();
    }
  }

 private:
  ~MediaControlKeyListener() = default;

  // The media can be moved around different browsing contexts, so this context
  // might be different from the one that we used to initialize
  // `ContentMediaAgent`.
  BrowsingContext* GetCurrentBrowsingContext() const {
    // Owner has been CCed, which would break the link of the weaker pointer.
    if (!Owner()) {
      return nullptr;
    }
    nsPIDOMWindowInner* window = Owner()->OwnerDoc()->GetInnerWindow();
    return window ? window->GetBrowsingContext() : nullptr;
  }

  bool InitMediaAgent() {
    MOZ_ASSERT(NS_IsMainThread());
    BrowsingContext* currentBC = GetCurrentBrowsingContext();
    mControlAgent = ContentMediaAgent::Get(currentBC);
    if (!mControlAgent) {
      return false;
    }
    MOZ_ASSERT(currentBC);
    mOwnerBrowsingContextId = currentBC->Id();
    mControlType = Owner()->IsControllableMediaSource()
                       ? ControlType::eControllable
                       : ControlType::eUncontrollable;
    MEDIACONTROL_LOG("Init agent in browsing context {}",
                     mOwnerBrowsingContextId);
    mControlAgent->AddReceiver(this, mControlType);
    return true;
  }

  HTMLMediaElement* Owner() const {
    // `mElement` would be clear during CC unlinked, but it would only happen
    // after stopping the listener.
    MOZ_ASSERT(mElement || !IsStarted());
    return mElement.get();
  }

  void NotifyPlaybackStateChanged(MediaPlaybackState aState) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mControlAgent);
    MEDIACONTROL_LOG("NotifyMediaState from state='{}' to state='{}'",
                     dom::EnumValueToString(mState),
                     dom::EnumValueToString(aState));
    MOZ_ASSERT(mState != aState, "Should not notify same state again!");
    mState = aState;
    mControlAgent->NotifyMediaPlaybackChanged(mOwnerBrowsingContextId, mState);

    if (aState == MediaPlaybackState::ePlayed ||
        aState == MediaPlaybackState::ePaused) {
      NotifyMediaPositionState();
    }
  }

  void NotifyAudibleStateChanged(MediaAudibleState aState) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(IsStarted());
    mControlAgent->NotifyMediaAudibleChanged(mOwnerBrowsingContextId, aState,
                                             mControlType,
                                             AudioSessionType::Playback);
  }

  MediaPlaybackState mState = MediaPlaybackState::eStopped;
  WeakPtr<HTMLMediaElement> mElement;
  RefPtr<ContentMediaAgent> mControlAgent;
  bool mIsPictureInPictureEnabled = false;
  bool mIsOwnerAudible = false;
  // The control type the listener registered with; set when the agent is
  // initialized and used to route audibility notifications and receiver
  // removal. Uncontrollable sources report audibility but not playback state.
  ControlType mControlType = ControlType::eControllable;
  // True while this listener paused its element for an audio-focus
  // interruption, so ResumeFromInterrupt only resumes an element it paused.
  bool mSuspendedByInterrupt = false;
  MOZ_INIT_OUTSIDE_CTOR uint64_t mOwnerBrowsingContextId = 0;
  const nsID mElementId;
};

class HTMLMediaElement::MediaStreamTrackListener
    : public DOMMediaStream::TrackListener {
 public:
  explicit MediaStreamTrackListener(HTMLMediaElement* aElement)
      : mElement(aElement) {}

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(MediaStreamTrackListener,
                                           DOMMediaStream::TrackListener)

  void NotifyTrackAdded(const RefPtr<MediaStreamTrack>& aTrack) override {
    if (!mElement) {
      return;
    }
    mElement->NotifyMediaStreamTrackAdded(aTrack);
  }

  void NotifyTrackRemoved(const RefPtr<MediaStreamTrack>& aTrack) override {
    if (!mElement) {
      return;
    }
    mElement->NotifyMediaStreamTrackRemoved(aTrack);
  }

  void OnActive() {
    MOZ_ASSERT(mElement);

    // mediacapture-main says:
    // Note that once ended equals true the HTMLVideoElement will not play media
    // even if new MediaStreamTracks are added to the MediaStream (causing it to
    // return to the active state) unless autoplay is true or the web
    // application restarts the element, e.g., by calling play().
    //
    // This is vague on exactly how to go from becoming active to playing, when
    // autoplaying. However, per the media element spec, to play an autoplaying
    // media element, we must load the source and reach readyState
    // HAVE_ENOUGH_DATA [1]. Hence, a MediaStream being assigned to a media
    // element and becoming active runs the load algorithm, so that it can
    // eventually be played.
    //
    // [1]
    // https://html.spec.whatwg.org/multipage/media.html#ready-states:event-media-play

    LOG(LogLevel::Debug,
        ("{}, mSrcStream {} became active, checking if we "
         "need to run the load algorithm",
         fmt::ptr(mElement.get()), fmt::ptr(mElement->mSrcStream.get())));
    if (!mElement->IsPlaybackEnded()) {
      return;
    }
    if (!mElement->Autoplay()) {
      return;
    }
    LOG(LogLevel::Info,
        ("{}, mSrcStream {} became active on autoplaying, "
         "ended element. Reloading.",
         fmt::ptr(mElement.get()), fmt::ptr(mElement->mSrcStream.get())));
    mElement->DoLoad();
  }

  void NotifyActive() override {
    if (!mElement) {
      return;
    }

    if (!mElement->IsVideo()) {
      // Audio elements use NotifyAudible().
      return;
    }

    OnActive();
  }

  void NotifyAudible() override {
    if (!mElement) {
      return;
    }

    if (mElement->IsVideo()) {
      // Video elements use NotifyActive().
      return;
    }

    OnActive();
  }

  void OnInactive() {
    MOZ_ASSERT(mElement);

    if (mElement->IsPlaybackEnded()) {
      return;
    }
    LOG(LogLevel::Debug,
        ("{}, mSrcStream {} became inactive", fmt::ptr(mElement.get()),
         fmt::ptr(mElement->mSrcStream.get())));

    mElement->PlaybackEnded();
  }

  void NotifyInactive() override {
    if (!mElement) {
      return;
    }

    if (!mElement->IsVideo()) {
      // Audio elements use NotifyInaudible().
      return;
    }

    OnInactive();
  }

  void NotifyInaudible() override {
    if (!mElement) {
      return;
    }

    if (mElement->IsVideo()) {
      // Video elements use NotifyInactive().
      return;
    }

    OnInactive();
  }

 protected:
  virtual ~MediaStreamTrackListener() = default;
  RefPtr<HTMLMediaElement> mElement;
};

NS_IMPL_CYCLE_COLLECTION_INHERITED(HTMLMediaElement::MediaStreamTrackListener,
                                   DOMMediaStream::TrackListener, mElement)
NS_IMPL_ADDREF_INHERITED(HTMLMediaElement::MediaStreamTrackListener,
                         DOMMediaStream::TrackListener)
NS_IMPL_RELEASE_INHERITED(HTMLMediaElement::MediaStreamTrackListener,
                          DOMMediaStream::TrackListener)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(
    HTMLMediaElement::MediaStreamTrackListener)
NS_INTERFACE_MAP_END_INHERITING(DOMMediaStream::TrackListener)

/**
 * Helper class that manages audio and video outputs for all enabled tracks in a
 * media element. It also manages calculating the current time when playing a
 * MediaStream.
 */
class HTMLMediaElement::MediaStreamRenderer {
 public:
  NS_INLINE_DECL_REFCOUNTING(MediaStreamRenderer)

  MediaStreamRenderer(AbstractThread* aMainThread,
                      VideoFrameContainer* aVideoContainer,
                      FirstFrameVideoOutput* aFirstFrameVideoOutput,
                      void* aAudioOutputKey)
      : mVideoContainer(aVideoContainer),
        mAudioOutputKey(aAudioOutputKey),
        mWatchManager(this, aMainThread),
        mGraphCurrentTime(aMainThread, 0,
                          "MediaStreamRenderer::mGraphCurrentTime"),
        mFirstFrameVideoOutput(aFirstFrameVideoOutput) {
    if (mFirstFrameVideoOutput) {
      mWatchManager.Watch(mFirstFrameVideoOutput->mFirstFrameRendered,
                          &MediaStreamRenderer::SetFirstFrameRendered);
      mWatchManager.Watch(mFirstFrameVideoOutput->mAttachment,
                          &MediaStreamRenderer::UpdateVideoTrackListeners);
    }
    mWatchManager.Watch(mVideoTrack,
                        &MediaStreamRenderer::UpdateVideoTrackListeners);
    mWatchManager.Watch(mRendering,
                        &MediaStreamRenderer::UpdateVideoTrackListeners);
  }

  void Shutdown() {
    for (const auto& t : mAudioTracks.Clone()) {
      if (t) {
        RemoveTrack(t->AsAudioStreamTrack());
      }
    }
    if (mVideoTrack.Ref()) {
      RemoveTrack(mVideoTrack.Ref()->AsVideoStreamTrack());
    }
    mWatchManager.Shutdown();
    mGraphCurrentTime.DisconnectIfConnected();
    if (mGraphTimeTracker && !mGraphTimeTracker->IsDestroyed()) {
      mGraphTimeTracker->Destroy();
    }
    mFirstFrameVideoOutput = nullptr;
    mVideoOutput = nullptr;
  }

  void UpdateCurrentTime() {
    if (!mProgressingCurrentTime) {
      // The graph may still advance mGraphTimeTracker until it has been
      // suspended on the graph thread. currentTime must not change meanwhile.
      return;
    }
    mCurrentTime = mGraphCurrentTime.Ref();
  }

  void SetFirstFrameRendered() {
    if (!mFirstFrameVideoOutput) {
      return;
    }
    if (mVideoTrack.Ref()) {
      mVideoTrack.Ref()->AsVideoStreamTrack()->RemoveVideoOutput(
          mFirstFrameVideoOutput);
    }
    mWatchManager.Unwatch(mFirstFrameVideoOutput->mFirstFrameRendered,
                          &MediaStreamRenderer::SetFirstFrameRendered);
    mWatchManager.Unwatch(mFirstFrameVideoOutput->mAttachment,
                          &MediaStreamRenderer::UpdateVideoTrackListeners);
    mFirstFrameVideoOutput = nullptr;
  }

  void SetProgressingCurrentTime(bool aProgress) {
    if (aProgress == mProgressingCurrentTime) {
      return;
    }

    MOZ_DIAGNOSTIC_ASSERT(mGraphTimeTracker);
    mProgressingCurrentTime = aProgress;
    if (mProgressingCurrentTime) {
      // The time may have changed on the graph since a prior suspend. If so,
      // this fires the first time update immediately.
      mCurrentTime = mGraphCurrentTime.Ref();
      mGraphTimeTracker->Resume();
    } else {
      mGraphTimeTracker->Suspend();
    }
  }

  void Start() {
    if (mRendering) {
      return;
    }

    LOG(LogLevel::Info, ("MediaStreamRenderer={} Start", fmt::ptr(this)));
    mRendering = true;

    if (!mGraphTimeTracker) {
      return;
    }

    for (const auto& t : mAudioTracks) {
      if (t) {
        t->AsAudioStreamTrack()->AddAudioOutput(mAudioOutputKey,
                                                mAudioOutputSink);
        t->AsAudioStreamTrack()->SetAudioOutputVolume(mAudioOutputKey,
                                                      mAudioOutputVolume);
      }
    }
    // Video attachment is handled by UpdateVideoTrackListeners via mRendering
    // Watchable.
  }

  void Stop() {
    if (!mRendering) {
      return;
    }

    LOG(LogLevel::Info, ("MediaStreamRenderer={} Stop", fmt::ptr(this)));
    mRendering = false;

    if (!mGraphTimeTracker) {
      return;
    }

    for (const auto& t : mAudioTracks) {
      if (t) {
        t->AsAudioStreamTrack()->RemoveAudioOutput(mAudioOutputKey);
      }
    }
    // There is no longer an audio output that needs the device so the
    // device may not start.  Ensure the promise is resolved.
    ResolveAudioDevicePromiseIfExists(__func__);

    if (mVideoTrack.Ref() && mVideoOutput) {
      // This is fine even if mVideoOutput is not attached to mVideoTrack (might
      // be pending removal from an earlier mVideoTrack), as RemoveVideoOutput
      // is idempotent.
      mVideoTrack.Ref()->AsVideoStreamTrack()->RemoveVideoOutput(mVideoOutput);
    }
  }

  void SetAudioOutputVolume(float aVolume) {
    if (mAudioOutputVolume == aVolume) {
      return;
    }
    mAudioOutputVolume = aVolume;
    if (!mRendering) {
      return;
    }
    for (const auto& t : mAudioTracks) {
      if (t) {
        t->AsAudioStreamTrack()->SetAudioOutputVolume(mAudioOutputKey,
                                                      mAudioOutputVolume);
      }
    }
  }

  RefPtr<GenericPromise> SetAudioOutputDevice(AudioDeviceInfo* aSink) {
    MOZ_ASSERT(aSink);
    MOZ_ASSERT(mAudioOutputSink != aSink);
    LOG(LogLevel::Info,
        ("MediaStreamRenderer={} SetAudioOutputDevice name={}\n",
         fmt::ptr(this), NS_ConvertUTF16toUTF8(aSink->Name()).get()));

    mAudioOutputSink = aSink;

    if (!mRendering) {
      MOZ_ASSERT(mSetAudioDevicePromise.IsEmpty());
      return GenericPromise::CreateAndResolve(true, __func__);
    }

    nsTArray<RefPtr<GenericPromise>> promises;
    for (const auto& t : mAudioTracks) {
      t->AsAudioStreamTrack()->RemoveAudioOutput(mAudioOutputKey);
      promises.AppendElement(t->AsAudioStreamTrack()->AddAudioOutput(
          mAudioOutputKey, mAudioOutputSink));
      t->AsAudioStreamTrack()->SetAudioOutputVolume(mAudioOutputKey,
                                                    mAudioOutputVolume);
    }
    if (!promises.Length()) {
      // Not active track, save it for later
      MOZ_ASSERT(mSetAudioDevicePromise.IsEmpty());
      return GenericPromise::CreateAndResolve(true, __func__);
    }

    // Resolve any existing promise for a previous device so that promises
    // resolve in order of setSinkId() invocation.
    ResolveAudioDevicePromiseIfExists(__func__);

    RefPtr promise = mSetAudioDevicePromise.Ensure(__func__);
    GenericPromise::AllSettled(AbstractThread::MainThread(), promises)
        ->Then(GetMainThreadSerialEventTarget(), __func__,
               [self = RefPtr{this},
                this](const GenericPromise::AllSettledPromiseType::
                          ResolveOrRejectValue& aValue) {
                 // This handler should have been disconnected if
                 // mSetAudioDevicePromise has been settled.
                 MOZ_ASSERT(!mSetAudioDevicePromise.IsEmpty());
                 mDeviceStartedRequest.Complete();
                 // The AudioStreamTrack::AddAudioOutput() promise is rejected
                 // either when the graph no longer needs the device, in which
                 // case this handler would have already been disconnected, or
                 // the graph is force shutdown.
                 // mSetAudioDevicePromise is resolved regardless of whether
                 // the AddAudioOutput() promises resolve or reject because
                 // the underlying device has been changed.
                 LOG(LogLevel::Info,
                     ("MediaStreamRenderer={} SetAudioOutputDevice settled",
                      fmt::ptr(this)));
                 mSetAudioDevicePromise.Resolve(true, __func__);
               })
        ->Track(mDeviceStartedRequest);

    return promise;
  }

  void AddTrack(AudioStreamTrack* aTrack) {
    MOZ_DIAGNOSTIC_ASSERT(!mAudioTracks.Contains(aTrack));
    mAudioTracks.AppendElement(aTrack);
    EnsureGraphTimeTracker();
    if (mRendering) {
      aTrack->AddAudioOutput(mAudioOutputKey, mAudioOutputSink);
      aTrack->SetAudioOutputVolume(mAudioOutputKey, mAudioOutputVolume);
    }
  }
  void AddTrack(VideoStreamTrack* aTrack) {
    MOZ_DIAGNOSTIC_ASSERT(!mVideoTrack.Ref());
    if (!mVideoContainer) {
      return;
    }
    mVideoTrack = aTrack;
    EnsureGraphTimeTracker();
    // Video output attachment is handled by UpdateVideoTrackListeners via the
    // mVideoTrack Watchable, when mVideoContainer is known to not be in use
    // from a VideoOutput for a previous track on a potentially different
    // MediaTrackGraph.
  }

  void RemoveTrack(AudioStreamTrack* aTrack) {
    MOZ_DIAGNOSTIC_ASSERT(mAudioTracks.Contains(aTrack));
    if (mRendering) {
      aTrack->RemoveAudioOutput(mAudioOutputKey);
    }
    mAudioTracks.RemoveElement(aTrack);

    if (mAudioTracks.IsEmpty()) {
      // There is no longer an audio output that needs the device so the
      // device may not start.  Ensure the promise is resolved.
      ResolveAudioDevicePromiseIfExists(__func__);
    }
  }
  void RemoveTrack(VideoStreamTrack* aTrack) {
    MOZ_DIAGNOSTIC_ASSERT(mVideoTrack.Ref() == aTrack);
    if (!mVideoContainer) {
      return;
    }
    if (mFirstFrameVideoOutput) {
      aTrack->RemoveVideoOutput(mFirstFrameVideoOutput);
    }
    if (mRendering && mVideoOutput) {
      // This is fine even if mVideoOutput is not attached to mVideoTrack (might
      // be pending removal from an earlier mVideoTrack), as RemoveVideoOutput
      // is idempotent.
      aTrack->RemoveVideoOutput(mVideoOutput);
    }
    mVideoTrack = nullptr;
  }

  double CurrentTime() const {
    if (!mGraphTimeTracker) {
      return 0.0;
    }

    return mGraphTimeTracker->TrackTimeToSeconds(mCurrentTime);
  }

  Watchable<TrackTime>& CurrentTrackTime() { return mCurrentTime; }

  // Set if we're rendering video.
  const RefPtr<VideoFrameContainer> mVideoContainer;

  // Set if we're rendering audio, nullptr otherwise.
  void* const mAudioOutputKey;

 private:
  ~MediaStreamRenderer() { Shutdown(); }

  void EnsureGraphTimeTracker() {
    if (mGraphTimeTracker) {
      return;
    }

    MediaTrackGraph* graph = nullptr;
    for (const auto& t : mAudioTracks) {
      if (t && !t->Ended()) {
        graph = t->Graph();
        break;
      }
    }

    if (!graph && mVideoTrack.Ref() && !mVideoTrack.Ref()->Ended()) {
      graph = mVideoTrack.Ref()->Graph();
    }

    if (!graph) {
      return;
    }

    mGraphTimeTracker = graph->CreateSourceTrack(
        MediaSegment::VIDEO, {SourceMediaTrack::Flag::EnableCanonicals});
    mGraphTimeTracker->Suspend();
    mGraphCurrentTime.Connect(&mGraphTimeTracker->CanonicalCurrentTime());
    mWatchManager.Watch(mGraphCurrentTime,
                        &MediaStreamRenderer::UpdateCurrentTime);
  }

  void UpdateVideoTrackListeners() {
    if (mFirstFrameVideoOutput &&
        mFirstFrameVideoOutput->mAttachment == VideoOutput::State::Detached &&
        mVideoTrack.Ref()) {
      // The first-frame VideoOutput is detached but should attach to
      // mVideoTrack. Attach now.
      // If we are rendering, then a subsequently registered mVideoOutput will
      // overwrite the frame from mFirstFrameVideoOutput with the same frame
      // (and possibly more frames).
      MOZ_ASSERT_IF(mVideoOutput,
                    mVideoOutput->mAttachment != VideoOutput::State::Attached);
      mVideoTrack.Ref()->AsVideoStreamTrack()->AddVideoOutput(
          mFirstFrameVideoOutput);
    }
    if (mVideoOutput &&
        mVideoOutput->mAttachment == VideoOutput::State::Detached) {
      // mVideoOutput became detached. Clear it.
      mWatchManager.Unwatch(mVideoOutput->mAttachment,
                            &MediaStreamRenderer::UpdateVideoTrackListeners);
      mVideoOutput = nullptr;
    }
    if (mRendering && mVideoTrack.Ref() && !mVideoOutput) {
      // There is no attached VideoOutput but there should be one because we are
      // rendering a track. Create one and attach now.
      MOZ_ASSERT_IF(
          mFirstFrameVideoOutput,
          mFirstFrameVideoOutput->mAttachment == VideoOutput::State::Attached);
      RefPtr o = new VideoOutput(mVideoContainer, AbstractThread::MainThread());
      mVideoTrack.Ref()->AsVideoStreamTrack()->AddVideoOutput(o);
      if (o->mAttachment == VideoOutput::State::Attached) {
        mVideoOutput = std::move(o);
        mWatchManager.Watch(mVideoOutput->mAttachment,
                            &MediaStreamRenderer::UpdateVideoTrackListeners);
      }
    }
  }

  void ResolveAudioDevicePromiseIfExists(StaticString aMethodName) {
    if (mSetAudioDevicePromise.IsEmpty()) {
      return;
    }
    LOG(LogLevel::Info, ("MediaStreamRenderer={} resolve audio device promise",
                         fmt::ptr(this)));
    mSetAudioDevicePromise.Resolve(true, aMethodName);
    mDeviceStartedRequest.Disconnect();
  }

  // True when all tracks are being rendered, i.e., when the media element is
  // playing.
  Watchable<bool> mRendering = {false, "MediaStreamRenderer::mRendering"};

  // True while we're progressing mCurrentTime. False otherwise.
  bool mProgressingCurrentTime = false;

  // The audio output volume for all audio tracks.
  float mAudioOutputVolume = 1.0f;

  // The sink device for all audio tracks.
  RefPtr<AudioDeviceInfo> mAudioOutputSink;
  // The promise returned from SetAudioOutputDevice() when an output is
  // active.
  MozPromiseHolder<GenericPromise> mSetAudioDevicePromise;
  // Request tracking the promise to indicate when the device passed to
  // SetAudioOutputDevice() is running.
  MozPromiseRequestHolder<GenericPromise::AllSettledPromiseType>
      mDeviceStartedRequest;

  WatchManager<MediaStreamRenderer> mWatchManager;

  // Mirror of mGraphTimeTracker's current time.
  Mirror<TrackTime> mGraphCurrentTime;

  // A MediaTrack to measure the duration for which we have been rendering media
  // on the graph. Set when the first track is added, never unset.
  RefPtr<SourceMediaTrack> mGraphTimeTracker;

  // Watchable that relays mGraphCurrentTime, the mirrored current time of
  // mGraphTimeTracker, to the media element only while mProgressingCurrentTime
  // is set. This is the current time of the rendering in TrackTime units.
  Watchable<TrackTime> mCurrentTime = {0, "MediaStreamRenderer::mCurrentTime"};

  // Currently enabled (and rendered) audio tracks.
  nsTArray<WeakPtr<MediaStreamTrack>> mAudioTracks;

  // Currently selected video track. Attachment of VideoOutputs to the track is
  // deferred to UpdateVideoTrackListeners.
  Watchable<WeakPtr<MediaStreamTrack>> mVideoTrack = {
      nullptr, "MediaStreamRenderer::mVideoTrack"};

  // Holds a reference to the first-frame-getting video output attached to
  // mVideoTrack. Set by the constructor, unset when the media element tells us
  // it has rendered the first frame.
  RefPtr<FirstFrameVideoOutput> mFirstFrameVideoOutput;

  // Reference to the video output currently attached to a video track. A new
  // mVideoTrack gets a new VideoOutput such that it sends frames with a new
  // ProducerID. Detaching from mVideoTrack is async, so even if mVideoTrack
  // changes, this remains until it has detached asynchronously from the
  // underlying track.
  RefPtr<VideoOutput> mVideoOutput;
};

static uint32_t sDecoderCaptureSourceId = 0;
static uint32_t sStreamCaptureSourceId = 0;
class HTMLMediaElement::MediaElementTrackSource
    : public MediaStreamTrackSource,
      public MediaStreamTrackSource::Sink,
      public MediaStreamTrackConsumer {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(MediaElementTrackSource,
                                           MediaStreamTrackSource)

  /* MediaDecoder track source */
  MediaElementTrackSource(HTMLMediaElement* aOwner, ProcessedMediaTrack* aTrack,
                          nsIPrincipal* aPrincipal, OutputMuteState aMuteState,
                          bool aHasAlpha)
      : MediaStreamTrackSource(
            aPrincipal, nsString(),
            TrackingId(TrackingId::Source::MediaElementDecoder,
                       sDecoderCaptureSourceId++,
                       TrackingId::TrackAcrossProcesses::Yes)),
        mOwner(aOwner),
        mTrack(aTrack),
        mIntendedElementMuteState(aMuteState),
        mElementMuteState(aMuteState),
        mMediaDecoderHasAlpha(Some(aHasAlpha)) {
    MOZ_ASSERT(mTrack);
  }

  /* MediaStream track source */
  MediaElementTrackSource(HTMLMediaElement* aOwner,
                          MediaStreamTrack* aCapturedTrack,
                          MediaStreamTrackSource* aCapturedTrackSource,
                          ProcessedMediaTrack* aTrack, MediaInputPort* aPort,
                          OutputMuteState aMuteState)
      : MediaStreamTrackSource(
            aCapturedTrackSource->GetPrincipal(), nsString(),
            TrackingId(TrackingId::Source::MediaElementStream,
                       sStreamCaptureSourceId++,
                       TrackingId::TrackAcrossProcesses::Yes)),
        mOwner(aOwner),
        mCapturedTrack(aCapturedTrack),
        mCapturedTrackSource(aCapturedTrackSource),
        mTrack(aTrack),
        mPort(aPort),
        mIntendedElementMuteState(aMuteState),
        mElementMuteState(aMuteState) {
    MOZ_ASSERT(mTrack);
    MOZ_ASSERT(mCapturedTrack);
    MOZ_ASSERT(mCapturedTrackSource);
    MOZ_ASSERT(mPort);

    mCapturedTrack->AddConsumer(this);
    mCapturedTrackSource->RegisterSink(this);
  }

  void SetEnabled(bool aEnabled) {
    if (!mTrack) {
      return;
    }
    mTrack->SetDisabledTrackMode(aEnabled ? DisabledTrackMode::ENABLED
                                          : DisabledTrackMode::SILENCE_FREEZE);
  }

  void SetPrincipal(RefPtr<nsIPrincipal> aPrincipal) {
    mPrincipal = std::move(aPrincipal);
    MediaStreamTrackSource::PrincipalChanged();
  }

  void SetMutedByElement(OutputMuteState aMuteState) {
    if (mIntendedElementMuteState == aMuteState) {
      return;
    }
    mIntendedElementMuteState = aMuteState;
    GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
        "MediaElementTrackSource::SetMutedByElement",
        [self = RefPtr<MediaElementTrackSource>(this), this, aMuteState] {
          mElementMuteState = aMuteState;
          MediaStreamTrackSource::MutedChanged(Muted());
        }));
  }

  void Destroy() override {
    if (mCapturedTrack) {
      mCapturedTrack->RemoveConsumer(this);
      mCapturedTrack = nullptr;
    }
    if (mCapturedTrackSource) {
      mCapturedTrackSource->UnregisterSink(this);
      mCapturedTrackSource = nullptr;
    }
    if (mTrack && !mTrack->IsDestroyed()) {
      mTrack->Destroy();
    }
    if (mPort) {
      mPort->Destroy();
      mPort = nullptr;
    }
  }

  MediaSourceEnum GetMediaSource() const override {
    return MediaSourceEnum::Other;
  }

  void Stop() override {
    // Do nothing. There may appear new output streams
    // that need tracks sourced from this source, so we
    // cannot destroy things yet.
  }

  /**
   * Do not keep the track source alive. The source lifetime is controlled by
   * its associated tracks.
   */
  bool KeepsSourceAlive() const override { return false; }

  /**
   * Do not keep the track source on. It is controlled by its associated tracks.
   */
  bool Enabled() const override { return false; }

  void Disable() override {}

  void Enable() override {}

  void PrincipalChanged() override {
    if (!mCapturedTrackSource) {
      // This could happen during shutdown.
      return;
    }

    SetPrincipal(mCapturedTrackSource->GetPrincipal());
  }

  void MutedChanged(bool aNewState) override {
    MediaStreamTrackSource::MutedChanged(Muted());
  }

  void ConstraintsChanged(const MediaTrackConstraints& aConstraints) override {}

  void OverrideEnded() override {
    Destroy();
    MediaStreamTrackSource::OverrideEnded();
  }

  void NotifyEnabledChanged(MediaStreamTrack* aTrack, bool aEnabled) override {
    MediaStreamTrackSource::MutedChanged(Muted());
  }

  bool Muted() const {
    return mElementMuteState == OutputMuteState::Muted ||
           (mCapturedTrack &&
            (mCapturedTrack->Muted() || !mCapturedTrack->Enabled()));
  }

  bool HasAlpha() const override {
    if (mCapturedTrack) {
      return mCapturedTrack->AsVideoStreamTrack()
                 ? mCapturedTrack->AsVideoStreamTrack()->HasAlpha()
                 : false;
    }
    return mMediaDecoderHasAlpha.valueOr(false);
  }

  void GetSettings(dom::MediaTrackSettings& aResult) override {
    if (!mOwner) {
      return;
    }

    auto* elem = mOwner->AsHTMLVideoElement();
    if (!elem) {
      return;
    }

    aResult.mWidth.Construct(elem->VideoWidth());
    aResult.mHeight.Construct(elem->VideoHeight());
  }

  ProcessedMediaTrack* Track() const { return mTrack; }

 private:
  virtual ~MediaElementTrackSource() { Destroy(); };

  WeakPtr<HTMLMediaElement> mOwner;
  RefPtr<MediaStreamTrack> mCapturedTrack;
  RefPtr<MediaStreamTrackSource> mCapturedTrackSource;
  const RefPtr<ProcessedMediaTrack> mTrack;
  RefPtr<MediaInputPort> mPort;
  // The mute state as intended by the media element.
  OutputMuteState mIntendedElementMuteState;
  // The mute state as applied to this track source. It is applied async, so
  // needs to be tracked separately from the intended state.
  OutputMuteState mElementMuteState;
  // Some<bool> if this is a MediaDecoder track source.
  const Maybe<bool> mMediaDecoderHasAlpha;
};

HTMLMediaElement::OutputMediaStream::OutputMediaStream(
    RefPtr<DOMMediaStream> aStream, bool aCapturingAudioOnly,
    bool aFinishWhenEnded)
    : mStream(std::move(aStream)),
      mCapturingAudioOnly(aCapturingAudioOnly),
      mFinishWhenEnded(aFinishWhenEnded) {}
HTMLMediaElement::OutputMediaStream::~OutputMediaStream() = default;

void ImplCycleCollectionTraverse(nsCycleCollectionTraversalCallback& aCallback,
                                 HTMLMediaElement::OutputMediaStream& aField,
                                 const char* aName, uint32_t aFlags) {
  ImplCycleCollectionTraverse(aCallback, aField.mStream, "mStream", aFlags);
  ImplCycleCollectionTraverse(aCallback, aField.mLiveTracks, "mLiveTracks",
                              aFlags);
  ImplCycleCollectionTraverse(aCallback, aField.mFinishWhenEndedLoadingSrc,
                              "mFinishWhenEndedLoadingSrc", aFlags);
  ImplCycleCollectionTraverse(aCallback, aField.mFinishWhenEndedAttrStream,
                              "mFinishWhenEndedAttrStream", aFlags);
  ImplCycleCollectionTraverse(aCallback, aField.mFinishWhenEndedMediaSource,
                              "mFinishWhenEndedMediaSource", aFlags);
}

void ImplCycleCollectionUnlink(HTMLMediaElement::OutputMediaStream& aField) {
  ImplCycleCollectionUnlink(aField.mStream);
  ImplCycleCollectionUnlink(aField.mLiveTracks);
  ImplCycleCollectionUnlink(aField.mFinishWhenEndedLoadingSrc);
  ImplCycleCollectionUnlink(aField.mFinishWhenEndedAttrStream);
  ImplCycleCollectionUnlink(aField.mFinishWhenEndedMediaSource);
}

NS_IMPL_ADDREF_INHERITED(HTMLMediaElement::MediaElementTrackSource,
                         MediaStreamTrackSource)
NS_IMPL_RELEASE_INHERITED(HTMLMediaElement::MediaElementTrackSource,
                          MediaStreamTrackSource)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(
    HTMLMediaElement::MediaElementTrackSource)
NS_INTERFACE_MAP_END_INHERITING(MediaStreamTrackSource)
NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLMediaElement::MediaElementTrackSource)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(
    HTMLMediaElement::MediaElementTrackSource, MediaStreamTrackSource)
  tmp->Destroy();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCapturedTrack)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCapturedTrackSource)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(
    HTMLMediaElement::MediaElementTrackSource, MediaStreamTrackSource)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCapturedTrack)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCapturedTrackSource)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

/**
 * There is a reference cycle involving this class: MediaLoadListener
 * holds a reference to the HTMLMediaElement, which holds a reference
 * to an nsIChannel, which holds a reference to this listener.
 * We break the reference cycle in OnStartRequest by clearing mElement.
 */
class HTMLMediaElement::MediaLoadListener final
    : public nsIChannelEventSink,
      public nsIInterfaceRequestor,
      public nsIObserver,
      public nsIThreadRetargetableStreamListener {
  ~MediaLoadListener() = default;

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSICHANNELEVENTSINK
  NS_DECL_NSIOBSERVER
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSITHREADRETARGETABLESTREAMLISTENER

 public:
  explicit MediaLoadListener(HTMLMediaElement* aElement)
      : mElement(aElement), mLoadID(aElement->GetCurrentLoadID()) {
    MOZ_ASSERT(mElement, "Must pass an element to call back");
  }

 private:
  RefPtr<HTMLMediaElement> mElement;
  nsCOMPtr<nsIStreamListener> mNextListener;
  const uint32_t mLoadID;
};

NS_IMPL_ISUPPORTS(HTMLMediaElement::MediaLoadListener, nsIRequestObserver,
                  nsIStreamListener, nsIChannelEventSink, nsIInterfaceRequestor,
                  nsIObserver, nsIThreadRetargetableStreamListener)

NS_IMETHODIMP
HTMLMediaElement::MediaLoadListener::Observe(nsISupports* aSubject,
                                             const char* aTopic,
                                             const char16_t* aData) {
  nsContentUtils::UnregisterShutdownObserver(this);

  // Clear mElement to break cycle so we don't leak on shutdown
  mElement = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
HTMLMediaElement::MediaLoadListener::OnStartRequest(nsIRequest* aRequest) {
  nsContentUtils::UnregisterShutdownObserver(this);

  if (!mElement) {
    // We've been notified by the shutdown observer, and are shutting down.
    return NS_BINDING_ABORTED;
  }

  // The element is only needed until we've had a chance to call
  // InitializeDecoderForChannel. So make sure mElement is cleared here.
  RefPtr<HTMLMediaElement> element;
  element.swap(mElement);

  if (mLoadID != element->GetCurrentLoadID()) {
    // The channel has been cancelled before we had a chance to create
    // a decoder. Abort, don't dispatch an "error" event, as the new load
    // may not be in an error state.
    return NS_BINDING_ABORTED;
  }

  // Don't continue to load if the request failed or has been canceled.
  nsresult status;
  nsresult rv = aRequest->GetStatus(&status);
  NS_ENSURE_SUCCESS(rv, rv);
  if (NS_FAILED(status)) {
    if (element) {
      // Handle media not loading error because source was a tracking URL (or
      // fingerprinting, cryptomining, etc).
      // We make a note of this media node by including it in a dedicated
      // array of blocked tracking nodes under its parent document.
      if (net::ChannelClassifierUtils::IsClassifierBlockingErrorCode(status)) {
        element->OwnerDoc()->AddBlockedNodeByClassifier(element);
      }
      element->NotifyLoadError(
          nsPrintfCString("%u: %s", uint32_t(status), "Request failed"));
    }
    return status;
  }

  nsCOMPtr<nsIHttpChannel> hc = do_QueryInterface(aRequest);
  bool succeeded;
  if (hc && NS_SUCCEEDED(hc->GetRequestSucceeded(&succeeded)) && !succeeded) {
    uint32_t responseStatus = 0;
    (void)hc->GetResponseStatus(&responseStatus);
    nsAutoCString statusText;
    (void)hc->GetResponseStatusText(statusText);
    // we need status text for resist fingerprinting mode's message allowlist
    if (statusText.IsEmpty()) {
      net_GetDefaultStatusTextForCode(responseStatus, statusText);
    }
    element->NotifyLoadError(
        nsPrintfCString("%u: %s", responseStatus, statusText.get()));

    nsAutoString code;
    code.AppendInt(responseStatus);
    nsAutoString src;
    element->GetCurrentSrc(src);
    AutoTArray<nsString, 2> params = {std::move(code), std::move(src)};
    element->ReportLoadError("MediaLoadHttpError", params);
    return NS_BINDING_ABORTED;
  }

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
  if (channel &&
      NS_SUCCEEDED(rv = element->InitializeDecoderForChannel(
                       channel, getter_AddRefs(mNextListener))) &&
      mNextListener) {
    nsCOMPtr<nsIStreamListener> nextListener = mNextListener;
    rv = nextListener->OnStartRequest(aRequest);
  } else {
    // If InitializeDecoderForChannel() returned an error, fire a network error.
    if (NS_FAILED(rv) && !mNextListener) {
      // Load failed, attempt to load the next candidate resource. If there
      // are none, this will trigger a MEDIA_ERR_SRC_NOT_SUPPORTED error.
      element->NotifyLoadError("Failed to init decoder"_ns);
    }
    // If InitializeDecoderForChannel did not return a listener (but may
    // have otherwise succeeded), we abort the connection since we aren't
    // interested in keeping the channel alive ourselves.
    rv = NS_BINDING_ABORTED;
  }

  return rv;
}

NS_IMETHODIMP
HTMLMediaElement::MediaLoadListener::OnStopRequest(nsIRequest* aRequest,
                                                   nsresult aStatus) {
  if (nsCOMPtr<nsIStreamListener> nextListener = mNextListener) {
    return nextListener->OnStopRequest(aRequest, aStatus);
  }
  return NS_OK;
}

NS_IMETHODIMP
HTMLMediaElement::MediaLoadListener::OnDataAvailable(nsIRequest* aRequest,
                                                     nsIInputStream* aStream,
                                                     uint64_t aOffset,
                                                     uint32_t aCount) {
  if (!mNextListener) {
    NS_ERROR(
        "Must have a chained listener; OnStartRequest should have "
        "canceled this request");
    return NS_BINDING_ABORTED;
  }
  nsCOMPtr<nsIStreamListener> nextListener = mNextListener;
  return nextListener->OnDataAvailable(aRequest, aStream, aOffset, aCount);
}

NS_IMETHODIMP
HTMLMediaElement::MediaLoadListener::OnDataFinished(nsresult aStatus) {
  if (!mNextListener) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIThreadRetargetableStreamListener> retargetable =
      do_QueryInterface(mNextListener);
  if (retargetable) {
    return retargetable->OnDataFinished(aStatus);
  }

  return NS_OK;
}

NS_IMETHODIMP
HTMLMediaElement::MediaLoadListener::AsyncOnChannelRedirect(
    nsIChannel* aOldChannel, nsIChannel* aNewChannel, uint32_t aFlags,
    nsIAsyncVerifyRedirectCallback* cb) {
  // TODO is this really correct?? See bug #579329.
  if (mElement) {
    mElement->OnChannelRedirect(aOldChannel, aNewChannel, aFlags);
  }
  nsCOMPtr<nsIChannelEventSink> sink = do_QueryInterface(mNextListener);
  if (sink) {
    return sink->AsyncOnChannelRedirect(aOldChannel, aNewChannel, aFlags, cb);
  }
  cb->OnRedirectVerifyCallback(NS_OK);
  return NS_OK;
}

NS_IMETHODIMP
HTMLMediaElement::MediaLoadListener::CheckListenerChain() {
  MOZ_ASSERT(mNextListener);
  nsCOMPtr<nsIThreadRetargetableStreamListener> retargetable =
      do_QueryInterface(mNextListener);
  if (retargetable) {
    return retargetable->CheckListenerChain();
  }
  return NS_ERROR_NO_INTERFACE;
}

NS_IMETHODIMP
HTMLMediaElement::MediaLoadListener::GetInterface(const nsIID& aIID,
                                                  void** aResult) {
  return QueryInterface(aIID, aResult);
}

void HTMLMediaElement::ReportLoadError(const char* aMsg,
                                       const nsTArray<nsString>& aParams) {
  ReportToConsole(nsIScriptError::warningFlag, aMsg, aParams);
}

void HTMLMediaElement::ReportToConsole(
    uint32_t aErrorFlags, const char* aMsg,
    const nsTArray<nsString>& aParams) const {
  nsContentUtils::ReportToConsole(aErrorFlags, "Media"_ns, OwnerDoc(),
                                  PropertiesFile::DOM_PROPERTIES, aMsg,
                                  aParams);
}

class HTMLMediaElement::AudioChannelAgentCallback final
    : public nsIAudioChannelAgentCallback {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_CLASS(AudioChannelAgentCallback)

  explicit AudioChannelAgentCallback(HTMLMediaElement* aOwner)
      : mOwner(aOwner),
        mAudioChannelVolume(1.0),
        mPlayingThroughTheAudioChannel(false),
        mIsOwnerAudible(IsOwnerAudible()),
        mIsShutDown(false) {
    MOZ_ASSERT(mOwner);
    MaybeCreateAudioChannelAgent();
  }

  void UpdateAudioChannelPlayingState() {
    MOZ_ASSERT(!mIsShutDown);
    bool playingThroughTheAudioChannel = IsPlayingThroughTheAudioChannel();

    if (playingThroughTheAudioChannel != mPlayingThroughTheAudioChannel) {
      if (!MaybeCreateAudioChannelAgent()) {
        return;
      }

      mPlayingThroughTheAudioChannel = playingThroughTheAudioChannel;
      if (mPlayingThroughTheAudioChannel) {
        StartAudioChannelAgent();
      } else {
        StopAudioChanelAgent();
      }
    }
  }

  void NotifyPlayStateChanged() {
    MOZ_ASSERT(!mIsShutDown);
    UpdateAudioChannelPlayingState();
  }

  NS_IMETHODIMP WindowVolumeChanged(float aVolume, bool aMuted) override {
    MOZ_ASSERT(mAudioChannelAgent);

    MOZ_LOG_FMT(
        AudioChannelService::GetAudioChannelLog(), LogLevel::Debug,
        "HTMLMediaElement::AudioChannelAgentCallback, WindowVolumeChanged, "
        "this = {}, aVolume = {}, aMuted = {}\n",
        fmt::ptr(this), aVolume, aMuted ? "true" : "false");

    if (mAudioChannelVolume != aVolume) {
      mAudioChannelVolume = aVolume;
      mOwner->SetVolumeInternal();
    }

    const uint32_t muted = mOwner->mMuted;
    if (aMuted && !mOwner->ComputedMuted()) {
      mOwner->SetMutedInternal(muted | MUTED_BY_AUDIO_CHANNEL);
    } else if (!aMuted && mOwner->ComputedMuted()) {
      mOwner->SetMutedInternal(muted & ~MUTED_BY_AUDIO_CHANNEL);
    }

    return NS_OK;
  }

  NS_IMETHODIMP WindowSuspendChanged(SuspendTypes aSuspend) override {
    // Currently this method is only be used for delaying autoplay, and we've
    // separated related codes to `MediaPlaybackDelayPolicy`.
    return NS_OK;
  }

  NS_IMETHODIMP WindowAudioCaptureChanged(bool aCapture) override {
    MOZ_ASSERT(mAudioChannelAgent);
    AudioCaptureTrackChangeIfNeeded();
    return NS_OK;
  }

  void AudioCaptureTrackChangeIfNeeded() {
    MOZ_ASSERT(!mIsShutDown);
    if (!IsPlayingStarted()) {
      return;
    }

    MOZ_ASSERT(mAudioChannelAgent);
    bool isCapturing = mAudioChannelAgent->IsWindowAudioCapturingEnabled();
    mOwner->AudioCaptureTrackChange(isCapturing);
  }

  void NotifyAudioPlaybackChanged(AudibleChangedReasons aReason) {
    MOZ_ASSERT(!mIsShutDown);
    AudibleState newAudibleState = IsOwnerAudible();
    MOZ_LOG_FMT(AudioChannelService::GetAudioChannelLog(), LogLevel::Debug,
                "HTMLMediaElement::AudioChannelAgentCallback, "
                "NotifyAudioPlaybackChanged, this={}, current={}, new={}",
                fmt::ptr(this),
                AudioChannelService::EnumValueToString(mIsOwnerAudible),
                AudioChannelService::EnumValueToString(newAudibleState));
    if (mIsOwnerAudible == newAudibleState) {
      return;
    }

    mIsOwnerAudible = newAudibleState;
    if (IsPlayingStarted()) {
      mAudioChannelAgent->NotifyStartedAudible(mIsOwnerAudible, aReason);
    }
  }

  void Shutdown() {
    MOZ_ASSERT(!mIsShutDown);
    if (mAudioChannelAgent && mAudioChannelAgent->IsPlayingStarted()) {
      StopAudioChanelAgent();
    }
    mAudioChannelAgent = nullptr;
    mIsShutDown = true;
  }

  float GetEffectiveVolume() const {
    MOZ_ASSERT(!mIsShutDown);
    return static_cast<float>(mOwner->Volume()) * mAudioChannelVolume;
  }

 private:
  ~AudioChannelAgentCallback() { MOZ_ASSERT(mIsShutDown); };

  bool MaybeCreateAudioChannelAgent() {
    if (mAudioChannelAgent) {
      return true;
    }

    mAudioChannelAgent = new AudioChannelAgent();
    nsresult rv =
        mAudioChannelAgent->Init(mOwner->OwnerDoc()->GetInnerWindow(), this);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      mAudioChannelAgent = nullptr;
      MOZ_LOG_FMT(
          AudioChannelService::GetAudioChannelLog(), LogLevel::Debug,
          "HTMLMediaElement::AudioChannelAgentCallback, Fail to initialize "
          "the audio channel agent, this = {}\n",
          fmt::ptr(this));
      return false;
    }

    return true;
  }

  void StartAudioChannelAgent() {
    MOZ_ASSERT(mAudioChannelAgent);
    MOZ_ASSERT(!mAudioChannelAgent->IsPlayingStarted());
    if (NS_WARN_IF(NS_FAILED(
            mAudioChannelAgent->NotifyStartedPlaying(IsOwnerAudible())))) {
      return;
    }
    mAudioChannelAgent->PullInitialUpdate();
  }

  void StopAudioChanelAgent() {
    MOZ_ASSERT(mAudioChannelAgent);
    MOZ_ASSERT(mAudioChannelAgent->IsPlayingStarted());
    mAudioChannelAgent->NotifyStoppedPlaying();
    // If we have started audio capturing before, we have to tell media element
    // to clear the output capturing track.
    mOwner->AudioCaptureTrackChange(false);
  }

  bool IsPlayingStarted() {
    if (MaybeCreateAudioChannelAgent()) {
      return mAudioChannelAgent->IsPlayingStarted();
    }
    return false;
  }

  AudibleState IsOwnerAudible() const {
    // paused media doesn't produce any sound.
    if (mOwner->mPaused) {
      return AudibleState::eNotAudible;
    }
    return mOwner->IsAudible() ? AudibleState::eAudible
                               : AudibleState::eNotAudible;
  }

  bool IsPlayingThroughTheAudioChannel() const {
    // If we have an error, we are not playing.
    if (mOwner->GetError()) {
      return false;
    }

    // We should consider any bfcached page or inactive document as non-playing.
    if (!mOwner->OwnerDoc()->IsActive()) {
      return false;
    }

    // Media is suspended by the docshell.
    if (mOwner->ShouldBeSuspendedByInactiveDocShell()) {
      return false;
    }

    // Are we paused
    if (mOwner->mPaused) {
      return false;
    }

    // No audio track
    if (!mOwner->HasAudio()) {
      return false;
    }

    // A loop always is playing
    if (mOwner->HasAttr(nsGkAtoms::loop)) {
      return true;
    }

    // If we are actually playing...
    if (mOwner->IsCurrentlyPlaying()) {
      return true;
    }

    // If we are playing an external stream.
    if (mOwner->mSrcAttrStream) {
      return true;
    }

    return false;
  }

  RefPtr<AudioChannelAgent> mAudioChannelAgent;
  HTMLMediaElement* mOwner;

  // The audio channel volume
  float mAudioChannelVolume;
  // Is this media element playing?
  bool mPlayingThroughTheAudioChannel;
  // Indicate whether media element is audible for users.
  AudibleState mIsOwnerAudible;
  bool mIsShutDown;
};

NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLMediaElement::AudioChannelAgentCallback)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(
    HTMLMediaElement::AudioChannelAgentCallback)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAudioChannelAgent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(
    HTMLMediaElement::AudioChannelAgentCallback)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAudioChannelAgent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(
    HTMLMediaElement::AudioChannelAgentCallback)
  NS_INTERFACE_MAP_ENTRY(nsIAudioChannelAgentCallback)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(HTMLMediaElement::AudioChannelAgentCallback)
NS_IMPL_CYCLE_COLLECTING_RELEASE(HTMLMediaElement::AudioChannelAgentCallback)

class HTMLMediaElement::ChannelLoader final {
 public:
  NS_INLINE_DECL_REFCOUNTING(ChannelLoader);

  explicit ChannelLoader(const JSCallingLocation& aCallingLocation)
      : mCallingLocation(aCallingLocation) {}

  void LoadInternal(HTMLMediaElement* aElement) {
    if (mCancelled) {
      return;
    }

    JSCallingLocation::AutoFallback fallback(&mCallingLocation);

    // determine what security checks need to be performed in AsyncOpen().
    nsSecurityFlags securityFlags =
        aElement->ShouldCheckAllowOrigin()
            ? nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT
            : nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT;

    if (aElement->GetCORSMode() == CORS_USE_CREDENTIALS) {
      securityFlags |= nsILoadInfo::SEC_COOKIES_INCLUDE;
    }

    securityFlags |= nsILoadInfo::SEC_ALLOW_CHROME;

    MOZ_ASSERT(
        aElement->IsAnyOfHTMLElements(nsGkAtoms::audio, nsGkAtoms::video));
    nsContentPolicyType contentPolicyType =
        aElement->IsHTMLElement(nsGkAtoms::audio)
            ? nsIContentPolicy::TYPE_INTERNAL_AUDIO
            : nsIContentPolicy::TYPE_INTERNAL_VIDEO;

    // If aElement has 'triggeringprincipal' attribute, we will use the value as
    // triggeringPrincipal for the channel, otherwise it will default to use
    // aElement->NodePrincipal().
    // This function returns true when aElement has 'triggeringprincipal', so if
    // setAttrs is true we will override the origin attributes on the channel
    // later.
    nsCOMPtr<nsIPrincipal> triggeringPrincipal;
    bool setAttrs = nsContentUtils::QueryTriggeringPrincipal(
        aElement, aElement->mLoadingSrcTriggeringPrincipal,
        getter_AddRefs(triggeringPrincipal));

    nsCOMPtr<nsILoadGroup> loadGroup = aElement->GetDocumentLoadGroup();
    nsCOMPtr<nsIChannel> channel;
    nsresult rv = NS_NewChannelWithTriggeringPrincipal(
        getter_AddRefs(channel), aElement->mLoadingSrc,
        static_cast<Element*>(aElement), triggeringPrincipal, securityFlags,
        contentPolicyType,
        nullptr,  // aPerformanceStorage
        loadGroup,
        nullptr,  // aCallbacks
        nsICachingChannel::LOAD_BYPASS_LOCAL_CACHE_IF_BUSY |
            nsIChannel::LOAD_MEDIA_SNIFFER_OVERRIDES_CONTENT_TYPE |
            nsIChannel::LOAD_CALL_CONTENT_SNIFFERS);

    if (NS_FAILED(rv)) {
      // Notify load error so the element will try next resource candidate.
      aElement->NotifyLoadError("Fail to create channel"_ns);
      return;
    }

    nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
    if (setAttrs) {
      // The function simply returns NS_OK, so we ignore the return value.
      (void)loadInfo->SetOriginAttributes(
          triggeringPrincipal->OriginAttributesRef());
    }
    loadInfo->SetIsMediaRequest(true);

    if (nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(channel)) {
      nsString initiatorType =
          aElement->IsHTMLElement(nsGkAtoms::audio) ? u"audio"_ns : u"video"_ns;
      timedChannel->SetInitiatorType(initiatorType);
    }

    nsCOMPtr<nsIClassOfService> cos(do_QueryInterface(channel));
    if (cos) {
      if (aElement->mUseUrgentStartForChannel) {
        cos->AddClassFlags(nsIClassOfService::UrgentStart);

        // Reset the flag to avoid loading again without initiated by user
        // interaction.
        aElement->mUseUrgentStartForChannel = false;
      }

      // Unconditionally disable throttling since we want the media to fluently
      // play even when we switch the tab to background.
      cos->AddClassFlags(nsIClassOfService::DontThrottle);
    }

    // The listener holds a strong reference to us.  This creates a
    // reference cycle, once we've set mChannel, which is manually broken
    // in the listener's OnStartRequest method after it is finished with
    // the element. The cycle will also be broken if we get a shutdown
    // notification before OnStartRequest fires.  Necko guarantees that
    // OnStartRequest will eventually fire if we don't shut down first.
    RefPtr<MediaLoadListener> loadListener = new MediaLoadListener(aElement);

    channel->SetNotificationCallbacks(loadListener);

    nsCOMPtr<nsIHttpChannel> hc = do_QueryInterface(channel);
    if (hc) {
      // Use a byte range request from the start of the resource.
      // This enables us to detect if the stream supports byte range
      // requests, and therefore seeking, early.
      rv = hc->SetRequestHeader("Range"_ns, "bytes=0-"_ns, false);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
      aElement->SetRequestHeaders(hc);
    }

    rv = channel->AsyncOpen(loadListener);
    if (NS_FAILED(rv)) {
      // Notify load error so the element will try next resource candidate.
      aElement->NotifyLoadError("Failed to open channel"_ns);
      return;
    }

    // Else the channel must be open and starting to download. If it encounters
    // a non-catastrophic failure, it will set a new task to continue loading
    // another candidate.  It's safe to set it as mChannel now.
    mChannel = std::move(channel);

    // loadListener will be unregistered either on shutdown or when
    // OnStartRequest for the channel we just opened fires.
    nsContentUtils::RegisterShutdownObserver(loadListener);
  }

  nsresult Load(HTMLMediaElement* aElement) {
    MOZ_ASSERT(aElement);
    // Per bug 1235183 comment 8, we can't spin the event loop from stable
    // state. Defer NS_NewChannel() to a new regular runnable.
    return aElement->OwnerDoc()->Dispatch(NewRunnableMethod<HTMLMediaElement*>(
        "ChannelLoader::LoadInternal", this, &ChannelLoader::LoadInternal,
        aElement));
  }

  void Cancel() {
    mCancelled = true;
    if (mChannel) {
      mChannel->CancelWithReason(NS_BINDING_ABORTED,
                                 "HTMLMediaElement::ChannelLoader::Cancel"_ns);
      mChannel = nullptr;
    }
  }

  void Done() {
    MOZ_ASSERT(mChannel);
    // Decoder successfully created, the decoder now owns the MediaResource
    // which owns the channel.
    mChannel = nullptr;
  }

  nsresult Redirect(nsIChannel* aChannel, nsIChannel* aNewChannel,
                    uint32_t aFlags) {
    NS_ASSERTION(aChannel == mChannel, "Channels should match!");
    mChannel = aNewChannel;

    // Handle forwarding of Range header so that the intial detection
    // of seeking support (via result code 206) works across redirects.
    nsCOMPtr<nsIHttpChannel> http = do_QueryInterface(aChannel);
    NS_ENSURE_STATE(http);

    constexpr auto rangeHdr = "Range"_ns;

    nsAutoCString rangeVal;
    if (NS_SUCCEEDED(http->GetRequestHeader(rangeHdr, rangeVal))) {
      NS_ENSURE_STATE(!rangeVal.IsEmpty());

      http = do_QueryInterface(aNewChannel);
      NS_ENSURE_STATE(http);

      nsresult rv = http->SetRequestHeader(rangeHdr, rangeVal, false);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    return NS_OK;
  }

 private:
  ~ChannelLoader() { MOZ_ASSERT(!mChannel); }
  // Holds a reference to the first channel we open to the media resource.
  // Once the decoder is created, control over the channel passes to the
  // decoder, and we null out this reference. We must store this in case
  // we need to cancel the channel before control of it passes to the decoder.
  nsCOMPtr<nsIChannel> mChannel;

  bool mCancelled = false;
  JSCallingLocation mCallingLocation;
};

class HTMLMediaElement::ErrorSink {
 public:
  explicit ErrorSink(HTMLMediaElement* aOwner) : mOwner(aOwner) {
    MOZ_ASSERT(mOwner);
  }

  void SetError(uint16_t aErrorCode, const Maybe<MediaResult>& aResult) {
    // Since we have multiple paths calling into DecodeError, e.g.
    // MediaKeys::Terminated and EMEH264Decoder::Error. We should take the 1st
    // one only in order not to fire multiple 'error' events.
    if (mError) {
      return;
    }

    if (!IsValidErrorCode(aErrorCode)) {
      NS_ASSERTION(false, "Undefined MediaError codes!");
      return;
    }

    ReportErrorProbe(aErrorCode, aResult);
    if (mOwner->ReadyState() == HAVE_NOTHING &&
        aErrorCode == MEDIA_ERR_ABORTED) {
      // https://html.spec.whatwg.org/multipage/media.html#media-data-processing-steps-list
      // "If the media data fetching process is aborted by the user"
      mOwner->QueueEvent(u"abort"_ns);
      mOwner->ChangeNetworkState(NETWORK_EMPTY);
      mOwner->QueueEvent(u"emptied"_ns);
      if (mOwner->mDecoder) {
        mOwner->ShutdownDecoder();
      }
      return;
    }

    if (aErrorCode == MEDIA_ERR_SRC_NOT_SUPPORTED) {
      // https://html.spec.whatwg.org/multipage/media.html#dedicated-media-source-failure-steps
      mOwner->ChangeNetworkState(NETWORK_NO_SOURCE);
    } else {
      // https://html.spec.whatwg.org/multipage/media.html#media-data-processing-steps-list
      // "If the connection is interrupted after some media data has been
      // received" or "If the media data is corrupted"
      mOwner->ChangeNetworkState(NETWORK_IDLE);
    }
    mError = new MediaError(mOwner, aErrorCode,
                            aResult ? aResult->Message() : nsCString());
    mOwner->QueueEvent(u"error"_ns);
  }

  void ResetError() { mError = nullptr; }

  RefPtr<MediaError> mError;

 private:
  bool IsValidErrorCode(const uint16_t& aErrorCode) const {
    return (aErrorCode == MEDIA_ERR_DECODE || aErrorCode == MEDIA_ERR_NETWORK ||
            aErrorCode == MEDIA_ERR_ABORTED ||
            aErrorCode == MEDIA_ERR_SRC_NOT_SUPPORTED);
  }

  void ReportErrorProbe(uint16_t aErrorCode,
                        const Maybe<MediaResult>& aResult) {
    MOZ_ASSERT(IsValidErrorCode(aErrorCode));
    auto getErrorTypeLabel = [&]() {
      if (aErrorCode == MEDIA_ERR_ABORTED) {
        return "abort_error"_ns;
      }
      if (aErrorCode == MEDIA_ERR_NETWORK) {
        return "network_error"_ns;
      }
      if (aErrorCode == MEDIA_ERR_DECODE) {
        return "decode_error"_ns;
      }
      return "not_supported_error"_ns;
    };
    nsCString encryptedLabel =
        mOwner->mMediaKeys ? "encrypted"_ns : "non_encrypted"_ns;
    glean::media::error.Get(getErrorTypeLabel(), encryptedLabel).Add();
  }

  // Media elememt's life cycle would be longer than error sink, so we use the
  // raw pointer and this class would only be referenced by media element.
  HTMLMediaElement* mOwner;
};

NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLMediaElement)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(HTMLMediaElement,
                                                  nsGenericHTMLElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mStreamWindowCapturer)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMediaSource)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSrcMediaSource)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSrcStream)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSrcAttrStream)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSourcePointer)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLoadBlockedDoc)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSourceLoadCandidate)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAudioChannelWrapper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mErrorSink->mError)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOutputStreams)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOutputTrackSources);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPlayed);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTextTrackManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAudioTrackList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mVideoTrackList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMediaStreamTrackListener)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMediaKeys)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mIncomingMediaKeys)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSelectedVideoStreamTrack)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingPlayPromises)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingPlayPromisesRunners)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSeekDOMPromise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSetMediaKeysDOMPromise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEventBlocker)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(HTMLMediaElement,
                                                nsGenericHTMLElement)
  tmp->RemoveMutationObserver(tmp);
  if (tmp->mSrcStream) {
    // Need to unhook everything that EndSrcMediaStreamPlayback would normally
    // do, without creating any new strong references.
    if (tmp->mSelectedVideoStreamTrack) {
      tmp->mSelectedVideoStreamTrack->RemovePrincipalChangeObserver(tmp);
    }
    if (tmp->mMediaStreamRenderer) {
      tmp->mMediaStreamRenderer->Shutdown();
      // We null out mMediaStreamRenderer here since Shutdown() will shut down
      // its WatchManager, and UpdateSrcStreamPotentiallyPlaying() contains a
      // guard for this.
      tmp->mMediaStreamRenderer = nullptr;
    }
    if (tmp->mSecondaryMediaStreamRenderer) {
      tmp->mSecondaryMediaStreamRenderer->Shutdown();
      tmp->mSecondaryMediaStreamRenderer = nullptr;
    }
    if (tmp->mMediaStreamTrackListener) {
      tmp->mSrcStream->UnregisterTrackListener(
          tmp->mMediaStreamTrackListener.get());
    }
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mStreamWindowCapturer)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSrcStream)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSrcAttrStream)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mMediaSource)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSrcMediaSource)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSourcePointer)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLoadBlockedDoc)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSourceLoadCandidate)
  if (tmp->mAudioChannelWrapper) {
    tmp->mAudioChannelWrapper->Shutdown();
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAudioChannelWrapper)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mErrorSink->mError)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOutputStreams)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOutputTrackSources)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPlayed)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTextTrackManager)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAudioTrackList)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mVideoTrackList)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mMediaStreamTrackListener)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mMediaKeys)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mIncomingMediaKeys)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSelectedVideoStreamTrack)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPendingPlayPromises)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPendingPlayPromisesRunners)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSeekDOMPromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSetMediaKeysDOMPromise)
  if (tmp->mMediaControlKeyListener) {
    tmp->mMediaControlKeyListener->Shutdown();
  }
  if (tmp->mEventBlocker) {
    tmp->mEventBlocker->Shutdown();
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(HTMLMediaElement,
                                               nsGenericHTMLElement)

void HTMLMediaElement::AddSizeOfExcludingThis(nsWindowSizes& aSizes,
                                              size_t* aNodeSize) const {
  nsGenericHTMLElement::AddSizeOfExcludingThis(aSizes, aNodeSize);

  // There are many other fields that might be worth reporting, but as seen in
  // bug 1595603, the event we postpone to dispatch can grow to be very large
  // sometimes, so at least report that.
  if (mEventBlocker) {
    *aNodeSize +=
        mEventBlocker->SizeOfExcludingThis(aSizes.mState.mMallocSizeOf);
  }
}

void HTMLMediaElement::ContentWillBeRemoved(nsIContent* aChild,
                                            const ContentRemoveInfo&) {
  if (aChild == mSourcePointer) {
    mSourcePointer = aChild->GetPreviousSibling();
  }
}

already_AddRefed<MediaSource> HTMLMediaElement::GetMozMediaSourceObject()
    const {
  RefPtr<MediaSource> source = mMediaSource;
  return source.forget();
}

already_AddRefed<Promise> HTMLMediaElement::MozRequestDebugInfo(
    ErrorResult& aRv) {
  RefPtr<Promise> promise = CreateDOMPromise(aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  auto result = std::make_unique<dom::HTMLMediaElementDebugInfo>();
  if (mMediaKeys) {
    GetEMEInfo(result->mEMEInfo);
  }
  if (mVideoFrameContainer) {
    result->mCompositorDroppedFrames =
        mVideoFrameContainer->GetDroppedImageCount();
  }
  if (mDecoder) {
    mDecoder->RequestDebugInfo(result->mDecoder)
        ->Then(
            AbstractMainThread(), __func__,
            [promise, ptr = std::move(result)]() {
              promise->MaybeResolve(ptr.get());
            },
            []() {
              MOZ_ASSERT_UNREACHABLE("Unexpected RequestDebugInfo() rejection");
            });
  } else {
    promise->MaybeResolve(result.get());
  }
  return promise.forget();
}

/* static */
void HTMLMediaElement::MozEnableDebugLog(const GlobalObject&) {
  DecoderDoctorLogger::EnableLogging();
}

already_AddRefed<Promise> HTMLMediaElement::MozRequestDebugLog(
    ErrorResult& aRv) {
  RefPtr<Promise> promise = CreateDOMPromise(aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  DecoderDoctorLogger::RetrieveMessages(this)->Then(
      AbstractMainThread(), __func__,
      [promise](const nsACString& aString) {
        promise->MaybeResolve(NS_ConvertUTF8toUTF16(aString));
      },
      [promise](nsresult rv) { promise->MaybeReject(rv); });

  return promise.forget();
}

void HTMLMediaElement::SetVisible(bool aVisible) {
  mForcedHidden = !aVisible;
  if (mDecoder) {
    mDecoder->SetForcedHidden(!aVisible);
  }
}

bool HTMLMediaElement::IsVideoDecodingSuspended() const {
  return mDecoder && mDecoder->IsVideoDecodingSuspended();
}

double HTMLMediaElement::TotalVideoPlayTime() const {
  return mDecoder ? mDecoder->GetTotalVideoPlayTimeInSeconds() : -1.0;
}

double HTMLMediaElement::TotalVideoHDRPlayTime() const {
  return mDecoder ? mDecoder->GetTotalVideoHDRPlayTimeInSeconds() : -1.0;
}

double HTMLMediaElement::VisiblePlayTime() const {
  return mDecoder ? mDecoder->GetVisibleVideoPlayTimeInSeconds() : -1.0;
}

double HTMLMediaElement::InvisiblePlayTime() const {
  return mDecoder ? mDecoder->GetInvisibleVideoPlayTimeInSeconds() : -1.0;
}

double HTMLMediaElement::TotalAudioPlayTime() const {
  return mDecoder ? mDecoder->GetTotalAudioPlayTimeInSeconds() : -1.0;
}

double HTMLMediaElement::AudiblePlayTime() const {
  return mDecoder ? mDecoder->GetAudiblePlayTimeInSeconds() : -1.0;
}

double HTMLMediaElement::InaudiblePlayTime() const {
  return mDecoder ? mDecoder->GetInaudiblePlayTimeInSeconds() : -1.0;
}

double HTMLMediaElement::MutedPlayTime() const {
  return mDecoder ? mDecoder->GetMutedPlayTimeInSeconds() : -1.0;
}

void HTMLMediaElement::SetFormatDiagnosticsReportForMimeType(
    const nsAString& aMimeType, DecoderDoctorReportType aType) {
  DecoderDoctorDiagnostics diagnostics;
  diagnostics.SetDecoderDoctorReportType(aType);
  diagnostics.StoreFormatDiagnostics(OwnerDoc(), aMimeType, false /* can play*/,
                                     __func__);
}

void HTMLMediaElement::SetDecodeError(const nsAString& aError,
                                      ErrorResult& aRv) {
  // The reason we use this map-ish structure is because we can't use
  // `CR.NS_ERROR.*` directly in test. In order to use them in test, we have to
  // add them into `xpc.msg`. As we won't use `CR.NS_ERROR.*` in the production
  // code, adding them to `xpc.msg` seems an overdesign and adding maintenance
  // effort (exposing them in CR also needs to add a description, which is
  // useless because we won't show them to users)
  static struct {
    const char* mName;
    nsresult mResult;
  } kSupportedErrorList[] = {
      {"NS_ERROR_DOM_MEDIA_ABORT_ERR", NS_ERROR_DOM_MEDIA_ABORT_ERR},
      {"NS_ERROR_DOM_MEDIA_NOT_ALLOWED_ERR",
       NS_ERROR_DOM_MEDIA_NOT_ALLOWED_ERR},
      {"NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR",
       NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR},
      {"NS_ERROR_DOM_MEDIA_DECODE_ERR", NS_ERROR_DOM_MEDIA_DECODE_ERR},
      {"NS_ERROR_DOM_MEDIA_FATAL_ERR", NS_ERROR_DOM_MEDIA_FATAL_ERR},
      {"NS_ERROR_DOM_MEDIA_METADATA_ERR", NS_ERROR_DOM_MEDIA_METADATA_ERR},
      {"NS_ERROR_DOM_MEDIA_OVERFLOW_ERR", NS_ERROR_DOM_MEDIA_OVERFLOW_ERR},
      {"NS_ERROR_DOM_MEDIA_MEDIASINK_ERR", NS_ERROR_DOM_MEDIA_MEDIASINK_ERR},
      {"NS_ERROR_DOM_MEDIA_DEMUXER_ERR", NS_ERROR_DOM_MEDIA_DEMUXER_ERR},
      {"NS_ERROR_DOM_MEDIA_CDM_ERR", NS_ERROR_DOM_MEDIA_CDM_ERR},
      {"NS_ERROR_DOM_MEDIA_CUBEB_INITIALIZATION_ERR",
       NS_ERROR_DOM_MEDIA_CUBEB_INITIALIZATION_ERR}};
  for (auto& error : kSupportedErrorList) {
    if (strcmp(error.mName, NS_ConvertUTF16toUTF8(aError).get()) == 0) {
      DecoderDoctorDiagnostics diagnostics;
      diagnostics.StoreDecodeError(OwnerDoc(), error.mResult, u""_ns, __func__);
      return;
    }
  }
  aRv.Throw(NS_ERROR_FAILURE);
}

void HTMLMediaElement::SetAudioSinkFailedStartup() {
  DecoderDoctorDiagnostics diagnostics;
  diagnostics.StoreEvent(OwnerDoc(),
                         {DecoderDoctorEvent::eAudioSinkStartup,
                          NS_ERROR_DOM_MEDIA_CUBEB_INITIALIZATION_ERR},
                         __func__);
}

already_AddRefed<layers::Image> HTMLMediaElement::GetCurrentImage() {
  MarkAsTainted();

  // TODO: In bug 1345404, handle case when video decoder is already suspended.
  ImageContainer* container = GetImageContainer();
  if (!container) {
    return nullptr;
  }

  AutoLockImage lockImage(container);
  RefPtr<layers::Image> image = lockImage.GetImage(TimeStamp::Now());
  return image.forget();
}

bool HTMLMediaElement::HasSuspendTaint() const {
  MOZ_ASSERT(!mDecoder || (mDecoder->HasSuspendTaint() == mHasSuspendTaint));
  return mHasSuspendTaint;
}

already_AddRefed<DOMMediaStream> HTMLMediaElement::GetSrcObject() const {
  return do_AddRef(mSrcAttrStream);
}

void HTMLMediaElement::SetSrcObject(DOMMediaStream& aValue) {
  SetSrcObject(&aValue);
}

void HTMLMediaElement::SetSrcObject(DOMMediaStream* aValue) {
  for (auto& outputStream : mOutputStreams) {
    if (aValue == outputStream.mStream) {
      ReportToConsole(nsIScriptError::warningFlag,
                      "MediaElementStreamCaptureCycle");
      return;
    }
  }
  mSrcAttrStream = aValue;
  UpdateAudioChannelPlayingState();
  DoLoad();
}

bool HTMLMediaElement::Ended() {
  return (mDecoder && mDecoder->IsEnded()) ||
         (mSrcStream && mSrcStreamReportPlaybackEnded);
}

void HTMLMediaElement::GetCurrentSrc(nsAString& aCurrentSrc) {
  nsAutoCString src;
  GetCurrentSpec(src);
  CopyUTF8toUTF16(src, aCurrentSrc);
}

nsresult HTMLMediaElement::OnChannelRedirect(nsIChannel* aChannel,
                                             nsIChannel* aNewChannel,
                                             uint32_t aFlags) {
  MOZ_ASSERT(mChannelLoader);
  if (aNewChannel) {
    nsCOMPtr<nsIURI> oldURI;
    if (aChannel) {
      aChannel->GetURI(getter_AddRefs(oldURI));
    }
    aNewChannel->GetURI(getter_AddRefs(mLoadingSrcFinalURI));
    LOG(LogLevel::Debug,
        ("{} OnChannelRedirect: from {} to {}", fmt::ptr(this),
         oldURI ? oldURI->GetSpecOrDefault().get() : "null",
         mLoadingSrcFinalURI ? mLoadingSrcFinalURI->GetSpecOrDefault().get()
                             : "null"));
  }
  return mChannelLoader->Redirect(aChannel, aNewChannel, aFlags);
}

void HTMLMediaElement::ShutdownDecoder() {
  RemoveMediaElementFromURITable();
  NS_ASSERTION(mDecoder, "Must have decoder to shut down");

  mWaitingForKeyListener.DisconnectIfExists();
  mDecoder->Shutdown();
  DDUNLINKCHILD(mDecoder.get());
  mDecoder = nullptr;
}

void HTMLMediaElement::AbortExistingLoads() {
  MOZ_ASSERT(NS_IsMainThread());
  LOG(LogLevel::Debug, ("{} Abort existing loads", fmt::ptr(this)));
  mLazyLoadingCompleted = false;
  // Abort any already-running instance of the resource selection algorithm.
  mLoadWaitStatus = NOT_WAITING;

  // A new resource is being loaded; allow its impact to be recorded again.
  mRecordedRuntimeContentAttrImpact = false;

  // Set a new load ID. This will cause events which were enqueued
  // with a different load ID to silently be cancelled.
  mCurrentLoadID++;

  // Immediately reject or resolve the already-dispatched
  // nsResolveOrRejectPendingPlayPromisesRunners. These runners won't be
  // executed again later since the mCurrentLoadID had been changed.
  for (auto& runner : mPendingPlayPromisesRunners) {
    runner->ResolveOrReject();
  }
  mPendingPlayPromisesRunners.Clear();

  if (mChannelLoader) {
    mChannelLoader->Cancel();
    mChannelLoader = nullptr;
  }

  mLoadingSrcFinalURI = nullptr;

  bool fireTimeUpdate = false;

  if (mDecoder) {
    fireTimeUpdate = mDecoder->GetCurrentTime() != 0.0;
    // When aborting during seeking, remove the seeking state since the decoder
    // won't call SeekCompleted() or SeekAborted() after being shut down.
    if (Seeking()) {
      RemoveStates(ElementState::SEEKING);
    }
    ShutdownDecoder();
  }
  if (mSrcStream) {
    EndSrcMediaStreamPlayback();
  }

  if (mMediaSource) {
    OwnerDoc()->RemoveMediaElementWithMSE();
  }

  RemoveMediaElementFromURITable();
  mLoadingSrcTriggeringPrincipal = nullptr;
  // The CORS mode is scoped to the current load.
  mCORSMode = CORS_NONE;
  DDLOG(DDLogCategory::Property, "loading_src", "");
  DDUNLINKCHILD(mMediaSource.get());
  mMediaSource = nullptr;

  if (mNetworkState == NETWORK_LOADING || mNetworkState == NETWORK_IDLE) {
    QueueEvent(u"abort"_ns);
  }

  bool hadVideo = HasVideo();
  mErrorSink->ResetError();
  mCurrentPlayRangeStart = Nothing();
  mPlayed = new TimeRanges(ToSupports(OwnerDoc()));
  mLoadedDataFired = false;
  mCanAutoplayFlag = true;
  mIsLoadingFromSourceChildren = false;
  mSuspendedAfterFirstFrame = false;
  mAllowSuspendAfterFirstFrame = true;
  mHaveQueuedSelectResource = false;
  mSuspendedForPreloadNone = false;
  mDownloadSuspendedByCache = false;
  mMediaInfo = MediaInfo();
  mIsEncrypted = false;
  mPendingEncryptedInitData.Reset();
  mWaitingForKey = NOT_WAITING_FOR_KEY;
  mSourcePointer = nullptr;
  mIsBlessed = false;
  SetAudibleState(false);

  mTags = nullptr;

  if (mNetworkState != NETWORK_EMPTY) {
    NS_ASSERTION(!mDecoder && !mSrcStream,
                 "How did someone setup a new stream/decoder already?");

    QueueEvent(u"emptied"_ns);

    // ChangeNetworkState() will call UpdateAudioChannelPlayingState()
    // indirectly which depends on mPaused. So we need to update mPaused first.
    if (!mPaused) {
      mPaused = true;
      UpdatePlaybackPseudoClasses();
      PlayPromise::RejectPromises(TakePendingPlayPromises(),
                                  NS_ERROR_DOM_MEDIA_ABORT_ERR);
    }
    ChangeNetworkState(NETWORK_EMPTY);
    RemoveMediaTracks();
    UpdateOutputTrackSources();
    ChangeReadyState(HAVE_NOTHING);

    // TODO: Apply the rules for text track cue rendering Bug 865407
    if (mTextTrackManager) {
      mTextTrackManager->GetTextTracks()->SetCuesInactive();
    }

    if (fireTimeUpdate) {
      // Since we destroyed the decoder above, the current playback position
      // will now be reported as 0. The playback position was non-zero when
      // we destroyed the decoder, so fire a timeupdate event so that the
      // change will be reflected in the controls.
      FireTimeUpdate(TimeupdateType::eMandatory);
    }
    UpdateAudioChannelPlayingState();
  }

  if (IsVideo() && hadVideo) {
    // Ensure we render transparent black after resetting video resolution.
    Maybe<nsIntSize> size = Some(nsIntSize(0, 0));
    Invalidate(ImageSizeChanged::Yes, size, Some(VideoRotation::kDegree_0),
               ForceInvalidate::No);
  }

  // As aborting current load would stop current playback, so we have no need to
  // resume a paused media element.
  ClearResumeDelayedMediaPlaybackAgentIfNeeded();

  mMediaControlKeyListener->StopIfNeeded();

  // We may have changed mPaused, mCanAutoplayFlag, and other
  // things which can affect AddRemoveSelfReference
  AddRemoveSelfReference();

  mIsRunningSelectResource = false;

  AssertReadyStateIsNothing();
}

void HTMLMediaElement::NoSupportedMediaSourceError(
    const nsACString& aErrorDetails) {
  if (mDecoder) {
    ShutdownDecoder();
  }

  bool isSameOriginLoad = false;
  nsresult rv = NS_ERROR_NOT_AVAILABLE;
  if (mLoadingSrcTriggeringPrincipal && mLoadingSrcFinalURI) {
    rv = mLoadingSrcTriggeringPrincipal->IsSameOrigin(mLoadingSrcFinalURI,
                                                      &isSameOriginLoad);
  }

  if (NS_FAILED(rv) || !isSameOriginLoad) {
    // aErrorDetails can include sensitive details like MimeType or HTTP Status
    // Code. In case we're loading a 3rd party resource we should not leak this
    // and pass a Generic Error Message
    mErrorSink->SetError(MEDIA_ERR_SRC_NOT_SUPPORTED,
                         Some(MediaResult{NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR,
                                          "Failed to open media"_ns}));
  } else {
    mErrorSink->SetError(
        MEDIA_ERR_SRC_NOT_SUPPORTED,
        Some(MediaResult{NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR, aErrorDetails}));
  }

  RemoveMediaTracks();
  ChangeDelayLoadStatus(false);
  UpdateAudioChannelPlayingState();
  PlayPromise::RejectPromises(TakePendingPlayPromises(),
                              NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR);
}

// Runs a "synchronous section", a function that must run once the event loop
// has reached a "stable state"
// http://www.whatwg.org/specs/web-apps/current-work/multipage/webappapis.html#synchronous-section
void HTMLMediaElement::RunInStableState(nsIRunnable* aRunnable) {
  if (mShuttingDown) {
    return;
  }

  nsCOMPtr<nsIRunnable> task = NS_NewRunnableFunction(
      "HTMLMediaElement::RunInStableState",
      [self = RefPtr<HTMLMediaElement>(this), loadId = GetCurrentLoadID(),
       runnable = RefPtr<nsIRunnable>(aRunnable)]() {
        if (self->GetCurrentLoadID() != loadId) {
          return;
        }
        runnable->Run();
      });
  nsContentUtils::RunInStableState(task.forget());
}

void HTMLMediaElement::QueueLoadFromSourceTask() {
  if (!mIsLoadingFromSourceChildren || mShuttingDown) {
    return;
  }

  if (mDecoder) {
    // Reset readyState to HAVE_NOTHING since we're going to load a new decoder.
    ShutdownDecoder();
    ChangeReadyState(HAVE_NOTHING);
  }

  AssertReadyStateIsNothing();

  ChangeDelayLoadStatus(true);
  ChangeNetworkState(NETWORK_LOADING);
  RefPtr<Runnable> r = NewRunnableMethod<JSCallingLocation>(
      "HTMLMediaElement::LoadFromSourceChildren", this,
      &HTMLMediaElement::LoadFromSourceChildren, JSCallingLocation::Get());
  RunInStableState(r);
}

void HTMLMediaElement::QueueSelectResourceTask() {
  // Don't allow multiple async select resource calls to be queued.
  if (mHaveQueuedSelectResource) {
    return;
  }
  mHaveQueuedSelectResource = true;
  ChangeNetworkState(NETWORK_NO_SOURCE);
  RefPtr<Runnable> r = NewRunnableMethod<JSCallingLocation>(
      "HTMLMediaElement::SelectResourceWrapper", this,
      &HTMLMediaElement::SelectResourceWrapper, JSCallingLocation::Get());
  RunInStableState(r);
}

static bool HasSourceChildren(nsIContent* aElement) {
  for (nsIContent* child = aElement->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (child->IsHTMLElement(nsGkAtoms::source)) {
      return true;
    }
  }
  return false;
}

static nsCString DocumentOrigin(Document* aDoc) {
  if (!aDoc) {
    return "null"_ns;
  }
  nsCOMPtr<nsIPrincipal> principal = aDoc->NodePrincipal();
  if (!principal) {
    return "null"_ns;
  }
  nsCString origin;
  if (NS_FAILED(principal->GetOrigin(origin))) {
    return "null"_ns;
  }
  return origin;
}

void HTMLMediaElement::SetLazyLoading() {
  if (!StaticPrefs::media_lazy_loading_enabled()) {
    return;
  }

  if (mLazyLoading || mLazyLoadingCompleted || !MaybeStartLazyLoading()) {
    return;
  }

  mLazyLoading = true;
  ChangeNetworkState(NETWORK_IDLE);
  LOG(LogLevel::Debug, ("{} SetLazyLoading: pending", fmt::ptr(this)));
}

void HTMLMediaElement::StopLazyLoading(StartLoad aStartLoad) {
  if (!mLazyLoading) {
    return;
  }

  mLazyLoading = false;
  Element::StopLazyLoading();

  if (IsVideo() && aStartLoad == StartLoad::Yes) {
    if (nsVideoFrame* videoFrame = do_QueryFrame(GetPrimaryFrame())) {
      videoFrame->UpdatePosterSource(true);
    }
  }

  if (aStartLoad == StartLoad::Yes) {
    LOG(LogLevel::Debug, ("{} StopLazyLoading: loading now", fmt::ptr(this)));
    DoLoad();
    mLazyLoadingCompleted = true;
  } else {
    LOG(LogLevel::Debug,
        ("{} StopLazyLoading: stopped without loading", fmt::ptr(this)));
  }
}

void HTMLMediaElement::Load() {
  LOG(LogLevel::Debug,
      ("{} Load() hasSrcAttrStream={} hasSrcAttr={} hasSourceChildren={} "
       "handlingInput={} hasAutoplayAttr={} AllowedToPlay={} "
       "ownerDoc={} ({}) ownerDocUserActivated={} "
       "muted={} volume={}",
       fmt::ptr(this), !!mSrcAttrStream, HasAttr(nsGkAtoms::src),
       HasSourceChildren(this), UserActivation::IsHandlingUserInput(),
       HasAttr(nsGkAtoms::autoplay), AllowedToPlay(), fmt::ptr(OwnerDoc()),
       DocumentOrigin(OwnerDoc()).get(),
       OwnerDoc()->HasBeenUserGestureActivated(), mMuted, mVolume));

  if (mIsRunningLoadMethod) {
    return;
  }

  // An explicit load() call must override lazy loading deferral.
  // StartLoad::No: DoLoad() handles resource selection itself and skips
  // UpdatePosterSource. An explicit load() should not trigger poster loading,
  // matching the spec's load() steps, which invoke only the media lazy load
  // resumption steps.
  const bool wasLazyLoading = mLazyLoading;
  StopLazyLoading(StartLoad::No);

  mIsDoingExplicitLoad = true;
  DoLoad();

  if (wasLazyLoading) {
    mLazyLoadingCompleted = true;
  }
}

void HTMLMediaElement::DoLoad() {
  // Check if media is allowed for the docshell.
  nsCOMPtr<nsIDocShell> docShell = OwnerDoc()->GetDocShell();
  if (docShell && !docShell->GetAllowMedia()) {
    LOG(LogLevel::Debug, ("{} Media not allowed", fmt::ptr(this)));
    return;
  }

  if (mIsRunningLoadMethod) {
    return;
  }

  if (mLazyLoading) {
    LOG(LogLevel::Debug, ("{} DoLoad: load deferred (lazy)", fmt::ptr(this)));
    return;
  }

  if (UserActivation::IsHandlingUserInput()) {
    // Detect if user has interacted with element so that play will not be
    // blocked when initiated by a script. This enables sites to capture user
    // intent to play by calling load() in the click handler of a "catalog
    // view" of a gallery of videos.
    mIsBlessed = true;
    // Mark the channel as urgent-start when autoplay so that it will play the
    // media from src after loading enough resource.
    if (HasAttr(nsGkAtoms::autoplay)) {
      mUseUrgentStartForChannel = true;
    }
  }

  SetPlayedOrSeeked(false);
  mIsRunningLoadMethod = true;
  AbortExistingLoads();
  SetPlaybackRate(mDefaultPlaybackRate, IgnoreErrors());
  QueueSelectResourceTask();
  ResetState();
  mIsRunningLoadMethod = false;
}

void HTMLMediaElement::ResetState() {
  // There might be a pending MediaDecoder::PlaybackPositionChanged() which
  // will overwrite |mMediaInfo.mVideo.mDisplay| in UpdateMediaSize() to give
  // staled videoWidth and videoHeight. We have to call ForgetElement() here
  // such that the staled callbacks won't reach us.
  if (mVideoFrameContainer) {
    mVideoFrameContainer->ForgetElement();
    mVideoFrameContainer = nullptr;
  }
  if (mMediaStreamRenderer) {
    // mMediaStreamRenderer, has a strong reference to mVideoFrameContainer.
    mMediaStreamRenderer->Shutdown();
    mMediaStreamRenderer = nullptr;
  }
  if (mSecondaryMediaStreamRenderer) {
    // mSecondaryMediaStreamRenderer, has a strong reference to
    // the secondary VideoFrameContainer.
    mSecondaryMediaStreamRenderer->Shutdown();
    mSecondaryMediaStreamRenderer = nullptr;
  }
}

void HTMLMediaElement::SelectResourceWrapper(
    const JSCallingLocation& aCallingLocation) {
  SelectResource(aCallingLocation);
  MaybeBeginCloningVisually();
  mIsRunningSelectResource = false;
  mHaveQueuedSelectResource = false;
  mIsDoingExplicitLoad = false;
}

void HTMLMediaElement::SelectResource(
    const JSCallingLocation& aCallingLocation) {
  if (!mSrcAttrStream && !HasAttr(nsGkAtoms::src) && !HasSourceChildren(this)) {
    // The media element has neither a src attribute nor any source
    // element children, abort the load.
    ChangeNetworkState(NETWORK_EMPTY);
    ChangeDelayLoadStatus(false);
    return;
  }

  if (mLazyLoading) {
    LOG(LogLevel::Debug,
        ("{} SelectResource: load deferred (lazy)", fmt::ptr(this)));
    return;
  }

  ChangeDelayLoadStatus(true);

  ChangeNetworkState(NETWORK_LOADING);
  QueueEvent(u"loadstart"_ns);

  // Delay setting mIsRunningSeletResource until after UpdatePreloadAction
  // so that we don't lose our state change by bailing out of the preload
  // state update
  UpdatePreloadAction(aCallingLocation);
  mIsRunningSelectResource = true;

  // If we have a 'src' attribute, use that exclusively.
  nsAutoString src;
  if (mSrcAttrStream) {
    // Media provider objects use local mode, so a previous URL load's CORS
    // mode does not apply.
    mCORSMode = CORS_NONE;
    SetupSrcMediaStreamPlayback(mSrcAttrStream);
  } else if (GetAttr(nsGkAtoms::src, src)) {
    nsCOMPtr<nsIURI> uri;
    MediaResult rv = NewURIFromString(src, getter_AddRefs(uri));
    if (NS_SUCCEEDED(rv)) {
      LOG(LogLevel::Debug, ("{} Trying load from src={}", fmt::ptr(this),
                            NS_ConvertUTF16toUTF8(src).get()));
      if (profiler_is_collecting_markers()) {
        profiler_add_marker("loadresource",
                            geckoprofiler::category::MEDIA_PLAYBACK, {},
                            LoadSourceMarker{}, nsString{src}, nsString{},
                            nsString{}, Flow::FromPointer(this));
      }

      NS_ASSERTION(
          !mIsLoadingFromSourceChildren,
          "Should think we're not loading from source children by default");

      RemoveMediaElementFromURITable();
      if (!mSrcMediaSource) {
        mLoadingSrc = uri;
      } else {
        mLoadingSrc = nullptr;
      }
      mLoadingSrcFinalURI = mLoadingSrc;
      mLoadingSrcTriggeringPrincipal = mSrcAttrTriggeringPrincipal;
      DDLOG(DDLogCategory::Property, "loading_src",
            nsCString(NS_ConvertUTF16toUTF8(src)));
      bool hadMediaSource = !!mMediaSource;
      mMediaSource = mSrcMediaSource;
      if (mMediaSource && !hadMediaSource) {
        OwnerDoc()->AddMediaElementWithMSE();
      }
      DDLINKCHILD("mediasource", mMediaSource.get());
      UpdatePreloadAction(aCallingLocation);
      if (mPreloadAction == HTMLMediaElement::PRELOAD_NONE && !mMediaSource) {
        // preload:none media, suspend the load here before we make any
        // network requests.
        SuspendLoad();
        return;
      }

      rv = LoadResource(aCallingLocation);
      if (NS_SUCCEEDED(rv)) {
        return;
      }
    } else {
      AutoTArray<nsString, 1> params = {std::move(src)};
      ReportLoadError("MediaLoadInvalidURI", params);
      rv = MediaResult(rv.Code(), "MediaLoadInvalidURI");
    }
    NoSupportedMediaSourceError(rv.Description());
  } else {
    // Otherwise, the source elements will be used.
    mIsLoadingFromSourceChildren = true;
    LoadFromSourceChildren(aCallingLocation);
  }
}

void HTMLMediaElement::NotifyLoadError(const nsACString& aErrorDetails) {
  if (!mIsLoadingFromSourceChildren) {
    LOG(LogLevel::Debug, ("NotifyLoadError(), no supported media error"));
    NoSupportedMediaSourceError(aErrorDetails);
  } else if (mSourceLoadCandidate) {
    DispatchAsyncSourceError(mSourceLoadCandidate, aErrorDetails);
    QueueLoadFromSourceTask();
  } else {
    NS_WARNING("Should know the source we were loading from!");
  }
  if (profiler_is_collecting_markers()) {
    profiler_add_marker("loaderror", geckoprofiler::category::MEDIA_PLAYBACK,
                        {}, LoadErrorMarker{}, aErrorDetails,
                        Flow::FromPointer(this));
  }
}

void HTMLMediaElement::NotifyMediaTrackAdded(dom::MediaTrack* aTrack) {
  // The set of tracks changed.
  mWatchManager.ManualNotify(&HTMLMediaElement::UpdateOutputTrackSources);
}

void HTMLMediaElement::NotifyMediaTrackRemoved(dom::MediaTrack* aTrack) {
  // The set of tracks changed.
  mWatchManager.ManualNotify(&HTMLMediaElement::UpdateOutputTrackSources);
}

void HTMLMediaElement::NotifyMediaTrackEnabled(dom::MediaTrack* aTrack) {
  MOZ_ASSERT(aTrack);
  if (!aTrack) {
    return;
  }
#ifdef DEBUG
  nsString id;
  aTrack->GetId(id);

  LOG(LogLevel::Debug,
      ("MediaElement {} {}Track with id {} enabled", fmt::ptr(this),
       aTrack->AsAudioTrack() ? "Audio" : "Video",
       NS_ConvertUTF16toUTF8(id).get()));
#endif

  MOZ_ASSERT((aTrack->AsAudioTrack() && aTrack->AsAudioTrack()->Enabled()) ||
             (aTrack->AsVideoTrack() && aTrack->AsVideoTrack()->Selected()));

  if (aTrack->AsAudioTrack()) {
    SetMutedInternal(mMuted & ~MUTED_BY_AUDIO_TRACK);
  } else if (aTrack->AsVideoTrack()) {
    if (!IsVideo()) {
      MOZ_ASSERT(false);
      return;
    }
    mDisableVideo = false;
  } else {
    MOZ_ASSERT(false, "Unknown track type");
  }

  if (mSrcStream) {
    if (AudioTrack* t = aTrack->AsAudioTrack()) {
      if (mMediaStreamRenderer) {
        mMediaStreamRenderer->AddTrack(t->GetAudioStreamTrack());
      }
    } else if (VideoTrack* t = aTrack->AsVideoTrack()) {
      MOZ_ASSERT(!mSelectedVideoStreamTrack);

      mSelectedVideoStreamTrack = t->GetVideoStreamTrack();
      mSelectedVideoStreamTrack->AddPrincipalChangeObserver(this);
      if (mMediaStreamRenderer) {
        mMediaStreamRenderer->AddTrack(mSelectedVideoStreamTrack);
      }
      if (mSecondaryMediaStreamRenderer) {
        mSecondaryMediaStreamRenderer->AddTrack(mSelectedVideoStreamTrack);
      }
      if (mMediaInfo.HasVideo()) {
        mMediaInfo.mVideo.SetAlpha(mSelectedVideoStreamTrack->HasAlpha());
      }
      nsContentUtils::CombineResourcePrincipals(
          &mSrcStreamVideoPrincipal, mSelectedVideoStreamTrack->GetPrincipal());
    }
  }

  // The set of enabled/selected tracks changed.
  mWatchManager.ManualNotify(&HTMLMediaElement::UpdateOutputTrackSources);
}

void HTMLMediaElement::NotifyMediaTrackDisabled(dom::MediaTrack* aTrack) {
  MOZ_ASSERT(aTrack);
  if (!aTrack) {
    return;
  }

  nsString id;
  aTrack->GetId(id);

  LOG(LogLevel::Debug,
      ("MediaElement {} {}Track with id {} disabled", fmt::ptr(this),
       aTrack->AsAudioTrack() ? "Audio" : "Video",
       NS_ConvertUTF16toUTF8(id).get()));

  MOZ_ASSERT((!aTrack->AsAudioTrack() || !aTrack->AsAudioTrack()->Enabled()) &&
             (!aTrack->AsVideoTrack() || !aTrack->AsVideoTrack()->Selected()));

  if (AudioTrack* t = aTrack->AsAudioTrack()) {
    if (mSrcStream) {
      if (mMediaStreamRenderer) {
        mMediaStreamRenderer->RemoveTrack(t->GetAudioStreamTrack());
      }
    }
    // If we don't have any live tracks, we don't need to mute MediaElement.
    MOZ_DIAGNOSTIC_ASSERT(AudioTracks(), "Element can't have been unlinked");
    if (AudioTracks()->Length() > 0) {
      bool shouldMute = true;
      for (uint32_t i = 0; i < AudioTracks()->Length(); ++i) {
        if ((*AudioTracks())[i]->Enabled()) {
          shouldMute = false;
          break;
        }
      }

      if (shouldMute) {
        SetMutedInternal(mMuted | MUTED_BY_AUDIO_TRACK);
      }
    }
  } else if (aTrack->AsVideoTrack()) {
    if (mSrcStream) {
      MOZ_DIAGNOSTIC_ASSERT(mSelectedVideoStreamTrack ==
                            aTrack->AsVideoTrack()->GetVideoStreamTrack());
      if (mMediaStreamRenderer) {
        mMediaStreamRenderer->RemoveTrack(mSelectedVideoStreamTrack);
      }
      if (mSecondaryMediaStreamRenderer) {
        mSecondaryMediaStreamRenderer->RemoveTrack(mSelectedVideoStreamTrack);
      }
      mSelectedVideoStreamTrack->RemovePrincipalChangeObserver(this);
      mSelectedVideoStreamTrack = nullptr;
    }
  }

  // The set of enabled/selected tracks changed.
  mWatchManager.ManualNotify(&HTMLMediaElement::UpdateOutputTrackSources);
}

void HTMLMediaElement::DealWithFailedElement(nsIContent* aSourceElement) {
  if (mShuttingDown) {
    return;
  }

  DispatchAsyncSourceError(aSourceElement, "Failed load on source element"_ns);
  GetMainThreadSerialEventTarget()->Dispatch(
      NewRunnableMethod("HTMLMediaElement::QueueLoadFromSourceTask", this,
                        &HTMLMediaElement::QueueLoadFromSourceTask));
}

void HTMLMediaElement::LoadFromSourceChildren(
    const JSCallingLocation& aCallingLocation) {
  NS_ASSERTION(mDelayingLoadEvent || LoadingState() == Loading::Lazy,
               "Should delay load event (if in document) during load");
  NS_ASSERTION(mIsLoadingFromSourceChildren,
               "Must remember we're loading from source children");

  AddMutationObserverUnlessExists(this);

  RemoveMediaTracks();

  while (true) {
    HTMLSourceElement* child = GetNextSource();
    if (!child) {
      // Exhausted candidates, wait for more candidates to be appended to
      // the media element.
      mLoadWaitStatus = WAITING_FOR_SOURCE;
      ChangeNetworkState(NETWORK_NO_SOURCE);
      ChangeDelayLoadStatus(false);
      ReportLoadError("MediaLoadExhaustedCandidates");
      return;
    }

    // Must have src attribute.
    nsAutoString src;
    if (!child->GetAttr(nsGkAtoms::src, src)) {
      ReportLoadError("MediaLoadSourceMissingSrc");
      DealWithFailedElement(child);
      return;
    }

    // If we have a type attribute, it must be a supported type.
    nsAutoString type;
    if (child->GetAttr(nsGkAtoms::type, type) && !type.IsEmpty()) {
      DecoderDoctorDiagnostics diagnostics;
      CanPlayStatus canPlay = GetCanPlay(type, &diagnostics);
      diagnostics.StoreFormatDiagnostics(OwnerDoc(), type,
                                         canPlay != CANPLAY_NO, __func__);
      if (canPlay == CANPLAY_NO) {
        // Check that at least one other source child exists and report that
        // we will try to load that one next.
        nsIContent* nextChild = mSourcePointer->GetNextSibling();
        AutoTArray<nsString, 2> params = {std::move(type), std::move(src)};

        while (nextChild) {
          if (nextChild && nextChild->IsHTMLElement(nsGkAtoms::source)) {
            ReportLoadError("MediaLoadUnsupportedTypeAttributeLoadingNextChild",
                            params);
            break;
          }

          nextChild = nextChild->GetNextSibling();
        };

        if (!nextChild) {
          ReportLoadError("MediaLoadUnsupportedTypeAttribute", params);
        }

        DealWithFailedElement(child);
        return;
      }
    }
    nsAutoString media;
    child->GetAttr(nsGkAtoms::media, media);
    HTMLSourceElement* childSrc = HTMLSourceElement::FromNode(child);
    MOZ_ASSERT(childSrc, "Expect child to be HTMLSourceElement");
    if (childSrc && !childSrc->MatchesCurrentMedia()) {
      AutoTArray<nsString, 2> params = {media, src};
      ReportLoadError("MediaLoadSourceMediaNotMatched", params);
      DealWithFailedElement(child);
      LOG(LogLevel::Debug,
          ("{} Media did not match from <source>={} type={} media={}",
           fmt::ptr(this), NS_ConvertUTF16toUTF8(src).get(),
           NS_ConvertUTF16toUTF8(type).get(),
           NS_ConvertUTF16toUTF8(media).get()));
      return;
    }
    LOG(LogLevel::Debug,
        ("{} Trying load from <source>={} type={} media={}", fmt::ptr(this),
         NS_ConvertUTF16toUTF8(src).get(), NS_ConvertUTF16toUTF8(type).get(),
         NS_ConvertUTF16toUTF8(media).get()));

    if (profiler_is_collecting_markers()) {
      profiler_add_marker("loadresource",
                          geckoprofiler::category::MEDIA_PLAYBACK, {},
                          LoadSourceMarker{}, nsString{src}, nsString{type},
                          nsString{media}, Flow::FromPointer(this));
    }

    nsCOMPtr<nsIURI> uri;
    NewURIFromString(src, getter_AddRefs(uri));
    if (!uri) {
      AutoTArray<nsString, 1> params = {std::move(src)};
      ReportLoadError("MediaLoadInvalidURI", params);
      DealWithFailedElement(child);
      return;
    }

    RemoveMediaElementFromURITable();
    mLoadingSrc = uri;
    mLoadingSrcFinalURI = mLoadingSrc;
    mLoadingSrcTriggeringPrincipal = child->GetSrcTriggeringPrincipal();
    DDLOG(DDLogCategory::Property, "loading_src",
          nsCString(NS_ConvertUTF16toUTF8(src)));
    bool hadMediaSource = !!mMediaSource;
    mMediaSource = child->GetSrcMediaSource();
    if (mMediaSource && !hadMediaSource) {
      OwnerDoc()->AddMediaElementWithMSE();
    }
    DDLINKCHILD("mediasource", mMediaSource.get());
    NS_ASSERTION(mNetworkState == NETWORK_LOADING,
                 "Network state should be loading");

    if (mPreloadAction == HTMLMediaElement::PRELOAD_NONE && !mMediaSource) {
      // preload:none media, suspend the load here before we make any
      // network requests.
      SuspendLoad();
      return;
    }

    if (NS_SUCCEEDED(LoadResource(aCallingLocation))) {
      return;
    }

    // If we fail to load, loop back and try loading the next resource.
    DispatchAsyncSourceError(child, "Failed load on resource"_ns);
  }
  MOZ_ASSERT_UNREACHABLE("Execution should not reach here!");
}

void HTMLMediaElement::SuspendLoad() {
  mSuspendedForPreloadNone = true;
  ChangeNetworkState(NETWORK_IDLE);
  ChangeDelayLoadStatus(false);
}

void HTMLMediaElement::ResumeLoad(PreloadAction aAction,
                                  const JSCallingLocation& aCallingLocation) {
  NS_ASSERTION(mSuspendedForPreloadNone,
               "Must be halted for preload:none to resume from preload:none "
               "suspended load.");
  mSuspendedForPreloadNone = false;
  mPreloadAction = aAction;
  ChangeDelayLoadStatus(true);
  ChangeNetworkState(NETWORK_LOADING);
  if (!mIsLoadingFromSourceChildren) {
    // We were loading from the element's src attribute.
    MediaResult rv = LoadResource(aCallingLocation);
    if (NS_FAILED(rv)) {
      NoSupportedMediaSourceError(rv.Description());
    }
  } else {
    // We were loading from a child <source> element. Try to resume the
    // load of that child, and if that fails, try the next child.
    if (NS_FAILED(LoadResource(aCallingLocation))) {
      LoadFromSourceChildren(aCallingLocation);
    }
  }
}

bool HTMLMediaElement::AllowedToPlay() const {
  return media::AutoplayPolicy::IsAllowedToPlay(*this);
}

uint32_t HTMLMediaElement::GetPreloadDefault() const {
  if (mMediaSource) {
    return HTMLMediaElement::PRELOAD_METADATA;
  }
  if (ShouldResistFingerprinting(RFPTarget::NetworkConnection)) {
    return HTMLMediaElement::PRELOAD_METADATA;
  }
  if (OnCellularConnection()) {
    return Preferences::GetInt("media.preload.default.cellular",
                               HTMLMediaElement::PRELOAD_NONE);
  }
  return Preferences::GetInt("media.preload.default",
                             HTMLMediaElement::PRELOAD_METADATA);
}

uint32_t HTMLMediaElement::GetPreloadDefaultAuto() const {
  if (ShouldResistFingerprinting(RFPTarget::NetworkConnection)) {
    return HTMLMediaElement::PRELOAD_ENOUGH;
  }
  if (OnCellularConnection()) {
    return Preferences::GetInt("media.preload.auto.cellular",
                               HTMLMediaElement::PRELOAD_METADATA);
  }
  return Preferences::GetInt("media.preload.auto",
                             HTMLMediaElement::PRELOAD_ENOUGH);
}

void HTMLMediaElement::UpdatePreloadAction(
    const JSCallingLocation& aCallingLocation) {
  PreloadAction nextAction = PRELOAD_UNDEFINED;
  // If autoplay is set, or we're playing, we should always preload data,
  // as we'll need it to play.
  if ((AllowedToPlay() && HasAttr(nsGkAtoms::autoplay)) || !mPaused) {
    nextAction = HTMLMediaElement::PRELOAD_ENOUGH;
  } else {
    // Find the appropriate preload action by looking at the attribute.
    const nsAttrValue* val =
        mAttrs.GetAttr(nsGkAtoms::preload, kNameSpaceID_None);
    // MSE doesn't work if preload is none, so it ignores the pref when src is
    // from MSE.
    uint32_t preloadDefault = GetPreloadDefault();
    uint32_t preloadAuto = GetPreloadDefaultAuto();
    if (!val) {
      // Attribute is not set. Use the preload action specified by the
      // media.preload.default pref, or just preload metadata if not present.
      nextAction = static_cast<PreloadAction>(preloadDefault);
    } else if (val->Type() == nsAttrValue::eEnum) {
      MediaPreloadAttrValue attr =
          static_cast<MediaPreloadAttrValue>(val->GetEnumValue());
      if (attr == MediaPreloadAttrValue::PRELOAD_ATTR_AUTO) {
        nextAction = static_cast<PreloadAction>(preloadAuto);
      } else if (attr == MediaPreloadAttrValue::PRELOAD_ATTR_METADATA) {
        nextAction = HTMLMediaElement::PRELOAD_METADATA;
      } else if (attr == MediaPreloadAttrValue::PRELOAD_ATTR_NONE) {
        nextAction = HTMLMediaElement::PRELOAD_NONE;
      }
    } else {
      // Use the suggested "missing value default" of "metadata", or the value
      // specified by the media.preload.default, if present.
      nextAction = static_cast<PreloadAction>(preloadDefault);
    }
  }

  if (nextAction == HTMLMediaElement::PRELOAD_NONE && mIsDoingExplicitLoad) {
    LOG(LogLevel::Debug, ("{} Force to preload metadata when explicit loading "
                          "a preload none element",
                          fmt::ptr(this)));
    nextAction = HTMLMediaElement::PRELOAD_METADATA;
  }

  mPreloadAction = nextAction;
  LOG(LogLevel::Debug,
      ("{} Preload action={}", fmt::ptr(this), static_cast<int>(nextAction)));

  if (nextAction == HTMLMediaElement::PRELOAD_ENOUGH) {
    if (mSuspendedForPreloadNone) {
      // Our load was previouly suspended due to the media having preload
      // value "none". The preload value has changed to preload:auto, so
      // resume the load.
      ResumeLoad(PRELOAD_ENOUGH, aCallingLocation);
    } else {
      // Preload as much of the video as we can, i.e. don't suspend after
      // the first frame.
      StopSuspendingAfterFirstFrame();
    }

  } else if (nextAction == HTMLMediaElement::PRELOAD_METADATA) {
    // If no preload attribute is present but Load() has been explicitly called,
    // assume more data may be needed and avoid suspending the decoder. This is
    // done because some sites may rely on the `canplaythrough` event after
    // calling load(), even though it’s not guaranteed to fire.
    //
    // This behavior can be reconsidered once bug 1969224 is resolved.
    if (!HasAttr(nsGkAtoms::preload) && mIsDoingExplicitLoad) {
      mAllowSuspendAfterFirstFrame = false;
    } else {
      // Ensure that the video can be suspended after first frame.
      mAllowSuspendAfterFirstFrame = true;
    }
    if (mSuspendedForPreloadNone) {
      // Our load was previouly suspended due to the media having preload
      // value "none". The preload value has changed to preload:metadata, so
      // resume the load. We'll pause the load again after we've read the
      // metadata.
      ResumeLoad(PRELOAD_METADATA, aCallingLocation);
    }
  }
}

MediaResult HTMLMediaElement::LoadResource(
    const JSCallingLocation& aCallingLocation) {
  NS_ASSERTION(mDelayingLoadEvent || LoadingState() == Loading::Lazy,
               "Should delay load event (if in document) during load");

  if (mChannelLoader) {
    mChannelLoader->Cancel();
    mChannelLoader = nullptr;
  }

  // Set the media element's CORS mode only when loading a resource
  mCORSMode = AttrValueToCORSMode(GetParsedAttr(nsGkAtoms::crossorigin));

  HTMLMediaElement* other = LookupMediaElementURITable(mLoadingSrc);
  if (other && other->mDecoder) {
    // Clone it.
    // TODO: remove the cast by storing ChannelMediaDecoder in the URI table.
    nsresult rv = InitializeDecoderAsClone(
        static_cast<ChannelMediaDecoder*>(other->mDecoder.get()));
    if (NS_SUCCEEDED(rv)) {
      return rv;
    }
  }

  LOG(LogLevel::Debug, ("{} LoadResource", fmt::ptr(this)));
  if (mMediaSource) {
    MediaDecoderInit decoderInit(
        this, this, mMuted ? 0.0 : mVolume, mPreservesPitch,
        ClampPlaybackRate(mPlaybackRate),
        mPreloadAction == HTMLMediaElement::PRELOAD_METADATA, mHasSuspendTaint,
        HasAttr(nsGkAtoms::loop),
        MediaContainerType(MEDIAMIMETYPE("application/x.mediasource")));

    RefPtr<MediaSourceDecoder> decoder = new MediaSourceDecoder(decoderInit);
    if (!mMediaSource->Attach(decoder)) {
      // TODO: Handle failure: run "If the media data cannot be fetched at
      // all, due to network errors, causing the user agent to give up
      // trying to fetch the resource" section of resource fetch algorithm.
      decoder->Shutdown();
      return MediaResult(NS_ERROR_FAILURE, "Failed to attach MediaSource");
    }
    ChangeDelayLoadStatus(false);
    nsresult rv = decoder->Load(mMediaSource->GetPrincipal());
    if (NS_FAILED(rv)) {
      decoder->Shutdown();
      LOG(LogLevel::Debug, ("{} Failed to load for decoder {}", fmt::ptr(this),
                            fmt::ptr(decoder.get())));
      return MediaResult(rv, "Fail to load decoder");
    }
    rv = FinishDecoderSetup(decoder);
    return MediaResult(rv, "Failed to set up decoder");
  }

  AssertReadyStateIsNothing();

  RefPtr<ChannelLoader> loader = new ChannelLoader(aCallingLocation);
  nsresult rv = loader->Load(this);
  if (NS_SUCCEEDED(rv)) {
    mChannelLoader = std::move(loader);
  }
  return MediaResult(rv, "Failed to load channel");
}

nsresult HTMLMediaElement::LoadWithChannel(nsIChannel* aChannel,
                                           nsIStreamListener** aListener) {
  NS_ENSURE_ARG_POINTER(aChannel);
  NS_ENSURE_ARG_POINTER(aListener);

  *aListener = nullptr;

  // Make sure we don't reenter during synchronous abort events.
  if (mIsRunningLoadMethod) {
    return NS_OK;
  }
  mIsRunningLoadMethod = true;
  AbortExistingLoads();
  mIsRunningLoadMethod = false;

  mLoadingSrcTriggeringPrincipal = nullptr;
  nsresult rv = aChannel->GetOriginalURI(getter_AddRefs(mLoadingSrc));
  NS_ENSURE_SUCCESS(rv, rv);

  ChangeDelayLoadStatus(true);
  rv = InitializeDecoderForChannel(aChannel, aListener);
  if (NS_FAILED(rv)) {
    ChangeDelayLoadStatus(false);
    return rv;
  }

  SetPlaybackRate(mDefaultPlaybackRate, IgnoreErrors());
  QueueEvent(u"loadstart"_ns);

  return NS_OK;
}

bool HTMLMediaElement::Seeking() const {
  return mDecoder && mDecoder->IsSeeking();
}

double HTMLMediaElement::CurrentTime() const {
  if (mMediaStreamRenderer) {
    return ToMicrosecondResolution(mMediaStreamRenderer->CurrentTime());
  }

  if (mDefaultPlaybackStartPosition == 0.0 && mDecoder) {
    return std::clamp(mDecoder->GetCurrentTime(), 0.0, mDecoder->GetDuration());
  }

  return mDefaultPlaybackStartPosition;
}

void HTMLMediaElement::FastSeek(double aTime, ErrorResult& aRv) {
  LOG(LogLevel::Debug, ("{} FastSeek({}) called by JS", fmt::ptr(this), aTime));
  Seek(aTime, SeekTarget::PrevSyncPoint, IgnoreErrors());
}

already_AddRefed<Promise> HTMLMediaElement::SeekToNextFrame(ErrorResult& aRv) {
  /* This will cause JIT code to be kept around longer, to help performance
   * when using SeekToNextFrame to iterate through every frame of a video.
   */
  nsPIDOMWindowInner* win = OwnerDoc()->GetInnerWindow();

  if (win) {
    if (JSObject* obj = win->AsGlobal()->GetGlobalJSObject()) {
      js::NotifyAnimationActivity(obj);
    }
  }

  Seek(CurrentTime(), SeekTarget::NextFrame, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  mSeekDOMPromise = CreateDOMPromise(aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  return do_AddRef(mSeekDOMPromise);
}

void HTMLMediaElement::SetCurrentTime(double aCurrentTime, ErrorResult& aRv) {
  LOG(LogLevel::Debug,
      ("{} SetCurrentTime({}) called by JS", fmt::ptr(this), aCurrentTime));
  Seek(aCurrentTime, SeekTarget::Accurate, IgnoreErrors());
}

/**
 * Check if aValue is inside a range of aRanges, and if so returns true
 * and puts the range index in aIntervalIndex. If aValue is not
 * inside a range, returns false, and aIntervalIndex
 * is set to the index of the range which starts immediately after aValue
 * (and can be aRanges.Length() if aValue is after the last range).
 */
static bool IsInRanges(TimeRanges& aRanges, double aValue,
                       uint32_t& aIntervalIndex) {
  uint32_t length = aRanges.Length();

  for (uint32_t i = 0; i < length; i++) {
    double start = aRanges.Start(i);
    if (start > aValue) {
      aIntervalIndex = i;
      return false;
    }
    double end = aRanges.End(i);
    if (aValue <= end) {
      aIntervalIndex = i;
      return true;
    }
  }
  aIntervalIndex = length;
  return false;
}

void HTMLMediaElement::Seek(double aTime, SeekTarget::Type aSeekType,
                            ErrorResult& aRv) {
  // Note: Seek is called both by synchronous code that expects errors thrown in
  // aRv, as well as asynchronous code that expects a promise. Make sure all
  // synchronous errors are returned using aRv, not promise rejections.

  // aTime should be non-NaN.
  MOZ_ASSERT(!std::isnan(aTime));

  // Seeking step1, Set the media element's show poster flag to false.
  // https://html.spec.whatwg.org/multipage/media.html#dom-media-seek
  mShowPoster = false;

  // Detect if user has interacted with element by seeking so that
  // play will not be blocked when initiated by a script.
  if (UserActivation::IsHandlingUserInput()) {
    mIsBlessed = true;
  }

  StopSuspendingAfterFirstFrame();

  if (mSrcAttrStream) {
    // do nothing since media streams have an empty Seekable range.
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  UpdatePlayedRangesBeforeSeek(CurrentTime());

  if (mReadyState == HAVE_NOTHING) {
    mDefaultPlaybackStartPosition = aTime;
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  if (!mDecoder) {
    // mDecoder must always be set in order to reach this point.
    NS_ASSERTION(mDecoder, "SetCurrentTime failed: no decoder");
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  // Clamp the seek target to inside the seekable ranges.
  media::TimeRanges seekableRanges = mDecoder->GetSeekableTimeRanges();
  if (seekableRanges.IsInvalid()) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }
  RefPtr<TimeRanges> seekable =
      new TimeRanges(ToSupports(OwnerDoc()), seekableRanges);
  uint32_t length = seekable->Length();
  if (length == 0) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  // If the position we want to seek to is not in a seekable range, we seek
  // to the closest position in the seekable ranges instead. If two positions
  // are equally close, we seek to the closest position from the currentTime.
  // See seeking spec, point 7 :
  // http://www.whatwg.org/specs/web-apps/current-work/multipage/the-video-element.html#seeking
  uint32_t range = 0;
  bool isInRange = IsInRanges(*seekable, aTime, range);
  if (!isInRange) {
    if (range == 0) {
      // aTime is before the first range in |seekable|, the closest point we can
      // seek to is the start of the first range.
      aTime = seekable->Start(0);
    } else if (range == length) {
      // Seek target is after the end last range in seekable data.
      // Clamp the seek target to the end of the last seekable range.
      aTime = seekable->End(length - 1);
    } else {
      double leftBound = seekable->End(range - 1);
      double rightBound = seekable->Start(range);
      double distanceLeft = Abs(leftBound - aTime);
      double distanceRight = Abs(rightBound - aTime);
      if (distanceLeft == distanceRight) {
        double currentTime = CurrentTime();
        distanceLeft = Abs(leftBound - currentTime);
        distanceRight = Abs(rightBound - currentTime);
      }
      aTime = (distanceLeft < distanceRight) ? leftBound : rightBound;
    }
  }

  // TODO: The spec requires us to update the current time to reflect the
  //       actual seek target before beginning the synchronous section, but
  //       that requires changing all MediaDecoderReaders to support telling
  //       us the fastSeek target, and it's currently not possible to get
  //       this information as we don't yet control the demuxer for all
  //       MediaDecoderReaders.

  mPlayingBeforeSeek = IsPotentiallyPlaying();

  // The media backend is responsible for dispatching the timeupdate
  // event if it changes the playback position as a result of the seek.
  LOG(LogLevel::Debug,
      ("{} SetCurrentTime({}) starting seek", fmt::ptr(this), aTime));
  AddStates(ElementState::SEEKING);
  mDecoder->Seek(aTime, aSeekType);

  // We changed whether we're seeking so we need to AddRemoveSelfReference.
  AddRemoveSelfReference();

  mMediaControlKeyListener->NotifyMediaPositionState();
}

double HTMLMediaElement::Duration() const {
  if (mSrcStream) {
    if (mSrcStreamPlaybackEnded) {
      return CurrentTime();
    }
    return std::numeric_limits<double>::infinity();
  }

  if (mDecoder) {
    return mDecoder->GetDuration();
  }

  return std::numeric_limits<double>::quiet_NaN();
}

already_AddRefed<TimeRanges> HTMLMediaElement::Seekable() const {
  media::TimeRanges seekable =
      mDecoder ? mDecoder->GetSeekableTimeRanges() : media::TimeRanges();
  RefPtr<TimeRanges> ranges = new TimeRanges(
      ToSupports(OwnerDoc()), seekable.ToMicrosecondResolution());
  return ranges.forget();
}

already_AddRefed<TimeRanges> HTMLMediaElement::Played() {
  RefPtr<TimeRanges> ranges = new TimeRanges(ToSupports(OwnerDoc()));

  uint32_t timeRangeCount = 0;
  if (mPlayed) {
    timeRangeCount = mPlayed->Length();
  }
  for (uint32_t i = 0; i < timeRangeCount; i++) {
    double begin = mPlayed->Start(i);
    double end = mPlayed->End(i);
    ranges->Add(begin, end);
  }

  if (mCurrentPlayRangeStart) {
    double now = CurrentTime();
    if (mCurrentPlayRangeStart.value() != now) {
      // Don't round the left of the interval: it comes from script and needs
      // to be exact.
      ranges->Add(mCurrentPlayRangeStart.value(), now);
    }
  }

  ranges->Normalize();
  return ranges.forget();
}

void HTMLMediaElement::Pause(ErrorResult& aRv) {
  LOG(LogLevel::Debug, ("{} Pause() called by JS", fmt::ptr(this)));
  if (mNetworkState == NETWORK_EMPTY) {
    LOG(LogLevel::Debug, ("Loading due to Pause()"));
    DoLoad();
  }
  PauseInternal();
}

void HTMLMediaElement::PauseInternal() {
  RefPtr<HTMLMediaElement> kungFuDeathGrip(this);
  if (mDecoder && mNetworkState != NETWORK_EMPTY) {
    mDecoder->Pause();
  }
  bool oldPaused = mPaused;
  mPaused = true;
  UpdatePlaybackPseudoClasses();
  // Step 1,
  // https://html.spec.whatwg.org/multipage/media.html#internal-pause-steps
  mCanAutoplayFlag = false;
  // We changed mPaused and mCanAutoplayFlag which can affect
  // AddRemoveSelfReference
  AddRemoveSelfReference();
  UpdateSrcMediaStreamPlaying();
  if (mAudioChannelWrapper) {
    mAudioChannelWrapper->NotifyPlayStateChanged();
  }
  NotifyAudioPlaybackChanged(
      AudioChannelService::AudibleChangedReasons::ePauseStateChanged);

  // We don't need to resume media which is paused explicitly by user.
  ClearResumeDelayedMediaPlaybackAgentIfNeeded();

  if (!oldPaused) {
    FireTimeUpdate(TimeupdateType::eMandatory);
    QueueEvent(u"pause"_ns);
    AsyncRejectPendingPlayPromises(NS_ERROR_DOM_MEDIA_ABORT_ERR);
  }

  if (mMediaControlKeyListener) {
    mMediaControlKeyListener->NotifyPaused();
  }
}

void HTMLMediaElement::SetVolume(double aVolume, ErrorResult& aRv) {
  LOG(LogLevel::Debug,
      ("{} SetVolume({}) called by JS", fmt::ptr(this), aVolume));

  if (aVolume < 0.0 || aVolume > 1.0) {
    aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
    return;
  }

  if (aVolume == mVolume) {
    return;
  }

  mVolume = aVolume;

  // Here we want just to update the volume.
  SetVolumeInternal();

  QueueEvent(u"volumechange"_ns);

  // We allow inaudible autoplay. But changing our volume may make this
  // media audible. So pause if we are no longer supposed to be autoplaying.
  PauseIfShouldNotBePlaying();
}

void HTMLMediaElement::MozGetMetadata(JSContext* aCx,
                                      JS::MutableHandle<JSObject*> aResult,
                                      ErrorResult& aRv) {
  if (mReadyState < HAVE_METADATA) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  JS::Rooted<JSObject*> tags(aCx, JS_NewPlainObject(aCx));
  if (!tags) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }
  if (mTags) {
    for (const auto& entry : *mTags) {
      nsString wideValue;
      CopyUTF8toUTF16(entry.GetData(), wideValue);
      JS::Rooted<JSString*> string(aCx,
                                   JS_NewUCStringCopyZ(aCx, wideValue.Data()));
      if (!string || !JS_DefineProperty(aCx, tags, entry.GetKey().Data(),
                                        string, JSPROP_ENUMERATE)) {
        NS_WARNING("couldn't create metadata object!");
        aRv.Throw(NS_ERROR_FAILURE);
        return;
      }
    }
  }

  aResult.set(tags);
}

void HTMLMediaElement::SetMutedInternal(uint32_t aMuted) {
  uint32_t oldMuted = mMuted;
  mMuted = aMuted;

  // The :muted pseudo-class follows the muted getter, so update it whenever a
  // reason that the getter reflects changes, independently of whether the
  // overall muted bitmask becomes (non-)empty below.
  constexpr uint32_t kReflectedByGetter =
      MUTED_BY_CONTENT | MUTED_BY_INVALID_PLAYBACK_RATE;
  if ((oldMuted & kReflectedByGetter) != (aMuted & kReflectedByGetter)) {
    // https://html.spec.whatwg.org/multipage/semantics-other.html#selector-muted
    SetStates(ElementState::MUTED, Muted());
  }

  if (!!aMuted == !!oldMuted) {
    return;
  }

  SetVolumeInternal();
}

void HTMLMediaElement::PauseIfShouldNotBePlaying() {
  if (GetPaused()) {
    return;
  }
  if (!AllowedToPlay()) {
    AUTOPLAY_LOG("pause because not allowed to play, element={}",
                 fmt::ptr(this));
    ErrorResult rv;
    Pause(rv);
  }
}

void HTMLMediaElement::SetVolumeInternal() {
  float effectiveVolume = ComputedVolume();

  if (mDecoder) {
    mDecoder->SetVolume(effectiveVolume);
  } else if (mMediaStreamRenderer) {
    mMediaStreamRenderer->SetAudioOutputVolume(effectiveVolume);
  }

  NotifyAudioPlaybackChanged(
      AudioChannelService::AudibleChangedReasons::eVolumeChanged);
  mEffectiveVolumeChangeEvent.Notify(effectiveVolume);
}

static const char* MutedReasonToStr(HTMLMediaElement::MutedReasons aReason) {
  switch (aReason) {
    case HTMLMediaElement::MUTED_BY_CONTENT:
      return "content";
    case HTMLMediaElement::MUTED_BY_INVALID_PLAYBACK_RATE:
      return "invalid-playback-rate";
    case HTMLMediaElement::MUTED_BY_AUDIO_CHANNEL:
      return "audio-channel";
    case HTMLMediaElement::MUTED_BY_AUDIO_TRACK:
      return "audio-track";
    case HTMLMediaElement::MUTED_BY_MEDIA_CONTROL:
      return "media-control";
  }
  MOZ_ASSERT_UNREACHABLE("Unknown MutedReasons value");
  return "unknown";
}

void HTMLMediaElement::SetMuted(bool aMuted, MutedReasons aReason) {
  LOG(LogLevel::Debug, ("{} SetMuted({}) reason={}", fmt::ptr(this), aMuted,
                        MutedReasonToStr(aReason)));

  // Only the web-facing content setter latches the muted state (to True or
  // False); afterwards the muted content attribute no longer affects the muted
  // getter. The state is latched even when the computed muted value is
  // unchanged, so that later removing the content attribute does not unmute the
  // element. Other reasons (audio channel, media control, etc.) must not affect
  // this content-attribute relationship.
  // https://html.spec.whatwg.org/multipage/media.html#dom-media-muted
  if (aReason == MUTED_BY_CONTENT) {
    mMutedState = aMuted ? MutedState::True : MutedState::False;
    // The setter has taken over; the content attribute no longer applies.
    mMutedByRuntimeContentAttr = false;
  }

  bool wasMuted = Muted();
  if (aMuted) {
    SetMutedInternal(mMuted | aReason);
  } else {
    SetMutedInternal(mMuted & ~aReason);
  }

  if (Muted() == wasMuted) {
    return;
  }

  QueueEvent(u"volumechange"_ns);

  // We allow inaudible autoplay. But changing our mute status may make this
  // media audible. So pause if we are no longer supposed to be autoplaying.
  PauseIfShouldNotBePlaying();
}

void HTMLMediaElement::GetAllEnabledMediaTracks(
    nsTArray<RefPtr<MediaTrack>>& aTracks) {
  if (AudioTrackList* tracks = AudioTracks()) {
    for (size_t i = 0; i < tracks->Length(); ++i) {
      AudioTrack* track = (*tracks)[i];
      if (track->Enabled()) {
        aTracks.AppendElement(track);
      }
    }
  }
  if (IsVideo()) {
    if (VideoTrackList* tracks = VideoTracks()) {
      for (size_t i = 0; i < tracks->Length(); ++i) {
        VideoTrack* track = (*tracks)[i];
        if (track->Selected()) {
          aTracks.AppendElement(track);
        }
      }
    }
  }
}

void HTMLMediaElement::SetCapturedOutputStreamsEnabled(bool aEnabled) {
  for (const auto& entry : mOutputTrackSources.Values()) {
    entry->SetEnabled(aEnabled);
  }
}

HTMLMediaElement::OutputMuteState HTMLMediaElement::OutputTracksMuted() {
  return mPaused || mReadyState <= HAVE_CURRENT_DATA ? OutputMuteState::Muted
                                                     : OutputMuteState::Unmuted;
}

void HTMLMediaElement::UpdateOutputTracksMuting() {
  for (const auto& entry : mOutputTrackSources.Values()) {
    entry->SetMutedByElement(OutputTracksMuted());
  }
}

void HTMLMediaElement::AddOutputTrackSourceToOutputStream(
    MediaElementTrackSource* aSource, OutputMediaStream& aOutputStream,
    AddTrackMode aMode) {
  if (aOutputStream.mStream == mSrcStream) {
    // Cycle detected. This can happen since tracks are added async.
    // We avoid forwarding it to the output here or we'd get into an infloop.
    LOG(LogLevel::Warning,
        ("{} NOT adding output track source {} to output stream "
         "{} -- cycle detected",
         fmt::ptr(this), fmt::ptr(aSource),
         fmt::ptr(aOutputStream.mStream.get())));
    return;
  }

  LOG(LogLevel::Debug,
      ("{} Adding output track source {} to output stream {} (mode={})",
       fmt::ptr(this), fmt::ptr(aSource), fmt::ptr(aOutputStream.mStream.get()),
       aMode == AddTrackMode::ASYNC ? "async" : "sync"));

  RefPtr<MediaStreamTrack> domTrack;
  if (aSource->Track()->mType == MediaSegment::AUDIO) {
    domTrack = new AudioStreamTrack(
        aOutputStream.mStream->GetOwnerWindow(), aSource->Track(), aSource,
        MediaStreamTrackState::Live, aSource->Muted());
  } else {
    domTrack = new VideoStreamTrack(
        aOutputStream.mStream->GetOwnerWindow(), aSource->Track(), aSource,
        MediaStreamTrackState::Live, aSource->Muted());
  }

  aOutputStream.mLiveTracks.AppendElement(domTrack);

  switch (aMode) {
    case AddTrackMode::ASYNC:
      GetMainThreadSerialEventTarget()->Dispatch(
          NewRunnableMethod<StoreRefPtrPassByPtr<MediaStreamTrack>>(
              "DOMMediaStream::AddTrackInternal", aOutputStream.mStream,
              &DOMMediaStream::AddTrackInternal, domTrack));
      break;
    case AddTrackMode::SYNC:
      aOutputStream.mStream->AddTrackInternal(domTrack);
      break;
    default:
      MOZ_CRASH("Unexpected mode");
  }

  LOG(LogLevel::Debug, ("{} Created capture {} track {}", fmt::ptr(this),
                        domTrack->AsAudioStreamTrack() ? "audio" : "video",
                        fmt::ptr(domTrack.get())));
}

bool HTMLMediaElement::ShouldHaveTrackSources() const {
  return mTracksCaptured.Ref() && !IsPlaybackEnded() &&
         mReadyState >= HAVE_METADATA;
}

void HTMLMediaElement::UpdateOutputTrackSources() {
  // This updates the track sources in mOutputTrackSources so they're in sync
  // with the tracks being currently played, and state saying whether we should
  // be capturing tracks. This method is long so here is a breakdown:
  // - Figure out the tracks that should be captured
  // - Diff those against currently captured tracks (mOutputTrackSources), into
  //   tracks-to-add, and tracks-to-remove
  // - Remove the tracks in tracks-to-remove and dispatch "removetrack" and
  //   "ended" events for them
  // - If playback has ended, or there is no longer a media provider object,
  //   remove any OutputMediaStreams that have the finish-when-ended flag set
  // - Create track sources for, and add to OutputMediaStreams, the tracks in
  //   tracks-to-add
  const bool shouldHaveTrackSources = ShouldHaveTrackSources();

  // Add track sources for all enabled/selected MediaTracks.
  nsPIDOMWindowInner* window = OwnerDoc()->GetInnerWindow();
  if (!window) {
    return;
  }

  if (mDecoder) {
    if (!mTracksCaptured.Ref()) {
      mDecoder->SetOutputCaptureState(MediaDecoder::OutputCaptureInfo(
          MediaDecoder::OutputCaptureState::None));
    } else if (!AudioTracks() || !VideoTracks() || !shouldHaveTrackSources) {
      // We've been unlinked, or tracks are not yet known.
      mDecoder->SetOutputCaptureState(MediaDecoder::OutputCaptureInfo(
          MediaDecoder::OutputCaptureState::Halt));
    } else {
      mDecoder->SetOutputCaptureState(MediaDecoder::OutputCaptureInfo(
          MediaDecoder::OutputCaptureState::Capture,
          mTracksCaptured.Ref().get(),
          mAudioOutputConfig == AudioOutputConfig::Needed, mSink.second));
    }
  }

  // Start with all MediaTracks
  AutoTArray<RefPtr<MediaTrack>, 4> mediaTracksToAdd;
  if (shouldHaveTrackSources) {
    GetAllEnabledMediaTracks(mediaTracksToAdd);
  }

  // ...and all MediaElementTrackSources.
  auto trackSourcesToRemove =
      ToTArray<AutoTArray<nsString, 4>>(mOutputTrackSources.Keys());

  // Then work out the differences.
  mediaTracksToAdd.RemoveLastElements(
      mediaTracksToAdd.end() -
      std::remove_if(mediaTracksToAdd.begin(), mediaTracksToAdd.end(),
                     [this, &trackSourcesToRemove](const auto& track) {
                       const bool remove =
                           mOutputTrackSources.GetWeak(track->GetId());
                       if (remove) {
                         trackSourcesToRemove.RemoveElement(track->GetId());
                       }
                       return remove;
                     }));

  // First remove stale track sources.
  for (const auto& id : trackSourcesToRemove) {
    RefPtr<MediaElementTrackSource> source = mOutputTrackSources.GetWeak(id);

    LOG(LogLevel::Debug,
        ("Removing output track source {} for track {}", fmt::ptr(source.get()),
         NS_ConvertUTF16toUTF8(id).get()));

    if (mDecoder) {
      mDecoder->RemoveOutputTrack(source->Track());
    }

    // The source of this track just ended. Force-notify that it ended.
    // If we bounce it to the MediaTrackGraph it might not be picked up,
    // for instance if the MediaInputPort was destroyed in the same
    // iteration as it was added.
    GetMainThreadSerialEventTarget()->Dispatch(
        NewRunnableMethod("MediaElementTrackSource::OverrideEnded", source,
                          &MediaElementTrackSource::OverrideEnded));

    // Remove the track from the MediaStream after it ended.
    for (OutputMediaStream& ms : mOutputStreams) {
      if (source->Track()->mType == MediaSegment::VIDEO &&
          ms.mCapturingAudioOnly) {
        continue;
      }
      DebugOnly<size_t> length = ms.mLiveTracks.Length();
      ms.mLiveTracks.RemoveElementsBy(
          [&](const RefPtr<MediaStreamTrack>& aTrack) {
            if (&aTrack->GetSource() != source) {
              return false;
            }
            GetMainThreadSerialEventTarget()->Dispatch(
                NewRunnableMethod<RefPtr<MediaStreamTrack>>(
                    "DOMMediaStream::RemoveTrackInternal", ms.mStream,
                    &DOMMediaStream::RemoveTrackInternal, aTrack));
            return true;
          });
      MOZ_ASSERT(ms.mLiveTracks.Length() == length - 1);
    }

    mOutputTrackSources.Remove(id);
  }

  // Then update finish-when-ended output streams as needed.
  for (size_t i = mOutputStreams.Length(); i-- > 0;) {
    if (!mOutputStreams[i].mFinishWhenEnded) {
      continue;
    }

    if (!mOutputStreams[i].mFinishWhenEndedLoadingSrc &&
        !mOutputStreams[i].mFinishWhenEndedAttrStream &&
        !mOutputStreams[i].mFinishWhenEndedMediaSource) {
      // This finish-when-ended stream has not seen any source loaded yet.
      // Update the loading src if it's time.
      if (!IsPlaybackEnded()) {
        if (mLoadingSrc) {
          mOutputStreams[i].mFinishWhenEndedLoadingSrc = mLoadingSrc;
        } else if (mSrcAttrStream) {
          mOutputStreams[i].mFinishWhenEndedAttrStream = mSrcAttrStream;
        } else if (mSrcMediaSource) {
          mOutputStreams[i].mFinishWhenEndedMediaSource = mSrcMediaSource;
        }
      }
      continue;
    }

    // Discard finish-when-ended output streams with a loading src set as
    // needed.
    if (!IsPlaybackEnded() &&
        mLoadingSrc == mOutputStreams[i].mFinishWhenEndedLoadingSrc) {
      continue;
    }
    if (!IsPlaybackEnded() &&
        mSrcAttrStream == mOutputStreams[i].mFinishWhenEndedAttrStream) {
      continue;
    }
    if (!IsPlaybackEnded() &&
        mSrcMediaSource == mOutputStreams[i].mFinishWhenEndedMediaSource) {
      continue;
    }
    LOG(LogLevel::Debug,
        ("Playback ended or source changed. Discarding stream {}",
         fmt::ptr(mOutputStreams[i].mStream.get())));
    mOutputStreams.RemoveElementAt(i);
    if (mOutputStreams.IsEmpty()) {
      mTracksCaptured = nullptr;
      // mTracksCaptured is one of the Watchables triggering this method.
      // Unsetting it here means we'll run through this method again very soon.
      return;
    }
  }

  // Finally add new MediaTracks.
  for (const auto& mediaTrack : mediaTracksToAdd) {
    nsAutoString id;
    mediaTrack->GetId(id);

    MediaSegment::Type type;
    if (mediaTrack->AsAudioTrack()) {
      type = MediaSegment::AUDIO;
    } else if (mediaTrack->AsVideoTrack()) {
      type = MediaSegment::VIDEO;
    } else {
      MOZ_CRASH("Unknown track type");
    }

    RefPtr<ProcessedMediaTrack> track;
    RefPtr<MediaElementTrackSource> source;
    if (mDecoder) {
      track = mTracksCaptured.Ref()->mTrack->Graph()->CreateForwardedInputTrack(
          type);
      RefPtr<nsIPrincipal> principal = GetCurrentPrincipal();
      if (!principal || IsCORSSameOrigin()) {
        principal = NodePrincipal();
      }
      source = MakeAndAddRef<MediaElementTrackSource>(
          this, track, principal, OutputTracksMuted(),
          type == MediaSegment::VIDEO
              ? HTMLVideoElement::FromNode(this)->HasAlpha()
              : false);
      mDecoder->AddOutputTrack(track);
    } else if (mSrcStream) {
      MediaStreamTrack* inputTrack;
      if (AudioTrack* t = mediaTrack->AsAudioTrack()) {
        inputTrack = t->GetAudioStreamTrack();
      } else if (VideoTrack* t = mediaTrack->AsVideoTrack()) {
        inputTrack = t->GetVideoStreamTrack();
      } else {
        MOZ_CRASH("Unknown track type");
      }
      MOZ_ASSERT(inputTrack);
      if (!inputTrack) {
        NS_ERROR("Input track not found in source stream");
        return;
      }
      MOZ_DIAGNOSTIC_ASSERT(!inputTrack->Ended());

      track = inputTrack->Graph()->CreateForwardedInputTrack(type);
      RefPtr<MediaInputPort> port = inputTrack->ForwardTrackContentsTo(track);
      source = MakeAndAddRef<MediaElementTrackSource>(
          this, inputTrack, &inputTrack->GetSource(), track, port,
          OutputTracksMuted());

      // Track is muted initially, so we don't leak data if it's added while
      // paused and an MTG iteration passes before the mute comes into effect.
      source->SetEnabled(mSrcStreamIsPlaying);
    } else {
      MOZ_CRASH("Unknown source");
    }

    LOG(LogLevel::Debug,
        ("Adding output track source {} for track {}", fmt::ptr(source.get()),
         NS_ConvertUTF16toUTF8(id).get()));

    track->QueueSetAutoend(false);
    MOZ_DIAGNOSTIC_ASSERT(!mOutputTrackSources.Contains(id));
    mOutputTrackSources.InsertOrUpdate(id, RefPtr{source});

    // Add the new track source to any existing output streams
    for (OutputMediaStream& ms : mOutputStreams) {
      if (source->Track()->mType == MediaSegment::VIDEO &&
          ms.mCapturingAudioOnly) {
        // If the output stream is for audio only we ignore video sources.
        continue;
      }
      AddOutputTrackSourceToOutputStream(source, ms);
    }
  }
}

bool HTMLMediaElement::CanBeCaptured(StreamCaptureType aCaptureType,
                                     ErrorResult& aRv) {
  // Don't bother capturing when the document has gone away. This can't surface
  // to JS since there is no execution context without a window, so a typed
  // DOMException is not needed here.
  if (!OwnerDoc()->GetInnerWindow()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return false;
  }

  // Prevent capturing restricted video. See
  // https://github.com/w3c/mediacapture-fromelement/issues/20
  if (aCaptureType == StreamCaptureType::CAPTURE_ALL_TRACKS &&
      ContainsRestrictedContent()) {
    aRv.ThrowNotSupportedError(
        "Capture of media from an EME-protected element is not supported");
    return false;
  }
  return true;
}

already_AddRefed<DOMMediaStream> HTMLMediaElement::CaptureStreamInternal(
    StreamCaptureBehavior aFinishBehavior, StreamCaptureType aStreamCaptureType,
    AudioOutputConfig aAudioOutputConfig, MediaTrackGraph* aGraph) {
  IgnoredErrorResult rv;
  MOZ_ASSERT(CanBeCaptured(aStreamCaptureType, rv));

  LogVisibility(CallerAPI::CAPTURE_STREAM);
  MarkAsTainted();

  // Once the audio output configuration is set to NotNeeded, the state remains
  // permanent.
  bool shouldRemoveAudioConfig =
      mAudioOutputConfig != aAudioOutputConfig &&
      aAudioOutputConfig == AudioOutputConfig::NotNeeded;
  mAudioOutputConfig = (mAudioOutputConfig == AudioOutputConfig::NotNeeded ||
                        aAudioOutputConfig == AudioOutputConfig::NotNeeded)
                           ? AudioOutputConfig::NotNeeded
                           : AudioOutputConfig::Needed;

  LOG(LogLevel::Debug,
      ("{} CaptureStreamInternal, behavior={}, type={}, needAudioConfig={}",
       fmt::ptr(this), static_cast<uint8_t>(aFinishBehavior),
       static_cast<uint8_t>(aStreamCaptureType),
       aAudioOutputConfig == AudioOutputConfig::Needed));

  if (mTracksCaptured.Ref()) {
    // Already have an output stream.  Check whether the graph rate matches if
    // specified.
    if (aGraph && aGraph != mTracksCaptured.Ref()->mTrack->Graph()) {
      return nullptr;
    }
    // Audio output was configured already but now we need to remove it. Eg.
    // MediaElementAudioSourceNode is connected so audio should be heard from
    // WebAudio's graph.
    if (shouldRemoveAudioConfig && (AudioTracks() || VideoTracks()) &&
        ShouldHaveTrackSources() && mDecoder) {
      MOZ_ASSERT(mAudioOutputConfig == AudioOutputConfig::NotNeeded);
      LOG(LogLevel::Debug,
          ("{} Update decoder capture state to remove audio output",
           fmt::ptr(this)));
      mDecoder->SetOutputCaptureState(MediaDecoder::OutputCaptureInfo(
          MediaDecoder::OutputCaptureState::Capture,
          mTracksCaptured.Ref().get(), /*aShouldConfigAudioOutput=*/false,
          mSink.second));
    }
  } else {
    // This is the first output stream, or there are no tracks. If the former,
    // start capturing all tracks. If the latter, they will be added later.
    MediaTrackGraph* graph = aGraph;
    if (!graph) {
      nsPIDOMWindowInner* window = OwnerDoc()->GetInnerWindow();
      if (!window) {
        return nullptr;
      }

      // A new graph will choose a SystemClockDriver until an audio output is
      // added, so don't open an audio stream up front.
      graph = MediaTrackGraph::GetInstance(
          MediaTrackGraph::SYSTEM_THREAD_DRIVER, window,
          MediaTrackGraph::REQUEST_DEFAULT_SAMPLE_RATE,
          MediaTrackGraph::DEFAULT_OUTPUT_DEVICE);
    }
    mTracksCaptured = MakeRefPtr<SharedDummyTrack>(
        graph->CreateSourceTrack(MediaSegment::AUDIO));
    UpdateOutputTrackSources();
  }

  nsPIDOMWindowInner* window = OwnerDoc()->GetInnerWindow();
  OutputMediaStream* out = mOutputStreams.EmplaceBack(
      MakeRefPtr<DOMMediaStream>(window),
      aStreamCaptureType == StreamCaptureType::CAPTURE_AUDIO,
      aFinishBehavior == StreamCaptureBehavior::FINISH_WHEN_ENDED);

  if (aFinishBehavior == StreamCaptureBehavior::FINISH_WHEN_ENDED &&
      !mOutputTrackSources.IsEmpty()) {
    // This output stream won't receive any more tracks when playback of the
    // current src of this media element ends, or when the src of this media
    // element changes. If we're currently playing something (i.e., if there are
    // tracks currently captured), set the current src on the output stream so
    // this can be tracked. If we're not playing anything,
    // UpdateOutputTrackSources will set the current src when it becomes
    // available later.
    if (mLoadingSrc) {
      out->mFinishWhenEndedLoadingSrc = mLoadingSrc;
    }
    if (mSrcAttrStream) {
      out->mFinishWhenEndedAttrStream = mSrcAttrStream;
    }
    if (mSrcMediaSource) {
      out->mFinishWhenEndedMediaSource = mSrcMediaSource;
    }
    MOZ_ASSERT(out->mFinishWhenEndedLoadingSrc ||
               out->mFinishWhenEndedAttrStream ||
               out->mFinishWhenEndedMediaSource);
  }

  if (aStreamCaptureType == StreamCaptureType::CAPTURE_AUDIO) {
    // mAudioCaptured tells the user that the audio played by this media element
    // is being routed to the captureStreams *instead* of being played to
    // speakers.
    mAudioCaptured = true;
  }

  for (const RefPtr<MediaElementTrackSource>& source :
       mOutputTrackSources.Values()) {
    if (source->Track()->mType == MediaSegment::VIDEO) {
      // Only add video tracks if we're a video element and the output stream
      // wants video.
      if (!IsVideo()) {
        continue;
      }
      if (out->mCapturingAudioOnly) {
        continue;
      }
    }
    AddOutputTrackSourceToOutputStream(source, *out, AddTrackMode::SYNC);
  }

  return do_AddRef(out->mStream);
}

already_AddRefed<DOMMediaStream> HTMLMediaElement::CaptureAudio(
    ErrorResult& aRv, MediaTrackGraph* aGraph) {
  MOZ_RELEASE_ASSERT(aGraph);

  if (!CanBeCaptured(StreamCaptureType::CAPTURE_AUDIO, aRv)) {
    return nullptr;
  }

  mozilla::glean::media::capture_stream_usage
      .EnumGet(mozilla::glean::media::CaptureStreamUsageLabel::
                   eAudiosourcenodecaptured)
      .Add();
  RefPtr<DOMMediaStream> stream = CaptureStreamInternal(
      StreamCaptureBehavior::CONTINUE_WHEN_ENDED,
      StreamCaptureType::CAPTURE_AUDIO, AudioOutputConfig::NotNeeded, aGraph);
  if (!stream) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  return stream.forget();
}

RefPtr<GenericNonExclusivePromise> HTMLMediaElement::GetAllowedToPlayPromise() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mOutputStreams.IsEmpty(),
             "This method should only be called during stream capturing!");
  if (AllowedToPlay()) {
    AUTOPLAY_LOG("MediaElement {} has allowed to play, resolve promise",
                 fmt::ptr(this));
    return GenericNonExclusivePromise::CreateAndResolve(true, __func__);
  }
  AUTOPLAY_LOG("create allow-to-play promise for MediaElement {}",
               fmt::ptr(this));
  return mAllowedToPlayPromise.Ensure(__func__);
}

already_AddRefed<DOMMediaStream> HTMLMediaElement::MozCaptureStream(
    ErrorResult& aRv) {
  if (StaticPrefs::media_captureStream_enabled()) {
    ReportToConsole(nsIScriptError::warningFlag,
                    "MozCaptureStreamDeprecatedWarning");
  }
  if (!CanBeCaptured(StreamCaptureType::CAPTURE_ALL_TRACKS, aRv)) {
    return nullptr;
  }

  mozilla::glean::media::capture_stream_usage
      .EnumGet(
          mozilla::glean::media::CaptureStreamUsageLabel::eMozcapturestream)
      .Add();
  // TODO : our mozCaptureStreamXXX implementation doesn't render audio
  // output, which is not spec compliant. A proper implementation will be
  // implemented in CaptureStream() which will allow audio playback. We don't
  // want to change its behavior as that might be breaking for existing
  // sites which are already using a workaround for audio playback.
  RefPtr<DOMMediaStream> stream =
      CaptureStreamInternal(StreamCaptureBehavior::CONTINUE_WHEN_ENDED,
                            StreamCaptureType::CAPTURE_ALL_TRACKS,
                            AudioOutputConfig::NotNeeded, nullptr);
  if (!stream) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  return stream.forget();
}

already_AddRefed<DOMMediaStream> HTMLMediaElement::MozCaptureStreamUntilEnded(
    ErrorResult& aRv) {
  if (StaticPrefs::media_captureStream_enabled()) {
    ReportToConsole(nsIScriptError::warningFlag,
                    "MozCaptureStreamDeprecatedWarning");
  }
  if (!CanBeCaptured(StreamCaptureType::CAPTURE_ALL_TRACKS, aRv)) {
    return nullptr;
  }

  mozilla::glean::media::capture_stream_usage
      .EnumGet(mozilla::glean::media::CaptureStreamUsageLabel::
                   eMozcapturestreamuntilended)
      .Add();
  // TODO : our mozCaptureStreamXXX implementation doesn't render audio
  // output, which is not spec compliant. A proper implementation will be
  // implemented in CaptureStream() which will allow audio playback. We don't
  // want to change its behavior as that might be breaking for existing
  // sites which are already using a workaround for audio playback.
  RefPtr<DOMMediaStream> stream =
      CaptureStreamInternal(StreamCaptureBehavior::FINISH_WHEN_ENDED,
                            StreamCaptureType::CAPTURE_ALL_TRACKS,
                            AudioOutputConfig::NotNeeded, nullptr);
  if (!stream) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  return stream.forget();
}

already_AddRefed<DOMMediaStream> HTMLMediaElement::CaptureStream(
    ErrorResult& aRv) {
  // Spec issue https://github.com/w3c/mediacapture-fromelement/issues/20
  // We began blocking the capture of encrypted playback in bug 1071482.
  if (!CanBeCaptured(StreamCaptureType::CAPTURE_ALL_TRACKS, aRv)) {
    return nullptr;
  }

  mozilla::glean::media::capture_stream_usage
      .EnumGet(mozilla::glean::media::CaptureStreamUsageLabel::eCapturestream)
      .Add();
  RefPtr<DOMMediaStream> stream =
      CaptureStreamInternal(StreamCaptureBehavior::CONTINUE_WHEN_ENDED,
                            StreamCaptureType::CAPTURE_ALL_TRACKS,
                            AudioOutputConfig::Needed, nullptr);
  if (!stream) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  return stream.forget();
}

class MediaElementSetForURI : public nsURIHashKey {
 public:
  explicit MediaElementSetForURI(const nsIURI* aKey) : nsURIHashKey(aKey) {}
  MediaElementSetForURI(MediaElementSetForURI&& aOther) noexcept
      : nsURIHashKey(std::move(aOther)),
        mElements(std::move(aOther.mElements)) {}
  nsTArray<HTMLMediaElement*> mElements;
};

using MediaElementURITable = nsTHashtable<MediaElementSetForURI>;
// Elements in this table must have non-null mDecoder and mLoadingSrc, and those
// can't change while the element is in the table. The table is keyed by
// the element's mLoadingSrc. Each entry has a list of all elements with the
// same mLoadingSrc.
static MediaElementURITable* gElementTable;

#ifdef DEBUG
static bool URISafeEquals(nsIURI* a1, nsIURI* a2) {
  if (!a1 || !a2) {
    // Consider two empty URIs *not* equal!
    return false;
  }
  bool equal = false;
  nsresult rv = a1->Equals(a2, &equal);
  return NS_SUCCEEDED(rv) && equal;
}
// Returns the number of times aElement appears in the media element table
// for aURI. If this returns other than 0 or 1, there's a bug somewhere!
static unsigned MediaElementTableCount(HTMLMediaElement* aElement,
                                       nsIURI* aURI) {
  if (!gElementTable || !aElement) {
    return 0;
  }
  uint32_t uriCount = 0;
  uint32_t otherCount = 0;
  for (const auto& entry : *gElementTable) {
    uint32_t count = 0;
    for (const auto& elem : entry.mElements) {
      if (elem == aElement) {
        count++;
      }
    }
    if (URISafeEquals(aURI, entry.GetKey())) {
      uriCount = count;
    } else {
      otherCount += count;
    }
  }
  NS_ASSERTION(otherCount == 0, "Should not have entries for unknown URIs");
  return uriCount;
}
#endif

void HTMLMediaElement::AddMediaElementToURITable() {
  NS_ASSERTION(mDecoder, "Call this only with decoder Load called");
  NS_ASSERTION(
      MediaElementTableCount(this, mLoadingSrc) == 0,
      "Should not have entry for element in element table before addition");
  if (!gElementTable) {
    gElementTable = new MediaElementURITable();
  }
  MediaElementSetForURI* entry = gElementTable->PutEntry(mLoadingSrc);
  entry->mElements.AppendElement(this);
  NS_ASSERTION(
      MediaElementTableCount(this, mLoadingSrc) == 1,
      "Should have a single entry for element in element table after addition");
}

void HTMLMediaElement::RemoveMediaElementFromURITable() {
  if (!mDecoder || !mLoadingSrc || !gElementTable) {
    return;
  }
  MediaElementSetForURI* entry = gElementTable->GetEntry(mLoadingSrc);
  if (!entry) {
    return;
  }
  entry->mElements.RemoveElement(this);
  if (entry->mElements.IsEmpty()) {
    gElementTable->RemoveEntry(entry);
    if (gElementTable->Count() == 0) {
      delete gElementTable;
      gElementTable = nullptr;
    }
  }
  NS_ASSERTION(MediaElementTableCount(this, mLoadingSrc) == 0,
               "After remove, should no longer have an entry in element table");
}

HTMLMediaElement* HTMLMediaElement::LookupMediaElementURITable(nsIURI* aURI) {
  if (!gElementTable) {
    return nullptr;
  }
  MediaElementSetForURI* entry = gElementTable->GetEntry(aURI);
  if (!entry) {
    return nullptr;
  }
  for (uint32_t i = 0; i < entry->mElements.Length(); ++i) {
    HTMLMediaElement* elem = entry->mElements[i];
    bool equal;
    // Look for elements that have the same principal and CORS mode.
    // Ditto for anything else that could cause us to send different headers.
    if (NS_SUCCEEDED(elem->NodePrincipal()->Equals(NodePrincipal(), &equal)) &&
        equal && elem->mCORSMode == mCORSMode) {
      // See SetupDecoder() below. We only add a element to the table when
      // mDecoder is a ChannelMediaDecoder.
      auto* decoder = static_cast<ChannelMediaDecoder*>(elem->mDecoder.get());
      NS_ASSERTION(decoder, "Decoder gone");
      if (decoder->CanClone()) {
        return elem;
      }
    }
  }
  return nullptr;
}

// This GeckoView-specific observer ensures that suspended elements resume
// downloading data or pending play() calls continue after receiving a late
// autoplay permission response, as permissions are granted asynchronously on
// GeckoView.
class HTMLMediaElement::GVAutoplayObserver final : public nsIObserver {
  enum class Phase : int8_t { Init, Subscribed, Unsubscribed };

 public:
  NS_DECL_ISUPPORTS

  explicit GVAutoplayObserver(HTMLMediaElement* aElement)
      : mElement(aElement), mPhase(Phase::Init) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aElement);
#if !defined(MOZ_WIDGET_ANDROID)
    LOG(LogLevel::Error, ("{} GVAutoplayObserver used outside Android!",
                          fmt::ptr(mElement.get())));
    MOZ_ASSERT_UNREACHABLE(
        "GVAutoplayObserver should never be constructed outside of Android.");
#endif
    Subscribe();
  }

 private:
  NS_IMETHOD Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t*) override {
    if (!mElement || !aTopic || !aSubject || (mPhase != Phase::Subscribed) ||
        strcmp(aTopic, kGVAutoplayRequestStatusChangedTopic)) {
      LOG(LogLevel::Debug, ("{} GVAutoplayObserver::Observe observer={} "
                            "Invalid element/topic/subject/phase, skip.",
                            fmt::ptr(mElement.get()), fmt::ptr(this)));
      return NS_OK;
    }

    nsCOMPtr<nsPIDOMWindowInner> inner(do_QueryInterface(aSubject));
    if (!inner) {
      LOG(LogLevel::Debug, ("{} GVAutoplayObserver::Observe observer={} "
                            "Couldn't get inner window from subject, skip.",
                            fmt::ptr(mElement.get()), fmt::ptr(this)));
      return NS_OK;
    }

    RefPtr<dom::BrowsingContext> bcSubject = inner->GetBrowsingContext();
    if (!bcSubject) {
      LOG(LogLevel::Debug, ("{} GVAutoplayObserver::Observe observer={} "
                            "Couldn't get subject browsing context, skip.",
                            fmt::ptr(mElement.get()), fmt::ptr(this)));
      return NS_OK;
    }

    BrowsingContext* bcElem = mElement->OwnerDoc()->GetBrowsingContext();
    if (!bcSubject || !bcElem || bcSubject->Top() != bcElem->Top()) {
      LOG(LogLevel::Debug, ("{} GVAutoplayObserver::Observe observer={} "
                            "Contexts don't match, skip.",
                            fmt::ptr(mElement.get()), fmt::ptr(this)));
      return NS_OK;
    }

    RefPtr<HTMLMediaElement> element = mElement.get();
    if (!element->mPendingPlayPromises.IsEmpty()) {
      if (element->AllowedToPlay()) {
        LOG(LogLevel::Debug, ("{} GVAutoplayObserver::Observe observer={} "
                              "Resuming pending play().",
                              fmt::ptr(element.get()), fmt::ptr(this)));
        element->mAllowedToPlayPromise.ResolveIfExists(true, __func__);
        element->PlayInternal(false);
        element->UpdateCustomPolicyAfterPlayed();
        element->MaybeMarkSHEntryAsUserInteracted();
        return NS_OK;
      }
      if (!element->ShouldDelayPlayUntilGVAutoplayRequestResolved()) {
        LOG(LogLevel::Debug, ("{} GVAutoplayObserver::Observe observer={} "
                              "Rejecting pending play().",
                              fmt::ptr(element.get()), fmt::ptr(this)));
        element->AsyncRejectPendingPlayPromises(
            NS_ERROR_DOM_MEDIA_NOT_ALLOWED_ERR);
        element->StopObservingGVAutoplayIfNeeded();
        return NS_OK;
      }
    }

    if ((element->NetworkState() >= NETWORK_LOADING) &&
        (element->ReadyState() >= HAVE_CURRENT_DATA)) {
      LOG(LogLevel::Debug, ("{} GVAutoplayObserver::Observe observer={} "
                            "Loading or has enough data, skip.",
                            fmt::ptr(element.get()), fmt::ptr(this)));
      return NS_OK;
    }

    // Element needs more data and the autoplay message is valid - update.
    LOG(LogLevel::Debug, ("{} GVAutoplayObserver::Observe observer={} "
                          "Updating preload action.",
                          fmt::ptr(element.get()), fmt::ptr(this)));
    element->UpdatePreloadAction(JSCallingLocation::Get());
    return NS_OK;
  }

  void Subscribe() {
    MOZ_ASSERT(mPhase == Phase::Init);
    MOZ_ASSERT(mElement);
    LOG(LogLevel::Debug, ("{} GVAutoplayObserver::Subscribe observer={}",
                          fmt::ptr(mElement.get()), fmt::ptr(this)));
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (mElement && observerService) {
      observerService->AddObserver(this, kGVAutoplayRequestStatusChangedTopic,
                                   false);
    }
    mPhase = Phase::Subscribed;
  }

  void Unsubscribe() {
    MOZ_ASSERT(mPhase == Phase::Subscribed);
    // mElement might be null
    LOG(LogLevel::Debug, ("{} GVAutoplayObserver::Unsubscribe observer={}",
                          fmt::ptr(mElement.get()), fmt::ptr(this)));
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService) {
      observerService->RemoveObserver(this,
                                      kGVAutoplayRequestStatusChangedTopic);
    }
    mElement = nullptr;
    mPhase = Phase::Unsubscribed;
  }

  virtual ~GVAutoplayObserver() {
    if (mPhase == Phase::Subscribed) {
      Unsubscribe();
    }
    MOZ_ASSERT(!mElement);
  }

  WeakPtr<HTMLMediaElement> mElement = nullptr;
  Phase mPhase = Phase::Init;
};

NS_IMPL_ISUPPORTS(HTMLMediaElement::GVAutoplayObserver, nsIObserver)

class HTMLMediaElement::ShutdownObserver : public nsIObserver {
  enum class Phase : int8_t { Init, Subscribed, Unsubscribed };

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD Observe(nsISupports*, const char* aTopic,
                     const char16_t*) override {
    if (mPhase != Phase::Subscribed) {
      // Bail out if we are not subscribed for this might be called even after
      // |nsContentUtils::UnregisterShutdownObserver(this)|.
      return NS_OK;
    }
    MOZ_DIAGNOSTIC_ASSERT(mWeak);
    if (strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID) == 0) {
      mWeak->NotifyShutdownEvent();
    }
    return NS_OK;
  }
  void Subscribe(HTMLMediaElement* aPtr) {
    MOZ_DIAGNOSTIC_ASSERT(mPhase == Phase::Init);
    MOZ_DIAGNOSTIC_ASSERT(!mWeak);
    mWeak = aPtr;
    nsContentUtils::RegisterShutdownObserver(this);
    mPhase = Phase::Subscribed;
  }
  void Unsubscribe() {
    MOZ_DIAGNOSTIC_ASSERT(mPhase == Phase::Subscribed);
    MOZ_DIAGNOSTIC_ASSERT(mWeak);
    MOZ_DIAGNOSTIC_ASSERT(!mAddRefed,
                          "ReleaseMediaElement should have been called first");
    mWeak = nullptr;
    nsContentUtils::UnregisterShutdownObserver(this);
    mPhase = Phase::Unsubscribed;
  }
  void AddRefMediaElement() {
    MOZ_DIAGNOSTIC_ASSERT(mWeak);
    MOZ_DIAGNOSTIC_ASSERT(!mAddRefed, "Should only ever AddRef once");
    mWeak->AddRef();
    mAddRefed = true;
  }
  void ReleaseMediaElement() {
    MOZ_DIAGNOSTIC_ASSERT(mWeak);
    MOZ_DIAGNOSTIC_ASSERT(mAddRefed, "Should only release after AddRef");
    mWeak->Release();
    mAddRefed = false;
  }

 private:
  virtual ~ShutdownObserver() {
    MOZ_DIAGNOSTIC_ASSERT(mPhase == Phase::Unsubscribed);
    MOZ_DIAGNOSTIC_ASSERT(!mWeak);
    MOZ_DIAGNOSTIC_ASSERT(!mAddRefed,
                          "ReleaseMediaElement should have been called first");
  }
  // Guaranteed to be valid by HTMLMediaElement.
  HTMLMediaElement* mWeak = nullptr;
  Phase mPhase = Phase::Init;
  bool mAddRefed = false;
};

NS_IMPL_ISUPPORTS(HTMLMediaElement::ShutdownObserver, nsIObserver)

class HTMLMediaElement::TitleChangeObserver final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS

  explicit TitleChangeObserver(HTMLMediaElement* aElement)
      : mElement(aElement) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aElement);
  }

  NS_IMETHOD Observe(nsISupports*, const char* aTopic,
                     const char16_t*) override {
    if (mElement) {
      mElement->UpdateStreamName();
    }

    return NS_OK;
  }

  void Subscribe() {
    if (!mIsSubscribed) {
      nsCOMPtr<nsIObserverService> observerService =
          mozilla::services::GetObserverService();
      if (observerService) {
        if (NS_WARN_IF(NS_FAILED(observerService->AddObserver(
                this, "document-title-changed", false)))) {
          return;
        }
        mIsSubscribed = true;
      }
    }
  }

  void Unsubscribe() {
    if (mIsSubscribed) {
      mIsSubscribed = false;
      nsCOMPtr<nsIObserverService> observerService =
          mozilla::services::GetObserverService();
      if (observerService) {
        observerService->RemoveObserver(this, "document-title-changed");
      }
    }
  }

 private:
  ~TitleChangeObserver() = default;

  WeakPtr<HTMLMediaElement> mElement;
  bool mIsSubscribed{false};
};

NS_IMPL_ISUPPORTS(HTMLMediaElement::TitleChangeObserver, nsIObserver)

HTMLMediaElement::HTMLMediaElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
    : nsGenericHTMLElement(std::move(aNodeInfo)),
      mWatchManager(this, AbstractThread::MainThread()),
      mShutdownObserver(new ShutdownObserver),
      mTitleChangeObserver(new TitleChangeObserver(this)),
      mEventBlocker(new EventBlocker(this)),
      mPlayed(new TimeRanges(ToSupports(OwnerDoc()))),
      mTracksCaptured(nullptr, "HTMLMediaElement::mTracksCaptured"),
      mErrorSink(new ErrorSink(this)),
      mAudioChannelWrapper(new AudioChannelAgentCallback(this)),
      mSink(std::pair(nsString(), RefPtr<AudioDeviceInfo>())),
      mShowPoster(IsVideo()),
      mMediaControlKeyListener(new MediaControlKeyListener(this)) {
  MOZ_ASSERT(GetMainThreadSerialEventTarget());
  // Please don't add anything to this constructor or the initialization
  // list that can cause AddRef to be called. This prevents subclasses
  // from overriding AddRef in a way that works with our refcount
  // logging mechanisms. Put these things inside of the ::Init method
  // instead.
}

void HTMLMediaElement::Init() {
  MOZ_ASSERT(mRefCnt == 0 && !mRefCnt.IsPurple(),
             "HTMLMediaElement::Init called when AddRef has been called "
             "at least once already, probably in the constructor. Please "
             "see the documentation in the HTMLMediaElement constructor.");
  MOZ_ASSERT(!mRefCnt.IsPurple());

  mAudioTrackList = new AudioTrackList(OwnerDoc()->GetParentObject(), this);
  mVideoTrackList = new VideoTrackList(OwnerDoc()->GetParentObject(), this);

  DecoderDoctorLogger::LogConstruction(this);

  mWatchManager.Watch(mPaused, &HTMLMediaElement::UpdateWakeLock);
  mWatchManager.Watch(mPaused, &HTMLMediaElement::UpdateOutputTracksMuting);
  mWatchManager.Watch(
      mPaused, &HTMLMediaElement::NotifyMediaControlPlaybackStateChanged);
  mWatchManager.Watch(mReadyState, &HTMLMediaElement::UpdateOutputTracksMuting);

  mWatchManager.Watch(mTracksCaptured,
                      &HTMLMediaElement::UpdateOutputTrackSources);
  mWatchManager.Watch(mReadyState, &HTMLMediaElement::UpdateOutputTrackSources);
  mWatchManager.Watch(mReadyState,
                      &HTMLMediaElement::UpdatePlaybackPseudoClasses);

  mWatchManager.Watch(mDownloadSuspendedByCache,
                      &HTMLMediaElement::UpdateReadyStateInternal);
  mWatchManager.Watch(mFirstFrameLoaded,
                      &HTMLMediaElement::UpdateReadyStateInternal);
  mWatchManager.Watch(mSrcStreamPlaybackEnded,
                      &HTMLMediaElement::UpdateReadyStateInternal);

  ErrorResult rv;

  double defaultVolume = Preferences::GetFloat("media.default_volume", 1.0);
  SetVolume(defaultVolume, rv);

  RegisterActivityObserver();
  NotifyOwnerDocumentActivityChanged();

  // We initialize the MediaShutdownManager as the HTMLMediaElement is always
  // constructed on the main thread, and not during stable state.
  // (MediaShutdownManager make use of nsIAsyncShutdownClient which is written
  // in JS)
  MediaShutdownManager::InitStatics();

#if defined(MOZ_WIDGET_ANDROID)
  GVAutoplayPermissionRequestor::AskForPermissionIfNeeded(
      OwnerDoc()->GetInnerWindow());
  StartObservingGVAutoplayIfNeeded();
#endif

  OwnerDoc()->SetDocTreeHadMedia();
  mShutdownObserver->Subscribe(this);
  UpdatePlaybackPseudoClasses();
  mInitialized = true;
}

HTMLMediaElement::~HTMLMediaElement() {
  MOZ_ASSERT(mInitialized,
             "HTMLMediaElement must be initialized before it is destroyed.");
  NS_ASSERTION(
      !mHasSelfReference,
      "How can we be destroyed if we're still holding a self reference?");

  PROFILER_MARKER("~HTMLMediaElement", MEDIA_PLAYBACK, {},
                  TerminatingFlowMarker, Flow::FromPointer(this));

  mWatchManager.Shutdown();

#if defined(MOZ_WIDGET_ANDROID)
  mGVAutoplayObserver = nullptr;
#endif

  mShutdownObserver->Unsubscribe();

  mTitleChangeObserver->Unsubscribe();

  if (mVideoFrameContainer) {
    mVideoFrameContainer->ForgetElement();
  }
  UnregisterActivityObserver();

  mSetCDMRequest.DisconnectIfExists();
  mAllowedToPlayPromise.RejectIfExists(NS_ERROR_FAILURE, __func__);

  if (mDecoder) {
    ShutdownDecoder();
  }
  if (mProgressTimer) {
    StopProgress();
  }
  if (mSrcStream) {
    EndSrcMediaStreamPlayback();
  }

  NS_ASSERTION(MediaElementTableCount(this, mLoadingSrc) == 0,
               "Destroyed media element should no longer be in element table");

  if (mChannelLoader) {
    mChannelLoader->Cancel();
  }

  if (mAudioChannelWrapper) {
    mAudioChannelWrapper->Shutdown();
    mAudioChannelWrapper = nullptr;
  }

  if (mResumeDelayedPlaybackAgent) {
    mResumePlaybackRequest.DisconnectIfExists();
    mResumeDelayedPlaybackAgent = nullptr;
  }

  mMediaControlKeyListener->StopIfNeeded();
  mMediaControlKeyListener = nullptr;

  WakeLockRelease();

  DecoderDoctorLogger::LogDestruction(this);
}

void HTMLMediaElement::StopSuspendingAfterFirstFrame() {
  mAllowSuspendAfterFirstFrame = false;
  if (!mSuspendedAfterFirstFrame) {
    return;
  }
  mSuspendedAfterFirstFrame = false;
  if (mDecoder) {
    mDecoder->Resume();
  }
}

void HTMLMediaElement::SetPlayedOrSeeked(bool aValue) {
  if (aValue == mHasPlayedOrSeeked) {
    return;
  }

  mHasPlayedOrSeeked = aValue;

  // Force a reflow so that the poster frame hides or shows immediately.
  nsIFrame* frame = GetPrimaryFrame();
  if (!frame) {
    return;
  }
  frame->PresShell()->FrameNeedsReflow(frame, IntrinsicDirty::FrameAndAncestors,
                                       NS_FRAME_IS_DIRTY);
}

void HTMLMediaElement::NotifyXPCOMShutdown() { ShutdownDecoder(); }

already_AddRefed<Promise> HTMLMediaElement::Play(ErrorResult& aRv) {
  LOG(LogLevel::Debug, ("{} Play() called by JS readyState={}", fmt::ptr(this),
                        mReadyState.Ref()));

  // 4.8.12.8
  // When the play() method on a media element is invoked, the user agent must
  // run the following steps.

  RefPtr<PlayPromise> promise = CreatePlayPromise(aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  // 4.8.12.8 - Step 1:
  // If the media element is not allowed to play, return a promise rejected
  // with a "NotAllowedError" DOMException and abort these steps.
  // NOTE: we may require requesting permission from the user, so we do the
  // "not allowed" check below.

  // 4.8.12.8 - Step 2:
  // If the media element's error attribute is not null and its code
  // attribute has the value MEDIA_ERR_SRC_NOT_SUPPORTED, return a promise
  // rejected with a "NotSupportedError" DOMException and abort these steps.
  if (GetError() && GetError()->Code() == MEDIA_ERR_SRC_NOT_SUPPORTED) {
    LOG(LogLevel::Debug,
        ("{} Play() promise rejected because source not supported.",
         fmt::ptr(this)));
    promise->MaybeReject(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR);
    return promise.forget();
  }

  // 4.8.12.8 - Step 3:
  // Let promise be a new promise and append promise to the list of pending
  // play promises.
  // Note: Promise appended to list of pending promises as needed below.

  // Per spec, play() invokes the media lazy load resumption steps but not the
  // poster lazy load resumption steps. Handle this before other checks so
  // loading begins even if play is later blocked or delayed.
  if (mLazyLoading) {
    StopLazyLoading(StartLoad::No);
    DoLoad();
    // Must be set after DoLoad(): DoLoad() calls AbortExistingLoads(), which
    // resets mLazyLoadingCompleted to false.
    mLazyLoadingCompleted = true;
  }

  if (ShouldBeSuspendedByInactiveDocShell()) {
    LOG(LogLevel::Debug,
        ("{} no allow to play by the docShell for now", fmt::ptr(this)));
    mPendingPlayPromises.AppendElement(promise);
    return promise.forget();
  }

  // We may delay starting playback of a media resource for an unvisited tab
  // until it's going to foreground or being resumed by the play tab icon.
  if (MediaPlaybackDelayPolicy::ShouldDelayPlayback(this)) {
    CreateResumeDelayedMediaPlaybackAgentIfNeeded();
    LOG(LogLevel::Debug, ("{} delay Play() call", fmt::ptr(this)));
    MaybeDoLoad();
    // When play is delayed, save a reference to the promise, and return it.
    // The promise will be resolved when we resume play by either the tab is
    // brought to the foreground, or the audio tab indicator is clicked.
    mPendingPlayPromises.AppendElement(promise);
    return promise.forget();
  }

  const bool handlingUserInput = UserActivation::IsHandlingUserInput();
  mPendingPlayPromises.AppendElement(promise);

  if (AllowedToPlay()) {
    AUTOPLAY_LOG("allow MediaElement {} to play", fmt::ptr(this));
    mAllowedToPlayPromise.ResolveIfExists(true, __func__);
    PlayInternal(handlingUserInput);
    UpdateCustomPolicyAfterPlayed();

    MaybeMarkSHEntryAsUserInteracted();
  } else {
#if defined(MOZ_WIDGET_ANDROID)
    if (ShouldDelayPlayUntilGVAutoplayRequestResolved()) {
      AUTOPLAY_LOG("delay MediaElement {} play while waiting for GeckoView",
                   fmt::ptr(this));
      StartObservingGVAutoplayIfNeeded();
      MaybeDoLoad();
      return promise.forget();
    }
#endif
    AUTOPLAY_LOG("reject MediaElement {} to play", fmt::ptr(this));
    AsyncRejectPendingPlayPromises(NS_ERROR_DOM_MEDIA_NOT_ALLOWED_ERR);
  }
  return promise.forget();
}

void HTMLMediaElement::DispatchEventsWhenPlayWasNotAllowed() {
  if (StaticPrefs::media_autoplay_block_event_enabled()) {
    QueueEvent(u"blocked"_ns);
  }
  DispatchBlockEventForVideoControl();
  if (!mHasEverBeenBlockedForAutoplay) {
    MaybeNotifyAutoplayBlocked();
    ReportToConsole(nsIScriptError::warningFlag, "BlockAutoplayError");
    mHasEverBeenBlockedForAutoplay = true;
  }
}

void HTMLMediaElement::MaybeNotifyAutoplayBlocked() {
  // This event is used to notify front-end side that we've blocked autoplay,
  // so front-end side should show blocking icon as well.
  RefPtr<AsyncEventDispatcher> asyncDispatcher =
      new AsyncEventDispatcher(OwnerDoc(), u"GloballyAutoplayBlocked"_ns,
                               CanBubble::eYes, ChromeOnlyDispatch::eYes);
  asyncDispatcher->PostDOMEvent();
}

void HTMLMediaElement::DispatchBlockEventForVideoControl() {
#if defined(MOZ_WIDGET_ANDROID)
  nsVideoFrame* videoFrame = do_QueryFrame(GetPrimaryFrame());
  if (!videoFrame || !videoFrame->GetVideoControls()) {
    return;
  }

  RefPtr<AsyncEventDispatcher> asyncDispatcher = new AsyncEventDispatcher(
      videoFrame->GetVideoControls(), u"MozNoControlsBlockedVideo"_ns,
      CanBubble::eYes);
  asyncDispatcher->PostDOMEvent();
#endif
}

void HTMLMediaElement::MaybeRecordRuntimeMutedContentAttrImpact() {
  if (mRecordedRuntimeContentAttrImpact || !mMutedByRuntimeContentAttr) {
    return;
  }
  // Only count a playback that would be audible if not for the runtime muted
  // content attribute: it must have audio and non-zero volume, be playing, and
  // not already be muted for another reason.
  if (mPaused || !HasAudio() || mVolume == 0.0 ||
      (mMuted & ~MUTED_BY_CONTENT)) {
    return;
  }
  glean::media::muted_by_content_attribute_runtime.Add(1);
  mRecordedRuntimeContentAttrImpact = true;
}

void HTMLMediaElement::PlayInternal(bool aHandlingUserInput) {
#if defined(MOZ_WIDGET_ANDROID)
  AUTOPLAY_LOG("Stop observing GV autoplay permission (PlayInternal starting)");
  StopObservingGVAutoplayIfNeeded();
#endif

  if (mPreloadAction == HTMLMediaElement::PRELOAD_NONE) {
    // The media load algorithm will be initiated by a user interaction.
    // We want to boost the channel priority for better responsiveness.
    // Note this must be done before UpdatePreloadAction() which will
    // update |mPreloadAction|.
    mUseUrgentStartForChannel = true;
  }

  StopSuspendingAfterFirstFrame();
  SetPlayedOrSeeked(true);

  // 4.8.12.8 - Step 4:
  // If the media element's networkState attribute has the value NETWORK_EMPTY,
  // invoke the media element's resource selection algorithm.
  MaybeDoLoad();
  if (mSuspendedForPreloadNone) {
    ResumeLoad(PRELOAD_ENOUGH, JSCallingLocation::Get());
  }

  // 4.8.12.8 - Step 5:
  // If the playback has ended and the direction of playback is forwards,
  // seek to the earliest possible position of the media resource.

  // Even if we just did Load() or ResumeLoad(), we could already have a decoder
  // here if we managed to clone an existing decoder.
  if (mDecoder) {
    if (mDecoder->IsEnded()) {
      SetCurrentTime(0);
    }
    if (!mSuspendedByInactiveDocOrDocshell) {
      mDecoder->Play();
    }
  }

  if (!mCurrentPlayRangeStart) {
    mCurrentPlayRangeStart = Some(CurrentTime());
  }

  const bool oldPaused = mPaused;
  mPaused = false;
  UpdatePlaybackPseudoClasses();
  // Step 5,
  // https://html.spec.whatwg.org/multipage/media.html#internal-play-steps
  mCanAutoplayFlag = false;

  // We changed mPaused and mCanAutoplayFlag which can affect
  // AddRemoveSelfReference and our preload status.
  AddRemoveSelfReference();
  UpdatePreloadAction(JSCallingLocation::Get());
  UpdateSrcMediaStreamPlaying();
  StartMediaControlKeyListenerIfNeeded();
  NotifyAudioPlaybackChanged(
      AudioChannelService::AudibleChangedReasons::ePauseStateChanged);

  // Once play() has been called in a user generated event handler,
  // it is allowed to autoplay. Note: we can reach here when not in
  // a user generated event handler if our readyState has not yet
  // reached HAVE_METADATA.
  mIsBlessed |= aHandlingUserInput;

  // TODO: If the playback has ended, then the user agent must set
  // seek to the effective start.

  // 4.8.12.8 - Step 6:
  // If the media element's paused attribute is true, run the following steps:
  if (oldPaused) {
    // 6.1. Change the value of paused to false. (Already done.)
    // This step is uplifted because the "block-media-playback" feature needs
    // the mPaused to be false before UpdateAudioChannelPlayingState() being
    // called.

    // 6.2. If the show poster flag is true, set the element's show poster flag
    //      to false and run the time marches on steps.
    if (mShowPoster) {
      mShowPoster = false;
      if (mTextTrackManager) {
        mTextTrackManager->TimeMarchesOn();
      }
    }

    // 6.3. Queue a task to fire a simple event named play at the element.
    QueueEvent(u"play"_ns);

    // 6.4. If the media element's readyState attribute has the value
    //      HAVE_NOTHING, HAVE_METADATA, or HAVE_CURRENT_DATA, queue a task to
    //      fire a simple event named waiting at the element.
    //      Otherwise, the media element's readyState attribute has the value
    //      HAVE_FUTURE_DATA or HAVE_ENOUGH_DATA: notify about playing for the
    //      element.
    switch (mReadyState) {
      case HAVE_NOTHING:
        QueueEvent(u"waiting"_ns);
        break;
      case HAVE_METADATA:
      case HAVE_CURRENT_DATA:
        QueueEvent(u"waiting"_ns);
        break;
      case HAVE_FUTURE_DATA:
      case HAVE_ENOUGH_DATA:
        NotifyAboutPlaying();
        break;
    }
  } else if (mReadyState >= HAVE_FUTURE_DATA) {
    // 7. Otherwise, if the media element's readyState attribute has the value
    //    HAVE_FUTURE_DATA or HAVE_ENOUGH_DATA, take pending play promises and
    //    queue a task to resolve pending play promises with the result.
    AsyncResolvePendingPlayPromises();
  }

  // 8. Set the media element's autoplaying flag to false. (Already done.)

  // 9. Return promise.
  // (Done in caller.)

  MaybeRecordRuntimeMutedContentAttrImpact();
}

void HTMLMediaElement::MaybeDoLoad() {
  if (mNetworkState == NETWORK_EMPTY) {
    DoLoad();
  }
}

void HTMLMediaElement::UpdateWakeLock() {
  MOZ_ASSERT(NS_IsMainThread());
  // Ensure we have a wake lock if we're playing audibly. This ensures the
  // device doesn't sleep while playing.
  bool playing = !mPaused;
  bool isAudible = Volume() > 0.0 && !mMuted && mIsAudioTrackAudible;
  // WakeLock when playing audible media.
  if (playing && isAudible) {
    CreateAudioWakeLockIfNeeded();
  } else {
    ReleaseAudioWakeLockIfExists();
  }
}

void HTMLMediaElement::UpdatePlaybackPseudoClasses() {
  MOZ_ASSERT(NS_IsMainThread());
  LOG(LogLevel::Debug,
      ("{} UpdatePlaybackPseudoClasses: mPaused={}, mNetworkState={}, "
       "mReadyState={}, mIsCurrentlyStalled={}",
       fmt::ptr(this), mPaused.Ref(), mNetworkState, mReadyState.Ref(),
       mIsCurrentlyStalled));
  AutoStateChangeNotifier notifier(*this, /*aNotify=*/true);
  RemoveStatesSilently(ElementState::PAUSED | ElementState::BUFFERING |
                       ElementState::STALLED);
  // We don’t need to update the playing state because these states are
  // exclusive, and the `:playing` pseudo-class is determined by checking
  // the element's PAUSED state.
  if (mPaused) {
    AddStatesSilently(ElementState::PAUSED);
    return;
  }
  // https://html.spec.whatwg.org/multipage/semantics-other.html#selector-buffering
  if (mNetworkState == NETWORK_LOADING && mReadyState <= HAVE_CURRENT_DATA) {
    AddStatesSilently(ElementState::BUFFERING);
    // https://html.spec.whatwg.org/multipage/semantics-other.html#selector-stalled
    if (mIsCurrentlyStalled) {
      AddStatesSilently(ElementState::STALLED);
    }
  }
}

void HTMLMediaElement::CreateAudioWakeLockIfNeeded() {
  MOZ_ASSERT(NS_IsMainThread());
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return;
  }
  if (mAudioWakelockReleaseScheduler) {
    LOG(LogLevel::Debug,
        ("{} Reuse existing audio wakelock, cancel scheduler", fmt::ptr(this)));
    mAudioWakelockReleaseScheduler->Reset();
    mAudioWakelockReleaseScheduler.reset();
    return;
  }
  if (!mWakeLock) {
    RefPtr<power::PowerManagerService> pmService =
        power::PowerManagerService::GetInstance();
    NS_ENSURE_TRUE_VOID(pmService);
    LOG(LogLevel::Debug, ("{} creating audio wakelock", fmt::ptr(this)));
    ErrorResult rv;
    mWakeLock = pmService->NewWakeLock(u"audio-playing"_ns,
                                       OwnerDoc()->GetInnerWindow(), rv);
  }
}

void HTMLMediaElement::ReleaseAudioWakeLockIfExists() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mWakeLock) {
    const uint32_t delayMs =
        StaticPrefs::media_wakelock_audio_delay_releasing_ms();
    // Already in shutdown or no delay, release wakelock directly.
    if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed) ||
        delayMs == 0) {
      ReleaseAudioWakeLockInternal();
      return;
    }
    if (mAudioWakelockReleaseScheduler) {
      return;
    }
    LOG(LogLevel::Debug, ("{} Delaying audio wakelock release by {} ms",
                          fmt::ptr(this), delayMs));
    AwakeTimeStamp target =
        AwakeTimeStamp ::Now() + AwakeTimeDuration::FromMilliseconds(delayMs);
    mAudioWakelockReleaseScheduler.emplace(
        DelayedScheduler<AwakeTimeStamp>{GetMainThreadSerialEventTarget()});
    mAudioWakelockReleaseScheduler->Ensure(
        target,
        [self = RefPtr<HTMLMediaElement>(this), this]() {
          mAudioWakelockReleaseScheduler->CompleteRequest();
          ReleaseAudioWakeLockInternal();
        },
        [self = RefPtr<HTMLMediaElement>(this), this]() {
          LOG(LogLevel::Debug,
              ("{} Fail to delay audio wakelock releasing?!", fmt::ptr(this)));
          mAudioWakelockReleaseScheduler->CompleteRequest();
          ReleaseAudioWakeLockInternal();
        });
  }
}

void HTMLMediaElement::ReleaseAudioWakeLockInternal() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mAudioWakelockReleaseScheduler) {
    mAudioWakelockReleaseScheduler->Reset();
    mAudioWakelockReleaseScheduler.reset();
  }
  LOG(LogLevel::Debug, ("{} release audio wakelock", fmt::ptr(this)));
  ErrorResult rv;
  mWakeLock->Unlock(rv);
  rv.SuppressException();
  mWakeLock = nullptr;
}

void HTMLMediaElement::WakeLockRelease() {
  if (mWakeLock) {
    ReleaseAudioWakeLockInternal();
  }
}

void HTMLMediaElement::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  if (!this->Controls() || !aVisitor.mEvent->mFlags.mIsTrusted) {
    nsGenericHTMLElement::GetEventTargetParent(aVisitor);
    return;
  }

  // We will need to trap pointer, touch, and mouse events within the media
  // element, allowing media control exclusive consumption on these events,
  // and preventing the content from handling them.
  switch (aVisitor.mEvent->mMessage) {
    case eTouchRawUpdate:
      MOZ_FALLTHROUGH_ASSERT(
          "eTouchRawUpdate event shouldn't be dispatched into the DOM");
    // Always prevent touchmove captured in video element from being handled by
    // content, since we always do that for touchstart.
    case eTouchMove:
    case eTouchEnd:
    case eTouchStart: {
      // We stop the propagation (both at the capture and the bubbling phase)
      // when the original target is the part of the ControlBar or the
      // ClickToPlay button.
      if (ShadowRoot* shadowRoot = GetShadowRoot()) {
        nsINode* node =
            nsINode::FromEventTargetOrNull(aVisitor.mEvent->mOriginalTarget);
        const bool trap = [&] {
          if (node->SubtreeRoot() != shadowRoot) {
            return false;
          }

          for (auto* node : node->InclusiveAncestorsOfType<Element>()) {
            auto* id = node->GetID();
            if (!id) {
              continue;
            }
            if (id == nsGkAtoms::clickToPlay || id == nsGkAtoms::controlBar) {
              return true;
            }
          }
          return false;
        }();

        if (trap) {
          aVisitor.mCanHandle = false;
        } else {
          nsGenericHTMLElement::GetEventTargetParent(aVisitor);
        }
        return;
      }
      nsGenericHTMLElement::GetEventTargetParent(aVisitor);
      return;
    }
    case ePointerDown:
    case ePointerUp:
    case ePointerClick:
    case eMouseDoubleClick:
    case eMouseDown:
    case eMouseUp:
      aVisitor.mCanHandle = false;
      return;

    // The *move events however are only consumed when the range input is being
    // dragged.
    case eMouseRawUpdate:
      MOZ_FALLTHROUGH_ASSERT(
          "eMouseRawUpdate event shouldn't be dispatched into the DOM");
    case ePointerMove:
    case ePointerRawUpdate:
    case eMouseMove: {
      nsINode* node =
          nsINode::FromEventTargetOrNull(aVisitor.mEvent->mOriginalTarget);
      if (MOZ_UNLIKELY(!node)) {
        return;
      }
      HTMLInputElement* el = nullptr;
      if (node->ChromeOnlyAccess()) {
        if (node->IsHTMLElement(nsGkAtoms::input)) {
          // The node is a <input type="range">
          el = static_cast<HTMLInputElement*>(node);
        } else if (node->GetParentNode() &&
                   node->GetParentNode()->IsHTMLElement(nsGkAtoms::input)) {
          // The node is a child of <input type="range">
          el = static_cast<HTMLInputElement*>(node->GetParentNode());
        }
      }
      if (el && el->IsDraggingRange()) {
        aVisitor.mCanHandle = false;
        return;
      }
      nsGenericHTMLElement::GetEventTargetParent(aVisitor);
      return;
    }
    default:
      nsGenericHTMLElement::GetEventTargetParent(aVisitor);
      return;
  }
}

bool HTMLMediaElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                      const nsAString& aValue,
                                      nsIPrincipal* aMaybeScriptedPrincipal,
                                      nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::crossorigin) {
      ParseCORSValue(aValue, aResult);
      return true;
    }
    if (aAttribute == nsGkAtoms::preload) {
      return aResult.ParseEnumValue(aValue, kPreloadTable, false,
                                    // The default value is "auto" if aValue is
                                    // not a recognised value.
                                    kPreloadDefaultType);
    }

    if (aAttribute == nsGkAtoms::loading) {
      return ParseLoadingAttribute(aValue, aResult);
    }
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aMaybeScriptedPrincipal, aResult);
}

void HTMLMediaElement::DoneCreatingElement() {
  // A parser-created element with the muted content attribute has the "default"
  // muted state, so the attribute makes it muted.
  if (HasAttr(nsGkAtoms::muted)) {
    mMuted |= MUTED_BY_CONTENT;
    SetStates(ElementState::MUTED, Muted());
    // The attribute is present at creation, so it is not a runtime addition.
    mMutedByRuntimeContentAttr = false;
  }
}

bool HTMLMediaElement::IsHTMLFocusable(IsFocusableFlags aFlags,
                                       bool* aIsFocusable, int32_t* aTabIndex) {
  if (nsGenericHTMLElement::IsHTMLFocusable(aFlags, aIsFocusable, aTabIndex)) {
    return true;
  }

  *aIsFocusable = true;
  return false;
}

int32_t HTMLMediaElement::TabIndexDefault() { return 0; }

void HTMLMediaElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                    const nsAttrValue* aValue,
                                    const nsAttrValue* aOldValue,
                                    nsIPrincipal* aMaybeScriptedPrincipal,
                                    bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::loading) {
      if (aValue && Loading(aValue->GetEnumValue()) == Loading::Lazy) {
        SetLazyLoading();
      } else if (aOldValue &&
                 Loading(aOldValue->GetEnumValue()) == Loading::Lazy) {
        StopLazyLoading(StartLoad(aNotify));
      }
    } else if (aName == nsGkAtoms::src) {
      mSrcMediaSource = nullptr;
      nsAttrValueOrString srcVal(aValue);
      mSrcAttrTriggeringPrincipal = nsContentUtils::GetAttrTriggeringPrincipal(
          this, srcVal.String(), aMaybeScriptedPrincipal);
      if (aValue) {
        nsCOMPtr<nsIURI> uri;
        NewURIFromString(srcVal.String(), getter_AddRefs(uri));
        if (uri && uri->SchemeIs(BLOBURI_SCHEME)) {
          if (DocGroup* docGroup = OwnerDoc()->GetDocGroup()) {
            mSrcMediaSource = docGroup->LookupMediaSourceURL(uri);
          }
        }
      }
    } else if (aName == nsGkAtoms::autoplay) {
      if (aNotify) {
        if (aValue) {
          if (!mLazyLoading) {
#if defined(MOZ_WIDGET_ANDROID)
            StartObservingGVAutoplayIfNeeded();
#endif
            StopSuspendingAfterFirstFrame();
            CheckAutoplayDataReady();
          }
        }
        // This attribute can affect AddRemoveSelfReference
        AddRemoveSelfReference();
        UpdatePreloadAction(JSCallingLocation::Get());
      }
    } else if (aName == nsGkAtoms::preload) {
      UpdatePreloadAction(JSCallingLocation::Get());
    } else if (aName == nsGkAtoms::loop) {
      if (mDecoder) {
        mDecoder->SetLooping(!!aValue);
      }
    } else if (aName == nsGkAtoms::controls && IsInComposedDoc()) {
      AddScriptRunnerToNotifyUAWidgetSetupOrChange();
      SetCuesDirty();
    } else if (aName == nsGkAtoms::muted) {
      // While the muted state is "default", the muted content attribute is a
      // fallback that determines whether the element is muted. Changing the
      // content attribute is not a volumechange trigger; only the muted and
      // volume IDL setters fire that event.
      if (mMutedState == MutedState::Default) {
        // Track whether the muted content attribute added at runtime is what
        // would mute this element; its impact is recorded at playback.
        mMutedByRuntimeContentAttr = !!aValue;
        if (StaticPrefs::dom_media_muted_state_enabled()) {
          SetMutedInternal(aValue ? (mMuted | MUTED_BY_CONTENT)
                                  : (mMuted & ~MUTED_BY_CONTENT));
          if (IsInComposedDoc()) {
            AddScriptRunnerToNotifyUAWidgetSetupOrChange();
          }
        }
      }
    }
  }

  // Since AfterMaybeChangeAttr may call DoLoad, make sure that it is called
  // *after* any possible changes to mSrcMediaSource.
  if (aValue) {
    AfterMaybeChangeAttr(aNameSpaceID, aName, aNotify);
  }

  return nsGenericHTMLElement::AfterSetAttr(
      aNameSpaceID, aName, aValue, aOldValue, aMaybeScriptedPrincipal, aNotify);
}

void HTMLMediaElement::OnAttrSetButNotChanged(int32_t aNamespaceID,
                                              nsAtom* aName,
                                              const nsAttrValueOrString& aValue,
                                              bool aNotify) {
  AfterMaybeChangeAttr(aNamespaceID, aName, aNotify);

  return nsGenericHTMLElement::OnAttrSetButNotChanged(aNamespaceID, aName,
                                                      aValue, aNotify);
}

void HTMLMediaElement::AfterMaybeChangeAttr(int32_t aNamespaceID, nsAtom* aName,
                                            bool aNotify) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::src) {
      mLazyLoadingCompleted = false;

      if (StaticPrefs::media_lazy_loading_enabled() &&
          LoadingState() == Loading::Lazy) {
        if (!mLazyLoading) {
          AbortExistingLoads();
        }
        SetLazyLoading();
      }
      DoLoad();
    }
  }
}

nsresult HTMLMediaElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  nsresult rv = nsGenericHTMLElement::BindToTree(aContext, aParent);

  if (IsInComposedDoc()) {
    // Construct Shadow Root so web content can be hidden in the DOM.
    AttachAndSetUAShadowRoot(NotifyUAWidget::Yes);

    // The preload action depends on the value of the autoplay attribute.
    // It's value may have changed, so update it.
    UpdatePreloadAction(JSCallingLocation::Get());
  }

  if (mLazyLoading) {
    LazyLoadingElementBindToTree(aContext);
  }

  NotifyDecoderActivityChanges();
  mMediaControlKeyListener->UpdateOwnerBrowsingContextIfNeeded();
  return rv;
}

void HTMLMediaElement::UnbindFromTree(UnbindContext& aContext) {
  mVisibilityState = Visibility::Untracked;

  if (IsInComposedDoc()) {
    TeardownUAShadowRoot(NotifyUAWidget::Yes);
  }

  if (mLazyLoading) {
    LazyLoadingElementUnbindFromTree(aContext);
  }

  nsGenericHTMLElement::UnbindFromTree(aContext);

  MOZ_ASSERT(IsActuallyInvisible());
  NotifyDecoderActivityChanges();

  // https://html.spec.whatwg.org/#playing-the-media-resource:remove-an-element-from-a-document
  //
  // Dispatch a task to run once we're in a stable state which ensures we're
  // paused if we're no longer in a document. Note that we need to dispatch this
  // even if there are other tasks in flight for this because these can be
  // cancelled if there's a new load.
  //
  // FIXME(emilio): Per that spec section, we should only do this if we used to
  // be connected, though other browsers match our current behavior...
  //
  // Also, https://github.com/whatwg/html/issues/4928
  nsCOMPtr<nsIRunnable> task =
      NS_NewRunnableFunction("dom::HTMLMediaElement::UnbindFromTree",
                             [self = RefPtr<HTMLMediaElement>(this)]() {
                               if (!self->IsInComposedDoc()) {
                                 self->PauseInternal();
                                 self->mMediaControlKeyListener->StopIfNeeded();
                               }
                             });
  RunInStableState(task);
}

/* static */
CanPlayStatus HTMLMediaElement::GetCanPlay(
    const nsAString& aType, DecoderDoctorDiagnostics* aDiagnostics) {
  Maybe<MediaContainerType> containerType = MakeMediaContainerType(aType);
  if (!containerType) {
    return CANPLAY_NO;
  }
  CanPlayStatus status =
      DecoderTraits::CanHandleContainerType(*containerType, aDiagnostics);
  if (status == CANPLAY_YES &&
      (*containerType).ExtendedType().Codecs().IsEmpty()) {
    // Per spec: 'Generally, a user agent should never return "probably" for a
    // type that allows the `codecs` parameter if that parameter is not
    // present.' As all our currently-supported types allow for `codecs`, we can
    // do this check here.
    // TODO: Instead, missing `codecs` should be checked in each decoder's
    // `IsSupportedType` call from `CanHandleCodecsType()`.
    // See bug 1399023.
    return CANPLAY_MAYBE;
  }
  return status;
}

void HTMLMediaElement::CanPlayType(const nsAString& aType, nsAString& aResult) {
  DecoderDoctorDiagnostics diagnostics;
  CanPlayStatus canPlay = GetCanPlay(aType, &diagnostics);
  diagnostics.StoreFormatDiagnostics(OwnerDoc(), aType, canPlay != CANPLAY_NO,
                                     __func__);
  switch (canPlay) {
    case CANPLAY_NO:
      aResult.Truncate();
      break;
    case CANPLAY_YES:
      aResult.AssignLiteral("probably");
      break;
    case CANPLAY_MAYBE:
      aResult.AssignLiteral("maybe");
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected case.");
      break;
  }

  LOG(LogLevel::Debug, ("{} CanPlayType({}) = \"{}\"", fmt::ptr(this),
                        NS_ConvertUTF16toUTF8(aType).get(),
                        NS_ConvertUTF16toUTF8(aResult).get()));
}

void HTMLMediaElement::AssertReadyStateIsNothing() {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  if (mReadyState != HAVE_NOTHING) {
    char buf[1024];
    SprintfLiteral(buf,
                   "readyState=%d networkState=%d mLoadWaitStatus=%d "
                   "mSourceLoadCandidate=%d "
                   "mIsLoadingFromSourceChildren=%d mPreloadAction=%d "
                   "mSuspendedForPreloadNone=%d error=%d",
                   int(mReadyState), int(mNetworkState), int(mLoadWaitStatus),
                   !!mSourceLoadCandidate, mIsLoadingFromSourceChildren,
                   int(mPreloadAction), mSuspendedForPreloadNone,
                   GetError() ? GetError()->Code() : 0);
    MOZ_CRASH_UNSAFE_PRINTF("ReadyState should be HAVE_NOTHING! %s", buf);
  }
#endif
}

nsresult HTMLMediaElement::InitializeDecoderAsClone(
    ChannelMediaDecoder* aOriginal) {
  NS_ASSERTION(mLoadingSrc, "mLoadingSrc must already be set");
  NS_ASSERTION(mDecoder == nullptr, "Shouldn't have a decoder");
  AssertReadyStateIsNothing();

  MediaDecoderInit decoderInit(
      this, this, mMuted ? 0.0 : mVolume, mPreservesPitch,
      ClampPlaybackRate(mPlaybackRate),
      mPreloadAction == HTMLMediaElement::PRELOAD_METADATA, mHasSuspendTaint,
      HasAttr(nsGkAtoms::loop), aOriginal->ContainerType());

  RefPtr<ChannelMediaDecoder> decoder = aOriginal->Clone(decoderInit);
  if (!decoder) {
    return NS_ERROR_FAILURE;
  }

  LOG(LogLevel::Debug, ("{} Cloned decoder {} from {}", fmt::ptr(this),
                        fmt::ptr(decoder.get()), fmt::ptr(aOriginal)));

  return FinishDecoderSetup(decoder);
}

template <typename DecoderType, typename... LoadArgs>
nsresult HTMLMediaElement::SetupDecoder(DecoderType* aDecoder,
                                        LoadArgs&&... aArgs) {
  LOG(LogLevel::Debug,
      ("{} Created decoder {} for type {}", fmt::ptr(this), fmt::ptr(aDecoder),
       aDecoder->ContainerType().OriginalString().get()));

  nsresult rv = aDecoder->Load(std::forward<LoadArgs>(aArgs)...);
  if (NS_FAILED(rv)) {
    aDecoder->Shutdown();
    LOG(LogLevel::Debug, ("{} Failed to load for decoder {}", fmt::ptr(this),
                          fmt::ptr(aDecoder)));
    return rv;
  }

  rv = FinishDecoderSetup(aDecoder);
  // Only ChannelMediaDecoder supports resource cloning.
  if (std::is_same_v<DecoderType, ChannelMediaDecoder> && NS_SUCCEEDED(rv)) {
    AddMediaElementToURITable();
    NS_ASSERTION(
        MediaElementTableCount(this, mLoadingSrc) == 1,
        "Media element should have single table entry if decode initialized");
  }

  return rv;
}

nsresult HTMLMediaElement::InitializeDecoderForChannel(
    nsIChannel* aChannel, nsIStreamListener** aListener) {
  NS_ASSERTION(mLoadingSrc, "mLoadingSrc must already be set");
  AssertReadyStateIsNothing();

  DecoderDoctorDiagnostics diagnostics;

  nsAutoCString mimeType;
  aChannel->GetContentType(mimeType);
  NS_ASSERTION(!mimeType.IsEmpty(), "We should have the Content-Type.");
  NS_ConvertUTF8toUTF16 mimeUTF16(mimeType);

  RefPtr<HTMLMediaElement> self = this;
  auto reportCanPlay = [&, self](bool aCanPlay) {
    diagnostics.StoreFormatDiagnostics(self->OwnerDoc(), mimeUTF16, aCanPlay,
                                       __func__);
    if (!aCanPlay) {
      nsAutoString src;
      self->GetCurrentSrc(src);
      AutoTArray<nsString, 2> params = {mimeUTF16, src};
      self->ReportLoadError("MediaLoadUnsupportedMimeType", params);
    }
  };

  auto onExit = MakeScopeExit([self] {
    if (self->mChannelLoader) {
      self->mChannelLoader->Done();
      self->mChannelLoader = nullptr;
    }
  });

  Maybe<MediaContainerType> containerType = MakeMediaContainerType(mimeType);
  if (!containerType) {
    reportCanPlay(false);
    return NS_ERROR_FAILURE;
  }

  MediaDecoderInit decoderInit(
      this, this, mMuted ? 0.0 : mVolume, mPreservesPitch,
      ClampPlaybackRate(mPlaybackRate),
      mPreloadAction == HTMLMediaElement::PRELOAD_METADATA, mHasSuspendTaint,
      HasAttr(nsGkAtoms::loop), *containerType);

#ifdef MOZ_ANDROID_HLS_SUPPORT
  if (HLSDecoder::IsSupportedType(*containerType)) {
    RefPtr<HLSDecoder> decoder = HLSDecoder::Create(decoderInit);
    if (!decoder) {
      reportCanPlay(false);
      return NS_ERROR_OUT_OF_MEMORY;
    }
    reportCanPlay(true);
    return SetupDecoder(decoder.get(), aChannel);
  }
#endif

  RefPtr<ChannelMediaDecoder> decoder =
      ChannelMediaDecoder::Create(decoderInit, &diagnostics);
  if (!decoder) {
    reportCanPlay(false);
    return NS_ERROR_FAILURE;
  }

  reportCanPlay(true);
  bool isPrivateBrowsing = NodePrincipal()->GetIsInPrivateBrowsing();
  return SetupDecoder(decoder.get(), aChannel, isPrivateBrowsing, aListener);
}

nsresult HTMLMediaElement::FinishDecoderSetup(MediaDecoder* aDecoder) {
  ChangeNetworkState(NETWORK_LOADING);

  // Set mDecoder now so if methods like GetCurrentSrc get called between
  // here and Load(), they work.
  SetDecoder(aDecoder);

  // Notify the decoder of the initial activity status.
  NotifyDecoderActivityChanges();

  // Update decoder principal before we start decoding, since it
  // can affect how we feed data to MediaStreams
  NotifyDecoderPrincipalChanged();

  // Set sink device if we have one. Otherwise the default is used.
  if (mSink.second) {
    mDecoder->SetSink(mSink.second);
  }

  if (mMediaKeys) {
    if (mMediaKeys->GetCDMProxy()) {
      mDecoder->SetCDMProxy(mMediaKeys->GetCDMProxy());
    } else {
      // CDM must have crashed.
      ShutdownDecoder();
      return NS_ERROR_FAILURE;
    }
  }

  if (mChannelLoader) {
    mChannelLoader->Done();
    mChannelLoader = nullptr;
  }

  // We may want to suspend the new stream now.
  // This will also do an AddRemoveSelfReference.
  NotifyOwnerDocumentActivityChanged();

  if (!mDecoder) {
    // NotifyOwnerDocumentActivityChanged may shutdown the decoder if the
    // owning document is inactive and we're in the EME case. We could try and
    // handle this, but at the time of writing it's a pretty niche case, so just
    // bail.
    return NS_ERROR_FAILURE;
  }

  if (mSuspendedByInactiveDocOrDocshell) {
    mDecoder->Suspend();
  }

  if (!mPaused) {
    SetPlayedOrSeeked(true);
    if (!mSuspendedByInactiveDocOrDocshell) {
      mDecoder->Play();
    }
  }

  MaybeBeginCloningVisually();

  return NS_OK;
}

void HTMLMediaElement::UpdateSrcMediaStreamPlaying(uint32_t aFlags) {
  if (!mSrcStream) {
    return;
  }

  bool shouldPlay = !(aFlags & REMOVING_SRC_STREAM) && !mPaused &&
                    !mSuspendedByInactiveDocOrDocshell;
  if (shouldPlay == mSrcStreamIsPlaying) {
    return;
  }
  mSrcStreamIsPlaying = shouldPlay;

  LOG(LogLevel::Debug,
      ("MediaElement {} {} playback of DOMMediaStream {}", fmt::ptr(this),
       shouldPlay ? "Setting up" : "Removing", fmt::ptr(mSrcStream.get())));

  if (shouldPlay) {
    mSrcStreamPlaybackEnded = false;
    mSrcStreamReportPlaybackEnded = false;

    if (mMediaStreamRenderer) {
      mMediaStreamRenderer->Start();
    }
    if (mSecondaryMediaStreamRenderer) {
      mSecondaryMediaStreamRenderer->Start();
    }

    SetCapturedOutputStreamsEnabled(true);  // Unmute
    // If the input is a media stream, we don't check its data and always regard
    // it as audible when it's playing.
    SetAudibleState(true);
  } else {
    if (mMediaStreamRenderer) {
      mMediaStreamRenderer->Stop();
    }
    if (mSecondaryMediaStreamRenderer) {
      mSecondaryMediaStreamRenderer->Stop();
    }
    SetCapturedOutputStreamsEnabled(false);  // Mute
  }
}

void HTMLMediaElement::UpdateSrcStreamPotentiallyPlaying() {
  if (!mMediaStreamRenderer) {
    // Notifications are async, the renderer could have been cleared.
    return;
  }

  mMediaStreamRenderer->SetProgressingCurrentTime(IsPotentiallyPlaying());
}

void HTMLMediaElement::UpdateSrcStreamTime() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mSrcStreamPlaybackEnded) {
    // We do a separate FireTimeUpdate() when this is set.
    return;
  }

  FireTimeUpdate(TimeupdateType::ePeriodic);
}

void HTMLMediaElement::SetupSrcMediaStreamPlayback(DOMMediaStream* aStream) {
  NS_ASSERTION(!mSrcStream, "Should have been ended already");
  MOZ_ASSERT(mCORSMode == CORS_NONE);

  mLoadingSrc = nullptr;
  mSrcStream = aStream;

  VideoFrameContainer* container = GetVideoFrameContainer();
  RefPtr<FirstFrameVideoOutput> firstFrameOutput =
      container ? MakeAndAddRef<FirstFrameVideoOutput>(container,
                                                       AbstractMainThread())
                : nullptr;
  mMediaStreamRenderer = MakeAndAddRef<MediaStreamRenderer>(
      AbstractMainThread(), container, firstFrameOutput, this);
  mWatchManager.Watch(mPaused,
                      &HTMLMediaElement::UpdateSrcStreamPotentiallyPlaying);
  mWatchManager.Watch(mReadyState,
                      &HTMLMediaElement::UpdateSrcStreamPotentiallyPlaying);
  mWatchManager.Watch(mSrcStreamPlaybackEnded,
                      &HTMLMediaElement::UpdateSrcStreamPotentiallyPlaying);
  mWatchManager.Watch(mSrcStreamPlaybackEnded,
                      &HTMLMediaElement::UpdateSrcStreamReportPlaybackEnded);
  mWatchManager.Watch(mMediaStreamRenderer->CurrentTrackTime(),
                      &HTMLMediaElement::UpdateSrcStreamTime);
  SetVolumeInternal();
  if (mSink.second) {
    mMediaStreamRenderer->SetAudioOutputDevice(mSink.second);
  }

  UpdateSrcMediaStreamPlaying();
  UpdateSrcStreamPotentiallyPlaying();
  mSrcStreamVideoPrincipal = NodePrincipal();

  // If we pause this media element, track changes in the underlying stream
  // will continue to fire events at this element and alter its track list.
  // That's simpler than delaying the events, but probably confusing...
  nsTArray<RefPtr<MediaStreamTrack>> tracks;
  mSrcStream->GetTracks(tracks);
  for (const RefPtr<MediaStreamTrack>& track : tracks) {
    NotifyMediaStreamTrackAdded(track);
  }

  mMediaStreamTrackListener = new MediaStreamTrackListener(this);
  mSrcStream->RegisterTrackListener(mMediaStreamTrackListener.get());

  ChangeNetworkState(NETWORK_IDLE);
  ChangeDelayLoadStatus(false);

  // FirstFrameLoaded() will be called when the stream has tracks.
}

void HTMLMediaElement::EndSrcMediaStreamPlayback() {
  MOZ_ASSERT(mSrcStream);

  UpdateSrcMediaStreamPlaying(REMOVING_SRC_STREAM);

  if (mSelectedVideoStreamTrack) {
    mSelectedVideoStreamTrack->RemovePrincipalChangeObserver(this);
  }
  mSelectedVideoStreamTrack = nullptr;

  MOZ_ASSERT_IF(mSecondaryMediaStreamRenderer,
                !mMediaStreamRenderer == !mSecondaryMediaStreamRenderer);
  if (mMediaStreamRenderer) {
    mWatchManager.Unwatch(mPaused,
                          &HTMLMediaElement::UpdateSrcStreamPotentiallyPlaying);
    mWatchManager.Unwatch(mReadyState,
                          &HTMLMediaElement::UpdateSrcStreamPotentiallyPlaying);
    mWatchManager.Unwatch(mSrcStreamPlaybackEnded,
                          &HTMLMediaElement::UpdateSrcStreamPotentiallyPlaying);
    mWatchManager.Unwatch(
        mSrcStreamPlaybackEnded,
        &HTMLMediaElement::UpdateSrcStreamReportPlaybackEnded);
    mWatchManager.Unwatch(mMediaStreamRenderer->CurrentTrackTime(),
                          &HTMLMediaElement::UpdateSrcStreamTime);
    mMediaStreamRenderer->Shutdown();
    mMediaStreamRenderer = nullptr;
  }
  if (mSecondaryMediaStreamRenderer) {
    mSecondaryMediaStreamRenderer->Shutdown();
    mSecondaryMediaStreamRenderer = nullptr;
  }

  mSrcStream->UnregisterTrackListener(mMediaStreamTrackListener.get());
  mMediaStreamTrackListener = nullptr;
  mSrcStreamPlaybackEnded = false;
  mSrcStreamReportPlaybackEnded = false;
  mSrcStreamVideoPrincipal = nullptr;

  mSrcStream = nullptr;
}

static already_AddRefed<AudioTrack> CreateAudioTrack(
    AudioStreamTrack* aStreamTrack, nsIGlobalObject* aRelevantGlobal) {
  nsAutoString id;
  nsAutoString label;
  aStreamTrack->GetId(id);
  aStreamTrack->GetLabel(label, CallerType::System);

  return MediaTrackList::CreateAudioTrack(aRelevantGlobal, id, u"main"_ns,
                                          label, u""_ns, true, aStreamTrack);
}

static already_AddRefed<VideoTrack> CreateVideoTrack(
    VideoStreamTrack* aStreamTrack, nsIGlobalObject* aRelevantGlobal) {
  nsAutoString id;
  nsAutoString label;
  aStreamTrack->GetId(id);
  aStreamTrack->GetLabel(label, CallerType::System);

  return MediaTrackList::CreateVideoTrack(aRelevantGlobal, id, u"main"_ns,
                                          label, u""_ns, aStreamTrack);
}

void HTMLMediaElement::NotifyMediaStreamTrackAdded(
    const RefPtr<MediaStreamTrack>& aTrack) {
  MOZ_ASSERT(aTrack);

  if (aTrack->Ended()) {
    return;
  }

#ifdef DEBUG
  nsAutoString id;
  aTrack->GetId(id);

  LOG(LogLevel::Debug, ("{}, Adding {}Track with id {}", fmt::ptr(this),
                        aTrack->AsAudioStreamTrack() ? "Audio" : "Video",
                        NS_ConvertUTF16toUTF8(id).get()));
#endif

  if (AudioStreamTrack* t = aTrack->AsAudioStreamTrack()) {
    MOZ_DIAGNOSTIC_ASSERT(AudioTracks(), "Element can't have been unlinked");
    RefPtr<AudioTrack> audioTrack =
        CreateAudioTrack(t, AudioTracks()->GetRelevantGlobal());
    AudioTracks()->AddTrack(audioTrack);
  } else if (VideoStreamTrack* t = aTrack->AsVideoStreamTrack()) {
    // TODO: Fix this per the spec on bug 1273443.
    if (!IsVideo()) {
      return;
    }
    MOZ_DIAGNOSTIC_ASSERT(VideoTracks(), "Element can't have been unlinked");
    RefPtr<VideoTrack> videoTrack =
        CreateVideoTrack(t, VideoTracks()->GetRelevantGlobal());
    VideoTracks()->AddTrack(videoTrack);
    // New MediaStreamTrack added, set the new added video track as selected
    // video track when there is no selected track.
    if (VideoTracks()->SelectedIndex() == -1) {
      MOZ_ASSERT(!mSelectedVideoStreamTrack);
      videoTrack->SetEnabledInternal(true, dom::MediaTrack::FIRE_NO_EVENTS);
    }
  }

  // The set of enabled AudioTracks and selected video track might have changed.
  mWatchManager.ManualNotify(&HTMLMediaElement::UpdateReadyStateInternal);
  AbstractThread::DispatchDirectTask(
      NewRunnableMethod("HTMLMediaElement::FirstFrameLoaded", this,
                        &HTMLMediaElement::FirstFrameLoaded));
}

void HTMLMediaElement::NotifyMediaStreamTrackRemoved(
    const RefPtr<MediaStreamTrack>& aTrack) {
  MOZ_ASSERT(aTrack);

  nsAutoString id;
  aTrack->GetId(id);

  LOG(LogLevel::Debug, ("{}, Removing {}Track with id {}", fmt::ptr(this),
                        aTrack->AsAudioStreamTrack() ? "Audio" : "Video",
                        NS_ConvertUTF16toUTF8(id).get()));

  MOZ_DIAGNOSTIC_ASSERT(AudioTracks() && VideoTracks(),
                        "Element can't have been unlinked");
  if (dom::MediaTrack* t = AudioTracks()->GetTrackById(id)) {
    AudioTracks()->RemoveTrack(t);
  } else if (dom::MediaTrack* t = VideoTracks()->GetTrackById(id)) {
    VideoTracks()->RemoveTrack(t);
  } else {
    NS_ASSERTION(aTrack->AsVideoStreamTrack() && !IsVideo(),
                 "MediaStreamTrack ended but did not exist in track lists. "
                 "This is only allowed if a video element ends and we are an "
                 "audio element.");
    return;
  }
}

void HTMLMediaElement::ProcessMediaFragmentURI() {
  if (!mLoadingSrc) {
    mFragmentStart = mFragmentEnd = -1.0;
    return;
  }
  MediaFragmentURIParser parser(mLoadingSrc);

  if (mDecoder && parser.HasEndTime()) {
    mFragmentEnd = parser.GetEndTime();
  }

  if (parser.HasStartTime()) {
    SetCurrentTime(parser.GetStartTime());
    mFragmentStart = parser.GetStartTime();
  }
}

void HTMLMediaElement::MetadataLoaded(const MediaInfo* aInfo,
                                      UniquePtr<const MetadataTags> aTags) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mDecoder) {
    ConstructMediaTracks(aInfo);
  }

  SetMediaInfo(*aInfo);

  mIsEncrypted =
      aInfo->IsEncrypted() || mPendingEncryptedInitData.IsEncrypted();
  mTags = std::move(aTags);
  mLoadedDataFired = false;
  ChangeReadyState(HAVE_METADATA);

  // Add output tracks synchronously now to be sure they're available in
  // "loadedmetadata" event handlers.
  UpdateOutputTrackSources();

  QueueEvent(u"durationchange"_ns);
  if (IsVideo() && HasVideo()) {
    QueueEvent(u"resize"_ns);
    Invalidate(ImageSizeChanged::No, Some(mMediaInfo.mVideo.mDisplay),
               Nothing(), ForceInvalidate::No);
  }
  NS_ASSERTION(!HasVideo() || (mMediaInfo.mVideo.mDisplay.width > 0 &&
                               mMediaInfo.mVideo.mDisplay.height > 0),
               "Video resolution must be known on 'loadedmetadata'");
  QueueEvent(u"loadedmetadata"_ns);

  if (mDecoder && mDecoder->IsTransportSeekable() &&
      mDecoder->IsMediaSeekable()) {
    ProcessMediaFragmentURI();
    mDecoder->SetFragmentEndTime(mFragmentEnd);
  }
  if (mIsEncrypted) {
    // We only support playback of encrypted content via MSE by default.
    if (!mMediaSource && Preferences::GetBool("media.eme.mse-only", true)) {
      DecodeError(
          MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                      "Encrypted content not supported outside of MSE"));
      return;
    }

    // Dispatch a distinct 'encrypted' event for each initData we have.
    for (const auto& initData : mPendingEncryptedInitData.mInitDatas) {
      DispatchEncrypted(initData.mInitData, initData.mType);
    }
    mPendingEncryptedInitData.Reset();
  }

  if (IsVideo() && aInfo->HasVideo()) {
    // We are a video element playing video so update the screen wakelock
    NotifyOwnerDocumentActivityChanged();
  }

  if (mDefaultPlaybackStartPosition != 0.0) {
    SetCurrentTime(mDefaultPlaybackStartPosition);
    mDefaultPlaybackStartPosition = 0.0;
  }

  mWatchManager.ManualNotify(&HTMLMediaElement::UpdateReadyStateInternal);
}

void HTMLMediaElement::FirstFrameLoaded() {
  LOG(LogLevel::Debug,
      ("{}, FirstFrameLoaded() mFirstFrameLoaded={} mWaitingForKey={}",
       fmt::ptr(this), mFirstFrameLoaded.Ref(),
       static_cast<int>(mWaitingForKey)));

  NS_ASSERTION(!mSuspendedAfterFirstFrame, "Should not have already suspended");

  if (!mFirstFrameLoaded) {
    mFirstFrameLoaded = true;
  }

  ChangeDelayLoadStatus(false);

  if (ShouldSuspendDownloadAfterFirstFrameLoaded()) {
    LOG(LogLevel::Debug,
        ("{} Suspend decoder after first frame loaded", fmt::ptr(this)));
    mSuspendedAfterFirstFrame = true;
    mDecoder->Suspend();
  }
}

bool HTMLMediaElement::ShouldSuspendDownloadAfterFirstFrameLoaded() const {
  if (!mDecoder) {
    return false;
  }

  // If the media is set to autoplay, avoid suspending, to preload as much as
  // possible.
  if (HasAttr(nsGkAtoms::autoplay)) {
    return false;
  }

  // If the media is currently playing, do not suspend downloading.
  if (!mPaused) {
    return false;
  }

  return mPreloadAction == HTMLMediaElement::PRELOAD_METADATA &&
         mAllowSuspendAfterFirstFrame;
}

void HTMLMediaElement::NetworkError(const MediaResult& aError) {
  if (mReadyState == HAVE_NOTHING) {
    NoSupportedMediaSourceError(aError.Description());
  } else {
    Error(MEDIA_ERR_NETWORK);
  }
}

void HTMLMediaElement::DecodeError(const MediaResult& aError) {
  nsAutoString src;
  GetCurrentSrc(src);
  AutoTArray<nsString, 1> params = {src};
  ReportLoadError("MediaLoadDecodeError", params);

  DecoderDoctorDiagnostics diagnostics;
  diagnostics.StoreDecodeError(OwnerDoc(), aError, src, __func__);

  if (mIsLoadingFromSourceChildren) {
    mErrorSink->ResetError();
    if (mSourceLoadCandidate) {
      DispatchAsyncSourceError(mSourceLoadCandidate, aError.Message());
      QueueLoadFromSourceTask();
    } else {
      NS_WARNING("Should know the source we were loading from!");
    }
  } else if (mReadyState == HAVE_NOTHING) {
    NoSupportedMediaSourceError(aError.Description());
  } else if (IsCORSSameOrigin()) {
    Error(MEDIA_ERR_DECODE, Some(aError));
  } else {
    Error(MEDIA_ERR_DECODE, Some(MediaResult{NS_ERROR_DOM_MEDIA_DECODE_ERR,
                                             "Failed to decode media"}));
  }
}

void HTMLMediaElement::DecodeWarning(const MediaResult& aError) {
  nsAutoString src;
  GetCurrentSrc(src);
  DecoderDoctorDiagnostics diagnostics;
  diagnostics.StoreDecodeWarning(OwnerDoc(), aError, src, __func__);
}

bool HTMLMediaElement::HasError() const { return GetError(); }

void HTMLMediaElement::LoadAborted() { Error(MEDIA_ERR_ABORTED); }

void HTMLMediaElement::Error(uint16_t aErrorCode,
                             const Maybe<MediaResult>& aResult) {
  mErrorSink->SetError(aErrorCode, aResult);
  ChangeDelayLoadStatus(false);
  UpdateAudioChannelPlayingState();
}

void HTMLMediaElement::PlaybackEnded() {
  RefPtr<HTMLMediaElement> kungFuDeathGrip(this);
  // We changed state which can affect AddRemoveSelfReference
  AddRemoveSelfReference();

  NS_ASSERTION(!mDecoder || mDecoder->IsEnded(),
               "Decoder fired ended, but not in ended state");

  // IsPlaybackEnded() became true.
  mWatchManager.ManualNotify(&HTMLMediaElement::UpdateOutputTrackSources);

  if (mSrcStream) {
    LOG(LogLevel::Debug,
        ("{}, got duration by reaching the end of the resource",
         fmt::ptr(this)));
    mSrcStreamPlaybackEnded = true;
    QueueEvent(u"durationchange"_ns);
  } else {
    // mediacapture-main:
    // Setting the loop attribute has no effect since a MediaStream has no
    // defined end and therefore cannot be looped.
    if (HasAttr(nsGkAtoms::loop)) {
      SetCurrentTime(0);
      return;
    }
  }

  FireTimeUpdate(TimeupdateType::eMandatory);

  if (!mPaused) {
    Pause();
  }

  if (mSrcStream) {
    // A MediaStream that goes from inactive to active shall be eligible for
    // autoplay again according to the mediacapture-main spec.
    mCanAutoplayFlag = true;
  }

  if (StaticPrefs::media_mediacontrol_stopcontrol_aftermediaends()) {
    mMediaControlKeyListener->StopIfNeeded();
  }
  QueueEvent(u"ended"_ns);
}

void HTMLMediaElement::UpdateSrcStreamReportPlaybackEnded() {
  mSrcStreamReportPlaybackEnded = mSrcStreamPlaybackEnded;
}

void HTMLMediaElement::SeekStarted() { QueueEvent(u"seeking"_ns); }

void HTMLMediaElement::UpdatePlayedRangesBeforeSeek(double aRangeEndTime) {
  if (mPlayed && mCurrentPlayRangeStart) {
    LOG(LogLevel::Debug,
        ("{} Adding 'played' a range : [{}, {}]", fmt::ptr(this),
         mCurrentPlayRangeStart.value(), aRangeEndTime));
    // Multiple seek without playing, or seek while playing.
    if (mCurrentPlayRangeStart.value() != aRangeEndTime) {
      // Don't round the left of the interval: it comes from script and needs
      // to be exact.
      mPlayed->Add(mCurrentPlayRangeStart.value(), aRangeEndTime);
    }
    // Reset the current played range start time. We'll re-set it once
    // the seek completes.
    mCurrentPlayRangeStart = Nothing();
  }
}

void HTMLMediaElement::SeekCompleted() {
  RefPtr<HTMLMediaElement> kungFuDeathGrip(this);
  mPlayingBeforeSeek = false;
  SetPlayedOrSeeked(true);
  if (mTextTrackManager) {
    mTextTrackManager->DidSeek();
  }
  // https://html.spec.whatwg.org/multipage/media.html#seeking:dom-media-seek
  // (Step 16)
  // TODO (bug 1688131): run these steps in a stable state.
  FireTimeUpdate(TimeupdateType::eMandatory);
  RemoveStates(ElementState::SEEKING);
  QueueEvent(u"seeked"_ns);
  // We changed whether we're seeking so we need to AddRemoveSelfReference
  AddRemoveSelfReference();
  if (!mCurrentPlayRangeStart) {
    mCurrentPlayRangeStart = Some(CurrentTime());
  }

  if (mSeekDOMPromise) {
    AbstractMainThread()->Dispatch(NS_NewRunnableFunction(
        __func__, [promise = std::move(mSeekDOMPromise)] {
          promise->MaybeResolveWithUndefined();
        }));
  }
  MOZ_ASSERT(!mSeekDOMPromise);
  SetCuesDirty();
}

void HTMLMediaElement::SeekAborted() {
  RemoveStates(ElementState::SEEKING);
  if (mSeekDOMPromise) {
    AbstractMainThread()->Dispatch(NS_NewRunnableFunction(
        __func__, [promise = std::move(mSeekDOMPromise)] {
          promise->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
        }));
  }
  MOZ_ASSERT(!mSeekDOMPromise);
}

void HTMLMediaElement::NotifySuspendedByCache(bool aSuspendedByCache) {
  LOG(LogLevel::Debug,
      ("{}, mDownloadSuspendedByCache={}", fmt::ptr(this), aSuspendedByCache));
  mDownloadSuspendedByCache = aSuspendedByCache;
}

void HTMLMediaElement::DownloadSuspended() {
  if (mNetworkState == NETWORK_LOADING) {
    mIsCurrentlyStalled = false;
    UpdatePlaybackPseudoClasses();
    QueueEvent(u"progress"_ns);
  }
  ChangeNetworkState(NETWORK_IDLE);
}

void HTMLMediaElement::DownloadResumed() {
  ChangeNetworkState(NETWORK_LOADING);
}

void HTMLMediaElement::CheckProgress(bool aHaveNewProgress) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mNetworkState == NETWORK_LOADING);

  TimeStamp now = TimeStamp::Now();

  if (aHaveNewProgress) {
    mDataTime = now;
  }

  // If this is the first progress, or PROGRESS_MS has passed since the last
  // progress event fired and more data has arrived since then, fire a
  // progress event.
  NS_ASSERTION(
      (mProgressTime.IsNull() && !aHaveNewProgress) || !mDataTime.IsNull(),
      "null TimeStamp mDataTime should not be used in comparison");
  if (mProgressTime.IsNull()
          ? aHaveNewProgress
          : (now - mProgressTime >=
                 TimeDuration::FromMilliseconds(PROGRESS_MS) &&
             mDataTime > mProgressTime)) {
    mIsCurrentlyStalled = false;
    UpdatePlaybackPseudoClasses();
    QueueEvent(u"progress"_ns);
    // Going back 1ms ensures that future data will have now > mProgressTime,
    // and so will trigger another event.  mDataTime is not reset because it
    // is still required to detect stalled; it is similarly offset by
    // 1ms to indicate the new data has not yet arrived.
    mProgressTime = now - TimeDuration::FromMilliseconds(1);
    if (mDataTime > mProgressTime) {
      mDataTime = mProgressTime;
    }
    if (!mProgressTimer) {
      NS_ASSERTION(aHaveNewProgress,
                   "timer dispatched when there was no timer");
      // Were stalled.  Restart timer.
      StartProgressTimer();
      if (!mLoadedDataFired) {
        ChangeDelayLoadStatus(true);
      }
    }
    // Download statistics may have been updated, force a recheck of the
    // readyState.
    mWatchManager.ManualNotify(&HTMLMediaElement::UpdateReadyStateInternal);
  }

  if (now - mDataTime >= TimeDuration::FromMilliseconds(STALL_MS)) {
    if (!mMediaSource) {
      mIsCurrentlyStalled = true;
      UpdatePlaybackPseudoClasses();
      QueueEvent(u"stalled"_ns);
    } else {
      ChangeDelayLoadStatus(false);
    }

    NS_ASSERTION(mProgressTimer, "detected stalled without timer");
    // Stop timer events, which prevents repeated stalled events until there
    // is more progress.
    StopProgress();
  }

  AddRemoveSelfReference();
}

/* static */
void HTMLMediaElement::ProgressTimerCallback(nsITimer* aTimer, void* aClosure) {
  auto* decoder = static_cast<HTMLMediaElement*>(aClosure);
  decoder->CheckProgress(false);
}

void HTMLMediaElement::StartProgressTimer() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mNetworkState == NETWORK_LOADING);
  NS_ASSERTION(!mProgressTimer, "Already started progress timer.");

  NS_NewTimerWithFuncCallback(getter_AddRefs(mProgressTimer),
                              ProgressTimerCallback, this, PROGRESS_MS,
                              nsITimer::TYPE_REPEATING_SLACK,
                              "HTMLMediaElement::ProgressTimerCallback"_ns,
                              GetMainThreadSerialEventTarget());
}

void HTMLMediaElement::StartProgress() {
  // Record the time now for detecting stalled.
  mDataTime = TimeStamp::Now();
  // Reset mProgressTime so that mDataTime is not indicating bytes received
  // after the last progress event.
  mProgressTime = TimeStamp();
  StartProgressTimer();
}

void HTMLMediaElement::StopProgress() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mProgressTimer) {
    return;
  }

  mProgressTimer->Cancel();
  mProgressTimer = nullptr;
}

void HTMLMediaElement::DownloadProgressed() {
  if (mNetworkState != NETWORK_LOADING) {
    return;
  }
  CheckProgress(true);
}

bool HTMLMediaElement::ShouldCheckAllowOrigin() {
  return mCORSMode != CORS_NONE;
}

bool HTMLMediaElement::IsCORSSameOrigin() {
  bool subsumes;
  RefPtr<nsIPrincipal> principal = GetCurrentPrincipal();
  return (NS_SUCCEEDED(NodePrincipal()->Subsumes(principal, &subsumes)) &&
          subsumes) ||
         ShouldCheckAllowOrigin();
}

void HTMLMediaElement::UpdateReadyStateInternal() {
  if (!mDecoder && !mSrcStream) {
    // Not initialized - bail out.
    LOG(LogLevel::Debug, ("MediaElement {} UpdateReadyStateInternal() "
                          "Not initialized",
                          fmt::ptr(this)));
    return;
  }

  if (mDecoder && mReadyState < HAVE_METADATA) {
    // aNextFrame might have a next frame because the decoder can advance
    // on its own thread before MetadataLoaded gets a chance to run.
    // The arrival of more data can't change us out of this readyState.
    LOG(LogLevel::Debug, ("MediaElement {} UpdateReadyStateInternal() "
                          "Decoder ready state < HAVE_METADATA",
                          fmt::ptr(this)));
    return;
  }

  if (mDecoder) {
    // IsPlaybackEnded() might have become false.
    mWatchManager.ManualNotify(&HTMLMediaElement::UpdateOutputTrackSources);
  }

  if (mSrcStream && mReadyState < HAVE_METADATA) {
    bool hasAudioTracks = AudioTracks() && !AudioTracks()->IsEmpty();
    bool hasVideoTracks = VideoTracks() && !VideoTracks()->IsEmpty();
    if (!hasAudioTracks && !hasVideoTracks) {
      LOG(LogLevel::Debug, ("MediaElement {} UpdateReadyStateInternal() "
                            "Stream with no tracks",
                            fmt::ptr(this)));
      // Give it one last chance to remove the self reference if needed.
      AddRemoveSelfReference();
      return;
    }

    if (IsVideo() && hasVideoTracks && !HasVideo()) {
      LOG(LogLevel::Debug, ("MediaElement {} UpdateReadyStateInternal() "
                            "Stream waiting for video",
                            fmt::ptr(this)));
      return;
    }

    LOG(LogLevel::Debug,
        ("MediaElement {} UpdateReadyStateInternal() Stream has "
         "metadata; audioTracks={}, videoTracks={}, "
         "hasVideoFrame={}",
         fmt::ptr(this), AudioTracks()->Length(), VideoTracks()->Length(),
         HasVideo()));

    // We are playing a stream that has video and a video frame is now set.
    // This means we have all metadata needed to change ready state.
    MediaInfo mediaInfo = mMediaInfo;
    if (hasAudioTracks) {
      mediaInfo.EnableAudio();
    }
    if (hasVideoTracks) {
      mediaInfo.EnableVideo();
      if (mSelectedVideoStreamTrack) {
        mediaInfo.mVideo.SetAlpha(mSelectedVideoStreamTrack->HasAlpha());
      }
    }
    MetadataLoaded(&mediaInfo, nullptr);
  }

  if (mMediaSource) {
    // readyState has changed, assuming it's following the pending mediasource
    // operations. Notify the Mediasource that the operations have completed.
    mMediaSource->CompletePendingTransactions();
  }

  enum NextFrameStatus nextFrameStatus = NextFrameStatus();
  if (mWaitingForKey == NOT_WAITING_FOR_KEY) {
    if (nextFrameStatus == NEXT_FRAME_UNAVAILABLE && mDecoder &&
        !mDecoder->IsEnded()) {
      nextFrameStatus = mDecoder->NextFrameBufferedStatus();
    }
  } else if (mWaitingForKey == WAITING_FOR_KEY) {
    if (nextFrameStatus == NEXT_FRAME_UNAVAILABLE ||
        nextFrameStatus == NEXT_FRAME_UNAVAILABLE_BUFFERING) {
      // http://w3c.github.io/encrypted-media/#wait-for-key
      // Continuing 7.3.4 Queue a "waitingforkey" Event
      // 4. Queue a task to fire a simple event named waitingforkey
      // at the media element.
      // 5. Set the readyState of media element to HAVE_METADATA.
      // NOTE: We'll change to HAVE_CURRENT_DATA or HAVE_METADATA
      // depending on whether we've loaded the first frame or not
      // below.
      // 6. Suspend playback.
      // Note: Playback will already be stalled, as the next frame is
      // unavailable.
      mWaitingForKey = WAITING_FOR_KEY_DISPATCHED;
      QueueEvent(u"waitingforkey"_ns);
    }
  } else {
    MOZ_ASSERT(mWaitingForKey == WAITING_FOR_KEY_DISPATCHED);
    if (nextFrameStatus == NEXT_FRAME_AVAILABLE) {
      // We have new frames after dispatching "waitingforkey".
      // This means we've got the key and can reset mWaitingForKey now.
      mWaitingForKey = NOT_WAITING_FOR_KEY;
    }
  }

  if (nextFrameStatus == MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE_SEEKING) {
    LOG(LogLevel::Debug,
        ("MediaElement {} UpdateReadyStateInternal() "
         "NEXT_FRAME_UNAVAILABLE_SEEKING; Forcing HAVE_METADATA",
         fmt::ptr(this)));
    ChangeReadyState(HAVE_METADATA);
    return;
  }

  if (IsVideo() && VideoTracks() && !VideoTracks()->IsEmpty() &&
      !IsPlaybackEnded() && GetImageContainer() &&
      !GetImageContainer()->HasCurrentImage()
#ifdef MOZ_WMF_CDM
      // WMFClearKey frame-server mode renders video internally without exposing
      // frames through the image container.
      && !(mDecoder && mDecoder->IsUsingWMFClearKey())
#endif
  ) {
    // Don't advance if we are playing video, but don't have a video frame.
    // Also, if video became available after advancing to HAVE_CURRENT_DATA
    // while we are still playing, we need to revert to HAVE_METADATA until
    // a video frame is available.
    LOG(LogLevel::Debug,
        ("MediaElement {} UpdateReadyStateInternal() "
         "Playing video but no video frame; Forcing HAVE_METADATA",
         fmt::ptr(this)));
    ChangeReadyState(HAVE_METADATA);
    return;
  }

  if (!mFirstFrameLoaded) {
    // We haven't yet loaded the first frame, making us unable to determine
    // if we have enough valid data at the present stage.
    return;
  }

  if (nextFrameStatus == NEXT_FRAME_UNAVAILABLE_BUFFERING) {
    // Force HAVE_CURRENT_DATA when buffering.
    ChangeReadyState(HAVE_CURRENT_DATA);
    return;
  }

  // TextTracks must be loaded for the HAVE_ENOUGH_DATA and
  // HAVE_FUTURE_DATA.
  // So force HAVE_CURRENT_DATA if text tracks not loaded.
  if (mTextTrackManager && !mTextTrackManager->IsLoaded()) {
    ChangeReadyState(HAVE_CURRENT_DATA);
    return;
  }

  if (mDownloadSuspendedByCache && mDecoder && !mDecoder->IsEnded()) {
    // The decoder has signaled that the download has been suspended by the
    // media cache. So move readyState into HAVE_ENOUGH_DATA, in case there's
    // script waiting for a "canplaythrough" event; without this forced
    // transition, we will never fire the "canplaythrough" event if the
    // media cache is too small, and scripts are bound to fail. Don't force
    // this transition if the decoder is in ended state; the readyState
    // should remain at HAVE_CURRENT_DATA in this case.
    // Note that this state transition includes the case where we finished
    // downloaded the whole data stream.
    LOG(LogLevel::Debug, ("MediaElement {} UpdateReadyStateInternal() "
                          "Decoder download suspended by cache",
                          fmt::ptr(this)));
    ChangeReadyState(HAVE_ENOUGH_DATA);
    return;
  }

  if (nextFrameStatus != MediaDecoderOwner::NEXT_FRAME_AVAILABLE) {
    LOG(LogLevel::Debug, ("MediaElement {} UpdateReadyStateInternal() "
                          "Next frame not available",
                          fmt::ptr(this)));
    ChangeReadyState(HAVE_CURRENT_DATA);
    return;
  }

  if (mSrcStream) {
    LOG(LogLevel::Debug, ("MediaElement {} UpdateReadyStateInternal() "
                          "Stream HAVE_ENOUGH_DATA",
                          fmt::ptr(this)));
    ChangeReadyState(HAVE_ENOUGH_DATA);
    return;
  }

  // Now see if we should set HAVE_ENOUGH_DATA.
  // If it's something we don't know the size of, then we can't
  // make a real estimate, so we go straight to HAVE_ENOUGH_DATA once
  // we've downloaded enough data that our download rate is considered
  // reliable. We have to move to HAVE_ENOUGH_DATA at some point or
  // autoplay elements for live streams will never play. Otherwise we
  // move to HAVE_ENOUGH_DATA if we can play through the entire media
  // without stopping to buffer.
  if (mDecoder->CanPlayThrough()) {
    LOG(LogLevel::Debug, ("MediaElement {} UpdateReadyStateInternal() "
                          "Decoder can play through",
                          fmt::ptr(this)));
    ChangeReadyState(HAVE_ENOUGH_DATA);
    return;
  }
  LOG(LogLevel::Debug, ("MediaElement {} UpdateReadyStateInternal() "
                        "Default; Decoder has future data",
                        fmt::ptr(this)));
  ChangeReadyState(HAVE_FUTURE_DATA);
}

static const char* const gReadyStateToString[] = {
    "HAVE_NOTHING", "HAVE_METADATA", "HAVE_CURRENT_DATA", "HAVE_FUTURE_DATA",
    "HAVE_ENOUGH_DATA"};

void HTMLMediaElement::ChangeReadyState(nsMediaReadyState aState) {
  if (mReadyState == aState) {
    return;
  }

  RefPtr<HTMLMediaElement> kungFuDeathGrip(this);

  nsMediaReadyState oldState = mReadyState;
  mReadyState = aState;
  LOG(LogLevel::Debug, ("{} Ready state changed to {}", fmt::ptr(this),
                        gReadyStateToString[aState]));

  DDLOG(DDLogCategory::Property, "ready_state", gReadyStateToString[aState]);

  // https://html.spec.whatwg.org/multipage/media.html#text-track-cue-active-flag
  // The user agent must synchronously unset cues' active flag whenever the
  // media element's readyState is changed back to HAVE_NOTHING.
  if (mReadyState == HAVE_NOTHING && mTextTrackManager) {
    mTextTrackManager->NotifyReset();
  }

  if (mNetworkState == NETWORK_EMPTY) {
    return;
  }

  UpdateAudioChannelPlayingState();

  // Handle raising of "waiting" event during seek (see 4.8.10.9)
  // or
  // 4.8.12.7 Ready states:
  // "If the previous ready state was HAVE_FUTURE_DATA or more, and the new
  // ready state is HAVE_CURRENT_DATA or less
  // If the media element was potentially playing before its readyState
  // attribute changed to a value lower than HAVE_FUTURE_DATA, and the element
  // has not ended playback, and playback has not stopped due to errors,
  // paused for user interaction, or paused for in-band content, the user agent
  // must queue a task to fire a simple event named timeupdate at the element,
  // and queue a task to fire a simple event named waiting at the element."
  if (mPlayingBeforeSeek && mReadyState < HAVE_FUTURE_DATA) {
    QueueEvent(u"waiting"_ns);
  } else if (oldState >= HAVE_FUTURE_DATA && mReadyState < HAVE_FUTURE_DATA &&
             !Paused() && !Ended() && !mErrorSink->mError) {
    FireTimeUpdate(TimeupdateType::eMandatory);
    QueueEvent(u"waiting"_ns);
  }

  if (oldState < HAVE_CURRENT_DATA && mReadyState >= HAVE_CURRENT_DATA &&
      !mLoadedDataFired) {
    QueueEvent(u"loadeddata"_ns);
    mLoadedDataFired = true;
  }

  if (oldState < HAVE_FUTURE_DATA && mReadyState >= HAVE_FUTURE_DATA) {
    QueueEvent(u"canplay"_ns);
    if (!mPaused) {
      if (mDecoder && !mSuspendedByInactiveDocOrDocshell) {
        MOZ_ASSERT(AllowedToPlay());
        mDecoder->Play();
      }
      NotifyAboutPlaying();
    }
  }

  CheckAutoplayDataReady();

  if (oldState < HAVE_ENOUGH_DATA && mReadyState >= HAVE_ENOUGH_DATA) {
    QueueEvent(u"canplaythrough"_ns);
  }
}

static const char* const gNetworkStateToString[] = {"EMPTY", "IDLE", "LOADING",
                                                    "NO_SOURCE"};

void HTMLMediaElement::ChangeNetworkState(nsMediaNetworkState aState) {
  if (mNetworkState == aState) {
    return;
  }

  nsMediaNetworkState oldState = mNetworkState;
  mNetworkState = aState;
  UpdatePlaybackPseudoClasses();
  LOG(LogLevel::Debug, ("{} Network state changed to {}", fmt::ptr(this),
                        gNetworkStateToString[aState]));
  DDLOG(DDLogCategory::Property, "network_state",
        gNetworkStateToString[aState]);

  if (oldState == NETWORK_LOADING) {
    // Stop progress notification when exiting NETWORK_LOADING.
    StopProgress();
  }

  if (mNetworkState == NETWORK_LOADING) {
    // Start progress notification when entering NETWORK_LOADING.
    StartProgress();
  } else if (mNetworkState == NETWORK_IDLE && !mErrorSink->mError) {
    // Fire 'suspend' event when entering NETWORK_IDLE and no error presented.
    QueueEvent(u"suspend"_ns);
  }

  // According to the resource selection (step2, step9-18), dedicated media
  // source failure step (step4) and aborting existing load (step4), set show
  // poster flag to true. https://html.spec.whatwg.org/multipage/media.html
  if (mNetworkState == NETWORK_NO_SOURCE || mNetworkState == NETWORK_EMPTY) {
    mShowPoster = true;
  }

  // Changing mNetworkState affects AddRemoveSelfReference().
  AddRemoveSelfReference();
}

void HTMLMediaElement::StartObservingGVAutoplayIfNeeded() {
#if defined(MOZ_WIDGET_ANDROID)
  if (mGVAutoplayObserver || !StaticPrefs::media_geckoview_autoplay_request()) {
    return;
  }
  nsPIDOMWindowInner* inner = OwnerDoc()->GetInnerWindow();
  RefPtr<BrowsingContext> ctx = inner ? inner->GetBrowsingContext() : nullptr;
  if (ctx && GVAutoplayPermissionRequestor::HasUnresolvedRequest(inner)) {
    mGVAutoplayObserver = MakeAndAddRef<GVAutoplayObserver>(this);
  }
#endif
}

void HTMLMediaElement::StopObservingGVAutoplayIfNeeded() {
#if defined(MOZ_WIDGET_ANDROID)
  if (!mGVAutoplayObserver) {
    return;
  }
  nsPIDOMWindowInner* inner = OwnerDoc()->GetInnerWindow();
  RefPtr<BrowsingContext> ctx = inner ? inner->GetBrowsingContext() : nullptr;
  if (ctx == nullptr ||
      !GVAutoplayPermissionRequestor::HasUnresolvedRequest(inner)) {
    mGVAutoplayObserver = nullptr;
  }
#endif
}

bool HTMLMediaElement::ShouldDelayPlayUntilGVAutoplayRequestResolved() const {
#if defined(MOZ_WIDGET_ANDROID)
  if (!StaticPrefs::media_geckoview_autoplay_request()) {
    return false;
  }
  nsPIDOMWindowInner* inner = OwnerDoc()->GetInnerWindow();
  if (!inner) {
    return false;
  }
  RefPtr<BrowsingContext> context = inner->GetBrowsingContext();
  if (!context) {
    return false;
  }
  context = context->Top();
  const bool isInaudible = Volume() == 0.0 || Muted() ||
                           (!HasAudio() && mReadyState >= HAVE_METADATA);
  const auto status = isInaudible
                          ? context->GetGVInaudibleAutoplayRequestStatus()
                          : context->GetGVAudibleAutoplayRequestStatus();
  return status == GVAutoplayRequestStatus::eUNKNOWN ||
         status == GVAutoplayRequestStatus::ePENDING;
#else
  return false;
#endif
}

bool HTMLMediaElement::IsEligibleForAutoplay() {
  // We also activate autoplay when playing a media source since the data
  // download is controlled by the script and there is no way to evaluate
  // MediaDecoder::CanPlayThrough().

  if (!HasAttr(nsGkAtoms::autoplay)) {
    return false;
  }

  if (!mCanAutoplayFlag) {
    return false;
  }

  if (IsEditable()) {
    return false;
  }

  if (!mPaused) {
    return false;
  }

  if (mSuspendedByInactiveDocOrDocshell) {
    return false;
  }

  // Static document is used for print preview and printing, should not be
  // autoplay
  if (OwnerDoc()->IsStaticDocument()) {
    return false;
  }

  if (ShouldBeSuspendedByInactiveDocShell()) {
    LOG(LogLevel::Debug,
        ("{} prohibiting autoplay by the docShell", fmt::ptr(this)));
    return false;
  }

  if (MediaPlaybackDelayPolicy::ShouldDelayPlayback(this)) {
    CreateResumeDelayedMediaPlaybackAgentIfNeeded();
    LOG(LogLevel::Debug, ("{} delay playing from autoplay", fmt::ptr(this)));
    return false;
  }

  return mReadyState >= HAVE_ENOUGH_DATA;
}

void HTMLMediaElement::CheckAutoplayDataReady() {
  if (!IsEligibleForAutoplay()) {
    return;
  }
  if (!AllowedToPlay()) {
    DispatchEventsWhenPlayWasNotAllowed();
    return;
  }
#if defined(MOZ_WIDGET_ANDROID)
  StopObservingGVAutoplayIfNeeded();
#endif
  RunAutoplay();
}

void HTMLMediaElement::RunAutoplay() {
  mAllowedToPlayPromise.ResolveIfExists(true, __func__);
  mPaused = false;
  UpdatePlaybackPseudoClasses();

  // We changed mPaused which can affect AddRemoveSelfReference
  AddRemoveSelfReference();
  UpdateSrcMediaStreamPlaying();
  UpdateAudioChannelPlayingState();
  StartMediaControlKeyListenerIfNeeded();

  if (mDecoder) {
    SetPlayedOrSeeked(true);
    if (!mCurrentPlayRangeStart) {
      mCurrentPlayRangeStart = Some(CurrentTime());
    }
    MOZ_ASSERT(!mSuspendedByInactiveDocOrDocshell);
    mDecoder->Play();
  } else if (mSrcStream) {
    SetPlayedOrSeeked(true);
  }

  // https://html.spec.whatwg.org/multipage/media.html#ready-states:show-poster-flag
  if (mShowPoster) {
    mShowPoster = false;
    if (mTextTrackManager) {
      mTextTrackManager->TimeMarchesOn();
    }
  }

  // For blocked media, the event would be pending until it is resumed.
  QueueEvent(u"play"_ns);

  QueueEvent(u"playing"_ns);

  MaybeMarkSHEntryAsUserInteracted();

  MaybeRecordRuntimeMutedContentAttrImpact();
}

bool HTMLMediaElement::IsActuallyInvisible() const {
  // That means an element is not connected. It probably hasn't connected to a
  // document tree, or connects to a disconnected DOM tree.
  if (!IsInComposedDoc()) {
    return true;
  }

  // An element is not in user's view port, which means it's either existing in
  // somewhere in the page where user hasn't seen yet, or is being set
  // `display:none`.
  if (!IsInViewPort()) {
    return true;
  }

  // Element being used in picture-in-picture mode would be always visible.
  if (IsBeingUsedInPictureInPictureMode()) {
    return false;
  }

  // That check is the page is in the background.
  return OwnerDoc()->Hidden();
}

bool HTMLMediaElement::IsInViewPort() const {
  return mVisibilityState == Visibility::ApproximatelyVisible;
}

VideoFrameContainer* HTMLMediaElement::GetVideoFrameContainer() {
  if (mShuttingDown) {
    return nullptr;
  }

  if (mVideoFrameContainer) {
    return mVideoFrameContainer;
  }

  // Only video frames need an image container.
  if (!IsVideo()) {
    return nullptr;
  }

  mVideoFrameContainer = new VideoFrameContainer(
      this, MakeAndAddRef<ImageContainer>(ImageUsageType::VideoFrameContainer,
                                          ImageContainer::ASYNCHRONOUS));

  return mVideoFrameContainer;
}

void HTMLMediaElement::PrincipalChanged(MediaStreamTrack* aTrack) {
  if (aTrack != mSelectedVideoStreamTrack) {
    return;
  }

  nsContentUtils::CombineResourcePrincipals(&mSrcStreamVideoPrincipal,
                                            aTrack->GetPrincipal());

  LOG(LogLevel::Debug,
      ("HTMLMediaElement {} video track principal changed to {} (combined "
       "into {}). Waiting for it to reach VideoFrameContainer before setting.",
       fmt::ptr(this), fmt::ptr(aTrack->GetPrincipal()),
       fmt::ptr(mSrcStreamVideoPrincipal.get())));

  if (mVideoFrameContainer) {
    UpdateSrcStreamVideoPrincipal(
        mVideoFrameContainer->GetLastPrincipalHandle());
  }
}

void HTMLMediaElement::UpdateSrcStreamVideoPrincipal(
    const PrincipalHandle& aPrincipalHandle) {
  nsTArray<RefPtr<VideoStreamTrack>> videoTracks;
  mSrcStream->GetVideoTracks(videoTracks);

  for (const RefPtr<VideoStreamTrack>& track : videoTracks) {
    if (PrincipalHandleMatches(aPrincipalHandle, track->GetPrincipal()) &&
        !track->Ended()) {
      // When the PrincipalHandle for the VideoFrameContainer changes to that of
      // a live track in mSrcStream we know that a removed track was displayed
      // but is no longer so.
      LOG(LogLevel::Debug, ("HTMLMediaElement {} VideoFrameContainer's "
                            "PrincipalHandle matches track {}. That's all we "
                            "need.",
                            fmt::ptr(this), fmt::ptr(track.get())));
      mSrcStreamVideoPrincipal = track->GetPrincipal();
      break;
    }
  }
}

void HTMLMediaElement::PrincipalHandleChangedForVideoFrameContainer(
    VideoFrameContainer* aContainer,
    const PrincipalHandle& aNewPrincipalHandle) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mSrcStream) {
    return;
  }

  LOG(LogLevel::Debug, ("HTMLMediaElement {} PrincipalHandle changed in "
                        "VideoFrameContainer.",
                        fmt::ptr(this)));

  UpdateSrcStreamVideoPrincipal(aNewPrincipalHandle);
}

already_AddRefed<nsMediaEventRunner> HTMLMediaElement::GetEventRunner(
    const nsAString& aName, EventFlag aFlag) {
  RefPtr<nsMediaEventRunner> runner;
  if (aName.EqualsLiteral("playing")) {
    runner = new nsNotifyAboutPlayingRunner(this, TakePendingPlayPromises());
  } else if (aName.EqualsLiteral("timeupdate")) {
    runner = new nsTimeupdateRunner(this, aFlag == EventFlag::eMandatory);
  } else {
    runner = new nsAsyncEventRunner(aName, this);
  }
  return runner.forget();
}

nsresult HTMLMediaElement::FireEvent(const nsAString& aName) {
  if (mEventBlocker->ShouldBlockEventDelivery()) {
    RefPtr<nsMediaEventRunner> runner = GetEventRunner(aName);
    mEventBlocker->PostponeEvent(runner);
    return NS_OK;
  }

  LOG_EVENT(LogLevel::Debug, ("{} Firing event {}", fmt::ptr(this),
                              NS_ConvertUTF16toUTF8(aName).get()));

  return nsContentUtils::DispatchTrustedEvent(this, aName, CanBubble::eNo,
                                              Cancelable::eNo);
}

void HTMLMediaElement::QueueEvent(const nsAString& aName) {
  RefPtr<nsMediaEventRunner> runner = GetEventRunner(aName);
  QueueTask(std::move(runner));
}

void HTMLMediaElement::QueueTask(RefPtr<nsMediaEventRunner> aRunner) {
  NS_ConvertUTF16toUTF8 eventName(aRunner->EventName());
  LOG_EVENT(LogLevel::Debug,
            ("{} Queuing event {}", fmt::ptr(this), eventName.get()));
  DDLOG(DDLogCategory::Event, "HTMLMediaElement", nsCString(eventName.get()));
  if (mEventBlocker->ShouldBlockEventDelivery()) {
    mEventBlocker->PostponeEvent(aRunner);
    return;
  }
  GetMainThreadSerialEventTarget()->Dispatch(aRunner.forget());
}

bool HTMLMediaElement::IsPotentiallyPlaying() const {
  // TODO:
  //   playback has not stopped due to errors,
  //   and the element has not paused for user interaction
  return !mPaused &&
         (mReadyState == HAVE_ENOUGH_DATA || mReadyState == HAVE_FUTURE_DATA) &&
         !IsPlaybackEnded();
}

bool HTMLMediaElement::IsPlaybackEnded() const {
  // TODO:
  //   the current playback position is equal to the effective end of the media
  //   resource. See bug 449157.
  if (mDecoder) {
    return mReadyState >= HAVE_METADATA && mDecoder->IsEnded();
  }
  if (mSrcStream) {
    return mReadyState >= HAVE_METADATA && mSrcStreamPlaybackEnded;
  }
  return false;
}

already_AddRefed<nsIPrincipal> HTMLMediaElement::GetCurrentPrincipal() {
  if (mDecoder) {
    return mDecoder->GetCurrentPrincipal();
  }
  if (mSrcStream) {
    nsTArray<RefPtr<MediaStreamTrack>> tracks;
    mSrcStream->GetTracks(tracks);
    nsCOMPtr<nsIPrincipal> principal = mSrcStream->GetPrincipal();
    return principal.forget();
  }
  return nullptr;
}

bool HTMLMediaElement::HadCrossOriginRedirects() {
  if (mDecoder) {
    return mDecoder->HadCrossOriginRedirects();
  }
  return false;
}

bool HTMLMediaElement::ShouldResistFingerprinting(RFPTarget aTarget) const {
  return OwnerDoc()->ShouldResistFingerprinting(aTarget);
}

already_AddRefed<nsIPrincipal> HTMLMediaElement::GetCurrentVideoPrincipal() {
  if (mDecoder) {
    return mDecoder->GetCurrentPrincipal();
  }
  if (mSrcStream) {
    nsCOMPtr<nsIPrincipal> principal = mSrcStreamVideoPrincipal;
    return principal.forget();
  }
  return nullptr;
}

void HTMLMediaElement::NotifyDecoderPrincipalChanged() {
  RefPtr<nsIPrincipal> principal = GetCurrentPrincipal();
  bool isSameOrigin = !principal || IsCORSSameOrigin();
  mDecoder->UpdateSameOriginStatus(isSameOrigin);

  if (isSameOrigin) {
    principal = NodePrincipal();
  }
  for (const auto& entry : mOutputTrackSources.Values()) {
    entry->SetPrincipal(principal);
  }
  mDecoder->SetOutputTracksPrincipal(principal);
}

void HTMLMediaElement::Invalidate(ImageSizeChanged aImageSizeChanged,
                                  const Maybe<nsIntSize>& aNewIntrinsicSize,
                                  const Maybe<VideoRotation>& aNewRotation,
                                  ForceInvalidate aForceInvalidate) {
  nsIFrame* frame = GetPrimaryFrame();
  if (aNewIntrinsicSize || aNewRotation) {
    // Whichever of size/rotation didn't change this cycle keeps its
    // already-applied value from mMediaInfo.mVideo.
    UpdateMediaSize(aNewIntrinsicSize.valueOr(mMediaInfo.mVideo.mDisplay),
                    aNewRotation.valueOr(mMediaInfo.mVideo.mRotation));
    if (frame) {
      nsPresContext* presContext = frame->PresContext();
      PresShell* presShell = presContext->PresShell();
      presShell->FrameNeedsReflow(frame,
                                  IntrinsicDirty::FrameAncestorsAndDescendants,
                                  NS_FRAME_IS_DIRTY);
    }
  }

  RefPtr<ImageContainer> imageContainer = GetImageContainer();
  bool asyncInvalidate = imageContainer && imageContainer->IsAsync() &&
                         aForceInvalidate == ForceInvalidate::No;
  if (frame) {
    if (aImageSizeChanged == ImageSizeChanged::Yes) {
      frame->InvalidateFrame();
    } else {
      frame->InvalidateLayer(DisplayItemType::TYPE_VIDEO, nullptr, nullptr,
                             asyncInvalidate ? nsIFrame::UPDATE_IS_ASYNC : 0);
    }
  }

  SVGObserverUtils::InvalidateDirectRenderingObservers(this);
}

static nsIntSize EffectiveVideoSize(const nsIntSize& aSize,
                                    VideoRotation aRotation) {
  if (aRotation == VideoRotation::kDegree_90 ||
      aRotation == VideoRotation::kDegree_270) {
    return nsIntSize(aSize.height, aSize.width);
  }
  return aSize;
}

void HTMLMediaElement::UpdateMediaSize(const nsIntSize& aSize,
                                       VideoRotation aRotation) {
  MOZ_ASSERT(NS_IsMainThread());

  if (IsVideo() && mReadyState != HAVE_NOTHING &&
      EffectiveVideoSize(mMediaInfo.mVideo.mDisplay,
                         mMediaInfo.mVideo.mRotation) !=
          EffectiveVideoSize(aSize, aRotation)) {
    QueueEvent(u"resize"_ns);
  }

  mMediaInfo.mVideo.mDisplay = aSize;
  mMediaInfo.mVideo.mRotation = aRotation;
  mWatchManager.ManualNotify(&HTMLMediaElement::UpdateReadyStateInternal);
}

void HTMLMediaElement::SuspendOrResumeElement(bool aSuspendElement) {
  LOG(LogLevel::Debug, ("{} SuspendOrResumeElement(suspend={}) docHidden={}",
                        fmt::ptr(this), aSuspendElement, OwnerDoc()->Hidden()));

  if (aSuspendElement == mSuspendedByInactiveDocOrDocshell) {
    return;
  }

  mSuspendedByInactiveDocOrDocshell = aSuspendElement;
  UpdateSrcMediaStreamPlaying();
  UpdateAudioChannelPlayingState();

  if (aSuspendElement) {
    if (mDecoder) {
      mDecoder->Pause();
      mDecoder->Suspend();
      mDecoder->SetDelaySeekMode(true);
    }
    mEventBlocker->SetBlockEventDelivery(true);
    // We won't want to resume media element from the bfcache.
    ClearResumeDelayedMediaPlaybackAgentIfNeeded();
    mMediaControlKeyListener->StopIfNeeded();
  } else {
    if (mDecoder) {
      mDecoder->Resume();
      if (!mPaused && !mDecoder->IsEnded()) {
        mDecoder->Play();
      }
      mDecoder->SetDelaySeekMode(false);
    }
    mEventBlocker->SetBlockEventDelivery(false);
    // If the media element has been blocked and isn't still allowed to play
    // when it comes back from the bfcache, we would notify front end to show
    // the blocking icon in order to inform user that the site is still being
    // blocked.
    if (mHasEverBeenBlockedForAutoplay && !AllowedToPlay()) {
      MaybeNotifyAutoplayBlocked();
    }
    StartMediaControlKeyListenerIfNeeded();
  }
  if (StaticPrefs::media_testing_only_events()) {
    auto dispatcher = MakeRefPtr<AsyncEventDispatcher>(
        this, u"MozMediaSuspendChanged"_ns, CanBubble::eYes,
        ChromeOnlyDispatch::eYes);
    dispatcher->PostDOMEvent();
  }
}

bool HTMLMediaElement::IsBeingDestroyed() {
  nsIDocShell* docShell = OwnerDoc()->GetDocShell();
  bool isBeingDestroyed = false;
  if (docShell) {
    docShell->IsBeingDestroyed(&isBeingDestroyed);
  }
  return isBeingDestroyed;
}

bool HTMLMediaElement::ShouldBeSuspendedByInactiveDocShell() const {
  BrowsingContext* bc = OwnerDoc()->GetBrowsingContext();
  return bc && !bc->IsActive() && bc->Top()->GetSuspendMediaWhenInactive();
}

void HTMLMediaElement::NotifyOwnerDocumentActivityChanged() {
  if (mDecoder && !IsBeingDestroyed()) {
    NotifyDecoderActivityChanges();
  }

  // We would suspend media when the document is inactive, or its docshell has
  // been set to hidden and explicitly wants to suspend media. In those cases,
  // the media would be not visible and we don't want them to continue playing.
  bool shouldSuspend =
      !OwnerDoc()->IsActive() || ShouldBeSuspendedByInactiveDocShell();
  SuspendOrResumeElement(shouldSuspend);

  // If the owning document has become inactive we should shutdown the CDM.
  if (!OwnerDoc()->IsCurrentActiveDocument() && mMediaKeys) {
    // We don't shutdown MediaKeys here because it also listens for document
    // activity and will take care of shutting down itself.
    DDUNLINKCHILD(mMediaKeys.get());
    mMediaKeys = nullptr;
    if (mDecoder) {
      ShutdownDecoder();
    }
  }

#if defined(MOZ_WIDGET_ANDROID)
  // Android-only probe (bug 2066141) to help the mobile product team
  // answer how often background media playback happens. It is recorded when
  // this element transitions to hidden while audibly playing (the app is
  // backgrounded), once per background episode, and reset when the document
  // becomes visible again. Picture-in-Picture is excluded, since there the
  // document is hidden but the video is still visible to the user.
  //
  // This is a broad signal: it only fires on the foreground-to-background
  // transition, so it does NOT capture all background media playback (for
  // example media that starts or resumes while already backgrounded) and will
  // under-count.
  if (OwnerDoc()->Hidden() && !OwnerDoc()->InAndroidPipMode() && !mPaused &&
      IsAudible()) {
    if (!mRecordedBackgroundAudioPlayback) {
      mRecordedBackgroundAudioPlayback = true;
      glean::media::background_audio_playback.Record();
    }
  } else if (!OwnerDoc()->Hidden()) {
    mRecordedBackgroundAudioPlayback = false;
  }
#endif

  AddRemoveSelfReference();
}

void HTMLMediaElement::NotifyFullScreenChanged() {
  const bool isInFullScreen = IsInFullScreen();
  if (isInFullScreen) {
    StartMediaControlKeyListenerIfNeeded();
    if (!mMediaControlKeyListener->IsStarted()) {
      MEDIACONTROL_LOG("Failed to start the listener when entering fullscreen");
    }
  }
  // Updating controller fullscreen state no matter the listener starts or not.
  BrowsingContext* bc = OwnerDoc()->GetBrowsingContext();
  if (RefPtr<IMediaInfoUpdater> updater = ContentMediaAgent::Get(bc)) {
    updater->NotifyMediaFullScreenState(bc->Id(), isInFullScreen);
  }
}

void HTMLMediaElement::AddRemoveSelfReference() {
  // XXX we could release earlier here in many situations if we examined
  // which event listeners are attached. Right now we assume there is a
  // potential listener for every event. We would also have to keep the
  // element alive if it was playing and producing audio output --- right now
  // that's covered by the !mPaused check.
  Document* ownerDoc = OwnerDoc();

  // See the comment at the top of this file for the explanation of this
  // boolean expression.
  bool needSelfReference =
      !mShuttingDown && ownerDoc->IsActive() &&
      (mDelayingLoadEvent || (!mPaused && !Ended()) ||
       (mDecoder && mDecoder->IsSeeking()) || IsEligibleForAutoplay() ||
       (mMediaSource ? mProgressTimer : mNetworkState == NETWORK_LOADING));

  if (needSelfReference != mHasSelfReference) {
    mHasSelfReference = needSelfReference;
    RefPtr<HTMLMediaElement> self = this;
    if (needSelfReference) {
      // The shutdown observer will hold a strong reference to us. This
      // will do to keep us alive. We need to know about shutdown so that
      // we can release our self-reference.
      GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
          "dom::HTMLMediaElement::AddSelfReference",
          [self]() { self->mShutdownObserver->AddRefMediaElement(); }));
    } else {
      // Dispatch Release asynchronously so that we don't destroy this object
      // inside a call stack of method calls on this object
      GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
          "dom::HTMLMediaElement::AddSelfReference",
          [self]() { self->mShutdownObserver->ReleaseMediaElement(); }));
    }
  }
}

void HTMLMediaElement::NotifyShutdownEvent() {
  mShuttingDown = true;
  ResetState();
  AddRemoveSelfReference();
}

void HTMLMediaElement::DispatchAsyncSourceError(
    nsIContent* aSourceElement, const nsACString& aErrorDetails) {
  LOG_EVENT(LogLevel::Debug,
            ("{} Queuing simple source error event", fmt::ptr(this)));

  nsCOMPtr<nsIRunnable> event =
      new nsSourceErrorEventRunner(this, aSourceElement, aErrorDetails);
  GetMainThreadSerialEventTarget()->Dispatch(event.forget());
}

void HTMLMediaElement::NotifyAddedSource() {
  // If a source element is inserted as a child of a media element
  // that has no src attribute and whose networkState has the value
  // NETWORK_EMPTY, the user agent must invoke the media element's
  // resource selection algorithm.
  if (!HasAttr(nsGkAtoms::src) && mNetworkState == NETWORK_EMPTY) {
    AssertReadyStateIsNothing();
    QueueSelectResourceTask();
  }

  // A load was paused in the resource selection algorithm, waiting for
  // a new source child to be added, resume the resource selection algorithm.
  if (mLoadWaitStatus == WAITING_FOR_SOURCE) {
    // Rest the flag so we don't queue multiple LoadFromSourceTask() when
    // multiple <source> are attached in an event loop.
    mLoadWaitStatus = NOT_WAITING;
    QueueLoadFromSourceTask();
  }
}

HTMLSourceElement* HTMLMediaElement::GetNextSource() {
  mSourceLoadCandidate = nullptr;

  while (true) {
    if (mSourcePointer == nsINode::GetLastChild()) {
      return nullptr;  // no more children
    }

    if (!mSourcePointer) {
      mSourcePointer = nsINode::GetFirstChild();
    } else {
      mSourcePointer = mSourcePointer->GetNextSibling();
    }
    nsIContent* child = mSourcePointer;

    // If child is a <source> element, it is the next candidate.
    if (auto* source = HTMLSourceElement::FromNodeOrNull(child)) {
      mSourceLoadCandidate = source;
      return source;
    }
  }
  MOZ_ASSERT_UNREACHABLE("Execution should not reach here!");
  return nullptr;
}

void HTMLMediaElement::ChangeDelayLoadStatus(bool aDelay) {
  if (aDelay && LoadingState() == Loading::Lazy) {
    // Lazy media must not delay the document load event after deferral resumes.
    aDelay = false;
  }

  if (mDelayingLoadEvent == aDelay) {
    return;
  }

  mDelayingLoadEvent = aDelay;

  LOG(LogLevel::Debug, ("{} ChangeDelayLoadStatus({}) doc=0x{}", fmt::ptr(this),
                        aDelay, fmt::ptr(mLoadBlockedDoc.get())));
  if (mDecoder) {
    mDecoder->SetLoadInBackground(!aDelay);
  }
  if (aDelay) {
    mLoadBlockedDoc = OwnerDoc();
    mLoadBlockedDoc->BlockOnload();
  } else {
    // mLoadBlockedDoc might be null due to GC unlinking
    if (mLoadBlockedDoc) {
      mLoadBlockedDoc->UnblockOnload(false);
      mLoadBlockedDoc = nullptr;
    }
  }

  // We changed mDelayingLoadEvent which can affect AddRemoveSelfReference
  AddRemoveSelfReference();
}

already_AddRefed<nsILoadGroup> HTMLMediaElement::GetDocumentLoadGroup() {
  if (!OwnerDoc()->IsActive()) {
    NS_WARNING("Load group requested for media element in inactive document.");
  }
  return OwnerDoc()->GetDocumentLoadGroup();
}

nsresult HTMLMediaElement::CopyInnerTo(Element* aDest) {
  nsresult rv = nsGenericHTMLElement::CopyInnerTo(aDest);
  NS_ENSURE_SUCCESS(rv, rv);

  HTMLMediaElement* dest = static_cast<HTMLMediaElement*>(aDest);
  // The clone has the "default" muted state, so its muted content attribute
  // makes it muted.
  if (HasAttr(nsGkAtoms::muted)) {
    dest->mMuted |= MUTED_BY_CONTENT;
    dest->SetStates(ElementState::MUTED, dest->Muted());
  }

  if (aDest->OwnerDoc()->IsStaticDocument()) {
    dest->SetMediaInfo(mMediaInfo);
  }
  return rv;
}

already_AddRefed<TimeRanges> HTMLMediaElement::Buffered() const {
  media::TimeIntervals buffered =
      mDecoder ? mDecoder->GetBuffered() : media::TimeIntervals();
  RefPtr<TimeRanges> ranges = new TimeRanges(
      ToSupports(OwnerDoc()), buffered.ToMicrosecondResolution());
  return ranges.forget();
}

void HTMLMediaElement::SetRequestHeaders(nsIHttpChannel* aChannel) {
  // Send Accept header for video and audio types only (Bug 489071)
  SetAcceptHeader(aChannel);

  // Apache doesn't send Content-Length when gzip transfer encoding is used,
  // which prevents us from estimating the video length (if explicit
  // Content-Duration and a length spec in the container are not present either)
  // and from seeking. So, disable the standard "Accept-Encoding: gzip,deflate"
  // that we usually send. See bug 614760.
  DebugOnly<nsresult> rv =
      aChannel->SetRequestHeader("Accept-Encoding"_ns, ""_ns, false);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  // Set the Referrer header
  //
  // FIXME: Shouldn't this use the Element constructor? Though I guess it
  // doesn't matter as no HTMLMediaElement supports the referrerinfo attribute.
  auto referrerInfo = MakeRefPtr<ReferrerInfo>(*OwnerDoc());
  rv = aChannel->SetReferrerInfoWithoutClone(referrerInfo);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

const TimeStamp& HTMLMediaElement::LastTimeupdateDispatchTime() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mLastTimeUpdateDispatchTime;
}

void HTMLMediaElement::UpdateLastTimeupdateDispatchTime() {
  MOZ_ASSERT(NS_IsMainThread());
  mLastTimeUpdateDispatchTime = TimeStamp::Now();
}

bool HTMLMediaElement::ShouldQueueTimeupdateAsyncTask(
    TimeupdateType aType) const {
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  // That means dispatching `timeupdate` is mandatorily required in the spec.
  if (aType == TimeupdateType::eMandatory) {
    return true;
  }

  // The timeupdate only occurs when the current playback position changes.
  // https://html.spec.whatwg.org/multipage/media.html#event-media-timeupdate
  if (mLastCurrentTime == CurrentTime()) {
    return false;
  }

  // Number of milliseconds between timeupdate events as defined by spec.
  if (!mQueueTimeUpdateRunnerTime.IsNull() &&
      TimeStamp::Now() - mQueueTimeUpdateRunnerTime <
          TimeDuration::FromMilliseconds(TIMEUPDATE_MS)) {
    return false;
  }
  return true;
}

void HTMLMediaElement::FireTimeUpdate(TimeupdateType aType) {
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  RefPtr<HTMLMediaElement> kungFuDeathGrip(this);

  if (ShouldQueueTimeupdateAsyncTask(aType)) {
    RefPtr<nsMediaEventRunner> runner =
        GetEventRunner(u"timeupdate"_ns, aType == TimeupdateType::eMandatory
                                             ? EventFlag::eMandatory
                                             : EventFlag::eNone);
    QueueTask(std::move(runner));
    mQueueTimeUpdateRunnerTime = TimeStamp::Now();
    mLastCurrentTime = CurrentTime();
  }
  if (mFragmentEnd >= 0.0 && CurrentTime() >= mFragmentEnd) {
    Pause();
    mFragmentEnd = -1.0;
    mFragmentStart = -1.0;
    mDecoder->SetFragmentEndTime(mFragmentEnd);
  }

  // Update the cues displaying on the video.
  // Here mTextTrackManager can be null if the cycle collector has unlinked
  // us before our parent. In that case UnbindFromTree will call us
  // when our parent is unlinked.
  if (mTextTrackManager) {
    mTextTrackManager->TimeMarchesOn();
  }
}

MediaError* HTMLMediaElement::GetError() const { return mErrorSink->mError; }

void HTMLMediaElement::GetCurrentSpec(nsCString& aString) {
  // If playing a regular URL, an ObjectURL of a Blob/File, return that.
  if (mLoadingSrc) {
    mLoadingSrc->GetSpec(aString);
  } else if (mSrcMediaSource) {
    // If playing an ObjectURL, and it's a MediaSource, return the value of the
    // `src` attribute.
    nsAutoString src;
    GetSrc(src);
    CopyUTF16toUTF8(src, aString);
  } else {
    // Playing e.g. a MediaStream via an object URL - return an empty string
    aString.Truncate();
  }
}

double HTMLMediaElement::MozFragmentEnd() {
  double duration = Duration();

  // If there is no end fragment, or the fragment end is greater than the
  // duration, return the duration.
  return (mFragmentEnd < 0.0 || mFragmentEnd > duration) ? duration
                                                         : mFragmentEnd;
}

void HTMLMediaElement::SetDefaultPlaybackRate(double aDefaultPlaybackRate,
                                              ErrorResult& aRv) {
  if (mSrcAttrStream) {
    return;
  }

  if (aDefaultPlaybackRate < 0) {
    aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
    return;
  }

  double defaultPlaybackRate = ClampPlaybackRate(aDefaultPlaybackRate);

  if (mDefaultPlaybackRate == defaultPlaybackRate) {
    return;
  }

  mDefaultPlaybackRate = defaultPlaybackRate;
  QueueEvent(u"ratechange"_ns);
}

void HTMLMediaElement::SetPlaybackRate(double aPlaybackRate, ErrorResult& aRv) {
  if (mSrcAttrStream) {
    return;
  }

  // Changing the playback rate of a media that has more than two channels is
  // not supported.
  if (aPlaybackRate < 0) {
    aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return;
  }

  if (mPlaybackRate == aPlaybackRate) {
    return;
  }

  mPlaybackRate = aPlaybackRate;
  // Playback rate threshold above which audio is muted.
  uint32_t threshold = StaticPrefs::media_audio_playbackrate_muting_threshold();
  if (mPlaybackRate != 0.0 &&
      (mPlaybackRate > threshold || mPlaybackRate < 1. / threshold)) {
    SetMutedInternal(mMuted | MUTED_BY_INVALID_PLAYBACK_RATE);
  } else {
    SetMutedInternal(mMuted & ~MUTED_BY_INVALID_PLAYBACK_RATE);
  }

  if (mDecoder) {
    mDecoder->SetPlaybackRate(ClampPlaybackRate(mPlaybackRate));
  }
  QueueEvent(u"ratechange"_ns);
  mMediaControlKeyListener->NotifyMediaPositionState();
}

void HTMLMediaElement::SetPreservesPitch(bool aPreservesPitch) {
  mPreservesPitch = aPreservesPitch;
  if (mDecoder) {
    mDecoder->SetPreservesPitch(mPreservesPitch);
  }
}

ImageContainer* HTMLMediaElement::GetImageContainer() {
  VideoFrameContainer* container = GetVideoFrameContainer();
  return container ? container->GetImageContainer() : nullptr;
}

void HTMLMediaElement::UpdateAudioChannelPlayingState() {
  if (mAudioChannelWrapper) {
    mAudioChannelWrapper->UpdateAudioChannelPlayingState();
  }
}

static const char* VisibilityString(Visibility aVisibility) {
  switch (aVisibility) {
    case Visibility::Untracked: {
      return "Untracked";
    }
    case Visibility::ApproximatelyNonVisible: {
      return "ApproximatelyNonVisible";
    }
    case Visibility::ApproximatelyVisible: {
      return "ApproximatelyVisible";
    }
  }

  return "NAN";
}

void HTMLMediaElement::OnVisibilityChange(Visibility aNewVisibility) {
  LOG(LogLevel::Debug,
      ("OnVisibilityChange(): {}\n", VisibilityString(aNewVisibility)));

  mVisibilityState = aNewVisibility;
  if (StaticPrefs::media_test_video_suspend()) {
    QueueEvent(u"visibilitychanged"_ns);
  }

  if (!mDecoder) {
    return;
  }
  NotifyDecoderActivityChanges();
}

MediaKeys* HTMLMediaElement::GetMediaKeys() const { return mMediaKeys; }

bool HTMLMediaElement::ContainsRestrictedContent() const {
  return GetMediaKeys() != nullptr;
}

void HTMLMediaElement::SetCDMProxyFailure(const MediaResult& aResult) {
  LOG(LogLevel::Debug, ("{}", __func__));
  MOZ_ASSERT(mSetMediaKeysDOMPromise);

  ResetSetMediaKeysTempVariables();

  mSetMediaKeysDOMPromise->MaybeReject(aResult.Code(), aResult.Message());
}

void HTMLMediaElement::RemoveMediaKeys() {
  LOG(LogLevel::Debug, ("{}", __func__));
  // 5.2.3 Stop using the CDM instance represented by the mediaKeys attribute
  // to decrypt media data and remove the association with the media element.
  if (mMediaKeys) {
    mMediaKeys->Unbind();
  }
  mMediaKeys = nullptr;
}

bool HTMLMediaElement::TryRemoveMediaKeysAssociation() {
  MOZ_ASSERT(mMediaKeys);
  LOG(LogLevel::Debug, ("{}", __func__));
  // 5.2.1 If the user agent or CDM do not support removing the association,
  // let this object's attaching media keys value be false and reject promise
  // with a new DOMException whose name is NotSupportedError.
  // 5.2.2 If the association cannot currently be removed, let this object's
  // attaching media keys value be false and reject promise with a new
  // DOMException whose name is InvalidStateError.
  if (mDecoder) {
    RefPtr<HTMLMediaElement> self = this;
    mDecoder->SetCDMProxy(nullptr)
        ->Then(
            AbstractMainThread(), __func__,
            [self]() {
              self->mSetCDMRequest.Complete();

              self->RemoveMediaKeys();
              if (self->AttachNewMediaKeys()) {
                // No incoming MediaKeys object or MediaDecoder is not
                // created yet.
                self->MakeAssociationWithCDMResolved();
              }
            },
            [self](const MediaResult& aResult) {
              self->mSetCDMRequest.Complete();
              // 5.2.4 If the preceding step failed, let this object's
              // attaching media keys value be false and reject promise with
              // a new DOMException whose name is the appropriate error name.
              self->SetCDMProxyFailure(aResult);
            })
        ->Track(mSetCDMRequest);
    return false;
  }

  RemoveMediaKeys();
  return true;
}

bool HTMLMediaElement::DetachExistingMediaKeys() {
  LOG(LogLevel::Debug, ("{}", __func__));
  MOZ_ASSERT(mSetMediaKeysDOMPromise);
  // 5.1 If mediaKeys is not null, CDM instance represented by mediaKeys is
  // already in use by another media element, and the user agent is unable
  // to use it with this element, let this object's attaching media keys
  // value be false and reject promise with a new DOMException whose name
  // is QuotaExceededError.
  if (mIncomingMediaKeys && mIncomingMediaKeys->IsBoundToMediaElement()) {
    SetCDMProxyFailure(MediaResult(
        NS_ERROR_DOM_MEDIA_KEY_QUOTA_EXCEEDED_ERR,
        "MediaKeys object is already bound to another HTMLMediaElement"));
    return false;
  }

  // 5.2 If the mediaKeys attribute is not null, run the following steps:
  if (mMediaKeys) {
    return TryRemoveMediaKeysAssociation();
  }
  return true;
}

void HTMLMediaElement::MakeAssociationWithCDMResolved() {
  LOG(LogLevel::Debug, ("{}", __func__));
  MOZ_ASSERT(mSetMediaKeysDOMPromise);

  // 5.4 Set the mediaKeys attribute to mediaKeys.
  mMediaKeys = mIncomingMediaKeys;
#ifdef MOZ_WMF_CDM
  if (mMediaKeys && mMediaKeys->GetCDMProxy()) {
    mIsUsingWMFCDM = !!mMediaKeys->GetCDMProxy()->AsWMFCDMProxy();
  }
#endif
  // 5.5 Let this object's attaching media keys value be false.
  ResetSetMediaKeysTempVariables();
  // 5.6 Resolve promise.
  mSetMediaKeysDOMPromise->MaybeResolveWithUndefined();
  mSetMediaKeysDOMPromise = nullptr;

  if (profiler_is_collecting_markers()) {
    if (mMediaKeys) {
      nsString keySystem;
      mMediaKeys->GetKeySystem(keySystem);
      profiler_add_marker("cdmresolved",
                          geckoprofiler::category::MEDIA_PLAYBACK, {},
                          CDMResolvedMarker{}, keySystem,
                          mMediaKeys->GetMediaKeySystemConfigurationString(),
                          Flow::FromPointer(this));
    } else {
      PROFILER_MARKER("removemediakey", MEDIA_PLAYBACK, {}, FlowMarker,
                      Flow::FromPointer(this));
    }
  }
}

bool HTMLMediaElement::TryMakeAssociationWithCDM(CDMProxy* aProxy) {
  LOG(LogLevel::Debug, ("{}", __func__));
  MOZ_ASSERT(aProxy);

  // 5.3.3 Queue a task to run the "Attempt to Resume Playback If Necessary"
  // algorithm on the media element.
  // Note: Setting the CDMProxy on the MediaDecoder will unblock playback.
  if (mDecoder) {
    // CDMProxy is set asynchronously in MediaFormatReader, once it's done,
    // HTMLMediaElement should resolve or reject the DOM promise.
    RefPtr<HTMLMediaElement> self = this;
    mDecoder->SetCDMProxy(aProxy)
        ->Then(
            AbstractMainThread(), __func__,
            [self]() {
              self->mSetCDMRequest.Complete();
              self->MakeAssociationWithCDMResolved();
            },
            [self](const MediaResult& aResult) {
              self->mSetCDMRequest.Complete();
              self->SetCDMProxyFailure(aResult);
            })
        ->Track(mSetCDMRequest);
    return false;
  }
  return true;
}

bool HTMLMediaElement::AttachNewMediaKeys() {
  LOG(LogLevel::Debug, ("{} incoming MediaKeys({})", __func__,
                        fmt::ptr(mIncomingMediaKeys.get())));
  MOZ_ASSERT(mSetMediaKeysDOMPromise);

  // 5.3. If mediaKeys is not null, run the following steps:
  if (mIncomingMediaKeys) {
    auto* cdmProxy = mIncomingMediaKeys->GetCDMProxy();
    if (!cdmProxy) {
      SetCDMProxyFailure(MediaResult(
          NS_ERROR_DOM_INVALID_STATE_ERR,
          "CDM crashed before binding MediaKeys object to HTMLMediaElement"));
      return false;
    }

    // 5.3.1 Associate the CDM instance represented by mediaKeys with the
    // media element for decrypting media data.
    if (NS_FAILED(mIncomingMediaKeys->Bind(this))) {
      // 5.3.2 If the preceding step failed, run the following steps:

      // 5.3.2.1 Set the mediaKeys attribute to null.
      mMediaKeys = nullptr;
      // 5.3.2.2 Let this object's attaching media keys value be false.
      // 5.3.2.3 Reject promise with a new DOMException whose name is
      // the appropriate error name.
      SetCDMProxyFailure(
          MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                      "Failed to bind MediaKeys object to HTMLMediaElement"));
      return false;
    }
    return TryMakeAssociationWithCDM(cdmProxy);
  }
  return true;
}

void HTMLMediaElement::ResetSetMediaKeysTempVariables() {
  mAttachingMediaKey = false;
  mIncomingMediaKeys = nullptr;
}

bool HTMLMediaElement::MozAudioCaptured() const {
  ReportToConsole(nsIScriptError::warningFlag,
                  "MozAudioCapturedDeprecatedWarning");
  mozilla::glean::media::capture_stream_usage
      .EnumGet(
          mozilla::glean::media::CaptureStreamUsageLabel::eMozaudiocaptured)
      .Add();
  return mAudioCaptured;
}

already_AddRefed<Promise> HTMLMediaElement::SetMediaKeys(
    mozilla::dom::MediaKeys* aMediaKeys, ErrorResult& aRv) {
  LOG(LogLevel::Debug, ("{} SetMediaKeys({}) mMediaKeys={} mDecoder={}",
                        fmt::ptr(this), fmt::ptr(aMediaKeys),
                        fmt::ptr(mMediaKeys.get()), fmt::ptr(mDecoder.get())));

  if (MozAudioCaptured()) {
    aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return nullptr;
  }

  nsPIDOMWindowInner* win = OwnerDoc()->GetInnerWindow();
  if (!win) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }
  RefPtr<DetailedPromise> promise = DetailedPromise::Create(
      win->AsGlobal(), aRv, "HTMLMediaElement.setMediaKeys"_ns);
  if (aRv.Failed()) {
    return nullptr;
  }

  // 1. If mediaKeys and the mediaKeys attribute are the same object,
  // return a resolved promise.
  if (mMediaKeys == aMediaKeys) {
    promise->MaybeResolveWithUndefined();
    return promise.forget();
  }

  // 2. If this object's attaching media keys value is true, return a
  // promise rejected with a new DOMException whose name is InvalidStateError.
  if (mAttachingMediaKey) {
    promise->MaybeRejectWithInvalidStateError(
        "A MediaKeys object is in attaching operation.");
    return promise.forget();
  }

  // 3. Let this object's attaching media keys value be true.
  mAttachingMediaKey = true;
  mIncomingMediaKeys = aMediaKeys;

  // 4. Let promise be a new promise.
  mSetMediaKeysDOMPromise = promise;

  // 5. Run the following steps in parallel:

  // 5.1 & 5.2 & 5.3
  if (!DetachExistingMediaKeys() || !AttachNewMediaKeys()) {
    return promise.forget();
  }

  // 5.4, 5.5, 5.6
  MakeAssociationWithCDMResolved();

  // 6. Return promise.
  return promise.forget();
}

EventHandlerNonNull* HTMLMediaElement::GetOnencrypted() {
  return EventTarget::GetEventHandler(nsGkAtoms::onencrypted);
}

void HTMLMediaElement::SetOnencrypted(EventHandlerNonNull* aCallback) {
  EventTarget::SetEventHandler(nsGkAtoms::onencrypted, aCallback);
}

EventHandlerNonNull* HTMLMediaElement::GetOnwaitingforkey() {
  return EventTarget::GetEventHandler(nsGkAtoms::onwaitingforkey);
}

void HTMLMediaElement::SetOnwaitingforkey(EventHandlerNonNull* aCallback) {
  EventTarget::SetEventHandler(nsGkAtoms::onwaitingforkey, aCallback);
}

void HTMLMediaElement::DispatchEncrypted(const nsTArray<uint8_t>& aInitData,
                                         const nsAString& aInitDataType) {
  LOG(LogLevel::Debug,
      ("{} DispatchEncrypted initDataType='{}'", fmt::ptr(this),
       NS_ConvertUTF16toUTF8(aInitDataType).get()));

  if (mReadyState == HAVE_NOTHING) {
    // Ready state not HAVE_METADATA (yet), don't dispatch encrypted now.
    // Queueing for later dispatch in MetadataLoaded.
    mPendingEncryptedInitData.AddInitData(aInitDataType, aInitData);
    return;
  }

  RefPtr<MediaEncryptedEvent> event;
  if (IsCORSSameOrigin()) {
    event = MediaEncryptedEvent::Constructor(this, aInitDataType, aInitData);
  } else {
    event = MediaEncryptedEvent::Constructor(this);
  }

  RefPtr<AsyncEventDispatcher> asyncDispatcher =
      new AsyncEventDispatcher(this, event.forget());
  asyncDispatcher->PostDOMEvent();
  if (profiler_is_collecting_markers()) {
    nsPrintfCString markerName{"%p:encrypted", this};
    PROFILER_MARKER_UNTYPED(markerName, MEDIA_PLAYBACK);
  }
}

bool HTMLMediaElement::IsEventAttributeNameInternal(nsAtom* aName) {
  return nsContentUtils::IsEventAttributeName(
      aName, EventNameType_HTML | EventNameType_HTMLMedia);
}

void HTMLMediaElement::NotifyWaitingForKey() {
  LOG(LogLevel::Debug, ("{}, NotifyWaitingForKey()", fmt::ptr(this)));

  // http://w3c.github.io/encrypted-media/#wait-for-key
  // 7.3.4 Queue a "waitingforkey" Event
  // 1. Let the media element be the specified HTMLMediaElement object.
  // 2. If the media element's waiting for key value is true, abort these steps.
  if (mWaitingForKey == NOT_WAITING_FOR_KEY) {
    // 3. Set the media element's waiting for key value to true.
    // Note: algorithm continues in UpdateReadyStateInternal() when all decoded
    // data enqueued in the MDSM is consumed.
    mWaitingForKey = WAITING_FOR_KEY;
    // mWaitingForKey changed outside of UpdateReadyStateInternal. This may
    // affect mReadyState.
    mWatchManager.ManualNotify(&HTMLMediaElement::UpdateReadyStateInternal);
  }
}

AudioTrackList* HTMLMediaElement::AudioTracks() { return mAudioTrackList; }

VideoTrackList* HTMLMediaElement::VideoTracks() { return mVideoTrackList; }

TextTrackList* HTMLMediaElement::GetTextTracks() {
  return GetOrCreateTextTrackManager()->GetTextTracks();
}

already_AddRefed<TextTrack> HTMLMediaElement::AddTextTrack(
    TextTrackKind aKind, const nsAString& aLabel, const nsAString& aLanguage) {
  return GetOrCreateTextTrackManager()->AddTextTrack(
      aKind, aLabel, aLanguage, TextTrackMode::Hidden,
      TextTrackReadyState::Loaded, TextTrackSource::AddTextTrack);
}

void HTMLMediaElement::PopulatePendingTextTrackList() {
  if (mTextTrackManager) {
    mTextTrackManager->PopulatePendingList();
  }
}

TextTrackManager* HTMLMediaElement::GetOrCreateTextTrackManager() {
  if (!mTextTrackManager) {
    mTextTrackManager = new TextTrackManager(this);
  }
  return mTextTrackManager;
}

MediaDecoderOwner::NextFrameStatus HTMLMediaElement::NextFrameStatus() {
  if (mDecoder) {
    return mDecoder->NextFrameStatus();
  }
  if (mSrcStream) {
    AutoTArray<RefPtr<MediaTrack>, 4> tracks;
    GetAllEnabledMediaTracks(tracks);
    if (!tracks.IsEmpty() && !mSrcStreamPlaybackEnded) {
      return NEXT_FRAME_AVAILABLE;
    }
    return NEXT_FRAME_UNAVAILABLE;
  }
  return NEXT_FRAME_UNINITIALIZED;
}

void HTMLMediaElement::SetDecoder(MediaDecoder* aDecoder) {
  MOZ_ASSERT(aDecoder);  // Use ShutdownDecoder() to clear.
  if (mDecoder) {
    ShutdownDecoder();
  }
  mDecoder = aDecoder;
  DDLINKCHILD("decoder", mDecoder.get());
  if (mDecoder && mForcedHidden) {
    mDecoder->SetForcedHidden(mForcedHidden);
  }
}

float HTMLMediaElement::ComputedVolume() const {
  return mMuted                 ? 0.0f
         : mAudioChannelWrapper ? mAudioChannelWrapper->GetEffectiveVolume()
                                : static_cast<float>(mVolume);
}

bool HTMLMediaElement::ComputedMuted() const {
  return (mMuted & MUTED_BY_AUDIO_CHANNEL);
}

bool HTMLMediaElement::IsSuspendedByInactiveDocOrDocShell() const {
  return mSuspendedByInactiveDocOrDocshell;
}

bool HTMLMediaElement::IsCurrentlyPlaying() const {
  // We have playable data, but we still need to check whether data is "real"
  // current data.
  return mReadyState >= HAVE_CURRENT_DATA && !IsPlaybackEnded();
}

void HTMLMediaElement::SetAudibleState(bool aAudible) {
  if (mIsAudioTrackAudible != aAudible) {
    mIsAudioTrackAudible = aAudible;
    NotifyAudioPlaybackChanged(
        AudioChannelService::AudibleChangedReasons::eDataAudibleChanged);
  }
}

void HTMLMediaElement::NotifyAudioPlaybackChanged(
    AudibleChangedReasons aReason) {
  if (mAudioChannelWrapper) {
    mAudioChannelWrapper->NotifyAudioPlaybackChanged(aReason);
  }
  // The listener starts on first audibility (or immediately for PiP/fullscreen
  // controllable sources and all non-controllable sources). It stops only when
  // the element is fully stopped. Paused media produces no sound so we treat
  // it as inaudible here.
  const bool isAudible = !mPaused && IsAudible();
  if (isAudible && !mMediaControlKeyListener->IsStarted()) {
    StartMediaControlKeyListenerIfNeeded();
  }
  mMediaControlKeyListener->UpdateMediaAudibleState(isAudible);
  // only request wake lock for audible media.
  UpdateWakeLock();
}

void HTMLMediaElement::SetMediaInfo(const MediaInfo& aInfo) {
  const bool oldHasAudio = mMediaInfo.HasAudio();
  mMediaInfo = aInfo;
  if ((aInfo.HasAudio() != oldHasAudio) && mResumeDelayedPlaybackAgent) {
    mResumeDelayedPlaybackAgent->UpdateAudibleState(this, IsAudible());
  }
  nsILoadContext* loadContext = OwnerDoc()->GetLoadContext();
  if (HasAudio() && loadContext && !loadContext->UsePrivateBrowsing()) {
    mTitleChangeObserver->Subscribe();
    UpdateStreamName();
  } else {
    mTitleChangeObserver->Unsubscribe();
  }
  if (mAudioChannelWrapper) {
    mAudioChannelWrapper->AudioCaptureTrackChangeIfNeeded();
  }
  UpdateWakeLock();
}

MediaInfo HTMLMediaElement::GetMediaInfo() const { return mMediaInfo; }

FrameStatistics* HTMLMediaElement::GetFrameStatistics() const {
  return mDecoder ? &(mDecoder->GetFrameStatistics()) : nullptr;
}

void HTMLMediaElement::DispatchAsyncTestingEvent(const nsAString& aName) {
  if (!StaticPrefs::media_testing_only_events()) {
    return;
  }
  QueueEvent(aName);
}

void HTMLMediaElement::AudioCaptureTrackChange(bool aCapture) {
  // No need to capture a silent media element.
  if (!HasAudio()) {
    return;
  }

  LOG(LogLevel::Debug,
      ("{} AudioCaptureTrackChange={}", fmt::ptr(this), aCapture));

  if (aCapture && !mStreamWindowCapturer) {
    nsPIDOMWindowInner* window = OwnerDoc()->GetInnerWindow();
    if (!window) {
      return;
    }

    mozilla::glean::media::capture_stream_usage
        .EnumGet(mozilla::glean::media::CaptureStreamUsageLabel::
                     eWindowaudiocaptured)
        .Add();
    MediaTrackGraph* mtg = MediaTrackGraph::GetInstance(
        MediaTrackGraph::AUDIO_THREAD_DRIVER, window,
        MediaTrackGraph::REQUEST_DEFAULT_SAMPLE_RATE,
        MediaTrackGraph::DEFAULT_OUTPUT_DEVICE);
    RefPtr<DOMMediaStream> stream = CaptureStreamInternal(
        StreamCaptureBehavior::CONTINUE_WHEN_ENDED,
        StreamCaptureType::CAPTURE_AUDIO, AudioOutputConfig::Needed, mtg);
    mStreamWindowCapturer =
        new MediaStreamWindowCapturer(stream, window->WindowID());
    mStreamWindowCapturer->mStream->RegisterTrackListener(
        mStreamWindowCapturer);
  } else if (!aCapture && mStreamWindowCapturer) {
    for (size_t i = 0; i < mOutputStreams.Length(); i++) {
      if (mOutputStreams[i].mStream == mStreamWindowCapturer->mStream) {
        // We own this MediaStream, it is not exposed to JS.
        AutoTArray<RefPtr<MediaStreamTrack>, 2> tracks;
        mStreamWindowCapturer->mStream->GetTracks(tracks);
        for (auto& track : tracks) {
          track->Stop();
        }
        mOutputStreams.RemoveElementAt(i);
        break;
      }
    }

    mStreamWindowCapturer->mStream->UnregisterTrackListener(
        mStreamWindowCapturer);
    mStreamWindowCapturer = nullptr;
    if (mOutputStreams.IsEmpty()) {
      mTracksCaptured = nullptr;
    }
  }
}

void HTMLMediaElement::NotifyCueDisplayStatesChanged() {
  if (!mTextTrackManager) {
    return;
  }

  mTextTrackManager->DispatchUpdateCueDisplay();
}

void HTMLMediaElement::LogVisibility(CallerAPI aAPI) {
  const bool isVisible = mVisibilityState == Visibility::ApproximatelyVisible;

  LOG(LogLevel::Debug, ("{} visibility = {}, API: '{}' and 'All'",
                        fmt::ptr(this), isVisible, static_cast<int>(aAPI)));

  if (!isVisible) {
    LOG(LogLevel::Debug, ("{} inTree = {}, API: '{}' and 'All'", fmt::ptr(this),
                          IsInComposedDoc(), static_cast<int>(aAPI)));
  }
}

void HTMLMediaElement::UpdateCustomPolicyAfterPlayed() {
  if (mAudioChannelWrapper) {
    mAudioChannelWrapper->NotifyPlayStateChanged();
  }
}

nsTArray<RefPtr<PlayPromise>> HTMLMediaElement::TakePendingPlayPromises() {
  return std::move(mPendingPlayPromises);
}

void HTMLMediaElement::NotifyAboutPlaying() {
  // Stick to the QueueEvent() call path for now because we want to
  // trigger some telemetry-related codes in the QueueEvent() method.
  QueueEvent(u"playing"_ns);
  StartMediaControlKeyListenerIfNeeded();
}

already_AddRefed<PlayPromise> HTMLMediaElement::CreatePlayPromise(
    ErrorResult& aRv) const {
  nsPIDOMWindowInner* win = OwnerDoc()->GetInnerWindow();

  if (!win) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  RefPtr<PlayPromise> promise = PlayPromise::Create(win->AsGlobal(), aRv);
  LOG(LogLevel::Debug,
      ("{} created PlayPromise {}", fmt::ptr(this), fmt::ptr(promise.get())));

  return promise.forget();
}

already_AddRefed<Promise> HTMLMediaElement::CreateDOMPromise(
    ErrorResult& aRv) const {
  nsPIDOMWindowInner* win = OwnerDoc()->GetInnerWindow();

  if (!win) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  return Promise::Create(win->AsGlobal(), aRv);
}

void HTMLMediaElement::AsyncResolvePendingPlayPromises() {
  if (mShuttingDown) {
    return;
  }

  nsCOMPtr<nsIRunnable> event = new nsResolveOrRejectPendingPlayPromisesRunner(
      this, TakePendingPlayPromises());

  GetMainThreadSerialEventTarget()->Dispatch(event.forget());
}

void HTMLMediaElement::AsyncRejectPendingPlayPromises(nsresult aError) {
  if (!mPaused) {
    mPaused = true;
    UpdatePlaybackPseudoClasses();
    QueueEvent(u"pause"_ns);
  }

  if (mShuttingDown) {
    return;
  }

  if (aError == NS_ERROR_DOM_MEDIA_NOT_ALLOWED_ERR) {
    DispatchEventsWhenPlayWasNotAllowed();
  }

  nsCOMPtr<nsIRunnable> event = new nsResolveOrRejectPendingPlayPromisesRunner(
      this, TakePendingPlayPromises(), aError);

  GetMainThreadSerialEventTarget()->Dispatch(event.forget());
}

void HTMLMediaElement::GetEMEInfo(dom::EMEDebugInfo& aInfo) {
  MOZ_ASSERT(NS_IsMainThread(),
             "MediaKeys expects to be interacted with on main thread!");
  if (!mMediaKeys) {
    return;
  }
  mMediaKeys->GetKeySystem(aInfo.mKeySystem);
  mMediaKeys->GetSessionsInfo(aInfo.mSessionsInfo);
}

void HTMLMediaElement::NotifyDecoderActivityChanges() const {
  if (mDecoder) {
    mDecoder->NotifyOwnerActivityChanged(
        IsActuallyInvisible(), IsInComposedDoc(),
        OwnerDoc()->IsInBackgroundWindow(), HasPendingCallbacks());
  }
}

Document* HTMLMediaElement::GetDocument() const { return OwnerDoc(); }

bool HTMLMediaElement::IsAudible() const {
  // No audio track.
  if (!HasAudio()) {
    return false;
  }

  // Muted or the volume should not be ~0
  if (mMuted || (std::fabs(Volume()) <= 1e-7)) {
    return false;
  }

  return mIsAudioTrackAudible;
}

Maybe<nsAutoString> HTMLMediaElement::GetKeySystem() const {
  if (!mMediaKeys) {
    return Nothing();
  }
  nsAutoString keySystem;
  mMediaKeys->GetKeySystem(keySystem);
  return Some(keySystem);
}

void HTMLMediaElement::ConstructMediaTracks(const MediaInfo* aInfo) {
  if (!aInfo) {
    return;
  }

  AudioTrackList* audioList = AudioTracks();
  if (audioList && aInfo->HasAudio()) {
    LOG(LogLevel::Debug,
        ("{} ConstructMediaTracks, add an audio track", fmt::ptr(this)));
    const TrackInfo& info = aInfo->mAudio;
    RefPtr<AudioTrack> track = MediaTrackList::CreateAudioTrack(
        audioList->GetRelevantGlobal(), info.mId, info.mKind, info.mLabel,
        info.mLanguage, info.mEnabled);

    audioList->AddTrack(track);
  }

  VideoTrackList* videoList = VideoTracks();
  if (videoList && aInfo->HasVideo()) {
    LOG(LogLevel::Debug,
        ("{} ConstructMediaTracks, add a video track", fmt::ptr(this)));
    const TrackInfo& info = aInfo->mVideo;
    RefPtr<VideoTrack> track = MediaTrackList::CreateVideoTrack(
        videoList->GetRelevantGlobal(), info.mId, info.mKind, info.mLabel,
        info.mLanguage);

    videoList->AddTrack(track);
    track->SetEnabledInternal(info.mEnabled, MediaTrack::FIRE_NO_EVENTS);
  }
}

void HTMLMediaElement::RemoveMediaTracks() {
  if (mAudioTrackList) {
    mAudioTrackList->RemoveTracks();
  }
  if (mVideoTrackList) {
    mVideoTrackList->RemoveTracks();
  }
}

class MediaElementGMPCrashHelper : public GMPCrashHelper {
 public:
  explicit MediaElementGMPCrashHelper(HTMLMediaElement* aElement)
      : mElement(aElement) {
    MOZ_ASSERT(NS_IsMainThread());  // WeakPtr isn't thread safe.
  }
  already_AddRefed<nsPIDOMWindowInner> GetPluginCrashedEventTarget() override {
    MOZ_ASSERT(NS_IsMainThread());  // WeakPtr isn't thread safe.
    if (!mElement) {
      return nullptr;
    }
    return do_AddRef(mElement->OwnerDoc()->GetInnerWindow());
  }

 private:
  WeakPtr<HTMLMediaElement> mElement;
};

already_AddRefed<GMPCrashHelper> HTMLMediaElement::CreateGMPCrashHelper() {
  return MakeAndAddRef<MediaElementGMPCrashHelper>(this);
}

void HTMLMediaElement::MarkAsTainted() {
  mHasSuspendTaint = true;

  if (mDecoder) {
    mDecoder->SetSuspendTaint(true);
  }
}

already_AddRefed<Promise> HTMLMediaElement::SetSinkId(const nsAString& aSinkId,
                                                      ErrorResult& aRv) {
  LOG(LogLevel::Info, ("{}, setSinkId({})", fmt::ptr(this),
                       NS_ConvertUTF16toUTF8(aSinkId).get()));
  nsCOMPtr<nsPIDOMWindowInner> win = OwnerDoc()->GetInnerWindow();
  if (!win) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(win->AsGlobal(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  if (!PermissionsPolicyUtils::IsFeatureAllowed(win->GetExtantDoc(),
                                                u"speaker-selection"_ns)) {
    promise->MaybeRejectWithNotAllowedError(
        "Document's Permissions Policy does not allow setSinkId()");
  }

  if (mSink.first.Equals(aSinkId)) {
    promise->MaybeResolveWithUndefined();
    return promise.forget();
  }

  RefPtr<MediaDevices> mediaDevices = win->Navigator()->GetMediaDevices(aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  nsString sinkId(aSinkId);
  mediaDevices->GetSinkDevice(sinkId)
      ->Then(
          AbstractMainThread(), __func__,
          [self = RefPtr<HTMLMediaElement>(this),
           this](RefPtr<AudioDeviceInfo>&& aInfo) {
            // Sink found switch output device.
            MOZ_ASSERT(aInfo);
            if (mDecoder) {
              RefPtr<SinkInfoPromise> p = mDecoder->SetSink(aInfo)->Then(
                  AbstractMainThread(), __func__,
                  [aInfo](const GenericPromise::ResolveOrRejectValue& aValue) {
                    if (aValue.IsResolve()) {
                      return SinkInfoPromise::CreateAndResolve(aInfo, __func__);
                    }
                    return SinkInfoPromise::CreateAndReject(
                        aValue.RejectValue(), __func__);
                  });
              return p;
            }
            if (mSrcStream) {
              MOZ_ASSERT(mMediaStreamRenderer);
              RefPtr<SinkInfoPromise> p =
                  mMediaStreamRenderer->SetAudioOutputDevice(aInfo)->Then(
                      AbstractMainThread(), __func__,
                      [aInfo](
                          const GenericPromise::ResolveOrRejectValue& aValue) {
                        if (aValue.IsResolve()) {
                          return SinkInfoPromise::CreateAndResolve(aInfo,
                                                                   __func__);
                        }
                        return SinkInfoPromise::CreateAndReject(
                            aValue.RejectValue(), __func__);
                      });
              return p;
            }
            // No media attached to the element save it for later.
            return SinkInfoPromise::CreateAndResolve(aInfo, __func__);
          },
          [](nsresult res) {
            // Promise is rejected, sink not found.
            return SinkInfoPromise::CreateAndReject(res, __func__);
          })
      ->Then(AbstractMainThread(), __func__,
             [promise, self = RefPtr<HTMLMediaElement>(this), this,
              sinkId](const SinkInfoPromise::ResolveOrRejectValue& aValue) {
               if (aValue.IsResolve()) {
                 LOG(LogLevel::Info, ("{}, set sinkid={}", fmt::ptr(this),
                                      NS_ConvertUTF16toUTF8(sinkId).get()));
                 mSink = std::pair(sinkId, aValue.ResolveValue());
                 promise->MaybeResolveWithUndefined();
               } else {
                 switch (aValue.RejectValue()) {
                   case NS_ERROR_ABORT:
                     promise->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
                     break;
                   case NS_ERROR_NOT_AVAILABLE: {
                     promise->MaybeRejectWithNotFoundError(
                         "The object can not be found here.");
                     break;
                   }
                   default:
                     MOZ_ASSERT_UNREACHABLE("Invalid error.");
                 }
               }
             });

  aRv = NS_OK;
  return promise.forget();
}

void HTMLMediaElement::NotifyTextTrackModeChanged() {
  if (mPendingTextTrackChanged) {
    return;
  }
  mPendingTextTrackChanged = true;
  AbstractMainThread()->Dispatch(
      NS_NewRunnableFunction("HTMLMediaElement::NotifyTextTrackModeChanged",
                             [this, self = RefPtr<HTMLMediaElement>(this)]() {
                               mPendingTextTrackChanged = false;
                               if (!mTextTrackManager) {
                                 return;
                               }
                               GetTextTracks()->CreateAndDispatchChangeEvent();
                               // https://html.spec.whatwg.org/multipage/media.html#text-track-model:show-poster-flag
                               if (!mShowPoster) {
                                 mTextTrackManager->TimeMarchesOn();
                               }
                             }));
}

void HTMLMediaElement::CreateResumeDelayedMediaPlaybackAgentIfNeeded() {
  if (mResumeDelayedPlaybackAgent) {
    return;
  }
  mResumeDelayedPlaybackAgent =
      MediaPlaybackDelayPolicy::CreateResumeDelayedPlaybackAgent(this,
                                                                 IsAudible());
  if (!mResumeDelayedPlaybackAgent) {
    LOG(LogLevel::Debug,
        ("{} Failed to create a delayed playback agant", fmt::ptr(this)));
    return;
  }
  mResumeDelayedPlaybackAgent->GetResumePromise()
      ->Then(
          AbstractMainThread(), __func__,
          [self = RefPtr<HTMLMediaElement>(this)]() {
            LOG(LogLevel::Debug,
                ("{} Resume delayed Play() call", fmt::ptr(self.get())));
            self->mResumePlaybackRequest.Complete();
            self->mResumeDelayedPlaybackAgent = nullptr;
            IgnoredErrorResult dummy;
            RefPtr<Promise> toBeIgnored = self->Play(dummy);
          },
          [self = RefPtr<HTMLMediaElement>(this)]() {
            LOG(LogLevel::Debug, ("{} Can not resume delayed Play() call",
                                  fmt::ptr(self.get())));
            self->mResumePlaybackRequest.Complete();
            self->mResumeDelayedPlaybackAgent = nullptr;
          })
      ->Track(mResumePlaybackRequest);
}

void HTMLMediaElement::ClearResumeDelayedMediaPlaybackAgentIfNeeded() {
  if (mResumeDelayedPlaybackAgent) {
    mResumePlaybackRequest.DisconnectIfExists();
    mResumeDelayedPlaybackAgent = nullptr;
  }
}

void HTMLMediaElement::NotifyMediaControlPlaybackStateChanged() {
  if (!mMediaControlKeyListener->IsStarted()) {
    return;
  }
  if (mPaused) {
    mMediaControlKeyListener->NotifyMediaStoppedPlaying();
  } else {
    mMediaControlKeyListener->NotifyMediaStartedPlaying();
  }
}

bool HTMLMediaElement::IsInFullScreen() const {
  return State().HasState(ElementState::FULLSCREEN);
}

bool HTMLMediaElement::IsPlayable() const {
  return (mDecoder || mSrcStream) && !HasError();
}

bool HTMLMediaElement::IsControllableMediaSource() const {
  if (!IsPlayable()) {
    MEDIACONTROL_LOG("Uncontrollable: media is not playable");
    return false;
  }

  if (mSrcStream) {
    MEDIACONTROL_LOG("Uncontrollable: real-time stream source");
    return false;
  }

  if (IsBeingUsedInPictureInPictureMode()) {
    MEDIACONTROL_LOG("Controllable: media is in picture-in-picture mode");
    return true;
  }

  if (IsInFullScreen()) {
    MEDIACONTROL_LOG("Controllable: media is in fullscreen");
    return true;
  }

  if (mDecoder && mDecoder->IsLiveStream()) {
    MEDIACONTROL_LOG("Controllable: live stream");
    return true;
  }

  // Short sounds (e.g. notifications) are excluded to avoid triggering the OS
  // media-control interface for them.
  const bool meetsThreshold =
      Duration() >= StaticPrefs::media_mediacontrol_eligible_media_duration_s();
  MEDIACONTROL_LOG("{}: duration {} vs threshold",
                   meetsThreshold ? "Controllable" : "Uncontrollable",
                   Duration());
  return meetsThreshold;
}

void HTMLMediaElement::StartMediaControlKeyListenerIfNeeded() {
  if (!IsPlayable()) {
    return;
  }
  // For controllable sources, delay starting until the media is audible (or in
  // PiP/fullscreen) so we don't register silent media with the controller.
  // ComputedVolume() catches the cases IsAudible() misses, e.g. the tab being
  // silenced by an audio-channel volume factor of zero without an explicit
  // mute.
  if (IsControllableMediaSource() &&
      (!IsAudible() || ComputedVolume() == 0.0f) &&
      !IsBeingUsedInPictureInPictureMode() && !IsInFullScreen()) {
    MEDIACONTROL_LOG("Delay starting: controllable source not yet audible");
    return;
  }
  mMediaControlKeyListener->Start();
}

void HTMLMediaElement::UpdateStreamName() {
  MOZ_ASSERT(NS_IsMainThread());

  nsAutoString aTitle;
  OwnerDoc()->GetTitle(aTitle);

  if (mDecoder) {
    mDecoder->SetStreamName(aTitle);
  }
}

void HTMLMediaElement::SetSecondaryMediaStreamRenderer(
    VideoFrameContainer* aContainer,
    FirstFrameVideoOutput* aFirstFrameOutput /* = nullptr */) {
  MOZ_ASSERT(mSrcStream);
  MOZ_ASSERT(mMediaStreamRenderer);
  if (mSecondaryMediaStreamRenderer) {
    mSecondaryMediaStreamRenderer->Shutdown();
    mSecondaryMediaStreamRenderer = nullptr;
  }
  if (aContainer) {
    mSecondaryMediaStreamRenderer = MakeAndAddRef<MediaStreamRenderer>(
        AbstractMainThread(), aContainer, aFirstFrameOutput, this);
    if (mSrcStreamIsPlaying) {
      mSecondaryMediaStreamRenderer->Start();
    }
    if (mSelectedVideoStreamTrack) {
      mSecondaryMediaStreamRenderer->AddTrack(mSelectedVideoStreamTrack);
    }
  }
}

void HTMLMediaElement::UpdateMediaControlAfterPictureInPictureModeChanged() {
  if (IsBeingUsedInPictureInPictureMode()) {
    // When media enters PIP mode, we should ensure that the listener has been
    // started because we always want to control PIP video.
    StartMediaControlKeyListenerIfNeeded();
    if (!mMediaControlKeyListener->IsStarted()) {
      MEDIACONTROL_LOG("Failed to start listener when entering PIP mode");
    }
    // Updating controller PIP state no matter the listener starts or not.
    mMediaControlKeyListener->SetPictureInPictureModeEnabled(true);
  } else {
    mMediaControlKeyListener->SetPictureInPictureModeEnabled(false);
  }
}

bool HTMLMediaElement::IsBeingUsedInPictureInPictureMode() const {
  if (!IsVideo()) {
    return false;
  }
  return static_cast<const HTMLVideoElement*>(this)->IsCloningElementVisually();
}

void HTMLMediaElement::NodeInfoChanged(Document* aOldDoc) {
  if (mMediaSource) {
    OwnerDoc()->AddMediaElementWithMSE();
    aOldDoc->RemoveMediaElementWithMSE();
  }

  StopLazyLoading(StartLoad::No);
  if (LoadingState() == Loading::Lazy) {
    SetLazyLoading();
  }

  nsGenericHTMLElement::NodeInfoChanged(aOldDoc);
}

void HTMLMediaElement::MaybeMarkSHEntryAsUserInteracted() {
  if (media::AutoplayPolicy::GetAutoplayPolicy(*this) ==
      dom::AutoplayPolicy::Allowed) {
    // Only mark entries when autoplay is allowed for both audio and video,
    // i.e. when AutoplayPolicy is not Blocked or Allowed_muted.
    OwnerDoc()->SetSHEntryHasUserInteraction(true);
  }
}

#ifdef MOZ_WMF_CDM
bool HTMLMediaElement::IsUsingWMFCDM() const { return mIsUsingWMFCDM; };

CDMProxy* HTMLMediaElement::GetCDMProxy() const {
  return mMediaKeys ? mMediaKeys->GetCDMProxy() : nullptr;
};
#endif

}  // namespace mozilla::dom

#undef LOG
#undef LOG_EVENT
