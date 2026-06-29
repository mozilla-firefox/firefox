/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.pocket

import androidx.navigation.NavController
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import io.mockk.verifyOrder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.runTest
import mozilla.components.service.pocket.PocketStory
import mozilla.components.service.pocket.PocketStory.ContentRecommendation
import mozilla.components.service.pocket.PocketStory.PocketRecommendedStory
import mozilla.components.service.pocket.PocketStory.PocketSponsoredStory
import mozilla.components.service.pocket.PocketStory.SponsoredContent
import mozilla.components.service.pocket.PocketStory.SponsoredContentCallbacks
import mozilla.components.service.pocket.PocketStory.SponsoredContentFrequencyCaps
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Pocket
import org.mozilla.fenix.GleanMetrics.StoriesLibrary
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.ContentRecommendationsAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.recommendations.ContentRecommendationsState
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.home.mars.MARSUseCases
import org.mozilla.fenix.home.pocket.controller.DefaultPocketStoriesController
import org.mozilla.fenix.home.pocket.controller.StoriesImpressionSource
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.Stories.markAsOpenedFromHomeScreen
import org.mozilla.fenix.utils.Stories.markAsOpenedFromStoriesScreen
import java.lang.ref.WeakReference
import kotlin.test.assertNotNull

@RunWith(AndroidJUnit4::class)
class DefaultPocketStoriesControllerTest {

    @get:Rule
    val gleanTestRule = FenixGleanTestRule(testContext)

    private val navController: NavController = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
    private val marsUseCases: MARSUseCases = mockk(relaxed = true)
    private val nowInSeconds = System.currentTimeMillis() / 1000
    private val flightPeriod = 100

    // Data for "shown" tests, expecting 3 previous impressions.
    private val impressionsForShownTest = listOf(
        nowInSeconds - flightPeriod * 2, // This one is old and will be filtered out
        nowInSeconds - flightPeriod / 2,
        nowInSeconds - flightPeriod / 3,
        nowInSeconds - flightPeriod / 4,
    )
    private val contentCapsForShownTest = SponsoredContentFrequencyCaps(
        currentImpressions = impressionsForShownTest,
        flightCount = 5,
        flightPeriod = flightPeriod,
    )

    // Data for "clicked" tests, expecting 2 previous impressions.
    private val impressionsForClickedTest = listOf(
        nowInSeconds - flightPeriod * 2, // This one is old and will be filtered out
        nowInSeconds - flightPeriod / 2,
        nowInSeconds - flightPeriod / 3,
    )
    private val contentCapsForClickedTest = SponsoredContentFrequencyCaps(
        currentImpressions = impressionsForClickedTest,
        flightCount = 5,
        flightPeriod = flightPeriod,
    )

    @Test
    fun `GIVEN a category is selected WHEN that same category is clicked THEN deselect it and record telemetry`() = runTest {
        val category1 = PocketRecommendedStoriesCategory("cat1", emptyList())
        val category2 = PocketRecommendedStoriesCategory("cat2", emptyList())
        val selections = listOf(PocketRecommendedStoriesSelectedCategory(category2.name))
        val store = spyk(
            AppStore(
                AppState(
                    recommendationState = ContentRecommendationsState(
                        pocketStoriesCategories = listOf(category1, category2),
                        pocketStoriesCategoriesSelections = selections,
                    ),
                ),
            ),
        )
        val controller = createController(scope = this, appStore = store)
        assertNull(Pocket.homeRecsCategoryClicked.testGetValue())

        controller.handleCategoryClick(category2)
        verify(exactly = 0) { store.dispatch(ContentRecommendationsAction.SelectPocketStoriesCategory(category2.name)) }
        verify { store.dispatch(ContentRecommendationsAction.DeselectPocketStoriesCategory(category2.name)) }

        assertNotNull(Pocket.homeRecsCategoryClicked.testGetValue())
        val event = Pocket.homeRecsCategoryClicked.testGetValue()!!
        assertEquals(1, event.size)
        assertTrue(event.single().extra!!.containsKey("category_name"))
        assertEquals(category2.name, event.single().extra!!["category_name"])
        assertTrue(event.single().extra!!.containsKey("new_state"))
        assertEquals("deselected", event.single().extra!!["new_state"])
        assertTrue(event.single().extra!!.containsKey("selected_total"))
        assertEquals("1", event.single().extra!!["selected_total"])
    }

    @Test
    fun `GIVEN 8 categories are selected WHEN when a new one is clicked THEN the oldest selected is deselected before selecting the new one and record telemetry`() = runTest {
        val category1 = PocketRecommendedStoriesSelectedCategory(name = "cat1", selectionTimestamp = 111)
        val category2 = PocketRecommendedStoriesSelectedCategory(name = "cat2", selectionTimestamp = 222)
        val category3 = PocketRecommendedStoriesSelectedCategory(name = "cat3", selectionTimestamp = 333)
        val oldestSelectedCategory = PocketRecommendedStoriesSelectedCategory(name = "oldestSelectedCategory", selectionTimestamp = 0)
        val category4 = PocketRecommendedStoriesSelectedCategory(name = "cat4", selectionTimestamp = 444)
        val category5 = PocketRecommendedStoriesSelectedCategory(name = "cat5", selectionTimestamp = 555)
        val category6 = PocketRecommendedStoriesSelectedCategory(name = "cat6", selectionTimestamp = 678)
        val category7 = PocketRecommendedStoriesSelectedCategory(name = "cat7", selectionTimestamp = 890)
        val newSelectedCategory = PocketRecommendedStoriesSelectedCategory(name = "newSelectedCategory", selectionTimestamp = 654321)
        val store = spyk(
            AppStore(
                AppState(
                    recommendationState = ContentRecommendationsState(
                        pocketStoriesCategoriesSelections = listOf(
                            category1,
                            category2,
                            category3,
                            category4,
                            category5,
                            category6,
                            category7,
                            oldestSelectedCategory,
                        ),
                    ),
                ),
            ),
        )
        val controller = createController(scope = this, appStore = store)
        assertNull(Pocket.homeRecsCategoryClicked.testGetValue())

        controller.handleCategoryClick(PocketRecommendedStoriesCategory(newSelectedCategory.name))

        verify { store.dispatch(ContentRecommendationsAction.DeselectPocketStoriesCategory(oldestSelectedCategory.name)) }
        verify { store.dispatch(ContentRecommendationsAction.SelectPocketStoriesCategory(newSelectedCategory.name)) }

        assertNotNull(Pocket.homeRecsCategoryClicked.testGetValue())
        val event = Pocket.homeRecsCategoryClicked.testGetValue()!!
        assertEquals(1, event.size)
        assertTrue(event.single().extra!!.containsKey("category_name"))
        assertEquals(newSelectedCategory.name, event.single().extra!!["category_name"])
        assertTrue(event.single().extra!!.containsKey("new_state"))
        assertEquals("selected", event.single().extra!!["new_state"])
        assertTrue(event.single().extra!!.containsKey("selected_total"))
        assertEquals("8", event.single().extra!!["selected_total"])
    }

    @Test
    fun `GIVEN fewer than 8 categories are selected WHEN when a new one is clicked THEN don't deselect anything but select the newly clicked category and record telemetry`() = runTest {
        val category1 = PocketRecommendedStoriesSelectedCategory(name = "cat1", selectionTimestamp = 111)
        val category2 = PocketRecommendedStoriesSelectedCategory(name = "cat2", selectionTimestamp = 222)
        val category3 = PocketRecommendedStoriesSelectedCategory(name = "cat3", selectionTimestamp = 333)
        val oldestSelectedCategory = PocketRecommendedStoriesSelectedCategory(name = "oldestSelectedCategory", selectionTimestamp = 0)
        val category4 = PocketRecommendedStoriesSelectedCategory(name = "cat4", selectionTimestamp = 444)
        val category5 = PocketRecommendedStoriesSelectedCategory(name = "cat5", selectionTimestamp = 555)
        val category6 = PocketRecommendedStoriesSelectedCategory(name = "cat6", selectionTimestamp = 678)
        val store = spyk(
            AppStore(
                AppState(
                    recommendationState = ContentRecommendationsState(
                        pocketStoriesCategoriesSelections = listOf(
                            category1,
                            category2,
                            category3,
                            category4,
                            category5,
                            category6,
                            oldestSelectedCategory,
                        ),
                    ),
                ),
            ),
        )
        val newSelectedCategoryName = "newSelectedCategory"
        val controller = createController(scope = this, appStore = store)

        controller.handleCategoryClick(PocketRecommendedStoriesCategory(newSelectedCategoryName))

        verify(exactly = 0) { store.dispatch(ContentRecommendationsAction.DeselectPocketStoriesCategory(oldestSelectedCategory.name)) }
        verify { store.dispatch(ContentRecommendationsAction.SelectPocketStoriesCategory(newSelectedCategoryName)) }

        assertNotNull(Pocket.homeRecsCategoryClicked.testGetValue())
        val event = Pocket.homeRecsCategoryClicked.testGetValue()!!
        assertEquals(1, event.size)
        assertTrue(event.single().extra!!.containsKey("category_name"))
        assertEquals(newSelectedCategoryName, event.single().extra!!["category_name"])
        assertTrue(event.single().extra!!.containsKey("new_state"))
        assertEquals("selected", event.single().extra!!["new_state"])
        assertTrue(event.single().extra!!.containsKey("selected_total"))
        assertEquals("7", event.single().extra!!["selected_total"])
    }

    @Test
    fun `WHEN a new recommended story is shown THEN update the State`() = runTest {
        val store = spyk(AppStore())
        val controller = createController(scope = this, appStore = store)
        val storyShown: PocketRecommendedStory = mockk()
        val storyPosition = Triple(1, 2, 3)

        controller.handleStoryShown(storyShown, storyPosition)

        verify {
            store.dispatch(
                ContentRecommendationsAction.PocketStoriesShown(
                    impressions = listOf(
                        PocketImpression(story = storyShown, position = 3),
                    ),
                    source = StoriesImpressionSource.HOMEPAGE,
                ),
            )
        }
    }

    @Test
    fun `WHEN a sponsored content is shown THEN update the State and record telemetry`() = runTest {
        val store = spyk(AppStore())
        val controller = createController(scope = this, appStore = store)
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
            caps = contentCapsForShownTest,
            priority = 3,
        )

        controller.handleStoryShown(sponsoredContent, storyPosition = Triple(1, 2, 3))

        assertEquals(1, Pocket.homeRecsSpocShown.testGetValue()!!.size)

        val data = Pocket.homeRecsSpocShown.testGetValue()!!.single().extra
        assertEquals(2, data?.size)
        assertEquals("1x2", data?.entries?.first { it.key == "position" }?.value)
        assertEquals("4", data?.entries?.first { it.key == "times_shown" }?.value)

        verify {
            store.dispatch(
                ContentRecommendationsAction.PocketStoriesShown(
                    impressions = listOf(
                        PocketImpression(story = sponsoredContent, position = 3),
                    ),
                    source = StoriesImpressionSource.HOMEPAGE,
                ),
            )
        }
    }

    @Test
    fun `WHEN new stories are shown THEN update the State and record telemetry`() = runTest {
        val store = spyk(AppStore())
        val controller = createController(scope = this, appStore = store)
        val recommendation = mockk<ContentRecommendation>()
        val story = mockk<PocketStory>()
        val sponsoredStory = mockk<PocketSponsoredStory>()
        val storiesShown = listOf(recommendation, story, sponsoredStory)

        assertNull(Pocket.homeRecsShown.testGetValue())

        controller.handleStoriesShown(storiesShown, StoriesImpressionSource.HOMEPAGE)

        verify {
            store.dispatch(
                ContentRecommendationsAction.PocketStoriesShown(
                    impressions = listOf(
                        PocketImpression(story = recommendation, position = 0),
                        PocketImpression(story = story, position = 1),
                    ),
                    source = StoriesImpressionSource.HOMEPAGE,
                ),
            )
        }

        assertNotNull(Pocket.homeRecsShown.testGetValue())
        assertEquals(1, Pocket.homeRecsShown.testGetValue()!!.size)
        assertEquals(
            "homepage",
            Pocket.homeRecsShown.testGetValue()!!.single().extra?.get("source"),
        )
    }

    @Test
    fun `WHEN new stories are shown on the stories screen THEN update the State and record telemetry`() = runTest {
        val store = spyk(AppStore())
        val controller = createController(scope = this, appStore = store)
        val recommendation = mockk<ContentRecommendation>()
        val story = mockk<PocketStory>()
        val sponsoredStory = mockk<PocketSponsoredStory>()
        val storiesShown = listOf(recommendation, story, sponsoredStory)

        assertNull(Pocket.homeRecsShown.testGetValue())

        controller.handleStoriesShown(storiesShown, StoriesImpressionSource.STORIES_SCREEN)

        verify {
            store.dispatch(
                ContentRecommendationsAction.PocketStoriesShown(
                    impressions = listOf(
                        PocketImpression(story = recommendation, position = 0),
                        PocketImpression(story = story, position = 1),
                    ),
                    source = StoriesImpressionSource.STORIES_SCREEN,
                ),
            )
        }

        assertNotNull(Pocket.homeRecsShown.testGetValue())
        assertEquals(1, Pocket.homeRecsShown.testGetValue()!!.size)
        assertEquals(
            "stories_screen",
            Pocket.homeRecsShown.testGetValue()!!.single().extra?.get("source"),
        )
    }

    @Test
    fun `WHEN a recommended story is clicked THEN open that story's url using HomeActivity and record telemetry`() = runTest {
        val story = PocketRecommendedStory(
            title = "",
            url = "testLink",
            imageUrl = "",
            publisher = "",
            category = "",
            timeToRead = 0,
            timesShown = 123,
        )
        val controller = createController(scope = this)
        assertNull(Pocket.homeRecsStoryClicked.testGetValue())

        controller.handleStoryClicked(
            story,
            storyPosition = Triple(1, 2, 3),
            source = StoriesImpressionSource.HOMEPAGE,
        )

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = story.url,
                newTab = true,
                private = false,
            )
        }

        assertNotNull(Pocket.homeRecsStoryClicked.testGetValue())
        val event = Pocket.homeRecsStoryClicked.testGetValue()!!
        assertEquals(1, event.size)
        assertTrue(event.single().extra!!.containsKey("position"))
        assertEquals("1x2", event.single().extra!!["position"])
        assertTrue(event.single().extra!!.containsKey("times_shown"))
        assertEquals(story.timesShown.inc().toString(), event.single().extra!!["times_shown"])
        assertTrue(event.single().extra!!.containsKey("source"))
        assertEquals("homepage", event.single().extra!!["source"])
    }

    @Test
    fun `WHEN a recommended story is clicked on the stories screen THEN open that story's url using HomeActivity and record telemetry`() = runTest {
        val story = PocketRecommendedStory(
            title = "",
            url = "testLink",
            imageUrl = "",
            publisher = "",
            category = "",
            timeToRead = 0,
            timesShown = 123,
        )
        val controller = createController(scope = this)
        assertNull(Pocket.homeRecsStoryClicked.testGetValue())

        controller.handleStoryClicked(
            story,
            storyPosition = Triple(1, 2, 3),
            source = StoriesImpressionSource.STORIES_SCREEN,
        )

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = story.url,
                newTab = true,
                private = false,
            )
        }

        assertNotNull(Pocket.homeRecsStoryClicked.testGetValue())
        val event = Pocket.homeRecsStoryClicked.testGetValue()!!
        assertEquals(1, event.size)
        assertTrue(event.single().extra!!.containsKey("position"))
        assertEquals("1x2", event.single().extra!!["position"])
        assertTrue(event.single().extra!!.containsKey("times_shown"))
        assertEquals(story.timesShown.inc().toString(), event.single().extra!!["times_shown"])
        assertTrue(event.single().extra!!.containsKey("source"))
        assertEquals("stories_screen", event.single().extra!!["source"])
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN a recommended story is clicked THEN open that story's url using HomeActivity and record telemetry`() = runTest {
        every { settings.enableHomepageAsNewTab } returns true

        val story = PocketRecommendedStory(
            title = "",
            url = "testLink",
            imageUrl = "",
            publisher = "",
            category = "",
            timeToRead = 0,
            timesShown = 123,
        )
        val controller = createController(scope = this)
        assertNull(Pocket.homeRecsStoryClicked.testGetValue())

        controller.handleStoryClicked(
            story,
            storyPosition = Triple(1, 2, 3),
            source = StoriesImpressionSource.HOMEPAGE,
        )

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = story.url,
                newTab = false,
                private = false,
            )
        }

        assertNotNull(Pocket.homeRecsStoryClicked.testGetValue())
        val event = Pocket.homeRecsStoryClicked.testGetValue()!!
        assertEquals(1, event.size)
        assertTrue(event.single().extra!!.containsKey("position"))
        assertEquals("1x2", event.single().extra!!["position"])
        assertTrue(event.single().extra!!.containsKey("times_shown"))
        assertEquals(story.timesShown.inc().toString(), event.single().extra!!["times_shown"])
        assertTrue(event.single().extra!!.containsKey("source"))
        assertEquals("homepage", event.single().extra!!["source"])
    }

    @Test
    fun `WHEN a sponsored content is clicked THEN navigate to the sponsored content URL and record the interaction`() = runTest {
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
            caps = contentCapsForClickedTest,
            priority = 3,
        )
        val controller = createController(scope = this)

        assertNull(Pocket.homeRecsSpocClicked.testGetValue())

        controller.handleStoryClicked(
            sponsoredContent,
            storyPosition = Triple(2, 3, 4),
            source = StoriesImpressionSource.HOMEPAGE,
        )
        testScheduler.advanceUntilIdle()

        assertEquals(1, Pocket.homeRecsSpocClicked.testGetValue()!!.size)
        val data = Pocket.homeRecsSpocClicked.testGetValue()!!.single().extra
        assertEquals(2, data?.size)
        assertEquals("2x3", data?.entries?.first { it.key == "position" }?.value)
        assertEquals("3", data?.entries?.first { it.key == "times_shown" }?.value)

        coVerify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = sponsoredContent.url,
                newTab = true,
                private = false,
            )
            marsUseCases.recordInteraction(sponsoredContent.callbacks.clickUrl)
        }
    }

    @Test
    fun `WHEN a story is clicked THEN its link is opened`() = runTest {
        val story = PocketRecommendedStory("", "url", "", "", "", 0, 0)
        val controller = createController(scope = this)

        controller.handleStoryClicked(
            story,
            storyPosition = Triple(1, 2, 3),
            source = StoriesImpressionSource.HOMEPAGE,
        )

        verifyOrder {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = story.url,
                newTab = true,
                private = false,
            )
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN a story is clicked THEN its link is opened`() = runTest {
        every { settings.enableHomepageAsNewTab } returns true

        val story = PocketRecommendedStory("", "url", "", "", "", 0, 0)
        val controller = createController(scope = this)

        controller.handleStoryClicked(
            story,
            storyPosition = Triple(1, 2, 3),
            source = StoriesImpressionSource.HOMEPAGE,
        )

        verifyOrder {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = story.url,
                newTab = false,
                private = false,
            )
        }
    }

    @Test
    fun `GIVEN the user is on the home screen WHEN a story is clicked THEN its link is opened with a home screen UTM marker`() = runTest {
        every { navController.currentDestination?.id } returns R.id.homeFragment
        val originalURL = "https://story.test"
        val story = PocketRecommendedStory("", originalURL, "", "", "", 0, 0)
        val controller = createController(scope = this)

        controller.handleStoryClicked(
            story,
            storyPosition = Triple(1, 2, 3),
            source = StoriesImpressionSource.HOMEPAGE,
        )

        verifyOrder {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = originalURL.markAsOpenedFromHomeScreen(),
                newTab = true,
                private = false,
            )
        }
    }

    @Test
    fun `GIVEN the user is on the stories screen WHEN a story is clicked THEN its link is opened with a stories screen UTM marker`() = runTest {
        every { navController.currentDestination?.id } returns R.id.storiesFragment
        val originalURL = "https://story.test"
        val story = PocketRecommendedStory("", originalURL, "", "", "", 0, 0)
        val controller = createController(scope = this)

        controller.handleStoryClicked(
            story,
            storyPosition = Triple(1, 2, 3),
            source = StoriesImpressionSource.HOMEPAGE,
        )

        verifyOrder {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = originalURL.markAsOpenedFromStoriesScreen(),
                newTab = true,
                private = false,
            )
        }
    }

    @Test
    fun `WHEN the discover more button is clicked THEN navigate to the discover more stories screen`() = runTest {
        val controller = createController(scope = this)

        controller.handleDiscoverMoreClicked()

        verify {
            navController.nav(
                R.id.homeFragment,
                HomeFragmentDirections.actionHomeFragmentToStoriesFragment(),
            )
        }
    }

    @Test
    fun `WHEN screen is shown THEN impression is logged`() = runTest {
        assertNull(StoriesLibrary.viewed.testGetValue())
        val controller = createController(scope = this)
        controller.handleDiscoverMoreScreenViewed()
        assertNotNull(StoriesLibrary.viewed.testGetValue())
    }

    private fun createController(
        scope: CoroutineScope,
        appStore: AppStore = AppStore(),
    ) = DefaultPocketStoriesController(
        navControllerRef = WeakReference(navController),
        appStore = appStore,
        settings = settings,
        fenixBrowserUseCases = fenixBrowserUseCases,
        marsUseCases = marsUseCases,
        viewLifecycleScope = scope,
    )
}
