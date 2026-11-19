/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_SFRAME_SFRAME_MEDIA_DECRYPTOR_INTERFACE_H_
#define MODULES_SFRAME_SFRAME_MEDIA_DECRYPTOR_INTERFACE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

#include "api/ref_count.h"
#include "api/rtc_error.h"
#include "api/sframe/sframe_decryptor_interface.h"

namespace webrtc {

// Successful outcome of a Decrypt call.
struct SframeDecryptSuccess {
  // Number of plaintext bytes written to the output buffer.
  size_t bytes_written = 0;
};

// Failure outcome of a Decrypt call.
struct SframeDecryptFailure {
  static SframeDecryptFailure Authentication() {
    return {SframeDecryptErrorType::kAuthentication, std::nullopt};
  }

  static SframeDecryptFailure Syntax() {
    return {SframeDecryptErrorType::kSyntax, std::nullopt};
  }

  static SframeDecryptFailure KeyId(std::optional<uint64_t> key_id) {
    return {SframeDecryptErrorType::kKeyId, key_id};
  }

  SframeDecryptErrorType type;

  // Key id parsed from the Sframe header; set only when `type` is kKeyId.
  std::optional<uint64_t> key_id;
};

using SframeDecryptResult =
    std::variant<SframeDecryptSuccess, SframeDecryptFailure>;

// Internal media pipeline interface for Sframe decryption.
class SframeMediaDecryptorInterface : public RefCountInterface {
 public:
  virtual RTCError AddDecryptionKey(uint64_t key_id,
                                    std::span<const uint8_t> key_material) = 0;

  virtual RTCError RemoveDecryptionKey(uint64_t key_id) = 0;

  // Decrypts `encrypted_frame` into `frame`, authenticating it against
  // `additional_data`. The key id is read from the Sframe header, so the
  // matching decryption key must have been added beforehand. This is pure
  // crypto: it never invokes a callback and is codec-agnostic.
  virtual SframeDecryptResult Decrypt(std::span<const uint8_t> encrypted_frame,
                                      std::span<const uint8_t> additional_data,
                                      std::span<uint8_t> frame) = 0;

  virtual size_t GetMaxPlaintextByteSize(size_t encrypted_frame_size) = 0;

 protected:
  ~SframeMediaDecryptorInterface() override = default;
};

}  // namespace webrtc

#endif  // MODULES_SFRAME_SFRAME_MEDIA_DECRYPTOR_INTERFACE_H_
