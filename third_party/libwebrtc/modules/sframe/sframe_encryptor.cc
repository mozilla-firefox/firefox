/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/sframe/sframe_encryptor.h"

#include <cstddef>
#include <cstdint>
#include <span>

#include "api/make_ref_counted.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/sframe/sframe_types.h"
#include "modules/sframe/sframe_context_factory.h"
#include "rtc_base/race_checker.h"
#include "third_party/sframe/src/include/sframe/result.h"
#include "third_party/sframe/src/include/sframe/sframe.h"

namespace webrtc {

scoped_refptr<SframeEncryptor> SframeEncryptor::Create(
    SframeCipherSuite cipher_suite) {
  return make_ref_counted<SframeEncryptor>(cipher_suite);
}

SframeEncryptor::SframeEncryptor(SframeCipherSuite cipher_suite)
    : context_(CreateSframeContext(cipher_suite)) {}

SframeEncryptor::~SframeEncryptor() = default;

RTCError SframeEncryptor::SetEncryptionKey(
    uint64_t key_id,
    std::span<const uint8_t> key_material) {
  RTC_DCHECK_RUNS_SERIALIZED(&access_checker_);
  sframe::Result<void> result = context_->add_key(
      key_id, sframe::KeyUsage::protect,
      sframe::input_bytes(key_material.data(), key_material.size()));
  if (result.is_err()) {
    RTCError error = RTCError::InternalError("Failed to set encryption key");
    if (const char* message = result.error().message()) {
      error.string_builder() << ": " << message;
    }
    return error;
  }

  if (active_key_id_ && *active_key_id_ != key_id) {
    context_->remove_key(*active_key_id_);
  }

  active_key_id_ = key_id;

  return RTCError::OK();
}

RTCErrorOr<size_t> SframeEncryptor::Encrypt(
    std::span<const uint8_t> frame,
    std::span<const uint8_t> additional_data,
    std::span<uint8_t> encrypted_frame) {
  RTC_DCHECK_RUNS_SERIALIZED(&access_checker_);
  if (!active_key_id_) {
    return RTCError::InvalidState("Sframe encryption key not set");
  }

  auto result = context_->protect(
      *active_key_id_,
      sframe::output_bytes(encrypted_frame.data(), encrypted_frame.size()),
      sframe::input_bytes(frame.data(), frame.size()),
      sframe::input_bytes(additional_data.data(), additional_data.size()));
  if (result.is_err()) {
    RTCError error = RTCError::InternalError("Sframe encryption failed");
    if (const char* message = result.error().message()) {
      error.string_builder() << ": " << message;
    }
    return error;
  }
  return result.value().size();
}

size_t SframeEncryptor::GetMaxCiphertextByteSize(size_t frame_size) {
  return frame_size + sframe::Context::max_overhead;
}

}  // namespace webrtc
