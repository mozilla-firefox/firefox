/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for CSS "display: ruby-base" */

#include "nsRubyBaseFrame.h"

#include "mozilla/ComputedStyle.h"
#include "mozilla/PresShell.h"
#include "mozilla/WritingModes.h"
#include "nsLineLayout.h"
#include "nsPresContext.h"

using namespace mozilla;

//----------------------------------------------------------------------

// Frame class boilerplate
// =======================

NS_QUERYFRAME_HEAD(nsRubyBaseFrame)
  NS_QUERYFRAME_ENTRY(nsRubyBaseFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsRubyContentFrame)

NS_IMPL_FRAMEARENA_HELPERS(nsRubyBaseFrame)

nsContainerFrame* NS_NewRubyBaseFrame(mozilla::PresShell* aPresShell,
                                      ComputedStyle* aStyle) {
  return new nsRubyBaseFrame(aStyle, aPresShell->GetPresContext());
}

//----------------------------------------------------------------------

// nsRubyBaseFrame Method Implementations
// ======================================

#ifdef DEBUG_FRAME_DUMP
nsresult nsRubyBaseFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"RubyBase"_ns, aResult);
}
#endif
