/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_SFRAME_MOCK_SFRAME_MEDIA_ENCRYPTOR_H_
#define MODULES_SFRAME_MOCK_SFRAME_MEDIA_ENCRYPTOR_H_

#include <cstddef>
#include <cstdint>
#include <span>

#include "api/rtc_error.h"
#include "modules/sframe/sframe_media_encryptor_interface.h"
#include "test/gmock.h"

namespace webrtc {

class MockSframeMediaEncryptor : public SframeMediaEncryptorInterface {
 public:
  MOCK_METHOD(RTCError,
              SetEncryptionKey,
              (uint64_t key_id, std::span<const uint8_t> key_material),
              (override));
  MOCK_METHOD(RTCErrorOr<size_t>,
              Encrypt,
              (std::span<const uint8_t> frame,
               std::span<const uint8_t> additional_data,
               std::span<uint8_t> encrypted_frame),
              (override));
  MOCK_METHOD(size_t,
              GetMaxCiphertextByteSize,
              (size_t frame_size),
              (override));
};

}  // namespace webrtc

#endif  // MODULES_SFRAME_MOCK_SFRAME_MEDIA_ENCRYPTOR_H_
