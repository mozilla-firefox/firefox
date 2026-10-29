/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/sframe/sframe_context_factory.h"

#include <memory>

#include "api/sframe/sframe_types.h"
#include "third_party/sframe/src/include/sframe/sframe.h"

namespace webrtc {
namespace {

sframe::CipherSuite ToSframeCipherSuite(SframeCipherSuite suite) {
  switch (suite) {
    case SframeCipherSuite::kAes128CtrHmacSha256_80:
      return sframe::CipherSuite::AES_128_CTR_HMAC_SHA256_80;
    case SframeCipherSuite::kAes128CtrHmacSha256_64:
      return sframe::CipherSuite::AES_128_CTR_HMAC_SHA256_64;
    case SframeCipherSuite::kAes128CtrHmacSha256_32:
      return sframe::CipherSuite::AES_128_CTR_HMAC_SHA256_32;
    case SframeCipherSuite::kAes128GcmSha256_128:
      return sframe::CipherSuite::AES_GCM_128_SHA256;
    case SframeCipherSuite::kAes256GcmSha512_128:
      return sframe::CipherSuite::AES_GCM_256_SHA512;
    case SframeCipherSuite::kAes256CtrHmacSha512_80:
      return sframe::CipherSuite::AES_256_CTR_HMAC_SHA512_80;
    case SframeCipherSuite::kAes256CtrHmacSha512_64:
      return sframe::CipherSuite::AES_256_CTR_HMAC_SHA512_64;
    case SframeCipherSuite::kAes256CtrHmacSha512_32:
      return sframe::CipherSuite::AES_256_CTR_HMAC_SHA512_32;
  }
}

}  // namespace

std::unique_ptr<sframe::Context> CreateSframeContext(SframeCipherSuite suite) {
  return std::make_unique<sframe::Context>(ToSframeCipherSuite(suite));
}

}  // namespace webrtc
