/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxAndroidPlatform.h"

#include "AndroidBuild.h"
#include "AndroidSystemFontIterator.h"
#include "GeckoProfiler.h"
#include "VsyncSource.h"
#include "base/basictypes.h"
#include "cairo.h"
#include "ft2build.h"
#include "gfx2DGlue.h"
#include "gfxFT2FontList.h"
#include "gfxImageSurface.h"
#include "gfxTextRun.h"
#include "mozilla/CountingAllocatorBase.h"
#include "mozilla/EnumeratedArray.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/Tokenizer.h"
#include "mozilla/Utf16.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/intl/OSPreferences.h"
#include "mozilla/java/GeckoAppShellWrappers.h"
#include "mozilla/java/HardwareCodecCapabilityUtilsWrappers.h"
#include "mozilla/jni/Utils.h"
#include "mozilla/layers/AndroidHardwareBuffer.h"
#include "mozilla/widget/AndroidVsync.h"
#include "nsIScreen.h"
#include "nsServiceManagerUtils.h"
#include "nsUnicodeProperties.h"
#include "nsXULAppAPI.h"
#include FT_FREETYPE_H
#include FT_MODULE_H

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::unicode;
using mozilla::intl::LocaleService;
using mozilla::intl::OSPreferences;

static FT_Library gPlatformFTLibrary = nullptr;

class FreetypeReporter final : public nsIMemoryReporter,
                               public CountingAllocatorBase<FreetypeReporter> {
 private:
  ~FreetypeReporter() {}

 public:
  NS_DECL_ISUPPORTS

  static void* Malloc(FT_Memory, long size) { return CountingMalloc(size); }

  static void Free(FT_Memory, void* p) { return CountingFree(p); }

  static void* Realloc(FT_Memory, long cur_size, long new_size, void* p) {
    return CountingRealloc(p, new_size);
  }

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    MOZ_COLLECT_REPORT("explicit/freetype", KIND_HEAP, UNITS_BYTES,
                       MemoryAllocated(), "Memory used by Freetype.");

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(FreetypeReporter, nsIMemoryReporter)

static FT_MemoryRec_ sFreetypeMemoryRecord;

PRThread* gfxAndroidPlatform::sFontAPIInitializeThread = nullptr;
constinit nsCString gfxAndroidPlatform::sManufacturer;

// static
bool gfxAndroidPlatform::IsFontAPIDisabled(bool aDontCheckPref) {
  if (!aDontCheckPref &&
      StaticPrefs::gfx_font_list_use_font_match_api_force_enabled_AtStartup()) {
    return false;
  }

  // Some manufacturer devices seem to crash when using font match API
  // (Bug 1787551 / Bug 1990734).

  if (sManufacturer.IsEmpty()) {
    sManufacturer = java::sdk::Build::MANUFACTURER()->ToCString();
  }
  return (sManufacturer.EqualsLiteral("OPPO") ||
          sManufacturer.EqualsLiteral("realme") ||
          sManufacturer.EqualsLiteral("OnePlus") ||
          sManufacturer.EqualsLiteral("HONOR") ||
          sManufacturer.EqualsLiteral("HUAWEI"));
}

// static
void gfxAndroidPlatform::InitializeFontAPI() {
  if (XRE_GetProcessType() != GeckoProcessType_Default) {
    return;
  }

  // Android 12+ doesn't read font configuration XML files directly.
  // It means that it is slow to call font API at first.
  if (jni::GetAPIVersion() < 31) {
    return;
  }

  // This will be called before XPCOM service isn't started. So don't check
  // preferences.
  if (IsFontAPIDisabled(true)) {
    return;
  }

  if (__builtin_available(android 29, *)) {
    auto FontAPIInitializeCallback = [](void* aUnused) {
      AUTO_PROFILER_REGISTER_THREAD("InitializingFontAPI");
      PR_SetCurrentThreadName("InitializingFontAPI");
      AndroidSystemFontIterator::Preload();
    };
    sFontAPIInitializeThread = PR_CreateThread(
        PR_USER_THREAD, FontAPIInitializeCallback, nullptr, PR_PRIORITY_NORMAL,
        PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0);
  }
}

// static
void gfxAndroidPlatform::WaitForInitializeFontAPI() {
  if (sFontAPIInitializeThread) {
    PR_JoinThread(sFontAPIInitializeThread);
    sFontAPIInitializeThread = nullptr;
  }
}

static void AppendMimeType(nsCString& aList, const nsACString& aMimeType) {
  if (!aList.IsEmpty()) {
    aList.Append(',');
  }
  aList.Append(aMimeType);
}

using HwDecodeMaxSizes = EnumeratedArray<media::MediaCodec, gfx::IntSize,
                                         size_t(media::MediaCodec::SENTINEL)>;

// Codecs that map to a media::MediaCodec are returned in aSupported, and in
// aSupported10Bit if they can also decode a 10-bit profile, and in
// aAdaptivePlayback if their default decoder is a hardware decoder which
// supports adaptive playback. The largest width and height supported by any
// hardware decoder for each codec are returned in aHwMaxSizes. Decoder MIME
// types which do not map are returned as comma separated lists instead.
static void QueryMediaCodecsSupported(
    media::MediaCodecsSupported& aSupported,
    media::MediaCodecsSupported& aSupported10Bit,
    media::MediaCodecsSupported& aAdaptivePlayback,
    HwDecodeMaxSizes& aHwMaxSizes, nsCString& aUnmappedSwDecode,
    nsCString& aUnmappedHwDecode) {
  jni::ObjectArray::LocalRef supportedTypes =
      java::HardwareCodecCapabilityUtils::GetSupportedMimeTypesWithAccelInfo();
  for (size_t i = 0; i < supportedTypes->Length(); i++) {
    const nsCString s =
        jni::String::LocalRef(supportedTypes->GetElement(i))->ToCString();
    const int32_t sep = s.FindChar(' ');
    if (sep <= 0 || static_cast<uint32_t>(sep) + 1 >= s.Length()) {
      continue;
    }

    const auto caps = Substring(s, 0, sep);
    const auto mimeType = Substring(s, sep + 1);
    const media::MediaCodec codec =
        media::MCSInfo::GetMediaCodecFromMimeType(mimeType);
    if (codec == media::MediaCodec::SENTINEL) {
      if (caps == "SW"_ns) {
        AppendMimeType(aUnmappedSwDecode, mimeType);
      } else if (caps == "HW"_ns) {
        AppendMimeType(aUnmappedHwDecode, mimeType);
      }
      continue;
    }

    if (caps == "SW"_ns) {
      aSupported += media::MCSInfo::GetMediaCodecsSupportEnum(
          codec, media::DecodeSupport::SoftwareDecode);
    } else if (caps == "HW"_ns) {
      aSupported += media::MCSInfo::GetMediaCodecsSupportEnum(
          codec, media::DecodeSupport::HardwareDecode);
    } else if (caps == "SWE"_ns) {
      aSupported += media::MCSInfo::GetMediaCodecsSupportEnum(
          codec, media::EncodeSupport::SoftwareEncode);
    } else if (caps == "HWE"_ns) {
      aSupported += media::MCSInfo::GetMediaCodecsSupportEnum(
          codec, media::EncodeSupport::HardwareEncode);
    } else if (caps == "SW10B"_ns) {
      aSupported10Bit += media::MCSInfo::GetMediaCodecsSupportEnum(
          codec, media::DecodeSupport::SoftwareDecode);
    } else if (caps == "HW10B"_ns) {
      aSupported10Bit += media::MCSInfo::GetMediaCodecsSupportEnum(
          codec, media::DecodeSupport::HardwareDecode);
    } else if (caps == "HWADAPT"_ns) {
      aAdaptivePlayback += media::MCSInfo::GetMediaCodecsSupportEnum(
          codec, media::DecodeSupport::HardwareDecode);
    } else if (StringBeginsWith(caps, "HWMAX"_ns)) {
      Tokenizer t(Substring(caps, 5));
      int32_t width, height;
      if (t.ReadInteger(&width) && t.CheckChar(',') && t.ReadInteger(&height) &&
          t.CheckEOF() && width > 0 && height > 0) {
        gfx::IntSize& maxSize = aHwMaxSizes[codec];
        maxSize.width = std::max(maxSize.width, width);
        maxSize.height = std::max(maxSize.height, height);
      }
    }
  }
}

void gfxAndroidPlatform::InitPlatformHardwareVideoConfig() {
  media::MediaCodecsSupported supported;
  media::MediaCodecsSupported supported10Bit;
  media::MediaCodecsSupported adaptivePlayback;
  HwDecodeMaxSizes hwMaxSizes;
  nsCString unmappedSwDecode;
  nsCString unmappedHwDecode;
  QueryMediaCodecsSupported(supported, supported10Bit, adaptivePlayback,
                            hwMaxSizes, unmappedSwDecode, unmappedHwDecode);
  gfxVars::SetPlatformMediaCodecsSupported(supported);
  gfxVars::SetPlatformMediaCodecs10BitSupported(supported10Bit);
  gfxVars::SetPlatformMediaCodecsAdaptivePlayback(adaptivePlayback);
  gfxVars::SetPlatformH264HwDecodeMaxSize(hwMaxSizes[media::MediaCodec::H264]);
  gfxVars::SetPlatformHEVCHwDecodeMaxSize(hwMaxSizes[media::MediaCodec::HEVC]);
  gfxVars::SetPlatformVP8HwDecodeMaxSize(hwMaxSizes[media::MediaCodec::VP8]);
  gfxVars::SetPlatformVP9HwDecodeMaxSize(hwMaxSizes[media::MediaCodec::VP9]);
  gfxVars::SetPlatformAV1HwDecodeMaxSize(hwMaxSizes[media::MediaCodec::AV1]);
  gfxVars::SetPlatformUnmappedSwDecodeMimeTypes(unmappedSwDecode);
  gfxVars::SetPlatformUnmappedHwDecodeMimeTypes(unmappedHwDecode);
}

// static
bool gfxAndroidPlatform::IsHwCodecSupported(media::MediaCodec aCodec,
                                            bool aEncoder) {
  if (aEncoder) {
    return media::MCSInfo::SupportsHardwareEncode(
        gfxVars::PlatformMediaCodecsSupported(), aCodec);
  }
  return media::MCSInfo::SupportsHardwareDecode(
      gfxVars::PlatformMediaCodecsSupported(), aCodec);
}

gfxAndroidPlatform::gfxAndroidPlatform() {
  // A custom allocator.  It counts allocations, enabling memory reporting.
  sFreetypeMemoryRecord.user = nullptr;
  sFreetypeMemoryRecord.alloc = FreetypeReporter::Malloc;
  sFreetypeMemoryRecord.free = FreetypeReporter::Free;
  sFreetypeMemoryRecord.realloc = FreetypeReporter::Realloc;

  // These two calls are equivalent to FT_Init_FreeType(), but allow us to
  // provide a custom memory allocator.
  FT_New_Library(&sFreetypeMemoryRecord, &gPlatformFTLibrary);
  FT_Add_Default_Modules(gPlatformFTLibrary);

  Factory::SetFTLibrary(gPlatformFTLibrary);

  RegisterStrongMemoryReporter(MakeAndAddRef<FreetypeReporter>());

  // Bug 1886573: At this point, we don't yet have primary screen depth.
  // This setting of screen depth to 0 is preserving existing behavior,
  // and should be fixed.
  int32_t screenDepth = 0;
  mOffscreenFormat = screenDepth == 16 ? SurfaceFormat::R5G6B5_UINT16
                                       : SurfaceFormat::X8R8G8B8_UINT32;
}

gfxAndroidPlatform::~gfxAndroidPlatform() {
  FT_Done_Library(gPlatformFTLibrary);
  gPlatformFTLibrary = nullptr;
  layers::AndroidHardwareBufferManager::Shutdown();
}

void gfxAndroidPlatform::InitAcceleration() { gfxPlatform::InitAcceleration(); }

already_AddRefed<gfxASurface> gfxAndroidPlatform::CreateOffscreenSurface(
    const IntSize& aSize, gfxImageFormat aFormat) {
  if (!Factory::AllowedSurfaceSize(aSize)) {
    return nullptr;
  }

  RefPtr<gfxASurface> newSurface;
  newSurface = new gfxImageSurface(aSize, aFormat);

  return newSurface.forget();
}

static bool IsJapaneseLocale() {
  static bool sInitialized = false;
  static bool sIsJapanese = false;

  if (!sInitialized) {
    sInitialized = true;

    nsAutoCString appLocale;
    LocaleService::GetInstance()->GetAppLocaleAsBCP47(appLocale);

    const nsDependentCSubstring lang(appLocale, 0, 2);
    if (lang.EqualsLiteral("ja")) {
      sIsJapanese = true;
    } else {
      OSPreferences::GetInstance()->GetSystemLocale(appLocale);

      const nsDependentCSubstring lang(appLocale, 0, 2);
      if (lang.EqualsLiteral("ja")) {
        sIsJapanese = true;
      }
    }
  }

  return sIsJapanese;
}

void gfxAndroidPlatform::GetCommonFallbackFonts(
    uint32_t aCh, Script aRunScript, FontPresentation aPresentation,
    nsTArray<const char*>& aFontList) {
  static const char kDroidSansJapanese[] = "Droid Sans Japanese";
  static const char kMotoyaLMaru[] = "MotoyaLMaru";
  static const char kNotoSansCJKJP[] = "Noto Sans CJK JP";
  static const char kNotoColorEmoji[] = "Noto Color Emoji";

  if (PrefersColor(aPresentation)) {
    aFontList.AppendElement(kNotoColorEmoji);
  }

  if (mozilla::IsInBMP(aCh)) {
    // try language-specific "Droid Sans *" and "Noto Sans *" fonts for
    // certain blocks, as most devices probably have these
    uint8_t block = (aCh >> 8) & 0xff;
    switch (block) {
      case 0x05:
        aFontList.AppendElement("Noto Sans Hebrew");
        aFontList.AppendElement("Droid Sans Hebrew");
        aFontList.AppendElement("Noto Sans Armenian");
        aFontList.AppendElement("Droid Sans Armenian");
        break;
      case 0x06:
        aFontList.AppendElement("Noto Sans Arabic");
        aFontList.AppendElement("Droid Sans Arabic");
        break;
      case 0x09:
        aFontList.AppendElement("Noto Sans Devanagari");
        aFontList.AppendElement("Noto Sans Bengali");
        aFontList.AppendElement("Droid Sans Devanagari");
        break;
      case 0x0a:
        aFontList.AppendElement("Noto Sans Gurmukhi");
        aFontList.AppendElement("Noto Sans Gujarati");
        break;
      case 0x0b:
        aFontList.AppendElement("Noto Sans Tamil");
        aFontList.AppendElement("Noto Sans Oriya");
        aFontList.AppendElement("Droid Sans Tamil");
        break;
      case 0x0c:
        aFontList.AppendElement("Noto Sans Telugu");
        aFontList.AppendElement("Noto Sans Kannada");
        break;
      case 0x0d:
        aFontList.AppendElement("Noto Sans Malayalam");
        aFontList.AppendElement("Noto Sans Sinhala");
        break;
      case 0x0e:
        aFontList.AppendElement("Noto Sans Thai");
        aFontList.AppendElement("Noto Sans Lao");
        aFontList.AppendElement("Droid Sans Thai");
        break;
      case 0x0f:
        aFontList.AppendElement("Noto Sans Tibetan");
        break;
      case 0x10:
      case 0x2d:
        aFontList.AppendElement("Noto Sans Georgian");
        aFontList.AppendElement("Droid Sans Georgian");
        break;
      case 0x12:
      case 0x13:
        aFontList.AppendElement("Noto Sans Ethiopic");
        aFontList.AppendElement("Droid Sans Ethiopic");
        break;
      case 0x21:
      case 0x23:
      case 0x24:
      case 0x26:
      case 0x27:
      case 0x29:
        aFontList.AppendElement("Noto Sans Symbols");
        break;
      case 0xf9:
      case 0xfa:
        if (IsJapaneseLocale()) {
          aFontList.AppendElement(kMotoyaLMaru);
          aFontList.AppendElement(kNotoSansCJKJP);
          aFontList.AppendElement(kDroidSansJapanese);
        }
        break;
      default:
        if (block >= 0x2e && block <= 0x9f && IsJapaneseLocale()) {
          aFontList.AppendElement(kMotoyaLMaru);
          aFontList.AppendElement(kNotoSansCJKJP);
          aFontList.AppendElement(kDroidSansJapanese);
        }
        break;
    }
  }
  // and try Droid Sans Fallback as a last resort
  aFontList.AppendElement("Droid Sans Fallback");
}

bool gfxAndroidPlatform::CreatePlatformFontList() {
  return gfxPlatformFontList::Initialize(new gfxFT2FontList);
}

void gfxAndroidPlatform::ReadSystemFontList(
    mozilla::dom::SystemFontList* aFontList) {
  gfxFT2FontList::PlatformFontList()->ReadSystemFontList(aFontList);
}

bool gfxAndroidPlatform::FontHintingEnabled() {
  // In "mobile" builds, we sometimes use non-reflow-zoom, so we
  // might not want hinting.  Let's see.

#ifdef MOZ_WIDGET_ANDROID
  // On Android, we currently only use gecko to render web
  // content that can always be be non-reflow-zoomed.  So turn off
  // hinting.
  //
  // XXX when gecko-android-java is used as an "app runtime", we may
  // want to re-enable hinting for non-browser processes there.
  //
  // If this value is changed, we must ensure that the argument passed
  // to SkInitCairoFT() in GPUParent::RecvInit() is also updated.
  return false;
#endif  //  MOZ_WIDGET_ANDROID

  // Currently, we don't have any other targets, but if/when we do,
  // decide how to handle them here.

  MOZ_ASSERT_UNREACHABLE("oops, what platform is this?");
  return gfxPlatform::FontHintingEnabled();
}

bool gfxAndroidPlatform::RequiresLinearZoom() {
#ifdef MOZ_WIDGET_ANDROID
  // On Android, we currently only use gecko to render web
  // content that can always be be non-reflow-zoomed.
  //
  // XXX when gecko-android-java is used as an "app runtime", we may
  // want to use linear zoom only for the web browser process, not other apps.
  return true;
#endif

  MOZ_ASSERT_UNREACHABLE("oops, what platform is this?");
  return gfxPlatform::RequiresLinearZoom();
}

class AndroidVsyncSource final : public VsyncSource,
                                 public widget::AndroidVsync::Observer {
 public:
  AndroidVsyncSource() : mAndroidVsync(widget::AndroidVsync::GetInstance()) {}

  bool IsVsyncEnabled() override {
    MOZ_ASSERT(NS_IsMainThread());
    return mObservingVsync;
  }

  void EnableVsync() override {
    MOZ_ASSERT(NS_IsMainThread());

    if (mObservingVsync) {
      return;
    }

    float fps = java::GeckoAppShell::GetScreenRefreshRate();
    MOZ_ASSERT(fps > 0.0f);
    mVsyncRate = TimeDuration::FromMilliseconds(1000.0 / fps);
    mAndroidVsync->RegisterObserver(this, widget::AndroidVsync::RENDER);
    mObservingVsync = true;
  }

  void DisableVsync() override {
    MOZ_ASSERT(NS_IsMainThread());

    if (!mObservingVsync) {
      return;
    }
    mAndroidVsync->UnregisterObserver(this, widget::AndroidVsync::RENDER);
    mObservingVsync = false;
  }

  TimeDuration GetVsyncRate() override { return mVsyncRate; }

  void Shutdown() override { DisableVsync(); }

  // Override for the widget::AndroidVsync::Observer method
  void OnVsync(const TimeStamp& aTimeStamp) override {
    // Use the timebase from the frame callback as the vsync time, unless it
    // is in the future.
    TimeStamp now = TimeStamp::Now();
    TimeStamp vsyncTime = aTimeStamp < now ? aTimeStamp : now;
    TimeStamp outputTime = vsyncTime + GetVsyncRate();

    NotifyVsync(vsyncTime, outputTime);
  }

  void OnMaybeUpdateRefreshRate() override {
    NS_DispatchToMainThread(
        NS_NewRunnableFunction(__func__, [self = RefPtr{this}]() {
          if (!self->mObservingVsync) {
            return;
          }
          self->DisableVsync();
          self->EnableVsync();
        }));
  }

 private:
  virtual ~AndroidVsyncSource() { DisableVsync(); }

  RefPtr<widget::AndroidVsync> mAndroidVsync;
  TimeDuration mVsyncRate;
  bool mObservingVsync = false;
};

already_AddRefed<mozilla::gfx::VsyncSource>
gfxAndroidPlatform::CreateGlobalHardwareVsyncSource() {
  RefPtr<AndroidVsyncSource> vsyncSource = new AndroidVsyncSource();
  return vsyncSource.forget();
}
