/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.menu

import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.WebExtensionState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuAction
import mozilla.components.compose.menu.store.MenuStore
import mozilla.components.concept.engine.webextension.Action
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.feature.top.sites.TopSitesStorage
import mozilla.components.feature.top.sites.TopSitesUseCases
import mozilla.components.feature.webcompat.reporter.WebCompatReporterFeature.WEBCOMPAT_REPORTER_EXTENSION_ID
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import mozilla.telemetry.glean.testing.GleanTestRule
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.verifyNoInteractions
import org.mozilla.focus.GleanMetrics.Browser
import org.mozilla.focus.GleanMetrics.BrowserMenu
import org.mozilla.focus.GleanMetrics.CustomTabsToolbar
import org.mozilla.focus.GleanMetrics.Shortcuts
import org.mozilla.focus.state.AppState
import org.mozilla.focus.state.AppStore
import org.mozilla.focus.state.Screen
import org.robolectric.RobolectricTestRunner

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
class MenuMiddlewareTest {
    @get:Rule val gleanTestRule = GleanTestRule(testContext)

    private val menu: MenuItems = mock()
    private val sessionUseCases: SessionUseCases = mock()
    private val topSitesStorage: TopSitesStorage = mock()
    private val goBack: SessionUseCases.GoBackUseCase = mock()
    private val goForward: SessionUseCases.GoForwardUseCase = mock()
    private val reload: SessionUseCases.ReloadUrlUseCase = mock()
    private val stopLoading: SessionUseCases.StopLoadingUseCase = mock()
    private val tab = createTab("https://www.mozilla.org", id = "tab", title = "Mozilla")
    private val otherTab = createTab("https://example.com", id = "other")
    private val browserStore = BrowserStore(BrowserState(tabs = listOf(tab, otherTab), selectedTabId = otherTab.id))
    private val shortcut = TopSite.Pinned(1, "Mozilla", tab.content.url, null)
    private val appStore = AppStore(AppState(screen = Screen.Home(), topSites = listOf(shortcut)))
    private val groups = MutableStateFlow<List<MenuItemsGroup>>(emptyList())
    private val handled = mutableListOf<String>()
    private var dismissCount = 0
    private val callbacks =
        BrowserMenuCallbacks(
            shareCallback = { handled.add("share") },
            requestDesktopCallback = { handled.add("desktop:$it") },
            addToHomeScreenCallback = { handled.add("home") },
            showFindInPageCallback = { handled.add("find") },
            openInCallback = { handled.add("open") },
            openInBrowser = { handled.add("focus") },
            showShortcutAddedSnackBar = { handled.add("shortcut") },
        )
    private val settings =
        listOf(
            MenuItemsGroup.Row(
                id = "settings",
                items =
                    listOf(
                        StandardMenuItem(
                            MenuItemTitle(Text.String("Settings")),
                            MenuItemTapped(ToolbarMenu.Item.Settings),
                        )
                    ),
            )
        )

    @Before
    fun setup() {
        whenever(menu.menuGroups).thenReturn(groups)
        whenever(sessionUseCases.goBack).thenReturn(goBack)
        whenever(sessionUseCases.goForward).thenReturn(goForward)
        whenever(sessionUseCases.reload).thenReturn(reload)
        whenever(sessionUseCases.stopLoading).thenReturn(stopLoading)
    }

    @Test
    fun `WHEN menu items change THEN update the store until the menu scope is cancelled`() = runTest {
        val store = menuStore()
        runCurrent()

        groups.value = settings
        runCurrent()
        assertEquals(settings, store.state.menuGroups)

        backgroundScope.cancel()
        runCurrent()
        assertEquals(0, groups.subscriptionCount.value)
        groups.value = emptyList()
        runCurrent()
        assertEquals(settings, store.state.menuGroups)
        assertEquals(0, dismissCount)
        assertTrue(handled.isEmpty())
        verifyNoInteractions(goBack, goForward, reload, stopLoading, topSitesStorage)
    }

    @Test
    fun `WHEN updating items THEN pass the action to the reducer without dismissing`() = runTest {
        val store = menuStore()

        store.dispatch(MenuAction.Update(settings))

        assertEquals(settings, store.state.menuGroups)
        assertEquals(0, dismissCount)
        assertTrue(handled.isEmpty())
        verifyNoInteractions(goBack, goForward, reload, stopLoading, topSitesStorage)
    }

    @Test
    fun `WHEN navigation items are tapped THEN use the menu tab and record the correct telemetry`() = runTest {
        val store = menuStore()
        val items =
            listOf(
                ToolbarMenu.Item.Back,
                ToolbarMenu.Item.Forward,
                ToolbarMenu.Item.Reload,
                ToolbarMenu.Item.Stop,
                ToolbarMenu.CustomTabItem.Back,
                ToolbarMenu.CustomTabItem.Forward,
                ToolbarMenu.CustomTabItem.Reload,
                ToolbarMenu.CustomTabItem.Stop,
            )

        items.forEach { store.dispatch(MenuItemTapped(it)) }

        verify(goBack, times(2)).invoke(tab.id)
        verify(goForward, times(2)).invoke(tab.id)
        verify(reload, times(2)).invoke(tab.id)
        verify(stopLoading, times(2)).invoke(tab.id)
        assertEquals(items.size, dismissCount)
        val expected = listOf("back", "forward", "reload", "stop")
        assertEquals(expected, BrowserMenu.navigationToolbarAction.testGetValue()!!.map { it.extra!!["item"] })
        assertEquals(
            expected,
            CustomTabsToolbar.navigationToolbarAction.testGetValue()!!.map { it.extra!!["item"] },
        )
    }

    @Test
    fun `WHEN UI items are tapped THEN invoke their callbacks before dismissing once`() = runTest {
        val items =
            listOf(
                ToolbarMenu.Item.Share to "share",
                ToolbarMenu.Item.FindInPage to "find",
                ToolbarMenu.CustomTabItem.FindInPage to "find",
                ToolbarMenu.Item.RequestDesktop(false) to "desktop:false",
                ToolbarMenu.Item.RequestDesktop(true) to "desktop:true",
                ToolbarMenu.CustomTabItem.RequestDesktop(false) to "desktop:false",
                ToolbarMenu.CustomTabItem.RequestDesktop(true) to "desktop:true",
                ToolbarMenu.Item.AddToHomeScreen to "home",
                ToolbarMenu.CustomTabItem.AddToHomeScreen to "home",
                ToolbarMenu.Item.OpenInApp to "open",
                ToolbarMenu.CustomTabItem.OpenInApp to "open",
                ToolbarMenu.CustomTabItem.OpenInBrowser to "focus",
            )
        val store = menuStore(onDismiss = { handled.add("dismiss") })

        items.forEach { (item, _) -> store.dispatch(MenuItemTapped(item)) }

        assertEquals(items.flatMap { (_, callback) -> listOf(callback, "dismiss") }, handled)
        assertEquals(items.size, dismissCount)
    }

    @Test
    fun `WHEN settings is tapped THEN open settings and dismiss`() = runTest {
        val store = menuStore()

        store.dispatch(MenuItemTapped(ToolbarMenu.Item.Settings))

        assertEquals(Screen.Settings.Page.Start, (appStore.state.screen as Screen.Settings).page)
        assertEquals(1, dismissCount)
    }

    @Test
    fun `WHEN report site issue is tapped THEN invoke the latest action for the menu tab`() = runTest {
        var globalClicks = 0
        var tabClicks = 0
        val action = Action("Report broken site…", true, null, null, null, null) { globalClicks++ }
        val store = menuStore()
        browserStore.dispatch(
            WebExtensionAction.InstallWebExtensionAction(
                WebExtensionState(id = WEBCOMPAT_REPORTER_EXTENSION_ID, pageAction = action)
            )
        )
        browserStore.dispatch(
            WebExtensionAction.UpdateTabPageAction(
                sessionId = tab.id,
                extensionId = WEBCOMPAT_REPORTER_EXTENSION_ID,
                pageAction = action.copy(onClick = { tabClicks++ }),
            )
        )

        store.dispatch(MenuItemTapped(ToolbarMenu.Item.ReportSiteIssue))

        assertEquals(0, globalClicks)
        assertEquals(1, tabClicks)
        assertEquals(1, dismissCount)
        assertEquals(1, Browser.reportSiteIssueCounter.testGetValue())
    }

    @Test
    fun `WHEN reporting from a custom tab THEN use its action rather than the selected regular tab`() = runTest {
        var clicks = 0
        var globalClicks = 0
        var regularTabClicks = 0
        val action = Action("Report broken site…", true, null, null, null, null) { globalClicks++ }
        val extension =
            WebExtensionState(
                id = WEBCOMPAT_REPORTER_EXTENSION_ID,
                pageAction = action,
                allowedInPrivateBrowsing = true,
            )
        val customTab =
            createCustomTab("https://mozilla.org", id = "custom")
                .copy(
                    extensionState =
                        mapOf(extension.id to extension.copy(pageAction = action.copy(onClick = { clicks++ })))
                )
        val regularTab =
            otherTab.copy(
                extensionState =
                    mapOf(extension.id to extension.copy(pageAction = action.copy(onClick = { regularTabClicks++ })))
            )
        val browserStore =
            BrowserStore(
                BrowserState(
                    tabs = listOf(regularTab),
                    selectedTabId = regularTab.id,
                    customTabs = listOf(customTab),
                    extensions = mapOf(extension.id to extension),
                )
            )
        val store = menuStore(browserStore = browserStore, currentTabId = customTab.id)

        store.dispatch(MenuItemTapped(ToolbarMenu.CustomTabItem.ReportSiteIssue))

        assertEquals(1, clicks)
        assertEquals(0, globalClicks)
        assertEquals(0, regularTabClicks)
        assertEquals(1, dismissCount)
    }

    @Test
    fun `GIVEN reporter was disabled WHEN report site issue is tapped THEN do not invoke it`() = runTest {
        var clicks = 0
        val action = Action("Report broken site…", true, null, null, null, null) { clicks++ }
        val store = menuStore()
        browserStore.dispatch(
            WebExtensionAction.InstallWebExtensionAction(
                WebExtensionState(id = WEBCOMPAT_REPORTER_EXTENSION_ID, pageAction = action)
            )
        )
        browserStore.dispatch(
            WebExtensionAction.UpdateWebExtensionEnabledAction(WEBCOMPAT_REPORTER_EXTENSION_ID, false)
        )

        store.dispatch(MenuItemTapped(ToolbarMenu.Item.ReportSiteIssue))

        assertEquals(0, clicks)
        assertEquals(1, dismissCount)
    }

    @Test
    fun `WHEN adding a shortcut THEN the write survives menu dismissal`() = runTest {
        val store = menuStore(onDismiss = { backgroundScope.cancel() })

        store.dispatch(MenuItemTapped(ToolbarMenu.Item.AddToShortcuts))
        runCurrent()

        verify(topSitesStorage).addTopSite("Mozilla", tab.content.url, false)
        assertEquals(listOf("shortcut"), handled)
        assertEquals(1, dismissCount)
        assertEquals(1, Shortcuts.shortcutAddedCounter.testGetValue())
    }

    @Test
    fun `WHEN removing a shortcut THEN the write survives menu dismissal`() = runTest {
        val store = menuStore(onDismiss = { backgroundScope.cancel() })

        store.dispatch(MenuItemTapped(ToolbarMenu.Item.RemoveFromShortcuts))
        runCurrent()

        verify(topSitesStorage).removeTopSite(shortcut)
        assertEquals(1, dismissCount)
        assertEquals(1, Shortcuts.shortcutRemovedCounter["removed_from_browser_menu"].testGetValue())
    }

    private fun TestScope.menuStore(
        browserStore: BrowserStore = this@MenuMiddlewareTest.browserStore,
        currentTabId: String = tab.id,
        onDismiss: () -> Unit = {},
    ): MenuStore =
        MenuStore(
            middleware =
                listOf(
                    MenuMiddleware(
                        menu = menu,
                        sessionUseCases = sessionUseCases,
                        appStore = appStore,
                        browserStore = browserStore,
                        topSitesUseCases = TopSitesUseCases(topSitesStorage),
                        currentTabId = currentTabId,
                        callbacks = callbacks,
                        onDismiss = {
                            dismissCount++
                            onDismiss()
                        },
                        scope = backgroundScope,
                        applicationScope = this,
                        ioDispatcher = StandardTestDispatcher(testScheduler),
                    )
                )
        )
}
