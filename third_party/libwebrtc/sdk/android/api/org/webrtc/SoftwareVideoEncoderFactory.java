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

import androidx.annotation.Nullable;
import java.util.List;
import org.jni_zero.NativeMethods;

public class SoftwareVideoEncoderFactory implements VideoEncoderFactory {
  private static final String TAG = "SoftwareVideoEncoderFactory";

  private final long nativeFactory;

  public SoftwareVideoEncoderFactory() {
    this.nativeFactory = SoftwareVideoEncoderFactoryJni.get().createFactory();
  }

  @Nullable
  @Override
  public VideoEncoder createEncoder(VideoCodecInfo info) {
    if (!SoftwareVideoEncoderFactoryJni.get().isSupported(nativeFactory, info)) {
      Logging.w(TAG, "Trying to create encoder for unsupported format. " + info);
      return null;
    }

    return new WrappedNativeVideoEncoder() {
      @Override
      public long createNative(long webrtcEnvRef) {
        return SoftwareVideoEncoderFactoryJni.get().create(nativeFactory, webrtcEnvRef, info);
      }

      @Override
      public boolean isHardwareEncoder() {
        return false;
      }
    };
  }

  @Override
  public VideoCodecInfo[] getSupportedCodecs() {
    return SoftwareVideoEncoderFactoryJni.get()
        .getSupportedCodecs(nativeFactory)
        .toArray(new VideoCodecInfo[0]);
  }

  @NativeMethods
  interface Natives {
    long createFactory();

    boolean isSupported(long factory, VideoCodecInfo info);

    long create(long factory, long webrtcEnvRef, VideoCodecInfo info);

    List<VideoCodecInfo> getSupportedCodecs(long factory);
  }
}
