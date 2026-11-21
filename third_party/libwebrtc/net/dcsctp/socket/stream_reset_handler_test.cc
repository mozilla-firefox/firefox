/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "net/dcsctp/socket/stream_reset_handler.h"

#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include "absl/types/optional.h"
#include "api/array_view.h"
#include "api/task_queue/task_queue_base.h"
#include "net/dcsctp/common/handover_testing.h"
#include "net/dcsctp/common/internal_types.h"
#include "net/dcsctp/packet/chunk/forward_tsn_common.h"
#include "net/dcsctp/packet/chunk/reconfig_chunk.h"
#include "net/dcsctp/packet/parameter/incoming_ssn_reset_request_parameter.h"
#include "net/dcsctp/packet/parameter/outgoing_ssn_reset_request_parameter.h"
#include "net/dcsctp/packet/parameter/parameter.h"
#include "net/dcsctp/packet/parameter/reconfiguration_response_parameter.h"
#include "net/dcsctp/public/dcsctp_message.h"
#include "net/dcsctp/public/types.h"
#include "net/dcsctp/rx/data_tracker.h"
#include "net/dcsctp/rx/reassembly_queue.h"
#include "net/dcsctp/socket/mock_context.h"
#include "net/dcsctp/socket/mock_dcsctp_socket_callbacks.h"
#include "net/dcsctp/testing/data_generator.h"
#include "net/dcsctp/testing/testing_macros.h"
#include "net/dcsctp/timer/timer.h"
#include "net/dcsctp/tx/mock_send_queue.h"
#include "net/dcsctp/tx/retransmission_queue.h"
#include "rtc_base/gunit.h"
#include "test/gmock.h"

namespace dcsctp {
namespace {
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Property;
using ::testing::Return;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;
using ::webrtc::TimeDelta;
using ResponseResult = ReconfigurationResponseParameter::Result;
using SkippedStream = AnyForwardTsnChunk::SkippedStream;

constexpr TSN kMyInitialTsn = MockContext::MyInitialTsn();
constexpr ReconfigRequestSN kMyInitialReqSn = ReconfigRequestSN(*kMyInitialTsn);
constexpr TSN kPeerInitialTsn = MockContext::PeerInitialTsn();
constexpr ReconfigRequestSN kPeerInitialReqSn =
    ReconfigRequestSN(*kPeerInitialTsn);
constexpr uint32_t kArwnd = 131072;
constexpr TimeDelta kRto = TimeDelta::Millis(250);

constexpr std::array<uint8_t, 4> kShortPayload = {1, 2, 3, 4};

MATCHER_P3(SctpMessageIs, stream_id, ppid, expected_payload, "") {
  if (arg.stream_id() != stream_id) {
    *result_listener << "the stream_id is " << *arg.stream_id();
    return false;
  }

  if (arg.ppid() != ppid) {
    *result_listener << "the ppid is " << *arg.ppid();
    return false;
  }

  if (std::vector<uint8_t>(arg.payload().begin(), arg.payload().end()) !=
      std::vector<uint8_t>(expected_payload.begin(), expected_payload.end())) {
    *result_listener << "the payload is wrong";
    return false;
  }
  return true;
}

TSN AddTo(TSN tsn, int delta) {
  return TSN(*tsn + delta);
}

ReconfigRequestSN AddTo(ReconfigRequestSN req_sn, int delta) {
  return ReconfigRequestSN(*req_sn + delta);
}

class StreamResetHandlerTest : public testing::Test {
 protected:
  StreamResetHandlerTest()
      : ctx_(&callbacks_),
        timer_manager_([this](webrtc::TaskQueueBase::DelayPrecision precision) {
          return callbacks_.CreateTimeout(precision);
        }),
        delayed_ack_timer_(timer_manager_.CreateTimer(
            "test/delayed_ack",
            []() { return TimeDelta::Zero(); },
            TimerOptions(TimeDelta::Zero()))),
        t3_rtx_timer_(timer_manager_.CreateTimer(
            "test/t3_rtx",
            []() { return TimeDelta::Zero(); },
            TimerOptions(TimeDelta::Zero()))),
        data_tracker_(std::make_unique<DataTracker>("log: ",
                                                    delayed_ack_timer_.get(),
                                                    kPeerInitialTsn)),
        reasm_(std::make_unique<ReassemblyQueue>("log: ", kArwnd)),
        retransmission_queue_(std::make_unique<RetransmissionQueue>(
            "",
            &callbacks_,
            kMyInitialTsn,
            kArwnd,
            producer_,
            [](TimeDelta rtt) {},
            []() {},
            *t3_rtx_timer_,
            DcSctpOptions())),
        handler_(
            std::make_unique<StreamResetHandler>("log: ",
                                                 &ctx_,
                                                 &timer_manager_,
                                                 data_tracker_.get(),
                                                 reasm_.get(),
                                                 retransmission_queue_.get())) {
    EXPECT_CALL(ctx_, current_rto).WillRepeatedly(Return(kRto));
  }

  void AdvanceTime(TimeDelta duration) {
    callbacks_.AdvanceTime(duration);
    for (;;) {
      absl::optional<TimeoutID> timeout_id = callbacks_.GetNextExpiredTimeout();
      if (!timeout_id.has_value()) {
        break;
      }
      timer_manager_.HandleTimeout(*timeout_id);
    }
  }

  // Handles the passed in RE-CONFIG `chunk` and returns the responses
  // that are sent in the response RE-CONFIG.
  std::vector<ReconfigurationResponseParameter> HandleAndCatchResponse(
      ReConfigChunk chunk) {
    handler_->HandleReConfig(std::move(chunk));

    std::vector<uint8_t> payload = callbacks_.ConsumeSentPacket();
    if (payload.empty()) {
      EXPECT_TRUE(false);
      return {};
    }

    std::vector<ReconfigurationResponseParameter> responses;
    absl::optional<SctpPacket> p = SctpPacket::Parse(payload, DcSctpOptions());
    if (!p.has_value()) {
      EXPECT_TRUE(false);
      return {};
    }
    if (p->descriptors().size() != 1) {
      EXPECT_TRUE(false);
      return {};
    }
    absl::optional<ReConfigChunk> response_chunk =
        ReConfigChunk::Parse(p->descriptors()[0].data);
    if (!response_chunk.has_value()) {
      EXPECT_TRUE(false);
      return {};
    }
    for (const auto& desc : response_chunk->parameters().descriptors()) {
      if (desc.type == ReconfigurationResponseParameter::kType) {
        absl::optional<ReconfigurationResponseParameter> response =
            ReconfigurationResponseParameter::Parse(desc.data);
        if (!response.has_value()) {
          EXPECT_TRUE(false);
          return {};
        }
        responses.emplace_back(*std::move(response));
      }
    }
    return responses;
  }

  void PerformHandover() {
    EXPECT_TRUE(handler_->GetHandoverReadiness().IsReady());
    EXPECT_TRUE(data_tracker_->GetHandoverReadiness().IsReady());
    EXPECT_TRUE(reasm_->GetHandoverReadiness().IsReady());
    EXPECT_TRUE(retransmission_queue_->GetHandoverReadiness().IsReady());

    DcSctpSocketHandoverState state;
    handler_->AddHandoverState(state);
    data_tracker_->AddHandoverState(state);
    reasm_->AddHandoverState(state);

    retransmission_queue_->AddHandoverState(state);

    g_handover_state_transformer_for_test(&state);

    data_tracker_ = std::make_unique<DataTracker>(
        "log: ", delayed_ack_timer_.get(), kPeerInitialTsn);
    data_tracker_->RestoreFromState(state);
    reasm_ = std::make_unique<ReassemblyQueue>("log: ", kArwnd);
    reasm_->RestoreFromState(state);
    retransmission_queue_ = std::make_unique<RetransmissionQueue>(
        "", &callbacks_, kMyInitialTsn, kArwnd, producer_, [](TimeDelta rtt) {},
        []() {}, *t3_rtx_timer_, DcSctpOptions(),
        /*supports_partial_reliability=*/true,
        /*use_message_interleaving=*/false);
    retransmission_queue_->RestoreFromState(state);
    handler_ = std::make_unique<StreamResetHandler>(
        "log: ", &ctx_, &timer_manager_, data_tracker_.get(), reasm_.get(),
        retransmission_queue_.get(), &state);
  }

  DataGenerator gen_;
  NiceMock<MockDcSctpSocketCallbacks> callbacks_;
  NiceMock<MockContext> ctx_;
  NiceMock<MockSendQueue> producer_;
  TimerManager timer_manager_;
  std::unique_ptr<Timer> delayed_ack_timer_;
  std::unique_ptr<Timer> t3_rtx_timer_;
  std::unique_ptr<DataTracker> data_tracker_;
  std::unique_ptr<ReassemblyQueue> reasm_;
  std::unique_ptr<RetransmissionQueue> retransmission_queue_;
  std::unique_ptr<StreamResetHandler> handler_;
};

TEST_F(StreamResetHandlerTest, ChunkWithNoParametersReturnsError) {
  EXPECT_CALL(callbacks_, SendPacketWithStatus).Times(0);
  EXPECT_CALL(callbacks_, OnError).Times(1);
  handler_->HandleReConfig(ReConfigChunk(Parameters()));
}

TEST_F(StreamResetHandlerTest, ChunkWithInvalidParametersReturnsError) {
  Parameters::Builder builder;
  // Two OutgoingSSNResetRequestParameter in a RE-CONFIG is not valid.
  builder.Add(OutgoingSSNResetRequestParameter(ReconfigRequestSN(1),
                                               ReconfigRequestSN(10),
                                               kPeerInitialTsn, {StreamID(1)}));
  builder.Add(OutgoingSSNResetRequestParameter(ReconfigRequestSN(2),
                                               ReconfigRequestSN(10),
                                               kPeerInitialTsn, {StreamID(2)}));

  EXPECT_CALL(callbacks_, SendPacketWithStatus).Times(0);
  EXPECT_CALL(callbacks_, OnError).Times(1);
  handler_->HandleReConfig(ReConfigChunk(builder.Build()));
}

TEST_F(StreamResetHandlerTest, FailToDeliverWithoutResettingStream) {
  reasm_->Add(kPeerInitialTsn, gen_.Ordered({1, 2, 3, 4}, "BE"));
  reasm_->Add(AddTo(kPeerInitialTsn, 1), gen_.Ordered({1, 2, 3, 4}, "BE"));

  data_tracker_->Observe(kPeerInitialTsn);
  data_tracker_->Observe(AddTo(kPeerInitialTsn, 1));
  EXPECT_THAT(reasm_->FlushMessages(),
              UnorderedElementsAre(
                  SctpMessageIs(StreamID(1), PPID(53), kShortPayload),
                  SctpMessageIs(StreamID(1), PPID(53), kShortPayload)));

  gen_.ResetStream();
  reasm_->Add(AddTo(kPeerInitialTsn, 2), gen_.Ordered({1, 2, 3, 4}, "BE"));
  EXPECT_THAT(reasm_->FlushMessages(), IsEmpty());
}

TEST_F(StreamResetHandlerTest, ResetStreamsNotDeferred) {
  reasm_->Add(kPeerInitialTsn, gen_.Ordered({1, 2, 3, 4}, "BE"));
  reasm_->Add(AddTo(kPeerInitialTsn, 1), gen_.Ordered({1, 2, 3, 4}, "BE"));

  data_tracker_->Observe(kPeerInitialTsn);
  data_tracker_->Observe(AddTo(kPeerInitialTsn, 1));
  EXPECT_THAT(reasm_->FlushMessages(),
              UnorderedElementsAre(
                  SctpMessageIs(StreamID(1), PPID(53), kShortPayload),
                  SctpMessageIs(StreamID(1), PPID(53), kShortPayload)));

  Parameters::Builder builder;
  builder.Add(OutgoingSSNResetRequestParameter(
      kPeerInitialReqSn, ReconfigRequestSN(3), AddTo(kPeerInitialTsn, 1),
      {StreamID(1)}));

  std::vector<ReconfigurationResponseParameter> responses =
      HandleAndCatchResponse(ReConfigChunk(builder.Build()));
  EXPECT_THAT(responses, SizeIs(1));
  EXPECT_EQ(responses[0].result(), ResponseResult::kSuccessPerformed);

  gen_.ResetStream();
  reasm_->Add(AddTo(kPeerInitialTsn, 2), gen_.Ordered({1, 2, 3, 4}, "BE"));
  EXPECT_THAT(reasm_->FlushMessages(),
              UnorderedElementsAre(
                  SctpMessageIs(StreamID(1), PPID(53), kShortPayload)));
}

TEST_F(StreamResetHandlerTest, ResetStreamsDeferred) {
  constexpr StreamID kStreamId = StreamID(1);
  data_tracker_->Observe(TSN(10));
  reasm_->Add(TSN(10), gen_.Ordered({1, 2, 3, 4}, "BE", {.mid = MID(0)}));

  data_tracker_->Observe(TSN(11));
  reasm_->Add(TSN(11), gen_.Ordered({1, 2, 3, 4}, "BE", {.mid = MID(1)}));

  EXPECT_THAT(
      reasm_->FlushMessages(),
      UnorderedElementsAre(SctpMessageIs(kStreamId, PPID(53), kShortPayload),
                           SctpMessageIs(kStreamId, PPID(53), kShortPayload)));

  Parameters::Builder builder;
  builder.Add(OutgoingSSNResetRequestParameter(
      ReconfigRequestSN(10), ReconfigRequestSN(3), TSN(13), {kStreamId}));
  EXPECT_THAT(HandleAndCatchResponse(ReConfigChunk(builder.Build())),
              ElementsAre(Property(&ReconfigurationResponseParameter::result,
                                   ResponseResult::kInProgress)));

  data_tracker_->Observe(TSN(15));
  reasm_->Add(TSN(15), gen_.Ordered({1, 2, 3, 4}, "BE",
                                    {.mid = MID(1), .ppid = PPID(5)}));

  data_tracker_->Observe(TSN(14));
  reasm_->Add(TSN(14), gen_.Ordered({1, 2, 3, 4}, "BE",
                                    {.mid = MID(0), .ppid = PPID(4)}));

  data_tracker_->Observe(TSN(13));
  reasm_->Add(TSN(13), gen_.Ordered({1, 2, 3, 4}, "BE",
                                    {.mid = MID(3), .ppid = PPID(3)}));

  data_tracker_->Observe(TSN(12));
  reasm_->Add(TSN(12), gen_.Ordered({1, 2, 3, 4}, "BE",
                                    {.mid = MID(2), .ppid = PPID(2)}));

  builder.Add(OutgoingSSNResetRequestParameter(
      ReconfigRequestSN(11), ReconfigRequestSN(4), TSN(13), {kStreamId}));
  EXPECT_THAT(HandleAndCatchResponse(ReConfigChunk(builder.Build())),
              ElementsAre(Property(&ReconfigurationResponseParameter::result,
                                   ResponseResult::kSuccessPerformed)));

  EXPECT_THAT(
      reasm_->FlushMessages(),
      UnorderedElementsAre(SctpMessageIs(kStreamId, PPID(2), kShortPayload),
                           SctpMessageIs(kStreamId, PPID(3), kShortPayload),
                           SctpMessageIs(kStreamId, PPID(4), kShortPayload),
                           SctpMessageIs(kStreamId, PPID(5), kShortPayload)));
}

TEST_F(StreamResetHandlerTest, ResetStreamsDeferredOnlySelectedStreams) {
  // This test verifies the receiving behavior of receiving messages on
  // streams 1, 2 and 3, and receiving a reset request on stream 1, 2, causing
  // deferred reset processing.

  // Reset stream 1,2 with "last assigned TSN=12"
  Parameters::Builder builder;
  builder.Add(OutgoingSSNResetRequestParameter(ReconfigRequestSN(10),
                                               ReconfigRequestSN(3), TSN(12),
                                               {StreamID(1), StreamID(2)}));
  EXPECT_THAT(HandleAndCatchResponse(ReConfigChunk(builder.Build())),
              ElementsAre(Property(&ReconfigurationResponseParameter::result,
                                   ResponseResult::kInProgress)));

  // TSN 10, SID 1 - before TSN 12 -> deliver
  data_tracker_->Observe(TSN(10));
  reasm_->Add(TSN(10), gen_.Ordered({1, 2, 3, 4}, "BE",
                                    {.stream_id = StreamID(1),
                                     .mid = MID(0),
                                     .ppid = PPID(1001)}));

  // TSN 11, SID 2 - before TSN 12 -> deliver
  data_tracker_->Observe(TSN(11));
  reasm_->Add(TSN(11), gen_.Ordered({1, 2, 3, 4}, "BE",
                                    {.stream_id = StreamID(2),
                                     .mid = MID(0),
                                     .ppid = PPID(1002)}));

  // TSN 12, SID 3 - at TSN 12 -> deliver
  data_tracker_->Observe(TSN(12));
  reasm_->Add(TSN(12), gen_.Ordered({1, 2, 3, 4}, "BE",
                                    {.stream_id = StreamID(3),
                                     .mid = MID(0),
                                     .ppid = PPID(1003)}));

  // TSN 13, SID 1 - after TSN 12 and SID=1 -> defer
  data_tracker_->Observe(TSN(13));
  reasm_->Add(TSN(13), gen_.Ordered({1, 2, 3, 4}, "BE",
                                    {.stream_id = StreamID(1),
                                     .mid = MID(0),
                                     .ppid = PPID(1004)}));

  // TSN 14, SID 2 - after TSN 12 and SID=2 -> defer
  data_tracker_->Observe(TSN(14));
  reasm_->Add(TSN(14), gen_.Ordered({1, 2, 3, 4}, "BE",
                                    {.stream_id = StreamID(2),
                                     .mid = MID(0),
                                     .ppid = PPID(1005)}));

  // TSN 15, SID 3 - after TSN 12, but SID 3 is not reset -> deliver
  data_tracker_->Observe(TSN(15));
  reasm_->Add(TSN(15), gen_.Ordered({1, 2, 3, 4}, "BE",
                                    {.stream_id = StreamID(3),
                                     .mid = MID(1),
                                     .ppid = PPID(1006)}));

  EXPECT_THAT(reasm_->FlushMessages(),
              UnorderedElementsAre(
                  SctpMessageIs(StreamID(1), PPID(1001), kShortPayload),
                  SctpMessageIs(StreamID(2), PPID(1002), kShortPayload),
                  SctpMessageIs(StreamID(3), PPID(1003), kShortPayload),
                  SctpMessageIs(StreamID(3), PPID(1006), kShortPayload)));

  builder.Add(OutgoingSSNResetRequestParameter(ReconfigRequestSN(11),
                                               ReconfigRequestSN(3), TSN(13),
                                               {StreamID(1), StreamID(2)}));
  EXPECT_THAT(HandleAndCatchResponse(ReConfigChunk(builder.Build())),
              ElementsAre(Property(&ReconfigurationResponseParameter::result,
                                   ResponseResult::kSuccessPerformed)));

  EXPECT_THAT(reasm_->FlushMessages(),
              UnorderedElementsAre(
                  SctpMessageIs(StreamID(1), PPID(1004), kShortPayload),
                  SctpMessageIs(StreamID(2), PPID(1005), kShortPayload)));
}

TEST_F(StreamResetHandlerTest, ResetStreamsDefersForwardTsn) {
  // This test verifies that FORWARD-TSNs are deferred if they want to move
  // the cumulative ack TSN point past sender's last assigned TSN.
  static constexpr StreamID kStreamId = StreamID(42);

  // Simulate sender sends:
  // * TSN 10 (SSN=0, BE, lost),
  // * TSN 11 (SSN=1, BE, lost),
  // * TSN 12 (SSN=2, BE, lost)
  // * RESET THE STREAM
  // * TSN 13 (SSN=0, B, received)
  // * TSN 14 (SSN=0, E, lost),
  // * TSN 15 (SSN=1, BE, received)
  Parameters::Builder builder;
  builder.Add(OutgoingSSNResetRequestParameter(
      ReconfigRequestSN(10), ReconfigRequestSN(3), TSN(12), {kStreamId}));
  EXPECT_THAT(HandleAndCatchResponse(ReConfigChunk(builder.Build())),
              ElementsAre(Property(&ReconfigurationResponseParameter::result,
                                   ResponseResult::kInProgress)));

  // TSN 13, B, after TSN=12 -> defer
  data_tracker_->Observe(TSN(13));
  reasm_->Add(TSN(13),
              gen_.Ordered(
                  {1, 2, 3, 4}, "B",
                  {.stream_id = kStreamId, .mid = MID(0), .ppid = PPID(1004)}));

  // TSN 15, BE, after TSN=12 -> defer
  data_tracker_->Observe(TSN(15));
  reasm_->Add(TSN(15),
              gen_.Ordered(
                  {1, 2, 3, 4}, "BE",
                  {.stream_id = kStreamId, .mid = MID(1), .ppid = PPID(1005)}));

  // Time passes, sender decides to send FORWARD-TSN up to the RESET.
  data_tracker_->HandleForwardTsn(TSN(12));
  reasm_->HandleForwardTsn(
      TSN(12), std::vector<SkippedStream>({SkippedStream(kStreamId, SSN(2))}));

  // The receiver sends a SACK in response to that. The stream hasn't been
  // reset yet, but the sender now decides that TSN=13-14 is to be skipped.
  // As this has a TSN 14, after TSN=12 -> defer it.
  data_tracker_->HandleForwardTsn(TSN(14));
  reasm_->HandleForwardTsn(
      TSN(14), std::vector<SkippedStream>({SkippedStream(kStreamId, SSN(0))}));

  // Reset the stream -> deferred TSNs should be re-added.
  builder.Add(OutgoingSSNResetRequestParameter(
      ReconfigRequestSN(11), ReconfigRequestSN(3), TSN(12), {kStreamId}));
  EXPECT_THAT(HandleAndCatchResponse(ReConfigChunk(builder.Build())),
              ElementsAre(Property(&ReconfigurationResponseParameter::result,
                                   ResponseResult::kSuccessPerformed)));

  EXPECT_THAT(reasm_->FlushMessages(),
              UnorderedElementsAre(
                  SctpMessageIs(kStreamId, PPID(1005), kShortPayload)));
}

TEST_F(StreamResetHandlerTest, SendOutgoingRequestDirectly) {
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(42)));
  handler_->ResetStreams(std::vector<StreamID>({StreamID(42)}));

  EXPECT_CALL(producer_, HasStreamsReadyToBeReset()).WillOnce(Return(true));
  EXPECT_CALL(producer_, GetStreamsReadyToBeReset())
      .WillOnce(Return(std::vector<StreamID>({StreamID(42)})));

  absl::optional<ReConfigChunk> reconfig = handler_->MakeStreamResetRequest();
  ASSERT_TRUE(reconfig.has_value());
  ASSERT_HAS_VALUE_AND_ASSIGN(
      OutgoingSSNResetRequestParameter req,
      reconfig->parameters().get<OutgoingSSNResetRequestParameter>());

  EXPECT_EQ(req.request_sequence_number(), kMyInitialReqSn);
  EXPECT_EQ(req.sender_last_assigned_tsn(),
            TSN(*retransmission_queue_->next_tsn() - 1));
  EXPECT_THAT(req.stream_ids(), UnorderedElementsAre(StreamID(42)));
}

TEST_F(StreamResetHandlerTest, ResetMultipleStreamsInOneRequest) {
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(40)));
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(41)));
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(42))).Times(2);
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(43)));
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(44)));
  handler_->ResetStreams(std::vector<StreamID>({StreamID(42)}));
  handler_->ResetStreams(
      std::vector<StreamID>({StreamID(43), StreamID(44), StreamID(41)}));
  handler_->ResetStreams(std::vector<StreamID>({StreamID(42), StreamID(40)}));

  EXPECT_CALL(producer_, HasStreamsReadyToBeReset()).WillOnce(Return(true));
  EXPECT_CALL(producer_, GetStreamsReadyToBeReset())
      .WillOnce(Return(
          std::vector<StreamID>({StreamID(40), StreamID(41), StreamID(42),
                                 StreamID(43), StreamID(44)})));
  absl::optional<ReConfigChunk> reconfig = handler_->MakeStreamResetRequest();
  ASSERT_TRUE(reconfig.has_value());
  ASSERT_HAS_VALUE_AND_ASSIGN(
      OutgoingSSNResetRequestParameter req,
      reconfig->parameters().get<OutgoingSSNResetRequestParameter>());

  EXPECT_EQ(req.request_sequence_number(), kMyInitialReqSn);
  EXPECT_EQ(req.sender_last_assigned_tsn(),
            TSN(*retransmission_queue_->next_tsn() - 1));
  EXPECT_THAT(req.stream_ids(),
              UnorderedElementsAre(StreamID(40), StreamID(41), StreamID(42),
                                   StreamID(43), StreamID(44)));
}

TEST_F(StreamResetHandlerTest, SendOutgoingRequestDeferred) {
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(42)));
  handler_->ResetStreams(std::vector<StreamID>({StreamID(42)}));

  EXPECT_CALL(producer_, HasStreamsReadyToBeReset())
      .WillOnce(Return(false))
      .WillOnce(Return(false))
      .WillOnce(Return(true));

  EXPECT_FALSE(handler_->MakeStreamResetRequest().has_value());
  EXPECT_FALSE(handler_->MakeStreamResetRequest().has_value());
  EXPECT_TRUE(handler_->MakeStreamResetRequest().has_value());
}

TEST_F(StreamResetHandlerTest, SendOutgoingResettingOnPositiveResponse) {
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(42)));
  handler_->ResetStreams(std::vector<StreamID>({StreamID(42)}));

  EXPECT_CALL(producer_, HasStreamsReadyToBeReset()).WillOnce(Return(true));
  EXPECT_CALL(producer_, GetStreamsReadyToBeReset())
      .WillOnce(Return(std::vector<StreamID>({StreamID(42)})));

  absl::optional<ReConfigChunk> reconfig = handler_->MakeStreamResetRequest();
  ASSERT_TRUE(reconfig.has_value());
  ASSERT_HAS_VALUE_AND_ASSIGN(
      OutgoingSSNResetRequestParameter req,
      reconfig->parameters().get<OutgoingSSNResetRequestParameter>());

  Parameters::Builder builder;
  builder.Add(ReconfigurationResponseParameter(
      req.request_sequence_number(), ResponseResult::kSuccessPerformed));
  ReConfigChunk response_reconfig(builder.Build());

  EXPECT_CALL(producer_, CommitResetStreams);
  EXPECT_CALL(producer_, RollbackResetStreams).Times(0);

  // Processing a response shouldn't result in sending anything.
  EXPECT_CALL(callbacks_, OnError).Times(0);
  EXPECT_CALL(callbacks_, SendPacketWithStatus).Times(0);
  handler_->HandleReConfig(std::move(response_reconfig));
}

TEST_F(StreamResetHandlerTest, SendOutgoingResetRollbackOnError) {
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(42)));
  handler_->ResetStreams(std::vector<StreamID>({StreamID(42)}));

  EXPECT_CALL(producer_, HasStreamsReadyToBeReset()).WillOnce(Return(true));
  EXPECT_CALL(producer_, GetStreamsReadyToBeReset())
      .WillOnce(Return(std::vector<StreamID>({StreamID(42)})));

  absl::optional<ReConfigChunk> reconfig = handler_->MakeStreamResetRequest();
  ASSERT_TRUE(reconfig.has_value());
  ASSERT_HAS_VALUE_AND_ASSIGN(
      OutgoingSSNResetRequestParameter req,
      reconfig->parameters().get<OutgoingSSNResetRequestParameter>());

  Parameters::Builder builder;
  builder.Add(ReconfigurationResponseParameter(
      req.request_sequence_number(), ResponseResult::kErrorBadSequenceNumber));
  ReConfigChunk response_reconfig(builder.Build());

  EXPECT_CALL(producer_, CommitResetStreams).Times(0);
  EXPECT_CALL(producer_, RollbackResetStreams);

  // Only requests should result in sending responses.
  EXPECT_CALL(callbacks_, OnError).Times(0);
  EXPECT_CALL(callbacks_, SendPacketWithStatus).Times(0);
  handler_->HandleReConfig(std::move(response_reconfig));
}

TEST_F(StreamResetHandlerTest, SendOutgoingResetRetransmitOnInProgress) {
  static constexpr StreamID kStreamToReset = StreamID(42);

  EXPECT_CALL(producer_, PrepareResetStream(kStreamToReset));
  handler_->ResetStreams(std::vector<StreamID>({kStreamToReset}));

  EXPECT_CALL(producer_, HasStreamsReadyToBeReset()).WillOnce(Return(true));
  EXPECT_CALL(producer_, GetStreamsReadyToBeReset())
      .WillOnce(Return(std::vector<StreamID>({kStreamToReset})));

  absl::optional<ReConfigChunk> reconfig1 = handler_->MakeStreamResetRequest();
  ASSERT_TRUE(reconfig1.has_value());
  ASSERT_HAS_VALUE_AND_ASSIGN(
      OutgoingSSNResetRequestParameter req1,
      reconfig1->parameters().get<OutgoingSSNResetRequestParameter>());

  // Simulate that the peer responded "In Progress".
  Parameters::Builder builder;
  builder.Add(ReconfigurationResponseParameter(req1.request_sequence_number(),
                                               ResponseResult::kInProgress));
  ReConfigChunk response_reconfig(builder.Build());

  EXPECT_CALL(producer_, CommitResetStreams()).Times(0);
  EXPECT_CALL(producer_, RollbackResetStreams()).Times(0);

  // Processing a response shouldn't result in sending anything.
  EXPECT_CALL(callbacks_, OnError).Times(0);
  EXPECT_CALL(callbacks_, SendPacketWithStatus).Times(0);
  handler_->HandleReConfig(std::move(response_reconfig));

  // Let some time pass, so that the reconfig timer expires, and retries the
  // same request.
  EXPECT_CALL(callbacks_, SendPacketWithStatus).Times(1);
  AdvanceTime(kRto);

  std::vector<uint8_t> payload = callbacks_.ConsumeSentPacket();
  ASSERT_FALSE(payload.empty());

  ASSERT_HAS_VALUE_AND_ASSIGN(SctpPacket packet,
                              SctpPacket::Parse(payload, DcSctpOptions()));
  ASSERT_THAT(packet.descriptors(), SizeIs(1));
  ASSERT_HAS_VALUE_AND_ASSIGN(
      ReConfigChunk reconfig2,
      ReConfigChunk::Parse(packet.descriptors()[0].data));

  ASSERT_HAS_VALUE_AND_ASSIGN(
      OutgoingSSNResetRequestParameter req2,
      reconfig2.parameters().get<OutgoingSSNResetRequestParameter>());

  EXPECT_EQ(req2.request_sequence_number(),
            AddTo(req1.request_sequence_number(), 1));
  EXPECT_THAT(req2.stream_ids(), UnorderedElementsAre(kStreamToReset));
}

TEST_F(StreamResetHandlerTest, ResetWhileRequestIsSentWillQueue) {
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(42)));
  handler_->ResetStreams(std::vector<StreamID>({StreamID(42)}));

  EXPECT_CALL(producer_, HasStreamsReadyToBeReset()).WillOnce(Return(true));
  EXPECT_CALL(producer_, GetStreamsReadyToBeReset())
      .WillOnce(Return(std::vector<StreamID>({StreamID(42)})));

  absl::optional<ReConfigChunk> reconfig1 = handler_->MakeStreamResetRequest();
  ASSERT_TRUE(reconfig1.has_value());
  ASSERT_HAS_VALUE_AND_ASSIGN(
      OutgoingSSNResetRequestParameter req1,
      reconfig1->parameters().get<OutgoingSSNResetRequestParameter>());
  EXPECT_EQ(req1.request_sequence_number(), kMyInitialReqSn);
  EXPECT_EQ(req1.sender_last_assigned_tsn(),
            AddTo(retransmission_queue_->next_tsn(), -1));
  EXPECT_THAT(req1.stream_ids(), UnorderedElementsAre(StreamID(42)));

  // Streams reset while the request is in-flight will be queued.
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(41)));
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(43)));
  StreamID stream_ids[] = {StreamID(41), StreamID(43)};
  handler_->ResetStreams(stream_ids);
  EXPECT_EQ(handler_->MakeStreamResetRequest(), absl::nullopt);

  Parameters::Builder builder;
  builder.Add(ReconfigurationResponseParameter(
      req1.request_sequence_number(), ResponseResult::kSuccessPerformed));
  ReConfigChunk response_reconfig(builder.Build());

  EXPECT_CALL(producer_, CommitResetStreams()).Times(1);
  EXPECT_CALL(producer_, RollbackResetStreams()).Times(0);

  // Processing a response shouldn't result in sending anything.
  EXPECT_CALL(callbacks_, OnError).Times(0);
  EXPECT_CALL(callbacks_, SendPacketWithStatus).Times(0);
  handler_->HandleReConfig(std::move(response_reconfig));

  // Response has been processed. A new request can be sent.
  EXPECT_CALL(producer_, HasStreamsReadyToBeReset()).WillOnce(Return(true));
  EXPECT_CALL(producer_, GetStreamsReadyToBeReset())
      .WillOnce(Return(std::vector<StreamID>({StreamID(41), StreamID(43)})));

  absl::optional<ReConfigChunk> reconfig2 = handler_->MakeStreamResetRequest();
  ASSERT_TRUE(reconfig2.has_value());
  ASSERT_HAS_VALUE_AND_ASSIGN(
      OutgoingSSNResetRequestParameter req2,
      reconfig2->parameters().get<OutgoingSSNResetRequestParameter>());
  EXPECT_EQ(req2.request_sequence_number(), AddTo(kMyInitialReqSn, 1));
  EXPECT_EQ(req2.sender_last_assigned_tsn(),
            TSN(*retransmission_queue_->next_tsn() - 1));
  EXPECT_THAT(req2.stream_ids(),
              UnorderedElementsAre(StreamID(41), StreamID(43)));
}

TEST_F(StreamResetHandlerTest, SendIncomingResetJustReturnsNothingPerformed) {
  Parameters::Builder builder;
  builder.Add(
      IncomingSSNResetRequestParameter(kPeerInitialReqSn, {StreamID(1)}));

  std::vector<ReconfigurationResponseParameter> responses =
      HandleAndCatchResponse(ReConfigChunk(builder.Build()));
  ASSERT_THAT(responses, SizeIs(1));
  EXPECT_THAT(responses[0].response_sequence_number(), kPeerInitialReqSn);
  EXPECT_THAT(responses[0].result(), ResponseResult::kSuccessNothingToDo);
}

TEST_F(StreamResetHandlerTest, SendSameRequestTwiceIsIdempotent) {
  // Simulate that receiving the same chunk twice (due to network issues,
  // or retransmissions, causing a RECONFIG to be re-received) is idempotent.
  for (int i = 0; i < 2; ++i) {
    Parameters::Builder builder;
    builder.Add(OutgoingSSNResetRequestParameter(
        kPeerInitialReqSn, ReconfigRequestSN(3), AddTo(kPeerInitialTsn, 1),
        {StreamID(1)}));

    std::vector<ReconfigurationResponseParameter> responses1 =
        HandleAndCatchResponse(ReConfigChunk(builder.Build()));
    EXPECT_THAT(responses1, SizeIs(1));
    EXPECT_EQ(responses1[0].result(), ResponseResult::kInProgress);
  }
}

TEST_F(StreamResetHandlerTest,
       HandoverIsAllowedOnlyWhenNoStreamIsBeingOrWillBeReset) {
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(42)));
  handler_->ResetStreams(std::vector<StreamID>({StreamID(42)}));
  EXPECT_CALL(producer_, HasStreamsReadyToBeReset()).WillOnce(Return(true));
  EXPECT_EQ(
      handler_->GetHandoverReadiness(),
      HandoverReadinessStatus(HandoverUnreadinessReason::kPendingStreamReset));

  EXPECT_CALL(producer_, HasStreamsReadyToBeReset())
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_CALL(producer_, GetStreamsReadyToBeReset())
      .WillOnce(Return(std::vector<StreamID>({StreamID(42)})));

  ASSERT_TRUE(handler_->MakeStreamResetRequest().has_value());
  EXPECT_EQ(handler_->GetHandoverReadiness(),
            HandoverReadinessStatus(
                HandoverUnreadinessReason::kPendingStreamResetRequest));

  // Reset more streams while the request is in-flight.
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(41)));
  EXPECT_CALL(producer_, PrepareResetStream(StreamID(43)));
  StreamID stream_ids[] = {StreamID(41), StreamID(43)};
  handler_->ResetStreams(stream_ids);

  EXPECT_CALL(producer_, HasStreamsReadyToBeReset()).WillOnce(Return(true));
  EXPECT_EQ(handler_->GetHandoverReadiness(),
            HandoverReadinessStatus()
                .Add(HandoverUnreadinessReason::kPendingStreamResetRequest)
                .Add(HandoverUnreadinessReason::kPendingStreamReset));

  // Processing a response to first request.
  EXPECT_CALL(producer_, CommitResetStreams()).Times(1);
  handler_->HandleReConfig(
      ReConfigChunk(Parameters::Builder()
                        .Add(ReconfigurationResponseParameter(
                            kMyInitialReqSn, ResponseResult::kSuccessPerformed))
                        .Build()));
  EXPECT_CALL(producer_, HasStreamsReadyToBeReset()).WillOnce(Return(true));
  EXPECT_EQ(
      handler_->GetHandoverReadiness(),
      HandoverReadinessStatus(HandoverUnreadinessReason::kPendingStreamReset));

  // Second request can be sent.
  EXPECT_CALL(producer_, HasStreamsReadyToBeReset())
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_CALL(producer_, GetStreamsReadyToBeReset())
      .WillOnce(Return(std::vector<StreamID>({StreamID(41), StreamID(43)})));

  ASSERT_TRUE(handler_->MakeStreamResetRequest().has_value());
  EXPECT_EQ(handler_->GetHandoverReadiness(),
            HandoverReadinessStatus(
                HandoverUnreadinessReason::kPendingStreamResetRequest));

  // Processing a response to second request.
  EXPECT_CALL(producer_, CommitResetStreams()).Times(1);
  handler_->HandleReConfig(ReConfigChunk(
      Parameters::Builder()
          .Add(ReconfigurationResponseParameter(
              AddTo(kMyInitialReqSn, 1), ResponseResult::kSuccessPerformed))
          .Build()));

  // Seconds response has been processed. No pending resets.
  EXPECT_CALL(producer_, HasStreamsReadyToBeReset()).WillOnce(Return(false));

  EXPECT_TRUE(handler_->GetHandoverReadiness().IsReady());
}

TEST_F(StreamResetHandlerTest, HandoverInInitialState) {
  PerformHandover();

  EXPECT_CALL(producer_, PrepareResetStream(StreamID(42)));
  handler_->ResetStreams(std::vector<StreamID>({StreamID(42)}));

  EXPECT_CALL(producer_, HasStreamsReadyToBeReset()).WillOnce(Return(true));
  EXPECT_CALL(producer_, GetStreamsReadyToBeReset())
      .WillOnce(Return(std::vector<StreamID>({StreamID(42)})));

  absl::optional<ReConfigChunk> reconfig = handler_->MakeStreamResetRequest();
  ASSERT_TRUE(reconfig.has_value());
  ASSERT_HAS_VALUE_AND_ASSIGN(
      OutgoingSSNResetRequestParameter req,
      reconfig->parameters().get<OutgoingSSNResetRequestParameter>());

  EXPECT_EQ(req.request_sequence_number(), kMyInitialReqSn);
  EXPECT_EQ(req.sender_last_assigned_tsn(),
            TSN(*retransmission_queue_->next_tsn() - 1));
  EXPECT_THAT(req.stream_ids(), UnorderedElementsAre(StreamID(42)));
}

TEST_F(StreamResetHandlerTest, HandoverAfterHavingResetOneStream) {
  // Reset one stream
  {
    EXPECT_CALL(producer_, PrepareResetStream(StreamID(42)));
    handler_->ResetStreams(std::vector<StreamID>({StreamID(42)}));

    EXPECT_CALL(producer_, HasStreamsReadyToBeReset())
        .WillOnce(Return(true))
        .WillOnce(Return(false));
    EXPECT_CALL(producer_, GetStreamsReadyToBeReset())
        .WillOnce(Return(std::vector<StreamID>({StreamID(42)})));

    ASSERT_HAS_VALUE_AND_ASSIGN(ReConfigChunk reconfig,
                                handler_->MakeStreamResetRequest());
    ASSERT_HAS_VALUE_AND_ASSIGN(
        OutgoingSSNResetRequestParameter req,
        reconfig.parameters().get<OutgoingSSNResetRequestParameter>());
    EXPECT_EQ(req.request_sequence_number(), kMyInitialReqSn);
    EXPECT_EQ(req.sender_last_assigned_tsn(),
              TSN(*retransmission_queue_->next_tsn() - 1));
    EXPECT_THAT(req.stream_ids(), UnorderedElementsAre(StreamID(42)));

    EXPECT_CALL(producer_, CommitResetStreams()).Times(1);
    handler_->HandleReConfig(
        ReConfigChunk(Parameters::Builder()
                          .Add(ReconfigurationResponseParameter(
                              req.request_sequence_number(),
                              ResponseResult::kSuccessPerformed))
                          .Build()));
  }

  PerformHandover();

  // Reset another stream after handover
  {
    EXPECT_CALL(producer_, PrepareResetStream(StreamID(43)));
    handler_->ResetStreams(std::vector<StreamID>({StreamID(43)}));

    EXPECT_CALL(producer_, HasStreamsReadyToBeReset()).WillOnce(Return(true));
    EXPECT_CALL(producer_, GetStreamsReadyToBeReset())
        .WillOnce(Return(std::vector<StreamID>({StreamID(43)})));

    ASSERT_HAS_VALUE_AND_ASSIGN(ReConfigChunk reconfig,
                                handler_->MakeStreamResetRequest());
    ASSERT_HAS_VALUE_AND_ASSIGN(
        OutgoingSSNResetRequestParameter req,
        reconfig.parameters().get<OutgoingSSNResetRequestParameter>());

    EXPECT_EQ(req.request_sequence_number(),
              ReconfigRequestSN(kMyInitialReqSn.value() + 1));
    EXPECT_EQ(req.sender_last_assigned_tsn(),
              TSN(*retransmission_queue_->next_tsn() - 1));
    EXPECT_THAT(req.stream_ids(), UnorderedElementsAre(StreamID(43)));
  }
}

TEST_F(StreamResetHandlerTest, PerformCloseAfterOneFirstFailing) {
  // Inject a stream reset on the first expected TSN (which hasn't been seen).
  Parameters::Builder builder;
  builder.Add(OutgoingSSNResetRequestParameter(
      kPeerInitialReqSn, ReconfigRequestSN(3), kPeerInitialTsn, {StreamID(1)}));

  // The socket is expected to say "in progress" as that TSN hasn't been seen.
  std::vector<ReconfigurationResponseParameter> responses =
      HandleAndCatchResponse(ReConfigChunk(builder.Build()));
  EXPECT_THAT(responses, SizeIs(1));
  EXPECT_EQ(responses[0].result(), ResponseResult::kInProgress);

  // Let the socket receive the TSN.
  DataGeneratorOptions opts;
  opts.mid = MID(0);
  reasm_->Add(kPeerInitialTsn, gen_.Ordered({1, 2, 3, 4}, "BE", opts));
  data_tracker_->Observe(kPeerInitialTsn);

  // And emulate that time has passed, and the peer retries the stream reset,
  // but now with an incremented request sequence number.
  Parameters::Builder builder2;
  builder2.Add(OutgoingSSNResetRequestParameter(
      ReconfigRequestSN(*kPeerInitialReqSn + 1), ReconfigRequestSN(3),
      kPeerInitialTsn, {StreamID(1)}));

  // This is supposed to be handled well.
  std::vector<ReconfigurationResponseParameter> responses2 =
      HandleAndCatchResponse(ReConfigChunk(builder2.Build()));
  EXPECT_THAT(responses2, SizeIs(1));
  EXPECT_EQ(responses2[0].result(), ResponseResult::kSuccessPerformed);
}
}  // namespace
}  // namespace dcsctp
