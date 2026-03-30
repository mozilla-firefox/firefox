/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.accelerometer.sensormanager

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertSame
import org.junit.Test

class SensorSampleTest {

    @Test
    fun `request populates sample with given values`() {
        val buffer = SampleBuffer(2)
        val sample = buffer.request(1f, 2f, 3f, 100L)

        assertEquals(1f, sample.xAccel)
        assertEquals(2f, sample.yAccel)
        assertEquals(3f, sample.zAccel)
        assertEquals(100L, sample.timestampNs)
    }

    @Test
    fun `slots within capacity are distinct instances`() {
        val buffer = SampleBuffer(2)
        val first = buffer.request(1f, 0f, 0f, 0L)
        val second = buffer.request(2f, 0f, 0f, 0L)

        assertNotSame(first, second)
    }

    @Test
    fun `buffer wraps and reuses the first slot after full cycle`() {
        val buffer = SampleBuffer(2)
        val first = buffer.request(1f, 0f, 0f, 0L)
        buffer.request(2f, 0f, 0f, 0L)
        val wrapped = buffer.request(3f, 0f, 0f, 0L)

        assertSame(first, wrapped)
        assertEquals(3f, wrapped.xAccel)
    }

    @Test
    fun `reused slot reflects new values, not old ones`() {
        val buffer = SampleBuffer(1)

        val original = buffer.request(1f, 2f, 3f, 100L)
        val reused = buffer.request(9f, 8f, 7f, 999L)

        assertEquals(9f, reused.xAccel)
        assertEquals(8f, reused.yAccel)
        assertEquals(7f, reused.zAccel)
        assertEquals(999L, reused.timestampNs)
    }
}
