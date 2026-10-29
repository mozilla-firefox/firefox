/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/codecs/av1/dav1d_decoder.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "api/environment/environment.h"
#include "api/ref_counted_base.h"
#include "api/scoped_refptr.h"
#include "api/video/color_space.h"
#include "api/video/encoded_image.h"
#include "api/video/video_codec_constants.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video_codecs/video_decoder.h"
#include "common_video/include/video_frame_buffer.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/logging.h"
#include "third_party/dav1d/libdav1d/include/dav1d/common.h"
#include "third_party/dav1d/libdav1d/include/dav1d/data.h"
#include "third_party/dav1d/libdav1d/include/dav1d/dav1d.h"
#include "third_party/dav1d/libdav1d/include/dav1d/headers.h"
#include "third_party/dav1d/libdav1d/include/dav1d/picture.h"

namespace webrtc {
namespace {

class ScopedDav1dData {
 public:
  ~ScopedDav1dData() { dav1d_data_unref(&data_); }

  Dav1dData& Data() { return data_; }

 private:
  Dav1dData data_ = {};
};

class ScopedDav1dPicture : public RefCountedNonVirtual<ScopedDav1dPicture> {
 public:
  ~ScopedDav1dPicture() { dav1d_picture_unref(&picture_); }

  Dav1dPicture& Picture() { return picture_; }

 private:
  Dav1dPicture picture_ = {};
};

constexpr char kDav1dName[] = "dav1d";

class Dav1dDecoder : public VideoDecoder {
 public:
  Dav1dDecoder();
  explicit Dav1dDecoder(const Environment& env);
  Dav1dDecoder(const Dav1dDecoder&) = delete;
  Dav1dDecoder& operator=(const Dav1dDecoder&) = delete;

  ~Dav1dDecoder() override;

  bool Configure(const Settings& settings) override;
  int32_t Decode(const EncodedImage& encoded_image,
                 int64_t render_time_ms) override;
  int32_t RegisterDecodeCompleteCallback(
      DecodedImageCallback* callback) override;
  int32_t Release() override;
  DecoderInfo GetDecoderInfo() const override;
  const char* ImplementationName() const override;

 private:
  int32_t OutputPicture(scoped_refptr<ScopedDav1dPicture> dav1d_picture,
                        const EncodedImage& encoded_image);

  Dav1dContext* context_ = nullptr;
  DecodedImageCallback* decode_complete_callback_ = nullptr;

  const bool crop_to_render_resolution_ = false;
};

Dav1dDecoder::Dav1dDecoder() = default;

Dav1dDecoder::Dav1dDecoder(const Environment& env)
    : crop_to_render_resolution_(env.field_trials().IsEnabled(
          "WebRTC-Dav1dDecoder-CropToRenderResolution")) {}

Dav1dDecoder::~Dav1dDecoder() {
  Release();
}

bool Dav1dDecoder::Configure(const Settings& settings) {
  Release();

  Dav1dSettings s;
  dav1d_default_settings(&s);

  s.n_threads = std::clamp(settings.number_of_cores(), 1, DAV1D_MAX_THREADS);
  s.max_frame_delay = 1;  // For low latency decoding.
  s.all_layers = 0;       // Don't output a frame for every spatial layer.
  // Limit max frame size to avoid OOM'ing fuzzers. crbug.com/325284120.
  s.frame_size_limit = kMaxFrameSizePixels;
  s.operating_point = 31;  // Decode all operating points.

  return dav1d_open(&context_, &s) == 0;
}

int32_t Dav1dDecoder::RegisterDecodeCompleteCallback(
    DecodedImageCallback* decode_complete_callback) {
  decode_complete_callback_ = decode_complete_callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t Dav1dDecoder::Release() {
  // dav1d_close() tolerates a null context and always resets the pointer.
  dav1d_close(&context_);
  return WEBRTC_VIDEO_CODEC_OK;
}

VideoDecoder::DecoderInfo Dav1dDecoder::GetDecoderInfo() const {
  DecoderInfo info;
  info.implementation_name = kDav1dName;
  info.is_hardware_accelerated = false;
  return info;
}

const char* Dav1dDecoder::ImplementationName() const {
  return kDav1dName;
}

int32_t Dav1dDecoder::Decode(const EncodedImage& encoded_image,
                             int64_t /*render_time_ms*/) {
  if (!context_ || decode_complete_callback_ == nullptr) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  ScopedDav1dData scoped_dav1d_data;
  Dav1dData& dav1d_data = scoped_dav1d_data.Data();

  scoped_refptr<EncodedImageBufferInterface> bitstream_buffer =
      encoded_image.GetEncodedData();

  // Note that the `bitstream_buffer` can have a higher capacity than what is
  // actually being used, so `encoded_image.size()` should be used to get the
  // actual size of the bitstream.
  if (int wrap_res = dav1d_data_wrap(
          &dav1d_data, encoded_image.data(), encoded_image.size(),
          /*free_callback=*/
          [](const uint8_t* /* buffer */, void* user_data) {
            auto* bb = static_cast<EncodedImageBufferInterface*>(user_data);
            bb->Release();
          },
          /*user_data=*/bitstream_buffer.get())) {
    RTC_LOG(LS_WARNING)
        << "Dav1dDecoder::Decode wrapping the bitstream failed with error code "
        << wrap_res;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  // dav1d now holds the reference and drops it through `free_callback`.
  bitstream_buffer.release();

  // dav1d may stop parsing before the unit is consumed, rejecting further input
  // with EAGAIN until dav1d_get_picture() has parsed the rest.
  int decoded_pictures = 0;
  int get_picture_res = DAV1D_ERR(EAGAIN);
  while (dav1d_data.sz > 0 || get_picture_res == 0) {
    if (dav1d_data.sz > 0) {
      int send_res = dav1d_send_data(context_, &dav1d_data);
      if (send_res != 0 && send_res != DAV1D_ERR(EAGAIN)) {
        RTC_LOG(LS_WARNING)
            << "Dav1dDecoder::Decode decoding failed with error code "
            << send_res;
        return WEBRTC_VIDEO_CODEC_ERROR;
      }
    }

    scoped_refptr<ScopedDav1dPicture> scoped_dav1d_picture(
        new ScopedDav1dPicture{});
    get_picture_res =
        dav1d_get_picture(context_, &scoped_dav1d_picture->Picture());
    if (get_picture_res == DAV1D_ERR(EAGAIN)) {
      continue;
    }
    if (get_picture_res != 0) {
      RTC_LOG(LS_WARNING)
          << "Dav1dDecoder::Decode getting picture failed with error code "
          << get_picture_res;
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    if (++decoded_pictures > 1) {
      // All pictures would share the RTP timestamp of the image.
      RTC_LOG(LS_WARNING) << "Dav1dDecoder::Decode discarding picture "
                          << decoded_pictures << " with rtp timestamp "
                          << encoded_image.RtpTimestamp();
      continue;
    }
    if (int32_t res =
            OutputPicture(std::move(scoped_dav1d_picture), encoded_image);
        res != WEBRTC_VIDEO_CODEC_OK) {
      return res;
    }
  }

  return decoded_pictures > 0 ? WEBRTC_VIDEO_CODEC_OK
                              : WEBRTC_VIDEO_CODEC_NO_OUTPUT;
}

int32_t Dav1dDecoder::OutputPicture(
    scoped_refptr<ScopedDav1dPicture> scoped_dav1d_picture,
    const EncodedImage& encoded_image) {
  Dav1dPicture& dav1d_picture = scoped_dav1d_picture->Picture();

  if (dav1d_picture.seq_hdr == nullptr || dav1d_picture.frame_hdr == nullptr) {
    RTC_LOG(LS_ERROR) << "Dav1dDecoder::Decode picture without headers.";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  if (dav1d_picture.p.bpc != 8) {
    // Only accept 8 bit depth.
    RTC_LOG(LS_ERROR) << "Dav1dDecoder::Decode unhandled bit depth: "
                      << dav1d_picture.p.bpc;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  int width = dav1d_picture.p.w;
  int height = dav1d_picture.p.h;
  if (width > kMaxFrameSidePixels || height > kMaxFrameSidePixels) {
    RTC_LOG(LS_ERROR) << "Dav1dDecoder::Decode frame dimensions " << width
                      << "x" << height << " exceed maximum side length "
                      << kMaxFrameSidePixels;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  if (crop_to_render_resolution_) {
    // Interpret render_width/height as resolution decoded frame should be
    // cropped to.
    if (dav1d_picture.frame_hdr->render_width > 0 &&
        dav1d_picture.frame_hdr->render_height > 0) {
      width = std::min(width, dav1d_picture.frame_hdr->render_width);
      height = std::min(height, dav1d_picture.frame_hdr->render_height);
    } else {
      RTC_LOG(LS_WARNING) << "Dav1dDecoder::Decode invalid render resolution "
                          << dav1d_picture.frame_hdr->render_width << "x"
                          << dav1d_picture.frame_hdr->render_height;
    }
  }

  scoped_refptr<VideoFrameBuffer> wrapped_buffer;
  if (dav1d_picture.p.layout == DAV1D_PIXEL_LAYOUT_I420) {
    wrapped_buffer = WrapI420Buffer(
        width, height, static_cast<uint8_t*>(dav1d_picture.data[0]),
        dav1d_picture.stride[0], static_cast<uint8_t*>(dav1d_picture.data[1]),
        dav1d_picture.stride[1], static_cast<uint8_t*>(dav1d_picture.data[2]),
        dav1d_picture.stride[1],
        // To keep |scoped_dav1d_picture.Picture()| alive
        [scoped_dav1d_picture] {});
  } else if (dav1d_picture.p.layout == DAV1D_PIXEL_LAYOUT_I444) {
    wrapped_buffer = WrapI444Buffer(
        width, height, static_cast<uint8_t*>(dav1d_picture.data[0]),
        dav1d_picture.stride[0], static_cast<uint8_t*>(dav1d_picture.data[1]),
        dav1d_picture.stride[1], static_cast<uint8_t*>(dav1d_picture.data[2]),
        dav1d_picture.stride[1],
        // To keep |scoped_dav1d_picture.Picture()| alive
        [scoped_dav1d_picture] {});
  } else {
    // Only accept I420 or I444 pixel format.
    RTC_LOG(LS_ERROR) << "Dav1dDecoder::Decode unhandled pixel layout: "
                      << dav1d_picture.p.layout;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  if (!wrapped_buffer) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  ColorSpace color_space(
      ColorSpace::PrimaryID::kUnspecified, ColorSpace::TransferID::kUnspecified,
      ColorSpace::MatrixID::kUnspecified,
      dav1d_picture.seq_hdr->color_range ? ColorSpace::RangeID::kFull
                                         : ColorSpace::RangeID::kLimited);
  // AV1 color space defines match ISO 23001-8:2016 via ISO/IEC 23091-4/ITU-T
  // H.273. https://aomediacodec.github.io/av1-spec/#color-config-semantics
  color_space.set_primaries_from_uint8(dav1d_picture.seq_hdr->pri);
  color_space.set_transfer_from_uint8(dav1d_picture.seq_hdr->trc);
  color_space.set_matrix_from_uint8(dav1d_picture.seq_hdr->mtrx);

  VideoFrame decoded_frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(wrapped_buffer)
          .set_rtp_timestamp(encoded_image.RtpTimestamp())
          .set_ntp_time_ms(encoded_image.ntp_time_ms_)
          .set_color_space(color_space)
          .build();

  // Corresponds to QP_base in
  // J. Han et al., "A Technical Overview of AV1," in Proceedings of the IEEE,
  // vol. 109, no. 9, pp. 1435-1462, Sept. 2021,
  // doi: 10.1109/JPROC.2021.3058584. keywords:
  // {Encoding;Codecs;Decoding;Streaming media;Video compression;Media;Alliance
  // of Open Media;AV1;video compression},
  std::optional<uint8_t> qp = dav1d_picture.frame_hdr->quant.yac;
  decode_complete_callback_->Decoded(decoded_frame,
                                     /*decode_time_ms=*/std::nullopt, qp);

  return WEBRTC_VIDEO_CODEC_OK;
}

}  // namespace

std::unique_ptr<VideoDecoder> CreateDav1dDecoder() {
  return std::make_unique<Dav1dDecoder>();
}

std::unique_ptr<VideoDecoder> CreateDav1dDecoder(const Environment& env) {
  return std::make_unique<Dav1dDecoder>(env);
}

}  // namespace webrtc
