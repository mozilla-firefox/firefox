/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.gecko.util;

import static org.junit.Assert.*;

import java.util.Locale;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class LocaleUtilsTest {
  @Test
  public void languageTagForAcceptLanguage() {
    assertEquals(LocaleUtils.getLanguageRegionLocale(Locale.forLanguageTag("zn-Hans-CN")), "zn-CN");
    assertEquals(LocaleUtils.getLanguageRegionLocale(Locale.forLanguageTag("zn-Hant-TW")), "zn-TW");

    // If builder is Java 17+, Locale.getLanguage doesn't repelace with old language code.
    // But we should keep this to make things understandable.
    assertEquals(LocaleUtils.getLanguageRegionLocale(Locale.forLanguageTag("id-ID")), "id-ID");
    assertEquals(LocaleUtils.getLanguageRegionLocale(Locale.forLanguageTag("in-ID")), "id-ID");

    assertEquals(LocaleUtils.getLanguageRegionLocale(Locale.forLanguageTag("yi-US")), "yi-US");
    assertEquals(LocaleUtils.getLanguageRegionLocale(Locale.forLanguageTag("ji-US")), "yi-US");

    assertEquals(LocaleUtils.getLanguageRegionLocale(Locale.forLanguageTag("he-IL")), "he-IL");
    assertEquals(LocaleUtils.getLanguageRegionLocale(Locale.forLanguageTag("iw-IL")), "he-IL");

    // Android 14 may add extension (Bug 1873578)
    assertEquals(
        LocaleUtils.getLanguageRegionLocale(Locale.forLanguageTag("en-US-u-fw-mon-mu-celsius")),
        "en-US");

    assertEquals(
        LocaleUtils.getLanguageRegionLocale(Locale.forLanguageTag("en-US-u-mu-celsius")), "en-US");
  }
}
