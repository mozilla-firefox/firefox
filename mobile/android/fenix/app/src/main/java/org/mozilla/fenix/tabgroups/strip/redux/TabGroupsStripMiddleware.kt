/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.strip.redux

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import mozilla.components.feature.tabdata.coordinator.data.TabsSnapshot
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/**
 * A [Middleware] for the [TabGroupsStripStore] that observes the [TabDataCoordinator] to populate the strip's state and
 * reacts to [TabGroupsStripAction]s to drive navigation and other side effects.
 *
 * @param tabDataCoordinator The [TabDataCoordinator] whose tab data drives the strip.
 * @param scope The [CoroutineScope] used to observe the coordinator.
 * @param dispatcher The [CoroutineDispatcher] the coordinator observation runs on.
 */
class TabGroupsStripMiddleware(
    private val tabDataCoordinator: TabDataCoordinator,
    private val scope: CoroutineScope,
    private val dispatcher: CoroutineDispatcher = Dispatchers.Default,
) : Middleware<TabGroupsStripState, TabGroupsStripAction> {
    override fun invoke(
        store: Store<TabGroupsStripState, TabGroupsStripAction>,
        next: (TabGroupsStripAction) -> Unit,
        action: TabGroupsStripAction,
    ) {
        val visibleState = store.state as? TabGroupsStripState.TabGroupStripVisible

        // Bug 2071335 - React to AddTabClicked here by opening and selecting a new homepage tab in this group
        // Bug 2069525 - React to ExpandGroupClicked here by opening the expanded tab group
        next(action)

        when (action) {
            is TabGroupsStripAction.InitializeDataObservation -> observeTabData(store)
            is TabGroupsStripAction.TabClicked ->
                if (action.tabId != visibleState?.focusedTabId) tabDataCoordinator.selectTab(action.tabId)
            is TabGroupsStripAction.TabGroupChanged,
            is TabGroupsStripAction.TabGroupCleared,
            is TabGroupsStripAction.AddTabClicked,
            is TabGroupsStripAction.ExpandGroupClicked -> Unit
        }
    }

    private fun observeTabData(store: Store<TabGroupsStripState, TabGroupsStripAction>) {
        scope.launch(dispatcher) {
            tabDataCoordinator.tabsSnapshot.collect { snapshot ->
                store.dispatch(snapshot.toTabGroupAction())
            }
        }
    }
}

/** Transforms a [TabsSnapshot] into the [TabGroupsStripAction] for the active tab's group. */
private fun TabsSnapshot?.toTabGroupAction(): TabGroupsStripAction {
    val snapshot = this ?: return TabGroupsStripAction.TabGroupCleared

    val selectedTab = snapshot.selectedTab
    val group = selectedTab?.tabGroupId?.let { groupId -> snapshot.openTabGroups.find { it.id == groupId } }

    if (selectedTab == null || group == null) {
        return TabGroupsStripAction.TabGroupCleared
    }

    val tabsById = snapshot.normalTabs.associateBy { it.id }
    val tabs =
        group.tabIds
            .mapNotNull { tabsById[it] }
            .map { tab ->
                TabGroupsStripState.Tab(
                    id = tab.id,
                    url = tab.url,
                    title = tab.title,
                    isFocused = tab.id == selectedTab.id,
                )
            }
    return TabGroupsStripAction.TabGroupChanged(groupId = group.id, groupTitle = group.title, tabs = tabs)
}
