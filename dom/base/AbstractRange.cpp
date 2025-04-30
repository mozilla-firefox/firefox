/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/AbstractRange.h"
#include "mozilla/dom/AbstractRangeBinding.h"
#include "mozilla/dom/ShadowIncludingTreeIterator.h"

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/RangeUtils.h"
#include "mozilla/dom/ChildIterator.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/StaticRange.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/CrossShadowBoundaryRange.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionParticipant.h"
#include "nsGkAtoms.h"
#include "nsINode.h"
#include "nsRange.h"
#include "nsTArray.h"

namespace mozilla::dom {

template nsresult AbstractRange::SetStartAndEndInternal(
    const RangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary,
    nsRange* aRange);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RangeBoundary& aStartBoundary, const RawRangeBoundary& aEndBoundary,
    nsRange* aRange);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RawRangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary,
    nsRange* aRange);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RawRangeBoundary& aStartBoundary,
    const RawRangeBoundary& aEndBoundary, nsRange* aRange);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary,
    StaticRange* aRange);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RangeBoundary& aStartBoundary, const RawRangeBoundary& aEndBoundary,
    StaticRange* aRange);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RawRangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary,
    StaticRange* aRange);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RawRangeBoundary& aStartBoundary,
    const RawRangeBoundary& aEndBoundary, StaticRange* aRange);
template bool AbstractRange::MaybeCacheToReuse(nsRange& aInstance);
template bool AbstractRange::MaybeCacheToReuse(StaticRange& aInstance);
template bool AbstractRange::MaybeCacheToReuse(
    CrossShadowBoundaryRange& aInstance);

bool AbstractRange::sHasShutDown = false;

NS_IMPL_CYCLE_COLLECTING_ADDREF(AbstractRange)
NS_IMPL_CYCLE_COLLECTING_RELEASE(AbstractRange)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(AbstractRange)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(AbstractRange)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(AbstractRange)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOwner);
  // mStart and mEnd may depend on or be depended on some other members in
  // concrete classes so that they should be unlinked in sub classes.
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  tmp->mSelections.Clear();
  // Unregistering of the common inclusive ancestors would by design
  // also happen when the actual implementations unlink `mStart`/`mEnd`.
  // This may introduce additional overhead which is not needed when unlinking,
  // therefore this is done here beforehand.
  if (tmp->mRegisteredClosestCommonInclusiveAncestor) {
    tmp->UnregisterClosestCommonInclusiveAncestor(IsUnlinking::Yes);
  }
  MOZ_DIAGNOSTIC_ASSERT(!tmp->isInList(),
                        "Shouldn't be registered now that we're unlinking");

NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(AbstractRange)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOwner)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mStart)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEnd)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRegisteredClosestCommonInclusiveAncestor)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

// When aMarkDesendants is true, Set
// DescendantOfClosestCommonInclusiveAncestorForRangeInSelection flag for the
// shadow including children of aNode. When aMarkDesendants is false, unset that
// flag for the shadow including children of aNode.
void UpdateDescendantsByShadowIncludingOrder(const nsIContent& aNode,
                                             bool aMarkDesendants) {
  ShadowIncludingTreeIterator iter(*const_cast<nsIContent*>(&aNode));
  ++iter;  // We don't want to mark the root node

  while (iter) {
    nsINode* node = *iter;
    if (aMarkDesendants) {
      node->SetDescendantOfClosestCommonInclusiveAncestorForRangeInSelection();
    } else {
      node->ClearDescendantOfClosestCommonInclusiveAncestorForRangeInSelection();
    }

    if (node->IsClosestCommonInclusiveAncestorForRangeInSelection()) {
      iter.SkipChildren();
      continue;
    }
    ++iter;
  }
}

void AbstractRange::MarkDescendants(const nsINode& aNode) {
  // Set NodeIsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection on
  // aNode's descendants unless aNode is already marked as a range common
  // ancestor or a descendant of one, in which case all of our descendants have
  // the bit set already.
  if (!aNode.IsMaybeSelected()) {
    // If aNode has a web-exposed shadow root, use this shadow tree and ignore
    // the children of aNode.
    if (aNode.GetShadowRootForSelection()) {
      UpdateDescendantsByShadowIncludingOrder(*aNode.AsContent(), true);
      return;
    }
    // don't set the Descendant bit on |aNode| itself
    nsINode* node = aNode.GetNextNode(&aNode);
    while (node) {
      node->SetDescendantOfClosestCommonInclusiveAncestorForRangeInSelection();
      if (!node->IsClosestCommonInclusiveAncestorForRangeInSelection()) {
        if (StaticPrefs::dom_shadowdom_selection_across_boundary_enabled()) {
          UpdateDescendantsByShadowIncludingOrder(*node->AsContent(), true);
          // sub-tree of node has been marked already
          node = node->GetNextNonChildNode(&aNode);
        } else {
          node = node->GetNextNode(&aNode);
        }
      } else {
        // optimize: skip this sub-tree since it's marked already.
        node = node->GetNextNonChildNode(&aNode);
      }
    }
  }
}

void AbstractRange::UnmarkDescendants(const nsINode& aNode) {
  // Unset NodeIsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection
  // on aNode's descendants unless aNode is a descendant of another range common
  // ancestor. Also, exclude descendants of range common ancestors (but not the
  // common ancestor itself).
  if (!aNode
           .IsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection()) {
    // If aNode has a web-exposed shadow root, use this shadow tree and ignore
    // the children of aNode.
    if (aNode.GetShadowRootForSelection()) {
      UpdateDescendantsByShadowIncludingOrder(*aNode.AsContent(), false);
      return;
    }
    // we know |aNode| doesn't have any bit set
    nsINode* node = aNode.GetNextNode(&aNode);
    while (node) {
      node->ClearDescendantOfClosestCommonInclusiveAncestorForRangeInSelection();
      if (!node->IsClosestCommonInclusiveAncestorForRangeInSelection()) {
        if (StaticPrefs::dom_shadowdom_selection_across_boundary_enabled()) {
          UpdateDescendantsByShadowIncludingOrder(*node->AsContent(), false);
          // sub-tree has been marked already
          node = node->GetNextNonChildNode(&aNode);
        } else {
          node = node->GetNextNode(&aNode);
        }
      } else {
        // We found an ancestor of an overlapping range, skip its descendants.
        node = node->GetNextNonChildNode(&aNode);
      }
    }
  }
}

// NOTE: If you need to change default value of members of AbstractRange,
//       update nsRange::Create(nsINode* aNode) and ClearForReuse() too.
AbstractRange::AbstractRange(nsINode* aNode, bool aIsDynamicRange)
    : mRegisteredClosestCommonInclusiveAncestor(nullptr),
      mIsPositioned(false),
      mIsGenerated(false),
      mCalledByJS(false),
      mIsDynamicRange(aIsDynamicRange) {
  mRefCnt.SetIsOnMainThread();
  Init(aNode);
}

AbstractRange::~AbstractRange() = default;

void AbstractRange::Init(nsINode* aNode) {
  MOZ_ASSERT(aNode, "range isn't in a document!");
  mOwner = aNode->OwnerDoc();
}

// static
void AbstractRange::Shutdown() {
  sHasShutDown = true;
  if (nsTArray<RefPtr<nsRange>>* cachedRanges = nsRange::sCachedRanges) {
    nsRange::sCachedRanges = nullptr;
    cachedRanges->Clear();
    delete cachedRanges;
  }
  if (nsTArray<RefPtr<StaticRange>>* cachedRanges =
          StaticRange::sCachedRanges) {
    StaticRange::sCachedRanges = nullptr;
    cachedRanges->Clear();
    delete cachedRanges;
  }
  if (nsTArray<RefPtr<CrossShadowBoundaryRange>>* cachedRanges =
          CrossShadowBoundaryRange::sCachedRanges) {
    CrossShadowBoundaryRange::sCachedRanges = nullptr;
    cachedRanges->Clear();
    delete cachedRanges;
  }
}

// static
template <class RangeType>
bool AbstractRange::MaybeCacheToReuse(RangeType& aInstance) {
  static const size_t kMaxRangeCache = 64;

  // If the instance is not used by JS and the cache is not yet full, we
  // should reuse it.  Otherwise, delete it.
  if (sHasShutDown || aInstance.GetWrapperMaybeDead() || aInstance.GetFlags() ||
      (RangeType::sCachedRanges &&
       RangeType::sCachedRanges->Length() == kMaxRangeCache)) {
    return false;
  }

  aInstance.ClearForReuse();

  if (!RangeType::sCachedRanges) {
    RangeType::sCachedRanges = new nsTArray<RefPtr<RangeType>>(16);
  }
  RangeType::sCachedRanges->AppendElement(&aInstance);
  return true;
}

nsINode* AbstractRange::GetClosestCommonInclusiveAncestor(
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary) const {
  if (!mIsPositioned) {
    return nullptr;
  }
  nsINode* startContainer =
      aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes
          ? GetMayCrossShadowBoundaryStartContainer()
          : GetStartContainer();
  nsINode* endContainer =
      aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes
          ? GetMayCrossShadowBoundaryEndContainer()
          : GetEndContainer();

  if (MayCrossShadowBoundary() &&
      aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes) {
    // Since both the start container and the end container are
    // guaranteed to be in the same composed document.
    // If one of the boundary is a document, use that document
    // as the common ancestor since both nodes.
    const bool oneBoundaryIsDocument =
        (startContainer && startContainer->IsDocument()) ||
        (endContainer && endContainer->IsDocument());
    if (oneBoundaryIsDocument) {
      MOZ_ASSERT_IF(
          startContainer && startContainer->IsDocument(),
          !endContainer || endContainer->GetComposedDoc() == startContainer);
      MOZ_ASSERT_IF(
          endContainer && endContainer->IsDocument(),
          !startContainer || startContainer->GetComposedDoc() == endContainer);

      return startContainer ? startContainer->GetComposedDoc()
                            : endContainer->GetComposedDoc();
    }

    const auto rescope = [](nsINode*& aContainer) {
      if (!aContainer) {
        return;
      }
      // RangeBoundary allows the container to be shadow roots; When
      // this happens, we should use the shadow host here.
      if (auto* shadowRoot = ShadowRoot::FromNode(aContainer)) {
        aContainer = shadowRoot->GetHost();
        return;
      }
    };

    rescope(startContainer);
    rescope(endContainer);

    return nsContentUtils::GetCommonFlattenedTreeAncestorForSelection(
        startContainer ? startContainer->AsContent() : nullptr,
        endContainer ? endContainer->AsContent() : nullptr);
  }
  return nsContentUtils::GetClosestCommonInclusiveAncestor(startContainer,
                                                           endContainer);
}

// static
template <typename SPT, typename SRT, typename EPT, typename ERT,
          typename RangeType>
nsresult AbstractRange::SetStartAndEndInternal(
    const RangeBoundaryBase<SPT, SRT>& aStartBoundary,
    const RangeBoundaryBase<EPT, ERT>& aEndBoundary, RangeType* aRange) {
  if (NS_WARN_IF(!aStartBoundary.IsSet()) ||
      NS_WARN_IF(!aEndBoundary.IsSet())) {
    return NS_ERROR_INVALID_ARG;
  }

  nsINode* newStartRoot =
      RangeUtils::ComputeRootNode(aStartBoundary.GetContainer());
  if (!newStartRoot) {
    return NS_ERROR_DOM_INVALID_NODE_TYPE_ERR;
  }
  if (!aStartBoundary.IsSetAndValid()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  if (aStartBoundary.GetContainer() == aEndBoundary.GetContainer()) {
    if (!aEndBoundary.IsSetAndValid()) {
      return NS_ERROR_DOM_INDEX_SIZE_ERR;
    }
    // XXX: Offsets - handle this more efficiently.
    // If the end offset is less than the start offset, this should be
    // collapsed at the end offset.
    if (*aStartBoundary.Offset(
            RangeBoundaryBase<SPT, SRT>::OffsetFilter::kValidOffsets) >
        *aEndBoundary.Offset(
            RangeBoundaryBase<EPT, ERT>::OffsetFilter::kValidOffsets)) {
      aRange->DoSetRange(aEndBoundary, aEndBoundary, newStartRoot);
    } else {
      aRange->DoSetRange(aStartBoundary, aEndBoundary, newStartRoot);
    }
    return NS_OK;
  }

  nsINode* newEndRoot =
      RangeUtils::ComputeRootNode(aEndBoundary.GetContainer());
  if (!newEndRoot) {
    return NS_ERROR_DOM_INVALID_NODE_TYPE_ERR;
  }
  if (!aEndBoundary.IsSetAndValid()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  // Different root
  if (newStartRoot != newEndRoot) {
    if (aRange->IsStaticRange()) {
      // StaticRange allows nodes in different trees, so set start and end
      // accordingly
      aRange->DoSetRange(aStartBoundary, aEndBoundary, newEndRoot);
    } else {
      MOZ_ASSERT(aRange->IsDynamicRange());
      // In contrast, nsRange keeps both. It has a pair of start and end
      // which they have been collapsed to one end, and it also may have a pair
      // of start and end which are the original value.
      aRange->DoSetRange(aEndBoundary, aEndBoundary, newEndRoot);

      // Don't create the cross shadow bounday range if the one of the roots is
      // an UA widget regardless whether the boundaries are allowed to cross
      // shadow boundary or not.
      if (!IsRootUAWidget(newStartRoot) && !IsRootUAWidget(newEndRoot)) {
        aRange->AsDynamicRange()
            ->CreateOrUpdateCrossShadowBoundaryRangeIfNeeded(aStartBoundary,
                                                             aEndBoundary);
      }
    }
    return NS_OK;
  }

  const Maybe<int32_t> pointOrder =
      nsContentUtils::ComparePoints(aStartBoundary, aEndBoundary);
  if (!pointOrder) {
    // Safely return a value but also detected this in debug builds.
    MOZ_ASSERT_UNREACHABLE();
    return NS_ERROR_INVALID_ARG;
  }

  // If the end point is before the start point, this should be collapsed at
  // the end point.
  if (*pointOrder == 1) {
    aRange->DoSetRange(aEndBoundary, aEndBoundary, newEndRoot);
    return NS_OK;
  }

  // Otherwise, set the range as specified.
  aRange->DoSetRange(aStartBoundary, aEndBoundary, newStartRoot);
  return NS_OK;
}

bool AbstractRange::IsInSelection(const Selection& aSelection) const {
  return mSelections.Contains(&aSelection);
}

void AbstractRange::RegisterSelection(Selection& aSelection) {
  if (IsInSelection(aSelection)) {
    return;
  }
  bool isFirstSelection = mSelections.IsEmpty();
  mSelections.AppendElement(&aSelection);
  if (isFirstSelection && !mRegisteredClosestCommonInclusiveAncestor) {
    nsINode* commonAncestor = GetClosestCommonInclusiveAncestor(
        StaticPrefs::dom_shadowdom_selection_across_boundary_enabled()
            ? AllowRangeCrossShadowBoundary::Yes
            : AllowRangeCrossShadowBoundary::No);
    MOZ_ASSERT(commonAncestor, "unexpected disconnected nodes");
    RegisterClosestCommonInclusiveAncestor(commonAncestor);
  }
}

const nsTArray<WeakPtr<Selection>>& AbstractRange::GetSelections() const {
  return mSelections;
}

void AbstractRange::UnregisterSelection(const Selection& aSelection,
                                        IsUnlinking aIsUnlinking) {
  mSelections.RemoveElement(&aSelection);
  if (mSelections.IsEmpty() && mRegisteredClosestCommonInclusiveAncestor) {
    UnregisterClosestCommonInclusiveAncestor(aIsUnlinking);
    MOZ_DIAGNOSTIC_ASSERT(
        !mRegisteredClosestCommonInclusiveAncestor,
        "How can we have a registered common ancestor when we "
        "just unregistered?");
    MOZ_DIAGNOSTIC_ASSERT(
        !isInList(),
        "Shouldn't be registered if we have no "
        "mRegisteredClosestCommonInclusiveAncestor after unregistering");
  }
}

void AbstractRange::RegisterClosestCommonInclusiveAncestor(nsINode* aNode) {
  MOZ_ASSERT(aNode, "bad arg");

  MOZ_DIAGNOSTIC_ASSERT(IsInAnySelection(),
                        "registering range not in selection");

  mRegisteredClosestCommonInclusiveAncestor = aNode;

  MarkDescendants(*aNode);

  UniquePtr<LinkedList<AbstractRange>>& ranges =
      aNode->GetClosestCommonInclusiveAncestorRangesPtr();
  if (!ranges) {
    ranges = MakeUnique<LinkedList<AbstractRange>>();
  }

  MOZ_DIAGNOSTIC_ASSERT(!isInList());
  ranges->insertBack(this);
  aNode->SetClosestCommonInclusiveAncestorForRangeInSelection();
}

void AbstractRange::UnregisterClosestCommonInclusiveAncestor(
    IsUnlinking aIsUnlinking) {
  if (!mRegisteredClosestCommonInclusiveAncestor) {
    return;
  }
  nsCOMPtr oldClosestCommonInclusiveAncestor =
      mRegisteredClosestCommonInclusiveAncestor;
  mRegisteredClosestCommonInclusiveAncestor = nullptr;
  LinkedList<AbstractRange>* ranges =
      oldClosestCommonInclusiveAncestor
          ->GetExistingClosestCommonInclusiveAncestorRanges();
  MOZ_ASSERT(ranges);

#ifdef DEBUG
  bool found = false;
  for (AbstractRange* range : *ranges) {
    if (range == this) {
      found = true;
      break;
    }
  }
  MOZ_ASSERT(found,
             "We should be in the list on our registered common ancestor");
#endif  // DEBUG

  remove();

  // We don't want to waste time unmarking flags on nodes that are
  // being unlinked anyway.
  if (aIsUnlinking == IsUnlinking::No && ranges->isEmpty()) {
    oldClosestCommonInclusiveAncestor
        ->ClearClosestCommonInclusiveAncestorForRangeInSelection();
    UnmarkDescendants(*oldClosestCommonInclusiveAncestor);
  }
  oldClosestCommonInclusiveAncestor = nullptr;
}

void AbstractRange::UpdateCommonAncestorIfNecessary() {
  nsINode* oldCommonAncestor = mRegisteredClosestCommonInclusiveAncestor;
  nsINode* newCommonAncestor =
      GetClosestCommonInclusiveAncestor(AllowRangeCrossShadowBoundary::Yes);
  if (newCommonAncestor != oldCommonAncestor) {
    UnregisterClosestCommonInclusiveAncestor();

    if (newCommonAncestor) {
      RegisterClosestCommonInclusiveAncestor(newCommonAncestor);
    } else {
      MOZ_DIAGNOSTIC_ASSERT(!mIsPositioned, "unexpected disconnected nodes");
      mSelections.Clear();
      MOZ_DIAGNOSTIC_ASSERT(
          !mRegisteredClosestCommonInclusiveAncestor,
          "How can we have a registered common ancestor when we "
          "didn't register ourselves?");
      MOZ_DIAGNOSTIC_ASSERT(!isInList(),
                            "Shouldn't be registered if we have no "
                            "mRegisteredClosestCommonInclusiveAncestor");
    }
  }
}

const RangeBoundary& AbstractRange::MayCrossShadowBoundaryStartRef() const {
  return IsDynamicRange() ? AsDynamicRange()->MayCrossShadowBoundaryStartRef()
                          : mStart;
}

const RangeBoundary& AbstractRange::MayCrossShadowBoundaryEndRef() const {
  return IsDynamicRange() ? AsDynamicRange()->MayCrossShadowBoundaryEndRef()
                          : mEnd;
}

nsIContent* AbstractRange::GetMayCrossShadowBoundaryChildAtStartOffset() const {
  return IsDynamicRange()
             ? AsDynamicRange()->GetMayCrossShadowBoundaryChildAtStartOffset()
             : mStart.GetChildAtOffset();
}

nsIContent* AbstractRange::GetMayCrossShadowBoundaryChildAtEndOffset() const {
  return IsDynamicRange()
             ? AsDynamicRange()->GetMayCrossShadowBoundaryChildAtEndOffset()
             : mEnd.GetChildAtOffset();
}

nsINode* AbstractRange::GetMayCrossShadowBoundaryStartContainer() const {
  return IsDynamicRange()
             ? AsDynamicRange()->GetMayCrossShadowBoundaryStartContainer()
             : mStart.GetContainer();
}

nsINode* AbstractRange::GetMayCrossShadowBoundaryEndContainer() const {
  return IsDynamicRange()
             ? AsDynamicRange()->GetMayCrossShadowBoundaryEndContainer()
             : mEnd.GetContainer();
}

bool AbstractRange::MayCrossShadowBoundary() const {
  return IsDynamicRange() ? !!AsDynamicRange()->GetCrossShadowBoundaryRange()
                          : false;
}

uint32_t AbstractRange::MayCrossShadowBoundaryStartOffset() const {
  return IsDynamicRange()
             ? AsDynamicRange()->MayCrossShadowBoundaryStartOffset()
             : static_cast<uint32_t>(*mStart.Offset(
                   RangeBoundary::OffsetFilter::kValidOrInvalidOffsets));
}

uint32_t AbstractRange::MayCrossShadowBoundaryEndOffset() const {
  return IsDynamicRange()
             ? AsDynamicRange()->MayCrossShadowBoundaryEndOffset()
             : static_cast<uint32_t>(*mEnd.Offset(
                   RangeBoundary::OffsetFilter::kValidOrInvalidOffsets));
}

nsINode* AbstractRange::GetParentObject() const { return mOwner; }

JSObject* AbstractRange::WrapObject(JSContext* aCx,
                                    JS::Handle<JSObject*> aGivenProto) {
  MOZ_CRASH("Must be overridden");
}

bool AbstractRange::AreNormalRangeAndCrossShadowBoundaryRangeCollapsed() const {
  if (!Collapsed()) {
    return false;
  }

  // We know normal range is collapsed at this point
  if (IsStaticRange()) {
    return true;
  }

  if (const CrossShadowBoundaryRange* crossShadowBoundaryRange =
          AsDynamicRange()->GetCrossShadowBoundaryRange()) {
    return crossShadowBoundaryRange->Collapsed();
  }

  return true;
}

void AbstractRange::ClearForReuse() {
  mOwner = nullptr;
  mStart = RangeBoundary();
  mEnd = RangeBoundary();
  mIsPositioned = false;
  mIsGenerated = false;
  mCalledByJS = false;
}

/*static*/
bool AbstractRange::IsRootUAWidget(const nsINode* aRoot) {
  MOZ_ASSERT(aRoot);
  if (const ShadowRoot* shadowRoot = ShadowRoot::FromNode(aRoot)) {
    return shadowRoot->IsUAWidget();
  }
  return false;
}
}  // namespace mozilla::dom
