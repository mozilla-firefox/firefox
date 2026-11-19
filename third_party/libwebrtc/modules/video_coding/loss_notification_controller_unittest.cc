/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/loss_notification_controller.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "modules/include/module_common_types.h"
#include "rtc_base/checks.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

// The information about an RTP packet that is relevant in these tests.
struct Packet {
  uint16_t seq_num;
  bool first_in_frame;
  bool is_keyframe;
  int64_t frame_id;
  std::vector<int64_t> frame_dependencies;
};

Packet CreatePacket(
    bool first_in_frame,
    bool /* last_in_frame */,
    uint16_t seq_num,
    uint16_t frame_id,
    bool is_key_frame,
    std::vector<int64_t> ref_frame_ids = std::vector<int64_t>()) {
  Packet packet;
  packet.seq_num = seq_num;
  packet.first_in_frame = first_in_frame;
  if (first_in_frame) {
    packet.is_keyframe = is_key_frame;
    packet.frame_id = frame_id;
    RTC_DCHECK(!is_key_frame || ref_frame_ids.empty());
    packet.frame_dependencies = std::move(ref_frame_ids);
  }
  return packet;
}

class PacketStreamCreator final {
 public:
  PacketStreamCreator() : seq_num_(0), frame_id_(0), next_is_key_frame_(true) {}

  Packet NextPacket() {
    std::vector<int64_t> ref_frame_ids;
    if (!next_is_key_frame_) {
      ref_frame_ids.push_back(frame_id_ - 1);
    }

    Packet packet = CreatePacket(true, true, seq_num_++, frame_id_++,
                                 next_is_key_frame_, ref_frame_ids);

    next_is_key_frame_ = false;

    return packet;
  }

 private:
  uint16_t seq_num_;
  int64_t frame_id_;
  bool next_is_key_frame_;
};
}  // namespace

// Most of the logic for the tests is here. Subclasses allow parameterizing
// the test, or adding some more specific logic.
class LossNotificationControllerBaseTest : public ::testing::Test,
                                           public KeyFrameRequestSender,
                                           public LossNotificationSender {
 protected:
  LossNotificationControllerBaseTest()
      : uut_(this, this), key_frame_requested_(false) {}

  ~LossNotificationControllerBaseTest() override {
    EXPECT_FALSE(LastKeyFrameRequest());
    EXPECT_FALSE(LastLossNotification());
  }

  // KeyFrameRequestSender implementation.
  void RequestKeyFrame() override {
    EXPECT_FALSE(LastKeyFrameRequest());
    EXPECT_FALSE(LastLossNotification());
    key_frame_requested_ = true;
  }

  // LossNotificationSender implementation.
  void SendLossNotification(uint16_t last_decoded_seq_num,
                            uint16_t last_received_seq_num,
                            bool decodability_flag,
                            bool buffering_allowed) override {
    EXPECT_TRUE(buffering_allowed);  // (Flag useful elsewhere.)
    EXPECT_FALSE(LastKeyFrameRequest());
    EXPECT_FALSE(LastLossNotification());
    last_loss_notification_.emplace(last_decoded_seq_num, last_received_seq_num,
                                    decodability_flag);
  }

  void OnReceivedPacket(const Packet& packet) {
    EXPECT_FALSE(LastKeyFrameRequest());
    EXPECT_FALSE(LastLossNotification());

    if (packet.first_in_frame) {
      previous_first_packet_in_frame_ = packet;
      LossNotificationController::FrameDetails frame;
      frame.is_keyframe = packet.is_keyframe;
      frame.frame_id = packet.frame_id;
      frame.frame_dependencies = packet.frame_dependencies;
      uut_.OnReceivedPacket(packet.seq_num, &frame);
    } else {
      uut_.OnReceivedPacket(packet.seq_num, nullptr);
    }
  }

  void OnAssembledFrame(uint16_t first_seq_num,
                        int64_t frame_id,
                        bool discardable) {
    EXPECT_FALSE(LastKeyFrameRequest());
    EXPECT_FALSE(LastLossNotification());

    ASSERT_TRUE(previous_first_packet_in_frame_);
    uut_.OnAssembledFrame(first_seq_num, frame_id, discardable,
                          previous_first_packet_in_frame_->frame_dependencies);
  }

  void ExpectKeyFrameRequest() {
    EXPECT_EQ(LastLossNotification(), std::nullopt);
    EXPECT_TRUE(LastKeyFrameRequest());
  }

  void ExpectLossNotification(uint16_t last_decoded_seq_num,
                              uint16_t last_received_seq_num,
                              bool decodability_flag) {
    EXPECT_FALSE(LastKeyFrameRequest());
    const auto last_ln = LastLossNotification();
    ASSERT_TRUE(last_ln);
    const LossNotification expected_ln(
        last_decoded_seq_num, last_received_seq_num, decodability_flag);
    EXPECT_EQ(expected_ln, *last_ln)
        << "Expected loss notification (" << expected_ln.ToString()
        << ") != received loss notification (" << last_ln->ToString() + ")";
  }

  struct LossNotification {
    LossNotification(uint16_t last_decoded_seq_num,
                     uint16_t last_received_seq_num,
                     bool decodability_flag)
        : last_decoded_seq_num(last_decoded_seq_num),
          last_received_seq_num(last_received_seq_num),
          decodability_flag(decodability_flag) {}

    LossNotification& operator=(const LossNotification& other) = default;

    bool operator==(const LossNotification& other) const {
      return last_decoded_seq_num == other.last_decoded_seq_num &&
             last_received_seq_num == other.last_received_seq_num &&
             decodability_flag == other.decodability_flag;
    }

    std::string ToString() const {
      return std::to_string(last_decoded_seq_num) + ", " +
             std::to_string(last_received_seq_num) + ", " +
             std::to_string(decodability_flag);
    }

    uint16_t last_decoded_seq_num;
    uint16_t last_received_seq_num;
    bool decodability_flag;
  };

  bool LastKeyFrameRequest() {
    const bool result = key_frame_requested_;
    key_frame_requested_ = false;
    return result;
  }

  std::optional<LossNotification> LastLossNotification() {
    const std::optional<LossNotification> result = last_loss_notification_;
    last_loss_notification_ = std::nullopt;
    return result;
  }

  LossNotificationController uut_;  // Unit under test.

  bool key_frame_requested_;

  std::optional<LossNotification> last_loss_notification_;

  // First packet of last frame. (Note that if a test skips the first packet
  // of a subsequent frame, OnAssembledFrame is not called, and so this is
  // note read. Therefore, it's not a problem if it is not cleared when
  // the frame changes.)
  std::optional<Packet> previous_first_packet_in_frame_;
};

class LossNotificationControllerTest
    : public LossNotificationControllerBaseTest,
      public ::testing::WithParamInterface<std::tuple<bool, bool, bool>> {
 protected:
  // Arbitrary parameterized values, to be used by the tests whenever they
  // wish to either check some combinations, or wish to demonstrate that
  // a particular arbitrary value is unimportant.
  template <size_t N>
  bool Bool() const {
    return std::get<N>(GetParam());
  }
};

INSTANTIATE_TEST_SUITE_P(_,
                         LossNotificationControllerTest,
                         ::testing::Combine(::testing::Bool(),
                                            ::testing::Bool(),
                                            ::testing::Bool()));

// If the first frame, which is a key frame, is lost, then a new key frame
// is requested.
TEST_P(LossNotificationControllerTest,
       PacketLossBeforeFirstFrameAssembledTriggersKeyFrameRequest) {
  OnReceivedPacket(CreatePacket(true, false, 100, 0, true));
  OnReceivedPacket(CreatePacket(Bool<0>(), Bool<1>(), 103, 1, false, {0}));
  ExpectKeyFrameRequest();
}

// If packet loss occurs (but not of the first packet), then a loss notification
// is issued.
TEST_P(LossNotificationControllerTest,
       PacketLossAfterFirstFrameAssembledTriggersLossNotification) {
  OnReceivedPacket(CreatePacket(true, true, 100, 0, true));
  OnAssembledFrame(100, 0, false);
  const bool first = Bool<0>();
  const bool last = Bool<1>();
  OnReceivedPacket(CreatePacket(first, last, 103, 1, false, {0}));
  const bool expected_decodability_flag = first;
  ExpectLossNotification(100, 103, expected_decodability_flag);
}

// No key frame or loss notifications issued due to an innocuous wrap-around
// of the sequence number.
TEST_P(LossNotificationControllerTest, SeqNumWrapAround) {
  uint16_t seq_num = std::numeric_limits<uint16_t>::max();
  OnReceivedPacket(CreatePacket(true, true, seq_num, 0, true));
  OnAssembledFrame(seq_num, 0, false);
  const bool first = Bool<0>();
  const bool last = Bool<1>();
  OnReceivedPacket(CreatePacket(first, last, ++seq_num, 1, false, {0}));
}

TEST_F(LossNotificationControllerTest,
       KeyFrameAfterPacketLossProducesNoLossNotifications) {
  OnReceivedPacket(CreatePacket(true, true, 100, 1, true));
  OnAssembledFrame(100, 1, false);
  OnReceivedPacket(CreatePacket(true, true, 108, 8, true));
}

TEST_P(LossNotificationControllerTest, LostReferenceProducesLossNotification) {
  OnReceivedPacket(CreatePacket(true, true, 100, 0, true));
  OnAssembledFrame(100, 0, false);
  uint16_t last_decodable_non_discardable_seq_num = 100;

  // RTP gap produces loss notification - not the focus of this test.
  const bool first = Bool<0>();
  const bool last = Bool<1>();
  const bool discardable = Bool<2>();
  const bool decodable = first;  // Depends on assemblability.
  OnReceivedPacket(CreatePacket(first, last, 107, 3, false, {0}));
  ExpectLossNotification(100, 107, decodable);
  OnAssembledFrame(107, 3, discardable);
  if (!discardable) {
    last_decodable_non_discardable_seq_num = 107;
  }

  // Test focus - a loss notification is produced because of the missing
  // dependency (frame ID 2), despite the RTP sequence number being the
  // next expected one.
  OnReceivedPacket(CreatePacket(true, true, 108, 4, false, {2, 0}));
  ExpectLossNotification(last_decodable_non_discardable_seq_num, 108, false);
}

// The difference between this test and the previous one, is that in this test,
// although the reference frame was received, it was not decodable.
TEST_P(LossNotificationControllerTest,
       UndecodableReferenceProducesLossNotification) {
  OnReceivedPacket(CreatePacket(true, true, 100, 0, true));
  OnAssembledFrame(100, 0, false);
  uint16_t last_decodable_non_discardable_seq_num = 100;

  // RTP gap produces loss notification - not the focus of this test.
  // Also, not decodable; this is important for later in the test.
  OnReceivedPacket(CreatePacket(true, true, 107, 3, false, {2}));
  ExpectLossNotification(100, 107, false);
  const bool discardable = Bool<0>();
  OnAssembledFrame(107, 3, discardable);

  // Test focus - a loss notification is produced because of the undecodable
  // dependency (frame ID 3, which depended on the missing frame ID 2).
  OnReceivedPacket(CreatePacket(true, true, 108, 4, false, {3, 0}));
  ExpectLossNotification(last_decodable_non_discardable_seq_num, 108, false);
}

TEST_P(LossNotificationControllerTest, RobustnessAgainstHighInitialRefFrameId) {
  constexpr uint16_t max_uint16_t = std::numeric_limits<uint16_t>::max();
  OnReceivedPacket(CreatePacket(true, true, 100, 0, true));
  OnAssembledFrame(100, 0, false);
  OnReceivedPacket(CreatePacket(true, true, 101, 1, false, {max_uint16_t}));
  ExpectLossNotification(100, 101, false);
  OnAssembledFrame(101, max_uint16_t, Bool<0>());
}

TEST_P(LossNotificationControllerTest, RepeatedPacketsAreIgnored) {
  PacketStreamCreator packet_stream;

  const auto key_frame_packet = packet_stream.NextPacket();
  OnReceivedPacket(key_frame_packet);
  OnAssembledFrame(key_frame_packet.seq_num, key_frame_packet.frame_id, false);

  const bool gap = Bool<0>();

  if (gap) {
    // Lose one packet.
    packet_stream.NextPacket();
  }

  auto repeated_packet = packet_stream.NextPacket();
  OnReceivedPacket(repeated_packet);
  if (gap) {
    // Loss notification issued because of the gap. This is not the focus of
    // the test.
    ExpectLossNotification(key_frame_packet.seq_num, repeated_packet.seq_num,
                           false);
  }
  OnReceivedPacket(repeated_packet);
}

TEST_F(LossNotificationControllerTest,
       RecognizesDependencyAcrossIntraFrameThatIsNotAKeyframe) {
  int last_seq_num = 1;
  auto receive = [&](bool is_key_frame, int64_t frame_id,
                     std::vector<int64_t> ref_frame_ids) {
    ++last_seq_num;
    OnReceivedPacket(CreatePacket(
        /*first_in_frame=*/true, /*last_in_frame=*/true, last_seq_num, frame_id,
        is_key_frame, std::move(ref_frame_ids)));
    OnAssembledFrame(last_seq_num, frame_id, /*discardable=*/false);
  };
  //  11 -- 13
  //   |     |
  //  10    12
  receive(/*is_key_frame=*/true, /*frame_id=*/10, /*ref_frame_ids=*/{});
  receive(/*is_key_frame=*/false, /*frame_id=*/11, /*ref_frame_ids=*/{10});
  receive(/*is_key_frame=*/false, /*frame_id=*/12, /*ref_frame_ids=*/{});
  receive(/*is_key_frame=*/false, /*frame_id=*/13, /*ref_frame_ids=*/{11, 12});
  EXPECT_FALSE(LastLossNotification());
}

TEST_F(LossNotificationControllerTest, RepeatedAssembledFrameIsIgnored) {
  OnReceivedPacket(CreatePacket(true, true, 100, 0, true));
  OnAssembledFrame(100, 0, false);

  // Duplicate reassembly of the same packet before decode cleanup.
  OnAssembledFrame(100, 0, false);

  // Buggy/malicious remote reusing frame ID 0 on a new RTP sequence number.
  OnReceivedPacket(CreatePacket(true, true, 101, 0, true));
  OnAssembledFrame(101, 0, false);

  // Subsequent loss notification still reports the original frame's sequence
  // number (100) and treats frame ID 0 as decodable.
  OnReceivedPacket(CreatePacket(true, true, 105, 1, false, {0}));
  ExpectLossNotification(100, 105, true);
}

class LossNotificationControllerTestDecodabilityFlag
    : public LossNotificationControllerBaseTest {
 protected:
  LossNotificationControllerTestDecodabilityFlag()
      : key_frame_seq_num_(100),
        key_frame_frame_id_(0),
        never_received_frame_id_(key_frame_frame_id_ + 1),
        seq_num_(0),
        frame_id_(0) {}

  void ReceiveKeyFrame() {
    RTC_DCHECK_NE(key_frame_frame_id_, never_received_frame_id_);
    OnReceivedPacket(CreatePacket(true, true, key_frame_seq_num_,
                                  key_frame_frame_id_, true));
    OnAssembledFrame(key_frame_seq_num_, key_frame_frame_id_, false);
    seq_num_ = key_frame_seq_num_;
    frame_id_ = key_frame_frame_id_;
  }

  void ReceivePacket(bool first_packet_in_frame,
                     bool last_packet_in_frame,
                     const std::vector<int64_t>& ref_frame_ids) {
    if (first_packet_in_frame) {
      frame_id_ += 1;
    }
    RTC_DCHECK_NE(frame_id_, never_received_frame_id_);
    constexpr bool is_key_frame = false;
    OnReceivedPacket(CreatePacket(first_packet_in_frame, last_packet_in_frame,
                                  ++seq_num_, frame_id_, is_key_frame,
                                  ref_frame_ids));
  }

  void CreateGap() {
    seq_num_ += 50;
    frame_id_ += 10;
  }

  const uint16_t key_frame_seq_num_;
  const uint16_t key_frame_frame_id_;

  // The tests intentionally never receive this, and can therefore always
  // use this as an unsatisfied dependency.
  const int64_t never_received_frame_id_ = 123;

  uint16_t seq_num_;
  int64_t frame_id_;
};

TEST_F(LossNotificationControllerTestDecodabilityFlag,
       SinglePacketFrameWithDecodableDependencies) {
  ReceiveKeyFrame();
  CreateGap();

  const std::vector<int64_t> ref_frame_ids = {key_frame_frame_id_};
  ReceivePacket(true, true, ref_frame_ids);

  const bool expected_decodability_flag = true;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag);
}

TEST_F(LossNotificationControllerTestDecodabilityFlag,
       SinglePacketFrameWithUndecodableDependencies) {
  ReceiveKeyFrame();
  CreateGap();

  const std::vector<int64_t> ref_frame_ids = {never_received_frame_id_};
  ReceivePacket(true, true, ref_frame_ids);

  const bool expected_decodability_flag = false;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag);
}

TEST_F(LossNotificationControllerTestDecodabilityFlag,
       FirstPacketOfMultiPacketFrameWithDecodableDependencies) {
  ReceiveKeyFrame();
  CreateGap();

  const std::vector<int64_t> ref_frame_ids = {key_frame_frame_id_};
  ReceivePacket(true, false, ref_frame_ids);

  const bool expected_decodability_flag = true;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag);
}

TEST_F(LossNotificationControllerTestDecodabilityFlag,
       FirstPacketOfMultiPacketFrameWithUndecodableDependencies) {
  ReceiveKeyFrame();
  CreateGap();

  const std::vector<int64_t> ref_frame_ids = {never_received_frame_id_};
  ReceivePacket(true, false, ref_frame_ids);

  const bool expected_decodability_flag = false;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag);
}

TEST_F(LossNotificationControllerTestDecodabilityFlag,
       MiddlePacketOfMultiPacketFrameWithDecodableDependenciesIfFirstMissed) {
  ReceiveKeyFrame();
  CreateGap();

  const std::vector<int64_t> ref_frame_ids = {key_frame_frame_id_};
  ReceivePacket(false, false, ref_frame_ids);

  const bool expected_decodability_flag = false;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag);
}

TEST_F(LossNotificationControllerTestDecodabilityFlag,
       MiddlePacketOfMultiPacketFrameWithUndecodableDependenciesIfFirstMissed) {
  ReceiveKeyFrame();
  CreateGap();

  const std::vector<int64_t> ref_frame_ids = {never_received_frame_id_};
  ReceivePacket(false, false, ref_frame_ids);

  const bool expected_decodability_flag = false;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag);
}

TEST_F(LossNotificationControllerTestDecodabilityFlag,
       MiddlePacketOfMultiPacketFrameWithDecodableDependenciesIfFirstReceived) {
  ReceiveKeyFrame();
  CreateGap();

  // First packet in multi-packet frame. A loss notification is produced
  // because of the gap in RTP sequence numbers.
  const std::vector<int64_t> ref_frame_ids = {key_frame_frame_id_};
  ReceivePacket(true, false, ref_frame_ids);
  const bool expected_decodability_flag_first = true;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag_first);

  // Middle packet in multi-packet frame. No additional gap and the frame is
  // still potentially decodable, so no additional loss indication.
  ReceivePacket(false, false, ref_frame_ids);
  EXPECT_FALSE(LastKeyFrameRequest());
  EXPECT_FALSE(LastLossNotification());
}

TEST_F(
    LossNotificationControllerTestDecodabilityFlag,
    MiddlePacketOfMultiPacketFrameWithUndecodableDependenciesIfFirstReceived) {
  ReceiveKeyFrame();
  CreateGap();

  // First packet in multi-packet frame. A loss notification is produced
  // because of the gap in RTP sequence numbers. The frame is also recognized
  // as having non-decodable dependencies.
  const std::vector<int64_t> ref_frame_ids = {never_received_frame_id_};
  ReceivePacket(true, false, ref_frame_ids);
  const bool expected_decodability_flag_first = false;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag_first);

  // Middle packet in multi-packet frame. No additional gap, but the frame is
  // known to be non-decodable, so we keep issuing loss indications.
  ReceivePacket(false, false, ref_frame_ids);
  const bool expected_decodability_flag_middle = false;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag_middle);
}

TEST_F(LossNotificationControllerTestDecodabilityFlag,
       LastPacketOfMultiPacketFrameWithDecodableDependenciesIfAllPrevMissed) {
  ReceiveKeyFrame();
  CreateGap();

  const std::vector<int64_t> ref_frame_ids = {key_frame_frame_id_};
  ReceivePacket(false, true, ref_frame_ids);

  const bool expected_decodability_flag = false;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag);
}

TEST_F(LossNotificationControllerTestDecodabilityFlag,
       LastPacketOfMultiPacketFrameWithUndecodableDependenciesIfAllPrevMissed) {
  ReceiveKeyFrame();
  CreateGap();

  const std::vector<int64_t> ref_frame_ids = {never_received_frame_id_};
  ReceivePacket(false, true, ref_frame_ids);

  const bool expected_decodability_flag = false;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag);
}

TEST_F(LossNotificationControllerTestDecodabilityFlag,
       LastPacketOfMultiPacketFrameWithDecodableDependenciesIfAllPrevReceived) {
  ReceiveKeyFrame();
  CreateGap();

  // First packet in multi-packet frame. A loss notification is produced
  // because of the gap in RTP sequence numbers.
  const std::vector<int64_t> ref_frame_ids = {key_frame_frame_id_};
  ReceivePacket(true, false, ref_frame_ids);
  const bool expected_decodability_flag_first = true;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag_first);

  // Last packet in multi-packet frame. No additional gap and the frame is
  // still potentially decodable, so no additional loss indication.
  ReceivePacket(false, true, ref_frame_ids);
  EXPECT_FALSE(LastKeyFrameRequest());
  EXPECT_FALSE(LastLossNotification());
}

TEST_F(
    LossNotificationControllerTestDecodabilityFlag,
    LastPacketOfMultiPacketFrameWithUndecodableDependenciesIfAllPrevReceived) {
  ReceiveKeyFrame();
  CreateGap();

  // First packet in multi-packet frame. A loss notification is produced
  // because of the gap in RTP sequence numbers. The frame is also recognized
  // as having non-decodable dependencies.
  const std::vector<int64_t> ref_frame_ids = {never_received_frame_id_};
  ReceivePacket(true, false, ref_frame_ids);
  const bool expected_decodability_flag_first = false;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag_first);

  // Last packet in multi-packet frame. No additional gap, but the frame is
  // known to be non-decodable, so we keep issuing loss indications.
  ReceivePacket(false, true, ref_frame_ids);
  const bool expected_decodability_flag_last = false;
  ExpectLossNotification(key_frame_seq_num_, seq_num_,
                         expected_decodability_flag_last);
}

}  //  namespace webrtc
