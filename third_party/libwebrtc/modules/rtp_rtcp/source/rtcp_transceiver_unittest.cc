/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtcp_transceiver.h"

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/rtp_rtcp/source/rtcp_packet.h"
#include "modules/rtp_rtcp/source/rtcp_packet/remote_estimate.h"
#include "modules/rtp_rtcp/source/rtcp_packet/sender_report.h"
#include "modules/rtp_rtcp/source/rtcp_packet/transport_feedback.h"
#include "modules/rtp_rtcp/source/rtcp_transceiver_config.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/event.h"
#include "rtc_base/task_queue_for_test.h"
#include "system_wrappers/include/clock.h"
#include "system_wrappers/include/ntp_time.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/rtcp_packet_parser.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::AtLeast;
using ::testing::IsNull;
using ::testing::MockFunction;
using ::testing::NiceMock;

using rtcp::RemoteEstimate;
using rtcp::RtcpPacket;
using rtcp::TransportFeedback;
using test::RtcpPacketParser;

class MockMediaReceiverRtcpObserver : public MediaReceiverRtcpObserver {
 public:
  MOCK_METHOD(void, OnSenderReport, (uint32_t, NtpTime, uint32_t), (override));
};

constexpr TimeDelta kTimeout = TimeDelta::Seconds(1);

void WaitPostedTasks(TaskQueueForTest* queue) {
  Event done;
  queue->PostTask([&done] { done.Set(); });
  ASSERT_TRUE(done.Wait(kTimeout));
}

TEST(RtcpTransceiverTest, SendsRtcpOnTaskQueueWhenCreatedOffTaskQueue) {
  SimulatedClock clock(0);
  MockFunction<void(std::span<const uint8_t>)> outgoing_transport;
  TaskQueueForTest queue("rtcp");
  RtcpTransceiverConfig config;
  config.clock = &clock;
  config.rtcp_transport = outgoing_transport.AsStdFunction();
  config.task_queue = queue.Get();
  EXPECT_CALL(outgoing_transport, Call).WillRepeatedly([&] {
    EXPECT_TRUE(queue.IsCurrent());
  });

  RtcpTransceiver rtcp_transceiver(config);
  rtcp_transceiver.SendCompoundPacket();
  WaitPostedTasks(&queue);
}

TEST(RtcpTransceiverTest, SendsRtcpOnTaskQueueWhenCreatedOnTaskQueue) {
  SimulatedClock clock(0);
  MockFunction<void(std::span<const uint8_t>)> outgoing_transport;
  TaskQueueForTest queue("rtcp");
  RtcpTransceiverConfig config;
  config.clock = &clock;
  config.rtcp_transport = outgoing_transport.AsStdFunction();
  config.task_queue = queue.Get();
  EXPECT_CALL(outgoing_transport, Call).WillRepeatedly([&] {
    EXPECT_TRUE(queue.IsCurrent());
  });

  std::unique_ptr<RtcpTransceiver> rtcp_transceiver;
  queue.PostTask([&] {
    rtcp_transceiver = std::make_unique<RtcpTransceiver>(config);
    rtcp_transceiver->SendCompoundPacket();
  });
  WaitPostedTasks(&queue);
}

TEST(RtcpTransceiverTest, CanBeDestroyedOnTaskQueue) {
  SimulatedClock clock(0);
  MockFunction<void(std::span<const uint8_t>)> outgoing_transport;
  TaskQueueForTest queue("rtcp");
  RtcpTransceiverConfig config;
  config.clock = &clock;
  config.rtcp_transport = outgoing_transport.AsStdFunction();
  config.task_queue = queue.Get();
  auto rtcp_transceiver = std::make_unique<RtcpTransceiver>(config);

  queue.PostTask([&] {
    // Insert a packet just before destruction to test for races.
    rtcp_transceiver->SendCompoundPacket();
    rtcp_transceiver.reset();
  });
  WaitPostedTasks(&queue);
}

TEST(RtcpTransceiverTest, CanBeDestroyedWithoutBlocking) {
  SimulatedClock clock(0);
  TaskQueueForTest queue("rtcp");
  RtcpTransceiverConfig config;
  config.clock = &clock;
  config.task_queue = queue.Get();
  auto* rtcp_transceiver = new RtcpTransceiver(config);
  rtcp_transceiver->SendCompoundPacket();

  Event done;
  Event heavy_task;
  queue.PostTask([&] {
    EXPECT_TRUE(heavy_task.Wait(kTimeout));
    done.Set();
  });
  delete rtcp_transceiver;

  heavy_task.Set();
  EXPECT_TRUE(done.Wait(kTimeout));
}

TEST(RtcpTransceiverTest, MaySendPacketsAfterDestructor) {  // i.e. Be careful!
  SimulatedClock clock(0);
  // Must outlive queue below.
  NiceMock<MockFunction<void(std::span<const uint8_t>)>> transport;
  TaskQueueForTest queue("rtcp");
  RtcpTransceiverConfig config;
  config.clock = &clock;
  config.rtcp_transport = transport.AsStdFunction();
  config.task_queue = queue.Get();
  auto* rtcp_transceiver = new RtcpTransceiver(config);

  Event heavy_task;
  queue.PostTask([&] { EXPECT_TRUE(heavy_task.Wait(kTimeout)); });
  rtcp_transceiver->SendCompoundPacket();
  delete rtcp_transceiver;

  EXPECT_CALL(transport, Call);
  heavy_task.Set();

  WaitPostedTasks(&queue);
}

// Use rtp timestamp to distinguish different incoming sender reports.
CopyOnWriteBuffer CreateSenderReport(uint32_t ssrc, uint32_t rtp_time) {
  rtcp::SenderReport sr;
  sr.SetSenderSsrc(ssrc);
  sr.SetRtpTimestamp(rtp_time);
  // Switch to an efficient way creating CopyOnWriteBuffer from RtcpPacket when
  // there is one. Until then do not worry about extra memcpy in test.
  return CopyOnWriteBuffer(sr.Build());
}

TEST(RtcpTransceiverTest, DoesntPostToRtcpObserverAfterCallToRemove) {
  const uint32_t kRemoteSsrc = 1234;
  SimulatedClock clock(0);
  TaskQueueForTest queue("rtcp");
  RtcpTransceiverConfig config;
  config.clock = &clock;
  config.task_queue = queue.Get();
  RtcpTransceiver rtcp_transceiver(config);
  Event observer_deleted;

  auto observer = std::make_unique<MockMediaReceiverRtcpObserver>();
  EXPECT_CALL(*observer, OnSenderReport(kRemoteSsrc, _, 1));
  EXPECT_CALL(*observer, OnSenderReport(kRemoteSsrc, _, 2)).Times(0);

  rtcp_transceiver.AddMediaReceiverRtcpObserver(kRemoteSsrc, observer.get());
  rtcp_transceiver.ReceivePacket(CreateSenderReport(kRemoteSsrc, 1));
  rtcp_transceiver.RemoveMediaReceiverRtcpObserver(kRemoteSsrc, observer.get(),
                                                   /*on_removed=*/[&] {
                                                     observer.reset();
                                                     observer_deleted.Set();
                                                   });
  rtcp_transceiver.ReceivePacket(CreateSenderReport(kRemoteSsrc, 2));

  EXPECT_TRUE(observer_deleted.Wait(kTimeout));
  WaitPostedTasks(&queue);
}

TEST(RtcpTransceiverTest, RemoveMediaReceiverRtcpObserverIsNonBlocking) {
  const uint32_t kRemoteSsrc = 1234;
  SimulatedClock clock(0);
  TaskQueueForTest queue("rtcp");
  RtcpTransceiverConfig config;
  config.clock = &clock;
  config.task_queue = queue.Get();
  RtcpTransceiver rtcp_transceiver(config);
  auto observer = std::make_unique<MockMediaReceiverRtcpObserver>();
  rtcp_transceiver.AddMediaReceiverRtcpObserver(kRemoteSsrc, observer.get());

  Event queue_blocker;
  Event observer_deleted;
  queue.PostTask([&] { EXPECT_TRUE(queue_blocker.Wait(kTimeout)); });
  rtcp_transceiver.RemoveMediaReceiverRtcpObserver(kRemoteSsrc, observer.get(),
                                                   /*on_removed=*/[&] {
                                                     observer.reset();
                                                     observer_deleted.Set();
                                                   });

  EXPECT_THAT(observer, Not(IsNull()));
  queue_blocker.Set();
  EXPECT_TRUE(observer_deleted.Wait(kTimeout));
}

TEST(RtcpTransceiverTest, CanCallSendCompoundPacketFromAnyThread) {
  SimulatedClock clock(0);
  MockFunction<void(std::span<const uint8_t>)> outgoing_transport;
  TaskQueueForTest queue("rtcp");
  RtcpTransceiverConfig config;
  config.clock = &clock;
  config.rtcp_transport = outgoing_transport.AsStdFunction();
  config.task_queue = queue.Get();

  EXPECT_CALL(outgoing_transport, Call)
      // If test is slow, a periodic task may send an extra packet.
      .Times(AtLeast(3))
      .WillRepeatedly([&] { EXPECT_TRUE(queue.IsCurrent()); });

  RtcpTransceiver rtcp_transceiver(config);

  // Call from the construction thread.
  rtcp_transceiver.SendCompoundPacket();
  // Call from the same queue transceiver use for processing.
  queue.PostTask([&] { rtcp_transceiver.SendCompoundPacket(); });
  // Call from unrelated task queue.
  TaskQueueForTest queue_send("send_packet");
  queue_send.PostTask([&] { rtcp_transceiver.SendCompoundPacket(); });

  WaitPostedTasks(&queue_send);
  WaitPostedTasks(&queue);
}

TEST(RtcpTransceiverTest, DoesntSendPacketsAfterStopCallback) {
  SimulatedClock clock(0);
  NiceMock<MockFunction<void(std::span<const uint8_t>)>> outgoing_transport;
  TaskQueueForTest queue("rtcp");
  RtcpTransceiverConfig config;
  config.clock = &clock;
  config.rtcp_transport = outgoing_transport.AsStdFunction();
  config.task_queue = queue.Get();
  config.schedule_periodic_compound_packets = true;

  auto rtcp_transceiver = std::make_unique<RtcpTransceiver>(config);
  Event done;
  rtcp_transceiver->SendCompoundPacket();
  rtcp_transceiver->Stop([&] {
    EXPECT_CALL(outgoing_transport, Call).Times(0);
    done.Set();
  });
  rtcp_transceiver = nullptr;
  EXPECT_TRUE(done.Wait(kTimeout));
}

TEST(RtcpTransceiverTest, SendsCombinedRtcpPacketOnTaskQueue) {
  static constexpr uint32_t kSenderSsrc = 12345;

  SimulatedClock clock(0);
  MockFunction<void(std::span<const uint8_t>)> outgoing_transport;
  TaskQueueForTest queue("rtcp");
  RtcpTransceiverConfig config;
  config.clock = &clock;
  config.feedback_ssrc = kSenderSsrc;
  config.rtcp_transport = outgoing_transport.AsStdFunction();
  config.task_queue = queue.Get();
  config.schedule_periodic_compound_packets = false;
  RtcpTransceiver rtcp_transceiver(config);

  EXPECT_CALL(outgoing_transport, Call)
      .WillOnce([&](std::span<const uint8_t> buffer) {
        EXPECT_TRUE(queue.IsCurrent());
        RtcpPacketParser rtcp_parser;
        rtcp_parser.Parse(buffer);
        EXPECT_EQ(rtcp_parser.transport_feedback()->num_packets(), 1);
        EXPECT_EQ(rtcp_parser.transport_feedback()->sender_ssrc(), kSenderSsrc);
        EXPECT_EQ(rtcp_parser.app()->num_packets(), 1);
        EXPECT_EQ(rtcp_parser.app()->sender_ssrc(), kSenderSsrc);
        return true;
      });

  // Create minimalistic transport feedback packet.
  std::vector<std::unique_ptr<RtcpPacket>> packets;
  auto transport_feedback = std::make_unique<TransportFeedback>();
  transport_feedback->AddReceivedPacket(321, Timestamp::Millis(10));
  packets.push_back(std::move(transport_feedback));

  auto remote_estimate = std::make_unique<RemoteEstimate>();
  packets.push_back(std::move(remote_estimate));

  rtcp_transceiver.SendCombinedRtcpPacket(std::move(packets));
  WaitPostedTasks(&queue);
}

TEST(RtcpTransceiverTest, SendFrameIntraRequestDefaultsToNewRequest) {
  static constexpr uint32_t kSenderSsrc = 12345;

  SimulatedClock clock(0);
  MockFunction<void(std::span<const uint8_t>)> outgoing_transport;
  TaskQueueForTest queue("rtcp");
  RtcpTransceiverConfig config;
  config.clock = &clock;
  config.feedback_ssrc = kSenderSsrc;
  config.rtcp_transport = outgoing_transport.AsStdFunction();
  config.task_queue = queue.Get();
  config.schedule_periodic_compound_packets = false;
  RtcpTransceiver rtcp_transceiver(config);

  uint8_t first_seq_nr;
  EXPECT_CALL(outgoing_transport, Call)
      .WillOnce([&](std::span<const uint8_t> buffer) {
        EXPECT_TRUE(queue.IsCurrent());
        RtcpPacketParser rtcp_parser;
        rtcp_parser.Parse(buffer);
        EXPECT_EQ(rtcp_parser.fir()->requests()[0].ssrc, kSenderSsrc);
        first_seq_nr = rtcp_parser.fir()->requests()[0].seq_nr;
        return true;
      })
      .WillOnce([&](std::span<const uint8_t> buffer) {
        EXPECT_TRUE(queue.IsCurrent());
        RtcpPacketParser rtcp_parser;
        rtcp_parser.Parse(buffer);
        EXPECT_EQ(rtcp_parser.fir()->requests()[0].ssrc, kSenderSsrc);
        EXPECT_EQ(rtcp_parser.fir()->requests()[0].seq_nr, first_seq_nr + 1);
        return true;
      });

  // Send 2 FIR packets because the sequence numbers are incremented after,
  // sending. One wouldn't be able to differentiate the new_request.
  rtcp_transceiver.SendFullIntraRequest({kSenderSsrc});
  rtcp_transceiver.SendFullIntraRequest({kSenderSsrc});

  WaitPostedTasks(&queue);
}

}  // namespace

}  // namespace webrtc
