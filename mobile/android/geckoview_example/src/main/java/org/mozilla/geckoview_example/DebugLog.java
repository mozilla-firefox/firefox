/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview_example;

import android.util.Log;

/** Debug and info logging that only reaches logcat in debug builds. */
final class DebugLog {
  private DebugLog() {}

  static void d(final String tag, final String msg) {
    if (BuildConfig.DEBUG) {
      Log.d(tag, msg);
    }
  }

  static void i(final String tag, final String msg) {
    if (BuildConfig.DEBUG) {
      Log.i(tag, msg);
    }
  }
}
