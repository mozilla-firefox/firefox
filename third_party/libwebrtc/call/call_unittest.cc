/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "call/call.h"

#include <list>
#include <map>
#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/media_types.h"
#include "api/test/mock_audio_mixer.h"
#include "api/test/video/function_video_encoder_factory.h"
#include "api/units/timestamp.h"
#include "api/video/builtin_video_bitrate_allocator_factory.h"
#include "audio/audio_receive_stream.h"
#include "audio/audio_send_stream.h"
#include "call/adaptation/test/fake_resource.h"
#include "call/adaptation/test/mock_resource_listener.h"
#include "call/audio_state.h"
#include "modules/audio_device/include/mock_audio_device.h"
#include "modules/audio_processing/include/mock_audio_processing.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_interface.h"
#include "test/fake_encoder.h"
#include "test/gtest.h"
#include "test/mock_audio_decoder_factory.h"
#include "test/mock_transport.h"
#include "test/run_loop.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::Contains;
using ::testing::MockFunction;
using ::testing::NiceMock;
using ::testing::StrictMock;
using ::webrtc::test::FakeEncoder;
using ::webrtc::test::FunctionVideoEncoderFactory;
using ::webrtc::test::MockAudioDeviceModule;
using ::webrtc::test::MockAudioMixer;
using ::webrtc::test::MockAudioProcessing;
using ::webrtc::test::RunLoop;

struct CallHelper {
  explicit CallHelper(bool use_null_audio_processing) {
    AudioState::Config audio_state_config;
    audio_state_config.audio_mixer = rtc::make_ref_counted<MockAudioMixer>();
    audio_state_config.audio_processing =
        use_null_audio_processing
            ? nullptr
            : rtc::make_ref_counted<NiceMock<MockAudioProcessing>>();
    audio_state_config.audio_device_module =
        rtc::make_ref_counted<MockAudioDeviceModule>();
    CallConfig config(CreateEnvironment());
    config.audio_state = AudioState::Create(audio_state_config);
    call_ = Call::Create(config);
  }

  Call* operator->() { return call_.get(); }

 private:
  RunLoop loop_;
  std::unique_ptr<Call> call_;
};

rtc::scoped_refptr<Resource> FindResourceWhoseNameContains(
    const std::vector<rtc::scoped_refptr<Resource>>& resources,
    absl::string_view name_contains) {
  for (const auto& resource : resources) {
    if (resource->Name().find(std::string(name_contains)) != std::string::npos)
      return resource;
  }
  return nullptr;
}

}  // namespace

TEST(CallTest, ConstructDestruct) {
  for (bool use_null_audio_processing : {false, true}) {
    CallHelper call(use_null_audio_processing);
  }
}

TEST(CallTest, CreateDestroy_AudioSendStream) {
  for (bool use_null_audio_processing : {false, true}) {
    CallHelper call(use_null_audio_processing);
    MockTransport send_transport;
    AudioSendStream::Config config(&send_transport);
    config.rtp.ssrc = 42;
    AudioSendStream* stream = call->CreateAudioSendStream(config);
    EXPECT_NE(stream, nullptr);
    call->DestroyAudioSendStream(stream);
  }
}

TEST(CallTest, CreateDestroy_AudioReceiveStream) {
  for (bool use_null_audio_processing : {false, true}) {
    CallHelper call(use_null_audio_processing);
    AudioReceiveStreamInterface::Config config;
    MockTransport rtcp_send_transport;
    config.rtp.remote_ssrc = 42;
    config.rtcp_send_transport = &rtcp_send_transport;
    config.decoder_factory =
        rtc::make_ref_counted<webrtc::MockAudioDecoderFactory>();
    AudioReceiveStreamInterface* stream =
        call->CreateAudioReceiveStream(config);
    EXPECT_NE(stream, nullptr);
    call->DestroyAudioReceiveStream(stream);
  }
}

TEST(CallTest, CreateDestroy_AudioSendStreams) {
  for (bool use_null_audio_processing : {false, true}) {
    CallHelper call(use_null_audio_processing);
    MockTransport send_transport;
    AudioSendStream::Config config(&send_transport);
    std::list<AudioSendStream*> streams;
    for (int i = 0; i < 2; ++i) {
      for (uint32_t ssrc = 0; ssrc < 1234567; ssrc += 34567) {
        config.rtp.ssrc = ssrc;
        AudioSendStream* stream = call->CreateAudioSendStream(config);
        EXPECT_NE(stream, nullptr);
        if (ssrc & 1) {
          streams.push_back(stream);
        } else {
          streams.push_front(stream);
        }
      }
      for (auto s : streams) {
        call->DestroyAudioSendStream(s);
      }
      streams.clear();
    }
  }
}

TEST(CallTest, CreateDestroy_AudioReceiveStreams) {
  for (bool use_null_audio_processing : {false, true}) {
    CallHelper call(use_null_audio_processing);
    AudioReceiveStreamInterface::Config config;
    MockTransport rtcp_send_transport;
    config.rtcp_send_transport = &rtcp_send_transport;
    config.decoder_factory =
        rtc::make_ref_counted<webrtc::MockAudioDecoderFactory>();
    std::list<AudioReceiveStreamInterface*> streams;
    for (int i = 0; i < 2; ++i) {
      for (uint32_t ssrc = 0; ssrc < 1234567; ssrc += 34567) {
        config.rtp.remote_ssrc = ssrc;
        AudioReceiveStreamInterface* stream =
            call->CreateAudioReceiveStream(config);
        EXPECT_NE(stream, nullptr);
        if (ssrc & 1) {
          streams.push_back(stream);
        } else {
          streams.push_front(stream);
        }
      }
      for (auto s : streams) {
        call->DestroyAudioReceiveStream(s);
      }
      streams.clear();
    }
  }
}

TEST(CallTest, CreateDestroy_AssociateAudioSendReceiveStreams_RecvFirst) {
  for (bool use_null_audio_processing : {false, true}) {
    CallHelper call(use_null_audio_processing);
    AudioReceiveStreamInterface::Config recv_config;
    MockTransport rtcp_send_transport;
    recv_config.rtp.remote_ssrc = 42;
    recv_config.rtp.local_ssrc = 777;
    recv_config.rtcp_send_transport = &rtcp_send_transport;
    recv_config.decoder_factory =
        rtc::make_ref_counted<webrtc::MockAudioDecoderFactory>();
    AudioReceiveStreamInterface* recv_stream =
        call->CreateAudioReceiveStream(recv_config);
    EXPECT_NE(recv_stream, nullptr);

    MockTransport send_transport;
    AudioSendStream::Config send_config(&send_transport);
    send_config.rtp.ssrc = 777;
    AudioSendStream* send_stream = call->CreateAudioSendStream(send_config);
    EXPECT_NE(send_stream, nullptr);

    AudioReceiveStreamImpl* internal_recv_stream =
        static_cast<AudioReceiveStreamImpl*>(recv_stream);
    EXPECT_EQ(send_stream,
              internal_recv_stream->GetAssociatedSendStreamForTesting());

    call->DestroyAudioSendStream(send_stream);
    EXPECT_EQ(nullptr,
              internal_recv_stream->GetAssociatedSendStreamForTesting());

    call->DestroyAudioReceiveStream(recv_stream);
  }
}

TEST(CallTest, CreateDestroy_AssociateAudioSendReceiveStreams_SendFirst) {
  for (bool use_null_audio_processing : {false, true}) {
    CallHelper call(use_null_audio_processing);
    MockTransport send_transport;
    AudioSendStream::Config send_config(&send_transport);
    send_config.rtp.ssrc = 777;
    AudioSendStream* send_stream = call->CreateAudioSendStream(send_config);
    EXPECT_NE(send_stream, nullptr);

    AudioReceiveStreamInterface::Config recv_config;
    MockTransport rtcp_send_transport;
    recv_config.rtp.remote_ssrc = 42;
    recv_config.rtp.local_ssrc = 777;
    recv_config.rtcp_send_transport = &rtcp_send_transport;
    recv_config.decoder_factory =
        rtc::make_ref_counted<webrtc::MockAudioDecoderFactory>();
    AudioReceiveStreamInterface* recv_stream =
        call->CreateAudioReceiveStream(recv_config);
    EXPECT_NE(recv_stream, nullptr);

    AudioReceiveStreamImpl* internal_recv_stream =
        static_cast<AudioReceiveStreamImpl*>(recv_stream);
    EXPECT_EQ(send_stream,
              internal_recv_stream->GetAssociatedSendStreamForTesting());

    call->DestroyAudioReceiveStream(recv_stream);

    call->DestroyAudioSendStream(send_stream);
  }
}

TEST(CallTest, CreateDestroy_FlexfecReceiveStream) {
  for (bool use_null_audio_processing : {false, true}) {
    CallHelper call(use_null_audio_processing);
    MockTransport rtcp_send_transport;
    FlexfecReceiveStream::Config config(&rtcp_send_transport);
    config.payload_type = 118;
    config.rtp.remote_ssrc = 38837212;
    config.protected_media_ssrcs = {27273};

    FlexfecReceiveStream* stream = call->CreateFlexfecReceiveStream(config);
    EXPECT_NE(stream, nullptr);
    call->DestroyFlexfecReceiveStream(stream);
  }
}

TEST(CallTest, CreateDestroy_FlexfecReceiveStreams) {
  for (bool use_null_audio_processing : {false, true}) {
    CallHelper call(use_null_audio_processing);
    MockTransport rtcp_send_transport;
    FlexfecReceiveStream::Config config(&rtcp_send_transport);
    config.payload_type = 118;
    std::list<FlexfecReceiveStream*> streams;

    for (int i = 0; i < 2; ++i) {
      for (uint32_t ssrc = 0; ssrc < 1234567; ssrc += 34567) {
        config.rtp.remote_ssrc = ssrc;
        config.protected_media_ssrcs = {ssrc + 1};
        FlexfecReceiveStream* stream = call->CreateFlexfecReceiveStream(config);
        EXPECT_NE(stream, nullptr);
        if (ssrc & 1) {
          streams.push_back(stream);
        } else {
          streams.push_front(stream);
        }
      }
      for (auto s : streams) {
        call->DestroyFlexfecReceiveStream(s);
      }
      streams.clear();
    }
  }
}

TEST(CallTest, MultipleFlexfecReceiveStreamsProtectingSingleVideoStream) {
  for (bool use_null_audio_processing : {false, true}) {
    CallHelper call(use_null_audio_processing);
    MockTransport rtcp_send_transport;
    FlexfecReceiveStream::Config config(&rtcp_send_transport);
    config.payload_type = 118;
    config.protected_media_ssrcs = {1324234};
    FlexfecReceiveStream* stream;
    std::list<FlexfecReceiveStream*> streams;

    config.rtp.remote_ssrc = 838383;
    stream = call->CreateFlexfecReceiveStream(config);
    EXPECT_NE(stream, nullptr);
    streams.push_back(stream);

    config.rtp.remote_ssrc = 424993;
    stream = call->CreateFlexfecReceiveStream(config);
    EXPECT_NE(stream, nullptr);
    streams.push_back(stream);

    config.rtp.remote_ssrc = 99383;
    stream = call->CreateFlexfecReceiveStream(config);
    EXPECT_NE(stream, nullptr);
    streams.push_back(stream);

    config.rtp.remote_ssrc = 5548;
    stream = call->CreateFlexfecReceiveStream(config);
    EXPECT_NE(stream, nullptr);
    streams.push_back(stream);

    for (auto s : streams) {
      call->DestroyFlexfecReceiveStream(s);
    }
  }
}

TEST(CallTest,
     DeliverRtpPacketOfTypeAudioTriggerOnUndemuxablePacketHandlerIfNotDemuxed) {
  CallHelper call(/*use_null_audio_processing=*/false);
  MockFunction<bool(const RtpPacketReceived& parsed_packet)>
      un_demuxable_packet_handler;

  RtpPacketReceived packet;
  packet.set_arrival_time(Timestamp::Millis(1));
  EXPECT_CALL(un_demuxable_packet_handler, Call);
  call->Receiver()->DeliverRtpPacket(
      MediaType::AUDIO, packet, un_demuxable_packet_handler.AsStdFunction());
}

TEST(CallTest,
     DeliverRtpPacketOfTypeVideoTriggerOnUndemuxablePacketHandlerIfNotDemuxed) {
  CallHelper call(/*use_null_audio_processing=*/false);
  MockFunction<bool(const RtpPacketReceived& parsed_packet)>
      un_demuxable_packet_handler;

  RtpPacketReceived packet;
  packet.set_arrival_time(Timestamp::Millis(1));
  EXPECT_CALL(un_demuxable_packet_handler, Call);
  call->Receiver()->DeliverRtpPacket(
      MediaType::VIDEO, packet, un_demuxable_packet_handler.AsStdFunction());
}

TEST(CallTest,
     DeliverRtpPacketOfTypeAnyDoesNotTriggerOnUndemuxablePacketHandler) {
  CallHelper call(/*use_null_audio_processing=*/false);
  MockFunction<bool(const RtpPacketReceived& parsed_packet)>
      un_demuxable_packet_handler;

  RtpPacketReceived packet;
  packet.set_arrival_time(Timestamp::Millis(1));
  EXPECT_CALL(un_demuxable_packet_handler, Call).Times(0);
  call->Receiver()->DeliverRtpPacket(
      MediaType::ANY, packet, un_demuxable_packet_handler.AsStdFunction());
}

TEST(CallTest, RecreatingAudioStreamWithSameSsrcReusesRtpState) {
  constexpr uint32_t kSSRC = 12345;
  for (bool use_null_audio_processing : {false, true}) {
    CallHelper call(use_null_audio_processing);

    auto create_stream_and_get_rtp_state = [&](uint32_t ssrc) {
      MockTransport send_transport;
      AudioSendStream::Config config(&send_transport);
      config.rtp.ssrc = ssrc;
      AudioSendStream* stream = call->CreateAudioSendStream(config);
      const RtpState rtp_state =
          static_cast<internal::AudioSendStream*>(stream)->GetRtpState();
      call->DestroyAudioSendStream(stream);
      return rtp_state;
    };

    const RtpState rtp_state1 = create_stream_and_get_rtp_state(kSSRC);
    const RtpState rtp_state2 = create_stream_and_get_rtp_state(kSSRC);

    EXPECT_EQ(rtp_state1.sequence_number, rtp_state2.sequence_number);
    EXPECT_EQ(rtp_state1.start_timestamp, rtp_state2.start_timestamp);
    EXPECT_EQ(rtp_state1.timestamp, rtp_state2.timestamp);
    EXPECT_EQ(rtp_state1.capture_time, rtp_state2.capture_time);
    EXPECT_EQ(rtp_state1.last_timestamp_time, rtp_state2.last_timestamp_time);
  }
}

TEST(CallTest, AddAdaptationResourceAfterCreatingVideoSendStream) {
  CallHelper call(true);
  // Create a VideoSendStream.
  FunctionVideoEncoderFactory fake_encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return std::make_unique<FakeEncoder>(env);
      });
  auto bitrate_allocator_factory = CreateBuiltinVideoBitrateAllocatorFactory();
  MockTransport send_transport;
  VideoSendStream::Config config(&send_transport);
  config.rtp.payload_type = 110;
  config.rtp.ssrcs = {42};
  config.encoder_settings.encoder_factory = &fake_encoder_factory;
  config.encoder_settings.bitrate_allocator_factory =
      bitrate_allocator_factory.get();
  VideoEncoderConfig encoder_config;
  encoder_config.max_bitrate_bps = 1337;
  VideoSendStream* stream1 =
      call->CreateVideoSendStream(config.Copy(), encoder_config.Copy());
  EXPECT_NE(stream1, nullptr);
  config.rtp.ssrcs = {43};
  VideoSendStream* stream2 =
      call->CreateVideoSendStream(config.Copy(), encoder_config.Copy());
  EXPECT_NE(stream2, nullptr);
  // Add a fake resource.
  auto fake_resource = FakeResource::Create("FakeResource");
  call->AddAdaptationResource(fake_resource);
  // An adapter resource mirroring the `fake_resource` should now be present on
  // both streams.
  auto injected_resource1 = FindResourceWhoseNameContains(
      stream1->GetAdaptationResources(), fake_resource->Name());
  EXPECT_TRUE(injected_resource1);
  auto injected_resource2 = FindResourceWhoseNameContains(
      stream2->GetAdaptationResources(), fake_resource->Name());
  EXPECT_TRUE(injected_resource2);
  // Overwrite the real resource listeners with mock ones to verify the signal
  // gets through.
  injected_resource1->SetResourceListener(nullptr);
  StrictMock<MockResourceListener> resource_listener1;
  EXPECT_CALL(resource_listener1, OnResourceUsageStateMeasured(_, _))
      .Times(1)
      .WillOnce([injected_resource1](rtc::scoped_refptr<Resource> resource,
                                     ResourceUsageState usage_state) {
        EXPECT_EQ(injected_resource1, resource);
        EXPECT_EQ(ResourceUsageState::kOveruse, usage_state);
      });
  injected_resource1->SetResourceListener(&resource_listener1);
  injected_resource2->SetResourceListener(nullptr);
  StrictMock<MockResourceListener> resource_listener2;
  EXPECT_CALL(resource_listener2, OnResourceUsageStateMeasured(_, _))
      .Times(1)
      .WillOnce([injected_resource2](rtc::scoped_refptr<Resource> resource,
                                     ResourceUsageState usage_state) {
        EXPECT_EQ(injected_resource2, resource);
        EXPECT_EQ(ResourceUsageState::kOveruse, usage_state);
      });
  injected_resource2->SetResourceListener(&resource_listener2);
  // The kOveruse signal should get to our resource listeners.
  fake_resource->SetUsageState(ResourceUsageState::kOveruse);
  call->DestroyVideoSendStream(stream1);
  call->DestroyVideoSendStream(stream2);
}

TEST(CallTest, AddAdaptationResourceBeforeCreatingVideoSendStream) {
  CallHelper call(true);
  // Add a fake resource.
  auto fake_resource = FakeResource::Create("FakeResource");
  call->AddAdaptationResource(fake_resource);
  // Create a VideoSendStream.
  FunctionVideoEncoderFactory fake_encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return std::make_unique<FakeEncoder>(env);
      });
  auto bitrate_allocator_factory = CreateBuiltinVideoBitrateAllocatorFactory();
  MockTransport send_transport;
  VideoSendStream::Config config(&send_transport);
  config.rtp.payload_type = 110;
  config.rtp.ssrcs = {42};
  config.encoder_settings.encoder_factory = &fake_encoder_factory;
  config.encoder_settings.bitrate_allocator_factory =
      bitrate_allocator_factory.get();
  VideoEncoderConfig encoder_config;
  encoder_config.max_bitrate_bps = 1337;
  VideoSendStream* stream1 =
      call->CreateVideoSendStream(config.Copy(), encoder_config.Copy());
  EXPECT_NE(stream1, nullptr);
  config.rtp.ssrcs = {43};
  VideoSendStream* stream2 =
      call->CreateVideoSendStream(config.Copy(), encoder_config.Copy());
  EXPECT_NE(stream2, nullptr);
  // An adapter resource mirroring the `fake_resource` should be present on both
  // streams.
  auto injected_resource1 = FindResourceWhoseNameContains(
      stream1->GetAdaptationResources(), fake_resource->Name());
  EXPECT_TRUE(injected_resource1);
  auto injected_resource2 = FindResourceWhoseNameContains(
      stream2->GetAdaptationResources(), fake_resource->Name());
  EXPECT_TRUE(injected_resource2);
  // Overwrite the real resource listeners with mock ones to verify the signal
  // gets through.
  injected_resource1->SetResourceListener(nullptr);
  StrictMock<MockResourceListener> resource_listener1;
  EXPECT_CALL(resource_listener1, OnResourceUsageStateMeasured(_, _))
      .Times(1)
      .WillOnce([injected_resource1](rtc::scoped_refptr<Resource> resource,
                                     ResourceUsageState usage_state) {
        EXPECT_EQ(injected_resource1, resource);
        EXPECT_EQ(ResourceUsageState::kUnderuse, usage_state);
      });
  injected_resource1->SetResourceListener(&resource_listener1);
  injected_resource2->SetResourceListener(nullptr);
  StrictMock<MockResourceListener> resource_listener2;
  EXPECT_CALL(resource_listener2, OnResourceUsageStateMeasured(_, _))
      .Times(1)
      .WillOnce([injected_resource2](rtc::scoped_refptr<Resource> resource,
                                     ResourceUsageState usage_state) {
        EXPECT_EQ(injected_resource2, resource);
        EXPECT_EQ(ResourceUsageState::kUnderuse, usage_state);
      });
  injected_resource2->SetResourceListener(&resource_listener2);
  // The kUnderuse signal should get to our resource listeners.
  fake_resource->SetUsageState(ResourceUsageState::kUnderuse);
  call->DestroyVideoSendStream(stream1);
  call->DestroyVideoSendStream(stream2);
}

}  // namespace webrtc
