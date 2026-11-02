/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_STACKINGCONTEXTHELPER_H
#define GFX_STACKINGCONTEXTHELPER_H

#include "Units.h"
#include "mozilla/Attributes.h"
#include "mozilla/gfx/MatrixFwd.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "mozilla/webrender/WebRenderTypes.h"

namespace mozilla {

class nsDisplayTransform;
struct ActiveScrolledRoot;

namespace layers {

/**
 * This is a helper class that pushes/pops a stacking context, and manages
 * some of the coordinate space transformations needed.
 */
class MOZ_RAII StackingContextHelper {
 public:
  StackingContextHelper(
      const StackingContextHelper& aParentSC, const ActiveScrolledRoot* aAsr,
      nsIFrame* aContainerFrame, nsDisplayItem* aContainerItem,
      wr::DisplayListBuilder& aBuilder,
      const wr::StackingContextParams& aParams,
      const Maybe<wr::WrSpatialId>& aReferenceFrame = Nothing(),
      const LayoutDeviceSize& aBoundsSize = LayoutDeviceSize());

  // This version of the constructor should only be used at the root level
  // of the tree, so that we have a StackingContextHelper to pass down into
  // the RenderLayer traversal, but don't actually want it to push a stacking
  // context on the display list builder.
  StackingContextHelper();

  // Pops the stacking context, if one was pushed during the constructor.
  ~StackingContextHelper();

  // Export the inherited scale
  gfx::MatrixScales GetInheritedScale() const { return mScale; }

  // True if the raster scale that applies to this stacking context is too
  // small for its content to cover a device pixel. GetInheritedScale() reports
  // 1.0 in that case rather than the real (near-zero) scale, so callers that
  // size a rasterization buffer from untransformed bounds must not use it:
  // those bounds are in a coordinate space that can be arbitrarily large (up
  // to nscoord saturation), and scaling them by 1.0 asks for an enormous
  // buffer. Such content is scaled out of sight, so skip it. See bug 1906769.
  bool HasDegenerateRasterScale() const { return mRasterScaleIsDegenerate; }

  const gfx::Matrix& GetInheritedTransform() const {
    return mInheritedTransform;
  }

  const gfx::Matrix& GetSnappingSurfaceTransform() const {
    return mSnappingSurfaceTransform;
  }

  nsDisplayTransform* GetDeferredTransformItem() const;
  Maybe<gfx::Matrix4x4> GetDeferredTransformMatrix() const;
  // Functions for temporarily clearing and restoring the deferred
  // transform item during WebRender display list building. These are
  // used to ensure deferred transforms are not applied in duplicate
  // to nested nodes in the WebRenderScrollData tree.
  void ClearDeferredTransformItem() const;
  void RestoreDeferredTransformItem(nsDisplayTransform* aItem) const;

  bool AffectsClipPositioning() const { return mAffectsClipPositioning; }
  Maybe<wr::WrSpatialId> ReferenceFrameId() const { return mReferenceFrameId; }

 private:
  wr::DisplayListBuilder* mBuilder;
  gfx::MatrixScales mScale;
  gfx::Matrix mInheritedTransform;

  // The "snapping surface" defines the space that we want to snap in.
  // You can think of it as the nearest physical surface.
  // Animated transforms create a new snapping surface, so that changes to their
  // transform don't affect the snapping of their contents. Non-animated
  // transforms do *not* create a new snapping surface, so that for example the
  // existence of a non-animated identity transform does not affect snapping.
  gfx::Matrix mSnappingSurfaceTransform;
  bool mRasterScaleIsDegenerate;
  bool mAffectsClipPositioning;
  Maybe<wr::WrSpatialId> mReferenceFrameId;
  Maybe<wr::SpaceAndClipChainHelper> mSpaceAndClipChainHelper;

  // The deferred transform item is used when building the WebRenderScrollData
  // structure. The backstory is that APZ needs to know about transforms that
  // apply to the different APZC instances. Prior to bug 1423370, we would do
  // this by creating a new WebRenderLayerScrollData for each nsDisplayTransform
  // item we encountered. However, this was unnecessarily expensive because it
  // turned out a lot of nsDisplayTransform items didn't have new ASRs defined
  // as descendants, so we'd create the WebRenderLayerScrollData and send it
  // over to APZ even though the transform information was not needed in that
  // case.
  //
  // In bug 1423370 and friends, this was optimized by "deferring" a
  // nsDisplayTransform item when we encountered it during display list
  // traversal. If we found a descendant of that transform item that had a
  // new ASR or otherwise was "relevant to APZ", we would then pluck the
  // transform matrix off the deferred item and put it on the
  // WebRenderLayerScrollData instance created for that APZ-relevant descendant.
  //
  // One complication with this is if there are multiple nsDisplayTransform
  // items in the ancestor chain for the APZ-relevant item. As we traverse the
  // display list, we will defer the outermost nsDisplayTransform item, and when
  // we encounter the next one we will need to merge it with the already-
  // deferred one somehow. What we do in this case is have
  // mDeferredTransformItem always point to the "innermost" deferred transform
  // item (i.e. the closest ancestor nsDisplayTransform item of the item that
  // created this StackingContextHelper). And then we use
  // mDeferredAncestorTransform to store the product of all the other transforms
  // that were deferred. Note that this means we only need to look at
  // mDeferredAncestorTransform if mDeferredTransformItem is set. Note that we
  // can only do this if the nsDisplayTransform items share the same ASR. If we
  // are processing an nsDisplayTransform item with a different ASR than the
  // previously-deferred item, we assume that the previously-deferred transform
  // will get sent to APZ as part of a separate WebRenderLayerScrollData item,
  // and so we don't need to bother with any merging. (The merging probably
  // wouldn't even make sense because the coordinate spaces might be different
  // in the face of async scrolling). This behaviour of forcing a
  // WebRenderLayerScrollData item to be generated when the ASR changes is
  // implemented in
  // WebRenderCommandBuilder::CreateWebRenderCommandsFromDisplayList.
  mutable nsDisplayTransform* mDeferredTransformItem;
  Maybe<gfx::Matrix4x4> mDeferredAncestorTransform;
};

}  // namespace layers
}  // namespace mozilla

#endif /* GFX_STACKINGCONTEXTHELPER_H */
