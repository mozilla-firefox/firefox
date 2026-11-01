/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.action

import android.content.ComponentCallbacks2
import android.graphics.Bitmap
import android.os.SystemClock
import java.util.Locale
import mozilla.components.browser.state.search.RegionState
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.AppIntentState
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContainerState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.EngineState
import mozilla.components.browser.state.state.LoadRequestState
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.browser.state.state.ReaderState
import mozilla.components.browser.state.state.SearchState
import mozilla.components.browser.state.state.SecurityInfo
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.TrackingProtectionState
import mozilla.components.browser.state.state.UndoHistoryState
import mozilla.components.browser.state.state.WebExtensionState
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.content.FindResultState
import mozilla.components.browser.state.state.content.PermissionHighlightsState
import mozilla.components.browser.state.state.content.ShareResourceState
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
import mozilla.components.browser.state.state.extension.WebExtensionPromptRequest
import mozilla.components.browser.state.state.recover.RecoverableTab
import mozilla.components.browser.state.state.recover.TabState
import mozilla.components.concept.engine.Engine
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
import mozilla.components.concept.engine.search.SearchRequest
import mozilla.components.concept.engine.translate.Language
import mozilla.components.concept.engine.translate.LanguageModel
import mozilla.components.concept.engine.translate.LanguageSetting
import mozilla.components.concept.engine.translate.ModelManagementOptions
import mozilla.components.concept.engine.translate.TranslationDownloadSize
import mozilla.components.concept.engine.translate.TranslationEngineState
import mozilla.components.concept.engine.translate.TranslationError
import mozilla.components.concept.engine.translate.TranslationOperation
import mozilla.components.concept.engine.translate.TranslationOptions
import mozilla.components.concept.engine.translate.TranslationPageSettingOperation
import mozilla.components.concept.engine.translate.TranslationPageSettings
import mozilla.components.concept.engine.translate.TranslationSupport
import mozilla.components.concept.engine.webextension.WebExtensionBrowserAction
import mozilla.components.concept.engine.webextension.WebExtensionPageAction
import mozilla.components.concept.engine.window.WindowRequest
import mozilla.components.concept.storage.HistoryMetadataKey
import mozilla.components.lib.state.Action
import mozilla.components.lib.state.DelicateAction
import mozilla.components.support.base.android.Clock

/** [Action] implementation related to [BrowserState]. */
sealed class BrowserAction : Action

/**
 * [BrowserAction] dispatched to indicate that the store is initialized and ready to use. This action is dispatched
 * automatically before any other action is processed. Its main purpose is to trigger initialization logic in
 * middlewares. The action itself has no effect on the [BrowserState].
 */
object InitAction : BrowserAction()

/** [BrowserAction] to indicate that restoring [BrowserState] is complete. */
object RestoreCompleteAction : BrowserAction()

/** [BrowserAction] implementations to react to extensions process events. */
sealed class ExtensionsProcessAction : BrowserAction() {
    /** [BrowserAction] to indicate when the crash prompt should be displayed to the user. */
    data class ShowPromptAction(val show: Boolean) : ExtensionsProcessAction()

    /** [BrowserAction] to indicate that the process has been re-enabled by the user. */
    object EnabledAction : ExtensionsProcessAction()

    /** [BrowserAction] to indicate that the process has been left disabled by the user. */
    object DisabledAction : ExtensionsProcessAction()
}

/** [BrowserAction] implementations to react to system events. */
sealed class SystemAction : BrowserAction() {
    /**
     * Optimizes the [BrowserState] by removing unneeded and optional resources if the system is in a low memory
     * condition.
     *
     * @param level The context of the trim, giving a hint of the amount of trimming the application may like to
     *   perform. See constants in [ComponentCallbacks2].
     */
    data class LowMemoryAction(val level: Int) : SystemAction()
}

/** [BrowserAction] implementations related to updating the [Locale] inside [BrowserState]. */
sealed class LocaleAction : BrowserAction() {
    /**
     * Updating the [BrowserState] to reflect app [Locale] changes.
     *
     * @property locale the updated [Locale]
     */
    data class UpdateLocaleAction(val locale: Locale?) : LocaleAction()

    /** Restores the [BrowserState.locale] state from storage. */
    object RestoreLocaleStateAction : LocaleAction()
}

/** [BrowserAction] implementations related to updating the list of [ClosedTabSessionState] inside [BrowserState]. */
sealed class RecentlyClosedAction : BrowserAction() {
    /**
     * Adds a list of [RecoverableTab] to the [BrowserState.closedTabs] list.
     *
     * @property tabs the [RecoverableTab]s to add
     */
    data class AddClosedTabsAction(val tabs: List<RecoverableTab>) : RecentlyClosedAction()

    /**
     * Removes a [RecoverableTab] from the [BrowserState.closedTabs] list.
     *
     * @property tab the [RecoverableTab] to remove
     */
    data class RemoveClosedTabAction(val tab: TabState) : RecentlyClosedAction()

    /** Removes all [RecoverableTab]s from the [BrowserState.closedTabs] list. */
    object RemoveAllClosedTabAction : RecentlyClosedAction()

    /** Prunes [RecoverableTab]s from the [BrowserState.closedTabs] list to keep only [maxTabs]. */
    data class PruneClosedTabsAction(val maxTabs: Int) : RecentlyClosedAction()

    /** Updates [BrowserState.closedTabs] to register the given list of [ClosedTab]. */
    data class ReplaceTabsAction(val tabs: List<TabState>) : RecentlyClosedAction()
}

/** [BrowserAction] implementations related to updating the list of [TabSessionState] inside [BrowserState]. */
sealed class TabListAction : BrowserAction() {
    /**
     * Adds a new [TabSessionState] to the [BrowserState.tabs] list.
     *
     * @property tab the [TabSessionState] to add
     * @property select whether or not to the tab should be selected.
     */
    data class AddTabAction(val tab: TabSessionState, val select: Boolean = false) : TabListAction()

    /** Adds multiple [TabSessionState] objects to the [BrowserState.tabs] list. */
    data class AddMultipleTabsAction(val tabs: List<TabSessionState>) : TabListAction()

    /**
     * Moves a set of tabIDs into a new position (maintaining internal ordering)
     *
     * @property tabIds The IDs of the tabs to move.
     * @property targetTabId A tab that the moved tabs will be moved next to
     * @property placeAfter True if the moved tabs should be placed after the target, False for placing before the
     *   target. Irrelevant if the target is one of the tabs being moved, since then the whole list is moved to where
     *   the target was. Ordering of the moved tabs relative to each other is preserved.
     */
    data class MoveTabsAction(
        val tabIds: List<String>,
        val targetTabId: String,
        val placeAfter: Boolean,
    ) : TabListAction()

    /**
     * Marks the [TabSessionState] with the given [tabId] as selected tab.
     *
     * @property tabId the ID of the tab to select.
     */
    data class SelectTabAction(val tabId: String) : TabListAction()

    /**
     * Removes the [TabSessionState] with the given [tabId] from the list of sessions.
     *
     * @property tabId the ID of the tab to remove.
     * @property selectParentIfExists whether or not a parent tab should be selected if one exists, defaults to true.
     * @property excludedFallbackTabIds a list of tab IDs that should be ignored when selecting a fallback tab.
     */
    data class RemoveTabAction(
        val tabId: String,
        val selectParentIfExists: Boolean = true,
        val excludedFallbackTabIds: Set<String> = emptySet(),
    ) : TabListAction()

    /**
     * Removes the [TabSessionState]s with the given [tabIds] from the list of sessions.
     *
     * @property tabIds the IDs of the tabs to remove.
     * @property excludedFallbackTabIds a list of tab IDs that should be ignored when selecting a fallback tab.
     */
    data class RemoveTabsAction(
        val tabIds: List<String>,
        val excludedFallbackTabIds: Set<String> = emptySet(),
    ) : TabListAction()

    /**
     * Restores state from a (partial) previous state.
     *
     * @property tabs the [TabSessionState]s to restore.
     * @property selectedTabId the ID of the tab to select.
     * @property restoreLocation [RestoreLocation] indicating where to restore [tabs].
     */
    data class RestoreAction(
        val tabs: List<RecoverableTab>,
        val selectedTabId: String? = null,
        val restoreLocation: RestoreLocation,
    ) : TabListAction() {

        /** Indicates what location the tabs should be restored at */
        enum class RestoreLocation {
            BEGINNING,
            END,
            AT_INDEX,
        }
    }

    /**
     * Removes both private and normal [TabSessionState]s.
     *
     * @property recoverable Indicates whether removed tabs should be recoverable.
     */
    data class RemoveAllTabsAction(val recoverable: Boolean = true) : TabListAction()

    /** Removes all private [TabSessionState]s. */
    object RemoveAllPrivateTabsAction : TabListAction()

    /** Removes all non-private [TabSessionState]s. */
    object RemoveAllNormalTabsAction : TabListAction()
}

/** [BrowserAction] implementations dealing with "undo" after removing a tab. */
sealed class UndoAction : BrowserAction() {
    /** Adds the list of [tabs] to [UndoHistoryState] with the given [tag]. */
    data class AddRecoverableTabs(
        val tag: String,
        val tabs: List<RecoverableTab>,
        val selectedTabId: String?,
    ) : UndoAction()

    /** Clears the tabs from [UndoHistoryState] for the given [tag]. */
    data class ClearRecoverableTabs(val tag: String) : UndoAction()

    /** Restores the tabs in [UndoHistoryState]. */
    object RestoreRecoverableTabs : UndoAction()
}

/** [BrowserAction] implementations related to updating the [TabSessionState] inside [BrowserState]. */
sealed class LastAccessAction : BrowserAction() {
    /**
     * Updates the [TabSessionState.lastAccess] timestamp of the tab with the given [tabId].
     *
     * @property tabId the ID of the tab to update.
     * @property lastAccess the value to signify when the tab was last accessed; defaults to [System.currentTimeMillis].
     */
    data class UpdateLastAccessAction(
        val tabId: String,
        val lastAccess: Long = System.currentTimeMillis(),
    ) : LastAccessAction()

    /**
     * Updates the [TabSessionState.lastVisibleAt] timestamp of the tab with the given [tabId].
     *
     * @property tabId the ID of the tab to update.
     * @property lastVisibleAt the timestamp when the tab was last visible to the user.
     */
    data class UpdateLastVisibleAtAction(
        val tabId: String,
        val lastVisibleAt: Long,
    ) : LastAccessAction()

    /**
     * Updates [TabSessionState.lastMediaAccessState] for when media started playing in the tab identified by [tabId].
     *
     * @property tabId the ID of the tab to update.
     * @property lastMediaAccess the value to signify when the tab last started playing media. Defaults to
     *   [System.currentTimeMillis].
     */
    data class UpdateLastMediaAccessAction(
        val tabId: String,
        val lastMediaAccess: Long = System.currentTimeMillis(),
    ) : LastAccessAction()

    /**
     * Updates [TabSessionState.lastMediaAccessState] when the media session of this tab is deactivated.
     *
     * @property tabId the ID of the tab to update.
     */
    data class ResetLastMediaSessionAction(val tabId: String) : LastAccessAction()
}

/** [BrowserAction] implementations related to updating [BrowserState.customTabs]. */
sealed class CustomTabListAction : BrowserAction() {
    /**
     * Adds a new [CustomTabSessionState] to [BrowserState.customTabs].
     *
     * @property tab the [CustomTabSessionState] to add.
     */
    data class AddCustomTabAction(val tab: CustomTabSessionState) : CustomTabListAction()

    /**
     * Removes an existing [CustomTabSessionState] to [BrowserState.customTabs].
     *
     * @property tabId the ID of the custom tab to remove.
     */
    data class RemoveCustomTabAction(val tabId: String) : CustomTabListAction()

    /** Converts an existing [CustomTabSessionState] to a regular/normal [TabSessionState]. */
    data class TurnCustomTabIntoNormalTabAction(val tabId: String) : CustomTabListAction()

    /** Removes all custom tabs [TabSessionState]s. */
    object RemoveAllCustomTabsAction : CustomTabListAction()
}

/**
 * [BrowserAction] implementations related to updating the [ContentState] of a single [SessionState] inside
 * [BrowserState].
 */
sealed class ContentAction : BrowserAction() {
    /** Removes the icon of the [ContentState] with the given [sessionId]. */
    data class RemoveIconAction(val sessionId: String) : ContentAction()

    /** Updates the URL of the [ContentState] with the given [sessionId]. */
    data class UpdateUrlAction(
        val sessionId: String,
        val url: String,
        val hasUserGesture: Boolean = false,
    ) : ContentAction()

    /** Updates the progress of the [ContentState] with the given [sessionId]. */
    data class UpdateProgressAction(val sessionId: String, val progress: Int) : ContentAction()

    /** Updates permissions highlights of the [ContentState] with the given [sessionId]. */
    sealed class UpdatePermissionHighlightsStateAction : ContentAction() {
        /** Updates the notificationChanged property of the [PermissionHighlightsState] with the given [tabId]. */
        data class NotificationChangedAction(val tabId: String, val value: Boolean) :
            UpdatePermissionHighlightsStateAction()

        /** Updates the cameraChanged property of the [PermissionHighlightsState] with the given [tabId]. */
        data class CameraChangedAction(val tabId: String, val value: Boolean) : UpdatePermissionHighlightsStateAction()

        /** Updates the locationChanged property of the [PermissionHighlightsState] with the given [tabId]. */
        data class LocationChangedAction(val tabId: String, val value: Boolean) :
            UpdatePermissionHighlightsStateAction()

        /** Updates the microphoneChanged property of the [PermissionHighlightsState] with the given [tabId]. */
        data class MicrophoneChangedAction(val tabId: String, val value: Boolean) :
            UpdatePermissionHighlightsStateAction()

        /** Updates the persistentStorageChanged property of the [PermissionHighlightsState] with the given [tabId]. */
        data class PersistentStorageChangedAction(val tabId: String, val value: Boolean) :
            UpdatePermissionHighlightsStateAction()

        /**
         * Updates the mediaKeySystemAccessChanged property of the [PermissionHighlightsState] with the given [tabId].
         */
        data class MediaKeySystemAccesChangedAction(val tabId: String, val value: Boolean) :
            UpdatePermissionHighlightsStateAction()

        /**
         * Updates the [PermissionHighlightsState.localDeviceAccessChanged] property with the given [tabId]
         *
         * @property tabId The affected tab id
         * @property value The value indicating whether or not the local device access permission has changed
         */
        data class LocalDeviceAccessChangedAction(val tabId: String, val value: Boolean) :
            UpdatePermissionHighlightsStateAction()

        /**
         * Updates the [PermissionHighlightsState.localNetworkAccessChanged] property with the given [tabId]
         *
         * @property tabId The affected tab id
         * @property value The value indicating whether or not the local network access permission has changed
         */
        data class LocalNetworkAccessChangedAction(val tabId: String, val value: Boolean) :
            UpdatePermissionHighlightsStateAction()

        /** Updates the autoPlayAudibleChanged property of the [PermissionHighlightsState] with the given [tabId]. */
        data class AutoPlayAudibleChangedAction(val tabId: String, val value: Boolean) :
            UpdatePermissionHighlightsStateAction()

        /** Updates the autoPlayInaudibleChanged property of the [PermissionHighlightsState] with the given [tabId]. */
        data class AutoPlayInAudibleChangedAction(val tabId: String, val value: Boolean) :
            UpdatePermissionHighlightsStateAction()

        /** Updates the autoPlayAudibleBlocking property of the [PermissionHighlightsState] with the given [tabId]. */
        data class AutoPlayAudibleBlockingAction(val tabId: String, val value: Boolean) :
            UpdatePermissionHighlightsStateAction()

        /** Updates the autoPlayInaudibleBlocking property of the [PermissionHighlightsState] with the given [tabId]. */
        data class AutoPlayInAudibleBlockingAction(val tabId: String, val value: Boolean) :
            UpdatePermissionHighlightsStateAction()

        /** Updates permissions highlights of the [ContentState] with the given [tabId] to its default value. */
        data class Reset(val tabId: String) : UpdatePermissionHighlightsStateAction()
    }

    /** Updates the title of the [ContentState] with the given [sessionId]. */
    data class UpdateTitleAction(val sessionId: String, val title: String) : ContentAction()

    /** Updates the preview image URL of the [ContentState] with the given [sessionId]. */
    data class UpdatePreviewImageAction(val sessionId: String, val previewImageUrl: String) : ContentAction()

    /** Updates the loading state of the [ContentState] with the given [sessionId]. */
    data class UpdateLoadingStateAction(val sessionId: String, val loading: Boolean) : ContentAction()

    /** Updates the refreshCanceled state of the [ContentState] with the given [sessionId]. */
    data class UpdateRefreshCanceledStateAction(val sessionId: String, val refreshCanceled: Boolean) : ContentAction()

    /** Updates the search terms of the [ContentState] with the given [sessionId]. */
    data class UpdateSearchTermsAction(val sessionId: String, val searchTerms: String) : ContentAction()

    /**
     * Updates the isSearch state and optionally the search engine name of the [ContentState] with the given
     * [sessionId].
     */
    data class UpdateIsSearchAction(
        val sessionId: String,
        val isSearch: Boolean,
        val searchEngineName: String? = null,
    ) : ContentAction()

    /** Updates the [SecurityInfo] of the [ContentState] with the given [sessionId]. */
    data class UpdateSecurityInfoAction(
        val sessionId: String,
        val securityInfo: SecurityInfo,
    ) : ContentAction()

    /** Updates the icon of the [ContentState] with the given [sessionId]. */
    data class UpdateIconAction(val sessionId: String, val pageUrl: String, val icon: Bitmap) : ContentAction()

    /** Updates the thumbnail of the [ContentState] with the given [sessionId]. */
    data class UpdateThumbnailAction(val sessionId: String, val thumbnail: Bitmap) : ContentAction()

    /** Updates the [DownloadState] of the [ContentState] with the given [sessionId]. */
    data class UpdateDownloadAction(val sessionId: String, val download: DownloadState) : ContentAction()

    /**
     * Closes the [DownloadState.response] of the [ContentState.download] and removes the [DownloadState] of the
     * [ContentState] with the given [sessionId].
     */
    data class CancelDownloadAction(val sessionId: String, val downloadId: String) : ContentAction()

    /** Removes the [DownloadState] of the [ContentState] with the given [sessionId]. */
    data class ConsumeDownloadAction(val sessionId: String, val downloadId: String) : ContentAction()

    /** Updates the [HitResult] of the [ContentState] with the given [sessionId]. */
    data class UpdateHitResultAction(val sessionId: String, val hitResult: HitResult) : ContentAction()

    /** Removes the [HitResult] of the [ContentState] with the given [sessionId]. */
    data class ConsumeHitResultAction(val sessionId: String) : ContentAction()

    /** Updates the [PromptRequest] of the [ContentState] with the given [sessionId]. */
    data class UpdatePromptRequestAction(val sessionId: String, val promptRequest: PromptRequest) : ContentAction()

    /** Removes the [PromptRequest] of the [ContentState] with the given [sessionId]. */
    data class ConsumePromptRequestAction(val sessionId: String, val promptRequest: PromptRequest) : ContentAction()

    /** Replaces a prompt request from [ContentState] with [promptRequest] based on the [previousPromptUid]. */
    data class ReplacePromptRequestAction(
        val sessionId: String,
        val previousPromptUid: String,
        val promptRequest: PromptRequest,
    ) : ContentAction()

    /** Adds a [FindResultState] to the [ContentState] with the given [sessionId]. */
    data class AddFindResultAction(val sessionId: String, val findResult: FindResultState) : ContentAction()

    /** Removes all [FindResultState]s of the [ContentState] with the given [sessionId]. */
    data class ClearFindResultsAction(val sessionId: String) : ContentAction()

    /** Updates the [WindowRequest] of the [ContentState] with the given [sessionId]. */
    data class UpdateWindowRequestAction(val sessionId: String, val windowRequest: WindowRequest) : ContentAction()

    /** Removes the [WindowRequest] of the [ContentState] with the given [sessionId]. */
    data class ConsumeWindowRequestAction(val sessionId: String) : ContentAction()

    /** Updates the [SearchRequest] of the [ContentState] with the given [sessionId]. */
    data class UpdateSearchRequestAction(val sessionId: String, val searchRequest: SearchRequest) : ContentAction()

    /** Removes the [SearchRequest] of the [ContentState] with the given [sessionId]. */
    data class ConsumeSearchRequestAction(val sessionId: String) : ContentAction()

    /** Updates [fullScreenEnabled] with the given [sessionId]. */
    data class FullScreenChangedAction(val sessionId: String, val fullScreenEnabled: Boolean) : ContentAction()

    /** Updates [pipEnabled] with the given [sessionId]. */
    data class PictureInPictureChangedAction(val sessionId: String, val pipEnabled: Boolean) : ContentAction()

    /**
     * Updates the [layoutInDisplayCutoutMode] with the given [sessionId].
     *
     * @property sessionId the ID of the session
     * @property layoutInDisplayCutoutMode value of defined in
     *   https://developer.android.com/reference/android/view/WindowManager.LayoutParams#layoutInDisplayCutoutMode
     */
    data class ViewportFitChangedAction(val sessionId: String, val layoutInDisplayCutoutMode: Int) : ContentAction()

    /** Updates the [ContentState] of the given [sessionId] to indicate whether or not a back navigation is possible. */
    data class UpdateBackNavigationStateAction(val sessionId: String, val canGoBack: Boolean) : ContentAction()

    /**
     * Updates the [ContentState] of the given [sessionId] to indicate whether the first contentful paint has happened.
     */
    data class UpdateFirstContentfulPaintStateAction(
        val sessionId: String,
        val firstContentfulPaint: Boolean,
    ) : ContentAction()

    /**
     * Updates the [ContentState] of the given [sessionId] to indicate whether or not a forward navigation is possible.
     */
    data class UpdateForwardNavigationStateAction(
        val sessionId: String,
        val canGoForward: Boolean,
    ) : ContentAction()

    /** Updates the [WebAppManifest] of the [ContentState] with the given [sessionId]. */
    data class UpdateWebAppManifestAction(
        val sessionId: String,
        val webAppManifest: WebAppManifest,
    ) : ContentAction()

    /** Removes the [WebAppManifest] of the [ContentState] with the given [sessionId]. */
    data class RemoveWebAppManifestAction(val sessionId: String) : ContentAction()

    /** Updates the [ContentState] of the given [sessionId] to indicate the current history state. */
    data class UpdateHistoryStateAction(
        val sessionId: String,
        val historyList: List<HistoryItem>,
        val currentIndex: Int,
    ) : ContentAction()

    /** Updates the [LoadRequestState] of the [ContentState] with the given [sessionId]. */
    data class UpdateLoadRequestAction(val sessionId: String, val loadRequest: LoadRequestState) : ContentAction()

    /** Adds a new content permission request to the [ContentState] list. */
    data class UpdatePermissionsRequest(
        val sessionId: String,
        val permissionRequest: PermissionRequest,
    ) : ContentAction()

    /** Deletes a content permission request from the [ContentState] list. */
    data class ConsumePermissionsRequest(
        val sessionId: String,
        val permissionRequest: PermissionRequest,
    ) : ContentAction()

    /** Removes all content permission requests from the [ContentState] list. */
    data class ClearPermissionRequests(val sessionId: String) : ContentAction()

    /** Adds a new app permission request to the [ContentState] list. */
    data class UpdateAppPermissionsRequest(
        val sessionId: String,
        val appPermissionRequest: PermissionRequest,
    ) : ContentAction()

    /** Deletes an app permission request from the [ContentState] list. */
    data class ConsumeAppPermissionsRequest(
        val sessionId: String,
        val appPermissionRequest: PermissionRequest,
    ) : ContentAction()

    /** Removes all app permission requests from the [ContentState] list. */
    data class ClearAppPermissionRequests(val sessionId: String) : ContentAction()

    /** Sets the list of active recording devices (webcam, microphone, ..) used by web content. */
    data class SetRecordingDevices(
        val sessionId: String,
        val devices: List<RecordingDevice>,
    ) : ContentAction()

    /** Updates the [ContentState] of the given [sessionId] to indicate whether or not desktop mode is enabled. */
    data class UpdateTabDesktopMode(val sessionId: String, val enabled: Boolean) : ContentAction()

    /** Updates the [AppIntentState] of the [ContentState] with the given [sessionId]. */
    data class UpdateAppIntentAction(val sessionId: String, val appIntent: AppIntentState) : ContentAction()

    /** Removes the [AppIntentState] of the [ContentState] with the given [sessionId]. */
    data class ConsumeAppIntentAction(val sessionId: String) : ContentAction()

    /** Updates whether the toolbar should be forced to expand or have it follow the default behavior. */
    data class UpdateExpandedToolbarStateAction(val sessionId: String, val expanded: Boolean) : ContentAction()

    /**
     * Updates the [ContentState] with the provided [tabId] to the appropriate priority based on any existing form data.
     */
    data class UpdateHasFormDataAction(
        val tabId: String,
        val containsFormData: Boolean,
        val adjustPriority: Boolean = true,
    ) : ContentAction()

    /** Lowers priority of the [tabId] to default after certain period of time */
    data class UpdatePriorityToDefaultAfterTimeoutAction(val tabId: String) : ContentAction()

    /** Indicates the given [tabId] was unable to be checked for form data. */
    data class CheckForFormDataExceptionAction(val tabId: String, val throwable: Throwable) : ContentAction()

    /** Inform that the tab with [tabId] started rendering a pdf. */
    data class EnteredPdfViewer(val tabId: String) : ContentAction()

    /** Inform that the tab with [tabId] stopped rendering a pdf. */
    data class ExitedPdfViewer(val tabId: String) : ContentAction()
}

/** [BrowserAction] implementations related to translating a web content page. */
sealed class TranslationsAction : BrowserAction() {

    /**
     * Requests that the initialization data for the global translations engine state be fetched from the translations
     * engine and set on [BrowserState.translationEngine].
     */
    object InitTranslationsBrowserState : TranslationsAction()

    /**
     * Indicates that the translations engine expects the user may want to translate the page on the given [tabId].
     *
     * For example, could be used to show toolbar UI that translations are an option.
     *
     * @property tabId The ID of the tab the [EngineSession] should be linked to.
     */
    data class TranslateExpectedAction(override val tabId: String) : TranslationsAction(), ActionWithTab

    /**
     * Indicates that the translations engine suggests the user should be notified of the ability to translate on the
     * given [tabId].
     *
     * For example, could be used to show a reminder UI popup or a star beside the toolbar UI to strongly signal that
     * translations are an option.
     *
     * @property tabId The ID of the tab the [EngineSession] should be linked to.
     * @property isOfferTranslate If the engine should offer translating the page to the user.
     */
    data class TranslateOfferAction(
        override val tabId: String,
        val isOfferTranslate: Boolean,
    ) : TranslationsAction(), ActionWithTab

    /**
     * Indicates the translation state on the given [tabId].
     *
     * This provides the translations engine state. Not to be confused with the browser engine state of the translations
     * component.
     *
     * @property tabId The ID of the tab the [EngineSession] should be linked to.
     * @property translationEngineState The state of the translation engine for the page.
     */
    data class TranslateStateChangeAction(
        override val tabId: String,
        val translationEngineState: TranslationEngineState,
    ) : TranslationsAction(), ActionWithTab

    /**
     * Used to translate the page for a given [tabId].
     *
     * @property tabId The ID of the tab the [EngineSession] should be linked to.
     * @property fromLanguage The BCP 47 language tag that the page should be translated from.
     * @property toLanguage The BCP 47 language tag that the page should be translated to.
     * @property options Options for how the translation should be processed.
     */
    data class TranslateAction(
        override val tabId: String,
        val fromLanguage: String,
        val toLanguage: String,
        val options: TranslationOptions?,
    ) : TranslationsAction(), ActionWithTab

    /**
     * Indicates the given [tabId] should restore the original pre-translated content.
     *
     * @property tabId The ID of the tab the [EngineSession] should be linked to.
     */
    data class TranslateRestoreAction(override val tabId: String) : TranslationsAction(), ActionWithTab

    /**
     * Fetch the translation download size for the given [tabId]. Will use the specified [fromLanguage] and [toLanguage]
     * to query the download size.
     *
     * @property tabId The ID of the tab the [EngineSession] should set the state on.
     * @property fromLanguage The from [Language] in the translation pair.
     * @property toLanguage The to [Language] in the translation pair.
     */
    data class FetchTranslationDownloadSizeAction(
        override val tabId: String,
        val fromLanguage: Language,
        val toLanguage: Language,
    ) : TranslationsAction(), ActionWithTab

    /**
     * Set the [TranslationDownloadSize] for the given [tabId].
     *
     * @property tabId The ID of the tab the [EngineSession] should set the state on.
     * @property translationSize The [TranslationDownloadSize] that contains a to/from translations pair and a download
     *   size.
     */
    data class SetTranslationDownloadSizeAction(
        override val tabId: String,
        val translationSize: TranslationDownloadSize,
    ) : TranslationsAction(), ActionWithTab

    /**
     * Indicates the given [tabId] was successful in translating or restoring the page or acquiring a necessary
     * resource.
     *
     * @property tabId The ID of the tab the [EngineSession] should be linked to.
     * @property operation The translation operation that was successful.
     */
    data class TranslateSuccessAction(
        override val tabId: String,
        val operation: TranslationOperation,
    ) : TranslationsAction(), ActionWithTab

    /**
     * Indicates the given [tabId] was unable to translate or restore the page or acquire a necessary resource.
     *
     * @property tabId The ID of the tab the [EngineSession] should be linked to.
     * @property operation The translation operation that failed.
     * @property translationError The error that occurred.
     */
    data class TranslateExceptionAction(
        override val tabId: String,
        val operation: TranslationOperation,
        val translationError: TranslationError,
    ) : TranslationsAction(), ActionWithTab

    /**
     * Indicates an app level translations error occurred and to set the [TranslationError] on
     * [BrowserState.translationEngine].
     *
     * @property error The [TranslationError] that occurred.
     */
    data class EngineExceptionAction(val error: TranslationError) : TranslationsAction()

    /**
     * Indicates that the given [operation] data should be fetched for the given [tabId].
     *
     * @property tabId The ID of the tab the [EngineSession] should be linked to. May be null to complete the operation
     *   on the current tab (when a tab is required for the operation) or when no session is associated with the
     *   request.
     * @property operation The translation operation that failed.
     */
    data class OperationRequestedAction(
        val tabId: String?,
        val operation: TranslationOperation,
    ) : TranslationsAction()

    /**
     * Sets whether the translations feature is enabled and should be shown to the user.
     *
     * @property isTranslationsEnabled Whether the translations feature is enabled.
     */
    data class SetTranslationsEnabledAction(val isTranslationsEnabled: Boolean) : TranslationsAction()

    /**
     * Sets whether the device architecture supports translations or not on [BrowserState.translationEngine].
     *
     * @property isEngineSupported If the engine supports translations on this device.
     */
    data class SetEngineSupportedAction(val isEngineSupported: Boolean) : TranslationsAction()

    /**
     * Sets the languages that are supported by the translations engine on the [BrowserState.translationEngine].
     *
     * @property supportedLanguages The languages the engine supports for translation.
     */
    data class SetSupportedLanguagesAction(val supportedLanguages: TranslationSupport?) : TranslationsAction()

    /**
     * Sets the given page settings on the page on the given [tabId]'s store.
     *
     * @property tabId The ID of the tab the [EngineSession] should be linked to.
     * @property pageSettings The new page settings.
     */
    data class SetPageSettingsAction(
        override val tabId: String,
        val pageSettings: TranslationPageSettings?,
    ) : TranslationsAction(), ActionWithTab

    /**
     * Indicates the translation processing state on the given [tabId].
     *
     * A translation is processing when the engine is actively working on performing the translation.
     *
     * @property tabId The ID of the tab the [EngineSession] should be linked to.
     * @property isProcessing Whether the translation is processing or not.
     */
    data class SetTranslateProcessingAction(
        override val tabId: String,
        val isProcessing: Boolean,
    ) : TranslationsAction(), ActionWithTab

    /**
     * Updates the specified page setting operation on the translation engine and ensures the final state on the given
     * [tabId]'s store remains in-sync.
     *
     * @property tabId The ID of the tab the [EngineSession] should be linked to.
     * @property operation The page setting update operation to perform.
     * @property setting The boolean value of the corresponding [operation].
     */
    data class UpdatePageSettingAction(
        override val tabId: String,
        val operation: TranslationPageSettingOperation,
        val setting: Boolean,
    ) : TranslationsAction(), ActionWithTab

    /**
     * Sets the translations offer setting on the global store. The translations offer setting controls when to offer a
     * translation on a page.
     *
     * See [SetPageSettingsAction] for setting the offer setting on the session store.
     *
     * @property offerTranslation The offer setting to set.
     */
    data class SetGlobalOfferTranslateSettingAction(val offerTranslation: Boolean) : TranslationsAction()

    /**
     * Updates the specified translation offer setting on the translation engine and ensures the final state on the
     * global store remains in-sync.
     *
     * See [UpdatePageSettingAction] for updating the offer setting on the session store.
     *
     * @property offerTranslation The offer setting to set.
     */
    data class UpdateGlobalOfferTranslateSettingAction(val offerTranslation: Boolean) : TranslationsAction()

    /**
     * Sets the map of BCP 47 language codes (key) and the [LanguageSetting] option (value).
     *
     * @property languageSettings A map containing a key of BCP 47 language code and its [LanguageSetting].
     */
    data class SetLanguageSettingsAction(val languageSettings: Map<String, LanguageSetting>) : TranslationsAction()

    /**
     * Updates the specified translation language setting on the translation engine and ensures the final state on the
     * global store remains in-sync.
     *
     * See [UpdatePageSettingAction] for updating the language setting on the session store.
     *
     * @property languageCode The BCP-47 language code to update.
     * @property setting The [LanguageSetting] for the language.
     */
    data class UpdateLanguageSettingsAction(
        val languageCode: String,
        val setting: LanguageSetting,
    ) : TranslationsAction()

    /**
     * Sets the list of sites that the user has opted to never translate.
     *
     * @property neverTranslateSites The never translate sites.
     */
    data class SetNeverTranslateSitesAction(val neverTranslateSites: List<String>) : TranslationsAction()

    /**
     * Remove from the list of sites the user has opted to never translate.
     *
     * @property origin A site origin URI that will have the specified never translate permission set.
     */
    data class RemoveNeverTranslateSiteAction(val origin: String) : TranslationsAction()

    /**
     * Sets the list of language machine learning translation models the translation engine has available.
     *
     * @property languageModels The list of language machine learning translation models.
     */
    data class SetLanguageModelsAction(val languageModels: List<LanguageModel>) : TranslationsAction()

    /**
     * Manages the language machine learning translation models the translation engine has available. Has options for
     * downloading and deleting models.
     *
     * @property options The operation to perform to manage the model.
     */
    data class ManageLanguageModelsAction(val options: ModelManagementOptions) : TranslationsAction()
}

/**
 * [BrowserAction] implementations related to updating the [TrackingProtectionState] of a single [SessionState] inside
 * [BrowserState].
 */
sealed class TrackingProtectionAction : BrowserAction() {
    /** Updates the [TrackingProtectionState.enabled] flag. */
    data class ToggleAction(val tabId: String, val enabled: Boolean) : TrackingProtectionAction()

    /** Updates the [TrackingProtectionState.ignoredOnTrackingProtection] flag. */
    data class ToggleExclusionListAction(val tabId: String, val excluded: Boolean) : TrackingProtectionAction()

    /** Adds a [Tracker] to the [TrackingProtectionState.blockedTrackers] list. */
    data class TrackerBlockedAction(val tabId: String, val tracker: Tracker) : TrackingProtectionAction()

    /** Adds a [Tracker] to the [TrackingProtectionState.loadedTrackers] list. */
    data class TrackerLoadedAction(val tabId: String, val tracker: Tracker) : TrackingProtectionAction()

    /** Clears the [TrackingProtectionState.blockedTrackers] and [TrackingProtectionState.blockedTrackers] lists. */
    data class ClearTrackersAction(val tabId: String) : TrackingProtectionAction()
}

/**
 * [BrowserAction] implementations related to updating [BrowserState.extensions] and [TabSessionState.extensionState].
 */
sealed class WebExtensionAction : BrowserAction() {
    /** Updates [BrowserState.extensions] to register the given [extension] as installed. */
    data class InstallWebExtensionAction(val extension: WebExtensionState) : WebExtensionAction()

    /** Updates [BrowserState.webExtensionPromptRequest] give the given [promptRequest]. */
    data class UpdatePromptRequestWebExtensionAction(val promptRequest: WebExtensionPromptRequest) :
        WebExtensionAction()

    /** Removes the actual [WebExtensionPromptRequest] of the [BrowserState]. */
    object ConsumePromptRequestWebExtensionAction : WebExtensionAction()

    /**
     * Adds [pendingUpdate], replacing any pending update of the same extension. The replaced update is not completed;
     * the caller must complete it.
     */
    data class UpdatePendingExtensionUpdateAction(val pendingUpdate: PendingExtensionUpdate) : WebExtensionAction()

    /** Removes the pending update of [extensionId] without completing it. */
    data class RemovePendingExtensionUpdateAction(val extensionId: String) : WebExtensionAction()

    /**
     * Removes all state of the uninstalled extension from [BrowserState.extensions] and
     * [TabSessionState.extensionState].
     */
    data class UninstallWebExtensionAction(val extensionId: String) : WebExtensionAction()

    /** Removes state of all extensions from [BrowserState.extensions] and [TabSessionState.extensionState]. */
    object UninstallAllWebExtensionsAction : WebExtensionAction()

    /** Updates the [WebExtensionState.enabled] flag. */
    data class UpdateWebExtensionEnabledAction(val extensionId: String, val enabled: Boolean) : WebExtensionAction()

    /** Updates the [WebExtensionState.allowedInPrivateBrowsing] flag. */
    data class UpdateWebExtensionAllowedInPrivateBrowsingAction(
        val extensionId: String,
        val allowed: Boolean,
    ) : WebExtensionAction()

    /** Updates the given [updatedExtension] in the [BrowserState.extensions]. */
    data class UpdateWebExtensionAction(val updatedExtension: WebExtensionState) : WebExtensionAction()

    /** Updates a browser action of a given [extensionId]. */
    data class UpdateBrowserAction(
        val extensionId: String,
        val browserAction: WebExtensionBrowserAction,
    ) : WebExtensionAction()

    /** Updates a page action of a given [extensionId]. */
    data class UpdatePageAction(
        val extensionId: String,
        val pageAction: WebExtensionPageAction,
    ) : WebExtensionAction()

    /** Keeps track of the last session used to display an extension action popup. */
    data class UpdatePopupSessionAction(
        val extensionId: String,
        val popupSessionId: String? = null,
        val popupSession: EngineSession? = null,
    ) : WebExtensionAction()

    /**
     * Passes url and title necessary for opening options page via [WebExtensionState]. And keeps track of the last
     * instance used to display an extension options page. optionsPageInstanceId keeps repeated requests distinguishable
     * when the observer misses the cleared state.
     */
    data class UpdateOptionsPageSessionAction(
        val extensionId: String,
        val optionsPageInstanceId: String,
        val optionsPageUrl: String,
        val extensionTranslatedName: String,
    ) : WebExtensionAction()

    /** Clears the state of an options page session. */
    data class ClearOptionsPageSession(val extensionId: String) : WebExtensionAction()

    /**
     * Updates a tab-specific browser action that belongs to the given [sessionId] and [extensionId] on the
     * [TabSessionState.extensionState].
     */
    data class UpdateTabBrowserAction(
        val sessionId: String,
        val extensionId: String,
        val browserAction: WebExtensionBrowserAction,
    ) : WebExtensionAction()

    /**
     * Updates a page action that belongs to the given [sessionId] and [extensionId] on the
     * [TabSessionState.extensionState].
     */
    data class UpdateTabPageAction(
        val sessionId: String,
        val extensionId: String,
        val pageAction: WebExtensionPageAction,
    ) : WebExtensionAction()

    /**
     * Updates the [BrowserState.activeWebExtensionTabId] to mark a tab active for web extensions e.g. to support
     * tabs.query({active: true}).
     */
    data class UpdateActiveWebExtensionTabAction(val activeWebExtensionTabId: String?) : WebExtensionAction()
}

/**
 * [BrowserAction] implementations related to updating the [EngineState] of a single [SessionState] inside
 * [BrowserState].
 */
sealed class EngineAction : BrowserAction() {
    /** Creates an [EngineSession] for the given [tabId] if none exists yet. */
    data class CreateEngineSessionAction(
        override val tabId: String,
        val skipLoading: Boolean = false,
        val followupAction: BrowserAction? = null,
        val includeParent: Boolean = false,
    ) : EngineAction(), ActionWithTab

    /** Loads the given [url] in the tab with the given [tabId]. */
    data class LoadUrlAction(
        override val tabId: String,
        val url: String,
        val flags: EngineSession.LoadUrlFlags = EngineSession.LoadUrlFlags.none(),
        val additionalHeaders: Map<String, String>? = null,
        val includeParent: Boolean = false,
        val textDirectiveUserActivation: Boolean = false,
    ) : EngineAction(), ActionWithTab

    /**
     * Indicates to observers that a [LoadUrlAction] was shortcutted and a direct load on the engine occurred instead.
     */
    data class OptimizedLoadUrlTriggeredAction(
        override val tabId: String,
        val url: String,
        val flags: EngineSession.LoadUrlFlags = EngineSession.LoadUrlFlags.none(),
        val additionalHeaders: Map<String, String>? = null,
    ) : EngineAction(), ActionWithTab

    /** Loads [data] in the tab with the given [tabId]. */
    data class LoadDataAction(
        override val tabId: String,
        val data: String,
        val mimeType: String = "text/html",
        val encoding: String = "UTF-8",
    ) : EngineAction(), ActionWithTab

    /** Reloads the tab with the given [tabId]. */
    data class ReloadAction(
        override val tabId: String,
        val flags: EngineSession.LoadUrlFlags = EngineSession.LoadUrlFlags.none(),
    ) : EngineAction(), ActionWithTab

    /** Navigates back in the tab with the given [tabId]. */
    data class GoBackAction(
        override val tabId: String,
        val userInteraction: Boolean = true,
    ) : EngineAction(), ActionWithTab

    /** Navigates forward in the tab with the given [tabId]. */
    data class GoForwardAction(
        override val tabId: String,
        val userInteraction: Boolean = true,
    ) : EngineAction(), ActionWithTab

    /** Navigates to the specified index in the history of the tab with the given [tabId]. */
    data class GoToHistoryIndexAction(
        override val tabId: String,
        val index: Int,
    ) : EngineAction(), ActionWithTab

    /** Enables/disables desktop mode in the tabs with the given [tabId]. */
    data class ToggleDesktopModeAction(
        override val tabId: String,
        val enable: Boolean,
    ) : EngineAction(), ActionWithTab

    /** Exits fullscreen mode in the tabs with the given [tabId]. */
    data class ExitFullScreenModeAction(override val tabId: String) : EngineAction(), ActionWithTab

    /** Indicates the given [tabId] is to print the page content. */
    data class PrintContentAction(override val tabId: String) : EngineAction(), ActionWithTab

    /** Indicates the given [tabId] completed printing the page content. */
    data class PrintContentCompletedAction(override val tabId: String) : EngineAction(), ActionWithTab

    /**
     * Indicates the given [tabId] was unable to print the page content. [isPrint] indicates if it is in response to a
     * print (true) or PDF saving (false).
     */
    data class PrintContentExceptionAction(
        override val tabId: String,
        val isPrint: Boolean,
        val throwable: Throwable,
    ) : EngineAction(), ActionWithTab

    /** Navigates back in the tab with the given [tabId]. */
    data class SaveToPdfAction(override val tabId: String) : EngineAction(), ActionWithTab

    /** Indicates the given [tabId] was successful in generating a requested PDF page. */
    data class SaveToPdfCompleteAction(override val tabId: String) : EngineAction(), ActionWithTab

    /** Indicates the given [tabId] was unable to generate a requested save to PDF page. */
    data class SaveToPdfExceptionAction(
        override val tabId: String,
        val throwable: Throwable,
    ) : EngineAction(), ActionWithTab

    /** Clears browsing data for the tab with the given [tabId]. */
    data class ClearDataAction(
        override val tabId: String,
        val data: Engine.BrowsingData,
    ) : EngineAction(), ActionWithTab

    /**
     * Attaches the provided [EngineSession] to the session with the provided [tabId].
     *
     * @property tabId The ID of the tab the [EngineSession] should be linked to.
     * @property engineSession The [EngineSession] that should be linked to the tab.
     * @property timestamp Timestamp (milliseconds) of when the linking has happened (By default set to
     *   [SystemClock.elapsedRealtime].
     */
    data class LinkEngineSessionAction(
        override val tabId: String,
        val engineSession: EngineSession,
        val timestamp: Long = Clock.elapsedRealtime(),
        val skipLoading: Boolean = false,
        val includeParent: Boolean = false,
    ) : EngineAction(), ActionWithTab

    /** Suspends the [EngineSession] of the session with the provided [tabId]. */
    data class SuspendEngineSessionAction(override val tabId: String) : EngineAction(), ActionWithTab

    /**
     * Marks the [EngineSession] of the session with the provided [tabId] as killed (The matching content process was
     * killed).
     */
    data class KillEngineSessionAction(override val tabId: String) : EngineAction(), ActionWithTab

    /** Detaches the current [EngineSession] from the session with the provided [tabId]. */
    data class UnlinkEngineSessionAction(override val tabId: String) : EngineAction(), ActionWithTab

    /** Updates the [EngineState.initializing] flag of the session with the provided [tabId]. */
    data class UpdateEngineSessionInitializingAction(
        override val tabId: String,
        val initializing: Boolean,
    ) : EngineAction(), ActionWithTab

    /** Updates the [EngineSessionState] of the session with the provided [tabId]. */
    data class UpdateEngineSessionStateAction(
        override val tabId: String,
        val engineSessionState: EngineSessionState,
    ) : EngineAction(), ActionWithTab

    /** Updates the [EngineSession.Observer] of the session with the provided [tabId]. */
    data class UpdateEngineSessionObserverAction(
        override val tabId: String,
        val engineSessionObserver: EngineSession.Observer,
    ) : EngineAction(), ActionWithTab

    /** Purges the back/forward history of all tabs and custom tabs. */
    object PurgeHistoryAction : EngineAction()

    /** Flushes the most recent state of the session with the provided [tabId]. */
    data class FlushEngineSessionStateAction(override val tabId: String) : EngineAction(), ActionWithTab
}

/** [BrowserAction] implementations to react to crashes. */
sealed class CrashAction : BrowserAction() {
    /** Updates the [SessionState] of the session with provided ID to mark it as crashed. */
    data class SessionCrashedAction(val tabId: String) : CrashAction()

    /** Updates the [SessionState] of the session with provided ID to mark it as restored. */
    data class RestoreCrashedSessionAction(val tabId: String) : CrashAction()
}

/**
 * [BrowserAction] implementations related to updating the [ReaderState] of a single [TabSessionState] inside
 * [BrowserState].
 */
sealed class ReaderAction : BrowserAction() {
    /** Updates the [ReaderState.readerable] flag. */
    data class UpdateReaderableAction(val tabId: String, val readerable: Boolean) : ReaderAction()

    /** Updates the [ReaderState.active] flag. */
    data class UpdateReaderActiveAction(val tabId: String, val active: Boolean) : ReaderAction()

    /** Updates the [ReaderState.checkRequired] flag. */
    data class UpdateReaderableCheckRequiredAction(val tabId: String, val checkRequired: Boolean) : ReaderAction()

    /** Updates the [ReaderState.connectRequired] flag. */
    data class UpdateReaderConnectRequiredAction(val tabId: String, val connectRequired: Boolean) : ReaderAction()

    /** Updates the [ReaderState.baseUrl]. */
    data class UpdateReaderBaseUrlAction(val tabId: String, val baseUrl: String) : ReaderAction()

    /** Updates the [ReaderState.activeUrl]. */
    data class UpdateReaderActiveUrlAction(val tabId: String, val activeUrl: String) : ReaderAction()

    /** Updates the [ReaderState.scrollY]. */
    data class UpdateReaderScrollYAction(val tabId: String, val scrollY: Int) : ReaderAction()

    /** Clears the [ReaderState.activeUrl]. */
    data class ClearReaderActiveUrlAction(val tabId: String) : ReaderAction()
}

/** [BrowserAction] implementations related to updating the [MediaSessionState]. */
sealed class MediaSessionAction : BrowserAction() {
    /** Activates [MediaSession] owned by the tab with id [tabId]. */
    data class ActivatedMediaSessionAction(
        val tabId: String,
        val mediaSessionController: MediaSession.Controller,
    ) : MediaSessionAction()

    /** Activates [MediaSession] owned by the tab with id [tabId]. */
    data class DeactivatedMediaSessionAction(val tabId: String) : MediaSessionAction()

    /** Updates the [MediaSession.Metadata] owned by the tab with id [tabId]. */
    data class UpdateMediaMetadataAction(
        val tabId: String,
        val metadata: MediaSession.Metadata,
    ) : MediaSessionAction()

    /** Updates the [MediaSession.PlaybackState] owned by the tab with id [tabId]. */
    data class UpdateMediaPlaybackStateAction(
        val tabId: String,
        val playbackState: MediaSession.PlaybackState,
    ) : MediaSessionAction()

    /** Updates the [MediaSession.Feature] owned by the tab with id [tabId]. */
    data class UpdateMediaFeatureAction(
        val tabId: String,
        val features: MediaSession.Feature,
    ) : MediaSessionAction()

    /** Updates the [MediaSession.PositionState] owned by the tab with id [tabId]. */
    data class UpdateMediaPositionStateAction(
        val tabId: String,
        val positionState: MediaSession.PositionState,
    ) : MediaSessionAction()

    /** Updates the [muted] owned by the tab with id [tabId]. */
    data class UpdateMediaMutedAction(
        val tabId: String,
        val muted: Boolean,
    ) : MediaSessionAction()

    /** Updates the [fullScreen] and [MediaSession.ElementMetadata] owned by the tab with id [tabId]. */
    data class UpdateMediaFullscreenAction(
        val tabId: String,
        val fullScreen: Boolean,
        val elementMetadata: MediaSession.ElementMetadata?,
    ) : MediaSessionAction()

    /** Updates the [MediaSession.AudioSessionType] claimed by the tab with id [tabId]. */
    data class UpdateMediaAudioSessionTypeAction(
        val tabId: String,
        val audioSessionType: MediaSession.AudioSessionType,
    ) : MediaSessionAction()
}

/** [BrowserAction] implementations related to updating the global download state. */
sealed class DownloadAction : BrowserAction() {
    /** Updates the [BrowserState] to track the provided [download] as added. */
    data class AddDownloadAction(val download: DownloadState) : DownloadAction()

    /**
     * Updates the [BrowserState] to remove the download with the provided [downloadId].
     *
     * @param downloadId The ID of the download to remove.
     * @param removeFromDisk If true, forcibly deletes the file from storage. If false, only removes from history. If
     *   null, falls back to the global user preference.
     */
    data class RemoveDownloadAction(val downloadId: String, val removeFromDisk: Boolean? = null) : DownloadAction()

    /** Updates the [BrowserState] to remove all downloads. */
    object RemoveAllDownloadsAction : DownloadAction()

    /** Updates the provided [download] on the [BrowserState]. */
    data class UpdateDownloadAction(val download: DownloadState) : DownloadAction()

    /** Mark the download notification of the provided [downloadId] as removed from the status bar. */
    data class DismissDownloadNotificationAction(val downloadId: String) : DownloadAction()

    /** Restores the [BrowserState.downloads] state from the storage. */
    object RestoreDownloadsStateAction : DownloadAction()

    /** Restores the given [download] from the storage. */
    data class RestoreDownloadStateAction(val download: DownloadState) : DownloadAction()

    /**
     * [BrowserAction] to remove completed and cancelled downloads from the storage that no longer exist on disk.
     *
     * This action is used to clean up the download storage by removing entries for files that have been deleted or
     * moved from their original download location.
     */
    data object RemoveDeletedDownloads : DownloadAction()
}

/**
 * [BrowserAction] implementations related to updating the session state of internet or local resources to be shared.
 */
sealed class ShareResourceAction : BrowserAction() {
    /** Starts the sharing process of an Internet or local resource. */
    data class AddShareAction(
        val tabId: String,
        val resource: ShareResourceState,
    ) : ShareResourceAction()

    /**
     * Previous share request is considered completed. File was successfully shared with other apps / user may have
     * aborted the process or the operation may have failed. In either case the previous share request is considered
     * completed.
     */
    data class ConsumeShareAction(val tabId: String) : ShareResourceAction()
}

/** [BrowserAction] implementations related to updating the session state of internet resources to be copied. */
sealed class CopyInternetResourceAction : BrowserAction() {
    /** Starts the copying process of an Internet resource. */
    data class AddCopyAction(
        val tabId: String,
        val internetResource: ShareResourceState.InternetResource,
    ) : CopyInternetResourceAction()

    /**
     * Previous copy request is considered completed. File was successfully copied / user may have aborted the process
     * or the operation may have failed. In either case the previous copy request is considered completed.
     */
    data class ConsumeCopyAction(val tabId: String) : CopyInternetResourceAction()
}

/** [BrowserAction] implementations related to updating [BrowserState.containers] */
sealed class ContainerAction : BrowserAction() {
    /** Updates [BrowserState.containers] to register the given added [container]. */
    data class AddContainerAction(val container: ContainerState) : ContainerAction()

    /** Updates [BrowserState.containers] to register the given list of [containers]. */
    data class AddContainersAction(val containers: List<ContainerState>) : ContainerAction()

    /** Removes all state of the removed container from [BrowserState.containers]. */
    data class RemoveContainerAction(val contextId: String) : ContainerAction()
}

/** [BrowserAction] implementations related to updating [TabSessionState.historyMetadata]. */
sealed class HistoryMetadataAction : BrowserAction() {

    /** Associates a tab with a history metadata record described by the provided [historyMetadataKey]. */
    data class SetHistoryMetadataKeyAction(
        val tabId: String,
        val historyMetadataKey: HistoryMetadataKey,
    ) : HistoryMetadataAction()

    /** Removes [searchTerm] (and referrer) from any history metadata associated with tabs. */
    data class DisbandSearchGroupAction(val searchTerm: String) : HistoryMetadataAction()
}

/** [BrowserAction] implementations related to updating search engines in [SearchState]. */
sealed class SearchAction : BrowserAction() {
    /** Refreshes the list of search engines. */
    object RefreshSearchEnginesAction : SearchAction()

    /**
     * Sets the [RegionState] (region of the user). distribution is a [String] that specifies a set of default search
     * engines if available
     */
    data class SetRegionAction(val regionState: RegionState, val distribution: String? = null) : SearchAction()

    /** Application Search Engines have finished loading from disk. */
    data class ApplicationSearchEnginesLoaded(val applicationSearchEngines: List<SearchEngine>) : SearchAction()

    /** Sets the list of search engines and default search engine IDs. */
    data class SetSearchEnginesAction(
        val regionSearchEngines: List<SearchEngine>,
        val customSearchEngines: List<SearchEngine>,
        val hiddenSearchEngines: List<SearchEngine>,
        val disabledSearchEngineIds: List<String>,
        val additionalSearchEngines: List<SearchEngine>,
        val additionalAvailableSearchEngines: List<SearchEngine>,
        val userSelectedSearchEngineId: String?,
        val userSelectedSearchEngineName: String?,
        val userSelectedPrivateSearchEngineId: String?,
        val userSelectedPrivateSearchEngineName: String?,
        val regionDefaultSearchEngineId: String,
        val regionSearchEnginesOrder: List<String>,
        val searchEnginesConfigurationId: Int?,
    ) : SearchAction()

    /** Indicates that a new search engines configuration is available for the application to use. */
    data class SearchConfigurationAvailabilityChanged(val isNewSearchConfigurationAvailable: Boolean) : SearchAction()

    /** Updates [BrowserState.search] to add/modify a custom [SearchEngine]. */
    data class UpdateCustomSearchEngineAction(val searchEngine: SearchEngine) : SearchAction()

    /** Updates [BrowserState.search] to remove a custom [SearchEngine]. */
    data class RemoveCustomSearchEngineAction(val searchEngineId: String) : SearchAction()

    /**
     * Updates [BrowserState.search] to update [SearchState.userSelectedSearchEngineId] and
     * [SearchState.userSelectedSearchEngineName].
     */
    data class SelectSearchEngineAction(
        val searchEngineId: String,
        val searchEngineName: String?,
    ) : SearchAction()

    /**
     * Updates [BrowserState.search] to update [SearchState.userSelectedPrivateSearchEngineId] and
     * [SearchState.userSelectedPrivateSearchEngineName].
     */
    data class SelectPrivateSearchEngineAction(
        val searchEngineId: String,
        val searchEngineName: String?,
    ) : SearchAction()

    /**
     * Clears the private browsing search engine override, causing it to fall back to the normal default search engine.
     */
    object ClearPrivateSearchEngineAction : SearchAction()

    /**
     * Shows a previously hidden, bundled search engine in [SearchState.regionSearchEngines] again and removes it from
     * [SearchState.hiddenSearchEngines].
     */
    data class ShowSearchEngineAction(val searchEngineId: String) : SearchAction()

    /**
     * Hides a bundled search engine in [SearchState.regionSearchEngines] and adds it to
     * [SearchState.hiddenSearchEngines] instead.
     */
    data class HideSearchEngineAction(val searchEngineId: String) : SearchAction()

    /**
     * Adds an additional search engine from [SearchState.additionalAvailableSearchEngines] to
     * [SearchState.additionalSearchEngines].
     */
    data class AddAdditionalSearchEngineAction(val searchEngineId: String) : SearchAction()

    /**
     * Removes and additional search engine from [SearchState.additionalSearchEngines] and adds it back to
     * [SearchState.additionalAvailableSearchEngines].
     */
    data class RemoveAdditionalSearchEngineAction(val searchEngineId: String) : SearchAction()

    /** Updates [SearchState.disabledSearchEngineIds] list inside [BrowserState.search]. */
    data class UpdateDisabledSearchEngineIdsAction(
        val searchEngineId: String,
        val isEnabled: Boolean,
    ) : SearchAction()

    /** Restores hidden engines from [SearchState.hiddenSearchEngines] back to [SearchState.regionSearchEngines] */
    object RestoreHiddenSearchEnginesAction : SearchAction()
}

/** [BrowserAction] implements setting and updating the distribution */
data class UpdateDistribution(val distributionId: String?) : BrowserAction()

/**
 * [BrowserAction] implementations for updating state needed for debugging. These actions should be carefully considered
 * before being used.
 *
 * Every action **should** be annotated with [DelicateAction] to bring consumers to attention that this is a delicate
 * action.
 */
sealed class DebugAction : BrowserAction() {

    /**
     * Updates the [TabSessionState.createdAt] timestamp of the tab with the given [tabId].
     *
     * @property tabId the ID of the tab to update.
     * @property createdAt the value to signify when the tab was created.
     */
    @DelicateAction data class UpdateCreatedAtAction(val tabId: String, val createdAt: Long) : DebugAction()
}

/** [BrowserAction] implementations related to the application lifecycle. */
sealed class AppLifecycleAction : BrowserAction() {

    /** The application has received an ON_RESUME event. */
    object ResumeAction : AppLifecycleAction()

    /** The application has received an ON_PAUSE event. */
    object PauseAction : AppLifecycleAction()
}

/** [BrowserAction] implementations related to updating the application's default desktop mode setting. */
sealed class DefaultDesktopModeAction : BrowserAction() {
    /** Toggles the global default for desktop browsing mode. */
    data object ToggleDesktopMode : DefaultDesktopModeAction()

    /** Updates the global default for desktop browsing mode. */
    data class DesktopModeUpdated(val newValue: Boolean) : DefaultDesktopModeAction()
}

/** [BrowserAction] implementations related to requesting system-level permissions. */
sealed class SystemPermissionRequestAction : BrowserAction() {
    /** Indicates that a system permission request is currently in progress. */
    object SystemPermissionStateRequestInProgress : SystemPermissionRequestAction()

    /** Indicates that there is no system permission request currently in progress. */
    object SystemPermissionStateRequestNotInProgress : SystemPermissionRequestAction()
}
