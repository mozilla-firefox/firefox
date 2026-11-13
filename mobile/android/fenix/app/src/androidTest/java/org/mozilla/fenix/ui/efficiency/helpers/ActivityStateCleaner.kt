/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.helpers

import android.app.Activity
import android.os.SystemClock
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.UiDevice
import org.mozilla.fenix.ui.efficiency.logging.TestLogging

object ActivityStateCleaner {
    private const val REMOVAL_TIMEOUT_MS = 5_000L
    private const val POLL_INTERVAL_MS = 50L

    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val device by lazy { UiDevice.getInstance(instrumentation) }

    fun restore(activity: Activity, testId: String) {
        val taskId = activity.taskId
        val startedAt = SystemClock.elapsedRealtime()
        instrumentation.runOnMainSync {
            activity.finishAndRemoveTask()
        }
        val verified = waitUntilRemoved(activity, taskId)
        val taskPresentAfter = taskPresent(taskId)

        val taskRemovalRequested = activity.isFinishing || activity.isDestroyed
        try {
            TestLogging.installed()
                .record(
                    "activityCleanup",
                    mapOf(
                        "phase" to "after",
                        "testId" to testId,
                        "taskId" to taskId,
                        "taskRemovalRequested" to taskRemovalRequested,
                        "activityDestroyed" to activity.isDestroyed,
                        "taskPresentAfter" to taskPresentAfter,
                        "verified" to verified,
                        "elapsedMs" to SystemClock.elapsedRealtime() - startedAt,
                    ),
                )
        } catch (_: Throwable) {}
        check(verified) {
            "HomeActivity cleanup did not complete after $testId: " +
                "taskId=$taskId, requested=$taskRemovalRequested, " +
                "destroyed=${activity.isDestroyed}, taskPresent=$taskPresentAfter"
        }
    }

    private fun waitUntilRemoved(activity: Activity, taskId: Int): Boolean {
        val deadline = SystemClock.elapsedRealtime() + REMOVAL_TIMEOUT_MS
        while (SystemClock.elapsedRealtime() < deadline) {
            device.waitForIdle()
            if (activity.isDestroyed && !taskPresent(taskId)) return true
            SystemClock.sleep(POLL_INTERVAL_MS)
        }
        return activity.isDestroyed && !taskPresent(taskId)
    }

    private fun taskPresent(taskId: Int): Boolean {
        val dump = device.executeShellCommand("dumpsys activity activities")
        return Regex("""Task\{[^\n]* #$taskId\b""").containsMatchIn(dump)
    }
}
