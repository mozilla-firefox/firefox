/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.gfx;

import android.os.Binder;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;
import android.util.LongSparseArray;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.process.GeckoProcessManager;
import org.mozilla.gecko.process.GeckoServiceChildProcess;

/* package */ final class SurfaceAllocator {
  private static final String LOGTAG = "SurfaceAllocator";

  private static ISurfaceAllocator sAllocator;
  private static final IBinder sClient = new Binder();

  // Keep a reference to all allocated Surfaces, so that we can release them if we lose the
  // connection to the allocator service.
  private static final LongSparseArray<GeckoSurface> sSurfaces =
      new LongSparseArray<GeckoSurface>();

  private static synchronized void ensureConnection() {
    if (sAllocator != null) {
      return;
    }

    try {
      if (GeckoAppShell.isParentProcess()) {
        sAllocator = GeckoProcessManager.getInstance().getSurfaceAllocator(sClient);
      } else {
        sAllocator = GeckoServiceChildProcess.getSurfaceAllocator(sClient);
      }

      if (sAllocator == null) {
        Log.w(LOGTAG, "Failed to connect to RemoteSurfaceAllocator");
        return;
      }
      sAllocator
          .asBinder()
          .linkToDeath(
              new IBinder.DeathRecipient() {
                @Override
                public void binderDied() {
                  Log.w(LOGTAG, "RemoteSurfaceAllocator died");
                  synchronized (SurfaceAllocator.class) {
                    // Our connection to the remote allocator has died, so all our surfaces are
                    // invalid.  Release them all now. When their owners attempt to render in to
                    // them they can detect they have been released and allocate new ones instead.
                    for (int i = 0; i < sSurfaces.size(); i++) {
                      sSurfaces.valueAt(i).release();
                    }
                    sSurfaces.clear();
                    sAllocator = null;
                  }
                }
              },
              0);
    } catch (final RemoteException e) {
      Log.w(LOGTAG, "Failed to connect to RemoteSurfaceAllocator", e);
      sAllocator = null;
    }
  }

  @WrapForJNI
  public static synchronized GeckoSurface acquireSurface(
      final int width, final int height, final boolean singleBufferMode) {
    try {
      ensureConnection();

      if (sAllocator == null) {
        Log.w(LOGTAG, "Failed to acquire GeckoSurface: not connected");
        return null;
      }

      final GeckoSurface surface = sAllocator.acquireSurface(width, height, singleBufferMode);
      if (surface == null) {
        Log.w(LOGTAG, "Failed to acquire GeckoSurface: RemoteSurfaceAllocator returned null");
        return null;
      }
      sSurfaces.put(surface.getHandle(), surface);

      return surface;
    } catch (final RemoteException e) {
      Log.w(LOGTAG, "Failed to acquire GeckoSurface", e);
      return null;
    }
  }

  @WrapForJNI
  public static synchronized void disposeSurface(final GeckoSurface surface) {
    // If the surface has already been released (probably due to losing connection to the remote
    // allocator) then there is nothing to do here.
    if (surface.isReleased()) {
      return;
    }

    sSurfaces.remove(surface.getHandle());

    // Release our Surface
    surface.release();

    if (sAllocator == null) {
      return;
    }

    // Release the SurfaceTexture on the other side. If we have lost connection then do nothing, as
    // there is nothing on the other side to release.
    try {
      if (sAllocator != null) {
        sAllocator.releaseSurface(surface.getHandle());
      }
    } catch (final RemoteException e) {
      Log.w(LOGTAG, "Failed to release surface texture", e);
    }
  }
}
