/*
 *  Copyright 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/audio_rtp_receiver.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/call/audio_sink.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/rtp_packet_info.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_receiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/test/rtc_error_matchers.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "media/base/media_channel.h"
#include "pc/test/mock_voice_media_receive_channel_interface.h"
#include "rtc_base/event.h"
#include "rtc_base/thread.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/wait_until.h"

using ::testing::_;
using ::testing::Eq;

static const int kTimeOut = 100;
static const double kDefaultVolume = 1;
static const double kVolume = 3.7;
static const double kVolumeMuted = 0.0;
static const uint32_t kSsrc = 3;

namespace webrtc {
class AudioRtpReceiverTest : public ::testing::Test {
 protected:
  AudioRtpReceiverTest()
      : worker_thread_(Thread::Create()),
        receiver_(make_ref_counted<AudioRtpReceiver>(
            worker_thread_.get(),
            std::string(),
            std::vector<std::string>(),
            /*enable_sframe_at_owner=*/nullptr)) {
    worker_thread_->Start();
    EXPECT_CALL(receive_channel_, SetRawAudioSink(kSsrc, _));
    EXPECT_CALL(receive_channel_, SetBaseMinimumPlayoutDelayMs(kSsrc, _));
  }

  ~AudioRtpReceiverTest() override {
    EXPECT_CALL(receive_channel_, SetOutputVolume(kSsrc, kVolumeMuted));
    SetMediaChannel(nullptr);
  }

  void SetMediaChannel(MediaReceiveChannelInterface* media_channel) {
    worker_thread_->BlockingCall(
        [&]() { receiver_->SetMediaChannel(media_channel); });
  }

  test::RunLoop loop_;
  std::unique_ptr<Thread> worker_thread_;
  scoped_refptr<AudioRtpReceiver> receiver_;
  MockVoiceMediaReceiveChannelInterface receive_channel_;
};

TEST_F(AudioRtpReceiverTest, SetOutputVolumeIsCalled) {
  std::atomic_int set_volume_calls(0);

  EXPECT_CALL(receive_channel_, SetOutputVolume(kSsrc, kDefaultVolume))
      .WillOnce([&] {
        set_volume_calls++;
        return true;
      });

  receiver_->track();
  receiver_->track()->set_enabled(true);
  SetMediaChannel(&receive_channel_);
  EXPECT_CALL(receive_channel_, SetDefaultRawAudioSink(_)).Times(0);
  auto setup_task = receiver_->GetSetupForMediaChannel(kSsrc);
  worker_thread_->BlockingCall([&]() { std::move(setup_task)(); });

  EXPECT_CALL(receive_channel_, SetOutputVolume(kSsrc, kVolume)).WillOnce([&] {
    set_volume_calls++;
    return true;
  });

  receiver_->OnSetVolume(kVolume);
  EXPECT_THAT(WaitUntil([&] { return set_volume_calls.load(); }, Eq(2),
                        {.timeout = TimeDelta::Millis(kTimeOut)}),
              IsRtcOk());
}

TEST_F(AudioRtpReceiverTest, VolumesSetBeforeStartingAreRespected) {
  // Set the volume before setting the media channel. It should still be used
  // as the initial volume.
  receiver_->OnSetVolume(kVolume);

  receiver_->track()->set_enabled(true);
  SetMediaChannel(&receive_channel_);

  // The previosly set initial volume should be propagated to the provided
  // media_channel_ as soon as GetSetupForMediaChannel is called.
  EXPECT_CALL(receive_channel_, SetOutputVolume(kSsrc, kVolume));

  auto setup_task = receiver_->GetSetupForMediaChannel(kSsrc);
  worker_thread_->BlockingCall([&]() { std::move(setup_task)(); });
}

// Tests that a volume that is set while the track is disabled is applied when
// the track is enabled again.
TEST_F(AudioRtpReceiverTest, VolumeSetWhileTrackDisabledIsAppliedWhenEnabled) {
  receiver_->track()->set_enabled(true);
  SetMediaChannel(&receive_channel_);
  EXPECT_CALL(receive_channel_, SetOutputVolume(kSsrc, kDefaultVolume));
  auto setup_task = receiver_->GetSetupForMediaChannel(kSsrc);
  worker_thread_->BlockingCall([&]() { std::move(setup_task)(); });

  EXPECT_CALL(receive_channel_, SetOutputVolume(kSsrc, kVolumeMuted));
  receiver_->track()->set_enabled(false);
  receiver_->OnSetVolume(kVolume);
  // Flush the worker thread.
  worker_thread_->BlockingCall([] {});

  EXPECT_CALL(receive_channel_, SetOutputVolume(kSsrc, kVolume));
  receiver_->track()->set_enabled(true);
  // Flush the worker thread.
  worker_thread_->BlockingCall([] {});
}

// Tests that OnSetVolume() does not wait for the worker thread.
TEST_F(AudioRtpReceiverTest, SetVolumeDoesNotWaitForWorkerThread) {
  receiver_->track()->set_enabled(true);
  SetMediaChannel(&receive_channel_);
  EXPECT_CALL(receive_channel_, SetOutputVolume(kSsrc, kDefaultVolume));
  auto setup_task = receiver_->GetSetupForMediaChannel(kSsrc);
  worker_thread_->BlockingCall([&]() { std::move(setup_task)(); });

  // Keep the worker thread busy until OnSetVolume() has returned.
  Event worker_busy;
  Event set_volume_returned;
  bool returned_while_busy = false;
  worker_thread_->PostTask([&] {
    worker_busy.Set();
    returned_while_busy = set_volume_returned.Wait(TimeDelta::Seconds(5));
  });
  ASSERT_TRUE(worker_busy.Wait(TimeDelta::Seconds(5)));

  EXPECT_CALL(receive_channel_, SetOutputVolume(kSsrc, kVolume));
  receiver_->OnSetVolume(kVolume);
  set_volume_returned.Set();
  // Flush the worker thread.
  worker_thread_->BlockingCall([] {});
  EXPECT_TRUE(returned_while_busy);
}

// Tests that a volume that is set on another thread than the signaling thread,
// as done by the Android and Objective-C SDKs, is applied.
TEST_F(AudioRtpReceiverTest, VolumeSetOnOtherThreadIsApplied) {
  receiver_->track()->set_enabled(true);
  SetMediaChannel(&receive_channel_);
  EXPECT_CALL(receive_channel_, SetOutputVolume(kSsrc, kDefaultVolume));
  auto setup_task = receiver_->GetSetupForMediaChannel(kSsrc);
  worker_thread_->BlockingCall([&]() { std::move(setup_task)(); });

  EXPECT_CALL(receive_channel_, SetOutputVolume(kSsrc, kVolume));
  std::unique_ptr<Thread> other_thread = Thread::Create();
  other_thread->Start();
  other_thread->BlockingCall(
      [&] { receiver_->audio_track()->GetSource()->SetVolume(kVolume); });

  // Deliver the volume to the receiver on the signaling thread, then flush the
  // worker thread.
  loop_.Flush();
  worker_thread_->BlockingCall([] {});
}

TEST(AudioRtpReceiver, PropagatesPacketInfosToTrackSink) {
  test::RunLoop loop;

  std::unique_ptr<Thread> worker_thread = Thread::Create();
  worker_thread->Start();
  MockVoiceMediaReceiveChannelInterface receive_channel;
  auto receiver = make_ref_counted<AudioRtpReceiver>(
      worker_thread.get(), std::string(), std::vector<std::string>(),
      /*enable_sframe_at_owner=*/nullptr);

  std::unique_ptr<AudioSinkInterface> audio_sink;
  EXPECT_CALL(receive_channel, SetRawAudioSink(kSsrc, _))
      .WillOnce(
          [&](uint32_t /* ssrc */, std::unique_ptr<AudioSinkInterface> sink) {
            audio_sink = std::move(sink);
          });
  EXPECT_CALL(receive_channel, SetBaseMinimumPlayoutDelayMs(kSsrc, _));

  class TestTrackSink : public AudioTrackSinkInterface {
   public:
    using AudioTrackSinkInterface::OnData;
    void OnData(const void* /* audio_data */,
                int /* bits_per_sample */,
                int /* sample_rate */,
                size_t /* number_of_channels */,
                size_t /* number_of_frames */,
                std::optional<int64_t> /* absolute_capture_timestamp_ms */,
                const RtpPacketInfos& packet_infos) override {
      called_ = true;
      received_packet_infos_ = packet_infos;
    }
    bool called_ = false;
    RtpPacketInfos received_packet_infos_;
  };

  TestTrackSink sink;
  receiver->audio_track()->AddSink(&sink);
  receiver->track()->set_enabled(true);
  worker_thread->BlockingCall(
      [&]() { receiver->SetMediaChannel(&receive_channel); });
  auto setup_task = receiver->GetSetupForMediaChannel(kSsrc);
  worker_thread->BlockingCall([&]() { std::move(setup_task)(); });

  ASSERT_TRUE(audio_sink != nullptr);

  RtpPacketInfos::vector_type infos_vec;
  infos_vec.emplace_back(
      RtpPacketInfo(/*ssrc=*/kSsrc, /*csrcs=*/{5678},
                    /*rtp_timestamp=*/9999,
                    /*receive_time=*/Timestamp::Millis(100)));
  RtpPacketInfos packet_infos(std::move(infos_vec));

  int16_t dummy_data[160] = {0};
  AudioSinkInterface::Data audio_data(dummy_data, 160, 16000, 1, 9999,
                                      &packet_infos);
  audio_sink->OnData(audio_data);

  EXPECT_TRUE(sink.called_);
  ASSERT_EQ(sink.received_packet_infos_.size(), 1u);
  EXPECT_EQ(sink.received_packet_infos_[0].ssrc(), kSsrc);
  EXPECT_THAT(sink.received_packet_infos_[0].csrcs(),
              ::testing::ElementsAre(5678));

  receiver->audio_track()->RemoveSink(&sink);
  EXPECT_CALL(receive_channel, SetOutputVolume(kSsrc, kVolumeMuted));
  worker_thread->BlockingCall([&]() { receiver->SetMediaChannel(nullptr); });
}

// Tests that OnChanged notifications are processed correctly on the worker
// thread when a media channel pointer is passed to the receiver via the
// constructor.
TEST(AudioRtpReceiver, OnChangedNotificationsAfterConstruction) {
  test::RunLoop loop;

  std::unique_ptr<Thread> worker_thread = Thread::Create();
  worker_thread->Start();
  MockVoiceMediaReceiveChannelInterface receive_channel;
  auto receiver = make_ref_counted<AudioRtpReceiver>(
      worker_thread.get(), std::string(), std::vector<std::string>(),
      /*enable_sframe_at_owner=*/nullptr, &receive_channel);

  EXPECT_CALL(receive_channel, SetDefaultRawAudioSink(_)).Times(1);
  EXPECT_CALL(receive_channel, SetDefaultOutputVolume(kDefaultVolume)).Times(1);
  auto setup_task = receiver->GetSetupForUnsignaledMediaChannel();
  worker_thread->BlockingCall([&]() { std::move(setup_task)(); });

  // When the track is marked as disabled, an async notification is queued
  // for the worker thread. This notification should trigger the volume
  // of the media channel to be set to kVolumeMuted.
  // Set the expectation first for the call, before changing the track state.
  EXPECT_CALL(receive_channel, SetDefaultOutputVolume(kVolumeMuted)).Times(1);

  // Mark the track as disabled.
  receiver->track()->set_enabled(false);

  // Flush the worker thread.
  worker_thread->BlockingCall([] {});

  EXPECT_CALL(receive_channel, SetDefaultOutputVolume(kVolumeMuted)).Times(1);
  worker_thread->BlockingCall([&]() { receiver->SetMediaChannel(nullptr); });
}

class FakeRtpReceiverObserver : public RtpReceiverObserverInterface {
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

class AudioRtpReceiverSourceTrackerTest : public ::testing::Test {
 protected:
  AudioRtpReceiverSourceTrackerTest()
      : worker_thread_(Thread::Create()),
        receiver_(make_ref_counted<AudioRtpReceiver>(
            worker_thread_.get(),
            std::string(),
            std::vector<std::string>(),
            /*enable_sframe_at_owner=*/nullptr)) {
    worker_thread_->Start();
  }

  test::RunLoop loop_;
  std::unique_ptr<Thread> worker_thread_;
  scoped_refptr<AudioRtpReceiver> receiver_;
};

TEST_F(AudioRtpReceiverSourceTrackerTest, SourceChangeNotification) {
  FakeRtpReceiverObserver observer;
  receiver_->SetObserver(&observer);

  // Deliver initial packet info.
  receiver_->OnFrameDelivered(
      RtpPacketInfos({RtpPacketInfo(kSsrc, {}, 100, Timestamp::Millis(100))}),
      Timestamp::Millis(100));

  EXPECT_EQ(observer.source_changed_count(), 1);
  EXPECT_TRUE(observer.last_ssrc_changed());
  EXPECT_FALSE(observer.last_csrc_changed());

  // Subsequent frame with same SSRC should not fire callback.
  receiver_->OnFrameDelivered(
      RtpPacketInfos({RtpPacketInfo(kSsrc, {}, 101, Timestamp::Millis(120))}),
      Timestamp::Millis(120));

  EXPECT_EQ(observer.source_changed_count(), 1);

  // Frame with CSRC change should fire callback.
  receiver_->OnFrameDelivered(RtpPacketInfos({RtpPacketInfo(
                                  kSsrc, {1234}, 102, Timestamp::Millis(140))}),
                              Timestamp::Millis(140));

  EXPECT_EQ(observer.source_changed_count(), 2);
  EXPECT_FALSE(observer.last_ssrc_changed());
  EXPECT_TRUE(observer.last_csrc_changed());

  receiver_->SetObserver(nullptr);
}

TEST_F(AudioRtpReceiverSourceTrackerTest, LateObserverRegistration) {
  FakeRtpReceiverObserver observer;

  // Frame delivered before observer is registered.
  receiver_->OnFrameDelivered(
      RtpPacketInfos({RtpPacketInfo(kSsrc, {}, 100, Timestamp::Millis(100))}),
      Timestamp::Millis(100));

  EXPECT_EQ(observer.source_changed_count(), 0);

  // Setting observer should trigger callback for existing source.
  receiver_->SetObserver(&observer);

  EXPECT_EQ(observer.source_changed_count(), 1);
  EXPECT_TRUE(observer.last_ssrc_changed());
  EXPECT_FALSE(observer.last_csrc_changed());

  receiver_->SetObserver(nullptr);
}

TEST_F(AudioRtpReceiverSourceTrackerTest, ObserverTeardownStopsNotifications) {
  FakeRtpReceiverObserver observer;
  receiver_->SetObserver(&observer);

  receiver_->OnFrameDelivered(
      RtpPacketInfos({RtpPacketInfo(kSsrc, {}, 100, Timestamp::Millis(100))}),
      Timestamp::Millis(100));
  EXPECT_EQ(observer.source_changed_count(), 1);

  receiver_->SetObserver(nullptr);

  // New SSRC after observer is cleared should not reach observer.
  receiver_->OnFrameDelivered(RtpPacketInfos({RtpPacketInfo(
                                  kSsrc + 1, {}, 101, Timestamp::Millis(120))}),
                              Timestamp::Millis(120));

  EXPECT_EQ(observer.source_changed_count(), 1);
}

// Tests the SSRC state that the receiver maintains on the signaling thread.
// No media channel is set, so the worker thread part of a setup is a no-op.
class AudioRtpReceiverSsrcTest : public ::testing::Test {
 protected:
  AudioRtpReceiverSsrcTest()
      : worker_thread_(Thread::Create()),
        receiver_(make_ref_counted<AudioRtpReceiver>(
            worker_thread_.get(),
            std::string(),
            std::vector<std::string>(),
            /*enable_sframe_at_owner=*/nullptr)) {
    worker_thread_->Start();
  }

  void RunOnWorkerThread(absl::AnyInvocable<void() &&> task) {
    worker_thread_->BlockingCall([&] { std::move(task)(); });
  }

  test::RunLoop loop_;
  std::unique_ptr<Thread> worker_thread_;
  scoped_refptr<AudioRtpReceiver> receiver_;
};

// When the receiver is set up for unsignaled streams again (e.g. on
// renegotiation), the media channel may keep the existing unsignaled stream, in
// which case there's no new first packet notification. So the SSRC that the
// receiver was bound to must be kept.
TEST_F(AudioRtpReceiverSsrcTest,
       KeepsUnsignaledSsrcWhenSetUpAsUnsignaledAgain) {
  RunOnWorkerThread(receiver_->GetSetupForUnsignaledMediaChannel());
  EXPECT_FALSE(receiver_->has_signaled_ssrc_s());
  EXPECT_EQ(receiver_->ssrc_s(), std::nullopt);

  receiver_->SetUnsignaledSsrc_s(kSsrc);
  EXPECT_EQ(receiver_->ssrc_s(), kSsrc);

  RunOnWorkerThread(receiver_->GetSetupForUnsignaledMediaChannel());
  EXPECT_FALSE(receiver_->has_signaled_ssrc_s());
  EXPECT_EQ(receiver_->ssrc_s(), kSsrc);
}

TEST_F(AudioRtpReceiverSsrcTest,
       SignaledSsrcTakesPrecedenceOverUnsignaledSsrc) {
  constexpr uint32_t kSignaledSsrc = kSsrc + 1;
  constexpr uint32_t kNewUnsignaledSsrc = kSsrc + 2;
  RunOnWorkerThread(receiver_->GetSetupForUnsignaledMediaChannel());
  receiver_->SetUnsignaledSsrc_s(kSsrc);

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

}  // namespace webrtc
