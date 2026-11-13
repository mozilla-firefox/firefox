/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.redux.reducer

import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue
import org.junit.Test
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination.AddToTabGroup
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination.CloseTabAndDeleteGroupConfirmationDialog
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination.DeleteTabGroupConfirmationDialog
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination.EditTabGroup
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination.ExpandedTabGroup
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination.UngroupTabGroupConfirmationDialog
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState.Mode
import org.mozilla.fenix.tabstray.redux.state.initializeTabGroupForm

class TabGroupReducerTest {
    @Test
    fun `WHEN NameChanged AND form state exists THEN name is updated and edited is true`() {
        val initialState =
            TabsTrayState(
                tabGroupState =
                    TabsTrayState.TabGroupState(
                        formState =
                            TabGroupFormState(
                                tabGroupId = "1",
                                name = "Previous name",
                                edited = false,
                            )
                    )
            )

        val newName = "New name"

        val resultState =
            TabGroupActionReducer.reduce(
                initialState,
                TabGroupAction.NameChanged(newName),
            )

        val initialStateForm = requireNotNull(initialState.tabGroupState.formState)
        val expectedState =
            initialState.copy(
                tabGroupState =
                    TabsTrayState.TabGroupState(
                        formState =
                            initialStateForm.copy(
                                name = newName,
                                edited = true,
                            )
                    )
            )

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `WHEN SaveClicked THEN multi-select mode is exited and the tab group flow is closed`() {
        val formState =
            TabGroupFormState(
                tabGroupId = "1",
                name = "Tab Group 1",
                edited = true,
            )
        val initialState =
            TabsTrayState(
                mode = Mode.Select(selectedTabs = setOf()),
                tabGroupState =
                    TabsTrayState.TabGroupState(
                        formState = formState,
                        dragProcessingState = TabsTrayState.DragProcessingState.COMPLETED,
                    ),
                backStack =
                    listOf(
                        TabsTrayState().backStack.first(),
                        AddToTabGroup,
                        EditTabGroup,
                    ),
            )

        val resultState = TabGroupActionReducer.reduce(initialState, TabGroupAction.SaveClicked)

        val expectedState =
            initialState.copy(
                mode = Mode.Normal,
                backStack = TabsTrayState().backStack,
            )

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `WHEN SaveClicked THEN drag and drop mode is exited and the tab group flow is closed`() {
        val formState =
            TabGroupFormState(
                tabGroupId = null,
                name = "Tab Group 1",
                edited = true,
            )
        val initialState =
            TabsTrayState(
                mode = Mode.DragAndDrop(sourceId = "12345", destinationId = "54321"),
                tabGroupState = TabsTrayState.TabGroupState(formState = formState),
                backStack =
                    listOf(
                        TabsTrayState().backStack.first(),
                        AddToTabGroup,
                        EditTabGroup,
                    ),
            )

        val resultState = TabGroupActionReducer.reduce(initialState, TabGroupAction.SaveClicked)

        val expectedState =
            initialState.copy(
                mode = Mode.Normal,
                backStack = TabsTrayState().backStack,
                tabGroupState =
                    initialState.tabGroupState.copy(dragProcessingState = TabsTrayState.DragProcessingState.COMPLETED),
            )

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `GIVEN the user is not in multiselect mode WHEN a tab group is clicked THEN navigate to the expanded tab group destination`() {
        val initialState = TabsTrayState(mode = Mode.Normal)
        val expectedTabGroup = createTabGroup()
        val expectedBackStack = initialState.backStack + ExpandedTabGroup(group = expectedTabGroup)
        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabGroupClicked(group = expectedTabGroup),
            )

        assertEquals(expectedBackStack, resultState.backStack)
    }

    @Test
    fun `GIVEN the user is in multiselect mode WHEN a tab group is clicked THEN do not navigate away`() {
        val initialState = TabsTrayState(mode = Mode.Select(selectedTabs = setOf()))
        val expectedTabGroup = createTabGroup()
        val expectedBackStack = initialState.backStack
        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabGroupClicked(group = expectedTabGroup),
            )

        assertEquals(expectedBackStack, resultState.backStack)
    }

    @Test
    fun `WHEN open tab group is clicked from tab groups page THEN switch to normal tabs, reopen group, and show expanded sheet`() {
        val tabGroup = createTabGroup(closed = true)
        val initialState =
            TabsTrayState(
                selectedPage = Page.TabGroups,
                backStack = TabsTrayState().backStack,
            )

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.OpenTabGroupClicked(group = tabGroup),
            )

        val expectedState =
            initialState.copy(
                selectedPage = Page.NormalTabs,
                mode = Mode.Normal,
                backStack = initialState.backStack + ExpandedTabGroup(group = tabGroup.copy(closed = false)),
            )

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `WHEN OpenCreatedTabGroup is reduced THEN switch to normal tabs and show the expanded sheet`() {
        val tabGroup = createTabGroup()
        val initialState =
            TabsTrayState(
                selectedPage = Page.TabGroups,
                backStack = TabsTrayState().backStack,
            )

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.OpenCreatedTabGroup(group = tabGroup),
            )

        val expectedState =
            initialState.copy(
                selectedPage = Page.NormalTabs,
                backStack = initialState.backStack + ExpandedTabGroup(group = tabGroup.copy(closed = false)),
            )

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `WHEN ungroup is clicked THEN navigate to the ungroup confirmation dialog`() {
        val group = createTabGroup()
        val initialState = TabsTrayState()

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.UngroupConfirmationRequested(group = group),
            )

        assertEquals(
            initialState.backStack + UngroupTabGroupConfirmationDialog(group = group),
            resultState.backStack,
        )
    }

    @Test
    fun `WHEN an ungroup is requested THEN the state is unchanged`() {
        val initialState = TabsTrayState()

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.UngroupRequested(group = createTabGroup()),
            )

        assertEquals(initialState, resultState)
    }

    @Test
    fun `WHEN ungroup is confirmed THEN pop the confirmation dialog`() {
        val group = createTabGroup()
        val initialState =
            TabsTrayState(
                backStack =
                    listOf(
                        TabsTrayState().backStack.first(),
                        UngroupTabGroupConfirmationDialog(group = group),
                    )
            )

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.UngroupConfirmed(group = group, dontAskAgain = false),
            )

        assertEquals(TabsTrayState().backStack, resultState.backStack)
    }

    @Test
    fun `WHEN ungroup is confirmed from expanded tab group THEN pop the confirmation dialog and expanded tab group`() {
        val group = createTabGroup()
        val initialState =
            TabsTrayState(
                backStack =
                    listOf(
                        TabsTrayState().backStack.first(),
                        ExpandedTabGroup(group = group),
                        UngroupTabGroupConfirmationDialog(group = group),
                    )
            )

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.UngroupConfirmed(group = group, dontAskAgain = true),
            )

        assertEquals(TabsTrayState().backStack, resultState.backStack)
    }

    @Test
    fun `WHEN delete is confirmed from expanded tab group THEN pop the confirmation dialog and expanded tab group`() {
        val group = createTabGroup()
        val initialState =
            TabsTrayState(
                backStack =
                    listOf(
                        TabsTrayState().backStack.first(),
                        ExpandedTabGroup(group = group),
                        DeleteTabGroupConfirmationDialog(group = group),
                    )
            )

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.DeleteConfirmed(group = group),
            )

        assertEquals(TabsTrayState().backStack, resultState.backStack)
    }

    @Test
    fun `WHEN delete is confirmed from root tab manager THEN pop the confirmation dialog`() {
        val group = createTabGroup()
        val initialState =
            TabsTrayState(
                backStack =
                    listOf(
                        TabsTrayState().backStack.first(),
                        DeleteTabGroupConfirmationDialog(group = group),
                    )
            )

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.DeleteConfirmed(group = group),
            )

        assertEquals(TabsTrayState().backStack, resultState.backStack)
    }

    @Test
    fun `WHEN add to new tab group is clicked THEN navigate to create tab group destination`() {
        val initialState = TabsTrayState()

        val expectedFormState = initialState.initializeTabGroupForm()
        val expectedBackStack = initialState.backStack + EditTabGroup

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.AddToNewTabGroup,
            )

        assertEquals(expectedFormState, resultState.tabGroupState.formState)
        assertEquals(expectedBackStack, resultState.backStack)
    }

    @Test
    fun `WHEN the new tab group FAB is clicked THEN navigate to create a starter tab group`() {
        val initialState = TabsTrayState()

        val expectedFormState = initialState.initializeTabGroupForm(isStarterTabGroup = true)
        val expectedBackStack = initialState.backStack + EditTabGroup

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.NewTabGroupFabClicked,
            )

        assertEquals(expectedFormState, resultState.tabGroupState.formState)
        assertEquals(expectedBackStack, resultState.backStack)
    }

    @Test
    fun `WHEN the new tab group menu item is clicked THEN navigate to create a starter tab group`() {
        val initialState = TabsTrayState()

        val expectedFormState = initialState.initializeTabGroupForm(isStarterTabGroup = true)
        val expectedBackStack = initialState.backStack + EditTabGroup

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.NewTabGroupMenuClicked,
            )

        assertEquals(expectedFormState, resultState.tabGroupState.formState)
        assertEquals(expectedBackStack, resultState.backStack)
    }

    @Test
    fun `WHEN ThemeChanged is called THEN theme is updated`() {
        val initialFormState = TabGroupFormState(tabGroupId = "123", name = "123", theme = TabGroupTheme.Blue)

        val resultState =
            TabGroupActionReducer.reduce(
                state = TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(formState = initialFormState)),
                action = TabGroupAction.ThemeChanged(theme = TabGroupTheme.Pink),
            )

        assertEquals(resultState.tabGroupState.formState!!.theme, TabGroupTheme.Pink)
    }

    @Test(expected = IllegalArgumentException::class)
    fun `WHEN ThemeChanged is called with null form THEN exception is thrown`() {
        val resultState =
            TabGroupActionReducer.reduce(
                state = TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(formState = null)),
                action = TabGroupAction.ThemeChanged(theme = TabGroupTheme.Pink),
            )

        assertEquals(resultState.tabGroupState.formState!!.theme, TabGroupTheme.Pink)
    }

    @Test
    fun `WHEN no groups exist the default next number is 1`() {
        val resultState =
            TabGroupActionReducer.reduce(
                state = TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(groups = emptyList())),
                action = TabGroupAction.AddToNewTabGroup,
            )

        assertEquals(1, resultState.tabGroupState.formState!!.nextTabGroupNumber)
    }

    @Test
    fun `WHEN 1 group exists the default next number is 2`() {
        val resultState =
            TabGroupActionReducer.reduce(
                state =
                    TabsTrayState(
                        tabGroupState =
                            TabsTrayState.TabGroupState(
                                groups =
                                    listOf(
                                        TabsTrayItem.TabGroup(
                                            title = "Group 1",
                                            theme = TabGroupTheme.Yellow,
                                            tabs = mutableListOf(),
                                        )
                                    )
                            )
                    ),
                action = TabGroupAction.AddToNewTabGroup,
            )

        assertEquals(2, resultState.tabGroupState.formState!!.nextTabGroupNumber)
    }

    @Test
    fun `WHEN 99 groups exist the default next number is 100`() {
        val resultState =
            TabGroupActionReducer.reduce(
                state =
                    TabsTrayState(
                        tabGroupState =
                            TabsTrayState.TabGroupState(
                                groups =
                                    List(99) {
                                        TabsTrayItem.TabGroup(
                                            title = "Group $it",
                                            theme = TabGroupTheme.Yellow,
                                            tabs = mutableListOf(),
                                        )
                                    }
                            )
                    ),
                action = TabGroupAction.AddToNewTabGroup,
            )

        assertEquals(100, resultState.tabGroupState.formState!!.nextTabGroupNumber)
    }

    @Test
    fun `WHEN creating a new tab group THEN default theme is next from most recently used theme`() {
        val initialState =
            TabsTrayState(
                tabGroupState =
                    TabsTrayState.TabGroupState(
                        groups =
                            listOf(
                                TabsTrayItem.TabGroup(
                                    title = "Older",
                                    theme = TabGroupTheme.Yellow,
                                    tabs = mutableListOf(),
                                    lastModified = 1L,
                                ),
                                TabsTrayItem.TabGroup(
                                    title = "Newer",
                                    theme = TabGroupTheme.Red,
                                    tabs = mutableListOf(),
                                    lastModified = 2L,
                                ),
                            )
                    )
            )
        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.AddToNewTabGroup,
            )
        val expectedState =
            initialState.copy(
                tabGroupState =
                    initialState.tabGroupState.copy(
                        formState =
                            TabGroupFormState(
                                tabGroupId = null,
                                name = "",
                                nextTabGroupNumber = 3,
                                theme = TabGroupTheme.Pink,
                                edited = false,
                            ),
                        dragProcessingState = TabsTrayState.DragProcessingState.EDIT_IN_PROGRESS,
                    ),
                backStack = initialState.backStack + EditTabGroup,
            )

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `WHEN most recently used theme is last in sequence THEN default theme wraps to first theme`() {
        val initialState =
            TabsTrayState(
                tabGroupState =
                    TabsTrayState.TabGroupState(
                        groups =
                            listOf(
                                TabsTrayItem.TabGroup(
                                    title = "Latest",
                                    theme = TabGroupTheme.Grey,
                                    tabs = mutableListOf(),
                                    lastModified = 5L,
                                )
                            )
                    )
            )
        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.AddToNewTabGroup,
            )
        val expectedState =
            initialState.copy(
                tabGroupState =
                    initialState.tabGroupState.copy(
                        formState =
                            TabGroupFormState(
                                tabGroupId = null,
                                name = "",
                                nextTabGroupNumber = 2,
                                theme = TabGroupTheme.Yellow,
                                edited = false,
                            ),
                        dragProcessingState = TabsTrayState.DragProcessingState.EDIT_IN_PROGRESS,
                    ),
                backStack = initialState.backStack + EditTabGroup,
            )

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `WHEN tabs are added to a group via multiselection THEN multiselection is exited and navigate back to the root`() {
        val resultState =
            TabGroupActionReducer.reduce(
                state =
                    TabsTrayState(
                        mode = Mode.Select(),
                        backStack = TabsTrayState().backStack + AddToTabGroup,
                    ),
                action = TabGroupAction.SelectedTabsAddedToGroup(groupId = "12345"),
            )
        val expectedState =
            TabsTrayState(
                mode = Mode.Normal,
                backStack = TabsTrayState().backStack,
            )

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `WHEN the user adds a single tab to a group THEN the state is unchanged`() {
        val resultState =
            TabGroupActionReducer.reduce(
                state = TabsTrayState(),
                action =
                    TabGroupAction.TabAddedToExistingTabGroup(
                        tabId = "54321",
                        groupId = "12345",
                        groupTitle = "Tab Group 1",
                    ),
            )
        assertEquals(TabsTrayState(), resultState)
    }

    @Test
    fun `GIVEN the user has at least 1 tab group WHEN the user clicks to add tabs to a group THEN navigate to the ADD TO GROUP flow`() {
        val initialState = TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(groups = listOf(createTabGroup())))
        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.AddToTabGroup,
            )
        val expectedState = initialState.copy(backStack = initialState.backStack + AddToTabGroup)

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `GIVEN the user has no tab groups WHEN the user clicks to add tabs to a group THEN navigate to the EDIT_CREATE GROUP flow`() {
        val initialState = TabsTrayState()
        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.AddToTabGroup,
            )
        val expectedState =
            initialState.copy(
                tabGroupState =
                    initialState.tabGroupState.copy(
                        formState = initialState.initializeTabGroupForm(),
                        dragProcessingState = TabsTrayState.DragProcessingState.EDIT_IN_PROGRESS,
                    ),
                backStack = initialState.backStack + EditTabGroup,
            )

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `WHEN the user clicks to edit an existing tab group THEN navigate to the edit flow`() {
        val group = createTabGroup()
        val initialState = TabsTrayState()
        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.EditTabGroupClicked(group = group),
            )
        val expectedState =
            initialState.copy(
                tabGroupState = initialState.tabGroupState.copy(formState = group.initializeTabGroupForm()),
                backStack = initialState.backStack + EditTabGroup,
            )

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `WHEN a user clicks on an unselected tab group during multiselection THEN the group and its tabs are added to the selection state`() {
        val tabs = List(size = 20) { createTab(url = "") }
        val tabGroup = createTabGroup(tabs = MutableList(size = 20) { createTab(url = "") })
        val initialState =
            TabsTrayState(
                normalTabsState = TabsTrayState.NormalTabsState(items = tabs + tabGroup),
                mode =
                    Mode.Select(
                        selectedTabs = emptySet(),
                        selectedTabGroups = emptySet(),
                    ),
                tabGroupState = TabsTrayState.TabGroupState(groups = listOf(tabGroup)),
            )
        val resultState =
            TabsTrayReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabGroupClicked(group = tabGroup),
            )
        val expectedState =
            initialState.copy(
                mode =
                    Mode.Select(
                        selectedTabs = tabGroup.tabs.toSet(),
                        selectedTabGroups = setOf(tabGroup),
                    )
            )

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `WHEN a user clicks on a selected tab group during multiselection THEN the group and its tabs are removed from the selection state`() {
        val tabs = List(size = 20) { createTab(url = "") }
        val tabGroup = createTabGroup(tabs = MutableList(size = 20) { createTab(url = "") })
        val initialState =
            TabsTrayState(
                normalTabsState = TabsTrayState.NormalTabsState(items = tabs + tabGroup),
                mode =
                    Mode.Select(
                        selectedTabs = tabGroup.tabs.toSet(),
                        selectedTabGroups = setOf(tabGroup),
                    ),
                tabGroupState = TabsTrayState.TabGroupState(groups = listOf(tabGroup)),
            )
        val resultState =
            TabsTrayReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabGroupClicked(group = tabGroup),
            )
        val expectedState = initialState.copy(mode = Mode.Normal)

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `GIVEN there is a tab and a tab group selected WHEN a user taps clicks on the selected tab group during multiselection THEN the group and its tabs are removed from the selection state`() {
        val tabs = List(size = 20) { createTab(url = "") }
        val tabGroup = createTabGroup(tabs = MutableList(size = 20) { createTab(url = "") })
        val initialState =
            TabsTrayState(
                normalTabsState = TabsTrayState.NormalTabsState(items = tabs + tabGroup),
                mode =
                    Mode.Select(
                        selectedTabs = tabGroup.tabs.toSet() + tabs[0],
                        selectedTabGroups = setOf(tabGroup),
                    ),
                tabGroupState = TabsTrayState.TabGroupState(groups = listOf(tabGroup)),
            )
        val resultState =
            TabsTrayReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabGroupClicked(group = tabGroup),
            )
        val expectedState =
            initialState.copy(
                mode =
                    Mode.Select(
                        selectedTabs = setOf(tabs[0]),
                        selectedTabGroups = emptySet(),
                    )
            )

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `WHEN the user closes a tab group THEN navigate back to the root`() {
        val tabs = List(size = 20) { createTab(url = "") }
        val tabGroup = createTabGroup(tabs = MutableList(size = 20) { createTab(url = "") })
        val initialState =
            TabsTrayState(
                normalTabsState = TabsTrayState.NormalTabsState(items = tabs + tabGroup),
                tabGroupState = TabsTrayState.TabGroupState(groups = listOf(tabGroup)),
                backStack = listOf(TabManagerNavDestination.Root, ExpandedTabGroup(group = tabGroup)),
            )
        val resultState =
            TabsTrayReducer.reduce(
                state = initialState,
                action = TabGroupAction.CloseTabGroupClicked(group = tabGroup),
            )
        val expectedState = initialState.copy(backStack = listOf(TabManagerNavDestination.Root))

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `WHEN TabClosed is dispatched AND group has 1 tab THEN navigate to the confirmation dialog`() {
        val tab = createTab(url = "")
        val group = createTabGroup(tabs = mutableListOf(tab))
        val initialState =
            TabsTrayState(
                backStack =
                    listOf(
                        TabsTrayState().backStack.first(),
                        ExpandedTabGroup(group = group),
                    )
            )

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabClosed(tab = tab, group = group),
            )

        val expectedState =
            initialState.copy(
                backStack = initialState.backStack + CloseTabAndDeleteGroupConfirmationDialog(group = group)
            )

        assertEquals(expectedState, resultState)
    }

    @Test
    fun `WHEN TabClosed is dispatched AND group has multiple tabs THEN state is unchanged`() {
        val tab1 = createTab(url = "")
        val tab2 = createTab(url = "")
        val group = createTabGroup(tabs = mutableListOf(tab1, tab2))
        val initialState =
            TabsTrayState(
                backStack =
                    listOf(
                        TabsTrayState().backStack.first(),
                        ExpandedTabGroup(group = group),
                    )
            )

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.TabClosed(tab = tab1, group = group),
            )

        assertEquals(initialState, resultState)
    }

    @Test
    fun `WHEN close tab and delete group is confirmed THEN pop the confirmation dialog and expanded tab group`() {
        val group = createTabGroup()
        val initialState =
            TabsTrayState(
                backStack =
                    listOf(
                        TabsTrayState().backStack.first(),
                        ExpandedTabGroup(group = group),
                        CloseTabAndDeleteGroupConfirmationDialog(group = group),
                    )
            )

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.CloseTabAndDeleteGroupConfirmed(group = group),
            )

        assertEquals(TabsTrayState().backStack, resultState.backStack)
    }

    @Test
    fun `WHEN the user completes a drag and drop action THEN the normal tabs page focus indicator is re-enabled`() {
        val initialState =
            TabsTrayState(normalTabsState = TabsTrayState.NormalTabsState(itemFocusIndicatorEnabled = false))
        val expectedState =
            initialState.copy(normalTabsState = initialState.normalTabsState.copy(itemFocusIndicatorEnabled = true))
        val resultState =
            TabGroupActionReducer.reduce(
                state = TabsTrayState(),
                action = TabGroupAction.DragAndDropInitiated(sourceId = "54321", destinationId = "12345"),
            )
        assertEquals(expected = expectedState, actual = resultState)
    }

    @Test
    fun `WHEN the user performs a drag and drop with two tabs THEN the state is updated with the drag and drop information`() {
        val draggedId = "54321"
        val destinationId = "12345"
        val initialState = TabsTrayState()
        val expectedState =
            initialState.copy(
                tabGroupState =
                    initialState.tabGroupState.copy(
                        formState =
                            TabGroupFormState(
                                tabGroupId = null,
                                name = "",
                                nextTabGroupNumber = 1,
                                theme = TabGroupTheme.Yellow,
                                edited = false,
                            ),
                        dragProcessingState = TabsTrayState.DragProcessingState.EDIT_IN_PROGRESS,
                    ),
                backStack = listOf(TabManagerNavDestination.Root, EditTabGroup),
                mode =
                    Mode.DragAndDrop(
                        sourceId = draggedId,
                        destinationId = destinationId,
                    ),
            )
        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.DragAndDropTwoTabs(sourceTabId = draggedId, destinationTabId = destinationId),
            )
        assertEquals(expected = expectedState, actual = resultState)
    }

    @Test
    fun `WHEN OnboardingDismissed THEN tab group onboarding is disabled in the config`() {
        val initialState = TabsTrayState(config = TabsTrayState.TabsTrayConfig(tabGroupsOnboardingEnabled = true))

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.OnboardingDismissed,
            )

        assertFalse(resultState.config.tabGroupsOnboardingEnabled)
    }

    @Test
    fun `WHEN OnboardingShown THEN the onboarding impression is recorded for the session`() {
        val resultState =
            TabGroupActionReducer.reduce(
                state = TabsTrayState(),
                action = TabGroupAction.OnboardingShown,
            )

        assertTrue(resultState.tabGroupState.hasRecordedOnboardingImpression)
    }

    @Test
    fun `WHEN NewGroupCreated THEN entering group is updated`() {
        val groupId = "group123"
        val initialState = TabsTrayState()

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.NewGroupCreated(groupId),
            )

        assertEquals(expected = groupId, actual = resultState.tabGroupState.enteringGroupId)
    }

    @Test
    fun `WHEN NewGroupAnimationPlayed THEN entering group is set to null`() {
        val initialState = TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(enteringGroupId = "group123"))

        val resultState =
            TabGroupActionReducer.reduce(
                state = initialState,
                action = TabGroupAction.NewGroupAnimationFinished,
            )

        assertNull(resultState.tabGroupState.enteringGroupId)
    }

    @Test
    fun `WHEN SaveClicked THEN creation state is completed`() {
        val formState =
            TabGroupFormState(
                tabGroupId = "1",
                name = "Tab Group 1",
                edited = true,
            )
        val initialState =
            TabsTrayState(
                mode = Mode.Select(selectedTabs = setOf()),
                tabGroupState = TabsTrayState.TabGroupState(formState = formState),
                backStack =
                    listOf(
                        TabsTrayState().backStack.first(),
                        AddToTabGroup,
                        EditTabGroup,
                    ),
            )

        val resultState = TabGroupActionReducer.reduce(initialState, TabGroupAction.SaveClicked)

        assertEquals(
            expected = TabsTrayState.DragProcessingState.COMPLETED,
            resultState.tabGroupState.dragProcessingState,
        )
    }

    @Test
    fun `WHEN DragAndDropHandlingCompleted THEN drag handling state is COMPLETED`() {
        val initialState = TabsTrayState()

        val resultState = TabGroupActionReducer.reduce(initialState, TabGroupAction.DragAndDropProcessed)

        assertEquals(
            expected = TabsTrayState.DragProcessingState.COMPLETED,
            resultState.tabGroupState.dragProcessingState,
        )
    }

    @Test
    fun `WHEN AddToNewGroup THEN drag handling state is set to EDIT_IN_PROGRESS`() {
        val initialState = TabsTrayState()
        val resultState = TabGroupActionReducer.reduce(initialState, TabGroupAction.AddToNewTabGroup)

        assertEquals(
            expected = TabsTrayState.DragProcessingState.EDIT_IN_PROGRESS,
            resultState.tabGroupState.dragProcessingState,
        )
    }

    @Test
    fun `WHEN CollectionsMigrationCardDismissed THEN the migration card is hidden`() {
        val initialState =
            TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(showCollectionsMigrationCard = true))

        val resultState = TabGroupActionReducer.reduce(initialState, TabGroupAction.CollectionsMigrationCardDismissed)

        assertFalse(resultState.tabGroupState.showCollectionsMigrationCard)
    }
}
