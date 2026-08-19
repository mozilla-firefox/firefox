/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.accounts

import android.os.Looper.getMainLooper
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.test.runTest
import mozilla.appservices.fxaclient.FxaServer
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.webextension.MessageHandler
import mozilla.components.concept.engine.webextension.Port
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.FxAEntryPoint
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.concept.sync.Profile
import mozilla.components.concept.sync.SyncEngine
import mozilla.components.service.fxa.FxaAuthData
import mozilla.components.service.fxa.ServerConfig
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.eq
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import mozilla.components.support.webextensions.BuiltInWebExtensionController
import org.json.JSONException
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.clearInvocations
import org.mockito.Mockito.never
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.robolectric.Shadows.shadowOf

private const val PAIRING_AUTH_URL =
    "https://foo.bar/authorization?client_id=abc&" +
        "scope=profile%20https%3A%2F%2Fidentity.mozilla.com%2Fapps%2Foldsync&state=st8&" +
        "code_challenge_method=S256&code_challenge=chal8&access_type=offline&keys_jwk=jwk8"

@RunWith(AndroidJUnit4::class)
class FxaWebChannelFeatureTest {

    @Before
    fun setup() {
        BuiltInWebExtensionController.installedBuiltInExtensions.clear()
    }

    @Test
    fun `start installs webextension`() {
        val engine: Engine = mock()
        val store = BrowserStore()
        val accountManager: FxaAccountManager = mock()
        val serverConfig: ServerConfig = mock()
        val webchannelFeature = FxaWebChannelFeature(null, engine, store, accountManager, serverConfig)
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        val onSuccess = argumentCaptor<((WebExtension) -> Unit)>()
        val onError = argumentCaptor<((Throwable) -> Unit)>()
        verify(engine, times(1))
            .installBuiltInWebExtension(
                eq(FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_ID),
                eq(FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_URL),
                onSuccess.capture(),
                onError.capture(),
            )

        onSuccess.value.invoke(mock())

        // Already installed, should not try to install again.
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()
        verify(engine, times(1))
            .installBuiltInWebExtension(
                eq(FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_ID),
                eq(FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_URL),
                any(),
                any(),
            )
    }

    @Test
    fun `start registers the background message handler`() {
        val engine: Engine = mock()
        val store = BrowserStore()
        val accountManager: FxaAccountManager = mock()
        val serverConfig: ServerConfig = mock()
        val controller: BuiltInWebExtensionController = mock()
        val webchannelFeature = FxaWebChannelFeature(null, engine, store, accountManager, serverConfig)

        webchannelFeature.extensionController = controller

        webchannelFeature.start()

        verify(controller).registerBackgroundMessageHandler(any(), any())
    }

    @Test
    fun `backgroundMessageHandler sends overrideFxAServer`() {
        val engine: Engine = mock()
        val store = BrowserStore()
        val accountManager: FxaAccountManager = mock()
        val serverConfig: ServerConfig = mock()
        val controller: BuiltInWebExtensionController = mock()
        val webchannelFeature = FxaWebChannelFeature(null, engine, store, accountManager, serverConfig)

        whenever(serverConfig.server).thenReturn(FxaServer.Custom("https://foo.bar"))
        webchannelFeature.extensionController = controller

        webchannelFeature.start()

        val messageHandler = argumentCaptor<MessageHandler>()
        verify(controller).registerBackgroundMessageHandler(messageHandler.capture(), any())

        val port: Port = mock()
        val message = argumentCaptor<JSONObject>()
        messageHandler.value.onPortConnected(port)
        verify(port).postMessage(message.capture())

        val overrideUrlMessage = JSONObject().put("type", "overrideFxAServer").put("url", "https://foo.bar")
        verify(port, times(1)).postMessage(message.capture())

        assertEquals(overrideUrlMessage.toString(), message.value.toString())
    }

    @Test
    fun `backgroundMessageHandler should not send overrideFxAServer for predefined Config`() {
        val engine: Engine = mock()
        val store = BrowserStore()
        val accountManager: FxaAccountManager = mock()
        val serverConfig: ServerConfig = mock()
        val controller: BuiltInWebExtensionController = mock()
        val webchannelFeature = FxaWebChannelFeature(null, engine, store, accountManager, serverConfig)

        whenever(serverConfig.server).thenReturn(FxaServer.Release)
        webchannelFeature.extensionController = controller

        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        val messageHandler = argumentCaptor<MessageHandler>()
        verify(controller).registerBackgroundMessageHandler(messageHandler.capture(), any())

        val port: Port = mock()
        messageHandler.value.onPortConnected(port)

        verify(port, never()).postMessage(any())
    }

    @Test
    fun `start registers content message handler for selected session`() {
        val engine: Engine = mock()
        val engineSession: EngineSession = mock()
        val accountManager: FxaAccountManager = mock()
        val serverConfig: ServerConfig = mock()
        val controller: BuiltInWebExtensionController = mock()

        val tab = createTab("https://www.mozilla.org", id = "test-tab", engineSession = engineSession)
        val store = BrowserStore(initialState = BrowserState(tabs = listOf(tab), selectedTabId = tab.id))

        val webchannelFeature = FxaWebChannelFeature(null, engine, store, accountManager, serverConfig)
        webchannelFeature.extensionController = controller

        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(controller).registerContentMessageHandler(eq(engineSession), any(), any())
    }

    @Test
    fun `Ignores messages coming from a different FxA host than configured`() {
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val expectedEngines: Set<SyncEngine> = setOf(SyncEngine.History)
        val messageHandler = argumentCaptor<MessageHandler>()
        val webchannelFeature = prepareFeatureForTest(ext, port, engineSession, expectedEngines)
        whenever(port.senderUrl()).thenReturn("https://bar.foo/email")
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        val requestFromTheWebChannel =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:fxa_status",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )

        messageHandler.value.onPortMessage(requestFromTheWebChannel, port)
        verify(port, never()).postMessage(any())
    }

    @Test
    fun `COMMAND_STATUS configured with CWTS must provide a boolean=true flag to the web-channel`() {
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val expectedEngines: Set<SyncEngine> = setOf(SyncEngine.History)
        val messageHandler = argumentCaptor<MessageHandler>()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()
        val webchannelFeature =
            prepareFeatureForTest(
                ext,
                port,
                engineSession,
                expectedEngines,
                setOf(FxaCapability.CHOOSE_WHAT_TO_SYNC),
            )
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        val requestFromTheWebChannel =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:fxa_status",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )
        messageHandler.value.onPortMessage(requestFromTheWebChannel, port)
        verify(port).postMessage(responseToTheWebChannel.capture())
        assertTrue(responseToTheWebChannel.value.getCWTSSupport()!!)
    }

    // Receiving and responding a fxa-status message if sync is configured with one engine
    @Test
    fun `COMMAND_STATUS configured with one engine must be provided to the web-channel`() {
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val expectedEngines: Set<SyncEngine> = setOf(SyncEngine.History)
        val messageHandler = argumentCaptor<MessageHandler>()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()
        val webchannelFeature = prepareFeatureForTest(ext, port, engineSession, expectedEngines)
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        val requestFromTheWebChannel =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:fxa_status",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )

        messageHandler.value.onPortMessage(requestFromTheWebChannel, port)
        verify(port).postMessage(responseToTheWebChannel.capture())

        val capabilitiesFromWebChannel = responseToTheWebChannel.value.getSupportedEngines()
        assertTrue(capabilitiesFromWebChannel.size == 1)
        assertNull(responseToTheWebChannel.value.getCWTSSupport())

        assertTrue(responseToTheWebChannel.value.isSignedInUserNull())
    }

    // Receiving and responding a fxa-status message if sync is configured with more than one engine
    @Test
    fun `COMMAND_STATUS configured with more than one engine must be provided to the web-channel`() {
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val expectedEngines: Set<SyncEngine> = setOf(SyncEngine.History)
        val messageHandler = argumentCaptor<MessageHandler>()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()
        val webchannelFeature = prepareFeatureForTest(ext, port, engineSession, expectedEngines)
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        val requestFromTheWebChannel =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:fxa_status",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )

        messageHandler.value.onPortMessage(requestFromTheWebChannel, port)
        verify(port).postMessage(responseToTheWebChannel.capture())

        val capabilitiesFromWebChannel = responseToTheWebChannel.value.getSupportedEngines()
        assertTrue(
            expectedEngines.all {
                capabilitiesFromWebChannel.contains(it.nativeName)
            }
        )

        assertNull(responseToTheWebChannel.value.getCWTSSupport())
        assertTrue(responseToTheWebChannel.value.isSignedInUserNull())
    }

    // Receiving and responding a fxa-status message if account manager is logged in
    @Test
    fun `COMMAND_STATUS with account manager is logged in with profile`() {
        val accountManager: FxaAccountManager = mock()
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val expectedEngines: Set<SyncEngine> = setOf(SyncEngine.History)
        val messageHandler = argumentCaptor<MessageHandler>()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()

        val account: OAuthAccount = mock()
        val profile = Profile(uid = "testUID", email = "test@example.com", avatar = null, displayName = null)
        whenever(account.getSignedInUserForWebChannel())
            .thenReturn("""{"sessionToken":"testToken","email":"test@example.com","uid":"testUID","verified":true}""")
        whenever(accountManager.accountProfile()).thenReturn(profile)
        whenever(accountManager.authenticatedAccount()).thenReturn(account)
        whenever(accountManager.supportedSyncEngines()).thenReturn(expectedEngines)

        val webchannelFeature =
            prepareFeatureForTest(ext, port, engineSession, expectedEngines, emptySet(), accountManager)
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        val requestFromTheWebChannel =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:fxa_status",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )

        messageHandler.value.onPortMessage(requestFromTheWebChannel, port)
        verify(port).postMessage(responseToTheWebChannel.capture())

        val capabilitiesFromWebChannel = responseToTheWebChannel.value.getSupportedEngines()
        assertTrue(
            expectedEngines.all {
                capabilitiesFromWebChannel.contains(it.nativeName)
            }
        )

        assertNull(responseToTheWebChannel.value.getCWTSSupport())

        val signedInUser = responseToTheWebChannel.value.signedInUser()
        assertEquals("test@example.com", signedInUser.email)
        assertEquals("testUID", signedInUser.uid)
        assertTrue(signedInUser.verified)
        assertEquals("testToken", signedInUser.sessionToken)
    }

    @Test
    fun `COMMAND_STATUS with account manager is logged in without profile`() {
        val accountManager: FxaAccountManager = mock()
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val expectedEngines: Set<SyncEngine> = setOf(SyncEngine.History)
        val messageHandler = argumentCaptor<MessageHandler>()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()

        val account: OAuthAccount = mock()
        whenever(account.getSignedInUserForWebChannel())
            .thenReturn("""{"sessionToken":"testToken","email":null,"uid":null,"verified":true}""")
        whenever(accountManager.accountProfile()).thenReturn(null)
        whenever(accountManager.authenticatedAccount()).thenReturn(account)
        whenever(accountManager.supportedSyncEngines()).thenReturn(expectedEngines)

        val webchannelFeature =
            prepareFeatureForTest(ext, port, engineSession, expectedEngines, emptySet(), accountManager)
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        val requestFromTheWebChannel =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:fxa_status",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )

        messageHandler.value.onPortMessage(requestFromTheWebChannel, port)
        verify(port).postMessage(responseToTheWebChannel.capture())

        val capabilitiesFromWebChannel = responseToTheWebChannel.value.getSupportedEngines()
        assertTrue(
            expectedEngines.all {
                capabilitiesFromWebChannel.contains(it.nativeName)
            }
        )

        assertNull(responseToTheWebChannel.value.getCWTSSupport())

        val signedInUser = responseToTheWebChannel.value.signedInUser()
        assertNull(signedInUser.email)
        assertNull(signedInUser.uid)
        assertTrue(signedInUser.verified)
        assertEquals("testToken", signedInUser.sessionToken)
    }

    // Receiving and responding a fxa-status message if account manager is logged out
    @Test
    fun `COMMAND_STATUS with account manager is logged out`() {
        val accountManager: FxaAccountManager = mock()
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val expectedEngines = setOf(SyncEngine.History, SyncEngine.Bookmarks, SyncEngine.Passwords)
        val messageHandler = argumentCaptor<MessageHandler>()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()

        whenever(accountManager.accountProfile()).thenReturn(null)
        whenever(accountManager.supportedSyncEngines()).thenReturn(expectedEngines)
        BuiltInWebExtensionController.installedBuiltInExtensions[FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_ID] = ext

        val webchannelFeature =
            prepareFeatureForTest(ext, port, engineSession, expectedEngines, emptySet(), accountManager)
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        val requestFromTheWebChannel =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:fxa_status",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )

        messageHandler.value.onPortMessage(requestFromTheWebChannel, port)
        verify(port).postMessage(responseToTheWebChannel.capture())

        val capabilitiesFromWebChannel = responseToTheWebChannel.value.getSupportedEngines()
        assertTrue(
            expectedEngines.all {
                capabilitiesFromWebChannel.contains(it.nativeName)
            }
        )

        assertNull(responseToTheWebChannel.value.getCWTSSupport())
        assertTrue(responseToTheWebChannel.value.isSignedInUserNull())
    }

    // Receiving and responding a fxa-status message if account manager when sync is not configured
    @Test
    fun `COMMAND_STATUS with account manager when sync is not configured`() {
        val accountManager: FxaAccountManager = mock() // syncConfig is null by default (is not configured)
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val expectedEngines = setOf(SyncEngine.History, SyncEngine.Bookmarks, SyncEngine.Passwords)
        val messageHandler = argumentCaptor<MessageHandler>()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()

        BuiltInWebExtensionController.installedBuiltInExtensions[FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_ID] = ext

        val webchannelFeature =
            prepareFeatureForTest(ext, port, engineSession, expectedEngines, emptySet(), accountManager)
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        val requestFromTheWebChannel =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:fxa_status",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )

        messageHandler.value.onPortMessage(requestFromTheWebChannel, port)
        verify(port).postMessage(responseToTheWebChannel.capture())

        val capabilitiesFromWebChannel = responseToTheWebChannel.value.getSupportedEngines()
        assertTrue(
            expectedEngines.all {
                capabilitiesFromWebChannel.contains(it.nativeName)
            }
        )

        assertNull(responseToTheWebChannel.value.getCWTSSupport())
        assertTrue(responseToTheWebChannel.value.isSignedInUserNull())
    }

    @Test
    fun `COMMAND_STATUS with no capabilities configured must provide an empty list of engines to the web-channel`() {
        val accountManager: FxaAccountManager = mock() // syncConfig is null by default (is not configured)
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val messageHandler = argumentCaptor<MessageHandler>()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()

        whenever(accountManager.supportedSyncEngines()).thenReturn(null)
        BuiltInWebExtensionController.installedBuiltInExtensions[FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_ID] = ext

        val webchannelFeature = prepareFeatureForTest(ext, port, engineSession, null, emptySet(), accountManager)
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        val requestFromTheWebChannel =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:fxa_status",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )

        messageHandler.value.onPortMessage(requestFromTheWebChannel, port)
        verify(port).postMessage(responseToTheWebChannel.capture())

        assertNull(responseToTheWebChannel.value.getCWTSSupport())
        val capabilitiesFromWebChannel = responseToTheWebChannel.value.getSupportedEngines()
        assertTrue(capabilitiesFromWebChannel.isEmpty())
    }

    @Test
    fun `COMMAND_STATUS configured with PAIRING_V2 must report the pairing version to the web-channel`() {
        val port: Port = mock()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()

        val messageHandler =
            startedMessageHandler(
                ext = mock(),
                port = port,
                engineSession = mock(),
                fxaCapabilities = setOf(FxaCapability.PAIRING_V2),
                accountManager = mock(),
            )

        messageHandler.onPortMessage(jsonFxaStatus(), port)
        verify(port).postMessage(responseToTheWebChannel.capture())

        assertEquals(2, responseToTheWebChannel.value.getPairingVersion())
    }

    @Test
    fun `COMMAND_STATUS without PAIRING_V2 must not report the pairing version to the web-channel`() {
        val port: Port = mock()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()

        val messageHandler =
            startedMessageHandler(
                ext = mock(),
                port = port,
                engineSession = mock(),
                fxaCapabilities = setOf(FxaCapability.CHOOSE_WHAT_TO_SYNC),
                accountManager = mock(),
            )

        messageHandler.onPortMessage(jsonFxaStatus(), port)
        verify(port).postMessage(responseToTheWebChannel.capture())

        assertNull(responseToTheWebChannel.value.getPairingVersion())
    }

    @Test
    fun `COMMAND_PAIR_OAUTH_START must respond with the oauth parameters from the auth url`() = runTest {
        val port: Port = mock()
        val accountManager: FxaAccountManager = mock()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()

        whenever(accountManager.beginAuthentication(any(), any(), any(), any())).thenReturn(PAIRING_AUTH_URL)

        val messageHandler =
            startedMessageHandler(
                ext = mock(),
                port = port,
                engineSession = mock(),
                fxaCapabilities = setOf(FxaCapability.PAIRING_V2),
                accountManager = accountManager,
            )

        messageHandler.onPortMessage(jsonPairOAuthStart(), port)
        shadowOf(getMainLooper()).idle()

        verify(port).postMessage(responseToTheWebChannel.capture())
        val response = responseToTheWebChannel.value
        assertEquals("fxaccounts:pair_oauth_start", response.getJSONObject("message").getString("command"))
        assertEquals("123", response.getJSONObject("message").getString("messageId"))

        val data = response.messageData()
        assertEquals("st8", data.getString("state"))
        assertEquals("profile https://identity.mozilla.com/apps/oldsync", data.getString("scope"))
        assertEquals("chal8", data.getString("code_challenge"))
        assertEquals("S256", data.getString("code_challenge_method"))
        assertEquals("jwk8", data.getString("keys_jwk"))
        // Only the parameters FxA needs are exposed.
        assertEquals(5, data.length())
    }

    @Test
    fun `COMMAND_PAIR_OAUTH_START must request the pairing scopes`() = runTest {
        val port: Port = mock()
        val accountManager: FxaAccountManager = mock()
        val scopesCaptor = argumentCaptor<Set<String>>()
        val entrypointCaptor = argumentCaptor<FxAEntryPoint>()

        whenever(accountManager.beginAuthentication(any(), any(), any(), any())).thenReturn(PAIRING_AUTH_URL)

        val messageHandler =
            startedMessageHandler(
                ext = mock(),
                port = port,
                engineSession = mock(),
                fxaCapabilities = setOf(FxaCapability.PAIRING_V2),
                accountManager = accountManager,
            )

        messageHandler.onPortMessage(jsonPairOAuthStart(), port)
        shadowOf(getMainLooper()).idle()

        verify(accountManager)
            .beginAuthentication(
                eq(null),
                entrypointCaptor.capture(),
                scopesCaptor.capture(),
                eq(""),
            )
        assertEquals("webchannel-pairing", entrypointCaptor.value.entryName)
        assertEquals(
            setOf(
                "profile",
                "https://identity.mozilla.com/apps/oldsync",
                "https://identity.mozilla.com/tokens/session",
            ),
            scopesCaptor.value,
        )
    }

    @Test
    fun `COMMAND_PAIR_OAUTH_START must respond with an error when the flow cannot be started`() = runTest {
        val port: Port = mock()
        val accountManager: FxaAccountManager = mock()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()

        whenever(accountManager.beginAuthentication(any(), any(), any(), any())).thenReturn(null)

        val messageHandler =
            startedMessageHandler(
                ext = mock(),
                port = port,
                engineSession = mock(),
                fxaCapabilities = setOf(FxaCapability.PAIRING_V2),
                accountManager = accountManager,
            )

        messageHandler.onPortMessage(jsonPairOAuthStart(), port)
        shadowOf(getMainLooper()).idle()

        verify(port).postMessage(responseToTheWebChannel.capture())
        assertEquals(
            "Failed to begin a pairing OAuth flow",
            responseToTheWebChannel.value.getErrorMessage(),
        )
    }

    @Test
    fun `COMMAND_PAIR_OAUTH_START must respond with an error when the auth url is missing parameters`() = runTest {
        val port: Port = mock()
        val accountManager: FxaAccountManager = mock()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()

        whenever(accountManager.beginAuthentication(any(), any(), any(), any()))
            .thenReturn("https://foo.bar/authorization?client_id=abc&state=st8&scope=profile")

        val messageHandler =
            startedMessageHandler(
                ext = mock(),
                port = port,
                engineSession = mock(),
                fxaCapabilities = setOf(FxaCapability.PAIRING_V2),
                accountManager = accountManager,
            )

        messageHandler.onPortMessage(jsonPairOAuthStart(), port)
        shadowOf(getMainLooper()).idle()

        verify(port).postMessage(responseToTheWebChannel.capture())
        assertEquals(
            "Failed to begin a pairing OAuth flow",
            responseToTheWebChannel.value.getErrorMessage(),
        )
    }

    @Test
    fun `COMMAND_PAIR_OAUTH_START must be rejected without the PAIRING_V2 capability`() = runTest {
        val port: Port = mock()
        val accountManager: FxaAccountManager = mock()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()

        val messageHandler =
            startedMessageHandler(
                ext = mock(),
                port = port,
                engineSession = mock(),
                fxaCapabilities = emptySet(),
                accountManager = accountManager,
            )

        messageHandler.onPortMessage(jsonPairOAuthStart(), port)
        shadowOf(getMainLooper()).idle()

        verify(port).postMessage(responseToTheWebChannel.capture())
        assertEquals(
            "Pairing is disabled for command: fxaccounts:pair_oauth_start",
            responseToTheWebChannel.value.getErrorMessage(),
        )
        verify(accountManager, never()).beginAuthentication(any(), any(), any(), any())
    }

    @Test
    fun `COMMAND_PAIR_OAUTH_START must proceed with a warning when already connected`() = runTest {
        val port: Port = mock()
        val accountManager: FxaAccountManager = mock()
        val responseToTheWebChannel = argumentCaptor<JSONObject>()

        whenever(accountManager.connectedAccount()).thenReturn(mock())
        whenever(accountManager.beginAuthentication(any(), any(), any(), any())).thenReturn(PAIRING_AUTH_URL)

        val messageHandler =
            startedMessageHandler(
                ext = mock(),
                port = port,
                engineSession = mock(),
                fxaCapabilities = setOf(FxaCapability.PAIRING_V2),
                accountManager = accountManager,
            )

        messageHandler.onPortMessage(jsonPairOAuthStart(), port)
        shadowOf(getMainLooper()).idle()

        // Being connected only warrants a warning, so the flow should still start.
        verify(accountManager).beginAuthentication(any(), any(), any(), any())
        verify(port).postMessage(responseToTheWebChannel.capture())
        val response = responseToTheWebChannel.value
        assertEquals("fxaccounts:pair_oauth_start", response.getJSONObject("message").getString("command"))
        assertEquals("st8", response.messageData().getString("state"))
    }

    // Receiving an oauth-login message account manager accepts the request
    @Test
    fun `COMMAND_OAUTH_LOGIN web-channel must be processed through when the accountManager accepts the request`() =
        runTest {
            val accountManager: FxaAccountManager = mock() // syncConfig is null by default (is not configured)
            val engineSession: EngineSession = mock()
            val ext: WebExtension = mock()
            val port: Port = mock()
            val messageHandler = argumentCaptor<MessageHandler>()

            BuiltInWebExtensionController.installedBuiltInExtensions[FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_ID] =
                ext

            val webchannelFeature = prepareFeatureForTest(ext, port, engineSession, null, emptySet(), accountManager)
            webchannelFeature.start()
            shadowOf(getMainLooper()).idle()

            verify(ext)
                .registerContentMessageHandler(
                    eq(engineSession),
                    eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                    messageHandler.capture(),
                )
            messageHandler.value.onPortConnected(port)

            // Action: signin
            verifyOauthLogin("signin", AuthType.Signin, "fffs", "fsdf32", null, messageHandler.value, accountManager)
            // Signup.
            verifyOauthLogin(
                "signup",
                AuthType.Signup,
                "anotherCode1",
                "anotherState2",
                setOf(SyncEngine.Passwords),
                messageHandler.value,
                accountManager,
            )
            // Pairing.
            verifyOauthLogin(
                "pairing",
                AuthType.Pairing,
                "anotherCode2",
                "anotherState3",
                null,
                messageHandler.value,
                accountManager,
            )
            // Some other action.
            verifyOauthLogin(
                "newAction",
                AuthType.OtherExternal("newAction"),
                "anotherCode3",
                "anotherState4",
                null,
                messageHandler.value,
                accountManager,
            )
        }

    // Receiving an oauth-login message account manager refuses the request
    @Test
    fun `COMMAND_OAUTH_LOGIN web-channel must be processed when the accountManager refuses the request`() = runTest {
        val accountManager: FxaAccountManager = mock() // syncConfig is null by default (is not configured)
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val messageHandler = argumentCaptor<MessageHandler>()

        BuiltInWebExtensionController.installedBuiltInExtensions[FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_ID] = ext

        val webchannelFeature = prepareFeatureForTest(ext, port, engineSession, null, emptySet(), accountManager)
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        // Action: signin
        verifyOauthLogin(
            "signin",
            AuthType.Signin,
            "fffs",
            "fsdf32",
            setOf(SyncEngine.Passwords, SyncEngine.Bookmarks),
            messageHandler.value,
            accountManager,
        )
        // Signup.
        verifyOauthLogin(
            "signup",
            AuthType.Signup,
            "anotherCode1",
            "anotherState2",
            null,
            messageHandler.value,
            accountManager,
        )
        // Pairing.
        verifyOauthLogin(
            "pairing",
            AuthType.Pairing,
            "anotherCode2",
            "anotherState3",
            null,
            messageHandler.value,
            accountManager,
        )
        // Some other action.
        verifyOauthLogin(
            "newAction",
            AuthType.OtherExternal("newAction"),
            "anotherCode3",
            "anotherState4",
            null,
            messageHandler.value,
            accountManager,
        )
    }

    // Receiving can-link-account  returns 'ok=true' message (for now)
    @Test
    fun `COMMAND_CAN_LINK_ACCOUNT must provide an OK response to the web-channel`() {
        val accountManager: FxaAccountManager = mock() // syncConfig is null by default (is not configured)
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val jsonFromWebChannel = argumentCaptor<JSONObject>()
        val messageHandler = argumentCaptor<MessageHandler>()
        val expectedEngines = setOf(SyncEngine.History, SyncEngine.Bookmarks)

        whenever(accountManager.supportedSyncEngines()).thenReturn(expectedEngines)
        BuiltInWebExtensionController.installedBuiltInExtensions[FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_ID] = ext

        val webchannelFeature =
            prepareFeatureForTest(ext, port, engineSession, expectedEngines, emptySet(), accountManager)
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        val jsonToWebChannel =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:can_link_account",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )

        messageHandler.value.onPortMessage(jsonToWebChannel, port)
        verify(port).postMessage(jsonFromWebChannel.capture())

        assertTrue(jsonFromWebChannel.value.getOk())
    }

    @Test
    fun `isCommunicationAllowed extensive testing`() {
        // Unsafe URL: not https.
        assertFalse(FxaWebChannelFeature.isCommunicationAllowed("http://foo.bar", "http://foo.bar"))
        // Unsafe URL: login in url.
        assertFalse(FxaWebChannelFeature.isCommunicationAllowed("http://bobo:bobo@foo.bar", "http://foo.bar"))
        // Origin mismatch.
        assertFalse(FxaWebChannelFeature.isCommunicationAllowed("https://foo.bar", "https://foo.baz"))

        // Happy cases
        assertTrue(FxaWebChannelFeature.isCommunicationAllowed("https://foo.bar", "https://foo.bar"))
        // HTTP is allowed for localhost.
        assertTrue(FxaWebChannelFeature.isCommunicationAllowed("http://127.0.0.1", "http://127.0.0.1"))
        assertTrue(FxaWebChannelFeature.isCommunicationAllowed("http://localhost", "http://localhost"))
    }

    @Test
    fun `COMMAND_LOGIN must be processed and sets the user's data`() = runTest {
        val accountManager: FxaAccountManager = mock() // syncConfig is null by default (is not configured)
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val messageHandler = argumentCaptor<MessageHandler>()

        BuiltInWebExtensionController.installedBuiltInExtensions[FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_ID] = ext

        val webchannelFeature = prepareFeatureForTest(ext, port, engineSession, null, emptySet(), accountManager)
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        // Action: signin
        verifyLogin("sessiontoken123", "foo@bar.com", "uid123", false, messageHandler.value, accountManager)
    }

    @Test
    fun `COMMAND_CHANGE_PASSWORD forwards the payload to handleWebChannelPasswordChange`() = runTest {
        val accountManager: FxaAccountManager = mock()
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val messageHandler = argumentCaptor<MessageHandler>()

        BuiltInWebExtensionController.installedBuiltInExtensions[FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_ID] = ext

        val webchannelFeature = prepareFeatureForTest(ext, port, engineSession, null, emptySet(), accountManager)
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        val newSessionToken = "newsessiontoken456"
        val jsonToWebChannel =
            JSONObject(
                """{
             "message":{
                "command": "fxaccounts:change_password",
                "messageId":456,
                "data":{
                    "email":"foo@bar.com",
                    "sessionToken":"$newSessionToken",
                    "uid":"uid123",
                    "verified":true
                }
             }
            }
            """
                    .trimIndent()
            )
        whenever(port.senderUrl()).thenReturn("https://foo.bar/email")
        messageHandler.value.onPortMessage(jsonToWebChannel, port)
        shadowOf(getMainLooper()).idle()

        val dataCaptor = argumentCaptor<String>()
        verify(accountManager).handleWebChannelPasswordChange(dataCaptor.capture())
        assertEquals(
            jsonToWebChannel.getJSONObject("message").getJSONObject("data").toString(),
            dataCaptor.value,
        )
    }

    @Test
    fun `COMMAND_SYNC_PREFERENCES is processed if there is an account`() = runTest {
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val messageHandler = argumentCaptor<MessageHandler>()
        val accountManager: FxaAccountManager = mock {
            whenever(authenticatedAccount()).thenReturn(mock())
        }
        val jsonFromWebChannel = argumentCaptor<JSONObject>()
        val webchannelFeature =
            prepareFeatureForTest(
                ext = ext,
                port = port,
                engineSession = engineSession,
                fxaCapabilities = setOf(FxaCapability.CHOOSE_WHAT_TO_SYNC),
                accountManager = accountManager,
            )
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )

        messageHandler.value.onPortConnected(port)

        val jsonToWebChannelLogout =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:sync_preferences",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )

        messageHandler.value.onPortMessage(jsonToWebChannelLogout, port)
        verify(port).postMessage(jsonFromWebChannel.capture())

        assertTrue(jsonFromWebChannel.value.getOk())
    }

    @Test
    fun `COMMAND_LOGOUT and COMMAND_DELETE_ACCOUNT are processed`() = runTest {
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val expectedEngines: Set<SyncEngine> = setOf(SyncEngine.History)
        val messageHandler = argumentCaptor<MessageHandler>()
        val accountManager: FxaAccountManager = mock()
        val webchannelFeature =
            prepareFeatureForTest(
                ext,
                port,
                engineSession,
                expectedEngines,
                setOf(FxaCapability.CHOOSE_WHAT_TO_SYNC),
                accountManager,
            )
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )

        messageHandler.value.onPortConnected(port)

        val jsonToWebChannelLogout =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:logout",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )

        messageHandler.value.onPortMessage(jsonToWebChannelLogout, port)
        shadowOf(getMainLooper()).idle()

        verify(accountManager).logout()

        clearInvocations(accountManager)
        val jsonToWebChannelDelete =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:delete",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )

        messageHandler.value.onPortMessage(jsonToWebChannelDelete, port)
        shadowOf(getMainLooper()).idle()

        verify(accountManager).logout()
    }

    @Test
    fun `COMMAND_LOGIN invalid json sends back`() = runTest {
        val accountManager: FxaAccountManager = mock() // syncConfig is null by default (is not configured)
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val messageHandler = argumentCaptor<MessageHandler>()

        BuiltInWebExtensionController.installedBuiltInExtensions[FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_ID] = ext

        val webchannelFeature = prepareFeatureForTest(ext, port, engineSession, null, emptySet(), accountManager)
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)

        // Action: signin
        verifyLogin("sessiontoken123", "foo@bar.com", "uid123", false, messageHandler.value, accountManager)
    }

    @Test
    fun `an invalid command responds with an error message`() = runTest {
        val engineSession: EngineSession = mock()
        val ext: WebExtension = mock()
        val port: Port = mock()
        val messageHandler = argumentCaptor<MessageHandler>()
        val accountManager: FxaAccountManager = mock {
            whenever(authenticatedAccount()).thenReturn(mock())
        }
        val jsonFromWebChannel = argumentCaptor<JSONObject>()
        val webchannelFeature =
            prepareFeatureForTest(
                ext = ext,
                port = port,
                engineSession = engineSession,
                fxaCapabilities = setOf(FxaCapability.CHOOSE_WHAT_TO_SYNC),
                accountManager = accountManager,
            )
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )

        messageHandler.value.onPortConnected(port)

        val jsonToWebChannelLogout =
            JSONObject(
                """
                {
                             "message":{
                                "command": "fxaccounts:any_unknown_message",
                                "messageId":123
                             }
                            }
                """
                    .trimIndent()
            )

        messageHandler.value.onPortMessage(jsonToWebChannelLogout, port)
        verify(port).postMessage(jsonFromWebChannel.capture())

        assertTrue(jsonFromWebChannel.value.getError()!!.contains("Unrecognized FxAccountsWebChannel command"))
    }

    private fun JSONObject.getSupportedEngines(): List<String> {
        val engines =
            this.getJSONObject("message").getJSONObject("data").getJSONObject("capabilities").getJSONArray("engines")

        val list = mutableListOf<String>()
        for (i in 0 until engines.length()) {
            list.add(engines[i].toString())
        }
        return list
    }

    private fun JSONObject.getCWTSSupport(): Boolean? {
        return try {
            this.getJSONObject("message")
                .getJSONObject("data")
                .getJSONObject("capabilities")
                .getBoolean("choose_what_to_sync")
        } catch (e: JSONException) {
            null
        }
    }

    private fun JSONObject.getPairingVersion(): Int? {
        return try {
            this.getJSONObject("message").getJSONObject("data").getJSONObject("capabilities").getInt("pairingVersion")
        } catch (e: JSONException) {
            null
        }
    }

    private fun JSONObject.getPairingSupport(): Boolean? {
        return try {
            this.getJSONObject("message").getJSONObject("data").getJSONObject("capabilities").getBoolean("pairing")
        } catch (e: JSONException) {
            null
        }
    }

    private fun JSONObject.getErrorMessage(): String {
        return this.getJSONObject("message").getJSONObject("data").getJSONObject("error").getString("message")
    }

    private fun JSONObject.messageData(): JSONObject {
        return this.getJSONObject("message").getJSONObject("data")
    }

    data class SignedInUser(val email: String?, val uid: String?, val sessionToken: String, val verified: Boolean)

    private fun JSONObject.signedInUser(): SignedInUser {
        val obj = this.getJSONObject("message").getJSONObject("data").getJSONObject("signedInUser")

        val email =
            if (obj.getString("email") == "null") {
                null
            } else {
                obj.getString("email")
            }
        val uid =
            if (obj.getString("uid") == "null") {
                null
            } else {
                obj.getString("uid")
            }
        return SignedInUser(
            email = email,
            uid = uid,
            sessionToken = obj.getString("sessionToken"),
            verified = obj.getBoolean("verified"),
        )
    }

    private fun JSONObject.isSignedInUserNull(): Boolean {
        return this.getJSONObject("message").getJSONObject("data").isNull("signedInUser")
    }

    private fun JSONObject.getOk(): Boolean {
        return this.getJSONObject("message").getJSONObject("data").getBoolean("ok")
    }

    private fun JSONObject.getError(): String? {
        return this.getJSONObject("message").getJSONObject("data").getString("error")
    }

    private suspend fun verifyOauthLogin(
        action: String,
        expectedAuthType: AuthType,
        code: String,
        state: String,
        declined: Set<SyncEngine>?,
        messageHandler: MessageHandler,
        accountManager: FxaAccountManager,
    ) {
        val jsonToWebChannel = jsonOauthLogin(action, code, state, declined ?: emptySet())
        val port = mock<Port>()
        whenever(port.senderUrl()).thenReturn("https://foo.bar/email")
        messageHandler.onPortMessage(jsonToWebChannel, port)

        val expectedAuthData =
            FxaAuthData(
                authType = expectedAuthType,
                code = code,
                state = state,
                declinedEngines = declined ?: emptySet(),
            )
        shadowOf(getMainLooper()).idle()

        verify(accountManager).finishAuthentication(expectedAuthData)
    }

    private fun jsonOauthLogin(action: String, code: String, state: String, declined: Set<SyncEngine>): JSONObject {
        return JSONObject(
            """{
             "message":{
                "command": "fxaccounts:oauth_login",
                "messageId":123,
                "data":{
                    "action":"$action",
                    "redirect":"urn:ietf:wg:oauth:2.0:oob:oauth-redirect-webchannel",
                    "code":"$code",
                    "state":"$state",
                    "declinedSyncEngines":${declined.map { "${it.nativeName}," }.filterNotNull()}
                }
             }
            }
            """
                .trimIndent()
        )
    }

    private suspend fun verifyLogin(
        sessionToken: String,
        email: String,
        uid: String,
        verified: Boolean,
        messageHandler: MessageHandler,
        accountManager: FxaAccountManager,
    ) {
        val jsonToWebChannel = jsonLogin(sessionToken, email, uid, verified)
        val port = mock<Port>()
        whenever(port.senderUrl()).thenReturn("https://foo.bar/email")
        messageHandler.onPortMessage(jsonToWebChannel, port)

        shadowOf(getMainLooper()).idle()

        val loginDataCaptor = argumentCaptor<String>()
        verify(accountManager).handleWebChannelLogin(loginDataCaptor.capture())
        val loginData = JSONObject(loginDataCaptor.value)
        assertEquals(sessionToken, loginData.getString("sessionToken"))
        assertEquals(email, loginData.getString("email"))
        assertEquals(uid, loginData.getString("uid"))
        assertEquals(verified, loginData.getBoolean("verified"))
    }

    private fun jsonLogin(sessionToken: String, email: String, uid: String, verified: Boolean): JSONObject {
        return JSONObject(
            """{
             "message":{
                "command": "fxaccounts:login",
                "messageId":123,
                "data":{
                    "email":"$email",
                    "sessionToken":"$sessionToken",
                    "uid":"$uid",
                    "verified":$verified
                }
             }
            }
            """
                .trimIndent()
        )
    }

    private fun jsonFxaStatus(): JSONObject {
        return JSONObject(
            """
            {
                         "message":{
                            "command": "fxaccounts:fxa_status",
                            "messageId":123
                         }
                        }
            """
                .trimIndent()
        )
    }

    private fun jsonPairOAuthStart(): JSONObject {
        return JSONObject(
            """
            {
                         "message":{
                            "command": "fxaccounts:pair_oauth_start",
                            "messageId":123,
                            "data":{}
                         }
                        }
            """
                .trimIndent()
        )
    }

    /**
     * Starts a feature for the given capabilities and account manager, and returns the connected content message
     * handler.
     */
    private fun startedMessageHandler(
        ext: WebExtension,
        port: Port,
        engineSession: EngineSession,
        fxaCapabilities: Set<FxaCapability>,
        accountManager: FxaAccountManager,
    ): MessageHandler {
        val messageHandler = argumentCaptor<MessageHandler>()
        val webchannelFeature =
            prepareFeatureForTest(
                ext = ext,
                port = port,
                engineSession = engineSession,
                fxaCapabilities = fxaCapabilities,
                accountManager = accountManager,
            )
        webchannelFeature.start()
        shadowOf(getMainLooper()).idle()

        verify(ext)
            .registerContentMessageHandler(
                eq(engineSession),
                eq(FxaWebChannelFeature.WEB_CHANNEL_MESSAGING_ID),
                messageHandler.capture(),
            )
        messageHandler.value.onPortConnected(port)
        return messageHandler.value
    }

    private fun prepareFeatureForTest(
        ext: WebExtension = mock(),
        port: Port = mock(),
        engineSession: EngineSession = mock(),
        expectedEngines: Set<SyncEngine>? = setOf(SyncEngine.History),
        fxaCapabilities: Set<FxaCapability> = emptySet(),
        accountManager: FxaAccountManager = mock(),
    ): FxaWebChannelFeature {
        val serverConfig: ServerConfig = mock()
        BuiltInWebExtensionController.installedBuiltInExtensions[FxaWebChannelFeature.WEB_CHANNEL_EXTENSION_ID] = ext

        val tab =
            createTab(
                url = "https://www.mozilla.org",
                id = "test-tab",
                engineSession = engineSession,
            )
        val store = BrowserStore(initialState = BrowserState(tabs = listOf(tab), selectedTabId = tab.id))

        whenever(accountManager.supportedSyncEngines()).thenReturn(expectedEngines)
        whenever(port.engineSession).thenReturn(engineSession)
        whenever(port.senderUrl()).thenReturn("https://foo.bar/email")
        whenever(serverConfig.server).thenReturn(FxaServer.Custom("https://foo.bar"))

        return FxaWebChannelFeature(null, mock(), store, accountManager, serverConfig, fxaCapabilities)
    }
}
