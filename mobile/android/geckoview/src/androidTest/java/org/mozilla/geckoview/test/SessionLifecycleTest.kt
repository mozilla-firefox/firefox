/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test

import android.os.Bundle
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import java.lang.ref.ReferenceQueue
import java.lang.ref.WeakReference
import org.hamcrest.Matchers.equalTo
import org.hamcrest.Matchers.greaterThan
import org.hamcrest.Matchers.lessThan
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.GeckoRuntimeSettings
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.ClosedSessionAtStart
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.WithDisplay
import org.mozilla.geckoview.test.util.UiThreadUtils

@RunWith(AndroidJUnit4::class)
@MediumTest
class SessionLifecycleTest : BaseSessionTest() {
    companion object {
        val LOGTAG = "SessionLifecycleTest"
    }

    @Test
    fun open_interleaved() {
        val session1 = sessionRule.createOpenSession()
        val session2 = sessionRule.createOpenSession()
        session1.close()
        val session3 = sessionRule.createOpenSession()
        session2.close()
        session3.close()

        mainSession.reload()
        mainSession.waitForPageStop()
    }

    @Test
    fun open_repeated() {
        for (i in 1..5) {
            mainSession.close()
            mainSession.open()
        }
        mainSession.reload()
        mainSession.waitForPageStop()
    }

    @Test
    fun open_allowCallsWhileClosed() {
        mainSession.close()

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.reload()

        mainSession.open()
        mainSession.waitForPageStops(2)
    }

    @Test(expected = IllegalStateException::class)
    fun open_throwOnAlreadyOpen() {
        // Throw exception if retrying to open again; otherwise we would leak the old open window.
        mainSession.open()
    }

    @ClosedSessionAtStart
    @Test
    fun restoreRuntimeSettings_noSession() {
        val extrasSetting = Bundle(2)
        extrasSetting.putInt("test1", 10)
        extrasSetting.putBoolean("test2", true)

        val settings = GeckoRuntimeSettings.Builder().javaScriptEnabled(false).extras(extrasSetting).build()

        settings.toParcel { parcel ->
            val newSettings = GeckoRuntimeSettings.Builder().build()
            newSettings.readFromParcel(parcel)

            assertThat(
                "Parceled settings must match",
                newSettings.javaScriptEnabled,
                equalTo(settings.javaScriptEnabled),
            )
            assertThat(
                "Parceled settings must match",
                newSettings.extras.getInt("test1"),
                equalTo(settings.extras.getInt("test1")),
            )
            assertThat(
                "Parceled settings must match",
                newSettings.extras.getBoolean("test2"),
                equalTo(settings.extras.getBoolean("test2")),
            )
        }
    }

    @Test
    fun collectClosed() {
        // We can't use a normal scoped function like `run` because
        // those are inlined, which leaves a local reference.
        fun createSession(): QueuedWeakReference<GeckoSession> {
            return QueuedWeakReference<GeckoSession>(GeckoSession())
        }

        waitUntilCollected(createSession())
    }

    @Test
    fun collectAfterClose() {
        fun createSession(): QueuedWeakReference<GeckoSession> {
            val s = GeckoSession()
            s.open(sessionRule.runtime)
            s.close()
            return QueuedWeakReference<GeckoSession>(s)
        }

        waitUntilCollected(createSession())
    }

    @Test
    fun collectOpen() {
        fun createSession(): QueuedWeakReference<GeckoSession> {
            val s = GeckoSession()
            s.open(sessionRule.runtime)
            return QueuedWeakReference<GeckoSession>(s)
        }

        waitUntilCollected(createSession())
    }

    // Waits for 4 requestAnimationFrame calls and computes rate
    private fun computeRequestAnimationFrameRate(session: GeckoSession): Double {
        return session.evaluateJS(
            """
            new Promise(resolve => {
                let start = 0;
                let frames = 0;
                const ITERATIONS = 4;
                function raf() {
                    if (frames === 0) {
                        start = window.performance.now();
                    }
                    if (frames === ITERATIONS) {
                        resolve((window.performance.now() - start) / ITERATIONS);
                    }
                    frames++;
                    window.requestAnimationFrame(raf);
                }
                window.requestAnimationFrame(raf);
            });
        """
        ) as Double
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun asyncScriptsSuspendedWhileInactive() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "privacy.reduceTimerPrecision" to false,
                // This makes the throttled frame rate 4 times faster than normal,
                // so this test doesn't time out. Should still be significantly slower tha
                // the active frame rate so we can measure the effects
                "layout.throttled_frame_rate" to 4,
            )
        )

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        assertThat("docShell should start active", mainSession.active, equalTo(true))

        // Deactivate the GeckoSession and confirm that rAF/setTimeout/etc callbacks do not run
        mainSession.setActive(false)
        assertThat(
            "docShell shouldn't be active after calling setActive(false)",
            mainSession.active,
            equalTo(false),
        )

        mainSession.evaluateJS(
            """
            function fail() {
                document.documentElement.style.backgroundColor = 'green';
            }
            setTimeout(fail, 1);
            fetch("missing.html").catch(fail);
        """
        )

        var rafRate = computeRequestAnimationFrameRate(mainSession)
        assertThat(
            "requestAnimationFrame should be called about once a second",
            rafRate,
            greaterThan(450.0),
        )
        assertThat(
            "requestAnimationFrame should be called about once a second",
            rafRate,
            lessThan(10000.0),
        )

        val isNotGreen = mainSession.evaluateJS("document.documentElement.style.backgroundColor !== 'green'") as Boolean
        assertThat("timeouts have not run yet", isNotGreen, equalTo(true))

        // Reactivate the GeckoSession and confirm that rAF/setTimeout/etc callbacks now run
        mainSession.setActive(true)
        assertThat(
            "docShell should be active after calling setActive(true)",
            mainSession.active,
            equalTo(true),
        )

        // At 60fps, once a frame is about 16.6 ms
        rafRate = computeRequestAnimationFrameRate(mainSession)
        assertThat(
            "requestAnimationFrame should be called about once a frame",
            rafRate,
            lessThan(60.0),
        )
        assertThat(
            "requestAnimationFrame should be called about once a frame",
            rafRate,
            greaterThan(5.0),
        )
    }

    private fun waitUntilCollected(ref: QueuedWeakReference<*>) {
        UiThreadUtils.waitForCondition(
            {
                Runtime.getRuntime().gc()
                ref.queue.poll() != null
            },
            sessionRule.timeoutMillis,
        )
    }

    class QueuedWeakReference<T>
    @JvmOverloads
    constructor(
        obj: T,
        var queue: ReferenceQueue<T> = ReferenceQueue(),
    ) : WeakReference<T>(obj, queue)
}
