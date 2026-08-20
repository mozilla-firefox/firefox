/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_WIDGET_GTK
#  include <gdk/gdk.h>
#  include <gdk/gdkx.h>
#  define GET_NATIVE_WINDOW(aWidget) \
    GDK_WINDOW_XID((GdkWindow*)aWidget->GetNativeData(NS_NATIVE_WINDOW))
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "GLContextGLX.h"
#include "GLContextProvider.h"
#include "GLLibraryLoader.h"
#include "GLScreenBuffer.h"
#include "GLXLibrary.h"
#include "X11UndefineNone.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxCrashReporterUtils.h"
#include "gfxEnv.h"
#include "gfxPlatform.h"
#include "gfxUtils.h"
#include "mozilla/Range.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/layers/CompositorOptions.h"
#include "mozilla/widget/CompositorWidget.h"
#include "mozilla/widget/GtkCompositorWidget.h"
#include "nsDebug.h"
#include "nsIWidget.h"
#include "prenv.h"

#ifdef MOZ_WIDGET_GTK
#  include "gfxPlatformGtk.h"
#endif

namespace mozilla::gl {

using namespace mozilla::gfx;
using namespace mozilla::widget;

MOZ_GLOBINIT GLXLibrary sGLXLibrary;

static inline bool HasExtension(const char* aExtensions,
                                const char* aRequiredExtension) {
  return GLContext::ListHasExtension(
      reinterpret_cast<const GLubyte*>(aExtensions), aRequiredExtension);
}

bool GLXLibrary::EnsureInitialized(Display* aDisplay) {
  if (mInitialized) {
    return true;
  }

  // Don't repeatedly try to initialize.
  if (mTriedInitializing) {
    return false;
  }
  mTriedInitializing = true;

  MOZ_ASSERT(aDisplay);
  if (!aDisplay) {
    return false;
  }

  // Force enabling s3 texture compression. (Bug 774134)
  PR_SetEnv("force_s3tc_enable=true");

  if (!mOGLLibrary) {
    // see e.g. bug 608526: it is intrinsically interesting to know whether we
    // have dynamically linked to libGL.so.1 because at least the NVIDIA
    // implementation requires an executable stack, which causes mprotect calls,
    // which trigger glibc bug
    // http://sourceware.org/bugzilla/show_bug.cgi?id=12225
    const char* libGLfilename = "libGL.so.1";
#if defined(__OpenBSD__) || defined(__NetBSD__)
    libGLfilename = "libGL.so";
#endif

    const bool forceFeatureReport = false;
    ScopedGfxFeatureReporter reporter(libGLfilename, forceFeatureReport);
    mOGLLibrary = PR_LoadLibrary(libGLfilename);
    if (!mOGLLibrary) {
      NS_WARNING("Couldn't load OpenGL shared library.");
      return false;
    }
    reporter.SetSuccessful();
  }

  if (gfxEnv::MOZ_GLX_DEBUG()) {
    mDebug = true;
  }

#define SYMBOL(X)                 \
  {                               \
    (PRFuncPtr*)&mSymbols.f##X, { \
      {                           \
        "glX" #X                  \
      }                           \
    }                             \
  }
#define END_OF_SYMBOLS \
  {                    \
    nullptr, {}        \
  }

  const SymLoadStruct symbols[] = {
      /* functions that were in GLX 1.0 */
      SYMBOL(DestroyContext),
      SYMBOL(MakeCurrent),
      SYMBOL(SwapBuffers),
      SYMBOL(QueryVersion),
      SYMBOL(GetConfig),
      SYMBOL(GetCurrentContext),
      SYMBOL(WaitGL),
      SYMBOL(WaitX),

      /* functions introduced in GLX 1.1 */
      SYMBOL(QueryExtensionsString),
      SYMBOL(GetClientString),
      SYMBOL(QueryServerString),

      /* functions introduced in GLX 1.3 */
      SYMBOL(ChooseFBConfig),
      SYMBOL(ChooseVisual),
      SYMBOL(GetFBConfigAttrib),
      SYMBOL(GetFBConfigs),
      SYMBOL(CreatePixmap),
      SYMBOL(DestroyPixmap),
      SYMBOL(CreateNewContext),

      // Core in GLX 1.4, ARB extension before.
      {(PRFuncPtr*)&mSymbols.fGetProcAddress,
       {{"glXGetProcAddress", "glXGetProcAddressARB"}}},
      END_OF_SYMBOLS};

  {
    const SymbolLoader libLoader(*mOGLLibrary);
    if (!libLoader.LoadSymbols(symbols)) {
      NS_WARNING("Couldn't load required GLX symbols.");
      return false;
    }
  }
  const SymbolLoader pfnLoader(mSymbols.fGetProcAddress);

  int screen = DefaultScreen(aDisplay);

  {
    int major, minor;
    if (!fQueryVersion(aDisplay, &major, &minor) || major != 1 || minor < 3) {
      NS_ERROR("GLX version older than 1.3. (released in 1998)");
      return false;
    }
  }

  const SymLoadStruct symbols_createcontext[] = {
      SYMBOL(CreateContextAttribsARB), END_OF_SYMBOLS};

  const SymLoadStruct symbols_videosync[] = {
      SYMBOL(GetVideoSyncSGI), SYMBOL(WaitVideoSyncSGI), END_OF_SYMBOLS};

  const SymLoadStruct symbols_swapcontrol[] = {SYMBOL(SwapIntervalEXT),
                                               END_OF_SYMBOLS};

  const SymLoadStruct symbols_querydrawable[] = {SYMBOL(QueryDrawable),
                                                 END_OF_SYMBOLS};

  const auto fnLoadSymbols = [&](const SymLoadStruct* symbols) {
    if (pfnLoader.LoadSymbols(symbols)) return true;

    ClearSymbols(symbols);
    return false;
  };

  const char* clientVendor = fGetClientString(aDisplay, LOCAL_GLX_VENDOR);
  const char* serverVendor =
      fQueryServerString(aDisplay, screen, LOCAL_GLX_VENDOR);
  const char* extensionsStr = fQueryExtensionsString(aDisplay, screen);

  if (HasExtension(extensionsStr, "GLX_ARB_create_context") &&
      HasExtension(extensionsStr, "GLX_ARB_create_context_profile") &&
      fnLoadSymbols(symbols_createcontext)) {
    mHasCreateContextAttribs = true;
  }

  if (HasExtension(extensionsStr, "GLX_ARB_create_context_robustness")) {
    mHasRobustness = true;
  }

  if (HasExtension(extensionsStr, "GLX_NV_robustness_video_memory_purge")) {
    mHasVideoMemoryPurge = true;
  }

  if (HasExtension(extensionsStr, "GLX_SGI_video_sync") &&
      fnLoadSymbols(symbols_videosync)) {
    mHasVideoSync = true;
  }

  if (!HasExtension(extensionsStr, "GLX_EXT_swap_control") ||
      !fnLoadSymbols(symbols_swapcontrol)) {
    NS_WARNING(
        "GLX_swap_control unsupported, ASAP mode may still block on buffer "
        "swaps.");
  }

  if (HasExtension(extensionsStr, "GLX_EXT_buffer_age") &&
      fnLoadSymbols(symbols_querydrawable)) {
    mHasBufferAge = true;
  }

  mIsATI = serverVendor && DoesStringMatch(serverVendor, "ATI");
  mIsNVIDIA =
      serverVendor && DoesStringMatch(serverVendor, "NVIDIA Corporation");
  mClientIsMesa = clientVendor && DoesStringMatch(clientVendor, "Mesa");

  mInitialized = true;

  // This needs to be after `fQueryServerString` is called so that the
  // driver is loaded.
  MesaMemoryLeakWorkaround();

  return true;
}

bool GLXLibrary::SupportsVideoSync(Display* aDisplay) {
  if (!EnsureInitialized(aDisplay)) {
    return false;
  }

  return mHasVideoSync;
}

static int (*sOldErrorHandler)(Display*, XErrorEvent*);
static XErrorEvent sErrorEvent = {};

static int GLXErrorHandler(Display* display, XErrorEvent* ev) {
  if (!sErrorEvent.error_code) {
    sErrorEvent = *ev;
  }
  return 0;
}

GLXLibrary::WrapperScope::WrapperScope(const GLXLibrary& glx,
                                       const char* const funcName,
                                       Display* aDisplay)
    : mGlx(glx), mFuncName(funcName), mDisplay(aDisplay) {
  if (mGlx.mDebug) {
    sOldErrorHandler = XSetErrorHandler(GLXErrorHandler);
  }
}

GLXLibrary::WrapperScope::~WrapperScope() {
  if (mGlx.mDebug) {
    if (mDisplay) {
      FinishX(mDisplay);
    }
    if (sErrorEvent.error_code) {
      char buffer[100] = {};
      if (mDisplay) {
        XGetErrorText(mDisplay, sErrorEvent.error_code, buffer, sizeof(buffer));
      } else {
        SprintfLiteral(buffer, "%d", sErrorEvent.error_code);
      }
      printf_stderr("X ERROR after %s: %s (%i) - Request: %i.%i, Serial: %lu",
                    mFuncName, buffer, sErrorEvent.error_code,
                    sErrorEvent.request_code, sErrorEvent.minor_code,
                    sErrorEvent.serial);
      MOZ_ASSERT_UNREACHABLE("AfterGLXCall sErrorEvent");
    }
    const auto was = XSetErrorHandler(sOldErrorHandler);
    if (was != GLXErrorHandler) {
      NS_WARNING("Concurrent XSetErrorHandlers");
    }
  }
}

// Returns the GTK display if available; otherwise, if a display was
// previously opened by this method and is still open, returns a
// reference to it; otherwise, opens a new connection.  (The non-GTK
// cases are similar to what we do for EGL.)
std::shared_ptr<XlibDisplay> GLXLibrary::GetDisplay() {
  std::shared_ptr<XlibDisplay> display;

#ifdef MOZ_WIDGET_GTK
  static const bool kHaveGtk = !!gdk_display_get_default();
  if (kHaveGtk) {
    display = XlibDisplay::Borrow(DefaultXDisplay());
  }
#endif
  if (display) {
    return display;
  }

  auto ownDisplay = mOwnDisplay.Lock();
  display = ownDisplay->lock();
  if (display) {
    return display;
  }

  display = XlibDisplay::Open(nullptr);
  if (NS_WARN_IF(!display)) {
    return nullptr;
  }
  *ownDisplay = display;
  return display;
}

already_AddRefed<GLContextGLX> GLContextGLX::CreateGLContext(
    const GLContextDesc& desc, std::shared_ptr<XlibDisplay> display,
    GLXDrawable drawable, GLXFBConfig cfg, Drawable ownedPixmap) {
  GLXLibrary& glx = sGLXLibrary;

  int isDoubleBuffered = 0;
  int err = glx.fGetFBConfigAttrib(*display, cfg, LOCAL_GLX_DOUBLEBUFFER,
                                   &isDoubleBuffered);
  if (LOCAL_GLX_BAD_ATTRIBUTE != err) {
    if (ShouldSpew()) {
      printf("[GLX] FBConfig is %sdouble-buffered\n",
             isDoubleBuffered ? "" : "not ");
    }
  }

  if (!glx.HasCreateContextAttribs()) {
    NS_WARNING("Cannot create GLContextGLX without glxCreateContextAttribs");
    return nullptr;
  }

  // -

  const auto CreateWithAttribs =
      [&](const std::vector<int>& attribs) -> RefPtr<GLContextGLX> {
    auto terminated = attribs;
    terminated.push_back(0);

    const auto glxContext = glx.fCreateContextAttribs(
        *display, cfg, nullptr, X11True, terminated.data());
    if (!glxContext) return nullptr;
    const RefPtr<GLContextGLX> ret = new GLContextGLX(
        desc, display, drawable, glxContext, isDoubleBuffered, ownedPixmap);

    if (!ret->Init()) return nullptr;

    return ret;
  };

  // -

  RefPtr<GLContextGLX> glContext;

  std::vector<int> attribs;
  attribs.insert(attribs.end(), {
                                    LOCAL_GLX_RENDER_TYPE,
                                    LOCAL_GLX_RGBA_TYPE,
                                });
  if (glx.HasVideoMemoryPurge()) {
    attribs.insert(attribs.end(),
                   {
                       LOCAL_GLX_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV,
                       LOCAL_GL_TRUE,
                   });
  }
  const bool useCore =
      !(desc.flags & CreateContextFlags::REQUIRE_COMPAT_PROFILE);
  if (useCore) {
    attribs.insert(attribs.end(), {
                                      LOCAL_GLX_CONTEXT_MAJOR_VERSION_ARB,
                                      3,
                                      LOCAL_GLX_CONTEXT_MINOR_VERSION_ARB,
                                      2,
                                      LOCAL_GLX_CONTEXT_PROFILE_MASK_ARB,
                                      LOCAL_GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                                  });
  }

  if (glx.HasRobustness()) {
    auto withRobustness = attribs;
    withRobustness.insert(withRobustness.end(),
                          {
                              LOCAL_GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB,
                              LOCAL_GLX_LOSE_CONTEXT_ON_RESET_ARB,
                          });

    {
      auto withRBAB = withRobustness;
      withRBAB.insert(withRBAB.end(),
                      {
                          LOCAL_GLX_CONTEXT_FLAGS_ARB,
                          LOCAL_GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB,
                      });
      if (!glContext) {
        glContext = CreateWithAttribs(withRBAB);
        if (!glContext) {
          NS_WARNING("Failed to create+init GLContextGLX with RBAB");
        }
      }
    }

    if (!glContext) {
      glContext = CreateWithAttribs(withRobustness);
      if (!glContext) {
        NS_WARNING("Failed to create+init GLContextGLX with Robustness");
      }
    }
  }

  if (!glContext) {
    glContext = CreateWithAttribs(attribs);
    if (!glContext) {
      NS_WARNING("Failed to create+init GLContextGLX with required attribs");
    }
  }

  return glContext.forget();
}

GLContextGLX::~GLContextGLX() {
  MarkDestroyed();

  // Wrapped context should not destroy glxContext/Surface
  if (!mOwnsContext) {
    return;
  }

  // see bug 659842 comment 76
  bool success = mGLX->fMakeCurrent(*mDisplay, X11None, nullptr);
  if (!success) {
    NS_WARNING(
        "glXMakeCurrent failed to release GL context before we call "
        "glXDestroyContext!");
  }

  mGLX->fDestroyContext(*mDisplay, mContext);

  // If we own the enclosed X pixmap, then free it after we free the enclosing
  // GLX pixmap.
  if (mOwnedPixmap) {
    mGLX->fDestroyPixmap(*mDisplay, mDrawable);
    XFreePixmap(*mDisplay, mOwnedPixmap);
  }
}

bool GLContextGLX::Init() {
  if (!GLContext::Init()) {
    return false;
  }

  // EXT_framebuffer_object is not supported on Core contexts
  // so we'll also check for ARB_framebuffer_object
  if (!IsExtensionSupported(EXT_framebuffer_object) &&
      !IsSupported(GLFeature::framebuffer_object))
    return false;

  return true;
}

bool GLContextGLX::MakeCurrentImpl() const {
  if (mGLX->IsMesa()) {
    // Read into the event queue to ensure that Mesa receives a
    // DRI2InvalidateBuffers event before drawing. See bug 1280653.
    (void)XPending(*mDisplay);
  }

  const bool succeeded = mGLX->fMakeCurrent(*mDisplay, mDrawable, mContext);
  if (!succeeded) {
    NS_WARNING("Failed to make GL context current!");
  }

  if (!IsOffscreen() && mGLX->SupportsSwapControl()) {
    // Many GLX implementations default to blocking until the next
    // VBlank when calling glXSwapBuffers. We want to run unthrottled
    // in ASAP mode. See bug 1280744.
    const bool swapInterval = gfxVars::SwapIntervalGLX();
    const bool isASAP = (StaticPrefs::layout_frame_rate() == 0);
    const int interval = (swapInterval && !isASAP) ? 1 : 0;
    mGLX->fSwapInterval(*mDisplay, mDrawable, interval);
  }
  return succeeded;
}

bool GLContextGLX::IsCurrentImpl() const {
  return mGLX->fGetCurrentContext() == mContext;
}

Maybe<SymbolLoader> GLContextGLX::GetSymbolLoader() const {
  const auto pfn = sGLXLibrary.GetGetProcAddress();
  return Some(SymbolLoader(pfn));
}

bool GLContextGLX::IsDoubleBuffered() const { return mDoubleBuffered; }

bool GLContextGLX::SwapBuffers() {
  if (!mDoubleBuffered) return false;
  mGLX->fSwapBuffers(*mDisplay, mDrawable);
  return true;
}

GLint GLContextGLX::GetBufferAge() const {
  if (!sGLXLibrary.SupportsBufferAge()) {
    return 0;
  }

  GLuint result = 0;
  mGLX->fQueryDrawable(*mDisplay, mDrawable, LOCAL_GLX_BACK_BUFFER_AGE_EXT,
                       &result);
  if (result > INT32_MAX) {
    // If the result can't fit, just assume the buffer cannot be reused.
    return 0;
  }
  return result;
}

void GLContextGLX::GetWSIInfo(nsCString* const out) const {
  int screen = DefaultScreen(mDisplay->get());

  int majorVersion, minorVersion;
  sGLXLibrary.fQueryVersion(*mDisplay, &majorVersion, &minorVersion);

  out->Append(nsPrintfCString("GLX %u.%u", majorVersion, minorVersion));

  out->AppendLiteral("\nGLX_VENDOR(client): ");
  out->Append(sGLXLibrary.fGetClientString(*mDisplay, LOCAL_GLX_VENDOR));

  out->AppendLiteral("\nGLX_VENDOR(server): ");
  out->Append(
      sGLXLibrary.fQueryServerString(*mDisplay, screen, LOCAL_GLX_VENDOR));

  out->AppendLiteral("\nExtensions: ");
  out->Append(sGLXLibrary.fQueryExtensionsString(*mDisplay, screen));
}

bool GLContextGLX::OverrideDrawable(GLXDrawable drawable) {
  return mGLX->fMakeCurrent(*mDisplay, drawable, mContext);
}

bool GLContextGLX::RestoreDrawable() {
  return mGLX->fMakeCurrent(*mDisplay, mDrawable, mContext);
}

GLContextGLX::GLContextGLX(const GLContextDesc& desc,
                           std::shared_ptr<XlibDisplay> aDisplay,
                           GLXDrawable aDrawable, GLXContext aContext,
                           bool aDoubleBuffered, Drawable aOwnedPixmap)
    : GLContext(desc, nullptr),
      mContext(aContext),
      mDisplay(std::move(aDisplay)),
      mDrawable(aDrawable),
      mOwnedPixmap(aOwnedPixmap),
      mDoubleBuffered(aDoubleBuffered),
      mGLX(&sGLXLibrary) {}

static bool AreCompatibleVisuals(Visual* one, Visual* two) {
  if (one->c_class != two->c_class) {
    return false;
  }

  if (one->red_mask != two->red_mask || one->green_mask != two->green_mask ||
      one->blue_mask != two->blue_mask) {
    return false;
  }

  if (one->bits_per_rgb != two->bits_per_rgb) {
    return false;
  }

  return true;
}

already_AddRefed<GLContext> CreateForWidget(Display* aXDisplay, Window aXWindow,
                                            bool aHardwareWebRender,
                                            bool aForceAccelerated) {
  if (!sGLXLibrary.EnsureInitialized(aXDisplay)) {
    return nullptr;
  }

  // Currently, we take whatever Visual the window already has, and
  // try to create an fbconfig for that visual.  This isn't
  // necessarily what we want in the long run; an fbconfig may not
  // be available for the existing visual, or if it is, the GL
  // performance might be suboptimal.  But using the existing visual
  // is a relatively safe intermediate step.

  if (!aXDisplay) {
    NS_ERROR("X Display required for GLX Context provider");
    return nullptr;
  }

  if (!aXWindow) {
    NS_ERROR("X window required for GLX Context provider");
    return nullptr;
  }

  int xscreen = DefaultScreen(aXDisplay);

  GLXFBConfig config;
  int visid;
  if (!GLContextGLX::FindFBConfigForWindow(
          aXDisplay, xscreen, aXWindow, &config, &visid, aHardwareWebRender)) {
    return nullptr;
  }

  CreateContextFlags flags;
  if (aHardwareWebRender) {
    flags = CreateContextFlags::NONE;  // WR needs GL3.2+
  } else {
    flags = CreateContextFlags::REQUIRE_COMPAT_PROFILE;
  }
  return GLContextGLX::CreateGLContext(
      {{flags}, false}, XlibDisplay::Borrow(aXDisplay), aXWindow, config);
}

already_AddRefed<GLContext> GLContextProviderGLX::CreateForCompositorWidget(
    CompositorWidget* aCompositorWidget, bool aHardwareWebRender,
    bool aForceAccelerated) {
  if (!aCompositorWidget) {
    MOZ_ASSERT(false);
    return nullptr;
  }
  GtkCompositorWidget* compWidget = aCompositorWidget->AsGTK();
  MOZ_ASSERT(compWidget);

  return CreateForWidget(DefaultXDisplay(), compWidget->XWindow(),
                         aHardwareWebRender, aForceAccelerated);
}

static bool ChooseConfig(GLXLibrary* glx, Display* display, int screen,
                         GLXFBConfig* const out_config, int* const out_visid) {
  const int attribs[] = {
      LOCAL_GLX_RENDER_TYPE,
      LOCAL_GLX_RGBA_BIT,
      LOCAL_GLX_DRAWABLE_TYPE,
      LOCAL_GLX_PIXMAP_BIT,
      LOCAL_GLX_X_RENDERABLE,
      X11True,
      LOCAL_GLX_RED_SIZE,
      8,
      LOCAL_GLX_GREEN_SIZE,
      8,
      LOCAL_GLX_BLUE_SIZE,
      8,
      LOCAL_GLX_ALPHA_SIZE,
      8,
      LOCAL_GLX_DEPTH_SIZE,
      0,
      LOCAL_GLX_STENCIL_SIZE,
      0,
      0,
  };

  int numConfigs = 0;
  const auto scopedConfigArr =
      glx->fChooseFBConfig(display, screen, attribs, &numConfigs);
  const auto freeConfigList = MakeScopeExit([&]() {
    if (scopedConfigArr) {
      XFree(scopedConfigArr);
    }
  });
  if (!scopedConfigArr || !numConfigs) return false;

  // Issues with glxChooseFBConfig selection and sorting:
  // * ALPHA_SIZE is sorted as 'largest total RGBA bits first'. If we don't
  // request
  //   alpha bits, we'll probably get RGBA anyways, since 32 is more than 24.
  // * DEPTH_SIZE is sorted largest first, including for `0` inputs.
  // * STENCIL_SIZE is smallest first, but it might return `8` even though we
  // ask for
  //   `0`.

  // For now, we don't care about these. We *will* care when we do XPixmap
  // sharing.

  for (int i = 0; i < numConfigs; ++i) {
    GLXFBConfig curConfig = scopedConfigArr[i];

    int visid;
    if (glx->fGetFBConfigAttrib(display, curConfig, LOCAL_GLX_VISUAL_ID,
                                &visid) != X11Success) {
      continue;
    }

    if (!visid) continue;

    *out_config = curConfig;
    *out_visid = visid;
    return true;
  }

  return false;
}

bool GLContextGLX::FindVisual(Display* display, int screen,
                              int* const out_visualId) {
  if (!sGLXLibrary.EnsureInitialized(display)) {
    return false;
  }

  XVisualInfo visualTemplate;
  visualTemplate.screen = screen;

  // Get all visuals of screen

  int visualsLen = 0;
  XVisualInfo* xVisuals =
      XGetVisualInfo(display, VisualScreenMask, &visualTemplate, &visualsLen);
  if (!xVisuals) {
    return false;
  }
  const Range<XVisualInfo> visualInfos(xVisuals, visualsLen);
  auto cleanupVisuals = MakeScopeExit([&] { XFree(xVisuals); });

  // Get default visual info

  Visual* defaultVisual = DefaultVisual(display, screen);
  const auto defaultVisualInfo = [&]() -> const XVisualInfo* {
    for (const auto& cur : visualInfos) {
      if (cur.visual == defaultVisual) {
        return &cur;
      }
    }
    return nullptr;
  }();
  if (!defaultVisualInfo) {
    MOZ_ASSERT(false);
    return false;
  }

  const int bpp = 32;

  for (auto& cur : visualInfos) {
    const auto fnConfigMatches = [&](const int pname, const int expected) {
      int actual;
      if (sGLXLibrary.fGetConfig(display, &cur, pname, &actual)) {
        return false;
      }
      return actual == expected;
    };

    // Check if visual is compatible.
    if (cur.depth != bpp || cur.c_class != defaultVisualInfo->c_class) {
      continue;
    }

    // Check if visual is compatible to GL requests.
    if (fnConfigMatches(LOCAL_GLX_USE_GL, 1) &&
        fnConfigMatches(LOCAL_GLX_DOUBLEBUFFER, 1) &&
        fnConfigMatches(LOCAL_GLX_RED_SIZE, 8) &&
        fnConfigMatches(LOCAL_GLX_GREEN_SIZE, 8) &&
        fnConfigMatches(LOCAL_GLX_BLUE_SIZE, 8) &&
        fnConfigMatches(LOCAL_GLX_ALPHA_SIZE, 8)) {
      *out_visualId = cur.visualid;
      return true;
    }
  }

  return false;
}

bool GLContextGLX::FindFBConfigForWindow(Display* display, int screen,
                                         Window window,
                                         GLXFBConfig* const out_config,
                                         int* const out_visid,
                                         bool aWebRender) {
  // XXX the visual ID is almost certainly the LOCAL_GLX_FBCONFIG_ID, so
  // we could probably do this first and replace the glXGetFBConfigs
  // with glXChooseConfigs.  Docs are sparklingly clear as always.
  XWindowAttributes windowAttrs;
  if (!XGetWindowAttributes(display, window, &windowAttrs)) {
    NS_WARNING("[GLX] XGetWindowAttributes() failed");
    return false;
  }

  GLXFBConfig* cfgs = nullptr;
  const auto freeConfigList = MakeScopeExit([&]() {
    if (cfgs) {
      XFree(cfgs);
    }
  });
  int numConfigs;
  const int webrenderAttribs[] = {LOCAL_GLX_ALPHA_SIZE,
                                  windowAttrs.depth == 32 ? 8 : 0,
                                  LOCAL_GLX_DOUBLEBUFFER, X11True, 0};

  if (aWebRender) {
    cfgs = sGLXLibrary.fChooseFBConfig(display, screen, webrenderAttribs,
                                       &numConfigs);
  } else {
    cfgs = sGLXLibrary.fGetFBConfigs(display, screen, &numConfigs);
  }

  if (!cfgs) {
    NS_WARNING("[GLX] glXGetFBConfigs() failed");
    return false;
  }
  NS_ASSERTION(numConfigs > 0, "No FBConfigs found!");

  const VisualID windowVisualID = XVisualIDFromVisual(windowAttrs.visual);
#ifdef DEBUG
  printf("[GLX] window %lx has VisualID 0x%lx\n", window, windowVisualID);
#endif

  for (int i = 0; i < numConfigs; i++) {
    int visid = X11None;
    sGLXLibrary.fGetFBConfigAttrib(display, cfgs[i], LOCAL_GLX_VISUAL_ID,
                                   &visid);
    if (visid) {
      // WebRender compatible GLX visual is configured
      // at nsWindow::Create() by GLContextGLX::FindVisual(),
      // just reuse it here.
      if (windowVisualID == static_cast<VisualID>(visid)) {
        *out_config = cfgs[i];
        *out_visid = visid;
        return true;
      }
    }
  }

  // We don't have a frame buffer visual which matches the GLX visual
  // from GLContextGLX::FindVisual(). Let's try to find a near one and hope
  // we're not on NVIDIA (Bug 1478454) as it causes X11 BadMatch error there.
  for (int i = 0; i < numConfigs; i++) {
    int visid = X11None;
    sGLXLibrary.fGetFBConfigAttrib(display, cfgs[i], LOCAL_GLX_VISUAL_ID,
                                   &visid);
    if (visid) {
      int depth;
      Visual* visual;
      FindVisualAndDepth(display, visid, &visual, &depth);
      if (depth == windowAttrs.depth &&
          AreCompatibleVisuals(windowAttrs.visual, visual)) {
        *out_config = cfgs[i];
        *out_visid = visid;
        return true;
      }
    }
  }

  NS_WARNING("[GLX] Couldn't find a FBConfig matching window visual");
  return false;
}

static already_AddRefed<GLContextGLX> CreateOffscreenPixmapContext(
    const GLContextCreateDesc& desc, const IntSize& size,
    nsACString* const out_failureId) {
  GLXLibrary* glx = &sGLXLibrary;
  auto display = glx->GetDisplay();

  if (!display || !glx->EnsureInitialized(*display)) return nullptr;

  int screen = DefaultScreen(display->get());

  GLXFBConfig config;
  int visid;
  if (!ChooseConfig(glx, *display, screen, &config, &visid)) {
    NS_WARNING("Failed to find a compatible config.");
    return nullptr;
  }

  Visual* visual;
  int depth;
  FindVisualAndDepth(*display, visid, &visual, &depth);

  gfx::IntSize dummySize(16, 16);
  const auto drawable =
      XCreatePixmap(*display, DefaultRootWindow(display->get()),
                    dummySize.width, dummySize.height, depth);
  if (!drawable) {
    return nullptr;
  }

  // Handle slightly different signature between glXCreatePixmap and
  // its pre-GLX-1.3 extension equivalent (though given the ABI, we
  // might not need to).
  const auto pixmap = glx->fCreatePixmap(*display, config, drawable, nullptr);
  if (pixmap == 0) {
    XFreePixmap(*display, drawable);
    return nullptr;
  }

  auto fullDesc = GLContextDesc{desc};
  fullDesc.isOffscreen = true;

  return GLContextGLX::CreateGLContext(fullDesc, display, pixmap, config,
                                       drawable);
}

/*static*/
already_AddRefed<GLContext> GLContextProviderGLX::CreateHeadless(
    const GLContextCreateDesc& desc, nsACString* const out_failureId) {
  IntSize dummySize = IntSize(16, 16);
  return CreateOffscreenPixmapContext(desc, dummySize, out_failureId);
}

/*static*/
GLContext* GLContextProviderGLX::GetGlobalContext() {
  // Context sharing not supported.
  return nullptr;
}

/*static*/
void GLContextProviderGLX::Shutdown() {}

}  // namespace mozilla::gl
