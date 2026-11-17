/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
import static org.robolectric.Shadows.shadowOf;

import android.content.Context;
import android.content.ContextWrapper;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Vibrator;
import java.lang.reflect.Method;
import java.util.Arrays;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.AudioDeviceInfoBuilder;

@RunWith(RobolectricTestRunner.class)
public class GeckoAppShellTest {
  private Context mContext;

  @Before
  public void setUp() {
    mContext = RuntimeEnvironment.getApplication();
    GeckoAppShell.setApplicationContext(mContext);
  }

  @After
  public void tearDown() {
    GeckoAppShell.setCommunicationAudioModeOn(false);
  }

  private static AudioDeviceInfo device(final int type) {
    return AudioDeviceInfoBuilder.newBuilder().setType(type).build();
  }

  @SuppressWarnings("deprecation")
  private static boolean isBluetoothScoOn(final AudioManager audioManager) {
    return audioManager.isBluetoothScoOn();
  }

  private static Method vibrateMethod(final Class<?>... parameterTypes) throws Exception {
    final Method vibrate = GeckoAppShell.class.getDeclaredMethod("vibrate", parameterTypes);
    vibrate.setAccessible(true);
    return vibrate;
  }

  @Test
  public void communicationModeRoutesToBluetoothSco() {
    final AudioManager audioManager = mContext.getSystemService(AudioManager.class);
    final AudioDeviceInfo speaker = device(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER);
    final AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO);
    shadowOf(audioManager).setAvailableCommunicationDevices(Arrays.asList(speaker, sco));

    GeckoAppShell.setCommunicationAudioModeOn(true);
    assertEquals(sco, audioManager.getCommunicationDevice());

    GeckoAppShell.setCommunicationAudioModeOn(false);
    assertNull(audioManager.getCommunicationDevice());
  }

  @Test
  public void communicationModeOffClearsAfterRejectedRepeatRequest() {
    final AudioManager audioManager = mContext.getSystemService(AudioManager.class);
    final AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO);
    shadowOf(audioManager).setAvailableCommunicationDevices(Arrays.asList(sco));

    GeckoAppShell.setCommunicationAudioModeOn(true);
    shadowOf(audioManager).lockCommunicationDevice(true);
    try {
      GeckoAppShell.setCommunicationAudioModeOn(true);
    } finally {
      shadowOf(audioManager).lockCommunicationDevice(false);
    }

    GeckoAppShell.setCommunicationAudioModeOn(false);
    assertNull(audioManager.getCommunicationDevice());
  }

  @Test
  public void communicationModeIgnoresRejectedDevice() {
    final AudioManager audioManager = spy(mContext.getSystemService(AudioManager.class));
    final AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO);
    doReturn(Arrays.asList(sco)).when(audioManager).getAvailableCommunicationDevices();
    doThrow(new IllegalArgumentException()).when(audioManager).setCommunicationDevice(sco);
    doReturn(sco).when(audioManager).getCommunicationDevice();
    GeckoAppShell.setApplicationContext(
        new ContextWrapper(mContext) {
          @Override
          public Object getSystemService(final String name) {
            return Context.AUDIO_SERVICE.equals(name) ? audioManager : super.getSystemService(name);
          }
        });

    GeckoAppShell.setCommunicationAudioModeOn(true);
    GeckoAppShell.setCommunicationAudioModeOn(false);
    verify(audioManager, never()).clearCommunicationDevice();
  }

  @Test
  public void communicationModeOffKeepsLaterAppRoute() {
    final AudioManager audioManager = mContext.getSystemService(AudioManager.class);
    final AudioDeviceInfo speaker = device(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER);
    final AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO);
    shadowOf(audioManager).setAvailableCommunicationDevices(Arrays.asList(speaker, sco));

    GeckoAppShell.setCommunicationAudioModeOn(true);
    audioManager.setCommunicationDevice(speaker);

    GeckoAppShell.setCommunicationAudioModeOn(false);
    assertEquals(speaker, audioManager.getCommunicationDevice());
  }

  @Test
  public void communicationModeOffKeepsOtherRoute() {
    final AudioManager audioManager = mContext.getSystemService(AudioManager.class);
    final AudioDeviceInfo speaker = device(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER);
    shadowOf(audioManager).setAvailableCommunicationDevices(Arrays.asList(speaker));
    audioManager.setCommunicationDevice(speaker);

    GeckoAppShell.setCommunicationAudioModeOn(false);
    assertEquals(speaker, audioManager.getCommunicationDevice());
  }

  @Test
  public void communicationModeWithoutBluetoothSco() {
    final AudioManager audioManager = mContext.getSystemService(AudioManager.class);
    shadowOf(audioManager)
        .setAvailableCommunicationDevices(
            Arrays.asList(device(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER)));

    GeckoAppShell.setCommunicationAudioModeOn(true);
    assertNull(audioManager.getCommunicationDevice());
  }

  @Test
  @Config(sdk = 30)
  public void communicationModeUsesBluetoothScoBeforeApi31() {
    final AudioManager audioManager = mContext.getSystemService(AudioManager.class);

    GeckoAppShell.setCommunicationAudioModeOn(true);
    assertTrue(isBluetoothScoOn(audioManager));

    GeckoAppShell.setCommunicationAudioModeOn(false);
    assertFalse(isBluetoothScoOn(audioManager));
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
