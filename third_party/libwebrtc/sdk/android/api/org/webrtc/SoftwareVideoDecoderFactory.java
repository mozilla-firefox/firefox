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

public class SoftwareVideoDecoderFactory implements VideoDecoderFactory {
  private static final String TAG = "SoftwareVideoDecoderFactory";

  private final long nativeFactory;

  public SoftwareVideoDecoderFactory() {
    this.nativeFactory = SoftwareVideoDecoderFactoryJni.get().createFactory();
  }

  @Nullable
  @Override
  public VideoDecoder createDecoder(VideoCodecInfo info) {
    if (!SoftwareVideoDecoderFactoryJni.get().isSupported(nativeFactory, info)) {
      Logging.w(TAG, "Trying to create decoder for unsupported format. " + info);
      return null;
    }
    return new WrappedNativeVideoDecoder() {
      @Override
      public long createNative(long webrtcEnvRef) {
        return SoftwareVideoDecoderFactoryJni.get().create(nativeFactory, webrtcEnvRef, info);
      }
    };
  }

  @Override
  public VideoCodecInfo[] getSupportedCodecs() {
    return SoftwareVideoDecoderFactoryJni.get()
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
