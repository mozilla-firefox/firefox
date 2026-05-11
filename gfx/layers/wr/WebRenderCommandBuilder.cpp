/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebRenderCommandBuilder.h"

#include "mozilla/AutoRestore.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/EffectCompositor.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/SVGGeometryFrame.h"
#include "mozilla/SVGImageFrame.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/image/WebRenderImageProvider.h"
#include "mozilla/layers/AnimationHelper.h"
#include "mozilla/layers/ClipManager.h"
#include "mozilla/layers/ImageClient.h"
#include "mozilla/layers/RenderRootStateManager.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/layers/IpcResourceUpdateQueue.h"
#include "mozilla/layers/SharedSurfacesChild.h"
#include "mozilla/layers/SourceSurfaceSharedData.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "mozilla/layers/WebRenderDrawEventRecorder.h"
#include "UnitTransforms.h"
#include "gfxEnv.h"
#include "MediaInfo.h"
#include "nsDisplayListInvalidation.h"
#include "nsLayoutUtils.h"
#include "nsTHashSet.h"
#include "WebRenderCanvasRenderer.h"

#include <cstdint>

namespace mozilla::layers {

using namespace gfx;
using namespace image;
static int sIndent;
#include <stdarg.h>
#include <stdio.h>

static void GP(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
#if 0
    for (int i = 0; i < sIndent; i++) { printf(" "); }
    vprintf(fmt, args);
#endif
  va_end(args);
}

bool FitsInt32(const float aVal) {
  // Although int32_t min and max can't be represented exactly with floats, the
  // cast truncates towards zero which is what we want here.
  const float min = static_cast<float>(std::numeric_limits<int32_t>::min());
  const float max = static_cast<float>(std::numeric_limits<int32_t>::max());
  return aVal > min && aVal < max;
}

// XXX: problems:
// - How do we deal with scrolling while having only a single invalidation rect?
// We can have a valid rect and an invalid rect. As we scroll the valid rect
// will move and the invalid rect will be the new area

struct BlobItemData;
static void DestroyBlobGroupDataProperty(nsTArray<BlobItemData*>* aArray);
NS_DECLARE_FRAME_PROPERTY_WITH_DTOR(BlobGroupDataProperty,
                                    nsTArray<BlobItemData*>,
                                    DestroyBlobGroupDataProperty);

// These are currently manually allocated and ownership is help by the
// mDisplayItems hash table in DIGroup
struct BlobItemData {
  // a weak pointer to the frame for this item.
  // DisplayItemData has a mFrameList to deal with merged frames. Hopefully we
  // don't need to worry about that.
  nsIFrame* mFrame;

  uint32_t mDisplayItemKey;
  nsTArray<BlobItemData*>*
      mArray;  // a weak pointer to the array that's owned by the frame property

  LayerIntRect mRect;
  // It would be nice to not need this. We need to be able to call
  // ComputeInvalidationRegion. ComputeInvalidationRegion will sometimes reach
  // into parent style structs to get information that can change the
  // invalidation region
  UniquePtr<nsDisplayItemGeometry> mGeometry;
  DisplayItemClip mClip;
  bool mInvisible;
  bool mUsed;  // initialized near construction
  // XXX: only used for debugging
  bool mInvalid;

  // a weak pointer to the group that owns this item
  // we use this to track whether group for a particular item has changed
  struct DIGroup* mGroup;

  // We need to keep a list of all the external surfaces used by the blob image.
  // We do this on a per-display item basis so that the lists remains correct
  // during invalidations.
  DrawEventRecorderPrivate::ExternalSurfacesHolder mExternalSurfaces;

  BlobItemData(DIGroup* aGroup, nsDisplayItem* aItem)
      : mInvisible(false), mUsed(false), mGroup(aGroup) {
    mInvalid = false;
    mDisplayItemKey = aItem->GetPerFrameKey();
    AddFrame(aItem->Frame());
  }

 private:
  void AddFrame(nsIFrame* aFrame) {
    mFrame = aFrame;

    nsTArray<BlobItemData*>* array =
        aFrame->GetProperty(BlobGroupDataProperty());
    if (!array) {
      array = new nsTArray<BlobItemData*>();
      aFrame->SetProperty(BlobGroupDataProperty(), array);
    }
    array->AppendElement(this);
    mArray = array;
  }

 public:
  void ClearFrame() {
    // Delete the weak pointer to this BlobItemData on the frame
    MOZ_RELEASE_ASSERT(mFrame);
    // the property may already be removed if WebRenderUserData got deleted
    // first so we use our own mArray pointer.
    mArray->RemoveElement(this);

    // drop the entire property if nothing's left in the array
    if (mArray->IsEmpty()) {
      // If the frame is in the process of being destroyed this will fail
      // but that's ok, because the the property will be removed then anyways
      mFrame->RemoveProperty(BlobGroupDataProperty());
    }
    mFrame = nullptr;
  }

  ~BlobItemData() {
    if (mFrame) {
      ClearFrame();
    }
  }
};

static BlobItemData* GetBlobItemData(nsDisplayItem* aItem) {
  nsIFrame* frame = aItem->Frame();
  uint32_t key = aItem->GetPerFrameKey();
  const nsTArray<BlobItemData*>* array =
      frame->GetProperty(BlobGroupDataProperty());
  if (array) {
    for (BlobItemData* item : *array) {
      if (item->mDisplayItemKey == key) {
        return item;
      }
    }
  }
  return nullptr;
}

// We keep around the BlobItemData so that when we invalidate it get properly
// included in the rect
static void DestroyBlobGroupDataProperty(nsTArray<BlobItemData*>* aArray) {
  for (BlobItemData* item : *aArray) {
    GP("DestroyBlobGroupDataProperty: %p-%d\n", item->mFrame,
       item->mDisplayItemKey);
    item->mFrame = nullptr;
  }
  delete aArray;
}

static void TakeExternalSurfaces(
    WebRenderDrawEventRecorder* aRecorder,
    DrawEventRecorderPrivate::ExternalSurfacesHolder& aExternalSurfaces,
    RenderRootStateManager* aManager, wr::IpcResourceUpdateQueue& aResources) {
  aRecorder->TakeExternalSurfaces(aExternalSurfaces);

  for (auto& entry : aExternalSurfaces) {
    // While we don't use the image key with the surface, because the blob image
    // renderer doesn't have easy access to the resource set, we still want to
    // ensure one is generated. That will ensure the surface remains alive until
    // at least the last epoch which the blob image could be used in.
    wr::ImageKey key;
    DebugOnly<nsresult> rv =
        SharedSurfacesChild::Share(entry.mSurface, aManager, aResources, key);
    MOZ_ASSERT(rv.value != NS_ERROR_NOT_IMPLEMENTED);
  }
}

struct DIGroup;
struct Grouper {
  explicit Grouper(ClipManager& aClipManager)
      : mAppUnitsPerDevPixel(0),
        mDisplayListBuilder(nullptr),
        mClipManager(aClipManager) {}

  int32_t mAppUnitsPerDevPixel;
  nsDisplayListBuilder* mDisplayListBuilder;
  ClipManager& mClipManager;
  HitTestInfoManager mHitTestInfoManager;
  Matrix mTransform;

  // Paint the list of aChildren display items.
  void PaintContainerItem(DIGroup* aGroup, nsDisplayItem* aItem,
                          BlobItemData* aData, const IntRect& aItemBounds,
                          bool aDirty, nsDisplayList* aChildren,
                          gfxContext* aContext,
                          WebRenderDrawEventRecorder* aRecorder,
                          RenderRootStateManager* aRootManager,
                          wr::IpcResourceUpdateQueue& aResources);

  // Builds groups of display items split based on 'layer activity'
  void ConstructGroups(nsDisplayListBuilder* aDisplayListBuilder,
                       WebRenderCommandBuilder* aCommandBuilder,
                       wr::DisplayListBuilder& aBuilder,
                       wr::IpcResourceUpdateQueue& aResources, DIGroup* aGroup,
                       nsDisplayList* aList, nsDisplayItem* aWrappingItem,
                       const StackingContextHelper& aSc);
  // Builds a group of display items without promoting anything to active.
  bool ConstructGroupInsideInactive(WebRenderCommandBuilder* aCommandBuilder,
                                    wr::DisplayListBuilder& aBuilder,
                                    wr::IpcResourceUpdateQueue& aResources,
                                    DIGroup* aGroup, nsDisplayList* aList,
                                    const StackingContextHelper& aSc);
  // Helper method for processing a single inactive item
  bool ConstructItemInsideInactive(WebRenderCommandBuilder* aCommandBuilder,
                                   wr::DisplayListBuilder& aBuilder,
                                   wr::IpcResourceUpdateQueue& aResources,
                                   DIGroup* aGroup, nsDisplayItem* aItem,
                                   const StackingContextHelper& aSc,
                                   bool* aOutIsInvisible);
  ~Grouper() = default;
};

// Returns whether this is an item for which complete invalidation was
// reliant on LayerTreeInvalidation in the pre-webrender world.
static bool IsContainerLayerItem(nsDisplayItem* aItem) {
  switch (aItem->GetType()) {
    case DisplayItemType::TYPE_WRAP_LIST:
    case DisplayItemType::TYPE_CONTAINER:
    case DisplayItemType::TYPE_TRANSFORM:
    case DisplayItemType::TYPE_OPACITY:
    case DisplayItemType::TYPE_FILTER:
    case DisplayItemType::TYPE_BLEND_CONTAINER:
    case DisplayItemType::TYPE_BLEND_MODE:
    case DisplayItemType::TYPE_MASK:
    case DisplayItemType::TYPE_PERSPECTIVE: {
      return true;
    }
    default: {
      return false;
    }
  }
}

#include <sstream>

static bool DetectContainerLayerPropertiesBoundsChange(
    nsDisplayItem* aItem, BlobItemData* aData,
    nsDisplayItemGeometry& aGeometry) {
  if (aItem->GetType() == DisplayItemType::TYPE_FILTER) {
    // Filters get clipped to the BuildingRect since they can
    // have huge bounds outside of the visible area.
    // This function and similar code in ComputeGeometryChange should be kept in
    // sync.
    aGeometry.mBounds = aGeometry.mBounds.Intersect(aItem->GetBuildingRect());
  }

  return !aGeometry.mBounds.IsEqualEdges(aData->mGeometry->mBounds);
}

/* A Display Item Group. This represents a set of diplay items that
 * have been grouped together for rasterization and can be partially
 * invalidated. It also tracks a number of properties from the environment
 * that when changed would cause us to repaint like mScale. */
struct DIGroup {
  // XXX: Storing owning pointers to the BlobItemData in a hash table is not
  // a good choice. There are two better options:
  //
  // 1. We should just be using a linked list for this stuff.
  //    That we can iterate over only the used items.
  //    We remove from the unused list and add to the used list
  //    when we see an item.
  //
  //    we allocate using a free list.
  //
  // 2. We can use a Vec and use SwapRemove().
  //    We'll just need to be careful when iterating.
  //    The advantage of a Vec is that everything stays compact
  //    and we don't need to heap allocate the BlobItemData's
  nsTHashSet<BlobItemData*> mDisplayItems;

  LayerIntRect mInvalidRect;
  LayerIntRect mVisibleRect;
  // This is the last visible rect sent to WebRender. It's used
  // to compute the invalid rect and ensure that we send
  // the appropriate data to WebRender for merging.
  LayerIntRect mLastVisibleRect;

  // This is the intersection of mVisibleRect and mLastVisibleRect
  LayerIntRect mPreservedRect;
  // mHitTestBounds is the same as mActualBounds except for the bounds
  // of invisible items which are accounted for in the former but not
  // in the latter.
  LayerIntRect mHitTestBounds;
  LayerIntRect mActualBounds;
  int32_t mAppUnitsPerDevPixel;
  gfx::MatrixScales mScale;
  ScrollableLayerGuid::ViewID mScrollId;
  CompositorHitTestInfo mHitInfo;
  LayerPoint mResidualOffset;
  LayerIntRect mLayerBounds;  // mGroupBounds converted to Layer space
  // mLayerBounds clipped to the container/parent of the
  // current item being processed.
  LayerIntRect mClippedImageBounds;  // mLayerBounds with the clipping of any
                                     // containers applied
  Maybe<wr::BlobImageKey> mKey;
  std::vector<RefPtr<ScaledFont>> mFonts;

  DIGroup()
      : mAppUnitsPerDevPixel(0),
        mScrollId(ScrollableLayerGuid::NULL_SCROLL_ID),
        mHitInfo(CompositorHitTestInvisibleToHit) {}

  void InvalidateRect(const LayerIntRect& aRect) {
    mInvalidRect = mInvalidRect.Union(aRect);
  }

  LayerIntRect ItemBounds(nsDisplayItem* aItem) {
    BlobItemData* data = GetBlobItemData(aItem);
    return data->mRect;
  }

  void ClearItems() {
    GP("items: %d\n", mDisplayItems.Count());
    for (BlobItemData* data : mDisplayItems) {
      GP("Deleting %p-%d\n", data->mFrame, data->mDisplayItemKey);
      delete data;
    }
    mDisplayItems.Clear();
  }

  void ClearImageKey(RenderRootStateManager* aManager, bool aForce = false) {
    if (mKey) {
      MOZ_RELEASE_ASSERT(aForce || mInvalidRect.IsEmpty());
      aManager->AddBlobImageKeyForDiscard(*mKey);
      mKey = Nothing();
    }
    mFonts.clear();
  }

  static LayerIntRect ToDeviceSpace(const nsRect& aBounds, Matrix& aMatrix,
                                    int32_t aAppUnitsPerDevPixel) {
    // RoundedOut can convert empty rectangles to non-empty ones
    // so special case them here
    if (aBounds.IsEmpty()) {
      return LayerIntRect();
    }
    return LayerIntRect::FromUnknownRect(RoundedOut(aMatrix.TransformBounds(
        ToRect(nsLayoutUtils::RectToGfxRect(aBounds, aAppUnitsPerDevPixel)))));
  }

  bool ComputeGeometryChange(nsDisplayItem* aItem, BlobItemData* aData,
                             Matrix& aMatrix, nsDisplayListBuilder* aBuilder) {
    // If the frame is marked as invalidated, and didn't specify a rect to
    // invalidate then we want to invalidate both the old and new bounds,
    // otherwise we only want to invalidate the changed areas. If we do get an
    // invalid rect, then we want to add this on top of the change areas.
    nsRect invalid;
    bool invalidated = false;
    const DisplayItemClip& clip = aItem->GetClip();

    int32_t appUnitsPerDevPixel =
        aItem->Frame()->PresContext()->AppUnitsPerDevPixel();
    MOZ_RELEASE_ASSERT(mAppUnitsPerDevPixel == appUnitsPerDevPixel);
    GP("\n");
    GP("clippedImageRect %d %d %d %d\n", mClippedImageBounds.x,
       mClippedImageBounds.y, mClippedImageBounds.width,
       mClippedImageBounds.height);
    LayerIntSize size = mVisibleRect.Size();
    GP("imageSize: %d %d\n", size.width, size.height);

    GP("pre mInvalidRect: %s %p-%d - inv: %d %d %d %d\n", aItem->Name(),
       aItem->Frame(), aItem->GetPerFrameKey(), mInvalidRect.x, mInvalidRect.y,
       mInvalidRect.width, mInvalidRect.height);
    if (!aData->mGeometry) {
      // This item is being added for the first time, invalidate its entire
      // area.
      UniquePtr<nsDisplayItemGeometry> geometry(
          aItem->AllocateGeometry(aBuilder));
      nsRect clippedBounds = clip.ApplyNonRoundedIntersection(
          geometry->ComputeInvalidationRegion());
      aData->mGeometry = std::move(geometry);

      LayerIntRect transformedRect =
          ToDeviceSpace(clippedBounds, aMatrix, appUnitsPerDevPixel);
      aData->mRect = transformedRect.Intersect(mClippedImageBounds);
      GP("CGC %s %d %d %d %d\n", aItem->Name(), clippedBounds.x,
         clippedBounds.y, clippedBounds.width, clippedBounds.height);
      GP("%d %d,  %f %f\n", mVisibleRect.TopLeft().x.value,
         mVisibleRect.TopLeft().y.value, aMatrix._11, aMatrix._22);
      GP("mRect %d %d %d %d\n", aData->mRect.x, aData->mRect.y,
         aData->mRect.width, aData->mRect.height);
      InvalidateRect(aData->mRect);
      aData->mInvalid = true;
      invalidated = true;
    } else if (aItem->IsInvalid(invalid) && invalid.IsEmpty()) {
      UniquePtr<nsDisplayItemGeometry> geometry(
          aItem->AllocateGeometry(aBuilder));
      nsRect clippedBounds = clip.ApplyNonRoundedIntersection(
          geometry->ComputeInvalidationRegion());
      aData->mGeometry = std::move(geometry);

      GP("matrix: %f %f\n", aMatrix._31, aMatrix._32);
      GP("frame invalid invalidate: %s\n", aItem->Name());
      GP("old rect: %d %d %d %d\n", aData->mRect.x, aData->mRect.y,
         aData->mRect.width, aData->mRect.height);
      InvalidateRect(aData->mRect);
      // We want to snap to outside pixels. When should we multiply by the
      // matrix?
      // XXX: TransformBounds is expensive. We should avoid doing it if we have
      // no transform
      LayerIntRect transformedRect =
          ToDeviceSpace(clippedBounds, aMatrix, appUnitsPerDevPixel);
      aData->mRect = transformedRect.Intersect(mClippedImageBounds);
      InvalidateRect(aData->mRect);
      GP("new rect: %d %d %d %d\n", aData->mRect.x, aData->mRect.y,
         aData->mRect.width, aData->mRect.height);
      aData->mInvalid = true;
      invalidated = true;
    } else {
      GP("else invalidate: %s\n", aItem->Name());
      nsRegion combined;
      // this includes situations like reflow changing the position
      aItem->ComputeInvalidationRegion(aBuilder, aData->mGeometry.get(),
                                       &combined);
      if (!combined.IsEmpty()) {
        // There might be no point in doing this elaborate tracking here to get
        // smaller areas
        InvalidateRect(aData->mRect);  // invalidate the old area -- in theory
                                       // combined should take care of this
        UniquePtr<nsDisplayItemGeometry> geometry(
            aItem->AllocateGeometry(aBuilder));
        // invalidate the invalidated area.

        aData->mGeometry = std::move(geometry);

        nsRect clippedBounds = clip.ApplyNonRoundedIntersection(
            aData->mGeometry->ComputeInvalidationRegion());
        LayerIntRect transformedRect =
            ToDeviceSpace(clippedBounds, aMatrix, appUnitsPerDevPixel);
        aData->mRect = transformedRect.Intersect(mClippedImageBounds);
        InvalidateRect(aData->mRect);

        aData->mInvalid = true;
        invalidated = true;
      } else {
        if (aData->mClip != clip) {
          UniquePtr<nsDisplayItemGeometry> geometry(
              aItem->AllocateGeometry(aBuilder));
          if (!IsContainerLayerItem(aItem)) {
            // the bounds of layer items can change on us without
            // ComputeInvalidationRegion returning any change. Other items
            // shouldn't have any hidden geometry change.
            MOZ_RELEASE_ASSERT(
                geometry->mBounds.IsEqualEdges(aData->mGeometry->mBounds));
          } else {
            aData->mGeometry = std::move(geometry);
          }
          nsRect clippedBounds = clip.ApplyNonRoundedIntersection(
              aData->mGeometry->ComputeInvalidationRegion());
          LayerIntRect transformedRect =
              ToDeviceSpace(clippedBounds, aMatrix, appUnitsPerDevPixel);
          InvalidateRect(aData->mRect);
          aData->mRect = transformedRect.Intersect(mClippedImageBounds);
          InvalidateRect(aData->mRect);
          invalidated = true;

          GP("ClipChange: %s %d %d %d %d\n", aItem->Name(), aData->mRect.x,
             aData->mRect.y, aData->mRect.XMost(), aData->mRect.YMost());

        } else if (IsContainerLayerItem(aItem)) {
          UniquePtr<nsDisplayItemGeometry> geometry(
              aItem->AllocateGeometry(aBuilder));
          // we need to catch bounds changes of containers so that we continue
          // to have the correct bounds rects in the recording
          if (DetectContainerLayerPropertiesBoundsChange(aItem, aData,
                                                         *geometry)) {
            nsRect clippedBounds = clip.ApplyNonRoundedIntersection(
                geometry->ComputeInvalidationRegion());
            aData->mGeometry = std::move(geometry);
            LayerIntRect transformedRect =
                ToDeviceSpace(clippedBounds, aMatrix, appUnitsPerDevPixel);
            InvalidateRect(aData->mRect);
            aData->mRect = transformedRect.Intersect(mClippedImageBounds);
            InvalidateRect(aData->mRect);
            invalidated = true;
            GP("DetectContainerLayerPropertiesBoundsChange change\n");
          } else {
            // Handle changes in mClippedImageBounds
            nsRect clippedBounds = clip.ApplyNonRoundedIntersection(
                geometry->ComputeInvalidationRegion());
            LayerIntRect transformedRect =
                ToDeviceSpace(clippedBounds, aMatrix, appUnitsPerDevPixel);
            auto rect = transformedRect.Intersect(mClippedImageBounds);
            if (!rect.IsEqualEdges(aData->mRect)) {
              GP("ContainerLayer image rect bounds change\n");
              InvalidateRect(aData->mRect);
              aData->mRect = rect;
              InvalidateRect(aData->mRect);
              invalidated = true;
            } else {
              GP("Layer NoChange: %s %d %d %d %d\n", aItem->Name(),
                 aData->mRect.x, aData->mRect.y, aData->mRect.XMost(),
                 aData->mRect.YMost());
            }
          }
        } else {
          UniquePtr<nsDisplayItemGeometry> geometry(
              aItem->AllocateGeometry(aBuilder));
          nsRect clippedBounds = clip.ApplyNonRoundedIntersection(
              geometry->ComputeInvalidationRegion());
          LayerIntRect transformedRect =
              ToDeviceSpace(clippedBounds, aMatrix, appUnitsPerDevPixel);
          auto rect = transformedRect.Intersect(mClippedImageBounds);
          // Make sure we update mRect for mClippedImageBounds changes
          if (!rect.IsEqualEdges(aData->mRect)) {
            GP("ContainerLayer image rect bounds change\n");
            InvalidateRect(aData->mRect);
            aData->mRect = rect;
            InvalidateRect(aData->mRect);
            invalidated = true;
          } else {
            GP("NoChange: %s %d %d %d %d\n", aItem->Name(), aData->mRect.x,
               aData->mRect.y, aData->mRect.XMost(), aData->mRect.YMost());
          }
        }
      }
    }

    if (aData->mGeometry && aItem->GetType() == DisplayItemType::TYPE_FILTER) {
      // This hunk DetectContainerLayerPropertiesBoundsChange should be kept in
      // sync.
      aData->mGeometry->mBounds =
          aData->mGeometry->mBounds.Intersect(aItem->GetBuildingRect());
    }

    mHitTestBounds.OrWith(aData->mRect);
    if (!aData->mInvisible) {
      mActualBounds.OrWith(aData->mRect);
    }
    aData->mClip = clip;
    GP("post mInvalidRect: %d %d %d %d\n", mInvalidRect.x, mInvalidRect.y,
       mInvalidRect.width, mInvalidRect.height);
    return invalidated;
  }

  void EndGroup(WebRenderLayerManager* aWrManager,
                nsDisplayListBuilder* aDisplayListBuilder,
                wr::DisplayListBuilder& aBuilder,
                wr::IpcResourceUpdateQueue& aResources, Grouper* aGrouper,
                nsDisplayList::iterator aStartItem,
                nsDisplayList::iterator aEndItem) {
    GP("\n\n");
    GP("Begin EndGroup\n");

    auto scale = LayoutDeviceToLayerScale2D::FromUnknownScale(mScale);

    auto hitTestRect = mVisibleRect.Intersect(ViewAs<LayerPixel>(
        mHitTestBounds, PixelCastJustification::LayerIsImage));
    if (!hitTestRect.IsEmpty()) {
      auto deviceHitTestRect =
          (LayerRect(hitTestRect) - mResidualOffset) / scale;
      PushHitTest(aBuilder, deviceHitTestRect);
    }

    mVisibleRect = mVisibleRect.Intersect(ViewAs<LayerPixel>(
        mActualBounds, PixelCastJustification::LayerIsImage));

    if (mVisibleRect.IsEmpty()) {
      return;
    }

    // Invalidate any unused items
    GP("mDisplayItems\n");
    mDisplayItems.RemoveIf([&](BlobItemData* data) {
      GP("  : %p-%d\n", data->mFrame, data->mDisplayItemKey);
      if (!data->mUsed) {
        GP("Invalidate unused: %p-%d\n", data->mFrame, data->mDisplayItemKey);
        InvalidateRect(data->mRect);
        delete data;
        return true;
      }

      data->mUsed = false;
      return false;
    });

    IntSize dtSize = mVisibleRect.Size().ToUnknownSize();
    // The actual display item's size shouldn't have the scale factored in
    // Round the bounds out to leave space for unsnapped content
    LayoutDeviceRect itemBounds =
        (LayerRect(mVisibleRect) - mResidualOffset) / scale;

    if (mInvalidRect.IsEmpty() && mVisibleRect.IsEqualEdges(mLastVisibleRect)) {
      GP("Not repainting group because it's empty\n");
      GP("End EndGroup\n");
      if (mKey) {
        // Although the contents haven't changed, the visible area *may* have,
        // so request it be updated unconditionally (wr should be able to easily
        // detect if this is a no-op on its side, if that matters)
        aResources.SetBlobImageVisibleArea(
            *mKey, ViewAs<ImagePixel>(mVisibleRect,
                                      PixelCastJustification::LayerIsImage));
        mLastVisibleRect = mVisibleRect;
        PushImage(aBuilder, itemBounds);
      }
      return;
    }

    std::vector<RefPtr<ScaledFont>> fonts;
    bool validFonts = true;
    RefPtr<WebRenderDrawEventRecorder> recorder =
        MakeAndAddRef<WebRenderDrawEventRecorder>(
            [&](MemStream& aStream,
                std::vector<RefPtr<ScaledFont>>& aScaledFonts) {
              size_t count = aScaledFonts.size();
              aStream.write((const char*)&count, sizeof(count));
              for (auto& scaled : aScaledFonts) {
                Maybe<wr::FontInstanceKey> key =
                    aWrManager->WrBridge()->GetFontKeyForScaledFont(scaled,
                                                                    aResources);
                if (key.isNothing()) {
                  validFonts = false;
                  break;
                }
                BlobFont font = {key.value(), scaled};
                aStream.write((const char*)&font, sizeof(font));
              }
              fonts = std::move(aScaledFonts);
            });

    RefPtr<gfx::DrawTarget> dummyDt =
        gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget();

    RefPtr<gfx::DrawTarget> dt = gfx::Factory::CreateRecordingDrawTarget(
        recorder, dummyDt, mLayerBounds.ToUnknownRect());
    if (!dt || !dt->IsValid()) {
      gfxCriticalNote << "Failed to create drawTarget for blob image";
      return;
    }

    gfxContext context(dt);
    context.SetMatrix(Matrix::Scaling(mScale).PostTranslate(mResidualOffset.x,
                                                            mResidualOffset.y));

    GP("mInvalidRect: %d %d %d %d\n", mInvalidRect.x, mInvalidRect.y,
       mInvalidRect.width, mInvalidRect.height);

    RenderRootStateManager* rootManager =
        aWrManager->GetRenderRootStateManager();

    bool empty = aStartItem == aEndItem;
    if (empty) {
      ClearImageKey(rootManager, true);
      return;
    }

    PaintItemRange(aGrouper, aStartItem, aEndItem, &context, recorder,
                   rootManager, aResources);

    // XXX: set this correctly perhaps using
    // aItem->GetOpaqueRegion(aDisplayListBuilder, &snapped).
    //   Contains(paintBounds);?
    wr::OpacityType opacity = wr::OpacityType::HasAlphaChannel;

    bool hasItems = recorder->Finish();
    GP("%d Finish\n", hasItems);
    if (!validFonts) {
      gfxCriticalNote << "Failed serializing fonts for blob image";
      return;
    }
    Range<uint8_t> bytes((uint8_t*)recorder->mOutputStream.mData,
                         recorder->mOutputStream.mLength);
    if (!mKey) {
      // we don't want to send a new image that doesn't have any
      // items in it
      if (!hasItems || mVisibleRect.IsEmpty()) {
        GP("Skipped group with no items\n");
        return;
      }

      wr::BlobImageKey key =
          wr::BlobImageKey{aWrManager->WrBridge()->GetNextImageKey()};
      GP("No previous key making new one %d\n", key._0.mHandle);
      wr::ImageDescriptor descriptor(dtSize, 0, dt->GetFormat(), opacity);
      MOZ_RELEASE_ASSERT(bytes.length() > sizeof(size_t));
      if (!aResources.AddBlobImage(
              key, descriptor, bytes,
              ViewAs<ImagePixel>(mVisibleRect,
                                 PixelCastJustification::LayerIsImage))) {
        return;
      }
      mKey = Some(key);
    } else {
      MOZ_DIAGNOSTIC_ASSERT(
          aWrManager->WrBridge()->MatchesNamespace(mKey.ref()),
          "Stale blob key for group!");

      wr::ImageDescriptor descriptor(dtSize, 0, dt->GetFormat(), opacity);

      // Convert mInvalidRect to image space by subtracting the corner of the
      // image bounds
      auto dirtyRect = ViewAs<ImagePixel>(mInvalidRect,
                                          PixelCastJustification::LayerIsImage);

      auto bottomRight = dirtyRect.BottomRight();
      GP("check invalid %d %d - %d %d\n", bottomRight.x.value,
         bottomRight.y.value, dtSize.width, dtSize.height);
      GP("Update Blob %d %d %d %d\n", mInvalidRect.x, mInvalidRect.y,
         mInvalidRect.width, mInvalidRect.height);
      if (!aResources.UpdateBlobImage(
              *mKey, descriptor, bytes,
              ViewAs<ImagePixel>(mVisibleRect,
                                 PixelCastJustification::LayerIsImage),
              dirtyRect)) {
        return;
      }
    }
    mFonts = std::move(fonts);
    aResources.SetBlobImageVisibleArea(
        *mKey,
        ViewAs<ImagePixel>(mVisibleRect, PixelCastJustification::LayerIsImage));
    mLastVisibleRect = mVisibleRect;
    PushImage(aBuilder, itemBounds);
    GP("End EndGroup\n\n");
  }

  void PushImage(wr::DisplayListBuilder& aBuilder,
                 const LayoutDeviceRect& bounds) {
    wr::LayoutRect dest = wr::ToLayoutRect(bounds);
    GP("PushImage: %f %f %f %f\n", dest.min.x, dest.min.y, dest.max.x,
       dest.max.y);
    // wr::ToImageRendering(aItem->Frame()->UsedImageRendering());
    auto rendering = wr::ImageRendering::Auto;
    bool backfaceHidden = false;

    // XXX - clipping the item against the paint rect breaks some content.
    // cf. Bug 1455422.
    // wr::LayoutRect clip = wr::ToLayoutRect(bounds.Intersect(mVisibleRect));

    aBuilder.PushImage(dest, dest, !backfaceHidden, false, rendering,
                       wr::AsImageKey(*mKey));
  }

  void PushHitTest(wr::DisplayListBuilder& aBuilder,
                   const LayoutDeviceRect& bounds) {
    wr::LayoutRect dest = wr::ToLayoutRect(bounds);
    GP("PushHitTest: %f %f %f %f\n", dest.min.x, dest.min.y, dest.max.x,
       dest.max.y);

    // We don't really know the exact shape of this blob because it may contain
    // SVG shapes. Also mHitInfo may be a combination of hit info flags from
    // different shapes so generate an irregular-area hit-test region for it.
    CompositorHitTestInfo hitInfo = mHitInfo;
    if (hitInfo.contains(CompositorHitTestFlags::eVisibleToHitTest)) {
      hitInfo += CompositorHitTestFlags::eIrregularArea;
    }

    bool backfaceHidden = false;
    aBuilder.PushHitTest(dest, dest, !backfaceHidden, mScrollId, hitInfo,
                         SideBits::eNone);
  }

  void PaintItemRange(Grouper* aGrouper, nsDisplayList::iterator aStartItem,
                      nsDisplayList::iterator aEndItem, gfxContext* aContext,
                      WebRenderDrawEventRecorder* aRecorder,
                      RenderRootStateManager* aRootManager,
                      wr::IpcResourceUpdateQueue& aResources) {
    LayerIntSize size = mVisibleRect.Size();
    for (auto it = aStartItem; it != aEndItem; ++it) {
      nsDisplayItem* item = *it;
      MOZ_ASSERT(item);

      if (item->GetType() == DisplayItemType::TYPE_COMPOSITOR_HITTEST_INFO) {
        continue;
      }

      BlobItemData* data = GetBlobItemData(item);
      if (data->mInvisible) {
        continue;
      }

      LayerIntRect bounds = data->mRect;

      // skip empty items
      if (bounds.IsEmpty()) {
        continue;
      }

      GP("Trying %s %p-%d %d %d %d %d\n", item->Name(), item->Frame(),
         item->GetPerFrameKey(), bounds.x, bounds.y, bounds.XMost(),
         bounds.YMost());

      auto bottomRight = bounds.BottomRight();

      GP("paint check invalid %d %d - %d %d\n", bottomRight.x.value,
         bottomRight.y.value, size.width, size.height);

      bool dirty = true;
      auto preservedBounds = bounds.Intersect(mPreservedRect);
      if (!mInvalidRect.Contains(preservedBounds)) {
        GP("Passing\n");
        dirty = false;
        if (data->mInvalid) {
          gfxCriticalError()
              << "DisplayItem" << item->Name() << "-should be invalid";
        }
        // if the item is invalid it needs to be fully contained
        MOZ_RELEASE_ASSERT(!data->mInvalid);
      }

      nsDisplayList* children = item->GetChildren();
      if (children) {
        // If we aren't dirty, we still need to iterate over the children to
        // ensure the blob index data is recorded the same as before to allow
        // the merging of the parts inside in the invalid rect. Any items that
        // are painted as a single item need to avoid repainting in that case.
        GP("doing children in EndGroup\n");
        aGrouper->PaintContainerItem(this, item, data, bounds.ToUnknownRect(),
                                     dirty, children, aContext, aRecorder,
                                     aRootManager, aResources);
        continue;
      }
      nsPaintedDisplayItem* paintedItem = item->AsPaintedDisplayItem();
      if (!paintedItem) {
        continue;
      }
      if (dirty) {
        // What should the clip settting strategy be? We can set the full
        // clip everytime. this is probably easiest for now. An alternative
        // would be to put the push and the pop into separate items and let
        // invalidation handle it that way.
        DisplayItemClip currentClip = paintedItem->GetClip();

        if (currentClip.HasClip()) {
          aContext->Save();
          currentClip.ApplyTo(aContext, aGrouper->mAppUnitsPerDevPixel);
        }
        aContext->NewPath();
        GP("painting %s %p-%d\n", paintedItem->Name(), paintedItem->Frame(),
           paintedItem->GetPerFrameKey());
        if (aGrouper->mDisplayListBuilder->IsPaintingToWindow()) {
          paintedItem->Frame()->AddStateBits(NS_FRAME_PAINTED_THEBES);
        }

        paintedItem->Paint(aGrouper->mDisplayListBuilder, aContext);
        TakeExternalSurfaces(aRecorder, data->mExternalSurfaces, aRootManager,
                             aResources);

        if (currentClip.HasClip()) {
          aContext->Restore();
        }
      }
      aContext->GetDrawTarget()->FlushItem(bounds.ToUnknownRect());
    }
  }

  ~DIGroup() {
    GP("Group destruct\n");
    for (BlobItemData* data : mDisplayItems) {
      GP("Deleting %p-%d\n", data->mFrame, data->mDisplayItemKey);
      delete data;
    }
  }
};

// If we have an item we need to make sure it matches the current group
// otherwise it means the item switched groups and we need to invalidate
// it and recreate the data.
static BlobItemData* GetBlobItemDataForGroup(nsDisplayItem* aItem,
                                             DIGroup* aGroup) {
  BlobItemData* data = GetBlobItemData(aItem);
  if (data) {
    MOZ_ASSERT(data->mGroup->mDisplayItems.Contains(data));
    if (data->mGroup != aGroup) {
      GP("group don't match %p %p\n", data->mGroup, aGroup);
      data->ClearFrame();
      // the item is for another group
      // it should be cleared out as being unused at the end of this paint
      data = nullptr;
    }
  }
  if (!data) {
    GP("Allocating blob data\n");
    data = new BlobItemData(aGroup, aItem);
    aGroup->mDisplayItems.Insert(data);
  }
  data->mUsed = true;
  return data;
}

void Grouper::PaintContainerItem(DIGroup* aGroup, nsDisplayItem* aItem,
                                 BlobItemData* aData,
                                 const IntRect& aItemBounds, bool aDirty,
                                 nsDisplayList* aChildren, gfxContext* aContext,
                                 WebRenderDrawEventRecorder* aRecorder,
                                 RenderRootStateManager* aRootManager,
                                 wr::IpcResourceUpdateQueue& aResources) {
  switch (aItem->GetType()) {
    case DisplayItemType::TYPE_TRANSFORM: {
      DisplayItemClip currentClip = aItem->GetClip();

      gfxContextMatrixAutoSaveRestore saveMatrix;
      if (currentClip.HasClip()) {
        aContext->Save();
        currentClip.ApplyTo(aContext, this->mAppUnitsPerDevPixel);
        aContext->GetDrawTarget()->FlushItem(aItemBounds);
      } else {
        saveMatrix.SetContext(aContext);
      }

      auto transformItem = static_cast<nsDisplayTransform*>(aItem);
      Matrix4x4Flagged trans = transformItem->GetTransform();
      Matrix trans2d;
      if (!trans.Is2D(&trans2d)) {
        // Painting will cause us to include the item's recording in the blob.
        // We only want to do that if it is dirty, because otherwise the
        // recording might change (e.g. due to factor of 2 scaling of images
        // giving different results) and the merging will discard it because it
        // is outside the invalid rect.
        if (aDirty) {
          // We don't currently support doing invalidation inside 3d transforms.
          // For now just paint it as a single item.
          aItem->AsPaintedDisplayItem()->Paint(mDisplayListBuilder, aContext);
          TakeExternalSurfaces(aRecorder, aData->mExternalSurfaces,
                               aRootManager, aResources);
        }
        aContext->GetDrawTarget()->FlushItem(aItemBounds);
      } else if (!trans2d.IsSingular()) {
        aContext->Multiply(ThebesMatrix(trans2d));
        aGroup->PaintItemRange(this, aChildren->begin(), aChildren->end(),
                               aContext, aRecorder, aRootManager, aResources);
      }

      if (currentClip.HasClip()) {
        aContext->Restore();
        aContext->GetDrawTarget()->FlushItem(aItemBounds);
      }
      break;
    }
    case DisplayItemType::TYPE_OPACITY: {
      auto opacityItem = static_cast<nsDisplayOpacity*>(aItem);
      float opacity = opacityItem->GetOpacity();
      if (opacity == 0.0f) {
        return;
      }

      aContext->GetDrawTarget()->PushLayer(false, opacityItem->GetOpacity(),
                                           nullptr, mozilla::gfx::Matrix(),
                                           aItemBounds);
      GP("beginGroup %s %p-%d\n", aItem->Name(), aItem->Frame(),
         aItem->GetPerFrameKey());
      aContext->GetDrawTarget()->FlushItem(aItemBounds);
      aGroup->PaintItemRange(this, aChildren->begin(), aChildren->end(),
                             aContext, aRecorder, aRootManager, aResources);
      aContext->GetDrawTarget()->PopLayer();
      GP("endGroup %s %p-%d\n", aItem->Name(), aItem->Frame(),
         aItem->GetPerFrameKey());
      aContext->GetDrawTarget()->FlushItem(aItemBounds);
      break;
    }
    case DisplayItemType::TYPE_BLEND_MODE: {
      auto blendItem = static_cast<nsDisplayBlendMode*>(aItem);
      auto blendMode = blendItem->BlendMode();
      aContext->GetDrawTarget()->PushLayerWithBlend(
          false, 1.0, nullptr, mozilla::gfx::Matrix(), aItemBounds, false,
          blendMode);
      GP("beginGroup %s %p-%d\n", aItem->Name(), aItem->Frame(),
         aItem->GetPerFrameKey());
      aContext->GetDrawTarget()->FlushItem(aItemBounds);
      aGroup->PaintItemRange(this, aChildren->begin(), aChildren->end(),
                             aContext, aRecorder, aRootManager, aResources);
      aContext->GetDrawTarget()->PopLayer();
      GP("endGroup %s %p-%d\n", aItem->Name(), aItem->Frame(),
         aItem->GetPerFrameKey());
      aContext->GetDrawTarget()->FlushItem(aItemBounds);
      break;
    }
    case DisplayItemType::TYPE_BLEND_CONTAINER: {
      auto* bc = static_cast<nsDisplayBlendContainer*>(aItem);
      const bool flatten = bc->ShouldFlattenAway(mDisplayListBuilder);
      if (!flatten) {
        aContext->GetDrawTarget()->PushLayer(
            false, 1.0, nullptr, mozilla::gfx::Matrix(), aItemBounds);
        GP("beginGroup %s %p-%d\n", aItem->Name(), aItem->Frame(),
           aItem->GetPerFrameKey());
        aContext->GetDrawTarget()->FlushItem(aItemBounds);
      }
      aGroup->PaintItemRange(this, aChildren->begin(), aChildren->end(),
                             aContext, aRecorder, aRootManager, aResources);
      if (!flatten) {
        aContext->GetDrawTarget()->PopLayer();
        GP("endGroup %s %p-%d\n", aItem->Name(), aItem->Frame(),
           aItem->GetPerFrameKey());
        aContext->GetDrawTarget()->FlushItem(aItemBounds);
      }
      break;
    }
    case DisplayItemType::TYPE_MASK: {
      GP("Paint Mask\n");
      auto maskItem = static_cast<nsDisplayMasksAndClipPaths*>(aItem);
      if (maskItem->IsValidMask()) {
        maskItem->PaintWithContentsPaintCallback(
            mDisplayListBuilder, aContext, [&] {
              GP("beginGroup %s %p-%d\n", aItem->Name(), aItem->Frame(),
                 aItem->GetPerFrameKey());
              aContext->GetDrawTarget()->FlushItem(aItemBounds);
              aGroup->PaintItemRange(this, aChildren->begin(), aChildren->end(),
                                     aContext, aRecorder, aRootManager,
                                     aResources);
              GP("endGroup %s %p-%d\n", aItem->Name(), aItem->Frame(),
                 aItem->GetPerFrameKey());
            });
        TakeExternalSurfaces(aRecorder, aData->mExternalSurfaces, aRootManager,
                             aResources);
        aContext->GetDrawTarget()->FlushItem(aItemBounds);
      }
      break;
    }
    case DisplayItemType::TYPE_FILTER: {
      GP("Paint Filter\n");
      // Painting will cause us to include the item's recording in the blob. We
      // only want to do that if it is dirty, because otherwise the recording
      // might change (e.g. due to factor of 2 scaling of images giving
      // different results) and the merging will discard it because it is
      // outside the invalid rect.
      if (aDirty) {
        auto filterItem = static_cast<nsDisplayFilters*>(aItem);
        filterItem->Paint(mDisplayListBuilder, aContext);
        TakeExternalSurfaces(aRecorder, aData->mExternalSurfaces, aRootManager,
                             aResources);
      }
      aContext->GetDrawTarget()->FlushItem(aItemBounds);
      break;
    }

    default:
      aGroup->PaintItemRange(this, aChildren->begin(), aChildren->end(),
                             aContext, aRecorder, aRootManager, aResources);
      break;
  }
}

class WebRenderGroupData : public WebRenderUserData {
 public:
  WebRenderGroupData(RenderRootStateManager* aWRManager, nsDisplayItem* aItem);
  WebRenderGroupData(RenderRootStateManager* aWRManager,
                     uint32_t aDisplayItemKey, nsIFrame* aFrame);
  virtual ~WebRenderGroupData();

  WebRenderGroupData* AsGroupData() override { return this; }
  UserDataType GetType() override { return UserDataType::eGroup; }
  static UserDataType Type() { return UserDataType::eGroup; }

  DIGroup mSubGroup;
  DIGroup mFollowingGroup;
};

enum class ItemActivity : uint8_t {
  /// Item must not be active.
  No = 0,
  /// Could be active if it has no layerization cost.
  /// Typically active if first of an item group.
  Could = 1,
  /// Should be active unless something external makes that less useful.
  /// For example if the item is affected by a complex mask, it remains
  /// inactive.
  Should = 2,
  /// Must be active regardless of external factors.
  Must = 3,
};

ItemActivity CombineActivity(ItemActivity a, ItemActivity b) {
  return a > b ? a : b;
}

bool ActivityAtLeast(ItemActivity rhs, ItemActivity atLeast) {
  return rhs >= atLeast;
}

static ItemActivity IsItemProbablyActive(
    nsDisplayItem* aItem, mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder, bool aSiblingActive,
    bool aUniformlyScaled);

static ItemActivity HasActiveChildren(
    const nsDisplayList& aList, mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder, bool aUniformlyScaled) {
  ItemActivity activity = ItemActivity::No;
  for (nsDisplayItem* item : aList) {
    // Here we only want to know if a child must be active, so we don't specify
    // when the item is first or last, which can cause an item that could be
    // either decide to be active. This is a bit conservative and avoids some
    // extra layers. It's a good tradeoff until we get to the point where most
    // items could have been active but none *had* to. Right now this is
    // unlikely but as more svg items get webrenderized it will be better to
    // make them active more aggressively.
    auto childActivity =
        IsItemProbablyActive(item, aBuilder, aResources, aSc, aManager,
                             aDisplayListBuilder, false, aUniformlyScaled);
    activity = CombineActivity(activity, childActivity);
    if (activity == ItemActivity::Must) {
      return activity;
    }
  }
  return activity;
}

static ItemActivity AssessBounds(const StackingContextHelper& aSc,
                                 nsDisplayListBuilder* aDisplayListBuilder,
                                 nsDisplayItem* aItem,
                                 bool aHasActivePrecedingSibling) {
  // Arbitrary threshold up for adjustments. What we want to avoid here
  // is alternating between active and non active items and create a lot
  // of overlapping blobs, so we only make images active if they are
  // costly enough that it's worth the risk of having more layers. As we
  // move more blob items into wr display items it will become less of a
  // concern.
  constexpr float largeish = 512;

  bool snap = false;
  nsRect bounds = aItem->GetBounds(aDisplayListBuilder, &snap);

  float appUnitsPerDevPixel =
      static_cast<float>(aItem->Frame()->PresContext()->AppUnitsPerDevPixel());

  float width =
      static_cast<float>(bounds.width) * aSc.GetInheritedScale().xScale;
  float height =
      static_cast<float>(bounds.height) * aSc.GetInheritedScale().yScale;

  // Webrender doesn't handle primitives smaller than a pixel well, so
  // avoid making them active.
  if (width >= appUnitsPerDevPixel && height >= appUnitsPerDevPixel) {
    if (aHasActivePrecedingSibling || width > largeish || height > largeish) {
      return ItemActivity::Should;
    }

    return ItemActivity::Could;
  }

  return ItemActivity::No;
}

// This function decides whether we want to treat this item as "active", which
// means that it's a container item which we will turn into a WebRender
// StackingContext, or whether we treat it as "inactive" and include it inside
// the parent blob image.
//
// We can't easily use GetLayerState because it wants a bunch of layers related
// information.
static ItemActivity IsItemProbablyActive(
    nsDisplayItem* aItem, mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder, bool aHasActivePrecedingSibling,
    bool aUniformlyScaled) {
  switch (aItem->GetType()) {
    case DisplayItemType::TYPE_TRANSFORM: {
      nsDisplayTransform* transformItem =
          static_cast<nsDisplayTransform*>(aItem);
      const Matrix4x4Flagged& t = transformItem->GetTransform();
      Matrix t2d;
      bool is2D = t.Is2D(&t2d);
      if (!is2D) {
        return ItemActivity::Must;
      }

      auto activity = HasActiveChildren(*transformItem->GetChildren(), aBuilder,
                                        aResources, aSc, aManager,
                                        aDisplayListBuilder, aUniformlyScaled);

      if (transformItem->MayBeAnimated(aDisplayListBuilder)) {
        activity = CombineActivity(activity, ItemActivity::Should);
      }

      return activity;
    }
    case DisplayItemType::TYPE_OPACITY: {
      auto* opacityItem = static_cast<nsDisplayOpacity*>(aItem);
      if (opacityItem->NeedsActiveLayer()) {
        return ItemActivity::Must;
      }
      return HasActiveChildren(*opacityItem->GetChildren(), aBuilder,
                               aResources, aSc, aManager, aDisplayListBuilder,
                               aUniformlyScaled);
    }
    case DisplayItemType::TYPE_FOREIGN_OBJECT: {
      return ItemActivity::Must;
    }
    case DisplayItemType::TYPE_SVG_GEOMETRY: {
      auto* svgItem = static_cast<DisplaySVGGeometry*>(aItem);
      if (StaticPrefs::gfx_webrender_svg_shapes() && aUniformlyScaled &&
          svgItem->ShouldBeActive(aBuilder, aResources, aSc, aManager,
                                  aDisplayListBuilder)) {
        return AssessBounds(aSc, aDisplayListBuilder, aItem,
                            aHasActivePrecedingSibling);
      }

      return ItemActivity::No;
    }
    case DisplayItemType::TYPE_SVG_IMAGE: {
      auto* svgItem = static_cast<DisplaySVGImage*>(aItem);
      if (StaticPrefs::gfx_webrender_svg_images() && aUniformlyScaled &&
          svgItem->ShouldBeActive(aBuilder, aResources, aSc, aManager,
                                  aDisplayListBuilder)) {
        return AssessBounds(aSc, aDisplayListBuilder, aItem,
                            aHasActivePrecedingSibling);
      }

      return ItemActivity::No;
    }
    case DisplayItemType::TYPE_BLEND_MODE: {
      /* BLEND_MODE needs to be active if it might have a previous sibling
       * that is active so that it's able to blend with that content. */
      if (aHasActivePrecedingSibling) {
        return ItemActivity::Must;
      }

      return HasActiveChildren(*aItem->GetChildren(), aBuilder, aResources, aSc,
                               aManager, aDisplayListBuilder, aUniformlyScaled);
    }
    case DisplayItemType::TYPE_MASK: {
      if (aItem->GetChildren()) {
        auto activity =
            HasActiveChildren(*aItem->GetChildren(), aBuilder, aResources, aSc,
                              aManager, aDisplayListBuilder, aUniformlyScaled);
        // For masked items, don't bother with making children active since we
        // are going to have to need to paint and upload a large mask anyway.
        if (activity < ItemActivity::Must) {
          return ItemActivity::No;
        }
        return activity;
      }
      return ItemActivity::No;
    }
    case DisplayItemType::TYPE_WRAP_LIST:
    case DisplayItemType::TYPE_CONTAINER:
    case DisplayItemType::TYPE_PERSPECTIVE: {
      if (aItem->GetChildren()) {
        return HasActiveChildren(*aItem->GetChildren(), aBuilder, aResources,
                                 aSc, aManager, aDisplayListBuilder,
                                 aUniformlyScaled);
      }
      return ItemActivity::No;
    }
    case DisplayItemType::TYPE_FILTER: {
      nsDisplayFilters* filters = static_cast<nsDisplayFilters*>(aItem);
      if (filters->CanCreateWebRenderCommands()) {
        // Items are usually expensive enough on the CPU that we want to
        // make them active whenever we can.
        return ItemActivity::Must;
      }
      return ItemActivity::No;
    }
    default:
      // TODO: handle other items?
      return ItemActivity::No;
  }
}

// This does a pass over the display lists and will join the display items
// into groups as well as paint them
void Grouper::ConstructGroups(nsDisplayListBuilder* aDisplayListBuilder,
                              WebRenderCommandBuilder* aCommandBuilder,
                              wr::DisplayListBuilder& aBuilder,
                              wr::IpcResourceUpdateQueue& aResources,
                              DIGroup* aGroup, nsDisplayList* aList,
                              nsDisplayItem* aWrappingItem,
                              const StackingContextHelper& aSc) {
  RenderRootStateManager* manager =
      aCommandBuilder->mManager->GetRenderRootStateManager();

  nsDisplayList::iterator startOfCurrentGroup = aList->end();
  DIGroup* currentGroup = aGroup;

  // We need to track whether we have active siblings for mixed blend mode.
  bool encounteredActiveItem = false;
  bool isFirstGroup = true;
  // Track whether the item is the first (visible) of its group in which case
  // making it active won't add extra layers.
  bool isFirst = true;

  for (auto it = aList->begin(); it != aList->end(); ++it) {
    nsDisplayItem* item = *it;
    MOZ_ASSERT(item);

    if (item->HasHitTestInfo()) {
      // Accumulate the hit-test info flags. In cases where there are multiple
      // hittest-info display items with different flags, mHitInfo will have
      // the union of all those flags. If that is the case, we will
      // additionally set eIrregularArea (at the site that we use mHitInfo)
      // so that downstream consumers of this (primarily APZ) will know that
      // the exact shape of what gets hit with what is unknown.
      currentGroup->mHitInfo += item->GetHitTestInfo().Info();
    }

    if (startOfCurrentGroup == aList->end()) {
      startOfCurrentGroup = it;
      if (!isFirstGroup) {
        mClipManager.SwitchItem(aDisplayListBuilder, aWrappingItem);
      }
    }

    bool isLast = it.HasNext();

    // WebRender's anti-aliasing approximation is not very good under
    // non-uniform scales.
    bool uniformlyScaled =
        fabs(aGroup->mScale.xScale - aGroup->mScale.yScale) < 0.1;

    auto activity = IsItemProbablyActive(
        item, aBuilder, aResources, aSc, manager, mDisplayListBuilder,
        encounteredActiveItem, uniformlyScaled);
    auto threshold =
        isFirst || isLast ? ItemActivity::Could : ItemActivity::Should;

    if (activity >= threshold) {
      encounteredActiveItem = true;
      // We're going to be starting a new group.
      RefPtr<WebRenderGroupData> groupData =
          aCommandBuilder->CreateOrRecycleWebRenderUserData<WebRenderGroupData>(
              item);

      groupData->mFollowingGroup.mInvalidRect.SetEmpty();

      // Initialize groupData->mFollowingGroup with data from currentGroup.
      // We want to copy out this information before calling EndGroup because
      // EndGroup will set mLastVisibleRect depending on whether
      // we send something to WebRender.

      // TODO: compute the group bounds post-grouping, so that they can be
      // tighter for just the sublist that made it into this group.
      // We want to ensure the tight bounds are still clipped by area
      // that we're building the display list for.
      if (groupData->mFollowingGroup.mScale != currentGroup->mScale ||
          groupData->mFollowingGroup.mAppUnitsPerDevPixel !=
              currentGroup->mAppUnitsPerDevPixel ||
          groupData->mFollowingGroup.mResidualOffset !=
              currentGroup->mResidualOffset) {
        if (groupData->mFollowingGroup.mAppUnitsPerDevPixel !=
            currentGroup->mAppUnitsPerDevPixel) {
          GP("app unit change following: %d %d\n",
             groupData->mFollowingGroup.mAppUnitsPerDevPixel,
             currentGroup->mAppUnitsPerDevPixel);
        }
        // The group changed size
        GP("Inner group size change\n");
        groupData->mFollowingGroup.ClearItems();
        groupData->mFollowingGroup.ClearImageKey(
            aCommandBuilder->mManager->GetRenderRootStateManager());
      }
      groupData->mFollowingGroup.mAppUnitsPerDevPixel =
          currentGroup->mAppUnitsPerDevPixel;
      groupData->mFollowingGroup.mLayerBounds = currentGroup->mLayerBounds;
      groupData->mFollowingGroup.mClippedImageBounds =
          currentGroup->mClippedImageBounds;
      groupData->mFollowingGroup.mScale = currentGroup->mScale;
      groupData->mFollowingGroup.mResidualOffset =
          currentGroup->mResidualOffset;
      groupData->mFollowingGroup.mVisibleRect = currentGroup->mVisibleRect;
      groupData->mFollowingGroup.mPreservedRect =
          groupData->mFollowingGroup.mVisibleRect.Intersect(
              groupData->mFollowingGroup.mLastVisibleRect);
      groupData->mFollowingGroup.mActualBounds = LayerIntRect();
      groupData->mFollowingGroup.mHitTestBounds = LayerIntRect();
      groupData->mFollowingGroup.mHitInfo = currentGroup->mHitInfo;

      currentGroup->EndGroup(aCommandBuilder->mManager, aDisplayListBuilder,
                             aBuilder, aResources, this, startOfCurrentGroup,
                             it);

      {
        auto spaceAndClipChain =
            mClipManager.SwitchItem(aDisplayListBuilder, item);
        wr::SpaceAndClipChainHelper saccHelper(aBuilder, spaceAndClipChain);
        bool hasHitTest = mHitTestInfoManager.ProcessItem(item, aBuilder,
                                                          aDisplayListBuilder);
        // XXX - This is hacky. Some items have hit testing info on them but we
        // also have dedicated hit testing items, the flags of which apply to
        // the the group that contains them. We don't want layerization to
        // affect that so if the item didn't emit any hit testing then we still
        // push a hit test item if the previous group had some hit test flags
        // set. This is obviously not great. Hit testing should be independent
        // from how we layerize.
        if (!hasHitTest &&
            currentGroup->mHitInfo != gfx::CompositorHitTestInvisibleToHit) {
          auto hitTestRect = item->GetBuildingRect();
          if (!hitTestRect.IsEmpty()) {
            currentGroup->PushHitTest(
                aBuilder, LayoutDeviceRect::FromAppUnits(
                              hitTestRect, currentGroup->mAppUnitsPerDevPixel));
          }
        }

        sIndent++;
        // Note: this call to CreateWebRenderCommands can recurse back into
        // this function.
        bool createdWRCommands = item->CreateWebRenderCommands(
            aBuilder, aResources, aSc, manager, mDisplayListBuilder);
        MOZ_RELEASE_ASSERT(
            createdWRCommands,
            "active transforms should always succeed at creating "
            "WebRender commands");
        sIndent--;
      }

      isFirstGroup = false;
      startOfCurrentGroup = aList->end();
      currentGroup = &groupData->mFollowingGroup;
      isFirst = true;
    } else {  // inactive item
      bool isInvisible = false;
      ConstructItemInsideInactive(aCommandBuilder, aBuilder, aResources,
                                  currentGroup, item, aSc, &isInvisible);
      if (!isInvisible) {
        // Invisible items don't count.
        isFirst = false;
      }
    }
  }

  currentGroup->EndGroup(aCommandBuilder->mManager, aDisplayListBuilder,
                         aBuilder, aResources, this, startOfCurrentGroup,
                         aList->end());
}

// This does a pass over the display lists and will join the display items
// into a single group.
bool Grouper::ConstructGroupInsideInactive(
    WebRenderCommandBuilder* aCommandBuilder, wr::DisplayListBuilder& aBuilder,
    wr::IpcResourceUpdateQueue& aResources, DIGroup* aGroup,
    nsDisplayList* aList, const StackingContextHelper& aSc) {
  bool invalidated = false;
  for (nsDisplayItem* item : *aList) {
    if (item->HasHitTestInfo()) {
      // Accumulate the hit-test info flags. In cases where there are multiple
      // hittest-info display items with different flags, mHitInfo will have
      // the union of all those flags. If that is the case, we will
      // additionally set eIrregularArea (at the site that we use mHitInfo)
      // so that downstream consumers of this (primarily APZ) will know that
      // the exact shape of what gets hit with what is unknown.
      aGroup->mHitInfo += item->GetHitTestInfo().Info();
    }

    bool invisible = false;
    invalidated |= ConstructItemInsideInactive(
        aCommandBuilder, aBuilder, aResources, aGroup, item, aSc, &invisible);
  }
  return invalidated;
}

bool Grouper::ConstructItemInsideInactive(
    WebRenderCommandBuilder* aCommandBuilder, wr::DisplayListBuilder& aBuilder,
    wr::IpcResourceUpdateQueue& aResources, DIGroup* aGroup,
    nsDisplayItem* aItem, const StackingContextHelper& aSc,
    bool* aOutIsInvisible) {
  nsDisplayList* children = aItem->GetChildren();
  BlobItemData* data = GetBlobItemDataForGroup(aItem, aGroup);

  /* mInvalid unfortunately persists across paints. Clear it so that if we don't
   * set it to 'true' we ensure that we're not using the value from the last
   * time that we painted */
  data->mInvalid = false;
  data->mInvisible = aItem->IsInvisible();
  *aOutIsInvisible = data->mInvisible;

  // we compute the geometry change here because we have the transform around
  // still
  bool invalidated = aGroup->ComputeGeometryChange(aItem, data, mTransform,
                                                   mDisplayListBuilder);

  // Temporarily restrict the image bounds to the bounds of the container so
  // that clipped children within the container know about the clip. This
  // ensures that the bounds passed to FlushItem are contained in the bounds of
  // the clip so that we don't include items in the recording without including
  // their corresponding clipping items.
  auto oldClippedImageBounds = aGroup->mClippedImageBounds;
  aGroup->mClippedImageBounds =
      aGroup->mClippedImageBounds.Intersect(data->mRect);

  if (aItem->GetType() == DisplayItemType::TYPE_FILTER) {
    // If ConstructGroupInsideInactive finds any change, we invalidate the
    // entire container item. This is needed because blob merging requires the
    // entire item to be within the invalid region.
    Matrix m = mTransform;
    mTransform = Matrix();
    sIndent++;
    if (ConstructGroupInsideInactive(aCommandBuilder, aBuilder, aResources,
                                     aGroup, children, aSc)) {
      data->mInvalid = true;
      aGroup->InvalidateRect(data->mRect);
      invalidated = true;
    }
    sIndent--;
    mTransform = m;
  } else if (aItem->GetType() == DisplayItemType::TYPE_TRANSFORM) {
    Matrix m = mTransform;
    nsDisplayTransform* transformItem = static_cast<nsDisplayTransform*>(aItem);
    const Matrix4x4Flagged& t = transformItem->GetTransform();
    Matrix t2d;
    bool is2D = t.CanDraw2D(&t2d);
    if (!is2D) {
      // If ConstructGroupInsideInactive finds any change, we invalidate the
      // entire container item. This is needed because blob merging requires the
      // entire item to be within the invalid region.
      mTransform = Matrix();
      sIndent++;
      if (ConstructGroupInsideInactive(aCommandBuilder, aBuilder, aResources,
                                       aGroup, children, aSc)) {
        data->mInvalid = true;
        aGroup->InvalidateRect(data->mRect);
        invalidated = true;
      }
      sIndent--;
    } else {
      GP("t2d: %f %f\n", t2d._31, t2d._32);
      mTransform.PreMultiply(t2d);
      GP("mTransform: %f %f\n", mTransform._31, mTransform._32);
      sIndent++;
      invalidated |= ConstructGroupInsideInactive(
          aCommandBuilder, aBuilder, aResources, aGroup, children, aSc);
      sIndent--;
    }
    mTransform = m;
  } else if (children) {
    sIndent++;
    invalidated |= ConstructGroupInsideInactive(
        aCommandBuilder, aBuilder, aResources, aGroup, children, aSc);
    sIndent--;
  }

  GP("Including %s of %d\n", aItem->Name(), aGroup->mDisplayItems.Count());
  aGroup->mClippedImageBounds = oldClippedImageBounds;
  return invalidated;
}

/* This is just a copy of nsRect::ScaleToOutsidePixels with an offset added in.
 * The offset is applied just before the rounding. It's in the scaled space. */
static mozilla::LayerIntRect ScaleToOutsidePixelsOffset(
    nsRect aRect, float aXScale, float aYScale, nscoord aAppUnitsPerPixel,
    LayerPoint aOffset) {
  mozilla::LayerIntRect rect;
  rect.SetNonEmptyBox(
      NSToIntFloor(NSAppUnitsToFloatPixels(aRect.x, float(aAppUnitsPerPixel)) *
                       aXScale +
                   aOffset.x),
      NSToIntFloor(NSAppUnitsToFloatPixels(aRect.y, float(aAppUnitsPerPixel)) *
                       aYScale +
                   aOffset.y),
      NSToIntCeil(
          NSAppUnitsToFloatPixels(aRect.XMost(), float(aAppUnitsPerPixel)) *
              aXScale +
          aOffset.x),
      NSToIntCeil(
          NSAppUnitsToFloatPixels(aRect.YMost(), float(aAppUnitsPerPixel)) *
              aYScale +
          aOffset.y));
  return rect;
}

/* This function is the same as the above except that it rounds to the
 * nearest instead of rounding out. We use it for attempting to compute the
 * actual pixel bounds of opaque items */
static mozilla::gfx::IntRect ScaleToNearestPixelsOffset(
    nsRect aRect, float aXScale, float aYScale, nscoord aAppUnitsPerPixel,
    LayerPoint aOffset) {
  mozilla::gfx::IntRect rect;
  rect.SetNonEmptyBox(
      NSToIntFloor(NSAppUnitsToFloatPixels(aRect.x, float(aAppUnitsPerPixel)) *
                       aXScale +
                   aOffset.x + 0.5),
      NSToIntFloor(NSAppUnitsToFloatPixels(aRect.y, float(aAppUnitsPerPixel)) *
                       aYScale +
                   aOffset.y + 0.5),
      NSToIntFloor(
          NSAppUnitsToFloatPixels(aRect.XMost(), float(aAppUnitsPerPixel)) *
              aXScale +
          aOffset.x + 0.5),
      NSToIntFloor(
          NSAppUnitsToFloatPixels(aRect.YMost(), float(aAppUnitsPerPixel)) *
              aYScale +
          aOffset.y + 0.5));
  return rect;
}

RenderRootStateManager* WebRenderCommandBuilder::GetRenderRootStateManager() {
  return mManager->GetRenderRootStateManager();
}

void WebRenderCommandBuilder::DoGroupingForDisplayList(
    nsDisplayList* aList, nsDisplayItem* aWrappingItem,
    nsDisplayListBuilder* aDisplayListBuilder, const StackingContextHelper& aSc,
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources) {
  if (!aList->GetBottom()) {
    return;
  }

  GP("DoGroupingForDisplayList\n");

  mClipManager.BeginList(aSc);
  mHitTestInfoManager.Reset();
  Grouper g(mClipManager);

  int32_t appUnitsPerDevPixel =
      aWrappingItem->Frame()->PresContext()->AppUnitsPerDevPixel();

  g.mDisplayListBuilder = aDisplayListBuilder;
  RefPtr<WebRenderGroupData> groupData =
      CreateOrRecycleWebRenderUserData<WebRenderGroupData>(aWrappingItem);

  nsRect groupBounds =
      aWrappingItem->GetUntransformedBounds(aDisplayListBuilder);
  DIGroup& group = groupData->mSubGroup;

  auto scale = aSc.GetInheritedScale();
  GP("Inherited scale %f %f\n", scale.xScale, scale.yScale);

  auto trans =
      ViewAs<LayerPixel>(aSc.GetSnappingSurfaceTransform().GetTranslation());
  auto snappedTrans = LayerIntPoint::Floor(trans);
  LayerPoint residualOffset = trans - snappedTrans;

  auto layerBounds =
      ScaleToOutsidePixelsOffset(groupBounds, scale.xScale, scale.yScale,
                                 appUnitsPerDevPixel, residualOffset);

  const nsRect& untransformedPaintRect =
      aWrappingItem->GetUntransformedPaintRect();

  auto visibleRect = ScaleToOutsidePixelsOffset(
                         untransformedPaintRect, scale.xScale, scale.yScale,
                         appUnitsPerDevPixel, residualOffset)
                         .Intersect(layerBounds);

  GP("LayerBounds: %d %d %d %d\n", layerBounds.x, layerBounds.y,
     layerBounds.width, layerBounds.height);
  GP("VisibleRect: %d %d %d %d\n", visibleRect.x, visibleRect.y,
     visibleRect.width, visibleRect.height);

  GP("Inherited scale %f %f\n", scale.xScale, scale.yScale);

  group.mInvalidRect.SetEmpty();
  if (group.mAppUnitsPerDevPixel != appUnitsPerDevPixel ||
      group.mScale != scale || group.mResidualOffset != residualOffset) {
    GP("Property change. Deleting blob\n");

    if (group.mAppUnitsPerDevPixel != appUnitsPerDevPixel) {
      GP(" App unit change %d -> %d\n", group.mAppUnitsPerDevPixel,
         appUnitsPerDevPixel);
    }

    if (group.mScale != scale) {
      GP(" Scale %f %f -> %f %f\n", group.mScale.xScale, group.mScale.yScale,
         scale.xScale, scale.yScale);
    }

    if (group.mResidualOffset != residualOffset) {
      GP(" Residual Offset %f %f -> %f %f\n", group.mResidualOffset.x.value,
         group.mResidualOffset.y.value, residualOffset.x.value,
         residualOffset.y.value);
    }

    group.ClearItems();
    group.ClearImageKey(mManager->GetRenderRootStateManager());
  }

  ScrollableLayerGuid::ViewID scrollId = ScrollableLayerGuid::NULL_SCROLL_ID;
  if (const ActiveScrolledRoot* asr = aWrappingItem->GetActiveScrolledRoot()) {
    scrollId = asr->GetNearestScrollASRViewId();
  }

  g.mAppUnitsPerDevPixel = appUnitsPerDevPixel;
  group.mResidualOffset = residualOffset;
  group.mLayerBounds = layerBounds;
  group.mVisibleRect = visibleRect;
  group.mActualBounds = LayerIntRect();
  group.mHitTestBounds = LayerIntRect();
  group.mPreservedRect = group.mVisibleRect.Intersect(group.mLastVisibleRect);
  group.mAppUnitsPerDevPixel = appUnitsPerDevPixel;
  group.mClippedImageBounds = layerBounds;

  g.mTransform =
      Matrix::Scaling(scale).PostTranslate(residualOffset.x, residualOffset.y);
  group.mScale = scale;
  group.mScrollId = scrollId;
  g.ConstructGroups(aDisplayListBuilder, this, aBuilder, aResources, &group,
                    aList, aWrappingItem, aSc);
  mClipManager.EndList(aSc);
}

WebRenderCommandBuilder::WebRenderCommandBuilder(
    WebRenderLayerManager* aManager)
    : mManager(aManager),
      mLastAsr(nullptr),
      mBuilderDumpIndex(0),
      mDumpIndent(0),
      mApzEnabled(true),
      mComputingOpaqueRegion(XRE_IsParentProcess()),
      mDoGrouping(false),
      mContainsSVGGroup(false) {}

void WebRenderCommandBuilder::Destroy() {
  mLastCanvasDatas.Clear();
  ClearCachedResources();
}

void WebRenderCommandBuilder::EmptyTransaction() {
  // We need to update canvases that might have changed.
  for (RefPtr<WebRenderCanvasData> canvasData : mLastCanvasDatas) {
    WebRenderCanvasRendererAsync* canvas = canvasData->GetCanvasRenderer();
    if (canvas) {
      canvas->UpdateCompositableClientForEmptyTransaction();
    }
  }
}

bool WebRenderCommandBuilder::NeedsEmptyTransaction() {
  return !mLastCanvasDatas.IsEmpty();
}

void WebRenderCommandBuilder::BuildWebRenderCommands(
    wr::DisplayListBuilder& aBuilder,
    wr::IpcResourceUpdateQueue& aResourceUpdates, nsDisplayList* aDisplayList,
    nsDisplayListBuilder* aDisplayListBuilder, WebRenderScrollData& aScrollData,
    WrFiltersHolder&& aFilters) {
  AUTO_PROFILER_LABEL_CATEGORY_PAIR(GRAPHICS_WRDisplayList);

  StackingContextHelper sc;
  aScrollData = WebRenderScrollData(mManager, aDisplayListBuilder);
  MOZ_ASSERT(mLayerScrollData.empty());
  mClipManager.BeginBuild(mManager, aBuilder);
  mHitTestInfoManager.Reset();

  mBuilderDumpIndex = 0;
  mLastCanvasDatas.Clear();
  mLastAsr = nullptr;
  mContainsSVGGroup = false;
  MOZ_ASSERT(mDumpIndent == 0);

  {
    wr::StackingContextParams params;
    params.mRootReferenceFrame = aDisplayListBuilder->RootReferenceFrame();
    params.mFilters = std::move(aFilters.filters);
    params.mFilterDatas = std::move(aFilters.filter_datas);
    params.clip =
        wr::WrStackingContextClip::ClipChain(aBuilder.CurrentClipChainId());

    StackingContextHelper pageRootSc(sc, nullptr, nullptr, nullptr, aBuilder,
                                     params);
    if (ShouldDumpDisplayList(aDisplayListBuilder)) {
      mBuilderDumpIndex =
          aBuilder.Dump(mDumpIndent + 1, Some(mBuilderDumpIndex), Nothing());
    }
    CreateWebRenderCommandsFromDisplayList(aDisplayList, nullptr,
                                           aDisplayListBuilder, pageRootSc,
                                           aBuilder, aResourceUpdates);
  }

  // Make a "root" layer data that has everything else as descendants
  mLayerScrollData.emplace_back();
  mLayerScrollData.back().InitializeRoot(mLayerScrollData.size() - 1);
  auto callback =
      [&aScrollData](ScrollableLayerGuid::ViewID aScrollId) -> bool {
    return aScrollData.HasMetadataFor(aScrollId).isSome();
  };
  Maybe<ScrollMetadata> rootMetadata =
      nsLayoutUtils::GetRootMetadata(aDisplayListBuilder, mManager, callback);
  if (rootMetadata) {
    // Put the fallback root metadata on the rootmost layer that is
    // a matching async zoom container, or the root layer that we just
    // created above.
    size_t rootMetadataTarget = mLayerScrollData.size() - 1;
    for (size_t i = rootMetadataTarget; i > 0; i--) {
      if (auto zoomContainerId =
              mLayerScrollData[i - 1].GetAsyncZoomContainerId()) {
        if (*zoomContainerId == rootMetadata->GetMetrics().GetScrollId()) {
          rootMetadataTarget = i - 1;
          break;
        }
      }
    }
    mLayerScrollData[rootMetadataTarget].AppendScrollMetadata(
        aScrollData, rootMetadata.ref());
  }

  // Append the WebRenderLayerScrollData items into WebRenderScrollData
  // in reverse order, from topmost to bottommost. This is in keeping with
  // the semantics of WebRenderScrollData.
  for (auto it = mLayerScrollData.rbegin(); it != mLayerScrollData.rend();
       it++) {
    aScrollData.AddLayerData(std::move(*it));
  }
  mLayerScrollData.clear();
  mClipManager.EndBuild();

  // Remove the user data those are not displayed on the screen and
  // also reset the data to unused for next transaction.
  RemoveUnusedAndResetWebRenderUserData();
}

bool WebRenderCommandBuilder::ShouldDumpDisplayList(
    nsDisplayListBuilder* aBuilder) {
  return aBuilder && aBuilder->IsInActiveDocShell() &&
         ((XRE_IsParentProcess() &&
           StaticPrefs::gfx_webrender_debug_dl_dump_parent()) ||
          (XRE_IsContentProcess() &&
           StaticPrefs::gfx_webrender_debug_dl_dump_content()));
}

void WebRenderCommandBuilder::CreateWebRenderCommands(
    nsDisplayItem* aItem, mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc,
    nsDisplayListBuilder* aDisplayListBuilder) {
  mHitTestInfoManager.ProcessItem(aItem, aBuilder, aDisplayListBuilder);
  if (aItem->GetType() == DisplayItemType::TYPE_COMPOSITOR_HITTEST_INFO) {
    // The hit test information was processed above.
    return;
  }

  auto* item = aItem->AsPaintedDisplayItem();
  MOZ_RELEASE_ASSERT(item, "Tried to paint item that cannot be painted");

  RenderRootStateManager* manager = mManager->GetRenderRootStateManager();

  // Note: this call to CreateWebRenderCommands can recurse back into
  // this function if the |item| is a wrapper for a sublist.
  const bool createdWRCommands = aItem->CreateWebRenderCommands(
      aBuilder, aResources, aSc, manager, aDisplayListBuilder);

  if (!createdWRCommands) {
    PushItemAsImage(aItem, aBuilder, aResources, aSc, aDisplayListBuilder);
  }
}

// A helper struct to store information needed when creating a new
// WebRenderLayerScrollData in CreateWebRenderCommandsFromDisplayList().
// This information is gathered before the recursion, and then used to
// emit the new layer after the recursion.
struct NewLayerData {
  size_t mLayerCountBeforeRecursing = 0;
  const ActiveScrolledRoot* mStopAtAsr = nullptr;

  // Information pertaining to the deferred transform.
  nsDisplayTransform* mDeferredItem = nullptr;
  ScrollableLayerGuid::ViewID mDeferredId = ScrollableLayerGuid::NULL_SCROLL_ID;
  bool mTransformShouldGetOwnLayer = false;

  void ComputeDeferredTransformInfo(const StackingContextHelper& aSc,
                                    nsDisplayItem* aItem) {
    // See the comments on StackingContextHelper::mDeferredTransformItem
    // for an overview of what deferred transforms are.
    // In the case where we deferred a transform, but have a child display
    // item with a different ASR than the deferred transform item, we cannot
    // put the transform on the WebRenderLayerScrollData item for the child.
    // We cannot do this because it will not conform to APZ's expectations
    // with respect to how the APZ tree ends up structured. In particular,
    // the GetTransformToThis() for the child APZ (which is created for the
    // child item's ASR) will not include the transform when we actually do
    // want it to.
    // When we run into this scenario, we solve it by creating two
    // WebRenderLayerScrollData items; one that just holds the transform,
    // that we deferred, and a child WebRenderLayerScrollData item that
    // holds the scroll metadata for the child's ASR.
    mDeferredItem = aSc.GetDeferredTransformItem();
    if (mDeferredItem) {
      // It's possible the transform's ASR is not only an ancestor of
      // the item's ASR, but an ancestor of stopAtAsr. In such cases,
      // don't use the transform at all at this level (it would be
      // scrolled by stopAtAsr which is incorrect). The transform will
      // instead be emitted as part of the ancestor WebRenderLayerScrollData
      // node (the one with stopAtAsr as its item ASR), or one of its
      // ancetors in turn.
      if (ActiveScrolledRoot::IsProperAncestor(
              mDeferredItem->GetActiveScrolledRoot(), mStopAtAsr)) {
        mDeferredItem = nullptr;
      }
    }
    if (mDeferredItem) {
      if (const auto* asr = mDeferredItem->GetActiveScrolledRoot()) {
        mDeferredId = asr->GetNearestScrollASRViewId();
      }
      if (mDeferredItem->GetActiveScrolledRoot() !=
          aItem->GetActiveScrolledRoot()) {
        mTransformShouldGetOwnLayer = true;
      } else if (aItem->GetType() == DisplayItemType::TYPE_SCROLL_INFO_LAYER) {
        // A scroll info layer has its own scroll id that's not reflected
        // in item->GetActiveScrolledRoot(), but will be added to the
        // WebRenderLayerScrollData node, so it needs to be treated as
        // having a distinct ASR from the deferred transform item.
        mTransformShouldGetOwnLayer = true;
      }
    }
  }
};

static Maybe<nsPoint> AllowComputingOpaqueRegionAcross(
    nsDisplayItem* aWrappingItem, nsDisplayListBuilder* aBuilder) {
  MOZ_ASSERT(aWrappingItem);
  if (aWrappingItem->GetType() != DisplayItemType::TYPE_TRANSFORM) {
    return {};
  }
  auto* transformItem = static_cast<nsDisplayTransform*>(aWrappingItem);
  if (transformItem->MayBeAnimated(aBuilder)) {
    return {};
  }
  const auto& transform = transformItem->GetTransform();
  if (!transform.Is2D()) {
    return {};
  }
  const auto transform2d = transform.GetMatrix().As2D();
  if (!transform2d.IsTranslation()) {
    return {};
  }
  return Some(LayoutDevicePoint::ToAppUnits(
      LayoutDevicePoint::FromUnknownPoint(transform2d.GetTranslation()),
      transformItem->Frame()->PresContext()->AppUnitsPerDevPixel()));
}

struct MOZ_STACK_CLASS WebRenderCommandBuilder::AutoOpaqueRegionStateTracker {
  WebRenderCommandBuilder& mBuilder;
  const bool mWasComputingOpaqueRegion;
  bool mThroughWrapper = false;

  AutoOpaqueRegionStateTracker(WebRenderCommandBuilder& aBuilder,
                               nsDisplayListBuilder* aDlBuilder,
                               nsDisplayItem* aWrappingItem)
      : mBuilder(aBuilder),
        mWasComputingOpaqueRegion(aBuilder.mComputingOpaqueRegion) {
    if (!mBuilder.mComputingOpaqueRegion || !aWrappingItem) {
      return;
    }
    Maybe<nsPoint> offset =
        AllowComputingOpaqueRegionAcross(aWrappingItem, aDlBuilder);
    if (!offset) {
      aBuilder.mComputingOpaqueRegion = false;
    } else {
      mThroughWrapper = true;
      aBuilder.mOpaqueRegionWrappers.AppendElement(
          std::make_pair(aWrappingItem, *offset));
    }
  }

  ~AutoOpaqueRegionStateTracker() {
    if (!mWasComputingOpaqueRegion) {
      return;
    }
    if (mThroughWrapper) {
      mBuilder.mOpaqueRegionWrappers.RemoveLastElement();
    }
    mBuilder.mComputingOpaqueRegion = mWasComputingOpaqueRegion;
  }
};

void WebRenderCommandBuilder::CreateWebRenderCommandsFromDisplayList(
    nsDisplayList* aDisplayList, nsDisplayItem* aWrappingItem,
    nsDisplayListBuilder* aDisplayListBuilder, const StackingContextHelper& aSc,
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    bool aNewClipList) {
  if (mDoGrouping) {
    MOZ_RELEASE_ASSERT(
        aWrappingItem,
        "Only the root list should have a null wrapping item, and mDoGrouping "
        "should never be true for the root list.");
    GP("actually entering the grouping code\n");
    DoGroupingForDisplayList(aDisplayList, aWrappingItem, aDisplayListBuilder,
                             aSc, aBuilder, aResources);
    return;
  }

  const bool dumpEnabled = ShouldDumpDisplayList(aDisplayListBuilder);
  if (dumpEnabled) {
    // If we're inside a nested display list, print the WR DL items from the
    // wrapper item before we start processing the nested items.
    mBuilderDumpIndex =
        aBuilder.Dump(mDumpIndent + 1, Some(mBuilderDumpIndex), Nothing());
  }

  FlattenedDisplayListIterator iter(aDisplayListBuilder, aDisplayList);
  if (!iter.HasNext()) {
    return;
  }

  mDumpIndent++;
  if (aNewClipList) {
    mClipManager.BeginList(aSc);
  }

  AutoOpaqueRegionStateTracker tracker(*this, aDisplayListBuilder,
                                       aWrappingItem);
  do {
    nsDisplayItem* item = iter.GetNextItem();

    const DisplayItemType itemType = item->GetType();

    // If this is a new (not retained/reused) item, then we need to disable
    // the display item cache for descendants, since it's possible that some of
    // them got cached with a flattened opacity values., which may no longer be
    // applied.
    if (itemType == DisplayItemType::TYPE_OPACITY) {
      nsDisplayOpacity* opacity = static_cast<nsDisplayOpacity*>(item);

      if (opacity->CanApplyOpacityToChildren(
              mManager->GetRenderRootStateManager()->LayerManager(),
              aDisplayListBuilder, aBuilder.GetInheritedOpacity())) {
        // If all our children support handling the opacity directly, then push
        // the opacity and clip onto the builder and skip creating a stacking
        // context.
        float oldOpacity = aBuilder.GetInheritedOpacity();
        const DisplayItemClipChain* oldClip = aBuilder.GetInheritedClipChain();
        aBuilder.SetInheritedOpacity(oldOpacity * opacity->GetOpacity());
        aBuilder.PushInheritedClipChain(aDisplayListBuilder,
                                        opacity->GetClipChain());

        CreateWebRenderCommandsFromDisplayList(opacity->GetChildren(), item,
                                               aDisplayListBuilder, aSc,
                                               aBuilder, aResources, false);

        aBuilder.SetInheritedOpacity(oldOpacity);
        aBuilder.SetInheritedClipChain(oldClip);
        continue;
      }
    }

    // If this is an unscrolled background item, in the root display list
    // for the parent process, consider doing opaque checks.
    if (mComputingOpaqueRegion &&
        (itemType == DisplayItemType::TYPE_BACKGROUND_COLOR ||
         itemType == DisplayItemType::TYPE_SOLID_COLOR ||
         itemType == DisplayItemType::TYPE_BACKGROUND) &&
        !item->GetActiveScrolledRoot()) {
      bool snap;
      nsRegion opaque = item->GetOpaqueRegion(aDisplayListBuilder, &snap);
      if (opaque.GetNumRects() == 1) {
        nsRect result =
            item->GetClip().ApproximateIntersectInward(opaque.GetBounds());
        if (!result.IsEmpty()) {
          for (auto& [item, offset] : Reversed(mOpaqueRegionWrappers)) {
            result =
                item->GetClip().ApproximateIntersectInward(result + offset);
            if (result.IsEmpty()) {
              break;
            }
          }
          if (!result.IsEmpty()) {
            aDisplayListBuilder->AddWindowOpaqueRegion(item->Frame(), result);
          }
        }
      }
    }

    AutoRestore<bool> restoreApzEnabled(mApzEnabled);
    mApzEnabled = mApzEnabled && mManager->AsyncPanZoomEnabled() &&
                  itemType != DisplayItemType::TYPE_VT_CAPTURE;

    Maybe<NewLayerData> newLayerData;
    if (mApzEnabled) {
      // For some types of display items we want to force a new
      // WebRenderLayerScrollData object, to ensure we preserve the APZ-relevant
      // data that is in the display item.
      if (item->UpdateScrollData(nullptr, nullptr)) {
        newLayerData = Some(NewLayerData());
      }

      // Anytime the ASR changes we also want to force a new layer data because
      // the stack of scroll metadata is going to be different for this
      // display item than previously, so we can't squash the display items
      // into the same "layer".
      const ActiveScrolledRoot* asr = item->GetActiveScrolledRoot();
      if (asr != mLastAsr) {
        mLastAsr = asr;
        newLayerData = Some(NewLayerData());
      }

      // Refer to the comment on StackingContextHelper::mDeferredTransformItem
      // for an overview of what this is about. This bit of code applies to the
      // case where we are deferring a transform item, and we then need to defer
      // another transform with a different ASR. In such a case we cannot just
      // merge the deferred transforms, but need to force a new
      // WebRenderLayerScrollData item to flush the old deferred transform, so
      // that we can then start deferring the new one.
      if (!newLayerData && item->CreatesStackingContextHelper() &&
          aSc.GetDeferredTransformItem() &&
          aSc.GetDeferredTransformItem()->GetActiveScrolledRoot() != asr) {
        newLayerData = Some(NewLayerData());
      }

      // If we're going to create a new layer data for this item, stash the
      // ASR so that if we recurse into a sublist they will know where to stop
      // walking up their ASR chain when building scroll metadata.
      if (newLayerData) {
        newLayerData->mLayerCountBeforeRecursing = mLayerScrollData.size();
        newLayerData->mStopAtAsr =
            mAsrStack.empty() ? nullptr : mAsrStack.back();
        newLayerData->mStopAtAsr = ActiveScrolledRoot::LowestCommonAncestor(
            asr, newLayerData->mStopAtAsr);
        newLayerData->ComputeDeferredTransformInfo(aSc, item);

        // Our children's |stopAtAsr| must not be an ancestor of our
        // |stopAtAsr|, otherwise we could get cyclic scroll metadata
        // annotations.
        MOZ_ASSERT(
            ActiveScrolledRoot::IsAncestor(newLayerData->mStopAtAsr, asr));
        const ActiveScrolledRoot* stopAtAsrForChildren = asr;
        // Additionally, while unusual and probably indicative of a poorly
        // behaved display list, it's possible to have a deferred transform item
        // which we will emit as its own layer on the way out of the recursion,
        // whose ASR (let's call it T) is a *descendant* of the current item's
        // ASR. In such cases, make sure our children have stopAtAsr=T,
        // otherwise ASRs in the range [T, asr) may be emitted in duplicate,
        // leading again to cylic scroll metadata annotations.
        if (newLayerData->mTransformShouldGetOwnLayer) {
          stopAtAsrForChildren = ActiveScrolledRoot::PickDescendant(
              stopAtAsrForChildren,
              newLayerData->mDeferredItem->GetActiveScrolledRoot());
        }
        mAsrStack.push_back(stopAtAsrForChildren);

        // If we're going to emit a deferred transform onto this layer,
        // clear the deferred transform from the StackingContextHelper
        // while we are building the subtree of descendant layers.
        // This ensures that the deferred transform is not applied in
        // duplicate to any of our descendant layers.
        if (newLayerData->mDeferredItem) {
          aSc.ClearDeferredTransformItem();
        }
      }
    }

    // This is where we emulate the clip/scroll stack that was previously
    // implemented on the WR display list side.
    auto spaceAndClipChain = mClipManager.SwitchItem(aDisplayListBuilder, item);
    wr::SpaceAndClipChainHelper saccHelper(aBuilder, spaceAndClipChain);

    {  // scope restoreDoGrouping
      AutoRestore<bool> restoreDoGrouping(mDoGrouping);
      if (itemType == DisplayItemType::TYPE_SVG_WRAPPER) {
        // Inside an <svg>, all display items that are not LAYER_ACTIVE wrapper
        // display items (like animated transforms / opacity) share the same
        // animated geometry root, so we can combine subsequent items of that
        // type into the same image.
        mContainsSVGGroup = mDoGrouping = true;
        GP("attempting to enter the grouping code\n");
      }

      if (dumpEnabled) {
        std::stringstream ss;
        nsIFrame::PrintDisplayItem(aDisplayListBuilder, item, ss,
                                   static_cast<uint32_t>(mDumpIndent));
        printf_stderr("%s", ss.str().c_str());
      }

      CreateWebRenderCommands(item, aBuilder, aResources, aSc,
                              aDisplayListBuilder);

      if (dumpEnabled) {
        mBuilderDumpIndex =
            aBuilder.Dump(mDumpIndent + 1, Some(mBuilderDumpIndex), Nothing());
      }
    }

    if (newLayerData) {
      // Pop the thing we pushed before the recursion, so the topmost item on
      // the stack is enclosing display item's ASR (or the stack is empty)
      mAsrStack.pop_back();

      if (newLayerData->mDeferredItem) {
        aSc.RestoreDeferredTransformItem(newLayerData->mDeferredItem);
      }

      const ActiveScrolledRoot* stopAtAsr = newLayerData->mStopAtAsr;

      int32_t descendants =
          mLayerScrollData.size() - newLayerData->mLayerCountBeforeRecursing;

      nsDisplayTransform* deferred = newLayerData->mDeferredItem;
      ScrollableLayerGuid::ViewID deferredId = newLayerData->mDeferredId;

      if (newLayerData->mTransformShouldGetOwnLayer) {
        // This creates the child WebRenderLayerScrollData for |item|, but
        // omits the transform (hence the Nothing() as the last argument to
        // Initialize(...)). We also need to make sure that the ASR from
        // the deferred transform item is not on this node, so we use that
        // ASR as the "stop at" ASR for this WebRenderLayerScrollData.
        mLayerScrollData.emplace_back();
        mLayerScrollData.back().Initialize(
            mManager->GetScrollData(), item, descendants,
            deferred->GetActiveScrolledRoot(), Nothing(),
            ScrollableLayerGuid::NULL_SCROLL_ID);

        // The above WebRenderLayerScrollData will also be a descendant of
        // the transform-holding WebRenderLayerScrollData we create below.
        descendants++;

        // This creates the WebRenderLayerScrollData for the deferred
        // transform item. This holds the transform matrix and the remaining
        // ASRs needed to complete the ASR chain (i.e. the ones from the
        // stopAtAsr down to the deferred transform item's ASR, which must be
        // "between" stopAtAsr and |item|'s ASR in the ASR tree).
        mLayerScrollData.emplace_back();
        mLayerScrollData.back().Initialize(
            mManager->GetScrollData(), deferred, descendants, stopAtAsr,
            aSc.GetDeferredTransformMatrix(), deferredId);
      } else {
        // This is the "simple" case where we don't need to create two
        // WebRenderLayerScrollData items; we can just create one that also
        // holds the deferred transform matrix, if any.
        mLayerScrollData.emplace_back();
        mLayerScrollData.back().Initialize(
            mManager->GetScrollData(), item, descendants, stopAtAsr,
            deferred ? aSc.GetDeferredTransformMatrix() : Nothing(),
            deferredId);
      }
    }
  } while (iter.HasNext());

  mDumpIndent--;
  if (aNewClipList) {
    mClipManager.EndList(aSc);
  }
}

void WebRenderCommandBuilder::PushOverrideForASR(
    const ActiveScrolledRoot* aASR, const wr::WrSpatialId& aSpatialId) {
  mClipManager.PushOverrideForASR(aASR, aSpatialId);
}

void WebRenderCommandBuilder::PopOverrideForASR(
    const ActiveScrolledRoot* aASR) {
  mClipManager.PopOverrideForASR(aASR);
}

static wr::WrRotation ToWrRotation(VideoRotation aRotation) {
  switch (aRotation) {
    case VideoRotation::kDegree_0:
      return wr::WrRotation::Degree0;
    case VideoRotation::kDegree_90:
      return wr::WrRotation::Degree90;
    case VideoRotation::kDegree_180:
      return wr::WrRotation::Degree180;
    case VideoRotation::kDegree_270:
      return wr::WrRotation::Degree270;
  }
  return wr::WrRotation::Degree0;
}

Maybe<wr::ImageKey> WebRenderCommandBuilder::CreateImageKey(
    nsDisplayItem* aItem, ImageContainer* aContainer,
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    mozilla::wr::ImageRendering aRendering, const StackingContextHelper& aSc,
    gfx::IntSize& aSize, const Maybe<LayoutDeviceRect>& aAsyncImageBounds) {
  RefPtr<WebRenderImageData> imageData =
      CreateOrRecycleWebRenderUserData<WebRenderImageData>(aItem);
  MOZ_ASSERT(imageData);

  if (aContainer->IsAsync()) {
    MOZ_ASSERT(aAsyncImageBounds);

    LayoutDeviceRect rect = aAsyncImageBounds.value();
    LayoutDeviceRect scBounds(LayoutDevicePoint(0, 0), rect.Size());
    // TODO!
    // We appear to be using the image bridge for a lot (most/all?) of
    // layers-free image handling and that breaks frame consistency.
    imageData->CreateAsyncImageWebRenderCommands(
        aBuilder, aContainer, aSc, rect, scBounds,
        ToWrRotation(aContainer->GetRotation()), aRendering,
        wr::MixBlendMode::Normal, !aItem->BackfaceIsHidden());
    return Nothing();
  }

  AutoLockImage autoLock(aContainer);
  if (!autoLock.HasImage()) {
    return Nothing();
  }
  mozilla::layers::Image* image = autoLock.GetImage();
  aSize = image->GetSize();

  return imageData->UpdateImageKey(aContainer, aResources);
}

bool WebRenderCommandBuilder::PushImage(
    nsDisplayItem* aItem, ImageContainer* aContainer,
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, const LayoutDeviceRect& aRect,
    const LayoutDeviceRect& aClip) {
  auto rendering = wr::ToImageRendering(aItem->Frame()->UsedImageRendering());
  gfx::IntSize size;
  Maybe<wr::ImageKey> key =
      CreateImageKey(aItem, aContainer, aBuilder, aResources, rendering, aSc,
                     size, Some(aRect));
  if (aContainer->IsAsync()) {
    // Async ImageContainer does not create ImageKey, instead it uses Pipeline.
    MOZ_ASSERT(key.isNothing());
    return true;
  }
  if (!key) {
    return false;
  }

  auto r = wr::ToLayoutRect(aRect);
  auto c = wr::ToLayoutRect(aClip);
  aBuilder.PushImage(r, c, !aItem->BackfaceIsHidden(), false, rendering,
                     key.value());

  return true;
}

Maybe<wr::ImageKey> WebRenderCommandBuilder::CreateImageProviderKey(
    nsDisplayItem* aItem, image::WebRenderImageProvider* aProvider,
    image::ImgDrawResult aDrawResult,
    mozilla::wr::IpcResourceUpdateQueue& aResources) {
  RefPtr<WebRenderImageProviderData> imageData =
      CreateOrRecycleWebRenderUserData<WebRenderImageProviderData>(aItem);
  MOZ_ASSERT(imageData);
  return imageData->UpdateImageKey(aProvider, aDrawResult, aResources);
}

bool WebRenderCommandBuilder::PushImageProvider(
    nsDisplayItem* aItem, image::WebRenderImageProvider* aProvider,
    image::ImgDrawResult aDrawResult, mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const LayoutDeviceRect& aRect, const LayoutDeviceRect& aClip) {
  Maybe<wr::ImageKey> key =
      CreateImageProviderKey(aItem, aProvider, aDrawResult, aResources);
  if (!key) {
    return false;
  }

  bool antialiased = aItem->GetType() == DisplayItemType::TYPE_SVG_GEOMETRY;

  auto rendering = wr::ToImageRendering(aItem->Frame()->UsedImageRendering());
  auto r = wr::ToLayoutRect(aRect);
  auto c = wr::ToLayoutRect(aClip);
  aBuilder.PushImage(r, c, !aItem->BackfaceIsHidden(), antialiased, rendering,
                     key.value());

  return true;
}

static void PaintItemByDrawTarget(nsDisplayItem* aItem, gfx::DrawTarget* aDT,
                                  const LayoutDevicePoint& aOffset,
                                  const IntRect& visibleRect,
                                  nsDisplayListBuilder* aDisplayListBuilder,
                                  const gfx::MatrixScales& aScale,
                                  Maybe<gfx::DeviceColor>& aHighlight) {
  MOZ_ASSERT(aDT && aDT->IsValid());

  // XXX Why is this ClearRect() needed?
  aDT->ClearRect(Rect(visibleRect));
  gfxContext context(aDT);

  switch (aItem->GetType()) {
    case DisplayItemType::TYPE_SVG_WRAPPER:
    case DisplayItemType::TYPE_MASK: {
      // These items should be handled by other code paths
      MOZ_RELEASE_ASSERT(0);
      break;
    }
    default:
      if (!aItem->AsPaintedDisplayItem()) {
        break;
      }

      context.SetMatrix(context.CurrentMatrix().PreScale(aScale).PreTranslate(
          -aOffset.x, -aOffset.y));
      if (aDisplayListBuilder->IsPaintingToWindow()) {
        aItem->Frame()->AddStateBits(NS_FRAME_PAINTED_THEBES);
      }
      aItem->AsPaintedDisplayItem()->Paint(aDisplayListBuilder, &context);
      break;
  }

  if (aHighlight && aItem->GetType() != DisplayItemType::TYPE_MASK) {
    // Apply highlight fills, if the appropriate prefs are set.
    // We don't do this for masks because we'd be filling the A8 mask surface,
    // which isn't very useful.
    aDT->SetTransform(gfx::Matrix());
    aDT->FillRect(Rect(visibleRect), gfx::ColorPattern(aHighlight.value()));
  }
}

bool WebRenderCommandBuilder::ComputeInvalidationForDisplayItem(
    nsDisplayListBuilder* aBuilder, const nsPoint& aShift,
    nsDisplayItem* aItem) {
  RefPtr<WebRenderFallbackData> fallbackData =
      CreateOrRecycleWebRenderUserData<WebRenderFallbackData>(aItem);

  nsRect invalid;
  if (!fallbackData->mGeometry || aItem->IsInvalid(invalid)) {
    fallbackData->mGeometry = WrapUnique(aItem->AllocateGeometry(aBuilder));
    return true;
  }

  fallbackData->mGeometry->MoveBy(aShift);
  nsRegion combined;
  aItem->ComputeInvalidationRegion(aBuilder, fallbackData->mGeometry.get(),
                                   &combined);

  UniquePtr<nsDisplayItemGeometry> geometry;
  if (!combined.IsEmpty() || aItem->NeedsGeometryUpdates()) {
    geometry = WrapUnique(aItem->AllocateGeometry(aBuilder));
  }

  fallbackData->mClip.AddOffsetAndComputeDifference(
      aShift, fallbackData->mGeometry->ComputeInvalidationRegion(),
      aItem->GetClip(),
      geometry ? geometry->ComputeInvalidationRegion()
               : fallbackData->mGeometry->ComputeInvalidationRegion(),
      &combined);

  if (geometry) {
    fallbackData->mGeometry = std::move(geometry);
  }
  fallbackData->mClip = aItem->GetClip();

  if (!combined.IsEmpty()) {
    return true;
  } else if (aItem->GetChildren()) {
    return ComputeInvalidationForDisplayList(aBuilder, aShift,
                                             aItem->GetChildren());
  }
  return false;
}

bool WebRenderCommandBuilder::ComputeInvalidationForDisplayList(
    nsDisplayListBuilder* aBuilder, const nsPoint& aShift,
    nsDisplayList* aList) {
  FlattenedDisplayListIterator iter(aBuilder, aList);
  while (iter.HasNext()) {
    if (ComputeInvalidationForDisplayItem(aBuilder, aShift,
                                          iter.GetNextItem())) {
      return true;
    }
  }
  return false;
}

// When drawing fallback images we create either
// a real image or a blob image that will contain the display item.
// In the case of a blob image we paint the item at 0,0 instead
// of trying to keep at aItem->GetBounds().TopLeft() like we do
// with SVG. We do this because there's not necessarily a reference frame
// between us and the rest of the world so the the coordinates
// that we get for the bounds are not necessarily stable across scrolling
// or other movement.
already_AddRefed<WebRenderFallbackData>
WebRenderCommandBuilder::GenerateFallbackData(
    nsDisplayItem* aItem, wr::DisplayListBuilder& aBuilder,
    wr::IpcResourceUpdateQueue& aResources, const StackingContextHelper& aSc,
    nsDisplayListBuilder* aDisplayListBuilder, LayoutDeviceRect& aImageRect) {
  Maybe<gfx::DeviceColor> highlight;
  if (StaticPrefs::gfx_webrender_debug_highlight_painted_layers()) {
    highlight.emplace(gfx::DeviceColor(1.0, 0.0, 0.0, 0.5));
  }

  RefPtr<WebRenderFallbackData> fallbackData =
      CreateOrRecycleWebRenderUserData<WebRenderFallbackData>(aItem);

  // Blob images will only draw the visible area of the blob so we don't need to
  // clip them here and can just rely on the webrender clipping.
  // TODO We also don't clip native themed widget to avoid over-invalidation
  // during scrolling. It would be better to support a sort of streaming/tiling
  // scheme for large ones but the hope is that we should not have large native
  // themed items.
  bool snap;
  nsRect paintBounds = aItem->GetBounds(aDisplayListBuilder, &snap);
  nsRect buildingRect = aItem->GetBuildingRect();

  const int32_t appUnitsPerDevPixel =
      aItem->Frame()->PresContext()->AppUnitsPerDevPixel();
  auto bounds =
      LayoutDeviceRect::FromAppUnits(paintBounds, appUnitsPerDevPixel);
  if (bounds.IsEmpty()) {
    return nullptr;
  }

  MatrixScales scale = aSc.GetInheritedScale();
  MatrixScales oldScale = fallbackData->mScale;
  // We tolerate slight changes in scale so that we don't, for example,
  // rerasterize on MotionMark
  bool differentScale = gfx::FuzzyEqual(scale.xScale, oldScale.xScale, 1e-6f) &&
                        gfx::FuzzyEqual(scale.yScale, oldScale.yScale, 1e-6f);

  auto layerScale = LayoutDeviceToLayerScale2D::FromUnknownScale(scale);

  auto trans =
      ViewAs<LayerPixel>(aSc.GetSnappingSurfaceTransform().GetTranslation());

  if (!FitsInt32(trans.X()) || !FitsInt32(trans.Y())) {
    // The translation overflowed int32_t.
    return nullptr;
  }

  auto snappedTrans = LayerIntPoint::Floor(trans);
  LayerPoint residualOffset = trans - snappedTrans;

  nsRegion opaqueRegion = aItem->GetOpaqueRegion(aDisplayListBuilder, &snap);
  wr::OpacityType opacity = opaqueRegion.Contains(paintBounds)
                                ? wr::OpacityType::Opaque
                                : wr::OpacityType::HasAlphaChannel;

  LayerIntRect dtRect, visibleRect;
  // If we think the item is opaque we round the bounds
  // to the nearest pixel instead of rounding them out. If we rounded
  // out we'd potentially introduce transparent pixels.
  //
  // Ideally we'd be able to ask an item its bounds in pixels and whether
  // they're all opaque. Unfortunately no such API exists so we currently
  // just hope that we get it right.
  if (aBuilder.GetInheritedOpacity() == 1.0f &&
      opacity == wr::OpacityType::Opaque && snap) {
    dtRect = LayerIntRect::FromUnknownRect(
        ScaleToNearestPixelsOffset(paintBounds, scale.xScale, scale.yScale,
                                   appUnitsPerDevPixel, residualOffset));

    visibleRect =
        LayerIntRect::FromUnknownRect(
            ScaleToNearestPixelsOffset(buildingRect, scale.xScale, scale.yScale,
                                       appUnitsPerDevPixel, residualOffset))
            .Intersect(dtRect);
  } else {
    dtRect = ScaleToOutsidePixelsOffset(paintBounds, scale.xScale, scale.yScale,
                                        appUnitsPerDevPixel, residualOffset);

    visibleRect =
        ScaleToOutsidePixelsOffset(buildingRect, scale.xScale, scale.yScale,
                                   appUnitsPerDevPixel, residualOffset)
            .Intersect(dtRect);
  }

  auto visibleSize = visibleRect.Size();
  // these rectangles can overflow from scaling so try to
  // catch that with IsEmpty() checks. See bug 1622126.
  if (visibleSize.IsEmpty() || dtRect.IsEmpty()) {
    return nullptr;
  }

  // Display item bounds should be unscaled
  aImageRect = visibleRect / layerScale;

  // We always paint items at 0,0 so the visibleRect that we use inside the blob
  // is needs to be adjusted by the display item bounds top left.
  visibleRect -= dtRect.TopLeft();

  nsDisplayItemGeometry* geometry = fallbackData->mGeometry.get();

  bool needPaint = true;

  MOZ_RELEASE_ASSERT(aItem->GetType() != DisplayItemType::TYPE_SVG_WRAPPER);
  if (geometry && !fallbackData->IsInvalid() &&
      aItem->GetType() != DisplayItemType::TYPE_SVG_WRAPPER && differentScale) {
    nsRect invalid;
    if (!aItem->IsInvalid(invalid)) {
      nsPoint shift = paintBounds.TopLeft() - geometry->mBounds.TopLeft();
      geometry->MoveBy(shift);

      nsRegion invalidRegion;
      aItem->ComputeInvalidationRegion(aDisplayListBuilder, geometry,
                                       &invalidRegion);

      nsRect lastBounds = fallbackData->mBounds;
      lastBounds.MoveBy(shift);

      if (lastBounds.IsEqualInterior(paintBounds) && invalidRegion.IsEmpty() &&
          aBuilder.GetInheritedOpacity() == fallbackData->mOpacity) {
        if (aItem->GetType() == DisplayItemType::TYPE_FILTER) {
          needPaint = ComputeInvalidationForDisplayList(
              aDisplayListBuilder, shift, aItem->GetChildren());
          if (!buildingRect.IsEqualInterior(fallbackData->mBuildingRect)) {
            needPaint = true;
          }
        } else {
          needPaint = false;
        }
      }
    }
  }

  if (needPaint || !fallbackData->GetImageKey()) {
    fallbackData->mGeometry =
        WrapUnique(aItem->AllocateGeometry(aDisplayListBuilder));

    gfx::SurfaceFormat format = aItem->GetType() == DisplayItemType::TYPE_MASK
                                    ? gfx::SurfaceFormat::A8
                                    : (opacity == wr::OpacityType::Opaque
                                           ? gfx::SurfaceFormat::B8G8R8X8
                                           : gfx::SurfaceFormat::B8G8R8A8);
    MOZ_ASSERT(!opaqueRegion.IsComplex());

    std::vector<RefPtr<ScaledFont>> fonts;
    bool validFonts = true;
    RefPtr<WebRenderDrawEventRecorder> recorder =
        MakeAndAddRef<WebRenderDrawEventRecorder>(
            [&](MemStream& aStream,
                std::vector<RefPtr<ScaledFont>>& aScaledFonts) {
              size_t count = aScaledFonts.size();
              aStream.write((const char*)&count, sizeof(count));
              for (auto& scaled : aScaledFonts) {
                Maybe<wr::FontInstanceKey> key =
                    mManager->WrBridge()->GetFontKeyForScaledFont(scaled,
                                                                  aResources);
                if (key.isNothing()) {
                  validFonts = false;
                  break;
                }
                BlobFont font = {key.value(), scaled};
                aStream.write((const char*)&font, sizeof(font));
              }
              fonts = std::move(aScaledFonts);
            });
    RefPtr<gfx::DrawTarget> dummyDt = gfx::Factory::CreateDrawTarget(
        gfx::BackendType::SKIA, gfx::IntSize(1, 1), format);
    RefPtr<gfx::DrawTarget> dt = gfx::Factory::CreateRecordingDrawTarget(
        recorder, dummyDt, (dtRect - dtRect.TopLeft()).ToUnknownRect());
    if (aBuilder.GetInheritedOpacity() != 1.0f) {
      dt->PushLayer(false, aBuilder.GetInheritedOpacity(), nullptr,
                    gfx::Matrix());
    }
    PaintItemByDrawTarget(aItem, dt, (dtRect / layerScale).TopLeft(),
                          /*aVisibleRect: */ dt->GetRect(), aDisplayListBuilder,
                          scale, highlight);
    if (aBuilder.GetInheritedOpacity() != 1.0f) {
      dt->PopLayer();
    }

    // the item bounds are relative to the blob origin which is
    // dtRect.TopLeft()
    recorder->FlushItem((dtRect - dtRect.TopLeft()).ToUnknownRect());
    recorder->Finish();

    if (!validFonts) {
      gfxCriticalNote << "Failed serializing fonts for blob image";
      return nullptr;
    }

    Range<uint8_t> bytes((uint8_t*)recorder->mOutputStream.mData,
                         recorder->mOutputStream.mLength);
    wr::BlobImageKey key =
        wr::BlobImageKey{mManager->WrBridge()->GetNextImageKey()};
    wr::ImageDescriptor descriptor(visibleSize.ToUnknownSize(), 0,
                                   dt->GetFormat(), opacity);
    if (!aResources.AddBlobImage(
            key, descriptor, bytes,
            ViewAs<ImagePixel>(visibleRect,
                               PixelCastJustification::LayerIsImage))) {
      return nullptr;
    }
    TakeExternalSurfaces(recorder, fallbackData->mExternalSurfaces,
                         mManager->GetRenderRootStateManager(), aResources);
    fallbackData->SetBlobImageKey(key);
    fallbackData->SetFonts(fonts);

    fallbackData->mScale = scale;
    fallbackData->mOpacity = aBuilder.GetInheritedOpacity();
    fallbackData->SetInvalid(false);
  }

  MOZ_DIAGNOSTIC_ASSERT(mManager->WrBridge()->MatchesNamespace(
                            fallbackData->GetBlobImageKey().ref()),
                        "Stale blob key for fallback!");

  aResources.SetBlobImageVisibleArea(
      fallbackData->GetBlobImageKey().value(),
      ViewAs<ImagePixel>(visibleRect, PixelCastJustification::LayerIsImage));

  // Update current bounds to fallback data
  fallbackData->mBounds = paintBounds;
  fallbackData->mBuildingRect = buildingRect;

  MOZ_ASSERT(fallbackData->GetImageKey());

  return fallbackData.forget();
}

void WebRenderMaskData::ClearImageKey() {
  if (mBlobKey) {
    mManager->AddBlobImageKeyForDiscard(mBlobKey.value());
  }
  mBlobKey.reset();
}

void WebRenderMaskData::Invalidate() {
  mMaskStyle = nsStyleImageLayers(nsStyleImageLayers::LayerType::Mask);
}

Maybe<wr::ImageMask> WebRenderCommandBuilder::BuildWrMaskImage(
    nsDisplayMasksAndClipPaths* aMaskItem, wr::DisplayListBuilder& aBuilder,
    wr::IpcResourceUpdateQueue& aResources, const StackingContextHelper& aSc,
    nsDisplayListBuilder* aDisplayListBuilder,
    const LayoutDeviceRect& aBounds) {
  RefPtr<WebRenderMaskData> maskData =
      CreateOrRecycleWebRenderUserData<WebRenderMaskData>(aMaskItem);

  if (!maskData) {
    return Nothing();
  }

  bool snap;
  nsRect bounds = aMaskItem->GetBounds(aDisplayListBuilder, &snap);

  const int32_t appUnitsPerDevPixel =
      aMaskItem->Frame()->PresContext()->AppUnitsPerDevPixel();

  MatrixScales scale = aSc.GetInheritedScale();
  MatrixScales oldScale = maskData->mScale;
  // This scale determination should probably be done using
  // ChooseScaleAndSetTransform but for now we just fake it.
  // We tolerate slight changes in scale so that we don't, for example,
  // rerasterize on MotionMark
  bool sameScale = gfx::FuzzyEqual(scale.xScale, oldScale.xScale, 1e-6f) &&
                   gfx::FuzzyEqual(scale.yScale, oldScale.yScale, 1e-6f);

  LayerIntRect itemRect =
      LayerIntRect::FromUnknownRect(bounds.ScaleToOutsidePixels(
          scale.xScale, scale.yScale, appUnitsPerDevPixel));

  LayerIntRect visibleRect =
      LayerIntRect::FromUnknownRect(
          aMaskItem->GetBuildingRect().ScaleToOutsidePixels(
              scale.xScale, scale.yScale, appUnitsPerDevPixel))
          .SafeIntersect(itemRect);

  if (visibleRect.IsEmpty()) {
    return Nothing();
  }

  LayoutDeviceToLayerScale2D layerScale(scale.xScale, scale.yScale);
  LayoutDeviceRect imageRect = LayerRect(visibleRect) / layerScale;

  nsPoint maskOffset = aMaskItem->ToReferenceFrame() - bounds.TopLeft();

  bool shouldHandleOpacity = aBuilder.GetInheritedOpacity() != 1.0f;

  nsRect dirtyRect;
  // If this mask item is being painted for the first time, some members of
  // WebRenderMaskData are still default initialized. This is intentional.
  if (aMaskItem->IsInvalid(dirtyRect) ||
      !itemRect.IsEqualInterior(maskData->mItemRect) ||
      !(aMaskItem->Frame()->StyleSVGReset()->mMask == maskData->mMaskStyle) ||
      maskOffset != maskData->mMaskOffset || !sameScale ||
      shouldHandleOpacity != maskData->mShouldHandleOpacity) {
    IntSize size = itemRect.Size().ToUnknownSize();

    if (!Factory::AllowedSurfaceSize(size)) {
      return Nothing();
    }

    std::vector<RefPtr<ScaledFont>> fonts;
    bool validFonts = true;
    RefPtr<WebRenderDrawEventRecorder> recorder =
        MakeAndAddRef<WebRenderDrawEventRecorder>(
            [&](MemStream& aStream,
                std::vector<RefPtr<ScaledFont>>& aScaledFonts) {
              size_t count = aScaledFonts.size();
              aStream.write((const char*)&count, sizeof(count));

              for (auto& scaled : aScaledFonts) {
                Maybe<wr::FontInstanceKey> key =
                    mManager->WrBridge()->GetFontKeyForScaledFont(scaled,
                                                                  aResources);
                if (key.isNothing()) {
                  validFonts = false;
                  break;
                }
                BlobFont font = {key.value(), scaled};
                aStream.write((const char*)&font, sizeof(font));
              }

              fonts = std::move(aScaledFonts);
            });

    RefPtr<DrawTarget> dummyDt = Factory::CreateDrawTarget(
        BackendType::SKIA, IntSize(1, 1), SurfaceFormat::A8);
    RefPtr<DrawTarget> dt = Factory::CreateRecordingDrawTarget(
        recorder, dummyDt, IntRect(IntPoint(0, 0), size));
    if (!dt || !dt->IsValid()) {
      gfxCriticalNote << "Failed to create drawTarget for blob mask image";
      return Nothing();
    }

    gfxContext context(dt);
    context.SetMatrix(context.CurrentMatrix()
                          .PreTranslate(-itemRect.x, -itemRect.y)
                          .PreScale(scale));

    bool maskPainted = false;
    bool maskIsComplete = aMaskItem->PaintMask(
        aDisplayListBuilder, &context, shouldHandleOpacity, &maskPainted);
    if (!maskPainted) {
      return Nothing();
    }

    // If a mask is incomplete or missing (e.g. it's display: none) the proper
    // behaviour depends on the masked frame being html or svg.
    //
    // For an HTML frame:
    //   According to css-masking spec, always create a mask surface when
    //   we have any item in maskFrame even if all of those items are
    //   non-resolvable <mask-sources> or <images> so continue with the
    //   painting code. Note that in a common case of no layer of the mask being
    //   complete or even partially complete then the mask surface will be
    //   transparent black so this results in hiding the frame.
    // For an SVG frame:
    //   SVG 1.1 say that if we fail to resolve a mask, we should draw the
    //   object unmasked so return Nothing().
    if (!maskIsComplete &&
        aMaskItem->Frame()->HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
      return Nothing();
    }

    recorder->FlushItem(IntRect(0, 0, size.width, size.height));
    recorder->Finish();

    if (!validFonts) {
      gfxCriticalNote << "Failed serializing fonts for blob mask image";
      return Nothing();
    }

    Range<uint8_t> bytes((uint8_t*)recorder->mOutputStream.mData,
                         recorder->mOutputStream.mLength);
    wr::BlobImageKey key =
        wr::BlobImageKey{mManager->WrBridge()->GetNextImageKey()};
    wr::ImageDescriptor descriptor(size, 0, dt->GetFormat(),
                                   wr::OpacityType::HasAlphaChannel);
    if (!aResources.AddBlobImage(key, descriptor, bytes,
                                 ImageIntRect(0, 0, size.width, size.height))) {
      return Nothing();
    }
    maskData->ClearImageKey();
    maskData->mBlobKey = Some(key);
    maskData->mFonts = fonts;
    TakeExternalSurfaces(recorder, maskData->mExternalSurfaces,
                         mManager->GetRenderRootStateManager(), aResources);
    if (maskIsComplete) {
      maskData->mItemRect = itemRect;
      maskData->mMaskOffset = maskOffset;
      maskData->mScale = scale;
      maskData->mMaskStyle = aMaskItem->Frame()->StyleSVGReset()->mMask;
      maskData->mShouldHandleOpacity = shouldHandleOpacity;
    }
  }

  aResources.SetBlobImageVisibleArea(
      maskData->mBlobKey.value(),
      ViewAs<ImagePixel>(visibleRect - itemRect.TopLeft(),
                         PixelCastJustification::LayerIsImage));

  MOZ_DIAGNOSTIC_ASSERT(
      mManager->WrBridge()->MatchesNamespace(maskData->mBlobKey.ref()),
      "Stale blob key for mask!");

  wr::ImageMask imageMask;
  imageMask.image = wr::AsImageKey(maskData->mBlobKey.value());
  imageMask.rect = wr::ToLayoutRect(imageRect);
  return Some(imageMask);
}

bool WebRenderCommandBuilder::PushItemAsImage(
    nsDisplayItem* aItem, wr::DisplayListBuilder& aBuilder,
    wr::IpcResourceUpdateQueue& aResources, const StackingContextHelper& aSc,
    nsDisplayListBuilder* aDisplayListBuilder) {
  LayoutDeviceRect imageRect;
  RefPtr<WebRenderFallbackData> fallbackData = GenerateFallbackData(
      aItem, aBuilder, aResources, aSc, aDisplayListBuilder, imageRect);
  if (!fallbackData) {
    return false;
  }

  wr::LayoutRect dest = wr::ToLayoutRect(imageRect);
  auto rendering = wr::ToImageRendering(aItem->Frame()->UsedImageRendering());
  mHitTestInfoManager.ProcessItemAsImage(aItem, dest, aBuilder,
                                         aDisplayListBuilder);
  aBuilder.PushImage(dest, dest, !aItem->BackfaceIsHidden(), false, rendering,
                     fallbackData->GetImageKey().value());
  return true;
}

void WebRenderCommandBuilder::RemoveUnusedAndResetWebRenderUserData() {
  mWebRenderUserDatas.RemoveIf([&](WebRenderUserData* data) {
    if (!data->IsUsed()) {
      nsIFrame* frame = data->GetFrame();

      MOZ_ASSERT(frame->HasProperty(WebRenderUserDataProperty::Key()));

      WebRenderUserDataTable* userDataTable =
          frame->GetProperty(WebRenderUserDataProperty::Key());

      MOZ_ASSERT(userDataTable->Count());

      userDataTable->Remove(
          WebRenderUserDataKey(data->GetDisplayItemKey(), data->GetType()));

      if (!userDataTable->Count()) {
        frame->RemoveProperty(WebRenderUserDataProperty::Key());
        userDataTable = nullptr;
      }

      switch (data->GetType()) {
        case WebRenderUserData::UserDataType::eCanvas:
          mLastCanvasDatas.Remove(data->AsCanvasData());
          break;
        case WebRenderUserData::UserDataType::eAnimation:
          EffectCompositor::ClearIsRunningOnCompositor(
              frame, GetDisplayItemTypeFromKey(data->GetDisplayItemKey()));
          break;
        default:
          break;
      }

      return true;
    }

    data->SetUsed(false);
    return false;
  });
}

void WebRenderCommandBuilder::ClearCachedResources() {
  RemoveUnusedAndResetWebRenderUserData();
  // UserDatas should only be in the used state during a call to
  // WebRenderCommandBuilder::BuildWebRenderCommands The should always be false
  // upon return from BuildWebRenderCommands().
  MOZ_RELEASE_ASSERT(mWebRenderUserDatas.Count() == 0);
}

WebRenderGroupData::WebRenderGroupData(
    RenderRootStateManager* aRenderRootStateManager, nsDisplayItem* aItem)
    : WebRenderGroupData(aRenderRootStateManager, aItem->GetPerFrameKey(),
                         aItem->Frame()) {}

WebRenderGroupData::WebRenderGroupData(
    RenderRootStateManager* aRenderRootStateManager, uint32_t aDisplayItemKey,
    nsIFrame* aFrame)
    : WebRenderUserData(aRenderRootStateManager, aDisplayItemKey, aFrame) {
  MOZ_COUNT_CTOR(WebRenderGroupData);
}

WebRenderGroupData::~WebRenderGroupData() {
  MOZ_COUNT_DTOR(WebRenderGroupData);
  GP("Group data destruct\n");
  mSubGroup.ClearImageKey(mManager, true);
  mFollowingGroup.ClearImageKey(mManager, true);
}

}  // namespace mozilla::layers
