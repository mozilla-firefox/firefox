/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.setup.store

import io.mockk.every
import io.mockk.mockk
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flowOf
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.lib.state.Store
import mozilla.components.support.test.rule.MainCoroutineRule
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.junit.MockitoJUnitRunner
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.setup.checklist.ChecklistItem

@RunWith(MockitoJUnitRunner::class)
class SetupChecklistPreferencesMiddlewareTest {

    @get:Rule
    val mainCoroutineTestRule = MainCoroutineRule()

    private val context: MiddlewareContext<AppState, AppAction> = mockk(relaxed = true)

    // tests for invoke action
    @Test
    fun `GIVEN init action WHEN invoked the repository is initialised`() {
        val repository = FakeRepository()
        val middleware = SetupChecklistPreferencesMiddleware(repository)

        val store: Store<AppState, AppAction> = mockk(relaxed = true)
        every { context.store } returns store
        middleware.invoke(context, {}, AppAction.SetupChecklistAction.Init)

        assertTrue(repository.initInvoked)
    }

    @Test
    fun `invoke sets the preference only when a relevant task is clicked`() {
        ChecklistItem.Task.Type.entries.forEach {
            val repository = FakeRepository()
            val middleware = SetupChecklistPreferencesMiddleware(repository)
            val task = buildTask(type = it)
            middleware.invoke(
                context,
                {},
                AppAction.SetupChecklistAction.ChecklistItemClicked(task),
            )

            when (it) {
                ChecklistItem.Task.Type.SELECT_THEME,
                ChecklistItem.Task.Type.CHANGE_TOOLBAR_PLACEMENT,
                ChecklistItem.Task.Type.EXPLORE_EXTENSION,
                -> assertTrue(repository.setPreferenceInvoked)

                ChecklistItem.Task.Type.SET_AS_DEFAULT,
                ChecklistItem.Task.Type.SIGN_IN,
                ChecklistItem.Task.Type.INSTALL_SEARCH_WIDGET,
                -> assertFalse(repository.setPreferenceInvoked)
            }
        }
    }

    @Test
    fun `invoke does not set the preference when the item is a group`() {
        val repository = FakeRepository()
        val middleware = SetupChecklistPreferencesMiddleware(repository)
        middleware.invoke(
            context,
            {},
            AppAction.SetupChecklistAction.ChecklistItemClicked(buildGroup()),
        )
        assertFalse(repository.setPreferenceInvoked)
    }

    @Test
    fun `GIVEN SetToDefault preference WHEN mapping to store action THEN returns SET_AS_DEFAULT task type`() {
        val preferenceUpdate = SetupChecklistRepository.SetupChecklistPreferenceUpdate(
            preference = SetupChecklistPreference.SetToDefault,
            value = true,
        )
        val result = mapRepoUpdateToStoreAction(preferenceUpdate)

        assertEquals(
            AppAction.SetupChecklistAction.TaskPreferenceUpdated(
                taskType = ChecklistItem.Task.Type.SET_AS_DEFAULT,
                prefValue = true,
            ),
            result,
        )
    }

    // tests for mapRepoUpdateToStoreAction
    @Test
    fun `GIVEN SignIn preference WHEN mapping to store action THEN returns SIGN_IN task type`() {
        val preferenceUpdate = SetupChecklistRepository.SetupChecklistPreferenceUpdate(
            SetupChecklistPreference.SignIn,
            true,
        )
        val result = mapRepoUpdateToStoreAction(preferenceUpdate)

        assertEquals(
            AppAction.SetupChecklistAction.TaskPreferenceUpdated(
                ChecklistItem.Task.Type.SIGN_IN,
                true,
            ),
            result,
        )
    }

    @Test
    fun `GIVEN ThemeComplete preference WHEN mapping to store action THEN returns SELECT_THEME task type`() {
        val preferenceUpdate = SetupChecklistRepository.SetupChecklistPreferenceUpdate(
            SetupChecklistPreference.ThemeComplete,
            true,
        )
        val result = mapRepoUpdateToStoreAction(preferenceUpdate)

        assertEquals(
            AppAction.SetupChecklistAction.TaskPreferenceUpdated(
                ChecklistItem.Task.Type.SELECT_THEME,
                true,
            ),
            result,
        )
    }

    @Test
    fun `GIVEN ToolbarComplete preference WHEN mapping to store action THEN returns CHANGE_TOOLBAR_PLACEMENT task type`() {
        val preferenceUpdate = SetupChecklistRepository.SetupChecklistPreferenceUpdate(
            SetupChecklistPreference.ToolbarComplete,
            true,
        )
        val result = mapRepoUpdateToStoreAction(preferenceUpdate)

        assertEquals(
            AppAction.SetupChecklistAction.TaskPreferenceUpdated(
                ChecklistItem.Task.Type.CHANGE_TOOLBAR_PLACEMENT,
                true,
            ),
            result,
        )
    }

    @Test
    fun `GIVEN ExtensionsComplete preference WHEN mapping to store action THEN returns EXPLORE_EXTENSION task type`() {
        val preferenceUpdate = SetupChecklistRepository.SetupChecklistPreferenceUpdate(
            SetupChecklistPreference.ExtensionsComplete,
            true,
        )
        val result = mapRepoUpdateToStoreAction(preferenceUpdate)

        assertEquals(
            AppAction.SetupChecklistAction.TaskPreferenceUpdated(
                ChecklistItem.Task.Type.EXPLORE_EXTENSION,
                true,
            ),
            result,
        )
    }

    @Test
    fun `GIVEN InstallSearchWidget preference WHEN mapping to store action THEN returns INSTALL_SEARCH_WIDGET task type`() {
        val preferenceUpdate = SetupChecklistRepository.SetupChecklistPreferenceUpdate(
            SetupChecklistPreference.InstallSearchWidget,
            true,
        )
        val result = mapRepoUpdateToStoreAction(preferenceUpdate)

        assertEquals(
            AppAction.SetupChecklistAction.TaskPreferenceUpdated(
                ChecklistItem.Task.Type.INSTALL_SEARCH_WIDGET,
                true,
            ),
            result,
        )
    }

    @Test
    fun `GIVEN ShowSetupChecklist preference WHEN mapping to store action THEN returns the close action`() {
        val preferenceUpdate = SetupChecklistRepository.SetupChecklistPreferenceUpdate(
            SetupChecklistPreference.ShowSetupChecklist,
            true,
        )
        val result = mapRepoUpdateToStoreAction(preferenceUpdate)

        assertEquals(
            AppAction.SetupChecklistAction.Closed,
            result,
        )
    }

    private fun buildGroup() = ChecklistItem.Group(
        title = 0,
        tasks = emptyList(),
        isExpanded = false,
    )

    private fun buildTask(type: ChecklistItem.Task.Type) = ChecklistItem.Task(
        type = type,
        title = 0,
        icon = 0,
        isCompleted = false,
    )
}

private class FakeRepository : SetupChecklistRepository {
    var initInvoked = false
    var setPreferenceInvoked = false

    override fun init() {
        initInvoked = true
    }

    override fun setPreference(type: SetupChecklistPreference, value: Boolean) {
        // Ensure the passed value is true.
        if (value) {
            setPreferenceInvoked = true
        }
    }

    override val setupChecklistPreferenceUpdates: Flow<SetupChecklistRepository.SetupChecklistPreferenceUpdate>
        get() = preferenceUpdates
}

private val preferenceUpdates = flowOf(
    SetupChecklistRepository.SetupChecklistPreferenceUpdate(
        SetupChecklistPreference.SetToDefault,
        true,
    ),
    SetupChecklistRepository.SetupChecklistPreferenceUpdate(
        SetupChecklistPreference.ThemeComplete,
        false,
    ),
)
