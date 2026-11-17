/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.robolectric.Shadows.shadowOf;

import android.content.Context;
import android.os.Vibrator;
import java.lang.reflect.Method;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;

@RunWith(RobolectricTestRunner.class)
public class GeckoAppShellTest {
  private Context mContext;

  @Before
  public void setUp() {
    mContext = RuntimeEnvironment.getApplication();
    GeckoAppShell.setApplicationContext(mContext);
  }

  private static Method vibrateMethod(final Class<?>... parameterTypes) throws Exception {
    final Method vibrate = GeckoAppShell.class.getDeclaredMethod("vibrate", parameterTypes);
    vibrate.setAccessible(true);
    return vibrate;
  }

  @Test
  public void vibrate() throws Exception {
    final Method vibrate = vibrateMethod(long.class);
    final Vibrator vibrator = mContext.getSystemService(Vibrator.class);

    vibrate.invoke(null, 100L);
    assertTrue(shadowOf(vibrator).isVibrating());
    assertEquals(100L, shadowOf(vibrator).getMilliseconds());
  }

  @Test
  public void vibrateIgnoresInvalidDuration() throws Exception {
    vibrateMethod(long.class).invoke(null, 0L);
    assertFalse(shadowOf(mContext.getSystemService(Vibrator.class)).isVibrating());
  }

  @Test
  public void vibratePattern() throws Exception {
    final long[] pattern = {0, 100, 50, 200};

    vibrateMethod(long[].class, int.class).invoke(null, pattern, -1);
    final Vibrator vibrator = mContext.getSystemService(Vibrator.class);
    assertTrue(shadowOf(vibrator).isVibrating());
    assertArrayEquals(pattern, shadowOf(vibrator).getPattern());
    assertEquals(-1, shadowOf(vibrator).getRepeat());
  }
}
