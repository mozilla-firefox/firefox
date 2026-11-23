/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaControlService.h"

#include "MediaControlUtils.h"
#include "MediaController.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Assertions.h"
#include "mozilla/Logging.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/intl/Localization.h"
#include "nsIObserverService.h"
#include "nsXULAppAPI.h"

using mozilla::intl::Localization;

#undef LOG
#define LOG(msg, ...)                            \
  MOZ_LOG_FMT(gMediaControlLog, LogLevel::Debug, \
              "MediaControlService={}, " msg, fmt::ptr(this), ##__VA_ARGS__)

#undef LOG_MAINCONTROLLER
#define LOG_MAINCONTROLLER(msg, ...) \
  MOZ_LOG_FMT(gMediaControlLog, LogLevel::Debug, msg, ##__VA_ARGS__)

#undef LOG_MAINCONTROLLER_INFO
#define LOG_MAINCONTROLLER_INFO(msg, ...) \
  MOZ_LOG_FMT(gMediaControlLog, LogLevel::Info, msg, ##__VA_ARGS__)

namespace mozilla::dom {

StaticRefPtr<MediaControlService> gMediaControlService;

/* static */
RefPtr<MediaControlService> MediaControlService::GetService() {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess(),
                        "MediaControlService only runs on Chrome process!");
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdown)) {
    return nullptr;
  }
  if (!gMediaControlService) {
    gMediaControlService = new MediaControlService();
    gMediaControlService->Init();
  }
  RefPtr<MediaControlService> service = gMediaControlService.get();
  return service;
}

/* static */
void MediaControlService::GenerateMediaControlKey(const GlobalObject& global,
                                                  MediaControlKey aKey,
                                                  double aSeekTime) {
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  if (service) {
    service->GenerateTestMediaControlKey(aKey, aSeekTime);
  }
}

/* static */
void MediaControlService::GetCurrentActiveMediaMetadata(
    const GlobalObject& aGlobal, MediaMetadataInit& aMetadata) {
  if (RefPtr<MediaControlService> service = MediaControlService::GetService()) {
    MediaMetadataBase metadata = service->GetMainControllerMediaMetadata();
    aMetadata.mTitle = metadata.mTitle;
    aMetadata.mArtist = metadata.mArtist;
    aMetadata.mAlbum = metadata.mAlbum;
    for (const auto& artwork : metadata.mArtwork) {
      // If OOM happens resulting in not able to append the element, then we
      // would get incorrect result and fail on test, so we don't need to throw
      // an error explicitly.
      if (MediaImage* image = aMetadata.mArtwork.AppendElement(fallible)) {
        image->mSrc = artwork.mSrc;
        image->mSizes = artwork.mSizes;
        image->mType = artwork.mType;
      }
    }
  }
}

/* static */
MediaSessionPlaybackState
MediaControlService::GetCurrentMediaSessionPlaybackState(
    GlobalObject& aGlobal) {
  if (RefPtr<MediaControlService> service = MediaControlService::GetService()) {
    return service->GetMainControllerPlaybackState();
  }
  return MediaSessionPlaybackState::None;
}

NS_INTERFACE_MAP_BEGIN(MediaControlService)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(MediaControlService)
NS_IMPL_RELEASE(MediaControlService)

MediaControlService::MediaControlService() {
  LOG("create media control service");
  RefPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->AddObserver(this, "xpcom-shutdown", false);
  }
}

void MediaControlService::Init() {
  mMediaKeysHandler = new MediaControlKeyHandler();
  mMediaControlKeyManager = new MediaControlKeyManager();
  mMediaControlKeyManager->AddListener(mMediaKeysHandler.get());
  mControllerManager = std::make_unique<ControllerManager>(this);

  // Initialize the fallback title
  nsTArray<nsCString> resIds{
      "branding/brand.ftl"_ns,
      "dom/media.ftl"_ns,
  };
  RefPtr<Localization> l10n = Localization::Create(resIds, true);
  {
    nsAutoCString translation;
    IgnoredErrorResult rv;
    l10n->FormatValueSync("mediastatus-fallback-title"_ns, translation, rv);
    if (!rv.Failed()) {
      mFallbackTitle = NS_ConvertUTF8toUTF16(translation);
    }
  }
}

MediaControlService::~MediaControlService() {
  LOG("destroy media control service");
  Shutdown();
}

NS_IMETHODIMP
MediaControlService::Observe(nsISupports* aSubject, const char* aTopic,
                             const char16_t* aData) {
  if (!strcmp(aTopic, "xpcom-shutdown")) {
    LOG("XPCOM shutdown");
    MOZ_ASSERT(gMediaControlService);
    RefPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
      obs->RemoveObserver(this, "xpcom-shutdown");
    }
    Shutdown();
    gMediaControlService = nullptr;
  }
  return NS_OK;
}

void MediaControlService::Shutdown() {
  mControllerManager->Shutdown();
  mMediaControlKeyManager->RemoveListener(mMediaKeysHandler.get());
}

bool MediaControlService::RegisterActiveMediaController(
    MediaController* aController) {
  MOZ_DIAGNOSTIC_ASSERT(mControllerManager,
                        "Register controller before initializing service");
  if (!mControllerManager->AddController(aController)) {
    LOG("Fail to register controller {}", aController->Id());
    return false;
  }
  LOG("Register media controller {}, currentNum={}", aController->Id(),
      GetActiveControllersNum());
  if (StaticPrefs::media_mediacontrol_testingevents_enabled()) {
    if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
      obs->NotifyObservers(nullptr, "media-controller-amount-changed", nullptr);
    }
  }
  return true;
}

bool MediaControlService::UnregisterActiveMediaController(
    MediaController* aController) {
  MOZ_DIAGNOSTIC_ASSERT(mControllerManager,
                        "Unregister controller before initializing service");
  if (!mControllerManager->RemoveController(aController)) {
    LOG("Fail to unregister controller {}", aController->Id());
    return false;
  }
  LOG("Unregister media controller {}, currentNum={}", aController->Id(),
      GetActiveControllersNum());
  if (StaticPrefs::media_mediacontrol_testingevents_enabled()) {
    if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
      obs->NotifyObservers(nullptr, "media-controller-amount-changed", nullptr);
    }
  }
  return true;
}

void MediaControlService::NotifyControllerPlaybackStateChanged(
    MediaController* aController) {
  MOZ_DIAGNOSTIC_ASSERT(
      mControllerManager,
      "controller state change happens before initializing service");
  MOZ_DIAGNOSTIC_ASSERT(aController);
  // The controller is not an active controller.
  if (!mControllerManager->Contains(aController)) {
    return;
  }

  // The controller is the main controller, propagate its playback state.
  if (GetMainController() == aController) {
    mControllerManager->MainControllerPlaybackStateChanged(
        aController->PlaybackState());
    return;
  }

  // The controller is not the main controller, but will become a new main
  // controller. As the service can contains multiple controllers and only one
  // controller can be controlled by media control keys. Therefore, when
  // controller's state becomes `playing`, then we would like to let that
  // controller being controlled, rather than other controller which might not
  // be playing at the time.
  if (GetMainController() != aController &&
      aController->PlaybackState() == MediaSessionPlaybackState::Playing) {
    mControllerManager->UpdateMainControllerIfNeeded(aController);
  }
}

void MediaControlService::RequestUpdateMainController(
    MediaController* aController) {
  MOZ_DIAGNOSTIC_ASSERT(aController);
  MOZ_DIAGNOSTIC_ASSERT(
      mControllerManager,
      "using controller in PIP mode before initializing service");
  // The controller is not an active controller.
  if (!mControllerManager->Contains(aController)) {
    return;
  }
  mControllerManager->UpdateMainControllerIfNeeded(aController);
}

uint64_t MediaControlService::GetActiveControllersNum() const {
  MOZ_DIAGNOSTIC_ASSERT(mControllerManager);
  return mControllerManager->GetControllersNum();
}

MediaController* MediaControlService::GetMainController() const {
  MOZ_DIAGNOSTIC_ASSERT(mControllerManager);
  return mControllerManager->GetMainController();
}

void MediaControlService::GenerateTestMediaControlKey(MediaControlKey aKey,
                                                      double aSeekValue) {
  if (!StaticPrefs::media_mediacontrol_testingevents_enabled()) {
    return;
  }
  // Generate seek details when necessary
  switch (aKey) {
    case MediaControlKey::Seekto:
      mMediaKeysHandler->OnActionPerformed(MediaControlAction(
          aKey, MediaControlActionParams(aSeekValue, false /* fast seek */)));
      break;
    case MediaControlKey::Seekbackward:
    case MediaControlKey::Seekforward:
      mMediaKeysHandler->OnActionPerformed(
          MediaControlAction(aKey, MediaControlActionParams(aSeekValue)));
      break;
    case MediaControlKey::Setvolume:
      mMediaKeysHandler->OnActionPerformed(MediaControlAction(
          aKey, MediaControlActionParams::FromVolume(aSeekValue)));
      break;
    default:
      mMediaKeysHandler->OnActionPerformed(MediaControlAction(aKey));
  }
}

MediaMetadataBase MediaControlService::GetMainControllerMediaMetadata() const {
  MediaMetadataBase metadata;
  if (!StaticPrefs::media_mediacontrol_testingevents_enabled()) {
    return metadata;
  }
  if (auto* controller = GetMainController()) {
    return controller->GetCurrentMediaMetadata();
  }
  return metadata;
}

MediaSessionPlaybackState MediaControlService::GetMainControllerPlaybackState()
    const {
  if (!StaticPrefs::media_mediacontrol_testingevents_enabled()) {
    return MediaSessionPlaybackState::None;
  }
  return GetMainController() ? GetMainController()->PlaybackState()
                             : MediaSessionPlaybackState::None;
}

nsString MediaControlService::GetFallbackTitle() const {
  return mFallbackTitle;
}

// Following functions belong to ControllerManager
MediaControlService::ControllerManager::ControllerManager(
    MediaControlService* aService)
    : mSource(aService->GetMediaControlKeySource()) {
  MOZ_ASSERT(mSource);
}

bool MediaControlService::ControllerManager::AddController(
    MediaController* aController) {
  MOZ_DIAGNOSTIC_ASSERT(aController);
  if (mControllers.contains(aController)) {
    return false;
  }
  mControllers.insertBack(aController);
  UpdateMainControllerIfNeeded(aController);
  return true;
}

bool MediaControlService::ControllerManager::RemoveController(
    MediaController* aController) {
  MOZ_DIAGNOSTIC_ASSERT(aController);
  if (!mControllers.contains(aController)) {
    return false;
  }
  // This is LinkedListElement's method which will remove controller from
  // `mController`.
  static_cast<LinkedListControllerPtr>(aController)->remove();
  // If main controller is removed from the list, the last controller in the
  // list would become the main controller. Or reset the main controller when
  // the list is already empty.
  if (GetMainController() == aController) {
    UpdateMainControllerInternal(
        mControllers.isEmpty() ? nullptr : mControllers.getLast());
  }
  return true;
}

void MediaControlService::ControllerManager::UpdateMainControllerIfNeeded(
    MediaController* aController) {
  MOZ_DIAGNOSTIC_ASSERT(aController);

  if (GetMainController() == aController) {
    LOG_MAINCONTROLLER("This controller is alreay the main controller");
    return;
  }

  if (GetMainController() &&
      GetMainController()->IsBeingUsedInPIPModeOrFullscreen() &&
      !aController->IsBeingUsedInPIPModeOrFullscreen()) {
    LOG_MAINCONTROLLER(
        "Normal media controller can't replace the controller being used in "
        "PIP mode or fullscreen");
    return ReorderGivenController(aController,
                                  InsertOptions::eInsertAsNormalController);
  }
  ReorderGivenController(aController, InsertOptions::eInsertAsMainController);
  UpdateMainControllerInternal(aController);
}

void MediaControlService::ControllerManager::ReorderGivenController(
    MediaController* aController, InsertOptions aOption) {
  MOZ_DIAGNOSTIC_ASSERT(aController);
  MOZ_DIAGNOSTIC_ASSERT(mControllers.contains(aController));
  // Reset the controller's position and make it not in any list.
  static_cast<LinkedListControllerPtr>(aController)->remove();

  if (aOption == InsertOptions::eInsertAsMainController) {
    // Make the main controller as the last element in the list to maintain the
    // order of controllers because we always use the last controller in the
    // list as the next main controller when removing current main controller
    // from the list. Eg. If the list contains [A, B, C], and now the last
    // element C is the main controller. When B becomes main controller later,
    // the list would become [A, C, B]. And if A becomes main controller, list
    // would become [C, B, A]. Then, if we remove A from the list, the next main
    // controller would be B. But if we don't maintain the controller order when
    // main controller changes, we would pick C as the main controller because
    // the list is still [A, B, C].
    return mControllers.insertBack(aController);
  }

  MOZ_ASSERT(aOption == InsertOptions::eInsertAsNormalController);
  MOZ_ASSERT(GetMainController() != aController);
  // We might have multiple controllers which have higher priority (being used
  // in PIP or fullscreen) from the head, the normal controller should be
  // inserted before them. Therefore, search a higher priority controller from
  // the head and insert new controller before it.
  // Eg. a list [A, B, C, D, E] and D and E have higher priority, if we want
  // to insert F, then the final result would be [A, B, C, F, D, E]
  auto* current = static_cast<LinkedListControllerPtr>(mControllers.getFirst());
  while (!static_cast<MediaController*>(current)
              ->IsBeingUsedInPIPModeOrFullscreen()) {
    current = current->getNext();
  }
  MOZ_ASSERT(current, "Should have at least one higher priority controller!");
  current->setPrevious(aController);
}

void MediaControlService::ControllerManager::Shutdown() {
  mControllers.clear();
  DisconnectMainControllerEvents();
}

void MediaControlService::ControllerManager::MainControllerPlaybackStateChanged(
    MediaSessionPlaybackState aState) {
  MOZ_ASSERT(NS_IsMainThread());
  mSource->SetPlaybackState(aState);
}

void MediaControlService::ControllerManager::MainControllerMetadataChanged(
    const MediaMetadataBase& aMetadata) {
  MOZ_ASSERT(NS_IsMainThread());
  mSource->SetMediaMetadata(aMetadata);
}

void MediaControlService::ControllerManager::UpdateMainControllerInternal(
    MediaController* aController) {
  MOZ_ASSERT(NS_IsMainThread());
  if (aController) {
    aController->Select();
  }
  if (mMainController) {
    mMainController->Unselect();
  }
  mMainController = aController;

  if (!mMainController) {
    LOG_MAINCONTROLLER_INFO("Clear main controller");
    mSource->Close();
    DisconnectMainControllerEvents();
  } else {
    LOG_MAINCONTROLLER_INFO("Set controller {} as main controller",
                            mMainController->Id());
    if (!mSource->Open()) {
      LOG("Failed to open source for monitoring media keys");
    }
    // We would still update those status to the event source even if it failed
    // to open, because it would save the result and set them to the real
    // source when it opens. In addition, another benefit to do that is to
    // prevent testing from affecting by platform specific issues, because our
    // testing events rely on those status changes and they are all platform
    // independent.
    mSource->SetPlaybackState(mMainController->PlaybackState());
    mSource->SetMediaMetadata(mMainController->GetCurrentMediaMetadata());
    mSource->SetSupportedMediaKeys(mMainController->GetSupportedMediaKeys());
    mSource->SetPositionState(mMainController->GetCurrentPositionState());
    ConnectMainControllerEvents();
  }

  if (StaticPrefs::media_mediacontrol_testingevents_enabled()) {
    if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
      obs->NotifyObservers(nullptr, "main-media-controller-changed", nullptr);
    }
  }
}

void MediaControlService::ControllerManager::ConnectMainControllerEvents() {
  // As main controller has been changed, we should disconnect listeners from
  // the previous controller and reconnect them to the new controller.
  DisconnectMainControllerEvents();
  // Listen to main controller's event in order to propagate the content that
  // might be displayed on the virtual control interface created by the source.
  mMetadataChangedListener = mMainController->MetadataChangedEvent().Connect(
      AbstractThread::MainThread(), this,
      &ControllerManager::MainControllerMetadataChanged);
  mSupportedKeysChangedListener =
      mMainController->SupportedKeysChangedEvent().Connect(
          AbstractThread::MainThread(),
          [this](const MediaKeysArray& aSupportedKeys) {
            mSource->SetSupportedMediaKeys(aSupportedKeys);
          });
  mFullScreenChangedListener =
      mMainController->FullScreenChangedEvent().Connect(
          AbstractThread::MainThread(), [this](bool aIsEnabled) {
            mSource->SetEnableFullScreen(aIsEnabled);
          });
  mPictureInPictureModeChangedListener =
      mMainController->PictureInPictureModeChangedEvent().Connect(
          AbstractThread::MainThread(), [this](bool aIsEnabled) {
            mSource->SetEnablePictureInPictureMode(aIsEnabled);
          });
  mPositionChangedListener = mMainController->PositionChangedEvent().Connect(
      AbstractThread::MainThread(), [this](const Maybe<PositionState>& aState) {
        mSource->SetPositionState(aState);
      });
}

void MediaControlService::ControllerManager::DisconnectMainControllerEvents() {
  mMetadataChangedListener.DisconnectIfExists();
  mSupportedKeysChangedListener.DisconnectIfExists();
  mFullScreenChangedListener.DisconnectIfExists();
  mPictureInPictureModeChangedListener.DisconnectIfExists();
  mPositionChangedListener.DisconnectIfExists();
}

MediaController* MediaControlService::ControllerManager::GetMainController()
    const {
  return mMainController.get();
}

uint64_t MediaControlService::ControllerManager::GetControllersNum() const {
  return mControllers.length();
}

bool MediaControlService::ControllerManager::Contains(
    MediaController* aController) const {
  return mControllers.contains(aController);
}

}  // namespace mozilla::dom
