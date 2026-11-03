/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import io.mockk.mockk
import kotlin.test.assertEquals
import kotlin.test.assertSame
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.store.MenuEvent
import org.junit.Test
import org.mozilla.fenix.browser.BackMenuItemProvider
import org.mozilla.fenix.browser.ForwardMenuItemProvider
import org.mozilla.fenix.browser.RefreshMenuItemProvider
import org.mozilla.fenix.browser.ShareMenuItemProvider
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.CustomizeReaderView
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.More
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Settings
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Row
import org.mozilla.fenix.components.menu.configuration.MenuSectionConfiguration
import org.mozilla.fenix.components.menu.store.MenuAction

class MenuItemsRegistryTest {
    @Test
    fun `GIVEN menu items configured more than once WHEN building the providers THEN build a single one for each menu item`() {
        val more = More(subMenuItems = listOf(FindInPage, Settings, FindInPage))
        val resolved = mutableListOf<FenixMenuItem>()

        MenuItemsRegistry(
            configuration =
                listOf(
                    sectionOf(CustomizeReaderView, more, FindInPage),
                    sectionOf(Settings, more, CustomizeReaderView),
                ),
            resolver = { item -> FakeMenuItemProvider().also { resolved += item } },
        )

        assertEquals(listOf(CustomizeReaderView, more, FindInPage, Settings), resolved)
    }

    @Test
    fun `WHEN asking for the provider of an item THEN always offer the one configured for it`() {
        val registry =
            MenuItemsRegistry(
                configuration = listOf(sectionOf(FindInPage)),
                resolver = { FakeMenuItemProvider() },
            )

        assertSame(registry.providers.getValue(FindInPage), registry[FindInPage])
        assertSame(registry[FindInPage], registry.get(FindInPage))
    }

    @Test
    fun `WHEN building the default menu THEN each navigation event is handled by exactly one provider`() = runTest {
        val browserStore = BrowserStore()
        val session = MenuTarget.BrowserTab
        val registry =
            MenuItemsRegistry(
                configuration =
                    BrowserMenuConfiguration.sections(isToolbarAtBottom = false, isExpandedToolbarEnabled = false),
                resolver = { item ->
                    when (item) {
                        FenixMenuItem.Back ->
                            BackMenuItemProvider(browserStore, session, mockk(), mockk(), backgroundScope)
                        FenixMenuItem.Forward ->
                            ForwardMenuItemProvider(browserStore, session, mockk(), backgroundScope)
                        FenixMenuItem.Share -> ShareMenuItemProvider(browserStore, session, mockk())
                        FenixMenuItem.Refresh ->
                            RefreshMenuItemProvider(browserStore, session, mockk(), mockk(), backgroundScope)
                        else -> FakeMenuItemProvider()
                    }
                },
            )
        val navigationEvents =
            listOf(
                MenuAction.Navigate.Back(viewHistory = false),
                MenuAction.Navigate.Back(viewHistory = true),
                MenuAction.Navigate.Forward(viewHistory = false),
                MenuAction.Navigate.Forward(viewHistory = true),
                MenuAction.Navigate.Share,
                MenuAction.Navigate.Reload(bypassCache = false),
                MenuAction.Navigate.Reload(bypassCache = true),
                MenuAction.Navigate.Stop,
            )

        navigationEvents.forEach { event ->
            assertEquals(1, registry.providers.values.count { it.handles(event) }, "Handlers of $event")
        }
    }

    private fun sectionOf(vararg items: FenixMenuItem) =
        MenuSectionConfiguration(id = "section", presentationMode = Row, items = items.toList())

    private class FakeMenuItemProvider : MenuItemProvider {
        override val itemsFlow = MutableStateFlow<List<MenuItem>>(emptyList())

        override fun handles(event: MenuEvent) = false

        override fun onEvent(event: MenuEvent, menu: MenuHost) = Unit
    }
}
