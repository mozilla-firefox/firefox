/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_capture/linux/pipewire_video_sizes.h"

#include <spa/param/format.h>
#include <spa/param/param.h>
#include <spa/pod/builder.h>
#include <spa/pod/filter.h>
#include <spa/pod/iter.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace videocapturemodule {
namespace {

using Size = std::pair<uint32_t, uint32_t>;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

class PipeWireVideoSizesTest : public ::testing::Test {
 protected:
  void SetChoice(uint32_t choice, std::initializer_list<Size> sizes) {
    spa_pod_builder builder =
        SPA_POD_BUILDER_INIT(data_.data(), static_cast<uint32_t>(data_.size()));
    spa_pod_frame object, values;
    spa_pod_builder_push_object(&builder, &object, SPA_TYPE_OBJECT_Format,
                                SPA_PARAM_EnumFormat);
    spa_pod_builder_prop(&builder, SPA_FORMAT_VIDEO_size, 0);
    spa_pod_builder_push_choice(&builder, &values, choice, 0);
    for (const auto& [width, height] : sizes) {
      spa_pod_builder_rectangle(&builder, width, height);
    }
    spa_pod_builder_pop(&builder, &values);
    format_ = static_cast<spa_pod*>(spa_pod_builder_pop(&builder, &object));
    ASSERT_NE(format_, nullptr);
  }

  std::vector<Size> Sizes() const {
    const spa_pod_prop* prop = spa_pod_object_find_prop(
        reinterpret_cast<const spa_pod_object*>(format_), nullptr,
        SPA_FORMAT_VIDEO_size);
    std::vector<Size> sizes;
    for (const auto& size : GetPipeWireVideoSizes(&prop->value)) {
      sizes.emplace_back(size.width, size.height);
    }
    return sizes;
  }

  void ExpectNegotiable() const {
    const auto sizes = Sizes();
    ASSERT_FALSE(sizes.empty());
    for (const auto& [width, height] : sizes) {
      alignas(8) std::array<uint8_t, 1024> request_data, result_data;
      spa_pod_builder request = SPA_POD_BUILDER_INIT(
          request_data.data(), static_cast<uint32_t>(request_data.size()));
      spa_pod_builder result = SPA_POD_BUILDER_INIT(
          result_data.data(), static_cast<uint32_t>(result_data.size()));
      const spa_rectangle size = SPA_RECTANGLE(width, height);
      const auto* filter =
          static_cast<const spa_pod*>(spa_pod_builder_add_object(
              &request, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
              SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&size)));
      spa_pod* filtered = nullptr;
      EXPECT_EQ(spa_pod_filter(&result, &filtered, format_, filter), 0)
          << width << "x" << height;
    }
  }

  alignas(8) std::array<uint8_t, 4096> data_;
  const spa_pod* format_ = nullptr;
};

TEST_F(PipeWireVideoSizesTest, KeepsFixedSize) {
  SetChoice(SPA_CHOICE_None, {{640, 480}});
  EXPECT_THAT(Sizes(), ElementsAre(Size(640, 480)));
  ExpectNegotiable();
}

TEST_F(PipeWireVideoSizesTest, OffersEachEnumAlternativeOnce) {
  SetChoice(SPA_CHOICE_Enum,
            {{640, 480}, {1280, 720}, {640, 480}, {1280, 720}});
  EXPECT_THAT(Sizes(), UnorderedElementsAre(Size(640, 480), Size(1280, 720)));
  ExpectNegotiable();
}

TEST_F(PipeWireVideoSizesTest, RangeIncludesDefaultBoundsAndCommonSize) {
  SetChoice(SPA_CHOICE_Range, {{1296, 736}, {320, 240}, {1920, 1080}});
  EXPECT_THAT(Sizes(), Contains(Size(1296, 736)));
  EXPECT_THAT(Sizes(), Contains(Size(320, 240)));
  EXPECT_THAT(Sizes(), Contains(Size(1920, 1080)));
  EXPECT_THAT(Sizes(), Contains(Size(640, 480)));
  ExpectNegotiable();
}

TEST_F(PipeWireVideoSizesTest, DefaultDoesNotLimitRangeMaximum) {
  SetChoice(SPA_CHOICE_Range, {{640, 480}, {320, 240}, {3840, 2160}});
  EXPECT_THAT(Sizes(), Contains(Size(1920, 1080)));
  EXPECT_THAT(Sizes(), Contains(Size(3840, 2160)));
  ExpectNegotiable();
}

TEST_F(PipeWireVideoSizesTest, RangeChecksWidthAndHeightIndependently) {
  SetChoice(SPA_CHOICE_Range, {{640, 480}, {320, 240}, {1000, 1000}});
  for (const auto& [width, height] : Sizes()) {
    EXPECT_GE(width, 320u);
    EXPECT_LE(width, 1000u);
    EXPECT_GE(height, 240u);
    EXPECT_LE(height, 1000u);
  }
  ExpectNegotiable();
}

TEST_F(PipeWireVideoSizesTest, StepUsesAbsoluteMultiplesWithUnalignedMinimum) {
  SetChoice(SPA_CHOICE_Step, {{648, 488}, {321, 241}, {1296, 736}, {8, 8}});
  EXPECT_THAT(Sizes(), Contains(Size(640, 480)));
  EXPECT_THAT(Sizes(), Contains(Size(648, 488)));
  ExpectNegotiable();
}

TEST_F(PipeWireVideoSizesTest, StepKeepsOnlyNegotiableCandidates) {
  // Older PipeWire versions report the maximum as the step for this camera.
  SetChoice(SPA_CHOICE_Step,
            {{1296, 736}, {320, 240}, {1296, 736}, {1296, 736}});
  EXPECT_THAT(Sizes(), ElementsAre(Size(1296, 736)));
  ExpectNegotiable();
}

TEST_F(PipeWireVideoSizesTest, RejectsZeroStep) {
  SetChoice(SPA_CHOICE_Step, {{640, 480}, {320, 240}, {1296, 736}, {0, 1}});
  EXPECT_THAT(Sizes(), IsEmpty());
  SetChoice(SPA_CHOICE_Step, {{640, 480}, {320, 240}, {1296, 736}, {1, 0}});
  EXPECT_THAT(Sizes(), IsEmpty());
}

TEST_F(PipeWireVideoSizesTest, OffersFourKMaximum) {
  SetChoice(SPA_CHOICE_Range, {{1280, 720}, {320, 240}, {4096, 2160}});
  EXPECT_THAT(Sizes(), Contains(Size(4096, 2160)));
  ExpectNegotiable();
}

TEST_F(PipeWireVideoSizesTest, OffersEightKMaximum) {
  SetChoice(SPA_CHOICE_Range, {{1920, 1080}, {320, 240}, {7680, 4320}});
  EXPECT_THAT(Sizes(), Contains(Size(7680, 4320)));
  ExpectNegotiable();
}

TEST_F(PipeWireVideoSizesTest, DoesNotCapMaximumAboveEightK) {
  SetChoice(SPA_CHOICE_Range, {{1920, 1080}, {320, 240}, {16384, 8640}});
  EXPECT_THAT(Sizes(), Contains(Size(16384, 8640)));
  ExpectNegotiable();
}

TEST_F(PipeWireVideoSizesTest, DegenerateRangeDoesNotDuplicateSize) {
  SetChoice(SPA_CHOICE_Range, {{640, 480}, {640, 480}, {640, 480}});
  EXPECT_THAT(Sizes(), ElementsAre(Size(640, 480)));
  ExpectNegotiable();
}

TEST_F(PipeWireVideoSizesTest, IgnoresDefaultOutsideRange) {
  SetChoice(SPA_CHOICE_Range, {{3840, 2160}, {320, 240}, {1920, 1080}});
  EXPECT_THAT(Sizes(), Contains(Size(320, 240)));
  EXPECT_THAT(Sizes(), Contains(Size(1920, 1080)));
  for (const auto& [width, height] : Sizes()) {
    EXPECT_LE(width, 1920u);
    EXPECT_LE(height, 1080u);
  }
  ExpectNegotiable();
}

TEST_F(PipeWireVideoSizesTest, RejectsNonRectangleValue) {
  const spa_pod_int integer = {{sizeof(int32_t), SPA_TYPE_Int}, 640};
  EXPECT_THAT(GetPipeWireVideoSizes(&integer.pod), IsEmpty());
}

}  // namespace
}  // namespace videocapturemodule
}  // namespace webrtc
