/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>

#include "api/test/simulated_network.h"
#include "api/test/video/function_video_encoder_factory.h"
#include "call/fake_network_pipe.h"
#include "call/simulated_network.h"
#include "modules/include/module_common_types_public.h"
#include "modules/rtp_rtcp/source/rtp_packet.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "modules/video_coding/codecs/vp9/include/vp9.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/task_queue_for_test.h"
#include "test/call_test.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/video_test_constants.h"

using ::testing::Contains;

namespace webrtc {
namespace {
constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr int kFps = 30;
constexpr int kFramesToObserve = 10;

uint8_t PayloadNameToPayloadType(const std::string& payload_name) {
  if (payload_name == "VP8") {
    return test::VideoTestConstants::kPayloadTypeVP8;
  } else if (payload_name == "VP9") {
    return test::VideoTestConstants::kPayloadTypeVP9;
  } else if (payload_name == "H264") {
    return test::VideoTestConstants::kPayloadTypeH264;
  } else {
    RTC_DCHECK_NOTREACHED();
    return 0;
  }
}

int RemoveOlderOrEqual(uint32_t timestamp, std::vector<uint32_t>* timestamps) {
  int num_removed = 0;
  while (!timestamps->empty()) {
    auto it = timestamps->begin();
    if (IsNewerTimestamp(*it, timestamp))
      break;

    timestamps->erase(it);
    ++num_removed;
  }
  return num_removed;
}

class FrameObserver : public test::RtpRtcpObserver,
                      public rtc::VideoSinkInterface<VideoFrame> {
 public:
  FrameObserver()
      : test::RtpRtcpObserver(test::VideoTestConstants::kDefaultTimeout) {}

  void Reset(uint8_t expected_payload_type) {
    MutexLock lock(&mutex_);
    num_sent_frames_ = 0;
    num_rendered_frames_ = 0;
    expected_payload_type_ = expected_payload_type;
  }

 private:
  // Sends kFramesToObserve.
  Action OnSendRtp(rtc::ArrayView<const uint8_t> packet) override {
    MutexLock lock(&mutex_);

    RtpPacket rtp_packet;
    EXPECT_TRUE(rtp_packet.Parse(packet));
    EXPECT_EQ(rtp_packet.Ssrc(), test::VideoTestConstants::kVideoSendSsrcs[0]);
    if (rtp_packet.payload_size() == 0)
      return SEND_PACKET;  // Skip padding, may be sent after OnFrame is called.

    if (expected_payload_type_ &&
        rtp_packet.PayloadType() != expected_payload_type_.value()) {
      return DROP_PACKET;  // All frames sent.
    }

    if (!last_timestamp_ || rtp_packet.Timestamp() != *last_timestamp_) {
      // New frame.
      // Sent enough frames?
      if (num_sent_frames_ >= kFramesToObserve)
        return DROP_PACKET;

      ++num_sent_frames_;
      sent_timestamps_.push_back(rtp_packet.Timestamp());
    }

    last_timestamp_ = rtp_packet.Timestamp();
    return SEND_PACKET;
  }

  // Verifies that all sent frames are decoded and rendered.
  void OnFrame(const VideoFrame& rendered_frame) override {
    MutexLock lock(&mutex_);
    EXPECT_THAT(sent_timestamps_, Contains(rendered_frame.rtp_timestamp()));

    // Remove old timestamps too, only the newest decoded frame is rendered.
    num_rendered_frames_ +=
        RemoveOlderOrEqual(rendered_frame.rtp_timestamp(), &sent_timestamps_);

    if (num_rendered_frames_ >= kFramesToObserve) {
      EXPECT_TRUE(sent_timestamps_.empty()) << "All sent frames not decoded.";
      observation_complete_.Set();
    }
  }

  Mutex mutex_;
  absl::optional<uint32_t> last_timestamp_;  // Only accessed from pacer thread.
  absl::optional<uint8_t> expected_payload_type_ RTC_GUARDED_BY(mutex_);
  int num_sent_frames_ RTC_GUARDED_BY(mutex_) = 0;
  int num_rendered_frames_ RTC_GUARDED_BY(mutex_) = 0;
  std::vector<uint32_t> sent_timestamps_ RTC_GUARDED_BY(mutex_);
};
}  // namespace

class MultiCodecReceiveTest : public test::CallTest {
 public:
  MultiCodecReceiveTest() {
    SendTask(task_queue(), [this]() {
      CreateCalls();
      CreateSendTransport(BuiltInNetworkBehaviorConfig(), &observer_);
      CreateReceiveTransport(BuiltInNetworkBehaviorConfig(), &observer_);
    });
  }

  virtual ~MultiCodecReceiveTest() {
    SendTask(task_queue(), [this]() {
      send_transport_.reset();
      receive_transport_.reset();
      DestroyCalls();
    });
  }

  struct CodecConfig {
    std::string payload_name;
    size_t num_temporal_layers;
  };

  void ConfigureEncoder(const CodecConfig& config,
                        VideoEncoderFactory* encoder_factory);
  void ConfigureDecoders(const std::vector<CodecConfig>& configs,
                         VideoDecoderFactory* decoder_factory);
  void RunTestWithCodecs(const std::vector<CodecConfig>& configs);

 private:
  FrameObserver observer_;
};

void MultiCodecReceiveTest::ConfigureDecoders(
    const std::vector<CodecConfig>& configs,
    VideoDecoderFactory* decoder_factory) {
  video_receive_configs_[0].decoders.clear();
  // Placing the payload names in a std::set retains the unique names only.
  video_receive_configs_[0].decoder_factory = decoder_factory;
  std::set<std::string> unique_payload_names;
  for (const auto& config : configs)
    if (unique_payload_names.insert(config.payload_name).second) {
      VideoReceiveStreamInterface::Decoder decoder =
          test::CreateMatchingDecoder(
              PayloadNameToPayloadType(config.payload_name),
              config.payload_name);

      video_receive_configs_[0].decoders.push_back(decoder);
    }
}

void MultiCodecReceiveTest::ConfigureEncoder(
    const CodecConfig& config,
    VideoEncoderFactory* encoder_factory) {
  GetVideoSendConfig()->encoder_settings.encoder_factory = encoder_factory;
  GetVideoSendConfig()->rtp.payload_name = config.payload_name;
  GetVideoSendConfig()->rtp.payload_type =
      PayloadNameToPayloadType(config.payload_name);
  GetVideoEncoderConfig()->codec_type =
      PayloadStringToCodecType(config.payload_name);
  EXPECT_EQ(1u, GetVideoEncoderConfig()->simulcast_layers.size());
  GetVideoEncoderConfig()->simulcast_layers[0].num_temporal_layers =
      config.num_temporal_layers;
  GetVideoEncoderConfig()->video_format.name = config.payload_name;
}

void MultiCodecReceiveTest::RunTestWithCodecs(
    const std::vector<CodecConfig>& configs) {
  EXPECT_TRUE(!configs.empty());

  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env,
         const SdpVideoFormat& format) -> std::unique_ptr<VideoEncoder> {
        if (format.name == "VP8") {
          return CreateVp8Encoder(env);
        }
        if (format.name == "VP9") {
          return CreateVp9Encoder(env);
        }
        if (format.name == "H264") {
          return CreateH264Encoder(env);
        }
        RTC_DCHECK_NOTREACHED() << format.name;
        return nullptr;
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      [](const Environment& env,
         const SdpVideoFormat& format) -> std::unique_ptr<VideoDecoder> {
        if (format.name == "VP8") {
          return CreateVp8Decoder(env);
        }
        if (format.name == "VP9") {
          return VP9Decoder::Create();
        }
        if (format.name == "H264") {
          return H264Decoder::Create();
        }
        RTC_DCHECK_NOTREACHED() << format.name;
        return nullptr;
      });
  // Create and start call.
  SendTask(task_queue(),
           [this, &configs, &encoder_factory, &decoder_factory]() {
             CreateSendConfig(1, 0, 0);
             ConfigureEncoder(configs[0], &encoder_factory);
             CreateMatchingReceiveConfigs();
             video_receive_configs_[0].renderer = &observer_;
             // Disable to avoid post-decode frame dropping in
             // VideoRenderFrames.
             video_receive_configs_[0].enable_prerenderer_smoothing = false;
             ConfigureDecoders(configs, &decoder_factory);
             CreateVideoStreams();
             CreateFrameGeneratorCapturer(kFps, kWidth, kHeight);
             Start();
           });
  EXPECT_TRUE(observer_.Wait()) << "Timed out waiting for frames.";

  for (size_t i = 1; i < configs.size(); ++i) {
    // Recreate VideoSendStream with new config (codec, temporal layers).
    SendTask(task_queue(), [this, i, &configs, &encoder_factory]() {
      DestroyVideoSendStreams();
      observer_.Reset(PayloadNameToPayloadType(configs[i].payload_name));

      ConfigureEncoder(configs[i], &encoder_factory);
      CreateVideoSendStreams();
      GetVideoSendStream()->Start();
      CreateFrameGeneratorCapturer(kFps, kWidth / 2, kHeight / 2);
      ConnectVideoSourcesToStreams();
      StartVideoSources();
    });
    EXPECT_TRUE(observer_.Wait()) << "Timed out waiting for frames.";
  }

  SendTask(task_queue(), [this]() {
    Stop();
    DestroyStreams();
  });
}

TEST_F(MultiCodecReceiveTest, SingleStreamReceivesVp8Vp9) {
  RunTestWithCodecs({{"VP8", 1}, {"VP9", 1}, {"VP8", 1}});
}

TEST_F(MultiCodecReceiveTest, SingleStreamReceivesVp8Vp9WithTl) {
  RunTestWithCodecs({{"VP8", 2}, {"VP9", 2}, {"VP8", 2}});
}

#if defined(WEBRTC_USE_H264)
TEST_F(MultiCodecReceiveTest, SingleStreamReceivesVp8H264) {
  RunTestWithCodecs({{"VP8", 1}, {"H264", 1}, {"VP8", 1}});
}

TEST_F(MultiCodecReceiveTest, SingleStreamReceivesVp8H264WithTl) {
  RunTestWithCodecs({{"VP8", 3}, {"H264", 1}, {"VP8", 3}});
}

TEST_F(MultiCodecReceiveTest, SingleStreamReceivesVp8Vp9H264) {
  RunTestWithCodecs({{"VP8", 1}, {"VP9", 1}, {"H264", 1}, {"VP9", 1}});
}

TEST_F(MultiCodecReceiveTest, SingleStreamReceivesVp8Vp9H264WithTl) {
  RunTestWithCodecs({{"VP8", 3}, {"VP9", 2}, {"H264", 1}, {"VP9", 3}});
}
#endif  // defined(WEBRTC_USE_H264)

}  // namespace webrtc
