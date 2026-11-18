/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.middleware

import android.app.NotificationManager
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.content.IntentFilter
import android.os.Binder
import android.os.Handler
import android.os.Looper
import androidx.core.app.NotificationManagerCompat
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.time.Duration.Companion.minutes
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.CustomTabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.media.RecordingDevice
import mozilla.components.feature.media.R
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyBoolean
import org.mockito.ArgumentMatchers.anyInt
import org.mockito.Mockito
import org.robolectric.Shadows

@RunWith(AndroidJUnit4::class)
class RecordingDevicesMiddlewareTest {
    private lateinit var notificationsDelegate: NotificationsDelegate

    @Before
    fun setup() {
        // Prepare the PackageManager to answer getLaunchIntentForPackage call.
        val applicationManager = Shadows.shadowOf(testContext.packageManager)

        val activityComponent = ComponentName(testContext.packageName, "Test")
        applicationManager.addActivityIfNotPresent(activityComponent)

        applicationManager.addIntentFilterForActivity(
            activityComponent,
            IntentFilter(Intent.ACTION_MAIN).apply { addCategory(Intent.CATEGORY_INFO) },
        )

        val notificationManagerCompat: NotificationManagerCompat =
            Mockito.spy(NotificationManagerCompat.from(testContext))

        notificationsDelegate = NotificationsDelegate(notificationManagerCompat)

        whenever(notificationManagerCompat.areNotificationsEnabled()).thenReturn(true)
    }

    @Test
    fun `updateNotification should show notification once when recording`() = runTest {
        val realNotificationManager = testContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        val notificationManager = Shadows.shadowOf(realNotificationManager)

        assertEquals(0, notificationManager.size())

        val middleware = RecordingDevicesMiddleware(testContext, notificationsDelegate, this)

        middleware.updateNotification(RecordingState.Camera)

        assertEquals(1, notificationManager.size())

        // Another update with the same state to ensure that the notification is shown once.
        middleware.updateNotification(RecordingState.CameraAndMicrophone)

        assertEquals(1, notificationManager.size())
    }

    @Test
    fun `updateNotification hides notification when it has shown notification`() = runTest {
        val realNotificationManager = testContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        val notificationManager = Shadows.shadowOf(realNotificationManager)

        assertEquals(0, notificationManager.size())

        val middleware = RecordingDevicesMiddleware(testContext, notificationsDelegate, this)

        middleware.updateNotification(RecordingState.Camera)

        assertEquals(1, notificationManager.size())

        middleware.updateNotification(RecordingState.None)

        assertEquals(0, notificationManager.size())
    }

    @Test
    fun `middleware shows notification when tab has a recording device then hides when recording devices become inactive`() =
        runTest {
            val realNotificationManager =
                testContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            val notificationManager = Shadows.shadowOf(realNotificationManager)

            val middleware = RecordingDevicesMiddleware(testContext, notificationsDelegate, this)
            val store =
                BrowserStore(
                    initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla"))),
                    middleware = listOf(middleware),
                )

            assertEquals(0, notificationManager.size())

            store.dispatch(
                ContentAction.SetRecordingDevices(
                    sessionId = "mozilla",
                    devices = listOf(RecordingDevice(RecordingDevice.Type.CAMERA, RecordingDevice.Status.RECORDING)),
                )
            )

            assertEquals(1, notificationManager.size())

            store.dispatch(
                ContentAction.SetRecordingDevices(
                    sessionId = "mozilla",
                    devices = emptyList(),
                )
            )

            assertEquals(0, notificationManager.size())
        }

    @Test
    fun `middleware shows notification when custom tab has a recording device then hides when recording devices become inactive`() =
        runTest {
            val realNotificationManager =
                testContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            val notificationManager = Shadows.shadowOf(realNotificationManager)

            val middleware = RecordingDevicesMiddleware(testContext, notificationsDelegate, this)
            val store =
                BrowserStore(
                    initialState =
                        BrowserState(customTabs = listOf(createCustomTab("https://www.mozilla.org", id = "mozilla"))),
                    middleware = listOf(middleware),
                )

            assertEquals(0, notificationManager.size())

            store.dispatch(
                ContentAction.SetRecordingDevices(
                    sessionId = "mozilla",
                    devices =
                        listOf(RecordingDevice(RecordingDevice.Type.MICROPHONE, RecordingDevice.Status.RECORDING)),
                )
            )

            assertEquals(1, notificationManager.size())

            store.dispatch(
                ContentAction.SetRecordingDevices(
                    sessionId = "mozilla",
                    devices = emptyList(),
                )
            )

            assertEquals(0, notificationManager.size())
        }

    @Test
    fun `middleware hides notification when a recording custom tab is removed`() = runTest {
        val realNotificationManager = testContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        val notificationManager = Shadows.shadowOf(realNotificationManager)

        val middleware = RecordingDevicesMiddleware(testContext, notificationsDelegate, this)
        val store =
            BrowserStore(
                initialState =
                    BrowserState(customTabs = listOf(createCustomTab("https://www.mozilla.org", id = "mozilla"))),
                middleware = listOf(middleware),
            )

        store.dispatch(
            ContentAction.SetRecordingDevices(
                sessionId = "mozilla",
                devices = listOf(RecordingDevice(RecordingDevice.Type.MICROPHONE, RecordingDevice.Status.RECORDING)),
            )
        )

        assertEquals(1, notificationManager.size())

        store.dispatch(CustomTabListAction.RemoveCustomTabAction(tabId = "mozilla"))

        assertEquals(0, notificationManager.size())
    }

    @Test
    fun `updateNotification unregisters broadcast receiver when notification is hidden`() = runTest {
        val shadowApp = Shadows.shadowOf(testContext.applicationContext as android.app.Application)
        val middleware = RecordingDevicesMiddleware(testContext, notificationsDelegate, this)

        assertEquals(0, getDismissalReceiversCount(shadowApp))

        middleware.updateNotification(RecordingState.Camera)
        assertEquals(1, getDismissalReceiversCount(shadowApp))

        middleware.updateNotification(RecordingState.None)
        assertEquals(0, getDismissalReceiversCount(shadowApp))
    }

    @Test
    fun `updateNotification does not register a receiver when the notification is never shown`() = runTest {
        whenever(notificationsDelegate.notificationManagerCompat.areNotificationsEnabled()).thenReturn(false)

        val shadowApp = Shadows.shadowOf(testContext.applicationContext as android.app.Application)
        val middleware = RecordingDevicesMiddleware(testContext, notificationsDelegate, this)

        repeat(10) {
            middleware.updateNotification(RecordingState.Camera)
        }

        assertEquals(0, getDismissalReceiversCount(shadowApp))
    }

    @Test
    fun `updateNotification does not repeatedly register receivers when the notification is shown`() = runTest {
        val shadowApp = Shadows.shadowOf(testContext.applicationContext as android.app.Application)
        val middleware = RecordingDevicesMiddleware(testContext, notificationsDelegate, this)

        middleware.updateNotification(RecordingState.Camera)
        middleware.updateNotification(RecordingState.None)
        middleware.updateNotification(RecordingState.Microphone)
        middleware.updateNotification(RecordingState.None)
        middleware.updateNotification(RecordingState.Camera)

        assertEquals(1, getDismissalReceiversCount(shadowApp))
    }

    @Test
    fun `middleware does not leave a receiver registered when the notification was never shown`() = runTest {
        whenever(notificationsDelegate.notificationManagerCompat.areNotificationsEnabled()).thenReturn(false)

        val shadowApp = Shadows.shadowOf(testContext.applicationContext as android.app.Application)
        val middleware = RecordingDevicesMiddleware(testContext, notificationsDelegate, this)
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla"))),
                middleware = listOf(middleware),
            )

        store.dispatch(
            ContentAction.SetRecordingDevices(
                sessionId = "mozilla",
                devices = listOf(RecordingDevice(RecordingDevice.Type.CAMERA, RecordingDevice.Status.RECORDING)),
            )
        )
        assertEquals(0, getDismissalReceiversCount(shadowApp))

        store.dispatch(ContentAction.SetRecordingDevices(sessionId = "mozilla", devices = emptyList()))
        assertEquals(0, getDismissalReceiversCount(shadowApp))
    }

    @Test
    fun `updateNotification still hides the notification when registering the receiver fails`() = runTest {
        val context =
            object : ContextWrapper(testContext) {
                override fun registerReceiver(
                    receiver: BroadcastReceiver?,
                    filter: IntentFilter?,
                    flags: Int,
                ): Intent? {
                    throw IllegalStateException("Too many receivers")
                }

                override fun registerReceiver(
                    receiver: BroadcastReceiver?,
                    filter: IntentFilter?,
                    broadcastPermission: String?,
                    scheduler: Handler?,
                    flags: Int,
                ): Intent? {
                    throw IllegalStateException("Too many receivers")
                }

                override fun registerReceiver(receiver: BroadcastReceiver?, filter: IntentFilter?): Intent? {
                    throw IllegalStateException("Too many receivers")
                }
            }
        val shadowApp = Shadows.shadowOf(testContext.applicationContext as android.app.Application)
        val notificationManager =
            Shadows.shadowOf(testContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
        val middleware = RecordingDevicesMiddleware(context, notificationsDelegate, this)

        middleware.updateNotification(RecordingState.Camera)

        assertEquals(1, notificationManager.size())
        assertEquals(0, getDismissalReceiversCount(shadowApp))

        middleware.updateNotification(RecordingState.None)

        assertEquals(0, notificationManager.size())
    }

    @Test
    fun `notification granted after recording ended is cancelled and no receiver is registered`() = runTest {
        val realNotificationManager = testContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        val notificationManager = Shadows.shadowOf(realNotificationManager)
        val shadowApp = Shadows.shadowOf(testContext.applicationContext as android.app.Application)
        val delegate: NotificationsDelegate = mock()
        var grantPermission: () -> Unit = {}
        whenever(delegate.notify(any(), anyInt(), any(), any(), any(), anyBoolean())).thenAnswer {
            @Suppress("UNCHECKED_CAST")
            grantPermission = {
                realNotificationManager.notify(it.getArgument(0), it.getArgument(1), it.getArgument(2))
                (it.getArgument<() -> Unit>(3))()
            }
            Unit
        }
        val middleware = RecordingDevicesMiddleware(testContext, delegate, this)

        middleware.updateNotification(RecordingState.Camera)
        middleware.updateNotification(RecordingState.None)
        grantPermission()

        assertEquals(0, notificationManager.size())
        assertEquals(0, getDismissalReceiversCount(shadowApp))
    }

    @Test
    fun `middleware unregisters receiver when recording ends`() = runTest {
        val shadowApp = Shadows.shadowOf(testContext.applicationContext as android.app.Application)
        val middleware = RecordingDevicesMiddleware(testContext, notificationsDelegate, this)
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla"))),
                middleware = listOf(middleware),
            )

        store.dispatch(
            ContentAction.SetRecordingDevices(
                sessionId = "mozilla",
                devices = listOf(RecordingDevice(RecordingDevice.Type.CAMERA, RecordingDevice.Status.RECORDING)),
            )
        )
        assertEquals(1, getDismissalReceiversCount(shadowApp))

        store.dispatch(ContentAction.SetRecordingDevices(sessionId = "mozilla", devices = emptyList()))
        assertEquals(0, getDismissalReceiversCount(shadowApp))
    }

    @Test
    fun `dismissing the notification shows the reminder notification after the delay`() = runTest {
        val realNotificationManager = testContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        val notificationManager = Shadows.shadowOf(realNotificationManager)
        Shadows.shadowOf(testContext.packageManager).setNameForUid(Binder.getCallingUid(), testContext.packageName)

        val middleware = RecordingDevicesMiddleware(testContext, notificationsDelegate, this)
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla"))),
                middleware = listOf(middleware),
            )

        store.dispatch(
            ContentAction.SetRecordingDevices(
                sessionId = "mozilla",
                devices = listOf(RecordingDevice(RecordingDevice.Type.CAMERA, RecordingDevice.Status.RECORDING)),
            )
        )

        val initialText = Shadows.shadowOf(notificationManager.allNotifications.single()).contentText.toString()
        assertEquals(testContext.getString(R.string.mozac_feature_media_sharing_camera_text), initialText)

        testContext.sendBroadcast(Intent(ACTION_RECORDING_DEVICES_NOTIFICATION_DISMISSED))
        val mainLooper = Shadows.shadowOf(Looper.getMainLooper())
        mainLooper.idle()

        testScheduler.advanceTimeBy(4.minutes)
        assertEquals(
            initialText,
            Shadows.shadowOf(notificationManager.allNotifications.single()).contentText.toString(),
        )

        testScheduler.advanceTimeBy(2.minutes)

        val reminderText =
            testContext.getString(
                R.string.mozac_feature_media_sharing_camera_reminder_text,
                testContext.packageManager.getApplicationLabel(testContext.applicationInfo).toString(),
            )
        assertEquals(1, notificationManager.size())
        assertEquals(
            reminderText,
            Shadows.shadowOf(notificationManager.allNotifications.single()).contentText.toString(),
        )
    }

    @Test
    fun `pending reminder is cancelled when the notification is hidden`() = runTest {
        val realNotificationManager = testContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        val notificationManager = Shadows.shadowOf(realNotificationManager)
        Shadows.shadowOf(testContext.packageManager).setNameForUid(Binder.getCallingUid(), testContext.packageName)

        val middleware = RecordingDevicesMiddleware(testContext, notificationsDelegate, this)
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla"))),
                middleware = listOf(middleware),
            )
        val camera = listOf(RecordingDevice(RecordingDevice.Type.CAMERA, RecordingDevice.Status.RECORDING))

        store.dispatch(ContentAction.SetRecordingDevices(sessionId = "mozilla", devices = camera))
        testContext.sendBroadcast(Intent(ACTION_RECORDING_DEVICES_NOTIFICATION_DISMISSED))
        val mainLooper = Shadows.shadowOf(Looper.getMainLooper())
        mainLooper.idle()

        store.dispatch(ContentAction.SetRecordingDevices(sessionId = "mozilla", devices = emptyList()))
        store.dispatch(ContentAction.SetRecordingDevices(sessionId = "mozilla", devices = camera))
        testScheduler.advanceTimeBy(6.minutes)

        assertEquals(
            testContext.getString(R.string.mozac_feature_media_sharing_camera_text),
            Shadows.shadowOf(notificationManager.allNotifications.single()).contentText.toString(),
        )
    }

    private fun getDismissalReceiversCount(shadowApp: org.robolectric.shadows.ShadowApplication): Int {
        return shadowApp.registeredReceivers.count { wrapper ->
            wrapper.intentFilter.hasAction(ACTION_RECORDING_DEVICES_NOTIFICATION_DISMISSED)
        }
    }
}
