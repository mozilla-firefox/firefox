/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.content.Context
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.every
import io.mockk.mockk
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.sync.DeviceType
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.MessageData
import mozilla.components.service.pocket.PocketStory
import mozilla.components.service.pocket.PocketStory.ContentRecommendation
import mozilla.components.service.pocket.PocketStory.PocketRecommendedStory
import mozilla.components.service.pocket.PocketStory.SponsoredContent
import mozilla.components.service.pocket.PocketStory.SponsoredContentCallbacks
import mozilla.components.service.pocket.PocketStory.SponsoredContentFrequencyCaps
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.ContentRecommendationsAction
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.UpdateMessageToShow
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.filterOut
import org.mozilla.fenix.components.appstate.recommendations.ContentRecommendationsState
import org.mozilla.fenix.components.appstate.setup.checklist.ChecklistItem
import org.mozilla.fenix.components.appstate.setup.checklist.SetupChecklistState
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.pocket.POCKET_STORIES_DEFAULT_CATEGORY_NAME
import org.mozilla.fenix.home.pocket.PocketImpression
import org.mozilla.fenix.home.pocket.PocketRecommendedStoriesCategory
import org.mozilla.fenix.home.pocket.PocketRecommendedStoriesSelectedCategory
import org.mozilla.fenix.home.pocket.controller.StoriesImpressionSource
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTab
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTabState
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryGroup
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryHighlight
import org.mozilla.fenix.messaging.FenixMessageSurfaceId
import org.mozilla.fenix.onboarding.FenixOnboarding
import mozilla.components.ui.icons.R as iconsR

@RunWith(AndroidJUnit4::class)
class AppStoreTest {
    private lateinit var context: Context
    private lateinit var accountManager: FxaAccountManager
    private lateinit var onboarding: FenixOnboarding
    private lateinit var browsingModeManager: BrowsingModeManager
    private lateinit var appState: AppState
    private lateinit var appStore: AppStore
    private lateinit var recentSyncedTabsList: List<RecentSyncedTab>

    @Before
    fun setup() {
        context = mockk(relaxed = true)
        accountManager = mockk(relaxed = true)
        onboarding = mockk(relaxed = true)
        browsingModeManager = mockk(relaxed = true)
        recentSyncedTabsList = listOf(
            RecentSyncedTab(
                deviceDisplayName = "",
                deviceType = mockk(relaxed = true),
                title = "",
                url = "",
                previewImageUrl = null,
            ),
        )

        every { context.components.backgroundServices.accountManager } returns accountManager
        every { onboarding.userHasBeenOnboarded() } returns true
        every { browsingModeManager.mode } returns BrowsingMode.Normal

        appState = AppState(
            collections = emptyList(),
            expandedCollections = emptySet(),
            mode = browsingModeManager.mode,
            topSites = emptyList(),
            recentTabs = emptyList(),
            recentSyncedTabState = RecentSyncedTabState.Success(recentSyncedTabsList),
        )

        appStore = AppStore(appState)
    }

    @Test
    fun `Test toggling the mode in AppStore`() = runTest {
        // Verify that the default mode and tab states of the HomeFragment are correct.
        assertEquals(BrowsingMode.Normal, appStore.state.mode)

        // Change the AppStore to Private mode.
        appStore.dispatch(AppAction.BrowsingModeManagerModeChanged(BrowsingMode.Private))
        assertEquals(BrowsingMode.Private, appStore.state.mode)

        // Change the AppStore back to Normal mode.
        appStore.dispatch(AppAction.BrowsingModeManagerModeChanged(BrowsingMode.Normal))
        assertEquals(BrowsingMode.Normal, appStore.state.mode)
    }

    @Test
    fun `GIVEN a new value for messageToShow WHEN NimbusMessageChange is called THEN update the current value`() =
        runTest {
            assertTrue(appStore.state.messaging.messageToShow.isEmpty())

            val message = Message(
                "message",
                MessageData(surface = FenixMessageSurfaceId.HOMESCREEN),
                "action",
                mockk(),
                emptyList(),
                emptyList(),
                mockk(),
            )
            appStore.dispatch(UpdateMessageToShow(message))

            assertFalse(appStore.state.messaging.messageToShow.isEmpty())
        }

    @Test
    fun `Test changing the collections in AppStore`() = runTest {
        assertEquals(0, appStore.state.collections.size)

        // Add 2 TabCollections to the AppStore.
        val tabCollections: List<TabCollection> = listOf(mockk(), mockk())
        appStore.dispatch(AppAction.CollectionsChange(tabCollections))

        assertEquals(tabCollections, appStore.state.collections)
    }

    @Test
    fun `Test changing the top sites in AppStore`() = runTest {
        assertEquals(0, appStore.state.topSites.size)

        // Add 2 TopSites to the AppStore.
        val topSites: List<TopSite> = listOf(mockk(), mockk())
        appStore.dispatch(AppAction.TopSitesChange(topSites))

        assertEquals(topSites, appStore.state.topSites)
    }

    @Test
    fun `Test changing the recent tabs in AppStore`() = runTest {
        val group1 = RecentHistoryGroup(title = "title1")
        val group2 = RecentHistoryGroup(title = "title2")
        val group3 = RecentHistoryGroup(title = "title3")
        val highlight = RecentHistoryHighlight(title = group2.title, "")
        appStore = AppStore(
            AppState(
                recentHistory = listOf(group1, group2, group3, highlight),
            ),
        )
        assertEquals(0, appStore.state.recentTabs.size)

        // Add 2 RecentTabs to the AppStore
        val recentTab1: RecentTab.Tab = mockk()
        val recentTabs: List<RecentTab> = listOf(recentTab1)
        appStore.dispatch(AppAction.RecentTabsChange(recentTabs))

        assertEquals(recentTabs, appStore.state.recentTabs)
        assertEquals(listOf(group1, group2, group3, highlight), appStore.state.recentHistory)
    }

    @Test
    fun `GIVEN initial state WHEN recent synced tab state is changed THEN state updated`() = runTest {
        appStore = AppStore(
            AppState(
                recentSyncedTabState = RecentSyncedTabState.None,
            ),
        )

        val loading = RecentSyncedTabState.Loading
        appStore.dispatch(AppAction.RecentSyncedTabStateChange(loading))
        assertEquals(loading, appStore.state.recentSyncedTabState)

        val recentSyncedTabs = listOf(RecentSyncedTab("device name", DeviceType.DESKTOP, "title", "url", null))
        val success = RecentSyncedTabState.Success(recentSyncedTabs)
        appStore.dispatch(AppAction.RecentSyncedTabStateChange(success))
        assertEquals(success, appStore.state.recentSyncedTabState)
        assertEquals(recentSyncedTabs, (appStore.state.recentSyncedTabState as RecentSyncedTabState.Success).tabs)
    }

    @Test
    fun `Test changing the history metadata in AppStore`() = runTest {
        assertEquals(0, appStore.state.recentHistory.size)

        val historyMetadata: List<RecentHistoryGroup> = listOf(mockk(), mockk())
        appStore.dispatch(AppAction.RecentHistoryChange(historyMetadata))

        assertEquals(historyMetadata, appStore.state.recentHistory)
    }

    @Test
    fun `Test removing a history highlight from AppStore`() = runTest {
        val g1 = RecentHistoryGroup(title = "group One")
        val g2 = RecentHistoryGroup(title = "grup two")
        val h1 = RecentHistoryHighlight(title = "highlight One", url = "url1")
        val h2 = RecentHistoryHighlight(title = "highlight two", url = "url2")
        val recentHistoryState = AppState(
            recentHistory = listOf(g1, g2, h1, h2),
        )
        appStore = AppStore(recentHistoryState)

        appStore.dispatch(AppAction.RemoveRecentHistoryHighlight("invalid"))
        assertEquals(recentHistoryState, appStore.state)

        appStore.dispatch(AppAction.RemoveRecentHistoryHighlight(h1.title))
        assertEquals(recentHistoryState, appStore.state)

        appStore.dispatch(AppAction.RemoveRecentHistoryHighlight(h1.url))
        assertEquals(
            recentHistoryState.copy(recentHistory = listOf(g1, g2, h2)),
            appStore.state,
        )
    }

    @Test
    fun `Test disbanding search group in AppStore`() = runTest {
        val g1 = RecentHistoryGroup(title = "test One")
        val g2 = RecentHistoryGroup(title = "test two")
        val h1 = RecentHistoryHighlight(title = "highlight One", url = "url1")
        val h2 = RecentHistoryHighlight(title = "highlight two", url = "url2")
        val recentHistory: List<RecentlyVisitedItem> = listOf(g1, g2, h1, h2)
        appStore.dispatch(AppAction.RecentHistoryChange(recentHistory))
        assertEquals(recentHistory, appStore.state.recentHistory)

        appStore.dispatch(AppAction.DisbandSearchGroupAction("Test one"))
        assertEquals(listOf(g2, h1, h2), appStore.state.recentHistory)
    }

    @Test
    fun `Test changing the expanded collections in AppStore`() = runTest {
        val collection: TabCollection = mockk<TabCollection>().apply {
            every { id } returns 0
        }

        // Expand the given collection.
        appStore.dispatch(AppAction.CollectionsChange(listOf(collection)))
        appStore.dispatch(AppAction.CollectionExpanded(collection, true))

        assertTrue(appStore.state.expandedCollections.contains(collection.id))
        assertEquals(1, appStore.state.expandedCollections.size)
    }

    @Test
    fun `Test changing the collections, mode, recent tabs and bookmarks, history metadata, top sites and recent synced tabs in the AppStore`() =
        runTest {
            // Verify that the default state of the HomeFragment is correct.
            assertEquals(0, appStore.state.collections.size)
            assertEquals(0, appStore.state.topSites.size)
            assertEquals(0, appStore.state.recentTabs.size)
            assertEquals(0, appStore.state.bookmarks.size)
            assertEquals(0, appStore.state.recentHistory.size)
            assertEquals(BrowsingMode.Normal, appStore.state.mode)
            assertEquals(
                RecentSyncedTabState.Success(recentSyncedTabsList),
                appStore.state.recentSyncedTabState,
            )

            val collections: List<TabCollection> = listOf(mockk())
            val topSites: List<TopSite> = listOf(mockk(), mockk())
            val recentTabs: List<RecentTab> = listOf(mockk(), mockk())
            val bookmarks: List<Bookmark> = listOf(mockk(), mockk())
            val group1 = RecentHistoryGroup(title = "test One")
            val group2 = RecentHistoryGroup(title = "testSearchTerm")
            val group3 = RecentHistoryGroup(title = "test two")
            val highlight = RecentHistoryHighlight(group2.title, "")
            val recentHistory: List<RecentlyVisitedItem> = listOf(group1, group2, group3, highlight)
            val recentSyncedTab = RecentSyncedTab(
                deviceDisplayName = "device1",
                deviceType = mockk(relaxed = true),
                title = "1",
                url = "",
                previewImageUrl = null,
            )
            val recentSyncedTabState: RecentSyncedTabState =
                RecentSyncedTabState.Success(recentSyncedTabsList + recentSyncedTab)

            appStore.dispatch(
                AppAction.Change(
                    collections = collections,
                    mode = BrowsingMode.Private,
                    topSites = topSites,
                    recentTabs = recentTabs,
                    bookmarks = bookmarks,
                    recentHistory = recentHistory,
                    recentSyncedTabState = recentSyncedTabState,
                ),
            )

            assertEquals(collections, appStore.state.collections)
            assertEquals(topSites, appStore.state.topSites)
            assertEquals(recentTabs, appStore.state.recentTabs)
            assertEquals(bookmarks, appStore.state.bookmarks)
            assertEquals(listOf(group1, group2, group3, highlight), appStore.state.recentHistory)
            assertEquals(BrowsingMode.Private, appStore.state.mode)
            assertEquals(
                recentSyncedTabState,
                appStore.state.recentSyncedTabState,
            )
        }

    @Test
    fun `Test selecting a Pocket recommendations category`() = runTest {
        val otherCategoryName = "other"
        val anotherCategoryName = "another"

        val storyInOtherCategory = mockk<PocketRecommendedStory>(name = "storyInOther").apply {
            every { title } returns "story_other"
            every { timesShown } returns 0
        }
        val storyInAnotherCategory = mockk<PocketRecommendedStory>(name = "storyInAnother").apply {
            every { title } returns "story_another"
            every { timesShown } returns 0
        }

        val otherStoriesCategory = PocketRecommendedStoriesCategory(
            name = otherCategoryName,
            stories = listOf(storyInOtherCategory),
        )
        val anotherStoriesCategory = PocketRecommendedStoriesCategory(
            name = anotherCategoryName,
            stories = listOf(storyInAnotherCategory),
        )

        appStore = AppStore(
            AppState(
                recommendationState = ContentRecommendationsState(
                    pocketStoriesCategories = listOf(otherStoriesCategory, anotherStoriesCategory),
                    pocketStoriesCategoriesSelections = listOf(
                        PocketRecommendedStoriesSelectedCategory(otherStoriesCategory.name),
                    ),
                ),
            ),
        )

        appStore.dispatch(ContentRecommendationsAction.SelectPocketStoriesCategory(anotherCategoryName))

        val selectedCategoriesState = appStore.state.recommendationState.pocketStoriesCategoriesSelections
        assertEquals("Two categories should now be selected", 2, selectedCategoriesState.size)
        assertEquals("The 'other' category should remain selected", otherCategoryName, selectedCategoriesState[0].name)
        assertEquals("The 'another' category should also be now selected", anotherCategoryName, selectedCategoriesState[1].name)

        // using set to compare the stories because the order in list is not deterministic.
        val expectedPocketStories = listOf(storyInOtherCategory, storyInAnotherCategory).toSet()

        assertEquals(
            "Pocket stories should match the stories from the 'another' category",
            expectedPocketStories,
            appStore.state.recommendationState.pocketStories.toSet(),
        )
    }

    @Test
    fun `Test deselecting a Pocket recommendations category`() = runTest {
        val otherCategoryName = "other"
        val anotherCategoryName = "another"

        val storyInOtherCategory = mockk<PocketRecommendedStory>(name = "storyInOther").apply {
            every { title } returns "story_other"
            every { timesShown } returns 0
        }
        val storyInAnotherCategory = mockk<PocketRecommendedStory>(name = "storyInAnother").apply {
            every { title } returns "story_another"
            every { timesShown } returns 0
        }

        val otherStoriesCategory = PocketRecommendedStoriesCategory(
            name = otherCategoryName,
            stories = listOf(storyInOtherCategory),
        )
        val anotherStoriesCategory = PocketRecommendedStoriesCategory(
            name = anotherCategoryName,
            stories = listOf(storyInAnotherCategory),
        )

        val expectedPocketStories = listOf(storyInAnotherCategory)

        appStore = AppStore(
            AppState(
                recommendationState = ContentRecommendationsState(
                    pocketStoriesCategories = listOf(otherStoriesCategory, anotherStoriesCategory),
                    pocketStoriesCategoriesSelections = listOf(
                        PocketRecommendedStoriesSelectedCategory(otherStoriesCategory.name),
                        PocketRecommendedStoriesSelectedCategory(anotherStoriesCategory.name),
                    ),
                ),
            ),
        )

        appStore.dispatch(ContentRecommendationsAction.DeselectPocketStoriesCategory(otherCategoryName))

        val selectedCategoriesState = appStore.state.recommendationState.pocketStoriesCategoriesSelections
        assertEquals("Only one category should remain selected", 1, selectedCategoriesState.size)
        assertEquals("The 'another' category should be the one remaining selected", anotherCategoryName, selectedCategoriesState[0].name)

        assertEquals(
            "Pocket stories should match the stories from the 'another' category",
            expectedPocketStories,
            appStore.state.recommendationState.pocketStories,
        )
    }

    @Test
    fun `Test cleaning the list of Pocket stories`() = runTest {
        appStore = AppStore(
            AppState(
                recommendationState = ContentRecommendationsState(
                    pocketStoriesCategories = listOf(mockk()),
                    pocketStoriesCategoriesSelections = listOf(mockk()),
                    pocketStories = listOf(mockk()),
                    contentRecommendations = listOf(mockk()),
                    sponsoredContents = listOf(mockk()),
                ),
            ),
        )

        appStore.dispatch(ContentRecommendationsAction.PocketStoriesClean)

        assertTrue(appStore.state.recommendationState.pocketStoriesCategories.isEmpty())
        assertTrue(appStore.state.recommendationState.pocketStoriesCategoriesSelections.isEmpty())
        assertTrue(appStore.state.recommendationState.pocketStories.isEmpty())
        assertTrue(appStore.state.recommendationState.contentRecommendations.isEmpty())
        assertTrue(appStore.state.recommendationState.sponsoredContents.isEmpty())
    }

    @Test
    fun `GIVEN content recommendations are enabled WHEN updating the list of sponsored contents THEN update the list of stories to show`() = runTest {
        val baseRecommendation = mockk<ContentRecommendation>(name = "baseRec_277").apply {
            every { url } returns "http://example.com/rec277"
            every { title } returns "Base Recommendation Title"
            every { corpusItemId } returns "corpusId_277"
            every { scheduledCorpusItemId } returns "scheduledId_277"
            every { excerpt } returns "This is a base recommendation excerpt."
            every { topic } returns "Sample Topic"
            every { publisher } returns "Test Publisher"
            every { isTimeSensitive } returns false
            every { imageUrl } returns "http://example.com/image_rec277.jpg"
            every { tileId } returns 277L
            every { receivedRank } returns 1
            every { recommendedAt } returns System.currentTimeMillis() / 1000
            every { impressions } returns 0L
        }

        val sponsoredContentCallbacks = SponsoredContentCallbacks(
            clickUrl = "https://firefox.com/click",
            impressionUrl = "https://firefox.com/impression",
        )
        // Configure caps to make the content item filtered out
        val sponsoredContentFrequencyCapsFilterOut = SponsoredContentFrequencyCaps(
            currentImpressions = listOf(System.currentTimeMillis() / 1000), // Has one recent impression
            flightCount = 1, // Allowed only 1 impression in the period
            flightPeriod = 86400, // e.g., 1 day
        )

        val sponsoredContent1 = SponsoredContent(
            url = "https://firefox.com",
            title = "Firefox",
            callbacks = sponsoredContentCallbacks,
            imageUrl = "https://test.com/image1.jpg",
            domain = "firefox.com",
            excerpt = "Mozilla Firefox",
            sponsor = "Mozilla",
            blockKey = "sponsoredBlockKey1",
            caps = sponsoredContentFrequencyCapsFilterOut,
            priority = 3,
        )

        val sponsoredContent2 = sponsoredContent1.copy(
            url = "https://firefox.com/2",
            blockKey = "sponsoredBlockKey2",
        )

        appStore = AppStore(
            AppState(
                recommendationState = ContentRecommendationsState(
                    contentRecommendations = listOf(baseRecommendation),
                ),
            ),
        )

        var currentSponsoredContentsToShow = listOf(sponsoredContent1, sponsoredContent2)

        appStore.dispatch(
            ContentRecommendationsAction.SponsoredContentsChange(
                sponsoredContents = currentSponsoredContentsToShow,
            ),
        )

        assertEquals(currentSponsoredContentsToShow, appStore.state.recommendationState.sponsoredContents)
        assertEquals(
            "After first dispatch, pocketStories should only contain the base recommendation",
            listOf<PocketStory>(baseRecommendation),
            appStore.state.recommendationState.pocketStories,
        )

        currentSponsoredContentsToShow = listOf(sponsoredContent1)

        appStore.dispatch(
            ContentRecommendationsAction.SponsoredContentsChange(
                sponsoredContents = currentSponsoredContentsToShow,
            ),
        )

        assertEquals(currentSponsoredContentsToShow, appStore.state.recommendationState.sponsoredContents)
        assertEquals(
            "After second dispatch, pocketStories should still only contain the base recommendation",
            listOf<PocketStory>(baseRecommendation),
            appStore.state.recommendationState.pocketStories,
        )
    }

    @Test
    fun `WHEN sponsored contents are shown THEN update the impressions of sponsored contents`() = runTest {
        val sponsoredContent = SponsoredContent(
            url = "https://firefox.com",
            title = "Firefox",
            callbacks = SponsoredContentCallbacks(
                clickUrl = "https://firefox.com/click",
                impressionUrl = "https://firefox.com/impression",
            ),
            imageUrl = "https://test.com/image1.jpg",
            domain = "firefox.com",
            excerpt = "Mozilla Firefox",
            sponsor = "Mozilla",
            blockKey = "1",
            caps = SponsoredContentFrequencyCaps(
                currentImpressions = listOf(1, 2),
                flightCount = 10,
                flightPeriod = 86400,
            ),
            priority = 3,
        )
        val sponsoredContent2 = sponsoredContent.copy(url = "https://firefox.com/2")
        val sponsoredContent3 = sponsoredContent.copy(url = "https://firefox.com/3")
        val sponsoredContent4 = sponsoredContent.copy(url = "https://firefox.com/4")
        appStore = AppStore(
            AppState(
                recommendationState = ContentRecommendationsState(
                    sponsoredContents = listOf(
                        sponsoredContent,
                        sponsoredContent2,
                        sponsoredContent3,
                        sponsoredContent4,
                    ),
                ),
            ),
        )

        appStore.dispatch(
            ContentRecommendationsAction.PocketStoriesShown(
                impressions = listOf(
                    PocketImpression(story = sponsoredContent, position = 0),
                    PocketImpression(story = sponsoredContent3, position = 2),
                ),
                source = StoriesImpressionSource.HOMEPAGE,
            ),
        )

        assertEquals(4, appStore.state.recommendationState.sponsoredContents.size)
        assertEquals(3, appStore.state.recommendationState.sponsoredContents[0].caps.currentImpressions.size)
        assertEquals(2, appStore.state.recommendationState.sponsoredContents[1].caps.currentImpressions.size)
        assertEquals(3, appStore.state.recommendationState.sponsoredContents[2].caps.currentImpressions.size)
        assertEquals(2, appStore.state.recommendationState.sponsoredContents[3].caps.currentImpressions.size)
    }

    @Test
    fun `WHEN content recommendations are shown THEN update the impressions of recommendations`() = runTest {
        val recommendation1 = ContentRecommendation(
            corpusItemId = "0",
            scheduledCorpusItemId = "1",
            url = "testUrl",
            title = "",
            excerpt = "",
            topic = "",
            publisher = "",
            isTimeSensitive = false,
            imageUrl = "",
            tileId = 1,
            receivedRank = 33,
            recommendedAt = 1L,
            impressions = 0,
        )
        val recommendation2 = recommendation1.copy(scheduledCorpusItemId = "2")
        val recommendation3 = recommendation1.copy(scheduledCorpusItemId = "3")
        val recommendation4 = recommendation1.copy(scheduledCorpusItemId = "4")

        appStore = AppStore(
            AppState(
                recommendationState = ContentRecommendationsState(
                    contentRecommendations = listOf(
                        recommendation1,
                        recommendation2,
                        recommendation3,
                        recommendation4,
                    ),
                ),
            ),
        )

        appStore.dispatch(
            ContentRecommendationsAction.PocketStoriesShown(
                listOf(
                    PocketImpression(story = recommendation1, position = 0),
                    PocketImpression(story = recommendation3, position = 2),
                ),
                source = StoriesImpressionSource.HOMEPAGE,
            ),
        )

        assertEquals(4, appStore.state.recommendationState.contentRecommendations.size)
        assertEquals(1, appStore.state.recommendationState.contentRecommendations[0].impressions)
        assertEquals(0, appStore.state.recommendationState.contentRecommendations[1].impressions)
        assertEquals(1, appStore.state.recommendationState.contentRecommendations[2].impressions)
        assertEquals(0, appStore.state.recommendationState.contentRecommendations[3].impressions)
    }

    @Test
    fun `Test updating the list of Pocket recommendations categories`() = runTest {
        val storyForDefaultCategory = mockk<PocketRecommendedStory>(name = "storyForDefaultCategory").apply {
            every { timesShown } returns 0
        }
        val storyForYetAnotherCategory = mockk<PocketRecommendedStory>(name = "storyForYetAnotherCategory").apply {
            every { timesShown } returns 0
        }

        val defaultPocketCategoryWithStory = PocketRecommendedStoriesCategory(
            name = POCKET_STORIES_DEFAULT_CATEGORY_NAME,
            stories = listOf(storyForDefaultCategory),
        )

        val anotherPocketCategory = PocketRecommendedStoriesCategory(
            name = "anotherRandomCategoryName",
            stories = emptyList(),
        )

        val yetAnotherPocketCategory = PocketRecommendedStoriesCategory(
            name = "yetAnotherUpdatedCategoryName",
            stories = listOf(storyForYetAnotherCategory),
        )

        appStore = AppStore(
            AppState(
                recommendationState = ContentRecommendationsState(),
            ),
        )

        val categoriesForFirstDispatch = listOf(defaultPocketCategoryWithStory, anotherPocketCategory)
        appStore.dispatch(
            ContentRecommendationsAction.PocketStoriesCategoriesChange(categoriesForFirstDispatch),
        )

        assertTrue(
            "Available categories should contain the dispatched categories",
            appStore.state.recommendationState.pocketStoriesCategories.containsAll(categoriesForFirstDispatch),
        )
        assertEquals(
            "Number of available categories should match",
            categoriesForFirstDispatch.size,
            appStore.state.recommendationState.pocketStoriesCategories.size,
        )

        assertEquals(
            "Pocket stories should contain stories from the default category",
            listOf(storyForDefaultCategory),
            appStore.state.recommendationState.pocketStories,
        )

        val categoriesForSecondDispatch = listOf(yetAnotherPocketCategory)
        appStore.dispatch(
            ContentRecommendationsAction.PocketStoriesCategoriesChange(categoriesForSecondDispatch),
        )

        assertTrue(
            "Available categories should contain the newly dispatched categories",
            appStore.state.recommendationState.pocketStoriesCategories.containsAll(categoriesForSecondDispatch),
        )
        assertEquals(
            "Number of available categories should match for the second dispatch",
            categoriesForSecondDispatch.size,
            appStore.state.recommendationState.pocketStoriesCategories.size,
        )

        assertEquals(
            "Pocket stories should be empty as the default category is not present and no selections made",
            emptyList<PocketRecommendedStory>(),
            appStore.state.recommendationState.pocketStories,
        )
    }

    @Test
    fun `Test updating the list of selected Pocket recommendations categories`() = runTest {
        val otherStoriesCategory = PocketRecommendedStoriesCategory("other")
        val anotherStoriesCategory = PocketRecommendedStoriesCategory("another")
        val selectedCategory = PocketRecommendedStoriesSelectedCategory("selected")
        appStore = AppStore(AppState())

        appStore.dispatch(
            ContentRecommendationsAction.PocketStoriesCategoriesSelectionsChange(
                storiesCategories = listOf(otherStoriesCategory, anotherStoriesCategory),
                categoriesSelected = listOf(selectedCategory),
            ),
        )

        assertTrue(
            appStore.state.recommendationState.pocketStoriesCategories.containsAll(
                listOf(otherStoriesCategory, anotherStoriesCategory),
            ),
        )
        assertTrue(
            appStore.state.recommendationState.pocketStoriesCategoriesSelections.containsAll(
                listOf(
                    selectedCategory,
                ),
            ),
        )
    }

    @Test
    fun `Test filtering out search groups`() {
        val group1 = RecentHistoryGroup("title1")
        val group2 = RecentHistoryGroup("title2")
        val group3 = RecentHistoryGroup("title3")
        val highLight1 = RecentHistoryHighlight("title1", "")
        val highLight2 = RecentHistoryHighlight("title2", "")
        val highLight3 = RecentHistoryHighlight("title3", "")
        val recentHistory = listOf(group1, highLight1, group2, highLight2, group3, highLight3)

        assertEquals(recentHistory, recentHistory.filterOut(null))
        assertEquals(recentHistory, recentHistory.filterOut(""))
        assertEquals(recentHistory, recentHistory.filterOut(" "))
        assertEquals(recentHistory - group2, recentHistory.filterOut("Title2"))
        assertEquals(recentHistory - group3, recentHistory.filterOut("title3"))
    }

    @Test
    fun `WHEN content recommendations are fetched THEN update the list of content recommendations and pocket stories`() = runTest {
        val recommendations = listOf(mockk<ContentRecommendation>())

        appStore = AppStore(
            AppState(
                recommendationState = ContentRecommendationsState(),
            ),
        )

        appStore.dispatch(
            ContentRecommendationsAction.ContentRecommendationsFetched(
                recommendations = recommendations,
            ),
        )

        assertEquals(recommendations, appStore.state.recommendationState.contentRecommendations)
        assertEquals(recommendations, appStore.state.recommendationState.pocketStories)
    }

    @Test
    fun `WHEN init action is dispatched THEN the setup checklist state remains the same`() {
        val appState = AppState(setupChecklistState = SetupChecklistState())

        appStore.dispatch(AppAction.SetupChecklistAction.Init)

        assertEquals(SetupChecklistState(), appState.setupChecklistState)
    }

    @Test
    fun `WHEN closed action is dispatched THEN the setup checklist state visible value is updated`() {
        val appState = AppState(setupChecklistState = SetupChecklistState())

        appStore.dispatch(AppAction.SetupChecklistAction.Closed)

        assertEquals(SetupChecklistState(isVisible = true), appState.setupChecklistState)
    }

    @Test
    fun `WHEN a group item is clicked action is dispatched THEN the group's expanded state is updated`() {
        val expandedGroup = ChecklistItem.Group(
            title = R.string.setup_checklist_group_essentials,
            tasks = listOf(
                ChecklistItem.Task(
                    type = ChecklistItem.Task.Type.SET_AS_DEFAULT,
                    title = R.string.setup_checklist_task_default_browser,
                    icon = iconsR.drawable.mozac_ic_extension_24,
                    isCompleted = false,
                ),
            ),
            isExpanded = true,
        )
        val collapsedGroup = ChecklistItem.Group(
            title = R.string.setup_checklist_group_essentials,
            tasks = listOf(
                ChecklistItem.Task(
                    type = ChecklistItem.Task.Type.INSTALL_SEARCH_WIDGET,
                    title = R.string.setup_checklist_task_search_widget_2,
                    icon = iconsR.drawable.mozac_ic_extension_24,
                    isCompleted = false,
                ),
            ),
            isExpanded = false,
        )

        val appState = AppState(
            setupChecklistState = SetupChecklistState(
                checklistItems = listOf(
                    expandedGroup,
                    collapsedGroup,
                ),
            ),
        )
        val store = AppStore(appState)

        // Verify that the expanded group is expanded, and the other one is not
        assertTrue((store.state.setupChecklistState!!.checklistItems[0] as ChecklistItem.Group).isExpanded)
        assertFalse((store.state.setupChecklistState!!.checklistItems[1] as ChecklistItem.Group).isExpanded)

        store.dispatch(AppAction.SetupChecklistAction.ChecklistItemClicked(collapsedGroup))

        // Verify that the expanded group was collapsed, and the other one got expanded
        assertFalse((store.state.setupChecklistState!!.checklistItems[0] as ChecklistItem.Group).isExpanded)
        assertTrue((store.state.setupChecklistState!!.checklistItems[1] as ChecklistItem.Group).isExpanded)
    }

    @Test
    fun `WHEN a task item is clicked action is dispatched THEN the task's completed state is not updated`() {
        val task = ChecklistItem.Task(
            type = ChecklistItem.Task.Type.EXPLORE_EXTENSION,
            title = R.string.setup_checklist_task_default_browser,
            icon = iconsR.drawable.mozac_ic_extension_24,
            isCompleted = false,
        )

        val appState =
            AppState(setupChecklistState = SetupChecklistState(checklistItems = listOf(task)))
        val store = AppStore(appState)
        store.dispatch(AppAction.SetupChecklistAction.ChecklistItemClicked(task))

        assertFalse((store.state.setupChecklistState!!.checklistItems[0] as ChecklistItem.Task).isCompleted)
    }

    @Test
    fun `WHEN a task preference updated action is dispatched THEN the task's completed state is updated`() {
        val task = ChecklistItem.Task(
            type = ChecklistItem.Task.Type.EXPLORE_EXTENSION,
            title = R.string.setup_checklist_task_default_browser,
            icon = iconsR.drawable.mozac_ic_extension_24,
            isCompleted = false,
        )

        val appState =
            AppState(setupChecklistState = SetupChecklistState(checklistItems = listOf(task)))
        val store = AppStore(appState)
        store.dispatch(AppAction.SetupChecklistAction.TaskPreferenceUpdated(task.type, true))

        assertTrue((store.state.setupChecklistState!!.checklistItems[0] as ChecklistItem.Task).isCompleted)

        store.dispatch(AppAction.SetupChecklistAction.TaskPreferenceUpdated(task.type, false))

        assertFalse((store.state.setupChecklistState!!.checklistItems[0] as ChecklistItem.Task).isCompleted)
    }

    @Test
    fun `WHEN a groups task preference is updated THEN only the updated task's completed state is updated`() {
        val updatedTask = ChecklistItem.Task(
            type = ChecklistItem.Task.Type.SET_AS_DEFAULT,
            title = R.string.setup_checklist_task_default_browser,
            icon = iconsR.drawable.mozac_ic_extension_24,
            isCompleted = false,
        )
        val nonUpdatedTask = ChecklistItem.Task(
            type = ChecklistItem.Task.Type.INSTALL_SEARCH_WIDGET,
            title = R.string.setup_checklist_task_search_widget_2,
            icon = iconsR.drawable.mozac_ic_extension_24,
            isCompleted = false,
        )
        val group = ChecklistItem.Group(
            title = R.string.setup_checklist_group_essentials,
            tasks = listOf(updatedTask, nonUpdatedTask),
            isExpanded = true,
        )

        val appState =
            AppState(setupChecklistState = SetupChecklistState(checklistItems = listOf(group)))
        val store = AppStore(appState)
        store.dispatch(AppAction.SetupChecklistAction.TaskPreferenceUpdated(updatedTask.type, true))

        assertTrue((store.state.setupChecklistState!!.checklistItems[0] as ChecklistItem.Group).tasks[0].isCompleted)
        assertFalse((store.state.setupChecklistState!!.checklistItems[0] as ChecklistItem.Group).tasks[1].isCompleted)

        store.dispatch(
            AppAction.SetupChecklistAction.TaskPreferenceUpdated(
                updatedTask.type,
                false,
            ),
        )

        assertFalse((store.state.setupChecklistState!!.checklistItems[0] as ChecklistItem.Group).tasks[0].isCompleted)
        assertFalse((store.state.setupChecklistState!!.checklistItems[0] as ChecklistItem.Group).tasks[1].isCompleted)
    }
}
