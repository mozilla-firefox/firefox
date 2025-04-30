/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClientWebGLContext.h"

#include <bitset>
#include <fmt/format.h>

#include "ClientWebGLExtensions.h"
#include "gfxCrashReporterUtils.h"
#include "HostWebGLContext.h"
#include "js/PropertyAndElement.h"  // JS_DefineElement
#include "js/ScalarType.h"          // js::Scalar::Type
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/dom/WebGLContextEvent.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/EnumeratedRange.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gfx/CanvasManagerChild.h"
#include "mozilla/ipc/Shmem.h"
#include "mozilla/gfx/Swizzle.h"
#include "mozilla/layers/CompositableForwarder.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/OOPCanvasRenderer.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "mozilla/layers/WebRenderUserData.h"
#include "mozilla/layers/WebRenderCanvasRenderer.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "TexUnpackBlob.h"
#include "WebGLFormats.h"
#include "WebGLMethodDispatcher.h"
#include "WebGLChild.h"
#include "WebGLTextureUpload.h"
#include "WebGLValidateStrings.h"

namespace mozilla {

namespace webgl {
std::string SanitizeRenderer(const std::string&);
}  // namespace webgl

// -

webgl::NotLostData::NotLostData(ClientWebGLContext& _context)
    : context(_context) {}

webgl::NotLostData::~NotLostData() {
  if (outOfProcess) {
    outOfProcess->Destroy();
  }
}

// -

bool webgl::ObjectJS::ValidateForContext(
    const ClientWebGLContext& targetContext, const char* const argName) const {
  if (!IsForContext(targetContext)) {
    targetContext.EnqueueError(
        LOCAL_GL_INVALID_OPERATION,
        "`%s` is from a different (or lost) WebGL context.", argName);
    return false;
  }
  return true;
}

void webgl::ObjectJS::WarnInvalidUse(const ClientWebGLContext& targetContext,
                                     const char* const argName) const {
  if (!ValidateForContext(targetContext, argName)) return;

  const auto errEnum = ErrorOnDeleted();
  targetContext.EnqueueError(errEnum, "Object `%s` is already deleted.",
                             argName);
}

// -

WebGLBufferJS::~WebGLBufferJS() {
  const auto webgl = Context();
  if (webgl) {
    webgl->DeleteBuffer(this);
  }
}

WebGLFramebufferJS::~WebGLFramebufferJS() {
  const auto webgl = Context();
  if (webgl) {
    webgl->DeleteFramebuffer(this);
  }
}

WebGLQueryJS::~WebGLQueryJS() {
  const auto webgl = Context();
  if (webgl) {
    webgl->DeleteQuery(this);
  }
}

WebGLRenderbufferJS::~WebGLRenderbufferJS() {
  const auto webgl = Context();
  if (webgl) {
    webgl->DeleteRenderbuffer(this);
  }
}

WebGLSamplerJS::~WebGLSamplerJS() {
  const auto webgl = Context();
  if (webgl) {
    webgl->DeleteSampler(this);
  }
}

WebGLSyncJS::~WebGLSyncJS() {
  const auto webgl = Context();
  if (webgl) {
    webgl->DeleteSync(this);
  }
}

WebGLTextureJS::~WebGLTextureJS() {
  const auto webgl = Context();
  if (webgl) {
    webgl->DeleteTexture(this);
  }
}

WebGLTransformFeedbackJS::~WebGLTransformFeedbackJS() {
  const auto webgl = Context();
  if (webgl) {
    webgl->DeleteTransformFeedback(this);
  }
}

WebGLVertexArrayJS::~WebGLVertexArrayJS() {
  const auto webgl = Context();
  if (webgl) {
    webgl->DeleteVertexArray(this);
  }
}

// -

static bool GetJSScalarFromGLType(GLenum type,
                                  js::Scalar::Type* const out_scalarType) {
  switch (type) {
    case LOCAL_GL_BYTE:
      *out_scalarType = js::Scalar::Int8;
      return true;

    case LOCAL_GL_UNSIGNED_BYTE:
      *out_scalarType = js::Scalar::Uint8;
      return true;

    case LOCAL_GL_SHORT:
      *out_scalarType = js::Scalar::Int16;
      return true;

    case LOCAL_GL_HALF_FLOAT:
    case LOCAL_GL_HALF_FLOAT_OES:
    case LOCAL_GL_UNSIGNED_SHORT:
    case LOCAL_GL_UNSIGNED_SHORT_4_4_4_4:
    case LOCAL_GL_UNSIGNED_SHORT_5_5_5_1:
    case LOCAL_GL_UNSIGNED_SHORT_5_6_5:
      *out_scalarType = js::Scalar::Uint16;
      return true;

    case LOCAL_GL_UNSIGNED_INT:
    case LOCAL_GL_UNSIGNED_INT_2_10_10_10_REV:
    case LOCAL_GL_UNSIGNED_INT_5_9_9_9_REV:
    case LOCAL_GL_UNSIGNED_INT_10F_11F_11F_REV:
    case LOCAL_GL_UNSIGNED_INT_24_8:
      *out_scalarType = js::Scalar::Uint32;
      return true;
    case LOCAL_GL_INT:
      *out_scalarType = js::Scalar::Int32;
      return true;

    case LOCAL_GL_FLOAT:
      *out_scalarType = js::Scalar::Float32;
      return true;

    default:
      return false;
  }
}

ClientWebGLContext::ClientWebGLContext(const bool webgl2)
    : mIsWebGL2(webgl2),
      mExtLoseContext(new ClientWebGLExtensionLoseContext(*this)) {}

ClientWebGLContext::~ClientWebGLContext() { RemovePostRefreshObserver(); }

void ClientWebGLContext::JsWarning(const std::string& utf8) const {
  nsIGlobalObject* global = nullptr;
  if (mCanvasElement) {
    mozilla::dom::Document* doc = mCanvasElement->OwnerDoc();
    if (doc) {
      global = doc->GetScopeObject();
    }
  } else if (mOffscreenCanvas) {
    global = mOffscreenCanvas->GetOwnerGlobal();
  }

  dom::AutoJSAPI api;
  if (!api.Init(global)) {
    return;
  }
  const auto& cx = api.cx();
  JS::WarnUTF8(cx, "%s", utf8.c_str());
}

void AutoJsWarning(const std::string& utf8) {
  if (NS_IsMainThread()) {
    const AutoJSContext cx;
    JS::WarnUTF8(cx, "%s", utf8.c_str());
    return;
  }

  JSContext* cx = dom::GetCurrentWorkerThreadJSContext();
  if (NS_WARN_IF(!cx)) {
    return;
  }

  JS::WarnUTF8(cx, "%s", utf8.c_str());
}

// ---------

bool ClientWebGLContext::DispatchEvent(const nsAString& eventName) const {
  const auto kCanBubble = CanBubble::eYes;
  const auto kIsCancelable = Cancelable::eYes;
  bool useDefaultHandler = true;

  if (mCanvasElement) {
    nsContentUtils::DispatchTrustedEvent(mCanvasElement->OwnerDoc(),
                                         mCanvasElement, eventName, kCanBubble,
                                         kIsCancelable, &useDefaultHandler);
  } else if (mOffscreenCanvas) {
    // OffscreenCanvas case
    RefPtr<dom::Event> event =
        new dom::Event(mOffscreenCanvas, nullptr, nullptr);
    event->InitEvent(eventName, kCanBubble, kIsCancelable);
    event->SetTrusted(true);
    useDefaultHandler = mOffscreenCanvas->DispatchEvent(
        *event, dom::CallerType::System, IgnoreErrors());
  }
  return useDefaultHandler;
}

// -

void ClientWebGLContext::EmulateLoseContext() const {
  const FuncScope funcScope(*this, "loseContext");
  if (mLossStatus != webgl::LossStatus::Ready) {
    JsWarning("loseContext: Already lost.");
    if (!mNextError) {
      mNextError = LOCAL_GL_INVALID_OPERATION;
    }
    return;
  }
  OnContextLoss(webgl::ContextLossReason::Manual);
}

void ClientWebGLContext::OnContextLoss(
    const webgl::ContextLossReason reason) const {
  JsWarning("WebGL context was lost.");

  if (mNotLost) {
    for (const auto& ext : mNotLost->extensions) {
      if (!ext) continue;
      ext->mContext = nullptr;  // Detach.
    }
    mNotLost = {};  // Lost now!
    mNextError = LOCAL_GL_CONTEXT_LOST_WEBGL;
  }

  switch (reason) {
    case webgl::ContextLossReason::Guilty:
      mLossStatus = webgl::LossStatus::LostForever;
      break;

    case webgl::ContextLossReason::None:
      mLossStatus = webgl::LossStatus::Lost;
      break;

    case webgl::ContextLossReason::Manual:
      mLossStatus = webgl::LossStatus::LostManually;
      break;
  }

  const auto weak = WeakPtr<const ClientWebGLContext>(this);
  const auto fnRun = [weak]() {
    const auto strong = RefPtr<const ClientWebGLContext>(weak);
    if (!strong) return;
    strong->Event_webglcontextlost();
  };
  already_AddRefed<mozilla::CancelableRunnable> runnable =
      NS_NewCancelableRunnableFunction("enqueue Event_webglcontextlost", fnRun);
  NS_DispatchToCurrentThread(std::move(runnable));
}

void ClientWebGLContext::Event_webglcontextlost() const {
  const bool useDefaultHandler = DispatchEvent(u"webglcontextlost"_ns);
  if (useDefaultHandler) {
    mLossStatus = webgl::LossStatus::LostForever;
  }

  if (mLossStatus == webgl::LossStatus::Lost) {
    RestoreContext(webgl::LossStatus::Lost);
  }
}

void ClientWebGLContext::RestoreContext(
    const webgl::LossStatus requiredStatus) const {
  if (requiredStatus != mLossStatus) {
    JsWarning(
        "restoreContext: Only valid iff context lost with loseContext().");
    if (!mNextError) {
      mNextError = LOCAL_GL_INVALID_OPERATION;
    }
    return;
  }
  MOZ_RELEASE_ASSERT(mLossStatus == webgl::LossStatus::Lost ||
                     mLossStatus == webgl::LossStatus::LostManually);

  if (mAwaitingRestore) return;
  mAwaitingRestore = true;

  const auto weak = WeakPtr<const ClientWebGLContext>(this);
  const auto fnRun = [weak]() {
    const auto strong = RefPtr<const ClientWebGLContext>(weak);
    if (!strong) return;
    strong->Event_webglcontextrestored();
  };
  already_AddRefed<mozilla::CancelableRunnable> runnable =
      NS_NewCancelableRunnableFunction("enqueue Event_webglcontextrestored",
                                       fnRun);
  NS_DispatchToCurrentThread(std::move(runnable));
}

void ClientWebGLContext::Event_webglcontextrestored() const {
  mAwaitingRestore = false;
  mLossStatus = webgl::LossStatus::Ready;
  mNextError = 0;

  uvec2 requestSize;
  if (mCanvasElement) {
    requestSize = {mCanvasElement->Width(), mCanvasElement->Height()};
  } else if (mOffscreenCanvas) {
    requestSize = {mOffscreenCanvas->Width(), mOffscreenCanvas->Height()};
  } else {
    MOZ_ASSERT_UNREACHABLE("no HTMLCanvasElement or OffscreenCanvas!");
    return;
  }

  if (!requestSize.x) {
    requestSize.x = 1;
  }
  if (!requestSize.y) {
    requestSize.y = 1;
  }

  const auto mutThis = const_cast<ClientWebGLContext*>(
      this);  // TODO: Make context loss non-mutable.
  if (!mutThis->CreateHostContext(requestSize)) {
    mLossStatus = webgl::LossStatus::LostForever;
    return;
  }

  mResetLayer = true;

  (void)DispatchEvent(u"webglcontextrestored"_ns);
}

// ---------

void ClientWebGLContext::ThrowEvent_WebGLContextCreationError(
    const std::string& text) const {
  nsCString msg;
  msg.AppendPrintf("Failed to create WebGL context: %s", text.c_str());
  JsWarning(msg.BeginReading());

  RefPtr<dom::EventTarget> target = mCanvasElement;
  if (!target && mOffscreenCanvas) {
    target = mOffscreenCanvas;
  } else if (!target) {
    return;
  }

  const auto kEventName = u"webglcontextcreationerror"_ns;

  dom::WebGLContextEventInit eventInit;
  // eventInit.mCancelable = true; // The spec says this, but it's silly.
  eventInit.mStatusMessage = NS_ConvertASCIItoUTF16(text.c_str());

  const RefPtr<dom::WebGLContextEvent> event =
      dom::WebGLContextEvent::Constructor(target, kEventName, eventInit);
  event->SetTrusted(true);

  target->DispatchEvent(*event);
}

// -------------------------------------------------------------------------
// Client-side helper methods.  Dispatch to a Host method.
// -------------------------------------------------------------------------

// If we are running WebGL in this process then call the HostWebGLContext
// method directly.  Otherwise, dispatch over IPC.
template <typename MethodT, typename... Args>
void ClientWebGLContext::Run_WithDestArgTypes(
    std::optional<JS::AutoCheckCannotGC>&& noGc, const MethodT method,
    const size_t id, const Args&... args) const {
  const auto notLost =
      mNotLost;  // Hold a strong-ref to prevent LoseContext=>UAF.

  // `AutoCheckCannotGC` must be reset after the GC data is done being used but
  // *before* the `notLost` destructor runs, since the latter can GC.
  const auto cleanup = MakeScopeExit([&]() { noGc.reset(); });

  if (IsContextLost()) {
    return;
  }

  const auto& inProcess = notLost->inProcess;
  if (inProcess) {
    (inProcess.get()->*method)(args...);
    return;
  }

  const auto& child = notLost->outOfProcess;

  const auto info = webgl::SerializationInfo(id, args...);
  const auto maybeDest = child->AllocPendingCmdBytes(info.requiredByteCount,
                                                     info.alignmentOverhead);
  if (!maybeDest) {
    noGc.reset();  // Reset early, as GC data will not be used, but JsWarning
                   // can GC.
    JsWarning("Failed to allocate internal command buffer.");
    OnContextLoss(webgl::ContextLossReason::None);
    return;
  }
  const auto& destBytes = *maybeDest;
  webgl::Serialize(destBytes, id, args...);
}

// -

#define RPROC(_METHOD) \
  decltype(&HostWebGLContext::_METHOD), &HostWebGLContext::_METHOD

// ------------------------- Composition, etc -------------------------

void ClientWebGLContext::OnBeforePaintTransaction() { Present(nullptr); }

void ClientWebGLContext::EndComposition() {
  // Mark ourselves as no longer invalidated.
  MarkContextClean();
}

// -

layers::TextureType ClientWebGLContext::GetTexTypeForSwapChain() const {
  const RefPtr<layers::ImageBridgeChild> imageBridge =
      layers::ImageBridgeChild::GetSingleton();
  const bool isOutOfProcess = mNotLost && mNotLost->outOfProcess != nullptr;
  return layers::TexTypeForWebgl(imageBridge, isOutOfProcess);
}

void ClientWebGLContext::Present(WebGLFramebufferJS* const xrFb,
                                 const bool webvr,
                                 const webgl::SwapChainOptions& options) {
  const auto texType = GetTexTypeForSwapChain();
  Present(xrFb, texType, webvr, options);
}

// Fill in remote texture ids to SwapChainOptions if async present is enabled.
webgl::SwapChainOptions ClientWebGLContext::PrepareAsyncSwapChainOptions(
    WebGLFramebufferJS* fb, bool webvr,
    const webgl::SwapChainOptions& options) {
  // Currently remote texture ids should only be set internally.
  MOZ_ASSERT(!options.remoteTextureOwnerId.IsValid() &&
             !options.remoteTextureId.IsValid());
  // Async present only works when out-of-process. It is not supported in WebVR.
  // Allow it if it is either forced or if the pref is set.
  if (fb || webvr) {
    return options;
  }
  if (!IsContextLost() && !mNotLost->inProcess &&
      (options.forceAsyncPresent ||
       StaticPrefs::webgl_out_of_process_async_present())) {
    if (!mRemoteTextureOwnerId) {
      mRemoteTextureOwnerId = Some(layers::RemoteTextureOwnerId::GetNext());
    }
    mLastRemoteTextureId = Some(layers::RemoteTextureId::GetNext());
    webgl::SwapChainOptions asyncOptions = options;
    asyncOptions.remoteTextureOwnerId = *mRemoteTextureOwnerId;
    asyncOptions.remoteTextureId = *mLastRemoteTextureId;
    return asyncOptions;
  }
  // Clear the current remote texture id so that we disable async.
  mRemoteTextureOwnerId = Nothing();
  return options;
}

void ClientWebGLContext::Present(WebGLFramebufferJS* const xrFb,
                                 const layers::TextureType type,
                                 const bool webvr,
                                 const webgl::SwapChainOptions& options) {
  if (!mIsCanvasDirty && !xrFb) return;
  if (!xrFb) {
    mIsCanvasDirty = false;
  }
  CancelAutoFlush();
  webgl::SwapChainOptions asyncOptions =
      PrepareAsyncSwapChainOptions(xrFb, webvr, options);
  Run<RPROC(Present)>(xrFb ? xrFb->mId : 0, type, webvr, asyncOptions);
}

void ClientWebGLContext::CopyToSwapChain(
    WebGLFramebufferJS* const fb, const webgl::SwapChainOptions& options) {
  CancelAutoFlush();
  const auto texType = GetTexTypeForSwapChain();
  webgl::SwapChainOptions asyncOptions =
      PrepareAsyncSwapChainOptions(fb, false, options);
  Run<RPROC(CopyToSwapChain)>(fb ? fb->mId : 0, texType, asyncOptions);
}

void ClientWebGLContext::EndOfFrame() {
  CancelAutoFlush();
  Run<RPROC(EndOfFrame)>();
}

Maybe<layers::SurfaceDescriptor> ClientWebGLContext::GetFrontBuffer(
    WebGLFramebufferJS* const fb, bool vr) {
  const FuncScope funcScope(*this, "<GetFrontBuffer>");
  if (IsContextLost()) return {};

  const auto& inProcess = mNotLost->inProcess;
  if (inProcess) {
    return inProcess->GetFrontBuffer(fb ? fb->mId : 0, vr);
  }

  const auto& child = mNotLost->outOfProcess;
  child->FlushPendingCmds();

  // Always synchronously get the front buffer if not using a remote texture.
  bool needsSync = true;
  Maybe<layers::SurfaceDescriptor> syncDesc;
  Maybe<layers::SurfaceDescriptor> remoteDesc;
  auto& info = child->GetFlushedCmdInfo();

  // If valid remote texture data was set for async present, then use it.
  if (!fb && !vr && mRemoteTextureOwnerId && mLastRemoteTextureId) {
    const auto tooManyFlushes = 10;
    // If there are many flushed cmds, force synchronous IPC to avoid too many
    // pending ipc messages. Otherwise don't sync for other cases to avoid any
    // performance penalty.
    needsSync = XRE_IsParentProcess() ||
                gfx::gfxVars::WebglOopAsyncPresentForceSync() ||
                info.flushesSinceLastCongestionCheck > tooManyFlushes;

    // Only send over a remote texture descriptor if the WebGLChild actor is
    // alive to ensure the remote texture id is valid.
    if (child->CanSend()) {
      remoteDesc = Some(layers::SurfaceDescriptorRemoteTexture(
          *mLastRemoteTextureId, *mRemoteTextureOwnerId));
    }
  }

  if (needsSync &&
      !child->SendGetFrontBuffer(fb ? fb->mId : 0, vr, &syncDesc)) {
    return {};
  }

  // Reset flushesSinceLastCongestionCheck
  info.flushesSinceLastCongestionCheck = 0;
  info.congestionCheckGeneration++;

  // If there is a remote texture descriptor, use that preferentially, as the
  // sync front buffer descriptor was only created to force a sync first.
  return remoteDesc ? remoteDesc : syncDesc;
}

Maybe<layers::SurfaceDescriptor> ClientWebGLContext::PresentFrontBuffer(
    WebGLFramebufferJS* const fb, bool webvr) {
  const auto texType = GetTexTypeForSwapChain();
  Present(fb, texType, webvr);
  return GetFrontBuffer(fb, webvr);
}

already_AddRefed<layers::FwdTransactionTracker>
ClientWebGLContext::UseCompositableForwarder(
    layers::CompositableForwarder* aForwarder) {
  if (mRemoteTextureOwnerId) {
    return layers::FwdTransactionTracker::GetOrCreate(mFwdTransactionTracker);
  }
  return nullptr;
}

void ClientWebGLContext::OnDestroyChild(dom::WebGLChild* aChild) {
  // Since NotLostData may be destructing at this point, the RefPtr to
  // WebGLChild may be unreliable. Instead, it must be explicitly passed in.
  if (mRemoteTextureOwnerId && mFwdTransactionTracker &&
      mFwdTransactionTracker->IsUsed()) {
    (void)aChild->SendWaitForTxn(
        *mRemoteTextureOwnerId,
        layers::ToRemoteTextureTxnType(mFwdTransactionTracker),
        layers::ToRemoteTextureTxnId(mFwdTransactionTracker));
  }
}

void ClientWebGLContext::ClearVRSwapChain() { Run<RPROC(ClearVRSwapChain)>(); }

// -

bool ClientWebGLContext::UpdateWebRenderCanvasData(
    nsDisplayListBuilder* aBuilder, WebRenderCanvasData* aCanvasData) {
  CanvasRenderer* renderer = aCanvasData->GetCanvasRenderer();

  if (!IsContextLost() && !mResetLayer && renderer) {
    return true;
  }

  const auto& size = DrawingBufferSize();

  if (!IsContextLost() && !renderer && mNotLost->mCanvasRenderer &&
      mNotLost->mCanvasRenderer->GetSize() == gfx::IntSize(size.x, size.y) &&
      aCanvasData->SetCanvasRenderer(mNotLost->mCanvasRenderer)) {
    mNotLost->mCanvasRenderer->SetDirty();
    mResetLayer = false;
    return true;
  }

  renderer = aCanvasData->CreateCanvasRenderer();
  if (!InitializeCanvasRenderer(aBuilder, renderer)) {
    // Clear CanvasRenderer of WebRenderCanvasData
    aCanvasData->ClearCanvasRenderer();
    return false;
  }

  mNotLost->mCanvasRenderer = renderer;

  MOZ_ASSERT(renderer);
  mResetLayer = false;

  return true;
}

bool ClientWebGLContext::InitializeCanvasRenderer(
    nsDisplayListBuilder* aBuilder, CanvasRenderer* aRenderer) {
  const FuncScope funcScope(*this, "<InitializeCanvasRenderer>");
  if (IsContextLost()) return false;

  layers::CanvasRendererData data;
  data.mContext = this;
  data.mOriginPos = gl::OriginPos::BottomLeft;

  const auto& options = *mInitialOptions;
  const auto& size = DrawingBufferSize();

  if (IsContextLost()) return false;

  data.mIsOpaque = !options.alpha;
  data.mIsAlphaPremult = !options.alpha || options.premultipliedAlpha;
  data.mSize = {size.x, size.y};

  if (aBuilder->IsPaintingToWindow() && mCanvasElement) {
    data.mDoPaintCallbacks = true;
  }

  aRenderer->Initialize(data);
  aRenderer->SetDirty();
  return true;
}

void ClientWebGLContext::UpdateCanvasParameters() {
  if (!mOffscreenCanvas) {
    return;
  }

  const auto& options = *mInitialOptions;
  const auto& size = DrawingBufferSize();

  mozilla::dom::OffscreenCanvasDisplayData data;
  data.mOriginPos = gl::OriginPos::BottomLeft;
  data.mIsOpaque = !options.alpha;
  data.mIsAlphaPremult = !options.alpha || options.premultipliedAlpha;
  data.mSize = {size.x, size.y};
  data.mDoPaintCallbacks = false;

  mOffscreenCanvas->UpdateDisplayData(data);
}

layers::LayersBackend ClientWebGLContext::GetCompositorBackendType() const {
  if (mCanvasElement) {
    return mCanvasElement->GetCompositorBackendType();
  } else if (mOffscreenCanvas) {
    return mOffscreenCanvas->GetCompositorBackendType();
  }

  return layers::LayersBackend::LAYERS_NONE;
}

mozilla::dom::Document* ClientWebGLContext::GetOwnerDoc() const {
  MOZ_ASSERT(mCanvasElement);
  if (!mCanvasElement) {
    return nullptr;
  }
  return mCanvasElement->OwnerDoc();
}

void ClientWebGLContext::Commit() {
  if (mOffscreenCanvas) {
    mOffscreenCanvas->CommitFrameToCompositor();
  }
}

void ClientWebGLContext::GetCanvas(
    dom::Nullable<dom::OwningHTMLCanvasElementOrOffscreenCanvas>& retval) {
  if (mCanvasElement) {
    MOZ_RELEASE_ASSERT(!mOffscreenCanvas, "GFX: Canvas is offscreen.");

    if (mCanvasElement->IsInNativeAnonymousSubtree()) {
      retval.SetNull();
    } else {
      retval.SetValue().SetAsHTMLCanvasElement() = mCanvasElement;
    }
  } else if (mOffscreenCanvas) {
    retval.SetValue().SetAsOffscreenCanvas() = mOffscreenCanvas;
  } else {
    retval.SetNull();
  }
}

void ClientWebGLContext::SetDrawingBufferColorSpace(
    const dom::PredefinedColorSpace val) {
  mDrawingBufferColorSpace = val;
  Run<RPROC(SetDrawingBufferColorSpace)>(*mDrawingBufferColorSpace);
}

void ClientWebGLContext::SetUnpackColorSpace(
    const dom::PredefinedColorSpace val) {
  mUnpackColorSpace = val;
  Run<RPROC(SetUnpackColorSpace)>(*mUnpackColorSpace);
}

void ClientWebGLContext::GetContextAttributes(
    dom::Nullable<dom::WebGLContextAttributes>& retval) {
  retval.SetNull();
  const FuncScope funcScope(*this, "getContextAttributes");
  if (IsContextLost()) return;

  dom::WebGLContextAttributes& result = retval.SetValue();

  const auto& options = mNotLost->info.options;

  result.mAlpha.Construct(options.alpha);
  result.mDepth = options.depth;
  result.mStencil = options.stencil;
  result.mAntialias.Construct(options.antialias);
  result.mPremultipliedAlpha = options.premultipliedAlpha;
  result.mPreserveDrawingBuffer = options.preserveDrawingBuffer;
  result.mFailIfMajorPerformanceCaveat = options.failIfMajorPerformanceCaveat;
  result.mPowerPreference = options.powerPreference;
  result.mForceSoftwareRendering = options.forceSoftwareRendering;
}

// -----------------------

NS_IMETHODIMP
ClientWebGLContext::SetDimensions(const int32_t signedWidth,
                                  const int32_t signedHeight) {
  const FuncScope funcScope(*this, "<SetDimensions>");
  MOZ_ASSERT(mInitialOptions);

  if (mLossStatus != webgl::LossStatus::Ready) {
    // Attempted resize of a lost context.
    return NS_OK;
  }

  uvec2 size = {static_cast<uint32_t>(signedWidth),
                static_cast<uint32_t>(signedHeight)};
  if (!size.x) {
    size.x = 1;
  }
  if (!size.y) {
    size.y = 1;
  }
  const auto prevRequestedSize = mRequestedSize;
  mRequestedSize = size;

  mResetLayer = true;  // Always treat this as resize.

  if (mNotLost) {
    auto& state = State();

    auto curSize = prevRequestedSize;
    if (state.mDrawingBufferSize) {
      curSize = *state.mDrawingBufferSize;
    }
    if (size == curSize) return NS_OK;  // MUST skip no-op resize

    state.mDrawingBufferSize = Nothing();
    Run<RPROC(Resize)>(size);

    UpdateCanvasParameters();
    MarkCanvasDirty();
    return NS_OK;
  }

  // -
  // Context (re-)creation

  if (!CreateHostContext(size)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

void ClientWebGLContext::ResetBitmap() {
  const auto size = DrawingBufferSize();
  Run<RPROC(Resize)>(size);  // No-change resize still clears/resets everything.
}

static bool IsWebglOutOfProcessEnabled() {
  if (StaticPrefs::webgl_out_of_process_force()) {
    return true;
  }
  if (!gfx::gfxVars::AllowWebglOop()) {
    return false;
  }
  if (!NS_IsMainThread()) {
    return StaticPrefs::webgl_out_of_process_worker();
  }
  return StaticPrefs::webgl_out_of_process();
}

bool ClientWebGLContext::CreateHostContext(const uvec2& requestedSize) {
  const auto pNotLost = std::make_shared<webgl::NotLostData>(*this);
  auto& notLost = *pNotLost;

  auto res = [&]() -> Result<Ok, std::string> {
    auto options = *mInitialOptions;
    if (StaticPrefs::webgl_disable_fail_if_major_performance_caveat()) {
      options.failIfMajorPerformanceCaveat = false;
    }

    if (options.failIfMajorPerformanceCaveat) {
      const auto backend = GetCompositorBackendType();
      bool isCompositorSlow = false;
      isCompositorSlow |= (backend == layers::LayersBackend::LAYERS_WR &&
                           gfx::gfxVars::UseSoftwareWebRender());

      if (isCompositorSlow) {
        return Err(
            "failIfMajorPerformanceCaveat: Compositor is not"
            " hardware-accelerated.");
      }
    }

    const bool resistFingerprinting =
        ShouldResistFingerprinting(RFPTarget::WebGLRenderCapability);
    const auto principalKey = GetPrincipalHashValue();
    const auto initDesc = webgl::InitContextDesc{
        .isWebgl2 = mIsWebGL2,
        .resistFingerprinting = resistFingerprinting,
        .principalKey = principalKey,
        .size = requestedSize,
        .options = options,
    };

    // -

    auto useOop = IsWebglOutOfProcessEnabled();
    if (XRE_IsParentProcess()) {
      useOop = false;
    }

    if (!useOop) {
      notLost.inProcess =
          HostWebGLContext::Create({this, nullptr}, initDesc, &notLost.info);
      return Ok();
    }

    // -

    ScopedGfxFeatureReporter reporter("IpcWebGL");

    auto* const cm = gfx::CanvasManagerChild::Get();
    if (NS_WARN_IF(!cm)) {
      return Err("!CanvasManagerChild::Get()");
    }

    RefPtr<dom::WebGLChild> outOfProcess = new dom::WebGLChild(*this);
    outOfProcess =
        static_cast<dom::WebGLChild*>(cm->SendPWebGLConstructor(outOfProcess));
    if (!outOfProcess) {
      return Err("SendPWebGLConstructor failed");
    }

    // Clear RemoteTextureOwnerId. HostWebGLContext is going to be replaced in
    // WebGLParent.
    if (mRemoteTextureOwnerId.isSome()) {
      mRemoteTextureOwnerId = Nothing();
      mFwdTransactionTracker = nullptr;
    }

    if (!outOfProcess->SendInitialize(initDesc, &notLost.info)) {
      return Err("WebGL actor Initialize failed");
    }

    notLost.outOfProcess = outOfProcess;
    reporter.SetSuccessful();
    return Ok();
  }();
  if (!res.isOk()) {
    auto str = res.unwrapErr();
    if (StartsWith(str, "failIfMajorPerformanceCaveat")) {
      str +=
          " (about:config override available:"
          " webgl.disable-fail-if-major-performance-caveat)";
    }
    notLost.info.error = str;
  }
  if (!notLost.info.error->empty()) {
    ThrowEvent_WebGLContextCreationError(notLost.info.error);
    return false;
  }
  mNotLost = pNotLost;
  UpdateCanvasParameters();
  MarkCanvasDirty();

  // Init state
  const auto& limits = Limits();
  auto& state = State();
  state.mIsEnabledMap = webgl::MakeIsEnabledMap(mIsWebGL2);

  state.mDefaultTfo = new WebGLTransformFeedbackJS(*this);
  state.mDefaultVao = new WebGLVertexArrayJS(this);

  state.mBoundTfo = state.mDefaultTfo;
  state.mBoundVao = state.mDefaultVao;

  (void)state.mBoundBufferByTarget[LOCAL_GL_ARRAY_BUFFER];

  state.mTexUnits.resize(limits.maxTexUnits);
  state.mBoundUbos.resize(limits.maxUniformBufferBindings);

  {
    webgl::TypedQuad initVal;
    const float fData[4] = {0, 0, 0, 1};
    memcpy(initVal.data.data(), fData, initVal.data.size());
    state.mGenericVertexAttribs.resize(limits.maxVertexAttribs, initVal);
  }

  const auto& size = DrawingBufferSize();
  state.mViewport = {0, 0, static_cast<int32_t>(size.x),
                     static_cast<int32_t>(size.y)};
  state.mScissor = state.mViewport;

  if (mIsWebGL2) {
    // Insert keys to enable slots:
    (void)state.mBoundBufferByTarget[LOCAL_GL_COPY_READ_BUFFER];
    (void)state.mBoundBufferByTarget[LOCAL_GL_COPY_WRITE_BUFFER];
    (void)state.mBoundBufferByTarget[LOCAL_GL_PIXEL_PACK_BUFFER];
    (void)state.mBoundBufferByTarget[LOCAL_GL_PIXEL_UNPACK_BUFFER];
    (void)state.mBoundBufferByTarget[LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER];
    (void)state.mBoundBufferByTarget[LOCAL_GL_UNIFORM_BUFFER];

    (void)state.mCurrentQueryByTarget[LOCAL_GL_ANY_SAMPLES_PASSED];
    //(void)state.mCurrentQueryByTarget[LOCAL_GL_ANY_SAMPLES_PASSED_CONSERVATIVE];
    //// Same slot as ANY_SAMPLES_PASSED.
    (void)state
        .mCurrentQueryByTarget[LOCAL_GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN];
  }

  return true;
}

std::unordered_map<GLenum, bool> webgl::MakeIsEnabledMap(const bool webgl2) {
  auto ret = std::unordered_map<GLenum, bool>{};

  ret[LOCAL_GL_BLEND] = false;
  ret[LOCAL_GL_CULL_FACE] = false;
  ret[LOCAL_GL_DEPTH_TEST] = false;
  ret[LOCAL_GL_DITHER] = true;
  ret[LOCAL_GL_POLYGON_OFFSET_FILL] = false;
  ret[LOCAL_GL_SAMPLE_ALPHA_TO_COVERAGE] = false;
  ret[LOCAL_GL_SAMPLE_COVERAGE] = false;
  ret[LOCAL_GL_SCISSOR_TEST] = false;
  ret[LOCAL_GL_STENCIL_TEST] = false;

  if (webgl2) {
    ret[LOCAL_GL_RASTERIZER_DISCARD] = false;
  }

  return ret;
}

// -------

uvec2 ClientWebGLContext::DrawingBufferSize() {
  if (IsContextLost()) return {};
  const auto notLost =
      mNotLost;  // Hold a strong-ref to prevent LoseContext=>UAF.
  auto& state = State();
  auto& size = state.mDrawingBufferSize;

  if (!size) {
    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      size = Some(inProcess->DrawingBufferSize());
    } else {
      const auto& child = mNotLost->outOfProcess;
      child->FlushPendingCmds();
      uvec2 actual = {};
      if (!child->SendDrawingBufferSize(&actual)) return {};
      size = Some(actual);
    }
  }

  return *size;
}

void ClientWebGLContext::OnMemoryPressure() {
  if (IsContextLost()) return;

  const auto& inProcess = mNotLost->inProcess;
  if (inProcess) {
    return inProcess->OnMemoryPressure();
  }
  const auto& child = mNotLost->outOfProcess;
  (void)child->SendOnMemoryPressure();
}

NS_IMETHODIMP
ClientWebGLContext::SetContextOptions(JSContext* cx,
                                      JS::Handle<JS::Value> options,
                                      ErrorResult& aRvForDictionaryInit) {
  if (mInitialOptions && options.isNullOrUndefined()) return NS_OK;

  dom::WebGLContextAttributes attributes;
  if (!attributes.Init(cx, options)) {
    aRvForDictionaryInit.Throw(NS_ERROR_UNEXPECTED);
    return NS_ERROR_UNEXPECTED;
  }

  WebGLContextOptions newOpts;

  newOpts.stencil = attributes.mStencil;
  newOpts.depth = attributes.mDepth;
  newOpts.premultipliedAlpha = attributes.mPremultipliedAlpha;
  newOpts.preserveDrawingBuffer = attributes.mPreserveDrawingBuffer;
  newOpts.failIfMajorPerformanceCaveat =
      attributes.mFailIfMajorPerformanceCaveat;
  newOpts.xrCompatible = attributes.mXrCompatible;
  newOpts.powerPreference = attributes.mPowerPreference;
  newOpts.forceSoftwareRendering = attributes.mForceSoftwareRendering;
  newOpts.enableDebugRendererInfo =
      StaticPrefs::webgl_enable_debug_renderer_info();
  MOZ_ASSERT(mCanvasElement || mOffscreenCanvas);
  newOpts.shouldResistFingerprinting =
      ShouldResistFingerprinting(RFPTarget::WebGLRenderCapability);

  if (attributes.mAlpha.WasPassed()) {
    newOpts.alpha = attributes.mAlpha.Value();
  }
  if (attributes.mAntialias.WasPassed()) {
    newOpts.antialias = attributes.mAntialias.Value();
  }

  // Don't do antialiasing if we've disabled MSAA.
  if (!StaticPrefs::webgl_msaa_samples()) {
    newOpts.antialias = false;
  }

  // -

  if (mInitialOptions && *mInitialOptions != newOpts) {
    // Err if the options asked for aren't the same as what they were
    // originally.
    return NS_ERROR_FAILURE;
  }

  mXRCompatible = attributes.mXrCompatible;

  mInitialOptions.emplace(newOpts);
  return NS_OK;
}

void ClientWebGLContext::DidRefresh() { Run<RPROC(DidRefresh)>(); }

already_AddRefed<gfx::SourceSurface> ClientWebGLContext::GetSurfaceSnapshot(
    gfxAlphaType* const out_alphaType) {
  const FuncScope funcScope(*this, "<GetSurfaceSnapshot>");
  if (IsContextLost()) return nullptr;
  const auto notLost =
      mNotLost;  // Hold a strong-ref to prevent LoseContext=>UAF.

  auto ret = BackBufferSnapshot();
  if (!ret) return nullptr;

  // -

  const auto& options = mNotLost->info.options;

  auto srcAlphaType = gfxAlphaType::Opaque;
  if (options.alpha) {
    if (options.premultipliedAlpha) {
      srcAlphaType = gfxAlphaType::Premult;
    } else {
      srcAlphaType = gfxAlphaType::NonPremult;
    }
  }

  if (out_alphaType) {
    *out_alphaType = srcAlphaType;
  } else {
    // Expects Opaque or Premult
    if (srcAlphaType == gfxAlphaType::NonPremult) {
      const gfx::DataSourceSurface::ScopedMap map(
          ret, gfx::DataSourceSurface::READ_WRITE);
      MOZ_RELEASE_ASSERT(map.IsMapped(), "Failed to map snapshot surface!");

      const auto& size = ret->GetSize();
      const auto format = ret->GetFormat();
      bool rv =
          gfx::PremultiplyData(map.GetData(), map.GetStride(), format,
                               map.GetData(), map.GetStride(), format, size);
      MOZ_RELEASE_ASSERT(rv, "PremultiplyData failed!");
    }
  }

  return ret.forget();
}

mozilla::ipc::IProtocol* ClientWebGLContext::SupportsSnapshotExternalCanvas()
    const {
  return GetChild();
}

RefPtr<gfx::SourceSurface> ClientWebGLContext::GetFrontBufferSnapshot(
    const bool requireAlphaPremult) {
  const FuncScope funcScope(*this, "<GetSurfaceSnapshot>");
  if (IsContextLost()) return nullptr;
  const auto notLost =
      mNotLost;  // Hold a strong-ref to prevent LoseContext=>UAF.

  const auto& options = mNotLost->info.options;

  const auto surfFormat = options.alpha ? gfx::SurfaceFormat::B8G8R8A8
                                        : gfx::SurfaceFormat::B8G8R8X8;

  const auto fnNewSurf = [&](const uvec2 size) {
    const auto stride = size.x * 4;
    return RefPtr<gfx::DataSourceSurface>(
        gfx::Factory::CreateDataSourceSurfaceWithStride({size.x, size.y},
                                                        surfFormat, stride,
                                                        /*zero=*/true));
  };

  const auto& inProcess = mNotLost->inProcess;
  if (inProcess) {
    const auto maybeSize = inProcess->FrontBufferSnapshotInto({});
    if (!maybeSize) return nullptr;
    const auto& surfSize = *maybeSize;
    const auto stride = surfSize.x * 4;
    const auto byteSize = stride * surfSize.y;
    const auto surf = fnNewSurf(surfSize);
    if (!surf) return nullptr;
    {
      const gfx::DataSourceSurface::ScopedMap map(
          surf, gfx::DataSourceSurface::READ_WRITE);
      if (!map.IsMapped()) {
        MOZ_ASSERT(false);
        return nullptr;
      }
      MOZ_RELEASE_ASSERT(map.GetStride() == static_cast<int64_t>(stride));
      auto range = Range<uint8_t>{map.GetData(), byteSize};
      if (!inProcess->FrontBufferSnapshotInto(Some(range))) {
        gfxCriticalNote << "ClientWebGLContext::GetFrontBufferSnapshot: "
                           "FrontBufferSnapshotInto(some) failed after "
                           "FrontBufferSnapshotInto(none)";
        return nullptr;
      }
      if (requireAlphaPremult && options.alpha && !options.premultipliedAlpha) {
        bool rv = gfx::PremultiplyData(
            map.GetData(), map.GetStride(), gfx::SurfaceFormat::R8G8B8A8,
            map.GetData(), map.GetStride(), gfx::SurfaceFormat::B8G8R8A8,
            surf->GetSize());
        MOZ_RELEASE_ASSERT(rv, "PremultiplyData failed!");
      } else {
        bool rv = gfx::SwizzleData(
            map.GetData(), map.GetStride(), gfx::SurfaceFormat::R8G8B8A8,
            map.GetData(), map.GetStride(), gfx::SurfaceFormat::B8G8R8A8,
            surf->GetSize());
        MOZ_RELEASE_ASSERT(rv, "SwizzleData failed!");
      }
    }
    return surf;
  }
  const auto& child = mNotLost->outOfProcess;
  child->FlushPendingCmds();
  webgl::FrontBufferSnapshotIpc res;
  if (!child->SendGetFrontBufferSnapshot(&res)) {
    res = {};
  }
  if (!res.shmem) return nullptr;

  const auto& surfSize = res.surfSize;
  const webgl::RaiiShmem shmem{child, res.shmem.ref()};
  if (!shmem) return nullptr;
  const auto& shmemBytes = shmem.ByteRange();
  if (!surfSize.x) return nullptr;  // Zero means failure.

  const auto stride = surfSize.x * 4;
  const auto byteSize = stride * surfSize.y;

  const auto surf = fnNewSurf(surfSize);
  if (!surf) return nullptr;

  {
    const gfx::DataSourceSurface::ScopedMap map(
        surf, gfx::DataSourceSurface::READ_WRITE);
    if (!map.IsMapped()) {
      MOZ_ASSERT(false);
      return nullptr;
    }
    MOZ_RELEASE_ASSERT(shmemBytes.length() == byteSize);
    if (requireAlphaPremult && options.alpha && !options.premultipliedAlpha) {
      bool rv = gfx::PremultiplyData(
          shmemBytes.begin().get(), stride, gfx::SurfaceFormat::R8G8B8A8,
          map.GetData(), map.GetStride(), gfx::SurfaceFormat::B8G8R8A8,
          surf->GetSize());
      MOZ_RELEASE_ASSERT(rv, "PremultiplyData failed!");
    } else {
      bool rv = gfx::SwizzleData(shmemBytes.begin().get(), stride,
                                 gfx::SurfaceFormat::R8G8B8A8, map.GetData(),
                                 map.GetStride(), gfx::SurfaceFormat::B8G8R8A8,
                                 surf->GetSize());
      MOZ_RELEASE_ASSERT(rv, "SwizzleData failed!");
    }
  }
  return surf;
}

RefPtr<gfx::DataSourceSurface> ClientWebGLContext::BackBufferSnapshot() {
  if (IsContextLost()) return nullptr;
  const auto notLost =
      mNotLost;  // Hold a strong-ref to prevent LoseContext=>UAF.

  const auto& options = mNotLost->info.options;
  const auto& state = State();

  const auto drawFbWas = state.mBoundDrawFb;
  const auto readFbWas = state.mBoundReadFb;
  const auto pboWas =
      Find(state.mBoundBufferByTarget, LOCAL_GL_PIXEL_PACK_BUFFER);

  const auto size = DrawingBufferSize();

  // -

  BindFramebuffer(LOCAL_GL_FRAMEBUFFER, nullptr);
  if (pboWas) {
    BindBuffer(LOCAL_GL_PIXEL_PACK_BUFFER, nullptr);
  }

  auto reset = MakeScopeExit([&] {
    if (drawFbWas == readFbWas) {
      BindFramebuffer(LOCAL_GL_FRAMEBUFFER, drawFbWas);
    } else {
      BindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, drawFbWas);
      BindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, readFbWas);
    }
    if (pboWas) {
      BindBuffer(LOCAL_GL_PIXEL_PACK_BUFFER, pboWas);
    }
  });

  const auto surfFormat = options.alpha ? gfx::SurfaceFormat::B8G8R8A8
                                        : gfx::SurfaceFormat::B8G8R8X8;
  const auto stride = size.x * 4;
  RefPtr<gfx::DataSourceSurface> surf =
      gfx::Factory::CreateDataSourceSurfaceWithStride(
          {size.x, size.y}, surfFormat, stride, /*zero=*/true);
  if (NS_WARN_IF(!surf)) {
    // Was this an OOM or alloc-limit? (500MB is our default resource size
    // limit)
    surf = gfx::Factory::CreateDataSourceSurfaceWithStride({1, 1}, surfFormat,
                                                           4, /*zero=*/true);
    if (!surf) {
      // Still failed for a 1x1 size.
      gfxCriticalError() << "CreateDataSourceSurfaceWithStride(surfFormat="
                         << surfFormat << ") failed.";
    }
    return nullptr;
  }

  {
    const gfx::DataSourceSurface::ScopedMap map(
        surf, gfx::DataSourceSurface::READ_WRITE);
    if (!map.IsMapped()) {
      MOZ_ASSERT(false);
      return nullptr;
    }
    MOZ_ASSERT(static_cast<uint32_t>(map.GetStride()) == stride);

    const auto desc = webgl::ReadPixelsDesc{{0, 0}, size};
    const auto pixels = Span<uint8_t>(map.GetData(), stride * size.y);
    if (!DoReadPixels(desc, pixels)) return nullptr;

    // RGBA->BGRA and flip-y.
    MOZ_RELEASE_ASSERT(gfx::SwizzleYFlipData(
        pixels.data(), stride, gfx::SurfaceFormat::R8G8B8A8, pixels.data(),
        stride, gfx::SurfaceFormat::B8G8R8A8, {size.x, size.y}));
  }

  return surf;
}

UniquePtr<uint8_t[]> ClientWebGLContext::GetImageBuffer(
    int32_t* out_format, gfx::IntSize* out_imageSize) {
  *out_format = 0;
  *out_imageSize = {};

  // Use GetSurfaceSnapshot() to make sure that appropriate y-flip gets applied
  gfxAlphaType any;
  RefPtr<gfx::SourceSurface> snapshot = GetSurfaceSnapshot(&any);
  if (!snapshot) return nullptr;

  RefPtr<gfx::DataSourceSurface> dataSurface = snapshot->GetDataSurface();

  const auto& premultAlpha = mNotLost->info.options.premultipliedAlpha;
  *out_imageSize = dataSurface->GetSize();

  if (ShouldResistFingerprinting(RFPTarget::CanvasRandomization)) {
    return gfxUtils::GetImageBufferWithRandomNoise(
        dataSurface, premultAlpha, GetCookieJarSettings(), out_format);
  }

  return gfxUtils::GetImageBuffer(dataSurface, premultAlpha, out_format);
}

NS_IMETHODIMP
ClientWebGLContext::GetInputStream(const char* mimeType,
                                   const nsAString& encoderOptions,
                                   nsIInputStream** out_stream) {
  // Use GetSurfaceSnapshot() to make sure that appropriate y-flip gets applied
  gfxAlphaType any;
  RefPtr<gfx::SourceSurface> snapshot = GetSurfaceSnapshot(&any);
  if (!snapshot) return NS_ERROR_FAILURE;

  RefPtr<gfx::DataSourceSurface> dataSurface = snapshot->GetDataSurface();
  const auto& premultAlpha = mNotLost->info.options.premultipliedAlpha;

  if (ShouldResistFingerprinting(RFPTarget::CanvasRandomization)) {
    return gfxUtils::GetInputStreamWithRandomNoise(
        dataSurface, premultAlpha, mimeType, encoderOptions,
        GetCookieJarSettings(), out_stream);
  }

  return gfxUtils::GetInputStream(dataSurface, premultAlpha, mimeType,
                                  encoderOptions, out_stream);
}

// ------------------------- Client WebGL Objects -------------------------
// ------------------------- Create/Destroy/Is -------------------------

template <typename T>
static already_AddRefed<T> AsAddRefed(T* ptr) {
  RefPtr<T> rp = ptr;
  return rp.forget();
}

template <typename T>
static RefPtr<T> AsRefPtr(T* ptr) {
  return {ptr};
}

already_AddRefed<WebGLBufferJS> ClientWebGLContext::CreateBuffer() const {
  const FuncScope funcScope(*this, "createBuffer");

  auto ret = AsRefPtr(new WebGLBufferJS(*this));
  Run<RPROC(CreateBuffer)>(ret->mId);
  return ret.forget();
}

already_AddRefed<WebGLFramebufferJS> ClientWebGLContext::CreateFramebuffer()
    const {
  const FuncScope funcScope(*this, "createFramebuffer");

  auto ret = AsRefPtr(new WebGLFramebufferJS(*this));
  Run<RPROC(CreateFramebuffer)>(ret->mId);
  return ret.forget();
}

already_AddRefed<WebGLFramebufferJS>
ClientWebGLContext::CreateOpaqueFramebuffer(
    const webgl::OpaqueFramebufferOptions& options) const {
  const FuncScope funcScope(*this, "createOpaqueFramebuffer");

  auto ret = AsRefPtr(new WebGLFramebufferJS(*this, true));

  if (mNotLost) {
    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      if (!inProcess->CreateOpaqueFramebuffer(ret->mId, options)) {
        ret = nullptr;
      }
      return ret.forget();
    }
    const auto& child = mNotLost->outOfProcess;
    child->FlushPendingCmds();
    bool ok = false;
    if (!child->SendCreateOpaqueFramebuffer(ret->mId, options, &ok))
      return nullptr;
    if (!ok) return nullptr;
  }

  return ret.forget();
}

already_AddRefed<WebGLProgramJS> ClientWebGLContext::CreateProgram() const {
  const FuncScope funcScope(*this, "createProgram");

  auto ret = AsRefPtr(new WebGLProgramJS(*this));
  Run<RPROC(CreateProgram)>(ret->mId);
  return ret.forget();
}

already_AddRefed<WebGLQueryJS> ClientWebGLContext::CreateQuery() const {
  const FuncScope funcScope(*this, "createQuery");

  auto ret = AsRefPtr(new WebGLQueryJS(this));
  Run<RPROC(CreateQuery)>(ret->mId);
  return ret.forget();
}

already_AddRefed<WebGLRenderbufferJS> ClientWebGLContext::CreateRenderbuffer()
    const {
  const FuncScope funcScope(*this, "createRenderbuffer");

  auto ret = AsRefPtr(new WebGLRenderbufferJS(*this));
  Run<RPROC(CreateRenderbuffer)>(ret->mId);
  return ret.forget();
}

already_AddRefed<WebGLSamplerJS> ClientWebGLContext::CreateSampler() const {
  const FuncScope funcScope(*this, "createSampler");

  auto ret = AsRefPtr(new WebGLSamplerJS(*this));
  Run<RPROC(CreateSampler)>(ret->mId);
  return ret.forget();
}

already_AddRefed<WebGLShaderJS> ClientWebGLContext::CreateShader(
    const GLenum type) const {
  const FuncScope funcScope(*this, "createShader");

  switch (type) {
    case LOCAL_GL_VERTEX_SHADER:
    case LOCAL_GL_FRAGMENT_SHADER:
      break;
    default:
      EnqueueError_ArgEnum("type", type);
      return nullptr;
  }

  auto ret = AsRefPtr(new WebGLShaderJS(*this, type));
  Run<RPROC(CreateShader)>(ret->mId, ret->mType);
  return ret.forget();
}

already_AddRefed<WebGLSyncJS> ClientWebGLContext::FenceSync(
    const GLenum condition, const GLbitfield flags) const {
  const FuncScope funcScope(*this, "fenceSync");

  if (condition != LOCAL_GL_SYNC_GPU_COMMANDS_COMPLETE) {
    EnqueueError_ArgEnum("condition", condition);
    return nullptr;
  }

  if (flags) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`flags` must be 0.");
    return nullptr;
  }

  auto ret = AsRefPtr(new WebGLSyncJS(*this));
  Run<RPROC(CreateSync)>(ret->mId);

  auto& availRunnable = EnsureAvailabilityRunnable();
  availRunnable.mSyncs.push_back(ret.get());
  ret->mCanBeAvailable = false;

  AutoEnqueueFlush();

  return ret.forget();
}

already_AddRefed<WebGLTextureJS> ClientWebGLContext::CreateTexture() const {
  const FuncScope funcScope(*this, "createTexture");

  auto ret = AsRefPtr(new WebGLTextureJS(*this));
  Run<RPROC(CreateTexture)>(ret->mId);
  return ret.forget();
}

already_AddRefed<WebGLTransformFeedbackJS>
ClientWebGLContext::CreateTransformFeedback() const {
  const FuncScope funcScope(*this, "createTransformFeedback");

  auto ret = AsRefPtr(new WebGLTransformFeedbackJS(*this));
  Run<RPROC(CreateTransformFeedback)>(ret->mId);
  return ret.forget();
}

already_AddRefed<WebGLVertexArrayJS> ClientWebGLContext::CreateVertexArray()
    const {
  const FuncScope funcScope(*this, "createVertexArray");

  auto ret = AsRefPtr(new WebGLVertexArrayJS(this));
  Run<RPROC(CreateVertexArray)>(ret->mId);
  return ret.forget();
}

// -

static bool ValidateOrSkipForDelete(const ClientWebGLContext& context,
                                    const webgl::ObjectJS* const obj) {
  if (!obj) return false;
  if (!obj->ValidateForContext(context, "obj")) return false;
  if (obj->IsDeleted()) return false;
  return true;
}

void ClientWebGLContext::DeleteBuffer(WebGLBufferJS* const obj) {
  const FuncScope funcScope(*this, "deleteBuffer");
  if (IsContextLost()) return;
  if (!ValidateOrSkipForDelete(*this, obj)) return;
  auto& state = State();

  // Unbind from all bind points and bound containers

  // UBOs
  for (const auto i : IntegerRange(state.mBoundUbos.size())) {
    if (state.mBoundUbos[i] == obj) {
      BindBufferBase(LOCAL_GL_UNIFORM_BUFFER, i, nullptr);
    }
  }

  // TFO only if not active
  if (!state.mBoundTfo->mActiveOrPaused) {
    const auto& buffers = state.mBoundTfo->mAttribBuffers;
    for (const auto i : IntegerRange(buffers.size())) {
      if (buffers[i] == obj) {
        BindBufferBase(LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER, i, nullptr);
      }
    }
  }

  // Generic/global bind points
  for (const auto& pair : state.mBoundBufferByTarget) {
    if (pair.second == obj) {
      BindBuffer(pair.first, nullptr);
    }
  }

  // VAO attachments
  if (state.mBoundVao->mIndexBuffer == obj) {
    BindBuffer(LOCAL_GL_ELEMENT_ARRAY_BUFFER, nullptr);
  }

  const auto& vaoBuffers = state.mBoundVao->mAttribBuffers;
  Maybe<WebGLBufferJS*> toRestore;
  for (const auto i : IntegerRange(vaoBuffers.size())) {
    if (vaoBuffers[i] == obj) {
      if (!toRestore) {
        toRestore =
            Some(state.mBoundBufferByTarget[LOCAL_GL_ARRAY_BUFFER].get());
        if (*toRestore) {
          BindBuffer(LOCAL_GL_ARRAY_BUFFER, nullptr);
        }
      }
      VertexAttribPointer(i, 4, LOCAL_GL_FLOAT, false, 0, 0);
    }
  }
  if (toRestore && *toRestore) {
    BindBuffer(LOCAL_GL_ARRAY_BUFFER, *toRestore);
  }

  // -

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteBuffer)>(obj->mId);
}

void ClientWebGLContext::DeleteFramebuffer(WebGLFramebufferJS* const obj,
                                           bool canDeleteOpaque) {
  const FuncScope funcScope(*this, "deleteFramebuffer");
  if (IsContextLost()) return;
  if (!ValidateOrSkipForDelete(*this, obj)) return;
  if (!canDeleteOpaque && obj->mOpaque) {
    EnqueueError(
        LOCAL_GL_INVALID_OPERATION,
        "An opaque framebuffer's attachments cannot be inspected or changed.");
    return;
  }
  const auto& state = State();

  // Unbind
  const auto fnDetach = [&](const GLenum target,
                            const WebGLFramebufferJS* const fb) {
    if (obj == fb) {
      BindFramebuffer(target, nullptr);
    }
  };
  if (state.mBoundDrawFb == state.mBoundReadFb) {
    fnDetach(LOCAL_GL_FRAMEBUFFER, state.mBoundDrawFb.get());
  } else {
    fnDetach(LOCAL_GL_DRAW_FRAMEBUFFER, state.mBoundDrawFb.get());
    fnDetach(LOCAL_GL_READ_FRAMEBUFFER, state.mBoundReadFb.get());
  }

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteFramebuffer)>(obj->mId);
}

void ClientWebGLContext::DeleteProgram(WebGLProgramJS* const obj) const {
  const FuncScope funcScope(*this, "deleteProgram");
  if (IsContextLost()) return;
  if (!ValidateOrSkipForDelete(*this, obj)) return;

  // Don't unbind

  obj->mKeepAlive = nullptr;
}

webgl::ProgramKeepAlive::~ProgramKeepAlive() {
  if (!mParent) return;
  const auto& context = mParent->Context();
  if (!context) return;
  context->DoDeleteProgram(*mParent);
}

void ClientWebGLContext::DoDeleteProgram(WebGLProgramJS& obj) const {
  obj.mNextLink_Shaders = {};
  Run<RPROC(DeleteProgram)>(obj.mId);
}

static GLenum QuerySlotTarget(const GLenum specificTarget);

void ClientWebGLContext::DeleteQuery(WebGLQueryJS* const obj) {
  const FuncScope funcScope(*this, "deleteQuery");
  if (IsContextLost()) return;
  if (!ValidateOrSkipForDelete(*this, obj)) return;
  const auto& state = State();

  // Unbind if current

  if (obj->mTarget) {
    // Despite mTarget being set, we may not have called BeginQuery on this
    // object. QueryCounter may also set mTarget.
    const auto slotTarget = QuerySlotTarget(obj->mTarget);
    const auto curForTarget =
        MaybeFind(state.mCurrentQueryByTarget, slotTarget);

    if (curForTarget && *curForTarget == obj) {
      EndQuery(obj->mTarget);
    }
  }

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteQuery)>(obj->mId);
}

void ClientWebGLContext::DeleteRenderbuffer(WebGLRenderbufferJS* const obj) {
  const FuncScope funcScope(*this, "deleteRenderbuffer");
  if (IsContextLost()) return;
  if (!ValidateOrSkipForDelete(*this, obj)) return;
  const auto& state = State();

  // Unbind
  if (state.mBoundRb == obj) {
    BindRenderbuffer(LOCAL_GL_RENDERBUFFER, nullptr);
  }

  // Unbind from bound FBs
  const auto fnDetach = [&](const GLenum target,
                            const WebGLFramebufferJS* const fb) {
    if (!fb) return;
    for (const auto& pair : fb->mAttachments) {
      if (pair.second.rb == obj) {
        FramebufferRenderbuffer(target, pair.first, LOCAL_GL_RENDERBUFFER,
                                nullptr);
      }
    }
  };
  if (state.mBoundDrawFb == state.mBoundReadFb) {
    fnDetach(LOCAL_GL_FRAMEBUFFER, state.mBoundDrawFb.get());
  } else {
    fnDetach(LOCAL_GL_DRAW_FRAMEBUFFER, state.mBoundDrawFb.get());
    fnDetach(LOCAL_GL_READ_FRAMEBUFFER, state.mBoundReadFb.get());
  }

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteRenderbuffer)>(obj->mId);
}

void ClientWebGLContext::DeleteSampler(WebGLSamplerJS* const obj) {
  const FuncScope funcScope(*this, "deleteSampler");
  if (IsContextLost()) return;
  if (!ValidateOrSkipForDelete(*this, obj)) return;
  const auto& state = State();

  // Unbind
  for (const auto i : IntegerRange(state.mTexUnits.size())) {
    if (state.mTexUnits[i].sampler == obj) {
      BindSampler(i, nullptr);
    }
  }

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteSampler)>(obj->mId);
}

void ClientWebGLContext::DeleteShader(WebGLShaderJS* const obj) const {
  const FuncScope funcScope(*this, "deleteShader");
  if (IsContextLost()) return;
  if (!ValidateOrSkipForDelete(*this, obj)) return;

  // Don't unbind

  obj->mKeepAlive = nullptr;
}

webgl::ShaderKeepAlive::~ShaderKeepAlive() {
  if (!mParent) return;
  const auto& context = mParent->Context();
  if (!context) return;
  context->DoDeleteShader(*mParent);
}

void ClientWebGLContext::DoDeleteShader(const WebGLShaderJS& obj) const {
  Run<RPROC(DeleteShader)>(obj.mId);
}

void ClientWebGLContext::DeleteSync(WebGLSyncJS* const obj) const {
  const FuncScope funcScope(*this, "deleteSync");
  if (IsContextLost()) return;
  if (!ValidateOrSkipForDelete(*this, obj)) return;

  // Nothing to unbind

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteSync)>(obj->mId);
}

void ClientWebGLContext::DeleteTexture(WebGLTextureJS* const obj) {
  const FuncScope funcScope(*this, "deleteTexture");
  if (IsContextLost()) return;
  if (!ValidateOrSkipForDelete(*this, obj)) return;
  auto& state = State();

  // Unbind
  const auto& target = obj->mTarget;
  if (target) {
    // Unbind from tex units
    Maybe<uint32_t> restoreTexUnit;
    for (const auto i : IntegerRange(state.mTexUnits.size())) {
      if (state.mTexUnits[i].texByTarget[target] == obj) {
        if (!restoreTexUnit) {
          restoreTexUnit = Some(state.mActiveTexUnit);
        }
        ActiveTexture(LOCAL_GL_TEXTURE0 + i);
        BindTexture(target, nullptr);
      }
    }
    if (restoreTexUnit) {
      ActiveTexture(LOCAL_GL_TEXTURE0 + *restoreTexUnit);
    }

    // Unbind from bound FBs
    const auto fnDetach = [&](const GLenum target,
                              const WebGLFramebufferJS* const fb) {
      if (!fb) return;
      for (const auto& pair : fb->mAttachments) {
        if (pair.second.tex == obj) {
          FramebufferRenderbuffer(target, pair.first, LOCAL_GL_RENDERBUFFER,
                                  nullptr);
        }
      }
    };
    if (state.mBoundDrawFb == state.mBoundReadFb) {
      fnDetach(LOCAL_GL_FRAMEBUFFER, state.mBoundDrawFb.get());
    } else {
      fnDetach(LOCAL_GL_DRAW_FRAMEBUFFER, state.mBoundDrawFb.get());
      fnDetach(LOCAL_GL_READ_FRAMEBUFFER, state.mBoundReadFb.get());
    }
  }

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteTexture)>(obj->mId);
}

void ClientWebGLContext::DeleteTransformFeedback(
    WebGLTransformFeedbackJS* const obj) {
  const FuncScope funcScope(*this, "deleteTransformFeedback");
  if (IsContextLost()) return;
  if (!ValidateOrSkipForDelete(*this, obj)) return;
  const auto& state = State();

  if (obj->mActiveOrPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Transform Feedback object still active or paused.");
    return;
  }

  // Unbind
  if (state.mBoundTfo == obj) {
    BindTransformFeedback(LOCAL_GL_TRANSFORM_FEEDBACK, nullptr);
  }

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteTransformFeedback)>(obj->mId);
}

void ClientWebGLContext::DeleteVertexArray(WebGLVertexArrayJS* const obj) {
  const FuncScope funcScope(*this, "deleteVertexArray");
  if (IsContextLost()) return;
  if (!ValidateOrSkipForDelete(*this, obj)) return;
  const auto& state = State();

  // Unbind
  if (state.mBoundVao == obj) {
    BindVertexArray(nullptr);
  }

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteVertexArray)>(obj->mId);
}

// -

bool ClientWebGLContext::IsBuffer(const WebGLBufferJS* const obj) const {
  const FuncScope funcScope(*this, "isBuffer");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) &&
         obj->mKind != webgl::BufferKind::Undefined;
}

bool ClientWebGLContext::IsFramebuffer(
    const WebGLFramebufferJS* const obj) const {
  const FuncScope funcScope(*this, "isFramebuffer");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) && obj->mHasBeenBound;
}

bool ClientWebGLContext::IsProgram(const WebGLProgramJS* const obj) const {
  const FuncScope funcScope(*this, "isProgram");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this);
}

bool ClientWebGLContext::IsQuery(const WebGLQueryJS* const obj) const {
  const FuncScope funcScope(*this, "isQuery");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) && obj->mTarget;
}

bool ClientWebGLContext::IsRenderbuffer(
    const WebGLRenderbufferJS* const obj) const {
  const FuncScope funcScope(*this, "isRenderbuffer");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) && obj->mHasBeenBound;
}

bool ClientWebGLContext::IsSampler(const WebGLSamplerJS* const obj) const {
  const FuncScope funcScope(*this, "isSampler");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this);
}

bool ClientWebGLContext::IsShader(const WebGLShaderJS* const obj) const {
  const FuncScope funcScope(*this, "isShader");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this);
}

bool ClientWebGLContext::IsSync(const WebGLSyncJS* const obj) const {
  const FuncScope funcScope(*this, "isSync");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this);
}

bool ClientWebGLContext::IsTexture(const WebGLTextureJS* const obj) const {
  const FuncScope funcScope(*this, "isTexture");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) && obj->mTarget;
}

bool ClientWebGLContext::IsTransformFeedback(
    const WebGLTransformFeedbackJS* const obj) const {
  const FuncScope funcScope(*this, "isTransformFeedback");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) && obj->mHasBeenBound;
}

bool ClientWebGLContext::IsVertexArray(
    const WebGLVertexArrayJS* const obj) const {
  const FuncScope funcScope(*this, "isVertexArray");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) && obj->mHasBeenBound;
}

// ------------------------- GL State -------------------------

void ClientWebGLContext::SetEnabledI(const GLenum cap, const Maybe<GLuint> i,
                                     const bool val) const {
  const FuncScope funcScope(*this, "enable/disable");
  if (IsContextLost()) return;

  auto& map = mNotLost->state.mIsEnabledMap;
  auto slot = MaybeFind(map, cap);
  if (i && cap != LOCAL_GL_BLEND) {
    slot = nullptr;
  }
  if (!slot) {
    EnqueueError_ArgEnum("cap", cap);
    return;
  }

  Run<RPROC(SetEnabled)>(cap, i, val);

  if (!i || *i == 0) {
    *slot = val;
  }
}

bool ClientWebGLContext::IsEnabled(const GLenum cap) const {
  const FuncScope funcScope(*this, "isEnabled");
  if (IsContextLost()) return false;

  const auto& map = mNotLost->state.mIsEnabledMap;
  const auto slot = MaybeFind(map, cap);
  if (!slot) {
    EnqueueError_ArgEnum("cap", cap);
    return false;
  }

  return *slot;
}

template <typename T, typename S>
static JS::Value Create(JSContext* cx, nsWrapperCache* creator, const S& src,
                        ErrorResult& rv) {
  return JS::ObjectOrNullValue(T::Create(cx, creator, src, rv));
}

void ClientWebGLContext::GetInternalformatParameter(
    JSContext* cx, GLenum target, GLenum internalformat, GLenum pname,
    JS::MutableHandle<JS::Value> retval, ErrorResult& rv) {
  const FuncScope funcScope(*this, "getInternalformatParameter");
  retval.set(JS::NullValue());
  const auto notLost =
      mNotLost;  // Hold a strong-ref to prevent LoseContext=>UAF.
  if (IsContextLost()) return;

  const auto& inProcessContext = notLost->inProcess;
  Maybe<std::vector<int32_t>> maybe;
  if (inProcessContext) {
    maybe = inProcessContext->GetInternalformatParameter(target, internalformat,
                                                         pname);
  } else {
    const auto& child = notLost->outOfProcess;
    child->FlushPendingCmds();
    if (!child->SendGetInternalformatParameter(target, internalformat, pname,
                                               &maybe)) {
      return;
    }
  }

  if (!maybe) {
    return;
  }

  retval.set(Create<dom::Int32Array>(cx, this, *maybe, rv));
}

static JS::Value StringValue(JSContext* cx, const std::string& str,
                             ErrorResult& er) {
  JSString* jsStr = JS_NewStringCopyN(cx, str.data(), str.size());
  if (!jsStr) {
    er.Throw(NS_ERROR_OUT_OF_MEMORY);
    return JS::NullValue();
  }

  return JS::StringValue(jsStr);
}

template <typename T>
bool ToJSValueOrNull(JSContext* const cx, const RefPtr<T>& ptr,
                     JS::MutableHandle<JS::Value> retval) {
  if (!ptr) {
    retval.set(JS::NullValue());
    return true;
  }
  return dom::ToJSValue(cx, ptr, retval);
}

Maybe<double> ClientWebGLContext::GetNumber(const GLenum pname) {
  MOZ_ASSERT(!IsContextLost());

  const auto& inProcess = mNotLost->inProcess;
  if (inProcess) {
    return inProcess->GetNumber(pname);
  }

  const auto& child = mNotLost->outOfProcess;
  child->FlushPendingCmds();

  Maybe<double> ret;
  if (!child->SendGetNumber(pname, &ret)) {
    ret.reset();
  }
  return ret;
}

Maybe<std::string> ClientWebGLContext::GetString(const GLenum pname) {
  MOZ_ASSERT(!IsContextLost());

  const auto& inProcess = mNotLost->inProcess;
  if (inProcess) {
    return inProcess->GetString(pname);
  }

  const auto& child = mNotLost->outOfProcess;
  child->FlushPendingCmds();

  Maybe<std::string> ret;
  if (!child->SendGetString(pname, &ret)) {
    ret.reset();
  }
  return ret;
}

void ClientWebGLContext::GetParameter(JSContext* cx, GLenum pname,
                                      JS::MutableHandle<JS::Value> retval,
                                      ErrorResult& rv, const bool debug) {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getParameter");
  if (IsContextLost()) return;
  const auto& limits = Limits();
  const auto& state = State();

  // -

  const auto fnSetRetval_Buffer = [&](const GLenum target) {
    const auto buffer = *MaybeFind(state.mBoundBufferByTarget, target);
    (void)ToJSValueOrNull(cx, buffer, retval);
  };
  const auto fnSetRetval_Tex = [&](const GLenum texTarget) {
    const auto& texUnit = state.mTexUnits[state.mActiveTexUnit];
    const auto tex = Find(texUnit.texByTarget, texTarget, nullptr);
    (void)ToJSValueOrNull(cx, tex, retval);
  };

  switch (pname) {
    case LOCAL_GL_ARRAY_BUFFER_BINDING:
      fnSetRetval_Buffer(LOCAL_GL_ARRAY_BUFFER);
      return;

    case LOCAL_GL_CURRENT_PROGRAM:
      (void)ToJSValueOrNull(cx, state.mCurrentProgram, retval);
      return;

    case LOCAL_GL_ELEMENT_ARRAY_BUFFER_BINDING:
      (void)ToJSValueOrNull(cx, state.mBoundVao->mIndexBuffer, retval);
      return;

    case LOCAL_GL_FRAMEBUFFER_BINDING:
      (void)ToJSValueOrNull(cx, state.mBoundDrawFb, retval);
      return;

    case LOCAL_GL_RENDERBUFFER_BINDING:
      (void)ToJSValueOrNull(cx, state.mBoundRb, retval);
      return;

    case LOCAL_GL_TEXTURE_BINDING_2D:
      fnSetRetval_Tex(LOCAL_GL_TEXTURE_2D);
      return;

    case LOCAL_GL_TEXTURE_BINDING_CUBE_MAP:
      fnSetRetval_Tex(LOCAL_GL_TEXTURE_CUBE_MAP);
      return;

    case LOCAL_GL_VERTEX_ARRAY_BINDING: {
      if (!mIsWebGL2 &&
          !IsExtensionEnabled(WebGLExtensionID::OES_vertex_array_object))
        break;

      auto ret = state.mBoundVao;
      if (ret == state.mDefaultVao) {
        ret = nullptr;
      }
      (void)ToJSValueOrNull(cx, ret, retval);
      return;
    }

    case LOCAL_GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
      retval.set(JS::NumberValue(limits.maxTexUnits));
      return;
    case LOCAL_GL_MAX_TEXTURE_SIZE:
      retval.set(JS::NumberValue(limits.maxTex2dSize));
      return;
    case LOCAL_GL_MAX_CUBE_MAP_TEXTURE_SIZE:
      retval.set(JS::NumberValue(limits.maxTexCubeSize));
      return;
    case LOCAL_GL_MAX_VERTEX_ATTRIBS:
      retval.set(JS::NumberValue(limits.maxVertexAttribs));
      return;

    case LOCAL_GL_MAX_VIEWS_OVR:
      if (IsExtensionEnabled(WebGLExtensionID::OVR_multiview2)) {
        retval.set(JS::NumberValue(limits.maxMultiviewLayers));
        return;
      }
      break;

    case LOCAL_GL_PACK_ALIGNMENT:
      retval.set(JS::NumberValue(state.mPixelPackState.alignmentInTypeElems));
      return;
    case LOCAL_GL_UNPACK_ALIGNMENT:
      retval.set(JS::NumberValue(state.mPixelUnpackState.alignmentInTypeElems));
      return;

    case dom::WebGLRenderingContext_Binding::UNPACK_FLIP_Y_WEBGL:
      retval.set(JS::BooleanValue(state.mPixelUnpackState.flipY));
      return;
    case dom::WebGLRenderingContext_Binding::UNPACK_PREMULTIPLY_ALPHA_WEBGL:
      retval.set(JS::BooleanValue(state.mPixelUnpackState.premultiplyAlpha));
      return;
    case dom::WebGLRenderingContext_Binding::UNPACK_COLORSPACE_CONVERSION_WEBGL:
      retval.set(JS::NumberValue(state.mPixelUnpackState.colorspaceConversion));
      return;

    case dom::WEBGL_provoking_vertex_Binding::PROVOKING_VERTEX_WEBGL:
      if (!IsExtensionEnabled(WebGLExtensionID::WEBGL_provoking_vertex)) break;
      retval.set(JS::NumberValue(UnderlyingValue(state.mProvokingVertex)));
      return;

    case LOCAL_GL_DEPTH_CLAMP:
      if (!IsExtensionEnabled(WebGLExtensionID::EXT_depth_clamp)) break;
      retval.set(JS::BooleanValue(state.mIsEnabledMap[LOCAL_GL_DEPTH_CLAMP]));
      return;

    // -
    // Array returns

    // 2 floats
    case LOCAL_GL_DEPTH_RANGE:
      retval.set(Create<dom::Float32Array>(cx, this, state.mDepthRange, rv));
      return;

    case LOCAL_GL_ALIASED_POINT_SIZE_RANGE:
      retval.set(
          Create<dom::Float32Array>(cx, this, limits.pointSizeRange, rv));
      return;

    case LOCAL_GL_ALIASED_LINE_WIDTH_RANGE:
      retval.set(
          Create<dom::Float32Array>(cx, this, limits.lineWidthRange, rv));
      return;

    // 4 floats
    case LOCAL_GL_COLOR_CLEAR_VALUE:
      retval.set(Create<dom::Float32Array>(cx, this, state.mClearColor, rv));
      return;

    case LOCAL_GL_BLEND_COLOR:
      retval.set(Create<dom::Float32Array>(cx, this, state.mBlendColor, rv));
      return;

    // 2 ints
    case LOCAL_GL_MAX_VIEWPORT_DIMS: {
      auto maxViewportDim = BitwiseCast<int32_t>(limits.maxViewportDim);
      const auto dims = std::array<int32_t, 2>{maxViewportDim, maxViewportDim};
      retval.set(Create<dom::Int32Array>(cx, this, dims, rv));
      return;
    }

    // 4 ints
    case LOCAL_GL_SCISSOR_BOX:
      retval.set(Create<dom::Int32Array>(cx, this, state.mScissor, rv));
      return;

    case LOCAL_GL_VIEWPORT:
      retval.set(Create<dom::Int32Array>(cx, this, state.mViewport, rv));
      return;

    // any
    case LOCAL_GL_COMPRESSED_TEXTURE_FORMATS:
      retval.set(Create<dom::Uint32Array>(cx, this,
                                          state.mCompressedTextureFormats, rv));
      return;
  }

  if (mIsWebGL2) {
    switch (pname) {
      case LOCAL_GL_COPY_READ_BUFFER_BINDING:
        fnSetRetval_Buffer(LOCAL_GL_COPY_READ_BUFFER);
        return;

      case LOCAL_GL_COPY_WRITE_BUFFER_BINDING:
        fnSetRetval_Buffer(LOCAL_GL_COPY_WRITE_BUFFER);
        return;

      case LOCAL_GL_DRAW_FRAMEBUFFER_BINDING:
        (void)ToJSValueOrNull(cx, state.mBoundDrawFb, retval);
        return;

      case LOCAL_GL_MAX_CLIENT_WAIT_TIMEOUT_WEBGL:
        retval.set(JS::NumberValue(webgl::kMaxClientWaitSyncTimeoutNS));
        return;

      case LOCAL_GL_PIXEL_PACK_BUFFER_BINDING:
        fnSetRetval_Buffer(LOCAL_GL_PIXEL_PACK_BUFFER);
        return;

      case LOCAL_GL_PIXEL_UNPACK_BUFFER_BINDING:
        fnSetRetval_Buffer(LOCAL_GL_PIXEL_UNPACK_BUFFER);
        return;

      case LOCAL_GL_READ_FRAMEBUFFER_BINDING:
        (void)ToJSValueOrNull(cx, state.mBoundReadFb, retval);
        return;

      case LOCAL_GL_SAMPLER_BINDING: {
        const auto& texUnit = state.mTexUnits[state.mActiveTexUnit];
        (void)ToJSValueOrNull(cx, texUnit.sampler, retval);
        return;
      }

      case LOCAL_GL_TEXTURE_BINDING_2D_ARRAY:
        fnSetRetval_Tex(LOCAL_GL_TEXTURE_2D_ARRAY);
        return;

      case LOCAL_GL_TEXTURE_BINDING_3D:
        fnSetRetval_Tex(LOCAL_GL_TEXTURE_3D);
        return;

      case LOCAL_GL_TRANSFORM_FEEDBACK_BINDING: {
        auto ret = state.mBoundTfo;
        if (ret == state.mDefaultTfo) {
          ret = nullptr;
        }
        (void)ToJSValueOrNull(cx, ret, retval);
        return;
      }

      case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
        fnSetRetval_Buffer(LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER);
        return;

      case LOCAL_GL_UNIFORM_BUFFER_BINDING:
        fnSetRetval_Buffer(LOCAL_GL_UNIFORM_BUFFER);
        return;

      case LOCAL_GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS:
        retval.set(
            JS::NumberValue(webgl::kMaxTransformFeedbackSeparateAttribs));
        return;
      case LOCAL_GL_MAX_UNIFORM_BUFFER_BINDINGS:
        retval.set(JS::NumberValue(limits.maxUniformBufferBindings));
        return;
      case LOCAL_GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:
        retval.set(JS::NumberValue(limits.uniformBufferOffsetAlignment));
        return;
      case LOCAL_GL_MAX_3D_TEXTURE_SIZE:
        retval.set(JS::NumberValue(limits.maxTex3dSize));
        return;
      case LOCAL_GL_MAX_ARRAY_TEXTURE_LAYERS:
        retval.set(JS::NumberValue(limits.maxTexArrayLayers));
        return;

      case LOCAL_GL_PACK_ROW_LENGTH:
        retval.set(JS::NumberValue(state.mPixelPackState.rowLength));
        return;
      case LOCAL_GL_PACK_SKIP_PIXELS:
        retval.set(JS::NumberValue(state.mPixelPackState.skipPixels));
        return;
      case LOCAL_GL_PACK_SKIP_ROWS:
        retval.set(JS::NumberValue(state.mPixelPackState.skipRows));
        return;

      case LOCAL_GL_UNPACK_IMAGE_HEIGHT:
        retval.set(JS::NumberValue(state.mPixelUnpackState.imageHeight));
        return;
      case LOCAL_GL_UNPACK_ROW_LENGTH:
        retval.set(JS::NumberValue(state.mPixelUnpackState.rowLength));
        return;
      case LOCAL_GL_UNPACK_SKIP_IMAGES:
        retval.set(JS::NumberValue(state.mPixelUnpackState.skipImages));
        return;
      case LOCAL_GL_UNPACK_SKIP_PIXELS:
        retval.set(JS::NumberValue(state.mPixelUnpackState.skipPixels));
        return;
      case LOCAL_GL_UNPACK_SKIP_ROWS:
        retval.set(JS::NumberValue(state.mPixelUnpackState.skipRows));
        return;
    }  // switch pname
  }  // if webgl2

  // -

  if (!debug) {
    const auto GetUnmaskedRenderer = [&]() {
      const auto prefLock = StaticPrefs::webgl_override_unmasked_renderer();
      if (!prefLock->IsEmpty()) {
        return Some(ToString(*prefLock));
      }
      return GetString(LOCAL_GL_RENDERER);
    };

    const auto GetUnmaskedVendor = [&]() {
      const auto prefLock = StaticPrefs::webgl_override_unmasked_vendor();
      if (!prefLock->IsEmpty()) {
        return Some(ToString(*prefLock));
      }
      return GetString(LOCAL_GL_VENDOR);
    };

    // -

    Maybe<std::string> ret;

    switch (pname) {
      case LOCAL_GL_VENDOR:
        ret = Some(std::string{"Mozilla"});
        break;

      case LOCAL_GL_RENDERER: {
        bool allowRenderer = StaticPrefs::webgl_enable_renderer_query();
        if (ShouldResistFingerprinting(RFPTarget::WebGLRenderInfo)) {
          allowRenderer = false;
        }
        if (allowRenderer) {
          ret = GetUnmaskedRenderer();
          if (ret) {
            ret = Some(webgl::SanitizeRenderer(*ret));
          }
        }
        if (!ret) {
          ret = Some(std::string{"Mozilla"});
        }
        break;
      }

      case LOCAL_GL_VERSION:
        if (mIsWebGL2) {
          ret = Some(std::string{"WebGL 2.0"});
        } else {
          ret = Some(std::string{"WebGL 1.0"});
        }
        break;

      case LOCAL_GL_SHADING_LANGUAGE_VERSION:
        if (mIsWebGL2) {
          ret = Some(std::string{"WebGL GLSL ES 3.00"});
        } else {
          ret = Some(std::string{"WebGL GLSL ES 1.0"});
        }
        break;

      case dom::WEBGL_debug_renderer_info_Binding::UNMASKED_VENDOR_WEBGL:
      case dom::WEBGL_debug_renderer_info_Binding::UNMASKED_RENDERER_WEBGL: {
        if (!IsExtensionEnabled(WebGLExtensionID::WEBGL_debug_renderer_info)) {
          EnqueueError_ArgEnum("pname", pname);
          return;
        }

        switch (pname) {
          case dom::WEBGL_debug_renderer_info_Binding::UNMASKED_RENDERER_WEBGL:
            ret = GetUnmaskedRenderer();
            if (ret && StaticPrefs::webgl_sanitize_unmasked_renderer()) {
              *ret = webgl::SanitizeRenderer(*ret);
            }
            break;

          case dom::WEBGL_debug_renderer_info_Binding::UNMASKED_VENDOR_WEBGL:
            ret = GetUnmaskedVendor();
            break;

          default:
            MOZ_CRASH();
        }
        break;
      }

      default:
        break;
    }

    if (ret) {
      retval.set(StringValue(cx, *ret, rv));
      return;
    }
  }  // if (!debug)

  // -

  bool debugOnly = false;
  bool asString = false;

  switch (pname) {
    case LOCAL_GL_EXTENSIONS:
    case LOCAL_GL_RENDERER:
    case LOCAL_GL_VENDOR:
    case LOCAL_GL_VERSION:
    case dom::MOZ_debug_Binding::CONTEXT_TYPE:
    case dom::MOZ_debug_Binding::WSI_INFO:
      debugOnly = true;
      asString = true;
      break;

    case dom::MOZ_debug_Binding::DOES_INDEX_VALIDATION:
      debugOnly = true;
      break;

    default:
      break;
  }

  if (debugOnly && !debug) {
    EnqueueError_ArgEnum("pname", pname);
    return;
  }

  // -

  if (asString) {
    const auto maybe = GetString(pname);
    if (maybe) {
      auto str = std::string{};
      if (pname == dom::MOZ_debug_Binding::WSI_INFO) {
        const auto& outOfProcess = mNotLost->outOfProcess;
        const auto& inProcess = mNotLost->inProcess;
        str += PrintfStdString("outOfProcess: %s\ninProcess: %s\n",
                               ToChars(bool(outOfProcess)),
                               ToChars(bool(inProcess)));
      }
      str += *maybe;
      retval.set(StringValue(cx, str.c_str(), rv));
    }
  } else {
    const auto maybe = GetNumber(pname);
    if (maybe) {
      switch (pname) {
        // WebGL 1:
        case LOCAL_GL_BLEND:
        case LOCAL_GL_CULL_FACE:
        case LOCAL_GL_DEPTH_TEST:
        case LOCAL_GL_DEPTH_WRITEMASK:
        case LOCAL_GL_DITHER:
        case LOCAL_GL_POLYGON_OFFSET_FILL:
        case LOCAL_GL_SAMPLE_ALPHA_TO_COVERAGE:
        case LOCAL_GL_SAMPLE_COVERAGE:
        case LOCAL_GL_SAMPLE_COVERAGE_INVERT:
        case LOCAL_GL_SCISSOR_TEST:
        case LOCAL_GL_STENCIL_TEST:
        // WebGL 2:
        case LOCAL_GL_RASTERIZER_DISCARD:
        case LOCAL_GL_TRANSFORM_FEEDBACK_ACTIVE:
        case LOCAL_GL_TRANSFORM_FEEDBACK_PAUSED:
          retval.set(JS::BooleanValue(*maybe));
          return;

        // 4 bools
        case LOCAL_GL_COLOR_WRITEMASK: {
          const auto mask = uint8_t(*maybe);
          const auto bs = std::bitset<4>(mask);
          const auto src = std::array<bool, 4>{bs[0], bs[1], bs[2], bs[3]};
          JS::Rooted<JS::Value> arr(cx);
          if (!dom::ToJSValue(cx, src.data(), src.size(), &arr)) {
            rv = NS_ERROR_OUT_OF_MEMORY;
          }
          retval.set(arr);
          return;
        }

        case LOCAL_GL_IMPLEMENTATION_COLOR_READ_TYPE: {
          auto readType = (GLenum)*maybe;
          // Map HALF_FLOAT to HALF_FLOAT_OES for webgl 1 clients.
          if (readType == LOCAL_GL_HALF_FLOAT && !mIsWebGL2) {
            readType = LOCAL_GL_HALF_FLOAT_OES;
          }
          retval.set(JS::NumberValue(readType));
          return;
        }

        default:
          retval.set(JS::NumberValue(*maybe));
          return;
      }
    }
  }
}

void ClientWebGLContext::GetBufferParameter(
    JSContext* cx, GLenum target, GLenum pname,
    JS::MutableHandle<JS::Value> retval) const {
  retval.set(JS::NullValue());
  if (IsContextLost()) return;

  const auto maybe = [&]() {
    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      return inProcess->GetBufferParameter(target, pname);
    }
    const auto& child = mNotLost->outOfProcess;
    child->FlushPendingCmds();
    Maybe<double> ret;
    if (!child->SendGetBufferParameter(target, pname, &ret)) {
      ret.reset();
    }
    return ret;
  }();
  if (maybe) {
    retval.set(JS::NumberValue(*maybe));
  }
}

bool IsFramebufferTarget(const bool isWebgl2, const GLenum target) {
  switch (target) {
    case LOCAL_GL_FRAMEBUFFER:
      return true;

    case LOCAL_GL_DRAW_FRAMEBUFFER:
    case LOCAL_GL_READ_FRAMEBUFFER:
      return isWebgl2;

    default:
      return false;
  }
}

void ClientWebGLContext::GetFramebufferAttachmentParameter(
    JSContext* const cx, const GLenum target, const GLenum attachment,
    const GLenum pname, JS::MutableHandle<JS::Value> retval,
    ErrorResult& rv) const {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getFramebufferAttachmentParameter");
  if (IsContextLost()) return;

  const auto& state = State();

  if (!IsFramebufferTarget(mIsWebGL2, target)) {
    EnqueueError_ArgEnum("target", target);
    return;
  }
  auto fb = state.mBoundDrawFb;
  if (target == LOCAL_GL_READ_FRAMEBUFFER) {
    fb = state.mBoundReadFb;
  }

  const auto fnGet = [&](const GLenum pname) {
    const auto fbId = fb ? fb->mId : 0;

    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      return inProcess->GetFramebufferAttachmentParameter(fbId, attachment,
                                                          pname);
    }
    const auto& child = mNotLost->outOfProcess;
    child->FlushPendingCmds();
    Maybe<double> ret;
    if (!child->SendGetFramebufferAttachmentParameter(fbId, attachment, pname,
                                                      &ret)) {
      ret.reset();
    }
    return ret;
  };

  if (fb) {
    if (fb->mOpaque) {
      EnqueueError(LOCAL_GL_INVALID_OPERATION,
                   "An opaque framebuffer's attachments cannot be inspected or "
                   "changed.");
      return;
    }
    auto attachmentSlotEnum = attachment;
    if (mIsWebGL2 && attachment == LOCAL_GL_DEPTH_STENCIL_ATTACHMENT) {
      // In webgl2, DEPTH_STENCIL is valid iff the DEPTH and STENCIL images
      // match, so check if the server errors.
      const auto maybe = fnGet(LOCAL_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE);
      if (!maybe) return;
      attachmentSlotEnum = LOCAL_GL_DEPTH_ATTACHMENT;
    }

    const auto maybeSlot = fb->GetAttachment(attachmentSlotEnum);
    if (!maybeSlot) {
      EnqueueError_ArgEnum("attachment", attachment);
      return;
    }
    const auto& attached = *maybeSlot;

    // -

    if (pname == LOCAL_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME) {
      if (attached.rb) {
        (void)ToJSValueOrNull(cx, attached.rb, retval);
      } else {
        if (!mIsWebGL2 && !attached.tex) {
          EnqueueError_ArgEnum("pname", pname);
          return;
        }
        (void)ToJSValueOrNull(cx, attached.tex, retval);
      }
      return;
    }
  }

  const auto maybe = fnGet(pname);
  if (maybe) {
    retval.set(JS::NumberValue(*maybe));
  }
}

void ClientWebGLContext::GetRenderbufferParameter(
    JSContext* cx, GLenum target, GLenum pname,
    JS::MutableHandle<JS::Value> retval) const {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getRenderbufferParameter");
  if (IsContextLost()) return;

  if (target != LOCAL_GL_RENDERBUFFER) {
    EnqueueError_ArgEnum("target", target);
    return;
  }

  const auto& state = State();
  const auto& rb = state.mBoundRb;
  const auto rbId = rb ? rb->mId : 0;
  const auto maybe = [&]() {
    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      return inProcess->GetRenderbufferParameter(rbId, pname);
    }
    const auto& child = mNotLost->outOfProcess;
    child->FlushPendingCmds();
    Maybe<double> ret;
    if (!child->SendGetRenderbufferParameter(rbId, pname, &ret)) {
      ret.reset();
    }
    return ret;
  }();
  if (maybe) {
    retval.set(JS::NumberValue(*maybe));
  }
}

void ClientWebGLContext::GetIndexedParameter(
    JSContext* cx, GLenum target, GLuint index,
    JS::MutableHandle<JS::Value> retval, ErrorResult& rv) const {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getIndexedParameter");
  if (IsContextLost()) return;

  const auto& state = State();

  switch (target) {
    case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER_BINDING: {
      const auto& list = state.mBoundTfo->mAttribBuffers;
      if (index >= list.size()) {
        EnqueueError(LOCAL_GL_INVALID_VALUE,
                     "`index` (%u) >= MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS",
                     index);
        return;
      }
      (void)ToJSValueOrNull(cx, list[index], retval);
      return;
    }

    case LOCAL_GL_UNIFORM_BUFFER_BINDING: {
      const auto& list = state.mBoundUbos;
      if (index >= list.size()) {
        EnqueueError(LOCAL_GL_INVALID_VALUE,
                     "`index` (%u) >= MAX_UNIFORM_BUFFER_BINDINGS", index);
        return;
      }
      (void)ToJSValueOrNull(cx, list[index], retval);
      return;
    }
  }

  const auto maybe = [&]() {
    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      return inProcess->GetIndexedParameter(target, index);
    }
    const auto& child = mNotLost->outOfProcess;
    child->FlushPendingCmds();
    Maybe<double> ret;
    if (!child->SendGetIndexedParameter(target, index, &ret)) {
      ret.reset();
    }
    return ret;
  }();
  if (maybe) {
    switch (target) {
      case LOCAL_GL_COLOR_WRITEMASK: {
        const auto bs = std::bitset<4>(*maybe);
        const auto src = std::array<bool, 4>{bs[0], bs[1], bs[2], bs[3]};
        JS::Rooted<JS::Value> arr(cx);
        if (!dom::ToJSValue(cx, src.data(), src.size(), &arr)) {
          rv = NS_ERROR_OUT_OF_MEMORY;
        }
        retval.set(arr);
        return;
      }

      default:
        retval.set(JS::NumberValue(*maybe));
        return;
    }
  }
}

void ClientWebGLContext::GetUniform(JSContext* const cx,
                                    const WebGLProgramJS& prog,
                                    const WebGLUniformLocationJS& loc,
                                    JS::MutableHandle<JS::Value> retval) {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getUniform");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "prog")) return;
  if (!loc.ValidateUsable(*this, "loc")) return;

  const auto& progLinkResult = GetLinkResult(prog);
  if (!progLinkResult.success) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "Program is not linked.");
    return;
  }
  const auto& uniformLinkResult = loc.mParent.lock();
  if (uniformLinkResult.get() != &progLinkResult) {
    EnqueueError(
        LOCAL_GL_INVALID_OPERATION,
        "UniformLocation is not from the most recent linking of Program.");
    return;
  }

  const auto res = [&]() {
    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      return inProcess->GetUniform(prog.mId, loc.mLocation);
    }
    const auto& child = mNotLost->outOfProcess;
    child->FlushPendingCmds();
    webgl::GetUniformData ret;
    if (!child->SendGetUniform(prog.mId, loc.mLocation, &ret)) {
      ret = {};
    }
    return ret;
  }();
  if (!res.type) return;

  const auto elemCount = ElemTypeComponents(res.type);
  MOZ_ASSERT(elemCount);

  switch (res.type) {
    case LOCAL_GL_BOOL:
      retval.set(JS::BooleanValue(res.data[0]));
      return;

    case LOCAL_GL_FLOAT: {
      const auto ptr = reinterpret_cast<const float*>(res.data);
      MOZ_ALWAYS_TRUE(dom::ToJSValue(cx, *ptr, retval));
      return;
    }
    case LOCAL_GL_INT: {
      const auto ptr = reinterpret_cast<const int32_t*>(res.data);
      MOZ_ALWAYS_TRUE(dom::ToJSValue(cx, *ptr, retval));
      return;
    }
    case LOCAL_GL_UNSIGNED_INT:
    case LOCAL_GL_SAMPLER_2D:
    case LOCAL_GL_SAMPLER_3D:
    case LOCAL_GL_SAMPLER_CUBE:
    case LOCAL_GL_SAMPLER_2D_SHADOW:
    case LOCAL_GL_SAMPLER_2D_ARRAY:
    case LOCAL_GL_SAMPLER_2D_ARRAY_SHADOW:
    case LOCAL_GL_SAMPLER_CUBE_SHADOW:
    case LOCAL_GL_INT_SAMPLER_2D:
    case LOCAL_GL_INT_SAMPLER_3D:
    case LOCAL_GL_INT_SAMPLER_CUBE:
    case LOCAL_GL_INT_SAMPLER_2D_ARRAY:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_3D:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_CUBE:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D_ARRAY: {
      const auto ptr = reinterpret_cast<const uint32_t*>(res.data);
      MOZ_ALWAYS_TRUE(dom::ToJSValue(cx, *ptr, retval));
      return;
    }

      // -

    case LOCAL_GL_BOOL_VEC2:
    case LOCAL_GL_BOOL_VEC3:
    case LOCAL_GL_BOOL_VEC4: {
      const auto intArr = reinterpret_cast<const int32_t*>(res.data);
      bool boolArr[4] = {};
      for (const auto i : IntegerRange(elemCount)) {
        boolArr[i] = bool(intArr[i]);
      }
      MOZ_ALWAYS_TRUE(dom::ToJSValue(cx, boolArr, elemCount, retval));
      return;
    }

    case LOCAL_GL_FLOAT_VEC2:
    case LOCAL_GL_FLOAT_VEC3:
    case LOCAL_GL_FLOAT_VEC4:
    case LOCAL_GL_FLOAT_MAT2:
    case LOCAL_GL_FLOAT_MAT3:
    case LOCAL_GL_FLOAT_MAT4:
    case LOCAL_GL_FLOAT_MAT2x3:
    case LOCAL_GL_FLOAT_MAT2x4:
    case LOCAL_GL_FLOAT_MAT3x2:
    case LOCAL_GL_FLOAT_MAT3x4:
    case LOCAL_GL_FLOAT_MAT4x2:
    case LOCAL_GL_FLOAT_MAT4x3: {
      const auto ptr = reinterpret_cast<const float*>(res.data);
      IgnoredErrorResult error;
      JSObject* obj =
          dom::Float32Array::Create(cx, this, Span(ptr, elemCount), error);
      MOZ_ASSERT(obj);
      retval.set(JS::ObjectOrNullValue(obj));
      return;
    }

    case LOCAL_GL_INT_VEC2:
    case LOCAL_GL_INT_VEC3:
    case LOCAL_GL_INT_VEC4: {
      const auto ptr = reinterpret_cast<const int32_t*>(res.data);
      IgnoredErrorResult error;
      JSObject* obj =
          dom::Int32Array::Create(cx, this, Span(ptr, elemCount), error);
      MOZ_ASSERT(obj);
      retval.set(JS::ObjectOrNullValue(obj));
      return;
    }

    case LOCAL_GL_UNSIGNED_INT_VEC2:
    case LOCAL_GL_UNSIGNED_INT_VEC3:
    case LOCAL_GL_UNSIGNED_INT_VEC4: {
      const auto ptr = reinterpret_cast<const uint32_t*>(res.data);
      IgnoredErrorResult error;
      JSObject* obj =
          dom::Uint32Array::Create(cx, this, Span(ptr, elemCount), error);
      MOZ_ASSERT(obj);
      retval.set(JS::ObjectOrNullValue(obj));
      return;
    }

    default:
      MOZ_CRASH("GFX: Invalid elemType.");
  }
}

already_AddRefed<WebGLShaderPrecisionFormatJS>
ClientWebGLContext::GetShaderPrecisionFormat(const GLenum shadertype,
                                             const GLenum precisiontype) {
  const FuncScope funcScope(*this, "getShaderPrecisionFormat");
  if (IsContextLost()) return nullptr;

  const auto& shaderPrecisions = *mNotLost->info.shaderPrecisions;
  const auto args =
      webgl::GetShaderPrecisionFormatArgs{shadertype, precisiontype};
  const auto found = MaybeFind(shaderPrecisions, args);
  if (!found) {
    EnqueueError(
        LOCAL_GL_INVALID_ENUM, "Bad shaderType (%s) or precisionType (%s)",
        EnumString(shadertype).c_str(), EnumString(precisiontype).c_str());
    return nullptr;
  }

  return AsAddRefed(new WebGLShaderPrecisionFormatJS(*found));
}

void ClientWebGLContext::BlendColor(GLclampf r, GLclampf g, GLclampf b,
                                    GLclampf a) {
  const FuncScope funcScope(*this, "blendColor");
  if (IsContextLost()) return;
  auto& state = State();

  const bool unclamped =
      (mIsWebGL2 ||
       IsExtensionEnabled(WebGLExtensionID::WEBGL_color_buffer_float) ||
       IsExtensionEnabled(WebGLExtensionID::EXT_color_buffer_half_float));
  if (!unclamped) {
    r = std::clamp(r, 0.0f, 1.0f);
    g = std::clamp(g, 0.0f, 1.0f);
    b = std::clamp(b, 0.0f, 1.0f);
    a = std::clamp(a, 0.0f, 1.0f);
  }

  auto& cache = state.mBlendColor;
  cache[0] = r;
  cache[1] = g;
  cache[2] = b;
  cache[3] = a;

  Run<RPROC(BlendColor)>(r, g, b, a);
}

void ClientWebGLContext::BlendEquationSeparateI(Maybe<GLuint> i, GLenum modeRGB,
                                                GLenum modeAlpha) {
  Run<RPROC(BlendEquationSeparate)>(i, modeRGB, modeAlpha);
}

void ClientWebGLContext::BlendFuncSeparateI(Maybe<GLuint> i, GLenum srcRGB,
                                            GLenum dstRGB, GLenum srcAlpha,
                                            GLenum dstAlpha) {
  Run<RPROC(BlendFuncSeparate)>(i, srcRGB, dstRGB, srcAlpha, dstAlpha);
}

GLenum ClientWebGLContext::CheckFramebufferStatus(GLenum target) {
  if (IsContextLost()) return LOCAL_GL_FRAMEBUFFER_UNSUPPORTED;

  const auto& inProcess = mNotLost->inProcess;
  if (inProcess) {
    return inProcess->CheckFramebufferStatus(target);
  }
  const auto& child = mNotLost->outOfProcess;
  child->FlushPendingCmds();
  GLenum ret = 0;
  if (!child->SendCheckFramebufferStatus(target, &ret)) {
    ret = 0;
  }
  return ret;
}

void ClientWebGLContext::Clear(GLbitfield mask) {
  Run<RPROC(Clear)>(mask);

  AfterDrawCall();
}

// -

void ClientWebGLContext::ClearBufferTv(const GLenum buffer,
                                       const GLint drawBuffer,
                                       const webgl::AttribBaseType type,
                                       JS::AutoCheckCannotGC&& nogc,
                                       const Span<const uint8_t>& view,
                                       const GLuint srcElemOffset) {
  if (IsContextLost()) return;

  const auto byteOffset = CheckedInt<size_t>(srcElemOffset) * sizeof(float);
  if (!byteOffset.isValid() || byteOffset.value() > view.Length()) {
    nogc.reset();
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`srcOffset` too large for `values`.");
    return;
  }
  webgl::TypedQuad data;
  data.type = type;

  auto dataSize = data.data.size();
  switch (buffer) {
    case LOCAL_GL_COLOR:
      break;

    case LOCAL_GL_DEPTH:
      dataSize = sizeof(float);
      break;

    case LOCAL_GL_STENCIL:
      dataSize = sizeof(int32_t);
      break;

    default:
      nogc.reset();
      EnqueueError_ArgEnum("buffer", buffer);
      return;
  }

  const auto requiredBytes = byteOffset + dataSize;
  if (!requiredBytes.isValid() || requiredBytes.value() > view.Length()) {
    nogc.reset();
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`values` too small.");
    return;
  }

  memcpy(data.data.data(), view.data() + byteOffset.value(), dataSize);
  nogc.reset();  // Done with `view`.
  Run<RPROC(ClearBufferTv)>(buffer, drawBuffer, data);

  AfterDrawCall();
}

void ClientWebGLContext::ClearBufferfi(GLenum buffer, GLint drawBuffer,
                                       GLfloat depth, GLint stencil) {
  Run<RPROC(ClearBufferfi)>(buffer, drawBuffer, depth, stencil);

  AfterDrawCall();
}

// -

void ClientWebGLContext::ClearColor(GLclampf r, GLclampf g, GLclampf b,
                                    GLclampf a) {
  const FuncScope funcScope(*this, "clearColor");
  if (IsContextLost()) return;
  auto& state = State();

  auto& cache = state.mClearColor;
  cache[0] = r;
  cache[1] = g;
  cache[2] = b;
  cache[3] = a;

  Run<RPROC(ClearColor)>(r, g, b, a);
}

void ClientWebGLContext::ClearDepth(GLclampf v) { Run<RPROC(ClearDepth)>(v); }

void ClientWebGLContext::ClearStencil(GLint v) { Run<RPROC(ClearStencil)>(v); }

void ClientWebGLContext::ColorMaskI(Maybe<GLuint> i, bool r, bool g, bool b,
                                    bool a) const {
  const FuncScope funcScope(*this, "colorMask");
  if (IsContextLost()) return;

  const uint8_t mask =
      uint8_t(r << 0) | uint8_t(g << 1) | uint8_t(b << 2) | uint8_t(a << 3);
  Run<RPROC(ColorMask)>(i, mask);
}

void ClientWebGLContext::CullFace(GLenum face) { Run<RPROC(CullFace)>(face); }

void ClientWebGLContext::DepthFunc(GLenum func) { Run<RPROC(DepthFunc)>(func); }

void ClientWebGLContext::DepthMask(WebGLboolean b) { Run<RPROC(DepthMask)>(b); }

void ClientWebGLContext::DepthRange(GLclampf zNear, GLclampf zFar) {
  const FuncScope funcScope(*this, "depthRange");
  if (IsContextLost()) return;
  auto& state = State();

  state.mDepthRange = {zNear, zFar};

  Run<RPROC(DepthRange)>(zNear, zFar);
}

void ClientWebGLContext::Flush(const bool flushGl) const {
  const FuncScope funcScope(*this, "flush");
  if (IsContextLost()) return;

  if (flushGl) {
    Run<RPROC(Flush)>();
  }

  if (mNotLost->inProcess) return;
  const auto& child = mNotLost->outOfProcess;
  child->FlushPendingCmds();
}

void ClientWebGLContext::Finish() {
  if (IsContextLost()) return;

  const auto& inProcess = mNotLost->inProcess;
  if (inProcess) {
    inProcess->Finish();
    return;
  }
  const auto& child = mNotLost->outOfProcess;
  child->FlushPendingCmds();
  (void)child->SendFinish();
}

void ClientWebGLContext::FrontFace(GLenum mode) { Run<RPROC(FrontFace)>(mode); }

GLenum ClientWebGLContext::GetError() {
  const FuncScope funcScope(*this, "getError");
  if (mNextError) {
    const auto ret = mNextError;
    mNextError = 0;
    return ret;
  }
  if (IsContextLost()) return 0;

  const auto& inProcess = mNotLost->inProcess;
  if (inProcess) {
    return inProcess->GetError();
  }
  const auto& child = mNotLost->outOfProcess;
  child->FlushPendingCmds();
  GLenum ret = 0;
  if (!child->SendGetError(&ret)) {
    ret = 0;
  }
  return ret;
}

void ClientWebGLContext::Hint(GLenum target, GLenum mode) {
  Run<RPROC(Hint)>(target, mode);
}

void ClientWebGLContext::LineWidth(GLfloat width) {
  Run<RPROC(LineWidth)>(width);
}

Maybe<webgl::ErrorInfo> SetPixelUnpack(
    const bool isWebgl2, webgl::PixelUnpackStateWebgl* const unpacking,
    const GLenum pname, const GLint param);

void ClientWebGLContext::PixelStorei(const GLenum pname, const GLint iparam) {
  const FuncScope funcScope(*this, "pixelStorei");
  if (IsContextLost()) return;
  if (!ValidateNonNegative("param", iparam)) return;
  const auto param = static_cast<uint32_t>(iparam);

  auto& state = State();
  auto& packState = state.mPixelPackState;
  switch (pname) {
    case LOCAL_GL_PACK_ALIGNMENT:
      switch (param) {
        case 1:
        case 2:
        case 4:
        case 8:
          break;
        default:
          EnqueueError(LOCAL_GL_INVALID_VALUE,
                       "PACK_ALIGNMENT must be one of [1,2,4,8], was %i.",
                       iparam);
          return;
      }
      packState.alignmentInTypeElems = param;
      return;

    case LOCAL_GL_PACK_ROW_LENGTH:
      if (!mIsWebGL2) break;
      packState.rowLength = param;
      return;

    case LOCAL_GL_PACK_SKIP_PIXELS:
      if (!mIsWebGL2) break;
      packState.skipPixels = param;
      return;

    case LOCAL_GL_PACK_SKIP_ROWS:
      if (!mIsWebGL2) break;
      packState.skipRows = param;
      return;

    case dom::MOZ_debug_Binding::UNPACK_REQUIRE_FASTPATH:
      if (!IsSupported(WebGLExtensionID::MOZ_debug)) {
        EnqueueError_ArgEnum("pname", pname);
        return;
      }
      break;

    default:
      break;
  }

  const auto err =
      SetPixelUnpack(mIsWebGL2, &state.mPixelUnpackState, pname, iparam);
  if (err) {
    EnqueueError(*err);
    return;
  }
}

void ClientWebGLContext::PolygonOffset(GLfloat factor, GLfloat units) {
  Run<RPROC(PolygonOffset)>(factor, units);
}

void ClientWebGLContext::SampleCoverage(GLclampf value, WebGLboolean invert) {
  Run<RPROC(SampleCoverage)>(value, invert);
}

void ClientWebGLContext::Scissor(GLint x, GLint y, GLsizei width,
                                 GLsizei height) {
  const FuncScope funcScope(*this, "scissor");
  if (IsContextLost()) return;
  auto& state = State();

  if (!ValidateNonNegative("width", width) ||
      !ValidateNonNegative("height", height)) {
    return;
  }

  state.mScissor = {x, y, width, height};

  Run<RPROC(Scissor)>(x, y, width, height);
}

void ClientWebGLContext::StencilFuncSeparate(GLenum face, GLenum func,
                                             GLint ref, GLuint mask) {
  Run<RPROC(StencilFuncSeparate)>(face, func, ref, mask);
}

void ClientWebGLContext::StencilMaskSeparate(GLenum face, GLuint mask) {
  Run<RPROC(StencilMaskSeparate)>(face, mask);
}

void ClientWebGLContext::StencilOpSeparate(GLenum face, GLenum sfail,
                                           GLenum dpfail, GLenum dppass) {
  Run<RPROC(StencilOpSeparate)>(face, sfail, dpfail, dppass);
}

void ClientWebGLContext::Viewport(GLint x, GLint y, GLsizei width,
                                  GLsizei height) {
  const FuncScope funcScope(*this, "viewport");
  if (IsContextLost()) return;
  auto& state = State();

  if (!ValidateNonNegative("width", width) ||
      !ValidateNonNegative("height", height)) {
    return;
  }

  state.mViewport = {x, y, width, height};

  Run<RPROC(Viewport)>(x, y, width, height);
}

// ------------------------- Buffer Objects -------------------------

Maybe<const webgl::ErrorInfo> ValidateBindBuffer(
    const GLenum target, const webgl::BufferKind curKind) {
  if (curKind == webgl::BufferKind::Undefined) return {};

  auto requiredKind = webgl::BufferKind::NonIndex;
  switch (target) {
    case LOCAL_GL_COPY_READ_BUFFER:
    case LOCAL_GL_COPY_WRITE_BUFFER:
      return {};  // Always ok

    case LOCAL_GL_ELEMENT_ARRAY_BUFFER:
      requiredKind = webgl::BufferKind::Index;
      break;

    default:
      break;
  }

  if (curKind != requiredKind) {
    const auto fnKindStr = [&](const webgl::BufferKind kind) {
      if (kind == webgl::BufferKind::Index) return "ELEMENT_ARRAY_BUFFER";
      return "non-ELEMENT_ARRAY_BUFFER";
    };
    const auto info = nsPrintfCString(
        "Buffer previously bound to %s cannot be now bound to %s.",
        fnKindStr(curKind), fnKindStr(requiredKind));
    return Some(
        webgl::ErrorInfo{LOCAL_GL_INVALID_OPERATION, info.BeginReading()});
  }

  return {};
}

Maybe<webgl::ErrorInfo> CheckBindBufferRange(
    const GLenum target, const GLuint index, const bool isBuffer,
    const uint64_t offset, const uint64_t size, const webgl::Limits& limits) {
  const auto fnSome = [&](const GLenum type, const nsACString& info) {
    return Some(webgl::ErrorInfo{type, info.BeginReading()});
  };

  switch (target) {
    case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER:
      if (index >= webgl::kMaxTransformFeedbackSeparateAttribs) {
        const auto info = nsPrintfCString(
            "`index` (%u) must be less than "
            "MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS (%u).",
            index, webgl::kMaxTransformFeedbackSeparateAttribs);
        return fnSome(LOCAL_GL_INVALID_VALUE, info);
      }

      if (offset % 4 != 0 || size % 4 != 0) {
        const auto info =
            nsPrintfCString("`offset` (%" PRIu64 ") and `size` (%" PRIu64
                            ") must both be aligned to 4 for"
                            " TRANSFORM_FEEDBACK_BUFFER.",
                            offset, size);
        return fnSome(LOCAL_GL_INVALID_VALUE, info);
      }
      break;

    case LOCAL_GL_UNIFORM_BUFFER:
      if (index >= limits.maxUniformBufferBindings) {
        const auto info = nsPrintfCString(
            "`index` (%u) must be less than MAX_UNIFORM_BUFFER_BINDINGS (%u).",
            index, limits.maxUniformBufferBindings);
        return fnSome(LOCAL_GL_INVALID_VALUE, info);
      }

      if (offset % limits.uniformBufferOffsetAlignment != 0) {
        const auto info =
            nsPrintfCString("`offset` (%" PRIu64
                            ") must be aligned to "
                            "UNIFORM_BUFFER_OFFSET_ALIGNMENT (%u).",
                            offset, limits.uniformBufferOffsetAlignment);
        return fnSome(LOCAL_GL_INVALID_VALUE, info);
      }
      break;

    default: {
      const auto info =
          nsPrintfCString("Unrecognized `target`: 0x%04x", target);
      return fnSome(LOCAL_GL_INVALID_ENUM, info);
    }
  }

  return {};
}

// -

void ClientWebGLContext::BindBuffer(const GLenum target,
                                    WebGLBufferJS* const buffer) {
  const FuncScope funcScope(*this, "bindBuffer");
  if (IsContextLost()) return;
  if (buffer && !buffer->ValidateUsable(*this, "buffer")) return;

  // -
  // Check for INVALID_ENUM

  auto& state = State();
  auto* slot = &(state.mBoundVao->mIndexBuffer);
  if (target != LOCAL_GL_ELEMENT_ARRAY_BUFFER) {
    const auto itr = state.mBoundBufferByTarget.find(target);
    if (itr == state.mBoundBufferByTarget.end()) {
      EnqueueError_ArgEnum("target", target);
      return;
    }
    slot = &(itr->second);
  }

  // -

  auto kind = webgl::BufferKind::Undefined;
  if (buffer) {
    kind = buffer->mKind;
  }
  const auto err = ValidateBindBuffer(target, kind);
  if (err) {
    EnqueueError(err->type, "%s", err->info.c_str());
    return;
  }

  // -
  // Validation complete

  if (buffer && buffer->mKind == webgl::BufferKind::Undefined) {
    if (target == LOCAL_GL_ELEMENT_ARRAY_BUFFER) {
      buffer->mKind = webgl::BufferKind::Index;
    } else {
      buffer->mKind = webgl::BufferKind::NonIndex;
    }
  }
  *slot = buffer;

  // -

  Run<RPROC(BindBuffer)>(target, buffer ? buffer->mId : 0);
}

// -

void ClientWebGLContext::BindBufferRangeImpl(const GLenum target,
                                             const GLuint index,
                                             WebGLBufferJS* const buffer,
                                             const uint64_t offset,
                                             const uint64_t size) {
  if (buffer && !buffer->ValidateUsable(*this, "buffer")) return;
  auto& state = State();

  // -

  const auto& limits = Limits();
  auto err =
      CheckBindBufferRange(target, index, bool(buffer), offset, size, limits);
  if (err) {
    EnqueueError(err->type, "%s", err->info.c_str());
    return;
  }

  // -

  auto kind = webgl::BufferKind::Undefined;
  if (buffer) {
    kind = buffer->mKind;
  }
  err = ValidateBindBuffer(target, kind);
  if (err) {
    EnqueueError(err->type, "%s", err->info.c_str());
    return;
  }

  if (target == LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER) {
    if (state.mTfActiveAndNotPaused) {
      EnqueueError(LOCAL_GL_INVALID_OPERATION,
                   "Cannot change TRANSFORM_FEEDBACK_BUFFER while "
                   "TransformFeedback is active and not paused.");
      return;
    }
  }

  // -
  // Validation complete

  if (buffer && buffer->mKind == webgl::BufferKind::Undefined) {
    buffer->mKind = webgl::BufferKind::NonIndex;
  }

  // -

  switch (target) {
    case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER:
      state.mBoundTfo->mAttribBuffers[index] = buffer;
      break;

    case LOCAL_GL_UNIFORM_BUFFER:
      state.mBoundUbos[index] = buffer;
      break;

    default:
      MOZ_CRASH("Bad `target`");
  }
  state.mBoundBufferByTarget[target] = buffer;

  // -

  Run<RPROC(BindBufferRange)>(target, index, buffer ? buffer->mId : 0, offset,
                              size);
}

static inline size_t SizeOfViewElem(const dom::ArrayBufferView& view) {
  const auto& elemType = view.Type();
  if (elemType == js::Scalar::MaxTypedArrayViewType)  // DataViews.
    return 1;

  return js::Scalar::byteSize(elemType);
}

void ClientWebGLContext::GetBufferSubData(GLenum target, GLintptr srcByteOffset,
                                          const dom::ArrayBufferView& dstData,
                                          GLuint dstElemOffset,
                                          GLuint dstElemCountOverride) {
  const FuncScope funcScope(*this, "getBufferSubData");
  if (IsContextLost()) return;
  const auto notLost =
      mNotLost;  // Hold a strong-ref to prevent LoseContext=>UAF.
  if (!ValidateNonNegative("srcByteOffset", srcByteOffset)) return;

  size_t elemSize = SizeOfViewElem(dstData);
  dstData.ProcessFixedData([&](const Span<uint8_t>& aData) {
    const auto& destView =
        ValidateArrayBufferView(aData, elemSize, dstElemOffset,
                                dstElemCountOverride, LOCAL_GL_INVALID_VALUE);
    if (!destView) {
      return;
    }

    const auto& inProcessContext = notLost->inProcess;
    if (inProcessContext) {
      inProcessContext->GetBufferSubData(target, srcByteOffset, *destView);
      return;
    }

    const auto& child = notLost->outOfProcess;
    child->FlushPendingCmds();
    mozilla::ipc::Shmem rawShmem;
    if (!child->SendGetBufferSubData(target, srcByteOffset, destView->size(),
                                     &rawShmem)) {
      return;
    }
    const webgl::RaiiShmem shmem{child, rawShmem};
    if (!shmem) {
      EnqueueError(LOCAL_GL_OUT_OF_MEMORY, "Failed to map in sub data buffer.");
      return;
    }

    const auto shmemView = Span{shmem.ByteRange()};
    MOZ_RELEASE_ASSERT(shmemView.size() == 1 + destView->size());

    const auto ok = bool(shmemView[0]);
    const auto srcView = shmemView.subspan(1);
    if (ok) {
      Memcpy(&*destView, srcView);
    }
  });
}

////

void ClientWebGLContext::BufferData(GLenum target, WebGLsizeiptr rawSize,
                                    GLenum usage) {
  const FuncScope funcScope(*this, "bufferData");
  if (!ValidateNonNegative("size", rawSize)) return;

  const auto size = MaybeAs<size_t>(rawSize);
  if (!size) {
    EnqueueError(LOCAL_GL_OUT_OF_MEMORY, "`size` too large for platform.");
    return;
  }
  Run<RPROC(BufferData_SizeOnly)>(target, *size, usage);
}

void ClientWebGLContext::BufferData(
    GLenum target, const dom::Nullable<dom::ArrayBuffer>& maybeSrc,
    GLenum usage) {
  const FuncScope funcScope(*this, "bufferData");
  if (!ValidateNonNull("src", maybeSrc)) return;
  const auto& src = maybeSrc.Value();

  src.ProcessFixedData([&](const Span<const uint8_t>& aData) {
    Run<RPROC(BufferData)>(target, aData, usage);
  });
}

void ClientWebGLContext::BufferData(GLenum target,
                                    const dom::ArrayBufferView& src,
                                    GLenum usage, GLuint srcElemOffset,
                                    GLuint srcElemCountOverride) {
  const FuncScope funcScope(*this, "bufferData");
  size_t elemSize = SizeOfViewElem(src);
  src.ProcessFixedData([&](const Span<uint8_t>& aData) {
    const auto& range =
        ValidateArrayBufferView(aData, elemSize, srcElemOffset,
                                srcElemCountOverride, LOCAL_GL_INVALID_VALUE);
    if (!range) {
      return;
    }
    Run<RPROC(BufferData)>(target, *range, usage);
  });
}

////

void ClientWebGLContext::BufferSubData(GLenum target,
                                       WebGLsizeiptr dstByteOffset,
                                       const dom::ArrayBuffer& src) {
  const FuncScope funcScope(*this, "bufferSubData");
  src.ProcessFixedData([&](const Span<const uint8_t>& aData) {
    Run<RPROC(BufferSubData)>(target, dstByteOffset, aData,
                              /* unsynchronized */ false);
  });
}

void ClientWebGLContext::BufferSubData(GLenum target,
                                       WebGLsizeiptr dstByteOffset,
                                       const dom::ArrayBufferView& src,
                                       GLuint srcElemOffset,
                                       GLuint srcElemCountOverride) {
  const FuncScope funcScope(*this, "bufferSubData");
  size_t elemSize = SizeOfViewElem(src);
  src.ProcessFixedData([&](const Span<uint8_t>& aData) {
    const auto& range =
        ValidateArrayBufferView(aData, elemSize, srcElemOffset,
                                srcElemCountOverride, LOCAL_GL_INVALID_VALUE);
    if (!range) {
      return;
    }
    Run<RPROC(BufferSubData)>(target, dstByteOffset, *range,
                              /* unsynchronized */ false);
  });
}

void ClientWebGLContext::CopyBufferSubData(GLenum readTarget,
                                           GLenum writeTarget,
                                           GLintptr readOffset,
                                           GLintptr writeOffset,
                                           GLsizeiptr size) {
  const FuncScope funcScope(*this, "copyBufferSubData");
  if (!ValidateNonNegative("readOffset", readOffset) ||
      !ValidateNonNegative("writeOffset", writeOffset) ||
      !ValidateNonNegative("size", size)) {
    return;
  }
  Run<RPROC(CopyBufferSubData)>(
      readTarget, writeTarget, static_cast<uint64_t>(readOffset),
      static_cast<uint64_t>(writeOffset), static_cast<uint64_t>(size));
}

// -------------------------- Framebuffer Objects --------------------------

void ClientWebGLContext::BindFramebuffer(const GLenum target,
                                         WebGLFramebufferJS* const fb) {
  const FuncScope funcScope(*this, "bindFramebuffer");
  if (IsContextLost()) return;
  if (fb && !fb->ValidateUsable(*this, "fb")) return;

  if (!IsFramebufferTarget(mIsWebGL2, target)) {
    EnqueueError_ArgEnum("target", target);
    return;
  }

  // -

  auto& state = State();

  switch (target) {
    case LOCAL_GL_FRAMEBUFFER:
      state.mBoundDrawFb = fb;
      state.mBoundReadFb = fb;
      break;

    case LOCAL_GL_DRAW_FRAMEBUFFER:
      state.mBoundDrawFb = fb;
      break;
    case LOCAL_GL_READ_FRAMEBUFFER:
      state.mBoundReadFb = fb;
      break;

    default:
      MOZ_CRASH();
  }

  // -

  if (fb) {
    fb->mHasBeenBound = true;
  }

  Run<RPROC(BindFramebuffer)>(target, fb ? fb->mId : 0);
}

// -

void ClientWebGLContext::FramebufferTexture2D(GLenum target, GLenum attachSlot,
                                              GLenum bindImageTarget,
                                              WebGLTextureJS* const tex,
                                              GLint mipLevel) const {
  const FuncScope funcScope(*this, "framebufferTexture2D");
  if (IsContextLost()) return;

  const auto bindTexTarget = ImageToTexTarget(bindImageTarget);
  uint32_t zLayer = 0;
  switch (bindTexTarget) {
    case LOCAL_GL_TEXTURE_2D:
      break;
    case LOCAL_GL_TEXTURE_CUBE_MAP:
      zLayer = bindImageTarget - LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_X;
      break;
    default:
      EnqueueError_ArgEnum("imageTarget", bindImageTarget);
      return;
  }

  if (!mIsWebGL2 &&
      !IsExtensionEnabled(WebGLExtensionID::OES_fbo_render_mipmap)) {
    if (mipLevel != 0) {
      EnqueueError(LOCAL_GL_INVALID_VALUE,
                   "mipLevel != 0 requires OES_fbo_render_mipmap.");
      return;
    }
  }

  FramebufferAttach(target, attachSlot, bindImageTarget, nullptr, tex,
                    static_cast<uint32_t>(mipLevel), zLayer, 0);
}

Maybe<webgl::ErrorInfo> CheckFramebufferAttach(const GLenum bindImageTarget,
                                               const GLenum curTexTarget,
                                               const uint32_t mipLevel,
                                               const uint32_t zLayerBase,
                                               const uint32_t zLayerCount,
                                               const webgl::Limits& limits) {
  if (!curTexTarget) {
    return Some(
        webgl::ErrorInfo{LOCAL_GL_INVALID_OPERATION,
                         "`tex` not yet bound. Call bindTexture first."});
  }

  auto texTarget = curTexTarget;
  if (bindImageTarget) {
    // FramebufferTexture2D
    const auto bindTexTarget = ImageToTexTarget(bindImageTarget);
    if (curTexTarget != bindTexTarget) {
      return Some(webgl::ErrorInfo{LOCAL_GL_INVALID_OPERATION,
                                   "`tex` cannot be rebound to a new target."});
    }

    switch (bindTexTarget) {
      case LOCAL_GL_TEXTURE_2D:
      case LOCAL_GL_TEXTURE_CUBE_MAP:
        break;
      default:
        return Some(webgl::ErrorInfo{LOCAL_GL_INVALID_ENUM,
                                     "`tex` must have been bound to target "
                                     "TEXTURE_2D or TEXTURE_CUBE_MAP."});
    }
    texTarget = bindTexTarget;
  } else {
    // FramebufferTextureLayer/Multiview
    switch (curTexTarget) {
      case LOCAL_GL_TEXTURE_2D_ARRAY:
      case LOCAL_GL_TEXTURE_3D:
        break;
      default:
        return Some(webgl::ErrorInfo{LOCAL_GL_INVALID_OPERATION,
                                     "`tex` must have been bound to target "
                                     "TEXTURE_2D_ARRAY or TEXTURE_3D."});
    }
  }
  MOZ_ASSERT(texTarget);
  uint32_t maxSize;
  uint32_t maxZ;
  switch (texTarget) {
    case LOCAL_GL_TEXTURE_2D:
      maxSize = limits.maxTex2dSize;
      maxZ = 1;
      break;
    case LOCAL_GL_TEXTURE_CUBE_MAP:
      maxSize = limits.maxTexCubeSize;
      maxZ = 6;
      break;
    case LOCAL_GL_TEXTURE_2D_ARRAY:
      maxSize = limits.maxTex2dSize;
      maxZ = limits.maxTexArrayLayers;
      break;
    case LOCAL_GL_TEXTURE_3D:
      maxSize = limits.maxTex3dSize;
      maxZ = limits.maxTex3dSize;
      break;
    default:
      MOZ_CRASH();
  }
  const auto maxMipLevel = FloorLog2(maxSize);
  if (mipLevel > maxMipLevel) {
    return Some(webgl::ErrorInfo{LOCAL_GL_INVALID_VALUE,
                                 "`mipLevel` too large for texture target."});
  }
  const auto requiredZLayers = CheckedInt<uint32_t>(zLayerBase) + zLayerCount;
  if (!requiredZLayers.isValid() || requiredZLayers.value() > maxZ) {
    return Some(webgl::ErrorInfo{LOCAL_GL_INVALID_VALUE,
                                 "`zLayer` too large for texture target."});
  }

  return {};
}

void ClientWebGLContext::FramebufferAttach(
    const GLenum target, const GLenum attachSlot, const GLenum bindImageTarget,
    WebGLRenderbufferJS* const rb, WebGLTextureJS* const tex,
    const uint32_t mipLevel, const uint32_t zLayerBase,
    const uint32_t numViewLayers) const {
  if (rb && !rb->ValidateUsable(*this, "rb")) return;
  if (tex && !tex->ValidateUsable(*this, "tex")) return;
  const auto& state = State();
  const auto& limits = Limits();

  if (!IsFramebufferTarget(mIsWebGL2, target)) {
    EnqueueError_ArgEnum("target", target);
    return;
  }
  auto fb = state.mBoundDrawFb;
  if (target == LOCAL_GL_READ_FRAMEBUFFER) {
    fb = state.mBoundReadFb;
  }
  if (!fb) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "No framebuffer bound.");
    return;
  }

  if (fb->mOpaque) {
    EnqueueError(
        LOCAL_GL_INVALID_OPERATION,
        "An opaque framebuffer's attachments cannot be inspected or changed.");
    return;
  }

  // -
  // Multiview-specific validation skipped by Host.

  if (tex && numViewLayers) {
    if (tex->mTarget != LOCAL_GL_TEXTURE_2D_ARRAY) {
      EnqueueError(LOCAL_GL_INVALID_OPERATION,
                   "`tex` must have been bound to target TEXTURE_2D_ARRAY.");
      return;
    }
    if (numViewLayers > limits.maxMultiviewLayers) {
      EnqueueError(LOCAL_GL_INVALID_VALUE,
                   "`numViews` (%u) must be <= MAX_VIEWS (%u).", numViewLayers,
                   limits.maxMultiviewLayers);
      return;
    }
  }

  // -

  webgl::ObjectId id = 0;
  if (tex) {
    auto zLayerCount = numViewLayers;
    if (!zLayerCount) {
      zLayerCount = 1;
    }
    const auto err =
        CheckFramebufferAttach(bindImageTarget, tex->mTarget, mipLevel,
                               zLayerBase, zLayerCount, limits);
    if (err) {
      EnqueueError(err->type, "%s", err->info.c_str());
      return;
    }
    id = tex->mId;
  } else if (rb) {
    if (!rb->mHasBeenBound) {
      EnqueueError(LOCAL_GL_INVALID_OPERATION,
                   "`rb` has not yet been bound with BindRenderbuffer.");
      return;
    }
    id = rb->mId;
  }

  // Ready!
  // But DEPTH_STENCIL in webgl2 is actually two slots!

  const auto fnAttachTo = [&](const GLenum actualAttachSlot) {
    const auto slot = fb->GetAttachment(actualAttachSlot);
    if (!slot) {
      EnqueueError_ArgEnum("attachment", actualAttachSlot);
      return;
    }

    slot->rb = rb;
    slot->tex = tex;

    Run<RPROC(FramebufferAttach)>(target, actualAttachSlot, bindImageTarget, id,
                                  mipLevel, zLayerBase, numViewLayers);
  };

  if (mIsWebGL2 && attachSlot == LOCAL_GL_DEPTH_STENCIL_ATTACHMENT) {
    fnAttachTo(LOCAL_GL_DEPTH_ATTACHMENT);
    fnAttachTo(LOCAL_GL_STENCIL_ATTACHMENT);
  } else {
    fnAttachTo(attachSlot);
  }

  if (bindImageTarget) {
    if (rb) {
      rb->mHasBeenBound = true;
    }
    if (tex) {
      tex->mTarget = ImageToTexTarget(bindImageTarget);
    }
  }
}

// -

void ClientWebGLContext::BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1,
                                         GLint srcY1, GLint dstX0, GLint dstY0,
                                         GLint dstX1, GLint dstY1,
                                         GLbitfield mask, GLenum filter) {
  Run<RPROC(BlitFramebuffer)>(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1,
                              dstY1, mask, filter);

  AfterDrawCall();
}

void ClientWebGLContext::InvalidateFramebuffer(
    GLenum target, const dom::Sequence<GLenum>& attachments,
    ErrorResult& unused) {
  Run<RPROC(InvalidateFramebuffer)>(target, Span{attachments});

  // Never invalidate the backbuffer, so never needs AfterDrawCall.
}

void ClientWebGLContext::InvalidateSubFramebuffer(
    GLenum target, const dom::Sequence<GLenum>& attachments, GLint x, GLint y,
    GLsizei width, GLsizei height, ErrorResult& unused) {
  Run<RPROC(InvalidateSubFramebuffer)>(target, Span{attachments}, x, y, width,
                                       height);

  // Never invalidate the backbuffer, so never needs AfterDrawCall.
}

void ClientWebGLContext::ReadBuffer(GLenum mode) {
  Run<RPROC(ReadBuffer)>(mode);
}

// ----------------------- Renderbuffer objects -----------------------

void ClientWebGLContext::BindRenderbuffer(const GLenum target,
                                          WebGLRenderbufferJS* const rb) {
  const FuncScope funcScope(*this, "bindRenderbuffer");
  if (IsContextLost()) return;
  if (rb && !rb->ValidateUsable(*this, "rb")) return;
  auto& state = State();

  if (target != LOCAL_GL_RENDERBUFFER) {
    EnqueueError_ArgEnum("target", target);
    return;
  }

  state.mBoundRb = rb;
  if (rb) {
    rb->mHasBeenBound = true;
  }
}

void ClientWebGLContext::RenderbufferStorageMultisample(GLenum target,
                                                        GLsizei samples,
                                                        GLenum internalFormat,
                                                        GLsizei width,
                                                        GLsizei height) const {
  const FuncScope funcScope(*this, "renderbufferStorageMultisample");
  if (IsContextLost()) return;

  if (target != LOCAL_GL_RENDERBUFFER) {
    EnqueueError_ArgEnum("target", target);
    return;
  }

  const auto& state = State();

  const auto& rb = state.mBoundRb;
  if (!rb) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "No renderbuffer bound");
    return;
  }

  if (!ValidateNonNegative("width", width) ||
      !ValidateNonNegative("height", height) ||
      !ValidateNonNegative("samples", samples)) {
    return;
  }

  if (internalFormat == LOCAL_GL_DEPTH_STENCIL && samples > 0) {
    // While our backend supports it trivially, the spec forbids it.
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "WebGL 1's DEPTH_STENCIL format may not be multisampled. Use "
                 "DEPTH24_STENCIL8 when `samples > 0`.");
    return;
  }

  Run<RPROC(RenderbufferStorageMultisample)>(
      rb->mId, static_cast<uint32_t>(samples), internalFormat,
      static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

// --------------------------- Texture objects ---------------------------

void ClientWebGLContext::ActiveTexture(const GLenum texUnitEnum) {
  const FuncScope funcScope(*this, "activeTexture");
  if (IsContextLost()) return;

  if (texUnitEnum < LOCAL_GL_TEXTURE0) {
    EnqueueError(LOCAL_GL_INVALID_VALUE,
                 "`texture` (0x%04x) must be >= TEXTURE0 (0x%04x).",
                 texUnitEnum, LOCAL_GL_TEXTURE0);
    return;
  }

  const auto texUnit = texUnitEnum - LOCAL_GL_TEXTURE0;

  auto& state = State();
  if (texUnit >= state.mTexUnits.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE,
                 "TEXTURE%u must be < MAX_COMBINED_TEXTURE_IMAGE_UNITS (%zu).",
                 texUnit, state.mTexUnits.size());
    return;
  }

  //-

  state.mActiveTexUnit = texUnit;
  Run<RPROC(ActiveTexture)>(texUnit);
}

static bool IsTexTarget(const GLenum texTarget, const bool webgl2) {
  switch (texTarget) {
    case LOCAL_GL_TEXTURE_2D:
    case LOCAL_GL_TEXTURE_CUBE_MAP:
      return true;

    case LOCAL_GL_TEXTURE_2D_ARRAY:
    case LOCAL_GL_TEXTURE_3D:
      return webgl2;

    default:
      return false;
  }
}

void ClientWebGLContext::BindTexture(const GLenum texTarget,
                                     WebGLTextureJS* const tex) {
  const FuncScope funcScope(*this, "bindTexture");
  if (IsContextLost()) return;
  if (tex && !tex->ValidateUsable(*this, "tex")) return;

  if (!IsTexTarget(texTarget, mIsWebGL2)) {
    EnqueueError_ArgEnum("texTarget", texTarget);
    return;
  }

  if (tex && tex->mTarget) {
    if (texTarget != tex->mTarget) {
      EnqueueError(LOCAL_GL_INVALID_OPERATION,
                   "Texture previously bound to %s cannot be bound now to %s.",
                   EnumString(tex->mTarget).c_str(),
                   EnumString(texTarget).c_str());
      return;
    }
  }

  auto& state = State();
  auto& texUnit = state.mTexUnits[state.mActiveTexUnit];
  texUnit.texByTarget[texTarget] = tex;
  if (tex) {
    tex->mTarget = texTarget;
  }

  Run<RPROC(BindTexture)>(texTarget, tex ? tex->mId : 0);
}

void ClientWebGLContext::GenerateMipmap(GLenum texTarget) const {
  Run<RPROC(GenerateMipmap)>(texTarget);
}

void ClientWebGLContext::GetTexParameter(
    JSContext* cx, GLenum texTarget, GLenum pname,
    JS::MutableHandle<JS::Value> retval) const {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getTexParameter");
  if (IsContextLost()) return;
  auto& state = State();

  auto& texUnit = state.mTexUnits[state.mActiveTexUnit];

  const auto& tex = Find(texUnit.texByTarget, texTarget, nullptr);
  if (!tex) {
    if (!IsTexTarget(texTarget, mIsWebGL2)) {
      EnqueueError_ArgEnum("texTarget", texTarget);
    } else {
      EnqueueError(LOCAL_GL_INVALID_OPERATION, "No texture bound to %s[%u].",
                   EnumString(texTarget).c_str(), state.mActiveTexUnit);
    }
    return;
  }

  const auto maybe = [&]() {
    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      return inProcess->GetTexParameter(tex->mId, pname);
    }
    const auto& child = mNotLost->outOfProcess;
    child->FlushPendingCmds();
    Maybe<double> ret;
    if (!child->SendGetTexParameter(tex->mId, pname, &ret)) {
      ret.reset();
    }
    return ret;
  }();

  if (maybe) {
    switch (pname) {
      case LOCAL_GL_TEXTURE_IMMUTABLE_FORMAT:
        retval.set(JS::BooleanValue(*maybe));
        break;

      default:
        retval.set(JS::NumberValue(*maybe));
        break;
    }
  }
}

void ClientWebGLContext::TexParameterf(GLenum texTarget, GLenum pname,
                                       GLfloat param) {
  Run<RPROC(TexParameter_base)>(texTarget, pname, FloatOrInt(param));
}

void ClientWebGLContext::TexParameteri(GLenum texTarget, GLenum pname,
                                       GLint param) {
  Run<RPROC(TexParameter_base)>(texTarget, pname, FloatOrInt(param));
}

////////////////////////////////////

static GLenum JSTypeMatchUnpackTypeError(GLenum unpackType,
                                         js::Scalar::Type jsType) {
  bool matches = false;
  switch (unpackType) {
    case LOCAL_GL_BYTE:
      matches = (jsType == js::Scalar::Type::Int8);
      break;

    case LOCAL_GL_UNSIGNED_BYTE:
      matches = (jsType == js::Scalar::Type::Uint8 ||
                 jsType == js::Scalar::Type::Uint8Clamped);
      break;

    case LOCAL_GL_SHORT:
      matches = (jsType == js::Scalar::Type::Int16);
      break;

    case LOCAL_GL_UNSIGNED_SHORT:
    case LOCAL_GL_UNSIGNED_SHORT_4_4_4_4:
    case LOCAL_GL_UNSIGNED_SHORT_5_5_5_1:
    case LOCAL_GL_UNSIGNED_SHORT_5_6_5:
    case LOCAL_GL_HALF_FLOAT:
    case LOCAL_GL_HALF_FLOAT_OES:
      matches = (jsType == js::Scalar::Type::Uint16);
      break;

    case LOCAL_GL_INT:
      matches = (jsType == js::Scalar::Type::Int32);
      break;

    case LOCAL_GL_UNSIGNED_INT:
    case LOCAL_GL_UNSIGNED_INT_2_10_10_10_REV:
    case LOCAL_GL_UNSIGNED_INT_10F_11F_11F_REV:
    case LOCAL_GL_UNSIGNED_INT_5_9_9_9_REV:
    case LOCAL_GL_UNSIGNED_INT_24_8:
      matches = (jsType == js::Scalar::Type::Uint32);
      break;

    case LOCAL_GL_FLOAT:
      matches = (jsType == js::Scalar::Type::Float32);
      break;

    case LOCAL_GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
      matches = false;  // No valid jsType, but we allow uploads with null.
      break;

    default:
      return LOCAL_GL_INVALID_ENUM;
  }
  if (!matches) return LOCAL_GL_INVALID_OPERATION;
  return 0;
}

/////////////////////////////////////////////////

static inline uvec2 CastUvec2(const ivec2& val) {
  return {static_cast<uint32_t>(val.x), static_cast<uint32_t>(val.y)};
}

static inline uvec3 CastUvec3(const ivec3& val) {
  return {static_cast<uint32_t>(val.x), static_cast<uint32_t>(val.y),
          static_cast<uint32_t>(val.z)};
}

template <typename T>
Range<T> SubRange(const Range<T>& full, const size_t offset,
                  const size_t length) {
  const auto newBegin = full.begin() + offset;
  return Range<T>{newBegin, newBegin + length};
}

Maybe<Span<const uint8_t>> GetRangeFromData(const Span<uint8_t>& data,
                                            size_t bytesPerElem,
                                            GLuint elemOffset,
                                            GLuint elemCountOverride) {
  auto elemCount = data.size() / bytesPerElem;
  if (elemOffset > elemCount) return {};
  elemCount -= elemOffset;

  if (elemCountOverride) {
    if (elemCountOverride > elemCount) return {};
    elemCount = elemCountOverride;
  }
  return Some(
      data.subspan(elemOffset * bytesPerElem, elemCount * bytesPerElem));
}

// -

static bool IsTexTargetForDims(const GLenum texTarget, const bool webgl2,
                               const uint8_t funcDims) {
  if (!IsTexTarget(texTarget, webgl2)) return false;
  switch (texTarget) {
    case LOCAL_GL_TEXTURE_2D:
    case LOCAL_GL_TEXTURE_CUBE_MAP:
      return funcDims == 2;

    default:
      return funcDims == 3;
  }
}

void ClientWebGLContext::TexStorage(uint8_t funcDims, GLenum texTarget,
                                    GLsizei levels, GLenum internalFormat,
                                    const ivec3& size) const {
  const FuncScope funcScope(*this, "texStorage[23]D");
  if (IsContextLost()) return;
  if (!IsTexTargetForDims(texTarget, mIsWebGL2, funcDims)) {
    EnqueueError_ArgEnum("texTarget", texTarget);
    return;
  }
  Run<RPROC(TexStorage)>(texTarget, static_cast<uint32_t>(levels),
                         internalFormat, CastUvec3(size));
}

// -

void webgl::TexUnpackBlobDesc::Shrink(const webgl::PackingInfo& pi) {
  if (cpuData) {
    if (!size.x || !size.y || !size.z) return;

    const auto unpackRes = ExplicitUnpacking(pi, {});
    if (!unpackRes.isOk()) {
      return;
    }
    const auto& unpack = unpackRes.inspect();

    const auto bytesUpperBound =
        CheckedInt<size_t>(unpack.metrics.bytesPerRowStride) *
        unpack.metrics.totalRows;
    if (bytesUpperBound.isValid()) {
      auto& span = *cpuData;
      span = span.subspan(0, std::min(span.size(), bytesUpperBound.value()));
    }
  }
}

// -

void ClientWebGLContext::TexImage(uint8_t funcDims, GLenum imageTarget,
                                  GLint level, GLenum respecFormat,
                                  const ivec3& offset,
                                  const Maybe<ivec3>& isize, GLint border,
                                  const webgl::PackingInfo& pi,
                                  const TexImageSource& src) const {
  const FuncScope funcScope(*this, "tex(Sub)Image[23]D");
  if (IsContextLost()) return;
  if (!IsTexTargetForDims(ImageToTexTarget(imageTarget), mIsWebGL2, funcDims)) {
    EnqueueError_ArgEnum("imageTarget", imageTarget);
    return;
  }
  if (border != 0) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`border` must be 0.");
    return;
  }

  Maybe<uvec3> size;
  if (isize) {
    size = Some(CastUvec3(isize.value()));
  }

  // -

  // -
  bool isDataUpload = false;
  auto desc = [&]() -> Maybe<webgl::TexUnpackBlobDesc> {
    if (src.mPboOffset) {
      isDataUpload = true;
      const auto offset = static_cast<uint64_t>(*src.mPboOffset);
      return Some(webgl::TexUnpackBlobDesc{imageTarget,
                                           size.value(),
                                           gfxAlphaType::NonPremult,
                                           {},
                                           Some(offset)});
    }

    if (src.mView) {
      isDataUpload = true;
      const auto& view = *src.mView;
      const auto& jsType = view.Type();
      const auto err = JSTypeMatchUnpackTypeError(pi.type, jsType);
      switch (err) {
        case LOCAL_GL_INVALID_ENUM:
          EnqueueError_ArgEnum("unpackType", pi.type);
          return {};
        case LOCAL_GL_INVALID_OPERATION:
          EnqueueError(LOCAL_GL_INVALID_OPERATION,
                       "ArrayBufferView type %s not compatible with `type` %s.",
                       name(jsType), EnumString(pi.type).c_str());
          return {};
        default:
          break;
      }

      return view.ProcessData(
          [&](const Span<uint8_t>& aData,
              JS::AutoCheckCannotGC&& nogc) -> Maybe<webgl::TexUnpackBlobDesc> {
            const auto range = GetRangeFromData(aData, SizeOfViewElem(view),
                                                src.mViewElemOffset,
                                                src.mViewElemLengthOverride);
            if (!range) {
              nogc.reset();
              EnqueueError(LOCAL_GL_INVALID_OPERATION, "`source` too small.");
              return {};
            }
            return Some(webgl::TexUnpackBlobDesc{imageTarget,
                                                 size.value(),
                                                 gfxAlphaType::NonPremult,
                                                 Some(*range),
                                                 {}});
          });
    }

    if (src.mImageBitmap) {
      return webgl::FromImageBitmap(imageTarget, size, *(src.mImageBitmap),
                                    src.mOut_error);
    }

    if (src.mImageData) {
      const auto& imageData = *src.mImageData;
      dom::Uint8ClampedArray scopedArr;
      MOZ_RELEASE_ASSERT(scopedArr.Init(imageData.GetDataObject()));

      return scopedArr.ProcessData(
          [&](const Span<uint8_t>& aData,
              JS::AutoCheckCannotGC&& nogc) -> Maybe<webgl::TexUnpackBlobDesc> {
            const auto dataSize = aData.Length();
            const auto data = aData.Elements();
            if (dataSize == 0) {
              nogc.reset();  // aData will not be used.
              EnqueueError(
                  LOCAL_GL_INVALID_VALUE,
                  "ImageData.data.buffer is Detached. (Maybe you Transfered "
                  "it to a Worker?");
              return {};
            }

            // -

            const gfx::IntSize imageSize(imageData.Width(), imageData.Height());
            const auto sizeFromDims =
                CheckedInt<size_t>(imageSize.width) * imageSize.height * 4;
            MOZ_RELEASE_ASSERT(sizeFromDims.isValid() &&
                               sizeFromDims.value() == dataSize);

            const RefPtr<gfx::DataSourceSurface> surf =
                gfx::Factory::CreateWrappingDataSourceSurface(
                    data, imageSize.width * 4, imageSize,
                    gfx::SurfaceFormat::R8G8B8A8);
            MOZ_ASSERT(surf);

            // -

            const auto imageUSize = *uvec2::FromSize(imageSize);
            const auto concreteSize =
                size.valueOr(uvec3{imageUSize.x, imageUSize.y, 1});

            // WhatWG "HTML Living Standard" (30 October 2015):
            // "The getImageData(sx, sy, sw, sh) method [...] Pixels must be
            // returned as non-premultiplied alpha values."
            auto result =
                Some(webgl::TexUnpackBlobDesc{imageTarget,
                                              concreteSize,
                                              gfxAlphaType::NonPremult,
                                              {},
                                              {},
                                              Some(imageUSize),
                                              nullptr,
                                              {},
                                              surf});
            nogc.reset();  // Done with aData
            return result;
          });
    }

    if (src.mOffscreenCanvas) {
      return webgl::FromOffscreenCanvas(
          *this, imageTarget, size, *(src.mOffscreenCanvas), src.mOut_error);
    }

    if (src.mVideoFrame) {
      return webgl::FromVideoFrame(*this, imageTarget, size, *(src.mVideoFrame),
                                   src.mOut_error);
    }

    if (src.mDomElem) {
      return webgl::FromDomElem(*this, imageTarget, size, *(src.mDomElem),
                                src.mOut_error);
    }

    return Some(webgl::TexUnpackBlobDesc{
        imageTarget, size.value(), gfxAlphaType::NonPremult, {}, {}});
  }();
  if (!desc) {
    return;
  }

  // -

  const auto& rawUnpacking = State().mPixelUnpackState;
  {
    auto defaultSubrectState = webgl::PixelPackingState{};
    defaultSubrectState.alignmentInTypeElems =
        rawUnpacking.alignmentInTypeElems;
    const bool isSubrect = (rawUnpacking != defaultSubrectState);
    if (isDataUpload && isSubrect) {
      if (rawUnpacking.flipY || rawUnpacking.premultiplyAlpha) {
        EnqueueError(LOCAL_GL_INVALID_OPERATION,
                     "Non-DOM-Element uploads with alpha-premult"
                     " or y-flip do not support subrect selection.");
        return;
      }
    }
  }
  desc->unpacking = rawUnpacking;

  if (desc->structuredSrcSize) {
    // WebGL 2 spec:
    //   ### 5.35 Pixel store parameters for uploads from TexImageSource
    //   UNPACK_ALIGNMENT and UNPACK_ROW_LENGTH are ignored.
    const auto& elemSize = *desc->structuredSrcSize;
    desc->unpacking.alignmentInTypeElems = 1;
    desc->unpacking.rowLength = elemSize.x;
  }
  if (!desc->unpacking.rowLength) {
    desc->unpacking.rowLength = desc->size.x;
  }
  if (!desc->unpacking.imageHeight) {
    desc->unpacking.imageHeight = desc->size.y;
  }

  // -

  // -

  mozilla::ipc::Shmem* pShmem = nullptr;
  // Image to release after WebGLContext::TexImage().
  RefPtr<layers::Image> keepAliveImage;
  RefPtr<gfx::SourceSurface> keepAliveSurf;

  if (desc->sd) {
    const auto& sd = *(desc->sd);
    const auto sdType = sd.type();
    const auto& contextInfo = mNotLost->info;

    // TODO (Bug 754256): Figure out the source colorSpace.
    const auto& webgl = this;
    dom::PredefinedColorSpace srcColorSpace = dom::PredefinedColorSpace::Srgb;
    dom::PredefinedColorSpace dstColorSpace =
        webgl->mUnpackColorSpace ? *webgl->mUnpackColorSpace
                                 : dom::PredefinedColorSpace::Srgb;
    bool sameColorSpace = (srcColorSpace == dstColorSpace);

    const auto fallbackReason = [&]() -> Maybe<std::string> {
      const bool canUploadViaSd = contextInfo.uploadableSdTypes[sdType];
      // Canvas2D surfaces may require and depend upon conversions such as
      // unpremultiplying the source data. We allow these conversions to occur
      // because it is still a performance benefit to do the conversion in the
      // GPU process where WebGL processing happens, rather than cause excess
      // synchronization and data transfer back to the content process.
      const bool allowConversion =
          canUploadViaSd &&
          sdType == layers::SurfaceDescriptor::TSurfaceDescriptorCanvasSurface;
      auto fallbackReason =
          BlitPreventReason(level, offset, respecFormat, pi, *desc,
                            contextInfo.optionalRenderableFormatBits,
                            sameColorSpace, allowConversion);
      if (fallbackReason) {
        return fallbackReason;
      }

      if (!canUploadViaSd) {
        const nsPrintfCString msg(
            "Fast uploads for resource type %i not implemented.", int(sdType));
        return Some(ToString(msg));
      }

      switch (sdType) {
        default:
          break;
        case layers::SurfaceDescriptor::TSurfaceDescriptorBuffer: {
          const auto& sdb = sd.get_SurfaceDescriptorBuffer();
          const auto& data = sdb.data();
          if (data.type() == layers::MemoryOrShmem::TShmem) {
            pShmem = &data.get_Shmem();
          } else {
            return Some(
                std::string{"SurfaceDescriptorBuffer data is not Shmem."});
          }
        } break;
        case layers::SurfaceDescriptor::TSurfaceDescriptorD3D10: {
          const auto& sdD3D = sd.get_SurfaceDescriptorD3D10();
          const auto& inProcess = mNotLost->inProcess;
          MOZ_ASSERT(desc->image);
          keepAliveImage = desc->image;

          if (sdD3D.gpuProcessTextureId().isSome() && inProcess) {
            return Some(
                std::string{"gpuProcessTextureId works only in GPU process."});
          }
        } break;
        case layers::SurfaceDescriptor::TSurfaceDescriptorGPUVideo: {
          const auto& inProcess = mNotLost->inProcess;
          MOZ_ASSERT(desc->image);
          keepAliveImage = desc->image;
          if (inProcess) {
            return Some(std::string{
                "SurfaceDescriptorGPUVideo works only in GPU process."});
          }
          const auto& sdv = sd.get_SurfaceDescriptorGPUVideo();
          if (sdv.type() != layers::SurfaceDescriptorGPUVideo::
                                TSurfaceDescriptorRemoteDecoder) {
            return Some(std::string{
                "SurfaceDescriptorGPUVideo does not contain RemoteDecoder."});
          }
          const auto& sdrd = sdv.get_SurfaceDescriptorRemoteDecoder();
          const auto& subdesc = sdrd.subdesc();
          if (subdesc.type() !=
              layers::RemoteDecoderVideoSubDescriptor::Tnull_t) {
            return Some(
                std::string{"SurfaceDescriptorGPUVideo does not contain "
                            "RemoteDecoder null subdesc."});
          }
        } break;
        case layers::SurfaceDescriptor::TSurfaceDescriptorExternalImage: {
          const auto& inProcess = mNotLost->inProcess;
          MOZ_ASSERT(desc->sourceSurf);
          keepAliveSurf = desc->sourceSurf;
          if (inProcess) {
            return Some(std::string{
                "SurfaceDescriptorExternalImage works only in GPU process."});
          }
        } break;
        case layers::SurfaceDescriptor::TSurfaceDescriptorCanvasSurface: {
          const auto& inProcess = mNotLost->inProcess;
          MOZ_ASSERT(desc->sourceSurf);
          keepAliveSurf = desc->sourceSurf;
          if (inProcess) {
            return Some(std::string{
                "SurfaceDescriptorCanvasSurface works only in GPU process."});
          }
        } break;
      }

      switch (respecFormat) {
        case LOCAL_GL_SRGB:
        case LOCAL_GL_SRGB8:
        case LOCAL_GL_SRGB_ALPHA:
        case LOCAL_GL_SRGB8_ALPHA8: {
          const nsPrintfCString msg(
              "srgb-encoded formats (like %s) are not supported.",
              EnumString(respecFormat).c_str());
          return Some(ToString(msg));
        }
      }

      if (StaticPrefs::webgl_disable_DOM_blit_uploads()) {
        return Some(std::string{"DOM blit uploads are disabled."});
      }
      return {};
    }();

    if (fallbackReason) {
      EnqueuePerfWarning("Missed GPU-copy fast-path: %s",
                         fallbackReason->c_str());

      const auto& image = desc->image;
      if (image) {
        const RefPtr<gfx::SourceSurface> surf = image->GetAsSourceSurface();
        if (surf) {
          // WARNING: OSX can lose our MakeCurrent here.
          desc->sourceSurf = surf->GetDataSurface();
        }
      } else if (desc->sourceSurf && !desc->sourceSurf->IsDataSourceSurface()) {
        // If the surface descriptor type was not supported, but there is an
        // underlying source surface, convert it to a data surface for fallback
        // usage.
        desc->sourceSurf = desc->sourceSurf->GetDataSurface();
      }
      if (!desc->sourceSurf) {
        EnqueueError(LOCAL_GL_OUT_OF_MEMORY,
                     "Failed to retrieve source bytes for CPU upload.");
        return;
      }
      desc->sd = Nothing();
    }
  }
  desc->image = nullptr;
  if (desc->sd) {
    desc->sourceSurf = nullptr;
  }

  desc->Shrink(pi);

  // -

  std::shared_ptr<webgl::RaiiShmem> tempShmem;

  const bool doInlineUpload = !desc->sd;
  // Why always de-inline SDs here?
  // 1. This way we always send SDs down the same handling path, which
  // should keep things from breaking if things flip between paths because of
  // what we get handed by SurfaceFromElement etc.
  // 2. We don't actually always grab strong-refs to the resources in the SDs,
  // so we should try to use them sooner rather than later. Yes we should fix
  // this, but for now let's give the SDs the best chance of lucking out, eh?
  // :)
  // 3. It means we don't need to write QueueParamTraits<SurfaceDescriptor>.
  if (doInlineUpload) {
    // We definitely want e.g. TexImage(PBO) here.
    Run<RPROC(TexImage)>(static_cast<uint32_t>(level), respecFormat,
                         CastUvec3(offset), pi, std::move(*desc));
  } else {
    // We can't handle shmems like SurfaceDescriptorBuffer inline, so use ipdl.
    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      return inProcess->TexImage(static_cast<uint32_t>(level), respecFormat,
                                 CastUvec3(offset), pi, *desc);
    }
    const auto& child = mNotLost->outOfProcess;
    child->FlushPendingCmds();

    // The shmem we're handling was only shared from RDD to Content, and
    // immediately on Content receiving it, it was closed! RIP
    // Eventually we'll be able to make shmems that can traverse multiple
    // endpoints, but for now we need to make a new Content->WebGLParent shmem
    // and memcpy into it. We don't use `desc` elsewhere, so just replace the
    // Shmem buried within it with one that's valid for WebGLChild->Parent
    // transport.
    if (pShmem) {
      MOZ_ASSERT(desc->sd);
      const auto srcBytes = ShmemRange<uint8_t>(*pShmem);
      tempShmem = std::make_shared<webgl::RaiiShmem>();

      // We need Unsafe because we want to dictate when to destroy it from the
      // client side.
      *tempShmem = webgl::RaiiShmem::AllocUnsafe(child, srcBytes.length());
      if (!*tempShmem) {
        NS_WARNING("AllocShmem failed in TexImage");
        return;
      }
      const auto dstBytes = ShmemRange<uint8_t>(tempShmem->Shmem());
      Memcpy(&dstBytes, srcBytes.begin());

      *pShmem = tempShmem->Shmem();
      // Not Extract, because we free tempShmem manually below, after the remote
      // side has finished executing SendTexImage.
    }

    (void)child->SendTexImage(static_cast<uint32_t>(level), respecFormat,
                              CastUvec3(offset), pi, std::move(*desc));

    if (tempShmem || keepAliveImage || keepAliveSurf) {
      const auto eventTarget = GetCurrentSerialEventTarget();
      MOZ_ASSERT(eventTarget);
      child->SendPing()->Then(eventTarget, __func__,
                              [tempShmem, keepAliveImage, keepAliveSurf]() {
                                // Cleans up when (our copy of)
                                // sendableShmem/image goes out of scope.
                              });
    }
  }
}

// -

void ClientWebGLContext::CompressedTexImage(bool sub, uint8_t funcDims,
                                            GLenum imageTarget, GLint level,
                                            GLenum format, const ivec3& offset,
                                            const ivec3& isize, GLint border,
                                            const TexImageSource& src,
                                            GLsizei pboImageSize) const {
  const FuncScope funcScope(*this, "compressedTex(Sub)Image[23]D");
  if (IsContextLost()) return;
  if (!IsTexTargetForDims(ImageToTexTarget(imageTarget), mIsWebGL2, funcDims)) {
    EnqueueError_ArgEnum("imageTarget", imageTarget);
    return;
  }
  if (border != 0) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`border` must be 0.");
    return;
  }

  if (src.mView) {
    src.mView->ProcessData([&](const Span<uint8_t>& aData,
                               JS::AutoCheckCannotGC&& aNoGC) {
      const auto range =
          GetRangeFromData(aData, SizeOfViewElem(*src.mView),
                           src.mViewElemOffset, src.mViewElemLengthOverride);
      if (!range) {
        aNoGC.reset();
        EnqueueError(LOCAL_GL_INVALID_VALUE, "`source` too small.");
        return;
      }

      // We don't need to shrink `range` because valid calls require
      // `range` to match requirements exactly.

      RunWithGCData<RPROC(CompressedTexImage)>(
          std::move(aNoGC), sub, imageTarget, static_cast<uint32_t>(level),
          format, CastUvec3(offset), CastUvec3(isize), *range,
          static_cast<uint32_t>(pboImageSize), Maybe<uint64_t>());
      return;
    });
    return;
  }
  if (!src.mPboOffset) {
    MOZ_CRASH("impossible");
  }
  if (!ValidateNonNegative("offset", *src.mPboOffset)) {
    return;
  }

  Run<RPROC(CompressedTexImage)>(
      sub, imageTarget, static_cast<uint32_t>(level), format, CastUvec3(offset),
      CastUvec3(isize), Span<const uint8_t>{},
      static_cast<uint32_t>(pboImageSize), Some(*src.mPboOffset));
}

void ClientWebGLContext::CopyTexImage(uint8_t funcDims, GLenum imageTarget,
                                      GLint level, GLenum respecFormat,
                                      const ivec3& dstOffset,
                                      const ivec2& srcOffset, const ivec2& size,
                                      GLint border) const {
  const FuncScope funcScope(*this, "copy(Sub)Image[23]D");
  if (IsContextLost()) return;
  if (!IsTexTargetForDims(ImageToTexTarget(imageTarget), mIsWebGL2, funcDims)) {
    EnqueueError_ArgEnum("imageTarget", imageTarget);
    return;
  }
  if (border != 0) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`border` must be 0.");
    return;
  }
  Run<RPROC(CopyTexImage)>(imageTarget, static_cast<uint32_t>(level),
                           respecFormat, CastUvec3(dstOffset), srcOffset,
                           CastUvec2(size));
}

// ------------------- Programs and shaders --------------------------------

void ClientWebGLContext::UseProgram(WebGLProgramJS* const prog) {
  const FuncScope funcScope(*this, "useProgram");
  if (IsContextLost()) return;
  if (prog && !prog->ValidateUsable(*this, "prog")) return;

  auto& state = State();

  if (state.mTfActiveAndNotPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Transform feedback is active and not paused.");
    return;
  }

  if (prog) {
    const auto& res = GetLinkResult(*prog);
    if (!res.success) {
      EnqueueError(LOCAL_GL_INVALID_OPERATION,
                   "Program must be linked successfully.");
      return;
    }
  }

  // -

  state.mCurrentProgram = prog;
  state.mProgramKeepAlive = prog ? prog->mKeepAliveWeak.lock() : nullptr;
  state.mActiveLinkResult = prog ? prog->mResult : nullptr;

  Run<RPROC(UseProgram)>(prog ? prog->mId : 0);
}

void ClientWebGLContext::ValidateProgram(WebGLProgramJS& prog) const {
  const FuncScope funcScope(*this, "validateProgram");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "prog")) return;

  prog.mLastValidate = [&]() {
    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      return inProcess->ValidateProgram(prog.mId);
    }
    const auto& child = mNotLost->outOfProcess;
    child->FlushPendingCmds();
    bool ret = {};
    if (!child->SendValidateProgram(prog.mId, &ret)) {
      ret = {};
    }
    return ret;
  }();
}

// ------------------------ Uniforms and attributes ------------------------

Maybe<double> ClientWebGLContext::GetVertexAttribPriv(const GLuint index,
                                                      const GLenum pname) {
  const auto& inProcess = mNotLost->inProcess;
  if (inProcess) {
    return inProcess->GetVertexAttrib(index, pname);
  }
  const auto& child = mNotLost->outOfProcess;
  child->FlushPendingCmds();
  Maybe<double> ret;
  if (!child->SendGetVertexAttrib(index, pname, &ret)) {
    ret.reset();
  }
  return ret;
}

void ClientWebGLContext::GetVertexAttrib(JSContext* cx, GLuint index,
                                         GLenum pname,
                                         JS::MutableHandle<JS::Value> retval,
                                         ErrorResult& rv) {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getVertexAttrib");
  if (IsContextLost()) return;
  const auto& state = State();

  const auto& genericAttribs = state.mGenericVertexAttribs;
  if (index >= genericAttribs.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`index` (%u) >= MAX_VERTEX_ATTRIBS",
                 index);
    return;
  }

  switch (pname) {
    case LOCAL_GL_CURRENT_VERTEX_ATTRIB: {
      const auto& attrib = genericAttribs[index];
      switch (attrib.type) {
        case webgl::AttribBaseType::Float: {
          const auto ptr = reinterpret_cast<const float*>(attrib.data.data());
          retval.setObjectOrNull(
              dom::Float32Array::Create(cx, this, Span(ptr, 4), rv));
          break;
        }
        case webgl::AttribBaseType::Int: {
          const auto ptr = reinterpret_cast<const int32_t*>(attrib.data.data());
          retval.setObjectOrNull(
              dom::Int32Array::Create(cx, this, Span(ptr, 4), rv));
          break;
        }
        case webgl::AttribBaseType::Uint: {
          const auto ptr =
              reinterpret_cast<const uint32_t*>(attrib.data.data());
          retval.setObjectOrNull(
              dom::Uint32Array::Create(cx, this, Span(ptr, 4), rv));
          break;
        }
        case webgl::AttribBaseType::Boolean:
          MOZ_CRASH("impossible");
      }

      return;
    }

    case LOCAL_GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: {
      const auto& buffers = state.mBoundVao->mAttribBuffers;
      const auto& buffer = buffers[index];
      (void)ToJSValueOrNull(cx, buffer, retval);
      return;
    }

    case LOCAL_GL_VERTEX_ATTRIB_ARRAY_POINTER:
      // Disallowed from JS, but allowed in Host.
      EnqueueError_ArgEnum("pname", pname);
      return;

    default:
      break;
  }

  const auto maybe = GetVertexAttribPriv(index, pname);
  if (maybe) {
    switch (pname) {
      case LOCAL_GL_VERTEX_ATTRIB_ARRAY_ENABLED:
      case LOCAL_GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
      case LOCAL_GL_VERTEX_ATTRIB_ARRAY_INTEGER:
        retval.set(JS::BooleanValue(*maybe));
        break;

      default:
        retval.set(JS::NumberValue(*maybe));
        break;
    }
  }
}

void ClientWebGLContext::UniformData(const GLenum funcElemType,
                                     const WebGLUniformLocationJS* const loc,
                                     bool transpose,
                                     const Range<const uint8_t>& bytes,
                                     JS::AutoCheckCannotGC&& nogc,
                                     GLuint elemOffset,
                                     GLuint elemCountOverride) const {
  // FuncScope::~FuncScope() can GC in a failure case, so all `return`
  // statements need to `nogc.reset()` up until the `nogc` is consumed by
  // `RunWithGCData`.
  const FuncScope funcScope(*this, "uniform setter");
  if (IsContextLost()) {
    nogc.reset();
    return;
  }

  const auto& activeLinkResult = GetActiveLinkResult();
  if (!activeLinkResult) {
    nogc.reset();
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "No active linked Program.");
    return;
  }

  // -

  auto availCount = bytes.length() / sizeof(float);
  if (elemOffset > availCount) {
    nogc.reset();
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`elemOffset` too large for `data`.");
    return;
  }
  availCount -= elemOffset;
  if (elemCountOverride) {
    if (elemCountOverride > availCount) {
      nogc.reset();
      EnqueueError(LOCAL_GL_INVALID_VALUE,
                   "`elemCountOverride` too large for `data`.");
      return;
    }
    availCount = elemCountOverride;
  }

  // -

  const auto channels = ElemTypeComponents(funcElemType);
  if (!availCount || availCount % channels != 0) {
    nogc.reset();
    EnqueueError(LOCAL_GL_INVALID_VALUE,
                 "`values` length (%u) must be a positive "
                 "integer multiple of size of %s.",
                 availCount, EnumString(funcElemType).c_str());
    return;
  }

  // -

  uint32_t locId = -1;
  if (MOZ_LIKELY(loc)) {
    locId = loc->mLocation;
    if (!loc->ValidateUsable(*this, "location")) {
      nogc.reset();
      return;
    }

    // -

    const auto& reqLinkInfo = loc->mParent.lock();
    if (reqLinkInfo.get() != activeLinkResult) {
      nogc.reset();
      EnqueueError(LOCAL_GL_INVALID_OPERATION,
                   "UniformLocation is not from the current active Program.");
      return;
    }

    // -

    bool funcMatchesLocation = false;
    for (const auto allowed : loc->mValidUploadElemTypes) {
      funcMatchesLocation |= (funcElemType == allowed);
    }
    if (MOZ_UNLIKELY(!funcMatchesLocation)) {
      std::string validSetters;
      for (const auto allowed : loc->mValidUploadElemTypes) {
        validSetters += EnumString(allowed);
        validSetters += '/';
      }
      validSetters.pop_back();  // Cheekily discard the extra trailing '/'.

      nogc.reset();
      EnqueueError(LOCAL_GL_INVALID_OPERATION,
                   "Uniform's `type` requires uniform setter of type %s.",
                   validSetters.c_str());
      return;
    }
  }

  // -

  const auto begin =
      reinterpret_cast<const webgl::UniformDataVal*>(bytes.begin().get()) +
      elemOffset;
  const auto range = Span{begin, availCount};
  RunWithGCData<RPROC(UniformData)>(std::move(nogc), locId, transpose, range);
}

// -

void ClientWebGLContext::BindVertexArray(WebGLVertexArrayJS* const vao) {
  const FuncScope funcScope(*this, "bindVertexArray");
  if (IsContextLost()) return;
  if (vao && !vao->ValidateUsable(*this, "vao")) return;
  auto& state = State();

  if (vao) {
    vao->mHasBeenBound = true;
    state.mBoundVao = vao;
  } else {
    state.mBoundVao = state.mDefaultVao;
  }

  Run<RPROC(BindVertexArray)>(vao ? vao->mId : 0);
}

void ClientWebGLContext::EnableVertexAttribArray(GLuint index) {
  Run<RPROC(EnableVertexAttribArray)>(index);
}

void ClientWebGLContext::DisableVertexAttribArray(GLuint index) {
  Run<RPROC(DisableVertexAttribArray)>(index);
}

WebGLsizeiptr ClientWebGLContext::GetVertexAttribOffset(GLuint index,
                                                        GLenum pname) {
  const FuncScope funcScope(*this, "getVertexAttribOffset");
  if (IsContextLost()) return 0;

  if (pname != LOCAL_GL_VERTEX_ATTRIB_ARRAY_POINTER) {
    EnqueueError_ArgEnum("pname", pname);
    return 0;
  }

  const auto maybe = GetVertexAttribPriv(index, pname);
  if (!maybe) return 0;
  return *maybe;
}

void ClientWebGLContext::VertexAttrib4Tv(GLuint index, webgl::AttribBaseType t,
                                         const Range<const uint8_t>& src) {
  const FuncScope funcScope(*this, "vertexAttrib[1234]u?[fi]{v}");
  if (IsContextLost()) return;
  auto& state = State();

  if (src.length() / sizeof(float) < 4) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "Array must have >=4 elements.");
    return;
  }

  auto& list = state.mGenericVertexAttribs;
  if (index >= list.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE,
                 "`index` must be < MAX_VERTEX_ATTRIBS.");
    return;
  }

  auto& attrib = list[index];
  attrib.type = t;
  memcpy(attrib.data.data(), src.begin().get(), attrib.data.size());

  Run<RPROC(VertexAttrib4T)>(index, attrib);
}

// -

void ClientWebGLContext::VertexAttribDivisor(GLuint index, GLuint divisor) {
  Run<RPROC(VertexAttribDivisor)>(index, divisor);
}

// -

void ClientWebGLContext::VertexAttribPointerImpl(bool isFuncInt, GLuint index,
                                                 GLint rawChannels, GLenum type,
                                                 bool normalized,
                                                 GLsizei rawByteStrideOrZero,
                                                 WebGLintptr rawByteOffset) {
  const FuncScope funcScope(*this, "vertexAttribI?Pointer");
  if (IsContextLost()) return;
  auto& state = State();

  const auto channels = MaybeAs<uint8_t>(rawChannels);
  if (!channels) {
    EnqueueError(LOCAL_GL_INVALID_VALUE,
                 "Channel count `size` must be within [1,4].");
    return;
  }

  const auto byteStrideOrZero = MaybeAs<uint8_t>(rawByteStrideOrZero);
  if (!byteStrideOrZero) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`stride` must be within [0,255].");
    return;
  }

  if (!ValidateNonNegative("byteOffset", rawByteOffset)) return;
  const auto byteOffset = static_cast<uint64_t>(rawByteOffset);

  // -

  const webgl::VertAttribPointerDesc desc{
      isFuncInt, *channels, normalized, *byteStrideOrZero, type, byteOffset};

  const auto res = CheckVertexAttribPointer(mIsWebGL2, desc);
  if (res.isErr()) {
    const auto& err = res.inspectErr();
    EnqueueError(err.type, "%s", err.info.c_str());
    return;
  }

  auto& list = state.mBoundVao->mAttribBuffers;
  if (index >= list.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE,
                 "`index` (%u) must be < MAX_VERTEX_ATTRIBS.", index);
    return;
  }

  const auto buffer = state.mBoundBufferByTarget[LOCAL_GL_ARRAY_BUFFER];
  if (!buffer && byteOffset) {
    return EnqueueError(LOCAL_GL_INVALID_OPERATION,
                        "If ARRAY_BUFFER is null, byteOffset must be zero.");
  }

  Run<RPROC(VertexAttribPointer)>(index, desc);

  list[index] = buffer;
}

// -------------------------------- Drawing -------------------------------

void ClientWebGLContext::DrawArraysInstanced(GLenum mode, GLint first,
                                             GLsizei count, GLsizei primcount) {
  Run<RPROC(DrawArraysInstanced)>(mode, first, count, primcount);
  AfterDrawCall();
}

void ClientWebGLContext::DrawElementsInstanced(GLenum mode, GLsizei count,
                                               GLenum type, WebGLintptr offset,
                                               GLsizei primcount) {
  Run<RPROC(DrawElementsInstanced)>(mode, count, type, offset, primcount);
  AfterDrawCall();
}

// ------------------------------ Readback -------------------------------
void ClientWebGLContext::ReadPixels(GLint x, GLint y, GLsizei width,
                                    GLsizei height, GLenum format, GLenum type,
                                    WebGLsizeiptr offset,
                                    dom::CallerType aCallerType,
                                    ErrorResult& out_error) const {
  const FuncScope funcScope(*this, "readPixels");
  if (!ReadPixels_SharedPrecheck(&type, aCallerType, out_error)) return;
  const auto& state = State();
  if (!ValidateNonNegative("width", width)) return;
  if (!ValidateNonNegative("height", height)) return;
  if (!ValidateNonNegative("offset", offset)) return;

  const auto desc = webgl::ReadPixelsDesc{{x, y},
                                          *uvec2::From(width, height),
                                          {format, type},
                                          state.mPixelPackState};
  Run<RPROC(ReadPixelsPbo)>(desc, static_cast<uint64_t>(offset));
}

void ClientWebGLContext::ReadPixels(GLint x, GLint y, GLsizei width,
                                    GLsizei height, GLenum format, GLenum type,
                                    const dom::ArrayBufferView& dstData,
                                    GLuint dstElemOffset,
                                    dom::CallerType aCallerType,
                                    ErrorResult& out_error) const {
  const FuncScope funcScope(*this, "readPixels");
  if (!ReadPixels_SharedPrecheck(&type, aCallerType, out_error)) return;
  const auto& state = State();
  if (!ValidateNonNegative("width", width)) return;
  if (!ValidateNonNegative("height", height)) return;

  ////

  js::Scalar::Type reqScalarType;
  if (!GetJSScalarFromGLType(type, &reqScalarType)) {
    nsCString name;
    WebGLContext::EnumName(type, &name);
    EnqueueError(LOCAL_GL_INVALID_ENUM, "type: invalid enum value %s",
                 name.BeginReading());
    return;
  }

  auto viewElemType = dstData.Type();
  if (viewElemType == js::Scalar::Uint8Clamped) {
    viewElemType = js::Scalar::Uint8;
  }
  if (viewElemType != reqScalarType) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "`pixels` type does not match `type`.");
    return;
  }

  size_t elemSize = SizeOfViewElem(dstData);
  dstData.ProcessFixedData([&](const Span<uint8_t>& aData) {
    const auto& range = ValidateArrayBufferView(aData, elemSize, dstElemOffset,
                                                0, LOCAL_GL_INVALID_VALUE);
    if (!range) {
      return;
    }

    const auto desc = webgl::ReadPixelsDesc{{x, y},
                                            *uvec2::From(width, height),
                                            {format, type},
                                            state.mPixelPackState};
    (void)DoReadPixels(desc, *range);
  });
}

bool ClientWebGLContext::DoReadPixels(const webgl::ReadPixelsDesc& desc,
                                      const Span<uint8_t> dest) const {
  const auto notLost =
      mNotLost;  // Hold a strong-ref to prevent LoseContext=>UAF.
  if (!notLost) return false;
  const auto& inProcess = notLost->inProcess;
  if (inProcess) {
    inProcess->ReadPixelsInto(desc, dest);
    return true;
  }
  const auto& child = notLost->outOfProcess;
  child->FlushPendingCmds();
  webgl::ReadPixelsResultIpc res = {};
  if (!child->SendReadPixels(desc, dest.size(), &res)) {
    res = {};
  }
  if (!res.byteStride || !res.shmem) return false;
  const auto& byteStride = res.byteStride;
  const auto& subrect = res.subrect;
  const webgl::RaiiShmem shmem{child, res.shmem.ref()};
  if (!shmem) {
    EnqueueError(LOCAL_GL_OUT_OF_MEMORY, "Failed to map in back buffer.");
    return false;
  }

  const auto& shmemBytes = Span{shmem.ByteRange()};

  const auto pii = webgl::PackingInfoInfo::For(desc.pi);
  if (!pii) {
    gfxCriticalError() << "ReadPixels: Bad " << desc.pi;
    return false;
  }
  const auto bpp = pii->BytesPerPixel();

  const auto& packing = desc.packState;
  auto packRect = *uvec2::From(subrect.x, subrect.y);
  packRect.x += packing.skipPixels;
  packRect.y += packing.skipRows;

  const auto xByteSize = bpp * static_cast<uint32_t>(subrect.width);
  const ptrdiff_t byteOffset = packRect.y * byteStride + packRect.x * bpp;

  const auto srcSubrect = shmemBytes.subspan(byteOffset);
  const auto destSubrect = dest.subspan(byteOffset);

  for (const auto i : IntegerRange(subrect.height)) {
    const auto srcRow = srcSubrect.subspan(i * byteStride, xByteSize);
    const auto destRow = destSubrect.subspan(i * byteStride, xByteSize);
    Memcpy(&destRow, srcRow);
  }

  return true;
}

bool ClientWebGLContext::ReadPixels_SharedPrecheck(
    GLenum* const inout_readType, dom::CallerType aCallerType,
    ErrorResult& out_error) const {
  if (IsContextLost()) return false;

  GLenum validHalfFloatType = LOCAL_GL_HALF_FLOAT;
  GLenum forbiddenHalfFloatType = LOCAL_GL_HALF_FLOAT_OES;
  if (!mIsWebGL2) {
    std::swap(validHalfFloatType, forbiddenHalfFloatType);  // Tragic.
  }
  if (*inout_readType == forbiddenHalfFloatType) {
    const auto msg = fmt::format(
        FMT_STRING("For WebGL {}, for `type`, enum {} is forbidden. Use {}."),
        mIsWebGL2 ? "2" : "1", EnumString(forbiddenHalfFloatType),
        EnumString(validHalfFloatType));
    EnqueueError({LOCAL_GL_INVALID_ENUM, msg});
    return false;
  }
  // Normalize to HALF_FLOAT non-_OES internally:
  if (*inout_readType == LOCAL_GL_HALF_FLOAT_OES) {
    *inout_readType = LOCAL_GL_HALF_FLOAT;
  }

  if (mCanvasElement && mCanvasElement->IsWriteOnly() &&
      aCallerType != dom::CallerType::System) {
    JsWarning("readPixels: Not allowed");
    out_error.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return false;
  }

  return true;
}

// --------------------------------- GL Query ---------------------------------

static inline GLenum QuerySlotTarget(const GLenum specificTarget) {
  if (specificTarget == LOCAL_GL_ANY_SAMPLES_PASSED_CONSERVATIVE) {
    return LOCAL_GL_ANY_SAMPLES_PASSED;
  }
  return specificTarget;
}

void ClientWebGLContext::GetQuery(JSContext* cx, GLenum specificTarget,
                                  GLenum pname,
                                  JS::MutableHandle<JS::Value> retval) const {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getQuery");
  if (IsContextLost()) return;
  const auto& limits = Limits();
  auto& state = State();

  if (IsExtensionEnabled(WebGLExtensionID::EXT_disjoint_timer_query)) {
    if (pname == LOCAL_GL_QUERY_COUNTER_BITS) {
      switch (specificTarget) {
        case LOCAL_GL_TIME_ELAPSED_EXT:
          retval.set(JS::NumberValue(limits.queryCounterBitsTimeElapsed));
          return;

        case LOCAL_GL_TIMESTAMP_EXT:
          retval.set(JS::NumberValue(limits.queryCounterBitsTimestamp));
          return;

        default:
          EnqueueError_ArgEnum("target", specificTarget);
          return;
      }
    }
  }

  if (pname != LOCAL_GL_CURRENT_QUERY) {
    EnqueueError_ArgEnum("pname", pname);
    return;
  }

  const auto slotTarget = QuerySlotTarget(specificTarget);
  const auto& slot = MaybeFind(state.mCurrentQueryByTarget, slotTarget);
  if (!slot) {
    EnqueueError_ArgEnum("target", specificTarget);
    return;
  }

  auto query = *slot;
  if (query && query->mTarget != specificTarget) {
    query = nullptr;
  }

  (void)ToJSValueOrNull(cx, query, retval);
}

void ClientWebGLContext::GetQueryParameter(
    JSContext*, WebGLQueryJS& query, const GLenum pname,
    JS::MutableHandle<JS::Value> retval) const {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getQueryParameter");
  if (IsContextLost()) return;
  if (!query.ValidateUsable(*this, "query")) return;

  auto maybe = [&]() {
    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      return inProcess->GetQueryParameter(query.mId, pname);
    }
    const auto& child = mNotLost->outOfProcess;
    child->FlushPendingCmds();
    Maybe<double> ret;
    if (!child->SendGetQueryParameter(query.mId, pname, &ret)) {
      ret.reset();
    }
    return ret;
  }();
  if (!maybe) return;

  // We must usually wait for an event loop before the query can be available.
  const bool canBeAvailable =
      (query.mCanBeAvailable || StaticPrefs::webgl_allow_immediate_queries());
  if (!canBeAvailable) {
    if (pname != LOCAL_GL_QUERY_RESULT_AVAILABLE) {
      return;
    }
    maybe = Some(0.0);
  }

  switch (pname) {
    case LOCAL_GL_QUERY_RESULT_AVAILABLE:
      retval.set(JS::BooleanValue(*maybe));
      break;

    default:
      retval.set(JS::NumberValue(*maybe));
      break;
  }
}

void ClientWebGLContext::BeginQuery(const GLenum specificTarget,
                                    WebGLQueryJS& query) {
  const FuncScope funcScope(*this, "beginQuery");
  if (IsContextLost()) return;
  if (!query.ValidateUsable(*this, "query")) return;
  auto& state = State();

  const auto slotTarget = QuerySlotTarget(specificTarget);
  const auto& slot = MaybeFind(state.mCurrentQueryByTarget, slotTarget);
  if (!slot) {
    EnqueueError_ArgEnum("target", specificTarget);
    return;
  }

  if (*slot) {
    auto enumStr = EnumString(slotTarget);
    if (slotTarget == LOCAL_GL_ANY_SAMPLES_PASSED) {
      enumStr += "/ANY_SAMPLES_PASSED_CONSERVATIVE";
    }
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "A Query is already active for %s.", enumStr.c_str());
    return;
  }

  if (query.mTarget && query.mTarget != specificTarget) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "`query` cannot be changed to a different target.");
    return;
  }

  *slot = &query;
  query.mTarget = specificTarget;

  Run<RPROC(BeginQuery)>(specificTarget, query.mId);
}

void ClientWebGLContext::EndQuery(const GLenum specificTarget) {
  const FuncScope funcScope(*this, "endQuery");
  if (IsContextLost()) return;
  auto& state = State();

  const auto slotTarget = QuerySlotTarget(specificTarget);
  const auto& maybeSlot = MaybeFind(state.mCurrentQueryByTarget, slotTarget);
  if (!maybeSlot) {
    EnqueueError_ArgEnum("target", specificTarget);
    return;
  }
  auto& slot = *maybeSlot;
  if (!slot || slot->mTarget != specificTarget) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "No Query is active for %s.",
                 EnumString(specificTarget).c_str());
    return;
  }
  const auto query = slot;
  slot = nullptr;

  Run<RPROC(EndQuery)>(specificTarget);

  auto& availRunnable = EnsureAvailabilityRunnable();
  availRunnable.mQueries.push_back(query.get());
  query->mCanBeAvailable = false;
}

void ClientWebGLContext::QueryCounter(WebGLQueryJS& query,
                                      const GLenum target) const {
  const FuncScope funcScope(*this, "queryCounter");
  if (IsContextLost()) return;
  if (!query.ValidateUsable(*this, "query")) return;

  if (target != LOCAL_GL_TIMESTAMP) {
    EnqueueError(LOCAL_GL_INVALID_ENUM, "`target` must be TIMESTAMP.");
    return;
  }

  if (query.mTarget && query.mTarget != target) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "`query` cannot be changed to a different target.");
    return;
  }
  query.mTarget = target;

  Run<RPROC(QueryCounter)>(query.mId);

  auto& availRunnable = EnsureAvailabilityRunnable();
  availRunnable.mQueries.push_back(&query);
  query.mCanBeAvailable = false;
}

// -------------------------------- Sampler -------------------------------
void ClientWebGLContext::GetSamplerParameter(
    JSContext* cx, const WebGLSamplerJS& sampler, const GLenum pname,
    JS::MutableHandle<JS::Value> retval) const {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getSamplerParameter");
  if (IsContextLost()) return;
  if (!sampler.ValidateUsable(*this, "sampler")) return;

  const auto maybe = [&]() {
    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      return inProcess->GetSamplerParameter(sampler.mId, pname);
    }
    const auto& child = mNotLost->outOfProcess;
    child->FlushPendingCmds();
    Maybe<double> ret;
    if (!child->SendGetSamplerParameter(sampler.mId, pname, &ret)) {
      ret.reset();
    }
    return ret;
  }();
  if (maybe) {
    retval.set(JS::NumberValue(*maybe));
  }
}

void ClientWebGLContext::BindSampler(const GLuint unit,
                                     WebGLSamplerJS* const sampler) {
  const FuncScope funcScope(*this, "bindSampler");
  if (IsContextLost()) return;
  if (sampler && !sampler->ValidateUsable(*this, "sampler")) return;
  auto& state = State();

  auto& texUnits = state.mTexUnits;
  if (unit >= texUnits.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`unit` (%u) larger than %zu.", unit,
                 texUnits.size());
    return;
  }

  // -

  texUnits[unit].sampler = sampler;

  Run<RPROC(BindSampler)>(unit, sampler ? sampler->mId : 0);
}

void ClientWebGLContext::SamplerParameteri(WebGLSamplerJS& sampler,
                                           const GLenum pname,
                                           const GLint param) const {
  const FuncScope funcScope(*this, "samplerParameteri");
  if (IsContextLost()) return;
  if (!sampler.ValidateUsable(*this, "sampler")) return;

  Run<RPROC(SamplerParameteri)>(sampler.mId, pname, param);
}

void ClientWebGLContext::SamplerParameterf(WebGLSamplerJS& sampler,
                                           const GLenum pname,
                                           const GLfloat param) const {
  const FuncScope funcScope(*this, "samplerParameterf");
  if (IsContextLost()) return;
  if (!sampler.ValidateUsable(*this, "sampler")) return;

  Run<RPROC(SamplerParameterf)>(sampler.mId, pname, param);
}

// ------------------------------- GL Sync ---------------------------------

void ClientWebGLContext::GetSyncParameter(
    JSContext* const cx, WebGLSyncJS& sync, const GLenum pname,
    JS::MutableHandle<JS::Value> retval) const {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getSyncParameter");
  if (IsContextLost()) return;
  if (!sync.ValidateUsable(*this, "sync")) return;

  retval.set([&]() -> JS::Value {
    switch (pname) {
      case LOCAL_GL_OBJECT_TYPE:
        return JS::NumberValue(LOCAL_GL_SYNC_FENCE);
      case LOCAL_GL_SYNC_CONDITION:
        return JS::NumberValue(LOCAL_GL_SYNC_GPU_COMMANDS_COMPLETE);
      case LOCAL_GL_SYNC_FLAGS:
        return JS::NumberValue(0);
      case LOCAL_GL_SYNC_STATUS: {
        const auto res = ClientWaitSync(sync, 0, 0);
        const auto signaled = (res == LOCAL_GL_ALREADY_SIGNALED ||
                               res == LOCAL_GL_CONDITION_SATISFIED);
        return JS::NumberValue(signaled ? LOCAL_GL_SIGNALED
                                        : LOCAL_GL_UNSIGNALED);
      }
      default:
        EnqueueError_ArgEnum("pname", pname);
        return JS::NullValue();
    }
  }());
}

// -

GLenum ClientWebGLContext::ClientWaitSync(WebGLSyncJS& sync,
                                          const GLbitfield flags,
                                          const GLuint64 timeout) const {
  const FuncScope funcScope(*this, "clientWaitSync");
  if (IsContextLost()) return LOCAL_GL_WAIT_FAILED;
  if (!sync.ValidateUsable(*this, "sync")) return LOCAL_GL_WAIT_FAILED;

  static constexpr auto VALID_BITS = LOCAL_GL_SYNC_FLUSH_COMMANDS_BIT;
  if ((flags | VALID_BITS) != VALID_BITS) {
    EnqueueError(LOCAL_GL_INVALID_VALUE,
                 "`flags` must be SYNC_FLUSH_COMMANDS_BIT or 0.");
    return LOCAL_GL_WAIT_FAILED;
  }

  if (timeout > webgl::kMaxClientWaitSyncTimeoutNS) {
    EnqueueError(
        LOCAL_GL_INVALID_OPERATION,
        "`timeout` (%sns) must be less than MAX_CLIENT_WAIT_TIMEOUT_WEBGL "
        "(%sns).",
        ToStringWithCommas(timeout).c_str(),
        ToStringWithCommas(webgl::kMaxClientWaitSyncTimeoutNS).c_str());
    return LOCAL_GL_WAIT_FAILED;
  }

  const bool canBeAvailable =
      (sync.mCanBeAvailable || StaticPrefs::webgl_allow_immediate_queries());
  if (!canBeAvailable) {
    constexpr uint8_t WARN_AT = 100;
    if (sync.mNumQueriesBeforeFirstFrameBoundary <= WARN_AT) {
      sync.mNumQueriesBeforeFirstFrameBoundary += 1;
      if (sync.mNumQueriesBeforeFirstFrameBoundary == WARN_AT) {
        EnqueueWarning(
            "ClientWaitSync must return TIMEOUT_EXPIRED until control has"
            " returned to the user agent's main loop, but was polled %hhu "
            "times. Are you spin-locking? (only warns once)",
            sync.mNumQueriesBeforeFirstFrameBoundary);
      }
    }
    return LOCAL_GL_TIMEOUT_EXPIRED;
  }

  if (mCompletedSyncId >= sync.mId) {
    return LOCAL_GL_ALREADY_SIGNALED;
  }
  if (flags & LOCAL_GL_SYNC_FLUSH_COMMANDS_BIT) {
    Flush();
  } else {
    constexpr uint8_t WARN_AT = 100;
    if (sync.mNumQueriesWithoutFlushCommandsBit <= WARN_AT) {
      sync.mNumQueriesWithoutFlushCommandsBit += 1;
      if (sync.mNumQueriesWithoutFlushCommandsBit == WARN_AT) {
        EnqueueWarning(
            "ClientWaitSync with timeout=0 (or GetSyncParameter(SYNC_STATUS)) "
            "called %hhu times without SYNC_FLUSH_COMMANDS_BIT. If you do not "
            "flush, this sync object is not guaranteed to ever complete.",
            sync.mNumQueriesWithoutFlushCommandsBit);
      }
    }
  }
  if (!timeout) return LOCAL_GL_TIMEOUT_EXPIRED;

  // -
  // Fine, time to block:

  const auto ret = [&]() {
    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      return inProcess->ClientWaitSync(sync.mId, flags, timeout);
    }
    const auto& child = mNotLost->outOfProcess;
    child->FlushPendingCmds();
    GLenum ret = {};
    if (!child->SendClientWaitSync(sync.mId, flags, timeout, &ret)) {
      ret = {};
    }
    return ret;
  }();

  switch (ret) {
    case LOCAL_GL_CONDITION_SATISFIED:
    case LOCAL_GL_ALREADY_SIGNALED:
      OnSyncComplete(sync.mId);
      break;
  }

  return ret;
}

void ClientWebGLContext::WaitSync(const WebGLSyncJS& sync,
                                  const GLbitfield flags,
                                  const GLint64 timeout) const {
  const FuncScope funcScope(*this, "waitSync");
  if (IsContextLost()) return;
  if (!sync.ValidateUsable(*this, "sync")) return;

  if (flags != 0) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`flags` must be 0.");
    return;
  }
  if (timeout != -1) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`timeout` must be TIMEOUT_IGNORED.");
    return;
  }

  JsWarning("waitSync is a no-op.");
}

// -------------------------- Transform Feedback ---------------------------

void ClientWebGLContext::BindTransformFeedback(
    const GLenum target, WebGLTransformFeedbackJS* const tf) {
  const FuncScope funcScope(*this, "bindTransformFeedback");
  if (IsContextLost()) return;
  if (tf && !tf->ValidateUsable(*this, "tf")) return;
  auto& state = State();

  if (target != LOCAL_GL_TRANSFORM_FEEDBACK) {
    EnqueueError(LOCAL_GL_INVALID_ENUM, "`target` must be TRANSFORM_FEEDBACK.");
    return;
  }
  if (state.mTfActiveAndNotPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Current Transform Feedback object is active and not paused.");
    return;
  }

  if (tf) {
    tf->mHasBeenBound = true;
    state.mBoundTfo = tf;
  } else {
    state.mBoundTfo = state.mDefaultTfo;
  }

  Run<RPROC(BindTransformFeedback)>(tf ? tf->mId : 0);
}

void ClientWebGLContext::BeginTransformFeedback(const GLenum primMode) {
  const FuncScope funcScope(*this, "beginTransformFeedback");
  if (IsContextLost()) return;
  auto& state = State();
  auto& tfo = *(state.mBoundTfo);

  if (tfo.mActiveOrPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Transform Feedback is already active or paused.");
    return;
  }
  MOZ_ASSERT(!state.mTfActiveAndNotPaused);

  auto& prog = state.mCurrentProgram;
  if (!prog) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "No program in use.");
    return;
  }
  const auto& linkResult = GetLinkResult(*prog);
  if (!linkResult.success) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Program is not successfully linked.");
    return;
  }

  auto tfBufferCount = linkResult.active.activeTfVaryings.size();
  if (tfBufferCount &&
      linkResult.tfBufferMode == LOCAL_GL_INTERLEAVED_ATTRIBS) {
    tfBufferCount = 1;
  }
  if (!tfBufferCount) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Program does not use Transform Feedback.");
    return;
  }

  const auto& buffers = tfo.mAttribBuffers;
  for (const auto i : IntegerRange(tfBufferCount)) {
    if (!buffers[i]) {
      EnqueueError(LOCAL_GL_INVALID_OPERATION,
                   "Transform Feedback buffer %u is null.", i);
      return;
    }
  }

  switch (primMode) {
    case LOCAL_GL_POINTS:
    case LOCAL_GL_LINES:
    case LOCAL_GL_TRIANGLES:
      break;
    default:
      EnqueueError(LOCAL_GL_INVALID_ENUM,
                   "`primitiveMode` must be POINTS, LINES< or TRIANGLES.");
      return;
  }

  // -

  tfo.mActiveOrPaused = true;
  tfo.mActiveProgram = prog;
  tfo.mActiveProgramKeepAlive = prog->mKeepAliveWeak.lock();
  prog->mActiveTfos.insert(&tfo);
  state.mTfActiveAndNotPaused = true;

  Run<RPROC(BeginTransformFeedback)>(primMode);
}

void ClientWebGLContext::EndTransformFeedback() {
  const FuncScope funcScope(*this, "endTransformFeedback");
  if (IsContextLost()) return;
  auto& state = State();
  auto& tfo = *(state.mBoundTfo);

  if (!tfo.mActiveOrPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Transform Feedback is not active or paused.");
    return;
  }

  tfo.mActiveOrPaused = false;
  tfo.mActiveProgram->mActiveTfos.erase(&tfo);
  tfo.mActiveProgram = nullptr;
  tfo.mActiveProgramKeepAlive = nullptr;
  state.mTfActiveAndNotPaused = false;
  Run<RPROC(EndTransformFeedback)>();
}

void ClientWebGLContext::PauseTransformFeedback() {
  const FuncScope funcScope(*this, "pauseTransformFeedback");
  if (IsContextLost()) return;
  auto& state = State();
  auto& tfo = *(state.mBoundTfo);

  if (!tfo.mActiveOrPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Transform Feedback is not active.");
    return;
  }
  if (!state.mTfActiveAndNotPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Transform Feedback is already paused.");
    return;
  }

  state.mTfActiveAndNotPaused = false;
  Run<RPROC(PauseTransformFeedback)>();
}

void ClientWebGLContext::ResumeTransformFeedback() {
  const FuncScope funcScope(*this, "resumeTransformFeedback");
  if (IsContextLost()) return;
  auto& state = State();
  auto& tfo = *(state.mBoundTfo);

  if (!tfo.mActiveOrPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Transform Feedback is not active and paused.");
    return;
  }
  if (state.mTfActiveAndNotPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Transform Feedback is not paused.");
    return;
  }
  if (state.mCurrentProgram != tfo.mActiveProgram) {
    EnqueueError(
        LOCAL_GL_INVALID_OPERATION,
        "Cannot Resume Transform Feedback with a program link result different"
        " from when Begin was called.");
    return;
  }

  state.mTfActiveAndNotPaused = true;
  Run<RPROC(ResumeTransformFeedback)>();
}

void ClientWebGLContext::SetFramebufferIsInOpaqueRAF(WebGLFramebufferJS* fb,
                                                     bool value) {
  fb->mInOpaqueRAF = value;
  Run<RPROC(SetFramebufferIsInOpaqueRAF)>(fb->mId, value);
}

// ---------------------------- Misc Extensions ----------------------------
void ClientWebGLContext::DrawBuffers(const dom::Sequence<GLenum>& buffers) {
  const auto range = MakeRange(buffers);
  const auto vec = std::vector<GLenum>(range.begin().get(), range.end().get());
  Run<RPROC(DrawBuffers)>(vec);
}

void ClientWebGLContext::EnqueueErrorImpl(const GLenum error,
                                          const nsACString& text) const {
  if (!mNotLost) return;  // Ignored if context is lost.
  AutoEnqueueFlush();
  Run<RPROC(GenerateError)>(error, ToString(text));
}

void ClientWebGLContext::RequestExtension(const WebGLExtensionID ext) const {
  Run<RPROC(RequestExtension)>(ext);
}

// -

bool ClientWebGLContext::IsExtensionForbiddenForCaller(
    const WebGLExtensionID ext, const dom::CallerType callerType) const {
  if (callerType == dom::CallerType::System) {
    return false;
  }

  if (StaticPrefs::webgl_enable_privileged_extensions()) {
    return false;
  }

  switch (ext) {
    case WebGLExtensionID::MOZ_debug:
      return true;

    case WebGLExtensionID::WEBGL_debug_renderer_info:
      return ShouldResistFingerprinting(RFPTarget::WebGLRenderInfo) ||
             !StaticPrefs::webgl_enable_debug_renderer_info();

    case WebGLExtensionID::WEBGL_debug_shaders:
      return ShouldResistFingerprinting(RFPTarget::WebGLRenderInfo);

    default:
      return false;
  }
}

bool ClientWebGLContext::IsSupported(const WebGLExtensionID ext,
                                     const dom::CallerType callerType) const {
  if (IsExtensionForbiddenForCaller(ext, callerType)) {
    return false;
  }

  const auto& limits = Limits();
  return limits.supportedExtensions[ext];
}

void ClientWebGLContext::GetSupportedExtensions(
    dom::Nullable<nsTArray<nsString>>& retval,
    const dom::CallerType callerType) const {
  retval.SetNull();
  if (!mNotLost) return;

  auto& retarr = retval.SetValue();
  for (const auto i : MakeEnumeratedRange(WebGLExtensionID::Max)) {
    if (!IsSupported(i, callerType)) continue;

    const auto& extStr = GetExtensionName(i);
    retarr.AppendElement(NS_ConvertUTF8toUTF16(extStr));
  }
}

// -

void ClientWebGLContext::GetSupportedProfilesASTC(
    dom::Nullable<nsTArray<nsString>>& retval) const {
  retval.SetNull();
  if (!mNotLost) return;
  const auto& limits = Limits();

  auto& retarr = retval.SetValue();
  retarr.AppendElement(u"ldr"_ns);
  if (limits.astcHdr) {
    retarr.AppendElement(u"hdr"_ns);
  }
}

void ClientWebGLContext::ProvokingVertex(const GLenum rawMode) const {
  const FuncScope funcScope(*this, "provokingVertex");
  if (IsContextLost()) return;

  const auto mode = AsEnumCase<webgl::ProvokingVertex>(rawMode);
  if (!mode) {
    EnqueueError_ArgEnum("mode", rawMode);
    return;
  }

  Run<RPROC(ProvokingVertex)>(*mode);

  funcScope.mKeepNotLostOrNull->state.mProvokingVertex = *mode;
}

// -

uint32_t ClientWebGLContext::GetPrincipalHashValue() const {
  if (mCanvasElement) {
    return mCanvasElement->NodePrincipal()->GetHashValue();
  }
  if (mOffscreenCanvas) {
    nsIGlobalObject* global = mOffscreenCanvas->GetOwnerGlobal();
    if (global) {
      nsIPrincipal* principal = global->PrincipalOrNull();
      if (principal) {
        return principal->GetHashValue();
      }
    }
  }
  return 0;
}

// ---------------------------

void ClientWebGLContext::EnqueueError_ArgEnum(const char* const argName,
                                              const GLenum val) const {
  EnqueueError(LOCAL_GL_INVALID_ENUM, "Bad `%s`: 0x%04x", argName, val);
}

// -
// WebGLProgramJS

void ClientWebGLContext::AttachShader(WebGLProgramJS& prog,
                                      WebGLShaderJS& shader) const {
  const FuncScope funcScope(*this, "attachShader");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  auto& slot = *MaybeFind(prog.mNextLink_Shaders, shader.mType);
  if (slot.shader) {
    if (&shader == slot.shader) {
      EnqueueError(LOCAL_GL_INVALID_OPERATION, "`shader` is already attached.");
    } else {
      EnqueueError(LOCAL_GL_INVALID_OPERATION,
                   "Only one of each type of"
                   " shader may be attached to a program.");
    }
    return;
  }
  slot = {&shader, shader.mKeepAliveWeak.lock()};

  Run<RPROC(AttachShader)>(prog.mId, shader.mId);
}

void ClientWebGLContext::BindAttribLocation(WebGLProgramJS& prog,
                                            const GLuint location,
                                            const nsAString& name) const {
  const FuncScope funcScope(*this, "bindAttribLocation");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  const auto& nameU8 = ToString(NS_ConvertUTF16toUTF8(name));
  Run<RPROC(BindAttribLocation)>(prog.mId, location, nameU8);
}

void ClientWebGLContext::DetachShader(WebGLProgramJS& prog,
                                      const WebGLShaderJS& shader) const {
  const FuncScope funcScope(*this, "detachShader");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  auto& slot = *MaybeFind(prog.mNextLink_Shaders, shader.mType);

  if (slot.shader != &shader) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "`shader` is not attached.");
    return;
  }
  slot = {};

  Run<RPROC(DetachShader)>(prog.mId, shader.mId);
}

void ClientWebGLContext::GetAttachedShaders(
    const WebGLProgramJS& prog,
    dom::Nullable<nsTArray<RefPtr<WebGLShaderJS>>>& retval) const {
  const FuncScope funcScope(*this, "getAttachedShaders");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  auto& arr = retval.SetValue();
  for (const auto& pair : prog.mNextLink_Shaders) {
    const auto& attachment = pair.second;
    if (!attachment.shader) continue;
    arr.AppendElement(attachment.shader);
  }
}

void ClientWebGLContext::LinkProgram(WebGLProgramJS& prog) const {
  const FuncScope funcScope(*this, "linkProgram");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  if (!prog.mActiveTfos.empty()) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Program still in use by active or paused"
                 " Transform Feedback objects.");
    return;
  }

  prog.mResult = std::make_shared<webgl::LinkResult>();
  prog.mUniformLocByName = Nothing();
  prog.mUniformBlockBindings = {};
  Run<RPROC(LinkProgram)>(prog.mId);
}

void ClientWebGLContext::TransformFeedbackVaryings(
    WebGLProgramJS& prog, const dom::Sequence<nsString>& varyings,
    const GLenum bufferMode) const {
  const FuncScope funcScope(*this, "transformFeedbackVaryings");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  std::vector<std::string> varyingsU8;
  varyingsU8.reserve(varyings.Length());
  for (const auto& cur : varyings) {
    const auto curU8 = ToString(NS_ConvertUTF16toUTF8(cur));
    varyingsU8.push_back(curU8);
  }

  Run<RPROC(TransformFeedbackVaryings)>(prog.mId, varyingsU8, bufferMode);
}

void ClientWebGLContext::UniformBlockBinding(WebGLProgramJS& prog,
                                             const GLuint blockIndex,
                                             const GLuint blockBinding) const {
  const FuncScope funcScope(*this, "uniformBlockBinding");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;
  const auto& state = State();

  (void)GetLinkResult(prog);
  auto& list = prog.mUniformBlockBindings;
  if (blockIndex >= list.size()) {
    EnqueueError(
        LOCAL_GL_INVALID_VALUE,
        "`blockIndex` (%u) must be less than ACTIVE_UNIFORM_BLOCKS (%zu).",
        blockIndex, list.size());
    return;
  }
  if (blockBinding >= state.mBoundUbos.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE,
                 "`blockBinding` (%u) must be less than "
                 "MAX_UNIFORM_BUFFER_BINDINGS (%zu).",
                 blockBinding, state.mBoundUbos.size());
    return;
  }

  list[blockIndex] = blockBinding;
  Run<RPROC(UniformBlockBinding)>(prog.mId, blockIndex, blockBinding);
}

// WebGLProgramJS link result reflection

already_AddRefed<WebGLActiveInfoJS> ClientWebGLContext::GetActiveAttrib(
    const WebGLProgramJS& prog, const GLuint index) {
  const FuncScope funcScope(*this, "getActiveAttrib");
  if (IsContextLost()) return nullptr;
  if (!prog.ValidateUsable(*this, "program")) return nullptr;

  const auto& res = GetLinkResult(prog);
  const auto& list = res.active.activeAttribs;
  if (index >= list.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`index` too large.");
    return nullptr;
  }

  const auto& info = list[index];
  return AsAddRefed(new WebGLActiveInfoJS(info));
}

already_AddRefed<WebGLActiveInfoJS> ClientWebGLContext::GetActiveUniform(
    const WebGLProgramJS& prog, const GLuint index) {
  const FuncScope funcScope(*this, "getActiveUniform");
  if (IsContextLost()) return nullptr;
  if (!prog.ValidateUsable(*this, "program")) return nullptr;

  const auto& res = GetLinkResult(prog);
  const auto& list = res.active.activeUniforms;
  if (index >= list.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`index` too large.");
    return nullptr;
  }

  const auto& info = list[index];
  return AsAddRefed(new WebGLActiveInfoJS(info));
}

void ClientWebGLContext::GetActiveUniformBlockName(const WebGLProgramJS& prog,
                                                   const GLuint index,
                                                   nsAString& retval) const {
  retval.SetIsVoid(true);
  const FuncScope funcScope(*this, "getActiveUniformBlockName");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  const auto& res = GetLinkResult(prog);
  if (!res.success) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "Program has not been linked.");
    return;
  }

  const auto& list = res.active.activeUniformBlocks;
  if (index >= list.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`index` too large.");
    return;
  }

  const auto& block = list[index];
  CopyUTF8toUTF16(block.name, retval);
}

void ClientWebGLContext::GetActiveUniformBlockParameter(
    JSContext* const cx, const WebGLProgramJS& prog, const GLuint index,
    const GLenum pname, JS::MutableHandle<JS::Value> retval, ErrorResult& rv) {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getActiveUniformBlockParameter");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  const auto& res = GetLinkResult(prog);
  const auto& list = res.active.activeUniformBlocks;
  if (index >= list.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`index` too large.");
    return;
  }
  const auto& block = list[index];

  retval.set([&]() -> JS::Value {
    switch (pname) {
      case LOCAL_GL_UNIFORM_BLOCK_BINDING:
        return JS::NumberValue(prog.mUniformBlockBindings[index]);

      case LOCAL_GL_UNIFORM_BLOCK_DATA_SIZE:
        return JS::NumberValue(block.dataSize);

      case LOCAL_GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS:
        return JS::NumberValue(block.activeUniformIndices.size());

      case LOCAL_GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES: {
        const auto& indices = block.activeUniformIndices;
        return Create<dom::Uint32Array>(cx, this, indices, rv);
      }

      case LOCAL_GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER:
        return JS::BooleanValue(block.referencedByVertexShader);

      case LOCAL_GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER:
        return JS::BooleanValue(block.referencedByFragmentShader);

      default:
        EnqueueError_ArgEnum("pname", pname);
        return JS::NullValue();
    }
  }());
}

void ClientWebGLContext::GetActiveUniforms(
    JSContext* const cx, const WebGLProgramJS& prog,
    const dom::Sequence<GLuint>& uniformIndices, const GLenum pname,
    JS::MutableHandle<JS::Value> retval) const {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getActiveUniforms");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  const auto& res = GetLinkResult(prog);
  const auto& list = res.active.activeUniforms;

  const auto count = uniformIndices.Length();
  JS::Rooted<JSObject*> array(cx, JS::NewArrayObject(cx, count));
  if (!array) return;  // Just bail.

  for (const auto i : IntegerRange(count)) {
    const auto index = uniformIndices[i];
    if (index >= list.size()) {
      EnqueueError(LOCAL_GL_INVALID_VALUE,
                   "`uniformIndices[%u]`: `%u` too large.", i, index);
      return;
    }
    const auto& uniform = list[index];

    JS::Rooted<JS::Value> value(cx);
    switch (pname) {
      case LOCAL_GL_UNIFORM_TYPE:
        value = JS::NumberValue(uniform.elemType);
        break;

      case LOCAL_GL_UNIFORM_SIZE:
        value = JS::NumberValue(uniform.elemCount);
        break;

      case LOCAL_GL_UNIFORM_BLOCK_INDEX:
        value = JS::NumberValue(uniform.block_index);
        break;

      case LOCAL_GL_UNIFORM_OFFSET:
        value = JS::NumberValue(uniform.block_offset);
        break;

      case LOCAL_GL_UNIFORM_ARRAY_STRIDE:
        value = JS::NumberValue(uniform.block_arrayStride);
        break;

      case LOCAL_GL_UNIFORM_MATRIX_STRIDE:
        value = JS::NumberValue(uniform.block_matrixStride);
        break;

      case LOCAL_GL_UNIFORM_IS_ROW_MAJOR:
        value = JS::BooleanValue(uniform.block_isRowMajor);
        break;

      default:
        EnqueueError_ArgEnum("pname", pname);
        return;
    }
    if (!JS_DefineElement(cx, array, i, value, JSPROP_ENUMERATE)) return;
  }

  retval.setObject(*array);
}

already_AddRefed<WebGLActiveInfoJS>
ClientWebGLContext::GetTransformFeedbackVarying(const WebGLProgramJS& prog,
                                                const GLuint index) {
  const FuncScope funcScope(*this, "getTransformFeedbackVarying");
  if (IsContextLost()) return nullptr;
  if (!prog.ValidateUsable(*this, "program")) return nullptr;

  const auto& res = GetLinkResult(prog);
  const auto& list = res.active.activeTfVaryings;
  if (index >= list.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`index` too large.");
    return nullptr;
  }

  const auto& info = list[index];
  return AsAddRefed(new WebGLActiveInfoJS(info));
}

GLint ClientWebGLContext::GetAttribLocation(const WebGLProgramJS& prog,
                                            const nsAString& name) const {
  const FuncScope funcScope(*this, "getAttribLocation");
  if (IsContextLost()) return -1;
  if (!prog.ValidateUsable(*this, "program")) return -1;

  const auto nameU8 = ToString(NS_ConvertUTF16toUTF8(name));
  const auto& res = GetLinkResult(prog);
  for (const auto& cur : res.active.activeAttribs) {
    if (cur.name == nameU8) return cur.location;
  }

  const auto err = CheckGLSLVariableName(mIsWebGL2, nameU8);
  if (err) {
    EnqueueError(err->type, "%s", err->info.c_str());
  }
  return -1;
}

GLint ClientWebGLContext::GetFragDataLocation(const WebGLProgramJS& prog,
                                              const nsAString& name) const {
  const FuncScope funcScope(*this, "getFragDataLocation");
  if (IsContextLost()) return -1;
  if (!prog.ValidateUsable(*this, "program")) return -1;

  const auto nameU8 = ToString(NS_ConvertUTF16toUTF8(name));

  const auto err = CheckGLSLVariableName(mIsWebGL2, nameU8);
  if (err) {
    EnqueueError(*err);
    return -1;
  }

  return [&]() {
    const auto& inProcess = mNotLost->inProcess;
    if (inProcess) {
      return inProcess->GetFragDataLocation(prog.mId, nameU8);
    }
    const auto& child = mNotLost->outOfProcess;
    child->FlushPendingCmds();
    GLint ret = {};
    if (!child->SendGetFragDataLocation(prog.mId, nameU8, &ret)) {
      ret = {};
    }
    return ret;
  }();
}

GLuint ClientWebGLContext::GetUniformBlockIndex(
    const WebGLProgramJS& prog, const nsAString& blockName) const {
  const FuncScope funcScope(*this, "getUniformBlockIndex");
  if (IsContextLost()) return LOCAL_GL_INVALID_INDEX;
  if (!prog.ValidateUsable(*this, "program")) return LOCAL_GL_INVALID_INDEX;

  const auto nameU8 = ToString(NS_ConvertUTF16toUTF8(blockName));

  const auto& res = GetLinkResult(prog);
  const auto& list = res.active.activeUniformBlocks;
  for (const auto i : IntegerRange(list.size())) {
    const auto& cur = list[i];
    if (cur.name == nameU8) {
      return i;
    }
  }
  return LOCAL_GL_INVALID_INDEX;
}

void ClientWebGLContext::GetUniformIndices(
    const WebGLProgramJS& prog, const dom::Sequence<nsString>& uniformNames,
    dom::Nullable<nsTArray<GLuint>>& retval) const {
  const FuncScope funcScope(*this, "getUniformIndices");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  const auto& res = GetLinkResult(prog);
  auto ret = nsTArray<GLuint>(uniformNames.Length());

  for (const auto& queriedNameU16 : uniformNames) {
    const auto queriedName = ToString(NS_ConvertUTF16toUTF8(queriedNameU16));
    const auto impliedProperArrayQueriedName = queriedName + "[0]";

    GLuint activeId = LOCAL_GL_INVALID_INDEX;
    for (const auto i : IntegerRange(res.active.activeUniforms.size())) {
      // O(N^2) ok for small N.
      const auto& activeInfoForI = res.active.activeUniforms[i];
      if (queriedName == activeInfoForI.name ||
          impliedProperArrayQueriedName == activeInfoForI.name) {
        activeId = i;
        break;
      }
    }
    ret.AppendElement(activeId);
  }

  retval.SetValue(std::move(ret));
}

already_AddRefed<WebGLUniformLocationJS> ClientWebGLContext::GetUniformLocation(
    const WebGLProgramJS& prog, const nsAString& name) const {
  const FuncScope funcScope(*this, "getUniformLocation");
  if (IsContextLost()) return nullptr;
  if (!prog.ValidateUsable(*this, "program")) return nullptr;

  const auto& res = GetLinkResult(prog);

  if (!prog.mUniformLocByName) {
    // Cache a map from name->location.
    // Since the only way to set uniforms requires calling GetUniformLocation,
    // we expect apps to query most active uniforms once for each scalar or
    // array. NB: Uniform array setters do have overflow semantics, even though
    // uniform locations aren't guaranteed contiguous, but GetUniformLocation
    // must still be called once per array.
    prog.mUniformLocByName.emplace();

    for (const auto& activeUniform : res.active.activeUniforms) {
      if (activeUniform.block_index != -1) continue;

      auto locName = activeUniform.name;
      const auto indexed = webgl::ParseIndexed(locName);
      if (indexed) {
        locName = indexed->name;
      }

      const auto err = CheckGLSLVariableName(mIsWebGL2, locName);
      if (err) continue;

      const auto baseLength = locName.size();
      for (const auto& pair : activeUniform.locByIndex) {
        if (indexed) {
          locName.erase(baseLength);  // Erase previous "[N]".
          locName += '[';
          locName += std::to_string(pair.first);
          locName += ']';
        }
        const auto locInfo =
            WebGLProgramJS::UniformLocInfo{pair.second, activeUniform.elemType};
        prog.mUniformLocByName->insert({locName, locInfo});
      }
    }
  }
  const auto& locByName = *(prog.mUniformLocByName);

  const auto nameU8 = ToString(NS_ConvertUTF16toUTF8(name));
  auto loc = MaybeFind(locByName, nameU8);
  if (!loc) {
    loc = MaybeFind(locByName, nameU8 + "[0]");
  }
  if (!loc) {
    const auto err = CheckGLSLVariableName(mIsWebGL2, nameU8);
    if (err) {
      EnqueueError(err->type, "%s", err->info.c_str());
    }
    return nullptr;
  }

  return AsAddRefed(new WebGLUniformLocationJS(*this, prog.mResult,
                                               loc->location, loc->elemType));
}

std::array<uint16_t, 3> ValidUploadElemTypes(const GLenum elemType) {
  std::vector<GLenum> ret;
  switch (elemType) {
    case LOCAL_GL_BOOL:
      ret = {LOCAL_GL_FLOAT, LOCAL_GL_INT, LOCAL_GL_UNSIGNED_INT};
      break;
    case LOCAL_GL_BOOL_VEC2:
      ret = {LOCAL_GL_FLOAT_VEC2, LOCAL_GL_INT_VEC2,
             LOCAL_GL_UNSIGNED_INT_VEC2};
      break;
    case LOCAL_GL_BOOL_VEC3:
      ret = {LOCAL_GL_FLOAT_VEC3, LOCAL_GL_INT_VEC3,
             LOCAL_GL_UNSIGNED_INT_VEC3};
      break;
    case LOCAL_GL_BOOL_VEC4:
      ret = {LOCAL_GL_FLOAT_VEC4, LOCAL_GL_INT_VEC4,
             LOCAL_GL_UNSIGNED_INT_VEC4};
      break;

    case LOCAL_GL_SAMPLER_2D:
    case LOCAL_GL_SAMPLER_3D:
    case LOCAL_GL_SAMPLER_CUBE:
    case LOCAL_GL_SAMPLER_2D_SHADOW:
    case LOCAL_GL_SAMPLER_2D_ARRAY:
    case LOCAL_GL_SAMPLER_2D_ARRAY_SHADOW:
    case LOCAL_GL_SAMPLER_CUBE_SHADOW:
    case LOCAL_GL_INT_SAMPLER_2D:
    case LOCAL_GL_INT_SAMPLER_3D:
    case LOCAL_GL_INT_SAMPLER_CUBE:
    case LOCAL_GL_INT_SAMPLER_2D_ARRAY:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_3D:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_CUBE:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
      ret = {LOCAL_GL_INT};
      break;

    default:
      ret = {elemType};
      break;
  }

  std::array<uint16_t, 3> arr = {};
  MOZ_ASSERT(arr[2] == 0);
  for (const auto i : IntegerRange(ret.size())) {
    arr[i] = AssertedCast<uint16_t>(ret[i]);
  }
  return arr;
}

void ClientWebGLContext::GetProgramInfoLog(const WebGLProgramJS& prog,
                                           nsAString& retval) const {
  retval.SetIsVoid(true);
  const FuncScope funcScope(*this, "getProgramInfoLog");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  const auto& res = GetLinkResult(prog);
  CopyUTF8toUTF16(res.log, retval);
}

void ClientWebGLContext::GetProgramParameter(
    JSContext* const js, const WebGLProgramJS& prog, const GLenum pname,
    JS::MutableHandle<JS::Value> retval) const {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getProgramParameter");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  retval.set([&]() -> JS::Value {
    switch (pname) {
      case LOCAL_GL_DELETE_STATUS:
        // "Is flagged for deletion?"
        return JS::BooleanValue(!prog.mKeepAlive);
      case LOCAL_GL_VALIDATE_STATUS:
        return JS::BooleanValue(prog.mLastValidate);
      case LOCAL_GL_ATTACHED_SHADERS: {
        size_t shaders = 0;
        for (const auto& pair : prog.mNextLink_Shaders) {
          const auto& slot = pair.second;
          if (slot.shader) {
            shaders += 1;
          }
        }
        return JS::NumberValue(shaders);
      }
      default:
        break;
    }

    const auto& res = GetLinkResult(prog);

    switch (pname) {
      case LOCAL_GL_LINK_STATUS:
        return JS::BooleanValue(res.success);

      case LOCAL_GL_ACTIVE_ATTRIBUTES:
        return JS::NumberValue(res.active.activeAttribs.size());

      case LOCAL_GL_ACTIVE_UNIFORMS:
        return JS::NumberValue(res.active.activeUniforms.size());

      case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER_MODE:
        if (!mIsWebGL2) break;
        return JS::NumberValue(res.tfBufferMode);

      case LOCAL_GL_TRANSFORM_FEEDBACK_VARYINGS:
        if (!mIsWebGL2) break;
        return JS::NumberValue(res.active.activeTfVaryings.size());

      case LOCAL_GL_ACTIVE_UNIFORM_BLOCKS:
        if (!mIsWebGL2) break;
        return JS::NumberValue(res.active.activeUniformBlocks.size());

      default:
        break;
    }
    EnqueueError_ArgEnum("pname", pname);
    return JS::NullValue();
  }());
}

// -
// WebGLShaderJS

void ClientWebGLContext::CompileShader(WebGLShaderJS& shader) const {
  const FuncScope funcScope(*this, "compileShader");
  if (IsContextLost()) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  shader.mResult = {};
  Run<RPROC(CompileShader)>(shader.mId);
}

void ClientWebGLContext::GetShaderInfoLog(const WebGLShaderJS& shader,
                                          nsAString& retval) const {
  retval.SetIsVoid(true);
  const FuncScope funcScope(*this, "getShaderInfoLog");
  if (IsContextLost()) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  const auto& result = GetCompileResult(shader);
  CopyUTF8toUTF16(result.log, retval);
}

void ClientWebGLContext::GetShaderParameter(
    JSContext* const cx, const WebGLShaderJS& shader, const GLenum pname,
    JS::MutableHandle<JS::Value> retval) const {
  retval.set(JS::NullValue());
  const FuncScope funcScope(*this, "getShaderParameter");
  if (IsContextLost()) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  retval.set([&]() -> JS::Value {
    switch (pname) {
      case LOCAL_GL_SHADER_TYPE:
        return JS::NumberValue(shader.mType);

      case LOCAL_GL_DELETE_STATUS:  // "Is flagged for deletion?"
        return JS::BooleanValue(!shader.mKeepAlive);

      case LOCAL_GL_COMPILE_STATUS: {
        const auto& result = GetCompileResult(shader);
        return JS::BooleanValue(result.success);
      }

      default:
        EnqueueError_ArgEnum("pname", pname);
        return JS::NullValue();
    }
  }());
}

void ClientWebGLContext::GetShaderSource(const WebGLShaderJS& shader,
                                         nsAString& retval) const {
  retval.SetIsVoid(true);
  const FuncScope funcScope(*this, "getShaderSource");
  if (IsContextLost()) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  CopyUTF8toUTF16(shader.mSource, retval);
}

void ClientWebGLContext::GetTranslatedShaderSource(const WebGLShaderJS& shader,
                                                   nsAString& retval) const {
  retval.SetIsVoid(true);
  const FuncScope funcScope(*this, "getTranslatedShaderSource");
  if (IsContextLost()) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  const auto& result = GetCompileResult(shader);
  CopyUTF8toUTF16(result.translatedSource, retval);
}

void ClientWebGLContext::ShaderSource(WebGLShaderJS& shader,
                                      const nsAString& sourceU16) const {
  const FuncScope funcScope(*this, "shaderSource");
  if (IsContextLost()) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  shader.mSource = ToString(NS_ConvertUTF16toUTF8(sourceU16));
  Run<RPROC(ShaderSource)>(shader.mId, shader.mSource);
}

// -

const webgl::CompileResult& ClientWebGLContext::GetCompileResult(
    const WebGLShaderJS& shader) const {
  if (shader.mResult.pending) {
    shader.mResult = [&]() {
      const auto& inProcess = mNotLost->inProcess;
      if (inProcess) {
        return inProcess->GetCompileResult(shader.mId);
      }
      const auto& child = mNotLost->outOfProcess;
      child->FlushPendingCmds();
      webgl::CompileResult ret = {};
      if (!child->SendGetCompileResult(shader.mId, &ret)) {
        ret = {};
      }
      return ret;
    }();
  }
  return shader.mResult;
}

const webgl::LinkResult& ClientWebGLContext::GetLinkResult(
    const WebGLProgramJS& prog) const {
  if (prog.mResult->pending) {
    const auto notLost =
        mNotLost;  // Hold a strong-ref to prevent LoseContext=>UAF.
    if (!notLost) return *(prog.mResult);

    *(prog.mResult) = [&]() {
      const auto& inProcess = mNotLost->inProcess;
      if (inProcess) {
        return inProcess->GetLinkResult(prog.mId);
      }
      const auto& child = mNotLost->outOfProcess;
      child->FlushPendingCmds();
      webgl::LinkResult ret;
      if (!child->SendGetLinkResult(prog.mId, &ret)) {
        ret = {};
      }
      return ret;
    }();

    prog.mUniformBlockBindings.resize(
        prog.mResult->active.activeUniformBlocks.size());

    auto& state = State();
    if (state.mCurrentProgram == &prog && prog.mResult->success) {
      state.mActiveLinkResult = prog.mResult;
    }
  }
  return *(prog.mResult);
}

#undef RPROC

// ---------------------------

Maybe<Span<uint8_t>> ClientWebGLContext::ValidateArrayBufferView(
    const Span<uint8_t>& bytes, size_t elemSize, GLuint elemOffset,
    GLuint elemCountOverride, const GLenum errorEnum) const {
  size_t elemCount = bytes.Length() / elemSize;
  if (elemOffset > elemCount) {
    EnqueueError(errorEnum, "Invalid offset into ArrayBufferView.");
    return Nothing();
  }
  elemCount -= elemOffset;

  if (elemCountOverride) {
    if (elemCountOverride > elemCount) {
      EnqueueError(errorEnum, "Invalid sub-length for ArrayBufferView.");
      return Nothing();
    }
    elemCount = elemCountOverride;
  }

  return Some(bytes.Subspan(elemOffset * elemSize, elemCount * elemSize));
}

// ---------------------------

webgl::ObjectJS::ObjectJS(const ClientWebGLContext* const webgl)
    : mGeneration(webgl ? webgl->mNotLost : nullptr),
      mId(webgl ? webgl->NextId() : 0) {}

// -

WebGLFramebufferJS::WebGLFramebufferJS(const ClientWebGLContext& webgl,
                                       bool opaque)
    : webgl::ObjectJS(&webgl), mOpaque(opaque) {
  (void)mAttachments[LOCAL_GL_DEPTH_ATTACHMENT];
  (void)mAttachments[LOCAL_GL_STENCIL_ATTACHMENT];
  if (!webgl.mIsWebGL2) {
    (void)mAttachments[LOCAL_GL_DEPTH_STENCIL_ATTACHMENT];
  }

  EnsureColorAttachments();
}

void WebGLFramebufferJS::EnsureColorAttachments() {
  const auto& webgl = Context();
  if (!webgl) return;  // Context is lost.

  const auto& limits = webgl->Limits();
  auto maxColorDrawBuffers = limits.maxColorDrawBuffers;
  if (!webgl->mIsWebGL2 &&
      !webgl->IsExtensionEnabled(WebGLExtensionID::WEBGL_draw_buffers)) {
    maxColorDrawBuffers = 1;
  }
  for (const auto i : IntegerRange(maxColorDrawBuffers)) {
    (void)mAttachments[LOCAL_GL_COLOR_ATTACHMENT0 + i];
  }
}

WebGLProgramJS::WebGLProgramJS(const ClientWebGLContext& webgl)
    : webgl::ObjectJS(&webgl),
      mKeepAlive(std::make_shared<webgl::ProgramKeepAlive>(*this)),
      mKeepAliveWeak(mKeepAlive) {
  (void)mNextLink_Shaders[LOCAL_GL_VERTEX_SHADER];
  (void)mNextLink_Shaders[LOCAL_GL_FRAGMENT_SHADER];

  mResult = std::make_shared<webgl::LinkResult>();
}

WebGLShaderJS::WebGLShaderJS(const ClientWebGLContext& webgl, const GLenum type)
    : webgl::ObjectJS(&webgl),
      mType(type),
      mKeepAlive(std::make_shared<webgl::ShaderKeepAlive>(*this)),
      mKeepAliveWeak(mKeepAlive) {}

WebGLTransformFeedbackJS::WebGLTransformFeedbackJS(
    const ClientWebGLContext& webgl)
    : webgl::ObjectJS(&webgl),
      mAttribBuffers(webgl::kMaxTransformFeedbackSeparateAttribs) {}

WebGLVertexArrayJS::WebGLVertexArrayJS(const ClientWebGLContext* const webgl)
    : webgl::ObjectJS(webgl),
      mAttribBuffers(Context() ? Context()->Limits().maxVertexAttribs : 0) {}

// -

#define _(WebGLType)                                                      \
  JSObject* WebGLType##JS::WrapObject(JSContext* const cx,                \
                                      JS::Handle<JSObject*> givenProto) { \
    return dom::WebGLType##_Binding::Wrap(cx, this, givenProto);          \
  }

_(WebGLBuffer)
_(WebGLFramebuffer)
_(WebGLProgram)
_(WebGLQuery)
_(WebGLRenderbuffer)
_(WebGLSampler)
_(WebGLShader)
_(WebGLSync)
_(WebGLTexture)
_(WebGLTransformFeedback)
_(WebGLUniformLocation)
//_(WebGLVertexArray) // The webidl is `WebGLVertexArrayObject` :(

#undef _

JSObject* WebGLVertexArrayJS::WrapObject(JSContext* const cx,
                                         JS::Handle<JSObject*> givenProto) {
  return dom::WebGLVertexArrayObject_Binding::Wrap(cx, this, givenProto);
}

bool WebGLActiveInfoJS::WrapObject(JSContext* const cx,
                                   JS::Handle<JSObject*> givenProto,
                                   JS::MutableHandle<JSObject*> reflector) {
  return dom::WebGLActiveInfo_Binding::Wrap(cx, this, givenProto, reflector);
}

bool WebGLShaderPrecisionFormatJS::WrapObject(
    JSContext* const cx, JS::Handle<JSObject*> givenProto,
    JS::MutableHandle<JSObject*> reflector) {
  return dom::WebGLShaderPrecisionFormat_Binding::Wrap(cx, this, givenProto,
                                                       reflector);
}

// ---------------------

template <typename T>
void ImplCycleCollectionTraverse(nsCycleCollectionTraversalCallback& callback,
                                 const std::vector<RefPtr<T>>& field,
                                 const char* name, uint32_t flags) {
  for (const auto& cur : field) {
    ImplCycleCollectionTraverse(callback, cur, name, flags);
  }
}

template <typename T>
void ImplCycleCollectionUnlink(std::vector<RefPtr<T>>& field) {
  field = {};
}

// -

template <typename T, size_t N>
void ImplCycleCollectionTraverse(nsCycleCollectionTraversalCallback& callback,
                                 const std::array<RefPtr<T>, N>& field,
                                 const char* name, uint32_t flags) {
  for (const auto& cur : field) {
    ImplCycleCollectionTraverse(callback, cur, name, flags);
  }
}

template <typename T, size_t N>
void ImplCycleCollectionUnlink(std::array<RefPtr<T>, N>& field) {
  field = {};
}

// -

template <typename T>
void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& callback,
    const std::unordered_map<GLenum, RefPtr<T>>& field, const char* name,
    uint32_t flags) {
  for (const auto& pair : field) {
    ImplCycleCollectionTraverse(callback, pair.second, name, flags);
  }
}

template <typename T>
void ImplCycleCollectionUnlink(std::unordered_map<GLenum, RefPtr<T>>& field) {
  field = {};
}

// -

void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& callback,
    const std::unordered_map<GLenum, WebGLFramebufferJS::Attachment>& field,
    const char* name, uint32_t flags) {
  for (const auto& pair : field) {
    const auto& attach = pair.second;
    ImplCycleCollectionTraverse(callback, attach.rb, name, flags);
    ImplCycleCollectionTraverse(callback, attach.tex, name, flags);
  }
}

void ImplCycleCollectionUnlink(
    std::unordered_map<GLenum, WebGLFramebufferJS::Attachment>& field) {
  field = {};
}

// -

void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& callback,
    const std::unordered_map<GLenum, WebGLProgramJS::Attachment>& field,
    const char* name, uint32_t flags) {
  for (const auto& pair : field) {
    const auto& attach = pair.second;
    ImplCycleCollectionTraverse(callback, attach.shader, name, flags);
  }
}

void ImplCycleCollectionUnlink(
    std::unordered_map<GLenum, WebGLProgramJS::Attachment>& field) {
  field = {};
}

// -

void ImplCycleCollectionUnlink(
    const RefPtr<ClientWebGLExtensionLoseContext>& field) {
  const_cast<RefPtr<ClientWebGLExtensionLoseContext>&>(field) = nullptr;
}
void ImplCycleCollectionUnlink(const RefPtr<WebGLProgramJS>& field) {
  const_cast<RefPtr<WebGLProgramJS>&>(field) = nullptr;
}
void ImplCycleCollectionUnlink(const RefPtr<WebGLShaderJS>& field) {
  const_cast<RefPtr<WebGLShaderJS>&>(field) = nullptr;
}

// ----------------------

void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& callback,
    const std::shared_ptr<webgl::NotLostData>& field, const char* name,
    uint32_t flags) {
  if (!field) return;

  ImplCycleCollectionTraverse(callback, field->extensions,
                              "NotLostData.extensions", flags);

  const auto& state = field->state;

  ImplCycleCollectionTraverse(callback, state.mDefaultTfo, "state.mDefaultTfo",
                              flags);
  ImplCycleCollectionTraverse(callback, state.mDefaultVao, "state.mDefaultVao",
                              flags);

  ImplCycleCollectionTraverse(callback, state.mCurrentProgram,
                              "state.mCurrentProgram", flags);

  ImplCycleCollectionTraverse(callback, state.mBoundBufferByTarget,
                              "state.mBoundBufferByTarget", flags);
  ImplCycleCollectionTraverse(callback, state.mBoundUbos, "state.mBoundUbos",
                              flags);
  ImplCycleCollectionTraverse(callback, state.mBoundDrawFb,
                              "state.mBoundDrawFb", flags);
  ImplCycleCollectionTraverse(callback, state.mBoundReadFb,
                              "state.mBoundReadFb", flags);
  ImplCycleCollectionTraverse(callback, state.mBoundRb, "state.mBoundRb",
                              flags);
  ImplCycleCollectionTraverse(callback, state.mBoundTfo, "state.mBoundTfo",
                              flags);
  ImplCycleCollectionTraverse(callback, state.mBoundVao, "state.mBoundVao",
                              flags);
  ImplCycleCollectionTraverse(callback, state.mCurrentQueryByTarget,
                              "state.state.mCurrentQueryByTarget", flags);

  for (const auto& texUnit : state.mTexUnits) {
    ImplCycleCollectionTraverse(callback, texUnit.sampler,
                                "state.mTexUnits[].sampler", flags);
    ImplCycleCollectionTraverse(callback, texUnit.texByTarget,
                                "state.mTexUnits[].texByTarget", flags);
  }
}

void ImplCycleCollectionUnlink(std::shared_ptr<webgl::NotLostData>& field) {
  if (!field) return;
  const auto keepAlive = field;
  keepAlive->extensions = {};
  keepAlive->state = {};
  field = nullptr;
}

// -----------------------------------------------------

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLBufferJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WebGLFramebufferJS, mAttachments)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WebGLProgramJS, mNextLink_Shaders)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLQueryJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLRenderbufferJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLSamplerJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLShaderJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLSyncJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLTextureJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WebGLTransformFeedbackJS, mAttribBuffers,
                                      mActiveProgram)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLUniformLocationJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WebGLVertexArrayJS, mIndexBuffer,
                                      mAttribBuffers)

// -

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ClientWebGLContext)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsICanvasRenderingContextInternal)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(ClientWebGLContext)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ClientWebGLContext)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_WEAK_PTR(
    ClientWebGLContext, mExtLoseContext, mNotLost,
    // Don't forget nsICanvasRenderingContextInternal:
    mCanvasElement, mOffscreenCanvas)

// -----------------------------

}  // namespace mozilla
