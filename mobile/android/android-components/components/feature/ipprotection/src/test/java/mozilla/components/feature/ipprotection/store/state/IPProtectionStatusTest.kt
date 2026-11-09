/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.store.state

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.storage.buildIPProtectionState
import org.junit.Assert.assertEquals
import org.junit.Test

class IPProtectionStatusTest {

    @Test
    fun `GIVEN the user is not eligible or the service is unavailable THEN the status is Unavailable`() {
        val cases =
            listOf(
                buildIPProtectionState(eligibilityStatus = EligibilityStatus.Ineligible),
                buildIPProtectionState(
                    eligibilityStatus = EligibilityStatus.UnsupportedRegion,
                    serviceStatus = ServiceState.Ready,
                    proxyStatus = Authorized.Active,
                ),
                buildIPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceStatus = ServiceState.Unavailable,
                ),
            )

        cases.forEach { assertEquals("$it", IPProtectionStatus.Unavailable, it.ipProtectionStatus) }
    }

    @Test
    fun `GIVEN eligibility is unknown, the service is uninitialized, or the proxy is uninitialized THEN the status is Uninitialized`() {
        val cases =
            listOf(
                buildIPProtectionState(
                    eligibilityStatus = EligibilityStatus.Unknown,
                    serviceStatus = ServiceState.Ready,
                    proxyStatus = Authorized.Idle,
                ),
                buildIPProtectionState(eligibilityStatus = EligibilityStatus.Eligible),
                buildIPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceStatus = ServiceState.Ready,
                    proxyStatus = Uninitialized,
                ),
            )

        cases.forEach { assertEquals("$it", IPProtectionStatus.Uninitialized, it.ipProtectionStatus) }
    }

    @Test
    fun `GIVEN the service is unauthenticated THEN an account awaiting enrollment, authenticated or enrolled shows AwaitingService AND every other account status shows AuthRequired`() {
        val cases =
            listOf(
                AccountStatus.Uninitialized to IPProtectionStatus.AuthRequired,
                AccountStatus.WarmingUp to IPProtectionStatus.AuthRequired,
                AccountStatus.NoAccount to IPProtectionStatus.AuthRequired,
                AccountStatus.NeedsAuthentication to IPProtectionStatus.AuthRequired,
                AccountStatus.RequestingAuthentication to IPProtectionStatus.AuthRequired,
                AccountStatus.NeedsAuthorization to IPProtectionStatus.AuthRequired,
                AccountStatus.RequestingAuthorization to IPProtectionStatus.AuthRequired,
                AccountStatus.AwaitingAuthentication to IPProtectionStatus.AuthRequired,
                AccountStatus.AwaitingAuthorization to IPProtectionStatus.AuthRequired,
                AccountStatus.AwaitingEnrollment to IPProtectionStatus.AwaitingService,
                AccountStatus.AuthFailed to IPProtectionStatus.AuthRequired,
                AccountStatus.Authenticated to IPProtectionStatus.AwaitingService,
                AccountStatus.EnrolledAndEntitled to IPProtectionStatus.AwaitingService,
                AccountStatus.TryAgain to IPProtectionStatus.AuthRequired,
            )

        cases.forEach { (accountStatus, expected) ->
            val state =
                buildIPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceStatus = ServiceState.Unauthenticated,
                    accountStatus = accountStatus,
                )

            assertEquals("$accountStatus", expected, state.ipProtectionStatus)
        }
    }

    @Test
    fun `GIVEN the service is ready THEN an idle, activating, active, paused or failed proxy shows Disabled, Activating, Enabled, DataLimitReached or ConnectionError`() {
        val cases =
            listOf(
                Authorized.Idle to IPProtectionStatus.Disabled,
                Authorized.Activating to IPProtectionStatus.Activating,
                Authorized.Active to IPProtectionStatus.Enabled,
                Authorized.DataLimitReached to IPProtectionStatus.DataLimitReached,
                Authorized.ConnectionError to IPProtectionStatus.ConnectionError,
            )

        cases.forEach { (proxyStatus, expected) ->
            val state =
                buildIPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceStatus = ServiceState.Ready,
                    proxyStatus = proxyStatus,
                )

            assertEquals("$proxyStatus", expected, state.ipProtectionStatus)
        }
    }

    @Test
    fun `GIVEN a running proxy request THEN only an activation from an idle proxy shows as Activating`() {
        val cases =
            listOf(
                Triple(Authorized.Idle, ProxyRequest.Activate(null), IPProtectionStatus.Activating),
                Triple(Authorized.Idle, ProxyRequest.Deactivate, IPProtectionStatus.Disabled),
                Triple(Authorized.Active, ProxyRequest.SwitchLocation("CA"), IPProtectionStatus.Enabled),
            )

        cases.forEach { (proxyStatus, proxyRequest, expected) ->
            val state =
                buildIPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceStatus = ServiceState.Ready,
                    proxyStatus = proxyStatus,
                    proxyRequest = proxyRequest,
                )

            assertEquals("$proxyStatus with $proxyRequest", expected, state.ipProtectionStatus)
        }
    }
}
