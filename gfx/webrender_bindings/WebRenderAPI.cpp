/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebRenderAPI.h"

#include "mozilla/Logging.h"
#include "mozilla/ipc/ByteBuf.h"
#include "mozilla/webrender/RendererOGL.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/ToString.h"
#include "mozilla/webrender/RenderCompositor.h"
#include "mozilla/widget/CompositorWidget.h"
#include "mozilla/layers/SynchronousTask.h"
#include "nsThreadUtils.h"
#include "TextDrawTarget.h"
#include "malloc_decls.h"
#include "GLContext.h"

static mozilla::LazyLogModule sWrDLLog("wr.dl");
#define WRDL_LOG(...) \
  MOZ_LOG(sWrDLLog, LogLevel::Debug, ("WRDL(%p): " __VA_ARGS__))

namespace mozilla {
using namespace layers;

namespace wr {

MOZ_DEFINE_MALLOC_SIZE_OF(WebRenderMallocSizeOf)
MOZ_DEFINE_MALLOC_ENCLOSING_SIZE_OF(WebRenderMallocEnclosingSizeOf)

class NewRenderer : public RendererEvent {
 public:
  NewRenderer(wr::DocumentHandle** aDocHandle,
              layers::CompositorBridgeParent* aBridge,
              WebRenderBackend* aBackend, WebRenderCompositor* aCompositor,
              int32_t* aMaxTextureSize, bool* aUseANGLE, bool* aUseDComp,
              bool* aUseTripleBuffering, bool* aSupportsExternalBufferTextures,
              RefPtr<widget::CompositorWidget>&& aWidget,
              layers::SynchronousTask* aTask, LayoutDeviceIntSize aSize,
              layers::WindowKind aWindowKind, layers::SyncHandle* aHandle,
              nsACString* aError)
      : mDocHandle(aDocHandle),
        mBackend(aBackend),
        mCompositor(aCompositor),
        mMaxTextureSize(aMaxTextureSize),
        mUseANGLE(aUseANGLE),
        mUseDComp(aUseDComp),
        mUseTripleBuffering(aUseTripleBuffering),
        mSupportsExternalBufferTextures(aSupportsExternalBufferTextures),
        mBridge(aBridge),
        mCompositorWidget(std::move(aWidget)),
        mTask(aTask),
        mSize(aSize),
        mWindowKind(aWindowKind),
        mSyncHandle(aHandle),
        mError(aError) {
    MOZ_COUNT_CTOR(NewRenderer);
  }

  MOZ_COUNTED_DTOR(NewRenderer)

  void Run(RenderThread& aRenderThread, WindowId aWindowId) override {
    layers::AutoCompleteTask complete(mTask);

    UniquePtr<RenderCompositor> compositor =
        RenderCompositor::Create(std::move(mCompositorWidget), *mError);
    if (!compositor) {
      if (!mError->IsEmpty()) {
        gfxCriticalNote << mError->BeginReading();
      }
      return;
    }

    compositor->MakeCurrent();

    *mBackend = compositor->BackendType();
    *mCompositor = compositor->CompositorType();
    *mUseANGLE = compositor->UseANGLE();
    *mUseDComp = compositor->UseDComp();
    *mUseTripleBuffering = compositor->UseTripleBuffering();
    *mSupportsExternalBufferTextures =
        compositor->SupportsExternalBufferTextures();

    // Only allow the panic on GL error functionality in nightly builds,
    // since it (deliberately) crashes the GPU process if any GL call
    // returns an error code.
    bool panic_on_gl_error = false;
#ifdef NIGHTLY_BUILD
    panic_on_gl_error =
        StaticPrefs::gfx_webrender_panic_on_gl_error_AtStartup();
#endif

    bool isMainWindow = true;  // TODO!
    bool supportLowPriorityTransactions = isMainWindow;
    bool supportLowPriorityThreadpool =
        supportLowPriorityTransactions &&
        StaticPrefs::gfx_webrender_enable_low_priority_pool();
    wr::Renderer* wrRenderer = nullptr;
    char* errorMessage = nullptr;
    int picTileWidth = StaticPrefs::gfx_webrender_picture_tile_width();
    int picTileHeight = StaticPrefs::gfx_webrender_picture_tile_height();
    auto* swgl = compositor->swgl();
    auto* gl = (compositor->gl() && !swgl) ? compositor->gl() : nullptr;
    auto* progCache = (aRenderThread.GetProgramCache() && !swgl)
                          ? aRenderThread.GetProgramCache()->Raw()
                          : nullptr;
    auto* shaders = (aRenderThread.GetShaders() && !swgl)
                        ? aRenderThread.GetShaders()->RawShaders()
                        : nullptr;

    // Check That if we are not using SWGL, we have at least a GL or GLES 3.0
    // context.
    if (gl && !swgl) {
      bool versionCheck =
          gl->IsAtLeast(gl::ContextProfile::OpenGLCore, 300) ||
          gl->IsAtLeast(gl::ContextProfile::OpenGLCompatibility, 300) ||
          gl->IsAtLeast(gl::ContextProfile::OpenGLES, 300);

      if (!versionCheck) {
        gfxCriticalNote << "GL context version (" << gl->Version()
                        << ") insufficent for hardware WebRender";

        mError->AssignASCII("GL context version insufficient");
        return;
      }
    }

    // Only allow the layer compositor in nightly builds, for now.
    bool use_layer_compositor = false;
#ifdef NIGHTLY_BUILD
    use_layer_compositor =
        StaticPrefs::gfx_webrender_layer_compositor_AtStartup();
#endif

    if (!wr_window_new(
            aWindowId, mSize.width, mSize.height,
            mWindowKind == WindowKind::MAIN, supportLowPriorityTransactions,
            supportLowPriorityThreadpool, gfx::gfxVars::UseGLSwizzle(),
            gfx::gfxVars::UseWebRenderScissoredCacheClears(), swgl, gl,
            compositor->SurfaceOriginIsTopLeft(), progCache, shaders,
            aRenderThread.ThreadPool().Raw(),
            aRenderThread.ThreadPoolLP().Raw(), aRenderThread.MemoryChunkPool(),
            aRenderThread.GlyphRasterThread().Raw(), &WebRenderMallocSizeOf,
            &WebRenderMallocEnclosingSizeOf, 0, compositor.get(),
            compositor->ShouldUseNativeCompositor(),
            compositor->UsePartialPresent(),
            compositor->GetMaxPartialPresentRects(),
            compositor->ShouldDrawPreviousPartialPresentRegions(), mDocHandle,
            &wrRenderer, mMaxTextureSize, &errorMessage,
            StaticPrefs::gfx_webrender_enable_gpu_markers_AtStartup(),
            panic_on_gl_error, picTileWidth, picTileHeight,
            gfx::gfxVars::WebRenderRequiresHardwareDriver(),
            StaticPrefs::gfx_webrender_low_quality_pinch_zoom_AtStartup(),
            StaticPrefs::gfx_webrender_max_shared_surface_size_AtStartup(),
            StaticPrefs::gfx_webrender_enable_subpixel_aa_AtStartup(),
            use_layer_compositor)) {
      // wr_window_new puts a message into gfxCriticalNote if it returns false
      MOZ_ASSERT(errorMessage);
      mError->AssignASCII(errorMessage);
      wr_api_free_error_msg(errorMessage);
      return;
    }
    MOZ_ASSERT(wrRenderer);

    RefPtr<RenderThread> thread = &aRenderThread;
    auto renderer =
        MakeUnique<RendererOGL>(std::move(thread), std::move(compositor),
                                aWindowId, wrRenderer, mBridge);
    if (wrRenderer && renderer) {
      wr::WrExternalImageHandler handler = renderer->GetExternalImageHandler();
      wr_renderer_set_external_image_handler(wrRenderer, &handler);
    }

    if (renderer) {
      layers::SyncObjectHost* syncObj = renderer->GetSyncObject();
      if (syncObj) {
        *mSyncHandle = syncObj->GetSyncHandle();
      }
    }

    aRenderThread.AddRenderer(aWindowId, std::move(renderer));
  }

  const char* Name() override { return "NewRenderer"; }

 private:
  wr::DocumentHandle** mDocHandle;
  WebRenderBackend* mBackend;
  WebRenderCompositor* mCompositor;
  int32_t* mMaxTextureSize;
  bool* mUseANGLE;
  bool* mUseDComp;
  bool* mUseTripleBuffering;
  bool* mSupportsExternalBufferTextures;
  layers::CompositorBridgeParent* mBridge;
  RefPtr<widget::CompositorWidget> mCompositorWidget;
  layers::SynchronousTask* mTask;
  LayoutDeviceIntSize mSize;
  layers::WindowKind mWindowKind;
  layers::SyncHandle* mSyncHandle;
  nsACString* mError;
};

class RemoveRenderer : public RendererEvent {
 public:
  explicit RemoveRenderer(layers::SynchronousTask* aTask) : mTask(aTask) {
    MOZ_COUNT_CTOR(RemoveRenderer);
  }

  MOZ_COUNTED_DTOR_OVERRIDE(RemoveRenderer)

  void Run(RenderThread& aRenderThread, WindowId aWindowId) override {
    aRenderThread.RemoveRenderer(aWindowId);
    layers::AutoCompleteTask complete(mTask);
  }

  const char* Name() override { return "RemoveRenderer"; }

 private:
  layers::SynchronousTask* mTask;
};

TransactionBuilder::TransactionBuilder(
    WebRenderAPI* aApi, bool aUseSceneBuilderThread,
    layers::RemoteTextureTxnScheduler* aRemoteTextureTxnScheduler,
    layers::RemoteTextureTxnId aRemoteTextureTxnId)
    : mRemoteTextureTxnScheduler(aRemoteTextureTxnScheduler),
      mRemoteTextureTxnId(aRemoteTextureTxnId),
      mUseSceneBuilderThread(aUseSceneBuilderThread),
      mApiBackend(aApi->GetBackendType()),
      mOwnsData(true) {
  mTxn = wr_transaction_new(mUseSceneBuilderThread);
}

TransactionBuilder::TransactionBuilder(
    WebRenderAPI* aApi, Transaction* aTxn, bool aUseSceneBuilderThread,
    bool aOwnsData,
    layers::RemoteTextureTxnScheduler* aRemoteTextureTxnScheduler,
    layers::RemoteTextureTxnId aRemoteTextureTxnId)
    : mRemoteTextureTxnScheduler(aRemoteTextureTxnScheduler),
      mRemoteTextureTxnId(aRemoteTextureTxnId),
      mTxn(aTxn),
      mUseSceneBuilderThread(aUseSceneBuilderThread),
      mApiBackend(aApi->GetBackendType()),
      mOwnsData(aOwnsData) {}

TransactionBuilder::~TransactionBuilder() {
  if (mOwnsData) {
    wr_transaction_delete(mTxn);
  }
}

void TransactionBuilder::SetLowPriority(bool aIsLowPriority) {
  wr_transaction_set_low_priority(mTxn, aIsLowPriority);
}

void TransactionBuilder::UpdateEpoch(PipelineId aPipelineId, Epoch aEpoch) {
  wr_transaction_update_epoch(mTxn, aPipelineId, aEpoch);
}

void TransactionBuilder::SetRootPipeline(PipelineId aPipelineId) {
  wr_transaction_set_root_pipeline(mTxn, aPipelineId);
}

void TransactionBuilder::RemovePipeline(PipelineId aPipelineId) {
  wr_transaction_remove_pipeline(mTxn, aPipelineId);
}

void TransactionBuilder::SetDisplayList(
    Epoch aEpoch, wr::WrPipelineId pipeline_id,
    wr::BuiltDisplayListDescriptor dl_descriptor,
    wr::Vec<uint8_t>& dl_items_data, wr::Vec<uint8_t>& dl_cache_data,
    wr::Vec<uint8_t>& dl_spatial_tree) {
  wr_transaction_set_display_list(mTxn, aEpoch, pipeline_id, dl_descriptor,
                                  &dl_items_data.inner, &dl_cache_data.inner,
                                  &dl_spatial_tree.inner);
}

void TransactionBuilder::ClearDisplayList(Epoch aEpoch,
                                          wr::WrPipelineId aPipelineId) {
  wr_transaction_clear_display_list(mTxn, aEpoch, aPipelineId);
}

void TransactionBuilder::GenerateFrame(const VsyncId& aVsyncId, bool aPresent,
                                       wr::RenderReasons aReasons) {
  wr_transaction_generate_frame(mTxn, aVsyncId.mId, aPresent, aReasons);
}

void TransactionBuilder::InvalidateRenderedFrame(wr::RenderReasons aReasons) {
  wr_transaction_invalidate_rendered_frame(mTxn, aReasons);
}

bool TransactionBuilder::IsEmpty() const {
  return wr_transaction_is_empty(mTxn);
}

bool TransactionBuilder::IsResourceUpdatesEmpty() const {
  return wr_transaction_resource_updates_is_empty(mTxn);
}

bool TransactionBuilder::IsRenderedFrameInvalidated() const {
  return wr_transaction_is_rendered_frame_invalidated(mTxn);
}

void TransactionBuilder::SetDocumentView(
    const LayoutDeviceIntRect& aDocumentRect) {
  wr::DeviceIntRect wrDocRect;
  wrDocRect.min.x = aDocumentRect.x;
  wrDocRect.min.y = aDocumentRect.y;
  wrDocRect.max.x = aDocumentRect.x + aDocumentRect.width;
  wrDocRect.max.y = aDocumentRect.y + aDocumentRect.height;
  wr_transaction_set_document_view(mTxn, &wrDocRect);
}

TransactionWrapper::TransactionWrapper(Transaction* aTxn) : mTxn(aTxn) {}

void TransactionWrapper::AppendDynamicProperties(
    const nsTArray<wr::WrOpacityProperty>& aOpacityArray,
    const nsTArray<wr::WrTransformProperty>& aTransformArray,
    const nsTArray<wr::WrColorProperty>& aColorArray) {
  wr_transaction_append_dynamic_properties(
      mTxn, aOpacityArray.IsEmpty() ? nullptr : aOpacityArray.Elements(),
      aOpacityArray.Length(),
      aTransformArray.IsEmpty() ? nullptr : aTransformArray.Elements(),
      aTransformArray.Length(),
      aColorArray.IsEmpty() ? nullptr : aColorArray.Elements(),
      aColorArray.Length());
}

void TransactionWrapper::AppendTransformProperties(
    const nsTArray<wr::WrTransformProperty>& aTransformArray) {
  wr_transaction_append_transform_properties(
      mTxn, aTransformArray.IsEmpty() ? nullptr : aTransformArray.Elements(),
      aTransformArray.Length());
}

void TransactionWrapper::UpdateScrollPosition(
    const wr::ExternalScrollId& aScrollId,
    const nsTArray<wr::SampledScrollOffset>& aSampledOffsets) {
  wr_transaction_scroll_layer(mTxn, aScrollId, &aSampledOffsets);
}

void TransactionWrapper::UpdateIsTransformAsyncZooming(uint64_t aAnimationId,
                                                       bool aIsZooming) {
  wr_transaction_set_is_transform_async_zooming(mTxn, aAnimationId, aIsZooming);
}

void TransactionWrapper::AddMinimapData(const wr::ExternalScrollId& aScrollId,
                                        const MinimapData& aMinimapData) {
  wr_transaction_add_minimap_data(mTxn, aScrollId, aMinimapData);
}

/*static*/
already_AddRefed<WebRenderAPI> WebRenderAPI::Create(
    layers::CompositorBridgeParent* aBridge,
    RefPtr<widget::CompositorWidget>&& aWidget, const wr::WrWindowId& aWindowId,
    LayoutDeviceIntSize aSize, layers::WindowKind aWindowKind,
    nsACString& aError) {
  MOZ_ASSERT(aBridge);
  MOZ_ASSERT(aWidget);
  static_assert(
      sizeof(size_t) == sizeof(uintptr_t),
      "The FFI bindings assume size_t is the same size as uintptr_t!");

  wr::DocumentHandle* docHandle = nullptr;
  WebRenderBackend backend = WebRenderBackend::HARDWARE;
  WebRenderCompositor compositor = WebRenderCompositor::DRAW;
  int32_t maxTextureSize = 0;
  bool useANGLE = false;
  bool useDComp = false;
  bool useTripleBuffering = false;
  bool supportsExternalBufferTextures = false;
  layers::SyncHandle syncHandle = {};

  // Dispatch a synchronous task because the DocumentHandle object needs to be
  // created on the render thread. If need be we could delay waiting on this
  // task until the next time we need to access the DocumentHandle object.
  layers::SynchronousTask task("Create Renderer");
  auto event = MakeUnique<NewRenderer>(
      &docHandle, aBridge, &backend, &compositor, &maxTextureSize, &useANGLE,
      &useDComp, &useTripleBuffering, &supportsExternalBufferTextures,
      std::move(aWidget), &task, aSize, aWindowKind, &syncHandle, &aError);
  RenderThread::Get()->PostEvent(aWindowId, std::move(event));

  task.Wait();

  if (!docHandle) {
    return nullptr;
  }

  return RefPtr<WebRenderAPI>(
             new WebRenderAPI(docHandle, aWindowId, backend, compositor,
                              maxTextureSize, useANGLE, useDComp,
                              useTripleBuffering,
                              supportsExternalBufferTextures, syncHandle))
      .forget();
}

already_AddRefed<WebRenderAPI> WebRenderAPI::Clone() {
  wr::DocumentHandle* docHandle = nullptr;
  wr_api_clone(mDocHandle, &docHandle);

  RefPtr<WebRenderAPI> renderApi = new WebRenderAPI(
      docHandle, mId, mBackend, mCompositor, mMaxTextureSize, mUseANGLE,
      mUseDComp, mUseTripleBuffering, mSupportsExternalBufferTextures,
      mSyncHandle, this, this);

  return renderApi.forget();
}

wr::WrIdNamespace WebRenderAPI::GetNamespace() {
  return wr_api_get_namespace(mDocHandle);
}

WebRenderAPI::WebRenderAPI(
    wr::DocumentHandle* aHandle, wr::WindowId aId, WebRenderBackend aBackend,
    WebRenderCompositor aCompositor, uint32_t aMaxTextureSize, bool aUseANGLE,
    bool aUseDComp, bool aUseTripleBuffering,
    bool aSupportsExternalBufferTextures, layers::SyncHandle aSyncHandle,
    wr::WebRenderAPI* aRootApi, wr::WebRenderAPI* aRootDocumentApi)
    : mDocHandle(aHandle),
      mId(aId),
      mBackend(aBackend),
      mCompositor(aCompositor),
      mMaxTextureSize(aMaxTextureSize),
      mUseANGLE(aUseANGLE),
      mUseDComp(aUseDComp),
      mUseTripleBuffering(aUseTripleBuffering),
      mSupportsExternalBufferTextures(aSupportsExternalBufferTextures),
      mCaptureSequence(false),
      mSyncHandle(aSyncHandle),
      mRendererDestroyed(false),
      mRootApi(aRootApi),
      mRootDocumentApi(aRootDocumentApi) {}

WebRenderAPI::~WebRenderAPI() {
  if (!mRootDocumentApi) {
    wr_api_delete_document(mDocHandle);
  }

  if (!mRootApi) {
    MOZ_RELEASE_ASSERT(mRendererDestroyed);
    wr_api_shut_down(mDocHandle);
  }

  wr_api_delete(mDocHandle);
}

void WebRenderAPI::DestroyRenderer() {
  MOZ_RELEASE_ASSERT(!mRootApi);

  RenderThread::Get()->SetDestroyed(GetId());
  // Call wr_api_stop_render_backend() before RemoveRenderer.
  wr_api_stop_render_backend(mDocHandle);

  layers::SynchronousTask task("Destroy WebRenderAPI");
  auto event = MakeUnique<RemoveRenderer>(&task);
  RunOnRenderThread(std::move(event));
  task.Wait();

  mRendererDestroyed = true;
}

wr::WebRenderAPI* WebRenderAPI::GetRootAPI() {
  if (mRootApi) {
    return mRootApi;
  }
  return this;
}

void WebRenderAPI::UpdateDebugFlags(uint32_t aFlags) {
  wr_api_set_debug_flags(mDocHandle, wr::DebugFlags{aFlags});
}

void WebRenderAPI::SendTransaction(TransactionBuilder& aTxn) {
  if (mRootApi && mRootApi->mRendererDestroyed) {
    return;
  }

  if (mPendingRemoteTextureInfoList &&
      !mPendingRemoteTextureInfoList->mList.empty()) {
    mPendingWrTransactionEvents.emplace(
        WrTransactionEvent::PendingRemoteTextures(
            std::move(mPendingRemoteTextureInfoList)));
  }

  if (mPendingAsyncImagePipelineOps &&
      !mPendingAsyncImagePipelineOps->mList.empty()) {
    mPendingWrTransactionEvents.emplace(
        WrTransactionEvent::PendingAsyncImagePipelineOps(
            std::move(mPendingAsyncImagePipelineOps), this, aTxn));
  }

  if (!mPendingWrTransactionEvents.empty()) {
    mPendingWrTransactionEvents.emplace(
        WrTransactionEvent::Transaction(this, aTxn));
    HandleWrTransactionEvents(RemoteTextureWaitType::AsyncWait);
  } else {
    wr_api_send_transaction(mDocHandle, aTxn.Raw(),
                            aTxn.UseSceneBuilderThread());
    if (aTxn.mRemoteTextureTxnScheduler) {
      aTxn.mRemoteTextureTxnScheduler->NotifyTxn(aTxn.mRemoteTextureTxnId);
    }
  }
}

layers::RemoteTextureInfoList* WebRenderAPI::GetPendingRemoteTextureInfoList() {
  if (!mRootApi) {
    // root api does not support async wait RemoteTexture.
    return nullptr;
  }

  if (!mPendingRemoteTextureInfoList) {
    mPendingRemoteTextureInfoList = MakeUnique<layers::RemoteTextureInfoList>();
  }
  return mPendingRemoteTextureInfoList.get();
}

layers::AsyncImagePipelineOps* WebRenderAPI::GetPendingAsyncImagePipelineOps(
    TransactionBuilder& aTxn) {
  if (!mRootApi) {
    // root api does not support async wait RemoteTexture.
    return nullptr;
  }

  if (!mPendingAsyncImagePipelineOps ||
      mPendingAsyncImagePipelineOps->mTransaction != aTxn.Raw()) {
    if (mPendingAsyncImagePipelineOps &&
        !mPendingAsyncImagePipelineOps->mList.empty()) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      gfxCriticalNoteOnce << "Invalid AsyncImagePipelineOps";
    }
    mPendingAsyncImagePipelineOps =
        MakeUnique<layers::AsyncImagePipelineOps>(aTxn.Raw());
  } else {
    MOZ_RELEASE_ASSERT(mPendingAsyncImagePipelineOps->mTransaction ==
                       aTxn.Raw());
  }

  return mPendingAsyncImagePipelineOps.get();
}

bool WebRenderAPI::CheckIsRemoteTextureReady(
    layers::RemoteTextureInfoList* aList, const TimeStamp& aTimeStamp) {
  MOZ_ASSERT(layers::CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(aList);

  RefPtr<WebRenderAPI> self = this;
  auto callback = [self](const layers::RemoteTextureInfo&) {
    RefPtr<nsIRunnable> runnable = NewRunnableMethod<RemoteTextureWaitType>(
        "WebRenderAPI::HandleWrTransactionEvents", self,
        &WebRenderAPI::HandleWrTransactionEvents,
        RemoteTextureWaitType::AsyncWait);
    layers::CompositorThread()->Dispatch(runnable.forget());
  };

  bool isReady = true;
  while (!aList->mList.empty() && isReady) {
    auto& front = aList->mList.front();
    isReady &= layers::RemoteTextureMap::Get()->CheckRemoteTextureReady(
        front, callback);
    if (isReady) {
      aList->mList.pop();
    }
  }

  if (isReady) {
    return true;
  }

#ifndef DEBUG
  const auto maxWaitDurationMs = 10000;
#else
  const auto maxWaitDurationMs = 30000;
#endif
  const auto now = TimeStamp::Now();
  const auto waitDurationMs =
      static_cast<uint32_t>((now - aTimeStamp).ToMilliseconds());

  const auto isTimeout = waitDurationMs > maxWaitDurationMs;
  if (isTimeout) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    gfxCriticalNote << "RemoteTexture ready timeout";
  }

  return false;
}

void WebRenderAPI::WaitRemoteTextureReady(
    layers::RemoteTextureInfoList* aList) {
  MOZ_ASSERT(layers::CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(aList);

  while (!aList->mList.empty()) {
    auto& front = aList->mList.front();
    layers::RemoteTextureMap::Get()->WaitRemoteTextureReady(front);
    aList->mList.pop();
  }
}

void WebRenderAPI::FlushPendingWrTransactionEventsWithoutWait() {
  HandleWrTransactionEvents(RemoteTextureWaitType::FlushWithoutWait);
}

void WebRenderAPI::FlushPendingWrTransactionEventsWithWait() {
  HandleWrTransactionEvents(RemoteTextureWaitType::FlushWithWait);
}

void WebRenderAPI::HandleWrTransactionEvents(RemoteTextureWaitType aType) {
  auto& events = mPendingWrTransactionEvents;

  while (!events.empty()) {
    auto& front = events.front();
    switch (front.mTag) {
      case WrTransactionEvent::Tag::Transaction:
        wr_api_send_transaction(mDocHandle, front.RawTransaction(),
                                front.UseSceneBuilderThread());
        if (front.GetTransactionBuilder()->mRemoteTextureTxnScheduler) {
          front.GetTransactionBuilder()->mRemoteTextureTxnScheduler->NotifyTxn(
              front.GetTransactionBuilder()->mRemoteTextureTxnId);
        }
        break;
      case WrTransactionEvent::Tag::PendingRemoteTextures: {
        bool isReady = true;
        if (aType == RemoteTextureWaitType::AsyncWait) {
          isReady = CheckIsRemoteTextureReady(front.RemoteTextureInfoList(),
                                              front.mTimeStamp);
        } else if (aType == RemoteTextureWaitType::FlushWithWait) {
          WaitRemoteTextureReady(front.RemoteTextureInfoList());
        } else {
          MOZ_ASSERT(aType == RemoteTextureWaitType::FlushWithoutWait);
          auto* list = front.RemoteTextureInfoList();
          while (!list->mList.empty()) {
            auto& front = list->mList.front();
            layers::RemoteTextureMap::Get()->SuppressRemoteTextureReadyCheck(
                front);
            list->mList.pop();
          }
        }
        if (!isReady && (aType != RemoteTextureWaitType::FlushWithoutWait)) {
          return;
        }
        break;
      }
      case WrTransactionEvent::Tag::PendingAsyncImagePipelineOps: {
        auto* list = front.AsyncImagePipelineOps();
        TransactionBuilder& txn = *front.GetTransactionBuilder();

        list->HandleOps(txn);
        break;
      }
    }
    events.pop();
  }
}

std::vector<WrHitResult> WebRenderAPI::HitTest(const wr::WorldPoint& aPoint) {
  static_assert(gfx::DoesCompositorHitTestInfoFitIntoBits<12>(),
                "CompositorHitTestFlags MAX value has to be less than number "
                "of bits in uint16_t minus 4 for SideBitsPacked");

  nsTArray<wr::HitResult> wrResults;
  wr_api_hit_test(mDocHandle, aPoint, &wrResults);

  std::vector<WrHitResult> geckoResults;
  for (wr::HitResult wrResult : wrResults) {
    WrHitResult geckoResult;
    geckoResult.mLayersId = wr::AsLayersId(wrResult.pipeline_id);
    geckoResult.mScrollId =
        static_cast<layers::ScrollableLayerGuid::ViewID>(wrResult.scroll_id);
    geckoResult.mHitInfo.deserialize(wrResult.hit_info & 0x0fff);
    geckoResult.mSideBits = static_cast<SideBits>(wrResult.hit_info >> 12);

    if (wrResult.animation_id != 0) {
      geckoResult.mAnimationId = Some(wrResult.animation_id);
    } else {
      geckoResult.mAnimationId = Nothing();
    }
    geckoResults.push_back(geckoResult);
  }
  return geckoResults;
}

void WebRenderAPI::Readback(const TimeStamp& aStartTime, gfx::IntSize size,
                            const gfx::SurfaceFormat& aFormat,
                            const Range<uint8_t>& buffer, bool* aNeedsYFlip) {
  class Readback : public RendererEvent {
   public:
    explicit Readback(layers::SynchronousTask* aTask, TimeStamp aStartTime,
                      gfx::IntSize aSize, const gfx::SurfaceFormat& aFormat,
                      const Range<uint8_t>& aBuffer, bool* aNeedsYFlip)
        : mTask(aTask),
          mStartTime(aStartTime),
          mSize(aSize),
          mFormat(aFormat),
          mBuffer(aBuffer),
          mNeedsYFlip(aNeedsYFlip) {
      MOZ_COUNT_CTOR(Readback);
    }

    MOZ_COUNTED_DTOR_OVERRIDE(Readback)

    void Run(RenderThread& aRenderThread, WindowId aWindowId) override {
      RendererStats stats = {0};
      wr::FrameReadyParams params = {
          .present = true,
          .render = true,
          .scrolled = false,
      };
      aRenderThread.UpdateAndRender(aWindowId, VsyncId(), mStartTime, params,
                                    Some(mSize),
                                    wr::SurfaceFormatToImageFormat(mFormat),
                                    Some(mBuffer), &stats, mNeedsYFlip);
      layers::AutoCompleteTask complete(mTask);
    }

    const char* Name() override { return "Readback"; }

   private:
    layers::SynchronousTask* mTask;
    TimeStamp mStartTime;
    gfx::IntSize mSize;
    gfx::SurfaceFormat mFormat;
    const Range<uint8_t>& mBuffer;
    bool* mNeedsYFlip;
  };

  // Disable debug flags during readback. See bug 1436020.
  UpdateDebugFlags(0);

  layers::SynchronousTask task("Readback");
  auto event = MakeUnique<Readback>(&task, aStartTime, size, aFormat, buffer,
                                    aNeedsYFlip);
  // This event will be passed from wr_backend thread to renderer thread. That
  // implies that all frame data have been processed when the renderer runs this
  // read-back event. Then, we could make sure this read-back event gets the
  // latest result.
  RunOnRenderThread(std::move(event));

  task.Wait();

  UpdateDebugFlags(gfx::gfxVars::WebRenderDebugFlags());
}

void WebRenderAPI::ClearAllCaches() { wr_api_clear_all_caches(mDocHandle); }

void WebRenderAPI::EnableNativeCompositor(bool aEnable) {
  wr_api_enable_native_compositor(mDocHandle, aEnable);
}

void WebRenderAPI::SetBatchingLookback(uint32_t aCount) {
  wr_api_set_batching_lookback(mDocHandle, aCount);
}

void WebRenderAPI::SetBool(wr::BoolParameter aKey, bool aValue) {
  wr_api_set_bool(mDocHandle, aKey, aValue);
}

void WebRenderAPI::SetInt(wr::IntParameter aKey, int32_t aValue) {
  wr_api_set_int(mDocHandle, aKey, aValue);
}

void WebRenderAPI::SetFloat(wr::FloatParameter aKey, float aValue) {
  wr_api_set_float(mDocHandle, aKey, aValue);
}

void WebRenderAPI::SetClearColor(const gfx::DeviceColor& aColor) {
  RenderThread::Get()->SetClearColor(mId, ToColorF(aColor));
}

void WebRenderAPI::SetProfilerUI(const nsACString& aUIString) {
  RenderThread::Get()->SetProfilerUI(mId, aUIString);
}

void WebRenderAPI::Pause() {
  class PauseEvent : public RendererEvent {
   public:
    explicit PauseEvent(layers::SynchronousTask* aTask) : mTask(aTask) {
      MOZ_COUNT_CTOR(PauseEvent);
    }

    MOZ_COUNTED_DTOR_OVERRIDE(PauseEvent)

    void Run(RenderThread& aRenderThread, WindowId aWindowId) override {
      aRenderThread.Pause(aWindowId);
      layers::AutoCompleteTask complete(mTask);
    }

    const char* Name() override { return "PauseEvent"; }

   private:
    layers::SynchronousTask* mTask;
  };

  layers::SynchronousTask task("Pause");
  auto event = MakeUnique<PauseEvent>(&task);
  RenderThread::Get()->PostEvent(mId, std::move(event));

  task.Wait();
}

bool WebRenderAPI::Resume() {
  class ResumeEvent : public RendererEvent {
   public:
    explicit ResumeEvent(layers::SynchronousTask* aTask, bool* aResult)
        : mTask(aTask), mResult(aResult) {
      MOZ_COUNT_CTOR(ResumeEvent);
    }

    MOZ_COUNTED_DTOR_OVERRIDE(ResumeEvent)

    void Run(RenderThread& aRenderThread, WindowId aWindowId) override {
      *mResult = aRenderThread.Resume(aWindowId);
      layers::AutoCompleteTask complete(mTask);
    }

    const char* Name() override { return "ResumeEvent"; }

   private:
    layers::SynchronousTask* mTask;
    bool* mResult;
  };

  bool result = false;
  layers::SynchronousTask task("Resume");
  auto event = MakeUnique<ResumeEvent>(&task, &result);
  RenderThread::Get()->PostEvent(mId, std::move(event));

  task.Wait();
  return result;
}

void WebRenderAPI::NotifyMemoryPressure() {
  wr_api_notify_memory_pressure(mDocHandle);
}

void WebRenderAPI::AccumulateMemoryReport(MemoryReport* aReport) {
  wr_api_accumulate_memory_report(mDocHandle, aReport, &WebRenderMallocSizeOf,
                                  &WebRenderMallocEnclosingSizeOf);
}

void WebRenderAPI::WakeSceneBuilder() { wr_api_wake_scene_builder(mDocHandle); }

void WebRenderAPI::FlushSceneBuilder() {
  wr_api_flush_scene_builder(mDocHandle);
}

void WebRenderAPI::WaitFlushed() {
  class WaitFlushedEvent : public RendererEvent {
   public:
    explicit WaitFlushedEvent(layers::SynchronousTask* aTask) : mTask(aTask) {
      MOZ_COUNT_CTOR(WaitFlushedEvent);
    }

    MOZ_COUNTED_DTOR_OVERRIDE(WaitFlushedEvent)

    void Run(RenderThread& aRenderThread, WindowId aWindowId) override {
      layers::AutoCompleteTask complete(mTask);
    }

    const char* Name() override { return "WaitFlushedEvent"; }

   private:
    layers::SynchronousTask* mTask;
  };

  layers::SynchronousTask task("WaitFlushed");
  auto event = MakeUnique<WaitFlushedEvent>(&task);
  // This event will be passed from wr_backend thread to renderer thread. That
  // implies that all frame data have been processed when the renderer runs this
  // event.
  RunOnRenderThread(std::move(event));

  task.Wait();
}

void WebRenderAPI::Capture() {
  // see CaptureBits
  // SCENE | FRAME | TILE_CACHE
  uint8_t bits = 15;                // TODO: get from JavaScript
  const char* path = "wr-capture";  // TODO: get from JavaScript
  wr_api_capture(mDocHandle, path, bits);
}

void WebRenderAPI::StartCaptureSequence(const nsACString& aPath,
                                        uint32_t aFlags) {
  if (mCaptureSequence) {
    wr_api_stop_capture_sequence(mDocHandle);
  }

  wr_api_start_capture_sequence(mDocHandle, PromiseFlatCString(aPath).get(),
                                aFlags);

  mCaptureSequence = true;
}

void WebRenderAPI::StopCaptureSequence() {
  if (mCaptureSequence) {
    wr_api_stop_capture_sequence(mDocHandle);
  }

  mCaptureSequence = false;
}

void WebRenderAPI::BeginRecording(const TimeStamp& aRecordingStart,
                                  wr::PipelineId aRootPipelineId) {
  class BeginRecordingEvent final : public RendererEvent {
   public:
    explicit BeginRecordingEvent(const TimeStamp& aRecordingStart,
                                 wr::PipelineId aRootPipelineId)
        : mRecordingStart(aRecordingStart), mRootPipelineId(aRootPipelineId) {
      MOZ_COUNT_CTOR(BeginRecordingEvent);
    }

    ~BeginRecordingEvent() { MOZ_COUNT_DTOR(BeginRecordingEvent); }

    void Run(RenderThread& aRenderThread, WindowId aWindowId) override {
      aRenderThread.BeginRecordingForWindow(aWindowId, mRecordingStart,
                                            mRootPipelineId);
    }

    const char* Name() override { return "BeginRecordingEvent"; }

   private:
    TimeStamp mRecordingStart;
    wr::PipelineId mRootPipelineId;
  };

  auto event =
      MakeUnique<BeginRecordingEvent>(aRecordingStart, aRootPipelineId);
  RunOnRenderThread(std::move(event));
}

RefPtr<WebRenderAPI::EndRecordingPromise> WebRenderAPI::EndRecording() {
  class EndRecordingEvent final : public RendererEvent {
   public:
    explicit EndRecordingEvent() { MOZ_COUNT_CTOR(EndRecordingEvent); }

    MOZ_COUNTED_DTOR(EndRecordingEvent);

    void Run(RenderThread& aRenderThread, WindowId aWindowId) override {
      Maybe<layers::FrameRecording> recording =
          aRenderThread.EndRecordingForWindow(aWindowId);

      if (recording) {
        mPromise.Resolve(recording.extract(), __func__);
      } else {
        mPromise.Reject(NS_ERROR_UNEXPECTED, __func__);
      }
    }

    RefPtr<WebRenderAPI::EndRecordingPromise> GetPromise() {
      return mPromise.Ensure(__func__);
    }

    const char* Name() override { return "EndRecordingEvent"; }

   private:
    MozPromiseHolder<WebRenderAPI::EndRecordingPromise> mPromise;
  };

  auto event = MakeUnique<EndRecordingEvent>();
  auto promise = event->GetPromise();

  RunOnRenderThread(std::move(event));
  return promise;
}

void TransactionBuilder::Clear() { wr_resource_updates_clear(mTxn); }

Transaction* TransactionBuilder::Take() {
  if (!mOwnsData) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }
  Transaction* txn = mTxn;
  mTxn = wr_transaction_new(mUseSceneBuilderThread);
  return txn;
}

void TransactionBuilder::Notify(wr::Checkpoint aWhen,
                                UniquePtr<NotificationHandler> aEvent) {
  wr_transaction_notify(mTxn, aWhen,
                        reinterpret_cast<uintptr_t>(aEvent.release()));
}

void TransactionBuilder::AddImage(ImageKey key,
                                  const ImageDescriptor& aDescriptor,
                                  wr::Vec<uint8_t>& aBytes) {
  wr_resource_updates_add_image(mTxn, key, &aDescriptor, &aBytes.inner);
}

void TransactionBuilder::AddBlobImage(BlobImageKey key,
                                      const ImageDescriptor& aDescriptor,
                                      uint16_t aTileSize,
                                      wr::Vec<uint8_t>& aBytes,
                                      const wr::DeviceIntRect& aVisibleRect) {
  wr_resource_updates_add_blob_image(mTxn, key, &aDescriptor, aTileSize,
                                     &aBytes.inner, aVisibleRect);
}

void TransactionBuilder::AddExternalImage(ImageKey key,
                                          const ImageDescriptor& aDescriptor,
                                          ExternalImageId aExtID,
                                          wr::ExternalImageType aImageType,
                                          uint8_t aChannelIndex,
                                          bool aNormalizedUvs) {
  wr_resource_updates_add_external_image(mTxn, key, &aDescriptor, aExtID,
                                         &aImageType, aChannelIndex,
                                         aNormalizedUvs);
}

void TransactionBuilder::AddExternalImageBuffer(
    ImageKey aKey, const ImageDescriptor& aDescriptor,
    ExternalImageId aHandle) {
  auto channelIndex = 0;
  AddExternalImage(aKey, aDescriptor, aHandle, wr::ExternalImageType::Buffer(),
                   channelIndex);
}

void TransactionBuilder::UpdateImageBuffer(ImageKey aKey,
                                           const ImageDescriptor& aDescriptor,
                                           wr::Vec<uint8_t>& aBytes) {
  wr_resource_updates_update_image(mTxn, aKey, &aDescriptor, &aBytes.inner);
}

void TransactionBuilder::UpdateBlobImage(BlobImageKey aKey,
                                         const ImageDescriptor& aDescriptor,
                                         wr::Vec<uint8_t>& aBytes,
                                         const wr::DeviceIntRect& aVisibleRect,
                                         const wr::LayoutIntRect& aDirtyRect) {
  wr_resource_updates_update_blob_image(mTxn, aKey, &aDescriptor, &aBytes.inner,
                                        aVisibleRect, aDirtyRect);
}

void TransactionBuilder::UpdateExternalImage(ImageKey aKey,
                                             const ImageDescriptor& aDescriptor,
                                             ExternalImageId aExtID,
                                             wr::ExternalImageType aImageType,
                                             uint8_t aChannelIndex,
                                             bool aNormalizedUvs) {
  wr_resource_updates_update_external_image(mTxn, aKey, &aDescriptor, aExtID,
                                            &aImageType, aChannelIndex,
                                            aNormalizedUvs);
}

void TransactionBuilder::UpdateExternalImageWithDirtyRect(
    ImageKey aKey, const ImageDescriptor& aDescriptor, ExternalImageId aExtID,
    wr::ExternalImageType aImageType, const wr::DeviceIntRect& aDirtyRect,
    uint8_t aChannelIndex, bool aNormalizedUvs) {
  wr_resource_updates_update_external_image_with_dirty_rect(
      mTxn, aKey, &aDescriptor, aExtID, &aImageType, aChannelIndex,
      aNormalizedUvs, aDirtyRect);
}

void TransactionBuilder::SetBlobImageVisibleArea(
    BlobImageKey aKey, const wr::DeviceIntRect& aArea) {
  wr_resource_updates_set_blob_image_visible_area(mTxn, aKey, &aArea);
}

void TransactionBuilder::DeleteImage(ImageKey aKey) {
  wr_resource_updates_delete_image(mTxn, aKey);
}

void TransactionBuilder::DeleteBlobImage(BlobImageKey aKey) {
  wr_resource_updates_delete_blob_image(mTxn, aKey);
}

void TransactionBuilder::AddSnapshotImage(wr::SnapshotImageKey aKey) {
  wr_resource_updates_add_snapshot_image(mTxn, aKey);
}

void TransactionBuilder::DeleteSnapshotImage(wr::SnapshotImageKey aKey) {
  wr_resource_updates_delete_snapshot_image(mTxn, aKey);
}

void TransactionBuilder::AddRawFont(wr::FontKey aKey, wr::Vec<uint8_t>& aBytes,
                                    uint32_t aIndex) {
  wr_resource_updates_add_raw_font(mTxn, aKey, &aBytes.inner, aIndex);
}

void TransactionBuilder::AddFontDescriptor(wr::FontKey aKey,
                                           wr::Vec<uint8_t>& aBytes,
                                           uint32_t aIndex) {
  wr_resource_updates_add_font_descriptor(mTxn, aKey, &aBytes.inner, aIndex);
}

void TransactionBuilder::DeleteFont(wr::FontKey aKey) {
  wr_resource_updates_delete_font(mTxn, aKey);
}

void TransactionBuilder::AddFontInstance(
    wr::FontInstanceKey aKey, wr::FontKey aFontKey, float aGlyphSize,
    const wr::FontInstanceOptions* aOptions,
    const wr::FontInstancePlatformOptions* aPlatformOptions,
    wr::Vec<uint8_t>& aVariations) {
  wr_resource_updates_add_font_instance(mTxn, aKey, aFontKey, aGlyphSize,
                                        aOptions, aPlatformOptions,
                                        &aVariations.inner);
}

void TransactionBuilder::DeleteFontInstance(wr::FontInstanceKey aKey) {
  wr_resource_updates_delete_font_instance(mTxn, aKey);
}

void TransactionBuilder::UpdateQualitySettings(
    bool aForceSubpixelAAWherePossible) {
  wr_transaction_set_quality_settings(mTxn, aForceSubpixelAAWherePossible);
}

class FrameStartTime : public RendererEvent {
 public:
  explicit FrameStartTime(const TimeStamp& aTime) : mTime(aTime) {
    MOZ_COUNT_CTOR(FrameStartTime);
  }

  MOZ_COUNTED_DTOR_OVERRIDE(FrameStartTime)

  void Run(RenderThread& aRenderThread, WindowId aWindowId) override {
    auto renderer = aRenderThread.GetRenderer(aWindowId);
    if (renderer) {
      renderer->SetFrameStartTime(mTime);
    }
  }

  const char* Name() override { return "FrameStartTime"; }

 private:
  TimeStamp mTime;
};

void WebRenderAPI::SetFrameStartTime(const TimeStamp& aTime) {
  auto event = MakeUnique<FrameStartTime>(aTime);
  RunOnRenderThread(std::move(event));
}

void WebRenderAPI::RunOnRenderThread(UniquePtr<RendererEvent> aEvent) {
  auto event = reinterpret_cast<uintptr_t>(aEvent.release());
  wr_api_send_external_event(mDocHandle, event);
}

DisplayListBuilder::DisplayListBuilder(PipelineId aId,
                                       WebRenderBackend aBackend)
    : mCurrentSpaceAndClipChain(wr::RootScrollNodeWithChain()),
      mActiveFixedPosTracker(nullptr),
      mPipelineId(aId),
      mBackend(aBackend),
      mDisplayItemCache(nullptr) {
  MOZ_COUNT_CTOR(DisplayListBuilder);
  mWrState = wr_state_new(aId);

  if (mDisplayItemCache && mDisplayItemCache->IsEnabled()) {
    mDisplayItemCache->SetPipelineId(aId);
  }
}

DisplayListBuilder::~DisplayListBuilder() {
  MOZ_COUNT_DTOR(DisplayListBuilder);
  wr_state_delete(mWrState);
}

void DisplayListBuilder::Save() { wr_dp_save(mWrState); }
void DisplayListBuilder::Restore() { wr_dp_restore(mWrState); }
void DisplayListBuilder::ClearSave() { wr_dp_clear_save(mWrState); }

usize DisplayListBuilder::Dump(usize aIndent, const Maybe<usize>& aStart,
                               const Maybe<usize>& aEnd) {
  return wr_dump_display_list(mWrState, aIndent, aStart.ptrOr(nullptr),
                              aEnd.ptrOr(nullptr));
}

void DisplayListBuilder::DumpSerializedDisplayList() {
  wr_dump_serialized_display_list(mWrState);
}

void DisplayListBuilder::Begin(layers::DisplayItemCache* aCache) {
  wr_api_begin_builder(mWrState);

  mScrollIds.clear();
  mCurrentSpaceAndClipChain = wr::RootScrollNodeWithChain();
  mClipChainLeaf = Nothing();
  mSuspendedSpaceAndClipChain = Nothing();
  mSuspendedClipChainLeaf = Nothing();
  mCachedTextDT = nullptr;
  mCachedContext = nullptr;
  mActiveFixedPosTracker = nullptr;
  mDisplayItemCache = aCache;
  mCurrentCacheSlot = Nothing();
}

void DisplayListBuilder::End(BuiltDisplayList& aOutDisplayList) {
  wr_api_end_builder(
      mWrState, &aOutDisplayList.dl_desc, &aOutDisplayList.dl_items.inner,
      &aOutDisplayList.dl_cache.inner, &aOutDisplayList.dl_spatial_tree.inner);

  mDisplayItemCache = nullptr;
}

void DisplayListBuilder::End(layers::DisplayListData& aOutTransaction) {
  if (mDisplayItemCache && mDisplayItemCache->IsEnabled()) {
    wr_dp_set_cache_size(mWrState, mDisplayItemCache->CurrentSize());
  }

  wr::VecU8 dlItems, dlCache, dlSpatialTree;
  wr_api_end_builder(mWrState, &aOutTransaction.mDLDesc, &dlItems.inner,
                     &dlCache.inner, &dlSpatialTree.inner);
  aOutTransaction.mDLItems.emplace(dlItems.inner.data, dlItems.inner.length,
                                   dlItems.inner.capacity);
  aOutTransaction.mDLCache.emplace(dlCache.inner.data, dlCache.inner.length,
                                   dlCache.inner.capacity);
  aOutTransaction.mDLSpatialTree.emplace(dlSpatialTree.inner.data,
                                         dlSpatialTree.inner.length,
                                         dlSpatialTree.inner.capacity);
  dlItems.inner.capacity = 0;
  dlItems.inner.data = nullptr;
  dlCache.inner.capacity = 0;
  dlCache.inner.data = nullptr;
  dlSpatialTree.inner.capacity = 0;
  dlSpatialTree.inner.data = nullptr;
}

Maybe<wr::WrSpatialId> DisplayListBuilder::PushStackingContext(
    const wr::StackingContextParams& aParams, const wr::LayoutRect& aBounds,
    const wr::RasterSpace& aRasterSpace) {
  MOZ_ASSERT(mClipChainLeaf.isNothing(),
             "Non-empty leaf from clip chain given, but not used with SC!");

  WRDL_LOG(
      "PushStackingContext b=%s t=%s id=0x%" PRIx64 "\n", mWrState,
      ToString(aBounds).c_str(),
      aParams.mTransformPtr ? ToString(*aParams.mTransformPtr).c_str() : "none",
      aParams.animation ? aParams.animation->id : 0);

  auto spatialId = wr_dp_push_stacking_context(
      mWrState, aBounds, mCurrentSpaceAndClipChain.space, &aParams,
      aParams.mTransformPtr, aParams.mFilters.Elements(),
      aParams.mFilters.Length(), aParams.mFilterDatas.Elements(),
      aParams.mFilterDatas.Length(), aRasterSpace);

  return spatialId.id != 0 ? Some(spatialId) : Nothing();
}

void DisplayListBuilder::PopStackingContext(bool aIsReferenceFrame) {
  WRDL_LOG("PopStackingContext\n", mWrState);
  wr_dp_pop_stacking_context(mWrState, aIsReferenceFrame);
}

wr::WrClipChainId DisplayListBuilder::DefineClipChain(
    const nsTArray<wr::WrClipId>& aClips, bool aParentWithCurrentChain) {
  CancelGroup();

  const uint64_t* parent = nullptr;
  if (aParentWithCurrentChain &&
      mCurrentSpaceAndClipChain.clip_chain != wr::ROOT_CLIP_CHAIN) {
    parent = &mCurrentSpaceAndClipChain.clip_chain;
  }
  uint64_t clipchainId = wr_dp_define_clipchain(
      mWrState, parent, aClips.Elements(), aClips.Length());
  if (MOZ_LOG_TEST(sWrDLLog, LogLevel::Debug)) {
    nsCString message;
    message.AppendPrintf("DefineClipChain id=%" PRIu64
                         " clipCount=%zu clipIds=[",
                         clipchainId, aClips.Length());
    for (const auto& clip : aClips) {
      message.AppendPrintf("%" PRIuPTR ",", clip.id);
    }
    message.Append("]");
    WRDL_LOG("%s", mWrState, message.get());
  }
  return wr::WrClipChainId{clipchainId};
}

wr::WrClipId DisplayListBuilder::DefineImageMaskClip(
    const wr::ImageMask& aMask, const nsTArray<wr::LayoutPoint>& aPoints,
    wr::FillRule aFillRule) {
  CancelGroup();

  WrClipId clipId = wr_dp_define_image_mask_clip_with_parent_clip_chain(
      mWrState, mCurrentSpaceAndClipChain.space, aMask, aPoints.Elements(),
      aPoints.Length(), aFillRule);

  return clipId;
}

wr::WrClipId DisplayListBuilder::DefineRoundedRectClip(
    Maybe<wr::WrSpatialId> aSpace, const wr::ComplexClipRegion& aComplex) {
  CancelGroup();

  WrClipId clipId;
  if (aSpace) {
    clipId = wr_dp_define_rounded_rect_clip(mWrState, *aSpace, aComplex);
  } else {
    clipId = wr_dp_define_rounded_rect_clip(
        mWrState, mCurrentSpaceAndClipChain.space, aComplex);
  }

  return clipId;
}

wr::WrClipId DisplayListBuilder::DefineRectClip(Maybe<wr::WrSpatialId> aSpace,
                                                wr::LayoutRect aClipRect) {
  CancelGroup();

  WrClipId clipId;
  if (aSpace) {
    clipId = wr_dp_define_rect_clip(mWrState, *aSpace, aClipRect);
  } else {
    clipId = wr_dp_define_rect_clip(mWrState, mCurrentSpaceAndClipChain.space,
                                    aClipRect);
  }

  return clipId;
}

wr::WrSpatialId DisplayListBuilder::DefineStickyFrame(
    const wr::LayoutRect& aContentRect, const float* aTopMargin,
    const float* aRightMargin, const float* aBottomMargin,
    const float* aLeftMargin, const StickyOffsetBounds& aVerticalBounds,
    const StickyOffsetBounds& aHorizontalBounds,
    const wr::LayoutVector2D& aAppliedOffset, wr::SpatialTreeItemKey aKey,
    const WrAnimationProperty* aAnimation) {
  auto spatialId = wr_dp_define_sticky_frame(
      mWrState, mCurrentSpaceAndClipChain.space, aContentRect, aTopMargin,
      aRightMargin, aBottomMargin, aLeftMargin, aVerticalBounds,
      aHorizontalBounds, aAppliedOffset, aKey, aAnimation);

  WRDL_LOG("DefineSticky id=%zu c=%s t=%s r=%s b=%s l=%s v=%s h=%s a=%s\n",
           mWrState, spatialId.id, ToString(aContentRect).c_str(),
           aTopMargin ? ToString(*aTopMargin).c_str() : "none",
           aRightMargin ? ToString(*aRightMargin).c_str() : "none",
           aBottomMargin ? ToString(*aBottomMargin).c_str() : "none",
           aLeftMargin ? ToString(*aLeftMargin).c_str() : "none",
           ToString(aVerticalBounds).c_str(),
           ToString(aHorizontalBounds).c_str(),
           ToString(aAppliedOffset).c_str());

  return spatialId;
}

Maybe<wr::WrSpatialId> DisplayListBuilder::GetScrollIdForDefinedScrollLayer(
    layers::ScrollableLayerGuid::ViewID aViewId) const {
  if (aViewId == layers::ScrollableLayerGuid::NULL_SCROLL_ID) {
    return Some(wr::RootScrollNode());
  }

  auto it = mScrollIds.find(aViewId);
  if (it == mScrollIds.end()) {
    return Nothing();
  }

  return Some(it->second);
}

wr::WrSpatialId DisplayListBuilder::DefineScrollLayer(
    const layers::ScrollableLayerGuid::ViewID& aViewId,
    const Maybe<wr::WrSpatialId>& aParent, const wr::LayoutRect& aContentRect,
    const wr::LayoutRect& aClipRect, const wr::LayoutVector2D& aScrollOffset,
    wr::APZScrollGeneration aScrollOffsetGeneration,
    wr::HasScrollLinkedEffect aHasScrollLinkedEffect,
    wr::SpatialTreeItemKey aKey) {
  auto it = mScrollIds.find(aViewId);
  if (it != mScrollIds.end()) {
    return it->second;
  }

  // We haven't defined aViewId before, so let's define it now.
  wr::WrSpatialId defaultParent = mCurrentSpaceAndClipChain.space;

  auto space = wr_dp_define_scroll_layer(
      mWrState, aViewId, aParent ? aParent.ptr() : &defaultParent, aContentRect,
      aClipRect, aScrollOffset, aScrollOffsetGeneration, aHasScrollLinkedEffect,
      aKey);

  WRDL_LOG("DefineScrollLayer id=%" PRIu64
           "/%zu p=%s co=%s cl=%s generation=%s hasScrollLinkedEffect=%s\n",
           mWrState, aViewId, space.id,
           aParent ? ToString(aParent->id).c_str() : "(nil)",
           ToString(aContentRect).c_str(), ToString(aClipRect).c_str(),
           ToString(aScrollOffsetGeneration).c_str(),
           ToString(aHasScrollLinkedEffect).c_str());

  mScrollIds[aViewId] = space;
  return space;
}

void DisplayListBuilder::PushRect(const wr::LayoutRect& aBounds,
                                  const wr::LayoutRect& aClip,
                                  bool aIsBackfaceVisible,
                                  bool aForceAntiAliasing, bool aIsCheckerboard,
                                  const wr::ColorF& aColor) {
  wr::LayoutRect clip = MergeClipLeaf(aClip);
  WRDL_LOG("PushRect b=%s cl=%s c=%s\n", mWrState, ToString(aBounds).c_str(),
           ToString(clip).c_str(), ToString(aColor).c_str());
  wr_dp_push_rect(mWrState, aBounds, clip, aIsBackfaceVisible,
                  aForceAntiAliasing, aIsCheckerboard,
                  &mCurrentSpaceAndClipChain, aColor);
}

void DisplayListBuilder::PushRoundedRect(const wr::LayoutRect& aBounds,
                                         const wr::LayoutRect& aClip,
                                         bool aIsBackfaceVisible,
                                         const wr::ColorF& aColor) {
  wr::LayoutRect clip = MergeClipLeaf(aClip);
  WRDL_LOG("PushRoundedRect b=%s cl=%s c=%s\n", mWrState,
           ToString(aBounds).c_str(), ToString(clip).c_str(),
           ToString(aColor).c_str());

  // Draw the rounded rectangle as a border with rounded corners. We could also
  // draw this as a rectangle clipped to a rounded rectangle, but:
  // - clips are not cached; borders are
  // - a simple border like this will be drawn as an image
  // - Processing lots of clips is not WebRender's strong point.
  //
  // Made the borders thicker than one half the width/height, to avoid
  // little white dots at the center at some magnifications.
  wr::BorderSide side = {aColor, wr::BorderStyle::Solid};
  float h = aBounds.width() * 0.6f;
  float v = aBounds.height() * 0.6f;
  wr::LayoutSideOffsets widths = {v, h, v, h};
  wr::BorderRadius radii = {{h, v}, {h, v}, {h, v}, {h, v}};

  // Anti-aliased borders are required for rounded borders.
  wr_dp_push_border(mWrState, aBounds, clip, aIsBackfaceVisible,
                    &mCurrentSpaceAndClipChain, wr::AntialiasBorder::Yes,
                    widths, side, side, side, side, radii);
}

void DisplayListBuilder::PushHitTest(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible,
    const layers::ScrollableLayerGuid::ViewID& aScrollId,
    const gfx::CompositorHitTestInfo& aHitInfo, SideBits aSideBits) {
  wr::LayoutRect clip = MergeClipLeaf(aClip);
  WRDL_LOG("PushHitTest b=%s cl=%s\n", mWrState, ToString(aBounds).c_str(),
           ToString(clip).c_str());

  static_assert(gfx::DoesCompositorHitTestInfoFitIntoBits<12>(),
                "CompositorHitTestFlags MAX value has to be less than number "
                "of bits in uint16_t minus 4 for SideBitsPacked");

  uint16_t hitInfoBits = static_cast<uint16_t>(aHitInfo.serialize()) |
                         (static_cast<uint16_t>(aSideBits) << 12);

  wr_dp_push_hit_test(mWrState, aBounds, clip, aIsBackfaceVisible,
                      &mCurrentSpaceAndClipChain, aScrollId, hitInfoBits);
}

void DisplayListBuilder::PushRectWithAnimation(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, const wr::ColorF& aColor,
    const WrAnimationProperty* aAnimation) {
  wr::LayoutRect clip = MergeClipLeaf(aClip);
  WRDL_LOG("PushRectWithAnimation b=%s cl=%s c=%s\n", mWrState,
           ToString(aBounds).c_str(), ToString(clip).c_str(),
           ToString(aColor).c_str());

  wr_dp_push_rect_with_animation(mWrState, aBounds, clip, aIsBackfaceVisible,
                                 &mCurrentSpaceAndClipChain, aColor,
                                 aAnimation);
}

void DisplayListBuilder::PushClearRect(const wr::LayoutRect& aBounds) {
  wr::LayoutRect clip = MergeClipLeaf(aBounds);
  WRDL_LOG("PushClearRect b=%s c=%s\n", mWrState, ToString(aBounds).c_str(),
           ToString(clip).c_str());
  wr_dp_push_clear_rect(mWrState, aBounds, clip, &mCurrentSpaceAndClipChain);
}

void DisplayListBuilder::PushBackdropFilter(
    const wr::LayoutRect& aBounds, const wr::ComplexClipRegion& aRegion,
    const nsTArray<wr::FilterOp>& aFilters,
    const nsTArray<wr::WrFilterData>& aFilterDatas, bool aIsBackfaceVisible) {
  wr::LayoutRect clip = MergeClipLeaf(aBounds);
  WRDL_LOG("PushBackdropFilter b=%s c=%s\n", mWrState,
           ToString(aBounds).c_str(), ToString(clip).c_str());

  auto clipId = DefineRoundedRectClip(Nothing(), aRegion);
  auto clipChainId = DefineClipChain({clipId}, true);
  auto spaceAndClip =
      WrSpaceAndClipChain{mCurrentSpaceAndClipChain.space, clipChainId.id};

  wr_dp_push_backdrop_filter(mWrState, aBounds, clip, aIsBackfaceVisible,
                             &spaceAndClip, aFilters.Elements(),
                             aFilters.Length(), aFilterDatas.Elements(),
                             aFilterDatas.Length());
}

void DisplayListBuilder::PushLinearGradient(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, const wr::LayoutPoint& aStartPoint,
    const wr::LayoutPoint& aEndPoint, const nsTArray<wr::GradientStop>& aStops,
    wr::ExtendMode aExtendMode, const wr::LayoutSize aTileSize,
    const wr::LayoutSize aTileSpacing) {
  wr_dp_push_linear_gradient(
      mWrState, aBounds, MergeClipLeaf(aClip), aIsBackfaceVisible,
      &mCurrentSpaceAndClipChain, aStartPoint, aEndPoint, aStops.Elements(),
      aStops.Length(), aExtendMode, aTileSize, aTileSpacing);
}

void DisplayListBuilder::PushRadialGradient(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, const wr::LayoutPoint& aCenter,
    const wr::LayoutSize& aRadius, const nsTArray<wr::GradientStop>& aStops,
    wr::ExtendMode aExtendMode, const wr::LayoutSize aTileSize,
    const wr::LayoutSize aTileSpacing) {
  wr_dp_push_radial_gradient(
      mWrState, aBounds, MergeClipLeaf(aClip), aIsBackfaceVisible,
      &mCurrentSpaceAndClipChain, aCenter, aRadius, aStops.Elements(),
      aStops.Length(), aExtendMode, aTileSize, aTileSpacing);
}

void DisplayListBuilder::PushConicGradient(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, const wr::LayoutPoint& aCenter, const float aAngle,
    const nsTArray<wr::GradientStop>& aStops, wr::ExtendMode aExtendMode,
    const wr::LayoutSize aTileSize, const wr::LayoutSize aTileSpacing) {
  wr_dp_push_conic_gradient(mWrState, aBounds, MergeClipLeaf(aClip),
                            aIsBackfaceVisible, &mCurrentSpaceAndClipChain,
                            aCenter, aAngle, aStops.Elements(), aStops.Length(),
                            aExtendMode, aTileSize, aTileSpacing);
}

void DisplayListBuilder::PushImage(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, bool aForceAntiAliasing,
    wr::ImageRendering aFilter, wr::ImageKey aImage, bool aPremultipliedAlpha,
    const wr::ColorF& aColor, bool aPreferCompositorSurface,
    bool aSupportsExternalCompositing) {
  wr::LayoutRect clip = MergeClipLeaf(aClip);
  WRDL_LOG("PushImage b=%s cl=%s\n", mWrState, ToString(aBounds).c_str(),
           ToString(clip).c_str());
  wr_dp_push_image(mWrState, aBounds, clip, aIsBackfaceVisible,
                   aForceAntiAliasing, &mCurrentSpaceAndClipChain, aFilter,
                   aImage, aPremultipliedAlpha, aColor,
                   aPreferCompositorSurface, aSupportsExternalCompositing);
}

void DisplayListBuilder::PushRepeatingImage(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, const wr::LayoutSize& aStretchSize,
    const wr::LayoutSize& aTileSpacing, wr::ImageRendering aFilter,
    wr::ImageKey aImage, bool aPremultipliedAlpha, const wr::ColorF& aColor) {
  wr::LayoutRect clip = MergeClipLeaf(aClip);
  WRDL_LOG("PushImage b=%s cl=%s s=%s t=%s\n", mWrState,
           ToString(aBounds).c_str(), ToString(clip).c_str(),
           ToString(aStretchSize).c_str(), ToString(aTileSpacing).c_str());
  wr_dp_push_repeating_image(
      mWrState, aBounds, clip, aIsBackfaceVisible, &mCurrentSpaceAndClipChain,
      aStretchSize, aTileSpacing, aFilter, aImage, aPremultipliedAlpha, aColor);
}

void DisplayListBuilder::PushYCbCrPlanarImage(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, wr::ImageKey aImageChannel0,
    wr::ImageKey aImageChannel1, wr::ImageKey aImageChannel2,
    wr::WrColorDepth aColorDepth, wr::WrYuvColorSpace aColorSpace,
    wr::WrColorRange aColorRange, wr::ImageRendering aRendering,
    bool aPreferCompositorSurface, bool aSupportsExternalCompositing) {
  wr_dp_push_yuv_planar_image(
      mWrState, aBounds, MergeClipLeaf(aClip), aIsBackfaceVisible,
      &mCurrentSpaceAndClipChain, aImageChannel0, aImageChannel1,
      aImageChannel2, aColorDepth, aColorSpace, aColorRange, aRendering,
      aPreferCompositorSurface, aSupportsExternalCompositing);
}

void DisplayListBuilder::PushNV12Image(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, wr::ImageKey aImageChannel0,
    wr::ImageKey aImageChannel1, wr::WrColorDepth aColorDepth,
    wr::WrYuvColorSpace aColorSpace, wr::WrColorRange aColorRange,
    wr::ImageRendering aRendering, bool aPreferCompositorSurface,
    bool aSupportsExternalCompositing) {
  wr_dp_push_yuv_NV12_image(
      mWrState, aBounds, MergeClipLeaf(aClip), aIsBackfaceVisible,
      &mCurrentSpaceAndClipChain, aImageChannel0, aImageChannel1, aColorDepth,
      aColorSpace, aColorRange, aRendering, aPreferCompositorSurface,
      aSupportsExternalCompositing);
}

void DisplayListBuilder::PushP010Image(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, wr::ImageKey aImageChannel0,
    wr::ImageKey aImageChannel1, wr::WrColorDepth aColorDepth,
    wr::WrYuvColorSpace aColorSpace, wr::WrColorRange aColorRange,
    wr::ImageRendering aRendering, bool aPreferCompositorSurface,
    bool aSupportsExternalCompositing) {
  wr_dp_push_yuv_P010_image(
      mWrState, aBounds, MergeClipLeaf(aClip), aIsBackfaceVisible,
      &mCurrentSpaceAndClipChain, aImageChannel0, aImageChannel1, aColorDepth,
      aColorSpace, aColorRange, aRendering, aPreferCompositorSurface,
      aSupportsExternalCompositing);
}

void DisplayListBuilder::PushNV16Image(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, wr::ImageKey aImageChannel0,
    wr::ImageKey aImageChannel1, wr::WrColorDepth aColorDepth,
    wr::WrYuvColorSpace aColorSpace, wr::WrColorRange aColorRange,
    wr::ImageRendering aRendering, bool aPreferCompositorSurface,
    bool aSupportsExternalCompositing) {
  wr_dp_push_yuv_NV16_image(
      mWrState, aBounds, MergeClipLeaf(aClip), aIsBackfaceVisible,
      &mCurrentSpaceAndClipChain, aImageChannel0, aImageChannel1, aColorDepth,
      aColorSpace, aColorRange, aRendering, aPreferCompositorSurface,
      aSupportsExternalCompositing);
}

void DisplayListBuilder::PushYCbCrInterleavedImage(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, wr::ImageKey aImageChannel0,
    wr::WrColorDepth aColorDepth, wr::WrYuvColorSpace aColorSpace,
    wr::WrColorRange aColorRange, wr::ImageRendering aRendering,
    bool aPreferCompositorSurface, bool aSupportsExternalCompositing) {
  wr_dp_push_yuv_interleaved_image(
      mWrState, aBounds, MergeClipLeaf(aClip), aIsBackfaceVisible,
      &mCurrentSpaceAndClipChain, aImageChannel0, aColorDepth, aColorSpace,
      aColorRange, aRendering, aPreferCompositorSurface,
      aSupportsExternalCompositing);
}

void DisplayListBuilder::PushIFrame(const LayoutDeviceRect& aDevPxBounds,
                                    bool aIsBackfaceVisible,
                                    PipelineId aPipeline,
                                    bool aIgnoreMissingPipeline) {
  // If the incoming bounds size has decimals (As it could when zoom is
  // involved), and is pushed straight through here, the compositor would end up
  // calculating the destination rect to paint the rendered iframe into
  // with those decimal values, rounding the result, instead of snapping. This
  // can cause the rendered iframe rect and its destination rect to be
  // mismatched, resulting in interpolation artifacts.
  auto snapped = aDevPxBounds;
  auto tl = snapped.TopLeft().Round();
  auto br = snapped.BottomRight().Round();

  snapped.SizeTo(LayoutDeviceSize(br.x - tl.x, br.y - tl.y));

  const auto bounds = wr::ToLayoutRect(snapped);
  wr_dp_push_iframe(mWrState, bounds, MergeClipLeaf(bounds), aIsBackfaceVisible,
                    &mCurrentSpaceAndClipChain, aPipeline,
                    aIgnoreMissingPipeline);
}

void DisplayListBuilder::PushBorder(const wr::LayoutRect& aBounds,
                                    const wr::LayoutRect& aClip,
                                    bool aIsBackfaceVisible,
                                    const wr::LayoutSideOffsets& aWidths,
                                    const Range<const wr::BorderSide>& aSides,
                                    const wr::BorderRadius& aRadius,
                                    wr::AntialiasBorder aAntialias) {
  MOZ_ASSERT(aSides.length() == 4);
  if (aSides.length() != 4) {
    return;
  }
  wr_dp_push_border(mWrState, aBounds, MergeClipLeaf(aClip), aIsBackfaceVisible,
                    &mCurrentSpaceAndClipChain, aAntialias, aWidths, aSides[0],
                    aSides[1], aSides[2], aSides[3], aRadius);
}

void DisplayListBuilder::PushBorderImage(const wr::LayoutRect& aBounds,
                                         const wr::LayoutRect& aClip,
                                         bool aIsBackfaceVisible,
                                         const wr::WrBorderImage& aParams) {
  wr_dp_push_border_image(mWrState, aBounds, MergeClipLeaf(aClip),
                          aIsBackfaceVisible, &mCurrentSpaceAndClipChain,
                          &aParams);
}

void DisplayListBuilder::PushBorderGradient(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, const wr::LayoutSideOffsets& aWidths,
    const int32_t aWidth, const int32_t aHeight, bool aFill,
    const wr::DeviceIntSideOffsets& aSlice, const wr::LayoutPoint& aStartPoint,
    const wr::LayoutPoint& aEndPoint, const nsTArray<wr::GradientStop>& aStops,
    wr::ExtendMode aExtendMode) {
  wr_dp_push_border_gradient(
      mWrState, aBounds, MergeClipLeaf(aClip), aIsBackfaceVisible,
      &mCurrentSpaceAndClipChain, aWidths, aWidth, aHeight, aFill, aSlice,
      aStartPoint, aEndPoint, aStops.Elements(), aStops.Length(), aExtendMode);
}

void DisplayListBuilder::PushBorderRadialGradient(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, const wr::LayoutSideOffsets& aWidths, bool aFill,
    const wr::LayoutPoint& aCenter, const wr::LayoutSize& aRadius,
    const nsTArray<wr::GradientStop>& aStops, wr::ExtendMode aExtendMode) {
  wr_dp_push_border_radial_gradient(
      mWrState, aBounds, MergeClipLeaf(aClip), aIsBackfaceVisible,
      &mCurrentSpaceAndClipChain, aWidths, aFill, aCenter, aRadius,
      aStops.Elements(), aStops.Length(), aExtendMode);
}

void DisplayListBuilder::PushBorderConicGradient(
    const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, const wr::LayoutSideOffsets& aWidths, bool aFill,
    const wr::LayoutPoint& aCenter, const float aAngle,
    const nsTArray<wr::GradientStop>& aStops, wr::ExtendMode aExtendMode) {
  wr_dp_push_border_conic_gradient(
      mWrState, aBounds, MergeClipLeaf(aClip), aIsBackfaceVisible,
      &mCurrentSpaceAndClipChain, aWidths, aFill, aCenter, aAngle,
      aStops.Elements(), aStops.Length(), aExtendMode);
}

void DisplayListBuilder::PushText(const wr::LayoutRect& aBounds,
                                  const wr::LayoutRect& aClip,
                                  bool aIsBackfaceVisible,
                                  const wr::ColorF& aColor,
                                  wr::FontInstanceKey aFontKey,
                                  Range<const wr::GlyphInstance> aGlyphBuffer,
                                  const wr::GlyphOptions* aGlyphOptions) {
  wr_dp_push_text(mWrState, aBounds, MergeClipLeaf(aClip), aIsBackfaceVisible,
                  &mCurrentSpaceAndClipChain, aColor, aFontKey,
                  &aGlyphBuffer[0], aGlyphBuffer.length(), aGlyphOptions);
}

void DisplayListBuilder::PushLine(const wr::LayoutRect& aClip,
                                  bool aIsBackfaceVisible,
                                  const wr::Line& aLine) {
  wr::LayoutRect clip = MergeClipLeaf(aClip);
  wr_dp_push_line(mWrState, &clip, aIsBackfaceVisible,
                  &mCurrentSpaceAndClipChain, &aLine.bounds,
                  aLine.wavyLineThickness, aLine.orientation, &aLine.color,
                  aLine.style);
}

void DisplayListBuilder::PushShadow(const wr::LayoutRect& aRect,
                                    const wr::LayoutRect& aClip,
                                    bool aIsBackfaceVisible,
                                    const wr::Shadow& aShadow,
                                    bool aShouldInflate) {
  // Local clip_rects are translated inside of shadows, as they are assumed to
  // be part of the element drawing itself, and not a parent frame clipping it.
  // As such, it is not sound to apply the MergeClipLeaf optimization inside of
  // shadows. So we disable the optimization when we encounter a shadow.
  // Shadows don't span frames, so we don't have to worry about MergeClipLeaf
  // being re-enabled mid-shadow. The optimization is restored in PopAllShadows.
  SuspendClipLeafMerging();
  wr_dp_push_shadow(mWrState, aRect, aClip, aIsBackfaceVisible,
                    &mCurrentSpaceAndClipChain, aShadow, aShouldInflate);
}

void DisplayListBuilder::PopAllShadows() {
  wr_dp_pop_all_shadows(mWrState);
  ResumeClipLeafMerging();
}

void DisplayListBuilder::SuspendClipLeafMerging() {
  if (mClipChainLeaf) {
    // No one should reinitialize mClipChainLeaf while we're suspended
    MOZ_ASSERT(!mSuspendedClipChainLeaf);

    mSuspendedClipChainLeaf = mClipChainLeaf;
    mSuspendedSpaceAndClipChain = Some(mCurrentSpaceAndClipChain);

    auto clipId = DefineRectClip(Nothing(), *mClipChainLeaf);
    auto clipChainId = DefineClipChain({clipId}, true);

    mCurrentSpaceAndClipChain.clip_chain = clipChainId.id;
    mClipChainLeaf = Nothing();
  }
}

void DisplayListBuilder::ResumeClipLeafMerging() {
  if (mSuspendedClipChainLeaf) {
    mCurrentSpaceAndClipChain = *mSuspendedSpaceAndClipChain;
    mClipChainLeaf = mSuspendedClipChainLeaf;

    mSuspendedClipChainLeaf = Nothing();
    mSuspendedSpaceAndClipChain = Nothing();
  }
}

void DisplayListBuilder::PushBoxShadow(
    const wr::LayoutRect& aRect, const wr::LayoutRect& aClip,
    bool aIsBackfaceVisible, const wr::LayoutRect& aBoxBounds,
    const wr::LayoutVector2D& aOffset, const wr::ColorF& aColor,
    const float& aBlurRadius, const float& aSpreadRadius,
    const wr::BorderRadius& aBorderRadius,
    const wr::BoxShadowClipMode& aClipMode) {
  wr_dp_push_box_shadow(mWrState, aRect, MergeClipLeaf(aClip),
                        aIsBackfaceVisible, &mCurrentSpaceAndClipChain,
                        aBoxBounds, aOffset, aColor, aBlurRadius, aSpreadRadius,
                        aBorderRadius, aClipMode);
}

void DisplayListBuilder::PushDebug(uint32_t aVal) {
  wr_dp_push_debug(mWrState, aVal);
}

void DisplayListBuilder::StartGroup(nsPaintedDisplayItem* aItem) {
  if (!mDisplayItemCache || mDisplayItemCache->IsFull()) {
    return;
  }

  MOZ_ASSERT(!mCurrentCacheSlot);
  mCurrentCacheSlot = mDisplayItemCache->AssignSlot(aItem);

  if (mCurrentCacheSlot) {
    wr_dp_start_item_group(mWrState);
  }
}

void DisplayListBuilder::CancelGroup(const bool aDiscard) {
  if (!mDisplayItemCache || !mCurrentCacheSlot) {
    return;
  }

  wr_dp_cancel_item_group(mWrState, aDiscard);
  mCurrentCacheSlot = Nothing();
}

void DisplayListBuilder::FinishGroup() {
  if (!mDisplayItemCache || !mCurrentCacheSlot) {
    return;
  }

  MOZ_ASSERT(mCurrentCacheSlot);

  if (wr_dp_finish_item_group(mWrState, mCurrentCacheSlot.ref())) {
    mDisplayItemCache->MarkSlotOccupied(mCurrentCacheSlot.ref(),
                                        CurrentSpaceAndClipChain());
    mDisplayItemCache->Stats().AddCached();
  }

  mCurrentCacheSlot = Nothing();
}

bool DisplayListBuilder::ReuseItem(nsPaintedDisplayItem* aItem) {
  if (!mDisplayItemCache) {
    return false;
  }

  mDisplayItemCache->Stats().AddTotal();

  if (mDisplayItemCache->IsEmpty()) {
    return false;
  }

  Maybe<uint16_t> slot =
      mDisplayItemCache->CanReuseItem(aItem, CurrentSpaceAndClipChain());

  if (slot) {
    mDisplayItemCache->Stats().AddReused();
    wr_dp_push_reuse_items(mWrState, slot.ref());
    return true;
  }

  return false;
}

Maybe<layers::ScrollableLayerGuid::ViewID>
DisplayListBuilder::GetContainingFixedPosScrollTarget(
    const ActiveScrolledRoot* aAsr) {
  return mActiveFixedPosTracker
             ? mActiveFixedPosTracker->GetScrollTargetForASR(aAsr)
             : Nothing();
}

Maybe<SideBits> DisplayListBuilder::GetContainingFixedPosSideBits(
    const ActiveScrolledRoot* aAsr) {
  return mActiveFixedPosTracker
             ? mActiveFixedPosTracker->GetSideBitsForASR(aAsr)
             : Nothing();
}

DisplayListBuilder::FixedPosScrollTargetTracker::FixedPosScrollTargetTracker(
    DisplayListBuilder& aBuilder, const ActiveScrolledRoot* aAsr,
    layers::ScrollableLayerGuid::ViewID aScrollId, SideBits aSideBits)
    : mParentTracker(aBuilder.mActiveFixedPosTracker),
      mBuilder(aBuilder),
      mAsr(aAsr),
      mScrollId(aScrollId),
      mSideBits(aSideBits) {
  aBuilder.mActiveFixedPosTracker = this;
}

DisplayListBuilder::FixedPosScrollTargetTracker::
    ~FixedPosScrollTargetTracker() {
  mBuilder.mActiveFixedPosTracker = mParentTracker;
}

Maybe<layers::ScrollableLayerGuid::ViewID>
DisplayListBuilder::FixedPosScrollTargetTracker::GetScrollTargetForASR(
    const ActiveScrolledRoot* aAsr) {
  return aAsr == mAsr ? Some(mScrollId) : Nothing();
}

Maybe<SideBits>
DisplayListBuilder::FixedPosScrollTargetTracker::GetSideBitsForASR(
    const ActiveScrolledRoot* aAsr) {
  return aAsr == mAsr ? Some(mSideBits) : Nothing();
}

gfxContext* DisplayListBuilder::GetTextContext(
    wr::IpcResourceUpdateQueue& aResources,
    const layers::StackingContextHelper& aSc,
    layers::RenderRootStateManager* aManager, nsDisplayItem* aItem,
    nsRect& aBounds, const gfx::Point& aDeviceOffset) {
  if (!mCachedTextDT) {
    mCachedTextDT = new layout::TextDrawTarget(*this, aResources, aSc, aManager,
                                               aItem, aBounds);
    if (mCachedTextDT->IsValid()) {
      mCachedContext = MakeUnique<gfxContext>(mCachedTextDT, aDeviceOffset);
    }
  } else {
    mCachedTextDT->Reinitialize(aResources, aSc, aManager, aItem, aBounds);
    mCachedContext->SetDeviceOffset(aDeviceOffset);
    mCachedContext->SetMatrix(gfx::Matrix());
  }

  return mCachedContext.get();
}

void DisplayListBuilder::PushInheritedClipChain(
    nsDisplayListBuilder* aBuilder, const DisplayItemClipChain* aClipChain) {
  if (!aClipChain || mInheritedClipChain == aClipChain) {
    return;
  }
  if (!mInheritedClipChain) {
    mInheritedClipChain = aClipChain;
    return;
  }

  mInheritedClipChain =
      aBuilder->CreateClipChainIntersection(mInheritedClipChain, aClipChain);
}

}  // namespace wr
}  // namespace mozilla

extern "C" {

void wr_transaction_notification_notified(uintptr_t aHandler,
                                          mozilla::wr::Checkpoint aWhen) {
  auto handler = reinterpret_cast<mozilla::wr::NotificationHandler*>(aHandler);
  handler->Notify(aWhen);
  // TODO: it would be better to get a callback when the object is destroyed on
  // the rust side and delete then.
  delete handler;
}

void wr_register_thread_local_arena() {
#ifdef MOZ_MEMORY
  jemalloc_thread_local_arena(true);
#endif
}

}  // extern C
