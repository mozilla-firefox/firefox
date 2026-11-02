/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import kotlin.math.abs
import kotlin.math.roundToLong
import kotlin.time.Duration
import kotlin.time.DurationUnit
import mozilla.components.feature.listentopage.ArticleProgress
import mozilla.components.feature.listentopage.synthesis.SpeechRate

/**
 * What the article's length is estimated from, filled in as synthesis finds it out.
 *
 * @property chunkCharacters How many characters each chunk holds, in reading order, or empty until the article is
 *   split.
 * @property measuredDurations How long each chunk that has been made lasts, by chunk.
 * @property speechRate What the opening measured at, or `null` when it could not be measured.
 */
@ConsistentCopyVisibility
data class ArticleMeasurements
internal constructor(
    internal val chunkCharacters: List<Int> = emptyList(),
    internal val measuredDurations: Map<Int, Duration> = emptyMap(),
    internal val speechRate: SpeechRate? = null,
) {
    /** How many chunks the article came to, or `0` before it has been split. */
    internal val chunkCount: Int = chunkCharacters.size

    /** Whether chunk at [index] is the last one of the article. */
    internal fun isLastChunk(index: Int): Boolean = index == chunkCount - 1

    // Where each chunk starts in the article's text, in characters, with the end of the text last.
    private val chunkBoundaries = chunkCharacters.runningFold(0, Int::plus)

    private val totalCharacters: Int = chunkBoundaries.last()

    // Computed once per instance and replaced on a split or chunk measurement.
    private val estimatedDurationsMs: List<Long?> by lazy {
        (0 until chunkCount).map { index ->
            calculateChunkReadingSpeedFromNearbyChunks(index)?.let {
                (chunkCharacters[index] * it * MS_PER_SECOND).toLong()
            }
        }
    }

    /**
     * How far through the article's text [positionMs] is, between 0 and 1, or `null` before the article is split.
     *
     * @param chunkDurationsMs How long each chunk lasts, which [positionMs] is placed by.
     */
    internal fun getTextPositionAfterVoiceChanged(positionMs: Long, chunkDurationsMs: List<Long>): Float? {
        val position = chunkDurationsMs.chunkPositionAt(positionMs)
        if (totalCharacters <= 0 || position == null || position.chunkIndex >= chunkCount) return null

        val chunkMs = chunkDurationsMs[position.chunkIndex]
        val fractionOfChunkRead = if (chunkMs > 0) position.offsetMs.toFloat() / chunkMs else 0f
        val characters =
            chunkBoundaries[position.chunkIndex] + (fractionOfChunkRead * chunkCharacters[position.chunkIndex])
        return (characters / totalCharacters).coerceIn(0f, 1f)
    }

    /**
     * The index of the chunk that holds [textPosition], between 0 and 1 of the article's characters, or `null` before
     * the article is split.
     */
    internal fun chunkIndexAtTextPosition(textPosition: Float): Int? {
        if (totalCharacters <= 0) return null

        val targetCharacters = textPosition.coerceIn(0f, 1f) * totalCharacters
        return chunkBoundaries.indexOfLast { it <= targetCharacters }.coerceAtMost(chunkCount - 1)
    }

    /**
     * Where [textPosition], between 0 and 1 of the article's characters, falls: the chunk that holds it, and the same
     * fraction of that chunk's length as of its characters.
     */
    private fun getChunkPositionFromTextPosition(textPosition: Float, chunkDurationsMs: List<Long>): ChunkPosition? {
        val chunkIndex = chunkIndexAtTextPosition(textPosition) ?: return null
        if (chunkDurationsMs.size != chunkCount) return null

        val charactersIntoChunk = textPosition.coerceIn(0f, 1f) * totalCharacters - chunkBoundaries[chunkIndex]
        val characters = chunkCharacters[chunkIndex]
        val fractionOfChunkRead = if (characters > 0) (charactersIntoChunk / characters).coerceIn(0f, 1f) else 0f
        return ChunkPosition(chunkIndex, (fractionOfChunkRead * chunkDurationsMs[chunkIndex]).roundToLong())
    }

    /**
     * The progress at [textPosition], between 0 and 1 of the article's characters, by the lengths these measurements
     * give its chunks. It is placed in the chunk that holds that text, so that it is where playback restarts.
     */
    internal fun getArticleProgressAtTextPosition(textPosition: Float): ArticleProgress {
        val chunkDurationsMs = mapper().chunkDurationsMs()
        val durationMs = chunkDurationsMs.sum()
        val positionMs =
            getChunkPositionFromTextPosition(textPosition, chunkDurationsMs)?.let(chunkDurationsMs::articlePositionOf)
                ?: (durationMs * textPosition).roundToLong()
        return ArticleProgress(
            positionMs = positionMs,
            durationMs = durationMs,
            chunkDurationsMs = chunkDurationsMs,
            barPosition = if (durationMs > 0) positionMs.toFloat() / durationMs else 0f,
        )
    }

    /** @param chunkDurationsBeforePlaylistMs See [ProgressMapper]. */
    internal fun mapper(chunkDurationsBeforePlaylistMs: List<Long> = emptyList()) =
        ProgressMapper(
            chunkCount = chunkCount,
            measuredDurationOf = measuredDurations::get,
            estimatedDurationMsOf = { estimatedDurationsMs[it] },
            charactersIn = chunkCharacters::get,
            chunkDurationsBeforePlaylistMs = chunkDurationsBeforePlaylistMs,
        )

    /**
     * How long one character takes to read near chunk [index], from the [NEAREST_MEASURED_CHUNKS] measured chunks on
     * each side, weighted by size and closeness. Parts of a page can be read at different speeds (such as the body, a
     * list or the references) and neighbouring chunks are usually the same part. Falls back to the opening's speed, or
     * `null`, when nothing is measured.
     */
    private fun calculateChunkReadingSpeedFromNearbyChunks(index: Int): Double? {
        var weighted = 0.0
        var weights = 0.0
        // Looks both ways from the chunk, so a chunk past the measured ones still takes the speed of the nearest.
        for (side in listOf(index - 1 downTo 0, index + 1 until chunkCount)) {
            side
                .asSequence()
                .mapNotNull { other -> measuredDurations[other]?.let { other to it } }
                .filter { (other, _) -> chunkCharacters[other] > 0 }
                .take(NEAREST_MEASURED_CHUNKS)
                .forEach { (other, measured) ->
                    // A bigger chunk is a more reliable measurement, and a nearer one more likely the same part of the
                    // page, so each counts by its size divided by how far away it is.
                    val size = chunkCharacters[other]
                    val weight = size.toDouble() / abs(index - other)
                    weighted += weight * measured.toDouble(DurationUnit.SECONDS) / size
                    weights += weight
                }
        }

        // The weighted average of the seconds per character, or the opening's speed while nothing is measured.
        return if (weights > 0) weighted / weights else speechRate?.let { 1 / it.charsPerSecond }
    }
}

/** How many measured chunks on each side an unmeasured chunk's speed is estimated from. */
private const val NEAREST_MEASURED_CHUNKS = 2

private const val MS_PER_SECOND = 1_000
