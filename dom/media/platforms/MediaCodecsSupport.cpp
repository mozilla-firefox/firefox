/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "MediaCodecsSupport.h"

#include <algorithm>
#include <array>
#include <memory>

#include "AOMDecoder.h"
#include "MP4Decoder.h"
#include "PDMFactory.h"
#include "PEMFactory.h"
#include "PlatformDecoderModule.h"
#include "VPXDecoder.h"
#include "VideoUtils.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/gfx/gfxVars.h"
#include "nsTHashMap.h"

using MediaCodecsSupport = mozilla::media::MediaCodecsSupport;

namespace mozilla::media {

static StaticAutoPtr<MCSInfo> sInstance;
static StaticMutex sMutex;

#define CODEC_SUPPORT_LOG(msg, ...)                                 \
  MOZ_LOG_FMT(sPDMLog, LogLevel::Debug, "MediaCodecsSupport, " msg, \
              ##__VA_ARGS__)

/* static */
MediaCodecsSupported MCSInfo::GetSupportFromFactory(
    bool aForceRefresh /* = false */) {
  return PDMFactory::Supported(aForceRefresh) +
         PEMFactory::Supported(aForceRefresh);
}

void MCSInfo::AddSupport(const MediaCodecsSupported& aSupport) {
  StaticMutexAutoLock lock(sMutex);
  MCSInfo* instance = GetInstance(lock);
  if (!instance) {
    CODEC_SUPPORT_LOG("Can't add codec support without a MCSInfo instance!");
    return;
  }
  instance->mSupport += aSupport;
}

MediaCodecsSupported MCSInfo::GetSupport() {
  StaticMutexAutoLock lock(sMutex);
  MCSInfo* instance = GetInstance(lock);
  if (!instance) {
    CODEC_SUPPORT_LOG("Can't get codec support without a MCSInfo instance!");
    return MediaCodecsSupported{};
  }
  return instance->mSupport;
}

void MCSInfo::ResetSupport() {
  StaticMutexAutoLock lock(sMutex);
  MCSInfo* instance = GetInstance(lock);
  if (!instance) {
    CODEC_SUPPORT_LOG("Can't reset codec support without a MCSInfo instance!");
    return;
  }
  instance->mSupport.clear();
}

DecodeSupportSet MCSInfo::GetDecodeSupportSet(
    const MediaCodec& aCodec, const MediaCodecsSupported& aSupported) {
  DecodeSupportSet support;
  const auto supportInfo = GetCodecDefinition(aCodec);
  if (aSupported.contains(supportInfo.swDecodeSupport)) {
    support += DecodeSupport::SoftwareDecode;
  }
  if (aSupported.contains(supportInfo.hwDecodeSupport)) {
    support += DecodeSupport::HardwareDecode;
  }
  return support;
}

EncodeSupportSet MCSInfo::GetEncodeSupportSet(
    const MediaCodec& aCodec, const MediaCodecsSupported& aSupported) {
  EncodeSupportSet support;
  const auto supportInfo = GetCodecDefinition(aCodec);
  if (aSupported.contains(supportInfo.swEncodeSupport)) {
    support += EncodeSupport::SoftwareEncode;
  }
  if (aSupported.contains(supportInfo.hwEncodeSupport)) {
    support += EncodeSupport::HardwareEncode;
  }
  return support;
}

MediaCodecsSupported MCSInfo::GetDecodeMediaCodecsSupported(
    const MediaCodec& aCodec, const DecodeSupportSet& aSupportSet) {
  MediaCodecsSupported support;
  const auto supportInfo = GetCodecDefinition(aCodec);
  if (aSupportSet.contains(DecodeSupport::SoftwareDecode)) {
    support += supportInfo.swDecodeSupport;
  }
  if (aSupportSet.contains(DecodeSupport::HardwareDecode)) {
    support += supportInfo.hwDecodeSupport;
  }
  if (aSupportSet.contains(DecodeSupport::UnsureDueToLackOfExtension)) {
    // Every codec reaching this arm must declare a real lack-of-extension
    // entry in GetAllCodecDefinitions.
    MOZ_ASSERT(supportInfo.lackOfHWExtenstion != MediaCodecsSupport::SENTINEL);
    support += supportInfo.lackOfHWExtenstion;
  }
  return support;
}

MediaCodecsSupported MCSInfo::GetEncodeMediaCodecsSupported(
    const MediaCodec& aCodec, const EncodeSupportSet& aSupportSet) {
  MediaCodecsSupported support;
  const auto supportInfo = GetCodecDefinition(aCodec);
  if (aSupportSet.contains(EncodeSupport::SoftwareEncode)) {
    support += supportInfo.swEncodeSupport;
  }
  if (aSupportSet.contains(EncodeSupport::HardwareEncode)) {
    support += supportInfo.hwEncodeSupport;
  }
  if (aSupportSet.contains(EncodeSupport::UnsureDueToLackOfExtension)) {
    // Every codec reaching this arm must declare a real lack-of-extension
    // entry in GetAllCodecDefinitions.
    MOZ_ASSERT(supportInfo.lackOfHWExtenstion != MediaCodecsSupport::SENTINEL);
    support += supportInfo.lackOfHWExtenstion;
  }
  return support;
}

bool MCSInfo::SupportsSoftwareDecode(
    const MediaCodecsSupported& aSupportedCodecs, const MediaCodec& aCodec) {
  return (
      aSupportedCodecs.contains(GetCodecDefinition(aCodec).swDecodeSupport));
}

bool MCSInfo::SupportsHardwareDecode(
    const MediaCodecsSupported& aSupportedCodecs, const MediaCodec& aCodec) {
  return (
      aSupportedCodecs.contains(GetCodecDefinition(aCodec).hwDecodeSupport));
}

#ifdef MOZ_WIDGET_ANDROID
DecodeSupportSet MCSInfo::GetMediaCodecDecodeSupportForColorDepth(
    const MediaCodec& aCodec, const DecodeSupportSet& aSupport,
    gfx::ColorDepth aColorDepth) {
  if (aColorDepth == gfx::ColorDepth::COLOR_8) {
    return aSupport;
  }

  // Only H264 and HEVC use MediaCodec for software decoding. Our own software
  // decoders support any color depth.
  DecodeSupportSet support;
  const bool mediaCodecSoftware =
      aCodec == MediaCodec::H264 || aCodec == MediaCodec::HEVC;
  if (!mediaCodecSoftware && aSupport.contains(DecodeSupport::SoftwareDecode)) {
    support += DecodeSupport::SoftwareDecode;
  }

  // MediaCodec does not support decoding more than 10-bit.
  if (aColorDepth != gfx::ColorDepth::COLOR_10) {
    return support;
  }

  const auto supported10Bit = gfx::gfxVars::PlatformMediaCodecs10BitSupported();
  if (aSupport.contains(DecodeSupport::HardwareDecode) &&
      SupportsHardwareDecode(supported10Bit, aCodec)) {
    support += DecodeSupport::HardwareDecode;
  }
  if (mediaCodecSoftware && aSupport.contains(DecodeSupport::SoftwareDecode) &&
      SupportsSoftwareDecode(supported10Bit, aCodec)) {
    support += DecodeSupport::SoftwareDecode;
  }
  return support;
}

static gfx::IntSize GetHardwareDecodeMaxSize(const MediaCodec& aCodec) {
  switch (aCodec) {
    case MediaCodec::H264:
      return gfx::gfxVars::PlatformH264HwDecodeMaxSize();
    case MediaCodec::HEVC:
      return gfx::gfxVars::PlatformHEVCHwDecodeMaxSize();
    case MediaCodec::VP8:
      return gfx::gfxVars::PlatformVP8HwDecodeMaxSize();
    case MediaCodec::VP9:
      return gfx::gfxVars::PlatformVP9HwDecodeMaxSize();
    case MediaCodec::AV1:
      return gfx::gfxVars::PlatformAV1HwDecodeMaxSize();
    default:
      return gfx::IntSize();
  }
}

DecodeSupportSet MCSInfo::GetMediaCodecDecodeSupportForSize(
    const MediaCodec& aCodec, const DecodeSupportSet& aSupport,
    const gfx::IntSize& aSize) {
  const gfx::IntSize maxSize = GetHardwareDecodeMaxSize(aCodec);
  if (aSize.IsEmpty() || maxSize.IsEmpty()) {
    return aSupport;
  }

  // Allow for either orientation.
  if ((aSize.width <= maxSize.width && aSize.height <= maxSize.height) ||
      (aSize.width <= maxSize.height && aSize.height <= maxSize.width)) {
    return aSupport;
  }

  return aSupport - DecodeSupport::HardwareDecode;
}

gfx::IntSize MCSInfo::GetMediaCodecAdaptivePlaybackMaxSize(
    const MediaCodec& aCodec, const gfx::IntSize& aSize) {
  if (!SupportsHardwareDecode(
          gfx::gfxVars::PlatformMediaCodecsAdaptivePlayback(), aCodec)) {
    return gfx::IntSize();
  }

  // Orient the sizes to match the stream, as decoders support either
  // orientation, see GetMediaCodecDecodeSupportForSize.
  const bool portrait = aSize.height > aSize.width;
  auto orient = [portrait](const gfx::IntSize& aOther) {
    return (aOther.height > aOther.width) == portrait
               ? aOther
               : gfx::IntSize(aOther.height, aOther.width);
  };

  // Matches what the Java MediaCodec decoder requests.
  gfx::IntSize maxSize = orient(gfx::IntSize(1920, 1080));
  const gfx::IntSize hwMaxSize = orient(GetHardwareDecodeMaxSize(aCodec));
  if (!hwMaxSize.IsEmpty()) {
    maxSize = gfx::IntSize(std::min(maxSize.width, hwMaxSize.width),
                           std::min(maxSize.height, hwMaxSize.height));
  }

  return gfx::IntSize(std::max(maxSize.width, aSize.width),
                      std::max(maxSize.height, aSize.height));
}
#endif

bool MCSInfo::SupportsSoftwareEncode(
    const MediaCodecsSupported& aSupportedCodecs, const MediaCodec& aCodec) {
  return (
      aSupportedCodecs.contains(GetCodecDefinition(aCodec).swEncodeSupport));
}

bool MCSInfo::SupportsHardwareEncode(
    const MediaCodecsSupported& aSupportedCodecs, const MediaCodec& aCodec) {
  return (
      aSupportedCodecs.contains(GetCodecDefinition(aCodec).hwEncodeSupport));
}

void MCSInfo::GetMediaCodecsSupportedString(
    nsCString& aSupportString, const MediaCodecsSupported& aSupportedCodecs) {
  CodecDefinition supportInfo;
  aSupportString = ""_ns;
  StaticMutexAutoLock lock(sMutex);
  MCSInfo* instance = GetInstance(lock);
  if (!instance) {
    CODEC_SUPPORT_LOG("Can't get codec support string w/o a MCSInfo instance!");
    return;
  }
  for (const auto& it : GetAllCodecDefinitions()) {
    if (it.codec == MediaCodec::SENTINEL) {
      break;
    }
    if (!instance->mHashTableCodec->Get(it.codec, &supportInfo)) {
      CODEC_SUPPORT_LOG("Can't find codec for MediaCodecsSupported enum: {}",
                        static_cast<int>(it.codec));
      continue;
    }
    aSupportString.Append(supportInfo.commonName);
    bool foundSupport = false;
    if (aSupportedCodecs.contains(it.swDecodeSupport)) {
      aSupportString.Append(" SWDEC"_ns);
      foundSupport = true;
    }
    if (aSupportedCodecs.contains(it.hwDecodeSupport)) {
      aSupportString.Append(" HWDEC"_ns);
      foundSupport = true;
    }
    if (aSupportedCodecs.contains(it.swEncodeSupport)) {
      aSupportString.Append(" SWENC"_ns);
      foundSupport = true;
    }
    if (aSupportedCodecs.contains(it.hwEncodeSupport)) {
      aSupportString.Append(" HWENC"_ns);
      foundSupport = true;
    }
    if (aSupportedCodecs.contains(it.lackOfHWExtenstion)) {
      aSupportString.Append(" LACK_OF_EXTENSION"_ns);
      foundSupport = true;
    }
    if (!foundSupport) {
      aSupportString.Append(" NONE"_ns);
    }
    aSupportString.Append("\n"_ns);
  }
  // Remove any trailing newline characters
  if (!aSupportString.IsEmpty()) {
    aSupportString.Truncate(aSupportString.Length() - 1);
  }
}

MCSInfo* MCSInfo::GetInstance(const StaticMutexAutoLock& /* unused */) {
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    CODEC_SUPPORT_LOG("In XPCOM shutdown - not returning MCSInfo instance!");
    return nullptr;
  }
  if (!sInstance) {
    sInstance = new MCSInfo();
  }
  return sInstance.get();
}

MCSInfo::MCSInfo() {
  // Initialize hash tables
  mHashTableMCS =
      std::make_unique<nsTHashMap<MediaCodecsSupport, CodecDefinition>>();
  mHashTableCodec = std::make_unique<nsTHashMap<MediaCodec, CodecDefinition>>();

  for (const auto& it : GetAllCodecDefinitions()) {
    // Insert MediaCodecsSupport values as keys
    mHashTableMCS->InsertOrUpdate(it.swDecodeSupport, it);
    mHashTableMCS->InsertOrUpdate(it.hwDecodeSupport, it);
    // Insert codec enum values as keys
    mHashTableCodec->InsertOrUpdate(it.codec, it);
  }

  GetMainThreadSerialEventTarget()->Dispatch(
      NS_NewRunnableFunction("MCSInfo::MCSInfo", [] {
        if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdown)) {
          StaticMutexAutoLock lock(sMutex);
          sInstance = nullptr;
          return;
        }
        RunOnShutdown(
            [] {
              StaticMutexAutoLock lock(sMutex);
              sInstance = nullptr;
            },
            ShutdownPhase::XPCOMShutdown);
      }));
}

CodecDefinition MCSInfo::GetCodecDefinition(const MediaCodec& aCodec) {
  CodecDefinition info;
  StaticMutexAutoLock lock(sMutex);
  MCSInfo* instance = GetInstance(lock);
  if (!instance) {
    CODEC_SUPPORT_LOG("Can't get codec definition without a MCSInfo instance!");
  } else if (!instance->mHashTableCodec->Get(aCodec, &info)) {
    CODEC_SUPPORT_LOG("Could not find codec definition for codec enum: {}!",
                      static_cast<int>(aCodec));
  }
  return info;
}

MediaCodecsSupport MCSInfo::GetMediaCodecsSupportEnum(
    const MediaCodec& aCodec, const DecodeSupport& aSupport) {
  const CodecDefinition cd = GetCodecDefinition(aCodec);
  if (aSupport == DecodeSupport::SoftwareDecode) {
    return cd.swDecodeSupport;
  }
  if (aSupport == DecodeSupport::HardwareDecode) {
    return cd.hwDecodeSupport;
  }
  return MediaCodecsSupport::SENTINEL;
}

MediaCodecsSupport MCSInfo::GetMediaCodecsSupportEnum(
    const MediaCodec& aCodec, const EncodeSupport& aSupport) {
  const CodecDefinition cd = GetCodecDefinition(aCodec);
  if (aSupport == EncodeSupport::SoftwareEncode) {
    return cd.swEncodeSupport;
  }
  if (aSupport == EncodeSupport::HardwareEncode) {
    return cd.hwEncodeSupport;
  }
  return MediaCodecsSupport::SENTINEL;
}

MediaCodecSet MCSInfo::GetMediaCodecSetFromMimeTypes(
    const nsTArray<nsCString>& aCodecStrings) {
  MediaCodecSet support;
  for (const auto& ms : aCodecStrings) {
    const MediaCodec codec = MCSInfo::GetMediaCodecFromMimeType(ms);
    if (codec == MediaCodec::SENTINEL) {
      continue;
    }
    MOZ_ASSERT(codec < MediaCodec::SENTINEL);
    support += codec;
  }
  return support;
}

MediaCodec MCSInfo::GetMediaCodecFromMimeType(const nsACString& aMimeType) {
  // Video codecs
  if (MP4Decoder::IsH264(aMimeType)) {
    return MediaCodec::H264;
  }
  if (VPXDecoder::IsVP8(aMimeType)) {
    return MediaCodec::VP8;
  }
  if (VPXDecoder::IsVP9(aMimeType)) {
    return MediaCodec::VP9;
  }
  if (MP4Decoder::IsHEVC(aMimeType)) {
    return MediaCodec::HEVC;
  }
  if (AOMDecoder::IsAV1(aMimeType)) {
    return MediaCodec::AV1;
  }
  if (aMimeType.EqualsLiteral("video/av01")) {
    return MediaCodec::AV1;
  }
  // TODO: Should this be Android only?
#ifdef ANDROID
  if (aMimeType.EqualsLiteral("video/x-vnd.on2.vp8")) {
    return MediaCodec::VP8;
  }
  if (aMimeType.EqualsLiteral("video/x-vnd.on2.vp9")) {
    return MediaCodec::VP9;
  }
#endif
  // Audio codecs
  if (MP4Decoder::IsAAC(aMimeType)) {
    return MediaCodec::AAC;
  }
  if (aMimeType.EqualsLiteral("audio/vorbis")) {
    return MediaCodec::Vorbis;
  }
  if (aMimeType.EqualsLiteral("audio/flac")) {
    return MediaCodec::FLAC;
  }
  if (IsWaveMimetype(aMimeType)) {
    return MediaCodec::Wave;
  }
  if (aMimeType.EqualsLiteral("audio/opus")) {
    return MediaCodec::Opus;
  }
  if (aMimeType.EqualsLiteral("audio/mpeg")) {
    return MediaCodec::MP3;
  }

  CODEC_SUPPORT_LOG("No specific codec enum for MIME type string: {}",
                    nsCString(aMimeType).get());
  return MediaCodec::SENTINEL;
}

#define MEDIA_CODEC_DEF_ENTRY_META(name, mimeType, lackOfExt) \
  {MediaCodec::name,                                          \
   #name,                                                     \
   mimeType,                                                  \
   MediaCodecsSupport::name##SoftwareDecode,                  \
   MediaCodecsSupport::name##HardwareDecode,                  \
   MediaCodecsSupport::name##SoftwareEncode,                  \
   MediaCodecsSupport::name##HardwareEncode,                  \
   MediaCodecsSupport::lackOfExt}

#define MEDIA_CODEC_DEF_ENTRY(name, mimeType) \
  MEDIA_CODEC_DEF_ENTRY_META(name, mimeType, SENTINEL)

#define MEDIA_CODEC_DEF_ENTRY_LACKOFEXT(name, mimeType) \
  MEDIA_CODEC_DEF_ENTRY_META(name, mimeType, name##LackOfExtension)

std::array<CodecDefinition, 13> MCSInfo::GetAllCodecDefinitions() {
  static constexpr std::array<CodecDefinition, 13> codecDefinitions = {
      {MEDIA_CODEC_DEF_ENTRY(H264, "video/avc"),
       MEDIA_CODEC_DEF_ENTRY(VP9, "video/vp9"),
       MEDIA_CODEC_DEF_ENTRY(VP8, "video/vp8"),
       MEDIA_CODEC_DEF_ENTRY_LACKOFEXT(AV1, "video/av1"),
       MEDIA_CODEC_DEF_ENTRY_LACKOFEXT(HEVC, "video/hevc"),
       MEDIA_CODEC_DEF_ENTRY(AAC, "audio/mp4a-latm"),
       MEDIA_CODEC_DEF_ENTRY(MP3, "audio/mpeg"),
       MEDIA_CODEC_DEF_ENTRY(Opus, "audio/opus"),
       MEDIA_CODEC_DEF_ENTRY(Vorbis, "audio/vorbis"),
       MEDIA_CODEC_DEF_ENTRY(FLAC, "audio/flac"),
       MEDIA_CODEC_DEF_ENTRY(Wave, "audio/x-wav")}};
  return codecDefinitions;
}

#undef MEDIA_CODEC_DEF_ENTRY_LACKOFEXT
#undef MEDIA_CODEC_DEF_ENTRY
#undef MEDIA_CODEC_DEF_ENTRY_META

}  // namespace mozilla::media

#undef CODEC_SUPPORT_LOG
