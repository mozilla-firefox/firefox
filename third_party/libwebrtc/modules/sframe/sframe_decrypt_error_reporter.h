/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_SFRAME_SFRAME_DECRYPT_ERROR_REPORTER_H_
#define MODULES_SFRAME_SFRAME_DECRYPT_ERROR_REPORTER_H_

#include <cstdint>
#include <optional>

#include "absl/base/nullability.h"
#include "api/ref_counted_base.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/sframe/sframe_decryptor_interface.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {

// Owns `SframeDecryptError` reporting for a single receive stream.
// Deduplicates consecutive failures of the same type — a sustained failure
// (e.g. missing key) fires the callback once rather than once per frame.
class SframeDecryptErrorReporter final
    : public RefCountedNonVirtual<SframeDecryptErrorReporter> {
 public:
  static absl_nonnull scoped_refptr<SframeDecryptErrorReporter> Create(
      absl_nonnull SframeDecryptErrorCallback error_callback);

  explicit SframeDecryptErrorReporter(
      absl_nonnull SframeDecryptErrorCallback error_callback);

  // Reports a decryption failure. Consecutive identical failures are reported
  // only once until `ReportSuccess()` is called.
  void ReportError(SframeDecryptError error);

  // Notes a successful decryption, clearing the dedup latch so a later failure
  // of the same type is reported again.
  void ReportSuccess();

 private:
  // Callers must use this object from a single sequence. Today that sequence
  // is the media-pipeline (worker) thread reached via the signaling thread.
  RTC_NO_UNIQUE_ADDRESS SequenceChecker sequence_checker_;

  SframeDecryptErrorCallback error_callback_ RTC_GUARDED_BY(sequence_checker_);

  struct LastReportedError {
    SframeDecryptErrorType error_type;
    std::optional<uint64_t> key_id;

    bool operator==(const LastReportedError&) const = default;
  };

  // Nullopt before any failure or after a success.
  std::optional<LastReportedError> last_reported_error_
      RTC_GUARDED_BY(sequence_checker_);
};

}  // namespace webrtc

#endif  // MODULES_SFRAME_SFRAME_DECRYPT_ERROR_REPORTER_H_
