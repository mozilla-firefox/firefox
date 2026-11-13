/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabgroups.flow

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction

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
                    )
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
        val initialState = TabGroupFlowState(backStack = listOf(TabGroupFlowDestination.Root))
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
                    )
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
        val initialState = TabGroupFlowState()
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
        val initialState = TabGroupFlowState()
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabAddedToNewTabGroup(tabId = "123"),
            )
        assertNotNull(resultState.formState)
    }

    @Test
    fun `WHEN TabAddedToExistingTabGroup invoked backstack is set up correctly`() {
        val initialState = TabGroupFlowState()
        val resultState =
            TabGroupFlowReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabAddedToExistingTabGroup(tabId = "123", groupId = "123"),
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
                    )
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
    fun `WHEN TabGroupsUpdated invoked THEN groups are replaced`() {
        val initialState = TabGroupFlowState(groups = listOf(createTabGroup(id = "old")))
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
}
