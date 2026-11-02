/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import androidx.annotation.VisibleForTesting
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.mediasession.MediaSession.PlaybackState.PAUSED
import mozilla.components.concept.engine.mediasession.MediaSession.PlaybackState.PLAYING
import mozilla.components.feature.media.ext.findActiveMediaTab
import mozilla.components.feature.media.ext.mediaDisplayKey
import mozilla.components.feature.media.service.MediaServiceBinder
import mozilla.components.feature.media.service.MediaSessionDelegate
import mozilla.components.lib.state.ext.flowScoped

/**
 * Feature implementation that handles MediaSession state changes and controls showing a notification reflecting the
 * media states.
 *
 * @param applicationContext the application's [Context].
 * @param mediaServiceClass the media service class will handle the media playback state
 * @param store Reference to the browser store where tab state is located.
 * @param mainDispatcher Dispatcher used for main thread operations.
 */
class MediaSessionFeature(
    val applicationContext: Context,
    val mediaServiceClass: Class<*>,
    val store: BrowserStore,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) {
    @VisibleForTesting internal var scope: CoroutineScope? = null

    @VisibleForTesting internal var mediaService: MediaSessionDelegate? = null

    @VisibleForTesting
    internal val mediaServiceConnection =
        object : ServiceConnection {
            override fun onServiceConnected(className: ComponentName, binder: IBinder) {
                val serviceBinder = binder as MediaServiceBinder
                mediaService = serviceBinder.getMediaService()
                // The service is bound when media starts playing. Ensure in-progress media status is correctly shown.
                store.state.findActiveMediaTab()?.let {
                    showMediaStatus(it)
                }
            }

            override fun onServiceDisconnected(className: ComponentName?) {
                mediaService = null
            }
        }

    /** Starts the feature. */
    fun start() {
        scope =
            store.flowScoped(dispatcher = mainDispatcher) { flow ->
                var last: SessionState? = null
                flow
                    .map { state -> state.findActiveMediaTab() }
                    .distinctUntilChangedBy { tab -> tab.mediaDisplayKey(applicationContext) }
                    .collect { tab ->
                        if (tab != null && tab.mediaSessionState == last?.mediaSessionState) {
                            mediaService?.handleMediaUpdated(tab)
                        } else {
                            showMediaStatus(tab)
                        }
                        last = tab
                    }
            }
    }

    /** Stops the feature. */
    fun stop() {
        scope?.cancel()
        scope = null
    }

    @VisibleForTesting
    internal fun showMediaStatus(sessionState: SessionState?) {
        if (sessionState == null) {
            mediaService?.let {
                it.handleNoMedia()
                applicationContext.unbindService(mediaServiceConnection)
                mediaService = null
            }

            return
        }

        when (sessionState.mediaSessionState?.playbackState) {
            PLAYING -> {
                if (mediaService == null) {
                    applicationContext.bindService(
                        Intent(applicationContext, mediaServiceClass),
                        mediaServiceConnection,
                        Context.BIND_AUTO_CREATE,
                    )
                }

                mediaService?.handleMediaPlaying(sessionState)
            }
            PAUSED -> mediaService?.handleMediaPaused(sessionState)
            else -> mediaService?.handleMediaStopped(sessionState)
        }
    }
}
