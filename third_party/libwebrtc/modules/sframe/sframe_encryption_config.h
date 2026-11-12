/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_SFRAME_SFRAME_ENCRYPTION_CONFIG_H_
#define MODULES_SFRAME_SFRAME_ENCRYPTION_CONFIG_H_

#include "absl/base/nullability.h"
#include "api/scoped_refptr.h"
#include "api/sframe/sframe_types.h"
#include "modules/sframe/sframe_media_encryptor_interface.h"

namespace webrtc {

// Keeps the encryptor and its mode together across the send pipeline.
struct SframeEncryptionConfig {
  SframeMode mode;
  absl_nonnull scoped_refptr<SframeMediaEncryptorInterface> encryptor;

  friend bool operator==(const SframeEncryptionConfig&,
                         const SframeEncryptionConfig&) = default;
};

}  // namespace webrtc

#endif  // MODULES_SFRAME_SFRAME_ENCRYPTION_CONFIG_H_
