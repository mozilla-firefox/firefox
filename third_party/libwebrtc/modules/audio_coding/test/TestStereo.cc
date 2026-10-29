/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_coding/test/TestStereo.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "api/audio/audio_frame.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/neteq/default_neteq_factory.h"
#include "api/neteq/neteq.h"
#include "api/rtp_headers.h"
#include "api/rtp_parameters.h"
#include "api/units/timestamp.h"
#include "modules/audio_coding/include/audio_coding_module.h"
#include "modules/audio_coding/include/audio_coding_module_typedefs.h"
#include "modules/audio_coding/test/PCMFile.h"
#include "rtc_base/checks.h"
#include "rtc_base/strings/string_builder.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"

namespace webrtc {

// Class for simulating packet handling
TestPackStereo::TestPackStereo()
    : neteq_(nullptr),
      seq_no_(0),
      timestamp_diff_(0),
      last_in_timestamp_(0),
      total_bytes_(0),
      payload_size_(0),
      lost_packet_(false) {}

TestPackStereo::~TestPackStereo() {}

void TestPackStereo::RegisterReceiverNetEq(NetEq* neteq) {
  neteq_ = neteq;
  return;
}

int32_t TestPackStereo::SendData(const AudioFrameType frame_type,
                                 const uint8_t payload_type,
                                 const uint32_t timestamp,
                                 const uint8_t* payload_data,
                                 const size_t payload_size,
                                 int64_t /* absolute_capture_timestamp_ms */) {
  RTPHeader rtp_header;
  int32_t status = 0;

  rtp_header.markerBit = false;
  rtp_header.ssrc = 0;
  rtp_header.sequenceNumber = seq_no_++;
  rtp_header.payloadType = payload_type;
  rtp_header.timestamp = timestamp;
  if (frame_type == AudioFrameType::kEmptyFrame) {
    // Skip this frame
    return 0;
  }

  if (lost_packet_ == false) {
    status = neteq_->InsertPacket(
        rtp_header, std::span<const uint8_t>(payload_data, payload_size),
        /*receive_time=*/Timestamp::MinusInfinity());

    if (frame_type != AudioFrameType::kAudioFrameCN) {
      payload_size_ = static_cast<int>(payload_size);
    } else {
      payload_size_ = -1;
    }

    timestamp_diff_ = timestamp - last_in_timestamp_;
    last_in_timestamp_ = timestamp;
    total_bytes_ += payload_size;
  }
  return status;
}

uint16_t TestPackStereo::payload_size() {
  return static_cast<uint16_t>(payload_size_);
}

uint32_t TestPackStereo::timestamp_diff() {
  return timestamp_diff_;
}

void TestPackStereo::reset_payload_size() {
  payload_size_ = 0;
}

void TestPackStereo::set_codec_mode(enum StereoMonoMode mode) {
  codec_mode_ = mode;
}

void TestPackStereo::set_lost_packet(bool lost) {
  lost_packet_ = lost;
}

TestStereo::TestStereo()
    : env_(CreateTestEnvironment()),
      acm_a_(AudioCodingModule::Create()),
      neteq_(DefaultNetEqFactory().Create(env_,
                                          NetEq::Config(),
                                          CreateBuiltinAudioDecoderFactory())),
      channel_a2b_(nullptr),
      test_cntr_(0),
      pack_size_samp_(0),
      pack_size_bytes_(0),
      counter_(0) {}

TestStereo::~TestStereo() {
  if (channel_a2b_ != nullptr) {
    delete channel_a2b_;
    channel_a2b_ = nullptr;
  }
}

void TestStereo::Perform() {
  uint16_t frequency_hz;
  int audio_channels;
  int codec_channels;

  // Open both mono and stereo test files in 32 kHz.
  const std::string file_name_stereo =
      test::ResourcePath("audio_coding/teststereo32kHz", "pcm");
  const std::string file_name_mono =
      test::ResourcePath("audio_coding/testfile32kHz", "pcm");
  frequency_hz = 32000;
  in_file_stereo_ = new PCMFile();
  in_file_mono_ = new PCMFile();
  in_file_stereo_->Open(file_name_stereo, frequency_hz, "rb");
  in_file_stereo_->ReadStereo(true);
  in_file_mono_->Open(file_name_mono, frequency_hz, "rb");
  in_file_mono_->ReadStereo(false);

  // Create and initialize two ACMs, one for each side of a one-to-one call.
  ASSERT_TRUE((acm_a_.get() != nullptr) && (neteq_.get() != nullptr));
  neteq_->FlushBuffers();

  neteq_->SetCodecs({{103, {"ISAC", 16000, 1}},
                     {104, {"ISAC", 32000, 1}},
                     {107, {"L16", 8000, 1}},
                     {108, {"L16", 16000, 1}},
                     {109, {"L16", 32000, 1}},
                     {111, {"L16", 8000, 2}},
                     {112, {"L16", 16000, 2}},
                     {113, {"L16", 32000, 2}},
                     {0, {"PCMU", 8000, 1}},
                     {110, {"PCMU", 8000, 2}},
                     {8, {"PCMA", 8000, 1}},
                     {118, {"PCMA", 8000, 2}},
                     {9, {"G722", 8000, 1}},
                     {119, {"G722", 8000, 2}},
                     {120, {"OPUS", 48000, 2, {{"stereo", "1"}}}},
                     {13, {"CN", 8000, 1}},
                     {98, {"CN", 16000, 1}},
                     {99, {"CN", 32000, 1}}});

  // Create and connect the channel.
  channel_a2b_ = new TestPackStereo;
  EXPECT_EQ(0, acm_a_->RegisterTransportCallback(channel_a2b_));
  channel_a2b_->RegisterReceiverNetEq(neteq_.get());

  char codec_pcma_temp[] = "PCMA";
  RegisterSendCodec('A', codec_pcma_temp, 8000, 64000, 80, 2);

  //
  // Test Stereo-To-Stereo for all codecs.
  //
  audio_channels = 2;
  codec_channels = 2;

// TODO(bugs.webrtc.org/345525069): Either fix/enable or remove G722.
#if defined(__has_feature) && !__has_feature(undefined_behavior_sanitizer)
  // All codecs are tested for all allowed sampling frequencies, rates and
  // packet sizes.
  channel_a2b_->set_codec_mode(kStereo);
  test_cntr_++;
  OpenOutFile(test_cntr_);
  char codec_g722[] = "G722";
  RegisterSendCodec('A', codec_g722, 16000, 64000, 160, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_g722, 16000, 64000, 320, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_g722, 16000, 64000, 480, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_g722, 16000, 64000, 640, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_g722, 16000, 64000, 800, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_g722, 16000, 64000, 960, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();
#endif

  channel_a2b_->set_codec_mode(kStereo);
  test_cntr_++;
  OpenOutFile(test_cntr_);
  char codec_l16[] = "L16";
  RegisterSendCodec('A', codec_l16, 8000, 128000, 80, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_l16, 8000, 128000, 160, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_l16, 8000, 128000, 240, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_l16, 8000, 128000, 320, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();

  test_cntr_++;
  OpenOutFile(test_cntr_);
  RegisterSendCodec('A', codec_l16, 16000, 256000, 160, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_l16, 16000, 256000, 320, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_l16, 16000, 256000, 480, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_l16, 16000, 256000, 640, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();

  test_cntr_++;
  OpenOutFile(test_cntr_);
  RegisterSendCodec('A', codec_l16, 32000, 512000, 320, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_l16, 32000, 512000, 640, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();
#ifdef PCMA_AND_PCMU
  channel_a2b_->set_codec_mode(kStereo);
  audio_channels = 2;
  codec_channels = 2;
  test_cntr_++;
  OpenOutFile(test_cntr_);
  char codec_pcma[] = "PCMA";
  RegisterSendCodec('A', codec_pcma, 8000, 64000, 80, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_pcma, 8000, 64000, 160, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_pcma, 8000, 64000, 240, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_pcma, 8000, 64000, 320, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_pcma, 8000, 64000, 400, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_pcma, 8000, 64000, 480, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();

  test_cntr_++;
  OpenOutFile(test_cntr_);
  char codec_pcmu[] = "PCMU";
  RegisterSendCodec('A', codec_pcmu, 8000, 64000, 80, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_pcmu, 8000, 64000, 160, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_pcmu, 8000, 64000, 240, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_pcmu, 8000, 64000, 320, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_pcmu, 8000, 64000, 400, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_pcmu, 8000, 64000, 480, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();
#endif
#ifdef WEBRTC_CODEC_OPUS
  channel_a2b_->set_codec_mode(kStereo);
  audio_channels = 2;
  codec_channels = 2;
  test_cntr_++;
  OpenOutFile(test_cntr_);

  char codec_opus[] = "opus";
  // Run Opus with 10 ms frame size.
  RegisterSendCodec('A', codec_opus, 48000, 64000, 480, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  // Run Opus with 20 ms frame size.
  RegisterSendCodec('A', codec_opus, 48000, 64000, 480 * 2, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  // Run Opus with 40 ms frame size.
  RegisterSendCodec('A', codec_opus, 48000, 64000, 480 * 4, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  // Run Opus with 60 ms frame size.
  RegisterSendCodec('A', codec_opus, 48000, 64000, 480 * 6, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  // Run Opus with 20 ms frame size and different bitrates.
  RegisterSendCodec('A', codec_opus, 48000, 40000, 960, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_opus, 48000, 510000, 960, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();
#endif
  //
  // Test Mono-To-Stereo for all codecs.
  //
  audio_channels = 1;
  codec_channels = 2;

// TODO(bugs.webrtc.org/345525069): Either fix/enable or remove G722.
#if defined(__has_feature) && !__has_feature(undefined_behavior_sanitizer)
  test_cntr_++;
  channel_a2b_->set_codec_mode(kStereo);
  OpenOutFile(test_cntr_);
  RegisterSendCodec('A', codec_g722, 16000, 64000, 160, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();
#endif

  test_cntr_++;
  channel_a2b_->set_codec_mode(kStereo);
  OpenOutFile(test_cntr_);
  RegisterSendCodec('A', codec_l16, 8000, 128000, 80, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();

  test_cntr_++;
  OpenOutFile(test_cntr_);
  RegisterSendCodec('A', codec_l16, 16000, 256000, 160, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();

  test_cntr_++;
  OpenOutFile(test_cntr_);
  RegisterSendCodec('A', codec_l16, 32000, 512000, 320, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();
#ifdef PCMA_AND_PCMU
  test_cntr_++;
  channel_a2b_->set_codec_mode(kStereo);
  OpenOutFile(test_cntr_);
  RegisterSendCodec('A', codec_pcmu, 8000, 64000, 80, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_pcma, 8000, 64000, 80, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();
#endif
#ifdef WEBRTC_CODEC_OPUS
  // Keep encode and decode in stereo.
  test_cntr_++;
  channel_a2b_->set_codec_mode(kStereo);
  OpenOutFile(test_cntr_);
  RegisterSendCodec('A', codec_opus, 48000, 64000, 960, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);

  // Encode in mono, decode in stereo mode.
  RegisterSendCodec('A', codec_opus, 48000, 64000, 960, 1);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();
#endif

  //
  // Test Stereo-To-Mono for all codecs.
  //
  audio_channels = 2;
  codec_channels = 1;
  channel_a2b_->set_codec_mode(kMono);

// TODO(bugs.webrtc.org/345525069): Either fix/enable or remove G722.
#if defined(__has_feature) && !__has_feature(undefined_behavior_sanitizer)
  // Run stereo audio and mono codec.
  test_cntr_++;
  OpenOutFile(test_cntr_);
  RegisterSendCodec('A', codec_g722, 16000, 64000, 160, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();
#endif

  test_cntr_++;
  OpenOutFile(test_cntr_);
  RegisterSendCodec('A', codec_l16, 8000, 128000, 80, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();

  test_cntr_++;
  OpenOutFile(test_cntr_);
  RegisterSendCodec('A', codec_l16, 16000, 256000, 160, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();

  test_cntr_++;
  OpenOutFile(test_cntr_);
  RegisterSendCodec('A', codec_l16, 32000, 512000, 320, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();
#ifdef PCMA_AND_PCMU
  test_cntr_++;
  OpenOutFile(test_cntr_);
  RegisterSendCodec('A', codec_pcmu, 8000, 64000, 80, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  RegisterSendCodec('A', codec_pcma, 8000, 64000, 80, codec_channels);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();
#endif
#ifdef WEBRTC_CODEC_OPUS
  test_cntr_++;
  OpenOutFile(test_cntr_);
  // Encode and decode in mono.
  RegisterSendCodec('A', codec_opus, 48000, 32000, 960, codec_channels);
  neteq_->SetCodecs({{120, {"OPUS", 48000, 2}}});
  Run(channel_a2b_, audio_channels, codec_channels);

  // Encode in stereo, decode in mono.
  RegisterSendCodec('A', codec_opus, 48000, 32000, 960, 2);
  Run(channel_a2b_, audio_channels, codec_channels);

  CloseOutFile();

  // Test switching between decoding mono and stereo for Opus.

  // Decode in mono.
  test_cntr_++;
  OpenOutFile(test_cntr_);
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();
  // Decode in stereo.
  test_cntr_++;
  OpenOutFile(test_cntr_);
  neteq_->SetCodecs({{120, {"OPUS", 48000, 2, {{"stereo", "1"}}}}});
  Run(channel_a2b_, audio_channels, 2);
  CloseOutFile();
  // Decode in mono.
  test_cntr_++;
  OpenOutFile(test_cntr_);
  neteq_->SetCodecs({{120, {"OPUS", 48000, 2}}});
  Run(channel_a2b_, audio_channels, codec_channels);
  CloseOutFile();
#endif

  // Delete the file pointers.
  delete in_file_stereo_;
  delete in_file_mono_;
}

// Register Codec to use in the test
//
// Input:   side             - which ACM to use, 'A' or 'B'
//          codec_name       - name to use when register the codec
//          sampling_freq_hz - sampling frequency in Herz
//          rate             - bitrate in bytes
//          pack_size        - packet size in samples
//          channels         - number of channels; 1 for mono, 2 for stereo
void TestStereo::RegisterSendCodec(char side,
                                   char* codec_name,
                                   int32_t sampling_freq_hz,
                                   int rate,
                                   int pack_size,
                                   int channels) {
  // Store packet size in samples, used to validate the received packet
  pack_size_samp_ = pack_size;

  // Store the expected packet size in bytes, used to validate the received
  // packet. Add 0.875 to always round up to a whole byte.
  pack_size_bytes_ = (uint16_t)(static_cast<float>(pack_size * rate) /
                                    static_cast<float>(sampling_freq_hz * 8) +
                                0.875);

  // Set pointer to the ACM where to register the codec
  AudioCodingModule* my_acm = nullptr;
  switch (side) {
    case 'A': {
      my_acm = acm_a_.get();
      break;
    }
    case 'B': {
      // We no longer use this case. Refactor code to avoid the switch.
      ASSERT_TRUE(false);
      // my_acm = neteq_.get();
      break;
    }
    default:
      break;
  }
  ASSERT_TRUE(my_acm != nullptr);

  auto encoder_factory = CreateBuiltinAudioEncoderFactory();
  const int clockrate_hz = absl::EqualsIgnoreCase(codec_name, "g722")
                               ? sampling_freq_hz / 2
                               : sampling_freq_hz;
  const std::string ptime = absl::StrCat(
      CheckedDivExact(pack_size, CheckedDivExact(sampling_freq_hz, 1000)));
  CodecParameterMap params = {{"ptime", ptime}};
  RTC_CHECK(channels == 1 || channels == 2);
  if (absl::EqualsIgnoreCase(codec_name, "opus")) {
    if (channels == 2) {
      params["stereo"] = "1";
    }
    channels = 2;
    params["maxaveragebitrate"] = absl::StrCat(rate);
  }
  auto encoder = encoder_factory->Create(
      env_, SdpAudioFormat(codec_name, clockrate_hz, channels, params),
      {.payload_type = 17});
  EXPECT_NE(nullptr, encoder);
  my_acm->SetEncoder(std::move(encoder));

  send_codec_name_ = codec_name;
}

void TestStereo::Run(TestPackStereo* channel,
                     int in_channels,
                     int out_channels,
                     int percent_loss) {
  AudioFrame audio_frame;

  int32_t out_freq_hz_b = out_file_.SamplingFrequency();
  uint16_t rec_size;
  uint32_t time_stamp_diff;
  channel->reset_payload_size();
  int error_count = 0;
  int variable_bytes = 0;
  int variable_packets = 0;
  // Set test length to 500 ms (50 blocks of 10 ms each).
  in_file_mono_->SetNum10MsBlocksToRead(50);
  in_file_stereo_->SetNum10MsBlocksToRead(50);
  // Fast-forward 1 second (100 blocks) since the files start with silence.
  in_file_stereo_->FastForward(100);
  in_file_mono_->FastForward(100);

  while (true) {
    // Simulate packet loss by setting `packet_loss_` to "true" in
    // `percent_loss` percent of the loops.
    if (percent_loss > 0) {
      if (counter_ == floor((100 / percent_loss) + 0.5)) {
        counter_ = 0;
        channel->set_lost_packet(true);
      } else {
        channel->set_lost_packet(false);
      }
      counter_++;
    }

    // Add 10 msec to ACM
    if (in_channels == 1) {
      if (in_file_mono_->EndOfFile()) {
        break;
      }
      in_file_mono_->Read10MsData(audio_frame);
    } else {
      if (in_file_stereo_->EndOfFile()) {
        break;
      }
      in_file_stereo_->Read10MsData(audio_frame);
    }
    EXPECT_GE(acm_a_->Add10MsData(audio_frame), 0);

    // Verify that the received packet size matches the settings.
    rec_size = channel->payload_size();
    if ((0 < rec_size) & (rec_size < 65535)) {
      if (strcmp(send_codec_name_, "opus") == 0) {
        // Opus is a variable rate codec, hence calculate the average packet
        // size, and later make sure the average is in the right range.
        variable_bytes += rec_size;
        variable_packets++;
      } else {
        // For fixed rate codecs, check that packet size is correct.
        if ((rec_size != pack_size_bytes_ * out_channels) &&
            (pack_size_bytes_ < 65535)) {
          error_count++;
        }
      }
      // Verify that the timestamp is updated with expected length
      time_stamp_diff = channel->timestamp_diff();
      if ((counter_ > 10) && (time_stamp_diff != pack_size_samp_)) {
        error_count++;
      }
    }

    // Run receive side of ACM
    bool muted;
    EXPECT_EQ(NetEq::kOK, neteq_->GetAudio(&audio_frame, &muted));
    EXPECT_TRUE(resampler_helper_.MaybeResample(out_freq_hz_b, &audio_frame));
    ASSERT_FALSE(muted);

    // Write output speech to file
    out_file_.Write10MsData(
        audio_frame.data(),
        audio_frame.samples_per_channel_ * audio_frame.num_channels_);
  }

  EXPECT_EQ(0, error_count);

  // Check that packet size is in the right range for variable rate codecs,
  // such as Opus.
  if (variable_packets > 0) {
    variable_bytes /= variable_packets;
    EXPECT_NEAR(variable_bytes, pack_size_bytes_, 18);
  }

  if (in_file_mono_->EndOfFile()) {
    in_file_mono_->Rewind();
  }
  if (in_file_stereo_->EndOfFile()) {
    in_file_stereo_->Rewind();
  }
  // Reset in case we ended with a lost packet
  channel->set_lost_packet(false);
}

void TestStereo::OpenOutFile(int16_t test_number) {
  StringBuilder file_stream;
  file_stream << "teststereo_out_" << test_number << ".pcm";
  out_file_name_ = test::TempFilename(test::OutputPath(), file_stream.str());
  out_file_.Open(out_file_name_, 32000, "wb");
}

void TestStereo::CloseOutFile() {
  out_file_.Close();
  EXPECT_TRUE(test::RemoveFile(out_file_name_));
}

}  // namespace webrtc
