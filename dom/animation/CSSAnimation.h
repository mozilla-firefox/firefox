/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_CSSAnimation_h
#define mozilla_dom_CSSAnimation_h

#include "AnimationCommon.h"
#include "mozilla/StyleAnimationValue.h"
#include "mozilla/dom/Animation.h"
#include "mozilla/dom/KeyframeEffect.h"
#include "mozilla/dom/MutationObservers.h"

namespace mozilla {

namespace dom {

class CSSAnimation final : public Animation {
 public:
  CSSAnimation(nsIGlobalObject* aGlobal, nsAtom* aAnimationName,
               bool aIsStylePaused)
      : dom::Animation(aGlobal),
        mAnimationName(aAnimationName),
        mIsStylePaused(aIsStylePaused) {
    // We might need to drop this assertion once we add a script-accessible
    // constructor but for animations generated from CSS markup the
    // animation-name should never be empty.
    MOZ_ASSERT(mAnimationName != nsGkAtoms::_empty,
               "animation-name should not be 'none'");
  }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  CSSAnimation* AsCSSAnimation() override { return this; }
  const CSSAnimation* AsCSSAnimation() const override { return this; }

  // CSSAnimation interface
  void GetAnimationName(nsString& aRetVal) const {
    mAnimationName->ToString(aRetVal);
  }

  nsAtom* AnimationName() const { return mAnimationName; }

  // Animation interface overrides
  void SetEffect(AnimationEffect* aEffect) override;
  void SetStartTime(const Nullable<CSSNumberish>& aStartTime,
                    ErrorResult& aRv) override;
  Promise* GetReady(ErrorResult& aRv) override;
  void Reverse(ErrorResult& aRv) override;

  // NOTE: tabbrowser.xml currently relies on the fact that reading the
  // currentTime of a CSSAnimation does *not* flush style (whereas reading the
  // playState does). If CSS Animations 2 specifies that reading currentTime
  // also flushes style we will need to find another way to detect canceled
  // animations in tabbrowser.xml. On the other hand, if CSS Animations 2
  // specifies that reading playState does *not* flush style (and we drop the
  // following override), then we should update tabbrowser.xml to check
  // the playState instead.
  AnimationPlayState PlayStateFromJS() const override;
  bool PendingFromJS() const override;
  void PlayFromJS(ErrorResult& aRv) override;
  void PauseFromJS(ErrorResult& aRv) override;

  void PlayFromStyle();
  void PauseFromStyle();
  void CancelFromStyle(PostRestyleMode aPostRestyle);
  // Drop this animation's reference to the named timeline aName from
  // nsAnimationManager, which otherwise keeps updating it.
  void RemovedNamedTimelineReference(const nsAtom* aName);

  void Tick(TickState&) override;
  void QueueEvents(
      const StickyTimeDuration& aActiveTime = StickyTimeDuration());

  int32_t CompareCompositeOrder(const CSSAnimation& aOther,
                                nsContentUtils::NodeIndexCache&) const;

  void SetAnimationIndex(uint64_t aIndex) {
    MOZ_ASSERT(IsTiedToMarkup());
    if (IsRelevant() && mAnimationIndex != aIndex) {
      MutationObservers::NotifyAnimationChanged(this);
      PostUpdate();
    }
    mAnimationIndex = aIndex;
  }

  // Sets the owning element which is used for determining the composite
  // order of CSSAnimation objects generated from CSS markup.
  //
  // @see mOwningElement
  void SetOwningElement(const OwningElementRef& aElement) {
    mOwningElement = aElement;
  }
  // True for animations that are generated from CSS markup and continue to
  // reflect changes to that markup.
  bool IsTiedToMarkup() const { return mOwningElement.IsSet(); }

  void MaybeQueueCancelEvent(const StickyTimeDuration& aActiveTime) override {
    QueueEvents(aActiveTime);
  }

  void PropertiesWillSetFromJS(CSSAnimationProperties aProperties) override {
    mOverriddenProperties |= aProperties;
  }
  CSSAnimationProperties PropertiesOverridenByJS() const override {
    return mOverriddenProperties;
  }

  // Try to sync with the new style and update the current CSSAnimation. This
  // ignores the overridden properties.
  void UpdatePropertiesWithNew(
      const NonOwningAnimationTarget& aTarget,
      const ComputedStyle* aComputedStyle, TimingParams&& aNewTiming,
      nsTArray<Keyframe>&& aNewKeyframes,
      const StyleComputedTimingFunction& aNewTimingFunction,
      CompositeOperation aNewComposite, AnimationTimeline* aNewTimeline,
      const dom::ScopedTimelineName& aNewTimelineName,
      dom::AnimationRange&& aNewRange, bool aNewIsStylePaused);

 protected:
  virtual ~CSSAnimation() {
    MOZ_ASSERT(!mOwningElement.IsSet(),
               "Owning element should be cleared "
               "before a CSS animation is destroyed");
  }

  // Animation overrides
  void UpdateTiming(SeekFlag aSeekFlag,
                    SyncNotifyFlag aSyncNotifyFlag) override;

  // Returns the duration from the start of the animation's source effect's
  // active interval to the point where the animation actually begins playback.
  // This is zero unless the animation's source effect has a negative delay in
  // which case it is the absolute value of that delay.
  // This is used for setting the elapsedTime member of CSS AnimationEvents.
  TimeDuration InitialAdvance() const {
    return mEffect ? std::max(TimeDuration(),
                              mEffect->NormalizedTiming().Delay() * -1)
                   : TimeDuration();
  }

  RefPtr<nsAtom> mAnimationName;

  // The (pseudo-)element whose computed animation-name refers to this
  // animation (if any).
  //
  // This is used for determining the relative composite order of animations
  // generated from CSS markup.
  //
  // Typically this will be the same as the target element of the keyframe
  // effect associated with this animation. However, it can differ in the
  // following circumstances:
  //
  // a) If script removes or replaces the effect of this animation,
  // b) If this animation is cancelled (e.g. by updating the
  //    animation-name property or removing the owning element from the
  //    document),
  // c) If this object is generated from script using the CSSAnimation
  //    constructor.
  //
  // For (b) and (c) the owning element will return !IsSet().
  OwningElementRef mOwningElement;

  uint64_t mPreviousIteration = 0;

  // Properties that would normally be defined by the cascade but which have
  // since been explicitly set via the Web Animations API.
  CSSAnimationProperties mOverriddenProperties = CSSAnimationProperties::None;

  // Phase and current iteration from the previous time we queued events.
  // This is used to determine what new events to dispatch.
  ComputedTiming::AnimationPhase mPreviousPhase =
      ComputedTiming::AnimationPhase::Idle;

  // When true, indicates that when this animation next leaves the idle state,
  // its animation index should be updated.
  bool mNeedsNewAnimationIndexWhenRun = false;

  // The animation-play-state for this CSSAnimation. We cache this to determine
  // if we need to play or pause the animation when there are any animation
  // style changes.
  // Note: We cannot just use Animation::PlayState() which may be affected by
  // the timeline, e.g. null timeline may make it paused even if
  // animation-play-state is running.
  bool mIsStylePaused = false;
};

// A subclass of KeyframeEffect that reports when specific properties have been
// overridden via the Web Animations API.
class CSSAnimationKeyframeEffect : public KeyframeEffect {
 public:
  CSSAnimationKeyframeEffect(Document* aDocument,
                             OwningAnimationTarget&& aTarget,
                             TimingParams&& aTiming,
                             const KeyframeEffectParams& aOptions)
      : KeyframeEffect(aDocument, std::move(aTarget), std::move(aTiming),
                       aOptions) {}

  CSSAnimationKeyframeEffect* AsCSSAnimationKeyframeEffect() override {
    return this;
  }

  void GetTiming(EffectTiming& aRetVal) const override;
  void GetComputedTimingAsDict(ComputedEffectTiming& aRetVal) const override;
  void UpdateTiming(const OptionalEffectTiming& aTiming,
                    ErrorResult& aRv) override;
  void SetKeyframes(JSContext* aContext, JS::Handle<JSObject*> aKeyframes,
                    ErrorResult& aRv) override;
  void SetComposite(const CompositeOperation& aComposite) override;
  void SetDefaultTimingFunction(
      const StyleComputedTimingFunction& aTimingFunction) {
    mDefaultTimingFunction = aTimingFunction;
  }
  void SetDefaultComposite(const CompositeOperation& aComposite) {
    mDefaultComposite = aComposite;
  }
  bool GetComputedKeyframes(nsTArray<Keyframe>& aKeyframes) const override;

 private:
  CSSAnimation* GetOwningCSSAnimation() {
    return mAnimation ? mAnimation->AsCSSAnimation() : nullptr;
  }
  const CSSAnimation* GetOwningCSSAnimation() const {
    return mAnimation ? mAnimation->AsCSSAnimation() : nullptr;
  }

  // Flushes styles if our owning animation is a CSSAnimation
  void MaybeFlushUnanimatedStyle() const;

  // The default timing function is the corresponding computed value of
  // animation-timing-function on element.
  // Note: We shouldn't reuse |mTiming.mFunction| because it has other usages,
  // e.g. it should still be "linear" when calling getTiming() and computing the
  // progress.
  // https://drafts.csswg.org/css-animations-2/#keyframe-processing
  StyleComputedTimingFunction mDefaultTimingFunction =
      StyleComputedTimingFunction::Keyword(StyleTimingKeyword::Ease);
  // The default composite is the corresponding computed value of
  // animation-composition on element.
  // Note: We cannot reuse |mEffectOptions.mComposite| which may be updated by
  // Web Animations. We should always use the animation-composition from style.
  CompositeOperation mDefaultComposite = CompositeOperation::Replace;

  // True if we should ignore keyframes generation (for 0% and 100%) because the
  // new keyframes are from JS. It's unfortunate we cannot reuse
  // |CSSAnimation::mOverriddenProperties| because this keyframe effect may not
  // be associated with an animation.
  bool mIgnoreKeyframesGeneration = false;
};

}  // namespace dom

}  // namespace mozilla

#endif  // mozilla_dom_CSSAnimation_h
