/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.flow

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.tabgroups.storage.TabGroupStorage
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction.TabsStorageAction

/**
 * Helper class that handles TabGroup flow storage actions outside the TabsTray.
 *
 * @param tabDataCoordinator The [TabDataCoordinator] used to observe and update tab data.
 * @param mainScope The [CoroutineScope] for running tab data updates on the main thread.
 */
class TabGroupFlowStorageMiddleware(
    private val tabDataCoordinator: TabDataCoordinator,
    private val mainScope: CoroutineScope = CoroutineScope(Dispatchers.Main),
) : Middleware<TabGroupFlowState, TabGroupAction> {
    private val tabGroupStorage = TabGroupStorage(tabDataCoordinator = tabDataCoordinator)

    override fun invoke(
        store: Store<TabGroupFlowState, TabGroupAction>,
        next: (TabGroupAction) -> Unit,
        action: TabGroupAction,
    ) {
        when (action) {
            is TabsStorageAction ->
                processAction(
                    action = action,
                    store = store,
                )

            else -> {}
        }

        next(action)
    }

    private fun processAction(
        action: TabsStorageAction,
        store: Store<TabGroupFlowState, TabGroupAction>,
    ) {
        when (action) {
            is TabGroupAction.SaveClicked -> {
                // Read before launching, as the reducer clears the pending tab once this action goes through.
                val form = store.state.formState ?: return
                val tabIds = if (form.inEditState) emptyList() else listOf(store.state.tabPendingNewGroup ?: return)
                mainScope.launch {
                    val groupId =
                        tabGroupStorage.handleSaveFromMultiSelection(
                            formState = form,
                            selectedTabIds = tabIds,
                        )
                    if (groupId != null) {
                        store.dispatch(TabGroupAction.NewGroupCreated(groupId))
                    }
                }
            }

            is TabGroupAction.TabAddedToExistingTabGroup ->
                addTabToGroup(groupId = action.groupId, tabId = action.tabId)
            is TabGroupAction.TabMovedToExistingTabGroup ->
                addTabToGroup(groupId = action.groupId, tabId = action.tabId)

            // Actions not handled outside the tabs tray currently
            is TabGroupAction.CloseTabAndDeleteGroupConfirmed,
            is TabGroupAction.CloseTabGroupClicked,
            is TabGroupAction.DeleteConfirmed,
            is TabGroupAction.DragAndDropInitiated,
            is TabGroupAction.OpenTabGroupClicked,
            is TabGroupAction.SelectedTabsAddedToGroup,
            is TabGroupAction.TabAddedToNewTabGroup,
            is TabGroupAction.TabClosed,
            is TabGroupAction.UngroupConfirmed,
            TabsTrayAction.InitAction,
            is TabsTrayAction.ReorderTabsTrayItem -> {}
        }
    }

    private fun addTabToGroup(groupId: String, tabId: String) = mainScope.launch {
        tabDataCoordinator.addTabsToTabGroup(tabGroupId = groupId, tabIds = listOf(tabId))
    }
}
