/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.content.Context
import io.mockk.MockKAnnotations
import io.mockk.Runs
import io.mockk.confirmVerified
import io.mockk.every
import io.mockk.impl.annotations.MockK
import io.mockk.just
import io.mockk.mockk
import io.mockk.verify
import kotlin.test.assertNull
import kotlinx.coroutines.CoroutineScope
import mozilla.components.concept.sync.AccountObserver
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.concept.sync.SyncEngine
import mozilla.components.service.nimbus.NimbusApi
import mozilla.components.support.base.observer.ObserverRegistry
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Onboarding
import org.mozilla.fenix.GleanMetrics.SyncAuth
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.helpers.perf.TestStrictModeManager
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

// For gleanTestRule
@RunWith(RobolectricTestRunner::class)
class BackgroundServicesTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @MockK private lateinit var context: Context

    @MockK private lateinit var settings: Settings

    @MockK private lateinit var nimbus: NimbusApi

    private lateinit var observer: TelemetryAccountObserver
    private lateinit var registry: ObserverRegistry<AccountObserver>

    @Before
    fun setup() {
        MockKAnnotations.init(this)
        every { settings.signedInFxaAccount = any() } just Runs
        every { settings.lastAuthFlowEntrypoint } returns null
        every { settings.lastAuthFlowEntrypoint = any() } just Runs

        val mockComponents: Components = mockk()
        every { mockComponents.settings } returns settings
        every { mockComponents.nimbus } returns
            mockk {
                every { sdk } returns nimbus
                every { events } returns nimbus
            }
        every { context.components } returns mockComponents
        every { nimbus.recordEvent(any()) } just Runs

        observer = TelemetryAccountObserver(context, mockComponents.settings)
        registry = ObserverRegistry<AccountObserver>().apply { register(observer) }
    }

    @Test
    fun `telemetry account observer tracks sign in event`() {
        val account = mockk<OAuthAccount>()

        registry.notifyObservers { onAuthenticated(account, AuthType.Signin) }
        assertEquals(1, SyncAuth.signIn.testGetValue()!!.size)
        assertEquals(null, SyncAuth.signIn.testGetValue()!!.single().extra)
        verify { settings.signedInFxaAccount = true }
        verify { settings.lastAuthFlowEntrypoint }
        confirmVerified(settings)
    }

    @Test
    fun `telemetry account observer tracks sign up event`() {
        val account = mockk<OAuthAccount>()

        registry.notifyObservers { onAuthenticated(account, AuthType.Signup) }
        assertEquals(1, SyncAuth.signUp.testGetValue()!!.size)
        assertEquals(null, SyncAuth.signUp.testGetValue()!!.single().extra)
        verify { settings.signedInFxaAccount = true }
        verify { settings.lastAuthFlowEntrypoint }
        confirmVerified(settings)
    }

    @Test
    fun `telemetry account observer tracks pairing event`() {
        val account = mockk<OAuthAccount>()

        registry.notifyObservers { onAuthenticated(account, AuthType.Pairing) }
        assertEquals(1, SyncAuth.paired.testGetValue()!!.size)
        assertEquals(null, SyncAuth.paired.testGetValue()!!.single().extra)
        verify { settings.signedInFxaAccount = true }
        verify { settings.lastAuthFlowEntrypoint }
        confirmVerified(settings)
    }

    @Test
    fun `telemetry account observer tracks recovered event`() {
        val account = mockk<OAuthAccount>()

        registry.notifyObservers { onAuthenticated(account, AuthType.Recovered) }
        assertEquals(1, SyncAuth.recovered.testGetValue()!!.size)
        assertEquals(null, SyncAuth.recovered.testGetValue()!!.single().extra)
        verify { settings.signedInFxaAccount = true }
        confirmVerified(settings)
    }

    @Test
    fun `telemetry account observer tracks external creation event with null action`() {
        val account = mockk<OAuthAccount>()

        registry.notifyObservers { onAuthenticated(account, AuthType.OtherExternal(null)) }
        assertEquals(1, SyncAuth.otherExternal.testGetValue()!!.size)
        assertEquals(null, SyncAuth.otherExternal.testGetValue()!!.single().extra)
        verify { settings.signedInFxaAccount = true }
        confirmVerified(settings)
    }

    @Test
    fun `telemetry account observer tracks external creation event with some action`() {
        val account = mockk<OAuthAccount>()

        registry.notifyObservers { onAuthenticated(account, AuthType.OtherExternal("someAction")) }
        assertEquals(1, SyncAuth.otherExternal.testGetValue()!!.size)
        assertEquals(null, SyncAuth.otherExternal.testGetValue()!!.single().extra)
        verify { settings.signedInFxaAccount = true }
        confirmVerified(settings)
    }

    @Test
    fun `telemetry account observer tracks sign out event`() {
        registry.notifyObservers { onLoggedOut() }
        assertEquals(1, SyncAuth.signOut.testGetValue()!!.size)
        assertEquals(null, SyncAuth.signOut.testGetValue()!!.single().extra)
        verify { settings.signedInFxaAccount = false }
        confirmVerified(settings)
    }

    @Test
    fun `telemetry account observer records nimbus event for logins`() {
        observer.onAuthenticated(mockk(), AuthType.Signin)
        verify {
            nimbus.recordEvent("sync_auth.sign_in")
        }
        confirmVerified(nimbus)
    }

    @Test
    fun `GIVEN credentials are syncable WHEN background services are created THEN passwords and credit cards are synced`() {
        val supportedEngines = createBackgroundServices(areCredentialsSyncable = true).supportedEngines

        assertTrue(supportedEngines.contains(SyncEngine.Passwords))
        assertTrue(supportedEngines.contains(SyncEngine.CreditCards))
    }

    @Test
    fun `GIVEN credentials are not syncable WHEN background services are created THEN passwords and credit cards are not synced`() {
        val supportedEngines = createBackgroundServices(areCredentialsSyncable = false).supportedEngines

        assertFalse(supportedEngines.contains(SyncEngine.Passwords))
        assertFalse(supportedEngines.contains(SyncEngine.CreditCards))
        assertTrue(supportedEngines.contains(SyncEngine.History))
        assertTrue(supportedEngines.contains(SyncEngine.Bookmarks))
        assertTrue(supportedEngines.contains(SyncEngine.Tabs))
    }

    @Test
    fun `GIVEN a pairing card flow WHEN the user pairs THEN the sign in is recorded and the entrypoint is cleared`() {
        every { settings.lastAuthFlowEntrypoint } returns FenixFxAEntryPoint.OnboardingPairing.entryName

        registry.notifyObservers { onAuthenticated(mockk(), AuthType.Pairing) }

        val event = Onboarding.syncPairingCardSignedIn.testGetValue()!!.single()
        assertEquals("pairing", event.extra?.getValue("auth_type"))
        verify { settings.lastAuthFlowEntrypoint = null }
    }

    @Test
    fun `GIVEN a pairing card flow WHEN the user signs in THEN the sign in is recorded and the entrypoint is cleared`() {
        every { settings.lastAuthFlowEntrypoint } returns FenixFxAEntryPoint.OnboardingPairing.entryName

        registry.notifyObservers { onAuthenticated(mockk(), AuthType.Signin) }

        val event = Onboarding.syncPairingCardSignedIn.testGetValue()!!.single()
        assertEquals("sign_in", event.extra?.getValue("auth_type"))
        verify { settings.lastAuthFlowEntrypoint = null }
    }

    @Test
    fun `GIVEN a pairing card flow WHEN the user signs up THEN the sign in is recorded and the entrypoint is cleared`() {
        every { settings.lastAuthFlowEntrypoint } returns FenixFxAEntryPoint.OnboardingPairing.entryName

        registry.notifyObservers { onAuthenticated(mockk(), AuthType.Signup) }

        val event = Onboarding.syncPairingCardSignedIn.testGetValue()!!.single()
        assertEquals("sign_up", event.extra?.getValue("auth_type"))
        verify { settings.lastAuthFlowEntrypoint = null }
    }

    @Test
    fun `GIVEN a flow from another entrypoint WHEN the user signs in THEN the sign in is not recorded and the entrypoint is cleared`() {
        every { settings.lastAuthFlowEntrypoint } returns FenixFxAEntryPoint.SettingsMenu.entryName

        registry.notifyObservers { onAuthenticated(mockk(), AuthType.Signin) }

        assertNull(Onboarding.syncPairingCardSignedIn.testGetValue())
        verify { settings.lastAuthFlowEntrypoint = null }
    }

    @Test
    fun `GIVEN a flow from the new user onboarding entrypoint WHEN the user signs in THEN the sign in is not recorded and the entrypoint is cleared`() {
        every { settings.lastAuthFlowEntrypoint } returns FenixFxAEntryPoint.NewUserOnboarding.entryName

        registry.notifyObservers { onAuthenticated(mockk(), AuthType.Signin) }

        assertNull(Onboarding.syncPairingCardSignedIn.testGetValue())
        verify { settings.lastAuthFlowEntrypoint = null }
    }

    @Test
    fun `GIVEN no pairing flow WHEN the user signs in THEN the sign in is not recorded`() {
        registry.notifyObservers { onAuthenticated(mockk(), AuthType.Signin) }

        assertNull(Onboarding.syncPairingCardSignedIn.testGetValue())
        verify(exactly = 0) { settings.lastAuthFlowEntrypoint = any() }
    }

    @Test
    fun `GIVEN a pairing card flow WHEN the account is recovered THEN the sign in is not recorded and the entrypoint is unchanged`() {
        every { settings.lastAuthFlowEntrypoint } returns FenixFxAEntryPoint.NewUserOnboarding.entryName

        registry.notifyObservers { onAuthenticated(mockk(), AuthType.Recovered) }

        assertNull(Onboarding.syncPairingCardSignedIn.testGetValue())
        verify(exactly = 0) { settings.lastAuthFlowEntrypoint = any() }
    }

    private fun createBackgroundServices(areCredentialsSyncable: Boolean) =
        BackgroundServices(
            context = testContext,
            push = mockk(relaxed = true),
            settings = mockk(relaxed = true),
            crashReporter = mockk(relaxed = true),
            historyStorage = mockk(relaxed = true),
            bookmarkStorage = mockk(relaxed = true),
            passwordsStorage = mockk(relaxed = true),
            remoteTabsStorage = mockk(relaxed = true),
            creditCardsStorage = mockk(relaxed = true),
            strictMode = TestStrictModeManager(),
            applicationScope = mockk<CoroutineScope>(relaxed = true),
            areCredentialsSyncable = areCredentialsSyncable,
        )
}
