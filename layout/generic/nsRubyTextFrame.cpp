/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for CSS "display: ruby-text" */

#include "nsRubyTextFrame.h"

#include "mozilla/ComputedStyle.h"
#include "mozilla/PresShell.h"
#include "mozilla/WritingModes.h"
#include "nsLineLayout.h"
#include "nsPresContext.h"

using namespace mozilla;

//----------------------------------------------------------------------

// Frame class boilerplate
// =======================

NS_QUERYFRAME_HEAD(nsRubyTextFrame)
  NS_QUERYFRAME_ENTRY(nsRubyTextFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsRubyContentFrame)

NS_IMPL_FRAMEARENA_HELPERS(nsRubyTextFrame)

nsContainerFrame* NS_NewRubyTextFrame(PresShell* aPresShell,
                                      ComputedStyle* aStyle) {
  return new nsRubyTextFrame(aStyle, aPresShell->GetPresContext());
}

//----------------------------------------------------------------------

// nsRubyTextFrame Method Implementations
// ======================================

/* virtual */
bool nsRubyTextFrame::CanContinueTextRun() const { return false; }

#ifdef DEBUG_FRAME_DUMP
nsresult nsRubyTextFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"RubyText"_ns, aResult);
}
#endif

/* virtual */
void nsRubyTextFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                       const nsDisplayListSet& aLists) {
  if (IsCollapsed()) {
    return;
  }

  nsRubyContentFrame::BuildDisplayList(aBuilder, aLists);
}

/* virtual */
void nsRubyTextFrame::Reflow(nsPresContext* aPresContext,
                             ReflowOutput& aDesiredSize,
                             const ReflowInput& aReflowInput,
                             nsReflowStatus& aStatus) {
  // Even if we want to hide this frame, we have to reflow it first.
  // If we leave it dirty, changes to its content will never be
  // propagated to the ancestors, then it won't be displayed even if
  // the content is no longer the same, until next reflow triggered by
  // some other change. In general, we always reflow all the frames we
  // created. There might be other problems if we don't do that.
  nsRubyContentFrame::Reflow(aPresContext, aDesiredSize, aReflowInput, aStatus);

  if (IsCollapsed()) {
    // Reset the ISize. The BSize is not changed so that it won't
    // affect vertical positioning in unexpected way.
    WritingMode lineWM = aReflowInput.mLineLayout->GetWritingMode();
    aDesiredSize.ISize(lineWM) = 0;
    aDesiredSize.SetOverflowAreasToDesiredBounds();
  }
}
