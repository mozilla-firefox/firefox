/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_WEBRENDERCOMMANDBUILDER_H
#define GFX_WEBRENDERCOMMANDBUILDER_H

#include "ImgDrawResult.h"
#include "mozilla/EnumeratedArray.h"
#include "mozilla/SVGIntegrationUtils.h"  // for WrFiltersHolder
#include "mozilla/layers/ClipManager.h"
#include "mozilla/layers/HitTestInfoManager.h"
#include "mozilla/layers/WebRenderMessages.h"
#include "mozilla/layers/WebRenderScrollData.h"
#include "mozilla/layers/WebRenderUserData.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsDisplayList.h"
#include "nsIFrame.h"
#include "nsTHashSet.h"

namespace mozilla {

namespace image {
class WebRenderImageProvider;
}

namespace layers {

class ImageClient;
class ImageContainer;
class WebRenderBridgeChild;
class WebRenderCanvasData;
class WebRenderCanvasRendererAsync;
class WebRenderImageData;
class WebRenderFallbackData;
class WebRenderParentCommand;
class WebRenderUserData;

class WebRenderCommandBuilder final {
  typedef nsTHashSet<RefPtr<WebRenderUserData>> WebRenderUserDataRefTable;
  typedef nsTHashSet<RefPtr<WebRenderCanvasData>> CanvasDataSet;

 public:
  explicit WebRenderCommandBuilder(WebRenderLayerManager* aManager);

  void Destroy();

  void EmptyTransaction();

  bool NeedsEmptyTransaction();

  void BuildWebRenderCommands(wr::DisplayListBuilder& aBuilder,
                              wr::IpcResourceUpdateQueue& aResourceUpdates,
                              nsDisplayList* aDisplayList,
                              nsDisplayListBuilder* aDisplayListBuilder,
                              WebRenderScrollData& aScrollData,
                              WrFiltersHolder&& aFilters);

  void PushOverrideForASR(const ActiveScrolledRoot* aASR,
                          const wr::WrSpatialId& aSpatialId);
  void PopOverrideForASR(const ActiveScrolledRoot* aASR);

  // The reference frames a frame can create, which together with the frame
  // identify a reference frame.
  enum class FrameReferenceFrame : uint8_t {
    Transform,
    TransformSeparator,
    SVGOrigin,
    Perspective,
    OwnLayer,
    ViewTransitionCapture,
  };

  // Defines reference frame aKind of aFrame in the current space. Every
  // reference frame created for a frame goes through here.
  wr::WrSpatialId DefineReferenceFrame(wr::DisplayListBuilder& aBuilder,
                                       nsIFrame* aFrame,
                                       FrameReferenceFrame aKind,
                                       const LayoutDevicePoint& aOrigin,
                                       const wr::ReferenceFrameParams& aParams);

  Maybe<wr::ImageKey> CreateImageKey(
      nsDisplayItem* aItem, ImageContainer* aContainer,
      mozilla::wr::DisplayListBuilder& aBuilder,
      mozilla::wr::IpcResourceUpdateQueue& aResources,
      mozilla::wr::ImageRendering aRendering, const StackingContextHelper& aSc,
      gfx::IntSize& aSize, const Maybe<LayoutDeviceRect>& aAsyncImageBounds);

  Maybe<wr::ImageKey> CreateImageProviderKey(
      nsDisplayItem* aItem, image::WebRenderImageProvider* aProvider,
      image::ImgDrawResult aDrawResult,
      mozilla::wr::IpcResourceUpdateQueue& aResources);

  WebRenderUserDataRefTable* GetWebRenderUserDataTable() {
    return &mWebRenderUserDatas;
  }

  bool PushImage(nsDisplayItem* aItem, ImageContainer* aContainer,
                 mozilla::wr::DisplayListBuilder& aBuilder,
                 mozilla::wr::IpcResourceUpdateQueue& aResources,
                 const StackingContextHelper& aSc,
                 const LayoutDeviceRect& aRect, const LayoutDeviceRect& aClip);

  // aOpacity is applied through the image's color multiplier.
  bool PushImageProvider(nsDisplayItem* aItem,
                         image::WebRenderImageProvider* aProvider,
                         image::ImgDrawResult aDrawResult,
                         mozilla::wr::DisplayListBuilder& aBuilder,
                         mozilla::wr::IpcResourceUpdateQueue& aResources,
                         const LayoutDeviceRect& aRect,
                         const LayoutDeviceRect& aClip,
                         bool aRasterizedForRect = false,
                         float aOpacity = 1.0f);

  Maybe<wr::ImageMask> BuildWrMaskImage(
      nsDisplayMasksAndClipPaths* aMaskItem, wr::DisplayListBuilder& aBuilder,
      wr::IpcResourceUpdateQueue& aResources, const StackingContextHelper& aSc,
      nsDisplayListBuilder* aDisplayListBuilder,
      const LayoutDeviceRect& aBounds);

  bool PushItemAsImage(nsDisplayItem* aItem, wr::DisplayListBuilder& aBuilder,
                       wr::IpcResourceUpdateQueue& aResources,
                       const StackingContextHelper& aSc,
                       nsDisplayListBuilder* aDisplayListBuilder);

  void CreateWebRenderCommandsFromDisplayList(
      nsDisplayList* aDisplayList, nsDisplayItem* aWrappingItem,
      nsDisplayListBuilder* aDisplayListBuilder,
      const StackingContextHelper& aSc, wr::DisplayListBuilder& aBuilder,
      wr::IpcResourceUpdateQueue& aResources, bool aNewClipList = true);

  // aWrappingItem has to be non-null.
  void DoGroupingForDisplayList(nsDisplayList* aDisplayList,
                                nsDisplayItem* aWrappingItem,
                                nsDisplayListBuilder* aDisplayListBuilder,
                                const StackingContextHelper& aSc,
                                wr::DisplayListBuilder& aBuilder,
                                wr::IpcResourceUpdateQueue& aResources);

  already_AddRefed<WebRenderFallbackData> GenerateFallbackData(
      nsDisplayItem* aItem, wr::DisplayListBuilder& aBuilder,
      wr::IpcResourceUpdateQueue& aResources, const StackingContextHelper& aSc,
      nsDisplayListBuilder* aDisplayListBuilder, LayoutDeviceRect& aImageRect);

  void RemoveUnusedAndResetWebRenderUserData();
  void ClearCachedResources();

  bool ShouldDumpDisplayList(nsDisplayListBuilder* aBuilder);
  wr::usize GetBuilderDumpIndex() const { return mBuilderDumpIndex; }

  bool GetContainsSVGGroup() { return mContainsSVGGroup; }

  // Those are data that we kept between transactions. We used to cache some
  // data in the layer. But in layers free mode, we don't have layer which
  // means we need some other place to cached the data between transaction.
  // We store the data in frame's property.
  template <class T>
  already_AddRefed<T> CreateOrRecycleWebRenderUserData(
      nsDisplayItem* aItem, bool* aOutIsRecycled = nullptr) {
    MOZ_ASSERT(aItem);
    return CreateOrRecycleWebRenderUserData<T>(aItem->GetPerFrameKey(),
                                               aItem->Frame());
  }
  template <class T>
  already_AddRefed<T> CreateOrRecycleWebRenderUserData(
      uint32_t aDisplayItemKey, nsIFrame* aFrame,
      bool* aOutIsRecycled = nullptr) {
    if (aOutIsRecycled) {
      *aOutIsRecycled = true;
    }

    WebRenderUserDataTable* userDataTable =
        aFrame->GetProperty(WebRenderUserDataProperty::Key());

    if (!userDataTable) {
      userDataTable = new WebRenderUserDataTable();
      aFrame->AddProperty(WebRenderUserDataProperty::Key(), userDataTable);
    }

    RefPtr<WebRenderUserData>& data = userDataTable->LookupOrInsertWith(
        WebRenderUserDataKey(aDisplayItemKey, T::Type()), [&] {
          auto data = MakeRefPtr<T>(GetRenderRootStateManager(),
                                    aDisplayItemKey, aFrame);
          mWebRenderUserDatas.Insert(data);
          if (aOutIsRecycled) {
            *aOutIsRecycled = false;
          }
          return data;
        });

    MOZ_ASSERT(data);
    MOZ_ASSERT(data->GetType() == T::Type());

    // Mark the data as being used. We will remove unused user data in the end
    // of EndTransaction.
    data->SetUsed(true);

    switch (T::Type()) {
      case WebRenderUserData::UserDataType::eCanvas:
        mLastCanvasDatas.Insert(data->AsCanvasData());
        break;
      default:
        break;
    }

    RefPtr<T> res = static_cast<T*>(data.get());
    return res.forget();
  }
  template <class T>
  already_AddRefed<T> GetWebRenderUserData(nsDisplayItem* aItem) {
    MOZ_ASSERT(aItem);
    return GetWebRenderUserData<T>(aItem->GetPerFrameKey(), aItem->Frame());
  }

  template <class T>
  already_AddRefed<T> GetWebRenderUserData(uint32_t aDisplayItemKey,
                                           nsIFrame* aFrame) {
    WebRenderUserDataTable* userDataTable =
        aFrame->GetProperty(WebRenderUserDataProperty::Key());

    if (!userDataTable) {
      return nullptr;
    }

    RefPtr<WebRenderUserData> data =
        userDataTable->Get(WebRenderUserDataKey(aDisplayItemKey, T::Type()));

    if (!data) {
      return nullptr;
    }

    MOZ_ASSERT(data->GetType() == T::Type());

    RefPtr<T> res = static_cast<T*>(data.get());
    return res.forget();
  }

  WebRenderLayerManager* const mManager;

 private:
  RenderRootStateManager* GetRenderRootStateManager();
  void ReportBlobStats();
  void CreateWebRenderCommands(nsDisplayItem* aItem,
                               mozilla::wr::DisplayListBuilder& aBuilder,
                               mozilla::wr::IpcResourceUpdateQueue& aResources,
                               const StackingContextHelper& aSc,
                               nsDisplayListBuilder* aDisplayListBuilder);

  bool ComputeInvalidationForDisplayItem(nsDisplayListBuilder* aBuilder,
                                         const nsPoint& aShift,
                                         nsDisplayItem* aItem);
  bool ComputeInvalidationForDisplayList(nsDisplayListBuilder* aBuilder,
                                         const nsPoint& aShift,
                                         nsDisplayList* aList);

  ClipManager mClipManager;
  HitTestInfoManager mHitTestInfoManager;

  // We use this as a temporary data structure while building the mScrollData
  // inside a layers-free transaction.
  std::vector<WebRenderLayerScrollData> mLayerScrollData;
  // We use this as a temporary data structure to track the current display
  // item's ASR as we recurse in CreateWebRenderCommandsFromDisplayList. We
  // need this so that WebRenderLayerScrollData items that deeper in the
  // tree don't duplicate scroll metadata that their ancestors already have.
  std::vector<const ActiveScrolledRoot*> mAsrStack;
  const ActiveScrolledRoot* mLastAsr;

  WebRenderUserDataRefTable mWebRenderUserDatas;

  // Store of WebRenderCanvasData objects for use in empty transactions
  CanvasDataSet mLastCanvasDatas;

  wr::usize mBuilderDumpIndex;
  wr::usize mDumpIndent;

  bool mApzEnabled;

  bool mComputingOpaqueRegion;

  // The wrapping items we compute opaque regions against. The point is the
  // offset from the item to the global coordinate space (since we only support
  // translations).
  AutoTArray<std::pair<nsDisplayItem*, nsPoint>, 3> mOpaqueRegionWrappers;
  struct AutoOpaqueRegionStateTracker;

 public:
  // Whether consecutive inactive display items should be grouped into one
  // blob image.
  bool mDoGrouping;

  // True if the most recently build display list contained an svg that
  // we did grouping for.
  bool mContainsSVGGroup;

  // Per display list build counters describing how much content went through
  // blob images. Reset in BuildWebRenderCommands and reported as a profiler
  // marker at the end of the build.
  struct BlobStats {
    // Blob images produced by grouping consecutive inactive SVG items.
    uint32_t mGroupBlobs = 0;
    // Group blobs that had to be re-recorded (as opposed to reused as is).
    uint32_t mGroupBlobsPainted = 0;
    // Blob images produced by the per item fallback path.
    uint32_t mFallbackBlobs = 0;
    // Sum of the visible pixel area of all blob images.
    uint64_t mBlobArea = 0;
    // Items painted into group blobs, by display item type.
    EnumeratedArray<DisplayItemType, uint32_t,
                    size_t(DisplayItemType::TYPE_MAX)>
        mGroupedItems;
    // Group splits caused by an active item, by the type of that item.
    EnumeratedArray<DisplayItemType, uint32_t,
                    size_t(DisplayItemType::TYPE_MAX)>
        mSplits;

    void Reset() { *this = BlobStats(); }
  };
  BlobStats mBlobStats;
};

}  // namespace layers
}  // namespace mozilla

#endif /* GFX_WEBRENDERCOMMANDBUILDER_H */
