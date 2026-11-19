/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test

import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.MotionEvent
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import org.hamcrest.Matchers.containsString
import org.hamcrest.Matchers.equalTo
import org.hamcrest.Matchers.notNullValue
import org.hamcrest.Matchers.nullValue
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assume.assumeThat
import org.junit.Ignore
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSession.ContentDelegate
import org.mozilla.geckoview.GeckoSession.HistoryDelegate
import org.mozilla.geckoview.GeckoSession.NavigationDelegate
import org.mozilla.geckoview.GeckoSession.ProgressDelegate
import org.mozilla.geckoview.GeckoSession.PromptDelegate
import org.mozilla.geckoview.GeckoSession.ScrollDelegate
import org.mozilla.geckoview.GeckoSession.SessionState
import org.mozilla.geckoview.GeckoSessionSettings
import org.mozilla.geckoview.WebRequestError
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.AssertCalled
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.ChildCrashedException
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.ClosedSessionAtStart
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.IgnoreCrash
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.NullDelegate
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.RejectedPromiseException
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.Setting
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.ShouldContinue
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.TimeoutMillis
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.WithDisplay
import org.mozilla.geckoview.test.util.UiThreadUtils

/**
 * Test for the GeckoSessionTestRule class, to ensure it properly sets up a session for each test, and to ensure it can
 * properly wait for and assert delegate callbacks.
 */
@RunWith(AndroidJUnit4::class)
@MediumTest
class GeckoSessionTestRuleTest : BaseSessionTest(noErrorCollector = true) {

    @Test
    fun getSession() {
        assertThat("Can get session", mainSession, notNullValue())
        assertThat(
            "Session is open",
            mainSession.isOpen,
            equalTo(true),
        )
    }

    @ClosedSessionAtStart
    @Test
    fun getSession_closedSession() {
        assertThat("Session is closed", mainSession.isOpen, equalTo(false))
    }

    @Setting.List(
        Setting(key = Setting.Key.USE_PRIVATE_MODE, value = "true"),
        Setting(key = Setting.Key.DISPLAY_MODE, value = "DISPLAY_MODE_MINIMAL_UI"),
        Setting(key = Setting.Key.ALLOW_JAVASCRIPT, value = "false"),
    )
    @Setting(key = Setting.Key.USE_TRACKING_PROTECTION, value = "true")
    @Test
    fun settingsApplied() {
        assertThat(
            "USE_PRIVATE_MODE should be set",
            mainSession.settings.usePrivateMode,
            equalTo(true),
        )
        assertThat(
            "DISPLAY_MODE should be set",
            mainSession.settings.displayMode,
            equalTo(GeckoSessionSettings.DISPLAY_MODE_MINIMAL_UI),
        )
        assertThat(
            "USE_TRACKING_PROTECTION should be set",
            mainSession.settings.useTrackingProtection,
            equalTo(true),
        )
        assertThat(
            "ALLOW_JAVASCRIPT should be set",
            mainSession.settings.allowJavascript,
            equalTo(false),
        )
    }

    @Test(expected = UiThreadUtils.TimeoutException::class)
    @TimeoutMillis(2000)
    fun noPendingCallbacks() {
        // Make sure we don't have unexpected pending callbacks at the start of a test.
        sessionRule.waitUntilCalled(
            object : ProgressDelegate, HistoryDelegate {
                // There may be extraneous onSessionStateChange and onHistoryStateChange calls
                // after a test, so ignore the first received.
                @AssertCalled(count = 2)
                override fun onSessionStateChange(session: GeckoSession, state: SessionState) {}

                @AssertCalled(count = 2)
                override fun onHistoryStateChange(session: GeckoSession, historyList: HistoryDelegate.HistoryList) {}
            }
        )
    }

    @NullDelegate.List(
        NullDelegate(ContentDelegate::class),
        NullDelegate(NavigationDelegate::class),
    )
    @NullDelegate(ScrollDelegate::class)
    @Test
    fun nullDelegate() {
        assertThat(
            "Content delegate should be null",
            mainSession.contentDelegate,
            nullValue(),
        )
        assertThat(
            "Navigation delegate should be null",
            mainSession.navigationDelegate,
            nullValue(),
        )
        assertThat(
            "Scroll delegate should be null",
            mainSession.scrollDelegate,
            nullValue(),
        )

        assertThat(
            "Progress delegate should not be null",
            mainSession.progressDelegate,
            notNullValue(),
        )
    }

    @NullDelegate(ProgressDelegate::class)
    @ClosedSessionAtStart
    @Test
    fun nullDelegate_closed() {
        assertThat(
            "Progress delegate should be null",
            mainSession.progressDelegate,
            nullValue(),
        )
    }

    @Test(expected = AssertionError::class)
    @NullDelegate(ProgressDelegate::class)
    @ClosedSessionAtStart
    fun nullDelegate_requireProgressOnOpen() {
        assertThat(
            "Progress delegate should be null",
            mainSession.progressDelegate,
            nullValue(),
        )

        mainSession.open()
    }

    @Test
    fun waitForPageStop() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        var counter = 0

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(1))
    }

    @Test
    fun waitForPageStops() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.reload()
        sessionRule.waitForPageStops(2)

        var counter = 0

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(2))
    }

    @Test(expected = AssertionError::class)
    @NullDelegate(ProgressDelegate::class)
    @ClosedSessionAtStart
    fun waitForPageStops_throwOnNullDelegate() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        mainSession.open(sessionRule.runtime) // Avoid waiting for initial load
        mainSession.reload()
        mainSession.waitForPageStops(2)
    }

    @Test
    fun waitUntilCalled_anyInterfaceMethod() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitUntilCalled(ProgressDelegate::class)

        var counter = 0

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                override fun onPageStart(session: GeckoSession, url: String) {
                    counter++
                }

                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }

                override fun onSecurityChange(
                    session: GeckoSession,
                    securityInfo: ProgressDelegate.SecurityInformation,
                ) {
                    counter++
                }

                override fun onProgressChange(session: GeckoSession, progress: Int) {
                    counter++
                }

                override fun onSessionStateChange(session: GeckoSession, state: SessionState) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(1))
    }

    @Test
    fun waitUntilCalled_specificInterfaceMethod() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitUntilCalled(
            ProgressDelegate::class,
            "onPageStart",
            "onPageStop",
        )

        var counter = 0

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                override fun onPageStart(session: GeckoSession, url: String) {
                    counter++
                }

                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(2))
    }

    private interface OverloadedDelegate {
        fun onDelegate(): Int

        fun onDelegate(foo: String): Int
    }

    @Test
    fun waitUntilCalled_anyOverload() {
        lateinit var delegate: OverloadedDelegate

        sessionRule.addExternalDelegateUntilTestEnd(
            OverloadedDelegate::class,
            { newDelegate -> delegate = newDelegate },
            {},
            object : OverloadedDelegate {
                override fun onDelegate(): Int = 24

                @AssertCalled(count = 1) override fun onDelegate(foo: String): Int = 42
            },
        )

        delegate.onDelegate("foo")
        sessionRule.waitUntilCalled(OverloadedDelegate::class, "onDelegate")
    }

    @Test
    fun waitUntilCalled_shouldContinue() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitUntilCalled(
            object : ProgressDelegate, ShouldContinue {
                var pageStart = false

                override fun shouldContinue(): Boolean = pageStart

                override fun onPageStart(session: GeckoSession, url: String) {
                    pageStart = true
                }

                // This is here to verify that we don't wait on all methods of this object
                override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )

        // This is to verify that the above only waits until pageStart, but not pageStop.
        // If the above block waits until pageStop, this will time out, indicating a problem.
        sessionRule.waitForPageStop()
    }

    @Test(expected = AssertionError::class)
    fun waitUntilCalled_throwOnNotGeckoSessionInterface() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitUntilCalled(CharSequence::class)
    }

    fun waitUntilCalled_notThrowOnCallbackInterface() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitUntilCalled(ProgressDelegate::class)
    }

    @NullDelegate(ScrollDelegate::class)
    @Test
    fun waitUntilCalled_notThrowOnNonNullDelegateMethod() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        mainSession.reload()
        mainSession.waitUntilCalled(ProgressDelegate::class, "onPageStop")
    }

    @Test
    fun waitUntilCalled_anyObjectMethod() {
        mainSession.loadTestPath(HELLO_HTML_PATH)

        var counter = 0

        sessionRule.waitUntilCalled(
            object : ProgressDelegate {
                override fun onPageStart(session: GeckoSession, url: String) {
                    counter++
                }

                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }

                override fun onSecurityChange(
                    session: GeckoSession,
                    securityInfo: ProgressDelegate.SecurityInformation,
                ) {
                    counter++
                }

                override fun onProgressChange(session: GeckoSession, progress: Int) {
                    counter++
                }

                override fun onSessionStateChange(session: GeckoSession, state: SessionState) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(1))
    }

    @Test
    fun waitUntilCalled_specificObjectMethod() {
        mainSession.loadTestPath(HELLO_HTML_PATH)

        var counter = 0

        sessionRule.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled
                override fun onPageStart(session: GeckoSession, url: String) {
                    counter++
                }

                @AssertCalled
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(2))
    }

    @Test(expected = AssertionError::class)
    @NullDelegate(ScrollDelegate::class)
    fun waitUntilCalled_throwOnNullDelegateObject() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        mainSession.reload()
        mainSession.waitUntilCalled(
            object : ScrollDelegate {
                @AssertCalled override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {}
            }
        )
    }

    @NullDelegate(ScrollDelegate::class)
    @Test
    fun waitUntilCalled_notThrowOnNonNullDelegateObject() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        mainSession.reload()
        mainSession.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test
    fun waitUntilCalled_multipleCount() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.reload()

        var counter = 0

        sessionRule.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled(count = 2)
                override fun onPageStart(session: GeckoSession, url: String) {
                    counter++
                }

                @AssertCalled(count = 2)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(4))
    }

    @Test
    fun waitUntilCalled_currentCall() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.reload()

        var counter = 0

        sessionRule.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled(count = 2, order = [1, 2])
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    val info = sessionRule.currentCall
                    assertThat("Method info should be valid", info, notNullValue())
                    assertThat(
                        "Counter should be correct",
                        info.counter,
                        equalTo(forEachCall(1, 2)),
                    )
                    assertThat(
                        "Order should equal counter",
                        info.order,
                        equalTo(info.counter),
                    )
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(2))
    }

    @Test(expected = IllegalStateException::class)
    fun waitUntilCalled_passThroughExceptions() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    throw IllegalStateException()
                }
            }
        )
    }

    @Test
    fun waitUntilCalled_zeroCount() {
        // Support having @AssertCalled(count = 0) annotations for waitUntilCalled calls.
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitUntilCalled(
            object : ProgressDelegate, ScrollDelegate {
                @AssertCalled(count = 1) override fun onPageStop(session: GeckoSession, success: Boolean) {}

                @AssertCalled(count = 0)
                override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {}
            }
        )
    }

    @Test
    fun forCallbacksDuringWait_anyMethod() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        var counter = 0

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(1))
    }

    @Test(expected = AssertionError::class)
    fun forCallbacksDuringWait_throwOnAnyMethodNotCalled() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        sessionRule.forCallbacksDuringWait(object : ScrollDelegate {})
    }

    @Test(expected = AssertionError::class)
    fun forCallbacksDuringWait_enforcesAssertCalledOnBridgedOverride() {
        // Regression test for bug 1948997: onLocationChange is reached through a
        // synthetic bridge (its hasUserGesture param is a boxed Boolean in the
        // Java interface), and the rule must still honor the annotation on the
        // real override. count = 0 has to fail since the load fires it.
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        sessionRule.forCallbacksDuringWait(
            object : NavigationDelegate {
                @AssertCalled(count = 0)
                override fun onLocationChange(
                    session: GeckoSession,
                    url: String?,
                    perms: MutableList<GeckoSession.PermissionDelegate.ContentPermission>,
                    hasUserGesture: Boolean,
                ) {}
            }
        )
    }

    @Test
    fun forCallbacksDuringWait_specificMethod() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        var counter = 0

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled
                override fun onPageStart(session: GeckoSession, url: String) {
                    counter++
                }

                @AssertCalled
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(2))
    }

    @Test
    fun forCallbacksDuringWait_specificMethodMultipleTimes() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.reload()
        sessionRule.waitForPageStops(2)

        var counter = 0

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled
                override fun onPageStart(session: GeckoSession, url: String) {
                    counter++
                }

                @AssertCalled
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(4))
    }

    @Test(expected = AssertionError::class)
    fun forCallbacksDuringWait_throwOnSpecificMethodNotCalled() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        sessionRule.forCallbacksDuringWait(
            object : ScrollDelegate {
                @AssertCalled override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {}
            }
        )
    }

    @Test
    fun waitUntilCalled_specificCount() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.reload()

        var counter = 0

        sessionRule.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled(count = 2)
                override fun onPageStart(session: GeckoSession, url: String) {
                    counter++
                }

                @AssertCalled(count = 2)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(4))
    }

    @Test
    fun forCallbacksDuringWait_specificCount() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.reload()
        sessionRule.waitForPageStops(2)

        var counter = 0

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(count = 2)
                override fun onPageStart(session: GeckoSession, url: String) {
                    counter++
                }

                @AssertCalled(count = 2)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(4))
    }

    @Test(expected = AssertionError::class)
    fun waitUntilCalled_throwOnWrongCount() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.reload()
        sessionRule.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled(count = 1) override fun onPageStart(session: GeckoSession, url: String) {}

                @AssertCalled(count = 2) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test(expected = AssertionError::class)
    fun forCallbacksDuringWait_throwOnWrongCount() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.reload()
        sessionRule.waitForPageStops(2)

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(count = 1) override fun onPageStart(session: GeckoSession, url: String) {}

                @AssertCalled(count = 1) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test
    fun waitUntilCalled_specificOrder() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled(order = [1]) override fun onPageStart(session: GeckoSession, url: String) {}

                @AssertCalled(order = [2]) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test
    fun forCallbacksDuringWait_specificOrder() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(order = [1]) override fun onPageStart(session: GeckoSession, url: String) {}

                @AssertCalled(order = [2]) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test(expected = AssertionError::class)
    fun waitUntilCalled_throwOnWrongOrder() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled(order = [2]) override fun onPageStart(session: GeckoSession, url: String) {}

                @AssertCalled(order = [1]) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test(expected = AssertionError::class)
    fun forCallbacksDuringWait_throwOnWrongOrder() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(order = [2]) override fun onPageStart(session: GeckoSession, url: String) {}

                @AssertCalled(order = [1]) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test
    fun forCallbacksDuringWait_multipleOrder() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.reload()
        sessionRule.waitForPageStops(2)

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(order = [1, 3, 1]) override fun onPageStart(session: GeckoSession, url: String) {}

                @AssertCalled(order = [2, 4, 1]) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test(expected = AssertionError::class)
    fun forCallbacksDuringWait_throwOnWrongMultipleOrder() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.reload()
        sessionRule.waitForPageStops(2)

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(order = [1, 2, 1]) override fun onPageStart(session: GeckoSession, url: String) {}

                @AssertCalled(order = [3, 4, 1]) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test
    fun forCallbacksDuringWait_notCalled() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        sessionRule.forCallbacksDuringWait(
            object : ScrollDelegate {
                @AssertCalled(false) override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {}
            }
        )
    }

    @Test(expected = AssertionError::class)
    fun waitUntilCalled_throwOnCallingZeroCall() {
        mainSession.loadTestPath(HELLO_HTML_PATH)

        sessionRule.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled(count = 0) override fun onPageStart(session: GeckoSession, url: String) {}

                @AssertCalled(count = 1) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    fun waitUntilCalled_assertCalledFalseNoTimeout() {
        mainSession.loadTestPath(HELLO_HTML_PATH)

        sessionRule.waitUntilCalled(
            object : ProgressDelegate, NavigationDelegate {
                @AssertCalled(count = 1) override fun onPageStop(session: GeckoSession, success: Boolean) {}

                @AssertCalled(false)
                override fun onLoadError(
                    session: GeckoSession,
                    uri: String?,
                    error: WebRequestError,
                ): GeckoResult<String>? {
                    return null
                }
            }
        )
    }

    @Test(expected = AssertionError::class)
    fun waitUntilCalled_throwOnCallingNoCall() {
        mainSession.loadTestPath(HELLO_HTML_PATH)

        sessionRule.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled(count = 1) override fun onPageStop(session: GeckoSession, success: Boolean) {}

                @AssertCalled(false) override fun onPageStart(session: GeckoSession, url: String) {}
            }
        )
    }

    @Test(expected = AssertionError::class)
    fun forCallbacksDuringWait_throwOnCallingNoCall() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(false) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test
    fun forCallbacksDuringWait_zeroCountEqualsNotCalled() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        sessionRule.forCallbacksDuringWait(
            object : ScrollDelegate {
                @AssertCalled(count = 0)
                override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {}
            }
        )
    }

    @Test(expected = AssertionError::class)
    fun forCallbacksDuringWait_throwOnCallingZeroCount() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(count = 0) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test
    fun forCallbacksDuringWait_limitedToLastWait() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.reload()
        mainSession.reload()
        mainSession.reload()

        // Wait for Gecko to finish all loads.
        Thread.sleep(100)

        sessionRule.waitForPageStop() // Wait for loadUri.
        sessionRule.waitForPageStop() // Wait for first reload.

        var counter = 0

        // assert should only apply to callbacks within range (loadUri, first reload].
        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStart(session: GeckoSession, url: String) {
                    counter++
                }

                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(2))
    }

    @Test
    fun forCallbacksDuringWait_currentCall() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    val info = sessionRule.currentCall
                    assertThat("Method info should be valid", info, notNullValue())
                    assertThat(
                        "Counter should be correct",
                        info.counter,
                        equalTo(1),
                    )
                    assertThat(
                        "Order should equal counter",
                        info.order,
                        equalTo(0),
                    )
                }
            }
        )
    }

    @Test(expected = IllegalStateException::class)
    fun forCallbacksDuringWait_passThroughExceptions() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    throw IllegalStateException()
                }
            }
        )
    }

    @Test(expected = AssertionError::class)
    @NullDelegate(ScrollDelegate::class)
    fun forCallbacksDuringWait_throwOnAnyNullDelegate() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()
        mainSession.reload()
        mainSession.waitForPageStop()

        mainSession.forCallbacksDuringWait(object : NavigationDelegate, ScrollDelegate {})
    }

    @Test(expected = AssertionError::class)
    @NullDelegate(ScrollDelegate::class)
    fun forCallbacksDuringWait_throwOnSpecificNullDelegate() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()
        mainSession.reload()
        mainSession.waitForPageStop()

        mainSession.forCallbacksDuringWait(
            object : ScrollDelegate {
                @AssertCalled override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {}
            }
        )
    }

    @NullDelegate(ScrollDelegate::class)
    @Test
    fun forCallbacksDuringWait_notThrowOnNonNullDelegate() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()
        mainSession.reload()
        mainSession.waitForPageStop()

        mainSession.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test(expected = AssertionError::class)
    fun getCurrentCall_throwOnNoCurrentCall() {
        sessionRule.currentCall
    }

    @Test
    fun delegateUntilTestEnd() {
        var counter = 0

        sessionRule.delegateUntilTestEnd(
            object : ProgressDelegate {
                @AssertCalled(count = 1, order = [1])
                override fun onPageStart(session: GeckoSession, url: String) {
                    counter++
                }

                @AssertCalled(count = 1, order = [2])
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        assertThat("Callback count should be correct", counter, equalTo(2))
    }

    @Test
    fun delegateUntilTestEnd_notCalled() {
        sessionRule.delegateUntilTestEnd(
            object : ScrollDelegate {
                @AssertCalled(false) override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {}
            }
        )
    }

    @Test(expected = AssertionError::class)
    fun delegateUntilTestEnd_throwOnNotCalled() {
        sessionRule.delegateUntilTestEnd(
            object : ScrollDelegate {
                @AssertCalled(count = 1)
                override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {}
            }
        )
        sessionRule.performTestEndCheck()
    }

    @Test(expected = AssertionError::class)
    fun delegateUntilTestEnd_throwOnCallingNoCall() {
        sessionRule.delegateUntilTestEnd(
            object : ProgressDelegate {
                @AssertCalled(false) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )

        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()
    }

    @Test(expected = AssertionError::class)
    fun delegateUntilTestEnd_throwOnWrongOrder() {
        sessionRule.delegateUntilTestEnd(
            object : ProgressDelegate {
                @AssertCalled(count = 1, order = [2]) override fun onPageStart(session: GeckoSession, url: String) {}

                @AssertCalled(count = 1, order = [1])
                override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )

        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()
    }

    @Test
    fun delegateUntilTestEnd_currentCall() {
        sessionRule.delegateUntilTestEnd(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    val info = sessionRule.currentCall
                    assertThat("Method info should be valid", info, notNullValue())
                    assertThat(
                        "Counter should be correct",
                        info.counter,
                        equalTo(1),
                    )
                    assertThat(
                        "Order should equal counter",
                        info.order,
                        equalTo(0),
                    )
                }
            }
        )

        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()
    }

    @Test
    fun delegateDuringNextWait() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()
        var counter = 0

        sessionRule.delegateDuringNextWait(
            object : ProgressDelegate {
                @AssertCalled(count = 1, order = [1])
                override fun onPageStart(session: GeckoSession, url: String) {
                    counter++
                }

                @AssertCalled(count = 1, order = [2])
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        assertThat("Should have delegated", counter, equalTo(2))

        mainSession.reload()
        sessionRule.waitForPageStop()

        assertThat("Delegate should be cleared", counter, equalTo(2))
    }

    @Test(expected = AssertionError::class)
    fun delegateDuringNextWait_throwOnNotCalled() {
        sessionRule.delegateDuringNextWait(
            object : ScrollDelegate {
                @AssertCalled(count = 1)
                override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {}
            }
        )
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()
    }

    @Test(expected = AssertionError::class)
    fun delegateDuringNextWait_throwOnNotCalledAtTestEnd() {
        sessionRule.delegateDuringNextWait(
            object : ScrollDelegate {
                @AssertCalled(count = 1)
                override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {}
            }
        )
        sessionRule.performTestEndCheck()
    }

    @Test
    fun delegateDuringNextWait_hasPrecedence() {
        var testCounter = 0
        var waitCounter = 0

        sessionRule.delegateUntilTestEnd(
            object : ProgressDelegate, NavigationDelegate {
                @AssertCalled(count = 1, order = [2])
                override fun onPageStart(session: GeckoSession, url: String) {
                    testCounter++
                }

                @AssertCalled(count = 1, order = [4])
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    testCounter++
                }

                @AssertCalled(count = 2, order = [1, 3])
                override fun onCanGoBack(session: GeckoSession, canGoBack: Boolean) {
                    testCounter++
                }

                @AssertCalled(count = 2, order = [1, 3])
                override fun onCanGoForward(session: GeckoSession, canGoForward: Boolean) {
                    testCounter++
                }
            }
        )

        sessionRule.delegateDuringNextWait(
            object : ProgressDelegate {
                @AssertCalled(count = 1, order = [1])
                override fun onPageStart(session: GeckoSession, url: String) {
                    waitCounter++
                }

                @AssertCalled(count = 1, order = [2])
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    waitCounter++
                }
            }
        )

        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        assertThat(
            "Text delegate should be overridden",
            testCounter,
            equalTo(2),
        )
        assertThat("Wait delegate should be used", waitCounter, equalTo(2))

        mainSession.reload()
        sessionRule.waitForPageStop()

        assertThat("Test delegate should be used", testCounter, equalTo(6))
        assertThat("Wait delegate should be cleared", waitCounter, equalTo(2))
    }

    @Test(expected = IllegalStateException::class)
    fun delegateDuringNextWait_passThroughExceptions() {
        sessionRule.delegateDuringNextWait(
            object : ProgressDelegate {
                @AssertCalled
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    throw IllegalStateException()
                }
            }
        )

        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()
    }

    @Test(expected = AssertionError::class)
    @NullDelegate(NavigationDelegate::class)
    fun delegateDuringNextWait_throwOnNullDelegate() {
        mainSession.delegateDuringNextWait(object : NavigationDelegate {})
    }

    @Test
    fun wrapSession() {
        val session = sessionRule.wrapSession(GeckoSession(mainSession.settings))
        sessionRule.openSession(session)
        session.reload()
        session.waitForPageStop()
    }

    @Test
    fun createOpenSession() {
        val newSession = sessionRule.createOpenSession()
        assertThat("Can create session", newSession, notNullValue())
        assertThat("New session is open", newSession.isOpen, equalTo(true))
        assertThat(
            "New session has same settings",
            newSession.settings,
            equalTo(mainSession.settings),
        )
    }

    @Test
    fun createOpenSession_withSettings() {
        val settings = GeckoSessionSettings.Builder(mainSession.settings).usePrivateMode(true).build()

        val newSession = sessionRule.createOpenSession(settings)
        assertThat("New session has same settings", newSession.settings, equalTo(settings))
    }

    @Ignore("Bug 1673953")
    @Test
    fun createOpenSession_canInterleaveOtherCalls() {
        mainSession.loadTestPath(HELLO_HTML_PATH)

        val newSession = sessionRule.createOpenSession()
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStops(2)

        newSession.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(false) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )

        mainSession.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(count = 2) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test
    fun createClosedSession() {
        val newSession = sessionRule.createClosedSession()
        assertThat("Can create session", newSession, notNullValue())
        assertThat("New session is open", newSession.isOpen, equalTo(false))
        assertThat(
            "New session has same settings",
            newSession.settings,
            equalTo(mainSession.settings),
        )
    }

    @Test
    fun createClosedSession_withSettings() {
        val settings = GeckoSessionSettings.Builder(mainSession.settings).usePrivateMode(true).build()

        val newSession = sessionRule.createClosedSession(settings)
        assertThat("New session has same settings", newSession.settings, equalTo(settings))
    }

    @Test(expected = UiThreadUtils.TimeoutException::class)
    @TimeoutMillis(2000)
    @ClosedSessionAtStart
    fun noPendingCallbacks_withSpecificSession() {
        sessionRule.createOpenSession()
        // Make sure we don't have unexpected pending callbacks after opening a session.
        sessionRule.waitUntilCalled(
            object : HistoryDelegate, ProgressDelegate {
                // There may be extraneous onSessionStateChange and onHistoryStateChange calls
                // after a test, so ignore the first received.
                @AssertCalled(count = 2)
                override fun onSessionStateChange(session: GeckoSession, state: SessionState) {}

                @AssertCalled(count = 2)
                override fun onHistoryStateChange(session: GeckoSession, historyList: HistoryDelegate.HistoryList) {}
            }
        )
    }

    @Test
    fun waitForPageStop_withSpecificSession() {
        val newSession = sessionRule.createOpenSession()
        newSession.loadTestPath(HELLO_HTML_PATH)
        newSession.waitForPageStop()
    }

    @Test
    fun waitForPageStop_withAllSessions() {
        val newSession = sessionRule.createOpenSession()
        newSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()
    }

    @Test(expected = AssertionError::class)
    fun waitForPageStop_throwOnNotWrapped() {
        GeckoSession(mainSession.settings).waitForPageStop()
    }

    @Test
    fun waitForPageStops_withSpecificSessions() {
        val newSession = sessionRule.createOpenSession()
        newSession.loadTestPath(HELLO_HTML_PATH)
        newSession.reload()
        newSession.waitForPageStops(2)
    }

    @Test
    fun waitForPageStops_withAllSessions() {
        val newSession = sessionRule.createOpenSession()
        newSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStops(2)
    }

    @Ignore("Bug 1673953")
    @Test
    fun waitForPageStops_acrossSessionCreation() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        val session = sessionRule.createOpenSession()
        mainSession.reload()
        session.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStops(3)
    }

    @Test
    fun waitUntilCalled_interfaceWithSpecificSession() {
        val newSession = sessionRule.createOpenSession()
        newSession.loadTestPath(HELLO_HTML_PATH)
        newSession.waitUntilCalled(ProgressDelegate::class, "onPageStop")
    }

    @Test
    fun waitUntilCalled_interfaceWithAllSessions() {
        val newSession = sessionRule.createOpenSession()
        newSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitUntilCalled(ProgressDelegate::class, "onPageStop")
    }

    @Test
    fun waitUntilCalled_callbackWithSpecificSession() {
        val newSession = sessionRule.createOpenSession()
        newSession.loadTestPath(HELLO_HTML_PATH)
        newSession.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled(count = 1) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test
    fun waitUntilCalled_callbackWithAllSessions() {
        val newSession = sessionRule.createOpenSession()
        newSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitUntilCalled(
            object : ProgressDelegate {
                @AssertCalled(count = 2) override fun onPageStop(session: GeckoSession, success: Boolean) {}
            }
        )
    }

    @Test
    fun forCallbacksDuringWait_withSpecificSession() {
        val newSession = sessionRule.createOpenSession()
        newSession.loadTestPath(HELLO_HTML_PATH)
        newSession.waitForPageStop()

        var counter = 0

        newSession.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        mainSession.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(false)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(1))
    }

    @Test
    fun forCallbacksDuringWait_withAllSessions() {
        val newSession = sessionRule.createOpenSession()
        newSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStops(2)

        var counter = 0

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(count = 2)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(2))
    }

    @Test
    fun forCallbacksDuringWait_limitedToLastSessionWait() {
        val newSession = sessionRule.createOpenSession()

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        newSession.loadTestPath(HELLO_HTML_PATH)
        newSession.waitForPageStop()

        // forCallbacksDuringWait calls strictly apply to the last wait, session-specific or not.
        var counter = 0

        mainSession.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(false)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        newSession.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        sessionRule.forCallbacksDuringWait(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        assertThat("Callback count should be correct", counter, equalTo(2))
    }

    @Test
    fun delegateUntilTestEnd_withSpecificSession() {
        val newSession = sessionRule.createOpenSession()

        var counter = 0

        newSession.delegateUntilTestEnd(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        mainSession.delegateUntilTestEnd(
            object : ProgressDelegate {
                @AssertCalled(false)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        newSession.loadTestPath(HELLO_HTML_PATH)
        newSession.waitForPageStop()

        assertThat("Callback count should be correct", counter, equalTo(1))
    }

    @Test
    fun delegateUntilTestEnd_withAllSessions() {
        var counter = 0

        sessionRule.delegateUntilTestEnd(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        val newSession = sessionRule.createOpenSession()
        newSession.loadTestPath(HELLO_HTML_PATH)
        newSession.waitForPageStop()

        assertThat("Callback count should be correct", counter, equalTo(1))
    }

    @Test
    fun delegateDuringNextWait_hasPrecedenceWithSpecificSession() {
        val newSession = sessionRule.createOpenSession()
        var counter = 0

        newSession.delegateDuringNextWait(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        newSession.delegateUntilTestEnd(
            object : ProgressDelegate {
                @AssertCalled(false)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        newSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStops(2)

        assertThat("Callback count should be correct", counter, equalTo(1))
    }

    @Test
    fun delegateDuringNextWait_specificSessionOverridesAll() {
        val newSession = sessionRule.createOpenSession()
        var counter = 0

        newSession.delegateDuringNextWait(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        sessionRule.delegateDuringNextWait(
            object : ProgressDelegate {
                @AssertCalled(count = 1)
                override fun onPageStop(session: GeckoSession, success: Boolean) {
                    counter++
                }
            }
        )

        newSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStops(2)

        assertThat("Callback count should be correct", counter, equalTo(2))
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun synthesizeTap() {
        mainSession.loadTestPath(CLICK_TO_RELOAD_HTML_PATH)
        mainSession.waitForPageStop()

        mainSession.synthesizeTap(50, 50)
        mainSession.waitForPageStop()
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun synthesizeMouse() {
        mainSession.loadTestPath(MOUSE_TO_RELOAD_HTML_PATH)
        mainSession.waitForPageStop()

        val time = SystemClock.uptimeMillis()
        mainSession.evaluateJS("document.body.addEventListener('mousedown', () => { window.location.reload() })")
        mainSession.synthesizeMouse(time, MotionEvent.ACTION_DOWN, 50, 50, MotionEvent.BUTTON_PRIMARY)
        mainSession.waitForPageStop()

        mainSession.evaluateJS("document.body.addEventListener('mouseup', () => { window.location.reload() })")
        mainSession.synthesizeMouse(time, MotionEvent.ACTION_UP, 50, 50, 0)
        mainSession.waitForPageStop()
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun synthesizeMouseMove() {
        mainSession.loadTestPath(MOUSE_TO_RELOAD_HTML_PATH)
        mainSession.waitForPageStop()

        mainSession.evaluateJS("document.body.addEventListener('mousemove', () => { window.location.reload() })")
        mainSession.synthesizeMouseMove(50, 50)
        mainSession.waitForPageStop()
    }

    @Test
    fun evaluateExtensionJS() {
        assertThat(
            "JS string result should be correct",
            sessionRule.evaluateExtensionJS("return 'foo';") as String,
            equalTo("foo"),
        )

        assertThat(
            "JS number result should be correct",
            sessionRule.evaluateExtensionJS("return 1+1;") as Double,
            equalTo(2.0),
        )

        assertThat(
            "JS boolean result should be correct",
            sessionRule.evaluateExtensionJS("return !0;") as Boolean,
            equalTo(true),
        )

        val expected = JSONObject("{bar:42,baz:true,foo:'bar'}")
        val actual = sessionRule.evaluateExtensionJS("return {foo:'bar',bar:42,baz:true};") as JSONObject
        for (key in expected.keys()) {
            assertThat(
                "JS object result should be correct",
                actual.get(key),
                equalTo(expected.get(key)),
            )
        }

        assertThat(
            "JS array result should be correct",
            sessionRule.evaluateExtensionJS("return [1,2,3];") as JSONArray,
            equalTo(JSONArray("[1,2,3]")),
        )

        assertThat(
            "Can access extension APIS",
            sessionRule.evaluateExtensionJS("return !!browser.runtime;") as Boolean,
            equalTo(true),
        )

        assertThat(
            "Can access extension APIS",
            sessionRule.evaluateExtensionJS(
                """
                return true;
                // Comments at the end are allowed
                """
                    .trimIndent()
            ) as Boolean,
            equalTo(true),
        )

        try {
            sessionRule.evaluateExtensionJS("test({ what")
            assertThat("Should fail", true, equalTo(false))
        } catch (e: RejectedPromiseException) {
            assertThat(
                "Syntax errors are reported",
                e.message,
                containsString("SyntaxError"),
            )
        }
    }

    @Test
    fun evaluateJS() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        assertThat(
            "JS string result should be correct",
            mainSession.evaluateJS("'foo'") as String,
            equalTo("foo"),
        )

        assertThat(
            "JS number result should be correct",
            mainSession.evaluateJS("1+1") as Double,
            equalTo(2.0),
        )

        assertThat(
            "JS boolean result should be correct",
            mainSession.evaluateJS("!0") as Boolean,
            equalTo(true),
        )

        val expected = JSONObject("{bar:42,baz:true,foo:'bar'}")
        val actual = mainSession.evaluateJS("({foo:'bar',bar:42,baz:true})") as JSONObject
        for (key in expected.keys()) {
            assertThat(
                "JS object result should be correct",
                actual.get(key),
                equalTo(expected.get(key)),
            )
        }

        assertThat(
            "JS array result should be correct",
            mainSession.evaluateJS("[1,2,3]") as JSONArray,
            equalTo(JSONArray("[1,2,3]")),
        )

        assertThat(
            "JS DOM object result should be correct",
            mainSession.evaluateJS("document.body.tagName") as String,
            equalTo("BODY"),
        )
    }

    @Test
    fun evaluateJS_windowObject() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        assertThat(
            "JS DOM window result should be correct",
            (mainSession.evaluateJS("window.location.pathname")) as String,
            equalTo(HELLO_HTML_PATH),
        )
    }

    @Test
    fun evaluateJS_multipleSessions() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        mainSession.evaluateJS("this.foo = 42")
        assertThat(
            "Variable should be set",
            mainSession.evaluateJS("this.foo") as Double,
            equalTo(42.0),
        )

        val newSession = sessionRule.createOpenSession()
        newSession.loadTestPath(HELLO_HTML_PATH)
        newSession.waitForPageStop()

        val result = newSession.evaluateJS("this.foo")
        assertThat(
            "New session should have separate JS context",
            result,
            nullValue(),
        )
    }

    @Test
    fun evaluateJS_supportPromises() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        assertThat(
            "Can get resolved promise",
            mainSession.evaluatePromiseJS("new Promise(resolve => resolve('foo'))").value as String,
            equalTo("foo"),
        )

        val promise = mainSession.evaluatePromiseJS("new Promise(r => window.resolve = r)")

        mainSession.evaluateJS("window.resolve('bar')")

        assertThat(
            "Can wait for promise to resolve",
            promise.value as String,
            equalTo("bar"),
        )
    }

    @Test(expected = RejectedPromiseException::class)
    fun evaluateJS_throwOnRejectedPromise() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.evaluatePromiseJS("Promise.reject('foo')").value
    }

    @Test
    fun evaluateJS_notBlockMainThread() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()
        // Test that we can still receive delegate callbacks during evaluateJS,
        // by calling alert(), which blocks until prompt delegate is called.
        assertThat(
            "JS blocking result should be correct",
            mainSession.evaluateJS("alert(); 'foo'") as String,
            equalTo("foo"),
        )
    }

    @TimeoutMillis(1000)
    @Test(expected = UiThreadUtils.TimeoutException::class)
    fun evaluateJS_canTimeout() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.delegateUntilTestEnd(
            object : PromptDelegate {
                override fun onAlertPrompt(
                    session: GeckoSession,
                    prompt: PromptDelegate.AlertPrompt,
                ): GeckoResult<PromptDelegate.PromptResponse> {
                    // Return a GeckoResult that we will never complete, so it hangs.
                    val res = GeckoResult<PromptDelegate.PromptResponse>()
                    return res
                }
            }
        )
        mainSession.evaluateJS("new Promise(resolve => window.setTimeout(resolve, 2000))")
    }

    @Test(expected = RuntimeException::class)
    fun evaluateJS_throwOnJSException() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.evaluateJS("throw Error()")
    }

    @Test(expected = RuntimeException::class)
    fun evaluateJS_throwOnSyntaxError() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.evaluateJS("<{[")
    }

    @Test(expected = RuntimeException::class)
    fun evaluateJS_throwOnChromeAccess() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.evaluateJS("ChromeUtils")
    }

    @Test
    fun getPrefs_undefinedPrefReturnsNull() {
        assertThat(
            "Undefined pref should have null value",
            sessionRule.getPrefs("invalid.pref")[0],
            equalTo(JSONObject.NULL),
        )
    }

    @Test
    fun setPrefsUntilTestEnd() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "test.pref.bool" to true,
                "test.pref.int" to 1,
                "test.pref.foo" to "foo",
            )
        )

        var prefs =
            sessionRule.getPrefs(
                "test.pref.bool",
                "test.pref.int",
                "test.pref.foo",
                "test.pref.bar",
            )

        assertThat("Prefs should be set", prefs[0] as Boolean, equalTo(true))
        assertThat("Prefs should be set", prefs[1] as Int, equalTo(1))
        assertThat("Prefs should be set", prefs[2] as String, equalTo("foo"))
        assertThat("Prefs should be set", prefs[3], equalTo(JSONObject.NULL))

        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "test.pref.foo" to "bar",
                "test.pref.bar" to "baz",
            )
        )

        prefs =
            sessionRule.getPrefs(
                "test.pref.bool",
                "test.pref.int",
                "test.pref.foo",
                "test.pref.bar",
            )

        assertThat("New prefs should be set", prefs[0] as Boolean, equalTo(true))
        assertThat("New prefs should be set", prefs[1] as Int, equalTo(1))
        assertThat("New prefs should be set", prefs[2] as String, equalTo("bar"))
        assertThat("New prefs should be set", prefs[3] as String, equalTo("baz"))
    }

    @Test
    fun setPrefsDuringNextWait() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        sessionRule.setPrefsDuringNextWait(
            mapOf(
                "test.pref.bool" to true,
                "test.pref.int" to 1,
                "test.pref.foo" to "foo",
            )
        )

        var prefs =
            sessionRule.getPrefs(
                "test.pref.bool",
                "test.pref.int",
                "test.pref.foo",
            )

        assertThat("Prefs should be set before wait", prefs[0] as Boolean, equalTo(true))
        assertThat("Prefs should be set before wait", prefs[1] as Int, equalTo(1))
        assertThat("Prefs should be set before wait", prefs[2] as String, equalTo("foo"))

        mainSession.reload()
        mainSession.waitForPageStop()

        prefs =
            sessionRule.getPrefs(
                "test.pref.bool",
                "test.pref.int",
                "test.pref.foo",
            )

        assertThat("Prefs should be cleared after wait", prefs[0], equalTo(JSONObject.NULL))
        assertThat("Prefs should be cleared after wait", prefs[1], equalTo(JSONObject.NULL))
        assertThat("Prefs should be cleared after wait", prefs[2], equalTo(JSONObject.NULL))
    }

    @Test
    fun setPrefsDuringNextWait_hasPrecedence() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "test.pref.int" to 1,
                "test.pref.foo" to "foo",
            )
        )

        sessionRule.setPrefsDuringNextWait(
            mapOf(
                "test.pref.foo" to "bar",
                "test.pref.bar" to "baz",
            )
        )

        var prefs =
            sessionRule.getPrefs(
                "test.pref.int",
                "test.pref.foo",
                "test.pref.bar",
            )

        assertThat("Prefs should be overridden", prefs[0] as Int, equalTo(1))
        assertThat("Prefs should be overridden", prefs[1] as String, equalTo("bar"))
        assertThat("Prefs should be overridden", prefs[2] as String, equalTo("baz"))

        mainSession.reload()
        mainSession.waitForPageStop()

        prefs =
            sessionRule.getPrefs(
                "test.pref.int",
                "test.pref.foo",
                "test.pref.bar",
            )

        assertThat("Overriden prefs should be restored", prefs[0] as Int, equalTo(1))
        assertThat("Overriden prefs should be restored", prefs[1] as String, equalTo("foo"))
        assertThat("Overriden prefs should be restored", prefs[2], equalTo(JSONObject.NULL))
    }

    @Test
    fun waitForJS() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        assertThat(
            "waitForJS should return correct result",
            mainSession.waitForJS("alert(), 'foo'") as String,
            equalTo("foo"),
        )

        mainSession.forCallbacksDuringWait(
            object : PromptDelegate {
                @AssertCalled(count = 1)
                override fun onAlertPrompt(
                    session: GeckoSession,
                    prompt: PromptDelegate.AlertPrompt,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    return null
                }
            }
        )
    }

    @Test
    fun waitForJS_resolvePromise() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()
        assertThat(
            "waitForJS should wait for promises",
            mainSession.waitForJS("Promise.resolve('foo')") as String,
            equalTo("foo"),
        )
    }

    @Test
    fun waitForJS_delegateDuringWait() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        var count = 0
        mainSession.delegateDuringNextWait(
            object : PromptDelegate {
                override fun onAlertPrompt(
                    session: GeckoSession,
                    prompt: PromptDelegate.AlertPrompt,
                ): GeckoResult<PromptDelegate.PromptResponse> {
                    count++
                    return GeckoResult.fromValue(prompt.dismiss())
                }
            }
        )

        mainSession.waitForJS("alert()")
        mainSession.waitForJS("alert()")

        // The delegate set through delegateDuringNextWait
        // should have been cleared after the first wait.
        assertThat("Delegate should only run once", count, equalTo(1))
    }

    @Test(expected = RejectedPromiseException::class)
    fun waitForJS_whileNavigating() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        // disabled for frequent failures - on Bug 1933390
        assumeThat(sessionRule.env.isX86, equalTo(false))

        // Trigger navigation and try again
        mainSession.loadTestPath(HELLO2_HTML_PATH)
        mainSession.waitForPageStop()

        // Navigate away and trigger a waitForJS that never completes, this will
        // fail because the page navigates away (disconnecting the port) before
        // the page can respond.
        mainSession.goBack()
        mainSession.waitForJS("new Promise(resolve => {})")
    }

    private interface TestDelegate {
        fun onDelegate(foo: String, bar: String): Int
    }

    @Test
    fun addExternalDelegateUntilTestEnd() {
        lateinit var delegate: TestDelegate

        sessionRule.addExternalDelegateUntilTestEnd(
            TestDelegate::class,
            { newDelegate -> delegate = newDelegate },
            {},
            object : TestDelegate {
                @AssertCalled(count = 1)
                override fun onDelegate(foo: String, bar: String): Int {
                    assertThat("First argument should be correct", foo, equalTo("foo"))
                    assertThat("Second argument should be correct", bar, equalTo("bar"))
                    return 42
                }
            },
        )

        assertThat("Delegate should be registered", delegate, notNullValue())
        assertThat(
            "Delegate return value should be correct",
            delegate.onDelegate("foo", "bar"),
            equalTo(42),
        )
        sessionRule.performTestEndCheck()
    }

    @Test(expected = AssertionError::class)
    fun addExternalDelegateUntilTestEnd_throwOnNotCalled() {
        sessionRule.addExternalDelegateUntilTestEnd(
            TestDelegate::class,
            {},
            {},
            object : TestDelegate {
                @AssertCalled(count = 1)
                override fun onDelegate(foo: String, bar: String): Int {
                    return 42
                }
            },
        )
        sessionRule.performTestEndCheck()
    }

    @Test
    fun addExternalDelegateDuringNextWait() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        var delegate: Runnable? = null

        sessionRule.addExternalDelegateDuringNextWait(
            Runnable::class,
            { newDelegate -> delegate = newDelegate },
            { delegate = null },
            Runnable {},
        )

        assertThat("Delegate should be registered", delegate, notNullValue())
        delegate?.run()

        mainSession.reload()
        mainSession.waitForPageStop()
        mainSession.forCallbacksDuringWait(
            object : Runnable {
                @AssertCalled(count = 1) override fun run() {}
            }
        )

        assertThat("Delegate should be unregistered after wait", delegate, nullValue())
    }

    @Test
    fun addExternalDelegateDuringNextWait_hasPrecedence() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        sessionRule.waitForPageStop()

        var delegate: TestDelegate? = null
        val register = { newDelegate: TestDelegate -> delegate = newDelegate }
        val unregister = { _: TestDelegate -> delegate = null }

        sessionRule.addExternalDelegateDuringNextWait(
            TestDelegate::class,
            register,
            unregister,
            object : TestDelegate {
                @AssertCalled(count = 1)
                override fun onDelegate(foo: String, bar: String): Int {
                    return 24
                }
            },
        )

        sessionRule.addExternalDelegateUntilTestEnd(
            TestDelegate::class,
            register,
            unregister,
            object : TestDelegate {
                @AssertCalled(count = 1)
                override fun onDelegate(foo: String, bar: String): Int {
                    return 42
                }
            },
        )

        assertThat("Wait delegate should be registered", delegate, notNullValue())
        assertThat(
            "Wait delegate return value should be correct",
            delegate?.onDelegate("", ""),
            equalTo(24),
        )

        mainSession.reload()
        mainSession.waitForPageStop()

        assertThat("Test delegate should still be registered", delegate, notNullValue())
        assertThat(
            "Test delegate return value should be correct",
            delegate?.onDelegate("", ""),
            equalTo(42),
        )
        sessionRule.performTestEndCheck()
    }

    @IgnoreCrash
    @Test
    fun contentCrashIgnored() {
        mainSession.loadUri(CONTENT_CRASH_URL)
        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1) override fun onCrash(session: GeckoSession) = Unit
            }
        )
    }

    @Test(expected = ChildCrashedException::class)
    fun contentCrashFails() {
        assumeThat(sessionRule.env.shouldShutdownOnCrash(), equalTo(false))

        mainSession.loadUri(CONTENT_CRASH_URL)
        sessionRule.waitForPageStop()
    }

    @Test
    fun waitForResult() {
        val handler = Handler(Looper.getMainLooper())
        val result =
            object : GeckoResult<Int>() {
                init {
                    handler.postDelayed(
                        {
                            complete(42)
                        },
                        100,
                    )
                }
            }

        val value = sessionRule.waitForResult(result)
        assertThat("Value should match", value, equalTo(42))
    }

    @Test(expected = IllegalStateException::class)
    fun waitForResultExceptionally() {
        val handler = Handler(Looper.getMainLooper())
        val result =
            object : GeckoResult<Int>() {
                init {
                    handler.postDelayed(
                        {
                            completeExceptionally(IllegalStateException("boom"))
                        },
                        100,
                    )
                }
            }

        sessionRule.waitForResult(result)
    }
}
