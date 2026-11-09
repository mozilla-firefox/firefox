/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.store

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler
import mozilla.components.concept.engine.ipprotection.ProxyState
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.Location
import mozilla.components.feature.ipprotection.store.state.ProxyActivation
import mozilla.components.feature.ipprotection.store.state.ProxyRequest
import mozilla.components.lib.state.Action

/** Actions that can be dispatched to [IPProtectionStore]. */
sealed class IPProtectionAction : Action {
    /** [IPProtectionAction]s related to the user's eligibility for IP Protection. */
    sealed class EligibilityAction : IPProtectionAction() {
        /** Reports a change in whether the user qualifies for IP Protection. */
        data class EligibilityChanged(val eligibility: EligibilityStatus) : EligibilityAction()
    }

    /** [IPProtectionAction]s related to the service state. */
    sealed class ServiceAction : IPProtectionAction() {
        /**
         * Reports a new service state from the GeckoView IP protection toolkit.
         *
         * @property serviceState The current [ServiceState].
         */
        data class ServiceStateChanged(val serviceState: ServiceState) : ServiceAction()
    }

    /** [IPProtectionAction]s related to the proxy state. */
    sealed class ProxyAction : IPProtectionAction() {
        /**
         * Reports a new proxy state from the GeckoView IP protection toolkit.
         *
         * @property proxyState The current [ProxyState].
         * @property lastError The error the proxy last failed with, or null.
         */
        data class ProxyStateChanged(
            val proxyState: ProxyState,
            val lastError: String? = null,
        ) : ProxyAction()
    }

    /** [IPProtectionAction]s related to the data usage. */
    sealed class UsageAction : IPProtectionAction() {
        /**
         * Reports a new data usage from the GeckoView IP protection toolkit.
         *
         * @property remaining Remaining data allowance in bytes.
         * @property max Maximum data allowance in bytes, or 0 if unlimited.
         * @property resetTime ISO 8601 string for when the allowance resets, or null if unavailable.
         */
        data class UsageChanged(
            val remaining: Long,
            val max: Long,
            val resetTime: String?,
        ) : UsageAction()
    }

    /** [IPProtectionAction]s related to the account status. */
    sealed class AccountAction : IPProtectionAction() {
        /**
         * Sets the account status.
         *
         * @property state The new [AccountStatus].
         */
        data class AccountStateChanged(val state: AccountStatus) : AccountAction()

        /**
         * Checks if an account has already been entitled. If so, this will lead to a token exchange that gives us a new
         * refresh token with increased scopes. If not, we do nothing.
         */
        object CheckAccount : AccountAction()

        /**
         * Reports a new account status from the account manager.
         *
         * @property status The [AccountStatus] derived from the account manager state.
         */
        internal data class AccountManagerStateChanged(val status: AccountStatus) : AccountAction()

        /** Reports that the account is ready to be used. */
        internal object AccountReadyForEnrollment : AccountAction()

        /**
         * Reports that the enrollment of the user has finished. They are now either entitled to use IP protection
         * feature or it errored out and they should try again.
         *
         * @property success Whether enrollment was successful or not.
         */
        internal data class FinishingEnrollment(val success: Boolean) : AccountAction()

        /**
         * Reports that the authentication flow has finished. It could have finished automatically via successful
         * authentication/authorization, or it could have been interrupted (canceled).
         */
        internal object FinishingAuthFlow : AccountAction()

        /** Puts the auth flow into an intermediary state while an incomplete authentication is occurring. */
        internal data class AwaitingAuth(val status: AccountStatus) : AccountAction()
    }

    /** [IPProtectionAction]s related to turning the proxy on or off. */
    sealed class ActivationAction : IPProtectionAction() {
        /**
         * Turns the IP Protection proxy either on/off - if the service requires an access token, the account auth-flow
         * is instantiated.
         */
        object Toggle : ActivationAction()

        /**
         * Clears the current [ProxyActivation] state.
         *
         * Resets [IPProtectionState.proxyActivation] to [ProxyActivation.Idle] after it has been handled.
         */
        data object ProxyActivationShown : ActivationAction()

        /**
         * Reports that the most recent activate or deactivate request failed.
         *
         * @property operation Which of the two engine requests failed.
         * @property error The [Throwable] the engine rejected the request with, or null when the engine gave no reason.
         */
        data class ToggleFailed(
            val operation: ActivationOperation,
            val error: Throwable? = null,
        ) : ActivationAction()

        /**
         * Reports that [mozilla.components.feature.ipprotection.store.middleware.IPProtectionEngineMiddleware] started
         * sending a proxy request.
         *
         * @property request The [ProxyRequest] the middleware started sending.
         */
        data class ProxyRequestStarted(val request: ProxyRequest) : ActivationAction()

        /**
         * Reports that [mozilla.components.feature.ipprotection.store.middleware.IPProtectionEngineMiddleware] finished
         * or cancelled the proxy request it was sending.
         */
        data object ProxyRequestFinished : ActivationAction()
    }

    /** [IPProtectionAction]s related to the selected location and the list of available locations. */
    sealed class LocationAction : IPProtectionAction() {
        /** Reports a fresh list of available proxy countries from the GeckoView IP protection toolkit. */
        data class CountryListChanged(val countries: List<IPProtectionHandler.Country>) : LocationAction()

        /**
         * Reports a newly selected location by the user from the location list.
         *
         * @property location The selected location.
         * @property userAction Whether the user picked the location, as opposed to the app restoring a persisted one.
         */
        data class LocationChanged(
            val location: Location,
            val userAction: Boolean,
        ) : LocationAction()

        /**
         * Reports a location reset, due to the previously selected location being unavailable.
         *
         * @property countryCode The cached country code that was not available, or null when no country code was
         *   cached.
         * @property status What the refreshed country list reported about the cached location.
         */
        data class LocationReset(
            val countryCode: String?,
            val status: CachedLocationStatus,
        ) : LocationAction()

        /**
         * Reports that a persisted location could not be restored because it is no longer available.
         *
         * @property countryCode The cached country code that could not be restored.
         * @property status What the refreshed country list reported about the cached location.
         */
        data class PersistedLocationUnavailable(
            val countryCode: String,
            val status: CachedLocationStatus,
        ) : LocationAction()

        /**
         * Reports that switching to a new location failed.
         *
         * @property error The [Throwable] the engine rejected the request with, or null when the engine gave no reason.
         */
        data class LocationSwitchFailed(val error: Throwable? = null) : LocationAction()

        /**
         * Reports that a location list update has failed.
         *
         * @property error The [Throwable] the engine rejected the request with.
         */
        data class LocationUpdateFailed(val error: Throwable) : LocationAction()

        /** Checks if the list of available locations needs to be updated due to a previously failed attempt. */
        object CheckLocations : LocationAction()
    }
}

/** Which engine request an [IPProtectionAction.ActivationAction.ToggleFailed] is reporting on. */
enum class ActivationOperation {
    /** An `activate` request. */
    Activate,

    /** A `deactivate` request. */
    Deactivate,
}

/** What a refreshed country list reported about the cached location. */
enum class CachedLocationStatus {
    /** The cached country was absent from the refreshed list. */
    Missing,

    /** The cached country was in the refreshed list and not selectable. */
    Unavailable,
}
