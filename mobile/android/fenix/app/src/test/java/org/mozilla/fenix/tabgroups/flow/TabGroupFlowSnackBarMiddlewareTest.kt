/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.flow

import kotlin.test.assertEquals
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState

class TabGroupFlowSnackBarMiddlewareTest {

    private val appStoreActions = CaptureActionsMiddleware<AppState, AppAction>()
    private val appStore = AppStore(middlewares = listOf(appStoreActions))

    @Test
    fun `GIVEN group strip disabled WHEN a tab is added to an existing group THEN the added to group snackbar is shown`() {
        val store = createStore()

        store.dispatch(
            TabGroupAction.TabAddedToExistingTabGroup(tabId = TAB_ID, groupId = GROUP_ID, groupTitle = GROUP_TITLE)
        )

        appStoreActions.assertLastAction(AppAction.TabAddedToGroup::class) {
            assertEquals(AppAction.TabAddedToGroup(groupTitle = GROUP_TITLE, groupId = GROUP_ID), it)
        }
    }

    @Test
    fun `GIVEN group strip disabled WHEN a tab is moved to an existing group THEN the moved to group snackbar is shown`() {
        val store = createStore(entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = TAB_ID))

        store.dispatch(
            TabGroupAction.TabMovedToExistingTabGroup(tabId = TAB_ID, groupId = GROUP_ID, groupTitle = GROUP_TITLE)
        )

        appStoreActions.assertLastAction(AppAction.TabMovedToGroup::class) {
            assertEquals(AppAction.TabMovedToGroup(groupTitle = GROUP_TITLE, groupId = GROUP_ID), it)
        }
    }

    @Test
    fun `GIVEN group strip enabled WHEN a tab is added to an existing group THEN no snackbar is shown`() {
        val store = createStore(tabGroupStripEnabled = true)

        store.dispatch(
            TabGroupAction.TabAddedToExistingTabGroup(tabId = TAB_ID, groupId = GROUP_ID, groupTitle = GROUP_TITLE)
        )

        appStoreActions.assertNoActionDispatched()
    }

    @Test
    fun `WHEN a tab is moved to an existing group THEN no snackbar is shown`() {
        val store =
            createStore(entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = TAB_ID), tabGroupStripEnabled = true)

        store.dispatch(
            TabGroupAction.TabMovedToExistingTabGroup(tabId = TAB_ID, groupId = GROUP_ID, groupTitle = GROUP_TITLE)
        )

        appStoreActions.assertNoActionDispatched()
    }

    @Test
    fun `GIVEN group strip disabled AND the add to group entry point WHEN a new group is created THEN the added to group snackbar shows the form name`() {
        val store = createStore(formState = TabGroupFormState(tabGroupId = null, name = NEW_GROUP_TITLE))

        store.dispatch(TabGroupAction.NewGroupCreated(NEW_GROUP_ID))

        appStoreActions.assertLastAction(AppAction.TabAddedToGroup::class) {
            assertEquals(AppAction.TabAddedToGroup(groupTitle = NEW_GROUP_TITLE, groupId = NEW_GROUP_ID), it)
        }
    }

    @Test
    fun `GIVEN group strip disabled AND the move to group entry point WHEN a new group is created THEN the moved to group snackbar shows the form name`() {
        val store =
            createStore(
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = TAB_ID),
                formState = TabGroupFormState(tabGroupId = null, name = NEW_GROUP_TITLE),
            )

        store.dispatch(TabGroupAction.NewGroupCreated(NEW_GROUP_ID))

        appStoreActions.assertLastAction(AppAction.TabMovedToGroup::class) {
            assertEquals(AppAction.TabMovedToGroup(groupTitle = NEW_GROUP_TITLE, groupId = NEW_GROUP_ID), it)
        }
    }

    @Test
    fun `GIVEN group strip disabled AND no form state WHEN a new group is created THEN no snackbar is shown`() {
        val store = createStore(formState = null)

        store.dispatch(TabGroupAction.NewGroupCreated(NEW_GROUP_ID))

        appStoreActions.assertNoActionDispatched()
    }

    @Test
    fun `GIVEN group strip disabled AND the edit group entry point WHEN a new group is created THEN no snackbar is shown`() {
        val store =
            createStore(
                entryPoint = TabGroupFlowEntryPoint.EditGroup(groupId = GROUP_ID),
                formState = TabGroupFormState(tabGroupId = null, name = NEW_GROUP_TITLE),
            )

        store.dispatch(TabGroupAction.NewGroupCreated(NEW_GROUP_ID))

        appStoreActions.assertNoActionDispatched()
    }

    @Test
    fun `GIVEN group strip disabled WHEN save is clicked THEN no snackbar is shown until the group is created`() {
        val store = createStore(formState = TabGroupFormState(tabGroupId = null, name = NEW_GROUP_TITLE))

        store.dispatch(TabGroupAction.SaveClicked)

        appStoreActions.assertNoActionDispatched()
    }

    private fun createStore(
        entryPoint: TabGroupFlowEntryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = TAB_ID),
        formState: TabGroupFormState? = null,
        tabGroupStripEnabled: Boolean = false,
    ) =
        TabGroupFlowStore(
            initialState = TabGroupFlowState(entryPoint = entryPoint, formState = formState),
            middlewares =
                listOf(
                    TabGroupFlowSnackBarMiddleware(appStore = appStore, tabGroupStripEnabled = tabGroupStripEnabled)
                ),
        )

    private companion object {
        const val TAB_ID = "tab-1"
        const val GROUP_ID = "group-1"
        const val GROUP_TITLE = "Work"
        const val NEW_GROUP_ID = "new-group"
        const val NEW_GROUP_TITLE = "Groceries"
    }
}
