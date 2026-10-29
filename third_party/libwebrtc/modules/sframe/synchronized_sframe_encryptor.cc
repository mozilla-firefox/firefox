/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/sframe/synchronized_sframe_encryptor.h"

#include <cstddef>
#include <cstdint>
#include <span>

#include "api/make_ref_counted.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/sframe/sframe_types.h"
#include "modules/sframe/sframe_encryptor.h"
#include "rtc_base/synchronization/mutex.h"

namespace webrtc {

scoped_refptr<SynchronizedSframeEncryptor> SynchronizedSframeEncryptor::Create(
    SframeCipherSuite cipher_suite) {
  return make_ref_counted<SynchronizedSframeEncryptor>(cipher_suite);
}

SynchronizedSframeEncryptor::SynchronizedSframeEncryptor(
    SframeCipherSuite cipher_suite)
    : encryptor_(SframeEncryptor::Create(cipher_suite)) {}

SynchronizedSframeEncryptor::~SynchronizedSframeEncryptor() = default;

RTCError SynchronizedSframeEncryptor::SetEncryptionKey(
    uint64_t key_id,
    std::span<const uint8_t> key_material) {
  MutexLock lock(&mutex_);
  return encryptor_->SetEncryptionKey(key_id, key_material);
}

RTCErrorOr<size_t> SynchronizedSframeEncryptor::Encrypt(
    std::span<const uint8_t> frame,
    std::span<const uint8_t> additional_data,
    std::span<uint8_t> encrypted_frame) {
  MutexLock lock(&mutex_);
  return encryptor_->Encrypt(frame, additional_data, encrypted_frame);
}

size_t SynchronizedSframeEncryptor::GetMaxCiphertextByteSize(
    size_t frame_size) {
  // Doesn't touch any mutable state, so no need to lock.
  return encryptor_->GetMaxCiphertextByteSize(frame_size);
}

}  // namespace webrtc
