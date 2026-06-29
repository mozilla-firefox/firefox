/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.appstate

import com.google.android.material.snackbar.BaseTransientBottomBar.LENGTH_SHORT
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.sync.TabData
import mozilla.components.feature.protection.dashboard.TrackersBlockedCategory
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.lib.crash.Crash.NativeCodeCrash
import mozilla.components.lib.crash.store.CrashAction
import mozilla.components.lib.state.Action
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.MessageSurfaceId
import mozilla.components.service.pocket.PocketStory.ContentRecommendation
import mozilla.components.service.pocket.PocketStory.SponsoredContent
import org.mozilla.fenix.bookmarks.BookmarksGlobalResultReport
import org.mozilla.fenix.browser.StandardSnackbarError
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.setup.checklist.ChecklistItem
import org.mozilla.fenix.components.appstate.sports.SportsWidgetState
import org.mozilla.fenix.components.appstate.webcompat.WebCompatState
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.pocket.PocketImpression
import org.mozilla.fenix.home.pocket.PocketRecommendedStoriesCategory
import org.mozilla.fenix.home.pocket.PocketRecommendedStoriesSelectedCategory
import org.mozilla.fenix.home.pocket.controller.StoriesImpressionSource
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTab
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTabState
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem
import org.mozilla.fenix.home.sports.MatchCard
import org.mozilla.fenix.home.sports.SportCardErrorState
import org.mozilla.fenix.library.history.PendingDeletionHistory
import org.mozilla.fenix.messaging.MessagingState
import org.mozilla.fenix.wallpapers.Wallpaper

/**
 * [Action] implementation related to [AppStore].
 */
sealed class AppAction : Action {
    /**
     * Updates the [AppState.inactiveTabsExpanded] boolean
     *
     * @property expanded The updated boolean to [AppState.inactiveTabsExpanded]
     */
    data class UpdateInactiveExpanded(val expanded: Boolean) : AppAction()

    /**
     * Updates whether the native default browser prompt was shown to the user during this session.
     *
     * @property wasShown The updated boolean to [AppState.wasNativeDefaultBrowserPromptShown]
     * This will be true if the prompt was shown, otherwise false.
     */
    data class UpdateWasNativeDefaultBrowserPromptShown(val wasShown: Boolean) : AppAction()

    /**
     * Updates whether the first frame of the homescreen has been [drawn].
     */
    data class UpdateFirstFrameDrawn(val drawn: Boolean) : AppAction()

    /**
     * Updates whether the fox peek animation should play on the next homepage view.
     */
    data class UpdateShowFoxPeekAnimation(val ready: Boolean) : AppAction()
    data class AddNonFatalCrash(val crash: NativeCodeCrash) : AppAction()
    data class RemoveNonFatalCrash(val crash: NativeCodeCrash) : AppAction()
    object RemoveAllNonFatalCrashes : AppAction()

    data class Change(
        val topSites: List<TopSite>,
        val mode: BrowsingMode,
        val collections: List<TabCollection>,
        val recentTabs: List<RecentTab>,
        val bookmarks: List<Bookmark>,
        val recentHistory: List<RecentlyVisitedItem>,
        val recentSyncedTabState: RecentSyncedTabState,
    ) :
        AppAction()

    data class CollectionExpanded(val collection: TabCollection, val expand: Boolean) :
        AppAction()

    data class CollectionsChange(val collections: List<TabCollection>) : AppAction()

    /**
     * Action dispatched when the browsing mode changes inside the BrowsingModeManager.
     */
    data class BrowsingModeManagerModeChanged(val mode: BrowsingMode) : AppAction()
    data class TopSitesChange(val topSites: List<TopSite>) : AppAction()
    data class RecentTabsChange(val recentTabs: List<RecentTab>) : AppAction()
    data class RemoveRecentTab(val recentTab: RecentTab) : AppAction()

    /**
     * The orientation of the application has changed.
     */
    data class OrientationChange(val orientation: OrientationMode) : AppAction()

    /**
     * The list of bookmarks displayed on the home screen has changed.
     */
    data class BookmarksChange(val bookmarks: List<Bookmark>) : AppAction()

    /**
     * A bookmark has been removed from the home screen.
     */
    data class RemoveBookmark(val bookmark: Bookmark) : AppAction()
    data class RecentHistoryChange(val recentHistory: List<RecentlyVisitedItem>) : AppAction()
    data class RemoveRecentHistoryHighlight(val highlightUrl: String) : AppAction()
    data class DisbandSearchGroupAction(val searchTerm: String) : AppAction()

    /**
     * Adds a set of items marked for removal to the app state, to be hidden in the UI.
     */
    data class AddPendingDeletionSet(val historyItems: Set<PendingDeletionHistory>) : AppAction()

    /**
     * Removes a set of items, previously marked for removal, to be displayed again in the UI.
     */
    data class UndoPendingDeletionSet(val historyItems: Set<PendingDeletionHistory>) : AppAction()

    /**
     * Action dispatched when the user has authenticated with their account.
     */
    data object UserAccountAuthenticated : AppAction()

    /**
     * Updates the [RecentSyncedTabState] with the given [state].
     */
    data class RecentSyncedTabStateChange(val state: RecentSyncedTabState) : AppAction()

    /**
     * Add a [RecentSyncedTab] url to the homescreen blocklist and remove it
     * from the recent synced tabs list.
     */
    data class RemoveRecentSyncedTab(val syncedTab: RecentSyncedTab) : AppAction()

    /**
     * Action dispatched when the browser is deleting its data and quitting.
     */
    data object DeleteAndQuitStarted : AppAction()

    /**
     * Action dispatched when the current tab has been closed.
     *
     * @property isPrivate Whether the closed tab was private or not.
     */
    data class CurrentTabClosed(
        val isPrivate: Boolean,
    ) : AppAction()

    /**
     * Action dispatched when an URL has been copied to the clipboard.
     */
    data object URLCopiedToClipboard : AppAction()

    /**
     * Action dispatched when open in firefox action is selected from custom tab.
     */
    data object OpenInFirefoxStarted : AppAction()

    /**
     * Action dispatched when open in firefox action is completed.
     */
    data object OpenInFirefoxFinished : AppAction()

    /**
     * Updates whether Firefox is the default browser.
     *
     * @property isDefault The updated boolean to [AppState.isDefaultBrowser]
     */
    data class UpdateDefaultBrowserStatus(val isDefault: Boolean) : AppAction()

    /**
     * [Action]s related to interactions with the Messaging Framework.
     */
    sealed class MessagingAction : AppAction() {
        /**
         * Restores the [Message] state from the storage.
         */
        object Restore : MessagingAction()

        /**
         * Evaluates if a new messages should be shown to users.
         */
        data class Evaluate(val surface: MessageSurfaceId) : MessagingAction()

        /**
         * Updates [MessagingState.messageToShow] with the given [message].
         */
        data class UpdateMessageToShow(val message: Message) : MessagingAction()

        /**
         * Updates [MessagingState.messageToShow] with the given [message].
         */
        data class ConsumeMessageToShow(val surface: MessageSurfaceId) : MessagingAction()

        /**
         * Updates [MessagingState.messages] with the given [messages].
         */
        data class UpdateMessages(val messages: List<Message>) : MessagingAction()

        /**
         * Indicates the given [message] was clicked.
         */
        data class MessageClicked(val message: Message) : MessagingAction()

        /**
         * Indicates the given [message] was dismissed.
         */
        data class MessageDismissed(val message: Message) : MessagingAction()

        /**
         * Sealed class representing actions related to microsurveys within messaging functionality.
         */
        sealed class MicrosurveyAction : MessagingAction() {
            /**
             * Indicates that the microsurvey associated with the [id] has been completed.
             *
             * @property id The id message associated with the completed microsurvey.
             * @property answer The answer provided for the microsurvey.
             */
            data class Completed(val id: String, val answer: String) : MicrosurveyAction()

            /**
             * Indicates the microsurvey associated with this [id] has been started.
             *
             * @property id The id of the message associated with the started microsurvey.
             */
            data class Started(val id: String) : MicrosurveyAction()

            /**
             * Indicates the microsurvey associated with the [id] has been shown.
             *
             * @property id The id of the message associated with the shown microsurvey.
             */
            data class Shown(val id: String) : MicrosurveyAction()

            /**
             * Indicates the microsurvey associated with the [id] has been dismissed.
             *
             * @property id The id of the message associated with the microsurvey.
             */
            data class Dismissed(val id: String) : MicrosurveyAction()

            /**
             * Indicates the sent confirmation message for this microsurvey [id] has been shown.
             *
             * @property id The id of the message associated with the microsurvey.
             */
            data class SentConfirmationShown(val id: String) : MicrosurveyAction()

            /**
             * Indicates the privacy notice of microsurveys has been tapped.
             *
             * @property id The id of the message associated with the microsurvey.
             */
            data class OnPrivacyNoticeTapped(val id: String) : MicrosurveyAction()
        }
    }

    /**
     * [Action]s related to interactions with the wallpapers feature.
     */
    sealed class WallpaperAction : AppAction() {
        /**
         * Indicates that a different [wallpaper] was selected.
         */
        data class UpdateCurrentWallpaper(val wallpaper: Wallpaper) : WallpaperAction()

        /**
         * Indicates that the list of potential wallpapers has changed.
         */
        data class UpdateAvailableWallpapers(val wallpapers: List<Wallpaper>) : WallpaperAction()

        /**
         * Indicates a change in the download state of a wallpaper. Note that this is meant to be
         * used for full size images, not thumbnails.
         *
         * @property wallpaper The wallpaper that is being updated.
         * @property imageState The updated image state for the wallpaper.
         */
        data class UpdateWallpaperDownloadState(
            val wallpaper: Wallpaper,
            val imageState: Wallpaper.ImageFileState,
        ) : WallpaperAction()
    }

    /**
     * [AppAction] implementations related to the application lifecycle.
     */
    sealed class AppLifecycleAction : AppAction() {
        /**
         * The application has started.
         */
        object StartAction : AppLifecycleAction()

        /**
         * The application has received an ON_RESUME event.
         */
        object ResumeAction : AppLifecycleAction()

        /**
         * The application has received an ON_PAUSE event.
         */
        object PauseAction : AppLifecycleAction()
    }

    /**
     * State of standard error snackBar has changed.
     */
    data class UpdateStandardSnackbarErrorAction(
        val standardSnackbarError: StandardSnackbarError?,
    ) : AppAction()

    /**
     * [AppAction]s related to the tab strip.
     */
    sealed class TabStripAction : AppAction() {

        /**
         * [TabStripAction] used to update whether the last remaining tab that was closed was private.
         * Null means the state should reset and no snackbar should be shown.
         */
        data class UpdateLastTabClosed(val private: Boolean?) : TabStripAction()
    }

    /**
     * An wrapper action for delegating [CrashAction]s to the appropriate Reducers and Middleware in the tree.
     */
    data class CrashActionWrapper(val inner: CrashAction) : AppAction()

    /**
     * [AppAction]s related to translations.
     */
    sealed class TranslationsAction : AppAction() {

        /**
         * [TranslationsAction] dispatched when a translation is in progress.
         *
         * @property sessionId The ID of the session being translated.
         */
        data class TranslationStarted(val sessionId: String?) : TranslationsAction()
    }

    /**
     * [AppAction]s related to bookmarks.
     */
    sealed class BookmarkAction : AppAction() {
        /**
         * [BookmarkAction] dispatched when a bookmark is added.
         *
         * @property guidToEdit The guid of the newly added bookmark or null.
         * @property parentNode The [BookmarkNode] representing the folder the bookmark was added to, if any.
         * @property source Describes where the action was called from.
         */
        data class BookmarkAdded(
            val guidToEdit: String?,
            val parentNode: BookmarkNode?,
            val source: MetricsUtils.BookmarkAction.Source,
        ) : BookmarkAction()

        /**
         * [BookmarkAction] dispatched when a bookmark is removed.
         *
         * @property title The title of the bookmark that was removed.
         */
        data class BookmarkDeleted(val title: String?) : BookmarkAction()

        /**
         * [BookmarkAction] dispatched when a bookmark operation has a result that must be
         * reported even if the bookmark feature goes out of scope.
         *
         * @property globalResultReport The specific result to report.
         */
        data class BookmarkOperationResultReported(
            val globalResultReport: BookmarksGlobalResultReport,
        ) : BookmarkAction()
    }

    /**
     * [AppAction]s related to Google Lens image search.
     */
    sealed class LensAction : AppAction() {
        /** The user has requested a Lens image search. */
        data object LensRequested : LensAction()

        /** The user has requested a Lens image search for an already-known image URL. */
        data class LensRequestedWithImageUrl(val imageUrl: String) : LensAction()

        /** The Lens request has been consumed and the image chooser launched. */
        data object LensRequestConsumed : LensAction()

        /** The Lens flow was dismissed or failed. */
        data object LensDismissed : LensAction()

        /** The Lens image upload completed and a results URL is available. */
        data class LensResultAvailable(val url: String) : LensAction()

        /** The Lens result URL has been consumed and navigation triggered. */
        data object LensResultConsumed : LensAction()
    }

    /**
     * [AppAction]s related to Qr Scanner.
     */
    sealed class QrScannerAction : AppAction() {
        /**
         * [QrScannerAction] dispatched when the QR Scanner is requested.
         */
        data object QrScannerRequested : QrScannerAction()

        /**
         * [QrScannerAction] dispatched when the QR Scanner request is consumed.
         */
        data object QrScannerRequestConsumed : QrScannerAction()

        /**
         * [QrScannerAction] dispatched when the QR Scanner is dismissed.
         */
        data object QrScannerDismissed : QrScannerAction()

        /**
         * [QrScannerAction] dispatched when the QR scanner loads a QR code.
         */
        data class QrScannerInputAvailable(val data: String?) : QrScannerAction()

        /**
         * [QrScannerAction] dispatched when the loaded QR code is consumed.
         */
        data object QrScannerInputConsumed : QrScannerAction()
    }

    /**
     * [AppAction]s related to shortcuts.
     */
    sealed class ShortcutAction : AppAction() {
        /**
         * [ShortcutAction] dispatched when a shortcut is added.
         */
        data object ShortcutAdded : ShortcutAction()
    }

    /**
     * [AppAction]s related to the share feature.
     */
    sealed class ShareAction : AppAction() {
        /**
         * [ShareAction] dispatched when sharing to an application failed.
         */
        data object ShareToAppFailed : ShareAction()

        /**
         * [ShareAction] dispatched when sharing to whatsapp.
         */
        data object ShareToWhatsApp : ShareAction()

        /**
         * [ShareAction] dispatched when sharing tabs to other connected devices was successful.
         *
         * @property destination List of device IDs with which tabs were shared.
         * @property tabs List of tabs that were shared.
         */
        data class SharedTabsSuccessfully(
            val destination: List<String>,
            val tabs: List<TabData>,
        ) : ShareAction()

        /**
         * [ShareAction] dispatched when sharing tabs to other connected devices failed.
         *
         * @property destination List of device IDs with which tabs were tried to be shared.
         * @property tabs List of tabs that were tried to be shared.
         */
        data class ShareTabsFailed(
            val destination: List<String>,
            val tabs: List<TabData>,
        ) : ShareAction()

        /**
         * [ShareAction] dispatched when a link is copied to the clipboard.
         */
        data object CopyLinkToClipboard : ShareAction()
    }

    /**
     * [AppAction]s related to the snackbar.
     */
    sealed class SnackbarAction : AppAction() {

        /**
         * [SnackbarAction] dispatched to dismiss the snackbar.
         */
        data object SnackbarDismissed : SnackbarAction()

        /**
         * [SnackbarAction] dispatched when a snackbar is shown.
         */
        data object SnackbarShown : SnackbarAction()

        /**
         * [SnackbarAction] dispatched to reset the [AppState.snackbarState] to its default state.
         */
        data object Reset : SnackbarAction()

        /**
         * [SnackbarAction] dispatched to show a snackbar with a custom title.
         *
         * @property title The title to display in the snackbar.
         * @property duration The length of time for the snackbar to show.
         */
        data class ShowSnackbar(val title: String, val duration: Int = LENGTH_SHORT) : SnackbarAction()
    }

    /**
     * [AppAction]s related to the find in page feature.
     */
    sealed class FindInPageAction : AppAction() {

        /**
         * [FindInPageAction] dispatched for launching the find in page feature.
         */
        data object FindInPageStarted : FindInPageAction()

        /**
         * [FindInPageAction] dispatched when find in page feature is shown.
         */
        data object FindInPageShown : FindInPageAction()

        /**
         * [FindInPageAction] dispatched when find in page feature is dismissed.
         */
        data object FindInPageDismissed : FindInPageAction()
    }

    /**
     * [AppAction]s related to the reader view feature.
     */
    sealed class ReaderViewAction : AppAction() {

        /**
         * [ReaderViewAction] dispatched when reader view should be shown.
         */
        data object ReaderViewStarted : ReaderViewAction()

        /**
         * [ReaderViewAction] dispatched when reader view controls should be shown.
         */
        data object ReaderViewControlsShown : ReaderViewAction()

        /**
         * [ReaderViewAction] dispatched when reader view is dismissed.
         */
        data object ReaderViewDismissed : ReaderViewAction()

        /**
         * [ReaderViewAction] dispatched to reset the [AppState.readerViewState] to its default
         * state.
         */
        data object Reset : ReaderViewAction()
    }

    /**
     * [AppAction]s related to the private‐browsing lock feature.
     */
    sealed class PrivateBrowsingLockAction : AppAction() {

        /**
         * Dispatched when the private-browsing lock state should be updated.
         *
         * @property isLocked whether the feature should be locked.
         */
        data class UpdatePrivateBrowsingLock(val isLocked: Boolean) : PrivateBrowsingLockAction()
    }

    /**
     * [AppAction]s related to the content recommendations feature.
     */
    sealed class ContentRecommendationsAction : AppAction() {
        /**
         * [ContentRecommendationsAction] dispatched when content recommendations were fetched.
         *
         * @property recommendations The new list of [ContentRecommendation] that was fetched.
         */
        data class ContentRecommendationsFetched(
            val recommendations: List<ContentRecommendation>,
        ) : ContentRecommendationsAction()

        /**
         * [ContentRecommendationsAction] dispatched when an user clicks on a content
         * recommendation.
         *
         * @property recommendation The [ContentRecommendation] that was clicked.
         * @property position The position (0-index) of the [ContentRecommendation].
         * @property source The surface where the clicked recommendation was shown.
         */
        data class ContentRecommendationClicked(
            val recommendation: ContentRecommendation,
            val position: Int,
            val source: StoriesImpressionSource,
        ) : ContentRecommendationsAction()

        /**
         * Indicates the given [categoryName] was selected by the user.
         */
        data class SelectPocketStoriesCategory(val categoryName: String) :
            ContentRecommendationsAction()

        /**
         * Indicates the given [categoryName] was deselected by the user.
         */
        data class DeselectPocketStoriesCategory(val categoryName: String) :
            ContentRecommendationsAction()

        /**
         * Indicates the given story [impressions] were seen by the user.
         *
         * @property impressions A list of [PocketImpression]s detailing the story shown and
         * their respective position.
         * @property source The surface where the stories were shown.
         */
        data class PocketStoriesShown(
            val impressions: List<PocketImpression>,
            val source: StoriesImpressionSource,
        ) : ContentRecommendationsAction()

        /**
         * Cleans all in-memory data about Pocket stories and categories.
         */
        data object PocketStoriesClean : ContentRecommendationsAction()

        /**
         * Replaces the current list of [SponsoredContent]s.
         *
         * @property sponsoredContents THe new list of [SponsoredContent] that was fetched.
         */
        data class SponsoredContentsChange(
            val sponsoredContents: List<SponsoredContent>,
        ) : ContentRecommendationsAction()

        /**
         * Replaces the list of available Pocket recommended stories categories.
         */
        data class PocketStoriesCategoriesChange(val storiesCategories: List<PocketRecommendedStoriesCategory>) :
            ContentRecommendationsAction()

        /**
         * Restores the list of Pocket recommended stories categories selections.
         */
        data class PocketStoriesCategoriesSelectionsChange(
            val storiesCategories: List<PocketRecommendedStoriesCategory>,
            val categoriesSelected: List<PocketRecommendedStoriesSelectedCategory>,
        ) : ContentRecommendationsAction()
    }

    /**
     * [AppAction]s related to the Web Compat feature.
     */
    sealed class WebCompatAction : AppAction() {
        /**
         * Dispatched when the [WebCompatState] has been updated.
         */
        data class WebCompatStateUpdated(val newState: WebCompatState) : WebCompatAction()

        /**
         * Dispatched when the [WebCompatState] has been cleared.
         */
        data object WebCompatStateReset : WebCompatAction()

        /**
         * Dispatched when the WebCompat reporter has been submitted successfully.
         */
        data object WebCompatReportSent : WebCompatAction()
    }

    /**
     * [AppAction]s related to the Setup Checklist feature.
     */
    sealed class SetupChecklistAction : AppAction() {
        /**
         * When the setup checklist feature is initialised.
         */
        data object Init : SetupChecklistAction()

        /**
         * When the setup checklist is closed.
         */
        data object Closed : SetupChecklistAction()

        /**
         * When a setup checklist item is clicked.
         */
        data class ChecklistItemClicked(val item: ChecklistItem) : SetupChecklistAction()

        /**
         * When a checklist task preference is updated.
         *
         * @property taskType The type of task whose preference was updated.
         * @property prefValue The new value of the preference.
         */
        data class TaskPreferenceUpdated(
            val taskType: ChecklistItem.Task.Type,
            val prefValue: Boolean,
        ) : SetupChecklistAction()
    }

    /**
     * [AppAction]s related to downloads.
     */
    sealed class DownloadAction : AppAction() {
        /**
         * Dispatched when a download is in progress.
         *
         * @property downloadId The unique identifier for the ongoing download.
         */
        data class DownloadInProgress(val downloadId: String) : DownloadAction()

        /**
         * Dispatched when a download has failed.
         *
         * @property fileName The name of the file that failed to download.
         */
        data class DownloadFailed(val fileName: String?) : DownloadAction()

        /**
         * Dispatched when a download has completed successfully.
         *
         * @property downloadState The state object containing information about the completed download.
         */
        data class DownloadCompleted(val downloadState: DownloadState) : DownloadAction()

        /**
         * Dispatched when there is an error attempting to open a downloaded file.
         *
         * @property downloadState The state object containing information about the failed download.
         */
        data class CannotOpenFile(val downloadState: DownloadState) : DownloadAction()
    }

    /**
     * [AppAction]s related to prompting the user for a store review/rating.
     */
    sealed class ReviewPromptAction : AppAction() {
        /**
         * Dispatched to trigger review prompt eligibility checks.
         */
        data object CheckIfEligibleForReviewPrompt : ReviewPromptAction()

        /**
         * Dispatched when no triggers for showing the prompt are satisfied.
         */
        data object DoNotShowReviewPrompt : ReviewPromptAction()

        /**
         * Dispatched when a trigger to show the Play Store prompt is satisfied.
         */
        data object ShowPlayStorePrompt : ReviewPromptAction()

        /**
         * Dispatched when a trigger to show our custom review prompt is satisfied.
         */
        data object ShowCustomReviewPrompt : ReviewPromptAction()

        /**
         * Dispatched after a review prompt was shown.
         */
        data object ReviewPromptShown : ReviewPromptAction()
    }

    /**
     * [AppAction]s related to the search feature.
     */
    sealed class SearchAction : AppAction() {
        /**
         * A new search has started.
         *
         * @property tabId The ID of the tab that triggered the search.
         * May be `null` if search was not started from a browser tab.
         * @property source The application feature from where a new search was started.
         */
        data class SearchStarted(
            val tabId: String? = null,
            val source: MetricsUtils.Source = MetricsUtils.Source.NONE,
        ) : SearchAction()

        /**
         * The current in-progress search has ended.
         */
        data object SearchEnded : SearchAction()

        /**
         * New search engine was chosen for the in-progress search.
         *
         * @property searchEngine The new [SearchEngine] to use for the current in-progress browser search.
         * @property isUserSelected Whether the search engine was selected by the user or not.
         */
        data class SearchEngineSelected(
            val searchEngine: SearchEngine,
            val isUserSelected: Boolean,
        ) : SearchAction()
    }

    /**
     * [AppAction]s related to menu notifications. These actions are used to manage
     * the display and removal of notifications within the application's menu.
     */
    sealed class MenuNotification : AppAction() {
        /**
         * Dispatched to add a new notification to the menu.
         *
         * @property notification The [SupportedMenuNotifications] type to be displayed.
         */
        data class AddMenuNotification(val notification: SupportedMenuNotifications) : MenuNotification()

        /**
         * Dispatched to remove an existing notification from the menu.
         *
         * @property notification The [SupportedMenuNotifications] type to be removed.
         */
        data class RemoveMenuNotification(val notification: SupportedMenuNotifications) : MenuNotification()
    }

    /**
     * [AppAction]s related to the the trackers blocked state.
     */
    sealed class BlockedTrackersAction : AppAction() {
        /**
         * Updates the total count of trackers blocked for the privacy report.
         *
         * @property count The new count of trackers blocked.
         */
        data class UpdateTrackersBlockedCount(val count: Int) : BlockedTrackersAction()

        /**
         * Updates the details about what trackers have been blocked this week.
         *
         * @property blockedTrackerCategories The list of trackers blocked this week as a tracker category split.
         */
        data class UpdateTrackersBlockedThisWeek(
            val blockedTrackerCategories: List<TrackersBlockedCategory>,
        ) : BlockedTrackersAction()

        /**
         * Updates the earliest date for which we have information about blocked trackers.
         *
         * @property date The earliest date for which we have information about blocked trackers as a Unix time stamp.
         * May be `null` if this information is not available.
         */
        data class UpdateEarliestTrackingDate(val date: Long?) : BlockedTrackersAction()
    }

    /**
     * [AppAction]s related to the sports widget.
     */
    sealed class SportsWidgetAction : AppAction() {
        /**
         * Dispatched when countries were selected in the sports widget country selector.
         *
         * @property countryCodes Set of ISO codes of the selected countries.
         */
        data class CountriesSelected(val countryCodes: Set<String>) : SportsWidgetAction()

        /**
         * Dispatched when the user skips the "Follow your team" card.
         */
        data object FollowTeamSkipped : SportsWidgetAction()

        /**
         * Dispatched when the sports widget's visibility changes.
         *
         * @property isVisible The new visibility state of the sports widget.
         */
        data class VisibilityChanged(val isVisible: Boolean) : SportsWidgetAction()

        /**
         * Dispatched when the countdown widget's visibility changes.
         *
         * @property isCountdownVisible The new visibility state of the countdown widget.
         */
        data class CountdownVisibilityChanged(val isCountdownVisible: Boolean) : SportsWidgetAction()

        /**
         * Dispatched when new match card data is available for the homepage sports widget.
         *
         * @property matchCardStates The new [MatchCard]s to display, or empty if no match
         * should be shown.
         */
        data class MatchCardStateUpdated(val matchCardStates: List<MatchCard>) : SportsWidgetAction()

        /**
         * Dispatched when the set of teams eliminated from the tournament changes,
         * derived from the latest fetched match data. Drives the eliminated-team
         * styling in the country selector bottom sheet.
         *
         * @property countryCodes ISO codes of teams flagged eliminated in the last response.
         */
        data class EliminatedCountriesUpdated(val countryCodes: Set<String>) : SportsWidgetAction()

        /**
         * Dispatched when the sport widget's debug tool visibility changes.
         *
         * @property visible Whether the debug tool should be displayed.
         */
        data class DebugToolVisibilityChanged(val visible: Boolean) : SportsWidgetAction()

        /**
         * Dispatched when the user toggles the world cup started override setting in the sport widget's debug tool.
         * This overrides [SportsWidgetState.hasWorldCupStarted] and should be used for debug only.
         *
         * @property hasWorldCupStartedOverride Whether the world cup has started.
         */
        data class WorldCupStartedOverrideUpdated(val hasWorldCupStartedOverride: Boolean) : SportsWidgetAction()

        /**
         * Dispatched when the user toggles the skip follow team setting in the sport widget's debug tool.
         * This should be used for debug only.
         *
         * @property hasSkippedFollowTeam Whether the user skipped the "Follow your team" card.
         */
        data class SkipFollowTeamUpdated(val hasSkippedFollowTeam: Boolean) : SportsWidgetAction()

        /**
         * Dispatched to request a fetch of the latest match data from the sports API.
         * Triggered by manual user refresh or when the selected countries change.
         */
        data object FetchMatches : SportsWidgetAction()

        /**
         * Dispatched when a match data fetch fails.
         *
         * @property error The [SportCardErrorState] describing the failure.
         */
        data class FetchFailed(val error: SportCardErrorState) : SportsWidgetAction()

        /**
         * Dispatched to clear any active [SportsWidgetState.errorState] without otherwise
         * touching widget data. Used on resume to retire a stale ConnectionInterrupted
         * banner once the device is back online and there's no fetch to schedule (e.g.
         * during the pre-7-day countdown phase).
         */
        data object ErrorStateCleared : SportsWidgetAction()

        /**
         * Dispatched when the user toggles the one week to the world cup override setting
         * in the sport widget's debug tool. This overrides [SportsWidgetState.isOneWeekToWorldCup] and should
         * be used for debug only.
         *
         * @property isOneWeekToWorldCupOverride Whether it's one week to the World Cup.
         */
        data class OneWeekToWorldCupOverrideUpdated(val isOneWeekToWorldCupOverride: Boolean) : SportsWidgetAction()
    }

    /**
     * [SnackbarAction]s related to the IP Protection feature.
     */
    sealed class IPProtectionSnackbarAction : SnackbarAction() {
        /**
         * Dispatched when IP Protection feature experienced a connection error.
         *
         * @property title The title to display in the snackbar.
         */
        data class ConnectionError(val title: String) : IPProtectionSnackbarAction()

        /**
         * Dispatched when IP Protection feature experienced a connection error.
         *
         * @property title The title to display in the snackbar.
         */
        data class DataLimitReached(val title: String) : IPProtectionSnackbarAction()
    }
}
