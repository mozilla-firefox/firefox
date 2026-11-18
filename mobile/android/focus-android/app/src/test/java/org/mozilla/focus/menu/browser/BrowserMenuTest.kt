/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.menu.browser

import android.graphics.Bitmap
import android.graphics.drawable.BitmapDrawable
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.WebExtensionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.ui.MenuItemIcon
import mozilla.components.compose.menu.ui.MenuItemIconDrawable
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.concept.engine.webextension.Action
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.feature.webcompat.reporter.WebCompatReporterFeature.WEBCOMPAT_REPORTER_EXTENSION_ID
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.focus.R
import org.mozilla.focus.menu.MenuItemTapped
import org.mozilla.focus.menu.ToolbarMenu.Item
import org.mozilla.focus.state.AppState
import org.mozilla.focus.state.AppStore
import org.mozilla.focus.state.Screen
import org.mozilla.focus.topsites.DefaultTopSitesStorage.Companion.TOP_SITES_MAX_LIMIT
import org.robolectric.RobolectricTestRunner

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
class BrowserMenuTest {
    private val tab = createTab("https://mozilla.org", id = "tab", private = true)
    private val browserState = BrowserState(tabs = listOf(tab), selectedTabId = tab.id)
    private val appState = AppState(screen = Screen.Home())
    private val action = Action("Report broken site…", true, null, null, null, null) {}
    private val extension =
        WebExtensionState(
            id = WEBCOMPAT_REPORTER_EXTENSION_ID,
            allowedInPrivateBrowsing = true,
            pageAction = action,
        )

    @Test
    fun `GIVEN navigation history WHEN building the menu THEN enable only available directions`() {
        for (canGoBack in listOf(false, true)) {
            for (canGoForward in listOf(false, true)) {
                val state =
                    browserState.copy(
                        tabs =
                            listOf(
                                tab.copy(content = tab.content.copy(canGoBack = canGoBack, canGoForward = canGoForward))
                            )
                    )
                val items = menu(state).currentMenuGroups().flatMap { it.items }

                assertEquals(
                    if (canGoBack) MenuItemState.DEFAULT else MenuItemState.DISABLED,
                    items.item(Item.Back).state,
                )
                assertEquals(
                    if (canGoForward) MenuItemState.DEFAULT else MenuItemState.DISABLED,
                    items.item(Item.Forward).state,
                )
            }
        }
    }

    @Test
    fun `WHEN loading changes THEN replace reload with stop and back again`() = runTest {
        val store = BrowserStore(browserState)
        val menu = BrowserMenu(store, AppStore(appState), mock())
        var groups = emptyList<MenuItemsGroup>()
        backgroundScope.launch { menu.menuGroups.collect { groups = it } }
        runCurrent()
        assertTrue(groups.flatMap { it.items }.hasItem(Item.Reload))

        store.dispatch(ContentAction.UpdateLoadingStateAction(tab.id, true))
        runCurrent()
        assertTrue(groups.flatMap { it.items }.hasItem(Item.Stop))
        assertFalse(groups.flatMap { it.items }.hasItem(Item.Reload))

        store.dispatch(ContentAction.UpdateLoadingStateAction(tab.id, false))
        runCurrent()
        assertTrue(groups.flatMap { it.items }.hasItem(Item.Reload))
        assertFalse(groups.flatMap { it.items }.hasItem(Item.Stop))
    }

    @Test
    fun `WHEN building desktop site item THEN show current state and request the opposite state`() {
        for (desktopMode in listOf(false, true)) {
            val state =
                browserState.copy(tabs = listOf(tab.copy(content = tab.content.copy(desktopMode = desktopMode))))
            val item = menu(state).currentMenuGroups().flatMap { it.items }.item(Item.RequestDesktop(!desktopMode))

            assertEquals(if (desktopMode) MenuItemState.ACTIVE else MenuItemState.DEFAULT, item.state)
            assertEquals(
                Text.Resource(if (desktopMode) R.string.preference_state_on else R.string.preference_state_off),
                item.badge?.text,
            )
            assertNull(item.contentDescription)
        }
    }

    @Test
    fun `GIVEN a PDF WHEN building the menu THEN hide desktop site`() {
        val state = browserState.copy(tabs = listOf(tab.copy(content = tab.content.copy(isPdf = true))))
        val items = menu(state).currentMenuGroups().flatMap { it.items }

        assertFalse(items.hasItem(Item.RequestDesktop(true)))
        assertFalse(items.hasItem(Item.RequestDesktop(false)))
    }

    @Test
    fun `GIVEN shortcut capacity WHEN building the menu THEN offer add or remove as appropriate`() {
        val shortcuts =
            List(TOP_SITES_MAX_LIMIT) {
                TopSite.Pinned(id = it.toLong(), title = "Shortcut", url = "https://example.org/$it", createdAt = 0)
            }
        val emptyItems = menu().currentMenuGroups().flatMap { it.items }
        assertTrue(emptyItems.hasItem(Item.AddToShortcuts))
        assertFalse(emptyItems.hasItem(Item.RemoveFromShortcuts))

        val fullGroups = menu(appState = appState.copy(topSites = shortcuts)).currentMenuGroups()
        assertFalse(fullGroups.flatMap { it.items }.hasItem(Item.AddToShortcuts))
        assertFalse(fullGroups.flatMap { it.items }.hasItem(Item.RemoveFromShortcuts))
        assertTrue(fullGroups.all { it.items.isNotEmpty() })

        val pinned = shortcuts.toMutableList().apply { this[0] = shortcuts[0].copy(url = tab.content.url) }
        val pinnedItems = menu(appState = appState.copy(topSites = pinned)).currentMenuGroups().flatMap { it.items }
        assertFalse(pinnedItems.hasItem(Item.AddToShortcuts))
        assertTrue(pinnedItems.hasItem(Item.RemoveFromShortcuts))
    }

    @Test
    fun `GIVEN pinning support WHEN building the menu THEN offer add to home only when supported`() {
        for (supported in listOf(null, false, true)) {
            val items =
                menu(appState = appState.copy(isPinningSupported = supported)).currentMenuGroups().flatMap { it.items }
            assertEquals(supported == true, items.hasItem(Item.AddToHomeScreen))
        }

        val items =
            menu(BrowserState(), appState.copy(isPinningSupported = true)).currentMenuGroups().flatMap { it.items }
        assertFalse(items.hasItem(Item.AddToHomeScreen))
        assertFalse(items.hasItem(Item.AddToShortcuts))
    }

    @Test
    fun `GIVEN an enabled reporter WHEN building the menu THEN use its page action title`() {
        val state = browserState.copy(extensions = mapOf(extension.id to extension))
        val item = menu(state).currentMenuGroups().flatMap { it.items }.item(Item.ReportSiteIssue)

        assertEquals(Text.String("Report broken site…"), item.title.text)
        assertEquals(action, state.webCompatReporterAction())
    }

    @Test
    fun `GIVEN an unavailable reporter WHEN building the menu THEN hide report site issue`() {
        val extensions =
            listOf(
                extension.copy(enabled = false),
                extension.copy(allowedInPrivateBrowsing = false),
                extension.copy(pageAction = null),
                extension.copy(pageAction = action.copy(enabled = false)),
                extension.copy(pageAction = action.copy(enabled = null)),
            )
        for (unavailable in extensions) {
            val state = browserState.copy(extensions = mapOf(extension.id to unavailable))
            assertNull(state.webCompatReporterAction())
            assertFalse(menu(state).currentMenuGroups().flatMap { it.items }.hasItem(Item.ReportSiteIssue))
        }
        assertNull(browserState.webCompatReporterAction())
    }

    @Test
    fun `GIVEN the reporter starts up while the menu is shown THEN load its icon once it is available`() = runTest {
        val icon = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888)
        val store = BrowserStore(browserState)
        val menu = BrowserMenu(store, AppStore(appState), testContext)
        var groups = emptyList<MenuItemsGroup>()
        backgroundScope.launch { menu.menuGroups.collect { groups = it } }
        runCurrent()
        assertFalse(groups.flatMap { it.items }.hasItem(Item.ReportSiteIssue))

        val loadable = extension.copy(pageAction = action.copy(loadIcon = { icon }))
        store.dispatch(WebExtensionAction.InstallWebExtensionAction(loadable))
        runCurrent()

        assertEquals(icon, groups.flatMap { it.items }.item(Item.ReportSiteIssue).icon.bitmap)
    }

    @Test
    fun `GIVEN a tab override WHEN resolving reporter action THEN merge it with the global action`() {
        val override = action.copy(title = "Report this tab", enabled = true)
        val state =
            browserState.copy(
                extensions = mapOf(extension.id to extension.copy(pageAction = action.copy(enabled = false))),
                tabs = listOf(tab.copy(extensionState = mapOf(extension.id to extension.copy(pageAction = override)))),
            )

        assertEquals(override, state.webCompatReporterAction())
        assertEquals(
            Text.String("Report this tab"),
            menu(state).currentMenuGroups().flatMap { it.items }.item(Item.ReportSiteIssue).title.text,
        )

        val disabledOverride = extension.copy(pageAction = override.copy(enabled = false))
        val disabled = state.copy(tabs = listOf(tab.copy(extensionState = mapOf(extension.id to disabledOverride))))
        assertNull(disabled.webCompatReporterAction())
    }

    private fun menu(state: BrowserState = browserState, appState: AppState = this.appState) =
        BrowserMenu(BrowserStore(state), AppStore(appState), mock())

    private val MenuItemIcon?.bitmap
        get() = ((this as MenuItemIconDrawable).icon as BitmapDrawable).bitmap

    private fun List<MenuItem>.item(item: Item) = single { it.onClickEvent == MenuItemTapped(item) }

    private fun List<MenuItem>.hasItem(item: Item) = any { it.onClickEvent == MenuItemTapped(item) }
}
