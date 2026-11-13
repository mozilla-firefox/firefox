/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_OverflowChangedTracker_h
#define mozilla_OverflowChangedTracker_h

#include "mozilla/HashTable.h"
#include "nsContainerFrame.h"
#include "nsIFrame.h"
#include "nsTArray.h"

namespace mozilla {

/**
 * Helper class that collects a list of frames that need
 * UpdateOverflow() called on them, and coalesces them
 * to avoid walking up the same ancestor tree multiple times.
 */
class OverflowChangedTracker {
 public:
  enum ChangeKind {
    /**
     * The frame was explicitly added as a result of
     * nsChangeHint_UpdatePostTransformOverflow and hence may have had a style
     * change that changes its geometry relative to parent, without reflowing.
     */
    TRANSFORM_CHANGED,
    /**
     * The overflow areas of children have changed
     * and we need to call UpdateOverflow on the frame.
     */
    CHILDREN_CHANGED,
  };

  OverflowChangedTracker() : mSubtreeRoot(nullptr) {}

  ~OverflowChangedTracker() {
    MOZ_ASSERT(mEntries.IsEmpty(), "Need to flush before destroying!");
  }

  /**
   * Add a frame that has had a style change, and needs its
   * overflow updated.
   *
   * If there are pre-transform overflow areas stored for this
   * frame, then we will call FinishAndStoreOverflow with those
   * areas instead of UpdateOverflow().
   *
   * If the overflow area changes, then UpdateOverflow will also
   * be called on the parent.
   */
  void AddFrame(nsIFrame* aFrame, ChangeKind aChangeKind) {
    MOZ_ASSERT(
        aFrame->FrameMaintainsOverflow(),
        "Why add a frame that doesn't maintain overflow to the tracker?");
    uint32_t depth = aFrame->GetDepthInFrameTree();
    // We use fallible allocation to avoid crashing on OOM; in the event that
    // of allocation failure, we'll effectively ignore the entries that weren't
    // added to the tracker, which could result in painting glitches but should
    // be otherwise harmless.
    if (NS_WARN_IF(!mEntries.EnsureLengthAtLeast(depth + 1, fallible))) {
      return;  // Failed to extend array! Just ignore this frame.
    }
    auto* entriesForDepth = mEntries[depth].get();
    if (!entriesForDepth) {
      mEntries[depth] = std::make_unique<FrameChangedMap>();
      entriesForDepth = mEntries[depth].get();
    }
    if (auto p = entriesForDepth->lookupForAdd(aFrame)) {
      p->value() = std::max(p->value(), aChangeKind);
    } else {
      // We also ignore OOM-failure here, just don't track this frame.
      (void)NS_WARN_IF(!entriesForDepth->add(p, aFrame, aChangeKind));
    }
  }

  /**
   * Remove a frame.
   */
  void RemoveFrame(nsIFrame* aFrame) {
    if (mEntries.IsEmpty()) {
      return;
    }
    uint32_t depth = aFrame->GetDepthInFrameTree();
    if (depth >= mEntries.Length()) {
      return;
    }
    auto* entriesForDepth = mEntries[depth].get();
    if (!entriesForDepth || entriesForDepth->empty()) {
      return;
    }
    entriesForDepth->remove(aFrame);
  }

  /**
   * Set the subtree root to limit overflow updates. This must be set if and
   * only if currently reflowing aSubtreeRoot, to ensure overflow changes will
   * still propagate correctly.
   */
  void SetSubtreeRoot(const nsIFrame* aSubtreeRoot) {
    mSubtreeRoot = aSubtreeRoot;
  }

  /**
   * Update the overflow of all added frames, and clear the entry list.
   *
   * Start from those deepest in the frame tree and works upwards. This stops
   * us from processing the same frame twice.
   */
  void Flush() {
    while (!mEntries.IsEmpty()) {
      // This takes ownership of the UniquePtr to the deepestEntries hashtable,
      // so it will be deleted at the end of the loop iteration.
      UniquePtr<FrameChangedMap> deepestEntries = mEntries.PopLastElement();
      if (!deepestEntries || deepestEntries->empty()) {
        continue;
      }
      for (auto iter = deepestEntries->iter(); !iter.done(); iter.next()) {
        nsIFrame* frame = iter.get().key();
        ChangeKind kind = iter.get().value();
        bool overflowChanged = false;
        if (kind == CHILDREN_CHANGED) {
          // Need to union the overflow areas of the children.
          // Only update the parent if the overflow changes.
          overflowChanged = frame->UpdateOverflow();
        } else {
          // Take a faster path that doesn't require unioning the overflow areas
          // of our children.
          NS_ASSERTION(frame->GetProperty(
                           nsIFrame::DebugInitialOverflowPropertyApplied()),
                       "InitialOverflowProperty must be set first.");

          OverflowAreas* overflow =
              frame->GetProperty(nsIFrame::InitialOverflowProperty());
          if (overflow) {
            // FinishAndStoreOverflow will change the overflow areas passed in,
            // so make a copy.
            OverflowAreas overflowCopy = *overflow;
            frame->FinishAndStoreOverflow(overflowCopy, frame->GetSize());
          } else {
            nsRect bounds(nsPoint(0, 0), frame->GetSize());
            OverflowAreas boundsOverflow;
            boundsOverflow.SetAllTo(bounds);
            frame->FinishAndStoreOverflow(boundsOverflow, bounds.Size());
          }

          // We can't tell if the overflow changed, so be conservative
          overflowChanged = true;
        }

        // If the frame style changed (e.g. positioning offsets)
        // then we need to update the parent with the overflow areas of its
        // children.
        // The hashmap for the parent's depth will be mEntries.LastElement(),
        // as we already popped the map for the current depth off the end.
        if (overflowChanged) {
          nsIFrame* parent = frame->GetParent();

          // It's possible that the parent is already in a nondisplay context,
          // should not add it to the list if that's true.
          if (parent && parent != mSubtreeRoot &&
              parent->FrameMaintainsOverflow()) {
            auto* entriesForParentDepth = mEntries.LastElement().get();
            if (!entriesForParentDepth) {
              mEntries.LastElement() = std::make_unique<FrameChangedMap>();
              entriesForParentDepth = mEntries.LastElement().get();
            }
            if (auto p = entriesForParentDepth->lookupForAdd(parent)) {
              p->value() = CHILDREN_CHANGED;
            } else {
              // We ignore OOM-failure here, just don't track this frame.
              (void)NS_WARN_IF(
                  !entriesForParentDepth->add(p, parent, CHILDREN_CHANGED));
            }
          }
        }
      }
    }
  }

 private:
  typedef HashMap<nsIFrame*, ChangeKind> FrameChangedMap;

  // A collection of frames to be processed. Frames whose depth in the frame
  // tree is /n/ will be stored in the hashmap at mEntries[n].
  AutoTArray<UniquePtr<FrameChangedMap>, 32> mEntries;

  // Don't update overflow of this frame or its ancestors.
  const nsIFrame* mSubtreeRoot;
};

}  // namespace mozilla

#endif
