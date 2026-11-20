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
 * A combined video decoder that falls back on a secondary decoder if the primary decoder fails.
 */
public class VideoDecoderFallback extends WrappedNativeVideoDecoder {
  private final VideoDecoder fallback;
  private final VideoDecoder primary;

  public VideoDecoderFallback(VideoDecoder fallback, VideoDecoder primary) {
    this.fallback = fallback;
    this.primary = primary;
  }

  @Override
  public long createNative(long webrtcEnvRef) {
    return VideoDecoderFallbackJni.get().create(webrtcEnvRef, fallback, primary);
  }

  @NativeMethods
  interface Natives {
    long create(long webrtcEnvRef, VideoDecoder fallback, VideoDecoder primary);
  }
}
