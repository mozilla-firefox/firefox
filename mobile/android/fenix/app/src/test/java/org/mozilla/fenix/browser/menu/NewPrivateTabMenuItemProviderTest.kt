/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.menu

import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.ui.icons.R as iconsR
import mozilla.components.ui.tabcounter.R as tabcounterR
import org.junit.Test
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.fake.FakeMenuHost
import org.mozilla.fenix.components.menu.fake.reachableEvents
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.utils.Settings

class NewPrivateTabMenuItemProviderTest {
    private val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private val browsingModeManager: BrowsingModeManager = mockk(relaxed = true)
    private val menu = FakeMenuHost()

    @Test
    fun `GIVEN target is BrowserTab WHEN building the menu item THEN offer opening a new private tab`() {
        val provider = createProvider(MenuTarget.BrowserTab)

        assertEquals(
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(tabcounterR.string.mozac_browser_menu_new_private_tab)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_private_mode_fill_24),
                onClickEvent = MenuAction.Navigate.NewPrivateTab,
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN target is CustomizeMenu WHEN building the menu item THEN offer default item without onClickEvent`() {
        val provider = createProvider(MenuTarget.CustomizeMenu)

        val item = provider.itemsFlow.value.single() as StandardMenuItem
        assertEquals(MenuItemTitle(Text.Resource(tabcounterR.string.mozac_browser_menu_new_private_tab)), item.title)
        assertEquals(MenuItemIconRes(iconsR.drawable.mozac_ic_private_mode_fill_24), item.icon)
        assertNull(item.onClickEvent)
    }

    @Test
    fun `GIVEN target is Preview WHEN building the menu item THEN offer item with onClickEvent for ripple animation`() {
        val provider = createProvider(MenuTarget.Preview)

        val item = provider.itemsFlow.value.single() as StandardMenuItem
        assertEquals(MenuItemTitle(Text.Resource(tabcounterR.string.mozac_browser_menu_new_private_tab)), item.title)
        assertEquals(MenuItemIconRes(iconsR.drawable.mozac_ic_private_mode_fill_24), item.icon)
        assertEquals(MenuAction.Navigate.NewPrivateTab, item.onClickEvent)
    }

    @Test
    fun `GIVEN target is Preview WHEN checking handled events THEN return false`() {
        val provider = createProvider(MenuTarget.Preview)

        assertFalse(provider.handles(MenuAction.Navigate.NewPrivateTab))
    }

    @Test
    fun `GIVEN homepage as new tab is enabled WHEN clicking the item THEN set private browsing mode, add new homepage tab and navigate home`() {
        every { settings.enableHomepageAsNewTab } returns true
        val provider = createProvider()

        provider.onEvent(MenuAction.Navigate.NewPrivateTab, menu)

        verify { browsingModeManager.mode = BrowsingMode.Private }
        verify { fenixBrowserUseCases.addNewHomepageTab(private = true) }
        assertEquals(NavGraphDirections.actionGlobalHome(), menu.directions)
    }

    @Test
    fun `GIVEN homepage as new tab is disabled WHEN clicking the item THEN set private browsing mode and navigate home focusing address bar`() {
        every { settings.enableHomepageAsNewTab } returns false
        every { settings.enableHomepageSearchBar } returns false
        every { settings.enableHomepageTrendingRecentSearch } returns false
        val provider = createProvider()

        provider.onEvent(MenuAction.Navigate.NewPrivateTab, menu)

        verify { browsingModeManager.mode = BrowsingMode.Private }
        verify(exactly = 0) { fenixBrowserUseCases.addNewHomepageTab(any()) }
        assertEquals(
            NavGraphDirections.actionGlobalHome(focusOnAddressBar = true),
            menu.directions,
        )
    }

    @Test
    fun `GIVEN homepage as new tab is disabled AND search bar is enabled WHEN clicking the item THEN do not focus address bar`() {
        every { settings.enableHomepageAsNewTab } returns false
        every { settings.enableHomepageSearchBar } returns true
        every { settings.enableHomepageTrendingRecentSearch } returns false
        val provider = createProvider()

        provider.onEvent(MenuAction.Navigate.NewPrivateTab, menu)

        verify { browsingModeManager.mode = BrowsingMode.Private }
        verify(exactly = 0) { fenixBrowserUseCases.addNewHomepageTab(any()) }
        assertEquals(
            NavGraphDirections.actionGlobalHome(focusOnAddressBar = false),
            menu.directions,
        )
    }

    @Test
    fun `WHEN building the item THEN handle all events it can dispatch and no others`() {
        val provider = createProvider()

        val events = provider.itemsFlow.value.single().reachableEvents()

        assertTrue(events.all { provider.handles(it) }, "Not all of $events are handled")
        assertFalse(provider.handles(MenuAction.FindInPage))
    }

    private fun createProvider(target: MenuTarget = MenuTarget.BrowserTab) =
        NewPrivateTabMenuItemProvider(
            target = target,
            fenixBrowserUseCases = fenixBrowserUseCases,
            settings = settings,
            browsingModeManager = browsingModeManager,
        )
}
