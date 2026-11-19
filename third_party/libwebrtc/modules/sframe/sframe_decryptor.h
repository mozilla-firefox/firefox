/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_SFRAME_SFRAME_DECRYPTOR_H_
#define MODULES_SFRAME_SFRAME_DECRYPTOR_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "absl/base/nullability.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/sframe/sframe_types.h"
#include "modules/sframe/sframe_media_decryptor_interface.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread_annotations.h"

namespace sframe {
class Context;
}  // namespace sframe

namespace webrtc {

class SframeDecryptor : public SframeMediaDecryptorInterface {
 public:
  // Creates a new SframeDecryptor. This factory method never fails.
  static absl_nonnull scoped_refptr<SframeDecryptor> Create(
      SframeCipherSuite cipher_suite);

  ~SframeDecryptor() override;

  // SframeMediaDecryptorInterface implementation.
  RTCError AddDecryptionKey(uint64_t key_id,
                            std::span<const uint8_t> key_material) override;
  RTCError RemoveDecryptionKey(uint64_t key_id) override;
  SframeDecryptResult Decrypt(std::span<const uint8_t> encrypted_frame,
                              std::span<const uint8_t> additional_data,
                              std::span<uint8_t> frame) override;

  size_t GetMaxPlaintextByteSize(size_t encrypted_frame_size) override;

 protected:
  explicit SframeDecryptor(SframeCipherSuite cipher_suite);

 private:
  // Callers must use this object from a single sequence. Today that sequence
  // is the media-pipeline (worker) thread reached via the signaling thread.
  RTC_NO_UNIQUE_ADDRESS SequenceChecker sequence_checker_;

  std::unique_ptr<sframe::Context> context_ RTC_GUARDED_BY(sequence_checker_);
};

}  // namespace webrtc

#endif  // MODULES_SFRAME_SFRAME_DECRYPTOR_H_
