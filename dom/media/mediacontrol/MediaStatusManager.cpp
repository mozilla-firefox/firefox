/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaStatusManager.h"

#include "MediaControlService.h"
#include "mozilla/Components.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/MediaControlUtils.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "nsContentUtils.h"
#include "nsIChromeRegistry.h"
#include "nsIObserverService.h"
#include "nsIXULAppInfo.h"
#include "nsNetUtil.h"

#ifdef MOZ_PLACES
#  include "nsIFaviconService.h"
#endif  // MOZ_PLACES

extern mozilla::LazyLogModule gMediaControlLog;

// avoid redefined macro in unified build
#undef LOG
#define LOG(msg, ...)                            \
  MOZ_LOG_FMT(gMediaControlLog, LogLevel::Debug, \
              "MediaStatusManager={}, " msg, fmt::ptr(this), ##__VA_ARGS__)

namespace mozilla::dom {

static bool IsMetadataEmpty(const Maybe<MediaMetadataBase>& aMetadata) {
  // Media session's metadata is null.
  if (!aMetadata) {
    return true;
  }

  // All attirbutes in metadata are empty.
  // https://w3c.github.io/mediasession/#empty-metadata
  const MediaMetadataBase& metadata = *aMetadata;
  return metadata.mTitle.IsEmpty() && metadata.mArtist.IsEmpty() &&
         metadata.mAlbum.IsEmpty() && metadata.mArtwork.IsEmpty();
}

MediaStatusManager::MediaStatusManager(uint64_t aBrowsingContextId)
    : mTopLevelBrowsingContextId(aBrowsingContextId) {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess(),
                        "MediaStatusManager only runs on Chrome process!");
}

void MediaStatusManager::NotifyMediaAudibleChanged(
    uint64_t aBrowsingContextId, MediaAudibleState aState, ControlType aType,
    AudioSessionType aSessionType) {
  const bool ownerChanged = mPlaybackStatusDelegate.UpdateMediaAudibleState(
      aBrowsingContextId, aState, aType, aSessionType);
  // Re-derive the active session context when the controllable owner
  // changed (as before), or when there is currently no controllable owner
  // (so an uncontrollable source's audible transition, e.g. the Web Audio
  // API, can still be picked up by the fallback in
  // UpdateActiveMediaSessionContext). Skipping this otherwise avoids redoing
  // the same derivation on every audible toggle of an unrelated source.
  if (ownerChanged ||
      !mPlaybackStatusDelegate.GetActiveAudibleControllableContextId()) {
    UpdateActiveMediaSessionContext();
  }
  UpdateGuessedPlaybackState();
}

void MediaStatusManager::NotifySessionCreated(uint64_t aBrowsingContextId) {
  const bool created = mMediaSessionInfoMap.WithEntryHandle(
      aBrowsingContextId, [&](auto&& entry) {
        if (entry) return false;

        LOG("Session {} has been created", aBrowsingContextId);
        entry.Insert(MediaSessionInfo::EmptyInfo());
        return true;
      });

  if (created) {
    UpdateActiveMediaSessionContext();
  }
}

void MediaStatusManager::NotifySessionDestroyed(uint64_t aBrowsingContextId) {
  if (mMediaSessionInfoMap.Remove(aBrowsingContextId)) {
    LOG("Session {} has been destroyed", aBrowsingContextId);
    // Another context may still have a session that qualifies, and without
    // a controllable owner nothing else would re-derive this until the next
    // audible change -- which for audio that simply keeps playing never
    // comes. The last branch of the derivation clears the active session
    // when nothing qualifies.
    UpdateActiveMediaSessionContext();
  }
}

void MediaStatusManager::UpdateMetadata(
    uint64_t aBrowsingContextId, const Maybe<MediaMetadataBase>& aMetadata) {
  auto info = mMediaSessionInfoMap.Lookup(aBrowsingContextId);
  if (!info) {
    return;
  }
  if (IsMetadataEmpty(aMetadata)) {
    LOG("Reset metadata for session {}", aBrowsingContextId);
    info->mMetadata.reset();
  } else {
    LOG("Update metadata for session {} title={} artist={} album={}",
        aBrowsingContextId, NS_ConvertUTF16toUTF8((*aMetadata).mTitle).get(),
        NS_ConvertUTF16toUTF8(aMetadata->mArtist).get(),
        NS_ConvertUTF16toUTF8(aMetadata->mAlbum).get());
    info->mMetadata = aMetadata;
  }
  // Only notify the event if the changed metadata belongs to the active media
  // session.
  if (mActiveMediaSessionContextId &&
      *mActiveMediaSessionContextId == aBrowsingContextId) {
    LOG("Notify metadata change for active session {}", aBrowsingContextId);
    mMetadataChangedEvent.Notify(GetCurrentMediaMetadata());
  }
  if (StaticPrefs::media_mediacontrol_testingevents_enabled()) {
    if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
      obs->NotifyObservers(nullptr, "media-session-controller-metadata-changed",
                           nullptr);
    }
  }
}

void MediaStatusManager::UpdateActiveMediaSessionContext() {
  // A context that owns a controllable audible source (e.g. an
  // <audio>/<video> element) always dominates. If it has a MediaSession
  // registered, that's authoritative; if it doesn't, there is nothing to
  // expose and we deliberately do NOT fall back to some other audible
  // session -- a real controllable source taking over without declaring its
  // own session still displaces whatever was previously active.
  Maybe<uint64_t> controllableOwner =
      mPlaybackStatusDelegate.GetActiveAudibleControllableContextId();
  if (controllableOwner) {
    if (mMediaSessionInfoMap.Contains(*controllableOwner)) {
      SetActiveMediaSessionContextId(*controllableOwner);
    } else {
      ClearActiveMediaSessionContextIdIfNeeded();
    }
    return;
  }

  // Everything below exposes a session whose audio comes from a source
  // outside the controllable media lifecycle, which is what the pref gates.
  if (!StaticPrefs::media_mediacontrol_webaudio_activation_enabled()) {
    return ClearActiveMediaSessionContextIdIfNeeded();
  }

  // No controllable source currently dominates. Prefer keeping the current
  // active session over switching to a different one that also qualifies,
  // so that multiple simultaneously-audible sessions without a controllable
  // source don't flip-flop against each other.
  if (mActiveMediaSessionContextId &&
      mMediaSessionInfoMap.Contains(*mActiveMediaSessionContextId) &&
      mPlaybackStatusDelegate.IsBcAudible(*mActiveMediaSessionContextId)) {
    return;
  }

  // Otherwise, fall back to any context that has a MediaSession and is
  // audible through any means, including sources that don't participate in
  // the controllable media lifecycle (e.g. the Web Audio API). Without this,
  // a page could declare a correct, fully populated MediaSession yet never
  // have it exposed simply because it doesn't play audio through an
  // HTMLMediaElement.
  for (const auto& entry : mMediaSessionInfoMap) {
    if (mPlaybackStatusDelegate.IsBcAudible(entry.GetKey())) {
      SetActiveMediaSessionContextId(entry.GetKey());
      return;
    }
  }

  // Nothing is audible right now. An uncontrollable source that has fallen
  // silent is the equivalent of a paused HTMLMediaElement, which stays
  // controllable because the element still exists, so keep the current
  // session while the page still has one registered. Otherwise pausing from
  // the platform's controls would tear those controls down and leave no way
  // to resume, and a brief silence between tracks would cycle them off and
  // on.
  if (mActiveMediaSessionContextId &&
      mMediaSessionInfoMap.Contains(*mActiveMediaSessionContextId)) {
    return;
  }

  ClearActiveMediaSessionContextIdIfNeeded();
}

void MediaStatusManager::SetActiveMediaSessionContextId(
    uint64_t aBrowsingContextId) {
  if (mActiveMediaSessionContextId &&
      *mActiveMediaSessionContextId == aBrowsingContextId) {
    LOG("Active session context {} keeps unchanged",
        *mActiveMediaSessionContextId);
    return;
  }
  mActiveMediaSessionContextId = Some(aBrowsingContextId);
  StoreMediaSessionContextIdOnWindowContext();
  LOG("context {} becomes active session context",
      *mActiveMediaSessionContextId);
  mMetadataChangedEvent.Notify(GetCurrentMediaMetadata());
  mSupportedActionsChangedEvent.Notify(GetSupportedActions());
  mPositionStateChangedEvent.Notify(GetCurrentPositionState());
  if (StaticPrefs::media_mediacontrol_testingevents_enabled()) {
    if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
      obs->NotifyObservers(nullptr, "active-media-session-changed", nullptr);
    }
  }
  // The active session's declared playback state now feeds into the actual
  // playback state (see UpdateActualPlaybackState), so switching sessions can
  // change it even though neither the declared nor the guessed state moved.
  UpdateActualPlaybackState();
}

void MediaStatusManager::ClearActiveMediaSessionContextIdIfNeeded() {
  if (!mActiveMediaSessionContextId) {
    return;
  }
  LOG("Clear active session context");
  mActiveMediaSessionContextId.reset();
  StoreMediaSessionContextIdOnWindowContext();
  mMetadataChangedEvent.Notify(GetCurrentMediaMetadata());
  mSupportedActionsChangedEvent.Notify(GetSupportedActions());
  mPositionStateChangedEvent.Notify(GetCurrentPositionState());
  if (StaticPrefs::media_mediacontrol_testingevents_enabled()) {
    if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
      obs->NotifyObservers(nullptr, "active-media-session-changed", nullptr);
    }
  }
  UpdateActualPlaybackState();
}

void MediaStatusManager::StoreMediaSessionContextIdOnWindowContext() {
  RefPtr<CanonicalBrowsingContext> bc =
      CanonicalBrowsingContext::Get(mTopLevelBrowsingContextId);
  if (bc && bc->GetTopWindowContext()) {
    (void)bc->GetTopWindowContext()->SetActiveMediaSessionContextId(
        mActiveMediaSessionContextId);
  }
}

MediaMetadataBase MediaStatusManager::CreateDefaultMetadata() const {
  MediaMetadataBase metadata;
  metadata.mTitle = GetDefaultTitle();
  metadata.mUrl = GetUrl();
  metadata.mArtwork.AppendElement()->mSrc = GetDefaultFaviconURL();

  LOG("Default media metadata, title={}, album src={}",
      NS_ConvertUTF16toUTF8(metadata.mTitle).get(),
      NS_ConvertUTF16toUTF8(metadata.mArtwork[0].mSrc).get());
  return metadata;
}

nsString MediaStatusManager::GetDefaultTitle() const {
  RefPtr<MediaControlService> service = MediaControlService::GetService();
  nsString defaultTitle = service->GetFallbackTitle();

  RefPtr<CanonicalBrowsingContext> bc =
      CanonicalBrowsingContext::Get(mTopLevelBrowsingContextId);
  if (!bc) {
    return defaultTitle;
  }

  RefPtr<WindowGlobalParent> globalParent = bc->GetCurrentWindowGlobal();
  if (!globalParent) {
    return defaultTitle;
  }

  // The media metadata would be shown on the virtual controller interface. For
  // example, on Android, the interface would be shown on both notification bar
  // and lockscreen. Therefore, what information we provide via metadata is
  // quite important, because if we're in private browsing, we don't want to
  // expose details about what website the user is browsing on the lockscreen.
  // Therefore, using the default title when in the private browsing or the
  // document title is empty. Otherwise, use the document title.
  nsString documentTitle;
  if (!IsInPrivateBrowsing()) {
    globalParent->GetDocumentTitle(documentTitle);
  }
  return documentTitle.IsEmpty() ? defaultTitle : documentTitle;
}

nsCString MediaStatusManager::GetUrl() const {
  nsCString defaultUrl;

  RefPtr<CanonicalBrowsingContext> bc =
      CanonicalBrowsingContext::Get(mTopLevelBrowsingContextId);
  if (!bc) {
    return defaultUrl;
  }

  RefPtr<WindowGlobalParent> globalParent = bc->GetCurrentWindowGlobal();
  if (!globalParent) {
    return defaultUrl;
  }

  if (IsInPrivateBrowsing()) {
    return defaultUrl;
  }

  nsIURI* documentURI = globalParent->GetDocumentURI();
  if (!documentURI) {
    return defaultUrl;
  }

  return documentURI->GetSpecOrDefault();
}

nsString MediaStatusManager::GetDefaultFaviconURL() const {
#ifdef MOZ_PLACES
  nsCOMPtr<nsIURI> faviconURI;
  nsresult rv = NS_NewURI(getter_AddRefs(faviconURI),
                          nsLiteralCString(FAVICON_DEFAULT_URL));
  NS_ENSURE_SUCCESS(rv, u""_ns);

  // Convert URI from `chrome://XXX` to `file://XXX` because we would like to
  // let OS related frameworks, such as SMTC and MPRIS, handle this URL in order
  // to show the icon on virtual controller interface.
  nsCOMPtr<nsIChromeRegistry> regService =
      components::ChromeRegistry::Service();
  if (!regService) {
    return u""_ns;
  }
  nsCOMPtr<nsIURI> processedURI;
  regService->ConvertChromeURL(faviconURI, getter_AddRefs(processedURI));

  nsAutoCString spec;
  if (NS_FAILED(processedURI->GetSpec(spec))) {
    return u""_ns;
  }
  return NS_ConvertUTF8toUTF16(spec);
#else
  return u""_ns;
#endif
}

void MediaStatusManager::SetDeclaredPlaybackState(
    uint64_t aBrowsingContextId, MediaSessionPlaybackState aState) {
  auto info = mMediaSessionInfoMap.Lookup(aBrowsingContextId);
  if (!info) {
    return;
  }
  LOG("SetDeclaredPlaybackState from {} to {}",
      ToMediaSessionPlaybackStateStr(info->mDeclaredPlaybackState),
      ToMediaSessionPlaybackStateStr(aState));
  info->mDeclaredPlaybackState = aState;
  UpdateActualPlaybackState();
}

MediaSessionPlaybackState MediaStatusManager::GetCurrentDeclaredPlaybackState()
    const {
  if (!mActiveMediaSessionContextId) {
    return MediaSessionPlaybackState::None;
  }
  return mMediaSessionInfoMap.Get(*mActiveMediaSessionContextId)
      .mDeclaredPlaybackState;
}

bool MediaStatusManager::IsActiveSessionActionSupported(
    MediaSessionAction aAction) const {
  if (!mActiveMediaSessionContextId) {
    return false;
  }
  return mMediaSessionInfoMap.Get(*mActiveMediaSessionContextId)
      .IsActionSupported(aAction);
}

void MediaStatusManager::NotifyMediaPlaybackChanged(uint64_t aBrowsingContextId,
                                                    MediaPlaybackState aState) {
  LOG("UpdateMediaPlaybackState {} for context {}", EnumValueToString(aState),
      aBrowsingContextId);
  const Maybe<uint64_t> oldOwner =
      mPlaybackStatusDelegate.GetActiveAudibleControllableContextId();
  mPlaybackStatusDelegate.UpdateMediaPlaybackState(aBrowsingContextId, aState);
  // Stopping the last controllable source hands ownership back, and the
  // context that takes over may be one whose session is only exposed through
  // the uncontrollable fallback. Without this it would stay unexposed until
  // that source next toggled audibility, which for audio that simply keeps
  // playing is never.
  if (mPlaybackStatusDelegate.GetActiveAudibleControllableContextId() !=
      oldOwner) {
    UpdateActiveMediaSessionContext();
  }
  UpdateGuessedPlaybackState();
}

void MediaStatusManager::UpdateGuessedPlaybackState() {
  // https://w3c.github.io/mediasession/#guessed-playback-state
  // Controlled media that is playing is the usual signal, but the spec asks
  // for other sources to be considered as well, so a tab producing audio
  // through the Web Audio API counts too. Those sources never report a
  // playback state of their own, so audibility is all we have to go on.
  // Only fall back to audibility when the tab has no controllable media at
  // all. Where such media exists its own playback state is authoritative, and
  // letting an unrelated audible source override it would mean pausing a
  // video left the tab reporting that it is still playing just because a
  // sound effect kept running.
  const bool playing =
      mPlaybackStatusDelegate.IsPlaying() ||
      (StaticPrefs::media_mediacontrol_webaudio_activation_enabled() &&
       !mPlaybackStatusDelegate.IsAnyMediaBeingControlled() &&
       mPlaybackStatusDelegate.IsAudible());
  SetGuessedPlayState(playing ? MediaSessionPlaybackState::Playing
                              : MediaSessionPlaybackState::Paused);
}

void MediaStatusManager::SetGuessedPlayState(MediaSessionPlaybackState aState) {
  if (aState == mGuessedPlaybackState) {
    return;
  }
  // Only a tab that was playing can become paused. Without this a tab that has
  // merely loaded media would move from `none` to `paused`.
  if (aState == MediaSessionPlaybackState::Paused &&
      mGuessedPlaybackState != MediaSessionPlaybackState::Playing) {
    return;
  }
  LOG("SetGuessedPlayState : '{}'", ToMediaSessionPlaybackStateStr(aState));
  mGuessedPlaybackState = aState;
  UpdateActualPlaybackState();
}

void MediaStatusManager::UpdateActualPlaybackState() {
  // The way to compute the actual playback state is based on the spec.
  // https://w3c.github.io/mediasession/#actual-playback-state
  MediaSessionPlaybackState newState =
      GetCurrentDeclaredPlaybackState() == MediaSessionPlaybackState::Playing
          ? MediaSessionPlaybackState::Playing
          : mGuessedPlaybackState;
  if (mActualPlaybackState == newState) {
    return;
  }
  mActualPlaybackState = newState;
  LOG("UpdateActualPlaybackState : '{}'",
      ToMediaSessionPlaybackStateStr(mActualPlaybackState));
  mPlaybackStateChangedEvent.Notify(mActualPlaybackState);
}

void MediaStatusManager::EnableAction(uint64_t aBrowsingContextId,
                                      MediaSessionAction aAction) {
  auto info = mMediaSessionInfoMap.Lookup(aBrowsingContextId);
  if (!info) {
    return;
  }
  if (info->IsActionSupported(aAction)) {
    LOG("Action '{}' has already been enabled for context {}",
        GetEnumString(aAction).get(), aBrowsingContextId);
    return;
  }
  LOG("Enable action {} for context {}", GetEnumString(aAction).get(),
      aBrowsingContextId);
  info->EnableAction(aAction);
  NotifySupportedKeysChangedIfNeeded(aBrowsingContextId);
}

void MediaStatusManager::DisableAction(uint64_t aBrowsingContextId,
                                       MediaSessionAction aAction) {
  auto info = mMediaSessionInfoMap.Lookup(aBrowsingContextId);
  if (!info) {
    return;
  }
  if (!info->IsActionSupported(aAction)) {
    LOG("Action '{}' hasn't been enabled yet for context {}",
        GetEnumString(aAction).get(), aBrowsingContextId);
    return;
  }
  LOG("Disable action {} for context {}", GetEnumString(aAction).get(),
      aBrowsingContextId);
  info->DisableAction(aAction);
  NotifySupportedKeysChangedIfNeeded(aBrowsingContextId);
}

void MediaStatusManager::UpdatePositionState(
    uint64_t aBrowsingContextId, const Maybe<PositionState>& aState) {
  auto info = mMediaSessionInfoMap.Lookup(aBrowsingContextId);
  if (info) {
    LOG("Update position state for context {}", aBrowsingContextId);
    info->mPositionState = aState;
  }

  // The position state comes from non-active media session which we don't care.
  if (!mActiveMediaSessionContextId ||
      *mActiveMediaSessionContextId != aBrowsingContextId) {
    return;
  }
  mPositionStateChangedEvent.Notify(aState);
}

void MediaStatusManager::UpdateGuessedPositionState(
    uint64_t aBrowsingContextId, const nsID& aMediaId,
    const Maybe<PositionState>& aGuessedState) {
  mPlaybackStatusDelegate.UpdateGuessedPositionState(aBrowsingContextId,
                                                     aMediaId, aGuessedState);

  // The position state comes from a non-active media session and
  // there is another one active (with some metadata).
  if (mActiveMediaSessionContextId &&
      *mActiveMediaSessionContextId != aBrowsingContextId) {
    return;
  }

  // media session is declared for the updated session, but there's no active
  // session - it will get emitted once the session becomes active
  if (mMediaSessionInfoMap.Contains(aBrowsingContextId) &&
      !mActiveMediaSessionContextId) {
    return;
  }

  mPositionStateChangedEvent.Notify(GetCurrentPositionState());
}

void MediaStatusManager::NotifySupportedKeysChangedIfNeeded(
    uint64_t aBrowsingContextId) {
  // Only the active media session's supported actions would be shown in virtual
  // control interface, so we only notify the event when supported actions
  // change happens on the active media session.
  if (!mActiveMediaSessionContextId ||
      *mActiveMediaSessionContextId != aBrowsingContextId) {
    return;
  }
  mSupportedActionsChangedEvent.Notify(GetSupportedActions());
}

CopyableTArray<MediaSessionAction> MediaStatusManager::GetSupportedActions()
    const {
  CopyableTArray<MediaSessionAction> supportedActions;
  if (!mActiveMediaSessionContextId) {
    return supportedActions;
  }

  MediaSessionInfo info =
      mMediaSessionInfoMap.Get(*mActiveMediaSessionContextId);
  for (MediaSessionAction action :
       MakeWebIDLEnumeratedRange<MediaSessionAction>()) {
    if (info.IsActionSupported(action)) {
      supportedActions.AppendElement(action);
    }
  }
  return supportedActions;
}

MediaMetadataBase MediaStatusManager::GetCurrentMediaMetadata() const {
  // If we don't have active media session, active media session doesn't have
  // media metadata, or we're in private browsing mode, then we should create a
  // default metadata which is using website's title and favicon as title and
  // artwork.
  if (mActiveMediaSessionContextId && !IsInPrivateBrowsing()) {
    MediaSessionInfo info =
        mMediaSessionInfoMap.Get(*mActiveMediaSessionContextId);
    if (!info.mMetadata) {
      return CreateDefaultMetadata();
    }
    MediaMetadataBase& metadata = *(info.mMetadata);
    FillMissingTitleAndArtworkIfNeeded(metadata);
    metadata.mUrl = GetUrl();
    return metadata;
  }
  return CreateDefaultMetadata();
}

Maybe<PositionState> MediaStatusManager::GetCurrentPositionState() const {
  if (mActiveMediaSessionContextId) {
    auto info = mMediaSessionInfoMap.Lookup(*mActiveMediaSessionContextId);
    if (info && info->mPositionState) {
      return info->mPositionState;
    }
  }

  return mPlaybackStatusDelegate.GuessedMediaPositionState(
      mActiveMediaSessionContextId);
}

void MediaStatusManager::FillMissingTitleAndArtworkIfNeeded(
    MediaMetadataBase& aMetadata) const {
  // If the metadata doesn't set its title and artwork properly, we would like
  // to use default title and favicon instead in order to prevent showing
  // nothing on the virtual control interface.
  if (aMetadata.mTitle.IsEmpty()) {
    aMetadata.mTitle = GetDefaultTitle();
  }
  if (aMetadata.mArtwork.IsEmpty()) {
    aMetadata.mArtwork.AppendElement()->mSrc = GetDefaultFaviconURL();
  }
}

bool MediaStatusManager::IsInPrivateBrowsing() const {
  RefPtr<CanonicalBrowsingContext> bc =
      CanonicalBrowsingContext::Get(mTopLevelBrowsingContextId);
  if (!bc) {
    return false;
  }
  RefPtr<Element> element = bc->GetEmbedderElement();
  if (!element) {
    return false;
  }
  if (StaticPrefs::media_privatebrowsing_metadata_enabled()) {
    return false;
  }
  return element->OwnerDoc()->IsInPrivateBrowsing();
}

MediaSessionPlaybackState MediaStatusManager::PlaybackState() const {
  return mActualPlaybackState;
}

bool MediaStatusManager::IsMediaAudible() const {
  return mPlaybackStatusDelegate.IsAudible();
}

void MediaStatusManager::NotifyBrowsingContextDiscarded(
    uint64_t aBrowsingContextId) {
  // When a child browsing context is destroyed (e.g. an iframe is removed),
  // clear its state so things associated with that state do not remain stuck,
  // then re-evaluate the active media session context in case the discarded
  // context was the current one.
  LOG("NotifyBrowsingContextDiscarded context %" PRIu64, aBrowsingContextId);
  mPlaybackStatusDelegate.ClearBrowsingContext(aBrowsingContextId);
  // The context takes its media session with it. Dropping the entry matters
  // because the active session is otherwise kept alive while the page still
  // has one registered, which would outlive the page itself.
  mMediaSessionInfoMap.Remove(aBrowsingContextId);
  UpdateActiveMediaSessionContext();
}

bool MediaStatusManager::IsMediaPlaying() const {
  return mActualPlaybackState == MediaSessionPlaybackState::Playing;
}

bool MediaStatusManager::IsAnyMediaBeingControlled() const {
  return mPlaybackStatusDelegate.IsAnyMediaBeingControlled();
}

AudioSessionType MediaStatusManager::EffectiveTypeForBc(
    uint64_t aBrowsingContextId) const {
  return mPlaybackStatusDelegate.EffectiveTypeForBc(aBrowsingContextId);
}

bool MediaStatusManager::IsBcAudible(uint64_t aBrowsingContextId) const {
  return mPlaybackStatusDelegate.IsBcAudible(aBrowsingContextId);
}

void MediaStatusManager::NotifyPageTitleChanged() {
  // If active media session has set non-empty metadata, then we would use that
  // instead of using default metadata.
  if (mActiveMediaSessionContextId &&
      mMediaSessionInfoMap.Lookup(*mActiveMediaSessionContextId)->mMetadata) {
    return;
  }
  // In private browsing mode, we won't show page title on default metadata so
  // we don't need to update that.
  if (IsInPrivateBrowsing()) {
    return;
  }
  LOG("page title changed, update default metadata");
  mMetadataChangedEvent.Notify(GetCurrentMediaMetadata());
}

}  // namespace mozilla::dom
