/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.accelerometer.sensormanager

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorManager
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.testing.TestLifecycleOwner
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.accelerometer.Accelerometer
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Shadows
import org.robolectric.annotation.Config
import org.robolectric.shadows.SensorEventBuilder
import org.robolectric.shadows.ShadowSensor
import org.robolectric.shadows.ShadowSensorManager

@RunWith(AndroidJUnit4::class)
@Config(sdk = [28])
class LifecycleAwareSensorManagerAccelerometerTest {

    private val testDispatcher = StandardTestDispatcher()

    private lateinit var lifecycleOwner: TestLifecycleOwner

    private lateinit var sensorManager: SensorManager
    private lateinit var shadowSensorManager: ShadowSensorManager

    private lateinit var accelerometer: LifecycleAwareSensorManagerAccelerometer
    private val logMessages = mutableListOf<String>()

    @Before
    fun setUp() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        sensorManager = context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
        shadowSensorManager = Shadows.shadowOf(sensorManager)
        shadowSensorManager.addSensor(ShadowSensor.newInstance(Sensor.TYPE_ACCELEROMETER))

        logMessages.clear()
        accelerometer = LifecycleAwareSensorManagerAccelerometer(
            sensorManager = sensorManager,
            logger = { message -> logMessages.add(message) },
        )

        lifecycleOwner = TestLifecycleOwner(Lifecycle.State.INITIALIZED)
        lifecycleOwner.lifecycle.addObserver(accelerometer)
    }

    @Test
    fun `when lifecycle is resumed, sensor listener is registered and logged`() {
        lifecycleOwner.handleLifecycleEvent(Lifecycle.Event.ON_RESUME)

        assertEquals(listOf("Registering self as sensor listener"), logMessages)
    }

    @Test
    fun `when lifecycle is paused, sensor listener is unregistered and logged`() {
        lifecycleOwner.handleLifecycleEvent(Lifecycle.Event.ON_RESUME)
        lifecycleOwner.handleLifecycleEvent(Lifecycle.Event.ON_PAUSE)

        assertEquals(
            listOf("Registering self as sensor listener", "Unregistering self as sensor listener"),
            logMessages,
        )
    }

    @Test
    fun `when lifecycle is resumed, sensor events are emitted`() = runTest(testDispatcher) {
        lifecycleOwner.handleLifecycleEvent(Lifecycle.Event.ON_RESUME)

        val samples = mutableListOf<Accelerometer.Sample>()
        val samplesJob = launch(testDispatcher) {
            accelerometer.samples()
                .collect { samples.add(it) }
        }
        testDispatcher.scheduler.runCurrent()

        emitSensorEvent(floatArrayOf(SensorManager.GRAVITY_EARTH, SensorManager.GRAVITY_EARTH * 2f, SensorManager.GRAVITY_EARTH * 3f))
        emitSensorEvent(floatArrayOf(SensorManager.GRAVITY_EARTH * 4f, SensorManager.GRAVITY_EARTH * 5f, SensorManager.GRAVITY_EARTH * 6f))

        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals("Two sensor events are observed", 2, samples.size)
        assertEquals(1f, samples[0].xAccel, 0.001f)
        assertEquals(2f, samples[0].yAccel, 0.001f)
        assertEquals(3f, samples[0].zAccel, 0.001f)
        assertEquals(4f, samples[1].xAccel, 0.001f)
        assertEquals(5f, samples[1].yAccel, 0.001f)
        assertEquals(6f, samples[1].zAccel, 0.001f)
        samplesJob.cancel()
    }

    @Test
    fun `when lifecycle is paused, no sensor events are emitted`() = runTest(testDispatcher) {
        val samples = mutableListOf<Accelerometer.Sample>()
        val samplesJob = launch(testDispatcher) {
            accelerometer.samples()
                .collect { samples.add(it) }
        }
        testDispatcher.scheduler.runCurrent()

        lifecycleOwner.handleLifecycleEvent(Lifecycle.Event.ON_RESUME)
        emitSensorEvent(floatArrayOf(SensorManager.GRAVITY_EARTH, SensorManager.GRAVITY_EARTH * 2f, SensorManager.GRAVITY_EARTH * 3f))
        emitSensorEvent(floatArrayOf(SensorManager.GRAVITY_EARTH * 4f, SensorManager.GRAVITY_EARTH * 5f, SensorManager.GRAVITY_EARTH * 6f))

        testDispatcher.scheduler.advanceUntilIdle()

        lifecycleOwner.handleLifecycleEvent(Lifecycle.Event.ON_PAUSE)

        emitSensorEvent(floatArrayOf(SensorManager.GRAVITY_EARTH * 7f, SensorManager.GRAVITY_EARTH * 8f, SensorManager.GRAVITY_EARTH * 9f))

        testDispatcher.scheduler.advanceUntilIdle()

        samplesJob.cancel()
        assertEquals("Expected that only the two prior sensor events were emitted", 2, samples.size)
    }

    @Test
    fun `samples normalizes sensor values by gravity`() = runTest(testDispatcher) {
        lifecycleOwner.handleLifecycleEvent(Lifecycle.Event.ON_RESUME)

        val samples = mutableListOf<Accelerometer.Sample>()
        val samplesJob = launch(testDispatcher) {
            accelerometer.samples()
                .collect { samples.add(it) }
        }
        testDispatcher.scheduler.runCurrent()

        emitSensorEvent(
            floatArrayOf(
                SensorManager.GRAVITY_EARTH * 2.0f,
                SensorManager.GRAVITY_EARTH * -1.5f,
                SensorManager.GRAVITY_EARTH * 0.25f,
            ),
        )

        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(1, samples.size)
        assertEquals(2.0f, samples[0].xAccel, 0.001f)
        assertEquals(-1.5f, samples[0].yAccel, 0.001f)
        assertEquals(0.25f, samples[0].zAccel, 0.001f)
        samplesJob.cancel()
    }

    @Test
    fun `samples emits normalized samples for multiple events`() = runTest(testDispatcher) {
        lifecycleOwner.handleLifecycleEvent(Lifecycle.Event.ON_RESUME)

        val samples = mutableListOf<Accelerometer.Sample>()
        val samplesJob = launch(testDispatcher) {
            accelerometer.samples()
                .collect { samples.add(it) }
        }
        testDispatcher.scheduler.runCurrent()

        emitSensorEvent(
            floatArrayOf(
                SensorManager.GRAVITY_EARTH * 0.5f,
                SensorManager.GRAVITY_EARTH * 1.0f,
                SensorManager.GRAVITY_EARTH * 0.0f,
            ),
        )
        emitSensorEvent(
            floatArrayOf(
                SensorManager.GRAVITY_EARTH * 1.5f,
                SensorManager.GRAVITY_EARTH * -0.5f,
                SensorManager.GRAVITY_EARTH * 2.0f,
            ),
        )

        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(2, samples.size)
        assertEquals(0.5f, samples[0].xAccel, 0.001f)
        assertEquals(1.0f, samples[0].yAccel, 0.001f)
        assertEquals(0.0f, samples[0].zAccel, 0.001f)

        assertEquals(1.5f, samples[1].xAccel, 0.001f)
        assertEquals(-0.5f, samples[1].yAccel, 0.001f)
        assertEquals(2.0f, samples[1].zAccel, 0.001f)
        samplesJob.cancel()
    }

    @Test
    fun `samples handles zero values`() = runTest(testDispatcher) {
        lifecycleOwner.handleLifecycleEvent(Lifecycle.Event.ON_RESUME)

        val samples = mutableListOf<Accelerometer.Sample>()
        val samplesJob = launch(testDispatcher) {
            accelerometer.samples()
                .collect { samples.add(it) }
        }
        testDispatcher.scheduler.runCurrent()

        emitSensorEvent(floatArrayOf(0.0f, 0.0f, 0.0f))

        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(1, samples.size)
        assertEquals(0.0f, samples[0].xAccel, 0.001f)
        assertEquals(0.0f, samples[0].yAccel, 0.001f)
        assertEquals(0.0f, samples[0].zAccel, 0.001f)
        samplesJob.cancel()
    }

    @Test
    fun `samples handles negative values`() = runTest(testDispatcher) {
        lifecycleOwner.handleLifecycleEvent(Lifecycle.Event.ON_RESUME)

        val samples = mutableListOf<Accelerometer.Sample>()
        val samplesJob = launch(testDispatcher) {
            accelerometer.samples()
                .collect { samples.add(it) }
        }
        testDispatcher.scheduler.runCurrent()

        emitSensorEvent(
            floatArrayOf(
                -SensorManager.GRAVITY_EARTH,
                -SensorManager.GRAVITY_EARTH * 2.0f,
                -SensorManager.GRAVITY_EARTH * 0.5f,
            ),
        )

        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(1, samples.size)
        assertEquals(-1.0f, samples[0].xAccel, 0.001f)
        assertEquals(-2.0f, samples[0].yAccel, 0.001f)
        assertEquals(-0.5f, samples[0].zAccel, 0.001f)
        samplesJob.cancel()
    }

    private fun emitSensorEvent(values: FloatArray) {
        val sensor = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
        shadowSensorManager.sendSensorEventToListeners(
            SensorEventBuilder.newBuilder()
                .setSensor(requireNotNull(sensor))
                .setValues(values)
                .setTimestamp(1234)
                .build(),
        )
    }
}
