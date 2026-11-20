/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import kotlin.math.roundToLong
import mozilla.components.feature.listentopage.ArticleProgress

/**
 * How many position samples in a row can be late, for example while the main thread is busy, before a jump counts as a
 * seek.
 */
private const val MAX_MISSED_SAMPLES = 5

/**
 * The furthest playback can move between two reports and still count as normal playback rather than a seek. It is how
 * far the fastest speed reads in [MAX_MISSED_SAMPLES] samples (5 s), well under the shortest skip forward (30 s).
 */
private val MAX_NORMAL_PLAYBACK_STEP_MS =
    (POSITION_SAMPLE_INTERVAL_MS * PlaybackSpeed.entries.maxOf { it.multiplier } * MAX_MISSED_SAMPLES).toLong()

/**
 * While the bar catches up with a changed estimate, it moves at 1 plus or minus this times the playback speed: faster
 * when it is behind, slower when it is ahead. A choice of how the bar looks: it catches up within seconds without
 * jumping or stopping.
 */
private const val BAR_PLAYBACK_SPEED_CHANGE = 0.5f

/**
 * Where the bar is drawn (0 to 1) now that playback is at [positionMs] of [durationMs], moving smoothly from [previous]
 * when the estimated length changes instead of jumping. A seek moves the bar to where playback now is against the
 * estimated length, so the bar and the length are back in step after it.
 */
internal fun barNextPosition(previous: ArticleProgress, positionMs: Long, durationMs: Long): Float {
    if (durationMs <= 0) return 0f

    val updatedPosition = (positionMs.toFloat() / durationMs).coerceIn(0f, 1f)
    val isNormalPlayback = positionMs in previous.positionMs..(previous.positionMs + MAX_NORMAL_PLAYBACK_STEP_MS)

    return when {
        previous.durationMs <= 0 -> updatedPosition
        !isNormalPlayback -> updatedPosition
        updatedPosition >= previous.fraction -> speedUpBar(previous, positionMs, durationMs, updatedPosition)
        else -> slowDownBar(previous, positionMs, durationMs)
    }
}

/** Moves a bar that is behind [updatedPosition] faster than playback, so it catches up without passing it. */
private fun speedUpBar(previous: ArticleProgress, positionMs: Long, durationMs: Long, updatedPosition: Float) =
    (previous.fraction + (positionMs - previous.positionMs) * (1 + BAR_PLAYBACK_SPEED_CHANGE) / durationMs)
        .coerceAtMost(updatedPosition)

/**
 * Moves a bar that is ahead of playback slower than playback, so playback catches up, without filling it before the
 * end.
 */
private fun slowDownBar(previous: ArticleProgress, positionMs: Long, durationMs: Long): Float {
    val stepMs = positionMs - previous.positionMs
    val remainingMs = (durationMs - previous.positionMs).coerceAtLeast(1)
    val stepFraction = minOf((1 - BAR_PLAYBACK_SPEED_CHANGE) / durationMs, (1f - previous.fraction) / remainingMs)
    return (previous.fraction + stepMs * stepFraction).coerceAtMost(1f)
}

/**
 * The length the bar is drawn against. Given to the notification, which draws the position divided by the length, so
 * its bar matches the sheet's.
 */
internal fun ArticleProgress.shownDurationMs(): Long {
    val drawn = fraction
    return if (drawn > 0f && barPosition != null && positionMs > 0) {
        (positionMs / drawn).roundToLong()
    } else {
        durationMs
    }
}
