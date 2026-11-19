/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/sframe/sframe_decrypt_error_reporter.h"

#include <optional>
#include <utility>

#include "absl/base/nullability.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/sframe/sframe_decryptor_interface.h"
#include "rtc_base/checks.h"

namespace webrtc {

scoped_refptr<SframeDecryptErrorReporter> SframeDecryptErrorReporter::Create(
    absl_nonnull SframeDecryptErrorCallback error_callback) {
  return make_ref_counted<SframeDecryptErrorReporter>(
      std::move(error_callback));
}

SframeDecryptErrorReporter::SframeDecryptErrorReporter(
    absl_nonnull SframeDecryptErrorCallback error_callback)
    : sequence_checker_(SequenceChecker::kDetached),
      error_callback_(std::move(error_callback)) {
  RTC_DCHECK(error_callback_);
}

void SframeDecryptErrorReporter::ReportError(SframeDecryptError error) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  // Skip repeated identical failures — a sustained failure would otherwise
  // fire the callback once per frame/packet, causing too many error events.
  LastReportedError current{error.error_type, error.key_id};
  if (last_reported_error_ == current) {
    return;
  }
  last_reported_error_ = current;

  error_callback_(std::move(error));
}

void SframeDecryptErrorReporter::ReportSuccess() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  last_reported_error_ = std::nullopt;
}

}  // namespace webrtc
