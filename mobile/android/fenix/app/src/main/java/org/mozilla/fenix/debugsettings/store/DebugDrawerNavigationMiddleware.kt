/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.store

import androidx.navigation.NavHostController
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.debugsettings.navigation.DebugDrawerRoute
import org.mozilla.fenix.debugsettings.ui.DEBUG_DRAWER_HOME_ROUTE

/**
 * Middleware that handles navigation events for the Debug Drawer feature.
 *
 * @param navController [NavHostController] used to execute any navigation actions on the UI.
 * @param scope [CoroutineScope] used to make calls to the main thread.
 */
class DebugDrawerNavigationMiddleware(
    private val navController: NavHostController,
    private val scope: CoroutineScope,
) : Middleware<DebugDrawerState, DebugDrawerAction> {

    override fun invoke(
        store: Store<DebugDrawerState, DebugDrawerAction>,
        next: (DebugDrawerAction) -> Unit,
        action: DebugDrawerAction,
    ) {
        next(action)
        scope.launch {
            when (action) {
                is DebugDrawerAction.NavigateTo.Home -> navController.popBackStack(
                    route = DEBUG_DRAWER_HOME_ROUTE,
                    inclusive = false,
                )
                is DebugDrawerAction.NavigateTo.TabTools ->
                    navController.navigate(route = DebugDrawerRoute.TabTools.route)
                is DebugDrawerAction.NavigateTo.Logins ->
                    navController.navigate(route = DebugDrawerRoute.Logins.route)
                is DebugDrawerAction.NavigateTo.Addresses ->
                    navController.navigate(route = DebugDrawerRoute.Addresses.route)
                is DebugDrawerAction.NavigateTo.CreditCards ->
                    navController.navigate(route = DebugDrawerRoute.CreditCards.route)
                is DebugDrawerAction.NavigateTo.Autofill ->
                    navController.navigate(route = DebugDrawerRoute.Autofill.route)
                is DebugDrawerAction.NavigateTo.CfrTools ->
                    navController.navigate(route = DebugDrawerRoute.CfrTools.route)
                is DebugDrawerAction.NavigateTo.GleanDebugTools ->
                    navController.navigate(route = DebugDrawerRoute.GleanDebugTools.route)
                is DebugDrawerAction.NavigateTo.RegionDebugTools ->
                    navController.navigate(route = DebugDrawerRoute.RegionDebugTools.route)
                is DebugDrawerAction.NavigateTo.AddonsDebugTools ->
                    navController.navigate(route = DebugDrawerRoute.AddonsDebugTools.route)
                is DebugDrawerAction.NavigateTo.CrashDebugTools ->
                    navController.navigate(route = DebugDrawerRoute.CrashDebugTools.route)
                is DebugDrawerAction.NavigateTo.IntegrityDebugTools ->
                    navController.navigate(route = DebugDrawerRoute.IntegrityTools.route)
                is DebugDrawerAction.OnBackPressed -> navController.popBackStack()
                is DebugDrawerAction.DrawerOpened, DebugDrawerAction.DrawerClosed -> Unit // no-op
            }
        }
    }
}
