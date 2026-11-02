/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/StackingContextHelper.h"

#include "ActiveLayerTracker.h"
#include "UnitTransforms.h"
#include "mozilla/PresShell.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/gfx/Matrix.h"
#include "mozilla/gfx/Point.h"
#include "nsDisplayList.h"
#include "nsLayoutUtils.h"

namespace mozilla {
namespace layers {
using namespace gfx;

StackingContextHelper::StackingContextHelper()
    : mBuilder(nullptr),
      mScale(1.0f, 1.0f),
      mRasterScaleIsDegenerate(false),
      mAffectsClipPositioning(false),
      mDeferredTransformItem(nullptr) {}

static nsSize ComputeDesiredDisplaySizeForAnimation(nsIFrame* aContainerFrame) {
  // Use the size of the nearest widget as the maximum size.  This
  // is important since it might be a popup that is bigger than the
  // pres context's size.
  nsPresContext* presContext = aContainerFrame->PresContext();
  nsIWidget* widget = aContainerFrame->GetNearestWidget();
  if (widget) {
    return LayoutDevicePixel::ToAppUnits(widget->GetClientSize(),
                                         presContext->AppUnitsPerDevPixel());
  }

  return presContext->GetVisibleArea().Size();
}

/* static */
MatrixScales ChooseScale(nsIFrame* aContainerFrame,
                         nsDisplayItem* aContainerItem,
                         const nsRect& aVisibleRect, float aXScale,
                         float aYScale, const Matrix& aTransform2d,
                         bool aCanDraw2D, bool* aOutDegenerate) {
  MatrixScales scale;
  *aOutDegenerate = false;
  // XXX Should we do something for 3D transforms?
  if (aCanDraw2D && !aContainerFrame->Combines3DTransformWithAncestors() &&
      !aContainerFrame->HasPerspective()) {
    // If the container's transform is animated off main thread, fix a suitable
    // scale size for animation
    if (aContainerItem &&
        aContainerItem->GetType() == DisplayItemType::TYPE_TRANSFORM &&
        // FIXME: What we need is only transform, rotate, and scale, not
        // translate, so it's be better to use a property set, instead of
        // display item type here.
        EffectCompositor::HasAnimationsForCompositor(
            aContainerFrame, DisplayItemType::TYPE_TRANSFORM)) {
      nsSize displaySize =
          ComputeDesiredDisplaySizeForAnimation(aContainerFrame);
      // compute scale using the animation on the container, taking ancestors in
      // to account
      nsSize scaledVisibleSize = nsSize(aVisibleRect.Width() * aXScale,
                                        aVisibleRect.Height() * aYScale);
      scale = nsLayoutUtils::ComputeSuitableScaleForAnimation(
          aContainerFrame, scaledVisibleSize, displaySize);
      // multiply by the scale inherited from ancestors--we use a uniform
      // scale factor to prevent blurring when the layer is rotated.
      float incomingScale = std::max(aXScale, aYScale);
      scale = scale * ScaleFactor<UnknownUnits, UnknownUnits>(incomingScale);
    } else {
      // Scale factors are normalized to a power of 2 to reduce the number of
      // resolution changes
      scale = (aTransform2d * gfx::Matrix::Scaling(aXScale, aYScale))
                  .ScaleFactors();
      // For frames with a changing scale transform round scale factors up to
      // nearest power-of-2 boundary so that we don't keep having to redraw
      // the content as it scales up and down. Rounding up to nearest
      // power-of-2 boundary ensures we never scale up, only down --- avoiding
      // jaggies. It also ensures we never scale down by more than a factor of
      // 2, avoiding bad downscaling quality.
      Matrix frameTransform;
      if (ActiveLayerTracker::IsScaleSubjectToAnimation(aContainerFrame)) {
        scale.xScale = gfxUtils::ClampToScaleFactor(scale.xScale);
        scale.yScale = gfxUtils::ClampToScaleFactor(scale.yScale);

        // Limit animated scale factors to not grow excessively beyond the
        // display size.
        nsSize maxScale(4, 4);
        if (!aVisibleRect.IsEmpty()) {
          nsSize displaySize =
              ComputeDesiredDisplaySizeForAnimation(aContainerFrame);
          maxScale = Max(maxScale, displaySize / aVisibleRect.Size());
        }
        if (scale.xScale > maxScale.width) {
          scale.xScale = gfxUtils::ClampToScaleFactor(maxScale.width, true);
        }
        if (scale.yScale > maxScale.height) {
          scale.yScale = gfxUtils::ClampToScaleFactor(maxScale.height, true);
        }
      } else {
        // XXX Do we need to move nearly-integer values to integers here?
      }
    }
    // If the scale factors are too small, just use 1.0. The content is being
    // scaled out of sight anyway. Report this to the caller: 1.0 is only a
    // placeholder to keep the rest of the pipeline well-behaved, and anything
    // that sizes a buffer from untransformed bounds must skip this content
    // rather than take the placeholder at face value (bug 1906769).
    if (fabs(scale.xScale) < 1e-8 || fabs(scale.yScale) < 1e-8) {
      scale = MatrixScales(1.0, 1.0);
      *aOutDegenerate = true;
    }
  } else {
    scale = MatrixScales(1.0, 1.0);
  }

  // Prevent the scale from getting too large, to avoid excessive memory
  // allocation. Usually memory allocation is limited by the visible region,
  // which should be restricted to the display port. But at very large scales
  // the visible region itself can become excessive due to rounding errors.
  // Clamping the scale here prevents that.
  return MatrixScales(std::min(scale.xScale, 32768.0f),
                      std::min(scale.yScale, 32768.0f));
}

StackingContextHelper::StackingContextHelper(
    const StackingContextHelper& aParentSC, const ActiveScrolledRoot* aAsr,
    nsIFrame* aContainerFrame, nsDisplayItem* aContainerItem,
    wr::DisplayListBuilder& aBuilder, const wr::StackingContextParams& aParams,
    const Maybe<wr::WrSpatialId>& aReferenceFrame,
    const LayoutDeviceSize& aBoundsSize)
    : mBuilder(&aBuilder),
      mScale(1.0f, 1.0f),
      mRasterScaleIsDegenerate(false),
      mDeferredTransformItem(aParams.mDeferredTransformItem) {
  MOZ_ASSERT(!aContainerItem || aContainerItem->CreatesStackingContextHelper());

  // Compute scale for fallback rendering. We don't try to guess a scale for 3d
  // transformed items

  if (aParams.mBoundTransform) {
    gfx::Matrix transform2d;
    bool canDraw2D = aParams.mBoundTransform->CanDraw2D(&transform2d);
    if (canDraw2D && !aContainerFrame->Combines3DTransformWithAncestors()) {
      mInheritedTransform = transform2d * aParentSC.mInheritedTransform;

      int32_t apd = aContainerFrame->PresContext()->AppUnitsPerDevPixel();
      nsRect r = LayoutDevicePixel::ToAppUnits(
          LayoutDeviceRect(LayoutDevicePoint(), aBoundsSize), apd);
      bool degenerate = false;
      mScale = ChooseScale(aContainerFrame, aContainerItem, r,
                           aParentSC.mScale.xScale, aParentSC.mScale.yScale,
                           transform2d,
                           /* aCanDraw2D = */ true, &degenerate);
      // ChooseScale composes with the parent's (possibly placeholder) scale,
      // so a degenerate ancestor stays degenerate here.
      mRasterScaleIsDegenerate =
          degenerate || aParentSC.mRasterScaleIsDegenerate;
    } else {
      // Deliberately discards the inherited scale, so the ancestor's degenerate
      // state does not carry over either.
      mScale = gfx::MatrixScales(1.0f, 1.0f);
      mInheritedTransform = gfx::Matrix::Scaling(1.f, 1.f);
    }

    if (aParams.mAnimated) {
      mSnappingSurfaceTransform = gfx::Matrix::Scaling(mScale);
    } else {
      mSnappingSurfaceTransform =
          transform2d * aParentSC.mSnappingSurfaceTransform;
    }

  } else if (aContainerItem &&
             aContainerItem->GetType() == DisplayItemType::TYPE_ASYNC_ZOOM &&
             aContainerItem->Frame()) {
    float resolution = aContainerItem->Frame()->PresShell()->GetResolution();
    gfx::Matrix transform = gfx::Matrix::Scaling(resolution, resolution);

    mInheritedTransform = transform * aParentSC.mInheritedTransform;
    mScale =
        ScaleFactor<UnknownUnits, UnknownUnits>(resolution) * aParentSC.mScale;
    mRasterScaleIsDegenerate = aParentSC.mRasterScaleIsDegenerate;

    MOZ_ASSERT(!aParams.mAnimated);
    mSnappingSurfaceTransform = transform * aParentSC.mSnappingSurfaceTransform;

  } else if (!aAsr && !aContainerFrame && !aContainerItem &&
             aParams.mRootReferenceFrame) {
    // this is the root stacking context helper
    Scale2D resolution;

    // If we are in a remote browser, then apply scaling from ancestor browsers
    if (mozilla::dom::BrowserChild* browserChild =
            mozilla::dom::BrowserChild::GetFrom(
                aParams.mRootReferenceFrame->PresShell())) {
      resolution = browserChild->GetEffectsInfo().mRasterScale;
    }

    gfx::Matrix transform =
        gfx::Matrix::Scaling(resolution.xScale, resolution.yScale);

    mInheritedTransform = transform * aParentSC.mInheritedTransform;
    mScale = aParentSC.mScale * resolution;
    mRasterScaleIsDegenerate = aParentSC.mRasterScaleIsDegenerate;

    MOZ_ASSERT(!aParams.mAnimated);
    mSnappingSurfaceTransform = transform * aParentSC.mSnappingSurfaceTransform;

  } else {
    mInheritedTransform = aParentSC.mInheritedTransform;
    mScale = aParentSC.mScale;
    mRasterScaleIsDegenerate = aParentSC.mRasterScaleIsDegenerate;
  }

  // Content is always rasterized in screen (device) space. We used to rasterize
  // in local space for animated transforms, but that skipped WebRender's
  // device-pixel snapping and left text blurry at fractional device offsets
  // (Bug 2051166); device raster space stays sharp and reuses cached glyphs.
  auto rasterSpace = wr::RasterSpace::Screen();

  MOZ_ASSERT(!aParams.clip.IsNone());
  mBuilder->PushStackingContext(aParams, rasterSpace, aReferenceFrame);
  mReferenceFrameId = aReferenceFrame;

  if (mReferenceFrameId) {
    mSpaceAndClipChainHelper.emplace(aBuilder, mReferenceFrameId.ref());
  }

  mAffectsClipPositioning = mReferenceFrameId.isSome();

  // If the parent stacking context has a deferred transform item, inherit it
  // into this stacking context, as long as the ASR hasn't changed. Refer to
  // the comments on StackingContextHelper::mDeferredTransformItem for an
  // explanation of what goes in these fields.
  if (aParentSC.mDeferredTransformItem &&
      aAsr == aParentSC.mDeferredTransformItem->GetActiveScrolledRoot()) {
    if (mDeferredTransformItem) {
      // If we are deferring another transform, put the combined transform from
      // all the ancestor deferred items into mDeferredAncestorTransform
      mDeferredAncestorTransform = aParentSC.GetDeferredTransformMatrix();
    } else {
      // We are not deferring another transform, so we can just inherit the
      // parent stacking context's deferred data without any modification.
      mDeferredTransformItem = aParentSC.mDeferredTransformItem;
      mDeferredAncestorTransform = aParentSC.mDeferredAncestorTransform;
    }
  }
}

StackingContextHelper::~StackingContextHelper() {
  if (mBuilder) {
    mSpaceAndClipChainHelper.reset();
    mBuilder->PopStackingContext();
  }
}

nsDisplayTransform* StackingContextHelper::GetDeferredTransformItem() const {
  return mDeferredTransformItem;
}

Maybe<gfx::Matrix4x4> StackingContextHelper::GetDeferredTransformMatrix()
    const {
  if (mDeferredTransformItem) {
    // See the comments on StackingContextHelper::mDeferredTransformItem for
    // an explanation of what's stored in mDeferredTransformItem and
    // mDeferredAncestorTransform. Here we need to return the combined transform
    // transform from all the deferred ancestors, including
    // mDeferredTransformItem.
    gfx::Matrix4x4 result = mDeferredTransformItem->GetTransform().GetMatrix();
    if (mDeferredAncestorTransform) {
      result = result * *mDeferredAncestorTransform;
    }
    return Some(result);
  } else {
    return Nothing();
  }
}

void StackingContextHelper::ClearDeferredTransformItem() const {
  mDeferredTransformItem = nullptr;
}

void StackingContextHelper::RestoreDeferredTransformItem(
    nsDisplayTransform* aItem) const {
  mDeferredTransformItem = aItem;
}

}  // namespace layers
}  // namespace mozilla
