/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sessioncontrol

import mozilla.components.feature.tab.collections.Tab
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.pocket.PocketStory
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.setup.checklist.ChecklistItem
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.bookmarks.controller.BookmarksController
import org.mozilla.fenix.home.interactor.HomepageInteractor
import org.mozilla.fenix.home.logo.LogoController
import org.mozilla.fenix.home.logo.TrackingProtectionController
import org.mozilla.fenix.home.pocket.PocketRecommendedStoriesCategory
import org.mozilla.fenix.home.pocket.controller.PocketStoriesController
import org.mozilla.fenix.home.pocket.controller.StoriesImpressionSource
import org.mozilla.fenix.home.privatebrowsing.controller.PrivateBrowsingController
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTab
import org.mozilla.fenix.home.recentsyncedtabs.controller.RecentSyncedTabController
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.home.recenttabs.controller.RecentTabController
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryGroup
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryHighlight
import org.mozilla.fenix.home.recentvisits.controller.RecentVisitsController
import org.mozilla.fenix.home.search.HomeSearchController
import org.mozilla.fenix.home.termsofuse.PrivacyNoticeBannerController
import org.mozilla.fenix.home.toolbar.ToolbarController
import org.mozilla.fenix.home.topsites.AddShortcutEntryPoint
import org.mozilla.fenix.home.topsites.AddShortcutSource
import org.mozilla.fenix.home.topsites.controller.TopSiteController
import org.mozilla.fenix.wallpapers.WallpaperState

/** Interface for tab related actions in the [SessionControlInteractor]. */
interface TabSessionInteractor {
    /**
     * Called when there is an update to the session state and updated metrics need to be reported
     *
     * * @param state The state the homepage from which to report desired metrics.
     */
    fun reportSessionMetrics(state: AppState)
}

/** Interface for collection related actions in the [SessionControlInteractor]. */
@SuppressWarnings("TooManyFunctions")
interface CollectionInteractor {
    /**
     * Shows the Collection Creation fragment for selecting the tabs to add to the given tab collection. Called when a
     * user taps on the "Add tab" collection menu item.
     *
     * @param collection The collection of tabs that will be modified.
     */
    fun onCollectionAddTabTapped(collection: TabCollection)

    /**
     * Opens the given tab. Called when a user clicks on a tab in the tab collection.
     *
     * @param tab The tab to open from the tab collection.
     */
    fun onCollectionOpenTabClicked(tab: Tab)

    /**
     * Opens all the tabs in a given tab collection. Called when a user taps on the "Open tabs" collection menu item.
     *
     * @param collection The collection of tabs to open.
     */
    fun onCollectionOpenTabsTapped(collection: TabCollection)

    /**
     * Removes the given tab from the given tab collection. Called when a user swipes to remove a tab or clicks on the
     * tab close button.
     *
     * @param collection The collection of tabs that will be modified.
     * @param tab The tab to remove from the tab collection.
     */
    fun onCollectionRemoveTab(collection: TabCollection, tab: Tab)

    /**
     * Shares the tabs in the given tab collection. Called when a user clicks on the Collection Share button.
     *
     * @param collection The collection of tabs to share.
     */
    fun onCollectionShareTabsClicked(collection: TabCollection)

    /**
     * Shows a prompt for deleting the given tab collection. Called when a user taps on the "Delete collection"
     * collection menu item.
     *
     * @param collection The collection of tabs to delete.
     */
    fun onDeleteCollectionTapped(collection: TabCollection)

    /**
     * Shows the Collection Creation fragment for renaming the given tab collection. Called when a user taps on the
     * "Rename collection" collection menu item.
     *
     * @param collection The collection of tabs to rename.
     */
    fun onRenameCollectionTapped(collection: TabCollection)

    /**
     * Toggles expanding or collapsing the given tab collection. Called when a user clicks on a [CollectionViewHolder].
     *
     * @param collection The collection of tabs that will be collapsed.
     * @param expand True if the given tab collection should be expanded or collapse if false.
     */
    fun onToggleCollectionExpanded(collection: TabCollection, expand: Boolean)

    /** Opens the collection creator */
    fun onAddTabsToCollectionTapped()
}

interface MessageCardInteractor {
    /** Called when a [Message]'s button is clicked */
    fun onMessageClicked(message: Message)

    /** Called when close button on a [Message] card. */
    fun onMessageClosedClicked(message: Message)
}

/** Interface for wallpaper related actions. */
interface WallpaperInteractor {
    /**
     * Show Wallpapers onboarding dialog to onboard users about the feature if conditions are met. Returns true if the
     * call has been passed down to the controller.
     *
     * @param state The wallpaper state.
     * @return Whether the onboarding dialog is currently shown.
     */
    fun showWallpapersOnboardingDialog(state: WallpaperState): Boolean
}

/** Interface for setup checklist feature related actions. */
interface SetupChecklistInteractor {
    /** Gets invoked when the user clicks a check list item. */
    fun onChecklistItemClicked(item: ChecklistItem)

    /** Invoked when the remove button is clicked. */
    fun onRemoveChecklistButtonClicked()
}

/** Interface for tracking protection related actions on the homepage. */
interface TrackingProtectionInteractor {
    /** Invoked when the privacy report card is tapped. */
    fun onPrivacyReportTapped()

    /** Invoked when the longfox entry point text is clicked. */
    fun onLongfoxEntryPointClicked()

    /** Invoked when the longfox entry point is shown. */
    fun onLongfoxEntryPointShown()
}

/**
 * Interactor for the Home screen. Provides implementations for the CollectionInteractor, OnboardingInteractor,
 * TopSiteInteractor, TabSessionInteractor, ToolbarInteractor, ExperimentCardInteractor, RecentTabInteractor,
 * RecentBookmarksInteractor and others.
 */
@SuppressWarnings("TooManyFunctions", "LongParameterList")
class SessionControlInteractor(
    private val controller: SessionControlController,
    private val recentTabController: RecentTabController,
    private val recentSyncedTabController: RecentSyncedTabController,
    private val bookmarksController: BookmarksController,
    private val recentVisitsController: RecentVisitsController,
    private val pocketStoriesController: PocketStoriesController,
    private val privateBrowsingController: PrivateBrowsingController,
    private val toolbarController: ToolbarController,
    private val homeSearchController: HomeSearchController,
    private val topSiteController: TopSiteController,
    private val privacyNoticeBannerController: PrivacyNoticeBannerController,
    private val trackingProtectionController: TrackingProtectionController,
    private val logoController: LogoController,
) : HomepageInteractor {

    override fun onCollectionAddTabTapped(collection: TabCollection) {
        controller.handleCollectionAddTabTapped(collection)
    }

    override fun onCollectionOpenTabClicked(tab: Tab) {
        controller.handleCollectionOpenTabClicked(tab)
    }

    override fun onCollectionOpenTabsTapped(collection: TabCollection) {
        controller.handleCollectionOpenTabsTapped(collection)
    }

    override fun onCollectionRemoveTab(collection: TabCollection, tab: Tab) {
        controller.handleCollectionRemoveTab(collection, tab)
    }

    override fun onCollectionShareTabsClicked(collection: TabCollection) {
        controller.handleCollectionShareTabsClicked(collection)
    }

    override fun onDeleteCollectionTapped(collection: TabCollection) {
        controller.handleDeleteCollectionTapped(collection)
    }

    override fun onOpenInPrivateTabClicked(topSite: TopSite) {
        topSiteController.handleOpenInPrivateTabClicked(topSite)
    }

    override fun onEditTopSiteClicked(topSite: TopSite) {
        topSiteController.handleEditTopSiteClicked(topSite)
    }

    override fun onRemoveTopSiteClicked(topSite: TopSite) {
        topSiteController.handleRemoveTopSiteClicked(topSite)
    }

    override fun onRenameCollectionTapped(collection: TabCollection) {
        controller.handleRenameCollectionTapped(collection)
    }

    override fun onSelectTopSite(topSite: TopSite, position: Int) {
        topSiteController.handleSelectTopSite(topSite, position)
    }

    override fun onTopSiteImpression(topSite: TopSite.Provided, position: Int) {
        topSiteController.handleTopSiteImpression(topSite, position)
    }

    override fun onSettingsClicked() {
        topSiteController.handleTopSiteSettingsClicked()
    }

    override fun onSponsorPrivacyClicked() {
        topSiteController.handleSponsorPrivacyClicked()
    }

    override fun onTopSiteLongClicked(topSite: TopSite) {
        topSiteController.handleTopSiteLongClicked(topSite)
    }

    override fun onShowAllTopSitesClicked() {
        topSiteController.handleShowAllTopSitesClicked()
    }

    override fun onExpandToggleClicked(isExpanded: Boolean) {
        topSiteController.handleExpandToggleClicked(isExpanded)
    }

    override fun onShortcutsLibraryViewed() {
        topSiteController.handleShortcutsLibraryViewed()
    }

    override fun onSaveShortcut(
        title: String,
        url: String,
        source: AddShortcutSource,
        entryPoint: AddShortcutEntryPoint,
    ) {
        topSiteController.handleSaveShortcut(
            title = title,
            url = url,
            source = source,
            entryPoint = entryPoint,
        )
    }

    override fun showWallpapersOnboardingDialog(state: WallpaperState): Boolean {
        return controller.handleShowWallpapersOnboardingDialog(state)
    }

    override fun onChecklistItemClicked(item: ChecklistItem) {
        controller.onChecklistItemClicked(item)
    }

    override fun onRemoveChecklistButtonClicked() {
        controller.onRemoveChecklistButtonClicked()
    }

    override fun onToggleCollectionExpanded(collection: TabCollection, expand: Boolean) {
        controller.handleToggleCollectionExpanded(collection, expand)
    }

    override fun onAddTabsToCollectionTapped() {
        controller.handleCreateCollection()
    }

    override fun onLearnMoreClicked() {
        privateBrowsingController.handleLearnMoreClicked()
    }

    override fun onPrivateModeButtonClicked(newMode: BrowsingMode) {
        privateBrowsingController.handlePrivateModeButtonClicked(newMode)
    }

    override fun onNavigateSearch() {
        toolbarController.handleNavigateSearch()
    }

    override fun onHomeContentFocusedWhileSearchIsActive() {
        homeSearchController.handleHomeContentFocusedWhileSearchIsActive()
    }

    override fun onRecentTabClicked(tabId: String) {
        recentTabController.handleRecentTabClicked(tabId)
    }

    override fun onRecentTabShowAllClicked() {
        recentTabController.handleRecentTabShowAllClicked()
    }

    override fun onRemoveRecentTab(tab: RecentTab.Tab) {
        recentTabController.handleRecentTabRemoved(tab)
    }

    override fun onRecentSyncedTabClicked(tab: RecentSyncedTab) {
        recentSyncedTabController.handleRecentSyncedTabClick(tab)
    }

    override fun onSyncedTabShowAllClicked() {
        recentSyncedTabController.handleSyncedTabShowAllClicked()
    }

    override fun onRemovedRecentSyncedTab(tab: RecentSyncedTab) {
        recentSyncedTabController.handleRecentSyncedTabRemoved(tab)
    }

    override fun onBookmarkClicked(bookmark: Bookmark) {
        bookmarksController.handleBookmarkClicked(bookmark)
    }

    override fun onShowAllBookmarksClicked() {
        bookmarksController.handleShowAllBookmarksClicked()
    }

    override fun onBookmarkRemoved(bookmark: Bookmark) {
        bookmarksController.handleBookmarkRemoved(bookmark)
    }

    override fun onHistoryShowAllClicked() {
        recentVisitsController.handleHistoryShowAllClicked()
    }

    override fun onRecentHistoryGroupClicked(recentHistoryGroup: RecentHistoryGroup) {
        recentVisitsController.handleRecentHistoryGroupClicked(recentHistoryGroup)
    }

    override fun onRemoveRecentHistoryGroup(groupTitle: String) {
        recentVisitsController.handleRemoveRecentHistoryGroup(groupTitle)
    }

    override fun onRecentHistoryHighlightClicked(recentHistoryHighlight: RecentHistoryHighlight) {
        recentVisitsController.handleRecentHistoryHighlightClicked(recentHistoryHighlight)
    }

    override fun onRemoveRecentHistoryHighlight(highlightUrl: String) {
        recentVisitsController.handleRemoveRecentHistoryHighlight(highlightUrl)
    }

    override fun onStoryShown(storyShown: PocketStory, storyPosition: Triple<Int, Int, Int>) {
        pocketStoriesController.handleStoryShown(storyShown, storyPosition)
    }

    override fun onStoriesShown(storiesShown: List<PocketStory>, source: StoriesImpressionSource) {
        pocketStoriesController.handleStoriesShown(storiesShown, source)
    }

    override fun onCategoryClicked(categoryClicked: PocketRecommendedStoriesCategory) {
        pocketStoriesController.handleCategoryClick(categoryClicked)
    }

    override fun onStoryClicked(
        storyClicked: PocketStory,
        storyPosition: Triple<Int, Int, Int>,
        source: StoriesImpressionSource,
    ) {
        pocketStoriesController.handleStoryClicked(storyClicked, storyPosition, source)
    }

    override fun onDiscoverMoreClicked() {
        pocketStoriesController.handleDiscoverMoreClicked()
    }

    override fun onDiscoverMoreScreenViewed() {
        pocketStoriesController.handleDiscoverMoreScreenViewed()
    }

    override fun reportSessionMetrics(state: AppState) {
        controller.handleReportSessionMetrics(state)
    }

    override fun onMessageClicked(message: Message) {
        controller.handleMessageClicked(message)
    }

    override fun onMessageClosedClicked(message: Message) {
        controller.handleMessageClosed(message)
    }

    override fun onPrivacyNoticeBannerCloseClicked() {
        privacyNoticeBannerController.onBannerCloseClicked()
    }

    override fun onPrivacyNoticeBannerPrivacyNoticeClicked() {
        privacyNoticeBannerController.onBannerPrivacyNoticeClicked()
    }

    override fun onPrivacyNoticeBannerLearnMoreClicked() {
        privacyNoticeBannerController.onBannerLearnMoreClicked()
    }

    override fun onPrivacyNoticeBannerDisplayed() {
        privacyNoticeBannerController.onBannerDisplayed()
    }

    override fun onPrivacyReportTapped() {
        trackingProtectionController.handleProtectionStatusPillClicked()
    }

    override fun onLongfoxEntryPointClicked() {
        logoController.handleLongfoxEntryPointClicked()
    }

    override fun onLongfoxEntryPointShown() {
        logoController.handleLongfoxEntryPointShown()
    }
}
