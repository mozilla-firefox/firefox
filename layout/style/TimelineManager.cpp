/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TimelineManager.h"

#include "mozilla/ElementAnimationData.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/ScrollTimeline.h"
#include "mozilla/dom/TimelineName.h"
#include "mozilla/dom/ViewTimeline.h"
#include "nsPresContext.h"

namespace mozilla {
using dom::Element;
using dom::ScrollTimeline;
using dom::ViewTimeline;

TimelineManager::TimelineManager(nsPresContext* aPresContext)
    : mPresContext(aPresContext) {}

template <typename TimelineType>
struct TimelineTargetMatches {
  bool operator()(const TimelineEntry<TimelineType>& aTimeline) {
    const auto target = aTimeline.mTimeline->TimelineTarget();
    return target.mElement == mElement &&
           target.mPseudoRequest == mPseudoRequest;
  }

  const Element* mElement;
  const PseudoStyleRequest& mPseudoRequest;
};

#ifdef DEBUG
template <typename TimelineType>
void TimelineManager::EnsureNoTimelineTarget(
    const TimelineTargetsIter<TimelineType>& aStart,
    const TimelineTargetsIter<TimelineType>& aEnd, const Element* aElement,
    const PseudoStyleRequest& aPseudoRequest) {
  const auto duplicateIt = std::find_if(
      aStart, aEnd,
      TimelineTargetMatches<TimelineType>{aElement, aPseudoRequest});
  // We should have one entry of the name for each target (See
  // `BuildTimelines`).
  MOZ_ASSERT(duplicateIt == aEnd, "Unexpected timeline target entry?");
}
#endif

template <typename TimelineType>
auto TimelineManager::FindInTimelineTargets(
    Timelines<TimelineType>& aTimelineTargets, const Element* aElement,
    const PseudoStyleRequest& aPseudoRequest)
    -> TimelineTargetsIter<TimelineType> {
  return std::find_if(
      aTimelineTargets.cbegin(), aTimelineTargets.cend(),
      TimelineTargetMatches<TimelineType>{aElement, aPseudoRequest});
}

template <typename TimelineType>
void TimelineManager::RemoveTimelineTargetByName(
    const nsAtom* aName, const Element* aElement,
    const PseudoStyleRequest& aPseudoRequest,
    TimelineNameMap<TimelineType>& aTimelineNameMap) {
  auto result = aTimelineNameMap.Lookup(aName);
  if (!result) {
    MOZ_ASSERT_UNREACHABLE("Trying to erase a non-existing timeline");
    return;
  }
  auto& targets = result.Data();
  MOZ_ASSERT(!targets.IsEmpty(), "Keeping an empty timeline list in map?");
  auto foundIt = FindInTimelineTargets(targets, aElement, aPseudoRequest);
  if (foundIt != targets.cend()) {
    DebugOnly<TimelineTargetsIter<TimelineType>> afterIt =
        targets.RemoveElementAt(foundIt);
#ifdef DEBUG
    EnsureNoTimelineTarget<TimelineType>(afterIt.value, targets.cend(),
                                         aElement, aPseudoRequest);
#endif
  }
  if (targets.IsEmpty()) {
    aTimelineNameMap.Remove(aName);
  }
}

template <typename TimelineType>
nsTArray<RefPtr<const nsAtom>> TimelineManager::TryDestroyTimeline(
    Element* aElement, const PseudoStyleRequest& aPseudoRequest,
    TimelineNameMap<TimelineType>& aTimelineNameMap) {
  auto* collection =
      TimelineCollection<TimelineType>::Get(aElement, aPseudoRequest);
  if (!collection) {
    return {};
  }
  nsTArray<RefPtr<const nsAtom>> result{collection->Timelines().Count()};
  for (const auto& name : collection->Timelines().Keys()) {
    result.AppendElement(name);
    RemoveTimelineTargetByName(name, aElement, aPseudoRequest,
                               aTimelineNameMap);
  }
  collection->Destroy();
  return result;
}

nsTArray<RefPtr<const nsAtom>> TimelineManager::UpdateTimelines(
    Element* aElement, const PseudoStyleRequest& aPseudoRequest,
    const ComputedStyle* aComputedStyle, ProgressTimelineType aType) {
  MOZ_ASSERT(
      aElement->IsInComposedDoc(),
      "No need to update timelines that are not attached to the document tree");

  // If we are in a display:none subtree we will have no computed values.
  // However, if we are on the root of display:none subtree, the computed values
  // might not have been cleared yet. In either case, since CSS animations
  // should not run in display:none subtrees, so we don't need timeline, either.
  const bool shouldDestroyTimelines =
      !aComputedStyle ||
      aComputedStyle->StyleDisplay()->mDisplay == StyleDisplay::None;

  switch (aType) {
    case ProgressTimelineType::Scroll:
      if (shouldDestroyTimelines) {
        return TryDestroyTimeline<ScrollTimeline>(aElement, aPseudoRequest,
                                                  mScrollTimelineNameMap);
      }
      return DoUpdateTimelines<ScrollTimeline>(
          mPresContext, aElement, aPseudoRequest,
          aComputedStyle->StyleUIReset(), mScrollTimelineNameMap);

    case ProgressTimelineType::View:
      if (shouldDestroyTimelines) {
        return TryDestroyTimeline<ViewTimeline>(aElement, aPseudoRequest,
                                                mViewTimelineNameMap);
      }
      return DoUpdateTimelines<ViewTimeline>(
          mPresContext, aElement, aPseudoRequest,
          aComputedStyle->StyleUIReset(), mViewTimelineNameMap);
  }
  MOZ_ASSERT_UNREACHABLE("Unhandled timelinetype?");
  return {};
}

void TimelineManager::UpdateTimelineScopes(
    Element* aElement, const ComputedStyle* aComputedStyle) {
  // For now, a scope specified on a pseudo-element is recorded against its
  // originating element.
  const auto pseudo = PseudoStyleRequest::NotPseudo();
  auto* scope = TimelineScope::Get(aElement, pseudo);
  const auto timelineScope =
      aComputedStyle->StyleUIReset()->mTimelineScope.value.AsSpan();
  if (timelineScope.IsEmpty()) {
    MOZ_ASSERT(scope, "Timeline scopes out of sync");
    if (scope) {
      scope->Destroy();
    }
    return;
  }

  if (!scope) {
    scope =
        &aElement->EnsureAnimationData().EnsureTimelineScope(*aElement, pseudo);
    mTimelineScopes.insertBack(scope);
  }

  nsTArray<RefPtr<const nsAtom>> names;
  if (timelineScope[0].AsAtom() == nsGkAtoms::all) {
    MOZ_ASSERT(timelineScope.Length() == 1);
  } else {
    names.SetCapacity(timelineScope.Length());
    for (const auto& name : timelineScope) {
      names.AppendElement(name.AsAtom());
    }
  }
  scope->SetNames(std::move(names));
}

static bool ScopedNameLooselyMatches(const dom::ShadowRoot* aTargetShadowRoot,
                                     const dom::Element* aTimelineElement,
                                     StyleCascadeLevel aTimelineCascadeLevel) {
  const auto* timelineShadowRoot =
      Servo_GetShadowRootForScoped(aTimelineElement, aTimelineCascadeLevel);
  for (auto* root = aTargetShadowRoot; root;
       root = root->Host()->GetContainingShadow()) {
    // Is `timelineShadowRoot` an ancestor of `aTargetShadowRoot`?
    if (root == timelineShadowRoot) {
      return true;
    }
  }
  // Reached light DOM, so is the timeline there too?
  return !timelineShadowRoot;
}

/* static */
bool TimelineManager::TimelineNameScopedByElement(const dom::Element* aElement,
                                                  const nsAtom* aName) {
  const auto* scope =
      TimelineScope::Get(aElement, PseudoStyleRequest::NotPseudo());
  return scope && scope->Scopes(aName);
}

RefPtr<dom::AnimationTimeline> TimelineManager::GetNamedTimelineForThisElement(
    const dom::Element* aElement, const PseudoStyleRequest& aPseudoRequest,
    const nsAtom* aName, const dom::ShadowRoot* aTargetShadowRoot) {
  if (auto* collection =
          TimelineCollection<ScrollTimeline>::Get(aElement, aPseudoRequest)) {
    auto result = collection->Lookup(aName);
    if (result.mTimeline &&
        ScopedNameLooselyMatches(aTargetShadowRoot, aElement,
                                 result.mCascadeLevel)) {
      return result.mTimeline;
    }
  }

  if (auto* collection =
          TimelineCollection<ViewTimeline>::Get(aElement, aPseudoRequest)) {
    auto result = collection->Lookup(aName);
    if (result.mTimeline &&
        ScopedNameLooselyMatches(aTargetShadowRoot, aElement,
                                 result.mCascadeLevel)) {
      return result.mTimeline;
    }
  }
  return nullptr;
}

already_AddRefed<dom::AnimationTimeline>
TimelineManager::GetNamedTimelineInSubtree(
    const dom::Element* aRoot, const nsAtom* aName,
    const dom::ShadowRoot* aTargetShadowRoot, dom::Document* aDocument) const {
  auto* scrollCandidate = DoGetNamedTimelineInSubtree(
      aRoot, aName, mScrollTimelineNameMap, aTargetShadowRoot);
  auto* viewCandidate = DoGetNamedTimelineInSubtree(
      aRoot, aName, mViewTimelineNameMap, aTargetShadowRoot);
  if (!scrollCandidate && !viewCandidate) {
    // If we're at the document, we aren't going to find a timeline.
    // https://drafts.csswg.org/scroll-animations-1/#timeline-scoping
    return MakeAndAddRef<dom::UnresolvedTimeline>(aDocument);
  }

  auto* picked = [&]() -> dom::AnimationTimeline* {
    if (!viewCandidate) {
      return scrollCandidate;
    } else if (!scrollCandidate) {
      return viewCandidate;
    }

    auto GetElement = [](const ScrollTimeline* aTarget) {
      const auto target = aTarget->TimelineTarget();
      return target.mElement->GetPseudoElement(target.mPseudoRequest);
    };
    // Ok, which one is defined later in tree order?
    const auto* scrollElement = GetElement(scrollCandidate);
    const auto* viewElement = GetElement(viewCandidate);
    const auto compare =
        nsContentUtils::CompareTreePosition<TreeKind::ShadowIncludingDOM>(
            scrollElement, viewElement, aRoot);
    if (compare >= 0) {
      return scrollCandidate;
    }
    return viewCandidate;
  }();

  if (!picked) {
    MOZ_ASSERT_UNREACHABLE("Should have picked a non-null result");
    return nullptr;
  }

  picked->AddRef();
  return already_AddRefed{picked};
}

template <typename TimelineType>
TimelineType* TimelineManager::DoGetNamedTimelineInSubtree(
    const dom::Element* aRoot, const nsAtom* aName,
    const TimelineNameMap<TimelineType>& aTimelineNameMap,
    const dom::ShadowRoot* aTargetShadowRoot) const {
  const auto candidates = aTimelineNameMap.Lookup(aName);
  if (!candidates) {
    return nullptr;
  }

  auto IsValid = [&](const Element* aTimelineCandidate, const Element* aRoot) {
    const auto* e = aTimelineCandidate;
    for (; e && e != aRoot; e = e->GetParentElementCrossingShadowRoot()) {
      if (TimelineNameScopedByElement(e, aName)) {
        // Scope already limited by some other element, bail.
        // TODO(dshin): This is a lot of linear traversals...
        return false;
      }
    }
    // Needs to be a descendant of this subtree.
    return e == aRoot;
  };

  // Reverse-iterate - Because the list is tree-order sorted, the first valid
  // one we find this way must be the last in tree order.
  for (auto itr = candidates.Data().crbegin(); itr != candidates.Data().crend();
       ++itr) {
    auto* timeline = itr->mTimeline.get();
    const auto* e = timeline->TimelineTarget().mElement;
    if (!IsValid(e, aRoot)) {
      continue;
    }
    if (!ScopedNameLooselyMatches(aTargetShadowRoot, e, itr->mCascadeLevel)) {
      continue;
    }
    return itr->mTimeline;
  }
  return nullptr;
}

template <typename TimelineType>
static already_AddRefed<TimelineType> PopExistingTimeline(
    const nsAtom* aName, TimelineCollection<TimelineType>* aCollection) {
  if (!aCollection) {
    return nullptr;
  }
  return aCollection->Extract(aName);
}

// Per-property cycling: when {scroll,view}-timeline-axis (or
// view-timeline-inset) has fewer values than view-timeline-name, the value list
// is repeated to match. See
// https://drafts.csswg.org/css-values-4/#linked-properties
template <typename TimelineType>
struct TimelineBuilder;

template <>
struct TimelineBuilder<ScrollTimeline> {
  static size_t NameCount(const nsStyleUIReset* aUI) {
    return aUI->mScrollTimelineNameCount;
  }
  static dom::ScopedTimelineName Name(const nsStyleUIReset* aUI, size_t aIdx) {
    const auto& scopedName = aUI->GetScrollTimelineName(aIdx);
    return dom::ScopedTimelineName{scopedName};
  }
  static already_AddRefed<ScrollTimeline> Make(
      nsPresContext* aPC, Element* aElement, const PseudoStyleRequest& aPseudo,
      const nsStyleUIReset* aUI, size_t aIdx) {
    return ScrollTimeline::MakeNamed(aPC->Document(), aElement, aPseudo,
                                     aUI->GetScrollTimelineAxis(aIdx));
  }
  static void Replace(ScrollTimeline* aDest, Element* aElement,
                      const PseudoStyleRequest& aPseudo,
                      const dom::ScopedTimelineName& aName,
                      const nsStyleUIReset* aUI, size_t aIdx) {
    aDest->ReplacePropertiesWith(aElement, aPseudo, aName,
                                 aUI->GetScrollTimelineAxis(aIdx));
  }
};

template <>
struct TimelineBuilder<ViewTimeline> {
  static size_t NameCount(const nsStyleUIReset* aUI) {
    return aUI->mViewTimelineNameCount;
  }
  static dom::ScopedTimelineName Name(const nsStyleUIReset* aUI, size_t aIdx) {
    const auto& scopedName = aUI->GetViewTimelineName(aIdx);
    return dom::ScopedTimelineName{scopedName};
  }
  static already_AddRefed<ViewTimeline> Make(nsPresContext* aPC,
                                             Element* aElement,
                                             const PseudoStyleRequest& aPseudo,
                                             const nsStyleUIReset* aUI,
                                             size_t aIdx) {
    return ViewTimeline::MakeNamed(aPC->Document(), aElement, aPseudo,
                                   aUI->GetViewTimelineAxis(aIdx),
                                   aUI->GetViewTimelineInset(aIdx));
  }
  static void Replace(ViewTimeline* aDest, Element* aElement,
                      const PseudoStyleRequest& aPseudo,
                      const dom::ScopedTimelineName& aName,
                      const nsStyleUIReset* aUI, size_t aIdx) {
    aDest->ReplacePropertiesWith(aElement, aPseudo, aName,
                                 aUI->GetViewTimelineAxis(aIdx),
                                 aUI->GetViewTimelineInset(aIdx));
  }
};

template <typename TimelineType>
static auto BuildTimelines(nsPresContext* aPresContext, Element* aElement,
                           const PseudoStyleRequest& aPseudoRequest,
                           const nsStyleUIReset* aUIReset,
                           TimelineCollection<TimelineType>* aCollection) {
  using Builder = TimelineBuilder<TimelineType>;
  typename TimelineCollection<TimelineType>::TimelineMap result;
  const size_t count = Builder::NameCount(aUIReset);
  // If multiple timelines are attempting to modify the same property, then the
  // timeline closest to the end of the list of names wins [1].
  // [1]: https://drafts.csswg.org/scroll-animations-1/#timeline-scoping
  for (size_t idx = 0; idx < count; ++idx) {
    auto scopedName = Builder::Name(aUIReset, idx);
    if (scopedName.mName == nsGkAtoms::_empty) {
      continue;
    }

    RefPtr<TimelineType> dest =
        PopExistingTimeline(scopedName.mName, aCollection);
    if (dest) {
      Builder::Replace(dest, aElement, aPseudoRequest, scopedName, aUIReset,
                       idx);
    } else {
      dest =
          Builder::Make(aPresContext, aElement, aPseudoRequest, aUIReset, idx);
    }
    MOZ_ASSERT(dest);

    // Override the previous one if it is duplicated.
    const TimelineEntry<TimelineType> entry{
        .mTimeline = dest,
        .mCascadeLevel = scopedName.mCascadeLevel,
    };
    (void)result.InsertOrUpdate(scopedName.mName, entry);
  }
  return result;
}

template <typename TimelineType>
static TimelineCollection<TimelineType>& EnsureTimelineCollection(
    Element& aElement, const PseudoStyleRequest& aPseudoRequest);

template <>
ScrollTimelineCollection& EnsureTimelineCollection<ScrollTimeline>(
    Element& aElement, const PseudoStyleRequest& aPseudoRequest) {
  return aElement.EnsureAnimationData().EnsureScrollTimelineCollection(
      aElement, aPseudoRequest);
}

template <>
ViewTimelineCollection& EnsureTimelineCollection<ViewTimeline>(
    Element& aElement, const PseudoStyleRequest& aPseudoRequest) {
  return aElement.EnsureAnimationData().EnsureViewTimelineCollection(
      aElement, aPseudoRequest);
}

template <typename TimelineType>
nsTArray<RefPtr<const nsAtom>> TimelineManager::DoUpdateTimelines(
    nsPresContext* aPresContext, Element* aElement,
    const PseudoStyleRequest& aPseudoRequest, const nsStyleUIReset* aUIReset,
    TimelineNameMap<TimelineType>& aTimelineNameMap) {
  using Builder = TimelineBuilder<TimelineType>;
  auto* collection =
      TimelineCollection<TimelineType>::Get(aElement, aPseudoRequest);
  if (!collection && Builder::NameCount(aUIReset) == 1 &&
      Builder::Name(aUIReset, 0).mName == nsGkAtoms::_empty) {
    return {};
  }

  // We create a new timeline list based on its computed style and the existing
  // timelines.
  auto newTimelines = BuildTimelines<TimelineType>(
      aPresContext, aElement, aPseudoRequest, aUIReset, collection);

  if (newTimelines.IsEmpty()) {
    nsTArray<RefPtr<const nsAtom>> result{
        collection ? collection->Timelines().Count() : 0};
    if (collection) {
      for (const auto& name : collection->Timelines().Keys()) {
        result.AppendElement(name);
        RemoveTimelineTargetByName(name, aElement, aPseudoRequest,
                                   aTimelineNameMap);
      }
      collection->Destroy();
    }
    return result;
  }

  if (!collection) {
    collection =
        &EnsureTimelineCollection<TimelineType>(*aElement, aPseudoRequest);
    if (!collection->isInList()) {
      AddTimelineCollection(collection);
    }
  }

  nsTArray<RefPtr<const nsAtom>> result{collection->Timelines().Count() +
                                        newTimelines.Count()};
  for (const auto& removed : collection->Timelines().Keys()) {
    result.AppendElement(removed);
    RemoveTimelineTargetByName(removed, aElement, aPseudoRequest,
                               aTimelineNameMap);
  }

  // Replace unused timeline with new ones.
  collection->Swap(newTimelines);

  for (auto addedOrExisting = collection->Timelines().ConstIter();
       !addedOrExisting.Done(); addedOrExisting.Next()) {
    auto& targets = aTimelineNameMap.LookupOrInsert(addedOrExisting.Key(),
                                                    Timelines<TimelineType>{});
    auto foundIt = FindInTimelineTargets(targets, aElement, aPseudoRequest);
    if (foundIt != targets.cend()) {
#ifdef DEBUG
      EnsureNoTimelineTarget<TimelineType>(foundIt + 1, targets.cend(),
                                           aElement, aPseudoRequest);
#endif
      continue;
    }
    result.AppendElement(addedOrExisting.Key());
    const auto& entry = addedOrExisting.Data();
    const auto target = entry.mTimeline->TimelineTarget();
    const auto* element =
        target.mElement->GetPseudoElement(target.mPseudoRequest);
    struct ElementComparator {
      const Element* mElement;
      int32_t operator()(const TimelineEntry<TimelineType>& aOther) const {
        const auto target = aOther.mTimeline->TimelineTarget();
        const auto* element =
            target.mElement->GetPseudoElement(target.mPseudoRequest);
        return nsContentUtils::CompareTreePosition<
            TreeKind::ShadowIncludingDOM>(mElement, element, nullptr);
      }
    };

    ElementComparator cmp{element};
    auto matchOrInsertionIdx = targets.Length();
    if (BinarySearchIf(targets, 0, targets.Length(), cmp,
                       &matchOrInsertionIdx)) {
      if (MOZ_UNLIKELY(targets.ElementAt(matchOrInsertionIdx).mTimeline ==
                       entry.mTimeline)) {
        MOZ_ASSERT_UNREACHABLE("Duplicate entry");
        continue;
      }
    }

    targets.InsertElementAt(matchOrInsertionIdx, entry);
  }

  // FIXME: Bug 1774060. We may have to restyle the animations which use the
  // dropped timelines. Or rely on restyling the subtree and the following
  // siblings when mutating {scroll|view}-timeline-name.
  return result;
}

}  // namespace mozilla
