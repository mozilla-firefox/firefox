/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "absl/types/optional.h"
#include "api/test/video/function_video_encoder_factory.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "rtc_base/synchronization/mutex.h"
#include "system_wrappers/include/metrics.h"
#include "test/call_test.h"
#include "test/gtest.h"
#include "test/video_test_constants.h"

namespace webrtc {
namespace {
enum : int {  // The first valid value is 1.
  kTransportSequenceNumberExtensionId = 1,
  kVideoContentTypeExtensionId,
};
}  // namespace

class HistogramTest : public test::CallTest {
 public:
  HistogramTest() {
    RegisterRtpExtension(RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                                      kTransportSequenceNumberExtensionId));
    RegisterRtpExtension(RtpExtension(RtpExtension::kVideoContentTypeUri,
                                      kVideoContentTypeExtensionId));
  }

 protected:
  void VerifyHistogramStats(bool use_rtx, bool use_fec, bool screenshare);
};

void HistogramTest::VerifyHistogramStats(bool use_rtx,
                                         bool use_fec,
                                         bool screenshare) {
  class FrameObserver : public test::EndToEndTest,
                        public rtc::VideoSinkInterface<VideoFrame> {
   public:
    FrameObserver(bool use_rtx, bool use_fec, bool screenshare)
        : EndToEndTest(test::VideoTestConstants::kLongTimeout),
          use_rtx_(use_rtx),
          use_fec_(use_fec),
          screenshare_(screenshare),
          // This test uses NACK, so to send FEC we can't use a fake encoder.
          encoder_factory_(
              [](const Environment& env, const SdpVideoFormat& format) {
                return CreateVp8Encoder(env);
              }),
          num_frames_received_(0) {}

   private:
    void OnFrame(const VideoFrame& video_frame) override {
      // The RTT is needed to estimate `ntp_time_ms` which is used by
      // end-to-end delay stats. Therefore, start counting received frames once
      // `ntp_time_ms` is valid.
      if (video_frame.ntp_time_ms() > 0 &&
          Clock::GetRealTimeClock()->CurrentNtpInMilliseconds() >=
              video_frame.ntp_time_ms()) {
        MutexLock lock(&mutex_);
        ++num_frames_received_;
      }
    }

    Action OnSendRtp(rtc::ArrayView<const uint8_t> packet) override {
      if (MinMetricRunTimePassed() && MinNumberOfFramesReceived())
        observation_complete_.Set();

      return SEND_PACKET;
    }

    bool MinMetricRunTimePassed() {
      int64_t now_ms = Clock::GetRealTimeClock()->TimeInMilliseconds();
      if (!start_runtime_ms_)
        start_runtime_ms_ = now_ms;

      int64_t elapsed_sec = (now_ms - *start_runtime_ms_) / 1000;
      return elapsed_sec > metrics::kMinRunTimeInSeconds * 2;
    }

    bool MinNumberOfFramesReceived() const {
      const int kMinRequiredHistogramSamples = 200;
      MutexLock lock(&mutex_);
      return num_frames_received_ > kMinRequiredHistogramSamples;
    }

    void ModifyVideoConfigs(
        VideoSendStream::Config* send_config,
        std::vector<VideoReceiveStreamInterface::Config>* receive_configs,
        VideoEncoderConfig* encoder_config) override {
      // NACK
      send_config->rtp.nack.rtp_history_ms =
          test::VideoTestConstants::kNackRtpHistoryMs;
      (*receive_configs)[0].rtp.nack.rtp_history_ms =
          test::VideoTestConstants::kNackRtpHistoryMs;
      (*receive_configs)[0].renderer = this;
      // FEC
      if (use_fec_) {
        send_config->rtp.ulpfec.ulpfec_payload_type =
            test::VideoTestConstants::kUlpfecPayloadType;
        send_config->rtp.ulpfec.red_payload_type =
            test::VideoTestConstants::kRedPayloadType;
        send_config->encoder_settings.encoder_factory = &encoder_factory_;
        send_config->rtp.payload_name = "VP8";
        encoder_config->codec_type = kVideoCodecVP8;
        (*receive_configs)[0].decoders[0].video_format = SdpVideoFormat::VP8();
        (*receive_configs)[0].rtp.red_payload_type =
            test::VideoTestConstants::kRedPayloadType;
        (*receive_configs)[0].rtp.ulpfec_payload_type =
            test::VideoTestConstants::kUlpfecPayloadType;
      }
      // RTX
      if (use_rtx_) {
        send_config->rtp.rtx.ssrcs.push_back(
            test::VideoTestConstants::kSendRtxSsrcs[0]);
        send_config->rtp.rtx.payload_type =
            test::VideoTestConstants::kSendRtxPayloadType;
        (*receive_configs)[0].rtp.rtx_ssrc =
            test::VideoTestConstants::kSendRtxSsrcs[0];
        (*receive_configs)[0].rtp.rtx_associated_payload_types
            [test::VideoTestConstants::kSendRtxPayloadType] =
            test::VideoTestConstants::kFakeVideoSendPayloadType;
        if (use_fec_) {
          send_config->rtp.ulpfec.red_rtx_payload_type =
              test::VideoTestConstants::kRtxRedPayloadType;
          (*receive_configs)[0].rtp.rtx_associated_payload_types
              [test::VideoTestConstants::kRtxRedPayloadType] =
              test::VideoTestConstants::kSendRtxPayloadType;
        }
      }
      // RTT needed for RemoteNtpTimeEstimator for the receive stream.
      (*receive_configs)[0].rtp.rtcp_xr.receiver_reference_time_report = true;
      encoder_config->content_type =
          screenshare_ ? VideoEncoderConfig::ContentType::kScreen
                       : VideoEncoderConfig::ContentType::kRealtimeVideo;
    }

    void PerformTest() override {
      EXPECT_TRUE(Wait()) << "Timed out waiting for min frames to be received.";
    }

    mutable Mutex mutex_;
    const bool use_rtx_;
    const bool use_fec_;
    const bool screenshare_;
    test::FunctionVideoEncoderFactory encoder_factory_;
    absl::optional<int64_t> start_runtime_ms_;
    int num_frames_received_ RTC_GUARDED_BY(&mutex_);
  } test(use_rtx, use_fec, screenshare);

  metrics::Reset();
  RunBaseTest(&test);

  const std::string video_prefix =
      screenshare ? "WebRTC.Video.Screenshare." : "WebRTC.Video.";

  // Verify that stats have been updated once.
  EXPECT_METRIC_EQ(2, metrics::NumSamples("WebRTC.Call.LifetimeInSeconds"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples(
                          "WebRTC.Call.TimeReceivingVideoRtpPacketsInSeconds"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Call.VideoBitrateReceivedInKbps"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Call.RtcpBitrateReceivedInBps"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Call.BitrateReceivedInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Call.EstimatedSendBitrateInKbps"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Call.PacerBitrateInKbps"));

  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.SendStreamLifetimeInSeconds"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.ReceiveStreamLifetimeInSeconds"));

  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.NackPacketsSentPerMinute"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(video_prefix + "NackPacketsReceivedPerMinute"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.FirPacketsSentPerMinute"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(video_prefix + "FirPacketsReceivedPerMinute"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.PliPacketsSentPerMinute"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(video_prefix + "PliPacketsReceivedPerMinute"));

  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(video_prefix + "KeyFramesSentInPermille"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.KeyFramesReceivedInPermille"));

  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(video_prefix + "SentPacketsLostInPercent"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.ReceivedPacketsLostInPercent"));

  EXPECT_METRIC_EQ(1, metrics::NumSamples(video_prefix + "InputWidthInPixels"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(video_prefix + "InputHeightInPixels"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples(video_prefix + "SentWidthInPixels"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples(video_prefix + "SentHeightInPixels"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(video_prefix + "ReceivedWidthInPixels"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(video_prefix + "ReceivedHeightInPixels"));

  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents(video_prefix + "InputWidthInPixels",
                                      test::VideoTestConstants::kDefaultWidth));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents(video_prefix + "InputHeightInPixels",
                            test::VideoTestConstants::kDefaultHeight));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents(video_prefix + "SentWidthInPixels",
                                      test::VideoTestConstants::kDefaultWidth));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents(video_prefix + "SentHeightInPixels",
                            test::VideoTestConstants::kDefaultHeight));
  EXPECT_METRIC_EQ(1,
                   metrics::NumEvents(video_prefix + "ReceivedWidthInPixels",
                                      test::VideoTestConstants::kDefaultWidth));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents(video_prefix + "ReceivedHeightInPixels",
                            test::VideoTestConstants::kDefaultHeight));

  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(video_prefix + "InputFramesPerSecond"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(video_prefix + "SentFramesPerSecond"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.DecodedFramesPerSecond"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.RenderFramesPerSecond"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.DelayedFramesToRenderer"));

  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.JitterBufferDelayInMs"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.TargetDelayInMs"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.CurrentDelayInMs"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.OnewayDelayInMs"));

  EXPECT_METRIC_EQ(1, metrics::NumSamples(video_prefix + "EndToEndDelayInMs"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(video_prefix + "EndToEndDelayMaxInMs"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(video_prefix + "InterframeDelayInMs"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(video_prefix + "InterframeDelayMaxInMs"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.RenderSqrtPixelsPerSecond"));

  EXPECT_METRIC_EQ(1, metrics::NumSamples(video_prefix + "EncodeTimeInMs"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.DecodeTimeInMs"));

  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(video_prefix + "NumberOfPauseEvents"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(video_prefix + "PausedTimeInPercent"));

  EXPECT_METRIC_EQ(1, metrics::NumSamples(video_prefix + "BitrateSentInKbps"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.BitrateReceivedInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(video_prefix + "MediaBitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.MediaBitrateReceivedInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(video_prefix + "PaddingBitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.PaddingBitrateReceivedInKbps"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples(video_prefix + "RetransmittedBitrateSentInKbps"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples(
                          "WebRTC.Video.RetransmittedBitrateReceivedInKbps"));

  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.SendDelayInMs"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples(video_prefix + "SendSideDelayInMs"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples(video_prefix + "SendSideDelayMaxInMs"));

  int num_rtx_samples = use_rtx ? 1 : 0;
  EXPECT_METRIC_EQ(num_rtx_samples,
                   metrics::NumSamples("WebRTC.Video.RtxBitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      num_rtx_samples,
      metrics::NumSamples("WebRTC.Video.RtxBitrateReceivedInKbps"));

  int num_red_samples = use_fec ? 1 : 0;
  EXPECT_METRIC_EQ(num_red_samples,
                   metrics::NumSamples("WebRTC.Video.FecBitrateSentInKbps"));
  EXPECT_METRIC_EQ(
      num_red_samples,
      metrics::NumSamples("WebRTC.Video.FecBitrateReceivedInKbps"));
  EXPECT_METRIC_EQ(
      num_red_samples,
      metrics::NumSamples("WebRTC.Video.ReceivedFecPacketsInPercent"));
}

TEST_F(HistogramTest, VerifyStatsWithRtx) {
  const bool kEnabledRtx = true;
  const bool kEnabledRed = false;
  const bool kScreenshare = false;
  VerifyHistogramStats(kEnabledRtx, kEnabledRed, kScreenshare);
}

TEST_F(HistogramTest, VerifyStatsWithRed) {
  const bool kEnabledRtx = false;
  const bool kEnabledRed = true;
  const bool kScreenshare = false;
  VerifyHistogramStats(kEnabledRtx, kEnabledRed, kScreenshare);
}

TEST_F(HistogramTest, VerifyStatsWithScreenshare) {
  const bool kEnabledRtx = false;
  const bool kEnabledRed = false;
  const bool kScreenshare = true;
  VerifyHistogramStats(kEnabledRtx, kEnabledRed, kScreenshare);
}

}  // namespace webrtc
