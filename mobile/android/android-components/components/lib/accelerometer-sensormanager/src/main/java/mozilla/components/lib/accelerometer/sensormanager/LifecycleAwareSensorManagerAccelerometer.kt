/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.accelerometer.sensormanager

import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.receiveAsFlow
import mozilla.components.concept.accelerometer.Accelerometer
import mozilla.components.support.base.log.logger.Logger

private const val NUM_BUFFER_CAPACITY = 5

/**
 * This class is an adapter between Android platform accelerometer data and
 * platform-agnostic data. It uses the Android [SensorManager] to collect this data,
 * and can be added as a lifecycle observer to handle registering and unregistering its
 * sensor management automatically. It then converts received Sensor data into more friendly
 * types.
 */
class LifecycleAwareSensorManagerAccelerometer(
    private val sensorManager: SensorManager,
    private val logger: (String) -> Unit = { message ->
        Logger("mozac/LifecycleAwareSensorManagerEventFlow").debug(message)
    },
) : Accelerometer, SensorEventListener, DefaultLifecycleObserver {

    private val sampleBuffer = SampleBuffer(capacity = 10)

    private val sensor: Sensor? by lazy {
        sensorManager.getDefaultSensor(Sensor.TYPE_LINEAR_ACCELERATION)
            ?: sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
    }

    private val _samples = Channel<Accelerometer.Sample>(
        capacity = NUM_BUFFER_CAPACITY,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )

    override fun samples(): Flow<Accelerometer.Sample> = _samples.receiveAsFlow()

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) = Unit

    override fun onSensorChanged(event: SensorEvent) {
        event.toSample()?.also { _samples.trySend(it) }
    }

    override fun onResume(owner: LifecycleOwner) {
        super.onResume(owner)
        logger("Registering self as sensor listener")
        sensorManager.registerListener(this, sensor, SensorManager.SENSOR_DELAY_UI)
    }

    override fun onPause(owner: LifecycleOwner) {
        super.onPause(owner)
        logger("Unregistering self as sensor listener")
        sensorManager.unregisterListener(this)
    }

    override fun onDestroy(owner: LifecycleOwner) {
        super.onDestroy(owner)
        _samples.close()
    }

    private fun SensorEvent.toSample(): Accelerometer.Sample? = when (sensor.type) {
        Sensor.TYPE_LINEAR_ACCELERATION -> linearEventToSample()
        Sensor.TYPE_ACCELEROMETER -> toLinearAccelerationSample()
        else -> null
    }

    private fun SensorEvent.linearEventToSample(): Accelerometer.Sample {
        return sampleBuffer.request(
            xAccel = values[0],
            yAccel = values[1],
            zAccel = values[2],
            timestampNs = timestamp,
        )
    }

    /**
     * This function converts raw [Sensor.TYPE_ACCELEROMETER] values to a linear acceleration. i.e
     * actual linear movements of the device across the x,y,z axes.
     *
     * The raw events here include the impact of gravity, so we need to isolate the impact of gravity,
     * and then subtract it from the reading.
     *
     * This is done in 2 steps:
     * ## Step 1: Isolate the effect of gravity in the accelerometer reading.
     *
     * Since the gravity does not change frequently, it is a low-frequency data, and since movements
     * of the phone are somewhat sudden and involve quick changes, that is considered high-frequency
     * data.
     *
     *  To properly isolate gravity then, we are using a low-pass filter with the following formula:
     *
     *          g = a*g + (1-a) * acceleration
     *
     * The idea behind the choice is that we are trying to isolate the acceleration due to gravity,
     * and since we consider that a fairly low frequency data (does not change frequently),
     * a low-pass is appropriate for the task.
     *
     * Important points:
     * * This operation is applied continuously, based on the previously calculated value, and
     * that's why we are writing to the same [GRAVITY_VALUES] float array.
     * * The alpha/a value ([ACCELEROMETER_FILTER_ALPHA]) is a smoothing factor, and this is based
     * off calculations related to the rate at which the sensor emits the data.
     *
     * ## Step 2: Remove acceleration due to gravity
     * Since we have now been able to isolate or estimate how much of this acceleration was caused
     * by the acceleration due to gravity, we can go ahead and subtract it from the raw value.
     */
    private fun SensorEvent.toLinearAccelerationSample(): Accelerometer.Sample {
        // Step 1: Isolate the effect of acceleration due to gravity from the raw event
        GRAVITY_VALUES[0] =
            ACCELEROMETER_FILTER_ALPHA * GRAVITY_VALUES[0] + (1 - ACCELEROMETER_FILTER_ALPHA) * values[0]
        GRAVITY_VALUES[1] =
            ACCELEROMETER_FILTER_ALPHA * GRAVITY_VALUES[1] + (1 - ACCELEROMETER_FILTER_ALPHA) * values[1]
        GRAVITY_VALUES[2] =
            ACCELEROMETER_FILTER_ALPHA * GRAVITY_VALUES[2] + (1 - ACCELEROMETER_FILTER_ALPHA) * values[2]

        // Step 2: Subtract it from the raw value to get the actual linear acceleration
        val linearX = values[0] - GRAVITY_VALUES[0]
        val linearY = values[1] - GRAVITY_VALUES[1]
        val linearZ = values[2] - GRAVITY_VALUES[2]

        return sampleBuffer.request(
            xAccel = linearX,
            yAccel = linearY,
            zAccel = linearZ,
            timestampNs = timestamp,
        )
    }

    private companion object {
        private val GRAVITY_VALUES = FloatArray(3)
        private const val ACCELEROMETER_FILTER_ALPHA = 0.8f
    }
}
