/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/rtp_transport.h"

#include <cerrno>
#include <cstdint>
#include <optional>

#include "api/environment/environment.h"
#include "api/rtc_error.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/test/rtc_error_matchers.h"
#include "api/transport/ecn_marking.h"
#include "api/units/time_delta.h"
#include "call/rtp_demuxer.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "p2p/base/packet_transport_internal.h"
#include "p2p/test/fake_packet_transport.h"
#include "pc/session_description.h"
#include "pc/test/rtp_transport_test_util.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/buffer.h"
#include "rtc_base/containers/flat_set.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/logging.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/network_route.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/wait_until.h"

namespace webrtc {

namespace {

using ::testing::Eq;
using ::testing::Ge;
using ::testing::HasSubstr;

constexpr bool kMuxDisabled = false;
constexpr bool kMuxEnabled = true;
constexpr uint16_t kLocalNetId = 1;
constexpr uint16_t kRemoteNetId = 2;
constexpr int kLastPacketId = 100;
constexpr int kTransportOverheadPerPacket = 28;  // Ipv4(20) + UDP(8).

class SignalObserver {
 public:
  explicit SignalObserver(RtpTransport* transport) {
    transport_ = transport;
    transport->SubscribeReadyToSend(
        this, [this](bool ready) { OnReadyToSend(ready); });
    transport->SubscribeNetworkRouteChanged(
        this, [this](std::optional<NetworkRoute> route) {
          OnNetworkRouteChanged(route);
        });
    if (transport->rtp_packet_transport()) {
      transport->rtp_packet_transport()->SubscribeSentPacket(
          this, [this](PacketTransportInternal* transport,
                       const SentPacketInfo& info) {
            OnSentPacket(transport, info);
          });
    }

    if (transport->rtcp_packet_transport()) {
      transport->rtcp_packet_transport()->SubscribeSentPacket(
          this, [this](PacketTransportInternal* transport,
                       const SentPacketInfo& info) {
            OnSentPacket(transport, info);
          });
    }
  }

  bool ready() const { return ready_; }
  void OnReadyToSend(bool ready) { ready_ = ready; }

  std::optional<NetworkRoute> network_route() { return network_route_; }
  void OnNetworkRouteChanged(std::optional<NetworkRoute> network_route) {
    network_route_ = network_route;
  }

  void OnSentPacket(PacketTransportInternal* packet_transport,
                    const SentPacketInfo& sent_packet) {
    if (packet_transport == transport_->rtp_packet_transport()) {
      rtp_transport_sent_count_++;
    } else {
      ASSERT_EQ(transport_->rtcp_packet_transport(), packet_transport);
      rtcp_transport_sent_count_++;
    }
  }

  int rtp_transport_sent_count() { return rtp_transport_sent_count_; }

  int rtcp_transport_sent_count() { return rtcp_transport_sent_count_; }

 private:
  int rtp_transport_sent_count_ = 0;
  int rtcp_transport_sent_count_ = 0;
  RtpTransport* transport_ = nullptr;
  bool ready_ = false;
  std::optional<NetworkRoute> network_route_;
};

TEST(RtpTransportTest, SettingRtcpAndRtpSignalsReady) {
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());

  SignalObserver observer(&transport);
  FakePacketTransport fake_rtcp(env, "fake_rtcp");
  fake_rtcp.SetWritable(true);
  FakePacketTransport fake_rtp(env, "fake_rtp");
  fake_rtp.SetWritable(true);

  transport.SetRtcpPacketTransport(&fake_rtcp);  // rtcp ready
  EXPECT_FALSE(observer.ready());
  transport.SetRtpPacketTransport(&fake_rtp);  // rtp ready
  EXPECT_TRUE(observer.ready());
}

TEST(RtpTransportTest, SettingRtpAndRtcpSignalsReady) {
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  SignalObserver observer(&transport);
  FakePacketTransport fake_rtcp(env, "fake_rtcp");
  fake_rtcp.SetWritable(true);
  FakePacketTransport fake_rtp(env, "fake_rtp");
  fake_rtp.SetWritable(true);

  transport.SetRtpPacketTransport(&fake_rtp);  // rtp ready
  EXPECT_FALSE(observer.ready());
  transport.SetRtcpPacketTransport(&fake_rtcp);  // rtcp ready
  EXPECT_TRUE(observer.ready());
}

TEST(RtpTransportTest, SettingRtpWithRtcpMuxEnabledSignalsReady) {
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxEnabled, env.field_trials());
  SignalObserver observer(&transport);
  FakePacketTransport fake_rtp(env, "fake_rtp");
  fake_rtp.SetWritable(true);

  transport.SetRtpPacketTransport(&fake_rtp);  // rtp ready
  EXPECT_TRUE(observer.ready());
}

TEST(RtpTransportTest, DisablingRtcpMuxSignalsNotReady) {
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxEnabled, env.field_trials());
  SignalObserver observer(&transport);
  FakePacketTransport fake_rtp(env, "fake_rtp");
  fake_rtp.SetWritable(true);

  transport.SetRtpPacketTransport(&fake_rtp);  // rtp ready
  EXPECT_TRUE(observer.ready());

  transport.SetRtcpMuxEnabled(false);
  EXPECT_FALSE(observer.ready());
}

TEST(RtpTransportTest, EnablingRtcpMuxSignalsReady) {
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  SignalObserver observer(&transport);
  FakePacketTransport fake_rtp(env, "fake_rtp");
  fake_rtp.SetWritable(true);

  transport.SetRtpPacketTransport(&fake_rtp);  // rtp ready
  EXPECT_FALSE(observer.ready());

  transport.SetRtcpMuxEnabled(true);
  EXPECT_TRUE(observer.ready());
}

// Tests the SignalNetworkRoute is fired when setting a packet transport.
TEST(RtpTransportTest, SetRtpTransportWithNetworkRouteChanged) {
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  SignalObserver observer(&transport);
  FakePacketTransport fake_rtp(env, "fake_rtp");

  EXPECT_FALSE(observer.network_route());

  NetworkRoute network_route;
  // Set a non-null RTP transport with a new network route.
  network_route.connected = true;
  network_route.local = RouteEndpoint::CreateWithNetworkId(kLocalNetId);
  network_route.remote = RouteEndpoint::CreateWithNetworkId(kRemoteNetId);
  network_route.last_sent_packet_id = kLastPacketId;
  network_route.packet_overhead = kTransportOverheadPerPacket;
  fake_rtp.SetNetworkRoute(std::optional<NetworkRoute>(network_route));
  transport.SetRtpPacketTransport(&fake_rtp);
  ASSERT_TRUE(observer.network_route());
  EXPECT_TRUE(observer.network_route()->connected);
  EXPECT_EQ(kLocalNetId, observer.network_route()->local.network_id());
  EXPECT_EQ(kRemoteNetId, observer.network_route()->remote.network_id());
  EXPECT_EQ(kTransportOverheadPerPacket,
            observer.network_route()->packet_overhead);
  EXPECT_EQ(kLastPacketId, observer.network_route()->last_sent_packet_id);

  // Set a null RTP transport.
  transport.SetRtpPacketTransport(nullptr);
  EXPECT_FALSE(observer.network_route());
}

TEST(RtpTransportTest, SetRtcpTransportWithNetworkRouteChanged) {
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  SignalObserver observer(&transport);
  FakePacketTransport fake_rtcp(env, "fake_rtcp");

  EXPECT_FALSE(observer.network_route());

  NetworkRoute network_route;
  // Set a non-null RTCP transport with a new network route.
  network_route.connected = true;
  network_route.local = RouteEndpoint::CreateWithNetworkId(kLocalNetId);
  network_route.remote = RouteEndpoint::CreateWithNetworkId(kRemoteNetId);
  network_route.last_sent_packet_id = kLastPacketId;
  network_route.packet_overhead = kTransportOverheadPerPacket;
  fake_rtcp.SetNetworkRoute(std::optional<NetworkRoute>(network_route));
  transport.SetRtcpPacketTransport(&fake_rtcp);
  ASSERT_TRUE(observer.network_route());
  EXPECT_TRUE(observer.network_route()->connected);
  EXPECT_EQ(kLocalNetId, observer.network_route()->local.network_id());
  EXPECT_EQ(kRemoteNetId, observer.network_route()->remote.network_id());
  EXPECT_EQ(kTransportOverheadPerPacket,
            observer.network_route()->packet_overhead);
  EXPECT_EQ(kLastPacketId, observer.network_route()->last_sent_packet_id);

  // Set a null RTCP transport.
  transport.SetRtcpPacketTransport(nullptr);
  EXPECT_FALSE(observer.network_route());
}

// Test that RTCP packets are sent over correct transport based on the RTCP-mux
// status.
TEST(RtpTransportTest, RtcpPacketSentOverCorrectTransport) {
  // If the RTCP-mux is not enabled, RTCP packets are expected to be sent over
  // the RtcpPacketTransport.
  test::RunLoop thread;
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  FakePacketTransport fake_rtcp(env, "fake_rtcp");
  FakePacketTransport fake_rtp(env, "fake_rtp");
  transport.SetRtcpPacketTransport(&fake_rtcp);  // rtcp ready
  transport.SetRtpPacketTransport(&fake_rtp);    // rtp ready
  SignalObserver observer(&transport);

  fake_rtp.SetDestination(&fake_rtp, true);
  fake_rtcp.SetDestination(&fake_rtcp, true);

  CopyOnWriteBuffer packet;
  EXPECT_TRUE(transport.SendRtcpPacket(&packet, AsyncSocketPacketOptions(), 0));
  EXPECT_THAT(
      WaitUntil([&] { return observer.rtcp_transport_sent_count(); }, Eq(1)),
      IsRtcOk());

  // The RTCP packets are expected to be sent over RtpPacketTransport if
  // RTCP-mux is enabled.
  transport.SetRtcpMuxEnabled(true);
  EXPECT_TRUE(transport.SendRtcpPacket(&packet, AsyncSocketPacketOptions(), 0));
  EXPECT_THAT(
      WaitUntil([&] { return observer.rtp_transport_sent_count(); }, Eq(1)),
      IsRtcOk());
  EXPECT_EQ(1, observer.rtp_transport_sent_count());
}

TEST(RtpTransportTest, ChangingReadyToSendStateOnlySignalsWhenChanged) {
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxEnabled, env.field_trials());
  TransportObserver observer(&transport);
  FakePacketTransport fake_rtp(env, "fake_rtp");
  fake_rtp.SetWritable(true);

  // State changes, so we should signal.
  transport.SetRtpPacketTransport(&fake_rtp);
  EXPECT_EQ(observer.ready_to_send_signal_count(), 1);

  // State does not change, so we should not signal.
  transport.SetRtpPacketTransport(&fake_rtp);
  EXPECT_EQ(observer.ready_to_send_signal_count(), 1);

  // State does not change, so we should not signal.
  transport.SetRtcpMuxEnabled(true);
  EXPECT_EQ(observer.ready_to_send_signal_count(), 1);

  // State changes, so we should signal.
  transport.SetRtcpMuxEnabled(false);
  EXPECT_EQ(observer.ready_to_send_signal_count(), 2);
}

TEST(RtpTransportTest, RegisterAndUnregisterRtpHeaderExtensionMap) {
  test::RunLoop thread;
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  RtpHeaderExtensions extensions1 = {RtpExtension(
      "urn:ietf:params:rtp-hdrext:ssrc-audio-level", RtpHeaderExtensionId(1))};
  RtpHeaderExtensions extensions2 = {
      RtpExtension("urn:ietf:params:rtp-hdrext:ssrc-audio-level",
                   RtpHeaderExtensionId(1)),
      RtpExtension("http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time",
                   RtpHeaderExtensionId(2))};

  // Register the first map.
  transport.RegisterRtpHeaderExtensionMap("audio", extensions1);

  // Parse a packet with an extension from the first map.
  constexpr uint8_t kRtpData1[] = {0x90, 0x11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0xBE, 0xDE, 0, 1,
                                   // ID=1, len=0 (1 byte payload)
                                   0x10, 0x00, 0x00, 0x00};
  CopyOnWriteBuffer rtp_packet1(kRtpData1);
  RtpPacketReceived parsed_packet1;
  FakePacketTransport fake_rtp(env, "fake_rtp");
  fake_rtp.SetDestination(&fake_rtp, true);
  transport.SetRtpPacketTransport(&fake_rtp);
  TransportObserver observer(&transport);

  // Register sink so the packet can be "demuxed" and surfaced to the observer.
  RtpDemuxerCriteria demuxer_criteria;
  demuxer_criteria.payload_types().insert(0x11);
  transport.RegisterRtpDemuxerSink(demuxer_criteria, &observer);

  // Send the packet.
  fake_rtp.SendPacket(rtp_packet1.data<char>(), rtp_packet1.size(),
                      AsyncSocketPacketOptions(), 0);
  EXPECT_THAT(WaitUntil([&] { return observer.rtp_count(); }, Eq(1)),
              IsRtcOk());
  RTC_LOG(LS_INFO) << "Packet 1 received: " << observer.rtp_count();
  RTC_LOG(LS_INFO)
      << "Packet 1 has AudioLevelExtension: "
      << observer.last_recv_rtp_packet().HasExtension<AudioLevelExtension>();
  EXPECT_TRUE(
      observer.last_recv_rtp_packet().HasExtension<AudioLevelExtension>());

  // Register the second map (simulating BUNDLE).
  transport.RegisterRtpHeaderExtensionMap("video", extensions2);

  // Parse a packet with an extension from the second map.
  constexpr uint8_t kRtpData2[] = {0x90, 0x11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0xBE, 0xDE, 0, 1,
                                   // ID=2, len=2 (3 byte payload)
                                   0x22, 0x01, 0x02, 0x03};
  CopyOnWriteBuffer rtp_packet2(kRtpData2);

  fake_rtp.SendPacket(rtp_packet2.data<char>(), rtp_packet2.size(),
                      AsyncSocketPacketOptions(), 0);
  EXPECT_THAT(WaitUntil([&] { return observer.rtp_count(); }, Eq(2)),
              IsRtcOk());
  RTC_LOG(LS_INFO) << "Packet 2 received: " << observer.rtp_count();
  RTC_LOG(LS_INFO)
      << "Packet 2 has AbsoluteSendTime: "
      << observer.last_recv_rtp_packet().HasExtension<AbsoluteSendTime>();
  EXPECT_TRUE(observer.last_recv_rtp_packet().HasExtension<AbsoluteSendTime>());

  // Unregister the second map.
  transport.UnregisterRtpHeaderExtensionMap("video");

  // A packet with the second map's extension should no longer parse the
  // extension.
  fake_rtp.SendPacket(rtp_packet2.data<char>(), rtp_packet2.size(),
                      AsyncSocketPacketOptions(), 0);
  EXPECT_THAT(WaitUntil([&] { return observer.rtp_count(); }, Eq(3)),
              IsRtcOk());
  EXPECT_FALSE(
      observer.last_recv_rtp_packet().HasExtension<AbsoluteSendTime>());

  transport.UnregisterRtpDemuxerSink(&observer);
}

TEST(RtpTransportTest, VerifyRtpHeaderExtensionMapRejectsIdReassignment) {
  test::RunLoop loop;
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  RtpHeaderExtensions extensions1 = {RtpExtension(
      "urn:ietf:params:rtp-hdrext:ssrc-audio-level", RtpHeaderExtensionId(1))};
  RtpHeaderExtensions extensions2 = {
      RtpExtension("http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time",
                   RtpHeaderExtensionId(1))};

  // Registering the first map should succeed.
  EXPECT_TRUE(
      transport.RegisterRtpHeaderExtensionMap("audio", extensions1).ok());

  // Verifying a map that tries to reassign ID 1 to a different URI should fail.
  RTCError error = transport.VerifyRtpHeaderExtensionMap(extensions2);
  EXPECT_FALSE(error.ok());
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_PARAMETER);
  EXPECT_THAT(error.message(), HasSubstr("RTP extension ID reassignment"));

  // Registering the second map should also fail.
  error = transport.RegisterRtpHeaderExtensionMap("video", extensions2);
  EXPECT_FALSE(error.ok());
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_PARAMETER);
}

TEST(RtpTransportTest,
     VerifyRtpHeaderExtensionMapAllowsIdReuseAfterUnregister) {
  test::RunLoop loop;
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  RtpHeaderExtensions extensions1 = {RtpExtension(
      "urn:ietf:params:rtp-hdrext:ssrc-audio-level", RtpHeaderExtensionId(1))};
  RtpHeaderExtensions extensions2 = {
      RtpExtension("http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time",
                   RtpHeaderExtensionId(1))};

  // Registering the first map should succeed.
  EXPECT_TRUE(
      transport.RegisterRtpHeaderExtensionMap("audio", extensions1).ok());

  // Unregister the first map.
  transport.UnregisterRtpHeaderExtensionMap("audio");

  // Registering the second map with same ID but different URI should now
  // succeed!
  EXPECT_TRUE(
      transport.RegisterRtpHeaderExtensionMap("video", extensions2).ok());
}

// Test that SignalPacketReceived fires with rtcp=true when a RTCP packet is
// received.
TEST(RtpTransportTest, SignalDemuxedRtcp) {
  test::RunLoop thread;
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  FakePacketTransport fake_rtp(env, "fake_rtp");
  fake_rtp.SetDestination(&fake_rtp, true);
  transport.SetRtpPacketTransport(&fake_rtp);
  TransportObserver observer(&transport);

  // An rtcp packet.
  const unsigned char data[] = {0x80, 73, 0, 0};
  const int len = 4;
  const AsyncSocketPacketOptions options;
  const int flags = 0;
  fake_rtp.SendPacket(reinterpret_cast<const char*>(data), len, options, flags);
  EXPECT_THAT(WaitUntil([&] { return observer.rtcp_count(); }, Ge(1)),
              IsRtcOk());
  EXPECT_EQ(0, observer.rtp_count());
}

constexpr uint8_t kRtpData[] = {0x80, 0x11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr int kRtpLen = 12;

// Test that SignalPacketReceived fires with rtcp=false when a RTP packet with a
// handled payload type is received.
TEST(RtpTransportTest, SignalHandledRtpPayloadType) {
  test::RunLoop thread;
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  FakePacketTransport fake_rtp(env, "fake_rtp");
  fake_rtp.SetDestination(&fake_rtp, true);
  transport.SetRtpPacketTransport(&fake_rtp);
  TransportObserver observer(&transport);
  RtpDemuxerCriteria demuxer_criteria;
  // Add a handled payload type.
  demuxer_criteria.payload_types().insert(0x11);
  transport.RegisterRtpDemuxerSink(demuxer_criteria, &observer);

  // An rtp packet.
  const AsyncSocketPacketOptions options;
  const int flags = 0;
  Buffer rtp_data(kRtpData, kRtpLen);
  fake_rtp.SendPacket(rtp_data.data<char>(), kRtpLen, options, flags);
  EXPECT_THAT(WaitUntil([&] { return observer.rtp_count(); }, Eq(1)),
              IsRtcOk());
  EXPECT_EQ(0, observer.un_demuxable_rtp_count());
  EXPECT_EQ(0, observer.rtcp_count());
  // Remove the sink before destroying the transport.
  transport.UnregisterRtpDemuxerSink(&observer);
}

TEST(RtpTransportTest, ReceivedPacketEcnMarkingPropagatedToDemuxedPacket) {
  test::RunLoop thread;
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  // Setup FakePacketTransport to send packets to itself.
  FakePacketTransport fake_rtp(env, "fake_rtp");
  fake_rtp.SetDestination(&fake_rtp, true);
  transport.SetRtpPacketTransport(&fake_rtp);
  TransportObserver observer(&transport);
  RtpDemuxerCriteria demuxer_criteria;
  // Add a payload type of kRtpData.
  demuxer_criteria.payload_types().insert(0x11);
  transport.RegisterRtpDemuxerSink(demuxer_criteria, &observer);

  AsyncSocketPacketOptions options;
  options.ect_1 = true;
  const int flags = 0;
  Buffer rtp_data(kRtpData, kRtpLen);
  fake_rtp.SendPacket(rtp_data.data<char>(), kRtpLen, options, flags);
  ASSERT_THAT(WaitUntil([&] { return observer.rtp_count(); }, Eq(1)),
              IsRtcOk());
  EXPECT_EQ(observer.last_recv_rtp_packet().ecn(), EcnMarking::kEct1);

  transport.UnregisterRtpDemuxerSink(&observer);
}

TEST(RtpTransportTest, RtcpSentAsEct1IfReceivedRtpPacketAsEct1) {
  test::RunLoop thread;
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  // Setup FakePacketTransport to send packets to itself.
  FakePacketTransport fake_rtp(env, "fake_rtp");
  fake_rtp.SetDestination(&fake_rtp, true);
  transport.SetRtpPacketTransport(&fake_rtp);
  // Setup RTCP transport to send to another fake transport.
  FakePacketTransport fake_rtcp_recipient(env, "rtcp_recipient");
  FakePacketTransport fake_rtcp(env, "fake_rtcp");
  fake_rtcp.SetDestination(&fake_rtcp_recipient, true);
  transport.SetRtcpPacketTransport(&fake_rtcp);

  AsyncSocketPacketOptions rtp_options;
  rtp_options.ect_1 = true;
  const int flags = 0;
  Buffer rtp_data(kRtpData, kRtpLen);
  // Receive RTP packet as ECT1 (since `fake_rtp` sends packets to `transport`).
  fake_rtp.SendPacket(rtp_data.data<char>(), kRtpLen, rtp_options, flags);

  CopyOnWriteBuffer rtcp_packet_payload;
  rtcp_packet_payload.SetData("hello");
  transport.SendRtcpPacket(&rtcp_packet_payload, AsyncSocketPacketOptions(),
                           flags);
  EXPECT_TRUE(fake_rtcp.last_sent_packet_options().ect_1);

  // but if next RTP packet is received as not ect, RTCP is sent as not ECT.
  rtp_options.ect_1 = false;
  fake_rtp.SendPacket(rtp_data.data<char>(), kRtpLen, rtp_options, flags);
  transport.SendRtcpPacket(&rtcp_packet_payload, AsyncSocketPacketOptions(),
                           flags);
  EXPECT_FALSE(fake_rtcp.last_sent_packet_options().ect_1);
}

// Test that SignalPacketReceived does not fire when a RTP packet with an
// unhandled payload type is received.
TEST(RtpTransportTest, DontSignalUnhandledRtpPayloadType) {
  test::RunLoop thread;
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  FakePacketTransport fake_rtp(env, "fake_rtp");
  fake_rtp.SetDestination(&fake_rtp, true);
  transport.SetRtpPacketTransport(&fake_rtp);
  TransportObserver observer(&transport);
  RtpDemuxerCriteria demuxer_criteria;
  // Add an unhandled payload type.
  demuxer_criteria.payload_types().insert(0x12);
  transport.RegisterRtpDemuxerSink(demuxer_criteria, &observer);

  const AsyncSocketPacketOptions options;
  const int flags = 0;
  Buffer rtp_data(kRtpData, kRtpLen);
  fake_rtp.SendPacket(rtp_data.data<char>(), kRtpLen, options, flags);
  EXPECT_THAT(
      WaitUntil([&] { return observer.un_demuxable_rtp_count(); }, Eq(1)),
      IsRtcOk());
  EXPECT_EQ(0, observer.rtp_count());
  EXPECT_EQ(0, observer.rtcp_count());
  // Remove the sink before destroying the transport.
  transport.UnregisterRtpDemuxerSink(&observer);
}

TEST(RtpTransportTest, DontChangeReadyToSendStateOnSendFailure) {
  // ReadyToSendState should only care about if transport is writable.
  test::RunLoop thread;
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxEnabled, env.field_trials());
  TransportObserver observer(&transport);

  FakePacketTransport fake_rtp(env, "fake_rtp");
  fake_rtp.SetDestination(&fake_rtp, true);
  transport.SetRtpPacketTransport(&fake_rtp);
  fake_rtp.SetWritable(true);
  EXPECT_TRUE(observer.ready_to_send());
  EXPECT_EQ(observer.ready_to_send_signal_count(), 1);
  CopyOnWriteBuffer packet;
  EXPECT_TRUE(transport.SendRtpPacket(&packet, AsyncSocketPacketOptions(), 0));

  // The fake RTP will return -1 due to ENOTCONN.
  fake_rtp.SetError(ENOTCONN);
  EXPECT_FALSE(transport.SendRtpPacket(&packet, AsyncSocketPacketOptions(), 0));
  // Ready to send state should not have changed.
  EXPECT_TRUE(observer.ready_to_send());
  EXPECT_EQ(observer.ready_to_send_signal_count(), 1);
}

TEST(RtpTransportTest, RecursiveOnSentPacketDoesNotCrash) {
  const int kShortTimeout = 100;
  test::RunLoop loop;
  const Environment env = CreateTestEnvironment();
  RtpTransport transport(kMuxDisabled, env.field_trials());
  FakePacketTransport fake_rtp(env, "fake_rtp");
  transport.SetRtpPacketTransport(&fake_rtp);
  fake_rtp.SetDestination(&fake_rtp, true);
  TransportObserver observer(&transport);
  const AsyncSocketPacketOptions options;
  const int flags = 0;

  fake_rtp.SetWritable(true);
  observer.SetActionOnSentPacket([&]() {
    CopyOnWriteBuffer rtp_data(kRtpData);
    if (observer.sent_packet_count() < 2) {
      transport.SendRtpPacket(&rtp_data, options, flags);
    }
  });
  CopyOnWriteBuffer rtp_data(kRtpData);
  transport.SendRtpPacket(&rtp_data, options, flags);
  EXPECT_THAT(WaitUntil([&] { return observer.sent_packet_count(); }, Eq(2),
                        {.timeout = TimeDelta::Millis(kShortTimeout)}),
              IsRtcOk());
}

}  // namespace
}  // namespace webrtc
