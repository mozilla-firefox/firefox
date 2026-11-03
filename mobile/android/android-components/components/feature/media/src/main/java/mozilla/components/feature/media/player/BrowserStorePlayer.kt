/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.player

import android.content.Context
import android.graphics.Bitmap
import android.os.Looper
import androidx.annotation.VisibleForTesting
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.Player
import androidx.media3.common.SimpleBasePlayer
import androidx.media3.common.util.UnstableApi
import com.google.common.util.concurrent.Futures
import com.google.common.util.concurrent.ListenableFuture
import java.io.ByteArrayOutputStream
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.mediasession.MediaSession as MozMediaSession
import mozilla.components.feature.media.ext.findActiveMediaTab
import mozilla.components.feature.media.ext.getArtistOrUrl
import mozilla.components.feature.media.ext.getNonPrivateIcon
import mozilla.components.feature.media.ext.getTitleOrUrl
import mozilla.components.feature.media.ext.mediaDisplayKey
import mozilla.components.lib.state.ext.flowScoped

/**
 * Bridge between the Mozilla [BrowserStore] and Media3. Mirrors the playback state and metadata of the active media tab
 * onto a [SimpleBasePlayer], and routes Media3 play/pause commands back to that tab's [MozMediaSession.Controller].
 *
 * This player does not play audio itself. Actual playback happens in web content inside GeckoView; this class only
 * exposes that state to Android's MediaSession infrastructure (lockscreen, Bluetooth headsets, Android Auto, etc.).
 *
 * @param context Application-scoped [Context] used for resolving fallback strings (e.g. the "Private mode" label shown
 *   instead of metadata from private tabs).
 * @param store The [BrowserStore] whose active media tab drives player state.
 * @param mainDispatcher Dispatcher for store observation; must be a main-thread dispatcher.
 * @param encodingDispatcher Dispatcher used to off-load artwork PNG encoding off the main thread.
 * @param looper Looper for [SimpleBasePlayer]; must be the same thread backing [mainDispatcher].
 * @param onPlay Invoked with the active media tab when Media3 asks to play. The tab is told to play only if it returns
 *   true.
 * @param onArtworkChanged Invoked when a completed artwork fetch changes the artwork reported for the active media tab.
 */
@UnstableApi
internal class BrowserStorePlayer(
    private val context: Context,
    private val store: BrowserStore,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
    private val encodingDispatcher: CoroutineDispatcher = Dispatchers.Default,
    looper: Looper = Looper.getMainLooper(),
    private val onPlay: (SessionState) -> Boolean = { true },
    private val onArtworkChanged: () -> Unit = {},
) : SimpleBasePlayer(looper) {

    @VisibleForTesting internal var cachedArtwork: Artwork? = null

    private var artworkJob: Job? = null

    private var lastArtworkKey: Triple<String, MozMediaSession.Metadata?, Bitmap?>? = null

    // On a track change the page often keeps reporting the previous track's positionState for a
    // short while before pushing a fresh one. While that stale value persists we report position 0
    // instead of the outgoing track's position. A tab's first update is always let through, where a
    // non-zero start position is legitimate.
    private var lastTabId: String? = null
    private var lastTitle: String? = null
    private var stalePositionState: MozMediaSession.PositionState? = null

    private var positionKey: PositionKey? = null
    private var positionSupplier: PositionSupplier = PositionSupplier.ZERO

    // `scope` starts the store-observing coroutine as part of its initializer; the looper
    // check must run first. Keep this `init` block above the declaration of `scope`.
    init {
        check(Looper.myLooper() == looper) {
            "BrowserStorePlayer must be constructed on its looper's thread"
        }
    }

    @VisibleForTesting
    internal val scope: CoroutineScope =
        store.flowScoped(dispatcher = mainDispatcher) { flow ->
            flow
                .map { it.findActiveMediaTab() }
                .distinctUntilChangedBy { it.mediaDisplayKey(context) }
                .collect { tab ->
                    refreshArtwork(tab)
                    trackStalePosition(tab)
                    invalidateState()
                }
        }

    override fun getState(): State {
        val tab = store.state.findActiveMediaTab()
        val playbackState = tab?.mediaSessionState?.playbackState

        val media3PlaybackState =
            when (playbackState) {
                MozMediaSession.PlaybackState.PLAYING,
                MozMediaSession.PlaybackState.PAUSED -> Player.STATE_READY
                // STOPPED and UNKNOWN both map to IDLE rather than ENDED. Our surfaces
                // (lockscreen, Bluetooth, becoming-noisy) react to playWhenReady, and ENDED
                // would trigger Media3's automatic playlist-advance and replay behavior that
                // we don't support.
                else -> Player.STATE_IDLE
            }
        val playWhenReady = playbackState == MozMediaSession.PlaybackState.PLAYING
        val positionState = tab?.mediaSessionState?.positionState

        val builder =
            State.Builder()
                .setAvailableCommands(commandsFor(tab))
                .setPlaybackState(media3PlaybackState)
                .setPlayWhenReady(
                    playWhenReady,
                    Player.PLAY_WHEN_READY_CHANGE_REASON_USER_REQUEST,
                )

        if (tab != null) {
            val speed = positionState?.playbackRate?.toFloat()?.takeIf { it > 0f && it.isFinite() } ?: 1f
            builder
                .setPlaylist(listOf(buildMediaItemData(tab)))
                .setContentPositionMs(positionSupplierFor(tab, positionState, if (playWhenReady) speed else 0f))
                .setPlaybackParameters(PlaybackParameters(speed))
        } else {
            positionKey = null
        }
        return builder.build()
    }

    private fun positionSupplierFor(
        tab: SessionState,
        positionState: MozMediaSession.PositionState?,
        rate: Float,
    ): PositionSupplier {
        val positionMs =
            if (stalePositionState != null) {
                0L
            } else {
                ((positionState?.position?.takeIf { it >= 0 && it.isFinite() } ?: 0.0) * C.MILLIS_PER_SECOND).toLong()
            }
        val key = PositionKey(tab.id, tab.mediaSessionState?.metadata?.title, positionState, positionMs, rate)
        if (key != positionKey) {
            val start = if (positionKey?.copy(rate = rate) == key) positionSupplier.get() else positionMs
            positionKey = key
            positionSupplier = PositionSupplier.getExtrapolating(start, rate)
        }
        return positionSupplier
    }

    override fun handleSetPlayWhenReady(playWhenReady: Boolean): ListenableFuture<*> {
        val tab = store.state.findActiveMediaTab()
        val controller = tab?.mediaSessionState?.controller ?: return Futures.immediateVoidFuture()
        when {
            !playWhenReady -> controller.pause()
            onPlay(tab) -> controller.play()
        }
        return Futures.immediateVoidFuture()
    }

    override fun handleSeek(
        mediaItemIndex: Int,
        positionMs: Long,
        seekCommand: Int,
    ): ListenableFuture<*> {
        val controller =
            store.state.findActiveMediaTab()?.mediaSessionState?.controller ?: return Futures.immediateVoidFuture()
        when (seekCommand) {
            Player.COMMAND_SEEK_TO_NEXT -> controller.nextTrack()
            Player.COMMAND_SEEK_TO_PREVIOUS -> controller.previousTrack()
            Player.COMMAND_SEEK_IN_CURRENT_MEDIA_ITEM ->
                if (positionMs != C.TIME_UNSET) {
                    controller.seekTo(positionMs / C.MILLIS_PER_SECOND.toDouble(), fast = false)
                    positionSupplier = PositionSupplier.getExtrapolating(positionMs, positionKey?.rate ?: 0f)
                }
            else -> Unit
        }
        return Futures.immediateVoidFuture()
    }

    override fun handleRelease(): ListenableFuture<*> {
        scope.cancel()
        return Futures.immediateVoidFuture()
    }

    // Skip commands are advertised only when web content declares the matching
    // MediaSession feature, so the system surfaces next/previous exactly when the
    // page can act on them.
    private fun commandsFor(tab: SessionState?): Player.Commands {
        val features = tab?.mediaSessionState?.features
        return Player.Commands.Builder()
            .addAll(
                Player.COMMAND_PLAY_PAUSE,
                Player.COMMAND_RELEASE,
                Player.COMMAND_GET_CURRENT_MEDIA_ITEM,
                Player.COMMAND_GET_METADATA,
                Player.COMMAND_GET_TIMELINE,
            )
            .addIf(
                Player.COMMAND_SEEK_TO_NEXT,
                features?.contains(MozMediaSession.Feature.NEXT_TRACK) == true,
            )
            .addIf(
                Player.COMMAND_SEEK_TO_PREVIOUS,
                features?.contains(MozMediaSession.Feature.PREVIOUS_TRACK) == true,
            )
            .addIf(Player.COMMAND_SEEK_IN_CURRENT_MEDIA_ITEM, tab?.mediaSessionState?.isSeekable() == true)
            .build()
    }

    private fun trackStalePosition(tab: SessionState?) {
        val mss = tab?.mediaSessionState ?: return
        val newTitle = mss.metadata?.title
        if (tab.id != lastTabId) {
            stalePositionState = null
        } else if (newTitle != lastTitle) {
            stalePositionState = mss.positionState
        } else if (mss.positionState != stalePositionState) {
            stalePositionState = null
        }
        lastTabId = tab.id
        lastTitle = newTitle
    }

    /** Returns the artwork fetched for [tab] and its current metadata, or null if none has arrived. */
    fun artworkFor(tab: SessionState): Artwork? = cachedArtwork?.takeIf {
        it.tabId == tab.id && it.metadata == tab.mediaSessionState?.metadata
    }

    private fun buildMediaItemData(tab: SessionState): MediaItemData {
        val meta = tab.mediaSessionState?.metadata
        val builder =
            MediaMetadata.Builder()
                .setTitle(tab.getTitleOrUrl(context, meta?.title))
                .setArtist(tab.getArtistOrUrl(meta?.artist))
        artworkFor(tab)?.let { builder.setArtworkData(it.png, MediaMetadata.PICTURE_TYPE_FRONT_COVER) }

        val mss = tab.mediaSessionState
        return MediaItemData.Builder(tab.id)
            .setMediaItem(MediaItem.Builder().setMediaId(tab.id).build())
            .setMediaMetadata(builder.build())
            .setDurationUs(mss?.durationSeconds()?.let { (it * C.MICROS_PER_SECOND).toLong() } ?: C.TIME_UNSET)
            .setIsSeekable(mss?.isSeekable() == true)
            .build()
    }

    private fun refreshArtwork(tab: SessionState?) {
        if (tab == null) {
            artworkJob?.cancel()
            artworkJob = null
            cachedArtwork = null
            lastArtworkKey = null
            return
        }
        val metadata = tab.mediaSessionState?.metadata
        val key = Triple(tab.id, metadata, tab.content.icon)
        if (key == lastArtworkKey) return
        lastArtworkKey = key
        artworkJob?.cancel()
        if (artworkFor(tab)?.fromIcon == false) return
        artworkJob = scope.launch {
            val artwork = runCatching {
                tab.getNonPrivateIcon(metadata?.getArtwork)?.let { bitmap ->
                    val png =
                        withContext(encodingDispatcher) {
                            ByteArrayOutputStream()
                                .also { out ->
                                    bitmap.compress(Bitmap.CompressFormat.PNG, BITMAP_COMPRESSION_QUALITY, out)
                                }
                                .toByteArray()
                        }
                    Artwork(tab.id, metadata, bitmap, png, fromIcon = bitmap === tab.content.icon)
                }
            }
                .onFailure { if (it is CancellationException || it !is Exception) throw it }
                .getOrNull()
            if (store.state.findActiveMediaTab()?.id != tab.id) return@launch
            val unchanged = artworkFor(tab)?.png contentEquals artwork?.png
            cachedArtwork = artwork
            if (unchanged) return@launch
            invalidateState()
            onArtworkChanged()
        }
    }

    internal class Artwork(
        val tabId: String,
        val metadata: MozMediaSession.Metadata?,
        val bitmap: Bitmap,
        val png: ByteArray,
        val fromIcon: Boolean,
    )
}

private data class PositionKey(
    val tabId: String,
    val title: String?,
    val positionState: MozMediaSession.PositionState?,
    val positionMs: Long,
    val rate: Float,
)

private fun MediaSessionState.durationSeconds(): Double? =
    positionState.duration.takeIf { it > 0 && it.isFinite() }
        ?: elementMetadata?.duration?.takeIf { it > 0 && it.isFinite() }

private fun MediaSessionState.isSeekable(): Boolean =
    durationSeconds() != null || features.contains(MozMediaSession.Feature.SEEK_TO)

// Ignored by PNG compression (lossless); required by the Bitmap.compress signature.
private const val BITMAP_COMPRESSION_QUALITY = 100
