/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko

import android.app.Activity
import android.content.Context
import android.graphics.Color
import android.os.Looper.getMainLooper
import androidx.annotation.OptIn
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.IOException
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.browser.engine.gecko.autofill.RuntimeAddressStructureAccessor
import mozilla.components.browser.engine.gecko.ext.getAntiTrackingPolicy
import mozilla.components.browser.engine.gecko.mediaquery.toGeckoValue
import mozilla.components.browser.engine.gecko.preferences.GeckoPreferenceAccessor
import mozilla.components.browser.engine.gecko.preferences.GeckoPreferenceObserverDelegate
import mozilla.components.browser.engine.gecko.serviceworker.GeckoServiceWorkerDelegate
import mozilla.components.browser.engine.gecko.translate.RuntimeTranslationAccessor
import mozilla.components.browser.engine.gecko.util.SpeculativeEngineSession
import mozilla.components.browser.engine.gecko.util.SpeculativeSessionObserver
import mozilla.components.browser.engine.gecko.webextension.GeckoWebExtensionException
import mozilla.components.browser.engine.gecko.webextension.mockNativeWebExtension
import mozilla.components.browser.engine.gecko.webextension.mockNativeWebExtensionMetaData
import mozilla.components.concept.engine.DefaultSettings
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSession.SafeBrowsingPolicy
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy.CookiePolicy
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy.TrackingCategory
import mozilla.components.concept.engine.UnsupportedSettingException
import mozilla.components.concept.engine.content.blocking.TrackerLog
import mozilla.components.concept.engine.content.blocking.TrackingProtectionEvent
import mozilla.components.concept.engine.mediaquery.PreferredColorScheme
import mozilla.components.concept.engine.preferences.Branch
import mozilla.components.concept.engine.preferences.BrowserPrefObserverDelegate
import mozilla.components.concept.engine.preferences.SetBrowserPreference
import mozilla.components.concept.engine.serviceworker.ServiceWorkerDelegate
import mozilla.components.concept.engine.translate.Language
import mozilla.components.concept.engine.translate.LanguageModel
import mozilla.components.concept.engine.translate.LanguageSetting
import mozilla.components.concept.engine.translate.ModelManagementOptions
import mozilla.components.concept.engine.translate.ModelOperation
import mozilla.components.concept.engine.translate.ModelState
import mozilla.components.concept.engine.translate.OperationLevel
import mozilla.components.concept.engine.translate.TranslationSupport
import mozilla.components.concept.engine.utils.EngineReleaseChannel
import mozilla.components.concept.engine.webextension.Action
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.concept.engine.webextension.PermissionPromptResponse
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.engine.webextension.WebExtensionDelegate
import mozilla.components.concept.engine.webextension.WebExtensionException
import mozilla.components.concept.engine.webextension.WebExtensionInstallException
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.eq
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import mozilla.components.test.ReflectionUtils
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyBoolean
import org.mockito.ArgumentMatchers.anyFloat
import org.mockito.ArgumentMatchers.anyInt
import org.mockito.ArgumentMatchers.anyList
import org.mockito.ArgumentMatchers.anyLong
import org.mockito.ArgumentMatchers.anyString
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.reset
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`
import org.mozilla.geckoview.ContentBlocking
import org.mozilla.geckoview.ContentBlocking.CookieBehavior
import org.mozilla.geckoview.ContentBlockingController
import org.mozilla.geckoview.ContentBlockingController.Event
import org.mozilla.geckoview.ExperimentalGeckoViewApi
import org.mozilla.geckoview.GeckoPreferenceController.GeckoPreference
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoRuntimeSettings
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoWebExecutor
import org.mozilla.geckoview.OrientationController
import org.mozilla.geckoview.StorageController
import org.mozilla.geckoview.WebExtension as GeckoWebExtension
import org.mozilla.geckoview.WebExtension.InstallException.ErrorCodes.ERROR_BLOCKLISTED
import org.mozilla.geckoview.WebExtension.InstallException.ErrorCodes.ERROR_CORRUPT_FILE
import org.mozilla.geckoview.WebExtension.InstallException.ErrorCodes.ERROR_FILE_ACCESS
import org.mozilla.geckoview.WebExtension.InstallException.ErrorCodes.ERROR_INCORRECT_HASH
import org.mozilla.geckoview.WebExtension.InstallException.ErrorCodes.ERROR_INCORRECT_ID
import org.mozilla.geckoview.WebExtension.InstallException.ErrorCodes.ERROR_NETWORK_FAILURE
import org.mozilla.geckoview.WebExtension.InstallException.ErrorCodes.ERROR_POSTPONED
import org.mozilla.geckoview.WebExtension.InstallException.ErrorCodes.ERROR_SIGNEDSTATE_REQUIRED
import org.mozilla.geckoview.WebExtension.InstallException.ErrorCodes.ERROR_UNEXPECTED_ADDON_TYPE
import org.mozilla.geckoview.WebExtension.InstallException.ErrorCodes.ERROR_USER_CANCELED
import org.mozilla.geckoview.WebExtensionController
import org.mozilla.geckoview.WebNotification
import org.mozilla.geckoview.WebPushController
import org.robolectric.Robolectric
import org.robolectric.Shadows.shadowOf

typealias GeckoInstallException = org.mozilla.geckoview.WebExtension.InstallException

@RunWith(AndroidJUnit4::class)
class GeckoEngineTest {

    private lateinit var runtime: GeckoRuntime
    private lateinit var context: Context
    private lateinit var runtimeTranslationAccessor: RuntimeTranslationAccessor

    @Before
    fun setup() {
        runtime = mock()
        whenever(runtime.settings).thenReturn(mock())
        context = mock()
        runtimeTranslationAccessor = mock()
    }

    @Test
    fun createView() {
        assertIs<GeckoEngineView>(
            GeckoEngine(context, runtime = runtime).createView(Robolectric.buildActivity(Activity::class.java).get())
        )
    }

    @Test
    fun createSession() {
        val engine = GeckoEngine(context, runtime = runtime)
        assertIs<GeckoEngineSession>(engine.createSession())

        // Create a private speculative session and consume it
        engine.speculativeCreateSession(private = true)
        assertTrue(engine.speculativeConnectionFactory.hasSpeculativeSession())
        var privateSpeculativeSession = engine.speculativeConnectionFactory.speculativeEngineSession!!.engineSession
        assertSame(privateSpeculativeSession, engine.createSession(private = true))
        assertFalse(engine.speculativeConnectionFactory.hasSpeculativeSession())

        // Create a regular speculative session and make sure it is not returned
        // if a private session is requested instead.
        engine.speculativeCreateSession(private = false)
        assertTrue(engine.speculativeConnectionFactory.hasSpeculativeSession())
        privateSpeculativeSession = engine.speculativeConnectionFactory.speculativeEngineSession!!.engineSession
        assertNotSame(privateSpeculativeSession, engine.createSession(private = true))
        // Make sure previous (never used) speculative session is now closed
        assertFalse(privateSpeculativeSession.geckoSession.isOpen)
        assertFalse(engine.speculativeConnectionFactory.hasSpeculativeSession())
    }

    @Test
    fun speculativeCreateSession() {
        val engine = GeckoEngine(context, runtime = runtime)
        assertNull(engine.speculativeConnectionFactory.speculativeEngineSession)

        // Create a private speculative session
        engine.speculativeCreateSession(private = true)
        assertNotNull(engine.speculativeConnectionFactory.speculativeEngineSession)
        val privateSpeculativeSession = engine.speculativeConnectionFactory.speculativeEngineSession!!
        assertTrue(privateSpeculativeSession.engineSession.geckoSession.settings.usePrivateMode)

        // Creating another private speculative session should have no effect as
        // session hasn't been "consumed".
        engine.speculativeCreateSession(private = true)
        assertSame(privateSpeculativeSession, engine.speculativeConnectionFactory.speculativeEngineSession)
        assertTrue(privateSpeculativeSession.engineSession.geckoSession.settings.usePrivateMode)

        // Creating a non-private speculative session should affect prepared session
        engine.speculativeCreateSession(private = false)
        assertNotSame(privateSpeculativeSession, engine.speculativeConnectionFactory.speculativeEngineSession)
        // Make sure previous (never used) speculative session is now closed
        assertFalse(privateSpeculativeSession.engineSession.geckoSession.isOpen)
        val regularSpeculativeSession = engine.speculativeConnectionFactory.speculativeEngineSession!!
        assertFalse(regularSpeculativeSession.engineSession.geckoSession.settings.usePrivateMode)
    }

    @Test
    fun clearSpeculativeSession() {
        val engine = GeckoEngine(context, runtime = runtime)
        assertNull(engine.speculativeConnectionFactory.speculativeEngineSession)

        val mockEngineSession: GeckoEngineSession = mock()
        val mockEngineSessionObserver: SpeculativeSessionObserver = mock()
        engine.speculativeConnectionFactory.speculativeEngineSession =
            SpeculativeEngineSession(mockEngineSession, mockEngineSessionObserver)
        engine.clearSpeculativeSession()

        verify(mockEngineSession).unregister(mockEngineSessionObserver)
        verify(mockEngineSession).close()
        assertNull(engine.speculativeConnectionFactory.speculativeEngineSession)
    }

    @Test
    fun `createSession with contextId`() {
        val engine = GeckoEngine(context, runtime = runtime)

        // Create a speculative session with a context id and consume it
        engine.speculativeCreateSession(private = false, contextId = "1")
        assertNotNull(engine.speculativeConnectionFactory.speculativeEngineSession)
        var newSpeculativeSession = engine.speculativeConnectionFactory.speculativeEngineSession!!.engineSession
        assertSame(newSpeculativeSession, engine.createSession(private = false, contextId = "1"))
        assertNull(engine.speculativeConnectionFactory.speculativeEngineSession)

        // Create a regular speculative session and make sure it is not returned
        // if a session with a context id is requested instead.
        engine.speculativeCreateSession(private = false)
        assertNotNull(engine.speculativeConnectionFactory.speculativeEngineSession)
        newSpeculativeSession = engine.speculativeConnectionFactory.speculativeEngineSession!!.engineSession
        assertNotSame(newSpeculativeSession, engine.createSession(private = false, contextId = "1"))
        // Make sure previous (never used) speculative session is now closed
        assertFalse(newSpeculativeSession.geckoSession.isOpen)
        assertNull(engine.speculativeConnectionFactory.speculativeEngineSession)
    }

    @Test
    fun name() {
        assertEquals("Gecko", GeckoEngine(context, runtime = runtime).name())
    }

    @OptIn(ExperimentalGeckoViewApi::class)
    @Test
    fun settings() {
        val defaultSettings = DefaultSettings()
        val contentBlockingSettings = ContentBlocking.Settings.Builder().build()
        val runtime = mock<GeckoRuntime>()
        val runtimeSettings = mock<GeckoRuntimeSettings>()
        whenever(runtimeSettings.javaScriptEnabled).thenReturn(true)
        whenever(runtimeSettings.webFontsEnabled).thenReturn(true)
        whenever(runtimeSettings.automaticFontSizeAdjustment).thenReturn(true)
        whenever(runtimeSettings.fontInflationEnabled).thenReturn(true)
        whenever(runtimeSettings.fontSizeFactor).thenReturn(1.0F)
        whenever(runtimeSettings.forceUserScalableEnabled).thenReturn(false)
        whenever(runtimeSettings.loginAutofillEnabled).thenReturn(false)
        whenever(runtimeSettings.creditCardSecurityCodeEnabled).thenReturn(false)
        whenever(runtimeSettings.enterpriseRootsEnabled).thenReturn(false)
        whenever(runtimeSettings.contentBlocking).thenReturn(contentBlockingSettings)
        whenever(runtimeSettings.preferredColorScheme).thenReturn(GeckoRuntimeSettings.COLOR_SCHEME_SYSTEM)
        whenever(runtime.settings).thenReturn(runtimeSettings)
        val engine = GeckoEngine(context, runtime = runtime, defaultSettings = defaultSettings)

        assertTrue(engine.settings.javascriptEnabled)
        engine.settings.javascriptEnabled = false
        verify(runtimeSettings).javaScriptEnabled = false

        assertFalse(engine.settings.loginAutofillEnabled)
        engine.settings.loginAutofillEnabled = true
        verify(runtimeSettings).loginAutofillEnabled = true

        assertFalse(engine.settings.creditCardSecurityCodeEnabled)
        engine.settings.creditCardSecurityCodeEnabled = true
        verify(runtimeSettings).creditCardSecurityCodeEnabled = true

        assertFalse(engine.settings.enterpriseRootsEnabled)
        engine.settings.enterpriseRootsEnabled = true
        verify(runtimeSettings).enterpriseRootsEnabled = true

        assertTrue(engine.settings.webFontsEnabled)
        engine.settings.webFontsEnabled = false
        verify(runtimeSettings).webFontsEnabled = false

        assertTrue(engine.settings.automaticFontSizeAdjustment)
        engine.settings.automaticFontSizeAdjustment = false
        verify(runtimeSettings).automaticFontSizeAdjustment = false

        assertTrue(engine.settings.fontInflationEnabled!!)
        engine.settings.fontInflationEnabled = null
        verify(runtimeSettings, never()).fontInflationEnabled = anyBoolean()
        engine.settings.fontInflationEnabled = false
        verify(runtimeSettings).fontInflationEnabled = false

        assertEquals(1.0F, engine.settings.fontSizeFactor)
        engine.settings.fontSizeFactor = null
        verify(runtimeSettings, never()).fontSizeFactor = anyFloat()
        engine.settings.fontSizeFactor = 2.0F
        verify(runtimeSettings).fontSizeFactor = 2.0F

        assertFalse(engine.settings.forceUserScalableContent)
        engine.settings.forceUserScalableContent = true
        verify(runtimeSettings).forceUserScalableEnabled = true

        assertFalse(engine.settings.remoteDebuggingEnabled)
        engine.settings.remoteDebuggingEnabled = true
        verify(runtimeSettings).remoteDebuggingEnabled = true

        assertFalse(engine.settings.testingModeEnabled)
        engine.settings.testingModeEnabled = true
        assertTrue(engine.settings.testingModeEnabled)

        assertEquals(PreferredColorScheme.System, engine.settings.preferredColorScheme)
        engine.settings.preferredColorScheme = PreferredColorScheme.Dark
        verify(runtimeSettings).preferredColorScheme = PreferredColorScheme.Dark.toGeckoValue()

        assertFalse(engine.settings.suspendMediaWhenInactive)
        engine.settings.suspendMediaWhenInactive = true
        assertEquals(true, engine.settings.suspendMediaWhenInactive)

        assertNull(engine.settings.clearColor)
        engine.settings.clearColor = Color.BLUE
        assertEquals(Color.BLUE, engine.settings.clearColor)

        // Specifying no ua-string default should result in GeckoView's default.
        assertEquals(GeckoSession.getDefaultUserAgent(), engine.settings.userAgentString)
        // It also should be possible to read and set a new default.
        engine.settings.userAgentString += "-test"
        assertEquals(GeckoSession.getDefaultUserAgent() + "-test", engine.settings.userAgentString)

        assertEquals(null, engine.settings.trackingProtectionPolicy)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.strict()

        val trackingStrictCategories = TrackingProtectionPolicy.strict().trackingCategories.sumOf { it.id }
        val artificialCategory = TrackingCategory.SCRIPTS_AND_SUB_RESOURCES.id
        assertEquals(
            trackingStrictCategories - artificialCategory,
            contentBlockingSettings.antiTrackingCategories,
        )

        assertFalse(engine.settings.emailTrackerBlockingPrivateBrowsing)
        engine.settings.emailTrackerBlockingPrivateBrowsing = true
        assertTrue(engine.settings.emailTrackerBlockingPrivateBrowsing)

        val safeStrictBrowsingCategories = SafeBrowsingPolicy.RECOMMENDED.id
        assertEquals(safeStrictBrowsingCategories, contentBlockingSettings.safeBrowsingCategories)

        engine.settings.safeBrowsingPolicy = arrayOf(SafeBrowsingPolicy.PHISHING)
        assertEquals(SafeBrowsingPolicy.PHISHING.id, contentBlockingSettings.safeBrowsingCategories)

        assertEquals(defaultSettings.trackingProtectionPolicy, TrackingProtectionPolicy.strict())
        assertEquals(contentBlockingSettings.cookieBehavior, CookiePolicy.ACCEPT_FIRST_PARTY_AND_ISOLATE_OTHERS.id)
        assertEquals(
            contentBlockingSettings.cookieBehaviorPrivateMode,
            CookiePolicy.ACCEPT_FIRST_PARTY_AND_ISOLATE_OTHERS.id,
        )

        assertEquals(contentBlockingSettings.queryParameterStrippingEnabled, engine.settings.queryParameterStripping)
        assertEquals(
            contentBlockingSettings.queryParameterStrippingPrivateBrowsingEnabled,
            engine.settings.queryParameterStrippingPrivateBrowsing,
        )
        assertEquals(
            contentBlockingSettings.queryParameterStrippingAllowList[0],
            engine.settings.queryParameterStrippingAllowList,
        )
        assertEquals(
            contentBlockingSettings.queryParameterStrippingStripList[0],
            engine.settings.queryParameterStrippingStripList,
        )
        assertEquals(contentBlockingSettings.contentBlockingDatabaseStatus, engine.settings.useContentBlockingDatabase)
        assertEquals(
            contentBlockingSettings.bounceTrackingProtectionMode,
            EngineSession.BounceTrackingProtectionMode.ENABLED.mode,
        )
        assertEquals(
            contentBlockingSettings.allowListBaselineTrackingProtection,
            (engine.settings.trackingProtectionPolicy as EngineSession.TrackingProtectionPolicyForSessionTypes)
                .allowListBaselineTrackingProtection,
        )
        assertEquals(
            contentBlockingSettings.allowListConvenienceTrackingProtection,
            (engine.settings.trackingProtectionPolicy as EngineSession.TrackingProtectionPolicyForSessionTypes)
                .allowListConvenienceTrackingProtection,
        )

        assertEquals(
            contentBlockingSettings.emailTrackerBlockingPrivateBrowsingEnabled,
            engine.settings.emailTrackerBlockingPrivateBrowsing,
        )

        // Verify safe browsing simulation defaults
        assertFalse(contentBlockingSettings.safeBrowsingGlobalCacheEnabled)
        assertFalse(contentBlockingSettings.safeBrowsingRealTimeEnabled)
        assertFalse(contentBlockingSettings.safeBrowsingRealTimeSimulationEnabled)
        assertEquals(5, contentBlockingSettings.safeBrowsingRealTimeSimulationHitProbability)
        assertEquals(300, contentBlockingSettings.safeBrowsingRealTimeSimulationCacheTTLSec)
        assertFalse(contentBlockingSettings.safeBrowsingRealTimeSimulationNegativeCacheEnabled)
        assertEquals(300, contentBlockingSettings.safeBrowsingRealTimeSimulationNegativeCacheTTLSec)

        try {
            engine.settings.domStorageEnabled
            fail("Expected UnsupportedOperationException")
        } catch (e: UnsupportedSettingException) {
            // Expected
        }

        try {
            engine.settings.domStorageEnabled = false
            fail("Expected UnsupportedOperationException")
        } catch (e: UnsupportedSettingException) {
            // Ignore exception
        }
    }

    @OptIn(ExperimentalGeckoViewApi::class)
    @Test
    fun `WHEN safe browsing simulation settings are set via DefaultSettings THEN they must be propagated to contentBlockingSettings`() {
        val contentBlockingSettings = ContentBlocking.Settings.Builder().build()
        val runtime = mock<GeckoRuntime>()
        val runtimeSettings = mock<GeckoRuntimeSettings>()
        whenever(runtimeSettings.javaScriptEnabled).thenReturn(true)
        whenever(runtime.settings).thenReturn(runtimeSettings)
        whenever(runtimeSettings.contentBlocking).thenReturn(contentBlockingSettings)
        whenever(runtimeSettings.fontInflationEnabled).thenReturn(true)

        val defaultSettings =
            DefaultSettings(
                safeBrowsingRealTimeSimulationHitProbability = 50,
                safeBrowsingRealTimeSimulationCacheTTLSec = 600,
                safeBrowsingRealTimeSimulationNegativeCacheEnabled = true,
                safeBrowsingRealTimeSimulationNegativeCacheTTLSec = 120,
            )
        GeckoEngine(context, defaultSettings, runtime)

        assertEquals(50, contentBlockingSettings.safeBrowsingRealTimeSimulationHitProbability)
        assertEquals(600, contentBlockingSettings.safeBrowsingRealTimeSimulationCacheTTLSec)
        assertTrue(contentBlockingSettings.safeBrowsingRealTimeSimulationNegativeCacheEnabled)
        assertEquals(120, contentBlockingSettings.safeBrowsingRealTimeSimulationNegativeCacheTTLSec)
    }

    @Test
    fun `the SCRIPTS_AND_SUB_RESOURCES tracking protection category must not be passed to gecko view`() {
        val mockRuntime = mock<GeckoRuntime>()
        val settings = spy(ContentBlocking.Settings.Builder().build())
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(settings)

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.strict()

        val trackingStrictCategories = TrackingProtectionPolicy.strict().trackingCategories.sumOf { it.id }
        val artificialCategory = TrackingCategory.SCRIPTS_AND_SUB_RESOURCES.id

        assertEquals(
            trackingStrictCategories - artificialCategory,
            mockRuntime.settings.contentBlocking.antiTrackingCategories,
        )

        mockRuntime.settings.contentBlocking.setAntiTracking(0)

        engine.settings.trackingProtectionPolicy =
            TrackingProtectionPolicy.select(arrayOf(TrackingCategory.SCRIPTS_AND_SUB_RESOURCES))

        assertEquals(0, mockRuntime.settings.contentBlocking.antiTrackingCategories)
    }

    @Test
    fun `WHEN a strict tracking protection policy is set THEN the strict social list must be activated`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.strict()

        verify(mockRuntime.settings.contentBlocking).setStrictSocialTrackingProtection(true)
    }

    @Test
    fun `WHEN a strict tracking protection policy is set THEN the setEnhancedTrackingProtectionLevel must be STRICT`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.strict()

        verify(mockRuntime.settings.contentBlocking).setEnhancedTrackingProtectionLevel(ContentBlocking.EtpLevel.STRICT)
    }

    @Test
    fun `WHEN a recommended tracking protection policy is set THEN Bounce Tracking Protection must be in dry run mode`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.recommended()

        verify(mockRuntime.settings.contentBlocking)
            .setBounceTrackingProtectionMode(EngineSession.BounceTrackingProtectionMode.ENABLED_DRY_RUN.mode)
    }

    @Test
    fun `WHEN a strict tracking protection policy is set THEN Bounce Tracking Protection must be enabled`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.strict()

        verify(mockRuntime.settings.contentBlocking)
            .setBounceTrackingProtectionMode(EngineSession.BounceTrackingProtectionMode.ENABLED.mode)
    }

    @Test
    fun `WHEN a strict tracking protection policy is set THEN fingerprinting protection is enabled on the runtime`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.strict()

        verify(mockRuntime.settings).setFingerprintingProtection(true)
        verify(mockRuntime.settings).setFingerprintingProtectionPrivateBrowsing(true)
    }

    @Test
    fun `WHEN a recommended tracking protection policy is set THEN fingerprinting protection matches the standard defaults`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.recommended()

        verify(mockRuntime.settings).setFingerprintingProtection(false)
        verify(mockRuntime.settings).setFingerprintingProtectionPrivateBrowsing(true)
    }

    @Test
    fun `WHEN a policy without fingerprinting protection is set THEN the fingerprinting protection prefs are left untouched`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.select()

        verify(mockRuntime.settings, never()).setFingerprintingProtection(anyBoolean())
        verify(mockRuntime.settings, never()).setFingerprintingProtectionPrivateBrowsing(anyBoolean())
    }

    @Test
    fun `WHEN a custom tracking protection policy is set THEN Bounce Tracking Protection must be in dry run mode`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy =
            TrackingProtectionPolicy.select(
                // Set only an unrelated setting.
                strictSocialTrackingProtection = true
            )

        verify(mockRuntime.settings.contentBlocking)
            .setBounceTrackingProtectionMode(EngineSession.BounceTrackingProtectionMode.ENABLED_DRY_RUN.mode)
    }

    @Test
    fun `WHEN a custom tracking protection policy enables BTP THEN Bounce Tracking Protection must be enabled`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy =
            TrackingProtectionPolicy.select(
                // Set only an unrelated setting.
                strictSocialTrackingProtection = true,
                bounceTrackingProtectionMode = EngineSession.BounceTrackingProtectionMode.ENABLED,
            )

        verify(mockRuntime.settings.contentBlocking)
            .setBounceTrackingProtectionMode(EngineSession.BounceTrackingProtectionMode.ENABLED.mode)
    }

    @Test
    fun `WHEN a none tracking protection policy is set THEN Bounce Tracking Protection must be in standby mode`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.none()

        verify(mockRuntime.settings.contentBlocking)
            .setBounceTrackingProtectionMode(EngineSession.BounceTrackingProtectionMode.ENABLED_STANDBY.mode)
    }

    @Test
    fun `WHEN a recommended tracking protection policy is set THEN Allow List baseline and convenience must be true`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.recommended()

        val policy = engine.settings.trackingProtectionPolicy as EngineSession.TrackingProtectionPolicyForSessionTypes
        assertTrue(policy.allowListBaselineTrackingProtection)
        assertTrue(policy.allowListConvenienceTrackingProtection)
    }

    @Test
    fun `WHEN a strict tracking protection policy is set THEN Allow List baseline must be true and convenience must be false by default`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.strict()

        val policy = engine.settings.trackingProtectionPolicy as EngineSession.TrackingProtectionPolicyForSessionTypes
        assertTrue(policy.allowListBaselineTrackingProtection)
        assertFalse(policy.allowListConvenienceTrackingProtection)
    }

    @Test
    fun `WHEN a custom tracking protection policy is set THEN Allow List baseline must be true and convenience must be false by default`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.select()

        val policy = engine.settings.trackingProtectionPolicy as EngineSession.TrackingProtectionPolicyForSessionTypes
        assertTrue(policy.allowListBaselineTrackingProtection)
        assertFalse(policy.allowListConvenienceTrackingProtection)
    }

    @Test
    fun `WHEN an HTTPS-Only mode is set THEN allowInsecureConnections is getting set on GeckoRuntime`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        reset(mockRuntime.settings)
        engine.settings.httpsOnlyMode = Engine.HttpsOnlyMode.ENABLED_PRIVATE_ONLY
        verify(mockRuntime.settings).allowInsecureConnections = GeckoRuntimeSettings.HTTPS_ONLY_PRIVATE

        reset(mockRuntime.settings)
        engine.settings.httpsOnlyMode = Engine.HttpsOnlyMode.ENABLED
        verify(mockRuntime.settings).allowInsecureConnections = GeckoRuntimeSettings.HTTPS_ONLY

        reset(mockRuntime.settings)
        engine.settings.httpsOnlyMode = Engine.HttpsOnlyMode.DISABLED
        verify(mockRuntime.settings).allowInsecureConnections = GeckoRuntimeSettings.ALLOW_ALL
    }

    @Test
    fun `setAntiTracking is only invoked when the value is changed`() {
        val mockRuntime = mock<GeckoRuntime>()
        val settings = spy(ContentBlocking.Settings.Builder().build())
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(settings)

        val engine = GeckoEngine(testContext, runtime = mockRuntime)
        val policy = TrackingProtectionPolicy.recommended()

        engine.settings.trackingProtectionPolicy = policy

        verify(mockRuntime.settings.contentBlocking).setAntiTracking(policy.getAntiTrackingPolicy())

        reset(settings)

        engine.settings.trackingProtectionPolicy = policy

        verify(mockRuntime.settings.contentBlocking, never()).setAntiTracking(policy.getAntiTrackingPolicy())
    }

    @Test
    fun `cookiePurging is only invoked when the value is changed`() {
        val mockRuntime = mock<GeckoRuntime>()
        val settings = spy(ContentBlocking.Settings.Builder().build())
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(settings)

        val engine = GeckoEngine(testContext, runtime = mockRuntime)
        val policy = TrackingProtectionPolicy.recommended()

        engine.settings.trackingProtectionPolicy = policy

        verify(mockRuntime.settings.contentBlocking).setCookiePurging(policy.cookiePurging)

        reset(settings)

        engine.settings.trackingProtectionPolicy = policy

        verify(mockRuntime.settings.contentBlocking, never()).setCookiePurging(policy.cookiePurging)
    }

    @Test
    fun `setCookieBehavior is only invoked when the value is changed`() {
        val mockRuntime = mock<GeckoRuntime>()
        val settings = spy(ContentBlocking.Settings.Builder().build())
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(settings)
        whenever(mockRuntime.settings.contentBlocking.cookieBehavior).thenReturn(CookieBehavior.ACCEPT_NONE)

        val engine = GeckoEngine(testContext, runtime = mockRuntime)
        val policy = TrackingProtectionPolicy.recommended()

        engine.settings.trackingProtectionPolicy = policy

        verify(mockRuntime.settings.contentBlocking).setCookieBehavior(policy.cookiePolicy.id)

        reset(settings)

        engine.settings.trackingProtectionPolicy = policy

        verify(mockRuntime.settings.contentBlocking, never()).setCookieBehavior(policy.cookiePolicy.id)
    }

    @Test
    fun `setCookieBehavior private mode is only invoked when the value is changed`() {
        val mockRuntime = mock<GeckoRuntime>()
        val settings = spy(ContentBlocking.Settings.Builder().build())
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(settings)
        whenever(mockRuntime.settings.contentBlocking.cookieBehaviorPrivateMode).thenReturn(CookieBehavior.ACCEPT_NONE)

        val engine = GeckoEngine(testContext, runtime = mockRuntime)
        val policy = TrackingProtectionPolicy.recommended()

        engine.settings.trackingProtectionPolicy = policy

        verify(mockRuntime.settings.contentBlocking).setCookieBehaviorPrivateMode(policy.cookiePolicy.id)

        reset(settings)

        engine.settings.trackingProtectionPolicy = policy

        verify(mockRuntime.settings.contentBlocking, never()).setCookieBehaviorPrivateMode(policy.cookiePolicy.id)
    }

    @Test
    fun `setQueryParameterStripping is only invoked when the value is changed`() {
        val mockRuntime = mock<GeckoRuntime>()
        val settings = spy(ContentBlocking.Settings.Builder().build())
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(settings)

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.queryParameterStripping = true

        verify(mockRuntime.settings.contentBlocking).setQueryParameterStrippingEnabled(true)

        reset(settings)

        engine.settings.queryParameterStripping = true

        verify(mockRuntime.settings.contentBlocking, never()).setQueryParameterStrippingEnabled(true)
    }

    @Test
    fun `setQueryParameterStrippingPrivateBrowsingEnabled is only invoked when the value is changed`() {
        val mockRuntime = mock<GeckoRuntime>()
        val settings = spy(ContentBlocking.Settings.Builder().build())
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(settings)

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.queryParameterStrippingPrivateBrowsing = true

        verify(mockRuntime.settings.contentBlocking).setQueryParameterStrippingPrivateBrowsingEnabled(true)

        reset(settings)

        engine.settings.queryParameterStrippingPrivateBrowsing = true

        verify(mockRuntime.settings.contentBlocking, never()).setQueryParameterStrippingPrivateBrowsingEnabled(true)
    }

    @Test
    fun `emailTrackerBlockingPrivateBrowsing is only invoked with the value is changed`() {
        val mockRuntime = mock<GeckoRuntime>()
        val settings = spy(ContentBlocking.Settings.Builder().build())
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(settings)

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.emailTrackerBlockingPrivateBrowsing = true

        verify(mockRuntime.settings.contentBlocking).setEmailTrackerBlockingPrivateBrowsing(true)

        reset(settings)

        engine.settings.emailTrackerBlockingPrivateBrowsing = true

        verify(mockRuntime.settings.contentBlocking, never()).setEmailTrackerBlockingPrivateBrowsing(true)
    }

    @Test
    fun `setEnhancedTrackingProtectionLevel MUST reflect the tracking protection policy`() {
        val mockRuntime = mock<GeckoRuntime>()
        val settings = spy(ContentBlocking.Settings.Builder().build())
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(settings)

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.recommended()

        verify(mockRuntime.settings.contentBlocking)
            .setEnhancedTrackingProtectionLevel(ContentBlocking.EtpLevel.DEFAULT)

        reset(settings)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.recommended()

        verify(mockRuntime.settings.contentBlocking, never())
            .setEnhancedTrackingProtectionLevel(ContentBlocking.EtpLevel.DEFAULT)

        reset(settings)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.strict()

        verify(mockRuntime.settings.contentBlocking).setEnhancedTrackingProtectionLevel(ContentBlocking.EtpLevel.STRICT)

        reset(settings)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.none()
        verify(mockRuntime.settings.contentBlocking).setEnhancedTrackingProtectionLevel(ContentBlocking.EtpLevel.NONE)

        reset(settings)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.none()
        verify(mockRuntime.settings.contentBlocking, never())
            .setEnhancedTrackingProtectionLevel(ContentBlocking.EtpLevel.NONE)

        reset(settings)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.strict()

        verify(mockRuntime.settings.contentBlocking).setEnhancedTrackingProtectionLevel(ContentBlocking.EtpLevel.STRICT)
    }

    @Test
    fun `WHEN a non strict tracking protection policy is set THEN the strict social list must be disabled`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking.strictSocialTrackingProtection).thenReturn(true)

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.recommended()

        verify(mockRuntime.settings.contentBlocking).setStrictSocialTrackingProtection(false)
    }

    @Test
    fun `WHEN strict social tracking protection is set to true THEN the strict social list must be activated`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy =
            TrackingProtectionPolicy.select(strictSocialTrackingProtection = true)

        verify(mockRuntime.settings.contentBlocking).setStrictSocialTrackingProtection(true)
    }

    @Test
    fun `WHEN strict social tracking protection is set to false THEN the strict social list must be disabled`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking.strictSocialTrackingProtection).thenReturn(true)

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.trackingProtectionPolicy =
            TrackingProtectionPolicy.select(strictSocialTrackingProtection = false)

        verify(mockRuntime.settings.contentBlocking).setStrictSocialTrackingProtection(false)
    }

    @Test
    fun `WHEN using the content blocking database is changed THEN update this value in content blocking settings`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())
        whenever(mockRuntime.settings.contentBlocking).thenReturn(mock())
        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        engine.settings.useContentBlockingDatabase = true

        verify(mockRuntime.settings.contentBlocking).setContentBlockingDatabaseStatus(true)
    }

    @Test
    fun defaultSettings() {
        val runtime = mock<GeckoRuntime>()
        val runtimeSettings = mock<GeckoRuntimeSettings>()
        val contentBlockingSettings = ContentBlocking.Settings.Builder().build()
        whenever(runtimeSettings.javaScriptEnabled).thenReturn(true)
        whenever(runtime.settings).thenReturn(runtimeSettings)
        whenever(runtimeSettings.contentBlocking).thenReturn(contentBlockingSettings)
        whenever(runtimeSettings.fontInflationEnabled).thenReturn(true)

        val engine =
            GeckoEngine(
                context,
                DefaultSettings(
                    trackingProtectionPolicy = TrackingProtectionPolicy.strict(),
                    javascriptEnabled = false,
                    webFontsEnabled = false,
                    automaticFontSizeAdjustment = false,
                    fontInflationEnabled = false,
                    fontSizeFactor = 2.0F,
                    remoteDebuggingEnabled = true,
                    testingModeEnabled = true,
                    userAgentString = "test-ua",
                    preferredColorScheme = PreferredColorScheme.Light,
                    suspendMediaWhenInactive = true,
                    forceUserScalableContent = false,
                ),
                runtime,
            )

        verify(runtimeSettings).javaScriptEnabled = false
        verify(runtimeSettings).webFontsEnabled = false
        verify(runtimeSettings).automaticFontSizeAdjustment = false
        verify(runtimeSettings).fontInflationEnabled = false
        verify(runtimeSettings).fontSizeFactor = 2.0F
        verify(runtimeSettings).remoteDebuggingEnabled = true
        verify(runtimeSettings).forceUserScalableEnabled = false

        val trackingStrictCategories = TrackingProtectionPolicy.strict().trackingCategories.sumOf { it.id }
        val artificialCategory = TrackingCategory.SCRIPTS_AND_SUB_RESOURCES.id
        assertEquals(
            trackingStrictCategories - artificialCategory,
            contentBlockingSettings.antiTrackingCategories,
        )

        assertEquals(SafeBrowsingPolicy.RECOMMENDED.id, contentBlockingSettings.safeBrowsingCategories)

        assertEquals(CookiePolicy.ACCEPT_FIRST_PARTY_AND_ISOLATE_OTHERS.id, contentBlockingSettings.cookieBehavior)
        assertEquals(
            CookiePolicy.ACCEPT_FIRST_PARTY_AND_ISOLATE_OTHERS.id,
            contentBlockingSettings.cookieBehaviorPrivateMode,
        )
        assertTrue(engine.settings.testingModeEnabled)
        assertEquals("test-ua", engine.settings.userAgentString)
        assertEquals(PreferredColorScheme.Light, engine.settings.preferredColorScheme)
        assertTrue(engine.settings.suspendMediaWhenInactive)

        engine.settings.safeBrowsingPolicy = arrayOf(SafeBrowsingPolicy.PHISHING)
        engine.settings.trackingProtectionPolicy =
            TrackingProtectionPolicy.select(
                trackingCategories = arrayOf(TrackingCategory.AD),
                cookiePolicy = CookiePolicy.ACCEPT_ONLY_FIRST_PARTY,
            )

        assertEquals(
            TrackingCategory.AD.id,
            contentBlockingSettings.antiTrackingCategories,
        )

        assertEquals(
            SafeBrowsingPolicy.PHISHING.id,
            contentBlockingSettings.safeBrowsingCategories,
        )

        assertEquals(
            CookiePolicy.ACCEPT_ONLY_FIRST_PARTY.id,
            contentBlockingSettings.cookieBehavior,
        )

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.none()

        assertEquals(CookiePolicy.ACCEPT_ALL.id, contentBlockingSettings.cookieBehavior)
    }

    @Test
    fun `speculativeConnect forwards call to executor`() {
        val executor: GeckoWebExecutor = mock()

        val engine = GeckoEngine(context, runtime = runtime, executorProvider = { executor })

        engine.speculativeConnect("https://www.mozilla.org")

        verify(executor).speculativeConnect("https://www.mozilla.org")
    }

    @Test
    fun `install built-in web extension successfully`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "resource://android/assets/extensions/test"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        var onSuccessCalled = false
        var onErrorCalled = false
        val result = GeckoResult<GeckoWebExtension>()

        whenever(extensionController.ensureBuiltIn(extUrl, extId)).thenReturn(result)
        engine.installBuiltInWebExtension(
            extId,
            extUrl,
            onSuccess = { onSuccessCalled = true },
            onError = { _ -> onErrorCalled = true },
        )
        result.complete(mockNativeWebExtension(extId, extUrl))

        shadowOf(getMainLooper()).idle()

        val extUrlCaptor = argumentCaptor<String>()
        val extIdCaptor = argumentCaptor<String>()
        verify(extensionController).ensureBuiltIn(extUrlCaptor.capture(), extIdCaptor.capture())
        assertEquals(extUrl, extUrlCaptor.value)
        assertEquals(extId, extIdCaptor.value)
        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `add optional permissions to a web extension successfully`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "resource://android/assets/extensions/test"
        val permissions = listOf("permission1")
        val origins = listOf("origin")
        val dataCollectionPermissions = listOf("data")

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        var onSuccessCalled = false
        var onErrorCalled = false
        val result = GeckoResult<GeckoWebExtension>()

        whenever(
                extensionController.addOptionalPermissions(
                    extId,
                    permissions.toTypedArray(),
                    origins.toTypedArray(),
                    dataCollectionPermissions.toTypedArray(),
                )
            )
            .thenReturn(result)
        engine.addOptionalPermissions(
            extId,
            permissions,
            origins,
            dataCollectionPermissions,
            onSuccess = { onSuccessCalled = true },
            onError = { _ -> onErrorCalled = true },
        )
        result.complete(mockNativeWebExtension(extId, extUrl))

        shadowOf(getMainLooper()).idle()

        verify(extensionController).addOptionalPermissions(anyString(), any(), any(), any())
        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `addOptionalPermissions with empty permissions and origins with `() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val engine = GeckoEngine(context, runtime = runtime)
        var onErrorCalled = false

        engine.addOptionalPermissions(
            extId,
            emptyList(),
            emptyList(),
            onError = { _ -> onErrorCalled = true },
        )

        shadowOf(getMainLooper()).idle()

        assertTrue(onErrorCalled)
    }

    @Test
    fun `remove optional permissions to a web extension successfully`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "resource://android/assets/extensions/test"
        val permissions = listOf("permission1")
        val origins = listOf("origin")
        val dataCollectionPermissions = listOf("data")

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        var onSuccessCalled = false
        var onErrorCalled = false
        val result = GeckoResult<GeckoWebExtension>()

        whenever(
                extensionController.removeOptionalPermissions(
                    extId,
                    permissions.toTypedArray(),
                    origins.toTypedArray(),
                    dataCollectionPermissions.toTypedArray(),
                )
            )
            .thenReturn(result)
        engine.removeOptionalPermissions(
            extId,
            permissions,
            origins,
            dataCollectionPermissions,
            onSuccess = { onSuccessCalled = true },
            onError = { _ -> onErrorCalled = true },
        )
        result.complete(mockNativeWebExtension(extId, extUrl))

        shadowOf(getMainLooper()).idle()

        verify(extensionController).removeOptionalPermissions(anyString(), any(), any(), any())
        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `removeOptionalPermissions with empty permissions and origins with `() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val engine = GeckoEngine(context, runtime = runtime)
        var onErrorCalled = false

        engine.removeOptionalPermissions(
            extId,
            emptyList(),
            emptyList(),
            onError = { _ -> onErrorCalled = true },
        )

        shadowOf(getMainLooper()).idle()

        assertTrue(onErrorCalled)
    }

    @Test
    fun `install external web extension successfully`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "https://addons.mozilla.org/firefox/downloads/file/123/some_web_ext.xpi"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        var onSuccessCalled = false
        var onErrorCalled = false
        val result = GeckoResult<GeckoWebExtension>()

        whenever(extensionController.install(any(), any())).thenReturn(result)
        engine.installWebExtension(
            extUrl,
            onSuccess = { onSuccessCalled = true },
            onError = { _ -> onErrorCalled = true },
        )
        result.complete(mockNativeWebExtension(extId, extUrl))

        shadowOf(getMainLooper()).idle()

        val extCaptor = argumentCaptor<String>()
        verify(extensionController).install(extCaptor.capture(), any())
        assertEquals(extUrl, extCaptor.value)
        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `install built-in web extension failure`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "resource://android/assets/extensions/test"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        var onErrorCalled = false
        val expected = IOException()
        val result = GeckoResult<GeckoWebExtension>()

        var throwable: Throwable? = null
        whenever(extensionController.ensureBuiltIn(extUrl, extId)).thenReturn(result)
        engine.installBuiltInWebExtension(extId, extUrl) { e ->
            onErrorCalled = true
            throwable = e
        }
        result.completeExceptionally(expected)

        shadowOf(getMainLooper()).idle()

        assertTrue(onErrorCalled)
        assertIs<GeckoWebExtensionException>(throwable)
    }

    @Test
    fun `install external web extension failure`() {
        val runtime = mock<GeckoRuntime>()
        val extUrl = "https://addons.mozilla.org/firefox/downloads/file/123/some_web_ext.xpi"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        var onErrorCalled = false
        val expected = IOException()
        val result = GeckoResult<GeckoWebExtension>()

        var throwable: Throwable? = null
        whenever(extensionController.install(any(), any())).thenReturn(result)
        engine.installWebExtension(extUrl) { e ->
            onErrorCalled = true
            throwable = e
        }
        result.completeExceptionally(expected)

        shadowOf(getMainLooper()).idle()

        assertTrue(onErrorCalled)
        assertIs<GeckoWebExtensionException>(throwable)
    }

    @Test
    fun `install web extension with installation method manager`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "https://addons.mozilla.org/firefox/downloads/file/123/some_web_ext.xpi"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val result = GeckoResult<GeckoWebExtension>()

        whenever(extensionController.install(any(), any())).thenReturn(result)

        engine.installWebExtension(
            extUrl,
            InstallationMethod.MANAGER,
        )

        result.complete(mockNativeWebExtension(extId, extUrl))

        shadowOf(getMainLooper()).idle()

        val methodCaptor = argumentCaptor<String>()

        verify(extensionController).install(any(), methodCaptor.capture())

        assertEquals(WebExtensionController.INSTALLATION_METHOD_MANAGER, methodCaptor.value)
    }

    @Test
    fun `install web extension with installation method file`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "https://addons.mozilla.org/firefox/downloads/file/123/some_web_ext.xpi"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val result = GeckoResult<GeckoWebExtension>()

        whenever(extensionController.install(any(), any())).thenReturn(result)

        engine.installWebExtension(
            extUrl,
            InstallationMethod.FROM_FILE,
        )

        result.complete(mockNativeWebExtension(extId, extUrl))

        shadowOf(getMainLooper()).idle()

        val methodCaptor = argumentCaptor<String>()

        verify(extensionController).install(any(), methodCaptor.capture())

        assertEquals(WebExtensionController.INSTALLATION_METHOD_FROM_FILE, methodCaptor.value)
    }

    @Test
    fun `install web extension with null installation method`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "https://addons.mozilla.org/firefox/downloads/file/123/some_web_ext.xpi"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val result = GeckoResult<GeckoWebExtension>()

        whenever(extensionController.install(any(), any())).thenReturn(result)

        engine.installWebExtension(
            extUrl,
            null,
        )

        result.complete(mockNativeWebExtension(extId, extUrl))

        shadowOf(getMainLooper()).idle()

        val methodCaptor = argumentCaptor<String>()

        verify(extensionController).install(any(), methodCaptor.capture())

        assertNull(methodCaptor.value)
    }

    @Test(expected = IllegalArgumentException::class)
    fun `installWebExtension should throw when a resource URL is passed`() {
        val engine = GeckoEngine(context, runtime = mock())
        engine.installWebExtension("resource://android/assets/extensions/test")
    }

    @Test(expected = IllegalArgumentException::class)
    fun `installBuiltInWebExtension should throw when a non-resource URL is passed`() {
        val engine = GeckoEngine(context, runtime = mock())
        engine.installBuiltInWebExtension(id = "id", url = "https://addons.mozilla.org/1/some_web_ext.xpi")
    }

    @Test
    fun `uninstall web extension successfully`() {
        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val nativeExtension = mockNativeWebExtension("test-webext", "https://addons.mozilla.org/1/some_web_ext.xpi")
        val ext =
            mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension(
                nativeExtension,
                runtime,
            )

        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        var onSuccessCalled = false
        var onErrorCalled = false
        val result = GeckoResult<Void>()

        whenever(extensionController.uninstall(any())).thenReturn(result)
        engine.uninstallWebExtension(
            ext,
            onSuccess = { onSuccessCalled = true },
            onError = { _, _ -> onErrorCalled = true },
        )
        result.complete(null)

        shadowOf(getMainLooper()).idle()

        val extCaptor = argumentCaptor<GeckoWebExtension>()
        verify(extensionController).uninstall(extCaptor.capture())
        assertSame(nativeExtension, extCaptor.value)
        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `uninstall web extension failure`() {
        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val nativeExtension =
            mockNativeWebExtension(
                "test-webext",
                "https://addons.mozilla.org/firefox/downloads/file/123/some_web_ext.xpi",
            )
        val ext =
            mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension(
                nativeExtension,
                runtime,
            )

        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        var onErrorCalled = false
        val expected = IOException()
        val result = GeckoResult<Void>()

        var throwable: Throwable? = null
        whenever(extensionController.uninstall(any())).thenReturn(result)
        engine.uninstallWebExtension(ext) { _, e ->
            onErrorCalled = true
            throwable = e
        }
        result.completeExceptionally(expected)

        shadowOf(getMainLooper()).idle()

        assertTrue(onErrorCalled)
        assertEquals(expected, throwable)
    }

    @Test
    fun `web extension delegate handles installation of built-in extensions`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val extId = "test-webext"
        val extUrl = "resource://android/assets/extensions/test"
        val result = GeckoResult<GeckoWebExtension>()
        whenever(webExtensionController.ensureBuiltIn(extUrl, extId)).thenReturn(result)
        engine.installBuiltInWebExtension(extId, extUrl)
        result.complete(mockNativeWebExtension(extId, extUrl))

        shadowOf(getMainLooper()).idle()

        val extCaptor = argumentCaptor<WebExtension>()
        verify(webExtensionsDelegate).onInstalled(extCaptor.capture())
        assertEquals(extId, extCaptor.value.id)
        assertEquals(extUrl, extCaptor.value.url)
    }

    @Test
    fun `web extension delegate handles installation of external extensions`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val extId = "test-webext"
        val extUrl = "https://addons.mozilla.org/firefox/downloads/123/some_web_ext.xpi"
        val result = GeckoResult<GeckoWebExtension>()
        whenever(webExtensionController.install(any(), any())).thenReturn(result)
        engine.installWebExtension(extUrl)
        result.complete(mockNativeWebExtension(extId, extUrl))

        shadowOf(getMainLooper()).idle()

        val extCaptor = argumentCaptor<WebExtension>()
        verify(webExtensionsDelegate).onInstalled(extCaptor.capture())
        assertEquals(extId, extCaptor.value.id)
        assertEquals(extUrl, extCaptor.value.url)
    }

    @Test
    fun `GIVEN approved permissions prompt WHEN onInstallPermissionRequest THEN delegate is called with allow`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("test", "uri")
        val permissions = arrayOf("some", "permissions")
        val origins = arrayOf("and some", "origins")
        val dataCollectionPermissions = arrayOf("some", "data", "collection", "perms")
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)

        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.PromptDelegate>()
        verify(webExtensionController).promptDelegate = geckoDelegateCaptor.capture()

        val result =
            geckoDelegateCaptor.value.onInstallPromptRequest(extension, permissions, origins, dataCollectionPermissions)

        val extensionCaptor = argumentCaptor<WebExtension>()
        val onConfirmCaptor = argumentCaptor<((PermissionPromptResponse) -> Unit)>()

        verify(webExtensionsDelegate)
            .onInstallPermissionRequest(
                extensionCaptor.capture(),
                eq(permissions.asList()),
                eq(origins.asList()),
                eq(dataCollectionPermissions.asList()),
                onConfirmCaptor.capture(),
            )

        onConfirmCaptor.value(
            PermissionPromptResponse(
                isPermissionsGranted = true,
                isPrivateModeGranted = false,
                isTechnicalAndInteractionDataGranted = false,
            )
        )

        var nativePermissionPromptResponse: NativePermissionPromptResponse? = null
        result!!.accept {
            nativePermissionPromptResponse = it
        }

        shadowOf(getMainLooper()).idle()
        assertTrue(nativePermissionPromptResponse!!.isPermissionsGranted!!)
        assertFalse(nativePermissionPromptResponse!!.isPrivateModeGranted!!)
        assertFalse(nativePermissionPromptResponse!!.isTechnicalAndInteractionDataGranted!!)
    }

    @Test
    fun `GIVEN permissions granted AND private mode granted AND technicalAndInteraction data granted WHEN onInstallPermissionRequest THEN delegate is called with all modes allowed`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("test", "uri")
        val permissions = arrayOf("some", "permissions")
        val origins = arrayOf("and some", "origins")
        val dataCollectionPermissions = arrayOf("some", "data", "collection", "perms")
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)

        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.PromptDelegate>()
        verify(webExtensionController).promptDelegate = geckoDelegateCaptor.capture()

        val result =
            geckoDelegateCaptor.value.onInstallPromptRequest(extension, permissions, origins, dataCollectionPermissions)

        val extensionCaptor = argumentCaptor<WebExtension>()
        val onConfirmCaptor = argumentCaptor<((PermissionPromptResponse) -> Unit)>()

        verify(webExtensionsDelegate)
            .onInstallPermissionRequest(
                extensionCaptor.capture(),
                eq(permissions.asList()),
                eq(origins.asList()),
                eq(dataCollectionPermissions.asList()),
                onConfirmCaptor.capture(),
            )

        onConfirmCaptor.value(
            PermissionPromptResponse(
                isPermissionsGranted = true,
                isPrivateModeGranted = true,
                isTechnicalAndInteractionDataGranted = true,
            )
        )

        var nativePermissionPromptResponse: NativePermissionPromptResponse? = null
        result!!.accept {
            nativePermissionPromptResponse = it
        }

        shadowOf(getMainLooper()).idle()
        assertTrue(nativePermissionPromptResponse!!.isPermissionsGranted!!)
        assertTrue(nativePermissionPromptResponse!!.isPrivateModeGranted!!)
        assertTrue(nativePermissionPromptResponse!!.isTechnicalAndInteractionDataGranted!!)
    }

    @Test
    fun `GIVEN denied permissions prompt WHEN onInstallPermissionRequest THEN delegate is called with deny`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("test", "uri")
        val permissions = arrayOf("some", "permissions")
        val origins = arrayOf("and some", "origins")
        val dataCollectionPermissions = arrayOf("some", "data", "collection", "perms")
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)

        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.PromptDelegate>()
        verify(webExtensionController).promptDelegate = geckoDelegateCaptor.capture()

        val result =
            geckoDelegateCaptor.value.onInstallPromptRequest(extension, permissions, origins, dataCollectionPermissions)

        val extensionCaptor = argumentCaptor<WebExtension>()
        val onConfirmCaptor = argumentCaptor<((PermissionPromptResponse) -> Unit)>()

        verify(webExtensionsDelegate)
            .onInstallPermissionRequest(
                extensionCaptor.capture(),
                eq(permissions.asList()),
                eq(origins.asList()),
                eq(dataCollectionPermissions.asList()),
                onConfirmCaptor.capture(),
            )

        onConfirmCaptor.value(
            PermissionPromptResponse(
                isPermissionsGranted = false,
                isPrivateModeGranted = false,
                isTechnicalAndInteractionDataGranted = false,
            )
        )

        var nativePermissionPromptResponse: NativePermissionPromptResponse? = null
        result!!.accept {
            nativePermissionPromptResponse = it
        }

        shadowOf(getMainLooper()).idle()
        assertFalse(nativePermissionPromptResponse!!.isPermissionsGranted!!)
        assertFalse(nativePermissionPromptResponse!!.isPrivateModeGranted!!)
        assertFalse(nativePermissionPromptResponse!!.isTechnicalAndInteractionDataGranted!!)
    }

    @Test
    fun `web extension delegate handles update prompt`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("test", "uri")
        val permissions = arrayOf("p1", "p2")
        val origins = arrayOf("p3", "p4")
        val dataCollectionPermissions = arrayOf("p5")
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.PromptDelegate>()
        verify(webExtensionController).promptDelegate = geckoDelegateCaptor.capture()

        val result =
            geckoDelegateCaptor.value.onUpdatePrompt(
                extension,
                permissions,
                origins,
                dataCollectionPermissions,
            )
        assertNotNull(result)

        val extensionCaptor = argumentCaptor<WebExtension>()
        val onPermissionsGrantedCaptor = argumentCaptor<((Boolean) -> Unit)>()
        verify(webExtensionsDelegate)
            .onUpdatePermissionRequest(
                extensionCaptor.capture(),
                eq(permissions.toList()),
                eq(origins.toList()),
                eq(dataCollectionPermissions.toList()),
                onPermissionsGrantedCaptor.capture(),
            )
        val ext = extensionCaptor.value as mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
        assertEquals(extension, ext.nativeExtension)

        onPermissionsGrantedCaptor.value.invoke(true)
        assertEquals(GeckoResult.allow(), result)
    }

    @Test
    fun `web extension delegate handles update prompt with empty host permissions`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("testUpdated", "uri")
        val permissions = arrayOf("p1", "p2")
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.PromptDelegate>()
        verify(webExtensionController).promptDelegate = geckoDelegateCaptor.capture()

        val result =
            geckoDelegateCaptor.value.onUpdatePrompt(
                extension,
                permissions,
                emptyArray(),
                emptyArray(),
            )
        assertNotNull(result)

        val extensionCaptor = argumentCaptor<WebExtension>()
        val onPermissionsGrantedCaptor = argumentCaptor<((Boolean) -> Unit)>()
        verify(webExtensionsDelegate)
            .onUpdatePermissionRequest(
                extensionCaptor.capture(),
                eq(permissions.toList()),
                eq(emptyList()),
                eq(emptyList()),
                onPermissionsGrantedCaptor.capture(),
            )
        val ext = extensionCaptor.value as mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
        assertEquals(extension, ext.nativeExtension)

        onPermissionsGrantedCaptor.value.invoke(true)
        assertEquals(GeckoResult.allow(), result)
    }

    @Test
    fun `web extension delegate handles optional permissions prompt - allow`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("test", "uri")
        val permissions = arrayOf("p1", "p2")
        val origins = arrayOf("p3", "p4")
        val dataCollectionPermissions = arrayOf("p5", "p6")
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.PromptDelegate>()
        verify(webExtensionController).promptDelegate = geckoDelegateCaptor.capture()

        val result =
            geckoDelegateCaptor.value.onOptionalPrompt(extension, permissions, origins, dataCollectionPermissions)
        assertNotNull(result)

        val extensionCaptor = argumentCaptor<WebExtension>()
        val onPermissionsGrantedCaptor = argumentCaptor<((Boolean) -> Unit)>()
        verify(webExtensionsDelegate)
            .onOptionalPermissionsRequest(
                extensionCaptor.capture(),
                eq(permissions.toList()),
                eq(origins.toList()),
                eq(dataCollectionPermissions.toList()),
                onPermissionsGrantedCaptor.capture(),
            )
        val current = extensionCaptor.value as mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
        assertEquals(extension, current.nativeExtension)

        onPermissionsGrantedCaptor.value.invoke(true)
        assertEquals(GeckoResult.allow(), result)
    }

    @Test
    fun `web extension delegate handles optional permissions prompt - deny`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("test", "uri")
        val permissions = arrayOf("p1", "p2")
        val origins = emptyArray<String>()
        val dataCollectionPermissions = emptyArray<String>()
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.PromptDelegate>()
        verify(webExtensionController).promptDelegate = geckoDelegateCaptor.capture()

        val result =
            geckoDelegateCaptor.value.onOptionalPrompt(
                extension,
                permissions,
                origins,
                dataCollectionPermissions,
            )
        assertNotNull(result)

        val extensionCaptor = argumentCaptor<WebExtension>()
        val onPermissionsGrantedCaptor = argumentCaptor<((Boolean) -> Unit)>()
        verify(webExtensionsDelegate)
            .onOptionalPermissionsRequest(
                extensionCaptor.capture(),
                eq(permissions.toList()),
                eq(origins.toList()),
                eq(dataCollectionPermissions.toList()),
                onPermissionsGrantedCaptor.capture(),
            )
        val current = extensionCaptor.value as mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
        assertEquals(extension, current.nativeExtension)

        onPermissionsGrantedCaptor.value.invoke(false)
        assertEquals(GeckoResult.deny(), result)
    }

    @Test
    fun `web extension delegate notified of browser actions from built-in extensions`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "resource://android/assets/extensions/test"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val result = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.ensureBuiltIn(extUrl, extId)).thenReturn(result)
        engine.installBuiltInWebExtension(extId, extUrl)
        val extension = mockNativeWebExtension(extId, extUrl)
        result.complete(extension)

        shadowOf(getMainLooper()).idle()

        val actionDelegateCaptor = argumentCaptor<org.mozilla.geckoview.WebExtension.ActionDelegate>()
        verify(extension).setActionDelegate(actionDelegateCaptor.capture())

        val browserAction: org.mozilla.geckoview.WebExtension.Action = mock()
        actionDelegateCaptor.value.onBrowserAction(extension, null, browserAction)

        val extensionCaptor = argumentCaptor<WebExtension>()
        val actionCaptor = argumentCaptor<Action>()
        verify(webExtensionsDelegate).onBrowserActionDefined(extensionCaptor.capture(), actionCaptor.capture())
        assertEquals(extId, extensionCaptor.value.id)

        actionCaptor.value.onClick()
        verify(browserAction).click()
    }

    @Test
    fun `WHEN an action popup is toggled in private browsing mode THEN the popup session is private`() {
        val runtime = mock<GeckoRuntime>()
        whenever(runtime.settings).thenReturn(mock())
        val extId = "test-webext"
        val extUrl = "resource://android/assets/extensions/test"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        whenever(webExtensionsDelegate.isInPrivateBrowsing()).thenReturn(true)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val result = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.ensureBuiltIn(extUrl, extId)).thenReturn(result)
        engine.installBuiltInWebExtension(extId, extUrl)
        val extension =
            mockNativeWebExtension(
                extId,
                extUrl,
                metaData = mockNativeWebExtensionMetaData(allowedInPrivateBrowsing = true),
            )
        result.complete(extension)

        shadowOf(getMainLooper()).idle()

        val actionDelegateCaptor = argumentCaptor<org.mozilla.geckoview.WebExtension.ActionDelegate>()
        verify(extension).setActionDelegate(actionDelegateCaptor.capture())

        val browserAction: org.mozilla.geckoview.WebExtension.Action = mock()
        actionDelegateCaptor.value.onTogglePopup(extension, browserAction)

        // The popup's engine session must be private, matching the current browsing mode.
        val engineSessionCaptor = argumentCaptor<EngineSession>()
        verify(webExtensionsDelegate).onToggleActionPopup(any(), engineSessionCaptor.capture(), any(), eq(true))
        assertTrue((engineSessionCaptor.value as GeckoEngineSession).geckoSession.settings.usePrivateMode)
    }

    @Test
    fun `WHEN a popup is toggled in private browsing mode for an extension without private access THEN no popup session is opened`() {
        val runtime = mock<GeckoRuntime>()
        whenever(runtime.settings).thenReturn(mock())
        val extId = "test-webext"
        val extUrl = "resource://android/assets/extensions/test"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        whenever(webExtensionsDelegate.isInPrivateBrowsing()).thenReturn(true)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val result = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.ensureBuiltIn(extUrl, extId)).thenReturn(result)
        engine.installBuiltInWebExtension(extId, extUrl)
        val extension =
            mockNativeWebExtension(
                extId,
                extUrl,
                metaData = mockNativeWebExtensionMetaData(allowedInPrivateBrowsing = false),
            )
        result.complete(extension)

        shadowOf(getMainLooper()).idle()

        val actionDelegateCaptor = argumentCaptor<org.mozilla.geckoview.WebExtension.ActionDelegate>()
        verify(extension).setActionDelegate(actionDelegateCaptor.capture())

        val browserAction: org.mozilla.geckoview.WebExtension.Action = mock()
        actionDelegateCaptor.value.onTogglePopup(extension, browserAction)

        verify(webExtensionsDelegate, never()).onToggleActionPopup(any(), any(), any(), anyBoolean())
    }

    @Test
    fun `WHEN a popup is toggled outside private browsing mode for an extension without private access THEN a non-private popup session is opened`() {
        val runtime = mock<GeckoRuntime>()
        whenever(runtime.settings).thenReturn(mock())
        val extId = "test-webext"
        val extUrl = "resource://android/assets/extensions/test"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        whenever(webExtensionsDelegate.isInPrivateBrowsing()).thenReturn(false)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val result = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.ensureBuiltIn(extUrl, extId)).thenReturn(result)
        engine.installBuiltInWebExtension(extId, extUrl)
        val extension =
            mockNativeWebExtension(
                extId,
                extUrl,
                metaData = mockNativeWebExtensionMetaData(allowedInPrivateBrowsing = false),
            )
        result.complete(extension)

        shadowOf(getMainLooper()).idle()

        val actionDelegateCaptor = argumentCaptor<org.mozilla.geckoview.WebExtension.ActionDelegate>()
        verify(extension).setActionDelegate(actionDelegateCaptor.capture())

        val browserAction: org.mozilla.geckoview.WebExtension.Action = mock()
        actionDelegateCaptor.value.onTogglePopup(extension, browserAction)

        val engineSessionCaptor = argumentCaptor<EngineSession>()
        verify(webExtensionsDelegate).onToggleActionPopup(any(), engineSessionCaptor.capture(), any(), eq(false))
        assertFalse((engineSessionCaptor.value as GeckoEngineSession).geckoSession.settings.usePrivateMode)
    }

    @Test
    fun `web extension delegate notified of page actions from built-in extensions`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "resource://android/assets/extensions/test"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val result = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.ensureBuiltIn(extUrl, extId)).thenReturn(result)
        engine.installBuiltInWebExtension(extId, extUrl)
        val extension = mockNativeWebExtension(extId, extUrl)
        result.complete(extension)

        shadowOf(getMainLooper()).idle()

        val actionDelegateCaptor = argumentCaptor<org.mozilla.geckoview.WebExtension.ActionDelegate>()
        verify(extension).setActionDelegate(actionDelegateCaptor.capture())

        val pageAction: org.mozilla.geckoview.WebExtension.Action = mock()
        actionDelegateCaptor.value.onPageAction(extension, null, pageAction)

        val extensionCaptor = argumentCaptor<WebExtension>()
        val actionCaptor = argumentCaptor<Action>()
        verify(webExtensionsDelegate).onPageActionDefined(extensionCaptor.capture(), actionCaptor.capture())
        assertEquals(extId, extensionCaptor.value.id)

        actionCaptor.value.onClick()
        verify(pageAction).click()
    }

    @Test
    fun `web extension delegate notified when built-in extension wants to open tab`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "resource://android/assets/extensions/test"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val result = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.ensureBuiltIn(extUrl, extId)).thenReturn(result)
        engine.installBuiltInWebExtension(extId, extUrl)
        val extension = mockNativeWebExtension(extId, extUrl)
        result.complete(extension)

        shadowOf(getMainLooper()).idle()

        val tabDelegateCaptor = argumentCaptor<org.mozilla.geckoview.WebExtension.TabDelegate>()
        verify(extension).tabDelegate = tabDelegateCaptor.capture()

        val createTabDetails: org.mozilla.geckoview.WebExtension.CreateTabDetails = mock()
        tabDelegateCaptor.value.onNewTab(extension, createTabDetails)

        val extensionCaptor = argumentCaptor<WebExtension>()
        verify(webExtensionsDelegate).onNewTab(extensionCaptor.capture(), any(), eq(false), eq(""), eq(false))
        assertEquals(extId, extensionCaptor.value.id)
    }

    @Test
    fun `web extension delegate notified of browser actions from external extensions`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "https://addons.mozilla.org/firefox/downloads/file/123/some_web_ext.xpi"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val result = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.install(any(), any())).thenReturn(result)
        engine.installWebExtension(extUrl)
        val extension = mockNativeWebExtension(extId, extUrl)
        result.complete(extension)

        shadowOf(getMainLooper()).idle()

        val actionDelegateCaptor = argumentCaptor<org.mozilla.geckoview.WebExtension.ActionDelegate>()
        verify(extension).setActionDelegate(actionDelegateCaptor.capture())

        val browserAction: org.mozilla.geckoview.WebExtension.Action = mock()
        actionDelegateCaptor.value.onBrowserAction(extension, null, browserAction)

        val extensionCaptor = argumentCaptor<WebExtension>()
        val actionCaptor = argumentCaptor<Action>()
        verify(webExtensionsDelegate).onBrowserActionDefined(extensionCaptor.capture(), actionCaptor.capture())
        assertEquals(extId, extensionCaptor.value.id)

        actionCaptor.value.onClick()
        verify(browserAction).click()
    }

    @Test
    fun `web extension delegate notified of page actions from external extensions`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "https://addons.mozilla.org/firefox/downloads/file/123/some_web_ext.xpi"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val result = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.install(any(), any())).thenReturn(result)
        engine.installWebExtension(extUrl)
        val extension = mockNativeWebExtension(extId, extUrl)
        result.complete(extension)

        shadowOf(getMainLooper()).idle()

        val actionDelegateCaptor = argumentCaptor<org.mozilla.geckoview.WebExtension.ActionDelegate>()
        verify(extension).setActionDelegate(actionDelegateCaptor.capture())

        val pageAction: org.mozilla.geckoview.WebExtension.Action = mock()
        actionDelegateCaptor.value.onPageAction(extension, null, pageAction)

        val extensionCaptor = argumentCaptor<WebExtension>()
        val actionCaptor = argumentCaptor<Action>()
        verify(webExtensionsDelegate).onPageActionDefined(extensionCaptor.capture(), actionCaptor.capture())
        assertEquals(extId, extensionCaptor.value.id)

        actionCaptor.value.onClick()
        verify(pageAction).click()
    }

    @Test
    fun `web extension delegate notified when external extension wants to open tab`() {
        val runtime = mock<GeckoRuntime>()
        val extId = "test-webext"
        val extUrl = "https://addons.mozilla.org/firefox/downloads/file/123/some_web_ext.xpi"

        val extensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val result = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.install(any(), any())).thenReturn(result)
        engine.installWebExtension(extUrl)
        val extension = mockNativeWebExtension(extId, extUrl)
        result.complete(extension)

        shadowOf(getMainLooper()).idle()

        val tabDelegateCaptor = argumentCaptor<org.mozilla.geckoview.WebExtension.TabDelegate>()
        verify(extension).tabDelegate = tabDelegateCaptor.capture()

        val createTabDetails: org.mozilla.geckoview.WebExtension.CreateTabDetails = mock()
        tabDelegateCaptor.value.onNewTab(extension, createTabDetails)

        val extensionCaptor = argumentCaptor<WebExtension>()
        verify(webExtensionsDelegate).onNewTab(extensionCaptor.capture(), any(), eq(false), eq(""), eq(false))
        assertEquals(extId, extensionCaptor.value.id)
    }

    @Test
    fun `web extension delegate notified of extension list change`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val debuggerDelegateCaptor = argumentCaptor<WebExtensionController.DebuggerDelegate>()
        verify(webExtensionController).setDebuggerDelegate(debuggerDelegateCaptor.capture())

        debuggerDelegateCaptor.value.onExtensionListUpdated()
        verify(webExtensionsDelegate).onExtensionListUpdated()
    }

    @Test
    fun `web extension delegate notified of extension process spawning disabled`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val webExtensionDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionDelegate)

        val extensionProcessDelegate = argumentCaptor<WebExtensionController.ExtensionProcessDelegate>()
        verify(webExtensionController).setExtensionProcessDelegate(extensionProcessDelegate.capture())

        extensionProcessDelegate.value.onDisabledProcessSpawning()
        verify(webExtensionDelegate).onDisabledExtensionProcessSpawning()
    }

    @Test
    fun `update web extension successfully`() {
        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()

        val updatedExtension = mockNativeWebExtension()
        val updateExtensionResult = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.update(any())).thenReturn(updateExtensionResult)
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val extension =
            mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension(
                mockNativeWebExtension(),
                runtime,
            )
        var result: WebExtension? = null
        var onErrorCalled = false

        engine.updateWebExtension(
            extension,
            onSuccess = { result = it },
            onError = { _, _ -> onErrorCalled = true },
        )
        updateExtensionResult.complete(updatedExtension)

        shadowOf(getMainLooper()).idle()

        assertFalse(onErrorCalled)
        assertNotNull(result)
    }

    @Test
    fun `try to update a web extension without a new update available`() {
        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()

        val updateExtensionResult = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.update(any())).thenReturn(updateExtensionResult)
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val extension =
            mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension(
                mockNativeWebExtension(),
                runtime,
            )
        var result: WebExtension? = null
        var onErrorCalled = false

        engine.updateWebExtension(
            extension,
            onSuccess = { result = it },
            onError = { _, _ -> onErrorCalled = true },
        )
        updateExtensionResult.complete(null)

        assertFalse(onErrorCalled)
        assertNull(result)
    }

    @Test
    fun `update web extension failure`() {
        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()

        val updateExtensionResult = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.update(any())).thenReturn(updateExtensionResult)
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val extension =
            mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension(
                mockNativeWebExtension(),
                runtime,
            )
        var result: WebExtension? = null
        val expected = IOException()
        var throwable: Throwable? = null

        engine.updateWebExtension(
            extension,
            onSuccess = { result = it },
            onError = { _, e -> throwable = e },
        )
        updateExtensionResult.completeExceptionally(expected)

        shadowOf(getMainLooper()).idle()

        assertSame(expected, throwable!!.cause)
        assertNull(result)
    }

    @Test
    fun `failures when updating MUST indicate if they are recoverable`() {
        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()
        val engine = GeckoEngine(context, runtime = runtime)

        val extension =
            mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension(
                mockNativeWebExtension(),
                runtime,
            )
        val performUpdate: (GeckoInstallException) -> WebExtensionException = { exception ->
            val updateExtensionResult = GeckoResult<GeckoWebExtension>()
            whenever(extensionController.update(any())).thenReturn(updateExtensionResult)
            whenever(runtime.webExtensionController).thenReturn(extensionController)
            var throwable: WebExtensionException? = null

            engine.updateWebExtension(
                extension,
                onError = { _, e ->
                    throwable = e as WebExtensionException
                },
            )

            updateExtensionResult.completeExceptionally(exception)

            shadowOf(getMainLooper()).idle()

            throwable!!
        }

        val unrecoverableExceptions =
            listOf(
                mockGeckoInstallException(ERROR_NETWORK_FAILURE),
                mockGeckoInstallException(ERROR_INCORRECT_HASH),
                mockGeckoInstallException(ERROR_CORRUPT_FILE),
                mockGeckoInstallException(ERROR_FILE_ACCESS),
                mockGeckoInstallException(ERROR_SIGNEDSTATE_REQUIRED),
                mockGeckoInstallException(ERROR_UNEXPECTED_ADDON_TYPE),
                mockGeckoInstallException(ERROR_INCORRECT_ID),
                mockGeckoInstallException(ERROR_POSTPONED),
            )

        unrecoverableExceptions.forEach { exception ->
            assertFalse(performUpdate(exception).isRecoverable)
        }

        val recoverableExceptions = listOf(mockGeckoInstallException(ERROR_USER_CANCELED))

        recoverableExceptions.forEach { exception ->
            assertTrue(performUpdate(exception).isRecoverable)
        }
    }

    @Test
    fun `list web extensions successfully`() {
        val installedExtension =
            mockNativeWebExtension(
                id = "id",
                location = "uri",
                metaData = mockNativeWebExtensionMetaData(allowedInPrivateBrowsing = false),
            )

        val installedExtensions = listOf(installedExtension)
        val installedExtensionResult = GeckoResult<List<GeckoWebExtension>>()

        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()
        whenever(extensionController.list()).thenReturn(installedExtensionResult)
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(testContext, runtime = runtime)
        var extensions: List<WebExtension>? = null
        var onErrorCalled = false

        engine.listInstalledWebExtensions(
            onSuccess = { extensions = it },
            onError = { onErrorCalled = true },
        )
        installedExtensionResult.complete(installedExtensions)

        shadowOf(getMainLooper()).idle()

        assertFalse(onErrorCalled)
        assertNotNull(extensions)
    }

    @Test
    fun `list web extensions failure`() {
        val installedExtensionResult = GeckoResult<List<GeckoWebExtension>>()

        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()
        whenever(extensionController.list()).thenReturn(installedExtensionResult)
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        var extensions: List<WebExtension>? = null
        val expected = IOException()
        var throwable: Throwable? = null

        engine.listInstalledWebExtensions(
            onSuccess = { extensions = it },
            onError = { throwable = it },
        )
        installedExtensionResult.completeExceptionally(expected)

        shadowOf(getMainLooper()).idle()

        assertSame(expected, throwable)
        assertNull(extensions)
    }

    @Test
    fun `enable web extension successfully`() {
        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()

        val enabledExtension = mockNativeWebExtension(id = "id", location = "uri")
        val enableExtensionResult = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.enable(any(), anyInt())).thenReturn(enableExtensionResult)
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val extension =
            mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension(
                mockNativeWebExtension(),
                runtime,
            )
        val engine = GeckoEngine(context, runtime = runtime)

        var result: WebExtension? = null
        var onErrorCalled = false

        engine.enableWebExtension(
            extension,
            onSuccess = { result = it },
            onError = { onErrorCalled = true },
        )
        enableExtensionResult.complete(enabledExtension)

        shadowOf(getMainLooper()).idle()

        assertFalse(onErrorCalled)
        assertNotNull(result)
    }

    @Test
    fun `enable web extension failure`() {
        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()

        val enableExtensionResult = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.enable(any(), anyInt())).thenReturn(enableExtensionResult)
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)

        val extension =
            mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension(
                mockNativeWebExtension(),
                runtime,
            )
        var result: WebExtension? = null
        val expected = IOException()
        var throwable: Throwable? = null

        engine.enableWebExtension(
            extension,
            onSuccess = { result = it },
            onError = { throwable = it },
        )
        enableExtensionResult.completeExceptionally(expected)

        shadowOf(getMainLooper()).idle()

        assertSame(expected, throwable)
        assertNull(result)
    }

    @Test
    fun `disable web extension successfully`() {
        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()

        val disabledExtension = mockNativeWebExtension(id = "id", location = "uri")
        val disableExtensionResult = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.disable(any(), anyInt())).thenReturn(disableExtensionResult)
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)

        val extension =
            mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension(
                mockNativeWebExtension(),
                runtime,
            )
        var result: WebExtension? = null
        var onErrorCalled = false

        engine.disableWebExtension(
            extension,
            onSuccess = { result = it },
            onError = { onErrorCalled = true },
        )
        disableExtensionResult.complete(disabledExtension)

        shadowOf(getMainLooper()).idle()

        assertFalse(onErrorCalled)
        assertNotNull(result)
    }

    @Test
    fun `disable web extension failure`() {
        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()

        val disableExtensionResult = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.disable(any(), anyInt())).thenReturn(disableExtensionResult)
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)

        val extension =
            mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension(
                mockNativeWebExtension(),
                runtime,
            )
        var result: WebExtension? = null
        val expected = IOException()
        var throwable: Throwable? = null

        engine.disableWebExtension(
            extension,
            onSuccess = { result = it },
            onError = { throwable = it },
        )
        disableExtensionResult.completeExceptionally(expected)

        shadowOf(getMainLooper()).idle()

        assertSame(expected, throwable)
        assertNull(result)
    }

    @Test
    fun `set allowedInPrivateBrowsing successfully`() {
        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()

        val allowedInPrivateBrowsing = mockNativeWebExtension(id = "id", location = "uri")
        val allowedInPrivateBrowsingExtensionResult = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.setAllowedInPrivateBrowsing(any(), anyBoolean()))
            .thenReturn(allowedInPrivateBrowsingExtensionResult)
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val extension =
            mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension(
                mockNativeWebExtension(),
                runtime,
            )
        var result: WebExtension? = null
        var onErrorCalled = false

        engine.setAllowedInPrivateBrowsing(
            extension,
            true,
            onSuccess = { ext -> result = ext },
            onError = { onErrorCalled = true },
        )
        allowedInPrivateBrowsingExtensionResult.complete(allowedInPrivateBrowsing)

        shadowOf(getMainLooper()).idle()

        assertFalse(onErrorCalled)
        assertNotNull(result)
        verify(webExtensionsDelegate).onAllowedInPrivateBrowsingChanged(result)
    }

    @Test
    fun `set allowedInPrivateBrowsing failure`() {
        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()

        val allowedInPrivateBrowsingExtensionResult = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.setAllowedInPrivateBrowsing(any(), anyBoolean()))
            .thenReturn(allowedInPrivateBrowsingExtensionResult)
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val extension =
            mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension(
                mockNativeWebExtension(),
                runtime,
            )
        var result: WebExtension? = null
        val expected = IOException()
        var throwable: Throwable? = null

        engine.setAllowedInPrivateBrowsing(
            extension,
            true,
            onSuccess = { ext -> result = ext },
            onError = { throwable = it },
        )
        allowedInPrivateBrowsingExtensionResult.completeExceptionally(expected)

        shadowOf(getMainLooper()).idle()

        assertSame(expected, throwable)
        assertNull(result)
        verify(webExtensionsDelegate, never()).onAllowedInPrivateBrowsingChanged(any())
    }

    @Test
    fun `GIVEN null native extension WHEN calling setAllowedInPrivateBrowsing THEN call onError`() {
        val runtime = mock<GeckoRuntime>()
        val extensionController: WebExtensionController = mock()

        val allowedInPrivateBrowsingExtensionResult = GeckoResult<GeckoWebExtension>()
        whenever(extensionController.setAllowedInPrivateBrowsing(any(), anyBoolean()))
            .thenReturn(allowedInPrivateBrowsingExtensionResult)
        whenever(runtime.webExtensionController).thenReturn(extensionController)

        val engine = GeckoEngine(context, runtime = runtime)
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val extension =
            mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension(
                mockNativeWebExtension(),
                runtime,
            )
        var result: WebExtension? = null
        var throwable: Throwable? = null

        engine.setAllowedInPrivateBrowsing(
            extension,
            true,
            onSuccess = { ext -> result = ext },
            onError = { throwable = it },
        )
        allowedInPrivateBrowsingExtensionResult.complete(null)

        shadowOf(getMainLooper()).idle()

        assertNotNull(throwable)
        assertNull(result)
        verify(webExtensionsDelegate, never()).onAllowedInPrivateBrowsingChanged(any())
    }

    @Test(expected = RuntimeException::class)
    fun `WHEN GeckoRuntime is shutting down THEN GeckoEngine throws runtime exception`() {
        val runtime: GeckoRuntime = mock()

        GeckoEngine(context, runtime = runtime)

        val captor = argumentCaptor<GeckoRuntime.Delegate>()
        verify(runtime).delegate = captor.capture()

        assertNotNull(captor.value)

        captor.value.onShutdown()
    }

    @Test
    fun `clear browsing data for all hosts`() {
        val runtime: GeckoRuntime = mock()
        val storageController: StorageController = mock()

        var onSuccessCalled = false

        val result = GeckoResult<Void>()
        whenever(runtime.storageController).thenReturn(storageController)
        whenever(storageController.clearData(eq(Engine.BrowsingData.all().types.toLong()))).thenReturn(result)
        result.complete(null)

        val engine = GeckoEngine(context, runtime = runtime)
        engine.clearData(data = Engine.BrowsingData.all(), onSuccess = { onSuccessCalled = true })

        shadowOf(getMainLooper()).idle()

        assertTrue(onSuccessCalled)
    }

    @Test
    fun `error handler invoked when clearing browsing data for all hosts fails`() {
        val runtime: GeckoRuntime = mock()
        val storageController: StorageController = mock()

        var throwable: Throwable? = null
        var onErrorCalled = false

        val exception = IOException()
        val result = GeckoResult<Void>()
        whenever(runtime.storageController).thenReturn(storageController)
        whenever(storageController.clearData(eq(Engine.BrowsingData.all().types.toLong()))).thenReturn(result)
        result.completeExceptionally(exception)

        val engine = GeckoEngine(context, runtime = runtime)
        engine.clearData(
            data = Engine.BrowsingData.all(),
            onError = {
                onErrorCalled = true
                throwable = it
            },
        )

        shadowOf(getMainLooper()).idle()

        assertTrue(onErrorCalled)
        assertSame(exception, throwable)
    }

    @Test
    fun `clear browsing data for specified host`() {
        val runtime: GeckoRuntime = mock()
        val storageController: StorageController = mock()

        var onSuccessCalled = false

        val result = GeckoResult<Void>()
        whenever(runtime.storageController).thenReturn(storageController)
        whenever(
                storageController.clearDataFromBaseDomain(
                    eq("mozilla.org"),
                    eq(Engine.BrowsingData.all().types.toLong()),
                )
            )
            .thenReturn(result)
        result.complete(null)

        val engine = GeckoEngine(context, runtime = runtime)
        engine.clearData(data = Engine.BrowsingData.all(), host = "mozilla.org", onSuccess = { onSuccessCalled = true })

        shadowOf(getMainLooper()).idle()

        assertTrue(onSuccessCalled)
    }

    @Test
    fun `error handler invoked when clearing browsing data for specified hosts fails`() {
        val runtime: GeckoRuntime = mock()
        val storageController: StorageController = mock()

        var throwable: Throwable? = null
        var onErrorCalled = false

        val exception = IOException()
        val result = GeckoResult<Void>()
        whenever(runtime.storageController).thenReturn(storageController)
        whenever(
                storageController.clearDataFromBaseDomain(
                    eq("mozilla.org"),
                    eq(Engine.BrowsingData.all().types.toLong()),
                )
            )
            .thenReturn(result)
        result.completeExceptionally(exception)

        val engine = GeckoEngine(context, runtime = runtime)
        engine.clearData(
            data = Engine.BrowsingData.all(),
            host = "mozilla.org",
            onError = {
                onErrorCalled = true
                throwable = it
            },
        )

        shadowOf(getMainLooper()).idle()

        assertTrue(onErrorCalled)
        assertSame(exception, throwable)
    }

    @Test
    fun `test parsing engine version`() {
        val runtime: GeckoRuntime = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        val version = engine.version

        println(version)

        assertTrue(version.major >= 69)
        assertTrue(version.isAtLeast(69, 0, 0))
        assertTrue(version.releaseChannel != EngineReleaseChannel.UNKNOWN)
    }

    @Test
    fun `fetch trackers logged successfully`() {
        val runtime = mock<GeckoRuntime>()
        val engine = GeckoEngine(context, runtime = runtime)
        var onSuccessCalled = false
        var onErrorCalled = false
        val mockSession = mock<GeckoEngineSession>()
        val mockGeckoSetting = mock<GeckoRuntimeSettings>()
        val mockGeckoContentBlockingSetting = mock<ContentBlocking.Settings>()
        var trackersLog: List<TrackerLog>? = null

        val mockContentBlockingController = mock<ContentBlockingController>()
        var logEntriesResult = GeckoResult<List<ContentBlockingController.LogEntry>>()

        whenever(runtime.settings).thenReturn(mockGeckoSetting)
        whenever(mockGeckoSetting.contentBlocking).thenReturn(mockGeckoContentBlockingSetting)
        whenever(mockGeckoContentBlockingSetting.enhancedTrackingProtectionLevel)
            .thenReturn(ContentBlocking.EtpLevel.STRICT)
        whenever(runtime.contentBlockingController).thenReturn(mockContentBlockingController)
        whenever(mockContentBlockingController.getLog(any())).thenReturn(logEntriesResult)

        engine.getTrackersLog(
            mockSession,
            onSuccess = {
                trackersLog = it
                onSuccessCalled = true
            },
            onError = { onErrorCalled = true },
        )

        logEntriesResult.complete(createDummyLogEntryList())

        shadowOf(getMainLooper()).idle()

        val trackerLog = trackersLog!!.first()
        assertTrue(trackerLog.cookiesHasBeenBlocked)
        assertEquals("www.tracker.com", trackerLog.url)
        assertTrue(trackerLog.blockedCategories.contains(TrackingCategory.SCRIPTS_AND_SUB_RESOURCES))
        assertTrue(trackerLog.blockedCategories.contains(TrackingCategory.FINGERPRINTING))
        assertTrue(trackerLog.blockedCategories.contains(TrackingCategory.CRYPTOMINING))
        assertTrue(trackerLog.blockedCategories.contains(TrackingCategory.MOZILLA_SOCIAL))
        assertTrue(trackerLog.loadedCategories.contains(TrackingCategory.SCRIPTS_AND_SUB_RESOURCES))
        assertTrue(trackerLog.loadedCategories.contains(TrackingCategory.FINGERPRINTING))
        assertTrue(trackerLog.loadedCategories.contains(TrackingCategory.CRYPTOMINING))
        assertTrue(trackerLog.loadedCategories.contains(TrackingCategory.MOZILLA_SOCIAL))
        assertTrue(trackerLog.unBlockedBySmartBlock)

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)

        logEntriesResult = GeckoResult()
        whenever(mockContentBlockingController.getLog(any())).thenReturn(logEntriesResult)
        logEntriesResult.completeExceptionally(Exception())

        engine.getTrackersLog(
            mockSession,
            onSuccess = {
                trackersLog = it
                onSuccessCalled = true
            },
            onError = { onErrorCalled = true },
        )

        shadowOf(getMainLooper()).idle()

        assertTrue(onErrorCalled)
    }

    @Test
    fun `shimmed content MUST be categorized as blocked`() {
        val runtime = mock<GeckoRuntime>()
        val engine = spy(GeckoEngine(context, runtime = runtime))
        val mockSession = mock<GeckoEngineSession>()
        val mockGeckoSetting = mock<GeckoRuntimeSettings>()
        val mockGeckoContentBlockingSetting = mock<ContentBlocking.Settings>()
        var trackersLog: List<TrackerLog>? = null

        val mockContentBlockingController = mock<ContentBlockingController>()
        val logEntriesResult = GeckoResult<List<ContentBlockingController.LogEntry>>()

        val engineSetting = DefaultSettings()
        engineSetting.trackingProtectionPolicy = TrackingProtectionPolicy.strict()

        whenever(engine.settings).thenReturn(engineSetting)
        whenever(runtime.settings).thenReturn(mockGeckoSetting)
        whenever(mockGeckoSetting.contentBlocking).thenReturn(mockGeckoContentBlockingSetting)

        whenever(runtime.contentBlockingController).thenReturn(mockContentBlockingController)
        whenever(mockContentBlockingController.getLog(any())).thenReturn(logEntriesResult)

        engine.getTrackersLog(mockSession, onSuccess = { trackersLog = it })

        logEntriesResult.complete(createShimmedEntryList())

        shadowOf(getMainLooper()).idle()

        val trackerLog = trackersLog!!.first()
        assertEquals("www.tracker.com", trackerLog.url)
        assertTrue(trackerLog.blockedCategories.contains(TrackingCategory.SCRIPTS_AND_SUB_RESOURCES))
        assertTrue(trackerLog.blockedCategories.contains(TrackingCategory.MOZILLA_SOCIAL))
        assertTrue(trackerLog.loadedCategories.isEmpty())
    }

    @Test
    fun `fetch site with social trackers`() {
        val runtime = mock<GeckoRuntime>()
        val engine = GeckoEngine(context, runtime = runtime)
        val mockSession = mock<GeckoEngineSession>()
        val mockGeckoSetting = mock<GeckoRuntimeSettings>()
        val mockGeckoContentBlockingSetting = mock<ContentBlocking.Settings>()
        var trackersLog: List<TrackerLog>? = null

        val mockContentBlockingController = mock<ContentBlockingController>()
        var logEntriesResult = GeckoResult<List<ContentBlockingController.LogEntry>>()

        whenever(runtime.settings).thenReturn(mockGeckoSetting)
        whenever(mockGeckoSetting.contentBlocking).thenReturn(mockGeckoContentBlockingSetting)
        whenever(runtime.contentBlockingController).thenReturn(mockContentBlockingController)
        whenever(mockContentBlockingController.getLog(any())).thenReturn(logEntriesResult)
        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.recommended()

        engine.getTrackersLog(mockSession, onSuccess = { trackersLog = it })
        logEntriesResult.complete(createSocialTrackersLogEntryList())

        shadowOf(getMainLooper()).idle()

        var trackerLog = trackersLog!!.first()
        assertTrue(trackerLog.cookiesHasBeenBlocked)
        assertEquals("www.tracker.com", trackerLog.url)
        assertTrue(trackerLog.blockedCategories.contains(TrackingCategory.MOZILLA_SOCIAL))

        var trackerLog2 = trackersLog[1]
        assertFalse(trackerLog2.cookiesHasBeenBlocked)
        assertEquals("www.tracker2.com", trackerLog2.url)
        assertTrue(trackerLog2.loadedCategories.contains(TrackingCategory.MOZILLA_SOCIAL))

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.strict()

        logEntriesResult = GeckoResult()
        whenever(mockContentBlockingController.getLog(any())).thenReturn(logEntriesResult)

        engine.getTrackersLog(mockSession, onSuccess = { trackersLog = it })
        logEntriesResult.complete(createSocialTrackersLogEntryList())

        trackerLog = trackersLog.first()
        assertTrue(trackerLog.cookiesHasBeenBlocked)
        assertEquals("www.tracker.com", trackerLog.url)
        assertTrue(trackerLog.blockedCategories.contains(TrackingCategory.MOZILLA_SOCIAL))

        trackerLog2 = trackersLog[1]
        assertFalse(trackerLog2.cookiesHasBeenBlocked)
        assertEquals("www.tracker2.com", trackerLog2.url)
        assertTrue(trackerLog2.loadedCategories.contains(TrackingCategory.MOZILLA_SOCIAL))
    }

    @Test
    fun `GIVEN a site with social trackers cross-site cookies WHEN these are blocked THEN engine informs about this`() {
        val runtime = mock<GeckoRuntime>()
        val engine = GeckoEngine(context, runtime = runtime)
        val mockSession = mock<GeckoEngineSession>()
        val mockGeckoSetting = mock<GeckoRuntimeSettings>()
        val mockGeckoContentBlockingSetting = mock<ContentBlocking.Settings>()
        var trackersLog: List<TrackerLog>? = null

        val mockContentBlockingController = mock<ContentBlockingController>()
        var logEntriesResult = GeckoResult<List<ContentBlockingController.LogEntry>>()

        whenever(runtime.settings).thenReturn(mockGeckoSetting)
        whenever(mockGeckoSetting.contentBlocking).thenReturn(mockGeckoContentBlockingSetting)
        whenever(runtime.contentBlockingController).thenReturn(mockContentBlockingController)
        whenever(mockContentBlockingController.getLog(any())).thenReturn(logEntriesResult)
        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.recommended()

        engine.getTrackersLog(mockSession, onSuccess = { trackersLog = it })
        logEntriesResult.complete(createSocialTrackersLogEntryList() + createBlockedCrossSiteCookiesLogEntryList())

        shadowOf(getMainLooper()).idle()

        assertEquals(3, trackersLog!!.size)

        var trackerLog = trackersLog.first()
        assertTrue(trackerLog.cookiesHasBeenBlocked)
        assertEquals("www.tracker.com", trackerLog.url)
        assertTrue(trackerLog.blockedCategories.contains(TrackingCategory.MOZILLA_SOCIAL))

        var trackerLog2 = trackersLog[1]
        assertFalse(trackerLog2.cookiesHasBeenBlocked)
        assertEquals("www.tracker2.com", trackerLog2.url)
        assertTrue(trackerLog2.loadedCategories.contains(TrackingCategory.MOZILLA_SOCIAL))

        var trackerLog3 = trackersLog.last()
        assertTrue(trackerLog3.cookiesHasBeenBlocked)
        assertEquals("www.data-collector.com", trackerLog3.url)
        assertTrue(trackerLog3.loadedCategories.isEmpty())

        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.strict()

        logEntriesResult = GeckoResult()
        whenever(mockContentBlockingController.getLog(any())).thenReturn(logEntriesResult)
        engine.getTrackersLog(mockSession, onSuccess = { trackersLog = it })
        logEntriesResult.complete(createSocialTrackersLogEntryList())

        trackerLog = trackersLog.first()
        assertTrue(trackerLog.cookiesHasBeenBlocked)
        assertEquals("www.tracker.com", trackerLog.url)
        assertTrue(trackerLog.blockedCategories.contains(TrackingCategory.MOZILLA_SOCIAL))

        trackerLog2 = trackersLog[1]
        assertFalse(trackerLog2.cookiesHasBeenBlocked)
        assertEquals("www.tracker2.com", trackerLog2.url)
        assertTrue(trackerLog2.loadedCategories.contains(TrackingCategory.MOZILLA_SOCIAL))

        trackerLog3 = trackersLog.last()
        assertTrue(trackerLog3.cookiesHasBeenBlocked)
        assertEquals("www.data-collector.com", trackerLog3.url)
        assertTrue(trackerLog3.loadedCategories.isEmpty())
    }

    @Test
    fun `GIVEN a site with an email tracker WHEN it is blocked THEN engine reports it under EMAIL`() {
        val runtime = mock<GeckoRuntime>()
        val engine = GeckoEngine(context, runtime = runtime)
        val mockSession = mock<GeckoEngineSession>()
        val mockGeckoSetting = mock<GeckoRuntimeSettings>()
        val mockGeckoContentBlockingSetting = mock<ContentBlocking.Settings>()
        var trackersLog: List<TrackerLog>? = null

        val mockContentBlockingController = mock<ContentBlockingController>()
        val logEntriesResult = GeckoResult<List<ContentBlockingController.LogEntry>>()

        whenever(runtime.settings).thenReturn(mockGeckoSetting)
        whenever(mockGeckoSetting.contentBlocking).thenReturn(mockGeckoContentBlockingSetting)
        whenever(runtime.contentBlockingController).thenReturn(mockContentBlockingController)
        whenever(mockContentBlockingController.getLog(any())).thenReturn(logEntriesResult)
        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.recommended()

        engine.getTrackersLog(mockSession, onSuccess = { trackersLog = it })
        logEntriesResult.complete(createEmailTrackerLogEntryList())

        shadowOf(getMainLooper()).idle()

        assertEquals(1, trackersLog!!.size)
        val trackerLog = trackersLog.first()
        assertEquals("www.email-tracker.com", trackerLog.url)
        assertTrue(trackerLog.blockedCategories.contains(TrackingCategory.EMAIL))
        assertFalse(trackerLog.cookiesHasBeenBlocked)
        assertFalse(trackerLog.unBlockedBySmartBlock)
    }

    @Test
    fun `GIVEN a site with a bounce tracker WHEN it is purged THEN engine reports it under SCRIPTS_AND_SUB_RESOURCES`() {
        val runtime = mock<GeckoRuntime>()
        val engine = GeckoEngine(context, runtime = runtime)
        val mockSession = mock<GeckoEngineSession>()
        val mockGeckoSetting = mock<GeckoRuntimeSettings>()
        val mockGeckoContentBlockingSetting = mock<ContentBlocking.Settings>()
        var trackersLog: List<TrackerLog>? = null

        val mockContentBlockingController = mock<ContentBlockingController>()
        val logEntriesResult = GeckoResult<List<ContentBlockingController.LogEntry>>()

        whenever(runtime.settings).thenReturn(mockGeckoSetting)
        whenever(mockGeckoSetting.contentBlocking).thenReturn(mockGeckoContentBlockingSetting)
        whenever(runtime.contentBlockingController).thenReturn(mockContentBlockingController)
        whenever(mockContentBlockingController.getLog(any())).thenReturn(logEntriesResult)
        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.recommended()

        engine.getTrackersLog(mockSession, onSuccess = { trackersLog = it })
        logEntriesResult.complete(createPurgedBounceTrackerLogEntryList())

        shadowOf(getMainLooper()).idle()

        assertEquals(1, trackersLog!!.size)
        val trackerLog = trackersLog.first()
        assertEquals("www.bounce-tracker.com", trackerLog.url)
        assertTrue(trackerLog.blockedCategories.contains(TrackingCategory.SCRIPTS_AND_SUB_RESOURCES))
        assertFalse(trackerLog.cookiesHasBeenBlocked)
        assertFalse(trackerLog.unBlockedBySmartBlock)
    }

    @Test
    fun `GIVEN a site with replaced fingerprinting content WHEN it is shimmed THEN engine reports it under FINGERPRINTING`() {
        val runtime = mock<GeckoRuntime>()
        val engine = GeckoEngine(context, runtime = runtime)
        val mockSession = mock<GeckoEngineSession>()
        val mockGeckoSetting = mock<GeckoRuntimeSettings>()
        val mockGeckoContentBlockingSetting = mock<ContentBlocking.Settings>()
        var trackersLog: List<TrackerLog>? = null

        val mockContentBlockingController = mock<ContentBlockingController>()
        val logEntriesResult = GeckoResult<List<ContentBlockingController.LogEntry>>()

        whenever(runtime.settings).thenReturn(mockGeckoSetting)
        whenever(mockGeckoSetting.contentBlocking).thenReturn(mockGeckoContentBlockingSetting)
        whenever(runtime.contentBlockingController).thenReturn(mockContentBlockingController)
        whenever(mockContentBlockingController.getLog(any())).thenReturn(logEntriesResult)
        engine.settings.trackingProtectionPolicy = TrackingProtectionPolicy.recommended()

        engine.getTrackersLog(mockSession, onSuccess = { trackersLog = it })
        logEntriesResult.complete(createReplacedFingerprintingLogEntryList())

        shadowOf(getMainLooper()).idle()

        assertEquals(1, trackersLog!!.size)
        val trackerLog = trackersLog.first()
        assertEquals("www.fingerprinting-shim.com", trackerLog.url)
        assertTrue(trackerLog.blockedCategories.contains(TrackingCategory.FINGERPRINTING))
        assertFalse(trackerLog.cookiesHasBeenBlocked)
        assertFalse(trackerLog.unBlockedBySmartBlock)
    }

    @Test
    fun `GIVEN a request to fetch tracking events WHEN successful THEN invoke the success callback`() {
        val runtime: GeckoRuntime = mock()
        val controller: ContentBlockingController = mock()
        whenever(runtime.contentBlockingController).thenReturn(controller)
        val engine = GeckoEngine(context, runtime = runtime)

        val dateFrom = 1000L
        val dateTo = 2000L
        val result = GeckoResult<List<ContentBlockingController.TrackingDbEvent>>()
        whenever(controller.getTrackingDbEventsByDateRange(dateFrom, dateTo)).thenReturn(result)

        val event = object : ContentBlockingController.TrackingDbEvent() {}
        ReflectionUtils.setField(event, "type", 1)
        ReflectionUtils.setField(event, "count", 5)
        ReflectionUtils.setField(event, "date", "2023-01-01")

        var onSuccessCalled = false
        var onErrorCalled = false
        var events: List<TrackingProtectionEvent>? = null

        engine.getTrackingProtectionEventsByDateRange(
            dateFrom,
            dateTo,
            onSuccess = {
                onSuccessCalled = true
                events = it
            },
            onError = { onErrorCalled = true },
        )
        result.complete(listOf(event))
        shadowOf(getMainLooper()).idle()

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
        assertEquals(1, events?.size)
        assertEquals(1, events?.first()?.type)
        assertEquals(5, events?.first()?.count)
        assertEquals("2023-01-01", events?.first()?.date)
    }

    @Test
    fun `GIVEN a request to fetch tracking events WHEN an error is encountered THEN call the error callback`() {
        val runtime: GeckoRuntime = mock()
        val controller: ContentBlockingController = mock()
        whenever(runtime.contentBlockingController).thenReturn(controller)
        val engine = GeckoEngine(context, runtime = runtime)

        val result = GeckoResult<List<ContentBlockingController.TrackingDbEvent>>()
        whenever(controller.getTrackingDbEventsByDateRange(anyLong(), anyLong())).thenReturn(result)

        var onErrorCalled = false
        var onSuccessCalled = false

        engine.getTrackingProtectionEventsByDateRange(
            dateFrom = 0,
            dateTo = 0,
            onSuccess = { onSuccessCalled = true },
            onError = { onErrorCalled = true },
        )
        result.completeExceptionally(Exception())
        shadowOf(getMainLooper()).idle()

        assertFalse(onSuccessCalled)
        assertTrue(onErrorCalled)
    }

    @Test
    fun `GIVEN a request to fetch tracking events WHEN no data is available THEN return an empty list`() {
        val runtime: GeckoRuntime = mock()
        val controller: ContentBlockingController = mock()
        whenever(runtime.contentBlockingController).thenReturn(controller)
        val engine = GeckoEngine(context, runtime = runtime)

        val result = GeckoResult<List<ContentBlockingController.TrackingDbEvent>>()
        whenever(controller.getTrackingDbEventsByDateRange(anyLong(), anyLong())).thenReturn(result)

        var events: List<TrackingProtectionEvent>? = null
        var onErrorCalled = false

        engine.getTrackingProtectionEventsByDateRange(
            dateFrom = 0,
            dateTo = 0,
            onSuccess = { events = it },
            onError = { onErrorCalled = true },
        )
        result.complete(null)
        shadowOf(getMainLooper()).idle()

        assertFalse(onErrorCalled)
        assertNotNull(events)
        assertTrue(events.isEmpty())
    }

    @Test
    fun `GIVEN a request to fetch total trackers blocked WHEN successful THEN return their sum`() {
        val runtime: GeckoRuntime = mock()
        val controller: ContentBlockingController = mock()
        whenever(runtime.contentBlockingController).thenReturn(controller)
        val engine = GeckoEngine(context, runtime = runtime)

        val result = GeckoResult<Int>()
        whenever(controller.sumAllTrackingDbEvents()).thenReturn(result)

        var onSuccessCalled = false
        var onErrorCalled = false
        var total = -1

        engine.sumAllTrackingProtectionEvents(
            onSuccess = {
                onSuccessCalled = true
                total = it
            },
            onError = { onErrorCalled = true },
        )
        result.complete(42)
        shadowOf(getMainLooper()).idle()

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
        assertEquals(42, total)
    }

    @Test
    fun `GIVEN a request to fetch total trackers blocked WHEN no data is available THEN return 0`() {
        val runtime: GeckoRuntime = mock()
        val controller: ContentBlockingController = mock()
        whenever(runtime.contentBlockingController).thenReturn(controller)
        val engine = GeckoEngine(context, runtime = runtime)

        val result = GeckoResult<Int>()
        whenever(controller.sumAllTrackingDbEvents()).thenReturn(result)

        var onSuccessCalled = false
        var onErrorCalled = false
        var total = -1

        engine.sumAllTrackingProtectionEvents(
            onSuccess = {
                onSuccessCalled = true
                total = it
            },
            onError = { onErrorCalled = true },
        )
        result.complete(null)
        shadowOf(getMainLooper()).idle()

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
        assertEquals(0, total)
    }

    @Test
    fun `GIVEN a request to fetch total trackers blocked WHEN an error is encountered THEN call the error callback`() {
        val runtime: GeckoRuntime = mock()
        val controller: ContentBlockingController = mock()
        whenever(runtime.contentBlockingController).thenReturn(controller)
        val engine = GeckoEngine(context, runtime = runtime)

        val result = GeckoResult<Int>()
        whenever(controller.sumAllTrackingDbEvents()).thenReturn(result)

        var onSuccessCalled = false
        var onErrorCalled = false

        engine.sumAllTrackingProtectionEvents(
            onSuccess = { onSuccessCalled = true },
            onError = { onErrorCalled = true },
        )
        result.completeExceptionally(Exception())
        shadowOf(getMainLooper()).idle()

        assertFalse(onSuccessCalled)
        assertTrue(onErrorCalled)
    }

    @Test
    fun `GIVEN a request to fetch earliest tracking date WHEN successful THEN return that date`() {
        val runtime: GeckoRuntime = mock()
        val controller: ContentBlockingController = mock()
        whenever(runtime.contentBlockingController).thenReturn(controller)
        val engine = GeckoEngine(context, runtime = runtime)

        val result = GeckoResult<Long>()
        whenever(controller.getTrackingDbEarliestRecordedDate()).thenReturn(result)

        var onSuccessCalled = false
        var onErrorCalled = false
        var date: Long? = -1L

        engine.getEarliestTrackingProtectionDate(
            onSuccess = {
                onSuccessCalled = true
                date = it
            },
            onError = { onErrorCalled = true },
        )
        result.complete(123456789L)
        shadowOf(getMainLooper()).idle()

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
        assertEquals(123456789L, date)
    }

    @Test
    fun `GIVEN a request to fetch earliest tracking date WHEN no data is available THEN return null`() {
        val runtime: GeckoRuntime = mock()
        val controller: ContentBlockingController = mock()
        whenever(runtime.contentBlockingController).thenReturn(controller)
        val engine = GeckoEngine(context, runtime = runtime)

        val result = GeckoResult<Long>()
        whenever(controller.getTrackingDbEarliestRecordedDate()).thenReturn(result)

        var onSuccessCalled = false
        var onErrorCalled = false
        var date: Long? = -1L

        engine.getEarliestTrackingProtectionDate(
            onSuccess = {
                onSuccessCalled = true
                date = it
            },
            onError = { onErrorCalled = true },
        )
        result.complete(0L)
        shadowOf(getMainLooper()).idle()

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
        assertNull(date)
    }

    @Test
    fun `GIVEN a request to fetch earliest tracking date WHEN an error is encountered THEN call the error callback`() {
        val runtime: GeckoRuntime = mock()
        val controller: ContentBlockingController = mock()
        whenever(runtime.contentBlockingController).thenReturn(controller)
        val engine = GeckoEngine(context, runtime = runtime)

        val result = GeckoResult<Long>()
        whenever(controller.getTrackingDbEarliestRecordedDate()).thenReturn(result)

        var onSuccessCalled = false
        var onErrorCalled = false

        engine.getEarliestTrackingProtectionDate(
            onSuccess = { onSuccessCalled = true },
            onError = { onErrorCalled = true },
        )
        result.completeExceptionally(Exception())
        shadowOf(getMainLooper()).idle()

        assertFalse(onSuccessCalled)
        assertTrue(onErrorCalled)
    }

    @Test
    fun `GIVEN a request to clear tracking protection data WHEN successful THEN invoke the success callback`() {
        val runtime: GeckoRuntime = mock()
        val controller: ContentBlockingController = mock()
        whenever(runtime.contentBlockingController).thenReturn(controller)
        val engine = GeckoEngine(context, runtime = runtime)

        val result = GeckoResult<Void>()
        whenever(controller.clearTrackingDb()).thenReturn(result)

        var onSuccessCalled = false
        var onErrorCalled = false

        engine.clearTrackingProtectionData(
            onSuccess = { onSuccessCalled = true },
            onError = { onErrorCalled = true },
        )
        result.complete(null)
        shadowOf(getMainLooper()).idle()

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `GIVEN a request to clear tracking protection data WHEN an error is encountered THEN call the error callback`() {
        val runtime: GeckoRuntime = mock()
        val controller: ContentBlockingController = mock()
        whenever(runtime.contentBlockingController).thenReturn(controller)
        val engine = GeckoEngine(context, runtime = runtime)

        val result = GeckoResult<Void>()
        whenever(controller.clearTrackingDb()).thenReturn(result)

        var onSuccessCalled = false
        var onErrorCalled = false

        engine.clearTrackingProtectionData(
            onSuccess = { onSuccessCalled = true },
            onError = { onErrorCalled = true },
        )
        result.completeExceptionally(Exception())
        shadowOf(getMainLooper()).idle()

        assertFalse(onSuccessCalled)
        assertTrue(onErrorCalled)
    }

    @Test
    fun `fetch trackers logged of the level 2 list`() {
        val runtime = mock<GeckoRuntime>()
        val engine = GeckoEngine(context, runtime = runtime)
        val mockSession = mock<GeckoEngineSession>()
        val mockGeckoSetting = mock<GeckoRuntimeSettings>()
        val mockGeckoContentBlockingSetting = mock<ContentBlocking.Settings>()
        var trackersLog: List<TrackerLog>? = null

        val mockContentBlockingController = mock<ContentBlockingController>()
        var logEntriesResult = GeckoResult<List<ContentBlockingController.LogEntry>>()

        whenever(runtime.settings).thenReturn(mockGeckoSetting)
        whenever(mockGeckoSetting.contentBlocking).thenReturn(mockGeckoContentBlockingSetting)
        whenever(mockGeckoContentBlockingSetting.enhancedTrackingProtectionLevel)
            .thenReturn(ContentBlocking.EtpLevel.STRICT)
        whenever(runtime.contentBlockingController).thenReturn(mockContentBlockingController)
        whenever(mockContentBlockingController.getLog(any())).thenReturn(logEntriesResult)

        engine.settings.trackingProtectionPolicy =
            TrackingProtectionPolicy.select(
                arrayOf(
                    TrackingCategory.STRICT,
                    TrackingCategory.CONTENT,
                )
            )

        logEntriesResult = GeckoResult()
        whenever(runtime.contentBlockingController).thenReturn(mockContentBlockingController)
        whenever(mockContentBlockingController.getLog(any())).thenReturn(logEntriesResult)

        engine.getTrackersLog(
            mockSession,
            onSuccess = {
                trackersLog = it
            },
            onError = {},
        )
        logEntriesResult.complete(createDummyLogEntryList())

        shadowOf(getMainLooper()).idle()

        val trackerLog = trackersLog!![1]
        assertTrue(trackerLog.loadedCategories.contains(TrackingCategory.SCRIPTS_AND_SUB_RESOURCES))
    }

    @Test
    fun `registerWebNotificationDelegate sets delegate`() {
        val runtime = mock<GeckoRuntime>()
        val engine = GeckoEngine(context, runtime = runtime)

        engine.registerWebNotificationDelegate(mock())

        verify(runtime).webNotificationDelegate = any()
    }

    @Test
    fun `registerWebPushDelegate sets delegate and returns same handler`() {
        val runtime = mock<GeckoRuntime>()
        val controller: WebPushController = mock()
        val engine = GeckoEngine(context, runtime = runtime)

        whenever(runtime.webPushController).thenReturn(controller)

        val handler1 = engine.registerWebPushDelegate(mock())
        val handler2 = engine.registerWebPushDelegate(mock())

        verify(controller, times(2)).setDelegate(any())

        assert(handler1 == handler2)
    }

    @Test
    fun `registerActivityDelegate sets delegate`() {
        val runtime = mock<GeckoRuntime>()
        val engine = GeckoEngine(context, runtime = runtime)

        engine.registerActivityDelegate(mock())

        verify(runtime).activityDelegate = any()
    }

    @Test
    fun `unregisterActivityDelegate sets delegate to null`() {
        val runtime = mock<GeckoRuntime>()
        val engine = GeckoEngine(context, runtime = runtime)

        engine.registerActivityDelegate(mock())

        verify(runtime).activityDelegate = any()

        engine.unregisterActivityDelegate()

        verify(runtime).activityDelegate = null
    }

    @Test
    fun `registerScreenOrientationDelegate sets delegate`() {
        val orientationController = mock<OrientationController>()
        val runtime = mock<GeckoRuntime>()
        doReturn(orientationController).`when`(runtime).orientationController
        val engine = GeckoEngine(context, runtime = runtime)

        engine.registerScreenOrientationDelegate(mock())

        verify(orientationController).delegate = any()
    }

    @Test
    fun `unregisterScreenOrientationDelegate sets delegate to null`() {
        val orientationController = mock<OrientationController>()
        val runtime = mock<GeckoRuntime>()
        doReturn(orientationController).`when`(runtime).orientationController
        val engine = GeckoEngine(context, runtime = runtime)

        engine.registerScreenOrientationDelegate(mock())
        verify(orientationController).delegate = any()

        engine.unregisterScreenOrientationDelegate()
        verify(orientationController).delegate = null
    }

    @Test
    fun `registerPrefObserverDelegate sets delegate`() {
        val delegate = mock<BrowserPrefObserverDelegate>()
        val runtime = GeckoRuntime.getDefault(testContext)
        val settings = DefaultSettings()
        val engine = GeckoEngine(context, runtime = runtime, defaultSettings = settings)

        engine.registerPrefObserverDelegate(delegate)
        val result = runtime.preferencesObserverDelegate as GeckoPreferenceObserverDelegate

        assertEquals(delegate, result.delegate)
    }

    @Test
    fun `unregisterPrefObserverDelegate sets delegate to null`() {
        val runtime = GeckoRuntime.getDefault(testContext)
        val settings = DefaultSettings()
        val engine = GeckoEngine(context, runtime = runtime, defaultSettings = settings)

        engine.registerPrefObserverDelegate(mock())
        assertNotNull(runtime.preferencesObserverDelegate)

        engine.unregisterPrefObserverDelegate()
        assertNull(runtime.preferencesObserverDelegate)
    }

    @Test
    fun `registerServiceWorkerDelegate sets delegate`() {
        val delegate = mock<ServiceWorkerDelegate>()
        val runtime = GeckoRuntime.getDefault(testContext)
        val settings = DefaultSettings()
        val engine = GeckoEngine(context, runtime = runtime, defaultSettings = settings)

        engine.registerServiceWorkerDelegate(delegate)
        val result = runtime.serviceWorkerDelegate as GeckoServiceWorkerDelegate

        assertEquals(delegate, result.delegate)
        assertEquals(runtime, result.runtime)
        assertEquals(settings, result.engineSettings)
    }

    @Test
    fun `unregisterServiceWorkerDelegate sets delegate to null`() {
        val runtime = GeckoRuntime.getDefault(testContext)
        val settings = DefaultSettings()
        val engine = GeckoEngine(context, runtime = runtime, defaultSettings = settings)

        engine.registerServiceWorkerDelegate(mock())
        assertNotNull(runtime.serviceWorkerDelegate)

        engine.unregisterServiceWorkerDelegate()
        assertNull(runtime.serviceWorkerDelegate)
    }

    @Test
    fun `handleWebNotificationClick calls click on the WebNotification`() {
        val runtime = GeckoRuntime.getDefault(testContext)
        val settings = DefaultSettings()
        val engine = GeckoEngine(context, runtime = runtime, defaultSettings = settings)

        // Check that having another argument doesn't cause any issues
        engine.handleWebNotificationClick(runtime, action = null)

        val notification: WebNotification = mock()
        engine.handleWebNotificationClick(notification, action = null)
        verify(notification).click()
    }

    @Test
    fun `web extension delegate handles add-on onEnabled event`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("test", "uri")
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.AddonManagerDelegate>()
        verify(webExtensionController).setAddonManagerDelegate(geckoDelegateCaptor.capture())

        assertEquals(Unit, geckoDelegateCaptor.value.onEnabled(extension))
        val extensionCaptor = argumentCaptor<WebExtension>()
        verify(webExtensionsDelegate).onEnabled(extensionCaptor.capture())
        val capturedExtension =
            extensionCaptor.value as mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
        assertEquals(extension, capturedExtension.nativeExtension)
    }

    @Test
    fun `web extension delegate handles add-on onOptionalPermissionsChanged event`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("test", "uri")
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.AddonManagerDelegate>()
        verify(webExtensionController).setAddonManagerDelegate(geckoDelegateCaptor.capture())

        assertEquals(Unit, geckoDelegateCaptor.value.onOptionalPermissionsChanged(extension))
        val extensionCaptor = argumentCaptor<WebExtension>()
        verify(webExtensionsDelegate).onOptionalPermissionsChanged(extensionCaptor.capture())
        val capturedExtension =
            extensionCaptor.value as mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
        assertEquals(extension, capturedExtension.nativeExtension)
    }

    @Test
    fun `web extension delegate handles add-on onInstallationFailed event`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()

        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("test", "uri")
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        val exception = mockGeckoInstallException(ERROR_BLOCKLISTED)

        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.AddonManagerDelegate>()
        verify(webExtensionController).setAddonManagerDelegate(geckoDelegateCaptor.capture())

        assertEquals(Unit, geckoDelegateCaptor.value.onInstallationFailed(extension, exception))

        val extensionCaptor = argumentCaptor<WebExtension>()
        val exceptionCaptor = argumentCaptor<WebExtensionInstallException>()

        verify(webExtensionsDelegate)
            .onInstallationFailedRequest(
                extensionCaptor.capture(),
                exceptionCaptor.capture(),
            )
        val capturedExtension =
            extensionCaptor.value as mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
        assertEquals(extension, capturedExtension.nativeExtension)

        assertIs<WebExtensionInstallException.Blocklisted>(exceptionCaptor.value)
    }

    @Test
    fun `web extension delegate handles add-on onDisabled event`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("test", "uri")
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.AddonManagerDelegate>()
        verify(webExtensionController).setAddonManagerDelegate(geckoDelegateCaptor.capture())

        assertEquals(Unit, geckoDelegateCaptor.value.onDisabled(extension))
        val extensionCaptor = argumentCaptor<WebExtension>()
        verify(webExtensionsDelegate).onDisabled(extensionCaptor.capture())
        val capturedExtension =
            extensionCaptor.value as mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
        assertEquals(extension, capturedExtension.nativeExtension)
    }

    @Test
    fun `web extension delegate handles add-on onUninstalled event`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("test", "uri")
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.AddonManagerDelegate>()
        verify(webExtensionController).setAddonManagerDelegate(geckoDelegateCaptor.capture())

        assertEquals(Unit, geckoDelegateCaptor.value.onUninstalled(extension))
        val extensionCaptor = argumentCaptor<WebExtension>()
        verify(webExtensionsDelegate).onUninstalled(extensionCaptor.capture())
        val capturedExtension =
            extensionCaptor.value as mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
        assertEquals(extension, capturedExtension.nativeExtension)
    }

    @Test
    fun `web extension delegate handles add-on onInstalled event`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("test", "uri")
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.AddonManagerDelegate>()
        verify(webExtensionController).setAddonManagerDelegate(geckoDelegateCaptor.capture())

        assertEquals(Unit, geckoDelegateCaptor.value.onInstalled(extension))
        val extensionCaptor = argumentCaptor<WebExtension>()
        verify(webExtensionsDelegate).onInstalled(extensionCaptor.capture())
        val capturedExtension =
            extensionCaptor.value as mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
        assertEquals(extension, capturedExtension.nativeExtension)

        // Make sure we called `registerActionHandler()` on the installed extension.
        verify(extension).setActionDelegate(any())
        // Make sure we called `registerTabHandler()` on the installed extension.
        verify(extension).tabDelegate = any()
    }

    @Test
    fun `web extension delegate handles add-on onReady event`() {
        val runtime: GeckoRuntime = mock()
        val webExtensionController: WebExtensionController = mock()
        whenever(runtime.webExtensionController).thenReturn(webExtensionController)

        val extension = mockNativeWebExtension("test", "uri")
        val webExtensionsDelegate: WebExtensionDelegate = mock()
        val engine = GeckoEngine(context, runtime = runtime)
        engine.registerWebExtensionDelegate(webExtensionsDelegate)

        val geckoDelegateCaptor = argumentCaptor<WebExtensionController.AddonManagerDelegate>()
        verify(webExtensionController).setAddonManagerDelegate(geckoDelegateCaptor.capture())

        assertEquals(Unit, geckoDelegateCaptor.value.onReady(extension))
        val extensionCaptor = argumentCaptor<WebExtension>()

        // Note: the "reacts to WebExtensionDelegate onReady" test in
        // WebExtensionSupportTest.kt provides further verification of onReady
        // doing anything meaningful.
        verify(webExtensionsDelegate).onReady(extensionCaptor.capture())
        val capturedExtension =
            extensionCaptor.value as mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
        assertEquals(extension, capturedExtension.nativeExtension)

        // registerTabHandler must be called again on the extension at onReady,
        // to make sure that changes to optionsPageUrl are propagated, and applied
        // when the onOpenOptionsPage delegate is called.
        // This is a regression test for bug 2046177.
        verify(extension).tabDelegate = any()

        // Although there are no known dependencies on setActionDelegate at the
        // time of writing, we also update the action delegate for consistency.
        verify(extension).setActionDelegate(any())
    }

    @Test
    fun `WHEN isTranslationsEngineSupported is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (Boolean) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<Boolean>()

        // simulate successful response call
        `when`(runtimeTranslationAccessor.isTranslationsEngineSupported(onSuccess, onError)).thenAnswer {
            onSuccess.invoke(true)
            geckoResult
        }

        engine.isTranslationsEngineSupported(
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .isTranslationsEngineSupported(
                onSuccess = onSuccess,
                onError = onError,
            )

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `WHEN isTranslationsEngineSupported is called AND excepts THEN onError is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (Boolean) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<Boolean>()

        // simulate unsuccessful response call
        `when`(runtimeTranslationAccessor.isTranslationsEngineSupported(onSuccess, onError)).thenAnswer {
            onError.invoke(Exception())
            geckoResult
        }

        engine.isTranslationsEngineSupported(
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .isTranslationsEngineSupported(
                onSuccess = onSuccess,
                onError = onError,
            )

        assertTrue(onErrorCalled)
        assertFalse(onSuccessCalled)
    }

    @Test
    fun `WHEN getTranslationsPairDownloadSize is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false
        val onSuccess: (Long) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }

        val geckoResult = GeckoResult<Long>()

        // simulate successful response call
        `when`(
                runtimeTranslationAccessor.getTranslationsPairDownloadSize(
                    any(),
                    any(),
                    eq(onSuccess),
                    eq(onError),
                )
            )
            .thenAnswer {
                onSuccess.invoke(2L)
                geckoResult
            }

        engine.getTranslationsPairDownloadSize(
            fromLanguage = "es",
            toLanguage = "en",
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .getTranslationsPairDownloadSize(
                fromLanguage = "es",
                toLanguage = "en",
                onSuccess = onSuccess,
                onError = onError,
            )

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `WHEN getTranslationsPairDownloadSize is called AND excepts THEN onError is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false
        val onSuccess: (Long) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }

        val geckoResult = GeckoResult<Long>()

        // simulate unsuccessful response call
        `when`(
                runtimeTranslationAccessor.getTranslationsPairDownloadSize(
                    any(),
                    any(),
                    eq(onSuccess),
                    eq(onError),
                )
            )
            .thenAnswer {
                onError.invoke(Exception())
                geckoResult
            }

        engine.getTranslationsPairDownloadSize(
            fromLanguage = "es",
            toLanguage = "en",
            onSuccess = onSuccess,
            onError = onError,
        )

        assertTrue(onErrorCalled)
        assertFalse(onSuccessCalled)
    }

    @Test
    fun `WHEN getTranslationsModelDownloadStates is called successfully THEN onSuccess is called AND the LanguageModel maps as expected`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (List<LanguageModel>) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val code = "es"
        val localizedDisplayName = "Spanish"
        val isDownloaded = ModelState.DOWNLOADED
        val size: Long = 1234
        val geckoLanguage = Language(code, localizedDisplayName)
        val geckoLanguageModel = LanguageModel(geckoLanguage, isDownloaded, size)
        val geckoResultValue: List<LanguageModel> = mutableListOf(geckoLanguageModel)
        val geckoResult = GeckoResult<List<LanguageModel>>()

        // simulate successful response call
        `when`(
                runtimeTranslationAccessor.getTranslationsModelDownloadStates(
                    onSuccess,
                    onError,
                )
            )
            .thenAnswer {
                onSuccess.invoke(geckoResultValue)
                geckoResult
            }

        engine.getTranslationsModelDownloadStates(
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .getTranslationsModelDownloadStates(
                onSuccess = onSuccess,
                onError = onError,
            )

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `WHEN getTranslationsModelDownloadStates is called AND excepts THEN onError is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (List<LanguageModel>) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }

        // simulate unsuccessful response call
        `when`(
                runtimeTranslationAccessor.getTranslationsModelDownloadStates(
                    onSuccess,
                    onError,
                )
            )
            .thenAnswer {
                onError.invoke(Exception())
            }

        engine.getTranslationsModelDownloadStates(
            onSuccess = onSuccess,
            onError = onError,
        )

        assertTrue(onErrorCalled)
        assertFalse(onSuccessCalled)
    }

    @Test
    fun `WHEN getSupportedTranslationLanguages is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (TranslationSupport) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<TranslationSupport>()
        val toLanguage = Language("de", "German")
        val fromLanguage = Language("es", "Spanish")
        val geckoResultValue = TranslationSupport(listOf(fromLanguage), listOf(toLanguage))

        // simulate successful response call
        `when`(
                runtimeTranslationAccessor.getSupportedTranslationLanguages(
                    onSuccess,
                    onError,
                )
            )
            .thenAnswer {
                onSuccess.invoke(geckoResultValue)
                geckoResult
            }

        engine.getSupportedTranslationLanguages(
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .getSupportedTranslationLanguages(
                onSuccess = onSuccess,
                onError = onError,
            )

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `WHEN getSupportedTranslationLanguages is called AND excepts THEN onError is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (TranslationSupport) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<TranslationSupport>()

        // simulate unsuccessful response call
        `when`(
                runtimeTranslationAccessor.getSupportedTranslationLanguages(
                    onSuccess,
                    onError,
                )
            )
            .thenAnswer {
                onError.invoke(Exception())
                geckoResult
            }

        engine.getSupportedTranslationLanguages(
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .getSupportedTranslationLanguages(
                onSuccess = onSuccess,
                onError = onError,
            )

        assertTrue(onErrorCalled)
        assertFalse(onSuccessCalled)
    }

    @Test
    fun `WHEN manageTranslationsLanguageModel is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: () -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }

        val options = ModelManagementOptions(null, ModelOperation.DOWNLOAD, OperationLevel.ALL)
        val geckoResult = GeckoResult<Void>()

        // simulate successful response call
        `when`(
                runtimeTranslationAccessor.manageTranslationsLanguageModel(
                    options,
                    onSuccess,
                    onError,
                )
            )
            .thenAnswer {
                onSuccess.invoke()
                geckoResult
            }

        engine.manageTranslationsLanguageModel(
            options = options,
            onSuccess = onSuccess,
            onError = onError,
        )

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `WHEN manageTranslationsLanguageModel is called AND excepts THEN onError is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: () -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }

        val options = ModelManagementOptions(null, ModelOperation.DOWNLOAD, OperationLevel.ALL)
        val geckoResult = GeckoResult<Void>()

        // simulate unsuccessful response call
        `when`(
                runtimeTranslationAccessor.manageTranslationsLanguageModel(
                    options,
                    onSuccess,
                    onError,
                )
            )
            .thenAnswer {
                onError.invoke(Exception())
                geckoResult
            }

        engine.manageTranslationsLanguageModel(
            options = options,
            onSuccess = onSuccess,
            onError = onError,
        )

        assertTrue(onErrorCalled)
        assertFalse(onSuccessCalled)
    }

    @Test
    fun `WHEN getUserPreferredLanguages is called successfully THEN onSuccess is called `() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (List<String>) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<List<String>>()

        // simulate successful response call
        `when`(
                runtimeTranslationAccessor.getUserPreferredLanguages(
                    onSuccess,
                    onError,
                )
            )
            .thenAnswer {
                val geckoResultValue = listOf("en")
                onSuccess.invoke(geckoResultValue)
                geckoResult
            }

        engine.getUserPreferredLanguages(
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .getUserPreferredLanguages(
                onSuccess = onSuccess,
                onError = onError,
            )

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `WHEN getUserPreferredLanguages is called AND excepts THEN onError is called `() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (List<String>) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<List<String>>()

        // simulate unsuccessful response call
        `when`(
                runtimeTranslationAccessor.getUserPreferredLanguages(
                    onSuccess,
                    onError,
                )
            )
            .thenAnswer {
                onError.invoke(Exception())
                geckoResult
            }

        engine.getUserPreferredLanguages(
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .getUserPreferredLanguages(
                onSuccess = onSuccess,
                onError = onError,
            )

        assertTrue(onErrorCalled)
        assertFalse(onSuccessCalled)
    }

    @Test
    fun `WHEN getTranslationsOfferPopup is called successfully THEN a result is retrieved `() {
        val runtime: GeckoRuntime = mock()
        val engine = GeckoEngine(testContext, runtime = runtime)
        val runtimeSettings = mock<GeckoRuntimeSettings>()

        whenever(runtime.settings).thenReturn(runtimeSettings)
        whenever(runtime.settings.translationsOfferPopup).thenReturn(true)

        val result = engine.getTranslationsOfferPopup()
        assert(result) { "Should successfully get a language setting." }
    }

    @Test
    fun `WHEN getLanguageSetting is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (LanguageSetting) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<String>()

        // simulate successful response call
        `when`(
                runtimeTranslationAccessor.getLanguageSetting(
                    any(),
                    eq(onSuccess),
                    eq(onError),
                )
            )
            .thenAnswer {
                onSuccess.invoke(LanguageSetting.ALWAYS)
                geckoResult
            }

        engine.getLanguageSetting(
            "es",
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .getLanguageSetting(
                "es",
                onSuccess,
                onError,
            )

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `WHEN getLanguageSetting is unsuccessful THEN onError is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (LanguageSetting) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<String>()

        // simulate unsuccessful response call
        `when`(
                runtimeTranslationAccessor.getLanguageSetting(
                    any(),
                    eq(onSuccess),
                    eq(onError),
                )
            )
            .thenAnswer {
                onError.invoke(Exception())
                geckoResult
            }

        engine.getLanguageSetting(
            "es",
            onSuccess = onSuccess,
            onError = onError,
        )
        verify(runtimeTranslationAccessor)
            .getLanguageSetting(
                "es",
                onSuccess,
                onError,
            )

        assertTrue(onErrorCalled)
        assertFalse(onSuccessCalled)
    }

    @Test
    fun `WHEN setLanguageSetting is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: () -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<Void>()

        // simulate successful response call
        `when`(
                runtimeTranslationAccessor.setLanguageSetting(
                    any(),
                    any(),
                    eq(onSuccess),
                    eq(onError),
                )
            )
            .thenAnswer {
                onSuccessCalled = true
                geckoResult
            }

        engine.setLanguageSetting(
            "es",
            LanguageSetting.ALWAYS,
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .setLanguageSetting(
                "es",
                LanguageSetting.ALWAYS,
                onSuccess,
                onError,
            )

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `WHEN setLanguageSetting is unsuccessful THEN onError is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: () -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<Void>()

        // simulate unsuccessful response call
        `when`(
                runtimeTranslationAccessor.setLanguageSetting(
                    any(),
                    any(),
                    eq(onSuccess),
                    eq(onError),
                )
            )
            .thenAnswer {
                onError(Exception())
                geckoResult
            }

        engine.setLanguageSetting(
            "es",
            LanguageSetting.ALWAYS,
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .setLanguageSetting(
                "es",
                LanguageSetting.ALWAYS,
                onSuccess,
                onError,
            )

        assertTrue(onErrorCalled)
        assertFalse(onSuccessCalled)
    }

    @Test
    fun `WHEN getLanguageSetting is unrecognized THEN onError is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (LanguageSetting) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<String>()

        // simulate unsuccessful response call
        `when`(
                runtimeTranslationAccessor.getLanguageSetting(
                    any(),
                    eq(onSuccess),
                    eq(onError),
                )
            )
            .thenAnswer {
                onError(Exception())
                geckoResult
            }

        engine.getLanguageSetting(
            "es",
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .getLanguageSetting(
                "es",
                onSuccess,
                onError,
            )

        assertTrue(onErrorCalled)
        assertFalse(onSuccessCalled)
    }

    @Test
    fun `WHEN getLanguageSettings is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (Map<String, LanguageSetting>) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<Map<String, LanguageSetting>>()

        // simulate successful response call
        `when`(
                runtimeTranslationAccessor.getLanguageSettings(
                    onSuccess,
                    onError,
                )
            )
            .thenAnswer {
                val geckoResultValue =
                    mapOf(
                        "es" to LanguageSetting.OFFER,
                        "de" to LanguageSetting.ALWAYS,
                        "fr" to LanguageSetting.NEVER,
                    )
                onSuccess.invoke(geckoResultValue)
                geckoResult
            }

        engine.getLanguageSettings(
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .getLanguageSettings(
                onSuccess = onSuccess,
                onError = onError,
            )

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `WHEN getLanguageSettings is unsuccessful THEN onError is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (Map<String, LanguageSetting>) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<Map<String, String>>()

        // simulate unsuccessful response call
        `when`(
                runtimeTranslationAccessor.getLanguageSettings(
                    onSuccess,
                    onError,
                )
            )
            .thenAnswer {
                onError.invoke(Exception())
                geckoResult
            }

        engine.getLanguageSettings(
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .getLanguageSettings(
                onSuccess = onSuccess,
                onError = onError,
            )

        assertTrue(onErrorCalled)
        assertFalse(onSuccessCalled)
    }

    @Test
    fun `WHEN getNeverTranslateSiteList is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (List<String>) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<List<String>>()

        // simulate successful response call
        `when`(
                runtimeTranslationAccessor.getNeverTranslateSiteList(
                    onSuccess,
                    onError,
                )
            )
            .thenAnswer {
                onSuccess.invoke(listOf("www.mozilla.org"))
                geckoResult
            }

        engine.getNeverTranslateSiteList(
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .getNeverTranslateSiteList(
                onSuccess = onSuccess,
                onError = onError,
            )

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `WHEN getNeverTranslateSiteList is unsuccessful THEN onError is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: (List<String>) -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<List<String>>()

        // simulate unsuccessful response call
        `when`(
                runtimeTranslationAccessor.getNeverTranslateSiteList(
                    onSuccess,
                    onError,
                )
            )
            .thenAnswer {
                onError.invoke(Exception())
                geckoResult
            }

        engine.getNeverTranslateSiteList(
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .getNeverTranslateSiteList(
                onSuccess = onSuccess,
                onError = onError,
            )

        assertTrue(onErrorCalled)
        assertFalse(onSuccessCalled)
    }

    @Test
    fun `WHEN setNeverTranslateSpecifiedSite is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: () -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<Void>()

        // simulate successful response call
        `when`(
                runtimeTranslationAccessor.setNeverTranslateSpecifiedSite(
                    any(),
                    eq(true),
                    eq(onSuccess),
                    eq(onError),
                )
            )
            .thenAnswer {
                onSuccess.invoke()
                geckoResult
            }

        engine.setNeverTranslateSpecifiedSite(
            "www.mozilla.org",
            true,
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .setNeverTranslateSpecifiedSite(
                "www.mozilla.org",
                true,
                onSuccess,
                onError,
            )

        assertTrue(onSuccessCalled)
        assertFalse(onErrorCalled)
    }

    @Test
    fun `WHEN setNeverTranslateSpecifiedSite is unsuccessful THEN onError is called`() {
        val runtime: GeckoRuntime = mock()
        val engine =
            GeckoEngine(testContext, runtime = runtime, runtimeTranslationAccessor = runtimeTranslationAccessor)

        var onSuccessCalled = false
        var onErrorCalled = false

        val onSuccess: () -> Unit = { onSuccessCalled = true }
        val onError: (Throwable) -> Unit = { onErrorCalled = true }
        val geckoResult = GeckoResult<List<String>>()

        // simulate unsuccessful response call
        `when`(
                runtimeTranslationAccessor.setNeverTranslateSpecifiedSite(
                    any(),
                    eq(true),
                    eq(onSuccess),
                    eq(onError),
                )
            )
            .thenAnswer {
                onError.invoke(Exception())
                geckoResult
            }

        engine.setNeverTranslateSpecifiedSite(
            "www.mozilla.org",
            true,
            onSuccess = onSuccess,
            onError = onError,
        )

        verify(runtimeTranslationAccessor)
            .setNeverTranslateSpecifiedSite(
                "www.mozilla.org",
                true,
                onSuccess,
                onError,
            )

        assertTrue(onErrorCalled)
        assertFalse(onSuccessCalled)
    }

    @Test
    fun `WHEN Global Privacy Control value is set THEN setGlobalPrivacyControl is getting called on GeckoRuntime`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        reset(mockRuntime.settings)
        engine.settings.globalPrivacyControlEnabled = true
        verify(mockRuntime.settings).setGlobalPrivacyControl(true)

        reset(mockRuntime.settings)
        engine.settings.globalPrivacyControlEnabled = false
        verify(mockRuntime.settings).setGlobalPrivacyControl(false)
    }

    @Test
    fun `WHEN Suspected Fingerprinting Protection value is set THEN setFingerprintingProtection is getting called on GeckoRuntime`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        reset(mockRuntime.settings)
        engine.settings.fingerprintingProtection = true
        verify(mockRuntime.settings).setFingerprintingProtection(true)

        reset(mockRuntime.settings)
        engine.settings.fingerprintingProtection = false
        verify(mockRuntime.settings).setFingerprintingProtection(false)

        reset(mockRuntime.settings)
        engine.settings.fingerprintingProtectionPrivateBrowsing = true
        verify(mockRuntime.settings).setFingerprintingProtectionPrivateBrowsing(true)

        reset(mockRuntime.settings)
        engine.settings.fingerprintingProtectionPrivateBrowsing = false
        verify(mockRuntime.settings).setFingerprintingProtectionPrivateBrowsing(false)
    }

    @Test
    fun `WHEN Fingerprinting Protection Overrides is set THEN setFingerprintingProtectionOverrides is getting called on GeckoRuntime`() {
        val mockRuntime = mock<GeckoRuntime>()
        whenever(mockRuntime.settings).thenReturn(mock())

        val engine = GeckoEngine(testContext, runtime = mockRuntime)

        reset(mockRuntime.settings)
        engine.settings.fingerprintingProtectionOverrides = "+AllTargets"
        verify(mockRuntime.settings).setFingerprintingProtectionOverrides("+AllTargets")

        reset(mockRuntime.settings)
        engine.settings.fingerprintingProtectionOverrides = "-AllTargets"
        verify(mockRuntime.settings).setFingerprintingProtectionOverrides("-AllTargets")

        reset(mockRuntime.settings)
        engine.settings.fingerprintingProtectionOverrides = ""
        verify(mockRuntime.settings).setFingerprintingProtectionOverrides("")
    }

    @Test
    fun `GIVEN an InstallationMethod WHEN calling toGeckoInstallationMethod THEN translate to counterpart WebExtensionController#INSTALLATION_METHOD`() {
        assertEquals(
            WebExtensionController.INSTALLATION_METHOD_MANAGER,
            InstallationMethod.MANAGER.toGeckoInstallationMethod(),
        )

        assertEquals(
            WebExtensionController.INSTALLATION_METHOD_FROM_FILE,
            InstallationMethod.FROM_FILE.toGeckoInstallationMethod(),
        )

        assertEquals(
            WebExtensionController.INSTALLATION_METHOD_ONBOARDING,
            InstallationMethod.ONBOARDING.toGeckoInstallationMethod(),
        )

        assertEquals(
            WebExtensionController.INSTALLATION_METHOD_RTAMO,
            InstallationMethod.RTAMO.toGeckoInstallationMethod(),
        )
    }

    @Test
    fun `WHEN registerPrefForObservation is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<Void>()
        val geckoResultValue = null

        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()
        whenever(geckoPreferenceAccessor.registerGeckoPrefForObservation(anyString())).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.registerPrefForObservation(
            anyString(),
            onSuccess = { onSuccessCalled = true },
            onError = { onErrorCalled = true },
        )

        geckoResult.complete(geckoResultValue)
        shadowOf(getMainLooper()).idle()

        assert(onSuccessCalled) { "Should have successfully registered." }
        assert(!onErrorCalled) { "Should not have called onError." }
    }

    @Test
    fun `WHEN registerPrefsForObservation is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<Void>()
        val geckoResultValue = null

        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()
        whenever(geckoPreferenceAccessor.registerGeckoPrefsForObservation(anyList<String>())).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.registerPrefsForObservation(
            anyList<String>(),
            onSuccess = { onSuccessCalled = true },
            onError = { onErrorCalled = true },
        )

        geckoResult.complete(geckoResultValue)
        shadowOf(getMainLooper()).idle()

        assert(onSuccessCalled) { "Should have successfully registered." }
        assert(!onErrorCalled) { "Should not have called onError." }
    }

    @Test
    fun `WHEN registerPrefForObservation is called unsuccessfully THEN onError is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<Void>()

        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()
        whenever(geckoPreferenceAccessor.registerGeckoPrefForObservation(anyString())).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.registerPrefForObservation(
            anyString(),
            onSuccess = { onSuccessCalled = true },
            onError = { onErrorCalled = true },
        )

        geckoResult.completeExceptionally(Exception())
        shadowOf(getMainLooper()).idle()

        assert(!onSuccessCalled) { "Should not have successfully registered." }
        assert(onErrorCalled) { "Should have called onError." }
    }

    @Test
    fun `WHEN registerPrefsForObservations is called unsuccessfully THEN onError is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<Void>()

        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()
        whenever(geckoPreferenceAccessor.registerGeckoPrefsForObservation(anyList<String>())).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.registerPrefsForObservation(
            anyList<String>(),
            onSuccess = { onSuccessCalled = true },
            onError = { onErrorCalled = true },
        )

        geckoResult.completeExceptionally(Exception())
        shadowOf(getMainLooper()).idle()

        assert(!onSuccessCalled) { "Should not have successfully registered." }
        assert(onErrorCalled) { "Should have called onError." }
    }

    @Test
    fun `WHEN unregisterPrefForObservation is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<Void>()
        val geckoResultValue = null

        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()
        whenever(geckoPreferenceAccessor.unregisterGeckoPrefForObservation(anyString())).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.unregisterPrefForObservation(
            anyString(),
            onSuccess = { onSuccessCalled = true },
            onError = { onErrorCalled = true },
        )

        geckoResult.complete(geckoResultValue)
        shadowOf(getMainLooper()).idle()

        assert(onSuccessCalled) { "Should have successfully registered." }
        assert(!onErrorCalled) { "Should not have called onError." }
    }

    @Test
    fun `WHEN unregisterPrefsForObservation is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<Void>()
        val geckoResultValue = null

        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()
        whenever(geckoPreferenceAccessor.unregisterGeckoPrefsForObservation(anyList<String>())).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.unregisterPrefsForObservation(
            anyList<String>(),
            onSuccess = { onSuccessCalled = true },
            onError = { onErrorCalled = true },
        )

        geckoResult.complete(geckoResultValue)
        shadowOf(getMainLooper()).idle()

        assert(onSuccessCalled) { "Should have successfully registered." }
        assert(!onErrorCalled) { "Should not have called onError." }
    }

    @Test
    fun `WHEN unregisterPrefForObservation is called unsuccessfully THEN onError is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<Void>()

        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()
        whenever(geckoPreferenceAccessor.unregisterGeckoPrefForObservation(anyString())).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.unregisterPrefForObservation(
            anyString(),
            onSuccess = { onSuccessCalled = true },
            onError = { onErrorCalled = true },
        )

        geckoResult.completeExceptionally(Exception())
        shadowOf(getMainLooper()).idle()

        assert(!onSuccessCalled) { "Should not have successfully registered." }
        assert(onErrorCalled) { "Should have called onError." }
    }

    @Test
    fun `WHEN unregisterPrefsForObservation is called unsuccessfully THEN onError is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<Void>()

        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()
        whenever(geckoPreferenceAccessor.unregisterGeckoPrefsForObservation(anyList<String>())).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.unregisterPrefsForObservation(
            anyList<String>(),
            onSuccess = { onSuccessCalled = true },
            onError = { onErrorCalled = true },
        )

        geckoResult.completeExceptionally(Exception())
        shadowOf(getMainLooper()).idle()

        assert(!onSuccessCalled) { "Should not have successfully registered." }
        assert(onErrorCalled) { "Should have called onError." }
    }

    @Test
    fun `WHEN getBrowserPref is called with null THEN onError is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<GeckoPreference<*>?>()
        val geckoPref = "test.test.test"
        val geckoResultValue = null

        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()
        whenever(geckoPreferenceAccessor.getGeckoPref(geckoPref)).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.getBrowserPref(
            geckoPref,
            onSuccess = {
                onSuccessCalled = true
            },
            onError = { onErrorCalled = true },
        )

        geckoResult.complete(geckoResultValue)
        shadowOf(getMainLooper()).idle()

        assert(!onSuccessCalled) { "Should not have been successful on a null preference." }
        assert(onErrorCalled) { "Should not be able to process a null Gecko preference." }
    }

    @Test
    fun `WHEN getBrowserPrefs is called with null THEN onError is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<List<GeckoPreference<*>>>()
        val geckoPrefs = listOf("test.test.test")
        val geckoResultValue = null

        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()
        whenever(geckoPreferenceAccessor.getGeckoPrefs(any())).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.getBrowserPrefs(
            prefs = geckoPrefs,
            onSuccess = {
                onSuccessCalled = true
            },
            onError = { onErrorCalled = true },
        )

        geckoResult.complete(geckoResultValue)
        shadowOf(getMainLooper()).idle()

        assert(!onSuccessCalled) { "Should not have been successful on a null preference." }
        assert(onErrorCalled) { "Should not be able to process a null Gecko preference." }
    }

    @Test
    fun `WHEN setBrowserPref is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<Void>()
        val geckoResultValue = null

        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()
        whenever(geckoPreferenceAccessor.setGeckoPref(anyString(), anyInt(), anyInt())).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.setBrowserPref(
            "test.test.test",
            1,
            Branch.USER,
            onSuccess = {
                onSuccessCalled = true
            },
            onError = { onErrorCalled = true },
        )

        geckoResult.complete(geckoResultValue)
        shadowOf(getMainLooper()).idle()

        assert(onSuccessCalled) { "Should have successfully completed." }
        assert(!onErrorCalled) { "Should not have called an error." }
    }

    @Test
    fun `WHEN setBrowserPrefs is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<Map<String, Boolean>>()
        val geckoResultValue = mapOf("some.pref" to true)

        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()

        whenever(geckoPreferenceAccessor.setGeckoPrefs(any())).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        val request =
            listOf<SetBrowserPreference<*>>(
                SetBrowserPreference.setStringPref(
                    pref = "some.pref",
                    value = "hello-world",
                    branch = Branch.USER,
                )
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.setBrowserPrefs(
            prefs = request,
            onSuccess = {
                onSuccessCalled = true
            },
            onError = { onErrorCalled = true },
        )

        geckoResult.complete(geckoResultValue)
        shadowOf(getMainLooper()).idle()

        assert(onSuccessCalled) { "Should have successfully completed." }
        assert(!onErrorCalled) { "Should not have called an error." }
    }

    @Test
    fun `WHEN setBrowserPrefs is called and fails THEN onError is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<Map<String, Boolean>>()
        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()

        whenever(geckoPreferenceAccessor.setGeckoPrefs(any())).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        val request =
            listOf<SetBrowserPreference<*>>(
                SetBrowserPreference.setStringPref(
                    pref = "some.pref",
                    value = "hello-world",
                    branch = Branch.USER,
                )
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.setBrowserPrefs(
            prefs = request,
            onSuccess = {
                onSuccessCalled = true
            },
            onError = { onErrorCalled = true },
        )

        geckoResult.completeExceptionally(Throwable("An issue occured!"))
        shadowOf(getMainLooper()).idle()

        assert(!onSuccessCalled) { "Should have not successfully completed." }
        assert(onErrorCalled) { "Should have called an error." }
    }

    @Test
    fun `WHEN clearBrowserUserPref is called successfully THEN onSuccess is called`() {
        val runtime: GeckoRuntime = mock()

        var onSuccessCalled = false
        var onErrorCalled = false

        val geckoResult = GeckoResult<Void>()
        val geckoResultValue = null

        val geckoPreferenceAccessor = mock<GeckoPreferenceAccessor>()
        whenever(geckoPreferenceAccessor.clearGeckoUserPref(any())).thenReturn(geckoResult)

        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                geckoPreferenceAccessor = geckoPreferenceAccessor,
            )

        @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
        engine.clearBrowserUserPref(
            "test.test.test",
            onSuccess = {
                onSuccessCalled = true
            },
            onError = { onErrorCalled = true },
        )

        geckoResult.complete(geckoResultValue)
        shadowOf(getMainLooper()).idle()

        assert(onSuccessCalled) { "Should have successfully completed." }
        assert(!onErrorCalled) { "Should not have called an error." }
    }

    @Test
    fun `WHEN getAddressStructure is called THEN addressStructureAccessor should be called`() {
        val addressStructureAccessor = mock<RuntimeAddressStructureAccessor>()
        val engine =
            GeckoEngine(
                testContext,
                runtime = runtime,
                addressStructureAccessor = addressStructureAccessor,
            )
        engine.getAddressStructure("JP", { _ -> }, { _ -> })
        verify(addressStructureAccessor).getAddressStructure(eq("JP"), any(), any())
    }

    @Test
    fun `GIVEN BLOCKED_EMAILTRACKING_CONTENT WHEN getBlockedCategory is called THEN it returns EMAIL`() {
        val blockingData = createBlockingData(Event.BLOCKED_EMAILTRACKING_CONTENT)

        assertEquals(TrackingCategory.EMAIL, blockingData.getBlockedCategory())
    }

    @Test
    fun `GIVEN BLOCKED_FINGERPRINTING_CONTENT WHEN getBlockedCategory is called THEN it returns FINGERPRINTING`() {
        val blockingData = createBlockingData(Event.BLOCKED_FINGERPRINTING_CONTENT)

        assertEquals(TrackingCategory.FINGERPRINTING, blockingData.getBlockedCategory())
    }

    @Test
    fun `GIVEN BLOCKED_SUSPICIOUS_FINGERPRINTING WHEN getBlockedCategory is called THEN it returns FINGERPRINTING`() {
        val blockingData = createBlockingData(Event.BLOCKED_SUSPICIOUS_FINGERPRINTING)

        assertEquals(TrackingCategory.FINGERPRINTING, blockingData.getBlockedCategory())
    }

    @Test
    fun `GIVEN BLOCKED_CRYPTOMINING_CONTENT WHEN getBlockedCategory is called THEN it returns CRYPTOMINING`() {
        val blockingData = createBlockingData(Event.BLOCKED_CRYPTOMINING_CONTENT)

        assertEquals(TrackingCategory.CRYPTOMINING, blockingData.getBlockedCategory())
    }

    @Test
    fun `GIVEN BLOCKED_SOCIALTRACKING_CONTENT WHEN getBlockedCategory is called THEN it returns MOZILLA_SOCIAL`() {
        val blockingData = createBlockingData(Event.BLOCKED_SOCIALTRACKING_CONTENT)

        assertEquals(TrackingCategory.MOZILLA_SOCIAL, blockingData.getBlockedCategory())
    }

    @Test
    fun `GIVEN COOKIES_BLOCKED_SOCIALTRACKER WHEN getBlockedCategory is called THEN it returns MOZILLA_SOCIAL`() {
        val blockingData = createBlockingData(Event.COOKIES_BLOCKED_SOCIALTRACKER)

        assertEquals(TrackingCategory.MOZILLA_SOCIAL, blockingData.getBlockedCategory())
    }

    @Test
    fun `GIVEN BLOCKED_TRACKING_CONTENT WHEN getBlockedCategory is called THEN it returns SCRIPTS_AND_SUB_RESOURCES`() {
        val blockingData = createBlockingData(Event.BLOCKED_TRACKING_CONTENT)

        assertEquals(TrackingCategory.SCRIPTS_AND_SUB_RESOURCES, blockingData.getBlockedCategory())
    }

    @Test
    fun `GIVEN an unmapped event WHEN getBlockedCategory is called THEN it returns NONE`() {
        val blockingData = createBlockingData(Event.LOADED_LEVEL_1_TRACKING_CONTENT)

        assertEquals(TrackingCategory.NONE, blockingData.getBlockedCategory())
    }

    @Test
    fun `GIVEN COOKIES_PARTITIONED_TRACKER WHEN hasBlockedCookies is called THEN it returns true`() {
        val blockingData = createBlockingData(Event.COOKIES_PARTITIONED_TRACKER)

        assertTrue(blockingData.hasBlockedCookies())
    }

    @Test
    fun `GIVEN COOKIES_LOADED WHEN hasBlockedCookies is called THEN it returns false`() {
        val blockingData = createBlockingData(Event.COOKIES_LOADED)

        assertFalse(blockingData.hasBlockedCookies())
    }

    @Test
    fun `GIVEN LOADED_LEVEL_1_TRACKING_CONTENT WHEN getLoadedCategory is called THEN it returns SCRIPTS_AND_SUB_RESOURCES`() {
        val blockingData = createBlockingData(Event.LOADED_LEVEL_1_TRACKING_CONTENT)

        with(GeckoEngine(context, runtime = runtime)) {
            assertEquals(
                TrackingCategory.SCRIPTS_AND_SUB_RESOURCES,
                blockingData.getLoadedCategory(),
            )
        }
    }

    private fun createSocialTrackersLogEntryList(): List<ContentBlockingController.LogEntry> {
        val blockedLogEntry = object : ContentBlockingController.LogEntry() {}

        ReflectionUtils.setField(blockedLogEntry, "origin", "www.tracker.com")
        val blockedCookieSocialTracker = createBlockingData(Event.COOKIES_BLOCKED_SOCIALTRACKER)
        val blockedSocialContent = createBlockingData(Event.BLOCKED_SOCIALTRACKING_CONTENT)

        ReflectionUtils.setField(
            blockedLogEntry,
            "blockingData",
            listOf(blockedSocialContent, blockedCookieSocialTracker),
        )

        val loadedLogEntry = object : ContentBlockingController.LogEntry() {}
        ReflectionUtils.setField(loadedLogEntry, "origin", "www.tracker2.com")

        val loadedCookieSocialTracker = createBlockingData(Event.COOKIES_LOADED_SOCIALTRACKER)
        val loadedSocialContent = createBlockingData(Event.LOADED_SOCIALTRACKING_CONTENT)

        ReflectionUtils.setField(loadedLogEntry, "blockingData", listOf(loadedCookieSocialTracker, loadedSocialContent))

        return listOf(blockedLogEntry, loadedLogEntry)
    }

    private fun createBlockedCrossSiteCookiesLogEntryList(): List<ContentBlockingController.LogEntry> {
        val blockedLogEntry = object : ContentBlockingController.LogEntry() {}

        ReflectionUtils.setField(blockedLogEntry, "origin", "www.data-collector.com")
        val blockedCrossSiteCookie = createBlockingData(Event.COOKIES_PARTITIONED_TRACKER)

        ReflectionUtils.setField(blockedLogEntry, "blockingData", listOf(blockedCrossSiteCookie))

        return listOf(blockedLogEntry)
    }

    private fun createEmailTrackerLogEntryList(): List<ContentBlockingController.LogEntry> {
        val blockedLogEntry = object : ContentBlockingController.LogEntry() {}

        ReflectionUtils.setField(blockedLogEntry, "origin", "www.email-tracker.com")
        val blockedEmailTracking = createBlockingData(Event.BLOCKED_EMAILTRACKING_CONTENT)

        ReflectionUtils.setField(blockedLogEntry, "blockingData", listOf(blockedEmailTracking))

        return listOf(blockedLogEntry)
    }

    private fun createPurgedBounceTrackerLogEntryList(): List<ContentBlockingController.LogEntry> {
        val blockedLogEntry = object : ContentBlockingController.LogEntry() {}

        ReflectionUtils.setField(blockedLogEntry, "origin", "www.bounce-tracker.com")
        val purgedBounceTracker = createBlockingData(Event.PURGED_BOUNCETRACKER)

        ReflectionUtils.setField(blockedLogEntry, "blockingData", listOf(purgedBounceTracker))

        return listOf(blockedLogEntry)
    }

    private fun createReplacedFingerprintingLogEntryList(): List<ContentBlockingController.LogEntry> {
        val blockedLogEntry = object : ContentBlockingController.LogEntry() {}

        ReflectionUtils.setField(blockedLogEntry, "origin", "www.fingerprinting-shim.com")
        val replacedFingerprinting = createBlockingData(Event.REPLACED_FINGERPRINTING_CONTENT)

        ReflectionUtils.setField(blockedLogEntry, "blockingData", listOf(replacedFingerprinting))

        return listOf(blockedLogEntry)
    }

    private fun createDummyLogEntryList(): List<ContentBlockingController.LogEntry> {
        val addLogEntry = object : ContentBlockingController.LogEntry() {}

        ReflectionUtils.setField(addLogEntry, "origin", "www.tracker.com")
        val blockedCookiePermission = createBlockingData(Event.COOKIES_BLOCKED_BY_PERMISSION)
        val loadedCookieSocialTracker = createBlockingData(Event.COOKIES_LOADED_SOCIALTRACKER)
        val blockedCookieSocialTracker = createBlockingData(Event.COOKIES_BLOCKED_SOCIALTRACKER)

        val blockedTrackingContent = createBlockingData(Event.BLOCKED_TRACKING_CONTENT)
        val blockedFingerprintingContent = createBlockingData(Event.BLOCKED_FINGERPRINTING_CONTENT)
        val blockedSuspiciousFingerprinting = createBlockingData(Event.BLOCKED_SUSPICIOUS_FINGERPRINTING)
        val blockedCyptominingContent = createBlockingData(Event.BLOCKED_CRYPTOMINING_CONTENT)
        val blockedSocialContent = createBlockingData(Event.BLOCKED_SOCIALTRACKING_CONTENT)
        val purgedBounceTracker = createBlockingData(Event.PURGED_BOUNCETRACKER)

        val loadedTrackingLevel1Content = createBlockingData(Event.LOADED_LEVEL_1_TRACKING_CONTENT)
        val loadedTrackingLevel2Content = createBlockingData(Event.LOADED_LEVEL_2_TRACKING_CONTENT)
        val loadedFingerprintingContent = createBlockingData(Event.LOADED_FINGERPRINTING_CONTENT)
        val loadedCyptominingContent = createBlockingData(Event.LOADED_CRYPTOMINING_CONTENT)
        val loadedSocialContent = createBlockingData(Event.LOADED_SOCIALTRACKING_CONTENT)
        val unBlockedBySmartBlock = createBlockingData(Event.ALLOWED_TRACKING_CONTENT)

        val contentBlockingList =
            listOf(
                blockedTrackingContent,
                loadedTrackingLevel1Content,
                loadedTrackingLevel2Content,
                blockedFingerprintingContent,
                loadedFingerprintingContent,
                blockedSuspiciousFingerprinting,
                blockedCyptominingContent,
                loadedCyptominingContent,
                blockedCookiePermission,
                blockedSocialContent,
                loadedSocialContent,
                purgedBounceTracker,
                loadedCookieSocialTracker,
                blockedCookieSocialTracker,
                unBlockedBySmartBlock,
            )

        val addLogSecondEntry = object : ContentBlockingController.LogEntry() {}
        ReflectionUtils.setField(addLogSecondEntry, "origin", "www.tracker2.com")
        val contentBlockingSecondEntryList = listOf(loadedTrackingLevel2Content)

        ReflectionUtils.setField(addLogEntry, "blockingData", contentBlockingList)
        ReflectionUtils.setField(addLogSecondEntry, "blockingData", contentBlockingSecondEntryList)

        return listOf(addLogEntry, addLogSecondEntry)
    }

    private fun createShimmedEntryList(): List<ContentBlockingController.LogEntry> {
        val addLogEntry = object : ContentBlockingController.LogEntry() {}

        ReflectionUtils.setField(addLogEntry, "origin", "www.tracker.com")
        val shimmedContent = createBlockingData(Event.REPLACED_TRACKING_CONTENT, 2)
        val loadedTrackingLevel1Content = createBlockingData(Event.LOADED_LEVEL_1_TRACKING_CONTENT)
        val loadedSocialContent = createBlockingData(Event.LOADED_SOCIALTRACKING_CONTENT)

        val contentBlockingList =
            listOf(
                loadedTrackingLevel1Content,
                loadedSocialContent,
                shimmedContent,
            )

        ReflectionUtils.setField(addLogEntry, "blockingData", contentBlockingList)

        return listOf(addLogEntry)
    }

    private fun createBlockingData(category: Int, count: Int = 0): ContentBlockingController.LogEntry.BlockingData {
        val blockingData = object : ContentBlockingController.LogEntry.BlockingData() {}
        ReflectionUtils.setField(blockingData, "category", category)
        ReflectionUtils.setField(blockingData, "count", count)
        return blockingData
    }

    private fun mockGeckoInstallException(errorCode: Int): GeckoInstallException {
        val exception = object : GeckoInstallException() {}
        ReflectionUtils.setField(exception, "code", errorCode)
        return exception
    }
}
