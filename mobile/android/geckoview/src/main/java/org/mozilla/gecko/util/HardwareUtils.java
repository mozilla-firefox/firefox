/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.util;

import android.content.Context;
import android.content.res.Configuration;

public final class HardwareUtils {
  private static final String LOGTAG = "GeckoHardwareUtils";

  private static volatile boolean sInited;

  // These are all set once, during init.
  private static volatile boolean sIsLargeTablet;
  private static volatile boolean sIsSmallTablet;

  private HardwareUtils() {}

  public static synchronized void init(final Context context) {
    if (sInited) {
      return;
    }

    // Pre-populate common flags from the context.
    final int screenLayoutSize =
        context.getResources().getConfiguration().screenLayout
            & Configuration.SCREENLAYOUT_SIZE_MASK;
    if (screenLayoutSize == Configuration.SCREENLAYOUT_SIZE_XLARGE) {
      sIsLargeTablet = true;
    } else if (screenLayoutSize == Configuration.SCREENLAYOUT_SIZE_LARGE) {
      sIsSmallTablet = true;
    }

    sInited = true;
  }

  public static boolean isTablet(final Context context) {
    if (!sInited) {
      init(context);
    }
    return sIsLargeTablet || sIsSmallTablet;
  }
}
