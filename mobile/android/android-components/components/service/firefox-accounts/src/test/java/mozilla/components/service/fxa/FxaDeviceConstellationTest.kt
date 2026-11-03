/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.fxa

import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.appservices.fxaclient.AccountEvent as ASAccountEvent
import mozilla.appservices.fxaclient.CloseTabsResult
import mozilla.appservices.fxaclient.Device as NativeDevice
import mozilla.appservices.fxaclient.DevicePushSubscription as NativeDevicePushSubscription
import mozilla.appservices.fxaclient.FxaClient as NativeFirefoxAccount
import mozilla.appservices.fxaclient.FxaException
import mozilla.appservices.fxaclient.IncomingDeviceCommand
import mozilla.appservices.fxaclient.SendTabPayload
import mozilla.appservices.fxaclient.TabHistoryEntry
import mozilla.appservices.sync15.DeviceType as RustDeviceType
import mozilla.appservices.syncmanager.DeviceSettings
import mozilla.components.concept.sync.AccountEvent
import mozilla.components.concept.sync.AccountEventsObserver
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.ConstellationState
import mozilla.components.concept.sync.DeviceCapability
import mozilla.components.concept.sync.DeviceCommandIncoming
import mozilla.components.concept.sync.DeviceCommandOutgoing
import mozilla.components.concept.sync.DeviceConfig
import mozilla.components.concept.sync.DeviceConstellationObserver
import mozilla.components.concept.sync.DevicePushSubscription
import mozilla.components.concept.sync.DeviceType
import mozilla.components.concept.sync.TabData
import mozilla.components.concept.sync.TabPrivacy
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.expectException
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.anyBoolean
import org.mockito.Mockito.atLeast
import org.mockito.Mockito.doAnswer
import org.mockito.Mockito.never
import org.mockito.Mockito.reset
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.verifyNoInteractions
import org.mockito.Mockito.`when`

@RunWith(AndroidJUnit4::class)
class FxaDeviceConstellationTest {
    lateinit var account: NativeFirefoxAccount
    lateinit var constellation: FxaDeviceConstellation
    private val testDispatcher = StandardTestDispatcher()

    @Before
    fun setup() {
        account = mock()
        val scope = TestScope(testDispatcher)
        constellation = FxaDeviceConstellation(account, scope, mock())
    }

    @Test
    fun `finalize device`() =
        runTest(testDispatcher) {
            fun expectedFinalizeAction(authType: AuthType): FxaDeviceConstellation.DeviceFinalizeAction =
                when (authType) {
                    AuthType.Existing -> FxaDeviceConstellation.DeviceFinalizeAction.EnsureCapabilities
                    AuthType.Signin -> FxaDeviceConstellation.DeviceFinalizeAction.Initialize
                    AuthType.Signup -> FxaDeviceConstellation.DeviceFinalizeAction.Initialize
                    AuthType.Pairing -> FxaDeviceConstellation.DeviceFinalizeAction.Initialize
                    is AuthType.OtherExternal -> FxaDeviceConstellation.DeviceFinalizeAction.Initialize
                    AuthType.MigratedCopy -> FxaDeviceConstellation.DeviceFinalizeAction.Initialize
                    AuthType.MigratedReuse -> FxaDeviceConstellation.DeviceFinalizeAction.EnsureCapabilities
                    AuthType.Recovered -> FxaDeviceConstellation.DeviceFinalizeAction.None
                }
            fun initAuthType(simpleClassName: String): AuthType =
                when (simpleClassName) {
                    "Existing" -> AuthType.Existing
                    "Signin" -> AuthType.Signin
                    "Signup" -> AuthType.Signup
                    "Pairing" -> AuthType.Pairing
                    "OtherExternal" -> AuthType.OtherExternal("test")
                    "MigratedCopy" -> AuthType.MigratedCopy
                    "MigratedReuse" -> AuthType.MigratedReuse
                    "Recovered" -> AuthType.Recovered
                    else -> throw AssertionError("Unknown AuthType: $simpleClassName")
                }
            val config = DeviceConfig("test name", DeviceType.TABLET, setOf(DeviceCapability.SEND_TAB))
            AuthType::class
                .sealedSubclasses
                .map { initAuthType(it.simpleName!!) }
                .forEach {
                    constellation.finalizeDevice(it, config)
                    when (expectedFinalizeAction(it)) {
                        FxaDeviceConstellation.DeviceFinalizeAction.Initialize -> {
                            verify(account)
                                .initializeDevice(
                                    "test name",
                                    RustDeviceType.TABLET,
                                    setOf(mozilla.appservices.fxaclient.DeviceCapability.SEND_TAB),
                                )
                        }
                        FxaDeviceConstellation.DeviceFinalizeAction.EnsureCapabilities -> {
                            verify(account)
                                .ensureCapabilities(setOf(mozilla.appservices.fxaclient.DeviceCapability.SEND_TAB))
                        }
                        FxaDeviceConstellation.DeviceFinalizeAction.None -> {
                            verifyNoInteractions(account)
                        }
                    }
                    reset(account)
                }
        }

    @Test
    fun `updating device name`() =
        runTest(testDispatcher) {
            val currentDevice = testDevice("currentTestDevice", true)
            `when`(account.getDevices()).thenReturn(arrayOf(currentDevice))

            // Can't update cached value in an empty cache
            try {
                constellation.setDeviceName("new name", testContext)
                fail()
            } catch (e: IllegalStateException) {
                // Ignore exception
            }

            val cache = FxaDeviceSettingsCache(testContext)
            cache.setToCache(DeviceSettings("someId", "test name", RustDeviceType.MOBILE))

            // No device state observer.
            assertTrue(constellation.setDeviceName("new name", testContext))
            verify(account, times(2)).setDeviceDisplayName("new name")

            assertEquals(DeviceSettings("someId", "new name", RustDeviceType.MOBILE), cache.getCached())

            // Set up the observer...
            val observer =
                object : DeviceConstellationObserver {
                    var state: ConstellationState? = null

                    override fun onDevicesUpdate(constellation: ConstellationState) {
                        state = constellation
                    }
                }
            constellation.registerDeviceObserver(observer, startedLifecycleOwner(), false)

            assertTrue(constellation.setDeviceName("another name", testContext))
            verify(account).setDeviceDisplayName("another name")

            assertEquals(DeviceSettings("someId", "another name", RustDeviceType.MOBILE), cache.getCached())

            // Since we're faking the data, here we're just testing that observer is notified with the
            // up-to-date constellation.
            assertEquals(observer.state!!.currentDevice!!.displayName, "testName")
        }

    @Test
    fun `set device push subscription`() =
        runTest(testDispatcher) {
            val subscription = DevicePushSubscription("http://endpoint.com", "pk", "auth key")
            constellation.setDevicePushSubscription(subscription)

            verify(account).setDevicePushSubscription("http://endpoint.com", "pk", "auth key")
        }

    @Test
    fun `process raw device command`() =
        runTest(testDispatcher) {
            // No commands, no observer.
            `when`(account.handlePushMessage("raw events payload"))
                .thenReturn(mozilla.appservices.fxaclient.AccountEvent.Unknown)
            assertTrue(constellation.processRawEvent("raw events payload"))

            // No commands, with observer.
            val eventsObserver =
                object : AccountEventsObserver {
                    var latestEvents: List<AccountEvent>? = null

                    override fun onEvents(events: List<AccountEvent>) {
                        latestEvents = events
                    }
                }

            // No commands, with an observer.
            constellation.register(eventsObserver)
            assertTrue(constellation.processRawEvent("raw events payload"))
            assertEquals(listOf(AccountEvent.Unknown), eventsObserver.latestEvents)

            // Some commands, with an observer. More detailed command handling tests below.
            val testDevice1 = testDevice("test1", false)
            val testTab1 = TabHistoryEntry("Hello", "http://world.com/1")
            `when`(account.handlePushMessage("raw events payload"))
                .thenReturn(
                    ASAccountEvent.CommandReceived(
                        command =
                            IncomingDeviceCommand.TabReceived(
                                testDevice1,
                                SendTabPayload(listOf(testTab1), "flowid", "streamid"),
                            )
                    )
                )

            `when`(account.pollDeviceCommands())
                .thenReturn(
                    arrayOf(
                        IncomingDeviceCommand.TabReceived(
                            testDevice1,
                            SendTabPayload(listOf(testTab1), "flowid", "streamid"),
                        )
                    )
                )
            assertTrue(constellation.processRawEvent("raw events payload"))
            verify(account).pollDeviceCommands()

            val events = eventsObserver.latestEvents!!
            val command = (events[0] as AccountEvent.DeviceCommandIncoming).command
            assertEquals(testDevice1.into(), (command as DeviceCommandIncoming.TabReceived).from)
            assertEquals(listOf(testTab1.into()), command.entries)
        }

    @Test
    fun `send command to device`() =
        runTest(testDispatcher) {
            `when`(account.gatherTelemetry()).thenReturn("{}")
            assertTrue(
                constellation.sendCommandToDevice(
                    "targetID",
                    DeviceCommandOutgoing.SendTab("Mozilla", "https://www.mozilla.org", TabPrivacy.Normal),
                )
            )

            verify(account).sendSingleTab("targetID", "Mozilla", "https://www.mozilla.org", false)
        }

    @Test
    fun `send command to device will report exceptions`() =
        runTest(testDispatcher) {
            val exception = FxaException.Other("")
            val exceptionCaptor = argumentCaptor<SendCommandException.Other>()
            doAnswer { throw exception }.`when`(account).sendSingleTab(any(), any(), any(), anyBoolean())

            val success =
                constellation.sendCommandToDevice(
                    "targetID",
                    DeviceCommandOutgoing.SendTab("Mozilla", "https://www.mozilla.org", TabPrivacy.Normal),
                )

            assertFalse(success)
            verify(constellation.crashReporter!!).submitCaughtException(exceptionCaptor.capture())
            assertSame(exception, exceptionCaptor.value.cause)
        }

    @Test
    fun `send command to device won't report network exceptions`() =
        runTest(testDispatcher) {
            val exception = FxaException.Network("timeout!")
            doAnswer { throw exception }.`when`(account).sendSingleTab(any(), any(), any(), anyBoolean())

            val success =
                constellation.sendCommandToDevice(
                    "targetID",
                    DeviceCommandOutgoing.SendTab("Mozilla", "https://www.mozilla.org", TabPrivacy.Normal),
                )

            assertFalse(success)
            verify(constellation.crashReporter!!, never()).submitCaughtException(any())
        }

    @Test
    fun `send command to device will propagate close tabs-specific exceptions`() =
        runTest(testDispatcher) {
            `when`(account.gatherTelemetry()).thenReturn("{}")
            `when`(account.closeTabs(any(), any()))
                .thenReturn(CloseTabsResult.TabsNotClosed(listOf("https://example.com")))

            val exception =
                expectException<SendCommandException.TabsNotClosed> {
                    constellation.sendCommandToDevice(
                        "targetID",
                        DeviceCommandOutgoing.CloseTab(listOf("https://example.com", "https://mozilla.org")),
                    )
                }
            assertEquals(listOf("https://example.com"), exception.urls)
            val exceptionCaptor = argumentCaptor<Throwable>()
            verify(constellation.crashReporter!!, atLeast(0)).submitCaughtException(exceptionCaptor.capture())
            assertFalse(exceptionCaptor.allValues.any { it is SendCommandException.TabsNotClosed })
        }

    @Test
    fun `refreshing constellation`() =
        runTest(testDispatcher) {
            // No devices, no observers.
            `when`(account.getDevices()).thenReturn(emptyArray())

            constellation.refreshDevices()

            val observer =
                object : DeviceConstellationObserver {
                    var state: ConstellationState? = null

                    override fun onDevicesUpdate(constellation: ConstellationState) {
                        state = constellation
                    }
                }
            constellation.registerDeviceObserver(observer, startedLifecycleOwner(), false)

            // No devices, with an observer.
            constellation.refreshDevices()
            assertEquals(ConstellationState(null, listOf()), observer.state)

            val testDevice1 = testDevice("test1", false)
            val testDevice2 = testDevice("test2", false)
            val currentDevice = testDevice("currentTestDevice", true)

            // Single device, no current device.
            `when`(account.getDevices()).thenReturn(arrayOf(testDevice1))
            constellation.refreshDevices()

            assertEquals(ConstellationState(null, listOf(testDevice1.into())), observer.state)
            assertEquals(ConstellationState(null, listOf(testDevice1.into())), constellation.state())

            // Current device, no other devices.
            `when`(account.getDevices()).thenReturn(arrayOf(currentDevice))
            constellation.refreshDevices()
            assertEquals(ConstellationState(currentDevice.into(), listOf()), observer.state)
            assertEquals(ConstellationState(currentDevice.into(), listOf()), constellation.state())

            // Current device with other devices.
            `when`(account.getDevices())
                .thenReturn(
                    arrayOf(
                        currentDevice,
                        testDevice1,
                        testDevice2,
                    )
                )
            constellation.refreshDevices()

            assertEquals(
                ConstellationState(currentDevice.into(), listOf(testDevice1.into(), testDevice2.into())),
                observer.state,
            )
            assertEquals(
                ConstellationState(currentDevice.into(), listOf(testDevice1.into(), testDevice2.into())),
                constellation.state(),
            )

            // Current device with expired subscription.
            val currentDeviceExpired = testDevice("currentExpired", true, expired = true)
            `when`(account.getDevices())
                .thenReturn(
                    arrayOf(
                        currentDeviceExpired,
                        testDevice2,
                    )
                )

            `when`(account.pollDeviceCommands()).thenReturn(emptyArray())
            `when`(account.gatherTelemetry()).thenReturn("{}")

            constellation.refreshDevices()

            verify(account, times(1)).pollDeviceCommands()

            assertEquals(ConstellationState(currentDeviceExpired.into(), listOf(testDevice2.into())), observer.state)
            assertEquals(
                ConstellationState(currentDeviceExpired.into(), listOf(testDevice2.into())),
                constellation.state(),
            )

            // Current device with no subscription.
            val currentDeviceNoSub = testDevice("currentNoSub", true, expired = false, subscribed = false)

            `when`(account.getDevices())
                .thenReturn(
                    arrayOf(
                        currentDeviceNoSub,
                        testDevice2,
                    )
                )

            `when`(account.pollDeviceCommands()).thenReturn(emptyArray())
            `when`(account.gatherTelemetry()).thenReturn("{}")

            constellation.refreshDevices()

            verify(account, times(2)).pollDeviceCommands()
            assertEquals(
                ConstellationState(currentDeviceNoSub.into(), listOf(testDevice2.into())),
                constellation.state(),
            )
        }

    @Test
    fun `polling for commands triggers observers`() =
        runTest(testDispatcher) {
            // No commands, no observers.
            `when`(account.gatherTelemetry()).thenReturn("{}")
            `when`(account.pollDeviceCommands()).thenReturn(emptyArray())
            assertTrue(constellation.pollForCommands())

            val eventsObserver =
                object : AccountEventsObserver {
                    var latestEvents: List<AccountEvent>? = null

                    override fun onEvents(events: List<AccountEvent>) {
                        latestEvents = events
                    }
                }

            // No commands, with an observer.
            constellation.register(eventsObserver)
            assertTrue(constellation.pollForCommands())
            assertEquals(listOf<AccountEvent>(), eventsObserver.latestEvents)

            // Some commands.
            `when`(account.pollDeviceCommands())
                .thenReturn(arrayOf(IncomingDeviceCommand.TabReceived(null, SendTabPayload(emptyList(), "", ""))))
            assertTrue(constellation.pollForCommands())

            var command = (eventsObserver.latestEvents!![0] as AccountEvent.DeviceCommandIncoming).command
            assertEquals(null, (command as DeviceCommandIncoming.TabReceived).from)
            assertEquals(listOf<TabData>(), command.entries)

            val testDevice1 = testDevice("test1", false)
            val testDevice2 = testDevice("test2", false)
            val testTab1 = TabHistoryEntry("Hello", "http://world.com/1")
            val testTab2 = TabHistoryEntry("Hello", "http://world.com/2")
            val testTab3 = TabHistoryEntry("Hello", "http://world.com/3")

            // Zero tabs from a single device.
            `when`(account.pollDeviceCommands())
                .thenReturn(
                    arrayOf(IncomingDeviceCommand.TabReceived(testDevice1, SendTabPayload(emptyList(), "", "")))
                )
            assertTrue(constellation.pollForCommands())

            assertNotNull(eventsObserver.latestEvents)
            assertEquals(1, eventsObserver.latestEvents!!.size)
            command = (eventsObserver.latestEvents!![0] as AccountEvent.DeviceCommandIncoming).command
            assertEquals(testDevice1.into(), (command as DeviceCommandIncoming.TabReceived).from)
            assertEquals(listOf<TabData>(), command.entries)

            // Single tab from a single device.
            `when`(account.pollDeviceCommands())
                .thenReturn(
                    arrayOf(IncomingDeviceCommand.TabReceived(testDevice2, SendTabPayload(listOf(testTab1), "", "")))
                )
            assertTrue(constellation.pollForCommands())

            command = (eventsObserver.latestEvents!![0] as AccountEvent.DeviceCommandIncoming).command
            assertEquals(testDevice2.into(), (command as DeviceCommandIncoming.TabReceived).from)
            assertEquals(listOf(testTab1.into()), command.entries)

            // Multiple tabs from a single device.
            `when`(account.pollDeviceCommands())
                .thenReturn(
                    arrayOf(
                        IncomingDeviceCommand.TabReceived(
                            testDevice2,
                            SendTabPayload(listOf(testTab1, testTab3), "", ""),
                        )
                    )
                )
            assertTrue(constellation.pollForCommands())

            command = (eventsObserver.latestEvents!![0] as AccountEvent.DeviceCommandIncoming).command
            assertEquals(testDevice2.into(), (command as DeviceCommandIncoming.TabReceived).from)
            assertEquals(listOf(testTab1.into(), testTab3.into()), command.entries)

            // Multiple tabs received from multiple devices.
            `when`(account.pollDeviceCommands())
                .thenReturn(
                    arrayOf(
                        IncomingDeviceCommand.TabReceived(
                            testDevice2,
                            SendTabPayload(listOf(testTab1, testTab2), "", ""),
                        ),
                        IncomingDeviceCommand.TabReceived(testDevice1, SendTabPayload(listOf(testTab3), "", "")),
                    )
                )
            assertTrue(constellation.pollForCommands())

            command = (eventsObserver.latestEvents!![0] as AccountEvent.DeviceCommandIncoming).command
            assertEquals(testDevice2.into(), (command as DeviceCommandIncoming.TabReceived).from)
            assertEquals(listOf(testTab1.into(), testTab2.into()), command.entries)
            command = (eventsObserver.latestEvents!![1] as AccountEvent.DeviceCommandIncoming).command
            assertEquals(testDevice1.into(), (command as DeviceCommandIncoming.TabReceived).from)
            assertEquals(listOf(testTab3.into()), command.entries)

            // TODO FirefoxAccount needs @Throws annotations for these tests to actually work.
            // Failure to poll for commands. Panics are re-thrown.
            //        `when`(account.pollDeviceCommands()).thenThrow(FxaPanicException("Don't panic!"))
            //        try {
            //            runBlocking(coroutinesTestRule.testDispatcher) {
            //                constellation.refreshAsync()
            //            }
            //            fail()
            //        } catch (e: FxaPanicException) {}
            //
            //        // Network exception are handled.
            //        `when`(account.pollDeviceCommands()).thenThrow(FxaNetworkException("four oh four"))
            //        runBlocking(coroutinesTestRule.testDispatcher) {
            //            Assert.assertFalse(constellation.refreshAsync())
            //        }
            //        // Unspecified exception are handled.
            //        `when`(account.pollDeviceCommands()).thenThrow(FxaUnspecifiedException("hmmm..."))
            //        runBlocking(coroutinesTestRule.testDispatcher) {
            //            Assert.assertFalse(constellation.refreshAsync())
            //        }
            //        // Unauthorized exception are handled.
            //        val authErrorObserver = object : AuthErrorObserver {
            //            var latestException: AuthException? = null
            //
            //            override fun onAuthErrorAsync(e: AuthException): Deferred<Unit> {
            //                latestException = e
            //                val r = CompletableDeferred<Unit>()
            //                r.complete(Unit)
            //                return r
            //            }
            //        }
            //        authErrorRegistry.register(authErrorObserver)
            //
            //        val authException = FxaUnauthorizedException("oh no you didn't!")
            //        `when`(account.pollDeviceCommands()).thenThrow(authException)
            //        runBlocking(coroutinesTestRule.testDispatcher) {
            //            Assert.assertFalse(constellation.refreshAsync())
            //        }
            //        assertEquals(authErrorObserver.latestException!!.cause, authException)
        }

    private fun testDevice(
        id: String,
        current: Boolean,
        expired: Boolean = false,
        subscribed: Boolean = true,
    ): NativeDevice {
        return NativeDevice(
            id = id,
            displayName = "testName",
            deviceType = RustDeviceType.MOBILE,
            isCurrentDevice = current,
            lastAccessTime = 123L,
            capabilities = listOf(),
            pushEndpointExpired = expired,
            pushSubscription =
                if (subscribed) NativeDevicePushSubscription("http://endpoint.com", "pk", "auth key") else null,
        )
    }

    private fun startedLifecycleOwner(): LifecycleOwner {
        val lifecycleOwner = mock<LifecycleOwner>()
        val lifecycle = mock<Lifecycle>()
        `when`(lifecycle.currentState).thenReturn(Lifecycle.State.STARTED)
        `when`(lifecycleOwner.lifecycle).thenReturn(lifecycle)
        return lifecycleOwner
    }
}
