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
  private static final IImageReaderFrameCallback sImageReaderCallback =
      new IImageReaderFrameCallback.Stub() {
        // Forward compositor requests to the native frame registry in RDD.
        @Override
        public void releaseFrame(
            final long handle, final long readerId, final long frameId, final boolean render) {
          if (!releaseImageReaderFrame(readerId, frameId, render) && render) {
            notifyImageReaderFrameReleaseFailed(handle, readerId, frameId);
          }
        }
      };

  private static void notifyImageReaderFrameReleaseFailed(
      final long handle, final long readerId, final long frameId) {
    final ISurfaceAllocator allocator;
    synchronized (SurfaceAllocator.class) {
      final GeckoSurface surface = sSurfaces.get(handle);
      if (surface == null || surface.getImageReaderId() != readerId || sAllocator == null) {
        return;
      }
      allocator = sAllocator;
    }
    try {
      allocator.notifyImageReaderFrameReleaseFailed(handle, frameId);
    } catch (final RemoteException | SecurityException e) {
      Log.w(LOGTAG, "Failed to report ImageReader frame release failure", e);
    }
  }

  /**
   * Consumes the RDD-side callback for this frame and requests rendering or discard. Returns false
   * if no callback remains or the release request fails.
   */
  @WrapForJNI
  private static native boolean releaseImageReaderFrame(
      long readerId, long frameId, boolean render);

  /**
   * Unregisters the reader and drops its pending native callbacks when the allocator connection is
   * lost.
   */
  @WrapForJNI
  private static native void discardImageReaderFrames(long readerId);

  /**
   * Requests the initial image from the compositor-owned reader through a synchronous Binder call.
   * Release the class lock before calling out so RDD can report a release failure while the
   * compositor waits for the image.
   */
  @WrapForJNI
  public static void maybeRenderImageReaderFirstFrame(final long handle, final long frameId) {
    final ISurfaceAllocator allocator;
    synchronized (SurfaceAllocator.class) {
      final GeckoSurface surface = sSurfaces.get(handle);
      if (surface == null || surface.getImageReaderId() == 0 || sAllocator == null) {
        return;
      }
      allocator = sAllocator;
    }
    // Keep this synchronous so OnSetCurrent waits for the initial image
    // acquisition attempt, which may time out.
    try {
      allocator.maybeRenderImageReaderFirstFrame(handle, frameId);
    } catch (final RemoteException | SecurityException e) {
      Log.w(LOGTAG, "Failed to render first ImageReader frame", e);
    }
  }

  /**
   * Acquires a compositor-owned ImageReader output Surface for the RDD decoder and registers its
   * frame callback. Tracks the returned Surface so allocator death can invalidate it.
   *
   * @return The remote output Surface, or null if allocation or the Binder connection fails.
   */
  @WrapForJNI
  public static synchronized GeckoSurface acquireImageReaderSurface() {
    try {
      ensureConnection();
      if (sAllocator == null) {
        return null;
      }
      final GeckoSurface surface = sAllocator.acquireImageReaderSurface(sImageReaderCallback);
      if (surface != null) {
        sSurfaces.put(surface.getHandle(), surface);
      }
      return surface;
    } catch (final RemoteException | SecurityException e) {
      Log.w(LOGTAG, "Failed to acquire ImageReader surface", e);
      return null;
    }
  }

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
                      final GeckoSurface surface = sSurfaces.valueAt(i);
                      surface.release();
                      if (surface.getImageReaderId() != 0) {
                        discardImageReaderFrames(surface.getImageReaderId());
                      }
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

    try {
      sAllocator.releaseSurface(surface.getHandle());
    } catch (final RemoteException | SecurityException e) {
      Log.w(LOGTAG, "Failed to release remote surface", e);
    }
  }
}
