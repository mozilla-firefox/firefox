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

import org.jni_zero.NativeMethods;

/**
 * This class contains the Java glue code for JNI generation of VideoDecoder.
 */
class VideoDecoderWrapper {
  /**
   * Forwards decoded frames to the native VideoDecoderWrapper until the callback is invalidated.
   * Frames delivered after that are dropped.
   */
  static class NativeDecoderCallback implements VideoDecoder.Callback {
    private final NativeLifecycleLock lifecycleLock;

    NativeDecoderCallback(long nativeDecoder) {
      lifecycleLock = new NativeLifecycleLock("VideoDecoderWrapper", nativeDecoder);
    }

    @Override
    public void onDecodedFrame(VideoFrame frame, Integer decodeTimeMs, Integer qp) {
      lifecycleLock.runIfAlive(
          (long nativeDecoder)
              -> VideoDecoderWrapperJni.get().onDecodedFrame(
                  nativeDecoder, frame, decodeTimeMs, qp));
    }

    // Called by the native VideoDecoderWrapper. Blocks until an ongoing onDecodedFrame() call, if
    // any, has returned. Frames delivered after this call are dropped.
    @CalledByNative
    void invalidate() {
      lifecycleLock.dispose((long nativeDecoder) -> {});
    }
  }

  @CalledByNative
  static NativeDecoderCallback createDecoderCallback(final long nativeDecoder) {
    return new NativeDecoderCallback(nativeDecoder);
  }

  @NativeMethods
  interface Natives {
    void onDecodedFrame(
        long nativeVideoDecoderWrapper, VideoFrame frame, Integer decodeTimeMs, Integer qp);
  }
}
