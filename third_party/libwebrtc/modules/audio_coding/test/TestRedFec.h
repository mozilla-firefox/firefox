/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_CODING_TEST_TESTREDFEC_H_
#define MODULES_AUDIO_CODING_TEST_TESTREDFEC_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/environment/environment.h"
#include "api/neteq/neteq.h"
#include "api/scoped_refptr.h"
#include "common_audio/vad/include/vad.h"
#include "modules/audio_coding/acm2/acm_resampler.h"
#include "modules/audio_coding/include/audio_coding_module.h"
#include "modules/audio_coding/test/Channel.h"
#include "modules/audio_coding/test/PCMFile.h"

namespace webrtc {

class TestRedFec final {
 public:
  explicit TestRedFec();
  ~TestRedFec();

  void Perform();

 private:
  void RegisterSendCodec(const std::unique_ptr<AudioCodingModule>& acm,
                         const SdpAudioFormat& codec_format,
                         std::optional<Vad::Aggressiveness> vad_mode,
                         bool use_red);
  void Run();
  void OpenOutFile(int16_t testNumber);
  void CloseOutFile();

  const Environment env_;
  const scoped_refptr<AudioEncoderFactory> encoder_factory_;
  const scoped_refptr<AudioDecoderFactory> decoder_factory_;
  std::unique_ptr<AudioCodingModule> _acmA;
  std::unique_ptr<NetEq> _neteq;
  acm2::ResamplerHelper _resampler_helper;

  Channel* _channelA2B;

  PCMFile _inFileA;
  PCMFile _outFileB;
  std::string _out_file_name;
  int16_t _testCntr;
};

}  // namespace webrtc

#endif  // MODULES_AUDIO_CODING_TEST_TESTREDFEC_H_
