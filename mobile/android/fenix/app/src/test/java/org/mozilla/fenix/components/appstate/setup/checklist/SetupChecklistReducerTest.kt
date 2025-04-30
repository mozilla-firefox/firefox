/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.appstate.setup.checklist

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppState

class SetupChecklistReducerTest {
    @Test
    fun `WHEN init action THEN the reduced state remains the same`() {
        val appState = AppState(setupChecklistState = SetupChecklistState())

        val reducedState =
            SetupChecklistReducer.reduce(appState, AppAction.SetupChecklistAction.Init)

        assertEquals(appState, reducedState)
    }

    @Test
    fun `WHEN closed action THEN the reduced state visible state is updated`() {
        val appState = AppState(setupChecklistState = SetupChecklistState())

        val reducedState =
            SetupChecklistReducer.reduce(appState, AppAction.SetupChecklistAction.Closed)

        val expected = appState.copy(setupChecklistState = SetupChecklistState(isVisible = false))

        assertEquals(expected, reducedState)
    }

    @Test
    fun `WHEN a group item is clicked action THEN the reduced state group's expanded state is updated`() {
        val expandedGroup = ChecklistItem.Group(
            title = R.string.setup_checklist_group_essentials,
            tasks = listOf(
                ChecklistItem.Task(
                    type = ChecklistItem.Task.Type.SET_AS_DEFAULT,
                    title = R.string.setup_checklist_task_default_browser,
                    icon = R.drawable.ic_addons_extensions,
                    isCompleted = false,
                ),
            ),
            isExpanded = true,
        )
        val collapsedGroup = ChecklistItem.Group(
            title = R.string.setup_checklist_group_helpful_tools,
            tasks = listOf(
                ChecklistItem.Task(
                    type = ChecklistItem.Task.Type.INSTALL_SEARCH_WIDGET,
                    title = R.string.setup_checklist_task_explore_extensions,
                    icon = R.drawable.ic_addons_extensions,
                    isCompleted = false,
                ),
            ),
            isExpanded = false,
        )

        val appState = AppState(
            setupChecklistState = SetupChecklistState(
                checklistItems = listOf(
                    expandedGroup,
                    collapsedGroup,
                ),
            ),
        )

        // Verify that the expanded group is expanded, and the other one is not
        assertTrue((appState.setupChecklistState!!.checklistItems[0] as ChecklistItem.Group).isExpanded)
        assertFalse((appState.setupChecklistState!!.checklistItems[1] as ChecklistItem.Group).isExpanded)

        val reducedState = SetupChecklistReducer.reduce(
            appState,
            AppAction.SetupChecklistAction.ChecklistItemClicked(collapsedGroup),
        )

        // Verify that the expanded group was collapsed, and the other one got expanded
        assertFalse((reducedState.setupChecklistState!!.checklistItems[0] as ChecklistItem.Group).isExpanded)
        assertTrue((reducedState.setupChecklistState!!.checklistItems[1] as ChecklistItem.Group).isExpanded)
    }

    @Test
    fun `WHEN a task item is clicked action is dispatched THEN the reduced states task's completed state is not updated`() {
        val task = ChecklistItem.Task(
            type = ChecklistItem.Task.Type.EXPLORE_EXTENSION,
            title = R.string.setup_checklist_task_default_browser,
            icon = R.drawable.ic_addons_extensions,
            isCompleted = false,
        )

        val appState =
            AppState(setupChecklistState = SetupChecklistState(checklistItems = listOf(task)))
        val reducedState = SetupChecklistReducer.reduce(
            appState,
            AppAction.SetupChecklistAction.ChecklistItemClicked(task),
        )

        assertFalse((reducedState.setupChecklistState!!.checklistItems[0] as ChecklistItem.Task).isCompleted)
    }

    @Test
    fun `WHEN a task preference updated action is dispatched THEN the reduced states task's completed state is updated`() {
        val task = ChecklistItem.Task(
            type = ChecklistItem.Task.Type.EXPLORE_EXTENSION,
            title = R.string.setup_checklist_task_explore_extensions,
            icon = R.drawable.ic_addons_extensions,
            isCompleted = false,
        )

        val appState =
            AppState(setupChecklistState = SetupChecklistState(checklistItems = listOf(task)))
        val reducedState = SetupChecklistReducer.reduce(
            appState,
            AppAction.SetupChecklistAction.TaskPreferenceUpdated(task.type, true),
        )

        assertTrue((reducedState.setupChecklistState!!.checklistItems[0] as ChecklistItem.Task).isCompleted)

        val reducedState2 = SetupChecklistReducer.reduce(
            reducedState,
            AppAction.SetupChecklistAction.TaskPreferenceUpdated(task.type, false),
        )

        assertFalse((reducedState2.setupChecklistState!!.checklistItems[0] as ChecklistItem.Task).isCompleted)
    }

    @Test
    fun `WHEN a groups task preference is updated THEN only the reduced state task's completed state is updated`() {
        val updatedTask = ChecklistItem.Task(
            type = ChecklistItem.Task.Type.SET_AS_DEFAULT,
            title = R.string.setup_checklist_task_default_browser,
            icon = R.drawable.ic_addons_extensions,
            isCompleted = false,
        )
        val nonUpdatedTask = ChecklistItem.Task(
            type = ChecklistItem.Task.Type.INSTALL_SEARCH_WIDGET,
            title = R.string.setup_checklist_task_default_browser,
            icon = R.drawable.ic_addons_extensions,
            isCompleted = false,
        )
        val group = ChecklistItem.Group(
            title = R.string.setup_checklist_group_customize,
            tasks = listOf(updatedTask, nonUpdatedTask),
            isExpanded = true,
        )

        val appState =
            AppState(setupChecklistState = SetupChecklistState(checklistItems = listOf(group)))
        val reducedState = SetupChecklistReducer.reduce(
            appState,
            AppAction.SetupChecklistAction.TaskPreferenceUpdated(updatedTask.type, true),
        )

        assertTrue((reducedState.setupChecklistState!!.checklistItems[0] as ChecklistItem.Group).tasks[0].isCompleted)
        assertFalse((reducedState.setupChecklistState!!.checklistItems[0] as ChecklistItem.Group).tasks[1].isCompleted)

        val reducedState2 = SetupChecklistReducer.reduce(
            reducedState,
            AppAction.SetupChecklistAction.TaskPreferenceUpdated(updatedTask.type, false),
        )

        assertFalse((reducedState2.setupChecklistState!!.checklistItems[0] as ChecklistItem.Group).tasks[0].isCompleted)
        assertFalse((reducedState2.setupChecklistState!!.checklistItems[0] as ChecklistItem.Group).tasks[1].isCompleted)
    }
}
