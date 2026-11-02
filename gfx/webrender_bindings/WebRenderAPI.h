/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_WEBRENDERAPI_H
#define MOZILLA_LAYERS_WEBRENDERAPI_H

#include <stdint.h>

#include <queue>
#include <unordered_map>
#include <vector>

#include "GLTypes.h"
#include "Units.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Range.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/VsyncDispatcher.h"
#include "mozilla/gfx/CompositorHitTestInfo.h"
#include "mozilla/layers/AsyncImagePipelineOp.h"
#include "mozilla/layers/CompositionRecorder.h"
#include "mozilla/layers/IpcResourceUpdateQueue.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/layers/ScrollableLayerGuid.h"
#include "mozilla/layers/SyncObject.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "mozilla/webrender/webrender_ffi.h"
#include "nsString.h"

class gfxContext;

#undef None

namespace mozilla {

class nsDisplayItem;
class nsPaintedDisplayItem;
class nsDisplayTransform;
class nsDisplayListBuilder;
struct DisplayItemClipChain;

struct ActiveScrolledRoot;

namespace widget {
class CompositorWidget;
}

namespace layers {
class CompositorBridgeParent;
class WebRenderBridgeParent;
class RenderRootStateManager;
class StackingContextHelper;
struct DisplayListData;
}  // namespace layers

namespace layout {
class TextDrawTarget;
}

namespace wr {

class DisplayListBuilder;
class RendererOGL;
class RendererEvent;
class WebRenderAPI;

// This isn't part of WR's API, but we define it here to simplify layout's
// logic and data plumbing.
struct Line {
  wr::LayoutRect bounds;
  float wavyLineThickness;
  wr::LineOrientation orientation;
  wr::ColorF color;
  wr::LineStyle style;
};

struct WebRenderCapabilities {
  layers::WebRenderBackend mBackendType = layers::WebRenderBackend::HARDWARE;
  layers::WebRenderCompositor mCompositorType =
      layers::WebRenderCompositor::DRAW;
  int32_t mMaxTextureSize = 0;
  bool mUseANGLE = false;
  bool mUseDComp = false;
  bool mUseLayerCompositor = false;
  bool mUseTripleBuffering = false;
  bool mSupportsExternalBufferTextures = false;
#ifdef XP_DARWIN
  wr::ImageBufferKind mIOSurfaceImageKind = wr::ImageBufferKind::Texture2D;
#endif
};

/// A handler that can be bundled into a transaction and notified at specific
/// points in the rendering pipeline, such as after scene building or after
/// frame building.
///
/// If for any reason the handler is dropped before reaching the requested
/// point, it is notified with the value Checkpoint::TransactionDropped.
/// So it is safe to assume that the handler will be notified "at some point".
class NotificationHandler {
 public:
  virtual void Notify(wr::Checkpoint aCheckpoint) = 0;
  virtual ~NotificationHandler() = default;
};

struct WrHitResult {
  layers::LayersId mLayersId;
  layers::ScrollableLayerGuid::ViewID mScrollId;
  gfx::CompositorHitTestInfo mHitInfo;
  SideBits mSideBits;
  Maybe<uint64_t> mAnimationId;
};

class TransactionBuilder final {
 public:
  explicit TransactionBuilder(
      WebRenderAPI* aApi, bool aUseSceneBuilderThread = true,
      layers::RemoteTextureTxnScheduler* aRemoteTextureTxnScheduler = nullptr,
      layers::RemoteTextureTxnId aRemoteTextureTxnId = 0);

  TransactionBuilder(
      WebRenderAPI* aApi, Transaction* aTxn, bool aUseSceneBuilderThread,
      bool aOwnsData,
      layers::RemoteTextureTxnScheduler* aRemoteTextureTxnScheduler,
      layers::RemoteTextureTxnId aRemoteTextureTxnId);

  ~TransactionBuilder();

  void SetLowPriority(bool aIsLowPriority);

  void UpdateEpoch(PipelineId aPipelineId, Epoch aEpoch);

  void SetRootPipeline(PipelineId aPipelineId);

  void RemovePipeline(PipelineId aPipelineId);

  // Sets the namespaces of the pipelines that aPipelineId may embed with
  // iframe items. Nothing() lets it embed any pipeline.
  void SetEmbedPolicy(PipelineId aPipelineId,
                      const Maybe<nsTArray<uint32_t>>& aNamespaces);

  // aIdNamespace is the namespace whose resources the display list is allowed
  // to reference. It must be supplied by the compositor-side owner of the
  // pipeline.
  void SetDisplayList(Epoch aEpoch, wr::IdNamespace aIdNamespace,
                      wr::WrPipelineId pipeline_id,
                      wr::BuiltDisplayListDescriptor dl_descriptor,
                      wr::Vec<uint8_t>& dl_items_data,
                      wr::Vec<uint8_t>& dl_spatial_tree,
                      wr::Vec<uint8_t>& dl_interner_delta);

  void ClearDisplayList(Epoch aEpoch, wr::IdNamespace aIdNamespace,
                        wr::WrPipelineId aPipeline);

  void GenerateFrame(const VsyncId& aVsyncId, bool aPresent, bool aTracked,
                     wr::RenderReasons aReasons);

  void InvalidateRenderedFrame(wr::RenderReasons aReasons);

  void SetDocumentView(const LayoutDeviceIntRect& aDocRect);

  bool IsEmpty() const;

  bool IsResourceUpdatesEmpty() const;

  bool IsRenderedFrameInvalidated() const;

  void AddImage(wr::ImageKey aKey, const ImageDescriptor& aDescriptor,
                wr::Vec<uint8_t>& aBytes);

  void AddBlobImage(wr::BlobImageKey aKey, const ImageDescriptor& aDescriptor,
                    uint16_t aTileSize, wr::Vec<uint8_t>& aBytes,
                    const wr::DeviceIntRect& aVisibleRect);

  void AddExternalImageBuffer(ImageKey key, const ImageDescriptor& aDescriptor,
                              ExternalImageId aHandle);

  void AddExternalImage(ImageKey key, const ImageDescriptor& aDescriptor,
                        ExternalImageId aExtID,
                        wr::ExternalImageType aImageType,
                        uint8_t aChannelIndex = 0, bool aNormalizedUvs = false);

  void UpdateImageBuffer(wr::ImageKey aKey, const ImageDescriptor& aDescriptor,
                         wr::Vec<uint8_t>& aBytes);

  void UpdateBlobImage(wr::BlobImageKey aKey,
                       const ImageDescriptor& aDescriptor,
                       wr::Vec<uint8_t>& aBytes,
                       const wr::DeviceIntRect& aVisibleRect,
                       const wr::LayoutIntRect& aDirtyRect);

  void UpdateExternalImage(ImageKey aKey, const ImageDescriptor& aDescriptor,
                           ExternalImageId aExtID,
                           wr::ExternalImageType aImageType,
                           uint8_t aChannelIndex = 0,
                           bool aNormalizedUvs = false);

  void UpdateExternalImageWithDirtyRect(
      ImageKey aKey, const ImageDescriptor& aDescriptor, ExternalImageId aExtID,
      wr::ExternalImageType aImageType, const wr::DeviceIntRect& aDirtyRect,
      uint8_t aChannelIndex = 0, bool aNormalizedUvs = false);

  void SetBlobImageVisibleArea(BlobImageKey aKey,
                               const wr::DeviceIntRect& aArea);

  void DeleteImage(wr::ImageKey aKey);

  void DeleteBlobImage(wr::BlobImageKey aKey);

  void AddSnapshotImage(wr::SnapshotImageKey aKey);

  void DeleteSnapshotImage(wr::SnapshotImageKey aKey);

  void AdoptSnapshotImage(wr::SnapshotImageKey aFrom, wr::SnapshotImageKey aTo);

  void AddRawFont(wr::FontKey aKey, wr::Vec<uint8_t>& aBytes, uint32_t aIndex);

  void AddFontDescriptor(wr::FontKey aKey, wr::Vec<uint8_t>& aBytes,
                         uint32_t aIndex);

  void DeleteFont(wr::FontKey aKey);

  void AddFontInstance(wr::FontInstanceKey aKey, wr::FontKey aFontKey,
                       float aGlyphSize,
                       const wr::FontInstanceOptions* aOptions,
                       const wr::FontInstancePlatformOptions* aPlatformOptions,
                       wr::Vec<uint8_t>& aVariations);

  void DeleteFontInstance(wr::FontInstanceKey aKey);

  void UpdateQualitySettings(bool aForceSubpixelAAWherePossible);

  void RenderOffscreen(wr::WrPipelineId aPipelineId);

  void Notify(wr::Checkpoint aWhen, UniquePtr<NotificationHandler> aHandler);

  void Clear();

  Transaction* Take();

  bool UseSceneBuilderThread() const { return mUseSceneBuilderThread; }
  const WebRenderCapabilities& GetCapabilities() const { return mCapabilities; }
  Transaction* Raw() const { return mTxn; }

  const RefPtr<layers::RemoteTextureTxnScheduler> mRemoteTextureTxnScheduler;
  const layers::RemoteTextureTxnId mRemoteTextureTxnId;

 protected:
  Transaction* mTxn;
  bool mUseSceneBuilderThread;
  WebRenderCapabilities mCapabilities;
  bool mOwnsData;
};

class TransactionWrapper final {
 public:
  explicit TransactionWrapper(Transaction* aTxn);

  void AppendDynamicProperties(
      const nsTArray<wr::WrOpacityProperty>& aOpacityArray,
      const nsTArray<wr::WrTransformProperty>& aTransformArray,
      const nsTArray<wr::WrColorProperty>& aColorArray);
  void AppendTransformProperties(
      const nsTArray<wr::WrTransformProperty>& aTransformArray);
  void UpdateScrollPosition(
      const wr::ExternalScrollId& aScrollId,
      const nsTArray<wr::SampledScrollOffset>& aSampledOffsets);
  void UpdateIsTransformAsyncZooming(uint64_t aAnimationId, bool aIsZooming);
  void AddMinimapData(const wr::ExternalScrollId& aScrollId,
                      const MinimapData& aMinimapData);

 private:
  Transaction* mTxn;
};

class WebRenderAPI final {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WebRenderAPI);

 public:
  using CreatePromise = MozPromise<RefPtr<WebRenderAPI>, nsCString, true>;
  // Dispatches a task to the Renderer thread to create the WebRenderAPI.
  static RefPtr<CreatePromise> Create(
      RefPtr<layers::CompositorBridgeParent> aBridge,
      RefPtr<widget::CompositorWidget> aWidget, const wr::WrWindowId& aWindowId,
      LayoutDeviceIntSize aSize, layers::WindowKind aWindowKind);

  already_AddRefed<WebRenderAPI> Clone();

  void DestroyRenderer();

  wr::WindowId GetId() const { return mId; }

  /// Do a non-blocking hit-testing query on a shared version of the hit
  /// testing information.
  std::vector<WrHitResult> HitTest(const wr::WorldPoint& aPoint);

  void SendTransaction(TransactionBuilder& aTxn);

  void SetFrameStartTime(const TimeStamp& aTime);

  void RunOnRenderThread(UniquePtr<RendererEvent> aEvent);

  void Readback(const TimeStamp& aStartTime, gfx::IntSize aSize,
                const gfx::SurfaceFormat& aFormat,
                const mozilla::Range<uint8_t>& aBuffer, bool* aNeedsYFlip);

  void ClearAllCaches();
  void SetBatchingLookback(uint32_t aCount);
  void SetBool(wr::BoolParameter, bool value);
  void SetInt(wr::IntParameter, int32_t value);
  void SetFloat(wr::FloatParameter, float value);

  void SetClearColor(const gfx::DeviceColor& aColor);
  void SetProfilerUI(const nsACString& aUIString);

  void Pause();
  bool Resume();

  void WakeSceneBuilder();
  void FlushSceneBuilder();

  void NotifyMemoryPressure();
  void AccumulateMemoryReport(wr::MemoryReport*);

  wr::WrIdNamespace GetNamespace();
  const WebRenderCapabilities& GetCapabilities() const { return mCapabilities; }
  layers::SyncHandle GetSyncHandle() const { return mSyncHandle; }

  void Capture();

  void StartCaptureSequence(uint32_t aFlags);
  void StopCaptureSequence();

  void BeginRecording(const TimeStamp& aRecordingStart,
                      wr::PipelineId aRootPipelineId);

  typedef MozPromise<layers::FrameRecording, nsresult, true>
      EndRecordingPromise;

  RefPtr<EndRecordingPromise> EndRecording();

#ifdef MOZ_WIDGET_ANDROID
  using ScreenPixelsPromise = MozPromise<Ok, nsresult, true>;
  // Queues a task to the render thread to capture screen pixels for the next
  // rendered frame. Returns a promise that resolves once the pixels are
  // captured.
  RefPtr<ScreenPixelsPromise> RequestScreenPixels(
      gfx::IntRect aSourceRect,
      RefPtr<layers::AndroidHardwareBuffer> aHardwareBuffer);
#endif

  layers::RemoteTextureInfoList* GetPendingRemoteTextureInfoList();
  layers::AsyncImagePipelineOps* GetPendingAsyncImagePipelineOps(
      TransactionBuilder& aTxn);

  void FlushPendingWrTransactionEventsWithoutWait();
  void FlushPendingWrTransactionEventsWithWait();

  wr::WebRenderAPI* GetRootAPI();

  bool CheckAndClearDidRasterize();

 protected:
  WebRenderAPI(wr::DocumentHandle* aHandle, wr::WindowId aId,
               WebRenderCapabilities aCapabilities,
               layers::SyncHandle aSyncHandle,
               wr::WebRenderAPI* aRootApi = nullptr,
               wr::WebRenderAPI* aRootDocumentApi = nullptr);

  ~WebRenderAPI();

  void WaitUntilPresentationFlushed();

  void UpdateDebugFlags(uint64_t aFlags);
  bool CheckIsRemoteTextureReady(layers::RemoteTextureInfoList* aList,
                                 const TimeStamp& aTimeStamp);
  void WaitRemoteTextureReady(layers::RemoteTextureInfoList* aList);

  enum class RemoteTextureWaitType : uint8_t {
    AsyncWait = 0,
    FlushWithWait = 1,
    FlushWithoutWait = 2
  };

  void HandleWrTransactionEvents(RemoteTextureWaitType aType);

  class WrTransactionEvent {
   public:
    enum class Tag {
      Transaction,
      PendingRemoteTextures,
      PendingAsyncImagePipelineOps,
    };
    const Tag mTag;
    const TimeStamp mTimeStamp;

   private:
    WrTransactionEvent(const Tag aTag,
                       UniquePtr<TransactionBuilder>&& aTransaction)
        : mTag(aTag),
          mTimeStamp(TimeStamp::Now()),
          mTransaction(std::move(aTransaction)) {
      MOZ_ASSERT(mTag == Tag::Transaction);
    }
    WrTransactionEvent(
        const Tag aTag,
        UniquePtr<layers::RemoteTextureInfoList>&& aPendingRemoteTextures)
        : mTag(aTag),
          mTimeStamp(TimeStamp::Now()),
          mPendingRemoteTextures(std::move(aPendingRemoteTextures)) {
      MOZ_ASSERT(mTag == Tag::PendingRemoteTextures);
    }
    WrTransactionEvent(const Tag aTag,
                       UniquePtr<layers::AsyncImagePipelineOps>&&
                           aPendingAsyncImagePipelineOps,
                       UniquePtr<TransactionBuilder>&& aTransaction)
        : mTag(aTag),
          mTimeStamp(TimeStamp::Now()),
          mPendingAsyncImagePipelineOps(
              std::move(aPendingAsyncImagePipelineOps)),
          mTransaction(std::move(aTransaction)) {
      MOZ_ASSERT(mTag == Tag::PendingAsyncImagePipelineOps);
    }

    UniquePtr<layers::RemoteTextureInfoList> mPendingRemoteTextures;
    UniquePtr<layers::AsyncImagePipelineOps> mPendingAsyncImagePipelineOps;
    UniquePtr<TransactionBuilder> mTransaction;

   public:
    static WrTransactionEvent Transaction(WebRenderAPI* aApi,
                                          TransactionBuilder& aTxn) {
      auto transaction = std::make_unique<TransactionBuilder>(
          aApi, aTxn.Take(), aTxn.UseSceneBuilderThread(), /* aOwnsData */ true,
          aTxn.mRemoteTextureTxnScheduler, aTxn.mRemoteTextureTxnId);
      return WrTransactionEvent(Tag::Transaction, std::move(transaction));
    }

    static WrTransactionEvent PendingRemoteTextures(
        UniquePtr<layers::RemoteTextureInfoList>&& aPendingRemoteTextures) {
      return WrTransactionEvent(Tag::PendingRemoteTextures,
                                std::move(aPendingRemoteTextures));
    }

    static WrTransactionEvent PendingAsyncImagePipelineOps(
        UniquePtr<layers::AsyncImagePipelineOps>&&
            aPendingAsyncImagePipelineOps,
        WebRenderAPI* aApi, const TransactionBuilder& aTxn) {
      auto transaction = std::make_unique<TransactionBuilder>(
          aApi, aTxn.Raw(), aTxn.UseSceneBuilderThread(), /* aOwnsData */ false,
          aTxn.mRemoteTextureTxnScheduler, aTxn.mRemoteTextureTxnId);
      return WrTransactionEvent(Tag::PendingAsyncImagePipelineOps,
                                std::move(aPendingAsyncImagePipelineOps),
                                std::move(transaction));
    }

    wr::Transaction* RawTransaction() {
      if (mTag == Tag::Transaction) {
        return mTransaction->Raw();
      }
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return nullptr;
    }

    TransactionBuilder* GetTransactionBuilder() {
      if (mTag == Tag::Transaction ||
          mTag == Tag::PendingAsyncImagePipelineOps) {
        return mTransaction.get();
      }
      MOZ_CRASH("Should not be called");
      return nullptr;
    }

    bool UseSceneBuilderThread() {
      if (mTag == Tag::Transaction) {
        return mTransaction->UseSceneBuilderThread();
      }
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return true;
    }

    layers::RemoteTextureInfoList* RemoteTextureInfoList() {
      if (mTag == Tag::PendingRemoteTextures) {
        MOZ_ASSERT(mPendingRemoteTextures);
        return mPendingRemoteTextures.get();
      }
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return nullptr;
    }

    layers::AsyncImagePipelineOps* AsyncImagePipelineOps() {
      if (mTag == Tag::PendingAsyncImagePipelineOps) {
        MOZ_ASSERT(mPendingAsyncImagePipelineOps);
        return mPendingAsyncImagePipelineOps.get();
      }
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return nullptr;
    }
  };

  wr::DocumentHandle* mDocHandle;
  wr::WindowId mId;
  const WebRenderCapabilities mCapabilities;
  bool mCaptureSequence;
  layers::SyncHandle mSyncHandle;
  bool mRendererDestroyed;

  UniquePtr<layers::RemoteTextureInfoList> mPendingRemoteTextureInfoList;
  UniquePtr<layers::AsyncImagePipelineOps> mPendingAsyncImagePipelineOps;
  std::queue<WrTransactionEvent> mPendingWrTransactionEvents;

  // We maintain alive the root api to know when to shut the render backend
  // down, and the root api for the document to know when to delete the
  // document. mRootApi is null for the api object that owns the channel (and is
  // responsible for shutting it down), and mRootDocumentApi is null for the api
  // object owning (and responsible for destroying) a given document. All api
  // objects in the same window use the same channel, and some api objects write
  // to the same document (but there is only one owner for each channel and for
  // each document).
  const RefPtr<wr::WebRenderAPI> mRootApi;
  const RefPtr<wr::WebRenderAPI> mRootDocumentApi;

  friend class DisplayListBuilder;
  friend class layers::WebRenderBridgeParent;
};

// This is a RAII class that automatically sends the transaction on
// destruction. This is useful for code that has multiple exit points and we
// want to ensure that the stuff accumulated in the transaction gets sent
// regardless of which exit we take. Note that if the caller explicitly calls
// mApi->SendTransaction() that's fine too because that empties out the
// TransactionBuilder and leaves it as a valid empty transaction, so calling
// SendTransaction on it again ends up being a no-op.
class MOZ_RAII AutoTransactionSender {
 public:
  AutoTransactionSender(WebRenderAPI* aApi, TransactionBuilder* aTxn)
      : mApi(aApi), mTxn(aTxn) {
    MOZ_RELEASE_ASSERT(mApi);
    MOZ_RELEASE_ASSERT(aTxn);
  }

  ~AutoTransactionSender() { mApi->SendTransaction(*mTxn); }

 private:
  WebRenderAPI* mApi;
  TransactionBuilder* mTxn;
};

/**
 * The parameters of a reference frame. By default an identity, flat,
 * non-animated transform reference frame.
 */
struct MOZ_STACK_CLASS ReferenceFrameParams : public WrReferenceFrameParams {
  ReferenceFrameParams()
      : WrReferenceFrameParams{wr::ToLayoutTransform(gfx::Matrix4x4()),
                               /* animation_id = */ 0,
                               wr::TransformStyle::Flat,
                               wr::WrReferenceFrameKind::Transform,
                               /* is_2d_scale_translation = */ false,
                               /* should_snap = */ false,
                               /* paired_with_perspective = */ false,
                               /* scrolling_relative_to = */ nullptr} {}

  void SetPreserve3D(bool aPreserve) {
    transform_style =
        aPreserve ? wr::TransformStyle::Preserve3D : wr::TransformStyle::Flat;
  }
};

/**
 * A set of optional parameters for stacking context creation.
 */
struct MOZ_STACK_CLASS StackingContextParams : public WrStackingContextParams {
  StackingContextParams()
      : WrStackingContextParams{
            WrStackingContextClip::None(),
            nullptr,
            nullptr,
            nullptr,
            wr::TransformStyle::Flat,
            /* prim_flags = */ wr::PrimitiveFlags::IS_BACKFACE_VISIBLE,
            wr::MixBlendMode::Normal,
            wr::StackingContextFlags{0}} {}

  void SetPreserve3D(bool aPreserve) {
    transform_style =
        aPreserve ? wr::TransformStyle::Preserve3D : wr::TransformStyle::Flat;
  }

  // Fill this in only if this is for the root StackingContextHelper.
  nsIFrame* mRootReferenceFrame = nullptr;
  nsTArray<wr::FilterOp> mFilters;
  nsTArray<wr::WrFilterData> mFilterDatas;
  wr::LayoutRect mBounds = wr::ToLayoutRect(LayoutDeviceRect());
  const gfx::Matrix4x4* mBoundTransform = nullptr;
  nsDisplayTransform* mDeferredTransformItem = nullptr;
  // Whether the stacking context is possibly animated. This alters how
  // coordinates are transformed/snapped to invalidate less when transforms
  // change frequently.
  bool mAnimated = false;
};

/// This is a simple C++ wrapper around WrState defined in the rust bindings.
/// We may want to turn this into a direct wrapper on top of
/// WebRenderFrameBuilder instead, so the interface may change a bit.
class DisplayListBuilder final {
 public:
  explicit DisplayListBuilder(wr::PipelineId aId,
                              layers::WebRenderBackend aBackend);
  DisplayListBuilder(DisplayListBuilder&&) = default;

  ~DisplayListBuilder();

  void Save();
  void Restore();
  void ClearSave();

  usize Dump(usize aIndent, const Maybe<usize>& aStart,
             const Maybe<usize>& aEnd);
  void DumpSerializedDisplayList();

  /// aAppUnitsPerDevPixel is the grid this display list's coordinates are
  /// authored on (nsPresContext::AppUnitsPerDevPixel). WebRender normalizes
  /// items by their accumulated external scroll offset in whole app units on
  /// that grid, which is exact; doing it in floats lets positions drift with
  /// the scroll offset and churn interning and invalidation (bug 2059570). It
  /// is taken per display list because it changes with device scale and full
  /// zoom, and differs between chrome and content within one WebRender
  /// document.
  void Begin(int32_t aAppUnitsPerDevPixel);
  void End(wr::BuiltDisplayList& aOutDisplayList);
  void End(layers::DisplayListData& aOutTransaction);

  // Reference frames are children of the current space. aOrigin is their
  // offset in it.
  wr::WrSpatialId DefineReferenceFrame(const wr::LayoutPoint& aOrigin,
                                       const ReferenceFrameParams& aParams);
  wr::WrSpatialId DefineComputedFrame(const wr::LayoutPoint& aOrigin,
                                      const wr::WrComputedTransformData& aData);

  // The stacking context is in aSpatialId if given, else in the current space.
  void PushStackingContext(const StackingContextParams& aParams,
                           const wr::RasterSpace& aRasterSpace,
                           const Maybe<wr::WrSpatialId>& aSpatialId);
  void PopStackingContext();

  wr::WrClipChainId DefineClipChain(Span<const wr::WrClipId> aClips,
                                    const Maybe<wr::WrClipChainId>& aParent);

  wr::WrClipId DefineImageMaskClip(const wr::ImageMask& aMask,
                                   const nsTArray<wr::LayoutPoint>&,
                                   wr::FillRule);
  // aAntiAliased keeps the clip at its exact position instead of snapping it
  // to the device pixel grid. It is meant for clips defined in the same space
  // as anti-aliased primitives, such as SVG user space.
  wr::WrClipId DefineRoundedRectClip(Maybe<wr::WrSpatialId> aSpace,
                                     const wr::ComplexClipRegion& aComplex,
                                     bool aAntiAliased = false);
  wr::WrClipId DefineBorderClip(
      const wr::LayoutRect& aRect, const wr::LayoutSideOffsets& aWidths,
      const mozilla::Range<const wr::BorderSide>& aSides,
      const wr::BorderRadius& aRadius, const wr::LayoutSideOffsets& aInset);
  wr::WrClipId DefineRectClip(Maybe<wr::WrSpatialId> aSpace,
                              wr::LayoutRect aClipRect,
                              bool aAntiAliased = false);
  // Clips to the inside of a path in the current space. See PathCommand for
  // the number of points each command consumes. The points are relative to
  // aRect's origin, aAabb contains all of them, and aRect also clips the
  // path's content.
  wr::WrClipId DefinePathClip(Span<const wr::PathCommand> aCommands,
                              Span<const wr::LayoutPoint> aPoints,
                              const wr::LayoutRect& aAabb,
                              const wr::LayoutRect& aRect,
                              wr::FillRule aFillRule);

  wr::WrSpatialId DefineStickyFrame(const ActiveScrolledRoot* aStickyAsr,
                                    Maybe<wr::WrSpatialId> aParentSpatialId,
                                    const wr::LayoutRect& aContentRect,
                                    const float* aTopMargin,
                                    const float* aRightMargin,
                                    const float* aBottomMargin,
                                    const float* aLeftMargin,
                                    const StickyOffsetBounds& aVerticalBounds,
                                    const StickyOffsetBounds& aHorizontalBounds,
                                    const wr::LayoutVector2D& aAppliedOffset,
                                    const WrAnimationProperty* aAnimation);

  Maybe<wr::WrSpatialId> GetSpatialIdForDefinedLayer(
      const ActiveScrolledRoot* aASR) const;

  // The external scroll offset WebRender adds to items pushed into the current
  // space, moving them from the pre-scrolled coordinates they are authored in
  // to the scroll-invariant ones they are snapped in.
  wr::LayoutVector2D CurrentAccumulatedScrollOffset();

  wr::WrSpatialId DefineScrollLayer(
      const ActiveScrolledRoot* aAsr,
      const layers::ScrollableLayerGuid::ViewID& aViewId,
      const Maybe<wr::WrSpatialId>& aParent, const wr::LayoutRect& aContentRect,
      const wr::LayoutRect& aClipRect, const wr::LayoutVector2D& aScrollOffset,
      wr::APZScrollGeneration aScrollOffsetGeneration,
      wr::HasScrollLinkedEffect aHasScrollLinkedEffect);

  void PushRect(const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
                bool aIsBackfaceVisible, bool aForceAntiAliasing,
                bool aIsCheckerboard, const wr::ColorF& aColor);
  void PushRectWithAnimation(const wr::LayoutRect& aBounds,
                             const wr::LayoutRect& aClip,
                             bool aIsBackfaceVisible, const wr::ColorF& aColor,
                             const WrAnimationProperty* aAnimation);
  // Fills an ellipse inscribed in aBounds.
  void PushEllipse(const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
                   bool aIsBackfaceVisible, const wr::ColorF& aColor);
  // Fills a rectangle with the given elliptical corner radii (the same for
  // all four corners). The radii must not exceed half the size of aBounds.
  void PushRoundedRect(const wr::LayoutRect& aBounds,
                       const wr::LayoutRect& aClip, bool aIsBackfaceVisible,
                       const wr::LayoutSize& aRadii, const wr::ColorF& aColor);
  void PushHitTest(const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
                   bool aIsBackfaceVisible,
                   const layers::ScrollableLayerGuid::ViewID& aScrollId,
                   const gfx::CompositorHitTestInfo& aHitInfo,
                   SideBits aSideBits);

  void PushBackdropFilter(const wr::LayoutRect& aBounds,
                          const wr::ComplexClipRegion& aRegion,
                          const nsTArray<wr::FilterOp>& aFilters,
                          const nsTArray<wr::WrFilterData>& aFilterDatas,
                          bool aIsBackfaceVisible);

  void PushLinearGradient(
      const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
      bool aIsBackfaceVisible, const wr::LayoutVector2D& aStartPoint,
      const wr::LayoutVector2D& aEndPoint,
      const nsTArray<wr::GradientStop>& aStops, wr::ExtendMode aExtendMode,
      const wr::LayoutSize aTileSize, const wr::LayoutSize aTileSpacing,
      bool aForceAntiAliasing = false,
      wr::AlphaMode aAlphaMode = wr::AlphaMode::Premultiplied);

  void PushRadialGradient(
      const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
      bool aIsBackfaceVisible, const wr::LayoutVector2D& aCenter,
      const wr::LayoutSize& aRadius, const nsTArray<wr::GradientStop>& aStops,
      wr::ExtendMode aExtendMode, const wr::LayoutSize aTileSize,
      const wr::LayoutSize aTileSpacing, bool aForceAntiAliasing = false,
      wr::AlphaMode aAlphaMode = wr::AlphaMode::Premultiplied);

  void PushConicGradient(
      const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
      bool aIsBackfaceVisible, const wr::LayoutVector2D& aCenter,
      const float aAngle, const nsTArray<wr::GradientStop>& aStops,
      wr::ExtendMode aExtendMode, const wr::LayoutSize aTileSize,
      const wr::LayoutSize aTileSpacing,
      wr::AlphaMode aAlphaMode = wr::AlphaMode::Premultiplied);

  void PushImage(const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
                 bool aIsBackfaceVisible, bool aForceAntiAliasing,
                 wr::ImageRendering aFilter, wr::ImageKey aImage,
                 bool aPremultipliedAlpha = true,
                 const wr::ColorF& aColor = wr::ColorF{1.0f, 1.0f, 1.0f, 1.0f},
                 bool aPreferCompositorSurface = false,
                 bool aSupportsExternalCompositing = false,
                 bool aRasterizedForRect = false);

  void PushRepeatingImage(
      const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
      bool aIsBackfaceVisible, const wr::LayoutSize& aStretchSize,
      const wr::LayoutSize& aTileSpacing, wr::ImageRendering aFilter,
      wr::ImageKey aImage, bool aPremultipliedAlpha = true,
      const wr::ColorF& aColor = wr::ColorF{1.0f, 1.0f, 1.0f, 1.0f});

  void PushYCbCrPlanarImage(
      const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
      bool aIsBackfaceVisible, wr::ImageKey aImageChannel0,
      wr::ImageKey aImageChannel1, wr::ImageKey aImageChannel2,
      wr::WrColorDepth aColorDepth, wr::WrYuvColorSpace aColorSpace,
      wr::WrColorRange aColorRange, wr::ImageRendering aFilter,
      bool aPreferCompositorSurface = false,
      bool aSupportsExternalCompositing = false);

  void PushNV12Image(const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
                     bool aIsBackfaceVisible, wr::ImageKey aImageChannel0,
                     wr::ImageKey aImageChannel1, wr::WrColorDepth aColorDepth,
                     wr::WrYuvColorSpace aColorSpace,
                     wr::WrColorRange aColorRange, wr::ImageRendering aFilter,
                     bool aPreferCompositorSurface = false,
                     bool aSupportsExternalCompositing = false);

  void PushP010Image(const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
                     bool aIsBackfaceVisible, wr::ImageKey aImageChannel0,
                     wr::ImageKey aImageChannel1, wr::WrColorDepth aColorDepth,
                     wr::WrYuvColorSpace aColorSpace,
                     wr::WrColorRange aColorRange, wr::ImageRendering aFilter,
                     bool aPreferCompositorSurface = false,
                     bool aSupportsExternalCompositing = false);

  void PushNV16Image(const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
                     bool aIsBackfaceVisible, wr::ImageKey aImageChannel0,
                     wr::ImageKey aImageChannel1, wr::WrColorDepth aColorDepth,
                     wr::WrYuvColorSpace aColorSpace,
                     wr::WrColorRange aColorRange, wr::ImageRendering aFilter,
                     bool aPreferCompositorSurface = false,
                     bool aSupportsExternalCompositing = false);

  void PushP210Image(const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
                     bool aIsBackfaceVisible, wr::ImageKey aImageChannel0,
                     wr::ImageKey aImageChannel1, wr::WrColorDepth aColorDepth,
                     wr::WrYuvColorSpace aColorSpace,
                     wr::WrColorRange aColorRange, wr::ImageRendering aFilter,
                     bool aPreferCompositorSurface = false,
                     bool aSupportsExternalCompositing = false);

  void PushYCbCrInterleavedImage(
      const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
      bool aIsBackfaceVisible, wr::ImageKey aImageChannel0,
      wr::WrColorDepth aColorDepth, wr::WrYuvColorSpace aColorSpace,
      wr::WrColorRange aColorRange, wr::ImageRendering aFilter,
      bool aPreferCompositorSurface = false,
      bool aSupportsExternalCompositing = false);

  void PushIFrame(const LayoutDeviceRect& aDevPxBounds, bool aIsBackfaceVisible,
                  wr::PipelineId aPipeline, bool aIgnoreMissingPipeline);

  // XXX WrBorderSides are passed with Range.
  // It is just to bypass compiler bug. See Bug 1357734.
  void PushBorder(
      const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
      bool aIsBackfaceVisible, const wr::LayoutSideOffsets& aWidths,
      const mozilla::Range<const wr::BorderSide>& aSides,
      const wr::BorderRadius& aRadius,
      const wr::LayoutSideOffsets& aInset = EmptyLayoutSideOffsets(),
      wr::AntialiasBorder = wr::AntialiasBorder::Yes,
      bool aForceAntiAliasing = false);

  void PushBorderImage(const wr::LayoutRect& aBounds,
                       const wr::LayoutRect& aClip, bool aIsBackfaceVisible,
                       const wr::WrBorderImage& aParams);

  void PushBorderGradient(const wr::LayoutRect& aBounds,
                          const wr::LayoutRect& aClip, bool aIsBackfaceVisible,
                          const wr::LayoutSideOffsets& aWidths,
                          const int32_t aWidth, const int32_t aHeight,
                          bool aFill, const wr::DeviceIntSideOffsets& aSlice,
                          const wr::LayoutVector2D& aStartPoint,
                          const wr::LayoutVector2D& aEndPoint,
                          const nsTArray<wr::GradientStop>& aStops,
                          wr::ExtendMode aExtendMode);

  void PushBorderRadialGradient(
      const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
      bool aIsBackfaceVisible, const wr::LayoutSideOffsets& aWidths, bool aFill,
      const wr::LayoutVector2D& aCenter, const wr::LayoutSize& aRadius,
      const nsTArray<wr::GradientStop>& aStops, wr::ExtendMode aExtendMode);

  void PushBorderConicGradient(
      const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
      bool aIsBackfaceVisible, const wr::LayoutSideOffsets& aWidths, bool aFill,
      const wr::LayoutVector2D& aCenter, const float aAngle,
      const nsTArray<wr::GradientStop>& aStops, wr::ExtendMode aExtendMode);

  void PushText(const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
                bool aIsBackfaceVisible, const wr::ColorF& aColor,
                wr::FontInstanceKey aFontKey,
                mozilla::Range<const wr::GlyphInstance> aGlyphBuffer,
                const wr::GlyphOptions* aGlyphOptions = nullptr);

  void PushLine(const wr::LayoutRect& aClip, bool aIsBackfaceVisible,
                const wr::Line& aLine);

  void PushShadow(const wr::LayoutRect& aBounds, const wr::LayoutRect& aClip,
                  bool aIsBackfaceVisible, const wr::Shadow& aShadow,
                  bool aShouldInflate);

  void PopAllShadows();

  void PushBoxShadow(const wr::LayoutRect& aRect, const wr::LayoutRect& aClip,
                     bool aIsBackfaceVisible, const wr::LayoutRect& aBoxBounds,
                     const wr::LayoutVector2D& aOffset,
                     const wr::ColorF& aColor, const float& aBlurRadius,
                     const float& aSpreadRadius,
                     const wr::BorderRadius& aBorderRadius,
                     const wr::BorderRadius& aShadowRadius,
                     const wr::BoxShadowClipMode& aClipMode);

  void PushDebug(uint32_t aVal);

  uint64_t CurrentClipChainId() const {
    return mCurrentSpaceAndClipChain.clip_chain;
  }

  Maybe<wr::WrClipChainId> CurrentClipChainIdIfNotRoot() const {
    return mCurrentSpaceAndClipChain.clip_chain != wr::ROOT_CLIP_CHAIN
               ? Some(mCurrentSpaceAndClipChain.clip_chain)
               : Nothing();
  }

  const wr::WrSpaceAndClipChain& CurrentSpaceAndClipChain() const {
    return mCurrentSpaceAndClipChain;
  }

  const wr::PipelineId& CurrentPipelineId() const { return mPipelineId; }
  layers::WebRenderBackend GetBackendType() const { return mBackend; }

  // Checks to see if the innermost enclosing fixed pos item has the same
  // ASR. If so, it returns the scroll target for that fixed-pos item.
  // Otherwise, it returns Nothing().
  Maybe<layers::ScrollableLayerGuid::ViewID> GetContainingFixedPosScrollTarget(
      const ActiveScrolledRoot* aAsr);

  Maybe<SideBits> GetContainingFixedPosSideBits(const ActiveScrolledRoot* aAsr);

  gfxContext* GetTextContext(wr::IpcResourceUpdateQueue& aResources,
                             const layers::StackingContextHelper& aSc,
                             layers::RenderRootStateManager* aManager,
                             nsDisplayItem* aItem, nsRect& aBounds,
                             const gfx::Point& aDeviceOffset);

  // Try to avoid using this when possible.
  wr::WrState* Raw() { return mWrState; }

  // Used for opacity flattening. When we flatten away an opacity item,
  // we push the opacity value onto the builder.
  // Descendant items should pull the inherited opacity during
  // their CreateWebRenderCommands implementation. This can only happen if all
  // descendant items reported supporting this functionality, via
  // nsDisplayItem::CanApplyOpacity.
  float GetInheritedOpacity() { return mInheritedOpacity; }
  void SetInheritedOpacity(float aOpacity) { mInheritedOpacity = aOpacity; }
  const DisplayItemClipChain* GetInheritedClipChain() {
    return mInheritedClipChain;
  }
  void PushInheritedClipChain(nsDisplayListBuilder* aBuilder,
                              const DisplayItemClipChain* aClipChain);
  void SetInheritedClipChain(const DisplayItemClipChain* aClipChain) {
    mInheritedClipChain = aClipChain;
  }

  // A chain of RAII objects, each holding a (ASR, ViewID, SideBits) tuple of
  // data. The topmost object is pointed to by the mActiveFixedPosTracker
  // pointer in the wr::DisplayListBuilder.
  class MOZ_RAII FixedPosScrollTargetTracker final {
   public:
    FixedPosScrollTargetTracker(DisplayListBuilder& aBuilder,
                                const ActiveScrolledRoot* aAsr,
                                layers::ScrollableLayerGuid::ViewID aScrollId,
                                SideBits aSideBits);
    ~FixedPosScrollTargetTracker();
    Maybe<layers::ScrollableLayerGuid::ViewID> GetScrollTargetForASR(
        const ActiveScrolledRoot* aAsr);
    Maybe<SideBits> GetSideBitsForASR(const ActiveScrolledRoot* aAsr);

   private:
    FixedPosScrollTargetTracker* mParentTracker;
    DisplayListBuilder& mBuilder;
    const ActiveScrolledRoot* mAsr;
    layers::ScrollableLayerGuid::ViewID mScrollId;
    SideBits mSideBits;
  };

 protected:
  wr::WrState* mWrState;

  // Track spatial ids that we've created corresponding to ActiveScrolledRoot
  // objects. Used for both scroll and sticky ASRs
  std::unordered_map<const ActiveScrolledRoot*, wr::WrSpatialId>
      mASRToSpatialIdMap;

  wr::WrSpaceAndClipChain mCurrentSpaceAndClipChain;

  RefPtr<layout::TextDrawTarget> mCachedTextDT;
  mozilla::UniquePtr<gfxContext> mCachedContext;

  FixedPosScrollTargetTracker* mActiveFixedPosTracker;

  wr::PipelineId mPipelineId;
  layers::WebRenderBackend mBackend;

  float mInheritedOpacity = 1.0f;
  const DisplayItemClipChain* mInheritedClipChain = nullptr;

  friend class WebRenderAPI;
  friend class SpaceAndClipChainHelper;
};

// This is a RAII class that overrides the current Wr's SpatialId and
// ClipChainId.
class MOZ_RAII SpaceAndClipChainHelper final {
 public:
  SpaceAndClipChainHelper(DisplayListBuilder& aBuilder,
                          wr::WrSpaceAndClipChain aSpaceAndClipChain)
      : mBuilder(aBuilder),
        mOldSpaceAndClipChain(aBuilder.mCurrentSpaceAndClipChain) {
    aBuilder.mCurrentSpaceAndClipChain = aSpaceAndClipChain;
  }
  SpaceAndClipChainHelper(DisplayListBuilder& aBuilder,
                          wr::WrSpatialId aSpatialId)
      : mBuilder(aBuilder),
        mOldSpaceAndClipChain(aBuilder.mCurrentSpaceAndClipChain) {
    aBuilder.mCurrentSpaceAndClipChain.space = aSpatialId;
  }
  SpaceAndClipChainHelper(DisplayListBuilder& aBuilder,
                          wr::WrClipChainId aClipChainId)
      : mBuilder(aBuilder),
        mOldSpaceAndClipChain(aBuilder.mCurrentSpaceAndClipChain) {
    aBuilder.mCurrentSpaceAndClipChain.clip_chain = aClipChainId.id;
  }

  ~SpaceAndClipChainHelper() {
    mBuilder.mCurrentSpaceAndClipChain = mOldSpaceAndClipChain;
  }

  SpaceAndClipChainHelper(const SpaceAndClipChainHelper&) = delete;

 private:
  DisplayListBuilder& mBuilder;
  wr::WrSpaceAndClipChain mOldSpaceAndClipChain;
};

Maybe<wr::ImageFormat> SurfaceFormatToImageFormat(gfx::SurfaceFormat aFormat);

}  // namespace wr
}  // namespace mozilla

#endif
