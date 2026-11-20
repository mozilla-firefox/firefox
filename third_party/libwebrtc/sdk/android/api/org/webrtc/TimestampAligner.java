/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
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
 * The TimestampAligner class helps translating camera timestamps into the same timescale as is
 * used by webrtc. Some cameras have built in timestamping which is more accurate than
 * reading the system clock, but using a different epoch and unknown clock drift. Frame timestamps
 * in webrtc should use clock from the webrtc::Environment (usually the system monotonic time), and
 * this class provides a filter which lets us use the webrtc::Environment timescale, and at the same
 * time take advantage of higheraccuracy of the camera clock.
 * This class is a wrapper on top of webrtc::TimestampAligner.
 */
public class TimestampAligner {
  /**
   * Wrapper around webrtc::TimeNanos(). This is normally same as System.nanoTime(), but call this
   * function to be safe.
   */
  @Deprecated
  public static long getRtcTimeNanos() {
    return Environment.builder().build().getCurrentTimeNanos();
  }

  private final NativeLifecycleLock lifecycleLock;

  @Deprecated
  public TimestampAligner() {
    this(Environment.builder().build());
  }

  public TimestampAligner(Environment webrtcEnv) {
    this.lifecycleLock =
        new NativeLifecycleLock(
            "TimestampAligner", TimestampAlignerJni.get().createTimestampAligner(webrtcEnv.ref()));
  }

  /**
   * Translates camera timestamps to the same timescale as is used by webrtc::TimeNanos().
   * `cameraTimeNs` is assumed to be accurate, but with an unknown epoch and clock drift. Returns
   * the translated timestamp.
   */
  public long translateTimestamp(long cameraTimeNs) {
    return lifecycleLock.call(
        nativeTimestampAligner ->
            TimestampAlignerJni.get().translateTimestamp(nativeTimestampAligner, cameraTimeNs));
  }

  /** Dispose native timestamp aligner. */
  public void dispose() {
    lifecycleLock.dispose(
        nativeTimestampAligner ->
            TimestampAlignerJni.get().releaseTimestampAligner(nativeTimestampAligner));
  }

  @NativeMethods
  interface Natives {
    long createTimestampAligner(long webrtcEnvRef);

    void releaseTimestampAligner(long timestampAligner);

    long translateTimestamp(long timestampAligner, long cameraTimeNs);
  }
}
