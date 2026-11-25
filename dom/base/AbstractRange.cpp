/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/AbstractRange.h"

#include "mozilla/Assertions.h"
#include "mozilla/ContentIterator.h"
#include "mozilla/PresShell.h"
#include "mozilla/RangeUtils.h"
#include "mozilla/SelectionMovementUtils.h"
#include "mozilla/dom/AbstractRangeBinding.h"
#include "mozilla/dom/ChildIterator.h"
#include "mozilla/dom/CrossShadowBoundaryRange.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Range.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/ShadowIncludingTreeIterator.h"
#include "mozilla/dom/StaticRange.h"
#include "mozilla/dom/TreeIterator.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionParticipant.h"
#include "nsFmtString.h"
#include "nsINode.h"
#include "nsLayoutUtils.h"
#include "nsTArray.h"
#include "nsTextFrame.h"

namespace mozilla::dom {

template nsresult AbstractRange::SetStartAndEndInternal(
    const RangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary,
    Range* aRange, AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RangeBoundary& aStartBoundary, const RawRangeBoundary& aEndBoundary,
    Range* aRange, AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RawRangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary,
    Range* aRange, AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RawRangeBoundary& aStartBoundary,
    const RawRangeBoundary& aEndBoundary, Range* aRange,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary,
    StaticRange* aRange,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RangeBoundary& aStartBoundary, const RawRangeBoundary& aEndBoundary,
    StaticRange* aRange,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RawRangeBoundary& aStartBoundary, const RangeBoundary& aEndBoundary,
    StaticRange* aRange,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template nsresult AbstractRange::SetStartAndEndInternal(
    const RawRangeBoundary& aStartBoundary,
    const RawRangeBoundary& aEndBoundary, StaticRange* aRange,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary);
template bool AbstractRange::MaybeCacheToReuse(Range& aInstance);
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

void AbstractRange::UpdateDescendantsInFlattenedTree(nsINode& aNode,
                                                     bool aMarkDescendants) {
  auto UpdateDescendant = [aMarkDescendants](nsINode* node) {
    if (aMarkDescendants) {
      node->SetDescendantOfClosestCommonInclusiveAncestorForRangeInSelection();
    } else {
      node->ClearDescendantOfClosestCommonInclusiveAncestorForRangeInSelection();
    }
  };

  nsINode* target = &aNode;

  if (target->IsDocument()) {
    if (auto* rootElement = aNode.AsDocument()->GetRootElement()) {
      target = rootElement;
      UpdateDescendant(target);
    }
  }

  if (!target || !target->IsContent()) {
    return;
  }

  TreeIterator<FlattenedChildIteratorForSelection> iter(*target->AsContent());
  iter.GetNext();  // Skip aNode itself.
  while (nsIContent* curNode = iter.GetCurrent()) {
    UpdateDescendant(curNode);
    if (curNode->IsClosestCommonInclusiveAncestorForRangeInSelection()) {
      iter.GetNextSkippingChildren();
    } else {
      iter.GetNext();
    }
  }
}

void AbstractRange::MarkDescendants(nsINode& aNode) {
  // Set NodeIsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection on
  // aNode's descendants unless aNode is already marked as a range common
  // ancestor or a descendant of one, in which case all of our descendants have
  // the bit set already.
  if (!aNode.IsMaybeSelected()) {
    // If aNode has a web-exposed shadow root, use this shadow tree and ignore
    // the children of aNode.

    UpdateDescendantsInFlattenedTree(aNode, true /* aMarkDescendants */);
  }
}

void AbstractRange::UnmarkDescendants(nsINode& aNode) {
  // Unset NodeIsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection
  // on aNode's descendants unless aNode is a descendant of another range common
  // ancestor. Also, exclude descendants of range common ancestors (but not the
  // common ancestor itself).
  if (!aNode
           .IsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection()) {
    UpdateDescendantsInFlattenedTree(aNode, false /* aMarkDescendants */);
  }
}

// NOTE: If you need to change default value of members of AbstractRange,
//       update Range::Create(nsINode* aNode) and ClearForReuse() too.
AbstractRange::AbstractRange(nsINode* aNode, bool aIsRange,
                             TreeKind aBoundaryTreeKind)
    : mStart(aBoundaryTreeKind),
      mEnd(aBoundaryTreeKind),
      mRegisteredClosestCommonInclusiveAncestor(nullptr),
      mIsPositioned(false),
      mIsGenerated(false),
      mCalledByJS(false),
      mIsRange(aIsRange) {
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
  if (nsTArray<RefPtr<Range>>* cachedRanges = Range::sCachedRanges) {
    Range::sCachedRanges = nullptr;
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
  nsINode* startContainer = ShadowDOMSelectionHelpers::GetStartContainer(
      this, aAllowCrossShadowBoundary);
  nsINode* endContainer = ShadowDOMSelectionHelpers::GetEndContainer(
      this, aAllowCrossShadowBoundary);

  if (aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes) {
    if (startContainer == endContainer) {
      return startContainer;
    }
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
    const RangeBoundaryBase<EPT, ERT>& aEndBoundary, RangeType* aRange,
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary) {
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
      MOZ_ASSERT(aRange->IsRange());
      // In contrast, Range keeps both. It has a pair of start and end
      // which they have been collapsed to one end, and it also may have a pair
      // of start and end which are the original value.
      aRange->DoSetRange(aEndBoundary, aEndBoundary, newEndRoot);

      // Don't create the cross shadow bounday range if the one of the roots is
      // an UA widget regardless whether the boundaries are allowed to cross
      // shadow boundary or not.
      if (aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes &&
          !IsRootUAWidget(newStartRoot) && !IsRootUAWidget(newEndRoot)) {
        const auto startInFlat =
            aStartBoundary.AsRangeBoundaryInFlatTreeOrNonFlattenedNode(
                RangeBoundaryFor::Start);
        const auto endInFlat =
            aEndBoundary.AsRangeBoundaryInFlatTreeOrNonFlattenedNode(
                RangeBoundaryFor::End);
        if (MOZ_UNLIKELY(!startInFlat.IsSet() || !endInFlat.IsSet())) {
          NS_WARNING_ASSERTION(
              !startInFlat.IsSet(),
              nsFmtCString(
                  FMT_STRING("aStartBoundary={} could not convert to a "
                             "point in the flat tree"),
                  aStartBoundary)
                  .get());
          NS_WARNING_ASSERTION(
              !endInFlat.IsSet(),
              nsFmtCString(FMT_STRING("aEndBoundary={} could not convert to a "
                                      "point in the flat tree"),
                           aEndBoundary)
                  .get());
          return NS_ERROR_FAILURE;
        }
        aRange->AsRange()->CreateOrUpdateCrossShadowBoundaryRangeIfNeeded(
            startInFlat, endInFlat);
      }
    }
    return NS_OK;
  }

  const bool useFlatTree =
      aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes;
  const Maybe<int32_t> pointOrder =
      useFlatTree ? nsContentUtils::ComparePoints<TreeKind::FlatForSelection>(
                        aStartBoundary, aEndBoundary)
                  : nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
                        aStartBoundary, aEndBoundary);
  if (!pointOrder) {
    // Safely return a value but also detected this in debug builds.
    MOZ_ASSERT_UNREACHABLE("The boundaries are not connected");
    return NS_ERROR_INVALID_ARG;
  }

  // If the end point is before the start point, this should be collapsed at
  // the end point.
  if (*pointOrder == 1) {
    aRange->DoSetRange(aEndBoundary, aEndBoundary, newEndRoot);
    return NS_OK;
  }

  // Otherwise, set the range as specified.  However, the order may be opposite
  // in the same tree if the given range is for the flat tree.  Thus, we need to
  // recompute the order within the same tree if we computed the order in the
  // flat tree.
  if (!useFlatTree) {
    aRange->DoSetRange(aStartBoundary, aEndBoundary, newStartRoot);
  } else {
    const Maybe<int32_t> pointOrderInSameTree =
        nsContentUtils::ComparePoints<TreeKind::DOM>(aStartBoundary,
                                                     aEndBoundary);
    if (MOZ_UNLIKELY(pointOrderInSameTree.isNothing())) {
      MOZ_ASSERT_UNREACHABLE(
          "The boundaries are not connected in the same DOM tree");
      aRange->DoSetRange(aEndBoundary, aEndBoundary, newStartRoot);
    } else if (*pointOrderInSameTree != 1) {
      aRange->DoSetRange(aStartBoundary, aEndBoundary, newStartRoot);
    } else {
      aRange->DoSetRange(aEndBoundary, aStartBoundary, newStartRoot);
    }
  }

  if (aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes &&
      aRange->IsRange()) {
    const bool isCollapsing = aStartBoundary == aEndBoundary;
    const auto startInFlat = aStartBoundary
                                 .AsRangeBoundaryInFlatTreeOrNonFlattenedNode(
                                     isCollapsing ? RangeBoundaryFor::Collapsed
                                                  : RangeBoundaryFor::Start)
                                 .AsRaw();
    const auto endInFlat =
        isCollapsing ? startInFlat
                     : aEndBoundary
                           .AsRangeBoundaryInFlatTreeOrNonFlattenedNode(
                               RangeBoundaryFor::End)
                           .AsRaw();
    if (MOZ_UNLIKELY(!startInFlat.IsSet() || !endInFlat.IsSet())) {
      NS_WARNING_ASSERTION(
          !startInFlat.IsSet(),
          nsFmtCString(FMT_STRING("aStartBoundary={} could not convert to a "
                                  "point in the flat tree"),
                       aStartBoundary)
              .get());
      NS_WARNING_ASSERTION(
          !endInFlat.IsSet(),
          nsFmtCString(FMT_STRING("aEndBoundary={} could not convert to a "
                                  "point in the flat tree"),
                       aEndBoundary)
              .get());
      return NS_ERROR_FAILURE;
    }

    aRange->AsRange()->CreateOrUpdateCrossShadowBoundaryRangeIfNeeded(
        startInFlat, endInFlat);
  }

  return NS_OK;
}

bool AbstractRange::IsInSelection(const Selection& aSelection) const {
  return mSelections.Contains(&aSelection);
}

nsresult AbstractRange::RegisterSelection(Selection& aSelection) {
  if (IsInSelection(aSelection)) {
    return NS_OK;
  }
  const Maybe<nsINode*> commonAncestor = [&]() -> Maybe<nsINode*> {
    const bool isFirstSelection = mSelections.IsEmpty();
    const bool isValidRange = !IsStaticRange() || AsStaticRange()->IsValid();
    if (isFirstSelection && !mRegisteredClosestCommonInclusiveAncestor &&
        isValidRange) {
      return Some(GetClosestCommonInclusiveAncestor(
          AllowRangeCrossShadowBoundary::Yes));
    }
    return Nothing{};
  }();
  if (commonAncestor.isSome() && NS_WARN_IF(!commonAncestor.value()))
      [[unlikely]] {
    NS_WARNING(
        fmt::format("start:{}", MayCrossShadowBoundaryStartRef()).c_str());
    NS_WARNING(fmt::format("end:  {}", MayCrossShadowBoundaryEndRef()).c_str());
    MOZ_ASSERT_UNREACHABLE("The boundaries must be connected");
    return NS_ERROR_FAILURE;
  }
  mSelections.AppendElement(&aSelection);
  if (commonAncestor.isSome()) {
    RegisterClosestCommonInclusiveAncestor(commonAncestor.value());
  }
  return NS_OK;
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
    ranges = std::make_unique<LinkedList<AbstractRange>>();
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
  return IsRange() ? AsRange()->MayCrossShadowBoundaryStartRef() : mStart;
}

const RangeBoundary& AbstractRange::MayCrossShadowBoundaryEndRef() const {
  return IsRange() ? AsRange()->MayCrossShadowBoundaryEndRef() : mEnd;
}

nsIContent* AbstractRange::GetMayCrossShadowBoundaryChildAtStartOffset() const {
  return IsRange() ? AsRange()->GetMayCrossShadowBoundaryChildAtStartOffset()
                   : mStart.GetChildAtOffset();
}

nsIContent* AbstractRange::GetMayCrossShadowBoundaryChildAtEndOffset() const {
  return IsRange() ? AsRange()->GetMayCrossShadowBoundaryChildAtEndOffset()
                   : mEnd.GetChildAtOffset();
}

nsINode* AbstractRange::GetMayCrossShadowBoundaryStartContainer() const {
  return IsRange() ? AsRange()->GetMayCrossShadowBoundaryStartContainer()
                   : mStart.GetContainer();
}

nsINode* AbstractRange::GetMayCrossShadowBoundaryEndContainer() const {
  return IsRange() ? AsRange()->GetMayCrossShadowBoundaryEndContainer()
                   : mEnd.GetContainer();
}

bool AbstractRange::MayCrossShadowBoundary() const {
  return IsRange() ? !!AsRange()->GetCrossShadowBoundaryRange() : false;
}

uint32_t AbstractRange::MayCrossShadowBoundaryStartOffset() const {
  return IsRange() ? AsRange()->MayCrossShadowBoundaryStartOffset()
                   : static_cast<uint32_t>(*mStart.Offset(
                         RangeBoundary::OffsetFilter::kValidOrInvalidOffsets));
}

uint32_t AbstractRange::MayCrossShadowBoundaryEndOffset() const {
  return IsRange() ? AsRange()->MayCrossShadowBoundaryEndOffset()
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
          AsRange()->GetCrossShadowBoundaryRange()) {
    return crossShadowBoundaryRange->Collapsed();
  }

  return true;
}

void AbstractRange::ClearForReuse() {
  mOwner = nullptr;
  mStart = RangeBoundary(mStart.GetTreeKind());
  mEnd = RangeBoundary(mEnd.GetTreeKind());
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

already_AddRefed<StaticRange> AbstractRange::GetShrunkenRangeToVisibleLeaves()
    const {
  if (NS_WARN_IF(!IsPositioned()) || NS_WARN_IF(Collapsed()) ||
      NS_WARN_IF(IsStaticRange() && !AsStaticRange()->IsValid())) {
    return nullptr;
  }

  const RawRangeBoundary startBoundary =
      SelectionMovementUtils::GetFirstVisiblePointAtLeaf(*this);
  if (MOZ_UNLIKELY(!startBoundary.IsSet())) {
    return nullptr;
  }
  const RawRangeBoundary endBoundary =
      SelectionMovementUtils::GetLastVisiblePointAtLeaf(*this);
  if (MOZ_UNLIKELY(!endBoundary.IsSet())) {
    return nullptr;
  }
  IgnoredErrorResult error;
  RefPtr<StaticRange> range =
      StaticRange::Create(startBoundary, endBoundary, error);
  if (NS_WARN_IF(error.Failed())) {
    error.SuppressException();
    return nullptr;
  }
  return range.forget();
}

static void ExtractRectFromOffset(nsIFrame* aFrame, const int32_t aOffset,
                                  nsRect* aR, bool aFlushToOriginEdge,
                                  bool aClampToEdge) {
  MOZ_ASSERT(aFrame);
  MOZ_ASSERT(aR);

  nsPoint point;
  aFrame->GetPointFromOffset(aOffset, &point);

  // Determine if aFrame has a vertical writing mode, which will change our math
  // on the output rect.
  bool isVertical = aFrame->GetWritingMode().IsVertical();

  if (!aClampToEdge && !aR->Contains(point)) {
    // If point is outside aR, and we aren't clamping, output an empty rect
    // with origin at the point.
    if (isVertical) {
      aR->SetHeight(0);
      aR->y = point.y;
    } else {
      aR->SetWidth(0);
      aR->x = point.x;
    }
    return;
  }

  if (aClampToEdge) {
    point = aR->ClampPoint(point);
  }

  // point is within aR, and now we'll modify aR to output a rect that has point
  // on one edge. But which edge?
  if (aFlushToOriginEdge) {
    // The output rect should be flush to the edge of aR that contains the
    // origin.
    if (isVertical) {
      aR->SetHeight(point.y - aR->y);
    } else {
      aR->SetWidth(point.x - aR->x);
    }
  } else {
    // The output rect should be flush to the edge of aR opposite the origin.
    if (isVertical) {
      aR->SetHeight(aR->YMost() - point.y);
      aR->y = point.y;
    } else {
      aR->SetWidth(aR->XMost() - point.x);
      aR->x = point.x;
    }
  }
}

static nsTextFrame* GetTextFrameForContent(nsIContent* aContent) {
  return do_QueryFrame(aContent->GetPrimaryFrame());
}

static void GetPartialTextRect(RectCallback* aCallback,
                               Sequence<nsString>* aTextList,
                               nsIContent* aContent, int32_t aStartOffset,
                               int32_t aEndOffset, bool aClampToEdge) {
  nsTextFrame* textFrame = GetTextFrameForContent(aContent);
  if (!textFrame) {
    return;
  }
  nsIFrame* relativeTo =
      nsLayoutUtils::GetContainingBlockForClientRect(textFrame);

  for (nsTextFrame* f = textFrame->FindContinuationForOffset(aStartOffset); f;
       f = static_cast<nsTextFrame*>(f->GetNextContinuation())) {
    int32_t fstart = f->GetContentOffset(), fend = f->GetContentEnd();
    if (fend <= aStartOffset) {
      continue;
    }
    if (fstart >= aEndOffset) {
      break;
    }

    // Calculate the text content offsets we'll need if text is requested.
    int32_t textContentStart = fstart;
    int32_t textContentEnd = fend;

    // overlapping with the offset we want
    f->EnsureTextRun(nsTextFrame::eInflated);
    gfxTextRun* run = f->GetTextRun(nsTextFrame::eInflated);
    if (NS_WARN_IF(!run)) {
      continue;
    }
    bool topLeftToBottomRight = !run->IsInlineReversed();
    nsRect r = f->GetRectRelativeToSelf();
    if (fstart < aStartOffset) {
      // aStartOffset is within this frame
      ExtractRectFromOffset(f, aStartOffset, &r, !topLeftToBottomRight,
                            aClampToEdge);
      textContentStart = aStartOffset;
    }
    if (fend > aEndOffset) {
      // aEndOffset is in the middle of this frame
      ExtractRectFromOffset(f, aEndOffset, &r, topLeftToBottomRight,
                            aClampToEdge);
      textContentEnd = aEndOffset;
    }
    r = nsLayoutUtils::TransformFrameRectToAncestor(f, r, relativeTo);
    aCallback->AddRect(r);

    // Finally capture the text, if requested.
    if (aTextList) {
      nsIFrame::RenderedText renderedText =
          f->GetRenderedText(textContentStart, textContentEnd,
                             nsIFrame::TextOffsetType::OffsetsInContentText,
                             nsIFrame::TrailingWhitespace::DontTrim);

      if (!aTextList->AppendElement(renderedText.mString, fallible)) {
        return;
      }
    }
  }
}

static void CollectClientRectsForSubtree(
    nsINode* aNode, RectCallback* aCollector, Sequence<nsString>* aTextList,
    nsINode* aStartContainer, uint32_t aStartOffset, nsINode* aEndContainer,
    uint32_t aEndOffset, bool aClampToEdge, bool aTextOnly) {
  auto* content = nsIContent::FromNode(aNode);
  if (!content) {
    return;
  }

  const bool isText = content->IsText();
  if (isText) {
    if (aNode == aStartContainer) {
      int32_t offset = aStartContainer == aEndContainer
                           ? static_cast<int32_t>(aEndOffset)
                           : content->AsText()->TextDataLength();
      GetPartialTextRect(aCollector, aTextList, content,
                         static_cast<int32_t>(aStartOffset), offset,
                         aClampToEdge);
      return;
    }

    if (aNode == aEndContainer) {
      GetPartialTextRect(aCollector, aTextList, content, 0,
                         static_cast<int32_t>(aEndOffset), aClampToEdge);
      return;
    }
  }

  if (nsIFrame* frame = content->GetPrimaryFrame()) {
    if (!aTextOnly || isText) {
      nsLayoutUtils::GetAllInFlowRectsAndTexts(
          frame, nsLayoutUtils::GetContainingBlockForClientRect(frame),
          aCollector, aTextList,
          nsLayoutUtils::GetAllInFlowRectsFlag::AccountForTransforms);
      if (isText) {
        return;
      }
      aTextOnly = true;
      // We just get the text when calling GetAllInFlowRectsAndTexts, so we
      // don't need to call it again when visiting the children.
      aTextList = nullptr;
    }
  } else if (!content->IsElement() ||
             !content->AsElement()->IsDisplayContents()) {
    return;
  }

  FlattenedChildIteratorForSelection childIter(content);
  for (nsIContent* child = childIter.GetNextChild(); child;
       child = childIter.GetNextChild()) {
    CollectClientRectsForSubtree(child, aCollector, aTextList, aStartContainer,
                                 aStartOffset, aEndContainer, aEndOffset,
                                 aClampToEdge, aTextOnly);
  }
}

/* static */
void AbstractRange::CollectClientRectsAndText(
    RectCallback* aCollector, Sequence<nsString>* aTextList,
    AbstractRange* aRange, nsINode* aStartContainer, uint32_t aStartOffset,
    nsINode* aEndContainer, uint32_t aEndOffset, bool aClampToEdge,
    bool aFlushLayout) {
  // Currently, this method is called with start of end offset of AbstractRange.
  // So, they must be between 0 - INT32_MAX.
  MOZ_ASSERT(RangeUtils::IsValidOffset(aStartOffset));
  MOZ_ASSERT(RangeUtils::IsValidOffset(aEndOffset));

  // Hold strong pointers across the flush
  nsCOMPtr<nsINode> startContainer = aStartContainer;
  nsCOMPtr<nsINode> endContainer = aEndContainer;

  // Flush out layout so our frames are up to date.
  if (!aStartContainer->IsInComposedDoc()) {
    return;
  }

  if (aFlushLayout) {
    if (auto* content = nsIContent::FromNode(aStartContainer)) {
      content->GetPrimaryFrame(FlushType::Layout);
    } else {
      aStartContainer->OwnerDoc()->FlushPendingNotifications(FlushType::Layout);
    }
    // Recheck whether we're still in the document
    if (!aStartContainer->IsInComposedDoc()) {
      return;
    }
  }

  RangeSubtreeIterator iter;

  if (NS_FAILED(iter.Init(aRange))) {
    return;
  }

  if (iter.IsDone()) {
    // the range is collapsed, only continue if the cursor is in a text node
    if (!aStartContainer->IsText()) {
      return;
    }
    nsTextFrame* textFrame = GetTextFrameForContent(aStartContainer->AsText());
    if (!textFrame) {
      return;
    }
    int32_t outOffset = 0;
    nsIFrame* outFrame = nullptr;
    textFrame->GetChildFrameContainingOffset(static_cast<int32_t>(aStartOffset),
                                             false, &outOffset, &outFrame);
    if (!outFrame) {
      return;
    }
    nsIFrame* relativeTo =
        nsLayoutUtils::GetContainingBlockForClientRect(outFrame);
    nsRect r = outFrame->GetRectRelativeToSelf();
    ExtractRectFromOffset(outFrame, static_cast<int32_t>(aStartOffset), &r,
                          false, aClampToEdge);
    r.SetWidth(0);
    r = nsLayoutUtils::TransformFrameRectToAncestor(outFrame, r, relativeTo);
    aCollector->AddRect(r);
    return;
  }

  do {
    nsCOMPtr<nsINode> node = iter.GetCurrentNode();
    iter.Next();

    CollectClientRectsForSubtree(node, aCollector, aTextList, aStartContainer,
                                 aStartOffset, aEndContainer, aEndOffset,
                                 aClampToEdge, false);
  } while (!iter.IsDone());
}

already_AddRefed<DOMRect> AbstractRange::GetBoundingClientRect(
    bool aClampToEdge, bool aFlushLayout) {
  RefPtr<DOMRect> rect = new DOMRect(ToSupports(mOwner));
  if (!mIsPositioned) {
    return rect.forget();
  }

  nsLayoutUtils::RectAccumulator accumulator;
  CollectClientRectsAndText(
      &accumulator, nullptr, this, mStart.GetContainer(),
      *mStart.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
      mEnd.GetContainer(),
      *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets), aClampToEdge,
      aFlushLayout);

  nsRect r = accumulator.mResultRect.IsEmpty() ? accumulator.mFirstRect
                                               : accumulator.mResultRect;
  rect->SetLayoutRect(r);
  return rect.forget();
}

already_AddRefed<DOMRectList> AbstractRange::GetClientRects(bool aClampToEdge,
                                                            bool aFlushLayout) {
  return GetClientRectsInner(AllowRangeCrossShadowBoundary::No, aClampToEdge,
                             aFlushLayout);
}

already_AddRefed<DOMRectList>
AbstractRange::GetAllowCrossShadowBoundaryClientRects(bool aClampToEdge,
                                                      bool aFlushLayout) {
  return GetClientRectsInner(AllowRangeCrossShadowBoundary::Yes, aClampToEdge,
                             aFlushLayout);
}

void AbstractRange::CollectClientRects(RectCallback& aCallback,
                                       bool aClampToEdge) const {
  if (!mIsPositioned) {
    return;
  }
  CollectClientRectsAndText(
      &aCallback, nullptr, const_cast<AbstractRange*>(this),
      mStart.GetContainer(),
      *mStart.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
      mEnd.GetContainer(),
      *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets), aClampToEdge,
      /* aFlushLayout */ false);
}

already_AddRefed<DOMRectList> AbstractRange::GetClientRectsInner(
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundaryRange,
    bool aClampToEdge, bool aFlushLayout) {
  if (!mIsPositioned) {
    return nullptr;
  }

  RefPtr<DOMRectList> rectList = new DOMRectList(ToSupports(mOwner));

  nsLayoutUtils::RectListBuilder builder(rectList);

  const auto& startRef =
      aAllowCrossShadowBoundaryRange == AllowRangeCrossShadowBoundary::Yes
          ? MayCrossShadowBoundaryStartRef()
          : mStart;
  const auto& endRef =
      aAllowCrossShadowBoundaryRange == AllowRangeCrossShadowBoundary::Yes
          ? MayCrossShadowBoundaryEndRef()
          : mEnd;

  CollectClientRectsAndText(
      &builder, nullptr, this, startRef.GetContainer(),
      *startRef.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
      endRef.GetContainer(),
      *endRef.Offset(RangeBoundary::OffsetFilter::kValidOffsets), aClampToEdge,
      aFlushLayout);
  return rectList.forget();
}

void AbstractRange::GetClientRectsAndTexts(
    mozilla::dom::ClientRectsAndTexts& aResult, ErrorResult& aErr) {
  if (!mIsPositioned) {
    return;
  }

  aResult.mRectList = new DOMRectList(ToSupports(mOwner));

  nsLayoutUtils::RectListBuilder builder(aResult.mRectList);

  CollectClientRectsAndText(
      &builder, &aResult.mTextList, this, mStart.GetContainer(),
      *mStart.Offset(RangeBoundary::OffsetFilter::kValidOffsets),
      mEnd.GetContainer(),
      *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOffsets), true, true);
}

}  // namespace mozilla::dom
