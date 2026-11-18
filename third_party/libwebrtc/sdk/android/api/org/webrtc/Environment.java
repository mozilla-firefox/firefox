/*
 *  Copyright 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import androidx.annotation.Nullable;

/** Wrapper for the webrtc::Environment native class. */
public final class Environment implements AutoCloseable {
  private final long webrtcEnv;

  /** Builder for {@link Environment}. */
  public static class Builder {
    public Builder setFieldTrials(String fieldTrials) {
      this.fieldTrials = fieldTrials;
      return this;
    }

    public Environment build() {
      return new Environment(this.fieldTrials);
    }

    private @Nullable String fieldTrials;
  }

  public static Builder builder() {
    return new Builder();
  }

  /** Returns non-owning non-null native pointer to the webrtc::Environment */
  public long ref() {
    return webrtcEnv;
  }

  /**
   * Returns current time as controlled by webrtcEnv. By default this is
   * normally same as System.nanoTime(), but call this function to be safe.
   */
  public long getCurrentTimeNanos() {
    return nativeCurrentTimeNanos(webrtcEnv);
  }

  @Override
  public void close() {
    nativeFree(webrtcEnv);
  }

  private Environment(@Nullable String fieldTrials) {
    this.webrtcEnv = nativeCreate(fieldTrials);
  }

  private static native long nativeCreate(@Nullable String fieldTrials);
  private static native long nativeCurrentTimeNanos(long webrtcEnv);
  private static native void nativeFree(long webrtcEnv);
}
