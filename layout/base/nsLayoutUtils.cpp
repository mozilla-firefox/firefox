/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsLayoutUtils.h"

#include <algorithm>
#include <limits>

#include "ActiveLayerTracker.h"
#include "DisplayItemClip.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxDrawable.h"
#include "gfxEnv.h"
#include "gfxMatrix.h"
#include "gfxPlatform.h"
#include "gfxRect.h"
#include "gfxTypes.h"
#include "gfxUtils.h"
#include "ImageContainer.h"
#include "ImageOps.h"
#include "ImageRegion.h"
#include "imgIContainer.h"
#include "imgIRequest.h"
#include "LayoutLogging.h"
#include "MobileViewportManager.h"
#include "mozilla/AccessibleCaretEventHub.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/Baseline.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/DisplayPortUtils.h"
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/dom/AnonymousContent.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/CanvasUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/DOMRect.h"
#include "mozilla/dom/DOMStringList.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLBodyElement.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/dom/HTMLImageElement.h"
#include "mozilla/dom/HTMLMediaElementBinding.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/InspectorFontFace.h"
#include "mozilla/dom/ImageBitmap.h"
#include "mozilla/dom/InteractiveWidget.h"
#include "mozilla/dom/KeyframeEffect.h"
#include "mozilla/dom/SVGViewportElement.h"
#include "mozilla/dom/UIEvent.h"
#include "mozilla/dom/VideoFrame.h"
#include "mozilla/dom/VideoFrameBinding.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "mozilla/EffectCompositor.h"
#include "mozilla/EffectSet.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gfx/PathHelpers.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/layers/APZCCallbackHelper.h"
#include "mozilla/layers/APZPublicUtils.h"  // for apz::CalculatePendingDisplayPort
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/PAPZ.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/Likely.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/Maybe.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/PerfStats.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/ScrollOrigin.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/ServoStyleSetInlines.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_font.h"
#include "mozilla/StaticPrefs_general.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_image.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/StyleAnimationValue.h"
#include "mozilla/SVGImageContext.h"
#include "mozilla/SVGIntegrationUtils.h"
#include "mozilla/SVGTextFrame.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/glean/LayoutMetrics.h"
#include "mozilla/ToString.h"
#include "mozilla/Unused.h"
#include "mozilla/ViewportFrame.h"
#include "mozilla/ViewportUtils.h"
#include "mozilla/WheelHandlingHelper.h"  // for WheelHandlingUtils
#include "nsAnimationManager.h"
#include "nsAtom.h"
#include "nsBidiPresUtils.h"
#include "nsBlockFrame.h"
#include "nsCanvasFrame.h"
#include "nsCaret.h"
#include "nsCharTraits.h"
#include "nsCOMPtr.h"
#include "nsComputedDOMStyle.h"
#include "nsContentUtils.h"
#include "nsCSSAnonBoxes.h"
#include "nsCSSColorUtils.h"
#include "nsCSSFrameConstructor.h"
#include "nsCSSProps.h"
#include "nsCSSPseudoElements.h"
#include "nsCSSRendering.h"
#include "nsDisplayList.h"
#include "nsFieldSetFrame.h"
#include "nsFlexContainerFrame.h"
#include "nsFontInflationData.h"
#include "nsFontMetrics.h"
#include "nsFrameList.h"
#include "nsFrameSelection.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsICanvasRenderingContextInternal.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsIDocShell.h"
#include "nsIDocumentViewer.h"
#include "nsIFrameInlines.h"
#include "nsIImageLoadingContent.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIWidget.h"
#include "nsListControlFrame.h"
#include "nsMenuPopupFrame.h"
#include "nsPIDOMWindow.h"
#include "nsPlaceholderFrame.h"
#include "nsPresContext.h"
#include "nsPresContextInlines.h"
#include "nsRefreshDriver.h"
#include "nsRegion.h"
#include "nsStyleConsts.h"
#include "nsStyleStructInlines.h"
#include "nsStyleTransformMatrix.h"
#include "nsSubDocumentFrame.h"
#include "nsTableWrapperFrame.h"
#include "nsTArray.h"
#include "nsTextFragment.h"
#include "nsTextFrame.h"
#include "nsTHashMap.h"
#include "nsTransitionManager.h"
#include "nsView.h"
#include "nsViewManager.h"
#include "prenv.h"
#include "RegionBuilder.h"
#include "RetainedDisplayListBuilder.h"
#include "TextDrawTarget.h"
#include "UnitTransforms.h"
#include "ViewportFrame.h"

#include "nsXULPopupManager.h"

// Make sure getpid() works.
#ifdef XP_WIN
#  include <process.h>
#  define getpid _getpid
#else
#  include <unistd.h>
#endif

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::image;
using namespace mozilla::layers;
using namespace mozilla::layout;
using namespace mozilla::gfx;
using mozilla::dom::HTMLMediaElement_Binding::HAVE_METADATA;
using mozilla::dom::HTMLMediaElement_Binding::HAVE_NOTHING;

typedef ScrollableLayerGuid::ViewID ViewID;
typedef nsStyleTransformMatrix::TransformReferenceBox TransformReferenceBox;

static ViewID sScrollIdCounter = ScrollableLayerGuid::START_SCROLL_ID;

typedef nsTHashMap<nsUint64HashKey, nsIContent*> ContentMap;
static StaticAutoPtr<ContentMap> sContentMap;

static ContentMap& GetContentMap() {
  if (!sContentMap) {
    sContentMap = new ContentMap();
  }
  return *sContentMap;
}

template <typename TestType>
static bool HasMatchingAnimations(EffectSet& aEffects, TestType&& aTest) {
  for (KeyframeEffect* effect : aEffects) {
    if (!effect->GetAnimation() || !effect->GetAnimation()->IsRelevant()) {
      continue;
    }

    if (aTest(*effect, aEffects)) {
      return true;
    }
  }

  return false;
}

template <typename TestType>
static bool HasMatchingAnimations(const nsIFrame* aFrame,
                                  const nsCSSPropertyIDSet& aPropertySet,
                                  TestType&& aTest) {
  MOZ_ASSERT(aFrame);

  if (!aFrame->MayHaveOpacityAnimation() &&
      aPropertySet.IsSubsetOf(nsCSSPropertyIDSet::OpacityProperties())) {
    return false;
  }

  if (!aFrame->MayHaveTransformAnimation() &&
      aPropertySet.IsSubsetOf(nsCSSPropertyIDSet::TransformLikeProperties())) {
    return false;
  }

  EffectSet* effectSet = EffectSet::GetForFrame(aFrame, aPropertySet);
  if (!effectSet) {
    return false;
  }

  return HasMatchingAnimations(*effectSet, aTest);
}

/* static */
bool nsLayoutUtils::HasAnimationOfPropertySet(
    const nsIFrame* aFrame, const nsCSSPropertyIDSet& aPropertySet) {
  return HasMatchingAnimations(
      aFrame, aPropertySet,
      [&aPropertySet](KeyframeEffect& aEffect, const EffectSet&) {
        return aEffect.HasAnimationOfPropertySet(aPropertySet);
      });
}

/* static */
bool nsLayoutUtils::HasAnimationOfPropertySet(
    const nsIFrame* aFrame, const nsCSSPropertyIDSet& aPropertySet,
    EffectSet* aEffectSet) {
  MOZ_ASSERT(
      !aEffectSet || EffectSet::GetForFrame(aFrame, aPropertySet) == aEffectSet,
      "The EffectSet, if supplied, should match what we would otherwise fetch");

  if (!aEffectSet) {
    return nsLayoutUtils::HasAnimationOfPropertySet(aFrame, aPropertySet);
  }

  if (!aEffectSet->MayHaveTransformAnimation() &&
      aPropertySet.IsSubsetOf(nsCSSPropertyIDSet::TransformLikeProperties())) {
    return false;
  }

  if (!aEffectSet->MayHaveOpacityAnimation() &&
      aPropertySet.IsSubsetOf(nsCSSPropertyIDSet::OpacityProperties())) {
    return false;
  }

  return HasMatchingAnimations(
      *aEffectSet,
      [&aPropertySet](KeyframeEffect& aEffect, const EffectSet& aEffectSet) {
        return aEffect.HasAnimationOfPropertySet(aPropertySet);
      });
}

/* static */
bool nsLayoutUtils::HasAnimationOfTransformAndMotionPath(
    const nsIFrame* aFrame) {
  auto returnValue = [&]() -> bool {
    return nsLayoutUtils::HasAnimationOfPropertySet(
               aFrame,
               nsCSSPropertyIDSet{eCSSProperty_transform,
                                  eCSSProperty_translate, eCSSProperty_rotate,
                                  eCSSProperty_scale,
                                  eCSSProperty_offset_path}) ||
           (!aFrame->StyleDisplay()->mOffsetPath.IsNone() &&
            nsLayoutUtils::HasAnimationOfPropertySet(
                aFrame, nsCSSPropertyIDSet::MotionPathProperties()));
  };

  if (!aFrame->MayHaveTransformAnimation()) {
    MOZ_ASSERT(!returnValue());
    return false;
  }
  return returnValue();
}

/* static */
bool nsLayoutUtils::HasEffectiveAnimation(
    const nsIFrame* aFrame, const nsCSSPropertyIDSet& aPropertySet) {
  return HasMatchingAnimations(
      aFrame, aPropertySet,
      [&aPropertySet](KeyframeEffect& aEffect, const EffectSet& aEffectSet) {
        return aEffect.HasEffectiveAnimationOfPropertySet(aPropertySet,
                                                          aEffectSet);
      });
}

/* static */
nsCSSPropertyIDSet nsLayoutUtils::GetAnimationPropertiesForCompositor(
    const nsIFrame* aStyleFrame) {
  nsCSSPropertyIDSet properties;

  // We fetch the effects for the style frame here since this method is called
  // by RestyleManager::AddLayerChangesForAnimation which takes care to apply
  // the relevant hints to the primary frame as needed.
  EffectSet* effects = EffectSet::GetForStyleFrame(aStyleFrame);
  if (!effects) {
    return properties;
  }

  AnimationPerformanceWarning::Type warning;
  if (!EffectCompositor::AllowCompositorAnimationsOnFrame(aStyleFrame,
                                                          warning)) {
    return properties;
  }

  for (const KeyframeEffect* effect : *effects) {
    properties |= effect->GetPropertiesForCompositor(*effects, aStyleFrame);
  }

  // If properties only have motion-path properties, we have to make sure they
  // have effects. i.e. offset-path is not none or we have offset-path
  // animations.
  if (properties.IsSubsetOf(nsCSSPropertyIDSet::MotionPathProperties()) &&
      !properties.HasProperty(eCSSProperty_offset_path) &&
      aStyleFrame->StyleDisplay()->mOffsetPath.IsNone()) {
    properties.Empty();
  }

  return properties;
}

static float GetSuitableScale(float aMaxScale, float aMinScale,
                              nscoord aVisibleDimension,
                              nscoord aDisplayDimension) {
  float displayVisibleRatio =
      float(aDisplayDimension) / float(aVisibleDimension);
  // We want to rasterize based on the largest scale used during the
  // transform animation, unless that would make us rasterize something
  // larger than the screen.  But we never want to go smaller than the
  // minimum scale over the animation.
  if (FuzzyEqualsMultiplicative(displayVisibleRatio, aMaxScale, .01f)) {
    // Using aMaxScale may make us rasterize something a fraction larger than
    // the screen. However, if aMaxScale happens to be the final scale of a
    // transform animation it is better to use aMaxScale so that for the
    // fraction of a second before we delayerize the composited texture it has
    // a better chance of being pixel aligned and composited without resampling
    // (avoiding visually clunky delayerization).
    return aMaxScale;
  }
  return std::clamp(displayVisibleRatio, aMinScale, aMaxScale);
}

// The first value in this pair is the min scale, and the second one is the max
// scale.
using MinAndMaxScale = std::pair<MatrixScales, MatrixScales>;

static inline void UpdateMinMaxScale(const nsIFrame* aFrame,
                                     const AnimationValue& aValue,
                                     MinAndMaxScale& aMinAndMaxScale) {
  MatrixScales size = aValue.GetScaleValue(aFrame);
  MatrixScales& minScale = aMinAndMaxScale.first;
  MatrixScales& maxScale = aMinAndMaxScale.second;

  minScale = Min(minScale, size);
  maxScale = Max(maxScale, size);
}

// The final transform matrix is calculated by merging the final results of each
// transform-like properties, so do the scale factors. In other words, the
// potential min/max scales could be gotten by multiplying the max/min scales of
// each properties.
//
// For example, there is an animation:
//   from { "transform: scale(1, 1)", "scale: 3, 3" };
//   to   { "transform: scale(2, 2)", "scale: 1, 1" };
//
// the min scale is (1, 1) * (1, 1) = (1, 1), and
// The max scale is (2, 2) * (3, 3) = (6, 6).
// This means we multiply the min/max scale factor of transform property and the
// min/max scale factor of scale property to get the final max/min scale factor.
static Array<MinAndMaxScale, 2> GetMinAndMaxScaleForAnimationProperty(
    const nsIFrame* aFrame,
    const nsTArray<RefPtr<dom::Animation>>& aAnimations) {
  // We use a fixed array to store the min/max scales for each property.
  // The first element in the array is for eCSSProperty_transform, and the
  // second one is for eCSSProperty_scale.
  const MinAndMaxScale defaultValue =
      std::make_pair(MatrixScales(std::numeric_limits<float>::max(),
                                  std::numeric_limits<float>::max()),
                     MatrixScales(std::numeric_limits<float>::min(),
                                  std::numeric_limits<float>::min()));
  Array<MinAndMaxScale, 2> minAndMaxScales(defaultValue, defaultValue);

  for (dom::Animation* anim : aAnimations) {
    // This method is only expected to be passed animations that are running on
    // the compositor and we only pass playing animations to the compositor,
    // which are, by definition, "relevant" animations (animations that are
    // not yet finished or which are filling forwards).
    MOZ_ASSERT(anim->IsRelevant());

    const dom::KeyframeEffect* effect =
        anim->GetEffect() ? anim->GetEffect()->AsKeyframeEffect() : nullptr;
    MOZ_ASSERT(effect, "A playing animation should have a keyframe effect");
    for (const AnimationProperty& prop : effect->Properties()) {
      if (prop.mProperty.mID != eCSSProperty_transform &&
          prop.mProperty.mID != eCSSProperty_scale) {
        continue;
      }

      // 0: eCSSProperty_transform.
      // 1: eCSSProperty_scale.
      MinAndMaxScale& scales =
          minAndMaxScales[prop.mProperty.mID == eCSSProperty_transform ? 0 : 1];

      // We need to factor in the scale of the base style if the base style
      // will be used on the compositor.
      const AnimationValue& baseStyle = effect->BaseStyle(prop.mProperty);
      if (!baseStyle.IsNull()) {
        UpdateMinMaxScale(aFrame, baseStyle, scales);
      }

      for (const AnimationPropertySegment& segment : prop.mSegments) {
        // In case of add or accumulate composite, StyleAnimationValue does
        // not have a valid value.
        if (segment.HasReplaceableFromValue()) {
          UpdateMinMaxScale(aFrame, segment.mFromValue, scales);
        }

        if (segment.HasReplaceableToValue()) {
          UpdateMinMaxScale(aFrame, segment.mToValue, scales);
        }
      }
    }
  }

  return minAndMaxScales;
}

MatrixScales nsLayoutUtils::ComputeSuitableScaleForAnimation(
    const nsIFrame* aFrame, const nsSize& aVisibleSize,
    const nsSize& aDisplaySize) {
  const nsTArray<RefPtr<dom::Animation>> compositorAnimations =
      EffectCompositor::GetAnimationsForCompositor(
          aFrame,
          nsCSSPropertyIDSet{eCSSProperty_transform, eCSSProperty_scale});

  if (compositorAnimations.IsEmpty()) {
    return MatrixScales();
  }

  const Array<MinAndMaxScale, 2> minAndMaxScales =
      GetMinAndMaxScaleForAnimationProperty(aFrame, compositorAnimations);

  // This might cause an issue if users use std::numeric_limits<float>::min()
  // (or max()) as the scale value. However, in this case, we may render an
  // extreme small (or large) element, so this may not be a problem. If so,
  // please fix this.
  MatrixScales maxScale(std::numeric_limits<float>::min(),
                        std::numeric_limits<float>::min());
  MatrixScales minScale(std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max());

  auto isUnset = [](const MatrixScales& aMax, const MatrixScales& aMin) {
    return aMax.xScale == std::numeric_limits<float>::min() &&
           aMax.yScale == std::numeric_limits<float>::min() &&
           aMin.xScale == std::numeric_limits<float>::max() &&
           aMin.yScale == std::numeric_limits<float>::max();
  };

  // Iterate the slots to get the final scale value.
  for (const auto& pair : minAndMaxScales) {
    const MatrixScales& currMinScale = pair.first;
    const MatrixScales& currMaxScale = pair.second;

    if (isUnset(currMaxScale, currMinScale)) {
      // We don't have this animation property, so skip.
      continue;
    }

    if (isUnset(maxScale, minScale)) {
      // Initialize maxScale and minScale.
      maxScale = currMaxScale;
      minScale = currMinScale;
    } else {
      // The scale factors of each transform-like property should be multiplied
      // by others because we merge their sampled values as a final matrix by
      // matrix multiplication, so here we multiply the scale factors by the
      // previous one to get the possible max and min scale factors.
      maxScale = maxScale * currMaxScale;
      minScale = minScale * currMinScale;
    }
  }

  if (isUnset(maxScale, minScale)) {
    // We didn't encounter any transform-like property.
    return MatrixScales();
  }

  return MatrixScales(
      GetSuitableScale(maxScale.xScale, minScale.xScale, aVisibleSize.width,
                       aDisplaySize.width),
      GetSuitableScale(maxScale.yScale, minScale.yScale, aVisibleSize.height,
                       aDisplaySize.height));
}

bool nsLayoutUtils::AreAsyncAnimationsEnabled() {
  return StaticPrefs::layers_offmainthreadcomposition_async_animations() &&
         gfxPlatform::OffMainThreadCompositingEnabled();
}

bool nsLayoutUtils::AreRetainedDisplayListsEnabled() {
#ifdef MOZ_WIDGET_ANDROID
  return StaticPrefs::layout_display_list_retain();
#else
  if (XRE_IsContentProcess()) {
    return StaticPrefs::layout_display_list_retain();
  }

  if (XRE_IsE10sParentProcess()) {
    return StaticPrefs::layout_display_list_retain_chrome();
  }

  // Retained display lists require e10s.
  return false;
#endif
}

bool nsLayoutUtils::DisplayRootHasRetainedDisplayListBuilder(nsIFrame* aFrame) {
  return GetRetainedDisplayListBuilder(aFrame) != nullptr;
}

RetainedDisplayListBuilder* nsLayoutUtils::GetRetainedDisplayListBuilder(
    nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame);
  MOZ_ASSERT(aFrame->PresShell());

  // Use the pres shell root frame to get the display root frame. This skips
  // the early exit in |nsLayoutUtils::GetDisplayRootFrame()| for popup frames.
  const nsIFrame* rootFrame = aFrame->PresShell()->GetRootFrame();
  if (!rootFrame) {
    return nullptr;
  }

  const nsIFrame* displayRootFrame = GetDisplayRootFrame(rootFrame);
  MOZ_ASSERT(displayRootFrame);

  return displayRootFrame->GetProperty(RetainedDisplayListBuilder::Cached());
}

void nsLayoutUtils::UnionChildOverflow(nsIFrame* aFrame,
                                       OverflowAreas& aOverflowAreas,
                                       FrameChildListIDs aSkipChildLists) {
  for (const auto& [list, listID] : aFrame->ChildLists()) {
    if (aSkipChildLists.contains(listID)) {
      continue;
    }
    for (nsIFrame* child : list) {
      aOverflowAreas.UnionWith(
          child->GetActualAndNormalOverflowAreasRelativeToParent());
    }
  }
}

static void DestroyViewID(void* aObject, nsAtom* aPropertyName,
                          void* aPropertyValue, void* aData) {
  ViewID* id = static_cast<ViewID*>(aPropertyValue);
  GetContentMap().Remove(*id);
  delete id;
}

/**
 * A namespace class for static layout utilities.
 */

bool nsLayoutUtils::FindIDFor(const nsIContent* aContent, ViewID* aOutViewId) {
  void* scrollIdProperty = aContent->GetProperty(nsGkAtoms::RemoteId);
  if (scrollIdProperty) {
    *aOutViewId = *static_cast<ViewID*>(scrollIdProperty);
    return true;
  }
  return false;
}

ViewID nsLayoutUtils::FindOrCreateIDFor(nsIContent* aContent) {
  ViewID scrollId;

  if (!FindIDFor(aContent, &scrollId)) {
    scrollId = sScrollIdCounter++;
    aContent->SetProperty(nsGkAtoms::RemoteId, new ViewID(scrollId),
                          DestroyViewID);
    GetContentMap().InsertOrUpdate(scrollId, aContent);
  }

  return scrollId;
}

nsIContent* nsLayoutUtils::FindContentFor(ViewID aId) {
  MOZ_ASSERT(aId != ScrollableLayerGuid::NULL_SCROLL_ID,
             "Cannot find a content element in map for null IDs.");
  nsIContent* content;
  bool exists = GetContentMap().Get(aId, &content);

  if (exists) {
    return content;
  } else {
    return nullptr;
  }
}

nsIFrame* nsLayoutUtils::GetScrollContainerFrameFromContent(
    nsIContent* aContent) {
  nsIFrame* frame = aContent->GetPrimaryFrame();
  if (aContent->OwnerDoc()->GetRootElement() == aContent) {
    PresShell* presShell = frame ? frame->PresShell() : nullptr;
    if (!presShell) {
      presShell = aContent->OwnerDoc()->GetPresShell();
    }
    // We want the scroll container frame, the root scroll frame differs from
    // all others in that the primary frame is not the scroll frame.
    nsIFrame* rootScrollContainerFrame =
        presShell ? presShell->GetRootScrollContainerFrame() : nullptr;
    if (rootScrollContainerFrame) {
      frame = rootScrollContainerFrame;
    }
  }
  return frame;
}

ScrollContainerFrame* nsLayoutUtils::FindScrollContainerFrameFor(
    nsIContent* aContent) {
  nsIFrame* scrollContainerFrame = GetScrollContainerFrameFromContent(aContent);
  return scrollContainerFrame ? scrollContainerFrame->GetScrollTargetFrame()
                              : nullptr;
}

ScrollContainerFrame* nsLayoutUtils::FindScrollContainerFrameFor(ViewID aId) {
  nsIContent* content = FindContentFor(aId);
  if (!content) {
    return nullptr;
  }

  return FindScrollContainerFrameFor(content);
}

ViewID nsLayoutUtils::FindIDForScrollContainerFrame(
    ScrollContainerFrame* aScrollContainerFrame) {
  if (!aScrollContainerFrame) {
    return ScrollableLayerGuid::NULL_SCROLL_ID;
  }

  nsIContent* scrollContent = aScrollContainerFrame->GetContent();

  ScrollableLayerGuid::ViewID scrollId;
  if (scrollContent && nsLayoutUtils::FindIDFor(scrollContent, &scrollId)) {
    return scrollId;
  }

  return ScrollableLayerGuid::NULL_SCROLL_ID;
}

bool nsLayoutUtils::UsesAsyncScrolling(nsIFrame* aFrame) {
#ifdef MOZ_WIDGET_ANDROID
  // We always have async scrolling for android
  return true;
#endif

  return AsyncPanZoomEnabled(aFrame);
}

bool nsLayoutUtils::AsyncPanZoomEnabled(const nsIFrame* aFrame) {
  // We use this as a shortcut, since if the compositor will never use APZ,
  // no widget will either.
  if (!gfxPlatform::AsyncPanZoomEnabled()) {
    return false;
  }

  const nsIFrame* frame = nsLayoutUtils::GetDisplayRootFrame(aFrame);
  nsIWidget* widget = frame->GetNearestWidget();
  if (!widget) {
    return false;
  }
  return widget->AsyncPanZoomEnabled();
}

bool nsLayoutUtils::AllowZoomingForDocument(
    const mozilla::dom::Document* aDocument) {
  if (aDocument->GetPresShell() &&
      !aDocument->GetPresShell()->AsyncPanZoomEnabled()) {
    return false;
  }
  // True if we allow zooming for all documents on this platform, or if we are
  // in RDM.
  BrowsingContext* bc = aDocument->GetBrowsingContext();
  return StaticPrefs::apz_allow_zooming() || (bc && bc->InRDMPane());
}

static bool HasVisibleAnonymousContents(Document* aDoc) {
  for (RefPtr<AnonymousContent>& ac : aDoc->GetAnonymousContents()) {
    // We check to see if the anonymous content node has a frame. If it doesn't,
    // that means that's not visible to the user because e.g. it's display:none.
    // For now we assume that if it has a frame, it is visible. We might be able
    // to refine this further by adding complexity if it turns out this
    // condition results in a lot of false positives.
    if (ac->Host()->GetPrimaryFrame()) {
      return true;
    }
  }
  return false;
}

bool nsLayoutUtils::ShouldDisableApzForElement(nsIContent* aContent) {
  if (!aContent) {
    return false;
  }

  if (aContent->GetProperty(nsGkAtoms::apzDisabled)) {
    return true;
  }

  Document* doc = aContent->GetComposedDoc();
  if (PresShell* rootPresShell =
          APZCCallbackHelper::GetRootContentDocumentPresShellForContent(
              aContent)) {
    if (Document* rootDoc = rootPresShell->GetDocument()) {
      nsIFrame* rootScrollContainerFrame =
          rootPresShell->GetRootScrollContainerFrame();
      nsIContent* rootContent = rootScrollContainerFrame
                                    ? rootScrollContainerFrame->GetContent()
                                    : rootDoc->GetDocumentElement();
      // For the AccessibleCaret and other anonymous contents: disable APZ on
      // any scrollable subframes that are not the root scrollframe of a
      // document, if the document has any visible anonymous contents.
      //
      // If we find this is triggering in too many scenarios then we might
      // want to tighten this check further. The main use cases for which we
      // want to disable APZ as of this writing are listed in bug 1316318.
      if (aContent != rootContent && HasVisibleAnonymousContents(rootDoc)) {
        return true;
      }
    }
  }

  if (!doc) {
    return false;
  }

  if (PresShell* presShell = doc->GetPresShell()) {
    if (RefPtr<AccessibleCaretEventHub> eventHub =
            presShell->GetAccessibleCaretEventHub()) {
      // Disable APZ for all elements if AccessibleCaret tells us to do so.
      if (eventHub->ShouldDisableApz()) {
        return true;
      }
    }
  }

  return StaticPrefs::apz_disable_for_scroll_linked_effects() &&
         doc->HasScrollLinkedEffect();
}

void nsLayoutUtils::NotifyPaintSkipTransaction(ViewID aScrollId) {
  if (ScrollContainerFrame* sf =
          nsLayoutUtils::FindScrollContainerFrameFor(aScrollId)) {
    MOZ_ASSERT(sf && sf->PresShell() &&
               !sf->PresShell()->IsResolutionUpdated());
    sf->NotifyApzTransaction();
  }
}

nsContainerFrame* nsLayoutUtils::LastContinuationWithChild(
    nsContainerFrame* aFrame) {
  MOZ_ASSERT(aFrame, "NULL frame pointer");
  for (auto f = aFrame->LastContinuation(); f; f = f->GetPrevContinuation()) {
    for (const auto& childList : f->ChildLists()) {
      if (MOZ_LIKELY(!childList.mList.IsEmpty())) {
        return static_cast<nsContainerFrame*>(f);
      }
    }
  }
  return aFrame;
}

// static
FrameChildListID nsLayoutUtils::GetChildListNameFor(nsIFrame* aChildFrame) {
  FrameChildListID id = FrameChildListID::Principal;

  MOZ_DIAGNOSTIC_ASSERT(!aChildFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW));

  if (aChildFrame->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
    nsIFrame* pif = aChildFrame->GetPrevInFlow();
    if (pif->GetParent() == aChildFrame->GetParent()) {
      id = FrameChildListID::ExcessOverflowContainers;
    } else {
      id = FrameChildListID::OverflowContainers;
    }
  } else {
    LayoutFrameType childType = aChildFrame->Type();
    if (LayoutFrameType::TableColGroup == childType) {
      id = FrameChildListID::ColGroup;
    } else if (aChildFrame->IsTableCaption()) {
      id = FrameChildListID::Caption;
    } else {
      id = FrameChildListID::Principal;
    }
  }

#ifdef DEBUG
  // Verify that the frame is actually in that child list or in the
  // corresponding overflow list.
  nsContainerFrame* parent = aChildFrame->GetParent();
  bool found = parent->GetChildList(id).ContainsFrame(aChildFrame);
  if (!found) {
    found = parent->GetChildList(FrameChildListID::Overflow)
                .ContainsFrame(aChildFrame);
    MOZ_ASSERT(found, "not in child list");
  }
#endif

  return id;
}

static Element* GetPseudo(const nsIContent* aContent, nsAtom* aPseudoProperty) {
  MOZ_ASSERT(aPseudoProperty == nsGkAtoms::beforePseudoProperty ||
             aPseudoProperty == nsGkAtoms::afterPseudoProperty ||
             aPseudoProperty == nsGkAtoms::markerPseudoProperty);
  if (!aContent->MayHaveAnonymousChildren()) {
    return nullptr;
  }
  return static_cast<Element*>(aContent->GetProperty(aPseudoProperty));
}

/*static*/
Element* nsLayoutUtils::GetBeforePseudo(const nsIContent* aContent) {
  return GetPseudo(aContent, nsGkAtoms::beforePseudoProperty);
}

/*static*/
nsIFrame* nsLayoutUtils::GetBeforeFrame(const nsIContent* aContent) {
  Element* pseudo = GetBeforePseudo(aContent);
  return pseudo ? pseudo->GetPrimaryFrame() : nullptr;
}

/*static*/
Element* nsLayoutUtils::GetAfterPseudo(const nsIContent* aContent) {
  return GetPseudo(aContent, nsGkAtoms::afterPseudoProperty);
}

/*static*/
nsIFrame* nsLayoutUtils::GetAfterFrame(const nsIContent* aContent) {
  Element* pseudo = GetAfterPseudo(aContent);
  return pseudo ? pseudo->GetPrimaryFrame() : nullptr;
}

/*static*/
Element* nsLayoutUtils::GetMarkerPseudo(const nsIContent* aContent) {
  return GetPseudo(aContent, nsGkAtoms::markerPseudoProperty);
}

/*static*/
nsIFrame* nsLayoutUtils::GetMarkerFrame(const nsIContent* aContent) {
  Element* pseudo = GetMarkerPseudo(aContent);
  return pseudo ? pseudo->GetPrimaryFrame() : nullptr;
}

#ifdef ACCESSIBILITY
void nsLayoutUtils::GetMarkerSpokenText(const nsIContent* aContent,
                                        nsAString& aText) {
  MOZ_ASSERT(aContent && aContent->IsGeneratedContentContainerForMarker());

  aText.Truncate();

  nsIFrame* frame = aContent->GetPrimaryFrame();
  if (!frame) {
    return;
  }

  if (!frame->StyleContent()->NonAltContentItems().IsEmpty()) {
    for (nsIFrame* child : frame->PrincipalChildList()) {
      nsIFrame::RenderedText text = child->GetRenderedText();
      aText += text.mString;
    }
    return;
  }

  if (!frame->StyleList()->mListStyleImage.IsNone()) {
    // ::marker is an image, so use default bullet character.
    static const char16_t kDiscMarkerString[] = {0x2022, ' ', 0};
    aText.AssignLiteral(kDiscMarkerString);
    return;
  }

  frame->PresContext()
      ->FrameConstructor()
      ->GetContainStyleScopeManager()
      .GetSpokenCounterText(frame, aText);
}
#endif

// static
nsIFrame* nsLayoutUtils::GetClosestFrameOfType(nsIFrame* aFrame,
                                               LayoutFrameType aFrameType,
                                               nsIFrame* aStopAt) {
  for (nsIFrame* frame = aFrame; frame; frame = frame->GetParent()) {
    if (frame->Type() == aFrameType) {
      return frame;
    }
    if (frame == aStopAt) {
      break;
    }
  }
  return nullptr;
}

/* static */
nsIFrame* nsLayoutUtils::GetPageFrame(nsIFrame* aFrame) {
  return GetClosestFrameOfType(aFrame, LayoutFrameType::Page);
}

/* static */
nsIFrame* nsLayoutUtils::GetStyleFrame(nsIFrame* aPrimaryFrame) {
  MOZ_ASSERT(aPrimaryFrame);
  if (aPrimaryFrame->IsTableWrapperFrame()) {
    nsIFrame* inner = aPrimaryFrame->PrincipalChildList().FirstChild();
    // inner may be null, if aPrimaryFrame is mid-destruction
    return inner;
  }

  return aPrimaryFrame;
}

const nsIFrame* nsLayoutUtils::GetStyleFrame(const nsIFrame* aPrimaryFrame) {
  return nsLayoutUtils::GetStyleFrame(const_cast<nsIFrame*>(aPrimaryFrame));
}

nsIFrame* nsLayoutUtils::GetStyleFrame(const nsIContent* aContent) {
  nsIFrame* frame = aContent->GetPrimaryFrame();
  if (!frame) {
    return nullptr;
  }

  return nsLayoutUtils::GetStyleFrame(frame);
}

CSSIntCoord nsLayoutUtils::UnthemedScrollbarSize(StyleScrollbarWidth aWidth) {
  switch (aWidth) {
    case StyleScrollbarWidth::Auto:
      return 12;
    case StyleScrollbarWidth::Thin:
      return 6;
    case StyleScrollbarWidth::None:
      return 0;
  }
  return 0;
}

/* static */
nsIFrame* nsLayoutUtils::GetPrimaryFrameFromStyleFrame(nsIFrame* aStyleFrame) {
  nsIFrame* parent = aStyleFrame->GetParent();
  return parent && parent->IsTableWrapperFrame() ? parent : aStyleFrame;
}

/* static */
const nsIFrame* nsLayoutUtils::GetPrimaryFrameFromStyleFrame(
    const nsIFrame* aStyleFrame) {
  return nsLayoutUtils::GetPrimaryFrameFromStyleFrame(
      const_cast<nsIFrame*>(aStyleFrame));
}

/*static*/
bool nsLayoutUtils::IsPrimaryStyleFrame(const nsIFrame* aFrame) {
  if (aFrame->IsTableWrapperFrame()) {
    return false;
  }

  const nsIFrame* parent = aFrame->GetParent();
  if (parent && parent->IsTableWrapperFrame()) {
    return parent->PrincipalChildList().FirstChild() == aFrame;
  }

  return aFrame->IsPrimaryFrame();
}

nsIFrame* nsLayoutUtils::GetFloatFromPlaceholder(nsIFrame* aFrame) {
  NS_ASSERTION(aFrame->IsPlaceholderFrame(), "Must have a placeholder here");
  if (aFrame->HasAnyStateBits(PLACEHOLDER_FOR_FLOAT)) {
    nsIFrame* outOfFlowFrame =
        nsPlaceholderFrame::GetRealFrameForPlaceholder(aFrame);
    NS_ASSERTION(outOfFlowFrame && outOfFlowFrame->IsFloating(),
                 "How did that happen?");
    return outOfFlowFrame;
  }

  return nullptr;
}

// static
nsIFrame* nsLayoutUtils::GetCrossDocParentFrameInProcess(
    const nsIFrame* aFrame, nsPoint* aCrossDocOffset) {
  nsIFrame* p = aFrame->GetParent();
  if (p) {
    return p;
  }

  nsView* v = aFrame->GetView();
  if (!v) {
    return nullptr;
  }
  v = v->GetParent();  // anonymous inner view
  if (!v) {
    return nullptr;
  }
  v = v->GetParent();  // subdocumentframe's view
  if (!v) {
    return nullptr;
  }

  p = v->GetFrame();
  if (p && aCrossDocOffset) {
    nsSubDocumentFrame* subdocumentFrame = do_QueryFrame(p);
    MOZ_ASSERT(subdocumentFrame);
    *aCrossDocOffset += subdocumentFrame->GetExtraOffset();
  }

  return p;
}

// static
nsIFrame* nsLayoutUtils::GetCrossDocParentFrame(const nsIFrame* aFrame,
                                                nsPoint* aCrossDocOffset) {
  return GetCrossDocParentFrameInProcess(aFrame, aCrossDocOffset);
}

// static
bool nsLayoutUtils::IsProperAncestorFrameCrossDoc(
    const nsIFrame* aAncestorFrame, const nsIFrame* aFrame,
    const nsIFrame* aCommonAncestor) {
  if (aFrame == aAncestorFrame) {
    return false;
  }
  return IsAncestorFrameCrossDoc(aAncestorFrame, aFrame, aCommonAncestor);
}

// static
bool nsLayoutUtils::IsProperAncestorFrameCrossDocInProcess(
    const nsIFrame* aAncestorFrame, const nsIFrame* aFrame,
    const nsIFrame* aCommonAncestor) {
  if (aFrame == aAncestorFrame) {
    return false;
  }
  return IsAncestorFrameCrossDocInProcess(aAncestorFrame, aFrame,
                                          aCommonAncestor);
}

// static
bool nsLayoutUtils::IsAncestorFrameCrossDoc(const nsIFrame* aAncestorFrame,
                                            const nsIFrame* aFrame,
                                            const nsIFrame* aCommonAncestor) {
  for (const nsIFrame* f = aFrame; f != aCommonAncestor;
       f = GetCrossDocParentFrameInProcess(f)) {
    if (f == aAncestorFrame) {
      return true;
    }
  }
  return aCommonAncestor == aAncestorFrame;
}

// static
bool nsLayoutUtils::IsAncestorFrameCrossDocInProcess(
    const nsIFrame* aAncestorFrame, const nsIFrame* aFrame,
    const nsIFrame* aCommonAncestor) {
  for (const nsIFrame* f = aFrame; f != aCommonAncestor;
       f = GetCrossDocParentFrameInProcess(f)) {
    if (f == aAncestorFrame) {
      return true;
    }
  }
  return aCommonAncestor == aAncestorFrame;
}

// static
bool nsLayoutUtils::IsProperAncestorFrame(const nsIFrame* aAncestorFrame,
                                          const nsIFrame* aFrame,
                                          const nsIFrame* aCommonAncestor) {
  if (aFrame == aAncestorFrame) {
    return false;
  }
  for (const nsIFrame* f = aFrame; f != aCommonAncestor; f = f->GetParent()) {
    if (f == aAncestorFrame) {
      return true;
    }
  }
  return aCommonAncestor == aAncestorFrame;
}

// static
nsIFrame* nsLayoutUtils::FillAncestors(nsIFrame* aFrame,
                                       nsIFrame* aStopAtAncestor,
                                       nsTArray<nsIFrame*>* aAncestors) {
  while (aFrame && aFrame != aStopAtAncestor) {
    aAncestors->AppendElement(aFrame);
    aFrame = nsLayoutUtils::GetParentOrPlaceholderFor(aFrame);
  }
  return aFrame;
}

// Return true if aFrame1 is after aFrame2
static bool IsFrameAfter(nsIFrame* aFrame1, nsIFrame* aFrame2) {
  nsIFrame* f = aFrame2;
  do {
    f = f->GetNextSibling();
    if (f == aFrame1) {
      return true;
    }
  } while (f);
  return false;
}

// static
int32_t nsLayoutUtils::DoCompareTreePosition(nsIFrame* aFrame1,
                                             nsIFrame* aFrame2,
                                             nsIFrame* aCommonAncestor) {
  MOZ_ASSERT(aFrame1, "aFrame1 must not be null");
  MOZ_ASSERT(aFrame2, "aFrame2 must not be null");

  AutoTArray<nsIFrame*, 20> frame2Ancestors;
  nsIFrame* nonCommonAncestor =
      FillAncestors(aFrame2, aCommonAncestor, &frame2Ancestors);
  return DoCompareTreePosition(aFrame1, aFrame2, frame2Ancestors,
                               nonCommonAncestor ? aCommonAncestor : nullptr);
}

// static
int32_t nsLayoutUtils::DoCompareTreePosition(
    nsIFrame* aFrame1, nsIFrame* aFrame2, nsTArray<nsIFrame*>& aFrame2Ancestors,
    nsIFrame* aCommonAncestor) {
  MOZ_ASSERT(aFrame1, "aFrame1 must not be null");
  MOZ_ASSERT(aFrame2, "aFrame2 must not be null");

  nsPresContext* presContext = aFrame1->PresContext();
  if (presContext != aFrame2->PresContext()) {
    NS_ERROR("no common ancestor at all, different documents");
    return 0;
  }

  AutoTArray<nsIFrame*, 20> frame1Ancestors;
  if (aCommonAncestor &&
      !FillAncestors(aFrame1, aCommonAncestor, &frame1Ancestors)) {
    // We reached the root of the frame tree ... if aCommonAncestor was set,
    // it is wrong
    return DoCompareTreePosition(aFrame1, aFrame2, nullptr);
  }

  int32_t last1 = int32_t(frame1Ancestors.Length()) - 1;
  int32_t last2 = int32_t(aFrame2Ancestors.Length()) - 1;
  while (last1 >= 0 && last2 >= 0 &&
         frame1Ancestors[last1] == aFrame2Ancestors[last2]) {
    last1--;
    last2--;
  }

  if (last1 < 0) {
    if (last2 < 0) {
      NS_ASSERTION(aFrame1 == aFrame2, "internal error?");
      return 0;
    }
    // aFrame1 is an ancestor of aFrame2
    return -1;
  }

  if (last2 < 0) {
    // aFrame2 is an ancestor of aFrame1
    return 1;
  }

  nsIFrame* ancestor1 = frame1Ancestors[last1];
  nsIFrame* ancestor2 = aFrame2Ancestors[last2];
  // Now we should be able to walk sibling chains to find which one is first
  if (IsFrameAfter(ancestor2, ancestor1)) {
    return -1;
  }
  if (IsFrameAfter(ancestor1, ancestor2)) {
    return 1;
  }
  NS_WARNING("Frames were in different child lists???");
  return 0;
}

// static
nsIFrame* nsLayoutUtils::GetLastSibling(nsIFrame* aFrame) {
  if (!aFrame) {
    return nullptr;
  }

  nsIFrame* next;
  while ((next = aFrame->GetNextSibling()) != nullptr) {
    aFrame = next;
  }
  return aFrame;
}

// static
nsView* nsLayoutUtils::FindSiblingViewFor(nsView* aParentView,
                                          nsIFrame* aFrame) {
  nsIFrame* parentViewFrame = aParentView->GetFrame();
  nsIContent* parentViewContent =
      parentViewFrame ? parentViewFrame->GetContent() : nullptr;
  for (nsView* insertBefore = aParentView->GetFirstChild(); insertBefore;
       insertBefore = insertBefore->GetNextSibling()) {
    nsIFrame* f = insertBefore->GetFrame();
    if (!f) {
      // this view could be some anonymous view attached to a meaningful parent
      for (nsView* searchView = insertBefore->GetParent(); searchView;
           searchView = searchView->GetParent()) {
        f = searchView->GetFrame();
        if (f) {
          break;
        }
      }
      NS_ASSERTION(f, "Can't find a frame anywhere!");
    }
    if (!f || !aFrame->GetContent() || !f->GetContent() ||
        nsContentUtils::CompareTreePosition<TreeKind::Flat>(
            aFrame->GetContent(), f->GetContent(), parentViewContent) > 0) {
      // aFrame's content is after f's content (or we just don't know),
      // so put our view before f's view
      return insertBefore;
    }
  }
  return nullptr;
}

// static
ScrollContainerFrame* nsLayoutUtils::GetScrollContainerFrameFor(
    const nsIFrame* aScrolledFrame) {
  nsIFrame* frame = aScrolledFrame->GetParent();
  ScrollContainerFrame* sf = do_QueryFrame(frame);
  return (sf && sf->GetScrolledFrame() == aScrolledFrame) ? sf : nullptr;
}

/* static */
SideBits nsLayoutUtils::GetSideBitsForFixedPositionContent(
    const nsIFrame* aFixedPosFrame) {
  SideBits sides = SideBits::eNone;
  if (aFixedPosFrame) {
    const nsStylePosition* position = aFixedPosFrame->StylePosition();
    if (!position
             ->GetAnchorResolvedInset(eSideRight, StylePositionProperty::Fixed)
             ->IsAuto()) {
      sides |= SideBits::eRight;
    }
    if (!position
             ->GetAnchorResolvedInset(eSideLeft, StylePositionProperty::Fixed)
             ->IsAuto()) {
      sides |= SideBits::eLeft;
    }
    if (!position
             ->GetAnchorResolvedInset(eSideBottom, StylePositionProperty::Fixed)
             ->IsAuto()) {
      sides |= SideBits::eBottom;
    }
    if (!position
             ->GetAnchorResolvedInset(eSideTop, StylePositionProperty::Fixed)
             ->IsAuto()) {
      sides |= SideBits::eTop;
    }
  }
  return sides;
}

ScrollableLayerGuid::ViewID nsLayoutUtils::ScrollIdForRootScrollFrame(
    nsPresContext* aPresContext) {
  ViewID id = ScrollableLayerGuid::NULL_SCROLL_ID;
  if (nsIFrame* rootScrollFrame =
          aPresContext->PresShell()->GetRootScrollContainerFrame()) {
    if (nsIContent* content = rootScrollFrame->GetContent()) {
      id = FindOrCreateIDFor(content);
    }
  }
  return id;
}

// static
ScrollContainerFrame* nsLayoutUtils::GetNearestScrollableFrameForDirection(
    nsIFrame* aFrame, ScrollDirections aDirections) {
  NS_ASSERTION(
      aFrame, "GetNearestScrollableFrameForDirection expects a non-null frame");
  // FIXME Bug 1714720 : This nearest scroll target is not going to work over
  // process boundaries, in such cases we need to hand over in APZ side.
  for (nsIFrame* f = aFrame; f;
       f = nsLayoutUtils::GetCrossDocParentFrameInProcess(f)) {
    ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(f);
    if (scrollContainerFrame) {
      ScrollDirections directions =
          scrollContainerFrame
              ->GetAvailableScrollingDirectionsForUserInputEvents();
      if (aDirections.contains(ScrollDirection::eVertical)) {
        if (directions.contains(ScrollDirection::eVertical)) {
          return scrollContainerFrame;
        }
      }
      if (aDirections.contains(ScrollDirection::eHorizontal)) {
        if (directions.contains(ScrollDirection::eHorizontal)) {
          return scrollContainerFrame;
        }
      }
    }
  }
  return nullptr;
}

static nsIFrame* GetNearestScrollableOrOverflowClipFrame(
    nsIFrame* aFrame, uint32_t aFlags,
    const std::function<bool(const nsIFrame* aCurrentFrame)>& aClipFrameCheck =
        nullptr) {
  MOZ_ASSERT(
      aFrame,
      "GetNearestScrollableOrOverflowClipFrame expects a non-null frame");

  auto GetNextFrame = [aFlags](const nsIFrame* aFrame) -> nsIFrame* {
    return (aFlags & nsLayoutUtils::SCROLLABLE_SAME_DOC)
               ? aFrame->GetParent()
               : nsLayoutUtils::GetCrossDocParentFrameInProcess(aFrame);
  };

  for (nsIFrame* f = aFrame; f; f = GetNextFrame(f)) {
    if (aClipFrameCheck && aClipFrameCheck(f)) {
      return f;
    }

    if ((aFlags & nsLayoutUtils::SCROLLABLE_STOP_AT_PAGE) && f->IsPageFrame()) {
      break;
    }

    // TODO: We should also stop at popup frames other than
    // SCROLLABLE_ONLY_ASYNC_SCROLLABLE cases.
    if ((aFlags & nsLayoutUtils::SCROLLABLE_ONLY_ASYNC_SCROLLABLE) &&
        f->IsMenuPopupFrame()) {
      break;
    }

    if (ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(f)) {
      if (aFlags & nsLayoutUtils::SCROLLABLE_ONLY_ASYNC_SCROLLABLE) {
        if (scrollContainerFrame->WantAsyncScroll()) {
          return f;
        }
      } else {
        ScrollStyles ss = scrollContainerFrame->GetScrollStyles();
        if ((aFlags & nsLayoutUtils::SCROLLABLE_INCLUDE_HIDDEN) ||
            ss.mVertical != StyleOverflow::Hidden ||
            ss.mHorizontal != StyleOverflow::Hidden) {
          return f;
        }
      }
      if (aFlags & nsLayoutUtils::SCROLLABLE_ALWAYS_MATCH_ROOT) {
        PresShell* presShell = f->PresShell();
        if (presShell->GetRootScrollContainerFrame() == f &&
            presShell->GetDocument() &&
            presShell->GetDocument()->IsRootDisplayDocument()) {
          return f;
        }
      }
    }
    if ((aFlags & nsLayoutUtils::SCROLLABLE_FIXEDPOS_FINDS_ROOT) &&
        f->StyleDisplay()->mPosition == StylePositionProperty::Fixed &&
        nsLayoutUtils::IsReallyFixedPos(f)) {
      return f->PresShell()->GetRootScrollContainerFrame();
    }
  }
  return nullptr;
}

// static
ScrollContainerFrame* nsLayoutUtils::GetNearestScrollContainerFrame(
    nsIFrame* aFrame, uint32_t aFlags) {
  nsIFrame* found = GetNearestScrollableOrOverflowClipFrame(aFrame, aFlags);
  if (!found) {
    return nullptr;
  }

  return do_QueryFrame(found);
}

// static
nsIFrame* nsLayoutUtils::GetNearestOverflowClipFrame(nsIFrame* aFrame) {
  return GetNearestScrollableOrOverflowClipFrame(
      aFrame, SCROLLABLE_SAME_DOC | SCROLLABLE_INCLUDE_HIDDEN,
      [](const nsIFrame* currentFrame) -> bool {
        // In cases of SVG Inner/Outer frames it basically clips descendants
        // unless overflow: visible is explicitly specified.
        LayoutFrameType type = currentFrame->Type();
        return ((type == LayoutFrameType::SVGOuterSVG ||
                 type == LayoutFrameType::SVGInnerSVG) &&
                (currentFrame->StyleDisplay()->mOverflowX !=
                     StyleOverflow::Visible &&
                 currentFrame->StyleDisplay()->mOverflowY !=
                     StyleOverflow::Visible));
      });
}

// static
bool nsLayoutUtils::HasPseudoStyle(nsIContent* aContent,
                                   ComputedStyle* aComputedStyle,
                                   PseudoStyleType aPseudoElement,
                                   nsPresContext* aPresContext) {
  MOZ_ASSERT(aPresContext, "Must have a prescontext");

  RefPtr<ComputedStyle> pseudoContext;
  if (aContent) {
    pseudoContext = aPresContext->StyleSet()->ProbePseudoElementStyle(
        *aContent->AsElement(), aPseudoElement, nullptr, aComputedStyle);
  }
  return pseudoContext != nullptr;
}

nsPoint nsLayoutUtils::GetDOMEventCoordinatesRelativeTo(Event* aDOMEvent,
                                                        nsIFrame* aFrame) {
  if (!aDOMEvent) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }
  WidgetEvent* event = aDOMEvent->WidgetEventPtr();
  if (!event) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }
  return GetEventCoordinatesRelativeTo(event, RelativeTo{aFrame});
}

static bool IsValidCoordinateTypeEvent(const WidgetEvent* aEvent) {
  if (!aEvent) {
    return false;
  }
  return aEvent->mClass == eMouseEventClass ||
         aEvent->mClass == eMouseScrollEventClass ||
         aEvent->mClass == eWheelEventClass ||
         aEvent->mClass == eDragEventClass ||
         aEvent->mClass == eSimpleGestureEventClass ||
         aEvent->mClass == ePointerEventClass ||
         aEvent->mClass == eGestureNotifyEventClass ||
         aEvent->mClass == eTouchEventClass ||
         aEvent->mClass == eQueryContentEventClass;
}

nsPoint nsLayoutUtils::GetEventCoordinatesRelativeTo(const WidgetEvent* aEvent,
                                                     RelativeTo aFrame) {
  if (!IsValidCoordinateTypeEvent(aEvent)) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  return GetEventCoordinatesRelativeTo(aEvent, aEvent->AsGUIEvent()->mRefPoint,
                                       aFrame);
}

nsPoint nsLayoutUtils::GetEventCoordinatesRelativeTo(
    const WidgetEvent* aEvent, const LayoutDeviceIntPoint& aPoint,
    RelativeTo aFrame) {
  if (!aFrame.mFrame) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  nsIWidget* widget = aEvent->AsGUIEvent()->mWidget;
  if (!widget) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  return GetEventCoordinatesRelativeTo(widget, aPoint, aFrame);
}

nsPoint GetEventCoordinatesRelativeTo(nsIWidget* aWidget,
                                      const LayoutDeviceIntPoint& aPoint,
                                      RelativeTo aFrame) {
  const nsIFrame* frame = aFrame.mFrame;
  if (!frame || !aWidget) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  nsView* view = frame->GetView();
  if (view) {
    nsIWidget* frameWidget = view->GetWidget();
    if (frameWidget && frameWidget == aWidget) {
      // Special case this cause it happens a lot.
      // This also fixes bug 664707, events in the extra-special case of select
      // dropdown popups that are transformed.
      nsPresContext* presContext = frame->PresContext();
      nsPoint pt(presContext->DevPixelsToAppUnits(aPoint.x),
                 presContext->DevPixelsToAppUnits(aPoint.y));
      return pt - view->ViewToWidgetOffset();
    }
  }

  /* If we walk up the frame tree and discover that any of the frames are
   * transformed, we need to do extra work to convert from the global
   * space to the local space.
   */
  const nsIFrame* rootFrame = frame;
  bool transformFound = false;
  for (const nsIFrame* f = frame; f;
       f = nsLayoutUtils::GetCrossDocParentFrameInProcess(f)) {
    if (f->IsTransformed() || ViewportUtils::IsZoomedContentRoot(f)) {
      transformFound = true;
    }

    rootFrame = f;
  }

  nsView* rootView = rootFrame->GetView();
  if (!rootView) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  nsPoint widgetToView = nsLayoutUtils::TranslateWidgetToView(
      rootFrame->PresContext(), aWidget, aPoint, rootView);

  if (widgetToView == nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE)) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  // Convert from root document app units to app units of the document aFrame
  // is in.
  int32_t rootAPD = rootFrame->PresContext()->AppUnitsPerDevPixel();
  int32_t localAPD = frame->PresContext()->AppUnitsPerDevPixel();
  widgetToView = widgetToView.ScaleToOtherAppUnits(rootAPD, localAPD);

  /* If we encountered a transform, we can't do simple arithmetic to figure
   * out how to convert back to aFrame's coordinates and must use the CTM.
   */
  if (transformFound || frame->IsInSVGTextSubtree()) {
    return nsLayoutUtils::TransformRootPointToFrame(ViewportType::Visual,
                                                    aFrame, widgetToView);
  }

  /* Otherwise, all coordinate systems are translations of one another,
   * so we can just subtract out the difference.
   */
  return widgetToView - frame->GetOffsetToCrossDoc(rootFrame);
}

nsPoint nsLayoutUtils::GetEventCoordinatesRelativeTo(
    nsIWidget* aWidget, const LayoutDeviceIntPoint& aPoint, RelativeTo aFrame) {
  nsPoint result = ::GetEventCoordinatesRelativeTo(aWidget, aPoint, aFrame);
  if (aFrame.mViewportType == ViewportType::Layout && aFrame.mFrame &&
      aFrame.mFrame->Type() == LayoutFrameType::Viewport &&
      aFrame.mFrame->PresContext()->IsRootContentDocumentCrossProcess()) {
    result = ViewportUtils::VisualToLayout(result, aFrame.mFrame->PresShell());
  }
  return result;
}

nsIFrame* nsLayoutUtils::GetPopupFrameForEventCoordinates(
    nsPresContext* aRootPresContext, const WidgetEvent* aEvent) {
  if (!IsValidCoordinateTypeEvent(aEvent)) {
    return nullptr;
  }

  const auto* guiEvent = aEvent->AsGUIEvent();
  return GetPopupFrameForPoint(aRootPresContext, guiEvent->mWidget,
                               guiEvent->mRefPoint);
}

nsIFrame* nsLayoutUtils::GetPopupFrameForPoint(
    nsPresContext* aRootPresContext, nsIWidget* aWidget,
    const mozilla::LayoutDeviceIntPoint& aPoint,
    GetPopupFrameForPointFlags aFlags /* = GetPopupFrameForPointFlags(0) */) {
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (!pm) {
    return nullptr;
  }
  nsTArray<nsMenuPopupFrame*> popups;
  pm->GetVisiblePopups(popups);
  // Search from top to bottom
  for (nsMenuPopupFrame* popup : popups) {
    if (popup->PresContext()->GetRootPresContext() != aRootPresContext) {
      continue;
    }
    if (!popup->ScrollableOverflowRect().Contains(GetEventCoordinatesRelativeTo(
            aWidget, aPoint, RelativeTo{popup}))) {
      continue;
    }
    if (aFlags & GetPopupFrameForPointFlags::OnlyReturnFramesWithWidgets) {
      if (!popup->HasView() || !popup->GetView()->HasWidget()) {
        continue;
      }
    }
    return popup;
  }
  return nullptr;
}

void nsLayoutUtils::GetContainerAndOffsetAtEvent(PresShell* aPresShell,
                                                 const WidgetEvent* aEvent,
                                                 nsIContent** aContainer,
                                                 int32_t* aOffset) {
  MOZ_ASSERT(aContainer || aOffset);

  if (aContainer) {
    *aContainer = nullptr;
  }
  if (aOffset) {
    *aOffset = 0;
  }

  if (!aPresShell) {
    return;
  }

  aPresShell->FlushPendingNotifications(FlushType::Layout);

  RefPtr<nsPresContext> presContext = aPresShell->GetPresContext();
  if (!presContext) {
    return;
  }

  nsIFrame* targetFrame = presContext->EventStateManager()->GetEventTarget();
  if (!targetFrame) {
    return;
  }

  WidgetEvent* openingEvent = nullptr;
  // For popupshowing events, redirect via the original mouse event
  // that triggered the popup to open.
  if (aEvent->mMessage == eXULPopupShowing) {
    if (auto* pm = nsXULPopupManager::GetInstance()) {
      if (Event* openingPopupEvent = pm->GetOpeningPopupEvent()) {
        openingEvent = openingPopupEvent->WidgetEventPtr();
      }
    }
  }

  nsPoint point = nsLayoutUtils::GetEventCoordinatesRelativeTo(
      openingEvent ? openingEvent : aEvent, RelativeTo{targetFrame});

  if (aContainer) {
    // TODO: This result may be useful to change to Selection.  However, this
    //       may return improper node (e.g., native anonymous node) for the
    //       Selection.  Perhaps, this should take Selection optionally and
    //       if it's specified, needs to check if it's proper for the
    //       Selection.
    nsCOMPtr<nsIContent> container =
        targetFrame->GetContentOffsetsFromPoint(point).content;
    if (container && (!container->ChromeOnlyAccess() ||
                      nsContentUtils::CanAccessNativeAnon())) {
      container.forget(aContainer);
    }
  }
  if (aOffset) {
    *aOffset = targetFrame->GetContentOffsetsFromPoint(point).offset;
  }
}

void nsLayoutUtils::ConstrainToCoordValues(float& aStart, float& aSize) {
  MOZ_ASSERT(aSize >= 0);

  // Here we try to make sure that the resulting nsRect will continue to cover
  // as much of the area that was covered by the original gfx Rect as possible.

  // We clamp the bounds of the rect to {nscoord_MIN,nscoord_MAX} since
  // nsRect::X/Y() and nsRect::XMost/YMost() can't return values outwith this
  // range:
  float end = aStart + aSize;
  aStart = std::clamp(aStart, float(nscoord_MIN), float(nscoord_MAX));
  end = std::clamp(end, float(nscoord_MIN), float(nscoord_MAX));

  aSize = end - aStart;

  // We must also clamp aSize to {0,nscoord_MAX} since nsRect::Width/Height()
  // can't return a value greater than nscoord_MAX. If aSize is greater than
  // nscoord_MAX then we reduce it to nscoord_MAX while keeping the rect
  // centered:
  if (MOZ_UNLIKELY(std::isnan(aSize))) {
    // Can happen if aStart is -inf and aSize is +inf for example.
    aStart = 0.0f;
    aSize = float(nscoord_MAX);
  } else if (aSize > float(nscoord_MAX)) {
    float excess = aSize - float(nscoord_MAX);
    excess /= 2;
    aStart += excess;
    aSize = float(nscoord_MAX);
  }
}

/**
 * Given a gfxFloat, constrains its value to be between nscoord_MIN and
 * nscoord_MAX.
 *
 * @param aVal The value to constrain (in/out)
 */
static void ConstrainToCoordValues(gfxFloat& aVal) {
  if (aVal <= nscoord_MIN) {
    aVal = nscoord_MIN;
  } else if (aVal >= nscoord_MAX) {
    aVal = nscoord_MAX;
  }
}

void nsLayoutUtils::ConstrainToCoordValues(gfxFloat& aStart, gfxFloat& aSize) {
  gfxFloat max = aStart + aSize;

  // Clamp the end points to within nscoord range
  ::ConstrainToCoordValues(aStart);
  ::ConstrainToCoordValues(max);

  aSize = max - aStart;
  // If the width if still greater than the max nscoord, then bring both
  // endpoints in by the same amount until it fits.
  if (MOZ_UNLIKELY(std::isnan(aSize))) {
    // Can happen if aStart is -inf and aSize is +inf for example.
    aStart = 0.0f;
    aSize = nscoord_MAX;
  } else if (aSize > nscoord_MAX) {
    gfxFloat excess = aSize - nscoord_MAX;
    excess /= 2;

    aStart += excess;
    aSize = nscoord_MAX;
  } else if (aSize < nscoord_MIN) {
    gfxFloat excess = aSize - nscoord_MIN;
    excess /= 2;

    aStart -= excess;
    aSize = nscoord_MIN;
  }
}

nsRegion nsLayoutUtils::RoundedRectIntersectRect(const nsRect& aRoundedRect,
                                                 const nscoord aRadii[8],
                                                 const nsRect& aContainedRect) {
  // rectFullHeight and rectFullWidth together will approximately contain
  // the total area of the frame minus the rounded corners.
  nsRect rectFullHeight = aRoundedRect;
  nscoord xDiff = std::max(aRadii[eCornerTopLeftX], aRadii[eCornerBottomLeftX]);
  rectFullHeight.x += xDiff;
  rectFullHeight.width -=
      std::max(aRadii[eCornerTopRightX], aRadii[eCornerBottomRightX]) + xDiff;
  nsRect r1;
  r1.IntersectRect(rectFullHeight, aContainedRect);

  nsRect rectFullWidth = aRoundedRect;
  nscoord yDiff = std::max(aRadii[eCornerTopLeftY], aRadii[eCornerTopRightY]);
  rectFullWidth.y += yDiff;
  rectFullWidth.height -=
      std::max(aRadii[eCornerBottomLeftY], aRadii[eCornerBottomRightY]) + yDiff;
  nsRect r2;
  r2.IntersectRect(rectFullWidth, aContainedRect);

  nsRegion result;
  result.Or(r1, r2);
  return result;
}

nsIntRegion nsLayoutUtils::RoundedRectIntersectIntRect(
    const nsIntRect& aRoundedRect, const RectCornerRadii& aCornerRadii,
    const nsIntRect& aContainedRect) {
  // rectFullHeight and rectFullWidth together will approximately contain
  // the total area of the frame minus the rounded corners.
  nsIntRect rectFullHeight = aRoundedRect;
  uint32_t xDiff =
      std::max(aCornerRadii.TopLeft().width, aCornerRadii.BottomLeft().width);
  rectFullHeight.x += xDiff;
  rectFullHeight.width -= std::max(aCornerRadii.TopRight().width,
                                   aCornerRadii.BottomRight().width) +
                          xDiff;
  nsIntRect r1;
  r1.IntersectRect(rectFullHeight, aContainedRect);

  nsIntRect rectFullWidth = aRoundedRect;
  uint32_t yDiff =
      std::max(aCornerRadii.TopLeft().height, aCornerRadii.TopRight().height);
  rectFullWidth.y += yDiff;
  rectFullWidth.height -= std::max(aCornerRadii.BottomLeft().height,
                                   aCornerRadii.BottomRight().height) +
                          yDiff;
  nsIntRect r2;
  r2.IntersectRect(rectFullWidth, aContainedRect);

  nsIntRegion result;
  result.Or(r1, r2);
  return result;
}

// Helper for RoundedRectIntersectsRect.
static bool CheckCorner(nscoord aXOffset, nscoord aYOffset, nscoord aXRadius,
                        nscoord aYRadius) {
  MOZ_ASSERT(aXOffset > 0 && aYOffset > 0,
             "must not pass nonpositives to CheckCorner");
  MOZ_ASSERT(aXRadius >= 0 && aYRadius >= 0,
             "must not pass negatives to CheckCorner");

  // Avoid floating point math unless we're either (1) within the
  // quarter-ellipse area at the rounded corner or (2) outside the
  // rounding.
  if (aXOffset >= aXRadius || aYOffset >= aYRadius) {
    return true;
  }

  // Convert coordinates to a unit circle with (0,0) as the center of
  // curvature, and see if we're inside the circle or outside.
  float scaledX = float(aXRadius - aXOffset) / float(aXRadius);
  float scaledY = float(aYRadius - aYOffset) / float(aYRadius);
  return scaledX * scaledX + scaledY * scaledY < 1.0f;
}

bool nsLayoutUtils::RoundedRectIntersectsRect(const nsRect& aRoundedRect,
                                              const nscoord aRadii[8],
                                              const nsRect& aTestRect) {
  if (!aTestRect.Intersects(aRoundedRect)) {
    return false;
  }

  // distances from this edge of aRoundedRect to opposite edge of aTestRect,
  // which we know are positive due to the Intersects check above.
  nsMargin insets;
  insets.top = aTestRect.YMost() - aRoundedRect.y;
  insets.right = aRoundedRect.XMost() - aTestRect.x;
  insets.bottom = aRoundedRect.YMost() - aTestRect.y;
  insets.left = aTestRect.XMost() - aRoundedRect.x;

  // Check whether the bottom-right corner of aTestRect is inside the
  // top left corner of aBounds when rounded by aRadii, etc.  If any
  // corner is not, then fail; otherwise succeed.
  return CheckCorner(insets.left, insets.top, aRadii[eCornerTopLeftX],
                     aRadii[eCornerTopLeftY]) &&
         CheckCorner(insets.right, insets.top, aRadii[eCornerTopRightX],
                     aRadii[eCornerTopRightY]) &&
         CheckCorner(insets.right, insets.bottom, aRadii[eCornerBottomRightX],
                     aRadii[eCornerBottomRightY]) &&
         CheckCorner(insets.left, insets.bottom, aRadii[eCornerBottomLeftX],
                     aRadii[eCornerBottomLeftY]);
}

nsRect nsLayoutUtils::MatrixTransformRect(const nsRect& aBounds,
                                          const Matrix4x4& aMatrix,
                                          float aFactor) {
  RectDouble image =
      RectDouble(NSAppUnitsToDoublePixels(aBounds.x, aFactor),
                 NSAppUnitsToDoublePixels(aBounds.y, aFactor),
                 NSAppUnitsToDoublePixels(aBounds.width, aFactor),
                 NSAppUnitsToDoublePixels(aBounds.height, aFactor));

  RectDouble maxBounds = RectDouble(
      double(nscoord_MIN) / aFactor * 0.5, double(nscoord_MIN) / aFactor * 0.5,
      double(nscoord_MAX) / aFactor, double(nscoord_MAX) / aFactor);

  image = aMatrix.TransformAndClipBounds(image, maxBounds);

  return RoundGfxRectToAppRect(ThebesRect(image), aFactor);
}

nsRect nsLayoutUtils::MatrixTransformRect(const nsRect& aBounds,
                                          const Matrix4x4Flagged& aMatrix,
                                          float aFactor) {
  RectDouble image =
      RectDouble(NSAppUnitsToDoublePixels(aBounds.x, aFactor),
                 NSAppUnitsToDoublePixels(aBounds.y, aFactor),
                 NSAppUnitsToDoublePixels(aBounds.width, aFactor),
                 NSAppUnitsToDoublePixels(aBounds.height, aFactor));

  RectDouble maxBounds = RectDouble(
      double(nscoord_MIN) / aFactor * 0.5, double(nscoord_MIN) / aFactor * 0.5,
      double(nscoord_MAX) / aFactor, double(nscoord_MAX) / aFactor);

  image = aMatrix.TransformAndClipBounds(image, maxBounds);

  return RoundGfxRectToAppRect(ThebesRect(image), aFactor);
}

nsPoint nsLayoutUtils::MatrixTransformPoint(const nsPoint& aPoint,
                                            const Matrix4x4& aMatrix,
                                            float aFactor) {
  gfxPoint image = gfxPoint(NSAppUnitsToFloatPixels(aPoint.x, aFactor),
                            NSAppUnitsToFloatPixels(aPoint.y, aFactor));
  image = aMatrix.TransformPoint(image);
  return nsPoint(NSFloatPixelsToAppUnits(float(image.x), aFactor),
                 NSFloatPixelsToAppUnits(float(image.y), aFactor));
}

void nsLayoutUtils::PostTranslate(Matrix4x4& aTransform, const nsPoint& aOrigin,
                                  float aAppUnitsPerPixel, bool aRounded) {
  Point3D gfxOrigin =
      Point3D(NSAppUnitsToFloatPixels(aOrigin.x, aAppUnitsPerPixel),
              NSAppUnitsToFloatPixels(aOrigin.y, aAppUnitsPerPixel), 0.0f);
  if (aRounded) {
    gfxOrigin.x = NS_round(gfxOrigin.x);
    gfxOrigin.y = NS_round(gfxOrigin.y);
  }
  aTransform.PostTranslate(gfxOrigin);
}

bool nsLayoutUtils::ShouldSnapToGrid(const nsIFrame* aFrame) {
  return !aFrame || !aFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT) ||
         aFrame->IsSVGOuterSVGAnonChildFrame();
}

Matrix4x4Flagged nsLayoutUtils::GetTransformToAncestor(
    RelativeTo aFrame, RelativeTo aAncestor, uint32_t aFlags,
    nsIFrame** aOutAncestor) {
  nsIFrame* parent;
  Matrix4x4Flagged ctm;
  // Make sure we don't get an invalid combination of source and destination
  // RelativeTo values.
  MOZ_ASSERT(!(aFrame.mViewportType == ViewportType::Visual &&
               aAncestor.mViewportType == ViewportType::Layout));
  if (aFrame == aAncestor) {
    return ctm;
  }
  ctm = aFrame.mFrame->GetTransformMatrix(aFrame.mViewportType, aAncestor,
                                          &parent, aFlags);
  if (!aFrame.mFrame->Combines3DTransformWithAncestors()) {
    ctm.ProjectTo2D();
  }
  while (parent && parent != aAncestor.mFrame &&
         (!(aFlags & nsIFrame::STOP_AT_STACKING_CONTEXT_AND_DISPLAY_PORT) ||
          (!parent->IsStackingContext() &&
           !DisplayPortUtils::FrameHasDisplayPort(parent)))) {
    nsIFrame* cur = parent;
    ctm = ctm * cur->GetTransformMatrix(aFrame.mViewportType, aAncestor,
                                        &parent, aFlags);
    if (!cur->Combines3DTransformWithAncestors()) {
      ctm.ProjectTo2D();
    }
  }
  if (aOutAncestor) {
    *aOutAncestor = parent;
  }
  return ctm;
}

MatrixScales nsLayoutUtils::GetTransformToAncestorScale(
    const nsIFrame* aFrame) {
  Matrix4x4Flagged transform = GetTransformToAncestor(
      RelativeTo{aFrame},
      RelativeTo{nsLayoutUtils::GetDisplayRootFrame(aFrame)});
  Matrix transform2D;
  if (transform.CanDraw2D(&transform2D)) {
    return ThebesMatrix(transform2D).ScaleFactors().ConvertTo<float>();
  }
  return MatrixScales();
}

static Matrix4x4Flagged GetTransformToAncestorExcludingAnimated(
    nsIFrame* aFrame, const nsIFrame* aAncestor) {
  nsIFrame* parent;
  Matrix4x4Flagged ctm;
  if (aFrame == aAncestor) {
    return ctm;
  }
  if (ActiveLayerTracker::IsScaleSubjectToAnimation(aFrame)) {
    return ctm;
  }
  ctm = aFrame->GetTransformMatrix(ViewportType::Layout, RelativeTo{aAncestor},
                                   &parent);
  while (parent && parent != aAncestor) {
    if (ActiveLayerTracker::IsScaleSubjectToAnimation(parent)) {
      return Matrix4x4Flagged();
    }
    if (!parent->Extend3DContext()) {
      ctm.ProjectTo2D();
    }
    ctm = ctm * parent->GetTransformMatrix(ViewportType::Layout,
                                           RelativeTo{aAncestor}, &parent);
  }
  return ctm;
}

MatrixScales nsLayoutUtils::GetTransformToAncestorScaleExcludingAnimated(
    nsIFrame* aFrame) {
  Matrix4x4Flagged transform = GetTransformToAncestorExcludingAnimated(
      aFrame, nsLayoutUtils::GetDisplayRootFrame(aFrame));
  Matrix transform2D;
  if (transform.Is2D(&transform2D)) {
    return ThebesMatrix(transform2D).ScaleFactors().ConvertTo<float>();
  }
  return MatrixScales();
}

const nsIFrame* nsLayoutUtils::FindNearestCommonAncestorFrame(
    const nsIFrame* aFrame1, const nsIFrame* aFrame2) {
  AutoTArray<const nsIFrame*, 100> ancestors1;
  AutoTArray<const nsIFrame*, 100> ancestors2;
  const nsIFrame* commonAncestor = nullptr;
  if (aFrame1->PresContext() == aFrame2->PresContext()) {
    commonAncestor = aFrame1->PresShell()->GetRootFrame();
  }
  for (const nsIFrame* f = aFrame1; f != commonAncestor;
       f = nsLayoutUtils::GetCrossDocParentFrameInProcess(f)) {
    ancestors1.AppendElement(f);
  }
  for (const nsIFrame* f = aFrame2; f != commonAncestor;
       f = nsLayoutUtils::GetCrossDocParentFrameInProcess(f)) {
    ancestors2.AppendElement(f);
  }
  uint32_t minLengths = std::min(ancestors1.Length(), ancestors2.Length());
  for (uint32_t i = 1; i <= minLengths; ++i) {
    if (ancestors1[ancestors1.Length() - i] ==
        ancestors2[ancestors2.Length() - i]) {
      commonAncestor = ancestors1[ancestors1.Length() - i];
    } else {
      break;
    }
  }
  return commonAncestor;
}

const nsIFrame* nsLayoutUtils::FindNearestCommonAncestorFrameWithinBlock(
    const nsTextFrame* aFrame1, const nsTextFrame* aFrame2) {
  MOZ_ASSERT(aFrame1);
  MOZ_ASSERT(aFrame2);

  const nsIFrame* f1 = aFrame1;
  const nsIFrame* f2 = aFrame2;

  int n1 = 1;
  int n2 = 1;

  for (auto f = f1->GetParent();;) {
    NS_ASSERTION(f, "All text frames should have a block ancestor");
    if (!f) {
      return nullptr;
    }
    if (f->IsBlockFrameOrSubclass()) {
      break;
    }
    ++n1;
    f = f->GetParent();
  }

  for (auto f = f2->GetParent();;) {
    NS_ASSERTION(f, "All text frames should have a block ancestor");
    if (!f) {
      return nullptr;
    }
    if (f->IsBlockFrameOrSubclass()) {
      break;
    }
    ++n2;
    f = f->GetParent();
  }

  if (n1 > n2) {
    std::swap(n1, n2);
    std::swap(f1, f2);
  }

  while (n2 > n1) {
    f2 = f2->GetParent();
    --n2;
  }

  while (n2 >= 0) {
    if (f1 == f2) {
      return f1;
    }
    f1 = f1->GetParent();
    f2 = f2->GetParent();
    --n2;
  }

  return nullptr;
}

bool nsLayoutUtils::AuthorSpecifiedBorderBackgroundDisablesTheming(
    StyleAppearance aAppearance) {
  return aAppearance == StyleAppearance::NumberInput ||
         aAppearance == StyleAppearance::PasswordInput ||
         aAppearance == StyleAppearance::Button ||
         aAppearance == StyleAppearance::Textfield ||
         aAppearance == StyleAppearance::Textarea ||
         aAppearance == StyleAppearance::Listbox ||
         aAppearance == StyleAppearance::Menulist;
}

static SVGTextFrame* GetContainingSVGTextFrame(const nsIFrame* aFrame) {
  if (!aFrame->IsInSVGTextSubtree()) {
    return nullptr;
  }

  return static_cast<SVGTextFrame*>(nsLayoutUtils::GetClosestFrameOfType(
      aFrame->GetParent(), LayoutFrameType::SVGText));
}

static bool TransformGfxPointFromAncestor(RelativeTo aFrame,
                                          const Point& aPoint,
                                          RelativeTo aAncestor,
                                          Maybe<Matrix4x4Flagged>& aMatrixCache,
                                          Point* aOut) {
  SVGTextFrame* text = GetContainingSVGTextFrame(aFrame.mFrame);

  if (!aMatrixCache) {
    auto matrix = nsLayoutUtils::GetTransformToAncestor(
        RelativeTo{text ? text : aFrame.mFrame, aFrame.mViewportType},
        aAncestor);
    aMatrixCache = matrix.MaybeInverse();
    if (aMatrixCache.isNothing()) {
      return false;
    }
  }

  const Matrix4x4Flagged& ctm = *aMatrixCache;
  Point4D point = ctm.ProjectPoint(aPoint);
  if (!point.HasPositiveWCoord()) {
    return false;
  }

  *aOut = point.As2DPoint();

  if (text) {
    *aOut = text->TransformFramePointToTextChild(*aOut, aFrame.mFrame);
  }

  return true;
}

static Point TransformGfxPointToAncestor(
    RelativeTo aFrame, const Point& aPoint, RelativeTo aAncestor,
    Maybe<Matrix4x4Flagged>& aMatrixCache) {
  if (SVGTextFrame* text = GetContainingSVGTextFrame(aFrame.mFrame)) {
    Point result =
        text->TransformFramePointFromTextChild(aPoint, aFrame.mFrame);
    return TransformGfxPointToAncestor(RelativeTo{text}, result, aAncestor,
                                       aMatrixCache);
  }
  if (!aMatrixCache) {
    aMatrixCache.emplace(
        nsLayoutUtils::GetTransformToAncestor(aFrame, aAncestor));
  }
  return aMatrixCache->ProjectPoint(aPoint).As2DPoint();
}

static Rect TransformGfxRectToAncestor(
    RelativeTo aFrame, const Rect& aRect, RelativeTo aAncestor,
    bool* aPreservesAxisAlignedRectangles = nullptr,
    Maybe<Matrix4x4Flagged>* aMatrixCache = nullptr,
    bool aStopAtStackingContextAndDisplayPortAndOOFFrame = false,
    nsIFrame** aOutAncestor = nullptr) {
  Rect result;
  Matrix4x4Flagged ctm;
  if (SVGTextFrame* text = GetContainingSVGTextFrame(aFrame.mFrame)) {
    result = text->TransformFrameRectFromTextChild(aRect, aFrame.mFrame);

    result = TransformGfxRectToAncestor(
        RelativeTo{text}, result, aAncestor, nullptr, aMatrixCache,
        aStopAtStackingContextAndDisplayPortAndOOFFrame, aOutAncestor);
    if (aPreservesAxisAlignedRectangles) {
      // TransformFrameRectFromTextChild could involve any kind of transform, we
      // could drill down into it to get an answer out of it but we don't yet.
      *aPreservesAxisAlignedRectangles = false;
    }
    return result;
  }
  if (aMatrixCache && *aMatrixCache) {
    // We are given a matrix to use, so use it
    ctm = aMatrixCache->value();
  } else {
    // Else, compute it
    uint32_t flags = 0;
    if (aStopAtStackingContextAndDisplayPortAndOOFFrame) {
      flags |= nsIFrame::STOP_AT_STACKING_CONTEXT_AND_DISPLAY_PORT;
    }
    ctm = nsLayoutUtils::GetTransformToAncestor(aFrame, aAncestor, flags,
                                                aOutAncestor);
    if (aMatrixCache) {
      // and put it in the cache, if provided
      *aMatrixCache = Some(ctm);
    }
  }
  // Fill out the axis-alignment flag
  if (aPreservesAxisAlignedRectangles) {
    // TransformFrameRectFromTextChild could involve any kind of transform, we
    // could drill down into it to get an answer out of it but we don't yet.
    Matrix matrix2d;
    *aPreservesAxisAlignedRectangles =
        ctm.Is2D(&matrix2d) && matrix2d.PreservesAxisAlignedRectangles();
  }
  const nsIFrame* ancestor = aOutAncestor ? *aOutAncestor : aAncestor.mFrame;
  float factor = ancestor->PresContext()->AppUnitsPerDevPixel();
  Rect maxBounds =
      Rect(float(nscoord_MIN) / factor * 0.5, float(nscoord_MIN) / factor * 0.5,
           float(nscoord_MAX) / factor, float(nscoord_MAX) / factor);
  return ctm.TransformAndClipBounds(aRect, maxBounds);
}

nsLayoutUtils::TransformResult nsLayoutUtils::TransformPoints(
    RelativeTo aFromFrame, RelativeTo aToFrame, uint32_t aPointCount,
    CSSPoint* aPoints) {
  // Conceptually, {ViewportFrame, Visual} is an ancestor of
  // {ViewportFrame, Layout}, so factor that into the nearest ancestor
  // computation.
  RelativeTo nearestCommonAncestor{
      FindNearestCommonAncestorFrame(aFromFrame.mFrame, aToFrame.mFrame),
      aFromFrame.mViewportType == ViewportType::Visual ||
              aToFrame.mViewportType == ViewportType::Visual
          ? ViewportType::Visual
          : ViewportType::Layout};
  if (!nearestCommonAncestor.mFrame) {
    return NO_COMMON_ANCESTOR;
  }
  CSSToLayoutDeviceScale devPixelsPerCSSPixelFromFrame =
      aFromFrame.mFrame->PresContext()->CSSToDevPixelScale();
  CSSToLayoutDeviceScale devPixelsPerCSSPixelToFrame =
      aToFrame.mFrame->PresContext()->CSSToDevPixelScale();
  Maybe<Matrix4x4Flagged> cacheTo;
  Maybe<Matrix4x4Flagged> cacheFrom;
  for (uint32_t i = 0; i < aPointCount; ++i) {
    LayoutDevicePoint devPixels = aPoints[i] * devPixelsPerCSSPixelFromFrame;
    // What should the behaviour be if some of the points aren't invertible
    // and others are? Just assume all points are for now.
    Point toDevPixels =
        TransformGfxPointToAncestor(aFromFrame, Point(devPixels.x, devPixels.y),
                                    nearestCommonAncestor, cacheTo);
    Point result;
    if (!TransformGfxPointFromAncestor(
            aToFrame, toDevPixels, nearestCommonAncestor, cacheFrom, &result)) {
      return NONINVERTIBLE_TRANSFORM;
    }
    // Divide here so that when the devPixelsPerCSSPixels are the same, we get
    // the correct answer instead of some inaccuracy multiplying a number by its
    // reciprocal.
    aPoints[i] =
        LayoutDevicePoint(result.x, result.y) / devPixelsPerCSSPixelToFrame;
  }
  return TRANSFORM_SUCCEEDED;
}

nsLayoutUtils::TransformResult nsLayoutUtils::TransformPoint(
    RelativeTo aFromFrame, RelativeTo aToFrame, nsPoint& aPoint) {
  CSSPoint point = CSSPoint::FromAppUnits(aPoint);
  auto result = TransformPoints(aFromFrame, aToFrame, 1, &point);
  if (result == TRANSFORM_SUCCEEDED) {
    aPoint = CSSPoint::ToAppUnits(point);
  }
  return result;
}

nsLayoutUtils::TransformResult nsLayoutUtils::TransformRect(
    const nsIFrame* aFromFrame, const nsIFrame* aToFrame, nsRect& aRect) {
  const nsIFrame* nearestCommonAncestor =
      FindNearestCommonAncestorFrame(aFromFrame, aToFrame);
  if (!nearestCommonAncestor) {
    return NO_COMMON_ANCESTOR;
  }
  Matrix4x4Flagged downToDest = GetTransformToAncestor(
      RelativeTo{aToFrame}, RelativeTo{nearestCommonAncestor});
  // invert downToDest in place
  if (!downToDest.Invert()) {
    return NONINVERTIBLE_TRANSFORM;
  }
  aRect = TransformFrameRectToAncestor(aFromFrame, aRect,
                                       RelativeTo{nearestCommonAncestor});

  float devPixelsPerAppUnitFromFrame =
      1.0f / nearestCommonAncestor->PresContext()->AppUnitsPerDevPixel();
  float devPixelsPerAppUnitToFrame =
      1.0f / aToFrame->PresContext()->AppUnitsPerDevPixel();
  gfx::Rect toDevPixels = downToDest.ProjectRectBounds(
      gfx::Rect(aRect.x * devPixelsPerAppUnitFromFrame,
                aRect.y * devPixelsPerAppUnitFromFrame,
                aRect.width * devPixelsPerAppUnitFromFrame,
                aRect.height * devPixelsPerAppUnitFromFrame),
      Rect(-std::numeric_limits<Float>::max() * devPixelsPerAppUnitFromFrame *
               0.5f,
           -std::numeric_limits<Float>::max() * devPixelsPerAppUnitFromFrame *
               0.5f,
           std::numeric_limits<Float>::max() * devPixelsPerAppUnitFromFrame,
           std::numeric_limits<Float>::max() * devPixelsPerAppUnitFromFrame));
  aRect.x = NSToCoordRoundWithClamp(toDevPixels.x / devPixelsPerAppUnitToFrame);
  aRect.y = NSToCoordRoundWithClamp(toDevPixels.y / devPixelsPerAppUnitToFrame);
  aRect.width =
      NSToCoordRoundWithClamp(toDevPixels.width / devPixelsPerAppUnitToFrame);
  aRect.height =
      NSToCoordRoundWithClamp(toDevPixels.height / devPixelsPerAppUnitToFrame);
  return TRANSFORM_SUCCEEDED;
}

nsRect nsLayoutUtils::GetRectRelativeToFrame(Element* aElement,
                                             nsIFrame* aFrame) {
  if (!aElement || !aFrame) {
    return nsRect();
  }

  nsIFrame* frame = aElement->GetPrimaryFrame();
  if (!frame) {
    return nsRect();
  }

  nsRect rect = frame->GetRectRelativeToSelf();
  nsLayoutUtils::TransformResult rv =
      nsLayoutUtils::TransformRect(frame, aFrame, rect);
  if (rv != nsLayoutUtils::TRANSFORM_SUCCEEDED) {
    return nsRect();
  }

  return rect;
}

bool nsLayoutUtils::ContainsPoint(const nsRect& aRect, const nsPoint& aPoint,
                                  nscoord aInflateSize) {
  nsRect rect = aRect;
  rect.Inflate(aInflateSize);
  return rect.Contains(aPoint);
}

nsRect nsLayoutUtils::ClampRectToScrollFrames(nsIFrame* aFrame,
                                              const nsRect& aRect) {
  nsIFrame* closestScrollFrame = nsLayoutUtils::GetClosestFrameOfType(
      aFrame, LayoutFrameType::ScrollContainer);

  nsRect resultRect = aRect;

  while (closestScrollFrame) {
    ScrollContainerFrame* sf = do_QueryFrame(closestScrollFrame);

    nsRect scrollPortRect = sf->GetScrollPortRect();
    nsLayoutUtils::TransformRect(closestScrollFrame, aFrame, scrollPortRect);

    resultRect = resultRect.Intersect(scrollPortRect);

    // Check whether aRect is visible in the scroll frame or not.
    if (resultRect.IsEmpty()) {
      break;
    }

    // Get next ancestor scroll frame.
    closestScrollFrame = nsLayoutUtils::GetClosestFrameOfType(
        closestScrollFrame->GetParent(), LayoutFrameType::ScrollContainer);
  }

  return resultRect;
}

nsPoint nsLayoutUtils::TransformAncestorPointToFrame(RelativeTo aFrame,
                                                     const nsPoint& aPoint,
                                                     RelativeTo aAncestor) {
  float factor = aFrame.mFrame->PresContext()->AppUnitsPerDevPixel();
  Point result(NSAppUnitsToFloatPixels(aPoint.x, factor),
               NSAppUnitsToFloatPixels(aPoint.y, factor));

  Maybe<Matrix4x4Flagged> matrixCache;
  if (!TransformGfxPointFromAncestor(aFrame, result, aAncestor, matrixCache,
                                     &result)) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  return nsPoint(NSFloatPixelsToAppUnits(float(result.x), factor),
                 NSFloatPixelsToAppUnits(float(result.y), factor));
}

nsPoint nsLayoutUtils::TransformFramePointToRoot(ViewportType aToType,
                                                 RelativeTo aFromFrame,
                                                 const nsPoint& aPoint) {
  float factor = aFromFrame.mFrame->PresContext()->AppUnitsPerDevPixel();
  Point result(NSAppUnitsToFloatPixels(aPoint.x, factor),
               NSAppUnitsToFloatPixels(aPoint.y, factor));

  RelativeTo ancestor = RelativeTo{nullptr, aToType};

  Maybe<Matrix4x4Flagged> matrixCache;
  Point res =
      TransformGfxPointToAncestor(aFromFrame, result, ancestor, matrixCache);

  return nsPoint(NSFloatPixelsToAppUnits(float(res.x), factor),
                 NSFloatPixelsToAppUnits(float(res.y), factor));
};

nsRect nsLayoutUtils::TransformFrameRectToAncestor(
    const nsIFrame* aFrame, const nsRect& aRect, RelativeTo aAncestor,
    bool* aPreservesAxisAlignedRectangles /* = nullptr */,
    Maybe<Matrix4x4Flagged>* aMatrixCache /* = nullptr */,
    bool aStopAtStackingContextAndDisplayPortAndOOFFrame /* = false */,
    nsIFrame** aOutAncestor /* = nullptr */) {
  MOZ_ASSERT(IsAncestorFrameCrossDocInProcess(aAncestor.mFrame, aFrame),
             "Fix the caller");
  float srcAppUnitsPerDevPixel = aFrame->PresContext()->AppUnitsPerDevPixel();
  Rect result(NSAppUnitsToFloatPixels(aRect.x, srcAppUnitsPerDevPixel),
              NSAppUnitsToFloatPixels(aRect.y, srcAppUnitsPerDevPixel),
              NSAppUnitsToFloatPixels(aRect.width, srcAppUnitsPerDevPixel),
              NSAppUnitsToFloatPixels(aRect.height, srcAppUnitsPerDevPixel));
  result = TransformGfxRectToAncestor(
      RelativeTo{aFrame}, result, aAncestor, aPreservesAxisAlignedRectangles,
      aMatrixCache, aStopAtStackingContextAndDisplayPortAndOOFFrame,
      aOutAncestor);

  float destAppUnitsPerDevPixel =
      aAncestor.mFrame->PresContext()->AppUnitsPerDevPixel();
  return nsRect(
      NSFloatPixelsToAppUnits(float(result.x), destAppUnitsPerDevPixel),
      NSFloatPixelsToAppUnits(float(result.y), destAppUnitsPerDevPixel),
      NSFloatPixelsToAppUnits(float(result.width), destAppUnitsPerDevPixel),
      NSFloatPixelsToAppUnits(float(result.height), destAppUnitsPerDevPixel));
}

static LayoutDeviceIntPoint GetWidgetOffset(nsIWidget* aWidget,
                                            nsIWidget*& aRootWidget) {
  LayoutDeviceIntPoint offset(0, 0);
  while (aWidget->GetWindowType() == widget::WindowType::Child) {
    nsIWidget* parent = aWidget->GetParent();
    if (!parent) {
      break;
    }
    LayoutDeviceIntRect bounds = aWidget->GetBounds();
    offset += bounds.TopLeft();
    aWidget = parent;
  }
  aRootWidget = aWidget;
  return offset;
}

LayoutDeviceIntPoint nsLayoutUtils::WidgetToWidgetOffset(nsIWidget* aFrom,
                                                         nsIWidget* aTo) {
  nsIWidget* fromRoot;
  LayoutDeviceIntPoint fromOffset = GetWidgetOffset(aFrom, fromRoot);
  nsIWidget* toRoot;
  LayoutDeviceIntPoint toOffset = GetWidgetOffset(aTo, toRoot);

  if (fromRoot != toRoot) {
    fromOffset = aFrom->WidgetToScreenOffset();
    toOffset = aTo->WidgetToScreenOffset();
  }
  return fromOffset - toOffset;
}

nsPoint nsLayoutUtils::TranslateWidgetToView(nsPresContext* aPresContext,
                                             nsIWidget* aWidget,
                                             const LayoutDeviceIntPoint& aPt,
                                             nsView* aView) {
  nsPoint viewOffset;
  nsIWidget* viewWidget = aView->GetNearestWidget(&viewOffset);
  if (!viewWidget) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  LayoutDeviceIntPoint widgetPoint =
      aPt + WidgetToWidgetOffset(aWidget, viewWidget);
  nsPoint widgetAppUnits(aPresContext->DevPixelsToAppUnits(widgetPoint.x),
                         aPresContext->DevPixelsToAppUnits(widgetPoint.y));
  return widgetAppUnits - viewOffset;
}

LayoutDeviceIntPoint nsLayoutUtils::TranslateViewToWidget(
    nsPresContext* aPresContext, nsView* aView, nsPoint aPt,
    ViewportType aViewportType, nsIWidget* aWidget) {
  nsPoint viewOffset;
  nsIWidget* viewWidget = aView->GetNearestWidget(&viewOffset);
  if (!viewWidget) {
    return LayoutDeviceIntPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  nsPoint pt = (aPt + viewOffset);
  // The target coordinates are visual, so perform a layout-to-visual
  // conversion if the incoming coordinates are layout.
  if (aViewportType == ViewportType::Layout && aPresContext->GetPresShell()) {
    pt = ViewportUtils::LayoutToVisual(pt, aPresContext->GetPresShell());
  }
  LayoutDeviceIntPoint relativeToViewWidget(
      aPresContext->AppUnitsToDevPixels(pt.x),
      aPresContext->AppUnitsToDevPixels(pt.y));
  return relativeToViewWidget + WidgetToWidgetOffset(viewWidget, aWidget);
}

UsedClear nsLayoutUtils::CombineClearType(UsedClear aOrigClearType,
                                          UsedClear aNewClearType) {
  UsedClear clearType = aOrigClearType;
  switch (clearType) {
    case UsedClear::Left:
      if (UsedClear::Right == aNewClearType ||
          UsedClear::Both == aNewClearType) {
        clearType = UsedClear::Both;
      }
      break;
    case UsedClear::Right:
      if (UsedClear::Left == aNewClearType ||
          UsedClear::Both == aNewClearType) {
        clearType = UsedClear::Both;
      }
      break;
    case UsedClear::None:
      if (UsedClear::Left == aNewClearType ||
          UsedClear::Right == aNewClearType ||
          UsedClear::Both == aNewClearType) {
        clearType = aNewClearType;
      }
      break;
    case UsedClear::Both:
      // Do nothing.
      break;
  }
  return clearType;
}

#ifdef MOZ_DUMP_PAINTING
#  include <stdio.h>

static bool gDumpEventList = false;

// nsLayoutUtils::PaintFrame() can call itself recursively, so rather than
// maintaining a single paint count, we need a stack.
StaticAutoPtr<nsTArray<int>> gPaintCountStack;

struct AutoNestedPaintCount {
  AutoNestedPaintCount() { gPaintCountStack->AppendElement(0); }
  ~AutoNestedPaintCount() { gPaintCountStack->RemoveLastElement(); }
};

#endif

nsIFrame* nsLayoutUtils::GetFrameForPoint(
    RelativeTo aRelativeTo, nsPoint aPt, const FrameForPointOptions& aOptions) {
  AUTO_PROFILER_LABEL("nsLayoutUtils::GetFrameForPoint", LAYOUT);

  nsresult rv;
  AutoTArray<nsIFrame*, 8> outFrames;
  rv = GetFramesForArea(aRelativeTo, nsRect(aPt, nsSize(1, 1)), outFrames,
                        aOptions);
  NS_ENSURE_SUCCESS(rv, nullptr);
  return outFrames.SafeElementAt(0);
}

nsresult nsLayoutUtils::GetFramesForArea(RelativeTo aRelativeTo,
                                         const nsRect& aRect,
                                         nsTArray<nsIFrame*>& aOutFrames,
                                         const FrameForPointOptions& aOptions) {
  AUTO_PROFILER_LABEL("nsLayoutUtils::GetFramesForArea", LAYOUT);

  nsIFrame* frame = const_cast<nsIFrame*>(aRelativeTo.mFrame);

  nsDisplayListBuilder builder(frame, nsDisplayListBuilderMode::EventDelivery,
                               false);
  builder.BeginFrame();
  nsDisplayList list(&builder);

  if (aOptions.mBits.contains(FrameForPointOption::IgnorePaintSuppression)) {
    builder.IgnorePaintSuppression();
  }
  if (aOptions.mBits.contains(FrameForPointOption::IgnoreRootScrollFrame)) {
    nsIFrame* rootScrollContainerFrame =
        frame->PresShell()->GetRootScrollContainerFrame();
    if (rootScrollContainerFrame) {
      builder.SetIgnoreScrollFrame(rootScrollContainerFrame);
    }
  }
  if (aRelativeTo.mViewportType == ViewportType::Layout) {
    builder.SetIsRelativeToLayoutViewport();
  }
  if (aOptions.mBits.contains(FrameForPointOption::IgnoreCrossDoc)) {
    builder.SetDescendIntoSubdocuments(false);
  }

  if (aOptions.mBits.contains(FrameForPointOption::OnlyVisible)) {
    builder.SetHitTestIsForVisibility(aOptions.mVisibleThreshold);
  }

  builder.EnterPresShell(frame);

  builder.SetVisibleRect(aRect);
  builder.SetDirtyRect(aRect);

  frame->BuildDisplayListForStackingContext(&builder, &list);
  builder.LeavePresShell(frame, nullptr);

#ifdef MOZ_DUMP_PAINTING
  if (gDumpEventList) {
    fprintf_stderr(stderr, "Event handling --- (%d,%d):\n", aRect.x, aRect.y);

    std::stringstream ss;
    nsIFrame::PrintDisplayList(&builder, list, ss);
    print_stderr(ss);
  }
#endif

  nsDisplayItem::HitTestState hitTestState;
  list.HitTest(&builder, aRect, &hitTestState, &aOutFrames);

  builder.SetIsDestroying();
  list.DeleteAll(&builder);
  builder.EndFrame();
  return NS_OK;
}

mozilla::ParentLayerToScreenScale2D
nsLayoutUtils::GetTransformToAncestorScaleCrossProcessForFrameMetrics(
    const nsIFrame* aFrame) {
  ParentLayerToScreenScale2D transformToAncestorScale =
      ViewAs<ParentLayerToScreenScale2D>(
          nsLayoutUtils::GetTransformToAncestorScale(aFrame));

  if (BrowserChild* browserChild = BrowserChild::GetFrom(aFrame->PresShell())) {
    transformToAncestorScale =
        ViewTargetAs<ParentLayerPixel>(
            transformToAncestorScale,
            PixelCastJustification::PropagatingToChildProcess) *
        browserChild->GetEffectsInfo().mTransformToAncestorScale;
  }

  return transformToAncestorScale;
}

FrameMetrics nsLayoutUtils::CalculateBasicFrameMetrics(
    ScrollContainerFrame* aScrollContainerFrame) {
  // Calculate the metrics necessary for calculating the displayport.
  // This code has a lot in common with the code in ComputeFrameMetrics();
  // we may want to refactor this at some point.
  FrameMetrics metrics;
  nsPresContext* presContext = aScrollContainerFrame->PresContext();
  PresShell* presShell = presContext->PresShell();
  CSSToLayoutDeviceScale deviceScale = presContext->CSSToDevPixelScale();
  float resolution = 1.0f;
  bool isRcdRsf = aScrollContainerFrame->IsRootScrollFrameOfDocument() &&
                  presContext->IsRootContentDocumentCrossProcess();
  metrics.SetIsRootContent(isRcdRsf);
  if (isRcdRsf) {
    // Only the root content document's root scroll container frame should pick
    // up the presShell's resolution. All the other frames are 1.0.
    resolution = presShell->GetResolution();
  }
  LayoutDeviceToLayerScale cumulativeResolution(
      LayoutDeviceToLayerScale(presShell->GetCumulativeResolution()));

  LayerToParentLayerScale layerToParentLayerScale(1.0f);
  metrics.SetDevPixelsPerCSSPixel(deviceScale);
  metrics.SetPresShellResolution(resolution);

  metrics.SetTransformToAncestorScale(
      GetTransformToAncestorScaleCrossProcessForFrameMetrics(
          aScrollContainerFrame));
  metrics.SetCumulativeResolution(cumulativeResolution);
  metrics.SetZoom(deviceScale * cumulativeResolution * layerToParentLayerScale);

  // Only the size of the composition bounds is relevant to the
  // displayport calculation, not its origin.
  nsSize compositionSize =
      nsLayoutUtils::CalculateCompositionSizeForFrame(aScrollContainerFrame);
  LayoutDeviceToParentLayerScale compBoundsScale;
  if (aScrollContainerFrame == presShell->GetRootScrollContainerFrame() &&
      presContext->IsRootContentDocumentCrossProcess()) {
    if (presContext->GetParentPresContext()) {
      float res = presContext->GetParentPresContext()
                      ->PresShell()
                      ->GetCumulativeResolution();
      compBoundsScale = LayoutDeviceToParentLayerScale(res);
    }
  } else {
    compBoundsScale = cumulativeResolution * layerToParentLayerScale;
  }
  metrics.SetCompositionBounds(
      LayoutDeviceRect::FromAppUnits(nsRect(nsPoint(0, 0), compositionSize),
                                     presContext->AppUnitsPerDevPixel()) *
      compBoundsScale);

  metrics.SetBoundingCompositionSize(
      nsLayoutUtils::CalculateBoundingCompositionSize(aScrollContainerFrame,
                                                      false, metrics));

  metrics.SetLayoutViewport(CSSRect::FromAppUnits(
      nsRect(aScrollContainerFrame->GetScrollPosition(),
             aScrollContainerFrame->GetScrollPortRect().Size())));
  metrics.SetVisualScrollOffset(
      isRcdRsf ? CSSPoint::FromAppUnits(presShell->GetVisualViewportOffset())
               : metrics.GetLayoutViewport().TopLeft());

  metrics.SetScrollableRect(
      CSSRect::FromAppUnits(nsLayoutUtils::CalculateScrollableRectForFrame(
          aScrollContainerFrame, nullptr)));

  return metrics;
}

ScrollContainerFrame* nsLayoutUtils::GetAsyncScrollableAncestorFrame(
    nsIFrame* aTarget) {
  uint32_t flags = nsLayoutUtils::SCROLLABLE_ALWAYS_MATCH_ROOT |
                   nsLayoutUtils::SCROLLABLE_ONLY_ASYNC_SCROLLABLE |
                   nsLayoutUtils::SCROLLABLE_FIXEDPOS_FINDS_ROOT;
  return nsLayoutUtils::GetNearestScrollContainerFrame(aTarget, flags);
}

void nsLayoutUtils::AddExtraBackgroundItems(nsDisplayListBuilder* aBuilder,
                                            nsDisplayList* aList,
                                            nsIFrame* aFrame,
                                            const nsRect& aCanvasArea,
                                            const nsRegion& aVisibleRegion,
                                            nscolor aBackstop) {
  if (aFrame->IsPageFrame()) {
    // For printing, this function is first called on an nsPageFrame, which
    // creates a display list with a PageContent item. The PageContent item's
    // paint function calls this function on the nsPageFrame's child which is an
    // nsPageContentFrame. We only want to add the canvas background color item
    // once, for the nsPageContentFrame.
    return;
  }
  // Add the canvas background color to the bottom of the list. This
  // happens after we've built the list so that AddCanvasBackgroundColorItem
  // can monkey with the contents if necessary.
  nsRect canvasArea = aVisibleRegion.GetBounds();
  canvasArea.IntersectRect(aCanvasArea, canvasArea);
  nsDisplayListBuilder::AutoBuildingDisplayList buildingDisplayList(
      aBuilder, aFrame, canvasArea, canvasArea);
  aFrame->PresShell()->AddCanvasBackgroundColorItem(aBuilder, aList, aFrame,
                                                    canvasArea, aBackstop);
}

// #define PRINT_HITTESTINFO_STATS
#ifdef PRINT_HITTESTINFO_STATS
void PrintHitTestInfoStatsInternal(nsDisplayList* aList, int& aTotal,
                                   int& aHitTest, int& aVisible,
                                   int& aSpecial) {
  for (nsDisplayItem* i : *aList) {
    aTotal++;

    if (i->GetChildren()) {
      PrintHitTestInfoStatsInternal(i->GetChildren(), aTotal, aHitTest,
                                    aVisible, aSpecial);
    }

    if (i->GetType() == DisplayItemType::TYPE_COMPOSITOR_HITTEST_INFO) {
      aHitTest++;

      const auto& hitTestInfo = static_cast<nsDisplayCompositorHitTestInfo*>(i)
                                    ->GetHitTestInfo()
                                    .Info();

      if (hitTestInfo.size() > 1) {
        aSpecial++;
        continue;
      }

      if (hitTestInfo == CompositorHitTestFlags::eVisibleToHitTest) {
        aVisible++;
        continue;
      }

      aSpecial++;
    }
  }
}

void PrintHitTestInfoStats(nsDisplayList* aList) {
  int total = 0;
  int hitTest = 0;
  int visible = 0;
  int special = 0;

  PrintHitTestInfoStatsInternal(aList, total, hitTest, visible, special);

  double ratio = (double)hitTest / (double)total;

  printf(
      "List %p: total items: %d, hit test items: %d, ratio: %f, visible: %d, "
      "special: %d\n",
      aList, total, hitTest, ratio, visible, special);
}
#endif

static void DumpBeforePaintDisplayList(UniquePtr<std::stringstream>& aStream,
                                       nsDisplayListBuilder* aBuilder,
                                       nsDisplayList* aList,
                                       const nsRect& aVisibleRect) {
#ifdef MOZ_DUMP_PAINTING
  if (gfxEnv::MOZ_DUMP_PAINT_TO_FILE()) {
    nsCString string("dump-");
    // Include the process ID in the dump file name, to make sure that in an
    // e10s setup different processes don't clobber each other's dump files.
    string.AppendInt(getpid());
    for (int paintCount : *gPaintCountStack) {
      string.AppendLiteral("-");
      string.AppendInt(paintCount);
    }
    string.AppendLiteral(".html");
    gfxUtils::sDumpPaintFile = fopen(string.BeginReading(), "w");
  } else {
    gfxUtils::sDumpPaintFile = stderr;
  }
  if (gfxEnv::MOZ_DUMP_PAINT_TO_FILE()) {
    *aStream << "<html><head><script>\n"
                "var array = {};\n"
                "function ViewImage(index) { \n"
                "  var image = document.getElementById(index);\n"
                "  if (image.src) {\n"
                "    image.removeAttribute('src');\n"
                "  } else {\n"
                "    image.src = array[index];\n"
                "  }\n"
                "}</script></head><body>";
  }
#endif
  *aStream << nsPrintfCString(
                  "Painting --- before optimization (dirty %d,%d,%d,%d):\n",
                  aVisibleRect.x, aVisibleRect.y, aVisibleRect.width,
                  aVisibleRect.height)
                  .get();
  nsIFrame::PrintDisplayList(aBuilder, *aList, *aStream,
                             gfxEnv::MOZ_DUMP_PAINT_TO_FILE());

  if (gfxEnv::MOZ_DUMP_PAINT() || gfxEnv::MOZ_DUMP_PAINT_ITEMS()) {
    // Flush stream now to avoid reordering dump output relative to
    // messages dumped by PaintRoot below.
    fprint_stderr(gfxUtils::sDumpPaintFile, *aStream);
    aStream = MakeUnique<std::stringstream>();
  }
}

static void DumpAfterPaintDisplayList(UniquePtr<std::stringstream>& aStream,
                                      nsDisplayListBuilder* aBuilder,
                                      nsDisplayList* aList) {
  *aStream << "Painting --- after optimization:\n";
  nsIFrame::PrintDisplayList(aBuilder, *aList, *aStream,
                             gfxEnv::MOZ_DUMP_PAINT_TO_FILE());

  fprint_stderr(gfxUtils::sDumpPaintFile, *aStream);

#ifdef MOZ_DUMP_PAINTING
  if (gfxEnv::MOZ_DUMP_PAINT_TO_FILE()) {
    *aStream << "</body></html>";
  }
  if (gfxEnv::MOZ_DUMP_PAINT_TO_FILE()) {
    fclose(gfxUtils::sDumpPaintFile);
  }
#endif

  std::stringstream lsStream;
  nsIFrame::PrintDisplayList(aBuilder, *aList, lsStream);
}

struct TemporaryDisplayListBuilder {
  TemporaryDisplayListBuilder(nsIFrame* aFrame,
                              nsDisplayListBuilderMode aBuilderMode,
                              const bool aBuildCaret)
      : mBuilder(aFrame, aBuilderMode, aBuildCaret), mList(&mBuilder) {}

  ~TemporaryDisplayListBuilder() {
    mBuilder.SetIsDestroying();
    mList.DeleteAll(&mBuilder);
  }

  nsDisplayListBuilder mBuilder;
  nsDisplayList mList;
  RetainedDisplayListMetrics mMetrics;
};

void nsLayoutUtils::PaintFrame(gfxContext* aRenderingContext, nsIFrame* aFrame,
                               const nsRegion& aDirtyRegion, nscolor aBackstop,
                               nsDisplayListBuilderMode aBuilderMode,
                               PaintFrameFlags aFlags) {
  AUTO_PROFILER_LABEL("nsLayoutUtils::PaintFrame", GRAPHICS);

  // Create a static storage counter that is incremented on eacy entry to
  // PaintFrame and decremented on exit. We can use this later to determine if
  // this is a top-level paint.
  static uint32_t paintFrameDepth = 0;
  ++paintFrameDepth;

#ifdef MOZ_DUMP_PAINTING
  if (!gPaintCountStack) {
    gPaintCountStack = new nsTArray<int>();
    ClearOnShutdown(&gPaintCountStack);

    gPaintCountStack->AppendElement(0);
  }
  ++gPaintCountStack->LastElement();
  AutoNestedPaintCount nestedPaintCount;
#endif

  nsIFrame* displayRoot = GetDisplayRootFrame(aFrame);

  if (aFlags & PaintFrameFlags::WidgetLayers) {
    nsView* view = aFrame->GetView();
    if (!(view && view->GetWidget() && displayRoot == aFrame)) {
      aFlags &= ~PaintFrameFlags::WidgetLayers;
      NS_ASSERTION(aRenderingContext, "need a rendering context");
    }
  }

  nsPresContext* presContext = aFrame->PresContext();
  PresShell* presShell = presContext->PresShell();

  TimeStamp startBuildDisplayList = TimeStamp::Now();
  auto dlTimerId = mozilla::glean::paint::build_displaylist_time.Start();

  const bool buildCaret = !(aFlags & PaintFrameFlags::HideCaret);

  // Note that isForPainting here does not include the PaintForPrinting builder
  // mode; that's OK because there is no point in using retained display lists
  // for a print destination.
  const bool isForPainting = (aFlags & PaintFrameFlags::WidgetLayers) &&
                             aBuilderMode == nsDisplayListBuilderMode::Painting;

  // Only allow retaining for painting when preffed on, and for root frames
  // (since the modified frame tracking is per-root-frame).
  const bool retainDisplayList =
      isForPainting && AreRetainedDisplayListsEnabled() && !aFrame->GetParent();

  RetainedDisplayListBuilder* retainedBuilder = nullptr;
  Maybe<TemporaryDisplayListBuilder> temporaryBuilder;

  nsDisplayListBuilder* builder = nullptr;
  nsDisplayList* list = nullptr;
  RetainedDisplayListMetrics* metrics = nullptr;

  if (retainDisplayList) {
    MOZ_ASSERT(aFrame == displayRoot);
    retainedBuilder = aFrame->GetProperty(RetainedDisplayListBuilder::Cached());
    if (!retainedBuilder) {
      retainedBuilder =
          new RetainedDisplayListBuilder(aFrame, aBuilderMode, buildCaret);
      aFrame->SetProperty(RetainedDisplayListBuilder::Cached(),
                          retainedBuilder);
    }

    builder = retainedBuilder->Builder();
    list = retainedBuilder->List();
    metrics = retainedBuilder->Metrics();
  } else {
    temporaryBuilder.emplace(aFrame, aBuilderMode, buildCaret);
    builder = &temporaryBuilder->mBuilder;
    list = &temporaryBuilder->mList;
    metrics = &temporaryBuilder->mMetrics;
  }

  MOZ_ASSERT(builder && list && metrics);

  nsAutoString uri;
  if (MOZ_LOG_TEST(GetLoggerByProcess(), LogLevel::Info) ||
      MOZ_UNLIKELY(gfxUtils::DumpDisplayList()) ||
      MOZ_UNLIKELY(gfxEnv::MOZ_DUMP_PAINT())) {
    if (Document* doc = presContext->Document()) {
      Unused << doc->GetDocumentURI(uri);
    }
  }

  nsAutoString frameName, displayRootName;
#ifdef DEBUG_FRAME_DUMP
  if (MOZ_LOG_TEST(GetLoggerByProcess(), LogLevel::Info)) {
    aFrame->GetFrameName(frameName);
    displayRoot->GetFrameName(displayRootName);
  }
#endif

  DL_LOGI("PaintFrame: %p (%s), DisplayRoot: %p (%s), Builder: %p, URI: %s",
          aFrame, NS_ConvertUTF16toUTF8(frameName).get(), displayRoot,
          NS_ConvertUTF16toUTF8(displayRootName).get(), retainedBuilder,
          NS_ConvertUTF16toUTF8(uri).get());

  metrics->Reset();
  metrics->StartBuild();

  builder->BeginFrame();

  MOZ_ASSERT(paintFrameDepth >= 1);
  // If this is a top-level paint, increment the paint sequence number.
  if (paintFrameDepth == 1) {
    // Increment the paint sequence number for the display list builder.
    nsDisplayListBuilder::IncrementPaintSequenceNumber();
  }

  if (aFlags & PaintFrameFlags::InTransform) {
    builder->SetInTransform(true);
  }
  if (aFlags & PaintFrameFlags::SyncDecodeImages) {
    builder->SetSyncDecodeImages(true);
  }
  if (aFlags & (PaintFrameFlags::WidgetLayers | PaintFrameFlags::ToWindow)) {
    builder->SetPaintingToWindow(true);
  }
  if (aFlags & PaintFrameFlags::UseHighQualityScaling) {
    builder->SetUseHighQualityScaling(true);
  }
  if (aFlags & PaintFrameFlags::ForWebRender) {
    builder->SetPaintingForWebRender(true);
  }
  if (aFlags & PaintFrameFlags::IgnoreSuppression) {
    builder->IgnorePaintSuppression();
  }

  if (BrowsingContext* bc = presContext->Document()->GetBrowsingContext()) {
    builder->SetInActiveDocShell(bc->IsActive());
  }

  nsRect rootInkOverflow = aFrame->InkOverflowRectRelativeToSelf();

  // If the dynamic toolbar is completely collapsed, the visible rect should
  // be expanded to include this area.
  const bool hasDynamicToolbar =
      presContext->IsRootContentDocumentCrossProcess() &&
      presContext->HasDynamicToolbar();
  if (hasDynamicToolbar) {
    rootInkOverflow.SizeTo(nsLayoutUtils::ExpandHeightForDynamicToolbar(
        presContext, rootInkOverflow.Size()));
  }

  // If we are in a remote browser, then apply clipping from ancestor browsers
  if (BrowserChild* browserChild = BrowserChild::GetFrom(presShell)) {
    if (!browserChild->IsTopLevel()) {
      const nsRect unscaledVisibleRect =
          browserChild->GetVisibleRect().valueOr(nsRect());
      rootInkOverflow.IntersectRect(rootInkOverflow, unscaledVisibleRect);
    }
  }

  builder->ClearHaveScrollableDisplayPort();
  if (builder->IsPaintingToWindow() &&
      nsLayoutUtils::AsyncPanZoomEnabled(aFrame)) {
    DisplayPortUtils::MaybeCreateDisplayPortInFirstScrollFrameEncountered(
        aFrame, builder);
  }

  ScrollContainerFrame* rootScrollContainerFrame =
      presShell->GetRootScrollContainerFrame();
  if (rootScrollContainerFrame && !aFrame->GetParent()) {
    nsRect displayPortBase = rootInkOverflow;
    nsRect temp = displayPortBase;
    Unused << rootScrollContainerFrame->DecideScrollableLayer(
        builder, &displayPortBase, &temp,
        /* aSetBase = */ true);
  }

  // In the case where we use APZ for the given popup frame, we need to set the
  // displayport base.
  if (aFrame->IsMenuPopupFrame() &&
      nsLayoutUtils::AsyncPanZoomEnabled(aFrame) &&
      !DisplayPortUtils::HasDisplayPort(aFrame->GetContent())) {
    MOZ_ASSERT(XRE_IsParentProcess());
    APZCCallbackHelper::InitializeRootDisplayport(aFrame);
  }

  nsRegion visibleRegion;
  if (aFlags & PaintFrameFlags::WidgetLayers) {
    // This layer tree will be reused, so we'll need to calculate it
    // for the whole "visible" area of the window
    //
    // |ignoreViewportScrolling| and |usingDisplayPort| are persistent
    // document-rendering state.  We rely on PresShell to flush
    // retained layers as needed when that persistent state changes.
    visibleRegion = rootInkOverflow;
  } else {
    visibleRegion = aDirtyRegion;
  }

  Maybe<nsPoint> originalScrollPosition;
  auto maybeResetScrollPosition = MakeScopeExit([&]() {
    if (originalScrollPosition && rootScrollContainerFrame) {
      MOZ_ASSERT(rootScrollContainerFrame->GetScrolledFrame()->GetPosition() ==
                 nsPoint());
      rootScrollContainerFrame->GetScrolledFrame()->SetPosition(
          *originalScrollPosition);
    }
  });

  nsRect canvasArea(nsPoint(0, 0),
                    aFrame->InkOverflowRectRelativeToSelf().Size());
  bool ignoreViewportScrolling =
      !aFrame->GetParent() && presShell->IgnoringViewportScrolling();

  if (!aFrame->GetParent() && hasDynamicToolbar) {
    canvasArea.SizeTo(nsLayoutUtils::ExpandHeightForDynamicToolbar(
        presContext, canvasArea.Size()));
  }

  if (ignoreViewportScrolling && rootScrollContainerFrame) {
    if (aFlags & PaintFrameFlags::ResetViewportScrolling) {
      // Temporarily scroll the root scroll frame to 0,0 so that position:fixed
      // elements will appear fixed to the top-left of the document. We manually
      // set the position of the scrolled frame instead of using ScrollTo, since
      // the latter fires scroll listeners, which we don't want.
      originalScrollPosition.emplace(
          rootScrollContainerFrame->GetScrolledFrame()->GetPosition());
      rootScrollContainerFrame->GetScrolledFrame()->SetPosition(nsPoint());
    }
    if (aFlags & PaintFrameFlags::DocumentRelative) {
      // Make visibleRegion and aRenderingContext relative to the
      // scrolled frame instead of the root frame.
      nsPoint pos = rootScrollContainerFrame->GetScrollPosition();
      visibleRegion.MoveBy(-pos);
      if (aRenderingContext) {
        gfxPoint devPixelOffset = nsLayoutUtils::PointToGfxPoint(
            pos, presContext->AppUnitsPerDevPixel());
        aRenderingContext->SetMatrixDouble(
            aRenderingContext->CurrentMatrixDouble().PreTranslate(
                devPixelOffset));
      }
    }
    builder->SetIgnoreScrollFrame(rootScrollContainerFrame);

    nsCanvasFrame* canvasFrame =
        do_QueryFrame(rootScrollContainerFrame->GetScrolledFrame());
    if (canvasFrame) {
      // Use UnionRect here to ensure that areas where the scrollbars
      // were are still filled with the background color.
      canvasArea.UnionRect(
          canvasArea,
          canvasFrame->CanvasArea() + builder->ToReferenceFrame(canvasFrame));
    }
  }

  nsRect visibleRect = visibleRegion.GetBounds();
  PartialUpdateResult updateState = PartialUpdateResult::Failed;

  {
    AUTO_PROFILER_LABEL_CATEGORY_PAIR(GRAPHICS_DisplayListBuilding);
    AUTO_PROFILER_TRACING_MARKER("Paint", "DisplayList", GRAPHICS);
    PerfStats::AutoMetricRecording<PerfStats::Metric::DisplayListBuilding>
        autoRecording;

    ViewID id = ScrollableLayerGuid::NULL_SCROLL_ID;
    nsDisplayListBuilder::AutoCurrentActiveScrolledRootSetter asrSetter(
        builder);

    if (presShell->GetDocument() &&
        presShell->GetDocument()->IsRootDisplayDocument() &&
        !presShell->GetRootScrollContainerFrame()) {
      // In cases where the root document is a XUL document, we want to take
      // the ViewID from the root element, as that will be the ViewID of the
      // root APZC in the tree. Skip doing this in cases where we know
      // ScrollContainerFrame::BuilDisplayList will do it instead.
      if (dom::Element* element =
              presShell->GetDocument()->GetDocumentElement()) {
        id = nsLayoutUtils::FindOrCreateIDFor(element);
      }
      // In some cases we get a root document here on an APZ-enabled window
      // that doesn't have the root displayport initialized yet, even though
      // the ChromeProcessController is supposed to do it when the widget is
      // created. This can happen simply because the ChromeProcessController
      // does it on the next spin of the event loop, and we can trigger a
      // paint synchronously after window creation but before that runs. In
      // that case we should initialize the root displayport here before we do
      // the paint.
    } else if (XRE_IsParentProcess() && presContext->IsRoot() &&
               presShell->GetDocument() != nullptr &&
               presShell->GetRootScrollContainerFrame() != nullptr &&
               nsLayoutUtils::UsesAsyncScrolling(
                   presShell->GetRootScrollContainerFrame())) {
      if (dom::Element* element =
              presShell->GetDocument()->GetDocumentElement()) {
        if (!DisplayPortUtils::HasNonMinimalDisplayPort(element)) {
          APZCCallbackHelper::InitializeRootDisplayport(presShell);
        }
      }
    }

    asrSetter.SetCurrentScrollParentId(id);

    builder->SetVisibleRect(visibleRect);
    builder->SetIsBuilding(true);
    builder->SetAncestorHasApzAwareEventHandler(
        builder->BuildCompositorHitTestInfo() &&
        nsLayoutUtils::HasDocumentLevelListenersForApzAwareEvents(presShell));

    // If a pref is toggled that adds or removes display list items,
    // we need to rebuild the display list. The pref may be toggled
    // manually by the user, or during test setup.
    if (retainDisplayList &&
        !builder->ShouldRebuildDisplayListDueToPrefChange()) {
      // Attempt to do a partial build and merge into the existing list.
      // This calls BuildDisplayListForStacking context on a subset of the
      // viewport.
      updateState = retainedBuilder->AttemptPartialUpdate(aBackstop);
      metrics->EndPartialBuild(updateState);
    } else {
      // Partial updates are disabled.
      DL_LOGI("Partial updates are disabled");
      metrics->mPartialUpdateResult = PartialUpdateResult::Failed;
      metrics->mPartialUpdateFailReason = PartialUpdateFailReason::Disabled;
    }

    // Rebuild the full display list if the partial display list build failed.
    bool doFullRebuild = updateState == PartialUpdateResult::Failed;

    if (StaticPrefs::layout_display_list_build_twice()) {
      // Build display list twice to compare partial and full display list
      // build times.
      metrics->StartBuild();
      doFullRebuild = true;
    }

    if (doFullRebuild) {
      if (retainDisplayList) {
        retainedBuilder->ClearRetainedData();
#ifdef DEBUG
        mozilla::RDLUtils::AssertFrameSubtreeUnmodified(
            builder->RootReferenceFrame());
#endif
      }

      list->DeleteAll(builder);

      builder->ClearRetainedWindowRegions();
      builder->ClearWillChangeBudgets();

      builder->EnterPresShell(aFrame);
      builder->SetDirtyRect(visibleRect);

      DL_LOGI("Starting full display list build, root frame: %p",
              builder->RootReferenceFrame());

      aFrame->BuildDisplayListForStackingContext(builder, list);
      AddExtraBackgroundItems(builder, list, aFrame, canvasArea, visibleRegion,
                              aBackstop);

      builder->LeavePresShell(aFrame, list);
      metrics->EndFullBuild();

      DL_LOGI("Finished full display list build");
      updateState = PartialUpdateResult::Updated;
    }

    builder->SetIsBuilding(false);
    builder->IncrementPresShellPaintCount(presShell);
  }

  MOZ_ASSERT(updateState != PartialUpdateResult::Failed);
  builder->Check();

  const double geckoDLBuildTime =
      (TimeStamp::Now() - startBuildDisplayList).ToMilliseconds();
  mozilla::glean::paint::build_displaylist_time.StopAndAccumulate(
      std::move(dlTimerId));

  bool consoleNeedsDisplayList =
      (gfxUtils::DumpDisplayList() || gfxEnv::MOZ_DUMP_PAINT()) &&
      builder->IsInActiveDocShell();
#ifdef MOZ_DUMP_PAINTING
  FILE* savedDumpFile = gfxUtils::sDumpPaintFile;
#endif

  UniquePtr<std::stringstream> ss;
  if (consoleNeedsDisplayList) {
    ss = MakeUnique<std::stringstream>();
    *ss << "Display list for " << uri << "\n";
    DumpBeforePaintDisplayList(ss, builder, list, visibleRect);
  }

  uint32_t flags = nsDisplayList::PAINT_DEFAULT;
  if (aFlags & PaintFrameFlags::WidgetLayers) {
    flags |= nsDisplayList::PAINT_USE_WIDGET_LAYERS;
    if (!(aFlags & PaintFrameFlags::DocumentRelative)) {
      nsIWidget* widget = aFrame->GetNearestWidget();
      if (widget) {
        // If we're finished building display list items for painting of the
        // outermost pres shell, notify the widget about any toolbars we've
        // encountered.
        widget->UpdateThemeGeometries(builder->GetThemeGeometries());
      }
    }
  }
  if (aFlags & PaintFrameFlags::ExistingTransaction) {
    flags |= nsDisplayList::PAINT_EXISTING_TRANSACTION;
  }
  if (updateState == PartialUpdateResult::NoChange && !aRenderingContext) {
    flags |= nsDisplayList::PAINT_IDENTICAL_DISPLAY_LIST;
  }

#ifdef PRINT_HITTESTINFO_STATS
  if (XRE_IsContentProcess()) {
    PrintHitTestInfoStats(list);
  }
#endif
  if (aFlags & PaintFrameFlags::CompositeOffscreen) {
    flags |= nsDisplayList::PAINT_COMPOSITE_OFFSCREEN;
  }

  TimeStamp paintStart = TimeStamp::Now();
  list->PaintRoot(builder, aRenderingContext, flags, Some(geckoDLBuildTime));
  glean::layout::paint_rasterize_time.AccumulateRawDuration(TimeStamp::Now() -
                                                            paintStart);

  if (builder->IsPaintingToWindow()) {
    presShell->EndPaint();
  }
  builder->Check();

  if (consoleNeedsDisplayList) {
    DumpAfterPaintDisplayList(ss, builder, list);
  }

#ifdef MOZ_DUMP_PAINTING
  gfxUtils::sDumpPaintFile = savedDumpFile;
#endif

  // Update the widget's opaque region information. This sets
  // glass boundaries on Windows. Also set up the window dragging region.
  if ((aFlags & PaintFrameFlags::WidgetLayers) &&
      !(aFlags & PaintFrameFlags::DocumentRelative)) {
    if (nsIWidget* widget = aFrame->GetNearestWidget()) {
      const nsRegion& opaqueRegion = builder->GetWindowOpaqueRegion();
      widget->UpdateOpaqueRegion(LayoutDeviceIntRegion::FromUnknownRegion(
          opaqueRegion.ToNearestPixels(presContext->AppUnitsPerDevPixel())));
      widget->UpdateWindowDraggingRegion(builder->GetWindowDraggingRegion());
    }
  }

  builder->Check();

  {
    AUTO_PROFILER_TRACING_MARKER("Paint", "DisplayListResources", GRAPHICS);

    builder->EndFrame();

    if (temporaryBuilder) {
      temporaryBuilder.reset();
    }
  }

  --paintFrameDepth;
#if 0
  if (XRE_IsParentProcess()) {
    if (metrics->mPartialUpdateResult == PartialUpdateResult::Failed) {
      printf("DL partial update failed: %s, Frame: %p\n",
             metrics->FailReasonString(), aFrame);
    } else {
      printf(
          "DL partial build success!"
          " new: %d, reused: %d, rebuilt: %d, removed: %d, total: %d\n",
          metrics->mNewItems, metrics->mReusedItems, metrics->mRebuiltItems,
          metrics->mRemovedItems, metrics->mTotalItems);
    }
  }
#endif
}

/**
 * Uses a binary search for find where the cursor falls in the line of text
 * It also keeps track of the part of the string that has already been measured
 * so it doesn't have to keep measuring the same text over and over
 *
 * @param "aBaseWidth" contains the width in twips of the portion
 * of the text that has already been measured, and aBaseInx contains
 * the index of the text that has already been measured.
 *
 * @param aTextWidth returns the (in twips) the length of the text that falls
 * before the cursor aIndex contains the index of the text where the cursor
 * falls
 */
bool nsLayoutUtils::BinarySearchForPosition(
    DrawTarget* aDrawTarget, nsFontMetrics& aFontMetrics, const char16_t* aText,
    int32_t aBaseWidth, int32_t aBaseInx, int32_t aStartInx, int32_t aEndInx,
    int32_t aCursorPos, int32_t& aIndex, int32_t& aTextWidth) {
  int32_t range = aEndInx - aStartInx;
  if ((range == 1) || (range == 2 && NS_IS_HIGH_SURROGATE(aText[aStartInx]))) {
    aIndex = aStartInx + aBaseInx;
    aTextWidth = nsLayoutUtils::AppUnitWidthOfString(aText, aIndex,
                                                     aFontMetrics, aDrawTarget);
    return true;
  }

  int32_t inx = aStartInx + (range / 2);

  // Make sure we don't leave a dangling low surrogate
  if (NS_IS_HIGH_SURROGATE(aText[inx - 1])) {
    inx++;
  }

  int32_t textWidth = nsLayoutUtils::AppUnitWidthOfString(
      aText, inx, aFontMetrics, aDrawTarget);

  int32_t fullWidth = aBaseWidth + textWidth;
  if (fullWidth == aCursorPos) {
    aTextWidth = textWidth;
    aIndex = inx;
    return true;
  } else if (aCursorPos < fullWidth) {
    aTextWidth = aBaseWidth;
    if (BinarySearchForPosition(aDrawTarget, aFontMetrics, aText, aBaseWidth,
                                aBaseInx, aStartInx, inx, aCursorPos, aIndex,
                                aTextWidth)) {
      return true;
    }
  } else {
    aTextWidth = fullWidth;
    if (BinarySearchForPosition(aDrawTarget, aFontMetrics, aText, aBaseWidth,
                                aBaseInx, inx, aEndInx, aCursorPos, aIndex,
                                aTextWidth)) {
      return true;
    }
  }
  return false;
}

void nsLayoutUtils::AddBoxesForFrame(nsIFrame* aFrame,
                                     nsLayoutUtils::BoxCallback* aCallback) {
  auto pseudoType = aFrame->Style()->GetPseudoType();

  if (pseudoType == PseudoStyleType::tableWrapper) {
    AddBoxesForFrame(aFrame->PrincipalChildList().FirstChild(), aCallback);
    if (aCallback->mIncludeCaptionBoxForTable) {
      nsIFrame* kid =
          aFrame->GetChildList(FrameChildListID::Caption).FirstChild();
      if (kid) {
        AddBoxesForFrame(kid, aCallback);
      }
    }
  } else if (pseudoType == PseudoStyleType::mozBlockInsideInlineWrapper ||
             pseudoType == PseudoStyleType::mozMathMLAnonymousBlock) {
    for (nsIFrame* kid : aFrame->PrincipalChildList()) {
      AddBoxesForFrame(kid, aCallback);
    }
  } else {
    aCallback->AddBox(aFrame);
  }
}

void nsLayoutUtils::GetAllInFlowBoxes(nsIFrame* aFrame,
                                      BoxCallback* aCallback) {
  aCallback->mInTargetContinuation = false;
  while (aFrame) {
    AddBoxesForFrame(aFrame, aCallback);
    aFrame = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(aFrame);
    aCallback->mInTargetContinuation = true;
  }
}

nsIFrame* nsLayoutUtils::GetFirstNonAnonymousFrame(nsIFrame* aFrame) {
  while (aFrame) {
    auto pseudoType = aFrame->Style()->GetPseudoType();

    if (pseudoType == PseudoStyleType::tableWrapper) {
      nsIFrame* f =
          GetFirstNonAnonymousFrame(aFrame->PrincipalChildList().FirstChild());
      if (f) {
        return f;
      }
      nsIFrame* kid =
          aFrame->GetChildList(FrameChildListID::Caption).FirstChild();
      if (kid) {
        f = GetFirstNonAnonymousFrame(kid);
        if (f) {
          return f;
        }
      }
    } else if (pseudoType == PseudoStyleType::mozBlockInsideInlineWrapper ||
               pseudoType == PseudoStyleType::mozMathMLAnonymousBlock) {
      for (nsIFrame* kid : aFrame->PrincipalChildList()) {
        nsIFrame* f = GetFirstNonAnonymousFrame(kid);
        if (f) {
          return f;
        }
      }
    } else {
      return aFrame;
    }

    aFrame = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(aFrame);
  }
  return nullptr;
}

struct BoxToRect : public nsLayoutUtils::BoxCallback {
  const nsIFrame* mRelativeTo;
  RectCallback* mCallback;
  nsLayoutUtils::GetAllInFlowRectsFlags mFlags;
  // If the frame we're measuring relative to is the root, we know all frames
  // are descendants of it, so we don't need to compute the common ancestor
  // between a frame and mRelativeTo.
  bool mRelativeToIsRoot;
  // For the same reason, if the frame we're measuring relative to is the target
  // (this is useful for IntersectionObserver), we know all frames are
  // descendants of it except if we're in a continuation or ib-split-sibling of
  // it.
  bool mRelativeToIsTarget;

  BoxToRect(const nsIFrame* aTargetFrame, const nsIFrame* aRelativeTo,
            RectCallback* aCallback,
            nsLayoutUtils::GetAllInFlowRectsFlags aFlags)
      : mRelativeTo(aRelativeTo),
        mCallback(aCallback),
        mFlags(aFlags),
        mRelativeToIsRoot(!aRelativeTo->GetParent()),
        mRelativeToIsTarget(aRelativeTo == aTargetFrame) {}

  void AddBox(nsIFrame* aFrame) override {
    nsRect r;
    nsIFrame* outer = SVGUtils::GetOuterSVGFrameAndCoveredRegion(aFrame, &r);
    const bool usingSVGOuterFrame = !!outer;
    if (!outer) {
      outer = aFrame;
      if (mFlags.contains(
              nsLayoutUtils::GetAllInFlowRectsFlag::UseContentBox)) {
        r = aFrame->GetContentRectRelativeToSelf();
      } else if (mFlags.contains(
                     nsLayoutUtils::GetAllInFlowRectsFlag::UsePaddingBox)) {
        r = aFrame->GetPaddingRectRelativeToSelf();
      } else if (mFlags.contains(
                     nsLayoutUtils::GetAllInFlowRectsFlag::UseMarginBox)) {
        r = aFrame->GetMarginRectRelativeToSelf();
      } else if (mFlags.contains(nsLayoutUtils::GetAllInFlowRectsFlag::
                                     UseMarginBoxWithAutoResolvedAsZero)) {
        r = aFrame->GetRectRelativeToSelf();
        nsMargin usedMargin =
            aFrame->GetUsedMargin().ApplySkipSides(aFrame->GetSkipSides());
        const auto* styleMargin = aFrame->StyleMargin();
        const auto positionProperty = aFrame->StyleDisplay()->mPosition;
        for (const Side side : AllPhysicalSides()) {
          if (styleMargin->GetMargin(side, positionProperty)->IsAuto()) {
            usedMargin.Side(side) = 0;
          }
        }
        r.Inflate(usedMargin);
      } else {
        // Use the border-box.
        r = aFrame->GetRectRelativeToSelf();
      }
    }
    if (outer != mRelativeTo) {
      if (mFlags.contains(
              nsLayoutUtils::GetAllInFlowRectsFlag::AccountForTransforms)) {
        const bool isAncestorKnown = [&] {
          if (mRelativeToIsRoot) {
            return true;
          }
          if (mRelativeToIsTarget && !mInTargetContinuation) {
            return !usingSVGOuterFrame;
          }
          return false;
        }();
        if (isAncestorKnown) {
          r = nsLayoutUtils::TransformFrameRectToAncestor(outer, r,
                                                          mRelativeTo);
        } else {
          nsLayoutUtils::TransformRect(outer, mRelativeTo, r);
        }
      } else {
        if (aFrame->PresContext() != mRelativeTo->PresContext()) {
          r += outer->GetOffsetToCrossDoc(mRelativeTo);
        } else {
          r += outer->GetOffsetTo(mRelativeTo);
        }
      }
    }
    mCallback->AddRect(r);
  }
};

struct MOZ_RAII BoxToRectAndText : public BoxToRect {
  Sequence<nsString>* mTextList;

  BoxToRectAndText(const nsIFrame* aTargetFrame, const nsIFrame* aRelativeTo,
                   RectCallback* aCallback, Sequence<nsString>* aTextList,
                   nsLayoutUtils::GetAllInFlowRectsFlags aFlags)
      : BoxToRect(aTargetFrame, aRelativeTo, aCallback, aFlags),
        mTextList(aTextList) {}

  static void AccumulateText(nsIFrame* aFrame, nsAString& aResult) {
    MOZ_ASSERT(aFrame);

    // Get all the text in aFrame and child frames, while respecting
    // the content offsets in each of the nsTextFrames.
    if (aFrame->IsTextFrame()) {
      nsTextFrame* textFrame = static_cast<nsTextFrame*>(aFrame);

      nsIFrame::RenderedText renderedText = textFrame->GetRenderedText(
          textFrame->GetContentOffset(),
          textFrame->GetContentOffset() + textFrame->GetContentLength(),
          nsIFrame::TextOffsetType::OffsetsInContentText,
          nsIFrame::TrailingWhitespace::DontTrim);

      aResult.Append(renderedText.mString);
    }

    for (nsIFrame* child = aFrame->PrincipalChildList().FirstChild(); child;
         child = child->GetNextSibling()) {
      AccumulateText(child, aResult);
    }
  }

  void AddBox(nsIFrame* aFrame) override {
    BoxToRect::AddBox(aFrame);
    if (mTextList) {
      nsString* textForFrame = mTextList->AppendElement(fallible);
      if (textForFrame) {
        AccumulateText(aFrame, *textForFrame);
      }
    }
  }
};

void nsLayoutUtils::GetAllInFlowRects(nsIFrame* aFrame,
                                      const nsIFrame* aRelativeTo,
                                      RectCallback* aCallback,
                                      GetAllInFlowRectsFlags aFlags) {
  BoxToRect converter(aFrame, aRelativeTo, aCallback, aFlags);
  GetAllInFlowBoxes(aFrame, &converter);
}

void nsLayoutUtils::GetAllInFlowRectsAndTexts(nsIFrame* aFrame,
                                              const nsIFrame* aRelativeTo,
                                              RectCallback* aCallback,
                                              Sequence<nsString>* aTextList,
                                              GetAllInFlowRectsFlags aFlags) {
  BoxToRectAndText converter(aFrame, aRelativeTo, aCallback, aTextList, aFlags);
  GetAllInFlowBoxes(aFrame, &converter);
}

nsLayoutUtils::RectAccumulator::RectAccumulator() : mSeenFirstRect(false) {}

void nsLayoutUtils::RectAccumulator::AddRect(const nsRect& aRect) {
  mResultRect.UnionRect(mResultRect, aRect);
  if (!mSeenFirstRect) {
    mSeenFirstRect = true;
    mFirstRect = aRect;
  }
}

nsLayoutUtils::RectListBuilder::RectListBuilder(DOMRectList* aList)
    : mRectList(aList) {}

void nsLayoutUtils::RectListBuilder::AddRect(const nsRect& aRect) {
  auto rect = MakeRefPtr<DOMRect>(mRectList);

  rect->SetLayoutRect(aRect);
  mRectList->Append(std::move(rect));
}

nsIFrame* nsLayoutUtils::GetContainingBlockForClientRect(nsIFrame* aFrame) {
  return aFrame->PresShell()->GetRootFrame();
}

nsRect nsLayoutUtils::GetAllInFlowRectsUnion(nsIFrame* aFrame,
                                             const nsIFrame* aRelativeTo,
                                             GetAllInFlowRectsFlags aFlags) {
  RectAccumulator accumulator;
  GetAllInFlowRects(aFrame, aRelativeTo, &accumulator, aFlags);
  return accumulator.mResultRect.IsEmpty() ? accumulator.mFirstRect
                                           : accumulator.mResultRect;
}

nsRect nsLayoutUtils::GetTextShadowRectsUnion(
    const nsRect& aTextAndDecorationsRect, nsIFrame* aFrame, uint32_t aFlags) {
  const nsStyleText* textStyle = aFrame->StyleText();
  auto shadows = textStyle->mTextShadow.AsSpan();
  if (shadows.IsEmpty()) {
    return aTextAndDecorationsRect;
  }

  nsRect resultRect = aTextAndDecorationsRect;
  int32_t A2D = aFrame->PresContext()->AppUnitsPerDevPixel();
  for (auto& shadow : shadows) {
    nsMargin blur =
        nsContextBoxBlur::GetBlurRadiusMargin(shadow.blur.ToAppUnits(), A2D);
    if ((aFlags & EXCLUDE_BLUR_SHADOWS) && blur != nsMargin(0, 0, 0, 0)) {
      continue;
    }

    nsRect tmpRect(aTextAndDecorationsRect);

    tmpRect.MoveBy(
        nsPoint(shadow.horizontal.ToAppUnits(), shadow.vertical.ToAppUnits()));
    tmpRect.Inflate(blur);

    resultRect.UnionRect(resultRect, tmpRect);
  }
  return resultRect;
}

enum ObjectDimensionType { eWidth, eHeight };
static nscoord ComputeMissingDimension(
    const nsSize& aDefaultObjectSize, const AspectRatio& aIntrinsicRatio,
    const Maybe<nscoord>& aSpecifiedWidth,
    const Maybe<nscoord>& aSpecifiedHeight,
    ObjectDimensionType aDimensionToCompute) {
  // The "default sizing algorithm" computes the missing dimension as follows:
  // (source: http://dev.w3.org/csswg/css-images-3/#default-sizing )

  // 1. "If the object has an intrinsic aspect ratio, the missing dimension of
  //     the concrete object size is calculated using the intrinsic aspect
  //     ratio and the present dimension."
  if (aIntrinsicRatio) {
    // Fill in the missing dimension using the intrinsic aspect ratio.
    if (aDimensionToCompute == eWidth) {
      return aIntrinsicRatio.ApplyTo(*aSpecifiedHeight);
    }
    return aIntrinsicRatio.Inverted().ApplyTo(*aSpecifiedWidth);
  }

  // 2. "Otherwise, if the missing dimension is present in the object's
  //     intrinsic dimensions, [...]"
  // NOTE: *Skipping* this case, because we already know it's not true -- we're
  // in this function because the missing dimension is *not* present in
  // the object's intrinsic dimensions.

  // 3. "Otherwise, the missing dimension of the concrete object size is taken
  //     from the default object size. "
  return (aDimensionToCompute == eWidth) ? aDefaultObjectSize.width
                                         : aDefaultObjectSize.height;
}

/*
 * This computes & returns the concrete object size of replaced content, if
 * that content were to be rendered with "object-fit: none".  (Or, if the
 * element has neither an intrinsic height nor width, this method returns an
 * empty Maybe<> object.)
 *
 * As specced...
 *   http://dev.w3.org/csswg/css-images-3/#valdef-object-fit-none
 * ..we use "the default sizing algorithm with no specified size,
 * and a default object size equal to the replaced element's used width and
 * height."
 *
 * The default sizing algorithm is described here:
 *   http://dev.w3.org/csswg/css-images-3/#default-sizing
 * Quotes in the function-impl are taken from that ^ spec-text.
 *
 * Per its final bulleted section: since there's no specified size,
 * we run the default sizing algorithm using the object's intrinsic size in
 * place of the specified size. But if the object has neither an intrinsic
 * height nor an intrinsic width, then we instead return without populating our
 * outparam, and we let the caller figure out the size (using a contain
 * constraint).
 */
static Maybe<nsSize> MaybeComputeObjectFitNoneSize(
    const nsSize& aDefaultObjectSize, const IntrinsicSize& aIntrinsicSize,
    const AspectRatio& aIntrinsicRatio) {
  // "If the object has an intrinsic height or width, its size is resolved as
  // if its intrinsic dimensions were given as the specified size."
  //
  // So, first we check if we have an intrinsic height and/or width:
  const Maybe<nscoord>& specifiedWidth = aIntrinsicSize.width;
  const Maybe<nscoord>& specifiedHeight = aIntrinsicSize.height;

  Maybe<nsSize> noneSize;  // (the value we'll return)
  if (specifiedWidth || specifiedHeight) {
    // We have at least one specified dimension; use whichever dimension is
    // specified, and compute the other one using our intrinsic ratio, or (if
    // no valid ratio) using the default object size.
    noneSize.emplace();

    noneSize->width =
        specifiedWidth
            ? *specifiedWidth
            : ComputeMissingDimension(aDefaultObjectSize, aIntrinsicRatio,
                                      specifiedWidth, specifiedHeight, eWidth);

    noneSize->height =
        specifiedHeight
            ? *specifiedHeight
            : ComputeMissingDimension(aDefaultObjectSize, aIntrinsicRatio,
                                      specifiedWidth, specifiedHeight, eHeight);
  }
  // [else:] "Otherwise [if there's neither an intrinsic height nor width], its
  // size is resolved as a contain constraint against the default object size."
  // We'll let our caller do that, to share code & avoid redundant
  // computations; so, we return w/out populating noneSize.
  return noneSize;
}

// Computes the concrete object size to render into, as described at
// http://dev.w3.org/csswg/css-images-3/#concrete-size-resolution
static nsSize ComputeConcreteObjectSize(const nsSize& aConstraintSize,
                                        const IntrinsicSize& aIntrinsicSize,
                                        const AspectRatio& aIntrinsicRatio,
                                        StyleObjectFit aObjectFit) {
  // Handle default behavior (filling the container) w/ fast early return.
  // (Also: if there's no valid intrinsic ratio, then we have the "fill"
  // behavior & just use the constraint size.)
  if (MOZ_LIKELY(aObjectFit == StyleObjectFit::Fill) || !aIntrinsicRatio) {
    return aConstraintSize;
  }

  // The type of constraint to compute (cover/contain), if needed:
  Maybe<nsImageRenderer::FitType> fitType;

  Maybe<nsSize> noneSize;
  if (aObjectFit == StyleObjectFit::None ||
      aObjectFit == StyleObjectFit::ScaleDown) {
    noneSize = MaybeComputeObjectFitNoneSize(aConstraintSize, aIntrinsicSize,
                                             aIntrinsicRatio);
    if (!noneSize || aObjectFit == StyleObjectFit::ScaleDown) {
      // Need to compute a 'CONTAIN' constraint (either for the 'none' size
      // itself, or for comparison w/ the 'none' size to resolve 'scale-down'.)
      fitType.emplace(nsImageRenderer::CONTAIN);
    }
  } else if (aObjectFit == StyleObjectFit::Cover) {
    fitType.emplace(nsImageRenderer::COVER);
  } else if (aObjectFit == StyleObjectFit::Contain) {
    fitType.emplace(nsImageRenderer::CONTAIN);
  }

  Maybe<nsSize> constrainedSize;
  if (fitType) {
    constrainedSize.emplace(nsImageRenderer::ComputeConstrainedSize(
        aConstraintSize, aIntrinsicRatio, *fitType));
  }

  // Now, we should have all the sizing information that we need.
  switch (aObjectFit) {
    // skipping StyleObjectFit::Fill; we handled it w/ early-return.
    case StyleObjectFit::Contain:
    case StyleObjectFit::Cover:
      MOZ_ASSERT(constrainedSize);
      return *constrainedSize;

    case StyleObjectFit::None:
      if (noneSize) {
        return *noneSize;
      }
      MOZ_ASSERT(constrainedSize);
      return *constrainedSize;

    case StyleObjectFit::ScaleDown:
      MOZ_ASSERT(constrainedSize);
      if (noneSize) {
        constrainedSize->width =
            std::min(constrainedSize->width, noneSize->width);
        constrainedSize->height =
            std::min(constrainedSize->height, noneSize->height);
      }
      return *constrainedSize;

    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected enum value for 'object-fit'");
      return aConstraintSize;  // fall back to (default) 'fill' behavior
  }
}

// (Helper for HasInitialObjectFitAndPosition, to check
// each "object-position" coord.)
static bool IsCoord50Pct(const LengthPercentage& aCoord) {
  return aCoord.ConvertsToPercentage() && aCoord.ToPercentage() == 0.5f;
}

// Indicates whether the given nsStylePosition has the initial values
// for the "object-fit" and "object-position" properties.
static bool HasInitialObjectFitAndPosition(const nsStylePosition* aStylePos) {
  const Position& objectPos = aStylePos->mObjectPosition;

  return aStylePos->mObjectFit == StyleObjectFit::Fill &&
         IsCoord50Pct(objectPos.horizontal) && IsCoord50Pct(objectPos.vertical);
}

/* static */
nsRect nsLayoutUtils::ComputeObjectDestRect(const nsRect& aConstraintRect,
                                            const IntrinsicSize& aIntrinsicSize,
                                            const AspectRatio& aIntrinsicRatio,
                                            const nsStylePosition* aStylePos,
                                            nsPoint* aAnchorPoint) {
  // Step 1: Figure out our "concrete object size"
  // (the size of the region we'll actually draw our image's pixels into).
  nsSize concreteObjectSize =
      ComputeConcreteObjectSize(aConstraintRect.Size(), aIntrinsicSize,
                                aIntrinsicRatio, aStylePos->mObjectFit);

  // Step 2: Figure out how to align that region in the element's content-box.
  nsPoint imageTopLeftPt, imageAnchorPt;
  nsImageRenderer::ComputeObjectAnchorPoint(
      aStylePos->mObjectPosition, aConstraintRect.Size(), concreteObjectSize,
      &imageTopLeftPt, &imageAnchorPt);
  // Right now, we're with respect to aConstraintRect's top-left point.  We add
  // that point here, to convert to the same broader coordinate space that
  // aConstraintRect is in.
  imageTopLeftPt += aConstraintRect.TopLeft();
  imageAnchorPt += aConstraintRect.TopLeft();

  if (aAnchorPoint) {
    // Special-case: if our "object-fit" and "object-position" properties have
    // their default values ("object-fit: fill; object-position:50% 50%"), then
    // we'll override the calculated imageAnchorPt, and instead use the
    // object's top-left corner.
    //
    // This special case is partly for backwards compatibility (since
    // traditionally we've pixel-aligned the top-left corner of e.g. <img>
    // elements), and partly because ComputeSnappedDrawingParameters produces
    // less error if the anchor point is at the top-left corner. So, all other
    // things being equal, we prefer that code path with less error.
    if (HasInitialObjectFitAndPosition(aStylePos)) {
      *aAnchorPoint = imageTopLeftPt;
    } else {
      *aAnchorPoint = imageAnchorPt;
    }
  }
  return nsRect(imageTopLeftPt, concreteObjectSize);
}

already_AddRefed<nsFontMetrics> nsLayoutUtils::GetFontMetricsForFrame(
    const nsIFrame* aFrame, float aInflation) {
  ComputedStyle* computedStyle = aFrame->Style();
  uint8_t variantWidth = NS_FONT_VARIANT_WIDTH_NORMAL;
  if (computedStyle->IsTextCombined()) {
    MOZ_ASSERT(aFrame->IsTextFrame());
    auto textFrame = static_cast<const nsTextFrame*>(aFrame);
    auto clusters = textFrame->CountGraphemeClusters();
    if (clusters == 2) {
      variantWidth = NS_FONT_VARIANT_WIDTH_HALF;
    } else if (clusters == 3) {
      variantWidth = NS_FONT_VARIANT_WIDTH_THIRD;
    } else if (clusters == 4) {
      variantWidth = NS_FONT_VARIANT_WIDTH_QUARTER;
    }
  }
  return GetFontMetricsForComputedStyle(computedStyle, aFrame->PresContext(),
                                        aInflation, variantWidth);
}

already_AddRefed<nsFontMetrics> nsLayoutUtils::GetFontMetricsForComputedStyle(
    const ComputedStyle* aComputedStyle, nsPresContext* aPresContext,
    float aInflation, uint8_t aVariantWidth) {
  WritingMode wm(aComputedStyle);
  const nsStyleFont* styleFont = aComputedStyle->StyleFont();
  nsFontMetrics::Params params;
  params.language = styleFont->mLanguage;
  params.explicitLanguage = styleFont->mExplicitLanguage;
  params.orientation = wm.IsVertical() && !wm.IsSideways()
                           ? nsFontMetrics::eVertical
                           : nsFontMetrics::eHorizontal;
  // pass the user font set object into the device context to
  // pass along to CreateFontGroup
  params.userFontSet = aPresContext->GetUserFontSet();
  params.textPerf = aPresContext->GetTextPerfMetrics();
  params.featureValueLookup = aPresContext->GetFontFeatureValuesLookup();

  // When aInflation is 1.0 and we don't require width variant, avoid
  // making a local copy of the nsFont.
  // This also avoids running font.size through floats when it is large,
  // which would be lossy.  Fortunately, in such cases, aInflation is
  // guaranteed to be 1.0f.
  if (aInflation == 1.0f && aVariantWidth == NS_FONT_VARIANT_WIDTH_NORMAL) {
    return aPresContext->GetMetricsFor(styleFont->mFont, params);
  }

  nsFont font = styleFont->mFont;
  MOZ_ASSERT(!std::isnan(float(font.size.ToCSSPixels())),
             "Style font should never be NaN");
  font.size.ScaleBy(aInflation);
  if (MOZ_UNLIKELY(std::isnan(float(font.size.ToCSSPixels())))) {
    font.size = {0};
  }
  font.variantWidth = aVariantWidth;
  return aPresContext->GetMetricsFor(font, params);
}

nsIFrame* nsLayoutUtils::FindChildContainingDescendant(
    nsIFrame* aParent, nsIFrame* aDescendantFrame) {
  nsIFrame* result = aDescendantFrame;

  while (result) {
    nsIFrame* parent = result->GetParent();
    if (parent == aParent) {
      break;
    }

    // The frame is not an immediate child of aParent so walk up another level
    result = parent;
  }

  return result;
}

nsBlockFrame* nsLayoutUtils::FindNearestBlockAncestor(nsIFrame* aFrame) {
  nsIFrame* nextAncestor;
  for (nextAncestor = aFrame->GetParent(); nextAncestor;
       nextAncestor = nextAncestor->GetParent()) {
    nsBlockFrame* block = do_QueryFrame(nextAncestor);
    if (block) {
      return block;
    }
  }
  return nullptr;
}

nsIFrame* nsLayoutUtils::GetParentOrPlaceholderFor(const nsIFrame* aFrame) {
  // This condition must match the condition in FindContainingBlocks in
  // RetainedDisplayListBuider.cpp, MarkFrameForDisplayIfVisible and
  // UnmarkFrameForDisplayIfVisible in nsDisplayList.cpp
  if (aFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) &&
      !aFrame->GetPrevInFlow()) {
    return aFrame->GetProperty(nsIFrame::PlaceholderFrameProperty());
  }
  return aFrame->GetParent();
}

nsIFrame* nsLayoutUtils::GetParentOrPlaceholderForCrossDoc(
    const nsIFrame* aFrame) {
  nsIFrame* f = GetParentOrPlaceholderFor(aFrame);
  if (f) {
    return f;
  }
  return GetCrossDocParentFrameInProcess(aFrame);
}

nsIFrame* nsLayoutUtils::GetDisplayListParent(nsIFrame* aFrame) {
  if (aFrame->HasAnyStateBits(NS_FRAME_IS_PUSHED_FLOAT)) {
    return aFrame->GetParent();
  }
  return nsLayoutUtils::GetParentOrPlaceholderForCrossDoc(aFrame);
}

nsIFrame* nsLayoutUtils::GetPrevContinuationOrIBSplitSibling(
    const nsIFrame* aFrame) {
  if (nsIFrame* result = aFrame->GetPrevContinuation()) {
    return result;
  }

  if (aFrame->HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT)) {
    // We are the first frame in the continuation chain. Get the ib-split prev
    // sibling property stored in us.
    return aFrame->GetProperty(nsIFrame::IBSplitPrevSibling());
  }

  return nullptr;
}

nsIFrame* nsLayoutUtils::GetNextContinuationOrIBSplitSibling(
    const nsIFrame* aFrame) {
  if (nsIFrame* result = aFrame->GetNextContinuation()) {
    return result;
  }

  if (aFrame->HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT)) {
    // We only store the ib-split sibling annotation with the first frame in the
    // continuation chain.
    return aFrame->FirstContinuation()->GetProperty(nsIFrame::IBSplitSibling());
  }

  return nullptr;
}

nsIFrame* nsLayoutUtils::FirstContinuationOrIBSplitSibling(
    const nsIFrame* aFrame) {
  nsIFrame* result = aFrame->FirstContinuation();

  if (result->HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT)) {
    while (auto* f = result->GetProperty(nsIFrame::IBSplitPrevSibling())) {
      result = f;
    }
  }

  return result;
}

nsIFrame* nsLayoutUtils::LastContinuationOrIBSplitSibling(
    const nsIFrame* aFrame) {
  nsIFrame* result = aFrame->FirstContinuation();

  if (result->HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT)) {
    while (auto* f = result->GetProperty(nsIFrame::IBSplitSibling())) {
      result = f;
    }
  }

  return result->LastContinuation();
}

bool nsLayoutUtils::IsFirstContinuationOrIBSplitSibling(
    const nsIFrame* aFrame) {
  if (aFrame->GetPrevContinuation()) {
    return false;
  }
  if (aFrame->HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT) &&
      aFrame->GetProperty(nsIFrame::IBSplitPrevSibling())) {
    return false;
  }

  return true;
}

bool nsLayoutUtils::IsViewportScrollbarFrame(nsIFrame* aFrame) {
  if (!aFrame) {
    return false;
  }

  ScrollContainerFrame* rootScrollContainerFrame =
      aFrame->PresShell()->GetRootScrollContainerFrame();
  if (!rootScrollContainerFrame) {
    return false;
  }

  if (!IsProperAncestorFrame(rootScrollContainerFrame, aFrame)) {
    return false;
  }

  nsIFrame* rootScrolledFrame = rootScrollContainerFrame->GetScrolledFrame();
  return !(rootScrolledFrame == aFrame ||
           IsProperAncestorFrame(rootScrolledFrame, aFrame));
}

static nscoord GetBSizePercentBasisAdjustment(StyleBoxSizing aBoxSizing,
                                              nsIFrame* aFrame,
                                              bool aHorizontalAxis,
                                              bool aResolvesAgainstPaddingBox);

static Maybe<nscoord> GetPercentBSize(const LengthPercentage& aSize,
                                      nsIFrame* aFrame, bool aHorizontalAxis);

// Only call on aSize for which GetAbsoluteSize returned Nothing().
//
// Bug 1363918: We can remove GetPercentBSize() after we've updated all of
// IntrinsicForAxis()'s callsites to pass it a percentage basis.
template <typename SizeOrMaxSize>
static Maybe<nscoord> GetPercentBSize(const SizeOrMaxSize& aSize,
                                      nsIFrame* aFrame, bool aHorizontalAxis) {
  if (!aSize->IsLengthPercentage()) {
    return Nothing();
  }
  return GetPercentBSize(aSize->AsLengthPercentage(), aFrame, aHorizontalAxis);
}

// Only call on aSize for which GetAbsoluteSize returned Nothing().
static Maybe<nscoord> GetPercentBSize(const LengthPercentage& aSize,
                                      nsIFrame* aFrame, bool aHorizontalAxis) {
  if (!aSize.HasPercent()) {
    return Nothing();
  }

  MOZ_ASSERT(!aSize.ConvertsToLength(),
             "GetAbsoluteSize should have handled this");

  // During reflow, ScrollContainerFrame::ReflowScrolledFrame uses
  // SetComputedHeight on the reflow input for its child to propagate its
  // computed height to the scrolled content. So here we skip to the scroll
  // frame that contains this scrolled content in order to get the same
  // behavior as layout when computing percentage heights.
  nsIFrame* f = aFrame->GetContainingBlock(nsIFrame::SKIP_SCROLLED_FRAME);
  if (!f) {
    MOZ_ASSERT_UNREACHABLE("top of frame tree not a containing block");
    return Nothing();
  }

  // Helper to compute the block-size, max-block-size, and min-block-size later
  // in this function.
  auto GetBSize = [&](const auto& aSize) {
    return nsLayoutUtils::GetAbsoluteSize(*aSize).orElse(
        [&]() { return GetPercentBSize(aSize, f, aHorizontalAxis); });
  };

  WritingMode wm = f->GetWritingMode();
  const nsStylePosition* pos = f->StylePosition();
  const auto positionProperty = f->StyleDisplay()->mPosition;
  Maybe<nscoord> bSize = GetBSize(pos->BSize(wm, positionProperty));
  if (!bSize) {
    LayoutFrameType fType = f->Type();
    if (fType != LayoutFrameType::Viewport &&
        fType != LayoutFrameType::Canvas &&
        fType != LayoutFrameType::PageContent) {
      // There's no basis for the percentage height, so it acts like auto.
      // Should we consider a max-height < min-height pair a basis for
      // percentage heights?  The spec is somewhat unclear, and not doing
      // so is simpler and avoids troubling discontinuities in behavior,
      // so I'll choose not to. -LDB
      return Nothing();
    }
    // For the viewport, canvas, and page-content kids, the percentage
    // basis is just the parent block-size.
    bSize.emplace(f->BSize(wm));
    if (*bSize == NS_UNCONSTRAINEDSIZE) {
      // We don't have a percentage basis after all
      return Nothing();
    }
  }

  if (Maybe<nscoord> maxBSize = GetBSize(pos->MaxBSize(wm, positionProperty))) {
    if (*maxBSize < *bSize) {
      *bSize = *maxBSize;
    }
  }

  if (Maybe<nscoord> minBSize = GetBSize(pos->MinBSize(wm, positionProperty))) {
    if (*minBSize > *bSize) {
      *bSize = *minBSize;
    }
  }

  // If we're an abspos box, percentages in that case resolve against the
  // padding box.
  //
  // TODO: This could conceivably cause some problems with fieldsets (which are
  // the other place that wants to ignore padding), but solving that here
  // without hardcoding a check for f being a fieldset-content frame is a bit of
  // a pain.
  const bool resolvesAgainstPaddingBox = aFrame->IsAbsolutelyPositioned();
  *bSize += GetBSizePercentBasisAdjustment(pos->mBoxSizing, f, aHorizontalAxis,
                                           resolvesAgainstPaddingBox);

  *bSize = std::max(*bSize, 0);
  return Some(std::max(aSize.Resolve(*bSize), 0));
}

// If aSize can be resolved to a definite value, returns it; otherwise returns
// Nothing().
static Maybe<nscoord> GetDefiniteSize(
    const LengthPercentage& aSize, nsIFrame* aFrame, bool aIsInlineAxis,
    const Maybe<LogicalSize>& aPercentageBasis) {
  if (aSize.ConvertsToLength()) {
    return Some(aSize.ToLength());
  }

  if (!aPercentageBasis) {
    return Nothing();
  }

  auto wm = aFrame->GetWritingMode();
  nscoord pb = aIsInlineAxis ? aPercentageBasis.value().ISize(wm)
                             : aPercentageBasis.value().BSize(wm);
  if (pb == NS_UNCONSTRAINEDSIZE) {
    return Nothing();
  }
  return Some(std::max(0, aSize.Resolve(pb)));
}

// If aSize can be resolved to a definite value, returns it; otherwise returns
// Nothing().
template <typename SizeOrMaxSize>
static Maybe<nscoord> GetDefiniteSize(
    const SizeOrMaxSize& aSize, nsIFrame* aFrame, bool aIsInlineAxis,
    const Maybe<LogicalSize>& aPercentageBasis) {
  if (!aSize->IsLengthPercentage()) {
    return Nothing();
  }
  return GetDefiniteSize(aSize->AsLengthPercentage(), aFrame, aIsInlineAxis,
                         aPercentageBasis);
}

// NOTE: this function will be replaced by GetDefiniteSizeTakenByBoxSizing (bug
// 1363918). Please do not add new uses of this function.
//
// Get the amount of space to add or subtract out of aFrame's 'block-size' or
// property value due its borders and paddings, given the box-sizing value in
// aBoxSizing.
//
// aHorizontalAxis is true if our inline direction is horizontal and our block
// direction is vertical. aResolvesAgainstPaddingBox is true if padding should
// be added or not removed.
static nscoord GetBSizePercentBasisAdjustment(StyleBoxSizing aBoxSizing,
                                              nsIFrame* aFrame,
                                              bool aHorizontalAxis,
                                              bool aResolvesAgainstPaddingBox) {
  nscoord adjustment = 0;
  if (aBoxSizing == StyleBoxSizing::Border) {
    const auto& border = aFrame->StyleBorder()->GetComputedBorder();
    adjustment -= aHorizontalAxis ? border.TopBottom() : border.LeftRight();
  }
  if ((aBoxSizing == StyleBoxSizing::Border) == !aResolvesAgainstPaddingBox) {
    const auto& stylePadding = aFrame->StylePadding()->mPadding;
    const LengthPercentage& paddingStart =
        stylePadding.Get(aHorizontalAxis ? eSideTop : eSideLeft);
    const LengthPercentage& paddingEnd =
        stylePadding.Get(aHorizontalAxis ? eSideBottom : eSideRight);

    // XXXbz Calling GetPercentBSize on padding values looks bogus, since
    // percent padding is always a percentage of the inline-size of the
    // containing block.  We should perhaps just treat non-absolute paddings
    // here as 0 instead, except that in some cases the width may in fact be
    // known.  See bug 1231059.
    auto GetPadding = [&](const LengthPercentage& aPadding) {
      return nsLayoutUtils::GetAbsoluteSize(aPadding).orElse(
          [&]() { return GetPercentBSize(aPadding, aFrame, aHorizontalAxis); });
    };
    if (Maybe<nscoord> pad = GetPadding(paddingStart)) {
      adjustment += aResolvesAgainstPaddingBox ? *pad : -*pad;
    }
    if (Maybe<nscoord> pad = GetPadding(paddingEnd)) {
      adjustment += aResolvesAgainstPaddingBox ? *pad : -*pad;
    }
  }
  return adjustment;
}

// Get the amount of space taken out of aFrame's content area due to its
// borders and paddings given the box-sizing value in aBoxSizing.  We don't
// get aBoxSizing from the frame because some callers want to compute this for
// specific box-sizing values.
// aIsInlineAxis is true if we're computing for aFrame's inline axis.
// aIgnorePadding is true if padding should be ignored.
static nscoord GetDefiniteSizeTakenByBoxSizing(
    StyleBoxSizing aBoxSizing, nsIFrame* aFrame, bool aIsInlineAxis,
    bool aIgnorePadding, const Maybe<LogicalSize>& aPercentageBasis) {
  nscoord sizeTakenByBoxSizing = 0;
  if (MOZ_UNLIKELY(aBoxSizing == StyleBoxSizing::Border)) {
    const bool isHorizontalAxis =
        aIsInlineAxis == !aFrame->GetWritingMode().IsVertical();
    const nsStyleBorder* styleBorder = aFrame->StyleBorder();
    sizeTakenByBoxSizing = isHorizontalAxis
                               ? styleBorder->GetComputedBorder().LeftRight()
                               : styleBorder->GetComputedBorder().TopBottom();
    if (!aIgnorePadding) {
      const auto& stylePadding = aFrame->StylePadding()->mPadding;
      const LengthPercentage& pStart =
          stylePadding.Get(isHorizontalAxis ? eSideLeft : eSideTop);
      const LengthPercentage& pEnd =
          stylePadding.Get(isHorizontalAxis ? eSideRight : eSideBottom);

      // XXXbz Calling GetPercentBSize on padding values looks bogus, since
      // percent padding is always a percentage of the inline-size of the
      // containing block.  We should perhaps just treat non-absolute paddings
      // here as 0 instead, except that in some cases the width may in fact be
      // known.  See bug 1231059.
      auto GetPadding =
          [&](const LengthPercentage& aPadding) -> Maybe<nscoord> {
        if (Maybe<nscoord> padding = GetDefiniteSize(
                aPadding, aFrame, aIsInlineAxis, aPercentageBasis)) {
          return padding;
        }
        if (aPercentageBasis) {
          return Nothing();
        }
        return GetPercentBSize(aPadding, aFrame, isHorizontalAxis);
      };
      if (Maybe<nscoord> pad = GetPadding(pStart)) {
        sizeTakenByBoxSizing += *pad;
      }
      if (Maybe<nscoord> pad = GetPadding(pEnd)) {
        sizeTakenByBoxSizing += *pad;
      }
    }
  }
  return sizeTakenByBoxSizing;
}

/**
 * Handles only max-content and min-content, and
 * -moz-fit-content for min-width and max-width, since the others
 * (-moz-fit-content for width, and -moz-available) have no effect on
 * intrinsic widths.
 */
static Maybe<nscoord> GetIntrinsicSize(nsIFrame::ExtremumLength aLength,
                                       gfxContext* aRenderingContext,
                                       nsIFrame* aFrame,
                                       Maybe<nscoord> aISizeFromAspectRatio,
                                       nsIFrame::SizeProperty aProperty,
                                       nscoord aContentBoxToBoxSizingDiff) {
  if (aLength == nsIFrame::ExtremumLength::MozAvailable ||
      aLength == nsIFrame::ExtremumLength::Stretch) {
    return Nothing();
  }

  if (aLength == nsIFrame::ExtremumLength::FitContentFunction) {
    // fit-content() should be handled by the caller.
    return Nothing();
  }

  if (aLength == nsIFrame::ExtremumLength::FitContent) {
    switch (aProperty) {
      case nsIFrame::SizeProperty::Size:
        // handle like 'width: auto'
        return Nothing();
      case nsIFrame::SizeProperty::MaxSize:
        // constrain large 'width' values down to max-content
        aLength = nsIFrame::ExtremumLength::MaxContent;
        break;
      case nsIFrame::SizeProperty::MinSize:
        // constrain small 'width' or 'max-width' values up to min-content
        aLength = nsIFrame::ExtremumLength::MinContent;
        break;
    }
  }

  NS_ASSERTION(aLength == nsIFrame::ExtremumLength::MinContent ||
                   aLength == nsIFrame::ExtremumLength::MaxContent,
               "should have reduced everything remaining to one of these");

  // If aFrame is a container for font size inflation, then shrink
  // wrapping inside of it should not apply font size inflation.
  AutoMaybeDisableFontInflation an(aFrame);

  nscoord result;
  if (aISizeFromAspectRatio) {
    result = *aISizeFromAspectRatio;
  } else {
    // Bug 1363918: We need to refactor this function to compute a percentage
    // basis when computing intrinsic sizes.
    const IntrinsicSizeInput input(aRenderingContext, Nothing(), Nothing());
    auto type = aLength == nsIFrame::ExtremumLength::MaxContent
                    ? IntrinsicISizeType::PrefISize
                    : IntrinsicISizeType::MinISize;
    result = aFrame->IntrinsicISize(input, type);
  }

  result += aContentBoxToBoxSizingDiff;
  return Some(result);
}

template <typename SizeOrMaxSize>
static Maybe<nscoord> GetIntrinsicSize(const SizeOrMaxSize& aSize,
                                       gfxContext* aRenderingContext,
                                       nsIFrame* aFrame,
                                       Maybe<nscoord> aISizeFromAspectRatio,
                                       nsIFrame::SizeProperty aProperty,
                                       nscoord aContentBoxToBoxSizingDiff) {
  auto length = nsIFrame::ToExtremumLength(aSize);
  if (!length) {
    return Nothing();
  }
  return GetIntrinsicSize(*length, aRenderingContext, aFrame,
                          aISizeFromAspectRatio, aProperty,
                          aContentBoxToBoxSizingDiff);
}

static nscoord GetFitContentSizeForMaxOrPreferredSize(
    const IntrinsicISizeType aType, const nsIFrame::SizeProperty aProperty,
    const nsIFrame* aFrame, const LengthPercentage& aStyleSize,
    const nscoord aInitialValue, const nscoord aMinContentSize,
    const nscoord aMaxContentSize) {
  MOZ_ASSERT(aProperty != nsIFrame::SizeProperty::MinSize);

  nscoord size;
  // 1. Treat fit-content()'s arg as a plain LengthPercentage
  // However, we have to handle the cyclic percentage contribution first.
  //
  // https://drafts.csswg.org/css-sizing-3/#cyclic-percentage-contribution
  if (aType == IntrinsicISizeType::MinISize &&
      aFrame->IsPercentageResolvedAgainstZero(aStyleSize, aProperty)) {
    // Case (c) in the spec.
    // FIXME: This doesn't follow the spec for calc(). We should fix this in
    // Bug 1463700.
    size = 0;
  } else if (Maybe<nscoord> length =
                 nsLayoutUtils::GetAbsoluteSize(aStyleSize)) {
    size = *length;
  } else {
    // As initial value. Case (a) and (b) in the spec.
    size = aInitialValue;
  }

  // 2. Clamp size by min-content and max-content.
  return std::clamp(size, aMinContentSize, aMaxContentSize);
}

/**
 * Add aOffsets which describes what to add on outside of the content box
 * aContentSize (controlled by 'box-sizing') and apply min/max properties.
 * We have to account for these properties after getting all the offsets
 * (margin, border, padding) because percentages do not operate linearly.
 * Doing this is ok because although percentages aren't handled linearly,
 * they are handled monotonically.
 *
 * @param aContentSize the content size calculated so far
                       (@see IntrinsicForAxis)
 * @param aStyleSize a 'width' or 'height' property value
 * @param aFixedMinSize if aStyleMinSize is a definite size then this contains
 *                      the value, otherwise Nothing()
 * @param aStyleMinSize a 'min-width' or 'min-height' property value
 * @param aFixedMaxSize if aStyleMaxSize is a definite size then this contains
 *                      the value, otherwise Nothing()
 * @param aStyleMaxSize a 'max-width' or 'max-height' property value
 * @param aISizeFromAspectRatio the content-box inline size computed from
 *                              aspect-ratio and the definite block size.
 *                              We use this value to resolve sizes in inline
 *                              axis with intrinsic keyword.
 * @param aFlags same as for IntrinsicForAxis
 */
static nscoord AddIntrinsicSizeOffset(
    gfxContext* aRenderingContext, nsIFrame* aFrame,
    const nsIFrame::IntrinsicSizeOffsetData& aOffsets, IntrinsicISizeType aType,
    StyleBoxSizing aBoxSizing, nscoord aContentSize,
    const StyleSize& aStyleSize, const Maybe<nscoord> aFixedMinSize,
    const StyleSize& aStyleMinSize, const Maybe<nscoord> aFixedMaxSize,
    const StyleMaxSize& aStyleMaxSize, Maybe<nscoord> aISizeFromAspectRatio,
    uint32_t aFlags, PhysicalAxis aAxis) {
  const nscoord padding =
      aFlags & nsLayoutUtils::IGNORE_PADDING ? 0 : aOffsets.padding;
  nscoord contentBoxToBoxSizingDiff;
  nscoord boxSizingToMarginDiff;

  // Note: |result| can be either the border-box size or the content-box size,
  // depending on the value of aBoxSizing.
  nscoord result;
  if (aBoxSizing == StyleBoxSizing::Border) {
    contentBoxToBoxSizingDiff = padding + aOffsets.border;
    boxSizingToMarginDiff = aOffsets.margin;
    result = NSCoordSaturatingAdd(aContentSize, contentBoxToBoxSizingDiff);
  } else {
    MOZ_ASSERT(aBoxSizing == StyleBoxSizing::Content);
    contentBoxToBoxSizingDiff = 0;
    boxSizingToMarginDiff = padding + aOffsets.border + aOffsets.margin;
    result = aContentSize;
  }

  // Compute min-content/max-content for fit-content().
  nscoord minContent = 0;
  nscoord maxContent = NS_UNCONSTRAINEDSIZE;
  if (aStyleSize.IsFitContentFunction() ||
      aStyleMaxSize.IsFitContentFunction() ||
      aStyleMinSize.IsFitContentFunction()) {
    if (aISizeFromAspectRatio) {
      minContent = maxContent = *aISizeFromAspectRatio;
    } else {
      // Bug 1363918: We need to refactor this function to compute a percentage
      // basis when computing intrinsic sizes.
      const IntrinsicSizeInput input(aRenderingContext, Nothing(), Nothing());
      minContent = aFrame->GetMinISize(input);
      maxContent = aFrame->GetPrefISize(input);
    }
    minContent += contentBoxToBoxSizingDiff;
    maxContent += contentBoxToBoxSizingDiff;
  }

  // Compute size.
  if (aType == IntrinsicISizeType::MinISize &&
      aFrame->IsPercentageResolvedAgainstZero(aStyleSize, aStyleMaxSize)) {
    // XXX bug 1463700: this doesn't handle calc() according to spec
    result = 0;
  } else if (Maybe<nscoord> size =
                 nsLayoutUtils::GetAbsoluteSize(aStyleSize).orElse([&]() {
                   return GetIntrinsicSize(aStyleSize, aRenderingContext,
                                           aFrame, aISizeFromAspectRatio,
                                           nsIFrame::SizeProperty::Size,
                                           contentBoxToBoxSizingDiff);
                 })) {
    result = *size + boxSizingToMarginDiff;
  } else if (aStyleSize.IsFitContentFunction()) {
    // |result| here is the content size or border size, depends on
    // StyleBoxSizing. We use it as the initial value when handling the cyclic
    // percentage.
    nscoord initial = result;
    nscoord fitContentFuncSize = GetFitContentSizeForMaxOrPreferredSize(
        aType, nsIFrame::SizeProperty::Size, aFrame,
        aStyleSize.AsFitContentFunction(), initial, minContent, maxContent);
    // Add border and padding.
    result = NSCoordSaturatingAdd(fitContentFuncSize, boxSizingToMarginDiff);
  } else {
    result = NSCoordSaturatingAdd(result, boxSizingToMarginDiff);
  }

  // Compute max-size.
  Maybe<nscoord> maxSize = aFixedMaxSize.orElse([&]() {
    return GetIntrinsicSize(
        aStyleMaxSize, aRenderingContext, aFrame, aISizeFromAspectRatio,
        nsIFrame::SizeProperty::MaxSize, contentBoxToBoxSizingDiff);
  });
  if (maxSize) {
    *maxSize += boxSizingToMarginDiff;
    if (result > *maxSize) {
      result = *maxSize;
    }
  } else if (aStyleMaxSize.IsFitContentFunction()) {
    nscoord fitContentFuncSize = GetFitContentSizeForMaxOrPreferredSize(
        aType, nsIFrame::SizeProperty::MaxSize, aFrame,
        aStyleMaxSize.AsFitContentFunction(), NS_UNCONSTRAINEDSIZE, minContent,
        maxContent);
    maxSize.emplace(
        NSCoordSaturatingAdd(fitContentFuncSize, boxSizingToMarginDiff));
    if (result > *maxSize) {
      result = *maxSize;
    }
  }

  // Compute min-size.
  Maybe<nscoord> minSize = aFixedMinSize.orElse([&]() {
    return GetIntrinsicSize(
        aStyleMinSize, aRenderingContext, aFrame, aISizeFromAspectRatio,
        nsIFrame::SizeProperty::MinSize, contentBoxToBoxSizingDiff);
  });
  if (minSize) {
    *minSize += boxSizingToMarginDiff;
    if (result < *minSize) {
      result = *minSize;
    }
  } else if (aStyleMinSize.IsFitContentFunction()) {
    minSize =
        nsLayoutUtils::GetAbsoluteSize(aStyleMinSize.AsFitContentFunction());
    if (!minSize) {
      // FIXME: Bug 1463700, we should resolve only the percentage part to 0
      // such as min-width: fit-content(calc(50% + 50px)).
      minSize.emplace(0);
    }
    nscoord fitContentFuncSize = CSSMinMax(*minSize, minContent, maxContent);
    *minSize = NSCoordSaturatingAdd(fitContentFuncSize, boxSizingToMarginDiff);
    if (result < *minSize) {
      result = *minSize;
    }
  }

  const nscoord borderPaddingMargin =
      contentBoxToBoxSizingDiff + boxSizingToMarginDiff;
  result = std::max(result, borderPaddingMargin);

  const nsStyleDisplay* disp = aFrame->StyleDisplay();
  if (aFrame->IsThemed(disp)) {
    nsPresContext* pc = aFrame->PresContext();
    LayoutDeviceIntSize devSize = pc->Theme()->GetMinimumWidgetSize(
        pc, aFrame, disp->EffectiveAppearance());
    nscoord themeSize = pc->DevPixelsToAppUnits(
        aAxis == PhysicalAxis::Vertical ? devSize.height : devSize.width);
    // GetMinimumWidgetSize() returns a border-box width.
    themeSize += aOffsets.margin;
    if (themeSize > result) {
      result = themeSize;
    }
  }
  return result;
}

static void AddStateBitToAncestors(nsIFrame* aFrame, nsFrameState aBit) {
  for (nsIFrame* f = aFrame; f; f = f->GetParent()) {
    if (f->HasAnyStateBits(aBit)) {
      break;
    }
    f->AddStateBits(aBit);
  }
}

static nsSize MeasureIntrinsicContentSize(
    gfxContext* aContext, nsIFrame* aFrame,
    const Maybe<LogicalSize>& aPercentageBasis) {
  nsPresContext* pc = aFrame->PresContext();
  nsIFrame* parent = aFrame->GetParent();
  const WritingMode parentWM = parent->GetWritingMode();
  const WritingMode childWM = aFrame->GetWritingMode();

  // We only expect to hit this codepath when the frame creates an
  // orthogonal flow.
  MOZ_ASSERT(childWM.IsOrthogonalTo(parentWM));

  const ReflowInput dummyParentRI(
      pc, parent, aContext,
      LogicalSize(parentWM, NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE),
      ReflowInput::InitFlag::DummyParentReflowInput);
  const ReflowInput reflowInput(
      pc, dummyParentRI, aFrame,
      LogicalSize(childWM, NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE),
      aPercentageBasis);

  // We call Reflow and FinishReflowChild just to measure the (desired) size of
  // the child frame. It can not actually be positioned properly at this stage,
  // since we don't have a known containing block; that will be done by the
  // main reflow that's in progress.
  ReflowOutput reflowOutput(reflowInput);
  nsReflowStatus status;
  aFrame->Reflow(pc, reflowOutput, reflowInput, status);
  MOZ_ASSERT(status.IsFullyComplete());

  const nsIFrame::ReflowChildFlags flags =
      nsIFrame::ReflowChildFlags::NoMoveFrame |
      nsIFrame::ReflowChildFlags::NoSizeView |
      nsIFrame::ReflowChildFlags::NoDeleteNextInFlowChild;
  nsContainerFrame::FinishReflowChild(aFrame, pc, reflowOutput, &reflowInput,
                                      childWM, LogicalPoint(parentWM), nsSize(),
                                      flags);

  // We're just returning the child's desired content size; IntrinsicForAxis
  // accounts for border and padding later through AddIntrinsicSizeOffset.
  return aFrame->ContentSize(childWM).GetPhysicalSize(childWM);
}

/* static */
nscoord nsLayoutUtils::IntrinsicForAxis(
    PhysicalAxis aAxis, gfxContext* aRenderingContext, nsIFrame* aFrame,
    IntrinsicISizeType aType, const Maybe<LogicalSize>& aPercentageBasis,
    uint32_t aFlags, nscoord aMarginBoxMinSizeClamp,
    const StyleSizeOverrides& aSizeOverrides) {
  MOZ_ASSERT(aFrame, "null frame");
  MOZ_ASSERT(aFrame->GetParent(),
             "IntrinsicForAxis called on frame not in tree");
  MOZ_ASSERT(aFrame->GetParent()->Type() != LayoutFrameType::GridContainer ||
                 aPercentageBasis.isSome(),
             "grid layout should always pass a percentage basis");

  const bool horizontalAxis = MOZ_LIKELY(aAxis == PhysicalAxis::Horizontal);

  // If aFrame is a container for font size inflation, then shrink
  // wrapping inside of it should not apply font size inflation.
  AutoMaybeDisableFontInflation an(aFrame);

  // We want the size this frame will contribute to the parent's inline-size,
  // so we work in the parent's writing mode; but if aFrame is orthogonal to
  // its parent, we'll need to look at its BSize instead of min/pref-ISize.
  const nsStylePosition* stylePos = aFrame->StylePosition();
  StyleBoxSizing boxSizing = stylePos->mBoxSizing;
  PhysicalAxis ourInlineAxis =
      aFrame->GetWritingMode().PhysicalAxis(LogicalAxis::Inline);
  const bool isInlineAxis = aAxis == ourInlineAxis;

  const auto positionProperty = aFrame->StyleDisplay()->mPosition;
  auto styleMinISize = horizontalAxis
                           ? stylePos->GetMinWidth(positionProperty)
                           : stylePos->GetMinHeight(positionProperty);
  auto styleISize = [&]() {
    if (aFlags & MIN_INTRINSIC_ISIZE) {
      return AnchorResolvedSizeHelper::Overridden(*styleMinISize);
    }
    const Maybe<StyleSize>& styleISizeOverride =
        isInlineAxis ? aSizeOverrides.mStyleISize : aSizeOverrides.mStyleBSize;
    return styleISizeOverride
               ? AnchorResolvedSizeHelper::Overridden(*styleISizeOverride)
               : (horizontalAxis ? stylePos->GetWidth(positionProperty)
                                 : stylePos->GetHeight(positionProperty));
  }();
  MOZ_ASSERT(!(aFlags & MIN_INTRINSIC_ISIZE) || styleISize->IsAuto() ||
                 nsIFrame::ToExtremumLength(*styleISize),
             "should only use MIN_INTRINSIC_ISIZE for intrinsic values");
  auto styleMaxISize = horizontalAxis
                           ? stylePos->GetMaxWidth(positionProperty)
                           : stylePos->GetMaxHeight(positionProperty);

  auto ResetIfKeywords = [](AnchorResolvedSize& aSize,
                            AnchorResolvedSize& aMinSize,
                            AnchorResolvedMaxSize& aMaxSize) {
    if (!aSize->IsLengthPercentage()) {
      aSize = AnchorResolvedSizeHelper::Auto();
    }
    if (!aMinSize->IsLengthPercentage()) {
      aMinSize = AnchorResolvedSizeHelper::Auto();
    }
    if (!aMaxSize->IsLengthPercentage()) {
      aMaxSize = AnchorResolvedMaxSizeHelper::None();
    }
  };
  // According to the spec, max-content and min-content should behave as the
  // property's initial values in block axis.
  // It also make senses to use the initial values for -moz-fit-content and
  // -moz-available for intrinsic size in block axis. Therefore, we reset them
  // if needed.
  if (!isInlineAxis) {
    ResetIfKeywords(styleISize, styleMinISize, styleMaxISize);
  }

  // We build up the content box size, storing in |result|, and then
  // adding padding, border and margin in AddIntrinsicSizeOffset().
  nscoord result = 0;

  Maybe<nscoord> fixedMaxISize = GetAbsoluteSize(*styleMaxISize);
  Maybe<nscoord> fixedMinISize;

  // Treat "min-width: auto" as 0.
  if (styleMinISize->IsAuto()) {
    // NOTE: Technically, "auto" is supposed to behave like "min-content" on
    // flex items. However, we don't need to worry about that here, because
    // flex items' min-sizes are intentionally ignored until the flex
    // container explicitly considers them during space distribution.
    fixedMinISize.emplace(0);
  } else {
    fixedMinISize = GetAbsoluteSize(*styleMinISize);
  }

  // Handle elements with an intrinsic ratio (or size) and a specified
  // height, min-height, or max-height.
  // NOTE:
  // 1. We treat "min-height:auto" as "0" for the purpose of this code,
  // since that's what it means in all cases except for on flex items -- and
  // even there, we're supposed to ignore it (i.e. treat it as 0) until the
  // flex container explicitly considers it.
  // 2. The 'B' in |styleBSize|, |styleMinBSize|, and |styleMaxBSize|
  // represents the ratio-determining axis of |aFrame|. It could be the inline
  // axis or the block axis of |aFrame|. (So we are calculating the size
  // along the ratio-dependent axis in this if-branch.)
  const Maybe<StyleSize>& styleBSizeOverride =
      isInlineAxis ? aSizeOverrides.mStyleBSize : aSizeOverrides.mStyleISize;
  auto styleBSize =
      styleBSizeOverride
          ? AnchorResolvedSizeHelper::Overridden(*styleBSizeOverride)
          : (horizontalAxis ? stylePos->GetHeight(positionProperty)
                            : stylePos->GetWidth(positionProperty));
  auto styleMinBSize = horizontalAxis ? stylePos->GetMinHeight(positionProperty)
                                      : stylePos->GetMinWidth(positionProperty);
  auto styleMaxBSize = horizontalAxis ? stylePos->GetMaxHeight(positionProperty)
                                      : stylePos->GetMaxWidth(positionProperty);

  // According to the spec, max-content and min-content should behave as the
  // property's initial values in block axis.
  // It also make senses to use the initial values for -moz-fit-content and
  // -moz-available for intrinsic size in block axis. Therefore, we reset them
  // if needed.
  if (isInlineAxis) {
    ResetIfKeywords(styleBSize, styleMinBSize, styleMaxBSize);
  }

  auto childWM = aFrame->GetWritingMode();
  nscoord pmPercentageBasis = NS_UNCONSTRAINEDSIZE;
  if (aPercentageBasis.isSome()) {
    // The padding/margin percentage basis is the inline-size in the parent's
    // writing-mode.
    pmPercentageBasis =
        aFrame->GetParent()->GetWritingMode().IsOrthogonalTo(childWM)
            ? aPercentageBasis->BSize(childWM)
            : aPercentageBasis->ISize(childWM);
  }
  nsIFrame::IntrinsicSizeOffsetData offsetInRequestedAxis =
      MOZ_LIKELY(isInlineAxis)
          ? aFrame->IntrinsicISizeOffsets(pmPercentageBasis)
          : aFrame->IntrinsicBSizeOffsets(pmPercentageBasis);

  auto GetContentEdgeToBoxSizing = [&](const StyleBoxSizing aBoxSizing) {
    if (aBoxSizing == StyleBoxSizing::Content) {
      return LogicalSize(childWM);
    }
    nsIFrame::IntrinsicSizeOffsetData offsetInOtherAxis =
        MOZ_LIKELY(isInlineAxis)
            ? aFrame->IntrinsicBSizeOffsets(pmPercentageBasis)
            : aFrame->IntrinsicISizeOffsets(pmPercentageBasis);
    const auto& inlineOffset =
        isInlineAxis ? offsetInRequestedAxis : offsetInOtherAxis;
    const auto& blockOffset =
        isInlineAxis ? offsetInOtherAxis : offsetInRequestedAxis;
    return LogicalSize(childWM, inlineOffset.BorderPadding(),
                       blockOffset.BorderPadding());
  };

  // Helper to compute the block-size, max-block-size, and min-block-size later
  // in this function.
  auto GetBSize = [&](const auto& aSize) -> Maybe<nscoord> {
    if (Maybe<nscoord> bSize =
            GetDefiniteSize(aSize, aFrame, !isInlineAxis, aPercentageBasis)) {
      return bSize;
    }
    if (aPercentageBasis) {
      return Nothing();
    }
    // Find percentage basis, then compute.
    return GetPercentBSize(aSize, aFrame, horizontalAxis);
  };

  Maybe<nscoord> iSizeFromAspectRatio;
  Maybe<LogicalSize> contentEdgeToBoxSizing;

  const bool ignorePadding =
      (aFlags & IGNORE_PADDING) || aFrame->IsAbsolutelyPositioned();

  // If we have a specified width (or a specified 'min-width' greater
  // than the specified 'max-width', which works out to the same thing),
  // don't even bother getting the frame's intrinsic width, because in
  // this case GetAbsoluteSize(styleISize) will always succeed, so
  // we'll never need the intrinsic dimensions.
  if (styleISize->IsMaxContent() || styleISize->IsMinContent()) {
    MOZ_ASSERT(isInlineAxis);
    // -moz-fit-content and -moz-available enumerated widths compute intrinsic
    // widths just like auto.
    // For max-content and min-content, we handle them like
    // specified widths, but ignore box-sizing.
    boxSizing = StyleBoxSizing::Content;
  } else if (!styleISize->ConvertsToLength() &&
             !(styleISize->IsFitContentFunction() &&
               styleISize->AsFitContentFunction().ConvertsToLength()) &&
             !(fixedMaxISize && fixedMinISize &&
               *fixedMaxISize <= *fixedMinISize)) {
    if (MOZ_UNLIKELY(!isInlineAxis)) {
      IntrinsicSize intrinsicSize = aFrame->GetIntrinsicSize();
      const auto& intrinsicBSize =
          horizontalAxis ? intrinsicSize.width : intrinsicSize.height;
      if (intrinsicBSize) {
        result = *intrinsicBSize;
      } else {
        // We don't have an intrinsic bsize and we need aFrame's block-dir size.
        if (aFlags & BAIL_IF_REFLOW_NEEDED) {
          return NS_INTRINSIC_ISIZE_UNKNOWN;
        }
        nsSize size = MeasureIntrinsicContentSize(aRenderingContext, aFrame,
                                                  aPercentageBasis);
        result = horizontalAxis ? size.Width() : size.Height();
      }
    } else {
      // To resolve aFrame's intrinsic inline size, we first check if we can
      // resolve a block-axis percentage basis for aFrame's children. This can
      // influence their inline size contributions, e.g. if they have an
      // aspect-ratio and a percentage-based block size.
      const nscoord percentageBasisBSizeForFrame =
          aPercentageBasis ? aPercentageBasis->BSize(childWM)
                           : NS_UNCONSTRAINEDSIZE;
      nscoord percentageBasisBSizeForChildren;
      if (aFrame->IsBlockContainer()) {
        // Compute and cache the box-sizing adjustment in contentEdgeToBoxSizing
        // for later use within this function.
        contentEdgeToBoxSizing.emplace(GetContentEdgeToBoxSizing(boxSizing));

        // aFrame is a containing block, so its block size (with min and max
        // block size constraints applied) serves as the percentage basis for
        // its children.
        percentageBasisBSizeForChildren =
            nsIFrame::ComputeBSizeValueAsPercentageBasis(
                *styleBSize, *styleMinBSize, *styleMaxBSize,
                percentageBasisBSizeForFrame,
                contentEdgeToBoxSizing->BSize(childWM));
      } else {
        // aFrame is not a containing block, so its children share the same
        // containing block as aFrame. Therefore, the percentage basis for
        // aFrame's children is the same as that for aFrame.
        percentageBasisBSizeForChildren = percentageBasisBSizeForFrame;
      }
      const IntrinsicSizeInput input(
          aRenderingContext, aPercentageBasis,
          Some(LogicalSize(childWM, NS_UNCONSTRAINEDSIZE,
                           percentageBasisBSizeForChildren)));
      result = aFrame->IntrinsicISize(input, aType);
    }

    // If our BSize or min/max-BSize properties are set to values that we can
    // resolve and that will impose a constraint when transferred through our
    // aspect ratio (if we have one), then compute and apply that constraint.
    //
    // (Note: This helper-bool & lambda just let us optimize away the actual
    // transferring-and-clamping arithmetic, for the common case where we can
    // tell that none of the block-axis size properties establish a meaningful
    // transferred constraint.)
    const bool mightHaveBlockAxisConstraintToTransfer = [&] {
      if (!styleBSize->BehavesLikeInitialValueOnBlockAxis()) {
        return true;  // BSize property might have a constraint to transfer.
      }
      // Check for min-BSize values that would obviously produce zero in the
      // transferring logic that follows; zero is trivially-ignorable as a
      // transferred lower-bound. (These include the the property's initial
      // value, explicit 0, and values that are equivalent to these.)
      bool minBSizeHasNoConstraintToTransfer =
          styleMinBSize->BehavesLikeInitialValueOnBlockAxis() ||
          (styleMinBSize->IsLengthPercentage() &&
           styleMinBSize->AsLengthPercentage().IsDefinitelyZero());
      if (!minBSizeHasNoConstraintToTransfer) {
        return true;  // min-BSize property might have a constraint to transfer.
      }
      if (!styleMaxBSize->BehavesLikeInitialValueOnBlockAxis()) {
        return true;  // max-BSize property might have a constraint to transfer.
      }
      return false;
    }();
    if (mightHaveBlockAxisConstraintToTransfer) {
      if (AspectRatio ratio = aFrame->GetAspectRatio()) {
        AddStateBitToAncestors(
            aFrame, NS_FRAME_DESCENDANT_INTRINSIC_ISIZE_DEPENDS_ON_BSIZE);

        nscoord bSizeTakenByBoxSizing = GetDefiniteSizeTakenByBoxSizing(
            boxSizing, aFrame, !isInlineAxis, ignorePadding, aPercentageBasis);
        if (!contentEdgeToBoxSizing) {
          contentEdgeToBoxSizing.emplace(GetContentEdgeToBoxSizing(boxSizing));
        }

        // NOTE: This is only the minContentSize if we've been passed
        // MIN_INTRINSIC_ISIZE (which is fine, because this should only be used
        // inside a check for that flag).
        nscoord minContentSize = result;
        if (Maybe<nscoord> bSize = GetBSize(styleBSize)) {
          *bSize = std::max(0, *bSize - bSizeTakenByBoxSizing);
          // We are computing the size of |aFrame|, so we use the inline & block
          // dimensions of |aFrame|.
          result = ratio.ComputeRatioDependentSize(
              isInlineAxis ? LogicalAxis::Inline : LogicalAxis::Block, childWM,
              *bSize, *contentEdgeToBoxSizing);
          // We have got the iSizeForAspectRatio value, so we don't need to
          // compute this again below.
          iSizeFromAspectRatio.emplace(result);
        }

        if (Maybe<nscoord> maxBSize = GetBSize(styleMaxBSize)) {
          *maxBSize = std::max(0, *maxBSize - bSizeTakenByBoxSizing);
          nscoord maxISize = ratio.ComputeRatioDependentSize(
              isInlineAxis ? LogicalAxis::Inline : LogicalAxis::Block, childWM,
              *maxBSize, *contentEdgeToBoxSizing);
          if (maxISize < result) {
            result = maxISize;
          }
          if (maxISize < minContentSize) {
            minContentSize = maxISize;
          }
        }

        if (Maybe<nscoord> minBSize = GetBSize(styleMinBSize)) {
          *minBSize = std::max(0, *minBSize - bSizeTakenByBoxSizing);
          nscoord minISize = ratio.ComputeRatioDependentSize(
              isInlineAxis ? LogicalAxis::Inline : LogicalAxis::Block, childWM,
              *minBSize, *contentEdgeToBoxSizing);
          if (minISize > result) {
            result = minISize;
          }
          if (minISize > minContentSize) {
            minContentSize = minISize;
          }
        }

        if (MOZ_UNLIKELY(aFlags & nsLayoutUtils::MIN_INTRINSIC_ISIZE) &&
            // FIXME: Bug 1715681. Should we use HasReplacedSizing instead
            // because IsReplaced is set on some other frames which are
            // non-replaced elements, e.g. <select>?
            aFrame->IsReplaced()) {
          // This is the 'min-width/height:auto' "transferred size" piece of:
          // https://drafts.csswg.org/css-flexbox-1/#min-size-auto
          // https://drafts.csswg.org/css-grid/#min-size-auto
          // Per spec, we handle it only for replaced elements.
          result = std::min(result, minContentSize);
        }
      }
    }
  }

  // If we have an aspect-ratio and a definite block size of |aFrame|, we should
  // use them to resolve the sizes with intrinsic keywords in the inline axis.
  // If |aAxis| is the block axis of |aFrame|, intrinsic keywords should behaves
  // as auto, so we don't need this.
  // https://github.com/w3c/csswg-drafts/issues/5032
  const AspectRatio ar = aFrame->GetAspectRatio();
  if (isInlineAxis && ar && !iSizeFromAspectRatio &&
      (nsIFrame::IsIntrinsicKeyword(*styleISize) ||
       nsIFrame::IsIntrinsicKeyword(*styleMinISize) ||
       nsIFrame::IsIntrinsicKeyword(*styleMaxISize))) {
    if (Maybe<nscoord> bSize = GetBSize(styleBSize)) {
      // We cannot reuse |boxSizing| because it may be updated to content-box
      // in the above if-branch.
      const StyleBoxSizing boxSizingForAR = stylePos->mBoxSizing;
      if (!contentEdgeToBoxSizing) {
        contentEdgeToBoxSizing.emplace(
            GetContentEdgeToBoxSizing(boxSizingForAR));
      }
      nscoord bSizeTakenByBoxSizing =
          GetDefiniteSizeTakenByBoxSizing(boxSizingForAR, aFrame, !isInlineAxis,
                                          ignorePadding, aPercentageBasis);

      *bSize -= bSizeTakenByBoxSizing;
      iSizeFromAspectRatio.emplace(ar.ComputeRatioDependentSize(
          LogicalAxis::Inline, childWM, *bSize, *contentEdgeToBoxSizing));
    }
  }

  nscoord contentBoxSize = result;
  result = AddIntrinsicSizeOffset(
      aRenderingContext, aFrame, offsetInRequestedAxis, aType, boxSizing,
      result, *styleISize, fixedMinISize, *styleMinISize, fixedMaxISize,
      *styleMaxISize, iSizeFromAspectRatio, aFlags, aAxis);
  nscoord overflow = result - aMarginBoxMinSizeClamp;
  if (MOZ_UNLIKELY(overflow > 0)) {
    nscoord newContentBoxSize = std::max(nscoord(0), contentBoxSize - overflow);
    result -= contentBoxSize - newContentBoxSize;
  }

  return result;
}

/* static */
nscoord nsLayoutUtils::IntrinsicForContainer(
    gfxContext* aRenderingContext, nsIFrame* aFrame, IntrinsicISizeType aType,
    const Maybe<LogicalSize>& aPercentageBasis, uint32_t aFlags,
    const StyleSizeOverrides& aSizeOverrides) {
  MOZ_ASSERT(aFrame && aFrame->GetParent());
  // We want the size aFrame will contribute to its parent's inline-size.
  PhysicalAxis axis =
      aFrame->GetParent()->GetWritingMode().PhysicalAxis(LogicalAxis::Inline);
  return IntrinsicForAxis(axis, aRenderingContext, aFrame, aType,
                          aPercentageBasis, aFlags, NS_MAXSIZE, aSizeOverrides);
}

/* static */
nscoord nsLayoutUtils::MinSizeContributionForAxis(
    PhysicalAxis aAxis, gfxContext* aRC, nsIFrame* aFrame,
    IntrinsicISizeType aType, const LogicalSize& aPercentageBasis,
    uint32_t aFlags) {
  MOZ_ASSERT(aFrame);
  MOZ_ASSERT(aFrame->IsFlexOrGridItem(),
             "only grid/flex items have this behavior currently");

  // Note: this method is only meant for grid/flex items.
  const nsStylePosition* const stylePos = aFrame->StylePosition();
  const auto positionProperty = aFrame->StyleDisplay()->mPosition;
  auto size = aAxis == PhysicalAxis::Horizontal
                  ? stylePos->GetMinWidth(positionProperty)
                  : stylePos->GetMinHeight(positionProperty);
  auto maxSize = aAxis == PhysicalAxis::Horizontal
                     ? stylePos->GetMaxWidth(positionProperty)
                     : stylePos->GetMaxHeight(positionProperty);
  auto childWM = aFrame->GetWritingMode();
  PhysicalAxis ourInlineAxis = childWM.PhysicalAxis(LogicalAxis::Inline);
  // According to the spec, max-content and min-content should behave as the
  // property's initial values in block axis.
  // It also make senses to use the initial values for -moz-fit-content and
  // -moz-available for intrinsic size in block axis. Therefore, we reset them
  // if needed.
  if (aAxis != ourInlineAxis) {
    if (size->BehavesLikeInitialValueOnBlockAxis()) {
      size = AnchorResolvedSizeHelper::Auto();
    }
    if (maxSize->BehavesLikeInitialValueOnBlockAxis()) {
      maxSize = AnchorResolvedMaxSizeHelper::None();
    }
  }

  Maybe<nscoord> fixedMinSize;
  if (size->IsAuto()) {
    if (aFrame->StyleDisplay()->IsScrollableOverflow()) {
      // min-[width|height]:auto with scrollable overflow computes to
      // zero.
      fixedMinSize.emplace(0);
    } else {
      size = aAxis == PhysicalAxis::Horizontal
                 ? stylePos->GetWidth(positionProperty)
                 : stylePos->GetHeight(positionProperty);
      // This is same as above: keywords should behaves as property's initial
      // values in block axis.
      if (aAxis != ourInlineAxis &&
          size->BehavesLikeInitialValueOnBlockAxis()) {
        size = AnchorResolvedSizeHelper::Auto();
      }

      fixedMinSize = GetAbsoluteSize(*size);
      if (fixedMinSize) {
        // We have a definite width/height.  This is the "specified size" in:
        // https://drafts.csswg.org/css-grid/#min-size-auto
      } else if (aFrame->IsPercentageResolvedAgainstZero(*size, *maxSize)) {
        // XXX bug 1463700: this doesn't handle calc() according to spec
        fixedMinSize.emplace(0);
      }
      // fall through - the caller will have to deal with "transferred size"
    }
  } else {
    fixedMinSize = GetAbsoluteSize(*size);
    if (!fixedMinSize && size->IsLengthPercentage()) {
      MOZ_ASSERT(size->HasPercent());
      fixedMinSize.emplace(0);
    }
  }

  if (!fixedMinSize) {
    // Let the caller deal with the "content size" cases.
    return NS_UNCONSTRAINEDSIZE;
  }

  // If aFrame is a container for font size inflation, then shrink
  // wrapping inside of it should not apply font size inflation.
  AutoMaybeDisableFontInflation an(aFrame);

  // The padding/margin percentage basis is the inline-size in the parent's
  // writing-mode.
  nscoord pmPercentageBasis =
      aFrame->GetParent()->GetWritingMode().IsOrthogonalTo(childWM)
          ? aPercentageBasis.BSize(childWM)
          : aPercentageBasis.ISize(childWM);
  nsIFrame::IntrinsicSizeOffsetData offsets =
      ourInlineAxis == aAxis ? aFrame->IntrinsicISizeOffsets(pmPercentageBasis)
                             : aFrame->IntrinsicBSizeOffsets(pmPercentageBasis);
  nscoord result = 0;
  // Note: aISizeFromAspectRatio is Nothing() here because we don't handle
  // "content size" cases here (i.e. we've returned earlier when |fixedMinSize|
  // is Nothing()).
  result = AddIntrinsicSizeOffset(
      aRC, aFrame, offsets, aType, stylePos->mBoxSizing, result, *size,
      fixedMinSize, *size, Nothing(), *maxSize, Nothing(), aFlags, aAxis);

  return result;
}

/* static */
void nsLayoutUtils::MarkDescendantsDirty(nsIFrame* aSubtreeRoot) {
  AutoTArray<nsIFrame*, 4> subtrees;
  subtrees.AppendElement(aSubtreeRoot);

  // dirty descendants, iterating over subtrees that may include
  // additional subtrees associated with placeholders
  do {
    nsIFrame* subtreeRoot = subtrees.PopLastElement();

    // Mark all descendants dirty (using an nsTArray stack rather than
    // recursion).
    // Note that ReflowInput::InitResizeFlags has some similar
    // code; see comments there for how and why it differs.
    AutoTArray<nsIFrame*, 32> stack;
    stack.AppendElement(subtreeRoot);

    do {
      nsIFrame* f = stack.PopLastElement();

      f->MarkIntrinsicISizesDirty();

      if (f->IsPlaceholderFrame()) {
        nsIFrame* oof = nsPlaceholderFrame::GetRealFrameForPlaceholder(f);
        if (!nsLayoutUtils::IsProperAncestorFrame(subtreeRoot, oof)) {
          // We have another distinct subtree we need to mark.
          subtrees.AppendElement(oof);
        }
      }

      for (const auto& childList : f->ChildLists()) {
        for (nsIFrame* kid : childList.mList) {
          stack.AppendElement(kid);
        }
      }
    } while (stack.Length() != 0);
  } while (subtrees.Length() != 0);
}

/* static */
void nsLayoutUtils::MarkIntrinsicISizesDirtyIfDependentOnBSize(
    nsIFrame* aFrame) {
  AutoTArray<nsIFrame*, 32> stack;
  stack.AppendElement(aFrame);

  do {
    nsIFrame* f = stack.PopLastElement();

    if (!f->HasAnyStateBits(
            NS_FRAME_DESCENDANT_INTRINSIC_ISIZE_DEPENDS_ON_BSIZE)) {
      continue;
    }
    f->MarkIntrinsicISizesDirty();

    for (const auto& childList : f->ChildLists()) {
      for (nsIFrame* kid : childList.mList) {
        stack.AppendElement(kid);
      }
    }
  } while (stack.Length() != 0);
}

nsSize nsLayoutUtils::ComputeAutoSizeWithIntrinsicDimensions(
    nscoord minWidth, nscoord minHeight, nscoord maxWidth, nscoord maxHeight,
    nscoord tentWidth, nscoord tentHeight) {
  // Now apply min/max-width/height - CSS 2.1 sections 10.4 and 10.7:

  if (minWidth > maxWidth) {
    maxWidth = minWidth;
  }
  if (minHeight > maxHeight) {
    maxHeight = minHeight;
  }

  nscoord heightAtMaxWidth, heightAtMinWidth, widthAtMaxHeight,
      widthAtMinHeight;

  if (tentWidth > 0) {
    heightAtMaxWidth = NSCoordMulDiv(maxWidth, tentHeight, tentWidth);
    if (heightAtMaxWidth < minHeight) {
      heightAtMaxWidth = minHeight;
    }
    heightAtMinWidth = NSCoordMulDiv(minWidth, tentHeight, tentWidth);
    if (heightAtMinWidth > maxHeight) {
      heightAtMinWidth = maxHeight;
    }
  } else {
    heightAtMaxWidth = heightAtMinWidth =
        CSSMinMax(tentHeight, minHeight, maxHeight);
  }

  if (tentHeight > 0) {
    widthAtMaxHeight = NSCoordMulDiv(maxHeight, tentWidth, tentHeight);
    if (widthAtMaxHeight < minWidth) {
      widthAtMaxHeight = minWidth;
    }
    widthAtMinHeight = NSCoordMulDiv(minHeight, tentWidth, tentHeight);
    if (widthAtMinHeight > maxWidth) {
      widthAtMinHeight = maxWidth;
    }
  } else {
    widthAtMaxHeight = widthAtMinHeight =
        CSSMinMax(tentWidth, minWidth, maxWidth);
  }

  // The table at http://www.w3.org/TR/CSS21/visudet.html#min-max-widths :

  nscoord width, height;

  if (tentWidth > maxWidth) {
    if (tentHeight > maxHeight) {
      if (int64_t(maxWidth) * int64_t(tentHeight) <=
          int64_t(maxHeight) * int64_t(tentWidth)) {
        width = maxWidth;
        height = heightAtMaxWidth;
      } else {
        width = widthAtMaxHeight;
        height = maxHeight;
      }
    } else {
      // This also covers "(w > max-width) and (h < min-height)" since in
      // that case (max-width/w < 1), and with (h < min-height):
      //   max(max-width * h/w, min-height) == min-height
      width = maxWidth;
      height = heightAtMaxWidth;
    }
  } else if (tentWidth < minWidth) {
    if (tentHeight < minHeight) {
      if (int64_t(minWidth) * int64_t(tentHeight) <=
          int64_t(minHeight) * int64_t(tentWidth)) {
        width = widthAtMinHeight;
        height = minHeight;
      } else {
        width = minWidth;
        height = heightAtMinWidth;
      }
    } else {
      // This also covers "(w < min-width) and (h > max-height)" since in
      // that case (min-width/w > 1), and with (h > max-height):
      //   min(min-width * h/w, max-height) == max-height
      width = minWidth;
      height = heightAtMinWidth;
    }
  } else {
    if (tentHeight > maxHeight) {
      width = widthAtMaxHeight;
      height = maxHeight;
    } else if (tentHeight < minHeight) {
      width = widthAtMinHeight;
      height = minHeight;
    } else {
      width = tentWidth;
      height = tentHeight;
    }
  }

  return nsSize(width, height);
}

static nscolor DarkenColor(nscolor aColor) {
  uint16_t hue, sat, value;
  uint8_t alpha;

  // convert the RBG to HSV so we can get the lightness (which is the v)
  NS_RGB2HSV(aColor, hue, sat, value, alpha);

  // The goal here is to send white to black while letting colored
  // stuff stay colored... So we adopt the following approach.
  // Something with sat = 0 should end up with value = 0.  Something
  // with a high sat can end up with a high value and it's ok.... At
  // the same time, we don't want to make things lighter.  Do
  // something simple, since it seems to work.
  if (value > sat) {
    value = sat;
    // convert this color back into the RGB color space.
    NS_HSV2RGB(aColor, hue, sat, value, alpha);
  }
  return aColor;
}

// Check whether we should darken text/decoration colors. We need to do this if
// background images and colors are being suppressed, because that means
// light text will not be visible against the (presumed light-colored)
// background.
static bool ShouldDarkenColors(nsIFrame* aFrame) {
  nsPresContext* pc = aFrame->PresContext();
  if (pc->GetBackgroundColorDraw() || pc->GetBackgroundImageDraw()) {
    return false;
  }
  return aFrame->StyleVisibility()->mPrintColorAdjust !=
         StylePrintColorAdjust::Exact;
}

nscolor nsLayoutUtils::DarkenColorIfNeeded(nsIFrame* aFrame, nscolor aColor) {
  return ShouldDarkenColors(aFrame) ? DarkenColor(aColor) : aColor;
}

gfxFloat nsLayoutUtils::GetSnappedBaselineY(nsIFrame* aFrame,
                                            gfxContext* aContext, nscoord aY,
                                            nscoord aAscent) {
  gfxFloat appUnitsPerDevUnit = aFrame->PresContext()->AppUnitsPerDevPixel();
  gfxFloat baseline = gfxFloat(aY) + aAscent;
  gfxRect putativeRect(0, baseline / appUnitsPerDevUnit, 1, 1);
  if (!aContext->UserToDevicePixelSnapped(
          putativeRect, gfxContext::SnapOption::IgnoreScale)) {
    return baseline;
  }
  return aContext->DeviceToUser(putativeRect.TopLeft()).y * appUnitsPerDevUnit;
}

gfxFloat nsLayoutUtils::GetSnappedBaselineX(nsIFrame* aFrame,
                                            gfxContext* aContext, nscoord aX,
                                            nscoord aAscent) {
  gfxFloat appUnitsPerDevUnit = aFrame->PresContext()->AppUnitsPerDevPixel();
  gfxFloat baseline = gfxFloat(aX) + aAscent;
  gfxRect putativeRect(baseline / appUnitsPerDevUnit, 0, 1, 1);
  if (!aContext->UserToDevicePixelSnapped(
          putativeRect, gfxContext::SnapOption::IgnoreScale)) {
    return baseline;
  }
  return aContext->DeviceToUser(putativeRect.TopLeft()).x * appUnitsPerDevUnit;
}

// Hard limit substring lengths to 8000 characters ... this lets us statically
// size the cluster buffer array in FindSafeLength
#define MAX_GFX_TEXT_BUF_SIZE 8000

static int32_t FindSafeLength(const char16_t* aString, uint32_t aLength,
                              uint32_t aMaxChunkLength) {
  if (aLength <= aMaxChunkLength) {
    return aLength;
  }

  int32_t len = aMaxChunkLength;

  // Ensure that we don't break inside a surrogate pair
  while (len > 0 && NS_IS_LOW_SURROGATE(aString[len])) {
    len--;
  }
  if (len == 0) {
    // We don't want our caller to go into an infinite loop, so don't
    // return zero. It's hard to imagine how we could actually get here
    // unless there are languages that allow clusters of arbitrary size.
    // If there are and someone feeds us a 500+ character cluster, too
    // bad.
    return aMaxChunkLength;
  }
  return len;
}

static int32_t GetMaxChunkLength(nsFontMetrics& aFontMetrics) {
  return std::min(aFontMetrics.GetMaxStringLength(), MAX_GFX_TEXT_BUF_SIZE);
}

nscoord nsLayoutUtils::AppUnitWidthOfString(const char16_t* aString,
                                            uint32_t aLength,
                                            nsFontMetrics& aFontMetrics,
                                            DrawTarget* aDrawTarget) {
  uint32_t maxChunkLength = GetMaxChunkLength(aFontMetrics);
  nscoord width = 0;
  while (aLength > 0) {
    int32_t len = FindSafeLength(aString, aLength, maxChunkLength);
    width += aFontMetrics.GetWidth(aString, len, aDrawTarget);
    aLength -= len;
    aString += len;
  }
  return width;
}

nscoord nsLayoutUtils::AppUnitWidthOfStringBidi(const char16_t* aString,
                                                uint32_t aLength,
                                                const nsIFrame* aFrame,
                                                nsFontMetrics& aFontMetrics,
                                                gfxContext& aContext) {
  nsPresContext* presContext = aFrame->PresContext();
  if (presContext->BidiEnabled()) {
    mozilla::intl::BidiEmbeddingLevel level =
        nsBidiPresUtils::BidiLevelFromStyle(aFrame->Style());
    return nsBidiPresUtils::MeasureTextWidth(
        aString, aLength, level, presContext, aContext, aFontMetrics);
  }
  aFontMetrics.SetTextRunRTL(false);
  aFontMetrics.SetVertical(aFrame->GetWritingMode().IsVertical());
  aFontMetrics.SetTextOrientation(aFrame->StyleVisibility()->mTextOrientation);
  return nsLayoutUtils::AppUnitWidthOfString(aString, aLength, aFontMetrics,
                                             aContext.GetDrawTarget());
}

bool nsLayoutUtils::StringWidthIsGreaterThan(const nsString& aString,
                                             nsFontMetrics& aFontMetrics,
                                             DrawTarget* aDrawTarget,
                                             nscoord aWidth) {
  const char16_t* string = aString.get();
  uint32_t length = aString.Length();
  uint32_t maxChunkLength = GetMaxChunkLength(aFontMetrics);
  nscoord width = 0;
  while (length > 0) {
    int32_t len = FindSafeLength(string, length, maxChunkLength);
    width += aFontMetrics.GetWidth(string, len, aDrawTarget);
    if (width > aWidth) {
      return true;
    }
    length -= len;
    string += len;
  }
  return false;
}

nsBoundingMetrics nsLayoutUtils::AppUnitBoundsOfString(
    const char16_t* aString, uint32_t aLength, nsFontMetrics& aFontMetrics,
    DrawTarget* aDrawTarget) {
  uint32_t maxChunkLength = GetMaxChunkLength(aFontMetrics);
  int32_t len = FindSafeLength(aString, aLength, maxChunkLength);
  // Assign directly in the first iteration. This ensures that
  // negative ascent/descent can be returned and the left bearing
  // is properly initialized.
  nsBoundingMetrics totalMetrics =
      aFontMetrics.GetBoundingMetrics(aString, len, aDrawTarget);
  aLength -= len;
  aString += len;

  while (aLength > 0) {
    len = FindSafeLength(aString, aLength, maxChunkLength);
    nsBoundingMetrics metrics =
        aFontMetrics.GetBoundingMetrics(aString, len, aDrawTarget);
    totalMetrics += metrics;
    aLength -= len;
    aString += len;
  }
  return totalMetrics;
}

void nsLayoutUtils::DrawString(const nsIFrame* aFrame,
                               nsFontMetrics& aFontMetrics,
                               gfxContext* aContext, const char16_t* aString,
                               int32_t aLength, nsPoint aPoint,
                               ComputedStyle* aComputedStyle,
                               DrawStringFlags aFlags) {
  nsresult rv = NS_ERROR_FAILURE;

  // If caller didn't pass a style, use the frame's.
  if (!aComputedStyle) {
    aComputedStyle = aFrame->Style();
  }

  if (aFlags & DrawStringFlags::ForceHorizontal) {
    aFontMetrics.SetVertical(false);
  } else {
    aFontMetrics.SetVertical(WritingMode(aComputedStyle).IsVertical());
  }

  aFontMetrics.SetTextOrientation(
      aComputedStyle->StyleVisibility()->mTextOrientation);

  nsPresContext* presContext = aFrame->PresContext();
  if (presContext->BidiEnabled()) {
    mozilla::intl::BidiEmbeddingLevel level =
        nsBidiPresUtils::BidiLevelFromStyle(aComputedStyle);
    rv = nsBidiPresUtils::RenderText(aString, aLength, level, presContext,
                                     *aContext, aContext->GetDrawTarget(),
                                     aFontMetrics, aPoint.x, aPoint.y);
  }
  if (NS_FAILED(rv)) {
    aFontMetrics.SetTextRunRTL(false);
    DrawUniDirString(aString, aLength, aPoint, aFontMetrics, *aContext);
  }
}

void nsLayoutUtils::DrawUniDirString(const char16_t* aString, uint32_t aLength,
                                     const nsPoint& aPoint,
                                     nsFontMetrics& aFontMetrics,
                                     gfxContext& aContext) {
  nscoord x = aPoint.x;
  nscoord y = aPoint.y;

  uint32_t maxChunkLength = GetMaxChunkLength(aFontMetrics);
  if (aLength <= maxChunkLength) {
    aFontMetrics.DrawString(aString, aLength, x, y, &aContext,
                            aContext.GetDrawTarget());
    return;
  }

  bool isRTL = aFontMetrics.GetTextRunRTL();

  // If we're drawing right to left, we must start at the end.
  if (isRTL) {
    x += nsLayoutUtils::AppUnitWidthOfString(aString, aLength, aFontMetrics,
                                             aContext.GetDrawTarget());
  }

  while (aLength > 0) {
    int32_t len = FindSafeLength(aString, aLength, maxChunkLength);
    nscoord width =
        aFontMetrics.GetWidth(aString, len, aContext.GetDrawTarget());
    if (isRTL) {
      x -= width;
    }
    aFontMetrics.DrawString(aString, len, x, y, &aContext,
                            aContext.GetDrawTarget());
    if (!isRTL) {
      x += width;
    }
    aLength -= len;
    aString += len;
  }
}

/* static */
void nsLayoutUtils::PaintTextShadow(
    const nsIFrame* aFrame, gfxContext* aContext, const nsRect& aTextRect,
    const nsRect& aDirtyRect, const nscolor& aForegroundColor,
    TextShadowCallback aCallback, void* aCallbackData) {
  const nsStyleText* textStyle = aFrame->StyleText();
  auto shadows = textStyle->mTextShadow.AsSpan();
  if (shadows.IsEmpty()) {
    return;
  }

  // Text shadow happens with the last value being painted at the back,
  // ie. it is painted first.
  gfxContext* aDestCtx = aContext;
  for (auto& shadow : Reversed(shadows)) {
    nsPoint shadowOffset(shadow.horizontal.ToAppUnits(),
                         shadow.vertical.ToAppUnits());
    nscoord blurRadius = std::max(shadow.blur.ToAppUnits(), 0);

    nsRect shadowRect(aTextRect);
    shadowRect.MoveBy(shadowOffset);

    nsPresContext* presCtx = aFrame->PresContext();
    nsContextBoxBlur contextBoxBlur;

    nscolor shadowColor = shadow.color.CalcColor(aForegroundColor);

    // Webrender just needs the shadow details
    if (auto* textDrawer = aContext->GetTextDrawer()) {
      wr::Shadow wrShadow;

      wrShadow.offset = {
          presCtx->AppUnitsToFloatDevPixels(shadow.horizontal.ToAppUnits()),
          presCtx->AppUnitsToFloatDevPixels(shadow.vertical.ToAppUnits())};

      wrShadow.blur_radius = presCtx->AppUnitsToFloatDevPixels(blurRadius);
      wrShadow.color = wr::ToColorF(ToDeviceColor(shadowColor));

      // Gecko already inflates the bounding rect of text shadows,
      // so tell WR not to inflate again.
      bool inflate = false;
      textDrawer->AppendShadow(wrShadow, inflate);
      continue;
    }

    gfxContext* shadowContext = contextBoxBlur.Init(
        shadowRect, 0, blurRadius, presCtx->AppUnitsPerDevPixel(), aDestCtx,
        aDirtyRect, nullptr,
        nsContextBoxBlur::DISABLE_HARDWARE_ACCELERATION_BLUR);
    if (!shadowContext) {
      continue;
    }

    aDestCtx->Save();
    aDestCtx->NewPath();
    aDestCtx->SetColor(sRGBColor::FromABGR(shadowColor));

    // The callback will draw whatever we want to blur as a shadow.
    aCallback(shadowContext, shadowOffset, shadowColor, aCallbackData);

    contextBoxBlur.DoPaint();
    aDestCtx->Restore();
  }
}

/* static */
nscoord nsLayoutUtils::GetCenteredFontBaseline(nsFontMetrics* aFontMetrics,
                                               nscoord aLineHeight,
                                               bool aIsInverted) {
  nscoord fontAscent =
      aIsInverted ? aFontMetrics->MaxDescent() : aFontMetrics->MaxAscent();
  nscoord fontHeight = aFontMetrics->MaxHeight();

  nscoord leading = aLineHeight - fontHeight;
  return fontAscent + leading / 2;
}

/* static */
bool nsLayoutUtils::GetFirstLineBaseline(WritingMode aWritingMode,
                                         const nsIFrame* aFrame,
                                         nscoord* aResult) {
  LinePosition position;
  if (!GetFirstLinePosition(aWritingMode, aFrame, &position)) {
    return false;
  }
  *aResult = position.mBaseline;
  return true;
}

/* static */
bool nsLayoutUtils::GetFirstLinePosition(WritingMode aWM,
                                         const nsIFrame* aFrame,
                                         LinePosition* aResult) {
  if (aFrame->StyleDisplay()->IsContainLayout()) {
    return false;
  }
  const nsBlockFrame* block = do_QueryFrame(aFrame);
  if (!block) {
    // For the first-line baseline we also have to check for a table, and if
    // so, use the baseline of its first row.
    LayoutFrameType fType = aFrame->Type();
    if (fType == LayoutFrameType::TableWrapper ||
        fType == LayoutFrameType::FlexContainer ||
        fType == LayoutFrameType::GridContainer) {
      if ((fType == LayoutFrameType::GridContainer &&
           aFrame->HasAnyStateBits(NS_STATE_GRID_SYNTHESIZE_BASELINE)) ||
          (fType == LayoutFrameType::FlexContainer &&
           aFrame->HasAnyStateBits(NS_STATE_FLEX_SYNTHESIZE_BASELINE)) ||
          (fType == LayoutFrameType::TableWrapper &&
           static_cast<const nsTableWrapperFrame*>(aFrame)->GetRowCount() ==
               0)) {
        // empty grid/flex/table container
        aResult->mBStart = 0;
        aResult->mBaseline = Baseline::SynthesizeBOffsetFromBorderBox(
            aFrame, aWM, BaselineSharingGroup::First);
        aResult->mBEnd = aFrame->BSize(aWM);
        return true;
      }
      if (fType == LayoutFrameType::TableWrapper &&
          aFrame->GetWritingMode().IsOrthogonalTo(aWM)) {
        // For tables, the upcoming GetLogicalBaseline call would determine the
        // table's baseline from its first row that has a baseline. However:
        // this doesn't make sense for an orthogonal writing mode, so in that
        // case we report no baseline instead. The table wrapper and its rows
        // should flow the same way, so we can bail out early, but this logic
        // wouldn't be correct to transplant into other places in the codebase
        // (Depending on how bug 1786633 is resolved).
        return false;
      }
      aResult->mBStart = 0;
      aResult->mBaseline = aFrame->GetLogicalBaseline(aWM);
      // This is what we want for the list bullet caller; not sure if
      // other future callers will want the same.
      aResult->mBEnd = aFrame->BSize(aWM);
      return true;
    }

    // For first-line baselines, we have to consider scroll frames.
    if (const ScrollContainerFrame* sFrame = do_QueryFrame(aFrame)) {
      LinePosition kidPosition;
      if (GetFirstLinePosition(aWM, sFrame->GetScrolledFrame(), &kidPosition)) {
        // Consider only the border (Padding is ignored, since
        // `-moz-scrolled-content` inherits and handles the padding) that
        // contributes to the kid's position, not the scrolling, so we get the
        // initial position.
        *aResult = kidPosition + aFrame->GetLogicalUsedBorder(aWM).BStart(aWM);
        // Don't want to move the line's block positioning, but the baseline
        // needs to be clamped (See bug 1791069).
        aResult->mBaseline = CSSMinMax(aResult->mBaseline, 0,
                                       aFrame->GetLogicalSize(aWM).BSize(aWM));
        return true;
      }
      return false;
    }

    if (fType == LayoutFrameType::FieldSet) {
      LinePosition kidPosition;
      // Get the first baseline from the fieldset content, not from the legend.
      nsIFrame* kid = static_cast<const nsFieldSetFrame*>(aFrame)->GetInner();
      if (kid && GetFirstLinePosition(aWM, kid, &kidPosition)) {
        *aResult = kidPosition +
                   kid->GetLogicalNormalPosition(aWM, aFrame->GetSize()).B(aWM);
        return true;
      }
      return false;
    }

    if (fType == LayoutFrameType::ColumnSet) {
      // Note(dshin): This is basically the same as
      // `nsColumnSetFrame::GetNaturalBaselineBOffset`, but with line start and
      // end, all stored in `LinePosition`. Field value apart from baseline is
      // used in one other place
      // (`nsBlockFrame`) - if that goes away, this becomes a duplication that
      // should be removed.
      LinePosition kidPosition;
      for (const auto* kid : aFrame->PrincipalChildList()) {
        LinePosition position;
        if (!GetFirstLinePosition(aWM, kid, &position)) {
          continue;
        }
        if (position.mBaseline < kidPosition.mBaseline) {
          kidPosition = position;
        }
      }
      if (kidPosition.mBaseline != nscoord_MAX) {
        *aResult = kidPosition;
        return true;
      }
    }

    // No baseline.
    return false;
  }

  for (const auto& line : block->Lines()) {
    if (line.IsBlock()) {
      const nsIFrame* kid = line.mFirstChild;
      LinePosition kidPosition;
      if (GetFirstLinePosition(aWM, kid, &kidPosition)) {
        // XXX Not sure if this is the correct value to use for container
        //    width here. It will only be used in vertical-rl layout,
        //    which we don't have full support and testing for yet.
        const auto& containerSize = line.mContainerSize;
        *aResult = kidPosition +
                   kid->GetLogicalNormalPosition(aWM, containerSize).B(aWM);
        return true;
      }
    } else {
      // XXX Is this the right test?  We have some bogus empty lines
      // floating around, but IsEmpty is perhaps too weak.
      if (0 != line.BSize() || !line.IsEmpty()) {
        nscoord bStart = line.BStart();
        aResult->mBStart = bStart;
        aResult->mBaseline = bStart + line.GetLogicalAscent();
        aResult->mBEnd = bStart + line.BSize();
        return true;
      }
    }
  }
  return false;
}

/* static */
bool nsLayoutUtils::GetLastLineBaseline(WritingMode aWM, const nsIFrame* aFrame,
                                        nscoord* aResult) {
  if (aFrame->StyleDisplay()->IsContainLayout()) {
    return false;
  }

  const nsBlockFrame* block = do_QueryFrame(aFrame);
  if (!block) {
    if (const ScrollContainerFrame* sFrame = do_QueryFrame(aFrame)) {
      // Use the baseline position only if the last line's baseline is within
      // the scrolling frame's box in the initial position.
      const auto* scrolledFrame = sFrame->GetScrolledFrame();
      if (!GetLastLineBaseline(aWM, scrolledFrame, aResult)) {
        return false;
      }
      // Go from scrolled frame to scrollable frame position.
      *aResult += aFrame->GetLogicalUsedBorder(aWM).BStart(aWM);
      const auto maxBaseline = aFrame->GetLogicalSize(aWM).BSize(aWM);
      // Clamp the last baseline to border (See bug 1791069).
      *aResult = std::clamp(*aResult, 0, maxBaseline);
      return true;
    }

    // No need to duplicate the baseline logic (Unlike `GetFirstLinePosition`,
    // we don't need to return any other value apart from baseline), just defer
    // to `GetNaturalBaselineBOffset`. Technically, we could do this at
    // `ColumnSetWrapperFrame` level, but this keeps it symmetric to
    // `GetFirstLinePosition`.
    if (aFrame->IsColumnSetFrame()) {
      const auto baseline = aFrame->GetNaturalBaselineBOffset(
          aWM, BaselineSharingGroup::Last, BaselineExportContext::Other);
      if (!baseline) {
        return false;
      }
      *aResult = aFrame->BSize(aWM) - *baseline;
      return true;
    }
    // No baseline.
    return false;
  }

  for (nsBlockFrame::ConstReverseLineIterator line = block->LinesRBegin(),
                                              line_end = block->LinesREnd();
       line != line_end; ++line) {
    if (line->IsBlock()) {
      nsIFrame* kid = line->mFirstChild;
      nscoord kidBaseline;
      const nsSize& containerSize = line->mContainerSize;
      if (GetLastLineBaseline(aWM, kid, &kidBaseline)) {
        // Ignore relative positioning for baseline calculations
        *aResult = kidBaseline +
                   kid->GetLogicalNormalPosition(aWM, containerSize).B(aWM);
        return true;
      }
      if (kid->IsScrollContainerFrame()) {
        // Defer to nsIFrame::GetLogicalBaseline (which synthesizes a baseline
        // from the margin-box).
        kidBaseline = kid->GetLogicalBaseline(aWM);
        *aResult = kidBaseline +
                   kid->GetLogicalNormalPosition(aWM, containerSize).B(aWM);
        return true;
      }
    } else {
      // XXX Is this the right test?  We have some bogus empty lines
      // floating around, but IsEmpty is perhaps too weak.
      if (line->BSize() != 0 || !line->IsEmpty()) {
        *aResult = line->BStart() + line->GetLogicalAscent();
        return true;
      }
    }
  }
  return false;
}

static nscoord CalculateBlockContentBEnd(WritingMode aWM,
                                         nsBlockFrame* aFrame) {
  MOZ_ASSERT(aFrame, "null ptr");

  nscoord contentBEnd = 0;

  for (const auto& line : aFrame->Lines()) {
    if (line.IsBlock()) {
      nsIFrame* child = line.mFirstChild;
      const auto& containerSize = line.mContainerSize;
      nscoord offset =
          child->GetLogicalNormalPosition(aWM, containerSize).B(aWM);
      contentBEnd =
          std::max(contentBEnd,
                   nsLayoutUtils::CalculateContentBEnd(aWM, child) + offset);
    } else {
      contentBEnd = std::max(contentBEnd, line.BEnd());
    }
  }
  return contentBEnd;
}

/* static */
nscoord nsLayoutUtils::CalculateContentBEnd(WritingMode aWM, nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame, "null ptr");

  nscoord contentBEnd = aFrame->BSize(aWM);

  // We want scrollable overflow rather than visual because this
  // calculation is intended to affect layout.
  LogicalSize overflowSize(aWM, aFrame->ScrollableOverflowRect().Size());
  if (overflowSize.BSize(aWM) > contentBEnd) {
    FrameChildListIDs skip = {FrameChildListID::Overflow,
                              FrameChildListID::ExcessOverflowContainers,
                              FrameChildListID::OverflowOutOfFlow};
    nsBlockFrame* blockFrame = do_QueryFrame(aFrame);
    if (blockFrame) {
      contentBEnd =
          std::max(contentBEnd, CalculateBlockContentBEnd(aWM, blockFrame));
      skip += FrameChildListID::Principal;
    }
    for (const auto& [list, listID] : aFrame->ChildLists()) {
      if (!skip.contains(listID)) {
        for (nsIFrame* child : list) {
          nscoord offset =
              child->GetLogicalNormalPosition(aWM, aFrame->GetSize()).B(aWM);
          contentBEnd =
              std::max(contentBEnd, CalculateContentBEnd(aWM, child) + offset);
        }
      }
    }
  }
  return contentBEnd;
}

/* static */
nsIFrame* nsLayoutUtils::GetClosestLayer(nsIFrame* aFrame) {
  nsIFrame* layer;
  for (layer = aFrame; layer; layer = layer->GetParent()) {
    if (layer->IsAbsPosContainingBlock() ||
        (layer->GetParent() && layer->GetParent()->IsScrollContainerFrame())) {
      break;
    }
  }
  if (layer) {
    return layer;
  }
  return aFrame->PresShell()->GetRootFrame();
}

SamplingFilter nsLayoutUtils::GetSamplingFilterForFrame(nsIFrame* aForFrame) {
  switch (aForFrame->UsedImageRendering()) {
    case StyleImageRendering::Smooth:
    case StyleImageRendering::Optimizequality:
      return SamplingFilter::LINEAR;
    case StyleImageRendering::CrispEdges:
    case StyleImageRendering::Optimizespeed:
    case StyleImageRendering::Pixelated:
      return SamplingFilter::POINT;
    case StyleImageRendering::Auto:
      return SamplingFilter::GOOD;
  }
  MOZ_ASSERT_UNREACHABLE("Unknown image-rendering value");
  return SamplingFilter::GOOD;
}

/**
 * Given an image being drawn into an appunit coordinate system, and
 * a point in that coordinate system, map the point back into image
 * pixel space.
 * @param aSize the size of the image, in pixels
 * @param aDest the rectangle that the image is being mapped into
 * @param aPt a point in the same coordinate system as the rectangle
 */
static gfxPoint MapToFloatImagePixels(const gfxSize& aSize,
                                      const gfxRect& aDest,
                                      const gfxPoint& aPt) {
  return gfxPoint(((aPt.x - aDest.X()) * aSize.width) / aDest.Width(),
                  ((aPt.y - aDest.Y()) * aSize.height) / aDest.Height());
}

/**
 * Given an image being drawn into an pixel-based coordinate system, and
 * a point in image space, map the point into the pixel-based coordinate
 * system.
 * @param aSize the size of the image, in pixels
 * @param aDest the rectangle that the image is being mapped into
 * @param aPt a point in image space
 */
static gfxPoint MapToFloatUserPixels(const gfxSize& aSize, const gfxRect& aDest,
                                     const gfxPoint& aPt) {
  return gfxPoint(aPt.x * aDest.Width() / aSize.width + aDest.X(),
                  aPt.y * aDest.Height() / aSize.height + aDest.Y());
}

/* static */
gfxRect nsLayoutUtils::RectToGfxRect(const nsRect& aRect,
                                     int32_t aAppUnitsPerDevPixel) {
  return gfxRect(gfxFloat(aRect.x) / aAppUnitsPerDevPixel,
                 gfxFloat(aRect.y) / aAppUnitsPerDevPixel,
                 gfxFloat(aRect.width) / aAppUnitsPerDevPixel,
                 gfxFloat(aRect.height) / aAppUnitsPerDevPixel);
}

struct SnappedImageDrawingParameters {
  // A transform from image space to device space.
  gfxMatrix imageSpaceToDeviceSpace;
  // The size at which the image should be drawn (which may not be its
  // intrinsic size due to, for example, HQ scaling).
  nsIntSize size;
  // The region in tiled image space which will be drawn, with an associated
  // region to which sampling should be restricted.
  ImageRegion region;
  // The default viewport size for SVG images, which we use unless a different
  // one has been explicitly specified. This is the same as |size| except that
  // it does not take into account any transformation on the gfxContext we're
  // drawing to - for example, CSS transforms are not taken into account.
  CSSIntSize svgViewportSize;
  // Whether there's anything to draw at all.
  bool shouldDraw;

  SnappedImageDrawingParameters()
      : region(ImageRegion::Empty()), shouldDraw(false) {}

  SnappedImageDrawingParameters(const gfxMatrix& aImageSpaceToDeviceSpace,
                                const nsIntSize& aSize,
                                const ImageRegion& aRegion,
                                const CSSIntSize& aSVGViewportSize)
      : imageSpaceToDeviceSpace(aImageSpaceToDeviceSpace),
        size(aSize),
        region(aRegion),
        svgViewportSize(aSVGViewportSize),
        shouldDraw(true) {}
};

/**
 * Given two axis-aligned rectangles, returns the transformation that maps the
 * first onto the second.
 *
 * @param aFrom The rect to be transformed.
 * @param aTo The rect that aFrom should be mapped onto by the transformation.
 */
static gfxMatrix TransformBetweenRects(const gfxRect& aFrom,
                                       const gfxRect& aTo) {
  MatrixScalesDouble scale(aTo.width / aFrom.width, aTo.height / aFrom.height);
  gfxPoint translation(aTo.x - aFrom.x * scale.xScale,
                       aTo.y - aFrom.y * scale.yScale);
  return gfxMatrix(scale.xScale, 0, 0, scale.yScale, translation.x,
                   translation.y);
}

static nsRect TileNearRect(const nsRect& aAnyTile, const nsRect& aTargetRect) {
  nsPoint distance = aTargetRect.TopLeft() - aAnyTile.TopLeft();
  return aAnyTile + nsPoint(distance.x / aAnyTile.width * aAnyTile.width,
                            distance.y / aAnyTile.height * aAnyTile.height);
}

static gfxFloat StableRound(gfxFloat aValue) {
  // Values slightly less than 0.5 should round up like 0.5 would; we're
  // assuming they were meant to be 0.5.
  return floor(aValue + 0.5001);
}

static gfxPoint StableRound(const gfxPoint& aPoint) {
  return gfxPoint(StableRound(aPoint.x), StableRound(aPoint.y));
}

/**
 * Given a set of input parameters, compute certain output parameters
 * for drawing an image with the image snapping algorithm.
 * See https://wiki.mozilla.org/Gecko:Image_Snapping_and_Rendering
 *
 *  @see nsLayoutUtils::DrawImage() for the descriptions of input parameters
 */
static SnappedImageDrawingParameters ComputeSnappedImageDrawingParameters(
    gfxContext* aCtx, int32_t aAppUnitsPerDevPixel, const nsRect aDest,
    const nsRect aFill, const nsPoint aAnchor, const nsRect aDirty,
    imgIContainer* aImage, const SamplingFilter aSamplingFilter,
    uint32_t aImageFlags, ExtendMode aExtendMode) {
  if (aDest.IsEmpty() || aFill.IsEmpty()) {
    return SnappedImageDrawingParameters();
  }

  // Avoid unnecessarily large offsets.
  bool doTile = !aDest.Contains(aFill);
  nsRect appUnitDest =
      doTile ? TileNearRect(aDest, aFill.Intersect(aDirty)) : aDest;
  nsPoint anchor = aAnchor + (appUnitDest.TopLeft() - aDest.TopLeft());

  gfxRect devPixelDest =
      nsLayoutUtils::RectToGfxRect(appUnitDest, aAppUnitsPerDevPixel);
  gfxRect devPixelFill =
      nsLayoutUtils::RectToGfxRect(aFill, aAppUnitsPerDevPixel);
  gfxRect devPixelDirty =
      nsLayoutUtils::RectToGfxRect(aDirty, aAppUnitsPerDevPixel);

  gfxMatrix currentMatrix = aCtx->CurrentMatrixDouble();
  gfxRect fill = devPixelFill;
  gfxRect dest = devPixelDest;
  bool didSnap;
  // Snap even if we have a scale in the context. But don't snap if
  // we have something that's not translation+scale, or if the scale flips in
  // the X or Y direction, because snapped image drawing can't handle that yet.
  if (!currentMatrix.HasNonAxisAlignedTransform() && currentMatrix._11 > 0.0 &&
      currentMatrix._22 > 0.0 &&
      aCtx->UserToDevicePixelSnapped(fill,
                                     gfxContext::SnapOption::IgnoreScale) &&
      aCtx->UserToDevicePixelSnapped(dest,
                                     gfxContext::SnapOption::IgnoreScale)) {
    // We snapped. On this code path, |fill| and |dest| take into account
    // currentMatrix's transform.
    didSnap = true;
  } else {
    // We didn't snap. On this code path, |fill| and |dest| do not take into
    // account currentMatrix's transform.
    didSnap = false;
    fill = devPixelFill;
    dest = devPixelDest;
  }

  // If we snapped above, |dest| already takes into account |currentMatrix|'s
  // scale and has integer coordinates. If not, we need these properties to
  // compute the optimal drawn image size, so compute |snappedDestSize| here.
  gfxSize snappedDestSize = dest.Size();
  auto scaleFactors = currentMatrix.ScaleFactors();
  if (!didSnap) {
    snappedDestSize.Scale(scaleFactors.xScale, scaleFactors.yScale);
    snappedDestSize.width = NS_round(snappedDestSize.width);
    snappedDestSize.height = NS_round(snappedDestSize.height);
  }

  // We need to be sure that this is at least one pixel in width and height,
  // or we'll end up drawing nothing even if we have a nonempty fill.
  snappedDestSize.width = std::max(snappedDestSize.width, 1.0);
  snappedDestSize.height = std::max(snappedDestSize.height, 1.0);

  // Bail if we're not going to end up drawing anything.
  if (fill.IsEmpty()) {
    return SnappedImageDrawingParameters();
  }

  nsIntSize intImageSize = aImage->OptimalImageSizeForDest(
      snappedDestSize, imgIContainer::FRAME_CURRENT, aSamplingFilter,
      aImageFlags);

  nsIntSize svgViewportSize;
  if (scaleFactors.xScale == 1.0 && scaleFactors.yScale == 1.0) {
    // intImageSize is scaled by currentMatrix. But since there are no scale
    // factors in currentMatrix, it is safe to assign intImageSize to
    // svgViewportSize directly.
    svgViewportSize = intImageSize;
  } else {
    // We should not take into account any transformation of currentMatrix
    // when computing svg viewport size. Since currentMatrix contains scale
    // factors, we need to recompute SVG viewport by unscaled devPixelDest.
    svgViewportSize = aImage->OptimalImageSizeForDest(
        devPixelDest.Size(), imgIContainer::FRAME_CURRENT, aSamplingFilter,
        aImageFlags);
  }

  gfxSize imageSize(intImageSize.width, intImageSize.height);

  // Compute the set of pixels that would be sampled by an ideal rendering
  gfxPoint subimageTopLeft =
      MapToFloatImagePixels(imageSize, devPixelDest, devPixelFill.TopLeft());
  gfxPoint subimageBottomRight = MapToFloatImagePixels(
      imageSize, devPixelDest, devPixelFill.BottomRight());
  gfxRect subimage;
  subimage.MoveTo(NSToIntFloor(subimageTopLeft.x),
                  NSToIntFloor(subimageTopLeft.y));
  subimage.SizeTo(NSToIntCeil(subimageBottomRight.x) - subimage.x,
                  NSToIntCeil(subimageBottomRight.y) - subimage.y);

  if (subimage.IsEmpty()) {
    // Bail if the subimage is empty (we're not going to be drawing anything).
    return SnappedImageDrawingParameters();
  }

  gfxMatrix transform;
  gfxMatrix invTransform;

  bool anchorAtUpperLeft =
      anchor.x == appUnitDest.x && anchor.y == appUnitDest.y;
  bool exactlyOneImageCopy = aFill.IsEqualEdges(appUnitDest);
  if (anchorAtUpperLeft && exactlyOneImageCopy) {
    // The simple case: we can ignore the anchor point and compute the
    // transformation from the sampled region (the subimage) to the fill rect.
    // This approach is preferable when it works since it tends to produce
    // less numerical error.
    transform = TransformBetweenRects(subimage, fill);
    invTransform = TransformBetweenRects(fill, subimage);
  } else {
    // The more complicated case: we compute the transformation from the
    // image rect positioned at the image space anchor point to the dest rect
    // positioned at the device space anchor point.

    // Compute the anchor point in both device space and image space.  This
    // code assumes that pixel-based devices have one pixel per device unit!
    gfxPoint anchorPoint(gfxFloat(anchor.x) / aAppUnitsPerDevPixel,
                         gfxFloat(anchor.y) / aAppUnitsPerDevPixel);
    gfxPoint imageSpaceAnchorPoint =
        MapToFloatImagePixels(imageSize, devPixelDest, anchorPoint);

    if (didSnap) {
      imageSpaceAnchorPoint = StableRound(imageSpaceAnchorPoint);
      anchorPoint = imageSpaceAnchorPoint;
      anchorPoint = MapToFloatUserPixels(imageSize, devPixelDest, anchorPoint);
      anchorPoint = currentMatrix.TransformPoint(anchorPoint);
      anchorPoint = StableRound(anchorPoint);
    }

    // Compute an unsnapped version of the dest rect's size. We continue to
    // follow the pattern that we take |currentMatrix| into account only if
    // |didSnap| is true.
    gfxSize unsnappedDestSize =
        didSnap ? devPixelDest.Size() * currentMatrix.ScaleFactors()
                : devPixelDest.Size();

    gfxRect anchoredDestRect(anchorPoint, unsnappedDestSize);
    gfxRect anchoredImageRect(imageSpaceAnchorPoint, imageSize);

    // Calculate anchoredDestRect with snapped fill rect when the devPixelFill
    // rect corresponds to just a single tile in that direction
    if (fill.Width() != devPixelFill.Width() &&
        devPixelDest.x == devPixelFill.x &&
        devPixelDest.XMost() == devPixelFill.XMost()) {
      anchoredDestRect.width = fill.width;
    }
    if (fill.Height() != devPixelFill.Height() &&
        devPixelDest.y == devPixelFill.y &&
        devPixelDest.YMost() == devPixelFill.YMost()) {
      anchoredDestRect.height = fill.height;
    }

    transform = TransformBetweenRects(anchoredImageRect, anchoredDestRect);
    invTransform = TransformBetweenRects(anchoredDestRect, anchoredImageRect);
  }

  // If the transform is not a straight translation by integers, then
  // filtering will occur, and restricting the fill rect to the dirty rect
  // would change the values computed for edge pixels, which we can't allow.
  // Also, if 'didSnap' is false then rounding out 'devPixelDirty' might not
  // produce pixel-aligned coordinates, which would also break the values
  // computed for edge pixels.
  if (didSnap && !invTransform.HasNonIntegerTranslation()) {
    // This form of Transform is safe to call since non-axis-aligned
    // transforms wouldn't be snapped.
    devPixelDirty = currentMatrix.TransformRect(devPixelDirty);
    devPixelDirty.RoundOut();
    fill = fill.Intersect(devPixelDirty);
  }
  if (fill.IsEmpty()) {
    return SnappedImageDrawingParameters();
  }

  gfxRect imageSpaceFill(didSnap ? invTransform.TransformRect(fill)
                                 : invTransform.TransformBounds(fill));

  // If we didn't snap, we need to post-multiply the matrix on the context to
  // get the final matrix we'll draw with, because we didn't take it into
  // account when computing the matrices above.
  if (!didSnap) {
    transform = transform * currentMatrix;
  }

  ExtendMode extendMode = (aImageFlags & imgIContainer::FLAG_CLAMP)
                              ? ExtendMode::CLAMP
                              : aExtendMode;
  // We were passed in the default extend mode but need to tile.
  if (extendMode == ExtendMode::CLAMP && doTile) {
    MOZ_ASSERT(!(aImageFlags & imgIContainer::FLAG_CLAMP));
    extendMode = ExtendMode::REPEAT;
  }

  ImageRegion region = ImageRegion::CreateWithSamplingRestriction(
      imageSpaceFill, subimage, extendMode);

  return SnappedImageDrawingParameters(
      transform, intImageSize, region,
      CSSIntSize(svgViewportSize.width, svgViewportSize.height));
}

static ImgDrawResult DrawImageInternal(
    gfxContext& aContext, nsPresContext* aPresContext, imgIContainer* aImage,
    const SamplingFilter aSamplingFilter, const nsRect& aDest,
    const nsRect& aFill, const nsPoint& aAnchor, const nsRect& aDirty,
    const SVGImageContext& aSVGContext, uint32_t aImageFlags,
    ExtendMode aExtendMode = ExtendMode::CLAMP, float aOpacity = 1.0) {
  ImgDrawResult result = ImgDrawResult::SUCCESS;

  aImageFlags |= imgIContainer::FLAG_ASYNC_NOTIFY;

  if (aPresContext->Type() == nsPresContext::eContext_Print) {
    // We want vector images to be passed on as vector commands, not a raster
    // image.
    aImageFlags |= imgIContainer::FLAG_BYPASS_SURFACE_CACHE;
  }
  if (aDest.Contains(aFill)) {
    aImageFlags |= imgIContainer::FLAG_CLAMP;
  }
  int32_t appUnitsPerDevPixel = aPresContext->AppUnitsPerDevPixel();

  SnappedImageDrawingParameters params = ComputeSnappedImageDrawingParameters(
      &aContext, appUnitsPerDevPixel, aDest, aFill, aAnchor, aDirty, aImage,
      aSamplingFilter, aImageFlags, aExtendMode);

  if (!params.shouldDraw) {
    return result;
  }

  {
    gfxContextMatrixAutoSaveRestore contextMatrixRestorer(&aContext);

    aContext.SetMatrixDouble(params.imageSpaceToDeviceSpace);

    SVGImageContext newContext = aSVGContext;
    if (!aSVGContext.GetViewportSize()) {
      newContext.SetViewportSize(Some(params.svgViewportSize));
    }

    result = aImage->Draw(&aContext, params.size, params.region,
                          imgIContainer::FRAME_CURRENT, aSamplingFilter,
                          newContext, aImageFlags, aOpacity);
  }

  return result;
}

/* static */
ImgDrawResult nsLayoutUtils::DrawSingleUnscaledImage(
    gfxContext& aContext, nsPresContext* aPresContext, imgIContainer* aImage,
    const SamplingFilter aSamplingFilter, const nsPoint& aDest,
    const nsRect* aDirty, const SVGImageContext& aSVGContext,
    uint32_t aImageFlags, const nsRect* aSourceArea) {
  CSSIntSize imageSize;
  aImage->GetWidth(&imageSize.width);
  aImage->GetHeight(&imageSize.height);
  aImage->GetResolution().ApplyTo(imageSize.width, imageSize.height);

  if (imageSize.width < 1 || imageSize.height < 1) {
    NS_WARNING("Image width or height is non-positive");
    return ImgDrawResult::TEMPORARY_ERROR;
  }

  nsSize size(CSSPixel::ToAppUnits(imageSize));
  nsRect source;
  if (aSourceArea) {
    source = *aSourceArea;
  } else {
    source.SizeTo(size);
  }

  nsRect dest(aDest - source.TopLeft(), size);
  nsRect fill(aDest, source.Size());
  // Ensure that only a single image tile is drawn. If aSourceArea extends
  // outside the image bounds, we want to honor the aSourceArea-to-aDest
  // translation but we don't want to actually tile the image.
  fill.IntersectRect(fill, dest);
  return DrawImageInternal(aContext, aPresContext, aImage, aSamplingFilter,
                           dest, fill, aDest, aDirty ? *aDirty : dest,
                           aSVGContext, aImageFlags);
}

/* static */
ImgDrawResult nsLayoutUtils::DrawSingleImage(
    gfxContext& aContext, nsPresContext* aPresContext, imgIContainer* aImage,
    SamplingFilter aSamplingFilter, const nsRect& aDest, const nsRect& aDirty,
    const SVGImageContext& aSVGContext, uint32_t aImageFlags,
    const nsPoint* aAnchorPoint) {
  // NOTE(emilio): We can hardcode resolution to 1 here, since we're interested
  // in the actual image pixels, for snapping purposes, not on the adjusted
  // size.
  CSSIntSize pixelImageSize(ComputeSizeForDrawingWithFallback(
      aImage, ImageResolution(), aDest.Size()));
  if (pixelImageSize.width < 1 || pixelImageSize.height < 1) {
    NS_ASSERTION(pixelImageSize.width >= 0 && pixelImageSize.height >= 0,
                 "Image width or height is negative");
    return ImgDrawResult::SUCCESS;  // no point in drawing a zero size image
  }

  const nsSize imageSize(CSSPixel::ToAppUnits(pixelImageSize));
  const nsRect source(nsPoint(), imageSize);
  const nsRect dest = GetWholeImageDestination(imageSize, source, aDest);

  // Ensure that only a single image tile is drawn. If aSourceArea extends
  // outside the image bounds, we want to honor the aSourceArea-to-aDest
  // transform but we don't want to actually tile the image.
  nsRect fill;
  fill.IntersectRect(aDest, dest);
  return DrawImageInternal(aContext, aPresContext, aImage, aSamplingFilter,
                           dest, fill,
                           aAnchorPoint ? *aAnchorPoint : fill.TopLeft(),
                           aDirty, aSVGContext, aImageFlags);
}

/* static */
void nsLayoutUtils::ComputeSizeForDrawing(
    imgIContainer* aImage, const ImageResolution& aResolution,
    /* outparam */ CSSIntSize& aImageSize,
    /* outparam */ AspectRatio& aIntrinsicRatio,
    /* outparam */ bool& aGotWidth,
    /* outparam */ bool& aGotHeight) {
  aGotWidth = NS_SUCCEEDED(aImage->GetWidth(&aImageSize.width));
  aGotHeight = NS_SUCCEEDED(aImage->GetHeight(&aImageSize.height));
  aIntrinsicRatio = aImage->GetIntrinsicRatio();

  if (aGotWidth) {
    aResolution.ApplyXTo(aImageSize.width);
  }
  if (aGotHeight) {
    aResolution.ApplyYTo(aImageSize.height);
  }
}

/* static */
CSSIntSize nsLayoutUtils::ComputeSizeForDrawingWithFallback(
    imgIContainer* aImage, const ImageResolution& aResolution,
    const nsSize& aFallbackSize) {
  CSSIntSize imageSize;
  AspectRatio imageRatio;
  bool gotHeight, gotWidth;
  ComputeSizeForDrawing(aImage, aResolution, imageSize, imageRatio, gotWidth,
                        gotHeight);

  // If we didn't get both width and height, try to compute them using the
  // intrinsic ratio of the image.
  if (gotWidth != gotHeight) {
    if (!gotWidth) {
      if (imageRatio) {
        imageSize.width = imageRatio.ApplyTo(imageSize.height);
        gotWidth = true;
      }
    } else {
      if (imageRatio) {
        imageSize.height = imageRatio.Inverted().ApplyTo(imageSize.width);
        gotHeight = true;
      }
    }
  }

  // If we still don't have a width or height, just use the fallback size the
  // caller provided.
  if (!gotWidth) {
    imageSize.width =
        nsPresContext::AppUnitsToIntCSSPixels(aFallbackSize.width);
  }
  if (!gotHeight) {
    imageSize.height =
        nsPresContext::AppUnitsToIntCSSPixels(aFallbackSize.height);
  }

  return imageSize;
}

/* static */ LayerIntRect SnapRectForImage(
    const gfx::Matrix& aTransform, const gfx::MatrixScales& aScaleFactors,
    const LayoutDeviceRect& aRect) {
  // Attempt to snap pixels, the same as ComputeSnappedImageDrawingParameters.
  // Any changes to the algorithm here will need to be reflected there.
  bool snapped = false;
  LayerIntRect snapRect;
  if (!aTransform.HasNonAxisAlignedTransform() && aTransform._11 > 0.0 &&
      aTransform._22 > 0.0) {
    gfxRect rect(gfxPoint(aRect.X(), aRect.Y()),
                 gfxSize(aRect.Width(), aRect.Height()));

    gfxPoint p1 =
        ThebesPoint(aTransform.TransformPoint(ToPoint(rect.TopLeft())));
    gfxPoint p2 =
        ThebesPoint(aTransform.TransformPoint(ToPoint(rect.TopRight())));
    gfxPoint p3 =
        ThebesPoint(aTransform.TransformPoint(ToPoint(rect.BottomRight())));

    if (p2 == gfxPoint(p1.x, p3.y) || p2 == gfxPoint(p3.x, p1.y)) {
      p1.Round();
      p3.Round();

      IntPoint p1i(int32_t(p1.x), int32_t(p1.y));
      IntPoint p3i(int32_t(p3.x), int32_t(p3.y));

      snapRect.MoveTo(std::min(p1i.x, p3i.x), std::min(p1i.y, p3i.y));
      snapRect.SizeTo(std::max(p1i.x, p3i.x) - snapRect.X(),
                      std::max(p1i.y, p3i.y) - snapRect.Y());
      snapped = true;
    }
  }

  if (!snapped) {
    // If we couldn't snap directly with the transform, we need to go best
    // effort in layer pixels.
    snapRect = RoundedToInt(
        aRect * LayoutDeviceToLayerScale2D::FromUnknownScale(aScaleFactors));
  }

  // An empty size is unacceptable so we ensure our suggested size is at least
  // 1 pixel wide/tall.
  if (snapRect.Width() < 1) {
    snapRect.SetWidth(1);
  }
  if (snapRect.Height() < 1) {
    snapRect.SetHeight(1);
  }
  return snapRect;
}

/* static */
IntSize nsLayoutUtils::ComputeImageContainerDrawingParameters(
    imgIContainer* aImage, nsIFrame* aForFrame,
    const LayoutDeviceRect& aDestRect, const LayoutDeviceRect& aFillRect,
    const StackingContextHelper& aSc, uint32_t aFlags,
    SVGImageContext& aSVGContext, Maybe<ImageIntRegion>& aRegion) {
  MOZ_ASSERT(aImage);
  MOZ_ASSERT(aForFrame);

  MatrixScales scaleFactors = aSc.GetInheritedScale();
  SamplingFilter samplingFilter =
      nsLayoutUtils::GetSamplingFilterForFrame(aForFrame);

  // Compute our SVG context parameters, if any. Don't replace the viewport
  // size if it was already set, prefer what the caller gave.
  SVGImageContext::MaybeStoreContextPaint(aSVGContext, aForFrame, aImage);
  if ((scaleFactors.xScale != 1.0 || scaleFactors.yScale != 1.0) &&
      aImage->GetType() == imgIContainer::TYPE_VECTOR &&
      (!aSVGContext.GetViewportSize())) {
    gfxSize gfxDestSize(aDestRect.Width(), aDestRect.Height());
    IntSize viewportSize = aImage->OptimalImageSizeForDest(
        gfxDestSize, imgIContainer::FRAME_CURRENT, samplingFilter, aFlags);

    CSSIntSize cssViewportSize(viewportSize.width, viewportSize.height);
    aSVGContext.SetViewportSize(Some(cssViewportSize));
  }

  const gfx::Matrix& itm = aSc.GetInheritedTransform();
  LayerIntRect destRect = SnapRectForImage(itm, scaleFactors, aDestRect);

  // Since we always decode entire raster images, we only care about the
  // ImageIntRegion for vector images when we are recording blobs, for which we
  // may only draw part of in some cases.
  if ((aImage->GetType() != imgIContainer::TYPE_VECTOR) ||
      !(aFlags & imgIContainer::FLAG_RECORD_BLOB)) {
    // If the transform scale of our stacking context helper is being animated
    // on the compositor then the transform will have the current value of the
    // scale, but the scale factors will have max value of the scale animation.
    // So we want to ask for a decoded image that can fulfill that larger size.
    int32_t scaleWidth = int32_t(ceil(aDestRect.Width() * scaleFactors.xScale));
    if (scaleWidth > destRect.width + 2) {
      destRect.width = scaleWidth;
    }
    int32_t scaleHeight =
        int32_t(ceil(aDestRect.Height() * scaleFactors.yScale));
    if (scaleHeight > destRect.height + 2) {
      destRect.height = scaleHeight;
    }

    return aImage->OptimalImageSizeForDest(
        gfxSize(destRect.Width(), destRect.Height()),
        imgIContainer::FRAME_CURRENT, samplingFilter, aFlags);
  }

  // We only use the region rect with blob recordings. This is because when we
  // rasterize an SVG image in process, we always create a complete
  // rasterization of the whole image which can be given to any caller, while
  // we support partial rasterization with the blob recordings.
  if (aFlags & imgIContainer::FLAG_RECORD_BLOB) {
    // If the dest rect contains the fill rect, then we are only displaying part
    // of the vector image. We need to calculate the restriction region to avoid
    // drawing more than we need, and sampling outside the desired bounds.
    LayerIntRect clipRect = SnapRectForImage(itm, scaleFactors, aFillRect);
    if (destRect.Contains(clipRect)) {
      LayerIntRect restrictRect = destRect.Intersect(clipRect);
      restrictRect.MoveBy(-destRect.TopLeft());

      if (restrictRect.Width() < 1) {
        restrictRect.SetWidth(1);
      }
      if (restrictRect.Height() < 1) {
        restrictRect.SetHeight(1);
      }

      if (restrictRect.X() != 0 || restrictRect.Y() != 0 ||
          restrictRect.Size() != destRect.Size()) {
        IntRect sampleRect = restrictRect.ToUnknownRect();
        aRegion = Some(ImageIntRegion::CreateWithSamplingRestriction(
            sampleRect, sampleRect, ExtendMode::CLAMP));
      }
    }
  }

  // VectorImage::OptimalImageSizeForDest will just round up, but we already
  // have an integer size.
  return destRect.Size().ToUnknownSize();
}

/* static */
nsPoint nsLayoutUtils::GetBackgroundFirstTilePos(const nsPoint& aDest,
                                                 const nsPoint& aFill,
                                                 const nsSize& aRepeatSize) {
  return nsPoint(NSToIntFloor(float(aFill.x - aDest.x) / aRepeatSize.width) *
                     aRepeatSize.width,
                 NSToIntFloor(float(aFill.y - aDest.y) / aRepeatSize.height) *
                     aRepeatSize.height) +
         aDest;
}

/* static */
ImgDrawResult nsLayoutUtils::DrawBackgroundImage(
    gfxContext& aContext, nsIFrame* aForFrame, nsPresContext* aPresContext,
    imgIContainer* aImage, SamplingFilter aSamplingFilter, const nsRect& aDest,
    const nsRect& aFill, const nsSize& aRepeatSize, const nsPoint& aAnchor,
    const nsRect& aDirty, uint32_t aImageFlags, ExtendMode aExtendMode,
    float aOpacity) {
  AUTO_PROFILER_LABEL("nsLayoutUtils::DrawBackgroundImage",
                      GRAPHICS_Rasterization);

  CSSIntSize destCSSSize{nsPresContext::AppUnitsToIntCSSPixels(aDest.width),
                         nsPresContext::AppUnitsToIntCSSPixels(aDest.height)};

  SVGImageContext svgContext(Some(destCSSSize));
  SVGImageContext::MaybeStoreContextPaint(svgContext, aForFrame, aImage);

  /* Fast path when there is no need for image spacing */
  if (aRepeatSize.width == aDest.width && aRepeatSize.height == aDest.height) {
    return DrawImageInternal(aContext, aPresContext, aImage, aSamplingFilter,
                             aDest, aFill, aAnchor, aDirty, svgContext,
                             aImageFlags, aExtendMode, aOpacity);
  }

  const nsPoint firstTilePos =
      GetBackgroundFirstTilePos(aDest.TopLeft(), aFill.TopLeft(), aRepeatSize);
  const nscoord xMost = aFill.XMost();
  const nscoord repeatWidth = aRepeatSize.width;
  const nscoord yMost = aFill.YMost();
  const nscoord repeatHeight = aRepeatSize.height;
  nsRect dest(0, 0, aDest.width, aDest.height);
  nsPoint anchor = aAnchor;
  for (nscoord x = firstTilePos.x; x < xMost; x += repeatWidth) {
    for (nscoord y = firstTilePos.y; y < yMost; y += repeatHeight) {
      dest.x = x;
      dest.y = y;
      ImgDrawResult result = DrawImageInternal(
          aContext, aPresContext, aImage, aSamplingFilter, dest, dest, anchor,
          aDirty, svgContext, aImageFlags, ExtendMode::CLAMP, aOpacity);
      anchor.y += repeatHeight;
      if (result != ImgDrawResult::SUCCESS) {
        return result;
      }
    }
    anchor.x += repeatWidth;
    anchor.y = aAnchor.y;
  }

  return ImgDrawResult::SUCCESS;
}

/* static */
ImgDrawResult nsLayoutUtils::DrawImage(
    gfxContext& aContext, ComputedStyle* aComputedStyle,
    nsPresContext* aPresContext, imgIContainer* aImage,
    const SamplingFilter aSamplingFilter, const nsRect& aDest,
    const nsRect& aFill, const nsPoint& aAnchor, const nsRect& aDirty,
    uint32_t aImageFlags, float aOpacity) {
  SVGImageContext svgContext;
  SVGImageContext::MaybeStoreContextPaint(svgContext, *aPresContext,
                                          *aComputedStyle, aImage);

  return DrawImageInternal(aContext, aPresContext, aImage, aSamplingFilter,
                           aDest, aFill, aAnchor, aDirty, svgContext,
                           aImageFlags, ExtendMode::CLAMP, aOpacity);
}

/* static */
nsRect nsLayoutUtils::GetWholeImageDestination(const nsSize& aWholeImageSize,
                                               const nsRect& aImageSourceArea,
                                               const nsRect& aDestArea) {
  double scaleX = double(aDestArea.width) / aImageSourceArea.width;
  double scaleY = double(aDestArea.height) / aImageSourceArea.height;
  nscoord destOffsetX = NSToCoordRound(aImageSourceArea.x * scaleX);
  nscoord destOffsetY = NSToCoordRound(aImageSourceArea.y * scaleY);
  nscoord wholeSizeX = NSToCoordRound(aWholeImageSize.width * scaleX);
  nscoord wholeSizeY = NSToCoordRound(aWholeImageSize.height * scaleY);
  return nsRect(aDestArea.TopLeft() - nsPoint(destOffsetX, destOffsetY),
                nsSize(wholeSizeX, wholeSizeY));
}

/* static */
already_AddRefed<imgIContainer> nsLayoutUtils::OrientImage(
    imgIContainer* aContainer, const StyleImageOrientation& aOrientation) {
  MOZ_ASSERT(aContainer, "Should have an image container");
  nsCOMPtr<imgIContainer> img(aContainer);

  switch (aOrientation) {
    case StyleImageOrientation::FromImage:
      break;
    case StyleImageOrientation::None:
      img = ImageOps::Unorient(img);
      break;
  }

  return img.forget();
}

/* static */
bool nsLayoutUtils::ImageRequestUsesCORS(imgIRequest* aRequest) {
  int32_t corsMode = mozilla::CORS_NONE;
  return NS_SUCCEEDED(aRequest->GetCORSMode(&corsMode)) &&
         corsMode != mozilla::CORS_NONE;
}

static bool NonZeroCorner(const LengthPercentage& aLength) {
  // Since negative results are clamped to 0, check > 0.
  return aLength.Resolve(nscoord_MAX) > 0 || aLength.Resolve(0) > 0;
}

/* static */
bool nsLayoutUtils::HasNonZeroCorner(const BorderRadius& aCorners) {
  for (const auto corner : mozilla::AllPhysicalHalfCorners()) {
    if (NonZeroCorner(aCorners.Get(corner))) {
      return true;
    }
  }
  return false;
}

// aCorner is a "full corner" value, i.e. eCornerTopLeft etc.
static bool IsCornerAdjacentToSide(uint8_t aCorner, Side aSide) {
  static_assert((int)eSideTop == eCornerTopLeft, "Check for Full Corner");
  static_assert((int)eSideRight == eCornerTopRight, "Check for Full Corner");
  static_assert((int)eSideBottom == eCornerBottomRight,
                "Check for Full Corner");
  static_assert((int)eSideLeft == eCornerBottomLeft, "Check for Full Corner");
  static_assert((int)eSideTop == ((eCornerTopRight - 1) & 3),
                "Check for Full Corner");
  static_assert((int)eSideRight == ((eCornerBottomRight - 1) & 3),
                "Check for Full Corner");
  static_assert((int)eSideBottom == ((eCornerBottomLeft - 1) & 3),
                "Check for Full Corner");
  static_assert((int)eSideLeft == ((eCornerTopLeft - 1) & 3),
                "Check for Full Corner");

  return aSide == aCorner || aSide == ((aCorner - 1) & 3);
}

/* static */
bool nsLayoutUtils::HasNonZeroCornerOnSide(const BorderRadius& aCorners,
                                           Side aSide) {
  static_assert(eCornerTopLeftX / 2 == eCornerTopLeft,
                "Check for Non Zero on side");
  static_assert(eCornerTopLeftY / 2 == eCornerTopLeft,
                "Check for Non Zero on side");
  static_assert(eCornerTopRightX / 2 == eCornerTopRight,
                "Check for Non Zero on side");
  static_assert(eCornerTopRightY / 2 == eCornerTopRight,
                "Check for Non Zero on side");
  static_assert(eCornerBottomRightX / 2 == eCornerBottomRight,
                "Check for Non Zero on side");
  static_assert(eCornerBottomRightY / 2 == eCornerBottomRight,
                "Check for Non Zero on side");
  static_assert(eCornerBottomLeftX / 2 == eCornerBottomLeft,
                "Check for Non Zero on side");
  static_assert(eCornerBottomLeftY / 2 == eCornerBottomLeft,
                "Check for Non Zero on side");

  for (const auto corner : mozilla::AllPhysicalHalfCorners()) {
    // corner is a "half corner" value, so dividing by two gives us a
    // "full corner" value.
    if (NonZeroCorner(aCorners.Get(corner)) &&
        IsCornerAdjacentToSide(corner / 2, aSide)) {
      return true;
    }
  }
  return false;
}

/* static */
widget::TransparencyMode nsLayoutUtils::GetFrameTransparency(
    nsIFrame* aBackgroundFrame, nsIFrame* aCSSRootFrame) {
  if (!aCSSRootFrame->StyleEffects()->IsOpaque()) {
    return TransparencyMode::Transparent;
  }

  if (HasNonZeroCorner(aCSSRootFrame->StyleBorder()->mBorderRadius)) {
    return TransparencyMode::Transparent;
  }

  nsITheme::Transparency transparency;
  if (aCSSRootFrame->IsThemed(&transparency)) {
    return transparency == nsITheme::eTransparent
               ? TransparencyMode::Transparent
               : TransparencyMode::Opaque;
  }

  // We need an uninitialized window to be treated as opaque because doing
  // otherwise breaks window display effects on some platforms, specifically
  // Vista. (bug 450322)
  if (aBackgroundFrame->IsViewportFrame() &&
      !aBackgroundFrame->PrincipalChildList().FirstChild()) {
    return TransparencyMode::Opaque;
  }

  const ComputedStyle* bgSC = nsCSSRendering::FindBackground(aBackgroundFrame);
  if (!bgSC) {
    return TransparencyMode::Transparent;
  }
  const nsStyleBackground* bg = bgSC->StyleBackground();
  if (NS_GET_A(bg->BackgroundColor(bgSC)) < 255 ||
      // bottom layer's clip is used for the color
      bg->BottomLayer().mClip != StyleGeometryBox::BorderBox) {
    return TransparencyMode::Transparent;
  }
  return TransparencyMode::Opaque;
}

/* static */
bool nsLayoutUtils::IsPopup(const nsIFrame* aFrame) {
  // Optimization: the frame can't possibly be a popup if it has no view.
  if (!aFrame->HasView()) {
    NS_ASSERTION(!aFrame->IsMenuPopupFrame(), "popup frame must have a view");
    return false;
  }
  return aFrame->IsMenuPopupFrame();
}

/* static */
nsIFrame* nsLayoutUtils::GetDisplayRootFrame(nsIFrame* aFrame) {
  return const_cast<nsIFrame*>(
      nsLayoutUtils::GetDisplayRootFrame(const_cast<const nsIFrame*>(aFrame)));
}

/* static */
const nsIFrame* nsLayoutUtils::GetDisplayRootFrame(const nsIFrame* aFrame) {
  // We could use GetRootPresContext() here if the
  // NS_FRAME_IN_POPUP frame bit is set.
  const nsIFrame* f = aFrame;
  for (;;) {
    if (!f->HasAnyStateBits(NS_FRAME_IN_POPUP)) {
      f = f->PresShell()->GetRootFrame();
      if (!f) {
        return aFrame;
      }
    } else if (IsPopup(f)) {
      return f;
    }
    nsIFrame* parent = GetCrossDocParentFrameInProcess(f);
    if (!parent) {
      return f;
    }
    f = parent;
  }
}

/* static */
nsIFrame* nsLayoutUtils::GetReferenceFrame(nsIFrame* aFrame) {
  nsIFrame* f = aFrame;
  for (;;) {
    if (f->IsTransformed() || IsPopup(f)) {
      return f;
    }
    nsIFrame* parent = GetCrossDocParentFrameInProcess(f);
    if (!parent) {
      return f;
    }
    f = parent;
  }
}

/* static */ gfx::ShapedTextFlags nsLayoutUtils::GetTextRunFlagsForStyle(
    const ComputedStyle* aComputedStyle, nsPresContext* aPresContext,
    const nsStyleFont* aStyleFont, const nsStyleText* aStyleText,
    nscoord aLetterSpacing) {
  gfx::ShapedTextFlags result = gfx::ShapedTextFlags();
  if (aLetterSpacing != 0 ||
      aStyleText->mTextJustify == StyleTextJustify::InterCharacter) {
    result |= gfx::ShapedTextFlags::TEXT_DISABLE_OPTIONAL_LIGATURES;
  }
  if (aStyleText->mMozControlCharacterVisibility ==
      StyleMozControlCharacterVisibility::Hidden) {
    result |= gfx::ShapedTextFlags::TEXT_HIDE_CONTROL_CHARACTERS;
  }
  switch (aComputedStyle->StyleText()->mTextRendering) {
    case StyleTextRendering::Optimizespeed:
      result |= gfx::ShapedTextFlags::TEXT_OPTIMIZE_SPEED;
      break;
    case StyleTextRendering::Auto:
      if (aPresContext &&
          aStyleFont->mFont.size.ToCSSPixels() <
              aPresContext->DevPixelsToFloatCSSPixels(
                  StaticPrefs::browser_display_auto_quality_min_font_size())) {
        result |= gfx::ShapedTextFlags::TEXT_OPTIMIZE_SPEED;
      }
      break;
    default:
      break;
  }
  return result | GetTextRunOrientFlagsForStyle(aComputedStyle);
}

/* static */ gfx::ShapedTextFlags nsLayoutUtils::GetTextRunOrientFlagsForStyle(
    const ComputedStyle* aComputedStyle) {
  auto writingMode = aComputedStyle->StyleVisibility()->mWritingMode;
  switch (writingMode) {
    case StyleWritingModeProperty::HorizontalTb:
      return gfx::ShapedTextFlags::TEXT_ORIENT_HORIZONTAL;

    case StyleWritingModeProperty::VerticalLr:
    case StyleWritingModeProperty::VerticalRl:
      switch (aComputedStyle->StyleVisibility()->mTextOrientation) {
        case StyleTextOrientation::Mixed:
          return gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_MIXED;
        case StyleTextOrientation::Upright:
          return gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT;
        case StyleTextOrientation::Sideways:
          return gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_RIGHT;
        default:
          MOZ_ASSERT_UNREACHABLE("unknown text-orientation");
          return gfx::ShapedTextFlags();
      }

    case StyleWritingModeProperty::SidewaysLr:
      return gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_LEFT;

    case StyleWritingModeProperty::SidewaysRl:
      return gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_RIGHT;

    default:
      MOZ_ASSERT_UNREACHABLE("unknown writing-mode");
      return gfx::ShapedTextFlags();
  }
}

/* static */
void nsLayoutUtils::GetRectDifferenceStrips(const nsRect& aR1,
                                            const nsRect& aR2, nsRect* aHStrip,
                                            nsRect* aVStrip) {
  NS_ASSERTION(aR1.TopLeft() == aR2.TopLeft(),
               "expected rects at the same position");
  nsRect unionRect(aR1.x, aR1.y, std::max(aR1.width, aR2.width),
                   std::max(aR1.height, aR2.height));
  nscoord VStripStart = std::min(aR1.width, aR2.width);
  nscoord HStripStart = std::min(aR1.height, aR2.height);
  *aVStrip = unionRect;
  aVStrip->x += VStripStart;
  aVStrip->width -= VStripStart;
  *aHStrip = unionRect;
  aHStrip->y += HStripStart;
  aHStrip->height -= HStripStart;
}

nsDeviceContext* nsLayoutUtils::GetDeviceContextForScreenInfo(
    nsPIDOMWindowOuter* aWindow) {
  if (!aWindow) {
    return nullptr;
  }

  nsCOMPtr<nsIDocShell> docShell = aWindow->GetDocShell();
  while (docShell) {
    // Now make sure our size is up to date.  That will mean that the device
    // context does the right thing on multi-monitor systems when we return it
    // to the caller.  It will also make sure that our prescontext has been
    // created, if we're supposed to have one.
    nsCOMPtr<nsPIDOMWindowOuter> win = docShell->GetWindow();
    if (!win) {
      // No reason to go on
      return nullptr;
    }

    win->EnsureSizeAndPositionUpToDate();

    RefPtr<nsPresContext> presContext = docShell->GetPresContext();
    if (presContext) {
      nsDeviceContext* context = presContext->DeviceContext();
      if (context) {
        return context;
      }
    }

    nsCOMPtr<nsIDocShellTreeItem> parentItem;
    docShell->GetInProcessParent(getter_AddRefs(parentItem));
    docShell = do_QueryInterface(parentItem);
  }

  return nullptr;
}

/* static */
bool nsLayoutUtils::IsReallyFixedPos(const nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame->StyleDisplay()->mPosition == StylePositionProperty::Fixed,
             "IsReallyFixedPos called on non-'position:fixed' frame");
  return MayBeReallyFixedPos(aFrame);
}

/* static */
bool nsLayoutUtils::MayBeReallyFixedPos(const nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame->GetParent(),
             "MayBeReallyFixedPos called on frame not in tree");
  LayoutFrameType parentType = aFrame->GetParent()->Type();
  return parentType == LayoutFrameType::Viewport ||
         parentType == LayoutFrameType::PageContent;
}

/* static */
bool nsLayoutUtils::IsInPositionFixedSubtree(const nsIFrame* aFrame) {
  for (const nsIFrame* f = aFrame; f; f = f->GetParent()) {
    if (f->StyleDisplay()->mPosition == StylePositionProperty::Fixed &&
        nsLayoutUtils::IsReallyFixedPos(f)) {
      return true;
    }
  }
  return false;
}

static RefPtr<SourceSurface> ScaleSourceSurface(SourceSurface& aSurface,
                                                const IntSize& aTargetSize) {
  const IntSize surfaceSize = aSurface.GetSize();

  MOZ_ASSERT(surfaceSize != aTargetSize);
  MOZ_ASSERT(!surfaceSize.IsEmpty());
  MOZ_ASSERT(!aTargetSize.IsEmpty());

  RefPtr<DrawTarget> dt = Factory::CreateDrawTarget(
      gfxVars::ContentBackend(), aTargetSize, aSurface.GetFormat());

  if (!dt || !dt->IsValid()) {
    return nullptr;
  }

  dt->DrawSurface(&aSurface, Rect(Point(), Size(aTargetSize)),
                  Rect(Point(), Size(surfaceSize)));
  return dt->GetBackingSurface();
}

SurfaceFromElementResult nsLayoutUtils::SurfaceFromOffscreenCanvas(
    OffscreenCanvas* aOffscreenCanvas, uint32_t aSurfaceFlags,
    RefPtr<DrawTarget>& aTarget) {
  SurfaceFromElementResult result;

  IntSize size = aOffscreenCanvas->GetWidthHeight().ToUnknownSize();
  if (size.IsEmpty()) {
    return result;
  }

  result.mSourceSurface =
      aOffscreenCanvas->GetSurfaceSnapshot(&result.mAlphaType);
  if (!result.mSourceSurface) {
    // If the element doesn't have a context then we won't get a snapshot. The
    // canvas spec wants us to not error and just draw nothing, so return an
    // empty surface.
    result.mSize = size;
    result.mAlphaType = gfxAlphaType::Opaque;
    RefPtr<DrawTarget> ref =
        aTarget ? aTarget : gfxPlatform::ThreadLocalScreenReferenceDrawTarget();
    if (ref->CanCreateSimilarDrawTarget(size, SurfaceFormat::B8G8R8A8)) {
      RefPtr<DrawTarget> dt =
          ref->CreateSimilarDrawTarget(size, SurfaceFormat::B8G8R8A8);
      if (dt) {
        result.mSourceSurface = dt->Snapshot();
      }
    }
  } else {
    result.mSize = result.mSourceSurface->GetSize();

    // If we want an exact sized surface, then we need to scale if we don't
    // match the intrinsic size.
    const bool exactSize = aSurfaceFlags & SFE_EXACT_SIZE_SURFACE;
    if (exactSize && size != result.mSize) {
      result.mSize = size;
      result.mSourceSurface = ScaleSourceSurface(*result.mSourceSurface, size);
    }

    if (aTarget && result.mSourceSurface) {
      RefPtr<SourceSurface> opt =
          aTarget->OptimizeSourceSurface(result.mSourceSurface);
      if (opt) {
        result.mSourceSurface = opt;
      }
    }
  }

  result.mHasSize = true;
  result.mIntrinsicSize = size;
  result.mIsWriteOnly = aOffscreenCanvas->IsWriteOnly();

  nsIGlobalObject* global = aOffscreenCanvas->GetParentObject();
  if (global) {
    result.mPrincipal = global->PrincipalOrNull();
  }

  return result;
}

SurfaceFromElementResult nsLayoutUtils::SurfaceFromVideoFrame(
    VideoFrame* aVideoFrame, uint32_t aSurfaceFlags,
    RefPtr<DrawTarget>& aTarget) {
  SurfaceFromElementResult result;

  RefPtr<layers::Image> layersImage = aVideoFrame->GetImage();
  if (!layersImage) {
    return result;
  }

  IntSize codedSize = aVideoFrame->NativeCodedSize();
  IntRect visibleRect = aVideoFrame->NativeVisibleRect();
  IntSize displaySize = aVideoFrame->NativeDisplaySize();

  MOZ_ASSERT(layersImage->GetSize() == codedSize);
  IntRect codedRect(IntPoint(0, 0), codedSize);

  if (visibleRect.IsEqualEdges(codedRect) && displaySize == codedSize) {
    // The display and coded rects are identical, which means we can just use
    // the image as is.
    result.mLayersImage = std::move(layersImage);
    result.mSize = codedSize;
    result.mIntrinsicSize = codedSize;
  } else if (aSurfaceFlags & SFE_ALLOW_UNCROPPED_UNSCALED) {
    // The caller supports cropping/scaling.
    result.mLayersImage = std::move(layersImage);
    result.mCropRect = Some(visibleRect);
    result.mSize = codedSize;
    result.mIntrinsicSize = displaySize;
  } else {
    // The caller does not support cropping/scaling. We need to on its behalf.
    RefPtr<SourceSurface> surface = layersImage->GetAsSourceSurface();
    if (!surface) {
      return result;
    }

    RefPtr<DrawTarget> ref = aTarget
                                 ? aTarget
                                 : gfxPlatform::GetPlatform()
                                       ->ThreadLocalScreenReferenceDrawTarget();
    if (!ref->CanCreateSimilarDrawTarget(displaySize,
                                         SurfaceFormat::B8G8R8A8)) {
      return result;
    }

    RefPtr<DrawTarget> dt =
        ref->CreateSimilarDrawTarget(displaySize, SurfaceFormat::B8G8R8A8);
    if (!dt) {
      return result;
    }

    gfx::Rect dstRect(0, 0, displaySize.Width(), displaySize.Height());
    gfx::Rect srcRect(visibleRect.X(), visibleRect.Y(), visibleRect.Width(),
                      visibleRect.Height());
    dt->DrawSurface(surface, dstRect, srcRect);
    result.mSourceSurface = dt->Snapshot();
    if (NS_WARN_IF(!result.mSourceSurface)) {
      return result;
    }

    result.mSize = displaySize;
    result.mIntrinsicSize = displaySize;
  }

  result.mAlphaType = gfxAlphaType::Premult;
  Nullable<VideoPixelFormat> format = aVideoFrame->GetFormat();
  if (!format.IsNull()) {
    switch (format.Value()) {
      case VideoPixelFormat::I420:
      case VideoPixelFormat::I422:
      case VideoPixelFormat::I444:
      case VideoPixelFormat::NV12:
      case VideoPixelFormat::RGBX:
      case VideoPixelFormat::BGRX:
        result.mAlphaType = gfxAlphaType::Opaque;
        break;
      default:
        break;
    }
  }

  result.mHasSize = true;

  // We shouldn't have a VideoFrame if either of these is true.
  result.mHadCrossOriginRedirects = false;
  result.mIsWriteOnly = false;

  nsIGlobalObject* global = aVideoFrame->GetParentObject();
  if (global) {
    result.mPrincipal = global->PrincipalOrNull();
  }

  if (aTarget) {
    // They gave us a DrawTarget to optimize for, so even though we may have a
    // layers::Image, we should unconditionally try to grab a SourceSurface and
    // try to optimize it.
    if (result.mLayersImage) {
      MOZ_ASSERT(!result.mSourceSurface);
      result.mSourceSurface = result.mLayersImage->GetAsSourceSurface();
    }

    if (result.mSourceSurface) {
      RefPtr<SourceSurface> opt =
          aTarget->OptimizeSourceSurface(result.mSourceSurface);
      if (opt) {
        result.mSourceSurface = std::move(opt);
      }
    }
  }

  return result;
}

SurfaceFromElementResult nsLayoutUtils::SurfaceFromImageBitmap(
    mozilla::dom::ImageBitmap* aImageBitmap, uint32_t aSurfaceFlags) {
  return aImageBitmap->SurfaceFrom(aSurfaceFlags);
}

SurfaceFromElementResult nsLayoutUtils::SurfaceFromElement(
    nsIImageLoadingContent* aElement, const Maybe<int32_t>& aResizeWidth,
    const Maybe<int32_t>& aResizeHeight, uint32_t aSurfaceFlags,
    RefPtr<DrawTarget>& aTarget) {
  SurfaceFromElementResult result;
  nsresult rv;

  nsCOMPtr<imgIRequest> imgRequest;
  rv = aElement->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                            getter_AddRefs(imgRequest));
  if (NS_FAILED(rv)) {
    return result;
  }

  if (!imgRequest) {
    // There's no image request. This is either because a request for
    // a non-empty URI failed, or the URI is the empty string.
    nsCOMPtr<nsIURI> currentURI;
    aElement->GetCurrentURI(getter_AddRefs(currentURI));
    if (!currentURI) {
      // Treat the empty URI as available instead of broken state.
      result.mHasSize = true;
    }
    return result;
  }

  uint32_t status;
  imgRequest->GetImageStatus(&status);
  result.mHasSize = status & imgIRequest::STATUS_SIZE_AVAILABLE;
  if ((status & imgIRequest::STATUS_LOAD_COMPLETE) == 0) {
    // Spec says to use GetComplete, but that only works on
    // HTMLImageElement, and we support all sorts of other stuff
    // here.  Do this for now pending spec clarification.
    result.mIsStillLoading = (status & imgIRequest::STATUS_ERROR) == 0;
    return result;
  }

  nsCOMPtr<nsIPrincipal> principal;
  rv = imgRequest->GetImagePrincipal(getter_AddRefs(principal));
  if (NS_FAILED(rv)) {
    return result;
  }

  nsCOMPtr<imgIContainer> imgContainer;
  rv = imgRequest->GetImage(getter_AddRefs(imgContainer));
  if (NS_FAILED(rv)) {
    return result;
  }

  nsCOMPtr<nsIContent> content = do_QueryInterface(aElement);

  // Ensure that the image is oriented the same way as it's displayed
  // if the image request is of the same origin.
  auto orientation =
      content->GetPrimaryFrame() &&
              !(aSurfaceFlags & SFE_ORIENTATION_FROM_IMAGE)
          ? content->GetPrimaryFrame()->StyleVisibility()->UsedImageOrientation(
                imgRequest)
          : nsStyleVisibility::UsedImageOrientation(
                imgRequest, StyleImageOrientation::FromImage);
  imgContainer = OrientImage(imgContainer, orientation);

  const bool noRasterize = aSurfaceFlags & SFE_NO_RASTERIZING_VECTORS;

  uint32_t whichFrame = aSurfaceFlags & SFE_WANT_FIRST_FRAME_IF_IMAGE
                            ? (uint32_t)imgIContainer::FRAME_FIRST
                            : (uint32_t)imgIContainer::FRAME_CURRENT;
  const bool exactSize = aSurfaceFlags & SFE_EXACT_SIZE_SURFACE;

  uint32_t frameFlags =
      imgIContainer::FLAG_SYNC_DECODE | imgIContainer::FLAG_ASYNC_NOTIFY;
  if (aSurfaceFlags & SFE_NO_COLORSPACE_CONVERSION) {
    frameFlags |= imgIContainer::FLAG_DECODE_NO_COLORSPACE_CONVERSION;
  }
  if (aSurfaceFlags & SFE_ALLOW_NON_PREMULT) {
    frameFlags |= imgIContainer::FLAG_DECODE_NO_PREMULTIPLY_ALPHA;
  }

  int32_t imgWidth, imgHeight;
  HTMLImageElement* element = HTMLImageElement::FromNodeOrNull(content);
  if (aSurfaceFlags & SFE_USE_ELEMENT_SIZE_IF_VECTOR && element &&
      imgContainer->GetType() == imgIContainer::TYPE_VECTOR) {
    // We're holding a strong ref to "element" via "content".
    imgWidth = MOZ_KnownLive(element)->Width();
    imgHeight = MOZ_KnownLive(element)->Height();
  } else {
    auto res = imgContainer->GetResolution();
    rv = imgContainer->GetWidth(&imgWidth);
    if (NS_SUCCEEDED(rv)) {
      res.ApplyXTo(imgWidth);
    } else if (aResizeWidth.isSome()) {
      imgWidth = *aResizeWidth;
    } else {
      // As stated in css-sizing-3 Intrinsic Sizes, the fallback size of
      // 300 x 150 for the width and height as needed.
      //
      // See https://drafts.csswg.org/css-sizing-3/#intrinsic-sizes
      imgWidth = kFallbackIntrinsicWidthInPixels;
    }
    rv = imgContainer->GetHeight(&imgHeight);
    if (NS_SUCCEEDED(rv)) {
      res.ApplyYTo(imgHeight);
    } else if (aResizeHeight.isSome()) {
      imgHeight = *aResizeHeight;
    } else {
      // As stated in css-sizing-3 Intrinsic Sizes, the fallback size of
      // 300 x 150 for the width and height as needed.
      //
      // See https://drafts.csswg.org/css-sizing-3/#intrinsic-sizes
      imgHeight = kFallbackIntrinsicHeightInPixels;
    }
  }
  result.mSize = result.mIntrinsicSize = IntSize(imgWidth, imgHeight);

  if (!noRasterize || imgContainer->GetType() == imgIContainer::TYPE_RASTER) {
    result.mSourceSurface =
        imgContainer->GetFrameAtSize(result.mSize, whichFrame, frameFlags);
    if (!result.mSourceSurface) {
      return result;
    }
    IntSize surfSize = result.mSourceSurface->GetSize();
    if (exactSize && surfSize != result.mSize) {
      result.mSourceSurface =
          ScaleSourceSurface(*result.mSourceSurface, result.mSize);
      if (!result.mSourceSurface) {
        return result;
      }
    } else {
      result.mSize = surfSize;
    }
    // The surface we return is likely to be cached. We don't want to have to
    // convert to a surface that's compatible with aTarget each time it's used
    // (that would result in terrible performance), so we convert once here
    // upfront if aTarget is specified.
    if (aTarget) {
      RefPtr<SourceSurface> optSurface =
          aTarget->OptimizeSourceSurface(result.mSourceSurface);
      if (optSurface) {
        result.mSourceSurface = optSurface;
      }
    }

    const auto& format = result.mSourceSurface->GetFormat();
    if (IsOpaque(format)) {
      result.mAlphaType = gfxAlphaType::Opaque;
    } else if (frameFlags & imgIContainer::FLAG_DECODE_NO_PREMULTIPLY_ALPHA) {
      result.mAlphaType = gfxAlphaType::NonPremult;
    } else {
      result.mAlphaType = gfxAlphaType::Premult;
    }
  } else {
    result.mDrawInfo.mImgContainer = imgContainer;
    result.mDrawInfo.mWhichFrame = whichFrame;
    result.mDrawInfo.mDrawingFlags = frameFlags;
  }

  result.mCORSUsed = nsLayoutUtils::ImageRequestUsesCORS(imgRequest);

  bool hadCrossOriginRedirects = true;
  imgRequest->GetHadCrossOriginRedirects(&hadCrossOriginRedirects);

  result.mPrincipal = std::move(principal);
  result.mHadCrossOriginRedirects = hadCrossOriginRedirects;
  result.mImageRequest = std::move(imgRequest);
  result.mIsWriteOnly = CanvasUtils::CheckWriteOnlySecurity(
      result.mCORSUsed, result.mPrincipal, result.mHadCrossOriginRedirects);

  return result;
}

SurfaceFromElementResult nsLayoutUtils::SurfaceFromElement(
    HTMLImageElement* aElement, uint32_t aSurfaceFlags,
    RefPtr<DrawTarget>& aTarget) {
  return SurfaceFromElement(static_cast<nsIImageLoadingContent*>(aElement),
                            Nothing(), Nothing(), aSurfaceFlags, aTarget);
}

SurfaceFromElementResult nsLayoutUtils::SurfaceFromElement(
    HTMLCanvasElement* aElement, uint32_t aSurfaceFlags,
    RefPtr<DrawTarget>& aTarget) {
  SurfaceFromElementResult result;

  IntSize size = aElement->GetSize().ToUnknownSize();
  if (size.IsEmpty()) {
    return result;
  }

  auto pAlphaType = &result.mAlphaType;
  if (!(aSurfaceFlags & SFE_ALLOW_NON_PREMULT)) {
    pAlphaType =
        nullptr;  // Coersce GetSurfaceSnapshot to give us Opaque/Premult only.
  }
  result.mSourceSurface = aElement->GetSurfaceSnapshot(pAlphaType, aTarget);
  if (!result.mSourceSurface) {
    // If the element doesn't have a context then we won't get a snapshot. The
    // canvas spec wants us to not error and just draw nothing, so return an
    // empty surface.
    result.mSize = size;
    result.mAlphaType = gfxAlphaType::Opaque;
    RefPtr<DrawTarget> ref =
        aTarget ? aTarget
                : gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget();
    if (ref->CanCreateSimilarDrawTarget(size, SurfaceFormat::B8G8R8A8)) {
      RefPtr<DrawTarget> dt =
          ref->CreateSimilarDrawTarget(size, SurfaceFormat::B8G8R8A8);
      if (dt) {
        result.mSourceSurface = dt->Snapshot();
      }
    }
  } else {
    result.mSize = result.mSourceSurface->GetSize();

    // If we want an exact sized surface, then we need to scale if we don't
    // match the intrinsic size.
    const bool exactSize = aSurfaceFlags & SFE_EXACT_SIZE_SURFACE;
    if (exactSize && size != result.mSize) {
      result.mSize = size;
      result.mSourceSurface = ScaleSourceSurface(*result.mSourceSurface, size);
    }

    if (aTarget && result.mSourceSurface) {
      RefPtr<SourceSurface> opt =
          aTarget->OptimizeSourceSurface(result.mSourceSurface);
      if (opt) {
        result.mSourceSurface = opt;
      }
    }
  }

  // Ensure that any future changes to the canvas trigger proper invalidation,
  // in case this is being used by -moz-element()
  aElement->MarkContextClean();

  result.mHasSize = true;
  result.mIntrinsicSize = size;
  result.mPrincipal = aElement->NodePrincipal();
  result.mHadCrossOriginRedirects = false;
  result.mIsWriteOnly = aElement->IsWriteOnly();

  return result;
}

SurfaceFromElementResult nsLayoutUtils::SurfaceFromElement(
    HTMLVideoElement* aElement, uint32_t aSurfaceFlags,
    RefPtr<DrawTarget>& aTarget, bool aOptimizeSourceSurface) {
  SurfaceFromElementResult result;
  result.mAlphaType = gfxAlphaType::Opaque;  // Assume opaque.

  if (aElement->ContainsRestrictedContent()) {
    return result;
  }

  uint16_t readyState = aElement->ReadyState();
  if (readyState == HAVE_NOTHING || readyState == HAVE_METADATA) {
    result.mIsStillLoading = true;
    return result;
  }

  // If it doesn't have a principal, just bail
  nsCOMPtr<nsIPrincipal> principal = aElement->GetCurrentVideoPrincipal();
  if (!principal) {
    return result;
  }

  result.mLayersImage = aElement->GetCurrentImage();
  if (!result.mLayersImage) {
    return result;
  }

  result.mCORSUsed = aElement->GetCORSMode() != CORS_NONE;
  result.mHasSize = true;
  result.mSize = result.mLayersImage->GetSize();
  result.mIntrinsicSize =
      gfx::IntSize(aElement->VideoWidth(), aElement->VideoHeight());
  result.mPrincipal = std::move(principal);
  result.mHadCrossOriginRedirects = aElement->HadCrossOriginRedirects();
  result.mIsWriteOnly = CanvasUtils::CheckWriteOnlySecurity(
      result.mCORSUsed, result.mPrincipal, result.mHadCrossOriginRedirects);

  if (aTarget && aOptimizeSourceSurface) {
    // They gave us a DrawTarget to optimize for, so even though we have a
    // layers::Image, we should unconditionally try to grab a SourceSurface and
    // try to optimize it.
    if ((result.mSourceSurface = result.mLayersImage->GetAsSourceSurface())) {
      RefPtr<SourceSurface> opt =
          aTarget->OptimizeSourceSurface(result.mSourceSurface);
      if (opt) {
        result.mSourceSurface = opt;
      }
    }
  }

  return result;
}

SurfaceFromElementResult nsLayoutUtils::SurfaceFromElement(
    dom::Element* aElement, const Maybe<int32_t>& aResizeWidth,
    const Maybe<int32_t>& aResizeHeight, uint32_t aSurfaceFlags,
    RefPtr<DrawTarget>& aTarget) {
  // If it's a <canvas>, we may be able to just grab its internal surface
  if (HTMLCanvasElement* canvas = HTMLCanvasElement::FromNodeOrNull(aElement)) {
    return SurfaceFromElement(canvas, aSurfaceFlags, aTarget);
  }

  // Maybe it's <video>?
  if (HTMLVideoElement* video = HTMLVideoElement::FromNodeOrNull(aElement)) {
    return SurfaceFromElement(video, aSurfaceFlags, aTarget);
  }

  // Finally, check if it's a normal image
  nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(aElement);

  if (!imageLoader) {
    return SurfaceFromElementResult();
  }

  return SurfaceFromElement(imageLoader, aResizeWidth, aResizeHeight,
                            aSurfaceFlags, aTarget);
}

/* static */
Element* nsLayoutUtils::GetEditableRootContentByContentEditable(
    Document* aDocument) {
  // If the document is in designMode we should return nullptr.
  if (!aDocument || aDocument->IsInDesignMode()) {
    return nullptr;
  }

  // contenteditable only works with HTML document.
  // XXXbz should this test IsHTMLOrXHTML(), or just IsHTML()?
  if (!aDocument->IsHTMLOrXHTML()) {
    return nullptr;
  }

  Element* rootElement = aDocument->GetRootElement();
  if (rootElement && rootElement->IsEditable()) {
    return rootElement;
  }

  // If there is no editable root element, check its <body> element.
  // Note that the body element could be <frameset> element.
  Element* bodyElement = aDocument->GetBody();
  if (bodyElement && bodyElement->IsEditable()) {
    return bodyElement;
  }
  return nullptr;
}

#ifdef DEBUG
/* static */
void nsLayoutUtils::AssertNoDuplicateContinuations(
    nsIFrame* aContainer, const nsFrameList& aFrameList) {
  for (nsIFrame* f : aFrameList) {
    // Check only later continuations of f; we deal with checking the
    // earlier continuations when we hit those earlier continuations in
    // the frame list.
    for (nsIFrame* c = f; (c = c->GetNextInFlow());) {
      NS_ASSERTION(c->GetParent() != aContainer || !aFrameList.ContainsFrame(c),
                   "Two continuations of the same frame in the same "
                   "frame list");
    }
  }
}

// Is one of aFrame's ancestors a letter frame?
static bool IsInLetterFrame(nsIFrame* aFrame) {
  for (nsIFrame* f = aFrame->GetParent(); f; f = f->GetParent()) {
    if (f->IsLetterFrame()) {
      return true;
    }
  }
  return false;
}

/* static */
void nsLayoutUtils::AssertTreeOnlyEmptyNextInFlows(nsIFrame* aSubtreeRoot) {
  NS_ASSERTION(aSubtreeRoot->GetPrevInFlow(),
               "frame tree not empty, but caller reported complete status");

  // Also assert that text frames map no text.
  auto [start, end] = aSubtreeRoot->GetOffsets();
  // In some cases involving :first-letter, we'll partially unlink a
  // continuation in the middle of a continuation chain from its
  // previous and next continuations before destroying it, presumably so
  // that we don't also destroy the later continuations.  Once we've
  // done this, GetOffsets returns incorrect values.
  // For examples, see list of tests in
  // https://bugzilla.mozilla.org/show_bug.cgi?id=619021#c29
  NS_ASSERTION(start == end || IsInLetterFrame(aSubtreeRoot),
               "frame tree not empty, but caller reported complete status");

  for (const auto& childList : aSubtreeRoot->ChildLists()) {
    for (nsIFrame* child : childList.mList) {
      nsLayoutUtils::AssertTreeOnlyEmptyNextInFlows(child);
    }
  }
}
#endif

static void GetFontFacesForFramesInner(
    nsIFrame* aFrame, nsLayoutUtils::UsedFontFaceList& aResult,
    nsLayoutUtils::UsedFontFaceTable& aFontFaces, uint32_t aMaxRanges,
    bool aSkipCollapsedWhitespace) {
  MOZ_ASSERT(aFrame, "NULL frame pointer");

  if (aFrame->IsTextFrame()) {
    if (!aFrame->GetPrevContinuation()) {
      nsLayoutUtils::GetFontFacesForText(aFrame, 0, INT32_MAX, true, aResult,
                                         aFontFaces, aMaxRanges,
                                         aSkipCollapsedWhitespace);
    }
    return;
  }

  for (nsIFrame* child : aFrame->PrincipalChildList()) {
    child = nsPlaceholderFrame::GetRealFrameFor(child);
    GetFontFacesForFramesInner(child, aResult, aFontFaces, aMaxRanges,
                               aSkipCollapsedWhitespace);
  }
}

/* static */
nsresult nsLayoutUtils::GetFontFacesForFrames(nsIFrame* aFrame,
                                              UsedFontFaceList& aResult,
                                              UsedFontFaceTable& aFontFaces,
                                              uint32_t aMaxRanges,
                                              bool aSkipCollapsedWhitespace) {
  MOZ_ASSERT(aFrame, "NULL frame pointer");

  while (aFrame) {
    GetFontFacesForFramesInner(aFrame, aResult, aFontFaces, aMaxRanges,
                               aSkipCollapsedWhitespace);
    aFrame = GetNextContinuationOrIBSplitSibling(aFrame);
  }

  return NS_OK;
}

static void AddFontsFromTextRun(gfxTextRun* aTextRun, nsTextFrame* aFrame,
                                gfxSkipCharsIterator& aSkipIter,
                                const gfxTextRun::Range& aRange,
                                nsLayoutUtils::UsedFontFaceList& aResult,
                                nsLayoutUtils::UsedFontFaceTable& aFontFaces,
                                uint32_t aMaxRanges) {
  nsIContent* content = aFrame->GetContent();
  int32_t contentLimit =
      aFrame->GetContentOffset() + aFrame->GetInFlowContentLength();
  for (gfxTextRun::GlyphRunIterator glyphRuns(aTextRun, aRange);
       !glyphRuns.AtEnd(); glyphRuns.NextRun()) {
    gfxFontEntry* fe = glyphRuns.GlyphRun()->mFont->GetFontEntry();
    // if we have already listed this face, just make sure the match type is
    // recorded
    InspectorFontFace* fontFace = aFontFaces.Get(fe);
    if (fontFace) {
      fontFace->AddMatchType(glyphRuns.GlyphRun()->mMatchType);
    } else {
      // A new font entry we haven't seen before
      fontFace = new InspectorFontFace(fe, aTextRun->GetFontGroup(),
                                       glyphRuns.GlyphRun()->mMatchType);
      aFontFaces.InsertOrUpdate(fe, fontFace);
      aResult.AppendElement(fontFace);
    }

    // Add this glyph run to the fontFace's list of ranges, unless we have
    // already collected as many as wanted.
    if (fontFace->RangeCount() < aMaxRanges) {
      int32_t start =
          aSkipIter.ConvertSkippedToOriginal(glyphRuns.StringStart());
      int32_t end = aSkipIter.ConvertSkippedToOriginal(glyphRuns.StringEnd());

      // Mapping back from textrun offsets ("skipped" offsets that reflect the
      // text after whitespace collapsing, etc) to DOM content offsets in the
      // original text is ambiguous, because many original characters can
      // map to a single skipped offset. aSkipIter.ConvertSkippedToOriginal()
      // will return an "original" offset that corresponds to the *end* of
      // a collapsed run of characters in this case; but that might extend
      // beyond the current content node if the textrun mapped multiple nodes.
      // So we clamp the end offset to keep it valid for the content node
      // that corresponds to the current textframe.
      end = std::min(end, contentLimit);

      if (end > start) {
        RefPtr<nsRange> range =
            nsRange::Create(content, start, content, end, IgnoreErrors());
        NS_WARNING_ASSERTION(range,
                             "nsRange::Create() failed to create valid range");
        if (range) {
          fontFace->AddRange(range);
        }
      }
    }
  }
}

/* static */
void nsLayoutUtils::GetFontFacesForText(nsIFrame* aFrame, int32_t aStartOffset,
                                        int32_t aEndOffset,
                                        bool aFollowContinuations,
                                        UsedFontFaceList& aResult,
                                        UsedFontFaceTable& aFontFaces,
                                        uint32_t aMaxRanges,
                                        bool aSkipCollapsedWhitespace) {
  MOZ_ASSERT(aFrame, "NULL frame pointer");

  if (!aFrame->IsTextFrame()) {
    return;
  }

  if (!aFrame->StyleVisibility()->IsVisible()) {
    return;
  }

  nsTextFrame* curr = static_cast<nsTextFrame*>(aFrame);
  do {
    int32_t fstart = std::max(curr->GetContentOffset(), aStartOffset);
    int32_t fend = std::min(curr->GetContentEnd(), aEndOffset);
    if (fstart >= fend) {
      curr = static_cast<nsTextFrame*>(curr->GetNextContinuation());
      continue;
    }

    // curr is overlapping with the offset we want
    gfxSkipCharsIterator iter = curr->EnsureTextRun(nsTextFrame::eInflated);
    gfxTextRun* textRun = curr->GetTextRun(nsTextFrame::eInflated);
    if (!textRun) {
      NS_WARNING("failed to get textRun, low memory?");
      return;
    }

    // include continuations in the range that share the same textrun
    nsTextFrame* next = nullptr;
    if (aFollowContinuations && fend < aEndOffset) {
      next = static_cast<nsTextFrame*>(curr->GetNextContinuation());
      while (next && next->GetTextRun(nsTextFrame::eInflated) == textRun) {
        fend = std::min(next->GetContentEnd(), aEndOffset);
        next = fend < aEndOffset
                   ? static_cast<nsTextFrame*>(next->GetNextContinuation())
                   : nullptr;
      }
    }

    if (!aSkipCollapsedWhitespace || (curr->HasAnyNoncollapsedCharacters() &&
                                      curr->HasNonSuppressedText())) {
      gfxTextRun::Range range(iter.ConvertOriginalToSkipped(fstart),
                              iter.ConvertOriginalToSkipped(fend));
      AddFontsFromTextRun(textRun, curr, iter, range, aResult, aFontFaces,
                          aMaxRanges);
    }

    curr = next;
  } while (aFollowContinuations && curr);
}

/* static */
size_t nsLayoutUtils::SizeOfTextRunsForFrames(nsIFrame* aFrame,
                                              MallocSizeOf aMallocSizeOf,
                                              bool clear) {
  MOZ_ASSERT(aFrame, "NULL frame pointer");

  size_t total = 0;

  if (aFrame->IsTextFrame()) {
    nsTextFrame* textFrame = static_cast<nsTextFrame*>(aFrame);
    for (uint32_t i = 0; i < 2; ++i) {
      gfxTextRun* run = textFrame->GetTextRun(
          (i != 0) ? nsTextFrame::eInflated : nsTextFrame::eNotInflated);
      if (run) {
        if (clear) {
          run->ResetSizeOfAccountingFlags();
        } else {
          total += run->MaybeSizeOfIncludingThis(aMallocSizeOf);
        }
      }
    }
    return total;
  }

  for (const auto& childList : aFrame->ChildLists()) {
    for (nsIFrame* f : childList.mList) {
      total += SizeOfTextRunsForFrames(f, aMallocSizeOf, clear);
    }
  }
  return total;
}

/* static */
void nsLayoutUtils::RecomputeSmoothScrollDefault() {
  // We want prefers-reduced-motion to determine the default
  // value of the general.smoothScroll pref. If the user
  // changed the pref we want to respect the change.
  Preferences::SetBool(
      StaticPrefs::GetPrefName_general_smoothScroll(),
      !LookAndFeel::GetInt(LookAndFeel::IntID::PrefersReducedMotion, 0),
      PrefValueKind::Default);
}

/* static */
bool nsLayoutUtils::IsSmoothScrollingEnabled() {
  if (nsContentUtils::ShouldResistFingerprinting(
          "We use the global RFP pref to maintain consistent scroll behavior "
          "in the browser.",
          RFPTarget::CSSPrefersReducedMotion)) {
    return true;
  }
  return StaticPrefs::general_smoothScroll();
}

/* static */
void nsLayoutUtils::Initialize() {
  nsComputedDOMStyle::RegisterPrefChangeCallbacks();
}

/* static */
void nsLayoutUtils::Shutdown() {
  if (sContentMap) {
    sContentMap = nullptr;
  }

  nsComputedDOMStyle::UnregisterPrefChangeCallbacks();
}

/* static */
void nsLayoutUtils::RegisterImageRequest(nsPresContext* aPresContext,
                                         imgIRequest* aRequest,
                                         bool* aRequestRegistered) {
  if (!aPresContext) {
    return;
  }

  if (aRequestRegistered && *aRequestRegistered) {
    // Our request is already registered with the refresh driver, so
    // no need to register it again.
    return;
  }

  if (aRequest) {
    aPresContext->RefreshDriver()->AddImageRequest(aRequest);
    if (aRequestRegistered) {
      *aRequestRegistered = true;
    }
  }
}

/* static */
void nsLayoutUtils::RegisterImageRequestIfAnimated(nsPresContext* aPresContext,
                                                   imgIRequest* aRequest,
                                                   bool* aRequestRegistered) {
  if (!aPresContext) {
    return;
  }

  if (aRequestRegistered && *aRequestRegistered) {
    // Our request is already registered with the refresh driver, so
    // no need to register it again.
    return;
  }

  if (aRequest) {
    nsCOMPtr<imgIContainer> image;
    if (NS_SUCCEEDED(aRequest->GetImage(getter_AddRefs(image)))) {
      // Check to verify that the image is animated. If so, then add it to the
      // list of images tracked by the refresh driver.
      bool isAnimated = false;
      nsresult rv = image->GetAnimated(&isAnimated);
      if (NS_SUCCEEDED(rv) && isAnimated) {
        aPresContext->RefreshDriver()->AddImageRequest(aRequest);
        if (aRequestRegistered) {
          *aRequestRegistered = true;
        }
      }
    }
  }
}

/* static */
void nsLayoutUtils::DeregisterImageRequest(nsPresContext* aPresContext,
                                           imgIRequest* aRequest,
                                           bool* aRequestRegistered) {
  if (!aPresContext) {
    return;
  }

  // Deregister our imgIRequest with the refresh driver to
  // complete tear-down, but only if it has been registered
  if (aRequestRegistered && !*aRequestRegistered) {
    return;
  }

  if (aRequest) {
    nsCOMPtr<imgIContainer> image;
    if (NS_SUCCEEDED(aRequest->GetImage(getter_AddRefs(image)))) {
      aPresContext->RefreshDriver()->RemoveImageRequest(aRequest);

      if (aRequestRegistered) {
        *aRequestRegistered = false;
      }
    }
  }
}

/* static */
void nsLayoutUtils::PostRestyleEvent(Element* aElement,
                                     RestyleHint aRestyleHint,
                                     nsChangeHint aMinChangeHint) {
  if (Document* doc = aElement->GetComposedDoc()) {
    if (nsPresContext* presContext = doc->GetPresContext()) {
      presContext->RestyleManager()->PostRestyleEvent(aElement, aRestyleHint,
                                                      aMinChangeHint);
    }
  }
}

nsSetAttrRunnable::nsSetAttrRunnable(Element* aElement, nsAtom* aAttrName,
                                     const nsAString& aValue)
    : mozilla::Runnable("nsSetAttrRunnable"),
      mElement(aElement),
      mAttrName(aAttrName),
      mValue(aValue) {
  NS_ASSERTION(aElement && aAttrName, "Missing stuff, prepare to crash");
}

nsSetAttrRunnable::nsSetAttrRunnable(Element* aElement, nsAtom* aAttrName,
                                     int32_t aValue)
    : mozilla::Runnable("nsSetAttrRunnable"),
      mElement(aElement),
      mAttrName(aAttrName) {
  NS_ASSERTION(aElement && aAttrName, "Missing stuff, prepare to crash");
  mValue.AppendInt(aValue);
}

NS_IMETHODIMP
nsSetAttrRunnable::Run() {
  return mElement->SetAttr(kNameSpaceID_None, mAttrName, mValue, true);
}

nsUnsetAttrRunnable::nsUnsetAttrRunnable(Element* aElement, nsAtom* aAttrName)
    : mozilla::Runnable("nsUnsetAttrRunnable"),
      mElement(aElement),
      mAttrName(aAttrName) {
  NS_ASSERTION(aElement && aAttrName, "Missing stuff, prepare to crash");
}

NS_IMETHODIMP
nsUnsetAttrRunnable::Run() {
  return mElement->UnsetAttr(kNameSpaceID_None, mAttrName, true);
}

/**
 * Compute the minimum font size inside of a container with the given
 * width, such that **when the user zooms the container to fill the full
 * width of the device**, the fonts satisfy our minima.
 */
static nscoord MinimumFontSizeFor(nsPresContext* aPresContext,
                                  WritingMode aWritingMode,
                                  nscoord aContainerISize) {
  PresShell* presShell = aPresContext->PresShell();

  uint32_t emPerLine = presShell->FontSizeInflationEmPerLine();
  uint32_t minTwips = presShell->FontSizeInflationMinTwips();
  if (emPerLine == 0 && minTwips == 0) {
    return 0;
  }

  nscoord byLine = 0, byInch = 0;
  if (emPerLine != 0) {
    byLine = aContainerISize / emPerLine;
  }
  if (minTwips != 0) {
    // REVIEW: Is this giving us app units and sizes *not* counting
    // viewport scaling?
    gfxSize screenSize = aPresContext->ScreenSizeInchesForFontInflation();
    float deviceISizeInches =
        aWritingMode.IsVertical() ? screenSize.height : screenSize.width;
    byInch =
        NSToCoordRound(aContainerISize / (deviceISizeInches * 1440 / minTwips));
  }
  return std::max(byLine, byInch);
}

/* static */
float nsLayoutUtils::FontSizeInflationInner(const nsIFrame* aFrame,
                                            nscoord aMinFontSize) {
  // Note that line heights should be inflated by the same ratio as the
  // font size of the same text; thus we operate only on the font size
  // even when we're scaling a line height.
  nscoord styleFontSize = aFrame->StyleFont()->mFont.size.ToAppUnits();
  if (styleFontSize <= 0) {
    // Never scale zero font size.
    return 1.0;
  }

  if (aMinFontSize <= 0) {
    // No need to scale.
    return 1.0;
  }

  // If between this current frame and its font inflation container there is a
  // non-inline element with fixed width or height, then we should not inflate
  // fonts for this frame.
  for (const nsIFrame* f = aFrame; f && !f->IsContainerForFontSizeInflation();
       f = f->GetParent()) {
    nsIContent* content = f->GetContent();
    LayoutFrameType fType = f->Type();
    nsIFrame* parent = f->GetParent();
    // Also, if there is more than one frame corresponding to a single
    // content node, we want the outermost one.
    if (!(parent && parent->GetContent() == content) &&
        // ignore width/height on inlines since they don't apply
        fType != LayoutFrameType::Inline &&
        // ignore width on radios and checkboxes since we enlarge them and
        // they have width/height in ua.css
        fType != LayoutFrameType::CheckboxRadio) {
      // ruby annotations should have the same inflation as its
      // grandparent, which is the ruby frame contains the annotation.
      if (fType == LayoutFrameType::RubyText) {
        MOZ_ASSERT(parent && parent->IsRubyTextContainerFrame());
        nsIFrame* grandparent = parent->GetParent();
        MOZ_ASSERT(grandparent && grandparent->IsRubyFrame());
        return FontSizeInflationFor(grandparent);
      }
      WritingMode wm = f->GetWritingMode();
      const auto positionProperty = f->StyleDisplay()->mPosition;
      const auto stylePosISize =
          f->StylePosition()->ISize(wm, positionProperty);
      const auto stylePosBSize =
          f->StylePosition()->BSize(wm, positionProperty);
      if (!stylePosISize->IsAuto() ||
          !stylePosBSize->BehavesLikeInitialValueOnBlockAxis()) {
        return 1.0;
      }
    }
  }

  int32_t interceptParam = StaticPrefs::font_size_inflation_mappingIntercept();
  float maxRatio = (float)StaticPrefs::font_size_inflation_maxRatio() / 100.0f;

  float ratio = float(styleFontSize) / float(aMinFontSize);
  float inflationRatio;

  // Given a minimum inflated font size m, a specified font size s, we want to
  // find the inflated font size i and then return the ratio of i to s (i/s).
  if (interceptParam >= 0) {
    // Since the mapping intercept parameter P is greater than zero, we use it
    // to determine the point where our mapping function intersects the i=s
    // line. This means that we have an equation of the form:
    //
    // i = m + s*(P/2)/(1 + P/2), if s <= (1 + P/2)*m
    // i = s, if s >= (1 + P/2)*m

    float intercept = 1 + float(interceptParam) / 2.0f;
    if (ratio >= intercept) {
      // If we're already at 1+P/2 or more times the minimum, don't scale.
      return 1.0;
    }

    // The point (intercept, intercept) is where the part of the i vs. s graph
    // that's not slope 1 meets the i=s line.  (This part of the
    // graph is a line from (0, m), to that point). We calculate the
    // intersection point to be ((1+P/2)m, (1+P/2)m), where P is the
    // intercept parameter above. We then need to return i/s.
    inflationRatio = (1.0f + (ratio * (intercept - 1) / intercept)) / ratio;
  } else {
    // This is the case where P is negative. We essentially want to implement
    // the case for P=infinity here, so we make i = s + m, which means that
    // i/s = s/s + m/s = 1 + 1/ratio
    inflationRatio = 1 + 1.0f / ratio;
  }

  if (maxRatio > 1.0 && inflationRatio > maxRatio) {
    return maxRatio;
  } else {
    return inflationRatio;
  }
}

static bool ShouldInflateFontsForContainer(const nsIFrame* aFrame) {
  // We only want to inflate fonts for text that is in a place
  // with room to expand.  The question is what the best heuristic for
  // that is...
  // For now, we're going to use NS_FRAME_IN_CONSTRAINED_BSIZE, which
  // indicates whether the frame is inside something with a constrained
  // block-size (propagating down the tree), but the propagation stops when
  // we hit overflow-y [or -x, for vertical mode]: scroll or auto.
  const nsStyleText* styleText = aFrame->StyleText();

  return styleText->mTextSizeAdjust != StyleTextSizeAdjust::None &&
         !aFrame->HasAnyStateBits(NS_FRAME_IN_CONSTRAINED_BSIZE) &&
         // We also want to disable font inflation for containers that have
         // preformatted text.
         // MathML cells need special treatment. See bug 1002526 comment 56.
         (styleText->WhiteSpaceCanWrap(aFrame) || aFrame->IsMathMLFrame());
}

nscoord nsLayoutUtils::InflationMinFontSizeFor(const nsIFrame* aFrame) {
  nsPresContext* presContext = aFrame->PresContext();
  if (!FontSizeInflationEnabled(presContext) ||
      presContext->mInflationDisabledForShrinkWrap) {
    return 0;
  }

  for (const nsIFrame* f = aFrame; f; f = f->GetParent()) {
    if (f->IsContainerForFontSizeInflation()) {
      if (!ShouldInflateFontsForContainer(f)) {
        return 0;
      }

      nsFontInflationData* data =
          nsFontInflationData::FindFontInflationDataFor(aFrame);
      // FIXME: The need to null-check here is sort of a bug, and might
      // lead to incorrect results.
      if (!data || !data->InflationEnabled()) {
        return 0;
      }

      return MinimumFontSizeFor(aFrame->PresContext(), aFrame->GetWritingMode(),
                                data->UsableISize());
    }
  }

  MOZ_ASSERT(false, "root should always be container");

  return 0;
}

float nsLayoutUtils::FontSizeInflationFor(const nsIFrame* aFrame) {
  if (aFrame->IsInSVGTextSubtree()) {
    const nsIFrame* container = aFrame;
    while (!container->IsSVGTextFrame()) {
      container = container->GetParent();
    }
    NS_ASSERTION(container, "expected to find an ancestor SVGTextFrame");
    return static_cast<const SVGTextFrame*>(container)
        ->GetFontSizeScaleFactor();
  }

  if (!FontSizeInflationEnabled(aFrame->PresContext())) {
    return 1.0f;
  }

  return FontSizeInflationInner(aFrame, InflationMinFontSizeFor(aFrame));
}

/* static */
bool nsLayoutUtils::FontSizeInflationEnabled(nsPresContext* aPresContext) {
  PresShell* presShell = aPresContext->GetPresShell();
  if (!presShell) {
    return false;
  }
  return presShell->FontSizeInflationEnabled();
}

/* static */
nsRect nsLayoutUtils::GetBoxShadowRectForFrame(nsIFrame* aFrame,
                                               const nsSize& aFrameSize) {
  auto boxShadows = aFrame->StyleEffects()->mBoxShadow.AsSpan();
  if (boxShadows.IsEmpty()) {
    return nsRect();
  }

  nsRect inputRect(nsPoint(0, 0), aFrameSize);

  // According to the CSS spec, box-shadow should be based on the border box.
  // However, that looks broken when the background extends outside the border
  // box, as can be the case with native theming.  To fix that we expand the
  // area that we shadow to include the bounds of any native theme drawing.
  const nsStyleDisplay* styleDisplay = aFrame->StyleDisplay();
  nsITheme::Transparency transparency;
  if (aFrame->IsThemed(styleDisplay, &transparency)) {
    // For opaque (rectangular) theme widgets we can take the generic
    // border-box path with border-radius disabled.
    if (transparency != nsITheme::eOpaque) {
      nsPresContext* presContext = aFrame->PresContext();
      presContext->Theme()->GetWidgetOverflow(
          presContext->DeviceContext(), aFrame,
          styleDisplay->EffectiveAppearance(), &inputRect);
    }
  }

  nsRect shadows;
  int32_t A2D = aFrame->PresContext()->AppUnitsPerDevPixel();
  for (auto& shadow : boxShadows) {
    nsRect tmpRect = inputRect;

    // inset shadows are never painted outside the frame
    if (shadow.inset) {
      continue;
    }

    tmpRect.MoveBy(nsPoint(shadow.base.horizontal.ToAppUnits(),
                           shadow.base.vertical.ToAppUnits()));
    tmpRect.Inflate(shadow.spread.ToAppUnits());
    tmpRect.Inflate(nsContextBoxBlur::GetBlurRadiusMargin(
        shadow.base.blur.ToAppUnits(), A2D));
    shadows.UnionRect(shadows, tmpRect);
  }
  return shadows;
}

/* static */
bool nsLayoutUtils::GetDocumentViewerSize(
    const nsPresContext* aPresContext, LayoutDeviceIntSize& aOutSize,
    SubtractDynamicToolbar aSubtractDynamicToolbar) {
  nsCOMPtr<nsIDocShell> docShell = aPresContext->GetDocShell();
  if (!docShell) {
    return false;
  }

  nsCOMPtr<nsIDocumentViewer> viewer;
  docShell->GetDocViewer(getter_AddRefs(viewer));
  if (!viewer) {
    return false;
  }

  LayoutDeviceIntRect bounds;
  viewer->GetBounds(bounds);

  if (aPresContext->IsRootContentDocumentCrossProcess() &&
      aSubtractDynamicToolbar == SubtractDynamicToolbar::Yes &&
      aPresContext->HasDynamicToolbar() && !bounds.IsEmpty()) {
    MOZ_ASSERT(aPresContext->IsRootContentDocumentCrossProcess());
    bounds.height -= aPresContext->GetDynamicToolbarMaxHeight();
    // Collapse the size in the case the dynamic toolbar max height is greater
    // than the content bound height so that hopefully embedders of GeckoView
    // may notice they set wrong dynamic toolbar max height.
    if (bounds.height < 0) {
      bounds.height = 0;
    }
  }

  aOutSize = bounds.Size();
  return true;
}

bool nsLayoutUtils::UpdateCompositionBoundsForRCDRSF(
    ParentLayerRect& aCompBounds, const nsPresContext* aPresContext,
    IncludeDynamicToolbar aIncludeDynamicToolbar) {
  SubtractDynamicToolbar shouldSubtractDynamicToolbar =
      aIncludeDynamicToolbar == IncludeDynamicToolbar::Force
          ? SubtractDynamicToolbar::No
      : aPresContext->IsRootContentDocumentCrossProcess() &&
              aPresContext->HasDynamicToolbar()
          ? SubtractDynamicToolbar::Yes
          : SubtractDynamicToolbar::No;

  const bool isKeyboardVisibleOnOverlaysContent =
      aPresContext->GetKeyboardHeight() &&
      aPresContext->Document()->InteractiveWidget() ==
          InteractiveWidget::OverlaysContent;
  if (shouldSubtractDynamicToolbar == SubtractDynamicToolbar::Yes &&
      // In `overlays-content` mode with the software keyboard visible, avoid
      // flipping `shouldSubtractDynamicToolbar` below. We want to exclude
      // the dynamic toolbar height from the visual viewport (composition
      // bounds) height in this case to be consistent with the handling of the
      // layout viewport height in ExpandHeightForDynamicToolbar(). Otherwise,
      // the visual viewport will be taller than the layout viewport which can
      // lead to rendering problems.
      !isKeyboardVisibleOnOverlaysContent) {
    if (RefPtr<MobileViewportManager> MVM =
            aPresContext->PresShell()->GetMobileViewportManager()) {
      // Convert the intrinsic composition size to app units here since
      // the returned size of below CalculateScrollableRectForFrame call has
      // been already converted/rounded to app units.
      nsSize intrinsicCompositionSize =
          CSSSize::ToAppUnits(MVM->GetIntrinsicCompositionSize());

      if (ScrollContainerFrame* rootScrollContainerFrame =
              aPresContext->PresShell()->GetRootScrollContainerFrame()) {
        // Expand the composition size to include the area initially covered by
        // the dynamic toolbar only if the content is taller than the intrinsic
        // composition size (i.e. the dynamic toolbar should be able to move
        // only if the content is vertically scrollable).
        if (intrinsicCompositionSize.height <
            CalculateScrollableRectForFrame(rootScrollContainerFrame, nullptr)
                .Height()) {
          shouldSubtractDynamicToolbar = SubtractDynamicToolbar::No;
        }
      }
    }
  }

  LayoutDeviceIntSize contentSize;
  if (!GetDocumentViewerSize(aPresContext, contentSize,
                             shouldSubtractDynamicToolbar)) {
    return false;
  }

  // Add the keyboard height in the case of
  // `interactive-widget=overlays-content` so that contents being overlaid by
  // the keyboard can NOT be reachable by scrolling.
  if (isKeyboardVisibleOnOverlaysContent) {
    contentSize.height += ViewAs<LayoutDevicePixel>(
        aPresContext->GetKeyboardHeight(),
        PixelCastJustification::LayoutDeviceIsScreenForBounds);
  }
  aCompBounds.SizeTo(ViewAs<ParentLayerPixel>(
      LayoutDeviceSize(contentSize),
      PixelCastJustification::LayoutDeviceIsParentLayerForRCDRSF));
  return true;
}

/* static */
nsMargin nsLayoutUtils::ScrollbarAreaToExcludeFromCompositionBoundsFor(
    const nsIFrame* aScrollFrame) {
  if (!aScrollFrame || !aScrollFrame->GetScrollTargetFrame()) {
    return nsMargin();
  }
  nsPresContext* presContext = aScrollFrame->PresContext();
  PresShell* presShell = presContext->GetPresShell();
  if (!presShell) {
    return nsMargin();
  }
  bool isRootScrollContainerFrame =
      aScrollFrame == presShell->GetRootScrollContainerFrame();
  bool isRootContentDocRootScrollFrame =
      isRootScrollContainerFrame &&
      presContext->IsRootContentDocumentCrossProcess();
  if (!isRootContentDocRootScrollFrame) {
    return nsMargin();
  }
  if (presContext->UseOverlayScrollbars()) {
    return nsMargin();
  }
  ScrollContainerFrame* scrollContainerFrame =
      aScrollFrame->GetScrollTargetFrame();
  if (!scrollContainerFrame) {
    return nsMargin();
  }
  return scrollContainerFrame->GetActualScrollbarSizes(
      ScrollContainerFrame::ScrollbarSizesOptions::
          INCLUDE_VISUAL_VIEWPORT_SCROLLBARS);
}

/* static */
nsSize nsLayoutUtils::CalculateCompositionSizeForFrame(
    nsIFrame* aFrame, bool aSubtractScrollbars,
    const nsSize* aOverrideScrollPortSize,
    IncludeDynamicToolbar aIncludeDynamicToolbar) {
  // If we have a scroll container frame, restrict the composition bounds to its
  // scroll port. The scroll port excludes the frame borders and the scroll
  // bars, which we don't want to be part of the composition bounds.
  ScrollContainerFrame* scrollContainerFrame = aFrame->GetScrollTargetFrame();
  nsRect rect = scrollContainerFrame ? scrollContainerFrame->GetScrollPortRect()
                                     : aFrame->GetRect();
  nsSize size =
      aOverrideScrollPortSize ? *aOverrideScrollPortSize : rect.Size();

  nsPresContext* presContext = aFrame->PresContext();
  PresShell* presShell = presContext->PresShell();

  bool isRootContentDocRootScrollFrame =
      presContext->IsRootContentDocumentCrossProcess() &&
      aFrame == presShell->GetRootScrollContainerFrame();
  if (isRootContentDocRootScrollFrame) {
    ParentLayerRect compBounds;
    if (UpdateCompositionBoundsForRCDRSF(compBounds, presContext,
                                         aIncludeDynamicToolbar)) {
      int32_t auPerDevPixel = presContext->AppUnitsPerDevPixel();
      size = nsSize(compBounds.width * auPerDevPixel,
                    compBounds.height * auPerDevPixel);
    }
  }

  if (aSubtractScrollbars) {
    nsMargin margins = ScrollbarAreaToExcludeFromCompositionBoundsFor(aFrame);
    size.width -= margins.LeftRight();
    size.height -= margins.TopBottom();
  }

  return size;
}

/* static */
CSSSize nsLayoutUtils::CalculateBoundingCompositionSize(
    const nsIFrame* aFrame, bool aIsRootContentDocRootScrollFrame,
    const FrameMetrics& aMetrics) {
  if (aIsRootContentDocRootScrollFrame) {
    return ViewAs<LayerPixel>(
               aMetrics.GetCompositionBounds().Size(),
               PixelCastJustification::ParentLayerToLayerForRootComposition) *
           LayerToScreenScale(1.0f) / aMetrics.DisplayportPixelsPerCSSPixel();
  }
  nsPresContext* presContext = aFrame->PresContext();
  ScreenSize rootCompositionSize;
  nsPresContext* rootPresContext =
      presContext->GetInProcessRootContentDocumentPresContext();
  if (!rootPresContext) {
    rootPresContext = presContext->GetRootPresContext();
  }

  const bool isPopupRoot = aFrame->HasAnyStateBits(NS_FRAME_IN_POPUP);
  PresShell* rootPresShell = nullptr;
  if (rootPresContext && !isPopupRoot) {
    rootPresShell = rootPresContext->PresShell();
    if (nsIFrame* rootFrame = rootPresShell->GetRootFrame()) {
      ParentLayerRect compBounds;
      if (UpdateCompositionBoundsForRCDRSF(compBounds, rootPresContext)) {
        rootCompositionSize = ViewAs<ScreenPixel>(
            compBounds.Size(),
            PixelCastJustification::ScreenIsParentLayerForRoot);
      } else {
        // LayoutDeviceToScreenScale2D =
        //   LayoutDeviceToParentLayerScale *
        //   ParentLayerToScreenScale2D
        LayoutDeviceToScreenScale2D cumulativeResolution =
            LayoutDeviceToParentLayerScale(
                rootPresShell->GetCumulativeResolution()) *
            GetTransformToAncestorScaleCrossProcessForFrameMetrics(rootFrame);

        int32_t rootAUPerDevPixel = rootPresContext->AppUnitsPerDevPixel();
        rootCompositionSize = (LayoutDeviceRect::FromAppUnits(
                                   rootFrame->GetRect(), rootAUPerDevPixel) *
                               cumulativeResolution)
                                  .Size();
      }
    }
  } else {
    nsIWidget* widget = aFrame->GetNearestWidget();
    LayoutDeviceIntRect widgetBounds = widget->GetBounds();
    rootCompositionSize = ScreenSize(ViewAs<ScreenPixel>(
        widgetBounds.Size(),
        PixelCastJustification::LayoutDeviceIsScreenForBounds));
  }

  // Adjust composition size for the size of scroll bars.
  nsIFrame* rootRootScrollContainerFrame =
      rootPresShell && !isPopupRoot
          ? rootPresShell->GetRootScrollContainerFrame()
          : nullptr;
  nsMargin scrollbarMargins = ScrollbarAreaToExcludeFromCompositionBoundsFor(
      rootRootScrollContainerFrame);
  LayoutDeviceMargin margins = LayoutDeviceMargin::FromAppUnits(
      scrollbarMargins, rootPresContext->AppUnitsPerDevPixel());
  // Scrollbars are not subject to resolution scaling, so LD pixels = layer
  // pixels for them.
  rootCompositionSize.width -= margins.LeftRight();
  rootCompositionSize.height -= margins.TopBottom();

  CSSSize result =
      rootCompositionSize / aMetrics.DisplayportPixelsPerCSSPixel();

  // If this is a nested content process, the in-process root content document's
  // composition size may still be arbitrarily large, so bound it further by
  // how much of the in-process RCD is visible in the top-level (cross-process
  // RCD) viewport.
  if (rootPresShell) {
    if (BrowserChild* bc = BrowserChild::GetFrom(rootPresShell)) {
      if (const auto& visibleRect =
              bc->GetTopLevelViewportVisibleRectInSelfCoords()) {
        CSSSize cssVisibleRect =
            visibleRect->Size() / rootPresContext->CSSToDevPixelScale();
        result = Min(result, cssVisibleRect);
      }
    }
  }

  return result;
}

/* static */
nsRect nsLayoutUtils::CalculateScrollableRectForFrame(
    const ScrollContainerFrame* aScrollContainerFrame,
    const nsIFrame* aRootFrame) {
  nsRect contentBounds;
  if (aScrollContainerFrame) {
    contentBounds = aScrollContainerFrame->GetScrollRange();

    nsPoint scrollPosition = aScrollContainerFrame->GetScrollPosition();
    if (aScrollContainerFrame->GetScrollStyles().mVertical ==
        StyleOverflow::Hidden) {
      contentBounds.y = scrollPosition.y;
      contentBounds.height = 0;
    }
    if (aScrollContainerFrame->GetScrollStyles().mHorizontal ==
        StyleOverflow::Hidden) {
      contentBounds.x = scrollPosition.x;
      contentBounds.width = 0;
    }

    contentBounds.width += aScrollContainerFrame->GetScrollPortRect().width;
    contentBounds.height += aScrollContainerFrame->GetScrollPortRect().height;
  } else {
    contentBounds = aRootFrame->GetRect();
    // Clamp to (0, 0) if there is no corresponding scrollable frame for the
    // given |aRootFrame|.
    contentBounds.MoveTo(0, 0);
  }
  return contentBounds;
}

/* static */
nsRect nsLayoutUtils::CalculateExpandedScrollableRect(nsIFrame* aFrame) {
  nsRect scrollableRect = CalculateScrollableRectForFrame(
      aFrame->GetScrollTargetFrame(), aFrame->PresShell()->GetRootFrame());
  nsSize compSize = CalculateCompositionSizeForFrame(aFrame);

  if (aFrame == aFrame->PresShell()->GetRootScrollContainerFrame()) {
    // the composition size for the root scroll frame does not include the
    // local resolution, so we adjust.
    float res = aFrame->PresShell()->GetResolution();
    compSize.width = NSToCoordRound(compSize.width / res);
    compSize.height = NSToCoordRound(compSize.height / res);
  }

  if (scrollableRect.width < compSize.width) {
    scrollableRect.x =
        std::max(0, scrollableRect.x - (compSize.width - scrollableRect.width));
    scrollableRect.width = compSize.width;
  }

  if (scrollableRect.height < compSize.height) {
    scrollableRect.y = std::max(
        0, scrollableRect.y - (compSize.height - scrollableRect.height));
    scrollableRect.height = compSize.height;
  }
  return scrollableRect;
}

/* static */
void nsLayoutUtils::DoLogTestDataForPaint(WebRenderLayerManager* aManager,
                                          ViewID aScrollId,
                                          const std::string& aKey,
                                          const std::string& aValue) {
  MOZ_ASSERT(nsLayoutUtils::IsAPZTestLoggingEnabled(), "don't call me");
  aManager->LogTestDataForCurrentPaint(aScrollId, aKey, aValue);
}

void nsLayoutUtils::LogAdditionalTestData(nsDisplayListBuilder* aBuilder,
                                          const std::string& aKey,
                                          const std::string& aValue) {
  WebRenderLayerManager* manager = aBuilder->GetWidgetLayerManager(nullptr);
  if (!manager) {
    return;
  }
  manager->LogAdditionalTestData(aKey, aValue);
}

/* static */
bool nsLayoutUtils::IsAPZTestLoggingEnabled() {
  return StaticPrefs::apz_test_logging_enabled();
}

////////////////////////////////////////
// SurfaceFromElementResult

SurfaceFromElementResult::SurfaceFromElementResult()
    // Use safe default values here
    : mHadCrossOriginRedirects(false),
      mIsWriteOnly(true),
      mIsStillLoading(false),
      mHasSize(false),
      mCORSUsed(false),
      mAlphaType(gfxAlphaType::Opaque) {}

const RefPtr<mozilla::gfx::SourceSurface>&
SurfaceFromElementResult::GetSourceSurface() {
  if (!mSourceSurface && mLayersImage) {
    mSourceSurface = mLayersImage->GetAsSourceSurface();
  }

  return mSourceSurface;
}

////////////////////////////////////////

bool nsLayoutUtils::IsNonWrapperBlock(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame);
  return aFrame->IsBlockFrameOrSubclass() && !aFrame->IsBlockWrapper();
}

AutoMaybeDisableFontInflation::AutoMaybeDisableFontInflation(nsIFrame* aFrame) {
  // FIXME: Now that inflation calculations are based on the flow
  // root's NCA's (nearest common ancestor of its inflatable
  // descendants) width, we could probably disable inflation in
  // fewer cases than we currently do.
  // MathML cells need special treatment. See bug 1002526 comment 56.
  if (aFrame->IsContainerForFontSizeInflation() && !aFrame->IsMathMLFrame()) {
    mPresContext = aFrame->PresContext();
    mOldValue = mPresContext->mInflationDisabledForShrinkWrap;
    mPresContext->mInflationDisabledForShrinkWrap = true;
  } else {
    // indicate we have nothing to restore
    mPresContext = nullptr;
    mOldValue = false;
  }
}

AutoMaybeDisableFontInflation::~AutoMaybeDisableFontInflation() {
  if (mPresContext) {
    mPresContext->mInflationDisabledForShrinkWrap = mOldValue;
  }
}

namespace mozilla {

Rect NSRectToRect(const nsRect& aRect, double aAppUnitsPerPixel) {
  // Note that by making aAppUnitsPerPixel a double we're doing floating-point
  // division using a larger type and avoiding rounding error.
  return Rect(Float(aRect.x / aAppUnitsPerPixel),
              Float(aRect.y / aAppUnitsPerPixel),
              Float(aRect.width / aAppUnitsPerPixel),
              Float(aRect.height / aAppUnitsPerPixel));
}

Rect NSRectToSnappedRect(const nsRect& aRect, double aAppUnitsPerPixel,
                         const gfx::DrawTarget& aSnapDT) {
  // Note that by making aAppUnitsPerPixel a double we're doing floating-point
  // division using a larger type and avoiding rounding error.
  Rect rect(Float(aRect.x / aAppUnitsPerPixel),
            Float(aRect.y / aAppUnitsPerPixel),
            Float(aRect.width / aAppUnitsPerPixel),
            Float(aRect.height / aAppUnitsPerPixel));
  MaybeSnapToDevicePixels(rect, aSnapDT, true);
  return rect;
}
// Similar to a snapped rect, except an axis is left unsnapped if the snapping
// process results in a length of 0.
Rect NSRectToNonEmptySnappedRect(const nsRect& aRect, double aAppUnitsPerPixel,
                                 const gfx::DrawTarget& aSnapDT) {
  // Note that by making aAppUnitsPerPixel a double we're doing floating-point
  // division using a larger type and avoiding rounding error.
  Rect rect(Float(aRect.x / aAppUnitsPerPixel),
            Float(aRect.y / aAppUnitsPerPixel),
            Float(aRect.width / aAppUnitsPerPixel),
            Float(aRect.height / aAppUnitsPerPixel));
  MaybeSnapToDevicePixels(rect, aSnapDT, true, false);
  return rect;
}

void StrokeLineWithSnapping(const nsPoint& aP1, const nsPoint& aP2,
                            int32_t aAppUnitsPerDevPixel,
                            DrawTarget& aDrawTarget, const Pattern& aPattern,
                            const StrokeOptions& aStrokeOptions,
                            const DrawOptions& aDrawOptions) {
  Point p1 = NSPointToPoint(aP1, aAppUnitsPerDevPixel);
  Point p2 = NSPointToPoint(aP2, aAppUnitsPerDevPixel);
  SnapLineToDevicePixelsForStroking(p1, p2, aDrawTarget,
                                    aStrokeOptions.mLineWidth);
  aDrawTarget.StrokeLine(p1, p2, aPattern, aStrokeOptions, aDrawOptions);
}

}  // namespace mozilla

/* static */
void nsLayoutUtils::SetBSizeFromFontMetrics(const nsIFrame* aFrame,
                                            ReflowOutput& aMetrics,
                                            const LogicalMargin& aFramePadding,
                                            WritingMode aLineWM,
                                            WritingMode aFrameWM) {
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetInflatedFontMetricsForFrame(aFrame);

  if (fm) {
    // Compute final height of the frame.
    //
    // Do things the standard css2 way -- though it's hard to find it
    // in the css2 spec! It's actually found in the css1 spec section
    // 4.4 (you will have to read between the lines to really see
    // it).
    //
    // The height of our box is the sum of our font size plus the top
    // and bottom border and padding. The height of children do not
    // affect our height.
    aMetrics.SetBlockStartAscent(
        aLineWM.IsAlphabeticalBaseline()
            ? aLineWM.IsLineInverted() ? fm->MaxDescent() : fm->MaxAscent()
            : fm->MaxHeight() / 2);
    aMetrics.BSize(aLineWM) = fm->MaxHeight();
  } else {
    NS_WARNING("Cannot get font metrics - defaulting sizes to 0");
    aMetrics.SetBlockStartAscent(aMetrics.BSize(aLineWM) = 0);
  }
  aMetrics.SetBlockStartAscent(aMetrics.BlockStartAscent() +
                               aFramePadding.BStart(aFrameWM));
  aMetrics.BSize(aLineWM) += aFramePadding.BStartEnd(aFrameWM);
}

/* static */
// _BOUNDARY because Dispatch() with `targets` must not handle the event.
MOZ_CAN_RUN_SCRIPT_BOUNDARY bool
nsLayoutUtils::HasDocumentLevelListenersForApzAwareEvents(
    PresShell* aPresShell) {
  if (RefPtr<Document> doc = aPresShell->GetDocument()) {
    WidgetEvent event(true, eVoidEvent);
    nsTArray<EventTarget*> targets;
    nsresult rv = EventDispatcher::Dispatch(doc, nullptr, &event, nullptr,
                                            nullptr, nullptr, &targets);
    NS_ENSURE_SUCCESS(rv, false);
    for (size_t i = 0; i < targets.Length(); i++) {
      if (targets[i]->IsApzAware()) {
        return true;
      }
    }
  }
  return false;
}

/* static */
bool nsLayoutUtils::CanScrollOriginClobberApz(ScrollOrigin aScrollOrigin) {
  switch (aScrollOrigin) {
    case ScrollOrigin::None:
    case ScrollOrigin::NotSpecified:
    case ScrollOrigin::Apz:
    case ScrollOrigin::Restore:
      return false;
    default:
      return true;
  }
}

/* static */
ScrollMetadata nsLayoutUtils::ComputeScrollMetadata(
    const nsIFrame* aForFrame, const nsIFrame* aScrollFrame,
    nsIContent* aContent, const nsIFrame* aItemFrame,
    const nsPoint& aOffsetToReferenceFrame,
    WebRenderLayerManager* aLayerManager, ViewID aScrollParentId,
    const nsSize& aScrollPortSize, bool aIsRootContent) {
  const nsPresContext* presContext = aForFrame->PresContext();
  int32_t auPerDevPixel = presContext->AppUnitsPerDevPixel();

  PresShell* presShell = presContext->GetPresShell();
  ScrollMetadata metadata;
  FrameMetrics& metrics = metadata.GetMetrics();
  metrics.SetLayoutViewport(
      CSSRect(CSSPoint(), CSSSize::FromAppUnits(aScrollPortSize)));

  nsIDocShell* docShell = presContext->GetDocShell();
  const BrowsingContext* bc =
      docShell ? docShell->GetBrowsingContext() : nullptr;
  bool isTouchEventsEnabled =
      bc &&
      bc->TouchEventsOverride() == mozilla::dom::TouchEventsOverride::Enabled;

  if (bc && bc->InRDMPane() && isTouchEventsEnabled) {
    metadata.SetIsRDMTouchSimulationActive(true);
  }

  ViewID scrollId = ScrollableLayerGuid::NULL_SCROLL_ID;
  if (aContent) {
    if (void* paintRequestTime =
            aContent->GetProperty(nsGkAtoms::paintRequestTime)) {
      metrics.SetPaintRequestTime(*static_cast<TimeStamp*>(paintRequestTime));
      aContent->RemoveProperty(nsGkAtoms::paintRequestTime);
    }
    scrollId = nsLayoutUtils::FindOrCreateIDFor(aContent);
    nsRect dp;
    if (DisplayPortUtils::GetDisplayPort(aContent, &dp)) {
      metrics.SetDisplayPort(CSSRect::FromAppUnits(dp));
      DisplayPortUtils::MarkDisplayPortAsPainted(aContent);
    }

    metrics.SetHasNonZeroDisplayPortMargins(false);
    if (DisplayPortMarginsPropertyData* currentData =
            static_cast<DisplayPortMarginsPropertyData*>(
                aContent->GetProperty(nsGkAtoms::DisplayPortMargins))) {
      if (currentData->mMargins.mMargins != ScreenMargin()) {
        metrics.SetHasNonZeroDisplayPortMargins(true);
      }
    }

    // Note: GetProperty() will return nullptr both in the case where
    // the property hasn't been set, and in the case where the property
    // has been set to false (in which case the property value is
    // `reinterpret_cast<void*>(false)` which is nullptr.
    if (aContent->GetProperty(nsGkAtoms::forceMousewheelAutodir)) {
      metadata.SetForceMousewheelAutodir(true);
    }

    if (aContent->GetProperty(nsGkAtoms::forceMousewheelAutodirHonourRoot)) {
      metadata.SetForceMousewheelAutodirHonourRoot(true);
    }

    if (IsAPZTestLoggingEnabled()) {
      LogTestDataForPaint(aLayerManager, scrollId, "displayport",
                          metrics.GetDisplayPort());
    }

    metrics.SetMinimalDisplayPort(
        aContent->GetProperty(nsGkAtoms::MinimalDisplayPort));
  }

  ScrollContainerFrame* scrollContainerFrame =
      aScrollFrame ? aScrollFrame->GetScrollTargetFrame() : nullptr;

  metrics.SetScrollableRect(
      CSSRect::FromAppUnits(nsLayoutUtils::CalculateScrollableRectForFrame(
          scrollContainerFrame, aForFrame)));

  if (scrollContainerFrame) {
    CSSPoint layoutScrollOffset =
        CSSPoint::FromAppUnits(scrollContainerFrame->GetScrollPosition());
    CSSPoint visualScrollOffset =
        aIsRootContent
            ? CSSPoint::FromAppUnits(presShell->GetVisualViewportOffset())
            : layoutScrollOffset;
    metrics.SetVisualScrollOffset(visualScrollOffset);
    // APZ sometimes reads this even if we haven't set a visual scroll
    // update type (specifically, in the isFirstPaint case), so always
    // set it.
    metrics.SetVisualDestination(visualScrollOffset);

    if (aIsRootContent) {
      if (aLayerManager->GetIsFirstPaint() &&
          presShell->IsVisualViewportOffsetSet()) {
        // Restore the visual viewport offset to the copy stored on the
        // main thread.
        presShell->ScrollToVisual(presShell->GetVisualViewportOffset(),
                                  FrameMetrics::eRestore, ScrollMode::Instant);
      }
    }

    if (scrollContainerFrame->IsRootScrollFrameOfDocument()) {
      if (const Maybe<PresShell::VisualScrollUpdate>& visualUpdate =
              presShell->GetPendingVisualScrollUpdate()) {
        metrics.SetVisualDestination(
            CSSPoint::FromAppUnits(visualUpdate->mVisualScrollOffset));
        metrics.SetVisualScrollUpdateType(visualUpdate->mUpdateType);
        presShell->AcknowledgePendingVisualScrollUpdate();
      }
    }

    if (aIsRootContent) {
      // Expand the layout viewport to the size including the area covered by
      // the dynamic toolbar in the case where the dynamic toolbar is being
      // used, otherwise when the dynamic toolbar transitions on the compositor,
      // the layout viewport will be smaller than the visual viewport on the
      // compositor, thus the layout viewport offset will be forced to be moved
      // in FrameMetrics::KeepLayoutViewportEnclosingVisualViewport.
      if (presContext->HasDynamicToolbar()) {
        CSSRect viewport = metrics.GetLayoutViewport();
        viewport.SizeTo(nsLayoutUtils::ExpandHeightForDynamicToolbar(
            presContext, viewport.Size()));
        metrics.SetLayoutViewport(viewport);

        // We need to set 'fixed margins' to adjust 'fixed margins' value on the
        // composiutor in the case where the dynamic toolbar is completely
        // hidden because the margin value on the compositor is offset from the
        // position where the dynamic toolbar is completely VISIBLE but now the
        // toolbar is completely HIDDEN we need to adjust the difference on the
        // compositor.
        if (presContext->GetDynamicToolbarState() ==
            DynamicToolbarState::Collapsed) {
          metrics.SetFixedLayerMargins(ScreenMargin(
              0, 0,
              ScreenCoord(presContext->GetDynamicToolbarHeight() -
                          presContext->GetDynamicToolbarMaxHeight()),
              0));
        }
      }

      metadata.SetIsSoftwareKeyboardVisible(presContext->GetKeyboardHeight() >
                                            0);
      metadata.SetInteractiveWidget(
          presContext->Document()->InteractiveWidget());
    }

    metrics.SetScrollGeneration(
        scrollContainerFrame->CurrentScrollGeneration());

    CSSRect viewport = metrics.GetLayoutViewport();
    viewport.MoveTo(layoutScrollOffset);
    metrics.SetLayoutViewport(viewport);

    nsSize lineScrollAmount = scrollContainerFrame->GetLineScrollAmount();
    LayoutDeviceIntSize lineScrollAmountInDevPixels =
        LayoutDeviceIntSize::FromAppUnitsRounded(
            lineScrollAmount, presContext->AppUnitsPerDevPixel());
    metadata.SetLineScrollAmount(lineScrollAmountInDevPixels);

    nsSize pageScrollAmount = scrollContainerFrame->GetPageScrollAmount();
    LayoutDeviceIntSize pageScrollAmountInDevPixels =
        LayoutDeviceIntSize::FromAppUnitsRounded(
            pageScrollAmount, presContext->AppUnitsPerDevPixel());
    metadata.SetPageScrollAmount(pageScrollAmountInDevPixels);

    if (aScrollFrame->GetParent()) {
      metadata.SetDisregardedDirection(
          WheelHandlingUtils::GetDisregardedWheelScrollDirection(
              aScrollFrame->GetParent()));
    }

    metadata.SetSnapInfo(scrollContainerFrame->GetScrollSnapInfo());
    metadata.SetOverscrollBehavior(
        scrollContainerFrame->GetOverscrollBehaviorInfo());
    auto scrollStyles = scrollContainerFrame->GetScrollStyles();
    metadata.SetOverflow({scrollStyles.mHorizontal, scrollStyles.mVertical});
    metadata.SetScrollUpdates(scrollContainerFrame->GetScrollUpdates());
  }

  // If we have the scrollparent being the same as the scroll id, the
  // compositor-side code could get into an infinite loop while building the
  // overscroll handoff chain.
  MOZ_ASSERT(aScrollParentId == ScrollableLayerGuid::NULL_SCROLL_ID ||
             scrollId != aScrollParentId);
  metrics.SetScrollId(scrollId);
  metrics.SetIsRootContent(aIsRootContent);
  metadata.SetScrollParentId(aScrollParentId);

  const nsIFrame* rootScrollContainerFrame =
      presShell->GetRootScrollContainerFrame();
  bool isRootScrollContainerFrame = aScrollFrame == rootScrollContainerFrame;
  Document* document = presShell->GetDocument();

  if (scrollId != ScrollableLayerGuid::NULL_SCROLL_ID) {
    if (aForFrame->IsMenuPopupFrame()) {
      // In the case of popup windows, the menu popup frame becomes the root.
      MOZ_ASSERT(XRE_IsParentProcess());
      metadata.SetIsLayersIdRoot(true);
    } else if (!presContext->GetParentPresContext()) {
      if ((aScrollFrame && isRootScrollContainerFrame)) {
        metadata.SetIsLayersIdRoot(true);
      } else {
        MOZ_ASSERT(document, "A non-root-scroll frame must be in a document");
        if (aContent == document->GetDocumentElement()) {
          metadata.SetIsLayersIdRoot(true);
        }
      }
    }
  }

  // Get whether the root content is RTL(E.g. it's true either if
  // "writing-mode: vertical-rl", or if
  // "writing-mode: horizontal-tb; direction: rtl;" in CSS).
  // For the concept of this and the reason why we need to get this kind of
  // information, see the definition of |mIsAutoDirRootContentRTL| in struct
  // |ScrollMetadata|.
  const Element* bodyElement = document ? document->GetBodyElement() : nullptr;
  const nsIFrame* primaryFrame =
      bodyElement ? bodyElement->GetPrimaryFrame() : rootScrollContainerFrame;
  if (!primaryFrame) {
    primaryFrame = rootScrollContainerFrame;
  }
  if (primaryFrame) {
    WritingMode writingModeOfRootScrollFrame = primaryFrame->GetWritingMode();
    if (writingModeOfRootScrollFrame.IsPhysicalRTL()) {
      metadata.SetIsAutoDirRootContentRTL(true);
    }
  }

  // Only the root scrollable frame for a given presShell should pick up
  // the presShell's resolution. All the other frames are 1.0.
  if (isRootScrollContainerFrame) {
    metrics.SetPresShellResolution(presShell->GetResolution());
  } else {
    metrics.SetPresShellResolution(1.0f);
  }

  if (presShell->IsResolutionUpdated()) {
    metadata.SetResolutionUpdated(true);
  }

  // The cumulative resolution is the resolution at which the scroll frame's
  // content is actually rendered. It includes the pres shell resolutions of
  // all the pres shells from here up to the root, as well as any css-driven
  // resolution. We don't need to compute it as it's already stored in the
  // container parameters... except if we're in WebRender in which case we
  // don't have a aContainerParameters. In that case we're also not rasterizing
  // in Gecko anyway, so the only resolution we care about here is the presShell
  // resolution which we need to propagate to WebRender.
  metrics.SetCumulativeResolution(
      LayoutDeviceToLayerScale(presShell->GetCumulativeResolution()));

  metrics.SetTransformToAncestorScale(
      GetTransformToAncestorScaleCrossProcessForFrameMetrics(
          aScrollFrame ? aScrollFrame : aForFrame));
  metrics.SetDevPixelsPerCSSPixel(presContext->CSSToDevPixelScale());

  // Initially, AsyncPanZoomController should render the content to the screen
  // at the painted resolution.
  const LayerToParentLayerScale layerToParentLayerScale(1.0f);
  metrics.SetZoom(metrics.GetCumulativeResolution() *
                  metrics.GetDevPixelsPerCSSPixel() * layerToParentLayerScale);

  // Calculate the composition bounds as the size of the scroll frame and
  // its origin relative to the reference frame.
  // If aScrollFrame is null, we are in a document without a root scroll frame,
  // so it's a xul document. In this case, use the size of the viewport frame.
  const nsIFrame* frameForCompositionBoundsCalculation =
      aScrollFrame ? aScrollFrame : aForFrame;
  nsRect compositionBounds(
      frameForCompositionBoundsCalculation->GetOffsetToCrossDoc(aItemFrame) +
          aOffsetToReferenceFrame,
      frameForCompositionBoundsCalculation->GetSize());
  if (scrollContainerFrame) {
    // If we have a scrollable frame, restrict the composition bounds to its
    // scroll port. The scroll port excludes the frame borders and the scroll
    // bars, which we don't want to be part of the composition bounds.
    nsRect scrollPort = scrollContainerFrame->GetScrollPortRect();
    compositionBounds = nsRect(
        compositionBounds.TopLeft() + scrollPort.TopLeft(), scrollPort.Size());
  }
  ParentLayerRect frameBounds =
      LayoutDeviceRect::FromAppUnits(compositionBounds, auPerDevPixel) *
      metrics.GetCumulativeResolution() * layerToParentLayerScale;

  // For the root scroll frame of the root content document (RCD-RSF), the above
  // calculation will yield the size of the viewport frame as the composition
  // bounds, which doesn't actually correspond to what is visible when
  // nsIDOMWindowUtils::setCSSViewport has been called to modify the visible
  // area of the prescontext that the viewport frame is reflowed into. In that
  // case if our document has a widget then the widget's bounds will correspond
  // to what is visible. If we don't have a widget the root view's bounds
  // correspond to what would be visible because they don't get modified by
  // setCSSViewport.
  bool isRootContentDocRootScrollFrame =
      isRootScrollContainerFrame &&
      presContext->IsRootContentDocumentCrossProcess();
  if (isRootContentDocRootScrollFrame) {
    UpdateCompositionBoundsForRCDRSF(frameBounds, presContext);
    if (RefPtr<MobileViewportManager> MVM =
            presContext->PresShell()->GetMobileViewportManager()) {
      metrics.SetCompositionSizeWithoutDynamicToolbar(
          MVM->GetCompositionSizeWithoutDynamicToolbar());
    }
  }

  metrics.SetCompositionBoundsWidthIgnoringScrollbars(frameBounds.width);

  nsMargin sizes = ScrollbarAreaToExcludeFromCompositionBoundsFor(aScrollFrame);
  // Scrollbars are not subject to resolution scaling, so LD pixels = layer
  // pixels for them.
  ParentLayerMargin boundMargins =
      LayoutDeviceMargin::FromAppUnits(sizes, auPerDevPixel) *
      LayoutDeviceToParentLayerScale(1.0f);
  frameBounds.Deflate(boundMargins);

  metrics.SetCompositionBounds(frameBounds);

  metrics.SetBoundingCompositionSize(
      nsLayoutUtils::CalculateBoundingCompositionSize(
          aScrollFrame ? aScrollFrame : aForFrame,
          isRootContentDocRootScrollFrame, metrics));

  if (StaticPrefs::apz_printtree() || StaticPrefs::apz_test_logging_enabled()) {
    if (const nsIContent* content =
            frameForCompositionBoundsCalculation->GetContent()) {
      nsAutoString contentDescription;
      if (content->IsElement()) {
        content->AsElement()->Describe(contentDescription);
      } else {
        contentDescription.AssignLiteral("(not an element)");
      }
      metadata.SetContentDescription(
          NS_LossyConvertUTF16toASCII(contentDescription));
      if (IsAPZTestLoggingEnabled()) {
        LogTestDataForPaint(aLayerManager, scrollId, "contentDescription",
                            metadata.GetContentDescription().get());
      }
    }
  }

  metrics.SetPresShellId(presShell->GetPresShellId());

  if (ShouldDisableApzForElement(aContent)) {
    metadata.SetForceDisableApz(true);
  }

  metadata.SetIsPaginatedPresentation(presContext->Type() !=
                                      nsPresContext::eContext_Galley);

  return metadata;
}

/*static*/
Maybe<ScrollMetadata> nsLayoutUtils::GetRootMetadata(
    nsDisplayListBuilder* aBuilder, WebRenderLayerManager* aLayerManager,
    const std::function<bool(ViewID& aScrollId)>& aCallback) {
  nsIFrame* frame = aBuilder->RootReferenceFrame();
  nsPresContext* presContext = frame->PresContext();
  PresShell* presShell = presContext->PresShell();
  Document* document = presShell->GetDocument();

  // There is one case where we want the root container layer to have metrics.
  // If the parent process is using XUL windows, there is no root scrollframe,
  // and without explicitly creating metrics there will be no guaranteed
  // top-level APZC.
  bool addMetrics =
      XRE_IsParentProcess() && !presShell->GetRootScrollContainerFrame();

  // Add metrics if there are none in the layer tree with the id (create an id
  // if there isn't one already) of the root scroll frame/root content.
  bool ensureMetricsForRootId =
      nsLayoutUtils::AsyncPanZoomEnabled(frame) &&
      aBuilder->IsPaintingToWindow() &&
      (!presContext->GetParentPresContext() || frame->IsMenuPopupFrame());
  MOZ_ASSERT(!presContext->GetParentPresContext() || frame->IsMenuPopupFrame());

  nsIContent* content = nullptr;
  ScrollContainerFrame* rootScrollContainerFrame =
      presShell->GetRootScrollContainerFrame();
  if (frame->IsMenuPopupFrame()) {
    content = frame->GetContent();
  } else if (rootScrollContainerFrame) {
    content = rootScrollContainerFrame->GetContent();
  } else {
    // If there is no root scroll frame, pick the document element instead.
    // The only case we don't want to do this is in non-APZ fennec, where
    // we want the root xul document to get a null scroll id so that the root
    // content document gets the first non-null scroll id.
    content = document->GetDocumentElement();
  }

  if (ensureMetricsForRootId && content) {
    ViewID scrollId = nsLayoutUtils::FindOrCreateIDFor(content);
    if (aCallback(scrollId)) {
      ensureMetricsForRootId = false;
    }
  }

  if (addMetrics || ensureMetricsForRootId) {
    bool isRootContent = presContext->IsRootContentDocumentCrossProcess();

    nsSize scrollPortSize = frame->GetSize();
    if (isRootContent && rootScrollContainerFrame) {
      scrollPortSize = rootScrollContainerFrame->GetScrollPortRect().Size();
    }
    return Some(nsLayoutUtils::ComputeScrollMetadata(
        frame, rootScrollContainerFrame, content, frame,
        aBuilder->ToReferenceFrame(frame), aLayerManager,
        ScrollableLayerGuid::NULL_SCROLL_ID, scrollPortSize, isRootContent));
  }

  return Nothing();
}

/* static */
void nsLayoutUtils::TransformToAncestorAndCombineRegions(
    const nsRegion& aRegion, nsIFrame* aFrame, const nsIFrame* aAncestorFrame,
    nsRegion* aPreciseTargetDest, nsRegion* aImpreciseTargetDest,
    Maybe<Matrix4x4Flagged>* aMatrixCache, const DisplayItemClip* aClip) {
  if (aRegion.IsEmpty()) {
    return;
  }
  bool isPrecise;
  RegionBuilder<nsRegion> transformedRegion;
  for (nsRegion::RectIterator it = aRegion.RectIter(); !it.Done(); it.Next()) {
    nsRect transformed = TransformFrameRectToAncestor(
        aFrame, it.Get(), aAncestorFrame, &isPrecise, aMatrixCache);
    if (aClip) {
      transformed = aClip->ApplyNonRoundedIntersection(transformed);
      if (aClip->GetRoundedRectCount() > 0) {
        isPrecise = false;
      }
    }
    transformedRegion.OrWith(transformed);
  }
  nsRegion* dest = isPrecise ? aPreciseTargetDest : aImpreciseTargetDest;
  dest->OrWith(transformedRegion.ToRegion());
  // If the region becomes too complex this has a large performance impact.
  // We limit its complexity here.
  if (dest->GetNumRects() > 12) {
    dest->SimplifyOutward(6);
    if (isPrecise) {
      aPreciseTargetDest->OrWith(*aImpreciseTargetDest);
      *aImpreciseTargetDest = std::move(*aPreciseTargetDest);
      aImpreciseTargetDest->SimplifyOutward(6);
      *aPreciseTargetDest = nsRegion();
    }
  }
}

/* static */
bool nsLayoutUtils::ShouldUseNoFramesSheet(Document* aDocument) {
  bool allowSubframes = true;
  nsIDocShell* docShell = aDocument->GetDocShell();
  if (docShell) {
    docShell->GetAllowSubframes(&allowSubframes);
  }
  return !allowSubframes;
}

/* static */
void nsLayoutUtils::GetFrameTextContent(nsIFrame* aFrame, nsAString& aResult) {
  aResult.Truncate();
  AppendFrameTextContent(aFrame, aResult);
}

/* static */
void nsLayoutUtils::AppendFrameTextContent(nsIFrame* aFrame,
                                           nsAString& aResult) {
  if (aFrame->IsTextFrame()) {
    auto* const textFrame = static_cast<nsTextFrame*>(aFrame);
    const auto offset = AssertedCast<uint32_t>(textFrame->GetContentOffset());
    const auto length = AssertedCast<uint32_t>(textFrame->GetContentLength());
    textFrame->TextFragment()->AppendTo(aResult, offset, length);
  } else {
    for (nsIFrame* child : aFrame->PrincipalChildList()) {
      AppendFrameTextContent(child, aResult);
    }
  }
}

/* static */
nsRect nsLayoutUtils::GetSelectionBoundingRect(const Selection* aSel) {
  nsRect res;
  // Bounding client rect may be empty after calling GetBoundingClientRect
  // when range is collapsed. So we get caret's rect when range is
  // collapsed.
  if (aSel->IsCollapsed()) {
    nsIFrame* frame = nsCaret::GetGeometry(aSel, &res);
    if (frame) {
      nsIFrame* relativeTo = GetContainingBlockForClientRect(frame);
      res = TransformFrameRectToAncestor(frame, res, relativeTo);
    }
  } else {
    RectAccumulator accumulator;
    const uint32_t rangeCount = aSel->RangeCount();
    for (const uint32_t idx : IntegerRange(rangeCount)) {
      MOZ_ASSERT(aSel->RangeCount() == rangeCount);
      nsRange* range = aSel->GetRangeAt(idx);
      nsRange::CollectClientRectsAndText(
          &accumulator, nullptr, range, range->GetStartContainer(),
          range->StartOffset(), range->GetEndContainer(), range->EndOffset(),
          true, false);
    }
    res = accumulator.mResultRect.IsEmpty() ? accumulator.mFirstRect
                                            : accumulator.mResultRect;
  }

  return res;
}

/* static */
nsBlockFrame* nsLayoutUtils::GetFloatContainingBlock(nsIFrame* aFrame) {
  nsIFrame* ancestor = aFrame->GetParent();
  while (ancestor && !ancestor->IsFloatContainingBlock()) {
    ancestor = ancestor->GetParent();
  }
  MOZ_ASSERT(!ancestor || ancestor->IsBlockFrameOrSubclass(),
             "Float containing block can only be block frame");
  return static_cast<nsBlockFrame*>(ancestor);
}

// The implementations of this calculation are adapted from
// Element::GetBoundingClientRect().
/* static */
CSSRect nsLayoutUtils::GetBoundingContentRect(
    const nsIContent* aContent,
    const ScrollContainerFrame* aRootScrollContainerFrame,
    Maybe<CSSRect>* aOutNearestScrollClip) {
  if (nsIFrame* frame = aContent->GetPrimaryFrame()) {
    return GetBoundingFrameRect(frame, aRootScrollContainerFrame,
                                aOutNearestScrollClip);
  }
  return CSSRect();
}

/* static */
CSSRect nsLayoutUtils::GetBoundingFrameRect(
    nsIFrame* aFrame, const ScrollContainerFrame* aRootScrollContainerFrame,
    Maybe<CSSRect>* aOutNearestScrollClip) {
  CSSRect result;
  nsIFrame* relativeTo = aRootScrollContainerFrame->GetScrolledFrame();
  result = CSSRect::FromAppUnits(nsLayoutUtils::GetAllInFlowRectsUnion(
      aFrame, relativeTo,
      nsLayoutUtils::GetAllInFlowRectsFlag::AccountForTransforms));

  // If the element is contained in a scroll container frame that is not the
  // root scroll container frame, make sure to clip the result so that it is not
  // larger than the containing scroll container frame's bounds.
  ScrollContainerFrame* scrollContainerFrame =
      nsLayoutUtils::GetNearestScrollContainerFrame(
          aFrame, SCROLLABLE_INCLUDE_HIDDEN | SCROLLABLE_FIXEDPOS_FINDS_ROOT);
  if (scrollContainerFrame &&
      scrollContainerFrame != aRootScrollContainerFrame) {
    // Get the bounds of the scroll frame in the same coordinate space
    // as |result|.
    nsRect subFrameRect = scrollContainerFrame->GetRectRelativeToSelf();
    TransformResult res = nsLayoutUtils::TransformRect(
        scrollContainerFrame, relativeTo, subFrameRect);
    MOZ_ASSERT(res == TRANSFORM_SUCCEEDED || res == NONINVERTIBLE_TRANSFORM);
    if (res == TRANSFORM_SUCCEEDED) {
      CSSRect subFrameRectCSS = CSSRect::FromAppUnits(subFrameRect);
      if (aOutNearestScrollClip) {
        *aOutNearestScrollClip = Some(subFrameRectCSS);
      }

      result = subFrameRectCSS.Intersect(result);
    }
  }
  return result;
}

/* static */
bool nsLayoutUtils::IsTransformed(nsIFrame* aForFrame, nsIFrame* aTopFrame) {
  for (nsIFrame* f = aForFrame; f != aTopFrame; f = f->GetParent()) {
    if (f->IsTransformed()) {
      return true;
    }
  }
  return false;
}

/*static*/
CSSPoint nsLayoutUtils::GetCumulativeApzCallbackTransform(nsIFrame* aFrame) {
  CSSPoint delta;
  if (!aFrame) {
    return delta;
  }
  nsIFrame* frame = aFrame;
  nsCOMPtr<nsIContent> lastContent;
  bool seenRcdRsf = false;

  // Helper lambda to apply the callback transform for a single frame.
  auto applyCallbackTransformForFrame = [&](nsIFrame* frame) {
    if (frame) {
      nsCOMPtr<nsIContent> content = frame->GetContent();
      if (content && (content != lastContent)) {
        void* property = content->GetProperty(nsGkAtoms::apzCallbackTransform);
        if (property) {
          delta += *static_cast<CSSPoint*>(property);
        }
      }
      lastContent = content;
    }
  };

  while (frame) {
    // Apply the callback transform for the current frame.
    applyCallbackTransformForFrame(frame);

    // Keep track of whether we've encountered the RCD-RSF's content element.
    nsPresContext* pc = frame->PresContext();
    if (pc->IsRootContentDocumentCrossProcess()) {
      if (PresShell* shell = pc->GetPresShell()) {
        if (nsIFrame* rsf = shell->GetRootScrollContainerFrame()) {
          if (frame->GetContent() == rsf->GetContent()) {
            seenRcdRsf = true;
          }
        }
      }
    }

    // If we reach the RCD's viewport frame, but have not encountered
    // the RCD-RSF, we were inside fixed content in the RCD.
    // We still want to apply the RCD-RSF's callback transform because
    // it contains the offset between the visual and layout viewports
    // which applies to fixed content as well.
    ViewportFrame* viewportFrame = do_QueryFrame(frame);
    if (viewportFrame) {
      if (pc->IsRootContentDocumentCrossProcess() && !seenRcdRsf) {
        applyCallbackTransformForFrame(
            pc->PresShell()->GetRootScrollContainerFrame());
      }
    }

    // Proceed to the parent frame.
    frame = GetCrossDocParentFrameInProcess(frame);
  }
  return delta;
}

static nsSize ComputeMaxSizeForPartialPrerender(nsIFrame* aFrame,
                                                nsSize aMaxSize) {
  Matrix4x4Flagged transform = nsLayoutUtils::GetTransformToAncestor(
      RelativeTo{aFrame},
      RelativeTo{nsLayoutUtils::GetDisplayRootFrame(aFrame)});

  Matrix transform2D;
  if (!transform.Is2D(&transform2D)) {
    return aMaxSize;
  }

  gfx::Rect result(0, 0, aMaxSize.width, aMaxSize.height);
  auto scale = transform2D.ScaleFactors();
  if (scale.xScale != 0 && scale.yScale != 0) {
    result.width /= scale.xScale;
    result.height /= scale.yScale;
  }

  // Don't apply translate.
  transform2D._31 = 0.0f;
  transform2D._32 = 0.0f;

  // Don't apply scale.
  if (scale.xScale != 0 && scale.yScale != 0) {
    transform2D._11 /= scale.xScale;
    transform2D._12 /= scale.xScale;
    transform2D._21 /= scale.yScale;
    transform2D._22 /= scale.yScale;
  }

  // Theoretically we should use transform2D.Inverse() here but in this case
  // |transform2D| is a pure rotation matrix, no scaling, no translate at all,
  // so that the result bound's width and height would be pretty much same
  // as the one rotated by the inverse matrix.
  result = transform2D.TransformBounds(result);
  return nsSize(
      result.width < (float)nscoord_MAX ? result.width : nscoord_MAX,
      result.height < (float)nscoord_MAX ? result.height : nscoord_MAX);
}

/* static */
nsRect nsLayoutUtils::ComputePartialPrerenderArea(
    nsIFrame* aFrame, const nsRect& aDirtyRect, const nsRect& aOverflow,
    const nsSize& aPrerenderSize) {
  nsSize maxSizeForPartialPrerender =
      ComputeMaxSizeForPartialPrerender(aFrame, aPrerenderSize);
  // Simple calculation for now: center the pre-render area on the dirty rect,
  // and clamp to the overflow area. Later we can do more advanced things like
  // redistributing from one axis to another, or from one side to another.
  nscoord xExcess =
      std::max(maxSizeForPartialPrerender.width - aDirtyRect.width, 0);
  nscoord yExcess =
      std::max(maxSizeForPartialPrerender.height - aDirtyRect.height, 0);
  nsRect result = aDirtyRect;
  result.Inflate(xExcess / 2, yExcess / 2);
  return result.MoveInsideAndClamp(aOverflow);
}

static bool LineHasNonEmptyContentWorker(nsIFrame* aFrame) {
  // Look for non-empty frames, but ignore inline and br frames.
  // For inline frames, descend into the children, if any.
  if (aFrame->IsInlineFrame()) {
    for (nsIFrame* child : aFrame->PrincipalChildList()) {
      if (LineHasNonEmptyContentWorker(child)) {
        return true;
      }
    }
  } else {
    if (!aFrame->IsBrFrame() && !aFrame->IsEmpty()) {
      return true;
    }
  }
  return false;
}

static bool LineHasNonEmptyContent(nsLineBox* aLine) {
  int32_t count = aLine->GetChildCount();
  for (nsIFrame* frame = aLine->mFirstChild; count > 0;
       --count, frame = frame->GetNextSibling()) {
    if (LineHasNonEmptyContentWorker(frame)) {
      return true;
    }
  }
  return false;
}

/* static */
bool nsLayoutUtils::IsInvisibleBreak(nsINode* aNode,
                                     nsIFrame** aNextLineFrame) {
  if (aNextLineFrame) {
    *aNextLineFrame = nullptr;
  }

  if (!aNode->IsElement() || !aNode->IsEditable()) {
    return false;
  }
  nsIFrame* frame = aNode->AsElement()->GetPrimaryFrame();
  if (!frame || !frame->IsBrFrame()) {
    return false;
  }

  nsContainerFrame* f = frame->GetParent();
  while (f && f->IsLineParticipant()) {
    f = f->GetParent();
  }
  nsBlockFrame* blockAncestor = do_QueryFrame(f);
  if (!blockAncestor) {
    // The container frame doesn't support line breaking.
    return false;
  }

  bool valid = false;
  nsBlockInFlowLineIterator iter(blockAncestor, frame, &valid);
  if (!valid) {
    return false;
  }

  bool lineNonEmpty = LineHasNonEmptyContent(iter.GetLine());
  if (!lineNonEmpty) {
    return false;
  }

  while (iter.Next()) {
    auto currentLine = iter.GetLine();
    // Completely skip empty lines.
    if (!currentLine->IsEmpty()) {
      // If we come across an inline line, the BR has caused a visible line
      // break.
      if (currentLine->IsInline()) {
        if (aNextLineFrame) {
          *aNextLineFrame = currentLine->mFirstChild;
        }
        return false;
      }
      break;
    }
  }

  return lineNonEmpty;
}

/* static */
nsRect nsLayoutUtils::ComputeSVGOriginBox(SVGViewportElement* aElement) {
  if (!aElement) {
    return {};
  }

  if (aElement->HasViewBox()) {
    // Return the "origin box", which is defined as a rect positioned at the
    // origin, but with the width and height given by the viewBox attribute
    //
    // https://drafts.csswg.org/css-box-3/#valdef-box-view-box
    //
    // For more discussion see
    // https://github.com/web-platform-tests/interop/issues/509
    const SVGViewBox& value = aElement->GetAnimatedViewBox()->GetAnimValue();
    return nsRect(0, 0, nsPresContext::CSSPixelsToAppUnits(value.width),
                  nsPresContext::CSSPixelsToAppUnits(value.height));
  }

  // No viewBox is specified, uses the nearest SVG viewport as reference
  // box.
  auto viewportSize = aElement->GetViewportSize();
  return nsRect(0, 0, nsPresContext::CSSPixelsToAppUnits(viewportSize.width),
                nsPresContext::CSSPixelsToAppUnits(viewportSize.height));
}

/* static */
nsRect nsLayoutUtils::ComputeSVGReferenceRect(
    nsIFrame* aFrame, StyleGeometryBox aGeometryBox,
    MayHaveNonScalingStrokeCyclicDependency aMayHaveCyclicDependency) {
  MOZ_ASSERT(aFrame->GetContent()->IsSVGElement());
  nsRect r;

  switch (aGeometryBox) {
    case StyleGeometryBox::StrokeBox: {
      // XXX Bug 1299876
      // The size of stroke-box is not correct if this graphic element has
      // specific stroke-linejoin or stroke-linecap.
      const uint32_t flags = SVGUtils::eBBoxIncludeFillGeometry |
                             SVGUtils::eBBoxIncludeStroke |
                             (bool(aMayHaveCyclicDependency)
                                  ? SVGUtils::eAvoidCycleIfNonScalingStroke
                                  : 0);
      gfxRect bbox = SVGUtils::GetBBox(aFrame, flags);
      r = nsLayoutUtils::RoundGfxRectToAppRect(bbox, AppUnitsPerCSSPixel());
      break;
    }
    case StyleGeometryBox::ViewBox: {
      SVGViewportElement* viewportElement =
          SVGElement::FromNode(aFrame->GetContent())->GetCtx();
      if (!viewportElement) {
        // We should not render without a viewport so return an empty rect.
        break;
      }
      r = nsLayoutUtils::ComputeSVGOriginBox(viewportElement);
      break;
    }
    case StyleGeometryBox::FillBox: {
      gfxRect bbox =
          SVGUtils::GetBBox(aFrame, SVGUtils::eBBoxIncludeFillGeometry);
      r = nsLayoutUtils::RoundGfxRectToAppRect(bbox, AppUnitsPerCSSPixel());
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unsupported SVG box");
      break;
    }
  }

  return r;
}

/* static */
nsRect nsLayoutUtils::ComputeHTMLReferenceRect(const nsIFrame* aFrame,
                                               StyleGeometryBox aGeometryBox) {
  nsRect r;

  switch (aGeometryBox) {
    case StyleGeometryBox::ContentBox:
      r = aFrame->GetContentRectRelativeToSelf();
      break;
    case StyleGeometryBox::PaddingBox:
      r = aFrame->GetPaddingRectRelativeToSelf();
      break;
    case StyleGeometryBox::MarginBox:
      r = aFrame->GetMarginRectRelativeToSelf();
      break;
    case StyleGeometryBox::BorderBox:
      r = aFrame->GetRectRelativeToSelf();
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unsupported CSS box");
      break;
  }

  return r;
}

static StyleGeometryBox ShapeBoxToGeometryBox(const StyleShapeBox& aBox) {
  switch (aBox) {
    case StyleShapeBox::BorderBox:
      return StyleGeometryBox::BorderBox;
    case StyleShapeBox::ContentBox:
      return StyleGeometryBox::ContentBox;
    case StyleShapeBox::MarginBox:
      return StyleGeometryBox::MarginBox;
    case StyleShapeBox::PaddingBox:
      return StyleGeometryBox::PaddingBox;
  }
  MOZ_ASSERT_UNREACHABLE("Unknown shape box type");
  return StyleGeometryBox::MarginBox;
}

static StyleGeometryBox ClipPathBoxToGeometryBox(
    const StyleShapeGeometryBox& aBox) {
  using Tag = StyleShapeGeometryBox::Tag;
  switch (aBox.tag) {
    case Tag::ShapeBox:
      return ShapeBoxToGeometryBox(aBox.AsShapeBox());
    case Tag::ElementDependent:
      return StyleGeometryBox::NoBox;
    case Tag::FillBox:
      return StyleGeometryBox::FillBox;
    case Tag::StrokeBox:
      return StyleGeometryBox::StrokeBox;
    case Tag::ViewBox:
      return StyleGeometryBox::ViewBox;
  }
  MOZ_ASSERT_UNREACHABLE("Unknown shape box type");
  return StyleGeometryBox::NoBox;
}

// The mapping is from
// https://drafts.fxtf.org/css-masking-1/#typedef-geometry-box
/* static */
nsRect nsLayoutUtils::ComputeClipPathGeometryBox(
    nsIFrame* aFrame, const StyleShapeGeometryBox& aBox) {
  StyleGeometryBox box = ClipPathBoxToGeometryBox(aBox);

  if (aFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
    // For SVG elements without associated CSS layout box, the used value for
    // content-box and padding-box is fill-box and for border-box and margin-box
    // is stroke-box.
    switch (box) {
      case StyleGeometryBox::ContentBox:
      case StyleGeometryBox::PaddingBox:
      case StyleGeometryBox::FillBox:
        return ComputeSVGReferenceRect(aFrame, StyleGeometryBox::FillBox);
      case StyleGeometryBox::NoBox:
      case StyleGeometryBox::BorderBox:
      case StyleGeometryBox::MarginBox:
      case StyleGeometryBox::StrokeBox:
        return ComputeSVGReferenceRect(aFrame, StyleGeometryBox::StrokeBox);
      case StyleGeometryBox::ViewBox:
        return ComputeSVGReferenceRect(aFrame, StyleGeometryBox::ViewBox);
      default:
        MOZ_ASSERT_UNREACHABLE("Unknown clip-path geometry box");
        // Use default, border-box (as stroke-box in SVG layout).
        return ComputeSVGReferenceRect(aFrame, StyleGeometryBox::StrokeBox);
    }
  }

  // For elements with associated CSS layout box, the used value for fill-box is
  // content-box and for stroke-box and view-box is border-box.
  switch (box) {
    case StyleGeometryBox::FillBox:
    case StyleGeometryBox::ContentBox:
      return ComputeHTMLReferenceRect(aFrame, StyleGeometryBox::ContentBox);
    case StyleGeometryBox::NoBox:
    case StyleGeometryBox::StrokeBox:
    case StyleGeometryBox::ViewBox:
    case StyleGeometryBox::BorderBox:
      return ComputeHTMLReferenceRect(aFrame, StyleGeometryBox::BorderBox);
    case StyleGeometryBox::PaddingBox:
      return ComputeHTMLReferenceRect(aFrame, StyleGeometryBox::PaddingBox);
    case StyleGeometryBox::MarginBox:
      return ComputeHTMLReferenceRect(aFrame, StyleGeometryBox::MarginBox);
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown clip-path geometry box");
      // Use default, border-box.
      return ComputeHTMLReferenceRect(aFrame, StyleGeometryBox::BorderBox);
  }
}

/* static */
nsPoint nsLayoutUtils::ComputeOffsetToUserSpace(nsDisplayListBuilder* aBuilder,
                                                nsIFrame* aFrame) {
  nsPoint offsetToBoundingBox =
      aBuilder->ToReferenceFrame(aFrame) -
      SVGIntegrationUtils::GetOffsetToBoundingBox(aFrame);
  if (!aFrame->IsSVGFrame()) {
    // Snap the offset if the reference frame is not a SVG frame, since other
    // frames will be snapped to pixel when rendering.
    offsetToBoundingBox =
        nsPoint(aFrame->PresContext()->RoundAppUnitsToNearestDevPixels(
                    offsetToBoundingBox.x),
                aFrame->PresContext()->RoundAppUnitsToNearestDevPixels(
                    offsetToBoundingBox.y));
  }

  // During SVG painting, the offset computed here is applied to the gfxContext
  // "ctx" used to paint the mask. After applying only "offsetToBoundingBox",
  // "ctx" would have its origin at the top left corner of frame's bounding box
  // (over all continuations).
  // However, SVG painting needs the origin to be located at the origin of the
  // SVG frame's "user space", i.e. the space in which, for example, the
  // frame's BBox lives.
  // SVG geometry frames and foreignObject frames apply their own offsets, so
  // their position is relative to their user space. So for these frame types,
  // if we want "ctx" to be in user space, we first need to subtract the
  // frame's position so that SVG painting can later add it again and the
  // frame is painted in the right place.
  gfxPoint toUserSpaceGfx =
      SVGUtils::FrameSpaceInCSSPxToUserSpaceOffset(aFrame);
  nsPoint toUserSpace =
      nsPoint(nsPresContext::CSSPixelsToAppUnits(float(toUserSpaceGfx.x)),
              nsPresContext::CSSPixelsToAppUnits(float(toUserSpaceGfx.y)));

  return (offsetToBoundingBox - toUserSpace);
}

/* static */
already_AddRefed<nsFontMetrics> nsLayoutUtils::GetMetricsFor(
    nsPresContext* aPresContext, bool aIsVertical,
    const nsStyleFont* aStyleFont, Length aFontSize, bool aUseUserFontSet) {
  nsFont font = aStyleFont->mFont;
  font.size = aFontSize;
  gfxFont::Orientation orientation =
      aIsVertical ? nsFontMetrics::eVertical : nsFontMetrics::eHorizontal;
  nsFontMetrics::Params params;
  params.language = aStyleFont->mLanguage;
  params.explicitLanguage = aStyleFont->mExplicitLanguage;
  params.orientation = orientation;
  params.userFontSet =
      aUseUserFontSet ? aPresContext->GetUserFontSet() : nullptr;
  params.textPerf = aPresContext->GetTextPerfMetrics();
  params.featureValueLookup = aPresContext->GetFontFeatureValuesLookup();
  return aPresContext->GetMetricsFor(font, params);
}

static void GetSpoofedSystemFontForRFP(LookAndFeel::FontID aFontID,
                                       gfxFontStyle& aStyle, nsAString& aName) {
#if defined(XP_MACOSX) || defined(MOZ_WIDGET_UIKIT)
  aName = u"-apple-system"_ns;
  // Values taken from a macOS 10.15 system.
  switch (aFontID) {
    case LookAndFeel::FontID::Caption:
    case LookAndFeel::FontID::Menu:
      aStyle.size = 13;
      break;
    case LookAndFeel::FontID::SmallCaption:
      aStyle.weight = gfxFontStyle::FontWeight::BOLD;
      // fall-through for font-size
      [[fallthrough]];
    case LookAndFeel::FontID::MessageBox:
    case LookAndFeel::FontID::StatusBar:
      aStyle.size = 11;
      break;
    default:
      aStyle.size = 12;
      break;
  }
#elif defined(XP_WIN)
  // Windows uses Segoe UI for Latin alphabets, but other fonts for some RTL
  // languages, so we fallback to sans-serif to fall back to the user's
  // default sans-serif. Size is 12px for all system fonts (tried in an en-US
  // system).
  aName = u"sans-serif"_ns;
  aStyle.size = 12;
#elif defined(MOZ_WIDGET_ANDROID)
  // Keep consistency with nsLookAndFeel::NativeGetFont.
  aName = u"Roboto"_ns;
  aStyle.size = 12;
#elif defined(MOZ_WIDGET_GTK)
  // On Linux, there is not a default. For example, GNOME on Debian uses
  // Cantarell, 14.667px. Ubuntu Mate uses the Ubuntu font, but also 14.667px.
  // Fedora with KDE uses Noto Sans, 13.3333px, but it uses Noto Sans on
  // GNOME, too.
  // In general, Linux uses some sans-serif, but its size can vary between
  // 12px and 16px. We chose 15px because it is what Firefox is doing for the
  // UI font-size.
  aName = u"sans-serif"_ns;
  aStyle.size = 15;
#else
#  error "Unknown platform"
#endif
}

/* static */
void nsLayoutUtils::ComputeSystemFont(nsFont* aSystemFont,
                                      LookAndFeel::FontID aFontID,
                                      const nsFont& aDefaultVariableFont,
                                      const Document* aDocument) {
  gfxFontStyle fontStyle;
  nsAutoString systemFontName;
  if (aDocument->ShouldResistFingerprinting(
          RFPTarget::FontVisibilityRestrictGenerics)) {
    GetSpoofedSystemFontForRFP(aFontID, fontStyle, systemFontName);
  } else if (!LookAndFeel::GetFont(aFontID, systemFontName, fontStyle)) {
    return;
  }
  systemFontName.Trim("\"'");
  NS_ConvertUTF16toUTF8 nameu8(systemFontName);
  Servo_FontFamily_ForSystemFont(&nameu8, &aSystemFont->family);
  aSystemFont->style = fontStyle.style;
  aSystemFont->family.is_system_font = fontStyle.systemFont;
  aSystemFont->weight = fontStyle.weight;
  aSystemFont->stretch = fontStyle.stretch;
  aSystemFont->size = Length::FromPixels(fontStyle.size);

  // aSystemFont->langGroup = fontStyle.langGroup;

  switch (StyleFontSizeAdjust::Tag(fontStyle.sizeAdjustBasis)) {
    case StyleFontSizeAdjust::Tag::None:
      aSystemFont->sizeAdjust = StyleFontSizeAdjust::None();
      break;
    case StyleFontSizeAdjust::Tag::ExHeight:
      aSystemFont->sizeAdjust =
          StyleFontSizeAdjust::ExHeight(fontStyle.sizeAdjust);
      break;
    case StyleFontSizeAdjust::Tag::CapHeight:
      aSystemFont->sizeAdjust =
          StyleFontSizeAdjust::CapHeight(fontStyle.sizeAdjust);
      break;
    case StyleFontSizeAdjust::Tag::ChWidth:
      aSystemFont->sizeAdjust =
          StyleFontSizeAdjust::ChWidth(fontStyle.sizeAdjust);
      break;
    case StyleFontSizeAdjust::Tag::IcWidth:
      aSystemFont->sizeAdjust =
          StyleFontSizeAdjust::IcWidth(fontStyle.sizeAdjust);
      break;
    case StyleFontSizeAdjust::Tag::IcHeight:
      aSystemFont->sizeAdjust =
          StyleFontSizeAdjust::IcHeight(fontStyle.sizeAdjust);
      break;
  }

  if (aFontID == LookAndFeel::FontID::MozField ||
      aFontID == LookAndFeel::FontID::MozButton ||
      aFontID == LookAndFeel::FontID::MozList) {
    // For textfields, buttons and selects, we use whatever font is defined by
    // the system. Which it appears (and the assumption is) it is always a
    // proportional font. Then we always use 2 points smaller than what the
    // browser has defined as the default proportional font.
    //
    // This matches historical Windows behavior and other browsers.
    auto newSize =
        aDefaultVariableFont.size.ToCSSPixels() - CSSPixel::FromPoints(2.0f);
    aSystemFont->size = Length::FromPixels(std::max(float(newSize), 0.0f));
  }
}

/* static */
bool nsLayoutUtils::ShouldHandleMetaViewport(const Document* aDocument) {
  BrowsingContext* bc = aDocument->GetBrowsingContext();
  return StaticPrefs::dom_meta_viewport_enabled() || (bc && bc->InRDMPane());
}

/* static */
ComputedStyle* nsLayoutUtils::StyleForScrollbar(
    const nsIFrame* aScrollbarPart) {
  // Get the closest content node which is not an anonymous scrollbar
  // part. It should be the originating element of the scrollbar part.
  nsIContent* content = aScrollbarPart->GetContent();
  // Note that the content may be a normal element with scrollbar part
  // value specified for its -moz-appearance, so don't rely on it being
  // a native anonymous. Also note that we have to check the node name
  // because anonymous element like generated content may originate a
  // scrollbar.
  MOZ_ASSERT(content, "No content for the scrollbar part?");
  while (content && content->IsInNativeAnonymousSubtree() &&
         content->IsAnyOfXULElements(
             nsGkAtoms::scrollbar, nsGkAtoms::scrollbarbutton,
             nsGkAtoms::scrollcorner, nsGkAtoms::slider, nsGkAtoms::thumb)) {
    content = content->GetParent();
  }
  MOZ_ASSERT(content, "Native anonymous element with no originating node?");
  // Use the style from the primary frame of the content.
  // Note: it is important to use the primary frame rather than an
  // ancestor frame of the scrollbar part for the correct handling of
  // viewport scrollbar. The content of the scroll frame of the viewport
  // is the root element, but its style inherits from the viewport.
  // Since we need to use the style of root element for the viewport
  // scrollbar, we have to get the style from the primary frame.
  if (nsIFrame* primaryFrame = content->GetPrimaryFrame()) {
    return primaryFrame->Style();
  }
  // If the element doesn't have primary frame, get the computed style
  // from the element directly. This can happen on viewport, because
  // the scrollbar of viewport may be shown when the root element has
  // > display: none; overflow: scroll;
  MOZ_ASSERT(
      content == aScrollbarPart->PresContext()->Document()->GetRootElement(),
      "Root element is the only case for this fallback "
      "path to be triggered");
  RefPtr<ComputedStyle> style =
      ServoStyleSet::ResolveServoStyle(*content->AsElement());
  // Dropping the strong reference is fine because the style should be
  // held strongly by the element.
  return style.get();
}

enum class FramePosition : uint8_t {
  Unknown,
  InView,
  OutOfView,
};

// NOTE: Returns a pair of Nothing() and `FramePosition::Unknown` if |aFrame|
// is not in out-of-process or if we haven't received enough information from
// APZ.
static std::pair<Maybe<ScreenRect>, FramePosition>
GetFrameRectVisibleRectOnScreen(const nsIFrame* aFrame,
                                const nsRect& aFrameRect) {
  // We actually want the in-process top prescontext here.
  nsPresContext* topContextInProcess =
      aFrame->PresContext()->GetInProcessRootContentDocumentPresContext();
  if (!topContextInProcess) {
    // We are in chrome process.
    return std::make_pair(Nothing(), FramePosition::Unknown);
  }

  if (topContextInProcess->Document()->IsTopLevelContentDocument()) {
    // We are in the top of content document.
    return std::make_pair(Nothing(), FramePosition::Unknown);
  }

  nsIDocShell* docShell = topContextInProcess->GetDocShell();
  BrowserChild* browserChild = BrowserChild::GetFrom(docShell);
  if (!browserChild) {
    // We are not in out-of-process iframe.
    return std::make_pair(Nothing(), FramePosition::Unknown);
  }

  if (!browserChild->GetEffectsInfo().IsVisible()) {
    // There is no visible rect on this iframe at all.
    return std::make_pair(Some(ScreenRect()), FramePosition::Unknown);
  }

  Maybe<ScreenRect> visibleRect =
      browserChild->GetTopLevelViewportVisibleRectInBrowserCoords();
  if (!visibleRect) {
    // We are unsure if we haven't received the transformed rectangle of the
    // iframe's visible area.
    return std::make_pair(Nothing(), FramePosition::Unknown);
  }

  nsIFrame* rootFrame = topContextInProcess->PresShell()->GetRootFrame();
  nsRect transformedToIFrame = nsLayoutUtils::TransformFrameRectToAncestor(
      aFrame, aFrameRect, rootFrame);

  LayoutDeviceRect rectInLayoutDevicePixel = LayoutDeviceRect::FromAppUnits(
      transformedToIFrame, topContextInProcess->AppUnitsPerDevPixel());

  ScreenRect transformedToRoot = ViewAs<ScreenPixel>(
      browserChild->GetChildToParentConversionMatrix().TransformBounds(
          rectInLayoutDevicePixel),
      PixelCastJustification::ContentProcessIsLayerInUiProcess);

  FramePosition position = FramePosition::Unknown;
  // we need to check whether the transformed rect is outside the iframe
  // visible rect or not because in some cases the rect size is (0x0), thus
  // the intersection between the transformed rect and the iframe visible rect
  // would also be (0x0), then we can't tell whether the given nsIFrame is
  // inside the iframe visible rect or not by calling BaseRect::IsEmpty for the
  // intersection.
  if (transformedToRoot.x > visibleRect->XMost() ||
      transformedToRoot.y > visibleRect->YMost() ||
      visibleRect->x > transformedToRoot.XMost() ||
      visibleRect->y > transformedToRoot.YMost()) {
    position = FramePosition::OutOfView;
  } else {
    position = FramePosition::InView;
  }

  return std::make_pair(Some(visibleRect->Intersect(transformedToRoot)),
                        position);
}

// static
bool nsLayoutUtils::FrameRectIsScrolledOutOfViewInCrossProcess(
    const nsIFrame* aFrame, const nsRect& aFrameRect) {
  auto [visibleRect, framePosition] =
      GetFrameRectVisibleRectOnScreen(aFrame, aFrameRect);
  if (visibleRect.isNothing()) {
    return false;
  }

  return visibleRect->IsEmpty() && framePosition != FramePosition::InView;
}

// static
bool nsLayoutUtils::FrameIsMostlyScrolledOutOfViewInCrossProcess(
    const nsIFrame* aFrame, nscoord aMargin) {
  auto [visibleRect, framePosition] = GetFrameRectVisibleRectOnScreen(
      aFrame, aFrame->InkOverflowRectRelativeToSelf());
  (void)framePosition;
  if (visibleRect.isNothing()) {
    return false;
  }

  nsPresContext* topContextInProcess =
      aFrame->PresContext()->GetInProcessRootContentDocumentPresContext();
  MOZ_ASSERT(topContextInProcess);

  nsIDocShell* docShell = topContextInProcess->GetDocShell();
  BrowserChild* browserChild = BrowserChild::GetFrom(docShell);
  MOZ_ASSERT(browserChild);

  auto scale =
      browserChild->GetChildToParentConversionMatrix().As2D().ScaleFactors();
  const CSSCoord cssMargin = CSSPixel::FromAppUnits(aMargin);
  ScreenSize margin =
      CSSSize(cssMargin, cssMargin) * ViewAs<CSSToScreenScale2D>(scale);

  return visibleRect->width < margin.width ||
         visibleRect->height < margin.height;
}

// static
nsSize nsLayoutUtils::ExpandHeightForViewportUnits(nsPresContext* aPresContext,
                                                   const nsSize& aSize) {
  nsSize sizeForViewportUnits = aPresContext->GetSizeForViewportUnits();

  // |aSize| might be the size expanded to the minimum-scale size whereas the
  // size for viewport units is not scaled so that we need to expand the |aSize|
  // height by multiplying by the ratio of the viewport units height to the
  // visible area height.
  float vhExpansionRatio = (float)sizeForViewportUnits.height /
                           aPresContext->GetVisibleArea().height;

  MOZ_ASSERT(aSize.height <= NSCoordSaturatingNonnegativeMultiply(
                                 aSize.height, vhExpansionRatio));
  return nsSize(aSize.width, NSCoordSaturatingNonnegativeMultiply(
                                 aSize.height, vhExpansionRatio));
}

template <typename SizeType>
/* static */ SizeType ExpandHeightForDynamicToolbarImpl(
    const nsPresContext* aPresContext, const SizeType& aSize) {
  MOZ_ASSERT(aPresContext);

  // This expansion is applicable only for cases where the software keyboard is
  // hidden or the document is `interactive-widget=resizes-content` mode
  // because in other cases the visual viewport size is always smaller than
  // the layout viewport so that there should be room to scroll.
  if (!aPresContext->IsKeyboardHiddenOrResizesContentMode()) {
    return aSize;
  }

  LayoutDeviceIntSize displaySize;
  if (RefPtr<MobileViewportManager> MVM =
          aPresContext->PresShell()->GetMobileViewportManager()) {
    displaySize = MVM->DisplaySize();
  } else if (!nsLayoutUtils::GetDocumentViewerSize(aPresContext, displaySize)) {
    return aSize;
  }

  float toolbarHeightRatio =
      mozilla::ScreenCoord(aPresContext->GetDynamicToolbarMaxHeight()) /
      mozilla::ViewAs<mozilla::ScreenPixel>(
          displaySize,
          mozilla::PixelCastJustification::LayoutDeviceIsScreenForBounds)
          .height;

  SizeType expandedSize = aSize;
  static_assert(std::is_same_v<nsSize, SizeType> ||
                std::is_same_v<CSSSize, SizeType>);
  if constexpr (std::is_same_v<nsSize, SizeType>) {
    expandedSize.height =
        NSCoordSaturatingAdd(aSize.height, aSize.height * toolbarHeightRatio);
  } else if (std::is_same_v<CSSSize, SizeType>) {
    expandedSize.height = aSize.height + aSize.height * toolbarHeightRatio;
  }
  return expandedSize;
}

CSSSize nsLayoutUtils::ExpandHeightForDynamicToolbar(
    const nsPresContext* aPresContext, const CSSSize& aSize) {
  return ExpandHeightForDynamicToolbarImpl(aPresContext, aSize);
}
nsSize nsLayoutUtils::ExpandHeightForDynamicToolbar(
    const nsPresContext* aPresContext, const nsSize& aSize) {
  return ExpandHeightForDynamicToolbarImpl(aPresContext, aSize);
}
