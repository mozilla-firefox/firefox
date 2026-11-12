/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_coding/test/opus_test.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "api/audio/audio_frame.h"
#include "api/audio/audio_view.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/neteq/default_neteq_factory.h"
#include "api/neteq/neteq.h"
#include "modules/audio_coding/codecs/opus/opus_interface.h"
#include "modules/audio_coding/include/audio_coding_module_typedefs.h"
#include "modules/audio_coding/test/TestStereo.h"
#include "rtc_base/strings/string_builder.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"

namespace webrtc {

OpusTest::OpusTest()
    : neteq_(DefaultNetEqFactory().Create(CreateTestEnvironment(),
                                          NetEq::Config(),
                                          CreateBuiltinAudioDecoderFactory())),
      channel_a2b_(nullptr),
      counter_(0),
      payload_type_(255),
      rtp_timestamp_(0) {}

OpusTest::~OpusTest() {
  if (channel_a2b_ != nullptr) {
    delete channel_a2b_;
    channel_a2b_ = nullptr;
  }
  if (opus_mono_encoder_ != nullptr) {
    WebRtcOpus_EncoderFree(opus_mono_encoder_);
    opus_mono_encoder_ = nullptr;
  }
  if (opus_stereo_encoder_ != nullptr) {
    WebRtcOpus_EncoderFree(opus_stereo_encoder_);
    opus_stereo_encoder_ = nullptr;
  }
  if (opus_mono_decoder_ != nullptr) {
    WebRtcOpus_DecoderFree(opus_mono_decoder_);
    opus_mono_decoder_ = nullptr;
  }
  if (opus_stereo_decoder_ != nullptr) {
    WebRtcOpus_DecoderFree(opus_stereo_decoder_);
    opus_stereo_decoder_ = nullptr;
  }
}

void OpusTest::Perform() {
#ifndef WEBRTC_CODEC_OPUS
  // Opus isn't defined, exit.
  return;
#else
  uint16_t frequency_hz;
  size_t audio_channels;
  int16_t test_cntr = 0;

  // Open both mono and stereo test files in 32 kHz.
  const std::string file_name_stereo =
      test::ResourcePath("audio_coding/teststereo32kHz", "pcm");
  const std::string file_name_mono =
      test::ResourcePath("audio_coding/testfile32kHz", "pcm");
  frequency_hz = 32000;
  in_file_stereo_.Open(file_name_stereo, frequency_hz, "rb");
  in_file_stereo_.ReadStereo(true);
  in_file_mono_.Open(file_name_mono, frequency_hz, "rb");
  in_file_mono_.ReadStereo(false);

  // Create Opus encoders for mono and stereo.
  ASSERT_GT(WebRtcOpus_EncoderCreate(&opus_mono_encoder_, 1, 0, 48000), -1);
  ASSERT_GT(WebRtcOpus_EncoderCreate(&opus_stereo_encoder_, 2, 1, 48000), -1);

  // Create Opus decoders for mono and stereo for stand-alone testing of Opus.
  ASSERT_GT(WebRtcOpus_DecoderCreate(&opus_mono_decoder_, 1, 48000), -1);
  ASSERT_GT(WebRtcOpus_DecoderCreate(&opus_stereo_decoder_, 2, 48000), -1);
  WebRtcOpus_DecoderInit(opus_mono_decoder_);
  WebRtcOpus_DecoderInit(opus_stereo_decoder_);

  ASSERT_TRUE(neteq_.get() != nullptr);
  neteq_->FlushBuffers();

  // Register Opus stereo as receiving codec.
  constexpr int kOpusPayloadType = 120;
  const SdpAudioFormat kOpusFormatStereo("opus", 48000, 2, {{"stereo", "1"}});
  payload_type_ = kOpusPayloadType;
  neteq_->SetCodecs({{kOpusPayloadType, kOpusFormatStereo}});

  // Create and connect the channel.
  channel_a2b_ = new TestPackStereo;
  channel_a2b_->RegisterReceiverNetEq(neteq_.get());

  //
  // Test Stereo.
  //

  channel_a2b_->set_codec_mode(kStereo);
  audio_channels = 2;
  test_cntr++;
  OpenOutFile(test_cntr);

  // Run Opus with 2.5 ms frame size.
  Run(channel_a2b_, audio_channels, 64000, 120);

  // Run Opus with 5 ms frame size.
  Run(channel_a2b_, audio_channels, 64000, 240);

  // Run Opus with 10 ms frame size.
  Run(channel_a2b_, audio_channels, 64000, 480);

  // Run Opus with 20 ms frame size.
  Run(channel_a2b_, audio_channels, 64000, 960);

  // Run Opus with 40 ms frame size.
  Run(channel_a2b_, audio_channels, 64000, 1920);

  // Run Opus with 60 ms frame size.
  Run(channel_a2b_, audio_channels, 64000, 2880);

  CloseOutFile();

  //
  // Test Opus stereo with packet-losses.
  //

  test_cntr++;
  OpenOutFile(test_cntr);

  // Run Opus with 20 ms frame size, 1% packet loss.
  Run(channel_a2b_, audio_channels, 64000, 960, 1);

  // Run Opus with 20 ms frame size, 5% packet loss.
  Run(channel_a2b_, audio_channels, 64000, 960, 5);

  // Run Opus with 20 ms frame size, 10% packet loss.
  Run(channel_a2b_, audio_channels, 64000, 960, 10);

  CloseOutFile();

  //
  // Test Mono.
  //
  channel_a2b_->set_codec_mode(kMono);
  audio_channels = 1;
  test_cntr++;
  OpenOutFile(test_cntr);

  // Register Opus mono as receiving codec.
  const SdpAudioFormat kOpusFormatMono("opus", 48000, 2);
  neteq_->SetCodecs({{kOpusPayloadType, kOpusFormatMono}});

  // Run Opus with 2.5 ms frame size.
  Run(channel_a2b_, audio_channels, 32000, 120);

  // Run Opus with 5 ms frame size.
  Run(channel_a2b_, audio_channels, 32000, 240);

  // Run Opus with 10 ms frame size.
  Run(channel_a2b_, audio_channels, 32000, 480);

  // Run Opus with 20 ms frame size.
  Run(channel_a2b_, audio_channels, 32000, 960);

  // Run Opus with 40 ms frame size.
  Run(channel_a2b_, audio_channels, 32000, 1920);

  // Run Opus with 60 ms frame size.
  Run(channel_a2b_, audio_channels, 32000, 2880);

  CloseOutFile();

  //
  // Test Opus mono with packet-losses.
  //
  test_cntr++;
  OpenOutFile(test_cntr);

  // Run Opus with 20 ms frame size, 1% packet loss.
  Run(channel_a2b_, audio_channels, 64000, 960, 1);

  // Run Opus with 20 ms frame size, 5% packet loss.
  Run(channel_a2b_, audio_channels, 64000, 960, 5);

  // Run Opus with 20 ms frame size, 10% packet loss.
  Run(channel_a2b_, audio_channels, 64000, 960, 10);

  // Close the files.
  in_file_stereo_.Close();
  in_file_mono_.Close();
  CloseOutFile();
#endif
}

void OpusTest::Run(TestPackStereo* channel,
                   size_t channels,
                   int bitrate,
                   size_t frame_length,
                   int percent_loss) {
  AudioFrame audio_frame;
  int32_t out_freq_hz_b = out_file_.SamplingFrequency();
  const size_t kBufferSizeSamples = 480 * 12 * 2;  // 120 ms stereo audio.
  int16_t audio[kBufferSizeSamples];
  int16_t out_audio[kBufferSizeSamples];
  int16_t audio_type;
  size_t written_samples = 0;
  size_t read_samples = 0;
  size_t decoded_samples = 0;
  bool first_packet = true;
  uint32_t start_time_stamp = 0;

  channel->reset_payload_size();
  counter_ = 0;

  // Set encoder rate.
  EXPECT_EQ(0, WebRtcOpus_SetBitRate(opus_mono_encoder_, bitrate));
  EXPECT_EQ(0, WebRtcOpus_SetBitRate(opus_stereo_encoder_, bitrate));

#if defined(WEBRTC_ANDROID) || defined(WEBRTC_IOS) || defined(WEBRTC_ARCH_ARM)
  // If we are on Android, iOS and/or ARM, use a lower complexity setting as
  // default.
  const int kOpusComplexity5 = 5;
  EXPECT_EQ(0, WebRtcOpus_SetComplexity(opus_mono_encoder_, kOpusComplexity5));
  EXPECT_EQ(0,
            WebRtcOpus_SetComplexity(opus_stereo_encoder_, kOpusComplexity5));
#endif

  // Fast-forward 1 second (100 blocks) since the files start with silence.
  in_file_stereo_.FastForward(100);
  in_file_mono_.FastForward(100);

  // Limit the runtime to 1000 blocks of 10 ms each.
  for (size_t audio_length = 0; audio_length < 1000; audio_length += 10) {
    bool lost_packet = false;

    // Get 10 msec of audio.
    if (channels == 1) {
      if (in_file_mono_.EndOfFile()) {
        break;
      }
      in_file_mono_.Read10MsData(audio_frame);
    } else {
      if (in_file_stereo_.EndOfFile()) {
        break;
      }
      in_file_stereo_.Read10MsData(audio_frame);
    }

    // If input audio is sampled at 32 kHz, resampling to 48 kHz is required.
    InterleavedView<int16_t> dst(&audio[written_samples], 480, channels);
    resampler_.Resample(audio_frame.data_view(), dst);
    written_samples += dst.size();

    // Sometimes we need to loop over the audio vector to produce the right
    // number of packets.
    size_t loop_encode =
        (written_samples - read_samples) / (channels * frame_length);

    if (loop_encode > 0) {
      const size_t kMaxBytes = 1000;  // Maximum number of bytes for one packet.
      size_t bitstream_len_byte;
      uint8_t bitstream[kMaxBytes];
      for (size_t i = 0; i < loop_encode; i++) {
        int bitstream_len_byte_int = WebRtcOpus_Encode(
            (channels == 1) ? opus_mono_encoder_ : opus_stereo_encoder_,
            &audio[read_samples], frame_length, kMaxBytes, bitstream);
        ASSERT_GE(bitstream_len_byte_int, 0);
        bitstream_len_byte = static_cast<size_t>(bitstream_len_byte_int);

        // Simulate packet loss by setting `packet_loss_` to "true" in
        // `percent_loss` percent of the loops.
        // TODO(tlegrand): Move handling of loss simulation to TestPackStereo.
        if (percent_loss > 0) {
          if (counter_ == floor((100 / percent_loss) + 0.5)) {
            counter_ = 0;
            lost_packet = true;
            channel->set_lost_packet(true);
          } else {
            lost_packet = false;
            channel->set_lost_packet(false);
          }
          counter_++;
        }

        // Run stand-alone Opus decoder, or decode PLC.
        if (channels == 1) {
          if (!lost_packet) {
            decoded_samples += WebRtcOpus_Decode(
                opus_mono_decoder_, bitstream, bitstream_len_byte,
                &out_audio[decoded_samples * channels], &audio_type);
          } else {
            // Call decoder PLC.
            constexpr int kPlcDurationMs = 10;
            constexpr int kPlcSamples = 48 * kPlcDurationMs;
            size_t total_plc_samples = 0;
            while (total_plc_samples < frame_length) {
              int ret = WebRtcOpus_Decode(
                  opus_mono_decoder_, nullptr, 0,
                  &out_audio[decoded_samples * channels], &audio_type);
              EXPECT_EQ(ret, kPlcSamples);
              decoded_samples += ret;
              total_plc_samples += ret;
            }
            EXPECT_EQ(total_plc_samples, frame_length);
          }
        } else {
          if (!lost_packet) {
            decoded_samples += WebRtcOpus_Decode(
                opus_stereo_decoder_, bitstream, bitstream_len_byte,
                &out_audio[decoded_samples * channels], &audio_type);
          } else {
            // Call decoder PLC.
            constexpr int kPlcDurationMs = 10;
            constexpr int kPlcSamples = 48 * kPlcDurationMs;
            size_t total_plc_samples = 0;
            while (total_plc_samples < frame_length) {
              int ret = WebRtcOpus_Decode(
                  opus_stereo_decoder_, nullptr, 0,
                  &out_audio[decoded_samples * channels], &audio_type);
              EXPECT_EQ(ret, kPlcSamples);
              decoded_samples += ret;
              total_plc_samples += ret;
            }
            EXPECT_EQ(total_plc_samples, frame_length);
          }
        }

        // Send data to the channel. "channel" will handle the loss simulation.
        channel->SendData(AudioFrameType::kAudioFrameSpeech, payload_type_,
                          rtp_timestamp_, bitstream, bitstream_len_byte, 0);
        if (first_packet) {
          first_packet = false;
          start_time_stamp = rtp_timestamp_;
        }
        rtp_timestamp_ += static_cast<uint32_t>(frame_length);
        read_samples += frame_length * channels;
      }
      if (read_samples == written_samples) {
        read_samples = 0;
        written_samples = 0;
      }
    }

    // Run received side of ACM.
    bool muted;
    ASSERT_EQ(NetEq::kOK, neteq_->GetAudio(&audio_frame, &muted));
    ASSERT_TRUE(resampler_helper_.MaybeResample(out_freq_hz_b, &audio_frame));

    // Write output speech to file.
    out_file_.Write10MsData(
        audio_frame.data(),
        audio_frame.samples_per_channel_ * audio_frame.num_channels_);

    // Write stand-alone speech to file.
    out_file_standalone_.Write10MsData(out_audio, decoded_samples * channels);

    if (audio_frame.timestamp_ > start_time_stamp) {
      // Number of channels should be the same for both stand-alone and
      // ACM-decoding.
      EXPECT_EQ(audio_frame.num_channels_, channels);
    }

    decoded_samples = 0;
  }

  if (in_file_mono_.EndOfFile()) {
    in_file_mono_.Rewind();
  }
  if (in_file_stereo_.EndOfFile()) {
    in_file_stereo_.Rewind();
  }
  // Reset in case we ended with a lost packet.
  channel->set_lost_packet(false);
}

void OpusTest::OpenOutFile(int test_number) {
  StringBuilder sb;
  sb << "opustest_out_" << test_number << ".pcm";
  out_file_name_ = test::TempFilename(test::OutputPath(), sb.str());
  out_file_.Open(out_file_name_, 48000, "wb");

  StringBuilder sb_standalone;
  sb_standalone << "opusstandalone_out_" << test_number << ".pcm";
  out_file_standalone_name_ =
      test::TempFilename(test::OutputPath(), sb_standalone.str());
  out_file_standalone_.Open(out_file_standalone_name_, 48000, "wb");
}

void OpusTest::CloseOutFile() {
  out_file_.Close();
  out_file_standalone_.Close();
  EXPECT_TRUE(test::RemoveFile(out_file_name_));
  EXPECT_TRUE(test::RemoveFile(out_file_standalone_name_));
}

}  // namespace webrtc
