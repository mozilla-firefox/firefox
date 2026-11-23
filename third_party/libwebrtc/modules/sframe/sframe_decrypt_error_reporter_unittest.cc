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

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "api/frame_transformer_interface.h"
#include "api/sframe/sframe_decryptor_interface.h"
#include "api/test/mock_transformable_frame.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::MockFunction;

SframeDecryptError MakeError(SframeDecryptErrorType error_type,
                             std::optional<uint64_t> key_id = std::nullopt) {
  SframeDecryptError error;
  error.error_type = error_type;
  error.key_id = key_id;
  error.frame = std::make_unique<MockTransformableFrame>();
  return error;
}

TEST(SframeDecryptErrorReporterTest, FirstErrorInvokesCallback) {
  MockFunction<void(SframeDecryptError)> callback;
  EXPECT_CALL(callback, Call).Times(1);

  SframeDecryptErrorReporter reporter(callback.AsStdFunction());
  reporter.ReportError(MakeError(SframeDecryptErrorType::kAuthentication));
}

TEST(SframeDecryptErrorReporterTest, ConsecutiveSameErrorTypeSuppressed) {
  MockFunction<void(SframeDecryptError)> callback;
  EXPECT_CALL(callback, Call).Times(1);

  SframeDecryptErrorReporter reporter(callback.AsStdFunction());
  reporter.ReportError(MakeError(SframeDecryptErrorType::kAuthentication));
  reporter.ReportError(MakeError(SframeDecryptErrorType::kAuthentication));
  reporter.ReportError(MakeError(SframeDecryptErrorType::kAuthentication));
}

TEST(SframeDecryptErrorReporterTest, DifferentErrorTypeBreaksSuppression) {
  MockFunction<void(SframeDecryptError)> callback;
  EXPECT_CALL(callback, Call).Times(2);

  SframeDecryptErrorReporter reporter(callback.AsStdFunction());
  reporter.ReportError(MakeError(SframeDecryptErrorType::kAuthentication));
  reporter.ReportError(MakeError(SframeDecryptErrorType::kKeyId));
}

TEST(SframeDecryptErrorReporterTest, DifferentKeyIdBreaksSuppression) {
  MockFunction<void(SframeDecryptError)> callback;
  EXPECT_CALL(callback, Call).Times(2);

  SframeDecryptErrorReporter reporter(callback.AsStdFunction());
  reporter.ReportError(MakeError(SframeDecryptErrorType::kKeyId, 1));
  reporter.ReportError(MakeError(SframeDecryptErrorType::kKeyId, 2));
}

TEST(SframeDecryptErrorReporterTest,
     ReportSuccessClearsDedupSoSameTypeFiresAgain) {
  MockFunction<void(SframeDecryptError)> callback;
  EXPECT_CALL(callback, Call).Times(2);

  SframeDecryptErrorReporter reporter(callback.AsStdFunction());
  reporter.ReportError(MakeError(SframeDecryptErrorType::kAuthentication));
  reporter.ReportSuccess();
  reporter.ReportError(MakeError(SframeDecryptErrorType::kAuthentication));
}

TEST(SframeDecryptErrorReporterTest, ReportSuccessWithNoErrorIsNoop) {
  MockFunction<void(SframeDecryptError)> callback;
  EXPECT_CALL(callback, Call).Times(1);

  SframeDecryptErrorReporter reporter(callback.AsStdFunction());
  reporter.ReportSuccess();
  reporter.ReportError(MakeError(SframeDecryptErrorType::kAuthentication));
}

TEST(SframeDecryptErrorReporterTest, CallbackReceivesCorrectErrorType) {
  std::optional<SframeDecryptErrorType> received_type;
  SframeDecryptErrorReporter reporter(
      [&](SframeDecryptError error) { received_type = error.error_type; });

  reporter.ReportError(MakeError(SframeDecryptErrorType::kKeyId));

  ASSERT_TRUE(received_type.has_value());
  EXPECT_EQ(*received_type, SframeDecryptErrorType::kKeyId);
}

TEST(SframeDecryptErrorReporterTest, CallbackReceivesFrame) {
  const TransformableFrameInterface* received_frame_ptr = nullptr;
  SframeDecryptErrorReporter reporter([&](SframeDecryptError error) {
    received_frame_ptr = error.frame.get();
  });

  auto frame = std::make_unique<MockTransformableFrame>();
  const TransformableFrameInterface* expected_ptr = frame.get();

  SframeDecryptError error;
  error.error_type = SframeDecryptErrorType::kSyntax;
  error.frame = std::move(frame);
  reporter.ReportError(std::move(error));

  EXPECT_EQ(received_frame_ptr, expected_ptr);
}

}  // namespace
}  // namespace webrtc
