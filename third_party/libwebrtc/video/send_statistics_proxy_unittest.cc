/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/send_statistics_proxy.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"
#include "api/environment/environment.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/video_adaptation_counters.h"
#include "api/video/video_adaptation_reason.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_codec_type.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/video_codec.h"
#include "call/video_send_stream.h"
#include "common_video/frame_counts.h"
#include "common_video/include/quality_limitation_reason.h"
#include "modules/rtp_rtcp/include/report_block_data.h"
#include "modules/rtp_rtcp/include/rtcp_statistics.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtcp_packet/report_block.h"
#include "modules/video_coding/codecs/interface/common_constants.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "system_wrappers/include/clock.h"
#include "system_wrappers/include/metrics.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "video/config/video_encoder_config.h"
#include "video/video_stream_encoder_observer.h"

namespace webrtc {
namespace {

using ::testing::Optional;

constexpr uint32_t kFirstSsrc = 17;
constexpr uint32_t kSecondSsrc = 42;
constexpr uint32_t kFirstRtxSsrc = 18;
constexpr uint32_t kSecondRtxSsrc = 43;
constexpr uint32_t kFlexFecSsrc = 55;
constexpr int kFpsPeriodicIntervalMs = 2000;
constexpr int kWidth = 640;
constexpr int kHeight = 480;
constexpr int kQpIdx0 = 21;
constexpr int kQpIdx1 = 39;
constexpr int kRtpClockRateHz = 90000;
const CodecSpecificInfo kDefaultCodecInfo = []() {
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP8;
  return codec_info;
}();

const VideoStreamEncoderObserver::AdaptationSettings kScalingEnabled(true,
                                                                     true);
const VideoStreamEncoderObserver::AdaptationSettings kFramerateScalingDisabled(
    true,
    false);
const VideoStreamEncoderObserver::AdaptationSettings kResolutionScalingDisabled(
    false,
    true);
const VideoStreamEncoderObserver::AdaptationSettings kScalingDisabled;
}  // namespace

class SendStatisticsProxyTest : public ::testing::Test {
 public:
  SendStatisticsProxyTest() : SendStatisticsProxyTest("") {}
  explicit SendStatisticsProxyTest(const std::string& field_trials)
      : fake_clock_(Timestamp::Seconds(1234)),
        env_(CreateTestEnvironment(
            {.field_trials = field_trials, .time = &fake_clock_})),
        config_(GetTestConfig()) {}
  ~SendStatisticsProxyTest() override {}

 protected:
  void SetUp() override {
    metrics::Reset();
    statistics_proxy_.reset(new SendStatisticsProxy(
        env_, GetTestConfig(),
        VideoEncoderConfig::ContentType::kRealtimeVideo));
    expected_ = VideoSendStream::Stats();
    for (const auto& ssrc : config_.rtp.ssrcs) {
      expected_.substreams[ssrc].type =
          VideoSendStream::StreamStats::StreamType::kMedia;
    }
    for (size_t i = 0; i < config_.rtp.rtx.ssrcs.size(); ++i) {
      uint32_t ssrc = config_.rtp.rtx.ssrcs[i];
      expected_.substreams[ssrc].type =
          VideoSendStream::StreamStats::StreamType::kRtx;
      expected_.substreams[ssrc].referenced_media_ssrc = config_.rtp.ssrcs[i];
    }
  }

  VideoSendStream::Config GetTestConfig() {
    VideoSendStream::Config config(nullptr);
    config.rtp.ssrcs.push_back(kFirstSsrc);
    config.rtp.ssrcs.push_back(kSecondSsrc);
    config.rtp.rtx.ssrcs.push_back(kFirstRtxSsrc);
    config.rtp.rtx.ssrcs.push_back(kSecondRtxSsrc);
    config.rtp.ulpfec.red_payload_type = 17;
    return config;
  }

  VideoSendStream::Config GetTestConfigWithFlexFec() {
    VideoSendStream::Config config(nullptr);
    config.rtp.ssrcs.push_back(kFirstSsrc);
    config.rtp.ssrcs.push_back(kSecondSsrc);
    config.rtp.rtx.ssrcs.push_back(kFirstRtxSsrc);
    config.rtp.rtx.ssrcs.push_back(kSecondRtxSsrc);
    config.rtp.flexfec.payload_type = 50;
    config.rtp.flexfec.ssrc = kFlexFecSsrc;
    config.rtp.flexfec.protected_media_ssrcs = {kFirstSsrc};
    return config;
  }

  VideoSendStream::StreamStats GetStreamStats(uint32_t ssrc) {
    VideoSendStream::Stats stats = statistics_proxy_->GetStats();
    std::map<uint32_t, VideoSendStream::StreamStats>::iterator it =
        stats.substreams.find(ssrc);
    EXPECT_NE(it, stats.substreams.end());
    return it->second;
  }

  void UpdateDataCounters(uint32_t ssrc) {
    StreamDataCountersCallback* proxy =
        static_cast<StreamDataCountersCallback*>(statistics_proxy_.get());
    StreamDataCounters counters;
    proxy->DataCountersUpdated(counters, ssrc);
  }

  void ExpectEqual(VideoSendStream::Stats one, VideoSendStream::Stats other) {
    EXPECT_EQ(one.frames, other.frames);
    EXPECT_EQ(one.input_frame_rate, other.input_frame_rate);
    EXPECT_EQ(one.encode_frame_rate, other.encode_frame_rate);
    EXPECT_EQ(one.media_bitrate_bps, other.media_bitrate_bps);
    EXPECT_EQ(one.suspended, other.suspended);

    EXPECT_EQ(one.substreams.size(), other.substreams.size());
    for (std::map<uint32_t, VideoSendStream::StreamStats>::const_iterator it =
             one.substreams.begin();
         it != one.substreams.end(); ++it) {
      std::map<uint32_t, VideoSendStream::StreamStats>::const_iterator
          corresponding_it = other.substreams.find(it->first);
      ASSERT_TRUE(corresponding_it != other.substreams.end());
      const VideoSendStream::StreamStats& a = it->second;
      const VideoSendStream::StreamStats& b = corresponding_it->second;

      EXPECT_EQ(a.type, b.type);
      EXPECT_EQ(a.frame_counts.key_frames, b.frame_counts.key_frames);
      EXPECT_EQ(a.frame_counts.delta_frames, b.frame_counts.delta_frames);
      EXPECT_EQ(a.total_bitrate_bps, b.total_bitrate_bps);
      EXPECT_EQ(a.avg_delay_ms, b.avg_delay_ms);
      EXPECT_EQ(a.max_delay_ms, b.max_delay_ms);

      EXPECT_EQ(a.rtp_stats.transmitted.payload_bytes,
                b.rtp_stats.transmitted.payload_bytes);
      EXPECT_EQ(a.rtp_stats.transmitted.header_bytes,
                b.rtp_stats.transmitted.header_bytes);
      EXPECT_EQ(a.rtp_stats.transmitted.padding_bytes,
                b.rtp_stats.transmitted.padding_bytes);
      EXPECT_EQ(a.rtp_stats.transmitted.packets,
                b.rtp_stats.transmitted.packets);
      EXPECT_EQ(a.rtp_stats.retransmitted.packets,
                b.rtp_stats.retransmitted.packets);
      EXPECT_EQ(a.rtp_stats.fec.packets, b.rtp_stats.fec.packets);

      EXPECT_EQ(a.report_block_data.has_value(),
                b.report_block_data.has_value());
      if (a.report_block_data.has_value()) {
        EXPECT_EQ(a.report_block_data->fraction_lost_raw(),
                  b.report_block_data->fraction_lost_raw());
        EXPECT_EQ(a.report_block_data->cumulative_lost(),
                  b.report_block_data->cumulative_lost());
        EXPECT_EQ(a.report_block_data->extended_highest_sequence_number(),
                  b.report_block_data->extended_highest_sequence_number());
        EXPECT_EQ(a.report_block_data->jitter(), b.report_block_data->jitter());
      }
    }
  }

  SimulatedClock fake_clock_;
  Environment env_;
  std::unique_ptr<SendStatisticsProxy> statistics_proxy_;
  VideoSendStream::Config config_;
  VideoSendStream::Stats expected_;
};

TEST_F(SendStatisticsProxyTest, ReportBlockDataObserver) {
  ReportBlockDataObserver* callback = statistics_proxy_.get();
  for (uint32_t ssrc : config_.rtp.ssrcs) {
    // Add statistics with some arbitrary, but unique, numbers.
    uint32_t offset = ssrc * 4;
    rtcp::ReportBlock report_block;
    report_block.SetMediaSsrc(ssrc);
    report_block.SetCumulativeLost(offset);
    report_block.SetExtHighestSeqNum(offset + 1);
    report_block.SetFractionLost(offset + 2);
    report_block.SetJitter(offset + 3);
    ReportBlockData data;
    data.SetReportBlock(/*sender_ssrc=*/0, report_block, Timestamp::Zero(),
                        Timestamp::Zero());
    expected_.substreams[ssrc].report_block_data = data;

    callback->OnReportBlockDataUpdated(data);
  }
  for (uint32_t ssrc : config_.rtp.rtx.ssrcs) {
    // Add statistics with some arbitrary, but unique, numbers.
    uint32_t offset = ssrc * 4;
    rtcp::ReportBlock report_block;
    report_block.SetMediaSsrc(ssrc);
    report_block.SetCumulativeLost(offset);
    report_block.SetExtHighestSeqNum(offset + 1);
    report_block.SetFractionLost(offset + 2);
    report_block.SetJitter(offset + 3);
    ReportBlockData data;
    data.SetReportBlock(/*sender_ssrc=*/0, report_block, Timestamp::Zero(),
                        Timestamp::Zero());
    expected_.substreams[ssrc].report_block_data = data;

    callback->OnReportBlockDataUpdated(data);
  }
  VideoSendStream::Stats stats = statistics_proxy_->GetStats();
  ExpectEqual(expected_, stats);
}

TEST_F(SendStatisticsProxyTest, Suspended) {
  // Verify that the value is false by default.
  EXPECT_FALSE(statistics_proxy_->GetStats().suspended);

  // Verify that we can set it to true.
  statistics_proxy_->OnSuspendChange(true);
  EXPECT_TRUE(statistics_proxy_->GetStats().suspended);

  // Verify that we can set it back to false again.
  statistics_proxy_->OnSuspendChange(false);
  EXPECT_FALSE(statistics_proxy_->GetStats().suspended);
}

TEST_F(SendStatisticsProxyTest, FrameCounts) {
  FrameCountObserver* observer = statistics_proxy_.get();
  for (const auto& ssrc : config_.rtp.ssrcs) {
    // Add statistics with some arbitrary, but unique, numbers.
    VideoSendStream::StreamStats& stats = expected_.substreams[ssrc];
    uint32_t offset = ssrc * sizeof(VideoSendStream::StreamStats);
    FrameCounts frame_counts;
    frame_counts.key_frames = offset;
    frame_counts.delta_frames = offset + 1;
    stats.frame_counts = frame_counts;
    observer->FrameCountUpdated(frame_counts, ssrc);
  }
  for (const auto& ssrc : config_.rtp.rtx.ssrcs) {
    // Add statistics with some arbitrary, but unique, numbers.
    VideoSendStream::StreamStats& stats = expected_.substreams[ssrc];
    uint32_t offset = ssrc * sizeof(VideoSendStream::StreamStats);
    FrameCounts frame_counts;
    frame_counts.key_frames = offset;
    frame_counts.delta_frames = offset + 1;
    stats.frame_counts = frame_counts;
    observer->FrameCountUpdated(frame_counts, ssrc);
  }

  VideoSendStream::Stats stats = statistics_proxy_->GetStats();
  ExpectEqual(expected_, stats);
}

TEST_F(SendStatisticsProxyTest, DataCounters) {
  StreamDataCountersCallback* callback = statistics_proxy_.get();
  for (const auto& ssrc : config_.rtp.ssrcs) {
    StreamDataCounters& counters = expected_.substreams[ssrc].rtp_stats;
    // Add statistics with some arbitrary, but unique, numbers.
    size_t offset = ssrc * sizeof(StreamDataCounters);
    uint32_t offset_uint32 = static_cast<uint32_t>(offset);
    counters.transmitted.payload_bytes = offset;
    counters.transmitted.header_bytes = offset + 1;
    counters.fec.packets = offset_uint32 + 2;
    counters.transmitted.padding_bytes = offset + 3;
    counters.retransmitted.packets = offset_uint32 + 4;
    counters.transmitted.packets = offset_uint32 + 5;
    callback->DataCountersUpdated(counters, ssrc);
  }
  for (const auto& ssrc : config_.rtp.rtx.ssrcs) {
    StreamDataCounters& counters = expected_.substreams[ssrc].rtp_stats;
    // Add statistics with some arbitrary, but unique, numbers.
    size_t offset = ssrc * sizeof(StreamDataCounters);
    uint32_t offset_uint32 = static_cast<uint32_t>(offset);
    counters.transmitted.payload_bytes = offset;
    counters.transmitted.header_bytes = offset + 1;
    counters.fec.packets = offset_uint32 + 2;
    counters.transmitted.padding_bytes = offset + 3;
    counters.retransmitted.packets = offset_uint32 + 4;
    counters.transmitted.packets = offset_uint32 + 5;
    callback->DataCountersUpdated(counters, ssrc);
  }

  VideoSendStream::Stats stats = statistics_proxy_->GetStats();
  ExpectEqual(expected_, stats);
}

TEST_F(SendStatisticsProxyTest, Bitrate) {
  BitrateStatisticsObserver* observer = statistics_proxy_.get();
  for (const auto& ssrc : config_.rtp.ssrcs) {
    // Use ssrc as bitrate_bps to get a unique value for each stream.
    uint32_t total = ssrc;
    uint32_t retransmit = ssrc + 1;
    observer->Notify(total, retransmit, ssrc);
    expected_.substreams[ssrc].total_bitrate_bps = total;
    expected_.substreams[ssrc].retransmit_bitrate_bps = retransmit;
  }
  for (const auto& ssrc : config_.rtp.rtx.ssrcs) {
    // Use ssrc as bitrate_bps to get a unique value for each stream.
    uint32_t total = ssrc;
    uint32_t retransmit = ssrc + 1;
    observer->Notify(total, retransmit, ssrc);
    expected_.substreams[ssrc].total_bitrate_bps = total;
    expected_.substreams[ssrc].retransmit_bitrate_bps = retransmit;
  }

  VideoSendStream::Stats stats = statistics_proxy_->GetStats();
  ExpectEqual(expected_, stats);
}

TEST_F(SendStatisticsProxyTest, SendSideDelay) {
  for (uint32_t ssrc : config_.rtp.ssrcs) {
    // Use ssrc as avg_delay_ms and max_delay_ms to get a unique value for each
    // stream.
    expected_.substreams[ssrc].avg_delay_ms = ssrc;
    expected_.substreams[ssrc].max_delay_ms = ssrc + 1;
    statistics_proxy_->OnSendPacket(ssrc,
                                    /*capture_time=*/fake_clock_.CurrentTime() -
                                        TimeDelta::Millis(ssrc + 1));
    statistics_proxy_->OnSendPacket(ssrc,
                                    /*capture_time=*/fake_clock_.CurrentTime() -
                                        TimeDelta::Millis(ssrc - 1));
  }
  for (uint32_t ssrc : config_.rtp.rtx.ssrcs) {
    // Use ssrc as avg_delay_ms and max_delay_ms to get a unique value for each
    // stream.
    expected_.substreams[ssrc].avg_delay_ms = ssrc;
    expected_.substreams[ssrc].max_delay_ms = ssrc + 1;
    statistics_proxy_->OnSendPacket(ssrc,
                                    /*capture_time=*/fake_clock_.CurrentTime() -
                                        TimeDelta::Millis(ssrc + 1));
    statistics_proxy_->OnSendPacket(ssrc,
                                    /*capture_time=*/fake_clock_.CurrentTime() -
                                        TimeDelta::Millis(ssrc - 1));
  }
  VideoSendStream::Stats stats = statistics_proxy_->GetStats();
  ExpectEqual(expected_, stats);
}

TEST_F(SendStatisticsProxyTest, OnEncodedFrameTimeMeasured) {
  const int kEncodeTimeMs = 11;
  int encode_usage_percent = 80;
  statistics_proxy_->OnEncodedFrameTimeMeasured(kEncodeTimeMs,
                                                encode_usage_percent);

  VideoSendStream::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(kEncodeTimeMs, stats.avg_encode_time_ms);
  EXPECT_EQ(encode_usage_percent, stats.encode_usage_percent);
}

TEST_F(SendStatisticsProxyTest, TotalEncodeTimeIncreasesPerFrameMeasured) {
  const int kEncodeUsagePercent = 0;  // Don't care for this test.
  EXPECT_EQ(0u, statistics_proxy_->GetStats().total_encode_time_ms);
  statistics_proxy_->OnEncodedFrameTimeMeasured(10, kEncodeUsagePercent);
  EXPECT_EQ(10u, statistics_proxy_->GetStats().total_encode_time_ms);
  statistics_proxy_->OnEncodedFrameTimeMeasured(20, kEncodeUsagePercent);
  EXPECT_EQ(30u, statistics_proxy_->GetStats().total_encode_time_ms);
}

TEST_F(SendStatisticsProxyTest, OnSendEncodedImageIncreasesFramesEncoded) {
  EncodedImage encoded_image;
  CodecSpecificInfo codec_info;
  EXPECT_EQ(0u, statistics_proxy_->GetStats().frames_encoded);
  for (uint32_t i = 1; i <= 3; ++i) {
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
    EXPECT_EQ(i, statistics_proxy_->GetStats().frames_encoded);
  }
}

TEST_F(SendStatisticsProxyTest, OnSendEncodedImageIncreasesQpSum) {
  EncodedImage encoded_image;
  CodecSpecificInfo codec_info;
  auto ssrc = config_.rtp.ssrcs[0];
  EXPECT_EQ(std::nullopt,
            statistics_proxy_->GetStats().substreams[ssrc].qp_sum);
  encoded_image.qp_ = 3;
  statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  EXPECT_EQ(3u, statistics_proxy_->GetStats().substreams[ssrc].qp_sum);
  encoded_image.qp_ = 127;
  statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  EXPECT_EQ(130u, statistics_proxy_->GetStats().substreams[ssrc].qp_sum);
}

TEST_F(SendStatisticsProxyTest, OnSendEncodedImageWithoutQpQpSumWontExist) {
  EncodedImage encoded_image;
  CodecSpecificInfo codec_info;
  auto ssrc = config_.rtp.ssrcs[0];
  encoded_image.qp_ = -1;
  EXPECT_EQ(std::nullopt,
            statistics_proxy_->GetStats().substreams[ssrc].qp_sum);
  statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  EXPECT_EQ(std::nullopt,
            statistics_proxy_->GetStats().substreams[ssrc].qp_sum);
}

TEST_F(SendStatisticsProxyTest,
       OnSendEncodedImageSetsScalabilityModeOfCurrentLayer) {
  EncodedImage encoded_image;
  CodecSpecificInfo codec_info;
  ScalabilityMode layer0_mode = ScalabilityMode::kL1T1;
  ScalabilityMode layer1_mode = ScalabilityMode::kL1T3;
  auto ssrc0 = config_.rtp.ssrcs[0];
  auto ssrc1 = config_.rtp.ssrcs[1];
  EXPECT_EQ(std::nullopt,
            statistics_proxy_->GetStats().substreams[ssrc0].scalability_mode);
  EXPECT_EQ(std::nullopt,
            statistics_proxy_->GetStats().substreams[ssrc1].scalability_mode);
  encoded_image.SetSimulcastIndex(0);
  codec_info.scalability_mode = layer0_mode;
  statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  EXPECT_THAT(statistics_proxy_->GetStats().substreams[ssrc0].scalability_mode,
              layer0_mode);
  EXPECT_EQ(std::nullopt,
            statistics_proxy_->GetStats().substreams[ssrc1].scalability_mode);
  encoded_image.SetSimulcastIndex(1);
  codec_info.scalability_mode = layer1_mode;
  statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  EXPECT_THAT(statistics_proxy_->GetStats().substreams[ssrc0].scalability_mode,
              layer0_mode);
  EXPECT_THAT(statistics_proxy_->GetStats().substreams[ssrc1].scalability_mode,
              layer1_mode);
}

TEST_F(SendStatisticsProxyTest, EncodeFrameRateInSubStream) {
  const int kInterframeDelayMs = 100;
  const auto ssrc = config_.rtp.ssrcs[0];

  // First frame
  EncodedImage encoded_image;
  statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  fake_clock_.AdvanceTimeMilliseconds(kInterframeDelayMs);
  // Second frame
  encoded_image.SetRtpTimestamp(encoded_image.RtpTimestamp() +
                                90 * kInterframeDelayMs);
  statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  fake_clock_.AdvanceTimeMilliseconds(kInterframeDelayMs);

  auto stats = statistics_proxy_->GetStats();
  EXPECT_EQ(stats.substreams[ssrc].encode_frame_rate, 10);
}

TEST_F(SendStatisticsProxyTest, EncodeFrameRateInSubStreamsVp8Simulcast) {
  const int kInterframeDelayMs = 100;
  EncodedImage encoded_image;
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP8;

  for (int i = 0; i < 10; ++i) {
    encoded_image.SetRtpTimestamp(encoded_image.RtpTimestamp() +
                                  90 * kInterframeDelayMs);
    encoded_image.SetSimulcastIndex(0);
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
    encoded_image.SetSimulcastIndex(1);
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
    fake_clock_.AdvanceTimeMilliseconds(kInterframeDelayMs);
  }

  VideoSendStream::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(2u, stats.substreams.size());
  EXPECT_EQ(stats.substreams[config_.rtp.ssrcs[0]].encode_frame_rate, 10);
  EXPECT_EQ(stats.substreams[config_.rtp.ssrcs[1]].encode_frame_rate, 10);

  // Stop encoding second stream, expect framerate to be zero.
  for (int i = 0; i < 10; ++i) {
    encoded_image.SetRtpTimestamp(encoded_image.RtpTimestamp() +
                                  90 * kInterframeDelayMs);
    encoded_image.SetSimulcastIndex(0);
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
    fake_clock_.AdvanceTimeMilliseconds(kInterframeDelayMs);
  }

  stats = statistics_proxy_->GetStats();
  EXPECT_EQ(2u, stats.substreams.size());
  EXPECT_EQ(stats.substreams[config_.rtp.ssrcs[0]].encode_frame_rate, 10);
  EXPECT_EQ(stats.substreams[config_.rtp.ssrcs[1]].encode_frame_rate, 0);

  // Start encoding second stream.
  for (int i = 0; i < 10; ++i) {
    encoded_image.SetRtpTimestamp(encoded_image.RtpTimestamp() +
                                  90 * kInterframeDelayMs);
    encoded_image.SetSimulcastIndex(0);
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
    encoded_image.SetSimulcastIndex(1);
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
    fake_clock_.AdvanceTimeMilliseconds(kInterframeDelayMs);
  }

  stats = statistics_proxy_->GetStats();
  EXPECT_EQ(2u, stats.substreams.size());
  EXPECT_EQ(stats.substreams[config_.rtp.ssrcs[0]].encode_frame_rate, 10);
  EXPECT_EQ(stats.substreams[config_.rtp.ssrcs[1]].encode_frame_rate, 10);
}

TEST_F(SendStatisticsProxyTest, EncodeFrameRateInSubStreamsVp9Svc) {
  const int kInterframeDelayMs = 100;
  EncodedImage encoded_image;
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP9;

  for (int i = 0; i < 10; ++i) {
    encoded_image.SetRtpTimestamp(encoded_image.RtpTimestamp() +
                                  90 * kInterframeDelayMs);
    encoded_image.SetSpatialIndex(0);
    codec_info.end_of_picture = false;
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
    encoded_image.SetSpatialIndex(1);
    codec_info.end_of_picture = true;
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
    fake_clock_.AdvanceTimeMilliseconds(kInterframeDelayMs);
  }

  VideoSendStream::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(1u, stats.substreams.size());
  EXPECT_EQ(stats.substreams[config_.rtp.ssrcs[0]].encode_frame_rate, 10);
}

TEST_F(SendStatisticsProxyTest, GetCpuAdaptationStats) {
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  EXPECT_FALSE(statistics_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().cpu_limited_resolution);
  cpu_counts.fps_adaptations = 1;
  cpu_counts.resolution_adaptations = 0;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  EXPECT_TRUE(statistics_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().cpu_limited_resolution);
  cpu_counts.fps_adaptations = 0;
  cpu_counts.resolution_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  EXPECT_FALSE(statistics_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_TRUE(statistics_proxy_->GetStats().cpu_limited_resolution);
  cpu_counts.fps_adaptations = 1;
  statistics_proxy_->UpdateAdaptationSettings(kResolutionScalingDisabled,
                                              kResolutionScalingDisabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  EXPECT_TRUE(statistics_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().cpu_limited_resolution);
  statistics_proxy_->UpdateAdaptationSettings(kScalingDisabled,
                                              kScalingDisabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  EXPECT_FALSE(statistics_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().cpu_limited_resolution);
}

TEST_F(SendStatisticsProxyTest, GetQualityAdaptationStats) {
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
  quality_counts.fps_adaptations = 1;
  quality_counts.resolution_adaptations = 0;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
  quality_counts.fps_adaptations = 0;
  quality_counts.resolution_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_resolution);
  quality_counts.fps_adaptations = 1;
  statistics_proxy_->UpdateAdaptationSettings(kResolutionScalingDisabled,
                                              kResolutionScalingDisabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
  statistics_proxy_->UpdateAdaptationSettings(kScalingDisabled,
                                              kScalingDisabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
}

TEST_F(SendStatisticsProxyTest, GetStatsReportsCpuAdaptChanges) {
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  EXPECT_EQ(0, statistics_proxy_->GetStats().number_of_cpu_adapt_changes);

  cpu_counts.resolution_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  EXPECT_FALSE(statistics_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_TRUE(statistics_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(1, statistics_proxy_->GetStats().number_of_cpu_adapt_changes);

  cpu_counts.resolution_adaptations = 2;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  EXPECT_FALSE(statistics_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_TRUE(statistics_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(2, statistics_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(0, statistics_proxy_->GetStats().number_of_quality_adapt_changes);
}

TEST_F(SendStatisticsProxyTest, GetStatsReportsQualityAdaptChanges) {
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  EXPECT_EQ(0, statistics_proxy_->GetStats().number_of_quality_adapt_changes);

  quality_counts.fps_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(1, statistics_proxy_->GetStats().number_of_quality_adapt_changes);

  quality_counts.fps_adaptations = 0;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(2, statistics_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(0, statistics_proxy_->GetStats().number_of_cpu_adapt_changes);
}

TEST_F(SendStatisticsProxyTest, TestAdaptationStatisticsMasking) {
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  EXPECT_EQ(0, statistics_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(0, statistics_proxy_->GetStats().number_of_cpu_adapt_changes);

  quality_counts.resolution_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  quality_counts.fps_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  cpu_counts.resolution_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  cpu_counts.fps_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  // We have 1 fps and resolution reduction for both cpu and quality
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(statistics_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_TRUE(statistics_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(2, statistics_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(2, statistics_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Disable quality scaling. Expect quality scaling not limited.
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled,
                                              kScalingDisabled);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(statistics_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_TRUE(statistics_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(2, statistics_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(2, statistics_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Disable framerate scaling.
  statistics_proxy_->UpdateAdaptationSettings(kFramerateScalingDisabled,
                                              kFramerateScalingDisabled);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(statistics_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_TRUE(statistics_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(2, statistics_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(2, statistics_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Disable resolution scaling.
  statistics_proxy_->UpdateAdaptationSettings(kResolutionScalingDisabled,
                                              kResolutionScalingDisabled);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(statistics_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(2, statistics_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(2, statistics_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Enable all
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(statistics_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_TRUE(statistics_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(2, statistics_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(2, statistics_proxy_->GetStats().number_of_cpu_adapt_changes);
}

TEST_F(SendStatisticsProxyTest, AdaptChangesNotReported_AdaptationNotEnabled) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);
  // Min runtime has passed.
  fake_clock_.AdvanceTime(metrics::kMinRunTime);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Cpu"));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Quality"));
}

TEST_F(SendStatisticsProxyTest, AdaptChangesNotReported_MinRuntimeNotPassed) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);
  // Enable adaptation.
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  // Min runtime has not passed.
  fake_clock_.AdvanceTime(metrics::kMinRunTime - TimeDelta::Millis(1));
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Cpu"));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Quality"));
}

TEST_F(SendStatisticsProxyTest, ZeroAdaptChangesReported) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);
  // Enable adaptation.
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  // Min runtime has passed.
  fake_clock_.AdvanceTime(metrics::kMinRunTime);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Cpu"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Cpu", 0));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Quality"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Quality", 0));
}

TEST_F(SendStatisticsProxyTest, CpuAdaptChangesReported) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);
  // Enable adaptation.
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  // Adapt changes: 1, elapsed time: 10 sec => 6 per minute.
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  fake_clock_.AdvanceTimeMilliseconds(10000);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Cpu"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Cpu", 6));
}

TEST_F(SendStatisticsProxyTest, ExcludesInitialQualityAdaptDownChange) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);
  // Enable adaptation.
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  // Adapt changes: 1 (1 initial) = 0, elapsed time: 10 sec => 0 per minute.
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  statistics_proxy_->OnInitialQualityResolutionAdaptDown();
  fake_clock_.AdvanceTimeMilliseconds(10000);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Quality"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Quality", 0));
}

TEST_F(SendStatisticsProxyTest, ExcludesInitialQualityAdaptDownChanges) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);
  // Enable adaptation.
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  // Adapt changes: 3 (2 initial) = 1, elapsed time: 10 sec => 6 per minute.
  quality_counts.resolution_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  statistics_proxy_->OnInitialQualityResolutionAdaptDown();
  quality_counts.resolution_adaptations = 2;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  statistics_proxy_->OnInitialQualityResolutionAdaptDown();
  quality_counts.resolution_adaptations = 3;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  fake_clock_.AdvanceTimeMilliseconds(10000);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Quality"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Quality", 6));
}

TEST_F(SendStatisticsProxyTest, InitialQualityAdaptChangesNotExcludedOnError) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);
  // Enable adaptation.
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  // Adapt changes: 1 (2 initial) = 1, elapsed time: 10 sec => 6 per minute.
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  statistics_proxy_->OnInitialQualityResolutionAdaptDown();
  statistics_proxy_->OnInitialQualityResolutionAdaptDown();
  fake_clock_.AdvanceTimeMilliseconds(10000);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Quality"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Quality", 6));
}

TEST_F(SendStatisticsProxyTest, ExcludesInitialQualityAdaptDownAndUpChanges) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);
  // Enable adaptation.
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  statistics_proxy_->ClearAdaptationStats();
  // Adapt changes: 8 (4 initial) = 4, elapsed time: 10 sec => 24 per minute.
  quality_counts.resolution_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  statistics_proxy_->OnInitialQualityResolutionAdaptDown();
  quality_counts.resolution_adaptations = 2;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  statistics_proxy_->OnInitialQualityResolutionAdaptDown();
  quality_counts.resolution_adaptations = 3;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  quality_counts.fps_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  quality_counts.fps_adaptations = 0;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  quality_counts.resolution_adaptations = 2;  // Initial resolution up.
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  quality_counts.resolution_adaptations = 1;  // Initial resolution up.
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  quality_counts.resolution_adaptations = 0;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);

  fake_clock_.AdvanceTimeMilliseconds(10000);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Quality"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Quality", 24));
}

TEST_F(SendStatisticsProxyTest, AdaptChangesStatsExcludesDisabledTime) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);

  // Disable quality adaptation.
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled,
                                              kScalingDisabled);
  fake_clock_.AdvanceTimeMilliseconds(10000);

  // Enable quality adaptation.
  // Adapt changes: 2, elapsed time: 20 sec.
  quality_counts.fps_adaptations = 0;
  statistics_proxy_->UpdateAdaptationSettings(kResolutionScalingDisabled,
                                              kResolutionScalingDisabled);
  fake_clock_.AdvanceTimeMilliseconds(5000);
  statistics_proxy_->UpdateAdaptationSettings(kResolutionScalingDisabled,
                                              kResolutionScalingDisabled);
  fake_clock_.AdvanceTimeMilliseconds(9000);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  fake_clock_.AdvanceTimeMilliseconds(6000);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);

  // Disable quality adaptation.
  statistics_proxy_->UpdateAdaptationSettings(kScalingDisabled,
                                              kScalingDisabled);
  fake_clock_.AdvanceTimeMilliseconds(30000);

  // Enable quality adaptation.
  // Adapt changes: 1, elapsed time: 10 sec.
  quality_counts.resolution_adaptations = 0;
  statistics_proxy_->UpdateAdaptationSettings(kFramerateScalingDisabled,
                                              kFramerateScalingDisabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  fake_clock_.AdvanceTimeMilliseconds(10000);

  // Disable quality adaptation.
  statistics_proxy_->UpdateAdaptationSettings(kScalingDisabled,
                                              kScalingDisabled);
  fake_clock_.AdvanceTimeMilliseconds(5000);
  statistics_proxy_->UpdateAdaptationSettings(kScalingDisabled,
                                              kScalingDisabled);
  fake_clock_.AdvanceTimeMilliseconds(20000);

  // Adapt changes: 3, elapsed time: 30 sec => 6 per minute.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Quality"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Quality", 6));
}

TEST_F(SendStatisticsProxyTest,
       AdaptChangesNotReported_ScalingNotEnabledVideoResumed) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);

  // Suspend and resume video.
  statistics_proxy_->OnSuspendChange(true);
  fake_clock_.AdvanceTimeMilliseconds(5000);
  statistics_proxy_->OnSuspendChange(false);

  // Min runtime has passed but scaling not enabled.
  fake_clock_.AdvanceTime(metrics::kMinRunTime);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Cpu"));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Quality"));
}

TEST_F(SendStatisticsProxyTest, QualityAdaptChangesStatsExcludesSuspendedTime) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);

  // Enable adaptation.
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  // Adapt changes: 2, elapsed time: 20 sec.
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  fake_clock_.AdvanceTimeMilliseconds(20000);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);

  // Suspend and resume video.
  statistics_proxy_->OnSuspendChange(true);
  fake_clock_.AdvanceTimeMilliseconds(30000);
  statistics_proxy_->OnSuspendChange(false);

  // Adapt changes: 1, elapsed time: 10 sec.
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  fake_clock_.AdvanceTimeMilliseconds(10000);

  // Adapt changes: 3, elapsed time: 30 sec => 6 per minute.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Quality"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Quality", 6));
}

TEST_F(SendStatisticsProxyTest, CpuAdaptChangesStatsExcludesSuspendedTime) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);

  // Video not suspended.
  statistics_proxy_->OnSuspendChange(false);
  fake_clock_.AdvanceTimeMilliseconds(30000);

  // Enable adaptation.
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  // Adapt changes: 1, elapsed time: 20 sec.
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  fake_clock_.AdvanceTimeMilliseconds(10000);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);

  // Video not suspended, stats time already started.
  statistics_proxy_->OnSuspendChange(false);
  fake_clock_.AdvanceTimeMilliseconds(10000);

  // Disable adaptation.
  statistics_proxy_->UpdateAdaptationSettings(kScalingDisabled,
                                              kScalingDisabled);
  fake_clock_.AdvanceTimeMilliseconds(30000);

  // Suspend and resume video, stats time not started when scaling not enabled.
  statistics_proxy_->OnSuspendChange(true);
  fake_clock_.AdvanceTimeMilliseconds(30000);
  statistics_proxy_->OnSuspendChange(false);
  fake_clock_.AdvanceTimeMilliseconds(30000);

  // Enable adaptation.
  // Adapt changes: 1, elapsed time: 10 sec.
  cpu_counts.fps_adaptations = 0;
  cpu_counts.resolution_adaptations = 0;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  fake_clock_.AdvanceTimeMilliseconds(10000);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);

  // Adapt changes: 2, elapsed time: 30 sec => 4 per minute.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Cpu"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Cpu", 4));
}

TEST_F(SendStatisticsProxyTest, AdaptChangesStatsNotStartedIfVideoSuspended) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);

  // Video suspended.
  statistics_proxy_->OnSuspendChange(true);

  // Enable adaptation, stats time not started when suspended.
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  fake_clock_.AdvanceTimeMilliseconds(10000);

  // Resume video, stats time started.
  // Adapt changes: 1, elapsed time: 10 sec.
  statistics_proxy_->OnSuspendChange(false);
  fake_clock_.AdvanceTimeMilliseconds(10000);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);

  // Adapt changes: 1, elapsed time: 10 sec => 6 per minute.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Cpu"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Cpu", 6));
}

TEST_F(SendStatisticsProxyTest, AdaptChangesStatsRestartsOnFirstSentPacket) {
  // Send first packet, adaptation enabled.
  // Elapsed time before first packet is sent should be excluded.
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  fake_clock_.AdvanceTimeMilliseconds(10000);
  UpdateDataCounters(kFirstSsrc);

  // Adapt changes: 1, elapsed time: 10 sec.
  fake_clock_.AdvanceTimeMilliseconds(10000);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  UpdateDataCounters(kFirstSsrc);

  // Adapt changes: 1, elapsed time: 10 sec => 6 per minute.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Quality"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Quality", 6));
}

TEST_F(SendStatisticsProxyTest, AdaptChangesStatsStartedAfterFirstSentPacket) {
  // Enable and disable adaptation.
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  fake_clock_.AdvanceTimeMilliseconds(60000);
  statistics_proxy_->UpdateAdaptationSettings(kScalingDisabled,
                                              kScalingDisabled);

  // Send first packet, scaling disabled.
  // Elapsed time before first packet is sent should be excluded.
  UpdateDataCounters(kFirstSsrc);
  fake_clock_.AdvanceTimeMilliseconds(60000);

  // Enable adaptation.
  cpu_counts.resolution_adaptations = 0;
  statistics_proxy_->UpdateAdaptationSettings(kFramerateScalingDisabled,
                                              kScalingDisabled);
  fake_clock_.AdvanceTimeMilliseconds(10000);
  UpdateDataCounters(kFirstSsrc);

  // Adapt changes: 1, elapsed time: 20 sec.
  fake_clock_.AdvanceTimeMilliseconds(10000);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);

  // Adapt changes: 1, elapsed time: 20 sec => 3 per minute.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Cpu"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Cpu", 3));
}

TEST_F(SendStatisticsProxyTest, AdaptChangesReportedAfterContentSwitch) {
  // First RTP packet sent, cpu adaptation enabled.
  UpdateDataCounters(kFirstSsrc);
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled,
                                              kScalingDisabled);

  // Adapt changes: 2, elapsed time: 15 sec => 8 per minute.
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  fake_clock_.AdvanceTimeMilliseconds(6000);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  fake_clock_.AdvanceTimeMilliseconds(9000);

  // Switch content type, real-time stats should be updated.
  VideoEncoderConfig config;
  config.content_type = VideoEncoderConfig::ContentType::kScreen;
  statistics_proxy_->OnEncoderReconfigured(config, {});
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Cpu"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AdaptChangesPerMinute.Cpu", 8));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.AdaptChangesPerMinute.Quality"));

  // First RTP packet sent, scaling enabled.
  UpdateDataCounters(kFirstSsrc);
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled,
                                              kScalingDisabled);

  // Adapt changes: 4, elapsed time: 120 sec => 2 per minute.
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  fake_clock_.AdvanceTimeMilliseconds(120000);

  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(
                       "WebRTC.Video.Screenshare.AdaptChangesPerMinute.Cpu"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents(
             "WebRTC.Video.Screenshare.AdaptChangesPerMinute.Cpu", 2));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples(
             "WebRTC.Video.Screenshare.AdaptChangesPerMinute.Quality"));
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationReasonIsCpuWhenCpuIsResolutionLimited) {
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;

  cpu_counts.resolution_adaptations = 1;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);

  EXPECT_EQ(QualityLimitationReason::kCpu,
            statistics_proxy_->GetStats().quality_limitation_reason);
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationReasonIsCpuWhenCpuIsFramerateLimited) {
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;

  cpu_counts.fps_adaptations = 1;

  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);

  EXPECT_EQ(QualityLimitationReason::kCpu,
            statistics_proxy_->GetStats().quality_limitation_reason);
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationReasonIsBandwidthWhenQualityIsResolutionLimited) {
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;

  quality_counts.resolution_adaptations = 1;

  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);

  EXPECT_EQ(QualityLimitationReason::kBandwidth,
            statistics_proxy_->GetStats().quality_limitation_reason);
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationReasonIsBandwidthWhenQualityIsFramerateLimited) {
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;

  quality_counts.fps_adaptations = 1;

  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);

  EXPECT_EQ(QualityLimitationReason::kBandwidth,
            statistics_proxy_->GetStats().quality_limitation_reason);
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationReasonIsBandwidthWhenBothCpuAndQualityIsLimited) {
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;

  cpu_counts.resolution_adaptations = 1;
  quality_counts.resolution_adaptations = 1;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);

  // Even if the last adaptation reason is kCpu, if the counters indicate being
  // both CPU and quality (=bandwidth) limited, kBandwidth takes precedence.
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);

  EXPECT_EQ(QualityLimitationReason::kBandwidth,
            statistics_proxy_->GetStats().quality_limitation_reason);
}

TEST_F(SendStatisticsProxyTest, QualityLimitationReasonIsNoneWhenNotLimited) {
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;

  // Observe a limitation due to CPU. This makes sure the test doesn't pass
  // due to "none" being the default value.
  cpu_counts.resolution_adaptations = 1;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  // Go back to not being limited.
  cpu_counts.resolution_adaptations = 0;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);

  EXPECT_EQ(QualityLimitationReason::kNone,
            statistics_proxy_->GetStats().quality_limitation_reason);
}

TEST_F(SendStatisticsProxyTest, QualityLimitationDurationIncreasesWithTime) {
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;

  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  // Not limited for 3000 ms
  fake_clock_.AdvanceTimeMilliseconds(3000);
  // CPU limited for 2000 ms
  cpu_counts.resolution_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  fake_clock_.AdvanceTimeMilliseconds(2000);
  // Bandwidth limited for 1000 ms
  cpu_counts.resolution_adaptations = 0;
  quality_counts.resolution_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  fake_clock_.AdvanceTimeMilliseconds(1000);
  // CPU limited for another 2000 ms
  cpu_counts.resolution_adaptations = 1;
  quality_counts.resolution_adaptations = 0;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);
  fake_clock_.AdvanceTimeMilliseconds(2000);

  auto quality_limitation_durations_ms =
      statistics_proxy_->GetStats().quality_limitation_durations_ms;

  EXPECT_EQ(3000,
            quality_limitation_durations_ms[QualityLimitationReason::kNone]);
  EXPECT_EQ(4000,
            quality_limitation_durations_ms[QualityLimitationReason::kCpu]);
  EXPECT_EQ(
      1000,
      quality_limitation_durations_ms[QualityLimitationReason::kBandwidth]);
  EXPECT_EQ(0,
            quality_limitation_durations_ms[QualityLimitationReason::kOther]);
}

TEST_F(SendStatisticsProxyTest, QualityLimitationResolutionChangesDefaultZero) {
  EXPECT_EQ(
      0u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationResolutionChangesNotChangesWithOnlyDefaultAllocation) {
  VideoCodec codec;
  VideoBitrateAllocation allocation;
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      0u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationResolutionChangesDoesNotIncreaseOnFirstAllocation) {
  VideoCodec codec;
  codec.simulcastStream[0].active = true;
  codec.simulcastStream[1].active = true;
  codec.simulcastStream[2].active = true;
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 100);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      0u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationResolutionChangesWhenNewLayerGetsBandwidth) {
  VideoCodec codec;
  codec.simulcastStream[0].active = true;
  codec.simulcastStream[1].active = true;
  codec.simulcastStream[2].active = true;
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 100);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  allocation.SetBitrate(1, 0, 100);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      1u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationResolutionDoesNotChangeWhenLayerSame) {
  VideoCodec codec;
  codec.simulcastStream[0].active = true;
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 100);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  // Layer 0 got more bandwidth, but still only one layer on
  allocation.SetBitrate(0, 0, 200);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      0u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationResolutionChangesWithTogglingLayers) {
  VideoCodec codec;
  codec.simulcastStream[0].active = true;
  codec.simulcastStream[1].active = true;
  codec.simulcastStream[2].active = true;
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 100);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      0u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);
  allocation.SetBitrate(1, 0, 300);
  allocation.SetBitrate(2, 0, 500);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      1u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);
  // Layer 2 off
  allocation.SetBitrate(2, 0, 0);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      2u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);
  // Layer 2 back on
  allocation.SetBitrate(2, 0, 500);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      3u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);
  allocation.SetBitrate(0, 0, 0);
  allocation.SetBitrate(1, 0, 0);
  allocation.SetBitrate(2, 0, 0);
  // All layers off
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      4u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationResolutionDoesNotUpdateOnCodecSimulcastStreamChanges) {
  VideoCodec codec;
  // 3 layers
  codec.simulcastStream[0].active = true;
  codec.simulcastStream[1].active = true;
  codec.simulcastStream[2].active = true;
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 500);
  allocation.SetBitrate(1, 0, 500);
  allocation.SetBitrate(2, 0, 500);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      0u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);

  // Down to one layer now, triggered by a config change
  codec.numberOfSimulcastStreams = 1;
  codec.simulcastStream[1].active = false;
  codec.simulcastStream[2].active = false;
  allocation.SetBitrate(0, 0, 100);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      0u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);

  // Up to 3 layers again.
  codec.numberOfSimulcastStreams = 3;
  codec.simulcastStream[1].active = true;
  codec.simulcastStream[2].active = true;
  allocation.SetBitrate(0, 0, 500);
  allocation.SetBitrate(1, 0, 500);
  allocation.SetBitrate(2, 0, 500);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      0u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);
}

TEST_F(SendStatisticsProxyTest, OnBitrateAllocationUpdatedSetsTargetBitrates) {
  // We only update target bitrates for substreams that exist and these are
  // created lazily in various places... calling OnInactiveSsrc() is one way to
  // ensure the stats are reported.
  statistics_proxy_->OnInactiveSsrc(kFirstSsrc);
  statistics_proxy_->OnInactiveSsrc(kSecondSsrc);

  // Update target bitrates!
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 123);
  allocation.SetBitrate(1, 0, 321);
  statistics_proxy_->OnBitrateAllocationUpdated(VideoCodec(), allocation);
  EXPECT_EQ(statistics_proxy_->GetStats().substreams[kFirstSsrc].target_bitrate,
            DataRate::BitsPerSec(123));
  EXPECT_EQ(
      statistics_proxy_->GetStats().substreams[kSecondSsrc].target_bitrate,
      DataRate::BitsPerSec(321));

  // 0 bitrate = no target.
  allocation.SetBitrate(0, 0, 0);
  allocation.SetBitrate(1, 0, 0);
  statistics_proxy_->OnBitrateAllocationUpdated(VideoCodec(), allocation);
  EXPECT_FALSE(statistics_proxy_->GetStats()
                   .substreams[kFirstSsrc]
                   .target_bitrate.has_value());
  EXPECT_FALSE(statistics_proxy_->GetStats()
                   .substreams[kSecondSsrc]
                   .target_bitrate.has_value());
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationResolutionDoesNotUpdateForSpatialLayerChanges) {
  VideoCodec codec;
  codec.simulcastStream[0].active = true;
  codec.spatialLayers[0].active = true;
  codec.spatialLayers[1].active = true;
  codec.spatialLayers[2].active = true;
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 500);
  allocation.SetBitrate(1, 0, 500);
  allocation.SetBitrate(2, 0, 500);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      0u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);

  // Down to one layer now, triggered by a config change
  codec.spatialLayers[1].active = false;
  codec.spatialLayers[2].active = false;
  allocation.SetBitrate(0, 0, 100);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      0u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);

  // Up to 3 layers again.
  codec.spatialLayers[1].active = true;
  codec.spatialLayers[2].active = true;
  allocation.SetBitrate(0, 0, 500);
  allocation.SetBitrate(1, 0, 500);
  allocation.SetBitrate(2, 0, 500);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(
      0u, statistics_proxy_->GetStats().quality_limitation_resolution_changes);
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationReasonsAreCorrectForContentType) {
  // Realtime case.
  // Configure two streams.
  VideoEncoderConfig config;
  config.content_type = VideoEncoderConfig::ContentType::kRealtimeVideo;
  config.number_of_streams = 2;
  VideoStream stream1;
  stream1.width = kWidth / 2;
  stream1.height = kHeight / 2;
  VideoStream stream2;
  stream2.width = kWidth;
  stream2.height = kHeight;
  statistics_proxy_->OnEncoderReconfigured(config, {stream1, stream2});
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(statistics_proxy_->GetStats().quality_limitation_reason,
            QualityLimitationReason::kNone);
  // Bw disabled one layer.
  VideoCodec codec;
  codec.numberOfSimulcastStreams = 2;
  codec.simulcastStream[0].active = true;
  codec.simulcastStream[1].active = true;
  VideoBitrateAllocation allocation;
  // Some positive bitrate only on the first stream.
  allocation.SetBitrate(0, 0, 10000);
  allocation.SetBitrate(1, 0, 0);
  allocation.set_bw_limited(true);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(statistics_proxy_->GetStats().quality_limitation_reason,
            QualityLimitationReason::kBandwidth);
  // Bw enabled all layers.
  allocation.SetBitrate(1, 0, 10000);
  allocation.set_bw_limited(false);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(statistics_proxy_->GetStats().quality_limitation_reason,
            QualityLimitationReason::kNone);

  // Screencast case
  // Configure two streams.
  config.content_type = VideoEncoderConfig::ContentType::kScreen;
  config.number_of_streams = 2;
  stream1.width = kWidth;
  stream1.height = kHeight;
  statistics_proxy_->OnEncoderReconfigured(config, {stream1, stream2});
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(statistics_proxy_->GetStats().quality_limitation_reason,
            QualityLimitationReason::kNone);
  // Bw disabled one layer.
  // Some positive bitrate only on the second stream.
  allocation.SetBitrate(0, 0, 10000);
  allocation.SetBitrate(1, 0, 0);
  allocation.set_bw_limited(true);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(statistics_proxy_->GetStats().quality_limitation_reason,
            QualityLimitationReason::kBandwidth);
  // Bw enabled all layers.
  allocation.SetBitrate(1, 0, 10000);
  allocation.set_bw_limited(false);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(statistics_proxy_->GetStats().quality_limitation_reason,
            QualityLimitationReason::kNone);
}

TEST_F(SendStatisticsProxyTest, SwitchContentTypeUpdatesHistograms) {
  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i)
    statistics_proxy_->OnIncomingFrame(kWidth, kHeight);

  // No switch, stats should not be updated.
  VideoEncoderConfig config;
  config.content_type = VideoEncoderConfig::ContentType::kRealtimeVideo;
  statistics_proxy_->OnEncoderReconfigured(config, {});
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.InputWidthInPixels"));

  // Switch to screenshare, real-time stats should be updated.
  config.content_type = VideoEncoderConfig::ContentType::kScreen;
  statistics_proxy_->OnEncoderReconfigured(config, {});
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.InputWidthInPixels"));
}

TEST_F(SendStatisticsProxyTest, InputResolutionHistogramsAreUpdated) {
  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i)
    statistics_proxy_->OnIncomingFrame(kWidth, kHeight);

  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.InputWidthInPixels"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.InputWidthInPixels", kWidth));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.InputHeightInPixels"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.InputHeightInPixels", kHeight));
}

TEST_F(SendStatisticsProxyTest, SentResolutionHistogramsAreUpdated) {
  const int64_t kMaxEncodedFrameWindowMs = 800;
  const int kFps = 5;
  const int kNumFramesPerWindow = kFps * kMaxEncodedFrameWindowMs / 1000;
  const int kMinSamples =  // Sample added when removed from EncodedFrameMap.
      SendStatisticsProxy::kMinRequiredMetricsSamples + kNumFramesPerWindow;
  EncodedImage encoded_image;

  // Not enough samples, stats should not be updated.
  for (int i = 0; i < kMinSamples - 1; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps);
    encoded_image.SetRtpTimestamp(encoded_image.RtpTimestamp() +
                                  90 * 1000 / kFps);
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  }
  SetUp();  // Reset stats proxy also causes histograms to be reported.
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.SentWidthInPixels"));
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.SentHeightInPixels"));

  // Enough samples, max resolution per frame should be reported.
  encoded_image.SetRtpTimestamp(0xffff0000);  // Will wrap.
  for (int i = 0; i < kMinSamples; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps);
    encoded_image.SetRtpTimestamp(encoded_image.RtpTimestamp() +
                                  90 * 1000 / kFps);
    encoded_image._encodedWidth = kWidth;
    encoded_image._encodedHeight = kHeight;
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
    encoded_image._encodedWidth = kWidth / 2;
    encoded_image._encodedHeight = kHeight / 2;
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  }

  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.SentWidthInPixels"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.SentWidthInPixels", kWidth));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.SentHeightInPixels"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.SentHeightInPixels", kHeight));
}

TEST_F(SendStatisticsProxyTest, InputFpsHistogramIsUpdated) {
  const int kFps = 20;
  const int kMinPeriodicSamples = 6;
  int frames = kMinPeriodicSamples * kFpsPeriodicIntervalMs * kFps / 1000;
  for (int i = 0; i <= frames; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps);
    statistics_proxy_->OnIncomingFrame(kWidth, kHeight);
  }
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.InputFramesPerSecond"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.InputFramesPerSecond", kFps));
}

TEST_F(SendStatisticsProxyTest, SentFpsHistogramIsUpdated) {
  EncodedImage encoded_image;
  const int kFps = 20;
  const int kMinPeriodicSamples = 6;
  int frames = kMinPeriodicSamples * kFpsPeriodicIntervalMs * kFps / 1000 + 1;
  for (int i = 0; i < frames; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps);
    encoded_image.SetRtpTimestamp(encoded_image.RtpTimestamp() + 1);
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
    // Frame with same timestamp should not be counted.
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  }
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.SentFramesPerSecond"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.SentFramesPerSecond", kFps));
}

TEST_F(SendStatisticsProxyTest, InputFpsHistogramExcludesSuspendedTime) {
  const int kFps = 20;
  const int kSuspendTimeMs = 10000;
  const int kMinPeriodicSamples = 6;
  int frames = kMinPeriodicSamples * kFpsPeriodicIntervalMs * kFps / 1000;
  for (int i = 0; i < frames; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps);
    statistics_proxy_->OnIncomingFrame(kWidth, kHeight);
  }
  // Suspend.
  statistics_proxy_->OnSuspendChange(true);
  fake_clock_.AdvanceTimeMilliseconds(kSuspendTimeMs);

  for (int i = 0; i < frames; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps);
    statistics_proxy_->OnIncomingFrame(kWidth, kHeight);
  }
  // Suspended time interval should not affect the framerate.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.InputFramesPerSecond"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.InputFramesPerSecond", kFps));
}

TEST_F(SendStatisticsProxyTest, SentFpsHistogramExcludesSuspendedTime) {
  EncodedImage encoded_image;
  const int kFps = 20;
  const int kSuspendTimeMs = 10000;
  const int kMinPeriodicSamples = 6;
  int frames = kMinPeriodicSamples * kFpsPeriodicIntervalMs * kFps / 1000;
  for (int i = 0; i < frames; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps);
    encoded_image.SetRtpTimestamp(i + 1);
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  }
  // Suspend.
  statistics_proxy_->OnSuspendChange(true);
  fake_clock_.AdvanceTimeMilliseconds(kSuspendTimeMs);

  for (int i = 0; i < frames; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps);
    encoded_image.SetRtpTimestamp(i + 1);
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  }
  // Suspended time interval should not affect the framerate.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.SentFramesPerSecond"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.SentFramesPerSecond", kFps));
}

TEST_F(SendStatisticsProxyTest, CpuLimitedHistogramNotUpdatedWhenDisabled) {
  statistics_proxy_->UpdateAdaptationSettings(kResolutionScalingDisabled,
                                              kResolutionScalingDisabled);

  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i)
    statistics_proxy_->OnIncomingFrame(kWidth, kHeight);

  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.CpuLimitedResolutionInPercent"));
}

TEST_F(SendStatisticsProxyTest, CpuLimitedHistogramUpdated) {
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  cpu_counts.resolution_adaptations = 0;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);

  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i)
    statistics_proxy_->OnIncomingFrame(kWidth, kHeight);

  cpu_counts.resolution_adaptations = 1;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kCpu,
                                         cpu_counts, quality_counts);

  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i)
    statistics_proxy_->OnIncomingFrame(kWidth, kHeight);

  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.CpuLimitedResolutionInPercent"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.CpuLimitedResolutionInPercent", 50));
}

TEST_F(SendStatisticsProxyTest, LifetimeHistogramIsUpdated) {
  const int64_t kTimeSec = 3;
  fake_clock_.AdvanceTimeMilliseconds(kTimeSec * 1000);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.SendStreamLifetimeInSeconds"));
  EXPECT_METRIC_EQ(
      1,
      metrics::NumEvents("WebRTC.Video.SendStreamLifetimeInSeconds", kTimeSec));
}

TEST_F(SendStatisticsProxyTest, CodecTypeHistogramIsUpdated) {
  fake_clock_.AdvanceTime(metrics::kMinRunTime);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Encoder.CodecType"));
}

TEST_F(SendStatisticsProxyTest, PauseEventHistogramIsUpdated) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);

  // Min runtime has passed.
  fake_clock_.AdvanceTime(metrics::kMinRunTime);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.NumberOfPauseEvents"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.NumberOfPauseEvents", 0));
}

TEST_F(SendStatisticsProxyTest,
       PauseEventHistogramIsNotUpdatedIfMinRuntimeHasNotPassed) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);

  // Min runtime has not passed.
  fake_clock_.AdvanceTime(metrics::kMinRunTime - TimeDelta::Millis(1));
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.NumberOfPauseEvents"));
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.PausedTimeInPercent"));
}

TEST_F(SendStatisticsProxyTest,
       PauseEventHistogramIsNotUpdatedIfNoMediaIsSent) {
  // First RTP packet not sent.
  fake_clock_.AdvanceTime(metrics::kMinRunTime);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.NumberOfPauseEvents"));
}

TEST_F(SendStatisticsProxyTest, NoPauseEvent) {
  // First RTP packet sent and min runtime passed.
  UpdateDataCounters(kFirstSsrc);

  // No change. Video: 10000 ms, paused: 0 ms (0%).
  statistics_proxy_->OnSetEncoderTargetRate(50000);
  fake_clock_.AdvanceTime(metrics::kMinRunTime);
  statistics_proxy_->OnSetEncoderTargetRate(0);  // VideoSendStream::Stop

  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.NumberOfPauseEvents"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.NumberOfPauseEvents", 0));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.PausedTimeInPercent"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.PausedTimeInPercent", 0));
}

TEST_F(SendStatisticsProxyTest, OnePauseEvent) {
  // First RTP packet sent and min runtime passed.
  UpdateDataCounters(kFirstSsrc);

  // One change. Video: 7000 ms, paused: 3000 ms (30%).
  statistics_proxy_->OnSetEncoderTargetRate(50000);
  fake_clock_.AdvanceTimeMilliseconds(7000);
  statistics_proxy_->OnSetEncoderTargetRate(0);
  fake_clock_.AdvanceTimeMilliseconds(3000);
  statistics_proxy_->OnSetEncoderTargetRate(0);  // VideoSendStream::Stop

  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.NumberOfPauseEvents"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.NumberOfPauseEvents", 1));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.PausedTimeInPercent"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.PausedTimeInPercent", 30));
}

TEST_F(SendStatisticsProxyTest, TwoPauseEvents) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);

  // Two changes. Video: 19000 ms, paused: 1000 ms (5%).
  statistics_proxy_->OnSetEncoderTargetRate(0);
  fake_clock_.AdvanceTimeMilliseconds(1000);
  statistics_proxy_->OnSetEncoderTargetRate(50000);  // Starts on bitrate > 0.
  fake_clock_.AdvanceTimeMilliseconds(7000);
  statistics_proxy_->OnSetEncoderTargetRate(60000);
  fake_clock_.AdvanceTimeMilliseconds(3000);
  statistics_proxy_->OnSetEncoderTargetRate(0);
  fake_clock_.AdvanceTimeMilliseconds(250);
  statistics_proxy_->OnSetEncoderTargetRate(0);
  fake_clock_.AdvanceTimeMilliseconds(750);
  statistics_proxy_->OnSetEncoderTargetRate(60000);
  fake_clock_.AdvanceTimeMilliseconds(5000);
  statistics_proxy_->OnSetEncoderTargetRate(50000);
  fake_clock_.AdvanceTimeMilliseconds(4000);
  statistics_proxy_->OnSetEncoderTargetRate(0);  // VideoSendStream::Stop

  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.NumberOfPauseEvents"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.NumberOfPauseEvents", 2));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.PausedTimeInPercent"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.PausedTimeInPercent", 5));
}

TEST_F(SendStatisticsProxyTest,
       PausedTimeHistogramIsNotUpdatedIfMinRuntimeHasNotPassed) {
  // First RTP packet sent.
  UpdateDataCounters(kFirstSsrc);
  fake_clock_.AdvanceTime(metrics::kMinRunTime);

  // Min runtime has not passed.
  statistics_proxy_->OnSetEncoderTargetRate(50000);
  fake_clock_.AdvanceTime(metrics::kMinRunTime - TimeDelta::Millis(1));
  statistics_proxy_->OnSetEncoderTargetRate(0);  // VideoSendStream::Stop

  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.PausedTimeInPercent"));
}

TEST_F(SendStatisticsProxyTest, VerifyQpHistogramStats_Vp8) {
  EncodedImage encoded_image;
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP8;

  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i) {
    encoded_image.SetSimulcastIndex(0);
    encoded_image.qp_ = kQpIdx0;
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
    encoded_image.SetSimulcastIndex(1);
    encoded_image.qp_ = kQpIdx1;
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  }
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Encoded.Qp.Vp8.S0"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.Encoded.Qp.Vp8.S0", kQpIdx0));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Encoded.Qp.Vp8.S1"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.Encoded.Qp.Vp8.S1", kQpIdx1));
}

TEST_F(SendStatisticsProxyTest, VerifyQpHistogramStats_Vp8OneSsrc) {
  VideoSendStream::Config config(nullptr);
  config.rtp.ssrcs.push_back(kFirstSsrc);
  statistics_proxy_.reset(new SendStatisticsProxy(
      env_, config, VideoEncoderConfig::ContentType::kRealtimeVideo));

  EncodedImage encoded_image;
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP8;

  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i) {
    encoded_image.SetSimulcastIndex(0);
    encoded_image.qp_ = kQpIdx0;
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  }
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Encoded.Qp.Vp8"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.Encoded.Qp.Vp8", kQpIdx0));
}

TEST_F(SendStatisticsProxyTest, VerifyQpHistogramStats_Vp9Svc) {
  EncodedImage encoded_image;
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP9;
  codec_info.codecSpecific.VP9.num_spatial_layers = 2;

  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i) {
    encoded_image.qp_ = kQpIdx0;
    encoded_image.SetSpatialIndex(0);
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
    encoded_image.qp_ = kQpIdx1;
    encoded_image.SetSpatialIndex(1);
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  }
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Encoded.Qp.Vp9.S0"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.Encoded.Qp.Vp9.S0", kQpIdx0));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Encoded.Qp.Vp9.S1"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.Encoded.Qp.Vp9.S1", kQpIdx1));
}

TEST_F(SendStatisticsProxyTest, VerifyQpHistogramStats_Vp9OneSpatialLayer) {
  VideoSendStream::Config config(nullptr);
  config.rtp.ssrcs.push_back(kFirstSsrc);
  statistics_proxy_.reset(new SendStatisticsProxy(
      env_, config, VideoEncoderConfig::ContentType::kRealtimeVideo));

  EncodedImage encoded_image;
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP9;
  codec_info.codecSpecific.VP9.num_spatial_layers = 1;

  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i) {
    encoded_image.qp_ = kQpIdx0;
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  }
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Encoded.Qp.Vp9"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.Encoded.Qp.Vp9", kQpIdx0));
}

TEST_F(SendStatisticsProxyTest, VerifyQpHistogramStats_H264) {
  EncodedImage encoded_image;
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecH264;

  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i) {
    encoded_image.SetSimulcastIndex(0);
    encoded_image.qp_ = kQpIdx0;
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
    encoded_image.SetSimulcastIndex(1);
    encoded_image.qp_ = kQpIdx1;
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  }
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Encoded.Qp.H264.S0"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.Encoded.Qp.H264.S0", kQpIdx0));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Encoded.Qp.H264.S1"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.Encoded.Qp.H264.S1", kQpIdx1));
}

TEST_F(SendStatisticsProxyTest, VerifyQpHistogramStats_Av1) {
  EncodedImage encoded_image;
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecAV1;

  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i) {
    encoded_image.SetSpatialIndex(0);
    encoded_image.qp_ = kQpIdx0;
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
    encoded_image.SetSpatialIndex(1);
    encoded_image.qp_ = kQpIdx1;
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  }
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Encoded.Qp.Av1.S0"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.Encoded.Qp.Av1.S0", kQpIdx0));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Encoded.Qp.Av1.S1"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.Encoded.Qp.Av1.S1", kQpIdx1));
}

TEST_F(SendStatisticsProxyTest, VerifyQpHistogramStats_H265) {
  EncodedImage encoded_image;
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecH265;

  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i) {
    encoded_image.SetSimulcastIndex(0);
    encoded_image.qp_ = kQpIdx0;
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
    encoded_image.SetSimulcastIndex(1);
    encoded_image.qp_ = kQpIdx1;
    statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  }
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Encoded.Qp.H265.S0"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.Encoded.Qp.H265.S0", kQpIdx0));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Encoded.Qp.H265.S1"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.Encoded.Qp.H265.S1", kQpIdx1));
}

TEST_F(SendStatisticsProxyTest,
       BandwidthLimitedHistogramsNotUpdatedForOneStream) {
  // Configure one stream.
  VideoEncoderConfig config;
  config.content_type = VideoEncoderConfig::ContentType::kRealtimeVideo;
  VideoStream stream1;
  stream1.width = kWidth;
  stream1.height = kHeight;
  statistics_proxy_->OnEncoderReconfigured(config, {stream1});

  const int64_t kMaxEncodedFrameWindowMs = 800;
  const int kFps = 20;
  const int kNumFramesPerWindow = kFps * kMaxEncodedFrameWindowMs / 1000;
  const int kMinSamples =  // Sample added when removed from EncodedFrameMap.
      SendStatisticsProxy::kMinRequiredMetricsSamples + kNumFramesPerWindow;

  // Stream encoded.
  EncodedImage encoded_image;
  encoded_image._encodedWidth = kWidth;
  encoded_image._encodedHeight = kHeight;
  for (int i = 0; i < kMinSamples; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps);
    encoded_image.SetRtpTimestamp(encoded_image.RtpTimestamp() +
                                  (kRtpClockRateHz / kFps));
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  }

  // Histograms are updated when the statistics_proxy_ is deleted.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(0, metrics::NumSamples(
                          "WebRTC.Video.BandwidthLimitedResolutionInPercent"));
  EXPECT_METRIC_EQ(0, metrics::NumSamples(
                          "WebRTC.Video.BandwidthLimitedResolutionsDisabled"));
}

TEST_F(SendStatisticsProxyTest,
       BandwidthLimitedHistogramsUpdatedForTwoStreams_NoResolutionDisabled) {
  // Configure two streams.
  VideoEncoderConfig config;
  config.content_type = VideoEncoderConfig::ContentType::kRealtimeVideo;
  VideoStream stream1;
  stream1.width = kWidth / 2;
  stream1.height = kHeight / 2;
  VideoStream stream2;
  stream2.width = kWidth;
  stream2.height = kHeight;
  statistics_proxy_->OnEncoderReconfigured(config, {stream1, stream2});

  const int64_t kMaxEncodedFrameWindowMs = 800;
  const int kFps = 20;
  const int kNumFramesPerWindow = kFps * kMaxEncodedFrameWindowMs / 1000;
  const int kMinSamples =  // Sample added when removed from EncodedFrameMap.
      SendStatisticsProxy::kMinRequiredMetricsSamples + kNumFramesPerWindow;

  // Two streams encoded.
  EncodedImage encoded_image;
  for (int i = 0; i < kMinSamples; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps);
    encoded_image.SetRtpTimestamp(encoded_image.RtpTimestamp() +
                                  (kRtpClockRateHz / kFps));
    encoded_image._encodedWidth = kWidth;
    encoded_image._encodedHeight = kHeight;
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
    encoded_image._encodedWidth = kWidth / 2;
    encoded_image._encodedHeight = kHeight / 2;
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  }

  // Histograms are updated when the statistics_proxy_ is deleted.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples(
                          "WebRTC.Video.BandwidthLimitedResolutionInPercent"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.BandwidthLimitedResolutionInPercent",
                            0));
  // No resolution disabled.
  EXPECT_METRIC_EQ(0, metrics::NumSamples(
                          "WebRTC.Video.BandwidthLimitedResolutionsDisabled"));
}

TEST_F(SendStatisticsProxyTest,
       BandwidthLimitedHistogramsUpdatedForTwoStreams_OneResolutionDisabled) {
  // Configure two streams.
  VideoEncoderConfig config;
  config.content_type = VideoEncoderConfig::ContentType::kRealtimeVideo;
  VideoStream stream1;
  stream1.width = kWidth / 2;
  stream1.height = kHeight / 2;
  VideoStream stream2;
  stream2.width = kWidth;
  stream2.height = kHeight;
  statistics_proxy_->OnEncoderReconfigured(config, {stream1, stream2});

  const int64_t kMaxEncodedFrameWindowMs = 800;
  const int kFps = 20;
  const int kNumFramesPerWindow = kFps * kMaxEncodedFrameWindowMs / 1000;
  const int kMinSamples =  // Sample added when removed from EncodedFrameMap.
      SendStatisticsProxy::kMinRequiredMetricsSamples + kNumFramesPerWindow;

  // One stream encoded.
  EncodedImage encoded_image;
  encoded_image._encodedWidth = kWidth / 2;
  encoded_image._encodedHeight = kHeight / 2;
  for (int i = 0; i < kMinSamples; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps);
    encoded_image.SetRtpTimestamp(encoded_image.RtpTimestamp() +
                                  (kRtpClockRateHz / kFps));
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  }

  // Histograms are updated when the statistics_proxy_ is deleted.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples(
                          "WebRTC.Video.BandwidthLimitedResolutionInPercent"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.BandwidthLimitedResolutionInPercent",
                            100));
  // One resolution disabled.
  EXPECT_METRIC_EQ(1, metrics::NumSamples(
                          "WebRTC.Video.BandwidthLimitedResolutionsDisabled"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.BandwidthLimitedResolutionsDisabled",
                            1));
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitedHistogramsNotUpdatedWhenDisabled) {
  statistics_proxy_->UpdateAdaptationSettings(kFramerateScalingDisabled,
                                              kScalingDisabled);
  EncodedImage encoded_image;
  encoded_image.SetSimulcastIndex(0);
  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i)
    statistics_proxy_->OnSendEncodedImage(encoded_image, &kDefaultCodecInfo);

  // Histograms are updated when the statistics_proxy_ is deleted.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.QualityLimitedResolutionInPercent"));
  EXPECT_METRIC_EQ(0, metrics::NumSamples(
                          "WebRTC.Video.QualityLimitedResolutionDownscales"));
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitedHistogramsUpdatedWhenEnabled_NoResolutionDownscale) {
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  EncodedImage encoded_image;
  encoded_image.SetSimulcastIndex(0);
  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i)
    statistics_proxy_->OnSendEncodedImage(encoded_image, &kDefaultCodecInfo);

  // Histograms are updated when the statistics_proxy_ is deleted.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.QualityLimitedResolutionInPercent"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents(
                          "WebRTC.Video.QualityLimitedResolutionInPercent", 0));
  // No resolution downscale.
  EXPECT_METRIC_EQ(0, metrics::NumSamples(
                          "WebRTC.Video.QualityLimitedResolutionDownscales"));
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitedHistogramsUpdatedWhenEnabled_TwoResolutionDownscales) {
  const int kDownscales = 2;
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  quality_counts.resolution_adaptations = kDownscales;
  statistics_proxy_->UpdateAdaptationSettings(kScalingEnabled, kScalingEnabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  EncodedImage encoded_image;
  encoded_image.SetSimulcastIndex(0);
  for (int i = 0; i < SendStatisticsProxy::kMinRequiredMetricsSamples; ++i)
    statistics_proxy_->OnSendEncodedImage(encoded_image, &kDefaultCodecInfo);
  // Histograms are updated when the statistics_proxy_ is deleted.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.QualityLimitedResolutionInPercent"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.QualityLimitedResolutionInPercent",
                            100));
  // Resolution downscales.
  EXPECT_METRIC_EQ(1, metrics::NumSamples(
                          "WebRTC.Video.QualityLimitedResolutionDownscales"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.QualityLimitedResolutionDownscales",
                            kDownscales));
}

TEST_F(SendStatisticsProxyTest, GetStatsReportsBandwidthLimitedResolution) {
  // Initially false.
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);

  // Configure two streams.
  VideoEncoderConfig config;
  config.content_type = VideoEncoderConfig::ContentType::kRealtimeVideo;
  config.number_of_streams = 2;
  VideoStream stream1;
  stream1.width = kWidth / 2;
  stream1.height = kHeight / 2;
  VideoStream stream2;
  stream2.width = kWidth;
  stream2.height = kHeight;
  statistics_proxy_->OnEncoderReconfigured(config, {stream1, stream2});

  // One stream encoded.
  EncodedImage encoded_image;
  encoded_image._encodedWidth = kWidth / 2;
  encoded_image._encodedHeight = kHeight / 2;

  // Resolution scaled due to quality.
  VideoAdaptationCounters cpu_counts;
  VideoAdaptationCounters quality_counts;
  quality_counts.resolution_adaptations = 1;
  statistics_proxy_->UpdateAdaptationSettings(kFramerateScalingDisabled,
                                              kFramerateScalingDisabled);
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_resolution);

  // Adapt up.
  quality_counts.resolution_adaptations = 0;
  statistics_proxy_->OnAdaptationChanged(VideoAdaptationReason::kQuality,
                                         cpu_counts, quality_counts);
  statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);

  // Bw disabled one layer.
  VideoCodec codec;
  codec.numberOfSimulcastStreams = 2;
  codec.simulcastStream[0].active = true;
  codec.simulcastStream[1].active = true;
  VideoBitrateAllocation allocation;
  // Some positive bitrate only on the second stream.
  allocation.SetBitrate(1, 0, 10000);
  allocation.set_bw_limited(true);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_resolution);

  // Revert for the next test.
  allocation.set_bw_limited(false);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_FALSE(statistics_proxy_->GetStats().bw_limited_resolution);

  // Internal encoder scaler reduced resolution.
  statistics_proxy_->OnEncoderInternalScalerUpdate(/*scaled=*/true);
  EXPECT_TRUE(statistics_proxy_->GetStats().bw_limited_resolution);
}

TEST_F(SendStatisticsProxyTest, GetStatsReportsTargetMediaBitrate) {
  // Initially zero.
  EXPECT_EQ(0, statistics_proxy_->GetStats().target_media_bitrate_bps);

  const int kBitrate = 100000;
  statistics_proxy_->OnSetEncoderTargetRate(kBitrate);
  EXPECT_EQ(kBitrate, statistics_proxy_->GetStats().target_media_bitrate_bps);

  statistics_proxy_->OnSetEncoderTargetRate(0);
  EXPECT_EQ(0, statistics_proxy_->GetStats().target_media_bitrate_bps);
}

TEST_F(SendStatisticsProxyTest, NoSubstreams) {
  uint32_t excluded_ssrc =
      std::max(*absl::c_max_element(config_.rtp.ssrcs),
               *absl::c_max_element(config_.rtp.rtx.ssrcs)) +
      1;
  // From ReportBlockDataObserver.
  ReportBlockDataObserver* rtcp_callback = statistics_proxy_.get();
  rtcp::ReportBlock report_block;
  report_block.SetMediaSsrc(excluded_ssrc);
  ReportBlockData data;
  data.SetReportBlock(0, report_block, Timestamp::Zero(), Timestamp::Zero());
  rtcp_callback->OnReportBlockDataUpdated(data);

  // From BitrateStatisticsObserver.
  uint32_t total = 0;
  uint32_t retransmit = 0;
  BitrateStatisticsObserver* bitrate_observer = statistics_proxy_.get();
  bitrate_observer->Notify(total, retransmit, excluded_ssrc);

  // From FrameCountObserver.
  FrameCountObserver* fps_observer = statistics_proxy_.get();
  FrameCounts frame_counts;
  frame_counts.key_frames = 1;
  fps_observer->FrameCountUpdated(frame_counts, excluded_ssrc);

  VideoSendStream::Stats stats = statistics_proxy_->GetStats();
  EXPECT_TRUE(stats.substreams.empty());
}

TEST_F(SendStatisticsProxyTest, EncodedResolutionTimesOut) {
  static const int kEncodedWidth = 123;
  static const int kEncodedHeight = 81;
  EncodedImage encoded_image;
  encoded_image._encodedWidth = kEncodedWidth;
  encoded_image._encodedHeight = kEncodedHeight;
  encoded_image.SetSimulcastIndex(0);

  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP8;

  statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  encoded_image.SetSimulcastIndex(1);
  statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);

  VideoSendStream::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(kEncodedWidth, stats.substreams[config_.rtp.ssrcs[0]].width);
  EXPECT_EQ(kEncodedHeight, stats.substreams[config_.rtp.ssrcs[0]].height);
  EXPECT_EQ(kEncodedWidth, stats.substreams[config_.rtp.ssrcs[1]].width);
  EXPECT_EQ(kEncodedHeight, stats.substreams[config_.rtp.ssrcs[1]].height);

  // Forward almost to timeout, this should not have removed stats.
  fake_clock_.AdvanceTime(SendStatisticsProxy::kStatsTimeout -
                          TimeDelta::Millis(1));
  stats = statistics_proxy_->GetStats();
  EXPECT_EQ(kEncodedWidth, stats.substreams[config_.rtp.ssrcs[0]].width);
  EXPECT_EQ(kEncodedHeight, stats.substreams[config_.rtp.ssrcs[0]].height);

  // Update the first SSRC with bogus RTCP stats to make sure that encoded
  // resolution still times out (no global timeout for all stats).
  ReportBlockDataObserver* rtcp_callback = statistics_proxy_.get();
  rtcp::ReportBlock report_block;
  report_block.SetMediaSsrc(config_.rtp.ssrcs[0]);
  ReportBlockData data;
  data.SetReportBlock(0, report_block, Timestamp::Zero(), Timestamp::Zero());
  rtcp_callback->OnReportBlockDataUpdated(data);

  // Report stats for second SSRC to make sure it's not outdated along with the
  // first SSRC.
  statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);

  // Forward 1 ms, reach timeout, substream 0 should have no resolution
  // reported, but substream 1 should.
  fake_clock_.AdvanceTimeMilliseconds(1);
  stats = statistics_proxy_->GetStats();
  EXPECT_EQ(0, stats.substreams[config_.rtp.ssrcs[0]].width);
  EXPECT_EQ(0, stats.substreams[config_.rtp.ssrcs[0]].height);
  EXPECT_EQ(kEncodedWidth, stats.substreams[config_.rtp.ssrcs[1]].width);
  EXPECT_EQ(kEncodedHeight, stats.substreams[config_.rtp.ssrcs[1]].height);
}

TEST_F(SendStatisticsProxyTest, ClearsResolutionFromInactiveSsrcs) {
  static const int kEncodedWidth = 123;
  static const int kEncodedHeight = 81;
  EncodedImage encoded_image;
  encoded_image._encodedWidth = kEncodedWidth;
  encoded_image._encodedHeight = kEncodedHeight;
  encoded_image.SetSimulcastIndex(0);

  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP8;

  statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  encoded_image.SetSimulcastIndex(1);
  statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);

  statistics_proxy_->OnInactiveSsrc(config_.rtp.ssrcs[1]);
  VideoSendStream::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(kEncodedWidth, stats.substreams[config_.rtp.ssrcs[0]].width);
  EXPECT_EQ(kEncodedHeight, stats.substreams[config_.rtp.ssrcs[0]].height);
  EXPECT_EQ(0, stats.substreams[config_.rtp.ssrcs[1]].width);
  EXPECT_EQ(0, stats.substreams[config_.rtp.ssrcs[1]].height);
}

TEST_F(SendStatisticsProxyTest, ClearsBitratesFromInactiveSsrcs) {
  uint32_t bitrate = 42;
  BitrateStatisticsObserver* observer = statistics_proxy_.get();
  observer->Notify(bitrate, bitrate, config_.rtp.ssrcs[0]);
  observer->Notify(bitrate, bitrate, config_.rtp.ssrcs[1]);

  statistics_proxy_->OnInactiveSsrc(config_.rtp.ssrcs[1]);

  VideoSendStream::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(static_cast<int>(bitrate),
            stats.substreams[config_.rtp.ssrcs[0]].total_bitrate_bps);
  EXPECT_EQ(static_cast<int>(bitrate),
            stats.substreams[config_.rtp.ssrcs[0]].retransmit_bitrate_bps);
  EXPECT_EQ(0, stats.substreams[config_.rtp.ssrcs[1]].total_bitrate_bps);
  EXPECT_EQ(0, stats.substreams[config_.rtp.ssrcs[1]].retransmit_bitrate_bps);
}

TEST_F(SendStatisticsProxyTest, ResetsRtcpCountersOnContentChange) {
  RtcpPacketTypeCounterObserver* proxy =
      static_cast<RtcpPacketTypeCounterObserver*>(statistics_proxy_.get());
  RtcpPacketTypeCounter counters;
  proxy->RtcpPacketTypesCounterUpdated(kFirstSsrc, counters);
  proxy->RtcpPacketTypesCounterUpdated(kSecondSsrc, counters);

  fake_clock_.AdvanceTime(metrics::kMinRunTime);

  counters.nack_packets += 1 * metrics::kMinRunTime.seconds();
  counters.fir_packets += 2 * metrics::kMinRunTime.seconds();
  counters.pli_packets += 3 * metrics::kMinRunTime.seconds();
  counters.unique_nack_requests += 4 * metrics::kMinRunTime.seconds();
  counters.nack_requests += 5 * metrics::kMinRunTime.seconds();

  proxy->RtcpPacketTypesCounterUpdated(kFirstSsrc, counters);
  proxy->RtcpPacketTypesCounterUpdated(kSecondSsrc, counters);

  // Changing content type causes histograms to be reported.
  VideoEncoderConfig config;
  config.content_type = VideoEncoderConfig::ContentType::kScreen;
  statistics_proxy_->OnEncoderReconfigured(config, {});

  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.NackPacketsReceivedPerMinute"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.FirPacketsReceivedPerMinute"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.PliPacketsReceivedPerMinute"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples(
                          "WebRTC.Video.UniqueNackRequestsReceivedInPercent"));

  const int kRate = 60 * 2;  // Packets per minute with two streams.

  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.NackPacketsReceivedPerMinute",
                            1 * kRate));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.FirPacketsReceivedPerMinute",
                            2 * kRate));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.PliPacketsReceivedPerMinute",
                            3 * kRate));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.UniqueNackRequestsReceivedInPercent",
                            4 * 100 / 5));

  // New start time but same counter values.
  proxy->RtcpPacketTypesCounterUpdated(kFirstSsrc, counters);
  proxy->RtcpPacketTypesCounterUpdated(kSecondSsrc, counters);

  fake_clock_.AdvanceTime(metrics::kMinRunTime);

  counters.nack_packets += 1 * metrics::kMinRunTime.seconds();
  counters.fir_packets += 2 * metrics::kMinRunTime.seconds();
  counters.pli_packets += 3 * metrics::kMinRunTime.seconds();
  counters.unique_nack_requests += 4 * metrics::kMinRunTime.seconds();
  counters.nack_requests += 5 * metrics::kMinRunTime.seconds();

  proxy->RtcpPacketTypesCounterUpdated(kFirstSsrc, counters);
  proxy->RtcpPacketTypesCounterUpdated(kSecondSsrc, counters);

  SetUp();  // Reset stats proxy also causes histograms to be reported.

  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(
             "WebRTC.Video.Screenshare.NackPacketsReceivedPerMinute"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(
                       "WebRTC.Video.Screenshare.FirPacketsReceivedPerMinute"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(
                       "WebRTC.Video.Screenshare.PliPacketsReceivedPerMinute"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(
             "WebRTC.Video.Screenshare.UniqueNackRequestsReceivedInPercent"));

  EXPECT_METRIC_EQ(
      1,
      metrics::NumEvents(
          "WebRTC.Video.Screenshare.NackPacketsReceivedPerMinute", 1 * kRate));
  EXPECT_METRIC_EQ(
      1,
      metrics::NumEvents("WebRTC.Video.Screenshare.FirPacketsReceivedPerMinute",
                         2 * kRate));
  EXPECT_METRIC_EQ(
      1,
      metrics::NumEvents("WebRTC.Video.Screenshare.PliPacketsReceivedPerMinute",
                         3 * kRate));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents(
             "WebRTC.Video.Screenshare.UniqueNackRequestsReceivedInPercent",
             4 * 100 / 5));
}

TEST_F(SendStatisticsProxyTest, GetStatsReportsIsRtx) {
  StreamDataCountersCallback* proxy =
      static_cast<StreamDataCountersCallback*>(statistics_proxy_.get());
  StreamDataCounters counters;
  proxy->DataCountersUpdated(counters, kFirstSsrc);
  proxy->DataCountersUpdated(counters, kFirstRtxSsrc);

  EXPECT_NE(GetStreamStats(kFirstSsrc).type,
            VideoSendStream::StreamStats::StreamType::kRtx);
  EXPECT_EQ(GetStreamStats(kFirstSsrc).referenced_media_ssrc, std::nullopt);
  EXPECT_EQ(GetStreamStats(kFirstRtxSsrc).type,
            VideoSendStream::StreamStats::StreamType::kRtx);
  EXPECT_EQ(GetStreamStats(kFirstRtxSsrc).referenced_media_ssrc, kFirstSsrc);
}

TEST_F(SendStatisticsProxyTest, GetStatsReportsIsFlexFec) {
  statistics_proxy_.reset(
      new SendStatisticsProxy(env_, GetTestConfigWithFlexFec(),
                              VideoEncoderConfig::ContentType::kRealtimeVideo));

  StreamDataCountersCallback* proxy =
      static_cast<StreamDataCountersCallback*>(statistics_proxy_.get());
  StreamDataCounters counters;
  proxy->DataCountersUpdated(counters, kFirstSsrc);
  proxy->DataCountersUpdated(counters, kFlexFecSsrc);

  EXPECT_NE(GetStreamStats(kFirstSsrc).type,
            VideoSendStream::StreamStats::StreamType::kFlexfec);
  EXPECT_EQ(GetStreamStats(kFirstSsrc).referenced_media_ssrc, std::nullopt);
  EXPECT_EQ(GetStreamStats(kFlexFecSsrc).type,
            VideoSendStream::StreamStats::StreamType::kFlexfec);
  EXPECT_EQ(GetStreamStats(kFlexFecSsrc).referenced_media_ssrc, kFirstSsrc);
}

TEST_F(SendStatisticsProxyTest, SendBitratesAreReportedWithFlexFecEnabled) {
  statistics_proxy_.reset(
      new SendStatisticsProxy(env_, GetTestConfigWithFlexFec(),
                              VideoEncoderConfig::ContentType::kRealtimeVideo));

  StreamDataCountersCallback* proxy =
      static_cast<StreamDataCountersCallback*>(statistics_proxy_.get());
  StreamDataCounters counters;
  StreamDataCounters rtx_counters;
  StreamDataCounters flexfec_counters;

  const int kMinRequiredPeriodSamples = 8;
  const int kPeriodIntervalMs = 2000;
  for (int i = 0; i < kMinRequiredPeriodSamples; ++i) {
    counters.transmitted.packets += 20;
    counters.transmitted.header_bytes += 500;
    counters.transmitted.padding_bytes += 1000;
    counters.transmitted.payload_bytes += 2000;
    counters.retransmitted.packets += 2;
    counters.retransmitted.header_bytes += 50;
    counters.retransmitted.padding_bytes += 200;
    counters.retransmitted.payload_bytes += 250;
    flexfec_counters.fec = counters.retransmitted;
    rtx_counters.transmitted = counters.transmitted;
    // Advance one interval and update counters.
    fake_clock_.AdvanceTimeMilliseconds(kPeriodIntervalMs);
    proxy->DataCountersUpdated(counters, kFirstSsrc);
    proxy->DataCountersUpdated(counters, kSecondSsrc);
    proxy->DataCountersUpdated(rtx_counters, kFirstRtxSsrc);
    proxy->DataCountersUpdated(rtx_counters, kSecondRtxSsrc);
    proxy->DataCountersUpdated(flexfec_counters, kFlexFecSsrc);
  }

  statistics_proxy_.reset();
  // Interval: 3500 bytes * 4 / 2 sec = 7000 bytes / sec  = 56 kbps
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.BitrateSentInKbps"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.BitrateSentInKbps", 56));
  // Interval: 3500 bytes * 2 / 2 sec = 3500 bytes / sec  = 28 kbps
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.RtxBitrateSentInKbps"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.RtxBitrateSentInKbps", 28));
  // Interval: (2000 - 250) bytes / 2 sec = 1750 bytes / sec  = 14 kbps
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.MediaBitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.MediaBitrateSentInKbps", 14));
  // Interval: 1000 bytes * 4 / 2 sec = 2000 bytes / sec = 16 kbps
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.PaddingBitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.PaddingBitrateSentInKbps", 16));
  // Interval: 500 bytes / 2 sec = 200 bytes / sec = 2 kbps
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.FecBitrateSentInKbps"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.FecBitrateSentInKbps", 2));
  // Interval: 500 bytes * 2 / 2 sec = 375 bytes / sec = 4 kbps
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.RetransmittedBitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.RetransmittedBitrateSentInKbps", 4));
}

TEST_F(SendStatisticsProxyTest, ResetsRtpCountersOnContentChange) {
  StreamDataCountersCallback* proxy =
      static_cast<StreamDataCountersCallback*>(statistics_proxy_.get());
  StreamDataCounters counters;
  StreamDataCounters rtx_counters;
  counters.first_packet_time = fake_clock_.CurrentTime();

  const int kMinRequiredPeriodSamples = 8;
  const int kPeriodIntervalMs = 2000;
  for (int i = 0; i < kMinRequiredPeriodSamples; ++i) {
    counters.transmitted.packets += 20;
    counters.transmitted.header_bytes += 500;
    counters.transmitted.padding_bytes += 1000;
    counters.transmitted.payload_bytes += 2000;
    counters.retransmitted.packets += 2;
    counters.retransmitted.header_bytes += 25;
    counters.retransmitted.padding_bytes += 100;
    counters.retransmitted.payload_bytes += 250;
    counters.fec = counters.retransmitted;
    rtx_counters.transmitted = counters.transmitted;
    // Advance one interval and update counters.
    fake_clock_.AdvanceTimeMilliseconds(kPeriodIntervalMs);
    proxy->DataCountersUpdated(counters, kFirstSsrc);
    proxy->DataCountersUpdated(counters, kSecondSsrc);
    proxy->DataCountersUpdated(rtx_counters, kFirstRtxSsrc);
    proxy->DataCountersUpdated(rtx_counters, kSecondRtxSsrc);
  }

  // Changing content type causes histograms to be reported.
  VideoEncoderConfig config;
  config.content_type = VideoEncoderConfig::ContentType::kScreen;
  statistics_proxy_->OnEncoderReconfigured(config, {});

  // Interval: 3500 bytes * 4 / 2 sec = 7000 bytes / sec  = 56 kbps
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.BitrateSentInKbps"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.BitrateSentInKbps", 56));
  // Interval: 3500 bytes * 2 / 2 sec = 3500 bytes / sec  = 28 kbps
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.RtxBitrateSentInKbps"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.RtxBitrateSentInKbps", 28));
  // Interval: (2000 - 2 * 250) bytes / 2 sec = 1500 bytes / sec  = 12 kbps
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.MediaBitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.MediaBitrateSentInKbps", 12));
  // Interval: 1000 bytes * 4 / 2 sec = 2000 bytes / sec = 16 kbps
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.PaddingBitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.PaddingBitrateSentInKbps", 16));
  // Interval: 375 bytes * 2 / 2 sec = 375 bytes / sec = 3 kbps
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.FecBitrateSentInKbps"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.FecBitrateSentInKbps", 3));
  // Interval: 375 bytes * 2 / 2 sec = 375 bytes / sec = 3 kbps
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.RetransmittedBitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.RetransmittedBitrateSentInKbps", 3));

  // New metric counters but same data counters.
  // Double counter values, this should result in the same counts as before but
  // with new histogram names.
  for (int i = 0; i < kMinRequiredPeriodSamples; ++i) {
    counters.transmitted.packets += 20;
    counters.transmitted.header_bytes += 500;
    counters.transmitted.padding_bytes += 1000;
    counters.transmitted.payload_bytes += 2000;
    counters.retransmitted.packets += 2;
    counters.retransmitted.header_bytes += 25;
    counters.retransmitted.padding_bytes += 100;
    counters.retransmitted.payload_bytes += 250;
    counters.fec = counters.retransmitted;
    rtx_counters.transmitted = counters.transmitted;
    // Advance one interval and update counters.
    fake_clock_.AdvanceTimeMilliseconds(kPeriodIntervalMs);
    proxy->DataCountersUpdated(counters, kFirstSsrc);
    proxy->DataCountersUpdated(counters, kSecondSsrc);
    proxy->DataCountersUpdated(rtx_counters, kFirstRtxSsrc);
    proxy->DataCountersUpdated(rtx_counters, kSecondRtxSsrc);
  }

  // Reset stats proxy also causes histograms to be reported.
  statistics_proxy_.reset();

  // Interval: 3500 bytes * 4 / 2 sec = 7000 bytes / sec  = 56 kbps
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.Screenshare.BitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.Screenshare.BitrateSentInKbps", 56));
  // Interval: 3500 bytes * 2 / 2 sec = 3500 bytes / sec  = 28 kbps
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.Screenshare.RtxBitrateSentInKbps"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents(
                          "WebRTC.Video.Screenshare.RtxBitrateSentInKbps", 28));
  // Interval: (2000 - 2 * 250) bytes / 2 sec = 1500 bytes / sec  = 12 kbps
  EXPECT_METRIC_EQ(1, metrics::NumSamples(
                          "WebRTC.Video.Screenshare.MediaBitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.Screenshare.MediaBitrateSentInKbps",
                            12));
  // Interval: 1000 bytes * 4 / 2 sec = 2000 bytes / sec = 16 kbps
  EXPECT_METRIC_EQ(1, metrics::NumSamples(
                          "WebRTC.Video.Screenshare.PaddingBitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.Screenshare.PaddingBitrateSentInKbps",
                            16));
  // Interval: 375 bytes * 2 / 2 sec = 375 bytes / sec = 3 kbps
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.Screenshare.FecBitrateSentInKbps"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents(
                          "WebRTC.Video.Screenshare.FecBitrateSentInKbps", 3));
  // Interval: 375 bytes * 2 / 2 sec = 375 bytes / sec = 3 kbps
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(
             "WebRTC.Video.Screenshare.RetransmittedBitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents(
             "WebRTC.Video.Screenshare.RetransmittedBitrateSentInKbps", 3));
}

TEST_F(SendStatisticsProxyTest, RtxBitrateIsZeroWhenEnabledAndNoRtxDataIsSent) {
  StreamDataCountersCallback* proxy =
      static_cast<StreamDataCountersCallback*>(statistics_proxy_.get());
  StreamDataCounters counters;
  StreamDataCounters rtx_counters;

  const int kMinRequiredPeriodSamples = 8;
  const int kPeriodIntervalMs = 2000;
  for (int i = 0; i < kMinRequiredPeriodSamples; ++i) {
    counters.transmitted.packets += 20;
    counters.transmitted.header_bytes += 500;
    counters.transmitted.payload_bytes += 2000;
    counters.fec = counters.retransmitted;
    // Advance one interval and update counters.
    fake_clock_.AdvanceTimeMilliseconds(kPeriodIntervalMs);
    proxy->DataCountersUpdated(counters, kFirstSsrc);
  }

  // RTX enabled. No data sent over RTX.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.RtxBitrateSentInKbps"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.RtxBitrateSentInKbps", 0));
}

TEST_F(SendStatisticsProxyTest, RtxBitrateNotReportedWhenNotEnabled) {
  VideoSendStream::Config config(nullptr);
  config.rtp.ssrcs.push_back(kFirstSsrc);  // RTX not configured.
  statistics_proxy_.reset(new SendStatisticsProxy(
      env_, config, VideoEncoderConfig::ContentType::kRealtimeVideo));

  StreamDataCountersCallback* proxy =
      static_cast<StreamDataCountersCallback*>(statistics_proxy_.get());
  StreamDataCounters counters;

  const int kMinRequiredPeriodSamples = 8;
  const int kPeriodIntervalMs = 2000;
  for (int i = 0; i < kMinRequiredPeriodSamples; ++i) {
    counters.transmitted.packets += 20;
    counters.transmitted.header_bytes += 500;
    counters.transmitted.payload_bytes += 2000;
    counters.fec = counters.retransmitted;
    // Advance one interval and update counters.
    fake_clock_.AdvanceTimeMilliseconds(kPeriodIntervalMs);
    proxy->DataCountersUpdated(counters, kFirstSsrc);
  }

  // RTX not enabled.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.RtxBitrateSentInKbps"));
}

TEST_F(SendStatisticsProxyTest, FecBitrateIsZeroWhenEnabledAndNoFecDataIsSent) {
  StreamDataCountersCallback* proxy =
      static_cast<StreamDataCountersCallback*>(statistics_proxy_.get());
  StreamDataCounters counters;
  StreamDataCounters rtx_counters;

  const int kMinRequiredPeriodSamples = 8;
  const int kPeriodIntervalMs = 2000;
  for (int i = 0; i < kMinRequiredPeriodSamples; ++i) {
    counters.transmitted.packets += 20;
    counters.transmitted.header_bytes += 500;
    counters.transmitted.payload_bytes += 2000;
    // Advance one interval and update counters.
    fake_clock_.AdvanceTimeMilliseconds(kPeriodIntervalMs);
    proxy->DataCountersUpdated(counters, kFirstSsrc);
  }

  // FEC enabled. No FEC data sent.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.FecBitrateSentInKbps"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.FecBitrateSentInKbps", 0));
}

TEST_F(SendStatisticsProxyTest, FecBitrateNotReportedWhenNotEnabled) {
  VideoSendStream::Config config(nullptr);
  config.rtp.ssrcs.push_back(kFirstSsrc);  // FEC not configured.
  statistics_proxy_.reset(new SendStatisticsProxy(
      env_, config, VideoEncoderConfig::ContentType::kRealtimeVideo));

  StreamDataCountersCallback* proxy =
      static_cast<StreamDataCountersCallback*>(statistics_proxy_.get());
  StreamDataCounters counters;

  const int kMinRequiredPeriodSamples = 8;
  const int kPeriodIntervalMs = 2000;
  for (int i = 0; i < kMinRequiredPeriodSamples; ++i) {
    counters.transmitted.packets += 20;
    counters.transmitted.header_bytes += 500;
    counters.transmitted.payload_bytes += 2000;
    counters.fec = counters.retransmitted;
    // Advance one interval and update counters.
    fake_clock_.AdvanceTimeMilliseconds(kPeriodIntervalMs);
    proxy->DataCountersUpdated(counters, kFirstSsrc);
  }

  // FEC not enabled.
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.FecBitrateSentInKbps"));
}

TEST_F(SendStatisticsProxyTest, GetStatsReportsEncoderImplementationName) {
  const std::string kName = "encoderName";
  statistics_proxy_->OnEncoderImplementationChanged(EncoderImplementation{
      .name = kName,
      .is_hardware_accelerated = true,
  });
  EXPECT_EQ(kName, statistics_proxy_->GetStats().encoder_implementation_name);
  EXPECT_THAT(statistics_proxy_->GetStats().power_efficient_encoder,
              ::testing::IsTrue());
}

TEST_F(SendStatisticsProxyTest, Vp9SvcLowSpatialLayerDoesNotUpdateResolution) {
  static const int kEncodedWidth = 123;
  static const int kEncodedHeight = 81;
  EncodedImage encoded_image;
  encoded_image._encodedWidth = kEncodedWidth;
  encoded_image._encodedHeight = kEncodedHeight;
  encoded_image.SetSpatialIndex(0);

  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP9;

  // For first picture, it is expected that low layer updates resolution.
  codec_info.end_of_picture = false;
  statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  VideoSendStream::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(kEncodedWidth, stats.substreams[config_.rtp.ssrcs[0]].width);
  EXPECT_EQ(kEncodedHeight, stats.substreams[config_.rtp.ssrcs[0]].height);

  // Top layer updates resolution.
  encoded_image._encodedWidth = kEncodedWidth * 2;
  encoded_image._encodedHeight = kEncodedHeight * 2;
  codec_info.end_of_picture = true;
  statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  stats = statistics_proxy_->GetStats();
  EXPECT_EQ(kEncodedWidth * 2, stats.substreams[config_.rtp.ssrcs[0]].width);
  EXPECT_EQ(kEncodedHeight * 2, stats.substreams[config_.rtp.ssrcs[0]].height);

  // Low layer of next frame doesn't update resolution.
  encoded_image._encodedWidth = kEncodedWidth;
  encoded_image._encodedHeight = kEncodedHeight;
  codec_info.end_of_picture = false;
  statistics_proxy_->OnSendEncodedImage(encoded_image, &codec_info);
  stats = statistics_proxy_->GetStats();
  EXPECT_EQ(kEncodedWidth * 2, stats.substreams[config_.rtp.ssrcs[0]].width);
  EXPECT_EQ(kEncodedHeight * 2, stats.substreams[config_.rtp.ssrcs[0]].height);
}

class ForcedFallbackTest : public SendStatisticsProxyTest {
 public:
  explicit ForcedFallbackTest(const std::string& field_trials)
      : SendStatisticsProxyTest(field_trials) {
    codec_info_.codecType = kVideoCodecVP8;
    codec_info_.codecSpecific.VP8.temporalIdx = 0;
    encoded_image_._encodedWidth = kWidth;
    encoded_image_._encodedHeight = kHeight;
    encoded_image_.SetSimulcastIndex(0);
  }

  ~ForcedFallbackTest() override {}

 protected:
  void InsertEncodedFrames(int num_frames, int interval_ms) {
    statistics_proxy_->OnEncoderImplementationChanged(
        {.name = codec_name_, .is_hardware_accelerated = false});

    // First frame is not updating stats, insert initial frame.
    if (statistics_proxy_->GetStats().frames_encoded == 0) {
      statistics_proxy_->OnSendEncodedImage(encoded_image_, &codec_info_);
    }
    for (int i = 0; i < num_frames; ++i) {
      statistics_proxy_->OnSendEncodedImage(encoded_image_, &codec_info_);
      fake_clock_.AdvanceTimeMilliseconds(interval_ms);
    }
    // Add frame to include last time interval.
    statistics_proxy_->OnSendEncodedImage(encoded_image_, &codec_info_);
  }

  EncodedImage encoded_image_;
  CodecSpecificInfo codec_info_;
  std::string codec_name_;
  const std::string kPrefix = "WebRTC.Video.Encoder.ForcedSw";
  const int kFrameIntervalMs = 1000;
  const int kMinFrames = 20;  // Min run time 20 sec.
};

class ForcedFallbackDisabled : public ForcedFallbackTest {
 public:
  ForcedFallbackDisabled()
      : ForcedFallbackTest("WebRTC-VP8-Forced-Fallback-Encoder-v2/Disabled-1," +
                           std::to_string(kWidth * kHeight) + ",3/") {}
};

class ForcedFallbackEnabled : public ForcedFallbackTest {
 public:
  ForcedFallbackEnabled()
      : ForcedFallbackTest("WebRTC-VP8-Forced-Fallback-Encoder-v2/Enabled-1," +
                           std::to_string(kWidth * kHeight) + ",3/") {}
};

TEST_F(ForcedFallbackEnabled, StatsNotUpdatedIfMinRunTimeHasNotPassed) {
  InsertEncodedFrames(kMinFrames, kFrameIntervalMs - 1);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples(kPrefix + "FallbackTimeInPercent.Vp8"));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples(kPrefix + "FallbackChangesPerMinute.Vp8"));
}

TEST_F(ForcedFallbackEnabled, StatsUpdated) {
  InsertEncodedFrames(kMinFrames, kFrameIntervalMs);
  EXPECT_FALSE(statistics_proxy_->GetStats().has_entered_low_resolution);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(kPrefix + "FallbackTimeInPercent.Vp8"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents(kPrefix + "FallbackTimeInPercent.Vp8", 0));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(kPrefix + "FallbackChangesPerMinute.Vp8"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents(kPrefix + "FallbackChangesPerMinute.Vp8", 0));
}

TEST_F(ForcedFallbackEnabled, StatsNotUpdatedIfNotVp8) {
  codec_info_.codecType = kVideoCodecVP9;
  InsertEncodedFrames(kMinFrames, kFrameIntervalMs);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples(kPrefix + "FallbackTimeInPercent.Vp8"));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples(kPrefix + "FallbackChangesPerMinute.Vp8"));
}

TEST_F(ForcedFallbackEnabled, StatsNotUpdatedForTemporalLayers) {
  codec_info_.codecSpecific.VP8.temporalIdx = 1;
  InsertEncodedFrames(kMinFrames, kFrameIntervalMs);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples(kPrefix + "FallbackTimeInPercent.Vp8"));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples(kPrefix + "FallbackChangesPerMinute.Vp8"));
}

TEST_F(ForcedFallbackEnabled, StatsNotUpdatedForSimulcast) {
  encoded_image_.SetSimulcastIndex(1);
  InsertEncodedFrames(kMinFrames, kFrameIntervalMs);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples(kPrefix + "FallbackTimeInPercent.Vp8"));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples(kPrefix + "FallbackChangesPerMinute.Vp8"));
}

TEST_F(ForcedFallbackDisabled, StatsNotUpdatedIfNoFieldTrial) {
  InsertEncodedFrames(kMinFrames, kFrameIntervalMs);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples(kPrefix + "FallbackTimeInPercent.Vp8"));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples(kPrefix + "FallbackChangesPerMinute.Vp8"));
}

TEST_F(ForcedFallbackDisabled, EnteredLowResolutionSetIfAtMaxPixels) {
  InsertEncodedFrames(1, kFrameIntervalMs);
  EXPECT_TRUE(statistics_proxy_->GetStats().has_entered_low_resolution);
}

TEST_F(ForcedFallbackEnabled, EnteredLowResolutionNotSetIfNotLibvpx) {
  InsertEncodedFrames(1, kFrameIntervalMs);
  EXPECT_FALSE(statistics_proxy_->GetStats().has_entered_low_resolution);
}

TEST_F(ForcedFallbackEnabled, EnteredLowResolutionSetIfLibvpx) {
  codec_name_ = "libvpx";
  InsertEncodedFrames(1, kFrameIntervalMs);
  EXPECT_TRUE(statistics_proxy_->GetStats().has_entered_low_resolution);
}

TEST_F(ForcedFallbackDisabled, EnteredLowResolutionNotSetIfAboveMaxPixels) {
  encoded_image_._encodedWidth = kWidth + 1;
  InsertEncodedFrames(1, kFrameIntervalMs);
  EXPECT_FALSE(statistics_proxy_->GetStats().has_entered_low_resolution);
}

TEST_F(ForcedFallbackDisabled, EnteredLowResolutionNotSetIfLibvpx) {
  codec_name_ = "libvpx";
  InsertEncodedFrames(1, kFrameIntervalMs);
  EXPECT_FALSE(statistics_proxy_->GetStats().has_entered_low_resolution);
}

TEST_F(ForcedFallbackDisabled,
       EnteredLowResolutionSetIfOnMinPixelLimitReached) {
  encoded_image_._encodedWidth = kWidth + 1;
  statistics_proxy_->OnMinPixelLimitReached();
  InsertEncodedFrames(1, kFrameIntervalMs);
  EXPECT_TRUE(statistics_proxy_->GetStats().has_entered_low_resolution);
}

TEST_F(ForcedFallbackEnabled, OneFallbackEvent) {
  // One change. Video: 20000 ms, fallback: 5000 ms (25%).
  EXPECT_FALSE(statistics_proxy_->GetStats().has_entered_low_resolution);
  InsertEncodedFrames(15, 1000);
  EXPECT_FALSE(statistics_proxy_->GetStats().has_entered_low_resolution);
  codec_name_ = "libvpx";
  InsertEncodedFrames(5, 1000);
  EXPECT_TRUE(statistics_proxy_->GetStats().has_entered_low_resolution);

  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(kPrefix + "FallbackTimeInPercent.Vp8"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents(kPrefix + "FallbackTimeInPercent.Vp8", 25));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(kPrefix + "FallbackChangesPerMinute.Vp8"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents(kPrefix + "FallbackChangesPerMinute.Vp8", 3));
}

TEST_F(ForcedFallbackEnabled, ThreeFallbackEvents) {
  codec_info_.codecSpecific.VP8.temporalIdx = kNoTemporalIdx;  // Should work.
  const int kMaxFrameDiffMs = 2000;

  // Three changes. Video: 60000 ms, fallback: 15000 ms (25%).
  InsertEncodedFrames(10, 1000);
  EXPECT_FALSE(statistics_proxy_->GetStats().has_entered_low_resolution);
  codec_name_ = "libvpx";
  InsertEncodedFrames(15, 500);
  EXPECT_TRUE(statistics_proxy_->GetStats().has_entered_low_resolution);
  codec_name_ = "notlibvpx";
  InsertEncodedFrames(20, 1000);
  InsertEncodedFrames(3, kMaxFrameDiffMs);  // Should not be included.
  InsertEncodedFrames(10, 1000);
  EXPECT_TRUE(statistics_proxy_->GetStats().has_entered_low_resolution);
  codec_name_ = "notlibvpx2";
  InsertEncodedFrames(10, 500);
  EXPECT_TRUE(statistics_proxy_->GetStats().has_entered_low_resolution);
  codec_name_ = "libvpx";
  InsertEncodedFrames(15, 500);
  EXPECT_TRUE(statistics_proxy_->GetStats().has_entered_low_resolution);

  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(kPrefix + "FallbackTimeInPercent.Vp8"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents(kPrefix + "FallbackTimeInPercent.Vp8", 25));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(kPrefix + "FallbackChangesPerMinute.Vp8"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents(kPrefix + "FallbackChangesPerMinute.Vp8", 3));
}

TEST_F(ForcedFallbackEnabled, NoFallbackIfAboveMaxPixels) {
  encoded_image_._encodedWidth = kWidth + 1;
  codec_name_ = "libvpx";
  InsertEncodedFrames(kMinFrames, kFrameIntervalMs);

  EXPECT_FALSE(statistics_proxy_->GetStats().has_entered_low_resolution);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples(kPrefix + "FallbackTimeInPercent.Vp8"));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples(kPrefix + "FallbackChangesPerMinute.Vp8"));
}

TEST_F(ForcedFallbackEnabled, FallbackIfAtMaxPixels) {
  encoded_image_._encodedWidth = kWidth;
  codec_name_ = "libvpx";
  InsertEncodedFrames(kMinFrames, kFrameIntervalMs);

  EXPECT_TRUE(statistics_proxy_->GetStats().has_entered_low_resolution);
  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(kPrefix + "FallbackTimeInPercent.Vp8"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(kPrefix + "FallbackChangesPerMinute.Vp8"));
}

TEST_F(SendStatisticsProxyTest, PsnrHistogramsRealtimeVideoSingleStream) {
  // Reconfigure to single stream to test "WebRTC.Video.Psnr.Y" (no spatial
  // index).
  VideoSendStream::Config config(nullptr);
  config.rtp.ssrcs.push_back(kFirstSsrc);
  statistics_proxy_.reset(new SendStatisticsProxy(
      env_, config, VideoEncoderConfig::ContentType::kRealtimeVideo));

  for (int i = 0; i < SendStatisticsProxy::kMinRequiredPsnrSamples; ++i) {
    EncodedImage encoded_image;
    encoded_image.SetRtpTimestamp(i);
    encoded_image.set_psnr(EncodedImage::Psnr{.y = 40, .u = 41, .v = 42});
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  }

  // Verify internal stats first
  EXPECT_EQ(5u, statistics_proxy_->GetStats().frames_encoded);
  // Check PSNR measurements if possible, usually exposed in substreams
  uint32_t ssrc = config.rtp.ssrcs[0];
  EXPECT_EQ(5u,
            statistics_proxy_->GetStats().substreams[ssrc].psnr_measurements);

  statistics_proxy_.reset();

  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Psnr.Y"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.Psnr.Y", 400));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Psnr.U"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.Psnr.U", 410));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Psnr.V"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.Psnr.V", 420));
}

TEST_F(SendStatisticsProxyTest, PsnrHistogramsRealtimeVideoSimulcast) {
  // Use more samples than min required to be safe.
  const int kNumSamples = SendStatisticsProxy::kMinRequiredPsnrSamples + 5;
  for (int i = 0; i < kNumSamples; ++i) {
    EncodedImage encoded_image;
    encoded_image.SetRtpTimestamp(i * 3000);
    encoded_image.SetSimulcastIndex(0);
    encoded_image.set_psnr(EncodedImage::Psnr{.y = 40, .u = 41, .v = 42});
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);

    EncodedImage encoded_image_s1;
    encoded_image_s1.SetRtpTimestamp(i * 3000);
    encoded_image_s1.SetSimulcastIndex(1);
    encoded_image_s1.set_psnr(EncodedImage::Psnr{.y = 50, .u = 51, .v = 52});
    statistics_proxy_->OnSendEncodedImage(encoded_image_s1, nullptr);
    fake_clock_.AdvanceTimeMilliseconds(33);
  }

  // Verify internal stats
  EXPECT_EQ(2u * kNumSamples, statistics_proxy_->GetStats().frames_encoded);
  uint32_t ssrc0 = config_.rtp.ssrcs[0];
  uint32_t ssrc1 = config_.rtp.ssrcs[1];
  EXPECT_EQ(static_cast<uint32_t>(kNumSamples),
            statistics_proxy_->GetStats().substreams[ssrc0].psnr_measurements);
  EXPECT_EQ(static_cast<uint32_t>(kNumSamples),
            statistics_proxy_->GetStats().substreams[ssrc1].psnr_measurements);

  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Psnr.S0.Y"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.Psnr.S0.Y", 400));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Psnr.S0.U"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.Psnr.S0.U", 410));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Psnr.S0.V"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.Psnr.S0.V", 420));

  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Psnr.S1.Y"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.Psnr.S1.Y", 500));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Psnr.S1.U"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.Psnr.S1.U", 510));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Psnr.S1.V"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.Psnr.S1.V", 520));
}

TEST_F(SendStatisticsProxyTest, PsnrHistogramsScreenshare) {
  // Reconfigure to single stream screenshare.
  VideoSendStream::Config config(nullptr);
  config.rtp.ssrcs.push_back(kFirstSsrc);
  statistics_proxy_.reset(new SendStatisticsProxy(
      env_, config, VideoEncoderConfig::ContentType::kScreen));

  const int kNumSamples = SendStatisticsProxy::kMinRequiredPsnrSamples + 5;
  for (int i = 0; i < kNumSamples; ++i) {
    EncodedImage encoded_image;
    encoded_image.SetRtpTimestamp(i * 3000);
    encoded_image.set_psnr(EncodedImage::Psnr{.y = 40, .u = 41, .v = 42});
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
    fake_clock_.AdvanceTimeMilliseconds(33);
  }

  // Verify internal stats
  EXPECT_EQ(static_cast<uint32_t>(kNumSamples),
            statistics_proxy_->GetStats().frames_encoded);
  uint32_t ssrc = config.rtp.ssrcs[0];
  EXPECT_EQ(static_cast<uint32_t>(kNumSamples),
            statistics_proxy_->GetStats().substreams[ssrc].psnr_measurements);

  statistics_proxy_.reset();
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Screenshare.Psnr.Y"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.Screenshare.Psnr.Y", 400));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Screenshare.Psnr.U"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.Screenshare.Psnr.U", 410));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Screenshare.Psnr.V"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents("WebRTC.Video.Screenshare.Psnr.V", 420));
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationReasonIsBandwidthWhenFramesDroppedByEncoder) {
  VideoCodec codec;
  codec.simulcastStream[0].active = true;
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 100);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);

  EncodedImage encoded_image;
  uint32_t rtp_timestamp = 0;
  auto encode_frame = [&] {
    rtp_timestamp += 3000;
    encoded_image.SetRtpTimestamp(rtp_timestamp);
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  };

  EXPECT_EQ(QualityLimitationReason::kNone,
            statistics_proxy_->GetStats().quality_limitation_reason);

  // Encode 80 frames and drop 18 frames in the first bucket, then advance one
  // bucket interval (100 ms) so RateTracker reports non-zero rates.
  for (int i = 0; i < 80; ++i) {
    encode_frame();
  }
  for (int i = 0; i < 18; ++i) {
    statistics_proxy_->OnFrameDropped(
        VideoStreamEncoderObserver::DropReason::kEncoder);
  }
  fake_clock_.AdvanceTimeMilliseconds(100);

  // 19 dropped / (80 encoded + 19 dropped) = 19 / 99 (~19.2%) < 20%.
  statistics_proxy_->OnFrameDropped(
      VideoStreamEncoderObserver::DropReason::kEncoder);
  EXPECT_EQ(QualityLimitationReason::kNone,
            statistics_proxy_->GetStats().quality_limitation_reason);

  // 20 dropped / (80 encoded + 20 dropped) = 20 / 100 (20.0%) >= 20%.
  statistics_proxy_->OnFrameDropped(
      VideoStreamEncoderObserver::DropReason::kEncoder);
  EXPECT_EQ(QualityLimitationReason::kBandwidth,
            statistics_proxy_->GetStats().quality_limitation_reason);

  // 20 dropped / (81 encoded + 20 dropped) = 20 / 101 (~19.8%) < 20%.
  encode_frame();
  EXPECT_EQ(QualityLimitationReason::kNone,
            statistics_proxy_->GetStats().quality_limitation_reason);
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationReasonIsBandwidthWhenFramesDroppedByMediaOptimization) {
  VideoCodec codec;
  codec.simulcastStream[0].active = true;
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 100);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);

  EncodedImage encoded_image;
  uint32_t rtp_timestamp = 0;
  auto encode_frame = [&] {
    rtp_timestamp += 3000;
    encoded_image.SetRtpTimestamp(rtp_timestamp);
    statistics_proxy_->OnSendEncodedImage(encoded_image, nullptr);
  };

  EXPECT_EQ(QualityLimitationReason::kNone,
            statistics_proxy_->GetStats().quality_limitation_reason);

  // Encode 80 frames and drop 18 frames in the first bucket, then advance one
  // bucket interval (100 ms) so RateTracker reports non-zero rates.
  for (int i = 0; i < 80; ++i) {
    encode_frame();
  }
  for (int i = 0; i < 18; ++i) {
    statistics_proxy_->OnFrameDropped(
        VideoStreamEncoderObserver::DropReason::kMediaOptimization);
  }
  fake_clock_.AdvanceTimeMilliseconds(100);

  // 19 dropped / (80 encoded + 19 dropped) = 19 / 99 (~19.2%) < 20%.
  statistics_proxy_->OnFrameDropped(
      VideoStreamEncoderObserver::DropReason::kMediaOptimization);
  EXPECT_EQ(QualityLimitationReason::kNone,
            statistics_proxy_->GetStats().quality_limitation_reason);

  // 20 dropped / (80 encoded + 20 dropped) = 20 / 100 (20.0%) >= 20%.
  statistics_proxy_->OnFrameDropped(
      VideoStreamEncoderObserver::DropReason::kMediaOptimization);
  EXPECT_EQ(QualityLimitationReason::kBandwidth,
            statistics_proxy_->GetStats().quality_limitation_reason);

  // 20 dropped / (81 encoded + 20 dropped) = 20 / 101 (~19.8%) < 20%.
  encode_frame();
  EXPECT_EQ(QualityLimitationReason::kNone,
            statistics_proxy_->GetStats().quality_limitation_reason);
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationReasonIsNoneWhenFramesDroppedForOtherReasons) {
  VideoCodec codec;
  codec.simulcastStream[0].active = true;
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 100);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);

  for (auto reason :
       {VideoStreamEncoderObserver::DropReason::kSource,
        VideoStreamEncoderObserver::DropReason::kBadTimestamp,
        VideoStreamEncoderObserver::DropReason::kEncoderQueue,
        VideoStreamEncoderObserver::DropReason::kCongestionWindow}) {
    statistics_proxy_->OnFrameDropped(reason);
    fake_clock_.AdvanceTimeMilliseconds(100);
    statistics_proxy_->OnFrameDropped(reason);
    EXPECT_EQ(QualityLimitationReason::kNone,
              statistics_proxy_->GetStats().quality_limitation_reason);
  }
}

TEST_F(SendStatisticsProxyTest,
       QualityLimitationReasonWhenFramesDroppedInSimulcast) {
  VideoCodec codec;
  codec.numberOfSimulcastStreams = 3;
  codec.simulcastStream[0].active = true;
  codec.simulcastStream[1].active = true;
  codec.simulcastStream[2].active = true;
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 100);
  allocation.SetBitrate(1, 0, 200);
  allocation.SetBitrate(2, 0, 400);
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);

  // Drop frames well above the 20% threshold for both bandwidth-related drop
  // reasons while 3 simulcast streams are active.
  for (auto reason :
       {VideoStreamEncoderObserver::DropReason::kEncoder,
        VideoStreamEncoderObserver::DropReason::kMediaOptimization}) {
    statistics_proxy_->OnFrameDropped(reason);
    fake_clock_.AdvanceTimeMilliseconds(100);
    statistics_proxy_->OnFrameDropped(reason);
    EXPECT_EQ(QualityLimitationReason::kNone,
              statistics_proxy_->GetStats().quality_limitation_reason);
  }

  // With 3 simulcast streams where only 1 is active, the frame drop rate
  // triggers kBandwidth.
  codec.simulcastStream[1].active = false;
  codec.simulcastStream[2].active = false;
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(QualityLimitationReason::kBandwidth,
            statistics_proxy_->GetStats().quality_limitation_reason);

  // Disabling the last active simulcast stream lifts the bandwidth limitation.
  codec.simulcastStream[0].active = false;
  statistics_proxy_->OnBitrateAllocationUpdated(codec, allocation);
  EXPECT_EQ(QualityLimitationReason::kNone,
            statistics_proxy_->GetStats().quality_limitation_reason);
}

}  // namespace webrtc
