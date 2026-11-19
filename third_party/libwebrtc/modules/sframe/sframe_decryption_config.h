/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_SFRAME_SFRAME_DECRYPTION_CONFIG_H_
#define MODULES_SFRAME_SFRAME_DECRYPTION_CONFIG_H_

#include "absl/base/nullability.h"
#include "api/scoped_refptr.h"
#include "modules/sframe/sframe_decrypt_error_reporter.h"
#include "modules/sframe/sframe_media_decryptor_interface.h"

namespace webrtc {

// Keeps the decryptor and its error reporter together in the receive pipeline.
struct SframeDecryptionConfig {
  absl_nonnull scoped_refptr<SframeMediaDecryptorInterface> decryptor;
  // Must only be used on the sequence that decrypts the frames.
  absl_nonnull scoped_refptr<SframeDecryptErrorReporter> error_reporter;
};

}  // namespace webrtc

#endif  // MODULES_SFRAME_SFRAME_DECRYPTION_CONFIG_H_
