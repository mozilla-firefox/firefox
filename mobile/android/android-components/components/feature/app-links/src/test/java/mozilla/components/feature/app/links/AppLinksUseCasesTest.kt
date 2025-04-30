/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.app.links

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.pm.PackageInfo
import android.content.pm.ResolveInfo
import android.net.Uri
import androidx.core.net.toUri
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import mozilla.components.support.utils.Browsers
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.any
import org.mockito.Mockito.never
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`
import org.robolectric.Shadows.shadowOf
import java.io.File

@RunWith(AndroidJUnit4::class)
class AppLinksUseCasesTest {

    private val appUrl = "https://example.com"
    private val appIntent = "intent://example.com"
    private val appSchemeIntent = "example://example.com"
    private val appPackage = "com.example.app"
    private val browserSchemeUrl = "browser://test"
    private val browserPackage = Browsers.KnownBrowser.ANDROID_STOCK_BROWSER.packageName
    private val testBrowserPackage = "com.current.browser"
    private val filePath = "file:///storage/abc/test.mp3"
    private val dataUrl = "data:text/plain;base64,SGVsbG8sIFdvcmxkIQ=="
    private val aboutUrl = "about:config"
    private val javascriptUrl = "javascript:'hello, world'"
    private val jarUrl = "jar:file://some/path/test.html"
    private val contentUrl = "content://media/external_primary/downloads/12345"
    private val fidoPath = "fido:12345678"
    private val fileType = "audio/mpeg"
    private val layerUrl = "https://example.com"
    private val layerPackage = "com.example.app"
    private val layerActivity = "com.example2.app.intentActivity"
    private val appIntentWithPackageAndFallback =
        "intent://com.example.app#Intent;package=com.example.com;S.browser_fallback_url=https://example.com;end"
    private val appIntentWithPackageAndPlayStoreFallback =
        "intent://com.example.app#Intent;package=com.example.com;S.browser_fallback_url=https://play.google.com/store/abc;end"
    private val urlWithAndroidFallbackLink =
        "https://example.com/?afl=https://example.com"
    private val urlWithFallbackLink =
        "https://example.com/?link=https://example.com"

    @Before
    fun setup() {
        AppLinksUseCases.redirectCache = null
    }

    private fun createContext(
        vararg urlToPackages: Triple<String, String, String>,
        default: Boolean = false,
        installedApps: List<String> = emptyList(),
    ): Context {
        val pm = testContext.packageManager
        val packageManager = shadowOf(pm)

        urlToPackages.forEach { (urlString, pkgName, className) ->
            val intent = Intent.parseUri(urlString, 0).addCategory(Intent.CATEGORY_BROWSABLE)

            val info = ActivityInfo().apply {
                packageName = pkgName
                name = className
                icon = android.R.drawable.btn_default
            }

            val resolveInfo = ResolveInfo().apply {
                labelRes = android.R.string.ok
                activityInfo = info
            }
            @Suppress("DEPRECATION") // Deprecation will be handled in https://github.com/mozilla-mobile/android-components/issues/11832
            packageManager.addResolveInfoForIntent(intent, resolveInfo)
            packageManager.addDrawableResolution(pkgName, android.R.drawable.btn_default, mock())
        }

        val context = mock<Context>()
        `when`(context.packageManager).thenReturn(pm)
        if (!default) {
            `when`(context.packageName).thenReturn(testBrowserPackage)
        }

        installedApps.forEach { name ->
            val packageInfo = PackageInfo().apply {
                packageName = name
            }
            packageManager.addPackageNoDefaults(packageInfo)
        }

        return context
    }

    @Test
    fun `WHEN receiving a malformed URL THEN will not cause a crash`() {
        val context = createContext()
        val subject = AppLinksUseCases(context, { true })
        val redirect = subject.interceptedAppLinkRedirect("test://test#Intent;")
        assertFalse(redirect.isRedirect())
    }

    @Test
    fun `A URL that matches app with activity is an app link with correct component`() {
        val context = createContext(Triple(layerUrl, layerPackage, layerActivity))
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(layerUrl)
        assertTrue(redirect.isRedirect())
        assertEquals(redirect.appIntent?.component?.packageName, layerPackage)
        assertEquals(redirect.appIntent?.component?.className, layerActivity)
    }

    @Test
    fun `A URL that matches zero apps is not an app link`() {
        val context = createContext()
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(appUrl)
        assertFalse(redirect.isRedirect())
    }

    @Test
    fun `A web URL that matches more than zero apps is an app link`() {
        val context = createContext(Triple(appUrl, appPackage, ""))
        val subject = AppLinksUseCases(context, { true })

        // We will redirect to it if browser option set to true.
        val redirect = subject.interceptedAppLinkRedirect(appUrl)
        assertTrue(redirect.isRedirect())
    }

    @Test
    fun `A intent that targets a specific package but installed will not uses market intent`() {
        val context = createContext(installedApps = listOf("com.example.com"))
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(appIntentWithPackageAndFallback)
        assertFalse(redirect.hasMarketplaceIntent())
        assertTrue(redirect.hasFallback())
    }

    @Test
    fun `A intent that targets a specific package but not installed will uses market intent`() {
        val context = createContext()
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(appIntentWithPackageAndFallback)
        assertFalse(redirect.hasExternalApp())
        assertTrue(redirect.hasMarketplaceIntent())
        assertTrue(redirect.hasFallback())
    }

    @Test
    fun `A file is not an app link`() {
        val context = createContext(Triple(filePath, appPackage, ""))
        val subject = AppLinksUseCases(context, { true })

        // We will redirect to it if browser option set to true.
        val redirect = subject.interceptedAppLinkRedirect(filePath)
        assertFalse(redirect.isRedirect())
    }

    @Test
    fun `A data url is not an app link`() {
        val context = createContext(Triple(dataUrl, appPackage, ""))
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(dataUrl)
        assertFalse(redirect.isRedirect())
    }

    @Test
    fun `A javascript url is not an app link`() {
        val context = createContext(Triple(javascriptUrl, appPackage, ""))
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(javascriptUrl)
        assertFalse(redirect.isRedirect())
    }

    @Test
    fun `An about url is not an app link`() {
        val context = createContext(Triple(aboutUrl, appPackage, ""))
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(aboutUrl)
        assertFalse(redirect.isRedirect())
    }

    @Test
    fun `A jar url is not an app link`() {
        val context = createContext(Triple(jarUrl, appPackage, ""))
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(jarUrl)
        assertFalse(redirect.isRedirect())
    }

    @Test
    fun `A content url is not an app link`() {
        val context = createContext(Triple(contentUrl, appPackage, ""))
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(contentUrl)
        assertFalse(redirect.isRedirect())
    }

    @Test
    fun `A fido url is not an app link`() {
        val context = createContext(Triple(fidoPath, appPackage, ""))
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(fidoPath)
        assertFalse(redirect.isRedirect())
    }

    @Test
    fun `Will not redirect app link if browser option set to false and scheme is supported`() {
        val context = createContext(Triple(appUrl, appPackage, ""))
        val subject = AppLinksUseCases(context, { false })

        val redirect = subject.interceptedAppLinkRedirect(appUrl)
        assertFalse(redirect.isRedirect())

        val menuRedirect = subject.appLinkRedirect(appUrl)
        assertTrue(menuRedirect.isRedirect())
    }

    @Test
    fun `Will redirect app link if browser option set to false and scheme is not supported`() {
        val context = createContext(Triple(appIntent, appPackage, ""))
        val subject = AppLinksUseCases(context, { false })

        val redirect = subject.interceptedAppLinkRedirect(appIntent)
        assertTrue(redirect.isRedirect())

        val menuRedirect = subject.appLinkRedirect(appIntent)
        assertTrue(menuRedirect.isRedirect())
    }

    @Test
    fun `WHEN A URL that matches a browser AND the scheme is not supported THEN is an app link`() {
        val context = createContext(Triple(browserSchemeUrl, browserPackage, ""))
        val browsers: Browsers = mock()
        whenever(browsers.isInstalled(browserPackage)).thenReturn(true)
        val subject = AppLinksUseCases(context = context, launchInApp = { true }, installedBrowsers = browsers)

        val redirect = subject.interceptedAppLinkRedirect(browserSchemeUrl)
        assertTrue(redirect.isRedirect())

        val menuRedirect = subject.appLinkRedirect(browserSchemeUrl)
        assertTrue(menuRedirect.isRedirect())
    }

    @Test
    fun `WHEN A URL that matches a browser AND the scheme is supported THEN is not an app link`() {
        val context = createContext(Triple(appUrl, browserPackage, ""))
        val browsers: Browsers = mock()
        whenever(browsers.isInstalled(browserPackage)).thenReturn(true)
        val subject = AppLinksUseCases(context = context, launchInApp = { true }, installedBrowsers = browsers)

        val redirect = subject.interceptedAppLinkRedirect(appUrl)
        assertFalse(redirect.isRedirect())

        val menuRedirect = subject.appLinkRedirect(appUrl)
        assertFalse(menuRedirect.isRedirect())
    }

    @Test
    fun `A intent scheme uri with an installed app is an app link`() {
        val uri = "intent://scan/#Intent;scheme=zxing;package=com.google.zxing.client.android;end"
        val context = createContext(Triple(uri, appPackage, ""))
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(uri)
        assertTrue(redirect.hasExternalApp())
        assertNotNull(redirect.appIntent)
        assertNotNull(redirect.marketplaceIntent)

        assertEquals("zxing://scan/", redirect.appIntent!!.dataString)
    }

    @Test
    fun `A bad intent scheme uri should not cause a crash`() {
        val uri = "intent://blank#Intent;package=com.twitter.android%23Intent%3B;end"
        val context = createContext(Triple(uri, appPackage, ""))
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.appLinkRedirectIncludeInstall.invoke(uri)

        assertTrue(redirect.hasExternalApp())
        assertFalse(redirect.isInstallable())
    }

    @Test
    fun `A market scheme uri with no installed app is an install link`() {
        val uri = "intent://details/#Intent;scheme=market;package=com.google.play;end"
        val context = createContext(Triple(uri, appPackage, ""))
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect.invoke(uri)

        assertTrue(redirect.hasExternalApp())
        assertTrue(redirect.isInstallable())
        assert(
            redirect.marketplaceIntent!!.flags and Intent.FLAG_ACTIVITY_NEW_TASK
                == Intent.FLAG_ACTIVITY_NEW_TASK,
        )
    }

    @Test
    fun `A intent scheme uri without an installed app is not an app link`() {
        val uri = "intent://scan/#Intent;scheme=zxing;package=com.google.zxing.client.android;end"
        val context = createContext()
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(uri)
        assertFalse(redirect.hasExternalApp())
        assertFalse(redirect.hasFallback())
        assertNull(redirect.fallbackUrl)
        assertFalse(redirect.isInstallable())
    }

    @Test
    fun `A intent scheme uri with a fallback without an installed app is not an app link`() {
        val uri =
            "intent://scan/#Intent;scheme=zxing;package=com.google.zxing.client.android;S.browser_fallback_url=http%3A%2F%2Fzxing.org;end"
        val context = createContext()
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(uri)
        assertFalse(redirect.hasExternalApp())
        assertTrue(redirect.hasFallback())

        assertEquals("http://zxing.org", redirect.fallbackUrl)
    }

    @Test
    fun `A intent scheme denied should return no app intent`() {
        val uri = "intent://details/#Intent"
        val context = createContext(Triple(uri, appPackage, ""))
        val subject = AppLinksUseCases(context, { true }, alwaysDeniedSchemes = setOf("intent"))

        val redirect = subject.interceptedAppLinkRedirect.invoke(uri)

        assertNull(redirect.appIntent)
        assertFalse(redirect.hasExternalApp())
    }

    @Test
    fun `An openAppLink use case starts an activity`() {
        val context = createContext()
        val appIntent = Intent()
        val appName = ""
        val redirect = AppLinkRedirect(appIntent, appName, appUrl, null)
        val subject = AppLinksUseCases(context, { true })

        subject.openAppLink(redirect.appIntent)

        verify(context).startActivity(any())
    }

    @Test
    fun `Start activity fails will perform failure action`() {
        val context = createContext()
        val appIntent = Intent()
        val appName = ""
        appIntent.putExtra(EXTRA_BROWSER_FALLBACK_URL, appUrl)
        val redirect = AppLinkRedirect(appIntent, appName, appUrl, null)
        val subject = AppLinksUseCases(context, { true })

        var failedToLaunch: String? = null
        val failedAction = { fallbackUrl: String? -> failedToLaunch = fallbackUrl }
        `when`(context.startActivity(any())).thenThrow(ActivityNotFoundException("failed"))
        subject.openAppLink(redirect.appIntent, failedToLaunchAction = failedAction)

        verify(context).startActivity(any())
        assertEquals(failedToLaunch, appUrl)
    }

    @Test
    fun `Security exception perform failure action`() {
        val context = createContext()
        val appIntent = Intent()
        val appName = ""
        appIntent.putExtra(EXTRA_BROWSER_FALLBACK_URL, appUrl)
        val redirect = AppLinkRedirect(appIntent, appName, appUrl, null)
        val subject = AppLinksUseCases(context, { true })

        var failedToLaunch: String? = null
        val failedAction = { fallbackUrl: String? -> failedToLaunch = fallbackUrl }
        `when`(context.startActivity(any())).thenThrow(SecurityException("failed"))
        subject.openAppLink(redirect.appIntent, failedToLaunchAction = failedAction)

        verify(context).startActivity(any())
        assertEquals(failedToLaunch, appUrl)
    }

    @Test
    fun `Null pointer exception perform failure action`() {
        val context = createContext()
        val appIntent = Intent()
        val appName = ""
        appIntent.putExtra(EXTRA_BROWSER_FALLBACK_URL, appUrl)
        val redirect = AppLinkRedirect(appIntent, appName, appUrl, null)
        val subject = AppLinksUseCases(context, { true })

        var failedToLaunch: String? = null
        val failedAction = { fallbackUrl: String? -> failedToLaunch = fallbackUrl }
        `when`(context.startActivity(any())).thenThrow(NullPointerException("failed"))
        subject.openAppLink(redirect.appIntent, failedToLaunchAction = failedAction)

        verify(context).startActivity(any())
        assertEquals(failedToLaunch, appUrl)
    }

    @Test
    fun `AppLinksUsecases uses cache`() {
        val context = createContext(Triple(appUrl, appPackage, ""))

        var subject = AppLinksUseCases(context, { true })
        var redirect = subject.interceptedAppLinkRedirect(appUrl)
        assertTrue(redirect.isRedirect())
        val timestamp = AppLinksUseCases.redirectCache?.cacheTimeStamp

        subject = AppLinksUseCases(context, { true })
        redirect = subject.interceptedAppLinkRedirect(appUrl)
        assertTrue(redirect.isRedirect())
        assert(timestamp == AppLinksUseCases.redirectCache?.cacheTimeStamp)

        AppLinksUseCases.clearRedirectCache()
        subject = AppLinksUseCases(context, { true })
        redirect = subject.interceptedAppLinkRedirect(appUrl)
        assertTrue(redirect.isRedirect())
    }

    @Test
    fun `OpenAppLinkRedirect should not try to open files`() {
        val context = createContext()
        val uri = Uri.fromFile(File(filePath))
        val intent = Intent(Intent.ACTION_VIEW)
        intent.setDataAndType(uri, fileType)
        val subject = AppLinksUseCases(context, { true })

        subject.openAppLink(intent)

        verify(context, never()).startActivity(any())
    }

    @Test
    fun `OpenAppLinkRedirect should not try to open data URIs`() {
        val context = createContext()
        val uri = dataUrl.toUri()
        val intent = Intent(Intent.ACTION_VIEW)
        intent.setDataAndType(uri, fileType)
        val subject = AppLinksUseCases(context, { true })

        subject.openAppLink(intent)

        verify(context, never()).startActivity(any())
    }

    @Test
    fun `OpenAppLinkRedirect should not try to open javascript URIs`() {
        val context = createContext()
        val uri = javascriptUrl.toUri()
        val intent = Intent(Intent.ACTION_VIEW)
        intent.setDataAndType(uri, fileType)
        val subject = AppLinksUseCases(context, { true })

        subject.openAppLink(intent)

        verify(context, never()).startActivity(any())
    }

    @Test
    fun `OpenAppLinkRedirect should not try to open about URIs`() {
        val context = createContext()
        val uri = aboutUrl.toUri()
        val intent = Intent(Intent.ACTION_VIEW)
        intent.setDataAndType(uri, fileType)
        val subject = AppLinksUseCases(context, { true })

        subject.openAppLink(intent)

        verify(context, never()).startActivity(any())
    }

    @Test
    fun `OpenAppLinkRedirect should not try to open jar URIs`() {
        val context = createContext()
        val uri = jarUrl.toUri()
        val intent = Intent(Intent.ACTION_VIEW)
        intent.setDataAndType(uri, fileType)
        val subject = AppLinksUseCases(context, { true })

        subject.openAppLink(intent)

        verify(context, never()).startActivity(any())
    }

    @Test
    fun `WHEN receiving a app scheme uri WITH target package THEN will have marketplace intent`() {
        val context = createContext()
        val uri = "intent://scan/#Intent;scheme=zxing;package=com.google.zxing.client.android;end"
        var subject = AppLinksUseCases(context, { false })
        var redirect = subject.interceptedAppLinkRedirect(uri)
        assertFalse(redirect.hasExternalApp())
        assertFalse(redirect.hasFallback())
        assertNotNull(redirect.marketplaceIntent)
        assertNull(redirect.fallbackUrl)

        subject = AppLinksUseCases(context, { true })
        redirect = subject.interceptedAppLinkRedirect(uri)
        assertFalse(redirect.hasExternalApp())
        assertFalse(redirect.hasFallback())
        assertNotNull(redirect.marketplaceIntent)
        assertNull(redirect.fallbackUrl)
    }

    @Test
    fun `WHEN receiving a app scheme uri THEN should try to redirect`() {
        val context = createContext(Triple(appSchemeIntent, appPackage, ""))

        var subject = AppLinksUseCases(context, { false })
        var redirect = subject.interceptedAppLinkRedirect(appSchemeIntent)
        assertTrue(redirect.hasExternalApp())
        assertFalse(redirect.hasFallback())
        assertNull(redirect.marketplaceIntent)
        assertNull(redirect.fallbackUrl)
        assertTrue(redirect.appIntent?.flags?.and(Intent.FLAG_ACTIVITY_CLEAR_TASK) == 0)

        subject = AppLinksUseCases(context, { true })
        redirect = subject.interceptedAppLinkRedirect(appSchemeIntent)
        assertTrue(redirect.hasExternalApp())
        assertFalse(redirect.hasFallback())
        assertNull(redirect.marketplaceIntent)
        assertNull(redirect.fallbackUrl)
        assertTrue(redirect.appIntent?.flags?.and(Intent.FLAG_ACTIVITY_CLEAR_TASK) == 0)
    }

    @Test
    fun `WHEN opening a app scheme uri WITH fallback URL THEN use fallback if needed`() {
        val context = createContext(Triple(appIntentWithPackageAndFallback, appPackage, ""))

        var subject = AppLinksUseCases(context, { false })
        var redirect = subject.interceptedAppLinkRedirect(appIntentWithPackageAndFallback)
        assertFalse(redirect.hasExternalApp())
        assertTrue(redirect.hasFallback())
        assertTrue(redirect.marketplaceIntent != null)
        assertEquals(redirect.fallbackUrl, "https://example.com")

        AppLinksUseCases.clearRedirectCache()
        subject = AppLinksUseCases(context, { true })
        redirect = subject.interceptedAppLinkRedirect(appIntentWithPackageAndFallback)
        assertTrue(redirect.hasExternalApp())
        assertTrue(redirect.hasFallback())
        assertTrue(redirect.marketplaceIntent != null)
        assertEquals(redirect.fallbackUrl, "https://example.com")
        assertTrue(redirect.appIntent?.flags?.and(Intent.FLAG_ACTIVITY_CLEAR_TASK) == 0)
    }

    @Test
    fun `WHEN opening a app scheme uri THEN tries to redirect`() {
        val context = createContext(Triple(appIntent, appPackage, ""))

        var subject = AppLinksUseCases(context, { false })
        var redirect = subject.interceptedAppLinkRedirect(appIntent)
        assertTrue(redirect.hasExternalApp())
        assertFalse(redirect.hasFallback())
        assertNull(redirect.marketplaceIntent)
        assertNull(redirect.fallbackUrl)
        assertTrue(redirect.appIntent?.flags?.and(Intent.FLAG_ACTIVITY_CLEAR_TASK) == 0)

        subject = AppLinksUseCases(context, { true })
        redirect = subject.interceptedAppLinkRedirect(appIntent)
        assertTrue(redirect.hasExternalApp())
        assertFalse(redirect.hasFallback())
        assertNull(redirect.marketplaceIntent)
        assertNull(redirect.fallbackUrl)
        assertTrue(redirect.appIntent?.flags?.and(Intent.FLAG_ACTIVITY_CLEAR_TASK) == 0)
    }

    @Test
    fun `WHEN opening a app scheme uri WITHOUT package installed THEN do not try to redirect`() {
        val context = createContext()

        var subject = AppLinksUseCases(context, { false })
        var redirect = subject.interceptedAppLinkRedirect(appIntent)
        assertFalse(redirect.hasExternalApp())
        assertFalse(redirect.hasFallback())
        assertNull(redirect.marketplaceIntent)
        assertNull(redirect.fallbackUrl)

        subject = AppLinksUseCases(context, { true })
        redirect = subject.interceptedAppLinkRedirect(appIntent)
        assertFalse(redirect.hasExternalApp())
        assertFalse(redirect.hasFallback())
        assertNull(redirect.marketplaceIntent)
        assertNull(redirect.fallbackUrl)
    }

    @Test
    fun `WHEN opening a app scheme uri without a host WITH package installed THEN try to redirect`() {
        val context = createContext(urlToPackages = arrayOf(Triple("my.scheme", appPackage, "")), default = true, installedApps = listOf(appPackage))

        var subject = AppLinksUseCases(context, { false })
        var redirect = subject.interceptedAppLinkRedirect("my.scheme")
        assertTrue(redirect.hasExternalApp())
        assertFalse(redirect.hasFallback())
        assertNull(redirect.marketplaceIntent)
        assertNull(redirect.fallbackUrl)
        assertTrue(redirect.appIntent?.flags?.and(Intent.FLAG_ACTIVITY_CLEAR_TASK) == 0)

        subject = AppLinksUseCases(context, { true })
        redirect = subject.interceptedAppLinkRedirect("my.scheme")
        assertTrue(redirect.hasExternalApp())
        assertFalse(redirect.hasFallback())
        assertNull(redirect.marketplaceIntent)
        assertNull(redirect.fallbackUrl)
        assertTrue(redirect.appIntent?.flags?.and(Intent.FLAG_ACTIVITY_CLEAR_TASK) == 0)
    }

    @Test
    fun `Failed to parse uri should not cause a crash`() {
        val context = createContext()
        val subject = AppLinksUseCases(context, { true })
        var uri = "intent://blank#Intent;package=test"
        var result = subject.safeParseUri(uri, 0)

        assertNull(result)

        uri =
            "intent://blank#Intent;package=test;i.android.support.customtabs.extra.TOOLBAR_COLOR=2239095040;end"
        result = subject.safeParseUri(uri, 0)

        assertNull(result)
    }

    @Test
    fun `Intent targeting same package should return null`() {
        val context = createContext()
        val subject = AppLinksUseCases(context, { true })
        val uri = "intent://blank#Intent;package=$testBrowserPackage;end"
        val result = subject.safeParseUri(uri, 0)

        assertNull(result)
    }

    @Test
    fun `Intent targeting external package should not return null`() {
        val context = createContext()
        val subject = AppLinksUseCases(context, { true })
        val uri = "intent://blank#Intent;package=org.mozilla.test;end"
        val result = subject.safeParseUri(uri, 0)

        assertNotNull(result)
        assertEquals(result?.`package`, "org.mozilla.test")
    }

    @Test
    fun `WHEN launch in app is updated to true THEN should redirect`() {
        val context = createContext(Triple(appUrl, appPackage, ""))
        val subject = AppLinksUseCases(context, { false })

        var redirect = subject.interceptedAppLinkRedirect(appUrl)
        assertFalse(redirect.isRedirect())

        AppLinksUseCases.clearRedirectCache()
        subject.updateLaunchInApp { true }
        redirect = subject.interceptedAppLinkRedirect(appUrl)
        assertTrue(redirect.isRedirect())
    }

    @Test
    fun `WHEN opening a app scheme uri WITH fallback URL WHERE the URL is Google PlayStore THEN ignore fallback URL`() {
        val context = createContext(Triple(appIntentWithPackageAndPlayStoreFallback, appPackage, ""))

        val subject = AppLinksUseCases(context, { false })
        val redirect = subject.interceptedAppLinkRedirect(appIntentWithPackageAndPlayStoreFallback)
        assertTrue(redirect.hasExternalApp())
        assertFalse(redirect.hasFallback())
        assertTrue(redirect.marketplaceIntent != null)
        assertNull(redirect.fallbackUrl)
    }

    @Test
    fun `WHEN opening a app scheme uri WITHOUT package installed WHERE the URL is Google PlayStore THEN use fallback URL`() {
        val context = createContext()

        val subject = AppLinksUseCases(context, { false })
        val redirect = subject.interceptedAppLinkRedirect(appIntentWithPackageAndPlayStoreFallback)
        assertFalse(redirect.hasExternalApp())
        assertTrue(redirect.hasFallback())
        assertEquals("https://play.google.com/store/abc", redirect.fallbackUrl)
    }

    @Test
    fun `WHEN A intent WITH android fallback link THEN fallback should be used`() {
        val context = createContext()
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(urlWithAndroidFallbackLink)
        assertNotNull(redirect.fallbackUrl)
        assertTrue(redirect.hasFallback())
    }

    @Test
    fun `WHEN A intent WITH fallback link THEN fallback should be used`() {
        val context = createContext()
        val subject = AppLinksUseCases(context, { true })

        val redirect = subject.interceptedAppLinkRedirect(urlWithFallbackLink)
        assertNotNull(redirect.fallbackUrl)
        assertTrue(redirect.hasFallback())
    }
}
