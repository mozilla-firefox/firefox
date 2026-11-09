/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko

import android.content.Intent
import android.graphics.Color
import android.os.Handler
import android.os.Looper.getMainLooper
import android.os.Message
import android.view.WindowManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.security.Principal
import java.security.cert.X509Certificate
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.engine.gecko.ext.geckoTrackingProtectionPermission
import mozilla.components.browser.engine.gecko.ext.isExcludedForTrackingProtection
import mozilla.components.browser.engine.gecko.permission.geckoContentPermission
import mozilla.components.browser.engine.gecko.translate.GeckoTranslationUtils.intoTranslationError
import mozilla.components.browser.engine.gecko.util.FakeEngineDownloadDelegate
import mozilla.components.concept.engine.DefaultSettings
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags.Companion.EXTERNAL
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags.Companion.LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE
import mozilla.components.concept.engine.EngineSession.SafeBrowsingPolicy
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy.CookiePolicy
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy.TrackingCategory
import mozilla.components.concept.engine.EngineSessionState
import mozilla.components.concept.engine.HitResult
import mozilla.components.concept.engine.UnsupportedSettingException
import mozilla.components.concept.engine.content.blocking.Tracker
import mozilla.components.concept.engine.history.HistoryItem
import mozilla.components.concept.engine.history.HistoryTrackingDelegate
import mozilla.components.concept.engine.manifest.WebAppManifest
import mozilla.components.concept.engine.permission.PermissionRequest
import mozilla.components.concept.engine.request.ErrorType
import mozilla.components.concept.engine.request.RequestInterceptor
import mozilla.components.concept.engine.translate.TranslationError
import mozilla.components.concept.engine.translate.TranslationOperation
import mozilla.components.concept.engine.window.WindowRequest
import mozilla.components.concept.fetch.Headers
import mozilla.components.concept.fetch.Headers.Names.CONTENT_DISPOSITION
import mozilla.components.concept.fetch.Response
import mozilla.components.concept.storage.PageVisit
import mozilla.components.concept.storage.RedirectSource
import mozilla.components.concept.storage.VisitType
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.eq
import mozilla.components.support.test.expectException
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import mozilla.components.support.utils.DownloadUtils.RESPONSE_CODE_SUCCESS
import mozilla.components.support.utils.ThreadUtils
import mozilla.components.test.ReflectionUtils
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentCaptor
import org.mockito.ArgumentMatchers.anyBoolean
import org.mockito.ArgumentMatchers.anyInt
import org.mockito.ArgumentMatchers.anyList
import org.mockito.ArgumentMatchers.anyString
import org.mockito.Mockito.atLeastOnce
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.reset
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.verifyNoInteractions
import org.mozilla.geckoview.AllowOrDeny
import org.mozilla.geckoview.ContentBlocking
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSession.ContentDelegate.ContextElement.TYPE_AUDIO
import org.mozilla.geckoview.GeckoSession.ContentDelegate.ContextElement.TYPE_IMAGE
import org.mozilla.geckoview.GeckoSession.ContentDelegate.ContextElement.TYPE_NONE
import org.mozilla.geckoview.GeckoSession.ContentDelegate.ContextElement.TYPE_VIDEO
import org.mozilla.geckoview.GeckoSession.GeckoPrintException
import org.mozilla.geckoview.GeckoSession.GeckoPrintException.ERROR_PRINT_SETTINGS_SERVICE_NOT_AVAILABLE
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.ContentPermission.VALUE_ALLOW
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.ContentPermission.VALUE_DENY
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.PERMISSION_STORAGE_ACCESS
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.PERMISSION_TRACKING
import org.mozilla.geckoview.GeckoSession.ProgressDelegate.SecurityInformation
import org.mozilla.geckoview.GeckoSessionSettings
import org.mozilla.geckoview.PdfViewerController
import org.mozilla.geckoview.SessionFinder
import org.mozilla.geckoview.SessionPdfFileSaver
import org.mozilla.geckoview.TranslationsController
import org.mozilla.geckoview.TranslationsController.TranslationsException
import org.mozilla.geckoview.WebRequestError
import org.mozilla.geckoview.WebRequestError.ERROR_CATEGORY_UNKNOWN
import org.mozilla.geckoview.WebRequestError.ERROR_MALFORMED_URI
import org.mozilla.geckoview.WebRequestError.ERROR_UNKNOWN
import org.mozilla.geckoview.WebResponse
import org.robolectric.Shadows.shadowOf

typealias GeckoAntiTracking = ContentBlocking.AntiTracking

typealias GeckoSafeBrowsing = ContentBlocking.SafeBrowsing

typealias GeckoCookieBehavior = ContentBlocking.CookieBehavior

@RunWith(AndroidJUnit4::class)
class GeckoEngineSessionTest {

    private lateinit var runtime: GeckoRuntime
    private lateinit var geckoSession: GeckoSession
    private lateinit var geckoSessionProvider: () -> GeckoSession

    private lateinit var navigationDelegate: ArgumentCaptor<GeckoSession.NavigationDelegate>
    private lateinit var progressDelegate: ArgumentCaptor<GeckoSession.ProgressDelegate>
    private lateinit var mediaDelegate: ArgumentCaptor<GeckoSession.MediaDelegate>
    private lateinit var contentDelegate: ArgumentCaptor<GeckoSession.ContentDelegate>
    private lateinit var permissionDelegate: ArgumentCaptor<GeckoSession.PermissionDelegate>
    private lateinit var scrollDelegate: ArgumentCaptor<GeckoSession.ScrollDelegate>
    private lateinit var contentBlockingDelegate: ArgumentCaptor<ContentBlocking.Delegate>
    private lateinit var historyDelegate: ArgumentCaptor<GeckoSession.HistoryDelegate>

    @Suppress("DEPRECATION")
    // Deprecation will be handled in https://github.com/mozilla-mobile/android-components/issues/8514
    @Before
    fun setup() {
        ThreadUtils.setHandlerForTest(
            object : Handler() {
                override fun sendMessageAtTime(msg: Message, uptimeMillis: Long): Boolean {
                    val wrappedRunnable = Runnable {
                        try {
                            msg.callback?.run()
                        } catch (t: Throwable) {
                            // We ignore this in the test as the runnable could be calling
                            // a native method (disposeNative) which won't work in Robolectric
                        }
                    }
                    return super.sendMessageAtTime(Message.obtain(this, wrappedRunnable), uptimeMillis)
                }
            }
        )

        runtime = mock()
        whenever(runtime.settings).thenReturn(mock())
        navigationDelegate = ArgumentCaptor.forClass(GeckoSession.NavigationDelegate::class.java)
        progressDelegate = ArgumentCaptor.forClass(GeckoSession.ProgressDelegate::class.java)
        mediaDelegate = ArgumentCaptor.forClass(GeckoSession.MediaDelegate::class.java)
        contentDelegate = ArgumentCaptor.forClass(GeckoSession.ContentDelegate::class.java)
        permissionDelegate = ArgumentCaptor.forClass(GeckoSession.PermissionDelegate::class.java)
        scrollDelegate = ArgumentCaptor.forClass(GeckoSession.ScrollDelegate::class.java)
        contentBlockingDelegate = ArgumentCaptor.forClass(ContentBlocking.Delegate::class.java)
        historyDelegate = ArgumentCaptor.forClass(GeckoSession.HistoryDelegate::class.java)

        geckoSession = mockGeckoSession()
        geckoSessionProvider = { geckoSession }
    }

    private fun captureDelegates() {
        verify(geckoSession).navigationDelegate = navigationDelegate.capture()
        verify(geckoSession).progressDelegate = progressDelegate.capture()
        verify(geckoSession).contentDelegate = contentDelegate.capture()
        verify(geckoSession).permissionDelegate = permissionDelegate.capture()
        verify(geckoSession).scrollDelegate = scrollDelegate.capture()
        verify(geckoSession).contentBlockingDelegate = contentBlockingDelegate.capture()
        verify(geckoSession).historyDelegate = historyDelegate.capture()
        verify(geckoSession).mediaDelegate = mediaDelegate.capture()
    }

    @Test
    fun engineSessionInitialization() {
        GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)

        verify(geckoSession).open(any())

        captureDelegates()

        assertNotNull(navigationDelegate.value)
        assertNotNull(progressDelegate.value)
    }

    @Test
    fun isIgnoredForTrackingProtection() {
        val session = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)

        session.geckoPermissions = listOf(geckoContentPermission(type = PERMISSION_TRACKING, value = VALUE_ALLOW))

        var ignored = session.isIgnoredForTrackingProtection()

        assertTrue(ignored)

        session.geckoPermissions = listOf(geckoContentPermission(type = PERMISSION_TRACKING, value = VALUE_DENY))

        ignored = session.isIgnoredForTrackingProtection()

        assertFalse(ignored)
    }

    @Test
    fun `WHEN calling isExcludedForTrackingProtection THEN indicate if it is excluded for tracking protection`() {
        val excludedPermission = geckoContentPermission(type = PERMISSION_TRACKING, value = VALUE_ALLOW)

        assertTrue(excludedPermission.isExcludedForTrackingProtection)

        val noExcludedPermission = geckoContentPermission(type = PERMISSION_TRACKING, value = VALUE_DENY)

        assertFalse(noExcludedPermission.isExcludedForTrackingProtection)

        val storagePermission = geckoContentPermission(type = PERMISSION_STORAGE_ACCESS, value = VALUE_DENY)

        assertFalse(storagePermission.isExcludedForTrackingProtection)
    }

    @Test
    fun `WHEN calling geckoTrackingProtectionPermission on a session THEN provide the gecko tracking protection permission`() {
        val session = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)
        val trackingProtectionPermission = geckoContentPermission(type = PERMISSION_TRACKING, value = VALUE_ALLOW)
        val storagePermission = geckoContentPermission(type = PERMISSION_STORAGE_ACCESS, value = VALUE_DENY)

        session.geckoPermissions = listOf(trackingProtectionPermission, storagePermission)

        assertEquals(session.geckoTrackingProtectionPermission, trackingProtectionPermission)

        session.geckoPermissions = listOf(storagePermission)

        assertNull(session.geckoTrackingProtectionPermission)
    }

    @Test
    fun progressDelegateNotifiesObservers() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        var observedProgress = 0
        var observedLoadingState = false
        var observedSecurityChange = false
        engineSession.register(
            object : EngineSession.Observer {
                override fun onLoadingStateChange(loading: Boolean) {
                    observedLoadingState = loading
                }

                override fun onProgress(progress: Int) {
                    observedProgress = progress
                }

                override fun onSecurityChange(
                    secure: Boolean,
                    host: String?,
                    issuer: String?,
                    certificate: X509Certificate?,
                ) {
                    // We cannot assert on actual parameters as SecurityInfo's fields can't be set
                    // from the outside and its constructor isn't accessible either.
                    observedSecurityChange = true
                }
            }
        )

        captureDelegates()

        progressDelegate.value.onPageStart(mock(), "http://mozilla.org")
        assertEquals(GeckoEngineSession.PROGRESS_START, observedProgress)
        assertEquals(true, observedLoadingState)

        progressDelegate.value.onPageStop(mock(), true)
        assertEquals(GeckoEngineSession.PROGRESS_STOP, observedProgress)
        assertEquals(false, observedLoadingState)

        // Stop will update the loading state and progress observers even when
        // we haven't completed been successful.
        progressDelegate.value.onPageStart(mock(), "http://mozilla.org")
        assertEquals(GeckoEngineSession.PROGRESS_START, observedProgress)
        assertEquals(true, observedLoadingState)

        progressDelegate.value.onPageStop(mock(), false)
        assertEquals(GeckoEngineSession.PROGRESS_STOP, observedProgress)
        assertEquals(false, observedLoadingState)

        val securityInfo = mock<SecurityInformation>()
        progressDelegate.value.onSecurityChange(mock(), securityInfo)
        assertTrue(observedSecurityChange)

        observedSecurityChange = false

        progressDelegate.value.onSecurityChange(mock(), mock())
        assertTrue(observedSecurityChange)
    }

    @Test
    fun navigationDelegateNotifiesObservers() {
        val engineSession = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)

        var observedUrl = ""
        var observedUserGesture = true
        var observedCanGoBack = false
        var observedCanGoForward = false
        var displaysProduct = false
        var translationsProcessing = true
        engineSession.register(
            object : EngineSession.Observer {
                override fun onLocationChange(url: String, hasUserGesture: Boolean) {
                    observedUrl = url
                    observedUserGesture = hasUserGesture
                }

                override fun onNavigationStateChange(canGoBack: Boolean?, canGoForward: Boolean?) {
                    canGoBack?.let { observedCanGoBack = canGoBack }
                    canGoForward?.let { observedCanGoForward = canGoForward }
                }

                override fun onTranslatePageChange() {
                    translationsProcessing = false
                }
            }
        )

        captureDelegates()

        navigationDelegate.value.onLocationChange(mock(), "http://mozilla.org", emptyList(), false)
        assertEquals("http://mozilla.org", observedUrl)
        assertEquals(false, observedUserGesture)
        // TO DO: add a positive test case after a test endpoint is implemented in desktop (Bug 1846341)
        assertEquals(false, displaysProduct)
        assertEquals(false, translationsProcessing)

        navigationDelegate.value.onCanGoBack(mock(), true)
        assertEquals(true, observedCanGoBack)

        navigationDelegate.value.onCanGoForward(mock(), true)
        assertEquals(true, observedCanGoForward)
    }

    @Test
    fun contentDelegateNotifiesObserverAboutDownloads() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                privateMode = true,
                defaultSettings =
                    DefaultSettings(
                        downloadDelegate = FakeEngineDownloadDelegate(guessFileName = { _, _, _ -> "image name.png" })
                    ),
            )

        val observer: EngineSession.Observer = mock()
        engineSession.register(observer)

        val response =
            WebResponse.Builder("https://download.mozilla.org/image%20name.png")
                .addHeader(Headers.Names.CONTENT_TYPE, "image/png")
                .addHeader(Headers.Names.CONTENT_LENGTH, "42")
                .skipConfirmation(true)
                .requestExternalApp(true)
                .body(mock())
                .build()

        val captor = argumentCaptor<Response>()
        captureDelegates()
        contentDelegate.value.onExternalResponse(mock(), response)

        verify(observer)
            .onExternalResource(
                url = eq("https://download.mozilla.org/image%20name.png"),
                fileName = eq("image name.png"),
                contentLength = eq(42),
                contentType = eq("image/png"),
                cookie = eq(null),
                userAgent = eq(null),
                isPrivate = eq(true),
                skipConfirmation = eq(true),
                openInApp = eq(true),
                response = captor.capture(),
            )

        assertNotNull(captor.value)
    }

    @Test
    fun contentDelegateNotifiesObserverAboutDownloadsWithContentDisposition() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                privateMode = true,
                defaultSettings =
                    DefaultSettings(
                        downloadDelegate = FakeEngineDownloadDelegate(guessFileName = { _, _, _ -> "image image.png" })
                    ),
            )

        val observer: EngineSession.Observer = mock()
        engineSession.register(observer)

        val response =
            WebResponse.Builder("https://download.mozilla.org/anyfile.txt")
                .addHeader(Headers.Names.CONTENT_TYPE, "image/png")
                .addHeader(Headers.Names.CONTENT_LENGTH, "42")
                .addHeader(Headers.Names.CONTENT_DISPOSITION, "attachment; filename=\"image%0Aimage.png\"\n")
                .skipConfirmation(true)
                .requestExternalApp(true)
                .body(mock())
                .build()

        val captor = argumentCaptor<Response>()
        captureDelegates()
        contentDelegate.value.onExternalResponse(mock(), response)

        verify(observer)
            .onExternalResource(
                url = eq("https://download.mozilla.org/anyfile.txt"),
                fileName = eq("image image.png"),
                contentLength = eq(42),
                contentType = eq("image/png"),
                cookie = eq(null),
                userAgent = eq(null),
                isPrivate = eq(true),
                skipConfirmation = eq(true),
                openInApp = eq(true),
                response = captor.capture(),
            )

        assertNotNull(captor.value)
    }

    @Test
    fun contentDelegateNotifiesObserverAboutDownloadsWithMalformedContentLength() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                privateMode = true,
                defaultSettings =
                    DefaultSettings(
                        downloadDelegate = FakeEngineDownloadDelegate(guessFileName = { _, _, _ -> "image.png" })
                    ),
            )

        val observer: EngineSession.Observer = mock()
        engineSession.register(observer)

        val response =
            WebResponse.Builder("https://download.mozilla.org/image.png")
                .addHeader(Headers.Names.CONTENT_TYPE, "image/png")
                .addHeader(Headers.Names.CONTENT_LENGTH, "42,42")
                .body(mock())
                .build()

        val captor = argumentCaptor<Response>()
        captureDelegates()
        contentDelegate.value.onExternalResponse(mock(), response)

        verify(observer)
            .onExternalResource(
                url = eq("https://download.mozilla.org/image.png"),
                fileName = eq("image.png"),
                contentLength = eq(null),
                contentType = eq("image/png"),
                cookie = eq(null),
                userAgent = eq(null),
                isPrivate = eq(true),
                skipConfirmation = eq(false),
                openInApp = eq(false),
                response = captor.capture(),
            )

        assertNotNull(captor.value)
    }

    @Test
    fun contentDelegateNotifiesObserverAboutDownloadsWithEmptyContentLength() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                privateMode = true,
                defaultSettings =
                    DefaultSettings(
                        downloadDelegate = FakeEngineDownloadDelegate(guessFileName = { _, _, _ -> "image.png" })
                    ),
            )

        val observer: EngineSession.Observer = mock()
        engineSession.register(observer)

        val response =
            WebResponse.Builder("https://download.mozilla.org/image.png")
                .addHeader(Headers.Names.CONTENT_TYPE, "image/png")
                .addHeader(Headers.Names.CONTENT_LENGTH, "")
                .body(mock())
                .build()

        val captor = argumentCaptor<Response>()
        captureDelegates()
        contentDelegate.value.onExternalResponse(mock(), response)

        verify(observer)
            .onExternalResource(
                url = eq("https://download.mozilla.org/image.png"),
                fileName = eq("image.png"),
                contentLength = eq(null),
                contentType = eq("image/png"),
                cookie = eq(null),
                userAgent = eq(null),
                isPrivate = eq(true),
                skipConfirmation = eq(false),
                openInApp = eq(false),
                response = captor.capture(),
            )

        assertNotNull(captor.value)
    }

    @Test
    fun contentDelegateNotifiesObserverAboutWebAppManifest() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        val observer: EngineSession.Observer = mock()
        engineSession.register(observer)

        val json =
            JSONObject().apply {
                put("name", "Minimal")
                put("start_url", "/")
            }
        val manifest =
            WebAppManifest(
                name = "Minimal",
                startUrl = "/",
            )

        captureDelegates()
        contentDelegate.value.onWebAppManifest(mock(), json)

        verify(observer).onWebAppManifestLoaded(manifest)
    }

    @Test
    fun permissionDelegateNotifiesObservers() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        val observedContentPermissionRequests: MutableList<PermissionRequest> = mutableListOf()
        val observedAppPermissionRequests: MutableList<PermissionRequest> = mutableListOf()
        engineSession.register(
            object : EngineSession.Observer {
                override fun onContentPermissionRequest(permissionRequest: PermissionRequest) {
                    observedContentPermissionRequests.add(permissionRequest)
                }

                override fun onAppPermissionRequest(permissionRequest: PermissionRequest) {
                    observedAppPermissionRequests.add(permissionRequest)
                }
            }
        )

        captureDelegates()

        permissionDelegate.value.onContentPermissionRequest(
            geckoSession,
            geckoContentPermission("originContent", GeckoSession.PermissionDelegate.PERMISSION_GEOLOCATION),
        )

        permissionDelegate.value.onContentPermissionRequest(
            geckoSession,
            geckoContentPermission("", GeckoSession.PermissionDelegate.PERMISSION_GEOLOCATION),
        )

        permissionDelegate.value.onMediaPermissionRequest(
            geckoSession,
            "originMedia",
            emptyArray(),
            emptyArray(),
            mock(),
        )

        permissionDelegate.value.onMediaPermissionRequest(
            geckoSession,
            "about:blank",
            null,
            null,
            mock(),
        )

        permissionDelegate.value.onAndroidPermissionsRequest(
            geckoSession,
            emptyArray(),
            mock(),
        )

        permissionDelegate.value.onAndroidPermissionsRequest(
            geckoSession,
            null,
            mock(),
        )

        assertEquals(4, observedContentPermissionRequests.size)
        assertEquals("originContent", observedContentPermissionRequests[0].uri)
        assertEquals("", observedContentPermissionRequests[1].uri)
        assertEquals("originMedia", observedContentPermissionRequests[2].uri)
        assertEquals("about:blank", observedContentPermissionRequests[3].uri)
        assertEquals(2, observedAppPermissionRequests.size)
    }

    @Test
    fun scrollDelegateNotifiesObservers() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        val observedScrollChanges: MutableList<Pair<Int, Int>> = mutableListOf()
        engineSession.register(
            object : EngineSession.Observer {
                override fun onScrollChange(scrollX: Int, scrollY: Int) {
                    observedScrollChanges.add(Pair(scrollX, scrollY))
                }
            }
        )

        captureDelegates()

        scrollDelegate.value.onScrollChanged(
            geckoSession,
            1234,
            4321,
        )

        scrollDelegate.value.onScrollChanged(
            geckoSession,
            2345,
            5432,
        )

        assertEquals(2, observedScrollChanges.size)
        assertEquals(Pair(1234, 4321), observedScrollChanges[0])
        assertEquals(Pair(2345, 5432), observedScrollChanges[1])
    }

    @Test
    fun loadUrl() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        val parentEngineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)

        engineSession.loadUrl("http://mozilla.org")
        verify(geckoSession).load(GeckoSession.Loader().uri("http://mozilla.org"))

        engineSession.loadUrl("http://www.mozilla.org", flags = LoadUrlFlags.select(LoadUrlFlags.EXTERNAL))
        verify(geckoSession).load(GeckoSession.Loader().uri("http://www.mozilla.org").flags(LoadUrlFlags.EXTERNAL))

        engineSession.loadUrl("http://www.mozilla.org", parent = parentEngineSession)
        verify(geckoSession)
            .load(GeckoSession.Loader().uri("http://www.mozilla.org").referrer(parentEngineSession.geckoSession))

        val extraHeaders = mapOf("X-Extra-Header" to "true")
        engineSession.loadUrl("http://www.mozilla.org", additionalHeaders = extraHeaders)
        verify(geckoSession)
            .load(
                GeckoSession.Loader()
                    .uri("http://www.mozilla.org")
                    .additionalHeaders(extraHeaders)
                    .headerFilter(GeckoSession.HEADER_FILTER_CORS_SAFELISTED)
            )

        engineSession.loadUrl(
            "http://www.mozilla.org",
            flags = LoadUrlFlags.select(LoadUrlFlags.ALLOW_ADDITIONAL_HEADERS),
            additionalHeaders = extraHeaders,
        )
        verify(geckoSession)
            .load(
                GeckoSession.Loader()
                    .uri("http://www.mozilla.org")
                    .additionalHeaders(extraHeaders)
                    .headerFilter(GeckoSession.HEADER_FILTER_CORS_SAFELISTED)
            )

        engineSession.loadUrl("http://mozilla.org", textDirectiveUserActivation = true)
        verify(geckoSession).load(GeckoSession.Loader().uri("http://mozilla.org").textDirectiveUserActivation(true))

        engineSession.loadUrl("https://example.com")
        verify(geckoSession)
            .load(
                GeckoSession.Loader()
                    .uri("https://example.com")
                    .appLinkLaunchType(LoadUrlFlags.APP_LINK_LAUNCH_TYPE_UNKNOWN)
            )

        engineSession.loadUrl("http://mozilla.org")
        verify(geckoSession)
            .load(
                GeckoSession.Loader()
                    .uri("https://example.com")
                    .appLinkLaunchType(LoadUrlFlags.APP_LINK_LAUNCH_TYPE_COLD)
            )
    }

    @Test
    fun `loadUrl doesn't load URLs with blocked schemes`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)

        engineSession.loadUrl("file://test.txt")
        engineSession.loadUrl("FILE://test.txt")
        verify(geckoSession, never()).load(GeckoSession.Loader().uri("file://test.txt"))
        verify(geckoSession, never()).load(GeckoSession.Loader().uri("FILE://test.txt"))

        engineSession.loadUrl("resource://package/test.text")
        engineSession.loadUrl("RESOURCE://package/test.text")
        verify(geckoSession, never()).load(GeckoSession.Loader().uri("resource://package/test.text"))
        verify(geckoSession, never()).load(GeckoSession.Loader().uri("RESOURCE://package/test.text"))

        engineSession.loadUrl("fido:/12345678")
        engineSession.loadUrl("FIDO:/12345678")
        verify(geckoSession, never()).load(GeckoSession.Loader().uri("fido:/12345678"))
        verify(geckoSession, never()).load(GeckoSession.Loader().uri("FIDO:/12345678"))
    }

    @Test
    fun loadData() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        engineSession.loadData("<html><body>Hello!</body></html>")
        verify(geckoSession).load(GeckoSession.Loader().data("<html><body>Hello!</body></html>", "text/html"))

        engineSession.loadData("Hello!", "text/plain", "UTF-8")
        verify(geckoSession).load(GeckoSession.Loader().data("Hello!", "text/plain"))

        engineSession.loadData("ahr0cdovl21vemlsbgeub3jn==", "text/plain", "base64")
        verify(geckoSession).load(GeckoSession.Loader().data("ahr0cdovl21vemlsbgeub3jn==".toByteArray(), "text/plain"))

        engineSession.loadData("ahr0cdovl21vemlsbgeub3jn==", encoding = "base64")
        verify(geckoSession).load(GeckoSession.Loader().data("ahr0cdovl21vemlsbgeub3jn==".toByteArray(), "text/html"))
    }

    @Test
    fun `getGeckoFlags returns only gecko load flags`() {
        val flags = LoadUrlFlags.select(LoadUrlFlags.all().getGeckoFlags())

        assertFalse(flags.contains(LoadUrlFlags.NONE))
        assertTrue(flags.contains(LoadUrlFlags.BYPASS_CACHE))
        assertTrue(flags.contains(LoadUrlFlags.BYPASS_PROXY))
        assertTrue(flags.contains(LoadUrlFlags.EXTERNAL))
        assertTrue(flags.contains(LoadUrlFlags.ALLOW_POPUPS))
        assertTrue(flags.contains(LoadUrlFlags.BYPASS_CLASSIFIER))
        assertTrue(flags.contains(LoadUrlFlags.LOAD_FLAGS_FORCE_ALLOW_DATA_URI))
        assertTrue(flags.contains(LoadUrlFlags.LOAD_FLAGS_REPLACE_HISTORY))
        assertTrue(flags.contains(LoadUrlFlags.LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE))
        assertFalse(flags.contains(LoadUrlFlags.ALLOW_ADDITIONAL_HEADERS))
        assertFalse(flags.contains(LoadUrlFlags.ALLOW_JAVASCRIPT_URL))
    }

    @Test
    fun loadDataBase64() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        engineSession.loadData("Hello!", "text/plain", "UTF-8")
        verify(geckoSession).load(GeckoSession.Loader().data("Hello!", "text/plain"))

        engineSession.loadData("ahr0cdovl21vemlsbgeub3jn==", "text/plain", "base64")
        verify(geckoSession).load(GeckoSession.Loader().data("ahr0cdovl21vemlsbgeub3jn==".toByteArray(), "text/plain"))

        engineSession.loadData("ahr0cdovl21vemlsbgeub3jn==", encoding = "base64")
        verify(geckoSession).load(GeckoSession.Loader().data("ahr0cdovl21vemlsbgeub3jn==".toByteArray(), "text/plain"))
    }

    @Test
    fun stopLoading() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        engineSession.stopLoading()

        verify(geckoSession).stop()
    }

    @Test
    fun reload() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        engineSession.loadUrl("http://mozilla.org")

        // Initial load is still in progress so reload should not be called.
        // Instead we should have called loadUrl again to prevent reloading
        // about:blank.
        engineSession.reload()
        verify(geckoSession, never()).reload(GeckoSession.LOAD_FLAGS_BYPASS_CACHE)
        verify(geckoSession, times(2)).load(GeckoSession.Loader().uri("http://mozilla.org"))

        // Subsequent reloads should simply call reload on the gecko session.
        engineSession.initialLoadRequest = null
        engineSession.reload()
        verify(geckoSession).reload(GeckoSession.LOAD_FLAGS_NONE)

        engineSession.reload(flags = LoadUrlFlags.select(LoadUrlFlags.BYPASS_CACHE))
        verify(geckoSession).reload(GeckoSession.LOAD_FLAGS_BYPASS_CACHE)
    }

    @Test
    fun goBack() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        engineSession.goBack()

        verify(geckoSession).goBack(true)
    }

    @Test
    fun goForward() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        engineSession.goForward()

        verify(geckoSession).goForward(true)
    }

    @Test
    fun goToHistoryIndex() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        engineSession.goToHistoryIndex(0)

        verify(geckoSession).gotoHistoryIndex(0)
    }

    @Test
    fun restoreState() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        val actualState: GeckoSession.SessionState = mock()
        val state = GeckoEngineSessionState(actualState)

        assertTrue(engineSession.restoreState(state))
        verify(geckoSession).restoreState(any())
    }

    @Test
    fun `restoreState returns false for null state`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        val state = GeckoEngineSessionState(null)

        assertFalse(engineSession.restoreState(state))
        verify(geckoSession, never()).restoreState(any())
    }

    @Test
    fun progressDelegateIgnoresInitialLoadOfAboutBlank() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        var observedSecurityChange = false
        var progressObserved = false
        var loadingStateChangeObserved = false
        engineSession.register(
            object : EngineSession.Observer {
                override fun onSecurityChange(
                    secure: Boolean,
                    host: String?,
                    issuer: String?,
                    certificate: X509Certificate?,
                ) {
                    observedSecurityChange = true
                }

                override fun onProgress(progress: Int) {
                    progressObserved = true
                }

                override fun onLoadingStateChange(loading: Boolean) {
                    loadingStateChangeObserved = true
                }
            }
        )

        captureDelegates()

        progressDelegate.value.onSecurityChange(
            mock(),
            MockSecurityInformation("moz-nullprincipal:{uuid}"),
        )
        assertFalse(observedSecurityChange)

        progressDelegate.value.onSecurityChange(
            mock(),
            MockSecurityInformation("https://www.mozilla.org"),
        )
        assertTrue(observedSecurityChange)

        progressDelegate.value.onPageStart(mock(), "about:blank")
        assertFalse(progressObserved)
        assertFalse(loadingStateChangeObserved)

        progressDelegate.value.onPageStop(mock(), true)
        assertFalse(progressObserved)
        assertFalse(loadingStateChangeObserved)

        progressDelegate.value.onPageStart(mock(), "https://www.mozilla.org")
        assertTrue(progressObserved)
        assertTrue(loadingStateChangeObserved)

        progressDelegate.value.onPageStop(mock(), true)
        assertTrue(progressObserved)
        assertTrue(loadingStateChangeObserved)
    }

    @Test
    fun navigationDelegateIgnoresInitialLoadOfAboutBlank() {
        val engineSession = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)

        var observedUrl = ""
        engineSession.register(
            object : EngineSession.Observer {
                override fun onLocationChange(url: String, hasUserGesture: Boolean) {
                    observedUrl = url
                }
            }
        )

        captureDelegates()

        navigationDelegate.value.onLocationChange(mock(), "about:blank", emptyList(), false)
        assertEquals("", observedUrl)

        navigationDelegate.value.onLocationChange(mock(), "about:blank", emptyList(), false)
        assertEquals("", observedUrl)

        navigationDelegate.value.onLocationChange(mock(), "https://www.mozilla.org", emptyList(), false)
        assertEquals("https://www.mozilla.org", observedUrl)

        navigationDelegate.value.onLocationChange(mock(), "about:blank", emptyList(), false)
        assertEquals("about:blank", observedUrl)
    }

    @Test
    fun `onLoadRequest will reset initial load flag on process switch to ignore about blank loads`() {
        val session = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)
        captureDelegates()
        assertTrue(session.initialLoad)

        navigationDelegate.value.onLocationChange(mock(), "https://mozilla.org", emptyList(), false)
        assertFalse(session.initialLoad)

        navigationDelegate.value.onLoadRequest(mock(), mockLoadRequest("moz-extension://1234-test"))
        assertTrue(session.initialLoad)

        var observedUrl = ""
        session.register(
            object : EngineSession.Observer {
                override fun onLocationChange(url: String, hasUserGesture: Boolean) {
                    observedUrl = url
                }
            }
        )
        navigationDelegate.value.onLocationChange(mock(), "about:blank", emptyList(), false)
        assertEquals("", observedUrl)

        navigationDelegate.value.onLocationChange(mock(), "https://www.mozilla.org", emptyList(), false)
        assertEquals("https://www.mozilla.org", observedUrl)

        navigationDelegate.value.onLocationChange(mock(), "about:blank", emptyList(), false)
        assertEquals("about:blank", observedUrl)
    }

    @Test
    fun `do not keep track of current url via onPageStart events`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        assertNull(engineSession.currentUrl)
        progressDelegate.value.onPageStart(geckoSession, "https://www.mozilla.org")
        assertNull(engineSession.currentUrl)

        progressDelegate.value.onPageStart(geckoSession, "https://www.firefox.com")
        assertNull(engineSession.currentUrl)
    }

    @Test
    fun `keeps track of current url via onLocationChange events`() {
        val engineSession = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)
        val geckoResult = GeckoResult<Boolean?>()

        captureDelegates()
        geckoResult.complete(true)

        assertNull(engineSession.currentUrl)
        navigationDelegate.value.onLocationChange(geckoSession, "https://www.mozilla.org", emptyList(), false)
        assertEquals("https://www.mozilla.org", engineSession.currentUrl)

        navigationDelegate.value.onLocationChange(geckoSession, "https://www.firefox.com", emptyList(), false)
        assertEquals("https://www.firefox.com", engineSession.currentUrl)
    }

    @Test
    fun `WHEN onLocationChange is called THEN geckoPermissions is assigned`() {
        val engineSession = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)

        captureDelegates()

        navigationDelegate.value.onLocationChange(geckoSession, "https://www.mozilla.org", listOf(mock()), false)

        assertTrue(engineSession.geckoPermissions.isNotEmpty())
    }

    @Test
    fun `WHEN onLocationChange is called with null URL THEN geckoPermissions is assigned`() {
        val engineSession = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)

        captureDelegates()

        navigationDelegate.value.onLocationChange(geckoSession, null, listOf(mock()), false)

        assertTrue(engineSession.geckoPermissions.isNotEmpty())
    }

    @Test
    fun `notifies configured history delegate of title changes`() = runTest {
        val engineSession =
            GeckoEngineSession(
                runtime,
                geckoSessionProvider = geckoSessionProvider,
                context = coroutineContext,
            )
        val historyTrackingDelegate: HistoryTrackingDelegate = mock()

        captureDelegates()

        // Nothing breaks if history delegate isn't configured.
        contentDelegate.value.onTitleChange(geckoSession, "Hello World!")
        testScheduler.advanceUntilIdle()

        engineSession.settings.historyTrackingDelegate = historyTrackingDelegate
        whenever(historyTrackingDelegate.shouldStoreUri(eq("https://www.mozilla.com"))).thenReturn(true)

        contentDelegate.value.onTitleChange(geckoSession, "Hello World!")
        testScheduler.advanceUntilIdle()

        verify(historyTrackingDelegate, never()).onTitleChanged(anyString(), anyString())

        // This sets the currentUrl.
        navigationDelegate.value.onLocationChange(geckoSession, "https://www.mozilla.com", emptyList(), false)

        contentDelegate.value.onTitleChange(geckoSession, "Hello World!")
        testScheduler.advanceUntilIdle()

        verify(historyTrackingDelegate).onTitleChanged(eq("https://www.mozilla.com"), eq("Hello World!"))
        verify(historyTrackingDelegate).shouldStoreUri(eq("https://www.mozilla.com"))
    }

    @Test
    fun `does not notify configured history delegate of title changes for private sessions`() = runTest {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                context = coroutineContext,
                privateMode = true,
            )
        val historyTrackingDelegate: HistoryTrackingDelegate = mock()

        captureDelegates()

        // Nothing breaks if history delegate isn't configured.
        contentDelegate.value.onTitleChange(geckoSession, "Hello World!")

        engineSession.settings.historyTrackingDelegate = historyTrackingDelegate

        val observer: EngineSession.Observer = mock()
        engineSession.register(observer)

        contentDelegate.value.onTitleChange(geckoSession, "Hello World!")
        verify(historyTrackingDelegate, never()).onTitleChanged(anyString(), anyString())
        verify(observer).onTitleChange("Hello World!")

        // This sets the currentUrl.
        progressDelegate.value.onPageStart(geckoSession, "https://www.mozilla.com")

        contentDelegate.value.onTitleChange(geckoSession, "Mozilla")
        verify(historyTrackingDelegate, never()).onTitleChanged(anyString(), anyString())
        verify(observer).onTitleChange("Mozilla")
    }

    @Test
    fun `GIVEN an app initiated request WHEN the user swipe back or launches the browser THEN the tab should display the correct page`() =
        runTest {
            val engineSession =
                GeckoEngineSession(
                    mock(),
                    geckoSessionProvider = geckoSessionProvider,
                    context = coroutineContext,
                )

            captureDelegates()

            val historyTrackingDelegate: HistoryTrackingDelegate = mock()

            var observedUrl = "https://www.google.com"
            var observedTitle = "Google Search"
            val emptyPageUrl = "https://example.com"

            engineSession.register(
                object : EngineSession.Observer {
                    override fun onLocationChange(url: String, hasUserGesture: Boolean) {
                        observedUrl = url
                    }

                    override fun onTitleChange(title: String) {
                        observedTitle = title
                    }
                }
            )
            engineSession.settings.historyTrackingDelegate = historyTrackingDelegate
            engineSession.appRedirectUrl = emptyPageUrl
            engineSession.initialLoad = false

            class MockHistoryList(
                items: List<GeckoSession.HistoryDelegate.HistoryItem>,
                private val currentIndex: Int,
            ) : ArrayList<GeckoSession.HistoryDelegate.HistoryItem>(items), GeckoSession.HistoryDelegate.HistoryList {
                override fun getCurrentIndex() = currentIndex
            }

            fun mockHistoryItem(title: String?, uri: String): GeckoSession.HistoryDelegate.HistoryItem {
                val item = mock<GeckoSession.HistoryDelegate.HistoryItem>()
                whenever(item.title).thenReturn(title)
                whenever(item.uri).thenReturn(uri)
                return item
            }

            historyDelegate.value.onHistoryStateChange(mock(), MockHistoryList(emptyList(), 0))

            historyDelegate.value.onHistoryStateChange(
                mock(),
                MockHistoryList(
                    listOf(
                        mockHistoryItem("Google Search", observedUrl),
                        mockHistoryItem("Moved", emptyPageUrl),
                    ),
                    1,
                ),
            )

            navigationDelegate.value.onLocationChange(geckoSession, emptyPageUrl, emptyList(), false)
            contentDelegate.value.onTitleChange(geckoSession, emptyPageUrl)

            historyDelegate.value.onVisited(
                geckoSession,
                emptyPageUrl,
                null,
                9,
            )

            verify(historyTrackingDelegate, never()).onVisited(eq(emptyPageUrl), any())
            assertEquals("https://www.google.com", observedUrl)
            assertEquals("Google Search", observedTitle)
        }

    @Test
    fun `GIVEN an app initiated request AND initial load WHEN user swipe back THEN the tab should display the loaded page`() =
        runTest {
            val engineSession =
                GeckoEngineSession(
                    mock(),
                    geckoSessionProvider = geckoSessionProvider,
                    context = coroutineContext,
                )

            captureDelegates()

            var observedUrl = "https://www.google.com"
            val emptyPageUrl = "https://example.com"

            engineSession.register(
                object : EngineSession.Observer {
                    override fun onLocationChange(url: String, hasUserGesture: Boolean) {
                        observedUrl = url
                    }
                }
            )
            engineSession.appRedirectUrl = emptyPageUrl
            engineSession.initialLoad = true

            navigationDelegate.value.onLocationChange(geckoSession, emptyPageUrl, emptyList(), false)
            contentDelegate.value.onTitleChange(geckoSession, emptyPageUrl)

            assertEquals("https://example.com", observedUrl)
        }

    @Test
    fun `notifies configured history delegate of preview image URL changes`() = runTest {
        val engineSession =
            GeckoEngineSession(
                runtime,
                geckoSessionProvider = geckoSessionProvider,
                context = coroutineContext,
            )
        val historyTrackingDelegate: HistoryTrackingDelegate = mock()
        val geckoResult = GeckoResult<Boolean?>()

        captureDelegates()
        geckoResult.complete(true)

        val previewImageUrl = "https://test.com/og-image-url"

        // Nothing breaks if history delegate isn't configured.
        contentDelegate.value.onPreviewImage(geckoSession, previewImageUrl)
        testScheduler.advanceUntilIdle()

        engineSession.settings.historyTrackingDelegate = historyTrackingDelegate
        whenever(historyTrackingDelegate.shouldStoreUri(eq("https://www.mozilla.com"))).thenReturn(true)

        contentDelegate.value.onPreviewImage(geckoSession, previewImageUrl)
        testScheduler.advanceUntilIdle()

        verify(historyTrackingDelegate, never()).onPreviewImageChange(anyString(), anyString())

        // This sets the currentUrl.
        navigationDelegate.value.onLocationChange(geckoSession, "https://www.mozilla.com", emptyList(), false)

        contentDelegate.value.onPreviewImage(geckoSession, previewImageUrl)
        testScheduler.advanceUntilIdle()

        verify(historyTrackingDelegate).onPreviewImageChange(eq("https://www.mozilla.com"), eq(previewImageUrl))
        verify(historyTrackingDelegate).shouldStoreUri(eq("https://www.mozilla.com"))
    }

    @Test
    fun `does not notify configured history delegate of preview image URL changes for private sessions`() = runTest {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                context = coroutineContext,
                privateMode = true,
            )
        val historyTrackingDelegate: HistoryTrackingDelegate = mock()

        captureDelegates()

        // Nothing breaks if history delegate isn't configured.
        contentDelegate.value.onPreviewImage(geckoSession, "https://test.com/og-image-url")
        testScheduler.advanceUntilIdle()

        engineSession.settings.historyTrackingDelegate = historyTrackingDelegate

        val observer: EngineSession.Observer = mock()
        engineSession.register(observer)

        contentDelegate.value.onPreviewImage(geckoSession, "https://test.com/og-image-url")
        testScheduler.advanceUntilIdle()

        verify(historyTrackingDelegate, never()).onPreviewImageChange(anyString(), anyString())
        verify(observer).onPreviewImageChange("https://test.com/og-image-url")

        // This sets the currentUrl.
        progressDelegate.value.onPageStart(geckoSession, "https://www.mozilla.com")

        contentDelegate.value.onPreviewImage(geckoSession, "https://test.com/og-image.jpg")
        testScheduler.advanceUntilIdle()

        verify(historyTrackingDelegate, never()).onPreviewImageChange(anyString(), anyString())
        verify(observer).onPreviewImageChange("https://test.com/og-image.jpg")
    }

    @Test
    fun `does not notify configured history delegate for top-level visits to error pages`() = runTest {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                context = coroutineContext,
            )
        val historyTrackingDelegate: HistoryTrackingDelegate = mock()

        captureDelegates()

        engineSession.settings.historyTrackingDelegate = historyTrackingDelegate
        whenever(historyTrackingDelegate.shouldStoreUri(any())).thenReturn(true)

        historyDelegate.value.onVisited(
            geckoSession,
            "about:neterror",
            null,
            GeckoSession.HistoryDelegate.VISIT_TOP_LEVEL or GeckoSession.HistoryDelegate.VISIT_UNRECOVERABLE_ERROR,
        )
        engineSession.job.children.forEach { it.join() }
        verify(historyTrackingDelegate, never()).onVisited(anyString(), any())
    }

    @Test
    fun `notifies configured history delegate of visits`() = runTest {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                context = coroutineContext,
            )
        val historyTrackingDelegate: HistoryTrackingDelegate = mock()

        captureDelegates()

        engineSession.settings.historyTrackingDelegate = historyTrackingDelegate
        whenever(historyTrackingDelegate.shouldStoreUri("https://www.mozilla.com")).thenReturn(true)

        historyDelegate.value.onVisited(
            geckoSession,
            "https://www.mozilla.com",
            null,
            GeckoSession.HistoryDelegate.VISIT_TOP_LEVEL,
        )
        engineSession.job.children.forEach { it.join() }
        verify(historyTrackingDelegate).onVisited(eq("https://www.mozilla.com"), eq(PageVisit(VisitType.LINK)))
    }

    @Test
    fun `notifies configured history delegate of reloads`() = runTest {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                context = coroutineContext,
            )
        val historyTrackingDelegate: HistoryTrackingDelegate = mock()

        captureDelegates()

        engineSession.settings.historyTrackingDelegate = historyTrackingDelegate
        whenever(historyTrackingDelegate.shouldStoreUri("https://www.mozilla.com")).thenReturn(true)

        historyDelegate.value.onVisited(
            geckoSession,
            "https://www.mozilla.com",
            "https://www.mozilla.com",
            GeckoSession.HistoryDelegate.VISIT_TOP_LEVEL,
        )
        engineSession.job.children.forEach { it.join() }
        verify(historyTrackingDelegate).onVisited(eq("https://www.mozilla.com"), eq(PageVisit(VisitType.RELOAD)))
    }

    @Test
    fun `checks with the delegate before trying to record a visit`() = runTest {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                context = coroutineContext,
            )
        val historyTrackingDelegate: HistoryTrackingDelegate = mock()

        captureDelegates()

        engineSession.settings.historyTrackingDelegate = historyTrackingDelegate
        whenever(historyTrackingDelegate.shouldStoreUri("https://www.mozilla.com/allowed")).thenReturn(true)
        whenever(historyTrackingDelegate.shouldStoreUri("https://www.mozilla.com/not-allowed")).thenReturn(false)

        historyDelegate.value.onVisited(
            geckoSession,
            "https://www.mozilla.com/allowed",
            null,
            GeckoSession.HistoryDelegate.VISIT_TOP_LEVEL,
        )

        engineSession.job.children.forEach { it.join() }
        verify(historyTrackingDelegate).shouldStoreUri("https://www.mozilla.com/allowed")
        verify(historyTrackingDelegate).onVisited(eq("https://www.mozilla.com/allowed"), eq(PageVisit(VisitType.LINK)))

        historyDelegate.value.onVisited(
            geckoSession,
            "https://www.mozilla.com/not-allowed",
            null,
            GeckoSession.HistoryDelegate.VISIT_TOP_LEVEL,
        )

        engineSession.job.children.forEach { it.join() }
        verify(historyTrackingDelegate).shouldStoreUri("https://www.mozilla.com/not-allowed")
        verify(historyTrackingDelegate, never()).onVisited(eq("https://www.mozilla.com/not-allowed"), any())
    }

    @Test
    fun `correctly processes redirect visit flags`() = runTest {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                context = coroutineContext,
            )
        val historyTrackingDelegate: HistoryTrackingDelegate = mock()

        captureDelegates()

        engineSession.settings.historyTrackingDelegate = historyTrackingDelegate
        whenever(historyTrackingDelegate.shouldStoreUri(any())).thenReturn(true)

        historyDelegate.value.onVisited(
            geckoSession,
            "https://www.mozilla.com/tempredirect",
            null,
            // bitwise 'or'
            GeckoSession.HistoryDelegate.VISIT_TOP_LEVEL or GeckoSession.HistoryDelegate.VISIT_REDIRECT_SOURCE,
        )

        engineSession.job.children.forEach { it.join() }
        verify(historyTrackingDelegate)
            .onVisited(
                eq("https://www.mozilla.com/tempredirect"),
                eq(PageVisit(VisitType.REDIRECT_TEMPORARY, RedirectSource.TEMPORARY)),
            )

        historyDelegate.value.onVisited(
            geckoSession,
            "https://www.mozilla.com/permredirect",
            null,
            GeckoSession.HistoryDelegate.VISIT_TOP_LEVEL or
                GeckoSession.HistoryDelegate.VISIT_REDIRECT_SOURCE_PERMANENT,
        )

        engineSession.job.children.forEach { it.join() }
        verify(historyTrackingDelegate)
            .onVisited(
                eq("https://www.mozilla.com/permredirect"),
                eq(PageVisit(VisitType.REDIRECT_PERMANENT, RedirectSource.PERMANENT)),
            )

        // Visits below are targets of redirects, not redirects themselves.
        // Check that they're mapped to "link".
        historyDelegate.value.onVisited(
            geckoSession,
            "https://www.mozilla.com/targettemp",
            null,
            GeckoSession.HistoryDelegate.VISIT_TOP_LEVEL or GeckoSession.HistoryDelegate.VISIT_REDIRECT_TEMPORARY,
        )

        engineSession.job.children.forEach { it.join() }
        verify(historyTrackingDelegate)
            .onVisited(eq("https://www.mozilla.com/targettemp"), eq(PageVisit(VisitType.LINK)))

        historyDelegate.value.onVisited(
            geckoSession,
            "https://www.mozilla.com/targetperm",
            null,
            GeckoSession.HistoryDelegate.VISIT_TOP_LEVEL or GeckoSession.HistoryDelegate.VISIT_REDIRECT_PERMANENT,
        )

        engineSession.job.children.forEach { it.join() }
        verify(historyTrackingDelegate)
            .onVisited(eq("https://www.mozilla.com/targetperm"), eq(PageVisit(VisitType.LINK)))
    }

    @Test
    fun `does not notify configured history delegate of visits for private sessions`() = runTest {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                context = coroutineContext,
                privateMode = true,
            )
        val historyTrackingDelegate: HistoryTrackingDelegate = mock()

        captureDelegates()

        engineSession.settings.historyTrackingDelegate = historyTrackingDelegate

        historyDelegate.value.onVisited(
            geckoSession,
            "https://www.mozilla.com",
            "https://www.mozilla.com",
            GeckoSession.HistoryDelegate.VISIT_TOP_LEVEL,
        )
        engineSession.job.children.forEach { it.join() }
        verify(historyTrackingDelegate, never()).onVisited(anyString(), any())
    }

    @Test
    fun `requests visited URLs from configured history delegate`() = runTest {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                context = coroutineContext,
            )
        val historyTrackingDelegate: HistoryTrackingDelegate = mock()

        captureDelegates()

        // Nothing breaks if history delegate isn't configured.
        historyDelegate.value.getVisited(geckoSession, arrayOf("https://www.mozilla.com", "https://www.mozilla.org"))
        engineSession.job.children.forEach { it.join() }

        engineSession.settings.historyTrackingDelegate = historyTrackingDelegate

        historyDelegate.value.getVisited(geckoSession, arrayOf("https://www.mozilla.com", "https://www.mozilla.org"))
        engineSession.job.children.forEach { it.join() }
        verify(historyTrackingDelegate).getVisited(eq(listOf("https://www.mozilla.com", "https://www.mozilla.org")))
    }

    @Test
    fun `does not request visited URLs from configured history delegate in private sessions`() = runTest {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                context = coroutineContext,
                privateMode = true,
            )
        val historyTrackingDelegate: HistoryTrackingDelegate = mock()

        captureDelegates()

        engineSession.settings.historyTrackingDelegate = historyTrackingDelegate

        historyDelegate.value.getVisited(geckoSession, arrayOf("https://www.mozilla.com", "https://www.mozilla.org"))
        engineSession.job.children.forEach { it.join() }
        verify(historyTrackingDelegate, never()).getVisited(anyList())
    }

    @Test
    fun `notifies configured history delegate of state changes`() = runTest {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                context = coroutineContext,
            )
        val observer = mock<EngineSession.Observer>()
        engineSession.register(observer)

        captureDelegates()

        class MockHistoryList(
            items: List<GeckoSession.HistoryDelegate.HistoryItem>,
            private val currentIndex: Int,
        ) : ArrayList<GeckoSession.HistoryDelegate.HistoryItem>(items), GeckoSession.HistoryDelegate.HistoryList {
            override fun getCurrentIndex() = currentIndex
        }

        fun mockHistoryItem(title: String?, uri: String): GeckoSession.HistoryDelegate.HistoryItem {
            val item = mock<GeckoSession.HistoryDelegate.HistoryItem>()
            whenever(item.title).thenReturn(title)
            whenever(item.uri).thenReturn(uri)
            return item
        }

        historyDelegate.value.onHistoryStateChange(mock(), MockHistoryList(emptyList(), 0))
        verify(observer).onHistoryStateChanged(emptyList(), 0)

        historyDelegate.value.onHistoryStateChange(
            mock(),
            MockHistoryList(
                listOf(
                    mockHistoryItem("Firefox", "https://firefox.com"),
                    mockHistoryItem("Mozilla", "http://mozilla.org"),
                    mockHistoryItem(null, "https://example.com"),
                ),
                1,
            ),
        )
        verify(observer)
            .onHistoryStateChanged(
                listOf(
                    HistoryItem("Firefox", "https://firefox.com"),
                    HistoryItem("Mozilla", "http://mozilla.org"),
                    HistoryItem("https://example.com", "https://example.com"),
                ),
                1,
            )
    }

    @Test
    fun websiteTitleUpdates() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        val observer: EngineSession.Observer = mock()
        engineSession.register(observer)

        captureDelegates()

        contentDelegate.value.onTitleChange(geckoSession, "Hello World!")

        verify(observer).onTitleChange("Hello World!")
    }

    @Test
    fun `WHEN preview image URL changes THEN notify observers`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        val observer: EngineSession.Observer = mock()
        engineSession.register(observer)

        captureDelegates()

        val previewImageURL = "https://test.com/og-image-url"
        contentDelegate.value.onPreviewImage(geckoSession, previewImageURL)

        verify(observer).onPreviewImageChange(previewImageURL)
    }

    @Test
    fun trackingProtectionDelegateNotifiesObservers() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        var trackerBlocked: Tracker? = null
        engineSession.register(
            object : EngineSession.Observer {
                override fun onTrackerBlocked(tracker: Tracker) {
                    trackerBlocked = tracker
                }
            }
        )

        captureDelegates()
        var geckoCategories = 0
        geckoCategories = geckoCategories.or(GeckoAntiTracking.AD)
        geckoCategories = geckoCategories.or(GeckoAntiTracking.ANALYTIC)
        geckoCategories = geckoCategories.or(GeckoAntiTracking.SOCIAL)
        geckoCategories = geckoCategories.or(GeckoAntiTracking.CRYPTOMINING)
        geckoCategories = geckoCategories.or(GeckoAntiTracking.FINGERPRINTING)
        geckoCategories = geckoCategories.or(GeckoAntiTracking.CONTENT)
        geckoCategories = geckoCategories.or(GeckoAntiTracking.TEST)

        contentBlockingDelegate.value.onContentBlocked(
            geckoSession,
            ContentBlocking.BlockEvent("tracker1", geckoCategories, 0, 0, false),
        )

        assertEquals("tracker1", trackerBlocked!!.url)

        val expectedBlockedCategories =
            listOf(
                TrackingCategory.AD,
                TrackingCategory.ANALYTICS,
                TrackingCategory.SOCIAL,
                TrackingCategory.CRYPTOMINING,
                TrackingCategory.FINGERPRINTING,
                TrackingCategory.CONTENT,
                TrackingCategory.TEST,
            )

        assertTrue(trackerBlocked.trackingCategories.containsAll(expectedBlockedCategories))

        var trackerLoaded: Tracker? = null
        engineSession.register(
            object : EngineSession.Observer {
                override fun onTrackerLoaded(tracker: Tracker) {
                    trackerLoaded = tracker
                }
            }
        )

        var geckoCookieCategories = 0
        geckoCookieCategories = geckoCookieCategories.or(GeckoCookieBehavior.ACCEPT_ALL)
        geckoCookieCategories = geckoCookieCategories.or(GeckoCookieBehavior.ACCEPT_VISITED)
        geckoCookieCategories = geckoCookieCategories.or(GeckoCookieBehavior.ACCEPT_NON_TRACKERS)
        geckoCookieCategories = geckoCookieCategories.or(GeckoCookieBehavior.ACCEPT_NONE)
        geckoCookieCategories = geckoCookieCategories.or(GeckoCookieBehavior.ACCEPT_FIRST_PARTY)

        contentBlockingDelegate.value.onContentLoaded(
            geckoSession,
            ContentBlocking.BlockEvent("tracker1", 0, 0, geckoCookieCategories, false),
        )

        val expectedCookieCategories =
            listOf(
                CookiePolicy.ACCEPT_ONLY_FIRST_PARTY,
                CookiePolicy.ACCEPT_NONE,
                CookiePolicy.ACCEPT_VISITED,
                CookiePolicy.ACCEPT_NON_TRACKERS,
            )

        assertEquals("tracker1", trackerLoaded!!.url)
        assertTrue(trackerLoaded.cookiePolicies.containsAll(expectedCookieCategories))

        contentBlockingDelegate.value.onContentLoaded(
            geckoSession,
            ContentBlocking.BlockEvent("tracker1", 0, 0, GeckoCookieBehavior.ACCEPT_ALL, false),
        )

        assertTrue(trackerLoaded.cookiePolicies.containsAll(listOf(CookiePolicy.ACCEPT_ALL)))
    }

    @Test
    fun `WHEN updateing tracking protection with a recommended policy THEN etpEnabled should be enabled`() {
        whenever(runtime.settings).thenReturn(mock())
        whenever(runtime.settings.contentBlocking).thenReturn(mock())

        val session = spy(GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider))
        var trackerBlockingObserved = false

        session.register(
            object : EngineSession.Observer {
                override fun onTrackerBlockingEnabledChange(enabled: Boolean) {
                    trackerBlockingObserved = enabled
                }
            }
        )

        val policy = TrackingProtectionPolicy.recommended()
        session.updateTrackingProtection(policy)
        shadowOf(getMainLooper()).idle()

        verify(session).updateContentBlocking(policy)
        assertTrue(session.etpEnabled!!)
        assertTrue(trackerBlockingObserved)
    }

    @Test
    fun `WHEN calling updateTrackingProtection with a none policy THEN etpEnabled should be disabled`() {
        whenever(runtime.settings).thenReturn(mock())
        whenever(runtime.settings.contentBlocking).thenReturn(mock())

        val session = spy(GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider))
        var trackerBlockingObserved = false

        session.register(
            object : EngineSession.Observer {
                override fun onTrackerBlockingEnabledChange(enabled: Boolean) {
                    trackerBlockingObserved = enabled
                }
            }
        )

        val policy = TrackingProtectionPolicy.none()
        session.updateTrackingProtection(policy)

        verify(session).updateContentBlocking(policy)
        assertFalse(session.etpEnabled!!)
        assertFalse(trackerBlockingObserved)
    }

    @Test
    fun `WHEN updating the contentBlocking with a policy SCRIPTS_AND_SUB_RESOURCES useForPrivateSessions being in privateMode THEN useTrackingProtection should be true`() {
        val geckoSetting = mock<GeckoSessionSettings>()
        val geckoSession = mock<GeckoSession>()

        val session =
            spy(
                GeckoEngineSession(
                    runtime = runtime,
                    geckoSessionProvider = geckoSessionProvider,
                    privateMode = true,
                )
            )

        whenever(geckoSession.settings).thenReturn(geckoSetting)

        session.geckoSession = geckoSession

        val policy =
            TrackingProtectionPolicy.select(trackingCategories = arrayOf(TrackingCategory.SCRIPTS_AND_SUB_RESOURCES))
                .forPrivateSessionsOnly()

        session.updateContentBlocking(policy)

        verify(geckoSetting).useTrackingProtection = true
    }

    @Test
    fun `WHEN calling updateContentBlocking with a policy SCRIPTS_AND_SUB_RESOURCES useForRegularSessions being in privateMode THEN useTrackingProtection should be true`() {
        val geckoSetting = mock<GeckoSessionSettings>()
        val geckoSession = mock<GeckoSession>()

        val session =
            spy(
                GeckoEngineSession(
                    runtime = runtime,
                    geckoSessionProvider = geckoSessionProvider,
                    privateMode = false,
                )
            )

        whenever(geckoSession.settings).thenReturn(geckoSetting)

        session.geckoSession = geckoSession

        val policy =
            TrackingProtectionPolicy.select(trackingCategories = arrayOf(TrackingCategory.SCRIPTS_AND_SUB_RESOURCES))
                .forRegularSessionsOnly()

        session.updateContentBlocking(policy)

        verify(geckoSetting).useTrackingProtection = true
    }

    @Test
    fun `WHEN updating content blocking without a policy SCRIPTS_AND_SUB_RESOURCES for any browsing mode THEN useTrackingProtection should be false`() {
        val geckoSetting = mock<GeckoSessionSettings>()
        val geckoSession = mock<GeckoSession>()

        var session =
            spy(
                GeckoEngineSession(
                    runtime = runtime,
                    geckoSessionProvider = geckoSessionProvider,
                    privateMode = false,
                )
            )

        whenever(geckoSession.settings).thenReturn(geckoSetting)
        session.geckoSession = geckoSession

        val policy = TrackingProtectionPolicy.none()

        session.updateContentBlocking(policy)

        verify(geckoSetting).useTrackingProtection = false

        session =
            spy(
                GeckoEngineSession(
                    runtime = runtime,
                    geckoSessionProvider = geckoSessionProvider,
                    privateMode = true,
                )
            )

        whenever(geckoSession.settings).thenReturn(geckoSetting)
        session.geckoSession = geckoSession

        session.updateContentBlocking(policy)

        verify(geckoSetting, times(2)).useTrackingProtection = false
    }

    @Test
    fun `changes to updateTrackingProtection will be notified to all new observers`() {
        whenever(runtime.settings).thenReturn(mock())
        whenever(runtime.settings.contentBlocking).thenReturn(mock())
        val session = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)
        val observers = mutableListOf<EngineSession.Observer>()
        val policy = TrackingProtectionPolicy.strict()

        repeat(5) {
            observers.add(spy(object : EngineSession.Observer {}))
        }

        session.updateTrackingProtection(policy)

        observers.forEach { session.register(it) }
        shadowOf(getMainLooper()).idle()

        observers.forEach {
            verify(it).onTrackerBlockingEnabledChange(true)
        }

        observers.forEach { session.unregister(it) }
        shadowOf(getMainLooper()).idle()

        session.updateTrackingProtection(TrackingProtectionPolicy.none())

        observers.forEach { session.register(it) }
        shadowOf(getMainLooper()).idle()

        observers.forEach {
            verify(it).onTrackerBlockingEnabledChange(false)
        }
    }

    @Test
    fun safeBrowsingCategoriesAreAligned() {
        assertEquals(GeckoSafeBrowsing.NONE, SafeBrowsingPolicy.NONE.id)
        assertEquals(GeckoSafeBrowsing.MALWARE, SafeBrowsingPolicy.MALWARE.id)
        assertEquals(GeckoSafeBrowsing.UNWANTED, SafeBrowsingPolicy.UNWANTED.id)
        assertEquals(GeckoSafeBrowsing.HARMFUL, SafeBrowsingPolicy.HARMFUL.id)
        assertEquals(GeckoSafeBrowsing.PHISHING, SafeBrowsingPolicy.PHISHING.id)
        assertEquals(GeckoSafeBrowsing.HARMFULADDON, SafeBrowsingPolicy.HARMFULADDON.id)
        assertEquals(GeckoSafeBrowsing.DEFAULT, SafeBrowsingPolicy.RECOMMENDED.id)
    }

    @Test
    fun trackingProtectionCategoriesAreAligned() {
        assertEquals(GeckoAntiTracking.NONE, TrackingCategory.NONE.id)
        assertEquals(GeckoAntiTracking.AD, TrackingCategory.AD.id)
        assertEquals(GeckoAntiTracking.CONTENT, TrackingCategory.CONTENT.id)
        assertEquals(GeckoAntiTracking.SOCIAL, TrackingCategory.SOCIAL.id)
        assertEquals(GeckoAntiTracking.TEST, TrackingCategory.TEST.id)
        assertEquals(GeckoAntiTracking.CRYPTOMINING, TrackingCategory.CRYPTOMINING.id)
        assertEquals(GeckoAntiTracking.FINGERPRINTING, TrackingCategory.FINGERPRINTING.id)
        assertEquals(GeckoAntiTracking.STP, TrackingCategory.MOZILLA_SOCIAL.id)
        assertEquals(GeckoAntiTracking.EMAIL, TrackingCategory.EMAIL.id)

        assertEquals(GeckoCookieBehavior.ACCEPT_ALL, CookiePolicy.ACCEPT_ALL.id)
        assertEquals(
            GeckoCookieBehavior.ACCEPT_NON_TRACKERS,
            CookiePolicy.ACCEPT_NON_TRACKERS.id,
        )
        assertEquals(GeckoCookieBehavior.ACCEPT_NONE, CookiePolicy.ACCEPT_NONE.id)
        assertEquals(
            GeckoCookieBehavior.ACCEPT_FIRST_PARTY,
            CookiePolicy.ACCEPT_ONLY_FIRST_PARTY.id,
        )
        assertEquals(GeckoCookieBehavior.ACCEPT_VISITED, CookiePolicy.ACCEPT_VISITED.id)
    }

    @Test
    fun settingTestingMode() {
        GeckoEngineSession(
            runtime,
            geckoSessionProvider = geckoSessionProvider,
            defaultSettings = DefaultSettings(),
        )
        verify(geckoSession.settings).fullAccessibilityTree = false

        GeckoEngineSession(
            runtime,
            geckoSessionProvider = geckoSessionProvider,
            defaultSettings = DefaultSettings(testingModeEnabled = true),
        )
        verify(geckoSession.settings).fullAccessibilityTree = true
    }

    @Test
    fun settingUserAgent() {
        val engineSession = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)
        engineSession.settings.userAgentString

        verify(geckoSession.settings).userAgentOverride

        engineSession.settings.userAgentString = "test-ua"

        verify(geckoSession.settings).userAgentOverride = "test-ua"
    }

    @Test
    fun settingUserAgentDefault() {
        GeckoEngineSession(
            runtime,
            geckoSessionProvider = geckoSessionProvider,
            defaultSettings = DefaultSettings(userAgentString = "test-ua"),
        )

        verify(geckoSession.settings).userAgentOverride = "test-ua"
    }

    @Test
    fun settingSuspendMediaWhenInactive() {
        val engineSession = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)
        verify(geckoSession.settings, never()).suspendMediaWhenInactive = anyBoolean()

        assertFalse(engineSession.settings.suspendMediaWhenInactive)
        verify(geckoSession.settings).suspendMediaWhenInactive

        engineSession.settings.suspendMediaWhenInactive = true
        verify(geckoSession.settings).suspendMediaWhenInactive = true
    }

    @Test
    fun settingSuspendMediaWhenInactiveDefault() {
        GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)
        verify(geckoSession.settings, never()).suspendMediaWhenInactive = anyBoolean()

        GeckoEngineSession(
            runtime,
            geckoSessionProvider = geckoSessionProvider,
            defaultSettings = DefaultSettings(),
        )
        verify(geckoSession.settings).suspendMediaWhenInactive = false

        GeckoEngineSession(
            runtime,
            geckoSessionProvider = geckoSessionProvider,
            defaultSettings = DefaultSettings(suspendMediaWhenInactive = true),
        )
        verify(geckoSession.settings).suspendMediaWhenInactive = true
    }

    @Test
    fun settingClearColorDefault() {
        whenever(geckoSession.compositorController).thenReturn(mock())

        GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)

        verify(geckoSession.compositorController, never()).clearColor = anyInt()

        GeckoEngineSession(
            runtime,
            geckoSessionProvider = geckoSessionProvider,
            defaultSettings = DefaultSettings(),
        )
        verify(geckoSession.compositorController, never()).clearColor = anyInt()

        GeckoEngineSession(
            runtime,
            geckoSessionProvider = geckoSessionProvider,
            defaultSettings = DefaultSettings(clearColor = Color.BLUE),
        )
        verify(geckoSession.compositorController).clearColor = Color.BLUE
    }

    @Test
    fun `onPipModeChanged sets same enabled value`() {
        whenever(geckoSession.compositorController).thenReturn(mock())
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        engineSession.onPipModeChanged(true)
        verify(geckoSession.compositorController).onPipModeChanged(true)
        engineSession.onPipModeChanged(false)
        verify(geckoSession.compositorController).onPipModeChanged(false)
    }

    @Test
    fun unsupportedSettings() {
        val settings =
            GeckoEngineSession(
                    runtime,
                    geckoSessionProvider = geckoSessionProvider,
                )
                .settings

        expectException<UnsupportedSettingException> {
            settings.javascriptEnabled = true
        }

        expectException<UnsupportedSettingException> {
            settings.domStorageEnabled = false
        }

        expectException<UnsupportedSettingException> {
            settings.trackingProtectionPolicy = TrackingProtectionPolicy.strict()
        }
    }

    @Test
    fun settingInterceptorToProvideAlternativeContent() {
        var interceptorCalledWithUri: String? = null

        val interceptor =
            object : RequestInterceptor {
                override fun interceptsAppInitiatedRequests() = true

                override fun onLoadRequest(
                    engineSession: EngineSession,
                    uri: String,
                    lastUri: String?,
                    hasUserGesture: Boolean,
                    isSameDomain: Boolean,
                    isRedirect: Boolean,
                    isDirectNavigation: Boolean,
                    isSubframeRequest: Boolean,
                ): RequestInterceptor.InterceptionResponse? {
                    interceptorCalledWithUri = uri
                    return RequestInterceptor.InterceptionResponse.Content("<h1>Hello World</h1>")
                }
            }

        val defaultSettings = DefaultSettings(requestInterceptor = interceptor)
        GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider, defaultSettings = defaultSettings)
        captureDelegates()

        navigationDelegate.value.onLoadRequest(geckoSession, mockLoadRequest("sample:about"))

        assertEquals("sample:about", interceptorCalledWithUri)
        verify(geckoSession).load(GeckoSession.Loader().data("<h1>Hello World</h1>", "text/html"))
    }

    @Test
    fun settingInterceptorToProvideAlternativeUrl() {
        var interceptorCalledWithUri: String? = null

        val interceptor =
            object : RequestInterceptor {
                override fun interceptsAppInitiatedRequests() = true

                override fun onLoadRequest(
                    engineSession: EngineSession,
                    uri: String,
                    lastUri: String?,
                    hasUserGesture: Boolean,
                    isSameDomain: Boolean,
                    isRedirect: Boolean,
                    isDirectNavigation: Boolean,
                    isSubframeRequest: Boolean,
                ): RequestInterceptor.InterceptionResponse? {
                    interceptorCalledWithUri = uri
                    return RequestInterceptor.InterceptionResponse.Url("https://mozilla.org")
                }
            }

        val defaultSettings = DefaultSettings(requestInterceptor = interceptor)
        GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider, defaultSettings = defaultSettings)
        captureDelegates()

        navigationDelegate.value.onLoadRequest(geckoSession, mockLoadRequest("sample:about", "trigger:uri"))

        assertEquals("sample:about", interceptorCalledWithUri)
        verify(geckoSession)
            .load(
                GeckoSession.Loader().uri("https://mozilla.org").flags(EXTERNAL + LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE)
            )
    }

    @Test
    fun settingInterceptorCanIgnoreAppInitiatedRequests() {
        var interceptorCalled = false

        val interceptor =
            object : RequestInterceptor {
                override fun interceptsAppInitiatedRequests() = false

                override fun onLoadRequest(
                    engineSession: EngineSession,
                    uri: String,
                    lastUri: String?,
                    hasUserGesture: Boolean,
                    isSameDomain: Boolean,
                    isRedirect: Boolean,
                    isDirectNavigation: Boolean,
                    isSubframeRequest: Boolean,
                ): RequestInterceptor.InterceptionResponse? {
                    interceptorCalled = true
                    return RequestInterceptor.InterceptionResponse.Url("https://mozilla.org")
                }
            }

        val defaultSettings = DefaultSettings(requestInterceptor = interceptor)
        GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider, defaultSettings = defaultSettings)
        captureDelegates()

        navigationDelegate.value.onLoadRequest(geckoSession, mockLoadRequest("sample:about", isDirectNavigation = true))
        assertFalse(interceptorCalled)

        navigationDelegate.value.onLoadRequest(
            geckoSession,
            mockLoadRequest("sample:about", isDirectNavigation = false),
        )
        assertTrue(interceptorCalled)
    }

    @Test
    fun onLoadRequestWithoutInterceptor() {
        val defaultSettings = DefaultSettings()

        GeckoEngineSession(
            mock(),
            geckoSessionProvider = geckoSessionProvider,
            defaultSettings = defaultSettings,
        )

        captureDelegates()

        navigationDelegate.value.onLoadRequest(geckoSession, mockLoadRequest("sample:about"))

        verify(geckoSession, never()).load(any())
    }

    @Test
    fun onLoadRequestWithInterceptorThatDoesNotIntercept() {
        var interceptorCalledWithUri: String? = null

        val interceptor =
            object : RequestInterceptor {
                override fun interceptsAppInitiatedRequests() = true

                override fun onLoadRequest(
                    engineSession: EngineSession,
                    uri: String,
                    lastUri: String?,
                    hasUserGesture: Boolean,
                    isSameDomain: Boolean,
                    isRedirect: Boolean,
                    isDirectNavigation: Boolean,
                    isSubframeRequest: Boolean,
                ): RequestInterceptor.InterceptionResponse? {
                    interceptorCalledWithUri = uri
                    return null
                }
            }

        val defaultSettings = DefaultSettings(requestInterceptor = interceptor)

        GeckoEngineSession(
            mock(),
            geckoSessionProvider = geckoSessionProvider,
            defaultSettings = defaultSettings,
        )

        captureDelegates()

        navigationDelegate.value.onLoadRequest(geckoSession, mockLoadRequest("sample:about"))

        assertEquals("sample:about", interceptorCalledWithUri!!)
        verify(geckoSession, never()).load(any())
    }

    @Test
    fun onLoadErrorCallsInterceptorWithNull() {
        var interceptedUri: String? = null
        val requestInterceptor: RequestInterceptor = mock()
        var defaultSettings = DefaultSettings()
        var engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                defaultSettings = defaultSettings,
            )

        captureDelegates()

        // Interceptor is not called when there is none attached.
        var onLoadError =
            navigationDelegate.value.onLoadError(
                geckoSession,
                "",
                WebRequestError(
                    ERROR_CATEGORY_UNKNOWN,
                    ERROR_UNKNOWN,
                ),
            )
        verify(requestInterceptor, never()).onErrorRequest(engineSession, ErrorType.UNKNOWN, "")
        onLoadError!!.then { value: String? ->
            interceptedUri = value
            GeckoResult.fromValue(null)
        }
        assertNull(interceptedUri)

        // Interceptor is called correctly
        defaultSettings = DefaultSettings(requestInterceptor = requestInterceptor)
        geckoSession = mockGeckoSession()
        engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
                defaultSettings = defaultSettings,
            )

        captureDelegates()

        onLoadError =
            navigationDelegate.value.onLoadError(
                geckoSession,
                "",
                WebRequestError(
                    ERROR_CATEGORY_UNKNOWN,
                    ERROR_UNKNOWN,
                ),
            )

        verify(requestInterceptor).onErrorRequest(engineSession, ErrorType.UNKNOWN, "")
        onLoadError!!.then { value: String? ->
            interceptedUri = value
            GeckoResult.fromValue(null)
        }
        assertNull(interceptedUri)
    }

    @Test
    fun onLoadErrorCallsInterceptorWithErrorPage() {
        val requestInterceptor: RequestInterceptor =
            object : RequestInterceptor {
                override fun onErrorRequest(
                    session: EngineSession,
                    errorType: ErrorType,
                    uri: String?,
                ): RequestInterceptor.ErrorResponse? = RequestInterceptor.ErrorResponse("nonNullData")
            }

        val defaultSettings = DefaultSettings(requestInterceptor = requestInterceptor)
        GeckoEngineSession(
            mock(),
            geckoSessionProvider = geckoSessionProvider,
            defaultSettings = defaultSettings,
        )

        captureDelegates()

        val onLoadError =
            navigationDelegate.value.onLoadError(
                geckoSession,
                "about:failed",
                WebRequestError(
                    ERROR_CATEGORY_UNKNOWN,
                    ERROR_UNKNOWN,
                ),
            )

        onLoadError!!.then { value: String? ->
            GeckoResult.fromValue(value)
        }
    }

    @Test
    fun onLoadErrorCallsInterceptorWithInvalidUri() {
        val requestInterceptor: RequestInterceptor = mock()
        val defaultSettings = DefaultSettings(requestInterceptor = requestInterceptor)
        val engineSession = GeckoEngineSession(runtime, defaultSettings = defaultSettings)

        engineSession.geckoSession.navigationDelegate!!.onLoadError(
            engineSession.geckoSession,
            null,
            WebRequestError(ERROR_MALFORMED_URI, ERROR_CATEGORY_UNKNOWN),
        )
        verify(requestInterceptor).onErrorRequest(engineSession, ErrorType.ERROR_MALFORMED_URI, null)
    }

    @Test
    fun maybeRequestLocalNetworkPermissionAndRetryRequestsPermissionWhenSupported() {
        val engineSession =
            spy(
                GeckoEngineSession(
                    mock(),
                    geckoSessionProvider = geckoSessionProvider,
                )
            )
        doReturn(true).`when`(engineSession).isAtLeastCinnamonBun()

        val observedPermissionRequests = mutableListOf<PermissionRequest>()
        engineSession.register(
            object : EngineSession.Observer {
                override fun onAppPermissionRequest(permissionRequest: PermissionRequest) {
                    observedPermissionRequests.add(permissionRequest)
                }
            }
        )
        engineSession.maybeRequestLocalNetworkPermissionAndRetry(
            "https://local.device/",
            WebRequestError.ERROR_LOCAL_NETWORK_ACCESS_DENIED,
        )

        assertEquals(1, observedPermissionRequests.size)
        assertEquals(1, observedPermissionRequests[0].permissions.size)
    }

    @Test
    fun maybeRequestLocalNetworkPermissionAndRetryGrantRetriesToLoad() {
        val engineSession =
            spy(
                GeckoEngineSession(
                    mock(),
                    geckoSessionProvider = geckoSessionProvider,
                )
            )
        doReturn(true).`when`(engineSession).isAtLeastCinnamonBun()

        val observedPermissionRequests = mutableListOf<PermissionRequest>()
        engineSession.register(
            object : EngineSession.Observer {
                override fun onAppPermissionRequest(permissionRequest: PermissionRequest) {
                    observedPermissionRequests.add(permissionRequest)
                }
            }
        )

        val uri = "https://local.device/"
        engineSession.maybeRequestLocalNetworkPermissionAndRetry(
            uri,
            WebRequestError.ERROR_LOCAL_NETWORK_ACCESS_DENIED,
        )

        observedPermissionRequests[0].grant()

        verify(geckoSession).loadUri(uri)
    }

    @Test
    fun maybeRequestLocalNetworkPermissionAndRetryDoesNotRequestPermissionWithNullUri() {
        val engineSession =
            spy(
                GeckoEngineSession(
                    mock(),
                    geckoSessionProvider = geckoSessionProvider,
                )
            )
        doReturn(true).`when`(engineSession).isAtLeastCinnamonBun()

        val observedPermissionRequests = mutableListOf<PermissionRequest>()
        engineSession.register(
            object : EngineSession.Observer {
                override fun onAppPermissionRequest(permissionRequest: PermissionRequest) {
                    observedPermissionRequests.add(permissionRequest)
                }
            }
        )
        engineSession.maybeRequestLocalNetworkPermissionAndRetry(
            null,
            WebRequestError.ERROR_LOCAL_NETWORK_ACCESS_DENIED,
        )

        assertTrue(observedPermissionRequests.isEmpty())
    }

    @Test
    fun maybeRequestLocalNetworkPermissionAndRetryDoesNotRequestPermissionWhenUnsupported() {
        val engineSession =
            spy(
                GeckoEngineSession(
                    mock(),
                    geckoSessionProvider = geckoSessionProvider,
                )
            )
        doReturn(false).`when`(engineSession).isAtLeastCinnamonBun()

        val observedPermissionRequests = mutableListOf<PermissionRequest>()
        engineSession.register(
            object : EngineSession.Observer {
                override fun onAppPermissionRequest(permissionRequest: PermissionRequest) {
                    observedPermissionRequests.add(permissionRequest)
                }
            }
        )
        engineSession.maybeRequestLocalNetworkPermissionAndRetry(
            "https://local.device/",
            WebRequestError.ERROR_LOCAL_NETWORK_ACCESS_DENIED,
        )

        assertTrue(observedPermissionRequests.isEmpty())
    }

    @Test
    fun maybeRequestLocalNetworkPermissionAndRetryDoesNotRequestPermissionForOtherErrors() {
        val engineSession =
            spy(
                GeckoEngineSession(
                    mock(),
                    geckoSessionProvider = geckoSessionProvider,
                )
            )
        doReturn(true).`when`(engineSession).isAtLeastCinnamonBun()

        val observedPermissionRequests = mutableListOf<PermissionRequest>()
        engineSession.register(
            object : EngineSession.Observer {
                override fun onAppPermissionRequest(permissionRequest: PermissionRequest) {
                    observedPermissionRequests.add(permissionRequest)
                }
            }
        )

        engineSession.maybeRequestLocalNetworkPermissionAndRetry(
            "https://example.com/",
            ERROR_UNKNOWN,
        )

        assertTrue(observedPermissionRequests.isEmpty())
    }

    @Test
    fun geckoErrorMappingToErrorType() {
        assertEquals(
            ErrorType.ERROR_SECURITY_SSL,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_SECURITY_SSL),
        )
        assertEquals(
            ErrorType.ERROR_SECURITY_BAD_CERT,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_SECURITY_BAD_CERT),
        )
        assertEquals(
            ErrorType.ERROR_NET_INTERRUPT,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_NET_INTERRUPT),
        )
        assertEquals(
            ErrorType.ERROR_NET_TIMEOUT,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_NET_TIMEOUT),
        )
        assertEquals(
            ErrorType.ERROR_NET_RESET,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_NET_RESET),
        )
        assertEquals(
            ErrorType.ERROR_CONNECTION_REFUSED,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_CONNECTION_REFUSED),
        )
        assertEquals(
            ErrorType.ERROR_LOCAL_NETWORK_ACCESS_DENIED,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_LOCAL_NETWORK_ACCESS_DENIED),
        )
        assertEquals(
            ErrorType.ERROR_UNKNOWN_SOCKET_TYPE,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_UNKNOWN_SOCKET_TYPE),
        )
        assertEquals(
            ErrorType.ERROR_REDIRECT_LOOP,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_REDIRECT_LOOP),
        )
        assertEquals(
            ErrorType.ERROR_OFFLINE,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_OFFLINE),
        )
        assertEquals(
            ErrorType.ERROR_PORT_BLOCKED,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_PORT_BLOCKED),
        )
        assertEquals(
            ErrorType.ERROR_UNSAFE_CONTENT_TYPE,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_UNSAFE_CONTENT_TYPE),
        )
        assertEquals(
            ErrorType.ERROR_CORRUPTED_CONTENT,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_CORRUPTED_CONTENT),
        )
        assertEquals(
            ErrorType.ERROR_CONTENT_CRASHED,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_CONTENT_CRASHED),
        )
        assertEquals(
            ErrorType.ERROR_INVALID_CONTENT_ENCODING,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_INVALID_CONTENT_ENCODING),
        )
        assertEquals(
            ErrorType.ERROR_UNKNOWN_HOST,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_UNKNOWN_HOST),
        )
        assertEquals(
            ErrorType.ERROR_MALFORMED_URI,
            GeckoEngineSession.geckoErrorToErrorType(ERROR_MALFORMED_URI),
        )
        assertEquals(
            ErrorType.ERROR_UNKNOWN_PROTOCOL,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_UNKNOWN_PROTOCOL),
        )
        assertEquals(
            ErrorType.ERROR_FILE_NOT_FOUND,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_FILE_NOT_FOUND),
        )
        assertEquals(
            ErrorType.ERROR_FILE_ACCESS_DENIED,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_FILE_ACCESS_DENIED),
        )
        assertEquals(
            ErrorType.ERROR_PROXY_CONNECTION_REFUSED,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_PROXY_CONNECTION_REFUSED),
        )
        assertEquals(
            ErrorType.ERROR_UNKNOWN_PROXY_HOST,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_UNKNOWN_PROXY_HOST),
        )
        assertEquals(
            ErrorType.ERROR_SAFEBROWSING_MALWARE_URI,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_SAFEBROWSING_MALWARE_URI),
        )
        assertEquals(
            ErrorType.ERROR_SAFEBROWSING_HARMFUL_URI,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_SAFEBROWSING_HARMFUL_URI),
        )
        assertEquals(
            ErrorType.ERROR_SAFEBROWSING_PHISHING_URI,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_SAFEBROWSING_PHISHING_URI),
        )
        assertEquals(
            ErrorType.ERROR_SAFEBROWSING_UNWANTED_URI,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_SAFEBROWSING_UNWANTED_URI),
        )
        assertEquals(
            ErrorType.ERROR_HARMFULADDON_URI,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_HARMFULADDON_URI),
        )
        assertEquals(
            ErrorType.UNKNOWN,
            GeckoEngineSession.geckoErrorToErrorType(-500),
        )
        assertEquals(
            ErrorType.ERROR_HTTPS_ONLY,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_HTTPS_ONLY),
        )
        assertEquals(
            ErrorType.ERROR_BAD_HSTS_CERT,
            GeckoEngineSession.geckoErrorToErrorType(WebRequestError.ERROR_BAD_HSTS_CERT),
        )
    }

    @Test
    fun defaultSettings() {
        val runtime = mock<GeckoRuntime>()
        whenever(runtime.settings).thenReturn(mock())

        val defaultSettings = DefaultSettings(trackingProtectionPolicy = TrackingProtectionPolicy.strict())

        GeckoEngineSession(
            runtime,
            geckoSessionProvider = geckoSessionProvider,
            privateMode = false,
            defaultSettings = defaultSettings,
        )

        assertFalse(geckoSession.settings.usePrivateMode)
        verify(geckoSession.settings).useTrackingProtection = true
    }

    @Test
    fun `WHEN TrackingCategory do not includes content then useTrackingProtection must be set to false`() {
        val defaultSettings = DefaultSettings(trackingProtectionPolicy = TrackingProtectionPolicy.recommended())

        GeckoEngineSession(
            runtime,
            geckoSessionProvider = geckoSessionProvider,
            privateMode = false,
            defaultSettings = defaultSettings,
        )

        verify(geckoSession.settings).useTrackingProtection = false
    }

    @Test
    fun contentDelegate() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        val delegate = engineSession.createContentDelegate()

        var observedChanged = false
        engineSession.register(
            object : EngineSession.Observer {
                override fun onLongPress(hitResult: HitResult) {
                    observedChanged = true
                }
            }
        )

        class MockContextElement(
            baseUri: String?,
            linkUri: String?,
            title: String?,
            altText: String?,
            typeStr: String,
            srcUri: String?,
        ) :
            GeckoSession.ContentDelegate.ContextElement(
                baseUri,
                linkUri,
                title,
                altText,
                typeStr,
                srcUri,
                // linkText =
                null,
            )

        delegate.onContextMenu(
            geckoSession,
            0,
            0,
            MockContextElement(null, null, "title", "alt", "HTMLAudioElement", "file.mp3"),
        )
        assertTrue(observedChanged)

        observedChanged = false
        delegate.onContextMenu(
            geckoSession,
            0,
            0,
            MockContextElement(null, null, "title", "alt", "HTMLAudioElement", null),
        )
        assertFalse(observedChanged)

        observedChanged = false
        delegate.onContextMenu(
            geckoSession,
            0,
            0,
            MockContextElement(null, null, "title", "alt", "foobar", null),
        )
        assertFalse(observedChanged)
    }

    @Test
    fun handleLongClick() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        var result = engineSession.handleLongClick("file.mp3", TYPE_AUDIO)
        assertNotNull(result)
        assertTrue(result is HitResult.AUDIO && result.src == "file.mp3")

        result = engineSession.handleLongClick("file.mp4", TYPE_VIDEO)
        assertNotNull(result)
        assertTrue(result is HitResult.VIDEO && result.src == "file.mp4")

        result = engineSession.handleLongClick("file.png", TYPE_IMAGE)
        assertNotNull(result)
        assertTrue(result is HitResult.IMAGE && result.src == "file.png")

        result = engineSession.handleLongClick("file.png", TYPE_IMAGE, "https://mozilla.org")
        assertNotNull(result)
        assertTrue(result is HitResult.IMAGE_SRC && result.src == "file.png" && result.uri == "https://mozilla.org")

        result = engineSession.handleLongClick(null, TYPE_IMAGE)
        assertNotNull(result)
        assertTrue(result is HitResult.UNKNOWN && result.src == "")

        result = engineSession.handleLongClick("tel:+1234567890", TYPE_NONE)
        assertNotNull(result)
        assertTrue(result is HitResult.PHONE && result.src == "tel:+1234567890")

        result = engineSession.handleLongClick("geo:1,-1", TYPE_NONE)
        assertNotNull(result)
        assertTrue(result is HitResult.GEO && result.src == "geo:1,-1")

        result = engineSession.handleLongClick("mailto:asa@mozilla.com", TYPE_NONE)
        assertNotNull(result)
        assertTrue(result is HitResult.EMAIL && result.src == "mailto:asa@mozilla.com")

        result = engineSession.handleLongClick(null, TYPE_NONE, "https://mozilla.org")
        assertNotNull(result)
        assertTrue(result is HitResult.UNKNOWN && result.src == "https://mozilla.org")

        result = engineSession.handleLongClick("data://foobar", TYPE_NONE, "https://mozilla.org")
        assertNotNull(result)
        assertTrue(result is HitResult.UNKNOWN && result.src == "data://foobar")

        result = engineSession.handleLongClick(null, TYPE_NONE, null)
        assertNull(result)

        result =
            engineSession.handleLongClick(
                elementSrc = null,
                elementType = TYPE_NONE,
                uri = "https://mozilla.org",
                linkText = "Mozilla",
            )
        assertTrue(result is HitResult.UNKNOWN && result.src == "https://mozilla.org")
        assertTrue(result is HitResult.UNKNOWN && result.linkText == "Mozilla")
    }

    @Test
    fun setDesktopMode() {
        val engineSession = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)

        var desktopModeToggled = false
        engineSession.register(
            object : EngineSession.Observer {
                override fun onDesktopModeChange(enabled: Boolean) {
                    desktopModeToggled = true
                }
            }
        )
        engineSession.toggleDesktopMode(true)
        assertTrue(desktopModeToggled)

        desktopModeToggled = false
        whenever(geckoSession.settings.userAgentMode).thenReturn(GeckoSessionSettings.USER_AGENT_MODE_DESKTOP)
        whenever(geckoSession.settings.viewportMode).thenReturn(GeckoSessionSettings.VIEWPORT_MODE_DESKTOP)

        engineSession.toggleDesktopMode(true)
        assertFalse(desktopModeToggled)

        engineSession.toggleDesktopMode(true)
        assertFalse(desktopModeToggled)

        engineSession.toggleDesktopMode(false)
        assertTrue(desktopModeToggled)
    }

    @Test
    fun `toggleDesktopMode should reload a non-mobile url when set to desktop mode`() {
        val mobileUrl = "https://m.example.com"
        val nonMobileUrl = "https://example.com"
        val engineSession = spy(GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider))
        engineSession.currentUrl = mobileUrl
        engineSession.pageLoadingUrl = "https://before-redirection.com"

        engineSession.toggleDesktopMode(true, reload = true)
        verify(engineSession, atLeastOnce())
            .loadUrl(nonMobileUrl, null, LoadUrlFlags.select(LoadUrlFlags.LOAD_FLAGS_REPLACE_HISTORY), null)

        engineSession.toggleDesktopMode(false, reload = true)
        verify(engineSession, atLeastOnce()).reload()
    }

    @Test
    fun `toggleDesktopMode should reload a pageLoadingUrl when set to desktop mode if it is different from currentUrl`() {
        val engineSession = spy(GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider))
        engineSession.currentUrl = "https://redirected.com"
        engineSession.pageLoadingUrl = "https://example.com"

        engineSession.toggleDesktopMode(true, reload = true)
        verify(engineSession, atLeastOnce())
            .loadUrl("https://example.com", null, LoadUrlFlags.select(LoadUrlFlags.LOAD_FLAGS_REPLACE_HISTORY), null)

        engineSession.toggleDesktopMode(false, reload = true)
        verify(engineSession, atLeastOnce()).reload()
    }

    @Test
    fun `toggleDesktopMode should reload pageLoadingUrl after redirect sequence`() {
        val initialUrl = "https://example.com"
        val redirectedUrl = "https://redirected.com"
        val engineSession = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)

        captureDelegates()

        progressDelegate.value.onPageStart(geckoSession, initialUrl)
        navigationDelegate.value.onLocationChange(geckoSession, redirectedUrl, emptyList(), false)

        engineSession.toggleDesktopMode(true, reload = true)
        verify(geckoSession).load(GeckoSession.Loader().uri(initialUrl).flags(LoadUrlFlags.LOAD_FLAGS_REPLACE_HISTORY))
    }

    @Test
    fun `toggleDesktopMode should call reload when redirect location change has user gesture`() {
        val initialUrl = "https://example.com"
        val redirectedUrl = "https://redirected.com"
        val engineSession = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)

        captureDelegates()

        progressDelegate.value.onPageStart(geckoSession, initialUrl)
        navigationDelegate.value.onLocationChange(geckoSession, redirectedUrl, emptyList(), true)

        engineSession.toggleDesktopMode(true, reload = true)
        verify(geckoSession).reload(LoadUrlFlags.NONE)
        verify(geckoSession, never()).load(any())
    }

    @Test
    fun `checkForPdfViewer should correctly process a GV response`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        var onResultCalled = false
        var onExceptionCalled = false

        val ruleResult = GeckoResult<Boolean>()
        whenever(geckoSession.isPdfJs).thenReturn(ruleResult)

        engineSession.checkForPdfViewer(
            onResult = { onResultCalled = true },
            onException = { onExceptionCalled = true },
        )

        ruleResult.complete(true)
        shadowOf(getMainLooper()).idle()

        assertTrue(onResultCalled)
        assertFalse(onExceptionCalled)
    }

    @Test
    fun `getBrokenSiteReport should correctly process a GV response`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        var onResultCalled = false
        var onExceptionCalled = false

        val ruleResult = GeckoResult<JSONObject>()
        whenever(geckoSession.brokenSiteReport).thenReturn(ruleResult)

        engineSession.getBrokenSiteReport(
            onResult = { onResultCalled = true },
            onException = { onExceptionCalled = true },
        )

        val json =
            JSONObject().apply {
                put("devicePixelRatio", 2.5)
                put(
                    "antitracking",
                    JSONObject().apply {
                        put("hasTrackingContentBlocked", false)
                    },
                )
            }
        ruleResult.complete(json)
        shadowOf(getMainLooper()).idle()

        assertTrue(onResultCalled)
        assertFalse(onExceptionCalled)
    }

    @Test
    fun `getWebCompatInfo should correctly process a GV response`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        var onResultCalled = false
        var onExceptionCalled = false

        val ruleResult = GeckoResult<JSONObject>()
        whenever(geckoSession.webCompatInfo).thenReturn(ruleResult)

        engineSession.getWebCompatInfo(
            onResult = { onResultCalled = true },
            onException = { onExceptionCalled = true },
        )

        val json =
            JSONObject().apply {
                put("devicePixelRatio", 2.5)
                put(
                    "antitracking",
                    JSONObject().apply {
                        put("hasTrackingContentBlocked", false)
                    },
                )
            }
        ruleResult.complete(json)
        shadowOf(getMainLooper()).idle()

        assertTrue(onResultCalled)
        assertFalse(onExceptionCalled)
    }

    @Test
    fun `sendMoreWebCompatInfo should correctly process a GV response`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        var onResultCalled = false
        var onExceptionCalled = false

        val testInfo =
            JSONObject().apply {
                put("reason", "test-reason")
                put("description", "test-description")
                put("endpointUrl", "https://webcompat.com/issues/new")
                put("reportUrl", "https://example.com")
            }

        val ruleResult = GeckoResult<Void>()
        whenever(geckoSession.sendMoreWebCompatInfo(any())).thenReturn(ruleResult)

        engineSession.sendMoreWebCompatInfo(
            info = testInfo,
            onResult = {
                onResultCalled = true
            },
            onException = {
                onExceptionCalled = true
            },
        )

        ruleResult.complete(null)
        shadowOf(getMainLooper()).idle()

        assertTrue(onResultCalled)
        assertFalse(onExceptionCalled)
    }

    @Test
    fun `WHEN session requestTranslate is successful THEN notify of completion`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        val mockedGeckoController: TranslationsController.SessionTranslation = mock()

        val geckoResult = GeckoResult<Void>()
        val fromLanguage = "en"
        val toLanguage = "es"
        val options = null

        geckoResult.complete(null)
        whenever(geckoSession.sessionTranslation).thenReturn(mockedGeckoController)
        whenever(geckoSession.sessionTranslation!!.translate(fromLanguage, toLanguage, options)).thenReturn(geckoResult)

        engineSession.register(
            object : EngineSession.Observer {
                override fun onTranslateComplete(operation: TranslationOperation) {
                    assert(true) { "We should notify of a successful translation." }
                }

                override fun onTranslateException(
                    operation: TranslationOperation,
                    translationError: TranslationError,
                ) {
                    assert(false) { "We should not notify of a failure." }
                }
            }
        )

        engineSession.requestTranslate(
            fromLanguage = fromLanguage,
            toLanguage = toLanguage,
            options = options,
        )

        shadowOf(getMainLooper()).idle()
    }

    @Test
    fun `WHEN session requestTranslationRestore is successful THEN notify of completion`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        val mockedGeckoController: TranslationsController.SessionTranslation = mock()

        val geckoResult = GeckoResult<Void>()
        geckoResult.complete(null)
        whenever(geckoSession.sessionTranslation).thenReturn(mockedGeckoController)
        whenever(geckoSession.sessionTranslation!!.restoreOriginalPage()).thenReturn(geckoResult)

        engineSession.register(
            object : EngineSession.Observer {
                override fun onTranslateComplete(operation: TranslationOperation) {
                    assert(true) { "We should notify of a successful translation." }
                }

                override fun onTranslateException(
                    operation: TranslationOperation,
                    translationError: TranslationError,
                ) {
                    assert(false) { "We should not notify of a failure." }
                }
            }
        )

        engineSession.requestTranslationRestore()

        shadowOf(getMainLooper()).idle()
    }

    @Test
    fun `WHEN session requestTranslate is unsuccessful THEN notify of failure`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        val mockedGeckoController: TranslationsController.SessionTranslation = mock()

        val geckoResult = GeckoResult<Void>()
        val fromLanguage = "en"
        val toLanguage = "es"
        val options = null

        geckoResult.completeExceptionally(Exception())
        whenever(geckoSession.sessionTranslation).thenReturn(mockedGeckoController)
        whenever(geckoSession.sessionTranslation!!.translate(fromLanguage, toLanguage, options)).thenReturn(geckoResult)

        engineSession.register(
            object : EngineSession.Observer {
                override fun onTranslateComplete(operation: TranslationOperation) {
                    assert(false) { "We should not notify of a successful translation." }
                }

                override fun onTranslateException(
                    operation: TranslationOperation,
                    translationError: TranslationError,
                ) {
                    assert(true) { "We should notify of a failure." }
                }
            }
        )

        engineSession.requestTranslate(
            fromLanguage = fromLanguage,
            toLanguage = toLanguage,
            options = options,
        )

        shadowOf(getMainLooper()).idle()
    }

    @Test
    fun `WHEN session requestTranslationRestore is unsuccessful THEN notify of failure`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        val mockedGeckoController: TranslationsController.SessionTranslation = mock()

        val geckoResult = GeckoResult<Void>()
        geckoResult.completeExceptionally(Exception())
        whenever(geckoSession.sessionTranslation).thenReturn(mockedGeckoController)
        whenever(geckoSession.sessionTranslation!!.restoreOriginalPage()).thenReturn(geckoResult)

        engineSession.register(
            object : EngineSession.Observer {
                override fun onTranslateComplete(operation: TranslationOperation) {
                    assert(false) { "We should not notify of a successful translation." }
                }

                override fun onTranslateException(
                    operation: TranslationOperation,
                    translationError: TranslationError,
                ) {
                    assert(true) { "We should notify of a failure." }
                }
            }
        )

        engineSession.requestTranslationRestore()

        shadowOf(getMainLooper()).idle()
    }

    @Test
    fun `WHEN session getNeverTranslateSiteSetting is successful THEN onResult should be called`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        val mockedGeckoController: TranslationsController.SessionTranslation = mock()

        val geckoResult = GeckoResult<Boolean>()

        whenever(geckoSession.sessionTranslation).thenReturn(mockedGeckoController)
        whenever(geckoSession.sessionTranslation!!.neverTranslateSiteSetting).thenReturn(geckoResult)

        var onResultCalled = false
        var onExceptionCalled = false

        engineSession.getNeverTranslateSiteSetting(
            onResult = {
                onResultCalled = true
                assertTrue(it)
            },
            onException = { onExceptionCalled = true },
        )

        geckoResult.complete(true)
        shadowOf(getMainLooper()).idle()

        assertTrue(onResultCalled)
        assertFalse(onExceptionCalled)
    }

    @Test
    fun `WHEN session getNeverTranslateSiteSetting has an error THEN onException should be called`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        val mockedGeckoController: TranslationsController.SessionTranslation = mock()

        val geckoResult = GeckoResult<Boolean>()

        whenever(geckoSession.sessionTranslation).thenReturn(mockedGeckoController)
        whenever(geckoSession.sessionTranslation!!.neverTranslateSiteSetting).thenReturn(geckoResult)

        var onResultCalled = false
        var onExceptionCalled = false

        engineSession.getNeverTranslateSiteSetting(
            onResult = { onResultCalled = true },
            onException = { onExceptionCalled = true },
        )

        geckoResult.completeExceptionally(Exception())
        shadowOf(getMainLooper()).idle()

        assertFalse(onResultCalled)
        assertTrue(onExceptionCalled)
    }

    @Test
    fun `WHEN addSignatureToPdf is successful THEN onResult should be called`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        val mockedGeckoEditor: PdfViewerController.SessionEditor = mock()

        val geckoResult = GeckoResult<Void>()

        whenever(geckoSession.pdfViewerEditor).thenReturn(mockedGeckoEditor)
        whenever(mockedGeckoEditor.addSignature(any())).thenReturn(geckoResult)

        var onResultCalled = false
        var onExceptionCalled = false

        engineSession.addSignatureToPdf(
            "Test User",
            onResult = { onResultCalled = true },
            onException = { onExceptionCalled = true },
        )

        geckoResult.complete(null)
        shadowOf(getMainLooper()).idle()

        assertTrue(onResultCalled)
        assertFalse(onExceptionCalled)
    }

    @Test
    fun `WHEN addSignatureToPdf has an error THEN onException should be called`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        val mockedGeckoEditor: PdfViewerController.SessionEditor = mock()

        val geckoResult = GeckoResult<Void>()

        whenever(geckoSession.pdfViewerEditor).thenReturn(mockedGeckoEditor)
        whenever(mockedGeckoEditor.addSignature(any())).thenReturn(geckoResult)

        var onResultCalled = false
        var onExceptionCalled = false

        engineSession.addSignatureToPdf(
            "Test User",
            onResult = { onResultCalled = true },
            onException = { onExceptionCalled = true },
        )

        geckoResult.completeExceptionally(Exception())
        shadowOf(getMainLooper()).idle()

        assertFalse(onResultCalled)
        assertTrue(onExceptionCalled)
    }

    @Test
    fun `WHEN session setNeverTranslateSiteSetting is successful THEN onResult should be called`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        val mockedGeckoController: TranslationsController.SessionTranslation = mock()

        val geckoResult = GeckoResult<Void>()

        whenever(geckoSession.sessionTranslation).thenReturn(mockedGeckoController)
        whenever(geckoSession.sessionTranslation!!.setNeverTranslateSiteSetting(any())).thenReturn(geckoResult)

        var onResultCalled = false
        var onExceptionCalled = false

        engineSession.setNeverTranslateSiteSetting(
            true,
            onResult = { onResultCalled = true },
            onException = { onExceptionCalled = true },
        )

        geckoResult.complete(null)
        shadowOf(getMainLooper()).idle()

        assertTrue(onResultCalled)
        assertFalse(onExceptionCalled)
    }

    @Test
    fun `WHEN session setNeverTranslateSiteSetting has an error THEN onException should be called`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        val mockedGeckoController: TranslationsController.SessionTranslation = mock()

        val geckoResult = GeckoResult<Void>()

        whenever(geckoSession.sessionTranslation).thenReturn(mockedGeckoController)
        whenever(geckoSession.sessionTranslation!!.setNeverTranslateSiteSetting(any())).thenReturn(geckoResult)

        var onResultCalled = false
        var onExceptionCalled = false

        engineSession.setNeverTranslateSiteSetting(
            true,
            onResult = { onResultCalled = true },
            onException = { onExceptionCalled = true },
        )

        geckoResult.completeExceptionally(Exception())
        shadowOf(getMainLooper()).idle()

        assertFalse(onResultCalled)
        assertTrue(onExceptionCalled)
    }

    @Test
    fun `WHEN mapping a Gecko TranslationsException THEN it maps as expected to a TranslationError`() {
        // Specifically defined unknown error thrown by the translations engine
        val geckoUnknownError = TranslationsException(TranslationsException.ERROR_UNKNOWN)
        val unknownError = geckoUnknownError.intoTranslationError()
        assertIs<TranslationError.UnknownError>(unknownError)
        assertEquals(
            unknownError.cause,
            geckoUnknownError,
        )
        assertEquals(
            (unknownError as Throwable).cause,
            geckoUnknownError,
        )
        assertEquals(
            unknownError.errorName,
            "unknown",
        )
        assertEquals(
            unknownError.displayError,
            false,
        )

        // Something really unexpected was thrown
        val unexpectedUnknownError = Exception("Something very unexpected")
        val unexpectedUnknown = unexpectedUnknownError.intoTranslationError()
        assertIs<TranslationError.UnknownError>(unexpectedUnknown)
        assertEquals(
            unexpectedUnknown.cause,
            unexpectedUnknownError,
        )
        assertEquals(
            unexpectedUnknown.errorName,
            "unknown",
        )
        assertEquals(
            unexpectedUnknown.displayError,
            false,
        )

        // For manual use as a guard for when the API returns a null value and it shouldn't be
        // possible
        val unexpectedNullError = TranslationError.UnexpectedNull()
        assertEquals(
            unexpectedNullError.errorName,
            "unexpected-null",
        )
        assertEquals(
            unexpectedNullError.displayError,
            false,
        )

        // For manual use as a guard for when the engine is missing a session coordinator
        val missingCoordinator = TranslationError.MissingSessionCoordinator()
        assertEquals(
            missingCoordinator.errorName,
            "missing-session-coordinator",
        )
        assertEquals(
            missingCoordinator.displayError,
            false,
        )

        val notSupported =
            TranslationsException(TranslationsException.ERROR_ENGINE_NOT_SUPPORTED).intoTranslationError()
        assertIs<TranslationError.EngineNotSupportedError>(notSupported)
        assertEquals(
            notSupported.errorName,
            "engine-not-supported",
        )
        assertEquals(
            notSupported.displayError,
            false,
        )

        val couldNotTranslate =
            TranslationsException(TranslationsException.ERROR_COULD_NOT_TRANSLATE).intoTranslationError()
        assertIs<TranslationError.CouldNotTranslateError>(couldNotTranslate)
        assertEquals(
            couldNotTranslate.errorName,
            "could-not-translate",
        )
        assertEquals(
            couldNotTranslate.displayError,
            true,
        )

        val couldNotRestore =
            TranslationsException(TranslationsException.ERROR_COULD_NOT_RESTORE).intoTranslationError()
        assertIs<TranslationError.CouldNotRestoreError>(couldNotRestore)
        assertEquals(
            couldNotRestore.errorName,
            "could-not-restore",
        )
        assertEquals(
            couldNotRestore.displayError,
            false,
        )

        val couldNotLoadLanguages =
            TranslationsException(TranslationsException.ERROR_COULD_NOT_LOAD_LANGUAGES).intoTranslationError()
        assertIs<TranslationError.CouldNotLoadLanguagesError>(couldNotLoadLanguages)
        assertEquals(
            couldNotLoadLanguages.errorName,
            "could-not-load-languages",
        )
        assertEquals(
            couldNotLoadLanguages.displayError,
            true,
        )

        val languageNotSupported =
            TranslationsException(TranslationsException.ERROR_LANGUAGE_NOT_SUPPORTED).intoTranslationError()
        assertIs<TranslationError.LanguageNotSupportedError>(languageNotSupported)
        assertEquals(
            languageNotSupported.errorName,
            "language-not-supported",
        )
        assertEquals(
            languageNotSupported.displayError,
            true,
        )

        val couldNotRetrieve =
            TranslationsException(TranslationsException.ERROR_MODEL_COULD_NOT_RETRIEVE).intoTranslationError()
        assertIs<TranslationError.ModelCouldNotRetrieveError>(couldNotRetrieve)
        assertEquals(
            couldNotRetrieve.errorName,
            "model-could-not-retrieve",
        )
        assertEquals(
            couldNotRetrieve.displayError,
            false,
        )

        val couldNotDelete =
            TranslationsException(TranslationsException.ERROR_MODEL_COULD_NOT_DELETE).intoTranslationError()
        assertIs<TranslationError.ModelCouldNotDeleteError>(couldNotDelete)
        assertEquals(
            couldNotDelete.errorName,
            "model-could-not-delete",
        )
        assertEquals(
            couldNotDelete.displayError,
            false,
        )

        val couldNotDownload =
            TranslationsException(TranslationsException.ERROR_MODEL_COULD_NOT_DOWNLOAD).intoTranslationError()
        assertIs<TranslationError.ModelCouldNotDownloadError>(couldNotDownload)
        assertEquals(
            couldNotDownload.errorName,
            "model-could-not-download",
        )
        assertEquals(
            couldNotDelete.displayError,
            false,
        )

        val languageRequired =
            TranslationsException(TranslationsException.ERROR_MODEL_LANGUAGE_REQUIRED).intoTranslationError()
        assertIs<TranslationError.ModelLanguageRequiredError>(languageRequired)
        assertEquals(
            languageRequired.errorName,
            "model-language-required",
        )
        assertEquals(
            languageRequired.displayError,
            false,
        )

        val downloadRequired =
            TranslationsException(TranslationsException.ERROR_MODEL_DOWNLOAD_REQUIRED).intoTranslationError()
        assertIs<TranslationError.ModelDownloadRequiredError>(downloadRequired)
        assertEquals(
            downloadRequired.errorName,
            "model-download-required",
        )
        assertEquals(
            downloadRequired.displayError,
            false,
        )
    }

    @Test
    fun containsFormData() {
        val engineSession = GeckoEngineSession(runtime = mock(), geckoSessionProvider = geckoSessionProvider)
        var formData = false
        engineSession.register(
            object : EngineSession.Observer {
                override fun onCheckForFormData(containsFormData: Boolean, adjustPriority: Boolean) {
                    formData = true
                }
            }
        )

        whenever(geckoSession.containsFormData())
            .thenReturn(GeckoResult.fromValue(null))
            .thenReturn(GeckoResult.fromException(IllegalStateException()))
        engineSession.checkForFormData()
        assertEquals(false, formData)
    }

    @Test
    fun checkForMobileSite() {
        val mUrl = "https://m.example.com"
        val mobileUrl = "https://mobile.example.com"
        val nonAuthorityUrl = "mobile.example.com"
        val unrecognizedMobilePrefixUrl = "https://phone.example.com"
        val nonMobileUrl = "https://example.com"

        val engineSession = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)

        assertNull(engineSession.checkForMobileSite(nonAuthorityUrl))
        assertNull(engineSession.checkForMobileSite(unrecognizedMobilePrefixUrl))
        assertEquals(nonMobileUrl, engineSession.checkForMobileSite(mUrl))
        assertEquals(nonMobileUrl, engineSession.checkForMobileSite(mobileUrl))
    }

    @Test
    fun findAll() {
        val finderResult = mock<GeckoSession.FinderResult>()
        val sessionFinder = mock<SessionFinder>()
        whenever(sessionFinder.find("mozilla", 0)).thenReturn(GeckoResult.fromValue(finderResult))

        whenever(geckoSession.finder).thenReturn(sessionFinder)

        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        var findObserved: String? = null
        var findResultObserved = false
        engineSession.register(
            object : EngineSession.Observer {
                override fun onFind(text: String) {
                    findObserved = text
                }

                override fun onFindResult(activeMatchOrdinal: Int, numberOfMatches: Int, isDoneCounting: Boolean) {
                    assertEquals(0, activeMatchOrdinal)
                    assertEquals(0, numberOfMatches)
                    assertTrue(isDoneCounting)
                    findResultObserved = true
                }
            }
        )

        engineSession.findAll("mozilla")
        shadowOf(getMainLooper()).idle()

        assertEquals("mozilla", findObserved)
        assertTrue(findResultObserved)
        verify(sessionFinder).find("mozilla", 0)
    }

    @Test
    fun findNext() {
        val finderResult = mock<GeckoSession.FinderResult>()
        val sessionFinder = mock<SessionFinder>()
        whenever(sessionFinder.find(eq(null), anyInt())).thenReturn(GeckoResult.fromValue(finderResult))

        whenever(geckoSession.finder).thenReturn(sessionFinder)

        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        var findResultObserved = false
        engineSession.register(
            object : EngineSession.Observer {
                override fun onFindResult(activeMatchOrdinal: Int, numberOfMatches: Int, isDoneCounting: Boolean) {
                    assertEquals(0, activeMatchOrdinal)
                    assertEquals(0, numberOfMatches)
                    assertTrue(isDoneCounting)
                    findResultObserved = true
                }
            }
        )

        engineSession.findNext(true)
        shadowOf(getMainLooper()).idle()

        assertTrue(findResultObserved)
        verify(sessionFinder).find(null, 0)

        engineSession.findNext(false)
        shadowOf(getMainLooper()).idle()

        assertTrue(findResultObserved)
        verify(sessionFinder).find(null, GeckoSession.FINDER_FIND_BACKWARDS)
    }

    @Test
    fun clearFindMatches() {
        val finder = mock<SessionFinder>()
        whenever(geckoSession.finder).thenReturn(finder)

        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        engineSession.clearFindMatches()

        verify(finder).clear()
    }

    @Test
    fun exitFullScreenModeTriggersExitEvent() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        val observer: EngineSession.Observer = mock()

        // Verify the event is triggered for exiting fullscreen mode and GeckoView is called.
        engineSession.exitFullScreenMode()
        verify(geckoSession).exitFullScreen()

        // Verify the call to the observer.
        engineSession.register(observer)

        captureDelegates()

        contentDelegate.value.onFullScreen(geckoSession, true)

        verify(observer).onFullScreenChange(true)
    }

    @Test
    fun exitFullscreenTrueHasNoInteraction() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        engineSession.exitFullScreenMode()
        verify(geckoSession).exitFullScreen()
    }

    @Test
    fun viewportFitChangeTranslateValuesCorrectly() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        val observer: EngineSession.Observer = mock()

        // Verify the call to the observer.
        engineSession.register(observer)
        captureDelegates()

        contentDelegate.value.onMetaViewportFitChange(geckoSession, "test")
        verify(observer).onMetaViewportFitChanged(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT)
        reset(observer)

        contentDelegate.value.onMetaViewportFitChange(geckoSession, "auto")
        verify(observer).onMetaViewportFitChanged(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT)
        reset(observer)

        contentDelegate.value.onMetaViewportFitChange(geckoSession, "cover")
        verify(observer).onMetaViewportFitChanged(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES)
        reset(observer)

        contentDelegate.value.onMetaViewportFitChange(geckoSession, "contain")
        verify(observer).onMetaViewportFitChanged(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER)
        reset(observer)
    }

    @Test
    fun onShowDynamicToolbarTriggersTheRightEvent() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        val observer: EngineSession.Observer = mock()

        // Verify the call to the observer.
        engineSession.register(observer)
        captureDelegates()

        contentDelegate.value.onShowDynamicToolbar(geckoSession)

        verify(observer).onShowDynamicToolbar()
    }

    @Test
    fun clearData() {
        val engineSession = GeckoEngineSession(runtime, geckoSessionProvider = geckoSessionProvider)
        val observer: EngineSession.Observer = mock()

        engineSession.register(observer)

        engineSession.clearData()

        verifyNoInteractions(observer)
    }

    @Test
    fun `Closing engine session should close underlying gecko session`() {
        val geckoSession = mockGeckoSession()

        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = { geckoSession })

        engineSession.close()

        verify(geckoSession).close()
    }

    @Test
    fun `onLoadRequest will try to intercept new window load requests`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        var observedUrl: String? = null
        var observedIntent: Intent? = null
        var observedFallbackUrl: String? = null
        var observedAppName: String? = null

        var observedLoadUrl: String? = null
        var observedTriggeredByRedirect: Boolean? = null
        var observedTriggeredByWebContent: Boolean? = null

        engineSession.settings.requestInterceptor =
            object : RequestInterceptor {
                override fun interceptsAppInitiatedRequests() = true

                override fun onLoadRequest(
                    engineSession: EngineSession,
                    uri: String,
                    lastUri: String?,
                    hasUserGesture: Boolean,
                    isSameDomain: Boolean,
                    isRedirect: Boolean,
                    isDirectNavigation: Boolean,
                    isSubframeRequest: Boolean,
                ): RequestInterceptor.InterceptionResponse? {
                    return when (uri) {
                        "sample:about" ->
                            RequestInterceptor.InterceptionResponse.AppIntent(mock(), "result", "fallback", "app")
                        else -> null
                    }
                }
            }

        engineSession.register(
            object : EngineSession.Observer {
                override fun onLaunchIntentRequest(
                    url: String,
                    appIntent: Intent?,
                    fallbackUrl: String?,
                    appName: String?,
                    sourceUrl: String?,
                ) {
                    observedUrl = url
                    observedIntent = appIntent
                    observedFallbackUrl = fallbackUrl
                    observedAppName = appName
                }

                override fun onLoadRequest(url: String, triggeredByRedirect: Boolean, triggeredByWebContent: Boolean) {
                    observedLoadUrl = url
                    observedTriggeredByRedirect = triggeredByRedirect
                    observedTriggeredByWebContent = triggeredByWebContent
                }
            }
        )

        var result =
            navigationDelegate.value.onLoadRequest(
                mock(),
                mockLoadRequest(
                    "sample:about",
                    null,
                    GeckoSession.NavigationDelegate.TARGET_WINDOW_NEW,
                    triggeredByRedirect = true,
                ),
            )

        assertEquals(result!!.poll(0), AllowOrDeny.DENY)
        assertNotNull(observedIntent)
        assertEquals("result", observedUrl)
        assertNotNull(observedFallbackUrl)
        assertNotNull(observedAppName)
        assertNull(observedLoadUrl)
        assertNull(observedTriggeredByRedirect)
        assertNull(observedTriggeredByWebContent)

        result =
            navigationDelegate.value.onLoadRequest(
                mock(),
                mockLoadRequest(
                    "sample:about",
                    null,
                    GeckoSession.NavigationDelegate.TARGET_WINDOW_NEW,
                    triggeredByRedirect = false,
                ),
            )

        assertEquals(result!!.poll(0), AllowOrDeny.DENY)
        assertNotNull(observedIntent)
        assertEquals("result", observedUrl)
        assertNull(observedLoadUrl)
        assertNull(observedTriggeredByRedirect)
        assertNull(observedTriggeredByWebContent)
    }

    @Test
    fun `onLoadRequest allows new window requests if not intercepted`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        var observedUrl: String? = null
        var observedIntent: Intent? = null
        var observedFallbackUrl: String? = null
        var observedAppName: String? = null

        var observedLoadUrl: String? = null
        var observedTriggeredByRedirect: Boolean? = null
        var observedTriggeredByWebContent: Boolean? = null

        engineSession.settings.requestInterceptor =
            object : RequestInterceptor {
                override fun interceptsAppInitiatedRequests() = true

                override fun onLoadRequest(
                    engineSession: EngineSession,
                    uri: String,
                    lastUri: String?,
                    hasUserGesture: Boolean,
                    isSameDomain: Boolean,
                    isRedirect: Boolean,
                    isDirectNavigation: Boolean,
                    isSubframeRequest: Boolean,
                ): RequestInterceptor.InterceptionResponse? {
                    return when (uri) {
                        "sample:about" ->
                            RequestInterceptor.InterceptionResponse.AppIntent(mock(), "result", "fallback", "app")
                        else -> null
                    }
                }
            }

        engineSession.register(
            object : EngineSession.Observer {
                override fun onLaunchIntentRequest(
                    url: String,
                    appIntent: Intent?,
                    fallbackUrl: String?,
                    appName: String?,
                    sourceUrl: String?,
                ) {
                    observedUrl = url
                    observedIntent = appIntent
                    observedFallbackUrl = fallbackUrl
                    observedAppName = appName
                }

                override fun onLoadRequest(url: String, triggeredByRedirect: Boolean, triggeredByWebContent: Boolean) {
                    observedLoadUrl = url
                    observedTriggeredByRedirect = triggeredByRedirect
                    observedTriggeredByWebContent = triggeredByWebContent
                }
            }
        )

        var result =
            navigationDelegate.value.onLoadRequest(
                mock(),
                mockLoadRequest(
                    "about:blank",
                    null,
                    GeckoSession.NavigationDelegate.TARGET_WINDOW_NEW,
                    triggeredByRedirect = true,
                ),
            )

        assertEquals(result!!.poll(0), AllowOrDeny.ALLOW)
        assertNull(observedIntent)
        assertNull(observedUrl)
        assertNull(observedLoadUrl)
        assertNull(observedFallbackUrl)
        assertNull(observedAppName)
        assertNull(observedTriggeredByRedirect)
        assertNull(observedTriggeredByWebContent)

        result =
            navigationDelegate.value.onLoadRequest(
                mock(),
                mockLoadRequest(
                    "https://www.example.com",
                    null,
                    GeckoSession.NavigationDelegate.TARGET_WINDOW_NEW,
                    triggeredByRedirect = true,
                ),
            )

        assertEquals(result!!.poll(0), AllowOrDeny.ALLOW)
        assertNull(observedIntent)
        assertNull(observedUrl)
        assertNull(observedLoadUrl)
        assertNull(observedFallbackUrl)
        assertNull(observedAppName)
        assertNull(observedTriggeredByRedirect)
        assertNull(observedTriggeredByWebContent)
    }

    @Test
    fun `onLoadRequest not intercepted and not new window will notify observer`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        var observedLoadUrl: String? = null
        var observedTriggeredByRedirect: Boolean? = null
        var observedTriggeredByWebContent: Boolean? = null

        engineSession.settings.requestInterceptor =
            object : RequestInterceptor {
                override fun interceptsAppInitiatedRequests() = true

                override fun onLoadRequest(
                    engineSession: EngineSession,
                    uri: String,
                    lastUri: String?,
                    hasUserGesture: Boolean,
                    isSameDomain: Boolean,
                    isRedirect: Boolean,
                    isDirectNavigation: Boolean,
                    isSubframeRequest: Boolean,
                ): RequestInterceptor.InterceptionResponse? {
                    return when (uri) {
                        "sample:about" ->
                            RequestInterceptor.InterceptionResponse.AppIntent(mock(), "result", null, null)
                        else -> null
                    }
                }
            }

        engineSession.register(
            object : EngineSession.Observer {
                override fun onLoadRequest(url: String, triggeredByRedirect: Boolean, triggeredByWebContent: Boolean) {
                    observedLoadUrl = url
                    observedTriggeredByRedirect = triggeredByRedirect
                    observedTriggeredByWebContent = triggeredByWebContent
                }
            }
        )

        val result =
            navigationDelegate.value.onLoadRequest(
                mock(),
                mockLoadRequest("https://www.example.com", null, triggeredByRedirect = true),
            )

        assertEquals(result!!.poll(0), AllowOrDeny.ALLOW)
        assertEquals("https://www.example.com", observedLoadUrl)
        assertEquals(true, observedTriggeredByRedirect)
        assertEquals(false, observedTriggeredByWebContent)
    }

    @Test
    fun `State provided through delegate will be returned from saveState`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        val state: GeckoSession.SessionState = mock()

        var observedState: EngineSessionState? = null

        engineSession.register(
            object : EngineSession.Observer {
                override fun onStateUpdated(state: EngineSessionState) {
                    observedState = state
                }
            }
        )

        progressDelegate.value.onSessionStateChange(mock(), state)

        assertNotNull(observedState)
        assertIs<GeckoEngineSessionState>(observedState)

        val actualState = (observedState as GeckoEngineSessionState).actualState
        assertEquals(state, actualState)
    }

    @Test
    fun `onFirstContentfulPaint notifies observers`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        var observed = false

        engineSession.register(
            object : EngineSession.Observer {
                override fun onFirstContentfulPaint() {
                    observed = true
                }
            }
        )

        contentDelegate.value.onFirstContentfulPaint(mock())
        assertTrue(observed)
    }

    @Test
    fun `onPaintStatusReset notifies observers`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        var observed = false

        engineSession.register(
            object : EngineSession.Observer {
                override fun onPaintStatusReset() {
                    observed = true
                }
            }
        )

        contentDelegate.value.onPaintStatusReset(mock())
        assertTrue(observed)
    }

    @Test
    fun `onCrash notifies observers about crash`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        var crashedState = false

        engineSession.register(
            object : EngineSession.Observer {
                override fun onCrash() {
                    crashedState = true
                }
            }
        )

        contentDelegate.value.onCrash(mock())

        assertEquals(true, crashedState)
    }

    @Test
    fun `onLoadRequest will notify onLaunchIntent observers if request was intercepted with app intent`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        engineSession.settings.requestInterceptor =
            object : RequestInterceptor {
                override fun interceptsAppInitiatedRequests() = true

                override fun onLoadRequest(
                    engineSession: EngineSession,
                    uri: String,
                    lastUri: String?,
                    hasUserGesture: Boolean,
                    isSameDomain: Boolean,
                    isRedirect: Boolean,
                    isDirectNavigation: Boolean,
                    isSubframeRequest: Boolean,
                ): RequestInterceptor.InterceptionResponse? {
                    return when (uri) {
                        "sample:triggeredByRedirect" ->
                            RequestInterceptor.InterceptionResponse.AppIntent(mock(), "result1", "fallback1", "app1")
                        "sample:NotTriggeredByRedirect" ->
                            RequestInterceptor.InterceptionResponse.AppIntent(mock(), "result2", "fallback2", "app2")
                        "sample:isDirectNavigation" ->
                            RequestInterceptor.InterceptionResponse.AppIntent(mock(), "result3", "fallback3", "app3")
                        else -> null
                    }
                }
            }

        val observer =
            object : EngineSession.Observer {
                var observedUrl: String? = null
                var observedIntent: Intent? = null
                var observedFallbackUrl: String? = null
                var observedAppName: String? = null
                var observedSourceUrl: String? = null

                override fun onLaunchIntentRequest(
                    url: String,
                    appIntent: Intent?,
                    fallbackUrl: String?,
                    appName: String?,
                    sourceUrl: String?,
                ) {
                    observedUrl = url
                    observedIntent = appIntent
                    observedFallbackUrl = fallbackUrl
                    observedAppName = appName
                    observedSourceUrl = sourceUrl
                }

                fun reset() {
                    observedUrl = null
                    observedIntent = null
                    observedFallbackUrl = null
                    observedAppName = null
                    observedSourceUrl = null
                }
            }

        engineSession.register(observer)
        engineSession.currentUrl = "https://www.mozilla.org"

        navigationDelegate.value.onLoadRequest(
            mock(),
            mockLoadRequest("sample:triggeredByRedirect", triggeredByRedirect = true, isDirectNavigation = false),
        )

        assertNotNull(observer.observedIntent)
        assertEquals("result1", observer.observedUrl)
        assertEquals("fallback1", observer.observedFallbackUrl)
        assertEquals("app1", observer.observedAppName)
        assertEquals("https://www.mozilla.org", observer.observedSourceUrl)

        observer.reset()
        navigationDelegate.value.onLoadRequest(
            mock(),
            mockLoadRequest("sample:NotTriggeredByRedirect", triggeredByRedirect = false, isDirectNavigation = false),
        )

        assertNotNull(observer.observedIntent)
        assertEquals("result2", observer.observedUrl)
        assertEquals("fallback2", observer.observedFallbackUrl)
        assertEquals("app2", observer.observedAppName)
        assertEquals("https://www.mozilla.org", observer.observedSourceUrl)

        observer.reset()
        navigationDelegate.value.onLoadRequest(
            mock(),
            mockLoadRequest("sample:isDirectNavigation", triggeredByRedirect = false, isDirectNavigation = true),
        )

        assertNotNull(observer.observedIntent)
        assertEquals("result3", observer.observedUrl)
        assertEquals("fallback3", observer.observedFallbackUrl)
        assertEquals("app3", observer.observedAppName)
        assertNull(observer.observedSourceUrl)

        observer.reset()
        navigationDelegate.value.onLoadRequest(
            mock(),
            mockLoadRequest("https://www.example.org", triggeredByRedirect = false, isDirectNavigation = true),
        )
        navigationDelegate.value.onLoadRequest(
            mock(),
            mockLoadRequest("sample:triggeredByRedirect", triggeredByRedirect = true, isDirectNavigation = false),
        )

        assertNotNull(observer.observedIntent)
        assertEquals("result1", observer.observedUrl)
        assertNull(observer.observedSourceUrl)
    }

    @Test
    fun `onLoadRequest keep track of the last onLoadRequest uri correctly`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        var observedUrl: String? = null

        engineSession.settings.requestInterceptor =
            object : RequestInterceptor {
                override fun interceptsAppInitiatedRequests() = true

                override fun onLoadRequest(
                    engineSession: EngineSession,
                    uri: String,
                    lastUri: String?,
                    hasUserGesture: Boolean,
                    isSameDomain: Boolean,
                    isRedirect: Boolean,
                    isDirectNavigation: Boolean,
                    isSubframeRequest: Boolean,
                ): RequestInterceptor.InterceptionResponse? {
                    observedUrl = lastUri
                    return null
                }
            }

        navigationDelegate.value.onLoadRequest(mock(), mockLoadRequest("test1"))
        assertEquals(null, observedUrl)

        navigationDelegate.value.onLoadRequest(mock(), mockLoadRequest("test2"))
        assertEquals("test1", observedUrl)

        navigationDelegate.value.onLoadRequest(mock(), mockLoadRequest("test3"))
        assertEquals("test2", observedUrl)
    }

    @Test
    fun `onSubframeLoadRequest will notify onLaunchIntent observers if request was intercepted with app intent`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        var observedUrl: String? = null
        var observedIntent: Intent? = null
        var observedFallbackUrl: String? = null
        var observedAppName: String? = null
        var observedIsSubframe = false

        engineSession.settings.requestInterceptor =
            object : RequestInterceptor {
                override fun interceptsAppInitiatedRequests() = true

                override fun onLoadRequest(
                    engineSession: EngineSession,
                    uri: String,
                    lastUri: String?,
                    hasUserGesture: Boolean,
                    isSameDomain: Boolean,
                    isRedirect: Boolean,
                    isDirectNavigation: Boolean,
                    isSubframeRequest: Boolean,
                ): RequestInterceptor.InterceptionResponse? {
                    observedIsSubframe = isSubframeRequest
                    return when (uri) {
                        "sample:about" ->
                            RequestInterceptor.InterceptionResponse.AppIntent(mock(), "result", "fallback", "app")
                        else -> null
                    }
                }
            }

        engineSession.register(
            object : EngineSession.Observer {
                override fun onLaunchIntentRequest(
                    url: String,
                    appIntent: Intent?,
                    fallbackUrl: String?,
                    appName: String?,
                    sourceUrl: String?,
                ) {
                    observedUrl = url
                    observedIntent = appIntent
                    observedFallbackUrl = fallbackUrl
                    observedAppName = appName
                }
            }
        )

        navigationDelegate.value.onSubframeLoadRequest(
            mock(),
            mockLoadRequest("sample:about", triggeredByRedirect = true),
        )

        assertNotNull(observedIntent)
        assertEquals("result", observedUrl)
        assertEquals("fallback", observedFallbackUrl)
        assertEquals("app", observedAppName)
        assertEquals(true, observedIsSubframe)

        observedUrl = null
        observedIntent = null
        observedFallbackUrl = null
        observedAppName = null
        navigationDelegate.value.onSubframeLoadRequest(
            mock(),
            mockLoadRequest("sample:about", triggeredByRedirect = false),
        )

        assertNotNull(observedIntent)
        assertEquals("result", observedUrl)
        assertEquals("fallback", observedFallbackUrl)
        assertEquals("app", observedAppName)
        assertEquals(true, observedIsSubframe)
    }

    @Test
    fun `onLoadRequest will notify any observers if request was intercepted as url`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        var observedUrl: String? = null
        var observedIntent: Intent? = null
        var observedFallbackUrl: String? = null
        var observedAppName: String? = null
        var observedOnLoadRequestUrl: String? = null
        var observedTriggeredByRedirect: Boolean? = null
        var observedTriggeredByWebContent: Boolean? = null

        engineSession.settings.requestInterceptor =
            object : RequestInterceptor {
                override fun interceptsAppInitiatedRequests() = true

                override fun onLoadRequest(
                    engineSession: EngineSession,
                    uri: String,
                    lastUri: String?,
                    hasUserGesture: Boolean,
                    isSameDomain: Boolean,
                    isRedirect: Boolean,
                    isDirectNavigation: Boolean,
                    isSubframeRequest: Boolean,
                ): RequestInterceptor.InterceptionResponse? {
                    return when (uri) {
                        "sample:about" -> RequestInterceptor.InterceptionResponse.Url("result")
                        else -> null
                    }
                }
            }

        engineSession.register(
            object : EngineSession.Observer {
                override fun onLaunchIntentRequest(
                    url: String,
                    appIntent: Intent?,
                    fallbackUrl: String?,
                    appName: String?,
                    sourceUrl: String?,
                ) {
                    observedUrl = url
                    observedIntent = appIntent
                    observedFallbackUrl = fallbackUrl
                    observedAppName = appName
                }

                override fun onLoadRequest(
                    url: String,
                    triggeredByRedirect: Boolean,
                    triggeredByWebContent: Boolean,
                ) {
                    observedOnLoadRequestUrl = url
                    observedTriggeredByRedirect = triggeredByRedirect
                    observedTriggeredByWebContent = triggeredByWebContent
                }
            }
        )

        navigationDelegate.value.onLoadRequest(
            mock(),
            mockLoadRequest("sample:about", triggeredByRedirect = true),
        )

        assertNull(observedUrl)
        assertNull(observedIntent)
        assertNull(observedFallbackUrl)
        assertNull(observedAppName)
        assertNull(observedTriggeredByRedirect)
        assertNull(observedTriggeredByWebContent)
        assertNull(observedOnLoadRequestUrl)

        navigationDelegate.value.onLoadRequest(
            mock(),
            mockLoadRequest("sample:about", triggeredByRedirect = false),
        )

        assertNull(observedUrl)
        assertNull(observedIntent)
        assertNull(observedFallbackUrl)
        assertNull(observedAppName)
        assertNull(observedTriggeredByRedirect)
        assertNull(observedTriggeredByWebContent)
        assertNull(observedOnLoadRequestUrl)
    }

    @Test
    fun `onLoadRequest will notify onLoadRequest observers if request was not intercepted`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        var observedUrl: String? = null
        var observedIntent: Intent? = null
        var observedFallbackUrl: String? = null
        var observedAppName: String? = null
        var observedOnLoadRequestUrl: String? = null
        var observedTriggeredByRedirect: Boolean? = null
        var observedTriggeredByWebContent: Boolean? = null

        engineSession.settings.requestInterceptor = null
        engineSession.register(
            object : EngineSession.Observer {
                override fun onLaunchIntentRequest(
                    url: String,
                    appIntent: Intent?,
                    fallbackUrl: String?,
                    appName: String?,
                    sourceUrl: String?,
                ) {
                    observedUrl = url
                    observedIntent = appIntent
                    observedFallbackUrl = fallbackUrl
                    observedAppName = appName
                }

                override fun onLoadRequest(
                    url: String,
                    triggeredByRedirect: Boolean,
                    triggeredByWebContent: Boolean,
                ) {
                    observedOnLoadRequestUrl = url
                    observedTriggeredByRedirect = triggeredByRedirect
                    observedTriggeredByWebContent = triggeredByWebContent
                }
            }
        )

        navigationDelegate.value.onLoadRequest(
            mock(),
            mockLoadRequest("sample:about", triggeredByRedirect = true),
        )

        assertNull(observedUrl)
        assertNull(observedIntent)
        assertNull(observedFallbackUrl)
        assertNull(observedAppName)
        assertNotNull(observedTriggeredByRedirect)
        assertTrue(observedTriggeredByRedirect)
        assertNotNull(observedTriggeredByWebContent)
        assertFalse(observedTriggeredByWebContent)
        assertEquals("sample:about", observedOnLoadRequestUrl)

        navigationDelegate.value.onLoadRequest(
            mock(),
            mockLoadRequest("sample:about", triggeredByRedirect = false),
        )

        assertNull(observedUrl)
        assertNull(observedIntent)
        assertNull(observedFallbackUrl)
        assertNull(observedAppName)
        assertNotNull(observedTriggeredByRedirect)
        assertFalse(observedTriggeredByRedirect)
        assertNotNull(observedTriggeredByWebContent)
        assertFalse(observedTriggeredByWebContent)
        assertEquals("sample:about", observedOnLoadRequestUrl)
    }

    @Test
    fun `onLoadRequest will notify observers if the url is loaded from the user interacting with chrome`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        val fakeUrl = "https://example.com"
        var observedUrl: String?
        var observedTriggeredByWebContent: Boolean?

        engineSession.settings.requestInterceptor =
            object : RequestInterceptor {
                override fun onLoadRequest(
                    engineSession: EngineSession,
                    uri: String,
                    lastUri: String?,
                    hasUserGesture: Boolean,
                    isSameDomain: Boolean,
                    isRedirect: Boolean,
                    isDirectNavigation: Boolean,
                    isSubframeRequest: Boolean,
                ): RequestInterceptor.InterceptionResponse? {
                    return when (uri) {
                        fakeUrl -> null
                        else -> RequestInterceptor.InterceptionResponse.AppIntent(mock(), fakeUrl, null, null)
                    }
                }
            }

        engineSession.register(
            object : EngineSession.Observer {
                override fun onLoadRequest(
                    url: String,
                    triggeredByRedirect: Boolean,
                    triggeredByWebContent: Boolean,
                ) {
                    observedTriggeredByWebContent = triggeredByWebContent
                    observedUrl = url
                }
            }
        )

        fun fakePageLoad(expectedTriggeredByWebContent: Boolean) {
            observedTriggeredByWebContent = null
            observedUrl = null
            navigationDelegate.value.onLoadRequest(
                mock(),
                mockLoadRequest(
                    fakeUrl,
                    triggeredByRedirect = true,
                    hasUserGesture = expectedTriggeredByWebContent,
                ),
            )
            progressDelegate.value.onPageStop(mock(), true)
            assertNotNull(observedTriggeredByWebContent)
            assertEquals(expectedTriggeredByWebContent, observedTriggeredByWebContent!!)
            assertNotNull(observedUrl)
            assertEquals(fakeUrl, observedUrl)
        }

        // loadUrl(url: String)
        engineSession.loadUrl(fakeUrl)
        verify(geckoSession).load(GeckoSession.Loader().uri(fakeUrl))
        fakePageLoad(false)

        // subsequent page loads _are_ from web content
        fakePageLoad(true)

        // loadData(data: String, mimeType: String, encoding: String)
        val fakeData = "data://"
        val fakeMimeType = ""
        val fakeEncoding = ""
        engineSession.loadData(data = fakeData, mimeType = fakeMimeType, encoding = fakeEncoding)
        verify(geckoSession).load(GeckoSession.Loader().data(fakeData, fakeMimeType))
        fakePageLoad(false)

        fakePageLoad(true)

        // reload()
        engineSession.initialLoadRequest = null
        engineSession.reload()
        verify(geckoSession).reload(GeckoSession.LOAD_FLAGS_NONE)
        fakePageLoad(false)

        fakePageLoad(true)

        // goBack()
        engineSession.goBack()
        verify(geckoSession).goBack(true)
        fakePageLoad(false)

        fakePageLoad(true)

        // goForward()
        engineSession.goForward()
        verify(geckoSession).goForward(true)
        fakePageLoad(false)

        fakePageLoad(true)

        // toggleDesktopMode()
        engineSession.toggleDesktopMode(false, reload = true)
        // This is the second time in this test, so we actually want two invocations.
        verify(geckoSession, times(2)).reload(GeckoSession.LOAD_FLAGS_NONE)
        fakePageLoad(false)

        fakePageLoad(true)

        // goToHistoryIndex(index: Int)
        engineSession.goToHistoryIndex(0)
        verify(geckoSession).gotoHistoryIndex(0)
        fakePageLoad(false)

        fakePageLoad(true)
    }

    @Test
    fun `onLoadRequest will return correct GeckoResult if no observer is available`() {
        GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)
        captureDelegates()

        val geckoResult =
            navigationDelegate.value.onLoadRequest(
                mock(),
                mockLoadRequest("sample:about", triggeredByRedirect = true),
            )

        assertEquals(geckoResult!!, GeckoResult.allow())
    }

    @Test
    fun loadFlagsAreAligned() {
        assertEquals(LoadUrlFlags.BYPASS_CACHE, GeckoSession.LOAD_FLAGS_BYPASS_CACHE)
        assertEquals(LoadUrlFlags.BYPASS_PROXY, GeckoSession.LOAD_FLAGS_BYPASS_PROXY)
        assertEquals(LoadUrlFlags.EXTERNAL, GeckoSession.LOAD_FLAGS_EXTERNAL)
        assertEquals(LoadUrlFlags.ALLOW_POPUPS, GeckoSession.LOAD_FLAGS_ALLOW_POPUPS)
        assertEquals(LoadUrlFlags.BYPASS_CLASSIFIER, GeckoSession.LOAD_FLAGS_BYPASS_CLASSIFIER)
        assertEquals(LoadUrlFlags.LOAD_FLAGS_FORCE_ALLOW_DATA_URI, GeckoSession.LOAD_FLAGS_FORCE_ALLOW_DATA_URI)
        assertEquals(LoadUrlFlags.LOAD_FLAGS_REPLACE_HISTORY, GeckoSession.LOAD_FLAGS_REPLACE_HISTORY)
    }

    @Test
    fun `onKill will notify observers`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        captureDelegates()

        var observerNotified = false

        engineSession.register(
            object : EngineSession.Observer {
                override fun onProcessKilled() {
                    observerNotified = true
                }
            }
        )

        val mockedState: GeckoSession.SessionState = mock()
        progressDelegate.value.onSessionStateChange(geckoSession, mockedState)

        contentDelegate.value.onKill(geckoSession)

        assertTrue(observerNotified)
    }

    @Test
    fun `onNewSession creates window request`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)

        captureDelegates()

        var receivedWindowRequest: WindowRequest? = null

        engineSession.register(
            object : EngineSession.Observer {
                override fun onWindowRequest(windowRequest: WindowRequest) {
                    receivedWindowRequest = windowRequest
                }
            }
        )

        navigationDelegate.value.onNewSession(mock(), "mozilla.org")

        assertNotNull(receivedWindowRequest)
        assertEquals("mozilla.org", receivedWindowRequest.url)
        assertEquals(WindowRequest.Type.OPEN, receivedWindowRequest.type)
    }

    @Test
    fun `onCloseRequest creates window request`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)

        captureDelegates()

        var receivedWindowRequest: WindowRequest? = null

        engineSession.register(
            object : EngineSession.Observer {
                override fun onWindowRequest(windowRequest: WindowRequest) {
                    receivedWindowRequest = windowRequest
                }
            }
        )

        contentDelegate.value.onCloseRequest(geckoSession)

        assertNotNull(receivedWindowRequest)
        assertSame(engineSession, receivedWindowRequest.prepare())
        assertEquals(WindowRequest.Type.CLOSE, receivedWindowRequest.type)
    }

    class MockSecurityInformation(
        origin: String? = null,
        certificate: X509Certificate? = null,
    ) : SecurityInformation() {
        init {
            origin?.let {
                ReflectionUtils.setField(this, "origin", origin)
            }
            certificate?.let {
                ReflectionUtils.setField(this, "certificate", certificate)
            }
        }
    }

    @Test
    fun `certificate issuer is parsed and provided onSecurityChange`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)

        var observedIssuer: String? = null
        var observedCertificate: X509Certificate? = null
        engineSession.register(
            object : EngineSession.Observer {
                override fun onSecurityChange(
                    secure: Boolean,
                    host: String?,
                    issuer: String?,
                    certificate: X509Certificate?,
                ) {
                    observedIssuer = issuer
                    observedCertificate = certificate
                }
            }
        )

        captureDelegates()

        val unparsedIssuerName =
            "CN=Digicert SHA2 Extended Validation Server CA,OU=www.digicert.com,O=DigiCert Inc,C=US"
        val parsedIssuerName = "DigiCert Inc"
        val certificate: X509Certificate = mock()
        val principal: Principal = mock()
        whenever(principal.name).thenReturn(unparsedIssuerName)
        whenever(certificate.issuerDN).thenReturn(principal)

        val securityInformation = MockSecurityInformation(certificate = certificate)
        progressDelegate.value.onSecurityChange(mock(), securityInformation)
        assertEquals(parsedIssuerName, observedIssuer)
        assertEquals(certificate, observedCertificate)
    }

    @Test
    fun `certificate issuer is parsed and provided onSecurityChange with null arg`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        var observedIssuer: String? = null
        var observedCertificate: X509Certificate? = null
        engineSession.register(
            object : EngineSession.Observer {
                override fun onSecurityChange(
                    secure: Boolean,
                    host: String?,
                    issuer: String?,
                    certificate: X509Certificate?,
                ) {
                    observedIssuer = issuer
                    observedCertificate = certificate
                }
            }
        )

        captureDelegates()

        val unparsedIssuerName = null
        val parsedIssuerName = null
        val certificate: X509Certificate = mock()
        val principal: Principal = mock()
        whenever(principal.name).thenReturn(unparsedIssuerName)
        whenever(certificate.issuerDN).thenReturn(principal)

        val securityInformation = MockSecurityInformation(certificate = certificate)
        progressDelegate.value.onSecurityChange(mock(), securityInformation)
        assertEquals(parsedIssuerName, observedIssuer)
        assertEquals(certificate, observedCertificate)
    }

    @Test
    fun `GIVEN canGoBack true WHEN goBack() is called THEN verify EngineObserver onNavigateBack() is triggered`() {
        var observedOnNavigateBack = false
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        engineSession.register(
            object : EngineSession.Observer {
                override fun onNavigateBack() {
                    observedOnNavigateBack = true
                }
            }
        )

        captureDelegates()
        navigationDelegate.value.onCanGoBack(mock(), true)
        engineSession.goBack()
        assertTrue(observedOnNavigateBack)
    }

    @Test
    fun `GIVEN canGoBack false WHEN goBack() is called THEN verify EngineObserver onNavigateBack() is not triggered`() {
        var observedOnNavigateBack = false
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        engineSession.register(
            object : EngineSession.Observer {
                override fun onNavigateBack() {
                    observedOnNavigateBack = true
                }
            }
        )

        captureDelegates()
        navigationDelegate.value.onCanGoBack(mock(), false)
        engineSession.goBack()
        assertFalse(observedOnNavigateBack)
    }

    @Test
    fun `GIVEN forward navigation is possible WHEN navigating forward THEN observers are notified`() {
        var observedOnNavigateForward = false
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        engineSession.register(
            object : EngineSession.Observer {
                override fun onNavigateForward() {
                    observedOnNavigateForward = true
                }
            }
        )

        captureDelegates()
        navigationDelegate.value.onCanGoForward(mock(), true)
        engineSession.goForward()
        assertTrue(observedOnNavigateForward)
    }

    @Test
    fun `GIVEN forward navigation is not possible WHEN navigating forward THEN forward navigation observers are not notified`() {
        var observedOnNavigateForward = false
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        engineSession.register(
            object : EngineSession.Observer {
                override fun onNavigateBack() {
                    observedOnNavigateForward = true
                }
            }
        )

        captureDelegates()
        navigationDelegate.value.onCanGoForward(mock(), false)
        engineSession.goForward()
        assertFalse(observedOnNavigateForward)
    }

    @Test
    fun `WHEN URL is loaded THEN URL load observer is notified`() {
        var onLoadUrlTriggered = false
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        engineSession.register(
            object : EngineSession.Observer {
                override fun onLoadUrl() {
                    onLoadUrlTriggered = true
                }
            }
        )

        captureDelegates()
        engineSession.loadUrl("http://mozilla.org")
        assertTrue(onLoadUrlTriggered)
    }

    @Test
    fun `WHEN data is loaded THEN data load observer is notified`() {
        var onLoadDataTriggered = false
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        engineSession.register(
            object : EngineSession.Observer {
                override fun onLoadData() {
                    onLoadDataTriggered = true
                }
            }
        )

        captureDelegates()
        engineSession.loadData("<html><body/></html>")
        assertTrue(onLoadDataTriggered)
    }

    @Test
    fun `WHEN navigating to history index THEN the observer is notified`() {
        var onGotoHistoryIndexTriggered = false
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        engineSession.register(
            object : EngineSession.Observer {
                override fun onGotoHistoryIndex() {
                    onGotoHistoryIndexTriggered = true
                }
            }
        )

        captureDelegates()
        engineSession.goToHistoryIndex(0)
        assertTrue(onGotoHistoryIndexTriggered)
    }

    @Test
    fun `GIVEN a list of blocked schemes set WHEN getBlockedSchemes is called THEN it returns that list`() {
        val engineSession = GeckoEngineSession(mock(), geckoSessionProvider = geckoSessionProvider)

        assertSame(GeckoEngineSession.BLOCKED_SCHEMES, engineSession.getBlockedSchemes())
    }

    @Test
    fun `WHEN requestPdfToDownload THEN notify observers`() {
        var seenContentDisposition: String? = null
        val engineSession =
            GeckoEngineSession(
                    runtime = mock(),
                    geckoSessionProvider = geckoSessionProvider,
                    defaultSettings =
                        DefaultSettings(
                            downloadDelegate =
                                FakeEngineDownloadDelegate(
                                    guessFileName = { contentDisposition, _, _ ->
                                        seenContentDisposition = contentDisposition
                                        "Mozilla.pdf"
                                    }
                                )
                        ),
                )
                .apply {
                    currentUrl = "https://mozilla.org"
                    currentTitle = "Mozilla"
                }
        engineSession.register(
            object : EngineSession.Observer {
                override fun onExternalResource(
                    url: String,
                    fileName: String?,
                    contentLength: Long?,
                    contentType: String?,
                    cookie: String?,
                    userAgent: String?,
                    isPrivate: Boolean,
                    skipConfirmation: Boolean,
                    openInApp: Boolean,
                    response: Response?,
                ) {
                    assertEquals("PDF response is always a success.", RESPONSE_CODE_SUCCESS, response!!.status)
                    assertEquals("Length should always be zero.", 0L, contentLength)
                    assertEquals("Filename is based on title, when available.", "Mozilla.pdf", fileName)
                    assertEquals("Content type is always static.", "application/pdf", contentType)
                }
            }
        )

        whenever(geckoSession.isPdfJs).thenReturn(GeckoResult.fromValue(false))
        whenever(geckoSession.saveAsPdf()).thenReturn(GeckoResult.fromValue(mock()))

        engineSession.requestPdfToDownload()
        shadowOf(getMainLooper()).idle()

        assertEquals("attachment; filename=\"Mozilla.pdf\";", seenContentDisposition)
    }

    @Test
    fun `GIVEN a PDF is showing the document WHEN requestPdfToDownload THEN it describes the download`() {
        var seenUrl: String? = null
        var seenContentDisposition: String? = null
        val engineSession =
            GeckoEngineSession(
                    runtime = mock(),
                    geckoSessionProvider = geckoSessionProvider,
                    defaultSettings =
                        DefaultSettings(
                            downloadDelegate =
                                FakeEngineDownloadDelegate(
                                    guessFileName = { contentDisposition, _, _ ->
                                        seenContentDisposition = contentDisposition
                                        "pdfForm.pdf"
                                    }
                                )
                        ),
                )
                .apply {
                    currentUrl = "content://com.android.providers.downloads.documents/document/1"
                    currentTitle = "Untitled document - pdfForm.pdf"
                }
        engineSession.register(
            object : EngineSession.Observer {
                override fun onExternalResource(
                    url: String,
                    fileName: String?,
                    contentLength: Long?,
                    contentType: String?,
                    cookie: String?,
                    userAgent: String?,
                    isPrivate: Boolean,
                    skipConfirmation: Boolean,
                    openInApp: Boolean,
                    response: Response?,
                ) {
                    seenUrl = url
                }
            }
        )

        val pdfFileSaver = mock<SessionPdfFileSaver>()
        whenever(pdfFileSaver.save())
            .thenReturn(
                GeckoResult.fromValue(
                    WebResponse.Builder("blob:resource://pdf.js/0123")
                        .statusCode(200)
                        .addHeader("Content-Disposition", "attachment; filename=\"pdfForm.pdf\"")
                        .body("%PDF-1.4".byteInputStream())
                        .build()
                )
            )
        whenever(geckoSession.pdfFileSaver).thenReturn(pdfFileSaver)
        whenever(geckoSession.isPdfJs).thenReturn(GeckoResult.fromValue(true))

        engineSession.requestPdfToDownload()
        shadowOf(getMainLooper()).idle()

        assertEquals(
            "The URL comes from PDF.",
            "blob:resource://pdf.js/0123",
            seenUrl,
        )
        assertEquals(
            "The PDF's own file name names it, not the tab title.",
            "attachment; filename=\"pdfForm.pdf\";",
            seenContentDisposition,
        )
    }

    @Test
    fun `GIVEN the PDF has no usable file name WHEN requestPdfToDownload THEN the tab title names it`() {
        var seenContentDisposition: String? = null
        val engineSession =
            GeckoEngineSession(
                    runtime = mock(),
                    geckoSessionProvider = geckoSessionProvider,
                    defaultSettings =
                        DefaultSettings(
                            downloadDelegate =
                                FakeEngineDownloadDelegate(
                                    guessFileName = { contentDisposition, _, _ ->
                                        seenContentDisposition = contentDisposition
                                        "Mozilla.pdf"
                                    }
                                )
                        ),
                )
                .apply { currentTitle = "Mozilla" }

        val pdfFileSaver = mock<SessionPdfFileSaver>()
        whenever(pdfFileSaver.save())
            .thenReturn(
                GeckoResult.fromValue(
                    WebResponse.Builder("blob:resource://pdf.js/0123")
                        .statusCode(200)
                        .addHeader("Content-Disposition", "attachment; filename=\".pdf\"")
                        .body("%PDF-1.4".byteInputStream())
                        .build()
                )
            )
        whenever(geckoSession.pdfFileSaver).thenReturn(pdfFileSaver)
        whenever(geckoSession.isPdfJs).thenReturn(GeckoResult.fromValue(true))

        engineSession.requestPdfToDownload()
        shadowOf(getMainLooper()).idle()

        assertEquals("attachment; filename=\"Mozilla.pdf\";", seenContentDisposition)
    }

    @Test
    fun `GIVEN a PDF is showing the document WHEN requestPdfToShare THEN the response is given a filename`() {
        val engineSession = GeckoEngineSession(runtime = mock(), geckoSessionProvider = geckoSessionProvider)
        val pdfFileSaver = mock<SessionPdfFileSaver>()
        whenever(pdfFileSaver.save())
            .thenReturn(
                GeckoResult.fromValue(
                    WebResponse.Builder("blob:resource://pdf.js/0123")
                        .statusCode(200)
                        .addHeader("Content-Disposition", "attachment; filename=\"Test.pdf\"")
                        .body("%PDF-1.4".byteInputStream())
                        .build()
                )
            )
        whenever(geckoSession.pdfFileSaver).thenReturn(pdfFileSaver)
        whenever(geckoSession.isPdfJs).thenReturn(GeckoResult.fromValue(true))
        var sharedResponse: Response? = null

        engineSession.requestPdfToShare(onResult = { sharedResponse = it }, onException = {})
        shadowOf(getMainLooper()).idle()

        assertEquals(
            listOf("attachment; filename=\"Test.pdf\";"),
            sharedResponse?.headers?.getAll(CONTENT_DISPOSITION),
        )
        assertEquals("application/pdf", sharedResponse?.headers?.get(Headers.Names.CONTENT_TYPE))
    }

    @Test
    fun `WHEN requestPdfToDownload cannot return a result THEN do nothing`() {
        val engineSession =
            GeckoEngineSession(
                runtime = mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        engineSession.register(
            object : EngineSession.Observer {
                override fun onExternalResource(
                    url: String,
                    fileName: String?,
                    contentLength: Long?,
                    contentType: String?,
                    cookie: String?,
                    userAgent: String?,
                    isPrivate: Boolean,
                    skipConfirmation: Boolean,
                    openInApp: Boolean,
                    response: Response?,
                ) {
                    assert(false) { "We should not notify observers." }
                }
            }
        )

        whenever(geckoSession.isPdfJs).thenReturn(GeckoResult.fromValue(false))
        whenever(geckoSession.saveAsPdf())
            .thenReturn(GeckoResult.fromValue(null))
            .thenReturn(GeckoResult.fromException(IllegalStateException()))

        // When input stream in the GeckoResult is null.
        engineSession.requestPdfToDownload()
        shadowOf(getMainLooper()).idle()

        // When we receive an exception from the GeckoResult.
        engineSession.requestPdfToDownload()
        shadowOf(getMainLooper()).idle()
    }

    @Test
    fun `setDisplayMode sets same display mode value`() {
        val geckoSetting = mock<GeckoSessionSettings>()
        val geckoSession = mock<GeckoSession>()

        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        whenever(geckoSession.settings).thenReturn(geckoSetting)

        engineSession.geckoSession = geckoSession

        engineSession.setDisplayMode(WebAppManifest.DisplayMode.FULLSCREEN)
        verify(geckoSetting, atLeastOnce()).setDisplayMode(GeckoSessionSettings.DISPLAY_MODE_FULLSCREEN)

        engineSession.setDisplayMode(WebAppManifest.DisplayMode.STANDALONE)
        verify(geckoSetting, atLeastOnce()).setDisplayMode(GeckoSessionSettings.DISPLAY_MODE_STANDALONE)

        engineSession.setDisplayMode(WebAppManifest.DisplayMode.MINIMAL_UI)
        verify(geckoSetting, atLeastOnce()).setDisplayMode(GeckoSessionSettings.DISPLAY_MODE_MINIMAL_UI)

        engineSession.setDisplayMode(WebAppManifest.DisplayMode.BROWSER)
        verify(geckoSetting, atLeastOnce()).setDisplayMode(GeckoSessionSettings.DISPLAY_MODE_BROWSER)
    }

    fun `WHEN requestPrintContent is successful THEN notify of completion`() {
        val engineSession =
            GeckoEngineSession(
                runtime = mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        whenever(geckoSession.didPrintPageContent()).thenReturn(GeckoResult.fromValue(true))

        engineSession.register(
            object : EngineSession.Observer {
                override fun onPrintFinish() {
                    assert(true) { "We should notify of a successful print." }
                }

                override fun onPrintException(isPrint: Boolean, throwable: Throwable) {
                    assert(false) { "We should not notify of an exception." }
                }
            }
        )
        engineSession.requestPrintContent()
        shadowOf(getMainLooper()).idle()
    }

    @Test
    fun `WHEN requestPrintContent has an exception THEN do nothing`() {
        val engineSession =
            GeckoEngineSession(
                runtime = mock(),
                geckoSessionProvider = geckoSessionProvider,
            )
        class MockGeckoPrintException : GeckoPrintException()
        whenever(geckoSession.didPrintPageContent()).thenReturn(GeckoResult.fromException(MockGeckoPrintException()))

        engineSession.register(
            object : EngineSession.Observer {
                override fun onPrintFinish() {
                    assert(false) { "We should not notify of a successful print." }
                }

                override fun onPrintException(isPrint: Boolean, throwable: Throwable) {
                    assert(true) { "An exception should occur." }
                    assertEquals(
                        "A GeckoPrintException occurred.",
                        ERROR_PRINT_SETTINGS_SERVICE_NOT_AVAILABLE,
                        (throwable as GeckoPrintException).code,
                    )
                }
            }
        )
        engineSession.requestPrintContent()
        shadowOf(getMainLooper()).idle()
    }

    @Test
    fun `processBackPressed`() {
        val engineSession =
            GeckoEngineSession(
                mock(),
                geckoSessionProvider = geckoSessionProvider,
            )

        val ruleResult = GeckoResult<Boolean>()
        whenever(geckoSession.processBackPressed()).thenReturn(ruleResult)

        var onResultCalled = false
        engineSession.processBackPressed(onResult = { onResultCalled = true })

        ruleResult.complete(true)
        shadowOf(getMainLooper()).idle()

        assertTrue(onResultCalled)
    }

    private fun mockGeckoSession(): GeckoSession {
        val session = mock<GeckoSession>()
        whenever(session.settings).thenReturn(mock())
        return session
    }

    private fun mockLoadRequest(
        uri: String,
        triggerUri: String? = null,
        target: Int = 0,
        triggeredByRedirect: Boolean = false,
        hasUserGesture: Boolean = false,
        isDirectNavigation: Boolean = false,
    ): GeckoSession.NavigationDelegate.LoadRequest {
        var flags = 0
        if (triggeredByRedirect) {
            flags = flags or 0x800000
        }

        val constructor =
            GeckoSession.NavigationDelegate.LoadRequest::class
                .java
                .getDeclaredConstructor(
                    String::class.java,
                    String::class.java,
                    Int::class.java,
                    Int::class.java,
                    Boolean::class.java,
                    Boolean::class.java,
                )
        constructor.isAccessible = true

        return constructor.newInstance(uri, triggerUri, target, flags, hasUserGesture, isDirectNavigation)
    }
}
