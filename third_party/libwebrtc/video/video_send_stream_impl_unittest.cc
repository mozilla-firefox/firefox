/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/video_send_stream_impl.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/types/optional.h"
#include "api/array_view.h"
#include "api/call/bitrate_allocation.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/rtc_event_log/rtc_event_log.h"
#include "api/rtp_parameters.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/encoded_image.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_layers_allocation.h"
#include "api/video_codecs/video_encoder.h"
#include "call/bitrate_allocator.h"
#include "call/rtp_config.h"
#include "call/rtp_video_sender_interface.h"
#include "call/test/mock_bitrate_allocator.h"
#include "call/test/mock_rtp_transport_controller_send.h"
#include "call/video_send_stream.h"
#include "modules/pacing/packet_router.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_sequence_number_map.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "rtc_base/experiments/alr_experiment.h"
#include "test/field_trial.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_transport.h"
#include "test/scoped_key_value_config.h"
#include "test/time_controller/simulated_time_controller.h"
#include "video/config/video_encoder_config.h"
#include "video/send_delay_stats.h"
#include "video/send_statistics_proxy.h"
#include "video/test/mock_video_stream_encoder.h"
#include "video/video_stream_encoder.h"
#include "video/video_stream_encoder_interface.h"

namespace webrtc {

bool operator==(const BitrateAllocationUpdate& a,
                const BitrateAllocationUpdate& b) {
  return a.target_bitrate == b.target_bitrate &&
         a.round_trip_time == b.round_trip_time &&
         a.packet_loss_ratio == b.packet_loss_ratio;
}

namespace internal {
namespace {
using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyNumber;
using ::testing::Field;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;

constexpr int64_t kDefaultInitialBitrateBps = 333000;
const double kDefaultBitratePriority = 0.5;

const float kAlrProbingExperimentPaceMultiplier = 1.0f;
std::string GetAlrProbingExperimentString() {
  return std::string(
             AlrExperimentSettings::kScreenshareProbingBweExperimentName) +
         "/1.0,2875,80,40,-60,3/";
}
class MockRtpVideoSender : public RtpVideoSenderInterface {
 public:
  MOCK_METHOD(void, SetSending, (bool sending), (override));
  MOCK_METHOD(bool, IsActive, (), (override));
  MOCK_METHOD(void, OnNetworkAvailability, (bool), (override));
  MOCK_METHOD((std::map<uint32_t, RtpState>),
              GetRtpStates,
              (),
              (const, override));
  MOCK_METHOD((std::map<uint32_t, RtpPayloadState>),
              GetRtpPayloadStates,
              (),
              (const, override));
  MOCK_METHOD(void, DeliverRtcp, (const uint8_t*, size_t), (override));
  MOCK_METHOD(void,
              OnBitrateAllocationUpdated,
              (const VideoBitrateAllocation&),
              (override));
  MOCK_METHOD(void,
              OnVideoLayersAllocationUpdated,
              (const VideoLayersAllocation&),
              (override));
  MOCK_METHOD(EncodedImageCallback::Result,
              OnEncodedImage,
              (const EncodedImage&, const CodecSpecificInfo*),
              (override));
  MOCK_METHOD(void, OnTransportOverheadChanged, (size_t), (override));
  MOCK_METHOD(void,
              OnBitrateUpdated,
              (BitrateAllocationUpdate, int),
              (override));
  MOCK_METHOD(uint32_t, GetPayloadBitrateBps, (), (const, override));
  MOCK_METHOD(uint32_t, GetProtectionBitrateBps, (), (const, override));
  MOCK_METHOD(void, SetEncodingData, (size_t, size_t, size_t), (override));
  MOCK_METHOD(std::vector<RtpSequenceNumberMap::Info>,
              GetSentRtpPacketInfos,
              (uint32_t ssrc, rtc::ArrayView<const uint16_t> sequence_numbers),
              (const, override));

  MOCK_METHOD(void, SetFecAllowed, (bool fec_allowed), (override));
};

BitrateAllocationUpdate CreateAllocation(int bitrate_bps) {
  BitrateAllocationUpdate update;
  update.target_bitrate = DataRate::BitsPerSec(bitrate_bps);
  update.packet_loss_ratio = 0;
  update.round_trip_time = TimeDelta::Zero();
  return update;
}

}  // namespace

class VideoSendStreamImplTest : public ::testing::Test {
 protected:
  VideoSendStreamImplTest()
      : time_controller_(Timestamp::Seconds(1000)),
        config_(&transport_),
        send_delay_stats_(time_controller_.GetClock()),
        encoder_queue_(time_controller_.GetTaskQueueFactory()->CreateTaskQueue(
            "encoder_queue",
            TaskQueueFactory::Priority::NORMAL)),
        stats_proxy_(time_controller_.GetClock(),
                     config_,
                     VideoEncoderConfig::ContentType::kRealtimeVideo,
                     field_trials_) {
    config_.rtp.ssrcs.push_back(8080);
    config_.rtp.payload_type = 1;

    EXPECT_CALL(transport_controller_, packet_router())
        .WillRepeatedly(Return(&packet_router_));
    EXPECT_CALL(transport_controller_, CreateRtpVideoSender)
        .WillRepeatedly(Return(&rtp_video_sender_));
    ON_CALL(rtp_video_sender_, IsActive()).WillByDefault(Invoke([&]() {
      return rtp_sending_;
    }));
    ON_CALL(rtp_video_sender_, SetSending)
        .WillByDefault(SaveArg<0>(&rtp_sending_));
  }
  ~VideoSendStreamImplTest() {}

  VideoEncoderConfig TestVideoEncoderConfig(
      VideoEncoderConfig::ContentType content_type =
          VideoEncoderConfig::ContentType::kRealtimeVideo,
      int initial_encoder_max_bitrate = kDefaultInitialBitrateBps,
      double initial_encoder_bitrate_priority = kDefaultBitratePriority) {
    VideoEncoderConfig encoder_config;
    encoder_config.max_bitrate_bps = initial_encoder_max_bitrate;
    encoder_config.bitrate_priority = initial_encoder_bitrate_priority;
    encoder_config.content_type = content_type;
    encoder_config.simulcast_layers.push_back(VideoStream());
    encoder_config.simulcast_layers.back().active = true;
    return encoder_config;
  }

  std::unique_ptr<VideoSendStreamImpl> CreateVideoSendStreamImpl(
      VideoEncoderConfig encoder_config) {
    EXPECT_CALL(bitrate_allocator_, GetStartBitrate).WillOnce(Return(123000));

    std::map<uint32_t, RtpState> suspended_ssrcs;
    std::map<uint32_t, RtpPayloadState> suspended_payload_states;

    std::unique_ptr<NiceMock<MockVideoStreamEncoder>> video_stream_encoder =
        std::make_unique<NiceMock<MockVideoStreamEncoder>>();
    video_stream_encoder_ = video_stream_encoder.get();

    auto ret = std::make_unique<VideoSendStreamImpl>(
        CreateEnvironment(&field_trials_, time_controller_.GetClock(),
                          time_controller_.GetTaskQueueFactory()),
        /*num_cpu_cores=*/1,
        /*call_stats=*/nullptr, &transport_controller_,
        /*metronome=*/nullptr, &bitrate_allocator_, &send_delay_stats_,
        config_.Copy(), std::move(encoder_config), suspended_ssrcs,
        suspended_payload_states,
        /*fec_controller=*/nullptr, std::move(video_stream_encoder));

    // The call to GetStartBitrate() executes asynchronously on the tq.
    // Ensure all tasks get to run.
    time_controller_.AdvanceTime(TimeDelta::Zero());
    testing::Mock::VerifyAndClearExpectations(&bitrate_allocator_);

    return ret;
  }

 protected:
  GlobalSimulatedTimeController time_controller_;
  webrtc::test::ScopedKeyValueConfig field_trials_;
  NiceMock<MockTransport> transport_;
  NiceMock<MockRtpTransportControllerSend> transport_controller_;
  NiceMock<MockBitrateAllocator> bitrate_allocator_;
  NiceMock<MockVideoStreamEncoder>* video_stream_encoder_ = nullptr;
  NiceMock<MockRtpVideoSender> rtp_video_sender_;
  bool rtp_sending_ = false;

  RtcEventLogNull event_log_;
  VideoSendStream::Config config_;
  SendDelayStats send_delay_stats_;
  std::unique_ptr<TaskQueueBase, TaskQueueDeleter> encoder_queue_;
  SendStatisticsProxy stats_proxy_;
  PacketRouter packet_router_;
};

TEST_F(VideoSendStreamImplTest,
       NotRegistersAsBitrateObserverOnStartIfNoActiveEncodings) {
  VideoEncoderConfig encoder_config = TestVideoEncoderConfig();
  encoder_config.simulcast_layers[0].active = false;
  auto vss_impl = CreateVideoSendStreamImpl(std::move(encoder_config));
  EXPECT_CALL(bitrate_allocator_, AddObserver(vss_impl.get(), _)).Times(0);
  EXPECT_CALL(bitrate_allocator_, RemoveObserver(vss_impl.get())).Times(0);

  vss_impl->Start();
  time_controller_.AdvanceTime(TimeDelta::Zero());
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest,
       RegistersAsBitrateObserverOnStartIfHasActiveEncodings) {
  auto vss_impl = CreateVideoSendStreamImpl(TestVideoEncoderConfig());

  EXPECT_CALL(bitrate_allocator_, AddObserver(vss_impl.get(), _));
  vss_impl->Start();
  time_controller_.AdvanceTime(TimeDelta::Zero());

  EXPECT_CALL(bitrate_allocator_, RemoveObserver(vss_impl.get())).Times(1);
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest,
       DeRegistersAsBitrateObserverIfNoActiveEncodings) {
  auto vss_impl = CreateVideoSendStreamImpl(TestVideoEncoderConfig());
  EXPECT_CALL(bitrate_allocator_, AddObserver(vss_impl.get(), _));
  vss_impl->Start();
  time_controller_.AdvanceTime(TimeDelta::Zero());

  EXPECT_CALL(bitrate_allocator_, RemoveObserver(vss_impl.get())).Times(1);
  VideoEncoderConfig no_active_encodings = TestVideoEncoderConfig();
  no_active_encodings.simulcast_layers[0].active = false;

  vss_impl->ReconfigureVideoEncoder(std::move(no_active_encodings));

  time_controller_.AdvanceTime(TimeDelta::Zero());
  ::testing::Mock::VerifyAndClearExpectations(&bitrate_allocator_);

  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest,
       DoNotRegistersAsBitrateObserverOnStrayEncodedImage) {
  auto vss_impl = CreateVideoSendStreamImpl(TestVideoEncoderConfig());

  EncodedImage encoded_image;
  CodecSpecificInfo codec_specific;
  ON_CALL(rtp_video_sender_, OnEncodedImage)
      .WillByDefault(Return(
          EncodedImageCallback::Result(EncodedImageCallback::Result::OK)));

  EXPECT_CALL(bitrate_allocator_, AddObserver(vss_impl.get(), _))
      .Times(AnyNumber());
  vss_impl->Start();
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // VideoSendStreamImpl gets an allocated bitrate.
  const uint32_t kBitrateBps = 100000;
  EXPECT_CALL(rtp_video_sender_, GetPayloadBitrateBps())
      .Times(1)
      .WillOnce(Return(kBitrateBps));
  static_cast<BitrateAllocatorObserver*>(vss_impl.get())
      ->OnBitrateUpdated(CreateAllocation(kBitrateBps));
  // A frame is encoded.
  encoder_queue_->PostTask([&] {
    static_cast<EncodedImageCallback*>(vss_impl.get())
        ->OnEncodedImage(encoded_image, &codec_specific);
  });

  // Expect allocation to be removed if encoder stop producing frames.
  EXPECT_CALL(bitrate_allocator_, RemoveObserver(vss_impl.get())).Times(1);
  time_controller_.AdvanceTime(TimeDelta::Seconds(5));
  Mock::VerifyAndClearExpectations(&bitrate_allocator_);

  EXPECT_CALL(bitrate_allocator_, AddObserver(vss_impl.get(), _)).Times(0);

  VideoEncoderConfig no_active_encodings = TestVideoEncoderConfig();
  no_active_encodings.simulcast_layers[0].active = false;
  vss_impl->ReconfigureVideoEncoder(std::move(no_active_encodings));

  // Expect that allocation in not resumed if a stray encoded image is received.
  encoder_queue_->PostTask([&] {
    static_cast<EncodedImageCallback*>(vss_impl.get())
        ->OnEncodedImage(encoded_image, &codec_specific);
  });

  time_controller_.AdvanceTime(TimeDelta::Zero());

  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, UpdatesObserverOnConfigurationChange) {
  const bool kSuspend = false;
  config_.suspend_below_min_bitrate = kSuspend;
  config_.rtp.extensions.emplace_back(RtpExtension::kTransportSequenceNumberUri,
                                      1);
  config_.rtp.ssrcs.emplace_back(1);
  config_.rtp.ssrcs.emplace_back(2);

  auto vss_impl = CreateVideoSendStreamImpl(TestVideoEncoderConfig());

  vss_impl->Start();

  // QVGA + VGA configuration matching defaults in
  // media/engine/simulcast.cc.
  VideoStream qvga_stream;
  qvga_stream.width = 320;
  qvga_stream.height = 180;
  qvga_stream.max_framerate = 30;
  qvga_stream.min_bitrate_bps = 30000;
  qvga_stream.target_bitrate_bps = 150000;
  qvga_stream.max_bitrate_bps = 200000;
  qvga_stream.max_qp = 56;
  qvga_stream.bitrate_priority = 1;

  VideoStream vga_stream;
  vga_stream.width = 640;
  vga_stream.height = 360;
  vga_stream.max_framerate = 30;
  vga_stream.min_bitrate_bps = 150000;
  vga_stream.target_bitrate_bps = 500000;
  vga_stream.max_bitrate_bps = 700000;
  vga_stream.max_qp = 56;
  vga_stream.bitrate_priority = 1;

  int min_transmit_bitrate_bps = 30000;

  EXPECT_CALL(bitrate_allocator_, AddObserver(vss_impl.get(), _))
      .WillRepeatedly(Invoke(
          [&](BitrateAllocatorObserver*, MediaStreamAllocationConfig config) {
            EXPECT_EQ(config.min_bitrate_bps,
                      static_cast<uint32_t>(min_transmit_bitrate_bps));
            EXPECT_EQ(config.max_bitrate_bps,
                      static_cast<uint32_t>(qvga_stream.max_bitrate_bps +
                                            vga_stream.max_bitrate_bps));
            if (config.pad_up_bitrate_bps != 0) {
              EXPECT_EQ(config.pad_up_bitrate_bps,
                        static_cast<uint32_t>(qvga_stream.target_bitrate_bps +
                                              vga_stream.min_bitrate_bps));
            }
            EXPECT_EQ(config.enforce_min_bitrate, !kSuspend);
          }));

  encoder_queue_->PostTask([&] {
    static_cast<VideoStreamEncoderInterface::EncoderSink*>(vss_impl.get())
        ->OnEncoderConfigurationChanged(
            std::vector<VideoStream>{qvga_stream, vga_stream}, false,
            VideoEncoderConfig::ContentType::kRealtimeVideo,
            min_transmit_bitrate_bps);
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, UpdatesObserverOnConfigurationChangeWithAlr) {
  const bool kSuspend = false;
  config_.suspend_below_min_bitrate = kSuspend;
  config_.rtp.extensions.emplace_back(RtpExtension::kTransportSequenceNumberUri,
                                      1);
  config_.periodic_alr_bandwidth_probing = true;
  config_.rtp.ssrcs.emplace_back(1);
  config_.rtp.ssrcs.emplace_back(2);

  auto vss_impl = CreateVideoSendStreamImpl(
      TestVideoEncoderConfig(VideoEncoderConfig::ContentType::kScreen));
  vss_impl->Start();

  // Simulcast screenshare.
  VideoStream low_stream;
  low_stream.width = 1920;
  low_stream.height = 1080;
  low_stream.max_framerate = 5;
  low_stream.min_bitrate_bps = 30000;
  low_stream.target_bitrate_bps = 200000;
  low_stream.max_bitrate_bps = 1000000;
  low_stream.num_temporal_layers = 2;
  low_stream.max_qp = 56;
  low_stream.bitrate_priority = 1;

  VideoStream high_stream;
  high_stream.width = 1920;
  high_stream.height = 1080;
  high_stream.max_framerate = 30;
  high_stream.min_bitrate_bps = 60000;
  high_stream.target_bitrate_bps = 1250000;
  high_stream.max_bitrate_bps = 1250000;
  high_stream.num_temporal_layers = 2;
  high_stream.max_qp = 56;
  high_stream.bitrate_priority = 1;

  // With ALR probing, this will be the padding target instead of
  // low_stream.target_bitrate_bps + high_stream.min_bitrate_bps.
  int min_transmit_bitrate_bps = 400000;

  EXPECT_CALL(bitrate_allocator_, AddObserver(vss_impl.get(), _))
      .WillRepeatedly(Invoke(
          [&](BitrateAllocatorObserver*, MediaStreamAllocationConfig config) {
            EXPECT_EQ(config.min_bitrate_bps,
                      static_cast<uint32_t>(low_stream.min_bitrate_bps));
            EXPECT_EQ(config.max_bitrate_bps,
                      static_cast<uint32_t>(low_stream.max_bitrate_bps +
                                            high_stream.max_bitrate_bps));
            if (config.pad_up_bitrate_bps != 0) {
              EXPECT_EQ(config.pad_up_bitrate_bps,
                        static_cast<uint32_t>(min_transmit_bitrate_bps));
            }
            EXPECT_EQ(config.enforce_min_bitrate, !kSuspend);
          }));
  encoder_queue_->PostTask([&] {
    static_cast<VideoStreamEncoderInterface::EncoderSink*>(vss_impl.get())
        ->OnEncoderConfigurationChanged(
            std::vector<VideoStream>{low_stream, high_stream}, false,
            VideoEncoderConfig::ContentType::kScreen, min_transmit_bitrate_bps);
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest,
       UpdatesObserverOnConfigurationChangeWithSimulcastVideoHysteresis) {
  test::ScopedKeyValueConfig hysteresis_experiment(
      field_trials_, "WebRTC-VideoRateControl/video_hysteresis:1.25/");
  config_.rtp.ssrcs.emplace_back(1);
  config_.rtp.ssrcs.emplace_back(2);

  auto vss_impl = CreateVideoSendStreamImpl(TestVideoEncoderConfig());

  vss_impl->Start();
  // 2-layer video simulcast.
  VideoStream low_stream;
  low_stream.width = 320;
  low_stream.height = 240;
  low_stream.max_framerate = 30;
  low_stream.min_bitrate_bps = 30000;
  low_stream.target_bitrate_bps = 100000;
  low_stream.max_bitrate_bps = 200000;
  low_stream.max_qp = 56;
  low_stream.bitrate_priority = 1;

  VideoStream high_stream;
  high_stream.width = 640;
  high_stream.height = 480;
  high_stream.max_framerate = 30;
  high_stream.min_bitrate_bps = 150000;
  high_stream.target_bitrate_bps = 500000;
  high_stream.max_bitrate_bps = 750000;
  high_stream.max_qp = 56;
  high_stream.bitrate_priority = 1;

  EXPECT_CALL(bitrate_allocator_, AddObserver(vss_impl.get(), _))
      .WillRepeatedly(Invoke([&](BitrateAllocatorObserver*,
                                 MediaStreamAllocationConfig config) {
        EXPECT_EQ(config.min_bitrate_bps,
                  static_cast<uint32_t>(low_stream.min_bitrate_bps));
        EXPECT_EQ(config.max_bitrate_bps,
                  static_cast<uint32_t>(low_stream.max_bitrate_bps +
                                        high_stream.max_bitrate_bps));
        if (config.pad_up_bitrate_bps != 0) {
          EXPECT_EQ(config.pad_up_bitrate_bps,
                    static_cast<uint32_t>(low_stream.target_bitrate_bps +
                                          1.25 * high_stream.min_bitrate_bps));
        }
      }));

  encoder_queue_->PostTask([&] {
    static_cast<VideoStreamEncoderInterface::EncoderSink*>(vss_impl.get())
        ->OnEncoderConfigurationChanged(
            std::vector<VideoStream>{low_stream, high_stream}, false,
            VideoEncoderConfig::ContentType::kRealtimeVideo,
            /*min_transmit_bitrate_bps=*/
            0);
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, SetsScreensharePacingFactorWithFeedback) {
  test::ScopedFieldTrials alr_experiment(GetAlrProbingExperimentString());

  constexpr int kId = 1;
  config_.rtp.extensions.emplace_back(RtpExtension::kTransportSequenceNumberUri,
                                      kId);
  EXPECT_CALL(transport_controller_,
              SetPacingFactor(kAlrProbingExperimentPaceMultiplier))
      .Times(1);
  auto vss_impl = CreateVideoSendStreamImpl(
      TestVideoEncoderConfig(VideoEncoderConfig::ContentType::kScreen));
  vss_impl->Start();
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, DoesNotSetPacingFactorWithoutFeedback) {
  test::ScopedFieldTrials alr_experiment(GetAlrProbingExperimentString());
  auto vss_impl = CreateVideoSendStreamImpl(
      TestVideoEncoderConfig(VideoEncoderConfig::ContentType::kScreen));
  EXPECT_CALL(transport_controller_, SetPacingFactor(_)).Times(0);
  vss_impl->Start();
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, ForwardsVideoBitrateAllocationWhenEnabled) {
  auto vss_impl = CreateVideoSendStreamImpl(
      TestVideoEncoderConfig(VideoEncoderConfig::ContentType::kScreen));

  EXPECT_CALL(transport_controller_, SetPacingFactor(_)).Times(0);
  VideoStreamEncoderInterface::EncoderSink* const sink =
      static_cast<VideoStreamEncoderInterface::EncoderSink*>(vss_impl.get());
  vss_impl->Start();
  // Populate a test instance of video bitrate allocation.
  VideoBitrateAllocation alloc;
  alloc.SetBitrate(0, 0, 10000);
  alloc.SetBitrate(0, 1, 20000);
  alloc.SetBitrate(1, 0, 30000);
  alloc.SetBitrate(1, 1, 40000);

  EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(alloc)).Times(0);
  encoder_queue_->PostTask([&] {
    // Encoder starts out paused, don't forward allocation.

    sink->OnBitrateAllocationUpdated(alloc);
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // Unpause encoder, allocation should be passed through.
  const uint32_t kBitrateBps = 100000;
  EXPECT_CALL(rtp_video_sender_, GetPayloadBitrateBps())
      .Times(1)
      .WillOnce(Return(kBitrateBps));
  static_cast<BitrateAllocatorObserver*>(vss_impl.get())
      ->OnBitrateUpdated(CreateAllocation(kBitrateBps));
  EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(alloc)).Times(1);
  encoder_queue_->PostTask([&] { sink->OnBitrateAllocationUpdated(alloc); });
  time_controller_.AdvanceTime(TimeDelta::Zero());
  // Pause encoder again, and block allocations.
  EXPECT_CALL(rtp_video_sender_, GetPayloadBitrateBps())
      .Times(1)
      .WillOnce(Return(0));
  static_cast<BitrateAllocatorObserver*>(vss_impl.get())
      ->OnBitrateUpdated(CreateAllocation(0));
  EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(alloc)).Times(0);
  encoder_queue_->PostTask([&] { sink->OnBitrateAllocationUpdated(alloc); });
  time_controller_.AdvanceTime(TimeDelta::Zero());
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, ThrottlesVideoBitrateAllocationWhenTooSimilar) {
  auto vss_impl = CreateVideoSendStreamImpl(
      TestVideoEncoderConfig(VideoEncoderConfig::ContentType::kScreen));
  vss_impl->Start();
  // Unpause encoder, to allows allocations to be passed through.
  const uint32_t kBitrateBps = 100000;
  EXPECT_CALL(rtp_video_sender_, GetPayloadBitrateBps())
      .Times(1)
      .WillOnce(Return(kBitrateBps));
  static_cast<BitrateAllocatorObserver*>(vss_impl.get())
      ->OnBitrateUpdated(CreateAllocation(kBitrateBps));
  VideoStreamEncoderInterface::EncoderSink* const sink =
      static_cast<VideoStreamEncoderInterface::EncoderSink*>(vss_impl.get());

  // Populate a test instance of video bitrate allocation.
  VideoBitrateAllocation alloc;
  alloc.SetBitrate(0, 0, 10000);
  alloc.SetBitrate(0, 1, 20000);
  alloc.SetBitrate(1, 0, 30000);
  alloc.SetBitrate(1, 1, 40000);

  // Initial value.
  EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(alloc)).Times(1);
  encoder_queue_->PostTask([&] { sink->OnBitrateAllocationUpdated(alloc); });
  time_controller_.AdvanceTime(TimeDelta::Zero());

  VideoBitrateAllocation updated_alloc = alloc;
  // Needs 10% increase in bitrate to trigger immediate forward.
  const uint32_t base_layer_min_update_bitrate_bps =
      alloc.GetBitrate(0, 0) + alloc.get_sum_bps() / 10;

  // Too small increase, don't forward.
  updated_alloc.SetBitrate(0, 0, base_layer_min_update_bitrate_bps - 1);
  EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(_)).Times(0);
  encoder_queue_->PostTask(
      [&] { sink->OnBitrateAllocationUpdated(updated_alloc); });
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // Large enough increase, do forward.
  updated_alloc.SetBitrate(0, 0, base_layer_min_update_bitrate_bps);
  EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(updated_alloc))
      .Times(1);
  encoder_queue_->PostTask(
      [&] { sink->OnBitrateAllocationUpdated(updated_alloc); });
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // This is now a decrease compared to last forward allocation,
  // forward immediately.
  updated_alloc.SetBitrate(0, 0, base_layer_min_update_bitrate_bps - 1);
  EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(updated_alloc))
      .Times(1);
  encoder_queue_->PostTask(
      [&] { sink->OnBitrateAllocationUpdated(updated_alloc); });
  time_controller_.AdvanceTime(TimeDelta::Zero());

  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, ForwardsVideoBitrateAllocationOnLayerChange) {
  auto vss_impl = CreateVideoSendStreamImpl(
      TestVideoEncoderConfig(VideoEncoderConfig::ContentType::kScreen));

  vss_impl->Start();
  // Unpause encoder, to allows allocations to be passed through.
  const uint32_t kBitrateBps = 100000;
  EXPECT_CALL(rtp_video_sender_, GetPayloadBitrateBps())
      .Times(1)
      .WillOnce(Return(kBitrateBps));
  static_cast<BitrateAllocatorObserver*>(vss_impl.get())
      ->OnBitrateUpdated(CreateAllocation(kBitrateBps));
  VideoStreamEncoderInterface::EncoderSink* const sink =
      static_cast<VideoStreamEncoderInterface::EncoderSink*>(vss_impl.get());

  // Populate a test instance of video bitrate allocation.
  VideoBitrateAllocation alloc;
  alloc.SetBitrate(0, 0, 10000);
  alloc.SetBitrate(0, 1, 20000);
  alloc.SetBitrate(1, 0, 30000);
  alloc.SetBitrate(1, 1, 40000);

  // Initial value.
  EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(alloc)).Times(1);
  sink->OnBitrateAllocationUpdated(alloc);

  // Move some bitrate from one layer to a new one, but keep sum the
  // same. Since layout has changed, immediately trigger forward.
  VideoBitrateAllocation updated_alloc = alloc;
  updated_alloc.SetBitrate(2, 0, 10000);
  updated_alloc.SetBitrate(1, 1, alloc.GetBitrate(1, 1) - 10000);
  EXPECT_EQ(alloc.get_sum_bps(), updated_alloc.get_sum_bps());
  EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(updated_alloc))
      .Times(1);
  encoder_queue_->PostTask(
      [&] { sink->OnBitrateAllocationUpdated(updated_alloc); });
  time_controller_.AdvanceTime(TimeDelta::Zero());

  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, ForwardsVideoBitrateAllocationAfterTimeout) {
  auto vss_impl = CreateVideoSendStreamImpl(
      TestVideoEncoderConfig(VideoEncoderConfig::ContentType::kScreen));
  vss_impl->Start();
  const uint32_t kBitrateBps = 100000;
  // Unpause encoder, to allows allocations to be passed through.
  EXPECT_CALL(rtp_video_sender_, GetPayloadBitrateBps())
      .Times(1)
      .WillRepeatedly(Return(kBitrateBps));
  static_cast<BitrateAllocatorObserver*>(vss_impl.get())
      ->OnBitrateUpdated(CreateAllocation(kBitrateBps));
  VideoStreamEncoderInterface::EncoderSink* const sink =
      static_cast<VideoStreamEncoderInterface::EncoderSink*>(vss_impl.get());

  // Populate a test instance of video bitrate allocation.
  VideoBitrateAllocation alloc;

  alloc.SetBitrate(0, 0, 10000);
  alloc.SetBitrate(0, 1, 20000);
  alloc.SetBitrate(1, 0, 30000);
  alloc.SetBitrate(1, 1, 40000);

  EncodedImage encoded_image;
  CodecSpecificInfo codec_specific;
  EXPECT_CALL(rtp_video_sender_, OnEncodedImage)
      .WillRepeatedly(Return(
          EncodedImageCallback::Result(EncodedImageCallback::Result::OK)));
  // Max time we will throttle similar video bitrate allocations.
  static constexpr int64_t kMaxVbaThrottleTimeMs = 500;

  {
    // Initial value.
    EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(alloc)).Times(1);
    encoder_queue_->PostTask([&] { sink->OnBitrateAllocationUpdated(alloc); });
    time_controller_.AdvanceTime(TimeDelta::Zero());
  }

  {
    EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(alloc)).Times(0);
    encoder_queue_->PostTask([&] {
      // Sending same allocation again, this one should be throttled.
      sink->OnBitrateAllocationUpdated(alloc);
    });
    time_controller_.AdvanceTime(TimeDelta::Zero());
  }

  time_controller_.AdvanceTime(TimeDelta::Millis(kMaxVbaThrottleTimeMs));
  {
    EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(alloc)).Times(1);
    encoder_queue_->PostTask([&] {
      // Sending similar allocation again after timeout, should
      // forward.
      sink->OnBitrateAllocationUpdated(alloc);
    });
    time_controller_.AdvanceTime(TimeDelta::Zero());
  }

  {
    EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(alloc)).Times(0);
    encoder_queue_->PostTask([&] {
      // Sending similar allocation again without timeout, throttle.
      sink->OnBitrateAllocationUpdated(alloc);
    });
    time_controller_.AdvanceTime(TimeDelta::Zero());
  }

  {
    EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(alloc)).Times(0);
    encoder_queue_->PostTask([&] {
      // Send encoded image, should be a noop.
      static_cast<EncodedImageCallback*>(vss_impl.get())
          ->OnEncodedImage(encoded_image, &codec_specific);
    });
    time_controller_.AdvanceTime(TimeDelta::Zero());
  }

  {
    // Advance time and send encoded image, this should wake up and
    // send cached bitrate allocation.
    time_controller_.AdvanceTime(TimeDelta::Millis(kMaxVbaThrottleTimeMs));

    EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(alloc)).Times(1);
    encoder_queue_->PostTask([&] {
      static_cast<EncodedImageCallback*>(vss_impl.get())
          ->OnEncodedImage(encoded_image, &codec_specific);
    });
    time_controller_.AdvanceTime(TimeDelta::Zero());
  }

  {
    // Advance time and send encoded image, there should be no
    // cached allocation to send.
    time_controller_.AdvanceTime(TimeDelta::Millis(kMaxVbaThrottleTimeMs));
    EXPECT_CALL(rtp_video_sender_, OnBitrateAllocationUpdated(alloc)).Times(0);
    encoder_queue_->PostTask([&] {
      static_cast<EncodedImageCallback*>(vss_impl.get())
          ->OnEncodedImage(encoded_image, &codec_specific);
    });
    time_controller_.AdvanceTime(TimeDelta::Zero());
  }

  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, PriorityBitrateConfigInactiveByDefault) {
  auto vss_impl = CreateVideoSendStreamImpl(TestVideoEncoderConfig());
  EXPECT_CALL(
      bitrate_allocator_,
      AddObserver(
          vss_impl.get(),
          Field(&MediaStreamAllocationConfig::priority_bitrate_bps, 0)));
  vss_impl->Start();
  EXPECT_CALL(bitrate_allocator_, RemoveObserver(vss_impl.get())).Times(1);
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, PriorityBitrateConfigAffectsAV1) {
  test::ScopedFieldTrials override_priority_bitrate(
      "WebRTC-AV1-OverridePriorityBitrate/bitrate:20000/");
  config_.rtp.payload_name = "AV1";
  auto vss_impl = CreateVideoSendStreamImpl(TestVideoEncoderConfig());
  EXPECT_CALL(
      bitrate_allocator_,
      AddObserver(
          vss_impl.get(),
          Field(&MediaStreamAllocationConfig::priority_bitrate_bps, 20000)));
  vss_impl->Start();
  EXPECT_CALL(bitrate_allocator_, RemoveObserver(vss_impl.get())).Times(1);
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest,
       PriorityBitrateConfigSurvivesConfigurationChange) {
  VideoStream qvga_stream;
  qvga_stream.width = 320;
  qvga_stream.height = 180;
  qvga_stream.max_framerate = 30;
  qvga_stream.min_bitrate_bps = 30000;
  qvga_stream.target_bitrate_bps = 150000;
  qvga_stream.max_bitrate_bps = 200000;
  qvga_stream.max_qp = 56;
  qvga_stream.bitrate_priority = 1;

  int min_transmit_bitrate_bps = 30000;

  test::ScopedFieldTrials override_priority_bitrate(
      "WebRTC-AV1-OverridePriorityBitrate/bitrate:20000/");
  config_.rtp.payload_name = "AV1";
  auto vss_impl = CreateVideoSendStreamImpl(TestVideoEncoderConfig());
  EXPECT_CALL(
      bitrate_allocator_,
      AddObserver(
          vss_impl.get(),
          Field(&MediaStreamAllocationConfig::priority_bitrate_bps, 20000)))
      .Times(2);
  vss_impl->Start();

  encoder_queue_->PostTask([&] {
    static_cast<VideoStreamEncoderInterface::EncoderSink*>(vss_impl.get())
        ->OnEncoderConfigurationChanged(
            std::vector<VideoStream>{qvga_stream}, false,
            VideoEncoderConfig::ContentType::kRealtimeVideo,
            min_transmit_bitrate_bps);
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());

  EXPECT_CALL(bitrate_allocator_, RemoveObserver(vss_impl.get())).Times(1);
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, CallsVideoStreamEncoderOnBitrateUpdate) {
  const bool kSuspend = false;
  config_.suspend_below_min_bitrate = kSuspend;
  config_.rtp.extensions.emplace_back(RtpExtension::kTransportSequenceNumberUri,
                                      1);
  auto vss_impl = CreateVideoSendStreamImpl(TestVideoEncoderConfig());

  vss_impl->Start();
  VideoStream qvga_stream;
  qvga_stream.width = 320;
  qvga_stream.height = 180;
  qvga_stream.max_framerate = 30;
  qvga_stream.min_bitrate_bps = 30000;
  qvga_stream.target_bitrate_bps = 150000;
  qvga_stream.max_bitrate_bps = 200000;
  qvga_stream.max_qp = 56;
  qvga_stream.bitrate_priority = 1;

  int min_transmit_bitrate_bps = 30000;

  config_.rtp.ssrcs.emplace_back(1);

  encoder_queue_->PostTask([&] {
    static_cast<VideoStreamEncoderInterface::EncoderSink*>(vss_impl.get())
        ->OnEncoderConfigurationChanged(
            std::vector<VideoStream>{qvga_stream}, false,
            VideoEncoderConfig::ContentType::kRealtimeVideo,
            min_transmit_bitrate_bps);
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());

  const DataRate network_constrained_rate =
      DataRate::BitsPerSec(qvga_stream.target_bitrate_bps);
  BitrateAllocationUpdate update;
  update.target_bitrate = network_constrained_rate;
  update.stable_target_bitrate = network_constrained_rate;
  update.round_trip_time = TimeDelta::Millis(1);
  EXPECT_CALL(rtp_video_sender_, OnBitrateUpdated(update, _));
  EXPECT_CALL(rtp_video_sender_, GetPayloadBitrateBps())
      .WillOnce(Return(network_constrained_rate.bps()));
  EXPECT_CALL(
      *video_stream_encoder_,
      OnBitrateUpdated(network_constrained_rate, network_constrained_rate,
                       network_constrained_rate, 0, _, 0));
  static_cast<BitrateAllocatorObserver*>(vss_impl.get())
      ->OnBitrateUpdated(update);

  // Test allocation where the link allocation is larger than the
  // target, meaning we have some headroom on the link.
  const DataRate qvga_max_bitrate =
      DataRate::BitsPerSec(qvga_stream.max_bitrate_bps);
  const DataRate headroom = DataRate::BitsPerSec(50000);
  const DataRate rate_with_headroom = qvga_max_bitrate + headroom;
  update.target_bitrate = rate_with_headroom;
  update.stable_target_bitrate = rate_with_headroom;
  EXPECT_CALL(rtp_video_sender_, OnBitrateUpdated(update, _));
  EXPECT_CALL(rtp_video_sender_, GetPayloadBitrateBps())
      .WillOnce(Return(rate_with_headroom.bps()));
  EXPECT_CALL(*video_stream_encoder_,
              OnBitrateUpdated(qvga_max_bitrate, qvga_max_bitrate,
                               rate_with_headroom, 0, _, 0));
  static_cast<BitrateAllocatorObserver*>(vss_impl.get())
      ->OnBitrateUpdated(update);

  // Add protection bitrate to the mix, this should be subtracted
  // from the headroom.
  const uint32_t protection_bitrate_bps = 10000;
  EXPECT_CALL(rtp_video_sender_, GetProtectionBitrateBps())
      .WillOnce(Return(protection_bitrate_bps));

  EXPECT_CALL(rtp_video_sender_, OnBitrateUpdated(update, _));
  EXPECT_CALL(rtp_video_sender_, GetPayloadBitrateBps())
      .WillOnce(Return(rate_with_headroom.bps()));
  const DataRate headroom_minus_protection =
      rate_with_headroom - DataRate::BitsPerSec(protection_bitrate_bps);
  EXPECT_CALL(*video_stream_encoder_,
              OnBitrateUpdated(qvga_max_bitrate, qvga_max_bitrate,
                               headroom_minus_protection, 0, _, 0));
  static_cast<BitrateAllocatorObserver*>(vss_impl.get())
      ->OnBitrateUpdated(update);

  // Protection bitrate exceeds head room, link allocation should be
  // capped to target bitrate.
  EXPECT_CALL(rtp_video_sender_, GetProtectionBitrateBps())
      .WillOnce(Return(headroom.bps() + 1000));
  EXPECT_CALL(rtp_video_sender_, OnBitrateUpdated(update, _));
  EXPECT_CALL(rtp_video_sender_, GetPayloadBitrateBps())
      .WillOnce(Return(rate_with_headroom.bps()));
  EXPECT_CALL(*video_stream_encoder_,
              OnBitrateUpdated(qvga_max_bitrate, qvga_max_bitrate,
                               qvga_max_bitrate, 0, _, 0));
  static_cast<BitrateAllocatorObserver*>(vss_impl.get())
      ->OnBitrateUpdated(update);

  // Set rates to zero on stop.
  EXPECT_CALL(*video_stream_encoder_,
              OnBitrateUpdated(DataRate::Zero(), DataRate::Zero(),
                               DataRate::Zero(), 0, 0, 0));
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, DisablesPaddingOnPausedEncoder) {
  int padding_bitrate = 0;
  auto vss_impl = CreateVideoSendStreamImpl(TestVideoEncoderConfig());

  // Capture padding bitrate for testing.
  EXPECT_CALL(bitrate_allocator_, AddObserver(vss_impl.get(), _))
      .WillRepeatedly(Invoke(
          [&](BitrateAllocatorObserver*, MediaStreamAllocationConfig config) {
            padding_bitrate = config.pad_up_bitrate_bps;
          }));
  // If observer is removed, no padding will be sent.
  EXPECT_CALL(bitrate_allocator_, RemoveObserver(vss_impl.get()))
      .WillRepeatedly(
          Invoke([&](BitrateAllocatorObserver*) { padding_bitrate = 0; }));

  EXPECT_CALL(rtp_video_sender_, OnEncodedImage)
      .WillRepeatedly(Return(
          EncodedImageCallback::Result(EncodedImageCallback::Result::OK)));
  const bool kSuspend = false;
  config_.suspend_below_min_bitrate = kSuspend;
  config_.rtp.extensions.emplace_back(RtpExtension::kTransportSequenceNumberUri,
                                      1);
  VideoStream qvga_stream;
  qvga_stream.width = 320;
  qvga_stream.height = 180;
  qvga_stream.max_framerate = 30;
  qvga_stream.min_bitrate_bps = 30000;
  qvga_stream.target_bitrate_bps = 150000;
  qvga_stream.max_bitrate_bps = 200000;
  qvga_stream.max_qp = 56;
  qvga_stream.bitrate_priority = 1;

  int min_transmit_bitrate_bps = 30000;

  config_.rtp.ssrcs.emplace_back(1);
  vss_impl->Start();
  // Starts without padding.
  EXPECT_EQ(0, padding_bitrate);
  encoder_queue_->PostTask([&] {
    // Reconfigure e.g. due to a fake frame.
    static_cast<VideoStreamEncoderInterface::EncoderSink*>(vss_impl.get())
        ->OnEncoderConfigurationChanged(
            std::vector<VideoStream>{qvga_stream}, false,
            VideoEncoderConfig::ContentType::kRealtimeVideo,
            min_transmit_bitrate_bps);
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());
  // Still no padding because no actual frames were passed, only
  // reconfiguration happened.
  EXPECT_EQ(0, padding_bitrate);

  // Unpause encoder.
  const uint32_t kBitrateBps = 100000;
  EXPECT_CALL(rtp_video_sender_, GetPayloadBitrateBps())
      .Times(1)
      .WillOnce(Return(kBitrateBps));
  static_cast<BitrateAllocatorObserver*>(vss_impl.get())
      ->OnBitrateUpdated(CreateAllocation(kBitrateBps));

  encoder_queue_->PostTask([&] {
    // A frame is encoded.
    EncodedImage encoded_image;
    CodecSpecificInfo codec_specific;
    static_cast<EncodedImageCallback*>(vss_impl.get())
        ->OnEncodedImage(encoded_image, &codec_specific);
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());
  // Only after actual frame is encoded are we enabling the padding.
  EXPECT_GT(padding_bitrate, 0);

  time_controller_.AdvanceTime(TimeDelta::Seconds(5));
  // Since no more frames are sent the last 5s, no padding is supposed to be
  // sent.
  EXPECT_EQ(0, padding_bitrate);
  testing::Mock::VerifyAndClearExpectations(&bitrate_allocator_);
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, KeepAliveOnDroppedFrame) {
  auto vss_impl = CreateVideoSendStreamImpl(TestVideoEncoderConfig());
  EXPECT_CALL(bitrate_allocator_, RemoveObserver(vss_impl.get())).Times(0);
  vss_impl->Start();
  const uint32_t kBitrateBps = 100000;
  EXPECT_CALL(rtp_video_sender_, GetPayloadBitrateBps())
      .Times(1)
      .WillOnce(Return(kBitrateBps));
  static_cast<BitrateAllocatorObserver*>(vss_impl.get())
      ->OnBitrateUpdated(CreateAllocation(kBitrateBps));
  encoder_queue_->PostTask([&] {
    // Keep the stream from deallocating by dropping a frame.
    static_cast<EncodedImageCallback*>(vss_impl.get())
        ->OnDroppedFrame(EncodedImageCallback::DropReason::kDroppedByEncoder);
  });
  time_controller_.AdvanceTime(TimeDelta::Seconds(2));
  testing::Mock::VerifyAndClearExpectations(&bitrate_allocator_);
  vss_impl->Stop();
}

TEST_F(VideoSendStreamImplTest, ConfiguresBitratesForSvc) {
  struct TestConfig {
    bool screenshare = false;
    bool alr = false;
    int min_padding_bitrate_bps = 0;
  };

  std::vector<TestConfig> test_variants;
  for (bool screenshare : {false, true}) {
    for (bool alr : {false, true}) {
      for (int min_padding : {0, 400000}) {
        test_variants.push_back({screenshare, alr, min_padding});
      }
    }
  }

  for (const TestConfig& test_config : test_variants) {
    const bool kSuspend = false;
    config_.suspend_below_min_bitrate = kSuspend;
    config_.rtp.extensions.emplace_back(
        RtpExtension::kTransportSequenceNumberUri, 1);
    config_.periodic_alr_bandwidth_probing = test_config.alr;
    auto vss_impl = CreateVideoSendStreamImpl(TestVideoEncoderConfig(
        test_config.screenshare
            ? VideoEncoderConfig::ContentType::kScreen
            : VideoEncoderConfig::ContentType::kRealtimeVideo));

    vss_impl->Start();

    // Svc
    VideoStream stream;
    stream.width = 1920;
    stream.height = 1080;
    stream.max_framerate = 30;
    stream.min_bitrate_bps = 60000;
    stream.target_bitrate_bps = 6000000;
    stream.max_bitrate_bps = 1250000;
    stream.num_temporal_layers = 2;
    stream.max_qp = 56;
    stream.bitrate_priority = 1;

    config_.rtp.ssrcs.emplace_back(1);
    config_.rtp.ssrcs.emplace_back(2);

    EXPECT_CALL(
        bitrate_allocator_,
        AddObserver(
            vss_impl.get(),
            AllOf(Field(&MediaStreamAllocationConfig::min_bitrate_bps,
                        static_cast<uint32_t>(stream.min_bitrate_bps)),
                  Field(&MediaStreamAllocationConfig::max_bitrate_bps,
                        static_cast<uint32_t>(stream.max_bitrate_bps)),
                  // Stream not yet active - no padding.
                  Field(&MediaStreamAllocationConfig::pad_up_bitrate_bps, 0u),
                  Field(&MediaStreamAllocationConfig::enforce_min_bitrate,
                        !kSuspend))));
    encoder_queue_->PostTask([&] {
      static_cast<VideoStreamEncoderInterface::EncoderSink*>(vss_impl.get())
          ->OnEncoderConfigurationChanged(
              std::vector<VideoStream>{stream}, true,
              test_config.screenshare
                  ? VideoEncoderConfig::ContentType::kScreen
                  : VideoEncoderConfig::ContentType::kRealtimeVideo,
              test_config.min_padding_bitrate_bps);
    });
    time_controller_.AdvanceTime(TimeDelta::Zero());
    ::testing::Mock::VerifyAndClearExpectations(&bitrate_allocator_);

    // Simulate an encoded image, this will turn the stream active and
    // enable padding.
    EXPECT_CALL(rtp_video_sender_, OnEncodedImage)
        .WillRepeatedly(Return(
            EncodedImageCallback::Result(EncodedImageCallback::Result::OK)));
    // Screensharing implicitly forces ALR.
    const bool using_alr = test_config.alr || test_config.screenshare;
    // If ALR is used, pads only to min bitrate as rampup is handled by
    // probing. Otherwise target_bitrate contains the padding target.
    int expected_padding =
        using_alr ? stream.min_bitrate_bps
                  : static_cast<int>(stream.target_bitrate_bps *
                                     (test_config.screenshare ? 1.35 : 1.2));
    // Min padding bitrate may override padding target.
    expected_padding =
        std::max(expected_padding, test_config.min_padding_bitrate_bps);
    EXPECT_CALL(
        bitrate_allocator_,
        AddObserver(
            vss_impl.get(),
            AllOf(Field(&MediaStreamAllocationConfig::min_bitrate_bps,
                        static_cast<uint32_t>(stream.min_bitrate_bps)),
                  Field(&MediaStreamAllocationConfig::max_bitrate_bps,
                        static_cast<uint32_t>(stream.max_bitrate_bps)),
                  // Stream now active - min bitrate use as padding target
                  // when ALR is active.
                  Field(&MediaStreamAllocationConfig::pad_up_bitrate_bps,
                        expected_padding),
                  Field(&MediaStreamAllocationConfig::enforce_min_bitrate,
                        !kSuspend))));
    encoder_queue_->PostTask([&] {
      EncodedImage encoded_image;
      CodecSpecificInfo codec_specific;

      static_cast<EncodedImageCallback*>(vss_impl.get())
          ->OnEncodedImage(encoded_image, &codec_specific);
    });
    time_controller_.AdvanceTime(TimeDelta::Zero());
    Mock::VerifyAndClearExpectations(&bitrate_allocator_);

    vss_impl->Stop();
  }
}
}  // namespace internal
}  // namespace webrtc
