/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>

#include "api/audio/audio_processing.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "modules/audio_processing/include/mock_audio_processing.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

using ::testing::_;
using ::testing::NotNull;

TEST(CustomAudioProcessingTest, ReturnsPassedAudioProcessing) {
  scoped_refptr<AudioProcessing> ap =
      make_ref_counted<test::MockAudioProcessing>();

  std::unique_ptr<AudioProcessingBuilderInterface> builder =
      CustomAudioProcessing(ap);

  ASSERT_THAT(builder, NotNull());
  EXPECT_EQ(builder->Build(CreateTestEnvironment()), ap);
}

#if GTEST_HAS_DEATH_TEST
TEST(CustomAudioProcessingTest, NullptrAudioProcessingIsUnsupported) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnonnull"
  EXPECT_DEATH(CustomAudioProcessing(nullptr), _);
#pragma clang diagnostic pop
}
#endif

}  // namespace webrtc
