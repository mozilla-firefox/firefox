/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "common_audio/vad/vad_unittest.h"

#include <cstdint>
#include <cstdlib>
#include <iterator>

#include "common_audio/signal_processing/include/signal_processing_library.h"
#include "common_audio/vad/include/webrtc_vad.h"
#include "rtc_base/checks.h"
#include "test/gtest.h"

namespace webrtc {
namespace test {

// TODO(bugs.webrtc.org/345674542): Fix/enable.
#if defined(__has_feature) && __has_feature(undefined_behavior_sanitizer)
TEST_F(VadTest, DISABLED_ApiTest) {
#else
TEST_F(VadTest, ApiTest) {
#endif
  // This API test runs through the APIs for all possible valid and invalid
  // combinations.

  VadInst* handle = WebRtcVad_Create();
  int16_t zeros[kMaxFrameLength] = {0};

  // Construct a speech signal that will trigger the VAD in all modes. It is
  // known that (i * i) will wrap around, but that doesn't matter in this case.
  int16_t speech[kMaxFrameLength];
  for (size_t i = 0; i < kMaxFrameLength; i++) {
    speech[i] = static_cast<int16_t>(i * i);
  }

  // nullptr instance tests
  EXPECT_EQ(-1, WebRtcVad_Init(nullptr));
  EXPECT_EQ(-1, WebRtcVad_set_mode(nullptr, kModes[0]));
  EXPECT_EQ(-1,
            WebRtcVad_Process(nullptr, kRates[0], speech, kFrameLengths[0]));

  // WebRtcVad_Create()
  RTC_CHECK(handle);

  // Not initialized tests
  EXPECT_EQ(-1, WebRtcVad_Process(handle, kRates[0], speech, kFrameLengths[0]));
  EXPECT_EQ(-1, WebRtcVad_set_mode(handle, kModes[0]));

  // WebRtcVad_Init() test
  ASSERT_EQ(0, WebRtcVad_Init(handle));

  // WebRtcVad_set_mode() invalid modes tests. Tries smallest supported value
  // minus one and largest supported value plus one.
  EXPECT_EQ(-1, WebRtcVad_set_mode(
                    handle, WebRtcSpl_MinValueW32(kModes, kModesSize) - 1));
  EXPECT_EQ(-1, WebRtcVad_set_mode(
                    handle, WebRtcSpl_MaxValueW32(kModes, kModesSize) + 1));

  // WebRtcVad_Process() tests
  // nullptr as speech pointer
  EXPECT_EQ(-1,
            WebRtcVad_Process(handle, kRates[0], nullptr, kFrameLengths[0]));
  // Invalid sampling rate
  EXPECT_EQ(-1, WebRtcVad_Process(handle, 9999, speech, kFrameLengths[0]));
  // All zeros as input should work
  EXPECT_EQ(0, WebRtcVad_Process(handle, kRates[0], zeros, kFrameLengths[0]));
  for (size_t k = 0; k < kModesSize; k++) {
    // Test valid modes
    EXPECT_EQ(0, WebRtcVad_set_mode(handle, kModes[k]));
    // Loop through sampling rate and frame length combinations
    for (size_t i = 0; i < kRatesSize; i++) {
      for (size_t j = 0; j < kFrameLengthsSize; j++) {
        if (ValidRatesAndFrameLengths(kRates[i], kFrameLengths[j])) {
          EXPECT_EQ(1, WebRtcVad_Process(handle, kRates[i], speech,
                                         kFrameLengths[j]));
        } else {
          EXPECT_EQ(-1, WebRtcVad_Process(handle, kRates[i], speech,
                                          kFrameLengths[j]));
        }
      }
    }
  }

  WebRtcVad_Free(handle);
}

TEST_F(VadTest, ValidRatesFrameLengths) {
  // This test verifies valid and invalid rate/frame_length combinations. We
  // loop through some sampling rates and frame lengths from negative values to
  // values larger than possible.
  const int kInvalidRates[] = {-8000, -4000, 0,     4000,  8000,  8001,
                               15999, 16000, 32000, 48000, 48001, 96000};

  const size_t kInvalidFrameLengths[] = {0,   80,  81,  159, 160,  240,
                                         320, 480, 640, 960, 1440, 2000};

  for (size_t i = 0; i < std::size(kInvalidRates); i++) {
    for (size_t j = 0; j < std::size(kInvalidFrameLengths); j++) {
      if (ValidRatesAndFrameLengths(kInvalidRates[i],
                                    kInvalidFrameLengths[j])) {
        EXPECT_EQ(0, WebRtcVad_ValidRateAndFrameLength(
                         kInvalidRates[i], kInvalidFrameLengths[j]));
      } else {
        EXPECT_EQ(-1, WebRtcVad_ValidRateAndFrameLength(
                          kInvalidRates[i], kInvalidFrameLengths[j]));
      }
    }
  }
}

// TODO(bjornv): Add a process test, run on file.

}  // namespace test
}  // namespace webrtc
