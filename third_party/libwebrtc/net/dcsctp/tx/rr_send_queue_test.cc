/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "net/dcsctp/tx/rr_send_queue.h"

#include <cstdint>
#include <type_traits>
#include <vector>

#include "net/dcsctp/common/internal_types.h"
#include "net/dcsctp/packet/data.h"
#include "net/dcsctp/public/dcsctp_message.h"
#include "net/dcsctp/public/dcsctp_options.h"
#include "net/dcsctp/public/dcsctp_socket.h"
#include "net/dcsctp/public/types.h"
#include "net/dcsctp/socket/mock_dcsctp_socket_callbacks.h"
#include "net/dcsctp/testing/testing_macros.h"
#include "net/dcsctp/tx/send_queue.h"
#include "rtc_base/gunit.h"
#include "test/gmock.h"

namespace dcsctp {
namespace {
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;
using ::webrtc::TimeDelta;
using ::webrtc::Timestamp;

constexpr Timestamp kNow = Timestamp::Zero();
constexpr StreamID kStreamID(1);
constexpr PPID kPPID(53);
constexpr StreamPriority kDefaultPriority(10);
constexpr size_t kBufferedAmountLowThreshold = 500;
constexpr size_t kOneFragmentPacketSize = 100;
constexpr size_t kTwoFragmentPacketSize = 101;
constexpr size_t kMtu = 1100;

class RRSendQueueTest : public testing::Test {
 protected:
  RRSendQueueTest()
      : buf_("log: ",
             &callbacks_,

             kMtu,
             kDefaultPriority,
             kBufferedAmountLowThreshold) {}

  testing::NiceMock<MockDcSctpSocketCallbacks> callbacks_;
  const DcSctpOptions options_;
  RRSendQueue buf_;
};

TEST_F(RRSendQueueTest, EmptyBuffer) {
  EXPECT_TRUE(buf_.IsEmpty());
  EXPECT_FALSE(buf_.Produce(kNow, kOneFragmentPacketSize).has_value());
}

TEST_F(RRSendQueueTest, AddAndGetSingleChunk) {
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, {1, 2, 4, 5, 6}));

  EXPECT_FALSE(buf_.IsEmpty());
  absl::optional<SendQueue::DataToSend> chunk_opt =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_opt.has_value());
  EXPECT_TRUE(chunk_opt->data.is_beginning);
  EXPECT_TRUE(chunk_opt->data.is_end);
}

TEST_F(RRSendQueueTest, CarveOutBeginningMiddleAndEnd) {
  std::vector<uint8_t> payload(60);
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  absl::optional<SendQueue::DataToSend> chunk_beg =
      buf_.Produce(kNow, /*max_size=*/20);
  ASSERT_TRUE(chunk_beg.has_value());
  EXPECT_TRUE(chunk_beg->data.is_beginning);
  EXPECT_FALSE(chunk_beg->data.is_end);

  absl::optional<SendQueue::DataToSend> chunk_mid =
      buf_.Produce(kNow, /*max_size=*/20);
  ASSERT_TRUE(chunk_mid.has_value());
  EXPECT_FALSE(chunk_mid->data.is_beginning);
  EXPECT_FALSE(chunk_mid->data.is_end);

  absl::optional<SendQueue::DataToSend> chunk_end =
      buf_.Produce(kNow, /*max_size=*/20);
  ASSERT_TRUE(chunk_end.has_value());
  EXPECT_FALSE(chunk_end->data.is_beginning);
  EXPECT_TRUE(chunk_end->data.is_end);

  EXPECT_FALSE(buf_.Produce(kNow, kOneFragmentPacketSize).has_value());
}

TEST_F(RRSendQueueTest, GetChunksFromTwoMessages) {
  std::vector<uint8_t> payload(60);
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  buf_.Add(kNow, DcSctpMessage(StreamID(3), PPID(54), payload));

  absl::optional<SendQueue::DataToSend> chunk_one =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_one.has_value());
  EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  EXPECT_EQ(chunk_one->data.ppid, kPPID);
  EXPECT_TRUE(chunk_one->data.is_beginning);
  EXPECT_TRUE(chunk_one->data.is_end);

  absl::optional<SendQueue::DataToSend> chunk_two =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_two.has_value());
  EXPECT_EQ(chunk_two->data.stream_id, StreamID(3));
  EXPECT_EQ(chunk_two->data.ppid, PPID(54));
  EXPECT_TRUE(chunk_two->data.is_beginning);
  EXPECT_TRUE(chunk_two->data.is_end);
}

TEST_F(RRSendQueueTest, BufferBecomesFullAndEmptied) {
  std::vector<uint8_t> payload(600);
  EXPECT_LT(buf_.total_buffered_amount(), 1000u);
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  EXPECT_LT(buf_.total_buffered_amount(), 1000u);
  buf_.Add(kNow, DcSctpMessage(StreamID(3), PPID(54), payload));
  EXPECT_GE(buf_.total_buffered_amount(), 1000u);
  // However, it's still possible to add messages. It's a soft limit, and it
  // might be necessary to forcefully add messages due to e.g. external
  // fragmentation.
  buf_.Add(kNow, DcSctpMessage(StreamID(5), PPID(55), payload));
  EXPECT_GE(buf_.total_buffered_amount(), 1000u);

  absl::optional<SendQueue::DataToSend> chunk_one = buf_.Produce(kNow, 1000);
  ASSERT_TRUE(chunk_one.has_value());
  EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  EXPECT_EQ(chunk_one->data.ppid, kPPID);

  EXPECT_GE(buf_.total_buffered_amount(), 1000u);

  absl::optional<SendQueue::DataToSend> chunk_two = buf_.Produce(kNow, 1000);
  ASSERT_TRUE(chunk_two.has_value());
  EXPECT_EQ(chunk_two->data.stream_id, StreamID(3));
  EXPECT_EQ(chunk_two->data.ppid, PPID(54));

  EXPECT_LT(buf_.total_buffered_amount(), 1000u);
  EXPECT_FALSE(buf_.IsEmpty());

  absl::optional<SendQueue::DataToSend> chunk_three = buf_.Produce(kNow, 1000);
  ASSERT_TRUE(chunk_three.has_value());
  EXPECT_EQ(chunk_three->data.stream_id, StreamID(5));
  EXPECT_EQ(chunk_three->data.ppid, PPID(55));

  EXPECT_LT(buf_.total_buffered_amount(), 1000u);
  EXPECT_TRUE(buf_.IsEmpty());
}

TEST_F(RRSendQueueTest, DefaultsToOrderedSend) {
  std::vector<uint8_t> payload(20);

  // Default is ordered
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  absl::optional<SendQueue::DataToSend> chunk_one =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_one.has_value());
  EXPECT_FALSE(chunk_one->data.is_unordered);

  // Explicitly unordered.
  SendOptions opts;
  opts.unordered = IsUnordered(true);
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload), opts);
  absl::optional<SendQueue::DataToSend> chunk_two =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_two.has_value());
  EXPECT_TRUE(chunk_two->data.is_unordered);
}

TEST_F(RRSendQueueTest, ProduceWithLifetimeExpiry) {
  std::vector<uint8_t> payload(20);

  // Default is no expiry
  Timestamp now = kNow;
  buf_.Add(now, DcSctpMessage(kStreamID, kPPID, payload));
  now += TimeDelta::Seconds(1000);
  ASSERT_TRUE(buf_.Produce(now, kOneFragmentPacketSize));

  SendOptions expires_2_seconds;
  expires_2_seconds.lifetime = DurationMs(2000);

  // Add and consume within lifetime
  buf_.Add(now, DcSctpMessage(kStreamID, kPPID, payload), expires_2_seconds);
  now += TimeDelta::Millis(2000);
  ASSERT_TRUE(buf_.Produce(now, kOneFragmentPacketSize));

  // Add and consume just outside lifetime
  buf_.Add(now, DcSctpMessage(kStreamID, kPPID, payload), expires_2_seconds);
  now += TimeDelta::Millis(2001);
  ASSERT_FALSE(buf_.Produce(now, kOneFragmentPacketSize));

  // A long time after expiry
  buf_.Add(now, DcSctpMessage(kStreamID, kPPID, payload), expires_2_seconds);
  now += TimeDelta::Seconds(1000);
  ASSERT_FALSE(buf_.Produce(now, kOneFragmentPacketSize));

  // Expire one message, but produce the second that is not expired.
  buf_.Add(now, DcSctpMessage(kStreamID, kPPID, payload), expires_2_seconds);

  SendOptions expires_4_seconds;
  expires_4_seconds.lifetime = DurationMs(4000);

  buf_.Add(now, DcSctpMessage(kStreamID, kPPID, payload), expires_4_seconds);
  now += TimeDelta::Millis(2001);

  ASSERT_TRUE(buf_.Produce(now, kOneFragmentPacketSize));
  ASSERT_FALSE(buf_.Produce(now, kOneFragmentPacketSize));
}

TEST_F(RRSendQueueTest, DiscardPartialPackets) {
  std::vector<uint8_t> payload(120);

  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  buf_.Add(kNow, DcSctpMessage(StreamID(2), PPID(54), payload));

  absl::optional<SendQueue::DataToSend> chunk_one =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_one.has_value());
  EXPECT_FALSE(chunk_one->data.is_end);
  EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  buf_.Discard(chunk_one->data.stream_id, chunk_one->message_id);

  absl::optional<SendQueue::DataToSend> chunk_two =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_two.has_value());
  EXPECT_FALSE(chunk_two->data.is_end);
  EXPECT_EQ(chunk_two->data.stream_id, StreamID(2));

  absl::optional<SendQueue::DataToSend> chunk_three =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_three.has_value());
  EXPECT_TRUE(chunk_three->data.is_end);
  EXPECT_EQ(chunk_three->data.stream_id, StreamID(2));
  ASSERT_FALSE(buf_.Produce(kNow, kOneFragmentPacketSize));

  // Calling it again shouldn't cause issues.
  buf_.Discard(chunk_one->data.stream_id, chunk_one->message_id);
  ASSERT_FALSE(buf_.Produce(kNow, kOneFragmentPacketSize));
}

TEST_F(RRSendQueueTest, PrepareResetStreamsDiscardsStream) {
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, {1, 2, 3}));
  buf_.Add(kNow, DcSctpMessage(StreamID(2), PPID(54), {1, 2, 3, 4, 5}));
  EXPECT_EQ(buf_.total_buffered_amount(), 8u);

  buf_.PrepareResetStream(StreamID(1));
  EXPECT_EQ(buf_.total_buffered_amount(), 5u);

  EXPECT_THAT(buf_.GetStreamsReadyToBeReset(),
              UnorderedElementsAre(StreamID(1)));
  buf_.CommitResetStreams();
  buf_.PrepareResetStream(StreamID(2));
  EXPECT_EQ(buf_.total_buffered_amount(), 0u);
}

TEST_F(RRSendQueueTest, PrepareResetStreamsNotPartialPackets) {
  std::vector<uint8_t> payload(120);

  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  absl::optional<SendQueue::DataToSend> chunk_one = buf_.Produce(kNow, 50);
  ASSERT_TRUE(chunk_one.has_value());
  EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  EXPECT_EQ(buf_.total_buffered_amount(), 2 * payload.size() - 50);

  buf_.PrepareResetStream(StreamID(1));
  EXPECT_EQ(buf_.total_buffered_amount(), payload.size() - 50);
}

TEST_F(RRSendQueueTest, EnqueuedItemsArePausedDuringStreamReset) {
  std::vector<uint8_t> payload(50);

  buf_.PrepareResetStream(StreamID(1));
  EXPECT_EQ(buf_.total_buffered_amount(), 0u);

  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  EXPECT_EQ(buf_.total_buffered_amount(), payload.size());

  EXPECT_FALSE(buf_.Produce(kNow, kOneFragmentPacketSize).has_value());

  EXPECT_TRUE(buf_.HasStreamsReadyToBeReset());
  EXPECT_THAT(buf_.GetStreamsReadyToBeReset(),
              UnorderedElementsAre(StreamID(1)));

  EXPECT_FALSE(buf_.Produce(kNow, kOneFragmentPacketSize).has_value());

  buf_.CommitResetStreams();
  EXPECT_EQ(buf_.total_buffered_amount(), payload.size());

  absl::optional<SendQueue::DataToSend> chunk_one = buf_.Produce(kNow, 50);
  ASSERT_TRUE(chunk_one.has_value());
  EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  EXPECT_EQ(buf_.total_buffered_amount(), 0u);
}

TEST_F(RRSendQueueTest, PausedStreamsStillSendPartialMessagesUntilEnd) {
  constexpr size_t kPayloadSize = 100;
  constexpr size_t kFragmentSize = 50;
  std::vector<uint8_t> payload(kPayloadSize);

  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  absl::optional<SendQueue::DataToSend> chunk_one =
      buf_.Produce(kNow, kFragmentSize);
  ASSERT_TRUE(chunk_one.has_value());
  EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  EXPECT_EQ(buf_.total_buffered_amount(), 2 * kPayloadSize - kFragmentSize);

  // This will stop the second message from being sent.
  buf_.PrepareResetStream(StreamID(1));
  EXPECT_EQ(buf_.total_buffered_amount(), 1 * kPayloadSize - kFragmentSize);

  // Should still produce fragments until end of message.
  absl::optional<SendQueue::DataToSend> chunk_two =
      buf_.Produce(kNow, kFragmentSize);
  ASSERT_TRUE(chunk_two.has_value());
  EXPECT_EQ(chunk_two->data.stream_id, kStreamID);
  EXPECT_EQ(buf_.total_buffered_amount(), 0ul);

  // But shouldn't produce any more messages as the stream is paused.
  EXPECT_FALSE(buf_.Produce(kNow, kFragmentSize).has_value());
}

TEST_F(RRSendQueueTest, CommittingResetsSSN) {
  std::vector<uint8_t> payload(50);

  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  absl::optional<SendQueue::DataToSend> chunk_one =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_one.has_value());
  EXPECT_EQ(chunk_one->data.ssn, SSN(0));

  absl::optional<SendQueue::DataToSend> chunk_two =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_two.has_value());
  EXPECT_EQ(chunk_two->data.ssn, SSN(1));

  buf_.PrepareResetStream(StreamID(1));

  // Buffered
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  EXPECT_TRUE(buf_.HasStreamsReadyToBeReset());
  EXPECT_THAT(buf_.GetStreamsReadyToBeReset(),
              UnorderedElementsAre(StreamID(1)));
  buf_.CommitResetStreams();

  absl::optional<SendQueue::DataToSend> chunk_three =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_three.has_value());
  EXPECT_EQ(chunk_three->data.ssn, SSN(0));
}

TEST_F(RRSendQueueTest, CommittingDoesNotResetMessageId) {
  std::vector<uint8_t> payload(50);

  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk1.data.ssn, SSN(0));
  EXPECT_EQ(chunk1.message_id, OutgoingMessageId(0));

  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk2.data.ssn, SSN(1));
  EXPECT_EQ(chunk2.message_id, OutgoingMessageId(1));

  buf_.PrepareResetStream(kStreamID);
  EXPECT_THAT(buf_.GetStreamsReadyToBeReset(), UnorderedElementsAre(kStreamID));
  buf_.CommitResetStreams();

  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk3,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk3.data.ssn, SSN(0));
  EXPECT_EQ(chunk3.message_id, OutgoingMessageId(2));
}

TEST_F(RRSendQueueTest, CommittingResetsSSNForPausedStreamsOnly) {
  std::vector<uint8_t> payload(50);

  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, payload));
  buf_.Add(kNow, DcSctpMessage(StreamID(3), kPPID, payload));

  absl::optional<SendQueue::DataToSend> chunk_one =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_one.has_value());
  EXPECT_EQ(chunk_one->data.stream_id, StreamID(1));
  EXPECT_EQ(chunk_one->data.ssn, SSN(0));

  absl::optional<SendQueue::DataToSend> chunk_two =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_two.has_value());
  EXPECT_EQ(chunk_two->data.stream_id, StreamID(3));
  EXPECT_EQ(chunk_two->data.ssn, SSN(0));

  buf_.PrepareResetStream(StreamID(3));

  // Send two more messages - SID 3 will buffer, SID 1 will send.
  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, payload));
  buf_.Add(kNow, DcSctpMessage(StreamID(3), kPPID, payload));

  EXPECT_TRUE(buf_.HasStreamsReadyToBeReset());
  EXPECT_THAT(buf_.GetStreamsReadyToBeReset(),
              UnorderedElementsAre(StreamID(3)));

  buf_.CommitResetStreams();

  absl::optional<SendQueue::DataToSend> chunk_three =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_three.has_value());
  EXPECT_EQ(chunk_three->data.stream_id, StreamID(1));
  EXPECT_EQ(chunk_three->data.ssn, SSN(1));

  absl::optional<SendQueue::DataToSend> chunk_four =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_four.has_value());
  EXPECT_EQ(chunk_four->data.stream_id, StreamID(3));
  EXPECT_EQ(chunk_four->data.ssn, SSN(0));
}

TEST_F(RRSendQueueTest, RollBackResumesSSN) {
  std::vector<uint8_t> payload(50);

  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  absl::optional<SendQueue::DataToSend> chunk_one =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_one.has_value());
  EXPECT_EQ(chunk_one->data.ssn, SSN(0));

  absl::optional<SendQueue::DataToSend> chunk_two =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_two.has_value());
  EXPECT_EQ(chunk_two->data.ssn, SSN(1));

  buf_.PrepareResetStream(StreamID(1));

  // Buffered
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  EXPECT_TRUE(buf_.HasStreamsReadyToBeReset());
  EXPECT_THAT(buf_.GetStreamsReadyToBeReset(),
              UnorderedElementsAre(StreamID(1)));
  buf_.RollbackResetStreams();

  absl::optional<SendQueue::DataToSend> chunk_three =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_three.has_value());
  EXPECT_EQ(chunk_three->data.ssn, SSN(2));
}

TEST_F(RRSendQueueTest, ReturnsFragmentsForOneMessageBeforeMovingToNext) {
  std::vector<uint8_t> payload(200);
  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, payload));
  buf_.Add(kNow, DcSctpMessage(StreamID(2), kPPID, payload));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk1.data.stream_id, StreamID(1));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk2.data.stream_id, StreamID(1));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk3,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk3.data.stream_id, StreamID(2));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk4,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk4.data.stream_id, StreamID(2));
}

TEST_F(RRSendQueueTest, ReturnsAlsoSmallFragmentsBeforeMovingToNext) {
  std::vector<uint8_t> payload(kTwoFragmentPacketSize);
  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, payload));
  buf_.Add(kNow, DcSctpMessage(StreamID(2), kPPID, payload));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk1.data.payload, SizeIs(kOneFragmentPacketSize));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk2.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk2.data.payload,
              SizeIs(kTwoFragmentPacketSize - kOneFragmentPacketSize));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk3,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk3.data.stream_id, StreamID(2));
  EXPECT_THAT(chunk3.data.payload, SizeIs(kOneFragmentPacketSize));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk4,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk4.data.stream_id, StreamID(2));
  EXPECT_THAT(chunk4.data.payload,
              SizeIs(kTwoFragmentPacketSize - kOneFragmentPacketSize));
}

TEST_F(RRSendQueueTest, WillCycleInRoundRobinFashionBetweenStreams) {
  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(1)));
  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(2)));
  buf_.Add(kNow, DcSctpMessage(StreamID(2), kPPID, std::vector<uint8_t>(3)));
  buf_.Add(kNow, DcSctpMessage(StreamID(2), kPPID, std::vector<uint8_t>(4)));
  buf_.Add(kNow, DcSctpMessage(StreamID(3), kPPID, std::vector<uint8_t>(5)));
  buf_.Add(kNow, DcSctpMessage(StreamID(3), kPPID, std::vector<uint8_t>(6)));
  buf_.Add(kNow, DcSctpMessage(StreamID(4), kPPID, std::vector<uint8_t>(7)));
  buf_.Add(kNow, DcSctpMessage(StreamID(4), kPPID, std::vector<uint8_t>(8)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk1.data.payload, SizeIs(1));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk2.data.stream_id, StreamID(2));
  EXPECT_THAT(chunk2.data.payload, SizeIs(3));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk3,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk3.data.stream_id, StreamID(3));
  EXPECT_THAT(chunk3.data.payload, SizeIs(5));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk4,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk4.data.stream_id, StreamID(4));
  EXPECT_THAT(chunk4.data.payload, SizeIs(7));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk5,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk5.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk5.data.payload, SizeIs(2));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk6,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk6.data.stream_id, StreamID(2));
  EXPECT_THAT(chunk6.data.payload, SizeIs(4));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk7,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk7.data.stream_id, StreamID(3));
  EXPECT_THAT(chunk7.data.payload, SizeIs(6));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk8,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk8.data.stream_id, StreamID(4));
  EXPECT_THAT(chunk8.data.payload, SizeIs(8));
}

TEST_F(RRSendQueueTest, DoesntTriggerOnBufferedAmountLowWhenSetToZero) {
  EXPECT_CALL(callbacks_, OnBufferedAmountLow).Times(0);
  buf_.SetBufferedAmountLowThreshold(StreamID(1), 0u);
}

TEST_F(RRSendQueueTest, TriggersOnBufferedAmountAtZeroLowWhenSent) {
  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(1)));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 1u);

  EXPECT_CALL(callbacks_, OnBufferedAmountLow(StreamID(1)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk1.data.payload, SizeIs(1));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 0u);
}

TEST_F(RRSendQueueTest, WillRetriggerOnBufferedAmountLowIfAddingMore) {
  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(1)));

  EXPECT_CALL(callbacks_, OnBufferedAmountLow(StreamID(1)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk1.data.payload, SizeIs(1));

  EXPECT_CALL(callbacks_, OnBufferedAmountLow).Times(0);

  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(1)));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 1u);

  // Should now trigger again, as buffer_amount went above the threshold.
  EXPECT_CALL(callbacks_, OnBufferedAmountLow(StreamID(1)));
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk2.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk2.data.payload, SizeIs(1));
}

TEST_F(RRSendQueueTest, OnlyTriggersWhenTransitioningFromAboveToBelowOrEqual) {
  buf_.SetBufferedAmountLowThreshold(StreamID(1), 1000);

  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(10)));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 10u);

  EXPECT_CALL(callbacks_, OnBufferedAmountLow).Times(0);
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk1.data.payload, SizeIs(10));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 0u);

  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(20)));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 20u);

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk2.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk2.data.payload, SizeIs(20));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 0u);
}

TEST_F(RRSendQueueTest, WillTriggerOnBufferedAmountLowSetAboveZero) {
  EXPECT_CALL(callbacks_, OnBufferedAmountLow).Times(0);

  buf_.SetBufferedAmountLowThreshold(StreamID(1), 700);

  std::vector<uint8_t> payload(1000);
  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, payload));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk1.data.payload, SizeIs(kOneFragmentPacketSize));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 900u);

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk2.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk2.data.payload, SizeIs(kOneFragmentPacketSize));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 800u);

  EXPECT_CALL(callbacks_, OnBufferedAmountLow(StreamID(1)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk3,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk3.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk3.data.payload, SizeIs(kOneFragmentPacketSize));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 700u);

  // Doesn't trigger when reducing even further.
  EXPECT_CALL(callbacks_, OnBufferedAmountLow).Times(0);

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk4,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk3.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk3.data.payload, SizeIs(kOneFragmentPacketSize));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 600u);
}

TEST_F(RRSendQueueTest, WillRetriggerOnBufferedAmountLowSetAboveZero) {
  EXPECT_CALL(callbacks_, OnBufferedAmountLow).Times(0);

  buf_.SetBufferedAmountLowThreshold(StreamID(1), 700);

  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(1000)));

  EXPECT_CALL(callbacks_, OnBufferedAmountLow(StreamID(1)));
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              buf_.Produce(kNow, 400));
  EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk1.data.payload, SizeIs(400));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 600u);

  EXPECT_CALL(callbacks_, OnBufferedAmountLow).Times(0);
  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(200)));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 800u);

  // Will trigger again, as it went above the limit.
  EXPECT_CALL(callbacks_, OnBufferedAmountLow(StreamID(1)));
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              buf_.Produce(kNow, 200));
  EXPECT_EQ(chunk2.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk2.data.payload, SizeIs(200));
  EXPECT_EQ(buf_.buffered_amount(StreamID(1)), 600u);
}

TEST_F(RRSendQueueTest, TriggersOnBufferedAmountLowOnThresholdChanged) {
  EXPECT_CALL(callbacks_, OnBufferedAmountLow).Times(0);

  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(100)));

  // Modifying the threshold, still under buffered_amount, should not trigger.
  buf_.SetBufferedAmountLowThreshold(StreamID(1), 50);
  buf_.SetBufferedAmountLowThreshold(StreamID(1), 99);

  // When the threshold reaches buffered_amount, it will trigger.
  EXPECT_CALL(callbacks_, OnBufferedAmountLow(StreamID(1)));
  buf_.SetBufferedAmountLowThreshold(StreamID(1), 100);

  // But not when it's set low again.
  EXPECT_CALL(callbacks_, OnBufferedAmountLow).Times(0);
  buf_.SetBufferedAmountLowThreshold(StreamID(1), 50);

  // But it will trigger when it overshoots.
  EXPECT_CALL(callbacks_, OnBufferedAmountLow(StreamID(1)));
  buf_.SetBufferedAmountLowThreshold(StreamID(1), 150);

  // But not when it's set low again.
  EXPECT_CALL(callbacks_, OnBufferedAmountLow).Times(0);
  buf_.SetBufferedAmountLowThreshold(StreamID(1), 0);
}

TEST_F(RRSendQueueTest,
       OnTotalBufferedAmountLowDoesNotTriggerOnBufferFillingUp) {
  EXPECT_CALL(callbacks_, OnTotalBufferedAmountLow).Times(0);
  std::vector<uint8_t> payload(kBufferedAmountLowThreshold - 1);
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  EXPECT_EQ(buf_.total_buffered_amount(), payload.size());

  // Will not trigger if going above but never below.
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID,
                               std::vector<uint8_t>(kOneFragmentPacketSize)));
}

TEST_F(RRSendQueueTest, TriggersOnTotalBufferedAmountLowWhenCrossing) {
  EXPECT_CALL(callbacks_, OnTotalBufferedAmountLow).Times(0);
  std::vector<uint8_t> payload(kBufferedAmountLowThreshold);
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  EXPECT_EQ(buf_.total_buffered_amount(), payload.size());

  // Reaches it.
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, std::vector<uint8_t>(1)));

  // Drain it a bit - will trigger.
  EXPECT_CALL(callbacks_, OnTotalBufferedAmountLow).Times(1);
  absl::optional<SendQueue::DataToSend> chunk_two =
      buf_.Produce(kNow, kOneFragmentPacketSize);
}

TEST_F(RRSendQueueTest, WillStayInAStreamAsLongAsThatMessageIsSending) {
  buf_.Add(kNow, DcSctpMessage(StreamID(5), kPPID, std::vector<uint8_t>(1)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk1.data.stream_id, StreamID(5));
  EXPECT_THAT(chunk1.data.payload, SizeIs(1));

  // Next, it should pick a different stream.

  buf_.Add(kNow,
           DcSctpMessage(StreamID(1), kPPID,
                         std::vector<uint8_t>(kOneFragmentPacketSize * 2)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk2.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk2.data.payload, SizeIs(kOneFragmentPacketSize));

  // It should still stay on the Stream1 now, even if might be tempted to switch
  // to this stream, as it's the stream following 5.
  buf_.Add(kNow, DcSctpMessage(StreamID(6), kPPID, std::vector<uint8_t>(1)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk3,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk3.data.stream_id, StreamID(1));
  EXPECT_THAT(chunk3.data.payload, SizeIs(kOneFragmentPacketSize));

  // After stream id 1 is complete, it's time to do stream 6.
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk4,
                              buf_.Produce(kNow, kOneFragmentPacketSize));
  EXPECT_EQ(chunk4.data.stream_id, StreamID(6));
  EXPECT_THAT(chunk4.data.payload, SizeIs(1));

  EXPECT_FALSE(buf_.Produce(kNow, kOneFragmentPacketSize).has_value());
}

TEST_F(RRSendQueueTest, StreamsHaveInitialPriority) {
  EXPECT_EQ(buf_.GetStreamPriority(StreamID(1)), kDefaultPriority);

  buf_.Add(kNow, DcSctpMessage(StreamID(2), kPPID, std::vector<uint8_t>(40)));
  EXPECT_EQ(buf_.GetStreamPriority(StreamID(2)), kDefaultPriority);
}

TEST_F(RRSendQueueTest, CanChangeStreamPriority) {
  buf_.SetStreamPriority(StreamID(1), StreamPriority(42));
  EXPECT_EQ(buf_.GetStreamPriority(StreamID(1)), StreamPriority(42));

  buf_.Add(kNow, DcSctpMessage(StreamID(2), kPPID, std::vector<uint8_t>(40)));
  buf_.SetStreamPriority(StreamID(2), StreamPriority(42));
  EXPECT_EQ(buf_.GetStreamPriority(StreamID(2)), StreamPriority(42));
}

TEST_F(RRSendQueueTest, WillHandoverPriority) {
  buf_.SetStreamPriority(StreamID(1), StreamPriority(42));

  buf_.Add(kNow, DcSctpMessage(StreamID(2), kPPID, std::vector<uint8_t>(40)));
  buf_.SetStreamPriority(StreamID(2), StreamPriority(42));

  DcSctpSocketHandoverState state;
  buf_.AddHandoverState(state);

  RRSendQueue q2("log: ", &callbacks_, kMtu, kDefaultPriority,
                 kBufferedAmountLowThreshold);
  q2.RestoreFromState(state);
  EXPECT_EQ(q2.GetStreamPriority(StreamID(1)), StreamPriority(42));
  EXPECT_EQ(q2.GetStreamPriority(StreamID(2)), StreamPriority(42));
}

TEST_F(RRSendQueueTest, WillSendMessagesByPrio) {
  buf_.EnableMessageInterleaving(true);
  buf_.SetStreamPriority(StreamID(1), StreamPriority(10));
  buf_.SetStreamPriority(StreamID(2), StreamPriority(20));
  buf_.SetStreamPriority(StreamID(3), StreamPriority(30));

  buf_.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(40)));
  buf_.Add(kNow, DcSctpMessage(StreamID(2), kPPID, std::vector<uint8_t>(20)));
  buf_.Add(kNow, DcSctpMessage(StreamID(3), kPPID, std::vector<uint8_t>(10)));
  std::vector<uint16_t> expected_streams = {3, 2, 2, 1, 1, 1, 1};

  for (uint16_t stream_num : expected_streams) {
    ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk,
                                buf_.Produce(kNow, 10));
    EXPECT_EQ(chunk.data.stream_id, StreamID(stream_num));
  }
  EXPECT_FALSE(buf_.Produce(kNow, 1).has_value());
}

TEST_F(RRSendQueueTest, WillSendLifecycleExpireWhenExpiredInSendQueue) {
  std::vector<uint8_t> payload(kOneFragmentPacketSize);
  buf_.Add(kNow, DcSctpMessage(StreamID(2), kPPID, payload),
           SendOptions{.lifetime = DurationMs(1000),
                       .lifecycle_id = LifecycleId(1)});

  EXPECT_CALL(callbacks_, OnLifecycleMessageExpired(LifecycleId(1),
                                                    /*maybe_delivered=*/false));
  EXPECT_CALL(callbacks_, OnLifecycleEnd(LifecycleId(1)));
  EXPECT_FALSE(
      buf_.Produce(kNow + TimeDelta::Millis(1001), kOneFragmentPacketSize)
          .has_value());
}

TEST_F(RRSendQueueTest, WillSendLifecycleExpireWhenDiscardingDuringPause) {
  std::vector<uint8_t> payload(120);

  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload),
           SendOptions{.lifecycle_id = LifecycleId(1)});
  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload),
           SendOptions{.lifecycle_id = LifecycleId(2)});

  absl::optional<SendQueue::DataToSend> chunk_one = buf_.Produce(kNow, 50);
  ASSERT_TRUE(chunk_one.has_value());
  EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  EXPECT_EQ(buf_.total_buffered_amount(), 2 * payload.size() - 50);

  EXPECT_CALL(callbacks_, OnLifecycleMessageExpired(LifecycleId(2),
                                                    /*maybe_delivered=*/false));
  EXPECT_CALL(callbacks_, OnLifecycleEnd(LifecycleId(2)));
  buf_.PrepareResetStream(StreamID(1));
  EXPECT_EQ(buf_.total_buffered_amount(), payload.size() - 50);
}

TEST_F(RRSendQueueTest, WillSendLifecycleExpireWhenDiscardingExplicitly) {
  std::vector<uint8_t> payload(kOneFragmentPacketSize + 20);

  buf_.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload),
           SendOptions{.lifecycle_id = LifecycleId(1)});

  absl::optional<SendQueue::DataToSend> chunk_one =
      buf_.Produce(kNow, kOneFragmentPacketSize);
  ASSERT_TRUE(chunk_one.has_value());
  EXPECT_FALSE(chunk_one->data.is_end);
  EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  EXPECT_CALL(callbacks_, OnLifecycleMessageExpired(LifecycleId(1),
                                                    /*maybe_delivered=*/false));
  EXPECT_CALL(callbacks_, OnLifecycleEnd(LifecycleId(1)));
  buf_.Discard(chunk_one->data.stream_id, chunk_one->message_id);
}
}  // namespace
}  // namespace dcsctp
