/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.process;

import android.content.ComponentCallbacks2;
import android.content.res.Configuration;
import android.util.Log;
import androidx.annotation.NonNull;
import org.mozilla.gecko.GeckoAppShell;

public class MemoryController implements ComponentCallbacks2 {
  private static final String LOGTAG = "MemoryController";
  private long mLastLowMemoryNotificationTime = 0;

  // Allowed elapsed time between full GCs while under constant memory pressure
  private static final long LOW_MEMORY_ONGOING_RESET_TIME_MS = 10000;

  private static final int LOW = 0;
  private static final int MODERATE = 1;
  private static final int CRITICAL = 2;

  @SuppressWarnings("deprecation")
  private int memoryLevelFromTrim(final int level) {
    if (level >= ComponentCallbacks2.TRIM_MEMORY_COMPLETE
        || level == ComponentCallbacks2.TRIM_MEMORY_RUNNING_CRITICAL) {
      return CRITICAL;
    } else if (level >= ComponentCallbacks2.TRIM_MEMORY_BACKGROUND) {
      return MODERATE;
    }
    return LOW;
  }

  public void onTrimMemory(final int level) {
    Log.i(LOGTAG, "onTrimMemory(" + level + ")");
    onMemoryNotification(memoryLevelFromTrim(level));
  }

  @Override
  public void onConfigurationChanged(final @NonNull Configuration newConfig) {}

  @SuppressWarnings("deprecation")
  public void onLowMemory() {
    Log.i(LOGTAG, "onLowMemory");
    onMemoryNotification(CRITICAL);
  }

  private void onMemoryNotification(final int level) {
    if (level == LOW) {
      // The trim level is too low to be actionable
      return;
    }

    // See nsIMemory.idl for descriptions of the various arguments to the "memory-pressure"
    // observer.
    final String observerArg;

    final long currentNotificationTime = System.currentTimeMillis();
    if (level == CRITICAL
        || (currentNotificationTime - mLastLowMemoryNotificationTime)
            >= LOW_MEMORY_ONGOING_RESET_TIME_MS) {
      // We do a full "low-memory" notification for both new and last-ditch onTrimMemory requests.
      observerArg = "low-memory";
      mLastLowMemoryNotificationTime = currentNotificationTime;
    } else {
      // If it has been less than ten seconds since the last time we sent a "low-memory"
      // notification, we send a "low-memory-ongoing" notification instead.
      // This prevents Gecko from re-doing full GC's repeatedly over and over in succession,
      // as they are expensive and quickly result in diminishing returns.
      observerArg = "low-memory-ongoing";
    }

    GeckoAppShell.notifyObservers("memory-pressure", observerArg);
  }
}
