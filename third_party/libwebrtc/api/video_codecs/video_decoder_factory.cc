/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/video_codecs/video_decoder_factory.h"

#include "api/video_codecs/sdp_video_format.h"

namespace webrtc {

VideoDecoderFactory::CodecSupport VideoDecoderFactory::QueryCodecSupport(
    const SdpVideoFormat& format,
    bool reference_scaling) const {
  // Default implementation, query for supported formats and check if the
  // specified format is supported. Returns false if `reference_scaling` is
  // true.
  return {.is_supported = !reference_scaling &&
                          format.IsCodecInList(GetSupportedFormats())};
}

}  // namespace webrtc
