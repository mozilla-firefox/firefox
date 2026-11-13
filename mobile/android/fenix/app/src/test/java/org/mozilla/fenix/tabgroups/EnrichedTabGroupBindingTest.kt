/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlinx.coroutines.test.StandardTestDispatcher
import mozilla.components.feature.tabdata.coordinator.data.Tab
import mozilla.components.feature.tabdata.coordinator.data.TabGroup
import mozilla.components.feature.tabdata.coordinator.data.TabsSnapshot
import mozilla.components.feature.tabdata.coordinator.fakes.FakeTabDataCoordinator
import org.junit.Test
import org.mozilla.fenix.tabgroups.flow.TabGroupFlowEntryPoint
import org.mozilla.fenix.tabgroups.flow.TabGroupFlowState
import org.mozilla.fenix.tabgroups.flow.TabGroupFlowStore
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction

class EnrichedTabGroupBindingTest {

    private val testDispatcher = StandardTestDispatcher()
    private val updates = mutableListOf<List<TabsTrayItem.TabGroup>>()
    private val store =
        TabGroupFlowStore(
            initialState = TabGroupFlowState(entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123")),
            middlewares =
                listOf { _, next, action ->
                    if (action is TabGroupAction.TabGroupsUpdated) updates.add(action.tabGroups)
                    next(action)
                },
        )

    @Test
    fun `GIVEN tab data is not loaded yet WHEN the binding starts THEN nothing is emitted`() {
        startBinding(FakeTabDataCoordinator(initialTabsSnapshot = null))

        assertTrue(updates.isEmpty())
    }

    @Test
    fun `WHEN tab data is available THEN groups are emitted with their tabs`() {
        val tab = createTab(id = "1", tabGroupId = GROUP_ID)
        startBinding(FakeTabDataCoordinator(createSnapshot(tabs = listOf(tab), groups = listOf(createTabGroup()))))

        assertEquals(
            expected =
                listOf(
                    TabsTrayItem.TabGroup(
                        id = GROUP_ID,
                        title = GROUP_TITLE,
                        theme = TabGroupTheme.Pink,
                        closed = false,
                        lastModified = LAST_MODIFIED,
                        tabs =
                            listOf(
                                TabsTrayItem.Tab(
                                    id = tab.id,
                                    url = tab.url,
                                    title = tab.title,
                                    inactive = false,
                                    private = false,
                                    icon = null,
                                    isMediaActive = false,
                                    lastAccess = 0L,
                                    isFocused = false,
                                )
                            ),
                    )
                ),
            actual = updates.single(),
        )
    }

    @Test
    fun `WHEN tab data is available THEN the store is updated with the groups`() {
        val tab = createTab(id = "1", tabGroupId = GROUP_ID)
        startBinding(FakeTabDataCoordinator(createSnapshot(tabs = listOf(tab), groups = listOf(createTabGroup()))))

        assertEquals(expected = updates.single(), actual = store.state.groups)
        assertEquals(expected = listOf(GROUP_ID), actual = store.state.groups.map { it.id })
    }

    @Test
    fun `WHEN a group is closed THEN it is still emitted`() {
        val tab = createTab(id = "1", tabGroupId = GROUP_ID)
        startBinding(
            FakeTabDataCoordinator(createSnapshot(tabs = listOf(tab), groups = listOf(createTabGroup(closed = true))))
        )

        val group = updates.single().single()
        assertTrue(group.closed)
        assertEquals(expected = listOf(tab.id), actual = group.tabs.map { it.id })
    }

    @Test
    fun `WHEN the tab groups change THEN the updated groups are emitted`() {
        val firstTab = createTab(id = "1", tabGroupId = GROUP_ID)
        val secondTab = createTab(id = "2")
        val coordinator =
            FakeTabDataCoordinator(
                createSnapshot(tabs = listOf(firstTab, secondTab), groups = listOf(createTabGroup()))
            )
        startBinding(coordinator)

        coordinator.tabsSnapshot.value =
            createSnapshot(
                tabs = listOf(firstTab, secondTab.copy(tabGroupId = GROUP_ID)),
                groups = listOf(createTabGroup()),
            )
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(expected = listOf("1", "2"), actual = updates.last().single().tabs.map { it.id })
    }

    @Test
    fun `WHEN a grouped tab changes THEN the group is emitted with the updated tab`() {
        val tab = createTab(id = "1", tabGroupId = GROUP_ID)
        val coordinator = FakeTabDataCoordinator(createSnapshot(tabs = listOf(tab), groups = listOf(createTabGroup())))
        startBinding(coordinator)

        coordinator.tabsSnapshot.value =
            createSnapshot(tabs = listOf(tab.copy(title = "Updated title")), groups = listOf(createTabGroup()))
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(expected = "Updated title", actual = updates.last().single().tabs.single().title)
    }

    @Test
    fun `WHEN an ungrouped tab changes THEN no new groups are emitted`() {
        val groupedTab = createTab(id = "1", tabGroupId = GROUP_ID)
        val ungroupedTab = createTab(id = "2")
        val coordinator =
            FakeTabDataCoordinator(
                createSnapshot(tabs = listOf(groupedTab, ungroupedTab), groups = listOf(createTabGroup()))
            )
        startBinding(coordinator)
        val emissionsBeforeChange = updates.size

        coordinator.tabsSnapshot.value =
            createSnapshot(
                tabs = listOf(groupedTab, ungroupedTab.copy(title = "Updated title")),
                groups = listOf(createTabGroup()),
            )
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(expected = emissionsBeforeChange, actual = updates.size)
    }

    @Test
    fun `WHEN the binding is stopped THEN later changes are not emitted`() {
        val tab = createTab(id = "1", tabGroupId = GROUP_ID)
        val coordinator = FakeTabDataCoordinator(createSnapshot(tabs = listOf(tab), groups = listOf(createTabGroup())))
        val binding = startBinding(coordinator)
        val emissionsBeforeStop = updates.size

        binding.stop()
        coordinator.tabsSnapshot.value =
            createSnapshot(tabs = listOf(tab.copy(title = "Updated title")), groups = listOf(createTabGroup()))
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(expected = emissionsBeforeStop, actual = updates.size)
    }

    private fun startBinding(coordinator: FakeTabDataCoordinator) =
        EnrichedTabGroupBinding(
                tabDataCoordinator = coordinator,
                store = store,
                computationDispatcher = testDispatcher,
                mainDispatcher = testDispatcher,
            )
            .also {
                it.start()
                testDispatcher.scheduler.advanceUntilIdle()
            }

    private fun createSnapshot(tabs: List<Tab>, groups: List<TabGroup>) =
        TabsSnapshot(
            normalTabs = tabs,
            tabGroups = groups,
            openTabGroups = groups.filterNot { it.closed },
        )

    private fun createTab(id: String, tabGroupId: String? = null) =
        Tab(
            id = id,
            url = "https://www.mozilla.org/$id",
            title = "Tab $id",
            private = false,
            inactive = false,
            tabGroupId = tabGroupId,
        )

    private fun createTabGroup(closed: Boolean = false) =
        TabGroup(
            id = GROUP_ID,
            title = GROUP_TITLE,
            theme = TabGroupTheme.Pink.toStorageValue(),
            closed = closed,
            lastModified = LAST_MODIFIED,
            tabIds = emptyList(),
        )

    private companion object {
        const val GROUP_ID = "group-1"
        const val GROUP_TITLE = "Work"
        const val LAST_MODIFIED = 5L
    }
}
