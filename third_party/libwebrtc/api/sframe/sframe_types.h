/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_SFRAME_SFRAME_TYPES_H_
#define API_SFRAME_SFRAME_TYPES_H_

namespace webrtc {

enum class SframeMode {
  kPerFrame,
  kPerPacket,
};

enum class SframeCipherSuite {
  kAes128CtrHmacSha256_80,
  kAes128CtrHmacSha256_64,
  kAes128CtrHmacSha256_32,
  kAes128GcmSha256_128,
  kAes256GcmSha512_128,
  kAes256CtrHmacSha512_80,
  kAes256CtrHmacSha512_64,
  kAes256CtrHmacSha512_32,
};

}  // namespace webrtc

#endif  // API_SFRAME_SFRAME_TYPES_H_
