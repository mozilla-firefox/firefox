/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.appstate.setup.checklist

import android.content.Context
import mozilla.components.lib.state.State
import org.mozilla.fenix.R

private const val NO_TASKS = 0
private const val ONE_TASK = 1
private const val TWO_TASKS = 2
private const val THREE_TASKS = 3
private const val FOUR_TASKS = 4
private const val FIVE_TASKS = 5
private const val SIX_TASKS = 6

private const val TASK_COUNT_WITH_TAB_STRIP_DISABLED = 6
private const val TASK_COUNT_WITH_TAB_STRIP_ENABLED = 5

/**
 * Represents the [State] of the setup checklist feature.
 *
 * @property isVisible Whether the setup checklist feature should be visible to the user.
 * This is a temporary state to remove the setup view immediately without having to update the
 * entire AppState. Visibility is handled by the preference once the app is restarted thereon.
 * @property checklistItems The list of checklist items.
 * @property progress The progress of the current checklist.
 */
data class SetupChecklistState(
    val isVisible: Boolean = true,
    val checklistItems: List<ChecklistItem> = emptyList(),
    val progress: Progress = checklistItems.getTaskProgress(),
) : State

/**
 * Returns the title of the setup checklist based on the current progress.
 */
fun getSetupChecklistTitle(context: Context, allTasksCompleted: Boolean) = with(context) {
    if (allTasksCompleted) {
        getString(R.string.setup_checklist_title_state_completed)
    } else {
        getString(
            R.string.setup_checklist_title_state_incomplete,
            getString(R.string.firefox),
        )
    }
}

/**
 * Returns the subtitle of the setup checklist based on the current progress.
 */
fun getSetupChecklistSubtitle(context: Context, progress: Progress, isGroups: Boolean) =
    with(context) {
        if (isGroups) {
            subtitleForGroups(context, progress)
        } else {
            subtitleForTasks(progress)
        }
    }

private fun subtitleForGroups(context: Context, progress: Progress) = with(context) {
    when (progress.totalTasks) {
        TASK_COUNT_WITH_TAB_STRIP_DISABLED -> subtitleForGroupsWith6Tasks(progress)
        TASK_COUNT_WITH_TAB_STRIP_ENABLED -> subtitleForGroupsWith5Tasks(progress)
        else -> null
    }
}

private fun Context.subtitleForGroupsWith6Tasks(progress: Progress) =
    when (progress.completedTasks) {
        NO_TASKS -> getString(
            R.string.setup_checklist_subtitle_6_steps_initial_state,
            getString(R.string.firefox),
        )

        ONE_TASK -> getString(R.string.setup_checklist_subtitle_6_steps_first_step)
        TWO_TASKS -> getString(R.string.setup_checklist_subtitle_6_steps_second_step)
        THREE_TASKS -> getString(R.string.setup_checklist_subtitle_6_steps_third_step)
        FOUR_TASKS -> getString(R.string.setup_checklist_subtitle_6_steps_fourth_step)
        FIVE_TASKS -> getString(R.string.setup_checklist_subtitle_6_steps_fifth_step)
        SIX_TASKS -> getString(
            R.string.setup_checklist_subtitle_6_steps_completed_state,
            getString(R.string.firefox),
        )

        else -> null
    }

private fun Context.subtitleForGroupsWith5Tasks(progress: Progress) =
    when (progress.completedTasks) {
        NO_TASKS -> getString(
            R.string.setup_checklist_subtitle_5_steps_initial_state,
            getString(R.string.firefox),
        )

        ONE_TASK -> getString(R.string.setup_checklist_subtitle_5_steps_first_step)
        TWO_TASKS -> getString(R.string.setup_checklist_subtitle_5_steps_second_step)
        THREE_TASKS -> getString(R.string.setup_checklist_subtitle_5_steps_third_step)
        FOUR_TASKS -> getString(R.string.setup_checklist_subtitle_5_steps_fourth_step)
        FIVE_TASKS -> getString(
            R.string.setup_checklist_subtitle_5_steps_completed_state,
            getString(R.string.firefox),
        )

        else -> null
    }

private fun Context.subtitleForTasks(progress: Progress) = when (progress.completedTasks) {
    NO_TASKS -> getString(
        R.string.setup_checklist_subtitle_3_steps_initial_state,
        getString(R.string.firefox),
    )

    ONE_TASK -> getString(R.string.setup_checklist_subtitle_3_steps_first_step)
    TWO_TASKS -> getString(R.string.setup_checklist_subtitle_3_steps_second_step)
    THREE_TASKS -> getString(
        R.string.setup_checklist_subtitle_3_steps_completed_state,
        getString(R.string.firefox),
    )

    else -> null
}
