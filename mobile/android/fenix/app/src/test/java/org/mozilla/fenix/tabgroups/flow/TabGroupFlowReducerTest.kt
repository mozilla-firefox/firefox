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
    fun `WHEN TabMovedToExistingTabGroup invoked THEN the move sheet is popped`() {
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
                action = TabGroupAction.TabMovedToExistingTabGroup(tabId = "123", groupId = "456", groupTitle = "Work"),
            )
        assertEquals(
            expected = listOf(TabGroupFlowDestination.Root),
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
}
