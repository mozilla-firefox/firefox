/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.store

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/**
 * A [Store] that holds the [DebugDrawerState] for the Debug Drawer and reduces [DebugDrawerAction]s
 * dispatched to the store.
 */
class DebugDrawerStore(
    initialState: DebugDrawerState = DebugDrawerState(),
    middlewares: List<Middleware<DebugDrawerState, DebugDrawerAction>> = emptyList(),
) : Store<DebugDrawerState, DebugDrawerAction>(
    initialState,
    ::reduce,
    middlewares,
)

private fun reduce(state: DebugDrawerState, action: DebugDrawerAction): DebugDrawerState = when (action) {
    is DebugDrawerAction.DrawerOpened -> state.copy(drawerStatus = DrawerStatus.Open)
    is DebugDrawerAction.DrawerClosed -> state.copy(drawerStatus = DrawerStatus.Closed)
    DebugDrawerAction.ViewAppeared,
    is DebugDrawerAction.NavigateTo,
    DebugDrawerAction.OnBackPressed,
    -> state // handled by [DebugDrawerNavigationMiddleware]
}
