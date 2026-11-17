/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.engine

import android.graphics.Color
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy
import mozilla.components.concept.engine.history.HistoryTrackingDelegate
import mozilla.components.concept.engine.mediaquery.PreferredColorScheme
import mozilla.components.concept.engine.request.RequestInterceptor
import mozilla.components.support.test.expectException
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SettingsTest {

    @Test
    fun settingsThrowByDefault() {
        val settings = object : Settings() {}

        expectUnsupportedSettingException(
            { settings.javascriptEnabled },
            { settings.javascriptEnabled = false },
            { settings.domStorageEnabled },
            { settings.domStorageEnabled = false },
            { settings.webFontsEnabled },
            { settings.webFontsEnabled = false },
            { settings.automaticFontSizeAdjustment },
            { settings.automaticFontSizeAdjustment = false },
            { settings.automaticLanguageAdjustment },
            { settings.automaticLanguageAdjustment = false },
            { settings.trackingProtectionPolicy },
            { settings.trackingProtectionPolicy = TrackingProtectionPolicy.strict() },
            { settings.historyTrackingDelegate },
            { settings.historyTrackingDelegate = null },
            { settings.requestInterceptor },
            { settings.requestInterceptor = null },
            { settings.userAgentString },
            { settings.userAgentString = null },
            { settings.mediaPlaybackRequiresUserGesture },
            { settings.mediaPlaybackRequiresUserGesture = false },
            { settings.javaScriptCanOpenWindowsAutomatically },
            { settings.javaScriptCanOpenWindowsAutomatically = false },
            { settings.displayZoomControls },
            { settings.displayZoomControls = false },
            { settings.loadWithOverviewMode },
            { settings.loadWithOverviewMode = false },
            { settings.useWideViewPort },
            { settings.useWideViewPort = null },
            { settings.allowFileAccess },
            { settings.allowFileAccess = false },
            { settings.allowContentAccess },
            { settings.allowContentAccess = false },
            { settings.allowFileAccessFromFileURLs },
            { settings.allowFileAccessFromFileURLs = false },
            { settings.allowUniversalAccessFromFileURLs },
            { settings.allowUniversalAccessFromFileURLs = false },
            { settings.verticalScrollBarEnabled },
            { settings.verticalScrollBarEnabled = false },
            { settings.horizontalScrollBarEnabled },
            { settings.horizontalScrollBarEnabled = false },
            { settings.remoteDebuggingEnabled },
            { settings.remoteDebuggingEnabled = false },
            { settings.supportMultipleWindows },
            { settings.supportMultipleWindows = false },
            { settings.preferredColorScheme },
            { settings.preferredColorScheme = PreferredColorScheme.System },
            { settings.testingModeEnabled },
            { settings.testingModeEnabled = false },
            { settings.suspendMediaWhenInactive },
            { settings.suspendMediaWhenInactive = false },
            { settings.fontInflationEnabled },
            { settings.fontInflationEnabled = false },
            { settings.fontSizeFactor },
            { settings.fontSizeFactor = 1.0F },
            { settings.forceUserScalableContent },
            { settings.forceUserScalableContent = true },
            { settings.loginAutofillEnabled },
            { settings.loginAutofillEnabled = false },
            { settings.creditCardSecurityCodeEnabled },
            { settings.creditCardSecurityCodeEnabled = false },
            { settings.clearColor },
            { settings.clearColor = Color.BLUE },
            { settings.enterpriseRootsEnabled },
            { settings.enterpriseRootsEnabled = false },
            { settings.emailTrackerBlockingPrivateBrowsing },
        )
    }

    private fun expectUnsupportedSettingException(vararg blocks: () -> Unit) {
        blocks.forEach { block ->
            expectException<UnsupportedSettingException>(block)
        }
    }

    @Test
    fun defaultSettings() {
        val settings = DefaultSettings()
        assertTrue(settings.domStorageEnabled)
        assertTrue(settings.javascriptEnabled)
        assertNull(settings.trackingProtectionPolicy)
        assertNull(settings.historyTrackingDelegate)
        assertNull(settings.requestInterceptor)
        assertNull(settings.userAgentString)
        assertTrue(settings.mediaPlaybackRequiresUserGesture)
        assertFalse(settings.javaScriptCanOpenWindowsAutomatically)
        assertTrue(settings.displayZoomControls)
        assertTrue(settings.automaticFontSizeAdjustment)
        assertTrue(settings.automaticLanguageAdjustment)
        assertFalse(settings.loadWithOverviewMode)
        assertNull(settings.useWideViewPort)
        assertTrue(settings.allowContentAccess)
        assertTrue(settings.allowFileAccess)
        assertFalse(settings.allowFileAccessFromFileURLs)
        assertFalse(settings.allowUniversalAccessFromFileURLs)
        assertTrue(settings.verticalScrollBarEnabled)
        assertTrue(settings.horizontalScrollBarEnabled)
        assertFalse(settings.remoteDebuggingEnabled)
        assertFalse(settings.supportMultipleWindows)
        assertEquals(PreferredColorScheme.System, settings.preferredColorScheme)
        assertFalse(settings.testingModeEnabled)
        assertFalse(settings.suspendMediaWhenInactive)
        assertNull(settings.fontInflationEnabled)
        assertNull(settings.fontSizeFactor)
        assertFalse(settings.forceUserScalableContent)
        assertFalse(settings.loginAutofillEnabled)
        assertFalse(settings.creditCardSecurityCodeEnabled)
        assertNull(settings.clearColor)
        assertFalse(settings.enterpriseRootsEnabled)
        assertFalse(settings.queryParameterStripping)
        assertFalse(settings.queryParameterStrippingPrivateBrowsing)
        assertFalse(settings.emailTrackerBlockingPrivateBrowsing)
        assertEquals("", settings.queryParameterStrippingAllowList)
        assertEquals("", settings.queryParameterStrippingStripList)

        val interceptor: RequestInterceptor = mock()
        val historyTrackingDelegate: HistoryTrackingDelegate = mock()

        val defaultSettings =
            DefaultSettings(
                javascriptEnabled = false,
                domStorageEnabled = false,
                webFontsEnabled = false,
                automaticFontSizeAdjustment = false,
                automaticLanguageAdjustment = false,
                trackingProtectionPolicy = TrackingProtectionPolicy.strict(),
                historyTrackingDelegate = historyTrackingDelegate,
                requestInterceptor = interceptor,
                userAgentString = "userAgent",
                mediaPlaybackRequiresUserGesture = false,
                javaScriptCanOpenWindowsAutomatically = true,
                displayZoomControls = false,
                loadWithOverviewMode = true,
                useWideViewPort = true,
                allowContentAccess = false,
                allowFileAccess = false,
                allowFileAccessFromFileURLs = true,
                allowUniversalAccessFromFileURLs = true,
                verticalScrollBarEnabled = false,
                horizontalScrollBarEnabled = false,
                remoteDebuggingEnabled = true,
                supportMultipleWindows = true,
                preferredColorScheme = PreferredColorScheme.Dark,
                testingModeEnabled = true,
                suspendMediaWhenInactive = true,
                fontInflationEnabled = false,
                fontSizeFactor = 2.0F,
                forceUserScalableContent = true,
                loginAutofillEnabled = true,
                creditCardSecurityCodeEnabled = true,
                clearColor = Color.BLUE,
                enterpriseRootsEnabled = true,
                queryParameterStripping = true,
                queryParameterStrippingPrivateBrowsing = true,
                queryParameterStrippingAllowList = "AllowList",
                queryParameterStrippingStripList = "StripList",
                emailTrackerBlockingPrivateBrowsing = true,
            )

        assertFalse(defaultSettings.domStorageEnabled)
        assertFalse(defaultSettings.javascriptEnabled)
        assertFalse(defaultSettings.webFontsEnabled)
        assertFalse(defaultSettings.automaticFontSizeAdjustment)
        assertFalse(defaultSettings.automaticLanguageAdjustment)
        assertEquals(TrackingProtectionPolicy.strict(), defaultSettings.trackingProtectionPolicy)
        assertEquals(historyTrackingDelegate, defaultSettings.historyTrackingDelegate)
        assertEquals(interceptor, defaultSettings.requestInterceptor)
        assertEquals("userAgent", defaultSettings.userAgentString)
        assertFalse(defaultSettings.mediaPlaybackRequiresUserGesture)
        assertTrue(defaultSettings.javaScriptCanOpenWindowsAutomatically)
        assertFalse(defaultSettings.displayZoomControls)
        assertTrue(defaultSettings.loadWithOverviewMode)
        assertEquals(defaultSettings.useWideViewPort, true)
        assertFalse(defaultSettings.allowContentAccess)
        assertFalse(defaultSettings.allowFileAccess)
        assertTrue(defaultSettings.allowFileAccessFromFileURLs)
        assertTrue(defaultSettings.allowUniversalAccessFromFileURLs)
        assertFalse(defaultSettings.verticalScrollBarEnabled)
        assertFalse(defaultSettings.horizontalScrollBarEnabled)
        assertTrue(defaultSettings.remoteDebuggingEnabled)
        assertTrue(defaultSettings.supportMultipleWindows)
        assertEquals(PreferredColorScheme.Dark, defaultSettings.preferredColorScheme)
        assertTrue(defaultSettings.testingModeEnabled)
        assertTrue(defaultSettings.suspendMediaWhenInactive)
        assertFalse(defaultSettings.fontInflationEnabled!!)
        assertEquals(2.0F, defaultSettings.fontSizeFactor)
        assertTrue(defaultSettings.forceUserScalableContent)
        assertTrue(defaultSettings.creditCardSecurityCodeEnabled)
        assertEquals(Color.BLUE, defaultSettings.clearColor)
        assertTrue(defaultSettings.enterpriseRootsEnabled)
        assertTrue(defaultSettings.queryParameterStripping)
        assertTrue(defaultSettings.queryParameterStrippingPrivateBrowsing)
        assertEquals("AllowList", defaultSettings.queryParameterStrippingAllowList)
        assertEquals("StripList", defaultSettings.queryParameterStrippingStripList)
        assertTrue(defaultSettings.emailTrackerBlockingPrivateBrowsing)
    }
}
