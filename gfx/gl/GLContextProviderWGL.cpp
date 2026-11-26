/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GLContextProvider.h"
#include "GLContextWGL.h"
#include "GLLibraryLoader.h"
#include "gfxCrashReporterUtils.h"
#include "gfxPlatform.h"
#include "mozilla/Preferences.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/SharedLibrary.h"
#include "mozilla/StaticPrefs_gl.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/CompositorOptions.h"
#include "mozilla/widget/CompositorWidget.h"
#include "mozilla/widget/WinCompositorWidget.h"
#include "nsDebug.h"
#include "nsIWidget.h"
#include "prenv.h"

namespace mozilla {
namespace gl {

using namespace mozilla::gfx;
using namespace mozilla::widget;

constinit WGLLibrary sWGLLib;

/*
ScopedWindow::~ScopedWindow()
{
    if (mDC) {
        MOZ_ALWAYS_TRUE( ReleaseDC(mDC) );
    }
    if (mWindow) {
        MOZ_ALWAYS_TRUE( DestroyWindow(mWindow) );
    }
}
*/
static HWND CreateDummyWindow() {
  WNDCLASSW wc{};
  if (!GetClassInfoW(GetModuleHandle(nullptr), L"GLContextWGLClass", &wc)) {
    wc = {};
    wc.style = CS_OWNDC;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpfnWndProc = DefWindowProc;
    wc.lpszClassName = L"GLContextWGLClass";
    if (!RegisterClassW(&wc)) {
      NS_WARNING("Failed to register GLContextWGLClass?!");
      // er. failed to register our class?
      return nullptr;
    }
  }

  return CreateWindowW(L"GLContextWGLClass", L"GLContextWGL", 0, 0, 0, 1, 1,
                       nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
}

static inline bool HasExtension(const char* aExtensions,
                                const char* aRequiredExtension) {
  return GLContext::ListHasExtension(
      reinterpret_cast<const GLubyte*>(aExtensions), aRequiredExtension);
}

SymbolLoader WGLLibrary::GetSymbolLoader() const {
  auto ret = SymbolLoader(*mOGLLibrary);
  ret.mPfn = SymbolLoader::GetProcAddressT(mSymbols.fGetProcAddress);
  return ret;
}

bool WGLLibrary::EnsureInitialized() {
  if (mInitialized) return true;

  mozilla::ScopedGfxFeatureReporter reporter("WGL");

  std::wstring libGLFilename = L"Opengl32.dll";
  // SU_SPIES_DIRECTORY is for AMD CodeXL/gDEBugger
  if (_wgetenv(L"SU_SPIES_DIRECTORY")) {
    libGLFilename =
        std::wstring(_wgetenv(L"SU_SPIES_DIRECTORY")) + L"\\opengl32.dll";
  }

  if (!mOGLLibrary) {
    mOGLLibrary = LoadLibraryWithFlags(libGLFilename.c_str());
    if (!mOGLLibrary) {
      NS_WARNING("Couldn't load OpenGL library.");
      return false;
    }
  }

#define SYMBOL(X)                 \
  {                               \
    (PRFuncPtr*)&mSymbols.f##X, { \
      {                           \
        "wgl" #X                  \
      }                           \
    }                             \
  }
#define END_OF_SYMBOLS {nullptr, {}}

  {
    const auto loader = SymbolLoader(*mOGLLibrary);
    const SymLoadStruct earlySymbols[] = {SYMBOL(CreateContext),
                                          SYMBOL(MakeCurrent),
                                          SYMBOL(GetProcAddress),
                                          SYMBOL(DeleteContext),
                                          SYMBOL(GetCurrentContext),
                                          SYMBOL(GetCurrentDC),
                                          END_OF_SYMBOLS};

    if (!loader.LoadSymbols(earlySymbols)) {
      NS_WARNING(
          "Couldn't find required entry points in OpenGL DLL (early init)");
      return false;
    }
  }

  mDummyWindow = CreateDummyWindow();
  MOZ_ASSERT(mDummyWindow);
  if (!mDummyWindow) return false;
  auto cleanup = MakeScopeExit([&]() { Reset(); });

  mRootDc = GetDC(mDummyWindow);
  MOZ_ASSERT(mRootDc);
  if (!mRootDc) return false;

  // --

  {
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    // pfd.iPixelType = PFD_TYPE_RGBA;
    // pfd.cColorBits = 24;
    // pfd.cRedBits = 8;
    // pfd.cGreenBits = 8;
    // pfd.cBlueBits = 8;
    // pfd.cAlphaBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const auto pixelFormat = ChoosePixelFormat(mRootDc, &pfd);
    MOZ_ASSERT(pixelFormat);
    if (!pixelFormat) return false;
    const bool setPixelFormatOk = SetPixelFormat(mRootDc, pixelFormat, nullptr);
    MOZ_ASSERT(setPixelFormatOk);
    if (!setPixelFormatOk) return false;
  }

  // --

  // create rendering context
  mDummyGlrc = mSymbols.fCreateContext(mRootDc);
  if (!mDummyGlrc) return false;

  const auto curCtx = mSymbols.fGetCurrentContext();
  const auto curDC = mSymbols.fGetCurrentDC();

  GLContext::ResetTLSCurrentContext();

  if (!mSymbols.fMakeCurrent(mRootDc, mDummyGlrc)) {
    NS_WARNING("wglMakeCurrent failed");
    return false;
  }
  const auto resetContext = MakeScopeExit([&]() {
    GLContext::ResetTLSCurrentContext();
    mSymbols.fMakeCurrent(curDC, curCtx);
  });

  const auto loader = GetSymbolLoader();

  // Now we can grab all the other symbols that we couldn't without having
  // a context current.
  // clang-format off
    const SymLoadStruct reqExtSymbols[] = {
        { (PRFuncPtr*)&mSymbols.fCreatePbuffer, {{ "wglCreatePbufferARB", "wglCreatePbufferEXT" }} },
        { (PRFuncPtr*)&mSymbols.fDestroyPbuffer, {{ "wglDestroyPbufferARB", "wglDestroyPbufferEXT" }} },
        { (PRFuncPtr*)&mSymbols.fGetPbufferDC, {{ "wglGetPbufferDCARB", "wglGetPbufferDCEXT" }} },
        { (PRFuncPtr*)&mSymbols.fReleasePbufferDC, {{ "wglReleasePbufferDCARB", "wglReleasePbufferDCEXT" }} },
    //    { (PRFuncPtr*)&mSymbols.fBindTexImage, {{ "wglBindTexImageARB", "wglBindTexImageEXT" }} },
    //    { (PRFuncPtr*)&mSymbols.fReleaseTexImage, {{ "wglReleaseTexImageARB", "wglReleaseTexImageEXT" }} },
        { (PRFuncPtr*)&mSymbols.fChoosePixelFormat, {{ "wglChoosePixelFormatARB", "wglChoosePixelFormatEXT" }} },
    //    { (PRFuncPtr*)&mSymbols.fGetPixelFormatAttribiv, {{ "wglGetPixelFormatAttribivARB", "wglGetPixelFormatAttribivEXT" }} },
        SYMBOL(GetExtensionsStringARB),
        END_OF_SYMBOLS
    };
  // clang-format on
  if (!loader.LoadSymbols(reqExtSymbols)) {
    NS_WARNING("reqExtSymbols missing");
    return false;
  }

  // --

  const auto extString = mSymbols.fGetExtensionsStringARB(mRootDc);
  MOZ_ASSERT(extString);

  // --

  if (HasExtension(extString, "WGL_ARB_create_context")) {
    const SymLoadStruct createContextSymbols[] = {
        SYMBOL(CreateContextAttribsARB), END_OF_SYMBOLS};
    if (loader.LoadSymbols(createContextSymbols)) {
      if (HasExtension(extString, "WGL_ARB_create_context_robustness")) {
        mHasRobustness = true;
      }
    } else {
      NS_ERROR(
          "WGL_ARB_create_context announced without supplying its functions.");
      ClearSymbols(createContextSymbols);
    }
  }

  // --

  bool hasDXInterop2 = HasExtension(extString, "WGL_NV_DX_interop2");
  if (gfxVars::DXInterop2Blocked() &&
      !StaticPrefs::gl_ignore_dx_interop2_blacklist()) {
    hasDXInterop2 = false;
  }

  if (hasDXInterop2) {
    const SymLoadStruct dxInteropSymbols[] = {
        SYMBOL(DXSetResourceShareHandleNV),
        SYMBOL(DXOpenDeviceNV),
        SYMBOL(DXCloseDeviceNV),
        SYMBOL(DXRegisterObjectNV),
        SYMBOL(DXUnregisterObjectNV),
        SYMBOL(DXObjectAccessNV),
        SYMBOL(DXLockObjectsNV),
        SYMBOL(DXUnlockObjectsNV),
        END_OF_SYMBOLS};
    if (!loader.LoadSymbols(dxInteropSymbols)) {
      NS_ERROR(
          "WGL_NV_DX_interop2 announceed without supplying its functions.");
      ClearSymbols(dxInteropSymbols);
    }
  }

  // --

  cleanup.release();

  mInitialized = true;

  reporter.SetSuccessful();
  return true;
}

#undef SYMBOL
#undef END_OF_SYMBOLS

void WGLLibrary::Reset() {
  if (mDummyGlrc) {
    (void)mSymbols.fDeleteContext(mDummyGlrc);
    mDummyGlrc = nullptr;
  }
  if (mRootDc) {
    (void)ReleaseDC(mDummyWindow, mRootDc);
    mRootDc = nullptr;
  }
  if (mDummyWindow) {
    (void)DestroyWindow(mDummyWindow);
    mDummyWindow = nullptr;
  }
}

GLContextWGL::GLContextWGL(const GLContextDesc& desc, HDC aDC, HGLRC aContext,
                           HWND aWindow)
    : GLContext(desc, nullptr, false),
      mDC(aDC),
      mContext(aContext),
      mWnd(aWindow),
      mPBuffer(nullptr),
      mPixelFormat(0) {}

GLContextWGL::GLContextWGL(const GLContextDesc& desc, HANDLE aPbuffer, HDC aDC,
                           HGLRC aContext, int aPixelFormat)
    : GLContext(desc, nullptr, false),
      mDC(aDC),
      mContext(aContext),
      mWnd(nullptr),
      mPBuffer(aPbuffer),
      mPixelFormat(aPixelFormat) {}

GLContextWGL::~GLContextWGL() {
  MarkDestroyed();

  (void)sWGLLib.mSymbols.fDeleteContext(mContext);

  if (mPBuffer) {
    (void)sWGLLib.mSymbols.fReleasePbufferDC(mPBuffer, mDC);
    (void)sWGLLib.mSymbols.fDestroyPbuffer(mPBuffer);
  }
  if (mWnd) {
    (void)ReleaseDC(mWnd, mDC);
    DestroyWindow(mWnd);
  }
}

bool GLContextWGL::MakeCurrentImpl() const {
  GLContext::ResetTLSCurrentContext();

  const bool succeeded = sWGLLib.mSymbols.fMakeCurrent(mDC, mContext);
  NS_ASSERTION(succeeded, "Failed to make GL context current!");
  return succeeded;
}

bool GLContextWGL::IsCurrentImpl() const {
  return sWGLLib.mSymbols.fGetCurrentContext() == mContext;
}

bool GLContextWGL::SwapBuffers() {
  if (!mIsDoubleBuffered) return false;
  return ::SwapBuffers(mDC);
}

void GLContextWGL::GetWSIInfo(nsCString* const out) const {
  out->AppendLiteral("wglGetExtensionsString: ");
  out->Append(sWGLLib.mSymbols.fGetExtensionsStringARB(mDC));
}

HGLRC
WGLLibrary::CreateContextWithFallback(const HDC dc,
                                      const bool tryRobustBuffers) const {
  if (mHasRobustness) {
    if (tryRobustBuffers) {
      const int attribs[] = {LOCAL_WGL_CONTEXT_FLAGS_ARB,
                             LOCAL_WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB,
                             LOCAL_WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB,
                             LOCAL_WGL_LOSE_CONTEXT_ON_RESET_ARB, 0};
      const auto context =
          mSymbols.fCreateContextAttribsARB(dc, nullptr, attribs);
      if (context) return context;
    }

    const int attribs[] = {LOCAL_WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB,
                           LOCAL_WGL_LOSE_CONTEXT_ON_RESET_ARB, 0};
    const auto context =
        mSymbols.fCreateContextAttribsARB(dc, nullptr, attribs);
    if (context) return context;
  }
  if (mSymbols.fCreateContextAttribsARB) {
    const auto context =
        mSymbols.fCreateContextAttribsARB(dc, nullptr, nullptr);
    if (context) return context;
  }
  return mSymbols.fCreateContext(dc);
}

static RefPtr<GLContext> CreateForWidget(const HWND window,
                                         const bool isWebRender,
                                         const bool requireAccelerated) {
  auto& wgl = sWGLLib;
  if (!wgl.EnsureInitialized()) return nullptr;

  const auto dc = GetDC(window);
  if (!dc) return nullptr;
  auto cleanupDc = MakeScopeExit([&]() { (void)ReleaseDC(window, dc); });

  int chosenFormat;
  UINT foundFormats = 0;

  if (!foundFormats) {
    const int kAttribs[] = {LOCAL_WGL_DRAW_TO_WINDOW_ARB,
                            true,
                            LOCAL_WGL_SUPPORT_OPENGL_ARB,
                            true,
                            LOCAL_WGL_DOUBLE_BUFFER_ARB,
                            true,
                            LOCAL_WGL_ACCELERATION_ARB,
                            LOCAL_WGL_FULL_ACCELERATION_ARB,
                            0};
    const int kAttribsForWebRender[] = {LOCAL_WGL_DRAW_TO_WINDOW_ARB,
                                        true,
                                        LOCAL_WGL_SUPPORT_OPENGL_ARB,
                                        true,
                                        LOCAL_WGL_DOUBLE_BUFFER_ARB,
                                        true,
                                        LOCAL_WGL_DEPTH_BITS_ARB,
                                        24,
                                        LOCAL_WGL_ACCELERATION_ARB,
                                        LOCAL_WGL_FULL_ACCELERATION_ARB,
                                        0};
    const int* attribs;
    if (isWebRender) {
      attribs = kAttribsForWebRender;
    } else {
      attribs = kAttribs;
    }

    if (!wgl.mSymbols.fChoosePixelFormat(wgl.RootDc(), attribs, nullptr, 1,
                                         &chosenFormat, &foundFormats)) {
      foundFormats = 0;
    }
  }
  if (!foundFormats) {
    if (requireAccelerated) return nullptr;

    const int kAttribs[] = {LOCAL_WGL_DRAW_TO_WINDOW_ARB,
                            true,
                            LOCAL_WGL_SUPPORT_OPENGL_ARB,
                            true,
                            LOCAL_WGL_DOUBLE_BUFFER_ARB,
                            true,
                            0};
    const int kAttribsForWebRender[] = {LOCAL_WGL_DRAW_TO_WINDOW_ARB,
                                        true,
                                        LOCAL_WGL_SUPPORT_OPENGL_ARB,
                                        true,
                                        LOCAL_WGL_DOUBLE_BUFFER_ARB,
                                        true,
                                        LOCAL_WGL_DEPTH_BITS_ARB,
                                        24,
                                        0};

    const int* attribs;
    if (isWebRender) {
      attribs = kAttribsForWebRender;
    } else {
      attribs = kAttribs;
    }

    if (!wgl.mSymbols.fChoosePixelFormat(wgl.RootDc(), attribs, nullptr, 1,
                                         &chosenFormat, &foundFormats)) {
      foundFormats = 0;
    }
  }
  if (!foundFormats) return nullptr;

  // We need to make sure we call SetPixelFormat -after- calling
  // EnsureInitialized, otherwise it can load/unload the dll and
  // wglCreateContext will fail.

  SetPixelFormat(dc, chosenFormat, nullptr);
  const auto context = sWGLLib.CreateContextWithFallback(dc, false);
  if (!context) return nullptr;

  const RefPtr<GLContextWGL> gl = new GLContextWGL({}, dc, context);
  cleanupDc.release();
  gl->mIsDoubleBuffered = true;
  if (!gl->Init()) return nullptr;

  return gl;
}

already_AddRefed<GLContext> GLContextProviderWGL::CreateForCompositorWidget(
    CompositorWidget* aCompositorWidget, bool aHardwareWebRender,
    bool aForceAccelerated) {
  if (!aCompositorWidget) {
    MOZ_ASSERT(false);
    return nullptr;
  }
  return CreateForWidget(aCompositorWidget->AsWindows()->GetHwnd(),
                         aHardwareWebRender, aForceAccelerated)
      .forget();
}

/*static*/
already_AddRefed<GLContext> GLContextProviderWGL::CreateHeadless(
    const GLContextCreateDesc& desc, nsACString* const out_failureId) {
  auto& wgl = sWGLLib;
  if (!wgl.EnsureInitialized()) return nullptr;

  int chosenFormat;
  UINT foundFormats = 0;

  bool forbidHardware =
      static_cast<bool>(desc.flags & CreateContextFlags::FORBID_HARDWARE);
  if (!foundFormats && forbidHardware) {
    const int kAttribs[] = {LOCAL_WGL_DRAW_TO_PBUFFER_ARB,
                            true,
                            LOCAL_WGL_SUPPORT_OPENGL_ARB,
                            true,
                            LOCAL_WGL_ACCELERATION_ARB,
                            LOCAL_WGL_NO_ACCELERATION_ARB,
                            0};
    if (!wgl.mSymbols.fChoosePixelFormat(wgl.RootDc(), kAttribs, nullptr, 1,
                                         &chosenFormat, &foundFormats)) {
      foundFormats = 0;
      return nullptr;
    }
  }

  if (!foundFormats && !forbidHardware) {
    const int kAttribs[] = {LOCAL_WGL_DRAW_TO_PBUFFER_ARB,
                            true,
                            LOCAL_WGL_SUPPORT_OPENGL_ARB,
                            true,
                            LOCAL_WGL_ACCELERATION_ARB,
                            LOCAL_WGL_FULL_ACCELERATION_ARB,
                            0};
    if (!wgl.mSymbols.fChoosePixelFormat(wgl.RootDc(), kAttribs, nullptr, 1,
                                         &chosenFormat, &foundFormats)) {
      foundFormats = 0;
    }
  }
  if (!foundFormats) {
    const int kAttribs[] = {LOCAL_WGL_DRAW_TO_PBUFFER_ARB, true,
                            LOCAL_WGL_SUPPORT_OPENGL_ARB, true, 0};
    if (!wgl.mSymbols.fChoosePixelFormat(wgl.RootDc(), kAttribs, nullptr, 1,
                                         &chosenFormat, &foundFormats)) {
      foundFormats = 0;
    }
  }
  if (!foundFormats) return nullptr;
  const int kPbufferAttribs[] = {0};
  const auto pbuffer = wgl.mSymbols.fCreatePbuffer(wgl.RootDc(), chosenFormat,
                                                   1, 1, kPbufferAttribs);
  if (!pbuffer) return nullptr;
  auto cleanupPbuffer =
      MakeScopeExit([&]() { (void)wgl.mSymbols.fDestroyPbuffer(pbuffer); });

  const auto dc = wgl.mSymbols.fGetPbufferDC(pbuffer);
  if (!dc) return nullptr;
  auto cleanupDc = MakeScopeExit(
      [&]() { (void)wgl.mSymbols.fReleasePbufferDC(pbuffer, dc); });

  const auto context = wgl.CreateContextWithFallback(dc, true);
  if (!context) return nullptr;

  const auto fullDesc = GLContextDesc{desc, true};
  const RefPtr<GLContextWGL> gl =
      new GLContextWGL(fullDesc, pbuffer, dc, context, chosenFormat);
  cleanupPbuffer.release();
  cleanupDc.release();
  if (!gl->Init()) return nullptr;

  return RefPtr<GLContext>(gl.get()).forget();
}

/*static*/
GLContext* GLContextProviderWGL::GetGlobalContext() { return nullptr; }

/*static*/
void GLContextProviderWGL::Shutdown() {}

} /* namespace gl */
} /* namespace mozilla */
