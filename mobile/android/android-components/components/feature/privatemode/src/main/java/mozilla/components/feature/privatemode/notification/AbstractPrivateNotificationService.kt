/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.privatemode.notification

import android.app.ForegroundServiceStartNotAllowedException
import android.app.Notification
import android.app.PendingIntent
import android.app.PendingIntent.FLAG_ONE_SHOT
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.Build.VERSION.SDK_INT
import android.os.IBinder
import androidx.annotation.CallSuper
import androidx.annotation.VisibleForTesting
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationCompat.VISIBILITY_SECRET
import androidx.core.app.NotificationManagerCompat.IMPORTANCE_LOW
import java.util.Locale
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.selector.privateTabs
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.feature.privatemode.R
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.support.base.ids.SharedIdsHelper
import mozilla.components.support.ktx.android.notification.ChannelData
import mozilla.components.support.ktx.android.notification.ensureNotificationChannelExists
import mozilla.components.support.utils.ext.stopForegroundCompat

/**
 * Manages notifications for private tabs.
 *
 * Private tab notifications solve two problems:
 * 1. They allow users to interact with the browser from outside of the app (example: by closing all private tabs).
 * 2. The notification will keep the process alive, allowing the browser to keep private tabs in memory.
 *
 * As long as a private tab is open this service will keep its notification alive.
 */
@Suppress("TooManyFunctions")
abstract class AbstractPrivateNotificationService(
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
    private val notificationScope: CoroutineScope = CoroutineScope(Dispatchers.IO),
) : Service() {
    private var privateTabsScope: CoroutineScope? = null
    private var localeScope: CoroutineScope? = null

    abstract val store: BrowserStore
    abstract val notificationsDelegate: NotificationsDelegate

    protected open val crashReporter: CrashReporting? = null

    /** Customizes the private browsing notification. */
    abstract fun NotificationCompat.Builder.buildNotification()

    /** Customize the notification response when the [Locale] has been changed. */
    abstract fun notifyLocaleChanged()

    /** Erases all private tabs in reaction to the user tapping the notification. */
    @CallSuper
    protected open fun erasePrivateTabs() {
        store.dispatch(TabListAction.RemoveAllPrivateTabsAction)
    }

    /** Retrieves the notification id based on the tag. */
    protected fun getNotificationId(): Int {
        return SharedIdsHelper.getIdForTag(this, NOTIFICATION_TAG)
    }

    /** Retrieves the channel id based on the channel data. */
    protected fun getChannelId(): String {
        return ensureNotificationChannelExists(
            this,
            NOTIFICATION_CHANNEL,
            onSetupChannel = {
                enableLights(false)
                enableVibration(false)
                setShowBadge(false)
            },
        )
    }

    /** Re-build and notify an existing notification. */
    protected fun refreshNotification() {
        notificationScope.launch {
            val notificationId = getNotificationId()
            val channelId = getChannelId()

            val notification = createNotification(channelId)
            withContext(mainDispatcher) {
                notificationsDelegate.notify(notificationId = notificationId, notification = notification)
            }
        }
    }

    /**
     * Create the private browsing notification and add a listener to stop the service once all private tabs are closed.
     *
     * The service should be started only if private tabs are open.
     */
    final override fun onCreate() {
        notificationScope.launch {
            val notificationId = getNotificationId()
            val channelId = getChannelId()
            val notification = createNotification(channelId)

            if (SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                notificationsDelegate.requestNotificationPermission(onPermissionGranted = { refreshNotification() })
            }

            withContext(mainDispatcher) {
                tryStartForeground(notificationId, notification)
            }
        }

        privateTabsScope =
            store.flowScoped(dispatcher = mainDispatcher) { flow ->
                flow
                    .map { state -> state.privateTabs.isEmpty() }
                    .distinctUntilChanged()
                    .collect { noPrivateTabs ->
                        if (noPrivateTabs) stopService()
                    }
            }

        localeScope =
            store.flowScoped(dispatcher = mainDispatcher) { flow ->
                flow
                    .mapNotNull { state -> state.locale }
                    .distinctUntilChanged()
                    .collect {
                        notifyLocaleChanged()
                    }
            }
    }

    @Suppress("TooGenericExceptionCaught")
    private fun tryStartForeground(notificationId: Int, notification: Notification) {
        try {
            startForeground(notificationId, notification)
        } catch (e: Exception) {
            if (SDK_INT >= Build.VERSION_CODES.S && e is ForegroundServiceStartNotAllowedException) {
                // similar to https://bugzilla.mozilla.org/show_bug.cgi?id=1802620 we prefer to catch this.
                // see https://bugzilla.mozilla.org/show_bug.cgi?id=2065344#c7 and linked conversations for more
                // details.
                crashReporter?.submitCaughtException(e)
                return
            } else {
                throw e
            }
        }
    }

    /**
     * Builds a notification based on the specified channel id.
     *
     * @param channelId The channel id for the [Notification]
     */
    private fun createNotification(channelId: String): Notification {
        val eraseIntent =
            Intent(ACTION_ERASE).let { intent ->
                intent.setClass(this, this::class.java)
                PendingIntent.getService(
                    this,
                    0,
                    intent,
                    PendingIntent.FLAG_IMMUTABLE or FLAG_ONE_SHOT,
                )
            }

        return NotificationCompat.Builder(this, channelId)
            .setOngoing(true)
            .setVisibility(VISIBILITY_SECRET)
            .setShowWhen(false)
            .setLocalOnly(true)
            .setContentIntent(eraseIntent)
            .apply {
                if (SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                    setDeleteIntent(eraseIntent)
                }

                buildNotification()
            }
            .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
            .build()
    }

    final override fun onStartCommand(intent: Intent, flags: Int, startId: Int): Int {
        if (intent.action == ACTION_ERASE) {
            erasePrivateTabs()
        }

        return START_NOT_STICKY
    }

    final override fun onDestroy() {
        privateTabsScope?.cancel()
        localeScope?.cancel()
    }

    final override fun onBind(intent: Intent?): IBinder? = null

    final override fun onTaskRemoved(rootIntent: Intent) {
        if (
            rootIntent.action in defaultIgnoreTaskActions ||
                rootIntent.action in ignoreTaskActions() ||
                rootIntent.component?.className in ignoreTaskComponentClasses()
        ) {
            // The app may have multiple tasks (e.g. for PWAs). If tasks get removed that are not
            // the main browser task then we do not want to remove all private tabs here.
            // I am not sure whether we can reliably identify the main task since it can be launched
            // from multiple entry points (e.g. the launcher, VIEW intents, ..).
            // So instead we ignore tasks with root intents that we absolutely do not want to handle
            // here (e.g. PWAs) and then extend the list if needed.
            return
        }

        store.dispatch(TabListAction.RemoveAllPrivateTabsAction)
        stopService()
    }

    private fun stopService() {
        stopForegroundCompat(true)
        stopSelf()
    }

    /**
     * Builds a list of Intent actions that will get ignored when they are in the root intent that gets passed to
     * onTaskRemoved().
     */
    abstract fun ignoreTaskActions(): List<String>

    /**
     * Builds a list of Intent components' qualified class name that will get ignored when they are in the root intent
     * that gets passed to onTaskRemoved().
     */
    abstract fun ignoreTaskComponentClasses(): List<String>

    companion object {
        private const val NOTIFICATION_TAG =
            "mozilla.components.feature.privatemode.notification.AbstractPrivateNotificationService"
        const val ACTION_ERASE = "mozilla.components.feature.privatemode.action.ERASE"

        val NOTIFICATION_CHANNEL =
            ChannelData(
                id = "browsing-session",
                name = R.string.mozac_feature_privatemode_notification_channel_name,
                importance = IMPORTANCE_LOW,
            )

        // List of default Intent actions that will get ignored
        // when they are in the root intent that gets passed to onTaskRemoved().
        @VisibleForTesting internal val defaultIgnoreTaskActions = listOf("mozilla.components.feature.pwa.VIEW_PWA")
    }
}
