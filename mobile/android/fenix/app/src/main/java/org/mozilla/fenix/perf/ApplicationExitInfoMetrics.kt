/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.perf

import android.app.ActivityManager
import android.app.ActivityManager.RunningAppProcessInfo
import android.app.ApplicationExitInfo
import android.content.Context
import android.content.SharedPreferences
import android.os.Build
import androidx.annotation.RequiresApi
import androidx.annotation.VisibleForTesting
import androidx.annotation.VisibleForTesting.Companion.PRIVATE
import androidx.core.content.edit
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.GleanMetrics.AppExitInfo
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.perf.ApplicationExitInfoMetrics.recordProcessExits

/**
 * Contains logic for recording the processes that exited in the previous sessions, i.e historical
 * [ApplicationExitInfo].
 */
object ApplicationExitInfoMetrics {

    private const val KILOBYTES_TO_MEGABYTES_CONVERSION = 1024.0

    // User-triggered reasons (USER_REQUESTED, USER_STOPPED, PERMISSION_CHANGE, PACKAGE_*) are excluded.
    @RequiresApi(Build.VERSION_CODES.R)
    @VisibleForTesting(otherwise = PRIVATE)
    internal val TRACKED_REASONS = buildList {
        add(ApplicationExitInfo.REASON_ANR)
        add(ApplicationExitInfo.REASON_CRASH)
        add(ApplicationExitInfo.REASON_CRASH_NATIVE)
        add(ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE)
        add(ApplicationExitInfo.REASON_LOW_MEMORY)
        add(ApplicationExitInfo.REASON_SIGNALED)
        add(ApplicationExitInfo.REASON_OTHER)
        add(ApplicationExitInfo.REASON_DEPENDENCY_DIED)
        add(ApplicationExitInfo.REASON_INITIALIZATION_FAILURE)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            add(ApplicationExitInfo.REASON_FREEZER)
        }
    }

    @VisibleForTesting(otherwise = PRIVATE) internal const val PREFERENCE_NAME = "app_exit_info"

    private val logger = Logger("ApplicationExitInfoMetrics")

    /**
     * Returns all historical process exits mapped to [ProcessExitRecord] for display purposes. Unlike
     * [recordProcessExits], this does not apply timestamp deduplication and returns the full contents of the ring
     * buffer.
     *
     * @param context Application [Context]
     */
    @RequiresApi(Build.VERSION_CODES.R)
    fun getProcessExitsForDisplay(context: Context): List<ProcessExitRecord> =
        getHistoricalProcessExits(context).map { exit ->
            ProcessExitRecord(
                date = exit.timestamp.toDateTimeFormat(),
                reason = exit.toProcessExitReason() ?: "unknown",
                processType = exit.processName.toProcessType(),
                importance = exit.importance.toProcessImportance() ?: "unknown",
                pssInMb = exit.pss.toValueInMB(),
                rssInMb = exit.rss.toValueInMB(),
            )
        }

    /**
     * Records process exits.
     *
     * @param context Application [Context]
     */
    @RequiresApi(Build.VERSION_CODES.R)
    fun recordProcessExits(context: Context) {
        val historicalExitReasons = getHistoricalProcessExits(context)

        // return if there's no recent process exit
        if (historicalExitReasons.isEmpty()) return

        // get last time a process exit was recorded
        val lastTimeHandled = getLastTimeHandled(context)

        // return if there's no more recent process exit than the lastTimeHandled
        if (!shouldRecordProcessExit(lastTimeHandled, historicalExitReasons[0].timestamp)) return

        // record the process exits
        record(context, lastTimeHandled, historicalExitReasons)
    }

    // Some OEM AMS implementations violate the platform contract and throw
    // IllegalArgumentException from getHistoricalProcessExitReasons (bug 2072086). Return an
    // empty list on failure — callers already treat empty as "no data".
    @Suppress("TooGenericExceptionCaught")
    @RequiresApi(Build.VERSION_CODES.R)
    private fun getHistoricalProcessExits(context: Context): List<ApplicationExitInfo> {
        val activityManager = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val applicationExitInfoList =
            try {
                activityManager.getHistoricalProcessExitReasons(null, 0, 0)
            } catch (e: RuntimeException) {
                logger.warn("getHistoricalProcessExitReasons threw", e)
                return emptyList()
            }
        applicationExitInfoList.retainAll {
            shouldRetainApplicationExitInfo(it)
        }
        return applicationExitInfoList
    }

    @RequiresApi(Build.VERSION_CODES.R)
    private fun record(
        context: Context,
        lastTimeHandled: Long,
        historicalExitReasons: List<ApplicationExitInfo>,
    ) {
        for (historicalExit in historicalExitReasons) {
            // only record process exits happened after lastTimeHandled
            if (lastTimeHandled < historicalExit.timestamp) {
                // We intentionally exclude recording the `historicalExit.description` and
                // `historicalExit.status` values.
                //
                // - `description` was removed after prior analysis showed low value due to
                // OEM-specific, non-standardized data that cannot be aggregated.
                // - `status` (e.g., SIGNALED exits) was not useful for the metric’s goal.
                //
                // This metric focuses on actionable tab reload causes (e.g., low memory),
                // while crashes/ANRs are covered by dedicated tooling.
                AppExitInfo.processExited.record(
                    AppExitInfo.ProcessExitedExtra(
                        date = historicalExit.timestamp.toSimpleDateFormat(),
                        importance = historicalExit.importance.toProcessImportance(),
                        processType = historicalExit.processName.toProcessType(),
                        pss = historicalExit.pss.toValueInMB(),
                        rss = historicalExit.rss.toValueInMB(),
                        reason = historicalExit.toProcessExitReason(),
                    )
                )
            }
        }

        // The historical process exit info are stored in a ring buffer with a limited size.
        // This buffer persists across app sessions. We need to keep an index of the most recently
        // recorded process exit info so that we do not record the same entries in the buffer
        // multiple times.
        updateLastTimeHandled(context, historicalExitReasons[0].timestamp)
    }

    @RequiresApi(Build.VERSION_CODES.R)
    private fun shouldRecordProcessExit(
        lastTimeHandled: Long,
        mostRecentProcessExitTimestamp: Long,
    ): Boolean {
        return mostRecentProcessExitTimestamp > lastTimeHandled
    }

    private fun shouldRetainApplicationExitInfo(appExitInfo: ApplicationExitInfo?): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            TRACKED_REASONS.contains(appExitInfo?.reason)
        } else {
            false
        }
    }

    private fun getLastTimeHandled(context: Context): Long {
        return preferences(context)
            .getLong(
                context.getPreferenceKey(R.string.pref_key_application_exit_info_last_handled_time),
                -1,
            )
    }

    private fun updateLastTimeHandled(
        context: Context,
        mostRecentProcessExitTimestamp: Long,
    ) {
        preferences(context).edit {
            putLong(
                context.getPreferenceKey(R.string.pref_key_application_exit_info_last_handled_time),
                mostRecentProcessExitTimestamp,
            )
        }
    }

    @VisibleForTesting(otherwise = PRIVATE)
    internal fun String.toProcessType(): String {
        if (':' !in this) return "parent"
        val suffix = substringAfter(':')
        return when {
            suffix.contains("tab", ignoreCase = true) -> "content"
            suffix.contains("gpu", ignoreCase = true) -> "gpu"
            suffix.contains("media", ignoreCase = true) -> "media"
            suffix.contains("utility", ignoreCase = true) -> "utility"
            else -> "other"
        }
    }

    private fun Long.toValueInMB(): Int {
        return (this / KILOBYTES_TO_MEGABYTES_CONVERSION).toInt()
    }

    @RequiresApi(Build.VERSION_CODES.R)
    private fun ApplicationExitInfo.toProcessExitReason(): String? {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU && reason == ApplicationExitInfo.REASON_FREEZER) {
            return "freezer"
        }
        return when (reason) {
            ApplicationExitInfo.REASON_ANR -> "anr"
            ApplicationExitInfo.REASON_CRASH -> "crash"
            ApplicationExitInfo.REASON_CRASH_NATIVE -> "crash_native"
            ApplicationExitInfo.REASON_LOW_MEMORY -> "low_memory"
            ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE -> "excessive_resource"
            ApplicationExitInfo.REASON_SIGNALED -> "signaled"
            ApplicationExitInfo.REASON_DEPENDENCY_DIED -> "dependency_died"
            ApplicationExitInfo.REASON_INITIALIZATION_FAILURE -> "initialization_failure"
            ApplicationExitInfo.REASON_OTHER ->
                if (description?.contains("MemoryLimiter:AnonSwap") == true) {
                    "memory_limiter"
                } else {
                    "other"
                }

            else -> null
        }
    }

    private fun Int.toProcessImportance(): String? {
        return when (this) {
            RunningAppProcessInfo.IMPORTANCE_CACHED -> "cached"
            RunningAppProcessInfo.IMPORTANCE_CANT_SAVE_STATE -> "cant_save_state"
            RunningAppProcessInfo.IMPORTANCE_FOREGROUND -> "foreground"
            RunningAppProcessInfo.IMPORTANCE_FOREGROUND_SERVICE -> "foreground_service"
            RunningAppProcessInfo.IMPORTANCE_GONE -> "gone"
            RunningAppProcessInfo.IMPORTANCE_PERCEPTIBLE -> "perceptible"
            RunningAppProcessInfo.IMPORTANCE_SERVICE -> "service"
            RunningAppProcessInfo.IMPORTANCE_TOP_SLEEPING -> "top_sleeping"
            RunningAppProcessInfo.IMPORTANCE_VISIBLE -> "visible"
            else -> null
        }
    }

    private fun Long.toSimpleDateFormat(): String {
        val date = Date(this)
        return SimpleDateFormat("yyyy-MM-dd", Locale.US).format(date)
    }

    private fun Long.toDateTimeFormat(): String {
        val date = Date(this)
        return SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(date)
    }

    private fun preferences(context: Context): SharedPreferences =
        context.getSharedPreferences(PREFERENCE_NAME, Context.MODE_PRIVATE)
}
