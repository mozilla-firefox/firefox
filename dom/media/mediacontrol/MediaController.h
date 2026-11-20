/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_MEDIACONTROL_MEDIACONTROLLER_H_
#define DOM_MEDIA_MEDIACONTROL_MEDIACONTROLLER_H_

#include "AudioSessionManager.h"
#include "AudioSessionRecord.h"
#include "MediaEventSource.h"
#include "MediaPlaybackStatus.h"
#include "MediaStatusManager.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/LinkedList.h"
#include "mozilla/dom/AudioSessionBinding.h"
#include "mozilla/dom/MediaControllerBinding.h"
#include "mozilla/dom/MediaSession.h"
#include "nsISupportsImpl.h"
#include "nsITimer.h"

namespace mozilla::dom {

class BrowsingContext;
enum class AudioFocusInterruptAction : uint8_t;

/**
 * IMediaController is an interface which includes control related methods and
 * methods used to know its playback state.
 */
class IMediaController {
 public:
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  // Focus the window currently playing media.
  virtual void Focus() = 0;
  virtual void Play() = 0;
  virtual void Pause() = 0;
  virtual void Stop() = 0;
  virtual void PrevTrack() = 0;
  virtual void NextTrack() = 0;
  virtual void SeekBackward(double aSeekOffset) = 0;
  virtual void SeekForward(double aSeekOffset) = 0;
  virtual void SkipAd() = 0;
  virtual void SeekTo(double aSeekTime, bool aFastSeek) = 0;
  virtual void SetVolume(double aVolume) = 0;
  virtual void Mute() = 0;
  virtual void Unmute() = 0;

  // Return the ID of the top level browsing context within a tab.
  virtual uint64_t Id() const = 0;
  virtual bool IsAudible() const = 0;
  virtual bool IsPlaying() const = 0;
  virtual bool IsActive() const = 0;
};

/**
 * MediaController is a class, which is used to control all media within a tab.
 * It can only be used in Chrome process and the controlled media are usually
 * in the content process (unless we disable e10s).
 *
 * Each tab would have only one media controller, they are 1-1 corresponding
 * relationship, we use tab's top-level browsing context ID to initialize the
 * controller and use that as its ID.
 *
 * The controller would be activated when its controlled media starts and
 * becomes audible. After the controller is activated, then we can use its
 * controlling methods, such as `Play()`, `Pause()` to control the media within
 * the tab.
 *
 * If there is at least one controlled media playing in the tab, then we would
 * say the controller is `playing`. If there is at least one controlled media is
 * playing and audible, then we would say the controller is `audible`.
 *
 * Note that, if we don't enable audio competition, then we might have multiple
 * tabs playing media at the same time, we can use the ID to query the specific
 * controller from `MediaControlService`.
 */
class MediaController final : public DOMEventTargetHelper,
                              public IMediaController,
                              public LinkedListElement<RefPtr<MediaController>>,
                              public MediaStatusManager,
                              public nsITimerCallback,
                              public nsINamed {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSITIMERCALLBACK
  NS_DECL_NSINAMED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(MediaController,
                                                         DOMEventTargetHelper)
  explicit MediaController(uint64_t aBrowsingContextId);

  // WebIDL methods
  nsISupports* GetParentObject() const;
  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;
  void GetSupportedKeys(nsTArray<MediaControlKey>& aRetVal) const;
  void GetMetadata(MediaMetadataInit& aMetadata, ErrorResult& aRv);
  IMPL_EVENT_HANDLER(activated);
  IMPL_EVENT_HANDLER(deactivated);
  IMPL_EVENT_HANDLER(audiblechange);
  IMPL_EVENT_HANDLER(effectiveaudiosessiontypechange);
  IMPL_EVENT_HANDLER(metadatachange);
  IMPL_EVENT_HANDLER(supportedkeyschange);
  IMPL_EVENT_HANDLER(playbackstatechange);
  IMPL_EVENT_HANDLER(positionstatechange);

  // IMediaController's methods
  void Focus() override;
  void Play() override;
  void Pause() override;
  void Stop() override;
  void PrevTrack() override;
  void NextTrack() override;
  void SeekBackward(double aSeekOffset) override;
  void SeekForward(double aSeekOffset) override;
  void SkipAd() override;
  void SeekTo(double aSeekTime, bool aFastSeek) override;
  void SetVolume(double aVolume) override;
  void Mute() override;
  void Unmute() override;

  // Chrome-only audio-focus-aware pause/resume (see MediaController.webidl).
  // These are distinct from Play()/Pause(): PauseWithReason with a "user"
  // reason is the ordinary user transport, but a "system-*" reason pauses for
  // an audio-focus loss and suspends even sources the user cannot control.
  // Resume() is the counterpart to that system pause: it revives only what a
  // transient interrupt suspended, and is not Play(), which is the user
  // starting playback.
  void PauseWithReason(AudioFocusLossReason aReason);
  void Resume();

  uint64_t Id() const override;
  bool IsAudible() const override;
  bool IsPlaying() const override;
  bool IsActive() const override;
  bool IsMuted() const;

  // IMediaInfoUpdater's methods
  void NotifyMediaPlaybackChanged(uint64_t aBrowsingContextId,
                                  MediaPlaybackState aState) override;
  void NotifyMediaAudibleChanged(
      uint64_t aBrowsingContextId, MediaAudibleState aState,
      ControlType aType = ControlType::eControllable,
      AudioSessionType aSessionType = AudioSessionType::Playback) override;
  void EnableAction(uint64_t aBrowsingContextId,
                    MediaSessionAction aAction) override;
  void NotifySessionDestroyed(uint64_t aBrowsingContextId) override;
  void NotifyBrowsingContextDiscarded(uint64_t aBrowsingContextId) override;
  void SetIsInPictureInPictureMode(uint64_t aBrowsingContextId,
                                   bool aIsInPictureInPictureMode) override;
  void NotifyMediaFullScreenState(uint64_t aBrowsingContextId,
                                  bool aIsInFullScreen) override;

  // Calling this method explicitly would mark this controller as deprecated,
  // then calling any its method won't take any effect.
  void Shutdown();

  // This event would be notified media controller's supported media keys
  // change.
  MediaEventSource<nsTArray<MediaControlKey>>& SupportedKeysChangedEvent() {
    return mSupportedKeysChangedEvent;
  }

  MediaEventSource<bool>& FullScreenChangedEvent() {
    return mFullScreenChangedEvent;
  }

  MediaEventSource<bool>& PictureInPictureModeChangedEvent() {
    return mPictureInPictureModeChangedEvent;
  }

  CopyableTArray<MediaControlKey> GetSupportedMediaKeys() const;

  bool IsBeingUsedInPIPModeOrFullscreen() const;

  // These methods are used to select/unselect the media controller as a main
  // controller.
  void Select() const;
  void Unselect() const;

  // Record the override the user set on the given browsing context.
  // `Auto` means the user wants no explicit override.
  void SetAudioSessionTypeOverride(uint64_t aBrowsingContextId,
                                   AudioSessionType aType);

  // Forget any per-AudioSession state stored for the given browsing context.
  void ClearAudioSessionFor(uint64_t aBrowsingContextId);

  // Interrupt or restore the tab's selected audio session on an audio-focus
  // change. aKind distinguishes a non-resumable loss from a transient one.
  void InterruptAudioSession(AudioSessionInterruptKind aKind);
  void RestoreAudioSession();

  // The audio-session type the tab is currently exposing to chrome
  // consumers. Returns Auto when the tab is producing no audio.
  AudioSessionType GetEffectiveAudioSessionType() const;

  // Test-only accessor for the per-browsing-context AudioSession record.
  // Returns nullptr when no record exists.
  const AudioSessionRecord* GetAudioSessionRecordForTesting(
      uint64_t aBrowsingContextId) const;

  // Test-only accessor for the AudioSessionManager that owns this tab's
  // parent-side AudioSession spec state.
  const AudioSessionManager* GetAudioSessionManagerForTesting() const;

 private:
  friend class AudioSessionManager;

  ~MediaController();
  void HandleActualPlaybackStateChanged();
  void UpdateMediaControlActionToContentMediaIfNeeded(
      const MediaControlAction& aAction);
  // Dispatch an audio-focus interrupt (suspend or resume) to every browsing
  // context in the tab so uncontrollable receivers can react even on an
  // inactive controller.
  void UpdateMediaSessionInterruptToContentMediaIfNeeded(
      AudioFocusInterruptAction aAction);
  void HandleSupportedMediaSessionActionsChanged(
      const nsTArray<MediaSessionAction>& aSupportedAction);

  void HandlePositionStateChanged(const Maybe<PositionState>& aState);
  void HandleMetadataChanged(const MediaMetadataBase& aMetadata);

  // This would register controller to the media control service that takes a
  // responsibility to manage all active controllers.
  void Activate();

  // This would unregister controller from the media control service.
  void Deactivate();

  void UpdateActivatedStateIfNeeded();
  bool ShouldActivateController() const;
  bool ShouldDeactivateController() const;

  void UpdateDeactivationTimerIfNeeded();

  void DispatchAsyncEvent(const nsAString& aName);
  void DispatchAsyncEvent(already_AddRefed<Event> aEvent);

  bool IsMainController() const;
  void ForceToBecomeMainControllerIfNeeded();
  bool ShouldRequestForMainController() const;

  bool ShouldPropagateActionToAllContexts(
      const MediaControlAction& aAction) const;

  bool mIsActive = false;
  bool mShutdown = false;
  bool mIsMuted = false;
  bool mIsInPictureInPictureMode = false;
  bool mIsInFullScreenMode = false;

  // We would monitor the change of media session actions and convert them to
  // the media keys, then determine the supported media keys.
  MediaEventListener mSupportedActionsChangedListener;
  MediaEventProducer<nsTArray<MediaControlKey>> mSupportedKeysChangedEvent;

  MediaEventListener mPlaybackChangedListener;
  MediaEventListener mPositionStateChangedListener;
  MediaEventListener mMetadataChangedListener;

  MediaEventProducer<bool> mFullScreenChangedEvent;
  MediaEventProducer<bool> mPictureInPictureModeChangedEvent;
  // Use copyable array so that we can use the result as a parameter for the
  // media event.
  CopyableTArray<MediaControlKey> mSupportedKeys;
  // Timer to deactivate the controller if the time of being paused exceeds the
  // threshold of time.
  nsCOMPtr<nsITimer> mDeactivationTimer;

  // Owns parent-side AudioSession spec state and algorithms for this tab.
  AudioSessionManager mAudioSessionManager;
};

}  // namespace mozilla::dom

#endif
