/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.fxa

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.plus
import kotlinx.coroutines.withContext
import mozilla.appservices.fxaclient.FxaClient
import mozilla.appservices.fxaclient.FxaEvent
import mozilla.appservices.fxaclient.FxaState
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.sync.DeviceConstellation
import mozilla.components.concept.sync.FxAEntryPoint
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.concept.sync.StatePersistenceCallback
import mozilla.components.support.base.log.logger.Logger

typealias PersistCallback = mozilla.appservices.fxaclient.FxaClient.PersistCallback

/** FirefoxAccount represents the authentication state of a client. */
class FirefoxAccount
internal constructor(
    private val inner: FxaClient,
    crashReporter: CrashReporting? = null,
) : OAuthAccount {
    private val job = SupervisorJob()
    private val scope = CoroutineScope(Dispatchers.IO) + job

    private val logger = Logger("FirefoxAccount")

    /**
     * Why this exists: in the `init` block below you'll notice that we register a persistence callback as soon as we
     * initialize this object. Essentially, we _always_ have a persistence callback registered with [FxaClient].
     * However, our own lifecycle is such that we will not know how to actually persist account state until sometime
     * after this object has been created. Currently, we're expecting [FxaAccountManager] to configure a real callback.
     * This wrapper exists to facilitate that flow of events.
     */
    private class WrappingPersistenceCallback : PersistCallback {
        private val logger = Logger("WrappingPersistenceCallback")

        @Volatile private var persistenceCallback: StatePersistenceCallback? = null

        fun setCallback(callback: StatePersistenceCallback) {
            logger.debug("Setting persistence callback")
            persistenceCallback = callback
        }

        override fun persist(data: String) {
            val callback = persistenceCallback

            if (callback == null) {
                logger.warn("FxaClient tried persist state, but persistence callback is not set")
            } else {
                logger.debug("Logging state to $callback")
                callback.persist(data)
            }
        }
    }

    private var persistCallback = WrappingPersistenceCallback()
    private val deviceConstellation = FxaDeviceConstellation(inner, scope, crashReporter)

    init {
        inner.registerPersistCallback(persistCallback)
    }

    /**
     * Construct a FirefoxAccount from a [Config], a clientId, and a redirectUri.
     *
     * @param crashReporter A crash reporter instance.
     *
     * Note that it is not necessary to `close` the Config if this constructor is used (however doing so will not cause
     * an error).
     */
    constructor(
        config: ServerConfig,
        crashReporter: CrashReporting? = null,
    ) : this(FxaClient(config), crashReporter)

    override fun close() {
        job.cancel()
        inner.close()
    }

    override fun registerPersistenceCallback(callback: StatePersistenceCallback) {
        logger.info("Registering persistence callback")
        persistCallback.setCallback(callback)
    }

    internal fun getAuthState() = inner.getAuthState()

    internal fun processEvent(event: FxaEvent): FxaState = inner.processEvent(event)

    internal fun simulateNetworkError() = inner.simulateNetworkError()

    internal fun simulateTemporaryAuthTokenIssue() = inner.simulateTemporaryAuthTokenIssue()

    internal fun simulatePermanentAuthTokenIssue() = inner.simulatePermanentAuthTokenIssue()

    override suspend fun getProfile(ignoreCache: Boolean) =
        withContext(scope.coroutineContext) {
            handleFxaExceptions(logger, "getProfile", { null }) {
                inner.getProfile(ignoreCache).into()
            }
        }

    override suspend fun handleWebChannelLogin(jsonPayload: String) {
        handleFxaExceptions(logger, "handleWebChannelLogin", { null }) {
            inner.handleWebChannelLogin(jsonPayload)
        }
    }

    override fun getCurrentDeviceId(): String? {
        // This is awkward, yes. Underlying method simply reads some data from in-memory state, and yet it throws
        // in case that data isn't there. See https://github.com/mozilla/application-services/issues/2202.
        return try {
            inner.getCurrentDeviceId()
        } catch (e: FxaPanicException) {
            throw e
        } catch (e: FxaException) {
            null
        }
    }

    override fun getSignedInUserForWebChannel(): String? {
        return inner.getSignedInUserForWebChannel()
    }

    override suspend fun getTokenServerEndpointURL() =
        withContext(scope.coroutineContext) {
            handleFxaExceptions(logger, "getTokenServerEndpointURL", { null }) {
                inner.getTokenServerEndpointURL()
            }
        }

    override suspend fun getManageAccountURL(entryPoint: FxAEntryPoint): String? {
        return handleFxaExceptions(logger, "getManageAccountURL", { null }) {
            inner.getManageAccountURL(entryPoint.entryName)
        }
    }

    override fun getPairingAuthorityURL(): String {
        return inner.getPairingAuthorityURL()
    }

    /** Fetches the connection success url. */
    fun getConnectionSuccessURL(): String {
        return inner.getConnectionSuccessURL()
    }

    override suspend fun getAccessToken(singleScope: String) =
        withContext(scope.coroutineContext) {
            handleFxaExceptions(logger, "get access token", { null }) {
                inner.getAccessToken(singleScope).into()
            }
        }

    /** See [OAuthAccount.getAttachedClient]. */
    override suspend fun getAttachedClient() =
        withContext(scope.coroutineContext) {
            handleFxaExceptions(logger, "get attached client", { emptyList() }) {
                inner.getAttachedClients().map { it.into() }
            }
        }

    override fun authErrorDetected() {
        // fxalib maintains some internal token caches that need to be cleared whenever we
        // hit an auth problem. Call below makes that clean-up happen.
        inner.clearAccessTokenCache()
    }

    override suspend fun checkAuthorizationStatus(singleScope: String) =
        withContext(scope.coroutineContext) {
            // Now that internal token caches are cleared, we can perform a connectivity check.
            // Do so by requesting a new access token using an internally-stored "refresh token".
            // Success here means that we're still able to connect - our cached access token simply expired.
            // Failure indicates that we need to re-authenticate.
            try {
                inner.getAccessToken(singleScope)
                // We were able to obtain a token, so we're in a good authorization state.
                true
            } catch (e: FxaUnauthorizedException) {
                // We got back a 401 while trying to obtain a new access token, which means our refresh
                // token is also in a bad state. We need re-authentication for the tested scope.
                false
            } catch (e: FxaPanicException) {
                // Re-throw any panics we may encounter.
                throw e
            } catch (e: FxaException) {
                // On any other FxaExceptions (networking, etc) we have to return an indeterminate result.
                null
            }
            // Re-throw all other exceptions.
        }

    override suspend fun disconnect() =
        withContext(scope.coroutineContext) {
            // TODO can this ever throw FxaUnauthorizedException? would that even make sense? or is that a bug?
            handleFxaExceptions(logger, "disconnect", { false }) {
                inner.disconnect()
                true
            }
        }

    override fun deviceConstellation(): DeviceConstellation {
        return deviceConstellation
    }

    override fun hasScope(scope: String): Boolean {
        return inner.hasScope(scope)
    }

    override fun toJSONString(): String = inner.toJSONString()

    companion object {
        /**
         * Restores the account's authentication state from a JSON string produced by [FirefoxAccount.toJSONString].
         *
         * @param crashReporter object used for logging caught exceptions
         * @param persistCallback This callback will be called every time the [FirefoxAccount] internal state has
         *   mutated. The FirefoxAccount instance can be later restored using the [FirefoxAccount.fromJSONString]` class
         *   method. It is the responsibility of the consumer to ensure the persisted data is saved in a secure
         *   location, as it can contain Sync Keys and OAuth tokens.
         * @return [FirefoxAccount] representing the authentication state
         */
        fun fromJSONString(
            json: String,
            crashReporter: CrashReporting?,
            persistCallback: PersistCallback? = null,
        ): FirefoxAccount {
            return FirefoxAccount(FxaClient.fromJSONString(json, persistCallback), crashReporter)
        }
    }
}
