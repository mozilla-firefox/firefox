/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioChunkList.h"
#include "gtest/gtest.h"
#include "nsContentUtils.h"

using namespace mozilla;

TEST(TestAudioChunkList, Basic1)
{
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());
  AudioChunkList list(256, 2, testPrincipal);
  list.SetSampleFormat(AUDIO_FORMAT_FLOAT32);
  EXPECT_EQ(list.ChunkCapacity(), 128u);
  EXPECT_EQ(list.TotalCapacity(), 256u);

  AudioChunk& c1 = list.GetNext();
  float* c1_ch1 = c1.ChannelDataForWrite<float>(0);
  float* c1_ch2 = c1.ChannelDataForWrite<float>(1);
  EXPECT_EQ(c1.mPrincipalHandle, testPrincipal);
  EXPECT_EQ(c1.mBufferFormat, AUDIO_FORMAT_FLOAT32);
  for (uint32_t i = 0; i < list.ChunkCapacity(); ++i) {
    c1_ch1[i] = c1_ch2[i] = 0.01f * static_cast<float>(i);
  }
  AudioChunk& c2 = list.GetNext();
  EXPECT_EQ(c2.mPrincipalHandle, testPrincipal);
  EXPECT_EQ(c2.mBufferFormat, AUDIO_FORMAT_FLOAT32);
  EXPECT_NE(c1.mBuffer.get(), c2.mBuffer.get());
  AudioChunk& c3 = list.GetNext();
  EXPECT_EQ(c3.mPrincipalHandle, testPrincipal);
  EXPECT_EQ(c3.mBufferFormat, AUDIO_FORMAT_FLOAT32);
  // Cycle
  EXPECT_EQ(c1.mBuffer.get(), c3.mBuffer.get());
  float* c3_ch1 = c3.ChannelDataForWrite<float>(0);
  float* c3_ch2 = c3.ChannelDataForWrite<float>(1);
  for (uint32_t i = 0; i < list.ChunkCapacity(); ++i) {
    EXPECT_FLOAT_EQ(c1_ch1[i], c3_ch1[i]);
    EXPECT_FLOAT_EQ(c1_ch2[i], c3_ch2[i]);
  }
}

TEST(TestAudioChunkList, Basic2)
{
  const PrincipalHandle testPrincipal(nsContentUtils::GetSystemPrincipal());
  AudioChunkList list(256, 2, testPrincipal);
  list.SetSampleFormat(AUDIO_FORMAT_S16);
  EXPECT_EQ(list.ChunkCapacity(), 256u);
  EXPECT_EQ(list.TotalCapacity(), 512u);

  AudioChunk& c1 = list.GetNext();
  EXPECT_EQ(c1.mPrincipalHandle, testPrincipal);
  EXPECT_EQ(c1.mBufferFormat, AUDIO_FORMAT_S16);
  short* c1_ch1 = c1.ChannelDataForWrite<short>(0);
  short* c1_ch2 = c1.ChannelDataForWrite<short>(1);
  for (uint32_t i = 0; i < list.ChunkCapacity(); ++i) {
    c1_ch1[i] = c1_ch2[i] = static_cast<short>(i);
  }
  AudioChunk& c2 = list.GetNext();
  EXPECT_EQ(c2.mPrincipalHandle, testPrincipal);
  EXPECT_EQ(c2.mBufferFormat, AUDIO_FORMAT_S16);
  EXPECT_NE(c1.mBuffer.get(), c2.mBuffer.get());
  AudioChunk& c3 = list.GetNext();
  EXPECT_EQ(c3.mPrincipalHandle, testPrincipal);
  EXPECT_EQ(c3.mBufferFormat, AUDIO_FORMAT_S16);
  AudioChunk& c4 = list.GetNext();
  EXPECT_EQ(c4.mPrincipalHandle, testPrincipal);
  EXPECT_EQ(c4.mBufferFormat, AUDIO_FORMAT_S16);
  // Cycle
  AudioChunk& c5 = list.GetNext();
  EXPECT_EQ(c5.mPrincipalHandle, testPrincipal);
  EXPECT_EQ(c5.mBufferFormat, AUDIO_FORMAT_S16);
  EXPECT_EQ(c1.mBuffer.get(), c5.mBuffer.get());
  short* c5_ch1 = c5.ChannelDataForWrite<short>(0);
  short* c5_ch2 = c5.ChannelDataForWrite<short>(1);
  for (uint32_t i = 0; i < list.ChunkCapacity(); ++i) {
    EXPECT_EQ(c1_ch1[i], c5_ch1[i]);
    EXPECT_EQ(c1_ch2[i], c5_ch2[i]);
  }
}

TEST(TestAudioChunkList, Basic3)
{
  AudioChunkList list(260, 2, PRINCIPAL_HANDLE_NONE);
  list.SetSampleFormat(AUDIO_FORMAT_FLOAT32);
  EXPECT_EQ(list.ChunkCapacity(), 128u);
  EXPECT_EQ(list.TotalCapacity(), 256u + 128u);

  AudioChunk& c1 = list.GetNext();
  AudioChunk& c2 = list.GetNext();
  EXPECT_NE(c1.mBuffer.get(), c2.mBuffer.get());
  AudioChunk& c3 = list.GetNext();
  EXPECT_NE(c1.mBuffer.get(), c3.mBuffer.get());
  AudioChunk& c4 = list.GetNext();
  EXPECT_EQ(c1.mBuffer.get(), c4.mBuffer.get());
}

TEST(TestAudioChunkList, Basic4)
{
  AudioChunkList list(260, 2, PRINCIPAL_HANDLE_NONE);
  list.SetSampleFormat(AUDIO_FORMAT_S16);
  EXPECT_EQ(list.ChunkCapacity(), 256u);
  EXPECT_EQ(list.TotalCapacity(), 512u + 256u);

  AudioChunk& c1 = list.GetNext();
  AudioChunk& c2 = list.GetNext();
  EXPECT_NE(c1.mBuffer.get(), c2.mBuffer.get());
  AudioChunk& c3 = list.GetNext();
  EXPECT_NE(c1.mBuffer.get(), c3.mBuffer.get());
  AudioChunk& c4 = list.GetNext();
  EXPECT_EQ(c1.mBuffer.get(), c4.mBuffer.get());
}

TEST(TestAudioChunkList, UpdateChannels)
{
  AudioChunkList list(256, 2, PRINCIPAL_HANDLE_NONE);
  list.SetSampleFormat(AUDIO_FORMAT_FLOAT32);

  AudioChunk& c1 = list.GetNext();
  AudioChunk& c2 = list.GetNext();
  EXPECT_EQ(c1.ChannelCount(), 2u);
  EXPECT_EQ(c2.ChannelCount(), 2u);

  // Update to Quad
  list.Update(4);

  AudioChunk& c3 = list.GetNext();
  AudioChunk& c4 = list.GetNext();
  EXPECT_EQ(c3.ChannelCount(), 4u);
  EXPECT_EQ(c4.ChannelCount(), 4u);
}

TEST(TestAudioChunkList, UpdateBetweenMonoAndStereo)
{
  AudioChunkList list(256, 2, PRINCIPAL_HANDLE_NONE);
  list.SetSampleFormat(AUDIO_FORMAT_FLOAT32);

  AudioChunk& c1 = list.GetNext();
  float* c1_ch1 = c1.ChannelDataForWrite<float>(0);
  float* c1_ch2 = c1.ChannelDataForWrite<float>(1);
  for (uint32_t i = 0; i < list.ChunkCapacity(); ++i) {
    c1_ch1[i] = c1_ch2[i] = 0.01f * static_cast<float>(i);
  }

  AudioChunk& c2 = list.GetNext();
  EXPECT_EQ(c1.ChannelCount(), 2u);
  EXPECT_EQ(c2.ChannelCount(), 2u);

  // Downmix to mono
  list.Update(1);

  AudioChunk& c3 = list.GetNext();
  float* c3_ch1 = c3.ChannelDataForWrite<float>(0);
  for (uint32_t i = 0; i < list.ChunkCapacity(); ++i) {
    EXPECT_FLOAT_EQ(c3_ch1[i], c1_ch1[i]);
  }

  AudioChunk& c4 = list.GetNext();
  EXPECT_EQ(c3.ChannelCount(), 1u);
  EXPECT_EQ(c4.ChannelCount(), 1u);
  EXPECT_EQ(static_cast<SharedChannelArrayBuffer<float>*>(c3.mBuffer.get())
                ->mBuffers[0]
                .Length(),
            list.ChunkCapacity());

  // Upmix to stereo
  list.Update(2);

  AudioChunk& c5 = list.GetNext();
  AudioChunk& c6 = list.GetNext();
  EXPECT_EQ(c5.ChannelCount(), 2u);
  EXPECT_EQ(c6.ChannelCount(), 2u);
  EXPECT_EQ(static_cast<SharedChannelArrayBuffer<float>*>(c5.mBuffer.get())
                ->mBuffers[0]
                .Length(),
            list.ChunkCapacity());
  EXPECT_EQ(static_cast<SharedChannelArrayBuffer<float>*>(c5.mBuffer.get())
                ->mBuffers[1]
                .Length(),
            list.ChunkCapacity());

  // Downmix to mono
  list.Update(1);

  AudioChunk& c7 = list.GetNext();
  float* c7_ch1 = c7.ChannelDataForWrite<float>(0);
  for (uint32_t i = 0; i < list.ChunkCapacity(); ++i) {
    EXPECT_FLOAT_EQ(c7_ch1[i], c1_ch1[i]);
  }

  AudioChunk& c8 = list.GetNext();
  EXPECT_EQ(c7.ChannelCount(), 1u);
  EXPECT_EQ(c8.ChannelCount(), 1u);
  EXPECT_EQ(static_cast<SharedChannelArrayBuffer<float>*>(c7.mBuffer.get())
                ->mBuffers[0]
                .Length(),
            list.ChunkCapacity());
}

TEST(TestAudioChunkList, ConsumeAndForget)
{
  AudioSegment s;
  AudioChunkList list(256, 2, PRINCIPAL_HANDLE_NONE);
  list.SetSampleFormat(AUDIO_FORMAT_FLOAT32);

  AudioChunk& c1 = list.GetNext();
  AudioChunk tmp1 = c1;
  s.AppendAndConsumeChunk(std::move(tmp1));
  EXPECT_FALSE(c1.mBuffer.get() == nullptr);
  EXPECT_EQ(c1.ChannelData<float>().Length(), 2u);

  AudioChunk& c2 = list.GetNext();
  AudioChunk tmp2 = c2;
  s.AppendAndConsumeChunk(std::move(tmp2));
  EXPECT_FALSE(c2.mBuffer.get() == nullptr);
  EXPECT_EQ(c2.ChannelData<float>().Length(), 2u);

  s.ForgetUpTo(256);
  list.GetNext();
  list.GetNext();
}
