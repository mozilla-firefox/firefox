/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_SampledAPZCState_h
#define mozilla_layers_SampledAPZCState_h

#include "FrameMetrics.h"
#include "mozilla/Maybe.h"
#include "mozilla/ScrollGeneration.h"
#include "mozilla/layers/CompositorScrollUpdate.h"

namespace mozilla {
namespace layers {

class SampledAPZCState {
 public:
  SampledAPZCState();
  explicit SampledAPZCState(const FrameMetrics& aMetrics);
  SampledAPZCState(const FrameMetrics& aMetrics,
                   Maybe<CompositionPayload>&& aPayload,
                   APZScrollGeneration aGeneration,
                   std::vector<CompositorScrollUpdate>&& aUpdates);

  bool operator==(const SampledAPZCState& aOther) const;
  bool operator!=(const SampledAPZCState& aOther) const;

  CSSRect GetLayoutViewport() const { return mLayoutViewport; }
  CSSPoint GetVisualScrollOffset() const { return mVisualScrollOffset; }
  CSSToParentLayerScale GetZoom() const { return mZoom; }
  Maybe<CompositionPayload> TakeScrollPayload();
  const APZScrollGeneration& Generation() const { return mGeneration; }
  const std::vector<CompositorScrollUpdate> Updates() const { return mUpdates; }

  void UpdateScrollProperties(const FrameMetrics& aMetrics);
  void UpdateScrollPropertiesWithRelativeDelta(const FrameMetrics& aMetrics,
                                               const CSSPoint& aRelativeDelta);

  void UpdateZoomProperties(const FrameMetrics& aMetrics);

  /**
   * Re-clamp mVisualScrollOffset to the scroll range specified by the provided
   * metrics. This only needs to be called if the scroll offset changes
   * outside of AsyncPanZoomController::SampleCompositedAsyncTransform().
   * It also recalculates mLayoutViewport so that it continues to enclose
   * the visual viewport. This only needs to be called if the
   * layout viewport changes outside of SampleCompositedAsyncTransform().
   */
  void ClampVisualScrollOffset(const FrameMetrics& aMetrics);

  void ZoomBy(float aScale);

 private:
  // These variables cache the layout viewport, scroll offset, and zoom stored
  // in |Metrics()| at the time this class was constructed.
  CSSRect mLayoutViewport;
  CSSPoint mVisualScrollOffset;
  CSSToParentLayerScale mZoom;
  // An optional payload that rides along with the sampled state.
  Maybe<CompositionPayload> mScrollPayload;
  APZScrollGeneration mGeneration;
  // Compositor scroll updates since the last sample.
  std::vector<CompositorScrollUpdate> mUpdates;

  void RemoveFractionalAsyncDelta();
  // A handy wrapper to call
  // FrameMetrics::KeepLayoutViewportEnclosingVisualViewport with this
  // SampledAPZCState and the given |aMetrics|.
  void KeepLayoutViewportEnclosingVisualViewport(const FrameMetrics& aMetrics);
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_SampledAPZCState_h
