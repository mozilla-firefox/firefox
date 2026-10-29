/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/ssl_fingerprint.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/strings/string_view.h"
#include "rtc_base/buffer.h"
#include "rtc_base/logging.h"
#include "rtc_base/message_digest.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/ssl_identity.h"
#include "rtc_base/string_encode.h"

namespace webrtc {

std::unique_ptr<SSLFingerprint> SSLFingerprint::Create(
    absl::string_view algorithm,
    const SSLIdentity& identity) {
  return Create(algorithm, identity.certificate());
}

std::unique_ptr<SSLFingerprint> SSLFingerprint::Create(
    absl::string_view algorithm,
    const SSLCertificate& cert) {
  Buffer digest = Buffer::CreateWithCapacity(MessageDigest::kMaxSize);
  bool ret = cert.ComputeDigest(algorithm, digest);
  if (!ret) {
    return nullptr;
  }
  return std::make_unique<SSLFingerprint>(algorithm, digest);
}

std::unique_ptr<SSLFingerprint> SSLFingerprint::CreateFromRfc4572(
    absl::string_view algorithm,
    absl::string_view fingerprint) {
  if (algorithm.empty() || !IsFips180DigestAlgorithm(algorithm))
    return nullptr;

  if (fingerprint.empty())
    return nullptr;

  uint8_t buffer[MessageDigest::kMaxSize];
  std::span<uint8_t> value = HexDecodeWithDelimiter(buffer, fingerprint, ':');
  if (value.empty()) {
    return nullptr;
  }

  return std::make_unique<SSLFingerprint>(algorithm, value);
}

std::optional<SSLFingerprint> SSLFingerprint::CreateOptionalFromRfc4572(
    absl::string_view algorithm,
    absl::string_view fingerprint) {
  if (auto parsed = CreateFromRfc4572(algorithm, fingerprint)) {
    return std::move(*parsed);
  }
  return std::nullopt;
}

std::unique_ptr<SSLFingerprint> SSLFingerprint::CreateFromCertificate(
    const RTCCertificate& cert) {
  std::string digest_alg;
  if (!cert.GetSSLCertificate().GetSignatureDigestAlgorithm(&digest_alg)) {
    RTC_LOG(LS_ERROR)
        << "Failed to retrieve the certificate's digest algorithm";
    return nullptr;
  }

  std::unique_ptr<SSLFingerprint> fingerprint =
      Create(digest_alg, *cert.identity());
  if (fingerprint == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to create identity fingerprint, alg="
                      << digest_alg;
  }
  return fingerprint;
}

SSLFingerprint::SSLFingerprint(absl::string_view algorithm,
                               std::span<const uint8_t> digest_view)
    : algorithm(algorithm), digest(digest_view) {}

bool SSLFingerprint::operator==(const SSLFingerprint& other) const {
  return algorithm == other.algorithm && digest == other.digest;
}

std::string SSLFingerprint::GetRfc4572Fingerprint() const {
  std::string fingerprint = HexEncodeWithDelimiter(digest, ':');
  absl::c_transform(fingerprint, fingerprint.begin(), ::toupper);
  return fingerprint;
}

std::string SSLFingerprint::ToString() const {
  std::string fp_str = algorithm;
  fp_str.append(" ");
  fp_str.append(GetRfc4572Fingerprint());
  return fp_str;
}

}  // namespace webrtc
