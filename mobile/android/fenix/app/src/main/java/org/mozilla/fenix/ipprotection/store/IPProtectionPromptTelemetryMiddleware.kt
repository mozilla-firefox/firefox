/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.GleanMetrics.Vpn

internal class IPProtectionPromptTelemetryMiddleware :
    Middleware<IPProtectionPromptState, IPProtectionPromptAction> {
    override fun invoke(
        store: Store<IPProtectionPromptState, IPProtectionPromptAction>,
        next: (IPProtectionPromptAction) -> Unit,
        action: IPProtectionPromptAction,
    ) {
        next(action)

        when (action) {
            is IPProtectionPromptAction.OnImpression ->
                Vpn.onboardingShown.record(
                    Vpn.OnboardingShownExtra(entrypoint = action.surface.metricLabel),
                )

            is IPProtectionPromptAction.OnGetStartedClicked ->
                Vpn.getStartedTapped.record(
                    Vpn.GetStartedTappedExtra(entrypoint = action.surface.metricLabel),
                )

            is IPProtectionPromptAction.OnNotNowClicked ->
                Vpn.onboardingNotNowTapped.record(
                    Vpn.OnboardingNotNowTappedExtra(entrypoint = action.surface.metricLabel),
                )

            is IPProtectionPromptAction.OnPromptManuallyDismissed ->
                Vpn.onboardingDismissed.record(
                    Vpn.OnboardingDismissedExtra(entrypoint = action.surface.metricLabel),
                )

            is IPProtectionPromptAction.OnBrowseWithExtraProtectionClicked ->
                Vpn.onboardingBrowseWithProtectionTapped.record(
                    Vpn.OnboardingBrowseWithProtectionTappedExtra(
                        entrypoint = action.surface.metricLabel,
                    ),
                )

            // no-ops
            is IPProtectionPromptAction.OnPromptCreated,
            is IPProtectionPromptAction.OnPromptDismissed,
                -> {
            }
        }
    }
}
