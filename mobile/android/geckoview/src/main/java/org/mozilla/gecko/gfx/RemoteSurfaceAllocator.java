/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.gfx;

import android.os.IBinder;
import android.os.RemoteException;
import android.util.LongSparseArray;
import java.util.concurrent.atomic.AtomicInteger;
import org.mozilla.gecko.GeckoThread;

public final class RemoteSurfaceAllocator extends ISurfaceAllocator.Stub
    implements IBinder.DeathRecipient {
  private static final String LOGTAG = "RemoteSurfaceAllocator";

  /// Unique ID identifying the process this instance belongs to, which must be 0 for the parent
  /// process. This is used so that following a GPU process shutdown, surface handles allocated by
  /// the new compositor process (either a new GPU process, or the parent process) do not clash with
  /// surface handles allocated by a previous GPU process.
  private final int mAllocatorId;

  /// Monotonically increasing counter used to generate unique handles for each SurfaceTexture by
  /// combining with mAllocatorId.
  private static AtomicInteger sNextHandle = new AtomicInteger(1);

  /// List of Surface handles owned by this instance.
  private final LongSparseArray<Boolean> mOwnedHandles = new LongSparseArray<Boolean>();

  /// Whether the client is still connected to this allocator.
  private boolean mClientConnected = true;

  /**
   * Retrieves the allocator instance for the provided client.
   *
   * @param allocatorId A unique ID identifying the process this instance belongs to, which must be
   *     0 for a parent process instance.
   * @param client An IBinder identifying the process for which we will be allocating surfaces.
   */
  public static RemoteSurfaceAllocator create(final int allocatorId, final IBinder client) {
    if (GeckoThread.isStateAtLeast(GeckoThread.State.JNI_READY)) {
      try {
        return new RemoteSurfaceAllocator(allocatorId, client);
      } catch (final RemoteException ignored) {
      }
    }
    return null;
  }

  private RemoteSurfaceAllocator(final int allocatorId, final IBinder client)
      throws RemoteException {
    mAllocatorId = allocatorId;
    client.linkToDeath(this, 0);
  }

  @Override
  public synchronized GeckoSurface acquireSurface(
      final int width, final int height, final boolean singleBufferMode) {
    if (!mClientConnected) {
      return null;
    }

    final long handle = ((long) mAllocatorId << 32) | sNextHandle.getAndIncrement();
    final GeckoSurfaceTexture gst = GeckoSurfaceTexture.acquire(singleBufferMode, handle);

    if (gst == null) {
      return null;
    }

    if (width > 0 && height > 0) {
      gst.setDefaultBufferSize(width, height);
    }

    mOwnedHandles.put(handle, true);
    return new GeckoSurface(gst);
  }

  @Override
  public synchronized void releaseSurface(final long handle) {
    ensureOwned(handle);
    mOwnedHandles.remove(handle);

    final GeckoSurfaceTexture gst = GeckoSurfaceTexture.lookup(handle);
    if (gst != null) {
      gst.decrementUse();
    }
  }

  @Override
  public synchronized void binderDied() {
    mClientConnected = false;
    for (int i = 0; i < mOwnedHandles.size(); i++) {
      final GeckoSurfaceTexture gst = GeckoSurfaceTexture.lookup(mOwnedHandles.keyAt(i));
      if (gst != null) {
        gst.decrementUse();
      }
    }
    mOwnedHandles.clear();
  }

  private void ensureOwned(final long handle) {
    if (mOwnedHandles.indexOfKey(handle) < 0) {
      throw new SecurityException("Surface handle is not owned by this allocator session");
    }
  }
}
