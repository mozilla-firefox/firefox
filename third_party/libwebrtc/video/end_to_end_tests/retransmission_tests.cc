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
#include "api/task_queue/task_queue_base.h"
#include "api/test/simulated_network.h"
#include "api/test/video/function_video_encoder_factory.h"
#include "api/units/time_delta.h"
#include "call/fake_network_pipe.h"
#include "call/simulated_network.h"
#include "modules/rtp_rtcp/source/rtp_packet.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "rtc_base/event.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/task_queue_for_test.h"
#include "test/call_test.h"
#include "test/field_trial.h"
#include "test/gtest.h"
#include "test/rtcp_packet_parser.h"
#include "test/video_test_constants.h"

namespace webrtc {
namespace {
enum : int {  // The first valid value is 1.
  kVideoRotationExtensionId = 1,
};
}  // namespace

class RetransmissionEndToEndTest : public test::CallTest {
 public:
  RetransmissionEndToEndTest() {
    RegisterRtpExtension(RtpExtension(RtpExtension::kVideoRotationUri,
                                      kVideoRotationExtensionId));
  }

 protected:
  void DecodesRetransmittedFrame(bool enable_rtx, bool enable_red);
  void ReceivesPliAndRecovers(int rtp_history_ms);
};

TEST_F(RetransmissionEndToEndTest, ReceivesAndRetransmitsNack) {
  static const int kNumberOfNacksToObserve = 2;
  static const int kLossBurstSize = 2;
  static const int kPacketsBetweenLossBursts = 9;
  class NackObserver : public test::EndToEndTest {
   public:
    NackObserver()
        : EndToEndTest(test::VideoTestConstants::kLongTimeout),
          sent_rtp_packets_(0),
          packets_left_to_drop_(0),
          nacks_left_(kNumberOfNacksToObserve) {}

   private:
    Action OnSendRtp(rtc::ArrayView<const uint8_t> packet) override {
      MutexLock lock(&mutex_);
      RtpPacket rtp_packet;
      EXPECT_TRUE(rtp_packet.Parse(packet));

      // Never drop retransmitted packets.
      if (dropped_packets_.find(rtp_packet.SequenceNumber()) !=
          dropped_packets_.end()) {
        retransmitted_packets_.insert(rtp_packet.SequenceNumber());
        return SEND_PACKET;
      }

      if (nacks_left_ <= 0 &&
          retransmitted_packets_.size() == dropped_packets_.size()) {
        observation_complete_.Set();
      }

      ++sent_rtp_packets_;

      // Enough NACKs received, stop dropping packets.
      if (nacks_left_ <= 0)
        return SEND_PACKET;

      // Check if it's time for a new loss burst.
      if (sent_rtp_packets_ % kPacketsBetweenLossBursts == 0)
        packets_left_to_drop_ = kLossBurstSize;

      // Never drop padding packets as those won't be retransmitted.
      if (packets_left_to_drop_ > 0 && rtp_packet.padding_size() == 0) {
        --packets_left_to_drop_;
        dropped_packets_.insert(rtp_packet.SequenceNumber());
        return DROP_PACKET;
      }

      return SEND_PACKET;
    }

    Action OnReceiveRtcp(rtc::ArrayView<const uint8_t> packet) override {
      MutexLock lock(&mutex_);
      test::RtcpPacketParser parser;
      EXPECT_TRUE(parser.Parse(packet));
      nacks_left_ -= parser.nack()->num_packets();
      return SEND_PACKET;
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

    void PerformTest() override {
      EXPECT_TRUE(Wait())
          << "Timed out waiting for packets to be NACKed, retransmitted and "
             "rendered.";
    }

    Mutex mutex_;
    std::set<uint16_t> dropped_packets_;
    std::set<uint16_t> retransmitted_packets_;
    uint64_t sent_rtp_packets_;
    int packets_left_to_drop_;
    int nacks_left_ RTC_GUARDED_BY(&mutex_);
  } test;

  RunBaseTest(&test);
}

TEST_F(RetransmissionEndToEndTest, ReceivesNackAndRetransmitsAudio) {
  class NackObserver : public test::EndToEndTest {
   public:
    NackObserver()
        : EndToEndTest(test::VideoTestConstants::kLongTimeout),
          local_ssrc_(0),
          remote_ssrc_(0),
          receive_transport_(nullptr) {}

   private:
    size_t GetNumVideoStreams() const override { return 0; }
    size_t GetNumAudioStreams() const override { return 1; }

    Action OnSendRtp(rtc::ArrayView<const uint8_t> packet) override {
      RtpPacket rtp_packet;
      EXPECT_TRUE(rtp_packet.Parse(packet));

      if (!sequence_number_to_retransmit_) {
        sequence_number_to_retransmit_ = rtp_packet.SequenceNumber();
        return DROP_PACKET;

        // Don't ask for retransmission straight away, may be deduped in pacer.
      } else if (rtp_packet.SequenceNumber() ==
                 *sequence_number_to_retransmit_) {
        observation_complete_.Set();
      } else {
        // Send a NACK as often as necessary until retransmission is received.
        rtcp::Nack nack;
        nack.SetSenderSsrc(local_ssrc_);
        nack.SetMediaSsrc(remote_ssrc_);
        uint16_t nack_list[] = {*sequence_number_to_retransmit_};
        nack.SetPacketIds(nack_list, 1);
        rtc::Buffer buffer = nack.Build();

        EXPECT_TRUE(receive_transport_->SendRtcp(buffer));
      }

      return SEND_PACKET;
    }

    void ModifyAudioConfigs(AudioSendStream::Config* send_config,
                            std::vector<AudioReceiveStreamInterface::Config>*
                                receive_configs) override {
      (*receive_configs)[0].rtp.nack.rtp_history_ms =
          test::VideoTestConstants::kNackRtpHistoryMs;
      local_ssrc_ = (*receive_configs)[0].rtp.local_ssrc;
      remote_ssrc_ = (*receive_configs)[0].rtp.remote_ssrc;
      receive_transport_ = (*receive_configs)[0].rtcp_send_transport;
    }

    void PerformTest() override {
      EXPECT_TRUE(Wait())
          << "Timed out waiting for packets to be NACKed, retransmitted and "
             "rendered.";
    }

    uint32_t local_ssrc_;
    uint32_t remote_ssrc_;
    Transport* receive_transport_;
    absl::optional<uint16_t> sequence_number_to_retransmit_;
  } test;

  RunBaseTest(&test);
}

TEST_F(RetransmissionEndToEndTest,
       StopSendingKeyframeRequestsForInactiveStream) {
  class KeyframeRequestObserver : public test::EndToEndTest {
   public:
    explicit KeyframeRequestObserver(TaskQueueBase* task_queue)
        : clock_(Clock::GetRealTimeClock()), task_queue_(task_queue) {}

    void OnVideoStreamsCreated(VideoSendStream* send_stream,
                               const std::vector<VideoReceiveStreamInterface*>&
                                   receive_streams) override {
      RTC_DCHECK_EQ(1, receive_streams.size());
      send_stream_ = send_stream;
      receive_stream_ = receive_streams[0];
    }

    Action OnReceiveRtcp(rtc::ArrayView<const uint8_t> packet) override {
      test::RtcpPacketParser parser;
      EXPECT_TRUE(parser.Parse(packet));
      if (parser.pli()->num_packets() > 0)
        task_queue_->PostTask([this] { Run(); });
      return SEND_PACKET;
    }

    bool PollStats() {
      if (receive_stream_->GetStats().frames_decoded > 0) {
        frame_decoded_ = true;
      } else if (clock_->TimeInMilliseconds() - start_time_ < 5000) {
        task_queue_->PostDelayedTask([this] { Run(); }, TimeDelta::Millis(100));
        return false;
      }
      return true;
    }

    void PerformTest() override {
      start_time_ = clock_->TimeInMilliseconds();
      task_queue_->PostTask([this] { Run(); });
      test_done_.Wait(rtc::Event::kForever);
    }

    void Run() {
      if (!frame_decoded_) {
        if (PollStats()) {
          send_stream_->Stop();
          if (!frame_decoded_) {
            test_done_.Set();
          } else {
            // Now we wait for the PLI packet. Once we receive it, a task
            // will be posted (see OnReceiveRtcp) and we'll check the stats
            // once more before signaling that we're done.
          }
        }
      } else {
        EXPECT_EQ(
            1U,
            receive_stream_->GetStats().rtcp_packet_type_counts.pli_packets);
        test_done_.Set();
      }
    }

   private:
    Clock* const clock_;
    VideoSendStream* send_stream_;
    VideoReceiveStreamInterface* receive_stream_;
    TaskQueueBase* const task_queue_;
    rtc::Event test_done_;
    bool frame_decoded_ = false;
    int64_t start_time_ = 0;
  } test(task_queue());

  RunBaseTest(&test);
}

void RetransmissionEndToEndTest::ReceivesPliAndRecovers(int rtp_history_ms) {
  static const int kPacketsToDrop = 1;

  class PliObserver : public test::EndToEndTest,
                      public rtc::VideoSinkInterface<VideoFrame> {
   public:
    explicit PliObserver(int rtp_history_ms)
        : EndToEndTest(test::VideoTestConstants::kLongTimeout),
          rtp_history_ms_(rtp_history_ms),
          nack_enabled_(rtp_history_ms > 0),
          highest_dropped_timestamp_(0),
          frames_to_drop_(0),
          received_pli_(false) {}

   private:
    Action OnSendRtp(rtc::ArrayView<const uint8_t> packet) override {
      MutexLock lock(&mutex_);
      RtpPacket rtp_packet;
      EXPECT_TRUE(rtp_packet.Parse(packet));

      // Drop all retransmitted packets to force a PLI.
      if (rtp_packet.Timestamp() <= highest_dropped_timestamp_)
        return DROP_PACKET;

      if (frames_to_drop_ > 0) {
        highest_dropped_timestamp_ = rtp_packet.Timestamp();
        --frames_to_drop_;
        return DROP_PACKET;
      }

      return SEND_PACKET;
    }

    Action OnReceiveRtcp(rtc::ArrayView<const uint8_t> packet) override {
      MutexLock lock(&mutex_);
      test::RtcpPacketParser parser;
      EXPECT_TRUE(parser.Parse(packet));
      if (!nack_enabled_)
        EXPECT_EQ(0, parser.nack()->num_packets());
      if (parser.pli()->num_packets() > 0)
        received_pli_ = true;
      return SEND_PACKET;
    }

    void OnFrame(const VideoFrame& video_frame) override {
      MutexLock lock(&mutex_);
      if (received_pli_ &&
          video_frame.rtp_timestamp() > highest_dropped_timestamp_) {
        observation_complete_.Set();
      }
      if (!received_pli_)
        frames_to_drop_ = kPacketsToDrop;
    }

    void ModifyVideoConfigs(
        VideoSendStream::Config* send_config,
        std::vector<VideoReceiveStreamInterface::Config>* receive_configs,
        VideoEncoderConfig* encoder_config) override {
      send_config->rtp.nack.rtp_history_ms = rtp_history_ms_;
      (*receive_configs)[0].rtp.nack.rtp_history_ms = rtp_history_ms_;
      (*receive_configs)[0].renderer = this;
    }

    void PerformTest() override {
      EXPECT_TRUE(Wait()) << "Timed out waiting for PLI to be "
                             "received and a frame to be "
                             "rendered afterwards.";
    }

    Mutex mutex_;
    int rtp_history_ms_;
    bool nack_enabled_;
    uint32_t highest_dropped_timestamp_ RTC_GUARDED_BY(&mutex_);
    int frames_to_drop_ RTC_GUARDED_BY(&mutex_);
    bool received_pli_ RTC_GUARDED_BY(&mutex_);
  } test(rtp_history_ms);

  RunBaseTest(&test);
}

TEST_F(RetransmissionEndToEndTest, ReceivesPliAndRecoversWithNack) {
  ReceivesPliAndRecovers(1000);
}

TEST_F(RetransmissionEndToEndTest, ReceivesPliAndRecoversWithoutNack) {
  ReceivesPliAndRecovers(0);
}

// This test drops second RTP packet with a marker bit set, makes sure it's
// retransmitted and renders. Retransmission SSRCs are also checked.
void RetransmissionEndToEndTest::DecodesRetransmittedFrame(bool enable_rtx,
                                                           bool enable_red) {
  static const int kDroppedFrameNumber = 10;
  class RetransmissionObserver : public test::EndToEndTest,
                                 public rtc::VideoSinkInterface<VideoFrame> {
   public:
    RetransmissionObserver(bool enable_rtx, bool enable_red)
        : EndToEndTest(test::VideoTestConstants::kDefaultTimeout),
          payload_type_(GetPayloadType(false, enable_red)),
          retransmission_ssrc_(
              enable_rtx ? test::VideoTestConstants::kSendRtxSsrcs[0]
                         : test::VideoTestConstants::kVideoSendSsrcs[0]),
          retransmission_payload_type_(GetPayloadType(enable_rtx, enable_red)),
          encoder_factory_(
              [](const Environment& env, const SdpVideoFormat& format) {
                return CreateVp8Encoder(env);
              }),
          marker_bits_observed_(0),
          retransmitted_timestamp_(0) {}

   private:
    Action OnSendRtp(rtc::ArrayView<const uint8_t> packet) override {
      MutexLock lock(&mutex_);
      RtpPacket rtp_packet;
      EXPECT_TRUE(rtp_packet.Parse(packet));

      // Ignore padding-only packets over RTX.
      if (rtp_packet.PayloadType() != payload_type_) {
        EXPECT_EQ(retransmission_ssrc_, rtp_packet.Ssrc());
        if (rtp_packet.payload_size() == 0)
          return SEND_PACKET;
      }

      if (rtp_packet.Timestamp() == retransmitted_timestamp_) {
        EXPECT_EQ(retransmission_ssrc_, rtp_packet.Ssrc());
        EXPECT_EQ(retransmission_payload_type_, rtp_packet.PayloadType());
        return SEND_PACKET;
      }

      // Found the final packet of the frame to inflict loss to, drop this and
      // expect a retransmission.
      if (rtp_packet.PayloadType() == payload_type_ && rtp_packet.Marker() &&
          ++marker_bits_observed_ == kDroppedFrameNumber) {
        // This should be the only dropped packet.
        EXPECT_EQ(0u, retransmitted_timestamp_);
        retransmitted_timestamp_ = rtp_packet.Timestamp();
        return DROP_PACKET;
      }

      return SEND_PACKET;
    }

    void OnFrame(const VideoFrame& frame) override {
      EXPECT_EQ(kVideoRotation_90, frame.rotation());
      {
        MutexLock lock(&mutex_);
        if (frame.rtp_timestamp() == retransmitted_timestamp_)
          observation_complete_.Set();
      }
      orig_renderer_->OnFrame(frame);
    }

    void ModifyVideoConfigs(
        VideoSendStream::Config* send_config,
        std::vector<VideoReceiveStreamInterface::Config>* receive_configs,
        VideoEncoderConfig* encoder_config) override {
      send_config->rtp.nack.rtp_history_ms =
          test::VideoTestConstants::kNackRtpHistoryMs;

      // Insert ourselves into the rendering pipeline.
      RTC_DCHECK(!orig_renderer_);
      orig_renderer_ = (*receive_configs)[0].renderer;
      RTC_DCHECK(orig_renderer_);
      // To avoid post-decode frame dropping, disable the prerender buffer.
      (*receive_configs)[0].enable_prerenderer_smoothing = false;
      (*receive_configs)[0].renderer = this;

      (*receive_configs)[0].rtp.nack.rtp_history_ms =
          test::VideoTestConstants::kNackRtpHistoryMs;

      if (payload_type_ == test::VideoTestConstants::kRedPayloadType) {
        send_config->rtp.ulpfec.ulpfec_payload_type =
            test::VideoTestConstants::kUlpfecPayloadType;
        send_config->rtp.ulpfec.red_payload_type =
            test::VideoTestConstants::kRedPayloadType;
        if (retransmission_ssrc_ == test::VideoTestConstants::kSendRtxSsrcs[0])
          send_config->rtp.ulpfec.red_rtx_payload_type =
              test::VideoTestConstants::kRtxRedPayloadType;
        (*receive_configs)[0].rtp.ulpfec_payload_type =
            send_config->rtp.ulpfec.ulpfec_payload_type;
        (*receive_configs)[0].rtp.red_payload_type =
            send_config->rtp.ulpfec.red_payload_type;
      }

      if (retransmission_ssrc_ == test::VideoTestConstants::kSendRtxSsrcs[0]) {
        send_config->rtp.rtx.ssrcs.push_back(
            test::VideoTestConstants::kSendRtxSsrcs[0]);
        send_config->rtp.rtx.payload_type =
            test::VideoTestConstants::kSendRtxPayloadType;
        (*receive_configs)[0].rtp.rtx_ssrc =
            test::VideoTestConstants::kSendRtxSsrcs[0];
        (*receive_configs)[0].rtp.rtx_associated_payload_types
            [(payload_type_ == test::VideoTestConstants::kRedPayloadType)
                 ? test::VideoTestConstants::kRtxRedPayloadType
                 : test::VideoTestConstants::kSendRtxPayloadType] =
            payload_type_;
      }
      // Configure encoding and decoding with VP8, since generic packetization
      // doesn't support FEC with NACK.
      RTC_DCHECK_EQ(1, (*receive_configs)[0].decoders.size());
      send_config->encoder_settings.encoder_factory = &encoder_factory_;
      send_config->rtp.payload_name = "VP8";
      encoder_config->codec_type = kVideoCodecVP8;
      (*receive_configs)[0].decoders[0].video_format = SdpVideoFormat::VP8();
    }

    void OnFrameGeneratorCapturerCreated(
        test::FrameGeneratorCapturer* frame_generator_capturer) override {
      frame_generator_capturer->SetFakeRotation(kVideoRotation_90);
    }

    void PerformTest() override {
      EXPECT_TRUE(Wait())
          << "Timed out while waiting for retransmission to render.";
    }

    int GetPayloadType(bool use_rtx, bool use_fec) {
      if (use_fec) {
        if (use_rtx)
          return test::VideoTestConstants::kRtxRedPayloadType;
        return test::VideoTestConstants::kRedPayloadType;
      }
      if (use_rtx)
        return test::VideoTestConstants::kSendRtxPayloadType;
      return test::VideoTestConstants::kFakeVideoSendPayloadType;
    }

    Mutex mutex_;
    rtc::VideoSinkInterface<VideoFrame>* orig_renderer_ = nullptr;
    const int payload_type_;
    const uint32_t retransmission_ssrc_;
    const int retransmission_payload_type_;
    test::FunctionVideoEncoderFactory encoder_factory_;
    const std::string payload_name_;
    int marker_bits_observed_;
    uint32_t retransmitted_timestamp_ RTC_GUARDED_BY(&mutex_);
  } test(enable_rtx, enable_red);

  RunBaseTest(&test);
}

TEST_F(RetransmissionEndToEndTest, DecodesRetransmittedFrame) {
  DecodesRetransmittedFrame(false, false);
}

TEST_F(RetransmissionEndToEndTest, DecodesRetransmittedFrameOverRtx) {
  DecodesRetransmittedFrame(true, false);
}

TEST_F(RetransmissionEndToEndTest, DecodesRetransmittedFrameByRed) {
  DecodesRetransmittedFrame(false, true);
}

TEST_F(RetransmissionEndToEndTest, DecodesRetransmittedFrameByRedOverRtx) {
  DecodesRetransmittedFrame(true, true);
}

}  // namespace webrtc
