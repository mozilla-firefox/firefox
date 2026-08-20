/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.middleware

import androidx.annotation.VisibleForTesting
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.TrackingProtection
import org.mozilla.fenix.GleanMetrics.TrustPanel
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelState
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelStore

@VisibleForTesting internal const val TRUST_PANEL_TELEMETRY_SOURCE = "trust_panel"

/** A [Middleware] for recording telemetry based on [TrustPanelAction]s that are dispatched to the [TrustPanelStore]. */
class TrustPanelTelemetryMiddleware : Middleware<TrustPanelState, TrustPanelAction> {

    override fun invoke(
        store: Store<TrustPanelState, TrustPanelAction>,
        next: (TrustPanelAction) -> Unit,
        action: TrustPanelAction,
    ) {
        val currentState = store.state

        next(action)

        when (action) {
            TrustPanelAction.ToggleTrackingProtection ->
                if (currentState.isTrackingProtectionEnabled) {
                    TrackingProtection.exceptionAdded.record(NoExtras())
                }

            is TrustPanelAction.Navigate.SecurityCertificate -> {
                TrustPanel.securityCertificate.record(NoExtras())
            }

            is TrustPanelAction.Navigate.QWAC -> {
                TrustPanel.qwac.record(NoExtras())
            }

            is TrustPanelAction.Navigate.TrackersProtectionDashboard -> {
                TrackingProtection.privacyReportTapped.record(
                    TrackingProtection.PrivacyReportTappedExtra(TRUST_PANEL_TELEMETRY_SOURCE)
                )
            }

            is TrustPanelAction.Navigate.PrivacySecuritySettings -> {
                TrackingProtection.panelSettings.record(NoExtras())
            }

            is TrustPanelAction.UpdateDetailedTrackerCategory -> {
                TrackingProtection.etpTrackerList.record(NoExtras())
            }

            is TrustPanelAction.ClearSiteData,
            is TrustPanelAction.RequestClearSiteDataDialog,
            is TrustPanelAction.UpdateBaseDomain,
            is TrustPanelAction.UpdateNumberOfTrackersBlocked,
            is TrustPanelAction.UpdateTrackersBlocked,
            is TrustPanelAction.TogglePermission,
            is TrustPanelAction.UpdateAutoplayValue,
            is TrustPanelAction.UpdateSitePermissions,
            is TrustPanelAction.UpdateIPProtectionMenuState,
            is TrustPanelAction.WebsitePermissionAction,
            is TrustPanelAction.RequestQWAC,
            is TrustPanelAction.UpdateQWAC,
            is TrustPanelAction.Navigate.ManagePhoneFeature,
            is TrustPanelAction.Navigate.IPProtectionSettings -> Unit
        }
    }
}
