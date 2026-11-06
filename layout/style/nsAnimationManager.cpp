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
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/TimelineCollection.h"
#include "mozilla/dom/AnimationEffect.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/DocumentTimeline.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/KeyframeEffect.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/ScrollTimeline.h"
#include "mozilla/dom/TimelineName.h"
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
using mozilla::dom::UnresolvedTimeline;
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

  const ComputedStyle* Style() const { return mComputedStyle; }

  bool BuildKeyframes(const Element& aElement, nsPresContext* aPresContext,
                      nsAtom* aName,
                      const StyleComputedTimingFunction& aTimingFunction,
                      nsTArray<Keyframe>& aKeyframes) {
    return aPresContext->StyleSet()->GetKeyframesForName(
        aElement, *mComputedStyle, aName, aTimingFunction, aKeyframes);
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
    // Note that we don't care about scoped names - we already know that
    // we need to remove this exact animation.
    auto foundIt =
        std::find_if(l.cbegin(), l.cend(), AnimationMatches{aAnimation});
    if (foundIt != l.cend()) {
      l.RemoveElementAt(foundIt);
    }
    // Drop the list if there's no other animation referencing this name.
    if (l.IsEmpty()) {
      result.Remove();
    }
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

// https://drafts.csswg.org/scroll-animations-1/#timeline-scoping
static already_AddRefed<dom::AnimationTimeline> GetNamedProgressTimeline(
    dom::Document* aDocument, const NonOwningAnimationTarget& aTarget,
    const dom::ScopedTimelineName& aName) {
  auto* presContext = aDocument->GetPresContext();
  const auto* targetShadowRoot =
      Servo_GetShadowRootForScoped(aTarget.mElement, aName.mCascadeLevel);
  const auto* timelineManager =
      presContext ? presContext->TimelineManager() : nullptr;
  for (Element* e = aTarget.mElement->GetPseudoElement(aTarget.mPseudoRequest);
       e; e = e->GetParentElementCrossingShadowRoot()) {
    // Check ourselves first.
    const auto [element, pseudo] = AnimationUtils::GetElementPseudoPair(e);
    if (auto result = TimelineManager::GetNamedTimelineForThisElement(
            element, pseudo, aName.mName, targetShadowRoot)) {
      return result.forget();
    }
    // We're scoped, or have reached the top.
    if (timelineManager && (TimelineManager::TimelineNameScopedByElement(
                                element, pseudo, aName.mName) ||
                            e->IsRootElement())) {
      return timelineManager->GetNamedTimelineInSubtree(
          e, aName.mName, targetShadowRoot, aDocument);
    }
    // Continue the search with our parent.
  }
  return nullptr;
}

static already_AddRefed<dom::AnimationTimeline> GetTimeline(
    const StyleAnimationTimeline& aStyleTimeline, nsPresContext* aPresContext,
    const NonOwningAnimationTarget& aTarget,
    dom::AnimationTimeline* aOldTimeline) {
  switch (aStyleTimeline.tag) {
    case StyleAnimationTimeline::Tag::Timeline: {
      // Check scroll-timeline-name property or view-timeline-property.
      const auto& scopedName =
          dom::ScopedTimelineName{aStyleTimeline.AsTimeline()};
      if (scopedName.mName == nsGkAtoms::_empty) {
        // `animation-timeline: none`.
        return nullptr;
      }
      return GetNamedProgressTimeline(aPresContext->Document(), aTarget,
                                      scopedName);
    }
    case StyleAnimationTimeline::Tag::Scroll: {
      const auto& scroll = aStyleTimeline.AsScroll();
      const bool reuseOldTimeline = [&]() {
        const auto* scrollTimeline =
            aOldTimeline ? aOldTimeline->AsScrollTimeline() : nullptr;
        if (!scrollTimeline || scrollTimeline->IsViewTimeline()) {
          return false;
        }
        return scrollTimeline->IsReusableAnonymousTimeline(scroll);
      }();
      if (reuseOldTimeline) {
        // TODO(dshin): Refcount churn. Hopefully this doesn't happen often
        // enough?
        return do_AddRef(aOldTimeline);
      }
      return ScrollTimeline::MakeAnonymous(aPresContext->Document(), aTarget,
                                           scroll.axis, scroll.scroller);
    }
    case StyleAnimationTimeline::Tag::View: {
      const auto& view = aStyleTimeline.AsView();
      const bool reuseOldTimeline = [&]() {
        const auto* viewTimeline =
            aOldTimeline ? aOldTimeline->AsViewTimeline() : nullptr;
        if (!viewTimeline) {
          return false;
        }
        return viewTimeline->IsReusableAnonymousTimeline(view);
      }();
      if (reuseOldTimeline) {
        // TODO(dshin): Same potential issue as `scroll()` above.
        return do_AddRef(aOldTimeline);
      }
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
  return aAnimation->GetTimelineName().mName;
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
  const StyleComputedTimingFunction& timingFunction =
      aStyle.GetAnimationTimingFunction(animIdx);
  nsTArray<Keyframe> keyframes;
  if (!aBuilder.BuildKeyframes(*aTarget.mElement, aPresContext, animationName,
                               timingFunction, keyframes)) {
    return nullptr;
  }

  const StyleAnimationDuration& duration = aStyle.GetAnimationDuration(animIdx);
  TimingParams timing = TimingParamsFromCSSParams(
      duration.IsAuto() ? Nothing() : Some(duration.AsTime().ToMilliseconds()),
      aStyle.GetAnimationDelay(animIdx).ToMilliseconds(),
      aStyle.GetAnimationIterationCount(animIdx),
      aStyle.GetAnimationDirection(animIdx),
      aStyle.GetAnimationFillMode(animIdx));

  const auto composition = StyleToDom(aStyle.GetAnimationComposition(animIdx));
  const bool isStylePaused =
      aStyle.GetAnimationPlayState(animIdx) == StyleAnimationPlayState::Paused;

  // Find the matching animation with animation name in the old list
  // of animations and remove the matched animation from the list.
  RefPtr<CSSAnimation> oldAnim =
      PopExistingAnimation(animationName, aCollection);
  const auto& styleTimeline = aStyle.GetTimeline(animIdx);
  RefPtr<dom::AnimationTimeline> timeline =
      GetTimeline(styleTimeline, aPresContext, aTarget,
                  oldAnim ? oldAnim->GetTimeline() : nullptr);
  const auto timelineName = [&]() -> dom::ScopedTimelineName {
    if (!styleTimeline.IsTimeline()) {
      return {};
    }
    const auto* atom = styleTimeline.AsTimeline().value.AsAtom();
    if (atom == nsGkAtoms::_empty) {
      // This is actually `animation-timeline: none`.
      return {};
    }
    return dom::ScopedTimelineName{styleTimeline.AsTimeline()};
  }();

  auto range = dom::AnimationRange{aStyle.GetAnimationRangeStart(animIdx),
                                   aStyle.GetAnimationRangeEnd(animIdx)};

  // Reuse the existing animation. Update |oldAnim| with the new properties and
  // the corresponding map of timeline names.
  if (oldAnim) {
    const nsAtom* oldTimelineName = oldAnim->GetTimelineName().mName;
    const bool timelineReferenceChanged =
        oldAnim->GetTimeline() != timeline.get() ||
        oldTimelineName != timelineName.mName;
    // Remove the animation from the map if needed.
    if (timelineReferenceChanged && oldTimelineName) {
      RemoveCorrespondingAnimation(oldTimelineName, oldAnim,
                                   aTimelineNamesToAnimationMap);
    }

    // Sync the old animation with those new properties. This may ignore the
    // properties overridden by Web Animations.
    oldAnim->UpdatePropertiesWithNew(
        aTarget, aBuilder.Style(), std::move(timing), std::move(keyframes),
        timingFunction, composition, timeline.get(), timelineName,
        std::move(range), isStylePaused);

    if (timelineReferenceChanged && timelineName.mName) {
      auto& entries = aTimelineNamesToAnimationMap.LookupOrInsert(
          timelineName.mName, nsTArray<RefPtr<CSSAnimation>>{});
      entries.AppendElement(oldAnim);
    }

    // For now, only name-referenced timeline, or `none`, which is represented
    // as IsTimeline with the empty atom, can result in no timeline.
    MOZ_ASSERT_IF(timelineName.mName && !timeline, styleTimeline.IsTimeline());
    return oldAnim.forget();
  }

  // Since we don't have an existing animation, create a new one instead.
  KeyframeEffectParams effectOptions(composition);
  auto effect = MakeRefPtr<dom::CSSAnimationKeyframeEffect>(
      aPresContext->Document(),
      OwningAnimationTarget(aTarget.mElement, aTarget.mPseudoRequest),
      std::move(timing), effectOptions);
  effect->SetDefaultTimingFunction(timingFunction);
  effect->SetDefaultComposite(composition);
  effect->KeyframeEffect::SetKeyframes(std::move(keyframes), aBuilder.Style(),
                                       timeline, &range);

  auto animation = MakeRefPtr<CSSAnimation>(
      aPresContext->Document()->GetScopeObject(), animationName, isStylePaused);
  animation->SetOwningElement(
      OwningElementRef(*aTarget.mElement, aTarget.mPseudoRequest));

  animation->SetTimelineNoUpdate(timeline, timelineName, Animation::FromJS::No);
  animation->SetEffectNoUpdate(effect);
  animation->SetTimelineRangeNoUpdate(std::move(range), Animation::FromJS::No);

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
      (!StaticPrefs::layout_css_display_animations_enabled() &&
       aComputedStyle->StyleDisplay()->mDisplay == StyleDisplay::None)) {
    // If we are in a display:none subtree we will have no computed values.
    // However, if we are on the root of display:none subtree, the computed
    // values might not have been cleared yet.
    // Stop (actually, destroy) any animations here: either there are no
    // computed values, or display animations are disabled and this is a
    // display:none root.
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

/**
 * Try to update the named timeline associated with this animation.
 * If such named timeline is not found, and if deferring is allowed,
 * by passing a Some() aAnimationsWithDeferredUpdate, the animation
 * is added to the provided hashset, to be updated once a frame.
 * This follows the spec resolution made in
 * https://github.com/w3c/csswg-drafts/issues/13963.
 */
static void UpdateNamedTimelineAnimation(
    dom::Document* aDocument, CSSAnimation* aAnimation,
    const nsAtom* aTimelineName,
    Maybe<nsTHashSet<RefPtr<mozilla::dom::CSSAnimation>>&>
        aAnimationsWithDeferredUpdate) {
  const auto scopedName = aAnimation->GetTimelineName();
  if (aTimelineName != scopedName.mName) {
    return;
  }
  const auto target = aAnimation->GetTargetForAnimation();
  if (MOZ_UNLIKELY(!target.mElement)) {
    // Animation likely does not have an associated effect.
    return;
  }
  // Ok, we know at least one timeline by this name got updated, which may
  // change our lookup result. Lookup by our scoped name again.
  const RefPtr<dom::AnimationTimeline> newTimeline =
      GetNamedProgressTimeline(aDocument, target, scopedName);
  const auto* oldTimeline = aAnimation->GetTimeline();
  if (oldTimeline == newTimeline) {
    return;
  }
  if (aAnimationsWithDeferredUpdate &&
      (!newTimeline || newTimeline->IsUnresolvedTimeline())) {
    // We know this animation is looking for a named animation - but it does not
    // exist. One may become available later, so defer setting the new timeline
    // (There may be more incoming changes).
    aAnimationsWithDeferredUpdate->Insert(aAnimation);
    return;
  }
  // No need to call `SetTimeline` and force compositor animation update -
  // timeline changing shouldn't cause change in animation state or playback
  // rate.
  aAnimation->SetTimelineNoUpdate(newTimeline, scopedName,
                                  Animation::FromJS::No);
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
      UpdateNamedTimelineAnimation(document, animation.get(), name.get(),
                                   SomeRef(mAnimationsWithDeferredUpdate));
    }
  }
#ifdef DEBUG
  CheckNamedTimelineMap(mAnimationsWithNamedTimeline);
#endif
}

void nsAnimationManager::UpdateAllNamedTimelineAnimations() {
  auto* document = mPresContext->Document();
  for (auto& entry : mAnimationsWithNamedTimeline) {
    const auto* name = entry.GetKey();
    for (auto& animation : entry.GetData()) {
      UpdateNamedTimelineAnimation(document, animation.get(), name,
                                   SomeRef(mAnimationsWithDeferredUpdate));
    }
  }
#ifdef DEBUG
  CheckNamedTimelineMap(mAnimationsWithNamedTimeline);
#endif
}

void nsAnimationManager::UpdateDeferredTimelineChanges() {
  if (mAnimationsWithDeferredUpdate.IsEmpty()) {
    return;
  }
  // This is unneeded once we resolve Bug 2033912.
  nsAutoScriptBlocker scriptBlocker;
  auto* document = mPresContext->Document();
  for (auto* animation : mAnimationsWithDeferredUpdate) {
    if (!animation->GetTimelineName().mName) {
      // May have switched to e.g. a document timeline.
      // That's ok, just skip it.
      continue;
    }
    UpdateNamedTimelineAnimation(document, animation,
                                 animation->GetTimelineName().mName, Nothing{});
  }
  mAnimationsWithDeferredUpdate.Clear();
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
        RemoveCorrespondingAnimation(animation->GetTimelineName().mName,
                                     animation, mAnimationsWithNamedTimeline);
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
      RemoveCorrespondingAnimation(anim->GetTimelineName().mName, anim,
                                   mAnimationsWithNamedTimeline);
    }
  }
}
