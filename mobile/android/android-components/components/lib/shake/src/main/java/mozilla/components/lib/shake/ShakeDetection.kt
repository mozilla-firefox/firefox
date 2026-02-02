/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.shake

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.scan
import mozilla.components.concept.accelerometer.Accelerometer
import kotlin.math.sqrt

/**
 * Run accelerometer data through a shake detection function. Uses a rolling window to
 */
fun Accelerometer.detectShakes(sensitivity: ShakeSensitivity = ShakeSensitivity.Medium): Flow<Unit> =
    samples()
        .map { sample ->
            // magnitude of acceleration vector
            val magnitude = sqrt(
                sample.xAccel * sample.xAccel +
                    sample.yAccel * sample.yAccel +
                    sample.zAccel * sample.zAccel,
            )
            magnitude to sample.timestampNs
        }
        .scan(ShakeState()) { state, (magnitude, timestampNs) ->
            state.next(magnitude, timestampNs, sensitivity)
        }
        .filter { it.hasReachedMinHits }
        .map { Unit }

/**
 * Configuration element for the sensitivity of shake detection.
 *
 * @property threshold To be compared to an acceleration magnitude.
 */
@JvmInline
value class ShakeSensitivity(val threshold: Float) {
    companion object {
        val Low = ShakeSensitivity(threshold = 3.2f)
        val Medium = ShakeSensitivity(threshold = 2.7f)
        val High = ShakeSensitivity(threshold = 2.3f)
    }
}

/**
 * This class is used to accumulate rolling shake data - basically, we want to track acceleration
 * magnitudes across a period of time and if we get enough spikes above a threshold within a window
 * we consider it a shake. This state is accumulated until we have reached enough hits to pass the
 * minimum required.
 */
private data class ShakeState(
    val hits: Int = 0,
    val detectionWindowStartNs: Long = 0L,
    val lastShakeNs: Long = 0L,
    val hasReachedMinHits: Boolean = false,
) {
    fun next(
        magnitude: Float,
        timestampNs: Long,
        sensitivity: ShakeSensitivity,
        detectionWindowNs: Long = 350_000_000L,
        cooldownPeriodNs: Long = 800_000_000L,
        minHits: Int = 2,
    ): ShakeState {
        // Step 1: Check if acceleration magnitude exceeds the threshold.
        // If not, reset detection state and wait for stronger acceleration.
        val aboveThreshold = magnitude >= sensitivity.threshold
        if (!aboveThreshold) return copy(hasReachedMinHits = false)

        // Step 2: Manage the rolling time window for counting acceleration spikes.
        // Either start a new window (if no window exists or previous window expired)
        // or continue the current window and increment the hit counter.
        val (newWindowStart, newHits) =
            if (detectionWindowStartNs == 0L || timestampNs - detectionWindowStartNs > detectionWindowNs) {
                // Window expired or uninitialized: start new window at current timestamp
                timestampNs to 1
            } else {
                // Window still active: keep window start and increment hit count
                detectionWindowStartNs to hits + 1
            }

        // Step 3: Check if we're in the cooldown period after a previous shake detection.
        // This prevents rapid-fire shake events from a single physical shake gesture, since the
        // detection window is shorter than the cooldown period.
        val inCooldown =
            lastShakeNs != 0L && timestampNs - lastShakeNs < cooldownPeriodNs

        // Step 4: Decide whether to emit a shake event
        return if (!inCooldown && newHits >= minHits) {
            // Shake detected: we have enough hits within the window and previous cooldown has passed.
            // Reset state and record this shake's timestamp to start cooldown period.
            ShakeState(
                hits = 0,
                detectionWindowStartNs = 0L,
                lastShakeNs = timestampNs,
                hasReachedMinHits = true,
            )
        } else {
            // No shake yet: continue accumulating hits within the current window.
            ShakeState(
                hits = newHits,
                detectionWindowStartNs = newWindowStart,
                lastShakeNs = lastShakeNs,
                hasReachedMinHits = false,
            )
        }
    }
}
