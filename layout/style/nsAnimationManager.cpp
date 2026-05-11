/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAnimationManager.h"

#include <algorithm>  // std::stable_sort

#include "TimelineManager.h"
#include "mozilla/AnimationEventDispatcher.h"
#include "mozilla/AnimationUtils.h"
#include "mozilla/EffectCompositor.h"
#include "mozilla/ElementAnimationData.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/TimelineCollection.h"
#include "mozilla/dom/AnimationEffect.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/DocumentTimeline.h"
#include "mozilla/dom/KeyframeEffect.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/ScrollTimeline.h"
#include "mozilla/dom/ViewTimeline.h"
#include "nsDOMMutationObserver.h"
#include "nsIFrame.h"
#include "nsINode.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsPresContextInlines.h"
#include "nsRFPService.h"
#include "nsStyleChangeList.h"
#include "nsTransitionManager.h"

using namespace mozilla;
using namespace mozilla::css;
using mozilla::dom::Animation;
using mozilla::dom::AnimationPlayState;
using mozilla::dom::CSSAnimation;
using mozilla::dom::Element;
using mozilla::dom::KeyframeEffect;
using mozilla::dom::MutationObservers;
using mozilla::dom::ScrollTimeline;
using mozilla::dom::ViewTimeline;

////////////////////////// nsAnimationManager ////////////////////////////

// Find the matching animation by |aName| in the old list
// of animations and remove the matched animation from the list.
static already_AddRefed<CSSAnimation> PopExistingAnimation(
    const nsAtom* aName,
    nsAnimationManager::CSSAnimationCollection* aCollection) {
  if (!aCollection) {
    return nullptr;
  }

  // Animations are stored in reverse order to how they appear in the
  // animation-name property. However, we want to match animations beginning
  // from the end of the animation-name list, so we iterate *forwards*
  // through the collection.
  for (size_t idx = 0, length = aCollection->mAnimations.Length();
       idx != length; ++idx) {
    CSSAnimation* cssAnim = aCollection->mAnimations[idx];
    if (cssAnim->AnimationName() == aName) {
      RefPtr<CSSAnimation> match = cssAnim;
      aCollection->mAnimations.RemoveElementAt(idx);
      return match.forget();
    }
  }

  return nullptr;
}

class MOZ_STACK_CLASS ServoCSSAnimationBuilder final {
 public:
  explicit ServoCSSAnimationBuilder(const ComputedStyle* aComputedStyle)
      : mComputedStyle(aComputedStyle) {
    MOZ_ASSERT(aComputedStyle);
  }

  bool BuildKeyframes(const Element& aElement, nsPresContext* aPresContext,
                      nsAtom* aName,
                      const StyleComputedTimingFunction& aTimingFunction,
                      nsTArray<Keyframe>& aKeyframes) {
    return aPresContext->StyleSet()->GetKeyframesForName(
        aElement, *mComputedStyle, aName, aTimingFunction, aKeyframes);
  }
  void SetKeyframes(KeyframeEffect& aEffect, nsTArray<Keyframe>&& aKeyframes,
                    const dom::AnimationTimeline* aTimeline) {
    aEffect.SetKeyframes(std::move(aKeyframes), mComputedStyle, aTimeline);
  }

  // Currently all the animation building code in this file is based on
  // assumption that creating and removing animations should *not* trigger
  // additional restyles since those changes will be handled within the same
  // restyle.
  //
  // While that is true for the Gecko style backend, it is not true for the
  // Servo style backend where we want restyles to be triggered so that we
  // perform a second animation restyle where we will incorporate the changes
  // arising from creating and removing animations.
  //
  // Fortunately, our attempts to avoid posting extra restyles as part of the
  // processing here are imperfect and most of the time we happen to post
  // them anyway. Occasionally, however, we don't. For example, we don't post
  // a restyle when we create a new animation whose an animation index matches
  // the default value it was given already (which is typically only true when
  // the CSSAnimation we create is the first Animation created in a particular
  // content process).
  //
  // As a result, when we are using the Servo backend, whenever we have an added
  // or removed animation we need to explicitly trigger a restyle.
  //
  // This code should eventually disappear along with the Gecko style backend
  // and we should simply call Play() / Pause() / Cancel() etc. which will
  // post the required restyles.
  void NotifyNewOrRemovedAnimation(const dom::Animation& aAnimation) {
    dom::AnimationEffect* effect = aAnimation.GetEffect();
    if (!effect) {
      return;
    }

    KeyframeEffect* keyframeEffect = effect->AsKeyframeEffect();
    if (!keyframeEffect) {
      return;
    }

    keyframeEffect->RequestRestyle(EffectCompositor::RestyleType::Standard);
  }

 private:
  const ComputedStyle* mComputedStyle;
};

struct AnimationMatches {
  bool operator()(const RefPtr<CSSAnimation>& aAnimation) {
    return aAnimation.get() == mAnimation;
  }

  const CSSAnimation* mAnimation;
};

static void RemoveCorrespondingAnimation(
    const nsAtom* aName, const CSSAnimation* aAnimation,
    nsAnimationManager::TimelineNamesToAnimationMap&
        aTimelineNamesToAnimationMap) {
  auto result = aTimelineNamesToAnimationMap.Lookup(aName);
  if (result) {
    auto& l = result.Data();
    auto foundIt =
        std::find_if(l.cbegin(), l.cend(), AnimationMatches{aAnimation});
    if (foundIt != l.cend()) {
      l.RemoveElementAt(foundIt);
    }
    result.Remove();
  }
#ifdef DEBUG
  // One animation refers to one timeline, so if there is a duplication,
  // something went wrong.
  for (auto mapItr = aTimelineNamesToAnimationMap.Iter(); !mapItr.Done();
       mapItr.Next()) {
    auto& l = mapItr.Data();
    auto foundIt =
        std::find_if(l.cbegin(), l.cend(), AnimationMatches{aAnimation});
    MOZ_ASSERT(foundIt == l.cend(), "Duplication animation entry");
  }
#endif
}

static void UpdateOldAnimationPropertiesWithNew(
    CSSAnimation& aOld, TimingParams&& aNewTiming,
    nsTArray<Keyframe>&& aNewKeyframes, bool aNewIsStylePaused,
    CSSAnimationProperties aOverriddenProperties,
    ServoCSSAnimationBuilder& aBuilder, dom::AnimationTimeline* aTimeline,
    const nsAtom* aTimelineName, dom::CompositeOperation aNewComposite,
    dom::AnimationRange&& aTimelineRange,
    nsAnimationManager::TimelineNamesToAnimationMap&
        aTimelineNamesToAnimationMap) {
  const auto* oldTimelineName = aOld.GetTimelineName();
  const bool timelineReferenceChanged =
      aOld.GetTimeline() != aTimeline || oldTimelineName != aTimelineName;
  if (timelineReferenceChanged && oldTimelineName) {
    RemoveCorrespondingAnimation(oldTimelineName, &aOld,
                                 aTimelineNamesToAnimationMap);
  }
  bool animationChanged = false;

  // Update the old from the new so we can keep the original object
  // identity (and any expando properties attached to it).
  if (aOld.GetEffect()) {
    dom::AnimationEffect* oldEffect = aOld.GetEffect();

    // Copy across the changes that are not overridden
    TimingParams updatedTiming = oldEffect->SpecifiedTiming();
    if (~aOverriddenProperties & CSSAnimationProperties::Duration) {
      updatedTiming.SetDuration(aNewTiming.Duration());
    }
    if (~aOverriddenProperties & CSSAnimationProperties::IterationCount) {
      updatedTiming.SetIterations(aNewTiming.Iterations());
    }
    if (~aOverriddenProperties & CSSAnimationProperties::Direction) {
      updatedTiming.SetDirection(aNewTiming.Direction());
    }
    if (~aOverriddenProperties & CSSAnimationProperties::Delay) {
      updatedTiming.SetDelay(aNewTiming.Delay());
    }
    if (~aOverriddenProperties & CSSAnimationProperties::FillMode) {
      updatedTiming.SetFill(aNewTiming.Fill());
    }

    animationChanged = oldEffect->SpecifiedTiming() != updatedTiming;
    oldEffect->SetSpecifiedTiming(std::move(updatedTiming));

    if (KeyframeEffect* oldKeyframeEffect = oldEffect->AsKeyframeEffect()) {
      if (~aOverriddenProperties & CSSAnimationProperties::Keyframes) {
        aBuilder.SetKeyframes(*oldKeyframeEffect, std::move(aNewKeyframes),
                              aTimeline);
      }

      if (~aOverriddenProperties & CSSAnimationProperties::Composition) {
        animationChanged = oldKeyframeEffect->Composite() != aNewComposite;
        oldKeyframeEffect->SetCompositeFromStyle(aNewComposite);
      }
    }
  }

  // Checking pointers should be enough. If both are scroll-timeline, we reuse
  // the scroll-timeline object if their scrollers and axes are the same.
  if (aOld.GetTimeline() != aTimeline) {
    aOld.SetTimeline(aTimeline, aTimelineName);
    animationChanged = true;
  }

  if (aOld.GetTimelineRange() != aTimelineRange) {
    aOld.SetTimelineRange(std::move(aTimelineRange));
    animationChanged = true;
  }

  // Handle changes in play state. If the animation is idle, however,
  // changes to animation-play-state should *not* restart it.
  if (aOld.PlayState() != AnimationPlayState::Idle &&
      ~aOverriddenProperties & CSSAnimationProperties::PlayState) {
    bool wasPaused = aOld.PlayState() == AnimationPlayState::Paused;
    if (!wasPaused && aNewIsStylePaused) {
      aOld.PauseFromStyle();
      animationChanged = true;
    } else if (wasPaused && !aNewIsStylePaused) {
      aOld.PlayFromStyle();
      animationChanged = true;
    }
  }

  // Updating the effect timing above might already have caused the
  // animation to become irrelevant so only add a changed record if
  // the animation is still relevant.
  if (animationChanged && aOld.IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(&aOld);
  }

  if (timelineReferenceChanged && aTimelineName) {
    auto& entries = aTimelineNamesToAnimationMap.LookupOrInsert(
        aTimelineName, nsTArray<RefPtr<CSSAnimation>>{});
    entries.AppendElement(&aOld);
  }
}

static already_AddRefed<dom::AnimationTimeline> GetNamedProgressTimeline(
    dom::Document* aDocument, const NonOwningAnimationTarget& aTarget,
    const nsAtom* aName) {
  auto* presContext = aDocument->GetPresContext();
  const auto* timelineManager =
      presContext ? presContext->TimelineManager() : nullptr;
  // A named progress timeline is referenceable in animation-timeline by:
  // 1. the declaring element itself
  // 2. that element’s descendants
  // https://drafts.csswg.org/scroll-animations-1/#timeline-scope
  for (Element* e = aTarget.mElement->GetPseudoElement(aTarget.mPseudoRequest);
       e; e = e->GetParentElement()) {
    // If multiple elements have declared the same timeline name, the matching
    // timeline is the one declared on the nearest element in tree order, which
    // considers siblings closer than parents.
    // Note: This is fine for parallel traversal because we update animations by
    // SequentialTask.
    const auto [element, pseudo] = AnimationUtils::GetElementPseudoPair(e);
    if (auto* collection =
            TimelineCollection<ScrollTimeline>::Get(element, pseudo)) {
      if (RefPtr<ScrollTimeline> timeline = collection->Lookup(aName)) {
        return timeline.forget();
      }
    }

    if (auto* collection =
            TimelineCollection<ViewTimeline>::Get(element, pseudo)) {
      if (RefPtr<ViewTimeline> timeline = collection->Lookup(aName)) {
        return timeline.forget();
      }
    }

    if (!timelineManager) {
      continue;
    }

    if (auto scopedTimeline = timelineManager->GetScopedTimeline(e, aName)) {
      return already_AddRefed{scopedTimeline->take()};
    }
  }

  // If we cannot find a matched scroll-timeline-name, this animation is not
  // associated with a timeline.
  // https://drafts.csswg.org/css-animations-2/#valdef-animation-timeline-custom-ident
  return nullptr;
}

static already_AddRefed<dom::AnimationTimeline> GetTimeline(
    const StyleAnimationTimeline& aStyleTimeline, nsPresContext* aPresContext,
    const NonOwningAnimationTarget& aTarget) {
  switch (aStyleTimeline.tag) {
    case StyleAnimationTimeline::Tag::Timeline: {
      // Check scroll-timeline-name property or view-timeline-property.
      nsAtom* name = aStyleTimeline.AsTimeline().value.AsAtom();
      return name != nsGkAtoms::_empty
                 ? GetNamedProgressTimeline(aPresContext->Document(), aTarget,
                                            name)
                 : nullptr;
    }
    case StyleAnimationTimeline::Tag::Scroll: {
      const auto& scroll = aStyleTimeline.AsScroll();
      return ScrollTimeline::MakeAnonymous(aPresContext->Document(), aTarget,
                                           scroll.axis, scroll.scroller);
    }
    case StyleAnimationTimeline::Tag::View: {
      const auto& view = aStyleTimeline.AsView();
      return ViewTimeline::MakeAnonymous(aPresContext->Document(), aTarget,
                                         view.axis, view.inset);
    }
    case StyleAnimationTimeline::Tag::Auto:
      return do_AddRef(aTarget.mElement->OwnerDoc()->Timeline());
  }
  MOZ_ASSERT_UNREACHABLE("Unknown animation-timeline value?");
  return nullptr;
}

static bool RefersToNamedTimeline(const CSSAnimation* aAnimation) {
  return aAnimation->GetTimelineName();
}

// Returns a new animation set up with given StyleAnimation.
// Or returns an existing animation matching StyleAnimation's name updated
// with the new StyleAnimation.
static already_AddRefed<CSSAnimation> BuildAnimation(
    nsPresContext* aPresContext, const NonOwningAnimationTarget& aTarget,
    const nsStyleUIReset& aStyle, uint32_t animIdx,
    ServoCSSAnimationBuilder& aBuilder,
    nsAnimationManager::CSSAnimationCollection* aCollection,
    nsAnimationManager::TimelineNamesToAnimationMap&
        aTimelineNamesToAnimationMap) {
  MOZ_ASSERT(aPresContext);

  nsAtom* animationName = aStyle.GetAnimationName(animIdx);
  nsTArray<Keyframe> keyframes;
  if (!aBuilder.BuildKeyframes(*aTarget.mElement, aPresContext, animationName,
                               aStyle.GetAnimationTimingFunction(animIdx),
                               keyframes)) {
    return nullptr;
  }

  const StyleAnimationDuration& duration = aStyle.GetAnimationDuration(animIdx);
  TimingParams timing = TimingParamsFromCSSParams(
      duration.IsAuto() ? Nothing() : Some(duration.AsTime().ToMilliseconds()),
      aStyle.GetAnimationDelay(animIdx).ToMilliseconds(),
      aStyle.GetAnimationIterationCount(animIdx),
      aStyle.GetAnimationDirection(animIdx),
      aStyle.GetAnimationFillMode(animIdx));

  bool isStylePaused =
      aStyle.GetAnimationPlayState(animIdx) == StyleAnimationPlayState::Paused;

  const auto& styleTimeline = aStyle.GetTimeline(animIdx);
  RefPtr<dom::AnimationTimeline> timeline =
      GetTimeline(styleTimeline, aPresContext, aTarget);
  auto timelineName = [&]() -> const nsAtom* {
    if (!styleTimeline.IsTimeline()) {
      return nullptr;
    }
    const auto* atom = styleTimeline.AsTimeline().value.AsAtom();
    if (atom == nsGkAtoms::_empty) {
      // This is actually `animation-timeline: none`.
      return nullptr;
    }
    return atom;
  }();

  auto range = dom::AnimationRange{aStyle.GetAnimationRangeStart(animIdx),
                                   aStyle.GetAnimationRangeEnd(animIdx)};

  // Find the matching animation with animation name in the old list
  // of animations and remove the matched animation from the list.
  RefPtr<CSSAnimation> oldAnim =
      PopExistingAnimation(animationName, aCollection);

  const auto composition = StyleToDom(aStyle.GetAnimationComposition(animIdx));
  if (oldAnim) {
    // Copy over the start times and (if still paused) pause starts
    // for each animation (matching on name only) that was also in the
    // old list of animations.
    // This means that we honor dynamic changes, which isn't what the
    // spec says to do, but WebKit seems to honor at least some of
    // them.  See
    // http://lists.w3.org/Archives/Public/www-style/2011Apr/0079.html
    // In order to honor what the spec said, we'd copy more data over.
    UpdateOldAnimationPropertiesWithNew(
        *oldAnim, std::move(timing), std::move(keyframes), isStylePaused,
        oldAnim->GetOverriddenProperties(), aBuilder, timeline, timelineName,
        composition, std::move(range), aTimelineNamesToAnimationMap);
    // For now, only name-referenced timeline, or `none`, which is represented
    // as IsTimeline with the empty atom, can result in no timeline.
    MOZ_ASSERT_IF(timelineName && !timeline, styleTimeline.IsTimeline());
    return oldAnim.forget();
  }

  KeyframeEffectParams effectOptions(composition);
  auto effect = MakeRefPtr<dom::CSSAnimationKeyframeEffect>(
      aPresContext->Document(),
      OwningAnimationTarget(aTarget.mElement, aTarget.mPseudoRequest),
      std::move(timing), effectOptions);

  aBuilder.SetKeyframes(*effect, std::move(keyframes), timeline);

  auto animation = MakeRefPtr<CSSAnimation>(
      aPresContext->Document()->GetScopeObject(), animationName);
  animation->SetOwningElement(
      OwningElementRef(*aTarget.mElement, aTarget.mPseudoRequest));

  animation->SetTimelineNoUpdate(timeline, timelineName);
  animation->SetEffectNoUpdate(effect);
  animation->SetTimelineRangeNoUpdate(std::move(range));

  if (isStylePaused) {
    animation->PauseFromStyle();
  } else {
    animation->PlayFromStyle();
  }

  aBuilder.NotifyNewOrRemovedAnimation(*animation);

  // For now, only `none` or name-referenced timeline can result in no timeline.
  MOZ_ASSERT_IF(!timeline, styleTimeline.IsTimeline());
  if (RefersToNamedTimeline(animation)) {
    const auto* name = styleTimeline.AsTimeline().value.AsAtom();
    auto& entries = aTimelineNamesToAnimationMap.LookupOrInsert(
        name, nsTArray<RefPtr<CSSAnimation>>{});
    entries.AppendElement(animation);
  }

  return animation.forget();
}

static nsAnimationManager::OwningCSSAnimationPtrArray BuildAnimations(
    nsPresContext* aPresContext, const NonOwningAnimationTarget& aTarget,
    const nsStyleUIReset& aStyle, ServoCSSAnimationBuilder& aBuilder,
    nsAnimationManager::CSSAnimationCollection* aCollection,
    nsTHashSet<RefPtr<nsAtom>>& aReferencedAnimations,
    nsAnimationManager::TimelineNamesToAnimationMap&
        aTimelineNamesToAnimationMap) {
  nsAnimationManager::OwningCSSAnimationPtrArray result;

  for (size_t animIdx = aStyle.mAnimationNameCount; animIdx-- != 0;) {
    nsAtom* name = aStyle.GetAnimationName(animIdx);
    // CSS Animations whose animation-name does not match a @keyframes rule do
    // not generate animation events. This includes when the animation-name is
    // "none" which is represented by an empty name in the StyleAnimation.
    // Since such animations neither affect style nor dispatch events, we do
    // not generate a corresponding CSSAnimation for them.
    if (name == nsGkAtoms::_empty) {
      continue;
    }

    aReferencedAnimations.Insert(name);
    RefPtr<CSSAnimation> dest =
        BuildAnimation(aPresContext, aTarget, aStyle, animIdx, aBuilder,
                       aCollection, aTimelineNamesToAnimationMap);
    if (!dest) {
      continue;
    }

    dest->SetAnimationIndex(static_cast<uint64_t>(animIdx));
    result.AppendElement(dest);
  }
  return result;
}

void nsAnimationManager::UpdateAnimations(
    dom::Element* aElement, const PseudoStyleRequest& aPseudoRequest,
    const ComputedStyle* aComputedStyle) {
  MOZ_ASSERT(mPresContext->IsDynamic(),
             "Should not update animations for print or print preview");
  MOZ_ASSERT(aElement->IsInComposedDoc(),
             "Should not update animations that are not attached to the "
             "document tree");

  if (!aComputedStyle ||
      aComputedStyle->StyleDisplay()->mDisplay == StyleDisplay::None) {
    // If we are in a display:none subtree we will have no computed values.
    // However, if we are on the root of display:none subtree, the computed
    // values might not have been cleared yet.
    // In either case, since CSS animations should not run in display:none
    // subtrees we should stop (actually, destroy) any animations on this
    // element here.
    StopAnimationsForElement(aElement, aPseudoRequest);
    return;
  }

  NonOwningAnimationTarget target(aElement, aPseudoRequest);
  ServoCSSAnimationBuilder builder(aComputedStyle);

  DoUpdateAnimations(target, *aComputedStyle->StyleUIReset(), builder);
}

void nsAnimationManager::RemoveNamedTimelineAnimation(
    const nsAtom* aName, mozilla::dom::CSSAnimation* aAnimation) {
  RemoveCorrespondingAnimation(aName, aAnimation, mAnimationsWithNamedTimeline);
}

static void UpdateNamedTimelineAnimation(dom::Document* aDocument,
                                         CSSAnimation* aAnimation,
                                         const nsAtom* aTimelineName) {
  if (aTimelineName != aAnimation->GetTimelineName()) {
    return;
  }
  const auto target = aAnimation->GetTargetForAnimation();
  const RefPtr<dom::AnimationTimeline> newTimeline =
      GetNamedProgressTimeline(aDocument, target, aTimelineName);
  const auto* oldTimeline = aAnimation->GetTimeline();
  if (oldTimeline == newTimeline) {
    return;
  }
  aAnimation->SetTimeline(newTimeline, aTimelineName);
}

#ifdef DEBUG
static void CheckNamedTimelineMap(
    nsAnimationManager::TimelineNamesToAnimationMap&
        aTimelineNamesToAnimationMap) {
  for (const auto& key : aTimelineNamesToAnimationMap.Keys()) {
    MOZ_ASSERT(key != nsGkAtoms::_empty);
  }
}
#endif

void nsAnimationManager::UpdateNamedTimelineAnimations(
    const nsTArray<RefPtr<const nsAtom>>& aChanged) {
  auto* document = mPresContext->Document();
  for (const auto& name : aChanged) {
    auto entries = mAnimationsWithNamedTimeline.Lookup(name);
    if (!entries) {
      continue;
    }
    for (auto& animation : *entries) {
      UpdateNamedTimelineAnimation(document, animation.get(), name.get());
    }
  }
#ifdef DEBUG
  CheckNamedTimelineMap(mAnimationsWithNamedTimeline);
#endif
}

void nsAnimationManager::UpdateAllNamedTimelineAnimations() {
  auto* document = mPresContext->Document();
  for (auto& entry : mAnimationsWithNamedTimeline) {
    const auto& name = entry.GetKey();
    for (auto& animation : entry.GetData()) {
      UpdateNamedTimelineAnimation(document, animation.get(), name);
    }
  }
#ifdef DEBUG
  CheckNamedTimelineMap(mAnimationsWithNamedTimeline);
#endif
}

void nsAnimationManager::DoUpdateAnimations(
    const NonOwningAnimationTarget& aTarget, const nsStyleUIReset& aStyle,
    ServoCSSAnimationBuilder& aBuilder) {
  // Everything that causes our animation data to change triggers a
  // style change, which in turn triggers a non-animation restyle.
  // Likewise, when we initially construct frames, we're not in a
  // style change, but also not in an animation restyle.

  auto* collection =
      CSSAnimationCollection::Get(aTarget.mElement, aTarget.mPseudoRequest);
  if (!collection && aStyle.mAnimationNameCount == 1 &&
      aStyle.mAnimations[0].GetName() == nsGkAtoms::_empty) {
    return;
  }

  nsAutoAnimationMutationBatch mb(aTarget.mElement->OwnerDoc());

  // Build the updated animations list. Even if we remove entries in
  // `mAnimationsWithNamedTimeline` in this function later, entries are added
  // when we build the list, breaking the symmetry. This is unfortunate, but
  // unavoidable, because `newAmimations.Length()` does not necessarily equal to
  // the length of animations defined in `aStyle`, e.g. when a referenced
  // `animiation-name` does not exist.
  //
  // Old entries in `collection` may be reused. If they are, they are removed
  // from `collection` and put into `newAnimations`.
  OwningCSSAnimationPtrArray newAnimations =
      BuildAnimations(mPresContext, aTarget, aStyle, aBuilder, collection,
                      mMaybeReferencedAnimations, mAnimationsWithNamedTimeline);

  if (newAnimations.IsEmpty()) {
    if (collection) {
      for (const auto& animation : collection->mAnimations) {
        if (!RefersToNamedTimeline(animation)) {
          continue;
        }
        RemoveCorrespondingAnimation(animation->GetTimelineName(), animation,
                                     mAnimationsWithNamedTimeline);
      }
      collection->Destroy();
    }
    return;
  }

  if (!collection) {
    collection =
        &aTarget.mElement->EnsureAnimationData().EnsureAnimationCollection(
            *aTarget.mElement, aTarget.mPseudoRequest);
    if (!collection->isInList()) {
      AddElementCollection(collection);
    }
  }
  collection->mAnimations.SwapElements(newAnimations);

  // Cancel removed animations
  for (size_t newAnimIdx = newAnimations.Length(); newAnimIdx-- != 0;) {
    const auto& anim = newAnimations[newAnimIdx];
    aBuilder.NotifyNewOrRemovedAnimation(*anim);
    newAnimations[newAnimIdx]->CancelFromStyle(PostRestyleMode::IfNeeded);
    if (RefersToNamedTimeline(anim)) {
      RemoveCorrespondingAnimation(anim->GetTimelineName(), anim,
                                   mAnimationsWithNamedTimeline);
    }
  }
}
