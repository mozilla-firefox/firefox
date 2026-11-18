/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.content.Context;
import android.os.Bundle;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.util.Log;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.util.ThreadUtils;

public class SpeechSynthesisService {
  private static final String LOGTAG = "GeckoSpeechSynthesis";
  private static final String VOICE_URI_PREFIX = "moz-tts:android:";
  // Object type is used to make it easier to remove android.speech dependencies using Proguard.
  private static Object sTTS;

  @WrapForJNI(calledFrom = "gecko")
  public static void initSynth() {
    initSynthInternal();
  }

  // Extra internal method to make it easier to remove android.speech dependencies using Proguard.
  private static void initSynthInternal() {
    if (sTTS != null) {
      return;
    }

    final Context ctx = GeckoAppShell.getApplicationContext();

    sTTS =
        new TextToSpeech(
            ctx,
            new TextToSpeech.OnInitListener() {
              @Override
              public void onInit(final int status) {
                if (status != TextToSpeech.SUCCESS) {
                  Log.w(LOGTAG, "Failed to initialize TextToSpeech");
                  return;
                }

                setUtteranceListener();
                registerVoicesByLocale();
              }
            });
  }

  private static TextToSpeech getTTS() {
    return (TextToSpeech) sTTS;
  }

  private static void registerVoicesByLocale() {
    ThreadUtils.postToBackgroundThread(
        new Runnable() {
          @Override
          public void run() {
            final TextToSpeech tss = getTTS();
            if (tss == null) {
              Log.w(LOGTAG, "TextToSpeech is not initialized");
              return;
            }
            @SuppressWarnings("deprecation")
            final Locale defaultLocale = tss.getDefaultLanguage();
            for (final Locale locale : getAvailableLanguages()) {
              @SuppressWarnings("deprecation")
              final Set<String> features = tss.getFeatures(locale);
              @SuppressWarnings("deprecation")
              final boolean isLocal =
                  features != null
                      && features.contains(TextToSpeech.Engine.KEY_FEATURE_EMBEDDED_SYNTHESIS);
              final String localeStr = locale.toString();
              registerVoice(
                  VOICE_URI_PREFIX + localeStr,
                  locale.getDisplayName(),
                  localeStr.replace("_", "-"),
                  !isLocal,
                  defaultLocale == locale);
            }
            doneRegisteringVoices();
          }
        });
  }

  private static Set<Locale> getAvailableLanguages() {
    final Set<Locale> availableLanguages = getTTS().getAvailableLanguages();
    if (availableLanguages != null) {
      return availableLanguages;
    }

    final Set<Locale> locales = new HashSet<Locale>();
    for (final Locale locale : Locale.getAvailableLocales()) {
      if (locale.getVariant().isEmpty() && getTTS().isLanguageAvailable(locale) > 0) {
        locales.add(locale);
      }
    }

    return locales;
  }

  @WrapForJNI(dispatchTo = "gecko")
  private static native void registerVoice(
      String uri, String name, String locale, boolean isNetwork, boolean isDefault);

  @WrapForJNI(dispatchTo = "gecko")
  private static native void doneRegisteringVoices();

  @WrapForJNI(calledFrom = "gecko")
  public static String speak(
      final String uri,
      final String text,
      final float rate,
      final float pitch,
      final float volume) {
    final AtomicBoolean result = new AtomicBoolean(false);
    final String utteranceId = UUID.randomUUID().toString();
    speakInternal(uri, text, rate, pitch, volume, utteranceId, result);
    return result.get() ? utteranceId : null;
  }

  // Extra internal method to make it easier to remove android.speech dependencies using Proguard.
  private static void speakInternal(
      final String uri,
      final String text,
      final float rate,
      final float pitch,
      final float volume,
      final String utteranceId,
      final AtomicBoolean result) {
    if (sTTS == null) {
      Log.w(LOGTAG, "TextToSpeech is not initialized");
      return;
    }

    final Bundle params = new Bundle();
    params.putFloat(TextToSpeech.Engine.KEY_PARAM_VOLUME, volume);
    final TextToSpeech tss = (TextToSpeech) sTTS;
    tss.setLanguage(getVoiceLocale(uri));
    tss.setSpeechRate(rate);
    tss.setPitch(pitch);
    final int speakRes = tss.speak(text, TextToSpeech.QUEUE_FLUSH, params, utteranceId);
    result.set(speakRes == TextToSpeech.SUCCESS);
  }

  /* package */ static Locale getVoiceLocale(final String uri) {
    final String[] parts = uri.substring(VOICE_URI_PREFIX.length()).split("_#", 2);
    final Locale.Builder builder =
        new Locale.Builder().setLocale(Locale.forLanguageTag(parts[0].replace('_', '-')));
    if (parts.length > 1) {
      final String script = parts[1].split("[_-]", 2)[0];
      if (script.length() == 4) {
        builder.setScript(script);
      }
    }
    return builder.build();
  }

  private static void setUtteranceListener() {
    if (sTTS == null) {
      Log.w(LOGTAG, "TextToSpeech is not initialized");
      return;
    }

    getTTS()
        .setOnUtteranceProgressListener(
            new UtteranceProgressListener() {
              @Override
              public void onDone(final String utteranceId) {
                dispatchEnd(utteranceId);
              }

              @Override
              @SuppressWarnings("deprecation")
              public void onError(final String utteranceId) {
                dispatchError(utteranceId);
              }

              @Override
              public void onStart(final String utteranceId) {
                dispatchStart(utteranceId);
              }

              @Override
              public void onStop(final String utteranceId, final boolean interrupted) {
                if (interrupted) {
                  dispatchEnd(utteranceId);
                } else {
                  // utterance isn't started yet.
                  dispatchError(utteranceId);
                }
              }

              public void onRangeStart(
                  final String utteranceId, final int start, final int end, final int frame) {
                dispatchBoundary(utteranceId, start, end);
              }
            });
  }

  @WrapForJNI(dispatchTo = "gecko")
  private static native void dispatchStart(String utteranceId);

  @WrapForJNI(dispatchTo = "gecko")
  private static native void dispatchEnd(String utteranceId);

  @WrapForJNI(dispatchTo = "gecko")
  private static native void dispatchError(String utteranceId);

  @WrapForJNI(dispatchTo = "gecko")
  private static native void dispatchBoundary(String utteranceId, int start, int end);

  @WrapForJNI(calledFrom = "gecko")
  public static void stop() {
    stopInternal();
  }

  // Extra internal method to make it easier to remove android.speech dependencies using Proguard.
  private static void stopInternal() {
    if (sTTS == null) {
      Log.w(LOGTAG, "TextToSpeech is not initialized");
      return;
    }

    getTTS().stop();
  }
}
