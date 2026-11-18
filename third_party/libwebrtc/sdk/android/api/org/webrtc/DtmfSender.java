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

/** Java wrapper for a C++ DtmfSenderInterface. */
public class DtmfSender {
  private final NativeLifecycleLock lifecycleLock;

  public DtmfSender(long nativeDtmfSender) {
    this.lifecycleLock = new NativeLifecycleLock("DtmfSender", nativeDtmfSender);
  }

  /**
   * @return true if this DtmfSender is capable of sending DTMF. Otherwise false.
   */
  public boolean canInsertDtmf() {
    return lifecycleLock.call(
        nativeDtmfSender -> DtmfSenderJni.get().canInsertDtmf(nativeDtmfSender));
  }

  /**
   * Queues a task that sends the provided DTMF tones.
   *
   * <p>If insertDtmf is called on the same object while an existing task for this object to
   * generate DTMF is still running, the previous task is canceled.
   *
   * @param tones This parameter is treated as a series of characters. The characters 0 through 9, A
   *     through D, #, and * generate the associated DTMF tones. The characters a to d are
   *     equivalent to A to D. The character ',' indicates a delay of 2 seconds before processing
   *     the next character in the tones parameter. Unrecognized characters are ignored.
   * @param duration Indicates the duration in ms to use for each character passed in the tones
   *     parameter. The duration cannot be more than 6000 or less than 70.
   * @param interToneGap Indicates the gap between tones in ms. Must be at least 50 ms but should be
   *     as short as possible.
   * @return true on success and false on failure.
   */
  public boolean insertDtmf(String tones, int duration, int interToneGap) {
    return lifecycleLock.call(
        nativeDtmfSender ->
            DtmfSenderJni.get().insertDtmf(nativeDtmfSender, tones, duration, interToneGap));
  }

  /**
   * @return The tones remaining to be played out
   */
  public String tones() {
    return lifecycleLock.call(nativeDtmfSender -> DtmfSenderJni.get().tones(nativeDtmfSender));
  }

  /**
   * @return The current tone duration value in ms. This value will be the value last set via the
   *     insertDtmf() method, or the default value of 100 ms if insertDtmf() was never called.
   */
  public int duration() {
    return lifecycleLock.call(nativeDtmfSender -> DtmfSenderJni.get().duration(nativeDtmfSender));
  }

  /**
   * @return The current value of the between-tone gap in ms. This value will be the value last set
   *     via the insertDtmf() method, or the default value of 50 ms if insertDtmf() was never
   *     called.
   */
  public int interToneGap() {
    return lifecycleLock.call(
        nativeDtmfSender -> DtmfSenderJni.get().interToneGap(nativeDtmfSender));
  }

  public void dispose() {
    lifecycleLock.dispose(JniCommon::nativeReleaseRef);
  }

  @NativeMethods
  interface Natives {
    boolean canInsertDtmf(long dtmfSender);

    boolean insertDtmf(long dtmfSender, String tones, int duration, int interToneGap);

    String tones(long dtmfSender);

    int duration(long dtmfSender);

    int interToneGap(long dtmfSender);
  }
}
