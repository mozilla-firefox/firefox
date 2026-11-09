/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Animation.h"

#include "AnimationUtils.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/AnimationEventDispatcher.h"
#include "mozilla/AnimationTarget.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/DeclarationBlock.h"
#include "mozilla/Likely.h"
#include "mozilla/Maybe.h"  // For Maybe
#include "mozilla/ServoBindings.h"  // For Servo_SerializeTimelineRangeName, Servo_LengthPercentage_ToCss
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/AnimatableBinding.h"  // For the rangeStart/rangeEnd union
#include "mozilla/dom/AnimationBinding.h"
#include "mozilla/dom/CSSNumericValue.h"  // For CSSNumericValue::Parse
#include "mozilla/dom/CSSNumericValueBinding.h"
#include "mozilla/dom/CSSTransition.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/DocumentTimeline.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ScrollTimeline.h"  // For PROGRESS_TIMELINE_DURATION_MILLISEC
#include "nsAnimationManager.h"          // For CSSAnimation
#include "nsComputedDOMStyle.h"
#include "nsDOMCSSAttrDeclaration.h"  // For nsDOMCSSAttributeDeclaration
#include "nsDOMMutationObserver.h"    // For nsAutoAnimationMutationBatch
#include "nsIFrame.h"
#include "nsThreadUtils.h"  // For nsRunnableMethod and nsRevocableEventPtr
#include "nsTransitionManager.h"  // For CSSTransition

namespace mozilla::dom {

// Static members
uint64_t Animation::sNextAnimationIndex = 0;

NS_IMPL_CYCLE_COLLECTION_INHERITED(Animation, DOMEventTargetHelper, mTimeline,
                                   mEffect, mReady, mFinished)

NS_IMPL_ADDREF_INHERITED(Animation, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(Animation, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Animation)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

JSObject* Animation::WrapObject(JSContext* aCx,
                                JS::Handle<JSObject*> aGivenProto) {
  return dom::Animation_Binding::Wrap(aCx, this, aGivenProto);
}

// ---------------------------------------------------------------------------
//
// Utility methods
//
// ---------------------------------------------------------------------------

namespace {
// A wrapper around nsAutoAnimationMutationBatch that looks up the
// appropriate document from the supplied animation.
class MOZ_RAII AutoMutationBatchForAnimation {
 public:
  explicit AutoMutationBatchForAnimation(const Animation& aAnimation) {
    NonOwningAnimationTarget target = aAnimation.GetTargetForAnimation();
    if (!target) {
      return;
    }

    // For mutation observers, we use the OwnerDoc.
    mAutoBatch.emplace(target.mElement->OwnerDoc());
  }

 private:
  Maybe<nsAutoAnimationMutationBatch> mAutoBatch;
};
}  // namespace

// ---------------------------------------------------------------------------
//
// Animation interface:
//
// ---------------------------------------------------------------------------

Animation::Animation(nsIGlobalObject* aGlobal)
    : DOMEventTargetHelper(aGlobal),
      mAnimationIndex(sNextAnimationIndex++),
      mRTPCallerType(aGlobal->GetRTPCallerType()) {}

Animation::~Animation() = default;

/* static */
already_AddRefed<Animation> Animation::ClonePausedAnimation(
    nsIGlobalObject* aGlobal, const Animation& aOther, AnimationEffect& aEffect,
    AnimationTimeline& aTimeline) {
  // FIXME: Bug 1805950: Support printing for scroll-timeline once we resolve
  // the spec issue.
  if (aOther.UsingScrollTimeline()) {
    return nullptr;
  }

  RefPtr<Animation> animation = new Animation(aGlobal);

  // Setup the timeline. We always use document-timeline of the new document,
  // even if the timeline of |aOther| is null.
  animation->mTimeline = &aTimeline;

  // Setup the playback rate.
  animation->mPlaybackRate = aOther.mPlaybackRate;

  // Setup the timing.
  const Nullable<TimeDuration> currentTime = aOther.GetCurrentTimeAsDuration();
  if (!aOther.GetTimeline()) {
    // This simulates what we do in SetTimelineNoUpdate(). It's possible to
    // preserve the progress if the previous timeline is a scroll-timeline.
    // So for null timeline, it may have a progress and the non-null current
    // time.
    if (!currentTime.IsNull()) {
      animation->SilentlySetCurrentTime(currentTime.Value());
    }
    animation->mPreviousCurrentTime = animation->GetCurrentTimeAsDuration();
  } else {
    animation->mHoldTime = currentTime;
    if (!currentTime.IsNull()) {
      // FIXME: Should we use |timelineTime| as previous current time here? It
      // seems we should use animation->GetCurrentTimeAsDuration(), per
      // UpdateFinishedState().
      const Nullable<TimeDuration> timelineTime =
          aTimeline.GetCurrentTimeAsDuration();
      MOZ_ASSERT(!timelineTime.IsNull(), "Timeline not yet set");
      animation->mPreviousCurrentTime = timelineTime;
    }
  }

  // Setup the effect's link to this.
  animation->mEffect = &aEffect;
  animation->mEffect->SetAnimation(animation);

  animation->mPendingState = PendingState::PausePending;

  // We expect our relevance to be the same as the orginal.
  animation->mIsRelevant = aOther.mIsRelevant;

  animation->PostUpdate();
  animation->mTimeline->NotifyAnimationUpdated(*animation);
  return animation.forget();
}

NonOwningAnimationTarget Animation::GetTargetForAnimation() const {
  AnimationEffect* effect = GetEffect();
  NonOwningAnimationTarget target;
  if (!effect || !effect->AsKeyframeEffect()) {
    return target;
  }
  return effect->AsKeyframeEffect()->GetAnimationTarget();
}

/* static */
already_AddRefed<Animation> Animation::Constructor(
    const GlobalObject& aGlobal, AnimationEffect* aEffect,
    const Optional<AnimationTimeline*>& aTimeline, ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());

  AnimationTimeline* timeline;
  Document* document =
      AnimationUtils::GetCurrentRealmDocument(aGlobal.Context());

  if (aTimeline.WasPassed()) {
    timeline = aTimeline.Value();
  } else {
    if (!document) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }
    timeline = document->Timeline();
  }

  RefPtr<Animation> animation = new Animation(global);
  // JS side can't refer to timeline by name.
  animation->SetTimelineNoUpdate(timeline, {}, FromJS::Yes);
  animation->SetEffectNoUpdate(aEffect);

  return animation.forget();
}

void Animation::SetId(const nsAString& aId) {
  if (mId == aId) {
    return;
  }
  mId = aId;
  MutationObservers::NotifyAnimationChanged(this);
}

void Animation::SetEffect(AnimationEffect* aEffect) {
  SetEffectNoUpdate(aEffect);
  PostUpdate();
}

// https://drafts.csswg.org/web-animations/#setting-the-target-effect
void Animation::SetEffectNoUpdate(AnimationEffect* aEffect) {
  RefPtr<Animation> kungFuDeathGrip(this);

  if (mEffect == aEffect) {
    return;
  }

  AutoMutationBatchForAnimation mb(*this);
  bool wasRelevant = mIsRelevant;

  if (mEffect) {
    // We need to notify observers now because once we set mEffect to null
    // we won't be able to find the target element to notify.
    if (mIsRelevant) {
      MutationObservers::NotifyAnimationRemoved(this);
    }

    // Break links with the old effect and then drop it.
    RefPtr<AnimationEffect> oldEffect = mEffect;
    mEffect = nullptr;
    if (IsPartialPrerendered()) {
      if (KeyframeEffect* oldKeyframeEffect = oldEffect->AsKeyframeEffect()) {
        oldKeyframeEffect->ResetPartialPrerendered();
      }
    }
    oldEffect->SetAnimation(nullptr);

    // The following will not do any notification because mEffect is null.
    UpdateRelevance();
  }

  if (aEffect) {
    // Break links from the new effect to its previous animation, if any.
    RefPtr<AnimationEffect> newEffect = aEffect;
    Animation* prevAnim = aEffect->GetAnimation();
    if (prevAnim) {
      prevAnim->SetEffect(nullptr);
    }

    // Create links with the new effect. SetAnimation(this) will also update
    // mIsRelevant of this animation, and then notify mutation observer if
    // needed by calling Animation::UpdateRelevance(), so we don't need to
    // call it again.
    mEffect = newEffect;
    mEffect->SetAnimation(this);

    // Notify possible add or change.
    // If the target is different, the change notification will be ignored by
    // AutoMutationBatchForAnimation.
    if (wasRelevant && mIsRelevant) {
      MutationObservers::NotifyAnimationChanged(this);
    }
  }

  MaybeScheduleReplacementCheck();

  UpdateTiming(SeekFlag::NoSeek, SyncNotifyFlag::Async);
}

static TimeStamp EnsurePaintIsScheduled(Document& aDoc) {
  PresShell* presShell = aDoc.GetPresShell();
  if (!presShell) {
    return {};
  }
  nsIFrame* rootFrame = presShell->GetRootFrame();
  if (!rootFrame) {
    return {};
  }
  rootFrame->SchedulePaintWithoutInvalidatingObservers();
  auto* rd = rootFrame->PresContext()->RefreshDriver();
  if (!rd->IsInRefresh()) {
    return {};
  }
  return rd->MostRecentRefresh();
}

void Animation::SetTimelineFromJS(AnimationTimeline* aTimeline) {
  PropertiesWillSetFromJS(CSSAnimationProperties::Timeline);
  // Can't refer to timeline by name from JS side.
  const auto prevTimelineName = GetTimelineName();
  SetTimeline(aTimeline, {}, FromJS::Yes);
  if (prevTimelineName.mName) {
    // Only CSS animations are tracked by name in nsAnimationManager. Other
    // animations get the name from the timeline they share with one.
    if (auto* cssAnimation = AsCSSAnimation()) {
      cssAnimation->RemovedNamedTimelineReference(prevTimelineName.mName);
    }
  }
}

bool Animation::SetTimeline(AnimationTimeline* aTimeline,
                            const ScopedTimelineName& aTimelineName,
                            FromJS aFromJS) {
  const auto updated = SetTimelineNoUpdate(aTimeline, aTimelineName, aFromJS);
  PostUpdate();
  return updated;
}

// https://drafts.csswg.org/web-animations-2/#setting-the-timeline
bool Animation::SetTimelineNoUpdate(AnimationTimeline* aTimeline,
                                    const ScopedTimelineName& aTimelineName,
                                    FromJS aFromJS) {
  if (aFromJS == FromJS::No &&
      (PropertiesOverridenByJS() & CSSAnimationProperties::Timeline)) {
    return false;
  }
  // 1. Let old timeline be the current timeline of animation, if any.
  // 2. If new timeline is the same object as old timeline, abort this
  // procedure.
  if (mTimeline == aTimeline) {
    // nullptr -> nullptr but going from/to named timeline is significant.
    if (mTimelineName != aTimelineName) {
      mTimelineName = OwningScopedTimelineName{aTimelineName};
    }
    // Timeline still didn't update, so...
    return false;
  }

  // 3. Let previous play state be animation’s play state.
  const AnimationPlayState previousPlayState = PlayState();
  // 4. Let previous current time be the animation’s current time.
  const Nullable<TimeDuration> previousCurrentTime = GetCurrentTimeAsDuration();

  // 5. Set previous progress based in the first condition that applies:
  // If previous current time is unresolved:
  //   => Set previous progress to unresolved.
  // If end time is zero:
  //   => Set previous progress to zero.
  // Otherwise
  //   => Set previous progress = previous current time / end time
  Nullable<double> previousProgress;
  if (!previousCurrentTime.IsNull()) {
    const TimeDuration endTime = TimeDuration(EffectEnd());
    previousProgress.SetValue(endTime.IsZero()
                                  ? 0.0
                                  : previousCurrentTime.Value().ToSeconds() /
                                        endTime.ToSeconds());
  } else if (mTimeline && mTimeline->IsUnresolvedTimeline()) {
    // If we're switching out of an unresolved timeline into the document
    // timeline, we want to make sure that we trigger the animation.
    // This doesn't (& shouldn't) have any impact going into a finite timeline,
    // as the unresolved timeline does not have a resolved current time.
    previousProgress.SetValue(0.0);
  }

  // We compute the active time for the old timeline because we will use it to
  // queue the cancel event later.
  StickyTimeDuration activeTime =
      mEffect ? mEffect->GetComputedTiming().mActiveTime : StickyTimeDuration();

  // 6. Let from finite timeline be true if old timeline is not null and not
  // monotonically increasing.
  const bool fromFiniteTimeline =
      mTimeline && !mTimeline->IsMonotonicallyIncreasing();
  // 7. Let to finite timeline be true if timeline is not null and not
  // monotonically increasing.
  const bool toFiniteTimeline =
      aTimeline && !aTimeline->IsMonotonicallyIncreasing();

  // 8. Let the timeline of animation be new timeline.
  RefPtr<AnimationTimeline> oldTimeline = mTimeline;
  if (oldTimeline) {
    oldTimeline->RemoveAnimation(this);
  }
  mTimeline = aTimeline;
  mTimelineName = OwningScopedTimelineName{aTimelineName};
  // Update the normalized timing and keyframe timeline range ofset because we
  // are using the new timeline.
  if (mEffect) {
    mEffect->UpdateNormalizedTiming();
    MaybeUpdateKeyframeComputedOffsets();
  }

  // 9. Perform the steps corresponding to the first matching condition from the
  // following, if any:
  if (toFiniteTimeline) {
    // If to finite timeline,
    // Apply any pending playback rate on animation
    ApplyPendingPlaybackRate();
    // Set auto align start time to true.
    mAutoAlignStartTime = true;
    // Set start time to unresolved.
    mStartTime.SetNull();
    // Set hold time to unresolved.
    mHoldTime.SetNull();

    switch (previousPlayState) {
      case AnimationPlayState::Running:
      case AnimationPlayState::Finished:
        // If previous play state is "finished" or "running"
        // Schedule a pending play task.
        //
        // FIXME: It seems we may have to do more things than just play the
        // pending play task. In Chromium, it just calls Play(). However, in
        // WebKit, It re-creates the ready promise and play a pending play task.
        // We also call PlayNoUpdate() for now. However, this may be an
        // overshoot but we won't miss anything. If the spec issue gets
        // updated, we could refine this to just do the necessary things.
        // https://github.com/w3c/csswg-drafts/issues/11465
        PlayNoUpdate(IgnoredErrorResult(), LimitBehavior::AutoRewind);
        break;
      case AnimationPlayState::Paused:
        // If previous play state is "paused" and previous progress is resolved:
        if (!previousCurrentTime.IsNull()) {
          // Set hold time to previous progress * end time.
          mHoldTime.SetValue(
              TimeDuration(EffectEnd().MultDouble(previousProgress.Value())));
        }
        break;
      case AnimationPlayState::Idle:
        break;
    }
  } else if (fromFiniteTimeline) {
    const auto timeToSet = [&]() -> Maybe<TimeDuration> {
      const auto autoAlignStartTimePending = mAutoAlignStartTime;
      // mAutoAlignStartTime is only meaningful for finite timelines; clear it
      // here. Transitioning into a new finite timeline is handled by the
      // toFiniteTimeline branch above. This clearing is a deviation from spec
      // [1], which only acts when previousProgress is resolved; without it the
      // flag's invariant (true only while the timeline is finite) is violated
      // and AutoAlignStartTime would later fire on a monotonic timeline.
      // [1] https://drafts.csswg.org/web-animations-2/#setting-the-timeline
      if (mAutoAlignStartTime) {
        mAutoAlignStartTime = false;
      }
      if (autoAlignStartTimePending && mHoldTime.IsNull() &&
          mStartTime.IsNull()) {
        // If we don't have the start time aligned, and no valid (normalized)
        // time.
        return Some(TimeDuration::Zero());
      }

      if (previousProgress.IsNull()) {
        return Nothing{};
      }

      return Some(
          TimeDuration(EffectEnd().MultDouble(previousProgress.Value())));
    }();
    if (timeToSet) {
      // If from finite timeline and previous progress is resolved, run the
      // procedure to set the current time to previous progress * end time.
      SetCurrentTimeNoUpdate(TimeDuration(*timeToSet));
    }
  }
  // 10. If the start time of animation is resolved, make animation’s hold time
  // unresolved.
  if (!mStartTime.IsNull()) {
    mHoldTime.SetNull();
  }

  if (!aTimeline) {
    MaybeQueueCancelEvent(activeTime);
  }

  // 11. Run the procedure to update an animation’s finished state for animation
  // with the did seek flag set to false, and the synchronously notify flag set
  // to false.
  UpdateTiming(SeekFlag::NoSeek, SyncNotifyFlag::Async);

  // FIXME: Bug 1799071: Check if we need to add
  // MutationObservers::NotifyAnimationChanged(this) here.
  return true;
}

void Animation::SetTimelineRange(AnimationRange&& aRange, FromJS aFromJS) {
  SetTimelineRangeNoUpdate(std::move(aRange), aFromJS);
  PostUpdate();
}

void Animation::SetTimelineRangeNoUpdate(AnimationRange&& aRange,
                                         FromJS aFromJS) {
  if (mTimelineRange == aRange) {
    return;
  }

  // TODO: Bug 2006262. We may have to rewrite this when adding the attribute:
  // https://drafts.csswg.org/web-animations-2/#dom-animation-rangestart
  // https://drafts.csswg.org/web-animations-2/#dom-animation-rangeend
  //
  // For now, this is not exposed and is set during initialization of the CSS
  // Animations.
  const auto overridden = PropertiesOverridenByJS();
  const auto startOverridden =
      aFromJS == FromJS::No &&
      (overridden & CSSAnimationProperties::AnimationRangeStart);
  const auto endOverridden =
      aFromJS == FromJS::No &&
      (overridden & CSSAnimationProperties::AnimationRangeEnd);
  if (startOverridden && endOverridden) {
    return;
  }

  if (!startOverridden) {
    mTimelineRange.mStart = std::move(aRange.mStart);
  }
  if (!endOverridden) {
    mTimelineRange.mEnd = std::move(aRange.mEnd);
  }

  if (mEffect) {
    mEffect->UpdateNormalizedTiming();
    MaybeUpdateKeyframeComputedOffsets();
  }
}

// https://drafts.csswg.org/web-animations/#set-the-animation-start-time
void Animation::SetStartTime(const Nullable<TimeDuration>& aNewStartTime) {
  // Return early if the start time will not change. However, if we
  // are pending, then setting the start time to any value
  // including the current value has the effect of aborting
  // pending tasks so we should not return early in that case.
  if (!Pending() && aNewStartTime == mStartTime) {
    return;
  }

  AutoMutationBatchForAnimation mb(*this);

  Nullable<TimeDuration> timelineTime;
  if (mTimeline) {
    // The spec says to check if the timeline is active (has a resolved time)
    // before using it here, but we don't need to since it's harmless to set
    // the already null time to null.
    timelineTime = mTimeline->GetCurrentTimeAsDuration();
  }
  if (timelineTime.IsNull() && !aNewStartTime.IsNull()) {
    mHoldTime.SetNull();
  }

  Nullable<TimeDuration> previousCurrentTime = GetCurrentTimeAsDuration();

  ApplyPendingPlaybackRate();
  mStartTime = aNewStartTime;

  mAutoAlignStartTime = false;

  if (!aNewStartTime.IsNull()) {
    if (PlaybackRateInternal() != 0.0) {
      mHoldTime.SetNull();
    }
  } else {
    mHoldTime = previousCurrentTime;
  }

  CancelPendingTasks();
  // We may have already resolved mReady, but in that case calling
  // MaybeResolve is a no-op, so that's okay.
  MaybeResolvePromiseWithThis(mReady);

  UpdateTiming(SeekFlag::DidSeek, SyncNotifyFlag::Async);
  if (IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(this);
  }
  PostUpdate();
}

// https://drafts.csswg.org/web-animations/#current-time
Nullable<TimeDuration> Animation::GetCurrentTimeForHoldTime(
    const Nullable<TimeDuration>& aHoldTime) const {
  Nullable<TimeDuration> result;
  if (!aHoldTime.IsNull()) {
    result = aHoldTime;
    return result;
  }

  if (mTimeline && !mStartTime.IsNull()) {
    Nullable<TimeDuration> timelineTime = mTimeline->GetCurrentTimeAsDuration();
    if (!timelineTime.IsNull()) {
      result = CurrentTimeFromTimelineTime(
          timelineTime.Value(), mStartTime.Value(), PlaybackRateInternal());
    }
  }
  return result;
}

// https://drafts.csswg.org/web-animations/#set-the-current-time
void Animation::SetCurrentTime(const TimeDuration& aSeekTime) {
  // Return early if the current time has not changed. However, if we
  // are pause-pending, then setting the current time to any value
  // including the current value has the effect of aborting the
  // pause so we should not return early in that case.
  if (mPendingState != PendingState::PausePending &&
      Nullable<TimeDuration>(aSeekTime) == GetCurrentTimeAsDuration()) {
    return;
  }

  AutoMutationBatchForAnimation mb(*this);

  SetCurrentTimeNoUpdate(aSeekTime);

  if (IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(this);
  }
  PostUpdate();
}

void Animation::SetCurrentTimeNoUpdate(const TimeDuration& aSeekTime) {
  SilentlySetCurrentTime(aSeekTime);

  if (mPendingState == PendingState::PausePending) {
    // Finish the pause operation
    mHoldTime.SetValue(aSeekTime);

    ApplyPendingPlaybackRate();
    mStartTime.SetNull();

    MaybeResolvePromiseWithThis(mReady);
    CancelPendingTasks();
  }

  UpdateTiming(SeekFlag::DidSeek, SyncNotifyFlag::Async);
}

// https://drafts.csswg.org/web-animations-2/#the-overall-progress-of-an-animation
Nullable<double> Animation::GetOverallProgress() const {
  Nullable<double> result;
  if (!mEffect) {
    return result;
  }
  const Nullable<TimeDuration> currentTime = GetCurrentTimeAsDuration();
  if (currentTime.IsNull()) {
    return result;
  }

  const StickyTimeDuration endTime = EffectEnd();
  if (endTime.IsZero()) {
    if (currentTime.Value() < TimeDuration()) {
      result.SetValue(0.0);
    } else {
      result.SetValue(1.0);
    }
    return result;
  }

  if (endTime == StickyTimeDuration::Forever()) {
    result.SetValue(0.0);
    return result;
  }

  auto overallProgress =
      std::min(std::max(currentTime.Value() / endTime, 0.0), 1.0);
  result.SetValue(overallProgress);
  return result;
}

// https://drafts.csswg.org/web-animations-1/#set-the-playback-rate
void Animation::SetPlaybackRate(double aPlaybackRate) {
  // 1. Clear any pending playback rate on animation.
  mPendingPlaybackRate.reset();

  if (aPlaybackRate == mPlaybackRate) {
    return;
  }

  AutoMutationBatchForAnimation mb(*this);

  // 2. Let previous time be the value of the current time of animation before
  // changing the playback rate.
  const Nullable<TimeDuration> previousTime = GetCurrentTimeAsDuration();

  // 3. Let previous playback rate be the current effective playback rate of
  // animation.
  const double previousPlaybackRate = CurrentOrPendingPlaybackRate();

  // 4. Set the playback rate to new playback rate.
  mPlaybackRate = aPlaybackRate;

  // 5. Perform the steps corresponding to the first matching condition from the
  //    following, if any:
  if (mTimeline && mTimeline->IsMonotonicallyIncreasing() &&
      !previousTime.IsNull()) {
    // If animation is associated with a monotonically increasing timeline and
    // the previous time is resolved,
    // Set the current time of animation to previous time.
    SetCurrentTime(previousTime.Value());
  } else if (mTimeline && !mTimeline->IsMonotonicallyIncreasing() &&
             !mStartTime.IsNull() && EffectEnd() != TimeDuration::Forever() &&
             ((previousPlaybackRate < 0.0 && aPlaybackRate >= 0.0) ||
              (previousPlaybackRate >= 0.0 && aPlaybackRate < 0.0))) {
    // If animation is associated with a non-null timeline that is not
    // monotonically increasing, the start time of animation is resolved,
    // associated effect end is not infinity, and either:
    // - the previous playback rate < 0 and the new playback rate ≥ 0, or
    // - the previous playback rate ≥ 0 and the new playback rate < 0,
    // Set animation’s start time to the result of evaluating
    // "associated effect end − start time" for animation.
    mStartTime.SetValue(TimeDuration(EffectEnd()) - mStartTime.Value());
  }

  // In the case where GetCurrentTimeAsDuration() returns the same result before
  // and after updating mPlaybackRate, SetCurrentTime will return early since,
  // as far as it can tell, nothing has changed.
  // As a result, we need to perform the following updates here:
  // - update timing (since, if the sign of the playback rate has changed, our
  //   finished state may have changed),
  // - dispatch a change notification for the changed playback rate, and
  // - update the playback rate on animations on layers.
  UpdateTiming(SeekFlag::DidSeek, SyncNotifyFlag::Async);
  if (IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(this);
  }
  PostUpdate();
}

// https://drafts.csswg.org/web-animations/#seamlessly-update-the-playback-rate
void Animation::UpdatePlaybackRate(double aPlaybackRate) {
  if (mPendingPlaybackRate && mPendingPlaybackRate.value() == aPlaybackRate) {
    return;
  }

  // Calculate the play state using the existing playback rate since below we
  // want to know if the animation is _currently_ finished or not, not whether
  // it _will_ be finished.
  AnimationPlayState playState = PlayState();

  mPendingPlaybackRate = Some(aPlaybackRate);

  if (Pending()) {
    // If we already have a pending task, there is nothing more to do since the
    // playback rate will be applied then.
    //
    // However, as with the idle/paused case below, we still need to update the
    // relevance (and effect set to make sure it only contains relevant
    // animations) since the relevance is based on the Animation play state
    // which incorporates the _pending_ playback rate.
    UpdateEffect(PostRestyleMode::Never);
    return;
  }

  AutoMutationBatchForAnimation mb(*this);

  if (playState == AnimationPlayState::Idle ||
      playState == AnimationPlayState::Paused ||
      GetCurrentTimeAsDuration().IsNull()) {
    // If |previous play state| is idle or paused, or the current time is
    // unresolved, we apply any pending playback rate on animation immediately.
    ApplyPendingPlaybackRate();

    // We don't need to update timing or post an update here because:
    //
    // * the current time hasn't changed -- it's either unresolved or fixed
    //   with a hold time -- so the output won't have changed
    // * the finished state won't have changed even if the sign of the
    //   playback rate changed since we're not finished (we're paused or idle)
    // * the playback rate on layers doesn't need to be updated since we're not
    //   moving. Once we get a start time etc. we'll update the playback rate
    //   then.
    //
    // However we still need to update the relevance and effect set as well as
    // notifying observers.
    UpdateEffect(PostRestyleMode::Never);
    if (IsRelevant()) {
      MutationObservers::NotifyAnimationChanged(this);
    }
  } else if (playState == AnimationPlayState::Finished) {
    MOZ_ASSERT(mTimeline && !mTimeline->GetCurrentTimeAsDuration().IsNull(),
               "If we have no active timeline, we should be idle or paused");
    if (aPlaybackRate != 0) {
      // The unconstrained current time can only be unresolved if either we
      // don't have an active timeline (and we already asserted that is not
      // true) or we have an unresolved start time (in which case we should be
      // paused).
      MOZ_ASSERT(!GetUnconstrainedCurrentTime().IsNull(),
                 "Unconstrained current time should be resolved");
      TimeDuration unconstrainedCurrentTime =
          GetUnconstrainedCurrentTime().Value();
      TimeDuration timelineTime = mTimeline->GetCurrentTimeAsDuration().Value();
      mStartTime = StartTimeFromTimelineTime(
          timelineTime, unconstrainedCurrentTime, aPlaybackRate);
    } else {
      mStartTime = mTimeline->GetCurrentTimeAsDuration();
    }

    ApplyPendingPlaybackRate();

    // Even though we preserve the current time, we might now leave the finished
    // state (e.g. if the playback rate changes sign) so we need to update
    // timing.
    UpdateTiming(SeekFlag::NoSeek, SyncNotifyFlag::Async);
    if (IsRelevant()) {
      MutationObservers::NotifyAnimationChanged(this);
    }
    PostUpdate();
  } else {
    ErrorResult rv;
    Play(rv, LimitBehavior::Continue);
    MOZ_ASSERT(!rv.Failed(),
               "We should only fail to play when using auto-rewind behavior");
  }
}

// https://drafts.csswg.org/web-animations/#play-state
AnimationPlayState Animation::PlayState() const {
  Nullable<TimeDuration> currentTime = GetCurrentTimeAsDuration();
  if (currentTime.IsNull() && mStartTime.IsNull() && !Pending()) {
    return AnimationPlayState::Idle;
  }

  if (mPendingState == PendingState::PausePending ||
      (mStartTime.IsNull() && !Pending())) {
    return AnimationPlayState::Paused;
  }

  double playbackRate = CurrentOrPendingPlaybackRate();
  if (!currentTime.IsNull() &&
      ((playbackRate > 0.0 && currentTime.Value() >= EffectEnd()) ||
       (playbackRate < 0.0 && currentTime.Value() <= TimeDuration()))) {
    return AnimationPlayState::Finished;
  }

  return AnimationPlayState::Running;
}

Promise* Animation::GetReady(ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = GetRelevantGlobal();
  if (!mReady && global) {
    mReady = Promise::Create(global, aRv);  // Lazily create on demand
  }
  if (!mReady) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  if (!Pending()) {
    MaybeResolvePromiseWithThis(mReady);
  }
  return mReady;
}

void Animation::MaybeResolvePromiseWithThis(Promise* aPromise) {
  if (!aPromise) {
    return;
  }
  if (!nsContentUtils::IsSafeToRunScript()) [[unlikely]] {
    nsContentUtils::AddScriptRunner(NewRunnableMethod<RefPtr<Promise>>(
        "MaybeResolvePromiseWithThis", this,
        &Animation::MaybeResolvePromiseWithThis, aPromise));
    return;
  }
  RefPtr promise = aPromise;
  promise->MaybeResolve(this);
}

Promise* Animation::GetFinished(ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = GetRelevantGlobal();
  if (!mFinished && global) {
    mFinished = Promise::Create(global, aRv);  // Lazily create on demand
  }
  if (!mFinished) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  if (mFinishedIsResolved) {
    MaybeResolveFinishedPromise();
  }
  return mFinished;
}

// https://drafts.csswg.org/web-animations/#cancel-an-animation
void Animation::Cancel(PostRestyleMode aPostRestyle) {
  bool newlyIdle = false;

  if (PlayState() != AnimationPlayState::Idle) {
    newlyIdle = true;

    ResetPendingTasks();

    if (mFinished) {
      mFinished->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
      // mFinished can already be resolved.
      MOZ_ALWAYS_TRUE(mFinished->SetAnyPromiseIsHandled());
    }
    ResetFinishedPromise();

    QueuePlaybackEvent(nsGkAtoms::oncancel,
                       GetTimelineCurrentTimeAsTimeStamp());
  }

  StickyTimeDuration activeTime =
      mEffect ? mEffect->GetComputedTiming().mActiveTime : StickyTimeDuration();

  mHoldTime.SetNull();
  mStartTime.SetNull();

  // Allow our effect to remove itself from the its target element's EffectSet.
  UpdateEffect(aPostRestyle);

  if (mTimeline) {
    mTimeline->RemoveAnimation(this);
  }
  MaybeQueueCancelEvent(activeTime);

  if (newlyIdle && aPostRestyle == PostRestyleMode::IfNeeded) {
    PostUpdate();
  }
}

// https://drafts.csswg.org/web-animations/#finish-an-animation
void Animation::Finish(ErrorResult& aRv) {
  double effectivePlaybackRate = CurrentOrPendingPlaybackRate();

  if (effectivePlaybackRate == 0) {
    return aRv.ThrowInvalidStateError(
        "Can't finish animation with zero playback rate");
  }
  if (effectivePlaybackRate > 0 && EffectEnd() == TimeDuration::Forever()) {
    return aRv.ThrowInvalidStateError("Can't finish infinite animation");
  }

  AutoMutationBatchForAnimation mb(*this);

  ApplyPendingPlaybackRate();

  // Seek to the end
  TimeDuration limit =
      PlaybackRateInternal() > 0 ? TimeDuration(EffectEnd()) : TimeDuration();
  bool didChange = GetCurrentTimeAsDuration() != Nullable<TimeDuration>(limit);
  SilentlySetCurrentTime(limit);

  // If we are paused or play-pending we need to fill in the start time in
  // order to transition to the finished state.
  //
  // We only do this, however, if we have an active timeline. If we have an
  // inactive timeline we can't transition into the finished state just like
  // we can't transition to the running state (this finished state is really
  // a substate of the running state).
  if (mStartTime.IsNull() && mTimeline &&
      !mTimeline->GetCurrentTimeAsDuration().IsNull()) {
    mStartTime =
        StartTimeFromTimelineTime(mTimeline->GetCurrentTimeAsDuration().Value(),
                                  limit, PlaybackRateInternal());
    didChange = true;
  }

  // If we just resolved the start time for a pause or play-pending
  // animation, we need to clear the task. We don't do this as a branch of
  // the above however since we can have a play-pending animation with a
  // resolved start time if we aborted a pause operation.
  if (!mStartTime.IsNull() && (mPendingState == PendingState::PlayPending ||
                               mPendingState == PendingState::PausePending)) {
    if (mPendingState == PendingState::PausePending) {
      mHoldTime.SetNull();
    }
    CancelPendingTasks();
    didChange = true;
    MaybeResolvePromiseWithThis(mReady);
  }
  UpdateTiming(SeekFlag::DidSeek, SyncNotifyFlag::Sync);
  if (didChange && IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(this);
  }
  PostUpdate();
}

void Animation::Play(ErrorResult& aRv, LimitBehavior aLimitBehavior) {
  PlayNoUpdate(aRv, aLimitBehavior);
  PostUpdate();
}

// https://drafts.csswg.org/web-animations-1/#reverse-an-animation
void Animation::Reverse(ErrorResult& aRv) {
  if (!mTimeline) {
    return aRv.ThrowInvalidStateError(
        "Can't reverse an animation with no associated timeline");
  }
  if (mTimeline->GetCurrentTimeAsDuration().IsNull()) {
    return aRv.ThrowInvalidStateError(
        "Can't reverse an animation associated with an inactive timeline");
  }

  const double effectivePlaybackRate = CurrentOrPendingPlaybackRate();

  Maybe<double> originalPendingPlaybackRate = mPendingPlaybackRate;

  // We still call Play() even if the playback rate is 0 to make sure we update
  // the animation synchronously (e.g. start time / hold time).
  // Also, per spec, we do have to call Play() even if the playback rate is 0.
  // Note: If playback rate is 0, we have to preserve it, i.e. we don't set the
  // playback rate to -0.
  mPendingPlaybackRate =
      Some(effectivePlaybackRate == 0 ? 0 : -effectivePlaybackRate);

  Play(aRv, LimitBehavior::AutoRewind);

  // If Play() threw, restore state and don't report anything to mutation
  // observers.
  if (aRv.Failed()) {
    mPendingPlaybackRate = std::move(originalPendingPlaybackRate);
  }

  // Play(), above, unconditionally calls PostUpdate so we don't need to do
  // it here.
}

void Animation::Persist() {
  if (mReplaceState == AnimationReplaceState::Persisted) {
    return;
  }

  bool wasRemoved = mReplaceState == AnimationReplaceState::Removed;

  mReplaceState = AnimationReplaceState::Persisted;

  // If the animation is not (yet) removed, there should be no side effects of
  // persisting it.
  if (wasRemoved) {
    UpdateEffect(PostRestyleMode::IfNeeded);
    PostUpdate();
  }
}

DocGroup* Animation::GetDocGroup() {
  Document* doc = GetRenderedDocument();
  return doc ? doc->GetDocGroup() : nullptr;
}

// https://drafts.csswg.org/web-animations/#dom-animation-commitstyles
void Animation::CommitStyles(ErrorResult& aRv) {
  if (!mEffect) {
    return;
  }

  // Take an owning reference to the keyframe effect. This will ensure that
  // this Animation and the target element remain alive after flushing style.
  RefPtr<KeyframeEffect> keyframeEffect = mEffect->AsKeyframeEffect();
  if (!keyframeEffect) {
    return;
  }

  NonOwningAnimationTarget target = keyframeEffect->GetAnimationTarget();
  if (!target) {
    return;
  }

  if (!target.mPseudoRequest.IsNotPseudo()) {
    return aRv.ThrowNoModificationAllowedError(
        "Can't commit styles of a pseudo-element");
  }

  // Check it is an element with a style attribute
  RefPtr<nsStyledElement> styledElement =
      nsStyledElement::FromNodeOrNull(target.mElement);
  if (!styledElement) {
    return aRv.ThrowNoModificationAllowedError(
        "Target is not capable of having a style attribute");
  }

  // Hold onto a strong reference to the doc in case the flush destroys it.
  RefPtr<Document> doc = target.mElement->GetComposedDoc();

  // Flush frames before checking if the target element is rendered since the
  // result could depend on pending style changes, and IsRendered() looks at the
  // primary frame.
  if (doc) {
    doc->FlushPendingNotifications(FlushType::Frames);
  }
  if (!target.mElement->IsRendered()) {
    return aRv.ThrowInvalidStateError("Target is not rendered");
  }
  nsPresContext* presContext =
      nsContentUtils::GetContextForContent(target.mElement);
  if (!presContext) {
    return aRv.ThrowInvalidStateError("Target is not rendered");
  }

  // Get the computed animation values
  UniquePtr<StyleAnimationValueMap> animationValues(
      Servo_AnimationValueMap_Create());
  if (!presContext->EffectCompositor()->ComposeServoAnimationRuleForEffect(
          *keyframeEffect, CascadeLevel(), animationValues.get(),
          StaticPrefs::dom_animations_commit_styles_endpoint_inclusive()
              ? EndpointBehavior::Inclusive
              : EndpointBehavior::Exclusive)) {
    NS_WARNING("Failed to compose animation style to commit");
    return;
  }

  // Calling SetCSSDeclaration will trigger attribute setting code.
  // Start the update now so that the old rule doesn't get used
  // between when we mutate the declaration and when we set the new
  // rule.
  mozAutoDocUpdate autoUpdate(target.mElement->OwnerDoc(), true);

  // Get the inline style to append to
  RefPtr<StyleLockedDeclarationBlock> declarationBlock;
  if (auto* existing = target.mElement->GetInlineStyleDeclaration()) {
    declarationBlock = nsDOMCSSDeclaration::EnsureBlockMutable(existing);
  } else {
    declarationBlock = Servo_DeclarationBlock_CreateEmpty().Consume();
  }

  // Prepare the callback
  MutationClosureData closureData;
  closureData.mShouldBeCalled = true;
  closureData.mElement = target.mElement;
  DeclarationBlockMutationClosure beforeChangeClosure = {
      nsDOMCSSAttributeDeclaration::MutationClosureFunction,
      &closureData,
  };

  // Set the animated styles
  bool changed = false;
  const AnimatedPropertyIDSet& properties = keyframeEffect->GetPropertySet();
  for (const CSSPropertyId& property : properties) {
    RefPtr<StyleAnimationValue> computedValue =
        Servo_AnimationValueMap_GetValue(animationValues.get(), &property)
            .Consume();
    if (computedValue) {
      changed |= Servo_DeclarationBlock_SetPropertyToAnimationValue(
          declarationBlock.get(), computedValue, beforeChangeClosure);
    }
  }

  if (!changed) {
    MOZ_ASSERT(!closureData.mWasCalled);
    return;
  }

  MOZ_ASSERT(closureData.mWasCalled);
  // Update inline style declaration
  target.mElement->SetInlineStyleDeclaration(*declarationBlock, closureData);
}

// ---------------------------------------------------------------------------
//
// JS wrappers for Animation interface:
//
// ---------------------------------------------------------------------------

void Animation::GetStartTime(Nullable<OwningCSSNumberish>& aRetVal) const {
  AnimationUtils::DurationToCSSNumberish(
      GetStartTime(), AcceptsPercentageBasedTime(), mRTPCallerType,
      GetParentObject(), aRetVal);
}

void Animation::SetStartTime(const Nullable<CSSNumberish>& aStartTime,
                             ErrorResult& aRv) {
  if (aStartTime.IsNull()) {
    SetStartTime(Nullable<TimeDuration>());
    return;
  }

  const bool progressBased = AcceptsPercentageBasedTime();

  // Step 1: Run the validate a CSSNumberish time procedure; abort on failure.
  if (!AnimationUtils::ValidateCSSNumberishTime(aStartTime.Value(),
                                                progressBased, aRv)) {
    return;
  }

  Nullable<TimeDuration> time =
      AnimationUtils::CSSNumberishToDuration(aStartTime.Value(), progressBased);
  MOZ_ASSERT(!time.IsNull());
  SetStartTime(time);
}

bool Animation::AcceptsPercentageBasedTime() const {
  return StaticPrefs::layout_css_typed_om_enabled() && HasFiniteTimeline();
}

void Animation::GetCurrentTime(Nullable<OwningCSSNumberish>& aRetVal) const {
  AnimationUtils::DurationToCSSNumberish(
      GetCurrentTimeAsDuration(), AcceptsPercentageBasedTime(), mRTPCallerType,
      GetParentObject(), aRetVal);
}

// https://drafts.csswg.org/web-animations-2/#setting-the-current-time-of-an-animation
// https://drafts.csswg.org/web-animations-2/#set-the-current-time
void Animation::SetCurrentTime(const Nullable<CSSNumberish>& aCurrentTime,
                               ErrorResult& aRv) {
  // Step 1: If seek time is an unresolved time value.
  if (aCurrentTime.IsNull()) {
    if (!GetCurrentTimeAsDuration().IsNull()) {
      aRv.ThrowTypeError(
          "Current time is resolved but trying to set it to an unresolved "
          "time");
    }
    return;
  }

  const bool progressBased = AcceptsPercentageBasedTime();

  // Step 2+3: Run the validate a CSSNumberish time procedure; abort on failure.
  if (!AnimationUtils::ValidateCSSNumberishTime(aCurrentTime.Value(),
                                                progressBased, aRv)) {
    return;
  }

  Nullable<TimeDuration> seekTime = AnimationUtils::CSSNumberishToDuration(
      aCurrentTime.Value(), progressBased);
  MOZ_ASSERT(!seekTime.IsNull());
  SetCurrentTime(seekTime.Value());
}

// https://drafts.csswg.org/web-animations-2/#dom-animation-rangestart
static void RangeBoundaryToTimelineRangeValue(
    StyleTimelineRangeName aName, const StyleLengthPercentage& aOffset,
    nsIGlobalObject* aParent,
    OwningTimelineRangeOffsetOrCSSNumericValueOrCSSKeywordValueOrUTF8String&
        aRetVal) {
  if (aName == StyleTimelineRangeName::Normal) {
    aRetVal.SetAsUTF8String().AssignLiteral("normal");
    return;
  }

  TimelineRangeOffset& result = aRetVal.SetAsTimelineRangeOffset();
  if (aName != StyleTimelineRangeName::None) {
    nsAutoCString rangeName;
    Servo_SerializeTimelineRangeName(aName, &rangeName);
    result.mRangeName.Construct(std::move(rangeName));
  }
  // else: a bare <length-percentage>, so rangeName is left null.

  // If CSS Typed OM is disabled, we are not able to set the CSSNumericValue,
  // i.e. TimelineRangeOffset.offset below, so return early.
  if (!StaticPrefs::layout_css_typed_om_enabled()) {
    return;
  }

  // Serialize the computed offset and re-parse it into a CSSNumericValue so
  // percentages, absolute lengths and calc() (including length/percentage
  // mixes) all round-trip. The text came from the style system, so parsing it
  // back should not fail; if it somehow does, leave the offset unset.
  nsAutoCString offsetCss;
  Servo_LengthPercentage_ToCss(&aOffset, &offsetCss);
  if (RefPtr<CSSNumericValue> offset =
          CSSNumericValue::Parse(aParent, offsetCss, IgnoreErrors())) {
    MOZ_ASSERT(offset);
    result.mOffset.Construct(offset.forget());
  }
}

void Animation::GetRangeStart(JSContext* aCx,
                              JS::MutableHandle<JS::Value> aRetVal,
                              ErrorResult& aRv) {
  OwningTimelineRangeOffsetOrCSSNumericValueOrCSSKeywordValueOrUTF8String value;
  RangeBoundaryToTimelineRangeValue(mTimelineRange.mStart.name,
                                    mTimelineRange.mStart.lp, GetParentObject(),
                                    value);
  if (!value.ToJSVal(aCx, nullptr, aRetVal)) {
    aRv.NoteJSContextException(aCx);
  }
}

void Animation::GetRangeEnd(JSContext* aCx,
                            JS::MutableHandle<JS::Value> aRetVal,
                            ErrorResult& aRv) {
  OwningTimelineRangeOffsetOrCSSNumericValueOrCSSKeywordValueOrUTF8String value;
  RangeBoundaryToTimelineRangeValue(mTimelineRange.mEnd.name,
                                    mTimelineRange.mEnd.lp, GetParentObject(),
                                    value);
  if (!value.ToJSVal(aCx, nullptr, aRetVal)) {
    aRv.NoteJSContextException(aCx);
  }
}

void Animation::SetRangeStart(JSContext* aCx, JS::Handle<JS::Value> aValue,
                              ErrorResult& aRv) {
  OwningTimelineRangeOffsetOrCSSNumericValueOrCSSKeywordValueOrUTF8String value;
  if (!value.Init(aCx, aValue, "Animation.rangeStart")) {
    aRv.NoteJSContextException(aCx);
    return;
  }
  AnimationRange range = mTimelineRange;
  if (!AnimationUtils::SetAnimationRangeStart(value, range, aRv)) {
    return;
  }
  PropertiesWillSetFromJS(CSSAnimationProperties::AnimationRangeStart);
  SetTimelineRange(std::move(range), FromJS::Yes);
}

void Animation::SetRangeEnd(JSContext* aCx, JS::Handle<JS::Value> aValue,
                            ErrorResult& aRv) {
  OwningTimelineRangeOffsetOrCSSNumericValueOrCSSKeywordValueOrUTF8String value;
  if (!value.Init(aCx, aValue, "Animation.rangeEnd")) {
    aRv.NoteJSContextException(aCx);
    return;
  }
  AnimationRange range = mTimelineRange;
  if (!AnimationUtils::SetAnimationRangeEnd(value, range, aRv)) {
    return;
  }
  PropertiesWillSetFromJS(CSSAnimationProperties::AnimationRangeEnd);
  SetTimelineRange(std::move(range), FromJS::Yes);
}

// ---------------------------------------------------------------------------

void Animation::Tick(AnimationTimeline::TickState& aTickState) {
  MakeReadyAndMaybeTrigger();
  UpdateTiming(SeekFlag::NoSeek, SyncNotifyFlag::Sync);

  // Check for changes to whether or not this animation is replaceable.
  bool isReplaceable = IsReplaceable();
  if (isReplaceable && !mWasReplaceableAtLastTick) {
    ScheduleReplacementCheck();
  }
  mWasReplaceableAtLastTick = isReplaceable;

  if (!mEffect) {
    return;
  }

  // Update layers if we are newly finished.
  KeyframeEffect* keyframeEffect = mEffect->AsKeyframeEffect();
  if (keyframeEffect && !keyframeEffect->Properties().IsEmpty() &&
      !mFinishedAtLastComposeStyle &&
      PlayState() == AnimationPlayState::Finished) {
    PostUpdate();
  }
}

bool Animation::MakeReadyAndMaybeTrigger() {
  // FIXME: We probably need to do this only if the timeline data or range is
  // changed. For now we always call the procedure per spec.
  AutoAlignStartTime();
  if (!Pending()) {
    return false;
  }
  // mPendingReadyTime is only meaningful for monotonic timelines (see its
  // declaration comment). For finite timelines, trigger directly using the
  // timeline's current time.
  if (mPendingReadyTime.IsNull() && !HasFiniteTimeline()) {
    if (MOZ_LIKELY(mTimeline)) {
      // Monotonic timeline with no ready time yet — schedule the trigger for
      // the next tick, but with this tick's timestamp.
      mPendingReadyTime = mTimeline->GetCurrentTimeAsTimeStamp();
    }
    return false;
  }
  return TryTriggerNow();
}

bool Animation::TryTriggerNow() {
  if (!Pending()) {
    return true;
  }
  // If we don't have an active timeline we can't trigger the animation.
  if (NS_WARN_IF(!mTimeline)) {
    return false;
  }

  // Note(dshin): Don't try to trigger inactive timelines, since they won't
  // tick in any meaningful way. This has implications on fulfilling the ready
  // promise - See https://github.com/w3c/csswg-drafts/issues/9256
  if (mTimeline->IsUnresolvedTimeline()) {
    return false;
  }

  // FIXME: Bug 2017448. Force to use timeline current time for finite
  // timelines. We may have to figure out a more suitable way to handle it.
  auto currentTime = (mPendingReadyTime.IsNull() || HasFiniteTimeline())
                         ? mTimeline->GetCurrentTimeAsDuration()
                         : mTimeline->ToTimelineTime(mPendingReadyTime);
  mPendingReadyTime = {};
  if (currentTime.IsNull()) {
    (void)NS_WARN_IF(!HasFiniteTimeline());
    return false;
  }
  FinishPendingAt(currentTime.Value());
  return true;
}

double Animation::CurrentOrPendingPlaybackRate() const {
  if (mPendingPlaybackRate.isSome()) {
    return *mPendingPlaybackRate * AnimationsPlayBackRateMultiplier();
  }

  return PlaybackRateInternal();
}

TimeStamp Animation::AnimationTimeToTimeStamp(
    const StickyTimeDuration& aTime) const {
  // Initializes to null. Return the same object every time to benefit from
  // return-value-optimization.
  TimeStamp result;

  // We *don't* check for mTimeline->TracksWallclockTime() here because that
  // method only tells us if the timeline times can be converted to
  // TimeStamps that can be compared to TimeStamp::Now() or not, *not*
  // whether the timelines can be converted to TimeStamp values at all.
  //
  // Furthermore, we want to be able to use this method when the refresh driver
  // is under test control (in which case TracksWallclockTime() will return
  // false).
  //
  // Once we introduce timelines that are not time-based we will need to
  // differentiate between them here and determine how to sort their events.
  if (!mTimeline) {
    return result;
  }

  // Check the time is convertible to a timestamp
  if (aTime == TimeDuration::Forever() || PlaybackRateInternal() == 0.0 ||
      mStartTime.IsNull()) {
    return result;
  }

  // Invert the standard relation:
  //   current time = (timeline time - start time) * playback rate
  TimeDuration timelineTime =
      TimeDuration(aTime).MultDouble(1.0 / PlaybackRateInternal()) +
      mStartTime.Value();

  result = mTimeline->ToTimeStamp(timelineTime);
  return result;
}

TimeStamp Animation::ElapsedTimeToTimeStamp(
    const StickyTimeDuration& aElapsedTime) const {
  TimeDuration delay =
      mEffect ? mEffect->NormalizedTiming().Delay() : TimeDuration();
  if (MOZ_UNLIKELY((delay == -TimeDuration::Forever() &&
                    aElapsedTime == TimeDuration::Forever()) ||
                   (delay == TimeDuration::Forever() &&
                    aElapsedTime == -TimeDuration::Forever()))) {
    // It's possible to have a negative infinite delay, e.g. using an extreme
    // small value like -1e18s, or use calc() like
    // `animation-delay: calc(-infinity * 1s)`. Also, it's possible to have a
    // positive infinite |aElaspedTime| by specifying the infinite iteration
    // count or the infinite duration. In these cases, we may try to calculate
    // "inf + -inf", which results in an unexpected behavior, so here we just
    // return the current timeline time to avoid breaking the sorting of events
    // (this is the same as the cancel event to make sure we sort cancel first).
    // Note: The return TimeStamp of this function is used as
    // AnimationEventInfo::mScheduledEventTimeStamp for event sorting, and we
    // also use GetTimelineCurrentTimeAsTimeStamp() for some events. It should
    // be fine to use it as the fallback way.
    return GetTimelineCurrentTimeAsTimeStamp();
  }
  return AnimationTimeToTimeStamp(aElapsedTime + delay);
}

// https://drafts.csswg.org/web-animations/#silently-set-the-current-time
void Animation::SilentlySetCurrentTime(const TimeDuration& aSeekTime) {
  // TODO: Bug 1762238: Introduce "valid seek time" after introducing
  // CSSNumberish time values.
  // https://drafts.csswg.org/web-animations-2/#silently-set-the-current-time

  // Check the time is convertible to a timestamp
  if (!mHoldTime.IsNull() || mStartTime.IsNull() || !mTimeline ||
      mTimeline->GetCurrentTimeAsDuration().IsNull() ||
      PlaybackRateInternal() == 0.0) {
    mHoldTime.SetValue(aSeekTime);
  } else {
    mStartTime =
        StartTimeFromTimelineTime(mTimeline->GetCurrentTimeAsDuration().Value(),
                                  aSeekTime, PlaybackRateInternal());
  }

  if (!mTimeline || mTimeline->GetCurrentTimeAsDuration().IsNull()) {
    mStartTime.SetNull();
  }

  mPreviousCurrentTime.SetNull();
  mAutoAlignStartTime = false;
}

bool Animation::ShouldBeSynchronizedWithMainThread(
    const nsCSSPropertyIDSet& aPropertySet, const nsIFrame* aFrame,
    AnimationPerformanceWarning::Type& aPerformanceWarning) const {
  // Only synchronize playing animations
  if (!IsPlaying()) {
    return false;
  }

  // Currently only transform animations need to be synchronized
  if (!aPropertySet.Intersects(nsCSSPropertyIDSet::TransformLikeProperties())) {
    return false;
  }

  KeyframeEffect* keyframeEffect =
      mEffect ? mEffect->AsKeyframeEffect() : nullptr;
  if (!keyframeEffect) {
    return false;
  }

  return keyframeEffect->ShouldBlockAsyncTransformAnimations(
      aFrame, aPropertySet, aPerformanceWarning);
}

void Animation::UpdateRelevance() {
  bool wasRelevant = mIsRelevant;
  mIsRelevant = mReplaceState != AnimationReplaceState::Removed &&
                (HasCurrentEffect() || IsInEffect());

  // Notify animation observers.
  if (wasRelevant && !mIsRelevant) {
    MutationObservers::NotifyAnimationRemoved(this);
  } else if (!wasRelevant && mIsRelevant) {
    UpdateHiddenByContentVisibility();
    MutationObservers::NotifyAnimationAdded(this);
  }
}

template <class T>
bool IsMarkupAnimation(T* aAnimation) {
  return aAnimation && aAnimation->IsTiedToMarkup();
}

// https://drafts.csswg.org/web-animations/#replaceable-animation
bool Animation::IsReplaceable() const {
  // We never replace CSS animations or CSS transitions since they are managed
  // by CSS.
  if (IsMarkupAnimation(AsCSSAnimation()) ||
      IsMarkupAnimation(AsCSSTransition())) {
    return false;
  }

  // Only finished animations can be replaced.
  if (PlayState() != AnimationPlayState::Finished) {
    return false;
  }

  // Already removed animations cannot be replaced.
  if (ReplaceState() == AnimationReplaceState::Removed) {
    return false;
  }

  // We can only replace an animation if we know that, uninterfered, it would
  // never start playing again. That excludes any animations on timelines that
  // aren't monotonically increasing.
  //
  // If we don't have any timeline at all, then we can't be in the finished
  // state (since we need both a resolved start time and current time for that)
  // and will have already returned false above.
  //
  // (However, if it ever does become possible to be finished without a timeline
  // then we will want to return false here since it probably suggests an
  // animation being driven directly by script, in which case we can't assume
  // anything about how they will behave.)
  if (!GetTimeline() || !GetTimeline()->TracksWallclockTime()) {
    return false;
  }

  // If the animation doesn't have an effect then we can't determine if it is
  // filling or not so just leave it alone.
  if (!GetEffect()) {
    return false;
  }

  // At the time of writing we only know about KeyframeEffects. If we introduce
  // other types of effects we will need to decide if they are replaceable or
  // not.
  MOZ_ASSERT(GetEffect()->AsKeyframeEffect(),
             "Effect should be a keyframe effect");

  // We only replace animations that are filling.
  if (GetEffect()->GetComputedTiming().mProgress.IsNull()) {
    return false;
  }

  // We should only replace animations with a target element (since otherwise
  // what other effects would we consider when determining if they are covered
  // or not?).
  if (!GetEffect()->AsKeyframeEffect()->GetAnimationTarget()) {
    return false;
  }

  return true;
}

bool Animation::IsRemovable() const {
  return ReplaceState() == AnimationReplaceState::Active && IsReplaceable();
}

void Animation::ScheduleReplacementCheck() {
  MOZ_ASSERT(
      IsReplaceable(),
      "Should only schedule a replacement check for a replaceable animation");

  // If IsReplaceable() is true, the following should also hold
  MOZ_ASSERT(GetEffect());
  MOZ_ASSERT(GetEffect()->AsKeyframeEffect());

  NonOwningAnimationTarget target =
      GetEffect()->AsKeyframeEffect()->GetAnimationTarget();

  MOZ_ASSERT(target);

  nsPresContext* presContext =
      nsContentUtils::GetContextForContent(target.mElement);
  if (presContext) {
    presContext->EffectCompositor()->NoteElementForReducing(target);
  }
}

void Animation::MaybeScheduleReplacementCheck() {
  if (!IsReplaceable()) {
    return;
  }

  ScheduleReplacementCheck();
}

void Animation::Remove() {
  MOZ_ASSERT(IsRemovable(),
             "Should not be trying to remove an effect that is not removable");

  mReplaceState = AnimationReplaceState::Removed;

  UpdateEffect(PostRestyleMode::IfNeeded);
  PostUpdate();

  QueuePlaybackEvent(nsGkAtoms::onremove, GetTimelineCurrentTimeAsTimeStamp());
}

int32_t Animation::CompareCompositeOrder(
    const Maybe<EventContext>& aContext, const Animation& aOther,
    const Maybe<EventContext>& aOtherContext,
    nsContentUtils::NodeIndexCache& aCache) const {
  // 0. Object-equality case
  if (&aOther == this) {
    return 0;
  }

  // 1. CSS Transitions sort lowest
  {
    auto asCSSTransitionForSorting =
        [](const Animation& anim,
           const Maybe<EventContext>& aContext) -> const CSSTransition* {
      const CSSTransition* transition = anim.AsCSSTransition();
      return transition && (aContext || transition->IsTiedToMarkup())
                 ? transition
                 : nullptr;
    };
    const auto* const thisTransition =
        asCSSTransitionForSorting(*this, aContext);
    const auto* const otherTransition =
        asCSSTransitionForSorting(aOther, aOtherContext);
    if (thisTransition && otherTransition) {
      return thisTransition->CompareCompositeOrder(aContext, *otherTransition,
                                                   aOtherContext, aCache);
    }
    if (thisTransition || otherTransition) {
      // Cancelled transitions no longer have an owning element. To be strictly
      // correct we should store a strong reference to the owning element
      // so that if we arrive here while sorting cancel events, we can sort
      // them in the correct order.
      //
      // However, given that cancel events are almost always queued
      // synchronously in some deterministic manner, we can be fairly sure
      // that cancel events will be dispatched in a deterministic order
      // (which is our only hard requirement until specs say otherwise).
      // Furthermore, we only reach here when we have events with equal
      // timestamps so this is an edge case we can probably ignore for now.
      return thisTransition ? -1 : 1;
    }
  }

  // 2. CSS Animations sort next
  {
    auto asCSSAnimationForSorting =
        [](const Animation& anim) -> const CSSAnimation* {
      const CSSAnimation* animation = anim.AsCSSAnimation();
      return animation && animation->IsTiedToMarkup() ? animation : nullptr;
    };
    auto thisAnimation = asCSSAnimationForSorting(*this);
    auto otherAnimation = asCSSAnimationForSorting(aOther);
    if (thisAnimation && otherAnimation) {
      return thisAnimation->CompareCompositeOrder(*otherAnimation, aCache);
    }
    if (thisAnimation || otherAnimation) {
      return thisAnimation ? -1 : 1;
    }
  }

  // Subclasses of Animation repurpose mAnimationIndex to implement their
  // own brand of composite ordering. However, by this point we should have
  // handled any such custom composite ordering so we should now have unique
  // animation indices.
  MOZ_ASSERT(mAnimationIndex != aOther.mAnimationIndex,
             "Animation indices should be unique");

  // 3. Finally, generic animations sort by their position in the global
  // animation array.
  return mAnimationIndex > aOther.mAnimationIndex ? 1 : -1;
}

void Animation::WillComposeStyle() {
  mFinishedAtLastComposeStyle = (PlayState() == AnimationPlayState::Finished);

  MOZ_ASSERT(mEffect);

  KeyframeEffect* keyframeEffect = mEffect->AsKeyframeEffect();
  if (keyframeEffect) {
    keyframeEffect->WillComposeStyle();
  }
}

void Animation::ComposeStyle(
    StyleAnimationValueMap& aComposeResult,
    const InvertibleAnimatedPropertyIDSet& aPropertiesToSkip,
    EndpointBehavior aEndpointBehavior) {
  if (!mEffect) {
    return;
  }
  if (mTimeline && mTimeline->IsUnresolvedTimeline()) {
    return;
  }

  // In order to prevent flicker, there are a few cases where we want to use
  // a different time for rendering that would otherwise be returned by
  // GetCurrentTimeAsDuration. These are:
  //
  // (a) For animations that are pausing but which are still running on the
  //     compositor. In this case we send a layer transaction that removes the
  //     animation but which also contains the animation values calculated on
  //     the main thread. To prevent flicker when this occurs we want to ensure
  //     the timeline time used to calculate the main thread animation values
  //     does not lag far behind the time used on the compositor. Ideally we
  //     would like to use the "animation ready time", but for now we just use
  //     the current wallclock time. TODO(emilio): After bug 1864425 it seems we
  //     could just use the ready time, or maybe we can remove this?
  //
  // (b) For animations that are pausing that we have already taken off the
  //     compositor. In this case we record a pending ready time but we don't
  //     apply it until the next tick. However, while waiting for the next tick,
  //     we should still use the pending ready time as the timeline time. If we
  //     use the regular timeline time the animation may appear jump backwards
  //     if the main thread's timeline time lags behind the compositor.
  //
  // (c) For animations that are play-pending due to an aborted pause operation
  //     (i.e. a pause operation that was interrupted before we entered the
  //     paused state). When we cancel a pending pause we might momentarily take
  //     the animation off the compositor, only to re-add it moments later. In
  //     that case the compositor might have been ahead of the main thread so we
  //     should use the current wallclock time to ensure the animation doesn't
  //     temporarily jump backwards.
  //
  // To address each of these cases we temporarily tweak the hold time
  // immediately before updating the style rule and then restore it immediately
  // afterwards. This is purely to prevent visual flicker. Other behavior
  // such as dispatching events continues to rely on the regular timeline time.
  const bool pending = Pending();
  {
    AutoRestore<Nullable<TimeDuration>> restoreHoldTime(mHoldTime);
    if (pending && mHoldTime.IsNull() && !mStartTime.IsNull()) {
      Nullable<TimeDuration> timeToUse;
      if (mTimeline && mTimeline->TracksWallclockTime()) {
        timeToUse = mTimeline->ToTimelineTime(TimeStamp::Now());
      }
      if (!timeToUse.IsNull()) {
        mHoldTime = CurrentTimeFromTimelineTime(
            timeToUse.Value(), mStartTime.Value(), PlaybackRateInternal());
      }
    }

    KeyframeEffect* keyframeEffect = mEffect->AsKeyframeEffect();
    if (keyframeEffect) {
      keyframeEffect->ComposeStyle(aComposeResult, aPropertiesToSkip,
                                   aEndpointBehavior);
    }
  }

  MOZ_ASSERT(
      pending == Pending(),
      "Pending state should not change during the course of compositing");
}

void Animation::NotifyEffectTimingUpdated() {
  MOZ_ASSERT(mEffect,
             "We should only update effect timing when we have a target "
             "effect");
  UpdateTiming(Animation::SeekFlag::NoSeek, Animation::SyncNotifyFlag::Async);
}

void Animation::NotifyEffectPropertiesUpdated() {
  MOZ_ASSERT(mEffect,
             "We should only update effect properties when we have a target "
             "effect");

  MaybeScheduleReplacementCheck();
}

void Animation::NotifyEffectTargetUpdated() {
  MOZ_ASSERT(mEffect,
             "We should only update the effect target when we have a target "
             "effect");

  MaybeScheduleReplacementCheck();
}

// https://drafts.csswg.org/web-animations-2/#playing-an-animation-section
void Animation::PlayNoUpdate(ErrorResult& aRv, LimitBehavior aLimitBehavior) {
  AutoMutationBatchForAnimation mb(*this);

  // 1. Let aborted pause be a boolean flag that is true if animation has a
  //    pending pause task, and false otherwise.
  // 2. Let has pending ready promise be a boolean flag that is initially false.
  // 3. Let has finite timeline be true if animation has an associated timeline
  //    that is not monotonically increasing.
  // 4. Let previous current time be the animation’s current time.
  // 5. Let enable seek be true if the auto-rewind flag is true and has finite
  //    timeline is false. Otherwise, initialize to false.
  const bool abortedPause = mPendingState == PendingState::PausePending;
  bool hasPendingReadyPromise = false;
  const bool hasFiniteTimeline = HasFiniteTimeline();
  const Nullable<TimeDuration> prevCurrentTime = GetCurrentTimeAsDuration();
  const bool autoRewindIsTrue = aLimitBehavior == LimitBehavior::AutoRewind;
  const bool enableSeek = autoRewindIsTrue && !hasFiniteTimeline;

  // 6. Perform the steps corresponding to the first matching condition from the
  // following, if any:
  const double effectivePlaybackRate = CurrentOrPendingPlaybackRate();
  const StickyTimeDuration associatedEffectEnd = EffectEnd();
  if (effectivePlaybackRate > 0.0 && enableSeek &&
      (prevCurrentTime.IsNull() || prevCurrentTime.Value() < TimeDuration() ||
       prevCurrentTime.Value() >= associatedEffectEnd)) {
    // If animation’s effective playback rate > 0, enable seek is true and
    // either animation’s:
    //   - previous current time is unresolved, or
    //   - previous current time < zero, or
    //   - previous current time ≥ associated effect end,
    // Set the animation’s hold time to zero.
    mHoldTime = TimeDuration();
  } else if (effectivePlaybackRate < 0.0 && enableSeek &&
             (prevCurrentTime.IsNull() ||
              prevCurrentTime.Value() <= TimeDuration() ||
              prevCurrentTime.Value() > associatedEffectEnd)) {
    // If animation’s effective playback rate < 0, enable seek is true and
    // either animation’s:
    //   - previous current time is unresolved, or
    //   - previous current time ≤ zero, or
    //   - previous current time > associated effect end,
    // If associated effect end is positive infinity,
    //   throw an "InvalidStateError" DOMException and abort these steps.
    // Otherwise,
    //   Set the animation’s hold time to the animation’s associated effect end.
    if (associatedEffectEnd == TimeDuration::Forever()) {
      return aRv.ThrowInvalidStateError(
          "Can't rewind animation with infinite effect end");
    }
    mHoldTime.SetValue(TimeDuration(associatedEffectEnd));
  } else if (effectivePlaybackRate == 0.0 && prevCurrentTime.IsNull()) {
    // If animation’s effective playback rate = 0 and animation’s current time
    // is unresolved,
    // Set the animation’s hold time to zero.
    mHoldTime = TimeDuration();
  }

  // 7. If has finite timeline and auto-rewind is true:
  if (hasFiniteTimeline && autoRewindIsTrue) {
    // Set the flag auto align start time to true.
    mAutoAlignStartTime = true;
    // Set the animation’s hold time to previous current time.
    mHoldTime = prevCurrentTime;
  }

  // Note: This is a special case mentioned in web-animations-1, but not in
  // web-animations-2. The Gecko's implementation of auto-rewind is slightly
  // different from other browsers when playing a new animation and updating
  // an existing animation. (See [1] for more details.) So we need to set
  // |mHoldTime| for this special case to make sure our behavior matches other
  // browsers, especially for a null timeline with the false auto-rewind flag.
  // [1] https://github.com/w3c/csswg-drafts/issues/7145
  if (!hasFiniteTimeline && prevCurrentTime.IsNull() && mHoldTime.IsNull()) {
    mHoldTime = TimeDuration();
  }

  // 8. If animation’s hold time is resolved, let its start time be unresolved.
  //
  // Clear the start time until we resolve a new one. We do this except
  // for the case where we are aborting a pause and don't have a hold time.
  //
  // If we're aborting a pause and *do* have a hold time (e.g. because
  // the animation is finished or we just applied the auto-rewind behavior
  // above) we should respect it by clearing the start time. If we *don't*
  // have a hold time we should keep the current start time so that the
  // the animation continues moving uninterrupted by the aborted pause.
  if (!mHoldTime.IsNull()) {
    mStartTime.SetNull();
  }

  // 9. If animation has a pending play task or a pending pause task,
  if (mPendingState != PendingState::NotPending) {
    // Cancel that task.
    CancelPendingTasks();
    // Set has pending ready promise to true.
    hasPendingReadyPromise = true;
  }

  // 10. If the following three conditions are all satisfied:
  //   - animation’s hold time is unresolved, and
  //   - aborted pause is false, and
  //   - animation does not have a pending playback rate,
  // abort this procedure.
  //
  // Note:
  // If the hold time is null then we're already playing normally and,
  // typically, we can bail out here.
  //
  // However, there are two cases where we can't do that:
  //
  // (a) If we just aborted a pause. In this case, for consistency, we need to
  //     go through the motions of doing an asynchronous start.
  //
  // (b) If we have timing changes (specifically a change to the playbackRate)
  //     that should be applied asynchronously.
  //
  // FIXME: |pendingAutoAlignedStartTime| is a workaround because the spec
  // missed a condition for finite timelines. See the spec issue for details.
  // https://github.com/w3c/csswg-drafts/issues/10965#issuecomment-2413140700
  auto pendingAutoAlignedStartTime = mAutoAlignStartTime && mStartTime.IsNull();
  if (mHoldTime.IsNull() && !abortedPause && !mPendingPlaybackRate &&
      !pendingAutoAlignedStartTime) {
    return;
  }

  // 11. If has pending ready promise is false, let animation’s current ready
  // promise be a new promise in the relevant Realm of animation.
  if (!hasPendingReadyPromise) {
    // Clear ready promise. We'll create a new one lazily.
    mReady = nullptr;
  }

  // 12. Schedule a task to run as soon as animation is ready.
  // Note: See Animation::ResumeAt() for the details of this step.
  mPendingState = PendingState::PlayPending;
  mPendingReadyTime = {};
  if (Document* doc = GetRenderedDocument()) {
    mPendingReadyTime = EnsurePaintIsScheduled(*doc);
  }

  // 13. Run the procedure to update an animation’s finished state for animation
  // with the did seek flag set to false, and the synchronously notify flag set
  // to false.
  UpdateTiming(SeekFlag::NoSeek, SyncNotifyFlag::Async);
  if (IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(this);
  }
}

// Note: The module level 2 defines some replacements, so we have to take both
// sections into account.
// https://drafts.csswg.org/web-animations-1/#pausing-an-animation-section
// https://drafts.csswg.org/web-animations-2/#pausing-an-animation-section
void Animation::Pause(ErrorResult& aRv) {
  // If animation has a pending pause task, abort these steps.
  // If the play state of animation is paused, abort these steps.
  if (IsPausedOrPausing()) {
    return;
  }

  AutoMutationBatchForAnimation mb(*this);

  // Let has finite timeline be true if animation has an associated timeline
  // that is not monotonically increasing.
  const bool hasFiniteTimeline = HasFiniteTimeline();

  if (GetCurrentTimeAsDuration().IsNull()) {
    if (!hasFiniteTimeline) {
      // If the animation’s current time is unresolved and has finite timeline
      // is false, perform the steps according to the first matching condition
      // below:
      if (PlaybackRateInternal() >= 0.0) {
        // If animation’s playback rate is ≥ 0, set hold time to zero.
        mHoldTime.SetValue(TimeDuration());
      } else {
        if (EffectEnd() == TimeDuration::Forever()) {
          // If associated effect end for animation is positive infinity, throw
          // an "InvalidStateError" DOMException and abort these steps.
          return aRv.ThrowInvalidStateError(
              "Can't seek to infinite effect end");
        }
        // Otherwise, set hold time to animation’s associated effect end.
        mHoldTime.SetValue(TimeDuration(EffectEnd()));
      }
    } else {
      // If has finite timeline is true, and the animation’s current time is
      // unresolved, set the auto align start time flag to true.
      mAutoAlignStartTime = true;
    }
  }

  // Let has pending ready promise be a boolean flag that is initially false.
  bool hasPendingReadyPromise = false;

  // If animation has a pending play task, cancel that task and let has pending
  // ready promise be true.
  if (mPendingState == PendingState::PlayPending) {
    CancelPendingTasks();
    hasPendingReadyPromise = true;
  }

  // If has pending ready promise is false, set animation’s current ready
  // promise to a new promise in the relevant Realm of animation.
  if (!hasPendingReadyPromise) {
    // Clear ready promise. We'll create a new one lazily.
    mReady = nullptr;
  }

  // Schedule the panding pause task. See Animation::PauseAt() for details.
  mPendingState = PendingState::PausePending;
  mPendingReadyTime = {};
  if (Document* doc = GetRenderedDocument()) {
    mPendingReadyTime = EnsurePaintIsScheduled(*doc);
  }

  // Run the procedure to update an animation’s finished state for animation
  // with the did seek flag set to false, and the synchronously notify flag set
  // to false.
  UpdateTiming(SeekFlag::NoSeek, SyncNotifyFlag::Async);
  if (IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(this);
  }

  PostUpdate();
}

// https://drafts.csswg.org/web-animations-2/#playing-an-animation-section
void Animation::ResumeAt(const TimeDuration& aReadyTime) {
  // This method is only expected to be called for an animation that is
  // waiting to play. We can easily adapt it to handle other states
  // but it's currently not necessary.
  MOZ_ASSERT(mPendingState == PendingState::PlayPending,
             "Expected to resume a play-pending animation");
  MOZ_ASSERT(!mHoldTime.IsNull() || !mStartTime.IsNull(),
             "An animation in the play-pending state should have either a"
             " resolved hold time or resolved start time");

  AutoMutationBatchForAnimation mb(*this);
  bool hadPendingPlaybackRate = mPendingPlaybackRate.isSome();

  if (!mHoldTime.IsNull()) {
    // The hold time is set, so we don't need any special handling to preserve
    // the current time.
    ApplyPendingPlaybackRate();
    mStartTime = StartTimeFromTimelineTime(aReadyTime, mHoldTime.Value(),
                                           PlaybackRateInternal());
    if (PlaybackRateInternal() != 0) {
      mHoldTime.SetNull();
    }
  } else if (!mStartTime.IsNull() && mPendingPlaybackRate) {
    // Apply any pending playback rate, preserving the current time.
    TimeDuration currentTimeToMatch = CurrentTimeFromTimelineTime(
        aReadyTime, mStartTime.Value(), PlaybackRateInternal());
    ApplyPendingPlaybackRate();
    mStartTime = StartTimeFromTimelineTime(aReadyTime, currentTimeToMatch,
                                           PlaybackRateInternal());
    if (PlaybackRateInternal() == 0) {
      mHoldTime.SetValue(currentTimeToMatch);
    }
  }

  mPendingState = PendingState::NotPending;

  UpdateTiming(SeekFlag::NoSeek, SyncNotifyFlag::Sync);

  // If we had a pending playback rate, we will have now applied it so we need
  // to notify observers.
  if (hadPendingPlaybackRate && IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(this);
  }

  MaybeResolvePromiseWithThis(mReady);
}

void Animation::PauseAt(const TimeDuration& aReadyTime) {
  MOZ_ASSERT(mPendingState == PendingState::PausePending,
             "Expected to pause a pause-pending animation");

  if (!mStartTime.IsNull() && mHoldTime.IsNull()) {
    mHoldTime = CurrentTimeFromTimelineTime(aReadyTime, mStartTime.Value(),
                                            PlaybackRateInternal());
  }
  ApplyPendingPlaybackRate();
  mStartTime.SetNull();
  mPendingState = PendingState::NotPending;

  UpdateTiming(SeekFlag::NoSeek, SyncNotifyFlag::Async);

  MaybeResolvePromiseWithThis(mReady);
}

void Animation::UpdateTiming(SeekFlag aSeekFlag,
                             SyncNotifyFlag aSyncNotifyFlag) {
  // We call UpdateFinishedState before UpdateEffect because the former
  // can change the current time, which is used by the latter.
  UpdateFinishedState(aSeekFlag, aSyncNotifyFlag);
  UpdateEffect(PostRestyleMode::IfNeeded);

  if (mTimeline) {
    mTimeline->NotifyAnimationUpdated(*this);
  }
}

// https://drafts.csswg.org/web-animations/#update-an-animations-finished-state
void Animation::UpdateFinishedState(SeekFlag aSeekFlag,
                                    SyncNotifyFlag aSyncNotifyFlag) {
  Nullable<TimeDuration> unconstrainedCurrentTime =
      aSeekFlag == SeekFlag::NoSeek ? GetUnconstrainedCurrentTime()
                                    : GetCurrentTimeAsDuration();
  TimeDuration effectEnd = TimeDuration(EffectEnd());

  if (!unconstrainedCurrentTime.IsNull() && !mStartTime.IsNull() &&
      mPendingState == PendingState::NotPending) {
    if (PlaybackRateInternal() > 0.0 &&
        unconstrainedCurrentTime.Value() >= effectEnd) {
      if (aSeekFlag == SeekFlag::DidSeek) {
        mHoldTime = unconstrainedCurrentTime;
      } else if (!mPreviousCurrentTime.IsNull()) {
        mHoldTime.SetValue(std::max(mPreviousCurrentTime.Value(), effectEnd));
      } else {
        mHoldTime.SetValue(effectEnd);
      }
    } else if (PlaybackRateInternal() < 0.0 &&
               unconstrainedCurrentTime.Value() <= TimeDuration()) {
      if (aSeekFlag == SeekFlag::DidSeek) {
        mHoldTime = unconstrainedCurrentTime;
      } else if (!mPreviousCurrentTime.IsNull()) {
        mHoldTime.SetValue(
            std::min(mPreviousCurrentTime.Value(), TimeDuration()));
      } else {
        mHoldTime.SetValue(TimeDuration());
      }
    } else if (PlaybackRateInternal() != 0.0 && mTimeline &&
               !mTimeline->GetCurrentTimeAsDuration().IsNull()) {
      if (aSeekFlag == SeekFlag::DidSeek && !mHoldTime.IsNull()) {
        mStartTime = StartTimeFromTimelineTime(
            mTimeline->GetCurrentTimeAsDuration().Value(), mHoldTime.Value(),
            PlaybackRateInternal());
      }
      mHoldTime.SetNull();
    }
  }

  // We must recalculate the current time to take account of any mHoldTime
  // changes the code above made.
  mPreviousCurrentTime = GetCurrentTimeAsDuration();

  bool currentFinishedState = PlayState() == AnimationPlayState::Finished;
  if (currentFinishedState && !mFinishedIsResolved) {
    DoFinishNotification(aSyncNotifyFlag);
  } else if (!currentFinishedState && mFinishedIsResolved) {
    ResetFinishedPromise();
  }
}

void Animation::UpdateEffect(PostRestyleMode aPostRestyle) {
  if (mEffect) {
    UpdateRelevance();

    KeyframeEffect* keyframeEffect = mEffect->AsKeyframeEffect();
    if (keyframeEffect) {
      keyframeEffect->NotifyAnimationTimingUpdated(aPostRestyle);
    }
  }
}

void Animation::FlushUnanimatedStyle() const {
  if (Document* doc = GetRenderedDocument()) {
    doc->FlushPendingNotifications(
        ChangesToFlush(FlushType::Style, /* aFlushAnimations = */ false,
                       /* aUpdateRelevancy = */ false));
  }
}

void Animation::PostUpdate() {
  if (!mEffect) {
    return;
  }

  KeyframeEffect* keyframeEffect = mEffect->AsKeyframeEffect();
  if (!keyframeEffect) {
    return;
  }
  keyframeEffect->RequestRestyle(EffectCompositor::RestyleType::Layer);
}

void Animation::CancelPendingTasks() {
  mPendingState = PendingState::NotPending;
}

// https://drafts.csswg.org/web-animations/#reset-an-animations-pending-tasks
void Animation::ResetPendingTasks() {
  if (!Pending()) {
    return;
  }

  CancelPendingTasks();
  ApplyPendingPlaybackRate();

  if (mReady) {
    mReady->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
    MOZ_ALWAYS_TRUE(mReady->SetAnyPromiseIsHandled());
    mReady = nullptr;
  }
}

// https://drafts.csswg.org/web-animations-2/#at-timeline-boundary
/* static*/ Animation::ProgressTimelinePosition Animation::AtTimelineBoundary(
    const Nullable<TimeDuration>& aTimelineTime,
    const TimeDuration& aMinimumTimelineTime,
    const TimeDuration& aMaximumTimelineTime) {
  const auto timelineTime =
      aTimelineTime.IsNull() ? TimeDuration{} : aTimelineTime.Value();
  if (AnimationUtils::IsWithinAnimationTimeTolerance(timelineTime,
                                                     aMinimumTimelineTime) ||
      AnimationUtils::IsWithinAnimationTimeTolerance(timelineTime,
                                                     aMaximumTimelineTime)) {
    return ProgressTimelinePosition::Boundary;
  }

  return ProgressTimelinePosition::NotBoundary;
}

Animation::ProgressTimelinePosition Animation::AtTimelineBoundary() const {
  if (!mTimeline || !mTimeline->IsScrollTimeline() ||
      mTimeline->IsUnresolvedTimeline()) {
    // Null or unresolved timelines have no start and end time to speak of.
    // Document timelines technically have range of [-Infinity, Infinity],
    // making them effectively never be at boundaries.
    // https://drafts.csswg.org/web-animations-2/#minimum-timeline-time
    // https://drafts.csswg.org/web-animations-2/#maximum-timeline-time
    return ProgressTimelinePosition::NotBoundary;
  }

  const auto timelineRange =
      mTimeline->AsScrollTimeline()->IntervalForAttachmentRange(mTimelineRange);

  return AtTimelineBoundary(
      mTimeline->GetCurrentTimeAsDuration(),
      TimeDuration::FromMilliseconds(timelineRange.first *
                                     PROGRESS_TIMELINE_DURATION_MILLISEC),
      TimeDuration::FromMilliseconds(timelineRange.second *
                                     PROGRESS_TIMELINE_DURATION_MILLISEC));
}

void Animation::UpdateNormalizedTimingForTimelineDataChange() {
  if (!mEffect) {
    return;
  }

  mEffect->UpdateNormalizedTiming();
}

void Animation::MaybeUpdateKeyframeComputedOffsets() {
  if (!mEffect || !mEffect->AsKeyframeEffect()) {
    return;
  }

  mEffect->AsKeyframeEffect()->MaybeUpdateKeyframeComputedOffsets(
      mTimeline, mTimelineRange);
}

StickyTimeDuration Animation::EffectEnd() const {
  if (!mEffect) {
    return StickyTimeDuration();
  }

  // FIXME: The definition of end time in web-animation-1 is different from that
  // in web-animation-2, which includes the start time. We are still using the
  // definition in web-animation-1 here for now.
  // Note: It seems other browsers still use the definition in web-animation-1
  // as well. Perhaps the new definition is for some other new features?
  return mEffect->NormalizedTiming().EndTime();
}

Document* Animation::GetRenderedDocument() const {
  if (!mEffect || !mEffect->AsKeyframeEffect()) {
    return nullptr;
  }

  return mEffect->AsKeyframeEffect()->GetRenderedDocument();
}

Document* Animation::GetTimelineDocument() const {
  return mTimeline ? mTimeline->GetDocument() : nullptr;
}

class AsyncFinishNotification : public MicroTaskRunnable {
 public:
  explicit AsyncFinishNotification(Animation* aAnimation)
      : mAnimation(aAnimation) {}

  virtual void Run(AutoSlowOperation& aAso) override {
    mAnimation->DoFinishNotificationImmediately(this);
    mAnimation = nullptr;
  }

  virtual bool Suppressed() override {
    nsIGlobalObject* global = mAnimation->GetRelevantGlobal();
    return global && global->IsInSyncOperation();
  }

 private:
  RefPtr<Animation> mAnimation;
};

void Animation::DoFinishNotification(SyncNotifyFlag aSyncNotifyFlag) {
  CycleCollectedJSContext* context = CycleCollectedJSContext::Get();

  if (aSyncNotifyFlag == SyncNotifyFlag::Sync) {
    DoFinishNotificationImmediately();
  } else if (!mFinishNotificationTask) {
    RefPtr<MicroTaskRunnable> runnable = new AsyncFinishNotification(this);
    context->DispatchToMicroTask(do_AddRef(runnable));
    mFinishNotificationTask = std::move(runnable);
  }
}

void Animation::ResetFinishedPromise() {
  mFinishedIsResolved = false;
  mFinished = nullptr;
}

void Animation::MaybeResolveFinishedPromise() {
  mFinishedIsResolved = true;
  MaybeResolvePromiseWithThis(mFinished);
}

void Animation::DoFinishNotificationImmediately(MicroTaskRunnable* aAsync) {
  if (aAsync && aAsync != mFinishNotificationTask) {
    return;
  }

  mFinishNotificationTask = nullptr;

  if (PlayState() != AnimationPlayState::Finished) {
    return;
  }

  MaybeResolveFinishedPromise();

  QueuePlaybackEvent(nsGkAtoms::onfinish,
                     AnimationTimeToTimeStamp(EffectEnd()));
}

void Animation::QueuePlaybackEvent(nsAtom* aOnEvent,
                                   TimeStamp&& aScheduledEventTime) {
  // Use document for timing.
  // https://drafts.csswg.org/web-animations-1/#document-for-timing
  Document* doc = GetTimelineDocument();
  if (!doc) {
    return;
  }

  nsPresContext* presContext = doc->GetPresContext();
  if (!presContext) {
    return;
  }

  Nullable<double> currentTime;
  if (aOnEvent == nsGkAtoms::onfinish || aOnEvent == nsGkAtoms::onremove) {
    currentTime = AnimationUtils::TimeDurationToDouble(
        GetCurrentTimeAsDuration(), mRTPCallerType);
  }

  Nullable<double> timelineTime;
  if (mTimeline) {
    timelineTime = mTimeline->GetCurrentTimeAsDouble();
  }

  presContext->AnimationEventDispatcher()->QueueEvent(
      AnimationEventInfo(aOnEvent, currentTime, timelineTime,
                         std::move(aScheduledEventTime), this));
}

bool Animation::IsRunningOnCompositor() const {
  return mEffect && mEffect->AsKeyframeEffect() &&
         mEffect->AsKeyframeEffect()->IsRunningOnCompositor();
}

bool Animation::HasCurrentEffect() const {
  return GetEffect() && GetEffect()->IsCurrent();
}

bool Animation::IsInEffect() const {
  return GetEffect() && GetEffect()->IsInEffect();
}

void Animation::SetHiddenByContentVisibility(bool hidden) {
  if (mHiddenByContentVisibility == hidden) {
    return;
  }

  mHiddenByContentVisibility = hidden;

  if (!GetTimeline()) {
    return;
  }

  GetTimeline()->NotifyAnimationContentVisibilityChanged(this, !hidden);
}

void Animation::UpdateHiddenByContentVisibility() {
  // To be consistent with nsIFrame::UpdateAnimationVisibility, here we only
  // deal with CSSAnimation and CSSTransition.
  if (!AsCSSAnimation() && !AsCSSTransition()) {
    return;
  }
  NonOwningAnimationTarget target = GetTargetForAnimation();
  if (!target) {
    return;
  }
  // If a CSS animation or CSS transition is no longer associated with an owning
  // element, it behaves like a programmatic web animation, c-v shouldn't hide
  // it.
  bool hasOwningElement = IsMarkupAnimation(AsCSSAnimation()) ||
                          IsMarkupAnimation(AsCSSTransition());
  if (auto* frame = target.mElement->GetPrimaryFrame()) {
    SetHiddenByContentVisibility(
        hasOwningElement && frame->IsHiddenByContentVisibilityOnAnyAncestor());
  }
}

// When updating timeline current time, the start time of any attached
// animation is conditionally updated. For each attached animation, run the
// procedure for calculating an auto-aligned start time.
// https://drafts.csswg.org/scroll-animations-1/#event-loop
// https://drafts.csswg.org/web-animations-2/#auto-aligning-start-time
void Animation::AutoAlignStartTime() {
  // If the auto-align start time flag is false, abort this procedure.
  if (!mAutoAlignStartTime) {
    return;
  }

  // If the timeline is inactive, abort this procedure.
  if (!mTimeline || mTimeline->GetCurrentTimeAsDuration().IsNull()) {
    return;
  }

  MOZ_ASSERT(!mTimeline->IsMonotonicallyIncreasing(),
             "We shouldn't come here for monotonically increasing timeline");
  // Bail out in release builds if we somehow get here with a monotonic
  // timeline, to avoid dereferencing AsScrollTimeline() below.
  if (mTimeline->IsMonotonicallyIncreasing()) {
    return;
  }

  // If play state is idle, abort this procedure.
  const AnimationPlayState playState = PlayState();
  if (playState == AnimationPlayState::Idle) {
    return;
  }

  // If play state is paused, and hold time is resolved, abort this procedure.
  if (playState == AnimationPlayState::Paused && !mHoldTime.IsNull()) {
    return;
  }

  // Let start offset be the resolved timeline time corresponding to the start
  // of the animation attachment range. In the case of view timelines, it
  // requires a calculation based on the proportion of the cover range.
  //
  // Let end offset be the resolved timeline time corresponding to the end of
  // the animation attachment range. In the case of view timelines, it requires
  // a calculation based on the proportion of the cover range.
  MOZ_ASSERT(mTimeline->IsScrollTimeline(),
             "Only the finite timeline sets this flag.");
  const auto [startOffset, endOffset] =
      mTimeline->AsScrollTimeline()->IntervalForAttachmentRange(mTimelineRange);

  // Set start time to start offset if effective playback rate ≥ 0, and end
  // offset otherwise.
  const double effectivePlaybackRate = CurrentOrPendingPlaybackRate();
  mStartTime.SetValue(TimeDuration::FromMilliseconds(
      (effectivePlaybackRate >= 0.0 ? startOffset : endOffset) *
      PROGRESS_TIMELINE_DURATION_MILLISEC));

  // Apply any pending playback rate on animation.
  ApplyPendingPlaybackRate();

  // Clear hold time.
  mHoldTime.SetNull();
}

StickyTimeDuration Animation::IntervalStartTime(
    const StickyTimeDuration& aActiveDuration) const {
  MOZ_ASSERT(AsCSSTransition() || AsCSSAnimation(),
             "Should be called for CSS animations or transitions");
  static constexpr StickyTimeDuration zeroDuration = StickyTimeDuration();
  return std::max(
      std::min(StickyTimeDuration(-mEffect->NormalizedTiming().Delay()),
               aActiveDuration),
      zeroDuration);
}

// Later side of the elapsed time range reported in CSS Animations and CSS
// Transitions events.
//
// https://drafts.csswg.org/css-animations-2/#interval-end
// https://drafts.csswg.org/css-transitions-2/#interval-end
StickyTimeDuration Animation::IntervalEndTime(
    const StickyTimeDuration& aActiveDuration) const {
  MOZ_ASSERT(AsCSSTransition() || AsCSSAnimation(),
             "Should be called for CSS animations or transitions");

  static constexpr StickyTimeDuration zeroDuration = StickyTimeDuration();
  const StickyTimeDuration& effectEnd = EffectEnd();

  // If both "associated effect end" and "start delay" are Infinity, we skip it
  // because we will get NaN when computing "Infinity - Infinity", and
  // using NaN in std::min or std::max is undefined.
  if (MOZ_UNLIKELY(effectEnd == TimeDuration::Forever() &&
                   effectEnd == mEffect->NormalizedTiming().Delay())) {
    // Note: If we use TimeDuration::Forever(), within our animation event
    // handling, we'd end up turning that into a null TimeStamp which can causes
    // errors if we try to do any arithmetic with it. Given that we should never
    // end up _using_ the interval end time. So returning zeroDuration here is
    // probably fine.
    return zeroDuration;
  }

  return std::max(std::min(effectEnd - mEffect->NormalizedTiming().Delay(),
                           aActiveDuration),
                  zeroDuration);
}

double Animation::AnimationsPlayBackRateMultiplier() const {
  if (mEffect && mEffect->AsKeyframeEffect()) {
    return mEffect->AsKeyframeEffect()->AnimationsPlayBackRateMultiplier();
  }
  return 1.0;
}

double Animation::PlaybackRateInternal() const {
  return mPlaybackRate * AnimationsPlayBackRateMultiplier();
}

}  // namespace mozilla::dom
