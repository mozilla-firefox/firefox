/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import org.jni_zero.CalledByNative;
import org.jni_zero.NativeMethods;

/** Java wrapper for a C++ TurnCustomizer. */
public class TurnCustomizer {
  private final NativeLifecycleLock lifecycleLock;

  public TurnCustomizer(long nativeTurnCustomizer) {
    this.lifecycleLock = new NativeLifecycleLock("TurnCustomizer", nativeTurnCustomizer);
  }

  public void dispose() {
    lifecycleLock.dispose(
        nativeTurnCustomizer -> TurnCustomizerJni.get().freeTurnCustomizer(nativeTurnCustomizer));
  }

  /** Return a pointer to webrtc::TurnCustomizer. */
  @CalledByNative
  long getNativeTurnCustomizer() {
    return lifecycleLock.getNativePointer();
  }

  @NativeMethods
  interface Natives {
    void freeTurnCustomizer(long turnCustomizer);
  }
}
