/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.appstate.setup.checklist

import org.mozilla.fenix.components.appstate.AppAction.SetupChecklistAction
import org.mozilla.fenix.components.appstate.AppState

/**
 * Helper object which reduces [SetupChecklistAction]s.
 */
internal object SetupChecklistReducer {

    /**
     * Reduces [SetupChecklistAction]s and performs any necessary state mutations.
     *
     * @param state The current snapshot of [AppState].
     * @param action The [SetupChecklistAction] being reduced.
     * @return The resulting [AppState] after the given [action] has been reduced.
     */
    fun reduce(state: AppState, action: SetupChecklistAction): AppState = when (action) {
        is SetupChecklistAction.Init -> state
        is SetupChecklistAction.Closed -> state.copy(
            setupChecklistState = state.setupChecklistState?.copy(isVisible = false),
        )

        is SetupChecklistAction.ChecklistItemClicked -> {
            // A local variable to clearly distinguish between
            // the item in the list and the item from the action.
            val clickedItem = action.item
            if (clickedItem is ChecklistItem.Group && state.setupChecklistState != null) {
                // Task updates are handled by TaskPreferenceUpdated.
                val updatedGroups =
                    state.setupChecklistState.checklistItems.filterIsInstance<ChecklistItem.Group>()
                        .map { existingGroup ->
                            if (existingGroup == clickedItem) {
                                existingGroup.copy(isExpanded = !clickedItem.isExpanded)
                            } else {
                                // Only one group should be expanded at a time.
                                existingGroup.copy(isExpanded = false)
                            }
                        }

                state.copy(
                    setupChecklistState = state.setupChecklistState.copy(
                        checklistItems = updatedGroups,
                        progress = updatedGroups.getTaskProgress(),
                    ),
                )
            } else {
                state
            }
        }

        is SetupChecklistAction.TaskPreferenceUpdated -> {
            state.setupChecklistState?.let {
                val updatedItems = it.checklistItems.map { existingItem ->
                    val updatedTaskType = action.taskType
                    when (existingItem) {
                        // Given the updated item is a task, when we find a group in the list,
                        // check the group tasks for the updated one to change its completed state.
                        is ChecklistItem.Group -> {
                            existingItem.copy(
                                tasks = existingItem.tasks.map { existingTask ->
                                    if (existingTask.type == updatedTaskType) {
                                        existingTask.copy(isCompleted = action.prefValue)
                                    } else {
                                        existingTask
                                    }
                                },
                            )
                        }

                        // Given the updated item is a task, when we find a task in the list, we
                        // change its completed state.
                        is ChecklistItem.Task -> if (existingItem.type == updatedTaskType) {
                            existingItem.copy(isCompleted = action.prefValue)
                        } else {
                            existingItem
                        }
                    }
                }

                state.copy(
                    setupChecklistState = state.setupChecklistState.copy(
                        checklistItems = updatedItems,
                        progress = updatedItems.getTaskProgress(),
                    ),
                )
            } ?: state
        }
    }
}
