/*
 *  Copyright 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import androidx.annotation.Nullable;

/** Java wrapper for a C++ MediaStreamTrackInterface. */
public class MediaStreamTrack {
  public static final String AUDIO_TRACK_KIND = "audio";
  public static final String VIDEO_TRACK_KIND = "video";

  /** Tracks MediaStreamTrackInterface.TrackState */
  public enum State {
    LIVE,
    ENDED;

    @CalledByNative
    static State fromNativeIndex(int nativeIndex) {
      return values()[nativeIndex];
    }
  }

  // Must be kept in sync with webrtc::MediaType.
  public enum MediaType {
    MEDIA_TYPE_AUDIO(0),
    MEDIA_TYPE_VIDEO(1);

    private final int nativeIndex;

    private MediaType(int nativeIndex) {
      this.nativeIndex = nativeIndex;
    }

    @CalledByNative
    int getNative() {
      return nativeIndex;
    }

    @CalledByNative
    static MediaType fromNativeIndex(int nativeIndex) {
      for (MediaType type : MediaType.values()) {
        if (type.getNative() == nativeIndex) {
          return type;
        }
      }
      throw new IllegalArgumentException("Unknown native media type: " + nativeIndex);
    }
  }

  /** Factory method to create an AudioTrack or VideoTrack subclass. */
  static @Nullable MediaStreamTrack createMediaStreamTrack(long nativeTrack) {
    if (nativeTrack == 0) {
      return null;
    }
    String trackKind = nativeGetKind(nativeTrack);
    if (trackKind.equals(AUDIO_TRACK_KIND)) {
      return new AudioTrack(nativeTrack);
    } else if (trackKind.equals(VIDEO_TRACK_KIND)) {
      return new VideoTrack(nativeTrack);
    } else {
      return null;
    }
  }

  final NativeLifecycleLock lifecycleLock;

  public MediaStreamTrack(long nativeTrack) {
    if (nativeTrack == 0) {
      throw new IllegalArgumentException("nativeTrack may not be null");
    }
    this.lifecycleLock = new NativeLifecycleLock("MediaStreamTrack", nativeTrack);
  }

  public String id() {
    return lifecycleLock.call(nativeTrack -> nativeGetId(nativeTrack));
  }

  public String kind() {
    return lifecycleLock.call(nativeTrack -> nativeGetKind(nativeTrack));
  }

  public boolean enabled() {
    return lifecycleLock.call(nativeTrack -> nativeGetEnabled(nativeTrack));
  }

  public boolean setEnabled(boolean enable) {
    return lifecycleLock.call(
        nativeTrack -> nativeSetEnabled(nativeTrack, enable));
  }

  public State state() {
    return lifecycleLock.call(nativeTrack -> nativeGetState(nativeTrack));
  }

  public void dispose() {
    lifecycleLock.dispose(JniCommon::nativeReleaseRef);
  }

  long getNativeMediaStreamTrack() {
    return lifecycleLock.getNativePointer();
  }

  private static native String nativeGetId(long track);
  private static native String nativeGetKind(long track);
  private static native boolean nativeGetEnabled(long track);
  private static native boolean nativeSetEnabled(long track, boolean enabled);
  private static native State nativeGetState(long track);
}
