/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/audio_state.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/audio/audio_frame.h"
#include "api/audio/audio_frame_processor.h"
#include "api/audio/audio_mixer.h"
#include "api/location.h"
#include "api/make_ref_counted.h"
#include "api/ref_count.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/task_queue/test/mock_task_queue_base.h"
#include "call/audio_state.h"
#include "call/test/mock_audio_receive_stream.h"
#include "call/test/mock_audio_send_stream.h"
#include "modules/async_audio_processing/async_audio_processing.h"
#include "modules/audio_device/include/mock_audio_device.h"
#include "modules/audio_mixer/audio_mixer_impl.h"
#include "modules/audio_processing/include/mock_audio_processing.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/thread.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace test {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::Matcher;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::StrictMock;
using ::testing::Values;

constexpr int kSampleRate = 16000;
constexpr int kNumberOfChannels = 1;

struct FakeAsyncAudioProcessingHelper {
  class FakeTaskQueue : public StrictMock<MockTaskQueueBase> {
   public:
    FakeTaskQueue() = default;

    void Delete() override { delete this; }
    void PostTaskImpl(absl::AnyInvocable<void() &&> task,
                      const PostTaskTraits& /*traits*/,
                      const Location& /*location*/) override {
      std::move(task)();
    }
  };

  class FakeTaskQueueFactory : public TaskQueueFactory {
   public:
    FakeTaskQueueFactory() = default;
    ~FakeTaskQueueFactory() override = default;
    std::unique_ptr<TaskQueueBase, TaskQueueDeleter> CreateTaskQueue(
        absl::string_view /* name */,
        Priority /* priority */) const override {
      return std::unique_ptr<TaskQueueBase, TaskQueueDeleter>(
          new FakeTaskQueue());
    }
  };

  class MockAudioFrameProcessor : public AudioFrameProcessor {
   public:
    ~MockAudioFrameProcessor() override = default;

    MOCK_METHOD(void, ProcessCalled, ());
    MOCK_METHOD(void, SinkSet, ());
    MOCK_METHOD(void, SinkCleared, ());

    void Process(std::unique_ptr<AudioFrame> frame) override {
      ProcessCalled();
      sink_callback_(std::move(frame));
    }

    void SetSink(OnAudioFrameCallback sink_callback) override {
      sink_callback_ = std::move(sink_callback);
      if (sink_callback_ == nullptr)
        SinkCleared();
      else
        SinkSet();
    }

   private:
    OnAudioFrameCallback sink_callback_;
  };

  NiceMock<MockAudioFrameProcessor> audio_frame_processor_;
  FakeTaskQueueFactory task_queue_factory_;

  scoped_refptr<AsyncAudioProcessing::Factory> CreateFactory() {
    return make_ref_counted<AsyncAudioProcessing::Factory>(
        audio_frame_processor_, task_queue_factory_);
  }
};

struct ConfigHelper {
  struct Params {
    bool use_null_audio_processing;
    bool use_async_audio_processing;
  };

  explicit ConfigHelper(const Params& params)
      : audio_mixer(AudioMixerImpl::Create()) {
    audio_state_config.audio_mixer = audio_mixer;
    audio_state_config.audio_processing =
        params.use_null_audio_processing
            ? nullptr
            : make_ref_counted<testing::NiceMock<MockAudioProcessing>>();
    audio_state_config.audio_device_module =
        make_ref_counted<NiceMock<MockAudioDeviceModule>>();
    if (params.use_async_audio_processing) {
      audio_state_config.async_audio_processing_factory =
          async_audio_processing_helper_.CreateFactory();
    }
  }
  AudioState::Config& config() { return audio_state_config; }
  scoped_refptr<AudioMixer> mixer() { return audio_mixer; }
  NiceMock<FakeAsyncAudioProcessingHelper::MockAudioFrameProcessor>&
  mock_audio_frame_processor() {
    return async_audio_processing_helper_.audio_frame_processor_;
  }

 private:
  AudioState::Config audio_state_config;
  scoped_refptr<AudioMixer> audio_mixer;
  FakeAsyncAudioProcessingHelper async_audio_processing_helper_;
};

class FakeAudioSource : public AudioMixer::Source {
 public:
  int Ssrc() const override { return 0; }

  int PreferredSampleRate() const override { return kSampleRate; }

  MOCK_METHOD(AudioFrameInfo,
              GetAudioFrameWithInfo,
              (int sample_rate_hz, AudioFrame*),
              (override));
};

std::vector<int16_t> Create10msTestData(int sample_rate_hz,
                                        size_t num_channels) {
  const int samples_per_channel = sample_rate_hz / 100;
  std::vector<int16_t> audio_data(samples_per_channel * num_channels, 0);
  // Fill the first channel with a 1kHz sine wave.
  const float inc = (2 * std::numbers::pi_v<float> * 1000) / sample_rate_hz;
  float w = 0.f;
  for (int i = 0; i < samples_per_channel; ++i) {
    audio_data[i * num_channels] = static_cast<int16_t>(32767.f * std::sin(w));
    w += inc;
  }
  return audio_data;
}

std::vector<uint32_t> ComputeChannelLevels(AudioFrame* audio_frame) {
  const size_t num_channels = audio_frame->num_channels_;
  const size_t samples_per_channel = audio_frame->samples_per_channel_;
  std::vector<uint32_t> levels(num_channels, 0);
  for (size_t i = 0; i < samples_per_channel; ++i) {
    for (size_t j = 0; j < num_channels; ++j) {
      levels[j] += std::abs(audio_frame->data()[i * num_channels + j]);
    }
  }
  return levels;
}
}  // namespace

class AudioStateTest : public ::testing::TestWithParam<ConfigHelper::Params> {};

TEST_P(AudioStateTest, Create) {
  ConfigHelper helper(GetParam());
  auto audio_state = AudioState::Create(helper.config());
  EXPECT_TRUE(audio_state.get());
}

TEST_P(AudioStateTest, ConstructDestruct) {
  ConfigHelper helper(GetParam());
  scoped_refptr<internal::AudioState> audio_state(
      make_ref_counted<internal::AudioState>(helper.config()));
}

TEST_P(AudioStateTest, CreateUseDeleteOnDifferentThread) {
  ConfigHelper helper(GetParam());
  scoped_refptr<AudioState> audio_state = AudioState::Create(helper.config());
  ASSERT_THAT(audio_state, NotNull());
  TaskQueueForTest queue;
  queue.SendTask([&] {
    // Attach to the current TQ.
    audio_state->SetStereoChannelSwapping(true);
    // Ensure the object gets deleted on the current thread.
    EXPECT_EQ(audio_state.release()->Release(),
              RefCountReleaseStatus::kDroppedLastRef);
  });
}

TEST_P(AudioStateTest, RecordedAudioArrivesAtSingleStream) {
  ConfigHelper helper(GetParam());

  if (GetParam().use_async_audio_processing) {
    EXPECT_CALL(helper.mock_audio_frame_processor(), SinkSet);
    EXPECT_CALL(helper.mock_audio_frame_processor(), ProcessCalled);
    EXPECT_CALL(helper.mock_audio_frame_processor(), SinkCleared);
  }

  scoped_refptr<internal::AudioState> audio_state(
      make_ref_counted<internal::AudioState>(helper.config()));

  MockAudioSendStream stream;
  audio_state->AddSendingStream(&stream, 8000, 2);

  EXPECT_CALL(
      stream,
      SendAudioDataForMock(::testing::AllOf(
          ::testing::Field(&AudioFrame::sample_rate_hz_, ::testing::Eq(8000)),
          ::testing::Field(&AudioFrame::num_channels_, ::testing::Eq(2u)))))
      .WillOnce(
          // Verify that channels are not swapped by default.
          [](AudioFrame* audio_frame) {
            auto levels = ComputeChannelLevels(audio_frame);
            EXPECT_LT(0u, levels[0]);
            EXPECT_EQ(0u, levels[1]);
          });
  MockAudioProcessing* ap =
      GetParam().use_null_audio_processing
          ? nullptr
          : static_cast<MockAudioProcessing*>(audio_state->audio_processing());
  if (ap) {
    EXPECT_CALL(*ap, set_stream_delay_ms(0));
    EXPECT_CALL(*ap, set_stream_key_pressed(false));
    EXPECT_CALL(*ap, ProcessStream(_, _, _, Matcher<int16_t*>(_)));
  }

  constexpr size_t kNumChannels = 2;
  auto audio_data = Create10msTestData(kSampleRate, kNumChannels);
  uint32_t new_mic_level = 667;
  audio_state->audio_transport()->RecordedDataIsAvailable(
      &audio_data[0], kSampleRate / 100, kNumChannels * 2, kNumChannels,
      kSampleRate, 0, 0, 0, false, new_mic_level, std::nullopt);
  EXPECT_EQ(667u, new_mic_level);

  audio_state->RemoveSendingStream(&stream);
}

TEST_P(AudioStateTest, RecordedAudioArrivesAtMultipleStreams) {
  ConfigHelper helper(GetParam());

  if (GetParam().use_async_audio_processing) {
    EXPECT_CALL(helper.mock_audio_frame_processor(), SinkSet);
    EXPECT_CALL(helper.mock_audio_frame_processor(), ProcessCalled);
    EXPECT_CALL(helper.mock_audio_frame_processor(), SinkCleared);
  }

  scoped_refptr<internal::AudioState> audio_state(
      make_ref_counted<internal::AudioState>(helper.config()));

  MockAudioSendStream stream_1;
  MockAudioSendStream stream_2;
  audio_state->AddSendingStream(&stream_1, 8001, 2);
  audio_state->AddSendingStream(&stream_2, 32000, 1);

  EXPECT_CALL(
      stream_1,
      SendAudioDataForMock(::testing::AllOf(
          ::testing::Field(&AudioFrame::sample_rate_hz_, ::testing::Eq(16000)),
          ::testing::Field(&AudioFrame::num_channels_, ::testing::Eq(1u)))))
      .WillOnce(
          // Verify that there is output signal.
          [](AudioFrame* audio_frame) {
            auto levels = ComputeChannelLevels(audio_frame);
            EXPECT_LT(0u, levels[0]);
          });
  EXPECT_CALL(
      stream_2,
      SendAudioDataForMock(::testing::AllOf(
          ::testing::Field(&AudioFrame::sample_rate_hz_, ::testing::Eq(16000)),
          ::testing::Field(&AudioFrame::num_channels_, ::testing::Eq(1u)))))
      .WillOnce(
          // Verify that there is output signal.
          [](AudioFrame* audio_frame) {
            auto levels = ComputeChannelLevels(audio_frame);
            EXPECT_LT(0u, levels[0]);
          });
  MockAudioProcessing* ap =
      static_cast<MockAudioProcessing*>(audio_state->audio_processing());
  if (ap) {
    EXPECT_CALL(*ap, set_stream_delay_ms(5));
    EXPECT_CALL(*ap, set_stream_key_pressed(true));
    EXPECT_CALL(*ap, ProcessStream(_, _, _, Matcher<int16_t*>(_)));
  }

  constexpr size_t kNumChannels = 1;
  auto audio_data = Create10msTestData(kSampleRate, kNumChannels);
  uint32_t new_mic_level = 667;
  audio_state->audio_transport()->RecordedDataIsAvailable(
      &audio_data[0], kSampleRate / 100, kNumChannels * 2, kNumChannels,
      kSampleRate, 5, 0, 0, true, new_mic_level, std::nullopt);
  EXPECT_EQ(667u, new_mic_level);

  audio_state->RemoveSendingStream(&stream_1);
  audio_state->RemoveSendingStream(&stream_2);
}

TEST_P(AudioStateTest, EnableChannelSwap) {
  constexpr size_t kNumChannels = 2;

  ConfigHelper helper(GetParam());

  if (GetParam().use_async_audio_processing) {
    EXPECT_CALL(helper.mock_audio_frame_processor(), SinkSet);
    EXPECT_CALL(helper.mock_audio_frame_processor(), ProcessCalled);
    EXPECT_CALL(helper.mock_audio_frame_processor(), SinkCleared);
  }

  scoped_refptr<internal::AudioState> audio_state(
      make_ref_counted<internal::AudioState>(helper.config()));

  audio_state->SetStereoChannelSwapping(true);

  MockAudioSendStream stream;
  audio_state->AddSendingStream(&stream, kSampleRate, kNumChannels);

  EXPECT_CALL(stream, SendAudioDataForMock(_))
      .WillOnce(
          // Verify that channels are swapped.
          [](AudioFrame* audio_frame) {
            auto levels = ComputeChannelLevels(audio_frame);
            EXPECT_EQ(0u, levels[0]);
            EXPECT_LT(0u, levels[1]);
          });

  auto audio_data = Create10msTestData(kSampleRate, kNumChannels);
  uint32_t new_mic_level = 667;
  audio_state->audio_transport()->RecordedDataIsAvailable(
      &audio_data[0], kSampleRate / 100, kNumChannels * 2, kNumChannels,
      kSampleRate, 0, 0, 0, false, new_mic_level, std::nullopt);
  EXPECT_EQ(667u, new_mic_level);

  audio_state->RemoveSendingStream(&stream);
}

TEST_P(AudioStateTest,
       QueryingTransportForAudioShouldResultInGetAudioCallOnMixerSource) {
  ConfigHelper helper(GetParam());
  auto audio_state = AudioState::Create(helper.config());

  FakeAudioSource fake_source;
  helper.mixer()->AddSource(&fake_source);

  EXPECT_CALL(fake_source, GetAudioFrameWithInfo(_, _))
      .WillOnce([](int sample_rate_hz, AudioFrame* audio_frame) {
        audio_frame->sample_rate_hz_ = sample_rate_hz;
        audio_frame->samples_per_channel_ = sample_rate_hz / 100;
        audio_frame->num_channels_ = kNumberOfChannels;
        return AudioMixer::Source::AudioFrameInfo::kNormal;
      });

  int16_t audio_buffer[kSampleRate / 100 * kNumberOfChannels];
  size_t n_samples_out;
  int64_t elapsed_time_ms;
  int64_t ntp_time_ms;
  audio_state->audio_transport()->NeedMorePlayData(
      kSampleRate / 100, kNumberOfChannels * 2, kNumberOfChannels, kSampleRate,
      audio_buffer, n_samples_out, &elapsed_time_ms, &ntp_time_ms);
}

TEST_P(AudioStateTest, StartRecordingDoesNothingWithoutStream) {
  ConfigHelper helper(GetParam());
  scoped_refptr<internal::AudioState> audio_state(
      make_ref_counted<internal::AudioState>(helper.config()));

  auto* adm = reinterpret_cast<MockAudioDeviceModule*>(
      helper.config().audio_device_module.get());

  EXPECT_CALL(*adm, InitRecording()).Times(0);
  EXPECT_CALL(*adm, StartRecording()).Times(0);
  EXPECT_CALL(*adm, StopRecording()).Times(1);
  audio_state->SetRecording(false);
  audio_state->SetRecording(true);
}

TEST_P(AudioStateTest, AddStreamDoesNothingIfRecordingDisabled) {
  ConfigHelper helper(GetParam());
  scoped_refptr<internal::AudioState> audio_state(
      make_ref_counted<internal::AudioState>(helper.config()));

  auto* adm = reinterpret_cast<MockAudioDeviceModule*>(
      helper.config().audio_device_module.get());

  EXPECT_CALL(*adm, StopRecording()).Times(2);
  audio_state->SetRecording(false);

  MockAudioSendStream stream;
  EXPECT_CALL(*adm, StartRecording).Times(0);
  audio_state->AddSendingStream(&stream, kSampleRate, kNumberOfChannels);
  audio_state->RemoveSendingStream(&stream);
}

TEST_P(AudioStateTest, AlwaysCallInitRecordingBeforeStartRecording) {
  ConfigHelper helper(GetParam());
  scoped_refptr<internal::AudioState> audio_state(
      make_ref_counted<internal::AudioState>(helper.config()));

  auto* adm = reinterpret_cast<MockAudioDeviceModule*>(
      helper.config().audio_device_module.get());

  MockAudioSendStream stream;
  {
    InSequence s;
    EXPECT_CALL(*adm, InitRecording());
    EXPECT_CALL(*adm, StartRecording());
    audio_state->AddSendingStream(&stream, kSampleRate, kNumberOfChannels);
  }

  EXPECT_CALL(*adm, StopRecording());
  audio_state->SetRecording(false);

  {
    InSequence s;
    EXPECT_CALL(*adm, InitRecording());
    EXPECT_CALL(*adm, StartRecording());
    audio_state->SetRecording(true);
  }

  EXPECT_CALL(*adm, StopRecording());
  audio_state->RemoveSendingStream(&stream);
}

// The recording can also be initialized by WebRtcVoiceSendChannel
// options_.init_recording_on_send. Make sure StopRecording is still
// being called in this scenario.
TEST_P(AudioStateTest, CallStopRecordingIfRecordingIsInitialized) {
  ConfigHelper helper(GetParam());
  scoped_refptr<internal::AudioState> audio_state(
      make_ref_counted<internal::AudioState>(helper.config()));

  auto* adm = reinterpret_cast<MockAudioDeviceModule*>(
      helper.config().audio_device_module.get());

  audio_state->SetRecording(false);

  EXPECT_CALL(*adm, StopRecording());
  audio_state->SetRecording(false);
}

TEST_P(AudioStateTest, StartPlayoutDoesNothingWithoutStream) {
  ConfigHelper helper(GetParam());
  scoped_refptr<internal::AudioState> audio_state(
      make_ref_counted<internal::AudioState>(helper.config()));

  auto* adm = reinterpret_cast<MockAudioDeviceModule*>(
      helper.config().audio_device_module.get());

  EXPECT_CALL(*adm, InitPlayout()).Times(0);
  EXPECT_CALL(*adm, StartPlayout()).Times(0);
  EXPECT_CALL(*adm, StopPlayout()).Times(1);
  audio_state->SetPlayout(false);

  audio_state->SetPlayout(true);
}

TEST_P(AudioStateTest, AlwaysCallInitPlayoutBeforeStartPlayout) {
  ConfigHelper helper(GetParam());
  scoped_refptr<internal::AudioState> audio_state(
      make_ref_counted<internal::AudioState>(helper.config()));

  auto* adm = reinterpret_cast<MockAudioDeviceModule*>(
      helper.config().audio_device_module.get());

  MockAudioReceiveStream stream;
  {
    InSequence s;
    EXPECT_CALL(*adm, InitPlayout());
    EXPECT_CALL(*adm, StartPlayout());
    audio_state->AddReceivingStream(&stream);
  }

  // SetPlayout(false) starts the NullAudioPoller...which needs a thread.
  ThreadManager::Instance()->WrapCurrentThread();

  EXPECT_CALL(*adm, StopPlayout());
  audio_state->SetPlayout(false);

  {
    InSequence s;
    EXPECT_CALL(*adm, InitPlayout());
    EXPECT_CALL(*adm, StartPlayout());
    audio_state->SetPlayout(true);
  }

  // Playout without streams starts the NullAudioPoller...
  // which needs a thread.
  ThreadManager::Instance()->WrapCurrentThread();

  EXPECT_CALL(*adm, StopPlayout());
  audio_state->RemoveReceivingStream(&stream);
}

TEST_P(AudioStateTest, CallStopPlayoutIfPlayoutIsInitialized) {
  ConfigHelper helper(GetParam());
  scoped_refptr<internal::AudioState> audio_state(
      make_ref_counted<internal::AudioState>(helper.config()));

  auto* adm = reinterpret_cast<MockAudioDeviceModule*>(
      helper.config().audio_device_module.get());

  audio_state->SetPlayout(false);

  EXPECT_CALL(*adm, StopPlayout());
  audio_state->SetPlayout(false);
}

TEST_P(AudioStateTest, AddStreamDoesNothingIfPlayoutDisabled) {
  ConfigHelper helper(GetParam());
  scoped_refptr<internal::AudioState> audio_state(
      make_ref_counted<internal::AudioState>(helper.config()));

  auto* adm = reinterpret_cast<MockAudioDeviceModule*>(
      helper.config().audio_device_module.get());

  EXPECT_CALL(*adm, StopPlayout()).Times(2);
  audio_state->SetPlayout(false);

  // AddReceivingStream with playout disabled start the NullAudioPoller...
  // which needs a thread.
  ThreadManager::Instance()->WrapCurrentThread();

  MockAudioReceiveStream stream;
  audio_state->AddReceivingStream(&stream);
  audio_state->RemoveReceivingStream(&stream);
}

INSTANTIATE_TEST_SUITE_P(AudioStateTest,
                         AudioStateTest,
                         Values(ConfigHelper::Params({false, false}),
                                ConfigHelper::Params({true, false}),
                                ConfigHelper::Params({false, true}),
                                ConfigHelper::Params({true, true})));

}  // namespace test
}  // namespace webrtc
