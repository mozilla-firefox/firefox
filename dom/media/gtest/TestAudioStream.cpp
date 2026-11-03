/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>

#include "AudioStream.h"
#include "CubebUtils.h"
#include "MockCubeb.h"
#include "gtest/gtest.h"
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

}  // namespace

TEST(TestAudioStream, PlaybackRateSwitchesDoNotAccumulateDrift)
{
  constexpr uint32_t rate = 44100;
  constexpr uint32_t channelCount = 2;
  constexpr long framesPerCallback = 256;

  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  TestDataSource source(channelCount);
  auto initPromise = TakeN(cubeb->StreamInitEvent(), 1);
  RefPtr<AudioStream> audioStream =
      new AudioStream(source, rate, channelCount,
                      AudioConfig::ChannelLayout(channelCount).Map());
  ASSERT_EQ(audioStream->Init(nullptr), NS_OK);
  auto [stream] = WaitFor(initPromise).unwrap()[0];
  audioStream->Start();

  source.AppendFrames(500000, 0.5f);

  constexpr int kSwitches = 20;
  constexpr int kStretchCallbacks = 32;
  constexpr int kDrainCallbacks = 32;
  const uint32_t pendingBefore = source.PendingFrames();

  for (int i = 0; i < kSwitches; ++i) {
    ASSERT_EQ(audioStream->SetPlaybackRate(2.0), NS_OK);
    for (int j = 0; j < kStretchCallbacks; ++j) {
      ASSERT_EQ(stream->ManualDataCallback(framesPerCallback),
                MockCubebStream::KeepProcessing::Yes);
    }

    ASSERT_EQ(audioStream->SetPlaybackRate(1.0), NS_OK);
    for (int j = 0; j < kDrainCallbacks; ++j) {
      ASSERT_EQ(stream->ManualDataCallback(framesPerCallback),
                MockCubebStream::KeepProcessing::Yes);
    }
  }

  const uint32_t totalSourceConsumed = pendingBefore - source.PendingFrames();
  const uint32_t expectedSourceConsumed =
      kSwitches * (kStretchCallbacks * framesPerCallback * 2 +
                   kDrainCallbacks * framesPerCallback);
  const int delta = static_cast<int>(totalSourceConsumed) -
                    static_cast<int>(expectedSourceConsumed);

  EXPECT_LE(delta, framesPerCallback)
      << "Source consumed " << totalSourceConsumed << " frames, expected "
      << expectedSourceConsumed << " (delta " << delta << " frames)";

  audioStream->ShutDown();
  stream = nullptr;
  audioStream = nullptr;
  CubebUtils::ForceSetCubebContext(nullptr);
}
