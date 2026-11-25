/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * a list of the recomputation that needs to be done in response to a
 * style change
 */

#include "nsStyleChangeList.h"

#include "mozilla/dom/ElementInlines.h"
#include "nsCSSFrameConstructor.h"
#include "nsIContent.h"
#include "nsIFrame.h"

void nsStyleChangeList::AppendChange(nsIFrame* aFrame, nsIContent* aContent,
                                     nsChangeHint aHint) {
  MOZ_ASSERT(aFrame || (aHint & nsChangeHint_ReconstructFrame),
             "must have frame");
  MOZ_ASSERT(aHint, "No hint to process?");
  MOZ_ASSERT(!(aHint & nsChangeHint_NeutralChange),
             "Neutral changes do not need extra processing, "
             "and should be stripped out");
  MOZ_ASSERT(aContent || !(aHint & nsChangeHint_ReconstructFrame),
             "must have content");
  // XXXbz we should make this take Element instead of nsIContent
  MOZ_ASSERT(
      !aContent || aContent->IsElement() ||
          // display:contents elements posts the changes for their children:
          (aFrame && aContent->GetFlattenedTreeParentElementForStyle() &&
           Servo_Element_IsDisplayContents(
               aContent->GetFlattenedTreeParentElementForStyle())) ||
          (aContent->IsText() &&
           aContent->HasStyleFlag(NodeStyleFlags::NeedsFrame) &&
           aHint & nsChangeHint_ReconstructFrame),
      "Shouldn't be trying to restyle non-elements directly, "
      "except if it's a display:contents child or a text node "
      "doing lazy frame construction");
  MOZ_ASSERT(!(aHint & nsChangeHint_AllReflowHints) ||
                 (aHint & nsChangeHint_NeedReflow),
             "Reflow hint bits set without actually asking for a reflow");

  if (!IsEmpty() && aFrame && aFrame == LastElement().mFrame) {
    LastElement().mHint |= aHint;
    return;
  }

  if (aHint & nsChangeHint_ReconstructFrame) {
    // If Servo fires reconstruct at a node, it is the only change hint fired at
    // that node.

    // Note: Because we check whether |aHint| is a reconstruct above (which is
    // necessary to avoid debug test timeouts on certain crashtests), this check
    // will not find bugs where we add a non-reconstruct hint for an element
    // after adding a reconstruct. This is ok though, since
    // ProcessRestyledFrames will handle that case via mDestroyedFrames.
#ifdef DEBUG
    for (size_t i = 0; i < Length(); ++i) {
      MOZ_ASSERT(aContent != (*this)[i].mContent ||
                     !((*this)[i].mHint & nsChangeHint_ReconstructFrame),
                 "Should not append a non-ReconstructFrame hint after \
                 appending a ReconstructFrame hint for the same \
                 content.");
    }
#endif
  }

  AppendElement(nsStyleChangeData{aFrame, aContent, aHint});
}
