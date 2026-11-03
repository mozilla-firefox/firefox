/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.SearchAction
import mozilla.components.browser.state.search.RegionState
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SearchState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.top.sites.presenter.DefaultTopSitesPresenter
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.AMAZON_SEARCH_ENGINE_NAME

class TopSitesBindingTest {
    private val testDispatcher = StandardTestDispatcher()

    private lateinit var browserStore: BrowserStore
    private lateinit var presenter: DefaultTopSitesPresenter

    @Before
    fun setUp() {
        presenter = mockk(relaxed = true)
        browserStore =
            BrowserStore(
                initialState =
                    BrowserState(
                        search =
                            SearchState(
                                regionSearchEngines =
                                    listOf(
                                        SearchEngine(
                                            id = "google",
                                            name = "Google",
                                            icon = mockk(),
                                            type = SearchEngine.Type.BUNDLED,
                                        ),
                                        SearchEngine(
                                            id = "duckduckgo",
                                            name = "DuckDuckGo",
                                            icon = mockk(),
                                            type = SearchEngine.Type.BUNDLED,
                                        ),
                                        SearchEngine(
                                            id = AMAZON_SEARCH_ENGINE_NAME,
                                            name = AMAZON_SEARCH_ENGINE_NAME,
                                            icon = mockk(),
                                            type = SearchEngine.Type.BUNDLED,
                                        ),
                                    )
                            )
                    )
            )
    }

    @Test
    fun `WHEN binding is started and stopped THEN presenter is started and stopped`() {
        val binding = createBinding()
        binding.start()

        verify { presenter.start() }

        binding.stop()

        verify { presenter.stop() }
    }

    @Test
    fun `WHEN Amazon search engine is selected THEN presenter storage is updated`() =
        runTest(testDispatcher) {
            val binding = createBinding()
            binding.start()

            browserStore.dispatch(
                SearchAction.SelectSearchEngineAction(
                    searchEngineId = AMAZON_SEARCH_ENGINE_NAME,
                    searchEngineName = AMAZON_SEARCH_ENGINE_NAME,
                )
            )
            testDispatcher.scheduler.advanceUntilIdle()

            verify { presenter.onStorageUpdated() }
        }

    @Test
    fun `WHEN the region changes THEN presenter storage is updated`() =
        runTest(testDispatcher) {
            val binding = createBinding()
            binding.start()
            testDispatcher.scheduler.advanceUntilIdle()

            browserStore.dispatch(
                SearchAction.SetRegionAction(
                    regionState = RegionState(home = "JP", current = "JP"),
                    distribution = null,
                )
            )
            testDispatcher.scheduler.advanceUntilIdle()

            verify(exactly = 2) { presenter.onStorageUpdated() }
        }

    private fun createBinding() =
        TopSitesBinding(
            browserStore = browserStore,
            presenter = presenter,
            mainDispatcher = testDispatcher,
        )
}
