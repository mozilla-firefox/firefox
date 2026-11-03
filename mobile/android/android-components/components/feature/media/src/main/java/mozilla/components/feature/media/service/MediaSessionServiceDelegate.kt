/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.service

import android.app.ForegroundServiceStartNotAllowedException
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.AudioManager
import android.os.Build
import androidx.annotation.MainThread
import androidx.annotation.OptIn
import androidx.annotation.VisibleForTesting
import androidx.core.content.ContextCompat
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.MediaSession
import java.util.UUID
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.engine.mediasession.MediaSession as MozMediaSession
import mozilla.components.concept.engine.mediasession.MediaSession.PlaybackState.PAUSED
import mozilla.components.concept.engine.mediasession.MediaSession.PlaybackState.PLAYING
import mozilla.components.feature.media.facts.emitNotificationNextFact
import mozilla.components.feature.media.facts.emitNotificationPauseFact
import mozilla.components.feature.media.facts.emitNotificationPlayFact
import mozilla.components.feature.media.facts.emitNotificationPreviousFact
import mozilla.components.feature.media.facts.emitStatePauseFact
import mozilla.components.feature.media.facts.emitStatePlayFact
import mozilla.components.feature.media.facts.emitStateStopFact
import mozilla.components.feature.media.focus.AudioFocus
import mozilla.components.feature.media.notification.MediaNotification
import mozilla.components.feature.media.player.BrowserStorePlayer
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.support.base.ids.SharedIdsHelper
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.ext.registerReceiverCompat
import mozilla.components.support.utils.ext.stopForegroundCompat

@VisibleForTesting
internal class BecomingNoisyReceiver(private val onBecomingNoisy: () -> Unit) : BroadcastReceiver() {
    override fun onReceive(context: Context?, intent: Intent) {
        if (AudioManager.ACTION_AUDIO_BECOMING_NOISY == intent.action) {
            onBecomingNoisy()
        }
    }

    @VisibleForTesting
    fun deviceIsBecomingNoisy(context: Context) {
        val becomingNoisyIntent = Intent(AudioManager.ACTION_AUDIO_BECOMING_NOISY)
        onReceive(context, becomingNoisyIntent)
    }
}

/**
 * Delegate handling callbacks from an [AbstractMediaSessionService].
 *
 * The implementation was moved from [AbstractMediaSessionService] to this delegate for better testability.
 */
@OptIn(UnstableApi::class)
internal class MediaSessionServiceDelegate(
    @get:VisibleForTesting internal var context: Context,
    @get:VisibleForTesting internal val service: AbstractMediaSessionService,
    @get:VisibleForTesting internal val store: BrowserStore,
    @get:VisibleForTesting internal val crashReporter: CrashReporting?,
    @get:VisibleForTesting internal val notificationsDelegate: NotificationsDelegate,
) : MediaSessionDelegate {
    private val logger = Logger("MediaSessionService")

    @VisibleForTesting internal var notificationHelper = MediaNotification(context, service::class.java)

    @VisibleForTesting
    internal var player: BrowserStorePlayer =
        BrowserStorePlayer(context, store, onPlay = ::onMediaSessionPlay, onArtworkChanged = ::onArtworkChanged)

    // Media3 MediaSession enforces process-global ID uniqueness via a static registry keyed by id;
    // a random suffix keeps multiple concurrent instances (across tests or restart races) from colliding.
    @VisibleForTesting
    internal var mediaSession: MediaSession =
        MediaSession.Builder(context, player).setId("MozacMediaSession-${UUID.randomUUID()}").build()

    @VisibleForTesting
    internal var audioFocus =
        AudioFocus(
            context.getSystemService(Context.AUDIO_SERVICE) as AudioManager,
            store,
            onTransientFocusLoss = { isTransientAudioFocusLoss = it },
        )

    @VisibleForTesting
    internal val notificationId by lazy {
        SharedIdsHelper.getIdForTag(context, AbstractMediaSessionService.NOTIFICATION_TAG)
    }

    private var notificationState: SessionState? = null

    @VisibleForTesting internal val intentFilter = IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY)

    @VisibleForTesting internal var noisyAudioStreamReceiver: BecomingNoisyReceiver? = null

    @VisibleForTesting internal var isForegroundService: Boolean = false

    @VisibleForTesting internal var isTransientAudioFocusLoss: Boolean = false

    // The tab and audio-session type that audio focus was last granted (or not needed) for while
    // playing. Further updates for the same pair must not request again: after the system takes
    // focus away, a page can keep reporting that it is playing, and requesting again would take
    // focus back. The system returns focus after a transient loss on its own.
    private var focusRequestKey: Pair<String, MozMediaSession.AudioSessionType>? = null

    fun onCreate() {
        logger.debug("Service created")
    }

    fun onDestroy() {
        mediaSession.release()
        player.release()
        audioFocus.abandon()
        logger.debug("Service destroyed")
    }

    fun onStartCommand(intent: Intent?) {
        logger.debug("Command received: ${intent?.action}")

        when (intent?.action) {
            AbstractMediaSessionService.ACTION_PLAY -> {
                player.play()
                emitNotificationPlayFact()
            }
            AbstractMediaSessionService.ACTION_PAUSE -> {
                player.pause()
                emitNotificationPauseFact()
            }
            AbstractMediaSessionService.ACTION_NEXT_TRACK -> {
                player.seekToNext()
                emitNotificationNextFact()
            }
            AbstractMediaSessionService.ACTION_PREV_TRACK -> {
                player.seekToPrevious()
                emitNotificationPreviousFact()
            }
            else -> logger.debug("Can't process action: ${intent?.action}")
        }
    }

    fun onTaskRemoved() {
        // Only stop media for normal tabs; custom tabs are owned by external apps that
        // remain running after the user removes our task from recents.
        store.state.tabs.forEach {
            it.mediaSessionState?.controller?.stop()
        }

        shutdown()
    }

    override fun handleMediaPlaying(sessionState: SessionState) {
        emitStatePlayFact()

        registerBecomingNoisyListenerIfNeeded()

        if (isForegroundService) {
            // Audio focus must be requested only while a foreground service is running.
            // On Android 15+, requesting audio focus from the background without one
            // silently returns AUDIOFOCUS_REQUEST_FAILED.
            requestAudioFocusIfNeeded(sessionState)
            updateNotification(sessionState)
        } else {
            // startForeground() requests audio focus once the service is started, ensuring
            // the foreground service has WIU (While In Use) capabilities — granted when the
            // service is started while the app is visible — before the audio focus request.
            startForeground(sessionState)
        }
    }

    override fun handleMediaPaused(sessionState: SessionState) {
        emitStatePauseFact()

        resetFocusRequest()
        // Capture and clear the flag in a single pass. If the pause was triggered by a transient
        // audio focus loss (e.g. a notification sound), keep the foreground service alive so its
        // WIU (While In Use) capabilities are retained and audio focus can be reclaimed when
        // AUDIOFOCUS_GAIN is received. Clearing the flag on every call ensures a subsequent pause
        // (user-initiated or otherwise) always stops the foreground service, preventing it from
        // staying alive indefinitely.
        val wasTransientLoss = isTransientAudioFocusLoss
        isTransientAudioFocusLoss = false
        if (!wasTransientLoss) {
            unregisterBecomingNoisyListenerIfNeeded()
            stopForeground()
        }

        updateNotification(sessionState)
    }

    override fun handleMediaStopped(sessionState: SessionState) {
        emitStateStopFact()

        unregisterBecomingNoisyListenerIfNeeded()
        stopForeground()
        // Playback has ended permanently; release audio focus so other apps can acquire it.
        audioFocus.abandon()
        resetFocusRequest()

        updateNotification(sessionState)
    }

    override fun handleMediaUpdated(sessionState: SessionState) {
        updateNotification(sessionState)
    }

    override fun handleNoMedia() {
        shutdown()
    }

    @VisibleForTesting
    internal fun updateNotification(sessionState: SessionState) {
        notificationState = sessionState
        when (sessionState.mediaSessionState?.playbackState) {
            PLAYING,
            PAUSED -> {
                notificationsDelegate.notify(
                    notificationId = notificationId,
                    notification = createNotification(sessionState),
                )
            }
            else -> {
                notificationsDelegate.notificationManagerCompat.cancel(notificationId)
            }
        }
    }

    @VisibleForTesting
    @Suppress("TooGenericExceptionCaught")
    internal fun startForeground(sessionState: SessionState) {
        try {
            service.startForeground(notificationId, createNotification(sessionState))
        } catch (e: Exception) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && e is ForegroundServiceStartNotAllowedException) {
                // We should not encounter this exception if `android:foregroundServiceType="mediaPlayback"`
                // is added to the service. The crash reporter loses the stack trace for this
                // exception so we want to be able to track this crash independently to ensure
                // this case is fixed and be able to determine if there are other cases where we
                // might be trying to start foreground services from the background.
                // https://bugzilla.mozilla.org/show_bug.cgi?id=1802620
                crashReporter?.submitCaughtException(e)
                notificationState = null
                return
            } else {
                throw e
            }
        }

        isForegroundService = true
        notificationState = sessionState
        // Request audio focus only after the foreground service is running. This satisfies
        // the Android 15+ requirement that audio focus requests must come from an app that
        // is either visible or running a foreground service with WIU (While In Use)
        // capabilities, i.e. one that was started while the app was visible to the user.
        requestAudioFocusIfNeeded(sessionState)
    }

    private fun createNotification(sessionState: SessionState) =
        notificationHelper.create(sessionState, mediaSession, player.artworkFor(sessionState)?.bitmap)

    @VisibleForTesting
    internal fun onArtworkChanged() {
        notificationState?.let(::updateNotification)
    }

    @MainThread
    private fun requestAudioFocusIfNeeded(sessionState: SessionState): Boolean {
        val type = sessionState.mediaSessionState?.audioSessionType ?: MozMediaSession.AudioSessionType.AUTO
        val key = sessionState.id to type
        if (focusRequestKey == key) {
            logger.debug("Audio focus already requested for tab ${sessionState.id} with type $type, skipping")
            return true
        }
        val granted = audioFocus.request(sessionState.id, type)
        logger.debug("Audio focus requested for tab ${sessionState.id} with type $type, granted=$granted")
        focusRequestKey = if (granted) key else null
        return granted
    }

    // Play sent through the Android media session comes from the user, never from the page. After a
    // permanent focus loss the page can keep reporting that it is playing, so this is where focus is
    // requested again. Focus can only be requested while the service is in the foreground; before
    // that, play goes ahead and startForeground() requests it.
    @MainThread
    private fun onMediaSessionPlay(sessionState: SessionState): Boolean {
        focusRequestKey = null
        val shouldPlay = !isForegroundService || requestAudioFocusIfNeeded(sessionState)
        logger.debug(
            "Media session play for tab ${sessionState.id}, foreground=$isForegroundService, shouldPlay=$shouldPlay"
        )
        return shouldPlay
    }

    @MainThread
    private fun resetFocusRequest() {
        focusRequestKey = null
        logger.debug("Audio focus request reset")
    }

    @VisibleForTesting
    internal fun stopForeground() {
        service.stopForegroundCompat(false)
        isForegroundService = false
    }

    @VisibleForTesting
    internal fun registerBecomingNoisyListenerIfNeeded() {
        if (noisyAudioStreamReceiver != null) {
            return
        }

        noisyAudioStreamReceiver = BecomingNoisyReceiver { player.pause() }
        noisyAudioStreamReceiver?.let {
            registerBecomingNoisyListener(it)
        }
    }

    @VisibleForTesting
    internal fun registerBecomingNoisyListener(broadcastReceiver: BroadcastReceiver) {
        context.registerReceiverCompat(
            broadcastReceiver,
            intentFilter,
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )
    }

    @VisibleForTesting
    internal fun unregisterBecomingNoisyListenerIfNeeded() {
        noisyAudioStreamReceiver?.let {
            context.unregisterReceiver(noisyAudioStreamReceiver)
            noisyAudioStreamReceiver = null
        }
    }

    @VisibleForTesting
    internal fun shutdown() {
        // MediaSessionFeature can keep the service bound past stopSelf(), and a later playing state
        // must still reach the session and player; onDestroy() releases them.
        notificationState = null
        resetFocusRequest()
        // Explicitly cancel media notification.
        // Otherwise, when media is paused, with [STOP_FOREGROUND_DETACH] notification behavior,
        // the notification will persist even after service is stopped and destroyed.
        notificationsDelegate.notificationManagerCompat.cancel(notificationId)
        unregisterBecomingNoisyListenerIfNeeded()
        service.stopSelf()
    }

    @VisibleForTesting
    internal fun deviceBecomingNoisy(context: Context) {
        noisyAudioStreamReceiver?.deviceIsBecomingNoisy(context)
    }
}
