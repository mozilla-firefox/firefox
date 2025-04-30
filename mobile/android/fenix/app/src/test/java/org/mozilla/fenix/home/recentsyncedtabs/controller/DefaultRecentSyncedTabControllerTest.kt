/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.recentsyncedtabs.controller

import androidx.navigation.NavController
import androidx.navigation.NavDirections
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.runs
import io.mockk.verify
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.sync.DeviceType
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.test.libstate.ext.waitUntilIdle
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.RecentSyncedTabs
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTab
import org.mozilla.fenix.tabstray.Page
import org.mozilla.fenix.tabstray.TabsTrayAccessPoint
import org.mozilla.fenix.utils.Settings

@RunWith(AndroidJUnit4::class)
class DefaultRecentSyncedTabControllerTest {

    @get:Rule
    val gleanTestRule = FenixGleanTestRule(testContext)

    private val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
    private val tabsUseCases: TabsUseCases = mockk()
    private val navController: NavController = mockk()
    private val appStore: AppStore = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private val accessPoint = TabsTrayAccessPoint.HomeRecentSyncedTab

    private lateinit var controller: RecentSyncedTabController

    @Before
    fun setup() {
        controller = DefaultRecentSyncedTabController(
            fenixBrowserUseCases = fenixBrowserUseCases,
            tabsUseCase = tabsUseCases,
            navController = navController,
            accessPoint = accessPoint,
            appStore = appStore,
            settings = settings,
        )
    }

    @Test
    fun `WHEN synced tab clicked THEN new tab added and navigate to browser`() {
        val url = "url"
        val nonSyncId = "different id"
        val tab = RecentSyncedTab(
            deviceDisplayName = "display",
            deviceType = DeviceType.DESKTOP,
            title = "title",
            url = url,
            previewImageUrl = null,
        )
        val store = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(
                    TabSessionState(
                        id = nonSyncId,
                        content = ContentState(url = "different url", private = false),
                    ),
                ),
                selectedTabId = nonSyncId,
            ),
        )
        val selectOrAddTabUseCase = TabsUseCases.SelectOrAddUseCase(store)

        every { tabsUseCases.selectOrAddTab } returns selectOrAddTabUseCase
        every { navController.navigate(any<Int>()) } just runs

        controller.handleRecentSyncedTabClick(tab)

        store.waitUntilIdle()
        assertNotEquals(nonSyncId, store.state.selectedTabId)
        assertEquals(2, store.state.tabs.size)
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN synced tab clicked THEN open synced tab in the existing tab`() {
        val url = "url"
        val tab = RecentSyncedTab(
            deviceDisplayName = "display",
            deviceType = DeviceType.DESKTOP,
            title = "title",
            url = url,
            previewImageUrl = null,
        )

        every { settings.enableHomepageAsNewTab } returns true
        every { navController.navigate(any<Int>()) } just runs

        controller.handleRecentSyncedTabClick(tab)

        verify {
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = url,
                newTab = false,
                private = false,
            )
            navController.navigate(R.id.browserFragment)
        }
    }

    @Test
    fun `GIVEN synced tab is already open WHEN clicked THEN tab is re-opened and browser navigated`() {
        val url = "url"
        val syncId = "id"
        val nonSyncId = "different id"
        val tab = RecentSyncedTab(
            deviceDisplayName = "display",
            deviceType = DeviceType.DESKTOP,
            title = "title",
            url = url,
            previewImageUrl = null,
        )
        val store = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(
                    TabSessionState(
                        id = syncId,
                        content = ContentState(url = url, private = false),
                    ),
                    TabSessionState(
                        id = nonSyncId,
                        content = ContentState(url = "different url", private = false),
                    ),
                ),
                selectedTabId = nonSyncId,
            ),
        )
        val selectOrAddTabUseCase = TabsUseCases.SelectOrAddUseCase(store)

        every { tabsUseCases.selectOrAddTab } returns selectOrAddTabUseCase
        every { navController.navigate(any<Int>()) } just runs

        controller.handleRecentSyncedTabClick(tab)

        store.waitUntilIdle()
        assertEquals(syncId, store.state.selectedTabId)
        assertEquals(2, store.state.tabs.size)
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `WHEN synced tab show all clicked THEN navigate to synced tabs tray`() {
        every { navController.navigate(any<NavDirections>()) } just runs

        controller.handleSyncedTabShowAllClicked()

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalTabsTrayFragment(
                    page = Page.SyncedTabs,
                    accessPoint = accessPoint,
                ),
            )
        }
    }

    @Test
    fun `WHEN synced tab clicked THEN metric counter labeled by device type is incremented`() {
        val url = "https://mozilla.org"
        val deviceType = DeviceType.DESKTOP
        val tab = RecentSyncedTab(
            deviceDisplayName = "display",
            deviceType = deviceType,
            title = "title",
            url = url,
            previewImageUrl = null,
        )

        every { tabsUseCases.selectOrAddTab } returns mockk(relaxed = true)
        every { navController.navigate(any<Int>()) } just runs

        controller.handleRecentSyncedTabClick(tab)

        assertEquals(1, RecentSyncedTabs.recentSyncedTabOpened["desktop"].testGetValue())
    }

    @Test
    fun `WHEN synced tab show all clicked THEN metric counter is incremented`() {
        every { navController.navigate(any<NavDirections>()) } just runs

        controller.handleSyncedTabShowAllClicked()

        assertEquals(1, RecentSyncedTabs.showAllSyncedTabsClicked.testGetValue())
    }

    @Test
    fun `WHEN synced tab is removed from homescreen THEN RemoveRecentSyncedTab action is dispatched`() {
        val tab = RecentSyncedTab(
            deviceDisplayName = "display",
            deviceType = DeviceType.DESKTOP,
            title = "title",
            url = "https://mozilla.org",
            previewImageUrl = null,
        )

        controller.handleRecentSyncedTabRemoved(tab)

        verify { appStore.dispatch(AppAction.RemoveRecentSyncedTab(tab)) }
    }
}
