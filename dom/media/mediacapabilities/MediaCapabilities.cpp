/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaCapabilities.h"

#include <utility>

#include "AllocationPolicy.h"
#include "DecoderTraits.h"
#include "MP4Decoder.h"
#include "MediaCapabilitiesValidation.h"
#include "MediaInfo.h"
#include "MediaRecorder.h"
#include "PDMFactory.h"
#include "VPXDecoder.h"
#include "WindowRenderer.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/EMEUtils.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/dom/DOMMozPromiseRequestHolder.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/MediaCapabilitiesBinding.h"
#include "mozilla/dom/MediaKeySystemAccess.h"
#include "mozilla/dom/MediaSource.h"
#include "mozilla/dom/Navigator.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/layers/KnowsCompositor.h"
#include "mozilla/media/MediaUtils.h"
#include "mozilla/media/webrtc/CodecInfo.h"
#include "nsContentUtils.h"
#include "nsIPrincipal.h"

namespace mozilla::dom {
enum class CodecSupport : uint8_t { Supported, Unsupported, Unknown };
static const char* EnumValueToString(const CodecSupport& aEnum) {
  static constexpr const char* kStrings[] = {"Supported", "Unsupported",
                                             "Unknown"};
  return kStrings[static_cast<size_t>(aEnum)];
}
using CodecSupportPromise =
    MozPromise<CodecSupport, nsresult, /* IsExclusive = */ true>;
// Low-resolution heuristic baseline: 640x480 = 307200 pixels.
// Compared by total pixel count (not per-dimension) so that, e.g.,
// 720x360 (259200 pixels) is correctly classified as low-resolution.
constexpr uint32_t kLowResolutionPixelCount = 640 * 480;
struct VideoConfiguration;
struct AudioConfiguration;
bool MediaCapabilitiesKeySystemConfigurationToMediaKeySystemConfiguration(
    const MediaDecodingConfiguration& aInConfig,
    MediaKeySystemConfiguration& aOutConfig);

static mediacaps::BehaviorConfig GetBehaviorConfig(nsIGlobalObject* aParent) {
  nsAutoCString host;
  if (nsIPrincipal* p = aParent ? aParent->PrincipalOrNull() : nullptr) {
    p->GetAsciiHost(host);
  }
  // DataMutex operator* is deleted on rvalues; the lock must be a named
  // variable.
  auto legacyAllowlist =
      StaticPrefs::media_mediacapabilities_legacy_allowlist();
  auto webrtcAllowlist =
      StaticPrefs::media_mediacapabilities_webrtc_enabled_allowlist();
  return {
      .mLegacy = StaticPrefs::media_mediacapabilities_legacy_enabled() ||
                 media::HostnameInValue(*legacyAllowlist, host),
      .mWebRTCEnabled = StaticPrefs::media_mediacapabilities_webrtc_enabled() ||
                        media::HostnameInValue(*webrtcAllowlist, host),
  };
}
}  // namespace mozilla::dom

template <>
struct fmt::formatter<mozilla::dom::CodecSupport>
    : fmt::formatter<std::string_view> {
  auto format(mozilla::dom::CodecSupport aSupport,
              fmt::format_context& aCtx) const {
    return fmt::format_to(aCtx.out(), "{}", EnumValueToString(aSupport));
  }
};

template <>
struct fmt::formatter<mozilla::dom::VideoConfiguration>
    : fmt::formatter<std::string_view> {
  auto format(const mozilla::dom::VideoConfiguration& aConfig,
              fmt::format_context& aCtx) const {
    return fmt::format_to(
        aCtx.out(),
        "[contentType:{} width:{} height:{} bitrate:{} framerate:{} "
        "hasAlphaChannel:{} hdrMetadataType:{} colorGamut:{} "
        "transferFunction:{} scalabilityMode:{}]",
        NS_ConvertUTF16toUTF8(aConfig.mContentType).get(), aConfig.mWidth,
        aConfig.mHeight, aConfig.mBitrate, aConfig.mFramerate,
        aConfig.mHasAlphaChannel.WasPassed()
            ? (aConfig.mHasAlphaChannel.Value() ? "true" : "false")
            : "?",
        aConfig.mHdrMetadataType.WasPassed()
            ? GetEnumString(aConfig.mHdrMetadataType.Value()).get()
            : "?",
        aConfig.mColorGamut.WasPassed()
            ? GetEnumString(aConfig.mColorGamut.Value()).get()
            : "?",
        aConfig.mTransferFunction.WasPassed()
            ? GetEnumString(aConfig.mTransferFunction.Value()).get()
            : "?",
        aConfig.mScalabilityMode.WasPassed()
            ? NS_ConvertUTF16toUTF8(aConfig.mScalabilityMode.Value()).get()
            : "?");
  }
};

template <>
struct fmt::formatter<mozilla::dom::AudioConfiguration>
    : fmt::formatter<std::string_view> {
  auto format(const mozilla::dom::AudioConfiguration& aConfig,
              fmt::format_context& aCtx) const {
    return fmt::format_to(
        aCtx.out(), "[contentType:{} channels:{} bitrate:{} samplerate:{}]",
        NS_ConvertUTF16toUTF8(aConfig.mContentType).get(),
        aConfig.mChannels.WasPassed()
            ? NS_ConvertUTF16toUTF8(aConfig.mChannels.Value()).get()
            : "?",
        aConfig.mBitrate.WasPassed() ? aConfig.mBitrate.Value() : 0,
        aConfig.mSamplerate.WasPassed() ? aConfig.mSamplerate.Value() : 0);
  }
};

template <>
struct fmt::formatter<mozilla::dom::MediaCapabilitiesInfo>
    : fmt::formatter<std::string_view> {
  auto format(const mozilla::dom::MediaCapabilitiesInfo& aInfo,
              fmt::format_context& aCtx) const {
    return fmt::format_to(
        aCtx.out(), "[supported:{} smooth:{} powerEfficient:{}]",
        aInfo.mSupported ? "true" : "false", aInfo.mSmooth ? "true" : "false",
        aInfo.mPowerEfficient ? "true" : "false");
  }
};

template <>
struct fmt::formatter<mozilla::dom::MediaEncodingConfiguration>
    : fmt::formatter<std::string_view> {
  auto format(const mozilla::dom::MediaEncodingConfiguration& aConfig,
              fmt::format_context& aCtx) const {
    auto out = aCtx.out();
    out = fmt::format_to(out, "[video: ");
    if (aConfig.mVideo.WasPassed()) {
      out = fmt::format_to(out, "{}", aConfig.mVideo.Value());
    } else {
      out = fmt::format_to(out, "None");
    }
    out = fmt::format_to(out, ", audio: ");
    if (aConfig.mAudio.WasPassed()) {
      out = fmt::format_to(out, "{}", aConfig.mAudio.Value());
    } else {
      out = fmt::format_to(out, "None");
    }
    out = fmt::format_to(out, "]");
    return out;
  }
};

template <>
struct fmt::formatter<mozilla::dom::MediaDecodingConfiguration>
    : fmt::formatter<std::string_view> {
  auto format(const mozilla::dom::MediaDecodingConfiguration& aConfig,
              fmt::format_context& aCtx) const {
    auto out = aCtx.out();
    out = fmt::format_to(out, "[");

    if (aConfig.mVideo.WasPassed()) {
      out = fmt::format_to(out, "video:{}", aConfig.mVideo.Value());
      if (aConfig.mAudio.WasPassed()) {
        out = fmt::format_to(out, " ");
      }
    }

    if (aConfig.mAudio.WasPassed()) {
      out = fmt::format_to(out, "audio:{}", aConfig.mAudio.Value());
    }

    if (aConfig.mKeySystemConfiguration.WasPassed()) {
      out =
          fmt::format_to(out, "[keySystem:{}, ",
                         NS_ConvertUTF16toUTF8(
                             aConfig.mKeySystemConfiguration.Value().mKeySystem)
                             .get());

      mozilla::dom::MediaKeySystemConfiguration emeConfig;
      if (mozilla::dom::
              MediaCapabilitiesKeySystemConfigurationToMediaKeySystemConfiguration(
                  aConfig, emeConfig)) {
        nsCString emeStr =
            mozilla::dom::MediaKeySystemAccess::ToCString(emeConfig);
        out = std::copy(emeStr.BeginReading(), emeStr.EndReading(), out);
      }
      out = fmt::format_to(out, "]");
    }

    out = fmt::format_to(out, "]");
    return out;
  }
};

template <>
struct fmt::formatter<mozilla::dom::MediaCapabilitiesDecodingInfo>
    : fmt::formatter<std::string_view> {
  auto format(const mozilla::dom::MediaCapabilitiesDecodingInfo& aInfo,
              fmt::format_context& aCtx) const {
    return fmt::format_to(
        aCtx.out(),
        "[supported:{} smooth:{} powerEfficient:{} keySystemAccess:{}]",
        aInfo.mSupported ? "true" : "false", aInfo.mSmooth ? "true" : "false",
        aInfo.mPowerEfficient ? "true" : "false",
        aInfo.mKeySystemAccess ? "present" : "null");
  }
};

mozilla::LazyLogModule sMediaCapabilitiesLog("MediaCapabilities");

#define LOG(fmt, ...)                                          \
  MOZ_LOG_FMT(sMediaCapabilitiesLog, mozilla::LogLevel::Debug, \
              "[MediaCapabilities] {}: " fmt, __func__, __VA_ARGS__)

namespace mozilla::dom {
using mediacaps::IsValidMediaDecodingConfiguration;
using mediacaps::IsValidMediaEncodingConfiguration;

// Caches codec support state (e.g., WebrtcCodecInfo) for reuse across
// audio and video support queries within a single MediaCapabilities request.
class MOZ_STACK_CLASS CodecSupportState final {
 public:
  // The MediaCapabilities pointer is held as a raw pointer to avoid
  // refcount-thread-mismatch: MediaCapabilities uses main-thread-only
  // refcounting, while CodecSupportState is thread-safe-refcounted and may
  // be released on a non-main thread by an InvokeAsync continuation.
  // The caller (a MediaCapabilities member function) keeps itself alive
  // via the outer promise chain's `self` capture for the full duration
  // of any synchronous CheckTypeFor* calls below.
  explicit CodecSupportState(const MediaCapabilities& aCaps,
                             const mediacaps::BehaviorConfig& aBehavior)
      : mCaps(aCaps), mBehavior(aBehavior) {}

  const mozilla::WebrtcCodecInfo& WebrtcCodecInfo() const {
    if (!mWebrtcCodecInfo) {
      mWebrtcCodecInfo = mozilla::WebrtcCodecInfo::Create();
    }
    return *mWebrtcCodecInfo;
  }

  [[nodiscard]]
  CodecSupport CheckVideoDecodeSupport(
      const MediaDecodingConfiguration& aConfig,
      const MediaExtendedMIMEType& aMime) const {
    const VideoConfiguration& videoConfig = aConfig.mVideo.Value();
    Maybe<ColorGamut> gamut = videoConfig.mColorGamut.WasPassed()
                                  ? Some(videoConfig.mColorGamut.Value())
                                  : Nothing();
    Maybe<TransferFunction> transfer =
        videoConfig.mTransferFunction.WasPassed()
            ? Some(videoConfig.mTransferFunction.Value())
            : Nothing();
    return CheckCodecSupport(aMime, aConfig.mType, gamut, transfer);
  }

  [[nodiscard]]
  CodecSupport CheckVideoEncodeSupport(
      const MediaEncodingConfiguration& aConfig,
      const MediaExtendedMIMEType& aMime) const {
    const VideoConfiguration& videoConfig = aConfig.mVideo.Value();
    Maybe<ColorGamut> gamut = videoConfig.mColorGamut.WasPassed()
                                  ? Some(videoConfig.mColorGamut.Value())
                                  : Nothing();
    Maybe<TransferFunction> transfer =
        videoConfig.mTransferFunction.WasPassed()
            ? Some(videoConfig.mTransferFunction.Value())
            : Nothing();
    return CheckCodecSupport(aMime, aConfig.mType, gamut, transfer);
  }

  [[nodiscard]]
  CodecSupport CheckAudioDecodeSupport(
      const MediaDecodingConfiguration& aConfig,
      const MediaExtendedMIMEType& aMime) const {
    return CheckCodecSupport(aMime, aConfig.mType, Nothing(), Nothing());
  }

  [[nodiscard]]
  CodecSupport CheckAudioEncodeSupport(
      const MediaEncodingConfiguration& aConfig,
      const MediaExtendedMIMEType& aMime) const {
    return CheckCodecSupport(aMime, aConfig.mType, Nothing(), Nothing());
  }

 private:
  const MediaCapabilities& mCaps;
  mediacaps::BehaviorConfig mBehavior;
  mutable std::unique_ptr<mozilla::WebrtcCodecInfo> mWebrtcCodecInfo;

  [[nodiscard]] CodecSupport CheckCodecSupport(
      const MediaExtendedMIMEType& aMime, MediaDecodingType aType,
      const Maybe<ColorGamut>& aColorGamut,
      const Maybe<TransferFunction>& aTransferFunction) const {
    if (mediacaps::CheckMIMETypeSupport(aMime, AsVariant(aType), aColorGamut,
                                        aTransferFunction, mBehavior)
            .isErr()) {
      return CodecSupport::Unsupported;
    }
    switch (aType) {
      case MediaDecodingType::File:
        return mCaps.CheckTypeForFile(aMime) ? CodecSupport::Supported
                                             : CodecSupport::Unsupported;
      case MediaDecodingType::Media_source:
        return mCaps.CheckTypeForMediaSource(aMime) ? CodecSupport::Supported
                                                    : CodecSupport::Unsupported;
      case MediaDecodingType::Webrtc:
        return WebrtcCodecInfo().CheckDecodeType(aMime)
                   ? CodecSupport::Supported
                   : CodecSupport::Unsupported;
      default:
        MOZ_ASSERT_UNREACHABLE("Unhandled MediaDecodingType");
        return CodecSupport::Unsupported;
    }
  }

  [[nodiscard]] CodecSupport CheckCodecSupport(
      const MediaExtendedMIMEType& aMime, MediaEncodingType aType,
      const Maybe<ColorGamut>& aColorGamut,
      const Maybe<TransferFunction>& aTransferFunction) const {
    if (mediacaps::CheckMIMETypeSupport(aMime, AsVariant(aType), aColorGamut,
                                        aTransferFunction, mBehavior)
            .isErr()) {
      return CodecSupport::Unsupported;
    }
    switch (aType) {
      case MediaEncodingType::Record:
        return mCaps.CheckTypeForEncoder(aMime) ? CodecSupport::Supported
                                                : CodecSupport::Unsupported;
      case MediaEncodingType::Webrtc:
        return WebrtcCodecInfo().CheckEncodeType(aMime)
                   ? CodecSupport::Supported
                   : CodecSupport::Unsupported;
      default:
        MOZ_ASSERT_UNREACHABLE("Unhandled MediaEncodingType");
        return CodecSupport::Unsupported;
    }
  }
};

// Thread allocation matching libwebrtc's NumberOfThreads() per encoder.
// Sources: third_party/libwebrtc/modules/video_coding/codecs/*/
static uint32_t Av1EncoderThreads(const uint32_t aPixels,
                                  const uint32_t aCores) {
  // libaom_av1_encoder_v2.cc GetThreadingTilesAndSuperblockSize
  // https://searchfox.org/firefox-main/rev/8352bcb6d75d53f3e2190221b71190e47afa0bfc/third_party/libwebrtc/modules/video_coding/codecs/av1/libaom_av1_encoder_v2.cc#122-144
  if ((aPixels >= 1920u * 1080u) && (aCores > 8)) {
    return 8;
  } else if ((aPixels >= 640u * 360u) && (aCores > 4)) {
    return 4;
  } else if ((aPixels >= 320u * 180u) && (aCores > 2)) {
    return 2;
  }
  return 1;
}
static uint32_t Vp9EncoderThreads(const uint32_t aPixels,
                                  const uint32_t aCores) {
  // libvpx_vp9_encoder.cc NumberOfThreads
  // https://searchfox.org/firefox-main/rev/8352bcb6d75d53f3e2190221b71190e47afa0bfc/third_party/libwebrtc/modules/video_coding/codecs/vp9/libvpx_vp9_encoder.cc#762-781
  if ((aPixels >= 1280u * 720u) && (aCores > 4)) {
    return 4;
  } else if ((aPixels >= 640u * 360u) && (aCores > 2)) {
    return 2;
  }
  return 1;
}
static uint32_t Vp8EncoderThreads(const uint32_t aPixels,
                                  const uint32_t aCores) {
  // libvpx_vp8_encoder.cc NumberOfThreads
  // https://searchfox.org/firefox-main/rev/8352bcb6d75d53f3e2190221b71190e47afa0bfc/third_party/libwebrtc/modules/video_coding/codecs/vp8/libvpx_vp8_encoder.cc#821-872
#if defined(MOZ_WIDGET_ANDROID)
  if ((aPixels >= 320u * 180u)) {
    if (aCores >= 4) {
      return 3;
    } else if (aCores >= 2) {
      return 2;
    }
  }
  return 1;
#else
  if ((aPixels >= 1920u * 1080u) && (aCores > 8)) {
    return 8;
  } else if ((aPixels > 1280u * 960u) && (aCores >= 6)) {
    return 3;
  } else if ((aPixels > 640u * 480u) && (aCores >= 3)) {
    return (aCores >= 6 ? 3 : 2);
  }
  return 1;
#endif
}
static bool IsWebRTCSWEncodeSmooth(const VideoConfiguration& aConfig) {
  const auto shouldForceSmooth =
      StaticPrefs::media_mediacapabilities_webrtc_encode_smooth_override();
  if (shouldForceSmooth == 1) {
    return true;
  } else if (shouldForceSmooth == 2) {
    return false;
  }

  const NS_ConvertUTF16toUTF8 mimeStr(aConfig.mContentType);
  const int32_t slash = mimeStr.FindChar('/');
  if (slash < 0) {
    return false;
  }
  const auto afterSlash = Substring(mimeStr, slash + 1);
  const int32_t semi = afterSlash.FindChar(';');
  nsAutoCString codecStr(semi >= 0 ? Substring(afterSlash, 0, semi)
                                   : afterSlash);
  codecStr.Trim(" \t");

  // ratio = clip_duration / wall_time at 60fps: >1.0 means faster than
  // real-time. threads = libwebrtc thread count used during measurement. For
  // non-standard resolutions: rounds up to nearest standard bucket
  // Approximates scaling linearly by framerate and thread count.
  static const struct {
    const char* codec;
    uint32_t w, h;
    float ratio;  // realtime ratio at 60fps; >1.0 = faster than real-time
    uint32_t threads;
  } kMeasured[] = {
      {"h264", 426, 240, 2.06f, 1},   {"h264", 854, 480, 1.71f, 1},
      {"h264", 1280, 720, 1.51f, 1},  {"h264", 1920, 1080, 1.37f, 1},
      {"h264", 3840, 2160, 0.47f, 1},  // not smooth at 60fps
      {"av1", 426, 240, 2.10f, 2},    {"av1", 854, 480, 1.43f, 4},
      {"av1", 1280, 720, 0.98f, 4},   // not smooth at 60fps
      {"av1", 1920, 1080, 0.73f, 4},  // not smooth at 60fps
      {"av1", 3840, 2160, 0.26f, 4},  // not smooth at 60fps
      {"vp9", 426, 240, 1.94f, 1},    {"vp9", 854, 480, 1.85f, 2},
      {"vp9", 1280, 720, 1.64f, 4},   {"vp9", 1920, 1080, 1.20f, 4},
      {"vp9", 3840, 2160, 0.50f, 4},  // not smooth at 60fps
      {"vp8", 426, 240, 2.01f, 1},    {"vp8", 854, 480, 1.80f, 3},
      {"vp8", 1280, 720, 1.54f, 3},   {"vp8", 1920, 1080, 1.31f, 3},
      {"vp8", 3840, 2160, 0.55f, 3},  // not smooth at 60fps
  };

  const CheckedInt<uint32_t> pixelCount =
      CheckedInt<uint32_t>(aConfig.mWidth) * aConfig.mHeight;
  if (!pixelCount.isValid() || !std::isfinite(aConfig.mFramerate) ||
      aConfig.mFramerate <= 0) {
    return false;
  }
  const uint32_t pixels = pixelCount.value();
  const uint32_t rfps =
      std::max(1u, static_cast<uint32_t>(aConfig.mFramerate + 0.5));
  const uint32_t cores =
      std::max(1u, static_cast<uint32_t>(GetNumberOfProcessors()));

  // Actual thread count for this machine and codec.
  uint32_t actualThreads = 1;  // h264 (openh264) is always single-threaded
  if (codecStr.EqualsIgnoreCase("av1")) {
    actualThreads = Av1EncoderThreads(pixels, cores);
  } else if (codecStr.EqualsIgnoreCase("vp9")) {
    actualThreads = Vp9EncoderThreads(pixels, cores);
  } else if (codecStr.EqualsIgnoreCase("vp8")) {
    actualThreads = Vp8EncoderThreads(pixels, cores);
  }

  // kMeasured is sorted ascending by resolution within each codec.
  // First entry with bucket_pixels >= pixels is the smallest valid bucket
  // (rounds up, conservative).
  int32_t bucketIdx = -1;
  for (int32_t i = 0; i < static_cast<int32_t>(std::size(kMeasured)); i++) {
    if (!codecStr.EqualsIgnoreCase(kMeasured[i].codec)) {
      continue;
    } else if (kMeasured[i].w * kMeasured[i].h >= pixels) {
      bucketIdx = i;
      break;
    }
  }
  if (bucketIdx < 0) {
    return false;  // exceeds largest bucket (> 4K)
  }

  const auto& bucket = kMeasured[bucketIdx];
  const float scaledRatio =
      bucket.ratio * (60.0f / static_cast<float>(rfps)) *
      (static_cast<float>(actualThreads) / static_cast<float>(bucket.threads));
  return scaledRatio >= 1.0f;
}

// Gets the global's event target and creates a new DOMMozPromiseRequestHolder
// for that target. Must be called on the global's event target. On workers,
// acquires a StrongWorkerRef to block shutdown while the promise is in-flight.
// Returns false if the worker is already shutting down.
template <typename T>
[[nodiscard]]
static bool GetThreadForAsyncRequest(
    nsIGlobalObject* aParent, RefPtr<DOMMozPromiseRequestHolder<T>>* aHolderOut,
    RefPtr<nsISerialEventTarget>* aTargetThreadOut,
    RefPtr<StrongWorkerRef>* aWorkerRefOut, const char* aTag) {
  auto holder = MakeRefPtr<DOMMozPromiseRequestHolder<T>>(aParent);
  RefPtr<nsISerialEventTarget> target = aParent->SerialEventTarget();
  MOZ_ASSERT(target->IsOnCurrentThread());

  if (NS_IsMainThread()) {
    *aHolderOut = std::move(holder);
    *aTargetThreadOut = std::move(target);
    return true;
  }

  WorkerPrivate* wp = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT(wp, "Must be called from a worker thread");

  RefPtr<StrongWorkerRef> ref = StrongWorkerRef::Create(
      wp, aTag, [holder]() { holder->DisconnectIfExists(); });
  if (NS_WARN_IF(!ref)) {
    return false;
  }

  *aHolderOut = std::move(holder);
  *aTargetThreadOut = std::move(target);
  *aWorkerRefOut = std::move(ref);
  return true;
}

bool MediaCapabilitiesKeySystemConfigurationToMediaKeySystemConfiguration(
    const MediaDecodingConfiguration& aInConfig,
    MediaKeySystemConfiguration& aOutConfig) {
  if (!aInConfig.mKeySystemConfiguration.WasPassed()) {
    return false;
  }

  const auto& keySystemConfig = aInConfig.mKeySystemConfiguration.Value();
  if (!keySystemConfig.mInitDataType.IsEmpty()) {
    if (NS_WARN_IF(!aOutConfig.mInitDataTypes.AppendElement(
            keySystemConfig.mInitDataType, fallible))) {
      return false;
    }
  }
  if (keySystemConfig.mSessionTypes.WasPassed() &&
      !keySystemConfig.mSessionTypes.Value().IsEmpty()) {
    aOutConfig.mSessionTypes.Construct();
    for (const auto& type : keySystemConfig.mSessionTypes.Value()) {
      if (NS_WARN_IF(!aOutConfig.mSessionTypes.Value().AppendElement(
              type, fallible))) {
        return false;
      }
    }
  }
  aOutConfig.mDistinctiveIdentifier = keySystemConfig.mDistinctiveIdentifier;
  aOutConfig.mPersistentState = keySystemConfig.mPersistentState;

  if (aInConfig.mAudio.WasPassed()) {
    auto* capabilitiy = aOutConfig.mAudioCapabilities.AppendElement(fallible);
    if (NS_WARN_IF(!capabilitiy)) {
      return false;
    }
    capabilitiy->mContentType = aInConfig.mAudio.Value().mContentType;
    if (keySystemConfig.mAudio.WasPassed()) {
      const auto& config = keySystemConfig.mAudio.Value();
      capabilitiy->mRobustness = config.mRobustness;
      capabilitiy->mEncryptionScheme = config.mEncryptionScheme;
    }
  }
  if (aInConfig.mVideo.WasPassed()) {
    auto* capabilitiy = aOutConfig.mVideoCapabilities.AppendElement(fallible);
    if (NS_WARN_IF(!capabilitiy)) {
      return false;
    }
    capabilitiy->mContentType = aInConfig.mVideo.Value().mContentType;
    if (keySystemConfig.mVideo.WasPassed()) {
      const auto& config = keySystemConfig.mVideo.Value();
      capabilitiy->mRobustness = config.mRobustness;
      capabilitiy->mEncryptionScheme = config.mEncryptionScheme;
    }
  }
  return true;
}

MediaCapabilities::MediaCapabilities(nsIGlobalObject* aParent)
    : mParent(aParent) {}

void MediaCapabilities::CreateWebRTCDecodingInfo(
    const MediaDecodingConfiguration& aConfiguration, Promise* aPromise,
    Maybe<MediaContainerType> aVideoContainer,
    Maybe<MediaContainerType> aAudioContainer) {
  using PromiseType =
      MozPromise<MediaCapabilitiesDecodingInfo, bool, /*IsExclusive=*/true>;
  RefPtr<DOMMozPromiseRequestHolder<PromiseType>> holder;
  RefPtr<nsISerialEventTarget> targetThread;
  RefPtr<StrongWorkerRef> workerRef;
  if (!GetThreadForAsyncRequest<PromiseType>(
          mParent, &holder, &targetThread, &workerRef,
          "MediaCapabilities::DecodingInfo")) {
    aPromise->MaybeRejectWithInvalidStateError("The worker is shutting down");
    return;
  }

  RefPtr<TaskQueue> taskQueue =
      TaskQueue::Create(GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER),
                        "MediaCapabilities::TaskQueue");
  InvokeAsync(
      taskQueue, __func__,
      [aConfiguration, videoContainer = std::move(aVideoContainer),
       audioContainer = std::move(aAudioContainer)] {
        MOZ_ASSERT(videoContainer || audioContainer);

        // Step 7 returns early if neither audio nor video are supported.
        // If video isn't supported, audio must be - they can't both be
        // unknown. We can assume audio decoding and playback, which should be
        // smooth and powerEfficient.
        MediaCapabilitiesDecodingInfo info;
        info.mSupported = true;  // Passed previous support check
        info.mSmooth = true;
        info.mPowerEfficient = true;

        if (videoContainer) {
          auto videoSupport =
              // TODO(Bug 2024767): Handle fmtp parameters
              SupportsVideoMimeDecodeForWebrtc(videoContainer->ExtendedType());
          const auto& v = aConfiguration.mVideo.Value();
          const CheckedInt<uint32_t> pixels =
              CheckedInt<uint32_t>(v.mWidth) * CheckedInt<uint32_t>(v.mHeight);
          const bool lowResolution =
              pixels.isValid() && pixels.value() <= kLowResolutionPixelCount;
          const bool hwSupported =
              videoSupport.contains(media::DecodeSupport::HardwareDecode);
          info.mPowerEfficient = hwSupported || lowResolution;
        }

        return PromiseType::CreateAndResolve(
            std::move(info), "MediaCapabilities::CreateWebRTCDecodingInfo");
      })
      ->Then(
          targetThread, __func__,
          [promise = RefPtr(aPromise), workerRef,
           holder](MediaCapabilitiesDecodingInfo&& aInfo) {
            holder->Complete();
            nsIGlobalObject* global = holder->GetParentObject();
            NS_ENSURE_TRUE_VOID(global);
            promise->MaybeResolve(std::move(aInfo));
          },
          [] { MOZ_CRASH("Unexpected"); })
      ->Track(*holder);
}

// https://w3c.github.io/media-capabilities/#dom-mediacapabilities-decodinginfo
// Section 2.5.2 DecodingInfo() Method
already_AddRefed<Promise> MediaCapabilities::DecodingInfo(
    const MediaDecodingConfiguration& aConfiguration, ErrorResult& aRv) {
  RefPtr<Promise> promise = Promise::Create(mParent, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  const auto behavior = GetBehaviorConfig(mParent);

  // If WebRTC type is used and WebRTC is not enabled for this origin, reject.
  if (aConfiguration.mType == MediaDecodingType::Webrtc &&
      !behavior.mWebRTCEnabled) {
    promise->MaybeRejectWithTypeError<MSG_INVALID_ENUM_VALUE>(
        "type", "webrtc", "MediaDecodingType");
    return promise.forget();
  }

  // Step 1: If configuration is not a valid MediaDecodingConfiguration,
  // return a Promise rejected with a newly created TypeError.
  if (auto configCheck =
          IsValidMediaDecodingConfiguration(aConfiguration, behavior);
      configCheck.isErr()) {
    RejectWithValidationResult(promise, configCheck.unwrapErr());
    return promise.forget();
  }

  // Step 2: If configuration.keySystemConfiguration exists,
  // run the following substeps:
  if (aConfiguration.mKeySystemConfiguration.WasPassed()) {
    // Step 2.1: If the global object is of type WorkerGlobalScope,
    //           return a Promise rejected with a newly created DOMException
    //           whose name is InvalidStateError.
    if (IsWorkerGlobal(mParent->GetGlobalJSObject())) {
      promise->MaybeRejectWithInvalidStateError(
          "key system configuration is not allowed in the worker scope");
      return promise.forget();
    }
    // Step 2.2 If the global object’s relevant settings object is a
    //          non-secure context, return a Promise rejected with a newly
    //          created DOMException whose name is SecurityError.
    if (auto* window = mParent->GetAsInnerWindow();
        window && !window->IsSecureContext()) {
      promise->MaybeRejectWithSecurityError(
          "key system configuration is not allowed in a non-secure context");
      return promise.forget();
    }
  }

  // Step 3: Let p be a new Promise (already have it!)
  // Step 4: In parallel, run the Create a MediaCapabilitiesDecodingInfo
  //         algorithm with configuration and resolve p with its result.
  CreateMediaCapabilitiesDecodingInfo(aConfiguration, aRv, promise, behavior);
  return promise.forget();
}

// https://w3c.github.io/media-capabilities/#create-media-capabilities-decoding-info
void MediaCapabilities::CreateMediaCapabilitiesDecodingInfo(
    const MediaDecodingConfiguration& aConfiguration, ErrorResult& aRv,
    Promise* aPromise, const mediacaps::BehaviorConfig& aBehavior) {
  LOG("Processing {}", aConfiguration);

  const bool isWebRTC =
      mediacaps::IsMediaTypeWebRTC(AsVariant(aConfiguration.mType));
  CodecSupport videoSupported = CodecSupport::Unknown;
  CodecSupport audioSupported = CodecSupport::Unknown;
  CodecSupportState state(*this, aBehavior);

  Maybe<MediaContainerType> videoContainer;
  Maybe<MediaContainerType> audioContainer;

  // If configuration.video is present and is not a valid video configuration,
  // return a Promise rejected with a TypeError.
  if (aConfiguration.mVideo.WasPassed()) {
    auto videoMime = MakeMediaExtendedMIMEType(aConfiguration.mVideo.Value());
    if (!videoMime) {
      aPromise->MaybeRejectWithTypeError("Invalid VideoConfiguration");
      return;
    }
    videoSupported = state.CheckVideoDecodeSupport(aConfiguration, *videoMime);
    if (videoSupported == CodecSupport::Supported) {
      videoContainer = Some(MediaContainerType(std::move(*videoMime)));
    }
  }

  if (aConfiguration.mAudio.WasPassed()) {
    auto audioMime = MakeMediaExtendedMIMEType(aConfiguration.mAudio.Value());
    if (!audioMime) {
      aPromise->MaybeRejectWithTypeError("Invalid AudioConfiguration");
      return;
    }
    audioSupported = state.CheckAudioDecodeSupport(aConfiguration, *audioMime);
    if (audioSupported == CodecSupport::Supported) {
      audioContainer = Some(MediaContainerType(std::move(*audioMime)));
    }
  }
  const bool bothSupportUnknown = videoSupported == CodecSupport::Unknown &&
                                  audioSupported == CodecSupport::Unknown;

  // Step 4.6: If either videoSupported or audioSupported is unsupported, set
  // supported to false, smooth to false, powerEfficient to false, and return
  // info.
  if (videoSupported == CodecSupport::Unsupported ||
      audioSupported == CodecSupport::Unsupported || bothSupportUnknown) {
    MediaCapabilitiesDecodingInfo info;
    info.mSupported = false;
    info.mSmooth = false;
    info.mPowerEfficient = false;
    aPromise->MaybeResolve(std::move(info));
    return;
  }

  if (isWebRTC) {
    CreateWebRTCDecodingInfo(aConfiguration, aPromise,
                             std::move(videoContainer),
                             std::move(audioContainer));
  } else {
    CreateNonWebRTCDecodingInfo(aConfiguration, aPromise,
                                std::move(videoContainer),
                                std::move(audioContainer));
  }
}

static MediaCapabilitiesDecodingInfo CreateVideoDecodingInfo(
    const TrackInfo& aConfig, const bool aShouldResistFingerprinting,
    const bool aHardwareAccelerated) {
  MediaCapabilitiesDecodingInfo info;
  info.mSupported = true;
  info.mSmooth = true;
  info.mPowerEfficient = false;
  if (aShouldResistFingerprinting) {
    return info;
  }
  MOZ_ASSERT(aConfig.IsVideo());
  // mImage dimensions are int32_t from gfx::IntSize. CheckedInt rejects
  // negative inputs (mapping to !isValid()) and rejects width*height
  // overflow, in either case treating the result as not-low-resolution.
  const auto& image = aConfig.GetAsVideoInfo()->mImage;
  const CheckedInt<uint32_t> pixels =
      CheckedInt<uint32_t>(image.width) * CheckedInt<uint32_t>(image.height);
  const bool lowResolution =
      pixels.isValid() && pixels.value() <= kLowResolutionPixelCount;
  info.mPowerEfficient = aHardwareAccelerated || lowResolution;
  return info;
}

void MediaCapabilities::CreateNonWebRTCDecodingInfo(
    const MediaDecodingConfiguration& aConfiguration, Promise* aPromise,
    Maybe<MediaContainerType> aVideoContainer,
    Maybe<MediaContainerType> aAudioContainer) {
  nsTArray<UniquePtr<TrackInfo>> tracks;
  if (aConfiguration.mVideo.WasPassed()) {
    MOZ_ASSERT(aVideoContainer.isSome(),
               "configuration is valid and supported");
    auto videoTracks = DecoderTraits::GetTracksInfo(*aVideoContainer);
    // If the MIME type does not imply a codec, the string MUST
    // also have one and only one parameter that is named codecs with a value
    // describing a single media codec. Otherwise, it MUST contain no
    // parameters.
    if (videoTracks.Length() != 1) {
      aPromise->MaybeRejectWithTypeError(nsPrintfCString(
          "The provided type '%s' does not have a 'codecs' parameter.",
          aVideoContainer->OriginalString().get()));
      return;
    }
    MOZ_DIAGNOSTIC_ASSERT(videoTracks.ElementAt(0),
                          "must contain a valid trackinfo");
    // If the type refers to an audio codec, reject now.
    if (videoTracks[0]->GetType() != TrackInfo::kVideoTrack) {
      aPromise->MaybeRejectWithTypeError("Invalid VideoConfiguration");
      return;
    }
    tracks.AppendElements(std::move(videoTracks));
  }
  if (aConfiguration.mAudio.WasPassed()) {
    MOZ_ASSERT(aAudioContainer.isSome(),
               "configuration is valid and supported");
    auto audioTracks = DecoderTraits::GetTracksInfo(*aAudioContainer);
    // If the MIME type does not imply a codec, the string MUST
    // also have one and only one parameter that is named codecs with a value
    // describing a single media codec. Otherwise, it MUST contain no
    // parameters.
    if (audioTracks.Length() != 1) {
      aPromise->MaybeRejectWithTypeError(nsPrintfCString(
          "The provided type '%s' does not have a 'codecs' parameter.",
          aAudioContainer->OriginalString().get()));
      return;
    }
    MOZ_DIAGNOSTIC_ASSERT(audioTracks.ElementAt(0),
                          "must contain a valid trackinfo");
    // If the type refers to a video codec, reject now.
    if (audioTracks[0]->GetType() != TrackInfo::kAudioTrack) {
      aPromise->MaybeRejectWithTypeError("Invalid AudioConfiguration");
      return;
    }
    tracks.AppendElements(std::move(audioTracks));
  }

  // On Windows, the MediaDataDecoder expects to be created on a thread
  // supporting MTA, which the main thread doesn't. So we use our task queue
  // to create such decoder and perform initialization.
  RefPtr<TaskQueue> taskQueue =
      TaskQueue::Create(GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER),
                        "MediaCapabilities::TaskQueue");
  RefPtr<layers::KnowsCompositor> compositor = GetCompositor();
  const bool shouldResistFingerprinting =
      mParent->ShouldResistFingerprinting(RFPTarget::MediaCapabilities);
  float frameRate =
      aConfiguration.mVideo.WasPassed() && aVideoContainer.isSome()
          ? static_cast<float>(
                aVideoContainer->ExtendedType().GetFramerate().ref())
          : 0.0f;

  // Step 3: If configuration.keySystemConfiguration exists:
  if (aConfiguration.mKeySystemConfiguration.WasPassed()) {
    MOZ_ASSERT(
        NS_IsMainThread(),
        "Key system configuration qurey can not run on the worker thread!");

    RefPtr<nsISerialEventTarget> mainThread = GetMainThreadSerialEventTarget();
    if (!mainThread) {
      aPromise->MaybeRejectWithInvalidStateError(
          "The main thread is shutted down");
      return;
    }

    // This check isn't defined in the spec but exists in web platform tests,
    // so we perform the check as well in order to reduce the web
    // compatibility issues.
    // https://github.com/w3c/media-capabilities/issues/220
    const auto& keySystemConfig =
        aConfiguration.mKeySystemConfiguration.Value();
    if ((keySystemConfig.mVideo.WasPassed() &&
         !aConfiguration.mVideo.WasPassed()) ||
        (keySystemConfig.mAudio.WasPassed() &&
         !aConfiguration.mAudio.WasPassed())) {
      aPromise->MaybeRejectWithTypeError(
          "The type of decoding config doesn't match the type of key system "
          "config");
      return;
    }
    UniquePtr<TrackInfo> videoInfo;
    if (aConfiguration.mVideo.WasPassed() && aVideoContainer.isSome()) {
      videoInfo = std::move(tracks[0]);
    }
    CheckEncryptedDecodingSupport(aConfiguration)
        ->Then(
            mainThread, __func__,
            [promise = RefPtr<Promise>{aPromise}, aConfiguration, mainThread,
             taskQueue, compositor, shouldResistFingerprinting, frameRate,
             videoInfo = std::move(videoInfo)](
                MediaKeySystemAccessManager::MediaKeySystemAccessPromise::
                    ResolveOrRejectValue&& aValue) mutable {
              if (aValue.IsReject()) {
                MediaCapabilitiesDecodingInfo info;
                info.mSupported = false;
                info.mSmooth = false;
                info.mPowerEfficient = false;
                LOG("DRM support check rejected: {} -> {}", aConfiguration,
                    info);
                promise->MaybeResolve(std::move(info));
                return;
              }

              MediaCapabilitiesDecodingInfo drmInfo;
              drmInfo.mSupported = true;
              drmInfo.mSmooth = true;
              drmInfo.mKeySystemAccess = aValue.ResolveValue();
              MOZ_ASSERT(drmInfo.mKeySystemAccess);
              MediaKeySystemConfiguration config;
              drmInfo.mKeySystemAccess->GetConfiguration(config);
              const bool hwDRM = IsHardwareDecryptionSupported(config);

              if (shouldResistFingerprinting) {
                if (hwDRM) {
                  drmInfo.mSupported = false;
                  drmInfo.mSmooth = false;
                  drmInfo.mPowerEfficient = false;
                } else {
                  drmInfo.mPowerEfficient = false;
                }
                LOG("RFP: suppressing DRM capabilities: {} -> {}",
                    aConfiguration, drmInfo);
                promise->MaybeResolve(std::move(drmInfo));
                return;
              }

              if (hwDRM || !videoInfo) {
                drmInfo.mPowerEfficient = hwDRM && !!videoInfo;
                LOG("DRM hardware decrypt or no video track: {} -> {}",
                    aConfiguration, drmInfo);
                promise->MaybeResolve(std::move(drmInfo));
                return;
              }

              // Software DRM: query the video decoder for powerEfficient.
              CheckVideoDecodingInfo(taskQueue, compositor, frameRate,
                                     false /* RFP already handled */,
                                     std::move(videoInfo))
                  ->Then(
                      mainThread, __func__,
                      [promise, drmInfo = std::move(drmInfo), aConfiguration](
                          CapabilitiesPromise::ResolveOrRejectValue&&
                              aDecoderResult) mutable {
                        if (aDecoderResult.IsResolve()) {
                          drmInfo.mPowerEfficient =
                              aDecoderResult.ResolveValue().mPowerEfficient;
                        } else {
                          drmInfo.mPowerEfficient = false;
                        }
                        LOG("Software DRM decoder check: {} -> {}",
                            aConfiguration, drmInfo);
                        promise->MaybeResolve(std::move(drmInfo));
                      });
            });
    return;
  }

  // Step 4: Otherwise, run the following steps:
  nsTArray<RefPtr<CapabilitiesPromise>> promises;

  for (auto&& config : tracks) {
    TrackInfo::TrackType type =
        config->IsVideo() ? TrackInfo::kVideoTrack : TrackInfo::kAudioTrack;

    MOZ_ASSERT(type == TrackInfo::kAudioTrack ||
                   aVideoContainer->ExtendedType().GetFramerate().isSome(),
               "framerate is a required member of VideoConfiguration");

    if (type == TrackInfo::kAudioTrack) {
      // There's no need to create an audio decoder has we only want to know
      // if such codec is supported. We do need to call the
      // PDMFactory::Supports API outside the main thread to get accurate
      // results.
      promises.AppendElement(
          InvokeAsync(taskQueue, __func__, [config = std::move(config)]() {
            RefPtr<PDMFactory> pdm = new PDMFactory();
            SupportDecoderParams params{*config};
            if (pdm->Supports(params, nullptr /* decoder doctor */).isEmpty()) {
              return CapabilitiesPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                          __func__);
            }
            MediaCapabilitiesDecodingInfo info;
            info.mSupported = true;
            info.mSmooth = true;
            info.mPowerEfficient = true;
            return CapabilitiesPromise::CreateAndResolve(std::move(info),
                                                         __func__);
          }));
      continue;
    }

    promises.AppendElement(
        CheckVideoDecodingInfo(taskQueue, compositor, frameRate,
                               shouldResistFingerprinting, std::move(config)));
  }

  MOZ_ASSERT(tracks.Length() <= 2);

  RefPtr<DOMMozPromiseRequestHolder<CapabilitiesPromise::AllPromiseType>>
      holder;
  RefPtr<nsISerialEventTarget> targetThread;
  RefPtr<StrongWorkerRef> workerRef;
  if (!GetThreadForAsyncRequest<CapabilitiesPromise::AllPromiseType>(
          mParent, &holder, &targetThread, &workerRef,
          "MediaCapabilities::DecodingInfo")) {
    aPromise->MaybeRejectWithInvalidStateError("The worker is shutting down");
    return;
  }

  CapabilitiesPromise::All(taskQueue, promises)
      ->Then(targetThread, __func__,
             [promise = RefPtr{aPromise}, tracks = std::move(tracks), workerRef,
              holder, aConfiguration](
                 CapabilitiesPromise::AllPromiseType::ResolveOrRejectValue&&
                     aValue) {
               holder->Complete();
               nsIGlobalObject* global = holder->GetParentObject();
               NS_ENSURE_TRUE_VOID(global);
               if (aValue.IsReject()) {
                 MediaCapabilitiesDecodingInfo info;
                 info.mSupported = false;
                 info.mSmooth = false;
                 info.mPowerEfficient = false;
                 LOG("{} -> {}", aConfiguration, info);
                 promise->MaybeResolve(std::move(info));
                 return;
               }
               bool powerEfficient = true;
               bool smooth = true;
               for (auto&& capability : aValue.ResolveValue()) {
                 smooth &= capability.mSmooth;
                 powerEfficient &= capability.mPowerEfficient;
               }
               MediaCapabilitiesDecodingInfo info;
               info.mSupported = true;
               info.mSmooth = smooth;
               info.mPowerEfficient = powerEfficient;
               LOG("{} -> {}", aConfiguration, info);
               promise->MaybeResolve(std::move(info));
             })
      ->Track(*holder);
}

// static
RefPtr<MediaCapabilities::CapabilitiesPromise>
MediaCapabilities::CheckVideoDecodingInfo(
    RefPtr<TaskQueue> aTaskQueue, RefPtr<layers::KnowsCompositor> aCompositor,
    float aFrameRate, bool aShouldResistFingerprinting,
    UniquePtr<TrackInfo> aConfig) {
  MOZ_ASSERT(aConfig && aConfig->IsVideo());
  MOZ_ASSERT(aTaskQueue);
  RefPtr<nsISerialEventTarget> target = aTaskQueue;
  return InvokeAsync(
      target, __func__,
      [taskQueue = std::move(aTaskQueue), compositor = std::move(aCompositor),
       frameRate = aFrameRate,
       shouldResistFingerprinting = aShouldResistFingerprinting,
       config = std::move(aConfig)]() mutable -> RefPtr<CapabilitiesPromise> {
        // MediaDataDecoder keeps a reference to the config object, so we must
        // keep it alive until the decoder has been shutdown.
        static Atomic<uint32_t> sTrackingIdCounter(0);
        TrackingId trackingId(TrackingId::Source::MediaCapabilities,
                              sTrackingIdCounter++,
                              TrackingId::TrackAcrossProcesses::Yes);
        CreateDecoderParams params{
            *config, compositor, CreateDecoderParams::VideoFrameRate(frameRate),
            TrackInfo::kVideoTrack, Some(std::move(trackingId))};
        // We want to ensure that all decoder's queries are occurring only
        // once at a time as it can quickly exhaust the system resources
        // otherwise.
        static RefPtr<AllocPolicy> sVideoAllocPolicy = [&taskQueue]() {
          SchedulerGroup::Dispatch(NS_NewRunnableFunction(
              "MediaCapabilities::AllocPolicy:Video", []() {
                ClearOnShutdown(&sVideoAllocPolicy,
                                ShutdownPhase::XPCOMShutdownThreads);
              }));
          return new SingleAllocPolicy(TrackInfo::TrackType::kVideoTrack,
                                       taskQueue);
        }();
        return AllocationWrapper::CreateDecoder(params, sVideoAllocPolicy)
            ->Then(
                taskQueue, __func__,
                [taskQueue, shouldResistFingerprinting,
                 config = std::move(config)](
                    AllocationWrapper::AllocateDecoderPromise::
                        ResolveOrRejectValue&& aValue) mutable {
                  if (aValue.IsReject()) {
                    return CapabilitiesPromise::CreateAndReject(
                        std::move(aValue.RejectValue()), __func__);
                  }
                  RefPtr<MediaDataDecoder> decoder =
                      std::move(aValue.ResolveValue());
                  RefPtr<CapabilitiesPromise> p = decoder->Init()->Then(
                      taskQueue, __func__,
                      [taskQueue, decoder, shouldResistFingerprinting,
                       config = std::move(config)](
                          MediaDataDecoder::InitPromise::ResolveOrRejectValue&&
                              aValue) mutable {
                        RefPtr<CapabilitiesPromise> p;
                        if (aValue.IsReject()) {
                          p = CapabilitiesPromise::CreateAndReject(
                              std::move(aValue.RejectValue()), __func__);
                        } else {
                          nsAutoCString reason;
                          bool hwAccel = decoder->IsHardwareAccelerated(reason);
                          auto info = CreateVideoDecodingInfo(
                              *config, shouldResistFingerprinting, hwAccel);
                          p = CapabilitiesPromise::CreateAndResolve(
                              std::move(info), __func__);
                        }
                        MOZ_ASSERT(p.get(), "the promise has been created");
                        // Let's keep alive the decoder and the config object
                        // until the decoder has been shutdown.
                        decoder->Shutdown()->Then(
                            taskQueue, __func__,
                            [taskQueue, decoder, config = std::move(config)](
                                const ShutdownPromise::ResolveOrRejectValue&
                                    aValue) {});
                        return p;
                      });
                  return p;
                });
      });
}

// https://www.w3.org/TR/media-capabilities/#is-encrypted-decode-supported
RefPtr<MediaKeySystemAccessManager::MediaKeySystemAccessPromise>
MediaCapabilities::CheckEncryptedDecodingSupport(
    const MediaDecodingConfiguration& aConfiguration) {
  using MediaKeySystemAccessPromise =
      MediaKeySystemAccessManager::MediaKeySystemAccessPromise;
  auto* window = mParent->GetAsInnerWindow();
  if (NS_WARN_IF(!window)) {
    return MediaKeySystemAccessPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                        __func__);
  }

  auto* manager = window->Navigator()->GetOrCreateMediaKeySystemAccessManager();
  if (NS_WARN_IF(!manager)) {
    return MediaKeySystemAccessPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                        __func__);
  }

  // Let emeConfiguration be a new MediaKeySystemConfiguration, and initialize
  // it as follows
  Sequence<MediaKeySystemConfiguration> configs;
  auto* emeConfig = configs.AppendElement(fallible);
  if (NS_WARN_IF(!emeConfig)) {
    return MediaKeySystemAccessPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                        __func__);
  }

  if (!MediaCapabilitiesKeySystemConfigurationToMediaKeySystemConfiguration(
          aConfiguration, *emeConfig)) {
    return MediaKeySystemAccessPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                        __func__);
  }
  return manager->Request(
      aConfiguration.mKeySystemConfiguration.Value().mKeySystem, configs);
}

// https://w3c.github.io/media-capabilities/#abstract-opdef-create-a-mediacapabilitiesencodinginfo
already_AddRefed<Promise> MediaCapabilities::EncodingInfo(
    const MediaEncodingConfiguration& aConfiguration, ErrorResult& aRv) {
  RefPtr<Promise> encodePromise = Promise::Create(mParent, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  const auto behavior = GetBehaviorConfig(mParent);

  // If WebRTC type is used and WebRTC is not enabled for this origin, reject.
  if (aConfiguration.mType == MediaEncodingType::Webrtc &&
      !behavior.mWebRTCEnabled) {
    encodePromise->MaybeRejectWithTypeError<MSG_INVALID_ENUM_VALUE>(
        "type", "webrtc", "MediaEncodingType");
    return encodePromise.forget();
  }

  // If configuration is not a valid MediaConfiguration, return a Promise
  // rejected with a TypeError.
  if (auto configCheck =
          IsValidMediaEncodingConfiguration(aConfiguration, behavior);
      configCheck.isErr()) {
    RejectWithValidationResult(encodePromise, configCheck.unwrapErr());
    return encodePromise.forget();
  }

  LOG("Processing EncodingInfo for: {}", aConfiguration);

  // Step 1: Let info be a new MediaCapabilitiesEncodingInfo instance.
  // Step 2: Set configuration to be a new MediaEncodingConfiguration.
  // For every property in configuration create a new property with the same
  // name and value in configuration.
  // (Both steps handled when object created during async support check)

  // Step 3: Let videoSupported be unknown.
  CodecSupport videoSupported = CodecSupport::Unknown;
  CodecSupportState state(*this, behavior);

  // Step 4: If video is present in configuration, run the following steps:
  // Step 4.1: Let videoMimeType be the result of running parse a MIME type
  // with configuration's contentType.
  Maybe<MediaExtendedMIMEType> videoMime;
  // Step 4.2: Set videoSupported to the result of running check MIME type
  // support with videoMimeType configuration's type.
  if (aConfiguration.mVideo.WasPassed()) {
    videoMime =
        MakeMediaExtendedMIMEType(aConfiguration.mVideo.Value().mContentType);
    MOZ_ASSERT(videoMime, "Validation already succeeded");
    if (videoMime) {
      videoSupported =
          state.CheckVideoEncodeSupport(aConfiguration, *videoMime);
    }
  }

  // Step 5: Let audioSupported be unknown.
  CodecSupport audioSupported = CodecSupport::Unknown;

  // Step 6: If audio is present in configuration, run the following steps:
  Maybe<MediaExtendedMIMEType> audioMime;
  if (aConfiguration.mAudio.WasPassed()) {
    // Step 6.1: Let audioMimeType be the result of running parse a MIME type
    // with configuration's contentType.
    audioMime =
        MakeMediaExtendedMIMEType(aConfiguration.mAudio.Value().mContentType);
    // Step 6.2: Set audioSupported to the result of running check MIME type
    // support with audioMimeType configuration's type.
    audioSupported =
        audioMime ? state.CheckAudioEncodeSupport(aConfiguration, *audioMime)
                  : CodecSupport::Unknown;
  }

  MediaCapabilitiesInfo info;
  const bool bothSupportUnknown = videoSupported == CodecSupport::Unknown &&
                                  audioSupported == CodecSupport::Unknown;

  // Step 7: If either videoSupported or audioSupported is unsupported, set
  // supported to false, smooth to false, powerEfficient to false, and return
  // info.
  if (videoSupported == CodecSupport::Unsupported ||
      audioSupported == CodecSupport::Unsupported || bothSupportUnknown) {
    info.mSupported = false;
    info.mSmooth = false;
    info.mPowerEfficient = false;
    encodePromise->MaybeResolve(std::move(info));
    return encodePromise.forget();
  }

  // Step 8: Otherwise, set supported to true.
  info.mSupported = true;

  // We defer checking specific encoder support to a background TaskQueue, and
  // continue the algorithm async in the then handler later.
  using PromiseType =
      MozPromise<MediaCapabilitiesInfo, bool, /*IsExclusive=*/true>;
  RefPtr<DOMMozPromiseRequestHolder<PromiseType>> holder;
  RefPtr<nsISerialEventTarget> targetThread;
  RefPtr<StrongWorkerRef> workerRef;
  if (!GetThreadForAsyncRequest<PromiseType>(
          mParent, &holder, &targetThread, &workerRef,
          "MediaCapabilities::EncodingInfo")) {
    // Worker is shutting down. Per spec, leave the promise pending; it will
    // be cleaned up by GC when the worker is torn down.
    return encodePromise.forget();
  }

  RefPtr<TaskQueue> taskQueue =
      TaskQueue::Create(GetMediaThreadPool(MediaThreadType::PLATFORM_ENCODER),
                        "MediaCapabilities::TaskQueue");
  InvokeAsync(taskQueue, __func__,
              [aConfiguration, videoMime, videoSupported, audioMime,
               audioSupported, info = std::move(info)]() mutable {
                // Step 7 returns early if neither audio nor video are
                // supported. If video isn't supported, audio must be - they
                // can't both be unknown. We can assume audio playback, which
                // should be smooth and powerEfficient.
                MOZ_ASSERT(audioSupported == CodecSupport::Supported ||
                           videoSupported == CodecSupport::Supported);
                (void)audioSupported;
                info.mSmooth = true;
                info.mPowerEfficient = true;

                bool lowResolution = false;
                if (videoSupported == CodecSupport::Supported) {
                  // TODO(Bug 2024767): Handle fmtp parameters
                  const media::EncodeSupportSet videoEncoderSupport =
                      SupportsVideoMimeEncodeForWebrtc(*videoMime);
                  MOZ_ASSERT(!videoEncoderSupport.isEmpty());
                  const bool hwSupported = videoEncoderSupport.contains(
                      media::EncodeSupport::HardwareEncode);
                  MOZ_ASSERT(aConfiguration.mVideo.WasPassed());
                  const auto& v = aConfiguration.mVideo.Value();
                  const CheckedInt<uint32_t> pixels =
                      CheckedInt<uint32_t>(v.mWidth) *
                      CheckedInt<uint32_t>(v.mHeight);
                  lowResolution = pixels.isValid() &&
                                  pixels.value() <= kLowResolutionPixelCount;

                  // Step 9: If the user agent is able to encode the media
                  // represented by configuration at the indicated framerate,
                  // set smooth to true. Otherwise set it to false.
                  //
                  // NOTE: The spec doesn't give hard guidelines for smooth.
                  // We will hardware encode or low resolution encoding counts
                  // as "smooth". For the highest accuracy we'd want to use
                  // benchmarking code similar to what we had in the tree
                  // earlier for decoding which was removed due to maintenance
                  // concerns.
                  info.mSmooth &= hwSupported || IsWebRTCSWEncodeSmooth(v);

                  // Step 10: If the user agent is able to encode the media
                  // represented by configuration in a power efficient manner,
                  // set powerEfficient to true. Otherwise set it to false.
                  //
                  // Encoding or decoding is considered power efficient when the
                  // power draw is optimal. The definition of optimal power draw
                  // for encoding or decoding is left to the user agent.
                  // However, a common implementation strategy is to consider
                  // hardware usage as indicative of optimal power draw. User
                  // agents SHOULD NOT mark hardware encoding or decoding as
                  // power efficient by default, as non-hardware-accelerated
                  // codecs can be just as efficient, particularly with
                  // low-resolution video. User agents SHOULD NOT take the
                  // device's power source into consideration when determining
                  // encoding power efficiency unless the device's power source
                  // has side effects such as enabling different encoding or
                  // decoding modules.
                  info.mPowerEfficient &= (hwSupported || lowResolution);
                }

                LOG("{} -> {}", aConfiguration, info);

                return PromiseType::CreateAndResolve(
                    std::move(info), "MediaCapabilities::EncodingInfo");
              })
      ->Then(
          targetThread, __func__,
          [encodePromise, workerRef, holder,
           aConfiguration](MediaCapabilitiesInfo aInfo) {
            holder->Complete();
            nsIGlobalObject* global = holder->GetParentObject();
            NS_ENSURE_TRUE_VOID(global);
            // Step 11: Return info.
            encodePromise->MaybeResolve(std::move(aInfo));
          },
          [] { MOZ_CRASH("Unexpected"); })
      ->Track(*holder);
  return encodePromise.forget();
}

bool MediaCapabilities::CheckTypeForMediaSource(
    const MediaExtendedMIMEType& aType) const {
  IgnoredErrorResult rv;
  MediaSource::IsTypeSupported(
      NS_ConvertUTF8toUTF16(aType.OriginalString()),
      nullptr /* DecoderDoctorDiagnostics */, rv,
      Some(mParent->ShouldResistFingerprinting(RFPTarget::MediaCapabilities)));

  return !rv.Failed();
}

bool MediaCapabilities::CheckTypeForFile(
    const MediaExtendedMIMEType& aType) const {
  MediaContainerType containerType(aType);

  return DecoderTraits::CanHandleContainerType(
             containerType, nullptr /* DecoderDoctorDiagnostics */) !=
         CANPLAY_NO;
}

bool MediaCapabilities::CheckTypeForEncoder(
    const MediaExtendedMIMEType& aType) const {
  return MediaRecorder::IsTypeSupported(
      NS_ConvertUTF8toUTF16(aType.OriginalString()));
}

already_AddRefed<layers::KnowsCompositor> MediaCapabilities::GetCompositor() {
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(GetParentObject());
  if (NS_WARN_IF(!window)) {
    return nullptr;
  }

  nsCOMPtr<Document> doc = window->GetExtantDoc();
  if (NS_WARN_IF(!doc)) {
    return nullptr;
  }
  WindowRenderer* renderer = nsContentUtils::WindowRendererForDocument(doc);
  if (NS_WARN_IF(!renderer)) {
    return nullptr;
  }
  RefPtr<layers::KnowsCompositor> knows = renderer->AsKnowsCompositor();
  if (NS_WARN_IF(!knows)) {
    return nullptr;
  }
  return knows->GetForMedia().forget();
}

JSObject* MediaCapabilities::WrapObject(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return MediaCapabilities_Binding::Wrap(aCx, this, aGivenProto);
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MediaCapabilities)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(MediaCapabilities)
NS_IMPL_CYCLE_COLLECTING_RELEASE(MediaCapabilities)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(MediaCapabilities, mParent)

}  // namespace mozilla::dom
#undef LOG
