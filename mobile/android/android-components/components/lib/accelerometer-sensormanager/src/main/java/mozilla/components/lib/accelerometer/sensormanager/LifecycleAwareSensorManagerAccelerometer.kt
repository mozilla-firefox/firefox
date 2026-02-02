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
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.map
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

    private val sensor: Sensor? by lazy {
        sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
    }

    private val _samples = MutableSharedFlow<Accelerometer.Sample>(
        extraBufferCapacity = NUM_BUFFER_CAPACITY,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )

    override fun samples(): Flow<Accelerometer.Sample> = _samples.map(::normalizeForGravity)

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) = Unit

    override fun onSensorChanged(event: SensorEvent) {
        val sample = Accelerometer.Sample(
            xAccel = event.values[0],
            yAccel = event.values[1],
            zAccel = event.values[2],
            timestampNs = event.timestamp,
        )
        _samples.tryEmit(sample)
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

    private fun normalizeForGravity(sample: Accelerometer.Sample) = sample.copy(
        xAccel = sample.xAccel / SensorManager.GRAVITY_EARTH,
        yAccel = sample.yAccel / SensorManager.GRAVITY_EARTH,
        zAccel = sample.zAccel / SensorManager.GRAVITY_EARTH,
    )
}
