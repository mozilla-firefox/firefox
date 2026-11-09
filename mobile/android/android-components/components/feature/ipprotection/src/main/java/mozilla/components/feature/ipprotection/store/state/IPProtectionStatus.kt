/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.store.state

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.ServiceState

/**
 * The status of IP Protection computed from [IPProtectionState]. Each value states its condition; where several hold,
 * the first declared value applies.
 */
enum class IPProtectionStatus {
    /** The user is ineligible or in an unsupported region, or the service is unavailable. */
    Unavailable,

    /** Eligibility is unknown, the service is uninitialized, or the service is ready and the proxy is uninitialized. */
    Uninitialized,

    /**
     * The service is unauthenticated and the account is awaiting enrollment, authenticated, or enrolled and entitled.
     */
    AwaitingService,

    /** The service is unauthenticated. */
    AuthRequired,

    /** The proxy is idle and no activation is running. */
    Disabled,

    /** The proxy is activating, or is idle while an activation is running. */
    Activating,

    /** The proxy is active. */
    Enabled,

    /** The proxy is paused because the data limit has been reached. */
    DataLimitReached,

    /** The proxy reported an error. */
    ConnectionError,
}

/** The [IPProtectionStatus] computed from the eligibility, service, account, proxy and running proxy request. */
val IPProtectionState.ipProtectionStatus: IPProtectionStatus
    get() =
        when {
            eligibilityStatus == EligibilityStatus.Ineligible ||
                eligibilityStatus == EligibilityStatus.UnsupportedRegion ||
                serviceStatus == ServiceState.Unavailable -> IPProtectionStatus.Unavailable

            eligibilityStatus == EligibilityStatus.Unknown || serviceStatus == ServiceState.Uninitialized ->
                IPProtectionStatus.Uninitialized

            serviceStatus == ServiceState.Unauthenticated -> accountState.status.toUnauthenticatedStatus()

            else -> proxyStatus.toReadyStatus(proxyRequest)
        }

private fun AccountStatus.toUnauthenticatedStatus() =
    when (this) {
        AccountStatus.AwaitingEnrollment,
        AccountStatus.Authenticated,
        AccountStatus.EnrolledAndEntitled -> IPProtectionStatus.AwaitingService

        AccountStatus.Uninitialized,
        AccountStatus.WarmingUp,
        AccountStatus.NoAccount,
        AccountStatus.NeedsAuthentication,
        AccountStatus.RequestingAuthentication,
        AccountStatus.NeedsAuthorization,
        AccountStatus.RequestingAuthorization,
        AccountStatus.AwaitingAuthentication,
        AccountStatus.AwaitingAuthorization,
        AccountStatus.AuthFailed,
        AccountStatus.TryAgain -> IPProtectionStatus.AuthRequired
    }

private fun ProxyStatus.toReadyStatus(proxyRequest: ProxyRequest?) =
    when (this) {
        Uninitialized -> IPProtectionStatus.Uninitialized
        Authorized.Idle ->
            if (proxyRequest is ProxyRequest.Activate) {
                IPProtectionStatus.Activating
            } else {
                IPProtectionStatus.Disabled
            }
        Authorized.Activating -> IPProtectionStatus.Activating
        Authorized.Active -> IPProtectionStatus.Enabled
        Authorized.DataLimitReached -> IPProtectionStatus.DataLimitReached
        Authorized.ConnectionError -> IPProtectionStatus.ConnectionError
    }
