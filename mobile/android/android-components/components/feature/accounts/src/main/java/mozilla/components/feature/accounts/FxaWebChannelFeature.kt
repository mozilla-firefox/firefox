/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.accounts

import androidx.annotation.VisibleForTesting
import androidx.core.net.toUri
import java.net.URL
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.launch
import mozilla.appservices.fxaclient.contentUrl
import mozilla.appservices.fxaclient.isCustom
import mozilla.components.browser.state.selector.findCustomTabOrSelectedTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.webextension.MessageHandler
import mozilla.components.concept.engine.webextension.Port
import mozilla.components.concept.engine.webextension.WebExtensionRuntime
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.FxAEntryPoint
import mozilla.components.concept.sync.SyncEngine
import mozilla.components.feature.accounts.FxaWebChannelFeature.Companion.COMMAND_CAN_LINK_ACCOUNT
import mozilla.components.feature.accounts.FxaWebChannelFeature.Companion.COMMAND_CHANGE_PASSWORD
import mozilla.components.feature.accounts.FxaWebChannelFeature.Companion.COMMAND_DELETE_ACCOUNT
import mozilla.components.feature.accounts.FxaWebChannelFeature.Companion.COMMAND_LOGIN
import mozilla.components.feature.accounts.FxaWebChannelFeature.Companion.COMMAND_LOGOUT
import mozilla.components.feature.accounts.FxaWebChannelFeature.Companion.COMMAND_OAUTH_LOGIN
import mozilla.components.feature.accounts.FxaWebChannelFeature.Companion.COMMAND_PAIR_OAUTH_START
import mozilla.components.feature.accounts.FxaWebChannelFeature.Companion.COMMAND_STATUS
import mozilla.components.feature.accounts.FxaWebChannelFeature.Companion.COMMAND_SYNC_PREFERENCES
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.service.fxa.FxaAuthData
import mozilla.components.service.fxa.ServerConfig
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.service.fxa.manager.SCOPE_PROFILE
import mozilla.components.service.fxa.manager.SCOPE_SESSION
import mozilla.components.service.fxa.manager.SCOPE_SYNC
import mozilla.components.service.fxa.sync.toSyncEngines
import mozilla.components.service.fxa.toAuthType
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.kotlin.isSameOriginAs
import mozilla.components.support.webextensions.BuiltInWebExtensionController
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/** Configurable FxA capabilities. */
enum class FxaCapability {
    // Enables "choose what to sync" selection during support auth flows (currently, sign-up).
    CHOOSE_WHAT_TO_SYNC,

    // Advertises `pairingVersion` to FxA and enables the v2 pairing web channel commands.
    PAIRING_V2,
}

private object PairingWebChannelEntryPoint : FxAEntryPoint {
    override val entryName = "webchannel-pairing"
}

/**
 * Feature implementation that provides Firefox Accounts WebChannel support. For more information
 * https://github.com/mozilla/fxa/blob/master/packages/fxa-content-server/docs/relier-communication-protocols/fx-webchannel.md
 * This feature uses a web extension to communicate with FxA Web Content.
 *
 * @property customTabSessionId optional custom tab session ID, if feature is being used with a custom tab.
 * @property runtime the [WebExtensionRuntime] (e.g the browser engine) to use.
 * @property store a reference to the application's [BrowserStore].
 * @property accountManager a reference to application's [FxaAccountManager].
 * @property fxaCapabilities a set of [FxaCapability] that client supports.
 * @property onCommandExecuted an optional callback to know when a command has been executed.
 */
class FxaWebChannelFeature(
    private val customTabSessionId: String?,
    private val runtime: WebExtensionRuntime,
    private val store: BrowserStore,
    private val accountManager: FxaAccountManager,
    private val serverConfig: ServerConfig,
    private val fxaCapabilities: Set<FxaCapability> = emptySet(),
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
    private val onCommandExecuted: (WebChannelCommand) -> Unit = {},
) : LifecycleAwareFeature {

    private var scope: CoroutineScope? = null

    @VisibleForTesting
    // This is an internal var to make it mutable for unit testing purposes only
    internal var extensionController =
        BuiltInWebExtensionController(
            WEB_CHANNEL_EXTENSION_ID,
            WEB_CHANNEL_EXTENSION_URL,
            WEB_CHANNEL_MESSAGING_ID,
        )

    override fun start() {
        val messageHandler = WebChannelViewBackgroundMessageHandler(serverConfig)
        extensionController.registerBackgroundMessageHandler(messageHandler, WEB_CHANNEL_BACKGROUND_MESSAGING_ID)

        extensionController.install(runtime)

        scope =
            store.flowScoped(dispatcher = mainDispatcher) { flow ->
                flow
                    .mapNotNull { state -> state.findCustomTabOrSelectedTab(customTabSessionId) }
                    .distinctUntilChangedBy { it.engineState.engineSession }
                    .collect {
                        it.engineState.engineSession?.let { engineSession ->
                            registerFxaContentMessageHandler(engineSession)
                        }
                    }
            }
    }

    override fun stop() {
        scope?.cancel()
    }

    /**
     * Communication channel is established from fxa-web-content to this class via webextension, as follows:
     * [fxa-web-content] <--js events--> [fxawebchannel.js webextension] <--port messages--> [FxaWebChannelFeature]
     *
     * Overall message flow, as implemented by this class, is documented below. For detailed message descriptions, see:
     * https://github.com/mozilla/fxa/blob/master/packages/fxa-content-server/docs/relier-communication-protocols/fx-webchannel.md
     *
     * ```
     * [fxa-web-channel]            [FxaWebChannelFeature]         Notes:
     *     loaded           ------>          |                  fxa web content loaded
     *     fxa-status       ------>          |                  web content requests account status & device capabilities
     *        |             <------ fxa-status-response         this class responds, based on state of [accountManager]
     *     can-link-account ------>          |                  user submitted credentials, web content verifying if account linking is allowed
     *        |             <------ can-link-account-response   this class responds, based on state of [accountManager]
     *     oauth-login      ------>                             authentication completed within fxa web content, this class receives OAuth code & state
     *     pair-oauth-start ------>          |                  web content, pairing as a supplicant, asks us to start an OAuth flow
     *        |             <------ pair-oauth-start-response   this class responds with the OAuth parameters for the authority
     * ```
     */
    @Suppress("MaxLineLength")
    private class WebChannelViewContentMessageHandler(
        private val accountManager: FxaAccountManager,
        private val serverConfig: ServerConfig,
        private val fxaCapabilities: Set<FxaCapability>,
        private val onCommandExecuted: (WebChannelCommand) -> Unit,
    ) : MessageHandler {
        override fun onPortMessage(message: Any, port: Port) {
            if (!isCommunicationAllowed(serverConfig, port)) {
                logger.error("Communication disallowed, ignoring WebChannel message.")
                return
            }

            val json =
                try {
                    message as JSONObject
                } catch (e: ClassCastException) {
                    logger.error("Received an invalid WebChannel message of type: ${message.javaClass}")
                    // TODO ideally, this should log to Sentry
                    return
                }

            val payload: JSONObject
            val command: WebChannelCommand?
            val rawCommand: String?
            val messageId: String

            try {
                payload = json.getJSONObject("message")
                rawCommand = payload.getString("command")
                command = rawCommand.toWebChannelCommand()
                messageId = payload.optString("messageId", "")
            } catch (e: JSONException) {
                // We don't have control over what messages we will get from the webchannel.
                // If somehow we're receiving mis-constructed messages, it's probably best to not
                // blow up the host application. This comes at a cost: we might not catch problems
                // as quickly if we're not crashing (and thus receiving crash logs).
                // TODO ideally, this should log to Sentry.
                logger.error("Error while processing WebChannel command", e)
                return
            }

            logger.debug("Processing WebChannel command: $rawCommand")

            val response =
                when (command) {
                    WebChannelCommand.CAN_LINK_ACCOUNT ->
                        processCanLinkAccountCommand(accountManager, messageId, payload)
                    WebChannelCommand.FXA_STATUS -> processFxaStatusCommand(accountManager, messageId, fxaCapabilities)
                    WebChannelCommand.OAUTH_LOGIN -> processOauthLoginCommand(accountManager, payload)
                    WebChannelCommand.LOGIN -> processLoginCommand(accountManager, payload)
                    WebChannelCommand.CHANGE_PASSWORD -> processChangePasswordCommand(accountManager, payload)
                    WebChannelCommand.SYNC_PREFERENCES -> processSyncPreferencesCommand(accountManager)
                    WebChannelCommand.PAIR_OAUTH_START ->
                        processPairOAuthStartCommand(accountManager, fxaCapabilities, messageId, port)
                    WebChannelCommand.LOGOUT,
                    WebChannelCommand.DELETE_ACCOUNT -> processLogoutCommand(accountManager)
                    else -> processUnknownCommand(rawCommand)
                }
            response?.let { port.postMessage(it) }

            // Finally, let any consumer be aware of the action to update any UI affordances
            // for ONLY processed messages since we can receive unsupported ones too and we
            // shouldn't forward that onwards.
            command?.let {
                onCommandExecuted(command)
            }
        }
    }

    private fun registerFxaContentMessageHandler(engineSession: EngineSession) {
        val messageHandler =
            WebChannelViewContentMessageHandler(
                accountManager,
                serverConfig,
                fxaCapabilities,
                onCommandExecuted,
            )
        extensionController.registerContentMessageHandler(engineSession, messageHandler)
    }

    private class WebChannelViewBackgroundMessageHandler(private val serverConfig: ServerConfig) : MessageHandler {
        override fun onPortConnected(port: Port) {
            if (serverConfig.server.isCustom()) {
                port.postMessage(
                    JSONObject().put("type", "overrideFxAServer").put("url", serverConfig.server.contentUrl())
                )
            }
        }
    }

    companion object {
        private val logger = Logger("mozac-fxawebchannel")

        @VisibleForTesting internal const val WEB_CHANNEL_EXTENSION_ID = "fxa@mozac.org"

        @VisibleForTesting internal const val WEB_CHANNEL_MESSAGING_ID = "mozacWebchannel"

        @VisibleForTesting internal const val WEB_CHANNEL_BACKGROUND_MESSAGING_ID = "mozacWebchannelBackground"

        @VisibleForTesting
        internal const val WEB_CHANNEL_EXTENSION_URL = "resource://android/assets/extensions/fxawebchannel/"

        // Constants for incoming messages from the WebExtension.
        private const val CHANNEL_ID = "account_updates"

        enum class WebChannelCommand {
            CAN_LINK_ACCOUNT,
            LOGIN,
            OAUTH_LOGIN,
            CHANGE_PASSWORD,
            SYNC_PREFERENCES,
            FXA_STATUS,
            LOGOUT,
            DELETE_ACCOUNT,
            PAIR_OAUTH_START,
        }

        // Scopes requested when starting a pairing OAuth flow. Unlike Desktop we also request the
        // session scope: a paired client never receives a `fxaccounts:login` message, so the code
        // exchange at the end of the flow is its only chance to obtain a session token.
        private val PAIRING_OAUTH_SCOPES = setOf(SCOPE_PROFILE, SCOPE_SYNC, SCOPE_SESSION)

        // The OAuth parameters FxA needs from us to talk to the pairing authority on our behalf.
        private val PAIRING_OAUTH_PARAMS =
            listOf("state", "scope", "code_challenge", "code_challenge_method", "keys_jwk")

        // For all possible messages and their meaning/payloads, see:
        // https://github.com/mozilla/fxa/blob/master/packages/fxa-content-server/docs/relier-communication-protocols/fx-webchannel.md

        /**
         * Gets triggered when user initiates a login within FxA web content. Expects a response. On Fx Desktop, this
         * event triggers "a different user was previously signed in on this machine" warning.
         */
        private const val COMMAND_CAN_LINK_ACCOUNT = "fxaccounts:can_link_account"

        /** Gets triggered when a user successfully authenticates via OAuth. */
        private const val COMMAND_OAUTH_LOGIN = "fxaccounts:oauth_login"

        /**
         * Gets triggered on startup to fetch the FxA state from the host application. Expects a response, which
         * includes application's capabilities and a description of the current Firefox Account (if present).
         */
        private const val COMMAND_STATUS = "fxaccounts:fxa_status"

        /**
         * Gets triggered when the web content is signed in/up, but not necessarily verified it passes in its payload
         * the session token the web content is holding on to
         */
        private const val COMMAND_LOGIN = "fxaccounts:login"

        /**
         * Triggered when the web content has changed the password for the signed-in account. The payload carries the
         * new session token.
         */
        private const val COMMAND_CHANGE_PASSWORD = "fxaccounts:change_password"

        /**
         * Gets triggered when the web content signals to open sync preferences, typically right after a
         * sign-in/sign-up.
         */
        private const val COMMAND_SYNC_PREFERENCES = "fxaccounts:sync_preferences"

        /** Triggered when web content logs out of the account. */
        private const val COMMAND_LOGOUT = "fxaccounts:logout"

        /** Triggered when web content notifies a delete account request. */
        private const val COMMAND_DELETE_ACCOUNT = "fxaccounts:delete"

        /**
         * Asks us, as a pairing supplicant, to start an OAuth flow. Expects a response carrying the OAuth parameters
         * FxA relays to the authority over the pairing channel.
         */
        private const val COMMAND_PAIR_OAUTH_START = "fxaccounts:pair_oauth_start"

        /**
         * Handles the [COMMAND_CAN_LINK_ACCOUNT] event from the web-channel. On Fx Desktop, this event prompts a
         * possible "another user was previously logged in on this device" warning. Currently we don't persist that info
         * so can't support that, so always say it's OK to link when no one is signed in. However, when a profile is
         * already signed in we do check that the account being linked matches, which is an important safety-valve for
         * things like re-authenticating or authorizing new scopes.
         */
        private fun processCanLinkAccountCommand(
            accountManager: FxaAccountManager,
            messageId: String,
            payload: JSONObject,
        ): JSONObject {
            // In 'data' we currently have 'email', but hopefully soon FxA will also send `uid`.
            // If we have `uid` that's the only thing we check as emails might change.
            val profile = accountManager.accountProfile()
            val ok =
                if (profile == null) {
                    true
                } else {
                    val data = payload.optJSONObject("data")
                    val uid = data?.optString("uid")
                    if (!uid.isNullOrEmpty()) {
                        uid == profile.uid
                    } else {
                        data?.optString("email") == profile.email
                    }
                }
            if (!ok) {
                logger.error("Signed in user doesn't match new user, rejecting login")
            }
            return JSONObject().also { status ->
                status.put("id", CHANNEL_ID)
                status.put(
                    "message",
                    JSONObject().also { message ->
                        message.put("messageId", messageId)
                        message.put("command", COMMAND_CAN_LINK_ACCOUNT)
                        message.put(
                            "data",
                            JSONObject().also { data ->
                                data.put("ok", ok)
                            },
                        )
                    },
                )
            }
        }

        /**
         * Handles the [COMMAND_STATUS] event from the web-channel. Responds with supported application capabilities and
         * information about currently signed-in Firefox Account.
         */
        private fun processFxaStatusCommand(
            accountManager: FxaAccountManager,
            messageId: String,
            fxaCapabilities: Set<FxaCapability>,
        ): JSONObject {
            val status = JSONObject()
            status.put("id", CHANNEL_ID)
            status.put(
                "message",
                JSONObject().also { message ->
                    message.put("messageId", messageId)
                    message.put("command", COMMAND_STATUS)
                    message.put(
                        "data",
                        JSONObject().also { data ->
                            data.put(
                                "capabilities",
                                JSONObject().also { capabilities ->
                                    capabilities.put(
                                        "engines",
                                        JSONArray().also { engines ->
                                            accountManager.supportedSyncEngines()?.forEach { engine ->
                                                engines.put(engine.nativeName)
                                            } ?: emptyArray<SyncEngine>()
                                        },
                                    )

                                    if (fxaCapabilities.contains(FxaCapability.CHOOSE_WHAT_TO_SYNC)) {
                                        capabilities.put("choose_what_to_sync", true)
                                    }
                                    // Note that we don't report `pairing`: we support the pairing
                                    // OAuth commands, but we have no pairing authority UI.
                                    if (fxaCapabilities.contains(FxaCapability.PAIRING_V2)) {
                                        capabilities.put("pairingVersion", 2)
                                    }
                                    // we can check for uid in canLinkAccount
                                    capabilities.put("can_link_account_uid", true)
                                },
                            )
                            val account = accountManager.authenticatedAccount()
                            val signedInUserJson = account?.getSignedInUserForWebChannel()
                            if (signedInUserJson == null) {
                                data.put("signedInUser", JSONObject.NULL)
                            } else {
                                data.put("signedInUser", JSONObject(signedInUserJson))
                            }
                        },
                    )
                },
            )
            return status
        }

        private fun JSONArray.toStringList(): List<String> {
            val result = mutableListOf<String>()
            for (i in 0 until this.length()) {
                this.optString(i, null)?.let { result.add(it) }
            }
            return result
        }

        /** Handles the [COMMAND_LOGIN] event from the web-channel */
        private fun processLoginCommand(accountManager: FxaAccountManager, payload: JSONObject): JSONObject? {
            val dataJson: String
            try {
                dataJson = payload.getJSONObject("data").toString()
            } catch (e: JSONException) {
                logger.error("Error while processing WebChannel login command", e)
                return null
            }
            CoroutineScope(Dispatchers.Main).launch {
                accountManager.handleWebChannelLogin(dataJson)
            }
            return null
        }

        /** Handles the [COMMAND_CHANGE_PASSWORD] event from the web-channel. */
        private fun processChangePasswordCommand(
            accountManager: FxaAccountManager,
            payload: JSONObject,
        ): JSONObject? {
            val dataJson: String
            try {
                dataJson = payload.getJSONObject("data").toString()
            } catch (e: JSONException) {
                logger.error("Error while processing WebChannel change_password command", e)
                return null
            }
            CoroutineScope(Dispatchers.Main).launch {
                accountManager.handleWebChannelPasswordChange(dataJson)
            }
            return null
        }

        /** Handles the [COMMAND_OAUTH_LOGIN] event from the web-channel. */
        private fun processOauthLoginCommand(accountManager: FxaAccountManager, payload: JSONObject): JSONObject? {
            val authType: AuthType
            val code: String
            val state: String
            val declinedEngines: List<String>?

            try {
                val data = payload.getJSONObject("data")
                authType = data.getString("action").toAuthType()
                code = data.getString("code")
                state = data.getString("state")
                declinedEngines = data.optJSONArray("declinedSyncEngines")?.toStringList()
            } catch (e: JSONException) {
                // TODO ideally, this should log to Sentry.
                logger.error("Error while processing WebChannel oauth-login command", e)
                return null
            }

            CoroutineScope(Dispatchers.Main).launch {
                accountManager.finishAuthentication(
                    FxaAuthData(
                        authType = authType,
                        code = code,
                        state = state,
                        declinedEngines = declinedEngines?.toSyncEngines(),
                    )
                )
            }

            return null
        }

        /**
         * Handles the [COMMAND_PAIR_OAUTH_START] event from the web-channel, where we act as the pairing supplicant.
         * Starts an OAuth flow through the account manager - so that the follow-up [COMMAND_OAUTH_LOGIN] can complete
         * it - and replies with the OAuth parameters FxA relays to the authority.
         *
         * Returns a response for the synchronous rejection paths, and `null` when the reply will be posted from the
         * coroutine below.
         */
        private fun processPairOAuthStartCommand(
            accountManager: FxaAccountManager,
            fxaCapabilities: Set<FxaCapability>,
            messageId: String,
            port: Port,
        ): JSONObject? {
            ensurePairingEnabled(fxaCapabilities, COMMAND_PAIR_OAUTH_START, messageId)?.let {
                return it
            }
            // Starting a flow while connected would leave us in `FxaState.Authenticating`, which
            // pauses syncing until the flow completes. Add warning to raise awareness. Typically,
            // FxA would not enter a state like this.
            if (accountManager.connectedAccount() != null) {
                logger.warn("Warning! Account already in connected state and PairOAuthStart was requested!")
            }

            CoroutineScope(Dispatchers.Main).launch {
                // Note that we ignore any scopes the web content asked for: the session scope we
                // need isn't something the content knows to request.
                val authUrl =
                    accountManager.beginAuthentication(
                        entrypoint = PairingWebChannelEntryPoint,
                        authScopes = PAIRING_OAUTH_SCOPES,
                    )
                val params = authUrl?.let { pairingOAuthParams(it) }
                port.postMessage(
                    if (params == null) {
                        errorResponse(
                            COMMAND_PAIR_OAUTH_START,
                            messageId,
                            "Failed to begin a pairing OAuth flow",
                        )
                    } else {
                        webChannelResponse(COMMAND_PAIR_OAUTH_START, messageId, params)
                    }
                )
            }

            return null
        }

        /** Extracts the subset of OAuth parameters FxA needs from an authentication url. */
        private fun pairingOAuthParams(authUrl: String): JSONObject? {
            val uri = authUrl.toUri()
            val params = JSONObject()

            for (name in PAIRING_OAUTH_PARAMS) {
                val value = uri.getQueryParameter(name)
                if (value == null) {
                    logger.error("Pairing OAuth url is missing the $name parameter")
                    return null
                }
                params.put(name, value)
            }

            return params
        }

        /** Returns an error response when the pairing commands aren't enabled, and `null` otherwise. */
        private fun ensurePairingEnabled(
            fxaCapabilities: Set<FxaCapability>,
            command: String,
            messageId: String,
        ): JSONObject? {
            return if (fxaCapabilities.contains(FxaCapability.PAIRING_V2)) {
                null
            } else {
                errorResponse(command, messageId, "Pairing is disabled for command: $command")
            }
        }

        private fun webChannelResponse(command: String, messageId: String, data: JSONObject): JSONObject {
            return JSONObject().apply {
                put("id", CHANNEL_ID)
                put(
                    "message",
                    JSONObject().apply {
                        put("messageId", messageId)
                        put("command", command)
                        put("data", data)
                    },
                )
            }
        }

        private fun errorResponse(command: String, messageId: String, message: String): JSONObject {
            logger.error("Failed to handle WebChannel command $command: $message")
            return webChannelResponse(
                command,
                messageId,
                JSONObject().put("error", JSONObject().put("message", message)),
            )
        }

        /**
         * Handles unknown message types by responding with a `data` json that only contains an `error` error message.
         */
        private fun processUnknownCommand(command: String): JSONObject {
            return JSONObject().apply {
                put("id", CHANNEL_ID)
                put(
                    "message",
                    JSONObject().apply {
                        put(
                            "data",
                            JSONObject()
                                .put(
                                    "error",
                                    "Unrecognized FxAccountsWebChannel command: $command",
                                ),
                        )
                    },
                )
            }
        }

        /**
         * Handles the [COMMAND_SYNC_PREFERENCES] event from the web-channel. The UI affordances will be handled by
         * [FxaWebChannelFeature.onCommandExecuted].
         */
        private fun processSyncPreferencesCommand(accountManager: FxaAccountManager): JSONObject {
            // If we don't have an authenticated account, we should let FxA know that we couldn't
            // handle the message.
            val canHandle = accountManager.authenticatedAccount() != null
            return JSONObject().apply {
                put("id", CHANNEL_ID)
                put(
                    "message",
                    JSONObject().apply {
                        put(
                            "data",
                            JSONObject().put("ok", canHandle),
                        )
                    },
                )
            }
        }

        /** Handles the [COMMAND_LOGOUT] and [COMMAND_DELETE_ACCOUNT] event from the web-channel. */
        private fun processLogoutCommand(accountManager: FxaAccountManager): JSONObject? {
            CoroutineScope(Dispatchers.Main).launch {
                accountManager.logout()
            }
            return null
        }

        private fun String.toWebChannelCommand(): WebChannelCommand? {
            return when (this) {
                COMMAND_CAN_LINK_ACCOUNT -> WebChannelCommand.CAN_LINK_ACCOUNT
                COMMAND_OAUTH_LOGIN -> WebChannelCommand.OAUTH_LOGIN
                COMMAND_STATUS -> WebChannelCommand.FXA_STATUS
                COMMAND_LOGIN -> WebChannelCommand.LOGIN
                COMMAND_CHANGE_PASSWORD -> WebChannelCommand.CHANGE_PASSWORD
                COMMAND_SYNC_PREFERENCES -> WebChannelCommand.SYNC_PREFERENCES
                COMMAND_LOGOUT -> WebChannelCommand.LOGOUT
                COMMAND_DELETE_ACCOUNT -> WebChannelCommand.DELETE_ACCOUNT
                COMMAND_PAIR_OAUTH_START -> WebChannelCommand.PAIR_OAUTH_START
                else -> {
                    logger.warn("Unrecognized FxAccountsWebChannel command: $this")
                    null
                }
            }
        }

        private fun isCommunicationAllowed(serverConfig: ServerConfig, port: Port): Boolean {
            val senderOrigin = port.senderUrl()
            val expectedOrigin = serverConfig.server.contentUrl()
            return isCommunicationAllowed(senderOrigin, expectedOrigin)
        }

        @VisibleForTesting
        internal fun isCommunicationAllowed(senderOrigin: String, expectedOrigin: String): Boolean {
            if (!isSafeUrl(senderOrigin)) {
                logger.error("$senderOrigin looks unsafe, aborting.")
                return false
            }

            if (!senderOrigin.isSameOriginAs(expectedOrigin)) {
                logger.error("Host mismatch for WebChannel message. Expected: $expectedOrigin, got: $senderOrigin.")
                return false
            }
            return true
        }

        /** Rejects URLs that are deemed "unsafe" (not expected). */
        private fun isSafeUrl(urlStr: String): Boolean {
            val url = URL(urlStr)
            return url.userInfo.isNullOrEmpty() &&
                (url.protocol == "https" || url.host == "localhost" || url.host == "127.0.0.1")
        }
    }
}
