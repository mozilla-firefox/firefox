/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.appservices.fxaclient

import android.util.Log
import mozilla.appservices.sync15.DeviceType

/**
 * FxaClient represents the authentication state of a client.
 *
 * This is a thin wrapper around the `FirefoxAccount` object exposed from Rust.
 * Its main job is to transparently manage persistence of the account state by
 * calling the provided callback at appropriate times.
 *
 * In future it would be nice to push this logic down into the Rust code,
 * once UniFFI's support for callback interfaces is a little more battle-tested.
 *
 */
class FxaClient(inner: FirefoxAccount, persistCallback: PersistCallback?) : AutoCloseable {
    private var inner: FirefoxAccount = inner
    private var persistCallback: PersistCallback? = persistCallback

    /**
     * Create a FxaClient using the given config.
     *
     * This does not make network requests, and can be used on the main thread.
     *
     */
    constructor(config: FxaConfig, persistCallback: PersistCallback? = null) : this(
        FirefoxAccount(config),
        persistCallback,
    ) {
        // Persist the newly created instance state.
        this.tryPersistState()
    }

    companion object {
        /**
         * Restores the account's authentication state from a JSON string produced by
         * [FxaClient.toJSONString].
         *
         * This does not make network requests, and can be used on the main thread.
         *
         * @return [FxaClient] representing the authentication state
         */
        fun fromJSONString(json: String, persistCallback: PersistCallback? = null): FxaClient {
            return FxaClient(FirefoxAccount.fromJson(json), persistCallback)
        }
    }

    interface PersistCallback {
        fun persist(data: String)
    }

    /**
     * Registers a PersistCallback that will be called every time the
     * FirefoxAccount internal state has mutated.
     * The FirefoxAccount instance can be later restored using the
     * `fromJSONString` class method.
     * It is the responsibility of the consumer to ensure the persisted data
     * is saved in a secure location, as it can contain Sync Keys and
     * OAuth tokens.
     */
    fun registerPersistCallback(persistCallback: PersistCallback) {
        this.persistCallback = persistCallback
    }

    /**
     * Unregisters any previously registered PersistCallback.
     */
    fun unregisterPersistCallback() {
        this.persistCallback = null
    }

    private fun tryPersistState() {
        this.persistCallback?.let {
            val json: String
            try {
                json = this.toJSONString()
            } catch (e: FxaException) {
                Log.e("FirefoxAccount", "Error serializing the FirefoxAccount state.")
                return
            }
            it.persist(json)
        }
    }

    /**
     * Process an event (login, logout, etc).
     *
     * On success, update the current state and return it.
     * On error, the current state will remain the same.
     */
    fun processEvent(event: FxaEvent): FxaState = this.inner.processEvent(event)

    /**
     * Get the high-level authentication state of the client
     */
    fun getAuthState() = this.inner.getAuthState()

    /**
     * Stores anything necessary to login from a WebChannel login JSON payload. This includes the session
     * token, but that is abstracted because the consuming apps should not be aware of the
     * specific payload format returned, nor should they get access to the session token
     * directly if possible.
     *
     * @param jsonPayload The `data` object from the `fxaccounts:login` WebChannel command.
     */
    fun handleWebChannelLogin(jsonPayload: String) {
        this.inner.handleWebChannelLogin(jsonPayload)
        tryPersistState()
    }

    /**
     * Handle a WebChannel password-change notification by exchanging the new session token
     * for a new refresh token via a network call.
     *
     * @param jsonPayload is the `data` object from the `fxaccounts:change_password` WebChannel command.
     */
    fun handleWebChannelPasswordChange(jsonPayload: String) {
        this.inner.handleWebChannelPasswordChange(jsonPayload)
        tryPersistState()
    }

    /**
     * Returns a complete signedInUser JSON object for a WebChannel fxaccounts:fxa_status response.
     *
     * @return An opaque string which holds JSON data and can be directly supplied to the WebChannel.
     */
    fun getSignedInUserForWebChannel(): String? {
        return this.inner.getSignedInUserForWebChannel()
    }

    /**
     * Fetches the profile object for the current client either from the existing cached account,
     * or from the server (requires the client to have access to the profile scope).
     *
     * This performs network requests, and should not be used on the main thread.
     *
     * @param ignoreCache Fetch the profile information directly from the server
     * @return [Profile] representing the user's basic profile info
     * @throws FxaException.Unauthorized We couldn't find any suitable access token to make that call.
     * The caller should then start the OAuth Flow again with the "profile" scope.
     */
    fun getProfile(ignoreCache: Boolean): Profile {
        try {
            return this.inner.getProfile(ignoreCache)
        } finally {
            this.tryPersistState()
        }
    }

    /**
     * Convenience method to fetch the profile from a cached account by default, but fall back
     * to retrieval from the server.
     *
     * This performs network requests, and should not be used on the main thread.
     *
     * @return [Profile] representing the user's basic profile info
     * @throws FxaException.Unauthorized We couldn't find any suitable access token to make that call.
     * The caller should then start the OAuth Flow again with the "profile" scope.
     */
    fun getProfile(): Profile {
        return getProfile(false)
    }

    /**
     * Fetches the token server endpoint, for authenticating to Firefox Sync via OAuth.
     *
     * This performs network requests, and should not be used on the main thread.
     */
    fun getTokenServerEndpointURL(): String {
        return this.inner.getTokenServerEndpointUrl()
    }

    /**
     * Get the pairing URL to navigate to on the Auth side (typically a computer).
     *
     * This does not make network requests, and can be used on the main thread.
     */
    fun getPairingAuthorityURL(): String {
        return this.inner.getPairingAuthorityUrl()
    }

    /**
     * Fetches the connection success url.
     *
     * This does not make network requests, and can be used on the main thread.
     */
    fun getConnectionSuccessURL(): String {
        return this.inner.getConnectionSuccessUrl()
    }

    /**
     * Fetches the user's manage-account url.
     *
     * This performs network requests, and should not be used on the main thread.
     *
     * @throws FxaException.Unauthorized We couldn't find any suitable access token to identify the user.
     * The caller should then start the OAuth Flow again with the "profile" scope.
     */
    fun getManageAccountURL(entrypoint: String): String {
        return this.inner.getManageAccountUrl(entrypoint)
    }

    /**
     * Fetches the user's manage-devices url.
     *
     * This performs network requests, and should not be used on the main thread.
     *
     * @throws FxaException.Unauthorized We couldn't find any suitable access token to identify the user.
     * The caller should then start the OAuth Flow again with the "profile" scope.
     */
    fun getManageDevicesURL(entrypoint: String): String {
        return this.inner.getManageDevicesUrl(entrypoint)
    }

    /**
     * Get the list of all client applications attached to the user's account.
     *
     * This method returns a list of [AttachedClient] structs representing all the applications
     * connected to the user's account. This includes applications that are registered as a device
     * as well as server-side services that the user has connected.
     *
     * This information is really only useful for targeted messaging or marketing purposes,
     * e.g. if the application wants to advertize a related product, but first wants to check
     * whether the user is already using that product.
     *
     * # Notes
     *
     *    - Attached client metadata is only visible to applications that have been
     *      granted the `https:///identity.mozilla.com/apps/oldsync` scope.

     */
    fun getAttachedClients(): List<AttachedClient> {
        return this.inner.getAttachedClients()
    }

    /**
     * Tries to fetch an access token for the given scope.
     *
     * This performs network requests, and should not be used on the main thread.
     * It may modify the persisted account state.
     *
     * On `FxaException.Unauthorized` and `FxaException.SyncScopedKeyMissingInServerResponse`, the
     * caller should indicate to the user that there are authentication issues and allow them to
     * re-login by starting a new OAuth flow.
     *
     * @param scope Space-separated list of OAuth scopes for which the client wants access.
     * Scope order is not significant. When a single scope is requested and it has an associated
     * scoped key, [AccessTokenInfo.key] will be populated; for multi-scope requests it is null.
     * @param useCache set to false to force a new token request.  The fetched token will still be
     * cached for later `get_access_token` calls.
     * @return [AccessTokenInfo] that stores the token, along with its scopes and keys when complete
     * @throws FxaException.Network Network error while requesting the access token.
     * @throws FxaException.Unauthorized We couldn't provide an access token for this scope.
     * @throws FxaException.SyncScopedKeyMissingInServerResponse we received an access token for the
     * sync scoped, but the sync key that should accompany it was missing.
     */
    fun getAccessToken(scope: String, useCache: Boolean = true): AccessTokenInfo {
        try {
            return this.inner.getAccessToken(scope, useCache)
        } finally {
            this.tryPersistState()
        }
    }

    /**
     * Check whether the account has already been granted every given OAuth scope(s).
     *
     * @param scope space-separated list of OAuth scopes. Order is not significant.
     */
    fun hasScope(scope: String): Boolean {
        return this.inner.hasScope(scope)
    }

    fun checkAuthorizationStatus(): AuthorizationInfo {
        return this.inner.checkAuthorizationStatus()
    }

    /**
     * Get the current device id
     *
     * @throws FxaException Will send you an exception if there is no device id set
     */
    fun getCurrentDeviceId(): String {
        return this.inner.getCurrentDeviceId()
    }

    /**
     * Provisions an OAuth code using the session token from state
     *
     * @param authParams Parameters needed for the authorization request
     * This performs network requests, and should not be used on the main thread.
     */
    fun authorizeOAuthCode(
        authParams: AuthorizationParameters,
    ): String {
        return this.inner.authorizeCodeUsingSessionToken(authParams)
    }

    /**
     * This method should be called when a request made with
     * an OAuth token failed with an authentication error.
     * It clears the internal cache of OAuth access tokens,
     * so the caller can try to call `getAccessToken` or `getProfile`
     * again.
     */
    fun clearAccessTokenCache() {
        this.inner.clearAccessTokenCache()
    }

    /**
     * Saves the current account's authentication state as a JSON string, for persistence in
     * the Android KeyStore/shared preferences. The authentication state can be restored using
     * [FirefoxAccount.fromJSONString].
     *
     * This does not make network requests, and can be used on the main thread.
     *
     * @return String containing the authentication details in JSON format
     */
    fun toJSONString(): String {
        return this.inner.toJson()
    }

    /**
     * Update the push subscription details for the current device.
     * This needs to be called every time a push subscription is modified or expires.
     *
     * This performs network requests, and should not be used on the main thread.
     *
     * @param endpoint Push callback URL
     * @param publicKey Public key used to encrypt push payloads
     * @param authKey Auth key used to encrypt push payloads
     */
    fun setDevicePushSubscription(endpoint: String, publicKey: String, authKey: String) {
        try {
            this.inner.setPushSubscription(DevicePushSubscription(endpoint, publicKey, authKey))
        } finally {
            this.tryPersistState()
        }
    }

    /**
     * Update the display name (as shown in the FxA device manager, or the Send Tab target list)
     * for the current device.
     *
     * This performs network requests, and should not be used on the main thread.
     *
     * @param displayName The current device display name
     */
    fun setDeviceDisplayName(displayName: String) {
        try {
            this.inner.setDeviceName(displayName)
        } finally {
            this.tryPersistState()
        }
    }

    /**
     * Retrieves the list of the connected devices in the current account, including the current one.
     *
     * This performs network requests, and should not be used on the main thread.
     */
    fun getDevices(ignoreCache: Boolean = false): Array<Device> {
        return this.inner.getDevices(ignoreCache).toTypedArray()
    }

    /**
     * Disconnect from the account and optionally destroy our device record.
     * A `BeginOAuthFlow` state-machine event will need to be sent to reconnect.
     *
     * This performs network requests, and should not be used on the main thread.
     */
    fun disconnect() {
        this.inner.disconnect()
        this.tryPersistState()
    }

    /**
     * Retrieves any pending commands for the current device.
     * This should be called semi-regularly as the main method of commands delivery (push)
     * can sometimes be unreliable on mobile devices.
     * If a persist callback is set and the host application failed to process the
     * returned account events, they will never be seen again.
     *
     * This performs network requests, and should not be used on the main thread.
     *
     * @return A collection of [IncomingDeviceCommand] that should be handled by the caller.
     */
    fun pollDeviceCommands(): Array<IncomingDeviceCommand> {
        try {
            return this.inner.pollDeviceCommands().toTypedArray()
        } finally {
            this.tryPersistState()
        }
    }

    /**
     * Retrieves the account event associated with an
     * incoming push message payload coming Firefox Accounts.
     * Assumes the message that has been decrypted and authenticated by the Push crate.
     *
     * This performs network requests, and should not be used on the main thread.
     *
     * @return A collection of [AccountEvent] that should be handled by the caller.
     */
    fun handlePushMessage(payload: String): AccountEvent {
        try {
            return this.inner.handlePushMessage(payload)
        } finally {
            this.tryPersistState()
        }
    }

    /**
     * Ensure the current device is registered with the specified name and device type, with
     * the required capabilities (at this time only Send Tab).
     * This method should be called once per "device lifetime".
     *
     * This performs network requests, and should not be used on the main thread.
     */
    fun initializeDevice(name: String, deviceType: DeviceType, supportedCapabilities: Set<DeviceCapability>) {
        this.inner.initializeDevice(name, deviceType, supportedCapabilities.toList())
        this.tryPersistState()
    }

    /**
     * Ensure that the supported capabilities described earlier in `initializeDevice` are A-OK.
     * A set of capabilities to be supported by the Device must also be passed (at this time only
     * Send Tab).
     *
     * As for now there's only the Send Tab capability, so we ensure the command is registered with the server.
     * This method should be called at least every time the sync keys change (because Send Tab relies on them).
     *
     * This performs network requests, and should not be used on the main thread.
     */
    fun ensureCapabilities(supportedCapabilities: Set<DeviceCapability>) {
        this.inner.ensureCapabilities(supportedCapabilities.toList())
        this.tryPersistState()
    }

    /**
     * Send a single tab to another device identified by its device ID.
     *
     * This performs network requests, and should not be used on the main thread.
     *
     * @param targetDeviceId The target Device ID
     * @param title The document title of the tab being sent
     * @param url The url of the tab being sent
     * @param isPrivate Whether the tab is open as a private tab. Has a default value to help with progressive
     * implementation of this new attribute, but the default should be removed at some point.
     */
    fun sendSingleTab(targetDeviceId: String, title: String, url: String, isPrivate: Boolean = false) {
        this.inner.sendSingleTab(targetDeviceId, title, url, isPrivate)
    }

    /**
     * Close one or more tabs that are currently open on another device.
     *
     * This performs network requests, and should not be used on the main thread.
     *
     * @param targetDeviceId The ID of the device on which the tabs are
     * currently open.
     * @param urls The URLs of the tabs to close.
     * @return The result of the operation.
     */
    fun closeTabs(targetDeviceId: String, urls: List<String>): CloseTabsResult =
        this.inner.closeTabs(targetDeviceId, urls)

    /**
     * Gather any telemetry which has been collected internally and return
     * the result as a JSON string.
     *
     * This does not make network requests, and can be used on the main thread.
     */
    fun gatherTelemetry(): String {
        return this.inner.gatherTelemetry()
    }

    /**
     * Used by the application to test auth token issues
     */
    fun simulateNetworkError() = this.inner.simulateNetworkError()

    /**
     * Used by the application to test auth token issues
     */
    fun simulateTemporaryAuthTokenIssue() = this.inner.simulateTemporaryAuthTokenIssue()

    /**
     * Used by the application to test auth token issues
     */
    fun simulatePermanentAuthTokenIssue() = this.inner.simulatePermanentAuthTokenIssue()

    @Synchronized
    override fun close() {
        this.inner.destroy()
    }
}
