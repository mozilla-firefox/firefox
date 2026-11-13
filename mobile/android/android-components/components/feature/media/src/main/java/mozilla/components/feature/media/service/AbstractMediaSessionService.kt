/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.service

import android.app.Service
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.os.Binder
import android.os.IBinder
import androidx.annotation.VisibleForTesting
import java.lang.ref.WeakReference
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.support.base.android.NotificationsDelegate

/** [Binder] offering access to this service and all operations it can perform. */
internal class MediaServiceBinder(delegate: MediaSessionDelegate) : Binder() {
    // Necessary to prevent the delegate leaking Context when the MediaService is destroyed.
    @get:VisibleForTesting internal val service = WeakReference(delegate)

    /**
     * Get an instance of [MediaSessionDelegate] which supports showing/hiding and updating a media notification based
     * on the passed in [SessionState].
     */
    fun getMediaService(): MediaSessionDelegate? = service.get()
}

/**
 * A foreground service that will keep the process alive while we are playing media (with the app possibly in the
 * background) and shows an ongoing notification indicating the current media playing status.
 */
abstract class AbstractMediaSessionService : Service() {
    protected abstract val store: BrowserStore
    protected abstract val crashReporter: CrashReporting?
    protected abstract val notificationsDelegate: NotificationsDelegate

    @VisibleForTesting internal var binder: MediaServiceBinder? = null

    @VisibleForTesting internal var delegate: MediaSessionServiceDelegate? = null

    override fun onCreate() {
        super.onCreate()

        delegate =
            MediaSessionServiceDelegate(
                    context = this,
                    service = this,
                    store = store,
                    crashReporter = crashReporter,
                    notificationsDelegate = notificationsDelegate,
                )
                .also {
                    binder = MediaServiceBinder(it)
                }

        delegate?.onCreate()
    }

    override fun onDestroy() {
        delegate?.onDestroy()
        binder = null
        delegate = null

        super.onDestroy()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        delegate?.onStartCommand(intent)
        return START_NOT_STICKY
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        delegate?.onTaskRemoved()
    }

    override fun onBind(intent: Intent?): IBinder? {
        return binder
    }

    companion object {
        internal const val ACTION_PLAY = "mozac.feature.mediasession.service.PLAY"
        internal const val ACTION_PAUSE = "mozac.feature.mediasession.service.PAUSE"
        internal const val ACTION_NEXT_TRACK = "mozac.feature.mediasession.service.NEXT_TRACK"
        internal const val ACTION_PREV_TRACK = "mozac.feature.mediasession.service.PREV_TRACK"

        const val NOTIFICATION_TAG = "mozac.feature.mediasession.foreground-service"
        const val PENDING_INTENT_TAG = "mozac.feature.mediasession.pendingintent"

        internal fun playIntent(context: Context, cls: Class<*>): Intent =
            Intent(ACTION_PLAY).apply {
                component = ComponentName(context, cls)
            }

        internal fun pauseIntent(context: Context, cls: Class<*>): Intent =
            Intent(ACTION_PAUSE).apply {
                component = ComponentName(context, cls)
            }

        internal fun nextTrackIntent(context: Context, cls: Class<*>): Intent =
            Intent(ACTION_NEXT_TRACK).apply {
                component = ComponentName(context, cls)
            }

        internal fun previousTrackIntent(context: Context, cls: Class<*>): Intent =
            Intent(ACTION_PREV_TRACK).apply {
                component = ComponentName(context, cls)
            }
    }
}
