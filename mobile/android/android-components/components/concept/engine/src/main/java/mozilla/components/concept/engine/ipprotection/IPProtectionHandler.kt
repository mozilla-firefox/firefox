/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.engine.ipprotection

import mozilla.components.ExperimentalAndroidComponentsApi

/**
 * App-to-engine handle for controlling the IP protection proxy. Returned by
 * [mozilla.components.concept.engine.Engine.registerIPProtectionDelegate].
 */
@ExperimentalAndroidComponentsApi
interface IPProtectionHandler {

    /**
     * Activates the IP protection.
     *
     * @param countryCode ISO 3166-1 alpha-2 country code.
     * @param onResult Invoked once the activation request resolves. Receives `null` on success or the [Throwable] that
     *   caused the failure.
     */
    fun activate(countryCode: String?, onResult: (Throwable?) -> Unit = {})

    /**
     * Deactivates the IP protection proxy.
     *
     * @param onResult Invoked once the deactivation request resolves. Receives `null` on success or the [Throwable]
     *   that caused the failure.
     */
    fun deactivate(onResult: (Throwable?) -> Unit = {})

    /**
     * Triggers enrollment via the active auth provider. The [onResult] callback is invoked once the enrollment attempt
     * has completed, with the final outcome.
     *
     * @param onResult Called with the [EnrollResult] describing whether the user is now enrolled and entitled, and the
     *   error string if not.
     */
    fun enroll(onResult: (EnrollResult) -> Unit)

    /** Request for the current [ServiceState]. */
    fun getState(onResult: (ServiceState) -> Unit)

    /**
     * Requests an update for the list of countries available in the proxy server-list.
     *
     * @param onResult Invoked once the update request resolves. Receives `null` on success or the [Throwable] that
     *   caused the failure.
     */
    fun updateCountryList(onResult: (Throwable?) -> Unit = {})

    /** Initializes the proxy state machine. */
    fun init()

    /** Uninitializes the proxy state machine. */
    fun uninit()

    /**
     * Sets the [AuthProvider] used to supply authentication tokens to the IP protection service. Pass null to sign out.
     *
     * @param provider The [AuthProvider], or null to deauthenticate.
     */
    // FIXME(IPP) move this to the IPProtectionDelegate.
    fun setAuthProvider(provider: AuthProvider?)

    /**
     * Sets the [GpiProvider] used to handle Google Play Integrity warm-up and token requests. Pass null to clear the
     * provider.
     *
     * @param provider The [GpiProvider], or null to clear.
     */
    fun setGpiProvider(provider: GpiProvider?)

    /**
     * Result of an enrollment attempt.
     *
     * @property isEnrolledAndEntitled Whether the user is now enrolled and entitled to use the proxy.
     * @property error Error string describing why enrollment failed, or null on success.
     */
    data class EnrollResult(
        val isEnrolledAndEntitled: Boolean,
        val error: String? = null,
    )

    /** Notify account state changed. */
    fun notifyAccountStatus(signedIn: Boolean)

    /**
     * Provides a fresh authentication token on demand. Invoked each time the engine needs to authenticate with the
     * Guardian API.
     */
    interface AuthProvider {
        /**
         * Fetches a fresh authentication token and delivers it via [onComplete]. Pass null to [onComplete] if the token
         * could not be obtained.
         */
        fun getToken(onComplete: (String?) -> Unit)
    }

    /** Provides Google Play Integrity warm-up and token retrieval for the IP protection service. */
    interface GpiProvider {
        /** Warms up the GPI token provider. Calls [onComplete] with true on success, false on failure. */
        fun warmUp(onComplete: (Boolean) -> Unit)

        /**
         * Fetches a GPI integrity token and delivers it via [onComplete]. Pass null to [onComplete] if the token could
         * not be obtained.
         */
        fun getToken(onComplete: (String?) -> Unit)
    }

    /**
     * Represents a country from the IP protection proxy server list.
     *
     * @property code ISO 3166-1 alpha-2 country code.
     * @property available Whether the country could be selected as the active proxy.
     */
    data class Country(
        val code: String,
        val available: Boolean,
    )
}

/** The possible states of the IP protection proxy. */
@ExperimentalAndroidComponentsApi
enum class ProxyState {
    /** The proxy is not ready. */
    NOT_READY,

    /** The proxy is ready to be activated. */
    READY,

    /** The proxy is in the process of activating. */
    ACTIVATING,

    /** The proxy is active. */
    ACTIVE,

    /** The proxy encountered an error. */
    ERROR,

    /** The proxy is paused (e.g. bandwidth limit reached). */
    PAUSED,

    /** The engine reported a proxy state that this version does not know. */
    UNKNOWN,
}

/** The possible states of the IP protection service. */
@ExperimentalAndroidComponentsApi
enum class ServiceState {

    /** The service has not been initialized yet. */
    Uninitialized,

    /** The user is not eligible or still not signed in. */
    Unavailable,

    /** The user is signed out but eligible. */
    Unauthenticated,

    /** The service is ready to be activated. */
    Ready,
}
