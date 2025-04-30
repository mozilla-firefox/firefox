/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.setup.store

import androidx.annotation.StringRes
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.setup.checklist.ChecklistItem
import org.mozilla.fenix.components.appstate.setup.checklist.getSetupChecklistCollection
import org.mozilla.fenix.components.appstate.setup.checklist.getTaskProgress
import org.mozilla.fenix.nimbus.SetupChecklistType
import org.mozilla.fenix.utils.Settings

class ChecklistItemTest {

    @Test
    fun `GIVEN an empty list WHEN getTaskProgress is invoked THEN total and completed values should be zero`() {
        val checklist = emptyList<ChecklistItem>()

        val progress = checklist.getTaskProgress()

        assertEquals(0, progress.totalTasks)
        assertEquals(0, progress.completedTasks)
    }

    @Test
    fun `GIVEN a single completed task WHEN getTaskProgress is invoked THEN total and completed values should be equal to 1`() {
        val checklist = listOf(buildTask(isCompleted = true))

        val progress = checklist.getTaskProgress()

        assertEquals(1, progress.totalTasks)
        assertEquals(1, progress.completedTasks)
    }

    @Test
    fun `GIVEN a checklist with multiple tasks and a single completed task WHEN getTaskProgress is invoked THEN total and completed values should be equal to 1 still`() {
        val checklist = listOf(
            buildTask(isCompleted = true),
            buildTask(isCompleted = false),
            buildTask(isCompleted = false),
        )

        val progress = checklist.getTaskProgress()

        assertEquals(3, progress.totalTasks)
        assertEquals(1, progress.completedTasks)
    }

    @Test
    fun `GIVEN a checklist with a group containing tasks WHEN getTaskProgress is invoked THEN tasks within the group are calculated correctly`() {
        val checklist = listOf(
            ChecklistItem.Group(
                title = R.string.setup_checklist_group_essentials,
                tasks = listOf(
                    buildTask(isCompleted = true),
                    buildTask(isCompleted = false),
                ),
                isExpanded = false,
            ),
        )

        val progress = checklist.getTaskProgress()

        assertEquals(2, progress.totalTasks)
        assertEquals(1, progress.completedTasks)
    }

    @Test
    fun `GIVEN a checklist with multiple groups WHEN getTaskProgress is called THEN tasks within the groups are calculated correctly`() {
        val checklist = listOf(
            ChecklistItem.Group(
                title = R.string.setup_checklist_group_essentials,
                tasks = listOf(
                    buildTask(isCompleted = true),
                    buildTask(isCompleted = false),
                ),
                isExpanded = false,
            ),
            ChecklistItem.Group(
                title = R.string.setup_checklist_group_helpful_tools,
                tasks = listOf(
                    buildTask(isCompleted = true),
                    buildTask(isCompleted = false),
                    buildTask(isCompleted = false),
                ),
                isExpanded = false,
            ),
        )

        val progress = checklist.getTaskProgress()

        assertEquals(5, progress.totalTasks)
        assertEquals(2, progress.completedTasks)
    }

    @Test
    fun `GIVEN a checklist with both standalone tasks and a group WHEN getTaskProgress is called THEN counts all tasks correctly`() {
        val checklist = listOf(
            buildTask(isCompleted = true),
            ChecklistItem.Group(
                title = R.string.setup_checklist_group_essentials,
                tasks = listOf(
                    buildTask(isCompleted = false),
                    buildTask(isCompleted = true),
                ),
                isExpanded = false,
            ),
        )

        val progress = checklist.getTaskProgress()

        assertEquals(3, progress.totalTasks)
        assertEquals(2, progress.completedTasks)
    }

    @Test
    fun `WHEN collection 1 THEN getSetupChecklistCollection returns a list of the expected tasks`() {
        val settings = mock<Settings>()
        val isCompleted = false
        whenever(settings.isDefaultBrowserBlocking()).thenReturn(isCompleted)
        whenever(settings.hasCompletedSetupStepExtensions).thenReturn(isCompleted)
        whenever(settings.signedInFxaAccount).thenReturn(isCompleted)

        val result = getSetupChecklistCollection(
            settings = settings,
            collection = SetupChecklistType.COLLECTION_1,
        )

        val expected = listOf(
            ChecklistItem.Task(
                type = ChecklistItem.Task.Type.SET_AS_DEFAULT,
                title = R.string.setup_checklist_task_default_browser,
                icon = R.drawable.mozac_ic_globe_24,
                isCompleted = isCompleted,
            ),
            ChecklistItem.Task(
                type = ChecklistItem.Task.Type.EXPLORE_EXTENSION,
                title = R.string.setup_checklist_task_explore_extensions,
                icon = R.drawable.ic_addons_extensions,
                isCompleted = isCompleted,
            ),
            ChecklistItem.Task(
                type = ChecklistItem.Task.Type.SIGN_IN,
                title = R.string.setup_checklist_task_account_sync,
                icon = R.drawable.ic_fx_accounts_avatar,
                isCompleted = isCompleted,
            ),
        )

        assertEquals(expected, result)
    }

    @Test
    fun `WHEN collection 2 THEN getSetupChecklistCollection returns a list of the expected groups`() {
        val settings = mock<Settings>()
        val isCompleted = false
        whenever(settings.isDefaultBrowserBlocking()).thenReturn(isCompleted)
        whenever(settings.hasCompletedSetupStepExtensions).thenReturn(isCompleted)
        whenever(settings.hasCompletedSetupStepTheme).thenReturn(isCompleted)
        whenever(settings.hasCompletedSetupStepToolbar).thenReturn(isCompleted)
        whenever(settings.signedInFxaAccount).thenReturn(isCompleted)

        val result = getSetupChecklistCollection(
            settings = settings,
            collection = SetupChecklistType.COLLECTION_2,
        )

        val expected = listOf(
            ChecklistItem.Group(
                title = R.string.setup_checklist_group_essentials,
                tasks = listOf(
                    ChecklistItem.Task(
                        type = ChecklistItem.Task.Type.SET_AS_DEFAULT,
                        title = R.string.setup_checklist_task_default_browser,
                        icon = R.drawable.mozac_ic_globe_24,
                        isCompleted = isCompleted,
                    ),
                    ChecklistItem.Task(
                        type = ChecklistItem.Task.Type.SIGN_IN,
                        title = R.string.setup_checklist_task_account_sync,
                        icon = R.drawable.ic_fx_accounts_avatar,
                        isCompleted = isCompleted,
                    ),
                ),
            ),
            ChecklistItem.Group(
                title = R.string.setup_checklist_group_customize,
                tasks = listOf(
                    ChecklistItem.Task(
                        type = ChecklistItem.Task.Type.SELECT_THEME,
                        title = R.string.setup_checklist_task_theme_selection,
                        icon = R.drawable.mozac_ic_themes_24,
                        isCompleted = isCompleted,
                    ),
                    ChecklistItem.Task(
                        type = ChecklistItem.Task.Type.CHANGE_TOOLBAR_PLACEMENT,
                        title = R.string.setup_checklist_task_toolbar_selection,
                        icon = R.drawable.mozac_ic_tool_24,
                        isCompleted = isCompleted,
                    ),
                ),
            ),
            ChecklistItem.Group(
                title = R.string.setup_checklist_group_helpful_tools,
                tasks = listOf(
                    ChecklistItem.Task(
                        type = ChecklistItem.Task.Type.INSTALL_SEARCH_WIDGET,
                        title = R.string.setup_checklist_task_search_widget,
                        icon = R.drawable.ic_search,
                        isCompleted = isCompleted,
                    ),
                    ChecklistItem.Task(
                        type = ChecklistItem.Task.Type.EXPLORE_EXTENSION,
                        title = R.string.setup_checklist_task_explore_extensions,
                        icon = R.drawable.ic_addons_extensions,
                        isCompleted = isCompleted,
                    ),
                ),
            ),
        )

        assertEquals(expected, result)
    }

    @Test
    fun `WHEN collection 2 and tab strip enabled THEN getSetupChecklistCollection returns a list of the expected groups`() {
        val settings = mock<Settings>()
        val isCompleted = false
        whenever(settings.isDefaultBrowserBlocking()).thenReturn(isCompleted)
        whenever(settings.hasCompletedSetupStepExtensions).thenReturn(isCompleted)
        whenever(settings.hasCompletedSetupStepTheme).thenReturn(isCompleted)
        whenever(settings.hasCompletedSetupStepToolbar).thenReturn(isCompleted)
        whenever(settings.signedInFxaAccount).thenReturn(isCompleted)

        val result = getSetupChecklistCollection(
            settings = settings,
            collection = SetupChecklistType.COLLECTION_2,
            tabStripEnabled = true,
        )

        val expected = listOf(
            ChecklistItem.Group(
                title = R.string.setup_checklist_group_essentials,
                tasks = listOf(
                    ChecklistItem.Task(
                        type = ChecklistItem.Task.Type.SET_AS_DEFAULT,
                        title = R.string.setup_checklist_task_default_browser,
                        icon = R.drawable.mozac_ic_globe_24,
                        isCompleted = isCompleted,
                    ),
                    ChecklistItem.Task(
                        type = ChecklistItem.Task.Type.SIGN_IN,
                        title = R.string.setup_checklist_task_account_sync,
                        icon = R.drawable.ic_fx_accounts_avatar,
                        isCompleted = isCompleted,
                    ),
                ),
            ),
            ChecklistItem.Group(
                title = R.string.setup_checklist_group_customize,
                tasks = listOf(
                    ChecklistItem.Task(
                        type = ChecklistItem.Task.Type.SELECT_THEME,
                        title = R.string.setup_checklist_task_theme_selection,
                        icon = R.drawable.mozac_ic_themes_24,
                        isCompleted = isCompleted,
                    ),
                ),
            ),
            ChecklistItem.Group(
                title = R.string.setup_checklist_group_helpful_tools,
                tasks = listOf(
                    ChecklistItem.Task(
                        type = ChecklistItem.Task.Type.INSTALL_SEARCH_WIDGET,
                        title = R.string.setup_checklist_task_search_widget,
                        icon = R.drawable.ic_search,
                        isCompleted = isCompleted,
                    ),
                    ChecklistItem.Task(
                        type = ChecklistItem.Task.Type.EXPLORE_EXTENSION,
                        title = R.string.setup_checklist_task_explore_extensions,
                        icon = R.drawable.ic_addons_extensions,
                        isCompleted = isCompleted,
                    ),
                ),
            ),
        )

        assertEquals(expected, result)
    }

    private fun buildTask(
        @StringRes title: Int = R.string.setup_checklist_task_default_browser,
        isCompleted: Boolean,
    ) = ChecklistItem.Task(
        type = ChecklistItem.Task.Type.SIGN_IN,
        title = title,
        icon = 0,
        isCompleted = isCompleted,
    )
}
