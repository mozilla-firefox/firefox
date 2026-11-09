/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.util;

import androidx.annotation.NonNull;
import org.mozilla.gecko.GeckoThread;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.mozglue.JNIObject;
import org.mozilla.geckoview.BuildConfig;

/**
 * Wrapper for nsIEventTarget, enabling seamless dispatch of java runnables to Gecko event queues.
 */
@WrapForJNI
public final class XPCOMEventTarget extends JNIObject implements IXPCOMEventTarget {
  @Override
  public void execute(final Runnable runnable) {
    dispatchNative(new JNIRunnable(runnable));
  }

  public static synchronized IXPCOMEventTarget mainThread() {
    if (mMainThread == null) {
      mMainThread = new AsyncProxy("main");
    }
    return mMainThread;
  }

  private static IXPCOMEventTarget mMainThread = null;

  public static synchronized IXPCOMEventTarget launcherThread() {
    if (mLauncherThread == null) {
      mLauncherThread = new AsyncProxy("launcher");
    }
    return mLauncherThread;
  }

  private static IXPCOMEventTarget mLauncherThread = null;

  /**
   * Runs the provided runnable on the launcher thread. If this method is called from the launcher
   * thread itself, the runnable will be executed immediately and synchronously.
   *
   * @param runnable the runnable to run
   */
  public static void runOnLauncherThread(@NonNull final Runnable runnable) {
    final IXPCOMEventTarget launcherThread = launcherThread();
    if (launcherThread.isOnCurrentThread()) {
      // We're already on the launcher thread, just execute the runnable
      runnable.run();
      return;
    }

    launcherThread.execute(runnable);
  }

  public static void assertOnLauncherThread() {
    if (BuildConfig.DEBUG_BUILD && !launcherThread().isOnCurrentThread()) {
      throw new AssertionError("Expected to be running on XPCOM launcher thread");
    }
  }

  public static void assertNotOnLauncherThread() {
    if (BuildConfig.DEBUG_BUILD && launcherThread().isOnCurrentThread()) {
      throw new AssertionError("Expected to not be running on XPCOM launcher thread");
    }
  }

  private static synchronized IXPCOMEventTarget getTarget(final String name) {
    if (name.equals("launcher")) {
      return mLauncherThread;
    } else if (name.equals("main")) {
      return mMainThread;
    } else {
      throw new RuntimeException("Attempt to assign to unknown thread named " + name);
    }
  }

  @WrapForJNI
  private static synchronized void setTarget(final String name, final XPCOMEventTarget target) {
    if (name.equals("main")) {
      mMainThread = target;
    } else if (name.equals("launcher")) {
      mLauncherThread = target;
    } else {
      throw new RuntimeException("Attempt to assign to unknown thread named " + name);
    }

    // Ensure that we see the right name in the Java debugger. We don't do this for mMainThread
    // because its name was already set (in this context, "main" is the GeckoThread).
    if (mMainThread != target) {
      target.execute(
          () -> {
            Thread.currentThread().setName(name);
          });
    }
  }

  @Override
  public native boolean isOnCurrentThread();

  private native void dispatchNative(final JNIRunnable runnable);

  @WrapForJNI
  private static synchronized void resolveAndDispatch(final String name, final Runnable runnable) {
    getTarget(name).execute(runnable);
  }

  private static native void resolveAndDispatchNative(final String name, final Runnable runnable);

  @Override
  protected native void disposeNative();

  @WrapForJNI
  private static final class JNIRunnable {
    JNIRunnable(final Runnable inner) {
      mInner = inner;
    }

    @WrapForJNI
    void run() {
      mInner.run();
    }

    private Runnable mInner;
  }

  private static final class AsyncProxy implements IXPCOMEventTarget {
    private String mTargetName;

    public AsyncProxy(final String targetName) {
      mTargetName = targetName;
    }

    @Override
    public void execute(final Runnable runnable) {
      final IXPCOMEventTarget target = XPCOMEventTarget.getTarget(mTargetName);

      if (target instanceof XPCOMEventTarget) {
        target.execute(runnable);
        return;
      }

      GeckoThread.queueNativeCallUntil(
          GeckoThread.State.JNI_READY,
          XPCOMEventTarget.class,
          "resolveAndDispatchNative",
          String.class,
          mTargetName,
          Runnable.class,
          runnable);
    }

    @Override
    public boolean isOnCurrentThread() {
      final IXPCOMEventTarget target = XPCOMEventTarget.getTarget(mTargetName);

      // If target is not yet a XPCOMEventTarget then JNI is not
      // initialized yet. If JNI is not initialized yet, then we cannot
      // possibly be running on a target with an XPCOMEventTarget.
      if (!(target instanceof XPCOMEventTarget)) {
        return false;
      }

      // Otherwise we have a real XPCOMEventTarget, so we can delegate
      // this call to it.
      return target.isOnCurrentThread();
    }
  }
}
