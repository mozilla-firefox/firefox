/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.process;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import android.os.RemoteException;
import android.util.Log;
import java.io.IOException;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.GeckoThread;
import org.mozilla.gecko.IGeckoEditableChild;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.gfx.ICompositorSurfaceManager;
import org.mozilla.gecko.gfx.ISurfaceAllocator;
import org.mozilla.gecko.util.ThreadUtils;

public class GeckoServiceChildProcess extends Service {
  private static final String LOGTAG = "ServiceChildProcess";

  private static IProcessManager sProcessManager;
  private static String sOwnerProcessId;

  private enum ProcessState {
    NEW,
    CREATED,
    BOUND,
    STARTED,
    DESTROYED,
  }

  // Keep track of the process state to ensure we don't reuse the process
  private static ProcessState sState = ProcessState.NEW;

  @WrapForJNI(calledFrom = "gecko")
  private static void getEditableParent(
      final IGeckoEditableChild child, final long contentId, final long tabId) {
    try {
      sProcessManager.getEditableParent(child, contentId, tabId);
    } catch (final RemoteException e) {
      Log.e(LOGTAG, "Cannot get editable", e);
    }
  }

  @Override
  public void onCreate() {
    super.onCreate();
    Log.i(LOGTAG, "onCreate");

    if (sState != ProcessState.NEW) {
      // We don't support reusing processes, and this could get us in a really weird state,
      // so let's throw here.
      throw new RuntimeException(
          String.format("Cannot reuse process %s: %s", getClass().getSimpleName(), sState));
    }
    sState = ProcessState.CREATED;

    GeckoAppShell.setApplicationContext(getApplicationContext());
    GeckoThread.launch(); // Preload Gecko.
  }

  protected static class ChildProcessBinder extends IChildProcess.Stub {
    @Override
    public int getPid() {
      return Process.myPid();
    }

    @Override
    public int start(
        final IProcessManager procMan,
        final String mainProcessId,
        final String[] args,
        final Bundle extras,
        final int flags,
        final String userSerialNumber,
        final String crashHandlerService,
        final ParcelFileDescriptor[] pfds) {

      synchronized (GeckoServiceChildProcess.class) {
        if (sOwnerProcessId != null && !sOwnerProcessId.equals(mainProcessId)) {
          Log.w(
              LOGTAG,
              "This process belongs to a different GeckoRuntime owner: "
                  + sOwnerProcessId
                  + " process: "
                  + mainProcessId);
          // We need to close the File Descriptors here otherwise we will leak them causing a
          // shutdown hang.
          closeFds(pfds);
          return IChildProcess.STARTED_BUSY;
        }
        if (sProcessManager != null) {
          Log.e(LOGTAG, "Child process already started");
          closeFds(pfds);
          return IChildProcess.STARTED_FAIL;
        }
        sProcessManager = procMan;
        sOwnerProcessId = mainProcessId;
      }

      final int[] fds = new int[pfds.length];
      for (int i = 0; i < pfds.length; ++i) {
        fds[i] = pfds[i].detachFd();
      }

      ThreadUtils.runOnUiThread(
          new Runnable() {
            @Override
            public void run() {
              if (crashHandlerService != null) {
                try {
                  @SuppressWarnings("unchecked")
                  final Class<? extends Service> crashHandler =
                      (Class<? extends Service>) Class.forName(crashHandlerService);

                  // Native crashes are reported through pipes, so we don't have to
                  // do anything special for that.
                  GeckoAppShell.setCrashHandlerService(crashHandler);
                  GeckoAppShell.ensureCrashHandling(crashHandler);
                } catch (final ClassNotFoundException e) {
                  Log.w(LOGTAG, "Couldn't find crash handler service " + crashHandlerService);
                }
              }

              final GeckoThread.InitInfo info =
                  GeckoThread.InitInfo.builder()
                      .args(args)
                      .extras(extras)
                      .flags(flags | GeckoThread.FLAG_CHILD)
                      .userSerialNumber(userSerialNumber)
                      .fds(fds)
                      .build();

              if (GeckoThread.init(info)) {
                GeckoThread.launch();
              }
            }
          });
      sState = ProcessState.STARTED;
      return IChildProcess.STARTED_OK;
    }

    @Override
    public void crash() {
      GeckoThread.crash();
    }

    @Override
    public ICompositorSurfaceManager getCompositorSurfaceManager() {
      Log.e(
          LOGTAG, "Invalid call to IChildProcess.getCompositorSurfaceManager for non-GPU process");
      throw new AssertionError(
          "Invalid call to IChildProcess.getCompositorSurfaceManager for non-GPU process.");
    }

    @Override
    public ISurfaceAllocator getSurfaceAllocator(final int allocatorId) {
      Log.e(LOGTAG, "Invalid call to IChildProcess.getSurfaceAllocator for non-GPU process");
      throw new AssertionError(
          "Invalid call to IChildProcess.getSurfaceAllocator for non-GPU process.");
    }

    private void closeFds(final ParcelFileDescriptor[] pfds) {
      for (final ParcelFileDescriptor pfd : pfds) {
        try {
          pfd.close();
        } catch (final IOException ex) {
          Log.d(LOGTAG, "Failed to close File Descriptors.", ex);
        }
      }
    }
  }

  protected Binder createBinder() {
    return new ChildProcessBinder();
  }

  private final Binder mBinder = createBinder();

  @Override
  public void onDestroy() {
    Log.i(LOGTAG, "Destroying GeckoServiceChildProcess");
    sState = ProcessState.DESTROYED;
    System.exit(0);
  }

  @Override
  public IBinder onBind(final Intent intent) {
    // Calling stopSelf ensures that whenever the client unbinds the process dies immediately.
    stopSelf();
    sState = ProcessState.BOUND;
    return mBinder;
  }

  /**
   * Returns the surface allocator interface that should be used by this process to allocate
   * Surfaces, for consumption in either the GPU process or parent process.
   */
  public static ISurfaceAllocator getSurfaceAllocator() throws RemoteException {
    return sProcessManager.getSurfaceAllocator();
  }
}
