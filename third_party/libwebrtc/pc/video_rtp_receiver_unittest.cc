/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/video_rtp_receiver.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/rtp_packet_info.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_receiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/timestamp.h"
#include "api/video/recordable_encoded_frame.h"
#include "api/video/test/mock_recordable_encoded_frame.h"
#include "api/video/video_sink_interface.h"
#include "media/base/fake_media_engine.h"
#include "media/base/media_channel.h"
#include "rtc_base/thread.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::InSequence;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::SaveArg;
using ::testing::StrictMock;

namespace webrtc {
namespace {

class VideoRtpReceiverTest : public testing::Test {
 protected:
  class MockVideoMediaSendChannel : public FakeVideoMediaSendChannel {
   public:
    MockVideoMediaSendChannel(const VideoOptions& options,
                              TaskQueueBase* network_thread = Thread::Current())
        : FakeVideoMediaSendChannel(options, network_thread) {}
    MOCK_METHOD(void,
                GenerateSendKeyFrame,
                (uint32_t, const std::vector<std::string>&),
                (override));
  };

  class MockVideoMediaReceiveChannel : public FakeVideoMediaReceiveChannel {
   public:
    MockVideoMediaReceiveChannel(
        const VideoOptions& options,
        TaskQueueBase* network_thread = Thread::Current())
        : FakeVideoMediaReceiveChannel(options, network_thread) {}
    MOCK_METHOD(void,
                SetRecordableEncodedFrameCallback,
                (uint32_t, std::function<void(const RecordableEncodedFrame&)>),
                (override));
    MOCK_METHOD(void,
                ClearRecordableEncodedFrameCallback,
                (uint32_t),
                (override));
    MOCK_METHOD(void, RequestRecvKeyFrame, (uint32_t), (override));
  };

  class MockVideoSink : public VideoSinkInterface<RecordableEncodedFrame> {
   public:
    MOCK_METHOD(void, OnFrame, (const RecordableEncodedFrame&), (override));
  };

  VideoRtpReceiverTest()
      : worker_thread_(Thread::Create()),
        channel_(VideoOptions()),
        receiver_(make_ref_counted<VideoRtpReceiver>(
            worker_thread_.get(),
            "receiver",
            std::vector<std::string>({"stream"}),
            /*enable_sframe_at_owner=*/nullptr)) {
    worker_thread_->Start();
    SetMediaChannel(&channel_);
  }

  ~VideoRtpReceiverTest() override {
    // Clear expectations that tests may have set up before calling
    // SetMediaChannel(nullptr).
    Mock::VerifyAndClearExpectations(&channel_);
    receiver_->Stop();
    SetMediaChannel(nullptr);
  }

  void SetMediaChannel(MediaReceiveChannelInterface* media_channel) {
    worker_thread_->BlockingCall(
        [&]() { receiver_->SetMediaChannel(media_channel); });
  }

  void RunOnWorkerThread(absl::AnyInvocable<void() &&> task) {
    worker_thread_->BlockingCall([&] { std::move(task)(); });
  }

  VideoTrackSourceInterface* Source() {
    return receiver_->streams()[0]->FindVideoTrack("receiver")->GetSource();
  }

  test::RunLoop loop_;
  std::unique_ptr<Thread> worker_thread_;
  NiceMock<MockVideoMediaReceiveChannel> channel_;
  scoped_refptr<VideoRtpReceiver> receiver_;
};

TEST_F(VideoRtpReceiverTest, SupportsEncodedOutput) {
  EXPECT_TRUE(Source()->SupportsEncodedOutput());
}

TEST_F(VideoRtpReceiverTest, GeneratesKeyFrame) {
  EXPECT_CALL(channel_, RequestRecvKeyFrame(0));
  Source()->GenerateKeyFrame();
}

TEST_F(VideoRtpReceiverTest,
       GenerateKeyFrameOnChannelSwitchUnlessGenerateKeyframeCalled) {
  // A channel switch without previous call to GenerateKeyFrame shouldn't
  // cause a call to happen on the new channel.
  MockVideoMediaReceiveChannel channel2{VideoOptions()};
  EXPECT_CALL(channel_, RequestRecvKeyFrame).Times(0);
  EXPECT_CALL(channel2, RequestRecvKeyFrame).Times(0);
  SetMediaChannel(&channel2);
  Mock::VerifyAndClearExpectations(&channel2);

  // Generate a key frame. When we switch channel next time, we will have to
  // re-generate it as we don't know if it was eventually received
  EXPECT_CALL(channel2, RequestRecvKeyFrame).Times(1);
  Source()->GenerateKeyFrame();
  MockVideoMediaReceiveChannel channel3{VideoOptions()};
  EXPECT_CALL(channel3, RequestRecvKeyFrame);
  SetMediaChannel(&channel3);

  // Switching to a new channel should now not cause calls to GenerateKeyFrame.
  StrictMock<MockVideoMediaReceiveChannel> channel4{VideoOptions()};
  SetMediaChannel(&channel4);

  // We must call SetMediaChannel(nullptr) here since the mock media channels
  // live on the stack and `receiver_` still has a pointer to those objects.
  SetMediaChannel(nullptr);
}

TEST_F(VideoRtpReceiverTest, EnablesEncodedOutput) {
  EXPECT_CALL(channel_, SetRecordableEncodedFrameCallback(/*ssrc=*/0, _));
  EXPECT_CALL(channel_, ClearRecordableEncodedFrameCallback).Times(0);
  MockVideoSink sink;
  Source()->AddEncodedSink(&sink);
}

TEST_F(VideoRtpReceiverTest, DisablesEncodedOutput) {
  EXPECT_CALL(channel_, ClearRecordableEncodedFrameCallback(/*ssrc=*/0));
  MockVideoSink sink;
  Source()->AddEncodedSink(&sink);
  Source()->RemoveEncodedSink(&sink);
}

TEST_F(VideoRtpReceiverTest, DisablesEnablesEncodedOutputOnChannelSwitch) {
  InSequence s;
  EXPECT_CALL(channel_, SetRecordableEncodedFrameCallback);
  EXPECT_CALL(channel_, ClearRecordableEncodedFrameCallback);
  MockVideoSink sink;
  Source()->AddEncodedSink(&sink);
  MockVideoMediaReceiveChannel channel2{VideoOptions()};
  EXPECT_CALL(channel2, SetRecordableEncodedFrameCallback);
  SetMediaChannel(&channel2);
  Mock::VerifyAndClearExpectations(&channel2);

  // When clearing encoded frame buffer function, we need channel switches
  // to NOT set the callback again.
  EXPECT_CALL(channel2, ClearRecordableEncodedFrameCallback);
  Source()->RemoveEncodedSink(&sink);
  StrictMock<MockVideoMediaReceiveChannel> channel3{VideoOptions()};
  SetMediaChannel(&channel3);

  // We must call SetMediaChannel(nullptr) here since the mock media channels
  // live on the stack and `receiver_` still has a pointer to those objects.
  SetMediaChannel(nullptr);
}

TEST_F(VideoRtpReceiverTest, BroadcastsEncodedFramesWhenEnabled) {
  std::function<void(const RecordableEncodedFrame&)> broadcast;
  EXPECT_CALL(channel_, SetRecordableEncodedFrameCallback(_, _))
      .WillRepeatedly(SaveArg<1>(&broadcast));
  MockVideoSink sink;
  Source()->AddEncodedSink(&sink);

  // Make sure SetEncodedFrameBufferFunction completes.
  Mock::VerifyAndClearExpectations(&channel_);

  // Pass two frames on different contexts.
  EXPECT_CALL(sink, OnFrame).Times(2);
  MockRecordableEncodedFrame frame;
  broadcast(frame);
  worker_thread_->BlockingCall([&] { broadcast(frame); });
}

TEST_F(VideoRtpReceiverTest, EnablesEncodedOutputOnChannelRestart) {
  InSequence s;
  MockVideoSink sink;
  Source()->AddEncodedSink(&sink);
  EXPECT_CALL(channel_, SetRecordableEncodedFrameCallback(4711, _));
  auto setup_media_channel = receiver_->GetSetupForMediaChannel(4711);
  worker_thread_->BlockingCall([&]() { std::move(setup_media_channel)(); });
  EXPECT_CALL(channel_, ClearRecordableEncodedFrameCallback(4711));
  EXPECT_CALL(channel_, SetRecordableEncodedFrameCallback(0, _));
  auto setup_task = receiver_->GetSetupForUnsignaledMediaChannel();
  worker_thread_->BlockingCall([&]() { std::move(setup_task)(); });
}

class FakeVideoRtpReceiverObserver : public RtpReceiverObserverInterface {
 public:
  void OnFirstPacketReceived(MediaType media_type) override {}
  void OnSourceChanged(bool ssrc_changed, bool csrc_changed) override {
    source_changed_count_++;
    last_ssrc_changed_ = ssrc_changed;
    last_csrc_changed_ = csrc_changed;
  }

  int source_changed_count() const { return source_changed_count_; }
  bool last_ssrc_changed() const { return last_ssrc_changed_; }
  bool last_csrc_changed() const { return last_csrc_changed_; }

 private:
  int source_changed_count_ = 0;
  bool last_ssrc_changed_ = false;
  bool last_csrc_changed_ = false;
};

TEST_F(VideoRtpReceiverTest, SourceChangeNotification) {
  constexpr uint32_t kVideoSsrc = 12345;
  FakeVideoRtpReceiverObserver observer;
  receiver_->SetObserver(&observer);

  // Deliver initial packet info.
  receiver_->OnFrameDelivered(
      RtpPacketInfos(
          {RtpPacketInfo(kVideoSsrc, {}, 100, Timestamp::Millis(100))}),
      Timestamp::Millis(100));

  EXPECT_EQ(observer.source_changed_count(), 1);
  EXPECT_TRUE(observer.last_ssrc_changed());
  EXPECT_FALSE(observer.last_csrc_changed());

  // Subsequent frame with same SSRC should not fire callback.
  receiver_->OnFrameDelivered(
      RtpPacketInfos(
          {RtpPacketInfo(kVideoSsrc, {}, 101, Timestamp::Millis(120))}),
      Timestamp::Millis(120));

  EXPECT_EQ(observer.source_changed_count(), 1);

  // Frame with CSRC change should fire callback.
  receiver_->OnFrameDelivered(
      RtpPacketInfos(
          {RtpPacketInfo(kVideoSsrc, {9999}, 102, Timestamp::Millis(140))}),
      Timestamp::Millis(140));

  EXPECT_EQ(observer.source_changed_count(), 2);
  EXPECT_FALSE(observer.last_ssrc_changed());
  EXPECT_TRUE(observer.last_csrc_changed());

  receiver_->SetObserver(nullptr);
}

TEST_F(VideoRtpReceiverTest, LateObserverRegistration) {
  constexpr uint32_t kVideoSsrc = 12345;
  FakeVideoRtpReceiverObserver observer;

  // Frame delivered before observer is registered.
  receiver_->OnFrameDelivered(
      RtpPacketInfos(
          {RtpPacketInfo(kVideoSsrc, {}, 100, Timestamp::Millis(100))}),
      Timestamp::Millis(100));

  EXPECT_EQ(observer.source_changed_count(), 0);

  // Setting observer should trigger callback for existing source.
  receiver_->SetObserver(&observer);

  EXPECT_EQ(observer.source_changed_count(), 1);
  EXPECT_TRUE(observer.last_ssrc_changed());
  EXPECT_FALSE(observer.last_csrc_changed());

  receiver_->SetObserver(nullptr);
}

TEST_F(VideoRtpReceiverTest, ObserverTeardownStopsNotifications) {
  constexpr uint32_t kVideoSsrc = 12345;
  FakeVideoRtpReceiverObserver observer;
  receiver_->SetObserver(&observer);

  receiver_->OnFrameDelivered(
      RtpPacketInfos(
          {RtpPacketInfo(kVideoSsrc, {}, 100, Timestamp::Millis(100))}),
      Timestamp::Millis(100));
  EXPECT_EQ(observer.source_changed_count(), 1);

  receiver_->SetObserver(nullptr);

  // New SSRC after observer is cleared should not reach observer.
  receiver_->OnFrameDelivered(
      RtpPacketInfos(
          {RtpPacketInfo(kVideoSsrc + 1, {}, 101, Timestamp::Millis(120))}),
      Timestamp::Millis(120));

  EXPECT_EQ(observer.source_changed_count(), 1);
}

// When the receiver is set up for unsignaled streams again (e.g. on
// renegotiation), the media channel may keep the existing unsignaled stream, in
// which case there's no new first packet notification. So the SSRC that the
// receiver was bound to must be kept.
TEST_F(VideoRtpReceiverTest, KeepsUnsignaledSsrcWhenSetUpAsUnsignaledAgain) {
  constexpr uint32_t kVideoSsrc = 12345;
  RunOnWorkerThread(receiver_->GetSetupForUnsignaledMediaChannel());
  EXPECT_FALSE(receiver_->has_signaled_ssrc_s());
  EXPECT_EQ(receiver_->ssrc_s(), std::nullopt);

  receiver_->SetUnsignaledSsrc_s(kVideoSsrc);
  EXPECT_EQ(receiver_->ssrc_s(), kVideoSsrc);

  RunOnWorkerThread(receiver_->GetSetupForUnsignaledMediaChannel());
  EXPECT_FALSE(receiver_->has_signaled_ssrc_s());
  EXPECT_EQ(receiver_->ssrc_s(), kVideoSsrc);
}

TEST_F(VideoRtpReceiverTest, SignaledSsrcTakesPrecedenceOverUnsignaledSsrc) {
  constexpr uint32_t kVideoSsrc = 12345;
  constexpr uint32_t kSignaledSsrc = kVideoSsrc + 1;
  constexpr uint32_t kNewUnsignaledSsrc = kVideoSsrc + 2;
  RunOnWorkerThread(receiver_->GetSetupForUnsignaledMediaChannel());
  receiver_->SetUnsignaledSsrc_s(kVideoSsrc);

  RunOnWorkerThread(receiver_->GetSetupForMediaChannel(kSignaledSsrc));
  EXPECT_TRUE(receiver_->has_signaled_ssrc_s());
  EXPECT_EQ(receiver_->ssrc_s(), kSignaledSsrc);

  // When set up for unsignaled streams again, the receiver can be bound to a
  // new unsignaled stream.
  RunOnWorkerThread(receiver_->GetSetupForUnsignaledMediaChannel());
  EXPECT_FALSE(receiver_->has_signaled_ssrc_s());
  receiver_->SetUnsignaledSsrc_s(kNewUnsignaledSsrc);
  EXPECT_EQ(receiver_->ssrc_s(), kNewUnsignaledSsrc);
}

}  // namespace
}  // namespace webrtc
