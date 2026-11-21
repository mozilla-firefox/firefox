/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabgroups.flow

import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.state.initializeTabGroupForm

/** Reducer for the TabGroup flow. */
object TabGroupFlowReducer {
    /** Updates the [TabGroupFlowState] object based on the received [TabGroupAction] */
    fun reduce(
        state: TabGroupFlowState,
        action: TabGroupAction,
    ): TabGroupFlowState {
        return when (action) {
            is TabGroupAction.NavigateBackInvoked -> {
                state.copy(backStack = state.popBackStack())
            }
            is TabGroupAction.TabAddedToNewTabGroup -> {
                state.navigateToCreateTabGroup(tabId = action.tabId)
            }
            is TabGroupAction.TabAddedToExistingTabGroup -> {
                state.copy(backStack = state.popBackStack())
            }
            is TabGroupAction.ExistingGroupSelectedForMove -> state.reduceExistingGroupSelectedForMove(action)
            is TabGroupAction.MoveTabAndDeleteGroupCanceled -> {
                state.copy(backStack = state.popBackStack())
            }
            is TabGroupAction.MoveTabAndDeleteGroupConfirmed -> {
                state.resetBackstack()
            }
            is TabGroupAction.SaveClicked -> state.reduceSaveClicked()
            is TabGroupAction.TabGroupsUpdated -> {
                state.copy(groups = action.tabGroups)
            }
            is TabGroupAction.NameChanged -> {
                state.copy(formState = state.formState?.copy(name = action.name, edited = true))
            }
            is TabGroupAction.ThemeChanged -> {
                state.copy(formState = state.formState?.copy(theme = action.theme, edited = true))
            }
            is TabGroupAction.EditTabGroupClicked -> state.reduceEditTabGroupClicked(action.group)
            is TabGroupAction.DeleteClicked -> {
                state.copy(
                    backStack = state.backStack + TabGroupFlowDestination.DeleteTabGroupConfirmationDialog(action.group)
                )
            }
            is TabGroupAction.UngroupConfirmationRequested -> {
                state.copy(
                    backStack =
                        state.backStack + TabGroupFlowDestination.UngroupTabGroupConfirmationDialog(action.group)
                )
            }
            is TabGroupAction.CloseTabGroupClicked,
            is TabGroupAction.DeleteConfirmed,
            is TabGroupAction.CloseTabAndDeleteGroupConfirmed,
            is TabGroupAction.UngroupConfirmed -> {
                state.copy(backStack = listOf(TabGroupFlowDestination.Root))
            }
            is TabGroupAction.AddNewTabToGroupClicked -> {
                state.copy(backStack = state.popBackStack())
            }
            is TabGroupAction.TabClosed -> state.reduceTabClosed(action.group)

            // no op actions
            is TabGroupAction.DragAndDropInitiated,
            TabGroupAction.DragAndDropProcessed,
            is TabGroupAction.DragAndDropTwoTabs,
            TabGroupAction.NewGroupAnimationFinished,
            is TabGroupAction.NewGroupCreated,
            TabGroupAction.NewTabGroupFabClicked,
            TabGroupAction.NewTabGroupMenuClicked,
            is TabGroupAction.OpenCreatedTabGroup,
            is TabGroupAction.OpenTabGroupClicked,
            is TabGroupAction.SelectedTabsAddedToGroup,
            is TabGroupAction.TabGroupClicked,
            is TabGroupAction.AddToTabGroup,
            is TabGroupAction.AddToNewTabGroup,
            // Onboarding actions - not shown outside TabsTray
            TabGroupAction.OnboardingDismissed -> state
            TabGroupAction.OnboardingShown -> state
            // Ungroup actions
            is TabGroupAction.UngroupRequested -> state
            // Collections
            is TabGroupAction.CollectionsMigrationCardDismissed -> state
        }
    }

    private fun TabGroupFlowState.reduceExistingGroupSelectedForMove(
        action: TabGroupAction.ExistingGroupSelectedForMove
    ): TabGroupFlowState {
        val sourceGroup = groupEmptiedByMoving(action.tabId)
        return if (sourceGroup == null) {
            copy(backStack = popBackStack())
        } else {
            copy(
                backStack =
                    backStack +
                        TabGroupFlowDestination.MoveLastTabAndDeleteGroupDialog(
                            sourceGroupId = sourceGroup.id,
                            destinationGroupId = action.groupId,
                            sourceGroupTitle = sourceGroup.title,
                            destinationGroupTitle = action.groupTitle,
                        )
            )
        }
    }

    private fun TabGroupFlowState.reduceSaveClicked(): TabGroupFlowState =
        copy(
            backStack = popTabGroupFormFlow(),
            tabPendingNewGroup = null,
            groupPendingDeletion = null,
        )

    private fun TabGroupFlowState.resetBackstack(): TabGroupFlowState =
        copy(
            backStack = listOf(TabGroupFlowDestination.Root),
            tabPendingNewGroup = null,
            groupPendingDeletion = null,
        )

    private fun TabGroupFlowState.popTabGroupFormFlow(): List<TabGroupFlowDestination> {
        val expandedIndex = backStack.indexOfLast { it is TabGroupFlowDestination.ExpandedTabGroup }
        return if (expandedIndex >= 0) {
            backStack.take(expandedIndex + 1)
        } else {
            listOf(TabGroupFlowDestination.Root)
        }
    }

    private fun TabGroupFlowState.reduceEditTabGroupClicked(group: TabsTrayItem.TabGroup): TabGroupFlowState =
        copy(
            formState = group.initializeTabGroupForm(),
            backStack = backStack + TabGroupFlowDestination.EditTabGroup,
        )

    private fun TabGroupFlowState.reduceTabClosed(group: TabsTrayItem.TabGroup): TabGroupFlowState =
        if (group.tabs.size <= 1) {
            copy(backStack = backStack + TabGroupFlowDestination.CloseTabAndDeleteGroupConfirmationDialog(group))
        } else {
            this
        }

    private fun TabGroupFlowState.navigateToCreateTabGroup(tabId: String) =
        copy(
            formState = initializeTabGroupForm(isStarterTabGroup = false),
            backStack = backStack + TabGroupFlowDestination.EditTabGroup,
            tabPendingNewGroup = tabId,
            groupPendingDeletion = groupEmptiedByMoving(tabId)?.id,
        )
}
