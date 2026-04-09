/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.shake

import android.hardware.SensorManager
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import mozilla.components.concept.accelerometer.Accelerometer
import mozilla.components.lib.shake.ShakeSensitivity.Companion.High
import mozilla.components.lib.shake.ShakeSensitivity.Companion.Low
import mozilla.components.lib.shake.ShakeSensitivity.Companion.Medium
import kotlin.math.abs
import kotlin.math.sqrt

/**
 * Run accelerometer data through a shake detection function.
 *
 * This uses a combination of a rolling window, cooldown period, sensitivity, and the number of hits
 * or pulses to determine a shake.
 *
 * @param sensitivity The [ShakeSensitivity] of the detection. This determines how much effort is
 * required to trigger a shake
 * @param detectionWindowNs The time in nanoseconds during which the movements and hits are observed to
 * determine a shake gesture
 * @param cooldownPeriodNs The time in nanoseconds after a shake is detected. Any movement or shake that happens
 * during this period is ignored
 * @param minHits The minimum number of hits/pulses/rapid motion (above the threshold
 * defined by [sensitivity]) that we need to see to determine that a shake has happened.
 */
fun Accelerometer.detectShakes(
    sensitivity: ShakeSensitivity = Low,
    detectionWindowNs: Long = 500_000_000L,
    cooldownPeriodNs: Long = 800_000_000L,
    minHits: Int = 3,
): Flow<Unit> = flow {
    val state = ShakeState()
    samples().collect { sample ->
        state.process(
            sample = sample,
            sensitivity = sensitivity,
            detectionWindowNs = detectionWindowNs,
            cooldownPeriodNs = cooldownPeriodNs,
            minHits = minHits,
        )

        if (state.hasReachedMinHits) {
            emit(Unit)
        }
    }
}

/**
 * Configuration element for the sensitivity of shake detection.
 *
 * @property threshold To be compared to an acceleration magnitude.
 */
@JvmInline
value class ShakeSensitivity(val threshold: Float) {
    companion object {
        /**
         * [Low] sensitivity - a 2g threshold
         */
        val Low = ShakeSensitivity(threshold = 2.g)

        /**
         * [Medium] sensitivity - a 1.5g threshold
         */
        val Medium = ShakeSensitivity(threshold = 1.5.g)

        /**
         * [High] sensitivity - a 1g threshold
         */
        val High = ShakeSensitivity(threshold = 1.g)
    }
}

/**
 * Direction marker for the shake direction.
 */
private enum class ShakeDirection {

    /**
     * Unknown is the default state
     */
    Unknown,

    /**
     * Forward implies a positive acceleration value
     */
    Forward,

    /**
     * Reverse implies a negative acceleration value
     */
    Reverse,
}

/**
 * This class is used to accumulate rolling shake data - basically, we want to track acceleration
 * magnitudes across a period of time and if we get enough spikes above a threshold within a window
 * we consider it a shake. This state is accumulated until we have reached enough hits to pass the
 * minimum required.
 */
private data class ShakeState(
    var hits: Int = 0,
    var detectionWindowStartNs: Long = 0L,
    var lastShakeNs: Long = 0L,
    var lastShakeDirection: ShakeDirection = ShakeDirection.Unknown,
    var hasReachedMinHits: Boolean = false,
) {
    fun process(
        sample: Accelerometer.Sample,
        sensitivity: ShakeSensitivity,
        detectionWindowNs: Long,
        cooldownPeriodNs: Long,
        minHits: Int,
    ) {
        // Step 0: Unwrap the Sample object & calculate the magnitude of acceleration
        val magnitude = sqrt(
            sample.xAccel * sample.xAccel +
                sample.yAccel * sample.yAccel +
                sample.zAccel * sample.zAccel,
        )
        val timestampNs = sample.timestampNs

        // Step 1: Check if acceleration magnitude is below the threshold.
        // If it is below, return and wait for stronger hit.
        if (magnitude < sensitivity.threshold) {
            hasReachedMinHits = false
            return
        }

        // Step 2: Check the approximate shake direction based on the most dominant acceleration
        val currentShakeDirection = sample.approximateShakeDirection()
        val shakeDirectionChanged = currentShakeDirection != lastShakeDirection

        // Step 3: Manage the rolling time window for counting acceleration spikes.
        // Either:
        // - start a new window (if no window exists or previous window expired)
        // - continue the current window and increment the hit counter if the shake direction has changed
        // - else continue the current window, but don't record this as a new hit
        val isExpired =
            detectionWindowStartNs == 0L || timestampNs - detectionWindowStartNs > detectionWindowNs
        val newHits = when {
            isExpired -> 1
            shakeDirectionChanged -> hits + 1
            else -> hits
        }
        val newWindowStart = if (isExpired) timestampNs else detectionWindowStartNs

        // Step 4: Check if we're in the cooldown period after a previous shake detection.
        // This prevents rapid-fire shake events from a single physical shake gesture, since the
        // detection window is shorter than the cooldown period.
        val inCooldown = lastShakeNs != 0L && timestampNs - lastShakeNs < cooldownPeriodNs

        // Step 5: Decide whether to emit a shake event
        return if (!inCooldown && newHits >= minHits) {
            // Shake detected: we have enough hits within the window and previous cooldown has passed.
            // Reset state and record this shake's timestamp to start cooldown period.
            hits = 0
            detectionWindowStartNs = 0L
            lastShakeNs = timestampNs
            lastShakeDirection = ShakeDirection.Unknown
            hasReachedMinHits = true
        } else {
            // No shake yet: continue accumulating hits within the current window.
            hits = newHits
            detectionWindowStartNs = newWindowStart
            lastShakeNs = lastShakeNs
            lastShakeDirection = currentShakeDirection
            hasReachedMinHits = false
        }
    }

    /**
     * Approximates a shake direction from a [Accelerometer.Sample].
     *
     * It currently is a very simple approximation that infers the direction as [ShakeDirection.Forward]
     * or [ShakeDirection.Reverse] based on the largest acceleration recorded.
     *
     * E.g.:
     *   - if we record [x = -3, y = 0, z = 1], we determine the direction to be reverse because the
     *   maximum recorded acceleration had a magnitude of 3, but in the negative direction.
     *   - if we record [x = -1, y = 5, z = 3], we determine the direction to be forward because the
     *   maximum recorded acceleration had a magnitude of 5, but in the positive direction.
     *
     * There are more complex algorithms, like doing a dot product of previously recorded samples
     * and the new one, but this is sufficient for now.
     *
     */
    private fun Accelerometer.Sample.approximateShakeDirection(): ShakeDirection {
        val largestAcceleration = maxOf(abs(xAccel), abs(yAccel), abs(zAccel))
        val dominantAxis = when (largestAcceleration) {
            abs(xAccel) -> xAccel
            abs(yAccel) -> yAccel
            else -> zAccel
        }
        return if (dominantAxis > 0f) ShakeDirection.Forward else ShakeDirection.Reverse
    }
}

/**
 * Helper to convert [Double] to a value as a multiple acceleration due to gravity
 */
internal inline val Double.g: Float get() = this.toFloat() * SensorManager.GRAVITY_EARTH

/**
 * Helper to convert [Int] to a value as a multiple acceleration due to gravity
 */
internal inline val Int.g: Float get() = this * SensorManager.GRAVITY_EARTH
