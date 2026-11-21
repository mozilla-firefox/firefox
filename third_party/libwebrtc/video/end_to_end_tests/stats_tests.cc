/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>

#include "absl/algorithm/container.h"
#include "absl/types/optional.h"
#include "api/task_queue/task_queue_base.h"
#include "api/test/simulated_network.h"
#include "api/test/video/function_video_encoder_factory.h"
#include "call/fake_network_pipe.h"
#include "call/simulated_network.h"
#include "modules/rtp_rtcp/source/rtp_packet.h"
#include "modules/video_coding/include/video_coding_defines.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/task_queue_for_test.h"
#include "system_wrappers/include/metrics.h"
#include "system_wrappers/include/sleep.h"
#include "test/call_test.h"
#include "test/fake_encoder.h"
#include "test/gtest.h"
#include "test/rtcp_packet_parser.h"
#include "test/video_test_constants.h"

namespace webrtc {
namespace {
enum : int {  // The first valid value is 1.
  kVideoContentTypeExtensionId = 1,
};
}  // namespace

class StatsEndToEndTest : public test::CallTest {
 public:
  StatsEndToEndTest() {
    RegisterRtpExtension(RtpExtension(RtpExtension::kVideoContentTypeUri,
                                      kVideoContentTypeExtensionId));
  }
};

TEST_F(StatsEndToEndTest, GetStats) {
  static const int kStartBitrateBps = 3000000;
  static const int kExpectedRenderDelayMs = 20;

  class StatsObserver : public test::EndToEndTest {
   public:
    StatsObserver()
        : EndToEndTest(test::VideoTestConstants::kLongTimeout),
          encoder_factory_(
              [](const Environment& env, const SdpVideoFormat& format) {
                return std::make_unique<test::DelayedEncoder>(env, 10);
              }) {}

   private:
    Action OnSendRtp(rtc::ArrayView<const uint8_t> packet) override {
      // Drop every 25th packet => 4% loss.
      static const int kPacketLossFrac = 25;
      RtpPacket header;
      if (header.Parse(packet) &&
          expected_send_ssrcs_.find(header.Ssrc()) !=
              expected_send_ssrcs_.end() &&
          header.SequenceNumber() % kPacketLossFrac == 0) {
        return DROP_PACKET;
      }
      check_stats_event_.Set();
      return SEND_PACKET;
    }

    Action OnSendRtcp(rtc::ArrayView<const uint8_t> packet) override {
      check_stats_event_.Set();
      return SEND_PACKET;
    }

    Action OnReceiveRtp(rtc::ArrayView<const uint8_t> packet) override {
      check_stats_event_.Set();
      return SEND_PACKET;
    }

    Action OnReceiveRtcp(rtc::ArrayView<const uint8_t> packet) override {
      check_stats_event_.Set();
      return SEND_PACKET;
    }

    bool CheckReceiveStats() {
      for (size_t i = 0; i < receive_streams_.size(); ++i) {
        VideoReceiveStreamInterface::Stats stats =
            receive_streams_[i]->GetStats();
        EXPECT_EQ(expected_receive_ssrcs_[i], stats.ssrc);

        // Make sure all fields have been populated.
        // TODO(pbos): Use CompoundKey if/when we ever know that all stats are
        // always filled for all receivers.
        receive_stats_filled_["IncomingRate"] |=
            stats.network_frame_rate != 0 || stats.total_bitrate_bps != 0;

        send_stats_filled_["DecoderImplementationName"] |=
            stats.decoder_implementation_name ==
            test::FakeDecoder::kImplementationName;
        receive_stats_filled_["PowerEfficientDecoder"] =
            stats.power_efficient_decoder.has_value();
        receive_stats_filled_["RenderDelayAsHighAsExpected"] |=
            stats.render_delay_ms >= kExpectedRenderDelayMs;

        receive_stats_filled_["FrameCallback"] |= stats.decode_frame_rate != 0;

        receive_stats_filled_["FrameRendered"] |= stats.render_frame_rate != 0;

        receive_stats_filled_["StatisticsUpdated"] |=
            stats.rtp_stats.packets_lost != 0 || stats.rtp_stats.jitter != 0;

        receive_stats_filled_["DataCountersUpdated"] |=
            stats.rtp_stats.packet_counter.payload_bytes != 0 ||
            stats.rtp_stats.packet_counter.header_bytes != 0 ||
            stats.rtp_stats.packet_counter.packets != 0 ||
            stats.rtp_stats.packet_counter.padding_bytes != 0;

        receive_stats_filled_["CodecStats"] |= stats.target_delay_ms != 0;

        receive_stats_filled_["FrameCounts"] |=
            stats.frame_counts.key_frames != 0 ||
            stats.frame_counts.delta_frames != 0;

        receive_stats_filled_["JitterBufferDelay"] =
            stats.jitter_buffer_delay > TimeDelta::Zero();
        receive_stats_filled_["JitterBufferTargetDelay"] =
            stats.jitter_buffer_target_delay > TimeDelta::Zero();
        receive_stats_filled_["JitterBufferEmittedCount"] =
            stats.jitter_buffer_emitted_count != 0;
        receive_stats_filled_["JitterBufferMinimumDelay"] =
            stats.jitter_buffer_minimum_delay > TimeDelta::Zero();

        receive_stats_filled_["CName"] |= !stats.c_name.empty();

        receive_stats_filled_["RtcpPacketTypeCount"] |=
            stats.rtcp_packet_type_counts.fir_packets != 0 ||
            stats.rtcp_packet_type_counts.nack_packets != 0 ||
            stats.rtcp_packet_type_counts.pli_packets != 0 ||
            stats.rtcp_packet_type_counts.nack_requests != 0 ||
            stats.rtcp_packet_type_counts.unique_nack_requests != 0;

        RTC_DCHECK(stats.current_payload_type == -1 ||
                   stats.current_payload_type ==
                       test::VideoTestConstants::kFakeVideoSendPayloadType);
        receive_stats_filled_["IncomingPayloadType"] |=
            stats.current_payload_type ==
            test::VideoTestConstants::kFakeVideoSendPayloadType;
      }

      return AllStatsFilled(receive_stats_filled_);
    }

    bool CheckSendStats() {
      RTC_DCHECK(send_stream_);

      VideoSendStream::Stats stats;
      SendTask(task_queue_, [&]() { stats = send_stream_->GetStats(); });

      size_t expected_num_streams =
          test::VideoTestConstants::kNumSimulcastStreams +
          expected_send_ssrcs_.size();
      send_stats_filled_["NumStreams"] |=
          stats.substreams.size() == expected_num_streams;

      send_stats_filled_["CpuOveruseMetrics"] |=
          stats.avg_encode_time_ms != 0 && stats.encode_usage_percent != 0 &&
          stats.total_encode_time_ms != 0;

      send_stats_filled_["EncoderImplementationName"] |=
          stats.encoder_implementation_name ==
          test::FakeEncoder::kImplementationName;

      send_stats_filled_["PowerEfficientEncoder"] |=
          stats.power_efficient_encoder == true;

      for (const auto& kv : stats.substreams) {
        if (expected_send_ssrcs_.find(kv.first) == expected_send_ssrcs_.end())
          continue;  // Probably RTX.

        send_stats_filled_[CompoundKey("CapturedFrameRate", kv.first)] |=
            stats.input_frame_rate != 0;

        const VideoSendStream::StreamStats& stream_stats = kv.second;

        send_stats_filled_[CompoundKey("StatisticsUpdated", kv.first)] |=
            stream_stats.report_block_data.has_value();

        send_stats_filled_[CompoundKey("DataCountersUpdated", kv.first)] |=
            stream_stats.rtp_stats.fec.packets != 0 ||
            stream_stats.rtp_stats.transmitted.padding_bytes != 0 ||
            stream_stats.rtp_stats.retransmitted.packets != 0 ||
            stream_stats.rtp_stats.transmitted.packets != 0;

        send_stats_filled_[CompoundKey("BitrateStatisticsObserver.Total",
                                       kv.first)] |=
            stream_stats.total_bitrate_bps != 0;

        send_stats_filled_[CompoundKey("BitrateStatisticsObserver.Retransmit",
                                       kv.first)] |=
            stream_stats.retransmit_bitrate_bps != 0;

        send_stats_filled_[CompoundKey("FrameCountObserver", kv.first)] |=
            stream_stats.frame_counts.delta_frames != 0 ||
            stream_stats.frame_counts.key_frames != 0;

        send_stats_filled_[CompoundKey("OutgoingRate", kv.first)] |=
            stats.encode_frame_rate != 0;

        send_stats_filled_[CompoundKey("Delay", kv.first)] |=
            stream_stats.avg_delay_ms != 0 || stream_stats.max_delay_ms != 0;

        // TODO(pbos): Use CompoundKey when the test makes sure that all SSRCs
        // report dropped packets.
        send_stats_filled_["RtcpPacketTypeCount"] |=
            stream_stats.rtcp_packet_type_counts.fir_packets != 0 ||
            stream_stats.rtcp_packet_type_counts.nack_packets != 0 ||
            stream_stats.rtcp_packet_type_counts.pli_packets != 0 ||
            stream_stats.rtcp_packet_type_counts.nack_requests != 0 ||
            stream_stats.rtcp_packet_type_counts.unique_nack_requests != 0;
      }

      return AllStatsFilled(send_stats_filled_);
    }

    std::string CompoundKey(const char* name, uint32_t ssrc) {
      rtc::StringBuilder oss;
      oss << name << "_" << ssrc;
      return oss.Release();
    }

    bool AllStatsFilled(const std::map<std::string, bool>& stats_map) {
      for (const auto& stat : stats_map) {
        if (!stat.second)
          return false;
      }
      return true;
    }

    BuiltInNetworkBehaviorConfig GetSendTransportConfig() const override {
      BuiltInNetworkBehaviorConfig network_config;
      network_config.loss_percent = 5;
      return network_config;
    }

    void ModifySenderBitrateConfig(
        BitrateConstraints* bitrate_config) override {
      bitrate_config->start_bitrate_bps = kStartBitrateBps;
    }

    void ModifyVideoConfigs(
        VideoSendStream::Config* send_config,
        std::vector<VideoReceiveStreamInterface::Config>* receive_configs,
        VideoEncoderConfig* encoder_config) override {
      // Set low simulcast bitrates to not have to wait for bandwidth ramp-up.
      encoder_config->max_bitrate_bps = 50000;
      for (auto& layer : encoder_config->simulcast_layers) {
        layer.min_bitrate_bps = 10000;
        layer.target_bitrate_bps = 15000;
        layer.max_bitrate_bps = 20000;
      }

      send_config->rtp.c_name = "SomeCName";
      send_config->rtp.nack.rtp_history_ms =
          test::VideoTestConstants::kNackRtpHistoryMs;
      send_config->rtp.rtx.payload_type =
          test::VideoTestConstants::kSendRtxPayloadType;

      const std::vector<uint32_t>& ssrcs = send_config->rtp.ssrcs;
      for (size_t i = 0; i < ssrcs.size(); ++i) {
        expected_send_ssrcs_.insert(ssrcs[i]);
        expected_receive_ssrcs_.push_back(
            (*receive_configs)[i].rtp.remote_ssrc);
        (*receive_configs)[i].render_delay_ms = kExpectedRenderDelayMs;
        (*receive_configs)[i].rtp.nack.rtp_history_ms =
            test::VideoTestConstants::kNackRtpHistoryMs;

        (*receive_configs)[i].rtp.rtx_ssrc =
            test::VideoTestConstants::kSendRtxSsrcs[i];
        (*receive_configs)[i].rtp.rtx_associated_payload_types
            [test::VideoTestConstants::kSendRtxPayloadType] =
            test::VideoTestConstants::kFakeVideoSendPayloadType;
      }

      for (size_t i = 0; i < test::VideoTestConstants::kNumSimulcastStreams;
           ++i)
        send_config->rtp.rtx.ssrcs.push_back(
            test::VideoTestConstants::kSendRtxSsrcs[i]);

      // Use a delayed encoder to make sure we see CpuOveruseMetrics stats that
      // are non-zero.
      send_config->encoder_settings.encoder_factory = &encoder_factory_;
    }

    size_t GetNumVideoStreams() const override {
      return test::VideoTestConstants::kNumSimulcastStreams;
    }

    void OnVideoStreamsCreated(VideoSendStream* send_stream,
                               const std::vector<VideoReceiveStreamInterface*>&
                                   receive_streams) override {
      send_stream_ = send_stream;
      receive_streams_ = receive_streams;
      task_queue_ = TaskQueueBase::Current();
    }

    void PerformTest() override {
      Clock* clock = Clock::GetRealTimeClock();
      int64_t now_ms = clock->TimeInMilliseconds();
      int64_t stop_time_ms =
          now_ms + test::VideoTestConstants::kLongTimeout.ms();
      bool receive_ok = false;
      bool send_ok = false;

      while (now_ms < stop_time_ms) {
        if (!receive_ok && task_queue_) {
          SendTask(task_queue_, [&]() { receive_ok = CheckReceiveStats(); });
        }
        if (!send_ok)
          send_ok = CheckSendStats();

        if (receive_ok && send_ok)
          return;

        int64_t time_until_timeout_ms = stop_time_ms - now_ms;
        if (time_until_timeout_ms > 0)
          check_stats_event_.Wait(TimeDelta::Millis(time_until_timeout_ms));
        now_ms = clock->TimeInMilliseconds();
      }

      ADD_FAILURE() << "Timed out waiting for filled stats.";
      for (const auto& kv : receive_stats_filled_) {
        if (!kv.second) {
          ADD_FAILURE() << "Missing receive stats: " << kv.first;
        }
      }
      for (const auto& kv : send_stats_filled_) {
        if (!kv.second) {
          ADD_FAILURE() << "Missing send stats: " << kv.first;
        }
      }
    }

    test::FunctionVideoEncoderFactory encoder_factory_;
    std::vector<VideoReceiveStreamInterface*> receive_streams_;
    std::map<std::string, bool> receive_stats_filled_;

    VideoSendStream* send_stream_ = nullptr;
    std::map<std::string, bool> send_stats_filled_;

    std::vector<uint32_t> expected_receive_ssrcs_;
    std::set<uint32_t> expected_send_ssrcs_;

    rtc::Event check_stats_event_;
    TaskQueueBase* task_queue_ = nullptr;
  } test;

  RunBaseTest(&test);
}

TEST_F(StatsEndToEndTest, TimingFramesAreReported) {
  static const int kExtensionId = 5;
  RegisterRtpExtension(
      RtpExtension(RtpExtension::kVideoTimingUri, kExtensionId));

  class StatsObserver : public test::EndToEndTest {
   public:
    StatsObserver() : EndToEndTest(test::VideoTestConstants::kLongTimeout) {}

   private:
    void ModifyVideoConfigs(
        VideoSendStream::Config* send_config,
        std::vector<VideoReceiveStreamInterface::Config>* receive_configs,
        VideoEncoderConfig* encoder_config) override {
      send_config->rtp.extensions.clear();
      send_config->rtp.extensions.push_back(
          RtpExtension(RtpExtension::kVideoTimingUri, kExtensionId));
    }

    void OnVideoStreamsCreated(VideoSendStream* send_stream,
                               const std::vector<VideoReceiveStreamInterface*>&
                                   receive_streams) override {
      receive_streams_ = receive_streams;
      task_queue_ = TaskQueueBase::Current();
    }

    void PerformTest() override {
      // No frames reported initially.
      SendTask(task_queue_, [&]() {
        for (const auto& receive_stream : receive_streams_) {
          EXPECT_FALSE(receive_stream->GetStats().timing_frame_info);
        }
      });
      // Wait for at least one timing frame to be sent with 100ms grace period.
      SleepMs(kDefaultTimingFramesDelayMs + 100);
      // Check that timing frames are reported for each stream.
      SendTask(task_queue_, [&]() {
        for (const auto& receive_stream : receive_streams_) {
          EXPECT_TRUE(receive_stream->GetStats().timing_frame_info);
        }
      });
    }

    std::vector<VideoReceiveStreamInterface*> receive_streams_;
    TaskQueueBase* task_queue_ = nullptr;
  } test;

  RunBaseTest(&test);
}

TEST_F(StatsEndToEndTest, TestReceivedRtpPacketStats) {
  static const size_t kNumRtpPacketsToSend = 5;
  class ReceivedRtpStatsObserver : public test::EndToEndTest {
   public:
    explicit ReceivedRtpStatsObserver(TaskQueueBase* task_queue)
        : EndToEndTest(test::VideoTestConstants::kDefaultTimeout),
          task_queue_(task_queue) {}

   private:
    void OnVideoStreamsCreated(VideoSendStream* send_stream,
                               const std::vector<VideoReceiveStreamInterface*>&
                                   receive_streams) override {
      receive_stream_ = receive_streams[0];
    }

    void OnStreamsStopped() override { task_safety_flag_->SetNotAlive(); }

    Action OnSendRtp(rtc::ArrayView<const uint8_t> packet) override {
      if (sent_rtp_ >= kNumRtpPacketsToSend) {
        // Need to check the stats on the correct thread.
        task_queue_->PostTask(SafeTask(task_safety_flag_, [this]() {
          VideoReceiveStreamInterface::Stats stats =
              receive_stream_->GetStats();
          if (kNumRtpPacketsToSend == stats.rtp_stats.packet_counter.packets) {
            observation_complete_.Set();
          }
        }));
        return DROP_PACKET;
      }
      ++sent_rtp_;
      return SEND_PACKET;
    }

    void PerformTest() override {
      EXPECT_TRUE(Wait())
          << "Timed out while verifying number of received RTP packets.";
    }

    VideoReceiveStreamInterface* receive_stream_ = nullptr;
    uint32_t sent_rtp_ = 0;
    TaskQueueBase* const task_queue_;
    rtc::scoped_refptr<PendingTaskSafetyFlag> task_safety_flag_ =
        PendingTaskSafetyFlag::CreateDetached();
  } test(task_queue());

  RunBaseTest(&test);
}

#if defined(WEBRTC_WIN)
// Disabled due to flakiness on Windows (bugs.webrtc.org/7483).
#define MAYBE_ContentTypeSwitches DISABLED_ContentTypeSwitches
#else
#define MAYBE_ContentTypeSwitches ContentTypeSwitches
#endif
TEST_F(StatsEndToEndTest, MAYBE_ContentTypeSwitches) {
  class StatsObserver : public test::BaseTest,
                        public rtc::VideoSinkInterface<VideoFrame> {
   public:
    StatsObserver()
        : BaseTest(test::VideoTestConstants::kLongTimeout),
          num_frames_received_(0) {}

    bool ShouldCreateReceivers() const override { return true; }

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
      if (MinNumberOfFramesReceived())
        observation_complete_.Set();
      return SEND_PACKET;
    }

    bool MinNumberOfFramesReceived() const {
      // Have some room for frames with wrong content type during switch.
      const int kMinRequiredHistogramSamples = 200 + 50;
      MutexLock lock(&mutex_);
      return num_frames_received_ > kMinRequiredHistogramSamples;
    }

    // May be called several times.
    void PerformTest() override {
      EXPECT_TRUE(Wait()) << "Timed out waiting for enough packets.";
      // Reset frame counter so next PerformTest() call will do something.
      {
        MutexLock lock(&mutex_);
        num_frames_received_ = 0;
      }
    }

    mutable Mutex mutex_;
    int num_frames_received_ RTC_GUARDED_BY(&mutex_);
  } test;

  metrics::Reset();

  CallConfig send_config = SendCallConfig();
  test.ModifySenderBitrateConfig(&send_config.bitrate_config);
  CallConfig recv_config = RecvCallConfig();
  test.ModifyReceiverBitrateConfig(&recv_config.bitrate_config);

  VideoEncoderConfig encoder_config_with_screenshare;

  SendTask(task_queue(), [this, &test, &send_config, &recv_config,
                          &encoder_config_with_screenshare]() {
    CreateSenderCall(send_config);
    CreateReceiverCall(recv_config);
    CreateReceiveTransport(test.GetReceiveTransportConfig(), &test);
    CreateSendTransport(test.GetReceiveTransportConfig(), &test);

    receiver_call_->SignalChannelNetworkState(MediaType::VIDEO, kNetworkUp);
    CreateSendConfig(1, 0, 0);
    CreateMatchingReceiveConfigs();

    // Modify send and receive configs.
    GetVideoSendConfig()->rtp.nack.rtp_history_ms =
        test::VideoTestConstants::kNackRtpHistoryMs;
    video_receive_configs_[0].rtp.nack.rtp_history_ms =
        test::VideoTestConstants::kNackRtpHistoryMs;
    video_receive_configs_[0].renderer = &test;
    // RTT needed for RemoteNtpTimeEstimator for the receive stream.
    video_receive_configs_[0].rtp.rtcp_xr.receiver_reference_time_report = true;
    // Start with realtime video.
    GetVideoEncoderConfig()->content_type =
        VideoEncoderConfig::ContentType::kRealtimeVideo;
    // Encoder config for the second part of the test uses screenshare.
    encoder_config_with_screenshare = GetVideoEncoderConfig()->Copy();
    encoder_config_with_screenshare.content_type =
        VideoEncoderConfig::ContentType::kScreen;

    CreateVideoStreams();
    CreateFrameGeneratorCapturer(test::VideoTestConstants::kDefaultFramerate,
                                 test::VideoTestConstants::kDefaultWidth,
                                 test::VideoTestConstants::kDefaultHeight);
    Start();
  });

  test.PerformTest();

  // Replace old send stream.
  SendTask(task_queue(), [this, &encoder_config_with_screenshare]() {
    DestroyVideoSendStreams();
    CreateVideoSendStream(encoder_config_with_screenshare);
    SetVideoDegradation(DegradationPreference::BALANCED);
    GetVideoSendStream()->Start();
  });

  // Continue to run test but now with screenshare.
  test.PerformTest();

  SendTask(task_queue(), [this]() {
    Stop();
    DestroyStreams();
    DestroyCalls();
  });

  // Verify that stats have been updated for both screenshare and video.
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.EndToEndDelayInMs"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.Screenshare.EndToEndDelayInMs"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.EndToEndDelayMaxInMs"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.Screenshare.EndToEndDelayMaxInMs"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples("WebRTC.Video.InterframeDelayInMs"));
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.Screenshare.InterframeDelayInMs"));
  EXPECT_METRIC_EQ(1,
                   metrics::NumSamples("WebRTC.Video.InterframeDelayMaxInMs"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples(
                          "WebRTC.Video.Screenshare.InterframeDelayMaxInMs"));
}

TEST_F(StatsEndToEndTest, VerifyNackStats) {
  static const int kPacketNumberToDrop = 200;
  class NackObserver : public test::EndToEndTest {
   public:
    explicit NackObserver(TaskQueueBase* task_queue)
        : EndToEndTest(test::VideoTestConstants::kLongTimeout),
          task_queue_(task_queue) {}

   private:
    Action OnSendRtp(rtc::ArrayView<const uint8_t> packet) override {
      {
        MutexLock lock(&mutex_);
        if (++sent_rtp_packets_ == kPacketNumberToDrop) {
          RtpPacket header;
          EXPECT_TRUE(header.Parse(packet));
          dropped_rtp_packet_ = header.SequenceNumber();
          return DROP_PACKET;
        }
      }
      task_queue_->PostTask(
          SafeTask(task_safety_flag_, [this]() { VerifyStats(); }));
      return SEND_PACKET;
    }

    Action OnReceiveRtcp(rtc::ArrayView<const uint8_t> packet) override {
      MutexLock lock(&mutex_);
      test::RtcpPacketParser rtcp_parser;
      rtcp_parser.Parse(packet);
      const std::vector<uint16_t>& nacks = rtcp_parser.nack()->packet_ids();
      if (!nacks.empty() && absl::c_linear_search(nacks, dropped_rtp_packet_)) {
        dropped_rtp_packet_requested_ = true;
      }
      return SEND_PACKET;
    }

    void VerifyStats() {
      MutexLock lock(&mutex_);
      if (!dropped_rtp_packet_requested_)
        return;
      int send_stream_nack_packets = 0;
      int receive_stream_nack_packets = 0;
      VideoSendStream::Stats stats = send_stream_->GetStats();
      for (const auto& kv : stats.substreams) {
        const VideoSendStream::StreamStats& stream_stats = kv.second;
        send_stream_nack_packets +=
            stream_stats.rtcp_packet_type_counts.nack_packets;
      }
      for (const auto& receive_stream : receive_streams_) {
        VideoReceiveStreamInterface::Stats stats = receive_stream->GetStats();
        receive_stream_nack_packets +=
            stats.rtcp_packet_type_counts.nack_packets;
      }
      if (send_stream_nack_packets >= 1 && receive_stream_nack_packets >= 1) {
        // NACK packet sent on receive stream and received on sent stream.
        if (MinMetricRunTimePassed())
          observation_complete_.Set();
      }
    }

    bool MinMetricRunTimePassed() {
      int64_t now_ms = Clock::GetRealTimeClock()->TimeInMilliseconds();
      if (!start_runtime_ms_)
        start_runtime_ms_ = now_ms;

      int64_t elapsed_sec = (now_ms - *start_runtime_ms_) / 1000;
      return elapsed_sec > metrics::kMinRunTimeInSeconds;
    }

    void ModifyVideoConfigs(
        VideoSendStream::Config* send_config,
        std::vector<VideoReceiveStreamInterface::Config>* receive_configs,
        VideoEncoderConfig* encoder_config) override {
      send_config->rtp.nack.rtp_history_ms =
          test::VideoTestConstants::kNackRtpHistoryMs;
      (*receive_configs)[0].rtp.nack.rtp_history_ms =
          test::VideoTestConstants::kNackRtpHistoryMs;
    }

    void OnVideoStreamsCreated(VideoSendStream* send_stream,
                               const std::vector<VideoReceiveStreamInterface*>&
                                   receive_streams) override {
      send_stream_ = send_stream;
      receive_streams_ = receive_streams;
    }

    void OnStreamsStopped() override { task_safety_flag_->SetNotAlive(); }

    void PerformTest() override {
      EXPECT_TRUE(Wait()) << "Timed out waiting for packet to be NACKed.";
    }

    Mutex mutex_;
    uint64_t sent_rtp_packets_ RTC_GUARDED_BY(&mutex_) = 0;
    uint16_t dropped_rtp_packet_ RTC_GUARDED_BY(&mutex_) = 0;
    bool dropped_rtp_packet_requested_ RTC_GUARDED_BY(&mutex_) = false;
    std::vector<VideoReceiveStreamInterface*> receive_streams_;
    VideoSendStream* send_stream_ = nullptr;
    absl::optional<int64_t> start_runtime_ms_;
    TaskQueueBase* const task_queue_;
    rtc::scoped_refptr<PendingTaskSafetyFlag> task_safety_flag_ =
        PendingTaskSafetyFlag::CreateDetached();
  } test(task_queue());

  metrics::Reset();
  RunBaseTest(&test);

  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.UniqueNackRequestsSentInPercent"));
  EXPECT_METRIC_EQ(1, metrics::NumSamples(
                          "WebRTC.Video.UniqueNackRequestsReceivedInPercent"));
  EXPECT_METRIC_GT(metrics::MinSample("WebRTC.Video.NackPacketsSentPerMinute"),
                   0);
}

TEST_F(StatsEndToEndTest, CallReportsRttForSender) {
  static const int kSendDelayMs = 30;
  static const int kReceiveDelayMs = 70;

  SendTask(task_queue(), [this]() {
    BuiltInNetworkBehaviorConfig config;
    config.queue_delay_ms = kSendDelayMs;
    CreateCalls();
    CreateSendTransport(config, /*observer*/ nullptr);

    config.queue_delay_ms = kReceiveDelayMs;
    CreateReceiveTransport(config, /*observer*/ nullptr);

    CreateSendConfig(1, 0, 0);
    CreateMatchingReceiveConfigs();

    CreateVideoStreams();
    CreateFrameGeneratorCapturer(test::VideoTestConstants::kDefaultFramerate,
                                 test::VideoTestConstants::kDefaultWidth,
                                 test::VideoTestConstants::kDefaultHeight);
    receiver_call_->SignalChannelNetworkState(MediaType::VIDEO, kNetworkUp);
    Start();
  });

  int64_t start_time_ms = env().clock().TimeInMilliseconds();
  while (true) {
    Call::Stats stats;
    SendTask(task_queue(),
             [this, &stats]() { stats = sender_call_->GetStats(); });
    ASSERT_GE(start_time_ms + test::VideoTestConstants::kDefaultTimeout.ms(),
              env().clock().TimeInMilliseconds())
        << "No RTT stats before timeout!";
    if (stats.rtt_ms != -1) {
      // To avoid failures caused by rounding or minor ntp clock adjustments,
      // relax expectation by 1ms.
      constexpr int kAllowedErrorMs = 1;
      EXPECT_GE(stats.rtt_ms, kSendDelayMs + kReceiveDelayMs - kAllowedErrorMs);
      break;
    }
    SleepMs(10);
  }

  SendTask(task_queue(), [this]() {
    Stop();
    DestroyStreams();
    DestroyCalls();
  });
}
}  // namespace webrtc
