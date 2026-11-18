/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.middleware

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Binder
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.ext.registerReceiverCompat

internal const val ACTION_RECORDING_DEVICES_NOTIFICATION_DISMISSED =
    "mozac.feature.media.recordingDevices.notificationDismissed"
private val logger = Logger("NotificationDismissedReceiver")
private const val NOTIFICATION_REMINDER_DELAY_MS = 5 * 60 * 1000L // 5 minutes

/**
 * Listens for the recording devices notification being dismissed by the user and invokes [onDismissed] after a reminder
 * delay.
 *
 * A single instance is registered at most once at a time, so repeated calls to [register] do not leak receivers.
 */
internal class NotificationDismissedReceiver(
    private val context: Context,
    private val scope: CoroutineScope,
    private val onDismissed: () -> Unit,
) : BroadcastReceiver() {
    private var isRegistered = false
    private var reminderJob: Job? = null

    override fun onReceive(context: Context, intent: Intent) {
        val callingAppInfo = context.packageManager.getNameForUid(Binder.getCallingUid())
        if (callingAppInfo.equals(context.packageName)) {
            scheduleReminder()
        }
    }

    @Synchronized
    private fun scheduleReminder() {
        if (!isRegistered) return
        reminderJob?.cancel()
        reminderJob = scope.launch {
            delay(NOTIFICATION_REMINDER_DELAY_MS)
            onDismissed()
        }
    }

    @Synchronized
    fun register() {
        if (isRegistered) return
        try {
            context.registerReceiverCompat(
                this,
                IntentFilter(ACTION_RECORDING_DEVICES_NOTIFICATION_DISMISSED),
                ContextCompat.RECEIVER_EXPORTED,
            )
            isRegistered = true
        } catch (e: IllegalStateException) {
            logger.error("Failed to register the notification dismissed receiver", e)
        }
    }

    @Synchronized
    fun unregister() {
        reminderJob?.cancel()
        if (!isRegistered) return
        try {
            context.unregisterReceiver(this)
        } catch (_: IllegalArgumentException) {}
        isRegistered = false
    }
}
