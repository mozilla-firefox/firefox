
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#define GTEST_HAS_RTTI 0
#include "AudioConduit.h"
#include "Canonicals.h"
#include "ImageContainer.h"
#include "MockCall.h"
#include "MockConduit.h"
#include "RtpRtcpConfig.h"
#include "VideoConduit.h"
#include "VideoFrameConverter.h"
#include "api/field_trials_view.h"
#include "api/make_ref_counted.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_sink_interface.h"
#include "gtest/gtest.h"
#include "media/base/media_constants.h"
#include "mozilla/Span.h"
#include "nss.h"
#include "video/config/encoder_stream_factory.h"

using namespace mozilla;
using namespace mozilla::layers;
using namespace testing;
using namespace webrtc;

namespace test {

class MockVideoSink : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  MockVideoSink() : mVideoFrame(nullptr, kVideoRotation_0, 0) {}

  ~MockVideoSink() override = default;

  void OnFrame(const webrtc::VideoFrame& frame) override {
    mVideoFrame = frame;
    ++mOnFrameCount;
  }

  size_t mOnFrameCount = 0;
  webrtc::VideoFrame mVideoFrame;
};

struct TestRTCStatsTimestampState : public dom::RTCStatsTimestampState {
  TestRTCStatsTimestampState()
      : dom::RTCStatsTimestampState(
            0, TimeStamp::Now() + TimeDuration::FromMilliseconds(10),
            webrtc::Timestamp::Micros(0), RTPCallerType::Normal, 0) {}
};

class TestRTCStatsTimestampMaker : public dom::RTCStatsTimestampMaker {
 public:
  TestRTCStatsTimestampMaker()
      : dom::RTCStatsTimestampMaker(TestRTCStatsTimestampState()) {}
};

class DirectVideoFrameConverter : public VideoFrameConverter {
 public:
  explicit DirectVideoFrameConverter(bool aLockScaling)
      : VideoFrameConverter(do_AddRef(GetMainThreadSerialEventTarget()),
                            TestRTCStatsTimestampMaker(), aLockScaling) {}

  void SendVideoFrame(PlanarYCbCrImage* aImage, TimeStamp aTime) {
    FrameToProcess frame(aImage, aTime, aImage->GetSize(), false,
                         mozilla::VideoRotation::kDegree_0);
    ProcessVideoFrame(frame);
  }
};

static uint32_t sTrackingIdCounter = 0;
class VideoConduitTest : public Test {
 public:
  VideoConduitTest(
      VideoSessionConduit::Options aOptions = VideoSessionConduit::Options())
      : mCallWrapper(MockCallWrapper::Create()),
        mTrackingId(TrackingId::Source::Camera, ++sTrackingIdCounter),
        mImageContainer(MakeRefPtr<ImageContainer>(
            ImageUsageType::Webrtc, ImageContainer::SYNCHRONOUS)),
        mVideoFrameConverter(
            MakeRefPtr<DirectVideoFrameConverter>(aOptions.mLockScaling)),
        mVideoSink(std::make_unique<MockVideoSink>()),
        mVideoConduit(MakeRefPtr<WebrtcVideoConduit>(
            mCallWrapper, GetCurrentSerialEventTarget(), std::move(aOptions),
            "", mTrackingId)),
        mControl(GetCurrentSerialEventTarget()) {
    NSS_NoDB_Init(nullptr);

    EXPECT_EQ(mVideoConduit->Init(), kMediaConduitNoError);
    mControl.Update([&](auto& aControl) {
      mVideoConduit->InitControl(&mControl);
      mVideoConduit->SetTrackSource(mVideoFrameConverter);
      mVideoFrameConverter->SetTrackingId(mTrackingId);
      mVideoFrameConverter->AddOrUpdateSink(mVideoSink.get(), {});
      aControl.mLocalSsrcs = {42};
      aControl.mLocalVideoRtxSsrcs = {43};
    });
  }

  ~VideoConduitTest() override {
    mVideoFrameConverter->RemoveSink(mVideoSink.get());
    (void)WaitFor(mVideoConduit->Shutdown());
    mCallWrapper->Destroy();
  }

  MockCall* Call() { return mCallWrapper->GetMockCall(); }

  void SendVideoFrame(unsigned short width, unsigned short height,
                      int64_t capture_time_ms) {
    webrtc::scoped_refptr<webrtc::I420Buffer> buffer =
        webrtc::I420Buffer::Create(width, height);
    memset(buffer->MutableDataY(), 0x10, buffer->StrideY() * buffer->height());
    memset(buffer->MutableDataU(), 0x80,
           buffer->StrideU() * ((buffer->height() + 1) / 2));
    memset(buffer->MutableDataV(), 0x80,
           buffer->StrideV() * ((buffer->height() + 1) / 2));

    PlanarYCbCrData data;
    data.mYChannel = buffer->MutableDataY();
    data.mYStride = buffer->StrideY();
    data.mCbChannel = buffer->MutableDataU();
    data.mCrChannel = buffer->MutableDataV();
    MOZ_RELEASE_ASSERT(buffer->StrideU() == buffer->StrideV());
    data.mCbCrStride = buffer->StrideU();
    data.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
    data.mPictureRect = {0, 0, width, height};
    data.mStereoMode = StereoMode::MONO;
    data.mYUVColorSpace = gfx::YUVColorSpace::BT601;
    data.mColorDepth = gfx::ColorDepth::COLOR_8;
    data.mColorRange = gfx::ColorRange::LIMITED;

    RefPtr image = mImageContainer->CreatePlanarYCbCrImage();
    MOZ_ALWAYS_SUCCEEDS(image->CopyData(data));
    TimeStamp time =
        mVideoFrameConverter->mTimestampMaker.mState.mStartDomRealtime +
        TimeDuration::FromMilliseconds(capture_time_ms);

    mVideoFrameConverter->SendVideoFrame(image, time);
  }

  const RefPtr<MockCallWrapper> mCallWrapper;
  const TrackingId mTrackingId;
  const RefPtr<layers::ImageContainer> mImageContainer;
  const RefPtr<DirectVideoFrameConverter> mVideoFrameConverter;
  const UniquePtr<MockVideoSink> mVideoSink;
  const RefPtr<mozilla::WebrtcVideoConduit> mVideoConduit;
  ConcreteControl mControl;
};

class VideoConduitCodecModeTest
    : public VideoConduitTest,
      public WithParamInterface<webrtc::VideoCodecMode> {};

INSTANTIATE_TEST_SUITE_P(WebRtcCodecModes, VideoConduitCodecModeTest,
                         Values(webrtc::VideoCodecMode::kRealtimeVideo,
                                webrtc::VideoCodecMode::kScreensharing));

TEST_F(VideoConduitTest, TestConfigureReceiveMediaCodecs) {
  // No codecs
  mControl.Update([&](auto& aControl) {
    aControl.mReceiving = true;
    aControl.mVideoRecvCodecs = {};
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 0U);

  // empty codec name
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codec(120, "", VideoEncodingConstraints());
    aControl.mVideoRecvCodecs = {codec};
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 0U);

  // Defaults
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codec(120, "VP8", VideoEncodingConstraints());
    aControl.mVideoRecvCodecs = {codec};
    aControl.mVideoRecvRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].payload_type, 120);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].video_format.name, "VP8");
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.local_ssrc, 0U);
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 0U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.nack.rtp_history_ms, 0);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.remb);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.tmmbr);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kNone);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.ulpfec_payload_type, -1);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.red_payload_type, -1);
  ASSERT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);
}

TEST_F(VideoConduitTest, TestConfigureReceiveMediaCodecsFEC) {
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mFECFbSet = true;
    aControl.mVideoRecvCodecs = {
        codecConfig, VideoCodecConfig(1, "ulpfec", VideoEncodingConstraints()),
        VideoCodecConfig(2, "red", VideoEncodingConstraints())};
    aControl.mVideoRecvRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mReceiving = true;
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].payload_type, 120);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].video_format.name, "VP8");
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.local_ssrc, 0U);
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 0U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.nack.rtp_history_ms, 0);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.remb);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.tmmbr);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kNone);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.ulpfec_payload_type, 1);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.red_payload_type, 2);
  ASSERT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);
}

TEST_F(VideoConduitTest, TestConfigureReceiveMediaCodecsH264) {
  mControl.Update([&](auto& aControl) {
    aControl.mReceiving = true;
    aControl.mVideoRecvCodecs = {
        VideoCodecConfig(120, "H264", VideoEncodingConstraints())};
    aControl.mVideoRecvRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].payload_type, 120);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].video_format.name, "H264");
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.local_ssrc, 0U);
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 0U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.nack.rtp_history_ms, 0);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.remb);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.tmmbr);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kNone);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.ulpfec_payload_type, -1);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.red_payload_type, -1);
  ASSERT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);
}

TEST_F(VideoConduitTest, TestConfigureReceiveMediaCodecsMultipleH264) {
  mControl.Update([&](auto& aControl) {
    // Insert two H264 codecs to test that the receive stream knows about both.
    aControl.mReceiving = true;
    VideoCodecConfig h264_b(126, "H264", VideoEncodingConstraints());
    h264_b.mProfile = 0x42;
    h264_b.mConstraints = 0xE0;
    h264_b.mLevel = 0x01;
    VideoCodecConfig h264_h(105, "H264", VideoEncodingConstraints());
    h264_h.mProfile = 0x64;
    h264_h.mConstraints = 0xE0;
    h264_h.mLevel = 0x01;
    aControl.mVideoRecvCodecs = {h264_b, h264_h};
    aControl.mVideoRecvRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 2U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].payload_type, 126);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].video_format.name, "H264");
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[1].payload_type, 105);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[1].video_format.name, "H264");
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.local_ssrc, 0U);
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 0U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.nack.rtp_history_ms, 0);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.remb);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.tmmbr);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kNone);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.ulpfec_payload_type, -1);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.red_payload_type, -1);
  ASSERT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);
}

TEST_F(VideoConduitTest, TestConfigureReceiveMediaCodecsKeyframeRequestType) {
  // PLI should be preferred to FIR, same codec.
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mNackFbTypes.push_back("pli");
    codecConfig.mCcmFbTypes.push_back("fir");
    aControl.mReceiving = true;
    aControl.mVideoRecvCodecs = {codecConfig};
    aControl.mVideoRecvRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kPliRtcp);

  // Just FIR
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mCcmFbTypes.push_back("fir");
    aControl.mVideoRecvCodecs = {codecConfig};
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kFirRtcp);

  // PLI should be preferred to FIR, multiple codecs.
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig pliCodec(120, "VP8", VideoEncodingConstraints());
    pliCodec.mNackFbTypes.push_back("pli");
    VideoCodecConfig firCodec(120, "VP8", VideoEncodingConstraints());
    firCodec.mCcmFbTypes.push_back("fir");
    aControl.mVideoRecvCodecs = {pliCodec, firCodec};
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 2U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kPliRtcp);
}

TEST_F(VideoConduitTest, TestConfigureReceiveMediaCodecsNack) {
  mControl.Update([&](auto& aControl) {
    aControl.mReceiving = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mNackFbTypes.push_back("");
    aControl.mVideoRecvCodecs = {codecConfig};
    aControl.mVideoRecvRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].payload_type, 120);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].video_format.name, "VP8");
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.local_ssrc, 0U);
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 0U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.nack.rtp_history_ms, 1000);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.remb);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.tmmbr);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kNone);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.ulpfec_payload_type, -1);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.red_payload_type, -1);
  ASSERT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);
}

TEST_F(VideoConduitTest, TestConfigureReceiveMediaCodecsRemb) {
  mControl.Update([&](auto& aControl) {
    aControl.mReceiving = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mRembFbSet = true;
    aControl.mVideoRecvCodecs = {codecConfig};
    aControl.mVideoRecvRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].payload_type, 120);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].video_format.name, "VP8");
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.local_ssrc, 0U);
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 0U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.nack.rtp_history_ms, 0);
  ASSERT_TRUE(Call()->mVideoReceiveConfig->rtp.remb);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.tmmbr);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kNone);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.ulpfec_payload_type, -1);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.red_payload_type, -1);
  ASSERT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);
}

TEST_F(VideoConduitTest, TestConfigureReceiveMediaCodecsTmmbr) {
  mControl.Update([&](auto& aControl) {
    aControl.mReceiving = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mCcmFbTypes.push_back("tmmbr");
    aControl.mVideoRecvCodecs = {codecConfig};
    aControl.mVideoRecvRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].payload_type, 120);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].video_format.name, "VP8");
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.local_ssrc, 0U);
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 0U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.nack.rtp_history_ms, 0);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.remb);
  ASSERT_TRUE(Call()->mVideoReceiveConfig->rtp.tmmbr);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kNone);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.ulpfec_payload_type, -1);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.red_payload_type, -1);
  ASSERT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);
}

TEST_F(VideoConduitTest, TestConfigureSendMediaCodec) {
  // defaults
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoSendConfig);
  ASSERT_EQ(Call()->mVideoSendConfig->rtp.payload_name, "VP8");
  ASSERT_EQ(Call()->mVideoSendConfig->rtp.payload_type, 120);
  ASSERT_EQ(Call()->mVideoSendConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoSendConfig->rtp.max_packet_size, mozilla::kVideoMtu);
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->content_type,
            VideoEncoderConfig::ContentType::kRealtimeVideo);
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->min_transmit_bitrate_bps, 0);
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->max_bitrate_bps, KBPS(10000));
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->number_of_streams, 1U);
  ASSERT_EQ(
      Call()->mVideoSendEncoderConfig->simulcast_layers[0].max_bitrate_bps,
      KBPS(10000));

  // empty codec name
  mControl.Update([&](auto& aControl) {
    aControl.mVideoSendCodec =
        Some(VideoCodecConfig(120, "", VideoEncodingConstraints()));
  });
  // Bad codec gets ignored
  ASSERT_EQ(Call()->mVideoSendConfig->rtp.payload_name, "VP8");
}

TEST_F(VideoConduitTest, TestConfigureSendMediaCodecMaxFps) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoEncodingConstraints constraints;
    VideoCodecConfig codecConfig(120, "VP8", constraints);
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  std::vector<webrtc::VideoStream> videoStreams;
  videoStreams = Call()->CreateEncoderStreams(640, 480);
  ASSERT_EQ(videoStreams.size(), 1U);
  ASSERT_EQ(videoStreams[0].max_framerate, 30);  // DEFAULT_VIDEO_MAX_FRAMERATE

  mControl.Update([&](auto& aControl) {
    VideoEncodingConstraints constraints;
    constraints.maxFps = Some(42);
    VideoCodecConfig codecConfig(120, "VP8", constraints);
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  videoStreams = Call()->CreateEncoderStreams(640, 480);
  ASSERT_EQ(videoStreams.size(), 1U);
  ASSERT_EQ(videoStreams[0].max_framerate, 42);
}

TEST_F(VideoConduitTest, TestConfigureSendMediaCodecMaxMbps) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoEncodingConstraints constraints;
    constraints.maxMbps = 0;
    VideoCodecConfig codecConfig(120, "VP8", constraints);
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  SendVideoFrame(640, 480, 1);
  std::vector<webrtc::VideoStream> videoStreams;
  videoStreams = Call()->CreateEncoderStreams(640, 480);
  ASSERT_EQ(videoStreams.size(), 1U);
  ASSERT_EQ(videoStreams[0].max_framerate, 30);  // DEFAULT_VIDEO_MAX_FRAMERATE

  mControl.Update([&](auto& aControl) {
    VideoEncodingConstraints constraints;
    constraints.maxMbps = 10000;
    VideoCodecConfig codecConfig(120, "VP8", constraints);
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  SendVideoFrame(640, 480, 2);
  videoStreams = Call()->CreateEncoderStreams(640, 480);
  ASSERT_EQ(videoStreams.size(), 1U);
  ASSERT_EQ(videoStreams[0].max_framerate, 8);
}

TEST_F(VideoConduitTest, TestConfigureSendMediaCodecDefaults) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });

  {
    ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(640, 480);
    EXPECT_EQ(videoStreams.size(), 1U);
    EXPECT_EQ(videoStreams[0].min_bitrate_bps, 30000);
    EXPECT_EQ(videoStreams[0].target_bitrate_bps, 500000);
    EXPECT_EQ(videoStreams[0].max_bitrate_bps, 2000000);
  }

  {
    // SelectBitrates not called until we send a frame
    SendVideoFrame(1280, 720, 1);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(1280, 720);
    EXPECT_EQ(videoStreams.size(), 1U);
    EXPECT_EQ(videoStreams[0].min_bitrate_bps, 30000);
    EXPECT_EQ(videoStreams[0].target_bitrate_bps, 1500000);
    EXPECT_EQ(videoStreams[0].max_bitrate_bps, 5000000);
  }
}

TEST_F(VideoConduitTest, TestConfigureSendMediaCodecTias) {
  // TIAS
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfigTias(120, "VP8", VideoEncodingConstraints());
    codecConfigTias.mEncodings.emplace_back();
    codecConfigTias.mTias = 2000000;
    aControl.mVideoSendCodec = Some(codecConfigTias);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  {
    ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
    ASSERT_EQ(Call()->mVideoSendEncoderConfig->max_bitrate_bps, 2000000);
    ASSERT_EQ(Call()->mVideoSendEncoderConfig->number_of_streams, 1U);
    ASSERT_EQ(
        Call()->mVideoSendEncoderConfig->simulcast_layers[0].max_bitrate_bps,
        2000000);
    SendVideoFrame(1280, 720, 1);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(1280, 720);
    ASSERT_EQ(videoStreams.size(), 1U);
    ASSERT_EQ(videoStreams[0].min_bitrate_bps, 30000);
    ASSERT_EQ(videoStreams[0].target_bitrate_bps, 1500000);
    ASSERT_EQ(videoStreams[0].max_bitrate_bps, 2000000);
  }

  // TIAS (too low)
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfigTiasLow(120, "VP8", VideoEncodingConstraints());
    codecConfigTiasLow.mEncodings.emplace_back();
    codecConfigTiasLow.mTias = 1000;
    aControl.mVideoSendCodec = Some(codecConfigTiasLow);
  });
  {
    ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
    ASSERT_EQ(Call()->mVideoSendEncoderConfig->max_bitrate_bps, 1000);
    ASSERT_EQ(Call()->mVideoSendEncoderConfig->number_of_streams, 1U);
    ASSERT_EQ(
        Call()->mVideoSendEncoderConfig->simulcast_layers[0].max_bitrate_bps,
        1000);
    SendVideoFrame(1280, 720, 2);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(1280, 720);
    ASSERT_EQ(videoStreams.size(), 1U);
    ASSERT_EQ(videoStreams[0].min_bitrate_bps, 30000);
    ASSERT_EQ(videoStreams[0].target_bitrate_bps, 30000);
    ASSERT_EQ(videoStreams[0].max_bitrate_bps, 30000);
  }
}

TEST_F(VideoConduitTest, TestConfigureSendMediaCodecMaxBr) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    auto& encoding = codecConfig.mEncodings.emplace_back();
    encoding.constraints.maxBr = 50000;
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->max_bitrate_bps, 50000);
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->number_of_streams, 1U);
  ASSERT_EQ(
      Call()->mVideoSendEncoderConfig->simulcast_layers[0].max_bitrate_bps,
      50000);
  SendVideoFrame(1280, 720, 1);
  const std::vector<webrtc::VideoStream> videoStreams =
      Call()->CreateEncoderStreams(1280, 720);
  ASSERT_EQ(videoStreams.size(), 1U);
  ASSERT_LE(videoStreams[0].min_bitrate_bps, 50000);
  ASSERT_LE(videoStreams[0].target_bitrate_bps, 50000);
  ASSERT_EQ(videoStreams[0].max_bitrate_bps, 50000);
}

MATCHER(HasSameLayerLimits, "") {
  const auto& [actual, expected] = arg;
  return actual.min_bitrate_bps == expected.min_bitrate_bps &&
         actual.target_bitrate_bps == expected.target_bitrate_bps &&
         actual.max_bitrate_bps == expected.max_bitrate_bps;
}

class NoFieldTrials : public webrtc::FieldTrialsView {
 public:
  std::string Lookup(absl::string_view) const override { return ""; }
};

class VideoConduitSimulcastCodecTest : public VideoConduitTest,
                                       public WithParamInterface<const char*> {
 protected:
  // A b=TIAS or max-bitrate of 0 means unlimited.
  void ConfigureSimulcast(int aTias, uint64_t aMiddleLayerMaxBr) {
    mControl.Update([&](auto& aControl) {
      aControl.mTransmitting = true;
      VideoCodecConfig codecConfig(120, GetParam(), VideoEncodingConstraints());
      codecConfig.mTias = aTias;
      for (double scaleDownBy : {4.0, 2.0, 1.0}) {
        auto& encoding = codecConfig.mEncodings.emplace_back();
        encoding.constraints.scaleDownBy = scaleDownBy;
      }
      codecConfig.mEncodings[1].constraints.maxBr = aMiddleLayerMaxBr;
      aControl.mVideoSendCodec = Some(codecConfig);
      aControl.mVideoSendRtpRtcpConfig =
          Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
      aControl.mLocalSsrcs = {42, 43, 44};
      aControl.mLocalVideoRtxSsrcs = {45, 46, 47};
    });
    ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  }

  // The streams libwebrtc's own stream factory makes for the same three
  // layers, with no limits configured.
  std::vector<webrtc::VideoStream> ExpectedLibwebrtcStreams() {
    webrtc::VideoEncoderConfig config;
    config.codec_type = webrtc::PayloadStringToCodecType(GetParam());
    config.number_of_streams = 3;
    for (double scaleDownBy : {4.0, 2.0, 1.0}) {
      webrtc::VideoStream layer;
      layer.scale_resolution_down_by = scaleDownBy;
      config.simulcast_layers.push_back(layer);
    }
    return webrtc::make_ref_counted<webrtc::EncoderStreamFactory>(
               webrtc::VideoEncoder::EncoderInfo())
        ->CreateEncoderStreams(NoFieldTrials(), 1280, 720, config);
  }
};

INSTANTIATE_TEST_SUITE_P(WebRtcSimulcastCodecs, VideoConduitSimulcastCodecTest,
                         Values("VP8", "VP9", "H264", "AV1"));

TEST_P(VideoConduitSimulcastCodecTest, TestConfigureSendMediaCodecBitrates) {
  ConfigureSimulcast(0, 0);
  const std::vector<webrtc::VideoStream> videoStreams =
      Call()->CreateEncoderStreams(1280, 720);
  EXPECT_THAT(videoStreams,
              Pointwise(HasSameLayerLimits(), ExpectedLibwebrtcStreams()));
}

// The layer limits are only lowered, by the max-bitrate of an encoding and by
// b=TIAS. A cap can leave a gap between a layer's max and the next layer's min,
// as it can in libwebrtc.
TEST_P(VideoConduitSimulcastCodecTest,
       TestConfigureSendMediaCodecBitratesCapped) {
  constexpr int kTias = 1'000'000;
  constexpr int kMiddleLayerMaxBr = 300'000;
  ConfigureSimulcast(kTias, kMiddleLayerMaxBr);
  const std::vector<webrtc::VideoStream> videoStreams =
      Call()->CreateEncoderStreams(1280, 720);
  std::vector<webrtc::VideoStream> expected = ExpectedLibwebrtcStreams();
  for (size_t i = 0; i < expected.size(); ++i) {
    const int cap = i == 1 ? std::min(kTias, kMiddleLayerMaxBr) : kTias;
    auto& layer = expected[i];
    layer.max_bitrate_bps = std::min(layer.max_bitrate_bps, cap);
    layer.target_bitrate_bps = std::min(layer.target_bitrate_bps, cap);
    layer.min_bitrate_bps =
        std::min(layer.min_bitrate_bps, layer.max_bitrate_bps);
  }
  EXPECT_THAT(videoStreams, Pointwise(HasSameLayerLimits(), expected));
}

TEST_F(VideoConduitTest, TestConfigureSendMediaCodecScaleResolutionBy) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.constraints.scaleDownBy = 2;
    }
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.constraints.scaleDownBy = 4;
    }
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mLocalSsrcs = {42, 1729};
    aControl.mLocalVideoRtxSsrcs = {43, 1730};
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  SendVideoFrame(640, 360, 1);
  const std::vector<webrtc::VideoStream> videoStreams =
      Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                   mVideoSink->mVideoFrame.height());
  ASSERT_EQ(videoStreams.size(), 2U);
  ASSERT_EQ(videoStreams[0].width, 320U);
  ASSERT_EQ(videoStreams[0].height, 180U);
  ASSERT_EQ(videoStreams[1].width, 160U);
  ASSERT_EQ(videoStreams[1].height, 90U);
}

TEST_F(VideoConduitTest, TestConfigureSendMediaCodecCodecMode) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = webrtc::VideoCodecMode::kScreensharing;
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->content_type,
            VideoEncoderConfig::ContentType::kScreen);
}

TEST_F(VideoConduitTest, TestConfigureSendMediaCodecFEC) {
  {
    // H264 + FEC
    mControl.Update([&](auto& aControl) {
      aControl.mTransmitting = true;
      VideoCodecConfig codecConfig(120, "H264", VideoEncodingConstraints());
      codecConfig.mEncodings.emplace_back();
      codecConfig.mFECFbSet = true;
      codecConfig.mULPFECPayloadType = 1;
      codecConfig.mREDPayloadType = 2;
      codecConfig.mREDRTXPayloadType = 3;
      aControl.mVideoSendCodec = Some(codecConfig);
      aControl.mVideoSendRtpRtcpConfig =
          Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    });
    ASSERT_TRUE(Call()->mVideoSendConfig);
    ASSERT_EQ(Call()->mVideoSendConfig->rtp.ulpfec.ulpfec_payload_type, 1);
    ASSERT_EQ(Call()->mVideoSendConfig->rtp.ulpfec.red_payload_type, 2);
    ASSERT_EQ(Call()->mVideoSendConfig->rtp.ulpfec.red_rtx_payload_type, 3);
  }

  {
    // H264 + FEC + Nack
    mControl.Update([&](auto& aControl) {
      VideoCodecConfig codecConfig(120, "H264", VideoEncodingConstraints());
      codecConfig.mEncodings.emplace_back();
      codecConfig.mFECFbSet = true;
      codecConfig.mNackFbTypes.push_back("");
      codecConfig.mULPFECPayloadType = 1;
      codecConfig.mREDPayloadType = 2;
      codecConfig.mREDRTXPayloadType = 3;
      aControl.mVideoSendCodec = Some(codecConfig);
    });
    ASSERT_TRUE(Call()->mVideoSendConfig);
    ASSERT_EQ(Call()->mVideoSendConfig->rtp.ulpfec.ulpfec_payload_type, -1);
    ASSERT_EQ(Call()->mVideoSendConfig->rtp.ulpfec.red_payload_type, -1);
    ASSERT_EQ(Call()->mVideoSendConfig->rtp.ulpfec.red_rtx_payload_type, -1);
  }

  {
    // VP8 + FEC + Nack
    mControl.Update([&](auto& aControl) {
      VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
      codecConfig.mEncodings.emplace_back();
      codecConfig.mFECFbSet = true;
      codecConfig.mNackFbTypes.push_back("");
      codecConfig.mULPFECPayloadType = 1;
      codecConfig.mREDPayloadType = 2;
      codecConfig.mREDRTXPayloadType = 3;
      aControl.mVideoSendCodec = Some(codecConfig);
    });
    ASSERT_TRUE(Call()->mVideoSendConfig);
    ASSERT_EQ(Call()->mVideoSendConfig->rtp.ulpfec.ulpfec_payload_type, 1);
    ASSERT_EQ(Call()->mVideoSendConfig->rtp.ulpfec.red_payload_type, 2);
    ASSERT_EQ(Call()->mVideoSendConfig->rtp.ulpfec.red_rtx_payload_type, 3);
  }
}

TEST_F(VideoConduitTest, TestConfigureSendMediaCodecNack) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoSendConfig);
  ASSERT_EQ(Call()->mVideoSendConfig->rtp.nack.rtp_history_ms, 0);

  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig = *aControl.mVideoSendCodec.Ref();
    codecConfig.mNackFbTypes.push_back("");
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mVideoSendConfig);
  ASSERT_EQ(Call()->mVideoSendConfig->rtp.nack.rtp_history_ms, 1000);
}

TEST_F(VideoConduitTest, TestConfigureSendMediaCodecRids) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoSendConfig);
  ASSERT_EQ(Call()->mVideoSendConfig->rtp.rids.size(), 0U);

  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.rid = "1";
    }
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.rid = "2";
    }
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mLocalSsrcs = {42, 1729};
    aControl.mLocalVideoRtxSsrcs = {43, 1730};
  });
  ASSERT_TRUE(Call()->mVideoSendConfig);
  ASSERT_EQ(Call()->mVideoSendConfig->rtp.rids.size(), 2U);
  ASSERT_EQ(Call()->mVideoSendConfig->rtp.rids[0], "1");
  ASSERT_EQ(Call()->mVideoSendConfig->rtp.rids[1], "2");
}

TEST_F(VideoConduitTest, TestOnSinkWantsChanged) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    codecConfig.mEncodingConstraints.maxFs = 0;
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  webrtc::VideoSinkWants wants;
  wants.max_pixel_count = 256000;
  mVideoFrameConverter->AddOrUpdateSink(mVideoSink.get(), wants);
  SendVideoFrame(1920, 1080, 1);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    EXPECT_LE(videoStreams[0].width * videoStreams[0].height, 256000U);
    ASSERT_EQ(videoStreams.size(), 1U);
    EXPECT_EQ(videoStreams[0].width, 640U);
    EXPECT_EQ(videoStreams[0].height, 360U);
  }

  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig = *aControl.mVideoSendCodec.Ref();
    codecConfig.mEncodingConstraints.maxFs = 500;
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  SendVideoFrame(1920, 1080, 2);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    EXPECT_LE(videoStreams[0].width * videoStreams[0].height, 500U * 16U * 16U);
    ASSERT_EQ(videoStreams.size(), 1U);
    EXPECT_EQ(videoStreams[0].width, 476U);
    EXPECT_EQ(videoStreams[0].height, 268U);
  }

  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig = *aControl.mVideoSendCodec.Ref();
    codecConfig.mEncodingConstraints.maxFs = 1000;
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  mVideoFrameConverter->AddOrUpdateSink(mVideoSink.get(), wants);
  SendVideoFrame(1920, 1080, 3);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    EXPECT_LE(videoStreams[0].width * videoStreams[0].height,
              1000U * 16U * 16U);
    ASSERT_EQ(videoStreams.size(), 1U);
    EXPECT_EQ(videoStreams[0].width, 640U);
    EXPECT_EQ(videoStreams[0].height, 360U);
  }

  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig = *aControl.mVideoSendCodec.Ref();
    codecConfig.mEncodingConstraints.maxFs = 500;
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  wants.max_pixel_count = 64000;
  mVideoFrameConverter->AddOrUpdateSink(mVideoSink.get(), wants);
  SendVideoFrame(1920, 1080, 4);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams.size(), 1U);
    EXPECT_EQ(videoStreams[0].width, 320U);
    EXPECT_EQ(videoStreams[0].height, 180U);
    EXPECT_LE(videoStreams[0].width * videoStreams[0].height, 64000U);
  }
}

class VideoConduitTestScalingLocked : public VideoConduitTest {
 public:
  static VideoSessionConduit::Options CreateOptions() {
    VideoSessionConduit::Options options;
    options.mLockScaling = true;
    return options;
  }
  VideoConduitTestScalingLocked() : VideoConduitTest(CreateOptions()) {}
};

TEST_F(VideoConduitTestScalingLocked, TestOnSinkWantsChanged) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodingConstraints.maxFs = 0;
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  webrtc::VideoSinkWants wants;
  wants.max_pixel_count = 256000;
  mVideoFrameConverter->AddOrUpdateSink(mVideoSink.get(), wants);
  SendVideoFrame(1920, 1080, 1);
  EXPECT_EQ(mVideoSink->mVideoFrame.width(), 1920);
  EXPECT_EQ(mVideoSink->mVideoFrame.height(), 1080);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams.size(), 1U);
    EXPECT_EQ(videoStreams[0].width, 1920U);
    EXPECT_EQ(videoStreams[0].height, 1080U);
  }

  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig = *aControl.mVideoSendCodec.Ref();
    codecConfig.mEncodingConstraints.maxFs = 500;
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  SendVideoFrame(1920, 1080, 2);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    EXPECT_LE(videoStreams[0].width * videoStreams[0].height, 500U * 16U * 16U);
    ASSERT_EQ(videoStreams.size(), 1U);
    EXPECT_EQ(videoStreams[0].width, 476U);
    EXPECT_EQ(videoStreams[0].height, 268U);
  }
}

TEST_P(VideoConduitCodecModeTest,
       TestConfigureSendMediaCodecSimulcastOddResolution) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    {
      VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
      codecConfig.mEncodings.emplace_back();
      {
        auto& encoding = codecConfig.mEncodings.emplace_back();
        encoding.constraints.scaleDownBy = 2;
      }
      {
        auto& encoding = codecConfig.mEncodings.emplace_back();
        encoding.constraints.scaleDownBy = 4;
      }
      aControl.mVideoSendCodec = Some(codecConfig);
    }
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = GetParam();
    aControl.mLocalSsrcs = {42, 43, 44};
    aControl.mLocalVideoRtxSsrcs = {45, 46, 47};
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  SendVideoFrame(27, 25, 1);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams.size(), 3U);
    EXPECT_EQ(videoStreams[0].width, 27U);
    EXPECT_EQ(videoStreams[0].height, 25U);
    EXPECT_EQ(videoStreams[1].width, 13U);
    EXPECT_EQ(videoStreams[1].height, 12U);
    EXPECT_EQ(videoStreams[2].width, 6U);
    EXPECT_EQ(videoStreams[2].height, 6U);
  }

  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig = *aControl.mVideoSendCodec.Ref();
    codecConfig.mEncodings.clear();
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mLocalSsrcs = {42};
    aControl.mLocalVideoRtxSsrcs = {43};
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  SendVideoFrame(27, 25, 2);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams.size(), 1U);
    EXPECT_EQ(videoStreams[0].width, 27U);
    EXPECT_EQ(videoStreams[0].height, 25U);
  }
}

TEST_P(VideoConduitCodecModeTest,
       TestConfigureSendMediaCodecSimulcastAllScaling) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.constraints.scaleDownBy = 2;
    }
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.constraints.scaleDownBy = 4;
    }
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.constraints.scaleDownBy = 6;
    }
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = GetParam();
    aControl.mLocalSsrcs = {42, 43, 44};
    aControl.mLocalVideoRtxSsrcs = {45, 46, 47};
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  SendVideoFrame(1281, 721, 1);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams.size(), 3U);
    EXPECT_EQ(videoStreams[0].width, 640U);
    EXPECT_EQ(videoStreams[0].height, 360U);
    EXPECT_EQ(videoStreams[1].width, 320U);
    EXPECT_EQ(videoStreams[1].height, 180U);
    EXPECT_EQ(videoStreams[2].width, 213U);
    EXPECT_EQ(videoStreams[2].height, 120U);
  }

  SendVideoFrame(1281, 721, 2);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams.size(), 3U);
    EXPECT_EQ(videoStreams[0].width, 640U);
    EXPECT_EQ(videoStreams[0].height, 360U);
    EXPECT_EQ(videoStreams[1].width, 320U);
    EXPECT_EQ(videoStreams[1].height, 180U);
    EXPECT_EQ(videoStreams[2].width, 213U);
    EXPECT_EQ(videoStreams[2].height, 120U);
  }

  SendVideoFrame(1280, 720, 3);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams.size(), 3U);
    EXPECT_EQ(videoStreams[0].width, 640U);
    EXPECT_EQ(videoStreams[0].height, 360U);
    EXPECT_EQ(videoStreams[1].width, 320U);
    EXPECT_EQ(videoStreams[1].height, 180U);
    EXPECT_EQ(videoStreams[2].width, 213U);
    EXPECT_EQ(videoStreams[2].height, 120U);
  }

  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig = *aControl.mVideoSendCodec.Ref();
    codecConfig.mEncodings[0].constraints.scaleDownBy = 1;
    codecConfig.mEncodings[1].constraints.scaleDownBy = 2;
    codecConfig.mEncodings[2].constraints.scaleDownBy = 4;
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  SendVideoFrame(1280, 720, 4);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams.size(), 3U);
    EXPECT_EQ(videoStreams[0].width, 1280U);
    EXPECT_EQ(videoStreams[0].height, 720U);
    EXPECT_EQ(videoStreams[1].width, 640U);
    EXPECT_EQ(videoStreams[1].height, 360U);
    EXPECT_EQ(videoStreams[2].width, 320U);
    EXPECT_EQ(videoStreams[2].height, 180U);
  }
}

TEST_F(VideoConduitTest, TestReconfigureReceiveMediaCodecs) {
  // Defaults
  mControl.Update([&](auto& aControl) {
    aControl.mReceiving = true;
    aControl.mVideoRecvCodecs = {
        VideoCodecConfig(120, "VP8", VideoEncodingConstraints())};
    aControl.mVideoRecvRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].payload_type, 120);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].video_format.name, "VP8");
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.local_ssrc, 0U);
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 0U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.nack.rtp_history_ms, 0);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.remb);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.tmmbr);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kNone);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.ulpfec_payload_type, -1);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.red_payload_type, -1);
  ASSERT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);

  // FEC
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfigFecFb(120, "VP8", VideoEncodingConstraints());
    codecConfigFecFb.mFECFbSet = true;
    VideoCodecConfig codecConfigFEC(1, "ulpfec", VideoEncodingConstraints());
    VideoCodecConfig codecConfigRED(2, "red", VideoEncodingConstraints());
    aControl.mVideoRecvCodecs = {codecConfigFecFb, codecConfigFEC,
                                 codecConfigRED};
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].payload_type, 120);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].video_format.name, "VP8");
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.local_ssrc, 0U);
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 0U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.nack.rtp_history_ms, 0);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.remb);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.tmmbr);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kNone);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.ulpfec_payload_type, 1);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.red_payload_type, 2);
  ASSERT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);

  // H264
  mControl.Update([&](auto& aControl) {
    aControl.mVideoRecvCodecs = {
        VideoCodecConfig(120, "H264", VideoEncodingConstraints())};
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].payload_type, 120);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].video_format.name, "H264");
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.local_ssrc, 0U);
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 0U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.nack.rtp_history_ms, 0);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.remb);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.tmmbr);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kNone);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.ulpfec_payload_type, -1);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.red_payload_type, -1);
  ASSERT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);

  // Nack
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfigNack(120, "VP8", VideoEncodingConstraints());
    codecConfigNack.mNackFbTypes.push_back("");
    aControl.mVideoRecvCodecs = {codecConfigNack};
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].payload_type, 120);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].video_format.name, "VP8");
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.local_ssrc, 0U);
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 0U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.nack.rtp_history_ms, 1000);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.remb);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.tmmbr);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kNone);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.ulpfec_payload_type, -1);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.red_payload_type, -1);
  ASSERT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);

  // Remb
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfigRemb(120, "VP8", VideoEncodingConstraints());
    codecConfigRemb.mRembFbSet = true;
    aControl.mVideoRecvCodecs = {codecConfigRemb};
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].payload_type, 120);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].video_format.name, "VP8");
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.local_ssrc, 0U);
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 0U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.nack.rtp_history_ms, 0);
  ASSERT_TRUE(Call()->mVideoReceiveConfig->rtp.remb);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.tmmbr);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kNone);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.ulpfec_payload_type, -1);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.red_payload_type, -1);
  ASSERT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);

  // Tmmbr
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfigTmmbr(120, "VP8", VideoEncodingConstraints());
    codecConfigTmmbr.mCcmFbTypes.push_back("tmmbr");
    aControl.mVideoRecvCodecs = {codecConfigTmmbr};
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders.size(), 1U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].payload_type, 120);
  ASSERT_EQ(Call()->mVideoReceiveConfig->decoders[0].video_format.name, "VP8");
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.local_ssrc, 0U);
  ASSERT_NE(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 0U);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.nack.rtp_history_ms, 0);
  ASSERT_FALSE(Call()->mVideoReceiveConfig->rtp.remb);
  ASSERT_TRUE(Call()->mVideoReceiveConfig->rtp.tmmbr);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.keyframe_method,
            webrtc::KeyFrameReqMethod::kNone);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.ulpfec_payload_type, -1);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.red_payload_type, -1);
  ASSERT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);
}

TEST_P(VideoConduitCodecModeTest, TestReconfigureSendMediaCodec) {
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = GetParam();
  });
  ASSERT_FALSE(Call()->mVideoSendConfig);

  // Defaults
  mControl.Update([&](auto& aControl) { aControl.mTransmitting = true; });
  ASSERT_TRUE(Call()->mVideoSendConfig);
  EXPECT_EQ(Call()->mVideoSendConfig->rtp.payload_name, "VP8");
  EXPECT_EQ(Call()->mVideoSendConfig->rtp.payload_type, 120);
  EXPECT_EQ(Call()->mVideoSendConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  EXPECT_EQ(Call()->mVideoSendConfig->rtp.max_packet_size, mozilla::kVideoMtu);
  EXPECT_EQ(Call()->mVideoSendEncoderConfig->content_type,
            GetParam() == webrtc::VideoCodecMode::kRealtimeVideo
                ? VideoEncoderConfig::ContentType::kRealtimeVideo
                : VideoEncoderConfig::ContentType::kScreen);
  EXPECT_EQ(Call()->mVideoSendEncoderConfig->min_transmit_bitrate_bps, 0);
  EXPECT_EQ(Call()->mVideoSendEncoderConfig->max_bitrate_bps, KBPS(10000));
  EXPECT_EQ(Call()->mVideoSendEncoderConfig->number_of_streams, 1U);
  mControl.Update([&](auto& aControl) { aControl.mTransmitting = false; });

  // FEC
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfigFEC(120, "VP8", VideoEncodingConstraints());
    codecConfigFEC.mEncodings.emplace_back();
    codecConfigFEC.mFECFbSet = true;
    codecConfigFEC.mNackFbTypes.push_back("");
    codecConfigFEC.mULPFECPayloadType = 1;
    codecConfigFEC.mREDPayloadType = 2;
    codecConfigFEC.mREDRTXPayloadType = 3;
    aControl.mVideoSendCodec = Some(codecConfigFEC);
  });
  ASSERT_TRUE(Call()->mVideoSendConfig);
  EXPECT_EQ(Call()->mVideoSendConfig->rtp.ulpfec.ulpfec_payload_type, 1);
  EXPECT_EQ(Call()->mVideoSendConfig->rtp.ulpfec.red_payload_type, 2);
  EXPECT_EQ(Call()->mVideoSendConfig->rtp.ulpfec.red_rtx_payload_type, 3);
  mControl.Update([&](auto& aControl) { aControl.mTransmitting = false; });

  // H264
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfigH264(120, "H264", VideoEncodingConstraints());
    codecConfigH264.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfigH264);
  });
  ASSERT_TRUE(Call()->mVideoSendConfig);
  EXPECT_EQ(Call()->mVideoSendConfig->rtp.payload_name, "H264");
  EXPECT_EQ(Call()->mVideoSendConfig->rtp.payload_type, 120);
  mControl.Update([&](auto& aControl) { aControl.mTransmitting = false; });

  // TIAS
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfigTias(120, "VP8", VideoEncodingConstraints());
    codecConfigTias.mEncodings.emplace_back();
    codecConfigTias.mTias = 2000000;
    aControl.mVideoSendCodec = Some(codecConfigTias);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  EXPECT_EQ(Call()->mVideoSendEncoderConfig->max_bitrate_bps, 2000000);
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->number_of_streams, 1U);
  EXPECT_EQ(
      Call()->mVideoSendEncoderConfig->simulcast_layers[0].max_bitrate_bps,
      2000000);
  SendVideoFrame(1280, 720, 1);

  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(1280, 720);
    ASSERT_EQ(videoStreams.size(), 1U);
    EXPECT_EQ(videoStreams[0].min_bitrate_bps, 30000);
    EXPECT_EQ(videoStreams[0].target_bitrate_bps, 1500000);
    EXPECT_EQ(videoStreams[0].max_bitrate_bps, 2000000);
  }
  mControl.Update([&](auto& aControl) { aControl.mTransmitting = false; });

  // MaxBr
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    VideoCodecConfig::Encoding encoding;
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.constraints.maxBr = 50000;
    }
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  EXPECT_EQ(Call()->mVideoSendEncoderConfig->max_bitrate_bps, 50000);
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->number_of_streams, 1U);
  EXPECT_EQ(
      Call()->mVideoSendEncoderConfig->simulcast_layers[0].max_bitrate_bps,
      50000);
  SendVideoFrame(1280, 720, 2);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(1280, 720);
    ASSERT_EQ(videoStreams.size(), 1U);
    EXPECT_LE(videoStreams[0].min_bitrate_bps, 50000);
    EXPECT_LE(videoStreams[0].target_bitrate_bps, 50000);
    EXPECT_EQ(videoStreams[0].max_bitrate_bps, 50000);
  }
  mControl.Update([&](auto& aControl) { aControl.mTransmitting = false; });

  // MaxFs
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfigMaxFs(120, "VP8", VideoEncodingConstraints());
    codecConfigMaxFs.mEncodingConstraints.maxFs = 3600;
    VideoCodecConfig::Encoding encoding;
    encoding.constraints.maxBr = 0;
    codecConfigMaxFs.mEncodings.push_back(encoding);
    aControl.mVideoSendCodec = Some(codecConfigMaxFs);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  {
    SendVideoFrame(1280, 720, 3);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    EXPECT_EQ(videoStreams[0].width, 1280U);
    EXPECT_EQ(videoStreams[0].height, 720U);
    EXPECT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 3000U);
    EXPECT_EQ(mVideoSink->mOnFrameCount, 3U);
  }

  {
    SendVideoFrame(640, 360, 4);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    EXPECT_EQ(videoStreams[0].width, 640U);
    EXPECT_EQ(videoStreams[0].height, 360U);
    EXPECT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 4000U);
    EXPECT_EQ(mVideoSink->mOnFrameCount, 4U);
  }

  {
    SendVideoFrame(1920, 1280, 5);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    EXPECT_EQ(videoStreams[0].width, 1174U);
    EXPECT_EQ(videoStreams[0].height, 783U);
    EXPECT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 5000U);
    EXPECT_EQ(mVideoSink->mOnFrameCount, 5U);
  }
}

TEST_P(VideoConduitCodecModeTest,
       TestReconfigureSendMediaCodecWhileTransmitting) {
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = GetParam();
  });
  ASSERT_FALSE(Call()->mVideoSendConfig);

  // Defaults
  mControl.Update([&](auto& aControl) { aControl.mTransmitting = true; });
  ASSERT_TRUE(Call()->mVideoSendConfig);
  EXPECT_EQ(Call()->mVideoSendConfig->rtp.payload_name, "VP8");
  EXPECT_EQ(Call()->mVideoSendConfig->rtp.payload_type, 120);
  EXPECT_EQ(Call()->mVideoSendConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kCompound);
  EXPECT_EQ(Call()->mVideoSendConfig->rtp.max_packet_size, mozilla::kVideoMtu);
  EXPECT_EQ(Call()->mVideoSendEncoderConfig->content_type,
            GetParam() == webrtc::VideoCodecMode::kRealtimeVideo
                ? VideoEncoderConfig::ContentType::kRealtimeVideo
                : VideoEncoderConfig::ContentType::kScreen);
  EXPECT_EQ(Call()->mVideoSendEncoderConfig->min_transmit_bitrate_bps, 0);
  EXPECT_EQ(Call()->mVideoSendEncoderConfig->max_bitrate_bps, KBPS(10000));
  EXPECT_EQ(Call()->mVideoSendEncoderConfig->number_of_streams, 1U);

  // Changing these parameters should not require flipping mTransmitting for the
  // changes to take effect.

  // TIAS
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfigTias(120, "VP8", VideoEncodingConstraints());
    codecConfigTias.mEncodings.emplace_back();
    codecConfigTias.mTias = 2000000;
    aControl.mVideoSendCodec = Some(codecConfigTias);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->max_bitrate_bps, 2000000);
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->number_of_streams, 1U);
  EXPECT_EQ(
      Call()->mVideoSendEncoderConfig->simulcast_layers[0].max_bitrate_bps,
      2000000);
  SendVideoFrame(1280, 720, 1);

  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(1280, 720);
    ASSERT_EQ(videoStreams.size(), 1U);
    EXPECT_EQ(videoStreams[0].min_bitrate_bps, 30000);
    EXPECT_EQ(videoStreams[0].target_bitrate_bps, 1500000);
    EXPECT_EQ(videoStreams[0].max_bitrate_bps, 2000000);
  }

  // MaxBr
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.constraints.maxBr = 50000;
    }
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->number_of_streams, 1U);
  EXPECT_EQ(
      Call()->mVideoSendEncoderConfig->simulcast_layers[0].max_bitrate_bps,
      50000);
  SendVideoFrame(1280, 720, 2);
  {
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(1280, 720);
    ASSERT_EQ(videoStreams.size(), 1U);
    EXPECT_LE(videoStreams[0].min_bitrate_bps, 50000);
    EXPECT_LE(videoStreams[0].target_bitrate_bps, 50000);
    EXPECT_EQ(videoStreams[0].max_bitrate_bps, 50000);
  }

  // MaxFs
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodingConstraints.maxFs = 3600;
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.constraints.maxBr = 0;
    }
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  {
    SendVideoFrame(1280, 720, 3);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    EXPECT_EQ(videoStreams[0].width, 1280U);
    EXPECT_EQ(videoStreams[0].height, 720U);
    EXPECT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 3000U);
    EXPECT_EQ(mVideoSink->mOnFrameCount, 3U);
  }

  {
    SendVideoFrame(641, 360, 4);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    EXPECT_EQ(videoStreams[0].width, 641U);
    EXPECT_EQ(videoStreams[0].height, 360U);
    EXPECT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 4000U);
    EXPECT_EQ(mVideoSink->mOnFrameCount, 4U);
  }

  {
    SendVideoFrame(1920, 1280, 5);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    EXPECT_EQ(videoStreams[0].width, 1174U);
    EXPECT_EQ(videoStreams[0].height, 783U);
    EXPECT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 5000U);
    EXPECT_EQ(mVideoSink->mOnFrameCount, 5U);
  }

  // ScaleResolutionDownBy
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.constraints.maxFs = 0;
      encoding.constraints.scaleDownBy = 3.7;
    }
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  {
    SendVideoFrame(1280, 720, 6);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    EXPECT_EQ(videoStreams[0].width, 345U);
    EXPECT_EQ(videoStreams[0].height, 194U);
    EXPECT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 6000U);
    EXPECT_EQ(mVideoSink->mOnFrameCount, 6U);
  }

  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfigScaleDownBy = *aControl.mVideoSendCodec.Ref();
    codecConfigScaleDownBy.mEncodings[0].constraints.scaleDownBy = 1.3;
    aControl.mVideoSendCodec = Some(codecConfigScaleDownBy);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  {
    SendVideoFrame(641, 359, 7);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    EXPECT_EQ(videoStreams[0].width, 493U);
    EXPECT_EQ(videoStreams[0].height, 276U);
    EXPECT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 7000U);
    EXPECT_EQ(mVideoSink->mOnFrameCount, 7U);
  }
}

TEST_P(VideoConduitCodecModeTest, TestVideoEncode) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = GetParam();
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  SendVideoFrame(1280, 720, 1);
  ASSERT_EQ(mVideoSink->mVideoFrame.width(), 1280);
  ASSERT_EQ(mVideoSink->mVideoFrame.height(), 720);
  ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 1000U);
  ASSERT_EQ(mVideoSink->mOnFrameCount, 1U);

  SendVideoFrame(640, 360, 2);
  ASSERT_EQ(mVideoSink->mVideoFrame.width(), 640);
  ASSERT_EQ(mVideoSink->mVideoFrame.height(), 360);
  ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 2000U);
  ASSERT_EQ(mVideoSink->mOnFrameCount, 2U);

  SendVideoFrame(1920, 1280, 3);
  ASSERT_EQ(mVideoSink->mVideoFrame.width(), 1920);
  ASSERT_EQ(mVideoSink->mVideoFrame.height(), 1280);
  ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 3000U);
  ASSERT_EQ(mVideoSink->mOnFrameCount, 3U);
}

TEST_P(VideoConduitCodecModeTest, TestVideoEncodeMaxFs) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodingConstraints.maxFs = 3600;
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = GetParam();
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  {
    SendVideoFrame(1280, 720, 1);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 1280U);
    ASSERT_EQ(videoStreams[0].height, 720U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 1000U);
    ASSERT_EQ(mVideoSink->mOnFrameCount, 1U);
  }

  {
    SendVideoFrame(640, 360, 2);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 640U);
    ASSERT_EQ(videoStreams[0].height, 360U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 2000U);
    ASSERT_EQ(mVideoSink->mOnFrameCount, 2U);
  }

  {
    SendVideoFrame(1920, 1280, 3);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 1174U);
    ASSERT_EQ(videoStreams[0].height, 783U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 3000U);
    ASSERT_EQ(mVideoSink->mOnFrameCount, 3U);
  }

  // maxFs should not force pixel count above what a mVideoSink has requested.
  // We set 3600 macroblocks (16x16 pixels), so we request 3500 here.
  webrtc::VideoSinkWants wants;
  wants.max_pixel_count = 3500 * 16 * 16;
  mVideoFrameConverter->AddOrUpdateSink(mVideoSink.get(), wants);

  {
    SendVideoFrame(1280, 720, 4);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 960U);
    ASSERT_EQ(videoStreams[0].height, 540U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 4000U);
    ASSERT_EQ(mVideoSink->mOnFrameCount, 4U);
  }

  {
    SendVideoFrame(640, 360, 5);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 640U);
    ASSERT_EQ(videoStreams[0].height, 360U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 5000U);
    ASSERT_EQ(mVideoSink->mOnFrameCount, 5U);
  }

  {
    SendVideoFrame(1920, 1280, 6);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 960U);
    ASSERT_EQ(videoStreams[0].height, 640U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 6000U);
    ASSERT_EQ(mVideoSink->mOnFrameCount, 6U);
  }
}

TEST_P(VideoConduitCodecModeTest, TestVideoEncodeMaxFsNegotiatedThenSinkWants) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    codecConfig.mEncodingConstraints.maxFs = 3500;
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = GetParam();
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  unsigned int frame = 0;

  {
    SendVideoFrame(1280, 720, frame++);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 1260U);
    ASSERT_EQ(videoStreams[0].height, 709U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), (frame - 1) * 1000);
    ASSERT_EQ(mVideoSink->mOnFrameCount, frame);
  }

  webrtc::VideoSinkWants wants;
  wants.max_pixel_count = 3600 * 16 * 16;
  mVideoFrameConverter->AddOrUpdateSink(mVideoSink.get(), wants);

  {
    SendVideoFrame(1280, 720, frame++);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 1260U);
    ASSERT_EQ(videoStreams[0].height, 709U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), (frame - 1) * 1000);
    ASSERT_EQ(mVideoSink->mOnFrameCount, frame);
  }
}

TEST_P(VideoConduitCodecModeTest, TestVideoEncodeMaxFsCodecChange) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    codecConfig.mEncodingConstraints.maxFs = 3500;
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = GetParam();
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  unsigned int frame = 0;

  {
    SendVideoFrame(1280, 720, frame++);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 1260U);
    ASSERT_EQ(videoStreams[0].height, 709U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), (frame - 1) * 1000);
    ASSERT_EQ(mVideoSink->mOnFrameCount, frame);
  }

  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(121, "VP9", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    codecConfig.mEncodingConstraints.maxFs = 3500;
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  {
    SendVideoFrame(1280, 720, frame++);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 1260U);
    ASSERT_EQ(videoStreams[0].height, 709U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), (frame - 1) * 1000);
    ASSERT_EQ(mVideoSink->mOnFrameCount, frame);
  }
}

TEST_P(VideoConduitCodecModeTest,
       TestVideoEncodeMaxFsSinkWantsThenCodecChange) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = GetParam();
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  webrtc::VideoSinkWants wants;
  wants.max_pixel_count = 3500 * 16 * 16;
  mVideoFrameConverter->AddOrUpdateSink(mVideoSink.get(), wants);

  unsigned int frame = 0;

  SendVideoFrame(1280, 720, frame++);
  const std::vector<webrtc::VideoStream> videoStreams =
      Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                   mVideoSink->mVideoFrame.height());
  ASSERT_EQ(videoStreams[0].width, 960U);
  ASSERT_EQ(videoStreams[0].height, 540U);
  ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), (frame - 1) * 1000);
  ASSERT_EQ(mVideoSink->mOnFrameCount, frame);

  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(121, "VP9", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  {
    SendVideoFrame(1280, 720, frame++);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 960U);
    ASSERT_EQ(videoStreams[0].height, 540U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), (frame - 1) * 1000);
    ASSERT_EQ(mVideoSink->mOnFrameCount, frame);
  }
}

TEST_P(VideoConduitCodecModeTest, TestVideoEncodeMaxFsNegotiated) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = GetParam();
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  unsigned int frame = 0;
  SendVideoFrame(1280, 720, frame++);
  ASSERT_EQ(mVideoSink->mVideoFrame.width(), 1280);
  ASSERT_EQ(mVideoSink->mVideoFrame.height(), 720);
  ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), (frame - 1) * 1000);
  ASSERT_EQ(mVideoSink->mOnFrameCount, frame);

  // Ensure that negotiating a new max-fs works
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig = *aControl.mVideoSendCodec.Ref();
    codecConfig.mEncodingConstraints.maxFs = 3500;
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  {
    SendVideoFrame(1280, 720, frame++);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 1260U);
    ASSERT_EQ(videoStreams[0].height, 709U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), (frame - 1) * 1000);
    ASSERT_EQ(mVideoSink->mOnFrameCount, frame);
  }

  // Ensure that negotiating max-fs away works
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig = *aControl.mVideoSendCodec.Ref();
    codecConfig.mEncodingConstraints.maxFs = 0;
    aControl.mVideoSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  SendVideoFrame(1280, 720, frame++);
  ASSERT_EQ(mVideoSink->mVideoFrame.width(), 1280);
  ASSERT_EQ(mVideoSink->mVideoFrame.height(), 720);
  ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), (frame - 1) * 1000);
  ASSERT_EQ(mVideoSink->mOnFrameCount, frame);
}

TEST_P(VideoConduitCodecModeTest, TestVideoEncodeMaxWidthAndHeight) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodingConstraints.maxWidth = 1280;
    codecConfig.mEncodingConstraints.maxHeight = 720;
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = GetParam();
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  SendVideoFrame(1280, 720, 1);
  ASSERT_EQ(mVideoSink->mVideoFrame.width(), 1280);
  ASSERT_EQ(mVideoSink->mVideoFrame.height(), 720);
  ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 1000U);
  ASSERT_EQ(mVideoSink->mOnFrameCount, 1U);

  SendVideoFrame(640, 360, 2);
  ASSERT_EQ(mVideoSink->mVideoFrame.width(), 640);
  ASSERT_EQ(mVideoSink->mVideoFrame.height(), 360);
  ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 2000U);
  ASSERT_EQ(mVideoSink->mOnFrameCount, 2U);

  {
    SendVideoFrame(1920, 1280, 3);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 1080U);
    ASSERT_EQ(videoStreams[0].height, 720U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 3000U);
    ASSERT_EQ(mVideoSink->mOnFrameCount, 3U);
  }
}

TEST_P(VideoConduitCodecModeTest, TestVideoEncodeScaleResolutionBy) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodingConstraints.maxFs = 3600;
    auto& encoding = codecConfig.mEncodings.emplace_back();
    encoding.constraints.scaleDownBy = 2;
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = GetParam();
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  {
    SendVideoFrame(1280, 720, 1);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 640U);
    ASSERT_EQ(videoStreams[0].height, 360U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 1000U);
    ASSERT_EQ(mVideoSink->mOnFrameCount, 1U);
  }

  {
    SendVideoFrame(640, 360, 2);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 320U);
    ASSERT_EQ(videoStreams[0].height, 180U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 2000U);
    ASSERT_EQ(mVideoSink->mOnFrameCount, 2U);
  }
}

TEST_P(VideoConduitCodecModeTest, TestVideoEncodeSimulcastScaleResolutionBy) {
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.constraints.scaleDownBy = 2;
    }
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.constraints.scaleDownBy = 3;
    }
    {
      auto& encoding = codecConfig.mEncodings.emplace_back();
      encoding.constraints.scaleDownBy = 4;
    }

    aControl.mTransmitting = true;
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mVideoCodecMode = GetParam();
    aControl.mLocalSsrcs = {42, 43, 44};
    aControl.mLocalVideoRtxSsrcs = {45, 46, 47};
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

  {
    SendVideoFrame(640, 480, 1);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 320U);
    ASSERT_EQ(videoStreams[0].height, 240U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 1000U);
    ASSERT_EQ(mVideoSink->mOnFrameCount, 1U);
  }

  {
    SendVideoFrame(1280, 720, 2);
    const std::vector<webrtc::VideoStream> videoStreams =
        Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                     mVideoSink->mVideoFrame.height());
    ASSERT_EQ(videoStreams[0].width, 640U);
    ASSERT_EQ(videoStreams[0].height, 360U);
    ASSERT_EQ(mVideoSink->mVideoFrame.timestamp_us(), 2000U);
    ASSERT_EQ(mVideoSink->mOnFrameCount, 2U);
  }
}

TEST_P(VideoConduitCodecModeTest,
       TestVideoEncodeLargeScaleResolutionByFrameDropping) {
  const std::vector<std::vector<uint32_t>> scalesList = {
      {200U}, {200U, 300U}, {300U, 200U}};
  int64_t capture_time_ms = 0;
  for (size_t i = 0; i < scalesList.size(); ++i) {
    const auto& scales = scalesList[i];
    mControl.Update([&](auto& aControl) {
      aControl.mTransmitting = true;
      VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
      for (const auto& scale : scales) {
        auto& encoding = codecConfig.mEncodings.emplace_back();
        encoding.constraints.scaleDownBy = scale;
      }
      aControl.mVideoSendCodec = Some(codecConfig);
      aControl.mVideoSendRtpRtcpConfig =
          Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
      aControl.mVideoCodecMode = GetParam();
      aControl.mLocalSsrcs = scales;
    });
    ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

    {
      // If all layers' scaleDownBy is larger than any input dimension, that
      // dimension becomes zero.
      SendVideoFrame(199, 199, ++capture_time_ms);
      const std::vector<webrtc::VideoStream> videoStreams =
          Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                       mVideoSink->mVideoFrame.height());
      ASSERT_EQ(videoStreams.size(), scales.size());
      for (size_t j = 0; j < scales.size(); ++j) {
        EXPECT_EQ(videoStreams[j].width, 0U)
            << " for scalesList[" << i << "][" << j << "]";
        EXPECT_EQ(videoStreams[j].height, 0U)
            << " for scalesList[" << i << "][" << j << "]";
      }
    }

    {
      // If only width becomes zero, height is also set to zero.
      SendVideoFrame(199, 200, ++capture_time_ms);
      const std::vector<webrtc::VideoStream> videoStreams =
          Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                       mVideoSink->mVideoFrame.height());
      ASSERT_EQ(videoStreams.size(), scales.size());
      for (size_t j = 0; j < scales.size(); ++j) {
        EXPECT_EQ(videoStreams[j].width, 0U)
            << " for scalesList[" << i << "][" << j << "]";
        EXPECT_EQ(videoStreams[j].height, 0U)
            << " for scalesList[" << i << "][" << j << "]";
      }
    }

    {
      // If only height becomes zero, width is also set to zero.
      SendVideoFrame(200, 199, ++capture_time_ms);
      const std::vector<webrtc::VideoStream> videoStreams =
          Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                       mVideoSink->mVideoFrame.height());
      ASSERT_EQ(videoStreams.size(), scales.size());
      for (size_t j = 0; j < scales.size(); ++j) {
        EXPECT_EQ(videoStreams[j].width, 0U)
            << " for scalesList[" << i << "][" << j << "]";
        EXPECT_EQ(videoStreams[j].height, 0U)
            << " for scalesList[" << i << "][" << j << "]";
      }
    }

    {
      // If dimensions are non-zero, we pass through.
      SendVideoFrame(200, 200, ++capture_time_ms);
      const std::vector<webrtc::VideoStream> videoStreams =
          Call()->CreateEncoderStreams(mVideoSink->mVideoFrame.width(),
                                       mVideoSink->mVideoFrame.height());
      ASSERT_EQ(videoStreams.size(), scales.size());
      for (size_t j = 0; j < scales.size(); ++j) {
        EXPECT_EQ(videoStreams[j].width, scales[j] <= 200U ? 1U : 0U)
            << " for scalesList[" << i << "][" << j << "]";
        EXPECT_EQ(videoStreams[j].height, scales[j] <= 200U ? 1U : 0U)
            << " for scalesList[" << i << "][" << j << "]";
      }
    }
  }
}

TEST_P(VideoConduitCodecModeTest,
       TestVideoEncodeLargeScaleResolutionByStreamCreation) {
  for (const auto& scales :
       {std::vector{200U}, std::vector{200U, 300U}, std::vector{300U, 200U}}) {
    mControl.Update([&](auto& aControl) {
      aControl.mTransmitting = true;
      VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
      for (const auto& scale : scales) {
        auto& encoding = codecConfig.mEncodings.emplace_back();
        encoding.constraints.scaleDownBy = scale;
      }
      aControl.mVideoSendCodec = Some(codecConfig);
      aControl.mVideoSendRtpRtcpConfig =
          Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
      aControl.mVideoCodecMode = GetParam();
      aControl.mLocalSsrcs = scales;
    });
    ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

    {
      // If dimensions scale to <1, we create a 0x0 stream.
      const std::vector<webrtc::VideoStream> videoStreams =
          Call()->CreateEncoderStreams(199, 199);
      ASSERT_EQ(videoStreams.size(), scales.size());
      for (const auto& stream : videoStreams) {
        EXPECT_EQ(stream.width, 0U);
        EXPECT_EQ(stream.height, 0U);
      }
    }

    {
      // If width scales to <1, we create a 0x0 stream.
      const std::vector<webrtc::VideoStream> videoStreams =
          Call()->CreateEncoderStreams(199, 200);
      ASSERT_EQ(videoStreams.size(), scales.size());
      for (const auto& stream : videoStreams) {
        EXPECT_EQ(stream.width, 0U);
        EXPECT_EQ(stream.height, 0U);
      }
    }

    {
      // If height scales to <1, we create a 0x0 stream.
      const std::vector<webrtc::VideoStream> videoStreams =
          Call()->CreateEncoderStreams(200, 199);
      ASSERT_EQ(videoStreams.size(), scales.size());
      for (const auto& stream : videoStreams) {
        EXPECT_EQ(stream.width, 0U);
        EXPECT_EQ(stream.height, 0U);
      }
    }

    {
      // If dimensions scale to 1, we create a 1x1 stream.
      const std::vector<webrtc::VideoStream> videoStreams =
          Call()->CreateEncoderStreams(200, 200);
      ASSERT_EQ(videoStreams.size(), scales.size());
      for (size_t i = 0; i < scales.size(); ++i) {
        const auto& stream = videoStreams[i];
        const auto scale = scales[i];
        EXPECT_EQ(stream.width, scale <= 200U ? 1U : 0U);
        EXPECT_EQ(stream.height, scale <= 200U ? 1U : 0U);
      }
    }

    {
      // If one dimension scales to 0 and the other >1, we create a 0x0 stream.
      const std::vector<webrtc::VideoStream> videoStreams =
          Call()->CreateEncoderStreams(400, 199);
      ASSERT_EQ(videoStreams.size(), scales.size());
      for (const auto& stream : videoStreams) {
        EXPECT_EQ(stream.width, 0U);
        EXPECT_EQ(stream.height, 0U);
      }
    }

    {
      // Legit case scaling down to more than 1x1.
      const std::vector<webrtc::VideoStream> videoStreams =
          Call()->CreateEncoderStreams(600, 400);
      ASSERT_EQ(videoStreams.size(), scales.size());
      for (size_t i = 0; i < scales.size(); ++i) {
        // Streams are backwards for some reason
        const auto& stream = videoStreams[i];
        const auto& scale = scales[i];
        if (scale == 200U) {
          EXPECT_EQ(stream.width, 3U);
          EXPECT_EQ(stream.height, 2U);
        } else {
          EXPECT_EQ(stream.width, 2U);
          EXPECT_EQ(stream.height, 1U);
        }
      }
    }
  }
}

TEST_P(VideoConduitCodecModeTest, TestVideoEncodeResolutionAlignment) {
  for (const auto& scales : {std::vector{1U}, std::vector{1U, 9U}}) {
    mControl.Update([&](auto& aControl) {
      aControl.mTransmitting = true;
      VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
      for (const auto& scale : scales) {
        auto& encoding = codecConfig.mEncodings.emplace_back();
        encoding.constraints.scaleDownBy = scale;
      }
      aControl.mVideoSendCodec = Some(codecConfig);
      aControl.mVideoSendRtpRtcpConfig =
          Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
      aControl.mVideoCodecMode = GetParam();
      aControl.mLocalSsrcs = scales;
    });
    ASSERT_TRUE(Call()->mVideoSendEncoderConfig);

    for (const auto& alignment : {2, 16, 39, 400, 1000}) {
      // Test that requesting specific alignment always results in the expected
      // number of layers and valid alignment.

      // Mimic what libwebrtc would do for a given alignment.
      webrtc::VideoEncoder::EncoderInfo info;
      info.requested_resolution_alignment = alignment;
      Call()->SetEncoderInfo(info);

      const std::vector<webrtc::VideoStream> videoStreams =
          Call()->CreateEncoderStreams(640, 480);
      ASSERT_EQ(videoStreams.size(), scales.size());
      for (size_t i = 0; i < videoStreams.size(); ++i) {
        // videoStreams is backwards
        const auto& stream = videoStreams[i];
        const auto& scale = scales[i];
        EXPECT_EQ(stream.width % alignment, 0U)
            << " for scale " << scale << " and alignment " << alignment;
        EXPECT_EQ(stream.height % alignment, 0U);
      }
    }
  }
}

TEST_F(VideoConduitTest, TestSettingRtpRtcpRsize) {
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    RtpRtcpConfig rtcpConf(webrtc::RtcpMode::kReducedSize, true);

    aControl.mReceiving = true;
    aControl.mVideoRecvCodecs = {codecConfig};
    aControl.mVideoRecvRtpRtcpConfig = Some(rtcpConf);
    aControl.mTransmitting = true;
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig = Some(rtcpConf);
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kReducedSize);
  ASSERT_TRUE(Call()->mVideoSendConfig);
  ASSERT_EQ(Call()->mVideoSendConfig->rtp.rtcp_mode,
            webrtc::RtcpMode::kReducedSize);
}

TEST_F(VideoConduitTest, TestRemoteSsrcDefault) {
  mControl.Update([&](auto& aControl) {
    aControl.mRemoteSsrc = 0;
    aControl.mLocalSsrcs = {1};
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mReceiving = true;
    aControl.mTransmitting = true;
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_THAT(Call()->mVideoReceiveConfig->rtp.remote_ssrc,
              Not(testing::AnyOf(0U, 1U)));
  ASSERT_TRUE(Call()->mVideoSendConfig);
  ASSERT_THAT(Call()->mVideoSendConfig->rtp.ssrcs, ElementsAre(1U));
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.local_ssrc,
            Call()->mVideoSendConfig->rtp.ssrcs[0]);
}

TEST_F(VideoConduitTest, TestRemoteSsrcCollision) {
  mControl.Update([&](auto& aControl) {
    aControl.mRemoteSsrc = 1;
    aControl.mLocalSsrcs = {1};
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mReceiving = true;
    aControl.mTransmitting = true;
  });
  EXPECT_TRUE(Call()->mVideoReceiveConfig);
  EXPECT_EQ(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 1U);
  EXPECT_TRUE(Call()->mVideoSendConfig);
  EXPECT_THAT(Call()->mVideoSendConfig->rtp.ssrcs,
              ElementsAre(Not(testing::AnyOf(0U, 1U))));
  EXPECT_EQ(Call()->mVideoReceiveConfig->rtp.local_ssrc,
            Call()->mVideoSendConfig->rtp.ssrcs[0]);
}

TEST_F(VideoConduitTest, TestLocalSsrcDefault) {
  mControl.Update([&](auto& aControl) {
    aControl.mRemoteSsrc = 1;
    aControl.mLocalSsrcs = {};
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mReceiving = true;
    aControl.mTransmitting = true;
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 1U);
  ASSERT_TRUE(Call()->mVideoSendConfig);
  ASSERT_THAT(Call()->mVideoSendConfig->rtp.ssrcs,
              ElementsAre(Not(testing::AnyOf(0U, 1U))));
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.local_ssrc,
            Call()->mVideoSendConfig->rtp.ssrcs[0]);
}

TEST_F(VideoConduitTest, TestLocalSsrcCollision) {
  mControl.Update([&](auto& aControl) {
    aControl.mRemoteSsrc = 1;
    aControl.mLocalSsrcs = {2, 2};
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mReceiving = true;
    aControl.mTransmitting = true;
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 1U);
  ASSERT_TRUE(Call()->mVideoSendConfig);
  ASSERT_THAT(Call()->mVideoSendConfig->rtp.ssrcs,
              ElementsAre(2U, Not(testing::AnyOf(0U, 2U))));
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.local_ssrc,
            Call()->mVideoSendConfig->rtp.ssrcs[0]);
}

TEST_F(VideoConduitTest, TestLocalSsrcUnorderedCollision) {
  mControl.Update([&](auto& aControl) {
    aControl.mRemoteSsrc = 1;
    aControl.mLocalSsrcs = {2, 3, 2};
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    for (int i = 0; i < 3; ++i) {
      codecConfig.mEncodings.emplace_back();
    }
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mReceiving = true;
    aControl.mTransmitting = true;
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 1U);
  ASSERT_TRUE(Call()->mVideoSendConfig);
  ASSERT_THAT(Call()->mVideoSendConfig->rtp.ssrcs,
              ElementsAre(2U, 3U, Not(testing::AnyOf(0U, 2U))));
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.local_ssrc,
            Call()->mVideoSendConfig->rtp.ssrcs[0]);
}

TEST_F(VideoConduitTest, TestLocalAndRemoteSsrcCollision) {
  mControl.Update([&](auto& aControl) {
    aControl.mRemoteSsrc = 1;
    aControl.mLocalSsrcs = {1, 2, 2};
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    for (int i = 0; i < 3; ++i) {
      codecConfig.mEncodings.emplace_back();
    }
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mReceiving = true;
    aControl.mTransmitting = true;
  });
  ASSERT_TRUE(Call()->mVideoReceiveConfig);
  ASSERT_THAT(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 1U);
  ASSERT_TRUE(Call()->mVideoSendConfig);
  ASSERT_THAT(Call()->mVideoSendConfig->rtp.ssrcs,
              ElementsAre(Not(testing::AnyOf(0U, 1U, 2U)), 2U,
                          Not(testing::AnyOf(
                              0U, 1U, 2U,
                              Call()->mVideoReceiveConfig->rtp.remote_ssrc))));
  ASSERT_EQ(Call()->mVideoReceiveConfig->rtp.local_ssrc,
            Call()->mVideoSendConfig->rtp.ssrcs[0]);
}

TEST_F(VideoConduitTest, TestExternalRemoteSsrcCollision) {
  auto other = MakeRefPtr<MockConduit>();
  mCallWrapper->RegisterConduit(other);

  // First the mControl update should trigger an UnsetRemoteSSRC(1) from us.
  // Then we simulate another conduit using that same ssrc, which should trigger
  // us to generate a fresh ssrc that is not 0 and not 1.
  {
    InSequence s;
    EXPECT_CALL(*other, UnsetRemoteSSRC(1U)).Times(2);
    EXPECT_CALL(*other, UnsetRemoteSSRC(Not(testing::AnyOf(0U, 1U))));
  }

  mControl.Update([&](auto& aControl) {
    aControl.mRemoteSsrc = 1;
    aControl.mReceiving = true;
  });
  EXPECT_TRUE(Call()->mVideoReceiveConfig);
  EXPECT_EQ(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 1U);

  (void)WaitFor(InvokeAsync(
      GetCurrentSerialEventTarget(), __func__, [wrapper = mCallWrapper] {
        wrapper->UnsetRemoteSSRC(1);
        return GenericPromise::CreateAndResolve(true, __func__);
      }));

  EXPECT_TRUE(Call()->mVideoReceiveConfig);
  EXPECT_THAT(Call()->mVideoReceiveConfig->rtp.remote_ssrc,
              Not(testing::AnyOf(0U, 1U)));
}

TEST_F(VideoConduitTest, TestVideoConfigurationH264) {
  const int profileLevelId1 = 0x42E01F;
  const int profileLevelId2 = 0x64000C;
  const char* sprop1 = "foo bar";
  const char* sprop2 = "baz";

  // Test that VideoConduit propagates H264 configuration data properly.
  // We do two tests:
  // - Test valid data in packetization mode 0 (SingleNALU)
  // - Test different valid data in packetization mode 1 (NonInterleaved)

  {
    mControl.Update([&](auto& aControl) {
      aControl.mTransmitting = true;
      VideoCodecConfigH264 h264{};
      h264.packetization_mode = 0;
      h264.profile_level_id = profileLevelId1;
      strncpy(h264.sprop_parameter_sets, sprop1,
              sizeof(h264.sprop_parameter_sets) - 1);
      auto codecConfig = VideoCodecConfig::CreateH264Config(
          97, VideoEncodingConstraints(), h264);
      codecConfig.mEncodings.emplace_back();
      aControl.mVideoSendCodec = Some(codecConfig);
      aControl.mVideoSendRtpRtcpConfig =
          Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    });

    ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
    auto& params = Call()->mVideoSendEncoderConfig->video_format.parameters;
    EXPECT_EQ(params[webrtc::kH264FmtpPacketizationMode], "0");
    EXPECT_EQ(params[webrtc::kH264FmtpProfileLevelId], "42e01f");
    EXPECT_EQ(params[std::string(webrtc::kH264FmtpSpropParameterSets)], sprop1);
  }

  {
    mControl.Update([&](auto& aControl) {
      VideoCodecConfigH264 h264{};
      h264.packetization_mode = 1;
      h264.profile_level_id = profileLevelId2;
      strncpy(h264.sprop_parameter_sets, sprop2,
              sizeof(h264.sprop_parameter_sets) - 1);
      auto codecConfig = VideoCodecConfig::CreateH264Config(
          126, VideoEncodingConstraints(), h264);
      codecConfig.mEncodings.emplace_back();
      aControl.mVideoSendCodec = Some(codecConfig);
    });

    ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
    auto& params = Call()->mVideoSendEncoderConfig->video_format.parameters;
    EXPECT_EQ(params[webrtc::kH264FmtpPacketizationMode], "1");
    EXPECT_EQ(params[webrtc::kH264FmtpProfileLevelId], "64000c");
    EXPECT_EQ(params[std::string(webrtc::kH264FmtpSpropParameterSets)], sprop2);
  }
}

TEST_F(VideoConduitTest, TestVideoConfigurationAV1) {
  // Test that VideoConduit propagates AV1 configuration data properly.
  {
    mControl.Update([&](auto& aControl) {
      aControl.mTransmitting = true;
      auto av1Config = JsepVideoCodecDescription::Av1Config();

      av1Config.mProfile = Some(2);
      av1Config.mLevelIdx = Some(4);
      av1Config.mTier = Some(1);
      auto codecConfig = VideoCodecConfig::CreateAv1Config(
          99, VideoEncodingConstraints(), av1Config);
      codecConfig.mEncodings.emplace_back();
      aControl.mVideoSendCodec = Some(codecConfig);
      aControl.mVideoSendRtpRtcpConfig =
          Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    });

    ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
    auto& params = Call()->mVideoSendEncoderConfig->video_format.parameters;
    EXPECT_EQ(params[std::string(webrtc::kAv1FmtpProfile)], "2");
    EXPECT_EQ(params[std::string(webrtc::kAv1FmtpLevelIdx)], "4");
    EXPECT_EQ(params[std::string(webrtc::kAv1FmtpTier)], "1");
  }
}

TEST_F(VideoConduitTest, TestDegradationPreferences) {
  // Verify default value returned is MAINTAIN_FRAMERATE.
  ASSERT_EQ(mVideoConduit->DegradationPreference(),
            webrtc::DegradationPreference::MAINTAIN_FRAMERATE);

  // Verify that setting a degradation preference overrides default behavior.
  mControl.Update([&](auto& aControl) {
    aControl.mVideoDegradationPreference =
        webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mReceiving = true;
    aControl.mTransmitting = true;
  });
  ASSERT_EQ(mVideoConduit->DegradationPreference(),
            webrtc::DegradationPreference::MAINTAIN_RESOLUTION);
  ASSERT_EQ(Call()->mConfiguredDegradationPreference,
            webrtc::DegradationPreference::MAINTAIN_RESOLUTION);

  mControl.Update([&](auto& aControl) {
    aControl.mVideoDegradationPreference =
        webrtc::DegradationPreference::BALANCED;
  });
  ASSERT_EQ(mVideoConduit->DegradationPreference(),
            webrtc::DegradationPreference::BALANCED);
  ASSERT_EQ(Call()->mConfiguredDegradationPreference,
            webrtc::DegradationPreference::BALANCED);

  // Verify removing degradation preference returns default.
  mControl.Update([&](auto& aControl) {
    aControl.mVideoDegradationPreference =
        webrtc::DegradationPreference::DISABLED;
  });
  ASSERT_EQ(mVideoConduit->DegradationPreference(),
            webrtc::DegradationPreference::MAINTAIN_FRAMERATE);
  ASSERT_EQ(Call()->mConfiguredDegradationPreference,
            webrtc::DegradationPreference::MAINTAIN_FRAMERATE);

  // Verify with no degradation preference set changing codec mode to screen
  // sharing changes degradation to MAINTAIN_RESOLUTION.
  mControl.Update([&](auto& aControl) {
    aControl.mVideoCodecMode = webrtc::VideoCodecMode::kScreensharing;
  });
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->content_type,
            VideoEncoderConfig::ContentType::kScreen);
  ASSERT_EQ(mVideoConduit->DegradationPreference(),
            webrtc::DegradationPreference::MAINTAIN_RESOLUTION);
  ASSERT_EQ(Call()->mConfiguredDegradationPreference,
            webrtc::DegradationPreference::MAINTAIN_RESOLUTION);

  // Verify that setting a degradation preference overrides screen share
  // degradation value.
  mControl.Update([&](auto& aControl) {
    aControl.mVideoDegradationPreference =
        webrtc::DegradationPreference::MAINTAIN_FRAMERATE;
  });
  ASSERT_EQ(mVideoConduit->DegradationPreference(),
            webrtc::DegradationPreference::MAINTAIN_FRAMERATE);
  ASSERT_EQ(Call()->mConfiguredDegradationPreference,
            webrtc::DegradationPreference::MAINTAIN_FRAMERATE);

  mControl.Update([&](auto& aControl) {
    aControl.mVideoDegradationPreference =
        webrtc::DegradationPreference::BALANCED;
  });
  ASSERT_EQ(mVideoConduit->DegradationPreference(),
            webrtc::DegradationPreference::BALANCED);
  ASSERT_EQ(Call()->mConfiguredDegradationPreference,
            webrtc::DegradationPreference::BALANCED);

  // Verify removing degradation preference returns to screen sharing
  // degradation value.
  mControl.Update([&](auto& aControl) {
    aControl.mVideoDegradationPreference =
        webrtc::DegradationPreference::DISABLED;
  });
  ASSERT_EQ(mVideoConduit->DegradationPreference(),
            webrtc::DegradationPreference::MAINTAIN_RESOLUTION);
  ASSERT_EQ(Call()->mConfiguredDegradationPreference,
            webrtc::DegradationPreference::MAINTAIN_RESOLUTION);

  // Verify changing codec mode back to real time with no degradation
  // preference set returns degradation to MAINTAIN_FRAMERATE.
  mControl.Update([&](auto& aControl) {
    aControl.mVideoCodecMode = webrtc::VideoCodecMode::kRealtimeVideo;
  });
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->content_type,
            VideoEncoderConfig::ContentType::kRealtimeVideo);
  ASSERT_EQ(mVideoConduit->DegradationPreference(),
            webrtc::DegradationPreference::MAINTAIN_FRAMERATE);
  ASSERT_EQ(Call()->mConfiguredDegradationPreference,
            webrtc::DegradationPreference::MAINTAIN_FRAMERATE);

  // Verify that if a degradation preference was set changing mode does not
  // override the set preference.
  mControl.Update([&](auto& aControl) {
    aControl.mVideoDegradationPreference =
        webrtc::DegradationPreference::BALANCED;
  });
  ASSERT_EQ(mVideoConduit->DegradationPreference(),
            webrtc::DegradationPreference::BALANCED);
  ASSERT_EQ(Call()->mConfiguredDegradationPreference,
            webrtc::DegradationPreference::BALANCED);

  mControl.Update([&](auto& aControl) {
    aControl.mVideoCodecMode = webrtc::VideoCodecMode::kScreensharing;
  });
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->content_type,
            VideoEncoderConfig::ContentType::kScreen);
  ASSERT_EQ(mVideoConduit->DegradationPreference(),
            webrtc::DegradationPreference::BALANCED);
  ASSERT_EQ(Call()->mConfiguredDegradationPreference,
            webrtc::DegradationPreference::BALANCED);

  mControl.Update([&](auto& aControl) {
    aControl.mVideoCodecMode = webrtc::VideoCodecMode::kRealtimeVideo;
  });
  ASSERT_EQ(Call()->mVideoSendEncoderConfig->content_type,
            VideoEncoderConfig::ContentType::kRealtimeVideo);
  ASSERT_EQ(mVideoConduit->DegradationPreference(),
            webrtc::DegradationPreference::BALANCED);
  ASSERT_EQ(Call()->mConfiguredDegradationPreference,
            webrtc::DegradationPreference::BALANCED);
}

TEST_F(VideoConduitTest, TestRemoteRtxSsrc) {
  // Verify RTX is configured.
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    codecConfig.mRTXPayloadType = 121;
    aControl.mVideoRecvCodecs = {codecConfig};
    aControl.mVideoRecvRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mReceiving = true;
    aControl.mRemoteSsrc = 2;
    aControl.mRemoteVideoRtxSsrc = 43;
  });

  EXPECT_THAT(Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types,
              UnorderedElementsAre(Pair(121, 120)));
  EXPECT_EQ(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 2U);
  EXPECT_EQ(Call()->mVideoReceiveConfig->rtp.rtx_ssrc, 43U);

  // Bug 1956426 verify, if the recv codecs change but signaled SSRC has not,
  // that RTX is still configured.
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    VideoCodecConfig codecConfig264(96, "H264", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    codecConfig.mRTXPayloadType = 121;
    codecConfig264.mRTXPayloadType = 97;
    aControl.mVideoRecvCodecs = {codecConfig, codecConfig264};
    aControl.mVideoRecvRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mReceiving = true;
    aControl.mRemoteSsrc = 2;
    aControl.mRemoteVideoRtxSsrc = 43;
  });

  EXPECT_THAT(Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types,
              UnorderedElementsAre(Pair(121, 120), Pair(97, 96)));
  EXPECT_EQ(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 2U);
  EXPECT_EQ(Call()->mVideoReceiveConfig->rtp.rtx_ssrc, 43U);

  // Verify, if there is no RTX PT, we will unset the SSRC.
  mControl.Update([&](auto& aControl) {
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    VideoCodecConfig codecConfig264(96, "H264", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoRecvCodecs = {codecConfig, codecConfig264};
    aControl.mVideoRecvRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    aControl.mReceiving = true;
    aControl.mRemoteSsrc = 2;
    aControl.mRemoteVideoRtxSsrc = 43;
  });

  EXPECT_EQ(
      Call()->mVideoReceiveConfig->rtp.rtx_associated_payload_types.size(), 0U);
  EXPECT_EQ(Call()->mVideoReceiveConfig->rtp.remote_ssrc, 2U);
  EXPECT_EQ(Call()->mVideoReceiveConfig->rtp.rtx_ssrc, 0U);
}

TEST_F(VideoConduitTest, TestNetworkStateFollowsTransportReadiness) {
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  NS_ProcessPendingEvents(nullptr);
  auto& signals = Call()->mNetworkStateSignals;
  EXPECT_TRUE(signals.IsEmpty());

  mVideoConduit->SetTransportActive(true);
  NS_ProcessPendingEvents(nullptr);
  EXPECT_THAT(Span(signals), ElementsAre(FieldsAre(webrtc::MediaType::VIDEO,
                                                   webrtc::kNetworkUp)));
  signals.Clear();

  mVideoConduit->SetTransportActive(false);
  NS_ProcessPendingEvents(nullptr);
  EXPECT_THAT(Span(signals), ElementsAre(FieldsAre(webrtc::MediaType::VIDEO,
                                                   webrtc::kNetworkDown)));
}

TEST_F(VideoConduitTest, TestNetworkStateForAudioAndVideo) {
  auto audioConduit = MakeRefPtr<WebrtcAudioConduit>(
      mCallWrapper, GetCurrentSerialEventTarget());
  mControl.Update([&](auto& aControl) {
    audioConduit->InitControl(&mControl);
    aControl.mTransmitting = true;
    aControl.mAudioSendCodec =
        Some(AudioCodecConfig(114, "opus", 48000, 2, false));
    VideoCodecConfig codecConfig(120, "VP8", VideoEncodingConstraints());
    codecConfig.mEncodings.emplace_back();
    aControl.mVideoSendCodec = Some(codecConfig);
    aControl.mVideoSendRtpRtcpConfig =
        Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
  });
  ASSERT_TRUE(Call()->mAudioSendConfig);
  ASSERT_TRUE(Call()->mVideoSendEncoderConfig);
  NS_ProcessPendingEvents(nullptr);
  auto& signals = Call()->mNetworkStateSignals;
  EXPECT_TRUE(signals.IsEmpty());

  mVideoConduit->SetTransportActive(true);
  audioConduit->SetTransportActive(true);
  NS_ProcessPendingEvents(nullptr);
  EXPECT_THAT(
      Span(signals),
      ElementsAre(FieldsAre(webrtc::MediaType::VIDEO, webrtc::kNetworkUp),
                  FieldsAre(webrtc::MediaType::AUDIO, webrtc::kNetworkUp)));
  signals.Clear();

  mVideoConduit->SetTransportActive(false);
  audioConduit->SetTransportActive(false);
  NS_ProcessPendingEvents(nullptr);
  EXPECT_THAT(
      Span(signals),
      ElementsAre(FieldsAre(webrtc::MediaType::VIDEO, webrtc::kNetworkDown),
                  FieldsAre(webrtc::MediaType::AUDIO, webrtc::kNetworkDown)));
  signals.Clear();

  audioConduit->SetTransportActive(true);
  mVideoConduit->SetTransportActive(true);
  NS_ProcessPendingEvents(nullptr);
  EXPECT_THAT(
      Span(signals),
      ElementsAre(FieldsAre(webrtc::MediaType::AUDIO, webrtc::kNetworkUp),
                  FieldsAre(webrtc::MediaType::VIDEO, webrtc::kNetworkUp)));
  signals.Clear();

  audioConduit->SetTransportActive(false);
  mVideoConduit->SetTransportActive(false);
  NS_ProcessPendingEvents(nullptr);
  EXPECT_THAT(
      Span(signals),
      ElementsAre(FieldsAre(webrtc::MediaType::AUDIO, webrtc::kNetworkDown),
                  FieldsAre(webrtc::MediaType::VIDEO, webrtc::kNetworkDown)));

  (void)WaitFor(audioConduit->Shutdown());
}

}  // End namespace test.
