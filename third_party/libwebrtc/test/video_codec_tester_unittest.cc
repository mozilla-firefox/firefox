/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/video_codec_tester.h"

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/test/mock_video_decoder.h"
#include "api/test/mock_video_decoder_factory.h"
#include "api/test/mock_video_encoder.h"
#include "api/test/mock_video_encoder_factory.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/frequency.h"
#include "api/units/time_delta.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/svc/scalability_mode_util.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"
#include "third_party/libyuv/include/libyuv/planar_functions.h"

namespace webrtc {
namespace test {

namespace {
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAreArray;
using ::testing::Values;
using ::testing::WithoutArgs;

using VideoCodecStats = VideoCodecTester::VideoCodecStats;
using VideoSourceSettings = VideoCodecTester::VideoSourceSettings;
using CodedVideoSource = VideoCodecTester::CodedVideoSource;
using EncodingSettings = VideoCodecTester::EncodingSettings;
using LayerSettings = EncodingSettings::LayerSettings;
using LayerId = VideoCodecTester::LayerId;
using DecoderSettings = VideoCodecTester::DecoderSettings;
using EncoderSettings = VideoCodecTester::EncoderSettings;
using PacingSettings = VideoCodecTester::PacingSettings;
using PacingMode = PacingSettings::PacingMode;
using Filter = VideoCodecStats::Filter;
using Frame = VideoCodecTester::VideoCodecStats::Frame;
using Stream = VideoCodecTester::VideoCodecStats::Stream;

constexpr int kWidth = 2;
constexpr int kHeight = 2;
const DataRate kTargetLayerBitrate = DataRate::BytesPerSec(100);
const Frequency kTargetFramerate = Frequency::Hertz(30);
constexpr Frequency k90kHz = Frequency::Hertz(90000);

rtc::scoped_refptr<I420Buffer> CreateYuvBuffer(uint8_t y = 0,
                                               uint8_t u = 0,
                                               uint8_t v = 0) {
  rtc::scoped_refptr<I420Buffer> buffer(I420Buffer::Create(2, 2));

  libyuv::I420Rect(buffer->MutableDataY(), buffer->StrideY(),
                   buffer->MutableDataU(), buffer->StrideU(),
                   buffer->MutableDataV(), buffer->StrideV(), 0, 0,
                   buffer->width(), buffer->height(), y, u, v);
  return buffer;
}

// TODO(ssilkin): Wrap this into a class that removes file in dtor.
std::string CreateYuvFile(int width, int height, int num_frames) {
  std::string path = webrtc::test::TempFilename(webrtc::test::OutputPath(),
                                                "video_codec_tester_unittest");
  FILE* file = fopen(path.c_str(), "wb");
  for (int frame_num = 0; frame_num < num_frames; ++frame_num) {
    // For purposes of testing quality estimation, we need Y, U, V values in
    // source and decoded video to be unique and deterministic. In source video
    // we make them functions of frame number. The test decoder makes them
    // functions of encoded frame size in decoded video.
    uint8_t y = (frame_num * 3 + 0) & 255;
    uint8_t u = (frame_num * 3 + 1) & 255;
    uint8_t v = (frame_num * 3 + 2) & 255;
    rtc::scoped_refptr<I420Buffer> buffer = CreateYuvBuffer(y, u, v);
    fwrite(buffer->DataY(), 1, width * height, file);
    int chroma_size_bytes = (width + 1) / 2 * (height + 1) / 2;
    fwrite(buffer->DataU(), 1, chroma_size_bytes, file);
    fwrite(buffer->DataV(), 1, chroma_size_bytes, file);
  }
  fclose(file);
  return path;
}

class TestVideoEncoder : public MockVideoEncoder {
 public:
  TestVideoEncoder(ScalabilityMode scalability_mode,
                   std::vector<std::vector<Frame>> encoded_frames)
      : scalability_mode_(scalability_mode), encoded_frames_(encoded_frames) {}
  int32_t Encode(const VideoFrame& input_frame,
                 const std::vector<VideoFrameType>*) override {
    for (const Frame& frame : encoded_frames_[num_encoded_frames_]) {
      if (frame.frame_size.IsZero()) {
        continue;  // Frame drop.
      }
      EncodedImage encoded_frame;
      encoded_frame._encodedWidth = frame.width;
      encoded_frame._encodedHeight = frame.height;
      encoded_frame.SetFrameType(frame.keyframe
                                     ? VideoFrameType::kVideoFrameKey
                                     : VideoFrameType::kVideoFrameDelta);
      encoded_frame.SetRtpTimestamp(input_frame.rtp_timestamp());
      encoded_frame.SetSpatialIndex(frame.layer_id.spatial_idx);
      encoded_frame.SetTemporalIndex(frame.layer_id.temporal_idx);
      encoded_frame.SetEncodedData(
          EncodedImageBuffer::Create(frame.frame_size.bytes()));
      CodecSpecificInfo codec_specific_info;
      codec_specific_info.scalability_mode = scalability_mode_;
      callback_->OnEncodedImage(encoded_frame, &codec_specific_info);
    }
    ++num_encoded_frames_;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t RegisterEncodeCompleteCallback(
      EncodedImageCallback* callback) override {
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
  }

 private:
  ScalabilityMode scalability_mode_;
  std::vector<std::vector<Frame>> encoded_frames_;
  int num_encoded_frames_ = 0;
  EncodedImageCallback* callback_;
};

class TestVideoDecoder : public MockVideoDecoder {
 public:
  int32_t Decode(const EncodedImage& encoded_frame, int64_t) {
    uint8_t y = (encoded_frame.size() + 0) & 255;
    uint8_t u = (encoded_frame.size() + 2) & 255;
    uint8_t v = (encoded_frame.size() + 4) & 255;
    rtc::scoped_refptr<I420Buffer> frame_buffer = CreateYuvBuffer(y, u, v);
    VideoFrame decoded_frame =
        VideoFrame::Builder()
            .set_video_frame_buffer(frame_buffer)
            .set_rtp_timestamp(encoded_frame.RtpTimestamp())
            .build();
    callback_->Decoded(decoded_frame);
    frame_sizes_.push_back(DataSize::Bytes(encoded_frame.size()));
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t RegisterDecodeCompleteCallback(DecodedImageCallback* callback) {
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  const std::vector<DataSize>& frame_sizes() const { return frame_sizes_; }

 private:
  DecodedImageCallback* callback_;
  std::vector<DataSize> frame_sizes_;
};

class VideoCodecTesterTest : public ::testing::Test {
 public:
  std::unique_ptr<VideoCodecStats> RunEncodeDecodeTest(
      std::string codec_type,
      ScalabilityMode scalability_mode,
      std::vector<std::vector<Frame>> encoded_frames) {
    int num_frames = encoded_frames.size();
    std::string yuv_path = CreateYuvFile(kWidth, kHeight, num_frames);
    VideoSourceSettings video_source_settings{
        .file_path = yuv_path,
        .resolution = {.width = kWidth, .height = kHeight},
        .framerate = kTargetFramerate};

    NiceMock<MockVideoEncoderFactory> encoder_factory;
    ON_CALL(encoder_factory, Create).WillByDefault(WithoutArgs([&] {
      return std::make_unique<NiceMock<TestVideoEncoder>>(scalability_mode,
                                                          encoded_frames);
    }));

    NiceMock<MockVideoDecoderFactory> decoder_factory;
    ON_CALL(decoder_factory, Create).WillByDefault(WithoutArgs([&] {
      // Video codec tester destroyes decoder at the end of test. Test
      // decoder collects stats which we need to access after test. To keep
      // the decode alive we wrap it into a wrapper and pass the wrapper to
      // the tester.
      class DecoderWrapper : public TestVideoDecoder {
       public:
        explicit DecoderWrapper(TestVideoDecoder* decoder)
            : decoder_(decoder) {}
        int32_t Decode(const EncodedImage& encoded_frame,
                       int64_t render_time_ms) {
          return decoder_->Decode(encoded_frame, render_time_ms);
        }
        int32_t RegisterDecodeCompleteCallback(DecodedImageCallback* callback) {
          return decoder_->RegisterDecodeCompleteCallback(callback);
        }
        TestVideoDecoder* decoder_;
      };
      decoders_.push_back(std::make_unique<NiceMock<TestVideoDecoder>>());
      return std::make_unique<NiceMock<DecoderWrapper>>(decoders_.back().get());
    }));

    int num_spatial_layers =
        ScalabilityModeToNumSpatialLayers(scalability_mode);
    int num_temporal_layers =
        ScalabilityModeToNumTemporalLayers(scalability_mode);
    std::map<uint32_t, EncodingSettings> encoding_settings;
    for (int frame_num = 0; frame_num < num_frames; ++frame_num) {
      std::map<LayerId, LayerSettings> layers_settings;
      for (int sidx = 0; sidx < num_spatial_layers; ++sidx) {
        for (int tidx = 0; tidx < num_temporal_layers; ++tidx) {
          layers_settings.emplace(
              LayerId{.spatial_idx = sidx, .temporal_idx = tidx},
              LayerSettings{
                  .resolution = {.width = kWidth, .height = kHeight},
                  .framerate = kTargetFramerate /
                               (1 << (num_temporal_layers - 1 - tidx)),
                  .bitrate = kTargetLayerBitrate});
        }
      }
      encoding_settings.emplace(
          encoded_frames[frame_num].front().timestamp_rtp,
          EncodingSettings{.sdp_video_format = SdpVideoFormat(codec_type),
                           .scalability_mode = scalability_mode,
                           .layers_settings = layers_settings});
    }

    std::unique_ptr<VideoCodecStats> stats =
        VideoCodecTester::RunEncodeDecodeTest(
            env_, video_source_settings, &encoder_factory, &decoder_factory,
            EncoderSettings{}, DecoderSettings{}, encoding_settings);

    remove(yuv_path.c_str());
    return stats;
  }

 protected:
  const Environment env_ = CreateEnvironment();
  std::vector<std::unique_ptr<TestVideoDecoder>> decoders_;
};

EncodedImage CreateEncodedImage(uint32_t timestamp_rtp) {
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(timestamp_rtp);
  return encoded_image;
}

class MockCodedVideoSource : public CodedVideoSource {
 public:
  MockCodedVideoSource(int num_frames, Frequency framerate)
      : num_frames_(num_frames), frame_num_(0), framerate_(framerate) {}

  absl::optional<EncodedImage> PullFrame() override {
    if (frame_num_ >= num_frames_) {
      return absl::nullopt;
    }
    uint32_t timestamp_rtp = frame_num_ * k90kHz / framerate_;
    ++frame_num_;
    return CreateEncodedImage(timestamp_rtp);
  }

 private:
  int num_frames_;
  int frame_num_;
  Frequency framerate_;
};

}  // namespace

TEST_F(VideoCodecTesterTest, Slice) {
  std::unique_ptr<VideoCodecStats> stats =
      RunEncodeDecodeTest("VP9", ScalabilityMode::kL2T2,
                          {{{.timestamp_rtp = 0,
                             .layer_id = {.spatial_idx = 0, .temporal_idx = 0},
                             .frame_size = DataSize::Bytes(1)},
                            {.timestamp_rtp = 0,
                             .layer_id = {.spatial_idx = 1, .temporal_idx = 0},
                             .frame_size = DataSize::Bytes(2)}},
                           {{.timestamp_rtp = 1,
                             .layer_id = {.spatial_idx = 0, .temporal_idx = 1},
                             .frame_size = DataSize::Bytes(3)}}});
  std::vector<Frame> slice = stats->Slice(Filter{}, /*merge=*/false);
  EXPECT_THAT(slice,
              ElementsAre(Field(&Frame::frame_size, DataSize::Bytes(1)),
                          Field(&Frame::frame_size, DataSize::Bytes(2)),
                          Field(&Frame::frame_size, DataSize::Bytes(3)),
                          Field(&Frame::frame_size, DataSize::Bytes(0))));

  slice = stats->Slice({.min_timestamp_rtp = 1}, /*merge=*/false);
  EXPECT_THAT(slice,
              ElementsAre(Field(&Frame::frame_size, DataSize::Bytes(3)),
                          Field(&Frame::frame_size, DataSize::Bytes(0))));

  slice = stats->Slice({.max_timestamp_rtp = 0}, /*merge=*/false);
  EXPECT_THAT(slice,
              ElementsAre(Field(&Frame::frame_size, DataSize::Bytes(1)),
                          Field(&Frame::frame_size, DataSize::Bytes(2))));

  slice = stats->Slice({.layer_id = {{.spatial_idx = 0, .temporal_idx = 0}}},
                       /*merge=*/false);
  EXPECT_THAT(slice,
              ElementsAre(Field(&Frame::frame_size, DataSize::Bytes(1))));

  slice = stats->Slice({.layer_id = {{.spatial_idx = 0, .temporal_idx = 1}}},
                       /*merge=*/false);
  EXPECT_THAT(slice,
              ElementsAre(Field(&Frame::frame_size, DataSize::Bytes(1)),
                          Field(&Frame::frame_size, DataSize::Bytes(3))));
}

TEST_F(VideoCodecTesterTest, Merge) {
  std::unique_ptr<VideoCodecStats> stats =
      RunEncodeDecodeTest("VP8", ScalabilityMode::kL2T2_KEY,
                          {{{.timestamp_rtp = 0,
                             .layer_id = {.spatial_idx = 0, .temporal_idx = 0},
                             .frame_size = DataSize::Bytes(1),
                             .keyframe = true},
                            {.timestamp_rtp = 0,
                             .layer_id = {.spatial_idx = 1, .temporal_idx = 0},
                             .frame_size = DataSize::Bytes(2)}},
                           {{.timestamp_rtp = 1,
                             .layer_id = {.spatial_idx = 0, .temporal_idx = 1},
                             .frame_size = DataSize::Bytes(4)},
                            {.timestamp_rtp = 1,
                             .layer_id = {.spatial_idx = 1, .temporal_idx = 1},
                             .frame_size = DataSize::Bytes(8)}}});

  std::vector<Frame> slice = stats->Slice(Filter{}, /*merge=*/true);
  EXPECT_THAT(
      slice,
      ElementsAre(
          AllOf(Field(&Frame::timestamp_rtp, 0), Field(&Frame::keyframe, true),
                Field(&Frame::frame_size, DataSize::Bytes(3))),
          AllOf(Field(&Frame::timestamp_rtp, 1), Field(&Frame::keyframe, false),
                Field(&Frame::frame_size, DataSize::Bytes(12)))));
}

struct AggregationTestParameters {
  Filter filter;
  double expected_keyframe_sum;
  double expected_encoded_bitrate_kbps;
  double expected_encoded_framerate_fps;
  double expected_bitrate_mismatch_pct;
  double expected_framerate_mismatch_pct;
};

class VideoCodecTesterTestAggregation
    : public VideoCodecTesterTest,
      public ::testing::WithParamInterface<AggregationTestParameters> {};

TEST_P(VideoCodecTesterTestAggregation, Aggregate) {
  AggregationTestParameters test_params = GetParam();
  std::unique_ptr<VideoCodecStats> stats =
      RunEncodeDecodeTest("VP8", ScalabilityMode::kL2T2_KEY,
                          {{// L0T0
                            {.timestamp_rtp = 0,
                             .layer_id = {.spatial_idx = 0, .temporal_idx = 0},
                             .frame_size = DataSize::Bytes(1),
                             .keyframe = true},
                            // L1T0
                            {.timestamp_rtp = 0,
                             .layer_id = {.spatial_idx = 1, .temporal_idx = 0},
                             .frame_size = DataSize::Bytes(2)}},
                           // Emulate frame drop (frame_size = 0).
                           {{.timestamp_rtp = 3000,
                             .layer_id = {.spatial_idx = 0, .temporal_idx = 0},
                             .frame_size = DataSize::Zero()}},
                           {// L0T1
                            {.timestamp_rtp = 87000,
                             .layer_id = {.spatial_idx = 0, .temporal_idx = 1},
                             .frame_size = DataSize::Bytes(4)},
                            // L1T1
                            {.timestamp_rtp = 87000,
                             .layer_id = {.spatial_idx = 1, .temporal_idx = 1},
                             .frame_size = DataSize::Bytes(8)}}});

  Stream stream = stats->Aggregate(test_params.filter);
  EXPECT_EQ(stream.keyframe.GetSum(), test_params.expected_keyframe_sum);
  EXPECT_EQ(stream.encoded_bitrate_kbps.GetAverage(),
            test_params.expected_encoded_bitrate_kbps);
  EXPECT_EQ(stream.encoded_framerate_fps.GetAverage(),
            test_params.expected_encoded_framerate_fps);
  EXPECT_EQ(stream.bitrate_mismatch_pct.GetAverage(),
            test_params.expected_bitrate_mismatch_pct);
  EXPECT_EQ(stream.framerate_mismatch_pct.GetAverage(),
            test_params.expected_framerate_mismatch_pct);
}

INSTANTIATE_TEST_SUITE_P(
    All,
    VideoCodecTesterTestAggregation,
    Values(
        // No filtering.
        AggregationTestParameters{
            .filter = {},
            .expected_keyframe_sum = 1,
            .expected_encoded_bitrate_kbps =
                DataRate::BytesPerSec(15).kbps<double>(),
            .expected_encoded_framerate_fps = 2,
            .expected_bitrate_mismatch_pct =
                100 * (15.0 / (kTargetLayerBitrate.bytes_per_sec() * 4) - 1),
            .expected_framerate_mismatch_pct =
                100 * (2.0 / kTargetFramerate.hertz() - 1)},
        // L0T0
        AggregationTestParameters{
            .filter = {.layer_id = {{.spatial_idx = 0, .temporal_idx = 0}}},
            .expected_keyframe_sum = 1,
            .expected_encoded_bitrate_kbps =
                DataRate::BytesPerSec(1).kbps<double>(),
            .expected_encoded_framerate_fps = 1,
            .expected_bitrate_mismatch_pct =
                100 * (1.0 / kTargetLayerBitrate.bytes_per_sec() - 1),
            .expected_framerate_mismatch_pct =
                100 * (1.0 / (kTargetFramerate.hertz() / 2) - 1)},
        // L0T1
        AggregationTestParameters{
            .filter = {.layer_id = {{.spatial_idx = 0, .temporal_idx = 1}}},
            .expected_keyframe_sum = 1,
            .expected_encoded_bitrate_kbps =
                DataRate::BytesPerSec(5).kbps<double>(),
            .expected_encoded_framerate_fps = 2,
            .expected_bitrate_mismatch_pct =
                100 * (5.0 / (kTargetLayerBitrate.bytes_per_sec() * 2) - 1),
            .expected_framerate_mismatch_pct =
                100 * (2.0 / kTargetFramerate.hertz() - 1)},
        // L1T0
        AggregationTestParameters{
            .filter = {.layer_id = {{.spatial_idx = 1, .temporal_idx = 0}}},
            .expected_keyframe_sum = 1,
            .expected_encoded_bitrate_kbps =
                DataRate::BytesPerSec(3).kbps<double>(),
            .expected_encoded_framerate_fps = 1,
            .expected_bitrate_mismatch_pct =
                100 * (3.0 / kTargetLayerBitrate.bytes_per_sec() - 1),
            .expected_framerate_mismatch_pct =
                100 * (1.0 / (kTargetFramerate.hertz() / 2) - 1)},
        // L1T1
        AggregationTestParameters{
            .filter = {.layer_id = {{.spatial_idx = 1, .temporal_idx = 1}}},
            .expected_keyframe_sum = 1,
            .expected_encoded_bitrate_kbps =
                DataRate::BytesPerSec(11).kbps<double>(),
            .expected_encoded_framerate_fps = 2,
            .expected_bitrate_mismatch_pct =
                100 * (11.0 / (kTargetLayerBitrate.bytes_per_sec() * 2) - 1),
            .expected_framerate_mismatch_pct =
                100 * (2.0 / kTargetFramerate.hertz() - 1)}));

TEST_F(VideoCodecTesterTest, Psnr) {
  std::unique_ptr<VideoCodecStats> stats = RunEncodeDecodeTest(
      "VP8", ScalabilityMode::kL1T1,
      {{{.timestamp_rtp = 0, .frame_size = DataSize::Bytes(2)}},
       {{.timestamp_rtp = 3000, .frame_size = DataSize::Bytes(6)}}});

  std::vector<Frame> slice = stats->Slice(Filter{}, /*merge=*/false);
  ASSERT_THAT(slice, SizeIs(2));
  ASSERT_TRUE(slice[0].psnr.has_value());
  ASSERT_TRUE(slice[1].psnr.has_value());
  EXPECT_NEAR(slice[0].psnr->y, 42, 1);
  EXPECT_NEAR(slice[0].psnr->u, 38, 1);
  EXPECT_NEAR(slice[0].psnr->v, 36, 1);
  EXPECT_NEAR(slice[1].psnr->y, 38, 1);
  EXPECT_NEAR(slice[1].psnr->u, 36, 1);
  EXPECT_NEAR(slice[1].psnr->v, 34, 1);
}

struct ScalabilityTestParameters {
  std::string codec_type;
  ScalabilityMode scalability_mode;
  // Temporal unit -> spatial layer -> frame size.
  std::vector<std::map<int, DataSize>> encoded_frame_sizes;
  std::vector<DataSize> expected_decode_frame_sizes;
};

class VideoCodecTesterTestScalability
    : public VideoCodecTesterTest,
      public ::testing::WithParamInterface<ScalabilityTestParameters> {};

TEST_P(VideoCodecTesterTestScalability, EncodeDecode) {
  ScalabilityTestParameters test_params = GetParam();
  std::vector<std::vector<Frame>> frames;
  for (size_t frame_num = 0; frame_num < test_params.encoded_frame_sizes.size();
       ++frame_num) {
    std::vector<Frame> temporal_unit;
    for (auto [sidx, frame_size] : test_params.encoded_frame_sizes[frame_num]) {
      temporal_unit.push_back(
          Frame{.timestamp_rtp = static_cast<uint32_t>(3000 * frame_num),
                .layer_id = {.spatial_idx = sidx, .temporal_idx = 0},
                .frame_size = frame_size,
                .keyframe = (frame_num == 0 && sidx == 0)});
    }
    frames.push_back(temporal_unit);
  }
  RunEncodeDecodeTest(test_params.codec_type, test_params.scalability_mode,
                      frames);

  size_t num_spatial_layers =
      ScalabilityModeToNumSpatialLayers(test_params.scalability_mode);
  EXPECT_EQ(num_spatial_layers, decoders_.size());

  // Collect input frame sizes from all decoders.
  std::vector<DataSize> decode_frame_sizes;
  for (const auto& decoder : decoders_) {
    const auto& frame_sizes = decoder->frame_sizes();
    decode_frame_sizes.insert(decode_frame_sizes.end(), frame_sizes.begin(),
                              frame_sizes.end());
  }
  EXPECT_THAT(decode_frame_sizes, UnorderedElementsAreArray(
                                      test_params.expected_decode_frame_sizes));
}

INSTANTIATE_TEST_SUITE_P(
    All,
    VideoCodecTesterTestScalability,
    Values(
        ScalabilityTestParameters{
            .codec_type = "VP8",
            .scalability_mode = ScalabilityMode::kS2T1,
            .encoded_frame_sizes = {{{0, DataSize::Bytes(1)},
                                     {1, DataSize::Bytes(2)}},
                                    {{0, DataSize::Bytes(4)},
                                     // Emulate frame drop.
                                     {1, DataSize::Bytes(0)}}},
            .expected_decode_frame_sizes = {DataSize::Bytes(1),
                                            DataSize::Bytes(2),
                                            DataSize::Bytes(4)},
        },
        ScalabilityTestParameters{
            .codec_type = "VP9",
            .scalability_mode = ScalabilityMode::kL2T1,
            .encoded_frame_sizes =
                {{{0, DataSize::Bytes(1)}, {1, DataSize::Bytes(2)}},
                 {{0, DataSize::Bytes(4)}, {1, DataSize::Bytes(8)}},
                 {{0, DataSize::Bytes(16)},
                  // Emulate frame drop.
                  {1, DataSize::Bytes(0)}}},
            .expected_decode_frame_sizes =
                {DataSize::Bytes(1), DataSize::Bytes(3), DataSize::Bytes(4),
                 DataSize::Bytes(12), DataSize::Bytes(16), DataSize::Bytes(16)},
        },
        ScalabilityTestParameters{
            .codec_type = "VP9",
            .scalability_mode = ScalabilityMode::kL2T1_KEY,
            .encoded_frame_sizes =
                {{{0, DataSize::Bytes(1)}, {1, DataSize::Bytes(2)}},
                 {{0, DataSize::Bytes(4)}, {1, DataSize::Bytes(8)}},
                 {{0, DataSize::Bytes(16)},
                  // Emulate frame drop.
                  {1, DataSize::Bytes(0)}}},
            .expected_decode_frame_sizes =
                {DataSize::Bytes(1), DataSize::Bytes(3), DataSize::Bytes(4),
                 DataSize::Bytes(8), DataSize::Bytes(16)},
        },
        ScalabilityTestParameters{
            .codec_type = "VP9",
            .scalability_mode = ScalabilityMode::kS2T1,
            .encoded_frame_sizes =
                {{{0, DataSize::Bytes(1)}, {1, DataSize::Bytes(2)}},
                 {{0, DataSize::Bytes(4)}, {1, DataSize::Bytes(8)}},
                 {{0, DataSize::Bytes(16)},
                  // Emulate frame drop.
                  {1, DataSize::Bytes(0)}}},
            .expected_decode_frame_sizes =
                {DataSize::Bytes(1), DataSize::Bytes(2), DataSize::Bytes(4),
                 DataSize::Bytes(8), DataSize::Bytes(16)},
        }));

class VideoCodecTesterTestPacing
    : public ::testing::TestWithParam<std::tuple<PacingSettings, int>> {
 public:
  const int kSourceWidth = 2;
  const int kSourceHeight = 2;
  const int kNumFrames = 3;
  const int kTargetLayerBitrateKbps = 128;
  const Frequency kTargetFramerate = Frequency::Hertz(10);

  void SetUp() override {
    source_yuv_file_path_ = CreateYuvFile(kSourceWidth, kSourceHeight, 1);
  }

  void TearDown() override { remove(source_yuv_file_path_.c_str()); }

 protected:
  const Environment env_ = CreateEnvironment();
  std::string source_yuv_file_path_;
};

TEST_P(VideoCodecTesterTestPacing, PaceEncode) {
  auto [pacing_settings, expected_delta_ms] = GetParam();
  const Environment env = CreateEnvironment();
  VideoSourceSettings video_source{
      .file_path = source_yuv_file_path_,
      .resolution = {.width = kSourceWidth, .height = kSourceHeight},
      .framerate = kTargetFramerate};

  NiceMock<MockVideoEncoderFactory> encoder_factory;
  ON_CALL(encoder_factory, Create).WillByDefault(WithoutArgs([] {
    return std::make_unique<NiceMock<MockVideoEncoder>>();
  }));

  std::map<uint32_t, EncodingSettings> encoding_settings =
      VideoCodecTester::CreateEncodingSettings(
          "VP8", "L1T1", kSourceWidth, kSourceHeight, {kTargetLayerBitrateKbps},
          kTargetFramerate.hertz(), kNumFrames);

  EncoderSettings encoder_settings;
  encoder_settings.pacing_settings = pacing_settings;
  std::vector<Frame> frames =
      VideoCodecTester::RunEncodeTest(env, video_source, &encoder_factory,
                                      encoder_settings, encoding_settings)
          ->Slice(/*filter=*/{}, /*merge=*/false);
  ASSERT_THAT(frames, SizeIs(kNumFrames));
  EXPECT_NEAR((frames[1].encode_start - frames[0].encode_start).ms(),
              expected_delta_ms, 10);
  EXPECT_NEAR((frames[2].encode_start - frames[1].encode_start).ms(),
              expected_delta_ms, 10);
}

TEST_P(VideoCodecTesterTestPacing, PaceDecode) {
  auto [pacing_settings, expected_delta_ms] = GetParam();
  MockCodedVideoSource video_source(kNumFrames, kTargetFramerate);

  NiceMock<MockVideoDecoderFactory> decoder_factory;
  ON_CALL(decoder_factory, Create).WillByDefault(WithoutArgs([] {
    return std::make_unique<NiceMock<MockVideoDecoder>>();
  }));

  DecoderSettings decoder_settings;
  decoder_settings.pacing_settings = pacing_settings;
  std::vector<Frame> frames =
      VideoCodecTester::RunDecodeTest(env_, &video_source, &decoder_factory,
                                      decoder_settings, SdpVideoFormat::VP8())
          ->Slice(/*filter=*/{}, /*merge=*/false);
  ASSERT_THAT(frames, SizeIs(kNumFrames));
  EXPECT_NEAR((frames[1].decode_start - frames[0].decode_start).ms(),
              expected_delta_ms, 10);
  EXPECT_NEAR((frames[2].decode_start - frames[1].decode_start).ms(),
              expected_delta_ms, 10);
}

INSTANTIATE_TEST_SUITE_P(
    DISABLED_All,
    VideoCodecTesterTestPacing,
    Values(
        // No pacing.
        std::make_tuple(PacingSettings{.mode = PacingMode::kNoPacing},
                        /*expected_delta_ms=*/0),
        // Real-time pacing.
        std::make_tuple(PacingSettings{.mode = PacingMode::kRealTime},
                        /*expected_delta_ms=*/100),
        // Pace with specified constant rate.
        std::make_tuple(PacingSettings{.mode = PacingMode::kConstantRate,
                                       .constant_rate = Frequency::Hertz(20)},
                        /*expected_delta_ms=*/50)));

struct EncodingSettingsTestParameters {
  std::string codec_type;
  std::string scalability_mode;
  std::vector<int> bitrate_kbps;
  std::vector<int> expected_bitrate_kbps;
};

class VideoCodecTesterTestEncodingSettings
    : public ::testing::TestWithParam<EncodingSettingsTestParameters> {};

TEST_P(VideoCodecTesterTestEncodingSettings, CreateEncodingSettings) {
  EncodingSettingsTestParameters test_params = GetParam();
  std::map<uint32_t, EncodingSettings> encoding_settings =
      VideoCodecTester::CreateEncodingSettings(
          test_params.codec_type, test_params.scalability_mode, /*width=*/1280,
          /*height=*/720, test_params.bitrate_kbps, /*framerate_fps=*/30,
          /*num_frames=*/1);
  ASSERT_THAT(encoding_settings, SizeIs(1));
  const std::map<LayerId, LayerSettings>& layers_settings =
      encoding_settings.begin()->second.layers_settings;
  std::vector<int> configured_bitrate_kbps;
  std::transform(layers_settings.begin(), layers_settings.end(),
                 std::back_inserter(configured_bitrate_kbps),
                 [](const auto& layer_settings) {
                   return layer_settings.second.bitrate.kbps();
                 });
  EXPECT_EQ(configured_bitrate_kbps, test_params.expected_bitrate_kbps);
}

INSTANTIATE_TEST_SUITE_P(
    Vp8,
    VideoCodecTesterTestEncodingSettings,
    Values(EncodingSettingsTestParameters{.codec_type = "VP8",
                                          .scalability_mode = "L1T1",
                                          .bitrate_kbps = {1},
                                          .expected_bitrate_kbps = {1}},
           EncodingSettingsTestParameters{.codec_type = "VP8",
                                          .scalability_mode = "L1T1",
                                          .bitrate_kbps = {10000},
                                          .expected_bitrate_kbps = {10000}},
           EncodingSettingsTestParameters{
               .codec_type = "VP8",
               .scalability_mode = "L1T3",
               .bitrate_kbps = {1000},
               .expected_bitrate_kbps = {400, 200, 400}},
           EncodingSettingsTestParameters{
               .codec_type = "VP8",
               .scalability_mode = "S3T3",
               .bitrate_kbps = {100},
               .expected_bitrate_kbps = {40, 20, 40, 0, 0, 0, 0, 0, 0}},
           EncodingSettingsTestParameters{
               .codec_type = "VP8",
               .scalability_mode = "S3T3",
               .bitrate_kbps = {10000},
               .expected_bitrate_kbps = {60, 30, 60, 200, 100, 200, 1000, 500,
                                         1000}},
           EncodingSettingsTestParameters{
               .codec_type = "VP8",
               .scalability_mode = "S3T3",
               .bitrate_kbps = {100, 200, 300, 400, 500, 600, 700, 800, 900},
               .expected_bitrate_kbps = {100, 200, 300, 400, 500, 600, 700, 800,
                                         900}}));

INSTANTIATE_TEST_SUITE_P(
    Vp9,
    VideoCodecTesterTestEncodingSettings,
    Values(EncodingSettingsTestParameters{.codec_type = "VP9",
                                          .scalability_mode = "L1T1",
                                          .bitrate_kbps = {1},
                                          .expected_bitrate_kbps = {1}},
           EncodingSettingsTestParameters{.codec_type = "VP9",
                                          .scalability_mode = "L1T1",
                                          .bitrate_kbps = {10000},
                                          .expected_bitrate_kbps = {10000}},
           EncodingSettingsTestParameters{
               .codec_type = "VP9",
               .scalability_mode = "L1T3",
               .bitrate_kbps = {1000},
               .expected_bitrate_kbps = {540, 163, 297}},
           EncodingSettingsTestParameters{
               .codec_type = "VP9",
               .scalability_mode = "L3T3",
               .bitrate_kbps = {100},
               .expected_bitrate_kbps = {54, 16, 30, 0, 0, 0, 0, 0, 0}},
           EncodingSettingsTestParameters{
               .codec_type = "VP9",
               .scalability_mode = "L3T3",
               .bitrate_kbps = {10000},
               .expected_bitrate_kbps = {77, 23, 42, 226, 68, 124, 823, 249,
                                         452}},
           EncodingSettingsTestParameters{
               .codec_type = "VP9",
               .scalability_mode = "L3T3",
               .bitrate_kbps = {100, 200, 300, 400, 500, 600, 700, 800, 900},
               .expected_bitrate_kbps = {100, 200, 300, 400, 500, 600, 700, 800,
                                         900}}));

INSTANTIATE_TEST_SUITE_P(
    Av1,
    VideoCodecTesterTestEncodingSettings,
    Values(EncodingSettingsTestParameters{.codec_type = "AV1",
                                          .scalability_mode = "L1T1",
                                          .bitrate_kbps = {1},
                                          .expected_bitrate_kbps = {1}},
           EncodingSettingsTestParameters{.codec_type = "AV1",
                                          .scalability_mode = "L1T1",
                                          .bitrate_kbps = {10000},
                                          .expected_bitrate_kbps = {10000}},
           EncodingSettingsTestParameters{
               .codec_type = "AV1",
               .scalability_mode = "L1T3",
               .bitrate_kbps = {1000},
               .expected_bitrate_kbps = {540, 163, 297}},
           EncodingSettingsTestParameters{
               .codec_type = "AV1",
               .scalability_mode = "L3T3",
               .bitrate_kbps = {100},
               .expected_bitrate_kbps = {54, 16, 30, 0, 0, 0, 0, 0, 0}},
           EncodingSettingsTestParameters{
               .codec_type = "AV1",
               .scalability_mode = "L3T3",
               .bitrate_kbps = {10000},
               .expected_bitrate_kbps = {77, 23, 42, 226, 68, 124, 823, 249,
                                         452}},
           EncodingSettingsTestParameters{
               .codec_type = "AV1",
               .scalability_mode = "L3T3",
               .bitrate_kbps = {100, 200, 300, 400, 500, 600, 700, 800, 900},
               .expected_bitrate_kbps = {100, 200, 300, 400, 500, 600, 700, 800,
                                         900}}));

}  // namespace test
}  // namespace webrtc
