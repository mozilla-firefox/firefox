/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import androidx.datastore.core.DataStore
import io.mockk.coVerify
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flowOf
import mozilla.components.service.pocket.PocketStoriesService
import mozilla.components.service.pocket.PocketStory.ContentRecommendation
import mozilla.components.service.pocket.PocketStory.PocketRecommendedStory
import mozilla.components.service.pocket.PocketStory.SponsoredContent
import mozilla.components.service.pocket.PocketStory.SponsoredContentCallbacks
import mozilla.components.support.test.ext.joinBlocking
import mozilla.components.support.test.libstate.ext.waitUntilIdle
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.rule.MainCoroutineRule
import mozilla.components.support.test.rule.runTestOnMain
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.ContentRecommendationsAction
import org.mozilla.fenix.components.appstate.AppAction.ContentRecommendationsAction.PocketStoriesCategoriesSelectionsChange
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.recommendations.ContentRecommendationsState
import org.mozilla.fenix.datastore.SelectedPocketStoriesCategories
import org.mozilla.fenix.datastore.SelectedPocketStoriesCategories.SelectedPocketStoriesCategory
import org.mozilla.fenix.home.pocket.PocketImpression
import org.mozilla.fenix.home.pocket.PocketRecommendedStoriesCategory
import org.mozilla.fenix.home.pocket.PocketRecommendedStoriesSelectedCategory

class PocketUpdatesMiddlewareTest {
    @get:Rule
    val mainCoroutineTestRule = MainCoroutineRule()

    @Test
    fun `WHEN PocketStoriesShown is dispatched THEN update PocketStoriesService`() = runTestOnMain {
        val story1 = PocketRecommendedStory(
            "title",
            "url1",
            "imageUrl",
            "publisher",
            "category",
            0,
            timesShown = 0,
        )
        val story2 = story1.copy("title2", "url2")
        val story3 = story1.copy("title3", "url3")
        val pocketService: PocketStoriesService = mockk(relaxed = true)
        val pocketMiddleware = PocketUpdatesMiddleware(lazy { pocketService }, mockk(), this)
        val appstore = AppStore(
            AppState(
                recommendationState = ContentRecommendationsState(
                    pocketStories = listOf(story1, story2, story3),
                ),
            ),
            listOf(pocketMiddleware),
        )

        appstore.dispatch(
            ContentRecommendationsAction.PocketStoriesShown(
                impressions = listOf(
                    PocketImpression(
                        story = story2,
                        position = 1,
                    ),
                ),
            ),
        ).joinBlocking()

        coVerify { pocketService.updateStoriesTimesShown(listOf(story2.copy(timesShown = 1))) }
    }

    @Test
    fun `WHEN needing to persist impressions is called THEN update PocketStoriesService`() = runTestOnMain {
        val story = PocketRecommendedStory(
            "title",
            "url1",
            "imageUrl",
            "publisher",
            "category",
            0,
            timesShown = 3,
        )
        val recommendation = ContentRecommendation(
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
            caps = mockk(relaxed = true),
            priority = 3,
        )
        val stories = listOf(story, recommendation, sponsoredContent)
        val expectedStoryUpdate = story.copy(timesShown = story.timesShown.inc())
        val expectedRecommendationUpdate = recommendation.copy(impressions = recommendation.impressions.inc())

        val pocketService: PocketStoriesService = mockk(relaxed = true)

        persistStoriesImpressions(
            coroutineScope = this,
            pocketStoriesService = pocketService,
            updatedStories = stories,
        )

        coVerify {
            pocketService.updateStoriesTimesShown(listOf(expectedStoryUpdate))
            pocketService.updateRecommendationsImpressions(listOf(expectedRecommendationUpdate))
            pocketService.recordSponsoredContentImpressions(impressions = listOf(sponsoredContent.url))
        }
    }

    @Test
    fun `WHEN PocketStoriesCategoriesChange is dispatched THEN intercept and dispatch PocketStoriesCategoriesSelectionsChange`() = runTestOnMain {
        val dataStore = FakeDataStore()
        val currentCategories = listOf(mockk<PocketRecommendedStoriesCategory>())
        val pocketMiddleware = PocketUpdatesMiddleware(mockk(), dataStore, this)
        val appStore = spyk(
            AppStore(
                AppState(
                    recommendationState = ContentRecommendationsState(
                        pocketStoriesCategories = currentCategories,
                    ),
                ),
                listOf(pocketMiddleware),
            ),
        )

        appStore.dispatch(ContentRecommendationsAction.PocketStoriesCategoriesChange(currentCategories)).joinBlocking()

        verify {
            appStore.dispatch(
                PocketStoriesCategoriesSelectionsChange(
                    storiesCategories = currentCategories,
                    categoriesSelected = listOf(),
                ),
            )
        }
    }

    @Test
    fun `WHEN SelectPocketStoriesCategory is dispatched THEN persist details in DataStore and in memory`() = runTestOnMain {
        val categ1 = PocketRecommendedStoriesCategory("categ1")
        val categ2 = PocketRecommendedStoriesCategory("categ2")
        val dataStore = FakeDataStore()
        val pocketMiddleware = PocketUpdatesMiddleware(mockk(), dataStore, this)
        val appStore = spyk(
            AppStore(
                AppState(
                    recommendationState = ContentRecommendationsState(
                        pocketStoriesCategories = listOf(categ1, categ2),
                    ),
                ),
                listOf(pocketMiddleware),
            ),
        )
        dataStore.assertSelectedCategories()
        appStore.assertSelectedCategories()

        appStore.dispatch(ContentRecommendationsAction.SelectPocketStoriesCategory(categ2.name)).joinBlocking()
        dataStore.assertSelectedCategories(categ2.name)
        appStore.assertSelectedCategories(categ2.name)

        appStore.dispatch(ContentRecommendationsAction.SelectPocketStoriesCategory(categ1.name)).joinBlocking()
        dataStore.assertSelectedCategories(categ2.name, categ1.name)
        appStore.assertSelectedCategories(categ2.name, categ1.name)
    }

    @Test
    fun `WHEN DeselectPocketStoriesCategory is dispatched THEN persist details in DataStore and in memory`() = runTestOnMain {
        val categ1 = PocketRecommendedStoriesCategory("categ1")
        val categ2 = PocketRecommendedStoriesCategory("categ2")
        val persistedCateg1 = PocketRecommendedStoriesSelectedCategory("categ1")
        val persistedCateg2 = PocketRecommendedStoriesSelectedCategory("categ2")
        val dataStore = FakeDataStore(persistedCateg1.name, persistedCateg2.name)
        val pocketMiddleware = PocketUpdatesMiddleware(mockk(), dataStore, this)
        val appStore = spyk(
            AppStore(
                AppState(
                    recommendationState = ContentRecommendationsState(
                        pocketStoriesCategories = listOf(categ1, categ2),
                        pocketStoriesCategoriesSelections = listOf(persistedCateg1, persistedCateg2),
                    ),
                ),
                listOf(pocketMiddleware),
            ),
        )
        dataStore.assertSelectedCategories(persistedCateg1.name, persistedCateg2.name)
        appStore.assertSelectedCategories(persistedCateg1.name, persistedCateg2.name)

        appStore.dispatch(ContentRecommendationsAction.DeselectPocketStoriesCategory(categ2.name)).joinBlocking()
        dataStore.assertSelectedCategories(persistedCateg1.name)
        appStore.assertSelectedCategories(persistedCateg1.name)

        appStore.dispatch(ContentRecommendationsAction.DeselectPocketStoriesCategory(categ1.name)).joinBlocking()
        dataStore.assertSelectedCategories()
        appStore.assertSelectedCategories()
    }

    @Test
    fun `WHEN persistCategories is called THEN update dataStore`() = runTestOnMain {
        val newCategoriesSelection = listOf(PocketRecommendedStoriesSelectedCategory("categ1"))
        val dataStore = FakeDataStore()
        dataStore.assertSelectedCategories()

        persistSelectedCategories(this, newCategoriesSelection, dataStore)

        dataStore.assertSelectedCategories(newCategoriesSelection[0].name)
    }

    @Test
    fun `WHEN restoreSelectedCategories is called THEN dispatch PocketStoriesCategoriesSelectionsChange with data read from the persistence layer`() = runTestOnMain {
        val dataStore = FakeDataStore("testCategory")
        val currentCategories = listOf(mockk<PocketRecommendedStoriesCategory>())
        val captorMiddleware = CaptureActionsMiddleware<AppState, AppAction>()
        val appStore = AppStore(
            initialState = AppState(),
            middlewares = listOf(captorMiddleware),
        )

        restoreSelectedCategories(
            coroutineScope = this,
            currentCategories = currentCategories,
            store = appStore,
            selectedPocketCategoriesDataStore = dataStore,
        )
        appStore.waitUntilIdle()

        captorMiddleware.assertLastAction(PocketStoriesCategoriesSelectionsChange::class) {
            assertEquals(1, it.categoriesSelected.size)
            assertEquals("testCategory", it.categoriesSelected[0].name)
        }
    }

    /**
     * Assert that the Pocket categories with [expected] names are currently selected
     * and that this selection happened in the past 10 seconds.
     */
    private fun FakeDataStore.assertSelectedCategories(vararg expected: String) {
        val now = System.currentTimeMillis()
        val actualSelections = currentCategorySelection.valuesList
        assertEquals(expected.size, actualSelections.size)
        actualSelections.forEachIndexed { index, selection ->
            assertEquals(expected[index], selection.name)
            assertTrue(selection.selectionTimestamp in now - 10000..now)
        }
    }

    /**
     * Assert that the Pocket categories with [expected] names are currently selected
     * and that this selection happened in the past 10 seconds.
     */
    private fun AppStore.assertSelectedCategories(vararg expected: String) {
        val now = System.currentTimeMillis()
        val actualSelections = state.recommendationState.pocketStoriesCategoriesSelections
        assertEquals(expected.size, actualSelections.size)
        actualSelections.forEachIndexed { index, selection ->
            assertEquals(expected[index], selection.name)
            assertTrue(selection.selectionTimestamp in now - 10000..now)
        }
    }
}

/**
 * Incomplete fake of a [DataStore].
 * Respects the [DataStore] contract with basic method implementations but needs to have mocked behavior
 * for more complex interactions.
 * Can be used as a replacement for mocks of the [DataStore] interface which might fail intermittently.
 */
class FakeDataStore(
    vararg initialSelectedCategories: String,
) : DataStore<SelectedPocketStoriesCategories> {
    val initialSelection: List<SelectedPocketStoriesCategory> = initialSelectedCategories.map {
        SelectedPocketStoriesCategory.newBuilder().apply {
            name = it
            setSelectionTimestamp(System.currentTimeMillis())
        }.build()
    }

    private val persistedSelectedCategories = SelectedPocketStoriesCategories.newBuilder().apply {
        initialSelection.forEach { addValues(it) }
    }.build()

    var currentCategorySelection: SelectedPocketStoriesCategories = persistedSelectedCategories
        private set

    override val data: Flow<SelectedPocketStoriesCategories>
        get() = flowOf(persistedSelectedCategories)

    override suspend fun updateData(
        transform: suspend (t: SelectedPocketStoriesCategories) -> SelectedPocketStoriesCategories,
    ): SelectedPocketStoriesCategories {
        return transform(persistedSelectedCategories).apply {
            currentCategorySelection = this
        }
    }
}
