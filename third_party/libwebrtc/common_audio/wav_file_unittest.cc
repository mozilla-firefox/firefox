/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "common_audio/wav_file.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numbers>
#include <string>

#include "common_audio/wav_header.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"

// WavWriterTest.CPP flaky on Mac. See webrtc:9247.
#if defined(WEBRTC_MAC)
#define MAYBE_CPP DISABLED_CPP
#define MAYBE_CPPReset DISABLED_CPPReset
#else
#define MAYBE_CPP CPP
#define MAYBE_CPPReset CPPReset
#endif

namespace webrtc {

namespace {
const char* SampleFormatToStr(WavFile::SampleFormat format) {
  switch (format) {
    case WavFile::SampleFormat::kInt16:
      return "int16";
    case WavFile::SampleFormat::kFloat:
      return "float";
  }
  RTC_CHECK_NOTREACHED();
}
}  // namespace

static const float kSamples[] = {0.0, 10.0, 4e4, -1e9};

// Write a tiny WAV file with the C++ interface and verify the result.
TEST(WavWriterTest, MAYBE_CPP) {
  const std::string outdir = test::OutputPathWithRandomDirectory();
  const std::string outfile = outdir + "wavtest1.wav";
  static const size_t kNumSamples = 3;
  {
    WavWriter w(outfile, 14099, 1);
    EXPECT_EQ(14099, w.sample_rate());
    EXPECT_EQ(1u, w.num_channels());
    EXPECT_EQ(0u, w.num_samples());
    w.WriteSamples(kSamples, kNumSamples);
    EXPECT_EQ(kNumSamples, w.num_samples());
  }
  // Write some extra "metadata" to the file that should be silently ignored
  // by WavReader. We don't use WavWriter directly for this because it doesn't
  // support metadata.
  static const uint8_t kMetadata[] = {101, 202};
  {
    FILE* f = fopen(outfile.c_str(), "ab");
    ASSERT_TRUE(f);
    ASSERT_EQ(1u, fwrite(kMetadata, sizeof(kMetadata), 1, f));
    fclose(f);
  }
  static const uint8_t kExpectedContents[] = {
      // clang-format off
      // clang formatting doesn't respect inline comments.
    'R', 'I', 'F', 'F',
    42, 0, 0, 0,  // size of whole file - 8: 6 + 44 - 8
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    16, 0, 0, 0,  // size of fmt block - 8: 24 - 8
    1, 0,  // format: PCM (1)
    1, 0,  // channels: 1
    0x13, 0x37, 0, 0,  // sample rate: 14099
    0x26, 0x6e, 0, 0,  // byte rate: 2 * 14099
    2, 0,  // block align: NumChannels * BytesPerSample
    16, 0,  // bits per sample: 2 * 8
    'd', 'a', 't', 'a',
    6, 0, 0, 0,  // size of payload: 6
    0, 0,  // first sample: 0.0
    10, 0,  // second sample: 10.0
    0xff, 0x7f,  // third sample: 4e4 (saturated)
    kMetadata[0], kMetadata[1],
      // clang-format on
  };
  static const size_t kContentSize =
      kPcmWavHeaderSize + kNumSamples * sizeof(int16_t) + sizeof(kMetadata);
  static_assert(sizeof(kExpectedContents) == kContentSize, "content size");
  EXPECT_EQ(kContentSize, test::GetFileSize(outfile));
  FILE* f = fopen(outfile.c_str(), "rb");
  ASSERT_TRUE(f);
  uint8_t contents[kContentSize];
  ASSERT_EQ(1u, fread(contents, kContentSize, 1, f));
  EXPECT_EQ(0, fclose(f));
  EXPECT_EQ(0, memcmp(kExpectedContents, contents, kContentSize));

  {
    WavReader r(outfile);
    EXPECT_EQ(14099, r.sample_rate());
    EXPECT_EQ(1u, r.num_channels());
    EXPECT_EQ(kNumSamples, r.num_samples());
    static const float kTruncatedSamples[] = {0.0, 10.0, 32767.0};
    float samples[kNumSamples];
    EXPECT_EQ(kNumSamples, r.ReadSamples(kNumSamples, samples));
    EXPECT_EQ(0, memcmp(kTruncatedSamples, samples, sizeof(samples)));
    EXPECT_EQ(0u, r.ReadSamples(kNumSamples, samples));
  }
  EXPECT_TRUE(test::RemoveFile(outfile));
  EXPECT_TRUE(test::RemoveDir(outdir));
}

// Write a larger WAV file. You can listen to this file to sanity-check it.
TEST(WavWriterTest, LargeFile) {
  constexpr int kSampleRate = 8000;
  constexpr size_t kNumChannels = 2;
  constexpr size_t kNumSamples = 3 * kSampleRate * kNumChannels;
  for (WavFile::SampleFormat wav_format :
       {WavFile::SampleFormat::kInt16, WavFile::SampleFormat::kFloat}) {
    for (WavFile::SampleFormat write_format :
         {WavFile::SampleFormat::kInt16, WavFile::SampleFormat::kFloat}) {
      for (WavFile::SampleFormat read_format :
           {WavFile::SampleFormat::kInt16, WavFile::SampleFormat::kFloat}) {
        std::string outdir = test::OutputPathWithRandomDirectory();
        std::string outfile = outdir + "wavtest3.wav";
        float samples[kNumSamples];
        for (size_t i = 0; i < kNumSamples; i += kNumChannels) {
          // A nice periodic beeping sound.
          static const double kToneHz = 440;
          const double t =
              static_cast<double>(i) / (kNumChannels * kSampleRate);
          const double x = std::numeric_limits<int16_t>::max() *
                           std::sin(t * kToneHz * 2 * std::numbers::pi);
          samples[i] = std::pow(std::sin(t * 2 * 2 * std::numbers::pi), 10) * x;
          samples[i + 1] =
              std::pow(std::cos(t * 2 * 2 * std::numbers::pi), 10) * x;
          // See https://issues.webrtc.org/issues/379973428
          RTC_CHECK(std::isfinite(samples[i]));
          RTC_CHECK(std::isfinite(samples[i + 1]));
        }
        {
          WavWriter w(outfile, kSampleRate, kNumChannels, wav_format);
          EXPECT_EQ(kSampleRate, w.sample_rate());
          EXPECT_EQ(kNumChannels, w.num_channels());
          EXPECT_EQ(0u, w.num_samples());
          if (write_format == WavFile::SampleFormat::kFloat) {
            int16_t truncated_samples[kNumSamples];
            for (size_t k = 0; k < kNumSamples; ++k) {
              truncated_samples[k] = static_cast<int16_t>(samples[k]);
            }
            w.WriteSamples(truncated_samples, kNumSamples);
          } else {
            w.WriteSamples(samples, kNumSamples);
          }
          EXPECT_EQ(kNumSamples, w.num_samples());
        }
        if (wav_format == WavFile::SampleFormat::kFloat) {
          EXPECT_EQ(sizeof(float) * kNumSamples + kIeeeFloatWavHeaderSize,
                    test::GetFileSize(outfile));
        } else {
          EXPECT_EQ(sizeof(int16_t) * kNumSamples + kPcmWavHeaderSize,
                    test::GetFileSize(outfile));
        }

        {
          WavReader r(outfile);
          EXPECT_EQ(kSampleRate, r.sample_rate());
          EXPECT_EQ(kNumChannels, r.num_channels());
          EXPECT_EQ(kNumSamples, r.num_samples());

          if (read_format == WavFile::SampleFormat::kFloat) {
            float read_samples[kNumSamples];
            EXPECT_EQ(kNumSamples, r.ReadSamples(kNumSamples, read_samples));
            for (size_t i = 0; i < kNumSamples; ++i) {
              EXPECT_NEAR(samples[i], read_samples[i], 1);
              if (!std::isfinite(samples[i])) {
                // See https://issues.webrtc.org/issues/379973428
                RTC_LOG(LS_ERROR)
                    << "samples[" << i << "] is not finite. "
                    << "wav_format=" << SampleFormatToStr(wav_format)
                    << ", write_format=" << SampleFormatToStr(write_format)
                    << ", read_format=" << SampleFormatToStr(read_format);
              }
            }
            EXPECT_EQ(0u, r.ReadSamples(kNumSamples, read_samples));
          } else {
            int16_t read_samples[kNumSamples];
            EXPECT_EQ(kNumSamples, r.ReadSamples(kNumSamples, read_samples));
            for (size_t i = 0; i < kNumSamples; ++i) {
              EXPECT_NEAR(samples[i], static_cast<float>(read_samples[i]), 1);
              if (!std::isfinite(samples[i])) {
                // See https://issues.webrtc.org/issues/379973428
                RTC_LOG(LS_ERROR)
                    << "samples[" << i << "] is not finite. "
                    << "wav_format=" << SampleFormatToStr(wav_format)
                    << ", write_format=" << SampleFormatToStr(write_format)
                    << ", read_format=" << SampleFormatToStr(read_format);
              }
            }
            EXPECT_EQ(0u, r.ReadSamples(kNumSamples, read_samples));
          }
        }
        EXPECT_TRUE(test::RemoveFile(outfile));
        EXPECT_TRUE(test::RemoveDir(outdir));
      }
    }
  }
}

// Write a tiny WAV file with the C++ interface then read-reset-read.
TEST(WavReaderTest, MAYBE_CPPReset) {
  const std::string outdir = test::OutputPathWithRandomDirectory();
  const std::string outfile = outdir + "wavtest4.wav";
  static const size_t kNumSamples = 3;
  {
    WavWriter w(outfile, 14099, 1);
    EXPECT_EQ(14099, w.sample_rate());
    EXPECT_EQ(1u, w.num_channels());
    EXPECT_EQ(0u, w.num_samples());
    w.WriteSamples(kSamples, kNumSamples);
    EXPECT_EQ(kNumSamples, w.num_samples());
  }
  // Write some extra "metadata" to the file that should be silently ignored
  // by WavReader. We don't use WavWriter directly for this because it doesn't
  // support metadata.
  static const uint8_t kMetadata[] = {101, 202};
  {
    FILE* f = fopen(outfile.c_str(), "ab");
    ASSERT_TRUE(f);
    ASSERT_EQ(1u, fwrite(kMetadata, sizeof(kMetadata), 1, f));
    fclose(f);
  }
  static const uint8_t kExpectedContents[] = {
      // clang-format off
      // clang formatting doesn't respect inline comments.
    'R', 'I', 'F', 'F',
    42, 0, 0, 0,  // size of whole file - 8: 6 + 44 - 8
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    16, 0, 0, 0,  // size of fmt block - 8: 24 - 8
    1, 0,  // format: PCM (1)
    1, 0,  // channels: 1
    0x13, 0x37, 0, 0,  // sample rate: 14099
    0x26, 0x6e, 0, 0,  // byte rate: 2 * 14099
    2, 0,  // block align: NumChannels * BytesPerSample
    16, 0,  // bits per sample: 2 * 8
    'd', 'a', 't', 'a',
    6, 0, 0, 0,  // size of payload: 6
    0, 0,  // first sample: 0.0
    10, 0,  // second sample: 10.0
    0xff, 0x7f,  // third sample: 4e4 (saturated)
    kMetadata[0], kMetadata[1],
      // clang-format on
  };
  static const size_t kContentSize =
      kPcmWavHeaderSize + kNumSamples * sizeof(int16_t) + sizeof(kMetadata);
  static_assert(sizeof(kExpectedContents) == kContentSize, "content size");
  EXPECT_EQ(kContentSize, test::GetFileSize(outfile));
  FILE* f = fopen(outfile.c_str(), "rb");
  ASSERT_TRUE(f);
  uint8_t contents[kContentSize];
  ASSERT_EQ(1u, fread(contents, kContentSize, 1, f));
  EXPECT_EQ(0, fclose(f));
  EXPECT_EQ(0, memcmp(kExpectedContents, contents, kContentSize));

  {
    WavReader r(outfile);
    EXPECT_EQ(14099, r.sample_rate());
    EXPECT_EQ(1u, r.num_channels());
    EXPECT_EQ(kNumSamples, r.num_samples());
    static const float kTruncatedSamples[] = {0.0, 10.0, 32767.0};
    float samples[kNumSamples];
    EXPECT_EQ(kNumSamples, r.ReadSamples(kNumSamples, samples));
    EXPECT_EQ(0, memcmp(kTruncatedSamples, samples, sizeof(samples)));
    EXPECT_EQ(0u, r.ReadSamples(kNumSamples, samples));

    r.Reset();
    EXPECT_EQ(kNumSamples, r.ReadSamples(kNumSamples, samples));
    EXPECT_EQ(0, memcmp(kTruncatedSamples, samples, sizeof(samples)));
    EXPECT_EQ(0u, r.ReadSamples(kNumSamples, samples));
  }
  EXPECT_TRUE(test::RemoveFile(outfile));
  EXPECT_TRUE(test::RemoveDir(outdir));
}

}  // namespace webrtc
