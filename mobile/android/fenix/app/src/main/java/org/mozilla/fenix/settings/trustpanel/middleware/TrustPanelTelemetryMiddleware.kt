/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.middleware

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.TrackingProtection
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelState
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelStore

/**
 * A [Middleware] for recording telemetry based on [TrustPanelAction]s that are dispatched to the
 * [TrustPanelStore].
 */
class TrustPanelTelemetryMiddleware : Middleware<TrustPanelState, TrustPanelAction> {

    override fun invoke(
        context: MiddlewareContext<TrustPanelState, TrustPanelAction>,
        next: (TrustPanelAction) -> Unit,
        action: TrustPanelAction,
    ) {
        val currentState = context.state

        next(action)

        when (action) {
            TrustPanelAction.ToggleTrackingProtection -> if (currentState.isTrackingProtectionEnabled) {
                TrackingProtection.exceptionAdded.record(NoExtras())
            }

            is TrustPanelAction.ClearSiteData,
            is TrustPanelAction.RequestClearSiteDataDialog,
            is TrustPanelAction.UpdateBaseDomain,
            is TrustPanelAction.UpdateDetailedTrackerCategory,
            is TrustPanelAction.UpdateNumberOfTrackersBlocked,
            is TrustPanelAction.UpdateTrackersBlocked,
            is TrustPanelAction.TogglePermission,
            is TrustPanelAction.UpdateAutoplayValue,
            is TrustPanelAction.UpdateSitePermissions,
            is TrustPanelAction.WebsitePermissionAction,
            TrustPanelAction.Navigate.PrivacySecuritySettings,
            is TrustPanelAction.Navigate.ManagePhoneFeature,
            -> Unit
        }
    }
}
