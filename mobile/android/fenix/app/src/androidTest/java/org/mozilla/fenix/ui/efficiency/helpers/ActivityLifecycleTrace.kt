/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.helpers

import android.app.Activity
import android.app.Application
import android.content.Intent
import android.os.Bundle
import android.os.Process
import android.os.SystemClock
import java.io.Closeable
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.ui.efficiency.logging.JsonSink

/** Records the Android lifecycle and process/task identity around one test. */
class ActivityLifecycleTrace private constructor(private val testId: String) :
    Application.ActivityLifecycleCallbacks, Closeable {

    private val application = appContext.applicationContext as Application
    // Activity callbacks run under Fenix's main-thread StrictMode policy. JsonSink's provider transport queues its
    // file writes off-thread; the app-scoped file sink stays disabled here.
    private val logcat = JsonSink(null)
    private var sequence = 0

    init {
        application.registerActivityLifecycleCallbacks(this)
        record("traceStarted")
    }

    override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) =
        record("created", activity, savedInstanceState)

    override fun onActivityStarted(activity: Activity) = record("started", activity)

    override fun onActivityResumed(activity: Activity) = record("resumed", activity)

    override fun onActivityPaused(activity: Activity) = record("paused", activity)

    override fun onActivityStopped(activity: Activity) = record("stopped", activity)

    override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) =
        record("saveInstanceState", activity, outState)

    override fun onActivityDestroyed(activity: Activity) = record("destroyed", activity)

    override fun close() {
        record("traceStopped")
        application.unregisterActivityLifecycleCallbacks(this)
    }

    private fun record(event: String, activity: Activity? = null, state: Bundle? = null) {
        try {
            val appState = appContext.components.appStore.state
            logcat.event(
                mapOf(
                    "type" to "activityLifecycle",
                    "testId" to testId,
                    "sequence" to sequence++,
                    "event" to event,
                    "elapsedRealtimeMs" to SystemClock.elapsedRealtime(),
                    "processId" to Process.myPid(),
                    "activity" to activity?.javaClass?.name,
                    "activityId" to activity?.let { System.identityHashCode(it).toString(16) },
                    "taskId" to activity?.taskId,
                    "isTaskRoot" to activity?.isTaskRoot,
                    "intentFlags" to activity?.intent?.flags,
                    "newTaskIntent" to activity?.intent?.hasFlag(Intent.FLAG_ACTIVITY_NEW_TASK),
                    "clearTaskIntent" to activity?.intent?.hasFlag(Intent.FLAG_ACTIVITY_CLEAR_TASK),
                    "launchedFromHistory" to activity?.intent?.hasFlag(Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY),
                    "hasSavedInstanceState" to if (event == "created") state != null else null,
                    "savedStateKeyCount" to state?.keySet()?.size,
                    "isFinishing" to activity?.isFinishing,
                    "isChangingConfigurations" to activity?.isChangingConfigurations,
                    "isDestroyed" to activity?.isDestroyed,
                    "searchActive" to appState.searchState.isSearchActive,
                    "voiceInputRequested" to appState.voiceSearchState.isRequestingVoiceInput,
                )
            )
        } catch (_: Throwable) {}
    }

    private fun Intent.hasFlag(flag: Int) = flags and flag != 0

    companion object {
        fun start(testId: String) = ActivityLifecycleTrace(testId)
    }
}
