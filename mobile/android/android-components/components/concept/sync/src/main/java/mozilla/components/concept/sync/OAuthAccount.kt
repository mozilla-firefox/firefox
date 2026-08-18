/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.sync

import kotlinx.coroutines.Deferred

/**
 * User data provided by the web content as a means of delivering the session token to the
 * application
 */
data class UserData(
    val sessionToken: String,
    val email: String,
    val uid: String,
    val verified: Boolean,
)

/**
 * Representing all the possible entry points into FxA
 *
 * These entry points will be reflected in the authentication URL and will be tracked
 * in server telemetry to allow studying authentication entry points independently.
 *
 * If you are introducing a new path to the firefox accounts sign in please add a new entry point
 * here.
 */
interface FxAEntryPoint {
    val entryName: String
}

/**
 * Facilitates testing consumers of FirefoxAccount.
 */
interface OAuthAccount : AutoCloseable {

    /**
     * Returns current FxA Device ID for an authenticated account.
     *
     * @return Current device's FxA ID, if available. `null` otherwise.
     */
    fun getCurrentDeviceId(): String?

    /**
     * Stores whatever is necessary given the JSON payload from a webchannel login.
     * browser layer. The [jsonPayload] is the `data` object from the `fxaccounts:login`
     * WebChannel command.
     */
    suspend fun handleWebChannelLogin(jsonPayload: String)

    /**
     * Returns a complete `signedInUser` JSON object for a WebChannel `fxaccounts:fxa_status`
     * response, embedding the session token privately. Email and uid come from the cached profile
     * in internal state. Returns `null` if no session token is available.
     */
    fun getSignedInUserForWebChannel(): String?

    /**
     * Checks whether every given OAuth scope has been granted to this account, and so whether any
     * scoped keys they carry are available. Reads in-memory state; no network requests.
     *
     * @param scopes Space separated OAuth scopes. Order is not significant.
     */
    fun hasScope(scopes: String): Boolean

    /**
     * Fetches the profile object for the current client either from the existing cached state
     * or from the server (requires the client to have access to the profile scope).
     *
     * @param ignoreCache Fetch the profile information directly from the server
     * @return Profile (optional, if successfully retrieved) representing the user's basic profile info
     */
    suspend fun getProfile(ignoreCache: Boolean = false): Profile?

    /**
     * Tries to fetch an access token for the given scope.
     *
     * @param singleScope Single OAuth scope (no spaces) for which the client wants access
     * @return [AccessTokenInfo] that stores the token, along with its scope, key and
     *                           expiration timestamp (in seconds) since epoch when complete
     */
    suspend fun getAccessToken(singleScope: String): AccessTokenInfo?

    /**
     * Get the list of all client applications attached to the user's account.
     *
     * This method returns a list of AttachedClient structs representing all the applications
     * connected to the user's account. This includes applications that are registered as a
     * device as well as server-side services that the user has connected.
     */
    suspend fun getAttachedClient(): List<AttachedClient>

    /**
     * Call this whenever an authentication error was encountered while using an access token
     * issued by [getAccessToken].
     */
    fun authErrorDetected()

    /**
     * This method should be called when a request made with an OAuth token failed with an
     * authentication error. It will re-build cached state and perform a connectivity check.
     *
     * In time, fxalib will grow a similar method, at which point we'll just relay to it.
     * See https://github.com/mozilla/application-services/issues/1263
     *
     * @param singleScope An oauth scope for which to check authorization state.
     * @return An optional [Boolean] flag indicating if we're connected, or need to go through
     * re-authentication. A null result means we were not able to determine state at this time.
     */
    suspend fun checkAuthorizationStatus(singleScope: String): Boolean?

    /**
     * Fetches the token server endpoint, for authentication using the SAML bearer flow.
     *
     * @return Token server endpoint URL string, `null` if it couldn't be obtained.
     */
    suspend fun getTokenServerEndpointURL(): String?

    /**
     * Fetches the URL for the user to manage their account
     *
     * @param entryPoint A string which will be included as a query param in the URL for metrics.
     * @return The URL which should be opened in a browser tab.
     */
    suspend fun getManageAccountURL(entryPoint: FxAEntryPoint): String?

    /**
     * Get the pairing URL to navigate to on the Authority side (typically a computer).
     *
     * @return The URL to show the pairing user
     */
    fun getPairingAuthorityURL(): String

    /**
     * Registers a callback for when the account state gets persisted
     *
     * @param callback the account state persistence callback
     */
    fun registerPersistenceCallback(callback: StatePersistenceCallback)

    /**
     * Returns the device constellation for the current account
     *
     * @return Device constellation for the current account
     */
    fun deviceConstellation(): DeviceConstellation

    /**
     * Reset internal account state and destroy current device record.
     * Use this when device record is no longer relevant, e.g. while logging out. On success, other
     * devices will no longer see the current device in their device lists.
     *
     * @return A [Deferred] that will be resolved with a success flag once operation is complete.
     * Failure indicates that we may have failed to destroy current device record. Nothing to do for
     * the consumer; device record will be cleaned up eventually via TTL.
     */
    suspend fun disconnect(): Boolean

    /**
     * Serializes the current account's authentication state as a JSON string, for persistence in
     * the Android KeyStore/shared preferences. The authentication state can be restored using
     * [FirefoxAccount.fromJSONString].
     *
     * @return String containing the authentication details in JSON format
     */
    fun toJSONString(): String
}

/**
 * Describes a delegate object that is used by [OAuthAccount] to persist its internal state as it changes.
 */
interface StatePersistenceCallback {
    /**
     * @param data Account state representation as a string (e.g. as json).
     */
    fun persist(data: String)
}

sealed class AuthType {
    /**
     * Account restored from hydrated state on disk.
     */
    object Existing : AuthType()

    /**
     * Account created in response to a sign-in.
     */
    object Signin : AuthType()

    /**
     * Account created in response to a sign-up.
     */
    object Signup : AuthType()

    /**
     * Account created via pairing (similar to sign-in, but without requiring credentials).
     */
    object Pairing : AuthType()

    /**
     * Account was created for an unknown external reason, hopefully identified by [action].
     */
    data class OtherExternal(val action: String?) : AuthType()

    /**
     * Account created via a shared account state from another app via the copy token flow.
     */
    object MigratedCopy : AuthType()

    /**
     * Account created via a shared account state from another app via the reuse token flow.
     */
    object MigratedReuse : AuthType()

    /**
     * Existing account was recovered from an authentication problem.
     */
    object Recovered : AuthType()
}

/**
 * Different types of errors that may be encountered during authorization.
 * Intermittent network problems are the most common reason for these errors.
 */
enum class AuthFlowError {
    /**
     * Couldn't begin authorization, i.e. failed to obtain an authorization URL.
     */
    FailedToBeginAuth,

    /**
     * Couldn't complete authorization after user entered valid credentials/paired correctly.
     */
    FailedToCompleteAuth,
}

/**
 * Observer interface which lets its users monitor account state changes and major events.
 * (XXX - there's some tension between this and the
 * mozilla.components.concept.sync.AccountEvent we should resolve!)
 */
interface AccountObserver {
    /**
     * Account state has been resolved and can now be queried.
     *
     * @param authenticatedAccount Currently resolved authenticated account, if any.
     */
    fun onReady(authenticatedAccount: OAuthAccount?) = Unit

    /**
     * Account just got logged out.
     */
    fun onLoggedOut() = Unit

    /**
     * Account was successfully authenticated.
     *
     * @param account An authenticated instance of a [OAuthAccount].
     * @param authType Describes what kind of authentication event caused this invocation.
     */
    fun onAuthenticated(account: OAuthAccount, authType: AuthType) = Unit

    /**
     * Account's profile is now available.
     * @param profile A fresh version of account's [Profile].
     */
    fun onProfileUpdated(profile: Profile) = Unit

    /**
     * Account needs to be re-authenticated (e.g. due to a password change).
     */
    fun onAuthenticationProblems() = Unit

    /**
     * Encountered an error during an authentication or migration flow.
     * @param error Exact error encountered.
     */
    fun onFlowError(error: AuthFlowError) = Unit
}

data class Avatar(
    val url: String,
    val isDefault: Boolean,
)

data class Profile(
    val uid: String?,
    val email: String?,
    val avatar: Avatar?,
    val displayName: String?,
)

/**
 * Scoped key data.
 *
 * @property kid The JWK key identifier.
 * @property k The JWK key data.
 */
data class OAuthScopedKey(
    val kty: String,
    val scope: String,
    val kid: String,
    val k: String,
)

/**
 * The result of authentication with FxA via an OAuth flow.
 *
 * @property token The access token produced by the flow.
 * @property key An OAuthScopedKey if present.
 * @property expiresAt The expiry date timestamp of this token since unix epoch (in seconds).
 */
data class AccessTokenInfo(
    val scope: String,
    val token: String,
    val key: OAuthScopedKey?,
    val expiresAt: Long,
)

/**
 * The result of a request to get attached clients.
 */
data class AttachedClient(
    val clientId: String?,
    val deviceId: String?,
    val deviceType: DeviceType,
    val isCurrentSession: Boolean,
    val name: String?,
    val createdTime: Long?,
    val lastAccessTime: Long?,
    val scope: List<String>?,
)
