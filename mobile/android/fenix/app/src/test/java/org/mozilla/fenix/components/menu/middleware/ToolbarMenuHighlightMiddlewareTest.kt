/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import kotlin.test.assertEquals
import mozilla.components.compose.menu.store.MenuState
import mozilla.components.compose.menu.store.MenuStore
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.settings.summarize.FakeSummarizationFeatureConfiguration
import org.mozilla.fenix.tabgroups.fake.FakeTabGroupsFeatureDiscoveryConfiguration

class ToolbarMenuHighlightMiddlewareTest {
    private val tabGroupsDiscovery = FakeTabGroupsFeatureDiscoveryConfiguration()
    private val summarizeDiscovery = FakeSummarizationFeatureConfiguration()

    @Test
    fun `GIVEN tab groups highlights the toolbar WHEN the menu is opened from the browser THEN record the toolbar open for tab groups only`() {
        openMenu(notifications = setOf(SupportedMenuNotifications.TabGroups))

        assertEquals(1, tabGroupsDiscovery.toolbarOpenedCount)
        assertEquals(0, summarizeDiscovery.toolbarOverflowMenuInteractionCount)
    }

    @Test
    fun `GIVEN summarize highlights the toolbar WHEN the menu is opened from the browser THEN record the toolbar open for summarize only`() {
        openMenu(notifications = setOf(SupportedMenuNotifications.Summarize))

        assertEquals(0, tabGroupsDiscovery.toolbarOpenedCount)
        assertEquals(1, summarizeDiscovery.toolbarOverflowMenuInteractionCount)
    }

    @Test
    fun `GIVEN tab groups and summarize highlight the toolbar WHEN the menu is opened from the browser THEN record the toolbar open for both`() {
        openMenu(notifications = setOf(SupportedMenuNotifications.TabGroups, SupportedMenuNotifications.Summarize))

        assertEquals(1, tabGroupsDiscovery.toolbarOpenedCount)
        assertEquals(1, summarizeDiscovery.toolbarOverflowMenuInteractionCount)
    }

    @Test
    fun `GIVEN no feature highlights the toolbar WHEN the menu is opened from the browser THEN record nothing`() {
        openMenu(notifications = setOf(SupportedMenuNotifications.Downloads))

        assertEquals(0, tabGroupsDiscovery.toolbarOpenedCount)
        assertEquals(0, summarizeDiscovery.toolbarOverflowMenuInteractionCount)
    }

    @Test
    fun `GIVEN tab groups and summarize highlight the toolbar WHEN the menu is opened from outside the browser THEN record nothing`() {
        val notifications = setOf(SupportedMenuNotifications.TabGroups, SupportedMenuNotifications.Summarize)
        listOf(MenuAccessPoint.Home, MenuAccessPoint.External, MenuAccessPoint.Preview).forEach { accessPoint ->
            openMenu(notifications = notifications, accessPoint = accessPoint)
        }

        assertEquals(0, tabGroupsDiscovery.toolbarOpenedCount)
        assertEquals(0, summarizeDiscovery.toolbarOverflowMenuInteractionCount)
    }

    @Test
    fun `GIVEN tab groups and summarize highlight the toolbar WHEN an open browser menu is recreated THEN record nothing`() {
        openMenu(
            notifications = setOf(SupportedMenuNotifications.TabGroups, SupportedMenuNotifications.Summarize),
            isRestored = true,
        )

        assertEquals(0, tabGroupsDiscovery.toolbarOpenedCount)
        assertEquals(0, summarizeDiscovery.toolbarOverflowMenuInteractionCount)
    }

    private fun openMenu(
        notifications: Set<SupportedMenuNotifications>,
        accessPoint: MenuAccessPoint = MenuAccessPoint.Browser,
        isRestored: Boolean = false,
    ) =
        MenuStore(
            initialState = MenuState(emptyList()),
            middleware =
                listOf(
                    ToolbarMenuHighlightMiddleware(
                        accessPoint = accessPoint,
                        isRestored = isRestored,
                        appStore = AppStore(initialState = AppState(supportedMenuNotifications = notifications)),
                        tabGroupsDiscovery = tabGroupsDiscovery,
                        summarizeDiscovery = summarizeDiscovery,
                    )
                ),
        )
}
