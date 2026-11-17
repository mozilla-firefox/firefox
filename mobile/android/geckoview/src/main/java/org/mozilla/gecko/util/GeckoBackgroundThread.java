/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.util;

import android.os.Handler;
import android.os.Looper;

final class GeckoBackgroundThread extends Thread {
  private static final String LOOPER_NAME = "GeckoBackgroundThread";

  // Guarded by 'GeckoBackgroundThread.class'.
  private static Handler handler;
  private static Thread thread;

  // The initial Runnable to run on the new thread. Its purpose
  // is to avoid us having to wait for the new thread to start.
  private Runnable mInitialRunnable;

  // Singleton, so private constructor.
  private GeckoBackgroundThread(final Runnable initialRunnable) {
    mInitialRunnable = initialRunnable;
  }

  @Override
  public void run() {
    setName(LOOPER_NAME);
    Looper.prepare();

    synchronized (GeckoBackgroundThread.class) {
      handler = new Handler(Looper.myLooper());
      GeckoBackgroundThread.class.notifyAll();
    }

    if (mInitialRunnable != null) {
      mInitialRunnable.run();
      mInitialRunnable = null;
    }

    Looper.loop();
  }

  private static void startThread(final Runnable initialRunnable) {
    thread = new GeckoBackgroundThread(initialRunnable);
    thread.setDaemon(true);
    thread.start();
  }

  // Get a Handler for a looper thread, or create one if it doesn't yet exist.
  /*package*/ static synchronized Handler getHandler() {
    if (thread == null) {
      startThread(null);
    }

    while (handler == null) {
      try {
        GeckoBackgroundThread.class.wait();
      } catch (final InterruptedException e) {
      }
    }
    return handler;
  }

  /*package*/ static synchronized void post(final Runnable runnable) {
    if (thread == null) {
      startThread(runnable);
      return;
    }
    getHandler().post(runnable);
  }
}
