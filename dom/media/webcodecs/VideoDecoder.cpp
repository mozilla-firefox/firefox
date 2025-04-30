/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/VideoDecoder.h"
#include "mozilla/dom/VideoDecoderBinding.h"

#include "DecoderTraits.h"
#include "GPUVideoImage.h"
#include "H264.h"
#include "ImageContainer.h"
#include "MediaContainerType.h"
#include "MediaData.h"
#include "VideoUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/Try.h"
#include "mozilla/Unused.h"
#include "mozilla/dom/EncodedVideoChunk.h"
#include "mozilla/dom/EncodedVideoChunkBinding.h"
#include "mozilla/dom/ImageUtils.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/VideoColorSpaceBinding.h"
#include "mozilla/dom/VideoFrameBinding.h"
#include "mozilla/dom/WebCodecsUtils.h"
#include "nsPrintfCString.h"
#include "nsReadableUtils.h"

#ifdef XP_MACOSX
#  include "MacIOSurfaceImage.h"
#elif MOZ_WAYLAND
#  include "mozilla/layers/DMABUFSurfaceImage.h"
#  include "mozilla/widget/DMABufSurface.h"
#endif

extern mozilla::LazyLogModule gWebCodecsLog;

namespace mozilla::dom {

#ifdef LOG_INTERNAL
#  undef LOG_INTERNAL
#endif  // LOG_INTERNAL
#define LOG_INTERNAL(level, msg, ...) \
  MOZ_LOG(gWebCodecsLog, LogLevel::level, (msg, ##__VA_ARGS__))

#ifdef LOG
#  undef LOG
#endif  // LOG
#define LOG(msg, ...) LOG_INTERNAL(Debug, msg, ##__VA_ARGS__)

#ifdef LOGW
#  undef LOGW
#endif  // LOGW
#define LOGW(msg, ...) LOG_INTERNAL(Warning, msg, ##__VA_ARGS__)

#ifdef LOGE
#  undef LOGE
#endif  // LOGE
#define LOGE(msg, ...) LOG_INTERNAL(Error, msg, ##__VA_ARGS__)

#ifdef LOGV
#  undef LOGV
#endif  // LOGV
#define LOGV(msg, ...) LOG_INTERNAL(Verbose, msg, ##__VA_ARGS__)

NS_IMPL_CYCLE_COLLECTION_INHERITED(VideoDecoder, DOMEventTargetHelper,
                                   mErrorCallback, mOutputCallback)
NS_IMPL_ADDREF_INHERITED(VideoDecoder, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(VideoDecoder, DOMEventTargetHelper)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(VideoDecoder)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

/*
 * Below are helper classes
 */

VideoDecoderConfigInternal::VideoDecoderConfigInternal(
    const nsAString& aCodec, Maybe<uint32_t>&& aCodedHeight,
    Maybe<uint32_t>&& aCodedWidth, Maybe<VideoColorSpaceInternal>&& aColorSpace,
    already_AddRefed<MediaByteBuffer> aDescription,
    Maybe<uint32_t>&& aDisplayAspectHeight,
    Maybe<uint32_t>&& aDisplayAspectWidth,
    const HardwareAcceleration& aHardwareAcceleration,
    Maybe<bool>&& aOptimizeForLatency)
    : mCodec(aCodec),
      mCodedHeight(std::move(aCodedHeight)),
      mCodedWidth(std::move(aCodedWidth)),
      mColorSpace(std::move(aColorSpace)),
      mDescription(aDescription),
      mDisplayAspectHeight(std::move(aDisplayAspectHeight)),
      mDisplayAspectWidth(std::move(aDisplayAspectWidth)),
      mHardwareAcceleration(aHardwareAcceleration),
      mOptimizeForLatency(std::move(aOptimizeForLatency)) {};

/*static*/
RefPtr<VideoDecoderConfigInternal> VideoDecoderConfigInternal::Create(
    const VideoDecoderConfig& aConfig) {
  nsCString errorMessage;
  if (!VideoDecoderTraits::Validate(aConfig, errorMessage)) {
    LOGE("Failed to create VideoDecoderConfigInternal: %s", errorMessage.get());
    return nullptr;
  }

  RefPtr<MediaByteBuffer> description;
  if (aConfig.mDescription.WasPassed()) {
    auto rv = GetExtraDataFromArrayBuffer(aConfig.mDescription.Value());
    if (rv.isErr()) {  // Invalid description data.
      LOGE(
          "Failed to create VideoDecoderConfigInternal due to invalid "
          "description data. Error: 0x%08" PRIx32,
          static_cast<uint32_t>(rv.unwrapErr()));
      return nullptr;
    }
    description = rv.unwrap();
  }

  return MakeRefPtr<VideoDecoderConfigInternal>(
      aConfig.mCodec, OptionalToMaybe(aConfig.mCodedHeight),
      OptionalToMaybe(aConfig.mCodedWidth),
      OptionalToMaybe(aConfig.mColorSpace), description.forget(),
      OptionalToMaybe(aConfig.mDisplayAspectHeight),
      OptionalToMaybe(aConfig.mDisplayAspectWidth),
      aConfig.mHardwareAcceleration,
      OptionalToMaybe(aConfig.mOptimizeForLatency));
}

nsCString VideoDecoderConfigInternal::ToString() const {
  nsCString rv;

  rv.Append(NS_ConvertUTF16toUTF8(mCodec));
  if (mCodedWidth.isSome()) {
    rv.AppendPrintf("coded: %dx%d", mCodedWidth.value(), mCodedHeight.value());
  }
  if (mDisplayAspectWidth.isSome()) {
    rv.AppendPrintf("display %dx%d", mDisplayAspectWidth.value(),
                    mDisplayAspectHeight.value());
  }
  if (mColorSpace.isSome()) {
    rv.AppendPrintf("colorspace %s", "todo");
  }
  if (mDescription) {
    rv.AppendPrintf("extradata: %zu bytes", mDescription->Length());
  }
  rv.AppendPrintf("hw accel: %s", GetEnumString(mHardwareAcceleration).get());
  if (mOptimizeForLatency.isSome()) {
    rv.AppendPrintf("optimize for latency: %s",
                    mOptimizeForLatency.value() ? "true" : "false");
  }

  return rv;
}

/*
 * The followings are helpers for VideoDecoder methods
 */

struct MIMECreateParam {
  explicit MIMECreateParam(const VideoDecoderConfigInternal& aConfig)
      : mCodec(aConfig.mCodec),
        mWidth(aConfig.mCodedWidth),
        mHeight(aConfig.mCodedHeight) {}
  explicit MIMECreateParam(const VideoDecoderConfig& aConfig)
      : mCodec(aConfig.mCodec),
        mWidth(OptionalToMaybe(aConfig.mCodedWidth)),
        mHeight(OptionalToMaybe(aConfig.mCodedHeight)) {}

  const nsString mCodec;
  const Maybe<uint32_t> mWidth;
  const Maybe<uint32_t> mHeight;
};

static nsTArray<nsCString> GuessMIMETypes(const MIMECreateParam& aParam) {
  const auto codec = NS_ConvertUTF16toUTF8(aParam.mCodec);
  nsTArray<nsCString> types;
  for (const nsCString& container : GuessContainers(aParam.mCodec)) {
    nsPrintfCString mime("video/%s; codecs=%s", container.get(), codec.get());
    if (aParam.mWidth) {
      mime.AppendPrintf("; width=%d", *aParam.mWidth);
    }
    if (aParam.mHeight) {
      mime.AppendPrintf("; height=%d", *aParam.mHeight);
    }
    types.AppendElement(mime);
  }
  return types;
}

// https://w3c.github.io/webcodecs/#check-configuration-support
template <typename Config>
static bool CanDecode(const Config& aConfig) {
  // TODO: Enable WebCodecs on Android (Bug 1840508)
  if (IsOnAndroid()) {
    return false;
  }
  if (!IsSupportedVideoCodec(aConfig.mCodec)) {
    return false;
  }

  // TODO (1880326): code below is wrongly using the logic of HTMLMediaElement
  // for determining if a codec can be played, and incorrect codec string for
  // h264 are accepted for HTMLMediaElement for compat reasons. Perform stricter
  // check here until we fix it for real.
  if (IsH264CodecString(aConfig.mCodec)) {
    uint8_t profile, constraint;
    H264_LEVEL level;
    bool supported =
        ExtractH264CodecDetails(aConfig.mCodec, profile, constraint, level,
                                H264CodecStringStrictness::Strict);
    if (!supported) {
      return false;
    }
  }

  // TODO (1880326): Instead of calling CanHandleContainerType with the guessed
  // the containers, DecoderTraits should provide an API to tell if a codec is
  // decodable or not.
  for (const nsCString& mime : GuessMIMETypes(MIMECreateParam(aConfig))) {
    if (Maybe<MediaContainerType> containerType =
            MakeMediaExtendedMIMEType(mime)) {
      if (DecoderTraits::CanHandleContainerType(
              *containerType, nullptr /* DecoderDoctorDiagnostics */) !=
          CANPLAY_NO) {
        return true;
      }
    }
  }
  return false;
}

static nsTArray<UniquePtr<TrackInfo>> GetTracksInfo(
    const VideoDecoderConfigInternal& aConfig) {
  // TODO: Instead of calling GetTracksInfo with the guessed containers,
  // DecoderTraits should provide an API to create the TrackInfo directly.
  for (const nsCString& mime : GuessMIMETypes(MIMECreateParam(aConfig))) {
    if (Maybe<MediaContainerType> containerType =
            MakeMediaExtendedMIMEType(mime)) {
      if (nsTArray<UniquePtr<TrackInfo>> tracks =
              DecoderTraits::GetTracksInfo(*containerType);
          !tracks.IsEmpty()) {
        return tracks;
      }
    }
  }
  return {};
}

static Result<Ok, nsresult> CloneConfiguration(
    RootedDictionary<VideoDecoderConfig>& aDest, JSContext* aCx,
    const VideoDecoderConfig& aConfig, ErrorResult& aRv) {
  DebugOnly<nsCString> str;
  MOZ_ASSERT(VideoDecoderTraits::Validate(aConfig, str));

  aDest.mCodec = aConfig.mCodec;
  if (aConfig.mCodedHeight.WasPassed()) {
    aDest.mCodedHeight.Construct(aConfig.mCodedHeight.Value());
  }
  if (aConfig.mCodedWidth.WasPassed()) {
    aDest.mCodedWidth.Construct(aConfig.mCodedWidth.Value());
  }
  if (aConfig.mColorSpace.WasPassed()) {
    aDest.mColorSpace.Construct(aConfig.mColorSpace.Value());
  }
  if (aConfig.mDescription.WasPassed()) {
    aDest.mDescription.Construct();
    MOZ_TRY(CloneBuffer(aCx, aDest.mDescription.Value(),
                        aConfig.mDescription.Value(), aRv));
  }
  if (aConfig.mDisplayAspectHeight.WasPassed()) {
    aDest.mDisplayAspectHeight.Construct(aConfig.mDisplayAspectHeight.Value());
  }
  if (aConfig.mDisplayAspectWidth.WasPassed()) {
    aDest.mDisplayAspectWidth.Construct(aConfig.mDisplayAspectWidth.Value());
  }
  aDest.mHardwareAcceleration = aConfig.mHardwareAcceleration;
  if (aConfig.mOptimizeForLatency.WasPassed()) {
    aDest.mOptimizeForLatency.Construct(aConfig.mOptimizeForLatency.Value());
  }

  return Ok();
}

static Maybe<VideoPixelFormat> GuessPixelFormat(layers::Image* aImage) {
  if (aImage) {
    // TODO: Implement ImageUtils::Impl for MacIOSurfaceImage and
    // DMABUFSurfaceImage?
    if (aImage->AsPlanarYCbCrImage() || aImage->AsNVImage()) {
      const ImageUtils imageUtils(aImage);
      Maybe<dom::ImageBitmapFormat> format = imageUtils.GetFormat();
      Maybe<VideoPixelFormat> f =
          format.isSome() ? ImageBitmapFormatToVideoPixelFormat(format.value())
                          : Nothing();

      // ImageBitmapFormat cannot distinguish YUV420 or YUV420A.
      bool hasAlpha = aImage->AsPlanarYCbCrImage() &&
                      aImage->AsPlanarYCbCrImage()->GetData() &&
                      aImage->AsPlanarYCbCrImage()->GetData()->mAlpha;
      if (f && *f == VideoPixelFormat::I420 && hasAlpha) {
        return Some(VideoPixelFormat::I420A);
      }
      return f;
    }
    if (layers::GPUVideoImage* image = aImage->AsGPUVideoImage()) {
      RefPtr<layers::ImageBridgeChild> imageBridge =
          layers::ImageBridgeChild::GetSingleton();
      layers::TextureClient* texture = image->GetTextureClient(imageBridge);
      if (NS_WARN_IF(!texture)) {
        return Nothing();
      }
      return SurfaceFormatToVideoPixelFormat(texture->GetFormat());
    }
#ifdef XP_MACOSX
    if (layers::MacIOSurfaceImage* image = aImage->AsMacIOSurfaceImage()) {
      MOZ_ASSERT(image->GetSurface());
      return SurfaceFormatToVideoPixelFormat(image->GetSurface()->GetFormat());
    }
#endif
#ifdef MOZ_WAYLAND
    if (layers::DMABUFSurfaceImage* image = aImage->AsDMABUFSurfaceImage()) {
      MOZ_ASSERT(image->GetSurface());
      return SurfaceFormatToVideoPixelFormat(image->GetSurface()->GetFormat());
    }
#endif
  }
  LOGW("Failed to get pixel format from layers::Image");
  return Nothing();
}

static VideoColorSpaceInternal GuessColorSpace(
    const layers::PlanarYCbCrData* aData) {
  if (!aData) {
    LOGE("nullptr in GuessColorSpace");
    return {};
  }

  VideoColorSpaceInternal colorSpace;
  colorSpace.mFullRange = Some(ToFullRange(aData->mColorRange));
  if (Maybe<VideoMatrixCoefficients> m =
          ToMatrixCoefficients(aData->mYUVColorSpace)) {
    colorSpace.mMatrix = ToMatrixCoefficients(aData->mYUVColorSpace);
    colorSpace.mPrimaries = ToPrimaries(aData->mColorPrimaries);
  }
  if (!colorSpace.mPrimaries) {
    LOG("Missing primaries, guessing from colorspace");
    // Make an educated guess based on the coefficients.
    colorSpace.mPrimaries = colorSpace.mMatrix.map([](const auto& aMatrix) {
      switch (aMatrix) {
        case VideoMatrixCoefficients::Bt2020_ncl:
          return VideoColorPrimaries::Bt2020;
        case VideoMatrixCoefficients::Rgb:
        case VideoMatrixCoefficients::Bt470bg:
        case VideoMatrixCoefficients::Smpte170m:
          LOGW(
              "Warning: Falling back to BT709 when attempting to determine the "
              "primaries function of a YCbCr buffer");
          [[fallthrough]];
        case VideoMatrixCoefficients::Bt709:
          return VideoColorPrimaries::Bt709;
      }
      MOZ_ASSERT_UNREACHABLE("Unexpected matrix coefficients");
      LOGW(
          "Warning: Falling back to BT709 due to unexpected matrix "
          "coefficients "
          "when attempting to determine the primaries function of a YCbCr "
          "buffer");
      return VideoColorPrimaries::Bt709;
    });
  }

  if (Maybe<VideoTransferCharacteristics> c =
          ToTransferCharacteristics(aData->mTransferFunction)) {
    colorSpace.mTransfer = Some(*c);
  }
  if (!colorSpace.mTransfer) {
    LOG("Missing transfer characteristics, guessing from colorspace");
    colorSpace.mTransfer = Some(([&] {
      switch (aData->mYUVColorSpace) {
        case gfx::YUVColorSpace::Identity:
          return VideoTransferCharacteristics::Iec61966_2_1;
        case gfx::YUVColorSpace::BT2020:
          return VideoTransferCharacteristics::Pq;
        case gfx::YUVColorSpace::BT601:
          LOGW(
              "Warning: Falling back to BT709 when attempting to determine the "
              "transfer function of a MacIOSurface");
          [[fallthrough]];
        case gfx::YUVColorSpace::BT709:
          return VideoTransferCharacteristics::Bt709;
      }
      MOZ_ASSERT_UNREACHABLE("Unexpected color space");
      LOGW(
          "Warning: Falling back to BT709 due to unexpected color space "
          "when attempting to determine the transfer function of a "
          "MacIOSurface");
      return VideoTransferCharacteristics::Bt709;
    })());
  }

  return colorSpace;
}

#ifdef XP_MACOSX
static VideoColorSpaceInternal GuessColorSpace(const MacIOSurface* aSurface) {
  if (!aSurface) {
    return {};
  }
  VideoColorSpaceInternal colorSpace;
  colorSpace.mFullRange = Some(aSurface->IsFullRange());
  if (Maybe<dom::VideoMatrixCoefficients> m =
          ToMatrixCoefficients(aSurface->GetYUVColorSpace())) {
    colorSpace.mMatrix = Some(*m);
  }
  if (Maybe<VideoColorPrimaries> p = ToPrimaries(aSurface->mColorPrimaries)) {
    colorSpace.mPrimaries = Some(*p);
  }
  // Make an educated guess based on the coefficients.
  if (aSurface->GetYUVColorSpace() == gfx::YUVColorSpace::Identity) {
    colorSpace.mTransfer = Some(VideoTransferCharacteristics::Iec61966_2_1);
  } else if (aSurface->GetYUVColorSpace() == gfx::YUVColorSpace::BT709) {
    colorSpace.mTransfer = Some(VideoTransferCharacteristics::Bt709);
  } else if (aSurface->GetYUVColorSpace() == gfx::YUVColorSpace::BT2020) {
    colorSpace.mTransfer = Some(VideoTransferCharacteristics::Pq);
  } else {
    LOGW(
        "Warning: Falling back to BT709 when attempting to determine the "
        "transfer function of a MacIOSurface");
    colorSpace.mTransfer = Some(VideoTransferCharacteristics::Bt709);
  }

  return colorSpace;
}
#endif
#ifdef MOZ_WAYLAND
// TODO: Set DMABufSurface::IsFullRange() to const so aSurface can be const.
static VideoColorSpaceInternal GuessColorSpace(DMABufSurface* aSurface) {
  if (!aSurface) {
    return {};
  }
  VideoColorSpaceInternal colorSpace;
  colorSpace.mFullRange = Some(aSurface->IsFullRange());
  if (Maybe<dom::VideoMatrixCoefficients> m =
          ToMatrixCoefficients(aSurface->GetYUVColorSpace())) {
    colorSpace.mMatrix = Some(*m);
  }
  // No other color space information.
  return colorSpace;
}
#endif

static VideoColorSpaceInternal GuessColorSpace(layers::Image* aImage) {
  if (aImage) {
    if (layers::PlanarYCbCrImage* image = aImage->AsPlanarYCbCrImage()) {
      return GuessColorSpace(image->GetData());
    }
    if (layers::NVImage* image = aImage->AsNVImage()) {
      return GuessColorSpace(image->GetData());
    }
    if (layers::GPUVideoImage* image = aImage->AsGPUVideoImage()) {
      VideoColorSpaceInternal colorSpace;
      colorSpace.mFullRange =
          Some(image->GetColorRange() != gfx::ColorRange::LIMITED);
      colorSpace.mMatrix = ToMatrixCoefficients(image->GetYUVColorSpace());
      colorSpace.mPrimaries = ToPrimaries(image->GetColorPrimaries());
      colorSpace.mTransfer =
          ToTransferCharacteristics(image->GetTransferFunction());
      // In some circumstances, e.g. on Linux software decoding when using
      // VPXDecoder and RDD, the primaries aren't set correctly. Make a good
      // guess based on the other params. Fixing this is tracked in
      // https://bugzilla.mozilla.org/show_bug.cgi?id=1869825
      if (!colorSpace.mPrimaries) {
        if (colorSpace.mMatrix.isSome()) {
          switch (colorSpace.mMatrix.value()) {
            case VideoMatrixCoefficients::Rgb:
            case VideoMatrixCoefficients::Bt709:
              colorSpace.mPrimaries = Some(VideoColorPrimaries::Bt709);
              break;
            case VideoMatrixCoefficients::Bt470bg:
            case VideoMatrixCoefficients::Smpte170m:
              colorSpace.mPrimaries = Some(VideoColorPrimaries::Bt470bg);
              break;
            case VideoMatrixCoefficients::Bt2020_ncl:
              colorSpace.mPrimaries = Some(VideoColorPrimaries::Bt2020);
              break;
          };
        }
      }
      return colorSpace;
    }
#ifdef XP_MACOSX
    // TODO: Make sure VideoFrame can interpret its internal data in different
    // formats.
    if (layers::MacIOSurfaceImage* image = aImage->AsMacIOSurfaceImage()) {
      return GuessColorSpace(image->GetSurface());
    }
#endif
#ifdef MOZ_WAYLAND
    // TODO: Make sure VideoFrame can interpret its internal data in different
    // formats.
    if (layers::DMABUFSurfaceImage* image = aImage->AsDMABUFSurfaceImage()) {
      return GuessColorSpace(image->GetSurface());
    }
#endif
  }
  LOGW("Failed to get color space from layers::Image");
  return {};
}

static Result<gfx::IntSize, nsresult> AdjustDisplaySize(
    const uint32_t aDisplayAspectWidth, const uint32_t aDisplayAspectHeight,
    const gfx::IntSize& aDisplaySize) {
  if (aDisplayAspectHeight == 0) {
    return Err(NS_ERROR_ILLEGAL_VALUE);
  }

  const double aspectRatio =
      static_cast<double>(aDisplayAspectWidth) / aDisplayAspectHeight;

  double w = aDisplaySize.width;
  double h = aDisplaySize.height;

  if (aspectRatio >= w / h) {
    // Adjust w to match the aspect ratio
    w = aspectRatio * h;
  } else {
    // Adjust h to match the aspect ratio
    h = w / aspectRatio;
  }

  w = std::round(w);
  h = std::round(h);
  constexpr double MAX = static_cast<double>(
      std::numeric_limits<decltype(gfx::IntSize::width)>::max());
  if (w > MAX || h > MAX || w < 1.0 || h < 1.0) {
    return Err(NS_ERROR_ILLEGAL_VALUE);
  }
  return gfx::IntSize(static_cast<decltype(gfx::IntSize::width)>(w),
                      static_cast<decltype(gfx::IntSize::height)>(h));
}

// https://w3c.github.io/webcodecs/#create-a-videoframe
static RefPtr<VideoFrame> CreateVideoFrame(
    nsIGlobalObject* aGlobalObject, const VideoData* aData, int64_t aTimestamp,
    uint64_t aDuration, const Maybe<uint32_t> aDisplayAspectWidth,
    const Maybe<uint32_t> aDisplayAspectHeight,
    const VideoColorSpaceInternal& aColorSpace) {
  MOZ_ASSERT(aGlobalObject);
  MOZ_ASSERT(aData);
  MOZ_ASSERT((!!aDisplayAspectWidth) == (!!aDisplayAspectHeight));

  Maybe<VideoPixelFormat> format = GuessPixelFormat(aData->mImage.get());
  gfx::IntSize displaySize = aData->mDisplay;
  if (aDisplayAspectWidth && aDisplayAspectHeight) {
    auto r = AdjustDisplaySize(*aDisplayAspectWidth, *aDisplayAspectHeight,
                               displaySize);
    if (r.isOk()) {
      displaySize = r.unwrap();
    }
  }

  return MakeRefPtr<VideoFrame>(aGlobalObject, aData->mImage, format,
                                aData->mImage->GetSize(),
                                aData->mImage->GetPictureRect(), displaySize,
                                Some(aDuration), aTimestamp, aColorSpace);
}

/* static */
bool VideoDecoderTraits::IsSupported(
    const VideoDecoderConfigInternal& aConfig) {
  return CanDecode(aConfig);
}

/* static */
Result<UniquePtr<TrackInfo>, nsresult> VideoDecoderTraits::CreateTrackInfo(
    const VideoDecoderConfigInternal& aConfig) {
  LOG("Create a VideoInfo from %s config", aConfig.ToString().get());

  nsTArray<UniquePtr<TrackInfo>> tracks = GetTracksInfo(aConfig);
  if (tracks.Length() != 1 || tracks[0]->GetType() != TrackInfo::kVideoTrack) {
    LOGE("Failed to get TrackInfo");
    return Err(NS_ERROR_INVALID_ARG);
  }

  UniquePtr<TrackInfo> track(std::move(tracks[0]));
  VideoInfo* vi = track->GetAsVideoInfo();
  if (!vi) {
    LOGE("Failed to get VideoInfo");
    return Err(NS_ERROR_INVALID_ARG);
  }

  constexpr uint32_t MAX = static_cast<uint32_t>(
      std::numeric_limits<decltype(gfx::IntSize::width)>::max());
  if (aConfig.mCodedHeight.isSome()) {
    if (aConfig.mCodedHeight.value() > MAX) {
      LOGE("codedHeight overflows");
      return Err(NS_ERROR_INVALID_ARG);
    }
    vi->mImage.height = static_cast<decltype(gfx::IntSize::height)>(
        aConfig.mCodedHeight.value());
  }
  if (aConfig.mCodedWidth.isSome()) {
    if (aConfig.mCodedWidth.value() > MAX) {
      LOGE("codedWidth overflows");
      return Err(NS_ERROR_INVALID_ARG);
    }
    vi->mImage.width =
        static_cast<decltype(gfx::IntSize::width)>(aConfig.mCodedWidth.value());
  }

  if (aConfig.mDisplayAspectHeight.isSome()) {
    if (aConfig.mDisplayAspectHeight.value() > MAX) {
      LOGE("displayAspectHeight overflows");
      return Err(NS_ERROR_INVALID_ARG);
    }
    vi->mDisplay.height = static_cast<decltype(gfx::IntSize::height)>(
        aConfig.mDisplayAspectHeight.value());
  }
  if (aConfig.mDisplayAspectWidth.isSome()) {
    if (aConfig.mDisplayAspectWidth.value() > MAX) {
      LOGE("displayAspectWidth overflows");
      return Err(NS_ERROR_INVALID_ARG);
    }
    vi->mDisplay.width = static_cast<decltype(gfx::IntSize::width)>(
        aConfig.mDisplayAspectWidth.value());
  }

  if (aConfig.mColorSpace.isSome()) {
    const VideoColorSpaceInternal& colorSpace(aConfig.mColorSpace.value());
    if (colorSpace.mFullRange.isSome()) {
      vi->mColorRange = ToColorRange(colorSpace.mFullRange.value());
    }
    if (colorSpace.mMatrix.isSome()) {
      vi->mColorSpace.emplace(ToColorSpace(colorSpace.mMatrix.value()));
    }
    // Some decoders get their primaries and transfer function from the codec
    // string, and it's already set here. This is the case for VP9 decoders.
    if (colorSpace.mPrimaries.isSome()) {
      auto primaries = ToPrimaries(colorSpace.mPrimaries.value());
      if (vi->mColorPrimaries.isSome()) {
        if (vi->mColorPrimaries.value() != primaries) {
          LOG("Conflict between decoder config and codec string, keeping codec "
              "string primaries of %d",
              static_cast<int>(primaries));
        }
      } else {
        vi->mColorPrimaries.emplace(primaries);
      }
    }
    if (colorSpace.mTransfer.isSome()) {
      auto primaries = ToTransferFunction(colorSpace.mTransfer.value());
      if (vi->mTransferFunction.isSome()) {
        if (vi->mTransferFunction.value() != primaries) {
          LOG("Conflict between decoder config and codec string, keeping codec "
              "string transfer function of %d",
              static_cast<int>(vi->mTransferFunction.value()));
        }
      } else {
        vi->mTransferFunction.emplace(
            ToTransferFunction(colorSpace.mTransfer.value()));
      }
    }
  }

  if (aConfig.mDescription) {
    if (!aConfig.mDescription->IsEmpty()) {
      LOG("The given config has %zu bytes of description data",
          aConfig.mDescription->Length());
      if (vi->mExtraData) {
        LOGW("The default extra data is overwritten");
      }
      vi->mExtraData = aConfig.mDescription;
    }

    // TODO: Make this utility and replace the similar one in MP4Demuxer.cpp.
    if (vi->mExtraData && !vi->mExtraData->IsEmpty() &&
        IsH264CodecString(aConfig.mCodec)) {
      SPSData spsdata;
      if (H264::DecodeSPSFromExtraData(vi->mExtraData.get(), spsdata) &&
          spsdata.pic_width > 0 && spsdata.pic_height > 0 &&
          H264::EnsureSPSIsSane(spsdata)) {
        LOG("H264 sps data - pic size: %d x %d, display size: %d x %d",
            spsdata.pic_width, spsdata.pic_height, spsdata.display_width,
            spsdata.display_height);

        if (spsdata.pic_width > MAX || spsdata.pic_height > MAX ||
            spsdata.display_width > MAX || spsdata.display_height > MAX) {
          LOGE("H264 width or height in sps data overflows");
          return Err(NS_ERROR_INVALID_ARG);
        }

        vi->mImage.width =
            static_cast<decltype(gfx::IntSize::width)>(spsdata.pic_width);
        vi->mImage.height =
            static_cast<decltype(gfx::IntSize::height)>(spsdata.pic_height);
        vi->mDisplay.width =
            static_cast<decltype(gfx::IntSize::width)>(spsdata.display_width);
        vi->mDisplay.height =
            static_cast<decltype(gfx::IntSize::height)>(spsdata.display_height);
      }
    }
  } else {
    vi->mExtraData = new MediaByteBuffer();
  }

  LOG("Created a VideoInfo for decoder - %s", vi->ToString().get());

  return track;
}

// https://w3c.github.io/webcodecs/#valid-videodecoderconfig
/* static */
bool VideoDecoderTraits::Validate(const VideoDecoderConfig& aConfig,
                                  nsCString& aErrorMessage) {
  Maybe<nsString> codec = ParseCodecString(aConfig.mCodec);
  if (!codec || codec->IsEmpty()) {
    aErrorMessage.AssignLiteral("Invalid codec string");
    LOGE("%s", aErrorMessage.get());
    return false;
  }

  if (aConfig.mCodedWidth.WasPassed() != aConfig.mCodedHeight.WasPassed()) {
    aErrorMessage.AppendPrintf(
        "Missing coded %s",
        aConfig.mCodedWidth.WasPassed() ? "height" : "width");
    LOGE("%s", aErrorMessage.get());
    return false;
  }
  if (aConfig.mCodedWidth.WasPassed() &&
      (aConfig.mCodedWidth.Value() == 0 || aConfig.mCodedHeight.Value() == 0)) {
    aErrorMessage.AssignLiteral("codedWidth and/or codedHeight can't be zero");
    LOGE("%s", aErrorMessage.get());
    return false;
  }

  if (aConfig.mDisplayAspectWidth.WasPassed() !=
      aConfig.mDisplayAspectHeight.WasPassed()) {
    aErrorMessage.AppendPrintf(
        "Missing display aspect %s",
        aConfig.mDisplayAspectWidth.WasPassed() ? "height" : "width");
    LOGE("%s", aErrorMessage.get());
    return false;
  }
  if (aConfig.mDisplayAspectWidth.WasPassed() &&
      (aConfig.mDisplayAspectWidth.Value() == 0 ||
       aConfig.mDisplayAspectHeight.Value() == 0)) {
    aErrorMessage.AssignLiteral(
        "display aspect width and height cannot be zero");
    LOGE("%s", aErrorMessage.get());
    return false;
  }

  bool detached =
      aConfig.mDescription.WasPassed() &&
      (aConfig.mDescription.Value().IsArrayBuffer()
           ? JS::ArrayBuffer::fromObject(
                 aConfig.mDescription.Value().GetAsArrayBuffer().Obj())
                 .isDetached()
           : JS::ArrayBufferView::fromObject(
                 aConfig.mDescription.Value().GetAsArrayBufferView().Obj())
                 .isDetached());

  if (detached) {
    aErrorMessage.AssignLiteral("description is detached.");
    LOGE("%s", aErrorMessage.get());
    return false;
  }

  return true;
}

/* static */
RefPtr<VideoDecoderConfigInternal> VideoDecoderTraits::CreateConfigInternal(
    const VideoDecoderConfig& aConfig) {
  return VideoDecoderConfigInternal::Create(aConfig);
}

/* static */
bool VideoDecoderTraits::IsKeyChunk(const EncodedVideoChunk& aInput) {
  return aInput.Type() == EncodedVideoChunkType::Key;
}

/* static */
UniquePtr<EncodedVideoChunkData> VideoDecoderTraits::CreateInputInternal(
    const EncodedVideoChunk& aInput) {
  return aInput.Clone();
}

/*
 * Below are VideoDecoder implementation
 */

VideoDecoder::VideoDecoder(nsIGlobalObject* aParent,
                           RefPtr<WebCodecsErrorCallback>&& aErrorCallback,
                           RefPtr<VideoFrameOutputCallback>&& aOutputCallback)
    : DecoderTemplate(aParent, std::move(aErrorCallback),
                      std::move(aOutputCallback)) {
  MOZ_ASSERT(mErrorCallback);
  MOZ_ASSERT(mOutputCallback);
  LOG("VideoDecoder %p ctor", this);
}

VideoDecoder::~VideoDecoder() { LOG("VideoDecoder %p dtor", this); }

JSObject* VideoDecoder::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  AssertIsOnOwningThread();

  return VideoDecoder_Binding::Wrap(aCx, this, aGivenProto);
}

// https://w3c.github.io/webcodecs/#dom-videodecoder-videodecoder
/* static */
already_AddRefed<VideoDecoder> VideoDecoder::Constructor(
    const GlobalObject& aGlobal, const VideoDecoderInit& aInit,
    ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  return MakeAndAddRef<VideoDecoder>(
      global.get(), RefPtr<WebCodecsErrorCallback>(aInit.mError),
      RefPtr<VideoFrameOutputCallback>(aInit.mOutput));
}

// https://w3c.github.io/webcodecs/#dom-videodecoder-isconfigsupported
/* static */
already_AddRefed<Promise> VideoDecoder::IsConfigSupported(
    const GlobalObject& aGlobal, const VideoDecoderConfig& aConfig,
    ErrorResult& aRv) {
  LOG("VideoDecoder::IsConfigSupported, config: %s",
      NS_ConvertUTF16toUTF8(aConfig.mCodec).get());

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> p = Promise::Create(global.get(), aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return p.forget();
  }

  nsCString errorMessage;
  if (!VideoDecoderTraits::Validate(aConfig, errorMessage)) {
    p->MaybeRejectWithTypeError(nsPrintfCString(
        "IsConfigSupported: config is invalid: %s", errorMessage.get()));
    return p.forget();
  }

  RootedDictionary<VideoDecoderConfig> config(aGlobal.Context());
  auto r = CloneConfiguration(config, aGlobal.Context(), aConfig, aRv);
  if (r.isErr()) {
    // This can only be an OOM: all members to clone are known to be valid
    // because this is check by ::Validate above.
    MOZ_ASSERT(r.inspectErr() == NS_ERROR_OUT_OF_MEMORY &&
               aRv.ErrorCodeIs(NS_ERROR_OUT_OF_MEMORY));
    return p.forget();
  }

  bool canDecode = CanDecode(config);
  RootedDictionary<VideoDecoderSupport> s(aGlobal.Context());
  s.mConfig.Construct(std::move(config));
  s.mSupported.Construct(canDecode);

  p->MaybeResolve(s);
  return p.forget();
}

already_AddRefed<MediaRawData> VideoDecoder::InputDataToMediaRawData(
    UniquePtr<EncodedVideoChunkData>&& aData, TrackInfo& aInfo,
    const VideoDecoderConfigInternal& aConfig) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aInfo.GetAsVideoInfo());

  if (!aData) {
    LOGE("No data for conversion");
    return nullptr;
  }

  RefPtr<MediaRawData> sample = aData->TakeData();
  if (!sample) {
    LOGE("Take no data for conversion");
    return nullptr;
  }

  // aExtraData is either provided by Configure() or a default one created for
  // the decoder creation. If it's created for decoder creation only, we don't
  // set it to sample.
  if (aConfig.mDescription && aInfo.GetAsVideoInfo()->mExtraData) {
    sample->mExtraData = aInfo.GetAsVideoInfo()->mExtraData;
  }

  LOGV(
      "EncodedVideoChunkData %p converted to %zu-byte MediaRawData - time: "
      "%" PRIi64 "us, timecode: %" PRIi64 "us, duration: %" PRIi64
      "us, key-frame: %s, has extra data: %s",
      aData.get(), sample->Size(), sample->mTime.ToMicroseconds(),
      sample->mTimecode.ToMicroseconds(), sample->mDuration.ToMicroseconds(),
      sample->mKeyframe ? "yes" : "no", sample->mExtraData ? "yes" : "no");

  return sample.forget();
}

nsTArray<RefPtr<VideoFrame>> VideoDecoder::DecodedDataToOutputType(
    nsIGlobalObject* aGlobalObject, const nsTArray<RefPtr<MediaData>>&& aData,
    const VideoDecoderConfigInternal& aConfig) {
  AssertIsOnOwningThread();

  nsTArray<RefPtr<VideoFrame>> frames;
  for (const RefPtr<MediaData>& data : aData) {
    MOZ_RELEASE_ASSERT(data->mType == MediaData::Type::VIDEO_DATA);
    RefPtr<const VideoData> d(data->As<const VideoData>());
    VideoColorSpaceInternal colorSpace;
    // Determine which color space to use: prefer the color space as configured
    // at the decoder level, if it has one, otherwise look at the underlying
    // image and make a guess.
    if (aConfig.mColorSpace.isSome() &&
        aConfig.mColorSpace->mPrimaries.isSome() &&
        aConfig.mColorSpace->mTransfer.isSome() &&
        aConfig.mColorSpace->mMatrix.isSome()) {
      colorSpace = aConfig.mColorSpace.value();
    } else {
      colorSpace = GuessColorSpace(d->mImage.get());
    }
    frames.AppendElement(CreateVideoFrame(
        aGlobalObject, d.get(), d->mTime.ToMicroseconds(),
        static_cast<uint64_t>(d->mDuration.ToMicroseconds()),
        aConfig.mDisplayAspectWidth, aConfig.mDisplayAspectHeight, colorSpace));
  }
  return frames;
}

#undef LOG
#undef LOGW
#undef LOGE
#undef LOGV
#undef LOG_INTERNAL

}  // namespace mozilla::dom
