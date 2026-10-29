/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_coding/test/TestVADDTX.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "api/audio/audio_frame.h"
#include "api/audio_codecs/audio_decoder_factory_template.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_encoder_factory_template.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/opus/audio_decoder_opus.h"
#include "api/audio_codecs/opus/audio_encoder_opus.h"
#include "api/neteq/default_neteq_factory.h"
#include "api/neteq/neteq.h"
#include "common_audio/vad/include/vad.h"
#include "modules/audio_coding/codecs/cng/audio_encoder_cng.h"
#include "modules/audio_coding/include/audio_coding_module.h"
#include "modules/audio_coding/include/audio_coding_module_typedefs.h"
#include "modules/audio_coding/test/Channel.h"
#include "modules/audio_coding/test/PCMFile.h"
#include "rtc_base/strings/string_builder.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"

namespace webrtc {

MonitoringAudioPacketizationCallback::MonitoringAudioPacketizationCallback(
    AudioPacketizationCallback* next)
    : next_(next) {
  ResetStatistics();
}

int32_t MonitoringAudioPacketizationCallback::SendData(
    AudioFrameType frame_type,
    uint8_t payload_type,
    uint32_t timestamp,
    const uint8_t* payload_data,
    size_t payload_len_bytes,
    int64_t absolute_capture_timestamp_ms) {
  counter_[static_cast<int>(frame_type)]++;
  return next_->SendData(frame_type, payload_type, timestamp, payload_data,
                         payload_len_bytes, absolute_capture_timestamp_ms);
}

void MonitoringAudioPacketizationCallback::PrintStatistics() {
  printf("\n");
  printf("kEmptyFrame       %u\n",
         counter_[static_cast<int>(AudioFrameType::kEmptyFrame)]);
  printf("kAudioFrameSpeech %u\n",
         counter_[static_cast<int>(AudioFrameType::kAudioFrameSpeech)]);
  printf("kAudioFrameCN     %u\n",
         counter_[static_cast<int>(AudioFrameType::kAudioFrameCN)]);
  printf("\n\n");
}

void MonitoringAudioPacketizationCallback::ResetStatistics() {
  memset(counter_, 0, sizeof(counter_));
}

void MonitoringAudioPacketizationCallback::GetStatistics(uint32_t* counter) {
  memcpy(counter, counter_, sizeof(counter_));
}

TestVadDtx::TestVadDtx()
    : env_(CreateTestEnvironment()),
      encoder_factory_(CreateAudioEncoderFactory<AudioEncoderOpus>()),
      decoder_factory_(CreateAudioDecoderFactory<AudioDecoderOpus>()),
      acm_send_(AudioCodingModule::Create()),
      neteq_(DefaultNetEqFactory().Create(env_,
                                          NetEq::Config(),
                                          decoder_factory_)),
      channel_(std::make_unique<Channel>()),
      packetization_callback_(
          std::make_unique<MonitoringAudioPacketizationCallback>(
              channel_.get())) {
  EXPECT_EQ(
      0, acm_send_->RegisterTransportCallback(packetization_callback_.get()));
  channel_->RegisterReceiverNetEq(neteq_.get());
}

bool TestVadDtx::RegisterCodec(const SdpAudioFormat& codec_format,
                               std::optional<Vad::Aggressiveness> vad_mode) {
  constexpr int payload_type = 17, cn_payload_type = 117;
  bool added_comfort_noise = false;

  auto encoder = encoder_factory_->Create(env_, codec_format,
                                          {.payload_type = payload_type});
  if (vad_mode.has_value() &&
      !absl::EqualsIgnoreCase(codec_format.name, "opus")) {
    AudioEncoderCngConfig config;
    config.speech_encoder = std::move(encoder);
    config.num_channels = 1;
    config.payload_type = cn_payload_type;
    config.vad_mode = vad_mode.value();
    encoder = CreateComfortNoiseEncoder(std::move(config));
    added_comfort_noise = true;
  }
  channel_->SetIsStereo(encoder->NumChannels() > 1);
  acm_send_->SetEncoder(std::move(encoder));

  std::map<int, SdpAudioFormat> receive_codecs = {{payload_type, codec_format}};
  neteq_->SetCodecs(receive_codecs);

  return added_comfort_noise;
}

// Encoding a file and see if the numbers that various packets occur follow
// the expectation.
void TestVadDtx::Run(absl::string_view in_filename,
                     int frequency,
                     int channels,
                     absl::string_view out_filename,
                     bool append,
                     const int* expects) {
  packetization_callback_->ResetStatistics();

  PCMFile in_file;
  in_file.Open(in_filename, frequency, "rb");
  in_file.ReadStereo(channels > 1);
  // Set test length to 1000 ms (100 blocks of 10 ms each).
  in_file.SetNum10MsBlocksToRead(100);
  // Fast-forward both files 500 ms (50 blocks). The first second of the file is
  // silence, but we want to keep half of that to test silence periods.
  in_file.FastForward(50);

  PCMFile out_file;
  if (append) {
    out_file.Open(out_filename, kOutputFreqHz, "ab");
  } else {
    out_file.Open(out_filename, kOutputFreqHz, "wb");
  }

  uint16_t frame_size_samples = in_file.PayloadLength10Ms();
  AudioFrame audio_frame;
  while (!in_file.EndOfFile()) {
    in_file.Read10MsData(audio_frame);
    audio_frame.timestamp_ = time_stamp_;
    time_stamp_ += frame_size_samples;
    EXPECT_GE(acm_send_->Add10MsData(audio_frame), 0);
    bool muted;
    neteq_->GetAudio(&audio_frame, &muted);
    resampler_helper_.MaybeResample(kOutputFreqHz, &audio_frame);
    ASSERT_FALSE(muted);
    out_file.Write10MsData(audio_frame);
  }

  in_file.Close();
  out_file.Close();

#ifdef PRINT_STAT
  packetization_callback_->PrintStatistics();
#endif

  uint32_t stats[3];
  packetization_callback_->GetStatistics(stats);
  packetization_callback_->ResetStatistics();

  for (const auto& st : stats) {
    int i = &st - stats;  // Calculate the current position in stats.
    switch (expects[i]) {
      case 0: {
        EXPECT_EQ(0u, st) << "stats[" << i << "] error.";
        break;
      }
      case 1: {
        EXPECT_GT(st, 0u) << "stats[" << i << "] error.";
        break;
      }
    }
  }
}

// Following is the implementation of TestWebRtcVadDtx.
TestWebRtcVadDtx::TestWebRtcVadDtx() : output_file_num_(0) {}

void TestWebRtcVadDtx::Perform() {
  RunTestCases({"opus", 48000, 2});
}

// Test various configurations on VAD/DTX.
void TestWebRtcVadDtx::RunTestCases(const SdpAudioFormat& codec_format) {
  Test(/*new_outfile=*/true,
       /*expect_dtx_enabled=*/RegisterCodec(codec_format, std::nullopt));

  Test(/*new_outfile=*/false,
       /*expect_dtx_enabled=*/RegisterCodec(codec_format, Vad::kVadAggressive));

  Test(/*new_outfile=*/false,
       /*expect_dtx_enabled=*/RegisterCodec(codec_format, Vad::kVadLowBitrate));

  Test(/*new_outfile=*/false, /*expect_dtx_enabled=*/RegisterCodec(
           codec_format, Vad::kVadVeryAggressive));

  Test(/*new_outfile=*/false,
       /*expect_dtx_enabled=*/RegisterCodec(codec_format, Vad::kVadNormal));
  EXPECT_TRUE(test::RemoveFile(out_filename_));
}

// Set the expectation and run the test.
void TestWebRtcVadDtx::Test(bool new_outfile, bool expect_dtx_enabled) {
  int expects[] = {-1, 1, expect_dtx_enabled, 0, 0};
  if (new_outfile || out_filename_.empty()) {
    if (!out_filename_.empty()) {
      EXPECT_TRUE(test::RemoveFile(out_filename_));
    }
    output_file_num_++;
    StringBuilder out_filename;
    out_filename << "testWebRtcVadDtx_outFile_" << output_file_num_ << ".pcm";
    out_filename_ = test::TempFilename(test::OutputPath(), out_filename.str());
  }
  Run(test::ResourcePath("audio_coding/testfile32kHz", "pcm"), 32000, 1,
      out_filename_, !new_outfile, expects);
}

// Following is the implementation of TestOpusDtx.
void TestOpusDtx::Perform() {
  int expects[] = {0, 1, 0, 0, 0};

  // Register Opus as send codec
  std::string out_filename =
      test::TempFilename(test::OutputPath(), "testOpusDtx_outFile_mono.pcm");
  RegisterCodec({"opus", 48000, 2}, std::nullopt);
  acm_send_->ModifyEncoder([](std::unique_ptr<AudioEncoder>* encoder_ptr) {
    (*encoder_ptr)->SetDtx(false);
  });

  Run(test::ResourcePath("audio_coding/testfile32kHz", "pcm"), 32000, 1,
      out_filename, false, expects);

  acm_send_->ModifyEncoder([](std::unique_ptr<AudioEncoder>* encoder_ptr) {
    (*encoder_ptr)->SetDtx(true);
  });
  expects[static_cast<int>(AudioFrameType::kEmptyFrame)] = 1;
  expects[static_cast<int>(AudioFrameType::kAudioFrameCN)] = 1;
  Run(test::ResourcePath("audio_coding/testfile32kHz", "pcm"), 32000, 1,
      out_filename, true, expects);
  EXPECT_TRUE(test::RemoveFile(out_filename));
}

}  // namespace webrtc
