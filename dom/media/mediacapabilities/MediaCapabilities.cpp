/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaCapabilities.h"

#include <type_traits>
#include <utility>

#include "AOMDecoder.h"
#include "AllocationPolicy.h"
#include "DecoderTraits.h"
#include "H265.h"
#include "MP4Decoder.h"
#include "MediaCapabilitiesValidation.h"
#include "MediaInfo.h"
#include "MediaRecorder.h"
#include "PDMFactorySupport.h"
#include "VPXDecoder.h"
#include "VideoUtils.h"
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
#include "mozilla/media/webrtc/AV1FmtpParser.h"
#include "mozilla/media/webrtc/CodecInfo.h"
#include "mozilla/media/webrtc/H264FmtpParser.h"
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

static gfx::IntSize ClampedIntSize(uint32_t aWidth, uint32_t aHeight) {
  return gfx::IntSize(
      static_cast<int32_t>(std::min<uint32_t>(aWidth, INT32_MAX)),
      static_cast<int32_t>(std::min<uint32_t>(aHeight, INT32_MAX)));
}

static bool IsLowResolution(const VideoConfiguration& aConfig) {
  const CheckedInt<uint32_t> pixels =
      CheckedInt<uint32_t>(aConfig.mWidth) * aConfig.mHeight;
  return pixels.isValid() && pixels.value() <= kLowResolutionPixelCount;
}

static CodecType WebrtcMimeToCodecType(const MediaExtendedMIMEType& aMime) {
  const nsCString& mime = aMime.Type().AsString();
  if (mime.EqualsLiteral("video/h264")) {
    return CodecType::H264;
  }
  if (mime.EqualsLiteral("video/vp8")) {
    return CodecType::VP8;
  }
  if (mime.EqualsLiteral("video/vp9")) {
    return CodecType::VP9;
  }
  if (mime.EqualsLiteral("video/av1")) {
    return CodecType::AV1;
  }
  return CodecType::Unknown;
}

template <typename InfoType>
static InfoType UnsupportedInfo() {
  InfoType info;
  info.mSupported = false;
  info.mSmooth = false;
  info.mPowerEfficient = false;
  return info;
}

// Only for WithAllocPolicy. The process-wide SingleAllocPolicy that strict
// video decoder probes are serialized through, and the queue that owns it. A
// function-local static in WithAllocPolicy would be one per template
// instantiation, so it lives here.
struct ProbeAllocPolicy {
  const RefPtr<TaskQueue> mQueue;  // mPolicy->Cancel() must run here.
  const RefPtr<SingleAllocPolicy> mPolicy;
};

// Creates the policy on first use and clears it at XPCOMShutdownThreads.
static ProbeAllocPolicy* GetProbeAllocPolicy() {
  static UniquePtr<ProbeAllocPolicy> sPolicy = []() {
    SchedulerGroup::Dispatch(
        NS_NewRunnableFunction("GetProbeAllocPolicy", []() {
          ClearOnShutdown(&sPolicy, ShutdownPhase::XPCOMShutdownThreads);
        }));
    RefPtr<TaskQueue> queue =
        TaskQueue::Create(GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER),
                          "MediaCapabilities::ProbeAllocPolicy");
    return std::make_unique<ProbeAllocPolicy>(
        queue, MakeRefPtr<SingleAllocPolicy>(TrackInfo::TrackType::kVideoTrack,
                                             queue));
  }();
  return sPolicy.get();
}

// Runs aFunction(AllocPolicy*) with the shared policy while holding a
// ShutdownBlockingTicket, so shutdown does not reach XPCOMShutdownThreads,
// where GlobalAllocPolicy is cleared, until the returned promise has settled.
// Resolves an empty support set instead if shutdown has begun, and cancels the
// allocations still queued when shutdown starts.
template <typename Function,
          typename PromiseType = typename std::invoke_result_t<
              Function, AllocPolicy*>::element_type>
static RefPtr<PromiseType> WithAllocPolicy(const nsAString& aName,
                                           Function&& aFunction) {
  auto ticket = media::ShutdownBlockingTicket::Create(
      aName, NS_LITERAL_STRING_FROM_CSTRING(__FILE__), __LINE__);
  if (!ticket) {
    LOG("Shutting down, not probing codec support for {}",
        NS_ConvertUTF16toUTF8(aName).get());
    typename PromiseType::ResolveValueType unsupported;
    return PromiseType::CreateAndResolve(unsupported, __func__);
  }
  ProbeAllocPolicy* policy = GetProbeAllocPolicy();
  ticket->ShutdownPromise()->Then(
      policy->mQueue, __func__,
      [policy = policy->mPolicy]() { policy->Cancel(); }, []() {});
  return aFunction(policy->mPolicy.get())
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [ticket = std::move(ticket)](
                 typename PromiseType::ResolveOrRejectValue&& aValue) {
               return PromiseType::CreateAndResolveOrReject(std::move(aValue),
                                                            __func__);
             });
}

// Whether aMime signals AV1 tier=1 with an explicit level below 4.0
// (level-idx 8), for which Annex A.3 defines no high tier parameters.
// Present-but-invalid parameters have already been rejected by
// WebrtcCodecInfo during the support check, so only well-formed or absent
// ones reach here. With an absent level-idx the level is inferred from the
// requested resolution, which is only a lower bound, so tier can't be judged.
static bool WebrtcVideoTierUndefined(const MediaExtendedMIMEType& aMime) {
  if (WebrtcMimeToCodecType(aMime) != CodecType::AV1) {
    return false;
  }
  const auto fmtp = ParseAV1Fmtp(aMime.OriginalString());
  MOZ_ASSERT(!fmtp.HasInvalidParam());
  return fmtp.mTier.isOk() && fmtp.mTier.inspect() == 1 &&
         fmtp.mLevelIdx.isOk() && fmtp.mLevelIdx.inspect() < 8;
}

// Whether aVideo's resolution and framerate exceed the caps of the H264/AV1
// level signaled in aMime's fmtp parameters. Present-but-invalid parameters
// have already been rejected by WebrtcCodecInfo during the support check, so
// only well-formed or absent ones reach here. An absent level is deliberately
// not defaulted (to level 1.0 for H264, 3.1 for AV1, per their RTP payload
// specs): the resolution and framerate are given explicitly, so the level is
// inferred from them instead.
static bool WebrtcVideoExceedsLevel(const MediaExtendedMIMEType& aMime,
                                    const VideoConfiguration& aVideo) {
  const auto framerate = static_cast<double>(aVideo.mFramerate);
  switch (WebrtcMimeToCodecType(aMime)) {
    case CodecType::H264: {
      const auto fmtp = ParseH264Fmtp(aMime.OriginalString());
      MOZ_ASSERT(!fmtp.HasInvalidParam());
      return fmtp.mProfileLevel.isOk() &&
             !H264LevelFits(fmtp.mProfileLevel.inspect().mLevel, aVideo.mWidth,
                            aVideo.mHeight, framerate);
    }
    case CodecType::AV1: {
      const auto fmtp = ParseAV1Fmtp(aMime.OriginalString());
      MOZ_ASSERT(!fmtp.HasInvalidParam());
      return fmtp.mLevelIdx.isOk() &&
             !AV1LevelFits(fmtp.mLevelIdx.inspect(), aVideo.mWidth,
                           aVideo.mHeight, framerate);
    }
    default:
      return false;
  }
}

// Returns an EncoderConfig for use with PEMFactory::Supports.
static EncoderConfig BuildEncoderConfig(const MediaExtendedMIMEType& aMime,
                                        const VideoConfiguration& aConfig) {
  const auto codec = WebrtcMimeToCodecType(aMime);
  MOZ_ASSERT(codec != CodecType::Unknown);
  const gfx::IntSize size = ClampedIntSize(aConfig.mWidth, aConfig.mHeight);
  MOZ_ASSERT(size.width > 0 && size.height > 0);

  EncoderConfig::CodecSpecific specific(void_t{});
  if (codec == CodecType::H264) {
    // Default to Baseline / level 3.1 (the closest we can get to WebRTC's
    // default 0x42e01f Constrained Baseline; bug 2040726).

    const auto fmtp = ParseH264Fmtp(aMime.OriginalString());
    const H264ProfileLevel pl =
        fmtp.mProfileLevel.isOk()
            ? fmtp.mProfileLevel.inspect()
            : H264ProfileLevel{H264_PROFILE::H264_PROFILE_BASE,
                               H264_LEVEL::H264_LEVEL_3_1};
    specific = AsVariant(
        H264Specific(pl.mProfile, pl.mLevel, H264BitStreamFormat::ANNEXB));
  }
  const float framerate = static_cast<float>(aConfig.mFramerate);
  const uint32_t fr =
      framerate > 1.0f ? SaturatingCast<uint32_t>(std::ceil(framerate)) : 1;
  const uint32_t bitrate = SaturatingCast<uint32_t>(aConfig.mBitrate);
  // PEMFactory::Supports() does not check bitrate, but we include it here
  // for future use.
  return EncoderConfig(
      codec, size, Usage::Realtime,
      EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P), fr,
      /* kf interval*/ 0, bitrate, /* br min */ 0, /* br max */ 0,
      mozilla::BitrateMode::Variable, HardwarePreference::None,
      ScalabilityMode::None, specific);
}

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
static bool IsSWEncodeSmooth(const CodecType aCodec,
                             const VideoConfiguration& aConfig) {
  // ratio = clip_duration / wall_time at 60fps: >1.0 means faster than
  // real-time. threads = libwebrtc thread count used during measurement. For
  // non-standard resolutions: rounds up to nearest standard bucket
  // Approximates scaling linearly by framerate and thread count.
  static const struct {
    CodecType codec;
    uint32_t w, h;
    float ratio;  // realtime ratio at 60fps; >1.0 = faster than real-time
    uint32_t threads;
  } kMeasured[] = {
      {CodecType::H264, 426, 240, 2.06f, 1},
      {CodecType::H264, 854, 480, 1.71f, 1},
      {CodecType::H264, 1280, 720, 1.51f, 1},
      {CodecType::H264, 1920, 1080, 1.37f, 1},
      {CodecType::H264, 3840, 2160, 0.47f, 1},  // not smooth at 60fps
      {CodecType::AV1, 426, 240, 2.10f, 2},
      {CodecType::AV1, 854, 480, 1.43f, 4},
      {CodecType::AV1, 1280, 720, 0.98f, 4},   // not smooth at 60fps
      {CodecType::AV1, 1920, 1080, 0.73f, 4},  // not smooth at 60fps
      {CodecType::AV1, 3840, 2160, 0.26f, 4},  // not smooth at 60fps
      {CodecType::VP9, 426, 240, 1.94f, 1},
      {CodecType::VP9, 854, 480, 1.85f, 2},
      {CodecType::VP9, 1280, 720, 1.64f, 4},
      {CodecType::VP9, 1920, 1080, 1.20f, 4},
      {CodecType::VP9, 3840, 2160, 0.50f, 4},  // not smooth at 60fps
      {CodecType::VP8, 426, 240, 2.01f, 1},
      {CodecType::VP8, 854, 480, 1.80f, 3},
      {CodecType::VP8, 1280, 720, 1.54f, 3},
      {CodecType::VP8, 1920, 1080, 1.31f, 3},
      {CodecType::VP8, 3840, 2160, 0.55f, 3},  // not smooth at 60fps
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

  // Single-threaded default. OpenH264 media.gmp.encoder.multithreaded exists,
  // isn't hooked up to WebRTC and is buggy, at least for decode. Bug 2065570.
  uint32_t actualThreads = 1;
  if (aCodec == CodecType::AV1) {
    actualThreads = Av1EncoderThreads(pixels, cores);
  } else if (aCodec == CodecType::VP9) {
    actualThreads = Vp9EncoderThreads(pixels, cores);
  } else if (aCodec == CodecType::VP8) {
    actualThreads = Vp8EncoderThreads(pixels, cores);
  }

  // kMeasured is sorted ascending by resolution within each codec.
  // First entry with bucket_pixels >= pixels is the smallest valid bucket
  // (rounds up, conservative).
  int32_t bucketIdx = -1;
  for (int32_t i = 0; i < static_cast<int32_t>(std::size(kMeasured)); i++) {
    if (aCodec != kMeasured[i].codec) {
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

static bool IsWebRTCSWEncodeSmooth(const CodecType aCodec,
                                   const VideoConfiguration& aConfig) {
  const auto shouldForceSmooth =
      StaticPrefs::media_mediacapabilities_webrtc_encode_smooth_override();
  if (shouldForceSmooth == 1) {
    return true;
  } else if (shouldForceSmooth == 2) {
    return false;
  }
  return IsSWEncodeSmooth(aCodec, aConfig);
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
       audioContainer = std::move(aAudioContainer)]() -> RefPtr<PromiseType> {
        MOZ_ASSERT(videoContainer || audioContainer);

        // Step 7 returns early if neither audio nor video are supported.
        // If video isn't supported, audio must be - they can't both be
        // unknown. We can assume audio decoding and playback, which should be
        // smooth and powerEfficient.
        MediaCapabilitiesDecodingInfo info;
        info.mSupported = true;  // Passed previous support check
        info.mSmooth = true;
        info.mPowerEfficient = true;

        if (!videoContainer) {
          return PromiseType::CreateAndResolve(
              std::move(info), "MediaCapabilities::CreateWebRTCDecodingInfo");
        }

        const auto& v = aConfiguration.mVideo.Value();
        const auto& mime = videoContainer->ExtendedType();
        if (WebrtcVideoTierUndefined(mime) ||
            WebrtcVideoExceedsLevel(mime, v)) {
          auto unsupported = UnsupportedInfo<MediaCapabilitiesDecodingInfo>();
          LOG("{} -> {}", aConfiguration, unsupported);
          return PromiseType::CreateAndResolve(
              std::move(unsupported), "MediaCapabilities::DecodingInfo");
        }
        const CheckedInt<uint32_t> pixels =
            CheckedInt<uint32_t>(v.mWidth) * CheckedInt<uint32_t>(v.mHeight);
        const bool lowResolution =
            pixels.isValid() && pixels.value() <= kLowResolutionPixelCount;
        // Normalize for PDMs that expect "video/avc"
        nsCString trackMime(videoContainer->Type().AsString());
        if (trackMime.LowerCaseEqualsLiteral("video/h264")) {
          trackMime.AssignLiteral("video/avc");
        }
        auto trackInfo =
            CreateTrackInfoWithMIMETypeAndContainerTypeExtraParameters(
                trackMime, *videoContainer);
        if (!trackInfo) {
          auto unsupported = UnsupportedInfo<MediaCapabilitiesDecodingInfo>();
          LOG("{} -> {}", aConfiguration, unsupported);
          return PromiseType::CreateAndResolve(
              std::move(unsupported), "MediaCapabilities::DecodingInfo");
        }
        SupportDecoderParams videoParameters(
            *trackInfo,
            media::VideoFrameRate(static_cast<float>(v.mFramerate)));
        return SupportsVideoDecodeForWebrtc(mime, videoParameters)
            ->Then(GetCurrentSerialEventTarget(), __func__,
                   [aConfiguration, info, lowResolution](
                       PDMSupportsDecoderPromise::ResolveOrRejectValue&&
                           aValue) mutable -> RefPtr<PromiseType> {
                     // Treat an internal failure as unsupported.
                     if (aValue.IsReject() || aValue.ResolveValue().isEmpty()) {
                       auto unsupported =
                           UnsupportedInfo<MediaCapabilitiesDecodingInfo>();
                       LOG("{} -> {}", aConfiguration, unsupported);
                       return PromiseType::CreateAndResolve(
                           std::move(unsupported),
                           "MediaCapabilities::CreateWebRTCDecodingInfo");
                     }
                     const bool hwSupported = aValue.ResolveValue().contains(
                         media::DecodeSupport::HardwareDecode);
                     info.mPowerEfficient = hwSupported || lowResolution;
                     return PromiseType::CreateAndResolve(
                         std::move(info),
                         "MediaCapabilities::CreateWebRTCDecodingInfo");
                   });
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
    const gfx::IntSize& aResolution, const bool aShouldResistFingerprinting,
    const bool aHardwareAccelerated) {
  MediaCapabilitiesDecodingInfo info;
  info.mSupported = true;
  info.mSmooth = true;
  info.mPowerEfficient = false;
  if (aShouldResistFingerprinting) {
    return info;
  }
  // mImage dimensions are int32_t from gfx::IntSize. CheckedInt rejects
  // negative inputs (mapping to !isValid()) and rejects width*height
  // overflow, in either case treating the result as not-low-resolution.
  const CheckedInt<uint32_t> pixels = CheckedInt<uint32_t>(aResolution.width) *
                                      CheckedInt<uint32_t>(aResolution.height);
  const bool lowResolution =
      pixels.isValid() && pixels.value() <= kLowResolutionPixelCount;
  info.mPowerEfficient = aHardwareAccelerated || lowResolution;
  return info;
}

// Lowest AV1 seq_level_idx whose MaxPicSize fits aImage. Falls back to
// "maximum parameters" when no defined level fits or the size overflows.
static uint8_t AV1SeqLevelIdxForImage(const gfx::IntSize& aImage) {
  // 31 imposes no level constraints, for sizes larger than any defined level.
  constexpr uint8_t kMaxParameters = 31;
  const CheckedInt<uint32_t> pixels =
      CheckedInt<uint32_t>(aImage.width) * CheckedInt<uint32_t>(aImage.height);
  if (!pixels.isValid()) {
    return kMaxParameters;
  }
  struct Level {
    uint32_t mMaxPicSize;
    uint8_t mSeqLevelIdx;
  };
  // Rows are levels 2.0, 2.1, 3.0, 3.1, 4.0, 5.0, 6.0.
  static constexpr Level kLevels[] = {
      {147456, 0},  {278784, 1},   {665856, 4},   {1065024, 5},
      {2359296, 8}, {8912896, 12}, {35651584, 16}};
  for (const auto& level : kLevels) {
    if (pixels.value() <= level.mMaxPicSize) {
      return level.mSeqLevelIdx;
    }
  }
  return kMaxParameters;
}

// Derive the decoder configuration a codec string leaves out: a common default
// for VP8, VP9 and AV1, and for HEVC the profile, tier and level that the
// string does carry.
static void MaybeSynthesizeCodecConfig(VideoInfo* aInfo,
                                       const MediaContainerType& aContainer) {
  if (aInfo->mExtraData && !aInfo->mExtraData->IsEmpty()) {
    return;
  }
  if (VPXDecoder::IsVPX(aInfo->mMimeType)) {
    // The string carries no profile, so assume the common VP8/VP9 case.
    VPXDecoder::VPXStreamInfo info;
    info.mProfile = 0;
    info.mBitDepth = 8;
    RefPtr<MediaByteBuffer> extraData = new MediaByteBuffer();
    VPXDecoder::GetVPCCBox(extraData, info);
    aInfo->mExtraData = std::move(extraData);
    LOG("Synthesized default config for profile-less {}",
        aInfo->mMimeType.get());
  } else if (AOMDecoder::IsAV1(aInfo->mMimeType)) {
    // The string carries no profile, so assume the common AV1 case.
    AOMDecoder::AV1SequenceInfo info;
    info.mImage = aInfo->mImage;
    info.mProfile = 0;
    info.mBitDepth = 8;
    AOMDecoder::OperatingPoint op;
    op.mLevel = AV1SeqLevelIdxForImage(aInfo->mImage);
    info.mOperatingPoints.AppendElement(op);
    RefPtr<MediaByteBuffer> extraData = new MediaByteBuffer();
    bool hasSeqHdr = false;
    AOMDecoder::WriteAV1CBox(info, extraData, hasSeqHdr);
    aInfo->mExtraData = std::move(extraData);
    LOG("Synthesized default config for profile-less {} (seq_level_idx={})",
        aInfo->mMimeType.get(), static_cast<unsigned>(op.mLevel));
  } else if (MP4Decoder::IsHEVC(aInfo->mMimeType)) {
    // The track exists only because one of the codecs parsed as HEVC, so the
    // loop always finds it.
    for (const auto& codec : aContainer.ExtendedType().Codecs().Range()) {
      H265CodecDetails details;
      if (!ExtractH265CodecDetails(codec, &details)) {
        continue;
      }
      aInfo->mExtraData = H265::CreateExtraData(details, aInfo->mImage);
      LOG("Synthesized config for {} (profile_idc={}, level_idc={})",
          aInfo->mMimeType.get(),
          static_cast<unsigned>(details.general_profile_idc),
          static_cast<unsigned>(details.general_level_idc));
      break;
    }
  }
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
    MaybeSynthesizeCodecConfig(videoTracks[0]->GetAsVideoInfo(),
                               *aVideoContainer);
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

  // PDMFactory::Supports must be called off-main-thread for accurate results.
  RefPtr<TaskQueue> taskQueue =
      TaskQueue::Create(GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER),
                        "MediaCapabilities::TaskQueue");
  // Only needed by the decoder-creation fallback in CheckVideoDecodingInfo
  // (when media.mediacapabilities.codec-support-cache.enabled is false), but it
  // must be obtained here on the main thread.
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
              drmInfo.mPowerEfficient = false;
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
      promises.AppendElement(InvokeAsync(
          taskQueue, __func__,
          [config = std::move(config)]() -> RefPtr<CapabilitiesPromise> {
            SupportDecoderParams params{*config};
            // There's no need to create an audio decoder as we only want to
            // know if such codec is supported. We do need to call the
            // PDMFactory::Supports API outside the main thread to get accurate
            // results.
            RefPtr<PDMSupportsDecoderPromise> promise =
                StaticPrefs::
                        media_mediacapabilities_codec_support_cache_enabled()
                    ? PDMFactorySupport::IsSupportedAsync(params)
                    : PDMSupportsDecoderPromise::CreateAndResolve(
                          PDMFactorySupport::IsSupported(
                              params, nullptr /* decoder doctor */),
                          __func__);
            return promise->Then(
                GetCurrentSerialEventTarget(), __func__,
                [](media::DecodeSupportSet aSupport) {
                  if (aSupport.isEmpty()) {
                    return CapabilitiesPromise::CreateAndReject(
                        NS_ERROR_FAILURE, __func__);
                  }
                  MediaCapabilitiesDecodingInfo info;
                  info.mSupported = true;
                  info.mSmooth = true;
                  info.mPowerEfficient = true;
                  return CapabilitiesPromise::CreateAndResolve(std::move(info),
                                                               __func__);
                },
                [](nsresult) -> RefPtr<CapabilitiesPromise> {
                  return CapabilitiesPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                              __func__);
                });
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
  const bool checkDecoderInstance =
      !StaticPrefs::media_mediacapabilities_codec_support_cache_enabled();
  RefPtr<nsISerialEventTarget> target = aTaskQueue;
  return InvokeAsync(
      target, __func__,
      [taskQueue = std::move(aTaskQueue), compositor = std::move(aCompositor),
       frameRate = aFrameRate,
       shouldResistFingerprinting = aShouldResistFingerprinting,
       config = std::move(aConfig),
       checkDecoderInstance]() mutable -> RefPtr<CapabilitiesPromise> {
        // Query the cached codec-support snapshot, waiting asynchronously for
        // remote processes to report accurate hardware support. No decoder is
        // created.
        const nsCString type = config->mMimeType;
        const gfx::IntSize resolution = config->GetAsVideoInfo()->mImage;
        SupportDecoderParams params{*config, media::VideoFrameRate(frameRate)};
        RefPtr<PDMSupportsDecoderPromise> supportsPromise =
            PDMFactorySupport::IsSupportedAsync(params);

        if (checkDecoderInstance) {
          // Strict path: create a decoder and query it directly for hardware
          // acceleration. MediaDataDecoder keeps a reference to the
          // config object, so we must keep it alive until the decoder has
          // been shutdown.
          LOG("Using strict decoder probing for codec mime type '{}'", type);
          supportsPromise = supportsPromise->Then(
              GetCurrentSerialEventTarget(),
              "MediaCapabilities::CheckVideoDecodingInfo",
              [taskQueue = std::move(taskQueue),
               compositor = std::move(compositor), config = std::move(config),
               frameRate](
                  PDMSupportsDecoderPromise::ResolveOrRejectValue&&) mutable
                  -> RefPtr<PDMSupportsDecoderPromise> {
                static Atomic<uint32_t> sTrackingIdCounter(0);
                TrackingId trackingId(TrackingId::Source::MediaCapabilities,
                                      sTrackingIdCounter++,
                                      TrackingId::TrackAcrossProcesses::Yes);
                const CreateDecoderParams::OptionSet options{
                    CreateDecoderParams::Option::Default,
                    CreateDecoderParams::Option::AssertCanInstantiate};
                CreateDecoderParams params{
                    *config,
                    compositor,
                    CreateDecoderParams::VideoFrameRate(frameRate),
                    TrackInfo::kVideoTrack,
                    Some(std::move(trackingId)),
                    options};
                // We want to ensure that all decoder's queries are occurring
                // only once at a time as it can quickly exhaust the system
                // resources otherwise.
                return WithAllocPolicy(
                    u"MediaCapabilities::CheckVideoDecodingInfo"_ns,
                    [&params](AllocPolicy* aPolicy) {
                      return PDMFactory::StrictSupportsAsync(params, aPolicy);
                    });
              });
        }

        return supportsPromise->Then(
            GetCurrentSerialEventTarget(), __func__,
            [type, resolution, shouldResistFingerprinting,
             checkDecoderInstance](
                PDMSupportsDecoderPromise::ResolveOrRejectValue&&
                    aValue) mutable -> RefPtr<CapabilitiesPromise> {
              if (aValue.IsReject()) {
                LOG("Decoder support {} request for codec mime type '{}' "
                    "rejected with {}",
                    checkDecoderInstance ? "strict" : "cache", type,
                    aValue.RejectValue());
                return CapabilitiesPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                            __func__);
              }

              media::DecodeSupportSet support = aValue.ResolveValue();
              LOG("Decoder support {} request for codec mime type '{}' "
                  "resolved with sw={}, hw={}",
                  checkDecoderInstance ? "strict" : "cache", type,
                  support.contains(media::DecodeSupport::SoftwareDecode),
                  support.contains(media::DecodeSupport::HardwareDecode));
              if (support.isEmpty()) {
                return CapabilitiesPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                            __func__);
              }
              bool hwAccel =
                  support.contains(media::DecodeSupport::HardwareDecode);
              return CapabilitiesPromise::CreateAndResolve(
                  CreateVideoDecodingInfo(resolution,
                                          shouldResistFingerprinting, hwAccel),
                  __func__);
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

// Steps 8 through 11 for the "record" type. MediaRecorder encodes video in
// software, so there is no encoder factory to query. aVideoMime is set only
// for a video stream that was found supported.
static void CreateRecordEncodingInfo(
    const MediaEncodingConfiguration& aConfiguration, Promise* aPromise,
    const Maybe<MediaExtendedMIMEType>& aVideoMime) {
  // Step 8: Set supported to true.
  MediaCapabilitiesInfo info;
  info.mSupported = true;

  if (aVideoMime) {
    MOZ_ASSERT(aConfiguration.mVideo.WasPassed());
    const auto& v = aConfiguration.mVideo.Value();
    // Steps 9 and 10, video. CanRecordVideoTrackWith accepts only VP8.
    info.mSmooth = IsSWEncodeSmooth(CodecType::VP8, v);
    info.mPowerEfficient = IsLowResolution(v);
  } else {
    // Steps 9 and 10, audio. Audio encode is always smooth and power efficient.
    info.mSmooth = true;
    info.mPowerEfficient = true;
  }

  // Step 11: Return info.
  LOG("{} -> {}", aConfiguration, info);
  aPromise->MaybeResolve(std::move(info));
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
  MOZ_ASSERT(videoSupported == CodecSupport::Supported ||
             audioSupported == CodecSupport::Supported);

  // Steps 8 through 11 are type specific: WebRTC queries the encoder
  // factories, MediaRecorder does not.
  switch (aConfiguration.mType) {
    case MediaEncodingType::Record:
      CreateRecordEncodingInfo(aConfiguration, encodePromise, videoMime);
      return encodePromise.forget();
    case MediaEncodingType::Webrtc:
      CreateWebRTCEncodingInfo(aConfiguration, encodePromise, videoMime);
      return encodePromise.forget();
  }
  MOZ_ASSERT_UNREACHABLE("Unhandled MediaEncodingType");
  info.mSupported = false;
  info.mSmooth = false;
  info.mPowerEfficient = false;
  encodePromise->MaybeResolve(std::move(info));
  return encodePromise.forget();
}

// Steps 8 through 11 for the "webrtc" type.
void MediaCapabilities::CreateWebRTCEncodingInfo(
    const MediaEncodingConfiguration& aConfiguration, Promise* aPromise,
    const Maybe<MediaExtendedMIMEType>& aVideoMime) {
  // Step 8: Set supported to true.
  MediaCapabilitiesInfo info;
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
    return;
  }

  RefPtr<TaskQueue> taskQueue =
      TaskQueue::Create(GetMediaThreadPool(MediaThreadType::PLATFORM_ENCODER),
                        "MediaCapabilities::TaskQueue");
  InvokeAsync(
      taskQueue, __func__,
      [aConfiguration, videoMime = aVideoMime,
       info = std::move(info)]() mutable -> RefPtr<PromiseType> {
        // Step 7 returns early if neither audio nor video are
        // supported. If video isn't supported, audio must be - they
        // can't both be unknown. We can assume audio encoding, which
        // should be smooth and powerEfficient.
        info.mSmooth = true;
        info.mPowerEfficient = true;

        if (!videoMime) {
          LOG("{} -> {}", aConfiguration, info);
          return PromiseType::CreateAndResolve(
              std::move(info), "MediaCapabilities::EncodingInfo");
        }

        MOZ_ASSERT(aConfiguration.mVideo.WasPassed());
        const auto& v = aConfiguration.mVideo.Value();
        // Unlike DecodingInfo, a resolution/framerate that exceeds the
        // negotiated level isn't rejected here: the encoder downscales
        // and decimates to fit the level instead (see bug 2013936). An
        // undefined tier can't be fixed that way though.
        if (WebrtcVideoTierUndefined(*videoMime)) {
          auto unsupported = UnsupportedInfo<MediaCapabilitiesInfo>();
          LOG("{} -> {}", aConfiguration, unsupported);
          return PromiseType::CreateAndResolve(
              std::move(unsupported), "MediaCapabilities::EncodingInfo");
        }
        const CodecType codec = WebrtcMimeToCodecType(*videoMime);
        auto encoderConfig = BuildEncoderConfig(*videoMime, v);
        return SupportsVideoEncodeForWebrtc(encoderConfig)
            ->Then(
                GetCurrentSerialEventTarget(), __func__,
                [aConfiguration, codec,
                 info](media::EncodeSupportSet aVideoSupport) mutable
                    -> RefPtr<PromiseType> {
                  if (aVideoSupport.isEmpty()) {
                    auto unsupported = UnsupportedInfo<MediaCapabilitiesInfo>();
                    LOG("{} -> {}", aConfiguration, unsupported);
                    return PromiseType::CreateAndResolve(
                        std::move(unsupported),
                        "MediaCapabilities::EncodingInfo");
                  }
                  const auto& v = aConfiguration.mVideo.Value();
                  const bool hwSupported = aVideoSupport.contains(
                      media::EncodeSupport::HardwareEncode);

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
                  info.mSmooth &=
                      hwSupported || IsWebRTCSWEncodeSmooth(codec, v);

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
                  info.mPowerEfficient &= (hwSupported || IsLowResolution(v));

                  LOG("{} -> {}", aConfiguration, info);
                  return PromiseType::CreateAndResolve(
                      std::move(info), "MediaCapabilities::EncodingInfo");
                },
                [](nsresult) -> RefPtr<PromiseType> {
                  // Treat an internal failure as unsupported.
                  auto unsupported = UnsupportedInfo<MediaCapabilitiesInfo>();
                  return PromiseType::CreateAndResolve(
                      std::move(unsupported),
                      "MediaCapabilities::EncodingInfo");
                });
      })
      ->Then(
          targetThread, __func__,
          [encodePromise = RefPtr(aPromise), workerRef,
           holder](MediaCapabilitiesInfo aInfo) {
            holder->Complete();
            nsIGlobalObject* global = holder->GetParentObject();
            NS_ENSURE_TRUE_VOID(global);
            // Step 11: Return info.
            encodePromise->MaybeResolve(std::move(aInfo));
          },
          [] { MOZ_CRASH("Unexpected"); })
      ->Track(*holder);
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
  if (!knows) {
    // The compositor bridge may not be established yet (e.g. a not-yet-painted
    // tab), which is expected and not worth warning about; see how
    // MediaDecoder.cpp treats the same case.
    LOG("No compositor available yet for window {}", window->WindowID());
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
