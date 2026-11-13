/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.feature.tabdata.coordinator.data.Tab
import mozilla.components.feature.tabdata.coordinator.data.TabsSnapshot
import mozilla.components.feature.tabdata.coordinator.fakes.FakeTabDataCoordinator
import mozilla.components.ui.icons.R as iconsR
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuFragmentDirections
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.fake.FakeMenuHost
import org.mozilla.fenix.components.menu.fake.reachableEvents
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.tabgroups.TabGroupMenuItemProvider.Companion.ADD_ITEM
import org.mozilla.fenix.tabgroups.fake.FakeTabGroupsFeatureDiscoveryConfiguration
import org.mozilla.fenix.tabgroups.flow.TabGroupFlowEntryPoint

@OptIn(ExperimentalCoroutinesApi::class)
class TabGroupMenuItemProviderTest {
    private val menu = FakeMenuHost()
    private val discoveryConfiguration = FakeTabGroupsFeatureDiscoveryConfiguration()

    @Test
    fun `GIVEN the selected tab is not in a group WHEN building the menu item THEN show add to group`() = runTest {
        val provider = provider(snapshotWithSelectedTab(tabGroupId = null))

        assertEquals(expectedAddItem(), provider.itemsFlow.value.single())
    }

    @Test
    fun `GIVEN the selected tab is in a group WHEN building the menu item THEN show move to group`() = runTest {
        val provider = provider(snapshotWithSelectedTab(tabGroupId = "group"))

        assertEquals(expectedMoveItem(), provider.itemsFlow.value.single())
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

        assertEquals(expectedAddItem(), provider.itemsFlow.value.single())

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

        assertEquals(expectedAddItem(), provider.itemsFlow.value.single())

        coordinator.tabsSnapshot.value = snapshotWithSelectedTab(tabGroupId = "group")
        runCurrent()

        assertEquals(expectedMoveItem(), provider.itemsFlow.value.single())
    }

    @Test
    fun `WHEN the tab data becomes ready THEN show the menu item`() = runTest {
        val coordinator = FakeTabDataCoordinator(initialTabsSnapshot = null)
        val provider = provider(coordinator)

        assertTrue(provider.itemsFlow.value.isEmpty())

        coordinator.tabsSnapshot.value = snapshotWithSelectedTab(tabGroupId = null)
        runCurrent()

        assertEquals(expectedAddItem(), provider.itemsFlow.value.single())
    }

    @Test
    fun `GIVEN the menu item should not be highlighted WHEN building the menu item THEN it is not highlighted`() =
        runTest {
            listOf(null, "group").forEach { tabGroupId ->
                val provider =
                    provider(
                        snapshotWithSelectedTab(tabGroupId = tabGroupId),
                        discoveryConfiguration = highlighted(false),
                    )

                val item = provider.itemsFlow.value.single() as StandardMenuItem
                assertFalse(item.icon?.isHighlighted == true, "Expected no highlight when tabGroupId=$tabGroupId")
                assertFalse(item.showNewIndicator, "Expected no new indicator when tabGroupId=$tabGroupId")
            }
        }

    @Test
    fun `GIVEN the menu item should be highlighted WHEN building the add item THEN it is highlighted`() = runTest {
        val provider =
            provider(
                snapshotWithSelectedTab(tabGroupId = null),
                discoveryConfiguration = highlighted(true),
            )

        assertEquals(expectedAddItem(isHighlighted = true), provider.itemsFlow.value.single())
    }

    @Test
    fun `GIVEN the menu item should be highlighted WHEN building the move item THEN it is highlighted`() = runTest {
        val provider =
            provider(
                snapshotWithSelectedTab(tabGroupId = "group"),
                discoveryConfiguration = highlighted(true),
            )

        assertEquals(expectedMoveItem(isHighlighted = true), provider.itemsFlow.value.single())
    }

    @Test
    fun `GIVEN the menu item is highlighted WHEN it should stop being highlighted while the menu is open THEN keep the highlight`() =
        runTest {
            val configuration = highlighted(true)
            val coordinator = FakeTabDataCoordinator(snapshotWithSelectedTab(tabGroupId = null))
            val provider = provider(coordinator, discoveryConfiguration = configuration)

            configuration.shouldHighlightMenuItem = false
            coordinator.tabsSnapshot.value = snapshotWithSelectedTab(tabGroupId = "group")
            runCurrent()

            assertEquals(expectedMoveItem(isHighlighted = true), provider.itemsFlow.value.single())
        }

    @Test
    fun `GIVEN the menu item is highlighted WHEN the selected tab is added to a group THEN keep the highlight`() =
        runTest {
            val coordinator = FakeTabDataCoordinator(snapshotWithSelectedTab(tabGroupId = null))
            val provider = provider(coordinator, discoveryConfiguration = highlighted(true))

            coordinator.tabsSnapshot.value = snapshotWithSelectedTab(tabGroupId = "group")
            runCurrent()

            assertEquals(expectedMoveItem(isHighlighted = true), provider.itemsFlow.value.single())
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
    fun `GIVEN the customize menu screen WHEN building the menu item THEN show add to group without events or highlight`() =
        runTest {
            val provider =
                provider(
                    snapshotWithSelectedTab(tabGroupId = "group"),
                    target = MenuTarget.CustomizeMenu,
                    discoveryConfiguration = highlighted(true),
                )

            val item = provider.itemsFlow.value.single()
            assertEquals(ADD_ITEM, item)
            assertTrue(item.reachableEvents().isEmpty())
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
        val event = requireNotNull((provider.itemsFlow.value.single() as StandardMenuItem).onClickEvent)

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
            val event = requireNotNull((provider.itemsFlow.value.single() as StandardMenuItem).onClickEvent)

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
    fun `WHEN clicking the menu item THEN record that it was used`() = runTest {
        listOf(null, "group").forEach { tabGroupId ->
            val configuration = FakeTabGroupsFeatureDiscoveryConfiguration()
            val menu = FakeMenuHost()
            val provider =
                provider(snapshotWithSelectedTab(tabGroupId = tabGroupId), discoveryConfiguration = configuration)
            val event = requireNotNull((provider.itemsFlow.value.single() as StandardMenuItem).onClickEvent)

            provider.onEvent(event, menu)

            assertEquals(1, configuration.menuItemUsedCount, "Expected a use recorded when tabGroupId=$tabGroupId")
            assertEquals(0, configuration.menuItemExposureCount)
        }
    }

    @Test
    fun `WHEN the menu item is shown THEN record an exposure without navigating`() = runTest {
        listOf(null, "group").forEach { tabGroupId ->
            val configuration = FakeTabGroupsFeatureDiscoveryConfiguration()
            val menu = FakeMenuHost()
            val provider =
                provider(snapshotWithSelectedTab(tabGroupId = tabGroupId), discoveryConfiguration = configuration)
            val event = requireNotNull((provider.itemsFlow.value.single() as StandardMenuItem).onShownEvent)

            provider.onEvent(event, menu)

            assertEquals(1, configuration.menuItemExposureCount, "Expected an exposure when tabGroupId=$tabGroupId")
            assertEquals(0, configuration.menuItemUsedCount)
            assertEquals(null, menu.directions)
        }
    }

    @Test
    fun `WHEN the menu item is shown several times in the same menu THEN record only one exposure`() = runTest {
        val configuration = FakeTabGroupsFeatureDiscoveryConfiguration()
        val provider = provider(snapshotWithSelectedTab(tabGroupId = null), discoveryConfiguration = configuration)
        val event = requireNotNull((provider.itemsFlow.value.single() as StandardMenuItem).onShownEvent)

        repeat(3) { provider.onEvent(event, menu) }

        assertEquals(1, configuration.menuItemExposureCount)
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
        discoveryConfiguration: FakeTabGroupsFeatureDiscoveryConfiguration =
            this@TabGroupMenuItemProviderTest.discoveryConfiguration,
    ) = provider(FakeTabDataCoordinator(snapshot), target, isEnabled, discoveryConfiguration)

    private fun TestScope.provider(
        coordinator: FakeTabDataCoordinator,
        target: MenuTarget = MenuTarget.BrowserTab,
        isEnabled: Boolean = true,
        discoveryConfiguration: FakeTabGroupsFeatureDiscoveryConfiguration =
            this@TabGroupMenuItemProviderTest.discoveryConfiguration,
    ) =
        TabGroupMenuItemProvider(
            tabDataCoordinator = coordinator,
            discoveryConfiguration = discoveryConfiguration,
            target = target,
            isVisible = isEnabled,
            scope = backgroundScope,
        )

    private fun highlighted(isHighlighted: Boolean) =
        FakeTabGroupsFeatureDiscoveryConfiguration(shouldHighlightMenuItem = isHighlighted)

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

    private fun expectedAddItem(isHighlighted: Boolean = false) =
        expectedItem(
            title = R.string.browser_menu_add_to_tab_group,
            entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = TAB_ID),
            isHighlighted = isHighlighted,
        )

    private fun expectedMoveItem(isHighlighted: Boolean = false) =
        expectedItem(
            title = R.string.browser_menu_move_to_tab_group,
            entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = TAB_ID),
            isHighlighted = isHighlighted,
        )

    private fun expectedItem(
        title: Int,
        entryPoint: TabGroupFlowEntryPoint,
        isHighlighted: Boolean,
    ) =
        StandardMenuItem(
            title = MenuItemTitle(Text.Resource(title)),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_tab_group_24, isHighlighted = isHighlighted),
            showNewIndicator = isHighlighted,
            onClickEvent = MenuAction.Navigate.OpenTabGroupFlow(entryPoint),
            onShownEvent = MenuAction.OnTabGroupMenuItemExposed,
        )

    private companion object {
        const val TAB_ID = "tab"
    }
}
