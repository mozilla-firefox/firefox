/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.engine

import android.content.Intent
import java.security.cert.X509Certificate
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.CrashAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.MediaSessionAction
import mozilla.components.browser.state.action.PictureInPictureAction
import mozilla.components.browser.state.action.ReaderAction
import mozilla.components.browser.state.action.TrackingProtectionAction
import mozilla.components.browser.state.action.TranslationsAction
import mozilla.components.browser.state.selector.findTabOrCustomTab
import mozilla.components.browser.state.state.AppIntentState
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.LoadRequestState
import mozilla.components.browser.state.state.SecurityInfo
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.content.DownloadState.Status.INITIATED
import mozilla.components.browser.state.state.content.FindResultState
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSessionState
import mozilla.components.concept.engine.HitResult
import mozilla.components.concept.engine.content.blocking.Tracker
import mozilla.components.concept.engine.history.HistoryItem
import mozilla.components.concept.engine.manifest.WebAppManifest
import mozilla.components.concept.engine.media.RecordingDevice
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.concept.engine.permission.PermissionRequest
import mozilla.components.concept.engine.prompt.PromptRequest
import mozilla.components.concept.engine.translate.TranslationEngineState
import mozilla.components.concept.engine.translate.TranslationError
import mozilla.components.concept.engine.translate.TranslationOperation
import mozilla.components.concept.engine.window.WindowRequest
import mozilla.components.concept.fetch.Headers.Names.E_TAG
import mozilla.components.concept.fetch.Response
import mozilla.components.lib.state.Store

internal const val PAGE_LOAD_COMPLETION_PROGRESS = 100

/**
 * [EngineSession.Observer] implementation responsible to update the state of a [Session] from the events coming out of
 * an [EngineSession].
 */
@Suppress("TooManyFunctions", "LargeClass")
internal class EngineObserver(
    private val tabId: String,
    private val store: Store<BrowserState, BrowserAction>,
    private val scope: CoroutineScope,
) : EngineSession.Observer {

    override fun onScrollChange(scrollX: Int, scrollY: Int) {
        dispatchAsync(ReaderAction.UpdateReaderScrollYAction(tabId, scrollY))
    }

    override fun onNavigateBack() {
        dispatchAsync(ContentAction.UpdateSearchTermsAction(tabId, ""))
    }

    override fun onNavigateForward() {
        dispatchAsync(ContentAction.UpdateSearchTermsAction(tabId, ""))
    }

    override fun onGotoHistoryIndex() {
        dispatchAsync(ContentAction.UpdateSearchTermsAction(tabId, ""))
    }

    override fun onLoadData() {
        dispatchAsync(ContentAction.UpdateSearchTermsAction(tabId, ""))
    }

    override fun onLoadUrl() {
        if (store.state.findTabOrCustomTab(tabId)?.content?.isSearch == true) {
            dispatchAsync(ContentAction.UpdateIsSearchAction(tabId, false))
        } else {
            dispatchAsync(ContentAction.UpdateSearchTermsAction(tabId, ""))
        }
    }

    override fun onFirstContentfulPaint() {
        dispatchAsync(ContentAction.UpdateFirstContentfulPaintStateAction(tabId, true))
    }

    override fun onPaintStatusReset() {
        dispatchAsync(ContentAction.UpdateFirstContentfulPaintStateAction(tabId, false))
    }

    override fun onLocationChange(url: String, hasUserGesture: Boolean) {
        dispatchAsync(ContentAction.UpdateUrlAction(tabId, url, hasUserGesture))
    }

    @Suppress("DEPRECATION") // Session observable is deprecated
    override fun onLoadRequest(
        url: String,
        triggeredByRedirect: Boolean,
        triggeredByWebContent: Boolean,
    ) {
        if (triggeredByWebContent) {
            dispatchAsync(ContentAction.UpdateSearchTermsAction(tabId, ""))
        }

        val loadRequest = LoadRequestState(url, triggeredByRedirect, triggeredByWebContent)
        dispatchAsync(ContentAction.UpdateLoadRequestAction(tabId, loadRequest))
    }

    override fun onLaunchIntentRequest(
        url: String,
        appIntent: Intent?,
        fallbackUrl: String?,
        appName: String?,
        sourceUrl: String?,
    ) {
        dispatchAsync(
            ContentAction.UpdateAppIntentAction(
                tabId,
                AppIntentState(url, appIntent, fallbackUrl, appName, sourceUrl),
            )
        )
    }

    override fun onTitleChange(title: String) {
        dispatchAsync(ContentAction.UpdateTitleAction(tabId, title))
    }

    override fun onPreviewImageChange(previewImageUrl: String) {
        dispatchAsync(ContentAction.UpdatePreviewImageAction(tabId, previewImageUrl))
    }

    override fun onProgress(progress: Int) {
        // page load is completed, start the translation initialization if not initialized yet
        // referencing to a field in the state is not recommended, this flow should be reconsidered
        // while the visual completeness logic is revisited in Bug 1966977.
        if (progress == PAGE_LOAD_COMPLETION_PROGRESS && !store.state.translationsInitialized) {
            dispatchAsync(TranslationsAction.InitTranslationsBrowserState)
        }
        dispatchAsync(ContentAction.UpdateProgressAction(tabId, progress))
    }

    override fun onLoadingStateChange(loading: Boolean) {
        dispatchAsync(ContentAction.UpdateLoadingStateAction(tabId, loading))

        if (loading) {
            dispatchAsync(ContentAction.ClearFindResultsAction(tabId))
            dispatchAsync(ContentAction.UpdateRefreshCanceledStateAction(tabId, false))
            dispatchAsync(TrackingProtectionAction.ClearTrackersAction(tabId))
        }
    }

    override fun onNavigationStateChange(canGoBack: Boolean?, canGoForward: Boolean?) {
        canGoBack?.let {
            dispatchAsync(ContentAction.UpdateBackNavigationStateAction(tabId, canGoBack))
        }
        canGoForward?.let {
            dispatchAsync(ContentAction.UpdateForwardNavigationStateAction(tabId, canGoForward))
        }
    }

    override fun onSecurityChange(secure: Boolean, host: String?, issuer: String?, certificate: X509Certificate?) {
        dispatchAsync(
            ContentAction.UpdateSecurityInfoAction(
                tabId,
                SecurityInfo.from(secure, host ?: "", issuer ?: "", certificate),
            )
        )
    }

    override fun onTrackerBlocked(tracker: Tracker) {
        dispatchAsync(TrackingProtectionAction.TrackerBlockedAction(tabId, tracker))
    }

    override fun onTrackerLoaded(tracker: Tracker) {
        dispatchAsync(TrackingProtectionAction.TrackerLoadedAction(tabId, tracker))
    }

    override fun onExcludedOnTrackingProtectionChange(excluded: Boolean) {
        dispatchAsync(TrackingProtectionAction.ToggleExclusionListAction(tabId, excluded))
    }

    override fun onTrackerBlockingEnabledChange(enabled: Boolean) {
        dispatchAsync(TrackingProtectionAction.ToggleAction(tabId, enabled))
    }

    override fun onTranslatePageChange() {
        dispatchAsync(TranslationsAction.SetTranslateProcessingAction(tabId, isProcessing = false))
    }

    override fun onLongPress(hitResult: HitResult) {
        dispatchAsync(ContentAction.UpdateHitResultAction(tabId, hitResult))
    }

    override fun onFind(text: String) {
        dispatchAsync(ContentAction.ClearFindResultsAction(tabId))
    }

    override fun onFindResult(activeMatchOrdinal: Int, numberOfMatches: Int, isDoneCounting: Boolean) {
        dispatchAsync(
            ContentAction.AddFindResultAction(
                tabId,
                FindResultState(
                    activeMatchOrdinal,
                    numberOfMatches,
                    isDoneCounting,
                ),
            )
        )
    }

    override fun onExternalResource(
        url: String,
        fileName: String?,
        contentLength: Long?,
        contentType: String?,
        cookie: String?,
        userAgent: String?,
        isPrivate: Boolean,
        skipConfirmation: Boolean,
        openInApp: Boolean,
        response: Response?,
    ) {
        // We want to avoid negative contentLength values
        // For more info see https://bugzilla.mozilla.org/show_bug.cgi?id=1632594
        val fileSize = if (contentLength != null && contentLength < 0) null else contentLength
        val download =
            DownloadState(
                url,
                fileName,
                contentType,
                fileSize,
                0,
                INITIATED,
                userAgent,
                private = isPrivate,
                skipConfirmation = skipConfirmation,
                openInApp = openInApp,
                response = response,
                etag = response?.headers?.get(E_TAG),
            )

        dispatchAsync(
            ContentAction.UpdateDownloadAction(
                tabId,
                download,
            )
        )
    }

    override fun onDesktopModeChange(enabled: Boolean) {
        dispatchAsync(
            ContentAction.UpdateTabDesktopMode(
                tabId,
                enabled,
            )
        )
    }

    override fun onEnterPictureInPicture(session: EngineSession) {
        dispatchAsync(PictureInPictureAction.EnterAction(tabId, session))
    }

    override fun onPictureInPictureRequestFinished() {
        dispatchAsync(PictureInPictureAction.ConsumeRequestAction(tabId))
    }

    override fun onExitPictureInPicture() {
        dispatchAsync(PictureInPictureAction.ExitAction(tabId))
    }

    override fun onFullScreenChange(enabled: Boolean) {
        dispatchAsync(
            ContentAction.FullScreenChangedAction(
                tabId,
                enabled,
            )
        )
    }

    override fun onMetaViewportFitChanged(layoutInDisplayCutoutMode: Int) {
        dispatchAsync(
            ContentAction.ViewportFitChangedAction(
                tabId,
                layoutInDisplayCutoutMode,
            )
        )
    }

    override fun onContentPermissionRequest(permissionRequest: PermissionRequest) {
        dispatchAsync(
            ContentAction.UpdatePermissionsRequest(
                tabId,
                permissionRequest,
            )
        )
    }

    override fun onCancelContentPermissionRequest(permissionRequest: PermissionRequest) {
        dispatchAsync(
            ContentAction.ConsumePermissionsRequest(
                tabId,
                permissionRequest,
            )
        )
    }

    override fun onAppPermissionRequest(permissionRequest: PermissionRequest) {
        dispatchAsync(
            ContentAction.UpdateAppPermissionsRequest(
                tabId,
                permissionRequest,
            )
        )
    }

    override fun onPromptRequest(promptRequest: PromptRequest) {
        dispatchAsync(
            ContentAction.UpdatePromptRequestAction(
                tabId,
                promptRequest,
            )
        )
    }

    override fun onPromptDismissed(promptRequest: PromptRequest) {
        dispatchAsync(ContentAction.ConsumePromptRequestAction(tabId, promptRequest))
    }

    override fun onPromptUpdate(previousPromptRequestUid: String, promptRequest: PromptRequest) {
        dispatchAsync(ContentAction.ReplacePromptRequestAction(tabId, previousPromptRequestUid, promptRequest))
    }

    override fun onRepostPromptCancelled() {
        dispatchAsync(ContentAction.UpdateRefreshCanceledStateAction(tabId, true))
    }

    override fun onBeforeUnloadPromptDenied() {
        dispatchAsync(ContentAction.UpdateRefreshCanceledStateAction(tabId, true))
    }

    override fun onWindowRequest(windowRequest: WindowRequest) {
        dispatchAsync(
            ContentAction.UpdateWindowRequestAction(
                tabId,
                windowRequest,
            )
        )
    }

    override fun onShowDynamicToolbar() {
        dispatchAsync(ContentAction.UpdateExpandedToolbarStateAction(tabId, true))
    }

    override fun onMediaActivated(mediaSessionController: MediaSession.Controller) {
        dispatchAsync(
            MediaSessionAction.ActivatedMediaSessionAction(
                tabId,
                mediaSessionController,
            )
        )
    }

    override fun onMediaDeactivated() {
        dispatchAsync(MediaSessionAction.DeactivatedMediaSessionAction(tabId))
    }

    override fun onMediaMetadataChanged(metadata: MediaSession.Metadata) {
        dispatchAsync(MediaSessionAction.UpdateMediaMetadataAction(tabId, metadata))
    }

    override fun onMediaAudioSessionTypeChanged(type: MediaSession.AudioSessionType) {
        dispatchAsync(MediaSessionAction.UpdateMediaAudioSessionTypeAction(tabId, type))
    }

    override fun onMediaPlaybackStateChanged(playbackState: MediaSession.PlaybackState) {
        dispatchAsync(
            MediaSessionAction.UpdateMediaPlaybackStateAction(
                tabId,
                playbackState,
            )
        )
    }

    override fun onMediaFeatureChanged(features: MediaSession.Feature) {
        dispatchAsync(
            MediaSessionAction.UpdateMediaFeatureAction(
                tabId,
                features,
            )
        )
    }

    override fun onMediaPositionStateChanged(positionState: MediaSession.PositionState) {
        dispatchAsync(
            MediaSessionAction.UpdateMediaPositionStateAction(
                tabId,
                positionState,
            )
        )
    }

    override fun onMediaMuteChanged(muted: Boolean) {
        dispatchAsync(
            MediaSessionAction.UpdateMediaMutedAction(
                tabId,
                muted,
            )
        )
    }

    override fun onMediaFullscreenChanged(
        fullscreen: Boolean,
        elementMetadata: MediaSession.ElementMetadata?,
    ) {
        dispatchAsync(
            MediaSessionAction.UpdateMediaFullscreenAction(
                tabId,
                fullscreen,
                elementMetadata,
            )
        )
    }

    override fun onWebAppManifestLoaded(manifest: WebAppManifest) {
        dispatchAsync(ContentAction.UpdateWebAppManifestAction(tabId, manifest))
    }

    override fun onCrash() {
        dispatchAsync(CrashAction.SessionCrashedAction(tabId))
    }

    override fun onProcessKilled() {
        dispatchAsync(EngineAction.KillEngineSessionAction(tabId))
    }

    override fun onStateUpdated(state: EngineSessionState) {
        dispatchAsync(
            EngineAction.UpdateEngineSessionStateAction(
                tabId,
                state,
            )
        )
    }

    override fun onRecordingStateChanged(devices: List<RecordingDevice>) {
        dispatchAsync(
            ContentAction.SetRecordingDevices(
                tabId,
                devices,
            )
        )
    }

    override fun onHistoryStateChanged(historyList: List<HistoryItem>, currentIndex: Int) {
        dispatchAsync(
            ContentAction.UpdateHistoryStateAction(
                tabId,
                historyList,
                currentIndex,
            )
        )
    }

    override fun onSaveToPdfException(throwable: Throwable) {
        dispatchAsync(EngineAction.SaveToPdfExceptionAction(tabId, throwable))
    }

    override fun onPrintFinish() {
        dispatchAsync(EngineAction.PrintContentCompletedAction(tabId))
    }

    override fun onPrintException(isPrint: Boolean, throwable: Throwable) {
        dispatchAsync(EngineAction.PrintContentExceptionAction(tabId, isPrint, throwable))
    }

    override fun onSaveToPdfComplete() {
        dispatchAsync(EngineAction.SaveToPdfCompleteAction(tabId))
    }

    override fun onCheckForFormData(containsFormData: Boolean, adjustPriority: Boolean) {
        dispatchAsync(ContentAction.UpdateHasFormDataAction(tabId, containsFormData, adjustPriority))
    }

    override fun onCheckForFormDataException(throwable: Throwable) {
        dispatchAsync(ContentAction.CheckForFormDataExceptionAction(tabId, throwable))
    }

    override fun onTranslateExpected() {
        dispatchAsync(TranslationsAction.TranslateExpectedAction(tabId))
    }

    override fun onTranslateOffer() {
        dispatchAsync(TranslationsAction.TranslateOfferAction(tabId = tabId, isOfferTranslate = true))
    }

    override fun onTranslateStateChange(state: TranslationEngineState) {
        dispatchAsync(TranslationsAction.TranslateStateChangeAction(tabId, state))
    }

    override fun onTranslateComplete(operation: TranslationOperation) {
        dispatchAsync(TranslationsAction.TranslateSuccessAction(tabId, operation))
    }

    override fun onTranslateException(operation: TranslationOperation, translationError: TranslationError) {
        dispatchAsync(TranslationsAction.TranslateExceptionAction(tabId, operation, translationError))
    }

    private fun dispatchAsync(action: BrowserAction) = scope.launch {
        store.dispatch(action)
    }
}
