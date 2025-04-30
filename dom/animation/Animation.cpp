/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Animation.h"

#include "mozilla/Likely.h"
#include "nsIFrame.h"
#include "AnimationUtils.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/dom/AnimationBinding.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/DocumentTimeline.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/AnimationEventDispatcher.h"
#include "mozilla/AnimationTarget.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/DeclarationBlock.h"
#include "mozilla/Maybe.h"  // For Maybe
#include "mozilla/StaticPrefs_dom.h"
#include "nsAnimationManager.h"  // For CSSAnimation
#include "nsComputedDOMStyle.h"
#include "nsDOMMutationObserver.h"    // For nsAutoAnimationMutationBatch
#include "nsDOMCSSAttrDeclaration.h"  // For nsDOMCSSAttributeDeclaration
#include "nsThreadUtils.h"  // For nsRunnableMethod and nsRevocableEventPtr
#include "nsTransitionManager.h"  // For CSSTransition
#include "ScrollTimelineAnimationTracker.h"

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
  animation->SetTimelineNoUpdate(timeline);
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

void Animation::SetTimeline(AnimationTimeline* aTimeline) {
  SetTimelineNoUpdate(aTimeline);
  PostUpdate();
}

// https://drafts.csswg.org/web-animations/#setting-the-timeline
void Animation::SetTimelineNoUpdate(AnimationTimeline* aTimeline) {
  if (mTimeline == aTimeline) {
    return;
  }

  StickyTimeDuration activeTime =
      mEffect ? mEffect->GetComputedTiming().mActiveTime : StickyTimeDuration();

  const AnimationPlayState previousPlayState = PlayState();
  const Nullable<TimeDuration> previousCurrentTime = GetCurrentTimeAsDuration();
  // FIXME: The definition of end time in web-animation-1 is different from that
  // in web-animation-2, which includes the start time. We are still using the
  // definition in web-animation-1 here for now.
  const TimeDuration endTime = TimeDuration(EffectEnd());
  double previousProgress = 0.0;
  if (!previousCurrentTime.IsNull() && !endTime.IsZero()) {
    previousProgress =
        previousCurrentTime.Value().ToSeconds() / endTime.ToSeconds();
  }

  RefPtr<AnimationTimeline> oldTimeline = mTimeline;
  if (oldTimeline) {
    oldTimeline->RemoveAnimation(this);
  }

  mTimeline = aTimeline;

  mResetCurrentTimeOnResume = false;

  if (mEffect) {
    mEffect->UpdateNormalizedTiming();
  }

  if (mTimeline && !mTimeline->IsMonotonicallyIncreasing()) {
    // If "to finite timeline" is true.

    ApplyPendingPlaybackRate();
    Nullable<TimeDuration> seekTime;
    if (mPlaybackRate >= 0.0) {
      seekTime.SetValue(TimeDuration());
    } else {
      seekTime.SetValue(TimeDuration(EffectEnd()));
    }

    switch (previousPlayState) {
      case AnimationPlayState::Running:
      case AnimationPlayState::Finished:
        mStartTime = seekTime;
        break;
      case AnimationPlayState::Paused:
        if (!previousCurrentTime.IsNull()) {
          mResetCurrentTimeOnResume = true;
          mStartTime.SetNull();
          mHoldTime.SetValue(
              TimeDuration(EffectEnd().MultDouble(previousProgress)));
        } else {
          mStartTime = seekTime;
        }
        break;
      case AnimationPlayState::Idle:
      default:
        break;
    }
  } else if (oldTimeline && !oldTimeline->IsMonotonicallyIncreasing() &&
             !previousCurrentTime.IsNull()) {
    // If "from finite timeline" and previous progress is resolved.
    SetCurrentTimeNoUpdate(
        TimeDuration(EffectEnd().MultDouble(previousProgress)));
  }

  if (!mStartTime.IsNull()) {
    mHoldTime.SetNull();
  }

  if (!aTimeline) {
    MaybeQueueCancelEvent(activeTime);
  }

  UpdateScrollTimelineAnimationTracker(oldTimeline, aTimeline);

  UpdateTiming(SeekFlag::NoSeek, SyncNotifyFlag::Async);

  // FIXME: Bug 1799071: Check if we need to add
  // MutationObservers::NotifyAnimationChanged(this) here.
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

  mResetCurrentTimeOnResume = false;

  if (!aNewStartTime.IsNull()) {
    if (mPlaybackRate != 0.0) {
      mHoldTime.SetNull();
    }
  } else {
    mHoldTime = previousCurrentTime;
  }

  CancelPendingTasks();
  if (mReady) {
    // We may have already resolved mReady, but in that case calling
    // MaybeResolve is a no-op, so that's okay.
    mReady->MaybeResolve(this);
  }

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
      result = CurrentTimeFromTimelineTime(timelineTime.Value(),
                                           mStartTime.Value(), mPlaybackRate);
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

    if (mReady) {
      mReady->MaybeResolve(this);
    }
    CancelPendingTasks();
  }

  UpdateTiming(SeekFlag::DidSeek, SyncNotifyFlag::Async);
}

// https://drafts.csswg.org/web-animations/#set-the-playback-rate
void Animation::SetPlaybackRate(double aPlaybackRate) {
  mPendingPlaybackRate.reset();

  if (aPlaybackRate == mPlaybackRate) {
    return;
  }

  AutoMutationBatchForAnimation mb(*this);

  Nullable<TimeDuration> previousTime = GetCurrentTimeAsDuration();
  mPlaybackRate = aPlaybackRate;
  if (!previousTime.IsNull()) {
    SetCurrentTime(previousTime.Value());
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
  nsCOMPtr<nsIGlobalObject> global = GetOwnerGlobal();
  if (!mReady && global) {
    mReady = Promise::Create(global, aRv);  // Lazily create on demand
  }
  if (!mReady) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  if (!Pending()) {
    mReady->MaybeResolve(this);
  }
  return mReady;
}

Promise* Animation::GetFinished(ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = GetOwnerGlobal();
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
      mPlaybackRate > 0 ? TimeDuration(EffectEnd()) : TimeDuration(0);
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
    mStartTime = StartTimeFromTimelineTime(
        mTimeline->GetCurrentTimeAsDuration().Value(), limit, mPlaybackRate);
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
    if (mReady) {
      mReady->MaybeResolve(this);
    }
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

// https://drafts.csswg.org/web-animations/#reverse-an-animation
void Animation::Reverse(ErrorResult& aRv) {
  if (!mTimeline) {
    return aRv.ThrowInvalidStateError(
        "Can't reverse an animation with no associated timeline");
  }
  if (mTimeline->GetCurrentTimeAsDuration().IsNull()) {
    return aRv.ThrowInvalidStateError(
        "Can't reverse an animation associated with an inactive timeline");
  }

  double effectivePlaybackRate = CurrentOrPendingPlaybackRate();

  if (effectivePlaybackRate == 0.0) {
    return;
  }

  Maybe<double> originalPendingPlaybackRate = mPendingPlaybackRate;

  mPendingPlaybackRate = Some(-effectivePlaybackRate);

  Play(aRv, LimitBehavior::AutoRewind);

  // If Play() threw, restore state and don't report anything to mutation
  // observers.
  if (aRv.Failed()) {
    mPendingPlaybackRate = originalPendingPlaybackRate;
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

  // Count how often the endpoint-inclusive behavior makes a difference so we
  // can gauge if it is Web-compatible.
  auto computedTimingWithEndpointIncluded =
      keyframeEffect->GetComputedTiming(nullptr, EndpointBehavior::Inclusive);
  auto computedTimingWithEndpointExcluded =
      keyframeEffect->GetComputedTiming(nullptr, EndpointBehavior::Exclusive);
  if (computedTimingWithEndpointIncluded.mProgress !=
      computedTimingWithEndpointExcluded.mProgress) {
    doc->SetUseCounter(eUseCounter_custom_CommitStylesNonFillingFinalValue);
  }

  // Calling SetCSSDeclaration will trigger attribute setting code.
  // Start the update now so that the old rule doesn't get used
  // between when we mutate the declaration and when we set the new
  // rule.
  mozAutoDocUpdate autoUpdate(target.mElement->OwnerDoc(), true);

  // Get the inline style to append to
  RefPtr<DeclarationBlock> declarationBlock;
  if (auto* existing = target.mElement->GetInlineStyleDeclaration()) {
    declarationBlock = existing->EnsureMutable();
  } else {
    declarationBlock = new DeclarationBlock();
    declarationBlock->SetDirty();
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
  for (const AnimatedPropertyID& property : properties) {
    RefPtr<StyleAnimationValue> computedValue =
        Servo_AnimationValueMap_GetValue(animationValues.get(), &property)
            .Consume();
    if (computedValue) {
      changed |= Servo_DeclarationBlock_SetPropertyToAnimationValue(
          declarationBlock->Raw(), computedValue, beforeChangeClosure);
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

Nullable<double> Animation::GetStartTimeAsDouble() const {
  return AnimationUtils::TimeDurationToDouble(mStartTime, mRTPCallerType);
}

void Animation::SetStartTimeAsDouble(const Nullable<double>& aStartTime) {
  return SetStartTime(AnimationUtils::DoubleToTimeDuration(aStartTime));
}

Nullable<double> Animation::GetCurrentTimeAsDouble() const {
  return AnimationUtils::TimeDurationToDouble(GetCurrentTimeAsDuration(),
                                              mRTPCallerType);
}

void Animation::SetCurrentTimeAsDouble(const Nullable<double>& aCurrentTime,
                                       ErrorResult& aRv) {
  if (aCurrentTime.IsNull()) {
    if (!GetCurrentTimeAsDuration().IsNull()) {
      aRv.ThrowTypeError(
          "Current time is resolved but trying to set it to an unresolved "
          "time");
    }
    return;
  }

  return SetCurrentTime(TimeDuration::FromMilliseconds(aCurrentTime.Value()));
}

// ---------------------------------------------------------------------------

void Animation::Tick(AnimationTimeline::TickState& aTickState) {
  if (Pending()) {
    if (!mPendingReadyTime.IsNull()) {
      TryTriggerNow();
    } else if (MOZ_LIKELY(mTimeline)) {
      // Makes sure that we trigger the animation on the next tick but,
      // importantly, with this tick's timestamp.
      mPendingReadyTime = mTimeline->GetCurrentTimeAsTimeStamp();
    }
  }

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

bool Animation::TryTriggerNow() {
  if (!Pending()) {
    return true;
  }
  // If we don't have an active timeline we can't trigger the animation.
  if (NS_WARN_IF(!mTimeline)) {
    return false;
  }
  auto currentTime = mPendingReadyTime.IsNull()
                         ? mTimeline->GetCurrentTimeAsDuration()
                         : mTimeline->ToTimelineTime(mPendingReadyTime);
  mPendingReadyTime = {};
  if (NS_WARN_IF(currentTime.IsNull())) {
    return false;
  }
  FinishPendingAt(currentTime.Value());
  return true;
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
  if (aTime == TimeDuration::Forever() || mPlaybackRate == 0.0 ||
      mStartTime.IsNull()) {
    return result;
  }

  // Invert the standard relation:
  //   current time = (timeline time - start time) * playback rate
  TimeDuration timelineTime =
      TimeDuration(aTime).MultDouble(1.0 / mPlaybackRate) + mStartTime.Value();

  result = mTimeline->ToTimeStamp(timelineTime);
  return result;
}

TimeStamp Animation::ElapsedTimeToTimeStamp(
    const StickyTimeDuration& aElapsedTime) const {
  TimeDuration delay =
      mEffect ? mEffect->NormalizedTiming().Delay() : TimeDuration();
  return AnimationTimeToTimeStamp(aElapsedTime + delay);
}

// https://drafts.csswg.org/web-animations/#silently-set-the-current-time
void Animation::SilentlySetCurrentTime(const TimeDuration& aSeekTime) {
  // TODO: Bug 1762238: Introduce "valid seek time" after introducing
  // CSSNumberish time values.
  // https://drafts.csswg.org/web-animations-2/#silently-set-the-current-time

  if (!mHoldTime.IsNull() || mStartTime.IsNull() || !mTimeline ||
      mTimeline->GetCurrentTimeAsDuration().IsNull() || mPlaybackRate == 0.0) {
    mHoldTime.SetValue(aSeekTime);
  } else {
    mStartTime =
        StartTimeFromTimelineTime(mTimeline->GetCurrentTimeAsDuration().Value(),
                                  aSeekTime, mPlaybackRate);
  }

  if (!mTimeline || mTimeline->GetCurrentTimeAsDuration().IsNull()) {
    mStartTime.SetNull();
  }

  mPreviousCurrentTime.SetNull();
  mResetCurrentTimeOnResume = false;
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
            timeToUse.Value(), mStartTime.Value(), mPlaybackRate);
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

// https://drafts.csswg.org/web-animations/#play-an-animation
void Animation::PlayNoUpdate(ErrorResult& aRv, LimitBehavior aLimitBehavior) {
  AutoMutationBatchForAnimation mb(*this);

  const bool isAutoRewind = aLimitBehavior == LimitBehavior::AutoRewind;
  const bool abortedPause = mPendingState == PendingState::PausePending;
  double effectivePlaybackRate = CurrentOrPendingPlaybackRate();

  Nullable<TimeDuration> currentTime = GetCurrentTimeAsDuration();
  if (mResetCurrentTimeOnResume) {
    currentTime.SetNull();
    mResetCurrentTimeOnResume = false;
  }

  Nullable<TimeDuration> seekTime;
  if (isAutoRewind) {
    if (effectivePlaybackRate >= 0.0 &&
        (currentTime.IsNull() || currentTime.Value() < TimeDuration() ||
         currentTime.Value() >= EffectEnd())) {
      seekTime.SetValue(TimeDuration());
    } else if (effectivePlaybackRate < 0.0 &&
               (currentTime.IsNull() || currentTime.Value() <= TimeDuration() ||
                currentTime.Value() > EffectEnd())) {
      if (EffectEnd() == TimeDuration::Forever()) {
        return aRv.ThrowInvalidStateError(
            "Can't rewind animation with infinite effect end");
      }
      seekTime.SetValue(TimeDuration(EffectEnd()));
    }
  }

  if (seekTime.IsNull() && mStartTime.IsNull() && currentTime.IsNull()) {
    seekTime.SetValue(TimeDuration());
  }

  if (!seekTime.IsNull()) {
    if (HasFiniteTimeline()) {
      mStartTime = seekTime;
      mHoldTime.SetNull();
      ApplyPendingPlaybackRate();
    } else {
      mHoldTime = seekTime;
    }
  }

  bool reuseReadyPromise = false;
  if (mPendingState != PendingState::NotPending) {
    CancelPendingTasks();
    reuseReadyPromise = true;
  }

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
  if (mHoldTime.IsNull() && seekTime.IsNull() && !abortedPause &&
      !mPendingPlaybackRate) {
    return;
  }

  // Clear the start time until we resolve a new one. We do this except
  // for the case where we are aborting a pause and don't have a hold time.
  //
  // If we're aborting a pause and *do* have a hold time (e.g. because
  // the animation is finished or we just applied the auto-rewind behavior
  // above) we should respect it by clearing the start time. If we *don't*
  // have a hold time we should keep the current start time so that the
  // the animation continues moving uninterrupted by the aborted pause.
  //
  // (If we're not aborting a pause, mHoldTime must be resolved by now
  //  or else we would have returned above.)
  if (!mHoldTime.IsNull()) {
    mStartTime.SetNull();
  }

  if (!reuseReadyPromise) {
    // Clear ready promise. We'll create a new one lazily.
    mReady = nullptr;
  }

  mPendingState = PendingState::PlayPending;
  mPendingReadyTime = {};
  if (Document* doc = GetRenderedDocument()) {
    if (HasFiniteTimeline()) {
      // Always schedule a task even if we would like to let this animation
      // immediately ready, per spec.
      // https://drafts.csswg.org/web-animations/#playing-an-animation-section
      // If there's no rendered document, we fail to track this animation, so
      // let the scroll frame to trigger it when ticking.
      doc->GetOrCreateScrollTimelineAnimationTracker()->AddPending(*this);
    }
    // Make sure to try to schedule a tick.
    mPendingReadyTime = EnsurePaintIsScheduled(*doc);
  }

  UpdateTiming(SeekFlag::NoSeek, SyncNotifyFlag::Async);
  if (IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(this);
  }
}

// https://drafts.csswg.org/web-animations/#pause-an-animation
void Animation::Pause(ErrorResult& aRv) {
  if (IsPausedOrPausing()) {
    return;
  }

  AutoMutationBatchForAnimation mb(*this);

  Nullable<TimeDuration> seekTime;
  // If we are transitioning from idle, fill in the current time
  if (GetCurrentTimeAsDuration().IsNull()) {
    if (mPlaybackRate >= 0.0) {
      seekTime.SetValue(TimeDuration(0));
    } else {
      if (EffectEnd() == TimeDuration::Forever()) {
        return aRv.ThrowInvalidStateError("Can't seek to infinite effect end");
      }
      seekTime.SetValue(TimeDuration(EffectEnd()));
    }
  }

  if (!seekTime.IsNull()) {
    if (HasFiniteTimeline()) {
      mStartTime = seekTime;
    } else {
      mHoldTime = seekTime;
    }
  }

  bool reuseReadyPromise = false;
  if (mPendingState == PendingState::PlayPending) {
    CancelPendingTasks();
    reuseReadyPromise = true;
  }

  if (!reuseReadyPromise) {
    // Clear ready promise. We'll create a new one lazily.
    mReady = nullptr;
  }

  mPendingState = PendingState::PausePending;
  mPendingReadyTime = {};
  // See the relevant PlayPending code for comments.
  if (Document* doc = GetRenderedDocument()) {
    if (HasFiniteTimeline()) {
      doc->GetOrCreateScrollTimelineAnimationTracker()->AddPending(*this);
    }
    mPendingReadyTime = EnsurePaintIsScheduled(*doc);
  }

  UpdateTiming(SeekFlag::NoSeek, SyncNotifyFlag::Async);
  if (IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(this);
  }

  PostUpdate();
}

// https://drafts.csswg.org/web-animations/#play-an-animation
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
    mStartTime =
        StartTimeFromTimelineTime(aReadyTime, mHoldTime.Value(), mPlaybackRate);
    if (mPlaybackRate != 0) {
      mHoldTime.SetNull();
    }
  } else if (!mStartTime.IsNull() && mPendingPlaybackRate) {
    // Apply any pending playback rate, preserving the current time.
    TimeDuration currentTimeToMatch = CurrentTimeFromTimelineTime(
        aReadyTime, mStartTime.Value(), mPlaybackRate);
    ApplyPendingPlaybackRate();
    mStartTime = StartTimeFromTimelineTime(aReadyTime, currentTimeToMatch,
                                           mPlaybackRate);
    if (mPlaybackRate == 0) {
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

  if (mReady) {
    mReady->MaybeResolve(this);
  }
}

void Animation::PauseAt(const TimeDuration& aReadyTime) {
  MOZ_ASSERT(mPendingState == PendingState::PausePending,
             "Expected to pause a pause-pending animation");

  if (!mStartTime.IsNull() && mHoldTime.IsNull()) {
    mHoldTime = CurrentTimeFromTimelineTime(aReadyTime, mStartTime.Value(),
                                            mPlaybackRate);
  }
  ApplyPendingPlaybackRate();
  mStartTime.SetNull();
  mPendingState = PendingState::NotPending;

  UpdateTiming(SeekFlag::NoSeek, SyncNotifyFlag::Async);

  if (mReady) {
    mReady->MaybeResolve(this);
  }
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
    if (mPlaybackRate > 0.0 && unconstrainedCurrentTime.Value() >= effectEnd) {
      if (aSeekFlag == SeekFlag::DidSeek) {
        mHoldTime = unconstrainedCurrentTime;
      } else if (!mPreviousCurrentTime.IsNull()) {
        mHoldTime.SetValue(std::max(mPreviousCurrentTime.Value(), effectEnd));
      } else {
        mHoldTime.SetValue(effectEnd);
      }
    } else if (mPlaybackRate < 0.0 &&
               unconstrainedCurrentTime.Value() <= TimeDuration()) {
      if (aSeekFlag == SeekFlag::DidSeek) {
        mHoldTime = unconstrainedCurrentTime;
      } else if (!mPreviousCurrentTime.IsNull()) {
        mHoldTime.SetValue(
            std::min(mPreviousCurrentTime.Value(), TimeDuration(0)));
      } else {
        mHoldTime.SetValue(0);
      }
    } else if (mPlaybackRate != 0.0 && mTimeline &&
               !mTimeline->GetCurrentTimeAsDuration().IsNull()) {
      if (aSeekFlag == SeekFlag::DidSeek && !mHoldTime.IsNull()) {
        mStartTime = StartTimeFromTimelineTime(
            mTimeline->GetCurrentTimeAsDuration().Value(), mHoldTime.Value(),
            mPlaybackRate);
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

// https://drafts.csswg.org/web-animations-2/#at-progress-timeline-boundary
/* static*/ Animation::ProgressTimelinePosition
Animation::AtProgressTimelineBoundary(
    const Nullable<TimeDuration>& aTimelineDuration,
    const Nullable<TimeDuration>& aCurrentTime,
    const TimeDuration& aEffectStartTime, const double aPlaybackRate) {
  // Based on changed defined in: https://github.com/w3c/csswg-drafts/pull/6702
  // 1.  If any of the following conditions are true:
  //     * the associated animation's timeline is not a progress-based timeline,
  //     or
  //     * the associated animation's timeline duration is unresolved or zero,
  //     or
  //     * the animation's playback rate is zero
  //     return false
  // Note: We can detect a progress-based timeline by relying on the fact that
  // monotonic timelines (i.e. non-progress-based timelines) have an unresolved
  // timeline duration.
  if (aTimelineDuration.IsNull() || aTimelineDuration.Value().IsZero() ||
      aPlaybackRate == 0.0) {
    return ProgressTimelinePosition::NotBoundary;
  }

  // 2.  Let effective start time be the animation's start time if resolved, or
  // zero otherwise.
  const TimeDuration& effectiveStartTime = aEffectStartTime;

  // 3.  Let effective timeline time be (animation's current time / animation's
  // playback rate) + effective start time.
  // Note: we use zero if the current time is unresolved. See the spec issue:
  // https://github.com/w3c/csswg-drafts/issues/7458
  const TimeDuration effectiveTimelineTime =
      (aCurrentTime.IsNull()
           ? TimeDuration()
           : aCurrentTime.Value().MultDouble(1.0 / aPlaybackRate)) +
      effectiveStartTime;

  // 4.  Let effective timeline progress be (effective timeline time / timeline
  // duration)
  // 5.  If effective timeline progress is 0 or 1, return true,
  // We avoid the division here but it is effectively the same as 4 & 5 above.
  return effectiveTimelineTime.IsZero() ||
                 (AnimationUtils::IsWithinAnimationTimeTolerance(
                     effectiveTimelineTime, aTimelineDuration.Value()))
             ? ProgressTimelinePosition::Boundary
             : ProgressTimelinePosition::NotBoundary;
}

StickyTimeDuration Animation::EffectEnd() const {
  if (!mEffect) {
    return StickyTimeDuration(0);
  }

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

void Animation::UpdateScrollTimelineAnimationTracker(
    AnimationTimeline* aOldTimeline, AnimationTimeline* aNewTimeline) {
  // If we are still in pending, we may have to move this animation into the
  // correct animation tracker.
  Document* doc = GetRenderedDocument();
  if (!doc || !Pending()) {
    return;
  }

  const bool fromFiniteTimeline =
      aOldTimeline && !aOldTimeline->IsMonotonicallyIncreasing();
  const bool toFiniteTimeline =
      aNewTimeline && !aNewTimeline->IsMonotonicallyIncreasing();
  if (fromFiniteTimeline == toFiniteTimeline) {
    return;
  }

  if (toFiniteTimeline) {
    doc->GetOrCreateScrollTimelineAnimationTracker()->AddPending(*this);
  } else {
    // From scroll-timeline to null/document-timeline
    if (auto* tracker = doc->GetScrollTimelineAnimationTracker()) {
      tracker->RemovePending(*this);
    }
    EnsurePaintIsScheduled(*doc);
  }
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
    nsIGlobalObject* global = mAnimation->GetOwnerGlobal();
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
  if (mFinished) {
    mFinished->MaybeResolve(this);
  }
  mFinishedIsResolved = true;
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
    currentTime = GetCurrentTimeAsDouble();
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

}  // namespace mozilla::dom
