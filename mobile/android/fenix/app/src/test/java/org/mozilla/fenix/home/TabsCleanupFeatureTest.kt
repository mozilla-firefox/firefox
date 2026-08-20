/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import androidx.compose.material3.SnackbarHostState
import androidx.navigation.NavController
import androidx.navigation.NavDirections
import io.mockk.MockKAnnotations
import io.mockk.Runs
import io.mockk.every
import io.mockk.impl.annotations.RelaxedMockK
import io.mockk.just
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import io.mockk.verifyOrder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.StandardTestDispatcher
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.test.robolectric.testContext
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.tabsClosedUndoMessage
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_ACTIVE_NORMAL_TABS
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_NORMAL_TABS
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_PRIVATE_TABS
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class TabsCleanupFeatureTest {

    private val testDispatcher = StandardTestDispatcher()
    private val testCoroutineScope = CoroutineScope(testDispatcher)

    @RelaxedMockK private lateinit var viewModel: HomeScreenViewModel

    @RelaxedMockK private lateinit var browserStore: BrowserStore

    @RelaxedMockK private lateinit var browsingModeManager: BrowsingModeManager

    @RelaxedMockK private lateinit var navController: NavController

    @RelaxedMockK private lateinit var tabsUseCases: TabsUseCases

    @RelaxedMockK private lateinit var fenixBrowserUseCases: FenixBrowserUseCases

    @RelaxedMockK private lateinit var settings: Settings

    private val snackbarHostState = SnackbarHostState()

    private lateinit var feature: TabsCleanupFeature

    @Before
    fun setup() {
        MockKAnnotations.init(this)

        feature =
            spyk(
                TabsCleanupFeature(
                    context = testContext,
                    viewModel = viewModel,
                    browserStore = browserStore,
                    browsingModeManager = browsingModeManager,
                    navController = navController,
                    settings = settings,
                    tabsUseCases = tabsUseCases,
                    fenixBrowserUseCases = fenixBrowserUseCases,
                    snackbarHostState = snackbarHostState,
                    viewLifecycleScope = testCoroutineScope,
                )
            )

        every { feature.showUndoSnackbar(any(), any()) } just Runs
    }

    @Test
    fun `GIVEN all active normal tabs to delete WHEN feature is started THEN remove all active normal tabs and show undo snackbar`() {
        val tabs =
            listOf(
                createTab(
                    id = "1",
                    url = "https://mozilla.org/1",
                    private = false,
                    lastAccess = System.currentTimeMillis(),
                    createdAt = System.currentTimeMillis(),
                ),
                createTab(
                    id = "2",
                    url = "https://mozilla.org/2",
                    private = false,
                    lastAccess = System.currentTimeMillis(),
                    createdAt = System.currentTimeMillis(),
                ),
                createTab(
                    id = "3",
                    url = "https://mozilla.org/3",
                    private = false,
                    lastAccess = System.currentTimeMillis(),
                    createdAt = System.currentTimeMillis(),
                ),
                createTab(
                    id = "inactive_tab",
                    url = "https://mozilla.org/4",
                    private = false,
                    lastAccess = 0L,
                    createdAt = 0L,
                ),
            )

        every { settings.inactiveTabsAreEnabled } returns true
        every { browserStore.state } returns BrowserState(tabs = tabs)
        every { viewModel.sessionToDelete } returns ALL_ACTIVE_NORMAL_TABS

        feature.start()

        verify {
            tabsUseCases.removeTabs(listOf("1", "2", "3"), setOf("inactive_tab"))

            feature.showUndoSnackbar(
                testContext.tabsClosedUndoMessage(3),
                any(),
            )

            viewModel.sessionToDelete = null
        }

        verify(exactly = 0) { tabsUseCases.removeNormalTabs() }
    }

    @Test
    fun `GIVEN all normal tabs to delete WHEN feature is started THEN remove all normal tabs and show undo snackbar`() {
        val tabsCount = 3
        val mockTabs =
            List(tabsCount) {
                mockk<TabSessionState> {
                    every { content.private } returns false
                }
            }

        every { browserStore.state } returns BrowserState(tabs = mockTabs)
        every { viewModel.sessionToDelete } returns ALL_NORMAL_TABS

        feature.start()

        verify {
            tabsUseCases.removeNormalTabs()

            feature.showUndoSnackbar(
                testContext.tabsClosedUndoMessage(tabsCount),
                any(),
            )

            viewModel.sessionToDelete = null
        }
    }

    @Test
    fun `GIVEN all private tabs to delete WHEN feature is started THEN remove all private tabs and show undo snackbar`() {
        val tabsCount = 4
        val mockTabs =
            List(tabsCount) {
                mockk<TabSessionState> {
                    every { content.private } returns true
                }
            }

        every { viewModel.sessionToDelete } returns ALL_PRIVATE_TABS
        every { browserStore.state } returns BrowserState(tabs = mockTabs)

        feature.start()

        verify {
            tabsUseCases.removePrivateTabs()

            feature.showUndoSnackbar(
                testContext.tabsClosedUndoMessage(tabsCount),
                any(),
            )

            viewModel.sessionToDelete = null
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled and all normal tabs to delete WHEN feature is started THEN remove all normal tabs and show undo snackbar`() {
        every { settings.enableHomepageAsNewTab } returns true
        every { viewModel.sessionToDelete } returns ALL_NORMAL_TABS

        val tab1 = mockk<TabSessionState> { every { content.private } returns false }
        val tab2 = mockk<TabSessionState> { every { content.private } returns false }
        val mockTabs = listOf(tab1, tab2)

        every { browserStore.state } returns BrowserState(tabs = mockTabs, selectedTabId = null)

        feature.start()

        verifyOrder {
            tabsUseCases.removeNormalTabs()

            fenixBrowserUseCases.addNewHomepageTab(private = browsingModeManager.mode.isPrivate)

            feature.showUndoSnackbar(
                testContext.tabsClosedUndoMessage(2),
                any(),
            )

            viewModel.sessionToDelete = null
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled and all private tabs to delete WHEN feature is started THEN remove all normal tabs, show undo snackbar and ensure 1 new tab remains`() {
        every { settings.enableHomepageAsNewTab } returns true
        every { viewModel.sessionToDelete } returns ALL_PRIVATE_TABS

        val tabsCount = 3
        val mockTabs =
            List(tabsCount) {
                mockk<TabSessionState> {
                    every { content.private } returns true
                }
            }

        every { browserStore.state } returns BrowserState(tabs = mockTabs)
        every { browsingModeManager.mode.isPrivate } returns true

        feature.start()

        verifyOrder {
            tabsUseCases.removePrivateTabs()

            fenixBrowserUseCases.addNewHomepageTab(private = true)

            feature.showUndoSnackbar(
                testContext.tabsClosedUndoMessage(tabsCount),
                any(),
            )

            viewModel.sessionToDelete = null
        }
    }

    @Test
    fun `GIVEN all private tabs to delete WHEN remove tabs is called THEN remove all normal tabs and show undo snackbar`() {
        val tabsCount = 5
        val mockTabs =
            List(tabsCount) {
                mockk<TabSessionState> {
                    every { content.private } returns true
                }
            }

        every { viewModel.sessionToDelete } returns ALL_PRIVATE_TABS
        every { browserStore.state } returns BrowserState(tabs = mockTabs)

        feature.start()

        verify {
            tabsUseCases.removePrivateTabs()

            feature.showUndoSnackbar(
                testContext.tabsClosedUndoMessage(tabsCount),
                any(),
            )

            viewModel.sessionToDelete = null
        }
    }

    @Test
    fun `GIVEN a session ID to delete WHEN feature is started THEN remove tab and show undo snackbar`() {
        val private = true
        val sessionId = "1"

        val tab: TabSessionState = mockk {
            every { content.private } returns private
            every { content.url } returns "https://www.mozilla.org"
            every { id } returns sessionId
        }

        every { browserStore.state.tabs } returns listOf(tab)
        every { viewModel.sessionToDelete } returns sessionId

        feature.start()

        verify {
            tabsUseCases.removeTab(sessionId)

            feature.showUndoSnackbar(
                testContext.getString(R.string.snackbar_private_tab_closed),
                any(),
            )

            viewModel.sessionToDelete = null
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled and the last tab is to be removed WHEN feature is started THEN remove tab, show undo snackbar and ensure a new tab remains`() {
        val private = true
        val sessionId = "1"
        val tab: TabSessionState = mockk {
            every { content.private } returns private
            every { content.url } returns "https://www.mozilla.org"
            every { id } returns sessionId
        }

        every { settings.enableHomepageAsNewTab } returns true
        every { browsingModeManager.mode.isPrivate } returns private
        every { viewModel.sessionToDelete } returns sessionId

        every { browserStore.state.tabs } returns listOf(tab)

        feature.start()

        verifyOrder {
            tabsUseCases.removeTab(sessionId)

            fenixBrowserUseCases.addNewHomepageTab(private = private)

            feature.showUndoSnackbar(
                testContext.getString(R.string.snackbar_private_tab_closed),
                any(),
            )

            viewModel.sessionToDelete = null
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled and a session ID to delete WHEN feature is started THEN remove tab and show undo snackbar`() {
        val private = true
        val sessionId = "1"

        val tab: TabSessionState = mockk {
            every { content.private } returns private
            every { content.url } returns "https://www.mozilla.org"
            every { id } returns sessionId
        }

        val secondTab: TabSessionState = mockk {
            every { content.private } returns private
            every { id } returns "2"
        }

        every { settings.enableHomepageAsNewTab } returns true
        every { browsingModeManager.mode.isPrivate } returns private
        every { viewModel.sessionToDelete } returns sessionId

        every { browserStore.state.tabs } returns listOf(tab, secondTab)

        feature.start()

        verify {
            tabsUseCases.removeTab(sessionId)

            feature.showUndoSnackbar(
                testContext.getString(R.string.snackbar_private_tab_closed),
                any(),
            )

            viewModel.sessionToDelete = null
        }

        verify(exactly = 0) {
            fenixBrowserUseCases.addNewHomepageTab(private = private)
        }
    }

    @Test
    fun `WHEN undo tabs removed is called THEN undo tab removal and navigate to browser`() {
        feature.onUndoTabsRemoved(tabId = "")

        verify {
            tabsUseCases.undo.invoke()

            navController.navigate(HomeFragmentDirections.actionGlobalBrowser(null))
        }
    }

    @Test
    fun `GIVEN a tab ID WHEN undo tabs removed is called THEN undo tab removal, remove the tab and navigate to browser`() {
        val tabId = "1"

        feature.onUndoTabsRemoved(tabId = tabId)

        verifyOrder {
            tabsUseCases.undo.invoke()
            tabsUseCases.removeTab.invoke(tabId)
            navController.navigate(HomeFragmentDirections.actionGlobalBrowser(null))
        }
    }

    @Test
    fun `GIVEN the restored tab is a homepage tab WHEN undo tabs removed is called THEN do not navigate away from the homepage`() {
        feature.onUndoTabsRemoved(tabId = "", isRestoringHomepageTab = true)

        verify {
            tabsUseCases.undo.invoke()
        }
        verify(exactly = 0) {
            navController.navigate(any<NavDirections>())
        }
    }

    @Test
    fun `GIVEN a session ID to delete and inactive tabs exist WHEN feature is started THEN remove tab excluding inactive tabs`() {
        val activeTab =
            createTab(
                id = "1",
                url = "https://mozilla.org",
                private = false,
                lastAccess = System.currentTimeMillis(),
                createdAt = System.currentTimeMillis(),
            )

        val inactiveTab =
            createTab(
                id = "99",
                url = "https://mozilla.org",
                private = false,
                lastAccess = 0L,
                createdAt = 0L,
            )

        every { browserStore.state } returns BrowserState(tabs = listOf(activeTab, inactiveTab))
        every { viewModel.sessionToDelete } returns "1"

        every { settings.inactiveTabsAreEnabled } returns true

        feature.start()

        verify {
            tabsUseCases.removeTab(tabId = "1", excludedTabIds = setOf("99"))

            feature.showUndoSnackbar(testContext.getString(R.string.snackbar_tab_closed), any())

            viewModel.sessionToDelete = null
        }
    }

    @Test
    fun `GIVEN a session ID to delete and no inactive tabs exist WHEN feature is started THEN remove tab excluding empty set`() {
        val activeTab =
            createTab(
                id = "1",
                url = "https://mozilla.org",
                private = false,
                lastAccess = System.currentTimeMillis(),
                createdAt = System.currentTimeMillis(),
            )

        every { browserStore.state } returns BrowserState(tabs = listOf(activeTab))

        every { viewModel.sessionToDelete } returns "1"

        feature.start()

        verify {
            tabsUseCases.removeTab(tabId = "1", excludedTabIds = emptySet())

            feature.showUndoSnackbar(testContext.getString(R.string.snackbar_tab_closed), any())

            viewModel.sessionToDelete = null
        }
    }
}
