/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_coding/test/EncodeDecodeTest.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <span>
#include <string>

#include "absl/strings/string_view.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/environment/environment.h"
#include "api/neteq/default_neteq_factory.h"
#include "api/neteq/neteq.h"
#include "api/units/timestamp.h"
#include "modules/audio_coding/include/audio_coding_module.h"
#include "modules/audio_coding/include/audio_coding_module_typedefs.h"
#include "modules/audio_coding/test/RTPFile.h"
#include "rtc_base/strings/string_builder.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"

namespace webrtc {

namespace {
// Buffer size for stereo 48 kHz audio.
constexpr size_t kWebRtc10MsPcmAudio = 960;

}  // namespace

TestPacketization::TestPacketization(RTPStream* rtpStream, uint16_t frequency)
    : _rtpStream(rtpStream), _frequency(frequency), _seqNo(0) {}

TestPacketization::~TestPacketization() {}

int32_t TestPacketization::SendData(
    const AudioFrameType /* frameType */,
    const uint8_t payloadType,
    const uint32_t timeStamp,
    const uint8_t* payloadData,
    const size_t payloadSize,
    int64_t /* absolute_capture_timestamp_ms */) {
  _rtpStream->Write(payloadType, timeStamp, _seqNo++, payloadData, payloadSize,
                    _frequency);
  return 1;
}

Sender::Sender()
    : _acm(nullptr), _pcmFile(), _audioFrame(), _packetization(nullptr) {}

void Sender::Setup(const Environment& env,
                   AudioCodingModule* acm,
                   RTPStream* rtpStream,
                   absl::string_view in_file_name,
                   int in_sample_rate,
                   int payload_type,
                   SdpAudioFormat format) {
  // Open input file
  const std::string file_name = test::ResourcePath(in_file_name, "pcm");
  _pcmFile.Open(file_name, in_sample_rate, "rb");
  if (format.num_channels == 2) {
    _pcmFile.ReadStereo(true);
  }
  // Set test length to 500 ms (50 blocks of 10 ms each).
  _pcmFile.SetNum10MsBlocksToRead(50);
  // Fast-forward 1 second (100 blocks) since the file starts with silence.
  _pcmFile.FastForward(100);

  acm->SetEncoder(CreateBuiltinAudioEncoderFactory()->Create(
      env, format, {.payload_type = payload_type}));
  _packetization = new TestPacketization(rtpStream, format.clockrate_hz);
  EXPECT_EQ(0, acm->RegisterTransportCallback(_packetization));

  _acm = acm;
}

void Sender::Teardown() {
  _pcmFile.Close();
  delete _packetization;
}

bool Sender::Add10MsData() {
  if (!_pcmFile.EndOfFile()) {
    EXPECT_GT(_pcmFile.Read10MsData(_audioFrame), 0);
    int32_t ok = _acm->Add10MsData(_audioFrame);
    EXPECT_GE(ok, 0);
    return ok >= 0 ? true : false;
  }
  return false;
}

void Sender::Run() {
  while (true) {
    if (!Add10MsData()) {
      break;
    }
  }
}

Receiver::Receiver()
    : _playoutLengthSmpls(kWebRtc10MsPcmAudio),
      _payloadSizeBytes(MAX_INCOMING_PAYLOAD) {}

void Receiver::Setup(NetEq* neteq,
                     RTPStream* rtpStream,
                     absl::string_view out_file_name,
                     size_t channels,
                     int file_num) {
  if (channels == 1) {
    neteq->SetCodecs({{107, {"L16", 8000, 1}},
                      {108, {"L16", 16000, 1}},
                      {109, {"L16", 32000, 1}},
                      {0, {"PCMU", 8000, 1}},
                      {8, {"PCMA", 8000, 1}},
                      {9, {"G722", 8000, 1}},
                      {120, {"OPUS", 48000, 2}},
                      {13, {"CN", 8000, 1}},
                      {98, {"CN", 16000, 1}},
                      {99, {"CN", 32000, 1}}});
  } else {
    ASSERT_EQ(channels, 2u);
    neteq->SetCodecs({{111, {"L16", 8000, 2}},
                      {112, {"L16", 16000, 2}},
                      {113, {"L16", 32000, 2}},
                      {110, {"PCMU", 8000, 2}},
                      {118, {"PCMA", 8000, 2}},
                      {119, {"G722", 8000, 2}},
                      {120, {"OPUS", 48000, 2, {{"stereo", "1"}}}}});
  }

  int playSampFreq;
  StringBuilder file_stream;
  file_stream << out_file_name << file_num << ".pcm";
  _out_file_name = test::TempFilename(test::OutputPath(), file_stream.str());
  _rtpStream = rtpStream;

  playSampFreq = 32000;
  _pcmFile.Open(_out_file_name, 32000, "wb+");

  _realPayloadSizeBytes = 0;
  _playoutBuffer = new int16_t[kWebRtc10MsPcmAudio];
  _frequency = playSampFreq;
  _neteq = neteq;
  _firstTime = true;
}

void Receiver::Teardown() {
  delete[] _playoutBuffer;
  _pcmFile.Close();
  EXPECT_TRUE(test::RemoveFile(_out_file_name));
}

bool Receiver::IncomingPacket() {
  if (!_rtpStream->EndOfFile()) {
    if (_firstTime) {
      _firstTime = false;
      _realPayloadSizeBytes = _rtpStream->Read(&_rtpHeader, _incomingPayload,
                                               _payloadSizeBytes, &_nextTime);
      if (_realPayloadSizeBytes == 0) {
        if (_rtpStream->EndOfFile()) {
          _firstTime = true;
          return true;
        } else {
          return false;
        }
      }
    }

    EXPECT_GE(
        0, _neteq->InsertPacket(_rtpHeader,
                                std::span<const uint8_t>(_incomingPayload,
                                                         _realPayloadSizeBytes),
                                /*receive_time=*/Timestamp::Millis(_nextTime)));
    _realPayloadSizeBytes = _rtpStream->Read(&_rtpHeader, _incomingPayload,
                                             _payloadSizeBytes, &_nextTime);
    if (_realPayloadSizeBytes == 0 && _rtpStream->EndOfFile()) {
      _firstTime = true;
    }
  }
  return true;
}

bool Receiver::PlayoutData() {
  AudioFrame audioFrame;
  bool muted;
  int ok = _neteq->GetAudio(&audioFrame, &muted);
  if (muted) {
    ADD_FAILURE();
    return false;
  }
  EXPECT_EQ(NetEq::kOK, ok);
  if (ok < 0) {
    return false;
  }
  if (_playoutLengthSmpls == 0) {
    return false;
  }
  EXPECT_TRUE(_resampler_helper.MaybeResample(_frequency, &audioFrame));
  _pcmFile.Write10MsData(audioFrame.data(), audioFrame.samples_per_channel_ *
                                                audioFrame.num_channels_);
  return true;
}

void Receiver::Run() {
  uint8_t counter500Ms = 50;
  uint32_t clock = 0;

  while (counter500Ms > 0) {
    if (clock == 0 || clock >= _nextTime) {
      EXPECT_TRUE(IncomingPacket());
      if (clock == 0) {
        clock = _nextTime;
      }
    }
    if ((clock % 10) == 0) {
      if (!PlayoutData()) {
        clock++;
        continue;
      }
    }
    if (_rtpStream->EndOfFile()) {
      counter500Ms--;
    }
    clock++;
  }
}

EncodeDecodeTest::EncodeDecodeTest() = default;

void EncodeDecodeTest::Perform() {
  const std::map<int, SdpAudioFormat> send_codecs = {
      {107, {"L16", 8000, 1}},  {108, {"L16", 16000, 1}},
      {109, {"L16", 32000, 1}}, {0, {"PCMU", 8000, 1}},
      {8, {"PCMA", 8000, 1}},
// TODO(bugs.webrtc.org/345525069): Either fix/enable or remove G722.
#if defined(__has_feature) && !__has_feature(undefined_behavior_sanitizer)
      {9, {"G722", 8000, 1}},
#endif
  };
  const Environment env = CreateTestEnvironment();
  int file_num = 0;
  for (const auto& send_codec : send_codecs) {
    RTPFile rtpFile;
    std::unique_ptr<AudioCodingModule> acm(AudioCodingModule::Create());

    std::string fileName =
        test::TempFilename(test::OutputPath(), "encode_decode_rtp");
    rtpFile.Open(fileName.c_str(), "wb+");
    rtpFile.WriteHeader();
    Sender sender;
    sender.Setup(env, acm.get(), &rtpFile, "audio_coding/testfile32kHz", 32000,
                 send_codec.first, send_codec.second);
    sender.Run();
    sender.Teardown();
    rtpFile.Close();

    rtpFile.Open(fileName.c_str(), "rb");
    rtpFile.ReadHeader();
    std::unique_ptr<NetEq> neteq = DefaultNetEqFactory().Create(
        env, NetEq::Config(), CreateBuiltinAudioDecoderFactory());
    Receiver receiver;
    receiver.Setup(neteq.get(), &rtpFile, "encodeDecode_out", 1, file_num);
    receiver.Run();
    receiver.Teardown();
    rtpFile.Close();
    EXPECT_TRUE(test::RemoveFile(fileName));

    file_num++;
  }
}

}  // namespace webrtc
