/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GLLibraryEGL.h"

#include "gfxConfig.h"
#include "gfxCrashReporterUtils.h"
#include "gfxEnv.h"
#include "gfxUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/Preferences.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/Tokenizer.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/glean/DomCanvasMetrics.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsPrintfCString.h"
#ifdef XP_WIN
#  include <d3d11.h>

#  include "mozilla/gfx/DeviceManagerDx.h"
#  include "nsWindowsHelpers.h"
#  include "prerror.h"
#endif
#include "GLContext.h"
#include "GLContextProvider.h"
#include "GLLibraryLoader.h"
#include "GLReadTexImageHelper.h"
#include "OGLShaderProgram.h"
#include "ScopedGLHelpers.h"
#include "prenv.h"
#include "prsystem.h"
#ifdef MOZ_WIDGET_GTK
#  include "mozilla/WidgetUtilsGtk.h"
#  include "mozilla/widget/DMABufDevice.h"
#  ifdef MOZ_WAYLAND
#    include "mozilla/widget/nsWaylandDisplay.h"
#  endif  // MOZ_WIDGET_GTK
#  include <gdk/gdk.h>
#endif  // MOZ_WAYLAND

#include <mutex>  // for call_once

namespace mozilla {
namespace gl {

StaticMutex GLLibraryEGL::sMutex;
StaticRefPtr<GLLibraryEGL> GLLibraryEGL::sInstance;
StaticRefPtr<GLLibraryEGL> GLLibraryEGL::sNativeInstance;

// should match the order of EGLLibExtension, and be null-terminated.
static const char* sEGLLibraryExtensionNames[] = {
    "EGL_ANDROID_get_native_client_buffer",
    "EGL_ANGLE_device_creation",
    "EGL_ANGLE_device_creation_d3d11",
    "EGL_ANGLE_display_power_preference",
    "EGL_ANGLE_platform_angle",
    "EGL_ANGLE_platform_angle_d3d",
    "EGL_ANGLE_platform_angle_metal",
    "EGL_ANGLE_platform_angle_opengl",
    "EGL_ANGLE_platform_angle_display_key",
    "EGL_ANGLE_platform_angle_device_id",
    "EGL_EXT_device_enumeration",
    "EGL_EXT_device_query",
    "EGL_EXT_platform_device",
    "EGL_MESA_platform_surfaceless"};

// should match the order of EGLExtension, and be null-terminated.
static const char* sEGLExtensionNames[] = {
    "EGL_KHR_image_base",
    "EGL_KHR_image_pixmap",
    "EGL_KHR_gl_texture_2D_image",
    "EGL_ANGLE_surface_d3d_texture_2d_share_handle",
    "EGL_EXT_create_context_robustness",
    "EGL_KHR_image",
    "EGL_KHR_fence_sync",
    "EGL_KHR_wait_sync",
    "EGL_ANDROID_native_fence_sync",
    "EGL_ANDROID_image_crop",
    "EGL_ANGLE_d3d_share_handle_client_buffer",
    "EGL_KHR_create_context",
    "EGL_KHR_stream",
    "EGL_KHR_stream_consumer_gltexture",
    "EGL_NV_stream_consumer_gltexture_yuv",
    "EGL_ANGLE_stream_producer_d3d_texture",
    "EGL_KHR_surfaceless_context",
    "EGL_KHR_create_context_no_error",
    "EGL_EXT_swap_buffers_with_damage",
    "EGL_KHR_swap_buffers_with_damage",
    "EGL_EXT_buffer_age",
    "EGL_KHR_partial_update",
    "EGL_NV_robustness_video_memory_purge",
    "EGL_EXT_image_dma_buf_import",
    "EGL_EXT_image_dma_buf_import_modifiers",
    "EGL_MESA_image_dma_buf_export",
    "EGL_KHR_no_config_context",
    "EGL_ANGLE_iosurface_client_buffer",
    "EGL_ANGLE_metal_commands_scheduled_sync",
    "EGL_ANGLE_metal_shared_event_sync",
    "EGL_ANGLE_wait_until_work_scheduled",
};

// should match the order of EGLDeviceExtension, and be null-terminated.
static const char* sEGLDeviceExtensionNames[] = {
    "EGL_ANGLE_device_d3d",
    "EGL_ANGLE_device_metal",
};

PRLibrary* LoadApitraceLibrary() {
  const char* path = nullptr;

#ifdef ANDROID
  // We only need to explicitly dlopen egltrace
  // on android as we can use LD_PRELOAD or other tricks
  // on other platforms. We look for it in /data/local
  // as that's writeable by all users.
  path = "/data/local/tmp/egltrace.so";
#endif
  if (!path) return nullptr;

  // Initialization of gfx prefs here is only needed during the unit tests...
  if (!StaticPrefs::gfx_apitrace_enabled_AtStartup()) {
    return nullptr;
  }

  static PRLibrary* sApitraceLibrary = nullptr;
  if (sApitraceLibrary) return sApitraceLibrary;

  nsAutoCString logFile;
  Preferences::GetCString("gfx.apitrace.logfile", logFile);
  if (logFile.IsEmpty()) {
    logFile = "firefox.trace";
  }

  // The firefox process can't write to /data/local, but it can write
  // to $GRE_HOME/
  nsAutoCString logPath;
  logPath.AppendPrintf("%s/%s", getenv("GRE_HOME"), logFile.get());

#ifndef XP_WIN  // Windows is missing setenv and forbids PR_LoadLibrary.
  // apitrace uses the TRACE_FILE environment variable to determine where
  // to log trace output to
  printf_stderr("Logging GL tracing output to %s", logPath.get());
  setenv("TRACE_FILE", logPath.get(), false);

  printf_stderr("Attempting load of %s\n", path);
  sApitraceLibrary = PR_LoadLibrary(path);
#endif

  return sApitraceLibrary;
}

#ifdef XP_WIN
// see the comment in GLLibraryEGL::EnsureInitialized() for the rationale here.
static PRLibrary* LoadLibraryForEGLOnWindows(const nsAString& filename) {
  nsAutoString path(gfx::gfxVars::GREDirectory());
  path.Append(PR_GetDirectorySeparator());
  path.Append(filename);

  PRLibSpec lspec;
  lspec.type = PR_LibSpec_PathnameU;
  lspec.value.pathname_u = path.get();
  PRLibrary* lib = PR_LoadLibraryWithFlags(lspec, PR_LD_LAZY | PR_LD_LOCAL);
  if (!lib) {
    gfxCriticalNote << "Failed to load " << path.get() << " " << PR_GetError()
                    << " " << PR_GetOSError();
  }
  return lib;
}

#endif  // XP_WIN

static std::shared_ptr<EglDisplay> GetAndInitDisplay(
    GLLibraryEGL& egl, void* displayType,
    const StaticMutexAutoLock& aProofOfLock) {
  const auto display = egl.fGetDisplay(displayType);
  if (!display) return nullptr;
  return EglDisplay::Create(egl, display, false, aProofOfLock);
}

#ifdef MOZ_WIDGET_GTK
static std::shared_ptr<EglDisplay> GetAndInitDeviceDisplay(
    GLLibraryEGL& egl, const StaticMutexAutoLock& aProofOfLock) {
  nsAutoCString drmRenderDevice(gfx::gfxVars::DrmRenderDevice());
  if (drmRenderDevice.IsEmpty() ||
      !egl.IsExtensionSupported(EGLLibExtension::EXT_platform_device) ||
      !egl.IsExtensionSupported(EGLLibExtension::EXT_device_enumeration)) {
    return nullptr;
  }

  EGLint maxDevices;
  if (!egl.fQueryDevicesEXT(0, nullptr, &maxDevices)) {
    return nullptr;
  }

  std::vector<EGLDeviceEXT> devices(maxDevices);
  EGLint numDevices;
  if (!egl.fQueryDevicesEXT(devices.size(), devices.data(), &numDevices)) {
    return nullptr;
  }
  devices.resize(numDevices);

  EGLDisplay display = EGL_NO_DISPLAY;
  for (const auto& device : devices) {
    const char* renderNodeString =
        egl.fQueryDeviceStringEXT(device, LOCAL_EGL_DRM_RENDER_NODE_FILE_EXT);
    if (renderNodeString &&
        strcmp(renderNodeString, drmRenderDevice.get()) == 0) {
      const EGLAttrib attrib_list[] = {LOCAL_EGL_NONE};
      display = egl.fGetPlatformDisplay(LOCAL_EGL_PLATFORM_DEVICE_EXT, device,
                                        attrib_list);
      break;
    }
  }
  if (!display) {
    return nullptr;
  }

  return EglDisplay::Create(egl, display, true, aProofOfLock);
}

static std::shared_ptr<EglDisplay> GetAndInitSoftwareDisplay(
    GLLibraryEGL& egl, const StaticMutexAutoLock& aProofOfLock) {
  if (!egl.IsExtensionSupported(EGLLibExtension::EXT_platform_device) ||
      !egl.IsExtensionSupported(EGLLibExtension::EXT_device_enumeration)) {
    return nullptr;
  }

  EGLint maxDevices;
  if (!egl.fQueryDevicesEXT(0, nullptr, &maxDevices)) {
    return nullptr;
  }

  std::vector<EGLDeviceEXT> devices(maxDevices);
  EGLint numDevices;
  if (!egl.fQueryDevicesEXT(devices.size(), devices.data(), &numDevices)) {
    return nullptr;
  }
  devices.resize(numDevices);

  EGLDisplay display = EGL_NO_DISPLAY;
  for (const auto& device : devices) {
    const char* renderNodeString =
        egl.fQueryDeviceStringEXT(device, LOCAL_EGL_DRM_RENDER_NODE_FILE_EXT);
    // We are looking for a device with no file
    if (!renderNodeString || *renderNodeString == 0) {
      const EGLAttrib attrib_list[] = {LOCAL_EGL_NONE};
      display = egl.fGetPlatformDisplay(LOCAL_EGL_PLATFORM_DEVICE_EXT, device,
                                        attrib_list);
      break;
    }
  }
  if (!display) {
    return nullptr;
  }

  return EglDisplay::Create(egl, display, true, aProofOfLock);
}

static std::shared_ptr<EglDisplay> GetAndInitSurfacelessDisplay(
    GLLibraryEGL& egl, const StaticMutexAutoLock& aProofOfLock) {
  if (!egl.IsExtensionSupported(EGLLibExtension::MESA_platform_surfaceless)) {
    return nullptr;
  }

  const EGLAttrib attrib_list[] = {LOCAL_EGL_NONE};
  const EGLDisplay display = egl.fGetPlatformDisplay(
      LOCAL_EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, attrib_list);
  if (display == EGL_NO_DISPLAY) {
    return nullptr;
  }
  return EglDisplay::Create(egl, display, true, aProofOfLock);
}
#endif

static auto EglDebugLayersEnabled() {
  EGLAttrib ret = LOCAL_EGL_FALSE;
#ifdef XP_WIN
  if (StaticPrefs::gfx_direct3d11_enable_debug_layer_AtStartup()) {
    ret = LOCAL_EGL_TRUE;
  }
#endif
  return ret;
}

static std::shared_ptr<EglDisplay> GetAndInitWARPDisplay(
    GLLibraryEGL& egl, void* displayType,
    const StaticMutexAutoLock& aProofOfLock) {
  const EGLAttrib attrib_list[] = {
      LOCAL_EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
      LOCAL_EGL_PLATFORM_ANGLE_DEVICE_TYPE_WARP_ANGLE,
      LOCAL_EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE,
      EglDebugLayersEnabled(),
      // Requires:
      LOCAL_EGL_PLATFORM_ANGLE_TYPE_ANGLE,
      LOCAL_EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE, LOCAL_EGL_NONE};
  const EGLDisplay display = egl.fGetPlatformDisplay(
      LOCAL_EGL_PLATFORM_ANGLE_ANGLE, displayType, attrib_list);

  if (display == EGL_NO_DISPLAY) {
    const EGLint err = egl.fGetError();
    if (err != LOCAL_EGL_SUCCESS) {
      gfxCriticalError() << "Unexpected GL error: " << gfx::hexa(err);
      MOZ_CRASH("GFX: Unexpected GL error.");
    }
    return nullptr;
  }

  return EglDisplay::Create(egl, display, true, aProofOfLock);
}

std::shared_ptr<EglDisplay> GLLibraryEGL::CreateDisplay(
    ID3D11Device* const d3d11Device) {
  StaticMutexAutoLock lock(sMutex);
  EGLDeviceEXT eglDevice =
      fCreateDeviceANGLE(LOCAL_EGL_D3D11_DEVICE_ANGLE, d3d11Device, nullptr);
  if (!eglDevice) {
    gfxCriticalNote << "Failed to get EGLDeviceEXT of D3D11Device";
    return nullptr;
  }
  const char* features[] = {"allowES3OnFL10_0", nullptr};
  // Create an EGLDisplay using the EGLDevice
  const EGLAttrib attrib_list[] = {LOCAL_EGL_FEATURE_OVERRIDES_ENABLED_ANGLE,
                                   reinterpret_cast<EGLAttrib>(features),
                                   LOCAL_EGL_NONE};
  const auto display = fGetPlatformDisplay(LOCAL_EGL_PLATFORM_DEVICE_EXT,
                                           eglDevice, attrib_list);
  if (!display) {
    gfxCriticalNote << "Failed to get EGLDisplay of D3D11Device";
    return nullptr;
  }

  if (!display) {
    const EGLint err = fGetError();
    if (err != LOCAL_EGL_SUCCESS) {
      gfxCriticalError() << "Unexpected GL error: " << gfx::hexa(err);
      MOZ_CRASH("GFX: Unexpected GL error.");
    }
    return nullptr;
  }

  const auto ret = EglDisplay::Create(*this, display, false, lock);

  if (!ret) {
    const EGLint err = fGetError();
    if (err != LOCAL_EGL_SUCCESS) {
      gfxCriticalError()
          << "Failed to initialize EGLDisplay for WebRender error: "
          << gfx::hexa(err);
    }
    return nullptr;
  }
  return ret;
}

std::shared_ptr<EglDisplay> GLLibraryEGL::CreateDisplayForMetalDevice(
    uint64_t aMetalDeviceRegistryID) {
  StaticMutexAutoLock lock(sMutex);
  MOZ_ASSERT(IsExtensionSupported(EGLLibExtension::ANGLE_platform_angle_metal));
  MOZ_ASSERT(
      IsExtensionSupported(EGLLibExtension::ANGLE_platform_angle_device_id));

  const EGLAttrib attrib_list[] = {
      LOCAL_EGL_PLATFORM_ANGLE_TYPE_ANGLE,
      LOCAL_EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE,
      LOCAL_EGL_PLATFORM_ANGLE_DEVICE_ID_HIGH_ANGLE,
      static_cast<EGLAttrib>(aMetalDeviceRegistryID >> 32),
      LOCAL_EGL_PLATFORM_ANGLE_DEVICE_ID_LOW_ANGLE,
      static_cast<EGLAttrib>(aMetalDeviceRegistryID & 0xFFFFFFFF),
      LOCAL_EGL_NONE,
  };
  const EGLDisplay display = fGetPlatformDisplay(
      LOCAL_EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, attrib_list);
  if (!display) {
    return nullptr;
  }
  return EglDisplay::Create(*this, display, false, lock);
}  // namespace gl

#ifdef MOZ_WIDGET_ANDROID
std::shared_ptr<EglDisplay> GLLibraryEGL::CreateDisplayForWebRenderAndroidGLES(
    nsACString* const out_failureId) {
  StaticMutexAutoLock lock(sMutex);
  // Get() may return system EGL after failing to initialize bundled ANGLE.
  if (!IsANGLE() ||
      !IsExtensionSupported(EGLLibExtension::ANGLE_platform_angle_opengl)) {
    *out_failureId = "FEATURE_FAILURE_WR_ANGLE_GL_UNSUPPORTED"_ns;
    return nullptr;
  }
  if (!IsExtensionSupported(
          EGLLibExtension::ANGLE_platform_angle_display_key)) {
    *out_failureId = "FEATURE_FAILURE_WR_ANGLE_DISPLAY_KEY_UNSUPPORTED"_ns;
    return nullptr;
  }

  // WebGL uses the default key (0). Keep WebRender's display separate even
  // when both use the same ANGLE backend and underlying GPU.
  const EGLAttrib attribs[] = {LOCAL_EGL_PLATFORM_ANGLE_TYPE_ANGLE,
                               LOCAL_EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE,
                               LOCAL_EGL_PLATFORM_ANGLE_DISPLAY_KEY_ANGLE, 1,
                               LOCAL_EGL_NONE};
  const EGLDisplay display = fGetPlatformDisplay(LOCAL_EGL_PLATFORM_ANGLE_ANGLE,
                                                 EGL_DEFAULT_DISPLAY, attribs);
  if (!display) {
    *out_failureId = "FEATURE_FAILURE_WR_ANGLE_GET_DISPLAY"_ns;
    return nullptr;
  }
  auto egl = EglDisplay::Create(*this, display, false, lock);
  if (!egl) {
    *out_failureId = "FEATURE_FAILURE_WR_ANGLE_INIT_DISPLAY"_ns;
  }
  return egl;
}
#endif

static bool IsAccelAngleSupported(nsACString* const out_failureId) {
  if (!gfx::gfxVars::AllowWebglAccelAngle()) {
    if (out_failureId->IsEmpty()) {
      *out_failureId = "FEATURE_FAILURE_ACCL_ANGLE_NOT_OK"_ns;
    }
    return false;
  }
  return true;
}

class AngleErrorReporting {
 public:
  constexpr AngleErrorReporting() : mFailureId(nullptr) {
    // No static constructor
  }

  void SetFailureId(nsACString* const aFailureId) { mFailureId = aFailureId; }

  void logError(const char* errorMessage) {
    if (!mFailureId) {
      return;
    }

    nsCString str(errorMessage);
    Tokenizer tokenizer(str);

    // Parse "ANGLE Display::initialize error " << error.getID() << ": "
    //       << error.getMessage()
    nsCString currWord;
    Tokenizer::Token intToken;
    if (tokenizer.CheckWord("ANGLE") && tokenizer.CheckWhite() &&
        tokenizer.CheckWord("Display") && tokenizer.CheckChar(':') &&
        tokenizer.CheckChar(':') && tokenizer.CheckWord("initialize") &&
        tokenizer.CheckWhite() && tokenizer.CheckWord("error") &&
        tokenizer.CheckWhite() &&
        tokenizer.Check(Tokenizer::TOKEN_INTEGER, intToken)) {
      *mFailureId = "FAILURE_ID_ANGLE_ID_";
      mFailureId->AppendPrintf("%" PRIu64, intToken.AsInteger());
    } else {
      *mFailureId = "FAILURE_ID_ANGLE_UNKNOWN";
    }
  }

 private:
  nsACString* mFailureId;
};

constinit AngleErrorReporting gAngleErrorReporter;

static std::shared_ptr<EglDisplay> GetAndInitDisplayForAccelANGLE(
    GLLibraryEGL& egl, nsACString* const out_failureId,
    const StaticMutexAutoLock& aProofOfLock) {
  gfx::FeatureState& d3d11ANGLE =
      gfx::gfxConfig::GetFeature(gfx::Feature::D3D11_HW_ANGLE);

  if (!StaticPrefs::webgl_angle_try_d3d11()) {
    d3d11ANGLE.UserDisable("User disabled D3D11 ANGLE by pref",
                           "FAILURE_ID_ANGLE_PREF"_ns);
  }
  if (StaticPrefs::webgl_angle_force_d3d11()) {
    d3d11ANGLE.UserForceEnable(
        "User force-enabled D3D11 ANGLE on disabled hardware");
  }
  gAngleErrorReporter.SetFailureId(out_failureId);

  auto guardShutdown = mozilla::MakeScopeExit([&] {
    gAngleErrorReporter.SetFailureId(nullptr);
    // NOTE: Ideally we should be calling ANGLEPlatformShutdown after the
    //       ANGLE display is destroyed. However gAngleErrorReporter
    //       will live longer than the ANGLE display so we're fine.
  });

  std::shared_ptr<EglDisplay> ret;
  if (d3d11ANGLE.IsEnabled() ||
      gfx::gfxConfig::IsForcedOnByUser(gfx::Feature::D3D11_HW_ANGLE)) {
    ret = GetAndInitDisplay(egl, LOCAL_EGL_D3D11_ONLY_DISPLAY_ANGLE,
                            aProofOfLock);
  }

  if (!ret) {
    ret = GetAndInitDisplay(egl, EGL_DEFAULT_DISPLAY, aProofOfLock);
  }

  if (!ret && out_failureId->IsEmpty()) {
    *out_failureId = "FEATURE_FAILURE_ACCL_ANGLE_NO_DISP"_ns;
  }

  return ret;
}

// -

#if defined(XP_DARWIN)
#  define EGL_LIB "libEGL.dylib"
#  define GLES2_LIB "libGLESv2.dylib"
#elif defined(XP_UNIX)
#  define EGL_LIB "libEGL.so"
#  define EGL_LIB2 "libEGL.so.1"
#  define GLES2_LIB "libGLESv2.so"
#  define GLES2_LIB2 "libGLESv2.so.2"
#  define GL_LIB "libGL.so"
#  define GL_LIB2 "libGL.so.1"
#elif defined(XP_WIN)
#  define EGL_LIB "libEGL.dll"
#  define GLES2_LIB "libGLESv2.dll"
#else
#  error "Platform not recognized"
#endif

Maybe<SymbolLoader> GLLibraryEGL::GetSymbolLoader() const {
  auto ret = SymbolLoader(mSymbols.fGetProcAddress);
  // For ANGLE, use ANGLE's eglGetProcAddress as the only source for GL entry
  // points, avoiding raw library symbol lookups. In particular, dlsym on macOS
  // may resolve missing symbols from another GL implementation, such as the
  // system libGL.dylib, resulting in crashes.
  //
  // For non-ANGLE, we must prefer to use the library lookup. Some older EGL
  // implementations do not reliably return core GL entry points from
  // eglGetProcAddress. See bug 1420745.
  if (!IsANGLE()) {
    ret.mLib = mGLLibrary;
  }
  return Some(ret);
}

// -

/* static */
RefPtr<GLLibraryEGL> GLLibraryEGL::Get(nsACString* const out_failureId) {
  return GetImpl(LibraryType::Default, out_failureId);
}

#ifdef MOZ_WIDGET_ANDROID
/* static */
RefPtr<GLLibraryEGL> GLLibraryEGL::GetNative(nsACString* const out_failureId) {
  return GetImpl(LibraryType::Native, out_failureId);
}

/* static */
bool GLLibraryEGL::IsANGLEAvailable() {
  nsCString failureId;
  const auto lib = Get(&failureId);
  return lib && lib->IsANGLE();
}
#endif

/* static */
RefPtr<GLLibraryEGL> GLLibraryEGL::GetImpl(LibraryType type,
                                           nsACString* const out_failureId) {
  StaticMutexAutoLock lock(sMutex);

  const auto ensureInitialized =
      [&](StaticRefPtr<GLLibraryEGL>& instance,
          LibraryType libraryType) -> RefPtr<GLLibraryEGL> {
    if (!instance) {
      RefPtr<GLLibraryEGL> lib = new GLLibraryEGL;
      if (NS_WARN_IF(!lib->Init(libraryType, out_failureId))) {
        return nullptr;
      }
      instance = lib;
    }
    return instance.get();
  };

  // Keep the selected default implementation until Shutdown().
  if (type == LibraryType::Default && sInstance) {
    return sInstance;
  }

#ifdef MOZ_WIDGET_ANDROID
  if (type == LibraryType::Native || !gfx::gfxVars::UseWebRenderANGLE()) {
    auto lib = ensureInitialized(sNativeInstance, LibraryType::Native);
    if (type == LibraryType::Default) {
      sInstance = lib;
    }
    return lib;
  }
  // Preserve the backend decision through initialization, even if gfxVars
  // changes before the library is loaded or its extensions are queried.
  type = LibraryType::ANGLE;
#endif

  if (ensureInitialized(sInstance, type)) {
    return sInstance;
  }

#ifdef MOZ_WIDGET_ANDROID
  gfxCriticalNote << "Failed to initialize bundled ANGLE; using native EGL: "
                  << nsCString(*out_failureId).get();

  out_failureId->Truncate();
  sInstance = ensureInitialized(sNativeInstance, LibraryType::Native);
#endif

  return sInstance;
}

/* static */ void GLLibraryEGL::Shutdown() {
  StaticMutexAutoLock lock(sMutex);
  sInstance = nullptr;
  sNativeInstance = nullptr;
}

bool GLLibraryEGL::Init(LibraryType type, nsACString* const out_failureId) {
  MOZ_RELEASE_ASSERT(!mSymbols.fTerminate);

  mozilla::ScopedGfxFeatureReporter reporter("EGL");

#ifndef MOZ_WIDGET_ANDROID
  // The explicit native entry point is currently implemented only for Android.
  if (type == LibraryType::Native) {
    *out_failureId = "FEATURE_FAILURE_NATIVE_EGL_UNSUPPORTED"_ns;
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return false;
  }
#else
  MOZ_ASSERT(type != LibraryType::Default);
  const bool useANGLE = type == LibraryType::ANGLE;
  if (useANGLE) {
    // Load the default ANGLE implementation as a matching EGL/GLES pair.
    mGLLibrary = PR_LoadLibrary("libGLESv2_angle.so");
    if (mGLLibrary) {
      mEGLLibrary = PR_LoadLibrary("libEGL_angle.so");
    }
    if (!mEGLLibrary || !mGLLibrary) {
      *out_failureId = "FEATURE_FAILURE_ANGLE_EGL_LOAD"_ns;
      gfxCriticalNote << "Failed to load ANGLE";
      return false;
    }
  }
#endif

#ifdef XP_WIN
  if (!mEGLLibrary) {
    // On Windows, the GLESv2, EGL and DXSDK libraries are shipped with libxul
    // and we should look for them there. We have to load the libs in this
    // order, because libEGL.dll depends on libGLESv2.dll which depends on the
    // DXSDK libraries. This matters especially for WebRT apps which are in a
    // different directory. See bug 760323 and bug 749459

    // Also note that we intentionally leak the libs we load.

    do {
      // Windows 8.1+ has d3dcompiler_47.dll in the system directory.
      if (LoadLibrarySystem32(L"d3dcompiler_47.dll")) break;

      MOZ_ASSERT(false, "d3dcompiler DLL loading failed.");
    } while (false);

    mGLLibrary = LoadLibraryForEGLOnWindows(u"libGLESv2.dll"_ns);

    mEGLLibrary = LoadLibraryForEGLOnWindows(u"libEGL.dll"_ns);
  }

#else  // !Windows

  // On non-Windows (Android) we use system copies of libEGL. We look for
  // the APITrace lib, libEGL.so, and libEGL.so.1 in that order.

#  if defined(ANDROID)
  if (!mEGLLibrary) mEGLLibrary = LoadApitraceLibrary();
#  endif

  if (!mEGLLibrary) {
    mEGLLibrary = PR_LoadLibrary(EGL_LIB);
  }

#  ifdef EGL_LIB2
  if (!mEGLLibrary) {
    mEGLLibrary = PR_LoadLibrary(EGL_LIB2);
  }
#  endif

#  ifdef APITRACE_LIB
  if (!mGLLibrary) {
    mGLLibrary = PR_LoadLibrary(APITRACE_LIB);
  }
#  endif

#  ifdef GL_LIB
  if (!mGLLibrary) {
    mGLLibrary = PR_LoadLibrary(GL_LIB);
  }
#  endif

#  ifdef GL_LIB2
  if (!mGLLibrary) {
    mGLLibrary = PR_LoadLibrary(GL_LIB2);
  }
#  endif

  if (!mGLLibrary) {
    mGLLibrary = PR_LoadLibrary(GLES2_LIB);
  }

#  ifdef GLES2_LIB2
  if (!mGLLibrary) {
    mGLLibrary = PR_LoadLibrary(GLES2_LIB2);
  }
#  endif

#endif  // !Windows

  if (!mEGLLibrary || !mGLLibrary) {
    NS_WARNING("Couldn't load EGL LIB.");
    *out_failureId = "FEATURE_FAILURE_EGL_LOAD_3"_ns;
    return false;
  }

#define SYMBOL(X)                 \
  {                               \
    (PRFuncPtr*)&mSymbols.f##X, { \
      {                           \
        "egl" #X                  \
      }                           \
    }                             \
  }
#define END_OF_SYMBOLS \
  {                    \
    nullptr, {}        \
  }

  SymLoadStruct earlySymbols[] = {SYMBOL(GetDisplay),
                                  SYMBOL(Terminate),
                                  SYMBOL(GetCurrentSurface),
                                  SYMBOL(GetCurrentContext),
                                  SYMBOL(MakeCurrent),
                                  SYMBOL(DestroyContext),
                                  SYMBOL(CreateContext),
                                  SYMBOL(DestroySurface),
                                  SYMBOL(CreateWindowSurface),
                                  SYMBOL(CreatePbufferSurface),
                                  SYMBOL(CreatePbufferFromClientBuffer),
                                  SYMBOL(CreatePixmapSurface),
                                  SYMBOL(BindAPI),
                                  SYMBOL(Initialize),
                                  SYMBOL(ChooseConfig),
                                  SYMBOL(GetError),
                                  SYMBOL(GetConfigs),
                                  SYMBOL(GetConfigAttrib),
                                  SYMBOL(WaitNative),
                                  SYMBOL(GetProcAddress),
                                  SYMBOL(SwapBuffers),
                                  SYMBOL(CopyBuffers),
                                  SYMBOL(QueryString),
                                  SYMBOL(QueryContext),
                                  SYMBOL(BindTexImage),
                                  SYMBOL(ReleaseTexImage),
                                  SYMBOL(SwapInterval),
                                  SYMBOL(QuerySurface),
                                  END_OF_SYMBOLS};

  {
    const SymbolLoader libLoader(*mEGLLibrary);
    if (!libLoader.LoadSymbols(earlySymbols)) {
      NS_WARNING(
          "Couldn't find required entry points in EGL library (early init)");
      *out_failureId = "FEATURE_FAILURE_EGL_SYM"_ns;
      return false;
    }
  }

  {
    const char internalFuncName[] =
        "_Z35eglQueryStringImplementationANDROIDPvi";
    const auto& internalFunc =
        PR_FindFunctionSymbol(mEGLLibrary, internalFuncName);
    if (internalFunc) {
      *(PRFuncPtr*)&mSymbols.fQueryString = internalFunc;
    }
  }

  // -

  InitLibExtensions(type);

  const SymbolLoader pfnLoader(mSymbols.fGetProcAddress);

  const auto fnLoadSymbols = [&](const SymLoadStruct* symbols) {
    const bool shouldWarn = gfxEnv::MOZ_GL_SPEW();
    if (pfnLoader.LoadSymbols(symbols, shouldWarn)) return true;

    ClearSymbols(symbols);
    return false;
  };

  // Check the ANGLE support the system has
  mIsANGLE = IsExtensionSupported(EGLLibExtension::ANGLE_platform_angle);

#ifdef MOZ_WIDGET_ANDROID
  if (useANGLE && !mIsANGLE) {
    *out_failureId = "FEATURE_FAILURE_EGL_NOT_ANGLE"_ns;
    return false;
  }
#endif

  // Client exts are ready. (But not display exts!)

  if (mIsANGLE) {
    const SymLoadStruct angleSymbols[] = {SYMBOL(GetPlatformDisplay),
                                          END_OF_SYMBOLS};
    if (!fnLoadSymbols(angleSymbols)) {
      gfxCriticalError() << "Failed to load ANGLE symbols!";
      return false;
    }

#ifdef XP_WIN
    MOZ_ASSERT(IsExtensionSupported(EGLLibExtension::ANGLE_platform_angle_d3d));
#endif

    const SymLoadStruct createDeviceSymbols[] = {
        SYMBOL(CreateDeviceANGLE), SYMBOL(ReleaseDeviceANGLE), END_OF_SYMBOLS};
    if (!fnLoadSymbols(createDeviceSymbols)) {
      NS_ERROR(
          "EGL supports ANGLE_device_creation without exposing its functions!");
      MarkExtensionUnsupported(EGLLibExtension::ANGLE_device_creation);
    }
  }

  // ANDROID_get_native_client_buffer isn't necessarily enumerated in lib exts,
  // but it is one.
  {
    const SymLoadStruct symbols[] = {SYMBOL(GetNativeClientBufferANDROID),
                                     END_OF_SYMBOLS};
    if (fnLoadSymbols(symbols)) {
      mAvailableExtensions[UnderlyingValue(
          EGLLibExtension::ANDROID_get_native_client_buffer)] = true;
    }
  }

  // -
  // Load possible display ext symbols.

  {
    const SymLoadStruct symbols[] = {SYMBOL(QuerySurfacePointerANGLE),
                                     END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    // EGL_KHR_fence_sync
    const SymLoadStruct symbols[] = {
        SYMBOL(CreateSyncKHR), SYMBOL(DestroySyncKHR),
        SYMBOL(ClientWaitSyncKHR), SYMBOL(GetSyncAttribKHR), END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    // Core EGL 1.5 version
    const SymLoadStruct symbols[]{SYMBOL(CreateSync), END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {SYMBOL(CreateImageKHR),
                                     SYMBOL(DestroyImageKHR), END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {SYMBOL(WaitSyncKHR), END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {SYMBOL(DupNativeFenceFDANDROID),
                                     END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {SYMBOL(CreateStreamKHR),
                                     SYMBOL(DestroyStreamKHR),
                                     SYMBOL(QueryStreamKHR), END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {SYMBOL(StreamConsumerGLTextureExternalKHR),
                                     SYMBOL(StreamConsumerAcquireKHR),
                                     SYMBOL(StreamConsumerReleaseKHR),
                                     END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {
        SYMBOL(QueryDisplayAttribEXT), SYMBOL(QueryDeviceAttribEXT),
        SYMBOL(QueryDeviceStringEXT), END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {
        SYMBOL(StreamConsumerGLTextureExternalAttribsNV), END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {
        SYMBOL(CreateStreamProducerD3DTextureANGLE),
        SYMBOL(StreamPostD3DTextureANGLE), END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {
        {(PRFuncPtr*)&mSymbols.fSwapBuffersWithDamage,
         {{"eglSwapBuffersWithDamageEXT"}}},
        END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {
        {(PRFuncPtr*)&mSymbols.fSwapBuffersWithDamage,
         {{"eglSwapBuffersWithDamageKHR"}}},
        END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {
        {(PRFuncPtr*)&mSymbols.fSetDamageRegion, {{"eglSetDamageRegionKHR"}}},
        END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {SYMBOL(GetPlatformDisplay),
                                     END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {SYMBOL(ExportDMABUFImageQueryMESA),
                                     SYMBOL(ExportDMABUFImageMESA),
                                     END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {SYMBOL(QueryDevicesEXT), END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {SYMBOL(QueryDmaBufModifiersEXT),
                                     END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {SYMBOL(CopyMetalSharedEventANGLE),
                                     END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }
  {
    const SymLoadStruct symbols[] = {SYMBOL(WaitUntilWorkScheduledANGLE),
                                     END_OF_SYMBOLS};
    (void)fnLoadSymbols(symbols);
  }

  return true;
}

// -

template <size_t N>
static void MarkExtensions(const char* rawExtString, bool shouldDumpExts,
                           const char* extType, const char* const (&names)[N],
                           std::bitset<N>* const out) {
  MOZ_ASSERT(rawExtString);

  const nsDependentCString extString(rawExtString);

  std::vector<nsCString> extList;
  SplitByChar(extString, ' ', &extList);

  if (shouldDumpExts) {
    printf_stderr("%u EGL %s extensions: (*: recognized)\n",
                  (uint32_t)extList.size(), extType);
  }

  MarkBitfieldByStrings(extList, shouldDumpExts, names, out);
}

// -

// static
std::shared_ptr<EglDisplay> EglDisplay::Create(
    GLLibraryEGL& lib, const EGLDisplay display, const bool isWarp,
    const StaticMutexAutoLock& aProofOfLock) {
  // Retrieve the EglDisplay if it already exists
  {
    const auto itr = lib.mActiveDisplays.find(display);
    if (itr != lib.mActiveDisplays.end()) {
      const auto ret = itr->second.lock();
      if (ret) {
        return ret;
      }
    }
  }

  if (!lib.fInitialize(display, nullptr, nullptr)) {
    return nullptr;
  }

  static std::once_flag sMesaLeakFlag;
  std::call_once(sMesaLeakFlag, MesaMemoryLeakWorkaround);

  const auto ret =
      std::make_shared<EglDisplay>(PrivateUseOnly{}, lib, display, isWarp);
  // Insert if there is no existing display entry, or assign if there is an
  // expired weak_ptr that failed to lock above and was awaiting removal.
  lib.mActiveDisplays.insert_or_assign(display, ret);
  return ret;
}

EglDisplay::EglDisplay(const PrivateUseOnly&, GLLibraryEGL& lib,
                       const EGLDisplay disp, const bool isWarp)
    : mLib(&lib), mDisplay(disp), mIsWARP(isWarp) {
  const bool shouldDumpExts = GLContext::ShouldDumpExts();

  auto rawExtString = mLib->fQueryString(mDisplay, LOCAL_EGL_EXTENSIONS);
  if (!rawExtString) {
    NS_WARNING("Failed to query EGL display extensions!.");
    rawExtString = "";
  }
  MarkExtensions(rawExtString, shouldDumpExts, "display", sEGLExtensionNames,
                 &mAvailableExtensions);

  if (mLib->IsExtensionSupported(EGLLibExtension::EXT_device_query)) {
    EGLAttrib device = 0;
    if (fQueryDisplayAttribEXT(LOCAL_EGL_DEVICE_EXT, &device)) {
      auto rawDeviceExtString = mLib->fQueryDeviceStringEXT(
          reinterpret_cast<EGLDeviceEXT>(device), LOCAL_EGL_EXTENSIONS);
      if (!rawDeviceExtString) {
        NS_WARNING("Failed to query EGL device extensions!");
        rawDeviceExtString = "";
      }
      MarkExtensions(rawDeviceExtString, shouldDumpExts, "device",
                     sEGLDeviceExtensionNames, &mAvailableDeviceExtensions);
    } else {
      NS_WARNING("Failed to query EGL device!");
    }
  }

  // -

  if (!HasKHRImageBase()) {
    MarkExtensionUnsupported(EGLExtension::KHR_image_pixmap);
  }

  if (IsExtensionSupported(EGLExtension::KHR_surfaceless_context)) {
    const auto vendor = mLib->fQueryString(mDisplay, LOCAL_EGL_VENDOR);

    // Bug 1464610: Mali T720 (Amazon Fire 8 HD) claims to support this
    // extension, but if you actually eglMakeCurrent() with EGL_NO_SURFACE, it
    // fails to render anything when a real surface is provided later on. We
    // only have the EGL vendor available here, so just avoid using this
    // extension on all Mali devices.
    if (vendor && (strcmp(vendor, "ARM") == 0)) {
      MarkExtensionUnsupported(EGLExtension::KHR_surfaceless_context);
    }
  }

  // ANDROID_native_fence_sync isn't necessarily enumerated in display ext,
  // but it is one.
  if (mLib->mSymbols.fDupNativeFenceFDANDROID) {
    mAvailableExtensions[UnderlyingValue(
        EGLExtension::ANDROID_native_fence_sync)] = true;
  }
}

EglDisplay::~EglDisplay() {
  StaticMutexAutoLock lock(GLLibraryEGL::sMutex);
  // EglDisplay is being destructed, so its weak_ptr should be expired. If there
  // is an entry that is still live, it means a new EGLDisplay was subsequently
  // created that needs to use this display, so avoid terminating it.
  const auto itr = mLib->mActiveDisplays.find(mDisplay);
  if (itr != mLib->mActiveDisplays.end() && !itr->second.expired()) {
    return;
  }
  fTerminate();
  mLib->mActiveDisplays.erase(mDisplay);
}

// -

std::shared_ptr<EglDisplay> GLLibraryEGL::DefaultDisplay(
    nsACString* const out_failureId) {
  StaticMutexAutoLock lock(sMutex);
  auto ret = mDefaultDisplay.lock();
  if (ret) return ret;

  ret = CreateDisplayLocked({}, out_failureId, lock);
  mDefaultDisplay = ret;
  return ret;
}

std::shared_ptr<EglDisplay> GLLibraryEGL::CreateDisplay(
    const EGLCreateDisplayFlags& aFlags, nsACString* const out_failureId) {
  StaticMutexAutoLock lock(sMutex);
  return CreateDisplayLocked(aFlags, out_failureId, lock);
}

std::shared_ptr<EglDisplay> GLLibraryEGL::CreateDisplayLocked(
    const EGLCreateDisplayFlags& aFlags, nsACString* const out_failureId,
    const StaticMutexAutoLock& aProofOfLock) {
  std::shared_ptr<EglDisplay> ret;

  MOZ_ASSERT(!(aFlags.mForceAccel && aFlags.mForceSoftware),
             "Cannot force accelerated and software display");

  if (IsExtensionSupported(EGLLibExtension::ANGLE_platform_angle_d3d)) {
    nsCString accelAngleFailureId;
    bool accelAngleSupport = IsAccelAngleSupported(&accelAngleFailureId);
    bool shouldTryAccel =
        (aFlags.mForceAccel || accelAngleSupport) && !aFlags.mForceSoftware;
    bool shouldTryWARP =
        !aFlags.mForceAccel;  // Only if ANGLE not supported or fails

    // If WARP preferred, will override ANGLE support
    if (StaticPrefs::webgl_angle_force_warp()) {
      shouldTryWARP = true;
      shouldTryAccel = false;
      if (accelAngleFailureId.IsEmpty()) {
        accelAngleFailureId = "FEATURE_FAILURE_FORCE_WARP"_ns;
      }
    }

    // Hardware accelerated ANGLE path (supported or force accel)
    if (shouldTryAccel) {
      ret = GetAndInitDisplayForAccelANGLE(*this, out_failureId, aProofOfLock);
    }

    // Report the acceleration status to telemetry
    if (!ret) {
      if (accelAngleFailureId.IsEmpty()) {
        glean::canvas::webgl_accl_failure_id
            .Get("FEATURE_FAILURE_ACCL_ANGLE_UNKNOWN"_ns)
            .Add(1);
      } else {
        glean::canvas::webgl_accl_failure_id.Get(accelAngleFailureId).Add(1);
      }
    } else {
      glean::canvas::webgl_accl_failure_id.Get("SUCCESS"_ns).Add(1);
    }

    // Fallback to a WARP display if ANGLE fails, or if WARP is forced
    if (!ret && shouldTryWARP) {
      ret = GetAndInitWARPDisplay(*this, EGL_DEFAULT_DISPLAY, aProofOfLock);
      if (!ret) {
        if (out_failureId->IsEmpty()) {
          *out_failureId = "FEATURE_FAILURE_WARP_FALLBACK"_ns;
        }
        NS_ERROR("Fallback WARP context failed to initialize.");
        return nullptr;
      }
    }
  } else if (IsANGLE()) {
    MOZ_ASSERT(!aFlags.mForceSoftware,
               "Software rendering not supported by EGL on this platform");
    if (!aFlags.mForceSoftware) {
      AutoTArray<EGLAttrib, 5> attrib_list;

#ifdef MOZ_WIDGET_ANDROID
      // On Android, ANGLE may be compiled with multiple backends, so
      // explicitly request the GLES backend.
      if (!IsExtensionSupported(EGLLibExtension::ANGLE_platform_angle_opengl)) {
        *out_failureId = "FEATURE_FAILURE_ANGLE_GL_UNSUPPORTED"_ns;
        return nullptr;
      }
      attrib_list.AppendElement(LOCAL_EGL_PLATFORM_ANGLE_TYPE_ANGLE);
      attrib_list.AppendElement(LOCAL_EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE);
#endif

      if (IsExtensionSupported(
              EGLLibExtension::ANGLE_display_power_preference)) {
        // The docs state "if this extension is advertised and this display
        // creation attribute is not specified, the default value is
        // EGL_LOW_POWER_ANGLE."
        // In testing, however, it was found that EGL_LOW_POWER_ANGLE had to be
        // specifically requested in order to obtain a low power display.
        attrib_list.AppendElement(LOCAL_EGL_POWER_PREFERENCE_ANGLE);
        attrib_list.AppendElement(aFlags.mPreferHighPower
                                      ? LOCAL_EGL_HIGH_POWER_ANGLE
                                      : LOCAL_EGL_LOW_POWER_ANGLE);
      }

      attrib_list.AppendElement(LOCAL_EGL_NONE);
      const EGLDisplay display =
          fGetPlatformDisplay(LOCAL_EGL_PLATFORM_ANGLE_ANGLE,
                              EGL_DEFAULT_DISPLAY, attrib_list.Elements());
      if (display) {
        ret = EglDisplay::Create(*this, display, false, aProofOfLock);
      }
    }
  } else {
    void* nativeDisplay = EGL_DEFAULT_DISPLAY;
#ifdef MOZ_WIDGET_GTK
    if (!ret && (!gfx::gfxVars::WebglUseHardware() || aFlags.mForceSoftware)) {
      // Initialize a swrast egl device such as llvmpipe
      ret = GetAndInitSoftwareDisplay(*this, aProofOfLock);
    }
    // Initialize the display the normal way
    if (!ret && !gdk_display_get_default() && !aFlags.mForceSoftware) {
      ret = GetAndInitDeviceDisplay(*this, aProofOfLock);
      if (!ret) {
        ret = GetAndInitSurfacelessDisplay(*this, aProofOfLock);
      }
    }
#  ifdef MOZ_WAYLAND
    else if (!ret && widget::GdkIsWaylandDisplay() && !aFlags.mForceSoftware) {
      // Wayland does not support EGL_DEFAULT_DISPLAY
      nativeDisplay = widget::WaylandDisplayGetWLDisplay();
      if (!nativeDisplay) {
        NS_WARNING("Failed to get wl_display.");
        return nullptr;
      }
    }
#  endif
#endif

    if (!ret && !aFlags.mForceSoftware) {
      ret = GetAndInitDisplay(*this, nativeDisplay, aProofOfLock);
    }
  }

  if (!ret) {
    if (out_failureId->IsEmpty()) {
      *out_failureId = "FEATURE_FAILURE_NO_DISPLAY"_ns;
    }
    NS_WARNING("Failed to initialize a display.");
    return nullptr;
  }

  return ret;
}

void GLLibraryEGL::InitLibExtensions(LibraryType type) {
  const bool shouldDumpExts = GLContext::ShouldDumpExts();

  const char* rawExtString = nullptr;

#ifdef MOZ_WIDGET_ANDROID
  // Bug 1209612: Querying client extensions can crash Android system drivers.
  // Query only when Init() loaded bundled ANGLE. This decision must use the
  // selected library type rather than the current gfxVars value, which may
  // have changed since GetImpl(). Native fallback also skips this query.
  if (type != LibraryType::ANGLE) {
    return;
  }
#endif
  rawExtString = fQueryString(nullptr, LOCAL_EGL_EXTENSIONS);

  if (!rawExtString) {
    if (shouldDumpExts) {
      printf_stderr("No EGL lib extensions.\n");
    }
    return;
  }

  MarkExtensions(rawExtString, shouldDumpExts, "lib", sEGLLibraryExtensionNames,
                 &mAvailableExtensions);
}

void EglDisplay::DumpEGLConfig(EGLConfig cfg) const {
#define ATTR(_x)                                                     \
  do {                                                               \
    int attrval = 0;                                                 \
    mLib->fGetConfigAttrib(mDisplay, cfg, LOCAL_EGL_##_x, &attrval); \
    const auto err = mLib->fGetError();                              \
    if (err != 0x3000) {                                             \
      printf_stderr("  %s: ERROR (0x%04x)\n", #_x, err);             \
    } else {                                                         \
      printf_stderr("  %s: %d (0x%04x)\n", #_x, attrval, attrval);   \
    }                                                                \
  } while (0)

  printf_stderr("EGL Config: %d [%p]\n", (int)(intptr_t)cfg, cfg);

  ATTR(BUFFER_SIZE);
  ATTR(ALPHA_SIZE);
  ATTR(BLUE_SIZE);
  ATTR(GREEN_SIZE);
  ATTR(RED_SIZE);
  ATTR(DEPTH_SIZE);
  ATTR(STENCIL_SIZE);
  ATTR(CONFIG_CAVEAT);
  ATTR(CONFIG_ID);
  ATTR(LEVEL);
  ATTR(MAX_PBUFFER_HEIGHT);
  ATTR(MAX_PBUFFER_PIXELS);
  ATTR(MAX_PBUFFER_WIDTH);
  ATTR(NATIVE_RENDERABLE);
  ATTR(NATIVE_VISUAL_ID);
  ATTR(NATIVE_VISUAL_TYPE);
  ATTR(PRESERVED_RESOURCES);
  ATTR(SAMPLES);
  ATTR(SAMPLE_BUFFERS);
  ATTR(SURFACE_TYPE);
  ATTR(TRANSPARENT_TYPE);
  ATTR(TRANSPARENT_RED_VALUE);
  ATTR(TRANSPARENT_GREEN_VALUE);
  ATTR(TRANSPARENT_BLUE_VALUE);
  ATTR(BIND_TO_TEXTURE_RGB);
  ATTR(BIND_TO_TEXTURE_RGBA);
  ATTR(MIN_SWAP_INTERVAL);
  ATTR(MAX_SWAP_INTERVAL);
  ATTR(LUMINANCE_SIZE);
  ATTR(ALPHA_MASK_SIZE);
  ATTR(COLOR_BUFFER_TYPE);
  ATTR(RENDERABLE_TYPE);
  ATTR(CONFORMANT);

#undef ATTR
}

void EglDisplay::DumpEGLConfigs() const {
  int nc = 0;
  mLib->fGetConfigs(mDisplay, nullptr, 0, &nc);
  std::vector<EGLConfig> ec(nc);
  mLib->fGetConfigs(mDisplay, ec.data(), ec.size(), &nc);

  for (int i = 0; i < nc; ++i) {
    printf_stderr("========= EGL Config %d ========\n", i);
    DumpEGLConfig(ec[i]);
  }
}

static bool ShouldTrace() {
  static bool ret = gfxEnv::MOZ_GL_DEBUG_VERBOSE();
  return ret;
}

void BeforeEGLCall(const char* glFunction) {
  if (ShouldTrace()) {
    printf_stderr("[egl] > %s\n", glFunction);
  }
}

void AfterEGLCall(const char* glFunction) {
  if (ShouldTrace()) {
    printf_stderr("[egl] < %s\n", glFunction);
  }
}

} /* namespace gl */
} /* namespace mozilla */
