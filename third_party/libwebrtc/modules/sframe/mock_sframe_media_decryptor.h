/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_SFRAME_MOCK_SFRAME_MEDIA_DECRYPTOR_H_
#define MODULES_SFRAME_MOCK_SFRAME_MEDIA_DECRYPTOR_H_

#include <cstddef>
#include <cstdint>
#include <span>

#include "api/rtc_error.h"
#include "modules/sframe/sframe_media_decryptor_interface.h"
#include "test/gmock.h"

namespace webrtc {

class MockSframeMediaDecryptor : public SframeMediaDecryptorInterface {
 public:
  MOCK_METHOD(RTCError,
              AddDecryptionKey,
              (uint64_t key_id, std::span<const uint8_t> key_material),
              (override));
  MOCK_METHOD(RTCError, RemoveDecryptionKey, (uint64_t key_id), (override));
  MOCK_METHOD(SframeDecryptResult,
              Decrypt,
              (std::span<const uint8_t> encrypted_frame,
               std::span<const uint8_t> additional_data,
               std::span<uint8_t> frame),
              (override));
  MOCK_METHOD(size_t,
              GetMaxPlaintextByteSize,
              (size_t encrypted_frame_size),
              (override));
};

}  // namespace webrtc

#endif  // MODULES_SFRAME_MOCK_SFRAME_MEDIA_DECRYPTOR_H_
