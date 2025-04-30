/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CSSAnimation.h"

#include "mozilla/AnimationEventDispatcher.h"
#include "mozilla/dom/CSSAnimationBinding.h"
#include "mozilla/dom/KeyframeEffectBinding.h"
#include "mozilla/TimeStamp.h"
#include "nsPresContext.h"

namespace mozilla::dom {

using AnimationPhase = ComputedTiming::AnimationPhase;

JSObject* CSSAnimation::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return dom::CSSAnimation_Binding::Wrap(aCx, this, aGivenProto);
}

void CSSAnimation::SetEffect(AnimationEffect* aEffect) {
  Animation::SetEffect(aEffect);

  AddOverriddenProperties(CSSAnimationProperties::Effect);
}

void CSSAnimation::SetStartTimeAsDouble(const Nullable<double>& aStartTime) {
  // Note that we always compare with the paused state since for the purposes
  // of determining if play control is being overridden or not, we want to
  // treat the finished state as running.
  bool wasPaused = PlayState() == AnimationPlayState::Paused;

  Animation::SetStartTimeAsDouble(aStartTime);

  bool isPaused = PlayState() == AnimationPlayState::Paused;

  if (wasPaused != isPaused) {
    AddOverriddenProperties(CSSAnimationProperties::PlayState);
  }
}

mozilla::dom::Promise* CSSAnimation::GetReady(ErrorResult& aRv) {
  FlushUnanimatedStyle();
  return Animation::GetReady(aRv);
}

void CSSAnimation::Reverse(ErrorResult& aRv) {
  // As with CSSAnimation::SetStartTimeAsDouble, we're really only interested in
  // the paused state.
  bool wasPaused = PlayState() == AnimationPlayState::Paused;

  Animation::Reverse(aRv);
  if (aRv.Failed()) {
    return;
  }

  bool isPaused = PlayState() == AnimationPlayState::Paused;

  if (wasPaused != isPaused) {
    AddOverriddenProperties(CSSAnimationProperties::PlayState);
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

void CSSAnimation::PlayFromJS(ErrorResult& aRv) {
  // Note that flushing style below might trigger calls to
  // PlayFromStyle()/PauseFromStyle() on this object.
  FlushUnanimatedStyle();
  Animation::PlayFromJS(aRv);
  if (aRv.Failed()) {
    return;
  }

  AddOverriddenProperties(CSSAnimationProperties::PlayState);
}

void CSSAnimation::PauseFromJS(ErrorResult& aRv) {
  Animation::PauseFromJS(aRv);
  if (aRv.Failed()) {
    return;
  }

  AddOverriddenProperties(CSSAnimationProperties::PlayState);
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

  TimeStamp startTimeStamp = ElapsedTimeToTimeStamp(intervalStartTime);
  TimeStamp endTimeStamp = ElapsedTimeToTimeStamp(intervalEndTime);
  TimeStamp iterationTimeStamp = ElapsedTimeToTimeStamp(iterationStartTime);

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

    cssAnimation->AddOverriddenProperties(updatedProperties);
  }
}

void CSSAnimationKeyframeEffect::SetKeyframes(JSContext* aContext,
                                              JS::Handle<JSObject*> aKeyframes,
                                              ErrorResult& aRv) {
  KeyframeEffect::SetKeyframes(aContext, aKeyframes, aRv);

  if (aRv.Failed()) {
    return;
  }

  if (CSSAnimation* cssAnimation = GetOwningCSSAnimation()) {
    cssAnimation->AddOverriddenProperties(CSSAnimationProperties::Keyframes);
  }
}

void CSSAnimationKeyframeEffect::SetComposite(
    const CompositeOperation& aComposite) {
  KeyframeEffect::SetComposite(aComposite);

  if (CSSAnimation* cssAnimation = GetOwningCSSAnimation()) {
    cssAnimation->AddOverriddenProperties(CSSAnimationProperties::Composition);
  }
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
