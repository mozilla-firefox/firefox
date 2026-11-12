/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CSSAnimation.h"

#include "mozilla/AnimationEventDispatcher.h"
#include "mozilla/KeyframeUtils.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/CSSAnimationBinding.h"
#include "mozilla/dom/KeyframeEffectBinding.h"
#include "nsAnimationManager.h"
#include "nsContentUtils.h"
#include "nsPresContext.h"

namespace mozilla::dom {

using AnimationPhase = ComputedTiming::AnimationPhase;

JSObject* CSSAnimation::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return dom::CSSAnimation_Binding::Wrap(aCx, this, aGivenProto);
}

void CSSAnimation::SetEffect(AnimationEffect* aEffect) {
  Animation::SetEffect(aEffect);

  PropertiesWillSetFromJS(CSSAnimationProperties::Effect);
}

void CSSAnimation::SetStartTime(const Nullable<CSSNumberish>& aStartTime,
                                ErrorResult& aRv) {
  // Note that we always compare with the paused state since for the purposes
  // of determining if play control is being overridden or not, we want to
  // treat the finished state as running.
  bool wasPaused = PlayState() == AnimationPlayState::Paused;

  Animation::SetStartTime(aStartTime, aRv);
  if (aRv.Failed()) {
    return;
  }

  bool isPaused = PlayState() == AnimationPlayState::Paused;

  if (wasPaused != isPaused) {
    PropertiesWillSetFromJS(CSSAnimationProperties::PlayState);
  }
}

mozilla::dom::Promise* CSSAnimation::GetReady(ErrorResult& aRv) {
  FlushUnanimatedStyle();
  return Animation::GetReady(aRv);
}

void CSSAnimation::Reverse(ErrorResult& aRv) {
  // As with CSSAnimation::SetStartTime, we're really only interested in the
  // paused state.
  bool wasPaused = PlayState() == AnimationPlayState::Paused;

  Animation::Reverse(aRv);
  if (aRv.Failed()) {
    return;
  }

  bool isPaused = PlayState() == AnimationPlayState::Paused;

  if (wasPaused != isPaused) {
    PropertiesWillSetFromJS(CSSAnimationProperties::PlayState);
  }
}

AnimationPlayState CSSAnimation::PlayStateFromJS() const {
  // Flush style to ensure that any properties controlling animation state
  // (e.g. animation-play-state) are fully updated.
  FlushUnanimatedStyle();
  return Animation::PlayStateFromJS();
}

bool CSSAnimation::PendingFromJS() const {
  // Flush style since, for example, if the animation-play-state was just
  // changed its possible we should now be pending.
  FlushUnanimatedStyle();
  return Animation::PendingFromJS();
}

void CSSAnimation::RemovedNamedTimelineReference(const nsAtom* aName) {
  // Go through the owning element: the effect's target may have been changed
  // or removed by script. An animation that style no longer owns has already
  // been dropped.
  if (!mOwningElement.IsSet()) {
    return;
  }
  nsPresContext* presContext =
      nsContentUtils::GetContextForContent(mOwningElement.Target().mElement);
  if (!presContext) {
    return;
  }
  if (auto* manager = presContext->AnimationManager()) {
    manager->RemoveNamedTimelineAnimation(aName, this);
  }
}

void CSSAnimation::CancelFromStyle(PostRestyleMode aPostRestyle) {
  // Style no longer owns this animation, so nsAnimationManager must stop
  // updating its named timeline reference.
  if (const nsAtom* name = GetTimelineName().mName) {
    RemovedNamedTimelineReference(name);
  }

  Animation::Cancel(aPostRestyle);

  // When an animation is disassociated with style it enters an odd state
  // where its composite order is undefined until it first transitions
  // out of the idle state.
  //
  // Even if the composite order isn't defined we don't want it to be random
  // in case we need to determine the order to dispatch events associated
  // with an animation in this state. To solve this we treat the animation as
  // if it had been added to the end of the global animation list so that
  // its sort order is defined. We'll update this index again once the
  // animation leaves the idle state.
  //
  // Note: We have to update |mAnimationIndex| after calling
  // Animation::Cancel(), which enqueues animationcancel event, to make sure
  // we have the correct |mAnimationIndex| in AnimationEventInfo.
  mAnimationIndex = sNextAnimationIndex++;
  mNeedsNewAnimationIndexWhenRun = true;

  // We need to do this *after* calling Cancel() since
  // Cancel() might synchronously trigger a cancel event for which
  // we need an owning element to target the event at.
  mOwningElement = OwningElementRef();
}

void CSSAnimation::PlayFromJS(ErrorResult& aRv) {
  // Note that flushing style below might trigger calls to
  // PlayFromStyle()/PauseFromStyle() on this object.
  FlushUnanimatedStyle();
  Animation::PlayFromJS(aRv);
  if (aRv.Failed()) {
    return;
  }

  PropertiesWillSetFromJS(CSSAnimationProperties::PlayState);
}

void CSSAnimation::PauseFromJS(ErrorResult& aRv) {
  Animation::PauseFromJS(aRv);
  if (aRv.Failed()) {
    return;
  }

  PropertiesWillSetFromJS(CSSAnimationProperties::PlayState);
}

void CSSAnimation::PlayFromStyle() {
  ErrorResult rv;
  Animation::Play(rv, Animation::LimitBehavior::Continue);
  // play() should not throw when LimitBehavior is Continue
  MOZ_ASSERT(!rv.Failed(), "Unexpected exception playing animation");
}

void CSSAnimation::PauseFromStyle() {
  ErrorResult rv;
  Animation::Pause(rv);
  // pause() should only throw when *all* of the following conditions are true:
  // - we are in the idle state, and
  // - we have a negative playback rate, and
  // - we have an infinitely repeating animation
  // The first two conditions will never happen under regular style processing
  // but could happen if an author made modifications to the Animation object
  // and then updated animation-play-state. It's an unusual case and there's
  // no obvious way to pass on the exception information so we just silently
  // fail for now.
  if (rv.Failed()) {
    NS_WARNING("Unexpected exception pausing animation - silently failing");
  }
}

void CSSAnimation::Tick(TickState& aState) {
  Animation::Tick(aState);
  QueueEvents();
}

int32_t CSSAnimation::CompareCompositeOrder(
    const CSSAnimation& aOther, nsContentUtils::NodeIndexCache& aCache) const {
  MOZ_ASSERT(IsTiedToMarkup() && aOther.IsTiedToMarkup(),
             "Should only be called for CSS animations that are sorted "
             "as CSS animations (i.e. tied to CSS markup)");

  // 0. Object-equality case
  if (&aOther == this) {
    return 0;
  }

  // 1. Sort by document order
  if (!mOwningElement.Equals(aOther.mOwningElement)) {
    return mOwningElement.Compare(aOther.mOwningElement, aCache);
  }

  // 2. (Same element and pseudo): Sort by position in animation-name
  MOZ_ASSERT(mAnimationIndex != aOther.mAnimationIndex);
  return mAnimationIndex < aOther.mAnimationIndex ? -1 : 1;
}

void CSSAnimation::QueueEvents(const StickyTimeDuration& aActiveTime) {
  // If the animation is pending, we ignore animation events until we finish
  // pending.
  if (mPendingState != PendingState::NotPending) {
    return;
  }

  // CSS animations dispatch events at their owning element. This allows
  // script to repurpose a CSS animation to target a different element,
  // to use a group effect (which has no obvious "target element"), or
  // to remove the animation effect altogether whilst still getting
  // animation events.
  //
  // It does mean, however, that for a CSS animation that has no owning
  // element (e.g. it was created using the CSSAnimation constructor or
  // disassociated from CSS) no events are fired. If it becomes desirable
  // for these animations to still fire events we should spec the concept
  // of the "original owning element" or "event target" and allow script
  // to set it when creating a CSSAnimation object.
  if (!mOwningElement.ShouldFireEvents()) {
    return;
  }

  nsPresContext* presContext = mOwningElement.GetPresContext();
  if (!presContext) {
    return;
  }

  uint64_t currentIteration = 0;
  ComputedTiming::AnimationPhase currentPhase;
  StickyTimeDuration intervalStartTime;
  StickyTimeDuration intervalEndTime;
  StickyTimeDuration iterationStartTime;

  if (!mEffect) {
    currentPhase =
        GetAnimationPhaseWithoutEffect<ComputedTiming::AnimationPhase>(*this);
    if (currentPhase == mPreviousPhase) {
      return;
    }
  } else {
    ComputedTiming computedTiming = mEffect->GetComputedTiming();
    currentPhase = computedTiming.mPhase;
    currentIteration = computedTiming.mCurrentIteration;
    if (currentPhase == mPreviousPhase &&
        currentIteration == mPreviousIteration) {
      return;
    }
    intervalStartTime = IntervalStartTime(computedTiming.mActiveDuration);
    intervalEndTime = IntervalEndTime(computedTiming.mActiveDuration);

    uint64_t iterationBoundary = mPreviousIteration > currentIteration
                                     ? currentIteration + 1
                                     : currentIteration;
    double multiplier = iterationBoundary - computedTiming.mIterationStart;
    if (multiplier != 0.0) {
      iterationStartTime = computedTiming.mDuration.MultDouble(multiplier);
    }
  }

  AutoTArray<AnimationEventInfo, 2> events;

  auto appendAnimationEvent = [&](EventMessage aMessage,
                                  const StickyTimeDuration& aElapsedTime,
                                  const TimeStamp& aScheduledEventTimeStamp) {
    double elapsedTime = aElapsedTime.ToSeconds();
    if (aMessage == eAnimationCancel) {
      // 0 is an inappropriate value for this callsite. What we need to do is
      // use a single random value for all increasing times reportable.
      // That is to say, whenever elapsedTime goes negative (because an
      // animation restarts, something rewinds the animation, or otherwise)
      // a new random value for the mix-in must be generated.
      elapsedTime = nsRFPService::ReduceTimePrecisionAsSecsRFPOnly(
          elapsedTime, 0, mRTPCallerType);
    }
    events.AppendElement(AnimationEventInfo(
        mAnimationName, mOwningElement.Target(), aMessage, elapsedTime,
        mAnimationIndex, aScheduledEventTimeStamp, this));
  };

  // Handle cancel event first
  if ((mPreviousPhase != AnimationPhase::Idle &&
       mPreviousPhase != AnimationPhase::After) &&
      currentPhase == AnimationPhase::Idle) {
    appendAnimationEvent(eAnimationCancel, aActiveTime,
                         GetTimelineCurrentTimeAsTimeStamp());
  }

  const TimeStamp startTimeStamp = ElapsedTimeToTimeStamp(intervalStartTime);
  const TimeStamp endTimeStamp = ElapsedTimeToTimeStamp(intervalEndTime);
  const TimeStamp iterationTimeStamp =
      ElapsedTimeToTimeStamp(iterationStartTime);

  switch (mPreviousPhase) {
    case AnimationPhase::Idle:
    case AnimationPhase::Before:
      if (currentPhase == AnimationPhase::Active) {
        appendAnimationEvent(eAnimationStart, intervalStartTime,
                             startTimeStamp);
      } else if (currentPhase == AnimationPhase::After) {
        appendAnimationEvent(eAnimationStart, intervalStartTime,
                             startTimeStamp);
        appendAnimationEvent(eAnimationEnd, intervalEndTime, endTimeStamp);
      }
      break;
    case AnimationPhase::Active:
      if (currentPhase == AnimationPhase::Before) {
        appendAnimationEvent(eAnimationEnd, intervalStartTime, startTimeStamp);
      } else if (currentPhase == AnimationPhase::Active) {
        // The currentIteration must have changed or element we would have
        // returned early above.
        MOZ_ASSERT(currentIteration != mPreviousIteration);
        appendAnimationEvent(eAnimationIteration, iterationStartTime,
                             iterationTimeStamp);
      } else if (currentPhase == AnimationPhase::After) {
        appendAnimationEvent(eAnimationEnd, intervalEndTime, endTimeStamp);
      }
      break;
    case AnimationPhase::After:
      if (currentPhase == AnimationPhase::Before) {
        appendAnimationEvent(eAnimationStart, intervalEndTime, startTimeStamp);
        appendAnimationEvent(eAnimationEnd, intervalStartTime, endTimeStamp);
      } else if (currentPhase == AnimationPhase::Active) {
        appendAnimationEvent(eAnimationStart, intervalEndTime, endTimeStamp);
      }
      break;
  }
  mPreviousPhase = currentPhase;
  mPreviousIteration = currentIteration;

  if (!events.IsEmpty()) {
    presContext->AnimationEventDispatcher()->QueueEvents(std::move(events));
  }
}

void CSSAnimation::UpdatePropertiesWithNew(
    const NonOwningAnimationTarget& aTarget,
    const ComputedStyle* aComputedStyle, TimingParams&& aNewTiming,
    nsTArray<Keyframe>&& aNewKeyframes,
    const StyleComputedTimingFunction& aNewTimingFunction,
    CompositeOperation aNewComposite, AnimationTimeline* aNewTimeline,
    const dom::ScopedTimelineName& aNewTimelineName,
    dom::AnimationRange&& aNewRange, bool aNewIsStylePaused) {
  bool animationChanged = false;

  // Update the old from the new so we can keep the original object
  // identity (and any expando properties attached to it).
  if (AnimationEffect* effect = GetEffect()) {
    // Copy across the changes that are not overridden
    TimingParams updatedTiming = effect->SpecifiedTiming();
    if (~mOverriddenProperties & CSSAnimationProperties::Duration) {
      updatedTiming.SetDuration(aNewTiming.Duration());
    }
    if (~mOverriddenProperties & CSSAnimationProperties::IterationCount) {
      updatedTiming.SetIterations(aNewTiming.Iterations());
    }
    if (~mOverriddenProperties & CSSAnimationProperties::Direction) {
      updatedTiming.SetDirection(aNewTiming.Direction());
    }
    if (~mOverriddenProperties & CSSAnimationProperties::Delay) {
      updatedTiming.SetDelay(aNewTiming.Delay());
    }
    if (~mOverriddenProperties & CSSAnimationProperties::FillMode) {
      updatedTiming.SetFill(aNewTiming.Fill());
    }

    animationChanged = effect->SpecifiedTiming() != updatedTiming;
    effect->SetSpecifiedTiming(std::move(updatedTiming));

    if (KeyframeEffect* keyframeEffect = effect->AsKeyframeEffect()) {
      if (~mOverriddenProperties & CSSAnimationProperties::Keyframes) {
        // FIXME(emilio): Should we update the effect target on top if different
        // or something?
        const auto* style = keyframeEffect->GetAnimationTarget() == aTarget
                                ? aComputedStyle
                                : nullptr;
        keyframeEffect->SetKeyframes(std::move(aNewKeyframes), style,
                                     aNewTimeline, &aNewRange);

        // The default timing function and default composite for CSS Keyframes
        // processing.
        if (auto* cssEffect = keyframeEffect->AsCSSAnimationKeyframeEffect()) {
          cssEffect->SetDefaultTimingFunction(aNewTimingFunction);
          cssEffect->SetDefaultComposite(aNewComposite);
        }
      }

      if (~mOverriddenProperties & CSSAnimationProperties::Composition) {
        animationChanged = keyframeEffect->Composite() != aNewComposite;
        keyframeEffect->SetCompositeFromStyle(aNewComposite);
      }
    }
  }

  // Checking pointers should be enough. If both are scroll-timeline, we reuse
  // the scroll-timeline object if their scrollers and axes are the same.
  if (GetTimeline() != aNewTimeline) {
    // See `UpdateNamedTimelineAnimation` as to why `SetTimeline` isn't used.
    const bool timelineDidChange = SetTimelineNoUpdate(
        aNewTimeline, aNewTimelineName, Animation::FromJS::No);
    animationChanged = animationChanged || timelineDidChange;
  }

  if (GetTimelineRange() != aNewRange) {
    SetTimelineRange(std::move(aNewRange), Animation::FromJS::No);
    animationChanged = true;
  }

  // Handle changes in play state.
  if (mIsStylePaused != aNewIsStylePaused) {
    // If the animation is idle, however, changes to animation-play-state should
    // *not* restart it, e.g. if we call cancel() on this animation, updating
    // the properties shouldn't make it live again.
    // https://github.com/w3c/csswg-drafts/issues/13503
    if (PlayState() != AnimationPlayState::Idle &&
        ~mOverriddenProperties & CSSAnimationProperties::PlayState) {
      if (aNewIsStylePaused) {
        PauseFromStyle();
      } else {
        PlayFromStyle();
      }
      animationChanged = true;
    }
    mIsStylePaused = aNewIsStylePaused;
  }

  // Updating the effect timing above might already have caused the
  // animation to become irrelevant so only add a changed record if
  // the animation is still relevant.
  if (animationChanged && IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(this);
  }
}

void CSSAnimation::UpdateTiming(SeekFlag aSeekFlag,
                                SyncNotifyFlag aSyncNotifyFlag) {
  if (mNeedsNewAnimationIndexWhenRun &&
      PlayState() != AnimationPlayState::Idle) {
    mAnimationIndex = sNextAnimationIndex++;
    mNeedsNewAnimationIndexWhenRun = false;
  }

  Animation::UpdateTiming(aSeekFlag, aSyncNotifyFlag);
}

/////////////////////// CSSAnimationKeyframeEffect ////////////////////////

void CSSAnimationKeyframeEffect::GetTiming(EffectTiming& aRetVal) const {
  MaybeFlushUnanimatedStyle();
  KeyframeEffect::GetTiming(aRetVal);
}

void CSSAnimationKeyframeEffect::GetComputedTimingAsDict(
    ComputedEffectTiming& aRetVal) const {
  MaybeFlushUnanimatedStyle();
  KeyframeEffect::GetComputedTimingAsDict(aRetVal);
}

void CSSAnimationKeyframeEffect::UpdateTiming(
    const OptionalEffectTiming& aTiming, ErrorResult& aRv) {
  KeyframeEffect::UpdateTiming(aTiming, aRv);

  if (aRv.Failed()) {
    return;
  }

  if (CSSAnimation* cssAnimation = GetOwningCSSAnimation()) {
    CSSAnimationProperties updatedProperties = CSSAnimationProperties::None;
    if (aTiming.mDuration.WasPassed()) {
      updatedProperties |= CSSAnimationProperties::Duration;
    }
    if (aTiming.mIterations.WasPassed()) {
      updatedProperties |= CSSAnimationProperties::IterationCount;
    }
    if (aTiming.mDirection.WasPassed()) {
      updatedProperties |= CSSAnimationProperties::Direction;
    }
    if (aTiming.mDelay.WasPassed()) {
      updatedProperties |= CSSAnimationProperties::Delay;
    }
    if (aTiming.mFill.WasPassed()) {
      updatedProperties |= CSSAnimationProperties::FillMode;
    }

    cssAnimation->PropertiesWillSetFromJS(updatedProperties);
  }
}

void CSSAnimationKeyframeEffect::SetKeyframes(JSContext* aContext,
                                              JS::Handle<JSObject*> aKeyframes,
                                              ErrorResult& aRv) {
  nsTArray<Keyframe> keyframes = KeyframeUtils::GetKeyframesFromObject(
      aContext, mDocument, aKeyframes, "KeyframeEffect.setKeyframes", aRv);
  if (aRv.Failed()) {
    return;
  }

  // SetKeyframes below will trigger the keyframe generation (for missing 0% and
  // 100%), so we set this flag to avoid generating the keyframes if they come
  // from JS.
  // Note that this is different from CSSAnimationProperties::Keyframes below
  // because we have to avoid generating keyframes even if we don't have
  // animation, i.e. this is a keyframe-effect-specific flag for keyframes
  // generation.
  mIgnoreKeyframesGeneration = true;

  // This is from the JS API, so we use the associated animation's timeline and
  // range if any.
  RefPtr<const ComputedStyle> style = GetTargetComputedStyle(Flush::None);
  KeyframeEffect::SetKeyframes(
      std::move(keyframes), style,
      mAnimation ? mAnimation->GetTimeline() : nullptr,
      mAnimation ? &mAnimation->GetTimelineRange() : nullptr);

  if (CSSAnimation* cssAnimation = GetOwningCSSAnimation()) {
    cssAnimation->PropertiesWillSetFromJS(CSSAnimationProperties::Keyframes);
  }
}

void CSSAnimationKeyframeEffect::SetComposite(
    const CompositeOperation& aComposite) {
  KeyframeEffect::SetComposite(aComposite);

  if (CSSAnimation* cssAnimation = GetOwningCSSAnimation()) {
    cssAnimation->PropertiesWillSetFromJS(CSSAnimationProperties::Composition);
  }
}

class ComputedOffsetComparator {
 public:
  static bool LessThan(const Keyframe& aLhs, const Keyframe& aRhs) {
    // Keep the specified order if any of the keyframes have no computed offset.
    return !std::isnan(aLhs.mComputedOffset) &&
           !std::isnan(aRhs.mComputedOffset) &&
           aLhs.mComputedOffset < aRhs.mComputedOffset;
  }
};

enum class TargetKeyframe : uint8_t {
  Initial,
  Final,
};
static Keyframe* GetOrCreateInitialOrFinalComputedKeyframe(
    nsTArray<Keyframe>& aPercentageKeyframes,
    nsTArray<Keyframe>& aRangeKeyframes, const TargetKeyframe aTargetKeyframe,
    const StyleComputedTimingFunction& aDefaultTimingFunction,
    const CompositeOperationOrAuto aDefaultComposite) {
  const double targetOffset =
      aTargetKeyframe == TargetKeyframe::Initial ? 0.0 : 1.0;
  auto IsKeyframeMatched = [&](const Keyframe& aKeyframe) {
    return aKeyframe.mComputedOffset == targetOffset &&
           (aKeyframe.mComposite == CompositeOperationOrAuto::Auto ||
            aKeyframe.mComposite == aDefaultComposite) &&
           (aKeyframe.mTimingFunction
                ? *aKeyframe.mTimingFunction == aDefaultTimingFunction
                : aDefaultTimingFunction.IsLinearKeyword());
  };

  // 1. Search the offset in |aRangeKeyframes| first, in the reverse order. We
  //    search |aRangeKeyframes| first because we perfer the later keyframe in
  //    the used order, which breaks the tie with the computed order. And range
  //    offsets are always put after percentage offsets in the computed order.
  //
  // Note: We intentionally search in the reverse order because we prefer the
  // later keyframe if mulitple keyframes have 0% or 100% (with the matched
  // easing and composite). It should happen only in keyframes with
  // <timeline-range-offset> offsets because we don't group them when converting
  // them from Servo |KeyframesStep| into Gecko |Keyframe|, in
  // Servo_StyleSet_GetKeyframesForName(), and intentionally keep those
  // keyframes unsorted (i.e. the specified order).
  // e.g.
  // cover 0% {...}
  // entry 0% {...}
  // Both of them have computed offset 0%. Since we keep |mKeyframes| with
  // <timeline-range-offset> offsets unsorted, so we can simply choose the last
  // one when breaking a tie.
  //
  // This is spec'ed especially for 100%. For 0%, the spec doesnt' mention that
  // so we use the same rule for now.
  // https://github.com/w3c/csswg-drafts/issues/14281
  for (auto& keyframe : Reversed(aRangeKeyframes)) {
    MOZ_ASSERT(keyframe.mOffset);

    // Skip the keyframe with unresolved computed offset.
    if (std::isnan(keyframe.mComputedOffset)) {
      continue;
    }

    if (IsKeyframeMatched(keyframe)) {
      return &keyframe;
    }

    // |aRangeKeyframes| is sorted by |keyframe.mComputedOffset|, so we can
    // break early.
    if (keyframe.mComputedOffset < targetOffset) {
      break;
    }
  }

  // 2. Search the offset in |aPercentageKeyframes|. We group the keyframes with
  //    <percentage> offsets already so we shouldn't have mulitple matches.
  //
  // Note: |insertPosition| is used to find the last 0% or 100% keyframe. We
  // would like to insert the missing keyframe after it.
  // e.g.
  // 0% { animation-timing-function: linear; }
  // 50% { width: 100px; }
  // 100% { animation-composition: add; }
  // We have to generate the keyframes like this:
  // [0] { offset: 0.0, easing: 'linear', composite: 'auto' }
  // [1] { offset: 0.0, easing: 'ease', composite: 'replace' } // generated 0%
  // [2] { offset: 0.5, easing: 'ease', composite: 'auto' }
  // [3] { offset: 1.0, easing: 'ease', composite: 'add' }
  // [4] { offset: 1.0, easing: 'ease', composite: 'replace' } // generated 100%
  // So |insertPosition| for 0% is 1, and for 100% is 4.
  size_t insertPosition = 0;
  for (auto& keyframe : aPercentageKeyframes) {
    MOZ_ASSERT(keyframe.mOffset);
    MOZ_ASSERT(!std::isnan(keyframe.mComputedOffset));

    if (IsKeyframeMatched(keyframe)) {
      return &keyframe;
    }

    if (keyframe.mComputedOffset > targetOffset) {
      break;
    }
    ++insertPosition;
  }

  // Now this is a missing 0%/100% keyframe. Let's generate it.
  Keyframe* newKeyframe = aPercentageKeyframes.InsertElementAt(insertPosition);
  newKeyframe->mOffset.emplace(
      Keyframe::OffsetType::PercentageOffset(targetOffset));
  newKeyframe->mComputedOffset = targetOffset;
  if (!aDefaultTimingFunction.IsLinearKeyword()) {
    newKeyframe->mTimingFunction.emplace(aDefaultTimingFunction);
  }
  newKeyframe->mComposite = aDefaultComposite;
  return newKeyframe;
}

// The step 3 (Generate Initial and Final Frames) in
// https://drafts.csswg.org/css-animations-2/#keyframe-processing
//
// Note: we use "computed keyframes" here because we expect those keyframe
// offsets have been resolved (so we have resolved computed offsets here for the
// active timeline). The only difference from spec is that we don't compute the
// property values here since the caller will do that. Perhaps we could merge
// them together in the future.
// https://drafts.csswg.org/web-animations-1/#computed-keyframes
bool CSSAnimationKeyframeEffect::GetComputedKeyframes(
    nsTArray<Keyframe>& aKeyframes) const {
  // Check if we do need to do this. This flag is set if the keyframes come from
  // the JS API.
  if (mIgnoreKeyframesGeneration) {
    return false;
  }

  // FIXME: Perhaps we could cache |allProperties| for keyframes with
  // <percentage> offsets. (The properties of keyframes with
  // <timeline-range-offset> offsets need to be checked everytime
  // unfortunately.)
  AnimatedPropertyIDSet allProperties;
  AnimatedPropertyIDSet fromProperties;
  AnimatedPropertyIDSet toProperties;

  // Add 2 more elements for the insertion of 0% and 100% if any.
  nsTArray<Keyframe> percentageKeyframes(mKeyframes.Length() + 2);
  nsTArray<Keyframe> rangeKeyframes(mKeyframes.Length());

  // Iterate keyframes and collect properties first. We have to append the
  // propeties to the 0% or 100% keyframes if needed. We merged keyframes and
  // converted the properties into physical longhands for the specified
  // keyframes already, so we can just collect the propertis information here.
  for (const Keyframe& keyframe : mKeyframes) {
    MOZ_ASSERT(keyframe.mOffset);
    if (keyframe.mOffset->IsPercentageOffset()) {
      percentageKeyframes.AppendElement(keyframe);
    } else {
      rangeKeyframes.AppendElement(keyframe);
    }

    if (std::isnan(keyframe.mComputedOffset)) {
      continue;
    }

    AnimatedPropertyIDSet currentProperties;
    for (const auto& pair : keyframe.mPropertyValues) {
      MOZ_ASSERT(
          !pair.mProperty.IsShorthand(),
          "The shorthands should be expanded already for CSS Animations");
      MOZ_ASSERT(KeyframeUtils::IsAnimatableProperty(pair.mProperty),
                 "It should be animatable for CSS Animations");
      currentProperties.AddProperty(pair.mProperty);
    }
    allProperties.AddProperties(currentProperties);

    // Note that the automatic from (0%) and to (100%) keyframes are only
    // generated for properties that don’t have keyframes at or earlier than 0%
    // or at or after 100% (respectively).
    // https://drafts.csswg.org/scroll-animations-1/#named-range-keyframes
    //
    // So we have to collect the properties with offsets <= 0 and offsets >= 1,
    // no matter what keyframe-specific timing function and keyframe-specific
    // composite are used.
    if (keyframe.mComputedOffset <= 0.0) {
      fromProperties.AddProperties(currentProperties);
    } else if (keyframe.mComputedOffset >= 1.0) {
      toProperties.AddProperties(currentProperties);
    }
  }

  // We keep the specified order if all of them are unresolved. Otherwise, sort
  // them with computed offsets to get the used order. We do this for finding
  // and generating the initial and final keyframes (since we need the used
  // order to search the keyframes below).
  //
  // Also, in getKeyframes(), other browsers show the keyframes with
  // <timeline-range-offset> offsets in the order of the computed offsets. This
  // is vague in the spec after we introduced <timeline-range-offset>, so we
  // just match the behavior of other browsers here.
  rangeKeyframes.StableSort(ComputedOffsetComparator());

  // The default composite used to search to 0% and 100% keyframes below.
  const dom::CompositeOperationOrAuto defaultComposite = [&]() {
    switch (mDefaultComposite) {
      case CompositeOperation::Replace:
        return dom::CompositeOperationOrAuto::Replace;
      case CompositeOperation::Add:
        return dom::CompositeOperationOrAuto::Add;
      case CompositeOperation::Accumulate:
        return dom::CompositeOperationOrAuto::Accumulate;
    }
    // The initial value is replace.
    return dom::CompositeOperationOrAuto::Replace;
  }();

  // Generate Initial and Final Frames:
  // 1. Find or create the initial keyframe, a keyframe with a keyframe offset
  //    of 0%, default timing function as its keyframe timing function, and
  //    default composite as its keyframe composite.
  // 2. For any property in animated properties that is not otherwise present
  //    in a keyframe with an offset of 0% or one that would be positioned
  //    earlier in the used keyframe order, add the computed value of that
  //    property on element to initial keyframe’s keyframe values.
  // 3. If initial keyframe’s keyframe values is not empty, prepend initial
  //    keyframe to keyframes. (Note: We add it to keyframes after the last
  //    keyframe with offset 0%.)

  const bool isEmptyKeyframes =
      percentageKeyframes.IsEmpty() && rangeKeyframes.IsEmpty();
  auto appendProperties = [&](Keyframe& aKeyframe,
                              const AnimatedPropertyIDSet& aCurrentProperties) {
    auto& propertyValues = aKeyframe.mPropertyValues;
    for (const auto& property : allProperties) {
      if (aCurrentProperties.HasProperty(property)) {
        continue;
      }
      // The nullptr of |mServoDeclarationBlock| means we use the base values,
      // i.e. the value generated for that property by finding the computed
      // value for that property in the absence of animations.
      propertyValues.AppendElement(PropertyValuePair{property, nullptr});
    }
  };
  // Check the equality of two property sets. Note that |fromProperties| must be
  // a subset of |allProperties|.
  if (!allProperties.IsSubsetOf(fromProperties) && !isEmptyKeyframes) {
    Keyframe* fromKeyframe = GetOrCreateInitialOrFinalComputedKeyframe(
        percentageKeyframes, rangeKeyframes, TargetKeyframe::Initial,
        mDefaultTimingFunction, defaultComposite);
    MOZ_ASSERT(fromKeyframe);
    appendProperties(*fromKeyframe, fromProperties);
  }

  // 4. Repeat for final keyframe, using an offset of 100%, considering
  // keyframes positioned later in the used keyframe order, and appending to
  // keyframes. Note that |toProperties| must be a subset of |allProperties|.
  if (!allProperties.IsSubsetOf(toProperties) && !isEmptyKeyframes) {
    Keyframe* toKeyframe = GetOrCreateInitialOrFinalComputedKeyframe(
        percentageKeyframes, rangeKeyframes, TargetKeyframe::Final,
        mDefaultTimingFunction, defaultComposite);
    MOZ_ASSERT(toKeyframe);
    appendProperties(*toKeyframe, toProperties);
  }

  aKeyframes = std::move(percentageKeyframes);
  if (!rangeKeyframes.IsEmpty()) {
    aKeyframes.AppendElements(std::move(rangeKeyframes));
  }
  return true;
}

void CSSAnimationKeyframeEffect::MaybeFlushUnanimatedStyle() const {
  if (!GetOwningCSSAnimation()) {
    return;
  }

  if (dom::Document* doc = GetRenderedDocument()) {
    doc->FlushPendingNotifications(
        ChangesToFlush(FlushType::Style, false /* flush animations */, false));
  }
}

}  // namespace mozilla::dom
