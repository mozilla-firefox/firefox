/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.shake

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.accelerometer.Accelerometer
import org.junit.Assert.assertEquals
import org.junit.Test

class ShakeDetectionTest {

    @Test
    fun `shakes are detected when magnitude exceeds threshold`() = runTest {
        // Given a shake sequence of two hits
        val samples = flowOf(
            sample(x = 0.g, y = 0.g, z = 0.g, timestampNs = 0L),
            // shake with magnitude of 1g
            sample(x = 0.g, y = 1.g, z = 0.g, 1L),
            sample(x = 0.g, y = (-1).g, z = 0.g, 2L),
        )

        val accelerometer = FakeAccelerometer(samples)

        // When we listen for shakes with a threshold of 1g
        val shakes = accelerometer.detectShakes(
            sensitivity = ShakeSensitivity(1.g),
            detectionWindowNs = 10L,
            cooldownPeriodNs = 10L,
            minHits = 2,
        ).toList()

        // Then verify that 1 shake is detected
        assertEquals(
            "Expected 1 shake is detected since sensitivity is 1.g",
            1,
            shakes.size,
        )
    }

    @Test
    fun `no shake is detected when magnitude is below threshold`() = runTest {
        // Given a shake sequence of two weak hits
        val samples = flowOf(
            sample(x = 0.g, y = 0.g, z = 0.g, timestampNs = 0L),
            // shake with magnitude of 1g
            sample(x = 0.g, y = 1.g, z = 0.g, 10L),
            sample(x = 0.g, y = (-1).g, z = 0.g, 15L),
            // shake with magnitude of 1g
            sample(x = 0.g, y = 1.g, z = 0.g, 20L),
            sample(x = 0.g, y = (-1).g, z = 0.g, 25L),
        )

        val accelerometer = FakeAccelerometer(samples)
        val shakes = accelerometer.detectShakes(
            sensitivity = ShakeSensitivity(1.1.g),
            detectionWindowNs = 30L,
            cooldownPeriodNs = 20L,
            minHits = 2,
        ).toList()

        // Then verify that no shake is detected
        assertEquals(
            "Expected no shake is detected since sensitivity is above 1.g",
            true,
            shakes.isEmpty(),
        )
    }

    @Test
    fun `shakes are detected when minimum hits within the time window are received`() = runTest {
        // Given a shake sequence of two hits
        val samples = flowOf(
            sample(timestampNs = 0L),
            // shake - forward & backward along z-axis
            sample(z = 2.g, timestampNs = 1L),
            sample(z = (-1.1).g, timestampNs = 3L),
            // shake - forward & backward along x-axis
            sample(x = 1.g, timestampNs = 10L),
            sample(x = (-1).g, timestampNs = 13L),
        )

        val accelerometer = FakeAccelerometer(samples)

        // When we detect shakes with minimum hits of 3
        val shakes = accelerometer.detectShakes(
            sensitivity = ONE_G_SENSITIVITY,
            detectionWindowNs = 4,
            cooldownPeriodNs = 5,
            minHits = 3,
        ).toList()

        // Then assert that no shake is detected because we only ever recorded 2 hits during each shake
        assertEquals(
            "Expected no shake was detected because minimum hits is 3, but we only recorded 2 hits",
            true,
            shakes.isEmpty(),
        )
    }

    @Test
    fun `when more than the min hits are recorded during the time window, they are classified as a single shake`() =
        runTest {
            val samples = flowOf(
                sample(timestampNs = 0L),
                // shake
                sample(x = 2.g, timestampNs = 10L),
                sample(x = (-2).g, timestampNs = 11L),
                // shake
                sample(x = 2.g, timestampNs = 12L),
                sample(x = (-2).g, timestampNs = 13L),
                // shake
                sample(x = 1.g, timestampNs = 13L),
                sample(x = (-1).g, timestampNs = 14L),
            )

            val accelerometer = FakeAccelerometer(samples)
            val shakes = accelerometer.detectShakes(
                sensitivity = ONE_G_SENSITIVITY,
                detectionWindowNs = 5L,
                cooldownPeriodNs = 5L,
                minHits = 2,
            ).toList()

            assertEquals(1, shakes.size)
        }

    @Test
    fun `additional shakes during the cooldown period are ignored`() = runTest {
        // Given a flow of two shakes: before the cooldown period is over
        val samples = flowOf(
            sample(timestampNs = 0L),
            // shake
            sample(x = 2.g, timestampNs = 10L),
            sample(x = (-2).g, timestampNs = 11L),
            // shake
            sample(x = 2.g, timestampNs = 12L),
            sample(y = (-2).g, timestampNs = 13L),
        )

        val accelerometer = FakeAccelerometer(samples)

        // When we detect shakes
        val shakes = accelerometer.detectShakes(
            sensitivity = ONE_G_SENSITIVITY,
            detectionWindowNs = 5L,
            cooldownPeriodNs = 5L,
        ).toList()

        // Then verify that only one shake is detected
        assertEquals(1, shakes.size)
    }

    @Test
    fun `shakes are detected after cooldown period expires`() = runTest {
        // Given a flow of: two shakes, separated by 7ns
        val samples = flowOf(
            sample(timestampNs = 0L),
            // shake - three hits along z-axis within 4ns window
            sample(z = 2.g, timestampNs = 1L),
            sample(z = (-1.1).g, timestampNs = 2L),
            sample(z = 2.g, timestampNs = 3L),
            // shake - three hits along x-axis, after cooldown expires
            sample(x = 1.g, timestampNs = 10L),
            sample(x = (-1).g, timestampNs = 11L),
            sample(x = 1.g, timestampNs = 13L),
        )

        val accelerometer = FakeAccelerometer(samples)

        // When we detect shakes with the following conditions:
        // - a 4-nanosecond window and
        // - with 5-nanosecond cool down period
        val shakes = accelerometer.detectShakes(
            sensitivity = ONE_G_SENSITIVITY,
            detectionWindowNs = 4,
            cooldownPeriodNs = 5,
        ).toList()

        // Then verify that we observe 2 shakes
        assertEquals(2, shakes.size)
    }

    companion object {
        private val ONE_G_SENSITIVITY = ShakeSensitivity(1.g)
        private fun sample(
            x: Float = 1.g,
            y: Float = 1.g,
            z: Float = 1.g,
            timestampNs: Long,
        ): Accelerometer.Sample {
            return object : Accelerometer.Sample {
                override val xAccel = x
                override val yAccel = y
                override val zAccel = z
                override val timestampNs = timestampNs
            }
        }
    }
}

class FakeAccelerometer(private val samples: Flow<Accelerometer.Sample>) : Accelerometer {
    override fun samples(): Flow<Accelerometer.Sample> = samples
}
