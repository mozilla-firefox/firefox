/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AnimationHelper.h"

#include "CompositorAnimationStorage.h"
#include "NonCustomCSSPropertyId.h"  // for eCSSProperty_offset_path, etc
#include "base/process_util.h"
#include "gfx2DGlue.h"       // for ThebesRect
#include "gfxLineSegment.h"  // for gfxLineSegment
#include "gfxPoint.h"        // for gfxPoint
#include "gfxQuad.h"         // for gfxQuad
#include "gfxRect.h"         // for gfxRect
#include "gfxUtils.h"        // for gfxUtils::TransformToQuad
#include "mozilla/CSSPropertyId.h"
#include "mozilla/LayerAnimationInfo.h"   // for GetCSSPropertiesFor()
#include "mozilla/Maybe.h"                // for Maybe<>
#include "mozilla/MotionPathUtils.h"      // for ResolveMotionPath()
#include "mozilla/ServoStyleConsts.h"     // for StyleComputedTimingFunction
#include "mozilla/StyleAnimationValue.h"  // for StyleAnimationValue, etc
#include "mozilla/dom/AnimationEffectBinding.h"  // for dom::FillMode
#include "mozilla/dom/KeyframeEffect.h"  // for dom::KeyFrameEffectReadOnly
#include "mozilla/dom/KeyframeEffectBinding.h"  // for dom::IterationComposite
#include "mozilla/dom/Nullable.h"               // for dom::Nullable
#include "mozilla/layers/APZSampler.h"          // for APZSampler
#include "nsDisplayList.h"                      // for nsDisplayTransform, etc
#include "nsStyleTransformMatrix.h"

namespace mozilla::layers {

static dom::Nullable<TimeDuration> CalculateElapsedTimeForScrollTimeline(
    const Maybe<APZSampler::ScrollOffsetAndRange> aScrollMeta,
    const ScrollTimelineOptions& aOptions, const StickyTimeDuration& aEndTime,
    const TimeDuration& aStartTime, float aPlaybackRate) {
  // We return Nothing If the associated APZ controller is not available
  // (because it may be destroyed but this animation is still alive).
  if (!aScrollMeta) {
    // This may happen after we reload a page. There may be a race condition
    // because the animation is still alive but the APZ is destroyed. In this
    // case, this animation is invalid, so we return nullptr.
    return nullptr;
  }

  const bool isHorizontal =
      aOptions.axis() == layers::ScrollDirection::eHorizontal;
  double range =
      isHorizontal ? aScrollMeta->mRange.width : aScrollMeta->mRange.height;
  // The APZ sampler may give us a zero range (e.g. if the user resizes the
  // element).
  if (range == 0.0) {
    // If the range is zero, we cannot calculate the progress, so just return
    // nullptr.
    return nullptr;
  }
  MOZ_ASSERT(
      range > 0.0,
      "We don't expect to get a zero or negative range on the compositor");

  // The offset may be negative if the writing mode is from right to left.
  // Use std::abs() here to avoid getting a negative progress.
  double position =
      std::abs(isHorizontal ? aScrollMeta->mOffset.x : aScrollMeta->mOffset.y);
  double progress = position / range;
  // Just in case to avoid getting a progress more than 100%, for overscrolling.
  progress = std::min(progress, 1.0);
  auto timelineTime = TimeDuration(aEndTime.MultDouble(progress));
  return dom::Animation::CurrentTimeFromTimelineTime(timelineTime, aStartTime,
                                                     aPlaybackRate);
}

static dom::Nullable<TimeDuration> CalculateElapsedTime(
    const APZSampler* aAPZSampler, const LayersId& aLayersId,
    const MutexAutoLock& aProofOfMapLock, const PropertyAnimation& aAnimation,
    const TimeStamp aPreviousFrameTime, const TimeStamp aCurrentFrameTime,
    const AnimatedValue* aPreviousValue) {
  // -------------------------------------
  // Case 1: scroll-timeline animations.
  // -------------------------------------
  if (aAnimation.mScrollTimelineOptions) {
    MOZ_ASSERT(
        aAPZSampler,
        "We don't send scroll animations to the compositor if APZ is disabled");

    return CalculateElapsedTimeForScrollTimeline(
        aAPZSampler->GetCurrentScrollOffsetAndRange(
            aLayersId, aAnimation.mScrollTimelineOptions.value().source(),
            aProofOfMapLock),
        aAnimation.mScrollTimelineOptions.value(), aAnimation.mTiming.EndTime(),
        aAnimation.mStartTime.refOr(aAnimation.mHoldTime),
        aAnimation.mPlaybackRate);
  }

  // -------------------------------------
  // Case 2: document-timeline animations.
  // -------------------------------------
  MOZ_ASSERT(
      (!aAnimation.mOriginTime.IsNull() && aAnimation.mStartTime.isSome()) ||
          aAnimation.mIsNotPlaying,
      "If we are playing, we should have an origin time and a start time");

  // Determine if the animation was play-pending and used a ready time later
  // than the previous frame time.
  //
  // To determine this, _all_ of the following conditions need to hold:
  //
  // * There was no previous animation value (i.e. this is the first frame for
  //   the animation since it was sent to the compositor), and
  // * The animation is playing, and
  // * There is a previous frame time, and
  // * The ready time of the animation is ahead of the previous frame time.
  //
  bool hasFutureReadyTime = false;
  if (!aPreviousValue && !aAnimation.mIsNotPlaying &&
      !aPreviousFrameTime.IsNull()) {
    // This is the inverse of the calculation performed in
    // AnimationInfo::StartPendingAnimations to calculate the start time of
    // play-pending animations.
    // Note that we have to calculate (TimeStamp + TimeDuration) last to avoid
    // underflow in the middle of the calulation.
    const TimeStamp readyTime =
        aAnimation.mOriginTime +
        (aAnimation.mStartTime.ref() +
         aAnimation.mHoldTime.MultDouble(1.0 / aAnimation.mPlaybackRate));
    hasFutureReadyTime = !readyTime.IsNull() && readyTime > aPreviousFrameTime;
  }
  // Use the previous vsync time to make main thread animations and compositor
  // more closely aligned.
  //
  // On the first frame where we have animations the previous timestamp will
  // not be set so we simply use the current timestamp.  As a result we will
  // end up painting the first frame twice.  That doesn't appear to be
  // noticeable, however.
  //
  // Likewise, if the animation is play-pending, it may have a ready time that
  // is *after* |aPreviousFrameTime| (but *before* |aCurrentFrameTime|).
  // To avoid flicker we need to use |aCurrentFrameTime| to avoid temporarily
  // jumping backwards into the range prior to when the animation starts.
  const TimeStamp& timeStamp = aPreviousFrameTime.IsNull() || hasFutureReadyTime
                                   ? aCurrentFrameTime
                                   : aPreviousFrameTime;

  // If the animation is not currently playing, e.g. paused or
  // finished, then use the hold time to stay at the same position.
  TimeDuration elapsedDuration =
      aAnimation.mIsNotPlaying || aAnimation.mStartTime.isNothing()
          ? aAnimation.mHoldTime
          : (timeStamp - aAnimation.mOriginTime - aAnimation.mStartTime.ref())
                .MultDouble(aAnimation.mPlaybackRate);
  return elapsedDuration;
}

enum class CanSkipCompose {
  IfPossible,
  No,
};
// This function samples the animation for a specific property. We may have
// multiple animations for a single property, and the later animations override
// the eariler ones. This function returns the sampled animation value,
// |aAnimationValue| for a single CSS property.
static AnimationHelper::SampleResult SampleAnimationForProperty(
    const APZSampler* aAPZSampler, const LayersId& aLayersId,
    const MutexAutoLock& aProofOfMapLock, TimeStamp aPreviousFrameTime,
    TimeStamp aCurrentFrameTime, const AnimatedValue* aPreviousValue,
    CanSkipCompose aCanSkipCompose,
    nsTArray<PropertyAnimation>& aPropertyAnimations,
    RefPtr<StyleAnimationValue>& aAnimationValue) {
  MOZ_ASSERT(!aPropertyAnimations.IsEmpty(), "Should have animations");

  auto reason = AnimationHelper::SampleResult::Reason::None;
  bool hasInEffectAnimations = false;
#ifdef DEBUG
  // In cases where this function returns a SampleResult::Skipped, we actually
  // do populate aAnimationValue in debug mode, so that we can MOZ_ASSERT at the
  // call site that the value that would have been computed matches the stored
  // value that we end up using. This flag is used to ensure we populate
  // aAnimationValue in this scenario.
  bool shouldBeSkipped = false;
#endif
  // Process in order, since later animations override earlier ones.
  for (PropertyAnimation& animation : aPropertyAnimations) {
    dom::Nullable<TimeDuration> elapsedDuration = CalculateElapsedTime(
        aAPZSampler, aLayersId, aProofOfMapLock, animation, aPreviousFrameTime,
        aCurrentFrameTime, aPreviousValue);

    const auto progressTimelinePosition =
        animation.mScrollTimelineOptions
            ? dom::Animation::AtProgressTimelineBoundary(
                  TimeDuration::FromMilliseconds(
                      PROGRESS_TIMELINE_DURATION_MILLISEC),
                  elapsedDuration, animation.mStartTime.refOr(TimeDuration()),
                  animation.mPlaybackRate)
            : dom::Animation::ProgressTimelinePosition::NotBoundary;

    ComputedTiming computedTiming = dom::AnimationEffect::GetComputedTimingAt(
        elapsedDuration, animation.mTiming, animation.mPlaybackRate,
        progressTimelinePosition);

    if (computedTiming.mProgress.IsNull()) {
      // For the scroll-driven animations, it's possible to let it go between
      // the active phase and the before/after phase, and so its progress
      // becomes null. In this case, we shouldn't just skip this animation.
      // Instead, we have to reset the previous sampled result. Basically, we
      // use |mProgressOnLastCompose| to check if it goes from the active phase.
      // If so, we set the returned |mReason| to ScrollToDelayPhase to let the
      // caller know we need to use the base style for this property.
      //
      // If there are any other animations which need to be sampled together
      // (in the same property animation group), this |reason| will be ignored.
      if (animation.mScrollTimelineOptions &&
          !animation.mProgressOnLastCompose.IsNull() &&
          (computedTiming.mPhase == ComputedTiming::AnimationPhase::Before ||
           computedTiming.mPhase == ComputedTiming::AnimationPhase::After)) {
        // Appearally, we go back to delay, so need to reset the last
        // composition meta data. This is necessary because
        // 1. this animation is in delay so it shouldn't have any composition
        //    meta data, and
        // 2. we will not go into this condition multiple times during delay
        //    phase because we rely on |mProgressOnLastCompose|.
        animation.ResetLastCompositionValues();
        reason = AnimationHelper::SampleResult::Reason::ScrollToDelayPhase;
      }
      continue;
    }

    dom::IterationCompositeOperation iterCompositeOperation =
        animation.mIterationComposite;

    // Skip calculation if the progress hasn't changed since the last
    // calculation.
    // Note that we don't skip calculate this animation if there is another
    // animation since the other animation might be 'accumulate' or 'add', or
    // might have a missing keyframe (i.e. this animation value will be used in
    // the missing keyframe).
    // FIXME Bug 1455476: We should do this optimizations for the case where
    // the layer has multiple animations and multiple properties.
    if (aCanSkipCompose == CanSkipCompose::IfPossible &&
        !dom::KeyframeEffect::HasComputedTimingChanged(
            computedTiming, iterCompositeOperation,
            animation.mProgressOnLastCompose,
            animation.mCurrentIterationOnLastCompose)) {
#ifdef DEBUG
      shouldBeSkipped = true;
#else
      return AnimationHelper::SampleResult::Skipped();
#endif
    }

    size_t segmentSize = animation.mSegments.Length();
    if (segmentSize == 0) {
      return AnimationHelper::SampleResult();
    }

    uint32_t segmentIndex = 0;
    PropertyAnimation::SegmentData* segment = animation.mSegments.Elements();
    while (segment->mEndPortion < computedTiming.mProgress.Value() &&
           segmentIndex < segmentSize - 1) {
      ++segment;
      ++segmentIndex;
    }

    double positionInSegment =
        (computedTiming.mProgress.Value() - segment->mStartPortion) /
        (segment->mEndPortion - segment->mStartPortion);

    double portion = StyleComputedTimingFunction::GetPortion(
        segment->mFunction, positionInSegment, computedTiming.mBeforeFlag);

    // Like above optimization, skip calculation if the target segment isn't
    // changed and if the portion in the segment isn't changed.
    // This optimization is needed for CSS animations/transitions with step
    // timing functions (e.g. the throbber animation on tabs or frame based
    // animations).
    // FIXME Bug 1455476: Like the above optimization, we should apply this
    // optimizations for multiple animation cases and multiple properties as
    // well.
    if (aCanSkipCompose == CanSkipCompose::IfPossible &&
        animation.mSegmentIndexOnLastCompose == segmentIndex &&
        !animation.mPortionInSegmentOnLastCompose.IsNull() &&
        animation.mPortionInSegmentOnLastCompose.Value() == portion) {
#ifdef DEBUG
      shouldBeSkipped = true;
#else
      return AnimationHelper::SampleResult::Skipped();
#endif
    }

    AnimationPropertySegment animSegment;
    animSegment.mFromKey = 0.0;
    animSegment.mToKey = 1.0;
    animSegment.mFromValue = AnimationValue(segment->mStartValue);
    animSegment.mToValue = AnimationValue(segment->mEndValue);
    animSegment.mFromComposite = segment->mStartComposite;
    animSegment.mToComposite = segment->mEndComposite;

    // interpolate the property
    aAnimationValue =
        Servo_ComposeAnimationSegment(
            &animSegment, aAnimationValue,
            animation.mSegments.LastElement().mEndValue, iterCompositeOperation,
            portion, computedTiming.mCurrentIteration)
            .Consume();

#ifdef DEBUG
    if (shouldBeSkipped) {
      return AnimationHelper::SampleResult::Skipped();
    }
#endif

    hasInEffectAnimations = true;
    animation.mProgressOnLastCompose = computedTiming.mProgress;
    animation.mCurrentIterationOnLastCompose = computedTiming.mCurrentIteration;
    animation.mSegmentIndexOnLastCompose = segmentIndex;
    animation.mPortionInSegmentOnLastCompose.SetValue(portion);
  }

  auto rv = hasInEffectAnimations ? AnimationHelper::SampleResult::Sampled()
                                  : AnimationHelper::SampleResult();
  rv.mReason = reason;
  return rv;
}

// This function samples the animations for a group of CSS properties. We may
// have multiple CSS properties in a group (e.g. transform-like properties).
// So the returned animation array, |aAnimationValues|, include all the
// animation values of these CSS properties.
AnimationHelper::SampleResult AnimationHelper::SampleAnimationForEachNode(
    const APZSampler* aAPZSampler, const LayersId& aLayersId,
    const MutexAutoLock& aProofOfMapLock, TimeStamp aPreviousFrameTime,
    TimeStamp aCurrentFrameTime, const AnimatedValue* aPreviousValue,
    nsTArray<PropertyAnimationGroup>& aPropertyAnimationGroups,
    SampledAnimationArray& aAnimationValues /* output */) {
  MOZ_ASSERT(!aPropertyAnimationGroups.IsEmpty(),
             "Should be called with animation data");
  MOZ_ASSERT(aAnimationValues.IsEmpty(),
             "Should be called with empty aAnimationValues");

  nsTArray<RefPtr<StyleAnimationValue>> baseStyleOfDelayAnimations;
  nsTArray<RefPtr<StyleAnimationValue>> nonAnimatingValues;
  for (PropertyAnimationGroup& group : aPropertyAnimationGroups) {
    // Initialize animation value with base style.
    RefPtr<StyleAnimationValue> currValue = group.mBaseStyle;

    CanSkipCompose canSkipCompose =
        aPreviousValue && aPropertyAnimationGroups.Length() == 1 &&
                group.mAnimations.Length() == 1
            ? CanSkipCompose::IfPossible
            : CanSkipCompose::No;

    MOZ_ASSERT(
        !group.mAnimations.IsEmpty() ||
            nsCSSPropertyIDSet::TransformLikeProperties().HasProperty(
                group.mProperty),
        "Only transform-like properties can have empty PropertyAnimation list");

    // For properties which are not animating (i.e. their values are always the
    // same), we store them in a different array, and then merge them into the
    // final result (a.k.a. aAnimationValues) because we shouldn't take them
    // into account for SampleResult. (In other words, these properties
    // shouldn't affect the optimization.)
    if (group.mAnimations.IsEmpty()) {
      nonAnimatingValues.AppendElement(std::move(currValue));
      continue;
    }

    SampleResult result = SampleAnimationForProperty(
        aAPZSampler, aLayersId, aProofOfMapLock, aPreviousFrameTime,
        aCurrentFrameTime, aPreviousValue, canSkipCompose, group.mAnimations,
        currValue);

    // FIXME: Bug 1455476: Do optimization for multiple properties. For now,
    // the result is skipped only if the property count == 1.
    if (result.IsSkipped()) {
#ifdef DEBUG
      aAnimationValues.AppendElement(std::move(currValue));
#endif
      return result;
    }

    if (!result.IsSampled()) {
      if (result.mReason == SampleResult::Reason::ScrollToDelayPhase &&
          currValue) {
        MOZ_ASSERT(currValue == group.mBaseStyle);
        baseStyleOfDelayAnimations.AppendElement(std::move(currValue));
      }
      continue;
    }

    // Insert the interpolation result into the output array.
    MOZ_ASSERT(currValue);
    aAnimationValues.AppendElement(std::move(currValue));
  }

  SampleResult rv =
      aAnimationValues.IsEmpty() ? SampleResult() : SampleResult::Sampled();

  // If there is no other sampled result, we may store these base styles
  // (together with the non-animating values) to the webrenderer before it gets
  // sync with the main thread.
  if (rv.IsNone() && !baseStyleOfDelayAnimations.IsEmpty()) {
    aAnimationValues.AppendElements(std::move(baseStyleOfDelayAnimations));
    rv.mReason = SampleResult::Reason::ScrollToDelayPhase;
  }

  if (!aAnimationValues.IsEmpty()) {
    aAnimationValues.AppendElements(std::move(nonAnimatingValues));
  }
  return rv;
}

static dom::FillMode GetAdjustedFillMode(const Animation& aAnimation) {
  // Adjust fill mode so that if the main thread is delayed in clearing
  // this animation we don't introduce flicker by jumping back to the old
  // underlying value.
  auto fillMode = static_cast<dom::FillMode>(aAnimation.fillMode());
  float playbackRate = aAnimation.playbackRate();
  switch (fillMode) {
    case dom::FillMode::None:
      if (playbackRate > 0) {
        fillMode = dom::FillMode::Forwards;
      } else if (playbackRate < 0) {
        fillMode = dom::FillMode::Backwards;
      }
      break;
    case dom::FillMode::Backwards:
      if (playbackRate > 0) {
        fillMode = dom::FillMode::Both;
      }
      break;
    case dom::FillMode::Forwards:
      if (playbackRate < 0) {
        fillMode = dom::FillMode::Both;
      }
      break;
    default:
      break;
  }
  return fillMode;
}

#ifdef DEBUG
static bool HasTransformLikeAnimations(const AnimationArray& aAnimations) {
  nsCSSPropertyIDSet transformSet =
      nsCSSPropertyIDSet::TransformLikeProperties();

  for (const Animation& animation : aAnimations) {
    if (animation.isNotAnimating()) {
      continue;
    }

    if (transformSet.HasProperty(animation.property())) {
      return true;
    }
  }

  return false;
}
#endif

AnimationStorageData AnimationHelper::ExtractAnimations(
    const LayersId& aLayersId, const AnimationArray& aAnimations,
    const CompositorAnimationStorage* aStorage,
    const TimeStamp& aPreviousSampleTime) {
  AnimationStorageData storageData;
  storageData.mLayersId = aLayersId;

  NonCustomCSSPropertyId prevId = eCSSProperty_UNKNOWN;
  PropertyAnimationGroup* currData = nullptr;
  DebugOnly<const layers::Animatable*> currBaseStyle = nullptr;

  for (const Animation& animation : aAnimations) {
    // Animations with same property are grouped together, so we can just
    // check if the current property is the same as the previous one for
    // knowing this is a new group.
    if (prevId != animation.property()) {
      // Got a different group, we should create a different array.
      currData = storageData.mAnimation.AppendElement();
      currData->mProperty = animation.property();
      if (animation.transformData()) {
        MOZ_ASSERT(!storageData.mTransformData,
                   "Only one entry has TransformData");
        storageData.mTransformData = animation.transformData();
      }

      prevId = animation.property();

      // Reset the debug pointer.
      currBaseStyle = nullptr;
    }

    MOZ_ASSERT(currData);
    if (animation.baseStyle().type() != Animatable::Tnull_t) {
      MOZ_ASSERT(!currBaseStyle || *currBaseStyle == animation.baseStyle(),
                 "Should be the same base style");

      currData->mBaseStyle = AnimationValue::FromAnimatable(
          animation.property(), animation.baseStyle());
      currBaseStyle = &animation.baseStyle();
    }

    // If this layers::Animation sets isNotAnimating to true, it only has
    // base style and doesn't have any animation information, so we can skip
    // the rest steps. (And so its PropertyAnimationGroup::mAnimation will be
    // an empty array.)
    if (animation.isNotAnimating()) {
      MOZ_ASSERT(nsCSSPropertyIDSet::TransformLikeProperties().HasProperty(
                     animation.property()),
                 "Only transform-like properties could set this true");

      if (animation.property() == eCSSProperty_offset_path) {
        MOZ_ASSERT(currData->mBaseStyle,
                   "Fixed offset-path should have base style");
        MOZ_ASSERT(HasTransformLikeAnimations(aAnimations));

        const StyleOffsetPath& offsetPath =
            animation.baseStyle().get_StyleOffsetPath();
        // FIXME: Bug 1837042. Cache all basic shapes.
        if (offsetPath.IsPath()) {
          MOZ_ASSERT(!storageData.mCachedMotionPath,
                     "Only one offset-path: path() is set");

          RefPtr<gfx::PathBuilder> builder =
              MotionPathUtils::GetCompositorPathBuilder();
          storageData.mCachedMotionPath = MotionPathUtils::BuildSVGPath(
              offsetPath.AsSVGPathData(), builder);
        }
      }

      continue;
    }

    PropertyAnimation* propertyAnimation =
        currData->mAnimations.AppendElement();

    propertyAnimation->mOriginTime = animation.originTime();
    propertyAnimation->mStartTime = animation.startTime();
    propertyAnimation->mHoldTime = animation.holdTime();
    propertyAnimation->mPlaybackRate = animation.playbackRate();
    propertyAnimation->mIterationComposite =
        static_cast<dom::IterationCompositeOperation>(
            animation.iterationComposite());
    propertyAnimation->mIsNotPlaying = animation.isNotPlaying();
    propertyAnimation->mTiming =
        TimingParams{animation.duration(),
                     animation.delay(),
                     animation.endDelay(),
                     animation.iterations(),
                     animation.iterationStart(),
                     static_cast<dom::PlaybackDirection>(animation.direction()),
                     GetAdjustedFillMode(animation),
                     animation.easingFunction()};
    propertyAnimation->mScrollTimelineOptions =
        animation.scrollTimelineOptions();

    RefPtr<StyleAnimationValue> startValue;
    if (animation.replacedTransitionId()) {
      if (const auto* animatedValue =
              aStorage->GetAnimatedValue(*animation.replacedTransitionId())) {
        startValue = animatedValue->AsAnimationValue(animation.property());
        // Basically, the timeline time is increasing monotonically, so it may
        // not make sense to have a negative start time (i.e. the case when
        // aPreviousSampleTime is behind the origin time). Therefore, if the
        // previous sample time is less than the origin time, we skip the
        // replacement of the start time.
        if (!aPreviousSampleTime.IsNull() &&
            (aPreviousSampleTime >= animation.originTime())) {
          propertyAnimation->mStartTime =
              Some(aPreviousSampleTime - animation.originTime());
        }

        MOZ_ASSERT(animation.segments().Length() == 1,
                   "The CSS Transition only has one segement");
      }
    }

    nsTArray<PropertyAnimation::SegmentData>& segmentData =
        propertyAnimation->mSegments;
    for (const AnimationSegment& segment : animation.segments()) {
      segmentData.AppendElement(PropertyAnimation::SegmentData{
          // Note that even though we re-compute the start value on the main
          // thread, we still replace it with the last sampled value, to avoid
          // any possible lag.
          startValue ? startValue
                     : AnimationValue::FromAnimatable(animation.property(),
                                                      segment.startState()),
          AnimationValue::FromAnimatable(animation.property(),
                                         segment.endState()),
          segment.sampleFn(), segment.startPortion(), segment.endPortion(),
          static_cast<dom::CompositeOperation>(segment.startComposite()),
          static_cast<dom::CompositeOperation>(segment.endComposite())});
    }
  }

#ifdef DEBUG
  // Sanity check that the grouped animation data is correct by looking at the
  // property set.
  if (!storageData.mAnimation.IsEmpty()) {
    nsCSSPropertyIDSet seenProperties;
    for (const auto& group : storageData.mAnimation) {
      NonCustomCSSPropertyId id = group.mProperty;

      MOZ_ASSERT(!seenProperties.HasProperty(id), "Should be a new property");
      seenProperties.AddProperty(id);
    }

    MOZ_ASSERT(
        seenProperties.IsSubsetOf(LayerAnimationInfo::GetCSSPropertiesFor(
            DisplayItemType::TYPE_TRANSFORM)) ||
            seenProperties.IsSubsetOf(LayerAnimationInfo::GetCSSPropertiesFor(
                DisplayItemType::TYPE_OPACITY)) ||
            seenProperties.IsSubsetOf(LayerAnimationInfo::GetCSSPropertiesFor(
                DisplayItemType::TYPE_BACKGROUND_COLOR)),
        "The property set of output should be the subset of transform-like "
        "properties, opacity, or background_color.");

    if (seenProperties.IsSubsetOf(LayerAnimationInfo::GetCSSPropertiesFor(
            DisplayItemType::TYPE_TRANSFORM))) {
      MOZ_ASSERT(storageData.mTransformData, "Should have TransformData");
    }

    if (seenProperties.HasProperty(eCSSProperty_offset_path)) {
      MOZ_ASSERT(storageData.mTransformData, "Should have TransformData");
      MOZ_ASSERT(storageData.mTransformData->motionPathData(),
                 "Should have MotionPathData");
    }
  }
#endif

  return storageData;
}

uint64_t AnimationHelper::GetNextCompositorAnimationsId() {
  static uint32_t sNextId = 0;
  ++sNextId;

  uint32_t procId = static_cast<uint32_t>(base::GetCurrentProcId());
  uint64_t nextId = procId;
  nextId = nextId << 32 | sNextId;
  return nextId;
}

gfx::Matrix4x4 AnimationHelper::ServoAnimationValueToMatrix4x4(
    const SampledAnimationArray& aValues, const TransformData& aTransformData,
    gfx::Path* aCachedMotionPath) {
  using nsStyleTransformMatrix::TransformReferenceBox;

  // This is a bit silly just to avoid the transform list copy from the
  // animation transform list.
  auto noneTranslate = StyleTranslate::None();
  auto noneRotate = StyleRotate::None();
  auto noneScale = StyleScale::None();
  const StyleTransform noneTransform;

  const StyleTranslate* translate = nullptr;
  const StyleRotate* rotate = nullptr;
  const StyleScale* scale = nullptr;
  const StyleTransform* transform = nullptr;
  Maybe<StyleOffsetPath> path;
  const StyleLengthPercentage* distance = nullptr;
  const StyleOffsetRotate* offsetRotate = nullptr;
  const StylePositionOrAuto* anchor = nullptr;
  const StyleOffsetPosition* position = nullptr;

  for (const auto& value : aValues) {
    MOZ_ASSERT(value);
    CSSPropertyId property(eCSSProperty_UNKNOWN);
    Servo_AnimationValue_GetPropertyId(value, &property);
    switch (property.mId) {
      case eCSSProperty_transform:
        MOZ_ASSERT(!transform);
        transform = Servo_AnimationValue_GetTransform(value);
        break;
      case eCSSProperty_translate:
        MOZ_ASSERT(!translate);
        translate = Servo_AnimationValue_GetTranslate(value);
        break;
      case eCSSProperty_rotate:
        MOZ_ASSERT(!rotate);
        rotate = Servo_AnimationValue_GetRotate(value);
        break;
      case eCSSProperty_scale:
        MOZ_ASSERT(!scale);
        scale = Servo_AnimationValue_GetScale(value);
        break;
      case eCSSProperty_offset_path:
        MOZ_ASSERT(!path);
        path.emplace(StyleOffsetPath::None());
        Servo_AnimationValue_GetOffsetPath(value, path.ptr());
        break;
      case eCSSProperty_offset_distance:
        MOZ_ASSERT(!distance);
        distance = Servo_AnimationValue_GetOffsetDistance(value);
        break;
      case eCSSProperty_offset_rotate:
        MOZ_ASSERT(!offsetRotate);
        offsetRotate = Servo_AnimationValue_GetOffsetRotate(value);
        break;
      case eCSSProperty_offset_anchor:
        MOZ_ASSERT(!anchor);
        anchor = Servo_AnimationValue_GetOffsetAnchor(value);
        break;
      case eCSSProperty_offset_position:
        MOZ_ASSERT(!position);
        position = Servo_AnimationValue_GetOffsetPosition(value);
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Unsupported transform-like property");
    }
  }

  TransformReferenceBox refBox(nullptr, aTransformData.bounds());
  Maybe<ResolvedMotionPathData> motion = MotionPathUtils::ResolveMotionPath(
      path.ptrOr(nullptr), distance, offsetRotate, anchor, position,
      aTransformData.motionPathData(), refBox, aCachedMotionPath);

  // We expect all our transform data to arrive in device pixels
  gfx::Point3D transformOrigin = aTransformData.transformOrigin();
  nsDisplayTransform::FrameTransformProperties props(
      translate ? *translate : noneTranslate, rotate ? *rotate : noneRotate,
      scale ? *scale : noneScale, transform ? *transform : noneTransform,
      motion, transformOrigin);

  return nsDisplayTransform::GetResultingTransformMatrix(
      props, refBox, aTransformData.appUnitsPerDevPixel());
}

static uint8_t CollectOverflowedSideLines(const gfxQuad& aPrerenderedQuad,
                                          SideBits aOverflowSides,
                                          gfxLineSegment sideLines[4]) {
  uint8_t count = 0;

  if (aOverflowSides & SideBits::eTop) {
    sideLines[count] = gfxLineSegment(aPrerenderedQuad.mPoints[0],
                                      aPrerenderedQuad.mPoints[1]);
    count++;
  }
  if (aOverflowSides & SideBits::eRight) {
    sideLines[count] = gfxLineSegment(aPrerenderedQuad.mPoints[1],
                                      aPrerenderedQuad.mPoints[2]);
    count++;
  }
  if (aOverflowSides & SideBits::eBottom) {
    sideLines[count] = gfxLineSegment(aPrerenderedQuad.mPoints[2],
                                      aPrerenderedQuad.mPoints[3]);
    count++;
  }
  if (aOverflowSides & SideBits::eLeft) {
    sideLines[count] = gfxLineSegment(aPrerenderedQuad.mPoints[3],
                                      aPrerenderedQuad.mPoints[0]);
    count++;
  }

  return count;
}

enum RegionBits : uint8_t {
  Inside = 0,
  Left = (1 << 0),
  Right = (1 << 1),
  Bottom = (1 << 2),
  Top = (1 << 3),
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(RegionBits);

static RegionBits GetRegionBitsForPoint(double aX, double aY,
                                        const gfxRect& aClip) {
  RegionBits result = RegionBits::Inside;
  if (aX < aClip.X()) {
    result |= RegionBits::Left;
  } else if (aX > aClip.XMost()) {
    result |= RegionBits::Right;
  }

  if (aY < aClip.Y()) {
    result |= RegionBits::Bottom;
  } else if (aY > aClip.YMost()) {
    result |= RegionBits::Top;
  }
  return result;
};

// https://en.wikipedia.org/wiki/Cohen%E2%80%93Sutherland_algorithm
static bool LineSegmentIntersectsClip(double aX0, double aY0, double aX1,
                                      double aY1, const gfxRect& aClip) {
  RegionBits b0 = GetRegionBitsForPoint(aX0, aY0, aClip);
  RegionBits b1 = GetRegionBitsForPoint(aX1, aY1, aClip);

  while (true) {
    if (!(b0 | b1)) {
      // Completely inside.
      return true;
    }

    if (b0 & b1) {
      // Completely outside.
      return false;
    }

    double x, y;
    // Choose an outside point.
    RegionBits outsidePointBits = b1 > b0 ? b1 : b0;
    if (outsidePointBits & RegionBits::Top) {
      x = aX0 + (aX1 - aX0) * (aClip.YMost() - aY0) / (aY1 - aY0);
      y = aClip.YMost();
    } else if (outsidePointBits & RegionBits::Bottom) {
      x = aX0 + (aX1 - aX0) * (aClip.Y() - aY0) / (aY1 - aY0);
      y = aClip.Y();
    } else if (outsidePointBits & RegionBits::Right) {
      y = aY0 + (aY1 - aY0) * (aClip.XMost() - aX0) / (aX1 - aX0);
      x = aClip.XMost();
    } else if (outsidePointBits & RegionBits::Left) {
      y = aY0 + (aY1 - aY0) * (aClip.X() - aX0) / (aX1 - aX0);
      x = aClip.X();
    }

    if (outsidePointBits == b0) {
      aX0 = x;
      aY0 = y;
      b0 = GetRegionBitsForPoint(aX0, aY0, aClip);
    } else {
      aX1 = x;
      aY1 = y;
      b1 = GetRegionBitsForPoint(aX1, aY1, aClip);
    }
  }
  MOZ_ASSERT_UNREACHABLE();
  return false;
}

// static
bool AnimationHelper::ShouldBeJank(const LayoutDeviceRect& aPrerenderedRect,
                                   SideBits aOverflowSides,
                                   const gfx::Matrix4x4& aTransform,
                                   const ParentLayerRect& aClipRect) {
  if (aClipRect.IsEmpty()) {
    return false;
  }

  gfxQuad prerenderedQuad = gfxUtils::TransformToQuad(
      ThebesRect(aPrerenderedRect.ToUnknownRect()), aTransform);

  gfxLineSegment sideLines[4];
  uint8_t overflowSideCount =
      CollectOverflowedSideLines(prerenderedQuad, aOverflowSides, sideLines);

  gfxRect clipRect = ThebesRect(aClipRect.ToUnknownRect());
  for (uint8_t j = 0; j < overflowSideCount; j++) {
    if (LineSegmentIntersectsClip(sideLines[j].mStart.x, sideLines[j].mStart.y,
                                  sideLines[j].mEnd.x, sideLines[j].mEnd.y,
                                  clipRect)) {
      return true;
    }
  }

  // With step timing functions there are cases the transform jumps to a
  // position where the partial pre-render area is totally outside of the clip
  // rect without any intersection of the partial pre-render area and the clip
  // rect happened in previous compositions but there remains visible area of
  // the entire transformed area.
  //
  // So now all four points of the transformed partial pre-render rect are
  // outside of the clip rect, if all these four points are in either side of
  // the clip rect, we consider it's jank so that on the main-thread we will
  // either a) rebuild the up-to-date display item if there remains visible area
  // or b) no longer rebuild the display item if it's totally outside of the
  // clip rect.
  //
  // Note that RegionBits::Left and Right are mutually exclusive,
  // RegionBits::Top and Bottom are also mutually exclusive, so if there remains
  // any bits, it means all four points are in the same side.
  return GetRegionBitsForPoint(prerenderedQuad.mPoints[0].x,
                               prerenderedQuad.mPoints[0].y, clipRect) &
         GetRegionBitsForPoint(prerenderedQuad.mPoints[1].x,
                               prerenderedQuad.mPoints[1].y, clipRect) &
         GetRegionBitsForPoint(prerenderedQuad.mPoints[2].x,
                               prerenderedQuad.mPoints[2].y, clipRect) &
         GetRegionBitsForPoint(prerenderedQuad.mPoints[3].x,
                               prerenderedQuad.mPoints[3].y, clipRect);
}

}  // namespace mozilla::layers
