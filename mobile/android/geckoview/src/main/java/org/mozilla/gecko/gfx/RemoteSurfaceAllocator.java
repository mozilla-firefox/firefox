/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.gfx;

import android.os.IBinder;
import android.os.RemoteException;
import android.util.LongSparseArray;
import android.view.Surface;
import java.util.concurrent.atomic.AtomicInteger;
import org.mozilla.gecko.GeckoThread;
import org.mozilla.gecko.annotation.WrapForJNI;

public final class RemoteSurfaceAllocator extends ISurfaceAllocator.Stub
    implements IBinder.DeathRecipient {
  private static final String LOGTAG = "RemoteSurfaceAllocator";

  // The high bit separates remote ImageReader IDs from locally generated IDs.
  // Keep this value in sync with GpuProcessAndroidImageReaderId::kRemoteIdBit.
  private static final long REMOTE_IMAGE_READER_ID_BIT = Long.MIN_VALUE;

  /// Unique ID identifying the process this instance belongs to, which must be 0 for the parent
  /// process. This is used so that following a GPU process shutdown, surface handles allocated by
  /// the new compositor process (either a new GPU process, or the parent process) do not clash with
  /// surface handles allocated by a previous GPU process.
  private final int mAllocatorId;

  private static final LongSparseArray<IImageReaderFrameCallback> sImageReaderCallbacks =
      new LongSparseArray<>();

  /**
   * Creates and registers a native ImageReader in the GPU or parent process for this handle.
   * Returns its output Surface, or null if creation fails.
   */
  @WrapForJNI
  private static native Surface createImageReaderSurface(long handle);

  /** Removes the native allocation and disconnects the reader from its remote frame callback. */
  @WrapForJNI
  private static native void disposeImageReaderSurface(long handle);

  /**
   * Requests the reader's initial image in native code. Call without the allocator instance lock so
   * a failed frame release can be reported while native code waits for the image.
   */
  @WrapForJNI
  private static native void maybeRenderImageReaderFirstFrameNative(long handle, long frameId);

  @WrapForJNI
  private static native void notifyImageReaderFrameReleaseFailedNative(long handle, long frameId);

  /**
   * Queues a request to release a decoded buffer for rendering or discard in RDD. Copies the
   * callback under the registry lock, then makes the Binder call without that lock. Returns whether
   * the request was sent. RDD reports render failures asynchronously.
   */
  @WrapForJNI
  private static boolean releaseImageReaderFrame(
      final long handle, final long readerId, final long frameId, final boolean render) {
    final IImageReaderFrameCallback callback;
    synchronized (sImageReaderCallbacks) {
      callback = sImageReaderCallbacks.get(handle);
    }
    if (callback == null) {
      return false;
    }
    try {
      callback.releaseFrame(handle, readerId, frameId, render);
      return true;
    } catch (final RemoteException | SecurityException e) {
      return false;
    }
  }

  /**
   * Validates that this client owns the ImageReader handle, then requests its initial image.
   * Releases the allocator lock before waiting for the initial image.
   */
  @Override
  public void maybeRenderImageReaderFirstFrame(final long handle, final long frameId) {
    synchronized (this) {
      if (!mClientConnected) {
        return;
      }
      ensureOwned(handle);
      if (mOwnedHandles.get(handle) != AllocationType.IMAGE_READER) {
        return;
      }
    }
    maybeRenderImageReaderFirstFrameNative(handle, frameId);
  }

  @Override
  public void notifyImageReaderFrameReleaseFailed(final long handle, final long frameId) {
    synchronized (this) {
      if (!mClientConnected || mOwnedHandles.get(handle) != AllocationType.IMAGE_READER) {
        return;
      }
    }
    notifyImageReaderFrameReleaseFailedNative(handle, frameId);
  }

  /**
   * Creates a compositor-side ImageReader allocation and associates it with the client's callback.
   * The returned GeckoSurface carries both the allocation handle and the native reader ID.
   *
   * @return The decoder's output Surface, or null if the client or allocation is unavailable.
   */
  @Override
  public synchronized GeckoSurface acquireImageReaderSurface(
      final IImageReaderFrameCallback callback) {
    if (!mClientConnected || callback == null) {
      return null;
    }
    final long handle = ((long) mAllocatorId << 32) | sNextHandle.getAndIncrement();
    final Surface surface = createImageReaderSurface(handle);
    if (surface == null) {
      return null;
    }
    synchronized (sImageReaderCallbacks) {
      sImageReaderCallbacks.put(handle, callback);
    }
    mOwnedHandles.put(handle, AllocationType.IMAGE_READER);
    // The allocation handle is unchanged; only the reader ID is tagged as remote.
    return new GeckoSurface(surface, handle, handle | REMOTE_IMAGE_READER_ID_BIT);
  }

  /**
   * Releases an owned allocation according to its type: disconnects an ImageReader and removes its
   * callback, or decrements a SurfaceTexture's use count. The caller holds the allocator instance
   * lock and removes the ownership entry after release, or clears all entries after client death.
   */
  private void releaseOwnedSurface(final long handle) {
    if (mOwnedHandles.get(handle) == AllocationType.IMAGE_READER) {
      synchronized (sImageReaderCallbacks) {
        sImageReaderCallbacks.remove(handle);
      }
      disposeImageReaderSurface(handle);
      return;
    }
    final GeckoSurfaceTexture gst = GeckoSurfaceTexture.lookup(handle);
    if (gst != null) {
      gst.decrementUse();
    }
  }

  /// Monotonically increasing counter used to generate unique handles for SurfaceTexture and
  /// ImageReader allocations by combining with mAllocatorId.
  private static AtomicInteger sNextHandle = new AtomicInteger(1);

  private enum AllocationType {
    SURFACE_TEXTURE,
    IMAGE_READER
  }

  private final LongSparseArray<AllocationType> mOwnedHandles = new LongSparseArray<>();

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

    mOwnedHandles.put(handle, AllocationType.SURFACE_TEXTURE);
    return new GeckoSurface(gst);
  }

  @Override
  public synchronized void releaseSurface(final long handle) {
    ensureOwned(handle);
    releaseOwnedSurface(handle);
    mOwnedHandles.remove(handle);
  }

  @Override
  public synchronized void binderDied() {
    mClientConnected = false;
    for (int i = 0; i < mOwnedHandles.size(); i++) {
      releaseOwnedSurface(mOwnedHandles.keyAt(i));
    }
    mOwnedHandles.clear();
  }

  private void ensureOwned(final long handle) {
    if (mOwnedHandles.indexOfKey(handle) < 0) {
      throw new SecurityException("Surface handle is not owned by this allocator session");
    }
  }
}
