/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.appstate

import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.sync.TabData
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.lib.crash.Crash.NativeCodeCrash
import mozilla.components.lib.crash.store.CrashAction
import mozilla.components.lib.state.Action
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.MessageSurfaceId
import mozilla.components.service.pocket.PocketStory.ContentRecommendation
import mozilla.components.service.pocket.PocketStory.PocketSponsoredStory
import mozilla.components.service.pocket.PocketStory.SponsoredContent
import org.mozilla.fenix.browser.StandardSnackbarError
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.setup.checklist.ChecklistItem
import org.mozilla.fenix.components.appstate.shopping.ShoppingState
import org.mozilla.fenix.components.appstate.webcompat.WebCompatState
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.pocket.PocketImpression
import org.mozilla.fenix.home.pocket.PocketRecommendedStoriesCategory
import org.mozilla.fenix.home.pocket.PocketRecommendedStoriesSelectedCategory
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTab
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTabState
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem
import org.mozilla.fenix.library.history.PendingDeletionHistory
import org.mozilla.fenix.messaging.MessagingState
import org.mozilla.fenix.search.SearchDialogFragment
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
     * Updates whether the [SearchDialogFragment] is visible.
     */
    data class UpdateSearchDialogVisibility(val isVisible: Boolean) : AppAction()
    data class AddNonFatalCrash(val crash: NativeCodeCrash) : AppAction()
    data class RemoveNonFatalCrash(val crash: NativeCodeCrash) : AppAction()
    object RemoveAllNonFatalCrashes : AppAction()

    data class Change(
        val topSites: List<TopSite>,
        val mode: BrowsingMode,
        val collections: List<TabCollection>,
        val showCollectionPlaceholder: Boolean,
        val recentTabs: List<RecentTab>,
        val bookmarks: List<Bookmark>,
        val recentHistory: List<RecentlyVisitedItem>,
        val recentSyncedTabState: RecentSyncedTabState,
    ) :
        AppAction()

    data class CollectionExpanded(val collection: TabCollection, val expand: Boolean) :
        AppAction()

    data class CollectionsChange(val collections: List<TabCollection>) : AppAction()
    data class ModeChange(val mode: BrowsingMode) : AppAction()
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

    data object RemoveCollectionsPlaceholder : AppAction()

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
     * Action dispatched when the current site's data has been cleared.
     */
    data object SiteDataCleared : AppAction()

    /**
     * Action dispatched when the current tab has been closed.
     *
     * @property isPrivate Whether the closed tab was private or not.
     */
    data class CurrentTabClosed(
        val isPrivate: Boolean,
    ) : AppAction()

    /**
     * Action dispatched when open in firefox action is selected from custom tab.
     */
    data object OpenInFirefoxStarted : AppAction()

    /**
     * Action dispatched when open in firefox action is completed.
     */
    data object OpenInFirefoxFinished : AppAction()

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
     * [AppAction]s related to shopping sheet state.
     */
    sealed class ShoppingAction : AppAction() {

        /**
         * [ShoppingAction] used to update the expansion state of the shopping sheet.
         */
        data class ShoppingSheetStateUpdated(val expanded: Boolean) : ShoppingAction()

        /**
         * [ShoppingAction] used to update the expansion state of the highlights card.
         */
        data class HighlightsCardExpanded(
            val productPageUrl: String,
            val expanded: Boolean,
        ) : ShoppingAction()

        /**
         * [ShoppingAction] used to update the expansion state of the info card.
         */
        data class InfoCardExpanded(
            val productPageUrl: String,
            val expanded: Boolean,
        ) : ShoppingAction()

        /**
         * [ShoppingAction] used to update the expansion state of the settings card.
         */
        data class SettingsCardExpanded(
            val productPageUrl: String,
            val expanded: Boolean,
        ) : ShoppingAction()

        /**
         * [ShoppingAction] used to update the recorded product recommendation impressions set.
         */
        data class ProductRecommendationImpression(
            val key: ShoppingState.ProductRecommendationImpressionKey,
        ) : ShoppingAction()
    }

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
         */
        data class BookmarkAdded(
            val guidToEdit: String?,
            val parentNode: BookmarkNode?,
        ) : BookmarkAction()

        /**
         * [BookmarkAction] dispatched when a bookmark is removed.
         *
         * @property title The title of the bookmark that was removed.
         */
        data class BookmarkDeleted(val title: String?) : BookmarkAction()
    }

    /**
     * [AppAction]s related to shortcuts.
     */
    sealed class ShortcutAction : AppAction() {
        /**
         * [ShortcutAction] dispatched when a shortcut is added.
         */
        data object ShortcutAdded : ShortcutAction()

        /**
         * [ShortcutAction] dispatched when a shortcut is removed.
         */
        data object ShortcutRemoved : ShortcutAction()
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
         */
        data class ContentRecommendationClicked(
            val recommendation: ContentRecommendation,
            val position: Int,
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
         */
        data class PocketStoriesShown(val impressions: List<PocketImpression>) :
            ContentRecommendationsAction()

        /**
         * Cleans all in-memory data about Pocket stories and categories.
         */
        data object PocketStoriesClean : ContentRecommendationsAction()

        /**
         * Replaces the current list of Pocket sponsored stories.
         *
         * @property sponsoredStories The new list of [PocketSponsoredStory] that was fetched.
         * @property showContentRecommendations Whether or not to show Merino content
         * recommendations.
         */
        data class PocketSponsoredStoriesChange(
            val sponsoredStories: List<PocketSponsoredStory>,
            val showContentRecommendations: Boolean,
        ) : ContentRecommendationsAction()

        /**
         * Replaces the current list of [SponsoredContent]s.
         *
         * @property sponsoredContents THe new list of [SponsoredContent] that was fetched.
         * @property showContentRecommendations Whether or not to show Merino content
         * recommendations.
         */
        data class SponsoredContentsChange(
            val sponsoredContents: List<SponsoredContent>,
            val showContentRecommendations: Boolean,
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
}
