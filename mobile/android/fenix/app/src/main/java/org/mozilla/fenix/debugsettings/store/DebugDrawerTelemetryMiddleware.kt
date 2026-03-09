/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.store

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.GleanMetrics.DebugDrawer

/**
 * Middleware for translating actions into useful telemetry.
 */
class DebugDrawerTelemetryMiddleware : Middleware<DebugDrawerState, DebugDrawerAction> {
    override fun invoke(
        store: Store<DebugDrawerState, DebugDrawerAction>,
        next: (DebugDrawerAction) -> Unit,
        action: DebugDrawerAction,
    ) {
        next(action)

        when (action) {
            DebugDrawerAction.ViewAppeared -> {
                DebugDrawer.debugDrawerEnabled.set(true)
            }
            DebugDrawerAction.DrawerClosed,
            DebugDrawerAction.DrawerOpened,
            DebugDrawerAction.NavigateTo.AddonsDebugTools,
            DebugDrawerAction.NavigateTo.Addresses,
            DebugDrawerAction.NavigateTo.Autofill,
            DebugDrawerAction.NavigateTo.CfrTools,
            DebugDrawerAction.NavigateTo.CrashDebugTools,
            DebugDrawerAction.NavigateTo.CreditCards,
            DebugDrawerAction.NavigateTo.GleanDebugTools,
            DebugDrawerAction.NavigateTo.Home,
            DebugDrawerAction.NavigateTo.IntegrityDebugTools,
            DebugDrawerAction.NavigateTo.LlmDebugTools,
            DebugDrawerAction.NavigateTo.Logins,
            DebugDrawerAction.NavigateTo.RegionDebugTools,
            DebugDrawerAction.NavigateTo.TabTools,
            DebugDrawerAction.OnBackPressed,
            -> Unit
        }
    }
}
