/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.helpers

import android.app.Activity
import android.os.SystemClock
import android.view.inputmethod.InputMethodManager
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.UiDevice
import org.mozilla.fenix.ui.efficiency.logging.TestLogging

object InputStateCleaner {
    private const val DISMISS_TIMEOUT_MS = 2_000L
    private const val POLL_INTERVAL_MS = 50L

    private val visibilityPattern = Regex("""\bmInputShown=(true|false)\b""")
    private val device by lazy { UiDevice.getInstance(InstrumentationRegistry.getInstrumentation()) }

    fun restore(phase: String, testId: String, activity: Activity? = null) {
        val visibleBefore = keyboardVisible()
        val dismissals = mutableListOf<String>()

        if (visibleBefore) {
            activity?.let {
                dismissals += if (hideFromActivity(it)) "appFocus" else "appFocusRejected"
                waitUntilHidden(500)
            }
            if (keyboardVisible()) {
                dismissals += if (device.pressBack()) "uiDevice" else "uiDeviceRejected"
                waitUntilHidden(500)
            }
            if (keyboardVisible()) {
                device.executeShellCommand("input keyevent KEYCODE_BACK")
                dismissals += "shell"
            }
            waitUntilHidden(DISMISS_TIMEOUT_MS)
        }

        val visibleAfter = keyboardVisible()
        try {
            TestLogging.installed()
                .record(
                    "inputCleanup",
                    mapOf(
                        "phase" to phase,
                        "testId" to testId,
                        "visibleBefore" to visibleBefore,
                        "dismissals" to dismissals.joinToString(",").ifEmpty { "none" },
                        "visibleAfter" to visibleAfter,
                    ),
                )
        } catch (_: Throwable) {}
        check(!visibleAfter) {
            "Soft keyboard remained visible after $phase cleanup for $testId"
        }
    }

    private fun hideFromActivity(activity: Activity): Boolean {
        var accepted = false
        InstrumentationRegistry.getInstrumentation().runOnMainSync {
            val token = activity.currentFocus?.windowToken ?: activity.window.decorView.windowToken
            activity.currentFocus?.clearFocus()
            accepted = activity.getSystemService(InputMethodManager::class.java).hideSoftInputFromWindow(token, 0)
        }
        return accepted
    }

    private fun waitUntilHidden(timeoutMs: Long): Boolean {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        while (keyboardVisible() && SystemClock.elapsedRealtime() < deadline) {
            SystemClock.sleep(POLL_INTERVAL_MS)
        }
        return !keyboardVisible()
    }

    private fun keyboardVisible(): Boolean {
        val output = device.executeShellCommand("dumpsys input_method | grep 'mInputShown='")
        val state = visibilityPattern.find(output)?.groupValues?.get(1)?.toBooleanStrictOrNull()
        checkNotNull(state) {
            "Unable to determine soft-keyboard visibility from dumpsys input_method"
        }
        return state
    }
}
