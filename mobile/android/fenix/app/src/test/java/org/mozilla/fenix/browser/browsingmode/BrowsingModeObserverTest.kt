/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.browsingmode

import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabs.TabsUseCases
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class BrowsingModeObserverTest {
    private val testDispatcher = StandardTestDispatcher()
    private lateinit var store: BrowserStore
    private val selectedTabs = mutableListOf<TabSessionState>()

    @Before
    fun setup() {
        store = BrowserStore()
        selectedTabs.clear()
    }

    @Test
    fun `WHEN the selected tab changes and it is not a custom tab THEN onSelectedTabChanged is invoked`() =
        runTest(testDispatcher) {
            val tab = createTab(url = "https://mozilla.org")
            createBinding(customTabSessionId = null).start()
            testDispatcher.scheduler.advanceUntilIdle()
            assertTrue(selectedTabs.isEmpty())

            store.dispatch(TabListAction.AddTabAction(tab, true))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(listOf(tab), selectedTabs)
        }

    @Test
    fun `WHEN the selected tab changes and it is a custom tab THEN onSelectedTabChanged is not invoked`() =
        runTest(testDispatcher) {
            val tab = createTab(url = "https://mozilla.org")
            createBinding(customTabSessionId = "1").start()
            testDispatcher.scheduler.advanceUntilIdle()

            store.dispatch(TabListAction.AddTabAction(tab, true))
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(selectedTabs.isEmpty())
        }

    @Test
    fun `GIVEN a non custom tab is selected WHEN the state changes without changing the selected tab THEN onSelectedTabChanged is not invoked again`() =
        runTest(testDispatcher) {
            createBinding(customTabSessionId = null).start()
            store.dispatch(TabListAction.AddTabAction(createTab(url = "https://mozilla.org"), select = true))
            testDispatcher.scheduler.advanceUntilIdle()

            store.dispatch(TabListAction.AddTabAction(createTab("https://firefox.com"), select = false))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(1, selectedTabs.size)
        }

    @Test
    fun `WHEN a private tab is selected and migrated to a normal tab THEN onSelectedTabChanged is invoked with the normal tab`() =
        runTest(testDispatcher) {
            val privateTab = createTab(url = "https://mozilla.org", private = true)
            store.dispatch(TabListAction.AddTabAction(privateTab, true))
            createBinding(customTabSessionId = null).start()
            testDispatcher.scheduler.advanceUntilIdle()
            selectedTabs.clear()

            val normalTabId = TabsUseCases.MigratePrivateTabUseCase(store)(privateTab.id)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(listOf(normalTabId), selectedTabs.map { it.id })
        }

    @Test
    fun `GIVEN the selected tab changed before the binding is started WHEN the binding is started THEN onSelectedTabChanged is invoked with the current selected tab`() =
        runTest(testDispatcher) {
            val tab1 = createTab(url = "https://mozilla.org")
            val tab2 = createTab("https://firefox.com")
            store.dispatch(TabListAction.AddTabAction(tab1, select = true))
            store.dispatch(TabListAction.AddTabAction(tab2, select = true))

            createBinding().start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(listOf(tab2), selectedTabs)
        }

    @Test
    fun `WHEN the observer is stopped THEN onSelectedTabChanged is no longer invoked`() =
        runTest(testDispatcher) {
            val binding = createBinding()
            binding.start()
            testDispatcher.scheduler.advanceUntilIdle()
            store.dispatch(TabListAction.AddTabAction(createTab(url = "https://mozilla.org"), select = true))
            testDispatcher.scheduler.advanceUntilIdle()
            assertEquals(1, selectedTabs.size)

            binding.stop()
            store.dispatch(TabListAction.AddTabAction(createTab(url = "https://example.com"), select = true))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(1, selectedTabs.size)
        }

    private fun createBinding(customTabSessionId: String? = null) =
        BrowsingModeObserver(
            store = store,
            customTabSessionId = customTabSessionId,
            onSelectedTabChanged = { selectedTabs.add(it) },
            mainDispatcher = testDispatcher,
        )
}
