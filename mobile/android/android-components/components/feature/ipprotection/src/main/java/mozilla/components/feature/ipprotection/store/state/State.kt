/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.store.state

import java.util.IllformedLocaleException
import java.util.Locale
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.lib.state.State

const val BYTES_PER_GB = 1024 * 1024 * 1024f

/**
 * State stored by the feature to drive UI and decisions.
 *
 * @property eligibilityStatus Whether the device is eligible of using the service.
 * @property proxyStatus The proxy state.
 * @property serviceStatus The state of the IPProtection service.
 * @property remainingDataBytes Remaining monthly data allowance in bytes, or -1 if unavailable.
 * @property maxDataBytes Maximum monthly data allowance in bytes, -1 if unavailable, or otherwise 0 if unlimited.
 * @property resetDate ISO 8601 string for when the monthly allowance resets, or null if unavailable.
 * @property accountState The state of the authenticator being used.
 * @property lastError The last error received from the IPProtection service.
 * @property proxyActivation Tracks recent activation or deactivation state changes. See [ProxyActivation].
 * @property activate To turn protection on or off.
 * @property locationState The location selection state.
 */
data class IPProtectionState(
    val eligibilityStatus: EligibilityStatus = EligibilityStatus.Unknown,
    val proxyStatus: ProxyStatus = Uninitialized,
    val serviceStatus: ServiceState = ServiceState.Uninitialized,
    val remainingDataBytes: Long = -1L,
    val maxDataBytes: Long = -1L,
    val resetDate: String? = null,
    val accountState: AccountState = AccountState(),
    val lastError: String? = null,
    val proxyActivation: ProxyActivation = ProxyActivation.Idle,
    val activate: Boolean? = null,
    val locationState: LocationState = LocationState(),
) : State

/** Convenience function for eligibility. */
val IPProtectionState.isEligible
    get() = eligibilityStatus == EligibilityStatus.Eligible

/**
 * If we have negative values, then we haven't received new usage data yet.
 *
 * N.B: If we get -1, and we try to render that then the values are obviously incorrect, but we let the consumer handle
 * this for now.
 */
val IPProtectionState.remainingDataGb: Float
    get() = remainingDataBytes / BYTES_PER_GB

/**
 * If we have negative values, then we haven't received new usage data yet. A value of zero means that we have unlimited
 * data.
 *
 * N.B: If we get -1, and we try to render that then the values are obviously incorrect, but we let the consumer handle
 * this for now.
 */
val IPProtectionState.maxDataGb: Float
    get() = maxDataBytes / BYTES_PER_GB

val IPProtectionState.usedDataGb: Float
    get() = maxDataGb - remainingDataGb

/**
 * The combined state of an FxA account pertinent to IP Protection.
 *
 * @property status The state of the authenticator being used.
 */
data class AccountState(val status: AccountStatus = AccountStatus.Uninitialized)

/**
 * Holds the location related data.
 *
 * @property selectedLocation The location to use when connecting to VPN.
 * @property locations The list of locations for user to choose from.
 */
data class LocationState(
    val selectedLocation: Location = Recommended,
    val locations: List<Location> = listOf(Recommended),
)

/**
 * A sealed interface to represent a selectable location.
 *
 * @property countryCode ISO 3166-1 alpha-2 code of the location, or `null`.
 */
sealed interface Location {
    val countryCode: String?
}

/** The "recommended" (default) location, letting the proxy pick the server automatically. */
object Recommended : Location {
    override val countryCode: String? = null
}

/**
 * A specific country from the IP protection proxy server list.
 *
 * @property countryCode ISO 3166-1 alpha-2 country code.
 * @property available Whether the country could be selected as the active proxy.
 * @property displayName The localized name for UI. If localization fails, returns raw [countryCode].
 */
data class Country(
    override val countryCode: String,
    val available: Boolean,
) : Location {
    val displayName: String
        get() =
            try {
                Locale.Builder().setRegion(countryCode).build().getDisplayCountry(Locale.getDefault())
            } catch (_: IllformedLocaleException) {
                countryCode
            }
}

/**
 * Represents the lifecycle of the FxA account as it pertains to the IP protection service.
 *
 * We have a separation of authentication and authorization so that we can decide which scopes or services to use. With
 * FxA today, a device will have the VPN scope included in the authorization flow, where-as in current Android code, we
 * do not have Sync decoupled from FxA, so we need to authenticate with the VPN and Sync scopes. For this reason, we
 * have divergant flows.
 *
 * To avoid re-requesting an auth flow, we have the intermediary (UI) states `Needs*`, `Requesting*`, and `Awaiting*`:
 *
 * A user is prompted to auth with the [NeedsAuthorization] and [NeedsAuthentication]. We get here when the
 * [mozilla.components.feature.ipprotection.store.IPProtectionStore] deduces that our engine requires a valid auth token
 * to proceed.
 *
 * An observers use the [RequestingAuthorization] and [RequestingAuthentication] states to know we need to initiated an
 * auth flow. The observers are typically some form of UI driver that needs to trigger the flow.
 *
 * A user can leave an incomplete flow at any time in the UI, in which case we need to return to the top of the previous
 * branch. The [AwaitingAuthorization] and [AwaitingAuthorization] let us do this.
 *
 * Whether the flow is successful or not, we try to end with [AwaitingEnrollment]. If we received this event with a
 * result from the account manager, then we can move forward with [AuthFailed] or [Ready], otherwise, we go back into
 * the `Needs*` state for each branch.
 *
 * The optional [TryAgain] is typically used to re-notify the engine that we have an account in a valid auth state and
 * it's safe to re-request an access token, if needed.
 *
 * State transitions:
 * ```
 *                    +---------------+
 *                    | Uninitialized |
 *                    +-------+-------+
 *                            |
 *                            v
 *                    +---------------+
 *                    |   WarmingUp   |
 *                    +---+-------+---+
 *                        |       |
 *              +---------+       +---------+
 *              v                           v
 *  +---------------------+      +----------------------+
 *  | NeedsAuthentication |      |  NeedsAuthorization  |
 *  +----------+----------+      +-----------+----------+
 *             |                             |
 *             v                             v
 *  +--------------------------+  +---------------------------+
 *  | RequestingAuthentication |  | RequestingAuthorization   |
 *  +-------------+------------+  +-------------+-------------+
 *                |                             |
 *                v                             v
 *  +--------------------------+  +---------------------------+
 *  |  AwaitingAuthentication  |  |   AwaitingAuthorization   |
 *  +-------------+------------+  +-------------+-------------+
 *                |                             |
 *                +--------------+--------------+
 *                               v
 *                     +-------------------+
 *                     | AwaitingEnrollment |
 *                     +---+-----------+---+
 *                         |           |
 *              +----------+           +----------+
 *              v                                 v
 *         +------------+                    +---------+
 *         | AuthFailed |                    |  Ready  |
 *         +------+-----+                    +----+----+
 *                |                               |
 *                +---------------+---------------+
 *                                v
 *                         +------------+
 *                         |  TryAgain  |
 *                         +------------+
 * ```
 */
enum class AccountStatus {
    /** Unknown account state. */
    Uninitialized,

    /** First warmup to see if we need to authenticate or authorize. */
    WarmingUp,

    /** The user is not signed in. */
    NoAccount,

    /** Account is in a bad state. */
    NeedsAuthentication,

    /** Start Authenticating. */
    RequestingAuthentication,

    /** Account was in a good state, but authorization is needed. */
    NeedsAuthorization,

    /** Start authorization. */
    RequestingAuthorization,

    /**
     * An intermediary auth state that originates from [RequestingAuthentication] can lead to [AuthFailed],
     * [AwaitingEnrollment], or never completes.
     */
    AwaitingAuthentication,

    /**
     * An intermediary auth state that originates from [RequestingAuthorization] can lead to [AuthFailed],
     * [AwaitingEnrollment], or never completes.
     */
    AwaitingAuthorization,

    /**
     * An intermediary auth state that can start from [AwaitingAuthorization] or [AwaitingAuthentication] that tells us
     * the user has successfully passed fxa auth and moved to enrolling with the [IPProtectionHandler].
     */
    AwaitingEnrollment,

    /** An auth flow was exited abruptly. */
    AuthFailed,

    /** The user is authenticated in the FXA, but we do not know yet if they are entitled to use vpn. */
    Authenticated,

    /** The user is ready to use the service, and able to turn it on at any moment. */
    EnrolledAndEntitled,

    /**
     * An experimental API that tries to re-notify the IP Protection internals to try to fetch the access token again.
     */
    TryAgain,
}
