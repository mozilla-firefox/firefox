/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabgroups.flow

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.mapNotNull
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState

/**
 * A [Store] that holds the [TabGroupFlowState] for the tabs tray and reduces [TabGroupAction]s dispatched to the store.
 */
class TabGroupFlowStore(
    initialState: TabGroupFlowState,
    middlewares: List<Middleware<TabGroupFlowState, TabGroupAction>>,
) :
    Store<TabGroupFlowState, TabGroupAction>(
        initialState,
        TabGroupFlowReducer::reduce,
        middlewares,
    ) {
    val tabGroupFormStateFlow: Flow<TabGroupFormState> = stateFlow.mapNotNull { it.formState }

    /** Observe [TabGroupFlowStore] to listen to changes to the provided [TabsTrayItem.TabGroup]. */
    fun observeTabGroup(tabGroup: TabsTrayItem.TabGroup): Flow<TabsTrayItem.TabGroup> =
        stateFlow
            .map { it.groups }
            .distinctUntilChanged()
            .map { it.find { group -> group.id == tabGroup.id } ?: tabGroup }
            .distinctUntilChanged()
}
