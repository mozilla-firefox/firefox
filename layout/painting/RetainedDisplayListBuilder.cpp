/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "RetainedDisplayListBuilder.h"

#include "mozilla/Attributes.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/DisplayPortUtils.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/StaticPrefs_layout.h"
#include "nsCanvasFrame.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsPlaceholderFrame.h"
#include "nsSubDocumentFrame.h"

/**
 * Code for doing display list building for a modified subset of the window,
 * and then merging it into the existing display list (for the full window).
 *
 * The approach primarily hinges on the observation that the 'true' ordering
 * of display items is represented by a DAG (only items that intersect in 2d
 * space have a defined ordering). Our display list is just one of a many
 * possible linear representations of this ordering.
 *
 * Each time a frame changes (gets a new ComputedStyle, or has a size/position
 * change), we schedule a paint (as we do currently), but also reord the frame
 * that changed.
 *
 * When the next paint occurs we union the overflow areas (in screen space) of
 * the changed frames, and compute a rect/region that contains all changed
 * items. We then build a display list just for this subset of the screen and
 * merge it into the display list from last paint.
 *
 * Any items that exist in one list and not the other must not have a defined
 * ordering in the DAG, since they need to intersect to have an ordering and
 * we would have built both in the new list if they intersected. Given that, we
 * can align items that appear in both lists, and any items that appear between
 * matched items can be inserted into the merged list in any order.
 *
 * Frames that are a stacking context, containing blocks for position:fixed
 * descendants, and don't have any continuations (see
 * CanStoreDisplayListBuildingRect) trigger recursion into the algorithm with
 * separate retaining decisions made.
 *
 * RDL defines the concept of an AnimatedGeometryRoot (AGR), the nearest
 * ancestor frame which can be moved asynchronously on the compositor thread.
 * These are currently nsDisplayItems which return true from CanMoveAsync
 * (animated nsDisplayTransform and nsDisplayStickyPosition) and
 * ActiveScrolledRoots.
 *
 * For each context that we run the retaining algorithm, there can only be
 * mutations to one AnimatedGeometryRoot. This is because we are unable to
 * reason about intersections of items that might then move relative to each
 * other without RDL running again. If there are mutations to multiple
 * AnimatedGeometryRoots, then we bail out and rebuild all the items in the
 * context.
 *
 * Otherwise, when mutations are restricted to a single AGR, we pre-process the
 * old display list and mark the frames for all existing (unmodified!) items
 * that belong to a different AGR and ensure that we rebuild those items for
 * correct sorting with the modified ones.
 */

namespace mozilla {

RetainedDisplayListData::RetainedDisplayListData()
    : mModifiedFrameLimit(
          StaticPrefs::layout_display_list_rebuild_frame_limit()) {}

void RetainedDisplayListData::AddModifiedFrame(nsIFrame* aFrame) {
  MOZ_ASSERT(!aFrame->IsFrameModified());
  Flags(aFrame) += RetainedDisplayListData::FrameFlag::Modified;
  aFrame->SetFrameIsModified(true);
  mModifiedFrameCount++;
}

static void MarkFramesWithItemsAndImagesModified(nsDisplayList* aList) {
  for (nsDisplayItem* i : *aList) {
    if (!i->HasDeletedFrame() && i->CanBeReused() &&
        !i->Frame()->IsFrameModified()) {
      // If we have existing cached geometry for this item, then check that for
      // whether we need to invalidate for a sync decode. If we don't, then
      // use the item's flags.
      // XXX: handle webrender case by looking up retained data for the item
      // and checking InvalidateForSyncDecodeImages
      bool invalidate = false;
      if (!(i->GetFlags() & TYPE_RENDERS_NO_IMAGES)) {
        invalidate = true;
      }

      if (invalidate) {
        DL_LOGV("RDL - Invalidating item %p (%s)", i, i->Name());
        i->FrameForInvalidation()->MarkNeedsDisplayItemRebuild();
        if (i->GetDependentFrame()) {
          i->GetDependentFrame()->MarkNeedsDisplayItemRebuild();
        }
      }
    }
    if (i->GetChildren()) {
      MarkFramesWithItemsAndImagesModified(i->GetChildren());
    }
  }
}

static nsIFrame* SelectAGRForFrame(nsIFrame* aFrame, nsIFrame* aParentAGR) {
  if (!aFrame->IsStackingContext() || !aFrame->IsFixedPosContainingBlock()) {
    return aParentAGR;
  }

  if (!aFrame->HasOverrideDirtyRegion()) {
    return nullptr;
  }

  nsDisplayListBuilder::DisplayListBuildingData* data =
      aFrame->GetProperty(nsDisplayListBuilder::DisplayListBuildingRect());

  return data && data->mModifiedAGR ? data->mModifiedAGR : nullptr;
}

void RetainedDisplayListBuilder::AddSizeOfIncludingThis(
    nsWindowSizes& aSizes) const {
  aSizes.mLayoutRetainedDisplayListSize += aSizes.mState.mMallocSizeOf(this);
  mBuilder.AddSizeOfExcludingThis(aSizes);
  mList.AddSizeOfExcludingThis(aSizes);
}

bool AnyContentAncestorModified(nsIFrame* aFrame, nsIFrame* aStopAtFrame) {
  nsIFrame* f = aFrame;
  while (f) {
    if (f->IsFrameModified()) {
      return true;
    }

    if (aStopAtFrame && f == aStopAtFrame) {
      break;
    }

    f = nsLayoutUtils::GetDisplayListParent(f);
  }

  return false;
}

// Removes any display items that belonged to a frame that was deleted,
// and mark frames that belong to a different AGR so that get their
// items built again.
// TODO: We currently descend into all children even if we don't have an AGR
// to mark, as child stacking contexts might. It would be nice if we could
// jump into those immediately rather than walking the entire thing.
bool RetainedDisplayListBuilder::PreProcessDisplayList(
    RetainedDisplayList* aList, nsIFrame* aAGR, PartialUpdateResult& aUpdated,
    nsIFrame* aAsyncAncestor, const ActiveScrolledRoot* aAsyncAncestorASR,
    nsIFrame* aOuterFrame, uint32_t aCallerKey, uint32_t aNestingDepth,
    bool aKeepLinked) {
  // The DAG merging algorithm does not have strong mechanisms in place to keep
  // the complexity of the resulting DAG under control. In some cases we can
  // build up edges very quickly. Detect those cases and force a full display
  // list build if we hit them.
  static const uint32_t kMaxEdgeRatio = 5;
  const bool initializeDAG = !aList->mDAG.Length();
  if (!aKeepLinked && !initializeDAG &&
      aList->mDAG.mDirectPredecessorList.Length() >
          (aList->mDAG.mNodesInfo.Length() * kMaxEdgeRatio)) {
    return false;
  }

  // If we had aKeepLinked=true for this list on the previous paint, then
  // mOldItems will already be initialized as it won't have been consumed during
  // a merge.
  const bool initializeOldItems = aList->mOldItems.IsEmpty();
  if (initializeOldItems) {
    aList->mOldItems.SetCapacity(aList->Length());
  } else {
    MOZ_RELEASE_ASSERT(!initializeDAG);
  }

  MOZ_RELEASE_ASSERT(
      initializeDAG ||
      aList->mDAG.Length() ==
          (initializeOldItems ? aList->Length() : aList->mOldItems.Length()));

  nsDisplayList out;

  size_t i = 0;
  while (nsDisplayItem* item = aList->RemoveBottom()) {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    item->SetMergedPreProcessed(false, true);
#endif

    // If we have a previously initialized old items list, then it can differ
    // from the current list due to items removed for having a deleted frame.
    // We can't easily remove these, since the DAG has entries for those indices
    // and it's hard to rewrite in-place.
    // Skip over entries with no current item to keep the iterations in sync.
    if (!initializeOldItems) {
      while (!aList->mOldItems[i].mItem) {
        i++;
      }
    }

    if (initializeDAG) {
      if (i == 0) {
        aList->mDAG.AddNode(Span<const MergedListIndex>());
      } else {
        MergedListIndex previous(i - 1);
        aList->mDAG.AddNode(Span<const MergedListIndex>(&previous, 1));
      }
    }

    if (!item->CanBeReused() || item->HasDeletedFrame() ||
        AnyContentAncestorModified(item->FrameForInvalidation(), aOuterFrame)) {
      if (initializeOldItems) {
        aList->mOldItems.AppendElement(OldItemInfo(nullptr));
      } else {
        MOZ_RELEASE_ASSERT(aList->mOldItems[i].mItem == item);
        aList->mOldItems[i].mItem = nullptr;
      }

      item->Destroy(&mBuilder);
      Metrics()->mRemovedItems++;

      i++;
      aUpdated = PartialUpdateResult::Updated;
      continue;
    }

    if (initializeOldItems) {
      aList->mOldItems.AppendElement(OldItemInfo(item));
    }

    // If we're not going to keep the list linked, then this old item entry
    // is the only pointer to the item. Let it know that it now strongly
    // owns the item, so it can destroy it if it goes away.
    aList->mOldItems[i].mOwnsItem = !aKeepLinked;

    item->SetOldListIndex(aList, OldListIndex(i), aCallerKey, aNestingDepth);

    nsIFrame* f = item->Frame();

    if (item->GetChildren()) {
      // If children inside this list were invalid, then we'd have walked the
      // ancestors and set ForceDescendIntoVisible on the current frame. If an
      // ancestor is modified, then we'll throw this away entirely. Either way,
      // we won't need to run merging on this sublist, and we can keep the items
      // linked into their display list.
      // The caret can move without invalidating, but we always set the force
      // descend into frame state bit on that frame, so check for that too.
      // TODO: AGR marking below can call MarkFrameForDisplayIfVisible and make
      // us think future siblings need to be merged, even though we don't really
      // need to.
      bool keepLinked = aKeepLinked;
      nsIFrame* invalid = item->FrameForInvalidation();
      if (!invalid->ForceDescendIntoIfVisible() &&
          !invalid->HasAnyStateBits(NS_FRAME_FORCE_DISPLAY_LIST_DESCEND_INTO)) {
        keepLinked = true;
      }

      // If this item's frame is an AGR (can be moved asynchronously by the
      // compositor), then use that frame for descendants. Also pass the ASR
      // for that item, so that descendants can compare to see if any new
      // ASRs have been pushed since.
      nsIFrame* asyncAncestor = aAsyncAncestor;
      const ActiveScrolledRoot* asyncAncestorASR = aAsyncAncestorASR;
      if (item->CanMoveAsync()) {
        asyncAncestor = item->Frame();
        asyncAncestorASR = item->GetNearestScrollASR();
      }

      if (!PreProcessDisplayList(
              item->GetChildren(), SelectAGRForFrame(f, aAGR), aUpdated,
              asyncAncestor, asyncAncestorASR, item->Frame(),
              item->GetPerFrameKey(), aNestingDepth + 1, keepLinked)) {
        MOZ_RELEASE_ASSERT(
            !aKeepLinked,
            "Can't early return since we need to move the out list back");
        return false;
      }
    }

    // TODO: We should be able to check the clipped bounds relative
    // to the common AGR (of both the existing item and the invalidated
    // frame) and determine if they can ever intersect.
    // TODO: We only really need to build the ancestor container item that is a
    // sibling of the changed thing to get correct ordering. The changed content
    // is a frame though, and it's hard to map that to container items in this
    // list.
    // If an ancestor display item is an AGR, and our ASR matches the ASR
    // of that item, then there can't have been any new ASRs pushed since that
    // item, so that item is our AGR. Otherwise, our AGR is our ASR.
    // TODO: If aAsyncAncestorASR is non-null, then item->GetActiveScrolledRoot
    // should be the same or a descendant and also non-null. Unfortunately an
    // RDL bug means this can be wrong for sticky items after a partial update,
    // so we have to work around it. Bug 1730749 and bug 1730826 should resolve
    // this.
    nsIFrame* agrFrame = nullptr;
    const ActiveScrolledRoot* asr = item->GetNearestScrollASR();
    if (aAsyncAncestorASR == asr || !asr) {
      agrFrame = aAsyncAncestor;
    } else {
      auto* scrollContainerFrame = asr->ScrollFrame();
      if (MOZ_UNLIKELY(!scrollContainerFrame)) {
        MOZ_DIAGNOSTIC_ASSERT(false);
        gfxCriticalNoteOnce << "Found null mScrollContainerFrame in asr";
        return false;
      }
      agrFrame = scrollContainerFrame->GetScrolledFrame();
    }

    if (aAGR && agrFrame != aAGR) {
      mBuilder.MarkFrameForDisplayIfVisible(f, RootReferenceFrame());
    }

    // If we're going to keep this linked list and not merge it, then mark the
    // item as used and put it back into the list.
    if (aKeepLinked) {
      if (item->GetChildren()) {
        item->UpdateBounds(Builder());
      }
      if (item->GetType() == DisplayItemType::TYPE_SUBDOCUMENT) {
        IncrementSubDocPresShellPaintCount(item);
      }
      out.AppendToTop(item);
    }
    i++;
  }

  MOZ_RELEASE_ASSERT(aList->mOldItems.Length() == aList->mDAG.Length());

  if (aKeepLinked) {
    aList->AppendToTop(&out);
  }

  return true;
}

void IncrementPresShellPaintCount(nsDisplayListBuilder* aBuilder,
                                  nsDisplayItem* aItem) {
  MOZ_ASSERT(aItem->GetType() == DisplayItemType::TYPE_SUBDOCUMENT);

  nsSubDocumentFrame* subDocFrame =
      static_cast<nsDisplaySubDocument*>(aItem)->SubDocumentFrame();
  MOZ_ASSERT(subDocFrame);

  PresShell* presShell = subDocFrame->GetSubdocumentPresShellForPainting(0);
  MOZ_ASSERT(presShell);

  aBuilder->IncrementPresShellPaintCount(presShell);
}

void RetainedDisplayListBuilder::IncrementSubDocPresShellPaintCount(
    nsDisplayItem* aItem) {
  IncrementPresShellPaintCount(&mBuilder, aItem);
}

static Maybe<const ActiveScrolledRoot*> SelectContainerASR(
    const DisplayItemClipChain* aClipChain, const ActiveScrolledRoot* aItemASR,
    Maybe<const ActiveScrolledRoot*>& aContainerASR) {
  const ActiveScrolledRoot* itemClipASR =
      aClipChain ? aClipChain->mASR : nullptr;

  MOZ_DIAGNOSTIC_ASSERT(!aClipChain || aClipChain->mOnStack || !itemClipASR ||
                        itemClipASR->mFrame);

  const ActiveScrolledRoot* finiteBoundsASR =
      ActiveScrolledRoot::PickDescendant(itemClipASR, aItemASR);

  if (!aContainerASR) {
    return Some(finiteBoundsASR);
  }

  return Some(
      ActiveScrolledRoot::PickAncestor(*aContainerASR, finiteBoundsASR));
}

static void UpdateASR(nsDisplayItem* aItem,
                      Maybe<const ActiveScrolledRoot*>& aContainerASR) {
  const Maybe<const ActiveScrolledRoot*> frameASR =
      aItem->GetBaseASRForAncestorOfContainedASR();
  if (!frameASR) {
    return;
  }

  if (!aContainerASR) {
    aItem->SetActiveScrolledRoot(*frameASR);
    return;
  }

  aItem->SetActiveScrolledRoot(
      ActiveScrolledRoot::PickAncestor(*frameASR, *aContainerASR));
}

static void CopyASR(nsDisplayItem* aOld, nsDisplayItem* aNew) {
  aNew->SetActiveScrolledRoot(aOld->GetActiveScrolledRoot());
}

OldItemInfo::OldItemInfo(nsDisplayItem* aItem)
    : mItem(aItem), mUsed(false), mDiscarded(false), mOwnsItem(false) {
  if (mItem) {
    // Clear cached modified frame state when adding an item to the old list.
    mItem->SetModifiedFrame(false);
  }
}

void OldItemInfo::AddedMatchToMergedList(RetainedDisplayListBuilder* aBuilder,
                                         MergedListIndex aIndex) {
  AddedToMergedList(aIndex);
}

void OldItemInfo::Discard(RetainedDisplayListBuilder* aBuilder,
                          nsTArray<MergedListIndex>&& aDirectPredecessors) {
  MOZ_ASSERT(!IsUsed());
  mUsed = mDiscarded = true;
  mDirectPredecessors = std::move(aDirectPredecessors);
  if (mItem) {
    MOZ_ASSERT(mOwnsItem);
    mItem->Destroy(aBuilder->Builder());
    aBuilder->Metrics()->mRemovedItems++;
  }
  mItem = nullptr;
}

bool OldItemInfo::IsChanged() {
  return !mItem || !mItem->CanBeReused() || mItem->HasDeletedFrame();
}

/**
 * A C++ implementation of Markus Stange's merge-dags algorithm.
 * https://github.com/mstange/merge-dags
 *
 * MergeState handles combining a new list of display items into an existing
 * DAG and computes the new DAG in a single pass.
 * Each time we add a new item, we resolve all dependencies for it, so that the
 * resulting list and DAG are built in topological ordering.
 */
class MergeState {
 public:
  MergeState(RetainedDisplayListBuilder* aBuilder,
             RetainedDisplayList& aOldList, nsDisplayItem* aOuterItem)
      : mBuilder(aBuilder),
        mOldList(&aOldList),
        mOldItems(std::move(aOldList.mOldItems)),
        mOldDAG(
            std::move(*reinterpret_cast<DirectedAcyclicGraph<OldListUnits>*>(
                &aOldList.mDAG))),
        mOuterItem(aOuterItem),
        mResultIsModified(false) {
    mMergedDAG.EnsureCapacityFor(mOldDAG);
    MOZ_RELEASE_ASSERT(mOldItems.Length() == mOldDAG.Length());
  }

  Maybe<MergedListIndex> ProcessItemFromNewList(
      nsDisplayItem* aNewItem, const Maybe<MergedListIndex>& aPreviousItem) {
    OldListIndex oldIndex;
    MOZ_DIAGNOSTIC_ASSERT(aNewItem->HasModifiedFrame() ==
                          HasModifiedFrame(aNewItem));
    if (!aNewItem->HasModifiedFrame() &&
        HasMatchingItemInOldList(aNewItem, &oldIndex)) {
      mBuilder->Metrics()->mRebuiltItems++;
      nsDisplayItem* oldItem = mOldItems[oldIndex.val].mItem;
      MOZ_DIAGNOSTIC_ASSERT(oldItem->GetPerFrameKey() ==
                                aNewItem->GetPerFrameKey() &&
                            oldItem->Frame() == aNewItem->Frame());
      if (!mOldItems[oldIndex.val].IsChanged()) {
        MOZ_DIAGNOSTIC_ASSERT(!mOldItems[oldIndex.val].IsUsed());
        nsDisplayItem* destItem;
        if (ShouldUseNewItem(aNewItem)) {
          destItem = aNewItem;
        } else {
          destItem = oldItem;
          // The building rect can depend on the overflow rect (when the parent
          // frame is position:fixed), which can change without invalidating
          // the frame/items. If we're using the old item, copy the building
          // rect across from the new item.
          oldItem->SetBuildingRect(aNewItem->GetBuildingRect());
        }

        MergeChildLists(aNewItem, oldItem, destItem);

        AutoTArray<MergedListIndex, 2> directPredecessors =
            ProcessPredecessorsOfOldNode(oldIndex);
        MergedListIndex newIndex = AddNewNode(
            destItem, Some(oldIndex), directPredecessors, aPreviousItem);
        mOldItems[oldIndex.val].AddedMatchToMergedList(mBuilder, newIndex);
        if (destItem == aNewItem) {
          oldItem->Destroy(mBuilder->Builder());
        } else {
          aNewItem->Destroy(mBuilder->Builder());
        }
        return Some(newIndex);
      }
    }
    mResultIsModified = true;
    return Some(AddNewNode(aNewItem, Nothing(), Span<MergedListIndex>(),
                           aPreviousItem));
  }

  void MergeChildLists(nsDisplayItem* aNewItem, nsDisplayItem* aOldItem,
                       nsDisplayItem* aOutItem) {
    if (!aOutItem->GetChildren()) {
      return;
    }

    Maybe<const ActiveScrolledRoot*> containerASRForChildren;
    nsDisplayList empty;
    const bool modified = mBuilder->MergeDisplayLists(
        aNewItem ? aNewItem->GetChildren() : &empty, aOldItem->GetChildren(),
        aOutItem->GetChildren(), containerASRForChildren, aOutItem);
    if (modified) {
      aOutItem->InvalidateCachedChildInfo(mBuilder->Builder());
      UpdateASR(aOutItem, containerASRForChildren);
      mResultIsModified = true;
    } else if (aOutItem == aNewItem) {
      // If nothing changed, but we copied the contents across to
      // the new item, then also copy the ASR data.
      CopyASR(aOldItem, aNewItem);
    }
    // Ideally we'd only UpdateBounds if something changed, but
    // nsDisplayWrapList also uses this to update the clip chain for the
    // current ASR, which gets reset during RestoreState(), so we always need
    // to run it again.
    aOutItem->UpdateBounds(mBuilder->Builder());

    if (aOutItem->GetType() == DisplayItemType::TYPE_TRANSFORM) {
      MOZ_ASSERT(!aNewItem ||
                 aNewItem->GetType() == DisplayItemType::TYPE_TRANSFORM);
      MOZ_ASSERT(aOldItem->GetType() == DisplayItemType::TYPE_TRANSFORM);
      static_cast<nsDisplayTransform*>(aOutItem)->SetContainsASRs(
          static_cast<nsDisplayTransform*>(aOldItem)->GetContainsASRs() ||
          (aNewItem
               ? static_cast<nsDisplayTransform*>(aNewItem)->GetContainsASRs()
               : false));
    }
  }

  bool ShouldUseNewItem(nsDisplayItem* aNewItem) {
    // Generally we want to use the old item when the frame isn't marked as
    // modified so that any cached information on the item (or referencing the
    // item) gets retained. Quite a few FrameLayerBuilder performance
    // improvements benefit by this. Sometimes, however, we can end up where the
    // new item paints something different from the old item, even though we
    // haven't modified the frame, and it's hard to fix. In these cases we just
    // always use the new item to be safe.
    DisplayItemType type = aNewItem->GetType();
    if (type == DisplayItemType::TYPE_SOLID_COLOR) {
      // The canvas background color item can paint the color from another
      // frame, and even though we schedule a paint, we don't mark the canvas
      // frame as invalid.
      return true;
    }

    if (type == DisplayItemType::TYPE_TABLE_BORDER_COLLAPSE) {
      // We intentionally don't mark the root table frame as modified when a
      // subframe changes, even though the border collapse item for the root
      // frame is what paints the changed border. Marking the root frame as
      // modified would rebuild display items for the whole table area, and we
      // don't want that.
      return true;
    }

    if (type == DisplayItemType::TYPE_TEXT_OVERFLOW) {
      // Text overflow marker items are created with the wrapping block as their
      // frame, and have an index value to note which line they are created for.
      // Their rendering can change if the items on that line change, which may
      // not mark the block as modified. We rebuild them if we build any item on
      // the line, so we should always get new items if they might have changed
      // rendering, and it's easier to just use the new items rather than
      // computing if we actually need them.
      return true;
    }

    if (type == DisplayItemType::TYPE_SUBDOCUMENT ||
        type == DisplayItemType::TYPE_STICKY_POSITION) {
      // nsDisplaySubDocument::mShouldFlatten can change without an invalidation
      // (and is the reason we unconditionally build the subdocument item), so
      // always use the new one to make sure we get the right value.
      // Same for |nsDisplayStickyPosition::mShouldFlatten|.
      return true;
    }

    if (type == DisplayItemType::TYPE_CARET) {
      // The caret can change position while still being owned by the same frame
      // and we don't invalidate in that case. Use the new version since the
      // changed bounds are needed for DLBI.
      return true;
    }

    if (type == DisplayItemType::TYPE_MASK ||
        type == DisplayItemType::TYPE_FILTER ||
        type == DisplayItemType::TYPE_SVG_WRAPPER) {
      // SVG items have some invalidation issues, see bugs 1494110 and 1494663.
      return true;
    }

    if (type == DisplayItemType::TYPE_TRANSFORM) {
      // Prerendering of transforms can change without frame invalidation.
      return true;
    }

    return false;
  }

  RetainedDisplayList Finalize() {
    for (size_t i = 0; i < mOldDAG.Length(); i++) {
      if (mOldItems[i].IsUsed()) {
        continue;
      }

      AutoTArray<MergedListIndex, 2> directPredecessors =
          ResolveNodeIndexesOldToMerged(
              mOldDAG.GetDirectPredecessors(OldListIndex(i)));
      ProcessOldNode(OldListIndex(i), std::move(directPredecessors));
    }

    RetainedDisplayList result;
    result.AppendToTop(&mMergedItems);
    result.mDAG = std::move(mMergedDAG);
    MOZ_RELEASE_ASSERT(result.mDAG.Length() == result.Length());
    return result;
  }

  bool HasMatchingItemInOldList(nsDisplayItem* aItem, OldListIndex* aOutIndex) {
    // Look for an item that matches aItem's frame and per-frame-key, but isn't
    // the same item.
    uint32_t outerKey = mOuterItem ? mOuterItem->GetPerFrameKey() : 0;
    nsIFrame* frame = aItem->Frame();
    for (nsDisplayItem* i : frame->DisplayItems()) {
      if (i != aItem && i->Frame() == frame &&
          i->GetPerFrameKey() == aItem->GetPerFrameKey()) {
        if (i->GetOldListIndex(mOldList, outerKey, aOutIndex)) {
          return true;
        }
      }
    }
    return false;
  }

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  bool HasModifiedFrame(nsDisplayItem* aItem) {
    nsIFrame* stopFrame = mOuterItem ? mOuterItem->Frame() : nullptr;
    return AnyContentAncestorModified(aItem->FrameForInvalidation(), stopFrame);
  }
#endif

  void UpdateContainerASR(nsDisplayItem* aItem) {
    mContainerASR = SelectContainerASR(
        aItem->GetClipChain(), aItem->GetActiveScrolledRoot(), mContainerASR);
  }

  MergedListIndex AddNewNode(
      nsDisplayItem* aItem, const Maybe<OldListIndex>& aOldIndex,
      Span<const MergedListIndex> aDirectPredecessors,
      const Maybe<MergedListIndex>& aExtraDirectPredecessor) {
    if (aItem->GetType() != DisplayItemType::TYPE_VT_CAPTURE ||
        static_cast<nsDisplayViewTransitionCapture*>(aItem)->GetKind() ==
            nsDisplayViewTransitionCapture::Kind::Subdocument) {
      UpdateContainerASR(aItem);
    }
    aItem->NotifyUsed(mBuilder->Builder());

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    for (nsDisplayItem* i : aItem->Frame()->DisplayItems()) {
      if (i->Frame() == aItem->Frame() &&
          i->GetPerFrameKey() == aItem->GetPerFrameKey()) {
        MOZ_DIAGNOSTIC_ASSERT(!i->IsMergedItem());
      }
    }

    aItem->SetMergedPreProcessed(true, false);
#endif

    mMergedItems.AppendToTop(aItem);
    mBuilder->Metrics()->mTotalItems++;

    MergedListIndex newIndex =
        mMergedDAG.AddNode(aDirectPredecessors, aExtraDirectPredecessor);
    return newIndex;
  }

  void ProcessOldNode(OldListIndex aNode,
                      nsTArray<MergedListIndex>&& aDirectPredecessors) {
    nsDisplayItem* item = mOldItems[aNode.val].mItem;
    if (mOldItems[aNode.val].IsChanged()) {
      mOldItems[aNode.val].Discard(mBuilder, std::move(aDirectPredecessors));
      mResultIsModified = true;
    } else {
      MergeChildLists(nullptr, item, item);

      if (item->GetType() == DisplayItemType::TYPE_SUBDOCUMENT) {
        mBuilder->IncrementSubDocPresShellPaintCount(item);
      }
      mBuilder->Metrics()->mReusedItems++;
      mOldItems[aNode.val].AddedToMergedList(
          AddNewNode(item, Some(aNode), aDirectPredecessors, Nothing()));
    }
  }

  struct PredecessorStackItem {
    PredecessorStackItem(OldListIndex aNode, Span<OldListIndex> aPredecessors)
        : mNode(aNode),
          mDirectPredecessors(aPredecessors),
          mCurrentPredecessorIndex(0) {}

    bool IsFinished() {
      return mCurrentPredecessorIndex == mDirectPredecessors.Length();
    }

    OldListIndex GetAndIncrementCurrentPredecessor() {
      return mDirectPredecessors[mCurrentPredecessorIndex++];
    }

    OldListIndex mNode;
    Span<OldListIndex> mDirectPredecessors;
    size_t mCurrentPredecessorIndex;
  };

  AutoTArray<MergedListIndex, 2> ProcessPredecessorsOfOldNode(
      OldListIndex aNode) {
    AutoTArray<PredecessorStackItem, 256> mStack;
    mStack.AppendElement(
        PredecessorStackItem(aNode, mOldDAG.GetDirectPredecessors(aNode)));

    while (true) {
      if (mStack.LastElement().IsFinished()) {
        // If we've finished processing all the entries in the current set, then
        // pop it off the processing stack and process it.
        PredecessorStackItem item = mStack.PopLastElement();
        AutoTArray<MergedListIndex, 2> result =
            ResolveNodeIndexesOldToMerged(item.mDirectPredecessors);

        if (mStack.IsEmpty()) {
          return result;
        }

        ProcessOldNode(item.mNode, std::move(result));
      } else {
        // Grab the current predecessor, push predecessors of that onto the
        // processing stack (if it hasn't already been processed), and then
        // advance to the next entry.
        OldListIndex currentIndex =
            mStack.LastElement().GetAndIncrementCurrentPredecessor();
        if (!mOldItems[currentIndex.val].IsUsed()) {
          mStack.AppendElement(PredecessorStackItem(
              currentIndex, mOldDAG.GetDirectPredecessors(currentIndex)));
        }
      }
    }
  }

  AutoTArray<MergedListIndex, 2> ResolveNodeIndexesOldToMerged(
      Span<OldListIndex> aDirectPredecessors) {
    AutoTArray<MergedListIndex, 2> result;
    result.SetCapacity(aDirectPredecessors.Length());
    for (OldListIndex index : aDirectPredecessors) {
      OldItemInfo& oldItem = mOldItems[index.val];
      if (oldItem.IsDiscarded()) {
        for (MergedListIndex inner : oldItem.mDirectPredecessors) {
          if (!result.Contains(inner)) {
            result.AppendElement(inner);
          }
        }
      } else {
        result.AppendElement(oldItem.mIndex);
      }
    }
    return result;
  }

  RetainedDisplayListBuilder* mBuilder;
  RetainedDisplayList* mOldList;
  Maybe<const ActiveScrolledRoot*> mContainerASR;
  nsTArray<OldItemInfo> mOldItems;
  DirectedAcyclicGraph<OldListUnits> mOldDAG;
  // Unfortunately we can't use strong typing for the hashtables
  // since they internally encode the type with the mOps pointer,
  // and assert when we try swap the contents
  nsDisplayList mMergedItems;
  DirectedAcyclicGraph<MergedListUnits> mMergedDAG;
  nsDisplayItem* mOuterItem;
  bool mResultIsModified;
};

#ifdef DEBUG
void VerifyNotModified(nsDisplayList* aList) {
  for (nsDisplayItem* item : *aList) {
    MOZ_ASSERT(!AnyContentAncestorModified(item->FrameForInvalidation()));

    if (item->GetChildren()) {
      VerifyNotModified(item->GetChildren());
    }
  }
}
#endif

/**
 * Takes two display lists and merges them into an output list.
 *
 * Display lists wthout an explicit DAG are interpreted as linear DAGs (with a
 * maximum of one direct predecessor and one direct successor per node). We add
 * the two DAGs together, and then output the topological sorted ordering as the
 * final display list.
 *
 * Once we've merged a list, we then retain the DAG (as part of the
 * RetainedDisplayList object) to use for future merges.
 */
bool RetainedDisplayListBuilder::MergeDisplayLists(
    nsDisplayList* aNewList, RetainedDisplayList* aOldList,
    RetainedDisplayList* aOutList,
    mozilla::Maybe<const mozilla::ActiveScrolledRoot*>& aOutContainerASR,
    nsDisplayItem* aOuterItem) {
  AUTO_PROFILER_LABEL_CATEGORY_PAIR(GRAPHICS_DisplayListMerging);

  if (!aOldList->IsEmpty()) {
    // If we still have items in the actual list, then it is because
    // PreProcessDisplayList decided that it was sure it can't be modified. We
    // can just use it directly, and throw any new items away.

    aNewList->DeleteAll(&mBuilder);
#ifdef DEBUG
    VerifyNotModified(aOldList);
#endif

    if (aOldList != aOutList) {
      *aOutList = std::move(*aOldList);
    }

    return false;
  }

  MergeState merge(this, *aOldList, aOuterItem);

  Maybe<MergedListIndex> previousItemIndex;
  for (nsDisplayItem* item : aNewList->TakeItems()) {
    Metrics()->mNewItems++;
    previousItemIndex = merge.ProcessItemFromNewList(item, previousItemIndex);
  }

  *aOutList = merge.Finalize();
  aOutContainerASR = merge.mContainerASR;
  return merge.mResultIsModified;
}

void RetainedDisplayListBuilder::GetModifiedAndFramesWithProps(
    nsTArray<nsIFrame*>* aOutModifiedFrames,
    nsTArray<nsIFrame*>* aOutFramesWithProps) {
  for (auto it = Data()->ConstIterator(); !it.Done(); it.Next()) {
    nsIFrame* frame = it.Key();
    const RetainedDisplayListData::FrameFlags& flags = it.Data();

    if (flags.contains(RetainedDisplayListData::FrameFlag::Modified)) {
      aOutModifiedFrames->AppendElement(frame);
    }

    if (flags.contains(RetainedDisplayListData::FrameFlag::HasProps)) {
      aOutFramesWithProps->AppendElement(frame);
    }
  }

  Data()->Clear();
}

// ComputeRebuildRegion  debugging
// #define CRR_DEBUG 1
#if CRR_DEBUG
#  define CRR_LOG(...) printf_stderr(__VA_ARGS__)
#else
#  define CRR_LOG(...)
#endif

static nsDisplayItem* GetFirstDisplayItemWithChildren(nsIFrame* aFrame) {
  for (nsDisplayItem* i : aFrame->DisplayItems()) {
    if (i->HasDeletedFrame() || i->Frame() != aFrame) {
      // The main frame for the display item has been deleted or the display
      // item belongs to another frame.
      continue;
    }

    if (i->HasChildren()) {
      return static_cast<nsDisplayItem*>(i);
    }
  }
  return nullptr;
}

static bool IsInPreserve3DContext(const nsIFrame* aFrame) {
  return aFrame->Extend3DContext() ||
         aFrame->Combines3DTransformWithAncestors();
}

// Returns true if |aFrame| can store a display list building rect.
// These limitations are necessary to guarantee that
// 1) Just enough items are rebuilt to properly update display list
// 2) Modified frames will be visited during a partial display list build.
static bool CanStoreDisplayListBuildingRect(nsDisplayListBuilder* aBuilder,
                                            nsIFrame* aFrame) {
  return aFrame != aBuilder->RootReferenceFrame() &&
         aFrame->IsStackingContext() && aFrame->IsFixedPosContainingBlock() &&
         // Split frames might have placeholders for modified frames in their
         // unmodified continuation frame.
         !aFrame->GetPrevContinuation() && !aFrame->GetNextContinuation();
}

static bool ProcessFrameInternal(nsIFrame* aFrame,
                                 nsDisplayListBuilder* aBuilder,
                                 nsIFrame** aAGR, nsRect& aOverflow,
                                 const nsIFrame* aStopAtFrame,
                                 nsTArray<nsIFrame*>& aOutFramesWithProps,
                                 const bool aStopAtStackingContext) {
  nsIFrame* currentFrame = aFrame;

  while (currentFrame != aStopAtFrame) {
    CRR_LOG("currentFrame: %p (placeholder=%d), aOverflow: %d %d %d %d\n",
            currentFrame, !aStopAtStackingContext, aOverflow.x, aOverflow.y,
            aOverflow.width, aOverflow.height);

    // If the current frame is an OOF frame, DisplayListBuildingData needs to be
    // set on all the ancestor stacking contexts of the  placeholder frame, up
    // to the containing block of the OOF frame. This is done to ensure that the
    // content that might be behind the OOF frame is built for merging.
    nsIFrame* placeholder = currentFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)
                                ? currentFrame->GetPlaceholderFrame()
                                : nullptr;

    if (placeholder) {
      nsRect placeholderOverflow = aOverflow;
      auto rv = nsLayoutUtils::TransformRect(currentFrame, placeholder,
                                             placeholderOverflow);
      if (rv != nsLayoutUtils::TRANSFORM_SUCCEEDED) {
        placeholderOverflow = nsRect();
      }

      CRR_LOG("Processing placeholder %p for OOF frame %p\n", placeholder,
              currentFrame);

      CRR_LOG("OOF frame draw area: %d %d %d %d\n", placeholderOverflow.x,
              placeholderOverflow.y, placeholderOverflow.width,
              placeholderOverflow.height);

      // Tracking AGRs for the placeholder processing is not necessary, as the
      // goal is to only modify the DisplayListBuildingData rect.
      nsIFrame* dummyAGR = nullptr;

      // Find a common ancestor frame to handle frame continuations.
      // TODO: It might be possible to write a more specific and efficient
      // function for this.
      const nsIFrame* ancestor = nsLayoutUtils::FindNearestCommonAncestorFrame(
          currentFrame->GetParent(), placeholder->GetParent());

      if (!ProcessFrameInternal(placeholder, aBuilder, &dummyAGR,
                                placeholderOverflow, ancestor,
                                aOutFramesWithProps, false)) {
        return false;
      }
    }

    // Convert 'aOverflow' into the coordinate space of the nearest stacking
    // context or display port ancestor and update 'currentFrame' to point to
    // that frame.
    aOverflow = nsLayoutUtils::TransformFrameRectToAncestor(
        currentFrame, aOverflow, aStopAtFrame, nullptr, nullptr,
        TransformMatrixFlag::StopAtStackingContextAndDisplayPort,
        &currentFrame);
    if (IsInPreserve3DContext(currentFrame)) {
      return false;
    }

    MOZ_ASSERT(currentFrame);

    // Check whether the current frame is a scrollable frame with display port.
    nsRect displayPort;
    ScrollContainerFrame* sf = do_QueryFrame(currentFrame);
    nsIContent* content = sf ? currentFrame->GetContent() : nullptr;

    if (content && DisplayPortUtils::GetDisplayPort(content, &displayPort)) {
      CRR_LOG("Frame belongs to displayport frame %p\n", currentFrame);

      // Get overflow relative to the scrollport (from the scrollframe)
      nsRect r = aOverflow - sf->GetScrollPortRect().TopLeft();
      r.IntersectRect(r, displayPort);
      if (!r.IsEmpty()) {
        nsRect* rect = currentFrame->GetProperty(
            nsDisplayListBuilder::DisplayListBuildingDisplayPortRect());
        if (!rect) {
          rect = new nsRect();
          currentFrame->SetProperty(
              nsDisplayListBuilder::DisplayListBuildingDisplayPortRect(), rect);
          currentFrame->SetHasOverrideDirtyRegion(true);
          aOutFramesWithProps.AppendElement(currentFrame);
        }
        rect->UnionRect(*rect, r);
        CRR_LOG("Adding area to displayport draw area: %d %d %d %d\n", r.x, r.y,
                r.width, r.height);

        // TODO: Can we just use MarkFrameForDisplayIfVisible, plus
        // MarkFramesForDifferentAGR to ensure that this displayport, plus any
        // items that move relative to it get rebuilt, and then not contribute
        // to the root dirty area?
        aOverflow = sf->GetScrollPortRect();
      } else {
        // Don't contribute to the root dirty area at all.
        aOverflow.SetEmpty();
      }
    } else {
      aOverflow.IntersectRect(aOverflow,
                              currentFrame->InkOverflowRectRelativeToSelf());
    }

    if (aOverflow.IsEmpty()) {
      break;
    }

    if (CanStoreDisplayListBuildingRect(aBuilder, currentFrame)) {
      CRR_LOG("Frame belongs to stacking context frame %p\n", currentFrame);
      // Store the stacking context relative dirty area such
      // that display list building will pick it up when it
      // gets to it. This is needed even if we don't stop here, since display
      // list building treats a missing rect on such a frame as meaning that
      // nothing in its subtree needs to be rebuilt.
      nsDisplayListBuilder::DisplayListBuildingData* data =
          currentFrame->GetProperty(
              nsDisplayListBuilder::DisplayListBuildingRect());
      if (!data) {
        data = new nsDisplayListBuilder::DisplayListBuildingData();
        currentFrame->SetProperty(
            nsDisplayListBuilder::DisplayListBuildingRect(), data);
        currentFrame->SetHasOverrideDirtyRegion(true);
        aOutFramesWithProps.AppendElement(currentFrame);
      }
      CRR_LOG("Adding area to stacking context draw area: %d %d %d %d\n",
              aOverflow.x, aOverflow.y, aOverflow.width, aOverflow.height);
      data->mDirtyRect.UnionRect(data->mDirtyRect, aOverflow);

      if (!aStopAtStackingContext) {
        // Continue ascending the frame tree until we reach aStopAtFrame.
        continue;
      }

      // If we found an intermediate stacking context with an existing display
      // item then we can stop here. If we couldn't find one then we need to
      // keep bubbling up to the next stacking context.
      nsDisplayItem* wrapperItem =
          GetFirstDisplayItemWithChildren(currentFrame);
      if (!wrapperItem) {
        continue;
      }

      // Grab the visible (display list building) rect for children of this
      // wrapper item and convert into into coordinate relative to the current
      // frame.
      nsRect previousVisible = wrapperItem->GetBuildingRectForChildren();
      if (wrapperItem->ReferenceFrameForChildren() != wrapperItem->Frame()) {
        previousVisible -= wrapperItem->ToReferenceFrame();
      }

      if (!previousVisible.Contains(aOverflow)) {
        // If the overflow area of the changed frame isn't contained within the
        // old item, then we might change the size of the item and need to
        // update its sorting accordingly. Keep propagating the overflow area up
        // so that we build intersecting items for sorting.
        continue;
      }

      if (!data->mModifiedAGR) {
        data->mModifiedAGR = *aAGR;
      } else if (data->mModifiedAGR != *aAGR) {
        data->mDirtyRect = currentFrame->InkOverflowRectRelativeToSelf();
        CRR_LOG(
            "Found multiple modified AGRs within this stacking context, "
            "giving up\n");
      }

      // Don't contribute to the root dirty area at all.
      aOverflow.SetEmpty();
      *aAGR = nullptr;

      break;
    }
  }
  return true;
}

bool RetainedDisplayListBuilder::ProcessFrame(
    nsIFrame* aFrame, nsDisplayListBuilder* aBuilder, nsIFrame* aStopAtFrame,
    nsTArray<nsIFrame*>& aOutFramesWithProps, const bool aStopAtStackingContext,
    nsRect* aOutDirty, nsIFrame** aOutModifiedAGR) {
  if (aFrame->HasOverrideDirtyRegion()) {
    aOutFramesWithProps.AppendElement(aFrame);
  }

  if (aFrame->HasAnyStateBits(NS_FRAME_IN_POPUP)) {
    return true;
  }

  // TODO: There is almost certainly a faster way of doing this, probably can be
  // combined with the ancestor walk for TransformFrameRectToAncestor.
  nsIFrame* agrFrame = aBuilder->FindAnimatedGeometryRootFrameFor(aFrame);

  CRR_LOG("Processing frame %p with agr %p\n", aFrame, agr->mFrame);

  // Convert the frame's overflow rect into the coordinate space
  // of the nearest stacking context that has an existing display item.
  // We store that as a dirty rect on that stacking context so that we build
  // all items that intersect the changed frame within the stacking context,
  // and then we use MarkFrameForDisplayIfVisible to make sure the stacking
  // context itself gets built. We don't need to build items that intersect
  // outside of the stacking context, since we know the stacking context item
  // exists in the old list, so we can trivially merge without needing other
  // items.
  nsRect overflow = aFrame->InkOverflowRectRelativeToSelf();

  // If the modified frame is also a caret frame, include the caret area.
  // This is needed because some frames (for example text frames without text)
  // might have an empty overflow rect.
  if (aFrame == aBuilder->GetCaretFrame()) {
    overflow.UnionRect(overflow, aBuilder->GetCaretRect());
  }

  if (!ProcessFrameInternal(aFrame, aBuilder, &agrFrame, overflow, aStopAtFrame,
                            aOutFramesWithProps, aStopAtStackingContext)) {
    return false;
  }

  if (!overflow.IsEmpty()) {
    aOutDirty->UnionRect(*aOutDirty, overflow);
    CRR_LOG("Adding area to root draw area: %d %d %d %d\n", overflow.x,
            overflow.y, overflow.width, overflow.height);

    // If we get changed frames from multiple AGRS, then just give up as it gets
    // really complex to track which items would need to be marked in
    // MarkFramesForDifferentAGR.
    if (!*aOutModifiedAGR) {
      CRR_LOG("Setting %p as root stacking context AGR\n", agrFrame);
      *aOutModifiedAGR = agrFrame;
    } else if (agrFrame && *aOutModifiedAGR != agrFrame) {
      CRR_LOG("Found multiple AGRs in root stacking context, giving up\n");
      return false;
    }
  }
  return true;
}

static void AddFramesForContainingBlock(nsIFrame* aBlock,
                                        const nsFrameList& aFrames,
                                        nsTArray<nsIFrame*>& aExtraFrames) {
  for (nsIFrame* f : aFrames) {
    if (!f->IsFrameModified() && AnyContentAncestorModified(f, aBlock)) {
      CRR_LOG("Adding invalid OOF %p\n", f);
      aExtraFrames.AppendElement(f);
    }
  }
}

// Placeholder descendants of aFrame don't contribute to aFrame's overflow area.
// Find all the containing blocks that might own placeholders under us, walk
// their OOF frames list, and manually invalidate any frames that are
// descendants of a modified frame (us, or another frame we'll get to soon).
// This is combined with the work required for MarkFrameForDisplayIfVisible,
// so that we can avoid an extra ancestor walk, and we can reuse the flag
// to detect when we've already visited an ancestor (and thus all further
// ancestors must also be visited).
static void FindContainingBlocks(nsIFrame* aFrame,
                                 nsTArray<nsIFrame*>& aExtraFrames) {
  for (nsIFrame* f = aFrame; f; f = nsLayoutUtils::GetDisplayListParent(f)) {
    if (f->ForceDescendIntoIfVisible()) {
      return;
    }
    f->SetForceDescendIntoIfVisible(true);
    CRR_LOG("Considering OOFs for %p\n", f);

    AddFramesForContainingBlock(f, f->GetChildList(FrameChildListID::Float),
                                aExtraFrames);
    AddFramesForContainingBlock(f, f->GetChildList(FrameChildListID::Absolute),
                                aExtraFrames);

    // This condition must match the condition in
    // nsLayoutUtils::GetParentOrPlaceholderFor which is used by
    // nsLayoutUtils::GetDisplayListParent
    if (f->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) && !f->GetPrevInFlow()) {
      nsIFrame* parent = f->GetParent();
      if (parent && !parent->ForceDescendIntoIfVisible()) {
        // If the GetDisplayListParent call is going to walk to a placeholder,
        // in rare cases the placeholder might be contained in a different
        // continuation from the oof. So we have to make sure to mark the oofs
        // parent. In the common case this doesn't make us do any extra work,
        // just changes the order in which we visit the frames since walking
        // through placeholders will walk through the parent, and we stop when
        // we find a ForceDescendIntoIfVisible bit set.
        FindContainingBlocks(parent, aExtraFrames);
      }
    }
  }
}

/**
 * Given a list of frames that has been modified, computes the region that we
 * need to do display list building for in order to build all modified display
 * items.
 *
 * When a modified frame is within a stacking context (with an existing display
 * item), then we only contribute to the build area within the stacking context,
 * as well as forcing display list building to descend to the stacking context.
 * We don't need to add build area outside of the stacking context (and force
 * items above/below the stacking context container item to be built), since
 * just matching the position of the stacking context container item is
 * sufficient to ensure correct ordering during merging.
 *
 * We need to rebuild all items that might intersect with the modified frame,
 * both now and during async changes on the compositor. We do this by rebuilding
 * the area covered by the changed frame, as well as rebuilding all items that
 * have a different (async) AGR to the changed frame. If we have changes to
 * multiple AGRs (within a stacking context), then we rebuild that stacking
 * context entirely.
 *
 * @param aModifiedFrames The list of modified frames.
 * @param aOutDirty The result region to use for display list building.
 * @param aOutModifiedAGR The modified AGR for the root stacking context.
 * @param aOutFramesWithProps The list of frames to which we attached partial
 * build data so that it can be cleaned up.
 *
 * @return true if we succesfully computed a partial rebuild region, false if a
 * full build is required.
 */
bool RetainedDisplayListBuilder::ComputeRebuildRegion(
    nsTArray<nsIFrame*>& aModifiedFrames, nsRect* aOutDirty,
    nsIFrame** aOutModifiedAGR, nsTArray<nsIFrame*>& aOutFramesWithProps) {
  CRR_LOG("Computing rebuild regions for %zu frames:\n",
          aModifiedFrames.Length());
  nsTArray<nsIFrame*> extraFrames;
  for (nsIFrame* f : aModifiedFrames) {
    MOZ_ASSERT(f);

    mBuilder.AddFrameMarkedForDisplayIfVisible(f);
    FindContainingBlocks(f, extraFrames);

    if (!ProcessFrame(f, &mBuilder, RootReferenceFrame(), aOutFramesWithProps,
                      true, aOutDirty, aOutModifiedAGR)) {
      return false;
    }
  }

  // Since we set modified to true on the extraFrames, add them to
  // aModifiedFrames so that it will get reverted.
  aModifiedFrames.AppendElements(extraFrames);

  for (nsIFrame* f : extraFrames) {
    f->SetFrameIsModified(true);

    if (!ProcessFrame(f, &mBuilder, RootReferenceFrame(), aOutFramesWithProps,
                      true, aOutDirty, aOutModifiedAGR)) {
      return false;
    }
  }

  return true;
}

bool RetainedDisplayListBuilder::ShouldBuildPartial(
    nsTArray<nsIFrame*>& aModifiedFrames) {
  // We don't support retaining with overlay scrollbars, since they require
  // us to look at the display list and pick the highest z-index, which
  // we can't do during partial building.
  if (mBuilder.DisablePartialUpdates()) {
    mBuilder.SetDisablePartialUpdates(false);
    Metrics()->mPartialUpdateFailReason = PartialUpdateFailReason::Disabled;
    return false;
  }

  if (mList.IsEmpty()) {
    // Partial builds without a previous display list do not make sense.
    Metrics()->mPartialUpdateFailReason = PartialUpdateFailReason::EmptyList;
    return false;
  }

  if (aModifiedFrames.Length() >
      StaticPrefs::layout_display_list_rebuild_frame_limit()) {
    // Computing a dirty rect with too many modified frames can be slow.
    Metrics()->mPartialUpdateFailReason = PartialUpdateFailReason::RebuildLimit;
    return false;
  }

  for (nsIFrame* f : aModifiedFrames) {
    MOZ_ASSERT(f);

    const LayoutFrameType type = f->Type();

    // If we have any modified frames of the following types, it is likely that
    // doing a partial rebuild of the display list will be slower than doing a
    // full rebuild.
    // This is because these frames either intersect or may intersect with most
    // of the page content. This is either due to display port size or different
    // async AGR.
    if (type == LayoutFrameType::Viewport ||
        type == LayoutFrameType::PageContent ||
        type == LayoutFrameType::Canvas || type == LayoutFrameType::Scrollbar) {
      Metrics()->mPartialUpdateFailReason = PartialUpdateFailReason::FrameType;
      return false;
    }

    // Detect root scroll frame and do a full rebuild for them too for the same
    // reasons as above, but also because top layer items should to be marked
    // modified if the root scroll frame is modified. Putting this check here
    // means we don't need to check everytime a frame is marked modified though.
    if (type == LayoutFrameType::ScrollContainer && f->GetParent() &&
        !f->GetParent()->GetParent()) {
      Metrics()->mPartialUpdateFailReason = PartialUpdateFailReason::FrameType;
      return false;
    }
  }

  return true;
}

class AutoClearFramePropsArray {
 public:
  explicit AutoClearFramePropsArray(size_t aCapacity) : mFrames(aCapacity) {}
  AutoClearFramePropsArray() = default;
  ~AutoClearFramePropsArray() {
    size_t len = mFrames.Length();
    nsIFrame** elements = mFrames.Elements();
    for (size_t i = 0; i < len; ++i) {
      nsIFrame* f = elements[i];
      DL_LOGV("RDL - Clearing modified flags for frame %p", f);
      if (f->HasOverrideDirtyRegion()) {
        f->SetHasOverrideDirtyRegion(false);
        f->RemoveProperty(nsDisplayListBuilder::DisplayListBuildingRect());
        f->RemoveProperty(
            nsDisplayListBuilder::DisplayListBuildingDisplayPortRect());
      }
      f->SetFrameIsModified(false);
      f->SetHasModifiedDescendants(false);
    }
  }

  nsTArray<nsIFrame*>& Frames() { return mFrames; }
  bool IsEmpty() const { return mFrames.IsEmpty(); }

 private:
  nsTArray<nsIFrame*> mFrames;
};

void RetainedDisplayListBuilder::ClearFramesWithProps() {
  AutoClearFramePropsArray modifiedFrames(Data()->GetModifiedFrameCount());
  AutoClearFramePropsArray framesWithProps;
  GetModifiedAndFramesWithProps(&modifiedFrames.Frames(),
                                &framesWithProps.Frames());
}

void RetainedDisplayListBuilder::ClearRetainedData() {
  DL_LOGI("(%p) RDL - Clearing retained display list builder data", this);
  List()->DeleteAll(Builder());
  ClearFramesWithProps();
  ClearReuseableDisplayItems();
}

namespace RDLUtils {

MOZ_NEVER_INLINE_DEBUG void AssertFrameSubtreeUnmodified(
    const nsIFrame* aFrame) {
  MOZ_ASSERT(!aFrame->IsFrameModified());
  MOZ_ASSERT(!aFrame->HasModifiedDescendants());

  for (const auto& childList : aFrame->ChildLists()) {
    for (nsIFrame* child : childList.mList) {
      AssertFrameSubtreeUnmodified(child);
    }
  }
}

MOZ_NEVER_INLINE_DEBUG void AssertDisplayListUnmodified(nsDisplayList* aList) {
  for (nsDisplayItem* item : *aList) {
    AssertDisplayItemUnmodified(item);
  }
}

MOZ_NEVER_INLINE_DEBUG void AssertDisplayItemUnmodified(nsDisplayItem* aItem) {
  MOZ_ASSERT(!aItem->HasDeletedFrame());
  MOZ_ASSERT(!AnyContentAncestorModified(aItem->FrameForInvalidation()));

  if (aItem->GetChildren()) {
    AssertDisplayListUnmodified(aItem->GetChildren());
  }
}

}  // namespace RDLUtils

namespace RDL {

void MarkAncestorFrames(nsIFrame* aFrame,
                        nsTArray<nsIFrame*>& aOutFramesWithProps) {
  nsIFrame* frame = nsLayoutUtils::GetDisplayListParent(aFrame);
  while (frame && !frame->HasModifiedDescendants()) {
    aOutFramesWithProps.AppendElement(frame);
    frame->SetHasModifiedDescendants(true);
    frame = nsLayoutUtils::GetDisplayListParent(frame);
  }
}

/**
 * Iterates over the modified frames array and updates the frame tree flags
 * so that container frames know whether they have modified descendant frames.
 * Frames that were marked modified are added to |aOutFramesWithProps|, so that
 * the modified status can be cleared after the display list build.
 */
void MarkAllAncestorFrames(const nsTArray<nsIFrame*>& aModifiedFrames,
                           nsTArray<nsIFrame*>& aOutFramesWithProps) {
  nsAutoString frameName;
  DL_LOGI("RDL - Modified frames: %zu", aModifiedFrames.Length());
  for (nsIFrame* frame : aModifiedFrames) {
#ifdef DEBUG
    frame->GetFrameName(frameName);
#endif
    DL_LOGV("RDL - Processing modified frame: %p (%s)", frame,
            NS_ConvertUTF16toUTF8(frameName).get());

    MarkAncestorFrames(frame, aOutFramesWithProps);
  }
}

/**
 * Marks the given display item |aItem| as reuseable container, and updates the
 * bounds in case some child items were destroyed.
 */
MOZ_NEVER_INLINE_DEBUG void ReuseStackingContextItem(
    nsDisplayListBuilder* aBuilder, nsDisplayItem* aItem) {
  aItem->SetPreProcessed();

  if (aItem->HasChildren()) {
    aItem->UpdateBounds(aBuilder);
  }

  aBuilder->AddReusableDisplayItem(aItem);
  DL_LOGD("Reusing display item %p", aItem);
}

bool IsSupportedFrameType(const nsIFrame* aFrame) {
  // The way table backgrounds are handled makes these frames incompatible with
  // this retained display list approach.
  if (aFrame->IsTableColFrame()) {
    return false;
  }

  if (aFrame->IsTableColGroupFrame()) {
    return false;
  }

  if (aFrame->IsTableRowFrame()) {
    return false;
  }

  if (aFrame->IsTableRowGroupFrame()) {
    return false;
  }

  if (aFrame->IsTableCellFrame()) {
    return false;
  }

  // Everything else should work.
  return true;
}

bool IsReuseableStackingContextItem(nsDisplayItem* aItem) {
  if (!IsSupportedFrameType(aItem->Frame())) {
    return false;
  }

  if (!aItem->IsReusable()) {
    return false;
  }

  const nsIFrame* frame = aItem->FrameForInvalidation();
  return !frame->HasModifiedDescendants() && !frame->GetPrevContinuation() &&
         !frame->GetNextContinuation();
}

/**
 * Recursively visits every display item of the display list and destroys all
 * display items that depend on deleted or modified frames.
 * The stacking context display items for unmodified frame subtrees are kept
 * linked and collected in given |aOutItems| array.
 */
void CollectStackingContextItems(nsDisplayListBuilder* aBuilder,
                                 nsDisplayList* aList, nsIFrame* aOuterFrame,
                                 int aDepth = 0, bool aParentReused = false) {
  for (nsDisplayItem* item : aList->TakeItems()) {
    if (DL_LOG_TEST(LogLevel::Debug)) {
      DL_LOGD(
          "%*s Preprocessing item %p (%s) (frame: %p) "
          "(children: %zu) (depth: %d) (parentReused: %d)",
          aDepth, "", item, item->Name(),
          item->HasDeletedFrame() ? nullptr : item->Frame(),
          item->GetChildren() ? item->GetChildren()->Length() : 0, aDepth,
          aParentReused);
    }

    if (!item->CanBeReused() || item->HasDeletedFrame() ||
        AnyContentAncestorModified(item->FrameForInvalidation(), aOuterFrame)) {
      DL_LOGD("%*s Deleted modified or temporary item %p", aDepth, "", item);
      item->Destroy(aBuilder);
      continue;
    }

    MOZ_ASSERT(!AnyContentAncestorModified(item->FrameForInvalidation()));
    MOZ_ASSERT(!item->IsPreProcessed());
    item->InvalidateCachedChildInfo(aBuilder);
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    item->SetMergedPreProcessed(false, true);
#endif
    const bool isStackingContextItem = IsReuseableStackingContextItem(item);

    if (item->GetChildren()) {
      CollectStackingContextItems(aBuilder, item->GetChildren(), item->Frame(),
                                  aDepth + 1,
                                  aParentReused || isStackingContextItem);
    }

    if (aParentReused) {
      // Keep the contents of the current container item linked.
#ifdef DEBUG
      RDLUtils::AssertDisplayItemUnmodified(item);
#endif
      aList->AppendToTop(item);
    } else if (isStackingContextItem) {
      // |item| is a stacking context item that can be reused.
      ReuseStackingContextItem(aBuilder, item);
    } else {
      // |item| is inside a container item that will be destroyed later.
      DL_LOGD("%*s Deleted unused item %p", aDepth, "", item);
      item->Destroy(aBuilder);
      continue;
    }

    if (item->GetType() == DisplayItemType::TYPE_SUBDOCUMENT) {
      IncrementPresShellPaintCount(aBuilder, item);
    }
  }
}

}  // namespace RDL

bool RetainedDisplayListBuilder::TrySimpleUpdate(
    const nsTArray<nsIFrame*>& aModifiedFrames,
    nsTArray<nsIFrame*>& aOutFramesWithProps) {
  if (!mBuilder.IsReusingStackingContextItems()) {
    return false;
  }

  RDL::MarkAllAncestorFrames(aModifiedFrames, aOutFramesWithProps);
  RDL::CollectStackingContextItems(&mBuilder, &mList, RootReferenceFrame());

  return true;
}

PartialUpdateResult RetainedDisplayListBuilder::AttemptPartialUpdate(
    nscolor aBackstop) {
  DL_LOGI("(%p) RDL - AttemptPartialUpdate, root frame: %p", this,
          RootReferenceFrame());

  mBuilder.RemoveModifiedWindowRegions();

  if (mBuilder.ShouldSyncDecodeImages()) {
    DL_LOGI("RDL - Sync decoding images");
    MarkFramesWithItemsAndImagesModified(&mList);
  }

  mBuilder.InvalidateCaretFramesIfNeeded();

  // We set the override dirty regions during ComputeRebuildRegion or in
  // DisplayPortUtils::InvalidateForDisplayPortChange. The display port change
  // also marks the frame modified, so those regions are cleared here as well.
  AutoClearFramePropsArray modifiedFrames(Data()->GetModifiedFrameCount());
  AutoClearFramePropsArray framesWithProps(64);
  GetModifiedAndFramesWithProps(&modifiedFrames.Frames(),
                                &framesWithProps.Frames());

  if (!ShouldBuildPartial(modifiedFrames.Frames())) {
    // Do not allow partial builds if the |ShouldBuildPartial()| heuristic
    // fails.
    mBuilder.SetPartialBuildFailed(true);
    return PartialUpdateResult::Failed;
  }

  nsRect modifiedDirty;
  nsDisplayList modifiedDL;
  nsIFrame* modifiedAGR = nullptr;
  PartialUpdateResult result = PartialUpdateResult::NoChange;
  const bool simpleUpdate =
      TrySimpleUpdate(modifiedFrames.Frames(), framesWithProps.Frames());

  mBuilder.EnterPresShell(RootReferenceFrame());

  if (!simpleUpdate) {
    if (!ComputeRebuildRegion(modifiedFrames.Frames(), &modifiedDirty,
                              &modifiedAGR, framesWithProps.Frames()) ||
        !PreProcessDisplayList(&mList, modifiedAGR, result,
                               RootReferenceFrame(), nullptr)) {
      DL_LOGI("RDL - Partial update aborted");
      mBuilder.SetPartialBuildFailed(true);
      mBuilder.LeavePresShell(RootReferenceFrame(), nullptr);
      mList.DeleteAll(&mBuilder);
      return PartialUpdateResult::Failed;
    }
  } else {
    modifiedDirty = mBuilder.GetVisibleRect();
  }

  // This is normally handled by EnterPresShell, but we skipped it so that we
  // didn't call MarkFrameForDisplayIfVisible before ComputeRebuildRegion.
  ScrollContainerFrame* sf =
      RootReferenceFrame()->PresShell()->GetRootScrollContainerFrame();
  if (sf) {
    nsCanvasFrame* canvasFrame = do_QueryFrame(sf->GetScrolledFrame());
    if (canvasFrame) {
      mBuilder.MarkFrameForDisplayIfVisible(canvasFrame, RootReferenceFrame());
    }
  }

  nsRect rootOverflow = RootOverflowRect();
  modifiedDirty.IntersectRect(modifiedDirty, rootOverflow);

  mBuilder.SetDirtyRect(modifiedDirty);
  mBuilder.SetPartialUpdate(true);
  mBuilder.SetPartialBuildFailed(false);

  DL_LOGI("RDL - Starting display list build");
  RootReferenceFrame()->BuildDisplayListForStackingContext(&mBuilder,
                                                           &modifiedDL);
  DL_LOGI("RDL - Finished display list build");

  if (!modifiedDL.IsEmpty()) {
    nsLayoutUtils::AddExtraBackgroundItems(
        &mBuilder, &modifiedDL, RootReferenceFrame(),
        nsRect(nsPoint(0, 0), rootOverflow.Size()), rootOverflow, aBackstop);
  }
  mBuilder.SetPartialUpdate(false);

  if (mBuilder.PartialBuildFailed()) {
    DL_LOGI("RDL - Partial update failed!");
    mBuilder.LeavePresShell(RootReferenceFrame(), nullptr);
    mBuilder.ClearReuseableDisplayItems();
    mList.DeleteAll(&mBuilder);
    modifiedDL.DeleteAll(&mBuilder);
    Metrics()->mPartialUpdateFailReason = PartialUpdateFailReason::Content;
    return PartialUpdateResult::Failed;
  }

  // printf_stderr("Painting --- Modified list (dirty %d,%d,%d,%d):\n",
  //              modifiedDirty.x, modifiedDirty.y, modifiedDirty.width,
  //              modifiedDirty.height);
  // nsIFrame::PrintDisplayList(&mBuilder, modifiedDL);

  // |modifiedDL| can sometimes be empty here. We still perform the
  // display list merging to prune unused items (for example, items that
  // are not visible anymore) from the old list.
  // TODO: Optimization opportunity. In this case, MergeDisplayLists()
  // unnecessarily creates a hashtable of the old items.
  // TODO: Ideally we could skip this if result is NoChange, but currently when
  // we call RestoreState on nsDisplayWrapList it resets the clip to the base
  // clip, and we need the UpdateBounds call (within MergeDisplayLists) to
  // move it to the correct inner clip.
  if (!simpleUpdate) {
    Maybe<const ActiveScrolledRoot*> dummy;
    if (MergeDisplayLists(&modifiedDL, &mList, &mList, dummy)) {
      result = PartialUpdateResult::Updated;
    }
  } else {
    MOZ_ASSERT(mList.IsEmpty());
    mList = std::move(modifiedDL);
    mBuilder.ClearReuseableDisplayItems();
    result = PartialUpdateResult::Updated;
  }

#if 0
  if (DL_LOG_TEST(LogLevel::Verbose)) {
    printf_stderr("Painting --- Display list:\n");
    nsIFrame::PrintDisplayList(&mBuilder, mList);
  }
#endif

  mBuilder.LeavePresShell(RootReferenceFrame(), List());
  return result;
}

nsRect RetainedDisplayListBuilder::RootOverflowRect() const {
  const nsIFrame* rootReferenceFrame = RootReferenceFrame();
  nsRect rootOverflowRect = rootReferenceFrame->InkOverflowRectRelativeToSelf();
  const nsPresContext* presContext = rootReferenceFrame->PresContext();
  if (!rootReferenceFrame->GetParent() &&
      presContext->IsRootContentDocumentCrossProcess() &&
      presContext->HasDynamicToolbar()) {
    rootOverflowRect.SizeTo(nsLayoutUtils::ExpandHeightForDynamicToolbar(
        presContext, rootOverflowRect.Size()));
  }

  return rootOverflowRect;
}

}  // namespace mozilla
