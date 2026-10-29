/*
 *  Copyright 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/match.h"
#include "api/data_channel_interface.h"
#include "api/dtls_transport_interface.h"
#include "api/jsep.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/sctp_transport_interface.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "api/test/rtc_error_matchers.h"
#include "api/units/time_delta.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_info.h"
#include "p2p/test/test_turn_server.h"
#include "pc/media_session.h"
#include "pc/sctp_transport.h"
#include "pc/session_description.h"
#include "pc/test/fake_rtc_certificate_generator.h"
#include "pc/test/integration_test_helpers.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/virtual_socket_server.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/wait_until.h"

namespace webrtc {

namespace {

using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::SizeIs;
using ::testing::ValuesIn;

// All tests in this file require SCTP support.
#ifdef WEBRTC_HAVE_SCTP

#if defined(WEBRTC_ANDROID)
// Disable heavy tests running on low-end Android devices.
#define DISABLED_ON_ANDROID(t) DISABLED_##t
#else
#define DISABLED_ON_ANDROID(t) t
#endif

void VerifySctpState(PeerConnectionIntegrationWrapper* pc,
                     SctpTransportState expected_state) {
  auto sctp_transport = pc->pc()->GetSctpTransport();
  ASSERT_TRUE(sctp_transport);
  EXPECT_EQ(sctp_transport->Information().state(), expected_state);
}

class DataChannelIntegrationTest
    : public PeerConnectionIntegrationBaseTest,
      public ::testing::WithParamInterface<std::tuple<SdpSemantics, bool>> {
 protected:
  DataChannelIntegrationTest()
      : PeerConnectionIntegrationBaseTest(std::get<0>(GetParam())),
        allow_media_(std::get<1>(GetParam())) {}
  bool allow_media() { return allow_media_; }

  bool CreatePeerConnectionWrappers() {
    if (allow_media_) {
      return PeerConnectionIntegrationBaseTest::CreatePeerConnectionWrappers();
    }
    return PeerConnectionIntegrationBaseTest::
        CreatePeerConnectionWrappersWithoutMediaEngine();
  }

 private:
  // True if media is allowed to be added
  const bool allow_media_;
};

class DataChannelIntegrationTestPlanB
    : public PeerConnectionIntegrationBaseTest {
 protected:
  DataChannelIntegrationTestPlanB()
      : PeerConnectionIntegrationBaseTest(SdpSemantics::kPlanB_DEPRECATED) {}
};

class DataChannelIntegrationTestUnifiedPlan
    : public PeerConnectionIntegrationBaseTest {
 protected:
  DataChannelIntegrationTestUnifiedPlan()
      : PeerConnectionIntegrationBaseTest(SdpSemantics::kUnifiedPlan) {}
};

void MakeOfferHaveActiveDtlsRole(
    std::unique_ptr<SessionDescriptionInterface>& desc) {
  auto& transport_infos = desc->description()->transport_infos();
  for (auto& transport_info : transport_infos) {
    transport_info.description.connection_role = CONNECTIONROLE_ACTIVE;
  }
}

void MakeOfferHavePassiveDtlsRole(
    std::unique_ptr<SessionDescriptionInterface>& desc) {
  auto& transport_infos = desc->description()->transport_infos();
  for (auto& transport_info : transport_infos) {
    transport_info.description.connection_role = CONNECTIONROLE_PASSIVE;
  }
}

// This test causes a PeerConnection to enter Disconnected state, and
// sends data on a DataChannel while disconnected.
// The data should be surfaced when the connection reestablishes.
TEST_P(DataChannelIntegrationTest, DataChannelWhileDisconnected) {
  CreatePeerConnectionWrappers();
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer(); }));
  std::string data1 = "hello first";
  caller()->data_channel()->Send(DataBuffer(data1));
  EXPECT_THAT(
      WaitUntil([&] { return callee()->data_observer()->last_message(); },
                Eq(data1)),
      IsRtcOk());
  // Cause a network outage
  virtual_socket_server()->set_drop_probability(1.0);
  EXPECT_THAT(
      WaitUntil([&] { return caller()->standardized_ice_connection_state(); },
                Eq(PeerConnectionInterface::kIceConnectionDisconnected),
                {.timeout = TimeDelta::Seconds(10)}),
      IsRtcOk());
  std::string data2 = "hello second";
  caller()->data_channel()->Send(DataBuffer(data2));
  // Remove the network outage. The connection should reestablish.
  virtual_socket_server()->set_drop_probability(0.0);
  EXPECT_THAT(
      WaitUntil([&] { return callee()->data_observer()->last_message(); },
                Eq(data2)),
      IsRtcOk());
}

// This test causes a PeerConnection to enter Disconnected state,
// sends data on a DataChannel while disconnected, and then triggers
// an ICE restart.
// The data should be surfaced when the connection reestablishes.
TEST_P(DataChannelIntegrationTest, DataChannelWhileDisconnectedIceRestart) {
  CreatePeerConnectionWrappers();
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer(); }));
  std::string data1 = "hello first";
  caller()->data_channel()->Send(DataBuffer(data1));
  EXPECT_THAT(
      WaitUntil([&] { return callee()->data_observer()->last_message(); },
                Eq(data1)),
      IsRtcOk());
  // Cause a network outage
  virtual_socket_server()->set_drop_probability(1.0);
  ASSERT_THAT(
      WaitUntil([&] { return caller()->standardized_ice_connection_state(); },
                Eq(PeerConnectionInterface::kIceConnectionDisconnected),
                {.timeout = TimeDelta::Seconds(10)}),
      IsRtcOk());
  std::string data2 = "hello second";
  caller()->data_channel()->Send(DataBuffer(data2));

  // Trigger an ICE restart. The signaling channel is not affected by
  // the network outage.
  caller()->SetOfferAnswerOptions(IceRestartOfferAnswerOptions());
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Remove the network outage. The connection should reestablish.
  virtual_socket_server()->set_drop_probability(0.0);
  EXPECT_THAT(
      WaitUntil([&] { return callee()->data_observer()->last_message(); },
                Eq(data2)),
      IsRtcOk());
}

// This test sets up a call between two parties with audio, video and an SCTP
// data channel.
TEST_P(DataChannelIntegrationTest, EndToEndCallWithSctpDataChannel) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Expect that data channel created on caller side will show up for callee as
  // well.
  caller()->CreateDataChannel();
  if (allow_media()) {
    caller()->AddAudioVideoTracks();
    callee()->AddAudioVideoTracks();
  }
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  if (allow_media()) {
    // Ensure the existence of the SCTP data channel didn't impede audio/video.
    MediaExpectations media_expectations;
    media_expectations.ExpectBidirectionalAudioAndVideo();
    ASSERT_TRUE(ExpectNewFrames(media_expectations));
  }
  // Caller data channel should already exist (it created one). Callee data
  // channel may not exist yet, since negotiation happens in-band, not in SDP.
  ASSERT_NE(nullptr, caller()->data_channel());
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  EXPECT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));

  // Ensure data can be sent in both directions.
  std::string data = "hello world";
  caller()->data_channel()->Send(DataBuffer(data));
  EXPECT_THAT(
      WaitUntil([&] { return callee()->data_observer()->last_message(); },
                Eq(data)),
      IsRtcOk());
  callee()->data_channel()->Send(DataBuffer(data));
  EXPECT_THAT(
      WaitUntil([&] { return caller()->data_observer()->last_message(); },
                Eq(data)),
      IsRtcOk());
}

// This test sets up a call between two parties with an SCTP
// data channel only, and sends messages of various sizes.
TEST_P(DataChannelIntegrationTest,
       EndToEndCallWithSctpDataChannelVariousSizes) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Expect that data channel created on caller side will show up for callee as
  // well.
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Caller data channel should already exist (it created one). Callee data
  // channel may not exist yet, since negotiation happens in-band, not in SDP.
  ASSERT_NE(nullptr, caller()->data_channel());
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  EXPECT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));

  // Expect that all sizes under kSctpSendBufferSize(256 * 1024) to be sent
  // without any issue.
  for (int message_size = 1; message_size <= kSctpSendBufferSize;
       message_size *= 2) {
    std::string data(message_size, 'a');
    caller()->data_channel()->Send(DataBuffer(data));
    EXPECT_THAT(
        WaitUntil([&] { return callee()->data_observer()->last_message(); },
                  Eq(data)),
        IsRtcOk());
    callee()->data_channel()->Send(DataBuffer(data));
    EXPECT_THAT(
        WaitUntil([&] { return caller()->data_observer()->last_message(); },
                  Eq(data)),
        IsRtcOk());
  }
  // Specifically probe the area around the MTU size.
  for (int message_size = 1100; message_size < 1300; message_size += 1) {
    std::string data(message_size, 'a');
    caller()->data_channel()->Send(DataBuffer(data));
    EXPECT_THAT(
        WaitUntil([&] { return callee()->data_observer()->last_message(); },
                  Eq(data)),
        IsRtcOk());
    callee()->data_channel()->Send(DataBuffer(data));
    EXPECT_THAT(
        WaitUntil([&] { return caller()->data_observer()->last_message(); },
                  Eq(data)),
        IsRtcOk());
  }
  caller()->data_channel()->Close();

  EXPECT_THAT(WaitUntil([&] { return caller()->data_observer()->state(); },
                        Eq(DataChannelInterface::kClosed)),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return callee()->data_observer()->state(); },
                        Eq(DataChannelInterface::kClosed)),
              IsRtcOk());
}

// This test sets up a call between two parties with an SCTP
// data channel only, and sends a message exceeding the default size limit
// kSctpSendBufferSize(256 * 1024). We expect the Send method returns
// false and the channel closes.
TEST_P(DataChannelIntegrationTest,
       EndToEndCallWithSctpDataChannelOversizedMessage) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Expect that data channel created on caller side will show up for callee as
  // well.
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Caller data channel should already exist (it created one). Callee data
  // channel may not exist yet, since negotiation happens in-band, not in SDP.
  ASSERT_NE(nullptr, caller()->data_channel());
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  EXPECT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));

  // By default, SDP would set kSctpSendBufferSize as the size limit for the
  // transport. Expect that a longer message will not be sent and cause the
  // channel to be closed by error.
  std::string data(kSctpSendBufferSize + 1, 'a');
  EXPECT_FALSE(caller()->data_channel()->Send(DataBuffer(data)));
  EXPECT_EQ(caller()->data_channel()->state(), DataChannelInterface::kClosed);
  RTCError last_error = caller()->data_channel()->error();
  EXPECT_FALSE(last_error.ok());
  EXPECT_FALSE(std::string(last_error.message()).empty());
  EXPECT_EQ(RTCErrorType::NETWORK_ERROR, last_error.type());
}

// This test sets up a call between two parties with an SCTP
// data channel only, and sends enough messages to fill the queue and then
// closes on the caller. We expect the state to transition to closed on both
// caller and callee.
TEST_P(DataChannelIntegrationTest, EndToEndCallWithSctpDataChannelFullBuffer) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Expect that data channel created on caller side will show up for callee as
  // well.
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Caller data channel should already exist (it created one). Callee data
  // channel may not exist yet, since negotiation happens in-band, not in SDP.
  ASSERT_NE(nullptr, caller()->data_channel());
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  EXPECT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));

  std::string data(256 * 1024, 'a');
  for (size_t queued_size = 0;
       queued_size < DataChannelInterface::MaxSendQueueSize();
       queued_size += data.size()) {
    caller()->data_channel()->SendAsync(DataBuffer(data), nullptr);
  }

  caller()->data_channel()->Close();

  DataChannelInterface::DataState expected_states[] = {
      DataChannelInterface::DataState::kConnecting,
      DataChannelInterface::DataState::kOpen,
      DataChannelInterface::DataState::kClosing,
      DataChannelInterface::DataState::kClosed};

  // Debug data channels are very slow, use a long timeout for those slow,
  // heavily parallelized runs.
  EXPECT_THAT(WaitUntil([&] { return caller()->data_observer()->state(); },
                        Eq(DataChannelInterface::DataState::kClosed),
                        {.timeout = kLongTimeout}),
              IsRtcOk());
  EXPECT_THAT(caller()->data_observer()->states(),
              ::testing::ElementsAreArray(expected_states));

  EXPECT_THAT(WaitUntil([&] { return callee()->data_observer()->state(); },
                        Eq(DataChannelInterface::DataState::kClosed)),
              IsRtcOk());
  EXPECT_THAT(callee()->data_observer()->states(),
              ::testing::ElementsAreArray(expected_states));
}

// This test sets up a call between two parties with an SCTP
// data channel only, and sends empty messages
TEST_P(DataChannelIntegrationTest,
       EndToEndCallWithSctpDataChannelEmptyMessages) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Expect that data channel created on caller side will show up for callee as
  // well.
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Caller data channel should already exist (it created one). Callee data
  // channel may not exist yet, since negotiation happens in-band, not in SDP.
  ASSERT_NE(nullptr, caller()->data_channel());
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  EXPECT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));

  // Ensure data can be sent in both directions.
  // Sending empty string data
  std::string data = "";
  caller()->data_channel()->Send(DataBuffer(data));
  EXPECT_THAT(
      WaitUntil(
          [&] { return callee()->data_observer()->received_message_count(); },
          Eq(1u)),
      IsRtcOk());
  EXPECT_TRUE(callee()->data_observer()->last_message().empty());
  EXPECT_FALSE(callee()->data_observer()->messages().back().binary);
  callee()->data_channel()->Send(DataBuffer(data));
  EXPECT_THAT(
      WaitUntil(
          [&] { return caller()->data_observer()->received_message_count(); },
          Eq(1u)),
      IsRtcOk());
  EXPECT_TRUE(caller()->data_observer()->last_message().empty());
  EXPECT_FALSE(caller()->data_observer()->messages().back().binary);

  // Sending empty binary data
  CopyOnWriteBuffer empty_buffer;
  caller()->data_channel()->Send(DataBuffer(empty_buffer, true));
  EXPECT_THAT(
      WaitUntil(
          [&] { return callee()->data_observer()->received_message_count(); },
          Eq(2u)),
      IsRtcOk());
  EXPECT_TRUE(callee()->data_observer()->last_message().empty());
  EXPECT_TRUE(callee()->data_observer()->messages().back().binary);
  callee()->data_channel()->Send(DataBuffer(empty_buffer, true));
  EXPECT_THAT(
      WaitUntil(
          [&] { return caller()->data_observer()->received_message_count(); },
          Eq(2u)),
      IsRtcOk());
  EXPECT_TRUE(caller()->data_observer()->last_message().empty());
  EXPECT_TRUE(caller()->data_observer()->messages().back().binary);
}

TEST_P(DataChannelIntegrationTest,
       EndToEndCallWithSctpDataChannelLowestSafeMtu) {
  // The lowest payload size limit that's tested and found safe for this
  // application. Note that this is not the safe limit under all conditions;
  // in particular, the default is not the largest DTLS signature, and
  // this test does not use TURN.
  const size_t kLowestSafePayloadSizeLimit = 1225;

  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Expect that data channel created on caller side will show up for callee as
  // well.
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Caller data channel should already exist (it created one). Callee data
  // channel may not exist yet, since negotiation happens in-band, not in SDP.
  ASSERT_NE(nullptr, caller()->data_channel());
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  EXPECT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));

  virtual_socket_server()->set_max_udp_payload(kLowestSafePayloadSizeLimit);
  for (int message_size = 1140; message_size < 1240; message_size += 1) {
    std::string data(message_size, 'a');
    caller()->data_channel()->Send(DataBuffer(data));
    ASSERT_THAT(
        WaitUntil([&] { return callee()->data_observer()->last_message(); },
                  Eq(data)),
        IsRtcOk());
    callee()->data_channel()->Send(DataBuffer(data));
    ASSERT_THAT(
        WaitUntil([&] { return caller()->data_observer()->last_message(); },
                  Eq(data)),
        IsRtcOk());
  }
}

// This test verifies that lowering the MTU of the connection will cause
// the datachannel to not transmit reliably.
// The purpose of this test is to ensure that we know how a too-small MTU
// error manifests itself.
TEST_P(DataChannelIntegrationTest, EndToEndCallWithSctpDataChannelHarmfulMtu) {
  // The lowest payload size limit that's tested and found safe for this
  // application in this configuration (see test above).
  const size_t kLowestSafePayloadSizeLimit = 1225;
  // The size of the smallest message that fails to be delivered.
  const size_t kMessageSizeThatIsNotDelivered = 1157;

  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_NE(nullptr, caller()->data_channel());
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  EXPECT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));

  if (caller()->tls_version() == kDtls13VersionBytes) {
    ASSERT_EQ(caller()->tls_version(), kDtls13VersionBytes);
    GTEST_SKIP() << "DTLS1.3 fragments packets larger than MTU";
  }

  virtual_socket_server()->set_max_udp_payload(kLowestSafePayloadSizeLimit - 1);
  // Probe for an undelivered or slowly delivered message. The exact
  // size limit seems to be dependent on the message history, so make the
  // code easily able to find the current value.
  bool failure_seen = false;
  for (size_t message_size = 1110; message_size < 1400; message_size++) {
    const size_t message_count =
        callee()->data_observer()->received_message_count();
    const std::string data(message_size, 'a');
    caller()->data_channel()->Send(DataBuffer(data));
    // Wait a very short time for the message to be delivered.
    // Note: Waiting only 10 ms is too short for Windows bots; they will
    // flakily fail at a random frame.
    callee()->data_observer()->set_on_message_callback(
        [&](const DataBuffer&) { run_loop().Quit(); });
    run_loop().RunFor(TimeDelta::Millis(100));
    callee()->data_observer()->set_on_message_callback(nullptr);
    if (callee()->data_observer()->received_message_count() == message_count) {
      ASSERT_EQ(kMessageSizeThatIsNotDelivered, message_size);
      failure_seen = true;
      break;
    }
  }
  ASSERT_TRUE(failure_seen);
}

// Ensure that when the callee closes an SCTP data channel, the closing
// procedure results in the data channel being closed for the caller as well.
TEST_P(DataChannelIntegrationTest, CalleeClosesSctpDataChannel) {
  // Same procedure as above test.
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  if (allow_media()) {
    caller()->AddAudioVideoTracks();
    callee()->AddAudioVideoTracks();
  }
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_NE(nullptr, caller()->data_channel());
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  ASSERT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));

  // Close the data channel on the callee side, and wait for it to reach the
  // "closed" state on both sides.
  callee()->data_channel()->Close();

  DataChannelInterface::DataState expected_states[] = {
      DataChannelInterface::DataState::kConnecting,
      DataChannelInterface::DataState::kOpen,
      DataChannelInterface::DataState::kClosing,
      DataChannelInterface::DataState::kClosed};

  EXPECT_THAT(WaitUntil([&] { return caller()->data_observer()->state(); },
                        Eq(DataChannelInterface::DataState::kClosed)),
              IsRtcOk());
  EXPECT_THAT(caller()->data_observer()->states(),
              ::testing::ElementsAreArray(expected_states));

  EXPECT_THAT(WaitUntil([&] { return callee()->data_observer()->state(); },
                        Eq(DataChannelInterface::DataState::kClosed)),
              IsRtcOk());
  EXPECT_THAT(callee()->data_observer()->states(),
              ::testing::ElementsAreArray(expected_states));
}

TEST_P(DataChannelIntegrationTest, SctpDataChannelConfigSentToOtherSide) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  DataChannelInit init;
  init.id = 53;
  init.maxRetransmits = 52;
  caller()->CreateDataChannel("data-channel", &init);
  if (allow_media()) {
    caller()->AddAudioVideoTracks();
    callee()->AddAudioVideoTracks();
  }
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));
  // Since "negotiated" is false, the "id" parameter should be ignored.
  EXPECT_NE(init.id, callee()->data_channel()->id());
  EXPECT_EQ("data-channel", callee()->data_channel()->label());
  EXPECT_EQ(init.maxRetransmits,
            *callee()->data_channel()->maxRetransmitsOpt());
  EXPECT_FALSE(callee()->data_channel()->negotiated());
}

// Test sctp's ability to process unordered data stream, where data actually
// arrives out of order using simulated delays. Previously there have been some
// bugs in this area.
TEST_P(DataChannelIntegrationTest, StressTestUnorderedSctpDataChannel) {
  // Introduce random network delays.
  // Otherwise it's not a true "unordered" test.
  virtual_socket_server()->set_delay_mean(20);
  virtual_socket_server()->set_delay_stddev(5);
  virtual_socket_server()->UpdateDelayDistribution();
  // Normal procedure, but with unordered data channel config.
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  DataChannelInit init;
  init.ordered = false;
  caller()->CreateDataChannel(&init);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_NE(nullptr, caller()->data_channel());
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  ASSERT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));

  static constexpr int kNumMessages = 100;
  // Deliberately chosen to be larger than the MTU so messages get fragmented.
  static constexpr size_t kMaxMessageSize = 4096;
  // Create and send random messages.
  std::vector<std::string> sent_messages;
  for (int i = 0; i < kNumMessages; ++i) {
    size_t length =
        (rand() % kMaxMessageSize) + 1;  // NOLINT (rand_r instead of rand)
    std::string message;
    ASSERT_TRUE(CreateRandomString(length, &message));
    caller()->data_channel()->Send(DataBuffer(message));
    callee()->data_channel()->Send(DataBuffer(message));
    sent_messages.push_back(message);
  }

  // Wait for all messages to be received.
  EXPECT_THAT(
      WaitUntil(
          [&] { return caller()->data_observer()->received_message_count(); },
          Eq(checked_cast<size_t>(kNumMessages))),
      IsRtcOk());
  EXPECT_THAT(
      WaitUntil(
          [&] { return callee()->data_observer()->received_message_count(); },
          Eq(checked_cast<size_t>(kNumMessages))),
      IsRtcOk());

  // Sort and compare to make sure none of the messages were corrupted.
  std::vector<std::string> caller_received_messages;
  absl::c_transform(caller()->data_observer()->messages(),
                    std::back_inserter(caller_received_messages),
                    [](const auto& a) { return a.data; });

  std::vector<std::string> callee_received_messages;
  absl::c_transform(callee()->data_observer()->messages(),
                    std::back_inserter(callee_received_messages),
                    [](const auto& a) { return a.data; });

  absl::c_sort(sent_messages);
  absl::c_sort(caller_received_messages);
  absl::c_sort(callee_received_messages);
  EXPECT_EQ(sent_messages, caller_received_messages);
  EXPECT_EQ(sent_messages, callee_received_messages);
}

// Repeatedly open and close data channels on a peer connection to check that
// the channels are properly negotiated and SCTP stream IDs properly recycled.
TEST_P(DataChannelIntegrationTest, StressTestOpenCloseChannelNoDelay) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  int channel_id = 0;
  const size_t kChannelCount = 8;
  const size_t kIterations = 10;
  bool has_negotiated = false;

  DataChannelInit init;
  for (size_t repeats = 0; repeats < kIterations; ++repeats) {
    RTC_LOG(LS_INFO) << "Iteration " << (repeats + 1) << "/" << kIterations;

    for (size_t i = 0; i < kChannelCount; ++i) {
      StringBuilder sb;
      sb << "channel-" << channel_id++;
      caller()->CreateDataChannel(sb.Release(), &init);
    }
    ASSERT_EQ(caller()->data_channels().size(), kChannelCount);

    if (!has_negotiated) {
      caller()->CreateAndSetAndSignalOffer();
      ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
      has_negotiated = true;
    }

    for (size_t i = 0; i < kChannelCount; ++i) {
      ASSERT_THAT(
          WaitUntil([&] { return caller()->data_channels()[i]->state(); },
                    Eq(DataChannelInterface::DataState::kOpen)),
          IsRtcOk());
      RTC_LOG(LS_INFO) << "Caller Channel "
                       << caller()->data_channels()[i]->label() << " with id "
                       << caller()->data_channels()[i]->id() << " is open.";
    }
    ASSERT_THAT(WaitUntil([&] { return callee()->data_channels().size(); },
                          Eq(kChannelCount)),
                IsRtcOk());
    for (size_t i = 0; i < kChannelCount; ++i) {
      ASSERT_THAT(
          WaitUntil([&] { return callee()->data_channels()[i]->state(); },
                    Eq(DataChannelInterface::DataState::kOpen)),
          IsRtcOk());
      RTC_LOG(LS_INFO) << "Callee Channel "
                       << callee()->data_channels()[i]->label() << " with id "
                       << callee()->data_channels()[i]->id() << " is open.";
    }

    // Closing from both sides to attempt creating races.
    // A real application would likely only close from one side.
    for (size_t i = 0; i < kChannelCount; ++i) {
      if (i % 3 == 0) {
        callee()->data_channels()[i]->Close();
        caller()->data_channels()[i]->Close();
      } else {
        caller()->data_channels()[i]->Close();
        callee()->data_channels()[i]->Close();
      }
    }

    for (size_t i = 0; i < kChannelCount; ++i) {
      ASSERT_THAT(
          WaitUntil([&] { return caller()->data_channels()[i]->state(); },
                    Eq(DataChannelInterface::DataState::kClosed)),
          IsRtcOk());
      ASSERT_THAT(
          WaitUntil([&] { return callee()->data_channels()[i]->state(); },
                    Eq(DataChannelInterface::DataState::kClosed)),
          IsRtcOk());
    }

    caller()->data_channels().clear();
    caller()->data_observers().clear();
    callee()->data_channels().clear();
    callee()->data_observers().clear();
  }
}

// Repeatedly open and close data channels on a peer connection to check that
// the channels are properly negotiated and SCTP stream IDs properly recycled.
// Some delay is added for better coverage.
TEST_P(DataChannelIntegrationTest, StressTestOpenCloseChannelWithDelay) {
  // Simulate some network delay
  virtual_socket_server()->set_delay_mean(20);
  virtual_socket_server()->set_delay_stddev(5);
  virtual_socket_server()->UpdateDelayDistribution();

  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  int channel_id = 0;
  const size_t kChannelCount = 8;
  const size_t kIterations = 10;
  bool has_negotiated = false;

  DataChannelInit init;
  for (size_t repeats = 0; repeats < kIterations; ++repeats) {
    RTC_LOG(LS_INFO) << "Iteration " << (repeats + 1) << "/" << kIterations;

    for (size_t i = 0; i < kChannelCount; ++i) {
      StringBuilder sb;
      sb << "channel-" << channel_id++;
      caller()->CreateDataChannel(sb.Release(), &init);
    }
    ASSERT_EQ(caller()->data_channels().size(), kChannelCount);

    if (!has_negotiated) {
      caller()->CreateAndSetAndSignalOffer();
      ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
      has_negotiated = true;
    }

    for (size_t i = 0; i < kChannelCount; ++i) {
      ASSERT_THAT(
          WaitUntil([&] { return caller()->data_channels()[i]->state(); },
                    Eq(DataChannelInterface::DataState::kOpen)),
          IsRtcOk());
      RTC_LOG(LS_INFO) << "Caller Channel "
                       << caller()->data_channels()[i]->label() << " with id "
                       << caller()->data_channels()[i]->id() << " is open.";
    }
    ASSERT_THAT(WaitUntil([&] { return callee()->data_channels().size(); },
                          Eq(kChannelCount)),
                IsRtcOk());
    for (size_t i = 0; i < kChannelCount; ++i) {
      ASSERT_THAT(
          WaitUntil([&] { return callee()->data_channels()[i]->state(); },
                    Eq(DataChannelInterface::DataState::kOpen)),
          IsRtcOk());
      RTC_LOG(LS_INFO) << "Callee Channel "
                       << callee()->data_channels()[i]->label() << " with id "
                       << callee()->data_channels()[i]->id() << " is open.";
    }

    // Closing from both sides to attempt creating races.
    // A real application would likely only close from one side.
    for (size_t i = 0; i < kChannelCount; ++i) {
      if (i % 3 == 0) {
        callee()->data_channels()[i]->Close();
        caller()->data_channels()[i]->Close();
      } else {
        caller()->data_channels()[i]->Close();
        callee()->data_channels()[i]->Close();
      }
    }

    for (size_t i = 0; i < kChannelCount; ++i) {
      ASSERT_THAT(
          WaitUntil([&] { return caller()->data_channels()[i]->state(); },
                    Eq(DataChannelInterface::DataState::kClosed)),
          IsRtcOk());
      ASSERT_THAT(
          WaitUntil([&] { return callee()->data_channels()[i]->state(); },
                    Eq(DataChannelInterface::DataState::kClosed)),
          IsRtcOk());
    }

    caller()->data_channels().clear();
    caller()->data_observers().clear();
    callee()->data_channels().clear();
    callee()->data_observers().clear();
  }
}

// This test sets up a call between two parties with audio, and video. When
// audio and video are setup and flowing, an SCTP data channel is negotiated.
TEST_P(DataChannelIntegrationTest, AddSctpDataChannelInSubsequentOffer) {
  // This test can't be performed without media.
  if (!allow_media()) {
    return;
  }
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Do initial offer/answer with audio/video.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Create data channel and do new offer and answer.
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Caller data channel should already exist (it created one). Callee data
  // channel may not exist yet, since negotiation happens in-band, not in SDP.
  ASSERT_NE(nullptr, caller()->data_channel());
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  EXPECT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));
  // Ensure data can be sent in both directions.
  std::string data = "hello world";
  caller()->data_channel()->Send(DataBuffer(data));
  EXPECT_THAT(
      WaitUntil([&] { return callee()->data_observer()->last_message(); },
                Eq(data)),
      IsRtcOk());
  callee()->data_channel()->Send(DataBuffer(data));
  EXPECT_THAT(
      WaitUntil([&] { return caller()->data_observer()->last_message(); },
                Eq(data)),
      IsRtcOk());
}

// Fixture for tests of draft-hancke-tsvwg-snap, which carries the SCTP-init
// cookie in the SDP (a=sctp-init) and is gated behind the WebRTC-Sctp-Snap
// field trial.
class DataChannelIntegrationTestWithSctpSnap
    : public PeerConnectionIntegrationBaseTest {
 protected:
  DataChannelIntegrationTestWithSctpSnap()
      : PeerConnectionIntegrationBaseTest(SdpSemantics::kUnifiedPlan) {
    // Must be set before the PeerConnectionWrappers are created.
    SetFieldTrials("WebRTC-Sctp-Snap/Enabled/");
  }
};

TEST_F(DataChannelIntegrationTestWithSctpSnap,
       EarlyDataChannelPacketsAreBufferedUntilAnswerApplied) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  // Phase 1: establish an audio/video connection (no data channel yet).
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));

  // Phase 2: add a data channel and send an offer with an sctp-init, but
  // capture the answer and never apply it on the caller.
  caller()->CreateDataChannel();
  callee()->CreateDataChannel();
  std::string captured_answer;
  caller()->SetReceivedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        sdp->ToString(&captured_answer);
        sdp = nullptr;
      });
  caller()->CreateAndSetAndSignalOffer();
  EXPECT_EQ(caller()->pc()->signaling_state(),
            PeerConnectionInterface::kHaveLocalOffer);
  EXPECT_THAT(captured_answer, Not(IsEmpty()));

  // Caller has no SCTP socket yet, callee has and is sending data
  // which must be cached by the caller.
  ASSERT_TRUE(WaitUntil([&] {
    auto transport = callee()->pc()->GetSctpTransport();
    return transport &&
           transport->Information().state() == SctpTransportState::kConnected;
  }));
  EXPECT_FALSE(caller()->data_observer()->IsOpen());
  EXPECT_TRUE(callee()->data_observer()->IsOpen());

  auto caller_cached_packet_count = [&]() -> size_t {
    return network_thread()->BlockingCall([&]() -> size_t {
      auto* sctp_transport =
          static_cast<SctpTransport*>(caller()->pc()->GetSctpTransport().get());
      if (!sctp_transport) {
        return 0;
      }
      return sctp_transport->internal()->EarlyReceivedPacketCountForTesting();
    });
  };

  // Before any application data is sent, the caller has cached exactly one
  // packet: the DCEP "open" message for the data channel.
  EXPECT_TRUE(WaitUntil([&] { return caller_cached_packet_count() == 1u; }));

  // Send many small numbered messages. The caller has no SCTP socket yet, so it
  // never acknowledges them; the callee keeps retransmitting and the caller
  // caches every (re)transmitted packet. The early-packet buffer therefore
  // fills to its cap and is bounded there, never growing beyond it.
  constexpr int kNumMessages = 64;
  for (int i = 0; i < kNumMessages; ++i) {
    callee()->data_channel()->Send(DataBuffer(std::to_string(i)));
  }
  EXPECT_TRUE(WaitUntil([&] { return caller_cached_packet_count() == 32u; }));

  // Phase 3: apply the captured answer. We expect two open data channels
  // on each side.
  caller()->SetReceivedSdpMunger(nullptr);
  caller()->ReceiveSdpMessage(SdpType::kAnswer, captured_answer);
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  ASSERT_THAT(WaitUntil([&] { return caller()->data_channels(); }, SizeIs(2)),
              IsRtcOk());
  for (const auto& observer : caller()->data_observers()) {
    EXPECT_TRUE(WaitUntil([&] { return observer->IsOpen(); }));
  }
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channels(); }, SizeIs(2)),
              IsRtcOk());
  for (const auto& observer : callee()->data_observers()) {
    EXPECT_TRUE(WaitUntil([&] { return observer->IsOpen(); }));
  }

  // The caller must receive all kNumMessages messages, in order, on the
  // channel negotiated in-band from the callee. Some of them buffered,
  // some as resends.
  MockDataChannelObserver* receiver = caller()->data_observers().back().get();
  ASSERT_TRUE(WaitUntil([&] {
    return static_cast<int>(receiver->received_message_count()) == kNumMessages;
  }));
  for (int i = 0; i < kNumMessages; ++i) {
    EXPECT_EQ(receiver->messages()[i].data, std::to_string(i));
  }
}

// Set up a connection initially just using SCTP data channels, later
// upgrading to audio/video, ensuring frames are received end-to-end.
// Effectively the inverse of the test above. This was broken in M57; see
// https://crbug.com/711243
TEST_P(DataChannelIntegrationTest, SctpDataChannelToAudioVideoUpgrade) {
  // This test can't be performed without media.
  if (!allow_media()) {
    return;
  }
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Do initial offer/answer with just data channel.
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Wait until data can be sent over the data channel.
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  ASSERT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));

  // Do subsequent offer/answer with two-way audio and video. Audio and video
  // should end up bundled on the DTLS/ICE transport already used for data.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

void MakeSpecCompliantSctpOffer(
    std::unique_ptr<SessionDescriptionInterface>& desc) {
  SctpDataContentDescription* dcd_offer =
      GetFirstSctpDataContentDescription(desc->description());
  // See https://crbug.com/webrtc/11211 - this function is a no-op
  ASSERT_TRUE(dcd_offer);
  dcd_offer->set_use_sctpmap(false);
  dcd_offer->set_protocol("UDP/DTLS/SCTP");
}

// Test that the data channel works when a spec-compliant SCTP m= section is
// offered (using "a=sctp-port" instead of "a=sctpmap", and using
// "UDP/DTLS/SCTP" as the protocol).
TEST_P(DataChannelIntegrationTest,
       DataChannelWorksWhenSpecCompliantSctpOfferReceived) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  caller()->SetGeneratedSdpMunger(MakeSpecCompliantSctpOffer);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  EXPECT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));

  // Ensure data can be sent in both directions.
  std::string data = "hello world";
  caller()->data_channel()->Send(DataBuffer(data));
  EXPECT_THAT(
      WaitUntil([&] { return callee()->data_observer()->last_message(); },
                Eq(data)),
      IsRtcOk());
  callee()->data_channel()->Send(DataBuffer(data));
  EXPECT_THAT(
      WaitUntil([&] { return caller()->data_observer()->last_message(); },
                Eq(data)),
      IsRtcOk());
}

// Test that after closing PeerConnections, they stop sending any packets
// (ICE, DTLS, RTP...).
TEST_P(DataChannelIntegrationTest, ClosingConnectionStopsPacketFlow) {
  // This test can't be performed without media.
  if (!allow_media()) {
    return;
  }
  // Set up audio/video/data, wait for some frames to be received.
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
  // Close PeerConnections.
  ClosePeerConnections();
  // Pump messages for a second, and ensure no new packets end up sent.
  uint32_t sent_packets_a = virtual_socket_server()->sent_packets();
  run_loop().RunFor(TimeDelta::Seconds(1));
  uint32_t sent_packets_b = virtual_socket_server()->sent_packets();
  EXPECT_EQ(sent_packets_a, sent_packets_b);
}

TEST_P(DataChannelIntegrationTest, DtlsRoleIsSetNormally) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  ASSERT_FALSE(caller()->pc()->GetSctpTransport());
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  ASSERT_TRUE(caller()->pc()->GetSctpTransport());
  ASSERT_TRUE(
      caller()->pc()->GetSctpTransport()->Information().dtls_transport());
  EXPECT_TRUE(caller()
                  ->pc()
                  ->GetSctpTransport()
                  ->Information()
                  .dtls_transport()
                  ->Information()
                  .role());
  EXPECT_EQ(caller()
                ->pc()
                ->GetSctpTransport()
                ->Information()
                .dtls_transport()
                ->Information()
                .role(),
            DtlsTransportTlsRole::kServer);
  EXPECT_EQ(callee()
                ->pc()
                ->GetSctpTransport()
                ->Information()
                .dtls_transport()
                ->Information()
                .role(),
            DtlsTransportTlsRole::kClient);
  // ID should be assigned according to the odd/even rule based on role;
  // client gets even numbers, server gets odd ones. RFC 8832 section 6.
  // TODO(hta): Test multiple channels.
  EXPECT_EQ(caller()->data_channel()->id(), 1);
}

TEST_P(DataChannelIntegrationTest, DtlsRoleIsSetWhenReversed) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  callee()->SetReceivedSdpMunger(MakeOfferHaveActiveDtlsRole);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  EXPECT_TRUE(caller()
                  ->pc()
                  ->GetSctpTransport()
                  ->Information()
                  .dtls_transport()
                  ->Information()
                  .role());
  EXPECT_EQ(caller()
                ->pc()
                ->GetSctpTransport()
                ->Information()
                .dtls_transport()
                ->Information()
                .role(),
            DtlsTransportTlsRole::kClient);
  EXPECT_EQ(callee()
                ->pc()
                ->GetSctpTransport()
                ->Information()
                .dtls_transport()
                ->Information()
                .role(),
            DtlsTransportTlsRole::kServer);
  // ID should be assigned according to the odd/even rule based on role;
  // client gets even numbers, server gets odd ones. RFC 8832 section 6.
  // TODO(hta): Test multiple channels.
  EXPECT_EQ(caller()->data_channel()->id(), 0);
}

TEST_P(DataChannelIntegrationTest,
       DtlsRoleIsSetWhenReversedWithChannelCollision) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();

  callee()->SetReceivedSdpMunger(
      [this](std::unique_ptr<SessionDescriptionInterface>& desc) {
        MakeOfferHaveActiveDtlsRole(desc);
        callee()->CreateDataChannel();
      });
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  ASSERT_THAT(
      WaitUntil([&] { return callee()->data_channels().size(); }, Eq(2U)),
      IsRtcOk());
  ASSERT_THAT(
      WaitUntil([&] { return caller()->data_channels().size(); }, Eq(2U)),
      IsRtcOk());
  EXPECT_TRUE(caller()
                  ->pc()
                  ->GetSctpTransport()
                  ->Information()
                  .dtls_transport()
                  ->Information()
                  .role());
  EXPECT_EQ(caller()
                ->pc()
                ->GetSctpTransport()
                ->Information()
                .dtls_transport()
                ->Information()
                .role(),
            DtlsTransportTlsRole::kClient);
  EXPECT_EQ(callee()
                ->pc()
                ->GetSctpTransport()
                ->Information()
                .dtls_transport()
                ->Information()
                .role(),
            DtlsTransportTlsRole::kServer);
  // ID should be assigned according to the odd/even rule based on role;
  // client gets even numbers, server gets odd ones. RFC 8832 section 6.
  ASSERT_EQ(caller()->data_channels().size(), 2U);
  ASSERT_EQ(callee()->data_channels().size(), 2U);
  EXPECT_EQ(caller()->data_channels()[0]->id(), 0);
  EXPECT_EQ(caller()->data_channels()[1]->id(), 1);
  EXPECT_EQ(callee()->data_channels()[0]->id(), 1);
  EXPECT_EQ(callee()->data_channels()[1]->id(), 0);
}

// Test that transport stats are generated by the RTCStatsCollector for a
// connection that only involves data channels. This is a regression test for
// crbug.com/826972.
TEST_P(DataChannelIntegrationTest,
       TransportStatsReportedForDataChannelOnlyConnection) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_channel(); }));

  auto caller_report = caller()->NewGetStats(run_loop());
  ASSERT_THAT(caller_report, NotNull());
  EXPECT_EQ(1u, caller_report->GetStatsOfType<RTCTransportStats>().size());
  auto callee_report = callee()->NewGetStats(run_loop());
  ASSERT_THAT(callee_report, NotNull());
  EXPECT_EQ(1u, callee_report->GetStatsOfType<RTCTransportStats>().size());
}

TEST_P(DataChannelIntegrationTest, CreateDataChannelInvalidatesStatsCache) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  auto first_report = caller()->NewGetStats(run_loop());
  ASSERT_THAT(first_report, NotNull());
  caller()->CreateDataChannel();
  auto second_report = caller()->NewGetStats(run_loop());
  ASSERT_THAT(second_report, NotNull());

  EXPECT_EQ(0u, first_report->GetStatsOfType<RTCDataChannelStats>().size());
  EXPECT_EQ(1u, second_report->GetStatsOfType<RTCDataChannelStats>().size());
}

TEST_P(DataChannelIntegrationTest, QueuedPacketsGetDeliveredInReliableMode) {
  CreatePeerConnectionWrappers();
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_channel(); }));

  caller()->data_channel()->Send(DataBuffer("hello first"));
  ASSERT_THAT(
      WaitUntil(
          [&] { return callee()->data_observer()->received_message_count(); },
          Eq(1u)),
      IsRtcOk());
  // Cause a temporary network outage
  virtual_socket_server()->set_drop_probability(1.0);
  for (int i = 1; i <= 10; i++) {
    caller()->data_channel()->Send(DataBuffer("Sent while blocked"));
  }
  // Nothing should be delivered during outage. Short wait.
  EXPECT_THAT(
      WaitUntil(
          [&] { return callee()->data_observer()->received_message_count(); },
          Eq(1u)),
      IsRtcOk());
  // Reverse outage
  virtual_socket_server()->set_drop_probability(0.0);
  // All packets should be delivered.
  EXPECT_THAT(
      WaitUntil(
          [&] { return callee()->data_observer()->received_message_count(); },
          Eq(11u)),
      IsRtcOk());
}

TEST_P(DataChannelIntegrationTest, QueuedPacketsGetDroppedInUnreliableMode) {
  CreatePeerConnectionWrappers();
  ConnectFakeSignaling();
  DataChannelInit init;
  init.maxRetransmits = 0;
  init.ordered = false;
  caller()->CreateDataChannel(&init);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_channel(); }));
  caller()->data_channel()->Send(DataBuffer("hello first"));
  ASSERT_THAT(
      WaitUntil(
          [&] { return callee()->data_observer()->received_message_count(); },
          Eq(1u)),
      IsRtcOk());
  // Cause a temporary network outage
  virtual_socket_server()->set_drop_probability(1.0);
  // Send a few packets. Note that all get dropped only when all packets
  // fit into the receiver receive window/congestion window, so that they
  // actually get sent.
  for (int i = 1; i <= 10; i++) {
    caller()->data_channel()->Send(DataBuffer("Sent while blocked"));
  }
  // Nothing should be delivered during outage.
  // We do a short wait to verify that delivery count is still 1.
  run_loop().RunFor(TimeDelta::Millis(10));
  EXPECT_EQ(1u, callee()->data_observer()->received_message_count());
  // Reverse the network outage.
  virtual_socket_server()->set_drop_probability(0.0);
  // Send a new packet, and wait for it to be delivered.
  caller()->data_channel()->Send(DataBuffer("After block"));
  EXPECT_THAT(
      WaitUntil([&] { return callee()->data_observer()->last_message(); },
                Eq("After block")),
      IsRtcOk());
  // Some messages should be lost, but first and last message should have
  // been delivered.
  // First, check that the protocol guarantee is preserved.
  EXPECT_GT(11u, callee()->data_observer()->received_message_count());
  EXPECT_LE(2u, callee()->data_observer()->received_message_count());
  // Then, check that observed behavior (lose all messages) has not changed
  EXPECT_EQ(2u, callee()->data_observer()->received_message_count());
}

TEST_P(DataChannelIntegrationTest,
       QueuedPacketsGetDroppedInLifetimeLimitedMode) {
  CreatePeerConnectionWrappers();
  ConnectFakeSignaling();
  DataChannelInit init;
  init.maxRetransmitTime = 1;
  init.ordered = false;
  caller()->CreateDataChannel(&init);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_channel(); }));
  caller()->data_channel()->Send(DataBuffer("hello first"));
  ASSERT_THAT(
      WaitUntil(
          [&] { return callee()->data_observer()->received_message_count(); },
          Eq(1u)),
      IsRtcOk());
  // Cause a temporary network outage
  virtual_socket_server()->set_drop_probability(1.0);
  for (int i = 1; i <= 200; i++) {
    caller()->data_channel()->Send(DataBuffer("Sent while blocked"));
  }
  // Nothing should be delivered during outage.
  // We do a short wait to verify that delivery count is still 1,
  // and to make sure max packet lifetime (which is in ms) is exceeded.
  run_loop().RunFor(TimeDelta::Millis(10));
  EXPECT_EQ(1u, callee()->data_observer()->received_message_count());
  // Reverse the network outage.
  virtual_socket_server()->set_drop_probability(0.0);
  // Send a new packet, and wait for it to be delivered.
  caller()->data_channel()->Send(DataBuffer("After block"));
  EXPECT_THAT(
      WaitUntil([&] { return callee()->data_observer()->last_message(); },
                Eq("After block")),
      IsRtcOk());
  // Some messages should be lost, but first and last message should have
  // been delivered.
  // First, check that the protocol guarantee is preserved.
  EXPECT_GT(202u, callee()->data_observer()->received_message_count());
  EXPECT_LE(2u, callee()->data_observer()->received_message_count());
  // Then, check that observed behavior (lose some messages) has not changed
  // DcSctp loses all messages. This is correct.
  EXPECT_EQ(2u, callee()->data_observer()->received_message_count());
}

TEST_P(DataChannelIntegrationTest,
       DISABLED_ON_ANDROID(SomeQueuedPacketsGetDroppedInMaxRetransmitsMode)) {
  CreatePeerConnectionWrappers();
  ConnectFakeSignaling();
  DataChannelInit init;
  init.maxRetransmits = 0;
  init.ordered = false;
  caller()->CreateDataChannel(&init);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_channel(); }));
  caller()->data_channel()->Send(DataBuffer("hello first"));
  ASSERT_THAT(
      WaitUntil(
          [&] { return callee()->data_observer()->received_message_count(); },
          Eq(1u)),
      IsRtcOk());
  // Cause a temporary network outage
  virtual_socket_server()->set_drop_probability(1.0);
  // Fill the SCTP socket buffer until queued data starts to build.
  constexpr size_t kBufferedDataInSctpSocket = 2'000'000;
  size_t packet_counter = 0;
  while (caller()->data_channel()->buffered_amount() <
             kBufferedDataInSctpSocket &&
         packet_counter < 10000) {
    packet_counter++;
    caller()->data_channel()->Send(DataBuffer("Sent while blocked"));
  }
  if (caller()->data_channel()->buffered_amount() > kBufferedDataInSctpSocket) {
    RTC_LOG(LS_INFO) << "Buffered data after " << packet_counter << " packets";
  } else {
    RTC_LOG(LS_INFO) << "No buffered data after " << packet_counter
                     << " packets";
  }
  // Nothing should be delivered during outage.
  // We do a short wait to verify that delivery count is still 1.
  run_loop().RunFor(TimeDelta::Millis(10));
  EXPECT_EQ(1u, callee()->data_observer()->received_message_count());
  // Reverse the network outage.
  virtual_socket_server()->set_drop_probability(0.0);
  // Send a new packet, and wait for it to be delivered.
  caller()->data_channel()->Send(DataBuffer("After block"));
  EXPECT_THAT(
      WaitUntil([&] { return callee()->data_observer()->last_message(); },
                Eq("After block")),
      IsRtcOk());
  // Some messages should be lost, but first and last message should have
  // been delivered.
  // Due to the fact that retransmissions are only counted when the packet
  // goes on the wire, NOT when they are stalled in queue due to
  // congestion, we expect some of the packets to be delivered, because
  // congestion prevented them from being sent.
  // Citation: https://tools.ietf.org/html/rfc7496#section-3.1

  // First, check that the protocol guarantee is preserved.
  EXPECT_GT(packet_counter,
            callee()->data_observer()->received_message_count());
  EXPECT_LE(2u, callee()->data_observer()->received_message_count());
  // Then, check that observed behavior (lose between 100 and 200 messages)
  // has not changed.
  // Usrsctp behavior is different on Android (177) and other platforms (122).
  // Dcsctp loses 432 packets.
  EXPECT_GT(2 + packet_counter - 100,
            callee()->data_observer()->received_message_count());
  EXPECT_LT(2 + packet_counter - 500,
            callee()->data_observer()->received_message_count());
}

TEST_P(DataChannelIntegrationTest, ChangingSctpPortIsNotAllowed) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  EXPECT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));

  std::unique_ptr<SessionDescriptionInterface> answer;
  caller()->SetReceivedSdpMunger(
      [&answer](std::unique_ptr<SessionDescriptionInterface>& desc) {
        // Change the SCTP port.
        ContentInfo* sctp_content = GetFirstDataContent(desc->description());
        ASSERT_TRUE(sctp_content);
        auto sctp_description = sctp_content->media_description()->as_sctp();
        ASSERT_TRUE(sctp_description);
        sctp_description->set_port(sctp_description->port() + 1);

        // Capture and suppress the answer.
        answer.reset(desc.release());
      });
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_THAT(answer, NotNull());

  EXPECT_FALSE(caller()->SetRemoteDescription(std::move(answer)));
  // Check the state of the SCTP transport.
  VerifySctpState(caller(), SctpTransportState::kClosed);
}

TEST_P(DataChannelIntegrationTest, ChangingSctpPortIsAllowedWithDtlsRestart) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_THAT(WaitUntil([&] { return callee()->data_channel(); }, Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(WaitUntil([&] { return caller()->data_observer()->IsOpen(); }));
  EXPECT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));

  // Recreate the second peerconnection.
  PeerConnectionDependencies dependencies(nullptr);
  std::unique_ptr<FakeRTCCertificateGenerator> cert_generator(
      new FakeRTCCertificateGenerator());
  cert_generator->use_alternate_key();
  dependencies.cert_generator = std::move(cert_generator);
  SetCalleePcWrapperAndReturnCurrent(CreatePeerConnectionWrapper(
      "Callee2", nullptr, {}, std::move(dependencies), nullptr,
      /*reset_encoder_factory=*/false,
      /*reset_decoder_factory=*/false,
      /*create_media_engine=*/false));
  ConnectFakeSignaling();

  std::unique_ptr<SessionDescriptionInterface> answer;
  caller()->SetReceivedSdpMunger(
      [&answer](std::unique_ptr<SessionDescriptionInterface>& desc) {
        // Change the SCTP port.
        ContentInfo* sctp_content = GetFirstDataContent(desc->description());
        ASSERT_TRUE(sctp_content);
        auto sctp_description = sctp_content->media_description()->as_sctp();
        ASSERT_TRUE(sctp_description);
        sctp_description->set_port(sctp_description->port() + 1);

        // Capture and suppress the answer.
        answer.reset(desc.release());
      });
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_THAT(answer, NotNull());

  EXPECT_TRUE(caller()->SetRemoteDescription(std::move(answer)));
  // Check the state of the SCTP transport.
  VerifySctpState(caller(), SctpTransportState::kConnected);
}

INSTANTIATE_TEST_SUITE_P(DataChannelIntegrationTest,
                         DataChannelIntegrationTest,
                         Combine(Values(SdpSemantics::kPlanB_DEPRECATED,
                                        SdpSemantics::kUnifiedPlan),
                                 testing::Bool()));

TEST_F(DataChannelIntegrationTestUnifiedPlan,
       EndToEndCallWithBundledSctpDataChannel) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(
      WaitUntil([&] { return caller()->pc()->GetSctpTransport() != nullptr; }));
  ASSERT_THAT(
      WaitUntil(
          [&] {
            return caller()->pc()->GetSctpTransport()->Information().state();
          },
          Eq(SctpTransportState::kConnected)),
      IsRtcOk());
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_channel(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));
}

TEST_F(DataChannelIntegrationTestUnifiedPlan,
       EndToEndCallWithDataChannelOnlyConnects) {
  ASSERT_TRUE(CreatePeerConnectionWrappersWithoutMediaEngine());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_channel(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));
  ASSERT_TRUE(caller()->data_observer()->IsOpen());
}

TEST_F(DataChannelIntegrationTestUnifiedPlan, DataChannelClosesWhenClosed) {
  ASSERT_TRUE(CreatePeerConnectionWrappersWithoutMediaEngine());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));
  caller()->data_channel()->Close();
  ASSERT_TRUE(WaitUntil([&] { return !callee()->data_observer()->IsOpen(); }));
}

TEST_F(DataChannelIntegrationTestUnifiedPlan,
       DataChannelClosesWhenClosedReverse) {
  ASSERT_TRUE(CreatePeerConnectionWrappersWithoutMediaEngine());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));
  callee()->data_channel()->Close();
  ASSERT_TRUE(WaitUntil([&] { return !caller()->data_observer()->IsOpen(); }));
}

TEST_F(DataChannelIntegrationTestUnifiedPlan,
       DataChannelClosesWhenPeerConnectionClosed) {
  ASSERT_TRUE(CreatePeerConnectionWrappersWithoutMediaEngine());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer(); }));
  ASSERT_TRUE(WaitUntil([&] { return callee()->data_observer()->IsOpen(); }));
  caller()->pc()->Close();
  ASSERT_TRUE(WaitUntil([&] { return !callee()->data_observer()->IsOpen(); }));
}

TEST_F(DataChannelIntegrationTestUnifiedPlan, ReducingMaxChannelsAtCaller) {
  const int stream_count = 2;
  RTCConfiguration caller_config;
  caller_config.always_negotiate_data_channels = true;
  caller_config.max_sctp_streams = stream_count;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(
      caller_config, PeerConnectionInterface::RTCConfiguration()));
  ConnectFakeSignaling();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  scoped_refptr<SctpTransportInterface> caller_transport =
      caller()->pc()->GetSctpTransport();
  ASSERT_THAT(caller_transport, NotNull());
  ASSERT_TRUE(WaitUntil([&] {
    return caller_transport->Information().state() ==
           SctpTransportState::kConnected;
  }));
  scoped_refptr<SctpTransportInterface> callee_transport =
      callee()->pc()->GetSctpTransport();
  ASSERT_THAT(callee_transport, NotNull());
  std::optional<int> caller_channels =
      caller_transport->Information().MaxChannels();
  std::optional<int> callee_channels =
      callee_transport->Information().MaxChannels();
  ASSERT_TRUE(caller_channels.has_value());
  ASSERT_TRUE(callee_channels.has_value());
  EXPECT_THAT(caller_channels.value(), Eq(stream_count));
  EXPECT_THAT(callee_channels.value(), Eq(stream_count));
}
TEST_F(DataChannelIntegrationTestUnifiedPlan, ReducingMaxChannelsAtCallee) {
  const int stream_count = 2;
  RTCConfiguration caller_config;
  caller_config.always_negotiate_data_channels = true;
  RTCConfiguration callee_config;
  callee_config.max_sctp_streams = stream_count;
  ASSERT_TRUE(
      CreatePeerConnectionWrappersWithConfig(caller_config, callee_config));
  ConnectFakeSignaling();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  scoped_refptr<SctpTransportInterface> caller_transport =
      caller()->pc()->GetSctpTransport();
  ASSERT_THAT(caller_transport, NotNull());
  ASSERT_TRUE(WaitUntil([&] {
    return caller_transport->Information().state() ==
           SctpTransportState::kConnected;
  }));
  scoped_refptr<SctpTransportInterface> callee_transport =
      callee()->pc()->GetSctpTransport();
  ASSERT_THAT(callee_transport, NotNull());
  std::optional<int> caller_channels =
      caller_transport->Information().MaxChannels();
  std::optional<int> callee_channels =
      callee_transport->Information().MaxChannels();
  ASSERT_TRUE(caller_channels.has_value());
  ASSERT_TRUE(callee_channels.has_value());
  EXPECT_THAT(caller_channels.value(), Eq(stream_count));
  EXPECT_THAT(callee_channels.value(), Eq(stream_count));
}

class DataChannelIntegrationTestUnifiedPlanFieldTrials
    : public DataChannelIntegrationTestUnifiedPlan,
      public ::testing::WithParamInterface<std::tuple<
          /* callee-DTLS-active=*/bool,
          /* caller-field-trials=*/const char*,
          /* callee-field-trials=*/const char*,
          /* callee2-field-trials=*/const char*>> {
 protected:
  DataChannelIntegrationTestUnifiedPlanFieldTrials() {
    const bool callee_active = std::get<0>(GetParam());
    RTC_LOG(LS_INFO) << "dtls_active: " << (callee_active ? "callee" : "caller")
                     << " field-trials: caller: " << std::get<1>(GetParam())
                     << " callee: " << std::get<2>(GetParam())
                     << " callee2: " << std::get<3>(GetParam());

    SetFieldTrials(kCallerName, std::get<1>(GetParam()));
    SetFieldTrials(kCalleeName, std::get<2>(GetParam()));
    SetFieldTrials("Callee2", std::get<3>(GetParam()));
  }

  std::unique_ptr<PeerConnectionIntegrationWrapper> SetupCallee2AndDc(
      bool addTurn) {
    RTCConfiguration config;
    if (addTurn) {
      static const SocketAddress turn_server_1_internal_address{"192.0.2.1",
                                                                3478};
      static const SocketAddress turn_server_1_external_address{"192.0.3.1", 0};
      TestTurnServer* turn_server_1 = CreateTurnServer(
          turn_server_1_internal_address, turn_server_1_external_address);

      // Bypass permission check on received packets so media can be sent before
      // the candidate is signaled.
      SendTask(network_thread(), [turn_server_1] {
        turn_server_1->set_enable_permission_checks(false);
      });

      PeerConnectionInterface::IceServer ice_server_1;
      ice_server_1.urls.push_back("turn:192.0.2.1:3478");
      ice_server_1.username = "test";
      ice_server_1.password = "test";
      config.servers.push_back(ice_server_1);
      config.type = PeerConnectionInterface::kRelay;
      config.presume_writable_when_fully_relayed = true;
    }
    CreatePeerConnectionWrappersWithConfig(config, config,
                                           /* create_media_engine= */ false);
    PeerConnectionDependencies dependencies(nullptr);
    std::unique_ptr<FakeRTCCertificateGenerator> cert_generator(
        new FakeRTCCertificateGenerator());
    cert_generator->use_alternate_key();
    dependencies.cert_generator = std::move(cert_generator);
    auto callee2 = CreatePeerConnectionWrapper("Callee2", nullptr, &config,
                                               std::move(dependencies), nullptr,
                                               /*reset_encoder_factory=*/false,
                                               /*reset_decoder_factory=*/false,
                                               /*create_media_engine=*/false);
    ConnectFakeSignaling();
    DataChannelInit dc_init;
    dc_init.negotiated = true;
    dc_init.id = 77;
    caller()->CreateDataChannel("label", &dc_init);
    callee()->CreateDataChannel("label", &dc_init);
    callee2->CreateDataChannel("label", &dc_init);

    callee2->set_signaling_message_receiver(caller());
    return callee2;
  }

  void WaitConnectedAndDcOpen(bool prAnswer,
                              PeerConnectionIntegrationWrapper* caller,
                              PeerConnectionIntegrationWrapper* callee) {
    if (prAnswer) {
      EXPECT_EQ(caller->pc()->signaling_state(),
                PeerConnectionInterface::kHaveRemotePrAnswer);
      EXPECT_EQ(callee->pc()->signaling_state(),
                PeerConnectionInterface::kHaveLocalPrAnswer);
    } else {
      EXPECT_EQ(caller->pc()->signaling_state(),
                PeerConnectionInterface::kStable);
      EXPECT_EQ(callee->pc()->signaling_state(),
                PeerConnectionInterface::kStable);
    }
    ASSERT_THAT(WaitUntil([&] { return caller->data_channel()->state(); },
                          Eq(DataChannelInterface::kOpen)),
                IsRtcOk());
    ASSERT_THAT(WaitUntil([&] { return callee->data_channel()->state(); },
                          Eq(DataChannelInterface::kOpen)),
                IsRtcOk());
    ASSERT_THAT(
        WaitUntil([&] { return caller->pc()->peer_connection_state(); },
                  Eq(PeerConnectionInterface::PeerConnectionState::kConnected)),
        IsRtcOk());
    ASSERT_THAT(
        WaitUntil([&] { return callee->pc()->peer_connection_state(); },
                  Eq(PeerConnectionInterface::PeerConnectionState::kConnected)),
        IsRtcOk());
  }

  static void SendOnDatachannelWhenConnectedCallback(
      PeerConnectionIntegrationWrapper* peer,
      const std::string& data,
      std::atomic<int>& signal) {
    if (peer->pc()->peer_connection_state() ==
            PeerConnectionInterface::PeerConnectionState::kConnected &&
        peer->data_channel()->state() == DataChannelInterface::kOpen) {
      peer->data_channel()->SendAsync(DataBuffer(data), [&](RTCError err) {
        signal.store(err.ok() ? 1 : -1);
      });
    }
  }

  void VerifyDtlsRoles(PeerConnectionIntegrationWrapper* caller,
                       PeerConnectionIntegrationWrapper* callee) {
    const bool callee_active = std::get<0>(GetParam());
    if (callee_active) {
      ASSERT_THAT(caller->dtls_transport_role(),
                  Eq(DtlsTransportTlsRole::kServer));
      ASSERT_THAT(callee->dtls_transport_role(),
                  Eq(DtlsTransportTlsRole::kClient));
    } else {
      ASSERT_THAT(caller->dtls_transport_role(),
                  Eq(DtlsTransportTlsRole::kClient));
      ASSERT_THAT(callee->dtls_transport_role(),
                  Eq(DtlsTransportTlsRole::kServer));
    }
  }

  void VerifyReceivedDcMessages(PeerConnectionIntegrationWrapper* peer,
                                const std::string& data,
                                std::atomic<int>& signal) {
    ASSERT_THAT(WaitUntil([&] { return signal.load(); }, Ne(0)), IsRtcOk());
    EXPECT_THAT(WaitUntil([&] { return peer->data_observer()->last_message(); },
                          Eq(data)),
                IsRtcOk());
  }

  const char* CheckSupported() {
    const bool callee_active = std::get<0>(GetParam());
    const bool caller_has_dtls_in_stun = absl::StrContains(
        std::get<1>(GetParam()), "WebRTC-IceHandshakeDtls/Enabled/");
    const bool callee_has_dtls_in_stun = absl::StrContains(
        std::get<2>(GetParam()), "WebRTC-IceHandshakeDtls/Enabled/");
    const bool callee2_has_dtls_in_stun = absl::StrContains(
        std::get<3>(GetParam()), "WebRTC-IceHandshakeDtls/Enabled/");
    if (callee_active && (caller_has_dtls_in_stun || callee_has_dtls_in_stun ||
                          callee2_has_dtls_in_stun)) {
      return "dtls-in-stun when callee(s) are dtls clients";
    }

    return nullptr;
  }
};

const char* kTrialsVariants[] = {
    "",
    "WebRTC-IceHandshakeDtls/Enabled/",
    "WebRTC-EnableDtlsPqc/Enabled/",
    ("WebRTC-IceHandshakeDtls/Enabled/"
     "WebRTC-EnableDtlsPqc/Enabled/"),
};

INSTANTIATE_TEST_SUITE_P(DataChannelIntegrationTestUnifiedPlanFieldTrials,
                         DataChannelIntegrationTestUnifiedPlanFieldTrials,
                         Combine(testing::Bool(),
                                 ValuesIn(kTrialsVariants),
                                 ValuesIn(kTrialsVariants),
                                 ValuesIn(kTrialsVariants)));

struct SpedV1TestConfig {
  bool caller_enabled;
  bool callee_enabled;
};

class SdpNegotiationGoogSpedV1Test
    : public DataChannelIntegrationTestUnifiedPlan,
      public ::testing::WithParamInterface<SpedV1TestConfig> {};

TEST_P(SdpNegotiationGoogSpedV1Test, VerifySdp) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "DTLS-in-STUN needs boringssl.";
  }
  const auto& param = GetParam();
  SetFieldTrials(
      "Caller", param.caller_enabled ? "WebRTC-IceHandshakeDtls/Enabled/" : "");
  SetFieldTrials(
      "Callee", param.callee_enabled ? "WebRTC-IceHandshakeDtls/Enabled/" : "");

  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->CreateDataChannel();

  std::unique_ptr<SessionDescriptionInterface> offer;
  caller()->SetGeneratedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        offer = sdp->Clone();
      });

  std::unique_ptr<SessionDescriptionInterface> answer;
  callee()->SetGeneratedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        answer = sdp->Clone();
      });

  caller()->CreateAndSetAndSignalOffer();

  ASSERT_THAT(offer, NotNull());
  std::string offer_sdp;
  offer->ToString(&offer_sdp);

  if (param.caller_enabled) {
    EXPECT_THAT(offer_sdp, testing::HasSubstr(ICE_OPTION_GOOG_SPED_V1));
  } else {
    EXPECT_THAT(offer_sdp,
                testing::Not(testing::HasSubstr(ICE_OPTION_GOOG_SPED_V1)));
  }

  ASSERT_TRUE(WaitUntil([&] { return answer.get() != nullptr; }));

  std::string answer_sdp;
  answer->ToString(&answer_sdp);

  if (param.callee_enabled && param.caller_enabled) {
    EXPECT_THAT(answer_sdp, testing::HasSubstr(ICE_OPTION_GOOG_SPED_V1));
  } else {
    EXPECT_THAT(answer_sdp,
                testing::Not(testing::HasSubstr(ICE_OPTION_GOOG_SPED_V1)));
  }
}

INSTANTIATE_TEST_SUITE_P(SdpNegotiationGoogSpedV1Test,
                         SdpNegotiationGoogSpedV1Test,
                         testing::Values(SpedV1TestConfig{false, false},
                                         SpedV1TestConfig{false, true},
                                         SpedV1TestConfig{true, false},
                                         SpedV1TestConfig{true, true}));

TEST_P(DataChannelIntegrationTestUnifiedPlanFieldTrials,
       DtlsRestartOneCalleAtATime) {
  if (auto msg = CheckSupported()) {
    GTEST_SKIP() << "Testcase not supported for this scenario: " << msg;
  }

  auto callee2 = SetupCallee2AndDc(/* addTurn= */ false);
  const bool callee_active = std::get<0>(GetParam());
  std::unique_ptr<SessionDescriptionInterface> offer;
  callee()->SetReceivedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        if (callee_active) {
          MakeOfferHavePassiveDtlsRole(sdp);
        } else {
          MakeOfferHaveActiveDtlsRole(sdp);
        }
        // Capture offer so that it can be sent to Callee2 too.
        offer = sdp->Clone();
      });
  callee2->SetReceivedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        if (callee_active) {
          MakeOfferHavePassiveDtlsRole(sdp);
        } else {
          MakeOfferHaveActiveDtlsRole(sdp);
        }
      });
  callee()->SetGeneratedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        // Modify offer to kPrAnswer
        SetSdpType(sdp, SdpType::kPrAnswer);
        if (callee_active) {
          MakeOfferHaveActiveDtlsRole(sdp);
        } else {
          MakeOfferHavePassiveDtlsRole(sdp);
        }
      });
  callee2->SetGeneratedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        if (callee_active) {
          MakeOfferHaveActiveDtlsRole(sdp);
        } else {
          MakeOfferHavePassiveDtlsRole(sdp);
        }
      });
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_FALSE(HasFailure());
  WaitConnectedAndDcOpen(/* prAnswer= */ true, caller(), callee());
  VerifyDtlsRoles(caller(), callee());
  ASSERT_FALSE(HasFailure());

  std::atomic<int> caller_sent_on_dc(0);
  std::atomic<int> callee2_sent_on_dc(0);
  caller()->set_connection_change_callback([&](auto new_state) {
    SendOnDatachannelWhenConnectedCallback(caller(), "KESO", caller_sent_on_dc);
  });
  // Install same cb on both connection_change_callback and
  // data_observer->set_state_change_callback since they can fire in any order.
  callee2->set_connection_change_callback([&](auto new_state) {
    SendOnDatachannelWhenConnectedCallback(callee2.get(), "KENT",
                                           callee2_sent_on_dc);
  });
  callee2->data_observer()->set_state_change_callback([&](auto new_state) {
    SendOnDatachannelWhenConnectedCallback(callee2.get(), "KENT",
                                           callee2_sent_on_dc);
  });

  // Now let callee2 get the offer, apply it and send the answer to caller.
  std::string offer_sdp;
  EXPECT_TRUE(offer->ToString(&offer_sdp));
  callee2->ReceiveSdpMessage(SdpType::kOffer, offer_sdp);
  WaitConnectedAndDcOpen(/* prAnswer= */ false, caller(), callee2.get());
  ASSERT_FALSE(HasFailure());

  VerifyReceivedDcMessages(caller(), "KENT", callee2_sent_on_dc);
  VerifyReceivedDcMessages(callee2.get(), "KESO", caller_sent_on_dc);
  VerifyDtlsRoles(caller(), callee2.get());
  VerifySctpState(caller(), SctpTransportState::kConnected);
  VerifySctpState(callee(), SctpTransportState::kClosed);
  VerifySctpState(callee2.get(), SctpTransportState::kConnected);
  ASSERT_FALSE(HasFailure());
}

TEST_P(DataChannelIntegrationTestUnifiedPlanFieldTrials,
       DtlsRestartTwoActiveCallees) {
  if (auto msg = CheckSupported()) {
    GTEST_SKIP() << "Testcase not supported for this scenario: " << msg;
  }

  auto callee2 = SetupCallee2AndDc(/* addTurn= */ true);
  const bool callee_active = std::get<0>(GetParam());
  std::unique_ptr<SessionDescriptionInterface> offer;
  callee()->SetReceivedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        if (callee_active) {
          MakeOfferHavePassiveDtlsRole(sdp);
        } else {
          MakeOfferHaveActiveDtlsRole(sdp);
        }
        // Capture offer so that it can be sent to Callee2 too.
        offer = sdp->Clone();
      });
  callee2->SetReceivedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        if (callee_active) {
          MakeOfferHavePassiveDtlsRole(sdp);
        } else {
          MakeOfferHaveActiveDtlsRole(sdp);
        }
      });
  callee()->SetGeneratedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        // Modify offer to kPrAnswer
        SetSdpType(sdp, SdpType::kPrAnswer);
        if (callee_active) {
          MakeOfferHaveActiveDtlsRole(sdp);
        } else {
          MakeOfferHavePassiveDtlsRole(sdp);
        }
      });
  callee2->SetGeneratedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        if (callee_active) {
          MakeOfferHaveActiveDtlsRole(sdp);
        } else {
          MakeOfferHavePassiveDtlsRole(sdp);
        }
      });
  bool first_answer = true;
  std::unique_ptr<SessionDescriptionInterface> answer;
  caller()->SetReceivedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        if (first_answer) {
          first_answer = false;
        } else {
          answer = std::move(sdp);
        }
      });
  caller()->CreateAndSetAndSignalOffer();
  std::string offer_sdp;
  EXPECT_TRUE(offer->ToString(&offer_sdp));
  // Apply offer on callee2 "in parallell" to callee.
  callee2->ReceiveSdpMessage(SdpType::kOffer, offer_sdp);

  ASSERT_FALSE(HasFailure());
  EXPECT_EQ(callee2->pc()->signaling_state(), PeerConnectionInterface::kStable);
  WaitConnectedAndDcOpen(/* prAnswer= */ true, caller(), callee());
  ASSERT_FALSE(HasFailure());

  // Forward turn ice candidate also to callee2.
  auto candidate = caller()->last_gathered_ice_candidate();
  std::string ice_sdp = candidate->ToString();
  callee2->ReceiveIceMessage(candidate->sdp_mid(), candidate->sdp_mline_index(),
                             ice_sdp);

  // Wait until callee2 is ICE connected.
  ASSERT_THAT(
      WaitUntil(
          [&] { return callee2->pc()->standardized_ice_connection_state(); },
          Eq(PeerConnectionInterface::kIceConnectionConnected)),
      IsRtcOk());

  VerifyDtlsRoles(caller(), callee());
  if (callee_active) {
    ASSERT_THAT(callee2->dtls_transport_role(),
                Eq(DtlsTransportTlsRole::kClient));
  } else {
    ASSERT_THAT(callee2->dtls_transport_role(),
                Eq(DtlsTransportTlsRole::kServer));
  }

  std::atomic<int> caller_sent_on_dc(0);
  std::atomic<int> callee2_sent_on_dc(0);
  caller()->set_connection_change_callback([&](auto new_state) {
    SendOnDatachannelWhenConnectedCallback(caller(), "KESO", caller_sent_on_dc);
  });
  // Install same cb on both connection_change_callback and
  // data_observer->set_state_change_callback since they can fire in any order.
  callee2->set_connection_change_callback([&](auto new_state) {
    SendOnDatachannelWhenConnectedCallback(callee2.get(), "KENT",
                                           callee2_sent_on_dc);
  });
  callee2->data_observer()->set_state_change_callback([&](auto new_state) {
    SendOnDatachannelWhenConnectedCallback(callee2.get(), "KENT",
                                           callee2_sent_on_dc);
  });

  // Now switch to callee2!
  first_answer = true;
  ASSERT_THAT(answer, testing::Not(testing::IsNull()));
  std::string answer_sdp;
  EXPECT_TRUE(answer->ToString(&answer_sdp));

  caller()->ReceiveSdpMessage(SdpType::kAnswer, answer_sdp);

  EXPECT_EQ(caller()->pc()->signaling_state(),
            PeerConnectionInterface::kStable);

  VerifyReceivedDcMessages(caller(), "KENT", callee2_sent_on_dc);
  VerifyReceivedDcMessages(callee2.get(), "KESO", caller_sent_on_dc);
  VerifyDtlsRoles(caller(), callee2.get());
  ASSERT_FALSE(HasFailure());
}

#endif  // WEBRTC_HAVE_SCTP

}  // namespace

}  // namespace webrtc
