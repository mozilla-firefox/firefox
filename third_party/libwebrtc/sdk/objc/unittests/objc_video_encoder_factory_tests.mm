/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import <Foundation/Foundation.h>
#import <OCMock/OCMock.h>
#import <XCTest/XCTest.h>

#include "sdk/objc/native/src/objc_video_encoder_factory.h"

#include "api/environment/environment_factory.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#import "base/RTCVideoEncoder.h"
#import "base/RTCVideoEncoderFactory.h"
#import "base/RTCVideoFrameBuffer.h"
#import "components/video_frame_buffer/RTCCVPixelBuffer.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/gunit.h"
#include "sdk/objc/native/src/objc_frame_buffer.h"

id<RTC_OBJC_TYPE(RTCVideoEncoderFactory)> CreateEncoderFactoryReturning(int return_code) {
  id encoderMock = OCMProtocolMock(@protocol(RTC_OBJC_TYPE(RTCVideoEncoder)));
  OCMStub([encoderMock startEncodeWithSettings:[OCMArg any] numberOfCores:1])
      .andReturn(return_code);
  OCMStub([encoderMock encode:[OCMArg any] codecSpecificInfo:[OCMArg any] frameTypes:[OCMArg any]])
      .andReturn(return_code);
  OCMStub([encoderMock releaseEncoder]).andReturn(return_code);
  OCMStub([encoderMock setBitrate:0 framerate:0]).andReturn(return_code);

  id encoderFactoryMock = OCMProtocolMock(@protocol(RTC_OBJC_TYPE(RTCVideoEncoderFactory)));
  RTC_OBJC_TYPE(RTCVideoCodecInfo)* supported =
      [[RTC_OBJC_TYPE(RTCVideoCodecInfo) alloc] initWithName:@"H264" parameters:nil];
  OCMStub([encoderFactoryMock supportedCodecs]).andReturn(@[ supported ]);
  OCMStub([encoderFactoryMock implementations]).andReturn(@[ supported ]);
  OCMStub([encoderFactoryMock createEncoder:[OCMArg any]]).andReturn(encoderMock);
  return encoderFactoryMock;
}

id<RTC_OBJC_TYPE(RTCVideoEncoderFactory)> CreateOKEncoderFactory() {
  return CreateEncoderFactoryReturning(WEBRTC_VIDEO_CODEC_OK);
}

id<RTC_OBJC_TYPE(RTCVideoEncoderFactory)> CreateErrorEncoderFactory() {
  return CreateEncoderFactoryReturning(WEBRTC_VIDEO_CODEC_ERROR);
}

std::unique_ptr<webrtc::VideoEncoder> GetObjCEncoder(
    id<RTC_OBJC_TYPE(RTCVideoEncoderFactory)> factory) {
  webrtc::ObjCVideoEncoderFactory encoder_factory(factory);
  webrtc::SdpVideoFormat format("H264");
  return encoder_factory.Create(webrtc::CreateEnvironment(), format);
}

#pragma mark -

@interface ObjCVideoEncoderFactoryTests : XCTestCase
@end

@implementation ObjCVideoEncoderFactoryTests

- (void)testInitEncodeReturnsOKOnSuccess {
  std::unique_ptr<webrtc::VideoEncoder> encoder = GetObjCEncoder(CreateOKEncoderFactory());

  auto* settings = new webrtc::VideoCodec();
  const webrtc::VideoEncoder::Capabilities kCapabilities(false);
  EXPECT_EQ(encoder->InitEncode(settings, webrtc::VideoEncoder::Settings(kCapabilities, 1, 0)),
            WEBRTC_VIDEO_CODEC_OK);
}

- (void)testInitEncodeReturnsErrorOnFail {
  std::unique_ptr<webrtc::VideoEncoder> encoder = GetObjCEncoder(CreateErrorEncoderFactory());

  auto* settings = new webrtc::VideoCodec();
  const webrtc::VideoEncoder::Capabilities kCapabilities(false);
  EXPECT_EQ(encoder->InitEncode(settings, webrtc::VideoEncoder::Settings(kCapabilities, 1, 0)),
            WEBRTC_VIDEO_CODEC_ERROR);
}

- (void)testEncodeReturnsOKOnSuccess {
  std::unique_ptr<webrtc::VideoEncoder> encoder = GetObjCEncoder(CreateOKEncoderFactory());

  CVPixelBufferRef pixel_buffer;
  CVPixelBufferCreate(kCFAllocatorDefault, 640, 480, kCVPixelFormatType_32ARGB, nil, &pixel_buffer);
  rtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer =
      rtc::make_ref_counted<webrtc::ObjCFrameBuffer>(
          [[RTC_OBJC_TYPE(RTCCVPixelBuffer) alloc] initWithPixelBuffer:pixel_buffer]);
  webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                 .set_video_frame_buffer(buffer)
                                 .set_rotation(webrtc::kVideoRotation_0)
                                 .set_timestamp_us(0)
                                 .build();
  std::vector<webrtc::VideoFrameType> frame_types;

  EXPECT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);
}

- (void)testEncodeReturnsErrorOnFail {
  std::unique_ptr<webrtc::VideoEncoder> encoder = GetObjCEncoder(CreateErrorEncoderFactory());

  CVPixelBufferRef pixel_buffer;
  CVPixelBufferCreate(kCFAllocatorDefault, 640, 480, kCVPixelFormatType_32ARGB, nil, &pixel_buffer);
  rtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer =
      rtc::make_ref_counted<webrtc::ObjCFrameBuffer>(
          [[RTC_OBJC_TYPE(RTCCVPixelBuffer) alloc] initWithPixelBuffer:pixel_buffer]);
  webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                 .set_video_frame_buffer(buffer)
                                 .set_rotation(webrtc::kVideoRotation_0)
                                 .set_timestamp_us(0)
                                 .build();
  std::vector<webrtc::VideoFrameType> frame_types;

  EXPECT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_ERROR);
}

- (void)testReleaseEncodeReturnsOKOnSuccess {
  std::unique_ptr<webrtc::VideoEncoder> encoder = GetObjCEncoder(CreateOKEncoderFactory());

  EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

- (void)testReleaseEncodeReturnsErrorOnFail {
  std::unique_ptr<webrtc::VideoEncoder> encoder = GetObjCEncoder(CreateErrorEncoderFactory());

  EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_ERROR);
}

- (void)testGetSupportedFormats {
  webrtc::ObjCVideoEncoderFactory encoder_factory(CreateOKEncoderFactory());
  std::vector<webrtc::SdpVideoFormat> supportedFormats = encoder_factory.GetSupportedFormats();
  EXPECT_EQ(supportedFormats.size(), 1u);
  EXPECT_EQ(supportedFormats[0].name, "H264");
}

- (void)testGetImplementations {
  webrtc::ObjCVideoEncoderFactory encoder_factory(CreateOKEncoderFactory());
  std::vector<webrtc::SdpVideoFormat> supportedFormats = encoder_factory.GetImplementations();
  EXPECT_EQ(supportedFormats.size(), 1u);
  EXPECT_EQ(supportedFormats[0].name, "H264");
}

@end
