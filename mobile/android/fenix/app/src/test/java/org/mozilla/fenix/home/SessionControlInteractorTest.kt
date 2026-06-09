/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import mozilla.components.feature.tab.collections.Tab
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.service.pocket.PocketStory
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.bookmarks.controller.BookmarksController
import org.mozilla.fenix.home.logo.LogoController
import org.mozilla.fenix.home.logo.TrackingProtectionController
import org.mozilla.fenix.home.pocket.PocketRecommendedStoriesCategory
import org.mozilla.fenix.home.pocket.controller.PocketStoriesController
import org.mozilla.fenix.home.pocket.controller.StoriesImpressionSource
import org.mozilla.fenix.home.privatebrowsing.controller.PrivateBrowsingController
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTab
import org.mozilla.fenix.home.recentsyncedtabs.controller.RecentSyncedTabController
import org.mozilla.fenix.home.recenttabs.controller.RecentTabController
import org.mozilla.fenix.home.recentvisits.controller.RecentVisitsController
import org.mozilla.fenix.home.search.HomeSearchController
import org.mozilla.fenix.home.sessioncontrol.DefaultSessionControlController
import org.mozilla.fenix.home.sessioncontrol.SessionControlInteractor
import org.mozilla.fenix.home.sports.SportsController
import org.mozilla.fenix.home.termsofuse.PrivacyNoticeBannerController
import org.mozilla.fenix.home.toolbar.ToolbarController
import org.mozilla.fenix.home.topsites.controller.TopSiteController

class SessionControlInteractorTest {

    private val controller: DefaultSessionControlController = mockk(relaxed = true)
    private val recentTabController: RecentTabController = mockk(relaxed = true)
    private val recentSyncedTabController: RecentSyncedTabController = mockk(relaxed = true)
    private val bookmarksController: BookmarksController = mockk(relaxed = true)
    private val pocketStoriesController: PocketStoriesController = mockk(relaxed = true)
    private val privateBrowsingController: PrivateBrowsingController = mockk(relaxed = true)
    private val toolbarController: ToolbarController = mockk(relaxed = true)
    private val homeSearchController: HomeSearchController = mockk(relaxed = true)
    private val topSiteController: TopSiteController = mockk(relaxed = true)
    private val privacyNoticeBannerController: PrivacyNoticeBannerController = mockk(relaxed = true)
    private val trackingProtectionController: TrackingProtectionController = mockk(relaxed = true)
    private val logoController: LogoController = mockk(relaxed = true)
    private val sportsController: SportsController = mockk(relaxed = true)

    // Note: the recent visits tests are handled in [RecentVisitsInteractorTest] and [RecentVisitsControllerTest]
    private val recentVisitsController: RecentVisitsController = mockk(relaxed = true)

    private lateinit var interactor: SessionControlInteractor

    @Before
    fun setup() {
        interactor = SessionControlInteractor(
            controller,
            recentTabController,
            recentSyncedTabController,
            bookmarksController,
            recentVisitsController,
            pocketStoriesController,
            privateBrowsingController,
            toolbarController,
            homeSearchController,
            topSiteController,
            privacyNoticeBannerController,
            trackingProtectionController,
            logoController,
            sportsController,
        )
    }

    @Test
    fun onCollectionAddTabTapped() {
        val collection: TabCollection = mockk(relaxed = true)
        interactor.onCollectionAddTabTapped(collection)
        verify { controller.handleCollectionAddTabTapped(collection) }
    }

    @Test
    fun onCollectionOpenTabClicked() {
        val tab: Tab = mockk(relaxed = true)
        interactor.onCollectionOpenTabClicked(tab)
        verify { controller.handleCollectionOpenTabClicked(tab) }
    }

    @Test
    fun onCollectionOpenTabsTapped() {
        val collection: TabCollection = mockk(relaxed = true)
        interactor.onCollectionOpenTabsTapped(collection)
        verify { controller.handleCollectionOpenTabsTapped(collection) }
    }

    @Test
    fun onCollectionRemoveTab() {
        val collection: TabCollection = mockk(relaxed = true)
        val tab: Tab = mockk(relaxed = true)
        interactor.onCollectionRemoveTab(collection, tab)
        verify { controller.handleCollectionRemoveTab(collection, tab) }
    }

    @Test
    fun onCollectionShareTabsClicked() {
        val collection: TabCollection = mockk(relaxed = true)
        interactor.onCollectionShareTabsClicked(collection)
        verify { controller.handleCollectionShareTabsClicked(collection) }
    }

    @Test
    fun onDeleteCollectionTapped() {
        val collection: TabCollection = mockk(relaxed = true)
        interactor.onDeleteCollectionTapped(collection)
        verify { controller.handleDeleteCollectionTapped(collection) }
    }

    @Test
    fun onPrivateBrowsingLearnMoreClicked() {
        interactor.onLearnMoreClicked()
        verify { privateBrowsingController.handleLearnMoreClicked() }
    }

    @Test
    fun onRenameCollectionTapped() {
        val collection: TabCollection = mockk(relaxed = true)
        interactor.onRenameCollectionTapped(collection)
        verify { controller.handleRenameCollectionTapped(collection) }
    }

    @Test
    fun onToggleCollectionExpanded() {
        val collection: TabCollection = mockk(relaxed = true)
        interactor.onToggleCollectionExpanded(collection, true)
        verify { controller.handleToggleCollectionExpanded(collection, true) }
    }

    @Test
    fun onAddTabsToCollection() {
        interactor.onAddTabsToCollectionTapped()
        verify { controller.handleCreateCollection() }
    }

    @Test
    fun onNavigateSearch() {
        interactor.onNavigateSearch()
        verify { toolbarController.handleNavigateSearch() }
    }

    @Test
    fun onHomeContentFocusedWhileSearchIsActive() {
        interactor.onHomeContentFocusedWhileSearchIsActive()
        verify { homeSearchController.handleHomeContentFocusedWhileSearchIsActive() }
    }

    @Test
    fun onRecentTabClicked() {
        val tabId = "tabId"
        interactor.onRecentTabClicked(tabId)
        verify { recentTabController.handleRecentTabClicked(tabId) }
    }

    @Test
    fun onRecentTabShowAllClicked() {
        interactor.onRecentTabShowAllClicked()
        verify { recentTabController.handleRecentTabShowAllClicked() }
    }

    @Test
    fun `WHEN recent synced tab is clicked THEN the tab is handled`() {
        val tab: RecentSyncedTab = mockk()
        interactor.onRecentSyncedTabClicked(tab)

        verify { recentSyncedTabController.handleRecentSyncedTabClick(tab) }
    }

    @Test
    fun `WHEN recent synced tabs show all is clicked THEN show all synced tabs is handled`() {
        interactor.onSyncedTabShowAllClicked()

        verify { recentSyncedTabController.handleSyncedTabShowAllClicked() }
    }

    @Test
    fun `WHEN a bookmark is clicked THEN the selected bookmark is handled`() {
        val bookmark = Bookmark()

        interactor.onBookmarkClicked(bookmark)
        verify { bookmarksController.handleBookmarkClicked(bookmark) }
    }

    @Test
    fun `WHEN Show All bookmarks button is clicked THEN the click is handled`() {
        interactor.onShowAllBookmarksClicked()
        verify { bookmarksController.handleShowAllBookmarksClicked() }
    }

    @Test
    fun `WHEN private mode button is clicked THEN the click is handled`() {
        val newMode = BrowsingMode.Private

        interactor.onPrivateModeButtonClicked(newMode)
        verify { privateBrowsingController.handlePrivateModeButtonClicked(newMode) }
    }

    @Test
    fun `WHEN onSettingsClicked is called THEN handleTopSiteSettingsClicked is called`() {
        interactor.onSettingsClicked()
        verify { topSiteController.handleTopSiteSettingsClicked() }
    }

    @Test
    fun `WHEN onSponsorPrivacyClicked is called THEN handleSponsorPrivacyClicked is called`() {
        interactor.onSponsorPrivacyClicked()
        verify { topSiteController.handleSponsorPrivacyClicked() }
    }

    @Test
    fun `WHEN a top site is long clicked THEN the click is handled`() {
        val topSite: TopSite = mockk()
        interactor.onTopSiteLongClicked(topSite)
        verify { topSiteController.handleTopSiteLongClicked(topSite) }
    }

    @Test
    fun `WHEN save shortcut is called THEN handle the save action in the controller`() {
        interactor.onSaveShortcut(title = "Firefox", url = "firefox.com")
        verify { topSiteController.handleSaveShortcut(title = "Firefox", url = "firefox.com") }
    }

    @Test
    fun `GIVEN a PocketStoriesInteractor WHEN a story is shown THEN handle it in a PocketStoriesController`() {
        val shownStory: PocketStory = mockk()
        val storyPosition = Triple(1, 2, 3)

        interactor.onStoryShown(shownStory, storyPosition)

        verify { pocketStoriesController.handleStoryShown(shownStory, storyPosition) }
    }

    @Test
    fun `GIVEN a PocketStoriesInteractor WHEN stories are shown THEN handle it in a PocketStoriesController`() {
        val shownStories: List<PocketStory> = emptyList()

        interactor.onStoriesShown(shownStories, StoriesImpressionSource.HOMEPAGE)

        verify {
            pocketStoriesController.handleStoriesShown(shownStories, StoriesImpressionSource.HOMEPAGE)
        }
    }

    @Test
    fun `GIVEN a PocketStoriesInteractor WHEN a category is clicked THEN handle it in a PocketStoriesController`() {
        val clickedCategory: PocketRecommendedStoriesCategory = mockk()

        interactor.onCategoryClicked(clickedCategory)

        verify { pocketStoriesController.handleCategoryClick(clickedCategory) }
    }

    @Test
    fun `GIVEN a PocketStoriesInteractor WHEN a story is clicked THEN handle it in a PocketStoriesController`() {
        val clickedStory: PocketStory = mockk()
        val storyPosition = Triple(1, 2, 3)

        interactor.onStoryClicked(clickedStory, storyPosition)

        verify { pocketStoriesController.handleStoryClicked(clickedStory, storyPosition) }
    }

    @Test
    fun reportSessionMetrics() {
        val appState: AppState = mockk(relaxed = true)
        every { appState.bookmarks } returns emptyList()
        interactor.reportSessionMetrics(appState)
        verify { controller.handleReportSessionMetrics(appState) }
    }

    @Test
    fun `GIVEN a set of country codes WHEN countries are selected THEN sports controller handles the selection`() {
        val countryCodes = setOf("US", "JP", "BR")
        interactor.onCountriesSelected(countryCodes)
        verify { sportsController.handleCountriesSelected(countryCodes) }
    }

    @Test
    fun `GIVEN an empty set WHEN countries are selected THEN sports controller handles the empty selection`() {
        val countryCodes = emptySet<String>()
        interactor.onCountriesSelected(countryCodes)
        verify { sportsController.handleCountriesSelected(countryCodes) }
    }

    @Test
    fun `WHEN the follow team flow is skipped THEN sports controller handles the skip`() {
        interactor.onSkippedFollowTeam()
        verify { sportsController.handleSkippedFollowTeam() }
    }

    @Test
    fun `WHEN the sports widget is dismissed THEN sports controller handles the dismissal`() {
        interactor.onSportsWidgetDismissed()
        verify { sportsController.handleSportsWidgetDismissed() }
    }

    @Test
    fun `WHEN the countdown widget is dismissed THEN sports controller handles the dismissal`() {
        interactor.onCountdownWidgetDismissed()
        verify { sportsController.handleCountdownWidgetDismissed() }
    }

    @Test
    fun `WHEN the get custom wallpaper menu item is clicked THEN sports controller handles the navigation`() {
        interactor.onGetCustomWallpaperClicked()
        verify { sportsController.handleOnGetCustomWallpaperClicked() }
    }

    @Test
    fun `WHEN the share menu item is clicked THEN sports controller handles the share`() {
        interactor.onSportsWidgetShareClicked()
        verify { sportsController.handleSportsWidgetShareClicked() }
    }

    @Test
    fun `WHEN the privacy report is tapped THEN tracking protection controller handles the action`() {
        interactor.onPrivacyReportTapped()

        verify { trackingProtectionController.handleProtectionStatusPillClicked() }
    }
}
