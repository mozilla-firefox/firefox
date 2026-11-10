/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import android.os.Handler
import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.ForwardingSimpleBasePlayer
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.common.Timeline
import androidx.media3.common.util.UnstableApi
import androidx.media3.common.util.Util
import androidx.media3.exoplayer.source.ForwardingTimeline
import com.google.common.util.concurrent.Futures
import com.google.common.util.concurrent.ListenableFuture

/** How long a seek is shown as about to be made before it is given up on. */
internal const val PENDING_SEEK_TIMEOUT_MS = 1_000L

/**
 * Presents the article's times to the media session, where the player underneath only knows the chunk it is reading.
 *
 * @param onArticleSeek Called with the article position a seek, which only the session's owner can turn into a chunk.
 */
@OptIn(UnstableApi::class)
internal class SessionPlayer(
    player: Player,
    private val onArticleSeek: (positionMs: Long) -> Unit,
) : ForwardingSimpleBasePlayer(player) {

    private var pendingSeekMs: Long? = null
    private val handler = Handler(player.applicationLooper)
    private val forgetPendingSeek = Runnable { showPendingSeek(null) }
    private var hasNewPlaylistSinceBuffering = false
    private var finishedAtMs: Long? = null
    private var isPlayPendingAfterFinish = false

    /** Where the playlist sits in the article, how long the article lasts, and where a seek buffers. */
    var articleTimeline = ArticleTimeline()
        private set(value) {
            field = value
            invalidateState()
        }

    /** A seek's chunk is being made: the player stops, and the position is held at [positionMs] until it plays. */
    fun bufferAt(positionMs: Long) {
        // The buffering shows the seek from here on.
        showPendingSeek(null)
        releaseArticleEnd()

        // A stopped player keeps whether it should play, and makes no sound until it is prepared again.
        if (articleTimeline.bufferingAtMs == null) {
            hasNewPlaylistSinceBuffering = false
            player.stop()
        }
        playIfPending()

        articleTimeline = articleTimeline.copy(bufferingAtMs = positionMs)
    }

    /**
     * A seek finished the article: the player pauses, and the position is held at [positionMs], its end, until playback
     * moves again. A play while held starts the article again from the top.
     */
    fun finishAt(positionMs: Long) {
        showPendingSeek(null)
        isPlayPendingAfterFinish = false
        player.pause()
        finishedAtMs = positionMs
        invalidateState()
    }

    /** Stops holding the end of a finished article. */
    private fun releaseArticleEnd() {
        if (finishedAtMs != null) {
            finishedAtMs = null
            invalidateState()
        }
    }

    /** Plays, if a play was asked for while the end of a finished article was held. */
    private fun playIfPending() {
        if (isPlayPendingAfterFinish) {
            isPlayPendingAfterFinish = false
            player.play()
        }
    }

    /** The article is now estimated to last [durationMs]. */
    fun setArticleDuration(durationMs: Long) {
        articleTimeline = articleTimeline.copy(durationMs = durationMs)
    }

    /**
     * The player's state in article time, as the session and its controllers see it. Media3 calls this whenever the
     * state is read, and again after [invalidateState].
     */
    override fun getState(): State {
        val state = super.getState()
        val articleState = state.buildUpon()

        // A player that failed reports so, which is only allowed while it is idle.
        if (articleTimeline.bufferingAtMs != null && state.playerError == null && !state.timeline.isEmpty) {
            articleState.setPlaybackState(STATE_BUFFERING)
        }

        // Play requested while at the end of the article, which restarts the article and should show as playing
        if (isPlayPendingAfterFinish) {
            articleState.setPlayWhenReady(true, PLAY_WHEN_READY_CHANGE_REASON_USER_REQUEST)
        }

        // Held at the target while a seek buffers or waits to be made, or at the end once a seek finished the article,
        // because the player still reports where it was. At all other times it is the live position in the current
        // item, moved on by where that item starts.
        val shownMs = articleTimeline.bufferingAtMs ?: finishedAtMs ?: pendingSeekMs
        if (shownMs != null) {
            val shown = PositionSupplier.getConstant(shownMs)
            articleState.setContentPositionMs(shown).setContentBufferedPositionMs(shown)
        } else {
            val startMs = articleTimeline.chunkStartMs(player)
            articleState
                .setContentPositionMs { startMs + state.contentPositionMsSupplier.get() }
                .setContentBufferedPositionMs { startMs + state.contentBufferedPositionMsSupplier.get() }
        }

        if (state.hasPositionDiscontinuity) {
            articleState.setPositionDiscontinuity(
                state.positionDiscontinuityReason,
                articleTimeline.chunkStartMs(player) + state.discontinuityPositionMs,
            )
        }

        return articleState
            .setPlaylist(state.timeline.lastingArticle(), state.currentTracks, state.currentMetadata)
            .build()
    }

    /** [this] with every item lasting as long as the whole article, which is the length the session shows. */
    private fun Timeline.lastingArticle(): Timeline {
        val articleUs = articleTimeline.durationMs.takeIf { it > 0 }?.let(Util::msToUs) ?: C.TIME_UNSET
        return object : ForwardingTimeline(this) {
            override fun getWindow(windowIndex: Int, window: Window, defaultPositionProjectionUs: Long): Window =
                super.getWindow(windowIndex, window, defaultPositionProjectionUs).apply { durationUs = articleUs }
        }
    }

    /**
     * Shows [positionMs] as the position until playback is moved there, or stops showing one when `null`. Given up
     * after a while, in case the session's owner had no way to make the seek.
     */
    private fun showPendingSeek(positionMs: Long?) {
        if (positionMs == pendingSeekMs) return
        pendingSeekMs = positionMs

        handler.removeCallbacks(forgetPendingSeek)
        if (positionMs != null) handler.postDelayed(forgetPendingSeek, PENDING_SEEK_TIMEOUT_MS)

        invalidateState()
    }

    override fun handleSeek(mediaItemIndex: Int, positionMs: Long, seekCommand: Int): ListenableFuture<*> =
        when (seekCommand) {
            COMMAND_SEEK_IN_CURRENT_MEDIA_ITEM,
            COMMAND_SEEK_BACK,
            COMMAND_SEEK_FORWARD -> {
                onArticleSeek(positionMs)
                showPendingSeek(positionMs)
                Futures.immediateVoidFuture()
            }

            else -> {
                showPendingSeek(null)
                releaseArticleEnd()
                val chunkMs =
                    if (positionMs == C.TIME_UNSET) {
                        positionMs
                    } else {
                        articleTimeline.toChunkMs(player, mediaItemIndex, positionMs).coerceAtLeast(0)
                    }
                super.handleSeek(mediaItemIndex, chunkMs, seekCommand).also { playIfPending() }
            }
        }

    // The player still holds the audio from before the seek that finished the article, so it is not played on.
    override fun handleSetPlayWhenReady(playWhenReady: Boolean): ListenableFuture<*> {
        if (!playWhenReady) isPlayPendingAfterFinish = false
        if (!playWhenReady || finishedAtMs == null) return super.handleSetPlayWhenReady(playWhenReady)

        isPlayPendingAfterFinish = true
        onArticleSeek(0)
        return Futures.immediateVoidFuture()
    }

    // The session hands a controller's single item to the player as a list, so this is the one it calls.
    override fun handleSetMediaItems(
        mediaItems: List<MediaItem>,
        startIndex: Int,
        startPositionMs: Long,
    ): ListenableFuture<*> {
        if (articleTimeline.bufferingAtMs != null) hasNewPlaylistSinceBuffering = true
        releaseArticleEnd()
        startPlaylist(mediaItems.firstOrNull())
        return super.handleSetMediaItems(
                mediaItems,
                startIndex,
                chunkStartPositionMs(mediaItems, startIndex, startPositionMs),
            )
            .also { playIfPending() }
    }

    // Forgotten with the playlist, since the session outlives the article: the next one would otherwise be shown at the
    // place of this one's playlist to a controller that connects before its own playlist is set.
    override fun handleRemoveMediaItems(fromIndex: Int, toIndex: Int): ListenableFuture<*> {
        val removesAll = fromIndex == 0 && toIndex >= player.mediaItemCount
        val removed = super.handleRemoveMediaItems(fromIndex, toIndex)
        if (removesAll) {
            showPendingSeek(null)
            releaseArticleEnd()
            isPlayPendingAfterFinish = false
            articleTimeline = ArticleTimeline()
        }

        return removed
    }

    /** Places a new playlist in the article, as its first item says, before its start position is converted. */
    private fun startPlaylist(first: MediaItem?) {
        showPendingSeek(null)
        val started = first?.playlistTimeline() ?: return
        articleTimeline =
            articleTimeline.copy(playlistStartMs = started.playlistStartMs, durationMs = started.durationMs)
    }

    /** [startPositionMs] into the article, as a position into item [startIndex] of the new playlist [mediaItems]. */
    private fun chunkStartPositionMs(mediaItems: List<MediaItem>, startIndex: Int, startPositionMs: Long): Long {
        if (startPositionMs == C.TIME_UNSET) return startPositionMs

        val beforeMs = mediaItems.take(startIndex).sumOf { it.measuredDurationMs() ?: 0 }
        return startPositionMs - articleTimeline.playlistStartMs - beforeMs
    }

    /**
     * Prepares the player, and stops buffering for a seek once the seek's new playlist has been set. Preparing before
     * that, such as the one resume sends, is ignored, since it would start the playlist the seek left behind.
     */
    override fun handlePrepare(): ListenableFuture<*> {
        if (articleTimeline.bufferingAtMs != null && !hasNewPlaylistSinceBuffering) return Futures.immediateVoidFuture()

        val prepared = super.handlePrepare()
        stopBuffering()

        return prepared
    }

    /** The seek's new playlist is prepared, so the player's own position and state are shown again. */
    private fun stopBuffering() {
        if (articleTimeline.bufferingAtMs != null) {
            articleTimeline = articleTimeline.copy(bufferingAtMs = null)
        }
    }
}
