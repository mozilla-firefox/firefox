/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabgroups.flow

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState

class TabGroupFlowReducerTest {
    @Test
    fun `WHEN navigate back is invoked THEN backstack is popped`() {
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.AddToTabGroup(tabId = "123"),
                        TabGroupFlowDestination.EditTabGroup,
                    ),
                entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.NavigateBackInvoked,
            )
        assertEquals(
            expected =
                initialState.copy(
                    backStack =
                        listOf(
                            TabGroupFlowDestination.Root,
                            TabGroupFlowDestination.AddToTabGroup(tabId = "123"),
                        )
                ),
            actual = resultState,
        )
    }

    @Test
    fun `WHEN navigate back is invoked THEN backstack is never popped below root`() {
        val initialState =
            TabGroupFlowState(
                backStack = listOf(TabGroupFlowDestination.Root),
                entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.NavigateBackInvoked,
            )
        assertEquals(
            expected = initialState,
            actual = resultState,
        )
    }

    @Test
    fun `WHEN navigate back is invoked THEN root element remains element 0`() {
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.EditTabGroup,
                    ),
                entryPoint = TabGroupFlowEntryPoint.EditGroup(groupId = "123"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.NavigateBackInvoked,
            )
        assertEquals(
            expected = TabGroupFlowDestination.Root,
            actual = resultState.backStack[0],
        )
    }

    @Test
    fun `WHEN TabAddedToNewTabGroup invoked backstack is set up correctly`() {
        val initialState = TabGroupFlowState(entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123"))
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabAddedToNewTabGroup(tabId = "123"),
            )
        assertEquals(
            expected =
                listOf(
                    TabGroupFlowDestination.Root,
                    TabGroupFlowDestination.EditTabGroup,
                ),
            actual = resultState.backStack,
        )
    }

    @Test
    fun `WHEN TabAddedToNewTabGroup invoked form state is initialized`() {
        val initialState = TabGroupFlowState(entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123"))
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabAddedToNewTabGroup(tabId = "123"),
            )
        assertNotNull(resultState.formState)
    }

    @Test
    fun `WHEN TabAddedToExistingTabGroup invoked backstack is set up correctly`() {
        val initialState = TabGroupFlowState(entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123"))
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action =
                    TabGroupAction.TabAddedToExistingTabGroup(
                        tabId = "123",
                        groupId = "123",
                        groupTitle = "Tab Group 1",
                    ),
            )
        assertEquals(
            expected = listOf(TabGroupFlowDestination.Root),
            actual = resultState.backStack,
        )
    }

    @Test
    fun `WHEN SaveClicked invoked backstack is reset`() {
        // Saving flow:
        // Menu -> AddToTabGroup [Root, AddToTabGroup]
        // Tap "New Group" -> [Root, AddToTabGroup, EditTabGroup]
        // Save -> [Root]
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.AddToTabGroup(tabId = "123"),
                        TabGroupFlowDestination.EditTabGroup,
                    ),
                entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.SaveClicked,
            )
        assertEquals(
            expected = listOf(TabGroupFlowDestination.Root),
            actual = resultState.backStack,
        )
    }

    @Test
    fun `WHEN NameChanged invoked THEN name is updated`() {
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.AddToTabGroup(tabId = "123"),
                        TabGroupFlowDestination.EditTabGroup,
                    ),
                formState =
                    TabGroupFormState(
                        tabGroupId = "123",
                        name = "OldName",
                        theme = TabGroupTheme.Red,
                    ),
                entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123"),
            )

        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.NameChanged("NewName"),
            )
        assertEquals(
            expected = initialState.copy(formState = initialState.formState!!.copy(name = "NewName", edited = true)),
            actual = resultState,
        )
    }

    @Test
    fun `WHEN ThemeChanged invoked THEN theme is updated`() {
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.AddToTabGroup(tabId = "123"),
                        TabGroupFlowDestination.EditTabGroup,
                    ),
                formState =
                    TabGroupFormState(
                        tabGroupId = "123",
                        name = "OldName",
                        theme = TabGroupTheme.Red,
                    ),
                entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123"),
            )

        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.ThemeChanged(TabGroupTheme.Pink),
            )
        assertEquals(
            expected =
                initialState.copy(formState = initialState.formState!!.copy(theme = TabGroupTheme.Pink, edited = true)),
            actual = resultState,
        )
    }

    @Test
    fun `GIVEN the tab is not in a group WHEN ExistingGroupSelectedForMove invoked THEN the move sheet is popped`() {
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.MoveToTabGroup(tabId = "123"),
                    ),
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action =
                    TabGroupAction.ExistingGroupSelectedForMove(tabId = "123", groupId = "456", groupTitle = "Work"),
            )
        assertEquals(
            expected = listOf(TabGroupFlowDestination.Root),
            actual = resultState.backStack,
        )
    }

    @Test
    fun `GIVEN the tab's group has other tabs WHEN ExistingGroupSelectedForMove invoked THEN the move sheet is popped`() {
        val sourceGroup =
            createTabGroup(
                id = "source",
                tabs = listOf(createTab("mozilla.org", id = "123"), createTab("wikipedia.org", id = "456")),
            )
        val initialState =
            TabGroupFlowState(
                backStack = listOf(TabGroupFlowDestination.Root, TabGroupFlowDestination.MoveToTabGroup(tabId = "123")),
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123"),
                groups = listOf(sourceGroup),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action =
                    TabGroupAction.ExistingGroupSelectedForMove(
                        tabId = "123",
                        groupId = "destination",
                        groupTitle = "New",
                    ),
            )
        assertEquals(
            expected = listOf(TabGroupFlowDestination.Root),
            actual = resultState.backStack,
        )
    }

    @Test
    fun `GIVEN the tab is the last tab in its group WHEN ExistingGroupSelectedForMove invoked THEN the confirmation dialog is added to the backstack`() {
        val sourceGroup =
            createTabGroup(id = "source", title = "Old", tabs = listOf(createTab("mozilla.org", id = "123")))
        val initialState =
            TabGroupFlowState(
                backStack = listOf(TabGroupFlowDestination.Root, TabGroupFlowDestination.MoveToTabGroup(tabId = "123")),
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123"),
                groups = listOf(sourceGroup),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action =
                    TabGroupAction.ExistingGroupSelectedForMove(
                        tabId = "123",
                        groupId = "destination",
                        groupTitle = "New",
                    ),
            )
        assertEquals(
            expected =
                listOf(
                    TabGroupFlowDestination.Root,
                    TabGroupFlowDestination.MoveToTabGroup(tabId = "123"),
                    TabGroupFlowDestination.MoveLastTabAndDeleteGroupDialog(
                        sourceGroupId = "source",
                        destinationGroupId = "destination",
                        sourceGroupTitle = "Old",
                        destinationGroupTitle = "New",
                    ),
                ),
            actual = resultState.backStack,
        )
    }

    @Test
    fun `WHEN TabGroupsUpdated invoked THEN groups are replaced`() {
        val initialState =
            TabGroupFlowState(
                groups = listOf(createTabGroup(id = "old")),
                entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123"),
            )
        val updatedGroups = listOf(createTabGroup(id = "a"), createTabGroup(id = "b"))
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabGroupsUpdated(updatedGroups),
            )
        assertEquals(
            expected = initialState.copy(groups = updatedGroups),
            actual = resultState,
        )
    }

    @Test
    fun `WHEN TabAddedToNewTabGroup invoked THEN the tab is pending addition to the new group`() {
        val initialState = TabGroupFlowState(entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123"))
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabAddedToNewTabGroup(tabId = "123"),
            )
        assertEquals(
            expected = "123",
            actual = resultState.tabPendingNewGroup,
        )
    }

    @Test
    fun `WHEN TabAddedToNewTabGroup invoked with tab id THEN only the tab pending group assignment is set`() {
        val initialState = TabGroupFlowState(entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123"))
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabAddedToNewTabGroup(tabId = "123"),
            )
        assertEquals(
            expected =
                listOf(
                    TabGroupFlowDestination.Root,
                    TabGroupFlowDestination.EditTabGroup,
                ),
            actual = resultState.backStack,
        )
        assertNotNull(resultState.formState)
        assertEquals(expected = "123", actual = resultState.tabPendingNewGroup)
        assertNull(resultState.groupPendingDeletion)
    }

    @Test
    fun `GIVEN the tab is the last tab in its group WHEN TabAddedToNewTabGroup invoked THEN both the tab pending group assignment and group pending deletion are set`() {
        val initialState =
            TabGroupFlowState(
                backStack = listOf(TabGroupFlowDestination.Root, TabGroupFlowDestination.MoveToTabGroup(tabId = "123")),
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123"),
                groups = listOf(createTabGroup(id = "source", tabs = listOf(createTab("mozilla.org", id = "123")))),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabAddedToNewTabGroup(tabId = "123"),
            )
        assertEquals(
            expected =
                listOf(
                    TabGroupFlowDestination.Root,
                    TabGroupFlowDestination.MoveToTabGroup(tabId = "123"),
                    TabGroupFlowDestination.EditTabGroup,
                ),
            actual = resultState.backStack,
        )
        assertNotNull(resultState.formState)
        assertEquals(expected = "123", actual = resultState.tabPendingNewGroup)
        assertEquals(expected = "source", actual = resultState.groupPendingDeletion)
    }

    @Test
    fun `GIVEN the tab's group has other tabs WHEN TabAddedToNewTabGroup invoked THEN no group is pending deletion`() {
        val initialState =
            TabGroupFlowState(
                backStack = listOf(TabGroupFlowDestination.Root, TabGroupFlowDestination.MoveToTabGroup(tabId = "123")),
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123"),
                groups =
                    listOf(
                        createTabGroup(
                            id = "source",
                            tabs = listOf(createTab("mozilla.org", id = "123"), createTab("wikipedia.org", id = "456")),
                        )
                    ),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabAddedToNewTabGroup(tabId = "123"),
            )
        assertEquals(expected = "123", actual = resultState.tabPendingNewGroup)
        assertNull(resultState.groupPendingDeletion)
    }

    @Test
    fun `WHEN SaveClicked invoked THEN the pending tab is cleared`() {
        val initialState =
            TabGroupFlowState(
                backStack = listOf(TabGroupFlowDestination.Root, TabGroupFlowDestination.EditTabGroup),
                formState = TabGroupFormState(tabGroupId = null, name = "Work"),
                entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123"),
                tabPendingNewGroup = "123",
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.SaveClicked,
            )
        assertNull(resultState.tabPendingNewGroup)
    }

    @Test
    fun `WHEN SaveClicked invoked THEN the group pending deletion is cleared`() {
        val initialState =
            TabGroupFlowState(
                backStack = listOf(TabGroupFlowDestination.Root, TabGroupFlowDestination.EditTabGroup),
                formState = TabGroupFormState(tabGroupId = null, name = "Work"),
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123"),
                tabPendingNewGroup = "123",
                groupPendingDeletion = "source",
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.SaveClicked,
            )
        assertNull(resultState.groupPendingDeletion)
    }

    @Test
    fun `WHEN a tab is already part of a group THEN the target move list excludes that group`() {
        val groupWithTabs =
            createTabGroup(
                id = "GroupWithTabs",
                tabs = listOf(createTab("mozilla.com", id = "1"), createTab("wikipedia.org", id = "2")),
            )
        val emptyGroup = createTabGroup(id = "EmptyGroup")
        val initialState =
            TabGroupFlowState(
                backStack = listOf(TabGroupFlowDestination.Root, TabGroupFlowDestination.EditTabGroup),
                formState = TabGroupFormState(tabGroupId = null, name = "Work"),
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "1"),
                groups = listOf(groupWithTabs, emptyGroup),
            )
        assertEquals(expected = listOf(emptyGroup), actual = initialState.moveTargetGroups("1"))
    }

    @Test
    fun `WHEN a tab is not already part of a group THEN the target move list does not exclude that group`() {
        val groupWithTabs =
            createTabGroup(
                id = "GroupWithTabs",
                tabs = listOf(createTab("mozilla.com", id = "1"), createTab("wikipedia.org", id = "2")),
            )
        val emptyGroup = createTabGroup(id = "EmptyGroup")
        val initialState =
            TabGroupFlowState(
                backStack = listOf(TabGroupFlowDestination.Root, TabGroupFlowDestination.EditTabGroup),
                formState = TabGroupFormState(tabGroupId = null, name = "Work"),
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "3"),
                groups = listOf(groupWithTabs, emptyGroup),
            )
        assertEquals(expected = listOf(groupWithTabs, emptyGroup), actual = initialState.moveTargetGroups("3"))
    }

    @Test
    fun `WHEN a tab is part of a group THEN groupContaining returns that group`() {
        val groupWithTab = createTabGroup(id = "GroupWithTab", tabs = listOf(createTab("mozilla.com", id = "1")))
        val otherGroup = createTabGroup(id = "OtherGroup", tabs = listOf(createTab("wikipedia.org", id = "2")))
        val state =
            TabGroupFlowState(
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "1"),
                groups = listOf(otherGroup, groupWithTab),
            )

        assertEquals(expected = groupWithTab, actual = state.groupContaining("1"))
    }

    @Test
    fun `WHEN a tab is not part of any group THEN groupContaining returns null`() {
        val state =
            TabGroupFlowState(
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "3"),
                groups = listOf(createTabGroup(id = "Group", tabs = listOf(createTab("mozilla.com", id = "1")))),
            )

        assertNull(state.groupContaining("3"))
    }

    @Test
    fun `WHEN MoveTabAndDeleteGroupCanceled invoked THEN the user returns to the move sheet`() {
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.MoveToTabGroup(tabId = "123"),
                        TabGroupFlowDestination.MoveLastTabAndDeleteGroupDialog(
                            sourceGroupId = "source",
                            destinationGroupId = "destination",
                            sourceGroupTitle = "Old",
                            destinationGroupTitle = "New",
                        ),
                    ),
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.MoveTabAndDeleteGroupCanceled,
            )
        assertEquals(
            expected = listOf(TabGroupFlowDestination.Root, TabGroupFlowDestination.MoveToTabGroup(tabId = "123")),
            actual = resultState.backStack,
        )
    }

    @Test
    fun `WHEN MoveTabAndDeleteGroupConfirmed invoked THEN the backstack is reset to root`() {
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.MoveToTabGroup(tabId = "123"),
                        TabGroupFlowDestination.MoveLastTabAndDeleteGroupDialog(
                            sourceGroupId = "source",
                            destinationGroupId = "destination",
                            sourceGroupTitle = "Old",
                            destinationGroupTitle = "New",
                        ),
                    ),
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action =
                    TabGroupAction.MoveTabAndDeleteGroupConfirmed(
                        sourceGroupId = "source",
                        destinationGroupId = "destination",
                        destinationGroupTitle = "New",
                    ),
            )
        assertEquals(
            expected = listOf(TabGroupFlowDestination.Root),
            actual = resultState.backStack,
        )
    }

    @Test
    fun `GIVEN the user is on the edit group destination via the expanded destination WHEN the user invokes a save THEN the expanded view is restored`() {
        val group = createTabGroup(id = "group-1")
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.ExpandedTabGroup(group = group),
                        TabGroupFlowDestination.EditTabGroup,
                    ),
                entryPoint = TabGroupFlowEntryPoint.ViewGroup(groupId = "group-1"),
            )
        val resultState = TabGroupFlowReducer.reduce(state = initialState, action = TabGroupAction.SaveClicked)
        assertEquals(
            expected =
                listOf(
                    TabGroupFlowDestination.Root,
                    TabGroupFlowDestination.ExpandedTabGroup(group = group),
                ),
            actual = resultState.backStack,
        )
    }

    @Test
    fun `GIVEN the user is on the edit group destination via the move destination WHEN the user invokes a save THEN the backstack is reset to root`() {
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.MoveToTabGroup(tabId = "123"),
                        TabGroupFlowDestination.EditTabGroup,
                    ),
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123"),
            )
        val resultState = TabGroupFlowReducer.reduce(state = initialState, action = TabGroupAction.SaveClicked)
        assertEquals(expected = listOf(TabGroupFlowDestination.Root), actual = resultState.backStack)
    }

    @Test
    fun `WHEN AddNewTabToGroupClicked invoked THEN the expanded sheet is popped`() {
        val group = createTabGroup(id = "group-1")
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.ExpandedTabGroup(group = group),
                    ),
                entryPoint = TabGroupFlowEntryPoint.ViewGroup(groupId = "group-1"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.AddNewTabToGroupClicked(group = group),
            )
        assertEquals(expected = listOf(TabGroupFlowDestination.Root), actual = resultState.backStack)
    }

    @Test
    fun `WHEN the last tab of a group is closed THEN the close and delete confirmation dialog is added to the backstack`() {
        val group = createTabGroup(id = "group-1", tabs = listOf(createTab("mozilla.org", id = "1")))
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.ExpandedTabGroup(group = group),
                    ),
                entryPoint = TabGroupFlowEntryPoint.ViewGroup(groupId = "group-1"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabClosed(tab = group.tabs.first(), group = group),
            )
        assertEquals(
            expected = TabGroupFlowDestination.CloseTabAndDeleteGroupConfirmationDialog(group = group),
            actual = resultState.backStack.last(),
        )
    }

    @Test
    fun `WHEN a tab of a multi-tab group is closed THEN the backstack is unchanged`() {
        val group =
            createTabGroup(
                id = "group-1",
                tabs = listOf(createTab("mozilla.org", id = "1"), createTab("wikipedia.org", id = "2")),
            )
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.ExpandedTabGroup(group = group),
                    ),
                entryPoint = TabGroupFlowEntryPoint.ViewGroup(groupId = "group-1"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabClosed(tab = group.tabs.first(), group = group),
            )
        assertEquals(expected = initialState.backStack, actual = resultState.backStack)
    }

    @Test
    fun `WHEN closing the last tab and deleting is confirmed THEN the flow is reset to root`() {
        val group = createTabGroup(id = "group-1", tabs = listOf(createTab("mozilla.org", id = "1")))
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.ExpandedTabGroup(group = group),
                        TabGroupFlowDestination.CloseTabAndDeleteGroupConfirmationDialog(group = group),
                    ),
                entryPoint = TabGroupFlowEntryPoint.ViewGroup(groupId = "group-1"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.CloseTabAndDeleteGroupConfirmed(group = group),
            )
        assertEquals(expected = listOf(TabGroupFlowDestination.Root), actual = resultState.backStack)
    }

    @Test
    fun `WHEN EditTabGroupClicked invoked THEN the form is initialized and the edit sheet is pushed`() {
        val group = createTabGroup(id = "group-1", title = "Work")
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.ExpandedTabGroup(group = group),
                    ),
                entryPoint = TabGroupFlowEntryPoint.ViewGroup(groupId = "group-1"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.EditTabGroupClicked(group = group),
            )
        assertEquals(expected = "group-1", actual = resultState.formState?.tabGroupId)
        assertEquals(expected = "Work", actual = resultState.formState?.name)
        assertEquals(expected = TabGroupFlowDestination.EditTabGroup, actual = resultState.backStack.last())
    }

    @Test
    fun `WHEN DeleteClicked invoked THEN the delete confirmation dialog is pushed`() {
        val group = createTabGroup(id = "group-1")
        val initialState =
            TabGroupFlowState(
                backStack = listOf(TabGroupFlowDestination.Root),
                entryPoint = TabGroupFlowEntryPoint.ViewGroup(groupId = "group-1"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(state = initialState, action = TabGroupAction.DeleteClicked(group = group))
        assertEquals(
            expected = TabGroupFlowDestination.DeleteTabGroupConfirmationDialog(group = group),
            actual = resultState.backStack.last(),
        )
    }

    @Test
    fun `WHEN UngroupConfirmationRequested invoked THEN the ungroup confirmation dialog is pushed`() {
        val group = createTabGroup(id = "group-1")
        val initialState =
            TabGroupFlowState(
                backStack = listOf(TabGroupFlowDestination.Root),
                entryPoint = TabGroupFlowEntryPoint.ViewGroup(groupId = "group-1"),
            )
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.UngroupConfirmationRequested(group = group),
            )
        assertEquals(
            expected = TabGroupFlowDestination.UngroupTabGroupConfirmationDialog(group = group),
            actual = resultState.backStack.last(),
        )
    }

    @Test
    fun `WHEN a group operation is confirmed THEN the flow is reset to root`() {
        val group = createTabGroup(id = "group-1")
        val initialState =
            TabGroupFlowState(
                backStack =
                    listOf(
                        TabGroupFlowDestination.Root,
                        TabGroupFlowDestination.ExpandedTabGroup(group = group),
                    ),
                entryPoint = TabGroupFlowEntryPoint.ViewGroup(groupId = "group-1"),
            )
        val confirmations =
            listOf(
                TabGroupAction.CloseTabGroupClicked(group = group),
                TabGroupAction.DeleteConfirmed(group = group),
                TabGroupAction.UngroupConfirmed(group = group, dontAskAgain = false),
            )
        confirmations.forEach { action ->
            val resultState = TabGroupFlowReducer.reduce(state = initialState, action = action)
            assertEquals(
                expected = listOf(TabGroupFlowDestination.Root),
                actual = resultState.backStack,
                message = "Expected $action to reset the backstack to root, but got ${resultState.backStack}",
            )
        }
    }
}
