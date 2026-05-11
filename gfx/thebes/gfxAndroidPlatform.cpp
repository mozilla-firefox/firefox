/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "gfxAndroidPlatform.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/CountingAllocatorBase.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/intl/OSPreferences.h"
#include "mozilla/java/GeckoAppShellWrappers.h"
#include "mozilla/java/HardwareCodecCapabilityUtilsWrappers.h"
#include "mozilla/jni/Utils.h"
#include "mozilla/layers/AndroidHardwareBuffer.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/widget/AndroidVsync.h"

#include "AndroidBuild.h"
#include "AndroidSystemFontIterator.h"
#include "GeckoProfiler.h"
#include "gfx2DGlue.h"
#include "gfxFT2FontList.h"
#include "gfxImageSurface.h"
#include "gfxTextRun.h"
#include "nsXULAppAPI.h"
#include "nsIScreen.h"
#include "nsServiceManagerUtils.h"
#include "nsUnicodeProperties.h"
#include "cairo.h"
#include "VsyncSource.h"

#include "ft2build.h"
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
          sManufacturer.EqualsLiteral("HONOR"));
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

// static
bool gfxAndroidPlatform::IsHwCodecSupported(media::MediaCodec aCodec,
                                            bool aEncoder) {
  switch (aCodec) {
    case media::MediaCodec::H264:
      return java::HardwareCodecCapabilityUtils::HasHWH264(aEncoder);
    case media::MediaCodec::VP8:
      return java::HardwareCodecCapabilityUtils::HasHWVP8(aEncoder);
    case media::MediaCodec::VP9:
      return java::HardwareCodecCapabilityUtils::HasHWVP9(aEncoder);
    case media::MediaCodec::AV1:
      return java::HardwareCodecCapabilityUtils::HasHWAV1(aEncoder);
    case media::MediaCodec::HEVC:
      return java::HardwareCodecCapabilityUtils::HasHWHEVC(aEncoder);
    default:
      return false;
  }
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

  if (IS_IN_BMP(aCh)) {
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
