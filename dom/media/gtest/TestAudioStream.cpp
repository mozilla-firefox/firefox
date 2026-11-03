/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <utility>

#include "AudioStream.h"
#include "CubebUtils.h"
#include "MockCubeb.h"
#include "gtest/gtest.h"
#include "mozilla/Maybe.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/gtest/WaitFor.h"

using namespace mozilla;

namespace {

class TestDataSource final : public AudioStream::DataSource {
 public:
  explicit TestDataSource(uint32_t aChannelCount)
      : mChannelCount(aChannelCount) {}

  void AppendFrames(uint32_t aFrames, AudioDataValue aValue) {
    const size_t oldLength = mSamples.Length();
    const size_t sampleCount = static_cast<size_t>(aFrames) * mChannelCount;
    mSamples.SetLength(oldLength + sampleCount);
    std::fill_n(mSamples.Elements() + oldLength, sampleCount, aValue);
  }

  uint32_t PendingFrames() const {
    return (mSamples.Length() - mReadOffset) / mChannelCount;
  }

  uint32_t PopFrames(AudioDataValue* aAudio, uint32_t aFrames, bool) override {
    const uint32_t toCopyFrames = std::min(aFrames, PendingFrames());
    if (!toCopyFrames) {
      return 0;
    }

    const size_t toCopySamples =
        static_cast<size_t>(toCopyFrames) * mChannelCount;
    PodCopy(aAudio, mSamples.Elements() + mReadOffset, toCopySamples);
    mReadOffset += toCopySamples;
    return toCopyFrames;
  }

  bool Ended() const override { return false; }

 private:
  const uint32_t mChannelCount;
  nsTArray<AudioDataValue> mSamples;
  size_t mReadOffset = 0;
};

constexpr uint32_t kRate = 44100;
constexpr uint32_t kChannelCount = 2;
constexpr uint32_t kFramesPerCallback = 256;
constexpr int kSwitches = 20;
constexpr int kStretchCallbacks = 32;
constexpr int kDrainCallbacks = 32;
constexpr double kSlowRate = 0.5;
constexpr double kFastRate = 1.5;
constexpr double kMaxRate = 2.0;
constexpr double kQuickToggleRate = 1.25;

// The time stretcher processing window, from the default sequence and seek
// window prefs: (10 + 15) * 44100 / 1000 = 1102 frames.
constexpr uint32_t kSequenceMs = 10;
constexpr uint32_t kSeekWindowMs = 15;
constexpr uint32_t kWindowFrames = (kSequenceMs + kSeekWindowMs) * kRate / 1000;

// A 1x period shorter than one time stretcher window, so the flushed output
// has not played out when the rate changes again.
constexpr int kShortDrainCallbacks = 1;
static_assert(kShortDrainCallbacks * kFramesPerCallback < kWindowFrames);

// No cycle runs more than 96 callbacks, and the fastest rate pops 2 source
// frames per output frame: 20 * 96 * 256 * 2 = 983040 frames, plus one
// window for the time stretcher startup.
constexpr uint32_t kMaxCallbacksPerCycle = 96;
static_assert(kStretchCallbacks + kDrainCallbacks <= kMaxCallbacksPerCycle);
static_assert(3 * (kStretchCallbacks / 2) + kShortDrainCallbacks +
                  kDrainCallbacks <=
              kMaxCallbacksPerCycle);
static_assert(kSlowRate <= kMaxRate && kFastRate <= kMaxRate &&
              kQuickToggleRate <= kMaxRate);
static_assert(kMaxRate == static_cast<uint32_t>(kMaxRate));
constexpr uint32_t kSourceFrames = kSwitches * kMaxCallbacksPerCycle *
                                       kFramesPerCallback *
                                       static_cast<uint32_t>(kMaxRate) +
                                   kWindowFrames;

// The playback rate used between the returns to 1x, and whether the pitch is
// preserved.
using RateSwitchParam = std::tuple<double, bool>;

class TestAudioStreamRateSwitch
    : public ::testing::TestWithParam<RateSwitchParam> {
 protected:
  void SetUp() override {
    ASSERT_EQ(StaticPrefs::media_audio_playbackrate_soundtouch_sequence_ms(),
              static_cast<int32_t>(kSequenceMs));
    ASSERT_EQ(StaticPrefs::media_audio_playbackrate_soundtouch_seekwindow_ms(),
              static_cast<int32_t>(kSeekWindowMs));

    mCubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
    CubebUtils::ForceSetCubebContext(mCubeb->AsCubebContext());

    auto initPromise = TakeN(mCubeb->StreamInitEvent(), 1);
    mAudioStream =
        new AudioStream(mSource, kRate, kChannelCount,
                        AudioConfig::ChannelLayout(kChannelCount).Map());
    ASSERT_EQ(mAudioStream->Init(nullptr), NS_OK);
    std::tie(mStream) = WaitFor(initPromise).unwrap()[0];
    mAudioStream->Start();

    mSource.AppendFrames(kSourceFrames, 0.5f);
    mPendingAtStart = mSource.PendingFrames();
    ASSERT_EQ(mAudioStream->SetPreservesPitch(std::get<1>(GetParam())), NS_OK);
  }

  void TearDown() override {
    if (!mAudioStream) {
      CubebUtils::ForceSetCubebContext(nullptr);
      return;
    }
    if (!HasFatalFailure()) {
      EXPECT_GT(mSource.PendingFrames(), 0u) << "the stream underran";
    }
    mAudioStream->ShutDown();
    mStream = nullptr;
    mAudioStream = nullptr;
    CubebUtils::ForceSetCubebContext(nullptr);
  }

  void StartCycle() {
    mCycleStartConsumed = SourceFramesConsumed();
    mCycleNominalSource = 0;
    mCycleReturnsTo1x = 0;
  }

  // Run audio callbacks at |aRate|. After each one, the clock must not move
  // back, and must not report a position past the source taken from the data
  // source so far; one frame covers the conversion of the position to
  // microseconds and back.
  void RunCallbacks(double aRate, int aCount) {
    if (aRate == 1.0 && mRate != 1.0) {
      ++mCycleReturnsTo1x;
      ++mRunReturnsTo1x;
    }
    mRate = aRate;
    ASSERT_EQ(mAudioStream->SetPlaybackRate(aRate), NS_OK);
    for (int i = 0; i < aCount; ++i) {
      ASSERT_EQ(mStream->ManualDataCallback(kFramesPerCallback),
                MockCubebStream::KeepProcessing::Yes);
      const int64_t position = mAudioStream->GetPositionInFrames();
      ASSERT_GE(position, mLastPosition);
      ASSERT_LE(position, static_cast<int64_t>(SourceFramesConsumed()) + 1);
      mLastPosition = position;
    }
    mCycleNominalSource += aCount * kFramesPerCallback * aRate;
    mRunNominalSource += aCount * kFramesPerCallback * aRate;
  }

  // Check the state after a cycle that ended with a full 1x period.
  void CheckCycle(int aCycle) {
    const uint32_t consumed = SourceFramesConsumed() - mCycleStartConsumed;
    const int64_t position = mAudioStream->GetPositionInFrames();
    ASSERT_GE(position, 0) << "cycle " << aCycle;
    const int64_t lag = static_cast<int64_t>(SourceFramesConsumed()) - position;
    // A cleared time stretcher drops a few frames of source.
    EXPECT_LT(lag, static_cast<int64_t>(kFramesPerCallback))
        << "cycle " << aCycle;
    EXPECT_LE(static_cast<double>(consumed),
              mCycleNominalSource + kFramesPerCallback)
        << "cycle " << aCycle;
    // Each return to 1x can take less source than the rates ask for, because
    // the primed silence and the flush padding replace up to one window of
    // source each. The largest measured deficit is 1716 frames per return, at
    // 0.5x with the pitch preserved.
    EXPECT_GE(static_cast<double>(consumed),
              mCycleNominalSource - 2.0 * kWindowFrames * mCycleReturnsTo1x)
        << "cycle " << aCycle;
    // The first cycle starts from a new time stretcher, and every later one
    // from a cleared one.
    if (aCycle == 0) {
      return;
    }
    if (!mReference) {
      mReference = Some(std::make_pair(consumed, lag));
    }
    EXPECT_EQ(consumed, mReference->first) << "cycle " << aCycle;
    EXPECT_EQ(lag, mReference->second) << "cycle " << aCycle;
  }

  // The bounds of CheckCycle also hold for the whole run, so a small excess
  // or deficit per cycle cannot add up.
  void CheckRun() {
    const double consumed = SourceFramesConsumed();
    EXPECT_LE(consumed, mRunNominalSource + kFramesPerCallback)
        << "consumed " << consumed << ", nominal " << mRunNominalSource;
    EXPECT_GE(consumed,
              mRunNominalSource - 2.0 * kWindowFrames * mRunReturnsTo1x)
        << "consumed " << consumed << ", nominal " << mRunNominalSource;
  }

  uint32_t SourceFramesConsumed() const {
    return mPendingAtStart - mSource.PendingFrames();
  }

  TestDataSource mSource{kChannelCount};
  MockCubeb* mCubeb = nullptr;
  RefPtr<AudioStream> mAudioStream;
  RefPtr<SmartMockCubebStream> mStream;
  uint32_t mPendingAtStart = 0;
  uint32_t mCycleStartConsumed = 0;
  // The source the rates of the current cycle, and of the whole run, ask for.
  double mCycleNominalSource = 0;
  double mRunNominalSource = 0;
  int mCycleReturnsTo1x = 0;
  int mRunReturnsTo1x = 0;
  double mRate = 1.0;
  int64_t mLastPosition = 0;
  // The source consumed by the second cycle, and the clock lag after it.
  Maybe<std::pair<uint32_t, int64_t>> mReference;
};

TEST_P(TestAudioStreamRateSwitch, ClockFollowsSourceAcrossReturnsTo1x) {
  const double playbackRate = std::get<0>(GetParam());
  for (int i = 0; i < kSwitches; ++i) {
    StartCycle();

    // Play at the other rate, then return to 1x long enough for the time
    // stretcher to play out what it still holds.
    ASSERT_NO_FATAL_FAILURE(RunCallbacks(playbackRate, kStretchCallbacks));
    ASSERT_NO_FATAL_FAILURE(RunCallbacks(1.0, kDrainCallbacks));

    CheckCycle(i);
  }
  CheckRun();
}

TEST_P(TestAudioStreamRateSwitch, ClockFollowsSourceAcrossQuickReturnsTo1x) {
  const double playbackRate = std::get<0>(GetParam());
  for (int i = 0; i < kSwitches; ++i) {
    StartCycle();

    // Change between two other rates, then return to 1x too briefly for the
    // time stretcher to play out what it holds, so the next rate change
    // starts with flushed output still in it.
    ASSERT_NO_FATAL_FAILURE(RunCallbacks(playbackRate, kStretchCallbacks / 2));
    ASSERT_NO_FATAL_FAILURE(
        RunCallbacks(kQuickToggleRate, kStretchCallbacks / 2));
    ASSERT_NO_FATAL_FAILURE(RunCallbacks(1.0, kShortDrainCallbacks));
    ASSERT_NO_FATAL_FAILURE(RunCallbacks(playbackRate, kStretchCallbacks / 2));
    ASSERT_NO_FATAL_FAILURE(RunCallbacks(1.0, kDrainCallbacks));

    CheckCycle(i);
  }
  CheckRun();
}

INSTANTIATE_TEST_SUITE_P(
    TestAudioStream, TestAudioStreamRateSwitch,
    ::testing::Combine(::testing::Values(kSlowRate, kFastRate, kMaxRate),
                       ::testing::Bool()),
    [](const ::testing::TestParamInfo<RateSwitchParam>& aInfo) {
      return "Rate" +
             std::to_string(std::lround(std::get<0>(aInfo.param) * 100)) +
             (std::get<1>(aInfo.param) ? "_PreservesPitch"
                                       : "_NoPreservesPitch");
    });

}  // namespace
