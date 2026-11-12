/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_SFRAME_SYNCHRONIZED_SFRAME_ENCRYPTOR_H_
#define MODULES_SFRAME_SYNCHRONIZED_SFRAME_ENCRYPTOR_H_

#include <cstddef>
#include <cstdint>
#include <span>

#include "absl/base/nullability.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/sframe/sframe_types.h"
#include "modules/sframe/sframe_media_encryptor_interface.h"
#include "rtc_base/synchronization/mutex.h"

namespace webrtc {

class SynchronizedSframeEncryptor : public SframeMediaEncryptorInterface {
 public:
  static absl_nonnull scoped_refptr<SynchronizedSframeEncryptor> Create(
      SframeCipherSuite cipher_suite);

  ~SynchronizedSframeEncryptor() override;

  RTCError SetEncryptionKey(uint64_t key_id,
                            std::span<const uint8_t> key_material) override;
  RTCErrorOr<size_t> Encrypt(std::span<const uint8_t> frame,
                             std::span<const uint8_t> additional_data,
                             std::span<uint8_t> encrypted_frame) override;
  size_t GetMaxCiphertextByteSize(size_t frame_size) override;

 protected:
  explicit SynchronizedSframeEncryptor(SframeCipherSuite cipher_suite);

 private:
  // Serializes access to the state of `encryptor_`, which is not thread-safe.
  Mutex mutex_;
  const scoped_refptr<SframeMediaEncryptorInterface> encryptor_;
};

}  // namespace webrtc

#endif  // MODULES_SFRAME_SYNCHRONIZED_SFRAME_ENCRYPTOR_H_
