/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebrtcMediaDataEncoderCodec.h"

#include <utility>

#include "AnnexB.h"
#include "api/video_codecs/h264_profile_level_id.h"
#include "ImageContainer.h"
#include "media/base/media_constants.h"
#include "MediaData.h"
#include "modules/video_coding/utility/vp8_header_parser.h"
#include "modules/video_coding/utility/vp9_uncompressed_header_parser.h"
#include "mozilla/Assertions.h"
#include "mozilla/Maybe.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/media/MediaUtils.h"
#include "mozilla/StaticPrefs_media.h"
#include "PEMFactory.h"
#include "VideoUtils.h"

namespace mozilla {

extern LazyLogModule sPEMLog;

#undef LOG
#define LOG(msg, ...)               \
  MOZ_LOG(sPEMLog, LogLevel::Debug, \
          ("WebrtcMediaDataEncoder=%p, " msg, this, ##__VA_ARGS__))

#undef LOG_V
#define LOG_V(msg, ...)               \
  MOZ_LOG(sPEMLog, LogLevel::Verbose, \
          ("WebrtcMediaDataEncoder=%p, " msg, this, ##__VA_ARGS__))

using namespace media;
using namespace layers;

CodecType ConvertWebrtcCodecTypeToCodecType(
    const webrtc::VideoCodecType& aType) {
  switch (aType) {
    case webrtc::VideoCodecType::kVideoCodecVP8:
      return CodecType::VP8;
    case webrtc::VideoCodecType::kVideoCodecVP9:
      return CodecType::VP9;
    case webrtc::VideoCodecType::kVideoCodecH264:
      return CodecType::H264;
    case webrtc::VideoCodecType::kVideoCodecAV1:
      return CodecType::AV1;
    case webrtc::VideoCodecType::kVideoCodecGeneric:
    case webrtc::VideoCodecType::kVideoCodecH265:
      return CodecType::Unknown;
  }
  MOZ_CRASH("Unsupported codec type");
  return CodecType::Unknown;
}

bool WebrtcMediaDataEncoder::CanCreate(
    const webrtc::VideoCodecType aCodecType) {
  auto factory = MakeRefPtr<PEMFactory>();
  CodecType type = ConvertWebrtcCodecTypeToCodecType(aCodecType);
  return factory->SupportsCodec(type);
}

static const char* PacketModeStr(const webrtc::CodecSpecificInfo& aInfo) {
  MOZ_ASSERT(aInfo.codecType != webrtc::VideoCodecType::kVideoCodecGeneric);

  if (aInfo.codecType != webrtc::VideoCodecType::kVideoCodecH264) {
    return "N/A";
  }
  switch (aInfo.codecSpecific.H264.packetization_mode) {
    case webrtc::H264PacketizationMode::SingleNalUnit:
      return "SingleNalUnit";
    case webrtc::H264PacketizationMode::NonInterleaved:
      return "NonInterleaved";
    default:
      return "Unknown";
  }
}

static std::pair<H264_PROFILE, H264_LEVEL> ConvertProfileLevel(
    const webrtc::CodecParameterMap& aParameters) {
  const std::optional<webrtc::H264ProfileLevelId> profileLevel =
      webrtc::ParseSdpForH264ProfileLevelId(aParameters);

  if (!profileLevel) {
    // TODO: Eveluate if there is a better default setting.
    return std::make_pair(H264_PROFILE::H264_PROFILE_MAIN,
                          H264_LEVEL::H264_LEVEL_3_1);
  }

  H264_PROFILE profile =
      (profileLevel->profile == webrtc::H264Profile::kProfileBaseline ||
       profileLevel->profile ==
           webrtc::H264Profile::kProfileConstrainedBaseline)
          ? H264_PROFILE::H264_PROFILE_BASE
          : H264_PROFILE::H264_PROFILE_MAIN;
  // H264Level::kLevel1_b cannot be mapped to H264_LEVEL::H264_LEVEL_1_b by
  // value directly since their values are different.
  H264_LEVEL level =
      profileLevel->level == webrtc::H264Level::kLevel1_b
          ? H264_LEVEL::H264_LEVEL_1_b
          : static_cast<H264_LEVEL>(static_cast<int>(profileLevel->level));

  return std::make_pair(profile, level);
}

static VPXComplexity MapComplexity(webrtc::VideoCodecComplexity aComplexity) {
  switch (aComplexity) {
    case webrtc::VideoCodecComplexity::kComplexityNormal:
      return VPXComplexity::Normal;
    case webrtc::VideoCodecComplexity::kComplexityHigh:
      return VPXComplexity::High;
    case webrtc::VideoCodecComplexity::kComplexityHigher:
      return VPXComplexity::Higher;
    case webrtc::VideoCodecComplexity::kComplexityMax:
      return VPXComplexity::Max;
    default:
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Bad complexity value");
  }
}

WebrtcMediaDataEncoder::WebrtcMediaDataEncoder(
    const webrtc::SdpVideoFormat& aFormat)
    : mTaskQueue(
          TaskQueue::Create(GetMediaThreadPool(MediaThreadType::SUPERVISOR),
                            "WebrtcMediaDataEncoder::mTaskQueue")),
      mFactory(new PEMFactory()),
      mCallbackMutex("WebrtcMediaDataEncoderCodec encoded callback mutex"),
      mFormatParams(aFormat.parameters),
      // Use the same lower and upper bound as h264_video_toolbox_encoder which
      // is an encoder from webrtc's upstream codebase.
      // 0.5 is set as a mininum to prevent overcompensating for large temporary
      // overshoots. We don't want to degrade video quality too badly.
      // 0.95 is set to prevent oscillations. When a lower bitrate is set on the
      // encoder than previously set, its output seems to have a brief period of
      // drastically reduced bitrate, so we want to avoid that. In steady state
      // conditions, 0.95 seems to give us better overall bitrate over long
      // periods of time.
      mBitrateAdjuster(0.5, 0.95) {
  PodZero(&mCodecSpecific.codecSpecific);
}

WebrtcMediaDataEncoder::~WebrtcMediaDataEncoder() {
  if (mEncoder) {
    Shutdown();
  }
}

static void InitCodecSpecficInfo(webrtc::CodecSpecificInfo& aInfo,
                                 const webrtc::VideoCodec* aCodecSettings,
                                 const webrtc::CodecParameterMap& aParameters) {
  MOZ_ASSERT(aCodecSettings);

  aInfo.codecType = aCodecSettings->codecType;
  switch (aCodecSettings->codecType) {
    case webrtc::VideoCodecType::kVideoCodecH264: {
      aInfo.codecSpecific.H264.packetization_mode =
          aParameters.count(cricket::kH264FmtpPacketizationMode) == 1 &&
                  aParameters.at(cricket::kH264FmtpPacketizationMode) == "1"
              ? webrtc::H264PacketizationMode::NonInterleaved
              : webrtc::H264PacketizationMode::SingleNalUnit;
      break;
    }
    case webrtc::VideoCodecType::kVideoCodecVP9: {
      MOZ_ASSERT(aCodecSettings->VP9().numberOfSpatialLayers == 1);
      aInfo.codecSpecific.VP9.flexible_mode =
          aCodecSettings->VP9().flexibleMode;
      aInfo.codecSpecific.VP9.first_frame_in_picture = true;
      break;
    }
    default:
      break;
  }
}

int32_t WebrtcMediaDataEncoder::InitEncode(
    const webrtc::VideoCodec* aCodecSettings,
    const webrtc::VideoEncoder::Settings& aSettings) {
  MOZ_ASSERT(aCodecSettings);

  if (aCodecSettings->numberOfSimulcastStreams > 1) {
    LOG("Only one stream is supported. Falling back to simulcast adaptor");
    return WEBRTC_VIDEO_CODEC_ERR_SIMULCAST_PARAMETERS_NOT_SUPPORTED;
  }

  // TODO: enable max output size setting when supported.
  if (aCodecSettings->codecType == webrtc::VideoCodecType::kVideoCodecH264 &&
      !(mFormatParams.count(cricket::kH264FmtpPacketizationMode) == 1 &&
        mFormatParams.at(cricket::kH264FmtpPacketizationMode) == "1")) {
    LOG("Some platform encoders don't support setting max output size."
        " Falling back to SW");
    return WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE;
  }

  if (mEncoder) {
    // Clean existing encoder.
    Shutdown();
  }

  RefPtr<MediaDataEncoder> encoder = CreateEncoder(aCodecSettings);
  if (!encoder) {
    LOG("Fail to create encoder. Falling back to SW");
    return WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE;
  }

  InitCodecSpecficInfo(mCodecSpecific, aCodecSettings, mFormatParams);
  LOG("Init encode, mimeType %s, mode %s", mInfo.mMimeType.get(),
      PacketModeStr(mCodecSpecific));
  if (!media::Await(do_AddRef(mTaskQueue), encoder->Init()).IsResolve()) {
    LOG("Fail to init encoder. Falling back to SW");
    return WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE;
  }
  mEncoder = std::move(encoder);
  return WEBRTC_VIDEO_CODEC_OK;
}

bool WebrtcMediaDataEncoder::SetupConfig(
    const webrtc::VideoCodec* aCodecSettings) {
  mMaxFrameRate = aCodecSettings->maxFramerate;
  // Those bitrates in codec setting are all kbps, so we have to covert them to
  // bps.
  mMaxBitrateBps = aCodecSettings->maxBitrate * 1000;
  mMinBitrateBps = aCodecSettings->minBitrate * 1000;
  mBitrateAdjuster.SetTargetBitrateBps(aCodecSettings->startBitrate * 1000);
  return true;
}

already_AddRefed<MediaDataEncoder> WebrtcMediaDataEncoder::CreateEncoder(
    const webrtc::VideoCodec* aCodecSettings) {
  if (!SetupConfig(aCodecSettings)) {
    return nullptr;
  }
  LOG("Request platform encoder for %s, bitRate=%u bps, frameRate=%u",
      mInfo.mMimeType.get(), mBitrateAdjuster.GetTargetBitrateBps(),
      aCodecSettings->maxFramerate);

  size_t keyframeInterval = 1;
  switch (aCodecSettings->codecType) {
    case webrtc::VideoCodecType::kVideoCodecH264: {
      keyframeInterval = aCodecSettings->H264().keyFrameInterval;
      break;
    }
    case webrtc::VideoCodecType::kVideoCodecVP8: {
      keyframeInterval = aCodecSettings->VP8().keyFrameInterval;
      break;
    }
    case webrtc::VideoCodecType::kVideoCodecVP9: {
      keyframeInterval = aCodecSettings->VP9().keyFrameInterval;
      break;
    }
    default:
      MOZ_ASSERT_UNREACHABLE("Unsupported codec type");
      return nullptr;
  }

  CodecType type;
  Maybe<EncoderConfig::CodecSpecific> specific;
  switch (aCodecSettings->codecType) {
    case webrtc::VideoCodecType::kVideoCodecH264: {
      type = CodecType::H264;
      std::pair<H264_PROFILE, H264_LEVEL> profileLevel =
          ConvertProfileLevel(mFormatParams);
      specific.emplace(H264Specific(profileLevel.first, profileLevel.second,
                                    H264BitStreamFormat::ANNEXB));
      break;
    }
    case webrtc::VideoCodecType::kVideoCodecVP8: {
      type = CodecType::VP8;
      const webrtc::VideoCodecVP8& vp8 = aCodecSettings->VP8();
      const webrtc::VideoCodecComplexity complexity =
          aCodecSettings->GetVideoEncoderComplexity();
      const bool frameDropEnabled = aCodecSettings->GetFrameDropEnabled();
      specific.emplace(VP8Specific(MapComplexity(complexity), false,
                                   vp8.numberOfTemporalLayers, vp8.denoisingOn,
                                   vp8.automaticResizeOn, frameDropEnabled));
      break;
    }
    case webrtc::VideoCodecType::kVideoCodecVP9: {
      type = CodecType::VP9;
      const webrtc::VideoCodecVP9& vp9 = aCodecSettings->VP9();
      const webrtc::VideoCodecComplexity complexity =
          aCodecSettings->GetVideoEncoderComplexity();
      const bool frameDropEnabled = aCodecSettings->GetFrameDropEnabled();
      specific.emplace(VP9Specific(
          MapComplexity(complexity), false, vp9.numberOfTemporalLayers,
          vp9.denoisingOn, vp9.automaticResizeOn, frameDropEnabled,
          vp9.adaptiveQpMode, vp9.numberOfSpatialLayers, vp9.flexibleMode));
      break;
    }
    default:
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Unsupported codec type");
  }
  EncoderConfig config(
      type, {aCodecSettings->width, aCodecSettings->height}, Usage::Realtime,
      EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
      aCodecSettings->maxFramerate, keyframeInterval,
      mBitrateAdjuster.GetTargetBitrateBps(), mMinBitrateBps, mMaxBitrateBps,
      BitrateMode::Variable, HardwarePreference::None, ScalabilityMode::None,
      specific);
  return mFactory->CreateEncoder(config, mTaskQueue);
}

WebrtcVideoEncoder::EncoderInfo WebrtcMediaDataEncoder::GetEncoderInfo() const {
  WebrtcVideoEncoder::EncoderInfo info;
  info.supports_native_handle = false;
  info.implementation_name = "MediaDataEncoder";
  info.is_hardware_accelerated = false;
  info.supports_simulcast = false;

#ifdef MOZ_WIDGET_ANDROID
  // Assume MediaDataEncoder is used mainly for hardware encoding. 16-alignment
  // seems required on Android. This could be improved by querying the
  // underlying encoder.
  info.requested_resolution_alignment = 16;
  info.apply_alignment_to_all_simulcast_layers = true;
#endif
  return info;
}

int32_t WebrtcMediaDataEncoder::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* aCallback) {
  MutexAutoLock lock(mCallbackMutex);
  mCallback = aCallback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t WebrtcMediaDataEncoder::Shutdown() {
  LOG("Release encoder");
  {
    MutexAutoLock lock(mCallbackMutex);
    mCallback = nullptr;
    mError = NS_OK;
  }
  if (mEncoder) {
    media::Await(do_AddRef(mTaskQueue), mEncoder->Shutdown());
    mEncoder = nullptr;
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

static already_AddRefed<VideoData> CreateVideoDataFromWebrtcVideoFrame(
    const webrtc::VideoFrame& aFrame, const bool aIsKeyFrame,
    const TimeUnit aDuration) {
  MOZ_ASSERT(aFrame.video_frame_buffer()->type() ==
                 webrtc::VideoFrameBuffer::Type::kI420,
             "Only support YUV420!");
  const webrtc::I420BufferInterface* i420 =
      aFrame.video_frame_buffer()->GetI420();

  PlanarYCbCrData yCbCrData;
  yCbCrData.mYChannel = const_cast<uint8_t*>(i420->DataY());
  yCbCrData.mYStride = i420->StrideY();
  yCbCrData.mCbChannel = const_cast<uint8_t*>(i420->DataU());
  yCbCrData.mCrChannel = const_cast<uint8_t*>(i420->DataV());
  MOZ_ASSERT(i420->StrideU() == i420->StrideV());
  yCbCrData.mCbCrStride = i420->StrideU();
  yCbCrData.mPictureRect = gfx::IntRect(0, 0, i420->width(), i420->height());
  yCbCrData.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;

  RefPtr<PlanarYCbCrImage> image =
      new RecyclingPlanarYCbCrImage(new BufferRecycleBin());
  image->CopyData(yCbCrData);

  // Although webrtc::VideoFrame::timestamp_rtp_ will likely be deprecated,
  // webrtc::EncodedImage and the VPx encoders still use it in the imported
  // version of libwebrtc. Not using the same timestamp values generates
  // discontinuous time and confuses the video receiver when switching from
  // platform to libwebrtc encoder.
  TimeUnit timestamp =
      media::TimeUnit(aFrame.rtp_timestamp(), cricket::kVideoCodecClockrate);
  return VideoData::CreateFromImage(image->GetSize(), 0, timestamp, aDuration,
                                    image, aIsKeyFrame, timestamp);
}

static void UpdateCodecSpecificInfo(webrtc::CodecSpecificInfo& aInfo,
                                    const gfx::IntSize& aSize,
                                    const bool aIsKeyframe) {
  switch (aInfo.codecType) {
    case webrtc::VideoCodecType::kVideoCodecVP8: {
      // See webrtc::VP8EncoderImpl::PopulateCodecSpecific().
      webrtc::CodecSpecificInfoVP8& vp8 = aInfo.codecSpecific.VP8;
      vp8.keyIdx = webrtc::kNoKeyIdx;
      // Cannot be 100% sure unless parsing significant portion of the
      // bitstream. Treat all frames as referenced just to be safe.
      vp8.nonReference = false;
      // One temporal layer only.
      vp8.temporalIdx = webrtc::kNoTemporalIdx;
      vp8.layerSync = false;
      break;
    }
    case webrtc::VideoCodecType::kVideoCodecVP9: {
      // See webrtc::VP9EncoderImpl::PopulateCodecSpecific().
      webrtc::CodecSpecificInfoVP9& vp9 = aInfo.codecSpecific.VP9;
      vp9.inter_pic_predicted = !aIsKeyframe;
      vp9.ss_data_available = aIsKeyframe && !vp9.flexible_mode;
      // One temporal & spatial layer only.
      vp9.temporal_idx = webrtc::kNoTemporalIdx;
      vp9.temporal_up_switch = false;
      vp9.num_spatial_layers = 1;
      aInfo.end_of_picture = true;
      vp9.gof_idx = webrtc::kNoGofIdx;
      vp9.width[0] = aSize.width;
      vp9.height[0] = aSize.height;
      break;
    }
    default:
      break;
  }
}

static void GetVPXQp(const webrtc::VideoCodecType aType,
                     webrtc::EncodedImage& aImage) {
  switch (aType) {
    case webrtc::VideoCodecType::kVideoCodecVP8:
      webrtc::vp8::GetQp(aImage.data(), aImage.size(), &(aImage.qp_));
      break;
    case webrtc::VideoCodecType::kVideoCodecVP9:
      webrtc::vp9::GetQp(aImage.data(), aImage.size(), &(aImage.qp_));
      break;
    default:
      break;
  }
}

int32_t WebrtcMediaDataEncoder::Encode(
    const webrtc::VideoFrame& aInputFrame,
    const std::vector<webrtc::VideoFrameType>* aFrameTypes) {
  if (!aInputFrame.size() || !aInputFrame.video_frame_buffer() ||
      aFrameTypes->empty()) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }

  if (!mEncoder) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }
  {
    MutexAutoLock lock(mCallbackMutex);
    if (!mCallback) {
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    if (NS_FAILED(mError)) {
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
  }

  LOG_V("Encode frame, type %d size %u", static_cast<int>((*aFrameTypes)[0]),
        aInputFrame.size());
  MOZ_ASSERT(aInputFrame.video_frame_buffer()->type() ==
             webrtc::VideoFrameBuffer::Type::kI420);
  RefPtr<VideoData> data = CreateVideoDataFromWebrtcVideoFrame(
      aInputFrame, (*aFrameTypes)[0] == webrtc::VideoFrameType::kVideoFrameKey,
      TimeUnit::FromSeconds(1.0 / mMaxFrameRate));
  const gfx::IntSize displaySize = data->mDisplay;

  mEncoder->Encode(data)->Then(
      mTaskQueue, __func__,
      [self = RefPtr<WebrtcMediaDataEncoder>(this), this,
       displaySize](MediaDataEncoder::EncodedData aFrames) {
        LOG_V("Received encoded frame, nums %zu width %d height %d",
              aFrames.Length(), displaySize.width, displaySize.height);
        for (auto& frame : aFrames) {
          MutexAutoLock lock(mCallbackMutex);
          if (!mCallback) {
            break;
          }
          webrtc::EncodedImage image;
          image.SetEncodedData(
              webrtc::EncodedImageBuffer::Create(frame->Data(), frame->Size()));
          image._encodedWidth = displaySize.width;
          image._encodedHeight = displaySize.height;
          CheckedInt64 time =
              TimeUnitToFrames(frame->mTime, cricket::kVideoCodecClockrate);
          if (!time.isValid()) {
            self->mError = MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                       "invalid timestamp from encoder");
            break;
          }
          image.SetRtpTimestamp(time.value());
          image._frameType = frame->mKeyframe
                                 ? webrtc::VideoFrameType::kVideoFrameKey
                                 : webrtc::VideoFrameType::kVideoFrameDelta;
          GetVPXQp(mCodecSpecific.codecType, image);
          UpdateCodecSpecificInfo(mCodecSpecific, displaySize,
                                  frame->mKeyframe);

          LOG_V("Send encoded image");
          self->mCallback->OnEncodedImage(image, &mCodecSpecific);
          self->mBitrateAdjuster.Update(image.size());
        }
      },
      [self = RefPtr<WebrtcMediaDataEncoder>(this)](const MediaResult& aError) {
        self->mError = aError;
      });
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t WebrtcMediaDataEncoder::SetRates(
    const webrtc::VideoEncoder::RateControlParameters& aParameters) {
  if (!aParameters.bitrate.HasBitrate(0, 0)) {
    LOG("%s: no bitrate value to set.", __func__);
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  MOZ_ASSERT(aParameters.bitrate.IsSpatialLayerUsed(0));
  MOZ_ASSERT(!aParameters.bitrate.IsSpatialLayerUsed(1),
             "No simulcast support for platform encoder");

  const uint32_t newBitrateBps = aParameters.bitrate.GetBitrate(0, 0);
  if (newBitrateBps < mMinBitrateBps || newBitrateBps > mMaxBitrateBps) {
    LOG("%s: bitrate value out of range.", __func__);
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }

  // We have already been in this bitrate.
  if (mBitrateAdjuster.GetAdjustedBitrateBps() == newBitrateBps) {
    return WEBRTC_VIDEO_CODEC_OK;
  }

  if (!mEncoder) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }
  {
    MutexAutoLock lock(mCallbackMutex);
    if (NS_FAILED(mError)) {
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
  }
  mBitrateAdjuster.SetTargetBitrateBps(newBitrateBps);
  LOG("Set bitrate %u bps, minBitrate %u bps, maxBitrate %u bps", newBitrateBps,
      mMinBitrateBps, mMaxBitrateBps);
  auto rv =
      media::Await(do_AddRef(mTaskQueue), mEncoder->SetBitrate(newBitrateBps));
  return rv.IsResolve() ? WEBRTC_VIDEO_CODEC_OK : WEBRTC_VIDEO_CODEC_ERROR;
}

}  // namespace mozilla
