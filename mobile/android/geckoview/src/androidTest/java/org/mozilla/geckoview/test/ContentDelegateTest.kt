/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test

import android.graphics.SurfaceTexture
import android.net.Uri
import android.view.PointerIcon
import android.view.Surface
import androidx.annotation.AnyThread
import androidx.core.net.toUri
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import java.io.ByteArrayInputStream
import org.hamcrest.Matchers.containsString
import org.hamcrest.Matchers.endsWith
import org.hamcrest.Matchers.equalTo
import org.hamcrest.Matchers.greaterThan
import org.hamcrest.Matchers.isEmptyOrNullString
import org.hamcrest.Matchers.not
import org.hamcrest.Matchers.notNullValue
import org.hamcrest.Matchers.startsWith
import org.json.JSONObject
import org.junit.Assume.assumeThat
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.AllowOrDeny
import org.mozilla.geckoview.GeckoDisplay.SurfaceInfo
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSession.ContentDelegate
import org.mozilla.geckoview.GeckoSession.NavigationDelegate
import org.mozilla.geckoview.GeckoSession.NavigationDelegate.LoadRequest
import org.mozilla.geckoview.GeckoSession.ProgressDelegate
import org.mozilla.geckoview.GeckoSessionSettings
import org.mozilla.geckoview.SlowScriptResponse
import org.mozilla.geckoview.WebResponse
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.AssertCalled
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.IgnoreCrash
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.NullDelegate
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.WithDisplay

@RunWith(AndroidJUnit4::class)
@MediumTest
class ContentDelegateTest : BaseSessionTest() {
    @Test
    fun titleChange() {
        mainSession.loadTestPath(TITLE_CHANGE_HTML_PATH)

        sessionRule.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 2)
                override fun onTitleChange(session: GeckoSession, title: String?) {
                    assertThat(
                        "Title should match",
                        title,
                        equalTo(forEachCall("Title1", "Title2")),
                    )
                }
            }
        )
    }

    @Test
    fun openInAppRequest() {
        // Testing WebResponse behavior
        val data = "Hello, World.".toByteArray()
        val fileHeader = "attachment; filename=\"hello-world.txt\""
        val requestExternal = true
        val skipConfirmation = true
        var response =
            WebResponse.Builder(HELLO_HTML_PATH)
                .statusCode(200)
                .body(ByteArrayInputStream(data))
                .addHeader("Content-Type", "application/txt")
                .addHeader("Content-Length", data.size.toString())
                .addHeader("Content-Disposition", fileHeader)
                .requestExternalApp(requestExternal)
                .skipConfirmation(skipConfirmation)
                .build()
        assertThat(
            "Filename matches as expected",
            response.headers["Content-Disposition"],
            equalTo(fileHeader),
        )
        assertThat(
            "Request external response matches as expected.",
            requestExternal,
            equalTo(response.requestExternalApp),
        )
        assertThat(
            "Skipping the confirmation matches as expected.",
            skipConfirmation,
            equalTo(response.skipConfirmation),
        )
    }

    @Test
    fun downloadOneRequest() {
        // disable test on pgo for frequently failing Bug 1543355
        assumeThat(sessionRule.env.isDebugBuild, equalTo(true))

        mainSession.loadTestPath(DOWNLOAD_HTML_PATH)

        sessionRule.waitUntilCalled(
            object : NavigationDelegate, ContentDelegate {

                @AssertCalled(count = 2)
                override fun onLoadRequest(session: GeckoSession, request: LoadRequest): GeckoResult<AllowOrDeny>? {
                    return null
                }

                @AssertCalled(false)
                override fun onNewSession(session: GeckoSession, uri: String): GeckoResult<GeckoSession>? {
                    return null
                }

                @AssertCalled(count = 1)
                override fun onExternalResponse(session: GeckoSession, response: WebResponse) {
                    assertThat("Uri should start with data:", response.uri, startsWith("blob:"))
                    assertThat(
                        "We should download the thing",
                        String(response.body?.readBytes()!!),
                        equalTo("Downloaded Data"),
                    )
                    // The headers below are special headers that we try to get for responses of any kind (http, blob,
                    // etc.)
                    // Note the case of the header keys. In the WebResponse object, all of them are lower case.
                    assertThat("Content type should match", response.headers.get("content-type"), equalTo("text/plain"))
                    assertThat(
                        "Content length should be non-zero",
                        response.headers.get("Content-Length")!!.toLong(),
                        greaterThan(0L),
                    )
                    assertThat(
                        "Filename should match",
                        response.headers.get("cONTent-diSPOsiTion"),
                        equalTo("attachment; filename=\"download.txt\""),
                    )
                    assertThat(
                        "Request external response should not be set.",
                        response.requestExternalApp,
                        equalTo(false),
                    )
                    assertThat(
                        "Should not skip the confirmation on a regular download.",
                        response.skipConfirmation,
                        equalTo(false),
                    )
                }
            }
        )
    }

    @Test
    fun downloadAnchorFilenameRequest() {
        // disable test on pgo for frequently failing Bug 1543355
        assumeThat(sessionRule.env.isDebugBuild, equalTo(true))

        mainSession.loadTestPath(DOWNLOAD_WEBPAGE_HTML_PATH)

        sessionRule.waitUntilCalled(
            object : NavigationDelegate, ContentDelegate {

                @AssertCalled(count = 2)
                override fun onLoadRequest(session: GeckoSession, request: LoadRequest): GeckoResult<AllowOrDeny>? {
                    return null
                }

                @AssertCalled(false)
                override fun onNewSession(session: GeckoSession, uri: String): GeckoResult<GeckoSession>? {
                    return null
                }

                @AssertCalled(count = 1)
                override fun onExternalResponse(session: GeckoSession, response: WebResponse) {
                    assertThat("Uri should be the anchor target", response.uri, endsWith(HELLO_HTML_PATH))
                    assertThat("We should download the item", response.body, notNullValue())
                    assertThat("Content type should match", response.headers["content-type"], equalTo("text/html"))
                    assertThat(
                        "Content length should be non-zero",
                        response.headers["Content-Length"]!!.toLong(),
                        greaterThan(0L),
                    )
                    assertThat(
                        "Filename from the download anchor attribute is in the content-disposition header",
                        response.headers["content-disposition"],
                        equalTo("attachment; filename=\"download-specified-name.html\""),
                    )
                    assertThat(
                        "Request external response should not be set.",
                        response.requestExternalApp,
                        equalTo(false),
                    )
                    assertThat(
                        "Should not skip the confirmation on a regular download.",
                        response.skipConfirmation,
                        equalTo(false),
                    )
                }
            }
        )
    }

    private val contentDisposition = "attachment; filename=\"server-name.txt\""

    private fun withContentDisposition(path: String, contentDisposition: String) =
        "$path?contentDisposition=${Uri.encode(contentDisposition)}"

    @Test
    fun downloadServerContentDisposition() {
        mainSession.loadTestPath(withContentDisposition(HELLO_HTML_PATH, contentDisposition))

        sessionRule.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onExternalResponse(session: GeckoSession, response: WebResponse) {
                    assertThat("Uri should be the served page", response.uri, containsString(HELLO_HTML_PATH))
                    assertThat("We should download the item", response.body, notNullValue())
                    assertThat("Content type should match", response.headers["content-type"], equalTo("text/html"))
                    assertThat(
                        "Content length should be non-zero",
                        response.headers["Content-Length"]!!.toLong(),
                        greaterThan(0L),
                    )
                    assertThat(
                        "Server-set Content-Disposition header is passed through unchanged",
                        response.headers["content-disposition"],
                        equalTo(contentDisposition),
                    )
                    assertThat(
                        "Request external response should not be set.",
                        response.requestExternalApp,
                        equalTo(false),
                    )
                    assertThat(
                        "Should not skip the confirmation on a regular download.",
                        response.skipConfirmation,
                        equalTo(false),
                    )
                }
            }
        )
    }

    @Test
    fun downloadServerHeaderVsBlobName() {
        mainSession.loadTestPath(withContentDisposition(DOWNLOAD_HTML_PATH, contentDisposition))

        sessionRule.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onExternalResponse(session: GeckoSession, response: WebResponse) {
                    assertThat("Uri should be the served page", response.uri, containsString(DOWNLOAD_HTML_PATH))
                    assertThat("We should download the item", response.body, notNullValue())
                    assertThat("Content type should match", response.headers["content-type"], equalTo("text/html"))
                    assertThat(
                        "Content length should be non-zero",
                        response.headers["Content-Length"]!!.toLong(),
                        greaterThan(0L),
                    )
                    assertThat(
                        "A page served with an attachment header and is a blob downloads under the server filename",
                        response.headers["content-disposition"],
                        equalTo(contentDisposition),
                    )
                    assertThat(
                        "Request external response should not be set.",
                        response.requestExternalApp,
                        equalTo(false),
                    )
                    assertThat(
                        "Should not skip the confirmation on a regular download.",
                        response.skipConfirmation,
                        equalTo(false),
                    )
                }
            }
        )
    }

    @Test
    fun downloadServerHeaderVsAnchorName() {
        mainSession.loadTestPath(withContentDisposition(DOWNLOAD_WEBPAGE_HTML_PATH, contentDisposition))

        sessionRule.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onExternalResponse(session: GeckoSession, response: WebResponse) {
                    assertThat(
                        "Uri should be the served page",
                        response.uri,
                        containsString(DOWNLOAD_WEBPAGE_HTML_PATH),
                    )
                    assertThat("We should download the item", response.body, notNullValue())
                    assertThat("Content type should match", response.headers["content-type"], equalTo("text/html"))
                    assertThat(
                        "Content length should be non-zero",
                        response.headers["Content-Length"]!!.toLong(),
                        greaterThan(0L),
                    )
                    assertThat(
                        "A page served with an attachment header and is a href downloads under the server filename",
                        response.headers["content-disposition"],
                        equalTo(contentDisposition),
                    )
                    assertThat(
                        "Request external response should not be set.",
                        response.requestExternalApp,
                        equalTo(false),
                    )
                    assertThat(
                        "Should not skip the confirmation on a regular download.",
                        response.skipConfirmation,
                        equalTo(false),
                    )
                }
            }
        )
    }

    private fun assertDownloadFileName(contentDisposition: String, expected: String) {
        mainSession.loadTestPath(withContentDisposition(HELLO_HTML_PATH, contentDisposition))

        sessionRule.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onExternalResponse(session: GeckoSession, response: WebResponse) {
                    assertThat(
                        "Content-Disposition filename is validated by Gecko",
                        response.headers["content-disposition"],
                        equalTo("attachment; filename=\"$expected\""),
                    )
                }
            }
        )
    }

    @Test
    fun downloadFileNameReplacesBidiOverride() {
        assertDownloadFileName("attachment; filename*=UTF-8''file%E2%80%AEtxt.html", "file_txt.html")
    }

    @Test
    fun downloadFileNameReplacesPathSeparators() {
        assertDownloadFileName("attachment; filename=\"dir/sub\\\\name.html\"", "dir_sub_name.html")
    }

    @Test
    fun downloadFileNameReplacesIllegalCharacters() {
        assertDownloadFileName("attachment; filename=\"a:b*c?d.html\"", "a_b_c_d.html")
    }

    @IgnoreCrash
    @Test
    fun crashContent() {
        // TODO: bug 1710940
        assumeThat(sessionRule.env.isIsolatedProcess, equalTo(false))
        assumeThat(sessionRule.env.isAppZygoteProcess, equalTo(false))

        mainSession.loadUri(CONTENT_CRASH_URL)
        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onCrash(session: GeckoSession) {
                    assertThat(
                        "Session should be closed after a crash",
                        session.isOpen,
                        equalTo(false),
                    )
                }
            }
        )

        // Recover immediately
        mainSession.open()
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    assertThat("Page should load successfully", success, equalTo(true))
                }
            }
        )
    }

    @IgnoreCrash
    @WithDisplay(width = 10, height = 10)
    @Test
    fun crashContent_tapAfterCrash() {
        // TODO: bug 1710940
        assumeThat(sessionRule.env.isIsolatedProcess, equalTo(false))
        assumeThat(sessionRule.env.isAppZygoteProcess, equalTo(false))

        mainSession.delegateUntilTestEnd(
            object : ContentDelegate {
                override fun onCrash(session: GeckoSession) {
                    mainSession.open()
                    mainSession.loadTestPath(HELLO_HTML_PATH)
                }
            }
        )

        mainSession.synthesizeTap(5, 5)
        mainSession.loadUri(CONTENT_CRASH_URL)
        mainSession.waitForPageStop()

        mainSession.synthesizeTap(5, 5)
        mainSession.reload()
        mainSession.waitForPageStop()
    }

    @AnyThread
    fun killAllContentProcesses() {
        val contentProcessPids = sessionRule.getAllSessionPids()
        for (pid in contentProcessPids) {
            sessionRule.killContentProcess(pid)
        }
    }

    @IgnoreCrash
    @Test
    fun killContent() {
        killAllContentProcesses()
        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onKill(session: GeckoSession) {
                    assertThat(
                        "Session should be closed after being killed",
                        session.isOpen,
                        equalTo(false),
                    )
                }
            }
        )

        mainSession.open()
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    assertThat("Page should load successfully", success, equalTo(true))
                }
            }
        )
    }

    private fun goFullscreen() {
        sessionRule.setPrefsUntilTestEnd(mapOf("full-screen-api.allow-trusted-requests-only" to false))
        mainSession.loadTestPath(FULLSCREEN_PATH)
        mainSession.waitForPageStop()
        val promise = mainSession.evaluatePromiseJS("document.querySelector('#fullscreen').requestFullscreen()")
        sessionRule.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onFullScreen(session: GeckoSession, fullScreen: Boolean) {
                    assertThat("Div went fullscreen", fullScreen, equalTo(true))
                }
            }
        )
        promise.value
    }

    private fun waitForFullscreenExit() {
        sessionRule.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onFullScreen(session: GeckoSession, fullScreen: Boolean) {
                    assertThat("Div left fullscreen", fullScreen, equalTo(false))
                }
            }
        )
    }

    @Test
    fun fullscreen() {
        goFullscreen()
        val promise = mainSession.evaluatePromiseJS("document.exitFullscreen()")
        waitForFullscreenExit()
        promise.value
    }

    @Test
    fun sessionExitFullscreen() {
        goFullscreen()
        mainSession.exitFullScreen()
        waitForFullscreenExit()
    }

    @Test
    fun firstComposite() {
        val display = mainSession.acquireDisplay()
        val texture = SurfaceTexture(0)
        texture.setDefaultBufferSize(100, 100)
        val surface = Surface(texture)
        display.surfaceChanged(SurfaceInfo.Builder(surface).size(100, 100).build())
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1) override fun onFirstComposite(session: GeckoSession) {}
            }
        )
        display.surfaceDestroyed()
        display.surfaceChanged(SurfaceInfo.Builder(surface).size(100, 100).build())
        sessionRule.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1) override fun onFirstComposite(session: GeckoSession) {}
            }
        )
        display.surfaceDestroyed()
        mainSession.releaseDisplay(display)
    }

    @WithDisplay(width = 10, height = 10)
    @Test
    fun firstContentfulPaint() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1) override fun onFirstContentfulPaint(session: GeckoSession) {}
            }
        )
    }

    @Test
    fun webAppManifestPref() {
        val initialState = sessionRule.runtime.settings.getWebManifestEnabled()
        val jsToRun = "document.querySelector('link[rel=manifest]').relList.supports('manifest');"

        // Check pref'ed off
        sessionRule.runtime.settings.setWebManifestEnabled(false)
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop(mainSession)

        var result = equalTo(mainSession.evaluateJS(jsToRun) as Boolean)

        assertThat("Disabling pref makes relList.supports('manifest') return false", false, result)

        // Check pref'ed on
        sessionRule.runtime.settings.setWebManifestEnabled(true)
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop(mainSession)

        result = equalTo(mainSession.evaluateJS(jsToRun) as Boolean)
        assertThat("Enabling pref makes relList.supports('manifest') return true", true, result)

        sessionRule.runtime.settings.setWebManifestEnabled(initialState)
    }

    @Test
    fun webAppManifest() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitUntilCalled(
            object : ContentDelegate, ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    assertThat("Page load should succeed", success, equalTo(true))
                }

                @AssertCalled(count = 1)
                override fun onWebAppManifest(session: GeckoSession, manifest: JSONObject) {
                    // These values come from the manifest at assets/www/manifest.webmanifest
                    assertThat("name should match", manifest.getString("name"), equalTo("App"))
                    assertThat("short_name should match", manifest.getString("short_name"), equalTo("app"))
                    assertThat("display should match", manifest.getString("display"), equalTo("standalone"))

                    // The color here is "cadetblue" converted to #aarrggbb.
                    assertThat("theme_color should match", manifest.getString("theme_color"), equalTo("#ff5f9ea0"))
                    assertThat(
                        "background_color should match",
                        manifest.getString("background_color"),
                        equalTo("#eec0ffee"),
                    )
                    assertThat(
                        "start_url should match",
                        manifest.getString("start_url"),
                        endsWith("/assets/www/start/index.html"),
                    )

                    val icon = manifest.getJSONArray("icons").getJSONObject(0)

                    val iconSrc = icon.getString("src").toUri()
                    assertThat("icon should have a valid src", iconSrc, notNullValue())
                    assertThat("icon src should be absolute", iconSrc.isAbsolute, equalTo(true))
                    assertThat("icon should have sizes", icon.getString("sizes"), not(isEmptyOrNullString()))
                    assertThat("icon type should match", icon.getString("type"), equalTo("image/gif"))
                }
            }
        )
    }

    @Test
    fun previewImage() {
        mainSession.loadTestPath(METATAGS_PATH)
        mainSession.waitUntilCalled(
            object : ContentDelegate, ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPreviewImage(session: GeckoSession, previewImageUrl: String) {
                    assertThat("Preview image should match", previewImageUrl, equalTo("https://test.com/og-image-url"))
                }
            }
        )
    }

    @Test
    fun viewportFit() {
        mainSession.loadTestPath(VIEWPORT_PATH)
        mainSession.waitUntilCalled(
            object : ContentDelegate, ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    assertThat("Page load should succeed", success, equalTo(true))
                }

                @AssertCalled(count = 1)
                override fun onMetaViewportFitChange(session: GeckoSession, viewportFit: String) {
                    assertThat("viewport-fit should match", viewportFit, equalTo("cover"))
                }
            }
        )

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitUntilCalled(
            object : ContentDelegate, ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    assertThat("Page load should succeed", success, equalTo(true))
                }

                @AssertCalled(count = 1)
                override fun onMetaViewportFitChange(session: GeckoSession, viewportFit: String) {
                    assertThat("viewport-fit should match", viewportFit, equalTo("auto"))
                }
            }
        )
    }

    @Test
    fun closeRequest() {
        if (!sessionRule.env.isAutomation) {
            sessionRule.setPrefsUntilTestEnd(mapOf("dom.allow_scripts_to_close_windows" to true))
        }

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        mainSession.evaluateJS("window.close()")
        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1) override fun onCloseRequest(session: GeckoSession) {}
            }
        )
    }

    @Test
    fun windowOpenClose() {
        sessionRule.setPrefsUntilTestEnd(mapOf("dom.disable_open_during_load" to false))

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        val newSession = sessionRule.createClosedSession()
        mainSession.delegateDuringNextWait(
            object : NavigationDelegate {
                @AssertCalled(count = 1)
                override fun onNewSession(session: GeckoSession, uri: String): GeckoResult<GeckoSession>? {
                    return GeckoResult.fromValue(newSession)
                }
            }
        )

        mainSession.evaluateJS("const w = window.open('about:blank'); w.close()")

        newSession.waitUntilCalled(
            object : ContentDelegate, ProgressDelegate {
                @AssertCalled(count = 1) override fun onCloseRequest(session: GeckoSession) {}

                // (1) artificial from GeckoViewProgress._fireInitialLoad (2) about:blank load
                @AssertCalled(count = 2) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun setCursor() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        mainSession.evaluateJS("document.body.style.cursor = 'wait'")
        mainSession.synthesizeMouseMove(50, 50)

        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onPointerIconChange(session: GeckoSession, icon: PointerIcon) {
                    // PointerIcon has no compare method.
                }
            }
        )

        val delegate = mainSession.contentDelegate
        mainSession.contentDelegate = null
        mainSession.evaluateJS("document.body.style.cursor = 'text'")
        for (i in 51..70) {
            mainSession.synthesizeMouseMove(i, 50)
            // No wait function since we remove content delegate.
            mainSession.waitForJS("new Promise(resolve => window.setTimeout(resolve, 100))")
        }
        mainSession.contentDelegate = delegate
    }

    /** Preferences to induce wanted behaviour. */
    private fun setHangReportTestPrefs(timeout: Int = 20000) {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "dom.max_script_run_time" to 1,
                "dom.max_chrome_script_run_time" to 1,
                "dom.max_ext_content_script_run_time" to 1,
                "dom.ipc.reply.timeout" to 100,
                "browser.hangNotification.waitPeriod" to timeout,
            )
        )
    }

    /** With no delegate set, the default behaviour is to stop hung scripts. */
    @NullDelegate(ContentDelegate::class)
    @Test
    fun stopHungProcessDefault() {
        setHangReportTestPrefs()
        mainSession.loadTestPath(HUNG_SCRIPT)
        sessionRule.delegateUntilTestEnd(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    assertThat(
                        "The script did not complete.",
                        mainSession.evaluateJS("document.getElementById(\"content\").innerHTML") as String,
                        equalTo("Started"),
                    )
                }
            }
        )
        sessionRule.waitForPageStop(mainSession)
    }

    /** With no overriding implementation for onSlowScript, the default behaviour is to stop hung scripts. */
    @Test
    fun stopHungProcessNull() {
        setHangReportTestPrefs()
        sessionRule.delegateUntilTestEnd(
            object : ContentDelegate, ProgressDelegate {
                // default onSlowScript returns null
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    assertThat(
                        "The script did not complete.",
                        mainSession.evaluateJS("document.getElementById(\"content\").innerHTML") as String,
                        equalTo("Started"),
                    )
                }
            }
        )
        mainSession.loadTestPath(HUNG_SCRIPT)
        sessionRule.waitForPageStop(mainSession)
    }

    /** Test that, with a 'do nothing' delegate, the hung process completes after its delay */
    @Test
    fun stopHungProcessDoNothing() {
        setHangReportTestPrefs()
        var scriptHungReportCount = 0
        sessionRule.delegateUntilTestEnd(
            object : ContentDelegate, ProgressDelegate {
                @AssertCalled()
                override fun onSlowScript(
                    geckoSession: GeckoSession,
                    scriptFileName: String,
                ): GeckoResult<SlowScriptResponse> {
                    scriptHungReportCount += 1
                    return GeckoResult.fromValue(null)
                }

                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    assertThat(
                        "The delegate was informed of the hang repeatedly",
                        scriptHungReportCount,
                        greaterThan(1),
                    )
                    assertThat(
                        "The script did complete.",
                        mainSession.evaluateJS("document.getElementById(\"content\").innerHTML") as String,
                        equalTo("Finished"),
                    )
                }
            }
        )
        mainSession.loadTestPath(HUNG_SCRIPT)
        sessionRule.waitForPageStop(mainSession)
    }

    /** Test that the delegate is called and can stop a hung script */
    @Test
    fun stopHungProcess() {
        setHangReportTestPrefs()
        sessionRule.delegateUntilTestEnd(
            object : ContentDelegate, ProgressDelegate {
                @AssertCalled(count = 1, order = [1])
                override fun onSlowScript(
                    geckoSession: GeckoSession,
                    scriptFileName: String,
                ): GeckoResult<SlowScriptResponse> {
                    return GeckoResult.fromValue(SlowScriptResponse.STOP)
                }

                @AssertCalled(count = 1, order = [2])
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    assertThat(
                        "The script did not complete.",
                        mainSession.evaluateJS("document.getElementById(\"content\").innerHTML") as String,
                        equalTo("Started"),
                    )
                }
            }
        )
        mainSession.loadTestPath(HUNG_SCRIPT)
        sessionRule.waitForPageStop(mainSession)
    }

    /** Test that the delegate is called and can continue executing hung scripts */
    @Test
    fun stopHungProcessWait() {
        setHangReportTestPrefs()
        sessionRule.delegateUntilTestEnd(
            object : ContentDelegate, ProgressDelegate {
                @AssertCalled(count = 1, order = [1])
                override fun onSlowScript(
                    geckoSession: GeckoSession,
                    scriptFileName: String,
                ): GeckoResult<SlowScriptResponse> {
                    return GeckoResult.fromValue(SlowScriptResponse.CONTINUE)
                }

                @AssertCalled(count = 1, order = [2])
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    assertThat(
                        "The script did complete.",
                        mainSession.evaluateJS("document.getElementById(\"content\").innerHTML") as String,
                        equalTo("Finished"),
                    )
                }
            }
        )
        mainSession.loadTestPath(HUNG_SCRIPT)
        sessionRule.waitForPageStop(mainSession)
    }

    /** Test that the delegate is called and paused scripts re-notify after the wait period */
    @Test
    fun stopHungProcessWaitThenStop() {
        setHangReportTestPrefs(500)
        var scriptWaited = false
        sessionRule.delegateUntilTestEnd(
            object : ContentDelegate, ProgressDelegate {
                @AssertCalled(count = 2, order = [1, 2])
                override fun onSlowScript(
                    geckoSession: GeckoSession,
                    scriptFileName: String,
                ): GeckoResult<SlowScriptResponse> {
                    return if (!scriptWaited) {
                        scriptWaited = true
                        GeckoResult.fromValue(SlowScriptResponse.CONTINUE)
                    } else {
                        GeckoResult.fromValue(SlowScriptResponse.STOP)
                    }
                }

                @AssertCalled(count = 1, order = [3])
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    assertThat(
                        "The script did not complete.",
                        mainSession.evaluateJS("document.getElementById(\"content\").innerHTML") as String,
                        equalTo("Started"),
                    )
                }
            }
        )
        mainSession.loadTestPath(HUNG_SCRIPT)
        sessionRule.waitForPageStop(mainSession)
    }

    /** Test that the display mode is applied to CSS media query */
    @Test
    fun displayMode() {
        val pwaSession =
            sessionRule.createOpenSession(
                GeckoSessionSettings.Builder(mainSession.settings)
                    .displayMode(GeckoSessionSettings.DISPLAY_MODE_FULLSCREEN)
                    .build()
            )
        pwaSession.loadTestPath(HELLO_HTML_PATH)
        pwaSession.waitForPageStop()

        val matches = pwaSession.evaluateJS("window.matchMedia('(display-mode: fullscreen)').matches") as Boolean
        assertThat(
            "display-mode should be fullscreen",
            matches,
            equalTo(true),
        )
    }

    @Test
    fun closeWatcherForDialog() {
        mainSession.loadTestPath(DIALOG_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.waitForResult(mainSession.processBackPressed()).let {
            assertThat(
                "processBackPressed should be false due to no close watch request",
                it,
                equalTo(false),
            )
        }

        // dialog has no open event.
        mainSession.waitForJS(
            """
            new Promise(resolve => {
                const dialog = document.querySelector('dialog');
                dialog.showModal();
                window.setTimeout(resolve, 500);
            })
            """
        )

        val promise =
            mainSession.evaluatePromiseJS(
                """
            new Promise(resolve => {
                let cancel = false;
                const dialog = document.querySelector('dialog');
                dialog.addEventListener('cancel', () => { cancel = true; });
                dialog.addEventListener('close', () => resolve(cancel));
            })
            """
            )

        sessionRule.waitForResult(mainSession.processBackPressed()).let {
            assertThat(
                "processBackPressed should be true due to having close watch request",
                it,
                equalTo(true),
            )
        }
        assertThat("processBackPressed fires cancel event", promise.value as Boolean, equalTo(true))
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun closeWatcherForPopover() {
        mainSession.loadTestPath(POPOVER_HTML_PATH)
        mainSession.waitForPageStop()

        val promise1 =
            mainSession.evaluatePromiseJS(
                """
            new Promise(resolve => {
                let popover = document.getElementById('mypopover');
                popover.addEventListener('toggle', resolve, { once: true });
            });
            """
            )
        mainSession.synthesizeTap(50, 50)
        promise1.value

        val promise2 =
            mainSession.evaluatePromiseJS(
                """
            new Promise(resolve => {
                let popover = document.getElementById('mypopover');
                popover.addEventListener('toggle', () => resolve(true), { once: true });
            })
            """
            )

        sessionRule.waitForResult(mainSession.processBackPressed()).let {
            assertThat(
                "processBackPressed should be true due to having close watch request",
                it,
                equalTo(true),
            )
        }
        assertThat(
            "processBackPressed fires cancel event",
            promise2.value as Boolean,
            equalTo(true),
        )
    }
}
