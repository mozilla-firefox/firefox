/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.controller

import android.content.Context
import androidx.navigation.NavController
import androidx.navigation.NavDestination
import androidx.navigation.NavDirections
import androidx.navigation.NavOptions
import io.mockk.MockKAnnotations
import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.impl.annotations.RelaxedMockK
import io.mockk.just
import io.mockk.mockk
import io.mockk.runs
import io.mockk.spyk
import io.mockk.verify
import io.mockk.verifyOrder
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.storage.sync.Tab
import mozilla.components.browser.storage.sync.TabEntry
import mozilla.components.concept.base.profiler.Profiler
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.storage.BookmarkNodeType
import mozilla.components.feature.accounts.push.CloseTabsUseCases
import mozilla.components.feature.search.SearchUseCases
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.robolectric.testContext
import mozilla.telemetry.glean.private.NoExtras
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.bookmarks.BookmarksUseCase
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.components.usecases.ShareUseCases
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.maxActiveTime
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_NORMAL_TABS
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_PRIVATE_TABS
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.redux.store.TabsTrayStore
import org.mozilla.fenix.tabstray.ui.TabManagementFragmentDirections
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner
import java.util.concurrent.TimeUnit
import kotlin.test.assertNotNull

@RunWith(RobolectricTestRunner::class) // for gleanTestRule
class DefaultTabManagerControllerTest {
    @RelaxedMockK
    private lateinit var trayStore: TabsTrayStore

    @RelaxedMockK
    private lateinit var browserStore: BrowserStore

    @RelaxedMockK
    private lateinit var browsingModeManager: BrowsingModeManager

    @RelaxedMockK
    private lateinit var navController: NavController

    @RelaxedMockK
    private lateinit var profiler: Profiler

    @RelaxedMockK
    private lateinit var tabsUseCases: TabsUseCases

    @RelaxedMockK
    private lateinit var fenixBrowserUseCases: FenixBrowserUseCases

    @RelaxedMockK
    private lateinit var accountManager: FxaAccountManager

    private lateinit var loadUrlUseCase: SessionUseCases.DefaultLoadUrlUseCase
    private lateinit var searchUseCases: SearchUseCases
    private lateinit var homepageTitle: String
    private lateinit var context: Context

    private val appStore: AppStore = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private val shareUseCases: ShareUseCases = mockk(relaxed = true)

    private val addBookmarkUseCase: BookmarksUseCase.AddBookmarksUseCase = mockk(relaxed = true)
    private val closeSyncedTabsUseCases: CloseTabsUseCases = mockk(relaxed = true)
    private val collectionStorage: TabCollectionStorage = mockk(relaxed = true)
    private val testDispatcher = StandardTestDispatcher()

    private val testPrivateTab = createTab(
        id = "privateTestTabId",
        url = "",
        private = true,
    )

    private val testNormalTab = createTab(
        id = "testTabId",
        url = "https://www.mozilla.org",
    )

    private val testHomeTab = createTab(
        id = "testHomeTabId",
        url = ABOUT_HOME_URL,
    )

    @get:Rule
    val gleanTestRule = FenixGleanTestRule(testContext)

    @Before
    fun setup() {
        MockKAnnotations.init(this)
        context = spyk(testContext)
        loadUrlUseCase = mockk(relaxed = true)
        searchUseCases = mockk(relaxed = true)
        homepageTitle = testContext.getString(R.string.tab_tray_homepage_tab)
        profiler = mockk(relaxed = true) {
            every { getProfilerTime() } returns PROFILER_START_TIME
            every { isProfilerActive() } returns true
        }
    }

    @Test
    fun `GIVEN private mode WHEN the fab is clicked THEN a profile marker is added for the operations executed`() {
        profiler = spyk(profiler) {
            every { getProfilerTime() } returns Double.MAX_VALUE
        }

        assertNull(TabsTray.newPrivateTabTapped.testGetValue())

        val target = createController()
        target.handlePrivateTabsFabClick()

        assertNotNull(TabsTray.newPrivateTabTapped.testGetValue())

        verifyOrder {
            profiler.getProfilerTime()
            navController.popBackStack()
            navController.navigate(
                TabManagementFragmentDirections.actionGlobalHome(focusOnAddressBar = true),
            )
            TabsTray.closed.record(NoExtras())
            profiler.addMarker(
                "DefaultTabManagerController.onNewTabTapped",
                Double.MAX_VALUE,
            )
        }
    }

    @Test
    fun `GIVEN private mode and homepage as a new tab is enabled WHEN the fab is clicked THEN a new private homepage tab is displayed`() {
        every { settings.enableHomepageAsNewTab } returns true

        profiler = spyk(profiler) {
            every { getProfilerTime() } returns Double.MAX_VALUE
        }

        assertNull(TabsTray.newPrivateTabTapped.testGetValue())

        val target = createController()
        target.handlePrivateTabsFabClick()

        assertNotNull(TabsTray.newPrivateTabTapped.testGetValue())

        verifyOrder {
            profiler.getProfilerTime()
            fenixBrowserUseCases.addNewHomepageTab(
                private = true,
            )
            TabsTray.closed.record(NoExtras())
            profiler.addMarker(
                "DefaultTabManagerController.onNewTabTapped",
                Double.MAX_VALUE,
            )
        }
    }

    @Test
    fun `GIVEN normal mode WHEN the fab is clicked THEN a profile marker is added for the operations executed`() {
        profiler = spyk(profiler) {
            every { getProfilerTime() } returns Double.MAX_VALUE
        }

        val target = createController()
        target.handleNormalTabsFabClick()

        verifyOrder {
            profiler.getProfilerTime()
            navController.popBackStack()
            navController.navigate(
                TabManagementFragmentDirections.actionGlobalHome(focusOnAddressBar = true),
            )
            TabsTray.closed.record(NoExtras())
            profiler.addMarker(
                "DefaultTabManagerController.onNewTabTapped",
                Double.MAX_VALUE,
            )
        }
    }

    @Test
    fun `GIVEN normal mode and homepage as a new tab is enabled WHEN the fab is clicked THEN a new homepage tab is displayed`() {
        every { settings.enableHomepageAsNewTab } returns true

        profiler = spyk(profiler) {
            every { getProfilerTime() } returns Double.MAX_VALUE
        }

        val target = createController()
        target.handleNormalTabsFabClick()

        verifyOrder {
            profiler.getProfilerTime()
            fenixBrowserUseCases.addNewHomepageTab(
                private = false,
            )
            TabsTray.closed.record(NoExtras())
            profiler.addMarker(
                "DefaultTabManagerController.onNewTabTapped",
                Double.MAX_VALUE,
            )
        }
    }

    @Test
    fun `GIVEN private mode WHEN the fab is clicked THEN Event#NewPrivateTabTapped is added to telemetry`() {
        assertNull(TabsTray.newPrivateTabTapped.testGetValue())

        createController().handlePrivateTabsFabClick()

        assertNotNull(TabsTray.newPrivateTabTapped.testGetValue())
    }

    @Test
    fun `GIVEN normal mode WHEN the fab is clicked THEN Event#NewTabTapped is added to telemetry`() {
        assertNull(TabsTray.newTabTapped.testGetValue())

        createController().handleNormalTabsFabClick()

        assertNotNull(TabsTray.newTabTapped.testGetValue())
    }

    @Test
    fun `GIVEN the user is on the synced tabs page WHEN the fab is clicked THEN fire off a sync action`() {
        every { trayStore.state.sync.isSyncing } returns false

        createController().handleSyncedTabsFabClick()

        verify { trayStore.dispatch(TabsTrayAction.SyncNow) }
    }

    @Test
    fun `GIVEN the user is on the synced tabs page and there is already an active sync WHEN the fab is clicked THEN no action should be taken`() {
        every { trayStore.state.sync.isSyncing } returns true

        createController().handleSyncedTabsFabClick()

        verify(exactly = 0) { trayStore.dispatch(TabsTrayAction.SyncNow) }
    }

    @Test
    fun `WHEN handleTabDeletion is called THEN Event#ClosedExistingTab is added to telemetry`() {
        val tab = createTab(id = "testTabId", url = "https://mozilla.org", private = true)

        assertNull(TabsTray.closedExistingTab.testGetValue())

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab)
            every { selectedTabId } returns "otherTabId"
        }

        every { trayStore.state } returns mockk {
            every { selectedTabId } returns "otherTabId"
            every { privateBrowsing } returns mockk {
                every { tabs } returns listOf(TabsTrayItem.Tab(tab))
            }
            every { inactiveTabs } returns mockk {
                every { tabs } returns emptyList()
            }
        }

        createController().handleTabDeletion(TabsTrayItem.Tab(tab = tab), "unknown")
        assertNotNull(TabsTray.closedExistingTab.testGetValue())
    }

    @Test
    fun `GIVEN active private download WHEN handleTabDeletion is called for the last private tab THEN showCancelledDownloadWarning is called`() {
        var showCancelledDownloadWarningInvoked = false

        val tab = createTab(id = "testTabId", url = "https://mozilla.org", private = true)

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab)
            every { selectedTabId } returns "testTabId"
            every { downloads } returns mapOf(
                "1" to DownloadState(
                    "https://mozilla.org/download",
                    private = true,
                    status = DownloadState.Status.DOWNLOADING,
                ),
            )
        }

        every { trayStore.state } returns mockk {
            every { selectedTabId } returns "testTabId"
            every { privateBrowsing } returns mockk {
                every { tabs } returns listOf(TabsTrayItem.Tab(tab))
            }
            every { inactiveTabs } returns mockk {
                every { tabs } returns emptyList()
            }
        }

        val controller = createController(
            showCancelledDownloadWarning = { _, _, _ ->
                showCancelledDownloadWarningInvoked = true
            },
        )

        controller.handleTabDeletion(TabsTrayItem.Tab(tab = tab), "unknown")

        assertTrue(showCancelledDownloadWarningInvoked)
    }

    @Test
    fun `WHEN handleTabTrayPageClicked is called THEN it emits an action for the Page of that tab position`() {
        val page = Page.SyncedTabs
        every { trayStore.state.selectedPage } returns Page.NormalTabs

        createController().handleTabPageClicked(page)
        verify { trayStore.dispatch(TabsTrayAction.PageSelected(page)) }
    }

    @Test
    fun `GIVEN not already on browserFragment WHEN handleNavigateToBrowser is called THEN the manager is closed and popBackStack is executed`() {
        every { navController.currentDestination?.id } returns R.id.browserFragment + 1
        every { navController.popBackStack(R.id.browserFragment, false) } returns true

        createController().handleNavigateToBrowser()

        verify { navController.popBackStack(R.id.browserFragment, false) }
        verify(exactly = 0) { navController.navigate(any<Int>()) }
        verify(exactly = 0) { navController.navigate(any<NavDirections>()) }
        verify(exactly = 0) { navController.navigate(any<NavDirections>(), any<NavOptions>()) }
    }

    @Test
    fun `GIVEN not already on browserFragment WHEN handleNavigateToBrowser is called and popBackStack fails THEN it navigates to browserFragment`() {
        every { navController.currentDestination?.id } returns R.id.browserFragment + 1
        every { navController.popBackStack(R.id.browserFragment, false) } returns false
        every { browserStore.state.selectedTab?.content?.url } returns "https://www.mozilla.org"

        createController().handleNavigateToBrowser()

        verify { navController.popBackStack(R.id.browserFragment, false) }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `GIVEN not already on browserFragment WHEN handleNavigateToBrowser is called and popBackStack succeeds THEN the method finishes`() {
        every { navController.popBackStack(R.id.browserFragment, false) } returns true

        createController().handleNavigateToBrowser()

        verify(exactly = 1) { navController.popBackStack(R.id.browserFragment, false) }
        verify(exactly = 0) { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `GIVEN the browser is currently shown WHEN navigate to home is called THEN the manager is closed and popBackStack is executed`() {
        every { navController.currentDestination?.id } returns R.id.browserFragment
        every { navController.popBackStack(R.id.homeFragment, false) } returns true

        createController().handleNavigateToHome()

        verify { navController.popBackStack(R.id.homeFragment, false) }
        verify(exactly = 0) { navController.navigate(any<Int>()) }
        verify(exactly = 0) { navController.navigate(any<NavDirections>()) }
        verify(exactly = 0) { navController.navigate(any<NavDirections>(), any<NavOptions>()) }
    }

    @Test
    fun `GIVEN the browser is currently shown WHEN navigate to home is called and pop back stack fails THEN it navigates to home`() {
        every { navController.currentDestination?.id } returns R.id.browserFragment
        every { navController.popBackStack(R.id.homeFragment, false) } returns false

        createController().handleNavigateToHome()

        verify { navController.popBackStack(R.id.homeFragment, false) }
        verify { navController.navigate(TabManagementFragmentDirections.actionGlobalHome()) }
    }

    @Test
    fun `WHEN navigate to home is called and popBackStack succeeds THEN the method finishes`() {
        every { navController.popBackStack(R.id.homeFragment, false) } returns true

        createController().handleNavigateToHome()

        verify(exactly = 1) { navController.popBackStack(R.id.homeFragment, false) }
        verify(exactly = 0) { navController.navigate(TabManagementFragmentDirections.actionGlobalHome()) }
    }

    @Test
    fun `GIVEN more tabs opened WHEN handleTabDeletion is called THEN that tab is removed and an undo snackbar is shown`() {
        val tab = createTab(id = "22", url = "https://mozilla.org", private = true)

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab, testNormalTab)
            every { selectedTabId } returns "0"
        }

        every { trayStore.state } returns mockk {
            every { selectedTabId } returns "0"
            every { privateBrowsing } returns mockk {
                every { tabs } returns listOf(TabsTrayItem.Tab(tab))
            }
            every { inactiveTabs } returns mockk {
                every { tabs } returns emptyList()
            }
        }

        var showUndoSnackbarForTabInvoked = false
        createController(
            showUndoSnackbarForTab = {
                assertTrue(it)
                showUndoSnackbarForTabInvoked = true
            },
        ).handleTabDeletion(TabsTrayItem.Tab(tab = tab))

        verify { tabsUseCases.removeTab("22", emptySet()) }
        assertTrue(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `GIVEN only one tab opened WHEN handleTabDeletion is called THEN it navigates to home where the tab will be removed`() {
        val testTabId = "33"
        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(createController(showUndoSnackbarForTab = { showUndoSnackbarForTabInvoked = true }))

        val tab = createTab(id = testTabId, url = "https://mozilla.org", private = true)

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab)
            every { selectedTabId } returns testTabId
            every { downloads } returns emptyMap()
        }

        every { trayStore.state } returns mockk {
            every { selectedTabId } returns testTabId
            every { privateBrowsing } returns mockk {
                every { tabs } returns listOf(TabsTrayItem.Tab(tab))
            }
            every { inactiveTabs } returns mockk {
                every { tabs } returns emptyList()
            }
        }

        controller.handleTabDeletion(TabsTrayItem.Tab(tab = tab))

        verify { controller.dismissTabManagerAndNavigateHome(testTabId) }

        verify(exactly = 0) { tabsUseCases.removeTab(any()) }

        assertFalse(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `WHEN handleMultipleTabsDeletion is called to close all private tabs THEN that it navigates to home where that tabs will be removed`() {
        val controller = spyk(createController())

        val privateTab1 = createTab(id = "1", url = "https://mozilla.org", private = true)
        val privateTab2 = createTab(id = "2", url = "https://mozilla.org", private = true)
        val tabItems = listOf(TabsTrayItem.Tab(tab = privateTab1), TabsTrayItem.Tab(tab = privateTab2))

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(privateTab1, privateTab2)
        }

        every { trayStore.state } returns mockk {
            every { privateBrowsing } returns mockk {
                every { tabs } returns tabItems
            }
            every { inactiveTabs } returns mockk {
                every { tabs } returns emptyList()
            }
        }

        controller.deleteMultipleTabs(tabItems)

        verify { controller.dismissTabManagerAndNavigateHome(ALL_PRIVATE_TABS) }

        verify(exactly = 0) { tabsUseCases.removeTabs(any<List<String>>(), any<Set<String>>()) }
    }

    @Test
    fun `WHEN handleMultipleTabsDeletion is called to close all normal tabs THEN that it navigates to home where that tabs will be removed`() {
        val normalTab1 = createTab(id = "1", url = "https://mozilla.org", private = false)
        val normalTab2 = createTab(id = "2", url = "https://mozilla.org", private = false)
        val tabItems = listOf(TabsTrayItem.Tab(tab = normalTab1), TabsTrayItem.Tab(tab = normalTab2))

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(normalTab1, normalTab2)
        }

        every { trayStore.state } returns mockk {
            every { normalTabsState.items } returns tabItems
            every { inactiveTabs } returns mockk {
                every { tabs } returns emptyList()
            }
        }

        val controller = spyk(createController())

        controller.deleteMultipleTabs(tabItems)

        verify { controller.dismissTabManagerAndNavigateHome(ALL_NORMAL_TABS) }

        verify(exactly = 0) { tabsUseCases.removeTabs(any<List<String>>(), any<Set<String>>()) }
    }

    @Test
    fun `WHEN handleMultipleTabsDeletion is called to close some private tabs THEN that it uses tabsUseCases#removeTabs and shows an undo snackbar`() {
        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(
            createController(showUndoSnackbarForTab = { showUndoSnackbarForTabInvoked = true }),
        )

        val privateTabToClose = createTab(id = "42", url = "https://mozilla.org", private = true)
        val otherPrivateTab = createTab(id = "43", url = "https://mozilla.org", private = true)

        every { browserStore.state } returns mockk {
            every { selectedTabId } returns "42"
            every { tabs } returns listOf(privateTabToClose, otherPrivateTab)
        }

        every { trayStore.state } returns mockk {
            every { privateBrowsing } returns mockk {
                every { tabs } returns listOf(
                    TabsTrayItem.Tab(tab = privateTabToClose),
                    TabsTrayItem.Tab(tab = otherPrivateTab),
                )
            }
            every { inactiveTabs } returns mockk {
                every { tabs } returns emptyList()
            }
        }

        controller.deleteMultipleTabs(listOf(TabsTrayItem.Tab(tab = privateTabToClose)))

        verify { tabsUseCases.removeTabs(listOf("42"), emptySet()) }
        verify(exactly = 0) { controller.dismissTabManagerAndNavigateHome(any()) }
        assertTrue(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `WHEN handleMultipleTabsDeletion is called to close some normal tabs THEN that it uses tabsUseCases#removeTabs and shows an undo snackbar`() {
        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(createController(showUndoSnackbarForTab = { showUndoSnackbarForTabInvoked = true }))

        val normalTab1 = createTab(id = "24", url = "https://mozilla.org", private = false)
        val normalTab2 = createTab(id = "25", url = "https://mozilla.org", private = false)

        every { browserStore.state } returns BrowserState(
            tabs = listOf(normalTab1, normalTab2),
            selectedTabId = "24",
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            selectedTabId = "24",
            normalTabsState = TabsTrayState.NormalTabsState(
                items = listOf(
                    TabsTrayItem.Tab(tab = normalTab1),
                    TabsTrayItem.Tab(tab = normalTab2),
                ),
            ),
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = emptyList()),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = emptyList()),
            tabGroupState = TabsTrayState.TabGroupState(groups = emptyList()),
        )

        controller.deleteMultipleTabs(listOf(TabsTrayItem.Tab(tab = normalTab1)))

        verify { tabsUseCases.removeTabs(listOf("24"), emptySet()) }
        verify(exactly = 0) { controller.dismissTabManagerAndNavigateHome(any()) }
        assertTrue(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `GIVEN 1 active tab and 1 inactive tab WHEN handleTabDeletion is called on the active tab THEN it navigates home and excludes the inactive tab`() {
        val activeTab = createTab(id = "activeId", url = "https://mozilla.org", private = false)
        val inactiveTab = createTab(id = "inactiveId", url = "https://mozilla.org", private = false)

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(activeTab, inactiveTab)
            every { selectedTabId } returns "activeId"
            every { downloads } returns emptyMap()
        }

        every { trayStore.state } returns mockk {
            every { selectedTabId } returns "activeId"
            every { normalTabsState.items } returns listOf(TabsTrayItem.Tab(activeTab))
            every { privateBrowsing } returns mockk { every { tabs } returns emptyList() }
            every { inactiveTabs } returns mockk {
                every { tabs } returns listOf(TabsTrayItem.Tab(inactiveTab))
            }
        }

        val controller = spyk(createController())

        controller.handleTabDeletion(TabsTrayItem.Tab(tab = activeTab))

        verify { controller.dismissTabManagerAndNavigateHome("activeId") }

        verify(exactly = 0) { tabsUseCases.removeTab(any()) }
    }

    @Test
    fun `GIVEN 2 active tabs and 1 inactive tab WHEN deleteMultipleTabs is called on both active tabs THEN it navigates home and excludes the inactive tab`() {
        val activeTab1 = createTab(id = "active1", url = "https://mozilla.org", private = false)
        val activeTab2 = createTab(id = "active2", url = "https://mozilla.org", private = false)
        val inactiveTab = createTab(id = "inactiveId", url = "https://mozilla.org", private = false)

        val activeTabItems = listOf(TabsTrayItem.Tab(activeTab1), TabsTrayItem.Tab(activeTab2))

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(activeTab1, activeTab2, inactiveTab)
        }

        every { trayStore.state } returns mockk {
            every { normalTabsState.items } returns activeTabItems
            every { privateBrowsing } returns mockk { every { tabs } returns emptyList() }
            every { inactiveTabs } returns mockk {
                every { tabs } returns listOf(TabsTrayItem.Tab(inactiveTab))
            }
        }

        val controller = spyk(createController())

        controller.deleteMultipleTabs(activeTabItems)

        verify { controller.dismissTabManagerAndNavigateHome(ALL_NORMAL_TABS) }

        verify(exactly = 0) { tabsUseCases.removeTabs(ids = any(), excludedTabIds = any()) }
    }

    @Test
    fun `GIVEN 2 active tabs and 1 inactive tab WHEN handleTabDeletion is called on 1 active tab THEN it removes the tab, excludes the inactive tab, and shows undo snackbar`() {
        val active1 = createTab(id = "active1", url = "https://mozilla.org", private = false)
        val active2 = createTab(id = "active2", url = "https://mozilla.org", private = false)
        val inactiveTab = createTab(id = "inactive1", url = "https://mozilla.org", private = false)

        every { browserStore.state } returns BrowserState(
            tabs = listOf(active1, active2, inactiveTab),
            selectedTabId = "active1",
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            selectedTabId = "active1",
            normalTabsState = TabsTrayState.NormalTabsState(
                items = listOf(TabsTrayItem.Tab(tab = active1), TabsTrayItem.Tab(tab = active2)),
            ),
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = emptyList()),
            inactiveTabs = TabsTrayState.InactiveTabsState(
                tabs = listOf(TabsTrayItem.Tab(tab = inactiveTab)),
            ),
            tabGroupState = TabsTrayState.TabGroupState(groups = emptyList()),
        )

        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(
            createController(showUndoSnackbarForTab = {
            showUndoSnackbarForTabInvoked = true
        }),
        )

        controller.handleTabDeletion(TabsTrayItem.Tab(tab = active1))

        verify { tabsUseCases.removeTab(tabId = "active1", excludedTabIds = setOf("inactive1")) }
        verify(exactly = 0) { controller.dismissTabManagerAndNavigateHome(sessionId = any()) }
        assertTrue(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `GIVEN grouped tabs and an inactive tab WHEN deleteMultipleTabs is called on all grouped tabs THEN it flattens the group, navigates home, and excludes the inactive tab`() {
        val grouped1 = createTab(id = "grouped1", url = "https://mozilla.org", private = false)
        val grouped2 = createTab(id = "grouped2", url = "https://mozilla.org", private = false)
        val inactiveTab = createTab(id = "inactive1", url = "https://mozilla.org", private = false)

        val tabGroup = TabsTrayItem.TabGroup(
            id = "group1",
            title = "Test Group",
            theme = TabGroupTheme.default,
            tabs = mutableListOf(TabsTrayItem.Tab(grouped1), TabsTrayItem.Tab(grouped2)),
        )

        val itemsToDelete = listOf(TabsTrayItem.Tab(grouped1), TabsTrayItem.Tab(grouped2))

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(grouped1, grouped2, inactiveTab)
        }

        every { trayStore.state } returns mockk {
            every { normalTabsState.items } returns listOf(tabGroup)
            every { privateBrowsing } returns mockk { every { tabs } returns emptyList() }
            every { inactiveTabs } returns mockk {
                every { tabs } returns listOf(TabsTrayItem.Tab(inactiveTab))
            }
        }

        val controller = spyk(createController())

        controller.deleteMultipleTabs(itemsToDelete)

        verify { controller.dismissTabManagerAndNavigateHome(ALL_NORMAL_TABS) }
        verify(exactly = 0) { tabsUseCases.removeTabs(any(), any()) }
    }

    @Test
    fun `GIVEN multiple private tabs and one is focused WHEN handleTabDeletion is called on the focused tab THEN it removes the tab and shows an undo snackbar`() {
        val focusedPrivateTab = createTab(id = "focusedPrivate", url = "https://mozilla.org", private = true)
        val otherPrivateTab = createTab(id = "otherPrivate", url = "https://mozilla.org", private = true)

        every { browserStore.state } returns BrowserState(
            tabs = listOf(focusedPrivateTab, otherPrivateTab),
            selectedTabId = "focusedPrivate",
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            selectedTabId = "focusedPrivate",
            privateBrowsing = TabsTrayState.PrivateBrowsingState(
                tabs = listOf(
                    TabsTrayItem.Tab(focusedPrivateTab),
                    TabsTrayItem.Tab(otherPrivateTab),
                ),
            ),
            normalTabsState = TabsTrayState.NormalTabsState(items = emptyList()),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = emptyList()),
        )

        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(
            createController(showUndoSnackbarForTab = { isPrivate ->
                assertTrue(isPrivate)
                showUndoSnackbarForTabInvoked = true
            }),
        )

        controller.handleTabDeletion(TabsTrayItem.Tab(tab = focusedPrivateTab))

        verify { tabsUseCases.removeTab(tabId = "focusedPrivate", excludedTabIds = any()) }
        verify(exactly = 0) { controller.dismissTabManagerAndNavigateHome(any()) }
        assertTrue(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `GIVEN multiple normal tabs and one is focused WHEN handleTabDeletion is called on the focused tab THEN it removes the tab and shows an undo snackbar`() {
        val focusedNormalTab = createTab(id = "focusedNormal", url = "https://mozilla.org", private = false)
        val otherNormalTab = createTab(id = "otherNormal", url = "https://mozilla.org", private = false)

        every { browserStore.state } returns BrowserState(
            tabs = listOf(focusedNormalTab, otherNormalTab),
            selectedTabId = "focusedNormal",
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            selectedTabId = "focusedNormal",
            normalTabsState = TabsTrayState.NormalTabsState(
                items = listOf(
                    TabsTrayItem.Tab(focusedNormalTab),
                    TabsTrayItem.Tab(otherNormalTab),
                ),
            ),
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = emptyList()),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = emptyList()),
        )

        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(
            createController(showUndoSnackbarForTab = { isPrivate ->
                assertFalse(isPrivate)
                showUndoSnackbarForTabInvoked = true
            }),
        )

        controller.handleTabDeletion(TabsTrayItem.Tab(tab = focusedNormalTab))

        verify { tabsUseCases.removeTab(tabId = "focusedNormal", excludedTabIds = emptySet()) }
        verify(exactly = 0) { controller.dismissTabManagerAndNavigateHome(any()) }
        assertTrue(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `GIVEN only one normal tab opened WHEN handleTabDeletion is called THEN it navigates to home where the tab will be removed`() {
        val normalTab = createTab(id = "onlyNormalTab", url = "https://mozilla.org", private = false)

        val displayTab = TabsTrayItem.Tab(tab = normalTab)

        every { browserStore.state } returns BrowserState(
            tabs = listOf(normalTab),
            selectedTabId = "onlyNormalTab",
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            selectedTabId = "onlyNormalTab",
            normalTabsState = TabsTrayState.NormalTabsState(
                items = listOf(TabsTrayItem.Tab(tab = normalTab)),
            ),
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = emptyList()),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = emptyList()),
        )

        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(
            createController(showUndoSnackbarForTab = {
                showUndoSnackbarForTabInvoked = true
            }),
        )

        controller.handleTabDeletion(displayTab)

        verify { controller.dismissTabManagerAndNavigateHome(displayTab.id) }
        verify(exactly = 0) { tabsUseCases.removeTab(tabId = displayTab.id, excludedTabIds = any()) }
        assertFalse(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `WHEN handleMultipleTabsDeletion is called to close all private tabs THEN it navigates to home where those tabs will be removed`() {
        val privateTab1 = createTab(id = "private1", url = "https://mozilla.org", private = true)
        val privateTab2 = createTab(id = "private2", url = "https://mozilla.org", private = true)
        val tabItems = listOf(TabsTrayItem.Tab(tab = privateTab1), TabsTrayItem.Tab(tab = privateTab2))

        every { browserStore.state } returns BrowserState(
            tabs = listOf(privateTab1, privateTab2),
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            privateBrowsing = TabsTrayState.PrivateBrowsingState(
                tabs = tabItems,
            ),
            normalTabsState = TabsTrayState.NormalTabsState(items = emptyList()),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = emptyList()),
        )

        val controller = spyk(createController())

        controller.deleteMultipleTabs(tabItems)

        verify { controller.dismissTabManagerAndNavigateHome(ALL_PRIVATE_TABS) }
        verify(exactly = 0) { tabsUseCases.removeTabs(ids = any(), excludedTabIds = any()) }
    }

    @Test
    fun `WHEN handleMultipleTabsDeletion is called to close all inactive tabs THEN it navigates to home where those tabs will be removed`() {
        val inactiveTab1 = createTab(id = "inactive1", url = "https://mozilla.org", private = false)
        val inactiveTab2 = createTab(id = "inactive2", url = "https://mozilla.com", private = false)

        val inactiveTabItems = listOf(TabsTrayItem.Tab(inactiveTab1), TabsTrayItem.Tab(inactiveTab2))

        every { browserStore.state } returns BrowserState(
            tabs = listOf(inactiveTab1, inactiveTab2),
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = emptyList()),
            normalTabsState = TabsTrayState.NormalTabsState(items = emptyList()),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = inactiveTabItems),
        )

        val controller = spyk(createController())

        controller.deleteMultipleTabs(tabs = inactiveTabItems)

        verify { controller.dismissTabManagerAndNavigateHome(ALL_NORMAL_TABS) }
        verify(exactly = 0) { tabsUseCases.removeTabs(ids = any(), excludedTabIds = any()) }
    }

    @Test
    fun `GIVEN no tab groups exist WHEN handleMultipleTabsDeletion is called to close all normal tabs THEN it navigates to home where those tabs will be removed`() {
        val normalTab1 = createTab(id = "normal1", url = "https://mozilla.org", private = false)
        val normalTab2 = createTab(id = "normal2", url = "https://mozilla.com", private = false)

        val normalTabItems = listOf(TabsTrayItem.Tab(normalTab1), TabsTrayItem.Tab(normalTab2))

        every { browserStore.state } returns BrowserState(
            tabs = listOf(normalTab1, normalTab2),
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = emptyList()),
            normalTabsState = TabsTrayState.NormalTabsState(items = normalTabItems),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = emptyList()),
            tabGroupState = TabsTrayState.TabGroupState(groups = emptyList()),
        )

        val controller = spyk(createController())

        controller.deleteMultipleTabs(tabs = normalTabItems)

        verify { controller.dismissTabManagerAndNavigateHome(ALL_NORMAL_TABS) }

        verify(exactly = 0) { tabsUseCases.removeTabs(ids = any(), excludedTabIds = any()) }
    }

    @Test
    fun `GIVEN only open tab groups exist WHEN handleMultipleTabsDeletion is called to close all normal tabs THEN it flattens the groups and navigates to home`() {
        val groupedTab1 = createTab(id = "grouped1", url = "https://mozilla.org", private = false)
        val groupedTab2 = createTab(id = "grouped2", url = "https://mozilla.com", private = false)

        val tabItemsToDelete = listOf(TabsTrayItem.Tab(groupedTab1), TabsTrayItem.Tab(groupedTab2))

        val openTabGroup = TabsTrayItem.TabGroup(
            id = "group1",
            title = "Group 1",
            theme = TabGroupTheme.default,
            tabs = tabItemsToDelete.toMutableList(),
            closed = false,
        )

        every { browserStore.state } returns BrowserState(
            tabs = listOf(groupedTab1, groupedTab2),
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = emptyList()),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = emptyList()),
            tabGroupState = TabsTrayState.TabGroupState(groups = listOf(openTabGroup)),
            normalTabsState = TabsTrayState.NormalTabsState(items = listOf(openTabGroup)),
        )

        val controller = spyk(createController())

        controller.deleteMultipleTabs(tabs = tabItemsToDelete)

        verify { controller.dismissTabManagerAndNavigateHome(ALL_NORMAL_TABS) }

        verify(exactly = 0) {
            tabsUseCases.removeTabs(ids = any(), excludedTabIds = any())
        }
    }

    @Test
    fun `GIVEN a closed tab group exists WHEN deleteMultipleTabs is called to close all visible normal tabs THEN it navigates to home`() {
        val visibleTab = createTab(id = "visibleTab", url = "https://mozilla.org", private = false)
        val closedGroupTab = createTab(id = "closedTabForTabGroup", url = "https://mozilla.org", private = false)

        val visibleTabItem = TabsTrayItem.Tab(tab = visibleTab)

        val closedTabGroup = TabsTrayItem.TabGroup(
            id = "group1",
            title = "Closed Group",
            theme = TabGroupTheme.default,
            tabs = mutableListOf(TabsTrayItem.Tab(closedGroupTab)),
            closed = true,
        )

        every { browserStore.state } returns BrowserState(
            tabs = listOf(visibleTab, closedGroupTab),
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            tabGroupState = TabsTrayState.TabGroupState(groups = listOf(closedTabGroup)),
            normalTabsState = TabsTrayState.NormalTabsState(items = listOf(visibleTabItem)),
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = emptyList()),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = emptyList()),
        )

        val controller = spyk(createController())

        controller.deleteMultipleTabs(listOf(visibleTabItem))

        verify { controller.dismissTabManagerAndNavigateHome(ALL_NORMAL_TABS) }
        verify(exactly = 0) { tabsUseCases.removeTabs(ids = any(), excludedTabIds = any()) }
    }

    @Test
    fun `GIVEN multiple normal tabs WHEN deleteMultipleTabs is called but the focused tab is NOT included THEN it removes only the selected tabs `() {
        val focusedTab = createTab(id = "focused", url = "https://mozilla.org", private = false)
        val tabToDelete = createTab(id = "delete1", url = "https://mozilla.com", private = false)
        val tabToDelete2 = createTab(id = "delete2", url = "https://example.com", private = false)

        val itemsToDelete = listOf(TabsTrayItem.Tab(tabToDelete), TabsTrayItem.Tab(tabToDelete2))

        every { browserStore.state } returns BrowserState(
            tabs = listOf(focusedTab, tabToDelete, tabToDelete2),
            selectedTabId = "focused",
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            selectedTabId = "focused",
            normalTabsState = TabsTrayState.NormalTabsState(
                items = listOf(TabsTrayItem.Tab(tab = focusedTab)) + itemsToDelete,
            ),
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = emptyList()),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = emptyList()),
        )

        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(
            createController(showUndoSnackbarForTab = {
                showUndoSnackbarForTabInvoked = true
            }),
        )

        controller.deleteMultipleTabs(itemsToDelete)

        verify { tabsUseCases.removeTabs(ids = itemsToDelete.map { it.id }, excludedTabIds = emptySet()) }

        verify(exactly = 0) { controller.dismissTabManagerAndNavigateHome(any()) }
        assertTrue(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `GIVEN multiple normal and inactive tabs WHEN deleteMultipleTabs includes the focused tab THEN it removes the tabs, excludes the inactive tab, and shows an undo snackbar`() {
        val focusedTab = createTab(id = "focused_tab_to_delete", url = "https://mozilla.org", private = false)
        val normalTabToDelete = createTab(id = "normal_deleting", url = "https://mozilla.com", private = false)
        val normalTabSurviving = createTab(id = "normal_safe", url = "https://mozilla.org", private = false)
        val inactiveTab = createTab(id = "inactive_safe", url = "https://mozilla.org", private = false)

        val itemsToDelete = listOf(TabsTrayItem.Tab(focusedTab), TabsTrayItem.Tab(normalTabToDelete))

        every { browserStore.state } returns BrowserState(
            tabs = listOf(focusedTab, normalTabToDelete, normalTabSurviving, inactiveTab),
            selectedTabId = "focused_tab_to_delete",
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            selectedTabId = "focused_tab_to_delete",
            normalTabsState = TabsTrayState.NormalTabsState(
                items = itemsToDelete + TabsTrayItem.Tab(normalTabSurviving),
            ),
            inactiveTabs = TabsTrayState.InactiveTabsState(
                tabs = listOf(TabsTrayItem.Tab(inactiveTab)),
            ),
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = emptyList()),
            tabGroupState = TabsTrayState.TabGroupState(groups = emptyList()),
        )

        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(
            createController(showUndoSnackbarForTab = {
            showUndoSnackbarForTabInvoked = true
        }),
        )

        controller.deleteMultipleTabs(tabs = itemsToDelete)

        verify { tabsUseCases.removeTabs(ids = itemsToDelete.map { it.id }, excludedTabIds = setOf("inactive_safe")) }

        verify(exactly = 0) { controller.dismissTabManagerAndNavigateHome(any()) }
        assertTrue(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `GIVEN an unselected visible tab and a closed group WHEN deleteMultipleTabs includes the focused tab THEN it removes the tab and omits the closed group from excludedTabIds`() {
        val focusedTabToDelete = createTab(id = "focused_tab_to_delete", url = "https://mozilla.org", private = false)
        val unselectedVisibleTab = createTab(id = "unselected_visible_tab", url = "https://mozilla.com", private = false)
        val tabInsideClosedGroup = createTab(id = "tab_in_closed_group", url = "https://mozilla.org", private = false)

        val itemsToDelete = listOf(TabsTrayItem.Tab(focusedTabToDelete))

        val closedTabGroup = TabsTrayItem.TabGroup(
            id = "group1",
            title = "Closed Group",
            theme = TabGroupTheme.default,
            tabs = mutableListOf(TabsTrayItem.Tab(tabInsideClosedGroup)),
            closed = true,
        )

        every { browserStore.state } returns BrowserState(
            tabs = listOf(focusedTabToDelete, unselectedVisibleTab, tabInsideClosedGroup),
            selectedTabId = "focused_tab_to_delete",
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            selectedTabId = "focused_tab_to_delete",
            normalTabsState = TabsTrayState.NormalTabsState(items = itemsToDelete + TabsTrayItem.Tab(unselectedVisibleTab)),
            tabGroupState = TabsTrayState.TabGroupState(groups = listOf(closedTabGroup)),
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = emptyList()),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = emptyList()),
        )

        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(
            createController(showUndoSnackbarForTab = {
            showUndoSnackbarForTabInvoked = true
        }),
        )

        controller.deleteMultipleTabs(tabs = itemsToDelete)

        verify { tabsUseCases.removeTabs(ids = listOf("focused_tab_to_delete"), excludedTabIds = emptySet()) }

        verify(exactly = 0) { controller.dismissTabManagerAndNavigateHome(any()) }
        assertTrue(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `GIVEN multiple normal tabs WHEN deleteMultipleTabs manually includes all open normal tabs THEN it navigates to home where those tabs will be removed`() {
        val normalTabToDelete1 = createTab(id = "normal_to_delete_1", url = "https://mozilla.org", private = false)
        val normalTabToDelete2 = createTab(id = "normal_to_delete_2", url = "https://mozilla.com", private = false)

        val itemsToDelete = listOf(TabsTrayItem.Tab(normalTabToDelete1), TabsTrayItem.Tab(normalTabToDelete2))

        every { browserStore.state } returns BrowserState(
            tabs = listOf(normalTabToDelete1, normalTabToDelete2),
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            normalTabsState = TabsTrayState.NormalTabsState(
                items = itemsToDelete,
            ),
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = emptyList()),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = emptyList()),
            tabGroupState = TabsTrayState.TabGroupState(groups = emptyList()),
        )

        val controller = spyk(createController())

        controller.deleteMultipleTabs(tabs = itemsToDelete)

        verify { controller.dismissTabManagerAndNavigateHome(ALL_NORMAL_TABS) }

        verify(exactly = 0) { tabsUseCases.removeTabs(any(), any()) }
    }

    @Test
    fun `GIVEN inactive tabs and a closed group WHEN deleteMultipleTabs deletes all visible normal tabs THEN it navigates to home`() {
        val visibleNormalTabToDelete = createTab(id = "visible_to_delete", url = "https://mozilla.org", private = false)
        val inactiveTab = createTab(id = "inactive_tab", url = "https://mozilla.com", private = false)
        val tabInsideClosedGroup = createTab(id = "tab_in_closed_group", url = "https://mozilla.org", private = false)

        val itemsToDelete = listOf(TabsTrayItem.Tab(visibleNormalTabToDelete))

        val closedTabGroup = TabsTrayItem.TabGroup(
            id = "group1",
            title = "Closed Group",
            theme = TabGroupTheme.default,
            tabs = mutableListOf(TabsTrayItem.Tab(tabInsideClosedGroup)),
            closed = true,
        )

        every { browserStore.state } returns BrowserState(
            tabs = listOf(visibleNormalTabToDelete, inactiveTab, tabInsideClosedGroup),
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            normalTabsState = TabsTrayState.NormalTabsState(items = itemsToDelete),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = listOf(TabsTrayItem.Tab(inactiveTab))),
            tabGroupState = TabsTrayState.TabGroupState(groups = listOf(closedTabGroup)),
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = emptyList()),
        )

        val controller = spyk(createController())

        controller.deleteMultipleTabs(tabs = itemsToDelete)

        verify { controller.dismissTabManagerAndNavigateHome(ALL_NORMAL_TABS) }

        verify(exactly = 0) { tabsUseCases.removeTabs(any(), any()) }
    }

    @Test
    fun `GIVEN last private tab WHEN handleDeleteTabWarningAccepted is called THEN it bypasses the download warning and navigates to home`() {
        val tabToDelete = createTab(id = "private_last", url = "https://mozilla.org", private = true)

        every { browserStore.state } returns BrowserState(
            tabs = listOf(tabToDelete),
            selectedTabId = "private_last",
            downloads = emptyMap(),
        )

        every { trayStore.state } returns TabsTrayState(
            selectedTabId = "private_last",
            privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = listOf(TabsTrayItem.Tab(tabToDelete))),
            normalTabsState = TabsTrayState.NormalTabsState(items = emptyList()),
            inactiveTabs = TabsTrayState.InactiveTabsState(tabs = emptyList()),
            tabGroupState = TabsTrayState.TabGroupState(groups = emptyList()),
        )

        val controller = spyk(createController())

        controller.handleDeletePrivateTabWarningAccepted(tabId = "private_last")

        verify { controller.dismissTabManagerAndNavigateHome("private_last") }

        verify(exactly = 0) { tabsUseCases.removeTab(tabId = any<String>(), excludedTabIds = any<Set<String>>()) }
    }

    @Test
    fun `WHEN onCloseAllPrivateTabsWarningConfirmed is called THEN it bypasses the download warning and navigates to home`() {
        val controller = spyk(createController())

        controller.onCloseAllPrivateTabsWarningConfirmed(private = true)

        verify { controller.dismissTabManagerAndNavigateHome(ALL_PRIVATE_TABS) }
    }

    @Test
    fun `GIVEN select mode WHEN handleBackPressed is called THEN exit select mode and return true`() {
        every { trayStore.state.mode } returns TabsTrayState.Mode.Select()

        val controller = createController()
        val result = controller.handleBackPressed()

        assertTrue(result)
        verify { trayStore.dispatch(TabsTrayAction.ExitSelectMode) }
    }

    @Test
    fun `GIVEN normal mode WHEN handleBackPressed is called THEN return false and do nothing`() {
        every { trayStore.state.mode } returns TabsTrayState.Mode.Normal

        val controller = createController()
        val result = controller.handleBackPressed()

        assertFalse(result)
        verify(exactly = 0) { trayStore.dispatch(TabsTrayAction.ExitSelectMode) }
    }

    @Test
    fun `GIVEN one tab is selected WHEN the delete selected tabs button is clicked THEN report the telemetry and delete the tabs`() {
        val controller = spyk(createController())

        every { trayStore.state.mode.selectedTabs } returns setOf(TabsTrayItem.Tab(tab = createTab(url = "url")))
        every { controller.deleteMultipleTabs(any()) } just runs

        controller.handleDeleteSelectedTabsClicked()

        assertNotNull(TabsTray.closeSelectedTabs.testGetValue())
        val snapshot = TabsTray.closeSelectedTabs.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals("1", snapshot.single().extra?.getValue("tab_count"))

        verify { trayStore.dispatch(TabsTrayAction.ExitSelectMode) }
    }

    @Test
    fun `GIVEN private mode selected WHEN sendNewTabEvent is called THEN NewPrivateTabTapped is tracked in telemetry`() {
        createController().sendNewTabEvent(true)

        assertNotNull(TabsTray.newPrivateTabTapped.testGetValue())
    }

    @Test
    fun `GIVEN normal mode selected WHEN sendNewTabEvent is called THEN NewTabTapped is tracked in telemetry`() {
        assertNull(TabsTray.newTabTapped.testGetValue())

        createController().sendNewTabEvent(false)

        assertNotNull(TabsTray.newTabTapped.testGetValue())
    }

    @Test
    fun `WHEN dismissTabManagerAndNavigateHome is called with a specific tab id THEN navigates home is opened to delete that tab`() {
        var navigateToHomeAndDeleteSessionInvoked = false
        createController(
            navigateToHomeAndDeleteSession = {
                assertEquals("randomId", it)
                navigateToHomeAndDeleteSessionInvoked = true
            },
        ).dismissTabManagerAndNavigateHome("randomId")

        assertTrue(navigateToHomeAndDeleteSessionInvoked)
    }

    @Test
    fun `WHEN a synced tab is clicked THEN the metrics are reported and the tab is opened`() {
        val tab = mockk<Tab>()
        val entry = mockk<TabEntry>()
        assertNull(Events.syncedTabOpened.testGetValue())

        every { tab.active() }.answers { entry }
        every { entry.url }.answers { "https://mozilla.org" }

        val appStore = AppStore(initialState = AppState(mode = BrowsingMode.Normal))
        fenixBrowserUseCases = FenixBrowserUseCases(
            appStore = appStore,
            tabsUseCases = tabsUseCases,
            loadUrlUseCase = loadUrlUseCase,
            searchUseCases = searchUseCases,
            homepageTitle = homepageTitle,
            profiler = profiler,
        )
        every { testContext.components.useCases.fenixBrowserUseCases } returns fenixBrowserUseCases

        createController().handleSyncedTabClicked(tab)

        assertNotNull(Events.syncedTabOpened.testGetValue())

        val url = "https://mozilla.org"

        verify {
            tabsUseCases.addTab.invoke(
                url = url,
                flags = EngineSession.LoadUrlFlags.none(),
                private = false,
                historyMetadata = null,
                originalInput = url,
            )
        }
    }

    @Test
    fun `WHEN a synced tab is closed THEN a command to close the tab is queued AND an undo snackbar is shown`() {
        var showUndoSnackbarForSyncedTabInvoked = false
        val controller = createController(
            showUndoSnackbarForSyncedTab = {
                showUndoSnackbarForSyncedTabInvoked = true
            },
        )

        val tab = Tab(
            history = listOf(TabEntry(title = "Get Firefox", url = "https://getfirefox.com", iconUrl = null)),
            active = 0,
            lastUsed = 0,
            inactive = false,
        )
        controller.handleSyncedTabClosed("1234", tab)
        testDispatcher.scheduler.advanceUntilIdle()

        coVerify(exactly = 1) { closeSyncedTabsUseCases.close("1234", any()) }
        assertTrue(showUndoSnackbarForSyncedTabInvoked)
    }

    @Test
    fun `GIVEN no tabs selected and the user is not in multi select mode WHEN the user long taps a tab THEN that tab will become selected`() {
        every { browserStore.state } returns mockk {
            every { tabs } returns emptyList()
            every { selectedTabId } returns null
        }
        trayStore = TabsTrayStore()
        val controller = spyk(createController())
        val tab1 = TabsTrayItem.Tab(
            tab = createTab(
                id = "1",
                url = "www.mozilla.com",
            ),
        )
        val tab2 = TabsTrayItem.Tab(
            tab = createTab(
                id = "1",
                url = "www.google.com",
            ),
        )
        trayStore.dispatch(TabsTrayAction.ExitSelectMode)

        controller.handleTabSelected(tab1, "Tab Manager")
        verify(exactly = 1) { controller.handleTabSelected(tab1, "Tab Manager") }

        controller.handleTabSelected(tab2, "Tab Manager")
        verify(exactly = 1) { controller.handleTabSelected(tab2, "Tab Manager") }
    }

    @Test
    fun `GIVEN the user is in multi select mode and a tab is selected WHEN the user taps the selected tab THEN the tab will become unselected`() {
        val middleware = CaptureActionsMiddleware<TabsTrayState, TabsTrayAction>()
        trayStore = TabsTrayStore(middlewares = listOf(middleware))
        val tab1 = TabsTrayItem.Tab(tab = createTab(id = "1", url = "www.mozilla.com"))
        val tab2 = TabsTrayItem.Tab(tab = createTab(id = "2", url = "www.google.com"))
        val controller = createController()
        trayStore.dispatch(TabsTrayAction.EnterSelectMode)
        trayStore.dispatch(TabsTrayAction.AddSelectTab(tab1))
        trayStore.dispatch(TabsTrayAction.AddSelectTab(tab2))

        controller.handleTabSelected(tab1, "Tab Manager")
        middleware.assertLastAction(TabsTrayAction.RemoveSelectTab::class) {
            assertEquals(tab1, it.tab)
        }

        controller.handleTabSelected(tab2, "Tab Manager")
        middleware.assertLastAction(TabsTrayAction.RemoveSelectTab::class) {
            assertEquals(tab2, it.tab)
        }
    }

    @Test
    fun `GIVEN at least a tab is selected and the user is in multi select mode WHEN the user taps a tab THEN that tab will become selected`() {
        val middleware = CaptureActionsMiddleware<TabsTrayState, TabsTrayAction>()
        trayStore = TabsTrayStore(middlewares = listOf(middleware))
        trayStore.dispatch(TabsTrayAction.EnterSelectMode)
        val controller = createController()
        val tab1 = TabsTrayItem.Tab(tab = createTab(id = "1", url = "www.mozilla.com"))
        val tab2 = TabsTrayItem.Tab(tab = createTab(id = "2", url = "www.google.com"))

        trayStore.dispatch(TabsTrayAction.EnterSelectMode)
        trayStore.dispatch(TabsTrayAction.AddSelectTab(tab1))

        controller.handleTabSelected(tab2, "Tab Manager")

        middleware.assertLastAction(TabsTrayAction.AddSelectTab::class) {
            assertEquals(tab2, it.tab)
        }
    }

    @Test
    fun `GIVEN at least a tab is selected and the user is in multi select mode WHEN the user taps an inactive tab THEN that tab will not be selected`() {
        val middleware = CaptureActionsMiddleware<TabsTrayState, TabsTrayAction>()
        trayStore = TabsTrayStore(middlewares = listOf(middleware))
        trayStore.dispatch(TabsTrayAction.EnterSelectMode)
        val controller = spyk(createController())
        val normalTab = TabsTrayItem.Tab(
            tab = createTab(
                id = "1",
                url = "www.mozilla.com",
            ),
        )
        val inactiveTab = TabsTrayItem.Tab(
            tab = createTab(
                id = "1",
                url = "www.google.com",
            ),
        )

        trayStore.dispatch(TabsTrayAction.EnterSelectMode)
        trayStore.dispatch(TabsTrayAction.AddSelectTab(normalTab))

        controller.handleTabSelected(inactiveTab, INACTIVE_TABS_FEATURE_NAME)

        middleware.assertLastAction(TabsTrayAction.AddSelectTab::class) {
            assertEquals(normalTab, it.tab)
        }
    }

    @Test
    fun `GIVEN the user selects only the current tab WHEN the user forces tab to be inactive THEN tab does not become inactive`() {
        val currentTabData = createTab(id = "currentTab", url = "", createdAt = 11L)
        val secondTabData = createTab(id = "secondTab", url = "", createdAt = 22L)
        val currentTab = TabsTrayItem.Tab(tab = currentTabData)
        val secondTab = TabsTrayItem.Tab(tab = secondTabData)
        browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTabData, secondTabData),
                selectedTabId = currentTabData.id,
            ),
        )

        every { trayStore.state.mode.selectedTabs } returns setOf(currentTab)

        createController().handleForceSelectedTabsAsInactiveClicked(numDays = 5)

        val updatedCurrentTab = browserStore.state.tabs.first { it.id == currentTab.id }
        assertEquals(updatedCurrentTab, currentTabData)
        val updatedSecondTab = browserStore.state.tabs.first { it.id == secondTab.id }
        assertEquals(updatedSecondTab, secondTabData)
    }

    @Test
    fun `GIVEN the user selects multiple tabs including the current tab WHEN the user forces them all to be inactive THEN all but current tab become inactive`() {
        val currentTabData = createTab(id = "currentTab", url = "", createdAt = 11L)
        val secondTabData = createTab(id = "secondTab", url = "", createdAt = 22L)
        val currentTab = TabsTrayItem.Tab(tab = currentTabData)
        val secondTab = TabsTrayItem.Tab(tab = secondTabData)
        browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTabData, secondTabData),
                selectedTabId = currentTab.id,
            ),
        )

        every { trayStore.state.mode.selectedTabs } returns setOf(currentTab, secondTab)

        createController().handleForceSelectedTabsAsInactiveClicked(numDays = 5)

        val updatedCurrentTab = browserStore.state.tabs.first { it.id == currentTab.id }
        assertEquals(updatedCurrentTab, currentTabData)
        val updatedSecondTab = browserStore.state.tabs.first { it.id == secondTab.id }
        assertNotEquals(updatedSecondTab, secondTabData)
        val expectedTime = System.currentTimeMillis() - TimeUnit.DAYS.toMillis(5)
        // Account for System.currentTimeMillis() giving different values in test vs the system under test
        // and also for the waitUntilIdle to block for even hundreds of milliseconds.
        assertTrue(updatedSecondTab.lastAccess in (expectedTime - 5000)..expectedTime)
        assertTrue(updatedSecondTab.createdAt in (expectedTime - 5000)..expectedTime)
    }

    @Test
    fun `GIVEN no value is provided for inactive days WHEN forcing tabs as inactive THEN set their last active time 15 days ago and exit multi selection`() {
        val controller = spyk(createController())
        every { trayStore.state.mode.selectedTabs } returns setOf(TabsTrayItem.Tab(tab = createTab(url = "https://mozilla.org")))
        every { browserStore.state.selectedTabId } returns "test"

        controller.handleForceSelectedTabsAsInactiveClicked()

        verify { controller.handleForceSelectedTabsAsInactiveClicked(numDays = 15L) }

        verify { trayStore.dispatch(TabsTrayAction.ExitSelectMode) }
    }

    fun `WHEN the inactive tabs section is expanded THEN the expanded telemetry event should be reported`() {
        val controller = createController()

        assertNull(TabsTray.inactiveTabsExpanded.testGetValue())
        assertNull(TabsTray.inactiveTabsCollapsed.testGetValue())

        controller.handleInactiveTabsHeaderClicked(expanded = true)

        assertNotNull(TabsTray.inactiveTabsExpanded.testGetValue())
        assertNull(TabsTray.inactiveTabsCollapsed.testGetValue())
    }

    @Test
    fun `WHEN the inactive tabs section is collapsed THEN the collapsed telemetry event should be reported`() {
        val controller = createController()

        assertNull(TabsTray.inactiveTabsExpanded.testGetValue())
        assertNull(TabsTray.inactiveTabsCollapsed.testGetValue())

        controller.handleInactiveTabsHeaderClicked(expanded = false)

        assertNull(TabsTray.inactiveTabsExpanded.testGetValue())
        assertNotNull(TabsTray.inactiveTabsCollapsed.testGetValue())
    }

    @Test
    fun `WHEN the inactive tabs auto-close feature prompt is dismissed THEN update settings and report the telemetry event`() {
        val controller = createController()

        assertNull(TabsTray.autoCloseDimissed.testGetValue())

        controller.handleInactiveTabsAutoCloseDialogDismiss()

        assertNotNull(TabsTray.autoCloseDimissed.testGetValue())
        verify { settings.hasInactiveTabsAutoCloseDialogBeenDismissed = true }
    }

    @Test
    fun `WHEN the inactive tabs auto-close feature prompt is accepted THEN update settings and report the telemetry event`() {
        val controller = createController()

        assertNull(TabsTray.autoCloseTurnOnClicked.testGetValue())

        controller.handleEnableInactiveTabsAutoCloseClicked()

        assertNotNull(TabsTray.autoCloseTurnOnClicked.testGetValue())

        verify { settings.closeTabsAfterOneMonth = true }
        verify { settings.closeTabsAfterOneWeek = false }
        verify { settings.closeTabsAfterOneDay = false }
        verify { settings.manuallyCloseTabs = false }
        verify { settings.hasInactiveTabsAutoCloseDialogBeenDismissed = true }
    }

    @Test
    fun `WHEN an inactive tab is selected THEN report the telemetry event and open the tab`() {
        val controller = spyk(createController())
        val tab = TabsTrayItem.Tab(tab = createTab(url = ""))

        every { controller.handleTabSelected(any(), any()) } just runs

        assertNull(TabsTray.openInactiveTab.testGetValue())

        controller.handleInactiveTabClicked(tab)

        assertNotNull(TabsTray.openInactiveTab.testGetValue())

        verify { controller.handleTabSelected(tab, INACTIVE_TABS_FEATURE_NAME) }
    }

    @Test
    fun `WHEN an inactive tab is closed THEN report the telemetry event and delete the tab`() {
        val controller = spyk(createController())
        val tab = TabsTrayItem.Tab(tab = createTab(url = ""))

        every { controller.handleTabDeletion(any(), any()) } just runs

        assertNull(TabsTray.closeInactiveTab.testGetValue())

        controller.handleCloseInactiveTabClicked(tab)

        assertNotNull(TabsTray.closeInactiveTab.testGetValue())

        verify { controller.handleTabDeletion(tab, INACTIVE_TABS_FEATURE_NAME) }
    }

    @Test
    fun `WHEN all inactive tabs are closed THEN perform the deletion and report the telemetry event and show a Snackbar`() {
        var showSnackbarInvoked = false
        val controller = createController(
            showUndoSnackbarForInactiveTab = {
                showSnackbarInvoked = true
            },
        )
        val inactiveTab: TabSessionState = mockk {
            every { lastAccess } returns maxActiveTime
            every { createdAt } returns 0
            every { id } returns "24"
            every { content } returns mockk {
                every { private } returns false
            }
        }

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(inactiveTab)
            every { selectedTabId } returns "24"
        }

        assertNull(TabsTray.closeAllInactiveTabs.testGetValue())

        controller.handleDeleteAllInactiveTabsClicked()

        verify { tabsUseCases.removeTabs(listOf("24")) }
        assertNotNull(TabsTray.closeAllInactiveTabs.testGetValue())
        assertTrue(showSnackbarInvoked)
    }

    @Test
    fun `WHEN a tab is selected THEN report the metric, update the state, and open the browser`() {
        trayStore = TabsTrayStore()
        val controller = spyk(createController())
        val tabData = createTab(url = "")
        val tab = TabsTrayItem.Tab(tab = tabData)
        val source = INACTIVE_TABS_FEATURE_NAME
        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tabData, testNormalTab, testNormalTab)
            every { selectedTabId } returns tab.id
        }

        every { controller.handleNavigateToBrowser() } just runs

        assertNull(TabsTray.openedExistingTab.testGetValue())

        controller.handleTabSelected(tab, source)

        assertNotNull(TabsTray.openedExistingTab.testGetValue())
        val snapshot = TabsTray.openedExistingTab.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals(source, snapshot.single().extra?.getValue("source"))

        verify { tabsUseCases.selectTab(tab.id) }
        verify { controller.handleNavigateToBrowser() }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN a homepage tab is selected THEN report the metric, update the state, and show the homepage`() {
        every { settings.enableHomepageAsNewTab } returns true
        trayStore = TabsTrayStore()
        val controller = spyk(createController())
        val tabData = createTab(url = ABOUT_HOME_URL)
        val tab = TabsTrayItem.Tab(tab = tabData)
        val source = "Tab Manager"
        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tabData, testNormalTab, testNormalTab)
            every { selectedTabId } returns tab.id
        }

        every { controller.handleNavigateToHome() } just runs

        assertNull(TabsTray.openedExistingTab.testGetValue())

        controller.handleTabSelected(tab, source)

        assertNotNull(TabsTray.openedExistingTab.testGetValue())
        val snapshot = TabsTray.openedExistingTab.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals(source, snapshot.single().extra?.getValue("source"))

        verify { tabsUseCases.selectTab(tab.id) }
        verify { controller.handleNavigateToHome() }
    }

    @Test
    fun `WHEN a tab is selected without a source THEN report the metric with an unknown source, update the state, and open the browser`() {
        trayStore = TabsTrayStore()
        val controller = spyk(createController())
        val tabData = createTab(url = "")
        val tab = TabsTrayItem.Tab(tab = tabData)
        val sourceText = "unknown"
        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tabData, testNormalTab, testNormalTab)
            every { selectedTabId } returns tab.id
        }

        every { controller.handleNavigateToBrowser() } just runs

        assertNull(TabsTray.openedExistingTab.testGetValue())

        controller.handleTabSelected(tab, null)

        assertNotNull(TabsTray.openedExistingTab.testGetValue())
        val snapshot = TabsTray.openedExistingTab.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals(sourceText, snapshot.single().extra?.getValue("source"))

        verify { tabsUseCases.selectTab(tab.id) }
        verify { controller.handleNavigateToBrowser() }
    }

    @Test
    fun `GIVEN a normal tab is selected WHEN the last private tab is deleted THEN that private tab is removed and an undo snackbar is shown and original normal tab is still displayed`() {
        val currentTabData = createTab(
            id = "normalTab",
            url = "https://simulate.com",
        )
        val currentTab = TabsTrayItem.Tab(tab = currentTabData)
        val privateTabData = createTab(
            id = "privateTab",
            url = "https://mozilla.com",
            private = true,
        )
        var showUndoSnackbarForTabInvoked = false
        var navigateToHomeAndDeleteSessionInvoked = false
        trayStore = TabsTrayStore()
        browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTabData, privateTabData),
                selectedTabId = currentTab.id,
            ),
        )

        val controller = createController(
            showUndoSnackbarForTab = {
                showUndoSnackbarForTabInvoked = true
            },
            navigateToHomeAndDeleteSession = {
                navigateToHomeAndDeleteSessionInvoked = true
            },
        )

        controller.handleTabSelected(currentTab, "source")
        controller.handleTabDeletion(TabsTrayItem.Tab(tab = privateTabData))

        assertTrue(showUndoSnackbarForTabInvoked)
        assertFalse(navigateToHomeAndDeleteSessionInvoked)
    }

    @Test
    fun `GIVEN one tab is selected WHEN the share button is clicked THEN report telemetry and invoke the share use case`() {
        val tab = createTab(url = "https://mozilla.org", title = "Mozilla")
        every { trayStore.state.mode.selectedTabs } returns setOf(TabsTrayItem.Tab(tab = tab))

        createController().handleShareSelectedTabsClicked()

        verify {
            shareUseCases.shareItems(
                items = listOf(ShareData(url = tab.content.url, title = tab.content.title)),
                source = ShareSource.TABS_TRAY,
                isPrivate = false,
                navigateToShareFragment = any(),
            )
        }

        assertNotNull(TabsTray.shareSelectedTabs.testGetValue())
        val snapshot = TabsTray.shareSelectedTabs.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals("1", snapshot.single().extra?.getValue("tab_count"))
    }

    @Test
    fun `GIVEN multiple tabs are selected WHEN the share button is clicked THEN invoke the share use case with all tabs`() {
        val tab1 = createTab(url = "https://mozilla.org", title = "Mozilla")
        val tab2 = createTab(url = "https://firefox.com", title = "Firefox")
        val tabs = setOf(TabsTrayItem.Tab(tab = tab1), TabsTrayItem.Tab(tab = tab2))
        every { trayStore.state.mode.selectedTabs } returns tabs

        createController().handleShareSelectedTabsClicked()

        verify {
            shareUseCases.shareItems(
                items = listOf(
                    ShareData(url = tab1.content.url, title = tab1.content.title),
                    ShareData(url = tab2.content.url, title = tab2.content.title),
                ),
                source = ShareSource.TABS_TRAY,
                isPrivate = false,
                navigateToShareFragment = any(),
            )
        }

        val snapshot = TabsTray.shareSelectedTabs.testGetValue()!!
        assertEquals("2", snapshot.single().extra?.getValue("tab_count"))
    }

    @Test
    fun `GIVEN one tab is selected WHEN the add selected tabs to collection button is clicked THEN report the telemetry and show the collections dialog`() {
        val controller = spyk(createController())
        every { controller.showCollectionsDialog(any()) } just runs

        every { trayStore.state.mode.selectedTabs } returns setOf(TabsTrayItem.Tab(tab = createTab(url = "https://mozilla.org")))
        every { controller.showCollectionsDialog(any()) } answers { }

        assertNull(TabsTray.saveToCollection.testGetValue())

        controller.handleAddSelectedTabsToCollectionClicked()

        assertNotNull(TabsTray.saveToCollection.testGetValue())
    }

    @Test
    fun `GIVEN one tab selected WHEN saving selected tabs to bookmarks THEN add bookmark use case is invoked once and snackbar is shown`() =
        runTest(testDispatcher) {
            var showBookmarkSnackbarInvoked = false
            val parentNode = makeBookmarkFolder(guid = BookmarkRoot.Mobile.id)
            coEvery { addBookmarkUseCase.invoke(any(), any(), any(), any()) } returns
                BookmarksUseCase.AddBookmarksUseCase.Result(guidToEdit = "guid", parentNode = parentNode)
            every { trayStore.state.mode.selectedTabs } returns setOf(TabsTrayItem.Tab(tab = createTab(url = "https://mozilla.org")))

            createController(
                showBookmarkSnackbar = { _, _ ->
                    showBookmarkSnackbarInvoked = true
                },
            ).handleBookmarkSelectedTabsClicked()
            testDispatcher.scheduler.advanceUntilIdle()

            verify { trayStore.dispatch(TabsTrayAction.BookmarkSelectedTabs(1)) }
            coVerify(exactly = 1) { addBookmarkUseCase.invoke(url = "https://mozilla.org", title = any()) }
            assertTrue(showBookmarkSnackbarInvoked)
        }

    @Test
    fun `GIVEN multiple tabs selected WHEN saving selected tabs to bookmarks THEN add bookmark use case is invoked once per tab and snackbar is shown`() =
        runTest(testDispatcher) {
            var showBookmarkSnackbarInvoked = false
            val parentNode = makeBookmarkFolder(guid = BookmarkRoot.Mobile.id)
            coEvery { addBookmarkUseCase.invoke(any(), any(), any(), any()) } returns
                BookmarksUseCase.AddBookmarksUseCase.Result(guidToEdit = "guid", parentNode = parentNode)
            every { trayStore.state.mode.selectedTabs } returns setOf(
                TabsTrayItem.Tab(tab = createTab(url = "https://mozilla.org")),
                TabsTrayItem.Tab(tab = createTab(url = "https://mozilla2.org")),
            )

            createController(
                showBookmarkSnackbar = { _, _ ->
                    showBookmarkSnackbarInvoked = true
                },
            ).handleBookmarkSelectedTabsClicked()
            testDispatcher.scheduler.advanceUntilIdle()

            verify { trayStore.dispatch(TabsTrayAction.BookmarkSelectedTabs(2)) }
            coVerify(exactly = 2) { addBookmarkUseCase.invoke(any(), any(), any(), any()) }
            assertTrue(showBookmarkSnackbarInvoked)
        }

    @Test
    fun `GIVEN active page is not normal tabs WHEN the normal tabs page button is clicked THEN report the metric`() {
        every { trayStore.state.selectedPage } returns Page.PrivateTabs

        assertNull(TabsTray.normalModeTapped.testGetValue())

        createController().handleTabPageClicked(Page.NormalTabs)

        assertNotNull(TabsTray.normalModeTapped.testGetValue())
    }

    @Test
    fun `GIVEN active page is normal tabs WHEN normal tabs page button is clicked THEN do not report the metric`() {
        every { trayStore.state.selectedPage } returns Page.NormalTabs

        assertNull(TabsTray.normalModeTapped.testGetValue())

        createController().handleTabPageClicked(Page.NormalTabs)

        assertNull(TabsTray.normalModeTapped.testGetValue())
    }

    @Test
    fun `GIVEN active page is not private tabs WHEN the private tabs page button is clicked THEN report the metric`() {
        every { trayStore.state.selectedPage } returns Page.NormalTabs

        assertNull(TabsTray.privateModeTapped.testGetValue())

        createController().handleTabPageClicked(Page.PrivateTabs)

        assertNotNull(TabsTray.privateModeTapped.testGetValue())
    }

    @Test
    fun `GIVEN active page is private tabs WHEN the private tabs button is clicked THEN do not report the metric`() {
        every { trayStore.state.selectedPage } returns Page.PrivateTabs

        assertNull(TabsTray.privateModeTapped.testGetValue())

        createController().handleTabPageClicked(Page.PrivateTabs)

        assertNull(TabsTray.privateModeTapped.testGetValue())
    }

    @Test
    fun `GIVEN active page is not synced tabs WHEN the synced tabs page button is clicked THEN report the metric`() {
        every { trayStore.state.selectedPage } returns Page.NormalTabs

        assertNull(TabsTray.syncedModeTapped.testGetValue())

        createController().handleTabPageClicked(Page.SyncedTabs)

        assertNotNull(TabsTray.syncedModeTapped.testGetValue())
    }

    @Test
    fun `GIVEN active page is synced tabs WHEN the synced tabs page button is clicked THEN do not report the metric`() {
        every { trayStore.state.selectedPage } returns Page.SyncedTabs

        assertNull(TabsTray.syncedModeTapped.testGetValue())

        createController().handleTabPageClicked(Page.SyncedTabs)

        assertNull(TabsTray.syncedModeTapped.testGetValue())
    }

    @Test
    fun `WHEN the sign into Sync button is clicked THEN navigate the user to the sign into Sync flow`() {
        createController().handleSignInClicked()

        verify {
            navController.navigate(
                TabManagementFragmentDirections.actionGlobalTurnOnSync(
                    entrypoint = FenixFxAEntryPoint.SyncedTabsMenu,
                ),
            )
        }
    }

    @Test
    fun `GIVEN logged in state WHEN account settings is clicked THEN navigate to account settings`() {
        every { accountManager.authenticatedAccount() }.answers { mockk(relaxed = true) }

        createController().onAccountSettingsClicked()

        verify(exactly = 1) { navController.navigate(TabManagementFragmentDirections.actionGlobalAccountSettingsFragment()) }
    }

    @Test
    fun `GIVEN logged out state WHEN account settings is clicked THEN navigate to turn on sync`() {
        every { accountManager.authenticatedAccount() }.answers { null }

        createController().onAccountSettingsClicked()

        verify(exactly = 1) {
            navController.navigate(
                TabManagementFragmentDirections.actionGlobalTurnOnSync(
                    entrypoint = FenixFxAEntryPoint.NavigationInteraction,
                ),
            )
        }
    }

    @Test
    fun `WHEN tab settings is clicked THEN navigate to global tab settings`() {
        createController().onTabSettingsClicked()
        verify(exactly = 1) { navController.navigate(TabManagementFragmentDirections.actionGlobalTabSettingsFragment()) }
    }

    @Test
    fun `GIVEN no open recently closed tabs WHEN open recently closed tabs clicked THEN navigate to recently closed tabs`() {
        assertNull(Events.recentlyClosedTabsOpened.testGetValue())

        createController().onOpenRecentlyClosedClicked()

        verify(exactly = 1) { navController.navigate(TabManagementFragmentDirections.actionGlobalRecentlyClosed()) }
        assertNotNull(Events.recentlyClosedTabsOpened.testGetValue())
    }

    @Test
    fun `GIVEN public tabs and one download in progress WHEN close all tabs clicked THEN dismiss tab manager and navigate to home`() {
        val tab: TabSessionState = mockk { every { content.private } returns false }
        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab)
        }
        every { browserStore.state.downloads } returns mapOf(
            "1" to DownloadState(
                "https://mozilla.org/download",
                private = false,
                status = DownloadState.Status.DOWNLOADING,
            ),
        )

        val controller = spyk(createController())
        controller.onCloseAllTabsClicked(private = false)

        verify { controller.dismissTabManagerAndNavigateHome(any()) }
    }

    @Test
    fun `GIVEN private tabs and 1 download in progress WHEN close all tabs clicked THEN dismiss tab manager and navigate to home`() {
        val tab: TabSessionState = mockk { every { content.private } returns true }
        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab)
        }
        every { browserStore.state.downloads } returns mapOf(
            "1" to DownloadState(
                "https://mozilla.org/download",
                private = true,
                status = DownloadState.Status.DOWNLOADING,
            ),
        )

        val controller = spyk(createController())
        controller.onCloseAllTabsClicked(private = false)

        verify { controller.dismissTabManagerAndNavigateHome(any()) }
    }

    @Test
    fun `GIVEN active private download WHEN onCloseAllTabsClicked is called for private tabs THEN showCancelledDownloadWarning is called`() {
        var showCancelledDownloadWarningInvoked = false
        val tab: TabSessionState = mockk { every { content.private } returns true }
        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab)
        }
        every { browserStore.state.downloads } returns mapOf(
            "1" to DownloadState(
                "https://mozilla.org/download",
                private = true,
                status = DownloadState.Status.DOWNLOADING,
            ),
        )

        createController(
            showCancelledDownloadWarning = { _, _, _ ->
                showCancelledDownloadWarningInvoked = true
            },
        ).onCloseAllTabsClicked(true)

        assertTrue(showCancelledDownloadWarningInvoked)
    }

    @Test
    fun `GIVEN no active private download WHEN onCloseAllTabsClicked is called for private tabs THEN showCancelledDownloadWarning is not called`() {
        var showCancelledDownloadWarningInvoked = false
        val tab: TabSessionState = mockk { every { content.private } returns true }
        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab)
        }
        every { browserStore.state.downloads } returns emptyMap()

        createController(
            showCancelledDownloadWarning = { _, _, _ ->
                showCancelledDownloadWarningInvoked = true
            },
        ).onCloseAllTabsClicked(true)

        assertFalse(showCancelledDownloadWarningInvoked)
    }

    @Test
    fun `GIVEN no active download WHEN onCloseAllTabsClicked is called for public tabs THEN showCancelledDownloadWarning is not called`() {
        var showCancelledDownloadWarningInvoked = false
        val tab: TabSessionState = mockk { every { content.private } returns false }
        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab)
        }
        every { browserStore.state.downloads } returns mapOf(
            "1" to DownloadState(
                "https://mozilla.org/download",
                private = false,
                status = DownloadState.Status.DOWNLOADING,
            ),
        )

        createController(
            showCancelledDownloadWarning = { _, _, _ ->
                showCancelledDownloadWarningInvoked = true
            },
        ).onCloseAllTabsClicked(true)

        assertFalse(showCancelledDownloadWarningInvoked)
    }

    @Test
    fun `GIVEN active download WHEN onCloseAllTabsClicked is called for public tabs THEN showCancelledDownloadWarning is not called`() {
        var showCancelledDownloadWarningInvoked = false
        val tab: TabSessionState = mockk { every { content.private } returns false }
        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab)
        }
        every { browserStore.state.downloads } returns emptyMap()

        createController(
            showCancelledDownloadWarning = { _, _, _ ->
                showCancelledDownloadWarningInvoked = true
            },
        ).onCloseAllTabsClicked(true)

        assertFalse(showCancelledDownloadWarningInvoked)
    }

    @Test
    fun `GIVEN selected tab is home page WHEN navigation is called THEN user navigates to home`() {
        every { navController.currentDestination?.id } returns R.id.browserFragment
        every { navController.popBackStack(R.id.homeFragment, false) } returns false
        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(testNormalTab, testHomeTab)
            every { selectedTabId } returns testHomeTab.id
        }

        createController().handleNavigationRequested()

        verify { navController.navigate(TabManagementFragmentDirections.actionGlobalHome()) }
    }

    @Test
    fun `GIVEN selected tab is not home page WHEN navigation is called THEN user navigates to browser`() {
        every { navController.currentDestination?.id } returns R.id.homeFragment
        every { navController.popBackStack(R.id.browserFragment, false) } returns false
        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(testNormalTab, testHomeTab)
            every { selectedTabId } returns testNormalTab.id
        }

        createController().handleNavigationRequested()

        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `WHEN the privacy report pill is tapped THEN navigate to the protections dashboard`() {
        every { navController.currentDestination } returns mockk<NavDestination> {
            every { id } returns R.id.tabManagementFragment
        }
        val currentSessionId = "test"
        every { browserStore.state } returns mockk {
            every { selectedTabId } returns currentSessionId
        }

        createController().onPrivacyReportTapped()

        verify {
            navController.navigate(
                directions = TabManagementFragmentDirections.actionTabManagementFragmentToGlobalProtectionsDashboard(
                    currentSessionId,
                ),
                navOptions = null,
            )
        }
    }

    private fun makeBookmarkFolder(guid: String) = BookmarkNode(
        type = BookmarkNodeType.FOLDER,
        parentGuid = BookmarkRoot.Mobile.id,
        guid = guid,
        position = 42U,
        title = "title",
        url = "url",
        dateAdded = 0L,
        lastModified = 0L,
        children = null,
    )

    private fun createController(
        navigateToHomeAndDeleteSession: (String) -> Unit = { },
        showUndoSnackbarForTab: (Boolean) -> Unit = { _ -> },
        showUndoSnackbarForInactiveTab: (Int) -> Unit = { _ -> },
        showUndoSnackbarForSyncedTab: (CloseTabsUseCases.UndoableOperation) -> Unit = { _ -> },
        showCancelledDownloadWarning: (Int, String?, String?) -> Unit = { _, _, _ -> },
        showCollectionSnackbar: (Int, Boolean) -> Unit = { _, _ -> },
        showBookmarkSnackbar: (Int, String?) -> Unit = { _, _ -> },
    ): DefaultTabManagerController {
        return DefaultTabManagerController(
            accountManager = accountManager,
            context = context,
            appStore = appStore,
            tabsTrayStore = trayStore,
            browserStore = browserStore,
            settings = settings,
            browsingModeManager = browsingModeManager,
            navController = navController,
            navigateToHomeAndDeleteSession = navigateToHomeAndDeleteSession,
            profiler = profiler,
            tabsUseCases = tabsUseCases,
            fenixBrowserUseCases = fenixBrowserUseCases,
            shareUseCases = shareUseCases,
            closeSyncedTabsUseCases = closeSyncedTabsUseCases,
            addBookmarkUseCase = addBookmarkUseCase,
            ioDispatcher = testDispatcher,
            mainDispatcher = testDispatcher,
            collectionStorage = collectionStorage,
            showUndoSnackbarForTab = showUndoSnackbarForTab,
            showUndoSnackbarForInactiveTab = showUndoSnackbarForInactiveTab,
            showUndoSnackbarForSyncedTab = showUndoSnackbarForSyncedTab,
            showCancelledDownloadWarning = showCancelledDownloadWarning,
            showBookmarkSnackbar = showBookmarkSnackbar,
            showCollectionSnackbar = showCollectionSnackbar,
        )
    }

    companion object {
        private const val PROFILER_START_TIME = Double.MAX_VALUE
    }
}
