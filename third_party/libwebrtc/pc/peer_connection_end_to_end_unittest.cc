/*
 *  Copyright 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "api/audio_codecs/L16/audio_decoder_L16.h"
#include "api/audio_codecs/L16/audio_encoder_L16.h"
#include "api/audio_codecs/audio_codec_pair_id.h"
#include "api/audio_codecs/audio_decoder.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_decoder_factory_template.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_encoder_factory_template.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/opus_audio_decoder_factory.h"
#include "api/audio_codecs/opus_audio_encoder_factory.h"
#include "api/audio_options.h"
#include "api/data_channel_interface.h"
#include "api/environment/environment.h"
#include "api/make_ref_counted.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/sctp_transport_interface.h"
#include "api/test/rtc_error_matchers.h"
#include "api/units/time_delta.h"
#include "pc/test/peer_connection_test_wrapper.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/physical_socket_server.h"
#include "rtc_base/thread.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/wait_until.h"
// Notice that mockpeerconnectionobservers.h must be included after the above!
#include "pc/test/mock_peer_connection_observers.h"
#include "test/mock_audio_decoder.h"
#include "test/mock_audio_decoder_factory.h"
#include "test/mock_audio_encoder_factory.h"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::NotNull;
using ::testing::SizeIs;
using ::testing::StrictMock;
using ::testing::Values;

namespace webrtc {
namespace {

constexpr int kMaxWait = 25000;

class PeerConnectionEndToEndBaseTest : public ::testing::Test {
 public:
  typedef std::vector<scoped_refptr<DataChannelInterface>> DataChannelList;

  explicit PeerConnectionEndToEndBaseTest(SdpSemantics sdp_semantics)
      : env_(CreateTestEnvironment()),
        network_thread_(std::make_unique<Thread>(&pss_)) {
    RTC_CHECK(network_thread_->Start());
    caller_ = make_ref_counted<PeerConnectionTestWrapper>(
        "caller", env_, &pss_, network_thread_.get());
    callee_ = make_ref_counted<PeerConnectionTestWrapper>(
        "callee", env_, &pss_, network_thread_.get());
    PeerConnectionInterface::IceServer ice_server;
    ice_server.uri = "stun:stun.l.google.com:19302";
    caller_config_.servers.push_back(ice_server);
    caller_config_.sdp_semantics = sdp_semantics;
    callee_config_.servers.push_back(ice_server);
    callee_config_.sdp_semantics = sdp_semantics;
  }

  void CreatePcs(scoped_refptr<AudioEncoderFactory> audio_encoder_factory1,
                 scoped_refptr<AudioDecoderFactory> audio_decoder_factory1,
                 scoped_refptr<AudioEncoderFactory> audio_encoder_factory2,
                 scoped_refptr<AudioDecoderFactory> audio_decoder_factory2) {
    EXPECT_TRUE(caller_->CreatePc(caller_config_, audio_encoder_factory1,
                                  audio_decoder_factory1));
    EXPECT_TRUE(callee_->CreatePc(callee_config_, audio_encoder_factory2,
                                  audio_decoder_factory2));
    PeerConnectionTestWrapper::Connect(caller_.get(), callee_.get());

    caller_->SubscribeOnDataChannel(this,
                                    [this](DataChannelInterface* channel) {
                                      OnCallerAddedDataChanel(channel);
                                    });
    callee_->SubscribeOnDataChannel(this,
                                    [this](DataChannelInterface* channel) {
                                      OnCalleeAddedDataChannel(channel);
                                    });
  }

  void CreatePcs(scoped_refptr<AudioEncoderFactory> audio_encoder_factory,
                 scoped_refptr<AudioDecoderFactory> audio_decoder_factory) {
    CreatePcs(audio_encoder_factory, audio_decoder_factory,
              audio_encoder_factory, audio_decoder_factory);
  }

  void GetAndAddUserMedia() {
    AudioOptions audio_options;
    GetAndAddUserMedia(true, audio_options, true);
  }

  void GetAndAddUserMedia(bool audio,
                          const AudioOptions& audio_options,
                          bool video) {
    caller_->GetAndAddUserMedia(audio, audio_options, video);
    callee_->GetAndAddUserMedia(audio, audio_options, video);
  }

  void Negotiate() {
    caller_->CreateOffer(PeerConnectionInterface::RTCOfferAnswerOptions());
  }

  void WaitForCallEstablished() {
    caller_->WaitForCallEstablished();
    callee_->WaitForCallEstablished();
  }

  void WaitForConnection() {
    caller_->WaitForConnection();
    callee_->WaitForConnection();
  }

  void OnCallerAddedDataChanel(DataChannelInterface* dc) {
    caller_signaled_data_channels_.push_back(
        scoped_refptr<DataChannelInterface>(dc));
  }

  void OnCalleeAddedDataChannel(DataChannelInterface* dc) {
    callee_signaled_data_channels_.push_back(
        scoped_refptr<DataChannelInterface>(dc));
  }

  // Tests that `dc1` and `dc2` can send to and receive from each other.
  void TestDataChannelSendAndReceive(DataChannelInterface* dc1,
                                     DataChannelInterface* dc2,
                                     size_t size = 6) {
    auto dc1_observer =
        std::make_unique<MockDataChannelObserver>(network_thread_.get(), dc1);

    auto dc2_observer =
        std::make_unique<MockDataChannelObserver>(network_thread_.get(), dc2);

    static const std::string kDummyData =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    DataBuffer buffer("");

    size_t sizeLeft = size;
    while (sizeLeft > 0) {
      size_t chunkSize =
          sizeLeft > kDummyData.length() ? kDummyData.length() : sizeLeft;
      buffer.data.AppendData(kDummyData.data(), chunkSize);
      sizeLeft -= chunkSize;
    }

    EXPECT_TRUE(dc1->Send(buffer));
    EXPECT_THAT(
        WaitUntil(
            [&] { return CopyOnWriteBuffer(dc2_observer->last_message()); },
            Eq(buffer.data), {.timeout = TimeDelta::Millis(kMaxWait)}),
        IsRtcOk());

    EXPECT_TRUE(dc2->Send(buffer));
    EXPECT_THAT(
        WaitUntil(
            [&] { return CopyOnWriteBuffer(dc1_observer->last_message()); },
            Eq(buffer.data), {.timeout = TimeDelta::Millis(kMaxWait)}),
        IsRtcOk());

    EXPECT_EQ(1U, dc1_observer->received_message_count());
    EXPECT_EQ(size, dc1_observer->last_message().length());
    EXPECT_EQ(1U, dc2_observer->received_message_count());
    EXPECT_EQ(size, dc2_observer->last_message().length());
  }

  void WaitForDataChannelsToOpen(DataChannelInterface* local_dc,
                                 const DataChannelList& remote_dc_list,
                                 size_t remote_dc_index) {
    EXPECT_THAT(WaitUntil([&] { return local_dc->state(); },
                          Eq(DataChannelInterface::kOpen),
                          {.timeout = TimeDelta::Millis(kMaxWait)}),
                IsRtcOk());

    ASSERT_THAT(
        WaitUntil([&] { return remote_dc_list.size(); }, Gt(remote_dc_index),
                  {.timeout = TimeDelta::Millis(kMaxWait)}),
        IsRtcOk());
    EXPECT_THAT(
        WaitUntil([&] { return remote_dc_list[remote_dc_index]->state(); },
                  Eq(DataChannelInterface::kOpen),
                  {.timeout = TimeDelta::Millis(kMaxWait)}),
        IsRtcOk());
    EXPECT_EQ(local_dc->id(), remote_dc_list[remote_dc_index]->id());
  }

  void CloseDataChannels(DataChannelInterface* local_dc,
                         const DataChannelList& remote_dc_list,
                         size_t remote_dc_index) {
    local_dc->Close();
    EXPECT_THAT(WaitUntil([&] { return local_dc->state(); },
                          Eq(DataChannelInterface::kClosed),
                          {.timeout = TimeDelta::Millis(kMaxWait)}),
                IsRtcOk());
    EXPECT_THAT(
        WaitUntil([&] { return remote_dc_list[remote_dc_index]->state(); },
                  Eq(DataChannelInterface::kClosed),
                  {.timeout = TimeDelta::Millis(kMaxWait)}),
        IsRtcOk());
  }

  void SetCalleeMaxSctpStreams(int count) {
    callee_config_.max_sctp_streams = count;
  }

 protected:
  test::RunLoop main_thread_;
  PhysicalSocketServer pss_;
  Environment env_;
  std::unique_ptr<Thread> network_thread_;
  scoped_refptr<PeerConnectionTestWrapper> caller_;
  scoped_refptr<PeerConnectionTestWrapper> callee_;
  DataChannelList caller_signaled_data_channels_;
  DataChannelList callee_signaled_data_channels_;
  PeerConnectionInterface::RTCConfiguration caller_config_;
  PeerConnectionInterface::RTCConfiguration callee_config_;
};

class PeerConnectionEndToEndTest
    : public PeerConnectionEndToEndBaseTest,
      public ::testing::WithParamInterface<SdpSemantics> {
 protected:
  PeerConnectionEndToEndTest() : PeerConnectionEndToEndBaseTest(GetParam()) {}
};

namespace {

std::unique_ptr<AudioDecoder> CreateForwardingMockDecoder(
    std::unique_ptr<AudioDecoder> real_decoder) {
  class ForwardingMockDecoder : public StrictMock<MockAudioDecoder> {
   public:
    explicit ForwardingMockDecoder(std::unique_ptr<AudioDecoder> decoder)
        : decoder_(std::move(decoder)) {}

   private:
    std::unique_ptr<AudioDecoder> decoder_;
  };

  const auto dec = real_decoder.get();  // For lambda capturing.
  auto mock_decoder =
      std::make_unique<ForwardingMockDecoder>(std::move(real_decoder));
  EXPECT_CALL(*mock_decoder, Channels())
      .Times(AtLeast(1))
      .WillRepeatedly([dec] { return dec->Channels(); });
  EXPECT_CALL(*mock_decoder, DecodeInternal(_, _, _, _, _))
      .Times(AtLeast(1))
      .WillRepeatedly([dec](const uint8_t* encoded, size_t encoded_len,
                            int sample_rate_hz, int16_t* decoded,
                            AudioDecoder::SpeechType* speech_type) {
        return dec->Decode(encoded, encoded_len, sample_rate_hz,
                           std::numeric_limits<size_t>::max(), decoded,
                           speech_type);
      });
  EXPECT_CALL(*mock_decoder, Die());
  EXPECT_CALL(*mock_decoder, HasDecodePlc()).WillRepeatedly([dec] {
    return dec->HasDecodePlc();
  });
  EXPECT_CALL(*mock_decoder, PacketDuration(_, _))
      .Times(AtLeast(1))
      .WillRepeatedly([dec](const uint8_t* encoded, size_t encoded_len) {
        return dec->PacketDuration(encoded, encoded_len);
      });
  EXPECT_CALL(*mock_decoder, SampleRateHz())
      .Times(AtLeast(1))
      .WillRepeatedly([dec] { return dec->SampleRateHz(); });

  return std::move(mock_decoder);
}

scoped_refptr<AudioDecoderFactory> CreateForwardingMockDecoderFactory(
    AudioDecoderFactory* real_decoder_factory) {
  scoped_refptr<MockAudioDecoderFactory> mock_decoder_factory =
      make_ref_counted<StrictMock<MockAudioDecoderFactory>>();
  EXPECT_CALL(*mock_decoder_factory, GetSupportedDecoders())
      .Times(AtLeast(1))
      .WillRepeatedly([real_decoder_factory] {
        return real_decoder_factory->GetSupportedDecoders();
      });
  EXPECT_CALL(*mock_decoder_factory, IsSupportedDecoder(_))
      .Times(AtLeast(1))
      .WillRepeatedly([real_decoder_factory](const SdpAudioFormat& format) {
        return real_decoder_factory->IsSupportedDecoder(format);
      });
  EXPECT_CALL(*mock_decoder_factory, Create)
      .Times(AtLeast(2))
      .WillRepeatedly([real_decoder_factory](
                          const Environment& env, const SdpAudioFormat& format,
                          std::optional<AudioCodecPairId> /* pair */) {
        auto real_decoder = real_decoder_factory->Create(env, format);
        return real_decoder
                   ? CreateForwardingMockDecoder(std::move(real_decoder))
                   : nullptr;
      });
  return mock_decoder_factory;
}

struct AudioEncoderUnicornSparklesRainbow {
  using Config = AudioEncoderL16::Config;
  static std::optional<Config> SdpToConfig(SdpAudioFormat format) {
    if (absl::EqualsIgnoreCase(format.name, "UnicornSparklesRainbow")) {
      const CodecParameterMap expected_params = {{"num_horns", "1"}};
      EXPECT_EQ(expected_params, format.parameters);
      format.parameters.clear();
      format.name = "L16";
      return AudioEncoderL16::SdpToConfig(format);
    } else {
      return std::nullopt;
    }
  }
  static void AppendSupportedEncoders(std::vector<AudioCodecSpec>* specs) {
    std::vector<AudioCodecSpec> new_specs;
    AudioEncoderL16::AppendSupportedEncoders(&new_specs);
    for (auto& spec : new_specs) {
      spec.format.name = "UnicornSparklesRainbow";
      EXPECT_TRUE(spec.format.parameters.empty());
      spec.format.parameters.emplace("num_horns", "1");
      specs->push_back(spec);
    }
  }
  static AudioCodecInfo QueryAudioEncoder(const Config& config) {
    return AudioEncoderL16::QueryAudioEncoder(config);
  }
  static std::unique_ptr<AudioEncoder> MakeAudioEncoder(const Config& config,
                                                        int payload_type) {
    return AudioEncoderL16::MakeAudioEncoder(config, payload_type);
  }
};

struct AudioDecoderUnicornSparklesRainbow {
  using Config = AudioDecoderL16::Config;
  static std::optional<Config> SdpToConfig(SdpAudioFormat format) {
    if (absl::EqualsIgnoreCase(format.name, "UnicornSparklesRainbow")) {
      const CodecParameterMap expected_params = {{"num_horns", "1"}};
      EXPECT_EQ(expected_params, format.parameters);
      format.parameters.clear();
      format.name = "L16";
      return AudioDecoderL16::SdpToConfig(format);
    } else {
      return std::nullopt;
    }
  }
  static void AppendSupportedDecoders(std::vector<AudioCodecSpec>* specs) {
    std::vector<AudioCodecSpec> new_specs;
    AudioDecoderL16::AppendSupportedDecoders(&new_specs);
    for (auto& spec : new_specs) {
      spec.format.name = "UnicornSparklesRainbow";
      EXPECT_TRUE(spec.format.parameters.empty());
      spec.format.parameters.emplace("num_horns", "1");
      specs->push_back(spec);
    }
  }
  static std::unique_ptr<AudioDecoder> MakeAudioDecoder(
      const Config& config,
      std::optional<AudioCodecPairId> codec_pair_id = std::nullopt) {
    return AudioDecoderL16::MakeAudioDecoder(config, codec_pair_id);
  }
};

}  // namespace

TEST_P(PeerConnectionEndToEndTest, Call) {
  scoped_refptr<AudioDecoderFactory> real_decoder_factory =
      CreateOpusAudioDecoderFactory();
  CreatePcs(CreateOpusAudioEncoderFactory(),
            CreateForwardingMockDecoderFactory(real_decoder_factory.get()));
  GetAndAddUserMedia();
  Negotiate();
  WaitForCallEstablished();
}

#if defined(IS_FUCHSIA)
TEST_P(PeerConnectionEndToEndTest, CallWithSdesKeyNegotiation) {
  config_.enable_dtls_srtp = false;
  CreatePcs(CreateOpusAudioEncoderFactory(), CreateOpusAudioDecoderFactory());
  GetAndAddUserMedia();
  Negotiate();
  WaitForCallEstablished();
}
#endif

TEST_P(PeerConnectionEndToEndTest, CallWithCustomCodec) {
  CreatePcs(CreateAudioEncoderFactory<AudioEncoderUnicornSparklesRainbow>(),
            CreateAudioDecoderFactory<AudioDecoderUnicornSparklesRainbow>(),
            CreateAudioEncoderFactory<AudioEncoderUnicornSparklesRainbow>(),
            CreateAudioDecoderFactory<AudioDecoderUnicornSparklesRainbow>());
  GetAndAddUserMedia();
  Negotiate();
  WaitForCallEstablished();
}

#ifdef WEBRTC_HAVE_SCTP
// Verifies that a DataChannel created before the negotiation can transition to
// "OPEN" and transfer data.
TEST_P(PeerConnectionEndToEndTest, CreateDataChannelBeforeNegotiate) {
  CreatePcs(MockAudioEncoderFactory::CreateEmptyFactory(),
            MockAudioDecoderFactory::CreateEmptyFactory());

  DataChannelInit init;
  scoped_refptr<DataChannelInterface> caller_dc =
      caller_->CreateDataChannel("data", init);
  scoped_refptr<DataChannelInterface> callee_dc =
      callee_->CreateDataChannel("data", init);

  Negotiate();
  WaitForConnection();

  WaitForDataChannelsToOpen(caller_dc.get(), callee_signaled_data_channels_, 0);
  WaitForDataChannelsToOpen(callee_dc.get(), caller_signaled_data_channels_, 0);

  TestDataChannelSendAndReceive(caller_dc.get(),
                                callee_signaled_data_channels_[0].get());
  TestDataChannelSendAndReceive(callee_dc.get(),
                                caller_signaled_data_channels_[0].get());

  CloseDataChannels(caller_dc.get(), callee_signaled_data_channels_, 0);
  CloseDataChannels(callee_dc.get(), caller_signaled_data_channels_, 0);
}

// Verifies that a DataChannel created after the negotiation can transition to
// "OPEN" and transfer data.
TEST_P(PeerConnectionEndToEndTest, CreateDataChannelAfterNegotiate) {
  CreatePcs(MockAudioEncoderFactory::CreateEmptyFactory(),
            MockAudioDecoderFactory::CreateEmptyFactory());

  DataChannelInit init;

  // This DataChannel is for creating the data content in the negotiation.
  scoped_refptr<DataChannelInterface> dummy =
      caller_->CreateDataChannel("data", init);
  Negotiate();
  WaitForConnection();

  // Wait for the data channel created pre-negotiation to be opened.
  WaitForDataChannelsToOpen(dummy.get(), callee_signaled_data_channels_, 0);

  // Create new DataChannels after the negotiation and verify their states.
  scoped_refptr<DataChannelInterface> caller_dc =
      caller_->CreateDataChannel("hello", init);
  scoped_refptr<DataChannelInterface> callee_dc =
      callee_->CreateDataChannel("hello", init);

  WaitForDataChannelsToOpen(caller_dc.get(), callee_signaled_data_channels_, 1);
  WaitForDataChannelsToOpen(callee_dc.get(), caller_signaled_data_channels_, 0);

  TestDataChannelSendAndReceive(caller_dc.get(),
                                callee_signaled_data_channels_[1].get());
  TestDataChannelSendAndReceive(callee_dc.get(),
                                caller_signaled_data_channels_[0].get());

  CloseDataChannels(caller_dc.get(), callee_signaled_data_channels_, 1);
  CloseDataChannels(callee_dc.get(), caller_signaled_data_channels_, 0);
}

// Verifies that a DataChannel created can transfer large messages.
TEST_P(PeerConnectionEndToEndTest, CreateDataChannelLargeTransfer) {
  CreatePcs(MockAudioEncoderFactory::CreateEmptyFactory(),
            MockAudioDecoderFactory::CreateEmptyFactory());

  DataChannelInit init;

  // This DataChannel is for creating the data content in the negotiation.
  scoped_refptr<DataChannelInterface> dummy =
      caller_->CreateDataChannel("data", init);
  Negotiate();
  WaitForConnection();

  // Wait for the data channel created pre-negotiation to be opened.
  WaitForDataChannelsToOpen(dummy.get(), callee_signaled_data_channels_, 0);

  // Create new DataChannels after the negotiation and verify their states.
  scoped_refptr<DataChannelInterface> caller_dc =
      caller_->CreateDataChannel("hello", init);
  scoped_refptr<DataChannelInterface> callee_dc =
      callee_->CreateDataChannel("hello", init);

  WaitForDataChannelsToOpen(caller_dc.get(), callee_signaled_data_channels_, 1);
  WaitForDataChannelsToOpen(callee_dc.get(), caller_signaled_data_channels_, 0);

  TestDataChannelSendAndReceive(
      caller_dc.get(), callee_signaled_data_channels_[1].get(), 256 * 1024);
  TestDataChannelSendAndReceive(
      callee_dc.get(), caller_signaled_data_channels_[0].get(), 256 * 1024);

  CloseDataChannels(caller_dc.get(), callee_signaled_data_channels_, 1);
  CloseDataChannels(callee_dc.get(), caller_signaled_data_channels_, 0);
}

// Verifies that DataChannel IDs are even/odd based on the DTLS roles.
TEST_P(PeerConnectionEndToEndTest, DataChannelIdAssignment) {
  CreatePcs(MockAudioEncoderFactory::CreateEmptyFactory(),
            MockAudioDecoderFactory::CreateEmptyFactory());

  DataChannelInit init;
  scoped_refptr<DataChannelInterface> caller_dc_1 =
      caller_->CreateDataChannel("data", init);
  scoped_refptr<DataChannelInterface> callee_dc_1 =
      callee_->CreateDataChannel("data", init);

  MockDataChannelObserver caller_dc_1_observer(network_thread_.get(),
                                               caller_dc_1.get());
  MockDataChannelObserver callee_dc_1_observer(network_thread_.get(),
                                               callee_dc_1.get());

  Negotiate();
  WaitForConnection();
  EXPECT_TRUE(WaitUntil([&] { return caller_dc_1_observer.IsOpen(); }));
  EXPECT_TRUE(WaitUntil([&] { return callee_dc_1_observer.IsOpen(); }));

  EXPECT_NE(-1, caller_dc_1->id());
  EXPECT_NE(-1, callee_dc_1->id());
  EXPECT_EQ(1, caller_dc_1->id() % 2);
  EXPECT_EQ(0, callee_dc_1->id() % 2);

  scoped_refptr<DataChannelInterface> caller_dc_2 =
      caller_->CreateDataChannel("data", init);
  scoped_refptr<DataChannelInterface> callee_dc_2 =
      callee_->CreateDataChannel("data", init);

  MockDataChannelObserver caller_dc_2_observer(network_thread_.get(),
                                               caller_dc_2.get());
  MockDataChannelObserver callee_dc_2_observer(network_thread_.get(),
                                               callee_dc_2.get());

  EXPECT_TRUE(WaitUntil([&] { return caller_dc_2_observer.IsOpen(); }));
  EXPECT_TRUE(WaitUntil([&] { return callee_dc_2_observer.IsOpen(); }));

  EXPECT_NE(-1, caller_dc_2->id());
  EXPECT_NE(-1, callee_dc_2->id());
  EXPECT_EQ(1, caller_dc_2->id() % 2);
  EXPECT_EQ(0, callee_dc_2->id() % 2);
}

// Verifies that the message is received by the right remote DataChannel when
// there are multiple DataChannels.
TEST_P(PeerConnectionEndToEndTest,
       MessageTransferBetweenTwoPairsOfDataChannels) {
  CreatePcs(MockAudioEncoderFactory::CreateEmptyFactory(),
            MockAudioDecoderFactory::CreateEmptyFactory());

  DataChannelInit init;

  scoped_refptr<DataChannelInterface> caller_dc_1 =
      caller_->CreateDataChannel("data", init);
  scoped_refptr<DataChannelInterface> caller_dc_2 =
      caller_->CreateDataChannel("data", init);

  Negotiate();
  WaitForConnection();
  WaitForDataChannelsToOpen(caller_dc_1.get(), callee_signaled_data_channels_,
                            0);
  WaitForDataChannelsToOpen(caller_dc_2.get(), callee_signaled_data_channels_,
                            1);

  auto dc_1_observer = std::make_unique<MockDataChannelObserver>(
      network_thread_.get(), callee_signaled_data_channels_[0].get());

  auto dc_2_observer = std::make_unique<MockDataChannelObserver>(
      network_thread_.get(), callee_signaled_data_channels_[1].get());

  const std::string message_1 = "hello 1";
  const std::string message_2 = "hello 2";

  caller_dc_1->Send(DataBuffer(message_1));
  EXPECT_THAT(
      WaitUntil([&] { return dc_1_observer->last_message(); }, Eq(message_1),
                {.timeout = TimeDelta::Millis(kMaxWait)}),
      IsRtcOk());

  caller_dc_2->Send(DataBuffer(message_2));
  EXPECT_THAT(
      WaitUntil([&] { return dc_2_observer->last_message(); }, Eq(message_2),
                {.timeout = TimeDelta::Millis(kMaxWait)}),
      IsRtcOk());

  EXPECT_EQ(1U, dc_1_observer->received_message_count());
  EXPECT_EQ(1U, dc_2_observer->received_message_count());
}

// Verifies that a DataChannel added from an OPEN message functions after
// a channel has been previously closed (webrtc issue 3778).
// This previously failed because the new channel re-used the ID of the closed
// channel, and the closed channel was incorrectly still assigned to the ID.
TEST_P(PeerConnectionEndToEndTest,
       DataChannelFromOpenWorksAfterPreviousChannelClosed) {
  CreatePcs(MockAudioEncoderFactory::CreateEmptyFactory(),
            MockAudioDecoderFactory::CreateEmptyFactory());

  DataChannelInit init;
  scoped_refptr<DataChannelInterface> caller_dc =
      caller_->CreateDataChannel("data", init);

  Negotiate();
  WaitForConnection();

  WaitForDataChannelsToOpen(caller_dc.get(), callee_signaled_data_channels_, 0);
  int first_channel_id = caller_dc->id();
  // Wait for the local side to say it's closed, but not the remote side.
  // Previously, the channel on which Close is called reported being closed
  // prematurely, and this caused issues; see bugs.webrtc.org/4453.
  caller_dc->Close();
  EXPECT_THAT(WaitUntil([&] { return caller_dc->state(); },
                        Eq(DataChannelInterface::kClosed),
                        {.timeout = TimeDelta::Millis(kMaxWait)}),
              IsRtcOk());

  // Create a new channel and ensure it works after closing the previous one.
  caller_dc = caller_->CreateDataChannel("data2", init);
  WaitForDataChannelsToOpen(caller_dc.get(), callee_signaled_data_channels_, 1);
  // Since the second channel was created after the first finished closing, it
  // should be able to re-use the first one's ID.
  EXPECT_EQ(first_channel_id, caller_dc->id());
  TestDataChannelSendAndReceive(caller_dc.get(),
                                callee_signaled_data_channels_[1].get());

  CloseDataChannels(caller_dc.get(), callee_signaled_data_channels_, 1);
}

// This tests that if a data channel is closed remotely while not referenced
// by the application (meaning only the PeerConnection contributes to its
// reference count), no memory access violation will occur.
// See: https://code.google.com/p/chromium/issues/detail?id=565048
TEST_P(PeerConnectionEndToEndTest, CloseDataChannelRemotelyWhileNotReferenced) {
  CreatePcs(MockAudioEncoderFactory::CreateEmptyFactory(),
            MockAudioDecoderFactory::CreateEmptyFactory());

  DataChannelInit init;
  scoped_refptr<DataChannelInterface> caller_dc =
      caller_->CreateDataChannel("data", init);

  Negotiate();
  WaitForConnection();

  WaitForDataChannelsToOpen(caller_dc.get(), callee_signaled_data_channels_, 0);
  // This removes the reference to the remote data channel that we hold.
  callee_signaled_data_channels_.clear();
  caller_dc->Close();
  EXPECT_THAT(WaitUntil([&] { return caller_dc->state(); },
                        Eq(DataChannelInterface::kClosed),
                        {.timeout = TimeDelta::Millis(kMaxWait)}),
              IsRtcOk());

  // Wait for a bit longer so the remote data channel will receive the
  // close message and be destroyed.
  main_thread_.RunFor(webrtc::TimeDelta::Millis(100));
}

// Test behavior of creating too many datachannels.
TEST_P(PeerConnectionEndToEndTest, TooManyDataChannelsOpenedBeforeConnecting) {
  constexpr int kReducedMaxSctpStreams = 4;
  SetCalleeMaxSctpStreams(kReducedMaxSctpStreams);
  CreatePcs(MockAudioEncoderFactory::CreateEmptyFactory(),
            MockAudioDecoderFactory::CreateEmptyFactory());

  DataChannelInit init;
  std::vector<scoped_refptr<DataChannelInterface>> channels;
  // Add datachannels that will be assigned 0, 2 and 4 or 1, 3 and 5
  // depending on DTLS role.
  for (int i = 0; i <= kReducedMaxSctpStreams / 2; i++) {
    scoped_refptr<DataChannelInterface> caller_dc =
        caller_->CreateDataChannel("data", init);
    channels.push_back(std::move(caller_dc));
  }
  Negotiate();
  WaitForConnection();
  scoped_refptr<SctpTransportInterface> caller_transport =
      caller_->pc()->GetSctpTransport();
  scoped_refptr<SctpTransportInterface> callee_transport =
      callee_->pc()->GetSctpTransport();
  ASSERT_THAT(caller_transport, NotNull());
  ASSERT_THAT(callee_transport, NotNull());
  // SCTP handshake completes asynchronously after ICE connection; wait for
  // MaxChannels to be negotiated.
  EXPECT_THAT(
      WaitUntil([&] { return caller_transport->Information().MaxChannels(); },
                Eq(kReducedMaxSctpStreams),
                {.timeout = TimeDelta::Millis(kMaxWait)}),
      IsRtcOk());
  EXPECT_THAT(
      WaitUntil([&] { return callee_transport->Information().MaxChannels(); },
                Eq(kReducedMaxSctpStreams),
                {.timeout = TimeDelta::Millis(kMaxWait)}),
      IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return callee_signaled_data_channels_; },
                        SizeIs(Ge(kReducedMaxSctpStreams / 2)),
                        {.timeout = TimeDelta::Millis(kMaxWait)}),
              IsRtcOk());
  // 0 and 2 should be open, 4 should be rejected as "ID too large".
  EXPECT_THAT(
      WaitUntil(
          [&] { return channels[(kReducedMaxSctpStreams / 2) - 1]->state(); },
          Eq(DataChannelInterface::kOpen),
          {.timeout = TimeDelta::Millis(kMaxWait)}),
      IsRtcOk());
  EXPECT_THAT(
      WaitUntil([&] { return channels[kReducedMaxSctpStreams / 2]->state(); },
                Eq(DataChannelInterface::kClosed),
                {.timeout = TimeDelta::Millis(kMaxWait)}),
      IsRtcOk());
}

// Verifies that max-message-size is propagated to the DataChannel.
TEST_P(PeerConnectionEndToEndTest, MaxMessageSizePropagated) {
  CreatePcs(MockAudioEncoderFactory::CreateEmptyFactory(),
            MockAudioDecoderFactory::CreateEmptyFactory());

  DataChannelInit init;
  scoped_refptr<DataChannelInterface> caller_dc =
      caller_->CreateDataChannel("caller", init);
  MockDataChannelObserver caller_dc_observer(network_thread_.get(),
                                             caller_dc.get());

  Negotiate();
  WaitForConnection();

  EXPECT_TRUE(WaitUntil([&] { return caller_dc_observer.IsOpen(); }));

  // Attempting to send a message larger than our announced
  // max-message-size should fail.
  DataBuffer buffer(CopyOnWriteBuffer(kSctpSendBufferSize + 64),
                    /*binary=*/true);
  EXPECT_FALSE(caller_dc->Send(buffer));

  scoped_refptr<DataChannelInterface> callee_dc =
      callee_->CreateDataChannel("callee", init);
  MockDataChannelObserver callee_dc_observer(network_thread_.get(),
                                             callee_dc.get());
  EXPECT_TRUE(WaitUntil([&] { return callee_dc_observer.IsOpen(); }));
  EXPECT_FALSE(callee_dc->Send(buffer));
}

#endif  // WEBRTC_HAVE_SCTP

TEST_P(PeerConnectionEndToEndTest, CanRestartIce) {
  scoped_refptr<AudioDecoderFactory> real_decoder_factory =
      CreateOpusAudioDecoderFactory();
  CreatePcs(CreateOpusAudioEncoderFactory(),
            CreateForwardingMockDecoderFactory(real_decoder_factory.get()));
  GetAndAddUserMedia();
  Negotiate();
  WaitForCallEstablished();
  // Cause ICE restart to be requested.
  auto config = caller_->pc()->GetConfiguration();
  ASSERT_NE(PeerConnectionInterface::kRelay, config.type);
  config.type = PeerConnectionInterface::kRelay;
  ASSERT_TRUE(caller_->pc()->SetConfiguration(config).ok());
  // When solving https://crbug.com/webrtc/10504, all we need to check
  // is that we do not crash. We should also be testing that restart happens.
}

INSTANTIATE_TEST_SUITE_P(PeerConnectionEndToEndTest,
                         PeerConnectionEndToEndTest,
                         Values(SdpSemantics::kPlanB_DEPRECATED,
                                SdpSemantics::kUnifiedPlan));
}  // namespace
}  // namespace webrtc
