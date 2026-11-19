/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.tabgroups.storage.fakes

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import mozilla.components.feature.tabgroups.storage.data.TabGroup
import mozilla.components.feature.tabgroups.storage.data.TabGroupData
import mozilla.components.feature.tabgroups.storage.repository.TabGroupRepository
import mozilla.components.support.utils.DateTimeProvider
import mozilla.components.support.utils.FakeDateTimeProvider

/** A fake implementation of [TabGroupRepository] designed exclusively for unit tests. */
class FakeTabGroupRepository(
    initialTabGroupData: TabGroupData = TabGroupData(),
    private val onCreateTabGroupWithTabs: suspend (TabGroup) -> Unit = {},
    private val closeAllTabGroups: () -> Unit = {},
    private val deleteTabGroupAssignmentById: (String) -> Unit = {},
    private val deleteTabGroupAssignmentsById: (List<String>) -> Unit = {},
    private val dateTimeProvider: DateTimeProvider = FakeDateTimeProvider(),
) : TabGroupRepository {

    override val tabGroupDataFlow: Flow<TabGroupData>
        field: MutableStateFlow<TabGroupData> = MutableStateFlow(initialTabGroupData)

    override suspend fun createTabGroupWithTabs(tabGroup: TabGroup) {
        onCreateTabGroupWithTabs(tabGroup)
        val updatedAssignments = HashMap(tabGroupDataFlow.value.tabGroupAssignments)
        tabGroup.tabIds.forEach { id ->
            updatedAssignments[id] = tabGroup.id
        }
        tabGroupDataFlow.emit(
            tabGroupDataFlow.value.copy(
                tabGroups = tabGroupDataFlow.value.tabGroups + tabGroup,
                tabGroupAssignments = updatedAssignments,
            )
        )
    }

    override suspend fun addNewTabGroup(tabGroup: TabGroup) {
        tabGroupDataFlow.emit(tabGroupDataFlow.value.copy(tabGroups = tabGroupDataFlow.value.tabGroups + tabGroup))
    }

    override suspend fun updateTabGroup(tabGroup: TabGroup) {
        val updatedList =
            tabGroupDataFlow.value.tabGroups.map {
                if (it.id == tabGroup.id) {
                    // Tab membership is owned by the assignments, not by the group entry, so it is left untouched.
                    tabGroup.copy(tabIds = it.tabIds)
                } else {
                    it
                }
            }
        tabGroupDataFlow.emit(tabGroupDataFlow.value.copy(tabGroups = updatedList))
    }

    override suspend fun closeTabGroup(tabGroupId: String) {
        tabGroupDataFlow.emit(
            tabGroupDataFlow.value.copy(
                tabGroups =
                    tabGroupDataFlow.value.tabGroups.map { group ->
                        if (group.id == tabGroupId) {
                            group.copy(closed = true, lastModified = dateTimeProvider.currentTimeMillis())
                        } else {
                            group
                        }
                    }
            )
        )
    }

    override suspend fun openTabGroup(tabGroupId: String) {
        tabGroupDataFlow.emit(
            tabGroupDataFlow.value.copy(
                tabGroups =
                    tabGroupDataFlow.value.tabGroups.map { group ->
                        if (group.id == tabGroupId) {
                            group.copy(closed = false, lastModified = dateTimeProvider.currentTimeMillis())
                        } else {
                            group
                        }
                    }
            )
        )
    }

    override suspend fun closeAllTabGroups() {
        closeAllTabGroups.invoke()
    }

    override suspend fun deleteTabGroupById(tabGroupId: String) {
        deleteTabGroupsById(ids = listOf(tabGroupId))
    }

    override suspend fun deleteTabGroupsById(ids: List<String>) {
        val prunedAssignments = HashMap(tabGroupDataFlow.value.tabGroupAssignments)
        ids.forEach {
            prunedAssignments.remove(it)
        }
        tabGroupDataFlow.emit(
            tabGroupDataFlow.value.copy(
                tabGroups = tabGroupDataFlow.value.tabGroups.filterNot { it.id in ids },
                tabGroupAssignments = prunedAssignments,
            )
        )
    }

    override suspend fun ungroupTabGroup(tabGroupId: String) {
        tabGroupDataFlow.emit(
            tabGroupDataFlow.value.copy(
                tabGroups = tabGroupDataFlow.value.tabGroups.filterNot { it.id == tabGroupId },
                tabGroupAssignments = tabGroupDataFlow.value.tabGroupAssignments.filterValues { it != tabGroupId },
            )
        )
    }

    override suspend fun addTabGroupAssignment(
        tabId: String,
        tabGroupId: String,
    ) {
        val updatedAssignments = tabGroupDataFlow.value.tabGroupAssignments + (tabId to tabGroupId)
        tabGroupDataFlow.emit(tabGroupDataFlow.value.copy(tabGroupAssignments = updatedAssignments))
    }

    override suspend fun addTabsToTabGroup(
        tabGroupId: String,
        tabIds: List<String>,
    ) {
        val updatedAssignments = HashMap(tabGroupDataFlow.value.tabGroupAssignments)
        tabIds.forEach { id ->
            updatedAssignments[id] = tabGroupId
        }
        tabGroupDataFlow.emit(tabGroupDataFlow.value.copy(tabGroupAssignments = updatedAssignments))
    }

    override suspend fun updateTabGroupAssignment(
        tabId: String,
        tabGroupId: String,
    ) {
        val updatedAssignments = HashMap(tabGroupDataFlow.value.tabGroupAssignments)
        updatedAssignments[tabId] = tabGroupId
        tabGroupDataFlow.emit(tabGroupDataFlow.value.copy(tabGroupAssignments = updatedAssignments))
    }

    override suspend fun deleteTabGroupAssignmentById(tabId: String) {
        deleteTabGroupAssignmentById.invoke(tabId)
    }

    override suspend fun deleteTabGroupAssignmentsById(tabIds: List<String>) {
        deleteTabGroupAssignmentsById.invoke(tabIds)
    }

    override suspend fun deleteAllTabGroupAssignmentsForGroup(tabGroupId: String) {
        // no-op
    }

    override suspend fun deleteAllTabGroupData() {
        tabGroupDataFlow.emit(TabGroupData())
    }
}
