/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define PANGO_ENABLE_BACKEND
#define PANGO_ENABLE_ENGINE

#include "gfxPlatformGtk.h"

#include <fontconfig/fontconfig.h>
#include <gtk/gtk.h>

#include "GLContextProvider.h"
#include "VsyncSource.h"
#include "base/message_loop.h"
#include "base/task.h"
#include "base/thread.h"
#include "cairo.h"
#include "gfx2DGlue.h"
#include "gfxConfig.h"
#include "gfxContext.h"
#include "gfxFT2FontBase.h"
#include "gfxFcPlatformFontList.h"
#include "gfxImageSurface.h"
#include "gfxTextRun.h"
#include "gfxUserFontSet.h"
#include "gfxUtils.h"
#include "mozilla/Components.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/Monitor.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/WidgetUtilsGtk.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Logging.h"
#include "nsAppRunner.h"
#include "nsIGfxInfo.h"
#include "nsMathUtils.h"
#include "nsUnicharUtils.h"
#include "nsUnicodeProperties.h"
#include "prenv.h"

#ifdef MOZ_X11
#  include <X11/extensions/Xrandr.h>
#  include <gdk/gdkx.h>

#  include "GLContextGLX.h"
#  include "GLXLibrary.h"
#  include "SoftwareVsyncSource.h"
#  include "cairo-xlib.h"
#  include "gfxXlibSurface.h"
#  include "mozilla/X11Util.h"
#  include "mozilla/gfx/XlibDisplay.h"

/* Undefine the Status from Xlib since it will conflict with system headers on
 * OSX */
#  if defined(__APPLE__) && defined(Status)
#    undef Status
#  endif
#endif /* MOZ_X11 */

#ifdef MOZ_WAYLAND
#  include <gdk/gdkwayland.h>

#  include "mozilla/widget/nsWaylandDisplay.h"
#endif
#ifdef MOZ_WIDGET_GTK
#  include "mozilla/StaticPrefs_widget.h"
#  include "mozilla/widget/DMABufDevice.h"
#endif

#define GDK_PIXMAP_SIZE_MAX 32767

#define GFX_PREF_MAX_GENERIC_SUBSTITUTIONS \
  "gfx.font_rendering.fontconfig.max_generic_substitutions"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::unicode;
using namespace mozilla::widget;

static FT_Library gPlatformFTLibrary = nullptr;

#if defined(MOZ_X11)
// TODO(aosmond): The envvar is deprecated. We should remove it once EGL is the
// default in release.
static bool IsX11EGLEnvvarEnabled() {
  const char* eglPref = PR_GetEnv("MOZ_X11_EGL");
  return (eglPref && *eglPref);
}
#endif

gfxPlatformGtk::gfxPlatformGtk() {
  if (!gfxPlatform::IsHeadless()) {
    if (!gtk_init_check(nullptr, nullptr)) {
      gfxCriticalNote << "Failed to init Gtk, missing display? DISPLAY="
                      << getenv("DISPLAY")
                      << " WAYLAND_DISPLAY=" << getenv("WAYLAND_DISPLAY")
                      << "\n";
      abort();
    }
  }

  mIsX11Display = gfxPlatform::IsHeadless() ? false : GdkIsX11Display();
  if (XRE_IsParentProcess()) {
    InitX11EGLConfig();
    if (IsWaylandDisplay() || gfxConfig::IsEnabled(Feature::X11_EGL)) {
      gfxVars::SetUseEGL(true);
    }
    InitDmabufConfig();
    if (gfxConfig::IsEnabled(Feature::DMABUF)) {
      gfxVars::SetUseDMABuf(true);
    }
  }

  InitBackendPrefs(GetBackendPrefs());

  gPlatformFTLibrary = Factory::NewFTLibrary();
  MOZ_RELEASE_ASSERT(gPlatformFTLibrary);
  Factory::SetFTLibrary(gPlatformFTLibrary);

  // Bug 1714483: Force disable FXAA Antialiasing on NV drivers. This is a
  // temporary workaround for a driver bug.
  PR_SetEnv("__GL_ALLOW_FXAA_USAGE=0");

  InitMesaThreading();
}

gfxPlatformGtk::~gfxPlatformGtk() {
  Factory::ReleaseFTLibrary(gPlatformFTLibrary);
  gPlatformFTLibrary = nullptr;
}

void gfxPlatformGtk::InitAcceleration() {
  gfxPlatform::InitAcceleration();

  if (XRE_IsContentProcess()) {
    ImportCachedContentDeviceData();
  }
}

void gfxPlatformGtk::InitX11EGLConfig() {
  FeatureState& feature = gfxConfig::GetFeature(Feature::X11_EGL);
#ifdef MOZ_X11
  feature.EnableByDefault();

  if (StaticPrefs::gfx_x11_egl_force_enabled_AtStartup()) {
    feature.UserForceEnable("Force enabled by pref");
  } else if (IsX11EGLEnvvarEnabled()) {
    feature.UserForceEnable("Force enabled by envvar");
  } else if (StaticPrefs::gfx_x11_egl_force_disabled_AtStartup()) {
    feature.UserDisable("Force disabled by pref",
                        "FEATURE_FAILURE_USER_FORCE_DISABLED"_ns);
  }

  nsCString failureId;
  int32_t status;
  nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
  if (NS_FAILED(gfxInfo->GetFeatureStatus(nsIGfxInfo::FEATURE_X11_EGL,
                                          failureId, &status))) {
    feature.Disable(FeatureStatus::BlockedNoGfxInfo, "gfxInfo is broken",
                    "FEATURE_FAILURE_NO_GFX_INFO"_ns);
  } else if (status != nsIGfxInfo::FEATURE_STATUS_OK) {
    feature.Disable(FeatureStatus::Blocklisted, "Blocklisted by gfxInfo",
                    failureId);
  }

  nsAutoString testType;
  gfxInfo->GetTestType(testType);
  // We can only use X11/EGL if we actually found the EGL library and
  // successfully use it to determine system information in glxtest.
  if (testType != u"EGL") {
    feature.ForceDisable(FeatureStatus::Broken, "glxtest could not use EGL",
                         "FEATURE_FAILURE_GLXTEST_NO_EGL"_ns);
  }
#else
  feature.DisableByDefault(FeatureStatus::Unavailable, "X11 support missing",
                           "FEATURE_FAILURE_NO_X11"_ns);
#endif
}

void gfxPlatformGtk::InitDmabufConfig() {
  FeatureState& feature = gfxConfig::GetFeature(Feature::DMABUF);
  feature.EnableByDefault();

  nsCString failureId;
  int32_t status;
  nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
  if (NS_FAILED(gfxInfo->GetFeatureStatus(nsIGfxInfo::FEATURE_DMABUF, failureId,
                                          &status))) {
    feature.Disable(FeatureStatus::BlockedNoGfxInfo, "gfxInfo is broken",
                    "FEATURE_FAILURE_NO_GFX_INFO"_ns);
  } else if (status != nsIGfxInfo::FEATURE_STATUS_OK) {
    feature.Disable(FeatureStatus::Blocklisted, "Blocklisted by gfxInfo",
                    failureId);
  }

  if (StaticPrefs::widget_dmabuf_force_enabled_AtStartup()) {
    feature.UserForceEnable("Force enabled by pref");
  } else if (!StaticPrefs::widget_dmabuf_enabled_AtStartup()) {
    feature.UserDisable("Force disable by pref",
                        "FEATURE_FAILURE_USER_FORCE_DISABLED"_ns);
  }

  if (!gfxVars::UseEGL()) {
    feature.ForceDisable(FeatureStatus::Unavailable, "Requires EGL",
                         "FEATURE_FAILURE_REQUIRES_EGL"_ns);
  }

  if (!gfxVars::WebglUseHardware()) {
    feature.Disable(FeatureStatus::Blocklisted,
                    "DMABuf disabled with software rendering", failureId);
  }

  nsAutoCString drmRenderDevice;
  gfxInfo->GetDrmRenderDevice(drmRenderDevice);
  gfxVars::SetDrmRenderDevice(drmRenderDevice);

  if (feature.IsEnabled()) {
    DMABufDeviceLock device;
    if (!device.GetDMABufDevice()->IsEnabled(failureId)) {
      feature.ForceDisable(FeatureStatus::Failed, "Failed to configure",
                           failureId);
    }
    // Make sure we have DMABuf formats available.
    (void)GetGlobalDMABufFormats();
  }
}

void gfxPlatformGtk::InitPlatformHardwareVideoConfig() {
  FeatureState& featureDec =
      gfxConfig::GetFeature(Feature::HARDWARE_VIDEO_DECODING);
  if (!gfxVars::UseEGL()) {
    featureDec.ForceDisable(FeatureStatus::Unavailable, "Requires EGL",
                            "FEATURE_FAILURE_REQUIRES_EGL"_ns);
    gfxConfig::ForceDisable(Feature::HARDWARE_VIDEO_ENCODING,
                            FeatureStatus::Unavailable, "Requires EGL",
                            "FEATURE_FAILURE_REQUIRES_EGL"_ns);
  }

  if (!featureDec.IsEnabled()) {
    return;
  }

  // Configure zero-copy playback feature.
  FeatureState& featureZeroCopy =
      gfxConfig::GetFeature(Feature::HW_DECODED_VIDEO_ZERO_COPY);

  featureZeroCopy.EnableByDefault();
  uint32_t state =
      StaticPrefs::media_ffmpeg_vaapi_force_surface_zero_copy_AtStartup();
  if (state == 0) {
    featureZeroCopy.UserDisable("Force disable by pref",
                                "FEATURE_FAILURE_USER_FORCE_DISABLED"_ns);
  } else if (state == 1) {
    featureZeroCopy.UserEnable("Force enabled by pref");
  } else {
    nsCString failureId;
    int32_t status = nsIGfxInfo::FEATURE_STATUS_UNKNOWN;
    nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
    if (NS_FAILED(gfxInfo->GetFeatureStatus(
            nsIGfxInfo::FEATURE_HW_DECODED_VIDEO_ZERO_COPY, failureId,
            &status))) {
      featureZeroCopy.Disable(FeatureStatus::BlockedNoGfxInfo,
                              "gfxInfo is broken",
                              "FEATURE_FAILURE_NO_GFX_INFO"_ns);
    } else if (status == nsIGfxInfo::FEATURE_BLOCKED_PLATFORM_TEST) {
      featureZeroCopy.ForceDisable(FeatureStatus::Unavailable,
                                   "Force disabled by gfxInfo", failureId);
    } else if (status != nsIGfxInfo::FEATURE_ALLOW_ALWAYS) {
      featureZeroCopy.Disable(FeatureStatus::Blocklisted,
                              "Blocklisted by gfxInfo", failureId);
    }
  }
  if (featureZeroCopy.IsEnabled()) {
    gfxVars::SetHwDecodedVideoZeroCopy(true);
  }
}

void gfxPlatformGtk::InitWebRenderConfig() {
  gfxPlatform::InitWebRenderConfig();

  if (!XRE_IsParentProcess()) {
    return;
  }

  FeatureState& feature = gfxConfig::GetFeature(Feature::WEBRENDER_COMPOSITOR);
  // HDR requires compositor to work
#if defined(MOZ_WAYLAND)
  if (feature.IsEnabled()) {
    if (!IsWaylandDisplay()) {
      feature.ForceDisable(FeatureStatus::Unavailable,
                           "Wayland support missing",
                           "FEATURE_FAILURE_NO_WAYLAND"_ns);
    } else if (gfxConfig::IsEnabled(Feature::WEBRENDER) &&
               !gfxConfig::IsEnabled(Feature::DMABUF)) {
      // We use zwp_linux_dmabuf_v1 and GBM directly to manage FBOs. In theory
      // this is also possible vie EGLstreams, but we don't bother to implement
      // it as recent NVidia drivers support GBM and DMABuf as well.
      feature.ForceDisable(FeatureStatus::Unavailable,
                           "Hardware Webrender requires DMAbuf support",
                           "FEATURE_FAILURE_NO_DMABUF"_ns);
    } else if (!widget::WaylandDisplayGet()->GetViewporter()) {
      feature.ForceDisable(FeatureStatus::Unavailable,
                           "Requires wp_viewporter protocol support",
                           "FEATURE_FAILURE_REQUIRES_WPVIEWPORTER"_ns);
    }
  }
#else  // MOZ_WAYLAND
  feature.ForceDisable(FeatureStatus::Unavailable, "Not available on X11",
                       "FEATURE_FAILURE_NO_WAYLAND"_ns);
#endif

  gfxVars::SetUseWebRenderCompositor(feature.IsEnabled());
}

void gfxPlatformGtk::InitPlatformGPUProcessPrefs() {
#ifdef MOZ_WAYLAND
  if (IsWaylandDisplay()) {
    FeatureState& gpuProc = gfxConfig::GetFeature(Feature::GPU_PROCESS);
    gpuProc.ForceDisable(FeatureStatus::Blocked,
                         "Wayland does not work in the GPU process",
                         "FEATURE_FAILURE_WAYLAND"_ns);
  }
#endif
}

void gfxPlatformGtk::InitMesaThreading() {
  FeatureState& featureMesaThreading =
      gfxConfig::GetFeature(Feature::MESA_THREADING);
  featureMesaThreading.EnableByDefault();

  nsCString failureId;
  int32_t status;
  nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
  if (NS_FAILED(gfxInfo->GetFeatureStatus(nsIGfxInfo::FEATURE_MESA_THREADING,
                                          failureId, &status))) {
    featureMesaThreading.Disable(FeatureStatus::BlockedNoGfxInfo,
                                 "gfxInfo is broken",
                                 "FEATURE_FAILURE_NO_GFX_INFO"_ns);
  } else if (status != nsIGfxInfo::FEATURE_STATUS_OK) {
    featureMesaThreading.Disable(FeatureStatus::Blocklisted,
                                 "Blocklisted by gfxInfo", failureId);
  }

  // Enabling glthread crashes on X11/EGL, see bug 1670545
  if (gfxConfig::IsEnabled(Feature::X11_EGL) && IsX11Display()) {
    featureMesaThreading.Disable(FeatureStatus::Failed,
                                 "No glthread with EGL and X11",
                                 "FEATURE_FAILURE_EGL_X11"_ns);
  }

  if (!featureMesaThreading.IsEnabled()) {
    PR_SetEnv("mesa_glthread=false");
  }
}

already_AddRefed<gfxASurface> gfxPlatformGtk::CreateOffscreenSurface(
    const IntSize& aSize, gfxImageFormat aFormat) {
  if (!Factory::AllowedSurfaceSize(aSize)) {
    return nullptr;
  }

  RefPtr<gfxASurface> newSurface;
  bool needsClear = true;
  // XXX we really need a different interface here, something that passes
  // in more context, including the display and/or target surface type that
  // we should try to match
  GdkScreen* gdkScreen = gdk_screen_get_default();
  if (gdkScreen) {
    newSurface = new gfxImageSurface(aSize, aFormat);
    // The gfxImageSurface ctor zeroes this for us, no need to
    // waste time clearing again
    needsClear = false;
  }

  if (!newSurface) {
    // We couldn't create a native surface for whatever reason;
    // e.g., no display, no RENDER, bad size, etc.
    // Fall back to image surface for the data.
    newSurface = new gfxImageSurface(aSize, aFormat);
  }

  if (newSurface->CairoStatus()) {
    newSurface = nullptr;  // surface isn't valid for some reason
  }

  if (newSurface && needsClear) {
    gfxUtils::ClearThebesSurface(newSurface);
  }

  return newSurface.forget();
}

nsresult gfxPlatformGtk::GetFontList(nsAtom* aLangGroup,
                                     const nsACString& aGenericFamily,
                                     nsTArray<nsString>& aListOfFonts) {
  gfxPlatformFontList::PlatformFontList()->GetFontList(
      aLangGroup, aGenericFamily, aListOfFonts);
  return NS_OK;
}

// xxx - this is ubuntu centric, need to go through other distros and flesh
// out a more general list
static const char kFontDejaVuSans[] = "DejaVu Sans";
static const char kFontDejaVuSerif[] = "DejaVu Serif";
static const char kFontFreeSans[] = "FreeSans";
static const char kFontFreeSerif[] = "FreeSerif";
static const char kFontTakaoPGothic[] = "TakaoPGothic";
static const char kFontTwemojiMozilla[] = "Twemoji Mozilla";
static const char kFontDroidSansFallback[] = "Droid Sans Fallback";
static const char kFontWenQuanYiMicroHei[] = "WenQuanYi Micro Hei";
static const char kFontNanumGothic[] = "NanumGothic";
static const char kFontSymbola[] = "Symbola";
static const char kFontNotoSansSymbols[] = "Noto Sans Symbols";
static const char kFontNotoSansSymbols2[] = "Noto Sans Symbols2";

void gfxPlatformGtk::GetCommonFallbackFonts(uint32_t aCh, Script aRunScript,
                                            FontPresentation aPresentation,
                                            nsTArray<const char*>& aFontList) {
  if (PrefersColor(aPresentation)) {
    aFontList.AppendElement(kFontTwemojiMozilla);
  }

  aFontList.AppendElement(kFontDejaVuSerif);
  aFontList.AppendElement(kFontFreeSerif);
  aFontList.AppendElement(kFontDejaVuSans);
  aFontList.AppendElement(kFontFreeSans);
  aFontList.AppendElement(kFontSymbola);
  aFontList.AppendElement(kFontNotoSansSymbols);
  aFontList.AppendElement(kFontNotoSansSymbols2);

  // add fonts for CJK ranges
  // xxx - this isn't really correct, should use the same CJK font ordering
  // as the pref font code
  if (aCh >= 0x3000 && ((aCh < 0xe000) || (aCh >= 0xf900 && aCh < 0xfff0) ||
                        ((aCh >> 16) == 2))) {
    aFontList.AppendElement(kFontTakaoPGothic);
    aFontList.AppendElement(kFontDroidSansFallback);
    aFontList.AppendElement(kFontWenQuanYiMicroHei);
    aFontList.AppendElement(kFontNanumGothic);
  }
}

void gfxPlatformGtk::ReadSystemFontList(
    mozilla::dom::SystemFontList* retValue) {
  gfxFcPlatformFontList::PlatformFontList()->ReadSystemFontList(retValue);
}

bool gfxPlatformGtk::CreatePlatformFontList() {
  return gfxPlatformFontList::Initialize(new gfxFcPlatformFontList);
}

gfxImageFormat gfxPlatformGtk::GetOffscreenFormat() {
  // Make sure there is a screen
  GdkScreen* screen = gdk_screen_get_default();
  if (screen && gdk_visual_get_depth(gdk_visual_get_system()) == 16) {
    return SurfaceFormat::R5G6B5_UINT16;
  }

  return SurfaceFormat::X8R8G8B8_UINT32;
}

void gfxPlatformGtk::FontsPrefsChanged(const char* aPref) {
  // only checking for generic substitions, pass other changes up
  if (strcmp(GFX_PREF_MAX_GENERIC_SUBSTITUTIONS, aPref) != 0) {
    gfxPlatform::FontsPrefsChanged(aPref);
    return;
  }

  gfxFcPlatformFontList* pfl = gfxFcPlatformFontList::PlatformFontList();
  pfl->ClearGenericMappings();
  FlushFontAndWordCaches();
}

bool gfxPlatformGtk::AccelerateLayersByDefault() { return true; }

#if defined(MOZ_X11)

static nsTArray<uint8_t> GetDisplayICCProfile(Display* dpy, Window& root) {
  const char kIccProfileAtomName[] = "_ICC_PROFILE";
  Atom iccAtom = XInternAtom(dpy, kIccProfileAtomName, TRUE);
  if (!iccAtom) {
    return nsTArray<uint8_t>();
  }

  Atom retAtom;
  int retFormat;
  unsigned long retLength, retAfter;
  unsigned char* retProperty;

  if (XGetWindowProperty(dpy, root, iccAtom, 0, INT_MAX /* length */, X11False,
                         AnyPropertyType, &retAtom, &retFormat, &retLength,
                         &retAfter, &retProperty) != X11Success) {
    return nsTArray<uint8_t>();
  }

  nsTArray<uint8_t> result;

  if (retLength > 0) {
    result.AppendElements(static_cast<uint8_t*>(retProperty), retLength);
  }

  XFree(retProperty);

  return result;
}

nsTArray<uint8_t> gfxPlatformGtk::GetPlatformCMSOutputProfileData() {
  nsTArray<uint8_t> prefProfileData = GetPrefCMSOutputProfileData();
  if (!prefProfileData.IsEmpty()) {
    return prefProfileData;
  }

  if (XRE_IsContentProcess()) {
    auto& cmsOutputProfileData = GetCMSOutputProfileData();
    // We should have set our profile data when we received our initial
    // ContentDeviceData.
    MOZ_ASSERT(cmsOutputProfileData.isSome(),
               "Should have created output profile data when we received "
               "initial content device data.");
    if (cmsOutputProfileData.isSome()) {
      return cmsOutputProfileData.ref().Clone();
    }
    return nsTArray<uint8_t>();
  }

  if (!mIsX11Display) {
    return nsTArray<uint8_t>();
  }

  GdkDisplay* display = gdk_display_get_default();
  Display* dpy = GDK_DISPLAY_XDISPLAY(display);
  // In xpcshell tests, we never initialize X and hence don't have a Display.
  // In this case, there's no output colour management to be done, so we just
  // return with nullptr.
  if (!dpy) {
    return nsTArray<uint8_t>();
  }

  Window root = gdk_x11_get_default_root_xwindow();

  // First try ICC Profile
  nsTArray<uint8_t> iccResult = GetDisplayICCProfile(dpy, root);
  if (!iccResult.IsEmpty()) {
    return iccResult;
  }

  // If ICC doesn't work, then try EDID
  const char kEdid1AtomName[] = "XFree86_DDC_EDID1_RAWDATA";
  Atom edidAtom = XInternAtom(dpy, kEdid1AtomName, TRUE);
  if (!edidAtom) {
    return nsTArray<uint8_t>();
  }

  Atom retAtom;
  int retFormat;
  unsigned long retLength, retAfter;
  unsigned char* retProperty;

  if (XGetWindowProperty(dpy, root, edidAtom, 0, 32, X11False, AnyPropertyType,
                         &retAtom, &retFormat, &retLength, &retAfter,
                         &retProperty) != X11Success) {
    return nsTArray<uint8_t>();
  }

  if (retLength != 128) {
    return nsTArray<uint8_t>();
  }

  // Format documented in "VESA E-EDID Implementation Guide"
  float gamma = (100 + (float)retProperty[0x17]) / 100.0f;

  qcms_CIE_xyY whitePoint;
  whitePoint.x =
      ((retProperty[0x21] << 2) | (retProperty[0x1a] >> 2 & 3)) / 1024.0;
  whitePoint.y =
      ((retProperty[0x22] << 2) | (retProperty[0x1a] >> 0 & 3)) / 1024.0;
  whitePoint.Y = 1.0;

  qcms_CIE_xyYTRIPLE primaries;
  primaries.red.x =
      ((retProperty[0x1b] << 2) | (retProperty[0x19] >> 6 & 3)) / 1024.0;
  primaries.red.y =
      ((retProperty[0x1c] << 2) | (retProperty[0x19] >> 4 & 3)) / 1024.0;
  primaries.red.Y = 1.0;

  primaries.green.x =
      ((retProperty[0x1d] << 2) | (retProperty[0x19] >> 2 & 3)) / 1024.0;
  primaries.green.y =
      ((retProperty[0x1e] << 2) | (retProperty[0x19] >> 0 & 3)) / 1024.0;
  primaries.green.Y = 1.0;

  primaries.blue.x =
      ((retProperty[0x1f] << 2) | (retProperty[0x1a] >> 6 & 3)) / 1024.0;
  primaries.blue.y =
      ((retProperty[0x20] << 2) | (retProperty[0x1a] >> 4 & 3)) / 1024.0;
  primaries.blue.Y = 1.0;

  XFree(retProperty);

  void* mem = nullptr;
  size_t size = 0;
  qcms_data_create_rgb_with_gamma(whitePoint, primaries, gamma, &mem, &size);
  if (!mem) {
    return nsTArray<uint8_t>();
  }

  nsTArray<uint8_t> result;
  result.AppendElements(static_cast<uint8_t*>(mem), size);
  free(mem);

  // XXX: It seems like we get wrong colors when using this constructed profile:
  // See bug 1696819. For now just forget that we made it.
  return nsTArray<uint8_t>();
}

#else  // defined(MOZ_X11)

nsTArray<uint8_t> gfxPlatformGtk::GetPlatformCMSOutputProfileData() {
  return nsTArray<uint8_t>();
}

#endif

bool gfxPlatformGtk::CheckVariationFontSupport() {
  // Although there was some variation/multiple-master support in FreeType
  // in older versions, it seems too incomplete/unstable for us to use
  // until at least 2.7.1.
  FT_Int major, minor, patch;
  FT_Library_Version(Factory::GetFTLibrary(), &major, &minor, &patch);
  return major * 1000000 + minor * 1000 + patch >= 2007001;
}

#ifdef MOZ_X11

class GtkVsyncSource final : public VsyncSource {
 public:
  GtkVsyncSource()
      : mGLContext(nullptr),
        mXDisplay(nullptr),
        mSetupLock("GLXVsyncSetupLock"),
        mVsyncThread("GLXVsyncThread"),
        mVsyncTask(nullptr),
        mVsyncEnabledLock("GLXVsyncEnabledLock"),
        mVsyncEnabled(false) {
    MOZ_ASSERT(NS_IsMainThread());
  }

  virtual ~GtkVsyncSource() { MOZ_ASSERT(NS_IsMainThread()); }

  // Sets up the display's GL context on a worker thread.
  // Required as GLContexts may only be used by the creating thread.
  // Returns true if setup was a success.
  bool Setup() {
    MonitorAutoLock lock(mSetupLock);
    MOZ_ASSERT(NS_IsMainThread());
    if (!mVsyncThread.Start()) return false;

    RefPtr<Runnable> vsyncSetup =
        NewRunnableMethod("GtkVsyncSource::SetupGLContext", this,
                          &GtkVsyncSource::SetupGLContext);
    mVsyncThread.message_loop()->PostTask(vsyncSetup.forget());
    // Wait until the setup has completed.
    lock.Wait();
    return mGLContext != nullptr;
  }

  // Called on the Vsync thread to setup the GL context.
  void SetupGLContext() {
    MonitorAutoLock lock(mSetupLock);
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_ASSERT(!mGLContext, "GLContext already setup!");

    // Create video sync timer on a separate Display to prevent locking the
    // main thread X display.
    mXDisplay = XOpenDisplay(nullptr);
    if (!mXDisplay) {
      lock.NotifyAll();
      return;
    }

    // Most compositors wait for vsync events on the root window.
    Window root = DefaultRootWindow(mXDisplay);
    int screen = DefaultScreen(mXDisplay);

    GLXFBConfig config;
    int visid;
    bool forWebRender = false;
    if (!gl::GLContextGLX::FindFBConfigForWindow(
            mXDisplay, screen, root, &config, &visid, forWebRender)) {
      lock.NotifyAll();
      return;
    }

    mGLContext = gl::GLContextGLX::CreateGLContext(
        {}, gfx::XlibDisplay::Borrow(mXDisplay), root, config);

    if (!mGLContext) {
      lock.NotifyAll();
      return;
    }

    mGLContext->MakeCurrent();

    // Test that SGI_video_sync lets us get the counter.
    unsigned int syncCounter = 0;
    if (gl::sGLXLibrary.fGetVideoSync(&syncCounter) != 0) {
      mGLContext = nullptr;
    }

    lock.NotifyAll();
  }

  virtual void EnableVsync() override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mGLContext, "GLContext not setup!");

    MonitorAutoLock lock(mVsyncEnabledLock);
    if (mVsyncEnabled) {
      return;
    }
    mVsyncEnabled = true;

    // If the task has not nulled itself out, it hasn't yet realized
    // that vsync was disabled earlier, so continue its execution.
    if (!mVsyncTask) {
      mVsyncTask = NewRunnableMethod("GtkVsyncSource::RunVsync", this,
                                     &GtkVsyncSource::RunVsync);
      RefPtr<Runnable> addrefedTask = mVsyncTask;
      mVsyncThread.message_loop()->PostTask(addrefedTask.forget());
    }
  }

  virtual void DisableVsync() override {
    MonitorAutoLock lock(mVsyncEnabledLock);
    mVsyncEnabled = false;
  }

  virtual bool IsVsyncEnabled() override {
    MonitorAutoLock lock(mVsyncEnabledLock);
    return mVsyncEnabled;
  }

  virtual void Shutdown() override {
    MOZ_ASSERT(NS_IsMainThread());
    DisableVsync();

    // Cleanup thread-specific resources before shutting down.
    RefPtr<Runnable> shutdownTask = NewRunnableMethod(
        "GtkVsyncSource::Cleanup", this, &GtkVsyncSource::Cleanup);
    mVsyncThread.message_loop()->PostTask(shutdownTask.forget());

    // Stop, waiting for the cleanup task to finish execution.
    mVsyncThread.Stop();
  }

 private:
  void RunVsync() {
    MOZ_ASSERT(!NS_IsMainThread());

    mGLContext->MakeCurrent();

    unsigned int syncCounter = 0;
    gl::sGLXLibrary.fGetVideoSync(&syncCounter);
    for (;;) {
      {
        MonitorAutoLock lock(mVsyncEnabledLock);
        if (!mVsyncEnabled) {
          mVsyncTask = nullptr;
          return;
        }
      }

      TimeStamp lastVsync = TimeStamp::Now();
      bool useSoftware = false;

      // Wait until the video sync counter reaches the next value by waiting
      // until the parity of the counter value changes.
      unsigned int nextSync = syncCounter + 1;
      int status;
      if ((status = gl::sGLXLibrary.fWaitVideoSync(2, (int)nextSync % 2,
                                                   &syncCounter)) != 0) {
        gfxWarningOnce() << "glXWaitVideoSync returned " << status;
        useSoftware = true;
      }

      if (syncCounter == (nextSync - 1)) {
        gfxWarningOnce()
            << "glXWaitVideoSync failed to increment the sync counter.";
        useSoftware = true;
      }

      if (useSoftware) {
        double remaining =
            (1000.f / 60.f) - (TimeStamp::Now() - lastVsync).ToMilliseconds();
        if (remaining > 0) {
          AUTO_PROFILER_THREAD_SLEEP;
          PlatformThread::Sleep((int)remaining);
        }
      }

      lastVsync = TimeStamp::Now();
      TimeStamp outputTime = lastVsync + GetVsyncRate();
      NotifyVsync(lastVsync, outputTime);
    }
  }

  void Cleanup() {
    MOZ_ASSERT(!NS_IsMainThread());

    mGLContext = nullptr;
    if (mXDisplay) XCloseDisplay(mXDisplay);
  }

  // Owned by the vsync thread.
  RefPtr<gl::GLContextGLX> mGLContext;
  _XDisplay* mXDisplay;
  Monitor mSetupLock MOZ_UNANNOTATED;
  base::Thread mVsyncThread;
  RefPtr<Runnable> mVsyncTask;
  Monitor mVsyncEnabledLock MOZ_UNANNOTATED;
  bool mVsyncEnabled;
};

class XrandrSoftwareVsyncSource final
    : public mozilla::gfx::SoftwareVsyncSource {
 public:
  XrandrSoftwareVsyncSource() : SoftwareVsyncSource(ComputeVsyncRate()) {
    MOZ_ASSERT(NS_IsMainThread());

    GdkScreen* defaultScreen = gdk_screen_get_default();
    g_signal_connect(defaultScreen, "monitors-changed",
                     G_CALLBACK(monitors_changed), this);
  }

 private:
  // Request the current refresh rate via xrandr. It is hard to find the
  // "correct" one, thus choose the highest one, assuming this will usually
  // give the best user experience.
  static mozilla::TimeDuration ComputeVsyncRate() {
    struct _XDisplay* dpy = gdk_x11_get_default_xdisplay();

    // Use the default software refresh rate as lower bound. Allowing lower
    // rates makes a bunch of tests start to fail on CI. The main goal of this
    // VsyncSource is to support refresh rates greater than the default one.
    double highestRefreshRate = gfxPlatform::GetSoftwareVsyncRate();

    // When running on remote X11 the xrandr version may be stuck on an
    // ancient version. There are still setups using remote X11 out there, so
    // make sure we don't crash.
    int eventBase, errorBase, major, minor;
    if (XRRQueryExtension(dpy, &eventBase, &errorBase) &&
        XRRQueryVersion(dpy, &major, &minor) &&
        (major > 1 || (major == 1 && minor >= 3))) {
      Window root = gdk_x11_get_default_root_xwindow();
      XRRScreenResources* res = XRRGetScreenResourcesCurrent(dpy, root);

      if (res) {
        // We can't use refresh rates far below the default one (60Hz) because
        // otherwise random CI tests start to fail. However, many users have
        // screens just below the default rate, e.g. 59.95Hz. So slightly
        // decrease the lower bound.
        highestRefreshRate -= 1.0;

        for (int i = 0; i < res->noutput; i++) {
          XRROutputInfo* outputInfo =
              XRRGetOutputInfo(dpy, res, res->outputs[i]);
          if (outputInfo) {
            if (outputInfo->crtc) {
              XRRCrtcInfo* crtcInfo =
                  XRRGetCrtcInfo(dpy, res, outputInfo->crtc);
              if (crtcInfo) {
                for (int j = 0; j < res->nmode; j++) {
                  if (res->modes[j].id == crtcInfo->mode) {
                    double refreshRate = mode_refresh(&res->modes[j]);
                    if (refreshRate > highestRefreshRate) {
                      highestRefreshRate = refreshRate;
                    }
                    break;
                  }
                }

                XRRFreeCrtcInfo(crtcInfo);
              }
            }

            XRRFreeOutputInfo(outputInfo);
          }
        }
      }
      XRRFreeScreenResources(res);
    }

    const double rate = 1000.0 / highestRefreshRate;
    return mozilla::TimeDuration::FromMilliseconds(rate);
  }

  static void monitors_changed(GdkScreen* aScreen, gpointer aClosure) {
    XrandrSoftwareVsyncSource* self =
        static_cast<XrandrSoftwareVsyncSource*>(aClosure);
    self->SetVsyncRate(ComputeVsyncRate());
  }

  // from xrandr.c
  static double mode_refresh(const XRRModeInfo* mode_info) {
    double rate;
    double vTotal = mode_info->vTotal;

    if (mode_info->modeFlags & RR_DoubleScan) {
      /* doublescan doubles the number of lines */
      vTotal *= 2;
    }

    if (mode_info->modeFlags & RR_Interlace) {
      /* interlace splits the frame into two fields */
      /* the field rate is what is typically reported by monitors */
      vTotal /= 2;
    }

    if (mode_info->hTotal && vTotal) {
      rate = ((double)mode_info->dotClock /
              ((double)mode_info->hTotal * (double)vTotal));
    } else {
      rate = 0;
    }
    return rate;
  }
};
#endif

already_AddRefed<gfx::VsyncSource>
gfxPlatformGtk::CreateGlobalHardwareVsyncSource() {
#ifdef MOZ_X11
  if (IsHeadless() || IsWaylandDisplay()) {
    // On Wayland we can not create a global hardware based vsync source, thus
    // use a software based one here. We create window specific ones later.
    return GetSoftwareVsyncSource();
  }

  nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
  nsString windowProtocol;
  gfxInfo->GetWindowProtocol(windowProtocol);
  bool isXwayland = windowProtocol.Find(u"xwayland") != -1;
  nsString adapterDriverVendor;
  gfxInfo->GetAdapterDriverVendor(adapterDriverVendor);
  bool isMesa = adapterDriverVendor.Find(u"mesa") != -1;

  // Only use GLX vsync when the OpenGL compositor / WebRender is being used.
  // The extra cost of initializing a GLX context while blocking the main thread
  // is not worth it when using basic composition. Do not use it on Xwayland, as
  // Xwayland will give us a software timer as we are listening for the root
  // window, which does not have a Wayland equivalent. Don't call
  // gl::sGLXLibrary.SupportsVideoSync() when EGL is used as NVIDIA drivers
  // refuse to use EGL GL context when GLX was initialized first and fail
  // silently.
  if (StaticPrefs::gfx_x11_glx_sgi_video_sync_AtStartup() &&
      gfxConfig::IsEnabled(Feature::HW_COMPOSITING) && !isXwayland &&
      (!gfxVars::UseEGL() || isMesa) &&
      gl::sGLXLibrary.SupportsVideoSync(DefaultXDisplay())) {
    RefPtr<GtkVsyncSource> vsyncSource = new GtkVsyncSource();
    if (!vsyncSource->Setup()) {
      NS_WARNING("Failed to setup GLContext, falling back to software vsync.");
      return GetSoftwareVsyncSource();
    }
    return vsyncSource.forget();
  }

  RefPtr<VsyncSource> softwareVsync = new XrandrSoftwareVsyncSource();
  return softwareVsync.forget();
#else
  return GetSoftwareVsyncSource();
#endif
}

void gfxPlatformGtk::BuildContentDeviceData(ContentDeviceData* aOut) {
  gfxPlatform::BuildContentDeviceData(aOut);

  aOut->cmsOutputProfileData() = GetPlatformCMSOutputProfileData();
}

// Wrapper for third party code (WebRTC for instance) where
// gfxVars can't be included.
namespace mozilla::gfx {
bool IsDMABufEnabled() { return gfxVars::UseDMABuf(); }
}  // namespace mozilla::gfx
