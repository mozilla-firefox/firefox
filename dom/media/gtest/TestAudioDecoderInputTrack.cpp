/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <utility>

#include "AudioDecoderInputTrack.h"
#include "GraphDriver.h"
#include "MediaInfo.h"
#include "MediaTrackGraphImpl.h"
#include "VideoUtils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/gtest/WaitFor.h"
#include "nsThreadUtils.h"

using namespace mozilla;
using namespace mozilla::media;
using testing::AssertionResult;
using testing::NiceMock;
using testing::Return;
using ControlMessageInterface = MediaTrack::ControlMessageInterface;

constexpr uint32_t kNoFlags = 0;
constexpr TrackRate kRate = 44100;
constexpr uint32_t kChannels = 2;

class MockTestGraph : public MediaTrackGraphImpl {
 public:
  explicit MockTestGraph(TrackRate aRate)
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
  ~MockTestGraph() = default;

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

AudioData* CreateAudioDataFromInfo(uint32_t aFrames, const AudioInfo& aInfo) {
  AlignedAudioBuffer samples(aFrames * aInfo.mChannels);
  return new AudioData(0, TimeUnit::Zero(), std::move(samples), aInfo.mChannels,
                       aInfo.mRate);
}

AudioDecoderInputTrack* CreateTrack(MediaTrackGraph* aGraph,
                                    nsISerialEventTarget* aThread,
                                    const AudioInfo& aInfo,
                                    float aPlaybackRate = 1.0,
                                    bool aPreservesPitch = true) {
  return AudioDecoderInputTrack::Create(aGraph, aThread, aInfo, aPlaybackRate,
                                        aPreservesPitch);
}

class TestAudioDecoderInputTrack : public testing::Test {
 protected:
  void SetUp() override {
    mGraph = MakeRefPtr<NiceMock<MockTestGraph>>(kRate);
    mGraph->Init(kChannels);

    mInfo.mRate = kRate;
    mInfo.mChannels = kChannels;
    mTrack = CreateTrack(mGraph, NS_GetCurrentThread(), mInfo);
    EXPECT_FALSE(mTrack->Ended());
  }

  void TearDown() override {
    // This simulates the normal usage where the `Close()` is always be called
    // before the `Destroy()`.
    mTrack->Close();
    mTrack->Destroy();
    // Release the self-reference of mock graph.
    mGraph->Destroy();
  }

  AudioData* CreateAudioData(uint32_t aFrames) {
    return CreateAudioDataFromInfo(aFrames, mInfo);
  }

  AudioSegment* GetTrackSegment() { return mTrack->GetData<AudioSegment>(); }

  AssertionResult ExpectSegmentNonSilence(const char* aStartExpr,
                                          const char* aEndExpr,
                                          TrackTime aStart, TrackTime aEnd) {
    AudioSegment checkedRange;
    checkedRange.AppendSlice(*mTrack->GetData(), aStart, aEnd);
    if (!checkedRange.IsNull()) {
      return testing::AssertionSuccess();
    }
    return testing::AssertionFailure()
           << "segment [" << aStart << ":" << aEnd << "] should be non-silence";
  }

  AssertionResult ExpectSegmentSilence(const char* aStartExpr,
                                       const char* aEndExpr, TrackTime aStart,
                                       TrackTime aEnd) {
    AudioSegment checkedRange;
    checkedRange.AppendSlice(*mTrack->GetData(), aStart, aEnd);
    if (checkedRange.IsNull()) {
      return testing::AssertionSuccess();
    }
    return testing::AssertionFailure()
           << "segment [" << aStart << ":" << aEnd << "] should be silence";
  }

  RefPtr<MockTestGraph> mGraph;
  RefPtr<AudioDecoderInputTrack> mTrack;
  AudioInfo mInfo;
};

TEST_F(TestAudioDecoderInputTrack, BasicAppendData) {
  // Start from [0:10] and each time we move the time by 10ms.
  // Expected: outputDuration=10, outputFrames=0, outputSilence=10
  TrackTime start = 0;
  TrackTime end = 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_EQ(mTrack->GetEnd(), end);
  EXPECT_PRED_FORMAT2(ExpectSegmentSilence, start, end);

  // Expected: outputDuration=20, outputFrames=5, outputSilence=15
  RefPtr<AudioData> audio1 = CreateAudioData(5);
  mTrack->AppendData(audio1, PRINCIPAL_HANDLE_NONE);
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_EQ(mTrack->GetEnd(), end);
  EXPECT_PRED_FORMAT2(ExpectSegmentNonSilence, start, start + audio1->Frames());
  EXPECT_PRED_FORMAT2(ExpectSegmentSilence, start + audio1->Frames(), end);

  // Expected: outputDuration=30, outputFrames=15, outputSilence=15
  RefPtr<AudioData> audio2 = CreateAudioData(10);
  mTrack->AppendData(audio2, PRINCIPAL_HANDLE_NONE);
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentNonSilence, start, end);
  EXPECT_EQ(mTrack->GetEnd(), end);

  // Expected : sent all data, track should be ended in the next iteration and
  // fill slience in this iteration.
  mTrack->NotifyEndOfStream();
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, ProcessedMediaTrack::ALLOW_END);
  EXPECT_PRED_FORMAT2(ExpectSegmentSilence, start, end);
  EXPECT_EQ(mTrack->GetEnd(), end);
  EXPECT_FALSE(mTrack->Ended());

  // Expected : track ended
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, ProcessedMediaTrack::ALLOW_END);
  EXPECT_EQ(mTrack->WrittenFrames(), audio1->Frames() + audio2->Frames());
}

TEST_F(TestAudioDecoderInputTrack, ClearFuture) {
  // Start from [0:10] and each time we move the time by 10ms.
  // Expected: appended=30, expected duration=10
  RefPtr<AudioData> audio1 = CreateAudioData(30);
  mTrack->AppendData(audio1, PRINCIPAL_HANDLE_NONE);
  TrackTime start = 0;
  TrackTime end = 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentNonSilence, start, end);

  // In next iteration [10:20], we would consume the remaining data that was
  // appended in the previous iteration.
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentNonSilence, start, end);

  // Clear future data which is the remaining 10 frames so the track would
  // only output silence.
  mTrack->ClearFutureData();
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentSilence, start, end);

  // Test appending data again, to see if we can append data correctly after
  // calling `ClearFutureData()`.
  RefPtr<AudioData> audio2 = CreateAudioData(10);
  mTrack->AppendData(audio2, PRINCIPAL_HANDLE_NONE);
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentNonSilence, start, end);

  // Run another iteration that should only contains silence because the data
  // we appended only enough for one iteration.
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentSilence, start, end);

  // Clear future data would also remove the EOS.
  mTrack->NotifyEndOfStream();
  mTrack->ClearFutureData();
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, ProcessedMediaTrack::ALLOW_END);
  EXPECT_PRED_FORMAT2(ExpectSegmentSilence, start, end);
  EXPECT_FALSE(mTrack->Ended());

  // As EOS has been removed, in next iteration the track would still be
  // running.
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, ProcessedMediaTrack::ALLOW_END);
  EXPECT_PRED_FORMAT2(ExpectSegmentSilence, start, end);
  EXPECT_FALSE(mTrack->Ended());
  EXPECT_EQ(mTrack->WrittenFrames(),
            (audio1->Frames() - 10 /* got clear */) + audio2->Frames());
}

TEST_F(TestAudioDecoderInputTrack, ClearFutureTimeStretched) {
  RefPtr<AudioDecoderInputTrack> track =
      CreateTrack(mGraph, NS_GetCurrentThread(), mInfo, 0.5, false);
  RefPtr<AudioData> audio = CreateAudioData(4096);
  track->AppendData(audio, PRINCIPAL_HANDLE_NONE);
  track->ProcessInput(0, 10, kNoFlags);

  track->ClearFutureData();
  track->ProcessInput(10, 20, kNoFlags);

  AudioSegment output;
  output.AppendSlice(*track->GetData(), 10, 20);
  EXPECT_TRUE(output.IsNull());

  track->Close();
  track->Destroy();
}

TEST_F(TestAudioDecoderInputTrack, InputRateChange) {
  // Start from [0:10] and each time we move the time by 10ms.
  // Expected: appended=10, expected duration=10
  RefPtr<AudioData> audio1 = CreateAudioData(10);
  mTrack->AppendData(audio1, PRINCIPAL_HANDLE_NONE);
  TrackTime start = 0;
  TrackTime end = 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentNonSilence, start, end);

  // Change input sample rate to the half, input data should be resampled and
  // its duration would become longer.
  // Expected: appended=10 + 5,
  //           expected duration=10 + 5*2 (resampled)
  mInfo.mRate = kRate / 2;
  RefPtr<AudioData> audioHalfSampleRate = CreateAudioData(5);
  mTrack->AppendData(audioHalfSampleRate, PRINCIPAL_HANDLE_NONE);
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentNonSilence, start, end);

  // Change input sample rate to the double, input data should be resampled and
  // its duration would become shorter.
  // Expected: appended=10 + 10 + 10,
  //           expected duration=10 + 10 + 10/2(resampled) + 5(silence)
  mInfo.mRate = kRate * 2;
  RefPtr<AudioData> audioDoubleSampleRate = CreateAudioData(10);
  TrackTime expectedDuration = audioDoubleSampleRate->Frames() / 2;
  mTrack->AppendData(audioDoubleSampleRate, PRINCIPAL_HANDLE_NONE);
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentNonSilence, start, start + expectedDuration);
  EXPECT_PRED_FORMAT2(ExpectSegmentSilence, start + expectedDuration, end);
  EXPECT_EQ(mTrack->WrittenFrames(), audio1->Frames() +
                                         audioHalfSampleRate->Frames() * 2 +
                                         audioDoubleSampleRate->Frames() / 2);
}

TEST_F(TestAudioDecoderInputTrack, ChannelChange) {
  // Start from [0:10] and each time we move the time by 10ms.
  // Track was initialized in stero.
  EXPECT_EQ(mTrack->NumberOfChannels(), uint32_t(2));

  // But first audio data is mono, so the `NumberOfChannels()` changes to
  // reflect the maximum channel in the audio segment.
  mInfo.mChannels = 1;
  RefPtr<AudioData> audioMono = CreateAudioData(10);
  mTrack->AppendData(audioMono, PRINCIPAL_HANDLE_NONE);
  TrackTime start = 0;
  TrackTime end = 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentNonSilence, start, end);
  EXPECT_EQ(mTrack->NumberOfChannels(), audioMono->mChannels);

  // Then append audio data with 5 channels.
  mInfo.mChannels = 5;
  RefPtr<AudioData> audioWithFiveChannels = CreateAudioData(10);
  mTrack->AppendData(audioWithFiveChannels, PRINCIPAL_HANDLE_NONE);
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentNonSilence, start, end);
  EXPECT_EQ(mTrack->NumberOfChannels(), audioWithFiveChannels->mChannels);
  EXPECT_EQ(mTrack->WrittenFrames(),
            audioMono->Frames() + audioWithFiveChannels->Frames());
}

TEST_F(TestAudioDecoderInputTrack, BatchedData) {
  uint32_t appendedFrames = 0;
  RefPtr<AudioData> audio = CreateAudioData(10);
  for (size_t idx = 0; idx < 50; idx++) {
    mTrack->AppendData(audio, PRINCIPAL_HANDLE_NONE);
    appendedFrames += audio->Frames();
  }

  // First we need to call `ProcessInput` at least once to drain the track's
  // SPSC queue, otherwise we're not able to push the batched data later.
  TrackTime start = 0;
  TrackTime end = 10;
  uint32_t expectedFrames = end - start;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentNonSilence, start, end);

  // The batched data would be pushed to the graph thread in around 10ms after
  // the track first time started to batch data, which we can't control here.
  // Therefore, we need to wait until the batched data gets cleared.
  SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
      "TEST_F(TestAudioDecoderInputTrack, BatchedData)"_ns,
      [&] { return !mTrack->HasBatchedData(); });

  // Check that we received all the remainging data previously appended.
  start = end;
  end = start + (appendedFrames - expectedFrames);
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentNonSilence, start, end);

  // Check that we received no more data than previously appended.
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, kNoFlags);
  EXPECT_PRED_FORMAT2(ExpectSegmentSilence, start, end);
  EXPECT_EQ(mTrack->WrittenFrames(), appendedFrames);
}

TEST_F(TestAudioDecoderInputTrack, OutputAndEndEvent) {
  // Append an audio and EOS, the output event should notify the amount of
  // frames that is equal to the amount of audio we appended.
  RefPtr<AudioData> audio = CreateAudioData(10);
  auto outputPromise = TakeN(mTrack->OnOutput(), 1);
  mTrack->AppendData(audio, PRINCIPAL_HANDLE_NONE);
  mTrack->NotifyEndOfStream();
  TrackTime start = 0;
  TrackTime end = 10;
  mTrack->ProcessInput(start, end, ProcessedMediaTrack::ALLOW_END);
  auto output = WaitFor(outputPromise).unwrap()[0];
  EXPECT_EQ(std::get<int64_t>(output), audio->Frames());

  // Track should end in this iteration, so the end event should be notified.
  auto endPromise = TakeN(mTrack->OnEnd(), 1);
  start = end;
  end += 10;
  mTrack->ProcessInput(start, end, ProcessedMediaTrack::ALLOW_END);
  (void)WaitFor(endPromise);
}

TEST_F(TestAudioDecoderInputTrack, PlaybackRateChange) {
  // In order to run the playback change directly without using a real graph.
  // one for setting the track's playback, another for the track destruction.
  EXPECT_CALL(*mGraph, AppendMessage)
      .Times(2)
      .WillOnce(
          [](UniquePtr<ControlMessageInterface> aMessage) { aMessage->Run(); })
      .WillOnce([](UniquePtr<ControlMessageInterface> aMessage) {});

  // Changing the playback rate.
  float expectedPlaybackRate = 2.0;
  mTrack->SetPlaybackRate(expectedPlaybackRate);
  SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
      "TEST_F(TestAudioDecoderInputTrack, PlaybackRateChange)"_ns,
      [&] { return mTrack->PlaybackRate() == expectedPlaybackRate; });

  // Time stretcher in the track would usually need certain amount of data
  // before it outputs the time-stretched result. As we're in testing, we would
  // only append data once, so signal an EOS after appending data, in order to
  // ask the track to flush all samples from the time strecther.
  RefPtr<AudioData> audio = CreateAudioData(100);
  mTrack->AppendData(audio, PRINCIPAL_HANDLE_NONE);
  mTrack->NotifyEndOfStream();

  // Playback rate is 2x, so we should only get 1/2x sample frames, another 1/2
  // should be silence. Output should not be rate-aware.
  auto outputPromise = TakeN(mTrack->OnOutput(), 1);
  TrackTime start = 0;
  TrackTime end = audio->Frames();
  mTrack->ProcessInput(start, end, kNoFlags);
  auto output = WaitFor(outputPromise).unwrap()[0];
  EXPECT_EQ(std::get<int64_t>(output), audio->Frames());
  EXPECT_PRED_FORMAT2(ExpectSegmentNonSilence, start, audio->Frames() / 2);
  EXPECT_PRED_FORMAT2(ExpectSegmentSilence, start + audio->Frames() / 2, end);
}

// The transpose this exercises asks SoundTouch for millions of samples, which
// a wasm32 sandbox cannot grow to hold on 32-bit builds.
#ifdef HAVE_64BIT_BUILD
TEST(AudioDecoderInputTrack, LimitsLargeRateTransposeOutput)
{
  constexpr TrackRate graphRate = 48000;
  constexpr uint32_t inputFrames = 700000;
  // The MAX_DEST_LIMIT defined in libsoundtouch's TransposerBase::transpose
  constexpr uint32_t maxDestLimit = 10240000;

  RefPtr<MockTestGraph> graph = MakeRefPtr<NiceMock<MockTestGraph>>(graphRate);
  graph->Init(1);

  AudioInfo info;
  info.mRate = graphRate;
  info.mChannels = 1;
  RefPtr<AudioDecoderInputTrack> track =
      CreateTrack(graph, NS_GetCurrentThread(), info, 0.0625, false);
  RefPtr<AudioData> audio = CreateAudioDataFromInfo(inputFrames, info);
  track->AppendData(audio, PRINCIPAL_HANDLE_NONE);

  // Tear the graph down even if an assertion below returns early, so a failure
  // here cannot leave the MediaTrackGraph running into later tests.
  auto cleanup = MakeScopeExit([&] {
    track->Close();
    track->Destroy();
    graph->Destroy();
  });

  track->ProcessInput(0, 1, kNoFlags);
  const uint32_t timeStretcherSamples = track->TimeStretcherSamplesForTesting();
  ASSERT_GT(timeStretcherSamples, 0u);
  EXPECT_LT(timeStretcherSamples, maxDestLimit);
}
#endif
