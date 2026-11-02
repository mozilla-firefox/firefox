/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import kotlin.time.Duration
import mozilla.components.feature.listentopage.ArticleProgress

/** A place in the article: which chunk holds it, and how far into that chunk it is. */
internal data class ChunkPosition(val chunkIndex: Int, val offsetMs: Long)

/**
 * Works out how far through the whole article playback has got. Chunks are made a few at a time, so most of their
 * lengths are estimates until they are measured.
 *
 * @param chunkCount How many chunks the article came to, or `0` before it has been split.
 * @param measuredDurationOf How long a chunk's audio lasted, or `null` while it has not been measured.
 * @param estimatedDurationMsOf How long a chunk that has not been measured is expected to last, or `null` when there is
 *   nothing to estimate it from.
 * @param charactersIn How many characters a chunk holds.
 * @param chunkDurationsBeforePlaylistMs The lengths of the chunks before the playlist, frozen when it started. They are
 *   mostly estimates, and letting them change would move every position after them.
 */
internal class ProgressMapper(
    private val chunkCount: Int,
    private val measuredDurationOf: (Int) -> Duration?,
    private val estimatedDurationMsOf: (Int) -> Long?,
    private val charactersIn: (Int) -> Int,
    private val chunkDurationsBeforePlaylistMs: List<Long> = emptyList(),
) {

    /**
     * The progress for a report at [positionMs]: the position, the estimated length, and where the bar is drawn. The
     * position is [positionMs], except that a small step back keeps the previous position, because the player corrects
     * its position slightly when audio starts.
     *
     * @param previous The last reported progress.
     * @param positionMs How far into the article the player reports it has got.
     * @param isArticleFinished Whether the last chunk of the article has finished, which is what lands the bar on
     *   exactly 1.0.
     */
    fun updatedProgress(previous: ArticleProgress, positionMs: Long, isArticleFinished: Boolean): ArticleProgress {
        val articleMs = articleMs()
        if (chunkCount == 0 || articleMs <= 0) return ArticleProgress()

        val shownMs =
            if (positionMs in (previous.positionMs - MAX_POSITION_CORRECTION_MS) until previous.positionMs) {
                previous.positionMs
            } else {
                positionMs
            }

        if (isArticleFinished) {
            val endMs = maxOf(articleMs, shownMs)
            return ArticleProgress(positionMs = endMs, durationMs = endMs, barPosition = 1f)
        }

        // Longer than the position until the article has been read to the end, so that a bar with audio left never
        // reads as full.
        val durationMs = maxOf(articleMs, shownMs + 1)

        return ArticleProgress(
            positionMs = shownMs,
            durationMs = durationMs,
            barPosition = barNextPosition(previous, shownMs, durationMs),
        )
    }

    /** How long each chunk lasts. Empty if nothing has been sized. */
    fun chunkDurationsMs(): List<Long> = (0 until chunkCount).map(::durationMsOf).takeIf { it.sum() > 0 }.orEmpty()

    /** Which chunk holds [characterOffset], counting the characters of the chunks rather than the page's own text. */
    fun chunkAt(characterOffset: Int): Int {
        var end = 0
        for (index in 0 until chunkCount) {
            end += charactersIn(index)
            if (characterOffset < end) return index
        }

        return (chunkCount - 1).coerceAtLeast(0)
    }

    /** Where chunk [index] starts, counted in the characters of the chunks before it. */
    fun characterOffsetOf(index: Int): Int = (0 until index.coerceIn(0, chunkCount)).sumOf(charactersIn)

    /** How far along the bar [characterOffset] is, between 0 and 1. */
    fun fractionAt(characterOffset: Int): Float {
        val articleMs = articleMs()
        if (articleMs <= 0) return 0f

        val index = chunkAt(characterOffset)
        val chars = charactersIn(index)
        val into = (characterOffset - characterOffsetOf(index)).coerceIn(0, chars)
        val offsetInChunkMs = if (chars == 0) 0 else durationMsOf(index) * into / chars

        return ((msBefore(index) + offsetInChunkMs).toFloat() / articleMs).coerceIn(0f, 1f)
    }

    fun positionAt(fraction: Float): ChunkPosition? {
        if (chunkCount == 0) return null

        val target = (fraction.coerceIn(0f, 1f) * articleMs()).toLong()
        var passed = 0L
        for (index in 0 until chunkCount) {
            val chunkMs = durationMsOf(index)
            if (target < passed + chunkMs || index == chunkCount - 1) {
                return ChunkPosition(index, (target - passed).coerceIn(0, chunkMs))
            }
            passed += chunkMs
        }

        return null
    }

    /**
     * How long chunk [index] lasts.
     *
     * A chunk that has been made but not measured is sized exactly like one that does not exist yet: the file is added
     * before it is read, so having audio does not mean having a duration.
     */
    private fun durationMsOf(index: Int): Long =
        chunkDurationsBeforePlaylistMs.getOrNull(index)
            ?: measuredDurationOf(index)?.inWholeMilliseconds
            ?: estimatedDurationMsOf(index)
            ?: 0

    private fun msBefore(index: Int): Long = (0 until index).sumOf(::durationMsOf)

    private fun articleMs(): Long = (0 until chunkCount).sumOf(::durationMsOf)
}

/**
 * Where [positionMs] into an article falls, given how long each of its chunks lasts.
 *
 * Taken over the lengths rather than over a [ProgressMapper] so that it can be answered from a snapshot.
 */
internal fun List<Long>.chunkPositionAt(positionMs: Long): ChunkPosition? {
    if (isEmpty()) return null

    val target = positionMs.coerceAtLeast(0)
    var passed = 0L
    for ((index, chunkMs) in withIndex()) {
        if (target < passed + chunkMs || index == lastIndex) {
            return ChunkPosition(index, (target - passed).coerceIn(0, chunkMs))
        }
        passed += chunkMs
    }

    return null
}

/** Where [position] falls in the article, given how long each of its chunks lasts. */
internal fun List<Long>.articlePositionOf(position: ChunkPosition): Long =
    take(position.chunkIndex).sum() + position.offsetMs

/** Where [position] falls in the article, given the chunk durations last reported. */
internal fun ArticleProgress.articlePositionOf(position: ChunkPosition): Long =
    chunkDurationsMs.articlePositionOf(position)

/**
 * [offsetMs] into a chunk estimated at [estimatedMs], now that it has been measured at [measuredMs]: kept where it was
 * asked for, so that the article stays at the time it was moved to, unless that is past the end of a chunk that turned
 * out shorter.
 */
internal fun offsetInMeasuredChunk(offsetMs: Long, measuredMs: Long?, estimatedMs: Long? = measuredMs): Long {
    if (measuredMs == null) return offsetMs

    val lastMs = (measuredMs - CHUNK_END_MARGIN_MS).coerceAtLeast(0)
    if (offsetMs <= lastMs) return offsetMs

    // Scales the offset when the chunk turned out shorter than its estimate, otherwise only the end margin applies.
    val scaledOffsetMs = if (estimatedMs != null && estimatedMs > 0) offsetMs * measuredMs / estimatedMs else offsetMs
    return scaledOffsetMs.coerceAtMost(lastMs)
}

/** The furthest back the player corrects its position when audio starts. A bigger step back is always a seek. */
private const val MAX_POSITION_CORRECTION_MS = 1_000L

/** How far before the end of a chunk a seek into it lands at the latest. */
internal const val CHUNK_END_MARGIN_MS = 500L
