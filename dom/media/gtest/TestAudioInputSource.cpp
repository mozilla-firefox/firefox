/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "AudioInputSource.h"
#include "MockCubeb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mozilla/Result.h"
#include "mozilla/gtest/WaitFor.h"
#include "nsContentUtils.h"

using namespace mozilla;
using testing::ContainerEq;

// Short-hand for DispatchToCurrentThread with a function.
#define DispatchFunction(f) \
  NS_DispatchToCurrentThread(NS_NewRunnableFunction(__func__, f))

// Short-hand for draining the current threads event queue, i.e. processing
// those runnables dispatched per above.
#define ProcessEventQueue()                     \
  while (NS_ProcessNextEvent(nullptr, false)) { \
  }

class MockAudioEventListener : public AudioInputSource::EventListener {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MockAudioEventListener, override);
  MOCK_METHOD1(AudioDeviceChanged, void(AudioInputSource::Id));
  MOCK_METHOD2(AudioStateCallback,
               void(AudioInputSource::Id,
                    AudioInputSource::EventListener::State));

 private:
  ~MockAudioEventListener() = default;
};

TEST(TestAudioInputSource, StartAndStop)
{
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  const AudioInputSource::Id sourceId = 1;
  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;
  const uint32_t channels = 2;
  const PrincipalHandle testPrincipal =
      MakePrincipalHandle(nsContentUtils::GetSystemPrincipal());
  const TrackRate sourceRate = 44100;
  const TrackRate targetRate = 48000;

  auto listener = MakeRefPtr<MockAudioEventListener>();
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Started))
      .Times(2);
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Stopped))
      .Times(4);

  RefPtr<AudioInputSource> ais = MakeRefPtr<AudioInputSource>(
      std::move(listener), sourceId, deviceId, channels, true, testPrincipal,
      sourceRate, targetRate);
  ASSERT_TRUE(ais);

  // Make sure start and stop works.
  {
    DispatchFunction([&] {
      ais->Init();
      ais->Start();
    });
    RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
    EXPECT_TRUE(stream->mHasInput);
    EXPECT_FALSE(stream->mHasOutput);
    EXPECT_EQ(stream->GetInputDeviceID(), deviceId);
    EXPECT_EQ(stream->InputChannels(), channels);
    EXPECT_EQ(stream->SampleRate(), static_cast<uint32_t>(sourceRate));

    (void)WaitFor(stream->FramesProcessedEvent());

    DispatchFunction([&] { ais->Stop(); });
    (void)WaitFor(cubeb->StreamDestroyEvent());
  }

  // Make sure restart is ok.
  {
    DispatchFunction([&] {
      ais->Init();
      ais->Start();
    });
    RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
    EXPECT_TRUE(stream->mHasInput);
    EXPECT_FALSE(stream->mHasOutput);
    EXPECT_EQ(stream->GetInputDeviceID(), deviceId);
    EXPECT_EQ(stream->InputChannels(), channels);
    EXPECT_EQ(stream->SampleRate(), static_cast<uint32_t>(sourceRate));

    (void)WaitFor(stream->FramesProcessedEvent());

    DispatchFunction([&] { ais->Stop(); });
    (void)WaitFor(cubeb->StreamDestroyEvent());
  }

  ais = nullptr;  // Drop the SharedThreadPool here.
}

TEST(TestAudioInputSource, DataOutputBeforeStartAndAfterStop)
{
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  const AudioInputSource::Id sourceId = 1;
  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;
  const uint32_t channels = 2;
  const PrincipalHandle testPrincipal =
      MakePrincipalHandle(nsContentUtils::GetSystemPrincipal());
  const TrackRate sourceRate = 44100;
  const TrackRate targetRate = 48000;

  const TrackTime requestFrames = 2 * WEBAUDIO_BLOCK_SIZE;

  auto listener = MakeRefPtr<MockAudioEventListener>();
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Started));
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Stopped))
      .Times(2);

  RefPtr<AudioInputSource> ais = MakeRefPtr<AudioInputSource>(
      std::move(listener), sourceId, deviceId, channels, true, testPrincipal,
      sourceRate, targetRate);
  ASSERT_TRUE(ais);

  // It's ok to call GetAudioSegment before starting
  {
    AudioSegment data =
        ais->GetAudioSegment(requestFrames, AudioInputSource::Consumer::Same);
    EXPECT_EQ(data.GetDuration(), requestFrames);
    EXPECT_TRUE(data.IsNull());
  }

  DispatchFunction([&] {
    ais->Init();
    ais->Start();
  });
  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasInput);
  EXPECT_FALSE(stream->mHasOutput);
  EXPECT_EQ(stream->InputChannels(), channels);

  stream->SetInputRecordingEnabled(true);

  (void)WaitFor(stream->FramesProcessedEvent());

  DispatchFunction([&] { ais->Stop(); });
  (void)WaitFor(cubeb->StreamDestroyEvent());

  // Check the data output
  {
    nsTArray<AudioDataValue> record = stream->TakeRecordedInput();
    size_t frames = record.Length() / channels;
    AudioSegment deinterleaved;
    deinterleaved.AppendFromInterleavedBuffer(record.Elements(), frames,
                                              channels, testPrincipal);
    AudioDriftCorrection driftCorrector(sourceRate, targetRate, testPrincipal);
    AudioSegment expectedSegment = driftCorrector.RequestFrames(
        deinterleaved, static_cast<uint32_t>(requestFrames));

    CopyableTArray<AudioDataValue> expected;
    size_t expectedSamples =
        expectedSegment.WriteToInterleavedBuffer(expected, channels);

    AudioSegment actualSegment =
        ais->GetAudioSegment(requestFrames, AudioInputSource::Consumer::Same);
    EXPECT_EQ(actualSegment.GetDuration(), requestFrames);
    CopyableTArray<AudioDataValue> actual;
    size_t actualSamples =
        actualSegment.WriteToInterleavedBuffer(actual, channels);

    EXPECT_EQ(actualSamples, expectedSamples);
    EXPECT_EQ(actualSamples / channels, static_cast<size_t>(requestFrames));
    EXPECT_THAT(actual, ContainerEq(expected));
  }

  ais = nullptr;  // Drop the SharedThreadPool here.
}

TEST(TestAudioInputSource, ErrorCallback)
{
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  const AudioInputSource::Id sourceId = 1;
  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;
  const uint32_t channels = 2;
  const PrincipalHandle testPrincipal =
      MakePrincipalHandle(nsContentUtils::GetSystemPrincipal());
  const TrackRate sourceRate = 44100;
  const TrackRate targetRate = 48000;

  auto listener = MakeRefPtr<MockAudioEventListener>();
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Started));
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Error));
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Stopped))
      .Times(2);

  RefPtr<AudioInputSource> ais = MakeRefPtr<AudioInputSource>(
      std::move(listener), sourceId, deviceId, channels, true, testPrincipal,
      sourceRate, targetRate);
  ASSERT_TRUE(ais);

  DispatchFunction([&] {
    ais->Init();
    ais->Start();
  });
  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasInput);
  EXPECT_FALSE(stream->mHasOutput);
  EXPECT_EQ(stream->InputChannels(), channels);

  (void)WaitFor(stream->FramesProcessedEvent());

  DispatchFunction([&] { stream->ForceError(); });
  WaitFor(stream->ErrorForcedEvent());

  DispatchFunction([&] { ais->Stop(); });
  (void)WaitFor(cubeb->StreamDestroyEvent());

  ais = nullptr;  // Drop the SharedThreadPool here.
}

TEST(TestAudioInputSource, DeviceChangedCallback)
{
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  const AudioInputSource::Id sourceId = 1;
  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;
  const uint32_t channels = 2;
  const PrincipalHandle testPrincipal =
      MakePrincipalHandle(nsContentUtils::GetSystemPrincipal());
  const TrackRate sourceRate = 44100;
  const TrackRate targetRate = 48000;

  auto listener = MakeRefPtr<MockAudioEventListener>();
  EXPECT_CALL(*listener, AudioDeviceChanged(sourceId));
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Started));
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Stopped))
      .Times(2);

  RefPtr<AudioInputSource> ais = MakeRefPtr<AudioInputSource>(
      std::move(listener), sourceId, deviceId, channels, true, testPrincipal,
      sourceRate, targetRate);
  ASSERT_TRUE(ais);

  DispatchFunction([&] {
    ais->Init();
    ais->Start();
  });
  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasInput);
  EXPECT_FALSE(stream->mHasOutput);
  EXPECT_EQ(stream->InputChannels(), channels);

  (void)WaitFor(stream->FramesProcessedEvent());

  DispatchFunction([&] { stream->ForceDeviceChanged(); });
  WaitFor(stream->DeviceChangeForcedEvent());

  DispatchFunction([&] { ais->Stop(); });
  (void)WaitFor(cubeb->StreamDestroyEvent());

  ais = nullptr;  // Drop the SharedThreadPool here.
}

TEST(TestAudioInputSource, InputProcessing)
{
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  const AudioInputSource::Id sourceId = 1;
  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;
  const uint32_t channels = 2;
  const PrincipalHandle testPrincipal =
      MakePrincipalHandle(nsContentUtils::GetSystemPrincipal());
  const TrackRate sourceRate = 44100;
  const TrackRate targetRate = 48000;
  using ProcessingPromise =
      AudioInputSource::SetRequestedProcessingParamsPromise;

  auto listener = MakeRefPtr<MockAudioEventListener>();
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Started))
      .Times(0);
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Stopped))
      .Times(10);

  RefPtr<AudioInputSource> ais = MakeRefPtr<AudioInputSource>(
      std::move(listener), sourceId, deviceId, channels, true, testPrincipal,
      sourceRate, targetRate);

  const auto test =
      [&](cubeb_input_processing_params aRequested,
          const Result<cubeb_input_processing_params, int>& aExpected) {
        RefPtr<ProcessingPromise> p;
        DispatchFunction([&] {
          ais->Init();
          p = ais->SetRequestedProcessingParams(aRequested);
        });
        ProcessEventQueue();
        EXPECT_EQ(WaitFor(p), aExpected);

        DispatchFunction([&] { ais->Stop(); });
        (void)WaitFor(cubeb->StreamDestroyEvent());
      };

  // Not supported by backend.
  cubeb->SetSupportedInputProcessingParams(CUBEB_INPUT_PROCESSING_PARAM_NONE,
                                           CUBEB_ERROR_NOT_SUPPORTED);
  test(CUBEB_INPUT_PROCESSING_PARAM_NONE, Err(CUBEB_ERROR_NOT_SUPPORTED));

  // Not supported by params.
  cubeb->SetSupportedInputProcessingParams(CUBEB_INPUT_PROCESSING_PARAM_NONE,
                                           CUBEB_OK);
  test(CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION,
       CUBEB_INPUT_PROCESSING_PARAM_NONE);

  constexpr cubeb_input_processing_params allParams =
      CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
      CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION |
      CUBEB_INPUT_PROCESSING_PARAM_AUTOMATIC_GAIN_CONTROL |
      CUBEB_INPUT_PROCESSING_PARAM_VOICE_ISOLATION;

  // Successful all.
  cubeb->SetSupportedInputProcessingParams(allParams, CUBEB_OK);
  test(allParams, allParams);

  // Successful partial.
  test(CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION,
       CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION);

  // Not supported by stream.
  // Note this also tests that AudioInputSource resets its configured params
  // state from the previous successful test.
  constexpr int propagatedError = 99;
  cubeb->SetInputProcessingApplyRv(propagatedError);
  test(CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION, Err(propagatedError));

  ais = nullptr;  // Drop the SharedThreadPool here.
}
