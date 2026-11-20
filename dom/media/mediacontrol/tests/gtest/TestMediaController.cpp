/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaControlService.h"
#include "MediaController.h"
#include "gtest/gtest.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/MediaSessionBinding.h"

using namespace mozilla::dom;

#define CONTROLLER_ID 0
#define FAKE_CONTEXT_ID 0

#define FIRST_CONTROLLER_ID 0

TEST(MediaController, DefaultValueCheck)
{
  RefPtr<MediaController> controller = new MediaController(CONTROLLER_ID);
  ASSERT_TRUE(!controller->IsAnyMediaBeingControlled());
  ASSERT_TRUE(controller->Id() == CONTROLLER_ID);
  ASSERT_TRUE(controller->PlaybackState() == MediaSessionPlaybackState::None);
  ASSERT_TRUE(!controller->IsAudible());
}

TEST(MediaController, IsAnyMediaBeingControlled)
{
  RefPtr<MediaController> controller = new MediaController(CONTROLLER_ID);
  ASSERT_TRUE(!controller->IsAnyMediaBeingControlled());

  controller->NotifyMediaPlaybackChanged(FAKE_CONTEXT_ID,
                                         MediaPlaybackState::eStarted);
  ASSERT_TRUE(controller->IsAnyMediaBeingControlled());

  controller->NotifyMediaPlaybackChanged(FAKE_CONTEXT_ID,
                                         MediaPlaybackState::eStarted);
  ASSERT_TRUE(controller->IsAnyMediaBeingControlled());

  controller->NotifyMediaPlaybackChanged(FAKE_CONTEXT_ID,
                                         MediaPlaybackState::eStopped);
  ASSERT_TRUE(controller->IsAnyMediaBeingControlled());

  controller->NotifyMediaPlaybackChanged(FAKE_CONTEXT_ID,
                                         MediaPlaybackState::eStopped);
  ASSERT_TRUE(!controller->IsAnyMediaBeingControlled());
}

class FakeControlledMedia final {
 public:
  explicit FakeControlledMedia(MediaController* aController)
      : mController(aController) {
    mController->NotifyMediaPlaybackChanged(FAKE_CONTEXT_ID,
                                            MediaPlaybackState::eStarted);
  }

  void SetPlaying(MediaPlaybackState aState) {
    if (mPlaybackState == aState) {
      return;
    }
    mController->NotifyMediaPlaybackChanged(FAKE_CONTEXT_ID, aState);
    mPlaybackState = aState;
  }

  void SetAudible(MediaAudibleState aState) {
    if (mAudibleState == aState) {
      return;
    }
    mController->NotifyMediaAudibleChanged(FAKE_CONTEXT_ID, aState);
    mAudibleState = aState;
  }

  ~FakeControlledMedia() {
    if (mPlaybackState == MediaPlaybackState::ePlayed) {
      mController->NotifyMediaPlaybackChanged(FAKE_CONTEXT_ID,
                                              MediaPlaybackState::ePaused);
    }
    mController->NotifyMediaPlaybackChanged(FAKE_CONTEXT_ID,
                                            MediaPlaybackState::eStopped);
  }

 private:
  MediaPlaybackState mPlaybackState = MediaPlaybackState::eStopped;
  MediaAudibleState mAudibleState = MediaAudibleState::eInaudible;
  RefPtr<MediaController> mController;
};

TEST(MediaController, ActiveAndDeactiveController)
{
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);

  // In order to check active control number after FakeControlledMedia
  // destroyed.
  {
    FakeControlledMedia fakeMedia(controller);
    fakeMedia.SetPlaying(MediaPlaybackState::ePlayed);
    ASSERT_TRUE(service->GetActiveControllersNum() == 1);

    fakeMedia.SetAudible(MediaAudibleState::eAudible);
    ASSERT_TRUE(service->GetActiveControllersNum() == 1);

    fakeMedia.SetAudible(MediaAudibleState::eInaudible);
    ASSERT_TRUE(service->GetActiveControllersNum() == 1);
  }

  ASSERT_TRUE(service->GetActiveControllersNum() == 0);
}

TEST(MediaController, AudibleChanged)
{
  RefPtr<MediaController> controller = new MediaController(CONTROLLER_ID);

  FakeControlledMedia fakeMedia(controller);
  fakeMedia.SetPlaying(MediaPlaybackState::ePlayed);
  ASSERT_TRUE(!controller->IsAudible());

  fakeMedia.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(controller->IsAudible());

  fakeMedia.SetAudible(MediaAudibleState::eInaudible);
  ASSERT_TRUE(!controller->IsAudible());
}

TEST(MediaController, PlayingStateChangeViaControlledMedia)
{
  RefPtr<MediaController> controller = new MediaController(CONTROLLER_ID);

  // In order to check playing state after FakeControlledMedia destroyed.
  {
    FakeControlledMedia foo(controller);
    ASSERT_TRUE(controller->PlaybackState() == MediaSessionPlaybackState::None);

    foo.SetPlaying(MediaPlaybackState::ePlayed);
    ASSERT_TRUE(controller->PlaybackState() ==
                MediaSessionPlaybackState::Playing);

    foo.SetPlaying(MediaPlaybackState::ePaused);
    ASSERT_TRUE(controller->PlaybackState() ==
                MediaSessionPlaybackState::Paused);

    foo.SetPlaying(MediaPlaybackState::ePlayed);
    ASSERT_TRUE(controller->PlaybackState() ==
                MediaSessionPlaybackState::Playing);
  }

  // FakeControlledMedia has been destroyed, no playing media exists.
  ASSERT_TRUE(controller->PlaybackState() == MediaSessionPlaybackState::Paused);
}

TEST(MediaController, ControllerShouldRemainPlayingIfAnyPlayingMediaExists)
{
  RefPtr<MediaController> controller = new MediaController(CONTROLLER_ID);

  {
    FakeControlledMedia foo(controller);
    ASSERT_TRUE(controller->PlaybackState() == MediaSessionPlaybackState::None);

    foo.SetPlaying(MediaPlaybackState::ePlayed);
    ASSERT_TRUE(controller->PlaybackState() ==
                MediaSessionPlaybackState::Playing);

    // foo is playing, so controller is in `playing` state.
    FakeControlledMedia bar(controller);
    ASSERT_TRUE(controller->PlaybackState() ==
                MediaSessionPlaybackState::Playing);

    bar.SetPlaying(MediaPlaybackState::ePlayed);
    ASSERT_TRUE(controller->PlaybackState() ==
                MediaSessionPlaybackState::Playing);

    // Although we paused bar, but foo is still playing, so the controller would
    // still be in `playing`.
    bar.SetPlaying(MediaPlaybackState::ePaused);
    ASSERT_TRUE(controller->PlaybackState() ==
                MediaSessionPlaybackState::Playing);

    foo.SetPlaying(MediaPlaybackState::ePaused);
    ASSERT_TRUE(controller->PlaybackState() ==
                MediaSessionPlaybackState::Paused);
  }

  // both foo and bar have been destroyed, no playing media exists.
  ASSERT_TRUE(controller->PlaybackState() == MediaSessionPlaybackState::Paused);
}

TEST(MediaController, PictureInPictureModeOrFullscreen)
{
  RefPtr<MediaController> controller = new MediaController(CONTROLLER_ID);
  ASSERT_TRUE(!controller->IsBeingUsedInPIPModeOrFullscreen());

  controller->SetIsInPictureInPictureMode(FAKE_CONTEXT_ID, true);
  ASSERT_TRUE(controller->IsBeingUsedInPIPModeOrFullscreen());

  controller->SetIsInPictureInPictureMode(FAKE_CONTEXT_ID, false);
  ASSERT_TRUE(!controller->IsBeingUsedInPIPModeOrFullscreen());

  controller->NotifyMediaFullScreenState(FAKE_CONTEXT_ID, true);
  ASSERT_TRUE(controller->IsBeingUsedInPIPModeOrFullscreen());

  controller->NotifyMediaFullScreenState(FAKE_CONTEXT_ID, false);
  ASSERT_TRUE(!controller->IsBeingUsedInPIPModeOrFullscreen());
}

// Helper that simulates an uncontrollable source (e.g. Web Audio, Web Speech)
// reporting its audibility lifecycle to the MediaController. The destructor
// drops audibility automatically so individual tests don't have to do it.
class FakeUncontrollableSource final {
 public:
  explicit FakeUncontrollableSource(MediaController* aController,
                                    uint64_t aContextId = FAKE_CONTEXT_ID)
      : mController(aController), mContextId(aContextId) {}

  void SetAudible(MediaAudibleState aState) {
    if (mAudibleState == aState) {
      return;
    }
    mController->NotifyMediaAudibleChanged(mContextId, aState,
                                           ControlType::eUncontrollable);
    mAudibleState = aState;
  }

  ~FakeUncontrollableSource() {
    if (mAudibleState == MediaAudibleState::eAudible) {
      mController->NotifyMediaAudibleChanged(mContextId,
                                             MediaAudibleState::eInaudible,
                                             ControlType::eUncontrollable);
    }
  }

 private:
  MediaAudibleState mAudibleState = MediaAudibleState::eInaudible;
  RefPtr<MediaController> mController;
  uint64_t mContextId;
};

TEST(MediaController, UncontrollableSourceAudible)
{
  RefPtr<MediaController> controller = new MediaController(CONTROLLER_ID);
  ASSERT_FALSE(controller->IsAudible());

  FakeUncontrollableSource src(controller);
  src.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(controller->IsAudible());

  src.SetAudible(MediaAudibleState::eInaudible);
  ASSERT_FALSE(controller->IsAudible());
}

// Audible Web Audio with no Media Session at all must never activate the
// controller.
TEST(MediaController, UncontrollableSourceDoesNotActivateController)
{
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  {
    FakeUncontrollableSource src(controller);
    ASSERT_TRUE(service->GetActiveControllersNum() == 0);
    src.SetAudible(MediaAudibleState::eAudible);
    ASSERT_TRUE(service->GetActiveControllersNum() == 0);
  }
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);
}

TEST(MediaController, UncontrollableAudibleCombinedWithControllable)
{
  RefPtr<MediaController> controller = new MediaController(CONTROLLER_ID);

  FakeControlledMedia controlSrc(controller);
  controlSrc.SetPlaying(MediaPlaybackState::ePlayed);
  ASSERT_FALSE(controller->IsAudible());

  FakeUncontrollableSource uncontrolSrc(controller);
  uncontrolSrc.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(controller->IsAudible());

  // Both sources audible: controllable becomes inaudible, controller must
  // still report audible because the uncontrollable source is still audible.
  controlSrc.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(controller->IsAudible());
  controlSrc.SetAudible(MediaAudibleState::eInaudible);
  ASSERT_TRUE(controller->IsAudible());

  // Now the uncontrollable source goes inaudible too: controller is silent.
  uncontrolSrc.SetAudible(MediaAudibleState::eInaudible);
  ASSERT_FALSE(controller->IsAudible());

  // And bringing only the controllable back is enough on its own.
  controlSrc.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(controller->IsAudible());

  // Drop audibility before scope exit; FakeControlledMedia's destructor only
  // emits ePaused/eStopped, and MediaPlaybackStatus asserts the source is
  // inaudible by the time it is destroyed.
  controlSrc.SetAudible(MediaAudibleState::eInaudible);
}

TEST(MediaController, MultipleUncontrollableSources)
{
  RefPtr<MediaController> controller = new MediaController(CONTROLLER_ID);
  ASSERT_FALSE(controller->IsAudible());

  {
    FakeUncontrollableSource src1(controller, 1);
    FakeUncontrollableSource src2(controller, 2);

    src1.SetAudible(MediaAudibleState::eAudible);
    ASSERT_TRUE(controller->IsAudible());

    src2.SetAudible(MediaAudibleState::eAudible);
    ASSERT_TRUE(controller->IsAudible());

    src1.SetAudible(MediaAudibleState::eInaudible);
    ASSERT_TRUE(controller->IsAudible());

    src2.SetAudible(MediaAudibleState::eInaudible);
    ASSERT_FALSE(controller->IsAudible());
  }
  ASSERT_FALSE(controller->IsAudible());
}

// The Web Audio activation behaviour the tests below cover is behind a pref
// that is only enabled by default on Nightly.
class AutoWebAudioActivationPref {
 public:
  explicit AutoWebAudioActivationPref(bool aEnabled = true)
      : mOriginalValue(mozilla::Preferences::GetBool(kPref, false)) {
    mozilla::Preferences::SetBool(kPref, aEnabled);
  }
  ~AutoWebAudioActivationPref() {
    mozilla::Preferences::SetBool(kPref, mOriginalValue);
  }

 private:
  static constexpr const char* kPref =
      "media.mediacontrol.webaudio.activation.enabled";
  const bool mOriginalValue;
};

// A page can declare a Media Session and
// play audio purely through the Web Audio API, with no HTMLMediaElement
// anywhere. The controller must still activate once that session declares
// itself playing and the tab is actually audible.
TEST(MediaController, MediaSessionWithoutMediaElementActivatesController)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  controller->NotifySessionCreated(FAKE_CONTEXT_ID);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Play);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Pause);
  controller->SetDeclaredPlaybackState(FAKE_CONTEXT_ID,
                                       MediaSessionPlaybackState::Playing);
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  {
    FakeUncontrollableSource src(controller);
    src.SetAudible(MediaAudibleState::eAudible);
    ASSERT_TRUE(controller->PlaybackState() ==
                MediaSessionPlaybackState::Playing);
    ASSERT_TRUE(service->GetActiveControllersNum() == 1);
  }

  // Falling silent is the equivalent of pausing an HTMLMediaElement, which
  // stays controllable because the element still exists, so the controller
  // must survive it. Otherwise pausing from the platform's controls would
  // take those controls away and leave no way to resume.
  ASSERT_TRUE(service->GetActiveControllersNum() == 1);

  controller->Shutdown();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);
}

// A Web-Audio-only page
// that declares itself playing but never registers real `play`/`pause`
// action handlers must not activate the controller. Unlike an
// HTMLMediaElement, there is no default action to fall back to for such
// media, so the platform's play/pause keys would silently do nothing.
TEST(MediaController,
     MediaSessionWithoutActionHandlersDoesNotActivateController)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  controller->NotifySessionCreated(FAKE_CONTEXT_ID);
  controller->SetDeclaredPlaybackState(FAKE_CONTEXT_ID,
                                       MediaSessionPlaybackState::Playing);

  {
    FakeUncontrollableSource src(controller);
    src.SetAudible(MediaAudibleState::eAudible);
    ASSERT_TRUE(controller->PlaybackState() ==
                MediaSessionPlaybackState::Playing);
    ASSERT_TRUE(service->GetActiveControllersNum() == 0);
  }

  ASSERT_TRUE(service->GetActiveControllersNum() == 0);
  controller->Shutdown();
}

// A page can register
// its play/pause handlers after the tab is already audible and has declared
// itself playing, not only before. EnableAction() re-checks activation so
// this ordering isn't racy.
TEST(MediaController,
     MediaSessionActionHandlersRegisteredAfterAudibleStillActivateController)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  controller->NotifySessionCreated(FAKE_CONTEXT_ID);
  controller->SetDeclaredPlaybackState(FAKE_CONTEXT_ID,
                                       MediaSessionPlaybackState::Playing);

  {
    FakeUncontrollableSource src(controller);
    src.SetAudible(MediaAudibleState::eAudible);
    ASSERT_TRUE(service->GetActiveControllersNum() == 0);

    controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Play);
    ASSERT_TRUE(service->GetActiveControllersNum() == 0);

    controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Pause);
    ASSERT_TRUE(service->GetActiveControllersNum() == 1);
  }

  ASSERT_TRUE(service->GetActiveControllersNum() == 1);
  controller->Shutdown();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);
}

// A declared-playing session with real play/pause handlers must not
// activate the controller while the tab is inaudible.
TEST(MediaController, InaudibleWebAudioWithHandlersDoesNotActivateController)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  controller->NotifySessionCreated(FAKE_CONTEXT_ID);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Play);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Pause);
  controller->SetDeclaredPlaybackState(FAKE_CONTEXT_ID,
                                       MediaSessionPlaybackState::Playing);

  ASSERT_TRUE(service->GetActiveControllersNum() == 0);
  controller->Shutdown();
}

// A Web-Audio-only session must survive a pause/resume round trip. Pressing
// pause on the platform's controls makes the page fall silent, and if that
// tore the controller down there would be nothing left to receive the
// following play.
TEST(MediaController, WebAudioControllerSurvivesPauseAndResume)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  controller->NotifySessionCreated(FAKE_CONTEXT_ID);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Play);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Pause);
  controller->SetDeclaredPlaybackState(FAKE_CONTEXT_ID,
                                       MediaSessionPlaybackState::Playing);

  FakeUncontrollableSource src(controller);
  src.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(service->GetActiveControllersNum() == 1);

  controller->SetDeclaredPlaybackState(FAKE_CONTEXT_ID,
                                       MediaSessionPlaybackState::Paused);
  src.SetAudible(MediaAudibleState::eInaudible);
  ASSERT_TRUE(service->GetActiveControllersNum() == 1);

  controller->SetDeclaredPlaybackState(FAKE_CONTEXT_ID,
                                       MediaSessionPlaybackState::Playing);
  src.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(service->GetActiveControllersNum() == 1);
  ASSERT_TRUE(controller->PlaybackState() ==
              MediaSessionPlaybackState::Playing);

  src.SetAudible(MediaAudibleState::eInaudible);
  controller->Shutdown();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);
}

// A gapless or crossfading player dips to silence between tracks. That must
// not cycle the controller off and on, which would make the platform's
// controls flicker away and back.
TEST(MediaController, WebAudioControllerSurvivesSilenceBetweenTracks)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  controller->NotifySessionCreated(FAKE_CONTEXT_ID);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Play);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Pause);
  controller->SetDeclaredPlaybackState(FAKE_CONTEXT_ID,
                                       MediaSessionPlaybackState::Playing);

  FakeUncontrollableSource src(controller);
  src.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(service->GetActiveControllersNum() == 1);

  src.SetAudible(MediaAudibleState::eInaudible);
  ASSERT_TRUE(service->GetActiveControllersNum() == 1);

  src.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(service->GetActiveControllersNum() == 1);

  src.SetAudible(MediaAudibleState::eInaudible);
  controller->Shutdown();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);
}

// Destroying the media session removes the only thing keeping a
// Web-Audio-only controller alive, even while its audio keeps playing.
TEST(MediaController, WebAudioSessionDestroyedDeactivatesController)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  controller->NotifySessionCreated(FAKE_CONTEXT_ID);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Play);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Pause);
  controller->SetDeclaredPlaybackState(FAKE_CONTEXT_ID,
                                       MediaSessionPlaybackState::Playing);

  FakeUncontrollableSource src(controller);
  src.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(service->GetActiveControllersNum() == 1);

  controller->NotifySessionDestroyed(FAKE_CONTEXT_ID);
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  src.SetAudible(MediaAudibleState::eInaudible);
  controller->Shutdown();
}

// Discarding the browsing context takes the session with it. Controllable
// media reports a stop when it goes away, but an uncontrollable source does
// not, so the controller has to react to the discard itself.
TEST(MediaController, WebAudioBrowsingContextDiscardedDeactivatesController)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  controller->NotifySessionCreated(FAKE_CONTEXT_ID);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Play);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Pause);
  controller->SetDeclaredPlaybackState(FAKE_CONTEXT_ID,
                                       MediaSessionPlaybackState::Playing);

  {
    FakeUncontrollableSource src(controller);
    src.SetAudible(MediaAudibleState::eAudible);
    ASSERT_TRUE(service->GetActiveControllersNum() == 1);

    controller->NotifyBrowsingContextDiscarded(FAKE_CONTEXT_ID);
    ASSERT_TRUE(service->GetActiveControllersNum() == 0);
  }

  controller->Shutdown();
}

// Clearing the active media session changes which declared playback state
// feeds the actual playback state, so it has to be recomputed. Otherwise the
// controller keeps reporting that it is playing after the session that said so
// is gone, which feeds the deactivation timer and main controller selection.
TEST(MediaController, ClearingActiveSessionUpdatesActualPlaybackState)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  controller->NotifySessionCreated(FAKE_CONTEXT_ID);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Play);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Pause);
  controller->SetDeclaredPlaybackState(FAKE_CONTEXT_ID,
                                       MediaSessionPlaybackState::Playing);

  FakeUncontrollableSource src(controller);
  src.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(controller->IsPlaying());

  // Silence alone doesn't change it, because the session still declares
  // itself playing and a declared `playing` outranks the guessed state.
  src.SetAudible(MediaAudibleState::eInaudible);
  ASSERT_TRUE(controller->IsPlaying());

  // Destroying the session removes that declaration, so the actual state has
  // to fall back to the guessed one.
  controller->NotifySessionDestroyed(FAKE_CONTEXT_ID);
  ASSERT_TRUE(!controller->IsPlaying());

  controller->Shutdown();
}

// Audible Web Audio feeds the guessed playback state, so a page does not have
// to declare `playbackState` for its session to be exposed. The page-provided
// play/pause handlers remain the thing that distinguishes a player from a
// page that merely makes noise.
TEST(MediaController, WebAudioActivatesWithoutDeclaredPlaybackState)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  controller->NotifySessionCreated(FAKE_CONTEXT_ID);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Play);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Pause);

  FakeUncontrollableSource src(controller);
  src.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(controller->PlaybackState() ==
              MediaSessionPlaybackState::Playing);
  ASSERT_TRUE(service->GetActiveControllersNum() == 1);

  src.SetAudible(MediaAudibleState::eInaudible);
  controller->Shutdown();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);
}

// The counterpart: a page that produces audio through the Web Audio API and
// registers a session but no play/pause handlers is not a player. This is the
// case for games and other pages that only make sound effects.
TEST(MediaController, AudibleWebAudioWithoutHandlersIsNotAPlayer)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  controller->NotifySessionCreated(FAKE_CONTEXT_ID);

  FakeUncontrollableSource src(controller);
  src.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  src.SetAudible(MediaAudibleState::eInaudible);
  controller->Shutdown();
}

// A tab that has controllable media takes its playback state from that media,
// not from an unrelated audible source. Otherwise a page whose video has
// played and is now paused would report itself playing again as soon as a
// Web Audio sound effect ran.
TEST(MediaController, UncontrollableAudioDoesNotActivateForPausedElement)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  {
    FakeControlledMedia media(controller);
    ASSERT_TRUE(controller->IsAnyMediaBeingControlled());

    FakeUncontrollableSource src(controller);
    src.SetAudible(MediaAudibleState::eAudible);
    ASSERT_TRUE(service->GetActiveControllersNum() == 0);

    src.SetAudible(MediaAudibleState::eInaudible);
  }
  controller->Shutdown();
}

#define IFRAME_CONTEXT_ID 7

// A Web Audio player in the top-level page keeps playing while an iframe plays
// and then removes a media element of its own. Once that element is gone the
// player owns the tab's audio again, so its session has to be exposed again
// rather than waiting for an audibility change that never comes.
TEST(MediaController, SessionIsRestoredAfterControllableMediaIsRemoved)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  controller->NotifySessionCreated(FAKE_CONTEXT_ID);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Play);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Pause);

  FakeUncontrollableSource webAudio(controller, FAKE_CONTEXT_ID);
  webAudio.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(service->GetActiveControllersNum() == 1);
  ASSERT_TRUE(
      controller->IsActiveSessionActionSupported(MediaSessionAction::Play));

  controller->NotifyMediaPlaybackChanged(IFRAME_CONTEXT_ID,
                                         MediaPlaybackState::eStarted);
  controller->NotifyMediaPlaybackChanged(IFRAME_CONTEXT_ID,
                                         MediaPlaybackState::ePlayed);
  controller->NotifyMediaAudibleChanged(IFRAME_CONTEXT_ID,
                                        MediaAudibleState::eAudible,
                                        ControlType::eControllable);

  controller->NotifyMediaAudibleChanged(IFRAME_CONTEXT_ID,
                                        MediaAudibleState::eInaudible,
                                        ControlType::eControllable);
  controller->NotifyMediaPlaybackChanged(IFRAME_CONTEXT_ID,
                                         MediaPlaybackState::ePaused);
  controller->NotifyMediaPlaybackChanged(IFRAME_CONTEXT_ID,
                                         MediaPlaybackState::eStopped);

  ASSERT_TRUE(service->GetActiveControllersNum() == 1);
  ASSERT_TRUE(
      controller->IsActiveSessionActionSupported(MediaSessionAction::Play));
  ASSERT_TRUE(
      controller->IsActiveSessionActionSupported(MediaSessionAction::Pause));

  webAudio.SetAudible(MediaAudibleState::eInaudible);
  controller->Shutdown();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);
}

#define SECOND_SESSION_CONTEXT_ID 8

// Two contexts both have a session and both keep playing Web Audio. Destroying
// the active one has to hand over to the other, not simply clear the tab's
// session: with no controllable owner, nothing else re-derives this until the
// next audible change, which for audio that keeps playing never comes.
TEST(MediaController, DestroyingActiveSessionHandsOverToAnotherAudibleSession)
{
  AutoWebAudioActivationPref prefOn;
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  for (uint64_t id :
       {(uint64_t)FAKE_CONTEXT_ID, (uint64_t)SECOND_SESSION_CONTEXT_ID}) {
    controller->NotifySessionCreated(id);
    controller->EnableAction(id, MediaSessionAction::Play);
    controller->EnableAction(id, MediaSessionAction::Pause);
  }

  FakeUncontrollableSource first(controller, FAKE_CONTEXT_ID);
  FakeUncontrollableSource second(controller, SECOND_SESSION_CONTEXT_ID);
  first.SetAudible(MediaAudibleState::eAudible);
  second.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(service->GetActiveControllersNum() == 1);
  ASSERT_TRUE(
      controller->IsActiveSessionActionSupported(MediaSessionAction::Play));

  controller->NotifySessionDestroyed(FAKE_CONTEXT_ID);

  // The surviving session is still audible and still has its handlers, so the
  // tab remains controllable through it.
  ASSERT_TRUE(service->GetActiveControllersNum() == 1);
  ASSERT_TRUE(
      controller->IsActiveSessionActionSupported(MediaSessionAction::Play));
  ASSERT_TRUE(
      controller->IsActiveSessionActionSupported(MediaSessionAction::Pause));

  controller->NotifySessionDestroyed(SECOND_SESSION_CONTEXT_ID);
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  first.SetAudible(MediaAudibleState::eInaudible);
  second.SetAudible(MediaAudibleState::eInaudible);
  controller->Shutdown();
}

// With the pref off, the same page gets the behaviour it had before: its
// session is never exposed and the controller stays inactive, because there is
// no controllable media to key activation off.
TEST(MediaController, WebAudioDoesNotActivateControllerWhenPrefIsDisabled)
{
  AutoWebAudioActivationPref prefOff(false);
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);

  RefPtr<MediaController> controller = new MediaController(FIRST_CONTROLLER_ID);
  controller->NotifySessionCreated(FAKE_CONTEXT_ID);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Play);
  controller->EnableAction(FAKE_CONTEXT_ID, MediaSessionAction::Pause);
  controller->SetDeclaredPlaybackState(FAKE_CONTEXT_ID,
                                       MediaSessionPlaybackState::Playing);

  FakeUncontrollableSource src(controller);
  src.SetAudible(MediaAudibleState::eAudible);
  ASSERT_TRUE(service->GetActiveControllersNum() == 0);
  ASSERT_FALSE(
      controller->IsActiveSessionActionSupported(MediaSessionAction::Play));
  ASSERT_TRUE(controller->PlaybackState() == MediaSessionPlaybackState::None);

  src.SetAudible(MediaAudibleState::eInaudible);
  controller->Shutdown();
}
