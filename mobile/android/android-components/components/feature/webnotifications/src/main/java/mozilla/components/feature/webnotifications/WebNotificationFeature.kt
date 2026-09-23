/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.webnotifications

import android.app.Activity
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import androidx.annotation.DrawableRes
import androidx.core.app.NotificationCompat
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.browser.icons.BrowserIcons
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.permission.SitePermissionsStorage
import mozilla.components.concept.engine.webnotifications.WebNotification
import mozilla.components.concept.engine.webnotifications.WebNotificationDelegate
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.support.base.ids.SharedIdsHelper
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.kotlin.getOrigin

private const val NOTIFICATION_CHANNEL_ID = "mozac.feature.webnotifications.generic.channel"
private const val PENDING_INTENT_TAG = "mozac.feature.webnotifications.generic.pendingintent"
internal const val NOTIFICATION_ID = 1

/**
 * Feature implementation for configuring and displaying web notifications to the user.
 *
 * Initialize this feature globally once on app start
 *
 * ```Kotlin
 * WebNotificationFeature(
 *     applicationContext, engine, icons, R.mipmap.ic_launcher, sitePermissionsStorage, BrowserActivity::class.java
 * )
 * ```
 *
 * @param context The application Context.
 * @param engine The browser engine.
 * @param browserIcons The entry point for loading the large icon for the notification.
 * @param smallIcon The small icon for the notification.
 * @param sitePermissionsStorage The storage for checking notification site permissions.
 * @param activityClass The Activity that the notification will launch if user taps on it
 * @param coroutineContext An instance of [CoroutineContext] used for executing async site permission checks.
 */
class WebNotificationFeature(
    private val context: Context,
    private val engine: Engine,
    browserIcons: BrowserIcons,
    @DrawableRes smallIcon: Int,
    private val sitePermissionsStorage: SitePermissionsStorage,
    private val activityClass: Class<out Activity>?,
    private val coroutineContext: CoroutineContext = Dispatchers.IO,
    private val notificationsDelegate: NotificationsDelegate,
) : WebNotificationDelegate {
    private val logger = Logger("WebNotificationFeature")
    private val nativeNotificationBridge = NativeNotificationBridge(browserIcons, smallIcon)

    init {
        try {
            engine.registerWebNotificationDelegate(this)
        } catch (e: UnsupportedOperationException) {
            logger.error("failed to register for web notification delegate", e)
        }
    }

    override fun onShowNotification(webNotification: WebNotification): Deferred<Boolean> {
        val deferred = CompletableDeferred<Boolean>()
        CoroutineScope(coroutineContext).launch {
            // Only need to check permissions for notifications from web pages. Permissions for
            // web extensions are managed via the extension's manifest and approved by the user
            // upon installation.
            if (!webNotification.triggeredByWebExtension) {
                val origin = webNotification.sourceUrl?.getOrigin() ?: return@launch
                val permissions =
                    sitePermissionsStorage.findSitePermissionsBy(
                        origin,
                        private = webNotification.privateBrowsing,
                    ) ?: return@launch

                if (!permissions.notification.isAllowed()) {
                    return@launch
                }
            }

            ensureNotificationGroupAndChannelExists()
            notificationsDelegate.notificationManagerCompat.cancel(webNotification.tag, NOTIFICATION_ID)

            val notification =
                nativeNotificationBridge.convertToAndroidNotification(
                    webNotification,
                    context,
                    NOTIFICATION_CHANNEL_ID,
                    activityClass,
                    SharedIdsHelper.getNextIdForTag(context, PENDING_INTENT_TAG),
                )
            notificationsDelegate.notify(
                webNotification.tag,
                NOTIFICATION_ID,
                notification,
                onPermissionGranted = {
                    deferred.complete(true)
                },
                onPermissionRejected = {
                    deferred.complete(false)
                },
            )
        }
        return deferred
    }

    override fun onCloseNotification(webNotification: WebNotification) {
        notificationsDelegate.notificationManagerCompat.cancel(webNotification.tag, NOTIFICATION_ID)
    }

    private fun ensureNotificationGroupAndChannelExists() {
        val channel =
            NotificationChannel(
                NOTIFICATION_CHANNEL_ID,
                context.getString(R.string.mozac_feature_notification_channel_name),
                NotificationManager.IMPORTANCE_DEFAULT,
            )
        channel.setShowBadge(true)
        channel.lockscreenVisibility = NotificationCompat.VISIBILITY_PRIVATE

        notificationsDelegate.notificationManagerCompat.createNotificationChannel(channel)
    }
}
