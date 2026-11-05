/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.tabdata.coordinator.fakes

import kotlinx.coroutines.flow.MutableStateFlow
import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import mozilla.components.feature.tabdata.coordinator.data.TabsSnapshot

/**
 * A simple fake [TabDataCoordinator] for unit tests. Its [tabsSnapshot] can be set directly, and every other function
 * is stubbed to do nothing.
 *
 * @param initialTabsSnapshot The [TabsSnapshot] emitted by [tabsSnapshot]. Null by default, mirroring a coordinator
 *   which is not yet fully initialized.
 * @property selectedTabIds The IDs passed to [selectTab], in call order.
 */
class FakeTabDataCoordinator(initialTabsSnapshot: TabsSnapshot? = null) : TabDataCoordinator {

    override val tabsSnapshot: MutableStateFlow<TabsSnapshot?> = MutableStateFlow(initialTabsSnapshot)

    val selectedTabIds = mutableListOf<String>()

    override fun initialize() = Unit

    override fun selectTab(tabId: String) {
        selectedTabIds.add(tabId)
    }

    override suspend fun createTabGroup(
        title: String,
        theme: String,
        tabIds: List<String>,
        anchorTabId: String?,
    ): String = ""

    override suspend fun updateTabGroup(tabGroupId: String, title: String, theme: String) = Unit

    override suspend fun addTabsToTabGroup(tabGroupId: String, tabIds: List<String>) = Unit

    override suspend fun mergeTabGroupIntoTab(tabGroupId: String, tabId: String) = Unit

    override suspend fun mergeTabGroups(sourceTabGroupId: String, destinationTabGroupId: String) = Unit

    override suspend fun deleteTabGroupAndTabs(tabGroupId: String) = Unit

    override suspend fun deleteTabGroupsById(tabGroupIds: List<String>) = Unit

    override suspend fun ungroupTabGroup(tabGroupId: String) = Unit

    override suspend fun openTabGroup(tabGroupId: String) = Unit

    override suspend fun closeTabGroup(tabGroupId: String) = Unit

    override suspend fun removeTabFromTabGroup(tabGroupId: String, tabId: String) = Unit

    override suspend fun reorderItem(sourceId: String, destinationId: String?, placeAfter: Boolean) = Unit
}
