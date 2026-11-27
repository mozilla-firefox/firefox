/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.CurrentAuthProvider
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.isEligible

/**
 * Current continuous onboarding facts exposed under Nimbus's `triggerContext`.
 *
 * @param enabled Whether continuous onboarding is enabled.
 * @param defaultBrowserRoleRequestable Whether the default browser role can be requested.
 * @param notificationPermissionRequestable Whether the notification permission can be requested.
 * @param ipProtectionPromptAllowed Whether the IP Protection prompt is allowed to appear.
 */
internal fun IPProtectionState.toContinuousOnboardingMessageContext(
    enabled: Boolean,
    defaultBrowserRoleRequestable: Boolean,
    notificationPermissionRequestable: Boolean,
    ipProtectionPromptAllowed: Boolean,
): Map<String, Any?> =
    mapOf(
        "continuous_onboarding" to
            mapOf(
                "enabled" to enabled,
                "default_browser_role_requestable" to defaultBrowserRoleRequestable,
                "notification_permission_requestable" to notificationPermissionRequestable,
                "vpn_eligible" to isEligible,
                "vpn_ineligible" to isIneligible(),
                "vpn_prompt_allowed" to ipProtectionPromptAllowed,
            )
    )

/** Mirrors `IPProtectionOnboardingPrompt`: the user can't use IP Protection, and won't be able to. */
@OptIn(ExperimentalAndroidComponentsApi::class)
private fun IPProtectionState.isIneligible() =
    eligibilityStatus == EligibilityStatus.Ineligible ||
        eligibilityStatus == EligibilityStatus.UnsupportedRegion ||
        (serviceStatus == ServiceState.Unavailable && currentAuthProvider != CurrentAuthProvider.Unknown)
