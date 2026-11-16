/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioDriftCorrection.h"
#include "AudioGenerator.h"
#include "AudioVerifier.h"
#include "gtest/gtest.h"
#include "nsContentUtils.h"

using namespace mozilla;

template <class T>
AudioChunk CreateAudioChunk(uint32_t aFrames, uint32_t aChannels,
                            AudioSampleFormat aSampleFormat);

void testAudioCorrection(int32_t aSourceRate, int32_t aTargetRate,
                         bool aTestMonoToStereoInput = false) {
  const uint32_t frequency = 100;
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());
  AudioDriftCorrection ad(aSourceRate, aTargetRate, testPrincipal);

  uint8_t numChannels = 1;
  AudioGenerator<AudioDataValue> tone(numChannels, aSourceRate, frequency);
  AudioVerifier<AudioDataValue> inToneVerifier(aSourceRate, frequency);
  AudioVerifier<AudioDataValue> outToneVerifier(aTargetRate, frequency);

  // Run for some time: 3 * 5000 = 15000 iterations
  for (uint32_t j = 0; j < 3; ++j) {
    TrackTime sourceFramesIteration = 0;
    TrackTime targetFramesIteration = 0;

    // apply some drift (+/- .2%)
    const int8_t additionalDriftFrames =
        ((j % 2 == 0) ? aSourceRate : -aSourceRate) * 2 / 1000;

    // If the number of frames before changing channel count (and thereby
    // resetting the resampler) is very low, the measured buffering level curve
    // may look odd, as each resampler reset will reset the (possibly
    // fractional) output frame counter.
    const uint32_t numFramesBeforeChangingChannelCount = aSourceRate;
    uint32_t numFramesAtCurrentChannelCount = 0;

    // 50 seconds, allows for at least 50 correction changes, to stabilize
    // on the current drift.
    for (uint32_t n = 0; n < 5000; ++n) {
      const TrackTime sourceFrames =
          (n + 1) * (aSourceRate + additionalDriftFrames) / 100 -
          sourceFramesIteration;
      const TrackTime targetFrames =
          (n + 1) * aTargetRate / 100 - targetFramesIteration;
      AudioSegment inSegment;
      if (aTestMonoToStereoInput) {
        // Create the input (sine tone) of two chunks.
        const TrackTime sourceFramesPart1 = std::min<TrackTime>(
            sourceFrames, numFramesBeforeChangingChannelCount -
                              numFramesAtCurrentChannelCount);
        tone.Generate(inSegment, sourceFramesPart1);
        numFramesAtCurrentChannelCount += sourceFramesPart1;
        if (numFramesBeforeChangingChannelCount ==
            numFramesAtCurrentChannelCount) {
          tone.SetChannelsCount(numChannels = (numChannels % 2) + 1);
          numFramesAtCurrentChannelCount = sourceFrames - sourceFramesPart1;
          tone.Generate(inSegment, numFramesAtCurrentChannelCount);
        }
      } else {
        // Create the input (sine tone)
        tone.Generate(inSegment, sourceFrames);
      }
      inToneVerifier.AppendData(inSegment);

      // Get the output of the correction
      AudioSegment outSegment = ad.RequestFrames(inSegment, targetFrames);
      EXPECT_EQ(outSegment.GetDuration(), targetFrames);
      for (AudioSegment::ConstChunkIterator ci(outSegment); !ci.IsEnded();
           ci.Next()) {
        EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
      }
      outToneVerifier.AppendData(outSegment);
      sourceFramesIteration += sourceFrames;
      targetFramesIteration += targetFrames;
    }
  }

  // Initial buffering is 50ms, which is then expected to be reduced as the
  // drift adaptation stabilizes.
  EXPECT_LT(ad.CurrentBuffering(), aSourceRate * 50U / 1000);
  // Desired buffering should not go lower than some 130% of the source buffer
  // size per-iteration.
  EXPECT_GT(ad.CurrentBuffering(), aSourceRate * 10U / 1000);

  EXPECT_EQ(ad.NumUnderruns(), 0U);

  EXPECT_FLOAT_EQ(inToneVerifier.EstimatedFreq(), tone.mFrequency);
  EXPECT_EQ(inToneVerifier.PreSilenceSamples(), 0U);
  EXPECT_EQ(inToneVerifier.CountDiscontinuities(), 0U);

  EXPECT_NEAR(outToneVerifier.EstimatedFreq(), tone.mFrequency, 1.0f);
  // The expected pre-silence is equal to the initial desired buffering (50ms)
  // minus what is left after resampling the first input segment.
  const auto buffering = media::TimeUnit::FromSeconds(0.05);
  const auto sourceStep =
      media::TimeUnit(aSourceRate * 1002 / 1000 / 100, aSourceRate);
  const auto targetStep = media::TimeUnit(aTargetRate / 100, aTargetRate);
  EXPECT_NEAR(static_cast<int64_t>(outToneVerifier.PreSilenceSamples()),
              (targetStep + buffering - sourceStep)
                  .ToBase(aSourceRate)
                  .ToBase<media::TimeUnit::CeilingPolicy>(aTargetRate)
                  .ToTicksAtRate(aTargetRate),
              1U);
  EXPECT_EQ(outToneVerifier.CountDiscontinuities(), 0U);
}

TEST(TestAudioDriftCorrection, Basic)
{
  printf("Testing AudioCorrection 48 -> 48\n");
  testAudioCorrection(48000, 48000);
  printf("Testing AudioCorrection 48 -> 44.1\n");
  testAudioCorrection(48000, 44100);
  printf("Testing AudioCorrection 44.1 -> 48\n");
  testAudioCorrection(44100, 48000);
  printf("Testing AudioCorrection 23458 -> 25113\n");
  testAudioCorrection(23458, 25113);
}

TEST(TestAudioDriftCorrection, MonoToStereoInput)
{
  constexpr bool testMonoToStereoInput = true;
  printf("Testing MonoToStereoInput 48 -> 48\n");
  testAudioCorrection(48000, 48000, testMonoToStereoInput);
  printf("Testing MonoToStereoInput 48 -> 44.1\n");
  testAudioCorrection(48000, 44100, testMonoToStereoInput);
  printf("Testing MonoToStereoInput 44.1 -> 48\n");
  testAudioCorrection(44100, 48000, testMonoToStereoInput);
}

TEST(TestAudioDriftCorrection, NotEnoughFrames)
{
  const uint32_t frequency = 100;
  const uint32_t sampleRateTransmitter = 48000;
  const uint32_t sampleRateReceiver = 48000;
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());
  AudioDriftCorrection ad(sampleRateTransmitter, sampleRateReceiver,
                          testPrincipal);
  const uint32_t targetFrames = sampleRateReceiver / 100;

  AudioGenerator<AudioDataValue> tone(1, sampleRateTransmitter, frequency);
  AudioVerifier<AudioDataValue> outToneVerifier(sampleRateReceiver, frequency);

  for (uint32_t i = 0; i < 7; ++i) {
    // Input is something small, 10 frames here, in order to dry out fast,
    // after 4 iterations (pre-buffer = 2400)
    AudioSegment inSegment;
    tone.Generate(inSegment, 10);

    AudioSegment outSegment = ad.RequestFrames(inSegment, targetFrames);
    EXPECT_EQ(outSegment.GetDuration(), targetFrames);
    EXPECT_FALSE(outSegment.IsNull());
    for (AudioSegment::ConstChunkIterator ci(outSegment); !ci.IsEnded();
         ci.Next()) {
      if (i < 5) {
        if (!ci->IsNull()) {
          EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
        }
      }
    }

    outToneVerifier.AppendData(outSegment);
  }
  EXPECT_EQ(ad.BufferSize(), 4800U);
  EXPECT_EQ(ad.NumUnderruns(), 1u);
  EXPECT_EQ(outToneVerifier.CountDiscontinuities(), 1u);
}

TEST(TestAudioDriftCorrection, CrashInAudioResampler)
{
  const uint32_t sampleRateTransmitter = 48000;
  const uint32_t sampleRateReceiver = 48000;
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());
  AudioDriftCorrection ad(sampleRateTransmitter, sampleRateReceiver,
                          testPrincipal);
  const uint32_t targetFrames = sampleRateReceiver / 100;

  for (uint32_t i = 0; i < 100; ++i) {
    AudioChunk chunk = CreateAudioChunk<float>(sampleRateTransmitter / 1000, 1,
                                               AUDIO_FORMAT_FLOAT32);
    AudioSegment inSegment;
    inSegment.AppendAndConsumeChunk(std::move(chunk));

    AudioSegment outSegment = ad.RequestFrames(inSegment, targetFrames);
    EXPECT_EQ(outSegment.GetDuration(), targetFrames);
    for (AudioSegment::ConstChunkIterator ci(outSegment); !ci.IsEnded();
         ci.Next()) {
      if (!ci->IsNull()) {  // Don't check the data if ad is dried out.
        EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
      }
    }
  }
}

TEST(TestAudioDriftCorrection, HighLatencyProducerLowLatencyConsumer)
{
  constexpr uint32_t transmitterBlockSize = 2048;
  constexpr uint32_t receiverBlockSize = 128;
  constexpr uint32_t sampleRate = 48000;
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());
  AudioDriftCorrection ad(sampleRate, sampleRate, testPrincipal);

  uint32_t numBlocksProduced = 0;
  for (uint32_t i = 0; i < (sampleRate / 1000) * 500; i += receiverBlockSize) {
    AudioSegment inSegment;
    if ((i / transmitterBlockSize) > numBlocksProduced) {
      AudioChunk chunk = CreateAudioChunk<float>(transmitterBlockSize, 1,
                                                 AUDIO_FORMAT_FLOAT32);
      inSegment.AppendAndConsumeChunk(std::move(chunk));
      ++numBlocksProduced;
    }

    AudioSegment outSegment = ad.RequestFrames(inSegment, receiverBlockSize);
    EXPECT_EQ(outSegment.GetDuration(), receiverBlockSize);
  }

  // Input is stable so no corrections should occur.
  EXPECT_EQ(ad.NumCorrectionChanges(), 0U);
}

TEST(TestAudioDriftCorrection, LargerTransmitterBlockSizeThanDesiredBuffering)
{
  constexpr uint32_t transmitterBlockSize = 4096;
  constexpr uint32_t receiverBlockSize = 128;
  constexpr uint32_t sampleRate = 48000;
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());
  AudioDriftCorrection ad(sampleRate, sampleRate, testPrincipal);

  uint32_t numBlocksTransmitted = 0;
  for (uint32_t i = 0; i < (sampleRate / 1000) * 500; i += receiverBlockSize) {
    AudioSegment inSegment;
    if (uint32_t currentBlock = i / transmitterBlockSize;
        currentBlock > numBlocksTransmitted) {
      AudioChunk chunk = CreateAudioChunk<float>(transmitterBlockSize, 1,
                                                 AUDIO_FORMAT_FLOAT32);
      inSegment.AppendAndConsumeChunk(std::move(chunk));
      numBlocksTransmitted = currentBlock;
    }

    AudioSegment outSegment = ad.RequestFrames(inSegment, receiverBlockSize);
    EXPECT_EQ(outSegment.GetDuration(), receiverBlockSize);

    if (numBlocksTransmitted > 0) {
      EXPECT_GT(ad.CurrentBuffering(), 0U);
    }
  }

  // Input is stable so no corrections should occur.
  EXPECT_EQ(ad.NumCorrectionChanges(), 0U);
  // The desired buffering and pre-buffering level was
  // transmitterBlockSize * 11 / 10 to accomodate the large input block size.
  // The buffer size was twice the pre-buffering level.
  EXPECT_EQ(ad.BufferSize(), transmitterBlockSize * 11 / 10 * 2);
}

TEST(TestAudioDriftCorrection, LargerReceiverBlockSizeThanDesiredBuffering)
{
  constexpr uint32_t transmitterBlockSize = 128;
  constexpr uint32_t receiverBlockSize = 4096;
  constexpr uint32_t sampleRate = 48000;
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());
  AudioDriftCorrection ad(sampleRate, sampleRate, testPrincipal);

  AudioSegment inSegment;
  for (uint32_t i = 0; i < (sampleRate / 1000) * 500;
       i += transmitterBlockSize) {
    AudioChunk chunk =
        CreateAudioChunk<float>(transmitterBlockSize, 1, AUDIO_FORMAT_FLOAT32);
    inSegment.AppendAndConsumeChunk(std::move(chunk));

    if (i % receiverBlockSize == 0) {
      AudioSegment outSegment = ad.RequestFrames(inSegment, receiverBlockSize);
      EXPECT_EQ(outSegment.GetDuration(), receiverBlockSize);
      inSegment.Clear();
    }

    if (i >= receiverBlockSize) {
      EXPECT_GT(ad.CurrentBuffering(), 0U);
    }
  }

  // Input is stable so no corrections should occur.
  EXPECT_EQ(ad.NumCorrectionChanges(), 0U);
  EXPECT_EQ(ad.NumUnderruns(), 0U);
  // The drift correction buffer size had to be larger than the desired (the
  // buffer size is twice the initial buffering level), to accomodate the large
  // input block size that gets buffered in the resampler only when processing
  // output.
  EXPECT_EQ(ad.BufferSize(), 9600U);
}

TEST(TestAudioDriftCorrection, DynamicInputBufferSizeChanges)
{
  constexpr uint32_t transmitterBlockSize1 = 2048;
  constexpr uint32_t transmitterBlockSize2 = 4096;
  constexpr uint32_t receiverBlockSize = 128;
  constexpr uint32_t sampleRate = 48000;
  constexpr uint32_t frequencyHz = 100;
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());
  AudioDriftCorrection ad(sampleRate, sampleRate, testPrincipal);

  AudioGenerator<AudioDataValue> tone(1, sampleRate, frequencyHz);
  AudioVerifier<AudioDataValue> inToneVerifier(sampleRate, frequencyHz);
  AudioVerifier<AudioDataValue> outToneVerifier(sampleRate, frequencyHz);

  TrackTime totalFramesTransmitted = 0;
  TrackTime totalFramesReceived = 0;

  const auto produceSomeData = [&](uint32_t aTransmitterBlockSize,
                                   uint32_t aDuration) {
    TrackTime transmittedFramesStart = totalFramesTransmitted;
    TrackTime receivedFramesStart = totalFramesReceived;
    uint32_t numBlocksTransmitted = 0;
    for (uint32_t i = 0; i < aDuration; i += receiverBlockSize) {
      AudioSegment inSegment;
      if (((receivedFramesStart - transmittedFramesStart + i) /
           aTransmitterBlockSize) > numBlocksTransmitted) {
        tone.Generate(inSegment, aTransmitterBlockSize);
        MOZ_RELEASE_ASSERT(!inSegment.IsNull());
        inToneVerifier.AppendData(inSegment);
        MOZ_RELEASE_ASSERT(!inSegment.IsNull());
        ++numBlocksTransmitted;
        totalFramesTransmitted += aTransmitterBlockSize;
      }

      AudioSegment outSegment = ad.RequestFrames(inSegment, receiverBlockSize);
      EXPECT_EQ(outSegment.GetDuration(), receiverBlockSize);
      outToneVerifier.AppendData(outSegment);
      totalFramesReceived += receiverBlockSize;
    }
  };

  produceSomeData(transmitterBlockSize1, 5 * sampleRate);
  EXPECT_EQ(ad.BufferSize(), 4800U);
  // No input is provided for the first transmitterBlockSize1 of output
  // requested.  This causes a lower input rate estimate, so there are some
  // initial corrections.
  EXPECT_GT(ad.NumCorrectionChanges(), 0U);
  EXPECT_EQ(ad.NumUnderruns(), 0U);

  // Increase input latency. We expect this to underrun, but only once as the
  // drift correction adapts its buffer size and desired buffering level.
  produceSomeData(transmitterBlockSize2, 25 * sampleRate);
  auto numCorrectionChanges = ad.NumCorrectionChanges();
  EXPECT_EQ(ad.NumUnderruns(), 1U);

  // Adapting to the new input block size should have stabilized.
  EXPECT_GT(ad.BufferSize(), transmitterBlockSize2);
  produceSomeData(transmitterBlockSize2, 10 * sampleRate);
  EXPECT_LE(ad.NumCorrectionChanges(), numCorrectionChanges + 1);
  EXPECT_EQ(ad.NumUnderruns(), 1U);

  // Decrease input latency. We expect the drift correction to gradually
  // decrease its desired buffering level.
  produceSomeData(transmitterBlockSize1, 100 * sampleRate);
  numCorrectionChanges = ad.NumCorrectionChanges();
  EXPECT_EQ(ad.NumUnderruns(), 1U);

  EXPECT_EQ(ad.BufferSize(), 9600U);
  // Adjustments to the desired buffering level continue.
  produceSomeData(transmitterBlockSize1, 20 * sampleRate);
  EXPECT_LE(ad.NumCorrectionChanges(), numCorrectionChanges + 2);
  EXPECT_EQ(ad.NumUnderruns(), 1U);

  EXPECT_NEAR(inToneVerifier.EstimatedFreq(), tone.mFrequency, 1.0f);
  EXPECT_EQ(inToneVerifier.PreSilenceSamples(), 0U);
  EXPECT_EQ(inToneVerifier.CountDiscontinuities(), 0U);

  EXPECT_NEAR(outToneVerifier.EstimatedFreq(), tone.mFrequency, 1.0f);
  // No input is provided for the first transmitterBlockSize1 of output
  // requested, so this output is all silent.
  // The expected additional pre-silence is equal to the desired buffering
  // plus what's needed to produce the first output segment minus the first
  // input segment.
  EXPECT_EQ(outToneVerifier.PreSilenceSamples(), 2528U);
  EXPECT_NEAR(outToneVerifier.PreSilenceSamples() - transmitterBlockSize1,
              media::TimeUnit::FromSeconds(0.05).ToTicksAtRate(sampleRate) +
                  receiverBlockSize - transmitterBlockSize1,
              1U);
  // One mid-stream period of silence from increasing the input buffer size,
  // causing an underrun. Counts as two discontinuities.
  EXPECT_EQ(outToneVerifier.CountDiscontinuities(), 2U);
}

/**
 * This is helpful to run together with
 *   MOZ_LOG=raw,DriftControllerGraphs:5 MOZ_LOG_FILE=./plot_values.csv
 * to be able to plot the step response of a change in source clock rate (i.e.
 * drift). Useful for calculating and verifying PID coefficients.
 */
TEST(TestAudioDriftCorrection, DriftStepResponse)
{
  constexpr uint32_t nominalRate = 48000;
  constexpr uint32_t interval = nominalRate;
  constexpr uint32_t inputRate = nominalRate * 1005 / 1000;  // 0.5% drift
  constexpr uint32_t inputInterval = inputRate;
  constexpr uint32_t iterations = 200;
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());
  AudioGenerator<AudioDataValue> tone(1, nominalRate, 440);
  AudioDriftCorrection ad(nominalRate, nominalRate, testPrincipal);
  for (uint32_t i = 0; i < interval * iterations; i += interval / 100) {
    AudioSegment inSegment;
    tone.Generate(inSegment, inputInterval / 100);
    ad.RequestFrames(inSegment, interval / 100);
  }

  EXPECT_EQ(ad.BufferSize(), 4800U);
  EXPECT_EQ(ad.NumUnderruns(), 0u);
}

/**
 * Similar to DriftStepResponse but will underrun to allow testing the underrun
 * handling. This is helpful to run together with
 *   MOZ_LOG=raw,DriftControllerGraphs:5 MOZ_LOG_FILE=./plot_values.csv
 */
TEST(TestAudioDriftCorrection, DriftStepResponseUnderrun)
{
  constexpr uint32_t nominalRate = 48000;
  constexpr uint32_t interval = nominalRate;
  constexpr uint32_t iterations = 200;
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());
  uint32_t inputRate = nominalRate * 1005 / 1000;  // 0.5% drift
  uint32_t inputInterval = inputRate;
  AudioGenerator<AudioDataValue> tone(1, nominalRate, 440);
  AudioDriftCorrection ad(nominalRate, nominalRate, testPrincipal);
  for (uint32_t i = 0; i < interval * iterations; i += interval / 100) {
    AudioSegment inSegment;
    tone.Generate(inSegment, inputInterval / 100);
    ad.RequestFrames(inSegment, interval / 100);
  }

  inputRate = nominalRate * 997 / 1000;  // -0.3% drift
  inputInterval = inputRate;
  for (uint32_t i = 0; i < interval * iterations; i += interval / 100) {
    AudioSegment inSegment;
    tone.Generate(inSegment, inputInterval / 100);
    ad.RequestFrames(inSegment, interval / 100);
  }

  EXPECT_EQ(ad.BufferSize(), 4800U);
  EXPECT_EQ(ad.NumUnderruns(), 1u);
}

/**
 * Similar to DriftStepResponse but with a high-latency input, and will underrun
 * to allow testing the underrun handling. This is helpful to run together with
 *   MOZ_LOG=raw,DriftControllerGraphs:5 MOZ_LOG_FILE=./plot_values.csv
 */
TEST(TestAudioDriftCorrection, DriftStepResponseUnderrunHighLatencyInput)
{
  constexpr uint32_t nominalRate = 48000;
  constexpr uint32_t interval = nominalRate;
  constexpr uint32_t iterations = 200;
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());
  uint32_t inputRate1 = nominalRate * 1005 / 1000;  // 0.5% drift
  uint32_t inputInterval1 = inputRate1;
  AudioGenerator<AudioDataValue> tone(1, nominalRate, 440);
  AudioDriftCorrection ad(nominalRate, nominalRate, testPrincipal);
  for (uint32_t i = 0; i < interval * iterations; i += interval / 100) {
    AudioSegment inSegment;
    if (i > 0 && i % interval == 0) {
      tone.Generate(inSegment, inputInterval1);
    }
    ad.RequestFrames(inSegment, interval / 100);
  }

  uint32_t inputRate2 = nominalRate * 995 / 1000;  // -0.5% drift
  uint32_t inputInterval2 = inputRate2;
  for (uint32_t i = 0; i < interval * iterations; i += interval / 100) {
    AudioSegment inSegment;
    // The first segment is skipped to cause an underrun.
    if (i > 0 && i % interval == 0) {
      tone.Generate(inSegment, inputInterval2);
    }
    ad.RequestFrames(inSegment, interval / 100);
    if (i >= interval * 8 / 10 && i < interval) {
      // While the DynamicResampler has not set its pre-buffer after the
      // underrun, InFramesBuffered() reports the pre-buffer size.
      // The initial desired buffer and pre-buffer size was
      // inputInterval1 * 11 / 10 to accomodate the large input block size.
      // This was doubled when the underrun occurred.
      EXPECT_EQ(ad.CurrentBuffering(), inputInterval1 * 11 / 10 * 2)
          << "for i=" << i;
    } else if (i == interval) {
      // After the pre-buffer was set and used to generate the first output
      // with a non-empty input segment after the underun, the actual number
      // of frames buffered almost matches the pre-buffer size, with some
      // rounding from output to input frame count conversion.
      EXPECT_NEAR(ad.CurrentBuffering(), inputInterval1 * 11 / 10 * 2, 1)
          << "after first input after underrun";
    }
  }

  // The initial desired buffering and pre-buffering level was
  // inputInterval1 * 11 / 10 to accomodate the large input block size.
  // The buffer size was initially twice the pre-buffering level, and then
  // doubled when the underrun occurred.
  EXPECT_EQ(ad.BufferSize(), inputInterval1 * 11 / 10 * 2 * 2);
  EXPECT_EQ(ad.NumUnderruns(), 1u);
}

/**
 * Similar to DriftStepResponse but with a high-latency input, and will overrun
 * (input callback buffer is larger than AudioDriftCorrection's ring buffer for
 * input data) to allow testing the overrun handling. This is helpful to run
 * together with
 *   MOZ_LOG=raw,DriftControllerGraphs:5 MOZ_LOG_FILE=./plot_values.csv
 */
TEST(TestAudioDriftCorrection, DriftStepResponseOverrun)
{
  constexpr uint32_t nominalRate = 48000;
  constexpr uint32_t interval = nominalRate;
  constexpr uint32_t inputRate = nominalRate * 1005 / 1000;  // 0.5% drift
  constexpr uint32_t inputInterval = inputRate;
  constexpr uint32_t iterations = 200;
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());

  AudioGenerator<AudioDataValue> tone(1, nominalRate, 440);
  AudioDriftCorrection ad(nominalRate, nominalRate, testPrincipal);

  for (uint32_t i = 0; i < interval * iterations; i += interval / 100) {
    AudioSegment inSegment;
    tone.Generate(inSegment, inputInterval / 100);
    ad.RequestFrames(inSegment, interval / 100);
  }

  // Change input callbacks to 1000ms (+0.5% drift) = 48200 frames, which will
  // overrun the ring buffer.
  for (uint32_t i = 0; i < interval * iterations; i += interval / 100) {
    AudioSegment inSegment;
    if (i > 0 && i % interval == 0) {
      // This simulates the input stream latency increasing externally. It's
      // building up a second worth of data before the next callback. This also
      // causes an underrun.
      tone.Generate(inSegment, inputInterval);
    }
    ad.RequestFrames(inSegment, interval / 100);
  }

  // The desired buffering and pre-buffering levels were increased to
  // inputInterval * 11 / 10 to accomodate the large input block size.
  // The buffer size was increased to twice the pre-buffering level.
  EXPECT_EQ(ad.BufferSize(), inputInterval * 11 / 10 * 2);
  EXPECT_EQ(ad.NumUnderruns(), 1u);
}
