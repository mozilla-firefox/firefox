/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.accelerometer.sensormanager

import mozilla.components.concept.accelerometer.Accelerometer

/** Mutable [Accelerometer.Sample] backed by a [SampleBuffer] slot. */
internal data class SensorSample(
    override var xAccel: Float = 0f,
    override var yAccel: Float = 0f,
    override var zAccel: Float = 0f,
    override var timestampNs: Long = 0,
) : Accelerometer.Sample {
    fun reset(): SensorSample {
        xAccel = 0f
        yAccel = 0f
        zAccel = 0f
        return this
    }
}

/**
 * A fixed-size ring buffer of reusable [SensorSample] instances to avoid allocation on every sensor
 * event.
 *
 * @param capacity the number of [SensorSample] slots to pre-allocate.
 */
internal class SampleBuffer(
    capacity: Int,
) {
    private var samples = Array(capacity) { SensorSample() }
    private var index = 0

    /**
     * Returns the next slot in the ring buffer, populated with the given values.
     */
    fun request(
        xAccel: Float,
        yAccel: Float,
        zAccel: Float,
        timestampNs: Long,
    ): SensorSample {
        val requestedSample = samples[index].also {
            it.xAccel = xAccel
            it.yAccel = yAccel
            it.zAccel = zAccel
            it.timestampNs = timestampNs
        }

        index = (index + 1) % samples.size

        return requestedSample
    }
}
