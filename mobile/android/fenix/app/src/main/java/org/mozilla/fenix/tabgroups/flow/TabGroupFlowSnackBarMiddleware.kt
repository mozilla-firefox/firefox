/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.flow

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction

/** Helper class that handles TabGroup flow SnackBar messages outside the TabsTray. */
class TabGroupFlowSnackBarMiddleware(private val appStore: AppStore, private val tabGroupStripEnabled: Boolean) :
    Middleware<TabGroupFlowState, TabGroupAction> {

    private val logger = Logger("TabGroupFlowSnackBarMiddleware")

    override fun invoke(
        store: Store<TabGroupFlowState, TabGroupAction>,
        next: (TabGroupAction) -> Unit,
        action: TabGroupAction,
    ) {
        // SnackBars are only displayed if the group strip is not enabled.
        if (!tabGroupStripEnabled) {
            handleTabGroupAction(store, action)
        }
        next(action)
    }

    private fun handleTabGroupAction(store: Store<TabGroupFlowState, TabGroupAction>, action: TabGroupAction) {
        when (action) {
            is TabGroupAction.NewGroupCreated -> {
                val formState = store.state.formState ?: return
                when (store.state.entryPoint) {
                    is TabGroupFlowEntryPoint.MoveTabToGroup -> {
                        appStore.dispatch(AppAction.TabMovedToGroup(groupTitle = formState.name, groupId = action.id))
                    }
                    is TabGroupFlowEntryPoint.AddTabToGroup -> {
                        appStore.dispatch(AppAction.TabAddedToGroup(groupTitle = formState.name, groupId = action.id))
                    }
                    else -> {
                        logger.info("Invalid state from a save action.")
                    }
                }
            }
            is TabGroupAction.TabAddedToExistingTabGroup -> {
                appStore.dispatch(AppAction.TabAddedToGroup(groupTitle = action.groupTitle, groupId = action.groupId))
            }
            is TabGroupAction.TabMovedToExistingTabGroup -> {
                appStore.dispatch(AppAction.TabMovedToGroup(groupTitle = action.groupTitle, groupId = action.groupId))
            }
            else -> {}
        }
    }
}
