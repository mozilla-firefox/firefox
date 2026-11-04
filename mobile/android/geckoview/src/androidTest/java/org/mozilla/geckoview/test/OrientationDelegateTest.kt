/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test

import android.content.pm.ActivityInfo
import android.content.res.Configuration
import androidx.test.ext.junit.rules.ActivityScenarioRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import org.hamcrest.Matchers.equalTo
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith
import org.mozilla.geckoview.AllowOrDeny
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSession.ContentDelegate
import org.mozilla.geckoview.OrientationController
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.AssertCalled
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.WithDisplay

@RunWith(AndroidJUnit4::class)
@MediumTest
class OrientationDelegateTest : BaseSessionTest() {
    val activityRule = ActivityScenarioRule(GeckoViewTestActivity::class.java)

    private val iframeOrientation = "document.querySelector('iframe').contentWindow.screen.orientation"

    @get:Rule override val rules: RuleChain = RuleChain.outerRule(activityRule).around(sessionRule)

    @Before
    fun setup() {
        sessionRule.setPrefsUntilTestEnd(mapOf("dom.screenorientation.allow-lock" to true))
    }

    private fun goFullscreen() {
        mainSession.loadTestPath(FULLSCREEN_PATH)
        mainSession.waitForPageStop()
        enterFullscreen()
    }

    private fun enterFullscreen(doc: String = "document") {
        sessionRule.setPrefsUntilTestEnd(mapOf("full-screen-api.allow-trusted-requests-only" to false))
        val promise = mainSession.evaluatePromiseJS("$doc.querySelector('#fullscreen').requestFullscreen()")
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

    private fun goFullscreenInIframe() {
        mainSession.loadTestPath(FULLSCREEN_PATH)
        mainSession.waitForPageStop()
        mainSession
            .evaluatePromiseJS(
                """
                new Promise(resolve => {
                    const iframe = document.createElement('iframe');
                    iframe.allowFullscreen = true;
                    iframe.onload = resolve;
                    iframe.src = '$FULLSCREEN_PATH';
                    document.body.append(iframe);
                })
                """
                    .trimIndent()
            )
            .value
        enterFullscreen("document.querySelector('iframe').contentDocument")
    }

    private fun exitFullscreenAndWaitForUnlock(doc: String = "document") {
        val promise = mainSession.evaluatePromiseJS("$doc.exitFullscreen()")
        sessionRule.waitUntilCalled(
            object : ContentDelegate, OrientationController.OrientationDelegate {
                @AssertCalled(count = 1)
                override fun onFullScreen(session: GeckoSession, fullScreen: Boolean) {
                    assertThat("Exited fullscreen", fullScreen, equalTo(false))
                }

                @AssertCalled(count = 1)
                override fun onOrientationUnlock() {
                    activityRule.scenario.onActivity { activity ->
                        activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
                    }
                }
            }
        )
        promise.value
    }

    private fun waitForUnlock() {
        sessionRule.waitUntilCalled(
            object : OrientationController.OrientationDelegate {
                @AssertCalled(count = 1)
                override fun onOrientationUnlock() {
                    activityRule.scenario.onActivity { activity ->
                        activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
                    }
                }
            }
        )
    }

    private fun lock(type: String, expected: Int, configuration: Int, orientation: String = "screen.orientation") {
        sessionRule.delegateDuringNextWait(
            object : OrientationController.OrientationDelegate {
                @AssertCalled(count = 1)
                override fun onOrientationLock(aOrientation: Int): GeckoResult<AllowOrDeny> {
                    assertThat("The orientation should be $type", aOrientation, equalTo(expected))
                    activityRule.scenario.onActivity { activity ->
                        activity.requestedOrientation = aOrientation
                    }
                    return GeckoResult.allow()
                }
            }
        )
        val promise = mainSession.evaluatePromiseJS("$orientation.lock('$type')")
        sessionRule.waitUntilCalled(OrientationController.OrientationDelegate::class, "onOrientationLock")
        sessionRule.runtime.orientationChanged(configuration)
        promise.value
    }

    private fun lockPortrait() =
        lock("portrait-primary", ActivityInfo.SCREEN_ORIENTATION_PORTRAIT, Configuration.ORIENTATION_PORTRAIT)

    private fun lockLandscape(orientation: String = "screen.orientation") =
        lock(
            "landscape-primary",
            ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE,
            Configuration.ORIENTATION_LANDSCAPE,
            orientation,
        )

    @Test
    fun orientationLock() {
        goFullscreen()
        activityRule.scenario.onActivity { activity ->
            // If the orientation is landscape, lock to portrait and wait for delegate. If portrait, lock to landscape
            // instead.
            if (activity.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
                lockPortrait()
            } else if (activity.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT) {
                lockLandscape()
            }
        }
    }

    @Test
    fun orientationUnlock() {
        goFullscreen()
        mainSession.evaluateJS("screen.orientation.unlock()")
        waitForUnlock()
    }

    @Test
    fun orientationLockedAlready() {
        goFullscreen()
        // Lock to landscape twice to verify successful locking with existing lock
        lockLandscape()
        lockLandscape()
    }

    @Test
    fun orientationLockedExistingOrientation() {
        goFullscreen()

        val promise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => {
                    if (screen.orientation.type == "landscape-primary") {
                        resolve();
                    }
                    screen.orientation.addEventListener("change", e => {
                        if (screen.orientation.type == "landscape-primary") {
                            resolve();
                        }
                    }, { once: true });
                })
                """
                    .trimIndent()
            )

        // Lock to landscape twice to verify successful locking to existing orientation
        activityRule.scenario.onActivity { activity ->
            activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        }
        // Wait for orientation change by activity.requestedOrientation.
        promise.value
        lockLandscape()
    }

    @Test(expected = GeckoSessionTestRule.RejectedPromiseException::class)
    fun orientationLockNoFullscreen() {
        // Verify if fullscreen pre-lock conditions are not met, a rejected promise is returned.
        mainSession.loadTestPath(FULLSCREEN_PATH)
        mainSession.waitForPageStop()
        mainSession.evaluateJS("screen.orientation.lock('landscape-primary')")
    }

    @Test
    fun orientationLockUnlock() {
        goFullscreen()

        lockLandscape()

        // after locking to orientation landscape, unlock to default
        mainSession.evaluateJS("screen.orientation.unlock()")
        sessionRule.waitUntilCalled(
            object : OrientationController.OrientationDelegate {
                @AssertCalled(count = 1)
                override fun onOrientationUnlock() {
                    activityRule.scenario.onActivity { activity ->
                        activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
                    }
                }
            }
        )
    }

    @Test
    fun orientationLockUnsupported() {
        // If no delegate, orientation.lock must throws NotSupportedError
        goFullscreen()

        val promise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(r => {
                  screen.orientation.lock('landscape-primary')
                  .then(() => r("successful"))
                  .catch(e => r(e.name))
                })
                """
                    .trimIndent()
            )

        assertThat(
            "The operation must throw NotSupportedError",
            promise.value,
            equalTo("NotSupportedError"),
        )

        val promise2 =
            mainSession.evaluatePromiseJS(
                """
                new Promise(r => {
                  screen.orientation.lock(screen.orientation.type)
                  .then(() => r("successful"))
                  .catch(e => r(e.name))
                })
                """
                    .trimIndent()
            )

        assertThat(
            "The operation must throw NotSupportedError even if same orientation",
            promise2.value,
            equalTo("NotSupportedError"),
        )
    }

    @WithDisplay(width = 300, height = 200)
    @Test
    fun orientationUnlockByExitFullscreen() {
        goFullscreen()
        activityRule.scenario.onActivity { activity ->
            // If the orientation is landscape, lock to portrait and wait for delegate. If portrait, lock to landscape
            // instead.
            if (activity.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
                lockPortrait()
            } else if (activity.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT) {
                lockLandscape()
            }
        }

        exitFullscreenAndWaitForUnlock()
    }

    @Test
    fun orientationUnlockByRemovingFullscreenIframe() {
        goFullscreenInIframe()
        lockLandscape(iframeOrientation)
        mainSession.evaluateJS("document.querySelector('iframe').remove()")
        waitForUnlock()
    }

    @Test
    fun orientationUnlockByNavigatingFullscreenIframe() {
        goFullscreenInIframe()
        lockLandscape(iframeOrientation)
        mainSession.evaluateJS("document.querySelector('iframe').src = '$HELLO_HTML_PATH'")
        waitForUnlock()
    }

    @WithDisplay(width = 300, height = 200)
    @Test
    fun orientationLockKeptWhenUnlockedIframeIsRemoved() {
        goFullscreenInIframe()
        lockLandscape(iframeOrientation)
        exitFullscreenAndWaitForUnlock("document.querySelector('iframe').contentDocument")

        enterFullscreen()
        lockPortrait()
        sessionRule.delegateDuringNextWait(
            object : OrientationController.OrientationDelegate {
                @AssertCalled(false) override fun onOrientationUnlock() {}
            }
        )
        mainSession.evaluateJS("document.querySelector('iframe').remove()")
        mainSession.waitForRoundTrip()

        exitFullscreenAndWaitForUnlock()
    }

    @Test
    fun orientationUnlockByClosingSession() {
        goFullscreen()
        lockLandscape()
        mainSession.close()
        waitForUnlock()
    }

    @WithDisplay(width = 200, height = 300)
    @Test
    fun orientationNatural() {
        goFullscreen()

        // Set orientation to landscape since natural is portrait.
        var promise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => {
                    if (screen.orientation.type == "landscape-primary") {
                        resolve();
                    }
                    screen.orientation.addEventListener("change", e => {
                        if (screen.orientation.type == "landscape-primary") {
                            resolve();
                        }
                    }, { once: true });
                })
                """
                    .trimIndent()
            )

        activityRule.scenario.onActivity { activity ->
            activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        }
        // Wait for orientation change by activity.requestedOrientation.
        promise.value

        sessionRule.delegateDuringNextWait(
            object : OrientationController.OrientationDelegate {
                @AssertCalled(count = 1)
                override fun onOrientationLock(aOrientation: Int): GeckoResult<AllowOrDeny> {
                    assertThat(
                        "The orientation should be portrait",
                        aOrientation,
                        equalTo(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT),
                    )
                    activityRule.scenario.onActivity { activity ->
                        activity.requestedOrientation = aOrientation
                    }
                    return GeckoResult.allow()
                }
            }
        )
        promise = mainSession.evaluatePromiseJS("screen.orientation.lock('natural')")
        promise.value
        // Remove previous delegate
        mainSession.waitForRoundTrip()
    }
}
