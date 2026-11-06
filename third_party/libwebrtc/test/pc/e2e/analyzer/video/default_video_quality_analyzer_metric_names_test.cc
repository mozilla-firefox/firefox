/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>
#include <string>
#include <vector>

#include "api/rtp_packet_info.h"
#include "api/rtp_packet_infos.h"
#include "api/test/create_frame_generator.h"
#include "api/test/metrics/metric.h"
#include "api/test/metrics/metrics_logger.h"
#include "api/test/metrics/stdout_metrics_exporter.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "rtc_tools/frame_analyzer/video_geometry_aligner.h"
#include "system_wrappers/include/sleep.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/pc/e2e/analyzer/video/default_video_quality_analyzer.h"

namespace webrtc {
namespace {

using ::testing::Contains;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

using ::webrtc::test::DefaultMetricsLogger;
using ::webrtc::test::ImprovementDirection;
using ::webrtc::test::Metric;
using ::webrtc::test::MetricsExporter;
using ::webrtc::test::StdoutMetricsExporter;
using ::webrtc::test::Unit;

constexpr int kAnalyzerMaxThreadsCount = 1;
constexpr int kFrameWidth = 320;
constexpr int kFrameHeight = 240;

DefaultVideoQualityAnalyzerOptions AnalyzerOptionsForTest() {
  DefaultVideoQualityAnalyzerOptions options;
  options.compute_psnr = true;
  options.compute_ssim = true;
  options.adjust_cropping_before_comparing_frames = false;
  options.report_detailed_frame_stats = true;
  return options;
}

VideoFrame NextFrame(test::FrameGeneratorInterface* frame_generator,
                     int64_t timestamp_us) {
  test::FrameGeneratorInterface::VideoFrameData frame_data =
      frame_generator->NextFrame();
  return VideoFrame::Builder()
      .set_video_frame_buffer(frame_data.buffer)
      .set_update_rect(frame_data.update_rect)
      .set_timestamp_us(timestamp_us)
      .build();
}

EncodedImage FakeEncode(const VideoFrame& frame) {
  EncodedImage image;
  std::vector<RtpPacketInfo> packet_infos;
  packet_infos.push_back(RtpPacketInfo(
      /*ssrc=*/1,
      /*csrcs=*/{},
      /*rtp_timestamp=*/frame.rtp_timestamp(),
      /*receive_time=*/Timestamp::Micros(frame.timestamp_us() + 10000)));
  image.SetPacketInfos(RtpPacketInfos(packet_infos));
  return image;
}

VideoFrame DeepCopy(const VideoFrame& frame) {
  VideoFrame copy = frame;
  copy.set_video_frame_buffer(
      I420Buffer::Copy(*frame.video_frame_buffer()->ToI420()));
  return copy;
}

void PassFramesThroughAnalyzer(DefaultVideoQualityAnalyzer& analyzer,
                               absl::string_view sender,
                               absl::string_view stream_label,
                               std::vector<absl::string_view> receivers,
                               int frames_count,
                               test::FrameGeneratorInterface& frame_generator,
                               int interframe_delay_ms = 0) {
  for (int i = 0; i < frames_count; ++i) {
    VideoFrame frame = NextFrame(&frame_generator, /*timestamp_us=*/1);
    uint16_t frame_id =
        analyzer.OnFrameCaptured(sender, std::string(stream_label), frame);
    frame.set_id(frame_id);
    analyzer.OnFramePreEncode(sender, frame);
    analyzer.OnFrameEncoded(sender, frame.id(), FakeEncode(frame),
                            VideoQualityAnalyzerInterface::EncoderStats(),
                            false);
    for (absl::string_view receiver : receivers) {
      VideoFrame received_frame = DeepCopy(frame);
      analyzer.OnFramePreDecode(receiver, received_frame.id(),
                                FakeEncode(received_frame));
      analyzer.OnFrameDecoded(receiver, received_frame,
                              VideoQualityAnalyzerInterface::DecoderStats());
      analyzer.OnFrameRendered(receiver, received_frame);
    }
    if (i < frames_count - 1 && interframe_delay_ms > 0) {
      SleepMs(interframe_delay_ms);
    }
  }
}

// Metric fields to assert on
struct MetricValidationInfo {
  std::string test_case;
  std::string name;
  Unit unit;
  ImprovementDirection improvement_direction;
};

bool operator==(const MetricValidationInfo& a, const MetricValidationInfo& b) {
  return a.name == b.name && a.test_case == b.test_case && a.unit == b.unit &&
         a.improvement_direction == b.improvement_direction;
}

std::ostream& operator<<(std::ostream& os, const MetricValidationInfo& m) {
  os << "{ test_case=" << m.test_case << "; name=" << m.name
     << "; unit=" << test::ToString(m.unit)
     << "; improvement_direction=" << test::ToString(m.improvement_direction)
     << " }";
  return os;
}

std::vector<MetricValidationInfo> ToValidationInfo(
    const std::vector<Metric>& metrics) {
  std::vector<MetricValidationInfo> out;
  for (const Metric& m : metrics) {
    out.push_back(
        MetricValidationInfo{.test_case = m.test_case,
                             .name = m.name,
                             .unit = m.unit,
                             .improvement_direction = m.improvement_direction});
  }
  return out;
}

std::vector<std::string> ToTestCases(const std::vector<Metric>& metrics) {
  std::vector<std::string> out;
  for (const Metric& m : metrics) {
    out.push_back(m.test_case);
  }
  return out;
}

TEST(DefaultVideoQualityAnalyzerMetricNamesTest, MetricNamesForP2PAreCorrect) {
  std::unique_ptr<test::FrameGeneratorInterface> frame_generator =
      test::CreateSquareFrameGenerator(kFrameWidth, kFrameHeight,
                                       /*type=*/absl::nullopt,
                                       /*num_squares=*/absl::nullopt);

  DefaultVideoQualityAnalyzerOptions options = AnalyzerOptionsForTest();
  DefaultMetricsLogger metrics_logger(Clock::GetRealTimeClock());
  DefaultVideoQualityAnalyzer analyzer(Clock::GetRealTimeClock(),
                                       &metrics_logger, options);
  analyzer.Start("test_case", std::vector<std::string>{"alice", "bob"},
                 kAnalyzerMaxThreadsCount);

  PassFramesThroughAnalyzer(analyzer, "alice", "alice_video", {"bob"},
                            /*frames_count=*/5, *frame_generator,
                            /*interframe_delay_ms=*/50);
  analyzer.Stop();

  std::vector<MetricValidationInfo> metrics =
      ToValidationInfo(metrics_logger.GetCollectedMetrics());
  EXPECT_THAT(
      metrics,
      UnorderedElementsAre(
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "psnr_dB",
              .unit = Unit::kUnitless,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "ssim",
              .unit = Unit::kUnitless,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "transport_time",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "total_delay_incl_transport",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "time_between_rendered_frames",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "harmonic_framerate",
              .unit = Unit::kHertz,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "encode_frame_rate",
              .unit = Unit::kHertz,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "encode_time",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "time_between_freezes",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "freeze_time_ms",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "pixels_per_frame",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "min_psnr_dB",
              .unit = Unit::kUnitless,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "decode_time",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "receive_to_render_time",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "dropped_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "frames_in_flight",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "rendered_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "max_skipped",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "target_encode_bitrate",
              .unit = Unit::kKilobitsPerSecond,
              .improvement_direction = ImprovementDirection::kNeitherIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "qp_sl0",
              .unit = Unit::kUnitless,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "actual_encode_bitrate",
              .unit = Unit::kKilobitsPerSecond,
              .improvement_direction = ImprovementDirection::kNeitherIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "capture_frame_rate",
              .unit = Unit::kHertz,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "num_encoded_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "num_decoded_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "num_send_key_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "num_recv_key_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "recv_key_frame_size_bytes",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video",
              .name = "recv_delta_frame_size_bytes",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{.test_case = "test_case",
                               .name = "cpu_usage_%",
                               .unit = Unit::kUnitless,
                               .improvement_direction =
                                   ImprovementDirection::kSmallerIsBetter}));
}

TEST(DefaultVideoQualityAnalyzerMetricNamesTest,
     MetricNamesFor3PeersAreCorrect) {
  std::unique_ptr<test::FrameGeneratorInterface> frame_generator =
      test::CreateSquareFrameGenerator(kFrameWidth, kFrameHeight,
                                       /*type=*/absl::nullopt,
                                       /*num_squares=*/absl::nullopt);

  DefaultVideoQualityAnalyzerOptions options = AnalyzerOptionsForTest();
  DefaultMetricsLogger metrics_logger(Clock::GetRealTimeClock());
  DefaultVideoQualityAnalyzer analyzer(Clock::GetRealTimeClock(),
                                       &metrics_logger, options);
  analyzer.Start("test_case",
                 std::vector<std::string>{"alice", "bob", "charlie"},
                 kAnalyzerMaxThreadsCount);

  PassFramesThroughAnalyzer(analyzer, "alice", "alice_video",
                            {"bob", "charlie"},
                            /*frames_count=*/5, *frame_generator,
                            /*interframe_delay_ms=*/50);
  analyzer.Stop();

  std::vector<MetricValidationInfo> metrics =
      ToValidationInfo(metrics_logger.GetCollectedMetrics());
  EXPECT_THAT(
      metrics,
      UnorderedElementsAre(
          // Bob
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "psnr_dB",
              .unit = Unit::kUnitless,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "ssim",
              .unit = Unit::kUnitless,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "transport_time",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "total_delay_incl_transport",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "time_between_rendered_frames",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "harmonic_framerate",
              .unit = Unit::kHertz,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "encode_frame_rate",
              .unit = Unit::kHertz,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "encode_time",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "time_between_freezes",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "freeze_time_ms",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "pixels_per_frame",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "min_psnr_dB",
              .unit = Unit::kUnitless,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "decode_time",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "receive_to_render_time",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "dropped_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "frames_in_flight",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "rendered_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "max_skipped",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "target_encode_bitrate",
              .unit = Unit::kKilobitsPerSecond,
              .improvement_direction = ImprovementDirection::kNeitherIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "qp_sl0",
              .unit = Unit::kUnitless,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "actual_encode_bitrate",
              .unit = Unit::kKilobitsPerSecond,
              .improvement_direction = ImprovementDirection::kNeitherIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "capture_frame_rate",
              .unit = Unit::kHertz,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "num_encoded_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "num_decoded_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "num_send_key_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "num_recv_key_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "recv_key_frame_size_bytes",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_bob",
              .name = "recv_delta_frame_size_bytes",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},

          // Charlie
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "psnr_dB",
              .unit = Unit::kUnitless,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "ssim",
              .unit = Unit::kUnitless,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "transport_time",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "total_delay_incl_transport",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "time_between_rendered_frames",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "harmonic_framerate",
              .unit = Unit::kHertz,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "encode_frame_rate",
              .unit = Unit::kHertz,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "encode_time",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "time_between_freezes",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "freeze_time_ms",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "pixels_per_frame",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "min_psnr_dB",
              .unit = Unit::kUnitless,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "decode_time",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "receive_to_render_time",
              .unit = Unit::kMilliseconds,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "dropped_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "frames_in_flight",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "rendered_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "max_skipped",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "target_encode_bitrate",
              .unit = Unit::kKilobitsPerSecond,
              .improvement_direction = ImprovementDirection::kNeitherIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "qp_sl0",
              .unit = Unit::kUnitless,
              .improvement_direction = ImprovementDirection::kSmallerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "actual_encode_bitrate",
              .unit = Unit::kKilobitsPerSecond,
              .improvement_direction = ImprovementDirection::kNeitherIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "capture_frame_rate",
              .unit = Unit::kHertz,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "num_encoded_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "num_decoded_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "num_send_key_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "num_recv_key_frames",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "recv_key_frame_size_bytes",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{
              .test_case = "test_case/alice_video_alice_charlie",
              .name = "recv_delta_frame_size_bytes",
              .unit = Unit::kCount,
              .improvement_direction = ImprovementDirection::kBiggerIsBetter},
          MetricValidationInfo{.test_case = "test_case",
                               .name = "cpu_usage_%",
                               .unit = Unit::kUnitless,
                               .improvement_direction =
                                   ImprovementDirection::kSmallerIsBetter}));
}

TEST(DefaultVideoQualityAnalyzerMetricNamesTest,
     TestCaseFor3PeerIsTheSameAfterAllPeersLeft) {
  std::unique_ptr<test::FrameGeneratorInterface> frame_generator =
      test::CreateSquareFrameGenerator(kFrameWidth, kFrameHeight,
                                       /*type=*/absl::nullopt,
                                       /*num_squares=*/absl::nullopt);

  DefaultVideoQualityAnalyzerOptions options = AnalyzerOptionsForTest();
  DefaultMetricsLogger metrics_logger(Clock::GetRealTimeClock());
  DefaultVideoQualityAnalyzer analyzer(Clock::GetRealTimeClock(),
                                       &metrics_logger, options);
  analyzer.Start("test_case",
                 std::vector<std::string>{"alice", "bob", "charlie"},
                 kAnalyzerMaxThreadsCount);

  PassFramesThroughAnalyzer(analyzer, "alice", "alice_video",
                            {"bob", "charlie"},
                            /*frames_count=*/5, *frame_generator,
                            /*interframe_delay_ms=*/50);
  analyzer.UnregisterParticipantInCall("alice");
  analyzer.UnregisterParticipantInCall("bob");
  analyzer.UnregisterParticipantInCall("charlie");
  analyzer.Stop();

  std::vector<std::string> metrics =
      ToTestCases(metrics_logger.GetCollectedMetrics());
  EXPECT_THAT(metrics, SizeIs(57));
  EXPECT_THAT(metrics, Contains("test_case/alice_video_alice_bob").Times(28));
  EXPECT_THAT(metrics,
              Contains("test_case/alice_video_alice_charlie").Times(28));
  EXPECT_THAT(metrics, Contains("test_case").Times(1));
}

}  // namespace
}  // namespace webrtc
