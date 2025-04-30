/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AnimationInfo.h"
#include "mozilla/LayerAnimationInfo.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/layers/AnimationHelper.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/dom/Animation.h"
#include "mozilla/dom/CSSTransition.h"
#include "mozilla/dom/KeyframeEffect.h"
#include "mozilla/EffectSet.h"
#include "mozilla/MotionPathUtils.h"
#include "mozilla/PresShell.h"
#include "mozilla/ScrollContainerFrame.h"
#include "nsIContent.h"
#include "nsLayoutUtils.h"
#include "nsRefreshDriver.h"
#include "nsStyleTransformMatrix.h"
#include "PuppetWidget.h"

namespace mozilla::layers {

using TransformReferenceBox = nsStyleTransformMatrix::TransformReferenceBox;

AnimationInfo::AnimationInfo() : mCompositorAnimationsId(0), mMutated(false) {}

AnimationInfo::~AnimationInfo() = default;

void AnimationInfo::EnsureAnimationsId() {
  if (!mCompositorAnimationsId) {
    mCompositorAnimationsId = AnimationHelper::GetNextCompositorAnimationsId();
  }
}

Animation* AnimationInfo::AddAnimation() {
  MOZ_ASSERT(!CompositorThreadHolder::IsInCompositorThread());
  // Here generates a new id when the first animation is added and
  // this id is used to represent the animations in this layer.
  EnsureAnimationsId();

  MOZ_ASSERT(!mPendingAnimations, "should have called ClearAnimations first");

  Animation* anim = mAnimations.AppendElement();

  mMutated = true;

  return anim;
}

Animation* AnimationInfo::AddAnimationForNextTransaction() {
  MOZ_ASSERT(!CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(mPendingAnimations,
             "should have called ClearAnimationsForNextTransaction first");

  Animation* anim = mPendingAnimations->AppendElement();

  return anim;
}

void AnimationInfo::ClearAnimations() {
  mPendingAnimations = nullptr;

  if (mAnimations.IsEmpty() && mStorageData.IsEmpty()) {
    return;
  }

  mAnimations.Clear();
  mStorageData.Clear();

  mMutated = true;
}

void AnimationInfo::ClearAnimationsForNextTransaction() {
  // Ensure we have a non-null mPendingAnimations to mark a future clear.
  if (!mPendingAnimations) {
    mPendingAnimations = MakeUnique<AnimationArray>();
  }

  mPendingAnimations->Clear();
}

void AnimationInfo::MaybeStartPendingAnimation(Animation& aAnimation,
                                               const TimeStamp& aReadyTime) {
  // If the animation is doing an async update of its playback rate, then we
  // want to match whatever its current time would be at *aReadyTime*.
  if (!std::isnan(aAnimation.previousPlaybackRate()) &&
      aAnimation.startTime().isSome() && !aAnimation.originTime().IsNull() &&
      !aAnimation.isNotPlaying()) {
    TimeDuration readyTime = aReadyTime - aAnimation.originTime();
    aAnimation.holdTime() = dom::Animation::CurrentTimeFromTimelineTime(
        readyTime, aAnimation.startTime().ref(),
        aAnimation.previousPlaybackRate());
    // Make start time null so that we know to update it below.
    aAnimation.startTime() = Nothing();
  }

  // If the aAnimationation is play-pending, resolve the start time.
  if (aAnimation.startTime().isNothing() && !aAnimation.originTime().IsNull() &&
      !aAnimation.isNotPlaying()) {
    const TimeDuration readyTime = aReadyTime - aAnimation.originTime();
    aAnimation.startTime() = Some(dom::Animation::StartTimeFromTimelineTime(
        readyTime, aAnimation.holdTime(), aAnimation.playbackRate()));
  }
}

bool AnimationInfo::ApplyPendingUpdatesForThisTransaction() {
  if (mPendingAnimations) {
    mAnimations = std::move(*mPendingAnimations);
    mPendingAnimations = nullptr;
    return true;
  }

  return false;
}

bool AnimationInfo::HasTransformAnimation() const {
  const nsCSSPropertyIDSet& transformSet =
      LayerAnimationInfo::GetCSSPropertiesFor(DisplayItemType::TYPE_TRANSFORM);
  for (const auto& animation : mAnimations) {
    if (transformSet.HasProperty(animation.property())) {
      return true;
    }
  }
  return false;
}

/* static */
Maybe<uint64_t> AnimationInfo::GetGenerationFromFrame(
    nsIFrame* aFrame, DisplayItemType aDisplayItemKey) {
  MOZ_ASSERT(aFrame->IsPrimaryFrame() ||
             nsLayoutUtils::IsFirstContinuationOrIBSplitSibling(aFrame));

  // In case of continuation, KeyframeEffectReadOnly uses its first frame,
  // whereas nsDisplayItem uses its last continuation, so we have to use the
  // last continuation frame here.
  if (nsLayoutUtils::IsFirstContinuationOrIBSplitSibling(aFrame)) {
    aFrame = nsLayoutUtils::LastContinuationOrIBSplitSibling(aFrame);
  }
  RefPtr<WebRenderAnimationData> animationData =
      GetWebRenderUserData<WebRenderAnimationData>(aFrame,
                                                   (uint32_t)aDisplayItemKey);
  if (animationData) {
    return animationData->GetAnimationInfo().GetAnimationGeneration();
  }

  return Nothing();
}

/* static */
void AnimationInfo::EnumerateGenerationOnFrame(
    const nsIFrame* aFrame, const nsIContent* aContent,
    const CompositorAnimatableDisplayItemTypes& aDisplayItemTypes,
    AnimationGenerationCallback aCallback) {
  nsIWidget* widget = nsContentUtils::WidgetForContent(aContent);
  if (!widget) {
    return;
  }
  // If we haven't created a window renderer there's no animation generation
  // that we can have, thus we call the callback function with |Nothing()| for
  // the generation.
  if (!widget->HasWindowRenderer()) {
    for (auto displayItem : LayerAnimationInfo::sDisplayItemTypes) {
      aCallback(Nothing(), displayItem);
    }
    return;
  }
  WindowRenderer* renderer = widget->GetWindowRenderer();
  MOZ_ASSERT(renderer);
  if (!renderer->AsWebRender()) {
    return;
  }

  // In case of continuation, nsDisplayItem uses its last continuation, so we
  // have to use the last continuation frame here.
  if (nsLayoutUtils::IsFirstContinuationOrIBSplitSibling(aFrame)) {
    aFrame = nsLayoutUtils::LastContinuationOrIBSplitSibling(aFrame);
  }

  for (auto displayItem : LayerAnimationInfo::sDisplayItemTypes) {
    // For transform animations, the animation is on the primary frame but
    // |aFrame| is the style frame.
    const nsIFrame* frameToQuery =
        displayItem == DisplayItemType::TYPE_TRANSFORM
            ? nsLayoutUtils::GetPrimaryFrameFromStyleFrame(aFrame)
            : aFrame;
    RefPtr<WebRenderAnimationData> animationData =
        GetWebRenderUserData<WebRenderAnimationData>(frameToQuery,
                                                     (uint32_t)displayItem);
    Maybe<uint64_t> generation;
    if (animationData) {
      generation = animationData->GetAnimationInfo().GetAnimationGeneration();
    }
    aCallback(generation, displayItem);
  }
}

static StyleTransformOperation ResolveTranslate(
    TransformReferenceBox& aRefBox, const LengthPercentage& aX,
    const LengthPercentage& aY = LengthPercentage::Zero(),
    const Length& aZ = Length{0}) {
  float x = nsStyleTransformMatrix::ProcessTranslatePart(
      aX, &aRefBox, &TransformReferenceBox::Width);
  float y = nsStyleTransformMatrix::ProcessTranslatePart(
      aY, &aRefBox, &TransformReferenceBox::Height);
  return StyleTransformOperation::Translate3D(
      LengthPercentage::FromPixels(x), LengthPercentage::FromPixels(y), aZ);
}

static StyleTranslate ResolveTranslate(const StyleTranslate& aValue,
                                       TransformReferenceBox& aRefBox) {
  if (aValue.IsTranslate()) {
    const auto& t = aValue.AsTranslate();
    float x = nsStyleTransformMatrix::ProcessTranslatePart(
        t._0, &aRefBox, &TransformReferenceBox::Width);
    float y = nsStyleTransformMatrix::ProcessTranslatePart(
        t._1, &aRefBox, &TransformReferenceBox::Height);
    return StyleTranslate::Translate(LengthPercentage::FromPixels(x),
                                     LengthPercentage::FromPixels(y), t._2);
  }

  MOZ_ASSERT(aValue.IsNone());
  return StyleTranslate::None();
}

static StyleTransform ResolveTransformOperations(
    const StyleTransform& aTransform, TransformReferenceBox& aRefBox) {
  auto convertMatrix = [](const gfx::Matrix4x4& aM) {
    return StyleTransformOperation::Matrix3D(StyleGenericMatrix3D<StyleNumber>{
        aM._11, aM._12, aM._13, aM._14, aM._21, aM._22, aM._23, aM._24, aM._31,
        aM._32, aM._33, aM._34, aM._41, aM._42, aM._43, aM._44});
  };

  Vector<StyleTransformOperation> result;
  MOZ_RELEASE_ASSERT(
      result.initCapacity(aTransform.Operations().Length()),
      "Allocating vector of transform operations should be successful.");

  for (const StyleTransformOperation& op : aTransform.Operations()) {
    switch (op.tag) {
      case StyleTransformOperation::Tag::TranslateX:
        result.infallibleAppend(ResolveTranslate(aRefBox, op.AsTranslateX()));
        break;
      case StyleTransformOperation::Tag::TranslateY:
        result.infallibleAppend(ResolveTranslate(
            aRefBox, LengthPercentage::Zero(), op.AsTranslateY()));
        break;
      case StyleTransformOperation::Tag::TranslateZ:
        result.infallibleAppend(
            ResolveTranslate(aRefBox, LengthPercentage::Zero(),
                             LengthPercentage::Zero(), op.AsTranslateZ()));
        break;
      case StyleTransformOperation::Tag::Translate: {
        const auto& translate = op.AsTranslate();
        result.infallibleAppend(
            ResolveTranslate(aRefBox, translate._0, translate._1));
        break;
      }
      case StyleTransformOperation::Tag::Translate3D: {
        const auto& translate = op.AsTranslate3D();
        result.infallibleAppend(ResolveTranslate(aRefBox, translate._0,
                                                 translate._1, translate._2));
        break;
      }
      case StyleTransformOperation::Tag::InterpolateMatrix: {
        gfx::Matrix4x4 matrix;
        nsStyleTransformMatrix::ProcessInterpolateMatrix(matrix, op, aRefBox);
        result.infallibleAppend(convertMatrix(matrix));
        break;
      }
      case StyleTransformOperation::Tag::AccumulateMatrix: {
        gfx::Matrix4x4 matrix;
        nsStyleTransformMatrix::ProcessAccumulateMatrix(matrix, op, aRefBox);
        result.infallibleAppend(convertMatrix(matrix));
        break;
      }
      case StyleTransformOperation::Tag::RotateX:
      case StyleTransformOperation::Tag::RotateY:
      case StyleTransformOperation::Tag::RotateZ:
      case StyleTransformOperation::Tag::Rotate:
      case StyleTransformOperation::Tag::Rotate3D:
      case StyleTransformOperation::Tag::ScaleX:
      case StyleTransformOperation::Tag::ScaleY:
      case StyleTransformOperation::Tag::ScaleZ:
      case StyleTransformOperation::Tag::Scale:
      case StyleTransformOperation::Tag::Scale3D:
      case StyleTransformOperation::Tag::SkewX:
      case StyleTransformOperation::Tag::SkewY:
      case StyleTransformOperation::Tag::Skew:
      case StyleTransformOperation::Tag::Matrix:
      case StyleTransformOperation::Tag::Matrix3D:
      case StyleTransformOperation::Tag::Perspective:
        result.infallibleAppend(op);
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Function not handled yet!");
    }
  }

  auto transform = StyleTransform{
      StyleOwnedSlice<StyleTransformOperation>(std::move(result))};
  MOZ_ASSERT(!transform.HasPercent());
  MOZ_ASSERT(transform.Operations().Length() ==
             aTransform.Operations().Length());
  return transform;
}

static Maybe<ScrollTimelineOptions> GetScrollTimelineOptions(
    dom::AnimationTimeline* aTimeline) {
  if (!aTimeline || !aTimeline->IsScrollTimeline()) {
    return Nothing();
  }

  const dom::ScrollTimeline* timeline = aTimeline->AsScrollTimeline();
  MOZ_ASSERT(timeline->IsActive(),
             "We send scroll animation to the compositor only if its timeline "
             "is active");

  ScrollableLayerGuid::ViewID source = ScrollableLayerGuid::NULL_SCROLL_ID;
  DebugOnly<bool> success =
      nsLayoutUtils::FindIDFor(timeline->SourceElement(), &source);
  MOZ_ASSERT(success, "We should have a valid ViewID for the scroller");

  return Some(ScrollTimelineOptions(source, timeline->Axis()));
}

static void SetAnimatable(nsCSSPropertyID aProperty,
                          const AnimationValue& aAnimationValue,
                          nsIFrame* aFrame, TransformReferenceBox& aRefBox,
                          layers::Animatable& aAnimatable) {
  MOZ_ASSERT(aFrame);

  if (aAnimationValue.IsNull()) {
    aAnimatable = null_t();
    return;
  }

  switch (aProperty) {
    case eCSSProperty_background_color: {
      // We don't support color animation on the compositor yet so that we can
      // resolve currentColor at this moment.
      nscolor foreground =
          aFrame->Style()->GetVisitedDependentColor(&nsStyleText::mColor);
      aAnimatable = aAnimationValue.GetColor(foreground);
      break;
    }
    case eCSSProperty_opacity:
      aAnimatable = aAnimationValue.GetOpacity();
      break;
    case eCSSProperty_rotate:
      aAnimatable = aAnimationValue.GetRotateProperty();
      break;
    case eCSSProperty_scale:
      aAnimatable = aAnimationValue.GetScaleProperty();
      break;
    case eCSSProperty_translate:
      aAnimatable =
          ResolveTranslate(aAnimationValue.GetTranslateProperty(), aRefBox);
      break;
    case eCSSProperty_transform:
      aAnimatable = ResolveTransformOperations(
          aAnimationValue.GetTransformProperty(), aRefBox);
      break;
    case eCSSProperty_offset_path:
      aAnimatable = StyleOffsetPath::None();
      aAnimationValue.GetOffsetPathProperty(aAnimatable.get_StyleOffsetPath());
      break;
    case eCSSProperty_offset_distance:
      aAnimatable = aAnimationValue.GetOffsetDistanceProperty();
      break;
    case eCSSProperty_offset_rotate:
      aAnimatable = aAnimationValue.GetOffsetRotateProperty();
      break;
    case eCSSProperty_offset_anchor:
      aAnimatable = aAnimationValue.GetOffsetAnchorProperty();
      break;
    case eCSSProperty_offset_position:
      aAnimatable = aAnimationValue.GetOffsetPositionProperty();
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unsupported property");
  }
}

void AnimationInfo::AddAnimationForProperty(
    nsIFrame* aFrame, const AnimationProperty& aProperty,
    dom::Animation* aAnimation, const Maybe<TransformData>& aTransformData,
    Send aSendFlag) {
  MOZ_ASSERT(aAnimation->GetEffect(),
             "Should not be adding an animation without an effect");
  MOZ_ASSERT(!aAnimation->GetStartTime().IsNull() || !aAnimation->IsPlaying() ||
                 (aAnimation->GetTimeline() &&
                  aAnimation->GetTimeline()->TracksWallclockTime()),
             "If the animation has an unresolved start time it should either"
             " be static (so we don't need a start time) or else have a"
             " timeline capable of converting TimeStamps (so we can calculate"
             " one later");

  Animation* animation = (aSendFlag == Send::NextTransaction)
                             ? AddAnimationForNextTransaction()
                             : AddAnimation();

  const TimingParams& timing = aAnimation->GetEffect()->NormalizedTiming();

  // If we are starting a new transition that replaces an existing transition
  // running on the compositor, it is possible that the animation on the
  // compositor will have advanced ahead of the main thread. If we use as
  // the starting point of the new transition, the current value of the
  // replaced transition as calculated on the main thread using the refresh
  // driver time, the new transition will jump when it starts. Instead, we
  // re-calculate the starting point of the new transition by applying the
  // current TimeStamp to the parameters of the replaced transition.
  //
  // We need to do this here, rather than when we generate the new transition,
  // since after generating the new transition other requestAnimationFrame
  // callbacks may run that introduce further lag between the main thread and
  // the compositor.
  //
  // Note that we will replace the start value with the last sampled animation
  // value on the compositor.
  // The computation here is for updating the keyframe values, to make sure the
  // computed values on the main thread don't behind the rendering result on the
  // compositor too much.
  bool needReplaceTransition = false;
  if (dom::CSSTransition* cssTransition = aAnimation->AsCSSTransition()) {
    needReplaceTransition =
        cssTransition->UpdateStartValueFromReplacedTransition();
  }

  animation->originTime() =
      !aAnimation->GetTimeline()
          ? TimeStamp()
          : aAnimation->GetTimeline()->ToTimeStamp(TimeDuration());

  dom::Nullable<TimeDuration> startTime = aAnimation->GetStartTime();
  if (startTime.IsNull()) {
    animation->startTime() = Nothing();
  } else {
    animation->startTime() = Some(startTime.Value());
  }

  animation->holdTime() = aAnimation->GetCurrentTimeAsDuration().Value();

  const ComputedTiming computedTiming =
      aAnimation->GetEffect()->GetComputedTiming();
  animation->delay() = timing.Delay();
  animation->endDelay() = timing.EndDelay();
  animation->duration() = computedTiming.mDuration;
  animation->iterations() = static_cast<float>(computedTiming.mIterations);
  animation->iterationStart() =
      static_cast<float>(computedTiming.mIterationStart);
  animation->direction() = static_cast<uint8_t>(timing.Direction());
  animation->fillMode() = static_cast<uint8_t>(computedTiming.mFill);
  MOZ_ASSERT(!aProperty.mProperty.IsCustom(),
             "We don't animate custom properties in the compositor");
  animation->property() = aProperty.mProperty.mID;
  animation->playbackRate() =
      static_cast<float>(aAnimation->CurrentOrPendingPlaybackRate());
  animation->previousPlaybackRate() =
      aAnimation->HasPendingPlaybackRate()
          ? static_cast<float>(aAnimation->PlaybackRate())
          : std::numeric_limits<float>::quiet_NaN();
  animation->transformData() = aTransformData;
  animation->easingFunction() = timing.TimingFunction();
  animation->iterationComposite() = static_cast<uint8_t>(
      aAnimation->GetEffect()->AsKeyframeEffect()->IterationComposite());
  animation->isNotPlaying() = !aAnimation->IsPlaying();
  animation->isNotAnimating() = false;
  animation->scrollTimelineOptions() =
      GetScrollTimelineOptions(aAnimation->GetTimeline());
  // We set this flag to let the compositor know that the start value of this
  // transition is replaced. The compositor may replace the start value with its
  // last sampled animation value, instead of using the segment.mFromValue we
  // send to the compositor, to avoid any potential lag.
  animation->replacedTransitionId() =
      needReplaceTransition ? Some(GetCompositorAnimationsId()) : Nothing();

  TransformReferenceBox refBox(aFrame);

  // If the animation is additive or accumulates, we need to pass its base value
  // to the compositor.

  AnimationValue baseStyle =
      aAnimation->GetEffect()->AsKeyframeEffect()->BaseStyle(
          aProperty.mProperty);
  if (!baseStyle.IsNull()) {
    SetAnimatable(aProperty.mProperty.mID, baseStyle, aFrame, refBox,
                  animation->baseStyle());
  } else {
    animation->baseStyle() = null_t();
  }

  for (const AnimationPropertySegment& segment : aProperty.mSegments) {
    AnimationSegment* animSegment = animation->segments().AppendElement();
    SetAnimatable(aProperty.mProperty.mID, segment.mFromValue, aFrame, refBox,
                  animSegment->startState());
    SetAnimatable(aProperty.mProperty.mID, segment.mToValue, aFrame, refBox,
                  animSegment->endState());

    animSegment->startPortion() = segment.mFromKey;
    animSegment->endPortion() = segment.mToKey;
    animSegment->startComposite() =
        static_cast<uint8_t>(segment.mFromComposite);
    animSegment->endComposite() = static_cast<uint8_t>(segment.mToComposite);
    animSegment->sampleFn() = segment.mTimingFunction;
  }

  if (aAnimation->Pending()) {
    TimeStamp readyTime = aAnimation->GetPendingReadyTime();
    if (readyTime.IsNull()) {
      // TODO(emilio): This should generally not happen anymore, can we remove
      // this SetPendingReadyTime call?
      readyTime = aFrame->PresContext()->RefreshDriver()->MostRecentRefresh();
      MOZ_ASSERT(!readyTime.IsNull());
      aAnimation->SetPendingReadyTime(readyTime);
    }
    MaybeStartPendingAnimation(*animation, readyTime);
  }
}

// Let's use an example to explain this function:
//
// We have 4 playing animations (without any !important rule or transition):
// Animation A: [ transform, rotate ].
// Animation B: [ rotate, scale ].
// Animation C: [ transform, margin-left ].
// Animation D: [ opacity, margin-left ].
//
// Normally, GetAnimationsForCompositor(|transform-like properties|) returns:
// [ Animation A, Animation B, Animation C ], which is the first argument of
// this function.
//
// In this function, we want to re-organize the list as (Note: don't care
// the order of properties):
// [
//   { rotate:    [ Animation A, Animation B ] },
//   { scale:     [ Animation B ] },
//   { transform: [ Animation A, Animation C ] },
// ]
//
// Therefore, AddAnimationsForProperty() will append each animation property
// into AnimationInfo,  as a final list of layers::Animation:
// [
//   { rotate: Animation A },
//   { rotate: Animation B },
//   { scale: Animation B },
//   { transform: Animation A },
//   { transform: Animation C },
// ]
//
// And then, for each transaction, we send this list to the compositor thread.
static HashMap<nsCSSPropertyID, nsTArray<RefPtr<dom::Animation>>>
GroupAnimationsByProperty(const nsTArray<RefPtr<dom::Animation>>& aAnimations,
                          const nsCSSPropertyIDSet& aPropertySet) {
  HashMap<nsCSSPropertyID, nsTArray<RefPtr<dom::Animation>>> groupedAnims;
  for (const RefPtr<dom::Animation>& anim : aAnimations) {
    const dom::KeyframeEffect* effect = anim->GetEffect()->AsKeyframeEffect();
    MOZ_ASSERT(effect);
    for (const AnimationProperty& property : effect->Properties()) {
      // TODO(zrhoffman, bug 1869475): Handle custom properties
      if (!aPropertySet.HasProperty(property.mProperty)) {
        continue;
      }

      auto animsForPropertyPtr =
          groupedAnims.lookupForAdd(property.mProperty.mID);
      if (!animsForPropertyPtr) {
        DebugOnly<bool> rv =
            groupedAnims.add(animsForPropertyPtr, property.mProperty.mID,
                             nsTArray<RefPtr<dom::Animation>>());
        MOZ_ASSERT(rv, "Should have enough memory");
      }
      animsForPropertyPtr->value().AppendElement(anim);
    }
  }
  return groupedAnims;
}

bool AnimationInfo::AddAnimationsForProperty(
    nsIFrame* aFrame, const EffectSet* aEffects,
    const nsTArray<RefPtr<dom::Animation>>& aCompositorAnimations,
    const Maybe<TransformData>& aTransformData, nsCSSPropertyID aProperty,
    Send aSendFlag, WebRenderLayerManager* aLayerManager) {
  bool addedAny = false;
  // Add from first to last (since last overrides)
  for (dom::Animation* anim : aCompositorAnimations) {
    if (!anim->IsRelevant()) {
      continue;
    }

    MOZ_ASSERT(anim->GetEffect() && anim->GetEffect()->AsKeyframeEffect(),
               "A playing animation should have a keyframe effect");
    dom::KeyframeEffect* keyframeEffect = anim->GetEffect()->AsKeyframeEffect();
    const AnimationProperty* property =
        keyframeEffect->GetEffectiveAnimationOfProperty(
            AnimatedPropertyID(aProperty), *aEffects);
    if (!property) {
      continue;
    }

    // Note that if the property is overridden by !important rules,
    // GetEffectiveAnimationOfProperty returns null instead.
    // This is what we want, since if we have animations overridden by
    // !important rules, we don't want to send them to the compositor.
    MOZ_ASSERT(
        anim->CascadeLevel() != EffectCompositor::CascadeLevel::Animations ||
            !aEffects->PropertiesWithImportantRules().HasProperty(aProperty),
        "GetEffectiveAnimationOfProperty already tested the property "
        "is not overridden by !important rules");

    // Don't add animations that are pending if their timeline does not track
    // wallclock time. This is because any pending animations on layers will
    // have their start time updated with the current wallclock time.
    //
    // If we can't convert that wallclock time back to an equivalent timeline
    // time, we won't be able to update the content animation and it will end
    // up being out of sync with the layer animation.
    //
    // Currently this only happens when the timeline is driven by a refresh
    // driver under test control. In this case, the next time the refresh
    // driver is advanced it will trigger any pending animations.
    if (anim->Pending() && anim->GetTimeline() &&
        !anim->GetTimeline()->TracksWallclockTime()) {
      continue;
    }

    AddAnimationForProperty(aFrame, *property, anim, aTransformData, aSendFlag);
    keyframeEffect->SetIsRunningOnCompositor(aProperty, true);
    addedAny = true;
    if (aTransformData && aTransformData->partialPrerenderData() &&
        aLayerManager) {
      aLayerManager->AddPartialPrerenderedAnimation(GetCompositorAnimationsId(),
                                                    anim);
    }
  }
  return addedAny;
}

// Returns which pre-rendered area's sides are overflowed from the pre-rendered
// rect.
//
// We don't need to make jank animations when we are going to composite the
// area where there is no overflowed area even if it's outside of the
// pre-rendered area.
static SideBits GetOverflowedSides(const nsRect& aOverflow,
                                   const nsRect& aPartialPrerenderArea) {
  SideBits sides = SideBits::eNone;
  if (aOverflow.X() < aPartialPrerenderArea.X()) {
    sides |= SideBits::eLeft;
  }
  if (aOverflow.Y() < aPartialPrerenderArea.Y()) {
    sides |= SideBits::eTop;
  }
  if (aOverflow.XMost() > aPartialPrerenderArea.XMost()) {
    sides |= SideBits::eRight;
  }
  if (aOverflow.YMost() > aPartialPrerenderArea.YMost()) {
    sides |= SideBits::eBottom;
  }
  return sides;
}

static std::pair<ParentLayerRect, gfx::Matrix4x4>
GetClipRectAndTransformForPartialPrerender(
    const nsIFrame* aFrame, int32_t aDevPixelsToAppUnits,
    const nsIFrame* aClipFrame,
    const ScrollContainerFrame* aScrollContainerFrame) {
  MOZ_ASSERT(aClipFrame);

  gfx::Matrix4x4 transformInClip =
      nsLayoutUtils::GetTransformToAncestor(RelativeTo{aFrame->GetParent()},
                                            RelativeTo{aClipFrame})
          .GetMatrix();
  if (aScrollContainerFrame) {
    transformInClip.PostTranslate(
        LayoutDevicePoint::FromAppUnits(
            aScrollContainerFrame->GetScrollPosition(), aDevPixelsToAppUnits)
            .ToUnknownPoint());
  }

  // We don't necessarily use nsLayoutUtils::CalculateCompositionSizeForFrame
  // since this is a case where we don't use APZ at all.
  return std::make_pair(
      LayoutDeviceRect::FromAppUnits(
          aScrollContainerFrame ? aScrollContainerFrame->GetScrollPortRect()
                                : aClipFrame->GetRectRelativeToSelf(),
          aDevPixelsToAppUnits) *
          LayoutDeviceToLayerScale2D() * LayerToParentLayerScale(),
      transformInClip);
}

static PartialPrerenderData GetPartialPrerenderData(
    const nsIFrame* aFrame, const nsDisplayItem* aItem) {
  const nsRect& partialPrerenderedRect = aItem->GetUntransformedPaintRect();
  nsRect overflow = aFrame->InkOverflowRectRelativeToSelf();

  ScrollableLayerGuid::ViewID scrollId = ScrollableLayerGuid::NULL_SCROLL_ID;

  const nsIFrame* clipFrame =
      nsLayoutUtils::GetNearestOverflowClipFrame(aFrame->GetParent());
  const ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(clipFrame);

  if (!clipFrame) {
    // If there is no suitable clip frame in the same document, use the
    // root one.
    scrollContainerFrame = aFrame->PresShell()->GetRootScrollContainerFrame();
    if (scrollContainerFrame) {
      clipFrame = scrollContainerFrame;
    } else {
      // If there is no root scroll frame, use the viewport frame.
      clipFrame = aFrame->PresShell()->GetRootFrame();
    }
  }

  // If the scroll frame is asyncronously scrollable, try to find the scroll id.
  if (scrollContainerFrame &&
      !scrollContainerFrame->GetScrollStyles().IsHiddenInBothDirections() &&
      nsLayoutUtils::AsyncPanZoomEnabled(aFrame)) {
    const bool isInPositionFixed =
        nsLayoutUtils::IsInPositionFixedSubtree(aFrame);
    const ActiveScrolledRoot* asr = aItem->GetActiveScrolledRoot();
    if (!isInPositionFixed && asr &&
        aFrame->PresContext() == asr->mScrollContainerFrame->PresContext()) {
      scrollId = asr->GetViewId();
      MOZ_ASSERT(clipFrame == asr->mScrollContainerFrame);
    } else {
      // Use the root scroll id in the same document if the target frame is in
      // position:fixed subtree or there is no ASR or the ASR is in a different
      // ancestor document.
      scrollId =
          nsLayoutUtils::ScrollIdForRootScrollFrame(aFrame->PresContext());
      MOZ_ASSERT(clipFrame ==
                 aFrame->PresShell()->GetRootScrollContainerFrame());
    }
  }

  int32_t devPixelsToAppUnits = aFrame->PresContext()->AppUnitsPerDevPixel();

  auto [clipRect, transformInClip] = GetClipRectAndTransformForPartialPrerender(
      aFrame, devPixelsToAppUnits, clipFrame, scrollContainerFrame);

  return PartialPrerenderData{
      LayoutDeviceRect::FromAppUnits(partialPrerenderedRect,
                                     devPixelsToAppUnits),
      GetOverflowedSides(overflow, partialPrerenderedRect),
      scrollId,
      clipRect,
      transformInClip,
      LayoutDevicePoint()};  // will be set by caller.
}

enum class AnimationDataType {
  WithMotionPath,
  WithoutMotionPath,
};
static Maybe<TransformData> CreateAnimationData(
    nsIFrame* aFrame, nsDisplayItem* aItem, DisplayItemType aType,
    layers::LayersBackend aLayersBackend, AnimationDataType aDataType,
    const Maybe<LayoutDevicePoint>& aPosition) {
  if (aType != DisplayItemType::TYPE_TRANSFORM) {
    return Nothing();
  }

  // XXX Performance here isn't ideal for SVG. We'd prefer to avoid resolving
  // the dimensions of refBox. That said, we only get here if there are CSS
  // animations or transitions on this element, and that is likely to be a
  // lot rarer than transforms on SVG (the frequency of which drives the need
  // for TransformReferenceBox).
  TransformReferenceBox refBox(aFrame);
  const nsRect bounds(0, 0, refBox.Width(), refBox.Height());

  // all data passed directly to the compositor should be in dev pixels
  int32_t devPixelsToAppUnits = aFrame->PresContext()->AppUnitsPerDevPixel();
  float scale = devPixelsToAppUnits;
  gfx::Point3D offsetToTransformOrigin =
      nsDisplayTransform::GetDeltaToTransformOrigin(aFrame, refBox, scale);
  nsPoint origin;
  if (aLayersBackend == layers::LayersBackend::LAYERS_WR) {
    // leave origin empty, because we are sending it separately on the
    // stacking context that we are pushing to WR, and WR will automatically
    // include it when picking up the animated transform values
  } else if (aItem) {
    // This branch is for display items to leverage the cache of
    // nsDisplayListBuilder.
    origin = aItem->ToReferenceFrame();
  } else {
    // This branch is running for restyling.
    // Animations are animated at the coordination of the reference
    // frame outside, not the given frame itself.  The given frame
    // is also reference frame too, so the parent's reference frame
    // are used.
    nsIFrame* referenceFrame = nsLayoutUtils::GetReferenceFrame(
        nsLayoutUtils::GetCrossDocParentFrameInProcess(aFrame));
    origin = aFrame->GetOffsetToCrossDoc(referenceFrame);
  }

  Maybe<MotionPathData> motionPathData;
  if (aDataType == AnimationDataType::WithMotionPath) {
    const StyleTransformOrigin& styleOrigin =
        aFrame->StyleDisplay()->mTransformOrigin;
    CSSPoint motionPathOrigin = nsStyleTransformMatrix::Convert2DPosition(
        styleOrigin.horizontal, styleOrigin.vertical, refBox);
    CSSPoint anchorAdjustment =
        MotionPathUtils::ComputeAnchorPointAdjustment(*aFrame);
    // Note: If there is no containing block or coord-box is empty, we still
    // pass it to the compositor. Just render them as no path on the compositor
    // thread.
    nsRect coordBox;
    const nsIFrame* containingBlockFrame =
        MotionPathUtils::GetOffsetPathReferenceBox(aFrame, coordBox);
    nsTArray<nscoord> radii;
    if (containingBlockFrame) {
      radii = MotionPathUtils::ComputeBorderRadii(
          containingBlockFrame->StyleBorder()->mBorderRadius, coordBox);
    }
    motionPathData.emplace(
        std::move(motionPathOrigin), std::move(anchorAdjustment),
        std::move(coordBox),
        containingBlockFrame ? aFrame->GetOffsetTo(containingBlockFrame)
                             : aFrame->GetPosition(),
        MotionPathUtils::GetRayContainReferenceSize(aFrame), std::move(radii));
  }

  Maybe<PartialPrerenderData> partialPrerenderData;
  if (aItem && static_cast<nsDisplayTransform*>(aItem)->IsPartialPrerender()) {
    partialPrerenderData = Some(GetPartialPrerenderData(aFrame, aItem));

    if (aLayersBackend == layers::LayersBackend::LAYERS_WR) {
      MOZ_ASSERT(aPosition);
      partialPrerenderData->position() = *aPosition;
    }
  }

  return Some(TransformData(origin, offsetToTransformOrigin, bounds,
                            devPixelsToAppUnits, motionPathData,
                            partialPrerenderData));
}

void AnimationInfo::AddNonAnimatingTransformLikePropertiesStyles(
    const nsCSSPropertyIDSet& aNonAnimatingProperties, nsIFrame* aFrame,
    Send aSendFlag) {
  auto appendFakeAnimation = [this, aSendFlag](nsCSSPropertyID aProperty,
                                               Animatable&& aBaseStyle) {
    layers::Animation* animation = (aSendFlag == Send::NextTransaction)
                                       ? AddAnimationForNextTransaction()
                                       : AddAnimation();
    animation->property() = aProperty;
    animation->baseStyle() = std::move(aBaseStyle);
    animation->easingFunction() = Nothing();
    animation->isNotAnimating() = true;
  };

  const nsStyleDisplay* display = aFrame->StyleDisplay();
  // A simple optimization. We don't need to send offset-* properties if we
  // don't have offset-path and offset-position.
  bool hasMotion =
      !display->mOffsetPath.IsNone() ||
      !aNonAnimatingProperties.HasProperty(eCSSProperty_offset_path);

  for (nsCSSPropertyID id : aNonAnimatingProperties) {
    switch (id) {
      case eCSSProperty_transform:
        if (!display->mTransform.IsNone()) {
          TransformReferenceBox refBox(aFrame);
          appendFakeAnimation(
              id, ResolveTransformOperations(display->mTransform, refBox));
        }
        break;
      case eCSSProperty_translate:
        if (!display->mTranslate.IsNone()) {
          TransformReferenceBox refBox(aFrame);
          appendFakeAnimation(id,
                              ResolveTranslate(display->mTranslate, refBox));
        }
        break;
      case eCSSProperty_rotate:
        if (!display->mRotate.IsNone()) {
          appendFakeAnimation(id, display->mRotate);
        }
        break;
      case eCSSProperty_scale:
        if (!display->mScale.IsNone()) {
          appendFakeAnimation(id, display->mScale);
        }
        break;
      case eCSSProperty_offset_path:
        if (!display->mOffsetPath.IsNone()) {
          appendFakeAnimation(id, display->mOffsetPath);
        }
        break;
      case eCSSProperty_offset_distance:
        if (hasMotion && !display->mOffsetDistance.IsDefinitelyZero()) {
          appendFakeAnimation(id, display->mOffsetDistance);
        }
        break;
      case eCSSProperty_offset_rotate:
        if (hasMotion && (!display->mOffsetRotate.auto_ ||
                          display->mOffsetRotate.angle.ToDegrees() != 0.0)) {
          appendFakeAnimation(id, display->mOffsetRotate);
        }
        break;
      case eCSSProperty_offset_anchor:
        if (hasMotion && !display->mOffsetAnchor.IsAuto()) {
          appendFakeAnimation(id, display->mOffsetAnchor);
        }
        break;
      case eCSSProperty_offset_position:
        if (hasMotion && !display->mOffsetPosition.IsAuto()) {
          appendFakeAnimation(id, display->mOffsetPosition);
        }
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Unsupported transform-like properties");
    }
  }
}

void AnimationInfo::AddAnimationsForDisplayItem(
    nsIFrame* aFrame, nsDisplayListBuilder* aBuilder, nsDisplayItem* aItem,
    DisplayItemType aType, WebRenderLayerManager* aLayerManager,
    const Maybe<LayoutDevicePoint>& aPosition) {
  Send sendFlag = !aBuilder ? Send::NextTransaction : Send::Immediate;
  if (sendFlag == Send::NextTransaction) {
    ClearAnimationsForNextTransaction();
  } else {
    ClearAnimations();
  }

  // Update the animation generation on the layer. We need to do this before
  // any early returns since even if we don't add any animations to the
  // layer, we still need to mark it as up-to-date with regards to animations.
  // Otherwise, in RestyleManager we'll notice the discrepancy between the
  // animation generation numbers and update the layer indefinitely.
  EffectSet* effects = EffectSet::GetForFrame(aFrame, aType);
  uint64_t animationGeneration =
      effects ? effects->GetAnimationGeneration() : 0;
  SetAnimationGeneration(animationGeneration);
  if (!effects || effects->IsEmpty()) {
    return;
  }

  EffectCompositor::ClearIsRunningOnCompositor(aFrame, aType);
  const nsCSSPropertyIDSet& propertySet =
      LayerAnimationInfo::GetCSSPropertiesFor(aType);
  const nsTArray<RefPtr<dom::Animation>> matchedAnimations =
      EffectCompositor::GetAnimationsForCompositor(aFrame, propertySet);
  if (matchedAnimations.IsEmpty()) {
    return;
  }

  // If the frame is not prerendered, bail out.
  // Do this check only during layer construction; during updating the
  // caller is required to check it appropriately.
  if (aItem && !aItem->CanUseAsyncAnimations(aBuilder)) {
    // EffectCompositor needs to know that we refused to run this animation
    // asynchronously so that it will not throttle the main thread
    // animation.
    aFrame->SetProperty(nsIFrame::RefusedAsyncAnimationProperty(), true);
    return;
  }

  const HashMap<nsCSSPropertyID, nsTArray<RefPtr<dom::Animation>>>
      compositorAnimations =
          GroupAnimationsByProperty(matchedAnimations, propertySet);
  Maybe<TransformData> transformData =
      CreateAnimationData(aFrame, aItem, aType, aLayerManager->GetBackendType(),
                          compositorAnimations.has(eCSSProperty_offset_path) ||
                                  !aFrame->StyleDisplay()->mOffsetPath.IsNone()
                              ? AnimationDataType::WithMotionPath
                              : AnimationDataType::WithoutMotionPath,
                          aPosition);
  const bool hasMultipleTransformLikeProperties =
      aType == DisplayItemType::TYPE_TRANSFORM;
  nsCSSPropertyIDSet nonAnimatingProperties =
      nsCSSPropertyIDSet::TransformLikeProperties();
  for (auto iter = compositorAnimations.iter(); !iter.done(); iter.next()) {
    // Note: We can skip offset-* if there is no offset-path/offset-position
    // animations and styles. However, it should be fine and may be better to
    // send these information to the compositor because 1) they are simple data
    // structure, 2) AddAnimationsForProperty() marks these animations as
    // running on the composiror, so CanThrottle() returns true for them, and
    // we avoid running these animations on the main thread.
    bool added = AddAnimationsForProperty(aFrame, effects, iter.get().value(),
                                          transformData, iter.get().key(),
                                          sendFlag, aLayerManager);
    if (added && transformData) {
      // Only copy TransformLikeMetaData in the first animation property.
      transformData.reset();
    }

    if (hasMultipleTransformLikeProperties && added) {
      nonAnimatingProperties.RemoveProperty(iter.get().key());
    }
  }

  // If some transform-like properties have animations, but others not, and
  // those non-animating transform-like properties have non-none
  // transform/translate/rotate/scale styles or non-initial value for motion
  // path properties, we also pass their styles into the compositor, so the
  // final transform matrix (on the compositor) could take them into account.
  if (hasMultipleTransformLikeProperties &&
      // For these cases we don't need to send the property style values to
      // the compositor:
      // 1. No property has running animations on the compositor. (i.e. All
      //    properties should be handled by main thread)
      // 2. All properties have running animations on the compositor.
      //    (i.e. Those running animations should override the styles.)
      !nonAnimatingProperties.Equals(
          nsCSSPropertyIDSet::TransformLikeProperties()) &&
      !nonAnimatingProperties.IsEmpty()) {
    AddNonAnimatingTransformLikePropertiesStyles(nonAnimatingProperties, aFrame,
                                                 sendFlag);
  }
}

}  // namespace mozilla::layers
