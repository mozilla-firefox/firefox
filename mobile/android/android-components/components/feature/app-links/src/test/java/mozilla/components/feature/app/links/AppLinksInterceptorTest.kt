/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.app.links

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import androidx.fragment.app.FragmentManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.ExternalPackage
import mozilla.components.browser.state.state.PackageCategory
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.request.RequestInterceptor
import mozilla.components.feature.app.links.AppLinksInterceptor.Companion.APP_LINKS_DO_NOT_INTERCEPT_INTERVAL
import mozilla.components.feature.app.links.AppLinksInterceptor.Companion.APP_LINKS_DO_NOT_OPEN_CACHE_INTERVAL
import mozilla.components.feature.app.links.AppLinksInterceptor.Companion.addUserDoNotIntercept
import mozilla.components.feature.app.links.AppLinksInterceptor.Companion.inUserDoNotIntercept
import mozilla.components.feature.app.links.AppLinksInterceptor.Companion.isSubframeAllowed
import mozilla.components.feature.app.links.AppLinksInterceptor.Companion.lastApplinksPackageWithTimestamp
import mozilla.components.feature.app.links.AppLinksInterceptor.Companion.userDoNotInterceptCache
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.support.test.any
import mozilla.components.support.test.eq
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyBoolean
import org.mockito.ArgumentMatchers.anyString
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class AppLinksInterceptorTest {
    private lateinit var mockContext: Context
    private lateinit var store: BrowserStore
    private lateinit var mockUseCases: AppLinksUseCases
    private lateinit var mockGetRedirect: AppLinksUseCases.GetAppLinkRedirect
    private lateinit var mockEngineSession: EngineSession
    private lateinit var mockOpenRedirect: AppLinksUseCases.OpenAppLinkRedirect
    private lateinit var mockFragmentManager: FragmentManager
    private lateinit var mockDialog: RedirectDialogFragment
    private lateinit var mockLoadUrlUseCase: SessionUseCases.DefaultLoadUrlUseCase

    private lateinit var appLinksInterceptor: AppLinksInterceptor

    private val webUrl = "https://example.com"
    private val webUrlWithAppLink = "https://soundcloud.com"
    private val intentUrl = "zxing://scan;S.browser_fallback_url=example.com"
    private val fallbackUrl = "https://getpocket.com"
    private val marketplaceUrl = "market://details?id=example.com"
    private val aboutUrl = "about://scan"

    @Before
    fun setup() {
        mockContext = mock()
        store = BrowserStore()
        mockUseCases = mock()
        mockEngineSession = mock()
        mockGetRedirect = mock()
        mockOpenRedirect = mock()
        mockDialog = mock()
        mockLoadUrlUseCase = mock()
        mockFragmentManager = mock()
        whenever(mockUseCases.interceptedAppLinkRedirect).thenReturn(mockGetRedirect)
        whenever(mockUseCases.openAppLink).thenReturn(mockOpenRedirect)
        whenever(mockFragmentManager.beginTransaction()).thenReturn(mock())
        userDoNotInterceptCache.clear()
        lastApplinksPackageWithTimestamp = Pair(null, -APP_LINKS_DO_NOT_INTERCEPT_INTERVAL)

        val webRedirect = AppLinkRedirect(null, "", webUrl, null)
        val appRedirect = AppLinkRedirect(Intent.parseUri(intentUrl, 0), "", null, null)
        val appRedirectFromWebUrl = AppLinkRedirect(Intent.parseUri(webUrlWithAppLink, 0), "", null, null)
        val fallbackRedirect = AppLinkRedirect(null, "", fallbackUrl, null)
        val marketRedirect = AppLinkRedirect(null, "", null, Intent.parseUri(marketplaceUrl, 0))

        whenever(mockGetRedirect.invoke(webUrl)).thenReturn(webRedirect)
        whenever(mockGetRedirect.invoke(intentUrl)).thenReturn(appRedirect)
        whenever(mockGetRedirect.invoke(webUrlWithAppLink)).thenReturn(appRedirectFromWebUrl)
        whenever(mockGetRedirect.invoke(fallbackUrl)).thenReturn(fallbackRedirect)
        whenever(mockGetRedirect.invoke(marketplaceUrl)).thenReturn(marketRedirect)

        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { true },
            useCases = mockUseCases,
        )
    }

    @Test
    fun `request is intercepted by user clicking on a link`() {
        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, true, false, false, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.AppIntent)
    }

    @Test
    fun `request is intercepted by redirect`() {
        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, false, false, true, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.AppIntent)
    }

    @Test
    fun `request is not intercepted by a subframe redirect`() {
        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrl, null, false, false, true, false, true)
        assertEquals(null, response)
    }

    @Test
    fun `request is intercepted by direct navigation`() {
        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, false, false, false, true, false)
        assert(response is RequestInterceptor.InterceptionResponse.AppIntent)
    }

    @Test
    fun `request is not intercepted when interceptLinkClicks is false`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = false,
            launchInApp = { true },
            useCases = mockUseCases,
        )

        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, true, false, false, false, false)
        assertEquals(null, response)
    }

    @Test
    fun `request is not intercepted when launchInApp preference is false`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { false },
            useCases = mockUseCases,
        )

        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, true, false, false, false, false)
        assertEquals(null, response)
    }

    @Test
    fun `request is not intercepted when launchInApp preference is updated to false`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { false },
            useCases = mockUseCases,
        )

        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, true, false, false, false, false)
        assertEquals(null, response)

        appLinksInterceptor.updateLaunchInApp { true }
        verify(mockUseCases).updateLaunchInApp(any())
        val response2 = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, true, false, false, false, false)
        assert(response2 is RequestInterceptor.InterceptionResponse.AppIntent)
    }

    @Test
    fun `request is not intercepted when not user clicking on a link`() {
        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, false, false, false, false, false)
        assertEquals(null, response)
    }

    @Test
    fun `request is not intercepted if the current session is already on the same host`() {
        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, webUrlWithAppLink, true, true, false, false, false)
        assertEquals(null, response)
    }

    @Test
    fun `request is not intercepted by a redirect on same domain`() {
        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, webUrlWithAppLink, true, true, true, false, false)
        assertEquals(null, response)
    }

    @Test
    fun `domain is stripped before checking`() {
        var response = appLinksInterceptor.onLoadRequest(mockEngineSession, "http://example.com", "example.com", true, true, true, false, false)
        assertEquals(null, response)

        response = appLinksInterceptor.onLoadRequest(mockEngineSession, "https://example.com", "http://example.com", true, true, true, false, false)
        assertEquals(null, response)

        response = appLinksInterceptor.onLoadRequest(mockEngineSession, "https://www.example.com", "http://example.com", true, true, true, false, false)
        assertEquals(null, response)

        response = appLinksInterceptor.onLoadRequest(mockEngineSession, "http://www.example.com", "https://www.example.com", true, true, true, false, false)
        assertEquals(null, response)

        response = appLinksInterceptor.onLoadRequest(mockEngineSession, "http://m.example.com", "https://www.example.com", true, true, true, false, false)
        assertEquals(null, response)

        response = appLinksInterceptor.onLoadRequest(mockEngineSession, "http://mobile.example.com", "http://m.example.com", true, true, true, false, false)
        assertEquals(null, response)
    }

    @Test
    fun `request is not intercepted if a subframe request and not triggered by user`() {
        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, false, false, false, true, true)
        assertEquals(null, response)
    }

    @Test
    fun `request is not intercepted if not user gesture, not redirect and not direct navigation`() {
        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, false, false, false, false, false)
        assertEquals(null, response)
    }

    @Test
    fun `block listed schemes request not intercepted when triggered by user clicking on a link`() {
        val engineSession: EngineSession = mock()
        val blocklistedScheme = "blocklisted"
        val feature = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            alwaysDeniedSchemes = setOf(blocklistedScheme),
            launchInApp = { true },
            useCases = mockUseCases,
        )

        val blocklistedUrl = "$blocklistedScheme://example.com"
        val blocklistedRedirect = AppLinkRedirect(Intent.parseUri(blocklistedUrl, 0), "", blocklistedUrl, null)
        whenever(mockGetRedirect.invoke(blocklistedUrl)).thenReturn(blocklistedRedirect)
        var response = feature.onLoadRequest(engineSession, blocklistedUrl, null, true, false, false, false, false)
        assertEquals(null, response)
    }

    @Test
    fun `supported schemes request not launched if launchInApp is false`() {
        val engineSession: EngineSession = mock()
        val supportedScheme = "supported"
        val feature = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            engineSupportedSchemes = setOf(supportedScheme),
            launchInApp = { false },
            useCases = mockUseCases,
        )

        val supportedUrl = "$supportedScheme://example.com"
        val supportedRedirect = AppLinkRedirect(Intent.parseUri(supportedUrl, 0), "", null, null)
        whenever(mockGetRedirect.invoke(supportedUrl)).thenReturn(supportedRedirect)
        val response = feature.onLoadRequest(engineSession, supportedUrl, null, true, false, false, false, false)
        assertEquals(null, response)
    }

    @Test
    fun `supported schemes request not launched if interceptLinkClicks is false`() {
        val engineSession: EngineSession = mock()
        val supportedScheme = "supported"
        val feature = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = false,
            engineSupportedSchemes = setOf(supportedScheme),
            launchInApp = { true },
            useCases = mockUseCases,
        )

        val supportedUrl = "$supportedScheme://example.com"
        val supportedRedirect = AppLinkRedirect(Intent.parseUri(supportedUrl, 0), "", null, null)
        whenever(mockGetRedirect.invoke(supportedUrl)).thenReturn(supportedRedirect)
        val response = feature.onLoadRequest(engineSession, supportedUrl, null, true, false, false, false, false)
        assertEquals(null, response)
    }

    @Test
    fun `supported schemes request not launched if not triggered by user`() {
        val engineSession: EngineSession = mock()
        val supportedScheme = "supported"
        val feature = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            engineSupportedSchemes = setOf(supportedScheme),
            launchInApp = { true },
            useCases = mockUseCases,
        )

        val supportedUrl = "$supportedScheme://example.com"
        val supportedRedirect = AppLinkRedirect(Intent.parseUri(supportedUrl, 0), "", null, null)
        whenever(mockGetRedirect.invoke(supportedUrl)).thenReturn(supportedRedirect)
        val response = feature.onLoadRequest(engineSession, supportedUrl, null, false, false, false, false, false)
        assertEquals(null, response)
    }

    @Test
    fun `not supported schemes request always intercepted regardless of hasUserGesture, interceptLinkClicks or launchInApp`() {
        val engineSession: EngineSession = mock()
        val supportedScheme = "supported"
        val notSupportedScheme = "not_supported"
        val blocklistedScheme = "blocklisted"
        val feature = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = false,
            engineSupportedSchemes = setOf(supportedScheme),
            alwaysDeniedSchemes = setOf(blocklistedScheme),
            launchInApp = { false },
            useCases = mockUseCases,
        )

        val notSupportedUrl = "$notSupportedScheme://example.com"
        val notSupportedRedirect = AppLinkRedirect(Intent.parseUri(notSupportedUrl, 0), "", null, null)
        whenever(mockGetRedirect.invoke(notSupportedUrl)).thenReturn(notSupportedRedirect)
        val response = feature.onLoadRequest(engineSession, notSupportedUrl, null, false, false, false, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.AppIntent)
    }

    @Test
    fun `blocklisted schemes request always ignored even if the engine does not support it`() {
        val engineSession: EngineSession = mock()
        val supportedScheme = "supported"
        val notSupportedScheme = "not_supported"
        val feature = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = false,
            engineSupportedSchemes = setOf(supportedScheme),
            alwaysDeniedSchemes = setOf(notSupportedScheme),
            launchInApp = { false },
            useCases = mockUseCases,
        )

        val notSupportedUrl = "$notSupportedScheme://example.com"
        val notSupportedRedirect = AppLinkRedirect(Intent.parseUri(notSupportedUrl, 0), "", null, null)
        whenever(mockGetRedirect.invoke(notSupportedUrl)).thenReturn(notSupportedRedirect)
        val response = feature.onLoadRequest(engineSession, notSupportedUrl, null, false, false, false, false, false)
        assertEquals(null, response)
    }

    @Test
    fun `not supported schemes request should not use fallback if user preference is launch in app`() {
        val engineSession: EngineSession = mock()
        val supportedScheme = "supported"
        val notSupportedScheme = "not_supported"
        val blocklistedScheme = "blocklisted"
        val feature = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = false,
            engineSupportedSchemes = setOf(supportedScheme),
            alwaysDeniedSchemes = setOf(blocklistedScheme),
            launchInApp = { true },
            useCases = mockUseCases,
        )

        val notSupportedUrl = "$notSupportedScheme://example.com"
        val notSupportedRedirect = AppLinkRedirect(Intent.parseUri(notSupportedUrl, 0), "", fallbackUrl, null)
        whenever(mockGetRedirect.invoke(notSupportedUrl)).thenReturn(notSupportedRedirect)
        val response = feature.onLoadRequest(engineSession, notSupportedUrl, null, false, false, false, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.AppIntent)
    }

    @Test
    fun `not supported schemes request uses fallback URL if available and launchInApp is set to false`() {
        val engineSession: EngineSession = mock()
        val supportedScheme = "supported"
        val notSupportedScheme = "not_supported"
        val blocklistedScheme = "blocklisted"
        val feature = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            engineSupportedSchemes = setOf(supportedScheme),
            alwaysDeniedSchemes = setOf(blocklistedScheme),
            launchInApp = { false },
            useCases = mockUseCases,
        )

        val notSupportedUrl = "$notSupportedScheme://example.com"
        val fallbackUrl = "https://example.com"
        val notSupportedRedirect = AppLinkRedirect(Intent.parseUri(notSupportedUrl, 0), "", fallbackUrl, null)
        whenever(mockGetRedirect.invoke(notSupportedUrl)).thenReturn(notSupportedRedirect)
        val response = feature.onLoadRequest(engineSession, notSupportedUrl, null, true, false, false, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.Url)
    }

    @Test
    fun `not supported schemes request uses fallback URL not market intent if launchInApp is set to false`() {
        val engineSession: EngineSession = mock()
        val supportedScheme = "supported"
        val notSupportedScheme = "not_supported"
        val blocklistedScheme = "blocklisted"
        val feature = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            engineSupportedSchemes = setOf(supportedScheme),
            alwaysDeniedSchemes = setOf(blocklistedScheme),
            launchInApp = { false },
            useCases = mockUseCases,
        )

        val notSupportedUrl = "$notSupportedScheme://example.com"
        val fallbackUrl = "https://example.com"
        val notSupportedRedirect = AppLinkRedirect(null, "", fallbackUrl, Intent.parseUri(marketplaceUrl, 0))
        whenever(mockGetRedirect.invoke(notSupportedUrl)).thenReturn(notSupportedRedirect)
        val response = feature.onLoadRequest(engineSession, notSupportedUrl, null, true, false, false, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.Url)
    }

    @Test
    fun `intent scheme launch intent if fallback URL is unavailable and launchInApp is set to false`() {
        val engineSession: EngineSession = mock()
        val feature = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = false,
            launchInApp = { false },
            useCases = mockUseCases,
        )

        val intentUrl = "intent://example.com"
        val intentRedirect = AppLinkRedirect(Intent.parseUri(intentUrl, 0), "", null, null)
        whenever(mockGetRedirect.invoke(intentUrl)).thenReturn(intentRedirect)
        val response = feature.onLoadRequest(engineSession, intentUrl, null, true, false, false, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.AppIntent)
    }

    @Test
    fun `intent scheme uses fallback URL if available and launchInApp is set to false`() {
        val engineSession: EngineSession = mock()
        val feature = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = false,
            launchInApp = { false },
            useCases = mockUseCases,
        )

        val intentUrl = "intent://example.com"
        val fallbackUrl = "https://example.com"
        val intentRedirect = AppLinkRedirect(Intent.parseUri(intentUrl, 0), "", fallbackUrl, null)
        whenever(mockGetRedirect.invoke(intentUrl)).thenReturn(intentRedirect)
        val response = feature.onLoadRequest(engineSession, intentUrl, null, true, false, false, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.Url)
    }

    @Test
    fun `request is not intercepted for URLs with javascript scheme`() {
        val javascriptUri = "javascript:;"

        val appRedirect = AppLinkRedirect(Intent.parseUri(javascriptUri, 0), "", null, null)
        whenever(mockGetRedirect.invoke(javascriptUri)).thenReturn(appRedirect)

        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, javascriptUri, null, true, true, false, false, false)
        assertEquals(null, response)
    }

    @Test
    fun `Use the fallback URL when no non-browser app is installed`() {
        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, fallbackUrl, null, true, false, false, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.Url)
    }

    @Test
    fun `use the market intent if target app is not installed`() {
        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, marketplaceUrl, null, true, false, false, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.AppIntent)
    }

    @Test
    fun `external app is launched when launch in app is set to true and it is user triggered`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { true },
            useCases = mockUseCases,
            launchFromInterceptor = true,
            shouldPrompt = { false },
        )

        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, true, false, false, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.AppIntent)
        verify(mockOpenRedirect).invoke(any(), anyBoolean(), any())
    }

    @Test
    fun `external app is launched when launchInApp settings is set to AlwaysAsk and it is user triggered`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { true },
            useCases = mockUseCases,
            launchFromInterceptor = true,
            shouldPrompt = { true },
        )

        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, true, false, false, false, false)
        assertNull(response)
        verify(mockOpenRedirect).invoke(any(), anyBoolean(), any())
    }

    @Test
    fun `WHEN launch from intercept is false AND launch in app is set to true and it is user triggered THEN app intent is returned`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { true },
            useCases = mockUseCases,
            launchFromInterceptor = false,
            shouldPrompt = { false },
        )

        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, true, false, false, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.AppIntent)
        verify(mockOpenRedirect, times(0)).invoke(any(), anyBoolean(), any())
    }

    @Test
    fun `try to use fallback url if user preference is not to launch in third party app`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { false },
            useCases = mockUseCases,
            launchFromInterceptor = true,
        )

        val testRedirect = AppLinkRedirect(Intent.parseUri(intentUrl, 0), "", fallbackUrl, null)
        val response = appLinksInterceptor.handleRedirect(testRedirect, intentUrl, "")
        assert(response is RequestInterceptor.InterceptionResponse.Url)
    }

    @Test
    fun `external app is launched when url scheme is not supported by the engine`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { false },
            useCases = mockUseCases,
            launchFromInterceptor = true,
        )

        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, intentUrl, null, false, true, false, false, false)
        assertNull(response)
        verify(mockOpenRedirect).invoke(any(), anyBoolean(), any())
    }

    @Test
    fun `WHEN url scheme is not supported by the engine AND launch from interceptor is false THEN app intent is returned`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { false },
            useCases = mockUseCases,
            launchFromInterceptor = false,
        )

        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, intentUrl, null, false, true, false, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.AppIntent)
        verify(mockOpenRedirect, times(0)).invoke(any(), anyBoolean(), any())
    }

    @Test
    fun `do not use fallback url if trigger by user gesture and preference is to launch in app`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { true },
            useCases = mockUseCases,
            launchFromInterceptor = true,
        )

        val testRedirect = AppLinkRedirect(Intent.parseUri(intentUrl, 0), "", fallbackUrl, null)
        val response = appLinksInterceptor.handleRedirect(testRedirect, intentUrl, "")
        assert(response is RequestInterceptor.InterceptionResponse.AppIntent)
    }

    @Test
    fun `launch marketplace intent if available and no external app`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { true },
            useCases = mockUseCases,
            launchFromInterceptor = true,
        )

        val testRedirect = AppLinkRedirect(null, "", fallbackUrl, Intent.parseUri(marketplaceUrl, 0))
        val response = appLinksInterceptor.handleRedirect(testRedirect, webUrl, "")
        assert(response is RequestInterceptor.InterceptionResponse.AppIntent)
    }

    @Test
    fun `use fallback url if available and no external app`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { true },
            useCases = mockUseCases,
            launchFromInterceptor = true,
        )

        val testRedirect = AppLinkRedirect(null, "", fallbackUrl, null)
        val response = appLinksInterceptor.handleRedirect(testRedirect, webUrl, "")
        assert(response is RequestInterceptor.InterceptionResponse.Url)
    }

    @Test
    fun `WHEN url have same domain THEN is same domain returns true ELSE false`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { true },
            useCases = mockUseCases,
            launchFromInterceptor = true,
        )

        assert(appLinksInterceptor.isSameDomain("maps.google.com", "www.google.com"))
        assert(appLinksInterceptor.isSameDomain("mobile.mozilla.com", "www.mozilla.com"))
        assert(appLinksInterceptor.isSameDomain("m.mozilla.com", "maps.mozilla.com"))

        assertFalse(appLinksInterceptor.isSameDomain("www.google.ca", "www.google.com"))
        assertFalse(appLinksInterceptor.isSameDomain("maps.google.ca", "m.google.com"))
        assertFalse(appLinksInterceptor.isSameDomain("accounts.google.com", "www.google.com"))
    }

    @Test
    fun `WHEN request is in user do not intercept cache THEN request is not intercepted`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { true },
            useCases = mockUseCases,
        )

        addUserDoNotIntercept("https://soundcloud.com", null, "")

        val response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, true, false, false, false, false)
        assertNull(response)
    }

    @Test
    fun `WHEN request is in user do not intercept cache but there is a fallback THEN fallback is used`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { false },
            useCases = mockUseCases,
            launchFromInterceptor = true,
        )

        addUserDoNotIntercept(intentUrl, null, "")
        val testRedirect = AppLinkRedirect(Intent.parseUri(intentUrl, 0), "", fallbackUrl, null)
        val response = appLinksInterceptor.handleRedirect(testRedirect, intentUrl, "")
        assert(response is RequestInterceptor.InterceptionResponse.Url)
    }

    @Test
    fun `WHEN request is in user do not intercept cache and scheme is not supported by the engine THEN request is not intercepted`() {
        val engineSession: EngineSession = mock()
        val supportedScheme = "supported"
        val notSupportedScheme = "not_supported"
        val blocklistedScheme = "blocklisted"
        val feature = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = false,
            engineSupportedSchemes = setOf(supportedScheme),
            alwaysDeniedSchemes = setOf(blocklistedScheme),
            launchInApp = { true },
            useCases = mockUseCases,
        )

        val notSupportedUrl = "$notSupportedScheme://example.com"
        addUserDoNotIntercept(notSupportedUrl, null, "")
        val notSupportedRedirect = AppLinkRedirect(Intent.parseUri(notSupportedUrl, 0), "", null, null)
        whenever(mockGetRedirect.invoke(notSupportedUrl)).thenReturn(notSupportedRedirect)
        val response = feature.onLoadRequest(engineSession, notSupportedUrl, null, false, false, false, false, false)
        assertNull(response)
    }

    @Test
    fun `WHEN don't launch in app AND request is in user do not intercept cache AND scheme is not supported by the engine THEN request is not intercepted`() {
        val engineSession: EngineSession = mock()
        val supportedScheme = "supported"
        val notSupportedScheme = "not_supported"
        val blocklistedScheme = "blocklisted"
        val feature = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = false,
            engineSupportedSchemes = setOf(supportedScheme),
            alwaysDeniedSchemes = setOf(blocklistedScheme),
            launchInApp = { false },
            useCases = mockUseCases,
        )

        val notSupportedUrl = "$notSupportedScheme://example.com"
        addUserDoNotIntercept(notSupportedUrl, null, "")
        val notSupportedRedirect = AppLinkRedirect(Intent.parseUri(notSupportedUrl, 0), "", null, null)
        whenever(mockGetRedirect.invoke(notSupportedUrl)).thenReturn(notSupportedRedirect)
        val response = feature.onLoadRequest(engineSession, notSupportedUrl, null, false, false, false, false, false)
        assertNull(response)
    }

    @Test
    fun `WHEN added to user do not open cache THEN return true if user do no intercept cache exists`() {
        addUserDoNotIntercept("test://test.com", null, "")
        assertTrue(inUserDoNotIntercept("test://test.com", null, ""))
        assertFalse(inUserDoNotIntercept("https://test.com", null, ""))

        addUserDoNotIntercept("http://test.com", null, "")
        assertTrue(inUserDoNotIntercept("https://test.com", null, ""))
        assertFalse(inUserDoNotIntercept("https://example.com", null, ""))

        val testIntent: Intent = mock()
        val componentName: ComponentName = mock()
        doReturn(componentName).`when`(testIntent).component
        doReturn("app.example.com").`when`(componentName).packageName

        addUserDoNotIntercept("https://example.com", testIntent, "")
        assertTrue(inUserDoNotIntercept("https://example.com", testIntent, ""))
        assertTrue(inUserDoNotIntercept("https://test.com", testIntent, ""))

        doReturn("app.test.com").`when`(componentName).packageName
        assertFalse(inUserDoNotIntercept("https://test.com", testIntent, ""))
        assertFalse(inUserDoNotIntercept("https://mozilla.org", null, ""))
    }

    @Test
    fun `WHEN user do not open cache expires THEN return false`() {
        val testIntent: Intent = mock()
        val componentName: ComponentName = mock()
        doReturn(componentName).`when`(testIntent).component
        doReturn("app.example.com").`when`(componentName).packageName

        addUserDoNotIntercept("https://example.com", testIntent, "")
        assertTrue(inUserDoNotIntercept("https://example.com", testIntent, ""))
        assertTrue(inUserDoNotIntercept("https://test.com", testIntent, ""))

        userDoNotInterceptCache["app.example.com".hashCode()] = -APP_LINKS_DO_NOT_OPEN_CACHE_INTERVAL
        assertFalse(inUserDoNotIntercept("https://example.com", testIntent, ""))
        assertFalse(inUserDoNotIntercept("https://test.com", testIntent, ""))
    }

    @Test
    fun `WHEN request is redirecting to external app quickly THEN request is not intercepted`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { true },
            useCases = mockUseCases,
        )

        var response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, true, false, false, false, false)
        assertTrue(response is RequestInterceptor.InterceptionResponse.AppIntent)

        response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, true, false, false, false, false)
        assertNull(response)
    }

    @Test
    fun `WHEN request is redirecting to different app quickly THEN request is intercepted`() {
        appLinksInterceptor = AppLinksInterceptor(
            context = mockContext,
            interceptLinkClicks = true,
            launchInApp = { true },
            useCases = mockUseCases,
        )

        var response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrl, null, true, false, false, false, false)
        assert(response is RequestInterceptor.InterceptionResponse.Url)

        response = appLinksInterceptor.onLoadRequest(mockEngineSession, webUrlWithAppLink, null, true, false, false, false, false)
        assertTrue(response is RequestInterceptor.InterceptionResponse.AppIntent)
    }

    @Test
    fun `WHEN should prompt AND in non-private mode THEN an external app dialog is shown`() {
        appLinksInterceptor = spy(
            AppLinksInterceptor(
                context = mockContext,
                store = store,
                interceptLinkClicks = true,
                launchInApp = { true },
                useCases = mockUseCases,
                shouldPrompt = { true },
            ),
        )

        appLinksInterceptor.updateFragmentManger(mockFragmentManager)

        val tab = createTab(webUrl)
        val mockAppIntent: Intent = mock()
        val mockComponentName: ComponentName = mock()
        whenever(mockAppIntent.component).thenReturn(mockComponentName)
        whenever(mockComponentName.packageName).thenReturn("com.zxing.app")
        doReturn(mockDialog).`when`(appLinksInterceptor).getOrCreateDialog(anyBoolean(), any(), any())

        appLinksInterceptor.handleIntent(tab, intentUrl, mockAppIntent, null, mock(), "")

        verify(mockDialog).showNow(eq(mockFragmentManager), anyString())
        verify(mockOpenRedirect, never()).invoke(any(), anyBoolean(), any())
    }

    @Test
    fun `WHEN should prompt AND only have marketing intent THEN an external app dialog is shown`() {
        appLinksInterceptor = spy(
            AppLinksInterceptor(
                context = mockContext,
                store = store,
                interceptLinkClicks = true,
                launchInApp = { true },
                useCases = mockUseCases,
                shouldPrompt = { true },
            ),
        )

        appLinksInterceptor.updateFragmentManger(mockFragmentManager)

        val tab = createTab(webUrl)
        val mockMarketIntent: Intent = mock()
        val mockComponentName: ComponentName = mock()
        whenever(mockComponentName.packageName).thenReturn("com.zxing.app")
        doReturn(mockDialog).`when`(appLinksInterceptor).getOrCreateDialog(anyBoolean(), any(), any())

        appLinksInterceptor.handleIntent(tab, intentUrl, null, null, mockMarketIntent, "")

        verify(mockDialog).showNow(eq(mockFragmentManager), anyString())
        verify(mockOpenRedirect, never()).invoke(any(), anyBoolean(), any())
    }

    @Test
    fun `WHEN should not prompt AND in non-private mode THEN an external app dialog is not shown`() {
        appLinksInterceptor = spy(
            AppLinksInterceptor(
                context = mockContext,
                store = store,
                interceptLinkClicks = true,
                launchInApp = { true },
                useCases = mockUseCases,
                shouldPrompt = { false },
            ),
        )

        appLinksInterceptor.updateFragmentManger(mockFragmentManager)

        val tab = createTab(webUrl)
        appLinksInterceptor.handleIntent(tab, intentUrl, mock(), null, mock(), "")

        verify(mockDialog, never()).showNow(eq(mockFragmentManager), anyString())
    }

    @Test
    fun `WHEN custom tab and caller is the same as external app THEN an external app dialog is not shown`() {
        appLinksInterceptor = spy(
            AppLinksInterceptor(
                context = mockContext,
                store = store,
                interceptLinkClicks = true,
                launchInApp = { true },
                useCases = mockUseCases,
                shouldPrompt = { true },
            ),
        )

        appLinksInterceptor.updateFragmentManger(mockFragmentManager)

        val tab = createCustomTab(
            id = "c",
            url = webUrl,
            source = SessionState.Source.External.CustomTab(
                ExternalPackage("com.zxing.app", PackageCategory.PRODUCTIVITY),
            ),
        )

        val appIntent: Intent = mock()
        val componentName: ComponentName = mock()
        whenever(appIntent.component).thenReturn(componentName)
        whenever(componentName.packageName).thenReturn("com.zxing.app")

        appLinksInterceptor.handleIntent(tab, intentUrl, appIntent, null, null, "")

        verify(mockDialog, never()).showNow(eq(mockFragmentManager), anyString())
    }

    @Test
    fun `WHEN tab have action view and caller is the same as external app THEN an external app dialog is not shown`() {
        appLinksInterceptor = spy(
            AppLinksInterceptor(
                context = mockContext,
                store = store,
                interceptLinkClicks = true,
                launchInApp = { true },
                useCases = mockUseCases,
                shouldPrompt = { true },
            ),
        )

        appLinksInterceptor.updateFragmentManger(mockFragmentManager)

        val tab = createCustomTab(
            id = "d",
            url = webUrl,
            source = SessionState.Source.External.ActionView(
                ExternalPackage("com.zxing.app", PackageCategory.PRODUCTIVITY),
            ),
        )

        val appIntent: Intent = mock()
        val componentName: ComponentName = mock()
        whenever(appIntent.component).thenReturn(componentName)
        whenever(componentName.packageName).thenReturn("com.zxing.app")

        appLinksInterceptor.handleIntent(tab, intentUrl, appIntent, null, null, "")

        verify(mockDialog, never()).showNow(eq(mockFragmentManager), anyString())
    }

    @Test
    fun `WHEN tab have action send and caller is the same as external app THEN an external app dialog is shown`() {
        appLinksInterceptor = spy(
            AppLinksInterceptor(
                context = mockContext,
                store = store,
                interceptLinkClicks = true,
                launchInApp = { true },
                useCases = mockUseCases,
                shouldPrompt = { true },
            ),
        )

        appLinksInterceptor.updateFragmentManger(mockFragmentManager)

        val tab = createCustomTab(
            id = "d",
            url = webUrl,
            source = SessionState.Source.External.ActionSend(
                ExternalPackage("com.zxing.app", PackageCategory.PRODUCTIVITY),
            ),
        )

        val appIntent: Intent = mock()
        val componentName: ComponentName = mock()
        whenever(appIntent.component).thenReturn(componentName)
        whenever(componentName.packageName).thenReturn("com.zxing.app")
        doReturn(mockDialog).`when`(appLinksInterceptor).getOrCreateDialog(anyBoolean(), any(), any())

        appLinksInterceptor.handleIntent(tab, intentUrl, appIntent, null, null, "")

        verify(mockDialog).showNow(eq(mockFragmentManager), anyString())
        verify(mockOpenRedirect, never()).invoke(any(), anyBoolean(), any())
    }

    @Test
    fun `WHEN tab have action search and caller is the same as external app THEN an external app dialog is shown`() {
        appLinksInterceptor = spy(
            AppLinksInterceptor(
                context = mockContext,
                store = store,
                interceptLinkClicks = true,
                launchInApp = { true },
                useCases = mockUseCases,
                shouldPrompt = { true },
            ),
        )

        appLinksInterceptor.updateFragmentManger(mockFragmentManager)

        val tab = createCustomTab(
            id = "d",
            url = webUrl,
            source = SessionState.Source.External.ActionSearch(
                ExternalPackage("com.zxing.app", PackageCategory.PRODUCTIVITY),
            ),
        )

        val appIntent: Intent = mock()
        val componentName: ComponentName = mock()
        whenever(appIntent.component).thenReturn(componentName)
        whenever(componentName.packageName).thenReturn("com.zxing.app")
        doReturn(mockDialog).`when`(appLinksInterceptor).getOrCreateDialog(anyBoolean(), any(), any())

        appLinksInterceptor.handleIntent(tab, intentUrl, appIntent, null, null, "")

        verify(mockDialog).showNow(eq(mockFragmentManager), anyString())
        verify(mockOpenRedirect, never()).invoke(any(), anyBoolean(), any())
    }

    @Test
    fun `WHEN should prompt and in private mode THEN an external app dialog is shown`() {
        appLinksInterceptor = spy(
            AppLinksInterceptor(
                context = mockContext,
                store = store,
                interceptLinkClicks = true,
                launchInApp = { true },
                useCases = mockUseCases,
                shouldPrompt = { true },
            ),
        )

        appLinksInterceptor.updateFragmentManger(mockFragmentManager)

        val tabSessionState = TabSessionState(
            id = "tab1",
            content = ContentState(
                url = "https://mozilla.org",
                private = true,
                isProductUrl = false,
            ),
        )
        val mockAppIntent: Intent = mock()
        val mockComponentName: ComponentName = mock()
        whenever(mockAppIntent.component).thenReturn(mockComponentName)
        whenever(mockComponentName.packageName).thenReturn("com.zxing.app")
        doReturn(mockDialog).`when`(appLinksInterceptor).getOrCreateDialog(anyBoolean(), any(), any())

        appLinksInterceptor.handleIntent(tabSessionState, intentUrl, mockAppIntent, null, mock(), "")

        verify(mockDialog).showNow(eq(mockFragmentManager), anyString())
        verify(mockOpenRedirect, never()).invoke(any(), anyBoolean(), any())
    }

    @Test
    fun `WHEN should not prompt and in private mode THEN an external app dialog is shown`() {
        appLinksInterceptor = spy(
            AppLinksInterceptor(
                context = mockContext,
                store = store,
                interceptLinkClicks = true,
                launchInApp = { true },
                useCases = mockUseCases,
                shouldPrompt = { false },
            ),
        )

        appLinksInterceptor.updateFragmentManger(mockFragmentManager)

        val tabSessionState = TabSessionState(
            id = "tab1",
            content = ContentState(
                url = "https://mozilla.org",
                private = true,
                isProductUrl = false,
            ),
        )
        val mockAppIntent: Intent = mock()
        val mockComponentName: ComponentName = mock()
        whenever(mockAppIntent.component).thenReturn(mockComponentName)
        whenever(mockComponentName.packageName).thenReturn("com.zxing.app")
        doReturn(mockDialog).`when`(appLinksInterceptor).getOrCreateDialog(anyBoolean(), any(), any())

        appLinksInterceptor.handleIntent(tabSessionState, intentUrl, mockAppIntent, null, mock(), "")

        verify(mockDialog).showNow(eq(mockFragmentManager), anyString())
        verify(mockOpenRedirect, never()).invoke(any(), anyBoolean(), any())
    }

    @Test
    fun `redirect dialog is only added once`() {
        appLinksInterceptor = spy(
            AppLinksInterceptor(
                context = mockContext,
                store = store,
                interceptLinkClicks = true,
                launchInApp = { true },
                useCases = mockUseCases,
                shouldPrompt = { true },
            ),
        )

        appLinksInterceptor.updateFragmentManger(mockFragmentManager)
        val tabSessionState = TabSessionState(
            id = "tab1",
            content = ContentState(
                url = "https://mozilla.org",
                private = false,
                isProductUrl = false,
            ),
        )

        val appIntent: Intent = mock()
        val componentName: ComponentName = mock()
        whenever(appIntent.component).thenReturn(componentName)
        whenever(componentName.packageName).thenReturn("com.zxing.app")
        doReturn(mockDialog).`when`(appLinksInterceptor).getOrCreateDialog(anyBoolean(), any(), any())

        appLinksInterceptor.handleIntent(tabSessionState, intentUrl, appIntent, null, null, "")

        verify(mockDialog).showNow(eq(mockFragmentManager), anyString())

        doReturn(mockDialog).`when`(appLinksInterceptor).getOrCreateDialog(false, "", "")
        doReturn(mockDialog).`when`(mockFragmentManager).findFragmentByTag(RedirectDialogFragment.FRAGMENT_TAG)
        appLinksInterceptor.handleIntent(tabSessionState, intentUrl, mock(), null, mock(), "")
        verify(mockDialog, times(1)).showNow(mockFragmentManager, RedirectDialogFragment.FRAGMENT_TAG)
    }

    @Test
    fun `WHEN caller and intent have the same package name THEN return true`() {
        appLinksInterceptor = spy(
            AppLinksInterceptor(
                context = mockContext,
                store = store,
                interceptLinkClicks = true,
                launchInApp = { true },
                useCases = mockUseCases,
                shouldPrompt = { true },
            ),
        )

        appLinksInterceptor.updateFragmentManger(mockFragmentManager)

        val tabSessionState = TabSessionState(
            id = "tab1",
            content = ContentState(
                url = "https://mozilla.org",
                private = false,
                isProductUrl = false,
            ),
            source = SessionState.Source.External.CustomTab(ExternalPackage("com.zxing.app", PackageCategory.PRODUCTIVITY)),
        )

        val appIntent: Intent = mock()
        val componentName: ComponentName = mock()
        whenever(appIntent.component).thenReturn(componentName)
        whenever(componentName.packageName).thenReturn("com.zxing.app")
        assertTrue(appLinksInterceptor.isAuthentication(tabSessionState, appIntent))

        val tabSessionState2 = TabSessionState(
            id = "tab1",
            content = ContentState(
                url = "https://mozilla.org",
                private = false,
                isProductUrl = false,
            ),
        )
        assertFalse(appLinksInterceptor.isAuthentication(tabSessionState2, appIntent))

        val tabSessionState3 = TabSessionState(
            id = "tab1",
            content = ContentState(
                url = "https://mozilla.org",
                private = false,
                isProductUrl = false,
            ),
            source = SessionState.Source.External.CustomTab(ExternalPackage("com.example.app", PackageCategory.PRODUCTIVITY)),
        )
        assertFalse(appLinksInterceptor.isAuthentication(tabSessionState3, appIntent))

        doReturn(null).`when`(componentName).packageName
        assertFalse(appLinksInterceptor.isAuthentication(tabSessionState, appIntent))
    }

    @Test
    fun `WHEN intent source is actionView or customTab THEN isAuthentication returns true`() {
        appLinksInterceptor = spy(
            AppLinksInterceptor(
                context = mockContext,
                store = store,
                interceptLinkClicks = true,
                launchInApp = { true },
                useCases = mockUseCases,
                shouldPrompt = { true },
            ),
        )

        val appIntent: Intent = mock()
        val componentName: ComponentName = mock()
        whenever(appIntent.component).thenReturn(componentName)
        whenever(componentName.packageName).thenReturn("com.zxing.app")
        appLinksInterceptor.updateFragmentManger(mockFragmentManager)

        val tabSessionState = TabSessionState(
            id = "tab1",
            content = ContentState(
                url = "https://mozilla.org",
                private = false,
                isProductUrl = false,
            ),
            source = SessionState.Source.External.CustomTab(ExternalPackage("com.zxing.app", PackageCategory.PRODUCTIVITY)),
        )

        assertTrue(appLinksInterceptor.isAuthentication(tabSessionState, appIntent))

        val tabSessionState2 = TabSessionState(
            id = "tab1",
            content = ContentState(
                url = "https://mozilla.org",
                private = false,
                isProductUrl = false,
            ),
            source = SessionState.Source.External.ActionView(ExternalPackage("com.zxing.app", PackageCategory.PRODUCTIVITY)),
        )
        assertTrue(appLinksInterceptor.isAuthentication(tabSessionState2, appIntent))

        val tabSessionState3 = TabSessionState(
            id = "tab1",
            content = ContentState(
                url = "https://mozilla.org",
                private = false,
                isProductUrl = false,
            ),
            source = SessionState.Source.External.ActionSend(ExternalPackage("com.zxing.app", PackageCategory.PRODUCTIVITY)),
        )
        assertFalse(appLinksInterceptor.isAuthentication(tabSessionState3, appIntent))

        val tabSessionState4 = TabSessionState(
            id = "tab1",
            content = ContentState(
                url = "https://mozilla.org",
                private = false,
                isProductUrl = false,
            ),
            source = SessionState.Source.External.ActionSearch(ExternalPackage("com.zxing.app", PackageCategory.PRODUCTIVITY)),
        )
        assertFalse(appLinksInterceptor.isAuthentication(tabSessionState4, appIntent))
    }

    @Test
    fun `WHEN scheme is allowed for subframe THEN subframe is allowed to trigger applinks redirect`() {
        assertTrue(isSubframeAllowed("msteams"))
        assertFalse(isSubframeAllowed("msteam"))
        assertFalse(isSubframeAllowed("abc"))
        assertFalse(isSubframeAllowed("http")) // we should never allow http for subframes
        assertFalse(isSubframeAllowed("https")) // we should never allow https for subframes
    }

    @Test
    fun `WHEN scheme is supported THEN loads URL`() {
        val tab = createTab(webUrl, private = true)

        appLinksInterceptor = spy(
            AppLinksInterceptor(
                context = mockContext,
                store = store,
                interceptLinkClicks = true,
                launchInApp = { true },
                useCases = mockUseCases,
                shouldPrompt = { true },
                loadUrlUseCase = mockLoadUrlUseCase,
            ),
        )

        appLinksInterceptor.loadUrlIfSchemeSupported(tab, intentUrl)
        verify(mockLoadUrlUseCase, never()).invoke(anyString(), anyString(), any(), any(), any())

        appLinksInterceptor.loadUrlIfSchemeSupported(tab, webUrl)
        verify(mockLoadUrlUseCase, times(1)).invoke(anyString(), anyString(), any(), any(), any())

        appLinksInterceptor.loadUrlIfSchemeSupported(tab, aboutUrl)
        verify(mockLoadUrlUseCase, times(2)).invoke(anyString(), anyString(), any(), any(), any())
    }
}
