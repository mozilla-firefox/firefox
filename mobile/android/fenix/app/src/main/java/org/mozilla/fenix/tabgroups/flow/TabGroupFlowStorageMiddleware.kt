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
import org.mozilla.fenix.tabstray.repository.uistate.TabManagerUiStateRepository
import org.mozilla.fenix.tabstray.repository.uistate.UngroupConfirmationDelegate

/**
 * Helper class that handles TabGroup flow storage actions outside the TabsTray.
 *
 * @param tabDataCoordinator The [TabDataCoordinator] used to observe and update tab data.
 * @param uiStateRepository Persists UI state preferences.
 * @param scope The [CoroutineScope] for running the tab data updates off of the main thread.
 * @param mainScope The [CoroutineScope] used for returning to the main thread.
 * @param ungroupConfirmationDelegate Resolves and persists the ungroup confirmation preference.
 */
class TabGroupFlowStorageMiddleware(
    private val tabDataCoordinator: TabDataCoordinator,
    private val uiStateRepository: TabManagerUiStateRepository,
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.Default),
    private val mainScope: CoroutineScope = CoroutineScope(Dispatchers.Main),
    private val ungroupConfirmationDelegate: UngroupConfirmationDelegate =
        UngroupConfirmationDelegate(repository = uiStateRepository, scope = scope),
) : Middleware<TabGroupFlowState, TabGroupAction> {
    private val tabGroupStorage = TabGroupStorage(tabDataCoordinator = tabDataCoordinator)

    override fun invoke(
        store: Store<TabGroupFlowState, TabGroupAction>,
        next: (TabGroupAction) -> Unit,
        action: TabGroupAction,
    ) {
        when (action) {
            is TabGroupAction.UngroupRequested -> ungroupConfirmationDelegate.resolve(action.group, store::dispatch)
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
                val groupPendingDeletion = store.state.groupPendingDeletion
                val tabIds = if (form.inEditState) emptyList() else listOf(store.state.tabPendingNewGroup ?: return)
                scope.launch {
                    val groupId =
                        tabGroupStorage.handleSaveFromMultiSelection(
                            formState = form,
                            selectedTabIds = tabIds,
                        )
                    if (groupId != null) {
                        if (groupPendingDeletion != null) {
                            tabDataCoordinator.deleteTabGroupsById(listOf(groupPendingDeletion))
                        }
                        mainScope.launch {
                            store.dispatch(TabGroupAction.NewGroupCreated(groupId))
                        }
                    }
                }
            }

            is TabGroupAction.TabAddedToExistingTabGroup ->
                addTabToGroup(groupId = action.groupId, tabId = action.tabId)
            is TabGroupAction.ExistingGroupSelectedForMove -> {
                // Moving the last tab out of a group waits for MoveTabAndDeleteGroupConfirmed.
                if (store.state.groupEmptiedByMoving(action.tabId) == null) {
                    addTabToGroup(groupId = action.groupId, tabId = action.tabId)
                }
            }
            is TabGroupAction.MoveTabAndDeleteGroupConfirmed ->
                mainScope.launch {
                    tabDataCoordinator.mergeTabGroups(
                        sourceTabGroupId = action.sourceGroupId,
                        destinationTabGroupId = action.destinationGroupId,
                    )
                }

            is TabGroupAction.AddNewTabToGroupClicked ->
                scope.launch { tabDataCoordinator.addNewTabToTabGroup(tabGroupId = action.group.id) }

            is TabGroupAction.TabClosed ->
                scope.launch {
                    tabDataCoordinator.removeTabFromTabGroup(tabGroupId = action.group.id, tabId = action.tab.id)
                }

            is TabGroupAction.CloseTabGroupClicked ->
                scope.launch { tabDataCoordinator.closeTabGroup(tabGroupId = action.group.id) }

            is TabGroupAction.DeleteConfirmed ->
                scope.launch { tabDataCoordinator.deleteTabGroupAndTabs(tabGroupId = action.group.id) }

            is TabGroupAction.CloseTabAndDeleteGroupConfirmed ->
                scope.launch { tabDataCoordinator.deleteTabGroupAndTabs(tabGroupId = action.group.id) }

            is TabGroupAction.UngroupConfirmed -> {
                ungroupConfirmationDelegate.setSkip(action.dontAskAgain)
                scope.launch { tabDataCoordinator.ungroupTabGroup(tabGroupId = action.group.id) }
            }

            // Actions not handled outside the tabs tray currently
            is TabGroupAction.DragAndDropInitiated,
            is TabGroupAction.OpenTabGroupClicked,
            is TabGroupAction.SelectedTabsAddedToGroup,
            is TabGroupAction.TabAddedToNewTabGroup,
            TabsTrayAction.InitAction,
            is TabsTrayAction.ReorderTabsTrayItem -> {}
        }
    }

    private fun addTabToGroup(groupId: String, tabId: String) = scope.launch {
        tabDataCoordinator.addTabsToTabGroup(tabGroupId = groupId, tabIds = listOf(tabId))
    }
}
