/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.BuildConfig
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.IgnoreCrash

@RunWith(AndroidJUnit4::class)
@MediumTest
class GmpCrashTest : BaseSessionTest() {
    val client = TestCrashHandler.Client(InstrumentationRegistry.getInstrumentation().targetContext)

    @Before
    fun setup() {
        assertTrue(client.connect(env.defaultTimeoutMillis))
        client.setEvalNextCrashDump(GeckoRuntime.CRASHED_PROCESS_VISIBILITY_BACKGROUND_CHILD, "gmplugin", null)
    }

    private fun crashGmp(isolated: Boolean) {
        // We need the crash reporter for this test
        assumeTrue(BuildConfig.MOZ_CRASHREPORTER)

        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "media.eme.require-app-approval" to false,
                "media.gmp.android.isolated-process" to isolated,
            )
        )

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        // Creating ClearKey media keys launches the GMP process.
        mainSession
            .evaluatePromiseJS(
                """
                navigator.requestMediaKeySystemAccess("org.w3.clearkey", [{
                  initDataTypes: ["keyids"],
                  audioCapabilities: [{ contentType: 'audio/webm; codecs="opus"' }],
                }]).then(access => access.createMediaKeys())
                """
            )
            .value

        sessionRule.setPrefsUntilTestEnd(mapOf("media.gmp.plugin.crash" to true))

        val evalResult = client.getEvalResult(env.defaultTimeoutMillis)
        assertTrue(evalResult.mMsg, evalResult.mResult)
    }

    @IgnoreCrash @Test fun crashGmp() = crashGmp(isolated = false)

    @IgnoreCrash @Test fun crashIsolatedGmp() = crashGmp(isolated = true)

    @After
    fun teardown() {
        client.disconnect()
    }
}
