/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/histogram.h"
#include "test/gtest.h"

namespace webrtc {
namespace video_coding {

class TestHistogram : public ::testing::Test {
 protected:
  TestHistogram() : histogram_(5, 10) {}
  Histogram histogram_;
};

TEST_F(TestHistogram, NumValues) {
  EXPECT_EQ(0ul, histogram_.NumValues());
  histogram_.Add(0);
  EXPECT_EQ(1ul, histogram_.NumValues());
}

TEST_F(TestHistogram, InverseCdf) {
  histogram_.Add(0);
  histogram_.Add(1);
  histogram_.Add(2);
  histogram_.Add(3);
  histogram_.Add(4);
  EXPECT_EQ(5ul, histogram_.NumValues());
  EXPECT_EQ(1ul, histogram_.InverseCdf(0.2f));
  EXPECT_EQ(2ul, histogram_.InverseCdf(0.2000001f));
  EXPECT_EQ(4ul, histogram_.InverseCdf(0.8f));

  histogram_.Add(0);
  EXPECT_EQ(6ul, histogram_.NumValues());
  EXPECT_EQ(1ul, histogram_.InverseCdf(0.2f));
  EXPECT_EQ(1ul, histogram_.InverseCdf(0.2000001f));
}

TEST_F(TestHistogram, ReplaceOldValues) {
  histogram_.Add(0);
  histogram_.Add(0);
  histogram_.Add(0);
  histogram_.Add(0);
  histogram_.Add(0);
  histogram_.Add(1);
  histogram_.Add(1);
  histogram_.Add(1);
  histogram_.Add(1);
  histogram_.Add(1);
  EXPECT_EQ(10ul, histogram_.NumValues());
  EXPECT_EQ(1ul, histogram_.InverseCdf(0.5f));
  EXPECT_EQ(2ul, histogram_.InverseCdf(0.5000001f));

  histogram_.Add(4);
  histogram_.Add(4);
  histogram_.Add(4);
  histogram_.Add(4);
  EXPECT_EQ(10ul, histogram_.NumValues());
  EXPECT_EQ(1ul, histogram_.InverseCdf(0.1f));
  EXPECT_EQ(2ul, histogram_.InverseCdf(0.5f));

  histogram_.Add(20);
  EXPECT_EQ(10ul, histogram_.NumValues());
  EXPECT_EQ(2ul, histogram_.InverseCdf(0.5f));
  EXPECT_EQ(5ul, histogram_.InverseCdf(0.5000001f));
}

}  // namespace video_coding
}  // namespace webrtc
