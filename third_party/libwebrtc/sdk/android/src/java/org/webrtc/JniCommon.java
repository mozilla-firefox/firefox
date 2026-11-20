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

import java.nio.ByteBuffer;
import org.jni_zero.NativeMethods;

/** Class with static JNI helper functions that are used in many places. */
public class JniCommon {
  /** Functions to increment/decrement an RefCountInterface pointer. */
  public static void nativeAddRef(long refCountedPointer) {
    JniCommonJni.get().addRef(refCountedPointer);
  }

  public static void nativeReleaseRef(long refCountedPointer) {
    JniCommonJni.get().releaseRef(refCountedPointer);
  }

  public static ByteBuffer nativeAllocateByteBuffer(int size) {
    return JniCommonJni.get().allocateByteBuffer(size);
  }

  public static void nativeFreeByteBuffer(ByteBuffer buffer) {
    JniCommonJni.get().freeByteBuffer(buffer);
  }

  @NativeMethods
  interface Natives {
    void addRef(long refCountedPointer);

    void releaseRef(long refCountedPointer);

    ByteBuffer allocateByteBuffer(int size);

    void freeByteBuffer(ByteBuffer buffer);
  }
}
