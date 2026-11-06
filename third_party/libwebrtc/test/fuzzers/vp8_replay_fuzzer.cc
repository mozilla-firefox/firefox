/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "test/fuzzers/utils/rtp_replayer.h"

namespace webrtc {

void FuzzOneInput(const uint8_t* data, size_t size) {
  auto stream_state = std::make_unique<test::RtpReplayer::StreamState>();
  VideoReceiveStreamInterface::Config vp8_config(&(stream_state->transport));

  VideoReceiveStreamInterface::Decoder vp8_decoder;
  vp8_decoder.video_format = SdpVideoFormat::VP8();
  vp8_decoder.payload_type = 125;
  vp8_config.decoders.push_back(std::move(vp8_decoder));

  vp8_config.rtp.local_ssrc = 7731;
  vp8_config.rtp.remote_ssrc = 1337;
  vp8_config.rtp.rtx_ssrc = 100;
  vp8_config.rtp.nack.rtp_history_ms = 1000;
  vp8_config.rtp.lntf.enabled = true;

  std::vector<VideoReceiveStreamInterface::Config> replay_configs;
  replay_configs.push_back(std::move(vp8_config));

  test::RtpReplayer::Replay(std::move(stream_state), std::move(replay_configs),
                            data, size);
}

}  // namespace webrtc
