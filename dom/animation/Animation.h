/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_Animation_h
#define mozilla_dom_Animation_h

#include "X11UndefineNone.h"
#include "mozilla/AnimatedPropertyIDSet.h"
#include "mozilla/AnimationPerformanceWarning.h"
#include "mozilla/Attributes.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/EffectCompositor.h"  // For EffectCompositor::CascadeLevel
#include "mozilla/LinkedList.h"
#include "mozilla/Maybe.h"
#include "mozilla/PostRestyleMode.h"
#include "mozilla/ServoStyleConsts.h"
#include "mozilla/StickyTimeDuration.h"
#include "mozilla/TimeStamp.h"             // for TimeStamp, TimeDuration
#include "mozilla/dom/AnimationBinding.h"  // for AnimationPlayState
#include "mozilla/dom/AnimationTimeline.h"
#include "nsCycleCollectionParticipant.h"

struct JSContext;
class nsCSSPropertyIDSet;
class nsIFrame;
class nsIGlobalObject;
class nsAtom;

namespace mozilla {

struct AnimationRule;
class MicroTaskRunnable;

namespace dom {

class AnimationEffect;
class AsyncFinishNotification;
class CSSAnimation;
class CSSTransition;
class Document;
class Promise;

// The helper struct to hold the animation-range values.
struct AnimationRange {
  StyleAnimationRangeStart mStart = StyleAnimationRangeStart::DefaultStart();
  StyleAnimationRangeEnd mEnd = StyleAnimationRangeEnd::DefaultEnd();
  bool operator==(const AnimationRange& aOther) const {
    return mStart == aOther.mStart && mEnd == aOther.mEnd;
  }
  bool IsNormal() const {
    return mStart.name == StyleTimelineRangeName::Normal &&
           mEnd.name == StyleTimelineRangeName::Normal;
  }
};

class Animation : public DOMEventTargetHelper,
                  public LinkedListElement<Animation> {
 protected:
  virtual ~Animation();

 public:
  explicit Animation(nsIGlobalObject* aGlobal);

  // Constructs a copy of |aOther| with a new effect and timeline.
  // This is only intended to be used while making a static clone of a document
  // during printing, and does not assume that |aOther| is in the same document
  // as any of the other arguments.
  static already_AddRefed<Animation> ClonePausedAnimation(
      nsIGlobalObject* aGlobal, const Animation& aOther,
      AnimationEffect& aEffect, AnimationTimeline& aTimeline);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(Animation, DOMEventTargetHelper)

  nsIGlobalObject* GetParentObject() const { return GetRelevantGlobal(); }

  /**
   * Utility function to get the target (pseudo-)element associated with an
   * animation.
   */
  NonOwningAnimationTarget GetTargetForAnimation() const;

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  virtual CSSAnimation* AsCSSAnimation() { return nullptr; }
  virtual const CSSAnimation* AsCSSAnimation() const { return nullptr; }
  virtual CSSTransition* AsCSSTransition() { return nullptr; }
  virtual const CSSTransition* AsCSSTransition() const { return nullptr; }

  /**
   * Flag to pass to Play to indicate whether or not it should automatically
   * rewind the current time to the start point if the animation is finished.
   * For regular calls to play() from script we should do this, but when a CSS
   * animation's animation-play-state changes we shouldn't rewind the animation.
   */
  enum class LimitBehavior { AutoRewind, Continue };

  // Animation interface methods
  static already_AddRefed<Animation> Constructor(
      const GlobalObject& aGlobal, AnimationEffect* aEffect,
      const Optional<AnimationTimeline*>& aTimeline, ErrorResult& aRv);

  void GetId(nsAString& aResult) const { aResult = mId; }
  void SetId(const nsAString& aId);

  AnimationEffect* GetEffect() const { return mEffect; }
  virtual void SetEffect(AnimationEffect* aEffect);
  void SetEffectNoUpdate(AnimationEffect* aEffect);

  void SetTimeline(AnimationTimeline* aTimeline) {
    // Can't refer to timeline by name from JS side.
    const auto prevTimelineName = GetTimelineName();
    SetTimeline(aTimeline, nullptr);
    if (prevTimelineName) {
      RemovedNamedTimelineReferenceFromJS(prevTimelineName);
    }
  }

  void RemovedNamedTimelineReferenceFromJS(const nsAtom* aName);

  AnimationTimeline* GetTimeline() const { return mTimeline; }
  void SetTimeline(AnimationTimeline* aTimeline, const nsAtom* aTimelineName);
  void SetTimelineNoUpdate(AnimationTimeline* aTimeline,
                           const nsAtom* aTimelineName);

  const AnimationRange& GetTimelineRange() const { return mTimelineRange; }
  void SetTimelineRange(AnimationRange&& aRange);
  void SetTimelineRangeNoUpdate(AnimationRange&& aRange);

  Nullable<TimeDuration> GetStartTime() const { return mStartTime; }
  Nullable<double> GetStartTimeAsDouble() const;
  void SetStartTime(const Nullable<TimeDuration>& aNewStartTime);
  const TimeStamp& GetPendingReadyTime() const { return mPendingReadyTime; }
  void SetPendingReadyTime(const TimeStamp& aReadyTime) {
    mPendingReadyTime = aReadyTime;
  }
  virtual void SetStartTimeAsDouble(const Nullable<double>& aStartTime);

  // This is deliberately _not_ called GetCurrentTime since that would clash
  // with a macro defined in winbase.h
  Nullable<TimeDuration> GetCurrentTimeAsDuration() const {
    return GetCurrentTimeForHoldTime(mHoldTime);
  }
  Nullable<double> GetCurrentTimeAsDouble() const;
  void SetCurrentTime(const TimeDuration& aSeekTime);
  void SetCurrentTimeNoUpdate(const TimeDuration& aSeekTime);
  void SetCurrentTimeAsDouble(const Nullable<double>& aCurrentTime,
                              ErrorResult& aRv);

  Nullable<double> GetOverallProgress() const;

  double PlaybackRate() const { return mPlaybackRate; }
  void SetPlaybackRate(double aPlaybackRate);
  // Returns the playback rate multiplied by
  // BrowsingContext::AnimationsPlayBackRateMultiplier.
  double PlaybackRateInternal() const;

  AnimationPlayState PlayState() const;
  virtual AnimationPlayState PlayStateFromJS() const { return PlayState(); }

  bool Pending() const { return mPendingState != PendingState::NotPending; }
  virtual bool PendingFromJS() const { return Pending(); }
  AnimationReplaceState ReplaceState() const { return mReplaceState; }

  virtual Promise* GetReady(ErrorResult& aRv);
  Promise* GetFinished(ErrorResult& aRv);

  IMPL_EVENT_HANDLER(finish);
  IMPL_EVENT_HANDLER(cancel);
  IMPL_EVENT_HANDLER(remove);

  void Cancel(PostRestyleMode aPostRestyle = PostRestyleMode::IfNeeded);

  void Finish(ErrorResult& aRv);

  void Play(ErrorResult& aRv, LimitBehavior aLimitBehavior);
  virtual void PlayFromJS(ErrorResult& aRv) {
    Play(aRv, LimitBehavior::AutoRewind);
  }

  void Pause(ErrorResult& aRv);
  virtual void PauseFromJS(ErrorResult& aRv) { Pause(aRv); }

  void UpdatePlaybackRate(double aPlaybackRate);
  virtual void Reverse(ErrorResult& aRv);

  void Persist();
  MOZ_CAN_RUN_SCRIPT void CommitStyles(ErrorResult& aRv);

  bool IsRunningOnCompositor() const;

  using TickState = AnimationTimeline::TickState;
  virtual void Tick(TickState&);
  bool NeedsTicks() const {
    return Pending() ||
           (PlayState() == AnimationPlayState::Running &&
            // An animation with a zero playback rate doesn't need ticks even if
            // it is running since it effectively behaves as if it is paused.
            //
            // It's important we return false in this case since a zero playback
            // rate animation in the before or after phase that doesn't fill
            // won't be relevant and hence won't be returned by GetAnimations().
            // We don't want its timeline to keep it alive (which would happen
            // if we return true) since otherwise it will effectively be leaked.
            PlaybackRateInternal() != 0.0) ||
           // Always return true for not idle animations attached to not
           // monotonically increasing timelines even if the animation is
           // finished. This is required to accommodate cases where timeline
           // ticks back in time.
           (mTimeline && !mTimeline->IsMonotonicallyIncreasing() &&
            PlayState() != AnimationPlayState::Idle);
  }
  /**
   * For the monotonically increasing timeline, we use this only for testing:
   * Start or pause a pending animation using the current timeline time. This
   * is used to support existing tests that expect animations to begin
   * immediately. Ideally we would rewrite the those tests and get rid of this
   * method, but there are a lot of them.
   */
  bool TryTriggerNow();
  /**
   * As with the start time, we should use the pending playback rate when
   * producing layer animations.
   */
  double CurrentOrPendingPlaybackRate() const;
  bool HasPendingPlaybackRate() const { return mPendingPlaybackRate.isSome(); }

  /**
   * The following relationship from the definition of the 'current time' is
   * re-used in many algorithms so we extract it here into a static method that
   * can be re-used:
   *
   *   current time = (timeline time - start time) * playback rate
   *
   * As per https://drafts.csswg.org/web-animations-1/#current-time
   */
  static TimeDuration CurrentTimeFromTimelineTime(
      const TimeDuration& aTimelineTime, const TimeDuration& aStartTime,
      double aPlaybackRate) {
    return (aTimelineTime - aStartTime).MultDouble(aPlaybackRate);
  }

  /**
   * As with calculating the current time, we often need to calculate a start
   * time from a current time. The following method simply inverts the current
   * time relationship.
   *
   * In each case where this is used, the desired behavior for playbackRate ==
   * 0 is to return the specified timeline time (often referred to as the ready
   * time).
   */
  static TimeDuration StartTimeFromTimelineTime(
      const TimeDuration& aTimelineTime, const TimeDuration& aCurrentTime,
      double aPlaybackRate) {
    TimeDuration result = aTimelineTime;
    if (aPlaybackRate == 0) {
      return result;
    }

    result -= aCurrentTime.MultDouble(1.0 / aPlaybackRate);
    return result;
  }

  /**
   * Converts a time in the timescale of this Animation's currentTime, to a
   * TimeStamp. Returns a null TimeStamp if the conversion cannot be performed
   * because of the current state of this Animation (e.g. it has no timeline, a
   * zero playbackRate, an unresolved start time etc.) or the value of the time
   * passed-in (e.g. an infinite time).
   */
  TimeStamp AnimationTimeToTimeStamp(const StickyTimeDuration& aTime) const;

  // Converts an AnimationEvent's elapsedTime value to an equivalent TimeStamp
  // that can be used to sort events by when they occurred.
  TimeStamp ElapsedTimeToTimeStamp(
      const StickyTimeDuration& aElapsedTime) const;

  bool IsPausedOrPausing() const {
    return PlayState() == AnimationPlayState::Paused;
  }

  bool HasCurrentEffect() const;
  bool IsInEffect() const;

  bool IsPlaying() const {
    return PlaybackRateInternal() != 0.0 && mTimeline &&
           !mTimeline->GetCurrentTimeAsDuration().IsNull() &&
           PlayState() == AnimationPlayState::Running;
  }

  bool ShouldBeSynchronizedWithMainThread(
      const nsCSSPropertyIDSet& aPropertySet, const nsIFrame* aFrame,
      AnimationPerformanceWarning::Type& aPerformanceWarning /* out */) const;

  bool IsRelevant() const { return mIsRelevant; }
  void UpdateRelevance();

  // https://drafts.csswg.org/web-animations-1/#replaceable-animation
  bool IsReplaceable() const;

  /**
   * Returns true if this Animation satisfies the requirements for being
   * removed when it is replaced.
   *
   * Returning true does not imply this animation _should_ be removed.
   * Determining that depends on the other effects in the same EffectSet to
   * which this animation's effect, if any, contributes.
   */
  bool IsRemovable() const;

  /**
   * Make this animation's target effect no-longer part of the effect stack
   * while preserving its timing information.
   */
  void Remove();

  /**
   * Returns true if this Animation has a lower composite order than aOther.
   */
  struct EventContext {
    NonOwningAnimationTarget mTarget;
    uint64_t mIndex;
  };
  // Note: we provide |aContext|/|aOtherContext| only when it is a cancelled
  // transition or animation (for overridding the target and animation index).
  int32_t CompareCompositeOrder(const Maybe<EventContext>& aContext,
                                const Animation& aOther,
                                const Maybe<EventContext>& aOtherContext,
                                nsContentUtils::NodeIndexCache&) const;
  int32_t CompareCompositeOrder(const Animation& aOther,
                                nsContentUtils::NodeIndexCache& aCache) const {
    return CompareCompositeOrder(Nothing(), aOther, Nothing(), aCache);
  }

  /**
   * Returns the level at which the effect(s) associated with this Animation
   * are applied to the CSS cascade.
   */
  virtual EffectCompositor::CascadeLevel CascadeLevel() const {
    return EffectCompositor::CascadeLevel::Animations;
  }

  /**
   * Returns true if this animation does not currently need to update
   * style on the main thread (e.g. because it is empty, or is
   * running on the compositor).
   */
  bool CanThrottle() const;

  /**
   * Updates various bits of state that we need to update as the result of
   * running ComposeStyle().
   * See the comment of KeyframeEffect::WillComposeStyle for more detail.
   */
  void WillComposeStyle();

  /**
   * Updates |aComposeResult| with the animation values of this animation's
   * effect, if any.
   * Any properties contained in |aPropertiesToSkip| will not be added or
   * updated in |aComposeResult|.
   */
  void ComposeStyle(
      StyleAnimationValueMap& aComposeResult,
      const InvertibleAnimatedPropertyIDSet& aPropertiesToSkip,
      EndpointBehavior aEndpointBehavior = EndpointBehavior::Exclusive);

  void NotifyEffectTimingUpdated();
  void NotifyEffectPropertiesUpdated();
  void NotifyEffectTargetUpdated();

  /**
   * Used by subclasses to synchronously queue a cancel event in situations
   * where the Animation may have been cancelled.
   *
   * We need to do this synchronously because after a CSS animation/transition
   * is canceled, it will be released by its owning element and may not still
   * exist when we would normally go to queue events on the next tick.
   */
  virtual void MaybeQueueCancelEvent(const StickyTimeDuration& aActiveTime) {};

  void SetPartialPrerendered(uint64_t aIdOnCompositor) {
    mIdOnCompositor = aIdOnCompositor;
    mIsPartialPrerendered = true;
  }
  bool IsPartialPrerendered() const { return mIsPartialPrerendered; }
  uint64_t IdOnCompositor() const { return mIdOnCompositor; }
  /**
   * Needs to be called when the pre-rendered animation is going to no longer
   * run on the compositor.
   */
  void ResetPartialPrerendered() {
    MOZ_ASSERT(mIsPartialPrerendered);
    mIsPartialPrerendered = false;
    mIdOnCompositor = 0;
  }
  /**
   * Called via NotifyJankedAnimations IPC call from the compositor to update
   * pre-rendered area on the main-thread.
   */
  void UpdatePartialPrerendered() {
    ResetPartialPrerendered();
    PostUpdate();
  }

  bool UsingScrollTimeline() const {
    return mTimeline && mTimeline->IsScrollTimeline();
  }

  /**
   * Returns true if this is at the progress timeline boundary.
   * https://drafts.csswg.org/web-animations-2/#at-progress-timeline-boundary
   */
  enum class ProgressTimelinePosition : uint8_t { Boundary, NotBoundary };
  static ProgressTimelinePosition AtProgressTimelineBoundary(
      const Nullable<TimeDuration>& aTimelineDuration,
      const Nullable<TimeDuration>& aCurrentTime,
      const TimeDuration& aEffectStartTime, const double aPlaybackRate);
  ProgressTimelinePosition AtProgressTimelineBoundary() const {
    Nullable<TimeDuration> currentTime = GetUnconstrainedCurrentTime();
    return AtProgressTimelineBoundary(
        mTimeline ? mTimeline->TimelineDuration(mTimelineRange) : nullptr,
        // Set unlimited current time based on the first matching condition:
        // 1. start time is resolved:
        //    (timeline time - start time) × playback rate
        // 2. Otherwise:
        //    animation’s current time
        !currentTime.IsNull() ? currentTime : GetCurrentTimeAsDuration(),
        mStartTime.IsNull() ? TimeDuration() : mStartTime.Value(),
        PlaybackRateInternal());
  }

  void UpdateNormalizedTimingForTimelineDataChange();

  void SetHiddenByContentVisibility(bool hidden);
  bool IsHiddenByContentVisibility() const {
    return mHiddenByContentVisibility;
  }
  void UpdateHiddenByContentVisibility();

  DocGroup* GetDocGroup();

  void PostUpdate();

  void AutoAlignStartTime();

  const nsAtom* GetTimelineName() const { return mTimelineName; }

 protected:
  void SilentlySetCurrentTime(const TimeDuration& aNewCurrentTime);
  void CancelNoUpdate();
  void PlayNoUpdate(ErrorResult& aRv, LimitBehavior aLimitBehavior);
  void ResumeAt(const TimeDuration& aReadyTime);
  void PauseAt(const TimeDuration& aReadyTime);
  void FinishPendingAt(const TimeDuration& aReadyTime) {
    if (mPendingState == PendingState::PlayPending) {
      ResumeAt(aReadyTime);
    } else if (mPendingState == PendingState::PausePending) {
      PauseAt(aReadyTime);
    } else {
      MOZ_ASSERT_UNREACHABLE(
          "Can't finish pending if we're not in a pending state");
    }
  }
  void ApplyPendingPlaybackRate() {
    if (mPendingPlaybackRate) {
      mPlaybackRate = mPendingPlaybackRate.extract();
    }
  }

  /**
   * Finishing behavior depends on if changes to timing occurred due
   * to a seek or regular playback.
   */
  enum class SeekFlag { NoSeek, DidSeek };

  enum class SyncNotifyFlag { Sync, Async };

  virtual void UpdateTiming(SeekFlag aSeekFlag, SyncNotifyFlag aSyncNotifyFlag);
  void UpdateFinishedState(SeekFlag aSeekFlag, SyncNotifyFlag aSyncNotifyFlag);
  void UpdateEffect(PostRestyleMode aPostRestyle);
  /**
   * Flush all pending styles other than throttled animation styles (e.g.
   * animations running on the compositor).
   */
  void FlushUnanimatedStyle() const;
  void ResetFinishedPromise();
  void MaybeResolveFinishedPromise();
  void DoFinishNotification(SyncNotifyFlag aSyncNotifyFlag);
  friend class AsyncFinishNotification;
  void DoFinishNotificationImmediately(MicroTaskRunnable* aAsync = nullptr);
  void QueuePlaybackEvent(nsAtom* aOnEvent, TimeStamp&& aScheduledEventTime);
  void MaybeResolvePromiseWithThis(Promise*);

  /**
   * Remove this animation from the pending animation tracker and reset
   * mPendingState as necessary. The caller is responsible for resolving or
   * aborting the mReady promise as necessary.
   */
  void CancelPendingTasks();

  /**
   * Performs the same steps as CancelPendingTasks and also rejects and
   * recreates the ready promise if the animation was pending.
   */
  void ResetPendingTasks();
  StickyTimeDuration EffectEnd() const;

  Nullable<TimeDuration> GetCurrentTimeForHoldTime(
      const Nullable<TimeDuration>& aHoldTime) const;
  Nullable<TimeDuration> GetUnconstrainedCurrentTime() const {
    return GetCurrentTimeForHoldTime(Nullable<TimeDuration>());
  }

  void ScheduleReplacementCheck();
  void MaybeScheduleReplacementCheck();

  // Earlier side of the elapsed time range reported in CSS Animations and CSS
  // Transitions events.
  //
  // https://drafts.csswg.org/css-animations-2/#interval-start
  // https://drafts.csswg.org/css-transitions-2/#interval-start
  StickyTimeDuration IntervalStartTime(
      const StickyTimeDuration& aActiveDuration) const;

  // Later side of the elapsed time range reported in CSS Animations and CSS
  // Transitions events.
  //
  // https://drafts.csswg.org/css-animations-2/#interval-end
  // https://drafts.csswg.org/css-transitions-2/#interval-end
  StickyTimeDuration IntervalEndTime(
      const StickyTimeDuration& aActiveDuration) const;

  TimeStamp GetTimelineCurrentTimeAsTimeStamp() const {
    return mTimeline ? mTimeline->GetCurrentTimeAsTimeStamp() : TimeStamp();
  }

  Document* GetRenderedDocument() const;
  Document* GetTimelineDocument() const;

  bool HasFiniteTimeline() const {
    return mTimeline && !mTimeline->IsMonotonicallyIncreasing();
  }

  RefPtr<AnimationTimeline> mTimeline;
  RefPtr<AnimationEffect> mEffect;
  // The beginning of the delay period.
  Nullable<TimeDuration> mStartTime;            // Timeline timescale
  Nullable<TimeDuration> mHoldTime;             // Animation timescale
  Nullable<TimeDuration> mPreviousCurrentTime;  // Animation timescale
  double mPlaybackRate = 1.0;
  Maybe<double> mPendingPlaybackRate;

  AnimationRange mTimelineRange;

  // A Promise that is replaced on each call to Play()
  // and fulfilled when Play() is successfully completed.
  // This object is lazily created by GetReady.
  // See http://drafts.csswg.org/web-animations/#current-ready-promise
  RefPtr<Promise> mReady;

  // A Promise that is resolved when we reach the end of the effect, or
  // 0 when playing backwards. The Promise is replaced if the animation is
  // finished but then a state change makes it not finished.
  // This object is lazily created by GetFinished.
  // See http://drafts.csswg.org/web-animations/#current-finished-promise
  RefPtr<Promise> mFinished;

  static uint64_t sNextAnimationIndex;

  // The relative position of this animation within the global animation list.
  //
  // Note that subclasses such as CSSTransition and CSSAnimation may repurpose
  // this member to implement their own brand of sorting. As a result, it is
  // possible for two different objects to have the same index.
  uint64_t mAnimationIndex;

  // Indicates if the animation is in the pending state (and what state it is
  // waiting to enter when it finished pending).
  enum class PendingState : uint8_t { NotPending, PlayPending, PausePending };
  PendingState mPendingState = PendingState::NotPending;

  // Handling of this animation's target effect when filling while finished.
  AnimationReplaceState mReplaceState = AnimationReplaceState::Active;

  bool mFinishedAtLastComposeStyle = false;
  bool mWasReplaceableAtLastTick = false;

  bool mHiddenByContentVisibility = false;

  // Indicates that the animation should be exposed in an element's
  // getAnimations() list.
  bool mIsRelevant = false;

  // True if mFinished is resolved or would be resolved if mFinished has
  // yet to be created. This is not set when mFinished is rejected since
  // in that case mFinished is immediately reset to represent a new current
  // finished promise.
  bool mFinishedIsResolved = false;

  RefPtr<MicroTaskRunnable> mFinishNotificationTask;

  nsString mId;

  // Whether the Animation is System, ResistFingerprinting, or neither
  RTPCallerType mRTPCallerType;

  // The time at which our animation should be ready.
  // FIXME: Bug 2017448. We have to make sure what type or value is suitable for
  // pending ready time when using finite timelines because they don't use time
  // values. Perhaps we need to define or use a new type (e.g. CSSNumberish).
  // For now, we skip this for finite timelines and use the current time of
  // the timeline in TryTriggerNow().
  TimeStamp mPendingReadyTime;

 private:
  // Returns BrowsingContext.animationsPlayBackRateMultiplier for this
  // animation.
  double AnimationsPlayBackRateMultiplier() const;

  // The id for this animation on the compositor.
  uint64_t mIdOnCompositor = 0;
  bool mIsPartialPrerendered = false;

  // The flag to indicate that the animation’s start time cannot be reliably
  // calculated until post layout since the start time is to align with the
  // start or end of the animation range.
  bool mAutoAlignStartTime = false;

  // The name of the timeline this animation is referring to, if one exists.
  // Note that animations can have a null timeline but have this set, if it
  // refers to a timeline that does not exist by name.
  RefPtr<const nsAtom> mTimelineName;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_Animation_h
