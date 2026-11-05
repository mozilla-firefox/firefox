/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.tabdata.coordinator

import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.tabdata.coordinator.data.Tab
import mozilla.components.feature.tabdata.coordinator.data.TabGroup
import mozilla.components.feature.tabdata.coordinator.data.TabsSnapshot
import mozilla.components.feature.tabdata.coordinator.data.tabIdsInTabGroup
import mozilla.components.feature.tabdata.coordinator.fakes.FakeTabRepository
import mozilla.components.feature.tabdata.coordinator.repository.TabRepository
import mozilla.components.feature.tabgroups.storage.data.TabGroup as StoredTabGroup
import mozilla.components.feature.tabgroups.storage.data.TabGroupData
import mozilla.components.feature.tabgroups.storage.fakes.FakeTabGroupRepository
import mozilla.components.feature.tabgroups.storage.repository.TabGroupRepository
import mozilla.components.support.utils.FakeDateTimeProvider
import org.junit.Test

private const val CURRENT_TIME = 100L

@OptIn(ExperimentalCoroutinesApi::class)
class DefaultTabDataCoordinatorTest {

    @Test
    fun `WHEN the coordinator has not been initialized THEN the tabs state is null`() = runTest {
        val coordinator = createCoordinator(tabRepository = createTabRepository(tabs = listOf(createTab(id = "1"))))

        runCurrent()

        assertNull(coordinator.tabsSnapshot.value)
    }

    @Test
    fun `WHEN the coordinator is initialized THEN the tabs state is emitted`() = runTest {
        val tab = createTab(id = "1")
        val coordinator =
            createCoordinator(tabRepository = createTabRepository(selectedTabId = "1", tabs = listOf(tab)))

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected = TabsSnapshot(selectedTab = tab, normalTabs = listOf(tab), browsableNormalTabs = listOf(tab)),
            actual = coordinator.tabsSnapshot.value,
        )
    }

    @Test
    fun `WHEN a tab is private THEN it is sorted into the private tabs`() = runTest {
        val normalTab = createTab(id = "1")
        val privateTab = createTab(id = "2", private = true)
        val coordinator = createCoordinator(tabRepository = createTabRepository(tabs = listOf(normalTab, privateTab)))

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected =
                TabsSnapshot(
                    normalTabs = listOf(normalTab),
                    browsableNormalTabs = listOf(normalTab),
                    privateTabs = listOf(privateTab),
                ),
            actual = coordinator.tabsSnapshot.value,
        )
    }

    @Test
    fun `GIVEN a private tab is selected THEN it is the selected tab`() = runTest {
        val normalTab = createTab(id = "1")
        val privateTab = createTab(id = "2", private = true)
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(selectedTabId = privateTab.id, tabs = listOf(normalTab, privateTab))
            )

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected =
                TabsSnapshot(
                    selectedTab = privateTab,
                    normalTabs = listOf(normalTab),
                    browsableNormalTabs = listOf(normalTab),
                    privateTabs = listOf(privateTab),
                ),
            actual = coordinator.tabsSnapshot.value,
        )
    }

    @Test
    fun `GIVEN inactive tabs are enabled WHEN a tab is inactive THEN it is sorted into the inactive tabs`() = runTest {
        val normalTab = createTab(id = "1")
        val inactiveTab = createTab(id = "2", inactive = true)
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(normalTab, inactiveTab)),
                isInactiveTabsEnabled = { true },
            )

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected =
                TabsSnapshot(
                    normalTabs = listOf(normalTab, inactiveTab),
                    browsableNormalTabs = listOf(normalTab),
                    inactiveTabs = listOf(inactiveTab),
                    inactiveTabsEnabled = true,
                ),
            actual = coordinator.tabsSnapshot.value,
        )
    }

    @Test
    fun `GIVEN inactive tabs are disabled WHEN a tab is inactive THEN it is a browsable normal tab`() = runTest {
        val normalTab = createTab(id = "1")
        val inactiveTab = createTab(id = "2", inactive = true)
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(normalTab, inactiveTab)),
                isInactiveTabsEnabled = { false },
            )

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected =
                TabsSnapshot(
                    normalTabs = listOf(normalTab, inactiveTab),
                    browsableNormalTabs = listOf(normalTab, inactiveTab),
                    inactiveTabs = emptyList(),
                    inactiveTabsEnabled = false,
                ),
            actual = coordinator.tabsSnapshot.value,
        )
    }

    @Test
    fun `WHEN a tab is assigned to a group THEN the tab group ID is resolved onto the tab`() = runTest {
        val groupedTab = createTab(id = "1")
        val ungroupedTab = createTab(id = "2")
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf(groupedTab.id))
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(groupedTab, ungroupedTab)),
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(group),
                        tabGroupAssignments = mapOf(groupedTab.id to group.id),
                    ),
            )

        coordinator.initialize()
        runCurrent()

        val expectedGroupedTab = groupedTab.copy(tabGroupId = group.id)
        assertEquals(
            expected =
                TabsSnapshot(
                    normalTabs = listOf(expectedGroupedTab, ungroupedTab),
                    browsableNormalTabs = listOf(expectedGroupedTab, ungroupedTab),
                    tabGroups = listOf(createTabGroup(id = group.id, tabIds = group.tabIds)),
                    openTabGroups = listOf(createTabGroup(id = group.id, tabIds = group.tabIds)),
                ),
            actual = coordinator.tabsSnapshot.value,
        )
    }

    @Test
    fun `GIVEN a grouped tab is selected THEN the selected tab has its tab group ID`() = runTest {
        val groupedTab = createTab(id = "1")
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf(groupedTab.id))
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(selectedTabId = groupedTab.id, tabs = listOf(groupedTab)),
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(group),
                        tabGroupAssignments = mapOf(groupedTab.id to group.id),
                    ),
            )

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected = groupedTab.copy(tabGroupId = group.id),
            actual = coordinator.tabsSnapshot.value?.selectedTab,
        )
    }

    @Test
    fun `GIVEN a tab group references a tab that no longer exists THEN the tab group excludes that tab ID`() = runTest {
        val existingTab = createTab(id = "1")
        val staleTabId = "stale-tab"
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf(existingTab.id, staleTabId))
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(existingTab)),
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(group),
                        tabGroupAssignments = mapOf(existingTab.id to group.id),
                    ),
            )

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected = listOf(existingTab.id),
            actual = coordinator.tabsSnapshot.value?.tabGroups?.single()?.tabIds,
        )
    }

    @Test
    fun `GIVEN a tab group's tab IDs all no longer exist THEN the tab group has an empty tabIds list`() = runTest {
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf("stale-1", "stale-2"))
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(createTab(id = "1"))),
                tabGroupRepository = createTabGroupRepository(tabGroups = listOf(group)),
            )

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected = emptyList<String>(),
            actual = coordinator.tabsSnapshot.value?.tabGroups?.single()?.tabIds,
        )
    }

    @Test
    fun `WHEN a selected grouped tab closes before its assignment is deleted THEN the selected tab is ungrouped`() =
        runTest {
            val groupedTab = createTab(id = "1")
            val ungroupedTab = createTab(id = "2")
            val group = createStoredTabGroup(id = "group-1", tabIds = listOf(groupedTab.id))
            val tabRepository =
                createTabRepository(selectedTabId = groupedTab.id, tabs = listOf(groupedTab, ungroupedTab))
            val coordinator =
                createCoordinator(
                    tabRepository = tabRepository,
                    tabGroupRepository =
                        createTabGroupRepository(
                            tabGroups = listOf(group),
                            tabGroupAssignments = mapOf(groupedTab.id to group.id),
                        ),
                )

            coordinator.initialize()
            runCurrent()

            tabRepository.emit(TabRepository.Data(selectedTabId = ungroupedTab.id, tabs = listOf(ungroupedTab)))
            runCurrent()

            val snapshot = coordinator.tabsSnapshot.value
            assertEquals(expected = ungroupedTab, actual = snapshot?.selectedTab)
            assertEquals(expected = listOf(ungroupedTab), actual = snapshot?.normalTabs)
            assertEquals(expected = emptyList(), actual = snapshot?.tabGroups?.single()?.tabIds)
        }

    @Test
    fun `GIVEN a tab group is closed WHEN a tab belongs to it THEN it is not a browsable normal tab`() = runTest {
        val groupedTab = createTab(id = "1")
        val ungroupedTab = createTab(id = "2")
        val group = createStoredTabGroup(id = "group-1", closed = true, tabIds = listOf(groupedTab.id))
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(groupedTab, ungroupedTab)),
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(group),
                        tabGroupAssignments = mapOf(groupedTab.id to group.id),
                    ),
            )

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected =
                TabsSnapshot(
                    normalTabs = listOf(groupedTab.copy(tabGroupId = group.id), ungroupedTab),
                    browsableNormalTabs = listOf(ungroupedTab),
                    tabGroups = listOf(createTabGroup(id = group.id, closed = true, tabIds = group.tabIds)),
                ),
            actual = coordinator.tabsSnapshot.value,
        )
    }

    @Test
    fun `GIVEN tab groups are disabled WHEN a tab is grouped THEN the group data is ignored`() = runTest {
        val groupedTab = createTab(id = "1")
        val group = createStoredTabGroup(id = "group-1", closed = true, tabIds = listOf(groupedTab.id))
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(groupedTab)),
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(group),
                        tabGroupAssignments = mapOf(groupedTab.id to group.id),
                    ),
                isTabGroupsEnabled = { false },
            )

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected =
                TabsSnapshot(
                    normalTabs = listOf(groupedTab),
                    browsableNormalTabs = listOf(groupedTab),
                    tabGroupsEnabled = false,
                    tabGroups = emptyList(),
                ),
            actual = coordinator.tabsSnapshot.value,
        )
    }

    @Test
    fun `GIVEN tab groups are disabled THEN the tabs in closed groups are browsable`() = runTest {
        val groupedTab = createTab(id = "1")
        val group = createStoredTabGroup(id = "group-1", closed = true, tabIds = listOf(groupedTab.id))
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(groupedTab)),
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(group),
                        tabGroupAssignments = mapOf(groupedTab.id to group.id),
                    ),
                isTabGroupsEnabled = { false },
            )

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected = listOf(groupedTab),
            actual = coordinator.tabsSnapshot.value?.browsableNormalTabs,
        )
    }

    @Test
    fun `GIVEN tab groups are enabled THEN the tabs in closed groups are not browsable`() = runTest {
        val groupedTab = createTab(id = "1")
        val group = createStoredTabGroup(id = "group-1", closed = true, tabIds = listOf(groupedTab.id))
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(groupedTab)),
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(group),
                        tabGroupAssignments = mapOf(groupedTab.id to group.id),
                    ),
                isTabGroupsEnabled = { true },
            )

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected = emptyList<Tab>(),
            actual = coordinator.tabsSnapshot.value?.browsableNormalTabs,
        )
    }

    @Test
    fun `GIVEN an inactive tab is in an open tab group THEN its ID still appears in the tab group`() = runTest {
        val inactiveTab = createTab(id = "1", inactive = true)
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf(inactiveTab.id))
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(inactiveTab)),
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(group),
                        tabGroupAssignments = mapOf(inactiveTab.id to group.id),
                    ),
            )

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected = listOf(inactiveTab.id),
            actual = coordinator.tabsSnapshot.value?.tabGroups?.single()?.tabIds,
        )
    }

    @Test
    fun `GIVEN an inactive tab is in a closed tab group THEN its ID still appears in the tab group`() = runTest {
        val inactiveTab = createTab(id = "1", inactive = true)
        val group = createStoredTabGroup(id = "group-1", closed = true, tabIds = listOf(inactiveTab.id))
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(inactiveTab)),
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(group),
                        tabGroupAssignments = mapOf(inactiveTab.id to group.id),
                    ),
            )

        coordinator.initialize()
        runCurrent()

        assertEquals(
            expected = listOf(inactiveTab.id),
            actual = coordinator.tabsSnapshot.value?.tabGroups?.single()?.tabIds,
        )
    }

    @Test
    fun `GIVEN an inactive tab is in an open or closed tab group THEN it still appears in the inactive tabs`() =
        runTest {
            val openGroupTab = createTab(id = "1", inactive = true)
            val closedGroupTab = createTab(id = "2", inactive = true)
            val openGroup = createStoredTabGroup(id = "open-group", tabIds = listOf(openGroupTab.id))
            val closedGroup =
                createStoredTabGroup(id = "closed-group", closed = true, tabIds = listOf(closedGroupTab.id))
            val coordinator =
                createCoordinator(
                    tabRepository = createTabRepository(tabs = listOf(openGroupTab, closedGroupTab)),
                    tabGroupRepository =
                        createTabGroupRepository(
                            tabGroups = listOf(openGroup, closedGroup),
                            tabGroupAssignments =
                                mapOf(openGroupTab.id to openGroup.id, closedGroupTab.id to closedGroup.id),
                        ),
                )

            coordinator.initialize()
            runCurrent()

            val expectedOpenGroupTab = openGroupTab.copy(tabGroupId = openGroup.id)
            val expectedClosedGroupTab = closedGroupTab.copy(tabGroupId = closedGroup.id)
            assertEquals(
                expected = listOf(expectedOpenGroupTab, expectedClosedGroupTab),
                actual = coordinator.tabsSnapshot.value?.inactiveTabs,
            )
        }

    @Test
    fun `WHEN the tab repository emits new data THEN the tabs state is updated`() = runTest {
        val tab = createTab(id = "1")
        val addedTab = createTab(id = "2")
        val tabRepository = createTabRepository(selectedTabId = tab.id, tabs = listOf(tab))
        val coordinator = createCoordinator(tabRepository = tabRepository)

        coordinator.initialize()
        runCurrent()

        tabRepository.update { data -> data.copy(tabs = data.tabs + addedTab) }
        runCurrent()

        assertEquals(
            expected =
                TabsSnapshot(
                    selectedTab = tab,
                    normalTabs = listOf(tab, addedTab),
                    browsableNormalTabs = listOf(tab, addedTab),
                ),
            actual = coordinator.tabsSnapshot.value,
        )
    }

    @Test
    fun `WHEN the tab group repository emits new data THEN the tabs state is updated`() = runTest {
        val tab = createTab(id = "1")
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf(tab.id))
        val tabGroupRepository = createTabGroupRepository()
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(tab)),
                tabGroupRepository = tabGroupRepository,
            )

        coordinator.initialize()
        runCurrent()

        tabGroupRepository.createTabGroupWithTabs(tabGroup = group)
        runCurrent()

        val expectedTab = tab.copy(tabGroupId = group.id)
        assertEquals(
            expected =
                TabsSnapshot(
                    normalTabs = listOf(expectedTab),
                    browsableNormalTabs = listOf(expectedTab),
                    tabGroups = listOf(createTabGroup(id = group.id, tabIds = group.tabIds)),
                    openTabGroups = listOf(createTabGroup(id = group.id, tabIds = group.tabIds)),
                ),
            actual = coordinator.tabsSnapshot.value,
        )
    }

    @Test
    fun `WHEN a tab group is created THEN its tabs are assigned and sequenced next to each other`() = runTest {
        val tabRepository =
            createTabRepository(tabs = listOf(createTab(id = "1"), createTab(id = "2"), createTab(id = "3")))
        val tabGroupRepository = createTabGroupRepository()
        val coordinator = createCoordinator(tabRepository = tabRepository, tabGroupRepository = tabGroupRepository)

        coordinator.initialize()
        runCurrent()

        val groupId = coordinator.createTabGroup(title = "Group", theme = "Red", tabIds = listOf("1", "3"))
        runCurrent()

        assertEquals(expected = listOf("1", "3", "2"), actual = tabRepository.tabs.map { it.id })
        assertEquals(
            expected = listOf(groupId, groupId, null),
            actual = coordinator.tabsSnapshot.value?.normalTabs?.map { it.tabGroupId },
        )
        assertEquals(
            expected =
                TabGroup(
                    id = groupId,
                    title = "Group",
                    theme = "Red",
                    closed = false,
                    lastModified = CURRENT_TIME,
                    tabIds = listOf("1", "3"),
                ),
            actual = coordinator.tabsSnapshot.value?.tabGroups?.single(),
        )
    }

    @Test
    fun `GIVEN an anchor tab WHEN a tab group is created THEN its tabs are sequenced after the anchor`() = runTest {
        val tabRepository =
            createTabRepository(tabs = listOf(createTab(id = "1"), createTab(id = "2"), createTab(id = "3")))
        val coordinator = createCoordinator(tabRepository = tabRepository)

        coordinator.initialize()
        runCurrent()

        coordinator.createTabGroup(title = "Group", theme = "Red", tabIds = listOf("1", "3"), anchorTabId = "3")
        runCurrent()

        assertEquals(expected = listOf("2", "3", "1"), actual = tabRepository.tabs.map { it.id })
    }

    @Test
    fun `WHEN a tab group is created without tabs THEN no tabs are moved`() = runTest {
        val tabRepository = createTabRepository(tabs = listOf(createTab(id = "1"), createTab(id = "2")))
        val coordinator = createCoordinator(tabRepository = tabRepository)

        coordinator.initialize()
        runCurrent()

        val groupId = coordinator.createTabGroup(title = "Group", theme = "Red", tabIds = emptyList())
        runCurrent()

        assertEquals(expected = listOf("1", "2"), actual = tabRepository.tabs.map { it.id })
        assertEquals(expected = groupId, actual = coordinator.tabsSnapshot.value?.tabGroups?.single()?.id)
        assertEquals(
            expected = listOf(null, null),
            actual = coordinator.tabsSnapshot.value?.normalTabs?.map { it.tabGroupId },
        )
    }

    @Test
    fun `WHEN a tab group is updated THEN its title, theme, and lastModified change and its closed state is preserved`() =
        runTest {
            val group = createStoredTabGroup(id = "group-1", closed = true, tabIds = listOf("1"))
            val coordinator =
                createCoordinator(
                    tabRepository = createTabRepository(tabs = listOf(createTab(id = "1"))),
                    tabGroupRepository =
                        createTabGroupRepository(
                            tabGroups = listOf(group),
                            tabGroupAssignments = mapOf("1" to group.id),
                        ),
                )

            coordinator.initialize()
            runCurrent()

            coordinator.updateTabGroup(tabGroupId = group.id, title = "Renamed", theme = "Blue")
            runCurrent()

            assertEquals(
                expected =
                    TabGroup(
                        id = group.id,
                        title = "Renamed",
                        theme = "Blue",
                        closed = true,
                        lastModified = CURRENT_TIME,
                        tabIds = listOf("1"),
                    ),
                actual = coordinator.tabsSnapshot.value?.tabGroups?.single(),
            )
        }

    @Test
    fun `WHEN tabs are added to a tab group THEN they are sequenced after the group's last tab`() = runTest {
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf("1"))
        val tabRepository =
            createTabRepository(
                tabs = listOf(createTab(id = "1"), createTab(id = "2"), createTab(id = "3"), createTab(id = "4"))
            )
        val coordinator =
            createCoordinator(
                tabRepository = tabRepository,
                tabGroupRepository =
                    createTabGroupRepository(tabGroups = listOf(group), tabGroupAssignments = mapOf("1" to group.id)),
            )

        coordinator.initialize()
        runCurrent()

        coordinator.addTabsToTabGroup(tabGroupId = group.id, tabIds = listOf("4"))
        runCurrent()

        assertEquals(expected = listOf("1", "4", "2", "3"), actual = tabRepository.tabs.map { it.id })
        assertEquals(
            expected = listOf("1", "4"),
            actual = coordinator.tabsSnapshot.value?.tabIdsInTabGroup(tabGroupId = group.id),
        )
    }

    @Test
    fun `WHEN a group is merged into a tab THEN the group's tabs are moved to the tab's position`() = runTest {
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf("3"))
        val tabRepository =
            createTabRepository(tabs = listOf(createTab(id = "1"), createTab(id = "2"), createTab(id = "3")))
        val coordinator =
            createCoordinator(
                tabRepository = tabRepository,
                tabGroupRepository =
                    createTabGroupRepository(tabGroups = listOf(group), tabGroupAssignments = mapOf("3" to group.id)),
            )

        coordinator.initialize()
        runCurrent()

        coordinator.mergeTabGroupIntoTab(tabGroupId = group.id, tabId = "1")
        runCurrent()

        assertEquals(expected = listOf("3", "1", "2"), actual = tabRepository.tabs.map { it.id })
        assertEquals(
            expected = listOf("3", "1"),
            actual = coordinator.tabsSnapshot.value?.tabIdsInTabGroup(tabGroupId = group.id),
        )
    }

    @Test
    fun `WHEN tab groups are merged THEN the source tabs join the destination and the source group is deleted`() =
        runTest {
            val destinationGroup = createStoredTabGroup(id = "destination", tabIds = listOf("1"))
            val sourceGroup = createStoredTabGroup(id = "source", tabIds = listOf("3", "4"))
            val tabRepository =
                createTabRepository(
                    tabs = listOf(createTab(id = "1"), createTab(id = "2"), createTab(id = "3"), createTab(id = "4"))
                )
            val coordinator =
                createCoordinator(
                    tabRepository = tabRepository,
                    tabGroupRepository =
                        createTabGroupRepository(
                            tabGroups = listOf(destinationGroup, sourceGroup),
                            tabGroupAssignments =
                                mapOf("1" to destinationGroup.id, "3" to sourceGroup.id, "4" to sourceGroup.id),
                        ),
                )

            coordinator.initialize()
            runCurrent()

            coordinator.mergeTabGroups(
                sourceTabGroupId = sourceGroup.id,
                destinationTabGroupId = destinationGroup.id,
            )
            runCurrent()

            assertEquals(expected = listOf("1", "3", "4", "2"), actual = tabRepository.tabs.map { it.id })
            assertEquals(
                expected = destinationGroup.id,
                actual = coordinator.tabsSnapshot.value?.tabGroups?.single()?.id,
            )
            assertEquals(
                expected = listOf("1", "3", "4"),
                actual = coordinator.tabsSnapshot.value?.tabIdsInTabGroup(tabGroupId = destinationGroup.id),
            )
        }

    @Test
    fun `WHEN a tab group is deleted THEN its tabs are closed and inactive tabs are excluded from selection`() =
        runTest {
            val group = createStoredTabGroup(id = "group-1", tabIds = listOf("1", "3"))
            val tabRepository =
                createTabRepository(
                    tabs = listOf(createTab(id = "1"), createTab(id = "2", inactive = true), createTab(id = "3"))
                )
            val coordinator =
                createCoordinator(
                    tabRepository = tabRepository,
                    tabGroupRepository =
                        createTabGroupRepository(
                            tabGroups = listOf(group),
                            tabGroupAssignments = mapOf("1" to group.id, "3" to group.id),
                        ),
                    isInactiveTabsEnabled = { true },
                )

            coordinator.initialize()
            runCurrent()

            coordinator.deleteTabGroupAndTabs(tabGroupId = group.id)
            runCurrent()

            assertEquals(expected = listOf("2"), actual = tabRepository.tabs.map { it.id })
            assertEquals(expected = setOf("2"), actual = tabRepository.excludedFallbackTabIds)
            assertEquals(expected = emptyList(), actual = coordinator.tabsSnapshot.value?.tabGroups)
        }

    @Test
    fun `GIVEN inactive tabs are disabled WHEN a tab group is deleted THEN no tabs are excluded from selection`() =
        runTest {
            val group = createStoredTabGroup(id = "group-1", tabIds = listOf("1"))
            val tabRepository =
                createTabRepository(tabs = listOf(createTab(id = "1"), createTab(id = "2", inactive = true)))
            val coordinator =
                createCoordinator(
                    tabRepository = tabRepository,
                    tabGroupRepository =
                        createTabGroupRepository(
                            tabGroups = listOf(group),
                            tabGroupAssignments = mapOf("1" to group.id),
                        ),
                    isInactiveTabsEnabled = { false },
                )

            coordinator.initialize()
            runCurrent()

            coordinator.deleteTabGroupAndTabs(tabGroupId = group.id)
            runCurrent()

            assertEquals(expected = emptySet(), actual = tabRepository.excludedFallbackTabIds)
        }

    @Test
    fun `WHEN tab groups are deleted THEN their tabs stay open`() = runTest {
        val firstGroup = createStoredTabGroup(id = "group-1", tabIds = listOf("1"))
        val secondGroup = createStoredTabGroup(id = "group-2", tabIds = listOf("2"))
        val tabRepository = createTabRepository(tabs = listOf(createTab(id = "1"), createTab(id = "2")))
        val coordinator =
            createCoordinator(
                tabRepository = tabRepository,
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(firstGroup, secondGroup),
                        tabGroupAssignments = mapOf("1" to firstGroup.id, "2" to secondGroup.id),
                    ),
            )

        coordinator.initialize()
        runCurrent()

        coordinator.deleteTabGroupsById(tabGroupIds = listOf(firstGroup.id, secondGroup.id))
        runCurrent()

        assertEquals(expected = listOf("1", "2"), actual = tabRepository.tabs.map { it.id })
        assertEquals(expected = emptyList(), actual = coordinator.tabsSnapshot.value?.tabGroups)
    }

    @Test
    fun `WHEN a tab group is ungrouped THEN the group is deleted and its tabs stay open`() = runTest {
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf("1"))
        val tabRepository = createTabRepository(tabs = listOf(createTab(id = "1")))
        val coordinator =
            createCoordinator(
                tabRepository = tabRepository,
                tabGroupRepository =
                    createTabGroupRepository(tabGroups = listOf(group), tabGroupAssignments = mapOf("1" to group.id)),
            )

        coordinator.initialize()
        runCurrent()

        coordinator.ungroupTabGroup(tabGroupId = group.id)
        runCurrent()

        assertEquals(expected = listOf("1"), actual = tabRepository.tabs.map { it.id })
        assertEquals(expected = emptyList(), actual = coordinator.tabsSnapshot.value?.tabGroups)
        assertNull(coordinator.tabsSnapshot.value?.normalTabs?.single()?.tabGroupId)
    }

    @Test
    fun `WHEN a tab group is ungrouped THEN the order of the tabs is unchanged`() = runTest {
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf("2", "3"))
        val tabRepository =
            createTabRepository(
                tabs = listOf(createTab(id = "1"), createTab(id = "2"), createTab(id = "3"), createTab(id = "4"))
            )
        val coordinator =
            createCoordinator(
                tabRepository = tabRepository,
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(group),
                        tabGroupAssignments = mapOf("2" to group.id, "3" to group.id),
                    ),
            )

        coordinator.initialize()
        runCurrent()

        coordinator.ungroupTabGroup(tabGroupId = group.id)
        runCurrent()

        val expectedTabIds = listOf("1", "2", "3", "4")
        assertEquals(expected = expectedTabIds, actual = tabRepository.tabs.map { it.id })
        assertEquals(expected = expectedTabIds, actual = coordinator.tabsSnapshot.value?.normalTabs?.map { it.id })
    }

    @Test
    fun `WHEN a tab group is closed THEN the group is closed and its lastModified is updated`() = runTest {
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf("1"))
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(createTab(id = "1"))),
                tabGroupRepository =
                    createTabGroupRepository(tabGroups = listOf(group), tabGroupAssignments = mapOf("1" to group.id)),
            )

        coordinator.initialize()
        runCurrent()

        coordinator.closeTabGroup(tabGroupId = group.id)
        runCurrent()

        assertEquals(expected = true, actual = coordinator.tabsSnapshot.value?.tabGroups?.single()?.closed)
        assertEquals(
            expected = CURRENT_TIME,
            actual = coordinator.tabsSnapshot.value?.tabGroups?.single()?.lastModified,
        )
        assertEquals(expected = emptyList(), actual = coordinator.tabsSnapshot.value?.openTabGroups)
        assertEquals(expected = listOf("1"), actual = coordinator.tabsSnapshot.value?.normalTabs?.map { it.id })
    }

    @Test
    fun `WHEN a tab group is opened THEN the group is open and its lastModified is updated`() = runTest {
        val group = createStoredTabGroup(id = "group-1", closed = true, tabIds = listOf("1"))
        val coordinator =
            createCoordinator(
                tabRepository = createTabRepository(tabs = listOf(createTab(id = "1"))),
                tabGroupRepository =
                    createTabGroupRepository(tabGroups = listOf(group), tabGroupAssignments = mapOf("1" to group.id)),
            )

        coordinator.initialize()
        runCurrent()

        coordinator.openTabGroup(tabGroupId = group.id)
        runCurrent()

        assertEquals(expected = false, actual = coordinator.tabsSnapshot.value?.tabGroups?.single()?.closed)
        assertEquals(
            expected = CURRENT_TIME,
            actual = coordinator.tabsSnapshot.value?.tabGroups?.single()?.lastModified,
        )
        assertEquals(expected = 1, actual = coordinator.tabsSnapshot.value?.openTabGroups?.size)
    }

    @Test
    fun `WHEN a grouped tab is closed and there are other tabs remaining in the group THEN the tab is closed`() =
        runTest {
            val group = createStoredTabGroup(id = "group-1", tabIds = listOf("1", "2"))
            val tabRepository = createTabRepository(tabs = listOf(createTab(id = "1"), createTab(id = "2")))
            val coordinator =
                createCoordinator(
                    tabRepository = tabRepository,
                    tabGroupRepository =
                        createTabGroupRepository(
                            tabGroups = listOf(group),
                            tabGroupAssignments = mapOf("1" to group.id, "2" to group.id),
                        ),
                )

            coordinator.initialize()
            runCurrent()

            coordinator.removeTabFromTabGroup(tabGroupId = group.id, tabId = "1")
            runCurrent()

            assertEquals(expected = listOf("2"), actual = tabRepository.tabs.map { it.id })
        }

    @Test
    fun `WHEN the last tab of a tab group is removed THEN the tab stays open`() = runTest {
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf("1"))
        val tabRepository = createTabRepository(tabs = listOf(createTab(id = "1")))
        val coordinator =
            createCoordinator(
                tabRepository = tabRepository,
                tabGroupRepository =
                    createTabGroupRepository(tabGroups = listOf(group), tabGroupAssignments = mapOf("1" to group.id)),
            )

        coordinator.initialize()
        runCurrent()

        coordinator.removeTabFromTabGroup(tabGroupId = group.id, tabId = "1")
        runCurrent()

        assertEquals(expected = listOf("1"), actual = tabRepository.tabs.map { it.id })
    }

    @Test
    fun `WHEN a tab is reordered next to another tab THEN it is moved`() = runTest {
        val tabRepository =
            createTabRepository(tabs = listOf(createTab(id = "1"), createTab(id = "2"), createTab(id = "3")))
        val coordinator = createCoordinator(tabRepository = tabRepository)

        coordinator.initialize()
        runCurrent()

        coordinator.reorderItem(sourceId = "1", destinationId = "3", placeAfter = true)
        runCurrent()

        assertEquals(expected = listOf("2", "3", "1"), actual = tabRepository.tabs.map { it.id })
    }

    @Test
    fun `WHEN an item is reordered without a destination THEN nothing is moved`() = runTest {
        val tabRepository = createTabRepository(tabs = listOf(createTab(id = "1"), createTab(id = "2")))
        val coordinator = createCoordinator(tabRepository = tabRepository)

        coordinator.initialize()
        runCurrent()

        coordinator.reorderItem(sourceId = "2", destinationId = null, placeAfter = true)
        runCurrent()

        assertEquals(expected = listOf("1", "2"), actual = tabRepository.tabs.map { it.id })
    }

    @Test
    fun `WHEN an item is reordered to its current location THEN nothing is moved`() = runTest {
        val tabRepository = createTabRepository(tabs = listOf(createTab(id = "1"), createTab(id = "2")))
        val coordinator = createCoordinator(tabRepository = tabRepository)

        coordinator.initialize()
        runCurrent()

        coordinator.reorderItem(sourceId = "2", destinationId = "2", placeAfter = true)
        runCurrent()

        assertEquals(expected = listOf("1", "2"), actual = tabRepository.tabs.map { it.id })
    }

    @Test
    fun `WHEN a tab group is reordered next to a tab THEN all of its tabs are moved`() = runTest {
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf("1", "2"))
        val tabRepository =
            createTabRepository(
                tabs = listOf(createTab(id = "1"), createTab(id = "2"), createTab(id = "3"), createTab(id = "4"))
            )
        val coordinator =
            createCoordinator(
                tabRepository = tabRepository,
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(group),
                        tabGroupAssignments = mapOf("1" to group.id, "2" to group.id),
                    ),
            )

        coordinator.initialize()
        runCurrent()

        coordinator.reorderItem(sourceId = group.id, destinationId = "4", placeAfter = true)
        runCurrent()

        assertEquals(expected = listOf("3", "4", "1", "2"), actual = tabRepository.tabs.map { it.id })
    }

    @Test
    fun `WHEN a tab is reordered next to a tab group THEN it is anchored to the group's edge`() = runTest {
        val group = createStoredTabGroup(id = "group-1", tabIds = listOf("2", "3"))
        val tabRepository =
            createTabRepository(
                tabs = listOf(createTab(id = "1"), createTab(id = "2"), createTab(id = "3"), createTab(id = "4"))
            )
        val coordinator =
            createCoordinator(
                tabRepository = tabRepository,
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(group),
                        tabGroupAssignments = mapOf("2" to group.id, "3" to group.id),
                    ),
            )

        coordinator.initialize()
        runCurrent()

        coordinator.reorderItem(sourceId = "4", destinationId = group.id, placeAfter = false)
        runCurrent()

        assertEquals(expected = listOf("1", "4", "2", "3"), actual = tabRepository.tabs.map { it.id })

        coordinator.reorderItem(sourceId = "1", destinationId = group.id, placeAfter = true)
        runCurrent()

        assertEquals(expected = listOf("4", "2", "3", "1"), actual = tabRepository.tabs.map { it.id })
    }

    @Test
    fun `WHEN a tab group is reordered next to another tab group THEN it is anchored to the group's edge`() = runTest {
        val sourceGroup = createStoredTabGroup(id = "source", tabIds = listOf("1", "2"))
        val destinationGroup = createStoredTabGroup(id = "destination", tabIds = listOf("3", "4"))
        val tabRepository =
            createTabRepository(
                tabs = listOf(createTab(id = "1"), createTab(id = "2"), createTab(id = "3"), createTab(id = "4"))
            )
        val coordinator =
            createCoordinator(
                tabRepository = tabRepository,
                tabGroupRepository =
                    createTabGroupRepository(
                        tabGroups = listOf(sourceGroup, destinationGroup),
                        tabGroupAssignments =
                            mapOf(
                                "1" to sourceGroup.id,
                                "2" to sourceGroup.id,
                                "3" to destinationGroup.id,
                                "4" to destinationGroup.id,
                            ),
                    ),
            )

        coordinator.initialize()
        runCurrent()

        coordinator.reorderItem(sourceId = sourceGroup.id, destinationId = destinationGroup.id, placeAfter = true)
        runCurrent()

        assertEquals(expected = listOf("3", "4", "1", "2"), actual = tabRepository.tabs.map { it.id })
    }

    @Test
    fun `WHEN an item is reordered next to an empty tab group THEN nothing is moved`() = runTest {
        val group = createStoredTabGroup(id = "group-1")
        val tabRepository = createTabRepository(tabs = listOf(createTab(id = "1"), createTab(id = "2")))
        val coordinator =
            createCoordinator(
                tabRepository = tabRepository,
                tabGroupRepository = createTabGroupRepository(tabGroups = listOf(group)),
            )

        coordinator.initialize()
        runCurrent()

        coordinator.reorderItem(sourceId = "1", destinationId = group.id, placeAfter = true)
        runCurrent()

        assertEquals(expected = listOf("1", "2"), actual = tabRepository.tabs.map { it.id })
    }

    private fun TestScope.createCoordinator(
        tabRepository: TabRepository = createTabRepository(),
        tabGroupRepository: TabGroupRepository = createTabGroupRepository(),
        isInactiveTabsEnabled: () -> Boolean = { true },
        isTabGroupsEnabled: () -> Boolean = { true },
    ): DefaultTabDataCoordinator =
        DefaultTabDataCoordinator(
            tabRepository = tabRepository,
            tabGroupRepository = tabGroupRepository,
            isInactiveTabsEnabled = isInactiveTabsEnabled,
            isTabGroupsEnabled = isTabGroupsEnabled,
            scope = backgroundScope,
            dispatcher = StandardTestDispatcher(testScheduler),
            dateTimeProvider = FakeDateTimeProvider(currentTime = CURRENT_TIME),
        )

    private fun createTabRepository(
        selectedTabId: String? = null,
        tabs: List<Tab> = emptyList(),
    ): FakeTabRepository =
        FakeTabRepository(initialData = TabRepository.Data(selectedTabId = selectedTabId, tabs = tabs))

    private fun createTabGroupRepository(
        tabGroups: List<StoredTabGroup> = emptyList(),
        tabGroupAssignments: Map<String, String> = emptyMap(),
    ): FakeTabGroupRepository =
        FakeTabGroupRepository(
            initialTabGroupData = TabGroupData(tabGroups = tabGroups, tabGroupAssignments = tabGroupAssignments),
            dateTimeProvider = FakeDateTimeProvider(currentTime = CURRENT_TIME),
        )

    private fun createTab(
        id: String,
        private: Boolean = false,
        inactive: Boolean = false,
    ): Tab =
        Tab(
            id = id,
            url = "https://www.mozilla.org/$id",
            title = "Tab $id",
            private = private,
            inactive = inactive,
        )

    private fun createStoredTabGroup(
        id: String,
        closed: Boolean = false,
        tabIds: List<String> = emptyList(),
    ): StoredTabGroup =
        StoredTabGroup(
            id = id,
            title = "Group $id",
            theme = "theme",
            closed = closed,
            lastModified = 0L,
            tabIds = tabIds,
        )

    private fun createTabGroup(
        id: String,
        closed: Boolean = false,
        tabIds: List<String> = emptyList(),
    ): TabGroup =
        TabGroup(
            id = id,
            title = "Group $id",
            theme = "theme",
            closed = closed,
            lastModified = 0L,
            tabIds = tabIds,
        )
}
