/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.tabdata.coordinator.repository

import kotlinx.coroutines.flow.Flow
import mozilla.components.feature.tabdata.coordinator.data.Tab

/** Abstraction for observing and updating browser tab data. */
interface TabRepository {

    /** A [Flow] of transformed tab [Data]. */
    val tabFlow: Flow<Data>

    /**
     * Moves the tabs of [tabIds] next to [targetTabId], preserving the relative order of the moved tabs.
     *
     * @param tabIds The IDs of the tabs to move.
     * @param targetTabId The ID of the tab the moved tabs are placed next to.
     * @param placeAfter Whether the moved tabs are placed after, rather than before, the target.
     */
    fun moveTabs(tabIds: List<String>, targetTabId: String, placeAfter: Boolean)

    /**
     * Moves the tab with [sourceTabId] next to [targetTabId]. No-op if [targetTabId] is null or matches [sourceTabId].
     *
     * @param sourceTabId The ID of the tab to move.
     * @param targetTabId The ID of the tab the moved tab is placed next to.
     * @param placeAfter Whether the moved tab is placed after, rather than before, the target.
     */
    fun moveTab(sourceTabId: String, targetTabId: String, placeAfter: Boolean)

    /**
     * Removes the tabs with the provided [tabIds].
     *
     * @param tabIds The IDs of the tabs to remove.
     * @param excludedFallbackTabIds The IDs of the tabs that should not be selected after the removal.
     */
    fun removeTabs(tabIds: List<String>, excludedFallbackTabIds: Set<String> = emptySet())

    /**
     * Selects or makes active the tab with [tabId].
     *
     * @param tabId The ID of the tab to select.
     */
    fun selectTab(tabId: String)

    /**
     * Value class representing the data observed from the browser tab data source.
     *
     * @property selectedTabId The ID of the currently selected tab.
     * @property tabs The list of open tabs in the browser. Implementations are responsible for mapping their backing
     *   tab representation onto [Tab]; group membership is resolved by the coordinator, so [Tab.tabGroupId] is expected
     *   to be null here.
     */
    data class Data(
        val selectedTabId: String? = null,
        val tabs: List<Tab> = emptyList(),
    )
}
