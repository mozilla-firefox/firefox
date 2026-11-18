/*
 *  Copyright 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import org.jni_zero.NativeMethods;

/** Java wrapper for a C++ MediaSourceInterface. */
public class MediaSource {
  /** Tracks MediaSourceInterface.SourceState */
  public enum State {
    INITIALIZING,
    LIVE,
    ENDED,
    MUTED;

    @CalledByNative
    static State fromNativeIndex(int nativeIndex) {
      return values()[nativeIndex];
    }
  }

  final NativeLifecycleLock lifecycleLock;
  private final RefCountDelegate refCountDelegate;

  public MediaSource(long nativeSource) {
    refCountDelegate = new RefCountDelegate(() -> JniCommon.nativeReleaseRef(nativeSource));
    this.lifecycleLock = new NativeLifecycleLock("MediaSource", nativeSource);
  }

  public State state() {
    return lifecycleLock.call(nativeSource -> MediaSourceJni.get().getState(nativeSource));
  }

  public void dispose() {
    lifecycleLock.dispose(nativeSource -> refCountDelegate.release());
  }

  /** Returns a pointer to webrtc::MediaSourceInterface. */
  protected long getNativeMediaSource() {
    return lifecycleLock.getNativePointer();
  }

  /**
   * Runs code in {@code runnable} holding a reference to the media source. If the object has
   * already been released, does nothing.
   */
  void runWithReference(Runnable runnable) {
    if (refCountDelegate.safeRetain()) {
      try {
        runnable.run();
      } finally {
        refCountDelegate.release();
      }
    }
  }

  @NativeMethods
  interface Natives {
    State getState(long pointer);
  }
}
