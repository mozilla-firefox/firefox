/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage

import mozilla.components.feature.listentopage.playback.PlaybackPhase

/** A chunk that does not exist, for a playlist with nothing queued or playing. */
internal const val NO_CHUNK = -1

// Reports this far behind a restart still count, because the player nudges its position back when audio starts.
private const val RESTART_REPORT_TOLERANCE_MS = 1_000L

// Reports this far behind a seek still count, for the same reason.
private const val SEEK_REPORT_BEFORE_MS = 1_000L

// Reports this far past a seek still count, because playback moves on between reports.
private const val SEEK_REPORT_AFTER_MS = 5_000L

// The player can send a report or two from where it was before a seek reaches it, so up to this many reports far from
// the seek's target are ignored. After that the seek is given up, so a seek the player never makes cannot freeze the
// article.
private const val MAX_REPORTS_BEFORE_SEEK = 2

/**
 * A record of which chunks have been handed to the player since its playlist last started, and of the seek or restart
 * its reports have to catch up with.
 *
 * @property lastQueuedChunkIndex The index of the last chunk handed to the player, or [NO_CHUNK] when none has been
 *   since the playlist was last cleared.
 * @property playingChunkIndex The index of the chunk the player last reported reading out, or [NO_CHUNK].
 * @property chunkDurationsBeforePlaylistMs The durations of the chunks before the playlist, kept as they were when it
 *   started. Most of them were skipped, so they are estimates, and the session placed the playlist by them.
 * @property seekingToChunkIndex The index of the chunk a seek is moving playback into, until the player reports from
 *   there, or [NO_CHUNK].
 * @property lastReadReport The last report the player sent, or `null` when it was left over from before the last seek
 *   or restart.
 * @property isFinished Whether a seek moved playback to the end of the article, until playback moves again.
 */
@ConsistentCopyVisibility
data class Playlist
internal constructor(
    internal val lastQueuedChunkIndex: Int = NO_CHUNK,
    internal val playingChunkIndex: Int = NO_CHUNK,
    internal val chunkDurationsBeforePlaylistMs: List<Long> = emptyList(),
    internal val seekingToChunkIndex: Int = NO_CHUNK,
    internal val lastReadReport: PlaybackState? = null,
    private val restartedAtMs: Long? = null,
    private val seekingToMs: Long? = null,
    private val reportsBeforeSeek: Int = 0,
    internal val isFinished: Boolean = false,
) {
    /** The next chunk to hand to the player. */
    internal val nextToQueue: Int
        get() = lastQueuedChunkIndex + 1

    /**
     * What was handed to the player no longer counts, because the session is replacing it. A seek in progress is kept,
     * since it is what the new playlist is being made for.
     */
    internal fun cleared() = copy(playingChunkIndex = NO_CHUNK, lastQueuedChunkIndex = NO_CHUNK, restartedAtMs = null)

    /** Playback is being moved to [positionMs] into the article, which is inside the chunk at [chunkIndex]. */
    internal fun seeking(positionMs: Long, chunkIndex: Int) =
        copy(seekingToMs = positionMs, seekingToChunkIndex = chunkIndex, reportsBeforeSeek = 0, isFinished = false)

    /** A seek moved playback to the end of the article, which finished it there. */
    internal fun finished() = copy(isFinished = true, lastReadReport = null)

    /** The seek was given up, so what the player reports is read again whatever its position. */
    internal fun seekAbandoned() = copy(seekingToMs = null, seekingToChunkIndex = NO_CHUNK)

    /** The chunk at [chunkIndex] was handed to the player behind what was queued. */
    internal fun queued(chunkIndex: Int) = copy(lastQueuedChunkIndex = chunkIndex)

    /** The player reported reading the chunk at [chunkIndex] out. */
    internal fun playing(chunkIndex: Int) = copy(playingChunkIndex = chunkIndex)

    /**
     * The player was given a new playlist that starts on the chunk at [chunkIndex], [startMs] into the article. A seek
     * waits on [startMs] from now, since the made chunk can be shorter or longer than estimated, which moves the start.
     *
     * @param chunkDurationsBeforePlaylistMs The durations of the chunks before the chunk at [chunkIndex] that the
     *   session placed the playlist.
     */
    internal fun restarted(chunkIndex: Int, chunkDurationsBeforePlaylistMs: List<Long>, startMs: Long) =
        copy(
            chunkDurationsBeforePlaylistMs = chunkDurationsBeforePlaylistMs,
            playingChunkIndex = chunkIndex,
            lastQueuedChunkIndex = chunkIndex,
            restartedAtMs = startMs,
            seekingToMs = seekingToMs?.let { startMs },
            reportsBeforeSeek = 0,
            isFinished = false,
        )

    /**
     * The playlist after the player sent [report], keeping it as [lastReadReport], or `null` when it is left over from
     * before the last seek or restart. A failure is always kept, since it is the chunk sought or restarted on that
     * failed.
     */
    internal fun received(report: PlaybackState): Playlist {
        if (isFinished) return copy(lastReadReport = null)

        val isFailure = report.phase == PlaybackPhase.Failed

        // A report far from where a seek went was taken before the move. A few are ignored, then the seek is given up.
        val isBeforeSeek = seekingToMs != null && !report.isFromSeekTo(seekingToMs)
        if (isBeforeSeek && !isFailure && reportsBeforeSeek < MAX_REPORTS_BEFORE_SEEK) {
            return copy(reportsBeforeSeek = reportsBeforeSeek + 1, lastReadReport = null)
        }
        val afterSeek = seekAbandoned()

        // A report that is not from a restarted playlist is left over from the playlist it replaced.
        val isLeftOver =
            afterSeek.restartedAtMs != null &&
                !report.isFromRestartAt(afterSeek.restartedAtMs, afterSeek.playingChunkIndex)
        if (isLeftOver && !isFailure) {
            return afterSeek.copy(lastReadReport = null)
        }
        return afterSeek.copy(restartedAtMs = null, lastReadReport = report)
    }
}

/** Whether this report is close enough to where a seek went, at [targetMs], to come from after it. */
private fun PlaybackState.isFromSeekTo(targetMs: Long): Boolean =
    positionMs in (targetMs - SEEK_REPORT_BEFORE_MS)..(targetMs + SEEK_REPORT_AFTER_MS)

/** Whether this report comes from the playlist restarted at [startMs] into the article, on the chunk at [chunkIndex] */
private fun PlaybackState.isFromRestartAt(startMs: Long, chunkIndex: Int): Boolean {
    val isAtRestart = positionMs >= startMs - RESTART_REPORT_TOLERANCE_MS
    val hasEndedRestartedChunk = phase == PlaybackPhase.Ended && chunk.index == chunkIndex
    return isAtRestart && (phase == PlaybackPhase.Playing || phase == PlaybackPhase.Paused || hasEndedRestartedChunk)
}
