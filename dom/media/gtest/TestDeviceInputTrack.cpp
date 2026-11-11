/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "AudioGenerator.h"
#include "DeviceInputTrack.h"
#include "MediaTrackGraphImpl.h"
#include "MockCubeb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/gtest/WaitFor.h"
#include "nsContentUtils.h"

using namespace mozilla;
using testing::NiceMock;
using testing::Return;

namespace {
#define DispatchFunction(f) \
  NS_DispatchToCurrentThread(NS_NewRunnableFunction(__func__, f))
}  // namespace

class MockGraphImpl : public MediaTrackGraphImpl {
 public:
  explicit MockGraphImpl(TrackRate aRate)
      : MediaTrackGraphImpl(0, aRate, nullptr, AbstractThread::GetCurrent()) {
    ON_CALL(*this, OnGraphThread).WillByDefault(Return(true));
  }

  void Init(uint32_t aChannels) {
    MediaTrackGraphImpl::Init(OFFLINE_THREAD_DRIVER, DIRECT_DRIVER, aChannels);
    // We have to call `Destroy()` manually in order to break the reference.
    // The reason we don't assign a null driver is because we would add a track
    // to the graph, then it would trigger graph's `EnsureNextIteration()` that
    // requires a non-null driver.
    SetCurrentDriver(new NiceMock<MockDriver>());
  }

  MOCK_CONST_METHOD0(OnGraphThread, bool());
  MOCK_METHOD1(AppendMessage, void(UniquePtr<ControlMessageInterface>));

 protected:
  ~MockGraphImpl() = default;

  class MockDriver : public GraphDriver {
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MockDriver, override);

    MockDriver() : GraphDriver(nullptr, nullptr, 0) {
      ON_CALL(*this, OnThread).WillByDefault(Return(true));
      ON_CALL(*this, ThreadRunning).WillByDefault(Return(true));
    }

    MOCK_METHOD0(Start, void());
    MOCK_METHOD0(Shutdown, void());
    MOCK_METHOD0(IterationDuration, TimeDuration());
    MOCK_METHOD0(EnsureNextIteration, void());
    MOCK_CONST_METHOD0(OnThread, bool());
    MOCK_CONST_METHOD0(ThreadRunning, bool());

   protected:
    ~MockDriver() = default;
  };
};

class TestDeviceInputTrack : public testing::Test {
 protected:
  TestDeviceInputTrack() : mChannels(2), mRate(44100) {}

  void SetUp() override {
    mGraph = MakeRefPtr<NiceMock<MockGraphImpl>>(mRate);
    mGraph->Init(mChannels);
  }

  void TearDown() override { mGraph->Destroy(); }

  const uint32_t mChannels;
  const TrackRate mRate;
  RefPtr<MockGraphImpl> mGraph;
};

TEST_F(TestDeviceInputTrack, DeviceInputConsumerTrack) {
  class TestDeviceInputConsumerTrack : public DeviceInputConsumerTrack {
   public:
    static TestDeviceInputConsumerTrack* Create(MediaTrackGraph* aGraph) {
      MOZ_RELEASE_ASSERT(NS_IsMainThread());
      TestDeviceInputConsumerTrack* track =
          new TestDeviceInputConsumerTrack(aGraph->GraphRate());
      aGraph->AddTrack(track, {});
      return track;
    }

    void Destroy() {
      MOZ_RELEASE_ASSERT(NS_IsMainThread());
      DisconnectDeviceInput();
      DeviceInputConsumerTrack::Destroy();
    }

    void ProcessInput(GraphTime aFrom, GraphTime aTo,
                      uint32_t aFlags) override { /* Ignored */ };

    uint32_t NumberOfChannels() const override {
      if (mInputs.IsEmpty()) {
        return 0;
      }
      DeviceInputTrack* t = mInputs[0]->GetSource()->AsDeviceInputTrack();
      MOZ_RELEASE_ASSERT(t);
      return t->NumberOfChannels();
    }

   private:
    explicit TestDeviceInputConsumerTrack(TrackRate aSampleRate)
        : DeviceInputConsumerTrack(aSampleRate) {}
  };

  class TestAudioDataListener : public AudioDataListener {
   public:
    TestAudioDataListener(uint32_t aChannelCount, bool aIsVoice)
        : mChannelCount(aChannelCount), mIsVoice(aIsVoice) {}
    // Graph thread APIs: AudioDataListenerInterface implementations.
    uint32_t RequestedInputChannelCount(
        MediaTrackGraph* aGraph) const override {
      aGraph->AssertOnGraphThread();
      return mChannelCount;
    }
    cubeb_input_processing_params RequestedInputProcessingParams(
        MediaTrackGraph*) const override {
      return CUBEB_INPUT_PROCESSING_PARAM_NONE;
    }
    bool IsVoiceInput(MediaTrackGraph* aGraph) const override {
      return mIsVoice;
    };
    void DeviceChanged(MediaTrackGraph* aGraph) override { /* Ignored */ }
    void Disconnect(MediaTrackGraph* aGraph) override { /* Ignored */ };
    void NotifySetRequestedInputProcessingParams(
        MediaTrackGraph* aGraph, int aGeneration,
        cubeb_input_processing_params aRequestedParams) override {
      /* Ignored */
    }
    void NotifySetRequestedInputProcessingParamsResult(
        MediaTrackGraph* aGraph, int aGeneration,
        const Result<cubeb_input_processing_params, int>& aResult) override {
      /* Ignored */
    }

   private:
    ~TestAudioDataListener() = default;

    // Graph thread-only.
    uint32_t mChannelCount;
    // Any thread.
    const bool mIsVoice;
  };

  const PrincipalHandle testPrincipal =
      MakePrincipalHandle(nsContentUtils::GetSystemPrincipal());

  const CubebUtils::AudioDeviceID device1 = (void*)1;
  RefPtr<TestAudioDataListener> listener1 = new TestAudioDataListener(1, false);
  RefPtr<TestDeviceInputConsumerTrack> track1 =
      TestDeviceInputConsumerTrack::Create(mGraph);
  track1->ConnectDeviceInput(device1, listener1.get(), testPrincipal);
  EXPECT_TRUE(track1->ConnectedToNativeDevice());
  EXPECT_FALSE(track1->ConnectedToNonNativeDevice());

  const CubebUtils::AudioDeviceID device2 = (void*)2;
  RefPtr<TestAudioDataListener> listener2 = new TestAudioDataListener(2, false);
  RefPtr<TestDeviceInputConsumerTrack> track2 =
      TestDeviceInputConsumerTrack::Create(mGraph);
  track2->ConnectDeviceInput(device2, listener2.get(), testPrincipal);
  EXPECT_FALSE(track2->ConnectedToNativeDevice());
  EXPECT_TRUE(track2->ConnectedToNonNativeDevice());

  track2->Destroy();
  track1->Destroy();
}

TEST_F(TestDeviceInputTrack, NativeInputTrackData) {
  const uint32_t flags = 0;
  const CubebUtils::AudioDeviceID deviceId = (void*)1;

  AudioGenerator<AudioDataValue> generator(mChannels, mRate);
  const size_t nrFrames = 10;
  const size_t bufferSize = nrFrames * mChannels;
  nsTArray<AudioDataValue> buffer(bufferSize);
  buffer.AppendElements(bufferSize);

  const PrincipalHandle testPrincipal =
      MakePrincipalHandle(nsContentUtils::GetSystemPrincipal());

  // Setup: Create a NativeInputTrack and add it to mGraph
  RefPtr<NativeInputTrack> track =
      new NativeInputTrack(mGraph->GraphRate(), deviceId, testPrincipal);
  mGraph->AddTrack(track, {});

  // Main test below:

  generator.GenerateInterleaved(buffer.Elements(), nrFrames);
  track->NotifyInputData(mGraph.get(), buffer.Elements(), nrFrames, mRate,
                         mChannels, 0);

  track->ProcessInput(0, WEBAUDIO_BLOCK_SIZE + nrFrames, flags);
  EXPECT_EQ(static_cast<size_t>(track->GetEnd()),
            static_cast<size_t>(WEBAUDIO_BLOCK_SIZE) + nrFrames);

  // Check pre-buffering: null data with PRINCIPAL_HANDLE_NONE principal
  AudioSegment preBuffering;
  preBuffering.AppendSlice(*track->GetData(), 0, WEBAUDIO_BLOCK_SIZE);
  EXPECT_TRUE(preBuffering.IsNull());
  for (AudioSegment::ConstChunkIterator iter(preBuffering); !iter.IsEnded();
       iter.Next()) {
    const AudioChunk& chunk = *iter;
    EXPECT_EQ(chunk.mPrincipalHandle, PRINCIPAL_HANDLE_NONE);
  }

  // Check rest of the data
  AudioSegment data;
  data.AppendSlice(*track->GetData(), WEBAUDIO_BLOCK_SIZE,
                   WEBAUDIO_BLOCK_SIZE + nrFrames);
  nsTArray<AudioDataValue> interleaved;
  size_t sampleCount = data.WriteToInterleavedBuffer(interleaved, mChannels);
  EXPECT_EQ(sampleCount, bufferSize);
  EXPECT_EQ(interleaved, buffer);

  // Check principal in data
  for (AudioSegment::ConstChunkIterator iter(data); !iter.IsEnded();
       iter.Next()) {
    const AudioChunk& chunk = *iter;
    EXPECT_EQ(chunk.mPrincipalHandle, testPrincipal);
  }

  // Tear down: Destroy the NativeInputTrack.
  track->Destroy();
}

class MockEventListener : public AudioInputSource::EventListener {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MockEventListener, override);
  MOCK_METHOD1(AudioDeviceChanged, void(AudioInputSource::Id));
  MOCK_METHOD2(AudioStateCallback,
               void(AudioInputSource::Id,
                    AudioInputSource::EventListener::State));

 private:
  ~MockEventListener() = default;
};

TEST_F(TestDeviceInputTrack, StartAndStop) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  // Non native input settings
  const AudioInputSource::Id sourceId = 1;
  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;
  const uint32_t channels = 2;
  const PrincipalHandle testPrincipal =
      MakePrincipalHandle(nsContentUtils::GetSystemPrincipal());
  const TrackRate rate = 48000;

  // Setup: Create a NonNativeInputTrack and add it to mGraph.
  RefPtr<NonNativeInputTrack> track =
      new NonNativeInputTrack(mGraph->GraphRate(), deviceId, testPrincipal);
  mGraph->AddTrack(track, {});

  // Main test below:

  // Make sure the NonNativeInputTrack can start and stop its audio correctly.
  {
    auto listener = MakeRefPtr<MockEventListener>();
    EXPECT_CALL(*listener,
                AudioStateCallback(
                    sourceId, AudioInputSource::EventListener::State::Started));
    EXPECT_CALL(*listener,
                AudioStateCallback(
                    sourceId, AudioInputSource::EventListener::State::Stopped))
        .Times(2);

    // No input channels and device preference before start.
    EXPECT_EQ(track->NumberOfChannels(), 0U);
    EXPECT_EQ(track->DevicePreference(), AudioInputType::Unknown);

    DispatchFunction([&] {
      track->StartAudio(MakeRefPtr<AudioInputSource>(
          std::move(listener), sourceId, deviceId, channels, true /* voice */,
          testPrincipal, rate, mGraph->GraphRate()));
    });

    // Wait for stream creation.
    RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());

    // Make sure the audio stream and the track's settings are correct.
    EXPECT_TRUE(stream->mHasInput);
    EXPECT_FALSE(stream->mHasOutput);
    EXPECT_EQ(stream->GetInputDeviceID(), deviceId);
    EXPECT_EQ(stream->InputChannels(), channels);
    EXPECT_EQ(stream->SampleRate(), static_cast<uint32_t>(rate));
    EXPECT_EQ(track->NumberOfChannels(), channels);
    EXPECT_EQ(track->DevicePreference(), AudioInputType::Voice);

    // Wait for stream callbacks.
    (void)WaitFor(stream->FramesProcessedEvent());

    DispatchFunction([&] { track->StopAudio(); });

    // Wait for stream destroy.
    (void)WaitFor(cubeb->StreamDestroyEvent());

    // No input channels and device preference after stop.
    EXPECT_EQ(track->NumberOfChannels(), 0U);
    EXPECT_EQ(track->DevicePreference(), AudioInputType::Unknown);
  }

  // Make sure the NonNativeInputTrack can restart its audio correctly.
  {
    auto listener = MakeRefPtr<MockEventListener>();
    EXPECT_CALL(*listener,
                AudioStateCallback(
                    sourceId, AudioInputSource::EventListener::State::Started));
    EXPECT_CALL(*listener,
                AudioStateCallback(
                    sourceId, AudioInputSource::EventListener::State::Stopped))
        .Times(2);

    DispatchFunction([&] {
      track->StartAudio(MakeRefPtr<AudioInputSource>(
          std::move(listener), sourceId, deviceId, channels, true,
          testPrincipal, rate, mGraph->GraphRate()));
    });

    // Wait for stream creation.
    RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
    EXPECT_TRUE(stream->mHasInput);
    EXPECT_FALSE(stream->mHasOutput);
    EXPECT_EQ(stream->GetInputDeviceID(), deviceId);
    EXPECT_EQ(stream->InputChannels(), channels);
    EXPECT_EQ(stream->SampleRate(), static_cast<uint32_t>(rate));

    // Wait for stream callbacks.
    (void)WaitFor(stream->FramesProcessedEvent());

    DispatchFunction([&] { track->StopAudio(); });

    // Wait for stream destroy.
    (void)WaitFor(cubeb->StreamDestroyEvent());
  }

  // Tear down: Destroy the NativeInputTrack.
  track->Destroy();
}

TEST_F(TestDeviceInputTrack, NonNativeInputTrackData) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  // Graph settings
  const uint32_t flags = 0;
  const GraphTime frames = 440;

  // Non native input settings
  const AudioInputSource::Id sourceId = 1;
  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;
  const uint32_t channels = 2;
  const PrincipalHandle testPrincipal =
      MakePrincipalHandle(nsContentUtils::GetSystemPrincipal());
  const TrackRate rate = 48000;

  // Setup: Create a NonNativeInputTrack and add it to mGraph.
  RefPtr<NonNativeInputTrack> track =
      new NonNativeInputTrack(mGraph->GraphRate(), deviceId, testPrincipal);
  mGraph->AddTrack(track, {});

  // Main test below:

  // Make sure we get null data if the track is not started yet.
  GraphTime current = 0;
  GraphTime next = MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(frames);
  ASSERT_NE(current, next);  // Make sure we have data produced in ProcessInput.

  track->ProcessInput(current, next, flags);
  {
    AudioSegment data;
    data.AppendSegment(track->GetData<AudioSegment>());
    EXPECT_TRUE(data.IsNull());
  }

  // Make sure we get the AudioInputSource's data once we start the track.

  current = next;
  next = MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(2 * frames);
  ASSERT_NE(current, next);  // Make sure we have data produced in ProcessInput.

  auto listener = MakeRefPtr<MockEventListener>();
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Started));
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Stopped))
      .Times(2);

  DispatchFunction([&] {
    track->StartAudio(MakeRefPtr<AudioInputSource>(
        std::move(listener), sourceId, deviceId, channels, true, testPrincipal,
        rate, mGraph->GraphRate()));
  });
  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasInput);
  EXPECT_FALSE(stream->mHasOutput);
  EXPECT_EQ(stream->GetInputDeviceID(), deviceId);
  EXPECT_EQ(stream->InputChannels(), channels);
  EXPECT_EQ(stream->SampleRate(), static_cast<uint32_t>(rate));

  // Check audio data.
  (void)WaitFor(stream->FramesProcessedEvent());
  track->ProcessInput(current, next, flags);
  {
    AudioSegment data;
    data.AppendSlice(*track->GetData<AudioSegment>(), current, next);
    EXPECT_FALSE(data.IsNull());
    for (AudioSegment::ConstChunkIterator iter(data); !iter.IsEnded();
         iter.Next()) {
      EXPECT_EQ(iter->mChannelData.Length(), channels);
      EXPECT_EQ(iter->mPrincipalHandle, testPrincipal);
    }
  }

  // Stop the track and make sure it produces null data again.
  current = next;
  next = MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(3 * frames);
  ASSERT_NE(current, next);  // Make sure we have data produced in ProcessInput.

  DispatchFunction([&] { track->StopAudio(); });
  (void)WaitFor(cubeb->StreamDestroyEvent());

  track->ProcessInput(current, next, flags);
  {
    AudioSegment data;
    data.AppendSlice(*track->GetData<AudioSegment>(), current, next);
    EXPECT_TRUE(data.IsNull());
  }

  // Tear down: Destroy the NonNativeInputTrack.
  track->Destroy();
}

TEST_F(TestDeviceInputTrack, NonNativeDeviceChangedCallback) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  // Non native input settings
  const AudioInputSource::Id sourceId = 1;
  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;
  const uint32_t channels = 2;
  const PrincipalHandle testPrincipal =
      MakePrincipalHandle(nsContentUtils::GetSystemPrincipal());
  const TrackRate rate = 48000;

  // Setup: Create a NonNativeInputTrack and add it to mGraph.
  RefPtr<NonNativeInputTrack> track =
      new NonNativeInputTrack(mGraph->GraphRate(), deviceId, testPrincipal);
  mGraph->AddTrack(track, {});

  // Main test below:

  auto listener = MakeRefPtr<MockEventListener>();
  EXPECT_CALL(*listener, AudioDeviceChanged(sourceId));
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Started));
  EXPECT_CALL(*listener,
              AudioStateCallback(
                  sourceId, AudioInputSource::EventListener::State::Stopped))
      .Times(2);

  // Launch and start an audio stream.
  DispatchFunction([&] {
    track->StartAudio(MakeRefPtr<AudioInputSource>(
        std::move(listener), sourceId, deviceId, channels, true, testPrincipal,
        rate, mGraph->GraphRate()));
  });
  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasInput);
  EXPECT_FALSE(stream->mHasOutput);
  EXPECT_EQ(stream->GetInputDeviceID(), deviceId);
  EXPECT_EQ(stream->InputChannels(), channels);
  EXPECT_EQ(stream->SampleRate(), static_cast<uint32_t>(rate));

  // Make sure the stream is running.
  (void)WaitFor(stream->FramesProcessedEvent());

  // Fire a device-changed callback.
  DispatchFunction([&] { stream->ForceDeviceChanged(); });
  WaitFor(stream->DeviceChangeForcedEvent());

  // Stop and destroy the stream.
  DispatchFunction([&] { track->StopAudio(); });
  (void)WaitFor(cubeb->StreamDestroyEvent());

  // Tear down: Destroy the NonNativeInputTrack.
  track->Destroy();
}

TEST_F(TestDeviceInputTrack, NonNativeErrorCallback) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  // Non native input settings
  const AudioInputSource::Id sourceId = 1;
  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;
  const uint32_t channels = 2;
  const PrincipalHandle testPrincipal =
      MakePrincipalHandle(nsContentUtils::GetSystemPrincipal());
  const TrackRate rate = 48000;

  // Setup: Create a NonNativeInputTrack and add it to mGraph.
  RefPtr<NonNativeInputTrack> track =
      new NonNativeInputTrack(mGraph->GraphRate(), deviceId, testPrincipal);
  mGraph->AddTrack(track, {});

  // Main test below:

  auto listener = MakeRefPtr<MockEventListener>();
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

  // Launch and start an audio stream.
  DispatchFunction([&] {
    track->StartAudio(MakeRefPtr<AudioInputSource>(
        std::move(listener), sourceId, deviceId, channels, true, testPrincipal,
        rate, mGraph->GraphRate()));
  });
  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasInput);
  EXPECT_FALSE(stream->mHasOutput);
  EXPECT_EQ(stream->GetInputDeviceID(), deviceId);
  EXPECT_EQ(stream->InputChannels(), channels);
  EXPECT_EQ(stream->SampleRate(), static_cast<uint32_t>(rate));

  // Make sure the stream is running.
  (void)WaitFor(stream->FramesProcessedEvent());

  // Force an error in the MockCubeb.
  DispatchFunction([&] { stream->ForceError(); });
  WaitFor(stream->ErrorForcedEvent());

  // Stop and destroy the stream.
  DispatchFunction([&] { track->StopAudio(); });
  (void)WaitFor(cubeb->StreamDestroyEvent());

  // Tear down: Destroy the NonNativeInputTrack.
  track->Destroy();
}
