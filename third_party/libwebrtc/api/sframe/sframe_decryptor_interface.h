/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_SFRAME_SFRAME_DECRYPTOR_INTERFACE_H_
#define API_SFRAME_SFRAME_DECRYPTOR_INTERFACE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "api/frame_transformer_interface.h"
#include "api/ref_count.h"
#include "api/sframe/sframe_types.h"

namespace webrtc {

enum class SframeDecryptErrorType {
  kAuthentication,  // Authentication tag validation failed.
  kKeyId,           // Unknown key identifier in the Sframe header.
  kSyntax,          // Payload does not follow the Sframe format.
};

// The error reported when Sframe decryption fails,
// aligned with SFrameTransformErrorEvent:
// https://w3c.github.io/webrtc-encoded-transform/#dom-sframetransformerrorrevent
struct SframeDecryptError {
  SframeDecryptErrorType error_type;
  // Key identifier parsed from the Sframe header.
  // Set only when `error_type` is kKeyId; otherwise nullopt.
  std::optional<uint64_t> key_id;
  // Frame/Packet that failed decryption. The payload is the unmodified
  // encrypted data — Sframe verifies authentication before decrypting
  // (RFC 9605, Section 4.5.1), so no plaintext is ever exposed here.
  // TODO(bugs.webrtc.org/479862368): Add tests verifying that the frame
  // payload is unmodified ciphertext when this field is populated.
  std::unique_ptr<TransformableFrameInterface> frame;
};

// Invoked when Sframe decryption fails.
//
// The Sframe transform algorithm requires firing an error on each decrypt
// failure, which can mean one event per frame/packet for a sustained failure.
// WebRTC may instead invoke this at most once while the same error recurs
// consecutively. Whether the spec should limit such events is tracked in
// https://github.com/w3c/webrtc-encoded-transform/issues/307
using SframeDecryptErrorCallback = absl::AnyInvocable<void(SframeDecryptError)>;

// Key management handle for Sframe receiver decryption. Key updates are
// applied asynchronously, in the order they were requested, and their results
// are reported on the signaling thread. The `SframeDecryptorInterface` instance
// must be released on the signaling thread; no result is reported after that.
class SframeDecryptorInterface : public RefCountInterface {
 public:
  virtual void AddDecryptionKey(uint64_t key_id,
                                std::span<const uint8_t> key_material,
                                absl_nonnull SframeKeyUpdateCallback
                                    callback) = 0;

  virtual void RemoveDecryptionKey(uint64_t key_id,
                                   absl_nonnull SframeKeyUpdateCallback
                                       callback) = 0;

 protected:
  ~SframeDecryptorInterface() override = default;
};

}  // namespace webrtc

#endif  // API_SFRAME_SFRAME_DECRYPTOR_INTERFACE_H_
