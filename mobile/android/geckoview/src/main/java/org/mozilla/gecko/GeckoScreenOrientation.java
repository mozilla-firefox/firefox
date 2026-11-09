/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import static android.content.res.Configuration.ORIENTATION_LANDSCAPE;
import static android.content.res.Configuration.ORIENTATION_PORTRAIT;

import android.content.Context;
import android.graphics.Rect;
import android.util.Log;
import android.view.Surface;
import java.util.ArrayList;
import java.util.List;
import org.mozilla.gecko.util.ThreadUtils;

/*
 * Updates, locks and unlocks the screen orientation.
 *
 * Note: Replaces the OnOrientationChangeListener to avoid redundant rotation
 * event handling.
 */
public class GeckoScreenOrientation {
  private static final String LOGTAG = "GeckoScreenOrientation";

  // Make sure that any change in hal/HalScreenConfiguration.h happens here too.
  public enum ScreenOrientation {
    NONE(0),
    PORTRAIT_PRIMARY(1 << 0),
    PORTRAIT_SECONDARY(1 << 1),
    PORTRAIT(PORTRAIT_PRIMARY.value | PORTRAIT_SECONDARY.value),
    LANDSCAPE_PRIMARY(1 << 2),
    LANDSCAPE_SECONDARY(1 << 3),
    LANDSCAPE(LANDSCAPE_PRIMARY.value | LANDSCAPE_SECONDARY.value),
    ANY(
        PORTRAIT_PRIMARY.value
            | PORTRAIT_SECONDARY.value
            | LANDSCAPE_PRIMARY.value
            | LANDSCAPE_SECONDARY.value),
    DEFAULT(1 << 4);

    public final short value;

    ScreenOrientation(final int value) {
      this.value = (short) value;
    }

    private static final ScreenOrientation[] sValues = ScreenOrientation.values();

    public static ScreenOrientation get(final int value) {
      for (final ScreenOrientation orient : sValues) {
        if (orient.value == value) {
          return orient;
        }
      }
      return NONE;
    }
  }

  // Singleton instance.
  private static GeckoScreenOrientation sInstance;
  // Default rotation, used when device rotation is unknown.
  private static final int DEFAULT_ROTATION = Surface.ROTATION_0;
  // Last updated screen orientation with Gecko value space.
  private ScreenOrientation mScreenOrientation = ScreenOrientation.PORTRAIT_PRIMARY;

  public interface OrientationChangeListener {
    void onScreenOrientationChanged(ScreenOrientation newOrientation);
  }

  private final List<OrientationChangeListener> mListeners;

  public static GeckoScreenOrientation getInstance() {
    if (sInstance == null) {
      sInstance = new GeckoScreenOrientation();
    }
    return sInstance;
  }

  private GeckoScreenOrientation() {
    mListeners = new ArrayList<>();
    update();
  }

  /**
   * Add a listener that will be notified when the screen orientation has changed.
   *
   * @param aListener the listener to add
   */
  public void addListener(final OrientationChangeListener aListener) {
    ThreadUtils.assertOnUiThread();
    mListeners.add(aListener);
  }

  /**
   * Remove a OrientationChangeListener again.
   *
   * @param aListener the listener to remove
   */
  public void removeListener(final OrientationChangeListener aListener) {
    ThreadUtils.assertOnUiThread();
    mListeners.remove(aListener);
  }

  /**
   * Update screen orientation by retrieving orientation and rotation via GeckoAppShell.
   *
   * @return Whether the screen orientation has changed.
   */
  public boolean update() {
    // Check whether we have the application context for fenix/a-c unit test.
    final Context appContext = GeckoAppShell.getApplicationContext();
    if (appContext == null) {
      return false;
    }
    final Rect rect = GeckoAppShell.getScreenSizeIgnoreOverride();
    final int orientation =
        rect.width() >= rect.height() ? ORIENTATION_LANDSCAPE : ORIENTATION_PORTRAIT;
    return update(getScreenOrientation(orientation, getRotation()));
  }

  /**
   * Update screen orientation given the Android orientation by retrieving rotation via
   * GeckoAppShell.
   *
   * @param aAndroidOrientation Android screen orientation from Configuration.orientation.
   * @return Whether the screen orientation has changed.
   */
  public boolean update(final int aAndroidOrientation) {
    return update(getScreenOrientation(aAndroidOrientation, getRotation()));
  }

  /**
   * Update screen orientation given the screen orientation.
   *
   * @param aScreenOrientation Gecko screen orientation based on Android orientation and rotation.
   * @return Whether the screen orientation has changed.
   */
  public synchronized boolean update(final ScreenOrientation aScreenOrientation) {
    // Gecko expects a definite screen orientation, so we default to the
    // primary orientations.
    final ScreenOrientation screenOrientation;
    if ((aScreenOrientation.value & ScreenOrientation.PORTRAIT_PRIMARY.value) != 0) {
      screenOrientation = ScreenOrientation.PORTRAIT_PRIMARY;
    } else if ((aScreenOrientation.value & ScreenOrientation.PORTRAIT_SECONDARY.value) != 0) {
      screenOrientation = ScreenOrientation.PORTRAIT_SECONDARY;
    } else if ((aScreenOrientation.value & ScreenOrientation.LANDSCAPE_PRIMARY.value) != 0) {
      screenOrientation = ScreenOrientation.LANDSCAPE_PRIMARY;
    } else if ((aScreenOrientation.value & ScreenOrientation.LANDSCAPE_SECONDARY.value) != 0) {
      screenOrientation = ScreenOrientation.LANDSCAPE_SECONDARY;
    } else {
      screenOrientation = ScreenOrientation.PORTRAIT_PRIMARY;
    }
    if (mScreenOrientation == screenOrientation) {
      return false;
    }
    mScreenOrientation = screenOrientation;
    Log.d(LOGTAG, "updating to new orientation " + mScreenOrientation);
    notifyListeners(mScreenOrientation);
    ScreenManagerHelper.refreshScreenInfo();
    return true;
  }

  private void notifyListeners(final ScreenOrientation newOrientation) {
    final Runnable notifier =
        new Runnable() {
          @Override
          public void run() {
            for (final OrientationChangeListener listener : mListeners) {
              listener.onScreenOrientationChanged(newOrientation);
            }
          }
        };

    if (ThreadUtils.isOnUiThread()) {
      notifier.run();
    } else {
      ThreadUtils.runOnUiThread(notifier);
    }
  }

  /**
   * @return The Gecko screen orientation derived from Android orientation and rotation.
   */
  public ScreenOrientation getScreenOrientation() {
    return mScreenOrientation;
  }

  /**
   * Combine the Android orientation and rotation to the Gecko orientation.
   *
   * @param aAndroidOrientation Android orientation from Configuration.orientation.
   * @param aRotation Device rotation from Display.getRotation().
   * @return Gecko screen orientation.
   */
  private ScreenOrientation getScreenOrientation(
      final int aAndroidOrientation, final int aRotation) {
    final boolean isPrimary = aRotation == Surface.ROTATION_0 || aRotation == Surface.ROTATION_90;
    if (aAndroidOrientation == ORIENTATION_PORTRAIT) {
      if (isPrimary) {
        // Non-rotated portrait device or landscape device rotated
        // to primary portrait mode counter-clockwise.
        return ScreenOrientation.PORTRAIT_PRIMARY;
      }
      return ScreenOrientation.PORTRAIT_SECONDARY;
    }
    if (aAndroidOrientation == ORIENTATION_LANDSCAPE) {
      if (isPrimary) {
        // Non-rotated landscape device or portrait device rotated
        // to primary landscape mode counter-clockwise.
        return ScreenOrientation.LANDSCAPE_PRIMARY;
      }
      return ScreenOrientation.LANDSCAPE_SECONDARY;
    }
    return ScreenOrientation.NONE;
  }

  /**
   * @return Device rotation converted to an angle.
   */
  public short getAngle() {
    switch (getRotation()) {
      case Surface.ROTATION_0:
        return 0;
      case Surface.ROTATION_90:
        return 90;
      case Surface.ROTATION_180:
        return 180;
      case Surface.ROTATION_270:
        return 270;
      default:
        Log.w(LOGTAG, "getAngle: unexpected rotation value");
        return 0;
    }
  }

  /**
   * @return Device rotation.
   */
  private int getRotation() {
    return GeckoAppShell.getRotation();
  }
}
