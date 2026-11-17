/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.util.Log;
import org.mozilla.gecko.annotation.WrapForJNI;

/* package */ class GeckoSensor {
  private static final String LOGTAG = "GeckoSensor";

  /*
   * Keep these values consistent with |SensorType| in HalSensor.h
   */
  private static final int SENSOR_ORIENTATION = 0;
  private static final int SENSOR_ACCELERATION = 1;
  private static final int SENSOR_PROXIMITY = 2;
  private static final int SENSOR_LINEAR_ACCELERATION = 3;
  private static final int SENSOR_GYROSCOPE = 4;
  private static final int SENSOR_LIGHT = 5;
  private static final int SENSOR_ROTATION_VECTOR = 6;
  private static final int SENSOR_GAME_ROTATION_VECTOR = 7;

  private static final int ORIENTATION_SENSOR_DELAY = 16666; // 60Hz
  private static final int MOTION_SENSOR_DELAY = 16666; // 60Hz

  @SuppressWarnings("deprecation")
  private static final int SENSOR_TYPE_ORIENTATION = Sensor.TYPE_ORIENTATION;

  private static SensorListeners sListeners;

  private static Sensor gAccelerometerSensor;
  private static Sensor gLinearAccelerometerSensor;
  private static Sensor gGyroscopeSensor;
  private static Sensor gOrientationSensor;
  private static Sensor gLightSensor;
  private static Sensor gRotationVectorSensor;
  private static Sensor gGameRotationVectorSensor;

  private static class SensorListeners implements SensorEventListener {
    @Override
    public void onAccuracyChanged(final Sensor sensor, final int accuracy) {}

    @Override
    public void onSensorChanged(final SensorEvent s) {
      final int sensorType = s.sensor.getType();
      int halType = 0;
      float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
      // SensorEvent timestamp is in nanoseconds, Gecko expects microseconds.
      final long time = s.timestamp / 1000;

      switch (sensorType) {
        case Sensor.TYPE_ACCELEROMETER:
        case Sensor.TYPE_LINEAR_ACCELERATION:
        case SENSOR_TYPE_ORIENTATION:
          if (sensorType == Sensor.TYPE_ACCELEROMETER) {
            halType = SENSOR_ACCELERATION;
          } else if (sensorType == Sensor.TYPE_LINEAR_ACCELERATION) {
            halType = SENSOR_LINEAR_ACCELERATION;
          } else {
            halType = SENSOR_ORIENTATION;
          }
          x = s.values[0];
          y = s.values[1];
          z = s.values[2];
          break;

        case Sensor.TYPE_GYROSCOPE:
          halType = SENSOR_GYROSCOPE;
          x = (float) Math.toDegrees(s.values[0]);
          y = (float) Math.toDegrees(s.values[1]);
          z = (float) Math.toDegrees(s.values[2]);
          break;

        case Sensor.TYPE_LIGHT:
          halType = SENSOR_LIGHT;
          x = s.values[0];
          break;

        case Sensor.TYPE_ROTATION_VECTOR:
        case Sensor.TYPE_GAME_ROTATION_VECTOR: // API >= 18
          halType =
              (sensorType == Sensor.TYPE_ROTATION_VECTOR
                  ? SENSOR_ROTATION_VECTOR
                  : SENSOR_GAME_ROTATION_VECTOR);
          x = s.values[0];
          y = s.values[1];
          z = s.values[2];
          if (s.values.length >= 4) {
            w = s.values[3];
          } else {
            // s.values[3] was optional in API <= 18, so we need to compute it
            // The values form a unit quaternion, so we can compute the angle of
            // rotation purely based on the given 3 values.
            w =
                1.0f
                    - s.values[0] * s.values[0]
                    - s.values[1] * s.values[1]
                    - s.values[2] * s.values[2];
            w = (w > 0.0f) ? (float) Math.sqrt(w) : 0.0f;
          }
          break;
      }

      GeckoSensor.onSensorChanged(halType, x, y, z, w, time);
    }
  }

  // All methods are static
  private GeckoSensor() {}

  @SuppressWarnings("fallthrough")
  @WrapForJNI(calledFrom = "gecko")
  private static void enableSensor(final int aSensortype) {
    if (sListeners == null) {
      sListeners = new SensorListeners();
    }

    final SensorManager sm =
        (SensorManager)
            GeckoAppShell.getApplicationContext().getSystemService(Context.SENSOR_SERVICE);

    switch (aSensortype) {
      case SENSOR_GAME_ROTATION_VECTOR:
        if (gGameRotationVectorSensor == null) {
          gGameRotationVectorSensor = sm.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR);
        }
        if (gGameRotationVectorSensor != null) {
          // The game rotation vector sensor is available according to the system, but a few devices
          // seem to have a broken implementation (Bug 1356921). We also check the gyroscope sensor
          // for it.
          if (gGyroscopeSensor == null) {
            gGyroscopeSensor = sm.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
          }
          if (gGyroscopeSensor != null) {
            sm.registerListener(sListeners, gGameRotationVectorSensor, ORIENTATION_SENSOR_DELAY);
            break;
          }
          // This device has a broken game rotation vector sensor. Use rotation vector sensor if
          // available.
          gGameRotationVectorSensor = null;
        }
      // Fallthrough

      case SENSOR_ROTATION_VECTOR:
        if (gRotationVectorSensor == null) {
          gRotationVectorSensor = sm.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);
        }
        if (gRotationVectorSensor != null) {
          sm.registerListener(sListeners, gRotationVectorSensor, ORIENTATION_SENSOR_DELAY);
          break;
        }
      // Fallthrough

      case SENSOR_ORIENTATION:
        if (gOrientationSensor == null) {
          gOrientationSensor = sm.getDefaultSensor(SENSOR_TYPE_ORIENTATION);
        }
        if (gOrientationSensor != null) {
          sm.registerListener(sListeners, gOrientationSensor, ORIENTATION_SENSOR_DELAY);
        }
        break;

      case SENSOR_ACCELERATION:
        if (gAccelerometerSensor == null) {
          gAccelerometerSensor = sm.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        }
        if (gAccelerometerSensor != null) {
          sm.registerListener(sListeners, gAccelerometerSensor, MOTION_SENSOR_DELAY);
        }
        break;

      case SENSOR_LIGHT:
        if (gLightSensor == null) {
          gLightSensor = sm.getDefaultSensor(Sensor.TYPE_LIGHT);
        }
        if (gLightSensor != null) {
          sm.registerListener(sListeners, gLightSensor, SensorManager.SENSOR_DELAY_NORMAL);
        }
        break;

      case SENSOR_LINEAR_ACCELERATION:
        if (gLinearAccelerometerSensor == null) {
          gLinearAccelerometerSensor = sm.getDefaultSensor(Sensor.TYPE_LINEAR_ACCELERATION);
        }
        if (gLinearAccelerometerSensor != null) {
          sm.registerListener(sListeners, gLinearAccelerometerSensor, MOTION_SENSOR_DELAY);
        }
        break;

      case SENSOR_GYROSCOPE:
        if (gGyroscopeSensor == null) {
          gGyroscopeSensor = sm.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
        }
        if (gGyroscopeSensor != null) {
          sm.registerListener(sListeners, gGyroscopeSensor, MOTION_SENSOR_DELAY);
        }
        break;

      default:
        Log.w(LOGTAG, "Error! Can't enable unknown SENSOR type " + aSensortype);
    }
  }

  @SuppressWarnings("fallthrough")
  @WrapForJNI(calledFrom = "gecko")
  private static void disableSensor(final int aSensortype) {
    if (sListeners == null) {
      return;
    }

    final SensorManager sm =
        (SensorManager)
            GeckoAppShell.getApplicationContext().getSystemService(Context.SENSOR_SERVICE);

    switch (aSensortype) {
      case SENSOR_GAME_ROTATION_VECTOR:
        if (gGameRotationVectorSensor != null) {
          sm.unregisterListener(sListeners, gGameRotationVectorSensor);
          break;
        }
      // Fallthrough

      case SENSOR_ROTATION_VECTOR:
        if (gRotationVectorSensor != null) {
          sm.unregisterListener(sListeners, gRotationVectorSensor);
          break;
        }
      // Fallthrough

      case SENSOR_ORIENTATION:
        if (gOrientationSensor != null) {
          sm.unregisterListener(sListeners, gOrientationSensor);
        }
        break;

      case SENSOR_ACCELERATION:
        if (gAccelerometerSensor != null) {
          sm.unregisterListener(sListeners, gAccelerometerSensor);
        }
        break;

      case SENSOR_LIGHT:
        if (gLightSensor != null) {
          sm.unregisterListener(sListeners, gLightSensor);
        }
        break;

      case SENSOR_LINEAR_ACCELERATION:
        if (gLinearAccelerometerSensor != null) {
          sm.unregisterListener(sListeners, gLinearAccelerometerSensor);
        }
        break;

      case SENSOR_GYROSCOPE:
        if (gGyroscopeSensor != null) {
          sm.unregisterListener(sListeners, gGyroscopeSensor);
        }
        break;
      default:
        Log.w(LOGTAG, "Error! Can't disable unknown SENSOR type " + aSensortype);
    }
  }

  @WrapForJNI(calledFrom = "ui", dispatchTo = "gecko")
  /* package */ static native void onSensorChanged(
      int halType, float x, float y, float z, float w, long time);
}
