/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "sdk/objc/native/src/objc_video_decoder_factory.h"

#import "base/RTCMacros.h"
#import "base/RTCVideoDecoder.h"
#import "base/RTCVideoDecoderFactory.h"
#import "base/RTCVideoFrame.h"
#import "base/RTCVideoFrameBuffer.h"
#import "components/video_codec/RTCCodecSpecificInfoH264.h"
#import "sdk/objc/api/peerconnection/RTCEncodedImage+Private.h"
#import "sdk/objc/api/peerconnection/RTCVideoCodecInfo+Private.h"
#import "sdk/objc/api/video_codec/RTCNativeVideoDecoderBuilder+Native.h"
#import "sdk/objc/helpers/NSString+StdString.h"

#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_decoder.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"
#include "sdk/objc/native/src/objc_frame_buffer.h"

namespace webrtc {

namespace {
class ObjCVideoDecoder : public VideoDecoder {
 public:
  ObjCVideoDecoder(id<RTC_OBJC_TYPE(RTCVideoDecoder)> decoder)
      : decoder_(decoder), implementation_name_([decoder implementationName].stdString) {}

  bool Configure(const Settings &settings) override {
    return
        [decoder_ startDecodeWithNumberOfCores:settings.number_of_cores()] == WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Decode(const EncodedImage &input_image, int64_t render_time_ms = -1) override {
    RTC_OBJC_TYPE(RTCEncodedImage) *encodedImage =
        [[RTC_OBJC_TYPE(RTCEncodedImage) alloc] initWithNativeEncodedImage:input_image];

    return [decoder_ decode:encodedImage
              missingFrames:false
          codecSpecificInfo:nil
               renderTimeMs:render_time_ms];
  }

  int32_t RegisterDecodeCompleteCallback(DecodedImageCallback *callback) override {
    [decoder_ setCallback:^(RTC_OBJC_TYPE(RTCVideoFrame) * frame) {
      const auto buffer = rtc::make_ref_counted<ObjCFrameBuffer>(frame.buffer);
      VideoFrame videoFrame = VideoFrame::Builder()
                                  .set_video_frame_buffer(buffer)
                                  .set_rtp_timestamp(frame.timeStamp)
                                  .set_timestamp_ms(0)
                                  .set_rotation((VideoRotation)frame.rotation)
                                  .build();
      callback->Decoded(videoFrame);
    }];

    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Release() override { return [decoder_ releaseDecoder]; }

  const char *ImplementationName() const override { return implementation_name_.c_str(); }

 private:
  id<RTC_OBJC_TYPE(RTCVideoDecoder)> decoder_;
  const std::string implementation_name_;
};
}  // namespace

ObjCVideoDecoderFactory::ObjCVideoDecoderFactory(
    id<RTC_OBJC_TYPE(RTCVideoDecoderFactory)> decoder_factory)
    : decoder_factory_(decoder_factory) {}

ObjCVideoDecoderFactory::~ObjCVideoDecoderFactory() {}

id<RTC_OBJC_TYPE(RTCVideoDecoderFactory)> ObjCVideoDecoderFactory::wrapped_decoder_factory() const {
  return decoder_factory_;
}

std::unique_ptr<VideoDecoder> ObjCVideoDecoderFactory::Create(const Environment &env,
                                                              const SdpVideoFormat &format) {
  NSString *codecName = [NSString stringWithUTF8String:format.name.c_str()];
  for (RTC_OBJC_TYPE(RTCVideoCodecInfo) * codecInfo in decoder_factory_.supportedCodecs) {
    if ([codecName isEqualToString:codecInfo.name]) {
      id<RTC_OBJC_TYPE(RTCVideoDecoder)> decoder = [decoder_factory_ createDecoder:codecInfo];

      if ([decoder conformsToProtocol:@protocol(RTC_OBJC_TYPE(RTCNativeVideoDecoderBuilder))]) {
        return [((id<RTC_OBJC_TYPE(RTCNativeVideoDecoderBuilder)>)decoder) build:env];
      } else {
        return std::unique_ptr<ObjCVideoDecoder>(new ObjCVideoDecoder(decoder));
      }
    }
  }

  return nullptr;
}

std::vector<SdpVideoFormat> ObjCVideoDecoderFactory::GetSupportedFormats() const {
  std::vector<SdpVideoFormat> supported_formats;
  for (RTC_OBJC_TYPE(RTCVideoCodecInfo) * supportedCodec in decoder_factory_.supportedCodecs) {
    SdpVideoFormat format = [supportedCodec nativeSdpVideoFormat];
    supported_formats.push_back(format);
  }

  return supported_formats;
}

}  // namespace webrtc
