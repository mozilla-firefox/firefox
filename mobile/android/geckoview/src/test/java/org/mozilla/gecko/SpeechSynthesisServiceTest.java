/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.robolectric.Shadows.shadowOf;

import android.os.Looper;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.shadows.ShadowTextToSpeech;

@RunWith(RobolectricTestRunner.class)
public class SpeechSynthesisServiceTest {
  @After
  public void tearDown() throws Exception {
    setTextToSpeech(null);
  }

  private static void setTextToSpeech(final TextToSpeech tts) throws Exception {
    final Field field = SpeechSynthesisService.class.getDeclaredField("sTTS");
    field.setAccessible(true);
    field.set(null, tts);
  }

  @Test
  public void voiceLocaleWithRegion() {
    final Locale locale = SpeechSynthesisService.getVoiceLocale("moz-tts:android:en_US");
    assertEquals(Locale.US, locale);
    assertEquals("eng", locale.getISO3Language());
  }

  @Test
  public void voiceLocaleWithScript() {
    assertEquals(
        Locale.forLanguageTag("zh-Hant-HK"),
        SpeechSynthesisService.getVoiceLocale("moz-tts:android:zh_HK_#Hant"));
    assertEquals(
        Locale.JAPAN,
        SpeechSynthesisService.getVoiceLocale("moz-tts:android:ja_JP_#u-ca-japanese"));
    assertEquals(
        Locale.forLanguageTag("zh-Hant-HK"),
        SpeechSynthesisService.getVoiceLocale("moz-tts:android:zh_HK_#Hant_u-ca-chinese"));
    assertEquals(
        Locale.forLanguageTag("zh-TW"),
        SpeechSynthesisService.getVoiceLocale("moz-tts:android:zh_TW_#x-ab"));
  }

  @Test
  public void voiceLocaleWithVariant() {
    assertEquals(
        Locale.forLanguageTag("ca-ES-VALENCIA"),
        SpeechSynthesisService.getVoiceLocale("moz-tts:android:ca_ES_VALENCIA"));
  }

  @Test
  public void voiceLocaleWithoutRegion() {
    assertEquals(Locale.FRENCH, SpeechSynthesisService.getVoiceLocale("moz-tts:android:fr"));
  }

  @Test
  public void speak() throws Exception {
    final TextToSpeech tts = new TextToSpeech(RuntimeEnvironment.getApplication(), status -> {});
    ShadowTextToSpeech.addLanguageAvailability(Locale.US);
    final List<String> doneUtterances = new ArrayList<>();
    tts.setOnUtteranceProgressListener(
        new UtteranceProgressListener() {
          @Override
          public void onStart(final String utteranceId) {}

          @Override
          public void onDone(final String utteranceId) {
            doneUtterances.add(utteranceId);
          }

          @Override
          @SuppressWarnings("deprecation")
          public void onError(final String utteranceId) {}
        });
    setTextToSpeech(tts);

    final String utteranceId =
        SpeechSynthesisService.speak("moz-tts:android:en_US", "hello", 1.0f, 1.0f, 0.5f);

    assertNotNull(utteranceId);
    assertEquals(Locale.US, shadowOf(tts).getCurrentLanguage());
    assertEquals("hello", shadowOf(tts).getLastSpokenText());
    assertEquals(TextToSpeech.QUEUE_FLUSH, shadowOf(tts).getQueueMode());
    assertEquals(
        0.5f,
        ShadowTextToSpeech.getLastParams().getFloat(TextToSpeech.Engine.KEY_PARAM_VOLUME),
        0.0f);

    shadowOf(Looper.getMainLooper()).idle();
    assertEquals(Collections.singletonList(utteranceId), doneUtterances);
  }
}
