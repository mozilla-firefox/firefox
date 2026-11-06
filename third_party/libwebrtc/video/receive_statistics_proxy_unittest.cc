/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/receive_statistics_proxy.h"

#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "absl/types/optional.h"
#include "api/scoped_refptr.h"
#include "api/units/frequency.h"
#include "api/units/time_delta.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"
#include "rtc_base/thread.h"
#include "system_wrappers/include/metrics.h"
#include "test/gtest.h"
#include "test/scoped_key_value_config.h"
#include "test/time_controller/simulated_time_controller.h"
#include "video/video_receive_stream2.h"

namespace webrtc {
namespace internal {
namespace {
const TimeDelta kFreqOffsetProcessInterval = TimeDelta::Seconds(40);
const uint32_t kRemoteSsrc = 456;
const int kMinRequiredSamples = 200;
const int kWidth = 1280;
const int kHeight = 720;
}  // namespace

// TODO(sakal): ReceiveStatisticsProxy is lacking unittesting.
class ReceiveStatisticsProxyTest : public ::testing::Test {
 public:
  ReceiveStatisticsProxyTest() : time_controller_(Timestamp::Millis(1234)) {
    metrics::Reset();
    statistics_proxy_ = std::make_unique<ReceiveStatisticsProxy>(
        kRemoteSsrc, time_controller_.GetClock(),
        time_controller_.GetMainThread());
  }

  ~ReceiveStatisticsProxyTest() override { statistics_proxy_.reset(); }

 protected:
  // Convenience method to avoid too many explict flushes.
  VideoReceiveStreamInterface::Stats FlushAndGetStats() {
    time_controller_.AdvanceTime(TimeDelta::Zero());
    return statistics_proxy_->GetStats();
  }

  void FlushAndUpdateHistograms(absl::optional<int> fraction_lost,
                                const StreamDataCounters& rtp_stats,
                                const StreamDataCounters* rtx_stats) {
    time_controller_.AdvanceTime(TimeDelta::Zero());
    statistics_proxy_->UpdateHistograms(fraction_lost, rtp_stats, rtx_stats);
  }

  VideoFrame CreateFrame(int width, int height) {
    return CreateVideoFrame(width, height, 0);
  }

  VideoFrame CreateFrameWithRenderTime(Timestamp render_time) {
    return CreateFrameWithRenderTimeMs(render_time.ms());
  }

  VideoFrame CreateFrameWithRenderTimeMs(int64_t render_time_ms) {
    return CreateVideoFrame(kWidth, kHeight, render_time_ms);
  }

  VideoFrame CreateVideoFrame(int width, int height, int64_t render_time_ms) {
    VideoFrame frame =
        VideoFrame::Builder()
            .set_video_frame_buffer(I420Buffer::Create(width, height))
            .set_rtp_timestamp(0)
            .set_timestamp_ms(render_time_ms)
            .set_rotation(kVideoRotation_0)
            .build();
    frame.set_ntp_time_ms(
        time_controller_.GetClock()->CurrentNtpInMilliseconds());
    return frame;
  }

  // Return the current fake time as a Timestamp.
  Timestamp Now() { return time_controller_.GetClock()->CurrentTime(); }

  // Creates a VideoFrameMetaData instance with a timestamp.
  VideoFrameMetaData MetaData(const VideoFrame& frame, Timestamp ts) {
    return VideoFrameMetaData(frame, ts);
  }

  // Creates a VideoFrameMetaData instance with the current fake time.
  VideoFrameMetaData MetaData(const VideoFrame& frame) {
    return VideoFrameMetaData(frame, Now());
  }

  test::ScopedKeyValueConfig field_trials_;
  GlobalSimulatedTimeController time_controller_;
  std::unique_ptr<ReceiveStatisticsProxy> statistics_proxy_;
};

TEST_F(ReceiveStatisticsProxyTest, OnDecodedFrameIncreasesFramesDecoded) {
  EXPECT_EQ(0u, statistics_proxy_->GetStats().frames_decoded);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  for (uint32_t i = 1; i <= 3; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      VideoContentType::UNSPECIFIED,
                                      VideoFrameType::kVideoFrameKey);
    EXPECT_EQ(i, FlushAndGetStats().frames_decoded);
  }
}

TEST_F(ReceiveStatisticsProxyTest, DecodedFpsIsReported) {
  const Frequency kFps = Frequency::Hertz(20);
  const int kRequiredSamples =
      TimeDelta::Seconds(metrics::kMinRunTimeInSeconds) * kFps;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  for (int i = 0; i < kRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      VideoContentType::UNSPECIFIED,
                                      VideoFrameType::kVideoFrameKey);
    time_controller_.AdvanceTime(1 / kFps);
  }
  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.DecodedFramesPerSecond"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.DecodedFramesPerSecond",
                                         kFps.hertz()));
}

TEST_F(ReceiveStatisticsProxyTest, DecodedFpsIsNotReportedForTooFewSamples) {
  const Frequency kFps = Frequency::Hertz(20);
  const int kRequiredSamples =
      TimeDelta::Seconds(metrics::kMinRunTimeInSeconds) * kFps;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  for (int i = 0; i < kRequiredSamples - 1; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      VideoContentType::UNSPECIFIED,
                                      VideoFrameType::kVideoFrameKey);
    time_controller_.AdvanceTime(1 / kFps);
  }
  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples("WebRTC.Video.DecodedFramesPerSecond"));
}

TEST_F(ReceiveStatisticsProxyTest,
       OnDecodedFrameWithQpDoesNotResetFramesDecodedOrTotalDecodeTime) {
  EXPECT_EQ(0u, statistics_proxy_->GetStats().frames_decoded);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  TimeDelta expected_total_decode_time = TimeDelta::Zero();
  unsigned int expected_frames_decoded = 0;
  for (uint32_t i = 1; i <= 3; ++i) {
    statistics_proxy_->OnDecodedFrame(
        frame, absl::nullopt, TimeDelta::Millis(1),
        VideoContentType::UNSPECIFIED, VideoFrameType::kVideoFrameKey);
    expected_total_decode_time += TimeDelta::Millis(1);
    ++expected_frames_decoded;
    time_controller_.AdvanceTime(TimeDelta::Zero());
    EXPECT_EQ(expected_frames_decoded,
              statistics_proxy_->GetStats().frames_decoded);
    EXPECT_EQ(expected_total_decode_time,
              statistics_proxy_->GetStats().total_decode_time);
  }
  statistics_proxy_->OnDecodedFrame(frame, 1u, TimeDelta::Millis(3),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);
  ++expected_frames_decoded;
  expected_total_decode_time += TimeDelta::Millis(3);
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(expected_frames_decoded,
            statistics_proxy_->GetStats().frames_decoded);
  EXPECT_EQ(expected_total_decode_time,
            statistics_proxy_->GetStats().total_decode_time);
}

TEST_F(ReceiveStatisticsProxyTest, OnDecodedFrameIncreasesProcessingDelay) {
  const TimeDelta kProcessingDelay = TimeDelta::Millis(10);
  EXPECT_EQ(0u, statistics_proxy_->GetStats().frames_decoded);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  TimeDelta expected_total_processing_delay = TimeDelta::Zero();
  unsigned int expected_frames_decoded = 0;
  // We set receive time fixed and increase the clock by 10ms
  // in the loop which will increase the processing delay by
  // 10/20/30ms respectively.
  RtpPacketInfos::vector_type packet_infos = {RtpPacketInfo(
      /*ssrc=*/{}, /*csrcs=*/{}, /*rtp_timestamp=*/{}, /*receive_time=*/Now())};
  frame.set_packet_infos(RtpPacketInfos(packet_infos));
  for (int i = 1; i <= 3; ++i) {
    time_controller_.AdvanceTime(kProcessingDelay);
    statistics_proxy_->OnDecodedFrame(
        frame, absl::nullopt, TimeDelta::Millis(1),
        VideoContentType::UNSPECIFIED, VideoFrameType::kVideoFrameKey);
    expected_total_processing_delay += i * kProcessingDelay;
    ++expected_frames_decoded;
    time_controller_.AdvanceTime(TimeDelta::Zero());
    EXPECT_EQ(expected_frames_decoded,
              statistics_proxy_->GetStats().frames_decoded);
    EXPECT_EQ(expected_total_processing_delay,
              statistics_proxy_->GetStats().total_processing_delay);
  }
  time_controller_.AdvanceTime(kProcessingDelay);
  statistics_proxy_->OnDecodedFrame(frame, 1u, TimeDelta::Millis(3),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);
  ++expected_frames_decoded;
  expected_total_processing_delay += 4 * kProcessingDelay;
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(expected_frames_decoded,
            statistics_proxy_->GetStats().frames_decoded);
  EXPECT_EQ(expected_total_processing_delay,
            statistics_proxy_->GetStats().total_processing_delay);
}

TEST_F(ReceiveStatisticsProxyTest, OnDecodedFrameIncreasesAssemblyTime) {
  const TimeDelta kAssemblyTime = TimeDelta::Millis(7);
  EXPECT_EQ(0u, statistics_proxy_->GetStats().frames_decoded);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  TimeDelta expected_total_assembly_time = TimeDelta::Zero();
  unsigned int expected_frames_decoded = 0;
  unsigned int expected_frames_assembled_from_multiple_packets = 0;

  // A single-packet frame will not increase total assembly time
  // and frames assembled.
  RtpPacketInfos::vector_type single_packet_frame = {RtpPacketInfo(
      /*ssrc=*/{}, /*csrcs=*/{}, /*rtp_timestamp=*/{}, /*receive_time=*/Now())};
  frame.set_packet_infos(RtpPacketInfos(single_packet_frame));
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Millis(1),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);
  ++expected_frames_decoded;
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(expected_total_assembly_time,
            statistics_proxy_->GetStats().total_assembly_time);
  EXPECT_EQ(
      expected_frames_assembled_from_multiple_packets,
      statistics_proxy_->GetStats().frames_assembled_from_multiple_packets);

  // In an ordered frame the first and last packet matter.
  RtpPacketInfos::vector_type ordered_frame = {
      RtpPacketInfo(/*ssrc=*/{}, /*csrcs=*/{}, /*rtp_timestamp=*/{},
                    /*receive_time=*/Now()),
      RtpPacketInfo(/*ssrc=*/{}, /*csrcs=*/{}, /*rtp_timestamp=*/{},
                    /*receive_time=*/Now() + kAssemblyTime),
      RtpPacketInfo(/*ssrc=*/{}, /*csrcs=*/{}, /*rtp_timestamp=*/{},
                    /*receive_time=*/Now() + 2 * kAssemblyTime),
  };
  frame.set_packet_infos(RtpPacketInfos(ordered_frame));
  statistics_proxy_->OnDecodedFrame(frame, 1u, TimeDelta::Millis(3),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);
  ++expected_frames_decoded;
  ++expected_frames_assembled_from_multiple_packets;
  expected_total_assembly_time += 2 * kAssemblyTime;
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(expected_frames_decoded,
            statistics_proxy_->GetStats().frames_decoded);
  EXPECT_EQ(expected_total_assembly_time,
            statistics_proxy_->GetStats().total_assembly_time);
  EXPECT_EQ(
      expected_frames_assembled_from_multiple_packets,
      statistics_proxy_->GetStats().frames_assembled_from_multiple_packets);

  // "First" and "last" are in receive time, not sequence number.
  RtpPacketInfos::vector_type unordered_frame = {
      RtpPacketInfo(/*ssrc=*/{}, /*csrcs=*/{}, /*rtp_timestamp=*/{},
                    /*receive_time=*/Now() + 2 * kAssemblyTime),
      RtpPacketInfo(/*ssrc=*/{}, /*csrcs=*/{}, /*rtp_timestamp=*/{},
                    /*receive_time=*/Now()),
      RtpPacketInfo(/*ssrc=*/{}, /*csrcs=*/{}, /*rtp_timestamp=*/{},
                    /*receive_time=*/Now() + kAssemblyTime),
  };
  frame.set_packet_infos(RtpPacketInfos(unordered_frame));
  statistics_proxy_->OnDecodedFrame(frame, 1u, TimeDelta::Millis(3),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);
  ++expected_frames_decoded;
  ++expected_frames_assembled_from_multiple_packets;
  expected_total_assembly_time += 2 * kAssemblyTime;
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(expected_frames_decoded,
            statistics_proxy_->GetStats().frames_decoded);
  EXPECT_EQ(expected_total_assembly_time,
            statistics_proxy_->GetStats().total_assembly_time);
  EXPECT_EQ(
      expected_frames_assembled_from_multiple_packets,
      statistics_proxy_->GetStats().frames_assembled_from_multiple_packets);
}

TEST_F(ReceiveStatisticsProxyTest, OnDecodedFrameIncreasesQpSum) {
  EXPECT_EQ(absl::nullopt, statistics_proxy_->GetStats().qp_sum);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnDecodedFrame(frame, 3u, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(3u, FlushAndGetStats().qp_sum);
  statistics_proxy_->OnDecodedFrame(frame, 127u, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameDelta);
  EXPECT_EQ(130u, FlushAndGetStats().qp_sum);
}

TEST_F(ReceiveStatisticsProxyTest, OnDecodedFrameIncreasesTotalDecodeTime) {
  EXPECT_EQ(absl::nullopt, statistics_proxy_->GetStats().qp_sum);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnDecodedFrame(frame, 3u, TimeDelta::Millis(4),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(4u, FlushAndGetStats().total_decode_time.ms());
  statistics_proxy_->OnDecodedFrame(frame, 127u, TimeDelta::Millis(7),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameDelta);
  EXPECT_EQ(11u, FlushAndGetStats().total_decode_time.ms());
}

TEST_F(ReceiveStatisticsProxyTest, ReportsContentType) {
  const std::string kRealtimeString("realtime");
  const std::string kScreenshareString("screen");
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  EXPECT_EQ(kRealtimeString, videocontenttypehelpers::ToString(
                                 statistics_proxy_->GetStats().content_type));
  statistics_proxy_->OnDecodedFrame(frame, 3u, TimeDelta::Zero(),
                                    VideoContentType::SCREENSHARE,
                                    VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(kScreenshareString,
            videocontenttypehelpers::ToString(FlushAndGetStats().content_type));
  statistics_proxy_->OnDecodedFrame(frame, 3u, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameDelta);
  EXPECT_EQ(kRealtimeString,
            videocontenttypehelpers::ToString(FlushAndGetStats().content_type));
}

TEST_F(ReceiveStatisticsProxyTest, ReportsMaxInterframeDelay) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  const TimeDelta kInterframeDelay1 = TimeDelta::Millis(100);
  const TimeDelta kInterframeDelay2 = TimeDelta::Millis(200);
  const TimeDelta kInterframeDelay3 = TimeDelta::Millis(100);
  EXPECT_EQ(-1, statistics_proxy_->GetStats().interframe_delay_max_ms);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(-1, FlushAndGetStats().interframe_delay_max_ms);

  time_controller_.AdvanceTime(kInterframeDelay1);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameDelta);
  EXPECT_EQ(kInterframeDelay1.ms(), FlushAndGetStats().interframe_delay_max_ms);

  time_controller_.AdvanceTime(kInterframeDelay2);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameDelta);
  EXPECT_EQ(kInterframeDelay2.ms(), FlushAndGetStats().interframe_delay_max_ms);

  time_controller_.AdvanceTime(kInterframeDelay3);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameDelta);
  // kInterframeDelay3 is smaller than kInterframeDelay2.
  EXPECT_EQ(kInterframeDelay2.ms(), FlushAndGetStats().interframe_delay_max_ms);
}

TEST_F(ReceiveStatisticsProxyTest, ReportInterframeDelayInWindow) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  const TimeDelta kInterframeDelay1 = TimeDelta::Millis(900);
  const TimeDelta kInterframeDelay2 = TimeDelta::Millis(750);
  const TimeDelta kInterframeDelay3 = TimeDelta::Millis(700);
  EXPECT_EQ(-1, statistics_proxy_->GetStats().interframe_delay_max_ms);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(-1, FlushAndGetStats().interframe_delay_max_ms);

  time_controller_.AdvanceTime(kInterframeDelay1);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameDelta);
  EXPECT_EQ(kInterframeDelay1.ms(), FlushAndGetStats().interframe_delay_max_ms);

  time_controller_.AdvanceTime(kInterframeDelay2);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameDelta);
  // Still first delay is the maximum
  EXPECT_EQ(kInterframeDelay1.ms(), FlushAndGetStats().interframe_delay_max_ms);

  time_controller_.AdvanceTime(kInterframeDelay3);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameDelta);
  // Now the first sample is out of the window, so the second is the maximum.
  EXPECT_EQ(kInterframeDelay2.ms(), FlushAndGetStats().interframe_delay_max_ms);
}

TEST_F(ReceiveStatisticsProxyTest, ReportsFreezeMetrics) {
  const TimeDelta kFreezeDuration = TimeDelta::Seconds(1);

  VideoReceiveStreamInterface::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(0u, stats.freeze_count);
  EXPECT_FALSE(stats.total_freezes_duration_ms);

  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  for (size_t i = 0; i < VideoQualityObserver::kMinFrameSamplesToDetectFreeze;
       ++i) {
    time_controller_.AdvanceTime(TimeDelta::Millis(30));
    statistics_proxy_->OnRenderedFrame(MetaData(frame));
  }

  // Freeze.
  time_controller_.AdvanceTime(kFreezeDuration);
  statistics_proxy_->OnRenderedFrame(MetaData(frame));

  stats = statistics_proxy_->GetStats();
  EXPECT_EQ(1u, stats.freeze_count);
  EXPECT_EQ(kFreezeDuration.ms(), stats.total_freezes_duration_ms);
}

TEST_F(ReceiveStatisticsProxyTest, ReportsPauseMetrics) {
  VideoReceiveStreamInterface::Stats stats = statistics_proxy_->GetStats();
  ASSERT_EQ(0u, stats.pause_count);
  ASSERT_EQ(0u, stats.total_pauses_duration_ms);

  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnRenderedFrame(MetaData(frame));

  // Pause.
  time_controller_.AdvanceTime(TimeDelta::Millis(5432));
  statistics_proxy_->OnStreamInactive();
  statistics_proxy_->OnRenderedFrame(MetaData(frame));

  stats = statistics_proxy_->GetStats();
  EXPECT_EQ(1u, stats.pause_count);
  EXPECT_EQ(5432u, stats.total_pauses_duration_ms);
}

TEST_F(ReceiveStatisticsProxyTest, PauseBeforeFirstAndAfterLastFrameIgnored) {
  VideoReceiveStreamInterface::Stats stats = statistics_proxy_->GetStats();
  ASSERT_EQ(0u, stats.pause_count);
  ASSERT_EQ(0u, stats.total_pauses_duration_ms);

  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  // Pause -> Frame -> Pause
  time_controller_.AdvanceTime(TimeDelta::Seconds(5));
  statistics_proxy_->OnStreamInactive();
  statistics_proxy_->OnRenderedFrame(MetaData(frame));

  time_controller_.AdvanceTime(TimeDelta::Millis(30));
  statistics_proxy_->OnRenderedFrame(MetaData(frame));

  time_controller_.AdvanceTime(TimeDelta::Seconds(5));
  statistics_proxy_->OnStreamInactive();

  stats = statistics_proxy_->GetStats();
  EXPECT_EQ(0u, stats.pause_count);
  EXPECT_EQ(0u, stats.total_pauses_duration_ms);
}

TEST_F(ReceiveStatisticsProxyTest, ReportsTotalInterFrameDelay) {
  VideoReceiveStreamInterface::Stats stats = statistics_proxy_->GetStats();
  ASSERT_EQ(0.0, stats.total_inter_frame_delay);

  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  // Emulate delay before first frame is rendered. This is needed to ensure
  // that frame duration only covers time since first frame is rendered and
  // not the total time.
  time_controller_.AdvanceTime(TimeDelta::Millis(5432));
  for (int i = 0; i <= 10; ++i) {
    time_controller_.AdvanceTime(TimeDelta::Millis(30));
    statistics_proxy_->OnRenderedFrame(MetaData(frame));
  }

  stats = statistics_proxy_->GetStats();
  EXPECT_EQ(10 * 30 / 1000.0, stats.total_inter_frame_delay);
}

TEST_F(ReceiveStatisticsProxyTest, ReportsTotalSquaredInterFrameDelay) {
  VideoReceiveStreamInterface::Stats stats = statistics_proxy_->GetStats();
  ASSERT_EQ(0.0, stats.total_squared_inter_frame_delay);

  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  for (int i = 0; i <= 10; ++i) {
    time_controller_.AdvanceTime(TimeDelta::Millis(30));
    statistics_proxy_->OnRenderedFrame(MetaData(frame));
  }

  stats = statistics_proxy_->GetStats();
  const double kExpectedTotalSquaredInterFrameDelaySecs =
      10 * (30 / 1000.0 * 30 / 1000.0);
  EXPECT_EQ(kExpectedTotalSquaredInterFrameDelaySecs,
            stats.total_squared_inter_frame_delay);
}

TEST_F(ReceiveStatisticsProxyTest, OnDecodedFrameWithoutQpQpSumWontExist) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  EXPECT_EQ(absl::nullopt, statistics_proxy_->GetStats().qp_sum);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(absl::nullopt, FlushAndGetStats().qp_sum);
}

TEST_F(ReceiveStatisticsProxyTest, OnDecodedFrameWithoutQpResetsQpSum) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  EXPECT_EQ(absl::nullopt, statistics_proxy_->GetStats().qp_sum);
  statistics_proxy_->OnDecodedFrame(frame, 3u, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(3u, FlushAndGetStats().qp_sum);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameDelta);
  EXPECT_EQ(absl::nullopt, FlushAndGetStats().qp_sum);
}

TEST_F(ReceiveStatisticsProxyTest, OnRenderedFrameIncreasesFramesRendered) {
  EXPECT_EQ(0u, statistics_proxy_->GetStats().frames_rendered);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  for (uint32_t i = 1; i <= 3; ++i) {
    statistics_proxy_->OnRenderedFrame(MetaData(frame));
    EXPECT_EQ(i, statistics_proxy_->GetStats().frames_rendered);
  }
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsSsrc) {
  EXPECT_EQ(kRemoteSsrc, statistics_proxy_->GetStats().ssrc);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsIncomingPayloadType) {
  const int kPayloadType = 111;
  statistics_proxy_->OnIncomingPayloadType(kPayloadType);
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(kPayloadType, statistics_proxy_->GetStats().current_payload_type);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsDecoderInfo) {
  auto init_stats = statistics_proxy_->GetStats();
  EXPECT_EQ(init_stats.decoder_implementation_name, absl::nullopt);
  EXPECT_EQ(init_stats.power_efficient_decoder, absl::nullopt);

  const VideoDecoder::DecoderInfo decoder_info{
      .implementation_name = "decoderName", .is_hardware_accelerated = true};
  statistics_proxy_->OnDecoderInfo(decoder_info);
  time_controller_.AdvanceTime(TimeDelta::Zero());
  auto stats = statistics_proxy_->GetStats();
  EXPECT_EQ(decoder_info.implementation_name,
            stats.decoder_implementation_name);
  EXPECT_TRUE(stats.power_efficient_decoder);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsOnCompleteFrame) {
  const int kFrameSizeBytes = 1000;
  statistics_proxy_->OnCompleteFrame(true, kFrameSizeBytes,
                                     VideoContentType::UNSPECIFIED);
  VideoReceiveStreamInterface::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(1, stats.network_frame_rate);
  EXPECT_EQ(0, stats.frame_counts.key_frames);
  EXPECT_EQ(0, stats.frame_counts.delta_frames);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsOnDroppedFrame) {
  unsigned int dropped_frames = 0;
  for (int i = 0; i < 10; ++i) {
    statistics_proxy_->OnDroppedFrames(i);
    dropped_frames += i;
  }
  VideoReceiveStreamInterface::Stats stats = FlushAndGetStats();
  EXPECT_EQ(dropped_frames, stats.frames_dropped);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsDecodeTimingStats) {
  const int kMaxDecodeMs = 2;
  const int kCurrentDelayMs = 3;
  const TimeDelta kTargetDelay = TimeDelta::Millis(4);
  const int kJitterDelayMs = 5;
  const int kMinPlayoutDelayMs = 6;
  const int kRenderDelayMs = 7;
  const int64_t kRttMs = 8;
  const TimeDelta kJitterBufferDelay = TimeDelta::Millis(9);
  const TimeDelta kMinimumDelay = TimeDelta::Millis(1);
  statistics_proxy_->OnRttUpdate(kRttMs);
  statistics_proxy_->OnFrameBufferTimingsUpdated(
      kMaxDecodeMs, kCurrentDelayMs, kTargetDelay.ms(), kJitterDelayMs,
      kMinPlayoutDelayMs, kRenderDelayMs);
  statistics_proxy_->OnDecodableFrame(kJitterBufferDelay, kTargetDelay,
                                      kMinimumDelay);
  VideoReceiveStreamInterface::Stats stats = FlushAndGetStats();
  EXPECT_EQ(kMaxDecodeMs, stats.max_decode_ms);
  EXPECT_EQ(kCurrentDelayMs, stats.current_delay_ms);
  EXPECT_EQ(kTargetDelay.ms(), stats.target_delay_ms);
  EXPECT_EQ(kJitterDelayMs, stats.jitter_buffer_ms);
  EXPECT_EQ(kMinPlayoutDelayMs, stats.min_playout_delay_ms);
  EXPECT_EQ(kRenderDelayMs, stats.render_delay_ms);
  EXPECT_EQ(kJitterBufferDelay, stats.jitter_buffer_delay);
  EXPECT_EQ(kTargetDelay, stats.jitter_buffer_target_delay);
  EXPECT_EQ(1u, stats.jitter_buffer_emitted_count);
  EXPECT_EQ(kMinimumDelay, stats.jitter_buffer_minimum_delay);
}

TEST_F(ReceiveStatisticsProxyTest, CumulativeDecodeGetStatsAccumulate) {
  const TimeDelta kJitterBufferDelay = TimeDelta::Millis(3);
  const TimeDelta kTargetDelay = TimeDelta::Millis(2);
  const TimeDelta kMinimumDelay = TimeDelta::Millis(1);
  statistics_proxy_->OnDecodableFrame(kJitterBufferDelay, kTargetDelay,
                                      kMinimumDelay);
  statistics_proxy_->OnDecodableFrame(kJitterBufferDelay, kTargetDelay,
                                      kMinimumDelay);
  VideoReceiveStreamInterface::Stats stats = FlushAndGetStats();
  EXPECT_EQ(2 * kJitterBufferDelay, stats.jitter_buffer_delay);
  EXPECT_EQ(2 * kTargetDelay, stats.jitter_buffer_target_delay);
  EXPECT_EQ(2u, stats.jitter_buffer_emitted_count);
  EXPECT_EQ(2 * kMinimumDelay, stats.jitter_buffer_minimum_delay);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsRtcpPacketTypeCounts) {
  const uint32_t kFirPackets = 33;
  const uint32_t kPliPackets = 44;
  const uint32_t kNackPackets = 55;
  RtcpPacketTypeCounter counter;
  counter.fir_packets = kFirPackets;
  counter.pli_packets = kPliPackets;
  counter.nack_packets = kNackPackets;
  statistics_proxy_->RtcpPacketTypesCounterUpdated(kRemoteSsrc, counter);
  VideoReceiveStreamInterface::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(kFirPackets, stats.rtcp_packet_type_counts.fir_packets);
  EXPECT_EQ(kPliPackets, stats.rtcp_packet_type_counts.pli_packets);
  EXPECT_EQ(kNackPackets, stats.rtcp_packet_type_counts.nack_packets);
}

TEST_F(ReceiveStatisticsProxyTest,
       GetStatsReportsNoRtcpPacketTypeCountsForUnknownSsrc) {
  RtcpPacketTypeCounter counter;
  counter.fir_packets = 33;
  statistics_proxy_->RtcpPacketTypesCounterUpdated(kRemoteSsrc + 1, counter);
  EXPECT_EQ(0u,
            statistics_proxy_->GetStats().rtcp_packet_type_counts.fir_packets);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsFrameCounts) {
  const int kKeyFrames = 3;
  const int kDeltaFrames = 22;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  for (int i = 0; i < kKeyFrames; i++) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      VideoContentType::UNSPECIFIED,
                                      VideoFrameType::kVideoFrameKey);
  }
  for (int i = 0; i < kDeltaFrames; i++) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      VideoContentType::UNSPECIFIED,
                                      VideoFrameType::kVideoFrameDelta);
  }

  VideoReceiveStreamInterface::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(0, stats.frame_counts.key_frames);
  EXPECT_EQ(0, stats.frame_counts.delta_frames);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsCName) {
  const char* kName = "cName";
  statistics_proxy_->OnCname(kRemoteSsrc, kName);
  EXPECT_STREQ(kName, statistics_proxy_->GetStats().c_name.c_str());
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsNoCNameForUnknownSsrc) {
  const char* kName = "cName";
  statistics_proxy_->OnCname(kRemoteSsrc + 1, kName);
  EXPECT_STREQ("", statistics_proxy_->GetStats().c_name.c_str());
}

TEST_F(ReceiveStatisticsProxyTest, ReportsLongestTimingFrameInfo) {
  const int64_t kShortEndToEndDelay = 10;
  const int64_t kMedEndToEndDelay = 20;
  const int64_t kLongEndToEndDelay = 100;
  const uint32_t kExpectedRtpTimestamp = 2;
  TimingFrameInfo info;
  absl::optional<TimingFrameInfo> result;
  info.rtp_timestamp = kExpectedRtpTimestamp - 1;
  info.capture_time_ms = 0;
  info.decode_finish_ms = kShortEndToEndDelay;
  statistics_proxy_->OnTimingFrameInfoUpdated(info);
  info.rtp_timestamp =
      kExpectedRtpTimestamp;  // this frame should be reported in the end.
  info.capture_time_ms = 0;
  info.decode_finish_ms = kLongEndToEndDelay;
  statistics_proxy_->OnTimingFrameInfoUpdated(info);
  info.rtp_timestamp = kExpectedRtpTimestamp + 1;
  info.capture_time_ms = 0;
  info.decode_finish_ms = kMedEndToEndDelay;
  statistics_proxy_->OnTimingFrameInfoUpdated(info);
  result = FlushAndGetStats().timing_frame_info;
  EXPECT_TRUE(result);
  EXPECT_EQ(kExpectedRtpTimestamp, result->rtp_timestamp);
}

TEST_F(ReceiveStatisticsProxyTest, RespectsReportingIntervalForTimingFrames) {
  TimingFrameInfo info;
  const int64_t kShortEndToEndDelay = 10;
  const uint32_t kExpectedRtpTimestamp = 2;
  const TimeDelta kShortDelay = TimeDelta::Seconds(1);
  const TimeDelta kLongDelay = TimeDelta::Seconds(10);
  absl::optional<TimingFrameInfo> result;
  info.rtp_timestamp = kExpectedRtpTimestamp;
  info.capture_time_ms = 0;
  info.decode_finish_ms = kShortEndToEndDelay;
  statistics_proxy_->OnTimingFrameInfoUpdated(info);
  time_controller_.AdvanceTime(kShortDelay);
  result = FlushAndGetStats().timing_frame_info;
  EXPECT_TRUE(result);
  EXPECT_EQ(kExpectedRtpTimestamp, result->rtp_timestamp);
  time_controller_.AdvanceTime(kLongDelay);
  result = statistics_proxy_->GetStats().timing_frame_info;
  EXPECT_FALSE(result);
}

TEST_F(ReceiveStatisticsProxyTest, LifetimeHistogramIsUpdated) {
  const TimeDelta kLifetime = TimeDelta::Seconds(3);
  time_controller_.AdvanceTime(kLifetime);
  // Need at least one decoded frame to report stream lifetime.

  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnCompleteFrame(true, 1000, VideoContentType::UNSPECIFIED);
  statistics_proxy_->OnDecodedFrame(
      frame, absl::nullopt, TimeDelta::Millis(1000),
      VideoContentType::UNSPECIFIED, VideoFrameType::kVideoFrameKey);
  FlushAndGetStats();

  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.ReceiveStreamLifetimeInSeconds"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.ReceiveStreamLifetimeInSeconds",
                            kLifetime.seconds()));
}

TEST_F(ReceiveStatisticsProxyTest,
       LifetimeHistogramNotReportedForEmptyStreams) {
  const TimeDelta kLifetime = TimeDelta::Seconds(3);
  time_controller_.AdvanceTime(kLifetime);
  // No frames received.
  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.ReceiveStreamLifetimeInSeconds"));
}

TEST_F(ReceiveStatisticsProxyTest, PacketLossHistogramIsUpdated) {
  statistics_proxy_->UpdateHistograms(10, StreamDataCounters(), nullptr);
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.ReceivedPacketsLostInPercent"));

  // Restart
  SetUp();

  // Min run time has passed.
  time_controller_.AdvanceTime(
      TimeDelta::Seconds(metrics::kMinRunTimeInSeconds));
  statistics_proxy_->UpdateHistograms(10, StreamDataCounters(), nullptr);
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.ReceivedPacketsLostInPercent"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.ReceivedPacketsLostInPercent", 10));
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsPlayoutTimestamp) {
  const int64_t kVideoNtpMs = 21;
  const int64_t kSyncOffsetMs = 22;
  const double kFreqKhz = 90.0;
  EXPECT_EQ(absl::nullopt,
            statistics_proxy_->GetStats().estimated_playout_ntp_timestamp_ms);
  statistics_proxy_->OnSyncOffsetUpdated(kVideoNtpMs, kSyncOffsetMs, kFreqKhz);
  EXPECT_EQ(kVideoNtpMs, FlushAndGetStats().estimated_playout_ntp_timestamp_ms);
  time_controller_.AdvanceTime(TimeDelta::Millis(13));
  EXPECT_EQ(kVideoNtpMs + 13,
            statistics_proxy_->GetStats().estimated_playout_ntp_timestamp_ms);
  time_controller_.AdvanceTime(TimeDelta::Millis(5));
  EXPECT_EQ(kVideoNtpMs + 13 + 5,
            statistics_proxy_->GetStats().estimated_playout_ntp_timestamp_ms);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsAvSyncOffset) {
  const int64_t kVideoNtpMs = 21;
  const int64_t kSyncOffsetMs = 22;
  const double kFreqKhz = 90.0;
  EXPECT_EQ(std::numeric_limits<int>::max(),
            statistics_proxy_->GetStats().sync_offset_ms);
  statistics_proxy_->OnSyncOffsetUpdated(kVideoNtpMs, kSyncOffsetMs, kFreqKhz);
  EXPECT_EQ(kSyncOffsetMs, FlushAndGetStats().sync_offset_ms);
}

TEST_F(ReceiveStatisticsProxyTest, AvSyncOffsetHistogramIsUpdated) {
  const int64_t kVideoNtpMs = 21;
  const int64_t kSyncOffsetMs = 22;
  const double kFreqKhz = 90.0;
  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnSyncOffsetUpdated(kVideoNtpMs, kSyncOffsetMs,
                                           kFreqKhz);
  }
  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.AVSyncOffsetInMs"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.AVSyncOffsetInMs", kSyncOffsetMs));
}

TEST_F(ReceiveStatisticsProxyTest, RtpToNtpFrequencyOffsetHistogramIsUpdated) {
  const int64_t kVideoNtpMs = 21;
  const int64_t kSyncOffsetMs = 22;
  const double kFreqKhz = 90.0;
  statistics_proxy_->OnSyncOffsetUpdated(kVideoNtpMs, kSyncOffsetMs, kFreqKhz);
  statistics_proxy_->OnSyncOffsetUpdated(kVideoNtpMs, kSyncOffsetMs,
                                         kFreqKhz + 2.2);
  time_controller_.AdvanceTime(kFreqOffsetProcessInterval);
  //) Process interval passed, max diff: 2.
  statistics_proxy_->OnSyncOffsetUpdated(kVideoNtpMs, kSyncOffsetMs,
                                         kFreqKhz + 1.1);
  statistics_proxy_->OnSyncOffsetUpdated(kVideoNtpMs, kSyncOffsetMs,
                                         kFreqKhz - 4.2);
  statistics_proxy_->OnSyncOffsetUpdated(kVideoNtpMs, kSyncOffsetMs,
                                         kFreqKhz - 0.9);
  time_controller_.AdvanceTime(kFreqOffsetProcessInterval);
  //) Process interval passed, max diff: 4.
  statistics_proxy_->OnSyncOffsetUpdated(kVideoNtpMs, kSyncOffsetMs, kFreqKhz);
  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  // Average reported: (2 + 4) / 2 = 3.
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.RtpToNtpFreqOffsetInKhz"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.RtpToNtpFreqOffsetInKhz", 3));
}

TEST_F(ReceiveStatisticsProxyTest, Vp8QpHistogramIsUpdated) {
  const int kQp = 22;

  for (int i = 0; i < kMinRequiredSamples; ++i)
    statistics_proxy_->OnPreDecode(kVideoCodecVP8, kQp);

  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.Decoded.Vp8.Qp"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.Decoded.Vp8.Qp", kQp));
}

TEST_F(ReceiveStatisticsProxyTest, Vp8QpHistogramIsNotUpdatedForTooFewSamples) {
  const int kQp = 22;

  for (int i = 0; i < kMinRequiredSamples - 1; ++i)
    statistics_proxy_->OnPreDecode(kVideoCodecVP8, kQp);

  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.Decoded.Vp8.Qp"));
}

TEST_F(ReceiveStatisticsProxyTest, Vp8QpHistogramIsNotUpdatedIfNoQpValue) {
  for (int i = 0; i < kMinRequiredSamples; ++i)
    statistics_proxy_->OnPreDecode(kVideoCodecVP8, -1);

  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.Decoded.Vp8.Qp"));
}

TEST_F(ReceiveStatisticsProxyTest,
       KeyFrameHistogramNotUpdatedForTooFewSamples) {
  const bool kIsKeyFrame = false;
  const int kFrameSizeBytes = 1000;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kMinRequiredSamples - 1; ++i) {
    statistics_proxy_->OnCompleteFrame(kIsKeyFrame, kFrameSizeBytes,
                                       VideoContentType::UNSPECIFIED);
    statistics_proxy_->OnDecodedFrame(
        frame, absl::nullopt, TimeDelta::Millis(1000),
        VideoContentType::UNSPECIFIED, VideoFrameType::kVideoFrameDelta);
  }
  FlushAndGetStats();

  EXPECT_EQ(0, statistics_proxy_->GetStats().frame_counts.key_frames);
  EXPECT_EQ(kMinRequiredSamples - 1,
            statistics_proxy_->GetStats().frame_counts.delta_frames);

  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.KeyFramesReceivedInPermille"));
}

TEST_F(ReceiveStatisticsProxyTest,
       KeyFrameHistogramUpdatedForMinRequiredSamples) {
  const bool kIsKeyFrame = false;
  const int kFrameSizeBytes = 1000;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnCompleteFrame(kIsKeyFrame, kFrameSizeBytes,
                                       VideoContentType::UNSPECIFIED);
    statistics_proxy_->OnDecodedFrame(
        frame, absl::nullopt, TimeDelta::Millis(1000),
        VideoContentType::UNSPECIFIED, VideoFrameType::kVideoFrameDelta);
  }
  FlushAndGetStats();

  EXPECT_EQ(0, statistics_proxy_->GetStats().frame_counts.key_frames);
  EXPECT_EQ(kMinRequiredSamples,
            statistics_proxy_->GetStats().frame_counts.delta_frames);

  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.KeyFramesReceivedInPermille"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.KeyFramesReceivedInPermille", 0));
}

TEST_F(ReceiveStatisticsProxyTest, KeyFrameHistogramIsUpdated) {
  const int kFrameSizeBytes = 1000;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnCompleteFrame(true, kFrameSizeBytes,
                                       VideoContentType::UNSPECIFIED);
    statistics_proxy_->OnDecodedFrame(
        frame, absl::nullopt, TimeDelta::Millis(1000),
        VideoContentType::UNSPECIFIED, VideoFrameType::kVideoFrameKey);
  }
  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnCompleteFrame(false, kFrameSizeBytes,
                                       VideoContentType::UNSPECIFIED);
    statistics_proxy_->OnDecodedFrame(
        frame, absl::nullopt, TimeDelta::Millis(1000),
        VideoContentType::UNSPECIFIED, VideoFrameType::kVideoFrameDelta);
  }
  FlushAndGetStats();

  EXPECT_EQ(kMinRequiredSamples,
            statistics_proxy_->GetStats().frame_counts.key_frames);
  EXPECT_EQ(kMinRequiredSamples,
            statistics_proxy_->GetStats().frame_counts.delta_frames);

  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.KeyFramesReceivedInPermille"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.KeyFramesReceivedInPermille", 500));
}

TEST_F(ReceiveStatisticsProxyTest, TimingHistogramsNotUpdatedForTooFewSamples) {
  const int kMaxDecodeMs = 2;
  const int kCurrentDelayMs = 3;
  const int kTargetDelayMs = 4;
  const int kJitterDelayMs = 5;
  const int kMinPlayoutDelayMs = 6;
  const int kRenderDelayMs = 7;

  for (int i = 0; i < kMinRequiredSamples - 1; ++i) {
    statistics_proxy_->OnFrameBufferTimingsUpdated(
        kMaxDecodeMs, kCurrentDelayMs, kTargetDelayMs, kJitterDelayMs,
        kMinPlayoutDelayMs, kRenderDelayMs);
  }

  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.DecodeTimeInMs"));
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples("WebRTC.Video.JitterBufferDelayInMs"));
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.TargetDelayInMs"));
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.CurrentDelayInMs"));
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.OnewayDelayInMs"));
}

TEST_F(ReceiveStatisticsProxyTest, TimingHistogramsAreUpdated) {
  const int kMaxDecodeMs = 2;
  const int kCurrentDelayMs = 3;
  const int kTargetDelayMs = 4;
  const int kJitterDelayMs = 5;
  const int kMinPlayoutDelayMs = 6;
  const int kRenderDelayMs = 7;

  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnFrameBufferTimingsUpdated(
        kMaxDecodeMs, kCurrentDelayMs, kTargetDelayMs, kJitterDelayMs,
        kMinPlayoutDelayMs, kRenderDelayMs);
  }

  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.JitterBufferDelayInMs"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.TargetDelayInMs"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.CurrentDelayInMs"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.OnewayDelayInMs"));

  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.Video.JitterBufferDelayInMs",
                                         kJitterDelayMs));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.TargetDelayInMs", kTargetDelayMs));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.CurrentDelayInMs", kCurrentDelayMs));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.OnewayDelayInMs", kTargetDelayMs));
}

TEST_F(ReceiveStatisticsProxyTest, DoesNotReportStaleFramerates) {
  const Frequency kDefaultFps = Frequency::Hertz(30);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kDefaultFps.hertz(); ++i) {
    // Since OnRenderedFrame is never called the fps in each sample will be 0,
    // i.e. bad
    frame.set_ntp_time_ms(
        time_controller_.GetClock()->CurrentNtpInMilliseconds());
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      VideoContentType::UNSPECIFIED,
                                      VideoFrameType::kVideoFrameKey);
    statistics_proxy_->OnRenderedFrame(MetaData(frame));
    time_controller_.AdvanceTime(1 / kDefaultFps);
  }

  // Why -1? Because RateStatistics does not consider the first frame in the
  // rate as it will appear in the previous bucket.
  EXPECT_EQ(kDefaultFps.hertz() - 1,
            statistics_proxy_->GetStats().decode_frame_rate);
  EXPECT_EQ(kDefaultFps.hertz() - 1,
            statistics_proxy_->GetStats().render_frame_rate);

  // FPS trackers in stats proxy have a 1000ms sliding window.
  time_controller_.AdvanceTime(TimeDelta::Seconds(1));
  EXPECT_EQ(0, statistics_proxy_->GetStats().decode_frame_rate);
  EXPECT_EQ(0, statistics_proxy_->GetStats().render_frame_rate);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsReceivedFrameStats) {
  EXPECT_EQ(0, statistics_proxy_->GetStats().width);
  EXPECT_EQ(0, statistics_proxy_->GetStats().height);
  EXPECT_EQ(0u, statistics_proxy_->GetStats().frames_rendered);

  statistics_proxy_->OnRenderedFrame(MetaData(CreateFrame(kWidth, kHeight)));

  EXPECT_EQ(kWidth, statistics_proxy_->GetStats().width);
  EXPECT_EQ(kHeight, statistics_proxy_->GetStats().height);
  EXPECT_EQ(1u, statistics_proxy_->GetStats().frames_rendered);
}

TEST_F(ReceiveStatisticsProxyTest,
       ReceivedFrameHistogramsAreNotUpdatedForTooFewSamples) {
  for (int i = 0; i < kMinRequiredSamples - 1; ++i) {
    statistics_proxy_->OnRenderedFrame(MetaData(CreateFrame(kWidth, kHeight)));
  }

  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples("WebRTC.Video.ReceivedWidthInPixels"));
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples("WebRTC.Video.ReceivedHeightInPixels"));
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples("WebRTC.Video.RenderFramesPerSecond"));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.RenderSqrtPixelsPerSecond"));
}

TEST_F(ReceiveStatisticsProxyTest, ReceivedFrameHistogramsAreUpdated) {
  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnRenderedFrame(MetaData(CreateFrame(kWidth, kHeight)));
  }

  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.ReceivedWidthInPixels"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.ReceivedHeightInPixels"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.RenderFramesPerSecond"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.RenderSqrtPixelsPerSecond"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.ReceivedWidthInPixels", kWidth));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.ReceivedHeightInPixels", kHeight));
}

TEST_F(ReceiveStatisticsProxyTest, ZeroDelayReportedIfFrameNotDelayed) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);

  // Frame not delayed, delayed frames to render: 0%.
  statistics_proxy_->OnRenderedFrame(
      MetaData(CreateFrameWithRenderTime(Now())));

  // Min run time has passed.
  time_controller_.AdvanceTime(
      TimeDelta::Seconds((metrics::kMinRunTimeInSeconds)));
  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.DelayedFramesToRenderer"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.DelayedFramesToRenderer", 0));
  EXPECT_METRIC_EQ(0, metrics::NumSamples(
                          "WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs"));
}

TEST_F(ReceiveStatisticsProxyTest,
       DelayedFrameHistogramsAreNotUpdatedIfMinRuntimeHasNotPassed) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);

  // Frame not delayed, delayed frames to render: 0%.
  statistics_proxy_->OnRenderedFrame(
      MetaData(CreateFrameWithRenderTime(Now())));

  // Min run time has not passed.
  time_controller_.AdvanceTime(
      TimeDelta::Seconds(metrics::kMinRunTimeInSeconds) - TimeDelta::Millis(1));
  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples("WebRTC.Video.DelayedFramesToRenderer"));
  EXPECT_METRIC_EQ(0, metrics::NumSamples(
                          "WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs"));
}

TEST_F(ReceiveStatisticsProxyTest,
       DelayedFramesHistogramsAreNotUpdatedIfNoRenderedFrames) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);

  // Min run time has passed. No rendered frames.
  time_controller_.AdvanceTime(
      TimeDelta::Seconds((metrics::kMinRunTimeInSeconds)));
  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples("WebRTC.Video.DelayedFramesToRenderer"));
  EXPECT_METRIC_EQ(0, metrics::NumSamples(
                          "WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs"));
}

TEST_F(ReceiveStatisticsProxyTest, DelayReportedIfFrameIsDelayed) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);

  // Frame delayed 1 ms, delayed frames to render: 100%.
  statistics_proxy_->OnRenderedFrame(
      MetaData(CreateFrameWithRenderTimeMs(Now().ms() - 1)));

  // Min run time has passed.
  time_controller_.AdvanceTime(
      TimeDelta::Seconds(metrics::kMinRunTimeInSeconds));
  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.DelayedFramesToRenderer"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.DelayedFramesToRenderer", 100));
  EXPECT_METRIC_EQ(1, metrics::NumSamples(
                          "WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs",
                            1));
}

TEST_F(ReceiveStatisticsProxyTest, AverageDelayOfDelayedFramesIsReported) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    VideoContentType::UNSPECIFIED,
                                    VideoFrameType::kVideoFrameKey);

  // Two frames delayed (6 ms, 10 ms), delayed frames to render: 50%.
  const int64_t kNowMs = Now().ms();

  statistics_proxy_->OnRenderedFrame(
      MetaData(CreateFrameWithRenderTimeMs(kNowMs - 10)));
  statistics_proxy_->OnRenderedFrame(
      MetaData(CreateFrameWithRenderTimeMs(kNowMs - 6)));
  statistics_proxy_->OnRenderedFrame(
      MetaData(CreateFrameWithRenderTimeMs(kNowMs)));
  statistics_proxy_->OnRenderedFrame(
      MetaData(CreateFrameWithRenderTimeMs(kNowMs + 1)));

  // Min run time has passed.
  time_controller_.AdvanceTime(
      TimeDelta::Seconds(metrics::kMinRunTimeInSeconds));
  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.DelayedFramesToRenderer"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.DelayedFramesToRenderer", 50));
  EXPECT_METRIC_EQ(1, metrics::NumSamples(
                          "WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs",
                            8));
}

TEST_F(ReceiveStatisticsProxyTest,
       RtcpHistogramsNotUpdatedIfMinRuntimeHasNotPassed) {
  StreamDataCounters data_counters;
  data_counters.first_packet_time = time_controller_.GetClock()->CurrentTime();

  time_controller_.AdvanceTime(
      TimeDelta::Seconds(metrics::kMinRunTimeInSeconds) - TimeDelta::Millis(1));

  RtcpPacketTypeCounter counter;
  statistics_proxy_->RtcpPacketTypesCounterUpdated(kRemoteSsrc, counter);

  statistics_proxy_->UpdateHistograms(absl::nullopt, data_counters, nullptr);
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples("WebRTC.Video.FirPacketsSentPerMinute"));
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples("WebRTC.Video.PliPacketsSentPerMinute"));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.NackPacketsSentPerMinute"));
}

TEST_F(ReceiveStatisticsProxyTest, RtcpHistogramsAreUpdated) {
  StreamDataCounters data_counters;
  data_counters.first_packet_time = time_controller_.GetClock()->CurrentTime();
  time_controller_.AdvanceTime(
      TimeDelta::Seconds(metrics::kMinRunTimeInSeconds));

  const uint32_t kFirPackets = 100;
  const uint32_t kPliPackets = 200;
  const uint32_t kNackPackets = 300;

  RtcpPacketTypeCounter counter;
  counter.fir_packets = kFirPackets;
  counter.pli_packets = kPliPackets;
  counter.nack_packets = kNackPackets;
  statistics_proxy_->RtcpPacketTypesCounterUpdated(kRemoteSsrc, counter);

  statistics_proxy_->UpdateHistograms(absl::nullopt, data_counters, nullptr);
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.FirPacketsSentPerMinute"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.PliPacketsSentPerMinute"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.NackPacketsSentPerMinute"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.FirPacketsSentPerMinute",
                            kFirPackets * 60 / metrics::kMinRunTimeInSeconds));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.PliPacketsSentPerMinute",
                            kPliPackets * 60 / metrics::kMinRunTimeInSeconds));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.NackPacketsSentPerMinute",
                            kNackPackets * 60 / metrics::kMinRunTimeInSeconds));
}

class ReceiveStatisticsProxyTestWithFreezeDuration
    : public ReceiveStatisticsProxyTest,
      public ::testing::WithParamInterface<
          std::tuple<uint32_t, uint32_t, uint32_t>> {
 protected:
  const uint32_t frame_duration_ms_ = {std::get<0>(GetParam())};
  const uint32_t freeze_duration_ms_ = {std::get<1>(GetParam())};
  const uint32_t expected_freeze_count_ = {std::get<2>(GetParam())};
};

// It is a freeze if:
// frame_duration_ms >= max(3 * avg_frame_duration, avg_frame_duration + 150)
// where avg_frame_duration is average duration of last 30 frames including
// the current one.
//
// Condition 1: 3 * avg_frame_duration > avg_frame_duration + 150
const auto kFreezeDetectionCond1Freeze = std::make_tuple(150, 483, 1);
const auto kFreezeDetectionCond1NotFreeze = std::make_tuple(150, 482, 0);
// Condition 2: 3 * avg_frame_duration < avg_frame_duration + 150
const auto kFreezeDetectionCond2Freeze = std::make_tuple(30, 185, 1);
const auto kFreezeDetectionCond2NotFreeze = std::make_tuple(30, 184, 0);

INSTANTIATE_TEST_SUITE_P(_,
                         ReceiveStatisticsProxyTestWithFreezeDuration,
                         ::testing::Values(kFreezeDetectionCond1Freeze,
                                           kFreezeDetectionCond1NotFreeze,
                                           kFreezeDetectionCond2Freeze,
                                           kFreezeDetectionCond2NotFreeze));

TEST_P(ReceiveStatisticsProxyTestWithFreezeDuration, FreezeDetection) {
  VideoReceiveStreamInterface::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(0u, stats.freeze_count);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  // Add a very long frame. This is need to verify that average frame
  // duration, which is supposed to be calculated as mean of durations of
  // last 30 frames, is calculated correctly.
  statistics_proxy_->OnRenderedFrame(MetaData(frame));
  time_controller_.AdvanceTime(TimeDelta::Seconds(2));
  for (size_t i = 0;
       i <= VideoQualityObserver::kAvgInterframeDelaysWindowSizeFrames; ++i) {
    time_controller_.AdvanceTime(TimeDelta::Millis(frame_duration_ms_));
    statistics_proxy_->OnRenderedFrame(MetaData(frame));
  }

  time_controller_.AdvanceTime(TimeDelta::Millis(freeze_duration_ms_));
  statistics_proxy_->OnRenderedFrame(MetaData(frame));

  stats = statistics_proxy_->GetStats();
  EXPECT_EQ(stats.freeze_count, expected_freeze_count_);
}

class ReceiveStatisticsProxyTestWithContent
    : public ReceiveStatisticsProxyTest,
      public ::testing::WithParamInterface<webrtc::VideoContentType> {
 protected:
  const webrtc::VideoContentType content_type_{GetParam()};
};

INSTANTIATE_TEST_SUITE_P(ContentTypes,
                         ReceiveStatisticsProxyTestWithContent,
                         ::testing::Values(VideoContentType::UNSPECIFIED,
                                           VideoContentType::SCREENSHARE));

TEST_P(ReceiveStatisticsProxyTestWithContent, InterFrameDelaysAreReported) {
  const TimeDelta kInterFrameDelay = TimeDelta::Millis(33);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      content_type_,
                                      VideoFrameType::kVideoFrameKey);
    time_controller_.AdvanceTime(kInterFrameDelay);
  }
  // One extra with double the interval.
  time_controller_.AdvanceTime(kInterFrameDelay);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    content_type_,
                                    VideoFrameType::kVideoFrameDelta);

  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  const TimeDelta kExpectedInterFrame =
      (kInterFrameDelay * (kMinRequiredSamples - 1) + kInterFrameDelay * 2) /
      kMinRequiredSamples;
  if (videocontenttypehelpers::IsScreenshare(content_type_)) {
    EXPECT_METRIC_EQ(
        kExpectedInterFrame.ms(),
        metrics::MinSample("WebRTC.Video.Screenshare.InterframeDelayInMs"));
    EXPECT_METRIC_EQ(
        kInterFrameDelay.ms() * 2,
        metrics::MinSample("WebRTC.Video.Screenshare.InterframeDelayMaxInMs"));
  } else {
    EXPECT_METRIC_EQ(kExpectedInterFrame.ms(),
                     metrics::MinSample("WebRTC.Video.InterframeDelayInMs"));
    EXPECT_METRIC_EQ(kInterFrameDelay.ms() * 2,
                     metrics::MinSample("WebRTC.Video.InterframeDelayMaxInMs"));
  }
}

TEST_P(ReceiveStatisticsProxyTestWithContent,
       InterFrameDelaysPercentilesAreReported) {
  const TimeDelta kInterFrameDelay = TimeDelta::Millis(33);
  const int kLastFivePercentsSamples = kMinRequiredSamples * 5 / 100;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i <= kMinRequiredSamples - kLastFivePercentsSamples; ++i) {
    time_controller_.AdvanceTime(kInterFrameDelay);
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      content_type_,
                                      VideoFrameType::kVideoFrameKey);
  }
  // Last 5% of intervals are double in size.
  for (int i = 0; i < kLastFivePercentsSamples; ++i) {
    time_controller_.AdvanceTime(2 * kInterFrameDelay);
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      content_type_,
                                      VideoFrameType::kVideoFrameKey);
  }
  // Final sample is outlier and 10 times as big.
  time_controller_.AdvanceTime(10 * kInterFrameDelay);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    content_type_,
                                    VideoFrameType::kVideoFrameKey);

  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  const TimeDelta kExpectedInterFrame = kInterFrameDelay * 2;
  if (videocontenttypehelpers::IsScreenshare(content_type_)) {
    EXPECT_METRIC_EQ(
        kExpectedInterFrame.ms(),
        metrics::MinSample(
            "WebRTC.Video.Screenshare.InterframeDelay95PercentileInMs"));
  } else {
    EXPECT_METRIC_EQ(
        kExpectedInterFrame.ms(),
        metrics::MinSample("WebRTC.Video.InterframeDelay95PercentileInMs"));
  }
}

TEST_P(ReceiveStatisticsProxyTestWithContent,
       MaxInterFrameDelayOnlyWithValidAverage) {
  const TimeDelta kInterFrameDelay = TimeDelta::Millis(33);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      content_type_,
                                      VideoFrameType::kVideoFrameKey);
    time_controller_.AdvanceTime(kInterFrameDelay);
  }

  // `kMinRequiredSamples` samples, and thereby intervals, is required. That
  // means we're one frame short of having a valid data set.
  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  EXPECT_METRIC_EQ(0, metrics::NumSamples("WebRTC.Video.InterframeDelayInMs"));
  EXPECT_METRIC_EQ(0,
                   metrics::NumSamples("WebRTC.Video.InterframeDelayMaxInMs"));
  EXPECT_METRIC_EQ(
      0, metrics::NumSamples("WebRTC.Video.Screenshare.InterframeDelayInMs"));
  EXPECT_METRIC_EQ(0, metrics::NumSamples(
                          "WebRTC.Video.Screenshare.InterframeDelayMaxInMs"));
}

TEST_P(ReceiveStatisticsProxyTestWithContent, MaxInterFrameDelayOnlyWithPause) {
  const TimeDelta kInterFrameDelay = TimeDelta::Millis(33);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i <= kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      content_type_,
                                      VideoFrameType::kVideoFrameKey);
    time_controller_.AdvanceTime(kInterFrameDelay);
  }

  // At this state, we should have a valid inter-frame delay.
  // Indicate stream paused and make a large jump in time.
  statistics_proxy_->OnStreamInactive();
  time_controller_.AdvanceTime(TimeDelta::Seconds(5));
  // Insert two more frames. The interval during the pause should be
  // disregarded in the stats.
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    content_type_,
                                    VideoFrameType::kVideoFrameKey);
  time_controller_.AdvanceTime(kInterFrameDelay);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    content_type_,
                                    VideoFrameType::kVideoFrameDelta);

  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  if (videocontenttypehelpers::IsScreenshare(content_type_)) {
    EXPECT_METRIC_EQ(
        1, metrics::NumSamples("WebRTC.Video.Screenshare.InterframeDelayInMs"));
    EXPECT_METRIC_EQ(1, metrics::NumSamples(
                            "WebRTC.Video.Screenshare.InterframeDelayMaxInMs"));
    EXPECT_METRIC_EQ(
        kInterFrameDelay.ms(),
        metrics::MinSample("WebRTC.Video.Screenshare.InterframeDelayInMs"));
    EXPECT_METRIC_EQ(
        kInterFrameDelay.ms(),
        metrics::MinSample("WebRTC.Video.Screenshare.InterframeDelayMaxInMs"));
  } else {
    EXPECT_METRIC_EQ(1,
                     metrics::NumSamples("WebRTC.Video.InterframeDelayInMs"));
    EXPECT_METRIC_EQ(
        1, metrics::NumSamples("WebRTC.Video.InterframeDelayMaxInMs"));
    EXPECT_METRIC_EQ(kInterFrameDelay.ms(),
                     metrics::MinSample("WebRTC.Video.InterframeDelayInMs"));
    EXPECT_METRIC_EQ(kInterFrameDelay.ms(),
                     metrics::MinSample("WebRTC.Video.InterframeDelayMaxInMs"));
  }
}

TEST_P(ReceiveStatisticsProxyTestWithContent, FreezesAreReported) {
  const TimeDelta kInterFrameDelay = TimeDelta::Millis(33);
  const TimeDelta kFreezeDelay = TimeDelta::Millis(200);
  const TimeDelta kCallDuration =
      kMinRequiredSamples * kInterFrameDelay + kFreezeDelay;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kMinRequiredSamples; ++i) {
    VideoFrameMetaData meta = MetaData(frame);
    statistics_proxy_->OnDecodedFrame(
        meta, absl::nullopt, TimeDelta::Zero(), TimeDelta::Zero(),
        TimeDelta::Zero(), content_type_, VideoFrameType::kVideoFrameKey);
    statistics_proxy_->OnRenderedFrame(meta);
    time_controller_.AdvanceTime(kInterFrameDelay);
  }
  // Add extra freeze.
  time_controller_.AdvanceTime(kFreezeDelay);
  VideoFrameMetaData meta = MetaData(frame);
  statistics_proxy_->OnDecodedFrame(
      meta, absl::nullopt, TimeDelta::Zero(), TimeDelta::Zero(),
      TimeDelta::Zero(), content_type_, VideoFrameType::kVideoFrameDelta);
  statistics_proxy_->OnRenderedFrame(meta);

  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  const TimeDelta kExpectedTimeBetweenFreezes =
      kInterFrameDelay * (kMinRequiredSamples - 1);
  const int kExpectedNumberFreezesPerMinute = 60 / kCallDuration.seconds();
  if (videocontenttypehelpers::IsScreenshare(content_type_)) {
    EXPECT_METRIC_EQ(
        (kFreezeDelay + kInterFrameDelay).ms(),
        metrics::MinSample("WebRTC.Video.Screenshare.MeanFreezeDurationMs"));
    EXPECT_METRIC_EQ(kExpectedTimeBetweenFreezes.ms(),
                     metrics::MinSample(
                         "WebRTC.Video.Screenshare.MeanTimeBetweenFreezesMs"));
    EXPECT_METRIC_EQ(
        kExpectedNumberFreezesPerMinute,
        metrics::MinSample("WebRTC.Video.Screenshare.NumberFreezesPerMinute"));
  } else {
    EXPECT_METRIC_EQ((kFreezeDelay + kInterFrameDelay).ms(),
                     metrics::MinSample("WebRTC.Video.MeanFreezeDurationMs"));
    EXPECT_METRIC_EQ(
        kExpectedTimeBetweenFreezes.ms(),
        metrics::MinSample("WebRTC.Video.MeanTimeBetweenFreezesMs"));
    EXPECT_METRIC_EQ(kExpectedNumberFreezesPerMinute,
                     metrics::MinSample("WebRTC.Video.NumberFreezesPerMinute"));
  }
}

TEST_P(ReceiveStatisticsProxyTestWithContent, HarmonicFrameRateIsReported) {
  const TimeDelta kFrameDuration = TimeDelta::Millis(33);
  const TimeDelta kFreezeDuration = TimeDelta::Millis(200);
  const TimeDelta kPauseDuration = TimeDelta::Seconds(10);
  const TimeDelta kCallDuration =
      kMinRequiredSamples * kFrameDuration + kFreezeDuration + kPauseDuration;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kMinRequiredSamples; ++i) {
    time_controller_.AdvanceTime(kFrameDuration);
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      content_type_,
                                      VideoFrameType::kVideoFrameKey);
    statistics_proxy_->OnRenderedFrame(MetaData(frame));
  }

  // Freezes and pauses should be included into harmonic frame rate.
  // Add freeze.
  time_controller_.AdvanceTime(kFreezeDuration);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    content_type_,
                                    VideoFrameType::kVideoFrameDelta);
  statistics_proxy_->OnRenderedFrame(MetaData(frame));

  // Add pause.
  time_controller_.AdvanceTime(kPauseDuration);
  statistics_proxy_->OnStreamInactive();
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                    content_type_,
                                    VideoFrameType::kVideoFrameDelta);
  statistics_proxy_->OnRenderedFrame(MetaData(frame));

  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  double kSumSquaredFrameDurationSecs =
      (kMinRequiredSamples - 1) *
      (kFrameDuration.seconds<double>() * kFrameDuration.seconds<double>());
  kSumSquaredFrameDurationSecs +=
      kFreezeDuration.seconds<double>() * kFreezeDuration.seconds<double>();
  kSumSquaredFrameDurationSecs +=
      kPauseDuration.seconds<double>() * kPauseDuration.seconds<double>();
  const int kExpectedHarmonicFrameRateFps = std::round(
      kCallDuration.seconds<double>() / kSumSquaredFrameDurationSecs);
  if (videocontenttypehelpers::IsScreenshare(content_type_)) {
    EXPECT_METRIC_EQ(
        kExpectedHarmonicFrameRateFps,
        metrics::MinSample("WebRTC.Video.Screenshare.HarmonicFrameRate"));
  } else {
    EXPECT_METRIC_EQ(kExpectedHarmonicFrameRateFps,
                     metrics::MinSample("WebRTC.Video.HarmonicFrameRate"));
  }
}

TEST_P(ReceiveStatisticsProxyTestWithContent, PausesAreIgnored) {
  const TimeDelta kInterFrameDelay = TimeDelta::Millis(33);
  const TimeDelta kPauseDuration = TimeDelta::Seconds(10);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i <= kMinRequiredSamples; ++i) {
    VideoFrameMetaData meta = MetaData(frame);
    statistics_proxy_->OnDecodedFrame(
        meta, absl::nullopt, TimeDelta::Zero(), TimeDelta::Zero(),
        TimeDelta::Zero(), content_type_, VideoFrameType::kVideoFrameKey);
    statistics_proxy_->OnRenderedFrame(meta);
    time_controller_.AdvanceTime(kInterFrameDelay);
  }
  // Add a pause.
  time_controller_.AdvanceTime(kPauseDuration);
  statistics_proxy_->OnStreamInactive();
  // Second playback interval with triple the length.
  for (int i = 0; i <= kMinRequiredSamples * 3; ++i) {
    VideoFrameMetaData meta = MetaData(frame);
    statistics_proxy_->OnDecodedFrame(
        meta, absl::nullopt, TimeDelta::Zero(), TimeDelta::Zero(),
        TimeDelta::Zero(), content_type_, VideoFrameType::kVideoFrameDelta);
    statistics_proxy_->OnRenderedFrame(meta);
    time_controller_.AdvanceTime(kInterFrameDelay);
  }

  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  // Average of two playback intervals.
  const TimeDelta kExpectedTimeBetweenFreezes =
      kInterFrameDelay * kMinRequiredSamples * 2;
  if (videocontenttypehelpers::IsScreenshare(content_type_)) {
    EXPECT_METRIC_EQ(-1, metrics::MinSample(
                             "WebRTC.Video.Screenshare.MeanFreezeDurationMs"));
    EXPECT_METRIC_EQ(kExpectedTimeBetweenFreezes.ms(),
                     metrics::MinSample(
                         "WebRTC.Video.Screenshare.MeanTimeBetweenFreezesMs"));
  } else {
    EXPECT_METRIC_EQ(-1,
                     metrics::MinSample("WebRTC.Video.MeanFreezeDurationMs"));
    EXPECT_METRIC_EQ(
        kExpectedTimeBetweenFreezes.ms(),
        metrics::MinSample("WebRTC.Video.MeanTimeBetweenFreezesMs"));
  }
}

TEST_P(ReceiveStatisticsProxyTestWithContent, ManyPausesAtTheBeginning) {
  const TimeDelta kInterFrameDelay = TimeDelta::Millis(33);
  const TimeDelta kPauseDuration = TimeDelta::Seconds(10);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i <= kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      content_type_,
                                      VideoFrameType::kVideoFrameKey);
    time_controller_.AdvanceTime(kInterFrameDelay);
    statistics_proxy_->OnStreamInactive();
    time_controller_.AdvanceTime(kPauseDuration);
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, TimeDelta::Zero(),
                                      content_type_,
                                      VideoFrameType::kVideoFrameDelta);
    time_controller_.AdvanceTime(kInterFrameDelay);
  }

  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  // No freezes should be detected, as all long inter-frame delays were
  // pauses.
  if (videocontenttypehelpers::IsScreenshare(content_type_)) {
    EXPECT_METRIC_EQ(-1, metrics::MinSample(
                             "WebRTC.Video.Screenshare.MeanFreezeDurationMs"));
  } else {
    EXPECT_METRIC_EQ(-1,
                     metrics::MinSample("WebRTC.Video.MeanFreezeDurationMs"));
  }
}

TEST_P(ReceiveStatisticsProxyTestWithContent, TimeInHdReported) {
  const TimeDelta kInterFrameDelay = TimeDelta::Millis(20);
  webrtc::VideoFrame frame_hd = CreateFrame(1280, 720);
  webrtc::VideoFrame frame_sd = CreateFrame(640, 360);

  // HD frames.
  for (int i = 0; i < kMinRequiredSamples; ++i) {
    VideoFrameMetaData meta = MetaData(frame_hd);
    statistics_proxy_->OnDecodedFrame(
        meta, absl::nullopt, TimeDelta::Zero(), TimeDelta::Zero(),
        TimeDelta::Zero(), content_type_, VideoFrameType::kVideoFrameKey);
    statistics_proxy_->OnRenderedFrame(meta);
    time_controller_.AdvanceTime(kInterFrameDelay);
  }
  // SD frames.
  for (int i = 0; i < 2 * kMinRequiredSamples; ++i) {
    VideoFrameMetaData meta = MetaData(frame_sd);
    statistics_proxy_->OnDecodedFrame(
        meta, absl::nullopt, TimeDelta::Zero(), TimeDelta::Zero(),
        TimeDelta::Zero(), content_type_, VideoFrameType::kVideoFrameKey);
    statistics_proxy_->OnRenderedFrame(meta);
    time_controller_.AdvanceTime(kInterFrameDelay);
  }
  // Extra last frame.
  statistics_proxy_->OnRenderedFrame(MetaData(frame_sd));

  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  const int kExpectedTimeInHdPercents = 33;
  if (videocontenttypehelpers::IsScreenshare(content_type_)) {
    EXPECT_METRIC_EQ(
        kExpectedTimeInHdPercents,
        metrics::MinSample("WebRTC.Video.Screenshare.TimeInHdPercentage"));
  } else {
    EXPECT_METRIC_EQ(kExpectedTimeInHdPercents,
                     metrics::MinSample("WebRTC.Video.TimeInHdPercentage"));
  }
}

TEST_P(ReceiveStatisticsProxyTestWithContent, TimeInBlockyVideoReported) {
  const TimeDelta kInterFrameDelay = TimeDelta::Millis(20);
  const int kHighQp = 80;
  const int kLowQp = 30;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  // High quality frames.
  for (int i = 0; i < kMinRequiredSamples; ++i) {
    VideoFrameMetaData meta = MetaData(frame);
    statistics_proxy_->OnDecodedFrame(
        meta, kLowQp, TimeDelta::Zero(), TimeDelta::Zero(), TimeDelta::Zero(),
        content_type_, VideoFrameType::kVideoFrameKey);
    statistics_proxy_->OnRenderedFrame(meta);
    time_controller_.AdvanceTime(kInterFrameDelay);
  }
  // Blocky frames.
  for (int i = 0; i < 2 * kMinRequiredSamples; ++i) {
    VideoFrameMetaData meta = MetaData(frame);
    statistics_proxy_->OnDecodedFrame(
        meta, kHighQp, TimeDelta::Zero(), TimeDelta::Zero(), TimeDelta::Zero(),
        content_type_, VideoFrameType::kVideoFrameKey);
    statistics_proxy_->OnRenderedFrame(meta);
    time_controller_.AdvanceTime(kInterFrameDelay);
  }
  // Extra last frame.
  statistics_proxy_->OnDecodedFrame(frame, kHighQp, TimeDelta::Zero(),
                                    content_type_,
                                    VideoFrameType::kVideoFrameKey);
  statistics_proxy_->OnRenderedFrame(MetaData(frame));

  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  const int kExpectedTimeInHdPercents = 66;
  if (videocontenttypehelpers::IsScreenshare(content_type_)) {
    EXPECT_METRIC_EQ(
        kExpectedTimeInHdPercents,
        metrics::MinSample(
            "WebRTC.Video.Screenshare.TimeInBlockyVideoPercentage"));
  } else {
    EXPECT_METRIC_EQ(
        kExpectedTimeInHdPercents,
        metrics::MinSample("WebRTC.Video.TimeInBlockyVideoPercentage"));
  }
}

TEST_P(ReceiveStatisticsProxyTestWithContent, DownscalesReported) {
  // To ensure long enough call duration.
  const TimeDelta kInterFrameDelay = TimeDelta::Seconds(2);

  webrtc::VideoFrame frame_hd = CreateFrame(1280, 720);
  webrtc::VideoFrame frame_sd = CreateFrame(640, 360);
  webrtc::VideoFrame frame_ld = CreateFrame(320, 180);

  // Call once to pass content type.
  statistics_proxy_->OnDecodedFrame(frame_hd, absl::nullopt, TimeDelta::Zero(),
                                    content_type_,
                                    VideoFrameType::kVideoFrameKey);

  time_controller_.AdvanceTime(TimeDelta::Zero());
  statistics_proxy_->OnRenderedFrame(MetaData(frame_hd));
  time_controller_.AdvanceTime(kInterFrameDelay);
  // Downscale.
  statistics_proxy_->OnRenderedFrame(MetaData(frame_sd));
  time_controller_.AdvanceTime(kInterFrameDelay);
  // Downscale.
  statistics_proxy_->OnRenderedFrame(MetaData(frame_ld));
  time_controller_.AdvanceTime(kInterFrameDelay);
  statistics_proxy_->UpdateHistograms(absl::nullopt, StreamDataCounters(),
                                      nullptr);
  const int kExpectedDownscales = 30;  // 2 per 4 seconds = 30 per minute.
  if (!videocontenttypehelpers::IsScreenshare(content_type_)) {
    EXPECT_METRIC_EQ(kExpectedDownscales,
                     metrics::MinSample(
                         "WebRTC.Video.NumberResolutionDownswitchesPerMinute"));
  }
}

TEST_P(ReceiveStatisticsProxyTestWithContent, DecodeTimeReported) {
  const TimeDelta kInterFrameDelay = TimeDelta::Millis(20);
  const int kLowQp = 30;
  const TimeDelta kDecodeTime = TimeDelta::Millis(7);

  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, kLowQp, kDecodeTime, content_type_,
                                      VideoFrameType::kVideoFrameKey);
    time_controller_.AdvanceTime(kInterFrameDelay);
  }
  FlushAndUpdateHistograms(absl::nullopt, StreamDataCounters(), nullptr);
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.DecodeTimeInMs", kDecodeTime.ms()));
}

}  // namespace internal
}  // namespace webrtc
