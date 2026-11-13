/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.redux.reducer

import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination.CloseTabAndDeleteGroupConfirmationDialog
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination.DeleteTabGroupConfirmationDialog
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination.ExpandedTabGroup
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination.UngroupTabGroupConfirmationDialog
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState.DragProcessingState
import org.mozilla.fenix.tabstray.redux.state.initializeTabGroupForm

/** Reducer for [TabGroupAction] dispatched from the Tabs Tray store. */
object TabGroupActionReducer {

    /**
     * Reduces [TabGroupAction] into a new [TabsTrayState].
     *
     * @param state The current [TabsTrayState].
     * @param action The [TabGroupAction] to reduce.
     */
    fun reduce(
        state: TabsTrayState,
        action: TabGroupAction,
    ): TabsTrayState {
        return when (action) {
            is TabGroupAction.AddToTabGroup -> reduceAddToTabGroup(state)
            is TabGroupAction.AddToNewTabGroup -> state.navigateToCreateTabGroup()
            is TabGroupAction.NewTabGroupFabClicked,
            is TabGroupAction.NewTabGroupMenuClicked -> state.navigateToCreateTabGroup(isStarterTabGroup = true)
            is TabGroupAction.OpenCreatedTabGroup -> state.navigateToExpandedTabGroup(action.group)
            is TabGroupAction.DragAndDropTwoTabs -> reduceDragAndDropTwoTabs(state, action)
            is TabGroupAction.NameChanged -> handleNameChange(state, action)
            is TabGroupAction.ThemeChanged -> handleThemeChange(state, action)
            is TabGroupAction.SaveClicked -> {
                state.copy(
                    mode = TabsTrayState.Mode.Normal,
                    backStack = state.backStack.popTabGroupFlow(),
                    tabGroupState = state.tabGroupState.copy(dragProcessingState = DragProcessingState.COMPLETED),
                )
            }
            is TabGroupAction.TabGroupClicked -> processTabGroupClick(state, action.group)
            is TabGroupAction.TabAddedToExistingTabGroup -> state
            is TabGroupAction.SelectedTabsAddedToGroup ->
                state.copy(
                    mode = TabsTrayState.Mode.Normal,
                    backStack = state.backStack.popTabGroupFlow(),
                )
            is TabGroupAction.DeleteClicked ->
                state.copy(backStack = state.backStack + DeleteTabGroupConfirmationDialog(group = action.group))
            is TabGroupAction.DeleteConfirmed -> state.copy(backStack = state.backStack.popDeleteTabGroupFlow())
            is TabGroupAction.UngroupRequested -> state
            is TabGroupAction.UngroupConfirmationRequested ->
                state.copy(backStack = state.backStack + UngroupTabGroupConfirmationDialog(group = action.group))
            is TabGroupAction.UngroupConfirmed -> state.copy(backStack = state.backStack.popUngroupTabGroupFlow())
            is TabGroupAction.EditTabGroupClicked -> reduceEditTabGroupClicked(state, action)
            is TabGroupAction.NewGroupCreated ->
                state.copy(tabGroupState = state.tabGroupState.copy(enteringGroupId = action.id))
            is TabGroupAction.NewGroupAnimationFinished ->
                state.copy(tabGroupState = state.tabGroupState.copy(enteringGroupId = null))
            is TabGroupAction.OpenTabGroupClicked -> state.navigateToExpandedTabGroup(action.group)
            is TabGroupAction.CloseTabGroupClicked -> state.copy(backStack = listOf(TabManagerNavDestination.Root))
            is TabGroupAction.DragAndDropInitiated ->
                state.copy(normalTabsState = state.normalTabsState.copy(itemFocusIndicatorEnabled = true))
            is TabGroupAction.TabClosed -> reduceTabClosed(state, action)
            is TabGroupAction.CloseTabAndDeleteGroupConfirmed ->
                state.copy(backStack = state.backStack.popDeleteTabGroupFlow())
            is TabGroupAction.OnboardingDismissed ->
                state.copy(config = state.config.copy(tabGroupsOnboardingEnabled = false))
            is TabGroupAction.CollectionsMigrationCardDismissed ->
                state.copy(tabGroupState = state.tabGroupState.copy(showCollectionsMigrationCard = false))
            is TabGroupAction.OnboardingShown ->
                state.copy(tabGroupState = state.tabGroupState.copy(hasRecordedOnboardingImpression = true))
            is TabGroupAction.DragAndDropProcessed ->
                state.copy(
                    tabGroupState = state.tabGroupState.copy(dragProcessingState = DragProcessingState.COMPLETED)
                )
            // No-op actions within the TabsTray
            TabGroupAction.NavigateBackInvoked -> state
            is TabGroupAction.TabAddedToNewTabGroup -> state
            is TabGroupAction.TabGroupsUpdated -> state
            is TabGroupAction.TabMovedToExistingTabGroup -> state
        }
    }

    private fun reduceAddToTabGroup(state: TabsTrayState): TabsTrayState {
        return if (state.tabGroupState.groups.isEmpty()) {
            state.navigateToCreateTabGroup()
        } else {
            state.copy(backStack = state.backStack + TabManagerNavDestination.AddToTabGroup)
        }
    }

    private fun reduceDragAndDropTwoTabs(
        state: TabsTrayState,
        action: TabGroupAction.DragAndDropTwoTabs,
    ): TabsTrayState {
        return state
            .navigateToCreateTabGroup()
            .copy(
                mode =
                    TabsTrayState.Mode.DragAndDrop(
                        sourceId = action.sourceTabId,
                        destinationId = action.destinationTabId,
                    )
            )
    }

    private fun reduceEditTabGroupClicked(
        state: TabsTrayState,
        action: TabGroupAction.EditTabGroupClicked,
    ): TabsTrayState {
        return state.copy(
            tabGroupState = state.tabGroupState.copy(formState = action.group.initializeTabGroupForm()),
            backStack = state.navigateToEditTabGroup(),
        )
    }

    private fun reduceTabClosed(state: TabsTrayState, action: TabGroupAction.TabClosed): TabsTrayState {
        return if (action.group.tabs.size <= 1) {
            state.copy(backStack = state.backStack + CloseTabAndDeleteGroupConfirmationDialog(group = action.group))
        } else {
            state
        }
    }

    private fun handleThemeChange(state: TabsTrayState, action: TabGroupAction.ThemeChanged): TabsTrayState {
        val form =
            requireNotNull(state.tabGroupState.formState) {
                "ThemeChanged dispatched with no TabGroupFormState"
            }
        return state.copy(
            tabGroupState =
                state.tabGroupState.copy(
                    formState =
                        form.copy(
                            theme = action.theme,
                            edited = true,
                        )
                )
        )
    }

    private fun handleNameChange(state: TabsTrayState, action: TabGroupAction.NameChanged): TabsTrayState {
        val form =
            requireNotNull(state.tabGroupState.formState) {
                "NameChanged dispatched with no TabGroupFormState"
            }
        return state.copy(
            tabGroupState =
                state.tabGroupState.copy(
                    formState =
                        form.copy(
                            name = action.name,
                            edited = true,
                        )
                )
        )
    }

    private fun TabsTrayState.navigateToCreateTabGroup(isStarterTabGroup: Boolean = false) =
        copy(
            tabGroupState =
                tabGroupState.copy(
                    formState = initializeTabGroupForm(isStarterTabGroup = isStarterTabGroup),
                    dragProcessingState = DragProcessingState.EDIT_IN_PROGRESS,
                ),
            backStack = navigateToEditTabGroup(),
        )

    private fun TabsTrayState.navigateToExpandedTabGroup(group: TabsTrayItem.TabGroup): TabsTrayState =
        copy(
            selectedPage = Page.NormalTabs,
            backStack = backStack + ExpandedTabGroup(group = group.copy(closed = false)),
        )

    private fun List<TabManagerNavDestination>.popTabGroupFlow(): List<TabManagerNavDestination> = filterNot {
        it is TabManagerNavDestination.EditTabGroup || it is TabManagerNavDestination.AddToTabGroup
    }

    private fun List<TabManagerNavDestination>.popDeleteTabGroupFlow(): List<TabManagerNavDestination> = filterNot {
        it is DeleteTabGroupConfirmationDialog ||
            it is CloseTabAndDeleteGroupConfirmationDialog ||
            it is ExpandedTabGroup
    }

    private fun List<TabManagerNavDestination>.popUngroupTabGroupFlow(): List<TabManagerNavDestination> = filterNot {
        it is UngroupTabGroupConfirmationDialog || it is ExpandedTabGroup
    }

    private fun TabsTrayState.navigateToEditTabGroup(): List<TabManagerNavDestination> =
        backStack + TabManagerNavDestination.EditTabGroup

    private fun processTabGroupClick(
        currentState: TabsTrayState,
        group: TabsTrayItem.TabGroup,
    ): TabsTrayState =
        when (currentState.mode) {
            is TabsTrayState.Mode.Normal,
            is TabsTrayState.Mode.DragAndDrop ->
                currentState.copy(backStack = currentState.backStack + ExpandedTabGroup(group = group))

            is TabsTrayState.Mode.Select -> {
                val selectedTabs = currentState.mode.selectedTabs.toHashSet()
                val selectedTabGroups = currentState.mode.selectedTabGroups.toHashSet()

                if (group in currentState.mode.selectedTabGroups) {
                    selectedTabGroups.remove(group)
                    selectedTabs.removeAll(group.tabs.toSet())
                } else {
                    selectedTabGroups.add(group)
                    selectedTabs.addAll(group.tabs)
                }

                val newMode =
                    if (selectedTabs.isEmpty() && selectedTabGroups.isEmpty()) {
                        TabsTrayState.Mode.Normal
                    } else {
                        TabsTrayState.Mode.Select(
                            selectedTabs = selectedTabs,
                            selectedTabGroups = selectedTabGroups,
                        )
                    }

                currentState.copy(mode = newMode)
            }
        }
}
