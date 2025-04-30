/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SampledAPZCState.h"
#include "APZUtils.h"

namespace mozilla {
namespace layers {

SampledAPZCState::SampledAPZCState() {}

SampledAPZCState::SampledAPZCState(const FrameMetrics& aMetrics)
    : mLayoutViewport(aMetrics.GetLayoutViewport()),
      mVisualScrollOffset(aMetrics.GetVisualScrollOffset()),
      mZoom(aMetrics.GetZoom()) {
  RemoveFractionalAsyncDelta();
}

SampledAPZCState::SampledAPZCState(
    const FrameMetrics& aMetrics, Maybe<CompositionPayload>&& aPayload,
    APZScrollGeneration aGeneration,
    std::vector<CompositorScrollUpdate>&& aUpdates)
    : mLayoutViewport(aMetrics.GetLayoutViewport()),
      mVisualScrollOffset(aMetrics.GetVisualScrollOffset()),
      mZoom(aMetrics.GetZoom()),
      mScrollPayload(std::move(aPayload)),
      mGeneration(aGeneration),
      mUpdates(std::move(aUpdates)) {
  RemoveFractionalAsyncDelta();
}

bool SampledAPZCState::operator==(const SampledAPZCState& aOther) const {
  // The payload doesn't factor into equality, that just comes along for
  // the ride.
  // The compositor scroll updates also do not factor into equality.
  // We can think of those as not describing the current state, but the
  // process by which we got from the previous state to this one.
  // The FrameMetrics constructor of SampledAPZCState does not initialize
  // mUpdates, so we can't rely on them always being present.
  return mLayoutViewport.IsEqualEdges(aOther.mLayoutViewport) &&
         mVisualScrollOffset == aOther.mVisualScrollOffset &&
         mZoom == aOther.mZoom;
}

bool SampledAPZCState::operator!=(const SampledAPZCState& aOther) const {
  return !(*this == aOther);
}

Maybe<CompositionPayload> SampledAPZCState::TakeScrollPayload() {
  return std::move(mScrollPayload);
}

void SampledAPZCState::UpdateScrollProperties(const FrameMetrics& aMetrics) {
  mLayoutViewport = aMetrics.GetLayoutViewport();
  mVisualScrollOffset = aMetrics.GetVisualScrollOffset();
}

void SampledAPZCState::UpdateScrollPropertiesWithRelativeDelta(
    const FrameMetrics& aMetrics, const CSSPoint& aRelativeDelta) {
  mVisualScrollOffset += aRelativeDelta;
  KeepLayoutViewportEnclosingVisualViewport(aMetrics);
}

void SampledAPZCState::UpdateZoomProperties(const FrameMetrics& aMetrics) {
  mZoom = aMetrics.GetZoom();
}

void SampledAPZCState::ClampVisualScrollOffset(const FrameMetrics& aMetrics) {
  // Make sure that we use the local mZoom to do these calculations, because the
  // one on aMetrics might be newer.
  CSSRect scrollRange = FrameMetrics::CalculateScrollRange(
      aMetrics.GetScrollableRect(), aMetrics.GetCompositionBounds(), mZoom);
  mVisualScrollOffset = scrollRange.ClampPoint(mVisualScrollOffset);

  KeepLayoutViewportEnclosingVisualViewport(aMetrics);
}

void SampledAPZCState::ZoomBy(float aScale) { mZoom.scale *= aScale; }

void SampledAPZCState::RemoveFractionalAsyncDelta() {
  // This function is a performance hack. With non-WebRender, having small
  // fractional deltas between the layout offset and scroll offset on
  // container layers can trigger the creation of a temporary surface during
  // composition, because it produces a non-integer translation that doesn't
  // play well with layer clips. So we detect the case where the delta is
  // uselessly small (0.01 parentlayer pixels or less) and tweak the sampled
  // scroll offset to eliminate it. By doing this here at sample time rather
  // than elsewhere in the pipeline we are least likely to break assumptions
  // and invariants elsewhere in the code, since sampling effectively takes
  // a snapshot of APZ state (decoupling it from APZ assumptions) and provides
  // it as an input to the compositor (so all compositor state should be
  // internally consistent based on this input).
  // TODO(bug 1889267): Now that we use WebRender everywhere, can this hack be
  // removed?
  if (mLayoutViewport.TopLeft() == mVisualScrollOffset) {
    return;
  }
  const ParentLayerCoord EPSILON = 0.01;
  ParentLayerPoint paintedOffset = mLayoutViewport.TopLeft() * mZoom;
  ParentLayerPoint asyncOffset = mVisualScrollOffset * mZoom;
  if (FuzzyEqualsAdditive(paintedOffset.x, asyncOffset.x, EPSILON) &&
      FuzzyEqualsAdditive(paintedOffset.y, asyncOffset.y, EPSILON)) {
    mVisualScrollOffset = mLayoutViewport.TopLeft();
  }
}

void SampledAPZCState::KeepLayoutViewportEnclosingVisualViewport(
    const FrameMetrics& aMetrics) {
  FrameMetrics::KeepLayoutViewportEnclosingVisualViewport(
      CSSRect(mVisualScrollOffset,
              FrameMetrics::CalculateCompositedSizeInCssPixels(
                  aMetrics.GetCompositionBounds(), mZoom)),
      aMetrics.GetScrollableRect(), mLayoutViewport);
}

}  // namespace layers
}  // namespace mozilla
