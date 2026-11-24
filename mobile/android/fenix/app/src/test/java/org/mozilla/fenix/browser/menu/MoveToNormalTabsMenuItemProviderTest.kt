/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.menu

import io.mockk.mockk
import io.mockk.verify
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.ui.icons.R as iconsR
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.fake.FakeMenuHost
import org.mozilla.fenix.components.menu.fake.reachableEvents
import org.mozilla.fenix.components.menu.store.MenuAction

class MoveToNormalTabsMenuItemProviderTest {
    private val migratePrivateTab: TabsUseCases.MigratePrivateTabUseCase = mockk(relaxed = true)
    private val menu = FakeMenuHost()

    @Test
    fun `GIVEN a private tab WHEN building the menu item THEN offer moving it to normal tabs`() {
        val provider = createProvider(browserStore(isPrivate = true))

        assertEquals(
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_move_to_non_private_tab)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_external_link_24),
                onClickEvent = MenuAction.MoveToNonPrivateTab,
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN a normal tab WHEN building the menu item THEN don't show it`() {
        val provider = createProvider(browserStore(isPrivate = false))

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN there is no selected tab WHEN building the menu item THEN don't show it`() {
        val provider = createProvider(BrowserStore())

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN private browsing and a normal selected tab WHEN building the menu item THEN don't show it`() {
        val provider =
            createProvider(
                browserStore = browserStore(isPrivate = false),
                target = MenuTarget.BrowserTab,
                inPrivateBrowsing = true,
            )

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN private browsing and a normal selected tab WHEN customizing the menu THEN show the item`() {
        val provider =
            createProvider(
                browserStore = browserStore(isPrivate = false),
                target = MenuTarget.CustomizeMenu,
                inPrivateBrowsing = true,
            )

        assertEquals(
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_move_to_non_private_tab)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_external_link_24),
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN normal browsing and a private selected tab WHEN customizing the menu THEN don't show the item`() {
        val provider =
            createProvider(
                browserStore = browserStore(isPrivate = true),
                target = MenuTarget.CustomizeMenu,
                inPrivateBrowsing = false,
            )

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN private browsing and no tabs WHEN customizing the menu THEN show the item`() {
        val provider =
            createProvider(
                browserStore = BrowserStore(),
                target = MenuTarget.CustomizeMenu,
                inPrivateBrowsing = true,
            )

        assertEquals(
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_move_to_non_private_tab)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_external_link_24),
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN private browsing and a normal selected tab WHEN previewing the menu THEN show the clickable item`() {
        val provider =
            createProvider(
                browserStore = browserStore(isPrivate = false),
                target = MenuTarget.Preview,
                inPrivateBrowsing = true,
            )

        assertEquals(
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_move_to_non_private_tab)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_external_link_24),
                onClickEvent = MenuAction.MoveToNonPrivateTab,
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN normal browsing and a private selected tab WHEN previewing the menu THEN don't show the item`() {
        val provider =
            createProvider(
                browserStore = browserStore(isPrivate = true),
                target = MenuTarget.Preview,
                inPrivateBrowsing = false,
            )

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN private browsing and no tabs WHEN previewing the menu THEN show the clickable item`() {
        val provider =
            createProvider(
                browserStore = BrowserStore(),
                target = MenuTarget.Preview,
                inPrivateBrowsing = true,
            )

        assertEquals(
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_move_to_non_private_tab)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_external_link_24),
                onClickEvent = MenuAction.MoveToNonPrivateTab,
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN a private tab WHEN clicking the item THEN close the menu and move the tab to normal tabs`() {
        val provider = createProvider(browserStore(isPrivate = true))

        provider.onEvent(MenuAction.MoveToNonPrivateTab, menu)

        assertTrue(menu.isDismissed)
        verify { migratePrivateTab(TAB_ID) }
    }

    @Test
    fun `GIVEN no selected tab WHEN clicking the item THEN keep the menu open`() {
        val provider = createProvider(BrowserStore())

        provider.onEvent(MenuAction.MoveToNonPrivateTab, menu)

        assertFalse(menu.isUsed)
        verify(exactly = 0) { migratePrivateTab(any()) }
    }

    @Test
    fun `WHEN building the item THEN handle all events it can dispatch and no others`() {
        val provider = createProvider(browserStore(isPrivate = true))

        val events = provider.itemsFlow.value.single().reachableEvents()

        assertTrue(events.all { provider.handles(it) }, "Not all of $events are handled")
        assertFalse(provider.handles(MenuAction.FindInPage))
    }

    private fun createProvider(
        browserStore: BrowserStore,
        target: MenuTarget = MenuTarget.BrowserTab,
        inPrivateBrowsing: Boolean = false,
    ) =
        MoveToNormalTabsMenuItemProvider(
            browserStore = browserStore,
            target = target,
            migratePrivateTab = migratePrivateTab,
            inPrivateBrowsing = inPrivateBrowsing,
        )

    private fun browserStore(isPrivate: Boolean) =
        BrowserStore(
            BrowserState(
                tabs = listOf(createTab(url = "https://mozilla.org", id = TAB_ID, private = isPrivate)),
                selectedTabId = TAB_ID,
            )
        )

    private companion object {
        const val TAB_ID = "tab1"
    }
}
