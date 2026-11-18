/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.middleware

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import androidx.annotation.VisibleForTesting
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import kotlinx.coroutines.CoroutineScope
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.CustomTabListAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.concept.engine.media.RecordingDevice
import mozilla.components.feature.media.R
import mozilla.components.feature.media.notification.MediaNotificationChannel
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.support.base.android.OnPermissionGranted
import mozilla.components.support.base.ids.SharedIdsHelper
import mozilla.components.ui.icons.R as iconsR

private const val NOTIFICATION_TAG = "mozac.feature.media.recordingDevices"
private const val NOTIFICATION_ID = 1
private const val PENDING_INTENT_TAG = "mozac.feature.media.pendingintent"

/**
 * Middleware for displaying an ongoing notification while recording devices (camera, microphone) are used by web
 * content.
 */
class RecordingDevicesMiddleware(
    private val context: Context,
    private val notificationsDelegate: NotificationsDelegate,
    scope: CoroutineScope,
) : Middleware<BrowserState, BrowserAction> {
    private var isShowingNotification: Boolean = false

    @Volatile private var isRecording: Boolean = false

    @Volatile private var store: Store<BrowserState, BrowserAction>? = null

    private val dismissReceiver =
        NotificationDismissedReceiver(context, scope) {
            store?.let {
                synchronized(it) {
                    isShowingNotification = false
                    process(it, true)
                }
            }
        }

    override fun invoke(
        store: Store<BrowserState, BrowserAction>,
        next: (BrowserAction) -> Unit,
        action: BrowserAction,
    ) {
        next(action)
        if (this.store == null) {
            this.store = store
        }

        // Whenever the recording devices of a tab change or tabs get added/removed then process
        // the current list and show/hide the notification.
        if (action is ContentAction.SetRecordingDevices || action is TabListAction || action is CustomTabListAction) {
            process(store, false)
        }
    }

    private fun process(
        store: Store<BrowserState, BrowserAction>,
        isReminder: Boolean,
    ) {
        val devices =
            (store.state.tabs + store.state.customTabs)
                .map { tab -> tab.content.recordingDevices }
                .flatten()
                .filter { device -> device.status == RecordingDevice.Status.RECORDING }
                .distinctBy { device -> device.type }

        val isUsingCamera = devices.find { it.type == RecordingDevice.Type.CAMERA } != null
        val isUsingMicrophone = devices.find { it.type == RecordingDevice.Type.MICROPHONE } != null

        val recordingState =
            when {
                isUsingCamera && isUsingMicrophone -> RecordingState.CameraAndMicrophone
                isUsingCamera -> RecordingState.Camera
                isUsingMicrophone -> RecordingState.Microphone
                else -> RecordingState.None
            }

        updateNotification(
            recordingState,
            isReminder,
        )
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun updateNotification(
        recordingState: RecordingState,
        isReminder: Boolean = false,
    ) {
        isRecording = recordingState.isRecording
        if (recordingState.isRecording && !isShowingNotification) {
            showNotification(
                context,
                recordingState,
                notificationsDelegate,
                isReminder,
            ) {
                if (isRecording) {
                    dismissReceiver.register()
                    isShowingNotification = true
                } else {
                    NotificationManagerCompat.from(context).cancel(NOTIFICATION_TAG, NOTIFICATION_ID)
                }
            }
        } else if (!recordingState.isRecording && isShowingNotification) {
            hideNotification()
            isShowingNotification = false
        }
    }

    private fun hideNotification() {
        NotificationManagerCompat.from(context).cancel(NOTIFICATION_TAG, NOTIFICATION_ID)
        dismissReceiver.unregister()
    }

    private fun showNotification(
        context: Context,
        recordingState: RecordingState,
        notificationsDelegate: NotificationsDelegate,
        isReminder: Boolean = false,
        onPermissionGranted: OnPermissionGranted,
    ) {
        val channelId = MediaNotificationChannel.ensureChannelExists(context)

        val intent =
            context.packageManager.getLaunchIntentForPackage(context.packageName)?.apply {
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            } ?: throw IllegalStateException("Package has no launcher intent")

        val pendingIntent =
            PendingIntent.getActivity(
                context,
                SharedIdsHelper.getIdForTag(context, PENDING_INTENT_TAG),
                intent,
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
            )

        val dismissPendingIntent =
            PendingIntent.getBroadcast(
                context,
                0,
                Intent(ACTION_RECORDING_DEVICES_NOTIFICATION_DISMISSED),
                PendingIntent.FLAG_IMMUTABLE,
            )

        val textResource =
            if (isReminder) {
                context.getString(
                    recordingState.reminderTextResource,
                    context.packageManager.getApplicationLabel(context.applicationInfo).toString(),
                )
            } else {
                context.getString(recordingState.textResource)
            }

        val notification =
            NotificationCompat.Builder(context, channelId)
                .setSmallIcon(recordingState.iconResource)
                .setContentTitle(context.getString(recordingState.titleResource))
                .setContentText(textResource)
                .setPriority(NotificationCompat.PRIORITY_MAX)
                .setCategory(NotificationCompat.CATEGORY_CALL)
                .setContentIntent(pendingIntent)
                .setOngoing(true)
                .setDeleteIntent(dismissPendingIntent)
                .build()

        notificationsDelegate.notify(
            NOTIFICATION_TAG,
            NOTIFICATION_ID,
            notification,
            onPermissionGranted = onPermissionGranted,
        )
    }
}

internal sealed class RecordingState {
    abstract val iconResource: Int
    abstract val titleResource: Int
    abstract val textResource: Int
    abstract val reminderTextResource: Int

    val isRecording
        get() = this !is None

    object CameraAndMicrophone : RecordingState() {
        override val iconResource = iconsR.drawable.mozac_ic_camera_24
        override val titleResource = R.string.mozac_feature_media_sharing_camera_and_microphone
        override val textResource = R.string.mozac_feature_media_sharing_camera_and_microphone_text
        override val reminderTextResource = R.string.mozac_feature_media_sharing_camera_and_microphone_reminder_text_2
    }

    object Camera : RecordingState() {
        override val iconResource = iconsR.drawable.mozac_ic_camera_24
        override val titleResource = R.string.mozac_feature_media_sharing_camera
        override val textResource = R.string.mozac_feature_media_sharing_camera_text
        override val reminderTextResource = R.string.mozac_feature_media_sharing_camera_reminder_text
    }

    object Microphone : RecordingState() {
        override val iconResource = iconsR.drawable.mozac_ic_microphone_24
        override val titleResource = R.string.mozac_feature_media_sharing_microphone
        override val textResource = R.string.mozac_feature_media_sharing_microphone_text
        override val reminderTextResource = R.string.mozac_feature_media_sharing_microphone_reminder_text_2
    }

    object None : RecordingState() {
        override val iconResource: Int
            get() = throw UnsupportedOperationException()

        override val titleResource: Int
            get() = throw UnsupportedOperationException()

        override val textResource: Int
            get() = throw UnsupportedOperationException()

        override val reminderTextResource: Int
            get() = throw UnsupportedOperationException()
    }
}
