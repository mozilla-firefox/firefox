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

// Explicit imports necessary for JNI generation.
import androidx.annotation.Nullable;
import org.jni_zero.NativeMethods;

/**
 * This class contains the Java glue code for JNI generation of VideoEncoder.
 */
class VideoEncoderWrapper {
  @CalledByNative
  static boolean getScalingSettingsOn(VideoEncoder.ScalingSettings scalingSettings) {
    return scalingSettings.on;
  }

  @Nullable
  @CalledByNative
  static Integer getScalingSettingsLow(VideoEncoder.ScalingSettings scalingSettings) {
    return scalingSettings.low;
  }

  @Nullable
  @CalledByNative
  static Integer getScalingSettingsHigh(VideoEncoder.ScalingSettings scalingSettings) {
    return scalingSettings.high;
  }

  /**
   * Forwards encoded frames to the native VideoEncoderWrapper until the callback is invalidated.
   * Frames delivered after that are dropped.
   */
  static class NativeEncoderCallback implements VideoEncoder.Callback {
    private final NativeLifecycleLock lifecycleLock;

    NativeEncoderCallback(long nativeEncoder) {
      lifecycleLock = new NativeLifecycleLock("VideoEncoderWrapper", nativeEncoder);
    }

    @Override
    public void onEncodedFrame(EncodedImage frame, VideoEncoder.CodecSpecificInfo info) {
      lifecycleLock.runIfAlive(
          (long nativeEncoder) ->
              VideoEncoderWrapperJni.get().onEncodedFrame(nativeEncoder, frame));
    }

    // Called by the native VideoEncoderWrapper. Blocks until an ongoing onEncodedFrame() call, if
    // any, has returned. Frames delivered after this call are dropped.
    @CalledByNative
    void invalidate() {
      lifecycleLock.dispose((long nativeEncoder) -> {});
    }
  }

  @CalledByNative
  static NativeEncoderCallback createEncoderCallback(final long nativeEncoder) {
    return new NativeEncoderCallback(nativeEncoder);
  }

  @NativeMethods
  interface Natives {
    void onEncodedFrame(long nativeVideoEncoderWrapper, EncodedImage frame);
  }
}
