/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.settingssearch

import androidx.fragment.app.Fragment
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.navigation.NavController
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import kotlin.time.Duration.Companion.milliseconds
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class SettingsSearchMiddlewareTest {

    private val testDispatcher = StandardTestDispatcher()
    private val testScope = TestScope(testDispatcher)
    private val navController: NavController = mockk(relaxed = true)
    private lateinit var lifecycleOwner: FakeLifecycleOwner
    private lateinit var fragment: Fragment
    private val recentSearchesRepository: FenixRecentSettingsSearchesRepository = mockk(relaxed = true)
    private val recentSearchesFlow = MutableStateFlow<List<SettingsSearchItem>>(emptyList())

    @Before
    fun setUp() =
        runTest(testDispatcher) {
            every { recentSearchesRepository.recentSearches } returns recentSearchesFlow
            lifecycleOwner = FakeLifecycleOwner(Lifecycle.State.RESUMED)
            fragment =
                spyk(Fragment()).apply {
                    every { context } returns testContext
                }
            every { fragment.viewLifecycleOwner } returns lifecycleOwner
        }

    private fun buildMiddleware(
        fenixSettingsIndexer: SettingsIndexer = TestSettingsIndexer(),
        navController: NavController = this.navController,
        recentSettingsSearchesRepository: RecentSettingsSearchesRepository = this.recentSearchesRepository,
    ) =
        SettingsSearchMiddleware(
            fenixSettingsIndexer = fenixSettingsIndexer,
            navController = navController,
            recentSettingsSearchesRepository = recentSettingsSearchesRepository,
            scope = testScope,
            dispatcher = testDispatcher,
        )

    @Test
    fun `WHEN the settings search query is updated and results are not found THEN the state is updated`() =
        runTest(testDispatcher) {
            val middleware = buildMiddleware(EmptyTestSettingsIndexer())
            val query = "longSample"
            val store = SettingsSearchStore(middleware = listOf(middleware))
            store.dispatch(SettingsSearchAction.SearchQueryUpdated(query))
            testDispatcher.scheduler.advanceUntilIdle()

            assert(store.state is SettingsSearchState.NoSearchResults)
            assert(store.state.searchQuery == query)
            assert(store.state.searchResults.isEmpty())
        }

    @Test
    fun `WHEN the settings search query is updated and results are found THEN the state is updated`() =
        runTest(testDispatcher) {
            val middleware = buildMiddleware()
            val query = "a"
            val store = SettingsSearchStore(middleware = listOf(middleware))
            store.dispatch(SettingsSearchAction.Init)
            testDispatcher.scheduler.advanceUntilIdle()

            store.dispatch(SettingsSearchAction.SearchQueryUpdated(query))
            testDispatcher.scheduler.advanceUntilIdle()

            assert(store.state is SettingsSearchState.SearchInProgress)
            assert(store.state.searchQuery == query)
        }

    @Test
    fun `WHEN a result item is clicked THEN it should be added to the recent searches repository`() =
        runTest(testDispatcher) {
            val middleware = buildMiddleware()
            val store = SettingsSearchStore(middleware = listOf(middleware))
            val testItem = testList.first()

            store.dispatch(SettingsSearchAction.Init)
            testDispatcher.scheduler.advanceUntilIdle()

            store.dispatch(SettingsSearchAction.ResultItemClicked(testItem))
            testDispatcher.scheduler.advanceUntilIdle()

            coVerify { recentSearchesRepository.addRecentSearchItem(testItem) }
            verify { navController.navigate(testItem.preferenceFileInformation.fragmentId, any()) }
        }

    @Test
    fun `GIVEN a recent search is no longer indexed WHEN the screen is initialized THEN it is not shown in recent searches`() =
        runTest(testDispatcher) {
            val hiddenItem = testList.first().copy(preferenceKey = "hidden_pref")
            recentSearchesFlow.value = listOf(hiddenItem, testList.first())
            val store = SettingsSearchStore(middleware = listOf(buildMiddleware()))

            store.dispatch(SettingsSearchAction.Init)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(listOf(testList.first()), store.state.recentSearches)
        }

    @Test
    fun `GIVEN a recent search points to an outdated screen WHEN the screen is initialized THEN the indexed item is shown instead`() =
        runTest(testDispatcher) {
            val indexedItem = testList.first()
            val staleItem =
                indexedItem.copy(
                    title = "Old title",
                    categoryHeader = "",
                    preferenceFileInformation = PreferenceFileInformation.GeneralPreferences,
                )
            recentSearchesFlow.value = listOf(staleItem)
            val store = SettingsSearchStore(middleware = listOf(buildMiddleware()))

            store.dispatch(SettingsSearchAction.Init)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(listOf(indexedItem), store.state.recentSearches)
        }

    @Test
    fun `WHEN ClearRecentSearchesClicked is dispatched THEN store state is updated correctly`() =
        runTest(testDispatcher) {
            val middleware = buildMiddleware()
            val store = SettingsSearchStore(middleware = listOf(middleware))

            store.dispatch(SettingsSearchAction.ClearRecentSearchesClicked)
            testDispatcher.scheduler.advanceUntilIdle()

            assert(store.state.recentSearches.isEmpty())
        }

    @Test
    fun `WHEN multiple search queries are dispatched rapidly THEN previous search job is cancelled`() =
        runTest(testDispatcher) {
            val indexer = SlowTestSettingsIndexer(delayMs = 1000L)
            val middleware = buildMiddleware(fenixSettingsIndexer = indexer)
            val store = SettingsSearchStore(middleware = listOf(middleware))

            store.dispatch(SettingsSearchAction.SearchQueryUpdated("query1"))
            testDispatcher.scheduler.advanceTimeBy(100.milliseconds)
            store.dispatch(SettingsSearchAction.SearchQueryUpdated("query2"))
            testDispatcher.scheduler.advanceUntilIdle()

            assert(store.state.searchQuery == "query2")
            assert(store.state is SettingsSearchState.SearchInProgress)
            assert(store.state.searchResults.isNotEmpty())
        }

    @Test
    fun `WHEN a search query completes without interruption THEN results are properly dispatched`() =
        runTest(testDispatcher) {
            val middleware = buildMiddleware()
            val store = SettingsSearchStore(middleware = listOf(middleware))

            store.dispatch(SettingsSearchAction.SearchQueryUpdated("a"))
            testDispatcher.scheduler.advanceUntilIdle()

            assert(store.state is SettingsSearchState.SearchInProgress)
            assert(store.state.searchQuery == "a")
            assert(store.state.searchResults.isNotEmpty())
            assert(store.state.searchResults == testList)
        }

    @Test
    fun `WHEN a slow search is in progress and a new query arrives THEN old search is cancelled and does not dispatch results`() =
        runTest(testDispatcher) {
            val indexer = DelayedTestSettingsIndexer(delayMs = 500L)
            val middleware = buildMiddleware(fenixSettingsIndexer = indexer)
            val store = SettingsSearchStore(middleware = listOf(middleware))

            store.dispatch(SettingsSearchAction.SearchQueryUpdated("slow"))
            testDispatcher.scheduler.advanceTimeBy(200.milliseconds)
            store.dispatch(SettingsSearchAction.SearchQueryUpdated("fast"))
            testDispatcher.scheduler.advanceUntilIdle()

            assert(store.state.searchQuery == "fast")
            assert(indexer.queryCalls.size == 2)
            assert(indexer.queryCalls[0] == "slow")
            assert(indexer.queryCalls[1] == "fast")
        }

    @Test
    fun `WHEN an empty query is dispatched while search is in progress THEN search is cancelled and state returns to default`() =
        runTest(testDispatcher) {
            val indexer = SlowTestSettingsIndexer(delayMs = 1000L)
            val middleware = buildMiddleware(fenixSettingsIndexer = indexer)
            val store = SettingsSearchStore(middleware = listOf(middleware))

            store.dispatch(SettingsSearchAction.SearchQueryUpdated("query"))
            testDispatcher.scheduler.advanceTimeBy(100.milliseconds)
            store.dispatch(SettingsSearchAction.SearchQueryUpdated(""))
            testDispatcher.scheduler.advanceUntilIdle()

            assert(store.state is SettingsSearchState.Default)
            assert(store.state.searchQuery.isEmpty())
            assert(store.state.searchResults.isEmpty())
        }

    @Test
    fun `WHEN multiple queries are dispatched with varying completion times THEN only the most recent query results are displayed`() =
        runTest(testDispatcher) {
            val indexer = VariableDelayTestSettingsIndexer()
            val middleware = buildMiddleware(fenixSettingsIndexer = indexer)
            val store = SettingsSearchStore(middleware = listOf(middleware))

            store.dispatch(SettingsSearchAction.SearchQueryUpdated("query1"))
            testDispatcher.scheduler.advanceTimeBy(50.milliseconds)
            store.dispatch(SettingsSearchAction.SearchQueryUpdated("query2"))
            testDispatcher.scheduler.advanceTimeBy(50.milliseconds)
            store.dispatch(SettingsSearchAction.SearchQueryUpdated("query3"))
            testDispatcher.scheduler.advanceUntilIdle()

            assert(store.state.searchQuery == "query3")
            assert(store.state is SettingsSearchState.SearchInProgress)
            assert(store.state.searchResults == indexer.results["query3"])
        }

    @Test
    fun `GIVEN Init action THEN indexAllSettings is called`() = runTest {
        val indexer = spyk(TestSettingsIndexer())
        val middleware = buildMiddleware(fenixSettingsIndexer = indexer)
        val store = SettingsSearchStore(middleware = listOf(middleware))

        store.dispatch(SettingsSearchAction.Init)
        testDispatcher.scheduler.advanceUntilIdle()

        coVerify { indexer.indexAllSettings() }
    }

    @After
    fun tearDown() = runTest {
        lifecycleOwner.destroy()
    }
}

val testList =
    listOf(
        SettingsSearchItem(
            title = "Search Engine",
            summary = "Set your preferred search engine for browsing.",
            preferenceKey = "search_engine_main",
            categoryHeader = "General",
            preferenceFileInformation = PreferenceFileInformation.SearchSettingsPreferences,
        ),
        SettingsSearchItem(
            title = "Advanced Settings",
            summary = "", // Empty or blank summary
            preferenceKey = "advanced_stuff",
            categoryHeader = "Advanced",
            preferenceFileInformation = PreferenceFileInformation.GeneralPreferences,
        ),
        SettingsSearchItem(
            title = "Do not collect usage data",
            summary = "", // Empty or blank summary
            preferenceKey = "do_not_collect_data",
            categoryHeader = "Privacy",
            preferenceFileInformation = PreferenceFileInformation.GeneralPreferences,
        ),
    )

class TestSettingsIndexer : SettingsIndexer {

    override suspend fun indexAllSettings() {
        // no op
    }

    override suspend fun getSettingsWithQuery(query: String): List<SettingsSearchItem> {
        return testList
    }

    override fun getIndexedItem(preferenceKey: String) = testList.firstOrNull { it.preferenceKey == preferenceKey }
}

class EmptyTestSettingsIndexer : SettingsIndexer {
    override suspend fun indexAllSettings() {
        // no op
    }

    override suspend fun getSettingsWithQuery(query: String): List<SettingsSearchItem> {
        return emptyList()
    }

    override fun getIndexedItem(preferenceKey: String): SettingsSearchItem? = null
}

class SlowTestSettingsIndexer(
    private val delayMs: Long = 1000L,
    private val results: List<SettingsSearchItem> = testList,
) : SettingsIndexer {
    override suspend fun indexAllSettings() {
        // no op
    }

    override suspend fun getSettingsWithQuery(query: String): List<SettingsSearchItem> {
        delay(delayMs)
        return if (query.isBlank()) emptyList() else results
    }

    override fun getIndexedItem(preferenceKey: String) = testList.firstOrNull { it.preferenceKey == preferenceKey }
}

class DelayedTestSettingsIndexer(private val delayMs: Long = 500L) : SettingsIndexer {
    val queryCalls = mutableListOf<String>()

    override suspend fun indexAllSettings() {
        // no op
    }

    override suspend fun getSettingsWithQuery(query: String): List<SettingsSearchItem> {
        queryCalls.add(query)
        delay(delayMs)
        return testList.filter { it.title.contains(query, ignoreCase = true) }
    }

    override fun getIndexedItem(preferenceKey: String) = testList.firstOrNull { it.preferenceKey == preferenceKey }
}

class VariableDelayTestSettingsIndexer : SettingsIndexer {
    val results =
        mapOf(
            "query1" to listOf(testList[0]),
            "query2" to listOf(testList[1]),
            "query3" to listOf(testList[2]),
        )

    private val delays =
        mapOf(
            "query1" to 300L,
            "query2" to 100L,
            "query3" to 200L,
        )

    override suspend fun indexAllSettings() {
        // no op
    }

    override suspend fun getSettingsWithQuery(query: String): List<SettingsSearchItem> {
        delay(delays[query] ?: 100L)
        return results[query] ?: emptyList()
    }

    override fun getIndexedItem(preferenceKey: String) = testList.firstOrNull { it.preferenceKey == preferenceKey }
}

private class FakeLifecycleOwner(initialState: Lifecycle.State) : LifecycleOwner {
    private val registry = LifecycleRegistry(this)
    override val lifecycle: Lifecycle = registry

    init {
        registry.currentState = initialState
    }

    fun destroy() {
        registry.currentState = Lifecycle.State.DESTROYED
    }
}
