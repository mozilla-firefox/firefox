/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLContext.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <queue>
#include <regex>

#include "AccessCheck.h"
#include "CompositableHost.h"
#include "gfxConfig.h"
#include "gfxContext.h"
#include "gfxCrashReporterUtils.h"
#include "gfxEnv.h"
#include "gfxPattern.h"
#include "MozFramebuffer.h"
#include "GLBlitHelper.h"
#include "GLContext.h"
#include "GLContextProvider.h"
#include "GLReadTexImageHelper.h"
#include "GLScreenBuffer.h"
#include "ImageContainer.h"
#include "ImageEncoder.h"
#include "LayerUserData.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/ImageData.h"
#include "mozilla/dom/WebGLContextEvent.h"
#include "mozilla/EnumeratedArrayCycleCollection.h"
#include "mozilla/EnumeratedRange.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProcessPriorityManager.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/glean/DomCanvasMetrics.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "nsError.h"
#include "nsIClassInfoImpl.h"
#include "nsIWidget.h"
#include "nsServiceManagerUtils.h"
#include "SharedSurfaceGL.h"
#include "prenv.h"
#include "ScopedGLHelpers.h"
#include "VRManagerChild.h"
#include "mozilla/gfx/Swizzle.h"
#include "mozilla/layers/BufferTexture.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/WebRenderUserData.h"
#include "mozilla/layers/WebRenderCanvasRenderer.h"

// Local
#include "CanvasUtils.h"
#include "ClientWebGLContext.h"
#include "HostWebGLContext.h"
#include "WebGLBuffer.h"
#include "WebGLChild.h"
#include "WebGLContextLossHandler.h"
#include "WebGLContextUtils.h"
#include "WebGLExtensions.h"
#include "WebGLFormats.h"
#include "WebGLFramebuffer.h"
#include "WebGLMemoryTracker.h"
#include "WebGLObjectModel.h"
#include "WebGLParent.h"
#include "WebGLProgram.h"
#include "WebGLQuery.h"
#include "WebGLSampler.h"
#include "WebGLShader.h"
#include "WebGLShaderValidator.h"
#include "WebGLSync.h"
#include "WebGLTransformFeedback.h"
#include "WebGLValidateStrings.h"
#include "WebGLVertexArray.h"

#ifdef XP_WIN
#  include "WGLLibrary.h"
#endif

// Generated
#include "mozilla/dom/WebGLRenderingContextBinding.h"

namespace mozilla {

WebGLContextOptions::WebGLContextOptions() {
  // Set default alpha state based on preference.
  alpha = !StaticPrefs::webgl_default_no_alpha();
  antialias = StaticPrefs::webgl_default_antialias();
}

StaticMutex WebGLContext::sLruMutex;
MOZ_RUNINIT std::list<WebGLContext*> WebGLContext::sLru;

WebGLContext::LruPosition::LruPosition() {
  StaticMutexAutoLock lock(sLruMutex);
  mItr = sLru.end();
}  // NOLINT

WebGLContext::LruPosition::LruPosition(WebGLContext& context) {
  StaticMutexAutoLock lock(sLruMutex);
  mItr = sLru.insert(sLru.end(), &context);
}

void WebGLContext::LruPosition::AssignLocked(WebGLContext& aContext) {
  ResetLocked();
  mItr = sLru.insert(sLru.end(), &aContext);
}

void WebGLContext::LruPosition::ResetLocked() {
  const auto end = sLru.end();
  if (mItr != end) {
    sLru.erase(mItr);
    mItr = end;
  }
}

void WebGLContext::LruPosition::Reset() {
  StaticMutexAutoLock lock(sLruMutex);
  ResetLocked();
}

bool WebGLContext::LruPosition::IsInsertedLocked() const {
  return mItr != sLru.end();
}

WebGLContext::WebGLContext(HostWebGLContext* host,
                           const webgl::InitContextDesc& desc)
    : gl(mGL_OnlyClearInDestroyResourcesAndContext),  // const reference
      mHost(host),
      mResistFingerprinting(desc.resistFingerprinting),
      mOptions(desc.options),
      mPrincipalKey(desc.principalKey),
      mContextLossHandler(this),
      mRequestedSize(desc.size) {
  if (host) {
    host->mContext = this;
  }
  const FuncScope funcScope(*this, "<Create>");
  WebGLMemoryTracker::EnsureRegistered();
}

WebGLContext::~WebGLContext() { DestroyResourcesAndContext(); }

void WebGLContext::DestroyResourcesAndContext() {
  if (mRemoteTextureOwner) {
    // Clean up any remote textures registered for framebuffer swap chains.
    mRemoteTextureOwner->UnregisterAllTextureOwners();
    mRemoteTextureOwner = nullptr;
  }

  if (!gl) return;

  mDefaultFB = nullptr;
  mResolvedDefaultFB = nullptr;

  mBound2DTextures.Clear();
  mBoundCubeMapTextures.Clear();
  mBound3DTextures.Clear();
  mBound2DArrayTextures.Clear();
  mBoundSamplers.Clear();
  mBoundArrayBuffer = nullptr;
  mBoundCopyReadBuffer = nullptr;
  mBoundCopyWriteBuffer = nullptr;
  mBoundPixelPackBuffer = nullptr;
  mBoundPixelUnpackBuffer = nullptr;
  mBoundTransformFeedbackBuffer = nullptr;
  mBoundUniformBuffer = nullptr;
  mCurrentProgram = nullptr;
  mActiveProgramLinkInfo = nullptr;
  mBoundDrawFramebuffer = nullptr;
  mBoundReadFramebuffer = nullptr;
  mBoundVertexArray = nullptr;
  mDefaultVertexArray = nullptr;
  mBoundTransformFeedback = nullptr;
  mDefaultTransformFeedback = nullptr;

  mQuerySlot_SamplesPassed = nullptr;
  mQuerySlot_TFPrimsWritten = nullptr;
  mQuerySlot_TimeElapsed = nullptr;

  mIndexedUniformBufferBindings.clear();

  //////

  if (mEmptyTFO) {
    gl->fDeleteTransformFeedbacks(1, &mEmptyTFO);
    mEmptyTFO = 0;
  }

  //////

  if (mFakeVertexAttrib0BufferObject) {
    gl->fDeleteBuffers(1, &mFakeVertexAttrib0BufferObject);
    mFakeVertexAttrib0BufferObject = 0;
  }

  // disable all extensions except "WEBGL_lose_context". see bug #927969
  // spec: http://www.khronos.org/registry/webgl/specs/latest/1.0/#5.15.2
  for (size_t i = 0; i < size_t(WebGLExtensionID::Max); ++i) {
    WebGLExtensionID extension = WebGLExtensionID(i);
    if (extension == WebGLExtensionID::WEBGL_lose_context) continue;
    mExtensions[extension] = nullptr;
  }

  // We just got rid of everything, so the context had better
  // have been going away.
  if (gl::GLContext::ShouldSpew()) {
    printf_stderr("--- WebGL context destroyed: %p\n", gl.get());
  }

  MOZ_ASSERT(gl);
  gl->MarkDestroyed();
  mGL_OnlyClearInDestroyResourcesAndContext = nullptr;
  MOZ_ASSERT(!gl);
}

void ClientWebGLContext::MarkCanvasDirty() {
  if (!mCanvasElement && !mOffscreenCanvas) return;

  mFrameCaptureState = FrameCaptureState::DIRTY;

  if (mIsCanvasDirty) return;
  mIsCanvasDirty = true;

  if (mCanvasElement) {
    SVGObserverUtils::InvalidateDirectRenderingObservers(mCanvasElement);
    mCanvasElement->InvalidateCanvasContent(nullptr);
  } else if (mOffscreenCanvas) {
    mOffscreenCanvas->QueueCommitToCompositor();
  }
}

void WebGLContext::OnMemoryPressure() {
  bool shouldLoseContext = mLoseContextOnMemoryPressure;

  if (!mCanLoseContextInForeground &&
      ProcessPriorityManager::CurrentProcessIsForeground()) {
    shouldLoseContext = false;
  }

  if (shouldLoseContext) LoseContext();
}

// --

bool WebGLContext::CreateAndInitGL(
    bool forceEnabled, std::vector<FailureReason>* const out_failReasons) {
  const FuncScope funcScope(*this, "<Create>");

  // WebGL2 is separately blocked:
  if (IsWebGL2() && !forceEnabled) {
    FailureReason reason;
    if (!gfx::gfxVars::AllowWebgl2()) {
      reason.info =
          "AllowWebgl2:false restricts context creation on this system.";
      out_failReasons->push_back(reason);
      GenerateWarning("%s", reason.info.BeginReading());
      return false;
    }
  }

  auto flags = gl::CreateContextFlags::PREFER_ROBUSTNESS;

  if (StaticPrefs::webgl_gl_khr_no_error()) {
    flags |= gl::CreateContextFlags::NO_VALIDATION;
  }

  // -

  if (StaticPrefs::webgl_forbid_hardware()) {
    flags |= gl::CreateContextFlags::FORBID_HARDWARE;
  }
  if (StaticPrefs::webgl_forbid_software()) {
    flags |= gl::CreateContextFlags::FORBID_SOFTWARE;
  }

  if (mOptions.forceSoftwareRendering) {
    flags |= gl::CreateContextFlags::FORBID_HARDWARE;
    flags &= ~gl::CreateContextFlags::FORBID_SOFTWARE;
  }

  if (forceEnabled) {
    flags &= ~gl::CreateContextFlags::FORBID_HARDWARE;
    flags &= ~gl::CreateContextFlags::FORBID_SOFTWARE;
  }

  if ((flags & gl::CreateContextFlags::FORBID_HARDWARE) &&
      (flags & gl::CreateContextFlags::FORBID_SOFTWARE)) {
    FailureReason reason;
    reason.info = "Both hardware and software were forbidden by config.";
    out_failReasons->push_back(reason);
    GenerateWarning("%s", reason.info.BeginReading());
    return false;
  }

  // -

  if (StaticPrefs::webgl_cgl_multithreaded()) {
    flags |= gl::CreateContextFlags::PREFER_MULTITHREADED;
  }

  if (IsWebGL2()) {
    flags |= gl::CreateContextFlags::PREFER_ES3;
  } else {
    if (StaticPrefs::webgl_1_request_es2()) {
      // Request and prefer ES2 context for WebGL1.
      flags |= gl::CreateContextFlags::PREFER_EXACT_VERSION;
    }

    if (!StaticPrefs::webgl_1_allow_core_profiles()) {
      flags |= gl::CreateContextFlags::REQUIRE_COMPAT_PROFILE;
    }
  }

  {
    auto powerPref = mOptions.powerPreference;

    // If "Use hardware acceleration when available" option is disabled:
    if (!gfx::gfxConfig::IsEnabled(gfx::Feature::HW_COMPOSITING)) {
      powerPref = dom::WebGLPowerPreference::Low_power;
    }

    const auto overrideVal = StaticPrefs::webgl_power_preference_override();
    if (overrideVal > 0) {
      powerPref = dom::WebGLPowerPreference::High_performance;
    } else if (overrideVal < 0) {
      powerPref = dom::WebGLPowerPreference::Low_power;
    }

    if (powerPref == dom::WebGLPowerPreference::High_performance) {
      flags |= gl::CreateContextFlags::HIGH_POWER;
    }
  }

  if (!gfx::gfxVars::WebglAllowCoreProfile()) {
    flags |= gl::CreateContextFlags::REQUIRE_COMPAT_PROFILE;
  }

  // --

  const bool useEGL = PR_GetEnv("MOZ_WEBGL_FORCE_EGL");

  bool tryNativeGL = true;
  bool tryANGLE = false;

#ifdef XP_WIN
  tryNativeGL = false;
  tryANGLE = true;

  if (StaticPrefs::webgl_disable_wgl()) {
    tryNativeGL = false;
  }

  if (StaticPrefs::webgl_disable_angle() ||
      PR_GetEnv("MOZ_WEBGL_FORCE_OPENGL") || useEGL) {
    tryNativeGL = true;
    tryANGLE = false;
  }
#endif

  if (tryNativeGL && !forceEnabled) {
    FailureReason reason;
    if (!gfx::gfxVars::WebglAllowWindowsNativeGl()) {
      reason.info =
          "WebglAllowWindowsNativeGl:false restricts context creation on this "
          "system.";

      out_failReasons->push_back(reason);

      GenerateWarning("%s", reason.info.BeginReading());
      tryNativeGL = false;
    }
  }

  // --

  using fnCreateT = decltype(gl::GLContextProviderEGL::CreateHeadless);
  const auto fnCreate = [&](fnCreateT* const pfnCreate,
                            const char* const info) {
    nsCString failureId;
    const RefPtr<gl::GLContext> gl = pfnCreate({flags}, &failureId);
    if (!gl) {
      out_failReasons->push_back(WebGLContext::FailureReason(failureId, info));
    }
    return gl;
  };

  const auto newGL = [&]() -> RefPtr<gl::GLContext> {
    if (tryNativeGL) {
      if (useEGL)
        return fnCreate(&gl::GLContextProviderEGL::CreateHeadless, "useEGL");

      const auto ret =
          fnCreate(&gl::GLContextProvider::CreateHeadless, "tryNativeGL");
      if (ret) return ret;
    }

    if (tryANGLE) {
      return fnCreate(&gl::GLContextProviderEGL::CreateHeadless, "tryANGLE");
    }
    return nullptr;
  }();

  if (!newGL) {
    out_failReasons->push_back(
        FailureReason("FEATURE_FAILURE_WEBGL_EXHAUSTED_DRIVERS",
                      "Exhausted GL driver options."));
    return false;
  }

  // --

  FailureReason reason;

  mGL_OnlyClearInDestroyResourcesAndContext = newGL;
  MOZ_RELEASE_ASSERT(gl);
  if (!InitAndValidateGL(&reason)) {
    DestroyResourcesAndContext();
    MOZ_RELEASE_ASSERT(!gl);

    // The fail reason here should be specific enough for now.
    out_failReasons->push_back(reason);
    return false;
  }

  const auto val = StaticPrefs::webgl_debug_incomplete_tex_color();
  if (val) {
    mIncompleteTexOverride.reset(new gl::Texture(*gl));
    const gl::ScopedBindTexture autoBind(gl, mIncompleteTexOverride->name);
    const auto heapVal = std::make_unique<uint32_t>(val);
    gl->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, LOCAL_GL_RGBA, 1, 1, 0,
                    LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE, heapVal.get());
  }

  return true;
}

// Fallback for resizes:

bool WebGLContext::EnsureDefaultFB() {
  if (mDefaultFB) {
    MOZ_ASSERT(*uvec2::FromSize(mDefaultFB->mSize) == mRequestedSize);
    return true;
  }

  const bool depthStencil = mOptions.depth || mOptions.stencil;
  auto attemptSize = gfx::IntSize{mRequestedSize.x, mRequestedSize.y};

  while (attemptSize.width || attemptSize.height) {
    attemptSize.width = std::max(attemptSize.width, 1);
    attemptSize.height = std::max(attemptSize.height, 1);

    [&]() {
      if (mOptions.antialias) {
        MOZ_ASSERT(!mDefaultFB);
        mDefaultFB = gl::MozFramebuffer::Create(gl, attemptSize, mMsaaSamples,
                                                depthStencil);
        if (mDefaultFB) return;
        if (mOptionsFrozen) return;
      }

      MOZ_ASSERT(!mDefaultFB);
      mDefaultFB = gl::MozFramebuffer::Create(gl, attemptSize, 0, depthStencil);
    }();

    if (mDefaultFB) break;

    attemptSize.width /= 2;
    attemptSize.height /= 2;
  }

  if (!mDefaultFB) {
    GenerateWarning("Backbuffer resize failed. Losing context.");
    LoseContext();
    return false;
  }

  mDefaultFB_IsInvalid = true;

  const auto actualSize = *uvec2::FromSize(mDefaultFB->mSize);
  if (actualSize != mRequestedSize) {
    GenerateWarning(
        "Requested size %ux%u was too large, but resize"
        " to %ux%u succeeded.",
        mRequestedSize.x, mRequestedSize.y, actualSize.x, actualSize.y);
  }
  mRequestedSize = actualSize;
  return true;
}

void WebGLContext::Resize(uvec2 requestedSize) {
  // Zero-sized surfaces can cause problems.
  if (!requestedSize.x) {
    requestedSize.x = 1;
  }
  if (!requestedSize.y) {
    requestedSize.y = 1;
  }

  // Kill our current default fb(s), for later lazy allocation.
  mRequestedSize = requestedSize;
  mDefaultFB = nullptr;
  mResetLayer = true;  // New size means new Layer.
}

std::unique_ptr<webgl::FormatUsageAuthority> WebGLContext::CreateFormatUsage(
    gl::GLContext* gl) const {
  return webgl::FormatUsageAuthority::CreateForWebGL1(gl);
}

/*static*/
RefPtr<WebGLContext> WebGLContext::Create(HostWebGLContext* host,
                                          const webgl::InitContextDesc& desc,
                                          webgl::InitContextResult* const out) {
  AUTO_PROFILER_LABEL("WebGLContext::Create", GRAPHICS);
  nsCString failureId = "FEATURE_FAILURE_WEBGL_UNKOWN"_ns;
  const bool forceEnabled = StaticPrefs::webgl_force_enabled();
  ScopedGfxFeatureReporter reporter("WebGL", forceEnabled);

  auto res = [&]() -> Result<RefPtr<WebGLContext>, std::string> {
    bool disabled = StaticPrefs::webgl_disabled();

    // TODO: When we have software webgl support we should use that instead.
    disabled |= gfxPlatform::InSafeMode();

    if (disabled) {
      if (gfxPlatform::InSafeMode()) {
        failureId = "FEATURE_FAILURE_WEBGL_SAFEMODE"_ns;
      } else {
        failureId = "FEATURE_FAILURE_WEBGL_DISABLED"_ns;
      }
      return Err("WebGL is currently disabled.");
    }

    // Alright, now let's start trying.

    RefPtr<WebGLContext> webgl;
    if (desc.isWebgl2) {
      webgl = new WebGL2Context(host, desc);
    } else {
      webgl = new WebGLContext(host, desc);
    }

    MOZ_ASSERT(!webgl->gl);
    std::vector<FailureReason> failReasons;
    if (!webgl->CreateAndInitGL(forceEnabled, &failReasons)) {
      nsCString text("WebGL creation failed: ");
      for (const auto& cur : failReasons) {
        // Don't try to accumulate using an empty key if |cur.key| is empty.
        if (cur.key.IsEmpty()) {
          glean::canvas::webgl_failure_id
              .Get("FEATURE_FAILURE_REASON_UNKNOWN"_ns)
              .Add(1);
        } else {
          glean::canvas::webgl_failure_id.Get(cur.key).Add(1);
        }

        const auto str = nsPrintfCString("\n* %s (%s)", cur.info.BeginReading(),
                                         cur.key.BeginReading());
        text.Append(str);
      }
      failureId = "FEATURE_FAILURE_REASON"_ns;
      return Err(text.BeginReading());
    }
    MOZ_ASSERT(webgl->gl);

    if (desc.options.failIfMajorPerformanceCaveat) {
      if (webgl->gl->IsWARP()) {
        failureId = "FEATURE_FAILURE_WEBGL_PERF_WARP"_ns;
        return Err(
            "failIfMajorPerformanceCaveat: Driver is not"
            " hardware-accelerated.");
      }

#ifdef XP_WIN
      if (webgl->gl->GetContextType() == gl::GLContextType::WGL &&
          !gl::sWGLLib.HasDXInterop2()) {
        failureId = "FEATURE_FAILURE_WEBGL_DXGL_INTEROP2"_ns;
        return Err("failIfMajorPerformanceCaveat: WGL without DXGLInterop2.");
      }
#endif
    }

    const FuncScope funcScope(*webgl, "getContext/restoreContext");

    MOZ_ASSERT(!webgl->mDefaultFB);
    if (!webgl->EnsureDefaultFB()) {
      MOZ_ASSERT(!webgl->mDefaultFB);
      MOZ_ASSERT(webgl->IsContextLost());
      failureId = "FEATURE_FAILURE_WEBGL_BACKBUFFER"_ns;
      return Err("Initializing WebGL backbuffer failed.");
    }

    return webgl;
  }();
  if (res.isOk()) {
    failureId = "SUCCESS"_ns;
  }
  glean::canvas::webgl_failure_id.Get(failureId).Add(1);

  if (!res.isOk()) {
    out->error = res.unwrapErr();
    return nullptr;
  }
  const auto webgl = res.unwrap();

  // Update our internal stuff:
  webgl->FinishInit();

  reporter.SetSuccessful();
  if (gl::GLContext::ShouldSpew()) {
    printf_stderr("--- WebGL context created: %p\n", webgl.get());
  }

  // -

  const auto UploadableSdTypes = [&]() {
    webgl::EnumMask<layers::SurfaceDescriptor::Type> types;
    types[layers::SurfaceDescriptor::TSurfaceDescriptorBuffer] = true;
    // Only support canvas surface interchange if using AC2D. This guarantees
    // that WebGL and AC2D commands are sequenced and processed on the same
    // thread, so that there is no mal-ordering between AC2D and WebGL
    // processing. We can flush out AC2D commands to produce a surface in time
    // for WebGL to use without requiring any blocking to occur.
    types[layers::SurfaceDescriptor::TSurfaceDescriptorCanvasSurface] =
        gfx::gfxVars::UseAcceleratedCanvas2D();
    // This is conditional on not using the Compositor thread because we may
    // need to synchronize with the RDD process over the PVideoBridge protocol
    // to wait for the texture to be available in the compositor process. We
    // cannot block on the Compositor thread, so in that configuration, we would
    // prefer to do the readback from the RDD which is guaranteed to work, and
    // only block the owning thread for WebGL.
    const bool offCompositorThread = gfx::gfxVars::UseCanvasRenderThread() ||
                                     !gfx::gfxVars::SupportsThreadsafeGL();
    types[layers::SurfaceDescriptor::TSurfaceDescriptorGPUVideo] =
        offCompositorThread;
    // Similarly to the PVideoBridge protocol, we may need to synchronize with
    // the content process over the PCompositorManager protocol to wait for the
    // shared surface to be available in the compositor process, and we cannot
    // block on the Compositor thread.
    types[layers::SurfaceDescriptor::TSurfaceDescriptorExternalImage] =
        offCompositorThread;
    if (webgl->gl->IsANGLE()) {
      types[layers::SurfaceDescriptor::TSurfaceDescriptorD3D10] = true;
      types[layers::SurfaceDescriptor::TSurfaceDescriptorDXGIYCbCr] = true;
    }
    if (kIsMacOS) {
      types[layers::SurfaceDescriptor::TSurfaceDescriptorMacIOSurface] = true;
    }
    if (kIsAndroid) {
      types[layers::SurfaceDescriptor::TSurfaceTextureDescriptor] = true;
    }
    if (kIsLinux) {
      types[layers::SurfaceDescriptor::TSurfaceDescriptorDMABuf] = true;
    }
    return types;
  };

  // -

  constexpr GLenum SHADER_TYPES[] = {
      LOCAL_GL_VERTEX_SHADER,
      LOCAL_GL_FRAGMENT_SHADER,
  };
  constexpr GLenum PRECISIONS[] = {
      LOCAL_GL_LOW_FLOAT,
      LOCAL_GL_MEDIUM_FLOAT,
      LOCAL_GL_HIGH_FLOAT,
      LOCAL_GL_LOW_INT,
      LOCAL_GL_MEDIUM_INT,
      LOCAL_GL_HIGH_INT,
  };
  for (const auto& shaderType : SHADER_TYPES) {
    for (const auto& precisionType : PRECISIONS) {
      auto spf = webgl::ShaderPrecisionFormat{};

      GLint range[2] = {};
      GLint precision = 0;
      webgl->gl->fGetShaderPrecisionFormat(shaderType, precisionType, range,
                                           &precision);
      spf.rangeMin = LazyAssertedCast(range[0]);
      spf.rangeMax = LazyAssertedCast(range[1]);
      spf.precision = LazyAssertedCast(precision);

      out->shaderPrecisions->insert({{shaderType, precisionType}, spf});
    }
  }

  if (webgl->mDisableFragHighP) {
    out->shaderPrecisions->at({LOCAL_GL_FRAGMENT_SHADER, LOCAL_GL_HIGH_FLOAT}) = {};
    out->shaderPrecisions->at({LOCAL_GL_FRAGMENT_SHADER, LOCAL_GL_HIGH_INT}) = {};
  }

  // -

  out->options = webgl->mOptions;
  out->limits = *webgl->mLimits;
  out->uploadableSdTypes = UploadableSdTypes();
  out->vendor = webgl->gl->Vendor();
  out->optionalRenderableFormatBits = webgl->mOptionalRenderableFormatBits;

  return webgl;
}

void WebGLContext::FinishInit() {
  mOptions.antialias &= bool(mDefaultFB->mSamples);

  if (!mOptions.alpha) {
    // We always have alpha.
    mNeedsFakeNoAlpha = true;
  }

  if (mOptions.depth || mOptions.stencil) {
    // We always have depth+stencil if we have either.
    if (!mOptions.depth) {
      mNeedsFakeNoDepth = true;
    }
    if (!mOptions.stencil) {
      mNeedsFakeNoStencil = true;
    }
  }

  mResetLayer = true;
  mOptionsFrozen = true;

  //////
  // Initial setup.

  gl->mImplicitMakeCurrent = true;
  gl->mElideDuplicateBindFramebuffers = true;

  const auto& size = mDefaultFB->mSize;

  mViewportX = mViewportY = 0;
  mViewportWidth = size.width;
  mViewportHeight = size.height;
  gl->fViewport(mViewportX, mViewportY, mViewportWidth, mViewportHeight);

  mScissorRect = {0, 0, size.width, size.height};
  mScissorRect.Apply(*gl);

  {
    const auto& isEnabledMap = webgl::MakeIsEnabledMap(IsWebGL2());
    for (const auto& pair : isEnabledMap) {
      mIsEnabledMapKeys.insert(pair.first);
    }
  }

  //////
  // Check everything

  AssertCachedBindings();
  AssertCachedGlobalState();

  mShouldPresent = true;

  //////
  // mIsRgb8Renderable

  {
    const auto tex = gl::ScopedTexture(gl);
    const auto fb = gl::ScopedFramebuffer(gl);
    gl->fBindTexture(LOCAL_GL_TEXTURE_2D, tex);
    gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, fb);
    gl->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER, LOCAL_GL_COLOR_ATTACHMENT0,
                              LOCAL_GL_TEXTURE_2D, tex, 0);

    const auto IsRenderable = [&](const GLint internalFormat,
                                  const GLenum unpackFormat) {
      gl->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, internalFormat, 1, 1, 0,
                      unpackFormat, LOCAL_GL_UNSIGNED_BYTE, nullptr);
      const auto status = gl->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER);
      return (status == LOCAL_GL_FRAMEBUFFER_COMPLETE);
    };

    if (IsRenderable(LOCAL_GL_RGB, LOCAL_GL_RGB)) {
      mOptionalRenderableFormatBits |=
          webgl::OptionalRenderableFormatBits::RGB8;
    }
    if (gl->IsSupported(gl::GLFeature::sRGB)) {
      struct {
        GLint internal;
        GLenum unpack;
      } formats = {LOCAL_GL_SRGB8, LOCAL_GL_RGB};
      const bool isEs2 = (gl->IsGLES() && gl->Version() < 300);
      if (isEs2) {
        formats = {LOCAL_GL_SRGB, LOCAL_GL_SRGB};
      }
      if (IsRenderable(formats.internal, formats.unpack)) {
        mOptionalRenderableFormatBits |=
            webgl::OptionalRenderableFormatBits::SRGB8;
      }
    }
  }

  //////

  gl->ResetSyncCallCount("WebGLContext Initialization");
  LoseLruContextIfLimitExceeded();
}

void WebGLContext::SetCompositableHost(
    RefPtr<layers::CompositableHost>& aCompositableHost) {
  mCompositableHost = aCompositableHost;
}

void WebGLContext::BumpLruLocked() {
  if (!mIsContextLost && !mPendingContextLoss) {
    mLruPosition.AssignLocked(*this);
  } else {
    MOZ_ASSERT(!mLruPosition.IsInsertedLocked());
  }
}

void WebGLContext::BumpLru() {
  StaticMutexAutoLock lock(sLruMutex);
  BumpLruLocked();
}

void WebGLContext::LoseLruContextIfLimitExceeded() {
  StaticMutexAutoLock lock(sLruMutex);

  const auto maxContexts = std::max(1u, StaticPrefs::webgl_max_contexts());
  const auto maxContextsPerPrincipal =
      std::max(1u, StaticPrefs::webgl_max_contexts_per_principal());

  // it's important to update the index on a new context before losing old
  // contexts, otherwise new unused contexts would all have index 0 and we
  // couldn't distinguish older ones when choosing which one to lose first.
  BumpLruLocked();

  {
    size_t forPrincipal = 0;
    for (const auto& context : sLru) {
      if (context->mPrincipalKey == mPrincipalKey) {
        forPrincipal += 1;
      }
    }

    while (forPrincipal > maxContextsPerPrincipal) {
      const auto text = nsPrintfCString(
          "Exceeded %u live WebGL contexts for this principal, losing the "
          "least recently used one.",
          maxContextsPerPrincipal);
      JsWarning(ToString(text));

      for (const auto& context : sLru) {
        if (context->mPrincipalKey == mPrincipalKey) {
          MOZ_ASSERT(context != this);
          context->LoseContextLruLocked(webgl::ContextLossReason::None);
          forPrincipal -= 1;
          break;
        }
      }
    }
  }

  auto total = sLru.size();
  while (total > maxContexts) {
    const auto text = nsPrintfCString(
        "Exceeded %u live WebGL contexts, losing the least "
        "recently used one.",
        maxContexts);
    JsWarning(ToString(text));

    const auto& context = sLru.front();
    MOZ_ASSERT(context != this);
    context->LoseContextLruLocked(webgl::ContextLossReason::None);
    total -= 1;
  }
}

// -

namespace webgl {

ScopedPrepForResourceClear::ScopedPrepForResourceClear(
    const WebGLContext& webgl_)
    : webgl(webgl_) {
  const auto& gl = webgl.gl;

  if (webgl.mScissorTestEnabled) {
    gl->fDisable(LOCAL_GL_SCISSOR_TEST);
  }
  if (webgl.mRasterizerDiscardEnabled) {
    gl->fDisable(LOCAL_GL_RASTERIZER_DISCARD);
  }

  // "The clear operation always uses the front stencil write mask
  //  when clearing the stencil buffer."
  webgl.DoColorMask(Some(0), 0b1111);
  gl->fDepthMask(true);
  gl->fStencilMaskSeparate(LOCAL_GL_FRONT, 0xffffffff);

  gl->fClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  gl->fClearDepth(1.0f);  // Depth formats are always cleared to 1.0f, not 0.0f.
  gl->fClearStencil(0);
}

ScopedPrepForResourceClear::~ScopedPrepForResourceClear() {
  const auto& gl = webgl.gl;

  if (webgl.mScissorTestEnabled) {
    gl->fEnable(LOCAL_GL_SCISSOR_TEST);
  }
  if (webgl.mRasterizerDiscardEnabled) {
    gl->fEnable(LOCAL_GL_RASTERIZER_DISCARD);
  }

  webgl.DoColorMask(Some(0), webgl.mColorWriteMask0);
  gl->fDepthMask(webgl.mDepthWriteMask);
  gl->fStencilMaskSeparate(LOCAL_GL_FRONT, webgl.mStencilWriteMaskFront);

  gl->fClearColor(webgl.mColorClearValue[0], webgl.mColorClearValue[1],
                  webgl.mColorClearValue[2], webgl.mColorClearValue[3]);
  gl->fClearDepth(webgl.mDepthClearValue);
  gl->fClearStencil(webgl.mStencilClearValue);
}

}  // namespace webgl

// -

void WebGLContext::OnEndOfFrame() {
  if (StaticPrefs::webgl_perf_spew_frame_allocs()) {
    GeneratePerfWarning("[webgl.perf.spew-frame-allocs] %" PRIu64
                        " data allocations this frame.",
                        mDataAllocGLCallCount);
  }
  mDataAllocGLCallCount = 0;
  gl->ResetSyncCallCount("WebGLContext PresentScreenBuffer");

  mDrawCallsSinceLastFlush = 0;

  PollPendingSyncs();

  BumpLru();
}

void WebGLContext::BlitBackbufferToCurDriverFB(
    WebGLFramebuffer* const srcAsWebglFb,
    const gl::MozFramebuffer* const srcAsMozFb, bool srcIsBGRA) const {
  // BlitFramebuffer ignores ColorMask().

  if (mScissorTestEnabled) {
    gl->fDisable(LOCAL_GL_SCISSOR_TEST);
  }
  const auto cleanup = MakeScopeExit([&]() {
    if (mScissorTestEnabled) {
      gl->fEnable(LOCAL_GL_SCISSOR_TEST);
    }
  });

  // If a MozFramebuffer is supplied, ensure that a WebGLFramebuffer is not
  // used since it might not have completeness info, while the MozFramebuffer
  // can still supply the needed information.
  MOZ_ASSERT(!(srcAsMozFb && srcAsWebglFb));
  const auto* mozFb = srcAsMozFb ? srcAsMozFb : mDefaultFB.get();
  GLuint fbo = 0;
  gfx::IntSize size;
  if (srcAsWebglFb) {
    fbo = srcAsWebglFb->mGLName;
    const auto* info = srcAsWebglFb->GetCompletenessInfo();
    MOZ_ASSERT(info);
    size = gfx::IntSize(info->width, info->height);
  } else {
    fbo = mozFb->mFB;
    size = mozFb->mSize;
  }

  // If no format conversion is necessary, then attempt to directly blit
  // between framebuffers. Otherwise, if we need to convert to RGBA from
  // the source format, then we will need to use the texture blit path
  // below.
  if (!srcIsBGRA) {
    if (gl->IsSupported(gl::GLFeature::framebuffer_blit)) {
      gl->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, fbo);
      gl->fBlitFramebuffer(0, 0, size.width, size.height, 0, 0, size.width,
                           size.height, LOCAL_GL_COLOR_BUFFER_BIT,
                           LOCAL_GL_NEAREST);
      return;
    }
    if (mDefaultFB->mSamples &&
        gl->IsExtensionSupported(
            gl::GLContext::APPLE_framebuffer_multisample)) {
      gl->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, fbo);
      gl->fResolveMultisampleFramebufferAPPLE();
      return;
    }
  }

  GLuint colorTex = 0;
  if (srcAsWebglFb) {
    const auto& attach = srcAsWebglFb->ColorAttachment0();
    MOZ_ASSERT(attach.Texture());
    colorTex = attach.Texture()->mGLName;
  } else {
    colorTex = mozFb->ColorTex();
  }

  // DrawBlit handles ColorMask itself.
  gl->BlitHelper()->DrawBlitTextureToFramebuffer(
      colorTex, size, size, LOCAL_GL_TEXTURE_2D, srcIsBGRA);
}

// -

template <typename T, typename... Args>
constexpr auto MakeArray(Args... args) -> std::array<T, sizeof...(Args)> {
  return {{static_cast<T>(args)...}};
}

inline gfx::ColorSpace2 ToColorSpace2ForOutput(
    const std::optional<dom::PredefinedColorSpace> chosenCspace) {
  const auto cmsMode = GfxColorManagementMode();
  switch (cmsMode) {
    case CMSMode::Off:
      return gfx::ColorSpace2::Display;
    case CMSMode::TaggedOnly:
      if (!chosenCspace) {
        return gfx::ColorSpace2::Display;
      }
      break;
    case CMSMode::All:
      if (!chosenCspace) {
        return gfx::ColorSpace2::SRGB;
      }
      break;
  }
  return gfx::ToColorSpace2(*chosenCspace);
}

// -

template <class T>
GLuint GLNameOrZero(const T& t) {
  if (t) return t->mGLName;
  return 0;
}

// For an overview of how WebGL compositing works, see:
// https://wiki.mozilla.org/Platform/GFX/WebGL/Compositing
bool WebGLContext::PresentInto(gl::SwapChain& swapChain) {
  OnEndOfFrame();

  if (!ValidateAndInitFB(nullptr)) return false;

  const auto size = mDefaultFB->mSize;

  const auto error = [&]() -> std::optional<std::string> {
    const auto canvasCspace = ToColorSpace2ForOutput(mDrawingBufferColorSpace);
    auto presenter = swapChain.Acquire(size, canvasCspace);
    if (!presenter) {
      return "Swap chain surface creation failed.";
    }
    const auto outputCspace = presenter->BackBuffer()->mDesc.colorSpace;
    const auto destFb = presenter->Fb();

    // -

    bool colorManage = (canvasCspace != gfx::ColorSpace2::Display);
    if (canvasCspace == outputCspace) {
      colorManage = false;
    }
    if (!gl->IsSupported(gl::GLFeature::texture_3D)) {
      NS_WARNING("Missing GLFeature::texture_3D => colorManage = false.");
      colorManage = false;
    }

    auto colorLut = std::shared_ptr<gl::Texture>{};
    if (colorManage) {
      MOZ_ASSERT(canvasCspace != gfx::ColorSpace2::Display);
      colorLut = gl->BlitHelper()->GetColorLutTex(gl::GLBlitHelper::ColorLutKey{
          .src = canvasCspace, .dst = outputCspace});
      if (!colorLut) {
        NS_WARNING("GetColorLutTex() -> nullptr => colorManage = false.");
        colorManage = false;
      }
    }

    if (!colorManage) {
      gl->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, destFb);
      BlitBackbufferToCurDriverFB();
      return {};
    }

    // -

    const auto canvasFb = GetDefaultFBForRead({.endOfFrame = true});
    if (!canvasFb) {
      return "[WebGLContext::PresentInto] BindDefaultFBForRead failed.";
    }

    const auto& blitter = gl->BlitHelper()->GetDrawBlitProg({
        .fragHeader = gl::kFragHeader_Tex2D,
        .fragParts = {gl::kFragSample_OnePlane, gl::kFragConvert_ColorLut3d},
    });

    constexpr uint8_t texUnit_src = 0;
    constexpr uint8_t texUnit_lut = 1;
    gl->BindSamplerTexture(texUnit_src, SamplerLinear(), LOCAL_GL_TEXTURE_2D,
                           canvasFb->ColorTex());
    gl->BindSamplerTexture(texUnit_lut, SamplerLinear(), LOCAL_GL_TEXTURE_3D,
                           colorLut->name);
    const auto texCleanup = MakeScopeExit([&]() {
      gl->BindSamplerTexture(
          texUnit_src, GLNameOrZero(mBoundSamplers[texUnit_src]),
          LOCAL_GL_TEXTURE_2D, GLNameOrZero(mBound2DTextures[texUnit_src]));
      gl->BindSamplerTexture(
          texUnit_lut, GLNameOrZero(mBoundSamplers[texUnit_lut]),
          LOCAL_GL_TEXTURE_3D, GLNameOrZero(mBound3DTextures[texUnit_lut]));
      gl->fActiveTexture(LOCAL_GL_TEXTURE0 + mActiveTexture);
    });

    gl->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, destFb);

    gl->fUseProgram(blitter.mProg);
    const auto cleanupProg = MakeScopeExit(
        [&]() { gl->fUseProgram(GLNameOrZero(mCurrentProgram)); });

    gl->fUniform1i(blitter.mLoc_uColorLut, texUnit_lut);

    blitter.Draw({
        .texMatrix0 = gl::Mat3::I(),
        .yFlip = false,
        .destSize = size,
        .destRect = {},
    });

    gl->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, canvasFb->mFB);
    return {};
  }();
  if (error) {
    GenerateWarning("%s", error->c_str());
    LoseContext();
    return false;
  }

  if (!mOptions.preserveDrawingBuffer) {
    gl->InvalidateFramebuffer(LOCAL_GL_READ_FRAMEBUFFER);
    mDefaultFB_IsInvalid = true;
  }

  return true;
}

bool WebGLContext::PresentIntoXR(gl::SwapChain& swapChain,
                                 const gl::MozFramebuffer& fb) {
  OnEndOfFrame();

  const auto colorSpace = ToColorSpace2ForOutput(mDrawingBufferColorSpace);
  auto presenter = swapChain.Acquire(fb.mSize, colorSpace);
  if (!presenter) {
    GenerateWarning("Swap chain surface creation failed.");
    LoseContext();
    return false;
  }

  const auto destFb = presenter->Fb();
  gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, destFb);

  BlitBackbufferToCurDriverFB(nullptr, &fb);

  // https://immersive-web.github.io/webxr/#opaque-framebuffer
  // Opaque framebuffers will always be cleared regardless of the
  // associated WebGL context’s preserveDrawingBuffer value.
  if (gl->IsSupported(gl::GLFeature::invalidate_framebuffer)) {
    gl->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, fb.mFB);
    constexpr auto attachments = MakeArray<GLenum>(
        LOCAL_GL_COLOR_ATTACHMENT0, LOCAL_GL_DEPTH_STENCIL_ATTACHMENT);
    gl->fInvalidateFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, attachments.size(),
                               attachments.data());
  }

  return true;
}

// Initialize a swap chain's surface factory given the desired surface type.
void InitSwapChain(gl::GLContext& gl, gl::SwapChain& swapChain,
                   const layers::TextureType consumerType, bool useAsync) {
  if (!swapChain.mFactory) {
    auto typedFactory = gl::SurfaceFactory::Create(&gl, consumerType);
    if (typedFactory) {
      swapChain.mFactory = std::move(typedFactory);
    }
  }
  if (!swapChain.mFactory) {
    NS_WARNING("Failed to make an ideal SurfaceFactory.");
    swapChain.mFactory = MakeUnique<gl::SurfaceFactory_Basic>(gl);
  }
  MOZ_ASSERT(swapChain.mFactory);
  if (useAsync) {
    // RemoteTextureMap will handle recycling any surfaces, so don't rely on the
    // SwapChain's internal pooling.
    swapChain.DisablePool();
  }
}

void WebGLContext::Present(WebGLFramebuffer* const xrFb,
                           const layers::TextureType consumerType,
                           const bool webvr,
                           const webgl::SwapChainOptions& options) {
  const FuncScope funcScope(*this, "<Present>");
  if (IsContextLost()) {
    EnsureContextLostRemoteTextureOwner(options);
    return;
  }

  auto swapChain = GetSwapChain(xrFb, webvr);
  const gl::MozFramebuffer* maybeFB = nullptr;
  if (xrFb) {
    maybeFB = xrFb->mOpaque.get();
  } else {
    mResolvedDefaultFB = nullptr;
  }

  bool useAsync = options.remoteTextureOwnerId.IsValid() &&
                  options.remoteTextureId.IsValid();

  InitSwapChain(*gl, *swapChain, consumerType, useAsync);

  bool valid =
      maybeFB ? PresentIntoXR(*swapChain, *maybeFB) : PresentInto(*swapChain);
  if (!valid) {
    EnsureContextLostRemoteTextureOwner(options);
    return;
  }

  if (useAsync) {
    PushRemoteTexture(nullptr, *swapChain, swapChain->FrontBuffer(), options);
  }
}

void WebGLContext::WaitForTxn(layers::RemoteTextureOwnerId ownerId,
                              layers::RemoteTextureTxnType txnType,
                              layers::RemoteTextureTxnId txnId) {
  if (!ownerId.IsValid() || !txnType || !txnId) {
    return;
  }
  if (mRemoteTextureOwner && mRemoteTextureOwner->IsRegistered(ownerId)) {
    mRemoteTextureOwner->WaitForTxn(ownerId, txnType, txnId);
  }
}

bool WebGLContext::CopyToSwapChain(
    WebGLFramebuffer* const srcFb, const layers::TextureType consumerType,
    const webgl::SwapChainOptions& options,
    layers::RemoteTextureOwnerClient* ownerClient) {
  const FuncScope funcScope(*this, "<CopyToSwapChain>");
  if (IsContextLost()) {
    return false;
  }

  OnEndOfFrame();

  if (!srcFb) {
    return false;
  }
  const auto* info = srcFb->GetCompletenessInfo();
  if (!info) {
    return false;
  }
  gfx::IntSize size(info->width, info->height);

  bool useAsync = options.remoteTextureOwnerId.IsValid() &&
                  options.remoteTextureId.IsValid();

  InitSwapChain(*gl, srcFb->mSwapChain, consumerType, useAsync);

  // If we're using async present and if there is no way to serialize surfaces,
  // then a readback is required to do the copy. In this case, there's no reason
  // to copy into a separate shared surface for the front buffer. Just directly
  // read back the WebGL framebuffer into and push it as a remote texture.
  if (useAsync && srcFb->mSwapChain.mFactory->GetConsumerType() ==
                      layers::TextureType::Unknown) {
    return PushRemoteTexture(srcFb, srcFb->mSwapChain, nullptr, options,
                             ownerClient);
  }

  {
    // TODO: ColorSpace will need to be part of SwapChainOptions for DTWebgl.
    const auto colorSpace = ToColorSpace2ForOutput(mDrawingBufferColorSpace);
    auto presenter = srcFb->mSwapChain.Acquire(size, colorSpace);
    if (!presenter) {
      GenerateWarning("Swap chain surface creation failed.");
      LoseContext();
      return false;
    }

    const ScopedFBRebinder saveFB(this);

    const auto destFb = presenter->Fb();
    gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, destFb);

    BlitBackbufferToCurDriverFB(srcFb, nullptr, options.bgra);
  }

  if (useAsync) {
    return PushRemoteTexture(srcFb, srcFb->mSwapChain,
                             srcFb->mSwapChain.FrontBuffer(), options,
                             ownerClient);
  }
  return true;
}

bool WebGLContext::PushRemoteTexture(
    WebGLFramebuffer* fb, gl::SwapChain& swapChain,
    std::shared_ptr<gl::SharedSurface> surf,
    const webgl::SwapChainOptions& options,
    layers::RemoteTextureOwnerClient* ownerClient) {
  const auto onFailure = [&]() -> bool {
    GenerateWarning("Remote texture creation failed.");
    LoseContext();
    if (ownerClient && ownerClient == mRemoteTextureOwner) {
      ownerClient->PushDummyTexture(options.remoteTextureId,
                                    options.remoteTextureOwnerId);
    }
    return false;
  };

  if (!ownerClient) {
    if (!mRemoteTextureOwner) {
      // Ensure we have a remote texture owner client for WebGLParent.
      const auto* outOfProcess =
          mHost ? mHost->mOwnerData.outOfProcess : nullptr;
      if (!outOfProcess) {
        return onFailure();
      }
      auto pid = outOfProcess->OtherPid();
      mRemoteTextureOwner = MakeRefPtr<layers::RemoteTextureOwnerClient>(pid);
    }
    ownerClient = mRemoteTextureOwner;
  }

  layers::RemoteTextureOwnerId ownerId = options.remoteTextureOwnerId;
  layers::RemoteTextureId textureId = options.remoteTextureId;

  if (!ownerClient->IsRegistered(ownerId)) {
    // Register a texture owner to represent the swap chain.
    RefPtr<layers::RemoteTextureOwnerClient> textureOwner = ownerClient;
    auto destroyedCallback = [textureOwner, ownerId]() {
      textureOwner->UnregisterTextureOwner(ownerId);
    };

    swapChain.SetDestroyedCallback(destroyedCallback);
    ownerClient->RegisterTextureOwner(ownerId,
                                      /* aSharedRecycling */ !!fb);
  }

  MOZ_ASSERT(fb || surf);
  gfx::IntSize size;
  if (surf) {
    size = surf->mDesc.size;
  } else {
    const auto* info = fb->GetCompletenessInfo();
    MOZ_ASSERT(info);
    size = gfx::IntSize(info->width, info->height);
  }

  const auto surfaceFormat = mOptions.alpha ? gfx::SurfaceFormat::B8G8R8A8
                                            : gfx::SurfaceFormat::B8G8R8X8;
  Maybe<layers::SurfaceDescriptor> desc;
  if (surf) {
    desc = surf->ToSurfaceDescriptor();
  }
  if (!desc) {
    if (surf && surf->mDesc.type != gl::SharedSurfaceType::Basic) {
      return onFailure();
    }
    // If we can't serialize to a surface descriptor, then we need to create
    // a buffer to read back into that will become the remote texture.
    auto data = ownerClient->CreateOrRecycleBufferTextureData(
        size, surfaceFormat, ownerId);
    if (!data) {
      gfxCriticalNoteOnce << "Failed to allocate BufferTextureData";
      return onFailure();
    }

    layers::MappedTextureData mappedData;
    if (!data->BorrowMappedData(mappedData)) {
      return onFailure();
    }

    Range<uint8_t> range = {mappedData.data,
                            data->AsBufferTextureData()->GetBufferSize()};

    // If we have a surface representing the front buffer, then try to snapshot
    // that. Otherwise, when there is no surface, we read back directly from the
    // WebGL framebuffer.
    auto valid =
        surf ? FrontBufferSnapshotInto(surf, Some(range),
                                       Some(mappedData.stride))
             : SnapshotInto(fb->mGLName, size, range, Some(mappedData.stride));
    if (!valid) {
      return onFailure();
    }

    if (!options.bgra) {
      // If the buffer is already BGRA, we don't need to swizzle. However, if it
      // is RGBA, then a swizzle to BGRA is required.
      bool rv = gfx::SwizzleData(mappedData.data, mappedData.stride,
                                 gfx::SurfaceFormat::R8G8B8A8, mappedData.data,
                                 mappedData.stride,
                                 gfx::SurfaceFormat::B8G8R8A8, mappedData.size);
      MOZ_RELEASE_ASSERT(rv, "SwizzleData failed!");
    }

    ownerClient->PushTexture(textureId, ownerId, std::move(data));
    return true;
  }

  // SharedSurfaces of SurfaceDescriptorD3D10 and SurfaceDescriptorMacIOSurface
  // need to be kept alive. They will be recycled by
  // RemoteTextureOwnerClient::GetRecycledSharedSurface() when their usages are
  // ended.
  std::shared_ptr<gl::SharedSurface> keepAlive;
  switch (desc->type()) {
    case layers::SurfaceDescriptor::TSurfaceDescriptorD3D10:
    case layers::SurfaceDescriptor::TSurfaceDescriptorMacIOSurface:
    case layers::SurfaceDescriptor::TSurfaceTextureDescriptor:
    case layers::SurfaceDescriptor::TSurfaceDescriptorAndroidHardwareBuffer:
    case layers::SurfaceDescriptor::TEGLImageDescriptor:
    case layers::SurfaceDescriptor::TSurfaceDescriptorDMABuf:
      keepAlive = surf;
      break;
    default:
      break;
  }

  ownerClient->PushTexture(textureId, ownerId, keepAlive, size, surfaceFormat,
                           *desc);

  // Look for a recycled surface that matches the swap chain.
  while (auto recycledSurface = ownerClient->GetRecycledSharedSurface(
             size, surfaceFormat, desc->type(), ownerId)) {
    if (swapChain.StoreRecycledSurface(recycledSurface)) {
      break;
    }
  }
  return true;
}

void WebGLContext::EnsureContextLostRemoteTextureOwner(
    const webgl::SwapChainOptions& options) {
  if (!options.remoteTextureOwnerId.IsValid()) {
    return;
  }

  if (!mRemoteTextureOwner) {
    // Ensure we have a remote texture owner client for WebGLParent.
    const auto* outOfProcess = mHost ? mHost->mOwnerData.outOfProcess : nullptr;
    if (!outOfProcess) {
      return;
    }
    auto pid = outOfProcess->OtherPid();
    mRemoteTextureOwner = MakeRefPtr<layers::RemoteTextureOwnerClient>(pid);
  }

  layers::RemoteTextureOwnerId ownerId = options.remoteTextureOwnerId;

  if (!mRemoteTextureOwner->IsRegistered(ownerId)) {
    mRemoteTextureOwner->RegisterTextureOwner(ownerId);
  }
  mRemoteTextureOwner->NotifyContextLost();
}

void WebGLContext::EndOfFrame() {
  const FuncScope funcScope(*this, "<EndOfFrame>");
  if (IsContextLost()) return;

  OnEndOfFrame();
}

gl::SwapChain* WebGLContext::GetSwapChain(WebGLFramebuffer* const xrFb,
                                          const bool webvr) {
  auto swapChain = webvr ? &mWebVRSwapChain : &mSwapChain;
  if (xrFb) {
    swapChain = &xrFb->mSwapChain;
  }
  return swapChain;
}

Maybe<layers::SurfaceDescriptor> WebGLContext::GetFrontBuffer(
    WebGLFramebuffer* const xrFb, const bool webvr) {
  auto* swapChain = GetSwapChain(xrFb, webvr);
  if (!swapChain) return {};
  const auto& front = swapChain->FrontBuffer();
  if (!front) return {};

  return front->ToSurfaceDescriptor();
}

Maybe<uvec2> WebGLContext::FrontBufferSnapshotInto(
    const Maybe<Range<uint8_t>> maybeDest, const Maybe<size_t> destStride) {
  const auto& front = mSwapChain.FrontBuffer();
  if (!front) return {};
  return FrontBufferSnapshotInto(front, maybeDest, destStride);
}

Maybe<uvec2> WebGLContext::FrontBufferSnapshotInto(
    const std::shared_ptr<gl::SharedSurface>& front,
    const Maybe<Range<uint8_t>> maybeDest, const Maybe<size_t> destStride) {
  const auto& size = front->mDesc.size;
  if (!maybeDest) return Some(*uvec2::FromSize(size));

  // -

  front->WaitForBufferOwnership();
  front->LockProd();
  front->ProducerReadAcquire();
  auto reset = MakeScopeExit([&] {
    front->ProducerReadRelease();
    front->UnlockProd();
  });

  // -

  return SnapshotInto(front->mFb ? front->mFb->mFB : 0, size, *maybeDest,
                      destStride);
}

Maybe<uvec2> WebGLContext::SnapshotInto(GLuint srcFb, const gfx::IntSize& size,
                                        const Range<uint8_t>& dest,
                                        const Maybe<size_t> destStride) {
  const auto minStride = CheckedInt<size_t>(size.width) * 4;
  if (!minStride.isValid()) {
    gfxCriticalError() << "SnapshotInto: invalid stride, width:" << size.width;
    return {};
  }
  size_t stride = destStride.valueOr(minStride.value());
  if (stride < minStride.value() || (stride % 4) != 0) {
    gfxCriticalError() << "SnapshotInto: invalid stride, width:" << size.width
                       << ", stride:" << stride;
    return {};
  }

  gl->fPixelStorei(LOCAL_GL_PACK_ALIGNMENT, 1);
  if (IsWebGL2()) {
    gl->fPixelStorei(LOCAL_GL_PACK_ROW_LENGTH,
                     stride > minStride.value() ? stride / 4 : 0);
    gl->fPixelStorei(LOCAL_GL_PACK_SKIP_PIXELS, 0);
    gl->fPixelStorei(LOCAL_GL_PACK_SKIP_ROWS, 0);
  }

  // -

  const auto readFbWas = mBoundReadFramebuffer;
  const auto pboWas = mBoundPixelPackBuffer;

  GLenum fbTarget = LOCAL_GL_READ_FRAMEBUFFER;
  if (!IsWebGL2()) {
    fbTarget = LOCAL_GL_FRAMEBUFFER;
  }
  auto reset2 = MakeScopeExit([&] {
    DoBindFB(readFbWas, fbTarget);
    if (pboWas) {
      BindBuffer(LOCAL_GL_PIXEL_PACK_BUFFER, pboWas);
    }
  });

  gl->fBindFramebuffer(fbTarget, srcFb);
  if (pboWas) {
    BindBuffer(LOCAL_GL_PIXEL_PACK_BUFFER, nullptr);
  }

  // -

  const auto srcByteCount = CheckedInt<size_t>(stride) * size.height;
  if (!srcByteCount.isValid()) {
    gfxCriticalError() << "SnapshotInto: invalid srcByteCount, width:"
                       << size.width << ", height:" << size.height;
    return {};
  }
  const auto dstByteCount = dest.length();
  if (srcByteCount.value() > dstByteCount) {
    gfxCriticalError() << "SnapshotInto: srcByteCount:" << srcByteCount.value()
                       << " > dstByteCount:" << dstByteCount;
    return {};
  }
  uint8_t* dstPtr = dest.begin().get();
  gl->fReadPixels(0, 0, size.width, size.height, LOCAL_GL_RGBA,
                  LOCAL_GL_UNSIGNED_BYTE, dstPtr);

  if (!IsWebGL2() && stride > minStride.value() && size.height > 1) {
    // WebGL 1 doesn't support PACK_ROW_LENGTH. Instead, we read the data tight
    // into the front of the buffer, and use memmove (since the source and dest
    // may overlap) starting from the back to move it to the correct stride
    // offsets. We don't move the first row as it is already in the right place.
    uint8_t* destRow = dstPtr + stride * (size.height - 1);
    const uint8_t* srcRow = dstPtr + minStride.value() * (size.height - 1);
    while (destRow > dstPtr) {
      memmove(destRow, srcRow, minStride.value());
      destRow -= stride;
      srcRow -= minStride.value();
    }
  }

  return Some(*uvec2::FromSize(size));
}

void WebGLContext::ClearVRSwapChain() { mWebVRSwapChain.ClearPool(); }

// ------------------------

RefPtr<gfx::DataSourceSurface> GetTempSurface(const gfx::IntSize& aSize,
                                              gfx::SurfaceFormat& aFormat) {
  uint32_t stride =
      gfx::GetAlignedStride<8>(aSize.width, BytesPerPixel(aFormat));
  return gfx::Factory::CreateDataSourceSurfaceWithStride(aSize, aFormat,
                                                         stride);
}

void WebGLContext::DummyReadFramebufferOperation() {
  if (!mBoundReadFramebuffer) return;  // Infallible.

  const auto status = mBoundReadFramebuffer->CheckFramebufferStatus();
  if (status != LOCAL_GL_FRAMEBUFFER_COMPLETE) {
    ErrorInvalidFramebufferOperation("Framebuffer must be complete.");
  }
}

layers::SharedSurfacesHolder* WebGLContext::GetSharedSurfacesHolder() const {
  const auto* outOfProcess = mHost ? mHost->mOwnerData.outOfProcess : nullptr;
  if (outOfProcess) {
    return outOfProcess->mSharedSurfacesHolder;
  }
  MOZ_ASSERT_UNREACHABLE("Unexpected use of SharedSurfacesHolder in process!");
  return nullptr;
}

dom::ContentParentId WebGLContext::GetContentId() const {
  const auto* outOfProcess = mHost ? mHost->mOwnerData.outOfProcess : nullptr;
  if (outOfProcess) {
    return outOfProcess->mContentId;
  }
  if (XRE_IsContentProcess()) {
    return dom::ContentChild::GetSingleton()->GetID();
  }
  return dom::ContentParentId();
}

bool WebGLContext::Has64BitTimestamps() const {
  // 'sync' provides glGetInteger64v either by supporting ARB_sync, GL3+, or
  // GLES3+.
  return gl->IsSupported(gl::GLFeature::sync);
}

static bool CheckContextLost(gl::GLContext* gl, bool* const out_isGuilty) {
  MOZ_ASSERT(gl);

  const auto resetStatus = gl->fGetGraphicsResetStatus();
  if (resetStatus == LOCAL_GL_NO_ERROR) {
    *out_isGuilty = false;
    return false;
  }

  // Assume guilty unless we find otherwise!
  bool isGuilty = true;
  switch (resetStatus) {
    case LOCAL_GL_INNOCENT_CONTEXT_RESET_ARB:
    case LOCAL_GL_PURGED_CONTEXT_RESET_NV:
      // Either nothing wrong, or not our fault.
      isGuilty = false;
      break;
    case LOCAL_GL_GUILTY_CONTEXT_RESET_ARB:
      NS_WARNING(
          "WebGL content on the page definitely caused the graphics"
          " card to reset.");
      break;
    case LOCAL_GL_UNKNOWN_CONTEXT_RESET_ARB:
      NS_WARNING(
          "WebGL content on the page might have caused the graphics"
          " card to reset");
      // If we can't tell, assume not-guilty.
      // Todo: Implement max number of "unknown" resets per document or time.
      isGuilty = false;
      break;
    default:
      gfxCriticalError() << "Unexpected glGetGraphicsResetStatus: "
                         << gfx::hexa(resetStatus);
      break;
  }

  if (isGuilty) {
    NS_WARNING(
        "WebGL context on this page is considered guilty, and will"
        " not be restored.");
  }

  *out_isGuilty = isGuilty;
  return true;
}

void WebGLContext::RunContextLossTimer() { mContextLossHandler.RunTimer(); }

// We use this timer for many things. Here are the things that it is activated
// for:
// 1) If a script is using the MOZ_WEBGL_lose_context extension.
// 2) If we are using EGL and _NOT ANGLE_, we query periodically to see if the
//    CONTEXT_LOST_WEBGL error has been triggered.
// 3) If we are using ANGLE, or anything that supports ARB_robustness, query the
//    GPU periodically to see if the reset status bit has been set.
// In all of these situations, we use this timer to send the script context lost
// and restored events asynchronously. For example, if it triggers a context
// loss, the webglcontextlost event will be sent to it the next time the
// robustness timer fires.
// Note that this timer mechanism is not used unless one of these 3 criteria are
// met.
// At a bare minimum, from context lost to context restores, it would take 3
// full timer iterations: detection, webglcontextlost, webglcontextrestored.
void WebGLContext::CheckForContextLoss() {
  bool isGuilty = true;
  const auto isContextLost = CheckContextLost(gl, &isGuilty);
  if (!isContextLost) return;

  mWebGLError = LOCAL_GL_CONTEXT_LOST;

  auto reason = webgl::ContextLossReason::None;
  if (isGuilty) {
    reason = webgl::ContextLossReason::Guilty;
  }
  LoseContext(reason);
}

void WebGLContext::HandlePendingContextLoss() {
  mIsContextLost = true;
  if (mHost) {
    mHost->OnContextLoss(mPendingContextLossReason);
  }
}

void WebGLContext::LoseContextLruLocked(const webgl::ContextLossReason reason) {
  printf_stderr("WebGL(%p)::LoseContext(%u)\n", this,
                static_cast<uint32_t>(reason));
  mLruPosition.ResetLocked();
  mPendingContextLossReason = reason;
  mPendingContextLoss = true;
}

void WebGLContext::LoseContext(const webgl::ContextLossReason reason) {
  StaticMutexAutoLock lock(sLruMutex);
  LoseContextLruLocked(reason);
  HandlePendingContextLoss();
  if (mRemoteTextureOwner) {
    mRemoteTextureOwner->NotifyContextLost();
  }
}

void WebGLContext::DidRefresh() {
  if (gl) {
    gl->FlushIfHeavyGLCallsSinceLastFlush();
  }
}

////////////////////////////////////////////////////////////////////////////////

uvec2 WebGLContext::DrawingBufferSize() {
  const FuncScope funcScope(*this, "width/height");
  if (IsContextLost()) return {};

  if (!EnsureDefaultFB()) return {};

  return *uvec2::FromSize(mDefaultFB->mSize);
}

bool WebGLContext::ValidateAndInitFB(const WebGLFramebuffer* const fb,
                                     const GLenum incompleteFbError) {
  if (fb) return fb->ValidateAndInitAttachments(incompleteFbError);

  if (!EnsureDefaultFB()) return false;

  if (mDefaultFB_IsInvalid) {
    // Clear it!
    gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mDefaultFB->mFB);
    const webgl::ScopedPrepForResourceClear scopedPrep(*this);
    if (!mOptions.alpha) {
      gl->fClearColor(0, 0, 0, 1);
    }
    const GLbitfield bits = LOCAL_GL_COLOR_BUFFER_BIT |
                            LOCAL_GL_DEPTH_BUFFER_BIT |
                            LOCAL_GL_STENCIL_BUFFER_BIT;
    gl->fClear(bits);

    mDefaultFB_IsInvalid = false;
  }
  return true;
}

void WebGLContext::DoBindFB(const WebGLFramebuffer* const fb,
                            const GLenum target) const {
  const GLenum driverFB = fb ? fb->mGLName : (mDefaultFB ? mDefaultFB->mFB : 0);
  gl->fBindFramebuffer(target, driverFB);
}

bool WebGLContext::BindCurFBForDraw() {
  const auto& fb = mBoundDrawFramebuffer;
  if (!ValidateAndInitFB(fb)) return false;

  DoBindFB(fb);
  return true;
}

bool WebGLContext::BindCurFBForColorRead(
    const webgl::FormatUsageInfo** const out_format, uint32_t* const out_width,
    uint32_t* const out_height, const GLenum incompleteFbError) {
  const auto& fb = mBoundReadFramebuffer;

  if (fb) {
    if (!ValidateAndInitFB(fb, incompleteFbError)) return false;
    if (!fb->ValidateForColorRead(out_format, out_width, out_height))
      return false;

    gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, fb->mGLName);
    return true;
  }

  if (!BindDefaultFBForRead()) return false;

  if (mDefaultFB_ReadBuffer == LOCAL_GL_NONE) {
    ErrorInvalidOperation(
        "Can't read from backbuffer when readBuffer mode is NONE.");
    return false;
  }

  auto effFormat = mOptions.alpha ? webgl::EffectiveFormat::RGBA8
                                  : webgl::EffectiveFormat::RGB8;

  *out_format = mFormatUsage->GetUsage(effFormat);
  MOZ_ASSERT(*out_format);

  *out_width = mDefaultFB->mSize.width;
  *out_height = mDefaultFB->mSize.height;
  return true;
}

const gl::MozFramebuffer* WebGLContext::GetDefaultFBForRead(
    const GetDefaultFBForReadDesc& desc) {
  if (!ValidateAndInitFB(nullptr)) return nullptr;

  if (!mDefaultFB->mSamples) {
    return mDefaultFB.get();
  }

  if (!mResolvedDefaultFB) {
    mResolvedDefaultFB =
        gl::MozFramebuffer::Create(gl, mDefaultFB->mSize, 0, false);
    if (!mResolvedDefaultFB) {
      gfxCriticalNote << FuncName() << ": Failed to create mResolvedDefaultFB.";
      return nullptr;
    }
  }

  gl->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, mResolvedDefaultFB->mFB);
  BlitBackbufferToCurDriverFB();

  if (desc.endOfFrame && !mOptions.preserveDrawingBuffer) {
    gl->InvalidateFramebuffer(LOCAL_GL_READ_FRAMEBUFFER);
  }

  return mResolvedDefaultFB.get();
}

bool WebGLContext::BindDefaultFBForRead() {
  const auto fb = GetDefaultFBForRead();
  if (!fb) return false;
  gl->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, fb->mFB);
  return true;
}

void WebGLContext::DoColorMask(Maybe<GLuint> i, const uint8_t bitmask) const {
  if (!IsExtensionEnabled(WebGLExtensionID::OES_draw_buffers_indexed)) {
    i = Nothing();
  }
  const auto bs = std::bitset<4>(bitmask);
  if (i) {
    gl->fColorMaski(*i, bs[0], bs[1], bs[2], bs[3]);
  } else {
    gl->fColorMask(bs[0], bs[1], bs[2], bs[3]);
  }
}

////////////////////////////////////////////////////////////////////////////////

ScopedDrawCallWrapper::ScopedDrawCallWrapper(WebGLContext& webgl)
    : mWebGL(webgl) {
  uint8_t driverColorMask0 = mWebGL.mColorWriteMask0;
  bool driverDepthTest = mWebGL.mDepthTestEnabled;
  bool driverStencilTest = mWebGL.mStencilTestEnabled;
  const auto& fb = mWebGL.mBoundDrawFramebuffer;
  if (!fb) {
    if (mWebGL.mDefaultFB_DrawBuffer0 == LOCAL_GL_NONE) {
      driverColorMask0 = 0;  // Is this well-optimized enough for depth-first
                             // rendering?
    } else {
      driverColorMask0 &= ~(uint8_t(mWebGL.mNeedsFakeNoAlpha) << 3);
    }
    driverDepthTest &= !mWebGL.mNeedsFakeNoDepth;
    driverStencilTest &= !mWebGL.mNeedsFakeNoStencil;
  }

  const auto& gl = mWebGL.gl;
  mWebGL.DoColorMask(Some(0), driverColorMask0);
  if (mWebGL.mDriverDepthTest != driverDepthTest) {
    // "When disabled, the depth comparison and subsequent possible updates to
    // the
    //  depth buffer value are bypassed and the fragment is passed to the next
    //  operation." [GLES 3.0.5, p177]
    mWebGL.mDriverDepthTest = driverDepthTest;
    gl->SetEnabled(LOCAL_GL_DEPTH_TEST, mWebGL.mDriverDepthTest);
  }
  if (mWebGL.mDriverStencilTest != driverStencilTest) {
    // "When disabled, the stencil test and associated modifications are not
    // made, and
    //  the fragment is always passed." [GLES 3.0.5, p175]
    mWebGL.mDriverStencilTest = driverStencilTest;
    gl->SetEnabled(LOCAL_GL_STENCIL_TEST, mWebGL.mDriverStencilTest);
  }
}

ScopedDrawCallWrapper::~ScopedDrawCallWrapper() {
  if (mWebGL.mBoundDrawFramebuffer) return;

  mWebGL.mResolvedDefaultFB = nullptr;
  mWebGL.mShouldPresent = true;
}

// -

void WebGLContext::ScissorRect::Apply(gl::GLContext& gl) const {
  gl.fScissor(x, y, w, h);
}

////////////////////////////////////////

IndexedBufferBinding::IndexedBufferBinding() = default;
IndexedBufferBinding::~IndexedBufferBinding() = default;

uint64_t IndexedBufferBinding::ByteCount() const {
  if (!mBufferBinding) return 0;

  uint64_t bufferSize = mBufferBinding->ByteLength();
  if (!mRangeSize)  // BindBufferBase
    return bufferSize;

  if (mRangeStart >= bufferSize) return 0;
  bufferSize -= mRangeStart;

  return std::min(bufferSize, mRangeSize);
}

////////////////////////////////////////

ScopedFBRebinder::~ScopedFBRebinder() {
  const auto fnName = [&](WebGLFramebuffer* fb) {
    return fb ? fb->mGLName : 0;
  };

  const auto& gl = mWebGL->gl;
  if (mWebGL->IsWebGL2()) {
    gl->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER,
                         fnName(mWebGL->mBoundDrawFramebuffer));
    gl->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER,
                         fnName(mWebGL->mBoundReadFramebuffer));
  } else {
    MOZ_ASSERT(mWebGL->mBoundDrawFramebuffer == mWebGL->mBoundReadFramebuffer);
    gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER,
                         fnName(mWebGL->mBoundDrawFramebuffer));
  }
}

////////////////////

void DoBindBuffer(gl::GLContext& gl, const GLenum target,
                  const WebGLBuffer* const buffer) {
  gl.fBindBuffer(target, buffer ? buffer->mGLName : 0);
}

////////////////////////////////////////

bool Intersect(const int32_t srcSize, const int32_t read0,
               const int32_t readSize, int32_t* const out_intRead0,
               int32_t* const out_intWrite0, int32_t* const out_intSize) {
  MOZ_ASSERT(srcSize >= 0);
  MOZ_ASSERT(readSize >= 0);
  const auto read1 = int64_t(read0) + readSize;

  int32_t intRead0 = read0;  // Clearly doesn't need validation.
  int64_t intWrite0 = 0;
  int64_t intSize = readSize;

  if (read1 <= 0 || read0 >= srcSize) {
    // Disjoint ranges.
    intSize = 0;
  } else {
    if (read0 < 0) {
      const auto diff = int64_t(0) - read0;
      MOZ_ASSERT(diff >= 0);
      intRead0 = 0;
      intWrite0 = diff;
      intSize -= diff;
    }
    if (read1 > srcSize) {
      const auto diff = int64_t(read1) - srcSize;
      MOZ_ASSERT(diff >= 0);
      intSize -= diff;
    }

    if (!CheckedInt<int32_t>(intWrite0).isValid() ||
        !CheckedInt<int32_t>(intSize).isValid()) {
      return false;
    }
  }

  *out_intRead0 = intRead0;
  *out_intWrite0 = intWrite0;
  *out_intSize = intSize;
  return true;
}

// --

uint64_t AvailGroups(const uint64_t totalAvailItems,
                     const uint64_t firstItemOffset, const uint32_t groupSize,
                     const uint32_t groupStride) {
  MOZ_ASSERT(groupSize && groupStride);
  MOZ_ASSERT(groupSize <= groupStride);

  if (totalAvailItems <= firstItemOffset) return 0;
  const size_t availItems = totalAvailItems - firstItemOffset;

  size_t availGroups = availItems / groupStride;
  const size_t tailItems = availItems % groupStride;
  if (tailItems >= groupSize) {
    availGroups += 1;
  }
  return availGroups;
}

////////////////////////////////////////////////////////////////////////////////

const char* WebGLContext::FuncName() const {
  const char* ret;
  if (MOZ_LIKELY(mFuncScope)) {
    ret = mFuncScope->mFuncName;
  } else {
    ret = "<unknown function>";
  }
  return ret;
}

// -

WebGLContext::FuncScope::FuncScope(const WebGLContext& webgl,
                                   const char* const funcName)
    : mWebGL(webgl), mFuncName(bool(mWebGL.mFuncScope) ? nullptr : funcName) {
  if (!mFuncName) return;
  mWebGL.mFuncScope = this;
}

WebGLContext::FuncScope::~FuncScope() {
  if (mBindFailureGuard) {
    gfxCriticalError() << "mBindFailureGuard failure: Early exit from "
                       << mWebGL.FuncName();
  }

  if (!mFuncName) return;
  mWebGL.mFuncScope = nullptr;
}

// --

bool ClientWebGLContext::IsXRCompatible() const { return mXRCompatible; }

already_AddRefed<dom::Promise> ClientWebGLContext::MakeXRCompatible(
    ErrorResult& aRv) {
  const FuncScope funcScope(*this, "MakeXRCompatible");
  nsCOMPtr<nsIGlobalObject> global = GetParentObject();
  if (!global) {
    aRv.ThrowInvalidAccessError(
        "Using a WebGL context that is not attached to either a canvas or an "
        "OffscreenCanvas");
    return nullptr;
  }
  RefPtr<dom::Promise> promise = dom::Promise::Create(global, aRv);
  NS_ENSURE_TRUE(!aRv.Failed(), nullptr);

  if (IsContextLost()) {
    promise->MaybeRejectWithInvalidStateError(
        "Can not make context XR compatible when context is already lost.");
    return promise.forget();
  }

  // TODO: Bug 1580258 - WebGLContext.MakeXRCompatible needs to switch to
  //                     the device connected to the XR hardware
  // This should update `options` and lose+restore the context.
  mXRCompatible = true;
  promise->MaybeResolveWithUndefined();
  return promise.forget();
}

// --

webgl::AvailabilityRunnable& ClientWebGLContext::EnsureAvailabilityRunnable()
    const {
  if (!mAvailabilityRunnable) {
    mAvailabilityRunnable = new webgl::AvailabilityRunnable(this);
    auto forgettable = mAvailabilityRunnable;
    NS_DispatchToCurrentThread(forgettable.forget());
  }
  return *mAvailabilityRunnable;
}

webgl::AvailabilityRunnable::AvailabilityRunnable(
    const ClientWebGLContext* const webgl)
    : DiscardableRunnable("webgl::AvailabilityRunnable"), mWebGL(webgl) {}

webgl::AvailabilityRunnable::~AvailabilityRunnable() {
  MOZ_ASSERT(mQueries.empty());
  MOZ_ASSERT(mSyncs.empty());
}

nsresult webgl::AvailabilityRunnable::Run() {
  for (const auto& cur : mQueries) {
    if (!cur) continue;
    cur->mCanBeAvailable = true;
  }
  mQueries.clear();

  for (const auto& cur : mSyncs) {
    if (!cur) continue;
    cur->mCanBeAvailable = true;
  }
  mSyncs.clear();

  if (mWebGL) {
    mWebGL->mAvailabilityRunnable = nullptr;
  }
  return NS_OK;
}

// -

void WebGLContext::JsWarning(const std::string& text) const {
  if (mHost) {
    mHost->JsWarning(text);
  }
#ifdef DEBUG
  if (!mHost) {
    NS_WARNING(text.c_str());
  }
#endif
}

void WebGLContext::GenerateErrorImpl(const GLenum errOrWarning,
                                     const std::string& text) const {
  auto err = errOrWarning;
  bool isPerfWarning = false;
  if (err == webgl::kErrorPerfWarning) {
    err = 0;
    isPerfWarning = true;
  }

  if (err && mFuncScope && mFuncScope->mBindFailureGuard) {
    gfxCriticalError() << "mBindFailureGuard failure: Generating error "
                       << EnumString(err) << ": " << text;
  }

  /* ES2 section 2.5 "GL Errors" states that implementations can have
   * multiple 'flags', as errors might be caught in different parts of
   * a distributed implementation.
   * We're signing up as a distributed implementation here, with
   * separate flags for WebGL and the underlying GLContext.
   */
  if (!mWebGLError) mWebGLError = err;

  if (!mHost) return;  // Impossible?

  // -

  const auto ShouldWarn = [&]() {
    if (isPerfWarning) {
      return ShouldGeneratePerfWarnings();
    }
    return ShouldGenerateWarnings();
  };
  if (!ShouldWarn()) return;

  // -

  auto* pNumWarnings = &mWarningCount;
  const char* warningsType = "warnings";
  if (isPerfWarning) {
    pNumWarnings = &mNumPerfWarnings;
    warningsType = "perf warnings";
  }

  if (isPerfWarning) {
    const auto perfText = std::string("WebGL perf warning: ") + text;
    JsWarning(perfText);
  } else {
    JsWarning(text);
  }
  *pNumWarnings += 1;

  if (!ShouldWarn()) {
    const auto& msg = nsPrintfCString(
        "After reporting %i, no further %s will be reported for this WebGL "
        "context.",
        int(*pNumWarnings), warningsType);
    JsWarning(ToString(msg));
  }
}

// -

Maybe<std::string> WebGLContext::GetString(const GLenum pname) const {
  const WebGLContext::FuncScope funcScope(*this, "getParameter");
  if (IsContextLost()) return {};

  const auto FromRaw = [](const char* const raw) -> Maybe<std::string> {
    if (!raw) return {};
    return Some(std::string(raw));
  };

  switch (pname) {
    case LOCAL_GL_EXTENSIONS: {
      if (!gl->IsCoreProfile()) {
        const auto rawExt = (const char*)gl->fGetString(LOCAL_GL_EXTENSIONS);
        return FromRaw(rawExt);
      }
      std::string ret;
      const auto& numExts = gl->GetIntAs<GLuint>(LOCAL_GL_NUM_EXTENSIONS);
      for (GLuint i = 0; i < numExts; i++) {
        const auto rawExt =
            (const char*)gl->fGetStringi(LOCAL_GL_EXTENSIONS, i);
        if (!rawExt) continue;

        if (i > 0) {
          ret += " ";
        }
        ret += rawExt;
      }
      return Some(std::move(ret));
    }

    case LOCAL_GL_RENDERER:
    case LOCAL_GL_VENDOR:
    case LOCAL_GL_VERSION: {
      const auto raw = (const char*)gl->fGetString(pname);
      return FromRaw(raw);
    }

    case dom::MOZ_debug_Binding::WSI_INFO: {
      nsCString info;
      gl->GetWSIInfo(&info);
      return Some(std::string(info.BeginReading()));
    }

    case dom::MOZ_debug_Binding::CONTEXT_TYPE: {
      gl::GLContextType ctxType = gl->GetContextType();
      switch (ctxType) {
        case gl::GLContextType::Unknown:
          return Some("unknown"_ns);
        case gl::GLContextType::WGL:
          return Some("wgl"_ns);
        case gl::GLContextType::CGL:
          return Some("cgl"_ns);
        case gl::GLContextType::GLX:
          return Some("glx"_ns);
        case gl::GLContextType::EGL:
          return Some("egl"_ns);
        case gl::GLContextType::EAGL:
          return Some("eagl"_ns);
      }
      return Some("unknown"_ns);
    }

    default:
      ErrorInvalidEnumArg("pname", pname);
      return {};
  }
}

// ---------------------------------

Maybe<webgl::IndexedName> webgl::ParseIndexed(const std::string& str) {
  static const std::regex kRegex("(.*)\\[([0-9]+)\\]");

  std::smatch match;
  if (!std::regex_match(str, match, kRegex)) return {};

  const auto index = std::stoull(match[2]);
  return Some(webgl::IndexedName{match[1], index});
}

// ExplodeName("foo.bar[3].x") -> ["foo", ".", "bar", "[", "3", "]", ".", "x"]
static std::vector<std::string> ExplodeName(const std::string& str) {
  std::vector<std::string> ret;

  static const std::regex kSep("[.[\\]]");

  auto itr = std::regex_token_iterator<decltype(str.begin())>(
      str.begin(), str.end(), kSep, {-1, 0});
  const auto end = decltype(itr)();

  for (; itr != end; ++itr) {
    const auto& part = itr->str();
    if (part.size()) {
      ret.push_back(part);
    }
  }
  return ret;
}

//-

// #define DUMP_MakeLinkResult

webgl::LinkActiveInfo GetLinkActiveInfo(
    gl::GLContext& gl, const GLuint prog, const bool webgl2,
    const std::unordered_map<std::string, std::string>& nameUnmap) {
  webgl::LinkActiveInfo ret;
  [&]() {
    const auto fnGetProgramui = [&](const GLenum pname) {
      GLint ret = 0;
      gl.fGetProgramiv(prog, pname, &ret);
      return static_cast<uint32_t>(ret);
    };

    std::vector<char> stringBuffer(1);
    const auto fnEnsureCapacity = [&](const GLenum pname) {
      const auto maxWithNull = fnGetProgramui(pname);
      if (maxWithNull > stringBuffer.size()) {
        stringBuffer.resize(maxWithNull);
      }
    };

    fnEnsureCapacity(LOCAL_GL_ACTIVE_ATTRIBUTE_MAX_LENGTH);
    fnEnsureCapacity(LOCAL_GL_ACTIVE_UNIFORM_MAX_LENGTH);
    if (webgl2) {
      fnEnsureCapacity(LOCAL_GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH);
      fnEnsureCapacity(LOCAL_GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH);
    }

    // -

    const auto fnUnmapName = [&](const std::string& mappedName) {
      const auto parts = ExplodeName(mappedName);

      std::ostringstream ret;
      for (const auto& part : parts) {
        const auto maybe = MaybeFind(nameUnmap, part);
        if (maybe) {
          ret << *maybe;
        } else {
          ret << part;
        }
      }
      return ret.str();
    };

    // -

    {
      const auto count = fnGetProgramui(LOCAL_GL_ACTIVE_ATTRIBUTES);
      ret.activeAttribs.reserve(count);
      for (const auto i : IntegerRange(count)) {
        GLsizei lengthWithoutNull = 0;
        GLint elemCount = 0;  // `size`
        GLenum elemType = 0;  // `type`
        gl.fGetActiveAttrib(prog, i, stringBuffer.size(), &lengthWithoutNull,
                            &elemCount, &elemType, stringBuffer.data());
        if (!elemType) {
          const auto error = gl.fGetError();
          if (error != LOCAL_GL_CONTEXT_LOST) {
            gfxCriticalError() << "Failed to do glGetActiveAttrib: " << error;
          }
          return;
        }
        const auto mappedName =
            std::string(stringBuffer.data(), lengthWithoutNull);
        const auto userName = fnUnmapName(mappedName);

        auto loc = gl.fGetAttribLocation(prog, mappedName.c_str());
        if (mappedName.find("gl_") == 0) {
          // Bug 1328559: Appears problematic on ANGLE and OSX, but not Linux or
          // Win+GL.
          loc = -1;
        }

#ifdef DUMP_MakeLinkResult
        printf_stderr("[attrib %u/%u] @%i %s->%s\n", i, count, loc,
                      userName.c_str(), mappedName.c_str());
#endif
        webgl::ActiveAttribInfo info;
        info.elemType = elemType;
        info.elemCount = elemCount;
        info.name = userName;
        info.location = loc;
        info.baseType = webgl::ToAttribBaseType(info.elemType);
        ret.activeAttribs.push_back(std::move(info));
      }
    }

    // -

    {
      const auto count = fnGetProgramui(LOCAL_GL_ACTIVE_UNIFORMS);
      ret.activeUniforms.reserve(count);

      std::vector<GLint> blockIndexList(count, -1);
      std::vector<GLint> blockOffsetList(count, -1);
      std::vector<GLint> blockArrayStrideList(count, -1);
      std::vector<GLint> blockMatrixStrideList(count, -1);
      std::vector<GLint> blockIsRowMajorList(count, 0);

      if (webgl2 && count) {
        std::vector<GLuint> activeIndices;
        activeIndices.reserve(count);
        for (const auto i : IntegerRange(count)) {
          activeIndices.push_back(i);
        }

        gl.fGetActiveUniformsiv(
            prog, activeIndices.size(), activeIndices.data(),
            LOCAL_GL_UNIFORM_BLOCK_INDEX, blockIndexList.data());

        gl.fGetActiveUniformsiv(prog, activeIndices.size(),
                                activeIndices.data(), LOCAL_GL_UNIFORM_OFFSET,
                                blockOffsetList.data());

        gl.fGetActiveUniformsiv(
            prog, activeIndices.size(), activeIndices.data(),
            LOCAL_GL_UNIFORM_ARRAY_STRIDE, blockArrayStrideList.data());

        gl.fGetActiveUniformsiv(
            prog, activeIndices.size(), activeIndices.data(),
            LOCAL_GL_UNIFORM_MATRIX_STRIDE, blockMatrixStrideList.data());

        gl.fGetActiveUniformsiv(
            prog, activeIndices.size(), activeIndices.data(),
            LOCAL_GL_UNIFORM_IS_ROW_MAJOR, blockIsRowMajorList.data());
      }

      for (const auto i : IntegerRange(count)) {
        GLsizei lengthWithoutNull = 0;
        GLint elemCount = 0;  // `size`
        GLenum elemType = 0;  // `type`
        gl.fGetActiveUniform(prog, i, stringBuffer.size(), &lengthWithoutNull,
                             &elemCount, &elemType, stringBuffer.data());
        if (!elemType) {
          const auto error = gl.fGetError();
          if (error != LOCAL_GL_CONTEXT_LOST) {
            gfxCriticalError() << "Failed to do glGetActiveUniform: " << error;
          }
          return;
        }
        auto mappedName = std::string(stringBuffer.data(), lengthWithoutNull);

        // Get true name

        auto baseMappedName = mappedName;

        const bool isArray = [&]() {
          const auto maybe = webgl::ParseIndexed(mappedName);
          if (maybe) {
            MOZ_ASSERT(maybe->index == 0);
            baseMappedName = std::move(maybe->name);
            return true;
          }
          return false;
        }();

        const auto userName = fnUnmapName(mappedName);
        if (StartsWith(userName, "webgl_")) continue;

        // -

        webgl::ActiveUniformInfo info;
        info.elemType = elemType;
        info.elemCount = static_cast<uint32_t>(elemCount);
        info.name = userName;
        info.block_index = blockIndexList[i];
        info.block_offset = blockOffsetList[i];
        info.block_arrayStride = blockArrayStrideList[i];
        info.block_matrixStride = blockMatrixStrideList[i];
        info.block_isRowMajor = bool(blockIsRowMajorList[i]);

#ifdef DUMP_MakeLinkResult
        printf_stderr("[uniform %u/%u] %s->%s\n", i + 1, count,
                      userName.c_str(), mappedName.c_str());
#endif

        // Get uniform locations
        {
          auto locName = baseMappedName;
          const auto baseLength = locName.size();
          for (const auto i : IntegerRange(info.elemCount)) {
            if (isArray) {
              locName.erase(
                  baseLength);  // Erase previous [N], but retain capacity.
              locName += '[';
              locName += std::to_string(i);
              locName += ']';
            }
            const auto loc = gl.fGetUniformLocation(prog, locName.c_str());
            if (loc != -1) {
              info.locByIndex[i] = static_cast<uint32_t>(loc);
#ifdef DUMP_MakeLinkResult
              printf_stderr("   [%u] @%i\n", i, loc);
#endif
            }
          }
        }  // anon

        ret.activeUniforms.push_back(std::move(info));
      }  // for i
    }  // anon

    if (webgl2) {
      // -------------------------------------
      // active uniform blocks
      {
        const auto count = fnGetProgramui(LOCAL_GL_ACTIVE_UNIFORM_BLOCKS);
        ret.activeUniformBlocks.reserve(count);

        for (const auto i : IntegerRange(count)) {
          GLsizei lengthWithoutNull = 0;
          gl.fGetActiveUniformBlockName(prog, i, stringBuffer.size(),
                                        &lengthWithoutNull,
                                        stringBuffer.data());
          const auto mappedName =
              std::string(stringBuffer.data(), lengthWithoutNull);
          const auto userName = fnUnmapName(mappedName);

          // -

          auto info = webgl::ActiveUniformBlockInfo{userName};
          GLint val = 0;

          gl.fGetActiveUniformBlockiv(prog, i, LOCAL_GL_UNIFORM_BLOCK_DATA_SIZE,
                                      &val);
          info.dataSize = static_cast<uint32_t>(val);

          gl.fGetActiveUniformBlockiv(
              prog, i, LOCAL_GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &val);
          info.activeUniformIndices.resize(val);
          gl.fGetActiveUniformBlockiv(
              prog, i, LOCAL_GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES,
              reinterpret_cast<GLint*>(info.activeUniformIndices.data()));

          gl.fGetActiveUniformBlockiv(
              prog, i, LOCAL_GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER,
              &val);
          info.referencedByVertexShader = bool(val);

          gl.fGetActiveUniformBlockiv(
              prog, i, LOCAL_GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER,
              &val);
          info.referencedByFragmentShader = bool(val);

          ret.activeUniformBlocks.push_back(std::move(info));
        }  // for i
      }  // anon

      // -------------------------------------
      // active tf varyings
      {
        const auto count = fnGetProgramui(LOCAL_GL_TRANSFORM_FEEDBACK_VARYINGS);
        ret.activeTfVaryings.reserve(count);

        for (const auto i : IntegerRange(count)) {
          GLsizei lengthWithoutNull = 0;
          GLsizei elemCount = 0;  // `size`
          GLenum elemType = 0;    // `type`
          gl.fGetTransformFeedbackVarying(prog, i, stringBuffer.size(),
                                          &lengthWithoutNull, &elemCount,
                                          &elemType, stringBuffer.data());
          const auto mappedName =
              std::string(stringBuffer.data(), lengthWithoutNull);
          const auto userName = fnUnmapName(mappedName);

          ret.activeTfVaryings.push_back(
              {elemType, static_cast<uint32_t>(elemCount), userName});
        }
      }
    }  // if webgl2
  }();
  return ret;
}

nsCString ToCString(const std::string& s) {
  return nsCString(s.data(), s.size());
}

webgl::CompileResult WebGLContext::GetCompileResult(
    const WebGLShader& shader) const {
  webgl::CompileResult ret;
  [&]() {
    ret.pending = false;
    const auto& info = shader.CompileResults();
    if (!info) return;
    if (!info->mValid) {
      ret.log = info->mInfoLog.c_str();
      return;
    }
    // TODO: These could be large and should be made fallible.
    ret.translatedSource = ToCString(info->mObjectCode);
    ret.log = ToCString(shader.CompileLog());
    if (!shader.IsCompiled()) return;
    ret.success = true;
  }();
  return ret;
}

webgl::LinkResult WebGLContext::GetLinkResult(const WebGLProgram& prog) const {
  webgl::LinkResult ret;
  [&]() {
    ret.pending = false;  // Link status polling not yet implemented.
    ret.log = ToCString(prog.LinkLog());
    const auto& info = prog.LinkInfo();
    if (!info) return;
    ret.success = true;
    ret.active = info->active;
    ret.tfBufferMode = info->transformFeedbackBufferMode;
  }();
  return ret;
}

// -

GLint WebGLContext::GetFragDataLocation(const WebGLProgram& prog,
                                        const std::string& userName) const {
  const auto err = CheckGLSLVariableName(IsWebGL2(), userName);
  if (err) {
    GenerateError(err->type, "%s", err->info.c_str());
    return -1;
  }

  const auto& info = prog.LinkInfo();
  if (!info) return -1;
  const auto& nameMap = info->nameMap;

  const auto parts = ExplodeName(userName);

  std::ostringstream ret;
  for (const auto& part : parts) {
    const auto maybe = MaybeFind(nameMap, part);
    if (maybe) {
      ret << *maybe;
    } else {
      ret << part;
    }
  }
  const auto mappedName = ret.str();

  if (gl->WorkAroundDriverBugs() && gl->IsMesa()) {
    // Mesa incorrectly generates INVALID_OPERATION for gl_ prefixes here.
    if (mappedName.find("gl_") == 0) {
      return -1;
    }
  }

  return gl->fGetFragDataLocation(prog.mGLName, mappedName.c_str());
}

// -

WebGLContextBoundObject::WebGLContextBoundObject(WebGLContext* webgl)
    : mContext(webgl) {}

// -

Result<webgl::ExplicitPixelPackingState, std::string>
webgl::ExplicitPixelPackingState::ForUseWith(
    const webgl::PixelPackingState& stateOrZero, const GLenum target,
    const uvec3& subrectSize, const webgl::PackingInfo& pi,
    const Maybe<size_t> bytesPerRowStrideOverride) {
  auto state = stateOrZero;

  if (!IsTexTarget3D(target)) {
    state.skipImages = 0;
    state.imageHeight = 0;
  }
  if (!state.rowLength) {
    state.rowLength = subrectSize.x;
  }
  if (!state.imageHeight) {
    state.imageHeight = subrectSize.y;
  }

  // -

  const auto mpii = PackingInfoInfo::For(pi);
  if (!mpii) {
    const auto text =
        nsPrintfCString("Invalid pi: { 0x%x, 0x%x}", pi.format, pi.type);
    return Err(mozilla::ToString(text));
  }
  const auto pii = *mpii;
  const auto bytesPerPixel = pii.BytesPerPixel();

  const auto ElemsPerRowStride = [&]() {
    // GLES 3.0.6 p116:
    // p: `Elem*` pointer to the first element of the first row
    // N: row number, starting at 0
    // l: groups (pixels) per row
    // n: elements per group (pixel) in [1,2,3,4]
    // s: bytes per element in [1,2,4,8]
    // a: UNPACK_ALIGNMENT in [1,2,4,8]
    // Pointer to first element of Nth row: p + N*k
    // k(s>=a): n*l
    // k(s<a): a/s * ceil(s*n*l/a)
    const auto n__elemsPerPixel = pii.elementsPerPixel;
    const auto l__pixelsPerRow = state.rowLength;
    const auto a__alignment = state.alignmentInTypeElems;
    const auto s__bytesPerElem = pii.bytesPerElement;

    const auto nl = CheckedInt<size_t>(n__elemsPerPixel) * l__pixelsPerRow;
    auto k__elemsPerRowStride = nl;
    if (s__bytesPerElem < a__alignment) {
      // k = a/s * ceil(s*n*l/a)
      k__elemsPerRowStride =
          a__alignment / s__bytesPerElem *
          ((nl * s__bytesPerElem + a__alignment - 1) / a__alignment);
    }
    return k__elemsPerRowStride;
  };

  // -

  if (bytesPerRowStrideOverride) {  // E.g. HTMLImageElement
    const size_t bytesPerRowStrideRequired = *bytesPerRowStrideOverride;
    // We have to reverse-engineer an ALIGNMENT and ROW_LENGTH for this.

    // GL does this in elems not bytes, so we should too.
    MOZ_RELEASE_ASSERT(bytesPerRowStrideRequired % pii.bytesPerElement == 0);
    const auto elemsPerRowStrideRequired =
        bytesPerRowStrideRequired / pii.bytesPerElement;

    state.rowLength = bytesPerRowStrideRequired / bytesPerPixel;
    state.alignmentInTypeElems = 8;
    while (true) {
      const auto elemPerRowStride = ElemsPerRowStride();
      if (elemPerRowStride.isValid() &&
          elemPerRowStride.value() == elemsPerRowStrideRequired) {
        break;
      }
      state.alignmentInTypeElems /= 2;
      if (!state.alignmentInTypeElems) {
        const auto text = nsPrintfCString(
            "No valid alignment found: pi: { 0x%x, 0x%x},"
            " bytesPerRowStrideRequired: %zu",
            pi.format, pi.type, bytesPerRowStrideRequired);
        return Err(mozilla::ToString(text));
      }
    }
  }

  // -

  const auto usedPixelsPerRow =
      CheckedInt<size_t>(state.skipPixels) + subrectSize.x;
  if (!usedPixelsPerRow.isValid() ||
      usedPixelsPerRow.value() > state.rowLength) {
    return Err("UNPACK_SKIP_PIXELS + width > UNPACK_ROW_LENGTH.");
  }

  if (subrectSize.y > state.imageHeight) {
    return Err("height > UNPACK_IMAGE_HEIGHT.");
  }
  // The spec doesn't bound SKIP_ROWS + height <= IMAGE_HEIGHT, unfortunately.

  // -

  auto metrics = Metrics{};

  metrics.usedSize = subrectSize;
  metrics.bytesPerPixel = BytesPerPixel(pi);

  // -

  const auto elemsPerRowStride = ElemsPerRowStride();
  const auto bytesPerRowStride = pii.bytesPerElement * elemsPerRowStride;
  if (!bytesPerRowStride.isValid()) {
    return Err("ROW_LENGTH or width too large for packing.");
  }
  metrics.bytesPerRowStride = bytesPerRowStride.value();

  // -

  const auto firstImageTotalRows =
      CheckedInt<size_t>(state.skipRows) + metrics.usedSize.y;
  const auto totalImages =
      CheckedInt<size_t>(state.skipImages) + metrics.usedSize.z;
  auto totalRows = CheckedInt<size_t>(0);
  if (metrics.usedSize.y && metrics.usedSize.z) {
    totalRows = firstImageTotalRows + state.imageHeight * (totalImages - 1);
  }
  if (!totalRows.isValid()) {
    return Err(
        "SKIP_ROWS, height, IMAGE_HEIGHT, SKIP_IMAGES, or depth too large for "
        "packing.");
  }
  metrics.totalRows = totalRows.value();

  // -

  const auto totalBytesStrided = totalRows * metrics.bytesPerRowStride;
  if (!totalBytesStrided.isValid()) {
    return Err("Total byte count too large for packing.");
  }
  metrics.totalBytesStrided = totalBytesStrided.value();

  metrics.totalBytesUsed = metrics.totalBytesStrided;
  if (metrics.usedSize.x && metrics.usedSize.y && metrics.usedSize.z) {
    const auto usedBytesPerRow =
        usedPixelsPerRow.value() * metrics.bytesPerPixel;
    metrics.totalBytesUsed -= metrics.bytesPerRowStride;
    metrics.totalBytesUsed += usedBytesPerRow;
  }

  // -

  return {{state, metrics}};
}

GLuint WebGLContext::SamplerLinear() const {
  if (!mSamplerLinear) {
    mSamplerLinear = std::make_unique<gl::Sampler>(*gl);
    gl->fSamplerParameteri(mSamplerLinear->name, LOCAL_GL_TEXTURE_MAG_FILTER,
                           LOCAL_GL_LINEAR);
    gl->fSamplerParameteri(mSamplerLinear->name, LOCAL_GL_TEXTURE_MIN_FILTER,
                           LOCAL_GL_LINEAR);
    gl->fSamplerParameteri(mSamplerLinear->name, LOCAL_GL_TEXTURE_WRAP_S,
                           LOCAL_GL_CLAMP_TO_EDGE);
    gl->fSamplerParameteri(mSamplerLinear->name, LOCAL_GL_TEXTURE_WRAP_T,
                           LOCAL_GL_CLAMP_TO_EDGE);
    gl->fSamplerParameteri(mSamplerLinear->name, LOCAL_GL_TEXTURE_WRAP_R,
                           LOCAL_GL_CLAMP_TO_EDGE);
  }
  return mSamplerLinear->name;
}

}  // namespace mozilla
