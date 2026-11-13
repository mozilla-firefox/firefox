/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.feature.tabdata.coordinator.data.Tab
import mozilla.components.feature.tabdata.coordinator.data.TabsSnapshot
import mozilla.components.feature.tabdata.coordinator.fakes.FakeTabDataCoordinator
import org.junit.Test
import org.mozilla.fenix.components.menu.MenuFragmentDirections
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.fake.FakeMenuHost
import org.mozilla.fenix.components.menu.fake.reachableEvents
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.tabgroups.TabGroupMenuItemProvider.Companion.ADD_ITEM
import org.mozilla.fenix.tabgroups.TabGroupMenuItemProvider.Companion.MOVE_ITEM
import org.mozilla.fenix.tabgroups.flow.TabGroupFlowEntryPoint

@OptIn(ExperimentalCoroutinesApi::class)
class TabGroupMenuItemProviderTest {
    private val menu = FakeMenuHost()

    @Test
    fun `GIVEN the selected tab is not in a group WHEN building the menu item THEN show add to group`() = runTest {
        val provider = provider(snapshotWithSelectedTab(tabGroupId = null))

        assertEquals(ADD_ITEM.copy(onClickEvent = addToGroupFlow()), provider.itemsFlow.value.single())
    }

    @Test
    fun `GIVEN the selected tab is in a group WHEN building the menu item THEN show move to group`() = runTest {
        val provider = provider(snapshotWithSelectedTab(tabGroupId = "group"))

        assertEquals(MOVE_ITEM.copy(onClickEvent = moveToGroupFlow()), provider.itemsFlow.value.single())
    }

    @Test
    fun `WHEN building the menu item THEN it is not highlighted`() = runTest {
        listOf(null, "group").forEach { tabGroupId ->
            val item = provider(snapshotWithSelectedTab(tabGroupId = tabGroupId)).itemsFlow.value.single()

            assertNotEquals(
                (item as StandardMenuItem).icon?.isHighlighted,
                true,
                "Highlighted when tabGroupId=$tabGroupId",
            )
            assertFalse(item.showNewIndicator, "New indicator shown when tabGroupId=$tabGroupId")
        }
    }

    @Test
    fun `GIVEN the selected tab is private WHEN building the menu item THEN don't show it`() = runTest {
        val provider = provider(snapshotWithSelectedTab(tabGroupId = null, private = true))

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN the selected tab is private and in a group WHEN building the menu item THEN don't show it`() = runTest {
        val provider = provider(snapshotWithSelectedTab(tabGroupId = "group", private = true))

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `WHEN a private tab becomes selected THEN hide the menu item`() = runTest {
        val coordinator = FakeTabDataCoordinator(snapshotWithSelectedTab(tabGroupId = null))
        val provider = provider(coordinator)

        assertEquals(ADD_ITEM.copy(onClickEvent = addToGroupFlow()), provider.itemsFlow.value.single())

        coordinator.tabsSnapshot.value = snapshotWithSelectedTab(tabGroupId = null, private = true)
        runCurrent()

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN there is no selected tab WHEN building the menu item THEN don't show it`() = runTest {
        val provider = provider(TabsSnapshot(selectedTab = null))

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN the tab data is not ready yet WHEN building the menu item THEN don't show it`() = runTest {
        val provider = provider(snapshot = null)

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `WHEN the selected tab is added to a group THEN switch to move to group`() = runTest {
        val coordinator = FakeTabDataCoordinator(snapshotWithSelectedTab(tabGroupId = null))
        val provider = provider(coordinator)

        assertEquals(ADD_ITEM.copy(onClickEvent = addToGroupFlow()), provider.itemsFlow.value.single())

        coordinator.tabsSnapshot.value = snapshotWithSelectedTab(tabGroupId = "group")
        runCurrent()

        assertEquals(MOVE_ITEM.copy(onClickEvent = moveToGroupFlow()), provider.itemsFlow.value.single())
    }

    @Test
    fun `WHEN the tab data becomes ready THEN show the menu item`() = runTest {
        val coordinator = FakeTabDataCoordinator(initialTabsSnapshot = null)
        val provider = provider(coordinator)

        assertTrue(provider.itemsFlow.value.isEmpty())

        coordinator.tabsSnapshot.value = snapshotWithSelectedTab(tabGroupId = null)
        runCurrent()

        assertEquals(ADD_ITEM.copy(onClickEvent = addToGroupFlow()), provider.itemsFlow.value.single())
    }

    @Test
    fun `GIVEN tab groups are disabled in the menu WHEN building the menu item THEN don't show it`() = runTest {
        val provider = provider(snapshotWithSelectedTab(tabGroupId = null), isEnabled = false)

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN the menu is not for a browser tab WHEN building the menu item THEN don't show it`() = runTest {
        listOf(MenuTarget.Home, MenuTarget.CustomTab(customTabId = "custom")).forEach { target ->
            val provider = provider(snapshotWithSelectedTab(tabGroupId = "group"), target = target)

            assertTrue(provider.itemsFlow.value.isEmpty(), "Expected no items for $target")
        }
    }

    @Test
    fun `GIVEN the customize menu screen WHEN building the menu item THEN show add to group without a click event`() =
        runTest {
            val provider = provider(snapshotWithSelectedTab(tabGroupId = "group"), target = MenuTarget.CustomizeMenu)

            assertEquals(ADD_ITEM, provider.itemsFlow.value.single())
        }

    @Test
    fun `GIVEN tab groups are disabled WHEN building the item for the customize menu THEN don't show it`() = runTest {
        val provider =
            provider(
                snapshotWithSelectedTab(tabGroupId = null),
                target = MenuTarget.CustomizeMenu,
                isEnabled = false,
            )

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `WHEN clicking the menu item THEN open the tab group flow in place of the menu`() = runTest {
        val provider = provider(snapshotWithSelectedTab(tabGroupId = null))
        val event = provider.itemsFlow.value.single().reachableEvents().single()

        provider.onEvent(event, menu)

        assertEquals(
            MenuFragmentDirections.actionMenuFragmentToTabGroupFlowFragment(
                entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = TAB_ID)
            ),
            menu.directions,
        )
        assertFalse(menu.isDismissed)
    }

    @Test
    fun `GIVEN the selected tab is in a group WHEN clicking the menu item THEN open the move to group flow in place of the menu`() =
        runTest {
            val provider = provider(snapshotWithSelectedTab(tabGroupId = "group"))
            val event = provider.itemsFlow.value.single().reachableEvents().single()

            provider.onEvent(event, menu)

            assertEquals(
                MenuFragmentDirections.actionMenuFragmentToTabGroupFlowFragment(
                    entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = TAB_ID)
                ),
                menu.directions,
            )
            assertFalse(menu.isDismissed)
        }

    @Test
    fun `WHEN the tab is in a group or not THEN handle all events the item can dispatch and no others`() = runTest {
        listOf(null, "group").forEach { tabGroupId ->
            val provider = provider(snapshotWithSelectedTab(tabGroupId = tabGroupId))

            val events = provider.itemsFlow.value.single().reachableEvents()

            assertTrue(events.all { provider.handles(it) }, "Not all of $events are handled")
            assertFalse(provider.handles(MenuAction.Navigate.Settings))
        }
    }

    private fun TestScope.provider(
        snapshot: TabsSnapshot?,
        target: MenuTarget = MenuTarget.BrowserTab,
        isEnabled: Boolean = true,
    ) = provider(FakeTabDataCoordinator(snapshot), target, isEnabled)

    private fun TestScope.provider(
        coordinator: FakeTabDataCoordinator,
        target: MenuTarget = MenuTarget.BrowserTab,
        isEnabled: Boolean = true,
    ) =
        TabGroupMenuItemProvider(
            tabDataCoordinator = coordinator,
            target = target,
            isVisible = isEnabled,
            scope = backgroundScope,
        )

    private fun snapshotWithSelectedTab(
        tabGroupId: String?,
        private: Boolean = false,
    ) =
        TabsSnapshot(
            selectedTab =
                Tab(
                    id = TAB_ID,
                    url = "https://mozilla.org",
                    title = "Mozilla",
                    private = private,
                    inactive = false,
                    tabGroupId = tabGroupId,
                )
        )

    private fun addToGroupFlow() =
        MenuAction.Navigate.OpenTabGroupFlow(TabGroupFlowEntryPoint.AddTabToGroup(tabId = TAB_ID))

    private fun moveToGroupFlow() =
        MenuAction.Navigate.OpenTabGroupFlow(TabGroupFlowEntryPoint.MoveTabToGroup(tabId = TAB_ID))

    private companion object {
        const val TAB_ID = "tab"
    }
}
