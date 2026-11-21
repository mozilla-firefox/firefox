/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_GTEST_AUDIOVERIFIER_H_
#define DOM_MEDIA_GTEST_AUDIOVERIFIER_H_

#include "AudioGenerator.h"

namespace mozilla {

template <typename Sample>
class AudioVerifier {
 public:
  explicit AudioVerifier(uint32_t aRate, uint32_t aFrequency)
      : mRate(aRate), mFrequency(aFrequency) {}

  // Only the mono channel is taken into account.
  void AppendData(const AudioSegment& segment) {
    for (AudioSegment::ConstChunkIterator iter(segment); !iter.IsEnded();
         iter.Next()) {
      const AudioChunk& c = *iter;
      if (c.IsNull()) {
        for (int i = 0; i < c.GetDuration(); ++i) {
          CheckSample(0);
        }
      } else {
        const Sample* buffer = c.ChannelData<Sample>()[0];
        for (int i = 0; i < c.GetDuration(); ++i) {
          CheckSample(buffer[i]);
        }
      }
    }
  }

  void AppendDataInterleaved(const Sample* aBuffer, uint32_t aFrames,
                             uint32_t aChannels) {
    for (uint32_t i = 0; i < aFrames * aChannels; i += aChannels) {
      CheckSample(aBuffer[i]);
    }
  }

  float EstimatedFreq() const {
    if (mTotalFramesSoFar == PreSilenceSamples()) {
      return 0;
    }
    if (mSumPeriodInSamples == 0) {
      return 0;
    }
    if (mZeroCrossCount <= 1) {
      return 0;
    }
    return mRate /
           (static_cast<float>(mSumPeriodInSamples) / (mZeroCrossCount - 1));
  }

  // Returns the maximum difference in value between two adjacent samples along
  // the sine curve.
  Sample MaxMagnitudeDifference() {
    return static_cast<Sample>(AudioGenerator<Sample>::Amplitude() *
                               2 * sin(2 * M_PI * mFrequency / mRate));
  }

  bool PreSilenceEnded() const {
    return mTotalFramesSoFar > mPreSilenceSamples;
  }
  uint64_t PreSilenceSamples() const { return mPreSilenceSamples; }
  uint32_t CountDiscontinuities() const { return mDiscontinuitiesCount; }

 private:
  void CheckSample(Sample aCurrentSample) {
    ++mTotalFramesSoFar;
    // Avoid pre-silence
    if (!CountPreSilence(aCurrentSample)) {
      CountZeroCrossing(aCurrentSample);
      CountDiscontinuities(aCurrentSample);
    }

    mPrevious = aCurrentSample;
  }

  bool CountPreSilence(Sample aCurrentSample) {
    if (IsZero(aCurrentSample) && mPreSilenceSamples == mTotalFramesSoFar - 1) {
      ++mPreSilenceSamples;
      return true;
    }
    if (IsZero(mPrevious) && aCurrentSample > 0 &&
        aCurrentSample < 2 * MaxMagnitudeDifference() &&
        mPreSilenceSamples == mTotalFramesSoFar - 1) {
      // Previous zero considered the first sample of the waveform.
      --mPreSilenceSamples;
    }
    return false;
  }

  // Positive to negative direction
  void CountZeroCrossing(Sample aCurrentSample) {
    if (mPrevious > 0 && aCurrentSample <= 0) {
      if (mZeroCrossCount++) {
        MOZ_ASSERT(mZeroCrossCount > 1);
        mSumPeriodInSamples += mTotalFramesSoFar - mLastZeroCrossPosition;
      }
      mLastZeroCrossPosition = mTotalFramesSoFar;
    }
  }

  void CountDiscontinuities(Sample aCurrentSample) {
    mDiscontinuitiesCount += fabs(fabs(aCurrentSample) - fabs(mPrevious)) >
                             3 * MaxMagnitudeDifference();
  }

  bool IsZero(float aValue) { return fabs(aValue) < 1e-8; }
  bool IsZero(short aValue) { return aValue == 0; }

 private:
  const uint32_t mRate;
  const uint32_t mFrequency;

  uint32_t mZeroCrossCount = 0;
  uint64_t mLastZeroCrossPosition = 0;
  uint64_t mSumPeriodInSamples = 0;

  uint64_t mTotalFramesSoFar = 0;
  uint64_t mPreSilenceSamples = 0;

  uint32_t mDiscontinuitiesCount = 0;
  // This is needed to connect the previous buffers.
  Sample mPrevious = {};
};

}  // namespace mozilla

#endif  // DOM_MEDIA_GTEST_AUDIOVERIFIER_H_
