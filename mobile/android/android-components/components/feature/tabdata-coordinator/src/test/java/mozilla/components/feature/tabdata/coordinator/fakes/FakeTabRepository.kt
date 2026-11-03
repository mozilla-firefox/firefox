/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.tabdata.coordinator.fakes

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import mozilla.components.feature.tabdata.coordinator.data.Tab
import mozilla.components.feature.tabdata.coordinator.repository.TabRepository

/**
 * A fake, in-memory implementation of [TabRepository] for use in unit tests. Tab ordering mirrors the behavior of a
 * real tab data source, so the sequencing performed by grouping operations can be asserted on [tabFlow].
 *
 * @param initialData The [Data] the repository starts off emitting.
 */
class FakeTabRepository(initialData: TabRepository.Data = TabRepository.Data()) : TabRepository {

    override val tabFlow: Flow<TabRepository.Data>
        field = MutableStateFlow(initialData)

    /** The IDs of the tabs that have been excluded from selection by the removals performed on this repository. */
    var excludedFallbackTabIds: Set<String> = emptySet()
        private set

    /** The current list of tabs, in order. */
    val tabs: List<Tab>
        get() = tabFlow.value.tabs

    /** Emit [data] to all observers of [tabFlow]. */
    suspend fun emit(data: TabRepository.Data) {
        tabFlow.emit(data)
    }

    /** Emit the result of applying [transform] to the most recently emitted data. */
    suspend fun update(transform: (TabRepository.Data) -> TabRepository.Data) {
        emit(data = transform(tabFlow.value))
    }

    override fun moveTabs(tabIds: List<String>, targetTabId: String, placeAfter: Boolean) {
        val currentTabs = tabFlow.value.tabs
        val targetIndex = currentTabs.indexOfFirst { it.id == targetTabId }
        if (targetIndex == -1) return

        val targetPosition = targetIndex + if (placeAfter) 1 else 0
        val positionOffset = currentTabs.take(targetPosition).count { it.id in tabIds }
        val (movedTabs, unmovedTabs) = currentTabs.partition { it.id in tabIds }
        val insertionIndex = targetPosition - positionOffset

        tabFlow.value =
            tabFlow.value.copy(
                tabs =
                    unmovedTabs.subList(0, insertionIndex) +
                        movedTabs +
                        unmovedTabs.subList(insertionIndex, unmovedTabs.size)
            )
    }

    override fun moveTab(sourceTabId: String, targetTabId: String, placeAfter: Boolean) {
        if (targetTabId == sourceTabId) return

        moveTabs(tabIds = listOf(sourceTabId), targetTabId = targetTabId, placeAfter = placeAfter)
    }

    override fun removeTabs(tabIds: List<String>, excludedFallbackTabIds: Set<String>) {
        this.excludedFallbackTabIds = excludedFallbackTabIds
        tabFlow.value = tabFlow.value.copy(tabs = tabFlow.value.tabs.filterNot { it.id in tabIds })
    }

    override fun selectTab(tabId: String) {
        tabFlow.value = tabFlow.value.copy(selectedTabId = tabId)
    }
}
