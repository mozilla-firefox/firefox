/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioResampler.h"
#include "gtest/gtest.h"
#include "nsContentUtils.h"

using namespace mozilla;

template <class T>
AudioChunk CreateAudioChunk(uint32_t aFrames, uint32_t aChannels,
                            AudioSampleFormat aSampleFormat) {
  AudioChunk chunk;
  nsTArray<nsTArray<T>> buffer;
  buffer.AppendElements(aChannels);

  nsTArray<const T*> bufferPtrs;
  bufferPtrs.AppendElements(aChannels);

  for (uint32_t i = 0; i < aChannels; ++i) {
    T* ptr = buffer[i].AppendElements(aFrames);
    bufferPtrs[i] = ptr;
    for (uint32_t j = 0; j < aFrames; ++j) {
      if (aSampleFormat == AUDIO_FORMAT_FLOAT32) {
        ptr[j] = 0.01 * j;
      } else {
        ptr[j] = j;
      }
    }
  }

  chunk.mBuffer = new mozilla::SharedChannelArrayBuffer(std::move(buffer));
  chunk.mBufferFormat = aSampleFormat;
  chunk.mChannelData.AppendElements(aChannels);
  for (uint32_t i = 0; i < aChannels; ++i) {
    chunk.mChannelData[i] = bufferPtrs[i];
  }
  chunk.mDuration = aFrames;
  return chunk;
}

template <class T>
AudioSegment CreateAudioSegment(uint32_t aFrames, uint32_t aChannels,
                                AudioSampleFormat aSampleFormat) {
  AudioSegment segment;
  AudioChunk chunk = CreateAudioChunk<T>(aFrames, aChannels, aSampleFormat);
  segment.AppendAndConsumeChunk(std::move(chunk));
  return segment;
}

TEST(TestAudioResampler, OutAudioSegment_Float)
{
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());

  uint32_t in_frames = 10;
  uint32_t out_frames = 40;
  uint32_t channels = 2;
  uint32_t in_rate = 24000;
  uint32_t out_rate = 48000;

  uint32_t pre_buffer = 21;

  AudioResampler dr(in_rate, out_rate, pre_buffer, testPrincipal);

  AudioSegment inSegment =
      CreateAudioSegment<float>(in_frames, channels, AUDIO_FORMAT_FLOAT32);
  dr.AppendInput(inSegment);

  out_frames = 20u;
  bool hasUnderrun = false;
  AudioSegment s = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  EXPECT_EQ(s.GetDuration(), 20);
  EXPECT_EQ(s.GetType(), MediaSegment::AUDIO);

  for (AudioSegment::ChunkIterator ci(s); !ci.IsEnded(); ci.Next()) {
    AudioChunk& c = *ci;
    EXPECT_EQ(c.mPrincipalHandle, testPrincipal);
    EXPECT_EQ(c.ChannelCount(), 2u);
    for (uint32_t i = 0; i < out_frames; ++i) {
      // The first input segment is part of the pre buffer, so 21-10=11 of the
      // input is silence. They make up 22 silent output frames after
      // resampling.
      EXPECT_FLOAT_EQ(c.ChannelData<float>()[0][i], 0.0);
      EXPECT_FLOAT_EQ(c.ChannelData<float>()[1][i], 0.0);
    }
  }

  // Update in rate
  in_rate = 26122;
  dr.UpdateInRate(in_rate);
  out_frames = in_frames * out_rate / in_rate;
  EXPECT_EQ(out_frames, 18u);
  // Even if we provide no input if we have enough buffered input, we can create
  // output
  hasUnderrun = false;
  AudioSegment s1 = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  EXPECT_EQ(s1.GetDuration(), out_frames);
  EXPECT_EQ(s1.GetType(), MediaSegment::AUDIO);
  for (AudioSegment::ConstChunkIterator ci(s1); !ci.IsEnded(); ci.Next()) {
    EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
  }
}

TEST(TestAudioResampler, OutAudioSegment_Short)
{
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());

  uint32_t in_frames = 10;
  uint32_t out_frames = 40;
  uint32_t channels = 2;
  uint32_t in_rate = 24000;
  uint32_t out_rate = 48000;

  uint32_t pre_buffer = 21;

  AudioResampler dr(in_rate, out_rate, pre_buffer, testPrincipal);

  AudioSegment inSegment =
      CreateAudioSegment<short>(in_frames, channels, AUDIO_FORMAT_S16);
  dr.AppendInput(inSegment);

  out_frames = 20u;
  bool hasUnderrun = false;
  AudioSegment s = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  EXPECT_EQ(s.GetDuration(), 20);
  EXPECT_EQ(s.GetType(), MediaSegment::AUDIO);

  for (AudioSegment::ChunkIterator ci(s); !ci.IsEnded(); ci.Next()) {
    AudioChunk& c = *ci;
    EXPECT_EQ(c.mPrincipalHandle, testPrincipal);
    EXPECT_EQ(c.ChannelCount(), 2u);
    for (uint32_t i = 0; i < out_frames; ++i) {
      // The first input segment is part of the pre buffer, so 21-10=11 of the
      // input is silence. They make up 22 silent output frames after
      // resampling.
      EXPECT_FLOAT_EQ(c.ChannelData<short>()[0][i], 0.0);
      EXPECT_FLOAT_EQ(c.ChannelData<short>()[1][i], 0.0);
    }
  }

  // Update in rate
  in_rate = 26122;
  dr.UpdateInRate(out_rate);
  out_frames = in_frames * out_rate / in_rate;
  EXPECT_EQ(out_frames, 18u);
  // Even if we provide no input if we have enough buffered input, we can create
  // output
  hasUnderrun = false;
  AudioSegment s1 = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  EXPECT_EQ(s1.GetDuration(), out_frames);
  EXPECT_EQ(s1.GetType(), MediaSegment::AUDIO);
  for (AudioSegment::ConstChunkIterator ci(s1); !ci.IsEnded(); ci.Next()) {
    EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
  }
}

TEST(TestAudioResampler, OutAudioSegmentLargerThanResampledInput_Float)
{
  const uint32_t in_frames = 130;
  const uint32_t out_frames = 300;
  uint32_t channels = 2;
  uint32_t in_rate = 24000;
  uint32_t out_rate = 48000;

  uint32_t pre_buffer = 5;

  AudioResampler dr(in_rate, out_rate, pre_buffer, PRINCIPAL_HANDLE_NONE);

  AudioSegment inSegment =
      CreateAudioSegment<float>(in_frames, channels, AUDIO_FORMAT_FLOAT32);

  // Set the pre-buffer.
  dr.AppendInput(inSegment);
  bool hasUnderrun = false;
  AudioSegment s = dr.Resample(300, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  EXPECT_EQ(s.GetDuration(), 300);
  EXPECT_EQ(s.GetType(), MediaSegment::AUDIO);

  dr.AppendInput(inSegment);

  AudioSegment s2 = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_TRUE(hasUnderrun);
  EXPECT_EQ(s2.GetDuration(), 300);
  EXPECT_EQ(s2.GetType(), MediaSegment::AUDIO);
}

TEST(TestAudioResampler, InAudioSegment_Float)
{
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());

  uint32_t in_frames = 10;
  uint32_t out_frames = 20;
  uint32_t channels = 2;
  uint32_t in_rate = 24000;
  uint32_t out_rate = 48000;

  uint32_t pre_buffer = 10;
  AudioResampler dr(in_rate, out_rate, pre_buffer, testPrincipal);

  AudioSegment inSegment;

  AudioChunk chunk1;
  chunk1.SetNull(in_frames / 2);
  inSegment.AppendAndConsumeChunk(std::move(chunk1));

  AudioChunk chunk2;
  nsTArray<nsTArray<float>> buffer;
  buffer.AppendElements(channels);

  nsTArray<const float*> bufferPtrs;
  bufferPtrs.AppendElements(channels);

  for (uint32_t i = 0; i < channels; ++i) {
    float* ptr = buffer[i].AppendElements(5);
    bufferPtrs[i] = ptr;
    for (uint32_t j = 0; j < 5; ++j) {
      ptr[j] = 0.01f * j;
    }
  }

  chunk2.mBuffer = new mozilla::SharedChannelArrayBuffer(std::move(buffer));
  chunk2.mBufferFormat = AUDIO_FORMAT_FLOAT32;
  chunk2.mChannelData.AppendElements(channels);
  for (uint32_t i = 0; i < channels; ++i) {
    chunk2.mChannelData[i] = bufferPtrs[i];
  }
  chunk2.mDuration = in_frames / 2;
  inSegment.AppendAndConsumeChunk(std::move(chunk2));

  dr.AppendInput(inSegment);
  bool hasUnderrun = false;
  AudioSegment outSegment = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  // inSegment contains 10 frames, 5 null, 5 non-null. They're part of the pre
  // buffer which is 10, meaning there are no extra pre buffered silence frames.
  EXPECT_EQ(outSegment.GetDuration(), out_frames);
  EXPECT_EQ(outSegment.MaxChannelCount(), 2u);

  // Add another 5 null and 5 non-null frames.
  dr.AppendInput(inSegment);
  AudioSegment outSegment2 = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  EXPECT_EQ(outSegment2.GetDuration(), out_frames);
  EXPECT_EQ(outSegment2.MaxChannelCount(), 2u);
  for (AudioSegment::ConstChunkIterator ci(outSegment2); !ci.IsEnded();
       ci.Next()) {
    EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
  }
}

TEST(TestAudioResampler, InAudioSegment_Short)
{
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());

  uint32_t in_frames = 10;
  uint32_t out_frames = 20;
  uint32_t channels = 2;
  uint32_t in_rate = 24000;
  uint32_t out_rate = 48000;

  uint32_t pre_buffer = 10;
  AudioResampler dr(in_rate, out_rate, pre_buffer, testPrincipal);

  AudioSegment inSegment;

  // The null chunk at the beginning will be ignored.
  AudioChunk chunk1;
  chunk1.SetNull(in_frames / 2);
  inSegment.AppendAndConsumeChunk(std::move(chunk1));

  AudioChunk chunk2;
  nsTArray<nsTArray<short>> buffer;
  buffer.AppendElements(channels);

  nsTArray<const short*> bufferPtrs;
  bufferPtrs.AppendElements(channels);

  for (uint32_t i = 0; i < channels; ++i) {
    short* ptr = buffer[i].AppendElements(5);
    bufferPtrs[i] = ptr;
    for (uint32_t j = 0; j < 5; ++j) {
      ptr[j] = j;
    }
  }

  chunk2.mBuffer = new mozilla::SharedChannelArrayBuffer(std::move(buffer));
  chunk2.mBufferFormat = AUDIO_FORMAT_S16;
  chunk2.mChannelData.AppendElements(channels);
  for (uint32_t i = 0; i < channels; ++i) {
    chunk2.mChannelData[i] = bufferPtrs[i];
  }
  chunk2.mDuration = in_frames / 2;
  inSegment.AppendAndConsumeChunk(std::move(chunk2));

  dr.AppendInput(inSegment);
  bool hasUnderrun = false;
  AudioSegment outSegment = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  // inSegment contains 10 frames, 5 null, 5 non-null. They're part of the pre
  // buffer which is 10, meaning there are no extra pre buffered silence frames.
  EXPECT_EQ(outSegment.GetDuration(), out_frames);
  EXPECT_EQ(outSegment.MaxChannelCount(), 2u);

  // Add another 5 null and 5 non-null frames.
  dr.AppendInput(inSegment);
  AudioSegment outSegment2 = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  EXPECT_EQ(outSegment2.GetDuration(), out_frames);
  EXPECT_EQ(outSegment2.MaxChannelCount(), 2u);
  for (AudioSegment::ConstChunkIterator ci(outSegment2); !ci.IsEnded();
       ci.Next()) {
    EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
  }
}

TEST(TestAudioResampler, ChannelChange_MonoToStereo)
{
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());

  uint32_t in_frames = 10;
  uint32_t out_frames = 40;
  uint32_t in_rate = 24000;
  uint32_t out_rate = 48000;

  uint32_t pre_buffer = 0;

  AudioResampler dr(in_rate, out_rate, pre_buffer, testPrincipal);

  AudioChunk monoChunk =
      CreateAudioChunk<float>(in_frames, 1, AUDIO_FORMAT_FLOAT32);
  AudioChunk stereoChunk =
      CreateAudioChunk<float>(in_frames, 2, AUDIO_FORMAT_FLOAT32);

  AudioSegment inSegment;
  inSegment.AppendAndConsumeChunk(std::move(monoChunk));
  inSegment.AppendAndConsumeChunk(std::move(stereoChunk));
  dr.AppendInput(inSegment);

  bool hasUnderrun = false;
  AudioSegment s = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  EXPECT_EQ(s.GetDuration(), 40);
  EXPECT_EQ(s.GetType(), MediaSegment::AUDIO);
  EXPECT_EQ(s.MaxChannelCount(), 2u);
  for (AudioSegment::ConstChunkIterator ci(s); !ci.IsEnded(); ci.Next()) {
    EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
  }
}

TEST(TestAudioResampler, ChannelChange_StereoToMono)
{
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());

  uint32_t in_frames = 10;
  uint32_t out_frames = 40;
  uint32_t in_rate = 24000;
  uint32_t out_rate = 48000;

  uint32_t pre_buffer = 0;

  AudioResampler dr(in_rate, out_rate, pre_buffer, testPrincipal);

  AudioChunk monoChunk =
      CreateAudioChunk<float>(in_frames, 1, AUDIO_FORMAT_FLOAT32);
  AudioChunk stereoChunk =
      CreateAudioChunk<float>(in_frames, 2, AUDIO_FORMAT_FLOAT32);

  AudioSegment inSegment;
  inSegment.AppendAndConsumeChunk(std::move(stereoChunk));
  inSegment.AppendAndConsumeChunk(std::move(monoChunk));
  dr.AppendInput(inSegment);

  bool hasUnderrun = false;
  AudioSegment s = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  EXPECT_EQ(s.GetDuration(), 40);
  EXPECT_EQ(s.GetType(), MediaSegment::AUDIO);
  EXPECT_EQ(s.MaxChannelCount(), 1u);
  for (AudioSegment::ConstChunkIterator ci(s); !ci.IsEnded(); ci.Next()) {
    EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
  }
}

TEST(TestAudioResampler, ChannelChange_StereoToQuad)
{
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());

  uint32_t in_frames = 10;
  uint32_t out_frames = 40;
  uint32_t in_rate = 24000;
  uint32_t out_rate = 48000;

  uint32_t pre_buffer = 0;

  AudioResampler dr(in_rate, out_rate, pre_buffer, testPrincipal);

  AudioChunk stereoChunk =
      CreateAudioChunk<float>(in_frames, 2, AUDIO_FORMAT_FLOAT32);
  AudioChunk quadChunk =
      CreateAudioChunk<float>(in_frames, 4, AUDIO_FORMAT_FLOAT32);

  AudioSegment inSegment;
  inSegment.AppendAndConsumeChunk(std::move(stereoChunk));
  inSegment.AppendAndConsumeChunk(std::move(quadChunk));
  dr.AppendInput(inSegment);

  bool hasUnderrun = false;
  AudioSegment s = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  EXPECT_EQ(s.GetDuration(), 40u);
  EXPECT_EQ(s.GetType(), MediaSegment::AUDIO);

  AudioSegment s2 = dr.Resample(out_frames / 2, &hasUnderrun);
  EXPECT_TRUE(hasUnderrun);
  EXPECT_EQ(s2.GetDuration(), 20u);
  EXPECT_EQ(s2.GetType(), MediaSegment::AUDIO);
  for (AudioSegment::ConstChunkIterator ci(s); !ci.IsEnded(); ci.Next()) {
    EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
  }
}

TEST(TestAudioResampler, ChannelChange_QuadToStereo)
{
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());

  uint32_t in_frames = 10;
  uint32_t out_frames = 40;
  uint32_t in_rate = 24000;
  uint32_t out_rate = 48000;

  AudioResampler dr(in_rate, out_rate, 0, testPrincipal);

  AudioChunk stereoChunk =
      CreateAudioChunk<float>(in_frames, 2, AUDIO_FORMAT_FLOAT32);
  AudioChunk quadChunk =
      CreateAudioChunk<float>(in_frames, 4, AUDIO_FORMAT_FLOAT32);

  AudioSegment inSegment;
  inSegment.AppendAndConsumeChunk(std::move(quadChunk));
  inSegment.AppendAndConsumeChunk(std::move(stereoChunk));
  dr.AppendInput(inSegment);

  bool hasUnderrun = false;
  AudioSegment s = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  EXPECT_EQ(s.GetDuration(), 40u);
  EXPECT_EQ(s.GetType(), MediaSegment::AUDIO);

  AudioSegment s2 = dr.Resample(out_frames / 2, &hasUnderrun);
  EXPECT_TRUE(hasUnderrun);
  EXPECT_EQ(s2.GetDuration(), 20u);
  EXPECT_EQ(s2.GetType(), MediaSegment::AUDIO);
  for (AudioSegment::ConstChunkIterator ci(s); !ci.IsEnded(); ci.Next()) {
    EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
  }
}

void printAudioSegment(const AudioSegment& segment);

TEST(TestAudioResampler, ChannelChange_Discontinuity)
{
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());

  uint32_t in_rate = 24000;
  uint32_t out_rate = 48000;

  const float amplitude = 0.5;
  const float frequency = 200;
  const float phase = 0.0;
  float time = 0.0;
  const float deltaTime = 1.0f / static_cast<float>(in_rate);

  uint32_t in_frames = in_rate / 100;
  uint32_t out_frames = out_rate / 100;
  AudioResampler dr(in_rate, out_rate, 0, testPrincipal);

  AudioChunk monoChunk =
      CreateAudioChunk<float>(in_frames, 1, AUDIO_FORMAT_FLOAT32);
  for (uint32_t i = 0; i < monoChunk.GetDuration(); ++i) {
    double value = amplitude * sin(2 * M_PI * frequency * time + phase);
    monoChunk.ChannelDataForWrite<float>(0)[i] = static_cast<float>(value);
    time += deltaTime;
  }
  AudioChunk stereoChunk =
      CreateAudioChunk<float>(in_frames, 2, AUDIO_FORMAT_FLOAT32);
  for (uint32_t i = 0; i < stereoChunk.GetDuration(); ++i) {
    double value = amplitude * sin(2 * M_PI * frequency * time + phase);
    stereoChunk.ChannelDataForWrite<float>(0)[i] = static_cast<float>(value);
    if (stereoChunk.ChannelCount() == 2) {
      stereoChunk.ChannelDataForWrite<float>(1)[i] = value;
    }
    time += deltaTime;
  }

  AudioSegment inSegment;
  inSegment.AppendAndConsumeChunk(std::move(stereoChunk));
  // printAudioSegment(inSegment);

  dr.AppendInput(inSegment);
  bool hasUnderrun = false;
  AudioSegment s = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  // printAudioSegment(s);

  AudioSegment inSegment2;
  inSegment2.AppendAndConsumeChunk(std::move(monoChunk));
  // The resampler here is updated due to the channel change and that creates
  // discontinuity.
  dr.AppendInput(inSegment2);
  AudioSegment s2 = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  // printAudioSegment(s2);

  EXPECT_EQ(s2.GetDuration(), 480);
  EXPECT_EQ(s2.GetType(), MediaSegment::AUDIO);
  EXPECT_EQ(s2.MaxChannelCount(), 1u);
  for (AudioSegment::ConstChunkIterator ci(s2); !ci.IsEnded(); ci.Next()) {
    EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
  }
}

TEST(TestAudioResampler, ChannelChange_Discontinuity2)
{
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());

  uint32_t in_rate = 24000;
  uint32_t out_rate = 48000;

  const float amplitude = 0.5;
  const float frequency = 200;
  const float phase = 0.0;
  float time = 0.0;
  const float deltaTime = 1.0f / static_cast<float>(in_rate);

  uint32_t in_frames = in_rate / 100;
  uint32_t out_frames = out_rate / 100;
  AudioResampler dr(in_rate, out_rate, 10, testPrincipal);

  AudioChunk monoChunk =
      CreateAudioChunk<float>(in_frames / 2, 1, AUDIO_FORMAT_FLOAT32);
  for (uint32_t i = 0; i < monoChunk.GetDuration(); ++i) {
    double value = amplitude * sin(2 * M_PI * frequency * time + phase);
    monoChunk.ChannelDataForWrite<float>(0)[i] = static_cast<float>(value);
    time += deltaTime;
  }
  AudioChunk stereoChunk =
      CreateAudioChunk<float>(in_frames / 2, 2, AUDIO_FORMAT_FLOAT32);
  for (uint32_t i = 0; i < stereoChunk.GetDuration(); ++i) {
    double value = amplitude * sin(2 * M_PI * frequency * time + phase);
    stereoChunk.ChannelDataForWrite<float>(0)[i] = static_cast<float>(value);
    if (stereoChunk.ChannelCount() == 2) {
      stereoChunk.ChannelDataForWrite<float>(1)[i] = value;
    }
    time += deltaTime;
  }

  AudioSegment inSegment;
  inSegment.AppendAndConsumeChunk(std::move(monoChunk));
  inSegment.AppendAndConsumeChunk(std::move(stereoChunk));
  // printAudioSegment(inSegment);

  dr.AppendInput(inSegment);
  bool hasUnderrun = false;
  AudioSegment s1 = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  // printAudioSegment(s1);

  EXPECT_EQ(s1.GetDuration(), 480);
  EXPECT_EQ(s1.GetType(), MediaSegment::AUDIO);
  EXPECT_EQ(s1.MaxChannelCount(), 2u);
  for (AudioSegment::ConstChunkIterator ci(s1); !ci.IsEnded(); ci.Next()) {
    EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
  }

  // The resampler here is updated due to the channel change and that creates
  // discontinuity.
  dr.AppendInput(inSegment);
  AudioSegment s2 = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  // printAudioSegment(s2);

  EXPECT_EQ(s2.GetDuration(), 480);
  EXPECT_EQ(s2.GetType(), MediaSegment::AUDIO);
  EXPECT_EQ(s2.MaxChannelCount(), 2u);
  for (AudioSegment::ConstChunkIterator ci(s2); !ci.IsEnded(); ci.Next()) {
    EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
  }
}

TEST(TestAudioResampler, ChannelChange_Discontinuity3)
{
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());

  uint32_t in_rate = 48000;
  uint32_t out_rate = 48000;

  const float amplitude = 0.5;
  const float frequency = 200;
  const float phase = 0.0;
  float time = 0.0;
  const float deltaTime = 1.0f / static_cast<float>(in_rate);

  uint32_t in_frames = in_rate / 100;
  uint32_t out_frames = out_rate / 100;
  AudioResampler dr(in_rate, out_rate, 10, testPrincipal);

  AudioChunk stereoChunk =
      CreateAudioChunk<float>(in_frames, 2, AUDIO_FORMAT_FLOAT32);
  for (uint32_t i = 0; i < stereoChunk.GetDuration(); ++i) {
    double value = amplitude * sin(2 * M_PI * frequency * time + phase);
    stereoChunk.ChannelDataForWrite<float>(0)[i] = static_cast<float>(value);
    if (stereoChunk.ChannelCount() == 2) {
      stereoChunk.ChannelDataForWrite<float>(1)[i] = value;
    }
    time += deltaTime;
  }

  AudioSegment inSegment;
  inSegment.AppendAndConsumeChunk(std::move(stereoChunk));
  // printAudioSegment(inSegment);

  dr.AppendInput(inSegment);
  bool hasUnderrun = false;
  AudioSegment s = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  // printAudioSegment(s);

  EXPECT_EQ(s.GetDuration(), 480);
  EXPECT_EQ(s.GetType(), MediaSegment::AUDIO);
  EXPECT_EQ(s.MaxChannelCount(), 2u);

  // The resampler here is updated due to the rate change. This is because the
  // in and out rate was the same so a pass through logic was used. By updating
  // the in rate to something different than the out rate, the resampler will
  // start being used and discontinuity will exist.
  dr.UpdateInRate(in_rate - 400);
  dr.AppendInput(inSegment);
  AudioSegment s2 = dr.Resample(out_frames, &hasUnderrun);
  EXPECT_FALSE(hasUnderrun);
  // printAudioSegment(s2);

  EXPECT_EQ(s2.GetDuration(), 480);
  EXPECT_EQ(s2.GetType(), MediaSegment::AUDIO);
  EXPECT_EQ(s2.MaxChannelCount(), 2u);
  for (AudioSegment::ConstChunkIterator ci(s2); !ci.IsEnded(); ci.Next()) {
    EXPECT_EQ(ci->mPrincipalHandle, testPrincipal);
  }
}
