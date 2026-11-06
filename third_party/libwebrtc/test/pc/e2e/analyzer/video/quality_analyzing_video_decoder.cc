/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/pc/e2e/analyzer/video/quality_analyzing_video_decoder.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/logging.h"
#include "test/pc/e2e/analyzer/video/simulcast_dummy_buffer_helper.h"

namespace webrtc {
namespace webrtc_pc_e2e {

QualityAnalyzingVideoDecoder::QualityAnalyzingVideoDecoder(
    absl::string_view peer_name,
    std::unique_ptr<VideoDecoder> delegate,
    EncodedImageDataExtractor* extractor,
    VideoQualityAnalyzerInterface* analyzer)
    : peer_name_(peer_name),
      implementation_name_("AnalyzingDecoder-" +
                           std::string(delegate->ImplementationName())),
      delegate_(std::move(delegate)),
      extractor_(extractor),
      analyzer_(analyzer) {
  analyzing_callback_ = std::make_unique<DecoderCallback>(this);
}
QualityAnalyzingVideoDecoder::~QualityAnalyzingVideoDecoder() = default;

bool QualityAnalyzingVideoDecoder::Configure(const Settings& settings) {
  {
    MutexLock lock(&mutex_);
    codec_name_ = std::string(CodecTypeToPayloadString(settings.codec_type())) +
                  "_" + delegate_->GetDecoderInfo().implementation_name;
  }
  return delegate_->Configure(settings);
}

int32_t QualityAnalyzingVideoDecoder::Decode(const EncodedImage& input_image,
                                             int64_t render_time_ms) {
  // Image  extractor extracts id from provided EncodedImage and also returns
  // the image with the original buffer. Buffer can be modified in place, so
  // owner of original buffer will be responsible for deleting it, or extractor
  // can create a new buffer. In such case extractor will be responsible for
  // deleting it.
  EncodedImageExtractionResult out = extractor_->ExtractData(input_image);

  if (out.discard) {
    // To partly emulate behavior of Selective Forwarding Unit (SFU) in the
    // test, on receiver side we will "discard" frames from irrelevant streams.
    // When all encoded images were marked to discarded, black frame have to be
    // returned. Because simulcast streams will be received by receiver as 3
    // different independent streams we don't want that irrelevant streams
    // affect video quality metrics and also we don't want to use CPU time to
    // decode them to prevent regressions on relevant streams. Also we can't
    // just drop frame, because in such case, receiving part will be confused
    // with all frames missing and will request a key frame, which will result
    // into extra load on network and sender side. Because of it, discarded
    // image will be always decoded as black frame and will be passed to
    // callback directly without reaching decoder and video quality analyzer.
    //
    // For more details see QualityAnalyzingVideoEncoder.
    return analyzing_callback_->IrrelevantSimulcastStreamDecoded(
        out.id.value_or(VideoFrame::kNotSetId), input_image.RtpTimestamp());
  }

  EncodedImage* origin_image;
  {
    MutexLock lock(&mutex_);
    // Store id to be able to retrieve it in analyzing callback.
    timestamp_to_frame_id_.insert({input_image.RtpTimestamp(), out.id});
    // Store encoded image to prevent its destruction while it is used in
    // decoder.
    origin_image =
        &(decoding_images_
              .insert({input_image.RtpTimestamp(), std::move(out.image)})
              .first->second);
  }
  // We can safely dereference `origin_image`, because it can be removed from
  // the map only after `delegate_` Decode method will be invoked. Image will
  // be removed inside DecodedImageCallback, which can be done on separate
  // thread.
  analyzer_->OnFramePreDecode(
      peer_name_, out.id.value_or(VideoFrame::kNotSetId), *origin_image);
  int32_t result = delegate_->Decode(*origin_image, render_time_ms);
  if (result != WEBRTC_VIDEO_CODEC_OK) {
    // If delegate decoder failed, then cleanup data for this image.
    VideoQualityAnalyzerInterface::DecoderStats stats;
    {
      MutexLock lock(&mutex_);
      timestamp_to_frame_id_.erase(input_image.RtpTimestamp());
      decoding_images_.erase(input_image.RtpTimestamp());
      stats.decoder_name = codec_name_;
    }
    analyzer_->OnDecoderError(
        peer_name_, out.id.value_or(VideoFrame::kNotSetId), result, stats);
  }
  return result;
}

int32_t QualityAnalyzingVideoDecoder::RegisterDecodeCompleteCallback(
    DecodedImageCallback* callback) {
  analyzing_callback_->SetDelegateCallback(callback);
  return delegate_->RegisterDecodeCompleteCallback(analyzing_callback_.get());
}

int32_t QualityAnalyzingVideoDecoder::Release() {
  // Release decoder first. During release process it can still decode some
  // frames, so we don't take a lock to prevent deadlock.
  int32_t result = delegate_->Release();

  MutexLock lock(&mutex_);
  analyzing_callback_->SetDelegateCallback(nullptr);
  timestamp_to_frame_id_.clear();
  decoding_images_.clear();
  return result;
}

VideoDecoder::DecoderInfo QualityAnalyzingVideoDecoder::GetDecoderInfo() const {
  DecoderInfo info = delegate_->GetDecoderInfo();
  info.implementation_name = implementation_name_;
  return info;
}

const char* QualityAnalyzingVideoDecoder::ImplementationName() const {
  return implementation_name_.c_str();
}

QualityAnalyzingVideoDecoder::DecoderCallback::DecoderCallback(
    QualityAnalyzingVideoDecoder* decoder)
    : decoder_(decoder), delegate_callback_(nullptr) {}
QualityAnalyzingVideoDecoder::DecoderCallback::~DecoderCallback() = default;

void QualityAnalyzingVideoDecoder::DecoderCallback::SetDelegateCallback(
    DecodedImageCallback* delegate) {
  MutexLock lock(&callback_mutex_);
  delegate_callback_ = delegate;
}

// We have to implement all next 3 methods because we don't know which one
// exactly is implemented in `delegate_callback_`, so we need to call the same
// method on `delegate_callback_`, as was called on `this` callback.
int32_t QualityAnalyzingVideoDecoder::DecoderCallback::Decoded(
    VideoFrame& decodedImage) {
  decoder_->OnFrameDecoded(&decodedImage, /*decode_time_ms=*/absl::nullopt,
                           /*qp=*/absl::nullopt);

  MutexLock lock(&callback_mutex_);
  RTC_DCHECK(delegate_callback_);
  return delegate_callback_->Decoded(decodedImage);
}

int32_t QualityAnalyzingVideoDecoder::DecoderCallback::Decoded(
    VideoFrame& decodedImage,
    int64_t decode_time_ms) {
  decoder_->OnFrameDecoded(&decodedImage, decode_time_ms, /*qp=*/absl::nullopt);

  MutexLock lock(&callback_mutex_);
  RTC_DCHECK(delegate_callback_);
  return delegate_callback_->Decoded(decodedImage, decode_time_ms);
}

void QualityAnalyzingVideoDecoder::DecoderCallback::Decoded(
    VideoFrame& decodedImage,
    absl::optional<int32_t> decode_time_ms,
    absl::optional<uint8_t> qp) {
  decoder_->OnFrameDecoded(&decodedImage, decode_time_ms, qp);

  MutexLock lock(&callback_mutex_);
  RTC_DCHECK(delegate_callback_);
  delegate_callback_->Decoded(decodedImage, decode_time_ms, qp);
}

int32_t
QualityAnalyzingVideoDecoder::DecoderCallback::IrrelevantSimulcastStreamDecoded(
    uint16_t frame_id,
    uint32_t timestamp_ms) {
  webrtc::VideoFrame dummy_frame =
      webrtc::VideoFrame::Builder()
          .set_video_frame_buffer(GetDummyFrameBuffer())
          .set_rtp_timestamp(timestamp_ms)
          .set_id(frame_id)
          .build();
  MutexLock lock(&callback_mutex_);
  RTC_DCHECK(delegate_callback_);
  delegate_callback_->Decoded(dummy_frame, absl::nullopt, absl::nullopt);
  return WEBRTC_VIDEO_CODEC_OK;
}

rtc::scoped_refptr<webrtc::VideoFrameBuffer>
QualityAnalyzingVideoDecoder::DecoderCallback::GetDummyFrameBuffer() {
  if (!dummy_frame_buffer_) {
    dummy_frame_buffer_ = CreateDummyFrameBuffer();
  }

  return dummy_frame_buffer_;
}

void QualityAnalyzingVideoDecoder::OnFrameDecoded(
    VideoFrame* frame,
    absl::optional<int32_t> decode_time_ms,
    absl::optional<uint8_t> qp) {
  absl::optional<uint16_t> frame_id;
  std::string codec_name;
  {
    MutexLock lock(&mutex_);
    auto it = timestamp_to_frame_id_.find(frame->rtp_timestamp());
    if (it == timestamp_to_frame_id_.end()) {
      // Ensure, that we have info about this frame. It can happen that for some
      // reasons decoder response, that it failed to decode, when we were
      // posting frame to it, but then call the callback for this frame.
      RTC_LOG(LS_ERROR) << "QualityAnalyzingVideoDecoder::OnFrameDecoded: No "
                           "frame id for frame for frame->timestamp()="
                        << frame->rtp_timestamp();
      return;
    }
    frame_id = it->second;
    timestamp_to_frame_id_.erase(it);
    decoding_images_.erase(frame->rtp_timestamp());
    codec_name = codec_name_;
  }
  // Set frame id to the value, that was extracted from corresponding encoded
  // image.
  frame->set_id(frame_id.value_or(VideoFrame::kNotSetId));
  VideoQualityAnalyzerInterface::DecoderStats stats;
  stats.decoder_name = codec_name;
  stats.decode_time_ms = decode_time_ms;
  analyzer_->OnFrameDecoded(peer_name_, *frame, stats);
}

QualityAnalyzingVideoDecoderFactory::QualityAnalyzingVideoDecoderFactory(
    absl::string_view peer_name,
    std::unique_ptr<VideoDecoderFactory> delegate,
    EncodedImageDataExtractor* extractor,
    VideoQualityAnalyzerInterface* analyzer)
    : peer_name_(peer_name),
      delegate_(std::move(delegate)),
      extractor_(extractor),
      analyzer_(analyzer) {}
QualityAnalyzingVideoDecoderFactory::~QualityAnalyzingVideoDecoderFactory() =
    default;

std::vector<SdpVideoFormat>
QualityAnalyzingVideoDecoderFactory::GetSupportedFormats() const {
  return delegate_->GetSupportedFormats();
}

std::unique_ptr<VideoDecoder> QualityAnalyzingVideoDecoderFactory::Create(
    const Environment& env,
    const SdpVideoFormat& format) {
  std::unique_ptr<VideoDecoder> decoder = delegate_->Create(env, format);
  return std::make_unique<QualityAnalyzingVideoDecoder>(
      peer_name_, std::move(decoder), extractor_, analyzer_);
}

}  // namespace webrtc_pc_e2e
}  // namespace webrtc
