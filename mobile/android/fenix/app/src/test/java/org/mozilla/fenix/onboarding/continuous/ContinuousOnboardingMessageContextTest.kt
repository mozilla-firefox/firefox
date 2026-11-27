/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.CurrentAuthProvider
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import org.junit.Assert.assertEquals
import org.junit.Test

@OptIn(ExperimentalAndroidComponentsApi::class)
class ContinuousOnboardingMessageContextTest {
    @Test
    fun `WHEN creating the context THEN the facts are nested under continuous_onboarding`() {
        val context =
            IPProtectionState(eligibilityStatus = EligibilityStatus.Eligible)
                .toContinuousOnboardingMessageContext(
                    enabled = true,
                    defaultBrowserRoleRequestable = false,
                    notificationPermissionRequestable = true,
                    ipProtectionPromptAllowed = false,
                )

        assertEquals(
            mapOf(
                "continuous_onboarding" to
                    mapOf(
                        "enabled" to true,
                        "default_browser_role_requestable" to false,
                        "notification_permission_requestable" to true,
                        "vpn_eligible" to true,
                        "vpn_ineligible" to false,
                        "vpn_prompt_allowed" to false,
                    )
            ),
            context,
        )
    }

    @Test
    fun `GIVEN eligibility is unknown THEN the user is neither eligible nor ineligible`() {
        assertEquals(false to false, vpnEligibility(IPProtectionState()))
    }

    @Test
    fun `GIVEN the user is ineligible or in an unsupported region THEN the user is ineligible`() {
        listOf(EligibilityStatus.Ineligible, EligibilityStatus.UnsupportedRegion).forEach {
            assertEquals(false to true, vpnEligibility(IPProtectionState(eligibilityStatus = it)))
        }
    }

    @Test
    fun `GIVEN the service is unavailable THEN the user is ineligible once the auth provider is known`() {
        val unavailable =
            IPProtectionState(eligibilityStatus = EligibilityStatus.Eligible, serviceStatus = ServiceState.Unavailable)

        assertEquals(true to false, vpnEligibility(unavailable))
        assertEquals(true to true, vpnEligibility(unavailable.copy(currentAuthProvider = CurrentAuthProvider.Fxa)))
    }

    private fun vpnEligibility(state: IPProtectionState): Pair<Any?, Any?> {
        val context =
            state.toContinuousOnboardingMessageContext(
                enabled = true,
                defaultBrowserRoleRequestable = true,
                notificationPermissionRequestable = true,
                ipProtectionPromptAllowed = true,
            )["continuous_onboarding"] as Map<*, *>
        return context["vpn_eligible"] to context["vpn_ineligible"]
    }
}
