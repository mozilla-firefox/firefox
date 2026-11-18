/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import org.jni_zero.NativeMethods;

public class LibvpxVp9Encoder extends WrappedNativeVideoEncoder {
  @Override
  public long createNative(long webrtcEnvRef) {
    return LibvpxVp9EncoderJni.get().create(webrtcEnvRef);
  }

  @Override
  public boolean isHardwareEncoder() {
    return false;
  }

  @NativeMethods
  interface Natives {
    long create(long webrtcEnvRef);

    boolean isSupported();
  }
}
