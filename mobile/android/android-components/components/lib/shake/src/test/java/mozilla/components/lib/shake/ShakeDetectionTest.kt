/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.shake

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.accelerometer.Accelerometer
import org.junit.Assert.assertEquals
import org.junit.Test
import kotlin.math.sqrt

class ShakeDetectionTest {

    @Test
    fun `shakes detects shake with medium sensitivity when magnitude exceeds threshold`() = runTest {
        val samples = flowOf(
            Accelerometer.Sample(2.0f, 1.5f, 1.2f, 0L),
            Accelerometer.Sample(2.0f, 1.5f, 1.2f, 100_000_000L),
            Accelerometer.Sample(2.0f, 1.5f, 1.2f, 200_000_000L),
        )

        val accelerometer = FakeAccelerometer(samples)
        val shakes = accelerometer.detectShakes(ShakeSensitivity.Medium).take(1).toList()

        assertEquals(1, shakes.size)
    }

    @Test
    fun `shakes does not emit when magnitude is below threshold`() = runTest {
        val samples = flowOf(
            Accelerometer.Sample(0.5f, 0.5f, 0.5f, 0L),
            Accelerometer.Sample(0.6f, 0.4f, 0.3f, 100_000_000L),
            Accelerometer.Sample(0.4f, 0.5f, 0.4f, 200_000_000L),
        )

        val accelerometer = FakeAccelerometer(samples)
        val shakes = accelerometer.detectShakes(ShakeSensitivity.Medium).take(1)

        val result = runCatching {
            kotlinx.coroutines.withTimeout(100) {
                shakes.first()
            }
        }

        assertEquals(true, result.isFailure)
    }

    @Test
    fun `shakes requires minimum hits within window`() = runTest {
        val samples = flowOf(
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 0L),
            Accelerometer.Sample(0.5f, 0.5f, 0.5f, 100_000_000L),
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 400_000_000L),
        )

        val accelerometer = FakeAccelerometer(samples)
        val shakes = accelerometer.detectShakes(ShakeSensitivity.Medium).take(1)

        val result = runCatching {
            kotlinx.coroutines.withTimeout(100) {
                shakes.first()
            }
        }

        assertEquals(true, result.isFailure)
    }

    @Test
    fun `shakes detects multiple hits within time window`() = runTest {
        val samples = flowOf(
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 0L),
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 100_000_000L),
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 200_000_000L),
        )

        val accelerometer = FakeAccelerometer(samples)
        val shakes = accelerometer.detectShakes(ShakeSensitivity.Medium).take(1).toList()

        assertEquals(1, shakes.size)
    }

    @Test
    fun `shakes respects cooldown period`() = runTest {
        val samples = flowOf(
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 0L),
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 100_000_000L),
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 200_000_000L),
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 300_000_000L),
        )

        val accelerometer = FakeAccelerometer(samples)
        val shakes = accelerometer.detectShakes(ShakeSensitivity.Medium).take(1).toList()

        assertEquals(1, shakes.size)
    }

    @Test
    fun `shakes detects shake after cooldown period expires`() = runTest {
        val samples = flowOf(
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 0L),
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 100_000_000L),
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 200_000_000L),
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 1_100_000_000L),
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 1_200_000_000L),
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 1_300_000_000L),
        )

        val accelerometer = FakeAccelerometer(samples)
        val shakes = accelerometer.detectShakes(ShakeSensitivity.Medium).take(2).toList()

        assertEquals(2, shakes.size)
    }

    @Test
    fun `shakes with high sensitivity uses lower threshold`() = runTest {
        val magnitude = 2.4f
        val accel = magnitude / sqrt(3f)
        val samples = flowOf(
            Accelerometer.Sample(accel, accel, accel, 0L),
            Accelerometer.Sample(accel, accel, accel, 100_000_000L),
            Accelerometer.Sample(accel, accel, accel, 200_000_000L),
        )

        val accelerometer = FakeAccelerometer(samples)
        val shakes = accelerometer.detectShakes(ShakeSensitivity.High).take(1).toList()

        assertEquals(1, shakes.size)
    }

    @Test
    fun `shakes with low sensitivity uses higher threshold`() = runTest {
        val magnitude = 3.0f
        val accel = magnitude / sqrt(3f)
        val samples = flowOf(
            Accelerometer.Sample(accel, accel, accel, 0L),
            Accelerometer.Sample(accel, accel, accel, 100_000_000L),
        )

        val accelerometer = FakeAccelerometer(samples)
        val shakes = accelerometer.detectShakes(ShakeSensitivity.Low).take(1)

        val result = runCatching {
            kotlinx.coroutines.withTimeout(100) {
                shakes.first()
            }
        }

        assertEquals(true, result.isFailure)
    }

    @Test
    fun `shakes window resets after expiration`() = runTest {
        val samples = flowOf(
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 0L),
            Accelerometer.Sample(0.5f, 0.5f, 0.5f, 400_000_000L),
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 1_000_000_000L),
            Accelerometer.Sample(2.0f, 2.0f, 1.0f, 1_100_000_000L),
        )

        val accelerometer = FakeAccelerometer(samples)
        val shakes = accelerometer.detectShakes(ShakeSensitivity.Medium).take(1).toList()

        assertEquals(1, shakes.size)
    }

    @Test
    fun `shakes calculates magnitude correctly for all axes`() = runTest {
        val samples = flowOf(
            Accelerometer.Sample(3.0f, 0.0f, 0.0f, 0L),
            Accelerometer.Sample(0.0f, 3.0f, 0.0f, 100_000_000L),
            Accelerometer.Sample(0.0f, 0.0f, 3.0f, 200_000_000L),
        )

        val accelerometer = FakeAccelerometer(samples)
        val shakes = accelerometer.detectShakes(ShakeSensitivity.Medium).take(1).toList()

        assertEquals(1, shakes.size)
    }

    @Test
    fun `shakes handles negative acceleration values`() = runTest {
        val samples = flowOf(
            Accelerometer.Sample(-2.0f, -2.0f, -1.0f, 0L),
            Accelerometer.Sample(-2.0f, -2.0f, -1.0f, 100_000_000L),
            Accelerometer.Sample(-2.0f, -2.0f, -1.0f, 200_000_000L),
        )

        val accelerometer = FakeAccelerometer(samples)
        val shakes = accelerometer.detectShakes(ShakeSensitivity.Medium).take(1).toList()

        assertEquals(1, shakes.size)
    }
}

class FakeAccelerometer(private val samples: Flow<Accelerometer.Sample>) : Accelerometer {
    override fun samples(): Flow<Accelerometer.Sample> = samples
}
