/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.appstate.setup.checklist

import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import org.mozilla.fenix.R
import org.mozilla.fenix.nimbus.SetupChecklistType
import org.mozilla.fenix.utils.Settings

/**
 * A sealed class representing an item in the setup checklist.
 *
 * @property title the string resource for the checklist item title.
 */
sealed class ChecklistItem(@StringRes open val title: Int) {
    /**
     * A data class representing an individual task in the checklist.
     *
     * @property type the type of the task.
     * @property title the string resource of the task's title.
     * @property icon the icon resource to be displayed on the right of the task.
     * @property isCompleted whether the task has been completed.
     */
    data class Task(
        val type: Type,
        @StringRes override val title: Int,
        @DrawableRes val icon: Int,
        val isCompleted: Boolean = false,
    ) : ChecklistItem(title) {

        /**
         * A check list task type.
         */
        enum class Type(val telemetryName: String) {
            SET_AS_DEFAULT("default-browser"),
            SIGN_IN("sign-in"),
            SELECT_THEME("theme-selection"),
            CHANGE_TOOLBAR_PLACEMENT("toolbar-selection"),
            INSTALL_SEARCH_WIDGET("search-widget"),
            EXPLORE_EXTENSION("extensions"),
        }
    }

    /**
     * A data class representing a group of tasks in the checklist.
     *
     * @property title the string resource of the group's title.
     * @property tasks the list of tasks that belong to this group.
     * @property isExpanded whether the group is currently expanded.
     */
    data class Group(
        @StringRes override val title: Int,
        val tasks: List<Task>,
        val isExpanded: Boolean = false,
    ) : ChecklistItem(title) {
        val progress: Progress = tasks.getTaskProgress()
    }
}

/**
 * A data class representing the task progress.
 *
 * @property totalTasks the total number of tasks in the checklist.
 * @property completedTasks the number of completed tasks.
 */
data class Progress(
    val totalTasks: Int = 0,
    val completedTasks: Int = 0,
) {
    /**
     * Returns `true` if all tasks in the checklist are completed.
     */
    fun allTasksCompleted() = totalTasks == completedTasks
}

/**
 * Calculates the completion progress of a set of [ChecklistItem].
 */
fun List<ChecklistItem>.getTaskProgress(): Progress {
    val tasks = flatMap { item ->
        when (item) {
            is ChecklistItem.Task -> listOf(item)
            is ChecklistItem.Group -> item.tasks
        }
    }

    return Progress(
        totalTasks = tasks.size,
        completedTasks = tasks.count { it.isCompleted },
    )
}

/**
 * Returns true if all checklist items are groups.
 */
fun List<ChecklistItem>.checklistItemsAreGroups() = all { it is ChecklistItem.Group }

/**
 * Gets the checklist items for the given [SetupChecklistType].
 */
fun getSetupChecklistCollection(
    settings: Settings,
    collection: SetupChecklistType,
    tabStripEnabled: Boolean = false,
) = when (collection) {
    SetupChecklistType.COLLECTION_1 -> createTasksCollection(settings)
    SetupChecklistType.COLLECTION_2 -> createGroupsCollection(settings, tabStripEnabled)
}

private fun createTasksCollection(settings: Settings) = with(settings) {
    listOf(
        defaultBrowserTask(isDefaultBrowserBlocking()),
        exploreExtensionTask(hasCompletedSetupStepExtensions),
        signInTask(signedInFxaAccount),
    )
}

private fun defaultBrowserTask(isCompleted: Boolean) = ChecklistItem.Task(
    type = ChecklistItem.Task.Type.SET_AS_DEFAULT,
    title = R.string.setup_checklist_task_default_browser,
    icon = R.drawable.mozac_ic_globe_24,
    isCompleted = isCompleted,
)

private fun exploreExtensionTask(isCompleted: Boolean) = ChecklistItem.Task(
    type = ChecklistItem.Task.Type.EXPLORE_EXTENSION,
    title = R.string.setup_checklist_task_explore_extensions,
    icon = R.drawable.ic_addons_extensions,
    isCompleted = isCompleted,
)

private fun signInTask(isCompleted: Boolean) = ChecklistItem.Task(
    type = ChecklistItem.Task.Type.SIGN_IN,
    title = R.string.setup_checklist_task_account_sync,
    icon = R.drawable.ic_fx_accounts_avatar,
    isCompleted = isCompleted,
)

private fun createGroupsCollection(settings: Settings, tabStripEnabled: Boolean) =
    listOf(
        essentialsGroup(settings),
        customizeGroup(settings, tabStripEnabled),
        helpfulToolsGroup(settings),
    )

private fun essentialsGroup(settings: Settings) = ChecklistItem.Group(
    title = R.string.setup_checklist_group_essentials,
    tasks = with(settings) {
        listOf(
            defaultBrowserTask(isDefaultBrowserBlocking()),
            signInTask(signedInFxaAccount),
        )
    },
)

private fun customizeGroup(settings: Settings, tabStripEnabled: Boolean): ChecklistItem.Group =
    with(settings) {
        val tasks = mutableListOf(
            ChecklistItem.Task(
                type = ChecklistItem.Task.Type.SELECT_THEME,
                title = R.string.setup_checklist_task_theme_selection,
                icon = R.drawable.mozac_ic_themes_24,
                isCompleted = hasCompletedSetupStepTheme,
            ),
        )

        // Toolbar placement cannot be changed when the tab strip is enabled.
        if (!tabStripEnabled) {
            tasks.add(
                ChecklistItem.Task(
                    type = ChecklistItem.Task.Type.CHANGE_TOOLBAR_PLACEMENT,
                    title = R.string.setup_checklist_task_toolbar_selection,
                    icon = R.drawable.mozac_ic_tool_24,
                    isCompleted = hasCompletedSetupStepToolbar,
                ),
            )
        }

        ChecklistItem.Group(
            title = R.string.setup_checklist_group_customize,
            tasks = tasks,
        )
    }

private fun helpfulToolsGroup(settings: Settings) = ChecklistItem.Group(
    title = R.string.setup_checklist_group_helpful_tools,
    tasks = with(settings) {
        listOf(
            ChecklistItem.Task(
                type = ChecklistItem.Task.Type.INSTALL_SEARCH_WIDGET,
                title = R.string.setup_checklist_task_search_widget,
                icon = R.drawable.ic_search,
                isCompleted = searchWidgetInstalled,
            ),
            exploreExtensionTask(hasCompletedSetupStepExtensions),
        )
    },
)
