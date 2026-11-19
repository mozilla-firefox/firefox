/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/test/create_frame_generator.h"
#include "api/test/simulated_network.h"
#include "api/test/video/function_video_decoder_factory.h"
#include "api/video/video_codec_type.h"
#include "api/video_codecs/sdp_video_format.h"
#include "call/call.h"
#include "call/video_receive_stream.h"
#include "call/video_send_stream.h"
#include "rtc_base/checks.h"
#include "rtc_base/event.h"
#include "rtc_base/logging.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/thread_annotations.h"
#include "test/call_test.h"
#include "test/encoder_settings.h"
#include "test/fake_decoder.h"
#include "test/frame_generator_capturer.h"
#include "test/gtest.h"
#include "test/video_test_constants.h"
#include "video/config/video_encoder_config.h"

namespace webrtc {
namespace {
// Note: If you consider to re-use this class, think twice and instead consider
// writing tests that don't depend on the logging system.
class LogObserver {
 public:
  LogObserver() { LogMessage::AddLogToStream(&callback_, LS_INFO); }

  ~LogObserver() { LogMessage::RemoveLogToStream(&callback_); }

  void PushExpectedLogLine(absl::string_view expected_log_line) {
    callback_.PushExpectedLogLine(expected_log_line);
  }

  bool Wait() { return callback_.Wait(); }

 private:
  class Callback : public LogSink {
   public:
    void OnLogMessage(const LogLineRef& line) override {
      absl::string_view message = line.message();
      MutexLock lock(&mutex_);
      // Ignore log lines that are due to missing AST extensions, these are
      // logged when we switch back from AST to TOF until the wrapping bitrate
      // estimator gives up on using AST.
      if (absl::StrContains(message, "BitrateEstimator") &&
          !absl::StrContains(message, "packet is missing")) {
        received_log_lines_.push_back(std::string(message));
      }

      int num_popped = 0;
      while (!received_log_lines_.empty() && !expected_log_lines_.empty()) {
        std::string a = received_log_lines_.front();
        std::string b = expected_log_lines_.front();
        received_log_lines_.pop_front();
        expected_log_lines_.pop_front();
        num_popped++;
        EXPECT_TRUE(a.find(b) != absl::string_view::npos) << a << " != " << b;
      }
      if (expected_log_lines_.empty()) {
        if (num_popped > 0) {
          done_.Set();
        }
        return;
      }
    }

    bool Wait() {
      return done_.Wait(test::VideoTestConstants::kDefaultTimeout);
    }

    void PushExpectedLogLine(absl::string_view expected_log_line) {
      MutexLock lock(&mutex_);
      expected_log_lines_.emplace_back(expected_log_line);
    }

   private:
    typedef std::list<std::string> Strings;
    Mutex mutex_;
    Strings received_log_lines_ RTC_GUARDED_BY(mutex_);
    Strings expected_log_lines_ RTC_GUARDED_BY(mutex_);
    Event done_;
  };

  Callback callback_;
};

constexpr RtpHeaderExtensionId kTOFExtensionId(4);
constexpr RtpHeaderExtensionId kASTExtensionId(5);

class BitrateEstimatorTest : public test::CallTest {
 public:
  BitrateEstimatorTest() : receive_config_(nullptr) {}

  ~BitrateEstimatorTest() override { EXPECT_TRUE(streams_.empty()); }

  void SetUp() override {
    SendTask(task_queue(), [this]() {
      RegisterRtpExtension(
          RtpExtension(RtpExtension::kTimestampOffsetUri, kTOFExtensionId));
      RegisterRtpExtension(
          RtpExtension(RtpExtension::kAbsSendTimeUri, kASTExtensionId));

      CreateCalls();

      CreateSendTransport(BuiltInNetworkBehaviorConfig(), /*observer=*/nullptr);
      CreateReceiveTransport(BuiltInNetworkBehaviorConfig(),
                             /*observer=*/nullptr);

      VideoSendStream::Config video_send_config(send_transport_.get());
      video_send_config.rtp.ssrcs.push_back(
          test::VideoTestConstants::kVideoSendSsrcs[0]);
      video_send_config.encoder_settings.encoder_factory =
          &fake_encoder_factory_;
      video_send_config.encoder_settings.bitrate_allocator_factory =
          bitrate_allocator_factory_.get();
      video_send_config.rtp.payload_name = "FAKE";
      video_send_config.rtp.payload_type =
          test::VideoTestConstants::kFakeVideoSendPayloadType;
      SetVideoSendConfig(video_send_config);
      VideoEncoderConfig video_encoder_config;
      test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
      SetVideoEncoderConfig(video_encoder_config);

      receive_config_ =
          VideoReceiveStreamInterface::Config(receive_transport_.get());
      // receive_config_.decoders will be set by every stream separately.
      receive_config_.rtp.remote_ssrc = GetVideoSendConfig()->rtp.ssrcs[0];
    });
  }

  void TearDown() override {
    SendTask(task_queue(), [this]() {
      for (auto* stream : streams_) {
        stream->StopSending();
        delete stream;
      }
      streams_.clear();
      DestroyCalls();
    });
  }

 protected:
  friend class Stream;

  class Stream {
   public:
    explicit Stream(BitrateEstimatorTest* test)
        : test_(test),
          is_sending_receiving_(false),
          send_stream_(nullptr),
          frame_generator_capturer_(),
          decoder_factory_(
              []() { return std::make_unique<test::FakeDecoder>(); }) {
      test_->GetVideoSendConfig()->rtp.ssrcs[0]++;
      send_stream_ = test_->sender_call_->CreateVideoSendStream(
          test_->GetVideoSendConfig()->Copy(),
          test_->GetVideoEncoderConfig()->Copy());
      RTC_DCHECK_EQ(1, test_->GetVideoEncoderConfig()->number_of_streams);
      frame_generator_capturer_ =
          std::make_unique<test::FrameGeneratorCapturer>(
              &test->env().clock(),
              test::CreateSquareFrameGenerator(
                  test::VideoTestConstants::kDefaultWidth,
                  test::VideoTestConstants::kDefaultHeight, std::nullopt,
                  std::nullopt),
              test::VideoTestConstants::kDefaultFramerate,
              test->env().task_queue_factory());
      frame_generator_capturer_->Init();
      frame_generator_capturer_->Start();
      send_stream_->SetSource(frame_generator_capturer_.get(),
                              DegradationPreference::MAINTAIN_FRAMERATE);
      send_stream_->Start();

      VideoReceiveStreamInterface::Decoder decoder;
      test_->receive_config_.decoder_factory = &decoder_factory_;
      decoder.payload_type = test_->GetVideoSendConfig()->rtp.payload_type;
      decoder.video_format =
          SdpVideoFormat(test_->GetVideoSendConfig()->rtp.payload_name);
      test_->receive_config_.decoders.clear();
      test_->receive_config_.decoders.push_back(decoder);
      test_->receive_config_.rtp.remote_ssrc =
          test_->GetVideoSendConfig()->rtp.ssrcs[0];
      test_->receive_config_.renderer = &test->fake_renderer_;
      video_receive_stream_ = test_->receiver_call_->CreateVideoReceiveStream(
          test_->receive_config_.Copy());
      video_receive_stream_->Start();
      is_sending_receiving_ = true;
    }

    ~Stream() {
      EXPECT_FALSE(is_sending_receiving_);
      test_->sender_call_->DestroyVideoSendStream(send_stream_);
      frame_generator_capturer_.reset(nullptr);
      send_stream_ = nullptr;
      if (video_receive_stream_) {
        test_->receiver_call_->DestroyVideoReceiveStream(video_receive_stream_);
        video_receive_stream_ = nullptr;
      }
    }

    void StopSending() {
      if (is_sending_receiving_) {
        send_stream_->Stop();
        if (video_receive_stream_) {
          video_receive_stream_->Stop();
        }
        is_sending_receiving_ = false;
      }
    }

   private:
    BitrateEstimatorTest* test_;
    bool is_sending_receiving_;
    VideoSendStream* send_stream_;
    VideoReceiveStreamInterface* video_receive_stream_;
    std::unique_ptr<test::FrameGeneratorCapturer> frame_generator_capturer_;

    test::FunctionVideoDecoderFactory decoder_factory_;
  };

  LogObserver receiver_log_;
  VideoReceiveStreamInterface::Config receive_config_;
  std::vector<Stream*> streams_;
};

static const char* kAbsSendTimeLog =
    "RemoteBitrateEstimatorAbsSendTime: Instantiating.";
static const char* kSingleStreamLog =
    "RemoteBitrateEstimatorSingleStream: Instantiating.";

TEST_F(BitrateEstimatorTest, InstantiatesTOFPerDefaultForVideo) {
  SendTask(task_queue(), [this]() {
    GetVideoSendConfig()->rtp.extensions.push_back(
        RtpExtension(RtpExtension::kTimestampOffsetUri, kTOFExtensionId));
    receiver_log_.PushExpectedLogLine(kSingleStreamLog);
    receiver_log_.PushExpectedLogLine(kSingleStreamLog);
    streams_.push_back(new Stream(this));
  });
  EXPECT_TRUE(receiver_log_.Wait());
}

TEST_F(BitrateEstimatorTest, ImmediatelySwitchToASTForVideo) {
  SendTask(task_queue(), [this]() {
    GetVideoSendConfig()->rtp.extensions.push_back(
        RtpExtension(RtpExtension::kAbsSendTimeUri, kASTExtensionId));
    receiver_log_.PushExpectedLogLine(kSingleStreamLog);
    receiver_log_.PushExpectedLogLine(kSingleStreamLog);
    receiver_log_.PushExpectedLogLine("Switching to absolute send time RBE.");
    receiver_log_.PushExpectedLogLine(kAbsSendTimeLog);
    streams_.push_back(new Stream(this));
  });
  EXPECT_TRUE(receiver_log_.Wait());
}

TEST_F(BitrateEstimatorTest, SwitchesToASTForVideo) {
  SendTask(task_queue(), [this]() {
    GetVideoSendConfig()->rtp.extensions.push_back(
        RtpExtension(RtpExtension::kTimestampOffsetUri, kTOFExtensionId));
    receiver_log_.PushExpectedLogLine(kSingleStreamLog);
    receiver_log_.PushExpectedLogLine(kSingleStreamLog);
    streams_.push_back(new Stream(this));
  });
  EXPECT_TRUE(receiver_log_.Wait());

  SendTask(task_queue(), [this]() {
    GetVideoSendConfig()->rtp.extensions[0] =
        RtpExtension(RtpExtension::kAbsSendTimeUri, kASTExtensionId);
    receiver_log_.PushExpectedLogLine("Switching to absolute send time RBE.");
    receiver_log_.PushExpectedLogLine(kAbsSendTimeLog);
    streams_.push_back(new Stream(this));
  });
  EXPECT_TRUE(receiver_log_.Wait());
}

// This test is flaky. See webrtc:5790.
TEST_F(BitrateEstimatorTest, DISABLED_SwitchesToASTThenBackToTOFForVideo) {
  SendTask(task_queue(), [this]() {
    GetVideoSendConfig()->rtp.extensions.push_back(
        RtpExtension(RtpExtension::kTimestampOffsetUri, kTOFExtensionId));
    receiver_log_.PushExpectedLogLine(kSingleStreamLog);
    receiver_log_.PushExpectedLogLine(kAbsSendTimeLog);
    receiver_log_.PushExpectedLogLine(kSingleStreamLog);
    streams_.push_back(new Stream(this));
  });
  EXPECT_TRUE(receiver_log_.Wait());

  SendTask(task_queue(), [this]() {
    GetVideoSendConfig()->rtp.extensions[0] =
        RtpExtension(RtpExtension::kAbsSendTimeUri, kASTExtensionId);
    receiver_log_.PushExpectedLogLine(kAbsSendTimeLog);
    receiver_log_.PushExpectedLogLine("Switching to absolute send time RBE.");
    streams_.push_back(new Stream(this));
  });
  EXPECT_TRUE(receiver_log_.Wait());

  SendTask(task_queue(), [this]() {
    GetVideoSendConfig()->rtp.extensions[0] =
        RtpExtension(RtpExtension::kTimestampOffsetUri, kTOFExtensionId);
    receiver_log_.PushExpectedLogLine(kAbsSendTimeLog);
    receiver_log_.PushExpectedLogLine(
        "WrappingBitrateEstimator: Switching to transmission time offset RBE.");
    streams_.push_back(new Stream(this));
    streams_[0]->StopSending();
    streams_[1]->StopSending();
  });
  EXPECT_TRUE(receiver_log_.Wait());
}
}  // namespace
}  // namespace webrtc
