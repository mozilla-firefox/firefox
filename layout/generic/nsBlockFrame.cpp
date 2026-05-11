/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * rendering object for CSS display:block, inline-block, and list-item
 * boxes, also used for various anonymous boxes
 */

#include "nsBlockFrame.h"

#include <inttypes.h>

#include <algorithm>

#include "BlockReflowState.h"
#include "CounterStyleManager.h"
#include "TextOverflow.h"
#ifdef DEBUG
#  include "fmt/base.h"
#endif
#include "gfxContext.h"
#include "mozilla/AbsoluteContainingBlock.h"
#include "mozilla/AppUnits.h"
#include "mozilla/Baseline.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/DefineEnum.h"
#include "mozilla/Likely.h"
#include "mozilla/Maybe.h"
#include "mozilla/PresShell.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/ToString.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/Selection.h"
#include "nsBidiPresUtils.h"
#include "nsBlockReflowContext.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsCSSFrameConstructor.h"
#include "nsCSSRendering.h"
#include "nsDisplayList.h"
#include "nsError.h"
#include "nsFirstLetterFrame.h"
#include "nsFlexContainerFrame.h"
#include "nsFloatManager.h"
#include "nsFontMetrics.h"
#include "nsFrameManager.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsHTMLParts.h"
#include "nsIFrameInlines.h"
#include "nsLayoutUtils.h"
#include "nsLineBox.h"
#include "nsLineLayout.h"
#include "nsPlaceholderFrame.h"
#include "nsPresContext.h"
#include "nsPresContextInlines.h"
#include "nsStyleConsts.h"
#include "nsTextControlFrame.h"
#include "prenv.h"

static const int MIN_LINES_NEEDING_CURSOR = 20;

using namespace mozilla;
using namespace mozilla::css;
using namespace mozilla::dom;
using namespace mozilla::layout;
using ClearFloatsResult = BlockReflowState::ClearFloatsResult;
using ShapeType = nsFloatManager::ShapeType;

static void MarkAllInlineLinesDirty(nsBlockFrame* aBlock) {
  for (auto& line : aBlock->Lines()) {
    if (line.IsInline()) {
      line.MarkDirty();
    }
  }
}

static void MarkAllDescendantLinesDirty(nsBlockFrame* aBlock) {
  for (auto& line : aBlock->Lines()) {
    if (line.IsBlock()) {
      nsBlockFrame* bf = do_QueryFrame(line.mFirstChild);
      if (bf) {
        MarkAllDescendantLinesDirty(bf);
      }
    }
    line.MarkDirty();
  }
}

static void MarkSameFloatManagerLinesDirty(nsBlockFrame* aBlock) {
  nsBlockFrame* blockWithFloatMgr = aBlock;
  while (!blockWithFloatMgr->HasAnyStateBits(NS_BLOCK_BFC)) {
    nsBlockFrame* bf = do_QueryFrame(blockWithFloatMgr->GetParent());
    if (!bf) {
      break;
    }
    blockWithFloatMgr = bf;
  }

  // Mark every line at and below the line where the float was
  // dirty, and mark their lines dirty too. We could probably do
  // something more efficient --- e.g., just dirty the lines that intersect
  // the float vertically.
  MarkAllDescendantLinesDirty(blockWithFloatMgr);
}

/**
 * Returns true if aFrame is a block that has one or more float children.
 */
static bool BlockHasAnyFloats(nsIFrame* aFrame) {
  nsBlockFrame* block = do_QueryFrame(aFrame);
  if (!block) {
    return false;
  }
  if (block->GetChildList(FrameChildListID::Float).FirstChild()) {
    return true;
  }

  for (const auto& line : block->Lines()) {
    if (line.IsBlock() && BlockHasAnyFloats(line.mFirstChild)) {
      return true;
    }
  }
  return false;
}

// Determines whether the given frame is visible text or has visible text that
// participate in the same line. Frames that are not line participants do not
// have their children checked.
static bool FrameHasVisibleInlineText(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame, "Frame argument cannot be null");
  if (!aFrame->IsLineParticipant()) {
    return false;
  }
  if (aFrame->IsTextFrame()) {
    return aFrame->StyleVisibility()->IsVisible() &&
           NS_GET_A(aFrame->StyleText()->mWebkitTextFillColor.CalcColor(
               aFrame)) != 0;
  }
  for (nsIFrame* kid : aFrame->PrincipalChildList()) {
    if (FrameHasVisibleInlineText(kid)) {
      return true;
    }
  }
  return false;
}

// Determines whether any of the frames from the given line have visible text.
static bool LineHasVisibleInlineText(nsLineBox* aLine) {
  for (nsIFrame* kid : aLine->ChildFrames()) {
    if (FrameHasVisibleInlineText(kid)) {
      return true;
    }
  }
  return false;
}

/**
 * Iterates through the frame's in-flow children and
 * unions the ink overflow of all text frames which
 * participate in the line aFrame belongs to.
 * If a child of aFrame is not a text frame,
 * we recurse with the child as the aFrame argument.
 * If aFrame isn't a line participant, we skip it entirely
 * and return an empty rect.
 * The resulting nsRect is offset relative to the parent of aFrame.
 */
static nsRect GetFrameTextArea(nsIFrame* aFrame,
                               nsDisplayListBuilder* aBuilder) {
  nsRect textArea;
  if (const nsTextFrame* textFrame = do_QueryFrame(aFrame)) {
    if (!textFrame->IsEntirelyWhitespace()) {
      textArea = aFrame->InkOverflowRect();
    }
  } else if (aFrame->IsLineParticipant()) {
    for (nsIFrame* kid : aFrame->PrincipalChildList()) {
      nsRect kidTextArea = GetFrameTextArea(kid, aBuilder);
      textArea.OrWith(kidTextArea);
    }
  }
  // add aFrame's position to keep textArea relative to aFrame's parent
  return textArea + aFrame->GetPosition();
}

/**
 * Iterates through the line's children and
 * unions the ink overflow of all text frames.
 * GetFrameTextArea unions and returns the ink overflow
 * from all line-participating text frames within the given child.
 * The nsRect returned from GetLineTextArea is offset
 * relative to the given line.
 */
static nsRect GetLineTextArea(nsLineBox* aLine,
                              nsDisplayListBuilder* aBuilder) {
  nsRect textArea;
  for (nsIFrame* kid : aLine->ChildFrames()) {
    nsRect kidTextArea = GetFrameTextArea(kid, aBuilder);
    textArea.OrWith(kidTextArea);
  }

  return textArea;
}

/**
 * Starting with aFrame, iterates upward through parent frames and checks for
 * non-transparent background colors. If one is found, we use that as our
 * backplate color. Otheriwse, we use the default background color from
 * our high contrast theme.
 */
static nscolor GetBackplateColor(nsIFrame* aFrame) {
  nsPresContext* pc = aFrame->PresContext();
  nscolor currentBackgroundColor = NS_TRANSPARENT;
  for (nsIFrame* frame = aFrame; frame; frame = frame->GetParent()) {
    // NOTE(emilio): We assume themed frames (frame->IsThemed()) have correct
    // background-color information so as to compute the right backplate color.
    //
    // This holds because HTML widgets with author-specified backgrounds or
    // borders disable theming. So as long as the UA-specified background colors
    // match the actual theme (which they should because we always use system
    // colors with the non-native theme, and native system colors should also
    // match the native theme), then we're alright and we should compute an
    // appropriate backplate color.
    const auto* style = frame->Style();
    if (style->StyleBackground()->IsTransparent(style)) {
      continue;
    }
    bool drawImage = false, drawColor = false;
    nscolor backgroundColor = nsCSSRendering::DetermineBackgroundColor(
        pc, style, frame, drawImage, drawColor);
    if (!drawColor && !drawImage) {
      continue;
    }
    if (NS_GET_A(backgroundColor) == 0) {
      // Even if there's a background image, if there's no background color we
      // keep going up the frame tree, see bug 1723938.
      continue;
    }
    if (NS_GET_A(currentBackgroundColor) == 0) {
      // Try to avoid somewhat expensive math in the common case.
      currentBackgroundColor = backgroundColor;
    } else {
      currentBackgroundColor =
          NS_ComposeColors(backgroundColor, currentBackgroundColor);
    }
    if (NS_GET_A(currentBackgroundColor) == 0xff) {
      // If fully opaque, we're done, otherwise keep going up blending with our
      // background.
      return currentBackgroundColor;
    }
  }
  nscolor backgroundColor = aFrame->PresContext()->DefaultBackgroundColor();
  if (NS_GET_A(currentBackgroundColor) == 0) {
    return backgroundColor;
  }
  return NS_ComposeColors(backgroundColor, currentBackgroundColor);
}

static nsRect GetNormalMarginRect(const nsIFrame& aFrame,
                                  bool aIncludePositiveMargins = true) {
  nsMargin m = aFrame.GetUsedMargin().ApplySkipSides(aFrame.GetSkipSides());
  if (!aIncludePositiveMargins) {
    m.EnsureAtMost(nsMargin());
  }
  auto rect = aFrame.GetRectRelativeToSelf();
  rect.Inflate(m);
  return rect + aFrame.GetNormalPosition();
}

#ifdef DEBUG
#  include "nsBlockDebugFlags.h"

bool nsBlockFrame::gLamePaintMetrics;
bool nsBlockFrame::gLameReflowMetrics;
bool nsBlockFrame::gNoisy;
bool nsBlockFrame::gNoisyDamageRepair;
bool nsBlockFrame::gNoisyIntrinsic;
bool nsBlockFrame::gNoisyReflow;
bool nsBlockFrame::gReallyNoisyReflow;
bool nsBlockFrame::gNoisyFloatManager;
bool nsBlockFrame::gVerifyLines;
bool nsBlockFrame::gDisableResizeOpt;

int32_t nsBlockFrame::gNoiseIndent;

struct BlockDebugFlags {
  const char* name;
  bool* on;
};

static const BlockDebugFlags gFlags[] = {
    {"reflow", &nsBlockFrame::gNoisyReflow},
    {"really-noisy-reflow", &nsBlockFrame::gReallyNoisyReflow},
    {"intrinsic", &nsBlockFrame::gNoisyIntrinsic},
    {"float-manager", &nsBlockFrame::gNoisyFloatManager},
    {"verify-lines", &nsBlockFrame::gVerifyLines},
    {"damage-repair", &nsBlockFrame::gNoisyDamageRepair},
    {"lame-paint-metrics", &nsBlockFrame::gLamePaintMetrics},
    {"lame-reflow-metrics", &nsBlockFrame::gLameReflowMetrics},
    {"disable-resize-opt", &nsBlockFrame::gDisableResizeOpt},
};
#  define NUM_DEBUG_FLAGS (sizeof(gFlags) / sizeof(gFlags[0]))

static void ShowDebugFlags() {
  printf("Here are the available GECKO_BLOCK_DEBUG_FLAGS:\n");
  const BlockDebugFlags* bdf = gFlags;
  const BlockDebugFlags* end = gFlags + NUM_DEBUG_FLAGS;
  for (; bdf < end; bdf++) {
    printf("  %s\n", bdf->name);
  }
  printf("Note: GECKO_BLOCK_DEBUG_FLAGS is a comma separated list of flag\n");
  printf("names (no whitespace)\n");
}

void nsBlockFrame::InitDebugFlags() {
  static bool firstTime = true;
  if (firstTime) {
    firstTime = false;
    char* flags = PR_GetEnv("GECKO_BLOCK_DEBUG_FLAGS");
    if (flags) {
      bool error = false;
      for (;;) {
        char* cm = strchr(flags, ',');
        if (cm) {
          *cm = '\0';
        }

        bool found = false;
        const BlockDebugFlags* bdf = gFlags;
        const BlockDebugFlags* end = gFlags + NUM_DEBUG_FLAGS;
        for (; bdf < end; bdf++) {
          if (nsCRT::strcasecmp(bdf->name, flags) == 0) {
            *(bdf->on) = true;
            printf("nsBlockFrame: setting %s debug flag on\n", bdf->name);
            gNoisy = true;
            found = true;
            break;
          }
        }
        if (!found) {
          error = true;
        }

        if (!cm) {
          break;
        }
        *cm = ',';
        flags = cm + 1;
      }
      if (error) {
        ShowDebugFlags();
      }
    }
  }
}

MOZ_DEFINE_ENUM_TOSTRING_FUNC(LineReflowStatus,
                              (OK, Stop, RedoNoPull, RedoMoreFloats,
                               RedoNextBand, Truncated));
#endif

#ifdef REFLOW_STATUS_COVERAGE
static void RecordReflowStatus(bool aChildIsBlock,
                               const nsReflowStatus& aFrameReflowStatus) {
  static uint32_t record[2];

  // 0: child-is-block
  // 1: child-is-inline
  int index = 0;
  if (!aChildIsBlock) {
    index |= 1;
  }

  // Compute new status
  uint32_t newS = record[index];
  if (aFrameReflowStatus.IsInlineBreak()) {
    if (aFrameReflowStatus.IsInlineBreakBefore()) {
      newS |= 1;
    } else if (aFrameReflowStatus.IsIncomplete()) {
      newS |= 2;
    } else {
      newS |= 4;
    }
  } else if (aFrameReflowStatus.IsIncomplete()) {
    newS |= 8;
  } else {
    newS |= 16;
  }

  // Log updates to the status that yield different values
  if (record[index] != newS) {
    record[index] = newS;
    printf("record(%d): %02x %02x\n", index, record[0], record[1]);
  }
}
#endif

NS_DECLARE_FRAME_PROPERTY_WITH_DTOR_NEVER_CALLED(OverflowLinesProperty,
                                                 nsBlockFrame::FrameLines)
NS_DECLARE_FRAME_PROPERTY_FRAMELIST(OverflowOutOfFlowsProperty)
NS_DECLARE_FRAME_PROPERTY_FRAMELIST(FloatsProperty)
NS_DECLARE_FRAME_PROPERTY_FRAMELIST(PushedFloatsProperty)
NS_DECLARE_FRAME_PROPERTY_FRAMELIST(OutsideMarkerProperty)
NS_DECLARE_FRAME_PROPERTY_WITHOUT_DTOR(InsideMarkerProperty, nsIFrame)

//----------------------------------------------------------------------

nsBlockFrame* NS_NewBlockFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new (aPresShell) nsBlockFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsBlockFrame)

nsBlockFrame::~nsBlockFrame() = default;

void nsBlockFrame::AddSizeOfExcludingThisForTree(
    nsWindowSizes& aWindowSizes) const {
  nsContainerFrame::AddSizeOfExcludingThisForTree(aWindowSizes);

  // Add the size of any nsLineBox::mFrames hashtables we might have:
  for (const auto& line : Lines()) {
    line.AddSizeOfExcludingThis(aWindowSizes);
  }
  const FrameLines* overflowLines = GetOverflowLines();
  if (overflowLines) {
    ConstLineIterator line = overflowLines->mLines.begin(),
                      line_end = overflowLines->mLines.end();
    for (; line != line_end; ++line) {
      line->AddSizeOfExcludingThis(aWindowSizes);
    }
  }
}

void nsBlockFrame::Destroy(DestroyContext& aContext) {
  ClearLineCursors();
  DestroyAbsoluteFrames(aContext);
  nsPresContext* presContext = PresContext();
  mozilla::PresShell* presShell = presContext->PresShell();
  if (HasFloats()) {
    SafelyDestroyFrameListProp(aContext, presShell, FloatsProperty());
    RemoveStateBits(NS_BLOCK_HAS_FLOATS);
  }
  nsLineBox::DeleteLineList(presContext, mLines, &mFrames, aContext);

  if (HasPushedFloats()) {
    SafelyDestroyFrameListProp(aContext, presShell, PushedFloatsProperty());
    RemoveStateBits(NS_BLOCK_HAS_PUSHED_FLOATS);
  }

  // destroy overflow lines now
  FrameLines* overflowLines = RemoveOverflowLines();
  if (overflowLines) {
    nsLineBox::DeleteLineList(presContext, overflowLines->mLines,
                              &overflowLines->mFrames, aContext);
    delete overflowLines;
  }

  if (HasAnyStateBits(NS_BLOCK_HAS_OVERFLOW_OUT_OF_FLOWS)) {
    SafelyDestroyFrameListProp(aContext, presShell,
                               OverflowOutOfFlowsProperty());
    RemoveStateBits(NS_BLOCK_HAS_OVERFLOW_OUT_OF_FLOWS);
  }

  if (HasMarker()) {
    SafelyDestroyFrameListProp(aContext, presShell, OutsideMarkerProperty());
    RemoveStateBits(NS_BLOCK_HAS_MARKER);
  }

  nsContainerFrame::Destroy(aContext);
}

/* virtual */
nsILineIterator* nsBlockFrame::GetLineIterator() {
  nsLineIterator* iter = GetProperty(LineIteratorProperty());
  if (!iter) {
    const nsStyleVisibility* visibility = StyleVisibility();
    iter = new nsLineIterator(mLines,
                              visibility->mDirection == StyleDirection::Rtl);
    SetProperty(LineIteratorProperty(), iter);
  }
  return iter;
}

NS_QUERYFRAME_HEAD(nsBlockFrame)
  NS_QUERYFRAME_ENTRY(nsBlockFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

#ifdef DEBUG_FRAME_DUMP
void nsBlockFrame::List(FILE* out, const char* aPrefix,
                        ListFlags aFlags) const {
  nsCString str;
  ListGeneric(str, aPrefix, aFlags);

  fprintf_stderr(out, "%s <\n", str.get());

  nsCString pfx(aPrefix);
  pfx += "  ";

  // Output the lines
  if (!mLines.empty()) {
    ConstLineIterator line = LinesBegin(), line_end = LinesEnd();
    for (; line != line_end; ++line) {
      line->List(out, pfx.get(), aFlags);
    }
  }

  // Output the overflow lines.
  const FrameLines* overflowLines = GetOverflowLines();
  if (overflowLines && !overflowLines->mLines.empty()) {
    fprintf_stderr(out, "%sOverflow-lines %p/%p <\n", pfx.get(), overflowLines,
                   &overflowLines->mFrames);
    nsCString nestedPfx(pfx);
    nestedPfx += "  ";
    ConstLineIterator line = overflowLines->mLines.begin(),
                      line_end = overflowLines->mLines.end();
    for (; line != line_end; ++line) {
      line->List(out, nestedPfx.get(), aFlags);
    }
    fprintf_stderr(out, "%s>\n", pfx.get());
  }

  // skip the principal list - we printed the lines above
  // skip the overflow list - we printed the overflow lines above
  ChildListIDs skip = {FrameChildListID::Principal, FrameChildListID::Overflow};
  ListChildLists(out, pfx.get(), aFlags, skip);

  fprintf_stderr(out, "%s>\n", aPrefix);
}

nsresult nsBlockFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"Block"_ns, aResult);
}
#endif

void nsBlockFrame::InvalidateFrame(uint32_t aDisplayItemKey,
                                   bool aRebuildDisplayItems) {
  if (IsInSVGTextSubtree()) {
    NS_ASSERTION(GetParent()->IsSVGTextFrame(),
                 "unexpected block frame in SVG text");
    GetParent()->InvalidateFrame();
    return;
  }
  nsContainerFrame::InvalidateFrame(aDisplayItemKey, aRebuildDisplayItems);
}

void nsBlockFrame::InvalidateFrameWithRect(const nsRect& aRect,
                                           uint32_t aDisplayItemKey,
                                           bool aRebuildDisplayItems) {
  if (IsInSVGTextSubtree()) {
    NS_ASSERTION(GetParent()->IsSVGTextFrame(),
                 "unexpected block frame in SVG text");
    GetParent()->InvalidateFrame();
    return;
  }
  nsContainerFrame::InvalidateFrameWithRect(aRect, aDisplayItemKey,
                                            aRebuildDisplayItems);
}

nscoord nsBlockFrame::SynthesizeFallbackBaseline(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup) const {
  if (IsButtonOrTextInput() && StyleDisplay()->IsInlineOutsideStyle()) {
    return Baseline::SynthesizeBOffsetFromContentBox(this, aWM, aBaselineGroup);
  }
  return Baseline::SynthesizeBOffsetFromMarginBox(this, aWM, aBaselineGroup);
}

template <typename LineIteratorType>
Maybe<nscoord> nsBlockFrame::GetBaselineBOffset(
    LineIteratorType aStart, LineIteratorType aEnd, WritingMode aWM,
    BaselineSharingGroup aBaselineGroup,
    BaselineExportContext aExportContext) const {
  MOZ_ASSERT((std::is_same_v<LineIteratorType, ConstLineIterator> &&
              aBaselineGroup == BaselineSharingGroup::First) ||
                 (std::is_same_v<LineIteratorType, ConstReverseLineIterator> &&
                  aBaselineGroup == BaselineSharingGroup::Last),
             "Iterator direction must match baseline sharing group.");
  for (auto line = aStart; line != aEnd; ++line) {
    if (!line->IsBlock()) {
      // XXX Is this the right test?  We have some bogus empty lines
      // floating around, but IsEmpty is perhaps too weak.
      if (line->BSize() != 0 || !line->IsEmpty()) {
        const auto ascent = line->BStart() + line->GetLogicalAscent();
        if (aBaselineGroup == BaselineSharingGroup::Last) {
          return Some(BSize(aWM) - ascent);
        }
        return Some(ascent);
      }
      continue;
    }
    nsIFrame* kid = line->mFirstChild;
    if (aWM.IsOrthogonalTo(kid->GetWritingMode())) {
      continue;
    }
    if (aExportContext == BaselineExportContext::LineLayout &&
        kid->IsTableWrapperFrame()) {
      // `<table>` in inline-block context does not export any baseline.
      continue;
    }
    const auto kidBaselineGroup =
        aExportContext == BaselineExportContext::LineLayout
            ? kid->GetDefaultBaselineSharingGroup()
            : aBaselineGroup;
    const auto kidBaseline =
        kid->GetNaturalBaselineBOffset(aWM, kidBaselineGroup, aExportContext);
    if (!kidBaseline) {
      continue;
    }
    auto result = *kidBaseline;
    if (kidBaselineGroup == BaselineSharingGroup::Last) {
      result = kid->BSize(aWM) - result;
    }
    // Ignore relative positioning for baseline calculations.
    const nsSize& sz = line->mContainerSize;
    result += kid->GetLogicalNormalPosition(aWM, sz).B(aWM);
    if (aBaselineGroup == BaselineSharingGroup::Last) {
      return Some(BSize(aWM) - result);
    }
    return Some(result);
  }
  return Nothing{};
}

Maybe<nscoord> nsBlockFrame::GetNaturalBaselineBOffset(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext aExportContext) const {
  if (StyleDisplay()->IsContainLayout()) {
    return Nothing{};
  }

  Maybe<nscoord> offset =
      aBaselineGroup == BaselineSharingGroup::First
          ? GetBaselineBOffset(LinesBegin(), LinesEnd(), aWM, aBaselineGroup,
                               aExportContext)
          : GetBaselineBOffset(LinesRBegin(), LinesREnd(), aWM, aBaselineGroup,
                               aExportContext);
  if (!offset && IsButtonOrTextInput()) {
    for (const auto& line : Reversed(Lines())) {
      if (line.IsEmpty()) {
        continue;
      }
      // Buttons use the end of the content as a baseline if we haven't found
      // one yet.
      nscoord bEnd = line.BEnd();
      offset.emplace(aBaselineGroup == BaselineSharingGroup::Last
                         ? BSize(aWM) - bEnd
                         : bEnd);
      break;
    }
  }
  return offset;
}

nscoord nsBlockFrame::GetCaretBaseline() const {
  const auto wm = GetWritingMode();
  if (!mLines.empty()) {
    ConstLineIterator line = LinesBegin();
    if (!line->IsEmpty()) {
      if (line->IsBlock()) {
        return GetLogicalUsedBorderAndPadding(wm).BStart(wm) +
               line->mFirstChild->GetCaretBaseline();
      }
      return line->BStart() + line->GetLogicalAscent();
    }
  }
  return GetFontMetricsDerivedCaretBaseline(ContentBSize(wm));
}

/////////////////////////////////////////////////////////////////////////////
// Child frame enumeration

const nsFrameList& nsBlockFrame::GetChildList(ChildListID aListID) const {
  switch (aListID) {
    case FrameChildListID::Principal:
      return mFrames;
    case FrameChildListID::Overflow: {
      FrameLines* overflowLines = GetOverflowLines();
      return overflowLines ? overflowLines->mFrames : nsFrameList::EmptyList();
    }
    case FrameChildListID::OverflowOutOfFlow: {
      const nsFrameList* list = GetOverflowOutOfFlows();
      return list ? *list : nsFrameList::EmptyList();
    }
    case FrameChildListID::Float: {
      const nsFrameList* list = GetFloats();
      return list ? *list : nsFrameList::EmptyList();
    }
    case FrameChildListID::PushedFloats: {
      const nsFrameList* list = GetPushedFloats();
      return list ? *list : nsFrameList::EmptyList();
    }
    case FrameChildListID::Marker: {
      const nsFrameList* list = GetOutsideMarkerList();
      return list ? *list : nsFrameList::EmptyList();
    }
    default:
      return nsContainerFrame::GetChildList(aListID);
  }
}

void nsBlockFrame::GetChildLists(nsTArray<ChildList>* aLists) const {
  nsContainerFrame::GetChildLists(aLists);
  FrameLines* overflowLines = GetOverflowLines();
  if (overflowLines) {
    overflowLines->mFrames.AppendIfNonempty(aLists, FrameChildListID::Overflow);
  }
  if (const nsFrameList* list = GetOverflowOutOfFlows()) {
    list->AppendIfNonempty(aLists, FrameChildListID::OverflowOutOfFlow);
  }
  if (const nsFrameList* list = GetOutsideMarkerList()) {
    list->AppendIfNonempty(aLists, FrameChildListID::Marker);
  }
  if (const nsFrameList* list = GetFloats()) {
    list->AppendIfNonempty(aLists, FrameChildListID::Float);
  }
  if (const nsFrameList* list = GetPushedFloats()) {
    list->AppendIfNonempty(aLists, FrameChildListID::PushedFloats);
  }
}

/* virtual */
bool nsBlockFrame::IsFloatContainingBlock() const { return true; }

/**
 * Remove the first line from aFromLines and adjust the associated frame list
 * aFromFrames accordingly.  The removed line is assigned to *aOutLine and
 * a frame list with its frames is assigned to *aOutFrames, i.e. the frames
 * that were extracted from the head of aFromFrames.
 * aFromLines must contain at least one line, the line may be empty.
 * @return true if aFromLines becomes empty
 */
static bool RemoveFirstLine(nsLineList& aFromLines, nsFrameList& aFromFrames,
                            nsLineBox** aOutLine, nsFrameList* aOutFrames) {
  LineListIterator removedLine = aFromLines.begin();
  *aOutLine = removedLine;
  LineListIterator next = aFromLines.erase(removedLine);
  bool isLastLine = next == aFromLines.end();
  nsIFrame* firstFrameInNextLine = isLastLine ? nullptr : next->mFirstChild;
  *aOutFrames = aFromFrames.TakeFramesBefore(firstFrameInNextLine);
  return isLastLine;
}

//////////////////////////////////////////////////////////////////////
// Reflow methods

/* virtual */
void nsBlockFrame::MarkIntrinsicISizesDirty() {
  nsBlockFrame* dirtyBlock = static_cast<nsBlockFrame*>(FirstContinuation());
  dirtyBlock->mCachedIntrinsics.Clear();
  if (!HasAnyStateBits(NS_BLOCK_NEEDS_BIDI_RESOLUTION)) {
    for (nsIFrame* frame = dirtyBlock; frame;
         frame = frame->GetNextContinuation()) {
      frame->AddStateBits(NS_BLOCK_NEEDS_BIDI_RESOLUTION);
    }
  }

  nsContainerFrame::MarkIntrinsicISizesDirty();
}

void nsBlockFrame::CheckIntrinsicCacheAgainstShrinkWrapState() {
  nsPresContext* presContext = PresContext();
  if (!nsLayoutUtils::FontSizeInflationEnabled(presContext)) {
    return;
  }
  bool inflationEnabled = !presContext->mInflationDisabledForShrinkWrap;
  if (inflationEnabled != HasAnyStateBits(NS_BLOCK_INTRINSICS_INFLATED)) {
    mCachedIntrinsics.Clear();
    AddOrRemoveStateBits(NS_BLOCK_INTRINSICS_INFLATED, inflationEnabled);
  }
}

// Whether this line is indented by the text-indent amount.
bool nsBlockFrame::TextIndentAppliesTo(const LineIterator& aLine) const {
  const auto& textIndent = StyleText()->mTextIndent;

  bool isFirstLineOrAfterHardBreak = [&] {
    if (aLine != LinesBegin()) {
      // If not the first line of the block, but 'each-line' is in effect,
      // check if the previous line was not wrapped.
      return textIndent.each_line && !aLine.prev()->IsLineWrapped();
    }
    if (nsBlockFrame* prevBlock = do_QueryFrame(GetPrevInFlow())) {
      // There's a prev-in-flow, so this only counts as a first-line if
      // 'each-line' and the prev-in-flow's last line was not wrapped.
      return textIndent.each_line &&
             (prevBlock->Lines().empty() ||
              !prevBlock->LinesEnd().prev()->IsLineWrapped());
    }
    return true;
  }();

  // The 'hanging' option inverts which lines are/aren't indented.
  return isFirstLineOrAfterHardBreak != textIndent.hanging;
}

nscoord nsBlockFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                     IntrinsicISizeType aType) {
  nsIFrame* firstCont = FirstContinuation();
  if (firstCont != this) {
    return firstCont->IntrinsicISize(aInput, aType);
  }

  CheckIntrinsicCacheAgainstShrinkWrapState();

  return mCachedIntrinsics.GetOrSet(*this, aType, aInput, [&] {
    return aType == IntrinsicISizeType::MinISize ? MinISize(aInput)
                                                 : PrefISize(aInput);
  });
}

/* virtual */
nscoord nsBlockFrame::MinISize(const IntrinsicSizeInput& aInput) {
  if (Maybe<nscoord> containISize = ContainIntrinsicISize()) {
    return *containISize;
  }

#ifdef DEBUG
  if (gNoisyIntrinsic) {
    IndentBy(stdout, gNoiseIndent);
    ListTag(stdout);
    printf(": MinISize\n");
  }
  AutoNoisyIndenter indenter(gNoisyIntrinsic);
#endif

  for (nsBlockFrame* curFrame = this; curFrame;
       curFrame = static_cast<nsBlockFrame*>(curFrame->GetNextContinuation())) {
    curFrame->LazyMarkLinesDirty();
  }

  if (HasAnyStateBits(NS_BLOCK_NEEDS_BIDI_RESOLUTION) &&
      PresContext()->BidiEnabled()) {
    ResolveBidi();
  }

  const bool whiteSpaceCanWrap = StyleText()->WhiteSpaceCanWrapStyle();
  InlineMinISizeData data;
  for (nsBlockFrame* curFrame = this; curFrame;
       curFrame = static_cast<nsBlockFrame*>(curFrame->GetNextContinuation())) {
    for (LineIterator line = curFrame->LinesBegin(),
                      line_end = curFrame->LinesEnd();
         line != line_end; ++line) {
#ifdef DEBUG
      if (gNoisyIntrinsic) {
        IndentBy(stdout, gNoiseIndent);
        printf("line (%s%s)\n", line->IsBlock() ? "block" : "inline",
               line->IsEmpty() ? ", empty" : "");
      }
      AutoNoisyIndenter lineindent(gNoisyIntrinsic);
#endif
      if (line->IsBlock()) {
        data.ForceBreak();
        nsIFrame* kid = line->mFirstChild;
        const IntrinsicSizeInput kidInput(aInput, kid->GetWritingMode(),
                                          GetWritingMode());
        data.mCurrentLine = nsLayoutUtils::IntrinsicForContainer(
            kidInput.mContext, kid, IntrinsicISizeType::MinISize,
            kidInput.mPercentageBasisForChildren);
        data.ForceBreak();
      } else {
        if (!curFrame->GetPrevContinuation() && TextIndentAppliesTo(line)) {
          data.mCurrentLine += StyleText()->mTextIndent.length.Resolve(0);
        }
        data.mLine = &line;
        data.SetLineContainer(curFrame);
        for (nsIFrame* kid : line->ChildFrames()) {
          const IntrinsicSizeInput kidInput(aInput, kid->GetWritingMode(),
                                            GetWritingMode());
          kid->AddInlineMinISize(kidInput, &data);
          if (whiteSpaceCanWrap && data.mTrailingWhitespace) {
            data.OptionallyBreak();
          }
        }
      }
#ifdef DEBUG
      if (gNoisyIntrinsic) {
        IndentBy(stdout, gNoiseIndent);
        printf("min: [prevLines=%d currentLine=%d]\n", data.mPrevLines,
               data.mCurrentLine);
      }
#endif
    }
  }
  data.ForceBreak();
  return data.mPrevLines;
}

/* virtual */
nscoord nsBlockFrame::PrefISize(const IntrinsicSizeInput& aInput) {
  if (Maybe<nscoord> containISize = ContainIntrinsicISize()) {
    return *containISize;
  }

#ifdef DEBUG
  if (gNoisyIntrinsic) {
    IndentBy(stdout, gNoiseIndent);
    ListTag(stdout);
    printf(": PrefISize\n");
  }
  AutoNoisyIndenter indenter(gNoisyIntrinsic);
#endif

  for (nsBlockFrame* curFrame = this; curFrame;
       curFrame = static_cast<nsBlockFrame*>(curFrame->GetNextContinuation())) {
    curFrame->LazyMarkLinesDirty();
  }

  if (HasAnyStateBits(NS_BLOCK_NEEDS_BIDI_RESOLUTION) &&
      PresContext()->BidiEnabled()) {
    ResolveBidi();
  }
  InlinePrefISizeData data;
  for (nsBlockFrame* curFrame = this; curFrame;
       curFrame = static_cast<nsBlockFrame*>(curFrame->GetNextContinuation())) {
    for (LineIterator line = curFrame->LinesBegin(),
                      line_end = curFrame->LinesEnd();
         line != line_end; ++line) {
#ifdef DEBUG
      if (gNoisyIntrinsic) {
        IndentBy(stdout, gNoiseIndent);
        printf("line (%s%s)\n", line->IsBlock() ? "block" : "inline",
               line->IsEmpty() ? ", empty" : "");
      }
      AutoNoisyIndenter lineindent(gNoisyIntrinsic);
#endif
      if (line->IsBlock()) {
        nsIFrame* kid = line->mFirstChild;
        UsedClear clearType;
        if (!data.mLineIsEmpty || BlockCanIntersectFloats(kid)) {
          clearType = UsedClear::Both;
        } else {
          clearType = kid->StyleDisplay()->UsedClear(GetWritingMode());
        }
        data.ForceBreak(clearType);
        const IntrinsicSizeInput kidInput(aInput, kid->GetWritingMode(),
                                          GetWritingMode());
        data.mCurrentLine = nsLayoutUtils::IntrinsicForContainer(
            kidInput.mContext, kid, IntrinsicISizeType::PrefISize,
            kidInput.mPercentageBasisForChildren);
        data.ForceBreak();
      } else {
        if (!curFrame->GetPrevContinuation() && TextIndentAppliesTo(line)) {
          nscoord indent = StyleText()->mTextIndent.length.Resolve(0);
          data.mCurrentLine += indent;
          // XXXmats should the test below be indent > 0?
          if (indent != nscoord(0)) {
            data.mLineIsEmpty = false;
          }
        }
        data.mLine = &line;
        data.SetLineContainer(curFrame);
        for (nsIFrame* kid : line->ChildFrames()) {
          const IntrinsicSizeInput kidInput(aInput, kid->GetWritingMode(),
                                            GetWritingMode());
          kid->AddInlinePrefISize(kidInput, &data);
        }
      }
#ifdef DEBUG
      if (gNoisyIntrinsic) {
        IndentBy(stdout, gNoiseIndent);
        printf("pref: [prevLines=%d currentLine=%d]\n", data.mPrevLines,
               data.mCurrentLine);
      }
#endif
    }
  }
  data.ForceBreak();
  return data.mPrevLines;
}

nsRect nsBlockFrame::ComputeTightBounds(DrawTarget* aDrawTarget) const {
  // be conservative
  if (Style()->HasTextDecorationLines()) {
    return InkOverflowRect();
  }
  return ComputeSimpleTightBounds(aDrawTarget);
}

/* virtual */
nsresult nsBlockFrame::GetPrefWidthTightBounds(gfxContext* aRenderingContext,
                                               nscoord* aX, nscoord* aXMost) {
  nsIFrame* firstInFlow = FirstContinuation();
  if (firstInFlow != this) {
    return firstInFlow->GetPrefWidthTightBounds(aRenderingContext, aX, aXMost);
  }

  *aX = 0;
  *aXMost = 0;

  nsresult rv;
  InlinePrefISizeData data;
  for (nsBlockFrame* curFrame = this; curFrame;
       curFrame = static_cast<nsBlockFrame*>(curFrame->GetNextContinuation())) {
    for (LineIterator line = curFrame->LinesBegin(),
                      line_end = curFrame->LinesEnd();
         line != line_end; ++line) {
      nscoord childX, childXMost;
      if (line->IsBlock()) {
        data.ForceBreak();
        rv = line->mFirstChild->GetPrefWidthTightBounds(aRenderingContext,
                                                        &childX, &childXMost);
        NS_ENSURE_SUCCESS(rv, rv);
        *aX = std::min(*aX, childX);
        *aXMost = std::max(*aXMost, childXMost);
      } else {
        if (!curFrame->GetPrevContinuation() && TextIndentAppliesTo(line)) {
          data.mCurrentLine += StyleText()->mTextIndent.length.Resolve(0);
        }
        data.mLine = &line;
        data.SetLineContainer(curFrame);
        // Per comment in nsIFrame::GetPrefWidthTightBounds(), the function is
        // only implemented for nsBlockFrame and nsTextFrame and is used to
        // determine the intrinsic inline sizes of MathML token elements. These
        // elements shouldn't have percentage block sizes that require a
        // percentage basis for resolution.
        const IntrinsicSizeInput kidInput(aRenderingContext, Nothing(),
                                          Nothing());
        for (nsIFrame* kid : line->ChildFrames()) {
          rv = kid->GetPrefWidthTightBounds(aRenderingContext, &childX,
                                            &childXMost);
          NS_ENSURE_SUCCESS(rv, rv);
          *aX = std::min(*aX, data.mCurrentLine + childX);
          *aXMost = std::max(*aXMost, data.mCurrentLine + childXMost);
          kid->AddInlinePrefISize(kidInput, &data);
        }
      }
    }
  }
  data.ForceBreak();

  return NS_OK;
}

/**
 * Return whether aNewAvailableSpace is smaller *on either side*
 * (inline-start or inline-end) than aOldAvailableSpace, so that we know
 * if we need to redo layout on an line, replaced block, or block
 * formatting context, because its height (which we used to compute
 * aNewAvailableSpace) caused it to intersect additional floats.
 */
static bool AvailableSpaceShrunk(WritingMode aWM,
                                 const LogicalRect& aOldAvailableSpace,
                                 const LogicalRect& aNewAvailableSpace,
                                 bool aCanGrow /* debug-only */) {
  if (aNewAvailableSpace.ISize(aWM) == 0) {
    // Positions are not significant if the inline size is zero.
    return aOldAvailableSpace.ISize(aWM) != 0;
  }
  if (aCanGrow) {
    NS_ASSERTION(
        aNewAvailableSpace.IStart(aWM) <= aOldAvailableSpace.IStart(aWM) ||
            aNewAvailableSpace.IEnd(aWM) <= aOldAvailableSpace.IEnd(aWM),
        "available space should not shrink on the start side and "
        "grow on the end side");
    NS_ASSERTION(
        aNewAvailableSpace.IStart(aWM) >= aOldAvailableSpace.IStart(aWM) ||
            aNewAvailableSpace.IEnd(aWM) >= aOldAvailableSpace.IEnd(aWM),
        "available space should not grow on the start side and "
        "shrink on the end side");
  } else {
    NS_ASSERTION(
        aOldAvailableSpace.IStart(aWM) <= aNewAvailableSpace.IStart(aWM) &&
            aOldAvailableSpace.IEnd(aWM) >= aNewAvailableSpace.IEnd(aWM),
        "available space should never grow");
  }
  // Have we shrunk on either side?
  return aNewAvailableSpace.IStart(aWM) > aOldAvailableSpace.IStart(aWM) ||
         aNewAvailableSpace.IEnd(aWM) < aOldAvailableSpace.IEnd(aWM);
}

/**
 * Returns aFrame if it is an in-flow, non-BFC block frame, and null otherwise.
 *
 * This is used to determine whether to recurse into aFrame when applying
 * -webkit-line-clamp.
 */
static const nsBlockFrame* GetAsLineClampDescendant(const nsIFrame* aFrame) {
  const nsBlockFrame* block = do_QueryFrame(aFrame);
  if (!block || block->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW | NS_BLOCK_BFC)) {
    return nullptr;
  }
  return block;
}

static nsBlockFrame* GetAsLineClampDescendant(nsIFrame* aFrame) {
  return const_cast<nsBlockFrame*>(
      GetAsLineClampDescendant(const_cast<const nsIFrame*>(aFrame)));
}

static bool IsLineClampRoot(const nsBlockFrame* aFrame) {
  if (!aFrame->StyleDisplay()->mWebkitLineClamp) {
    return false;
  }

  if (!aFrame->HasAnyStateBits(NS_BLOCK_BFC)) {
    return false;
  }

  if (StaticPrefs::layout_css_webkit_line_clamp_block_enabled() ||
      aFrame->PresContext()->Document()->ChromeRulesEnabled()) {
    return true;
  }

  // For now, -webkit-box is the only thing allowed to be a line-clamp root.
  // Ideally we'd just make this work everywhere, but for now we're carrying
  // this forward as a limitation on the legacy -webkit-line-clamp feature,
  // since relaxing this limitation might create webcompat trouble.
  auto origDisplay = [&] {
    if (aFrame->Style()->GetPseudoType() ==
        PseudoStyleType::MozScrolledContent) {
      // If we're the anonymous block inside the scroll frame, we need to look
      // at the original display of our parent frame.
      MOZ_ASSERT(aFrame->GetParent());
      const auto& parentDisp = *aFrame->GetParent()->StyleDisplay();
      MOZ_ASSERT(parentDisp.mWebkitLineClamp ==
                     aFrame->StyleDisplay()->mWebkitLineClamp,
                 ":-moz-scrolled-content should inherit -webkit-line-clamp, "
                 "via rule in UA stylesheet");
      return parentDisp.mOriginalDisplay;
    }
    return aFrame->StyleDisplay()->mOriginalDisplay;
  }();
  return origDisplay.Inside() == StyleDisplayInside::WebkitBox;
}

nsBlockFrame* nsBlockFrame::GetLineClampRoot() const {
  if (IsLineClampRoot(this)) {
    return const_cast<nsBlockFrame*>(this);
  }
  const nsBlockFrame* cur = this;
  while (GetAsLineClampDescendant(cur)) {
    cur = do_QueryFrame(cur->GetParent());
    if (!cur) {
      break;
    }
    if (IsLineClampRoot(cur)) {
      return const_cast<nsBlockFrame*>(cur);
    }
  }
  return nullptr;
}

bool nsBlockFrame::MaybeHasFloats() const {
  if (HasFloats()) {
    return true;
  }
  if (HasPushedFloats()) {
    return true;
  }
  // For the OverflowOutOfFlowsProperty I think we do enforce that, but it's
  // a mix of out-of-flow frames, so that's why the method name has "Maybe".
  return HasAnyStateBits(NS_BLOCK_HAS_OVERFLOW_OUT_OF_FLOWS);
}

/**
 * Iterator over all descendant inline line boxes, except for those that are
 * under an independent formatting context.
 */
class MOZ_RAII LineClampLineIterator {
 public:
  LineClampLineIterator(nsBlockFrame* aFrame, const nsBlockFrame* aStopAtFrame)
      : mCur(aFrame->LinesBegin()),
        mEnd(aFrame->LinesEnd()),
        mCurrentFrame(mCur == mEnd ? nullptr : aFrame),
        mStopAtFrame(aStopAtFrame) {
    if (mCur != mEnd && !mCur->IsInline()) {
      Advance();
    }
  }

  nsLineBox* GetCurrentLine() { return mCurrentFrame ? mCur.get() : nullptr; }
  nsBlockFrame* GetCurrentFrame() { return mCurrentFrame; }

  // Advances the iterator to the next line line.
  //
  // Next() shouldn't be called once the iterator is at the end, which can be
  // checked for by GetCurrentLine() or GetCurrentFrame() returning null.
  void Next() {
    MOZ_ASSERT(mCur != mEnd && mCurrentFrame,
               "Don't call Next() when the iterator is at the end");
    ++mCur;
    Advance();
  }

 private:
  void Advance() {
    for (;;) {
      if (mCur == mEnd) {
        // Reached the end of the current block.  Pop the parent off the
        // stack; if there isn't one, then we've reached the end.
        if (mStack.IsEmpty()) {
          mCurrentFrame = nullptr;
          break;
        }
        if (mCurrentFrame == mStopAtFrame) {
          mStack.Clear();
          mCurrentFrame = nullptr;
          break;
        }

        auto entry = mStack.PopLastElement();
        mCurrentFrame = entry.first;
        mCur = entry.second;
        mEnd = mCurrentFrame->LinesEnd();
      } else if (mCur->IsBlock()) {
        if (nsBlockFrame* child = GetAsLineClampDescendant(mCur->mFirstChild)) {
          nsBlockFrame::LineIterator next = mCur;
          ++next;
          mStack.AppendElement(std::make_pair(mCurrentFrame, next));
          mCur = child->LinesBegin();
          mEnd = child->LinesEnd();
          mCurrentFrame = child;
        } else {
          // Some kind of frame we shouldn't descend into.
          ++mCur;
        }
      } else {
        MOZ_ASSERT(mCur->IsInline());
        break;
      }
    }
  }

  // The current line within the current block.
  //
  // When this is equal to mEnd, the iterator is at its end, and mCurrentFrame
  // is set to null.
  nsBlockFrame::LineIterator mCur;

  // The iterator end for the current block.
  nsBlockFrame::LineIterator mEnd;

  // The current block.
  nsBlockFrame* mCurrentFrame;

  // The block past which we can't look at line-clamp.
  const nsBlockFrame* mStopAtFrame;

  // Stack of mCurrentFrame and mEnd values that we push and pop as we enter and
  // exist blocks.
  AutoTArray<std::pair<nsBlockFrame*, nsBlockFrame::LineIterator>, 8> mStack;
};

static bool ClearLineClampEllipsis(nsBlockFrame* aFrame) {
  if (aFrame->HasLineClampEllipsis()) {
    MOZ_ASSERT(!aFrame->HasLineClampEllipsisDescendant());
    aFrame->SetHasLineClampEllipsis(false);
    for (auto& line : aFrame->Lines()) {
      if (line.HasLineClampEllipsis()) {
        line.ClearHasLineClampEllipsis();
        break;
      }
    }
    return true;
  }

  if (aFrame->HasLineClampEllipsisDescendant()) {
    aFrame->SetHasLineClampEllipsisDescendant(false);
    for (nsIFrame* f : aFrame->PrincipalChildList()) {
      if (nsBlockFrame* child = GetAsLineClampDescendant(f)) {
        if (ClearLineClampEllipsis(child)) {
          return true;
        }
      }
    }
  }

  return false;
}

void nsBlockFrame::ClearLineClampEllipsis() { ::ClearLineClampEllipsis(this); }

void nsBlockFrame::Reflow(nsPresContext* aPresContext, ReflowOutput& aMetrics,
                          const ReflowInput& aReflowInput,
                          nsReflowStatus& aStatus) {
  if (IsHiddenByContentVisibilityOfInFlowParentForLayout()) {
    FinishAndStoreOverflow(&aMetrics, aReflowInput.mStyleDisplay);
    return;
  }

  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsBlockFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

#ifdef DEBUG
  if (gNoisyReflow) {
    IndentBy(stdout, gNoiseIndent);
    fmt::println("{}: begin reflow: availSize={} computedSize={}",
                 ListTag().get(), ToString(aReflowInput.AvailableSize()),
                 ToString(aReflowInput.ComputedSize()));
  }
  AutoNoisyIndenter indent(gNoisy);
  PRTime start = 0;  // Initialize these variablies to silence the compiler.
  int32_t ctc = 0;   // We only use these if they are set (gLameReflowMetrics).
  if (gLameReflowMetrics) {
    start = PR_Now();
    ctc = nsLineBox::GetCtorCount();
  }
#endif

  // ColumnSetWrapper's children depend on ColumnSetWrapper's block-size or
  // max-block-size because both affect the children's available block-size.
  if (IsColumnSetWrapperFrame()) {
    AddStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);
  }

  Maybe<nscoord> restoreReflowInputAvailBSize;
  auto MaybeRestore = MakeScopeExit([&] {
    if (MOZ_UNLIKELY(restoreReflowInputAvailBSize)) {
      const_cast<ReflowInput&>(aReflowInput)
          .SetAvailableBSize(*restoreReflowInputAvailBSize);
    }
  });

  WritingMode wm = aReflowInput.GetWritingMode();
  const nscoord consumedBSize = CalcAndCacheConsumedBSize();
  const nscoord effectiveContentBoxBSize =
      GetEffectiveComputedBSize(aReflowInput, consumedBSize);
  // If we have non-auto block size, we're clipping our kids and we fit,
  // make sure our kids fit too.
  if (aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE &&
      aReflowInput.ComputedBSize() != NS_UNCONSTRAINEDSIZE &&
      ShouldApplyOverflowClipping(aReflowInput.mStyleDisplay)
          .contains(wm.PhysicalAxis(LogicalAxis::Block))) {
    LogicalMargin blockDirExtras =
        aReflowInput.ComputedLogicalBorderPadding(wm);
    if (GetLogicalSkipSides().BStart()) {
      blockDirExtras.BStart(wm) = 0;
    } else {
      // Block-end margin never causes us to create continuations, so we
      // don't need to worry about whether it fits in its entirety.
      blockDirExtras.BStart(wm) +=
          aReflowInput.ComputedLogicalMargin(wm).BStart(wm);
    }

    if (effectiveContentBoxBSize + blockDirExtras.BStartEnd(wm) <=
        aReflowInput.AvailableBSize()) {
      restoreReflowInputAvailBSize.emplace(aReflowInput.AvailableBSize());
      const_cast<ReflowInput&>(aReflowInput)
          .SetAvailableBSize(NS_UNCONSTRAINEDSIZE);
    }
  }

  if (IsFrameTreeTooDeep(aReflowInput, aMetrics, aStatus)) {
    return;
  }

  // OK, some lines may be reflowed. Blow away any saved line cursor
  // because we may invalidate the nondecreasing
  // overflowArea.InkOverflow().y/yMost invariant, and we may even
  // delete the line with the line cursor.
  ClearLineCursors();

  // See comment below about oldSize. Use *only* for the
  // abs-pos-containing-block-size-change optimization!
  nsSize oldSize = GetSize();

  // Should we create a float manager?
  nsAutoFloatManager autoFloatManager(const_cast<ReflowInput&>(aReflowInput));

  // XXXldb If we start storing the float manager in the frame rather
  // than keeping it around only during reflow then we should create it
  // only when there are actually floats to manage.  Otherwise things
  // like tables will gain significant bloat.
  //
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=1931286:
  // if we're a reflow root and no float manager is provided by the caller
  // in aReflowInput, we'd normally expect the block to be a BFC and so
  // BlockNeedsFloatManager will return true. But sometimes the block may
  // have lost its BFC-ness since it was recorded as a dirty reflow root
  // but before the reflow actually happens. Creating a float manager here
  // avoids crashing, but may not be entirely correct in such a case.
  bool needFloatManager =
      !aReflowInput.mFloatManager || nsBlockFrame::BlockNeedsFloatManager(this);
  if (needFloatManager) {
    autoFloatManager.CreateFloatManager(aPresContext);
  }

  if (HasAnyStateBits(NS_BLOCK_NEEDS_BIDI_RESOLUTION) &&
      PresContext()->BidiEnabled()) {
    static_cast<nsBlockFrame*>(FirstContinuation())->ResolveBidi();
  }

  // Whether to apply text-wrap: balance behavior.
  bool tryBalance =
      StyleText()->mTextWrapStyle == StyleTextWrapStyle::Balance &&
      !GetPrevContinuation();

  // Struct used to hold the "target" number of lines or clamp position to
  // maintain when doing text-wrap: balance.
  struct BalanceTarget {
    // If line-clamp is in effect, mContent and mOffset indicate the starting
    // position of the first line after the clamp limit, and mBlockCoord is the
    // block-axis offset of its position.
    // If line-clamp is not in use, mContent is null, mOffset is the total
    // number of lines that the block must contain, and mBlockCoord is its end
    // edge in the block direction.
    nsIContent* mContent = nullptr;
    int32_t mOffset = -1;
    nscoord mBlockCoord = 0;

    bool operator==(const BalanceTarget& aOther) const = default;
    bool operator!=(const BalanceTarget& aOther) const = default;
  };

  BalanceTarget balanceTarget;

  // Helpers for text-wrap: balance implementation:

  // Count the number of inline lines in the mLines list, but return -1 (to
  // suppress balancing) instead if the count is going to exceed aLimit.
  auto countLinesUpTo = [&](int32_t aLimit) -> int32_t {
    int32_t n = 0;
    for (auto iter = mLines.begin(); iter != mLines.end(); ++iter) {
      // Block lines are ignored as they do not participate in balancing.
      if (iter->IsInline() && ++n > aLimit) {
        return -1;
      }
    }
    return n;
  };

  // Return a BalanceTarget record representing the position at which line-clamp
  // will take effect for the current line list. Only to be used when there are
  // enough lines that the clamp will apply.
  auto getClampPosition = [&](uint32_t aClampCount) -> BalanceTarget {
    if (NS_WARN_IF(aClampCount >= mLines.size())) {
      return BalanceTarget{};
    }
    auto iter = mLines.begin();
    for (uint32_t i = 0; i < aClampCount; i++) {
      ++iter;
    }
    nsIFrame* firstChild = iter->mFirstChild;
    if (!firstChild) {
      return BalanceTarget{};
    }
    nsIContent* content = firstChild->GetContent();
    if (!content) {
      return BalanceTarget{};
    }
    int32_t offset = 0;
    if (firstChild->IsTextFrame()) {
      auto* textFrame = static_cast<nsTextFrame*>(firstChild);
      offset = textFrame->GetContentOffset();
    }
    return BalanceTarget{content, offset, iter.get()->BStart()};
  };

  // "balancing" is implemented by shortening the effective inline-size of the
  // lines, so that content will tend to be pushed down to fill later lines of
  // the block. `balanceInset` is the current amount of "inset" to apply, and
  // `balanceStep` is the increment to adjust it by for the next iteration.
  nscoord balanceStep = 0;

  // text-wrap: balance loop, executed only once if balancing is not required.
  nsReflowStatus reflowStatus;
  TrialReflowState trialState(consumedBSize, effectiveContentBoxBSize,
                              needFloatManager);
  while (true) {
    // Save the initial floatManager state for repeated trial reflows.
    // We'll restore (and re-save) the initial state each time we repeat the
    // reflow.
    nsFloatManager::SavedState floatManagerState;
    aReflowInput.mFloatManager->PushState(&floatManagerState);

    aMetrics = ReflowOutput(aMetrics.GetWritingMode());
    reflowStatus =
        TrialReflow(aPresContext, aMetrics, aReflowInput, trialState);

    // Do we need to start a `text-wrap: balance` iteration?
    if (tryBalance) {
      tryBalance = false;
      // Don't try to balance an incomplete block, or if we had to use an
      // overflow-wrap break position in the initial reflow.
      if (!reflowStatus.IsFullyComplete() || trialState.mUsedOverflowWrap) {
        break;
      }
      balanceTarget.mOffset =
          countLinesUpTo(StaticPrefs::layout_css_text_wrap_balance_limit());
      if (balanceTarget.mOffset < 2) {
        // If there are less than 2 lines, or the number exceeds the limit,
        // no balancing is needed; just break from the balance loop.
        break;
      }
      balanceTarget.mBlockCoord = mLines.back()->BEnd();
      // Initialize the amount of inset to try, and the iteration step size.
      balanceStep = aReflowInput.ComputedISize() / balanceTarget.mOffset;
      trialState.ResetForBalance(balanceStep);
      balanceStep /= 2;

      // If -webkit-line-clamp is in effect, then we need to maintain the
      // content location at which clamping occurs, rather than the total
      // number of lines in the block.
      if (StaticPrefs::layout_css_text_wrap_balance_after_clamp_enabled() &&
          IsLineClampRoot(this)) {
        uint32_t lineClampCount = aReflowInput.mStyleDisplay->mWebkitLineClamp;
        if (uint32_t(balanceTarget.mOffset) > lineClampCount) {
          auto t = getClampPosition(lineClampCount);
          if (t.mContent) {
            balanceTarget = t;
          }
        }
      }

      // Restore initial floatManager state for a new trial with updated inset.
      aReflowInput.mFloatManager->PopState(&floatManagerState);
      continue;
    }

    // Helper to determine whether the current trial succeeded (i.e. was able
    // to fit the content into the expected number of lines).
    auto trialSucceeded = [&]() -> bool {
      if (!reflowStatus.IsFullyComplete() || trialState.mUsedOverflowWrap) {
        return false;
      }
      if (balanceTarget.mContent) {
        auto t = getClampPosition(aReflowInput.mStyleDisplay->mWebkitLineClamp);
        return t == balanceTarget;
      }
      int32_t numLines =
          countLinesUpTo(StaticPrefs::layout_css_text_wrap_balance_limit());
      return numLines == balanceTarget.mOffset &&
             mLines.back()->BEnd() == balanceTarget.mBlockCoord;
    };

    // If we're in the process of a balance operation, check whether we've
    // inset by too much and either increase or reduce the inset for the next
    // iteration.
    if (balanceStep > 0) {
      if (trialSucceeded()) {
        trialState.ResetForBalance(balanceStep);
      } else {
        trialState.ResetForBalance(-balanceStep);
      }
      balanceStep /= 2;

      aReflowInput.mFloatManager->PopState(&floatManagerState);
      continue;
    }

    // If we were attempting to balance, check whether the final iteration was
    // successful, and if not, back up by one step.
    if (balanceTarget.mOffset >= 0) {
      if (!trialState.mInset || trialSucceeded()) {
        break;
      }
      trialState.ResetForBalance(-1);

      aReflowInput.mFloatManager->PopState(&floatManagerState);
      continue;
    }

    // If we reach here, no balancing was required, so just exit; we don't
    // reset (pop) the floatManager state because this is the reflow we're
    // going to keep. So the saved state is just dropped.
    break;
  }  // End of text-wrap: balance retry loop

  // If the block direction is right-to-left, we need to update the bounds of
  // lines that were placed relative to mContainerSize during reflow, as
  // we typically do not know the true container size until we've reflowed all
  // its children. So we use a dummy mContainerSize during reflow (see
  // BlockReflowState's constructor) and then fix up the positions of the
  // lines here, once the final block size is known.
  //
  // Note that writing-mode:vertical-rl is the only case where the block
  // logical direction progresses in a negative physical direction, and
  // therefore block-dir coordinate conversion depends on knowing the width
  // of the coordinate space in order to translate between the logical and
  // physical origins.
  if (aReflowInput.GetWritingMode().IsVerticalRL()) {
    nsSize containerSize = aMetrics.PhysicalSize();
    nscoord deltaX = containerSize.width - trialState.mContainerWidth;
    if (deltaX != 0) {
      // We compute our lines and markers' overflow areas later in
      // ComputeOverflowAreas(), so we don't need to adjust their overflow areas
      // here.
      const nsPoint physicalDelta(deltaX, 0);
      for (auto& line : Lines()) {
        UpdateLineContainerSize(&line, containerSize);
      }
      trialState.mFcBounds.Clear();
      if (nsFrameList* floats = GetFloats()) {
        for (nsIFrame* f : *floats) {
          f->MovePositionBy(physicalDelta);
          ConsiderChildOverflow(trialState.mFcBounds, f);
        }
      }
      if (nsFrameList* markerList = GetOutsideMarkerList()) {
        for (nsIFrame* f : *markerList) {
          f->MovePositionBy(physicalDelta);
        }
      }
      if (nsFrameList* overflowContainers = GetOverflowContainers()) {
        trialState.mOcBounds.Clear();
        for (nsIFrame* f : *overflowContainers) {
          f->MovePositionBy(physicalDelta);
          ConsiderChildOverflow(trialState.mOcBounds, f);
        }
      }
    }
  }

  aMetrics.SetOverflowAreasToDesiredBounds();
  ComputeOverflowAreas(aMetrics.mOverflowAreas, aReflowInput.mStyleDisplay);
  // Factor overflow container child bounds into the overflow area
  aMetrics.mOverflowAreas.UnionWith(trialState.mOcBounds);
  // Factor pushed float child bounds into the overflow area
  aMetrics.mOverflowAreas.UnionWith(trialState.mFcBounds);

  // Let the absolutely positioned container reflow any absolutely positioned
  // child frames that need to be reflowed, e.g., elements with a percentage
  // based width/height
  // We want to do this under either of two conditions:
  //  1. If we didn't do the incremental reflow above.
  //  2. If our size changed.
  // Even though it's the padding edge that's the containing block, we
  // can use our rect (the border edge) since if the border style
  // changed, the reflow would have been targeted at us so we'd satisfy
  // condition 1.
  // XXX checking oldSize is bogus, there are various reasons we might have
  // reflowed but our size might not have been changed to what we
  // asked for (e.g., we ended up being pushed to a new page)
  // When WillReflowAgainForClearance is true, we will reflow again without
  // resetting the size. Because of this, we must not reflow our abs-pos
  // children in that situation --- what we think is our "new size" will not be
  // our real new size. This also happens to be more efficient.
  auto* absoluteContainer = GetAbsoluteContainingBlock();
  if (absoluteContainer && absoluteContainer->PrepareAbsoluteFrames(this)) {
    bool haveInterrupt = aPresContext->HasPendingInterrupt();
    if (aReflowInput.WillReflowAgainForClearance() || haveInterrupt) {
      // Make sure that when we reflow again we'll actually reflow all the abs
      // pos frames that might conceivably depend on our size (or all of them,
      // if we're dirty right now and interrupted; in that case we also need
      // to mark them all with NS_FRAME_IS_DIRTY).  Sadly, we can't do much
      // better than that, because we don't really know what our size will be,
      // and it might in fact not change on the followup reflow!
      if (haveInterrupt && HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
        absoluteContainer->MarkAllFramesDirty();
      } else {
        absoluteContainer->MarkSizeDependentFramesDirty();
      }
      if (haveInterrupt) {
        // We're not going to reflow absolute frames; make sure to account for
        // their existing overflow areas, which is usually a side effect of this
        // reflow.
        //
        // TODO(emilio): AbsoluteContainingBlock::Reflow already checks for
        // interrupt, can we just rely on it and unconditionally take the else
        // branch below? That's a bit more subtle / risky, since I don't see
        // what would reflow them in that case if they depended on our size.
        for (nsIFrame* kid : absoluteContainer->GetChildList()) {
          ConsiderChildOverflow(aMetrics.mOverflowAreas, kid);
        }
      }
    } else {
      // Mark frames that depend on changes we just made to this frame as dirty:
      // Now we can assume that the padding edge hasn't moved.
      // We need to reflow the absolutes if one of them depends on
      // its placeholder position, or the containing block size in a
      // direction in which the containing block size might have
      // changed.

      // XXX "width" and "height" in this block will become ISize and BSize
      // when AbsoluteContainingBlock is logicalized
      bool cbWidthChanged = aMetrics.Width() != oldSize.width;
      bool isRoot = !GetContent()->GetParent();
      // If isRoot and we have auto height, then we are the initial
      // containing block and the containing block height is the
      // viewport height, which can't change during incremental
      // reflow.
      bool cbHeightChanged =
          !(isRoot && NS_UNCONSTRAINEDSIZE == aReflowInput.ComputedHeight()) &&
          aMetrics.Height() != oldSize.height;

      AbsPosReflowFlags flags{AbsPosReflowFlag::AllowFragmentation};
      if (cbWidthChanged) {
        flags += AbsPosReflowFlag::CBWidthChanged;
      }
      if (cbHeightChanged) {
        flags += AbsPosReflowFlag::CBHeightChanged;
      }
      // Setup the line cursor here to optimize line searching for
      // calculating hypothetical position of absolutely-positioned
      // frames.
      SetupLineCursorForQuery();

      LogicalRect cbRect(wm, LogicalPoint(wm), aMetrics.Size(wm));
      cbRect.Deflate(wm, aReflowInput.ComputedLogicalBorder(wm).ApplySkipSides(
                             PreReflowBlockLevelLogicalSkipSides()));
      absoluteContainer->Reflow(
          this, aPresContext, aReflowInput, reflowStatus,
          cbRect.GetPhysicalRect(wm, aMetrics.PhysicalSize()), flags,
          &aMetrics.mOverflowAreas);
    }
  }

  FinishAndStoreOverflow(&aMetrics, aReflowInput.mStyleDisplay);

  aStatus = reflowStatus;

#ifdef DEBUG
  // Between when we drain pushed floats and when we complete reflow,
  // we're allowed to have multiple continuations of the same float on
  // our floats list, since a first-in-flow might get pushed to a later
  // continuation of its containing block.  But it's not permitted
  // outside that time.
  nsLayoutUtils::AssertNoDuplicateContinuations(
      this, GetChildList(FrameChildListID::Float));

  if (gNoisyReflow) {
    IndentBy(stdout, gNoiseIndent);
    fmt::print("{}: status={} metrics={} carriedMargin={}", ListTag().get(),
               ToString(aStatus), ToString(aMetrics.Size(wm)),
               aMetrics.mCarriedOutBEndMargin.Get());
    if (HasOverflowAreas()) {
      fmt::print(" overflow-ink={} overflow-scr={}",
                 ToString(aMetrics.InkOverflow()),
                 ToString(aMetrics.ScrollableOverflow()));
    }
    printf("\n");
  }

  if (gLameReflowMetrics) {
    PRTime end = PR_Now();

    int32_t ectc = nsLineBox::GetCtorCount();
    int32_t numLines = mLines.size();
    if (!numLines) {
      numLines = 1;
    }
    PRTime delta, perLineDelta, lines;
    lines = int64_t(numLines);
    delta = end - start;
    perLineDelta = delta / lines;

    ListTag(stdout);
    char buf[400];
    SprintfLiteral(buf,
                   ": %" PRId64 " elapsed (%" PRId64
                   " per line) (%d lines; %d new lines)",
                   delta, perLineDelta, numLines, ectc - ctc);
    printf("%s\n", buf);
  }
#endif
}

nsReflowStatus nsBlockFrame::TrialReflow(nsPresContext* aPresContext,
                                         ReflowOutput& aMetrics,
                                         const ReflowInput& aReflowInput,
                                         TrialReflowState& aTrialState) {
#ifdef DEBUG
  // Between when we drain pushed floats and when we complete reflow,
  // we're allowed to have multiple continuations of the same float on
  // our floats list, since a first-in-flow might get pushed to a later
  // continuation of its containing block.  But it's not permitted
  // outside that time.
  nsLayoutUtils::AssertNoDuplicateContinuations(
      this, GetChildList(FrameChildListID::Float));
#endif

  // ALWAYS drain overflow. We never want to leave the previnflow's
  // overflow lines hanging around; block reflow depends on the
  // overflow line lists being cleared out between reflow passes.
  DrainOverflowLines();

  // Clear any existing -webkit-line-clamp ellipsis if we're reflowing the
  // line-clamp root.
  if (IsLineClampRoot(this)) {
    ClearLineClampEllipsis();
  }

  bool blockStartMarginRoot, blockEndMarginRoot;
  IsMarginRoot(&blockStartMarginRoot, &blockEndMarginRoot);

  BlockReflowState state(aReflowInput, aPresContext, this, blockStartMarginRoot,
                         blockEndMarginRoot, aTrialState.mNeedFloatManager,
                         aTrialState.mConsumedBSize,
                         aTrialState.mEffectiveContentBoxBSize,
                         aTrialState.mInset);

  // Handle paginated overflow (see nsContainerFrame.h)
  nsReflowStatus ocStatus;
  if (GetPrevInFlow()) {
    ReflowOverflowContainerChildren(
        aPresContext, aReflowInput, aTrialState.mOcBounds,
        ReflowChildFlags::Default, ocStatus, DefaultChildFrameMerge,
        Some(state.ContainerSize()));
  }

  // Now that we're done cleaning up our overflow container lists, we can
  // give |state| its nsOverflowContinuationTracker.
  nsOverflowContinuationTracker tracker(this, false);
  state.mOverflowTracker = &tracker;

  // Drain & handle pushed floats
  DrainPushedFloats();
  ReflowPushedFloats(state, aTrialState.mFcBounds);

  // If we're not dirty (which means we'll mark everything dirty later)
  // and our inline-size has changed, mark the lines dirty that we need to
  // mark dirty for a resize reflow.
  if (!HasAnyStateBits(NS_FRAME_IS_DIRTY) && aReflowInput.IsIResize()) {
    PrepareResizeReflow(state);
  }

  // The same for percentage text-indent, except conditioned on the
  // parent resizing.
  if (!HasAnyStateBits(NS_FRAME_IS_DIRTY) && aReflowInput.mCBReflowInput &&
      aReflowInput.mCBReflowInput->IsIResize() &&
      StyleText()->mTextIndent.length.HasPercent() && !mLines.empty()) {
    mLines.front()->MarkDirty();
  }

  // For text-wrap:balance trials, we need to reflow all the inline lines even
  // if they're not all "dirty", but we don't need to reflow any block lines.
  if (aTrialState.mBalancing) {
    MarkAllInlineLinesDirty(this);
  } else {
    LazyMarkLinesDirty();
  }

  // Now reflow...
  aTrialState.mUsedOverflowWrap = ReflowDirtyLines(state);

  // If we have a next-in-flow, and that next-in-flow has pushed floats from
  // this frame from a previous iteration of reflow, then we should not return
  // a status with IsFullyComplete() equals to true, since we actually have
  // overflow, it's just already been handled.

  // NOTE: This really shouldn't happen, since we _should_ pull back our floats
  // and reflow them, but just in case it does, this is a safety precaution so
  // we don't end up with a placeholder pointing to frames that have already
  // been deleted as part of removing our next-in-flow.
  if (state.mReflowStatus.IsFullyComplete()) {
    nsBlockFrame* nif = static_cast<nsBlockFrame*>(GetNextInFlow());
    while (nif) {
      if (nif->HasPushedFloatsFromPrevContinuation()) {
        if (nif->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
          state.mReflowStatus.SetOverflowIncomplete();
        } else {
          state.mReflowStatus.SetIncomplete();
        }
        break;
      }

      nif = static_cast<nsBlockFrame*>(nif->GetNextInFlow());
    }
  }

  state.mReflowStatus.MergeCompletionStatusFrom(ocStatus);

  // If we end in a BR with clear and affected floats continue,
  // we need to continue, too.
  if (NS_UNCONSTRAINEDSIZE != aReflowInput.AvailableBSize() &&
      state.mReflowStatus.IsComplete() &&
      state.FloatManager()->ClearContinues(FindTrailingClear())) {
    state.mReflowStatus.SetIncomplete();
  }

  if (!state.mReflowStatus.IsFullyComplete()) {
    if (HasOverflowLines() || HasPushedFloats()) {
      state.mReflowStatus.SetNextInFlowNeedsReflow();
    }
  }

  // Place the ::marker's frame if it is placed next to a block child.
  //
  // According to the CSS2 spec, section 12.6.1, the ::marker's box
  // participates in the height calculation of the list-item box's
  // first line box.
  //
  // There are exactly two places a ::marker can be placed: near the
  // first or second line. It's only placed on the second line in a
  // rare case: an empty first line followed by a second line that
  // contains a block (example: <LI>\n<P>... ). This is where
  // the second case can happen.
  nsIFrame* outsideMarker = GetOutsideMarker();
  if (outsideMarker && !mLines.empty() &&
      (mLines.front()->IsBlock() ||
       (0 == mLines.front()->BSize() && mLines.front() != mLines.back() &&
        mLines.begin().next()->IsBlock()))) {
    // Reflow the ::marker's frame.
    ReflowOutput reflowOutput(aReflowInput);
    // XXX Use the entire line when we fix bug 25888.
    nsLayoutUtils::LinePosition position;
    WritingMode wm = aReflowInput.GetWritingMode();
    bool havePosition =
        nsLayoutUtils::GetFirstLinePosition(wm, this, &position);
    nscoord lineBStart =
        havePosition ? position.mBStart
                     : aReflowInput.ComputedLogicalBorderPadding(wm).BStart(wm);
    ReflowOutsideMarker(outsideMarker, state, reflowOutput, lineBStart);
    NS_ASSERTION(!MarkerIsEmpty(outsideMarker) || reflowOutput.BSize(wm) == 0,
                 "empty ::marker frame took up space");

    if (havePosition && !MarkerIsEmpty(outsideMarker)) {
      // We have some lines to align the ::marker with.

      // Doing the alignment using the baseline will also cater for
      // ::markers that are placed next to a child block (bug 92896)

      // Tall ::markers won't look particularly nice here...
      LogicalRect bbox =
          outsideMarker->GetLogicalRect(wm, reflowOutput.PhysicalSize());
      const auto baselineGroup = BaselineSharingGroup::First;
      Maybe<nscoord> result;
      if (MOZ_LIKELY(!wm.IsOrthogonalTo(outsideMarker->GetWritingMode()))) {
        result = outsideMarker->GetNaturalBaselineBOffset(
            wm, baselineGroup, BaselineExportContext::LineLayout);
      }
      const auto markerBaseline =
          result.valueOrFrom([bbox, wm, outsideMarker]() {
            return bbox.BSize(wm) +
                   outsideMarker->GetLogicalUsedMargin(wm).BEnd(wm);
          });
      bbox.BStart(wm) = position.mBaseline - markerBaseline;
      outsideMarker->SetRect(wm, bbox, reflowOutput.PhysicalSize());
    }
    // Otherwise just leave the ::marker where it is, up against our
    // block-start padding.
  }

  CheckFloats(state);

  // Compute our final size (for this trial layout)
  aTrialState.mBlockEndEdgeOfChildren =
      ComputeFinalSize(aReflowInput, state, aMetrics);
  aTrialState.mContainerWidth = state.ContainerSize().width;

  // Align content
  AlignContent(state, aMetrics, aTrialState.mBlockEndEdgeOfChildren);

  return state.mReflowStatus;
}

bool nsBlockFrame::CheckForCollapsedBEndMarginFromClearanceLine() {
  for (auto& line : Reversed(Lines())) {
    if (0 != line.BSize() || !line.CachedIsEmpty()) {
      return false;
    }
    if (line.HasClearance()) {
      return true;
    }
  }
  return false;
}

std::pair<nsBlockFrame*, nsLineBox*> FindLineClampTarget(
    nsBlockFrame* const aRootFrame, const nsBlockFrame* const aStopAtFrame,
    StyleLineClamp aLineNumber) {
  MOZ_ASSERT(aLineNumber > 0);

  nsLineBox* targetLine = nullptr;
  nsBlockFrame* targetFrame = nullptr;
  bool foundFollowingLine = false;

  LineClampLineIterator iter(aRootFrame, aStopAtFrame);

  while (nsLineBox* line = iter.GetCurrentLine()) {
    // Don't count a line that only has collapsible white space (as might exist
    // after calling e.g. getBoxQuads).
    if (line->IsEmpty()) {
      iter.Next();
      continue;
    }

    if (aLineNumber == 0) {
      // We already previously found our target line, and now we have
      // confirmed that there is another line after it.
      foundFollowingLine = true;
      break;
    }

    if (--aLineNumber == 0) {
      // This is our target line.  Continue looping to confirm that we
      // have another line after us.
      targetLine = line;
      targetFrame = iter.GetCurrentFrame();
    }

    iter.Next();
  }

  if (!foundFollowingLine) {
    MOZ_ASSERT(!aRootFrame->HasLineClampEllipsis(),
               "should have been removed earlier");
    return std::pair(nullptr, nullptr);
  }

  MOZ_ASSERT(targetLine);
  MOZ_ASSERT(targetFrame);

  // If targetFrame is not the same as the line-clamp root, any ellipsis on the
  // root should have been previously cleared.
  MOZ_ASSERT(targetFrame == aRootFrame || !aRootFrame->HasLineClampEllipsis(),
             "line-clamp target mismatch");

  return std::pair(targetFrame, targetLine);
}

nscoord nsBlockFrame::ApplyLineClamp(nscoord aContentBlockEndEdge) {
  auto* root = GetLineClampRoot();
  if (!root) {
    return aContentBlockEndEdge;
  }

  auto lineClamp = root->StyleDisplay()->mWebkitLineClamp;
  auto [target, line] = FindLineClampTarget(root, this, lineClamp);
  if (!line) {
    // The number of lines did not exceed the -webkit-line-clamp value.
    return aContentBlockEndEdge;
  }

  // Mark the line as having an ellipsis so that TextOverflow will render it.
  line->SetHasLineClampEllipsis();
  target->SetHasLineClampEllipsis(true);

  // Translate the b-end edge of the line up to aFrame's space.
  nscoord edge = line->BEnd();
  for (nsIFrame* f = target; f; f = f->GetParent()) {
    MOZ_ASSERT(f->IsBlockFrameOrSubclass(),
               "GetAsLineClampDescendant guarantees this");
    if (f != target) {
      static_cast<nsBlockFrame*>(f)->SetHasLineClampEllipsisDescendant(true);
    }
    if (f == this) {
      break;
    }
    if (f == root) {
      // The clamped line is not in our subtree.
      return aContentBlockEndEdge;
    }
    const auto wm = f->GetWritingMode();
    const nsSize parentSize = f->GetParent()->GetSize();
    edge = f->GetLogicalRect(parentSize).BEnd(wm);
  }

  return edge;
}

nscoord nsBlockFrame::ComputeFinalSize(const ReflowInput& aReflowInput,
                                       BlockReflowState& aState,
                                       ReflowOutput& aMetrics) {
  WritingMode wm = aState.mReflowInput.GetWritingMode();
  const LogicalMargin& borderPadding = aState.BorderPadding();
#ifdef NOISY_FINAL_SIZE
  ListTag(stdout);
  printf(": mBCoord=%d mIsBEndMarginRoot=%s mPrevBEndMargin=%d bp=%d,%d\n",
         aState.mBCoord, aState.mFlags.mIsBEndMarginRoot ? "yes" : "no",
         aState.mPrevBEndMargin.get(), borderPadding.BStart(wm),
         borderPadding.BEnd(wm));
#endif

  // Compute final inline size
  LogicalSize finalSize(wm);
  finalSize.ISize(wm) =
      NSCoordSaturatingAdd(NSCoordSaturatingAdd(borderPadding.IStart(wm),
                                                aReflowInput.ComputedISize()),
                           borderPadding.IEnd(wm));

  // Return block-end margin information
  // rbs says he hit this assertion occasionally (see bug 86947), so
  // just set the margin to zero and we'll figure out why later
  // NS_ASSERTION(aMetrics.mCarriedOutBEndMargin.IsZero(),
  //             "someone else set the margin");
  nscoord nonCarriedOutBDirMargin = 0;
  if (!aState.mFlags.mIsBEndMarginRoot) {
    // Apply rule from CSS 2.1 section 8.3.1. If we have some empty
    // line with clearance and a non-zero block-start margin and all
    // subsequent lines are empty, then we do not allow our children's
    // carried out block-end margin to be carried out of us and collapse
    // with our own block-end margin.
    if (CheckForCollapsedBEndMarginFromClearanceLine()) {
      // Convert the children's carried out margin to something that
      // we will include in our height
      nonCarriedOutBDirMargin = aState.mPrevBEndMargin.Get();
      aState.mPrevBEndMargin.Zero();
    }
    aMetrics.mCarriedOutBEndMargin = aState.mPrevBEndMargin;
  } else {
    aMetrics.mCarriedOutBEndMargin.Zero();
  }

  nscoord blockEndEdgeOfChildren = aState.mBCoord + nonCarriedOutBDirMargin;
  // Shrink wrap our height around our contents.
  if (aState.mFlags.mIsBEndMarginRoot ||
      NS_UNCONSTRAINEDSIZE != aReflowInput.ComputedBSize()) {
    // When we are a block-end-margin root make sure that our last
    // child's block-end margin is fully applied. We also do this when
    // we have a computed height, since in that case the carried out
    // margin is not going to be applied anywhere, so we should note it
    // here to be included in the overflow area.
    // Apply the margin only if there's space for it.
    if (blockEndEdgeOfChildren < aState.mReflowInput.AvailableBSize()) {
      // Truncate block-end margin if it doesn't fit to our available BSize.
      blockEndEdgeOfChildren =
          std::min(blockEndEdgeOfChildren + aState.mPrevBEndMargin.Get(),
                   aState.mReflowInput.AvailableBSize());
    }
  }
  if (aState.mFlags.mBlockNeedsFloatManager) {
    // Include the float manager's state to properly account for the
    // block-end margin of any floated elements; e.g., inside a table cell.
    //
    // Note: The block coordinate returned by ClearFloats is always greater than
    // or equal to blockEndEdgeOfChildren.
    std::tie(blockEndEdgeOfChildren, std::ignore) =
        aState.ClearFloats(blockEndEdgeOfChildren, UsedClear::Both);
  }

  // undo cached alignment shift for sizing purposes
  // (we used shifted positions because the float manager uses them)
  blockEndEdgeOfChildren -= aState.mAlignContentShift;
  aState.UndoAlignContentShift();

  if (NS_UNCONSTRAINEDSIZE != aReflowInput.ComputedBSize()) {
    // Note: We don't use blockEndEdgeOfChildren because it includes the
    // previous margin.
    const nscoord contentBSizeWithBStartBP =
        aState.mBCoord + nonCarriedOutBDirMargin;

    // We don't care about ApplyLineClamp's return value (the line-clamped
    // content BSize) in this explicit-BSize codepath, but we do still need to
    // call ApplyLineClamp for ellipsis markers to be placed as-needed.
    ApplyLineClamp(contentBSizeWithBStartBP);

    finalSize.BSize(wm) = ComputeFinalBSize(aState, contentBSizeWithBStartBP);

    // If the content block-size is larger than the effective computed
    // block-size, we extend the block-size to contain all the content.
    // https://drafts.csswg.org/css-sizing-4/#aspect-ratio-minimum
    if (aReflowInput.ShouldApplyAutomaticMinimumOnBlockAxis()) {
      // Note: finalSize.BSize(wm) is the border-box size, so we compare it with
      // the content's block-size plus our border and padding..
      finalSize.BSize(wm) =
          std::max(finalSize.BSize(wm),
                   contentBSizeWithBStartBP + borderPadding.BEnd(wm));

      // The size should be capped by its maximum block size.
      if (aReflowInput.ComputedMaxBSize() != NS_UNCONSTRAINEDSIZE) {
        finalSize.BSize(wm) =
            std::min(finalSize.BSize(wm), aReflowInput.ComputedMaxBSize() +
                                              borderPadding.BStartEnd(wm));
      }
    }

    // Don't carry out a block-end margin when our BSize is fixed.
    //
    // Note: this also includes the case that aReflowInput.ComputedBSize() is
    // calculated from aspect-ratio. i.e. Don't carry out block margin-end if it
    // is replaced by the block size from aspect-ratio and inline size.
    aMetrics.mCarriedOutBEndMargin.Zero();
  } else if (Maybe<nscoord> containBSize = ContainIntrinsicBSize(
                 IsComboboxControlFrame() ? aReflowInput.GetLineHeight() : 0)) {
    // If we're size-containing in block axis and we don't have a specified
    // block size, then our final size should actually be computed from only
    // our border, padding and contain-intrinsic-block-size, ignoring the
    // actual contents. Hence this case is a simplified version of the case
    // below.
    nscoord contentBSize = *containBSize;
    nscoord autoBSize =
        aReflowInput.ApplyMinMaxBSize(contentBSize, aState.mConsumedBSize);
    aMetrics.mCarriedOutBEndMargin.Zero();
    autoBSize += borderPadding.BStartEnd(wm);
    finalSize.BSize(wm) = autoBSize;
  } else if (aState.mReflowStatus.IsInlineBreakBefore()) {
    // Our parent is expected to push this frame to the next page/column so
    // what size we set here doesn't really matter.
    finalSize.BSize(wm) = aReflowInput.AvailableBSize();
  } else if (aState.mReflowStatus.IsComplete()) {
    const nscoord lineClampedContentBlockEndEdge =
        ApplyLineClamp(blockEndEdgeOfChildren);

    const nscoord bpBStart = borderPadding.BStart(wm);
    const nscoord contentBSize = blockEndEdgeOfChildren - bpBStart;
    const nscoord lineClampedContentBSize =
        lineClampedContentBlockEndEdge - bpBStart;

    const nscoord autoBSize = aReflowInput.ApplyMinMaxBSize(
        lineClampedContentBSize, aState.mConsumedBSize);
    if (autoBSize != contentBSize) {
      // Our min-block-size, max-block-size, or -webkit-line-clamp value made
      // our bsize change.  Don't carry out our kids' block-end margins.
      aMetrics.mCarriedOutBEndMargin.Zero();
    }
    nscoord bSize = autoBSize + borderPadding.BStartEnd(wm);
    if (MOZ_UNLIKELY(autoBSize > contentBSize &&
                     bSize > aReflowInput.AvailableBSize() &&
                     aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE)) {
      // Applying `min-size` made us overflow our available size.
      // Clamp it and report that we're Incomplete, or BreakBefore if we have
      // 'break-inside: avoid' that is applicable.
      bSize = aReflowInput.AvailableBSize();
      if (ShouldAvoidBreakInside(aReflowInput)) {
        aState.mReflowStatus.SetInlineLineBreakBeforeAndReset();
      } else {
        aState.mReflowStatus.SetIncomplete();
      }
    }
    finalSize.BSize(wm) = bSize;
  } else {
    NS_ASSERTION(aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE,
                 "Shouldn't be incomplete if availableBSize is UNCONSTRAINED.");
    nscoord bSize = std::max(aState.mBCoord, aReflowInput.AvailableBSize());
    if (aReflowInput.AvailableBSize() == NS_UNCONSTRAINEDSIZE) {
      // This should never happen, but it does. See bug 414255
      bSize = aState.mBCoord;
    }
    const nscoord maxBSize = aReflowInput.ComputedMaxBSize();
    if (maxBSize != NS_UNCONSTRAINEDSIZE &&
        aState.mConsumedBSize + bSize - borderPadding.BStart(wm) > maxBSize) {
      // Compute this fragment's block-size, with the max-block-size
      // constraint taken into consideration.
      const nscoord clampedBSizeWithoutEndBP =
          std::max(0, maxBSize - aState.mConsumedBSize) +
          borderPadding.BStart(wm);
      const nscoord clampedBSize =
          clampedBSizeWithoutEndBP + borderPadding.BEnd(wm);
      if (clampedBSize <= aReflowInput.AvailableBSize()) {
        // We actually fit after applying `max-size` so we should be
        // Overflow-Incomplete instead.
        bSize = clampedBSize;
        aState.mReflowStatus.SetOverflowIncomplete();
      } else {
        // We cannot fit after applying `max-size` with our block-end BP, so
        // we should draw it in our next continuation.
        bSize = clampedBSizeWithoutEndBP;
      }
    }
    finalSize.BSize(wm) = bSize;
  }

  if (IsTrueOverflowContainer()) {
    if (aState.mReflowStatus.IsIncomplete()) {
      // Overflow containers can only be overflow complete.
      // Note that auto height overflow containers have no normal children
      NS_ASSERTION(finalSize.BSize(wm) == 0,
                   "overflow containers must be zero-block-size");
      aState.mReflowStatus.SetOverflowIncomplete();
    }
  } else if (aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE &&
             !aState.mReflowStatus.IsInlineBreakBefore() &&
             aState.mReflowStatus.IsComplete()) {
    // Currently only used for grid items, but could be used in other contexts.
    // The FragStretchBSizeProperty is our expected non-fragmented block-size
    // we should stretch to (for align-self:stretch etc).  In some fragmentation
    // cases though, the last fragment (this frame since we're complete), needs
    // to have extra size applied because earlier fragments consumed too much of
    // our computed size due to overflowing their containing block.  (E.g. this
    // ensures we fill the last row when a multi-row grid item is fragmented).
    bool found;
    nscoord bSize = GetProperty(FragStretchBSizeProperty(), &found);
    if (found) {
      finalSize.BSize(wm) = std::max(bSize, finalSize.BSize(wm));
    }
  }

  // Clamp the content size to fit within the margin-box clamp size, if any.
  if (MOZ_UNLIKELY(aReflowInput.mComputeSizeFlags.contains(
          ComputeSizeFlag::BClampMarginBoxMinSize)) &&
      aState.mReflowStatus.IsComplete()) {
    bool found;
    nscoord cbSize = GetProperty(BClampMarginBoxMinSizeProperty(), &found);
    if (found) {
      auto marginBoxBSize =
          finalSize.BSize(wm) +
          aReflowInput.ComputedLogicalMargin(wm).BStartEnd(wm);
      auto overflow = marginBoxBSize - cbSize;
      if (overflow > 0) {
        auto contentBSize = finalSize.BSize(wm) - borderPadding.BStartEnd(wm);
        auto newContentBSize = std::max(nscoord(0), contentBSize - overflow);
        // XXXmats deal with percentages better somehow?
        finalSize.BSize(wm) -= contentBSize - newContentBSize;
      }
    }
  }

  // Screen out negative block sizes --- can happen due to integer overflows :-(
  finalSize.BSize(wm) = std::max(0, finalSize.BSize(wm));
  aMetrics.SetSize(wm, finalSize);

  return blockEndEdgeOfChildren;
}

void nsBlockFrame::AlignContent(BlockReflowState& aState,
                                ReflowOutput& aMetrics,
                                nscoord aBEndEdgeOfChildren) {
  const StyleAlignFlags originalAlignment = EffectiveAlignContent();
  const auto alignment = originalAlignment & ~StyleAlignFlags::FLAG_BITS;

  // Short circuit
  const bool isCentered = alignment == StyleAlignFlags::CENTER ||
                          alignment == StyleAlignFlags::SPACE_AROUND ||
                          alignment == StyleAlignFlags::SPACE_EVENLY;
  const bool isEndAlign = alignment == StyleAlignFlags::END ||
                          alignment == StyleAlignFlags::FLEX_END ||
                          alignment == StyleAlignFlags::LAST_BASELINE;
  if (!isEndAlign && !isCentered && !aState.mAlignContentShift) {
    // desired shift = 0, no cached shift to undo
    return;
  }

  // NOTE: ComputeFinalSize already called aState.UndoAlignContentShift(),
  //       so metrics no longer include cached shift.
  // NOTE: Content is currently positioned at cached shift
  // NOTE: Content has been fragmented against 0-shift assumption.

  // Calculate shift
  nscoord shift = 0;
  WritingMode wm = aState.mReflowInput.GetWritingMode();
  if ((isCentered || isEndAlign) && !mLines.empty() &&
      aState.mReflowStatus.IsFullyComplete() && !GetPrevInFlow()) {
    nscoord availB = aState.mReflowInput.AvailableBSize();
    nscoord endB =
        aMetrics.Size(wm).BSize(wm) - aState.BorderPadding().BEnd(wm);
    shift = std::min(availB, endB) - aBEndEdgeOfChildren;

    // note: these measures all include start BP, so it subtracts out
    if (!(originalAlignment & StyleAlignFlags::UNSAFE)) {
      shift = std::max(0, shift);
    }
    if (isCentered) {
      shift = shift / 2;
    }
  }
  // else: zero shift if start-aligned or if fragmented

  nscoord delta = shift - aState.mAlignContentShift;
  if (delta) {
    // Shift children
    LogicalPoint translation(wm, 0, delta);
    for (nsLineBox& line : Lines()) {
      SlideLine(aState, &line, delta);
    }
    for (nsIFrame* kid : GetChildList(FrameChildListID::Float)) {
      kid->MovePositionBy(wm, translation);
    }
    nsIFrame* outsideMarker = GetOutsideMarker();
    if (outsideMarker && !mLines.empty()) {
      outsideMarker->MovePositionBy(wm, translation);
    }
  }

  if (shift) {
    // Cache shift
    SetProperty(AlignContentShift(), shift);
  } else {
    RemoveProperty(AlignContentShift());
  }
}

void nsBlockFrame::ComputeOverflowAreas(OverflowAreas& aOverflowAreas,
                                        const nsStyleDisplay* aDisplay) const {
  // XXX_perf: This can be done incrementally.  It is currently one of
  // the things that makes incremental reflow O(N^2).
  auto overflowClipAxes = ShouldApplyOverflowClipping(aDisplay);
  auto overflowClipMargin =
      OverflowClipMargin(overflowClipAxes, /* aAllowNegative = */ false);
  if (overflowClipAxes == kPhysicalAxesBoth && overflowClipMargin.IsAllZero()) {
    return;
  }

  // We rely here on our caller having called SetOverflowAreasToDesiredBounds().
  const nsRect frameBounds = aOverflowAreas.ScrollableOverflow();

  const auto wm = GetWritingMode();
  const auto borderPadding =
      GetLogicalUsedBorderAndPadding(wm).GetPhysicalMargin(wm);
  // Compute content-box by subtracting borderPadding off of frame rect.
  // This gives us a reasonable starting-rect for the child-rect-unioning
  // below, which we can then inflate by our padding (without needing to
  // worry about having double-counted our padding or anything).
  auto frameContentBounds = frameBounds;
  frameContentBounds.Deflate(borderPadding);
  // Margin rects (Zero-area rects included) of in-flow children (And floats,
  // as discussed later) are unioned (starting with the scroller's own
  // content-box), then inflated by the scroll container's padding...
  auto inFlowChildBounds = frameContentBounds;
  // ... While scrollable overflow rects contributed from further descendants
  // (Regardless of if they're in-flow or out-of-flow) are unioned separately
  // and their union does not get inflated by the scroll container's padding.
  auto inFlowScrollableOverflow = frameContentBounds;

  for (const auto& line : Lines()) {
    aOverflowAreas.InkOverflow() =
        aOverflowAreas.InkOverflow().Union(line.InkOverflowRect());
    if (aDisplay->IsContainLayout()) {
      // If we have layout containment, we should only consider our child's
      // ink overflow, leaving the scrollable regions of the parent
      // unaffected.
      // Note: Any overflow must be treated as ink overflow (As per
      // https://drafts.csswg.org/css-contain/#containment-layout part 3).
      // However, by unioning the children's ink overflow, we've already
      // incorporated its scrollable overflow, since scrollable overflow
      // is a subset of ink overflow.
      continue;
    }

    if (line.IsInline()) {
      // This is the maximum contribution for inline line-participating frames -
      // See `GetLineFrameInFlowBounds`.
      inFlowChildBounds =
          inFlowChildBounds.UnionEdges(line.GetPhysicalBounds());
    }
    auto lineInFlowChildBounds = line.GetInFlowChildBounds();
    if (lineInFlowChildBounds) {
      inFlowChildBounds = inFlowChildBounds.UnionEdges(*lineInFlowChildBounds);
    }
    inFlowScrollableOverflow =
        inFlowScrollableOverflow.Union(line.ScrollableOverflowRect());
  }

  if (Style()->GetPseudoType() == PseudoStyleType::MozScrolledContent) {
    // Padding inflation only applies to scrolled containers.
    const auto paddingInflatedOverflow =
        ComputePaddingInflatedScrollableOverflow(inFlowChildBounds);
    aOverflowAreas.UnionAllWith(paddingInflatedOverflow);

    // For backwards compatibility reasons, we don't allow scrollable overflow
    // on the block axis on inline inputs.
    if (IsSingleLineTextInput()) {
      overflowClipAxes +=
          wm.IsVertical() ? PhysicalAxis::Horizontal : PhysicalAxis::Vertical;
    }
  }
  // Note: we're using UnionAllWith so as to maintain the invariant of
  // ink overflow being a superset of scrollable overflow.
  aOverflowAreas.UnionAllWith(inFlowScrollableOverflow);

  // Factor an outside ::marker in; normally the ::marker will be factored
  // into the line-box's overflow areas. However, if the line is a block
  // line then it won't; if there are no lines, it won't. So just
  // factor it in anyway (it can't hurt if it was already done).
  // XXXldb Can we just fix GetOverflowArea instead?
  if (nsIFrame* outsideMarker = GetOutsideMarker()) {
    aOverflowAreas.UnionAllWith(outsideMarker->GetRect());
  }

  if (!overflowClipAxes.isEmpty()) {
    aOverflowAreas.ApplyClipping(frameBounds, overflowClipAxes,
                                 overflowClipMargin);
  }

#ifdef NOISY_OVERFLOW_AREAS
  printf("%s: InkOverflowArea=%s, ScrollableOverflowArea=%s\n", ListTag().get(),
         ToString(aOverflowAreas.InkOverflow()).c_str(),
         ToString(aOverflowAreas.ScrollableOverflow()).c_str());
#endif
}

// Text control padding handling is kinda disgusting.
static bool RestrictPaddingInflationInInline(const nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame);
  if (aFrame->Style()->GetPseudoType() != PseudoStyleType::MozScrolledContent) {
    // This can only happen when computing scrollable overflow for overflow:
    // visible frames (for scroll{Width,Height}).
    return false;
  }
  // If we're `input` or `textarea`, our grandparent element must be the text
  // control element that we can query.
  nsTextControlFrame* textControl = do_QueryFrame(aFrame->GetParent());
  if (MOZ_LIKELY(!textControl)) {
    return false;
  }

  // We implement `textarea` as a special case of a div, but based on
  // web-platform-tests, different rules apply for it - namely, no inline
  // padding inflation. See
  // `textarea-padding-iend-overlaps-content-001.tentative.html`.
  if (!textControl->IsTextArea()) {
    return false;
  }
  return true;
}

nsRect nsBlockFrame::ComputePaddingInflatedScrollableOverflow(
    const nsRect& aInFlowChildBounds) const {
  auto result = aInFlowChildBounds;
  const auto wm = GetWritingMode();
  auto padding = GetLogicalUsedPadding(wm);
  if (RestrictPaddingInflationInInline(this)) {
    padding.IStart(wm) = padding.IEnd(wm) = 0;
  }
  result.Inflate(padding.GetPhysicalMargin(wm));
  return result;
}

Maybe<nsRect> nsBlockFrame::GetLineFrameInFlowBounds(
    const nsLineBox& aLine, const nsIFrame& aLineChildFrame,
    bool aConsiderPositiveMargins) const {
  MOZ_ASSERT(aLineChildFrame.GetParent() == this,
             "Line's frame doesn't belong to this block frame?");
  // Line participants are considered in-flow for content within the line
  // bounds, which should be accounted for from the line bounds. This is
  // consistent with e.g. inline element's `margin-bottom` not affecting the
  // placement of the next line.
  if (aLineChildFrame.IsPlaceholderFrame() ||
      aLineChildFrame.IsLineParticipant()) {
    return Nothing{};
  }
  if (aLine.IsInline()) {
    return Some(GetNormalMarginRect(aLineChildFrame, aConsiderPositiveMargins));
  }
  const auto wm = GetWritingMode();
  auto rect = aLineChildFrame.GetRectRelativeToSelf();
  // Special handling is required for boxes of zero block size, which carry
  // out margin collapsing with themselves. We end up "rewinding" the line
  // position after carrying out the block start margin. This is not reflected
  // in the zero-sized frame's own frame-position.
  const auto linePoint = aLine.GetPhysicalBounds().TopLeft();
  const auto normalPosition = aLineChildFrame.GetLogicalSize(wm).BSize(wm) == 0
                                  ? linePoint
                                  : aLineChildFrame.GetNormalPosition();
  // Ensure we use the margin we actually carried out.
  nsMargin margin;
  if (aConsiderPositiveMargins) {
    auto logicalMargin = aLineChildFrame.GetLogicalUsedMargin(wm);
    logicalMargin.BEnd(wm) = aLine.GetCarriedOutBEndMargin().Get();
    margin = logicalMargin.GetPhysicalMargin(wm).ApplySkipSides(
        aLineChildFrame.GetSkipSides());
  } else {
    margin = aLineChildFrame.GetUsedMargin().ApplySkipSides(
        aLineChildFrame.GetSkipSides());
    margin.EnsureAtMost(nsMargin());
  }
  rect.Inflate(margin);
  return Some(rect + normalPosition);
}

void nsBlockFrame::UnionChildOverflow(OverflowAreas& aOverflowAreas,
                                      bool aAsIfScrolled) {
  // We need to update the overflow areas of lines manually, as they
  // get cached and re-used otherwise. Lines aren't exposed as normal
  // frame children, so calling UnionChildOverflow alone will end up
  // using the old cached values.
  const auto wm = GetWritingMode();

  // ButtonControlFrame elements don't support scrolling, and some like
  // comboboxes intentionally ignore padding to place their inner elements like
  // the button, so to preserve behavior of stuff like the scroll{Width,Height}
  // APIs, forcefully ignore aAsIfScrolled there.
  aAsIfScrolled = aAsIfScrolled && !IsButtonControlFrame();

  // Overflow area computed here should agree with one computed in
  // `ComputeOverflowAreas` (see bug 1800939 and bug 1800719). So the
  // documentation in that function applies here as well.
  const bool isScrolled =
      aAsIfScrolled ||
      Style()->GetPseudoType() == PseudoStyleType::MozScrolledContent;
  // Note that we don't add line in-flow margins if we're not a BFC (which can
  // happen only for overflow: visible), so that we don't incorrectly account
  // for margins that otherwise collapse through, see bug 1936156. Note that
  // ::-moz-scrolled-content is always a BFC (see `AnonymousBoxIsBFC`).
  const bool considerPositiveMarginsForInFlowChildBounds =
      isScrolled && HasAnyStateBits(NS_BLOCK_BFC);

  // Relying on aOverflowAreas having been set to frame border rect (if
  // aAsIfScrolled is false), or padding rect (if true).
  auto frameContentBounds = aOverflowAreas.ScrollableOverflow();
  frameContentBounds.Deflate((aAsIfScrolled
                                  ? GetLogicalUsedPadding(wm)
                                  : GetLogicalUsedBorderAndPadding(wm))
                                 .GetPhysicalMargin(wm));
  // We need to take in-flow children's margin rect into account, and inflate
  // it by the padding.
  auto inFlowChildBounds = frameContentBounds;
  auto inFlowScrollableOverflow = frameContentBounds;

  const auto inkOverflowOnly =
      !aAsIfScrolled && StyleDisplay()->IsContainLayout();

  for (auto& line : Lines()) {
    nsRect bounds = line.GetPhysicalBounds();
    OverflowAreas lineAreas(bounds, bounds);

    for (nsIFrame* lineFrame : line.ChildFrames()) {
      // Ensure this is called for each frame in the line
      ConsiderChildOverflow(lineAreas, lineFrame, aAsIfScrolled);

      if (inkOverflowOnly || !isScrolled) {
        continue;
      }

      if (auto lineFrameBounds = GetLineFrameInFlowBounds(
              line, *lineFrame, considerPositiveMarginsForInFlowChildBounds)) {
        inFlowChildBounds = inFlowChildBounds.UnionEdges(*lineFrameBounds);
      }
    }

    // Consider the overflow areas of the floats attached to the line as well
    if (line.HasFloats()) {
      for (nsIFrame* f : line.Floats()) {
        ConsiderChildOverflow(lineAreas, f, aAsIfScrolled);
        if (inkOverflowOnly || !isScrolled) {
          continue;
        }
        auto rect = GetNormalMarginRect(
            *f, considerPositiveMarginsForInFlowChildBounds);
        inFlowChildBounds = inFlowChildBounds.UnionEdges(rect);
      }
    }

    if (!aAsIfScrolled) {
      line.SetOverflowAreas(lineAreas);
    }
    aOverflowAreas.InkOverflow() =
        aOverflowAreas.InkOverflow().Union(lineAreas.InkOverflow());
    if (!inkOverflowOnly) {
      inFlowScrollableOverflow =
          inFlowScrollableOverflow.Union(lineAreas.ScrollableOverflow());
    }
  }

  if (isScrolled) {
    const auto paddingInflatedOverflow =
        ComputePaddingInflatedScrollableOverflow(inFlowChildBounds);
    aOverflowAreas.UnionAllWith(paddingInflatedOverflow);
  }
  aOverflowAreas.UnionAllWith(inFlowScrollableOverflow);

  // Union with child frames, skipping the principal and float lists
  // since we already handled those using the line boxes.
  nsLayoutUtils::UnionChildOverflow(
      this, aOverflowAreas,
      {FrameChildListID::Principal, FrameChildListID::Float});
}

bool nsBlockFrame::ComputeCustomOverflow(OverflowAreas& aOverflowAreas) {
  // Line cursor invariants depend on the overflow areas of the lines, so
  // we must clear the line cursor since those areas may have changed.
  ClearLineCursors();
  return nsContainerFrame::ComputeCustomOverflow(aOverflowAreas);
}

void nsBlockFrame::LazyMarkLinesDirty() {
  if (HasAnyStateBits(NS_BLOCK_LOOK_FOR_DIRTY_FRAMES)) {
    for (LineIterator line = LinesBegin(), line_end = LinesEnd();
         line != line_end; ++line) {
      for (nsIFrame* lineFrame : line->ChildFrames()) {
        if (lineFrame->IsSubtreeDirty()) {
          // NOTE:  MarkLineDirty does more than just marking the line dirty.
          MarkLineDirty(line, &mLines);
          break;
        }
      }
    }
    RemoveStateBits(NS_BLOCK_LOOK_FOR_DIRTY_FRAMES);
  }
}

void nsBlockFrame::MarkLineDirty(LineIterator aLine,
                                 const nsLineList* aLineList) {
  // Mark aLine dirty
  aLine->MarkDirty();
  aLine->SetInvalidateTextRuns(true);
#ifdef DEBUG
  if (gNoisyReflow) {
    IndentBy(stdout, gNoiseIndent);
    ListTag(stdout);
    printf(": mark line %p dirty\n", static_cast<void*>(aLine.get()));
  }
#endif

  // Mark previous line dirty if it's an inline line so that it can
  // maybe pullup something from the line just affected.
  // XXX We don't need to do this if aPrevLine ends in a break-after...
  if (aLine != aLineList->front() && aLine->IsInline() &&
      aLine.prev()->IsInline()) {
    aLine.prev()->MarkDirty();
    aLine.prev()->SetInvalidateTextRuns(true);
#ifdef DEBUG
    if (gNoisyReflow) {
      IndentBy(stdout, gNoiseIndent);
      ListTag(stdout);
      printf(": mark prev-line %p dirty\n",
             static_cast<void*>(aLine.prev().get()));
    }
#endif
  }
}

/**
 * Test whether lines are certain to be aligned left so that we can make
 * resizing optimizations
 */
static inline bool IsAlignedLeft(StyleTextAlign aAlignment,
                                 StyleDirection aDirection,
                                 StyleUnicodeBidi aUnicodeBidi,
                                 nsIFrame* aFrame) {
  return aFrame->IsInSVGTextSubtree() || StyleTextAlign::Left == aAlignment ||
         (((StyleTextAlign::Start == aAlignment &&
            StyleDirection::Ltr == aDirection) ||
           (StyleTextAlign::End == aAlignment &&
            StyleDirection::Rtl == aDirection)) &&
          aUnicodeBidi != StyleUnicodeBidi::Plaintext);
}

void nsBlockFrame::PrepareResizeReflow(BlockReflowState& aState) {
  // See if we can try and avoid marking all the lines as dirty
  // FIXME(emilio): This should be writing-mode aware, I guess.
  bool tryAndSkipLines =
      // The left content-edge must be a constant distance from the left
      // border-edge.
      !StylePadding()->mPadding.Get(eSideLeft).HasPercent();

#ifdef DEBUG
  if (gDisableResizeOpt) {
    tryAndSkipLines = false;
  }
  if (gNoisyReflow) {
    if (!tryAndSkipLines) {
      IndentBy(stdout, gNoiseIndent);
      ListTag(stdout);
      printf(": marking all lines dirty: availISize=%d\n",
             aState.mReflowInput.AvailableISize());
    }
  }
#endif

  if (tryAndSkipLines) {
    WritingMode wm = aState.mReflowInput.GetWritingMode();
    nscoord newAvailISize =
        aState.mReflowInput.ComputedLogicalBorderPadding(wm).IStart(wm) +
        aState.mReflowInput.ComputedISize();

#ifdef DEBUG
    if (gNoisyReflow) {
      IndentBy(stdout, gNoiseIndent);
      ListTag(stdout);
      printf(": trying to avoid marking all lines dirty\n");
    }
#endif

    for (LineIterator line = LinesBegin(), line_end = LinesEnd();
         line != line_end; ++line) {
      // We let child blocks make their own decisions the same
      // way we are here.
      bool isLastLine = line == mLines.back() && !GetNextInFlow();
      if (line->IsBlock() || line->HasFloats() ||
          (!isLastLine && !line->HasForcedLineBreakAfter()) ||
          ((isLastLine || !line->IsLineWrapped())) ||
          line->ResizeReflowOptimizationDisabled() ||
          line->IsImpactedByFloat() || (line->IEnd() > newAvailISize)) {
        line->MarkDirty();
      }

#ifdef REALLY_NOISY_REFLOW
      if (!line->IsBlock()) {
        printf("PrepareResizeReflow thinks line %p is %simpacted by floats\n",
               line.get(), line->IsImpactedByFloat() ? "" : "not ");
      }
#endif
#ifdef DEBUG
      if (gNoisyReflow && !line->IsDirty()) {
        IndentBy(stdout, gNoiseIndent + 1);
        printf(
            "skipped: line=%p next=%p %s %s%s%s clearTypeBefore/After=%s/%s "
            "xmost=%d\n",
            static_cast<void*>(line.get()),
            static_cast<void*>(
                (line.next() != LinesEnd() ? line.next().get() : nullptr)),
            line->IsBlock() ? "block" : "inline",
            line->HasForcedLineBreakAfter() ? "has-break-after " : "",
            line->HasFloats() ? "has-floats " : "",
            line->IsImpactedByFloat() ? "impacted " : "",
            line->UsedClearToString(line->FloatClearTypeBefore()),
            line->UsedClearToString(line->FloatClearTypeAfter()), line->IEnd());
      }
#endif
    }
  } else {
    // Mark everything dirty
    for (auto& line : Lines()) {
      line.MarkDirty();
    }
  }
}

//----------------------------------------

/**
 * Propagate reflow "damage" from from earlier lines to the current
 * line.  The reflow damage comes from the following sources:
 *  1. The regions of float damage remembered during reflow.
 *  2. The combination of nonzero |aDeltaBCoord| and any impact by a
 *     float, either the previous reflow or now.
 *
 * When entering this function, |aLine| is still at its old position and
 * |aDeltaBCoord| indicates how much it will later be slid (assuming it
 * doesn't get marked dirty and reflowed entirely).
 */
void nsBlockFrame::PropagateFloatDamage(BlockReflowState& aState,
                                        nsLineBox* aLine,
                                        nscoord aDeltaBCoord) {
  nsFloatManager* floatManager = aState.FloatManager();
  NS_ASSERTION(
      (aState.mReflowInput.mParentReflowInput &&
       aState.mReflowInput.mParentReflowInput->mFloatManager == floatManager) ||
          aState.mReflowInput.mBlockDelta == 0,
      "Bad block delta passed in");

  // Check to see if there are any floats; if there aren't, there can't
  // be any float damage
  if (!floatManager->HasAnyFloats()) {
    return;
  }

  // Check the damage region recorded in the float damage.
  if (floatManager->HasFloatDamage()) {
    // Need to check mBounds *and* mCombinedArea to find intersections
    // with aLine's floats
    nscoord lineBCoordBefore = aLine->BStart() + aDeltaBCoord;
    nscoord lineBCoordAfter = lineBCoordBefore + aLine->BSize();
    // Scrollable overflow should be sufficient for things that affect
    // layout.
    WritingMode wm = aState.mReflowInput.GetWritingMode();
    nsSize containerSize = aState.ContainerSize();
    LogicalRect overflow =
        aLine->GetOverflowArea(OverflowType::Scrollable, wm, containerSize);
    nscoord lineBCoordCombinedBefore = overflow.BStart(wm) + aDeltaBCoord;
    nscoord lineBCoordCombinedAfter =
        lineBCoordCombinedBefore + overflow.BSize(wm);

    bool isDirty =
        floatManager->IntersectsDamage(lineBCoordBefore, lineBCoordAfter) ||
        floatManager->IntersectsDamage(lineBCoordCombinedBefore,
                                       lineBCoordCombinedAfter);
    if (isDirty) {
      aLine->MarkDirty();
      return;
    }
  }

  // Check if the line is moving relative to the float manager
  if (aDeltaBCoord + aState.mReflowInput.mBlockDelta != 0) {
    if (aLine->IsBlock()) {
      // Unconditionally reflow sliding blocks; we only really need to reflow
      // if there's a float impacting this block, but the current float manager
      // makes it difficult to check that.  Therefore, we let the child block
      // decide what it needs to reflow.
      aLine->MarkDirty();
    } else {
      bool wasImpactedByFloat = aLine->IsImpactedByFloat();
      nsFlowAreaRect floatAvailableSpace =
          aState.GetFloatAvailableSpaceForBSize(
              aState.mReflowInput.GetWritingMode(),
              aLine->BStart() + aDeltaBCoord, aLine->BSize(), nullptr);

#ifdef REALLY_NOISY_REFLOW
      printf("nsBlockFrame::PropagateFloatDamage %p was = %d, is=%d\n", this,
             wasImpactedByFloat, floatAvailableSpace.HasFloats());
#endif

      // Mark the line dirty if it was or is affected by a float
      // We actually only really need to reflow if the amount of impact
      // changes, but that's not straightforward to check
      if (wasImpactedByFloat || floatAvailableSpace.HasFloats()) {
        aLine->MarkDirty();
      }
    }
  }
}

static bool LineHasClear(nsLineBox* aLine) {
  return aLine->IsBlock()
             ? (aLine->HasFloatClearTypeBefore() ||
                aLine->mFirstChild->HasAnyStateBits(
                    NS_BLOCK_HAS_CLEAR_CHILDREN) ||
                !nsBlockFrame::BlockCanIntersectFloats(aLine->mFirstChild))
             : aLine->HasFloatClearTypeAfter();
}

/**
 * Reparent a whole list of floats from aOldParent to this block.  The
 * floats might be taken from aOldParent's overflow list. They will be
 * removed from the list. They end up appended to our floats list.
 */
void nsBlockFrame::ReparentFloats(nsIFrame* aFirstFrame,
                                  nsBlockFrame* aOldParent,
                                  bool aReparentSiblings) {
  nsFrameList list;
  aOldParent->CollectFloats(aFirstFrame, list, aReparentSiblings);
  if (list.NotEmpty()) {
    for (nsIFrame* f : list) {
      MOZ_ASSERT(!f->HasAnyStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW),
                 "CollectFloats should've removed that bit");
      ReparentFrame(f, aOldParent, this);
    }
    EnsureFloats()->AppendFrames(nullptr, std::move(list));
  }
}

static void DumpLine(const BlockReflowState& aState, nsLineBox* aLine,
                     nscoord aDeltaBCoord, int32_t aDeltaIndent) {
#ifdef DEBUG
  if (nsBlockFrame::gNoisyReflow) {
    nsBlockFrame::IndentBy(stdout, nsBlockFrame::gNoiseIndent + aDeltaIndent);
    fmt::println(
        "line={} mBCoord={} dirty={} bounds={} overflow-ink={} "
        "overflow-scr={} deltaBCoord={} mPrevBEndMargin={} childCount={}",
        static_cast<void*>(aLine), aState.mBCoord, YesOrNo(aLine->IsDirty()),
        ToString(aLine->GetBounds()), ToString(aLine->InkOverflowRect()),
        ToString(aLine->ScrollableOverflowRect()), aDeltaBCoord,
        aState.mPrevBEndMargin.Get(), aLine->GetChildCount());
  }
#endif
}

bool nsBlockFrame::LinesAreEmpty() const {
  for (const auto& line : mLines) {
    if (!line.IsEmpty()) {
      return false;
    }
  }
  return true;
}

bool nsBlockFrame::ReflowDirtyLines(BlockReflowState& aState) {
  bool keepGoing = true;
  bool foundAnyClears = aState.mTrailingClearFromPIF != UsedClear::None;
  bool willReflowAgain = false;
  bool usedOverflowWrap = false;

#ifdef DEBUG
  if (gNoisyReflow) {
    IndentBy(stdout, gNoiseIndent);
    ListTag(stdout);
    printf(": reflowing dirty lines");
    printf(" computedISize=%d\n", aState.mReflowInput.ComputedISize());
  }
  AutoNoisyIndenter indent(gNoisyReflow);
#endif

  bool selfDirty = HasAnyStateBits(NS_FRAME_IS_DIRTY) ||
                   (aState.mReflowInput.IsBResize() &&
                    HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE));

  // Reflow our last line if our availableBSize has increased
  // so that we (and our last child) pull up content as necessary
  if (aState.mReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE &&
      GetNextInFlow() &&
      aState.mReflowInput.AvailableBSize() >
          GetLogicalSize().BSize(aState.mReflowInput.GetWritingMode())) {
    LineIterator lastLine = LinesEnd();
    if (lastLine != LinesBegin()) {
      --lastLine;
      lastLine->MarkDirty();
    }
  }
  // the amount by which we will slide the current line if it is not
  // dirty
  nscoord deltaBCoord = 0;

  // whether we did NOT reflow the previous line and thus we need to
  // recompute the carried out margin before the line if we want to
  // reflow it or if its previous margin is dirty
  bool needToRecoverState = false;
  // Float continuations were reflowed in ReflowPushedFloats
  bool reflowedFloat =
      HasFloats() && GetFloats()->FirstChild()->HasAnyStateBits(
                         NS_FRAME_IS_PUSHED_OUT_OF_FLOW);
  bool lastLineMovedUp = false;
  // We save up information about BR-clearance here
  UsedClear inlineFloatClearType = aState.mTrailingClearFromPIF;

  LineIterator line = LinesBegin(), line_end = LinesEnd();

  // Determine if children of this frame could have breaks between them for
  // page names.
  //
  // We need to check for paginated layout, the named-page pref, and if the
  // available block-size is constrained.
  //
  // Note that we need to check for paginated layout as named-pages are only
  // used during paginated reflow. We need to additionally check for
  // unconstrained block-size to avoid introducing fragmentation breaks during
  // "measuring" reflows within an overall paginated reflow, and to avoid
  // fragmentation in monolithic containers like 'inline-block'.
  //
  // Because we can only break for named pages using Class A breakpoints, we
  // also need to check that the block flow direction of the containing frame
  // of these items (which is this block) is parallel to that of this page.
  // See: https://www.w3.org/TR/css-break-3/#btw-blocks
  const nsPresContext* const presCtx = aState.mPresContext;
  const bool canBreakForPageNames =
      aState.mReflowInput.mFlags.mCanHaveClassABreakpoints &&
      aState.mReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE &&
      presCtx->GetPresShell()->GetRootFrame()->GetWritingMode().IsVertical() ==
          GetWritingMode().IsVertical();

  // ReflowInput.mFlags.mCanHaveClassABreakpoints should respect the named
  // pages pref and presCtx->IsPaginated, so we did not explicitly check these
  // above when setting canBreakForPageNames.
  if (canBreakForPageNames) {
    MOZ_ASSERT(presCtx->IsPaginated(),
               "canBreakForPageNames should not be set during non-paginated "
               "reflow");
  }

  // Reflow the lines that are already ours
  for (; line != line_end; ++line, aState.AdvanceToNextLine()) {
    DumpLine(aState, line, deltaBCoord, 0);
#ifdef DEBUG
    AutoNoisyIndenter indent2(gNoisyReflow);
#endif

    if (selfDirty) {
      line->MarkDirty();
    }

    // This really sucks, but we have to look inside any blocks that have clear
    // elements inside them.
    // XXX what can we do smarter here?
    if (!line->IsDirty() && line->IsBlock() &&
        line->mFirstChild->HasAnyStateBits(NS_BLOCK_HAS_CLEAR_CHILDREN) &&
        aState.FloatManager()->HasAnyFloats()) {
      line->MarkDirty();
    }

    nsIFrame* floatAvoidingBlock = nullptr;
    if (line->IsBlock() &&
        !nsBlockFrame::BlockCanIntersectFloats(line->mFirstChild)) {
      floatAvoidingBlock = line->mFirstChild;
    }

    // We have to reflow the line if it's a block whose clearance
    // might have changed, so detect that.
    if (!line->IsDirty() &&
        (line->HasFloatClearTypeBefore() || floatAvoidingBlock)) {
      nscoord curBCoord = aState.mBCoord;
      // See where we would be after applying any clearance due to
      // BRs.
      if (inlineFloatClearType != UsedClear::None) {
        std::tie(curBCoord, std::ignore) =
            aState.ClearFloats(curBCoord, inlineFloatClearType);
      }

      auto [newBCoord, result] = aState.ClearFloats(
          curBCoord, line->FloatClearTypeBefore(), floatAvoidingBlock);

      if (line->HasClearance()) {
        // Reflow the line if it might not have clearance anymore.
        if (result == ClearFloatsResult::BCoordNoChange
            // aState.mBCoord is the clearance point which should be the
            // block-start border-edge of the block frame. If sliding the
            // block by deltaBCoord isn't going to put it in the predicted
            // position, then we'd better reflow the line.
            || newBCoord != line->BStart() + deltaBCoord) {
          line->MarkDirty();
        }
      } else {
        // Reflow the line if the line might have clearance now.
        if (result != ClearFloatsResult::BCoordNoChange) {
          line->MarkDirty();
        }
      }
    }

    // We might have to reflow a line that is after a clearing BR.
    if (inlineFloatClearType != UsedClear::None) {
      std::tie(aState.mBCoord, std::ignore) =
          aState.ClearFloats(aState.mBCoord, inlineFloatClearType);
      if (aState.mBCoord != line->BStart() + deltaBCoord) {
        // SlideLine is not going to put the line where the clearance
        // put it. Reflow the line to be sure.
        line->MarkDirty();
      }
      inlineFloatClearType = UsedClear::None;
    }

    bool previousMarginWasDirty = line->IsPreviousMarginDirty();
    if (previousMarginWasDirty) {
      // If the previous margin is dirty, reflow the current line
      line->MarkDirty();
      line->ClearPreviousMarginDirty();
    } else if (aState.ContentBSize() != NS_UNCONSTRAINEDSIZE) {
      const nscoord scrollableOverflowBEnd =
          LogicalRect(line->mWritingMode, line->ScrollableOverflowRect(),
                      line->mContainerSize)
              .BEnd(line->mWritingMode);
      if (scrollableOverflowBEnd + deltaBCoord > aState.ContentBEnd()) {
        // Lines that aren't dirty but get slid past our available block-size
        // constraint must be reflowed.
        line->MarkDirty();
      }
    }

    if (!line->IsDirty()) {
      const bool isPaginated =
          // Last column can be reflowed unconstrained during column balancing.
          // Hence the additional NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR bit check
          // as a fail-safe fallback.
          aState.mReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE ||
          HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR) ||
          // Table can also be reflowed unconstrained during printing.
          aState.mPresContext->IsPaginated();
      if (isPaginated) {
        // We are in a paginated context, i.e. in columns or pages.
        const bool mayContainFloats =
            line->IsBlock() || line->HasFloats() || line->HadFloatPushed();
        if (mayContainFloats) {
          // The following if-else conditions check whether this line -- which
          // might have floats in its subtree, or has floats as direct children,
          // or had floats pushed -- needs to be reflowed.
          if (deltaBCoord != 0 || aState.mReflowInput.IsBResize()) {
            // The distance to the block-end edge might have changed. Reflow the
            // line both because the breakpoints within its floats may have
            // changed and because we might have to push/pull the floats in
            // their entirety.
            line->MarkDirty();
          } else if (HasPushedFloats()) {
            // We had pushed floats which haven't been drained by our
            // next-in-flow, which means our parent is currently reflowing us
            // again due to clearance without creating a next-in-flow for us.
            // Reflow the line to redo the floats split logic to correctly set
            // our reflow status.
            line->MarkDirty();
          } else if (aState.mReflowInput.mFlags.mMustReflowPlaceholders) {
            // Reflow the line (that may containing a float's placeholder frame)
            // if our parent tells us to do so.
            line->MarkDirty();
          } else if (aState.mReflowInput.mFlags.mMovedBlockFragments) {
            // Our parent's line containing us moved to a different fragment.
            // Reflow the line because the decision about whether the float fits
            // may be different in a different fragment.
            line->MarkDirty();
          }
        }
      }
    }

    if (!line->IsDirty()) {
      // See if there's any reflow damage that requires that we mark the
      // line dirty.
      PropagateFloatDamage(aState, line, deltaBCoord);
    }

    // If the container size has changed, reset mContainerSize. If the
    // line's writing mode is not ltr, or if the line is not left-aligned, also
    // mark the line dirty.
    if (aState.ContainerSize() != line->mContainerSize) {
      line->mContainerSize = aState.ContainerSize();

      const bool isLastLine = line == mLines.back() && !GetNextInFlow();
      const auto align = isLastLine ? StyleText()->TextAlignForLastLine()
                                    : StyleText()->mTextAlign;
      if (line->mWritingMode.IsVertical() || line->mWritingMode.IsBidiRTL() ||
          !IsAlignedLeft(align, StyleVisibility()->mDirection,
                         StyleTextReset()->mUnicodeBidi, this)) {
        line->MarkDirty();
      }
    }

    // Check for a page break caused by CSS named pages.
    //
    // We should break for named pages when two frames meet at a class A
    // breakpoint, where the first frame has a different end page value to the
    // second frame's start page value. canBreakForPageNames is true iff
    // children of this frame can form class A breakpoints, and that we are not
    // in a measurement reflow or in a monolithic container such as
    // 'inline-block'.
    //
    // We specifically do not want to cause a page-break for named pages when
    // we are at the top of a page. This would otherwise happen when the
    // previous sibling is an nsPageBreakFrame, or all previous siblings on the
    // current page are zero-height. The latter may not be per-spec, but is
    // compatible with Chrome's implementation of named pages.
    const nsAtom* nextPageName = nullptr;
    bool shouldBreakForPageName = false;
    if (canBreakForPageNames && (!aState.mReflowInput.mFlags.mIsTopOfPage ||
                                 !aState.IsAdjacentWithBStart())) {
      const nsIFrame* const frame = line->mFirstChild;
      if (!frame->IsPlaceholderFrame() && !frame->IsPageBreakFrame()) {
        nextPageName = frame->GetStartPageValue();
        // Walk back to the last frame that isn't a placeholder.
        const nsIFrame* prevFrame = frame->GetPrevSibling();
        while (prevFrame && prevFrame->IsPlaceholderFrame()) {
          prevFrame = prevFrame->GetPrevSibling();
        }
        if (prevFrame && prevFrame->GetEndPageValue() != nextPageName) {
          shouldBreakForPageName = true;
          line->MarkDirty();
        }
      }
    }

    if (needToRecoverState && line->IsDirty()) {
      // We need to reconstruct the block-end margin only if we didn't
      // reflow the previous line and we do need to reflow (or repair
      // the block-start position of) the next line.
      aState.ReconstructMarginBefore(line);
    }

    bool reflowedPrevLine = !needToRecoverState;
    if (needToRecoverState) {
      needToRecoverState = false;

      // Update aState.mPrevChild as if we had reflowed all of the frames in
      // this line.
      if (line->IsDirty()) {
        NS_ASSERTION(
            line->mFirstChild->GetPrevSibling() == line.prev()->LastChild(),
            "unexpected line frames");
        aState.mPrevChild = line->mFirstChild->GetPrevSibling();
      }
    }

    // Now repair the line and update |aState.mBCoord| by calling
    // |ReflowLine| or |SlideLine|.
    // If we're going to reflow everything again, then no need to reflow
    // the dirty line ... unless the line has floats, in which case we'd
    // better reflow it now to refresh its float cache, which may contain
    // dangling frame pointers! Ugh! This reflow of the line may be
    // incorrect because we skipped reflowing previous lines (e.g., floats
    // may be placed incorrectly), but that's OK because we'll mark the
    // line dirty below under "if (aState.mReflowInput.mDiscoveredClearance..."
    if (line->IsDirty() && (line->HasFloats() || !willReflowAgain)) {
      lastLineMovedUp = true;

      bool maybeReflowingForFirstTime =
          line->IStart() == 0 && line->BStart() == 0 && line->ISize() == 0 &&
          line->BSize() == 0;

      // Compute the dirty lines "before" BEnd, after factoring in
      // the running deltaBCoord value - the running value is implicit in
      // aState.mBCoord.
      nscoord oldB = line->BStart();
      nscoord oldBMost = line->BEnd();

      NS_ASSERTION(!willReflowAgain || !line->IsBlock(),
                   "Don't reflow blocks while willReflowAgain is true, reflow "
                   "of block abs-pos children depends on this");

      if (shouldBreakForPageName) {
        // Immediately fragment for page-name. It is possible we could break
        // out of the loop right here, but this should make it more similar to
        // what happens when reflow causes fragmentation.
        // Set the page name, so that PushTruncatedLine does not need to
        // recalculate the new page name.
        PresShell()->FrameConstructor()->SetNextPageContentFramePageName(
            nextPageName ? nextPageName : GetAutoPageValue());
        PushTruncatedLine(aState, line, &keepGoing,
                          ComputeNewPageNameIfNeeded::No);
      } else {
        // Reflow the dirty line. If it's an incremental reflow, then force
        // it to invalidate the dirty area if necessary
        usedOverflowWrap |= ReflowLine(aState, line, &keepGoing);
      }

      if (aState.mReflowInput.WillReflowAgainForClearance()) {
        line->MarkDirty();
        willReflowAgain = true;
        // Note that once we've entered this state, every line that gets here
        // (e.g. because it has floats) gets marked dirty and reflowed again.
        // in the next pass. This is important, see above.
      }

      if (line->HasFloats()) {
        reflowedFloat = true;
      }

      if (!keepGoing) {
        DumpLine(aState, line, deltaBCoord, -1);
        if (0 == line->GetChildCount()) {
          DeleteLine(aState, line, line_end);
        }
        break;
      }

      // Test to see whether the margin that should be carried out
      // to the next line (NL) might have changed. In ReflowBlockFrame
      // we call nextLine->MarkPreviousMarginDirty if the block's
      // actual carried-out block-end margin changed. So here we only
      // need to worry about the following effects:
      // 1) the line was just created, and it might now be blocking
      // a carried-out block-end margin from previous lines that
      // used to reach NL from reaching NL
      // 2) the line used to be empty, and is now not empty,
      // thus blocking a carried-out block-end margin from previous lines
      // that used to reach NL from reaching NL
      // 3) the line wasn't empty, but now is, so a carried-out
      // block-end margin from previous lines that didn't used to reach NL
      // now does
      // 4) the line might have changed in a way that affects NL's
      // ShouldApplyBStartMargin decision. The three things that matter
      // are the line's emptiness, its adjacency to the block-start edge of the
      // block, and whether it has clearance (the latter only matters if the
      // block was and is adjacent to the block-start and empty).
      //
      // If the line is empty now, we can't reliably tell if the line was empty
      // before, so we just assume it was and do
      // nextLine->MarkPreviousMarginDirty. This means the checks in 4) are
      // redundant; if the line is empty now we don't need to check 4), but if
      // the line is not empty now and we're sure it wasn't empty before, any
      // adjacency and clearance changes are irrelevant to the result of
      // nextLine->ShouldApplyBStartMargin.
      if (line.next() != LinesEnd()) {
        bool maybeWasEmpty = oldB == line.next()->BStart();
        bool isEmpty = line->CachedIsEmpty();
        if (maybeReflowingForFirstTime /*1*/ ||
            (isEmpty || maybeWasEmpty) /*2/3/4*/) {
          line.next()->MarkPreviousMarginDirty();
          // since it's marked dirty, nobody will care about |deltaBCoord|
        }
      }

      // If the line was just reflowed for the first time, then its
      // old mBounds cannot be trusted so this deltaBCoord computation is
      // bogus. But that's OK because we just did
      // MarkPreviousMarginDirty on the next line which will force it
      // to be reflowed, so this computation of deltaBCoord will not be
      // used.
      deltaBCoord = line->BEnd() - oldBMost;

      // Now do an interrupt check. We want to do this only in the case when we
      // actually reflow the line, so that if we get back in here we'll get
      // further on the reflow before interrupting.
      aState.mPresContext->CheckForInterrupt(this);
    } else {
      aState.mOverflowTracker->Skip(line->mFirstChild, aState.mReflowStatus);
      // Nop except for blocks (we don't create overflow container
      // continuations for any inlines atm), so only checking mFirstChild
      // is enough

      lastLineMovedUp = deltaBCoord < 0;

      if (deltaBCoord != 0) {
        SlideLine(aState, line, deltaBCoord);
      }

      NS_ASSERTION(!line->IsDirty() || !line->HasFloats(),
                   "Possibly stale float cache here!");
      if (willReflowAgain && line->IsBlock()) {
        // If we're going to reflow everything again, and this line is a block,
        // then there is no need to recover float state. The line may contain
        // other lines with floats, but in that case RecoverStateFrom would only
        // add floats to the float manager. We don't need to do that because
        // everything's going to get reflowed again "for real". Calling
        // RecoverStateFrom in this situation could be lethal because the
        // block's descendant lines may have float caches containing dangling
        // frame pointers. Ugh!
        // If this line is inline, then we need to recover its state now
        // to make sure that we don't forget to move its floats by deltaBCoord.
      } else {
        // XXX EVIL O(N^2) EVIL
        aState.RecoverStateFrom(line, deltaBCoord);
      }

      // Keep mBCoord up to date in case we're propagating reflow damage
      // and also because our final height may depend on it. If the
      // line is inlines, then only update mBCoord if the line is not
      // empty, because that's what PlaceLine does. (Empty blocks may
      // want to update mBCoord, e.g. if they have clearance.)
      if (line->IsBlock() || !line->CachedIsEmpty()) {
        aState.mBCoord = line->BEnd();
      }

      needToRecoverState = true;

      if (reflowedPrevLine && !line->IsBlock() &&
          aState.mPresContext->HasPendingInterrupt()) {
        // Need to make sure to pull overflows from any prev-in-flows
        for (nsIFrame* inlineKid = line->mFirstChild; inlineKid;
             inlineKid = inlineKid->PrincipalChildList().FirstChild()) {
          inlineKid->PullOverflowsFromPrevInFlow();
        }
      }
    }

    // Record if we need to clear floats before reflowing the next
    // line. Note that inlineFloatClearType will be handled and
    // cleared before the next line is processed, so there is no
    // need to combine break types here.
    if (line->HasFloatClearTypeAfter()) {
      inlineFloatClearType = line->FloatClearTypeAfter();
    }

    if (LineHasClear(line.get())) {
      foundAnyClears = true;
    }

    DumpLine(aState, line, deltaBCoord, -1);

    if (aState.mPresContext->HasPendingInterrupt()) {
      willReflowAgain = true;
      // Another option here might be to leave |line| clean if
      // !HasPendingInterrupt() before the CheckForInterrupt() call, since in
      // that case the line really did reflow as it should have.  Not sure
      // whether that would be safe, so doing this for now instead.  Also not
      // sure whether we really want to mark all lines dirty after an
      // interrupt, but until we get better at propagating float damage we
      // really do need to do it this way; see comments inside MarkLineDirty.
      MarkLineDirtyForInterrupt(line);
    }
  }

  // Handle BR-clearance from the last line of the block
  if (inlineFloatClearType != UsedClear::None) {
    std::tie(aState.mBCoord, std::ignore) =
        aState.ClearFloats(aState.mBCoord, inlineFloatClearType);
  }

  if (needToRecoverState) {
    // Is this expensive?
    aState.ReconstructMarginBefore(line);

    // Update aState.mPrevChild as if we had reflowed all of the frames in
    // the last line.
    NS_ASSERTION(line == line_end || line->mFirstChild->GetPrevSibling() ==
                                         line.prev()->LastChild(),
                 "unexpected line frames");
    aState.mPrevChild = line == line_end ? mFrames.LastChild()
                                         : line->mFirstChild->GetPrevSibling();
  }

  // We can skip trying to pull up the next line if our height is constrained
  // (so we can report being incomplete) and there is no next in flow or we
  // were told not to or we know it will be futile, i.e.,
  // -- the next in flow is not changing
  // -- and we cannot have added more space for its first line to be
  // pulled up into,
  // -- it's an incremental reflow of a descendant
  // -- and we didn't reflow any floats (so the available space
  // didn't change)
  // -- my chain of next-in-flows either has no first line, or its first
  // line isn't dirty.
  bool heightConstrained =
      aState.mReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE;
  bool skipPull = willReflowAgain && heightConstrained;
  if (!skipPull && heightConstrained && aState.mNextInFlow &&
      (aState.mReflowInput.mFlags.mNextInFlowUntouched && !lastLineMovedUp &&
       !HasAnyStateBits(NS_FRAME_IS_DIRTY) && !reflowedFloat)) {
    // We'll place lineIter at the last line of this block, so that
    // nsBlockInFlowLineIterator::Next() will take us to the first
    // line of my next-in-flow-chain.  (But first, check that I
    // have any lines -- if I don't, just bail out of this
    // optimization.)
    LineIterator lineIter = this->LinesEnd();
    if (lineIter != this->LinesBegin()) {
      lineIter--;  // I have lines; step back from dummy iterator to last line.
      nsBlockInFlowLineIterator bifLineIter(this, lineIter);

      // Check for next-in-flow-chain's first line.
      // (First, see if there is such a line, and second, see if it's clean)
      if (!bifLineIter.Next() || !bifLineIter.GetLine()->IsDirty()) {
        skipPull = true;
      }
    }
  }

  if (skipPull && aState.mNextInFlow) {
    NS_ASSERTION(heightConstrained, "Height should be constrained here\n");
    if (aState.mNextInFlow->IsTrueOverflowContainer()) {
      aState.mReflowStatus.SetOverflowIncomplete();
    } else {
      aState.mReflowStatus.SetIncomplete();
    }
  }

  if (!skipPull && aState.mNextInFlow) {
    // Pull data from a next-in-flow if there's still room for more
    // content here.
    while (keepGoing && aState.mNextInFlow) {
      // Grab first line from our next-in-flow
      nsBlockFrame* nextInFlow = aState.mNextInFlow;
      nsLineBox* pulledLine;
      nsFrameList pulledFrames;
      if (!nextInFlow->mLines.empty()) {
        RemoveFirstLine(nextInFlow->mLines, nextInFlow->mFrames, &pulledLine,
                        &pulledFrames);
        ClearLineCursors();
      } else {
        // Grab an overflow line if there are any
        FrameLines* overflowLines = nextInFlow->GetOverflowLines();
        if (!overflowLines) {
          aState.mNextInFlow =
              static_cast<nsBlockFrame*>(nextInFlow->GetNextInFlow());
          continue;
        }
        bool last =
            RemoveFirstLine(overflowLines->mLines, overflowLines->mFrames,
                            &pulledLine, &pulledFrames);
        if (last) {
          nextInFlow->DestroyOverflowLines();
        }
      }

      if (pulledFrames.IsEmpty()) {
        // The line is empty. Try the next one.
        NS_ASSERTION(
            pulledLine->GetChildCount() == 0 && !pulledLine->mFirstChild,
            "bad empty line");
        nextInFlow->FreeLineBox(pulledLine);
        continue;
      }

      if (nextInFlow->MaybeHasLineCursor()) {
        if (pulledLine == nextInFlow->GetLineCursorForDisplay()) {
          nextInFlow->ClearLineCursorForDisplay();
        }
        if (pulledLine == nextInFlow->GetLineCursorForQuery()) {
          nextInFlow->ClearLineCursorForQuery();
        }
      }
      ReparentFrames(pulledFrames, nextInFlow, this);
      pulledLine->SetMovedFragments();

      NS_ASSERTION(pulledFrames.LastChild() == pulledLine->LastChild(),
                   "Unexpected last frame");
      NS_ASSERTION(aState.mPrevChild || mLines.empty(),
                   "should have a prevchild here");
      NS_ASSERTION(aState.mPrevChild == mFrames.LastChild(),
                   "Incorrect aState.mPrevChild before inserting line at end");

      // Shift pulledLine's frames into our mFrames list.
      mFrames.AppendFrames(nullptr, std::move(pulledFrames));

      // Add line to our line list, and set its last child as our new prev-child
      line = mLines.before_insert(LinesEnd(), pulledLine);
      aState.mPrevChild = mFrames.LastChild();

      // Reparent floats whose placeholders are in the line.
      ReparentFloats(pulledLine->mFirstChild, nextInFlow, true);

      DumpLine(aState, pulledLine, deltaBCoord, 0);
#ifdef DEBUG
      AutoNoisyIndenter indent2(gNoisyReflow);
#endif

      if (aState.mPresContext->HasPendingInterrupt()) {
        MarkLineDirtyForInterrupt(line);
      } else {
        // Now reflow it and any lines that it makes during it's reflow
        // (we have to loop here because reflowing the line may cause a new
        // line to be created; see SplitLine's callers for examples of
        // when this happens).
        while (line != LinesEnd()) {
          usedOverflowWrap |= ReflowLine(aState, line, &keepGoing);

          if (aState.mReflowInput.WillReflowAgainForClearance()) {
            line->MarkDirty();
            keepGoing = false;
            // If we are going to be reflowed again by our ancestor due to a
            // clearance frame being discovered, reset the reflow completion
            // status. The current status does not matter to our parent frame
            // since we will reflow again anyway. This also prevents the false
            // assumption that we are incomplete when reflowing under an
            // unconstrained available block-size.
            aState.mReflowStatus.Reset();
            break;
          }

          DumpLine(aState, line, deltaBCoord, -1);
          if (!keepGoing) {
            if (0 == line->GetChildCount()) {
              DeleteLine(aState, line, line_end);
            }
            break;
          }

          if (LineHasClear(line.get())) {
            foundAnyClears = true;
          }

          if (aState.mPresContext->CheckForInterrupt(this)) {
            MarkLineDirtyForInterrupt(line);
            break;
          }

          // If this is an inline frame then its time to stop
          ++line;
          aState.AdvanceToNextLine();
        }
      }
    }

    if (aState.mReflowStatus.IsIncomplete()) {
      aState.mReflowStatus.SetNextInFlowNeedsReflow();
    }  // XXXfr shouldn't set this flag when nextinflow has no lines
  }

  // Handle an odd-ball case: a list-item with no lines
  nsIFrame* outsideMarker = GetOutsideMarker();
  if (outsideMarker && mLines.empty()) {
    ReflowOutput metrics(aState.mReflowInput);
    WritingMode wm = aState.mReflowInput.GetWritingMode();
    ReflowOutsideMarker(
        outsideMarker, aState, metrics,
        aState.mReflowInput.ComputedPhysicalBorderPadding().top);
    NS_ASSERTION(!MarkerIsEmpty(outsideMarker) || metrics.BSize(wm) == 0,
                 "empty ::marker frame took up space");

    if (!MarkerIsEmpty(outsideMarker)) {
      // There are no lines so we have to fake up some y motion so that
      // we end up with *some* height.
      // (Note: if we're layout-contained, we have to be sure to leave our
      // ReflowOutput's BlockStartAscent() (i.e. the baseline) untouched,
      // because layout-contained frames have no baseline.)
      if (!aState.mReflowInput.mStyleDisplay->IsContainLayout() &&
          metrics.BlockStartAscent() == ReflowOutput::ASK_FOR_BASELINE) {
        nscoord ascent;
        WritingMode wm = aState.mReflowInput.GetWritingMode();
        if (nsLayoutUtils::GetFirstLineBaseline(wm, outsideMarker, &ascent)) {
          metrics.SetBlockStartAscent(ascent);
        } else {
          metrics.SetBlockStartAscent(metrics.BSize(wm));
        }
      }

      RefPtr<nsFontMetrics> fm =
          nsLayoutUtils::GetInflatedFontMetricsForFrame(this);

      nscoord minAscent = nsLayoutUtils::GetCenteredFontBaseline(
          fm, aState.mMinLineHeight, wm.IsLineInverted());
      nscoord minDescent = aState.mMinLineHeight - minAscent;

      aState.mBCoord +=
          std::max(minAscent, metrics.BlockStartAscent()) +
          std::max(minDescent, metrics.BSize(wm) - metrics.BlockStartAscent());

      nscoord offset = minAscent - metrics.BlockStartAscent();
      if (offset > 0) {
        outsideMarker->SetRect(outsideMarker->GetRect() + nsPoint(0, offset));
      }
    }
  }

  if (LinesAreEmpty() && ShouldHaveLineIfEmpty()) {
    aState.mBCoord += aState.mMinLineHeight;
  }

  if (foundAnyClears) {
    AddStateBits(NS_BLOCK_HAS_CLEAR_CHILDREN);
  } else {
    RemoveStateBits(NS_BLOCK_HAS_CLEAR_CHILDREN);
  }

#ifdef DEBUG
  VerifyLines(true);
  VerifyOverflowSituation();
  if (gNoisyReflow) {
    IndentBy(stdout, gNoiseIndent - 1);
    ListTag(stdout);
    printf(": done reflowing dirty lines (status=%s)\n",
           ToString(aState.mReflowStatus).c_str());
  }
#endif

  return usedOverflowWrap;
}

void nsBlockFrame::MarkLineDirtyForInterrupt(nsLineBox* aLine) {
  aLine->MarkDirty();

  // Just checking NS_FRAME_IS_DIRTY is ok, because we've already
  // marked the lines that need to be marked dirty based on our
  // vertical resize stuff.  So we'll definitely reflow all those kids;
  // the only question is how they should behave.
  if (HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
    // Mark all our child frames dirty so we make sure to reflow them
    // later.
    for (nsIFrame* f : aLine->ChildFrames()) {
      f->MarkSubtreeDirty();
    }
    // And mark all the floats whose reflows we might be skipping dirty too.
    if (aLine->HasFloats()) {
      for (nsIFrame* f : aLine->Floats()) {
        f->MarkSubtreeDirty();
      }
    }
  } else {
    // Dirty all the descendant lines of block kids to handle float damage,
    // since our nsFloatManager will go away by the next time we're reflowing.
    // XXXbz Can we do something more like what PropagateFloatDamage does?
    // Would need to sort out the exact business with mBlockDelta for that....
    // This marks way too much dirty.  If we ever make this better, revisit
    // which lines we mark dirty in the interrupt case in ReflowDirtyLines.
    nsBlockFrame* bf = do_QueryFrame(aLine->mFirstChild);
    if (bf) {
      MarkAllDescendantLinesDirty(bf);
    }
  }
}

void nsBlockFrame::DeleteLine(BlockReflowState& aState,
                              nsLineList::iterator aLine,
                              nsLineList::iterator aLineEnd) {
  MOZ_ASSERT(0 == aLine->GetChildCount(), "can't delete !empty line");
  if (0 == aLine->GetChildCount()) {
    NS_ASSERTION(aState.mCurrentLine == aLine,
                 "using function more generally than designed, "
                 "but perhaps OK now");
    nsLineBox* line = aLine;
    aLine = mLines.erase(aLine);
    FreeLineBox(line);
    ClearLineCursors();
    // Mark the previous margin of the next line dirty since we need to
    // recompute its top position.
    if (aLine != aLineEnd) {
      aLine->MarkPreviousMarginDirty();
    }
  }
}

/**
 * Reflow a line. The line will either contain a single block frame
 * or contain 1 or more inline frames. aKeepReflowGoing indicates
 * whether or not the caller should continue to reflow more lines.
 * Returns true if the reflow used an overflow-wrap breakpoint.
 */
bool nsBlockFrame::ReflowLine(BlockReflowState& aState, LineIterator aLine,
                              bool* aKeepReflowGoing) {
  MOZ_ASSERT(aLine->GetChildCount(), "reflowing empty line");

  // Setup the line-layout for the new line
  aState.mCurrentLine = aLine;
  aLine->ClearDirty();
  aLine->InvalidateCachedIsEmpty();
  aLine->ClearHadFloatPushed();

  // If this line contains a single block that is hidden by `content-visibility`
  // don't reflow the line. If this line contains inlines and the first one is
  // hidden by `content-visibility`, all of them are, so avoid reflow in that
  // case as well.
  // For frames that own anonymous children, even the first child is hidden by
  // `content-visibility`, there could be some anonymous children need reflow,
  // so we don't skip reflow this line.
  nsIFrame* firstChild = aLine->mFirstChild;
  if (firstChild->IsHiddenByContentVisibilityOfInFlowParentForLayout() &&
      !HasAnyStateBits(NS_FRAME_OWNS_ANON_BOXES)) {
    return false;
  }

  // Now that we know what kind of line we have, reflow it
  bool usedOverflowWrap = false;
  if (aLine->IsBlock()) {
    ReflowBlockFrame(aState, aLine, aKeepReflowGoing);
  } else {
    aLine->SetLineWrapped(false);
    usedOverflowWrap = ReflowInlineFrames(aState, aLine, aKeepReflowGoing);

    // Store the line's float edges for overflow marker analysis if needed.
    aLine->ClearFloatEdges();
    if (aState.mFlags.mCanHaveOverflowMarkers) {
      WritingMode wm = aLine->mWritingMode;
      nsFlowAreaRect r = aState.GetFloatAvailableSpaceForBSize(
          wm, aLine->BStart(), aLine->BSize(), nullptr);
      if (r.HasFloats()) {
        LogicalRect so = aLine->GetOverflowArea(OverflowType::Scrollable, wm,
                                                aLine->mContainerSize);
        nscoord s = r.mRect.IStart(wm);
        nscoord e = r.mRect.IEnd(wm);
        if (so.IEnd(wm) > e || so.IStart(wm) < s) {
          // This line is overlapping a float - store the edges marking the area
          // between the floats for text-overflow analysis.
          aLine->SetFloatEdges(s, e);
        }
      }
    }
  }

  aLine->ClearMovedFragments();

  return usedOverflowWrap;
}

nsIFrame* nsBlockFrame::PullFrame(BlockReflowState& aState,
                                  LineIterator aLine) {
  // First check our remaining lines.
  if (LinesEnd() != aLine.next()) {
    return PullFrameFrom(aLine, this, aLine.next());
  }

  NS_ASSERTION(
      !GetOverflowLines(),
      "Our overflow lines should have been removed at the start of reflow");

  // Try each next-in-flow.
  nsBlockFrame* nextInFlow = aState.mNextInFlow;
  while (nextInFlow) {
    if (nextInFlow->mLines.empty()) {
      nextInFlow->DrainSelfOverflowList();
    }
    if (!nextInFlow->mLines.empty()) {
      return PullFrameFrom(aLine, nextInFlow, nextInFlow->mLines.begin());
    }
    nextInFlow = static_cast<nsBlockFrame*>(nextInFlow->GetNextInFlow());
    aState.mNextInFlow = nextInFlow;
  }

  return nullptr;
}

nsIFrame* nsBlockFrame::PullFrameFrom(nsLineBox* aLine,
                                      nsBlockFrame* aFromContainer,
                                      nsLineList::iterator aFromLine) {
  nsLineBox* fromLine = aFromLine;
  MOZ_ASSERT(fromLine, "bad line to pull from");
  MOZ_ASSERT(fromLine->GetChildCount(), "empty line");
  MOZ_ASSERT(aLine->GetChildCount(), "empty line");
  MOZ_ASSERT(!HasProperty(LineIteratorProperty()),
             "Shouldn't have line iterators mid-reflow");

  NS_ASSERTION(fromLine->IsBlock() == fromLine->mFirstChild->IsBlockOutside(),
               "Disagreement about whether it's a block or not");

  if (fromLine->IsBlock()) {
    // If our line is not empty and the child in aFromLine is a block
    // then we cannot pull up the frame into this line. In this case
    // we stop pulling.
    return nullptr;
  }
  // Take frame from fromLine
  nsIFrame* frame = fromLine->mFirstChild;
  nsIFrame* newFirstChild = frame->GetNextSibling();

  if (aFromContainer != this) {
    // The frame is being pulled from a next-in-flow; therefore we need to add
    // it to our sibling list.
    MOZ_ASSERT(aLine == mLines.back());
    MOZ_ASSERT(aFromLine == aFromContainer->mLines.begin(),
               "should only pull from first line");
    aFromContainer->mFrames.RemoveFrame(frame);

    // When pushing and pulling frames we need to check for whether any
    // views need to be reparented.
    ReparentFrame(frame, aFromContainer, this);
    mFrames.AppendFrame(nullptr, frame);

    // The frame might have (or contain) floats that need to be brought
    // over too. (pass 'false' since there are no siblings to check)
    ReparentFloats(frame, aFromContainer, false);
  } else {
    MOZ_ASSERT(aLine == aFromLine.prev());
  }

  aLine->NoteFrameAdded(frame);
  fromLine->NoteFrameRemoved(frame);

  if (fromLine->GetChildCount() > 0) {
    // Mark line dirty now that we pulled a child
    fromLine->MarkDirty();
    fromLine->mFirstChild = newFirstChild;
  } else {
    // Free up the fromLine now that it's empty.
    // Its bounds might need to be redrawn, though.
    if (aFromLine.next() != aFromContainer->mLines.end()) {
      aFromLine.next()->MarkPreviousMarginDirty();
    }
    aFromContainer->mLines.erase(aFromLine);
    // aFromLine is now invalid
    aFromContainer->FreeLineBox(fromLine);
  }

#ifdef DEBUG
  VerifyLines(true);
  VerifyOverflowSituation();
#endif

  return frame;
}

void nsBlockFrame::SlideLine(BlockReflowState& aState, nsLineBox* aLine,
                             nscoord aDeltaBCoord) {
  MOZ_ASSERT(aDeltaBCoord != 0, "why slide a line nowhere?");

  // Adjust line state
  aLine->SlideBy(aDeltaBCoord, aState.ContainerSize());

  // Adjust the frames in the line
  MoveChildFramesOfLine(aLine, aDeltaBCoord);
}

void nsBlockFrame::UpdateLineContainerSize(nsLineBox* aLine,
                                           const nsSize& aNewContainerSize) {
  if (aNewContainerSize == aLine->mContainerSize) {
    return;
  }

  // Adjust line state
  nsSize sizeDelta = aLine->UpdateContainerSize(aNewContainerSize);

  // Changing container width only matters if writing mode is vertical-rl
  if (GetWritingMode().IsVerticalRL()) {
    MoveChildFramesOfLine(aLine, sizeDelta.width);
  }
}

void nsBlockFrame::MoveChildFramesOfLine(nsLineBox* aLine,
                                         nscoord aDeltaBCoord) {
  // Adjust the frames in the line
  nsIFrame* kid = aLine->mFirstChild;
  if (!kid) {
    return;
  }

  WritingMode wm = GetWritingMode();
  LogicalPoint translation(wm, 0, aDeltaBCoord);

  if (aLine->IsBlock()) {
    if (aDeltaBCoord) {
      kid->MovePositionBy(wm, translation);
    }
  } else {
    // Adjust the block-dir coordinate of the frames in the line.
    // Note: we need to re-position views even if aDeltaBCoord is 0, because
    // one of our parent frames may have moved and so the view's position
    // relative to its parent may have changed.
    if (aDeltaBCoord) {
      for (nsIFrame* f : aLine->ChildFrames()) {
        f->MovePositionBy(wm, translation);
      }
    }
  }
}

static inline bool IsNonAutoNonZeroBSize(const StyleSize& aCoord) {
  // The "extremum length" values (see ExtremumLength) that return true from
  // 'BehavesLikeInitialValueOnBlockAxis()' were originally aimed at
  // inline-size (or width, as it was before logicalization). For now, let them
  // return false here, so we treat them like 'auto' pending a real
  // implementation. (See bug 1126420.)
  if (aCoord.BehavesLikeInitialValueOnBlockAxis()) {
    return false;
  }
  if (aCoord.BehavesLikeStretchOnBlockAxis()) {
    // We return true for "stretch" because it's essentially equivalent to
    // "100%" for the purposes of this function (and this function returns true
    // for nonzero percentage values, in the final return statement below).
    return true;
  }
  MOZ_ASSERT(aCoord.IsLengthPercentage());
  // If we evaluate the length/percent/calc at a percentage basis of
  // both nscoord_MAX and 0, and it's zero both ways, then it's a zero
  // length, percent, or combination thereof.  Test > 0 so we clamp
  // negative calc() results to 0.
  return aCoord.AsLengthPercentage().Resolve(nscoord_MAX) > 0 ||
         aCoord.AsLengthPercentage().Resolve(0) > 0;
}

/* virtual */
bool nsBlockFrame::IsSelfEmpty() {
  if (IsHiddenByContentVisibilityOfInFlowParentForLayout()) {
    return true;
  }

  // Blocks which are margin-roots (including inline-blocks) cannot be treated
  // as empty for margin-collapsing and other purposes. They're more like
  // replaced elements.
  if (HasAnyStateBits(NS_BLOCK_BFC)) {
    return false;
  }

  WritingMode wm = GetWritingMode();
  const nsStylePosition* position = StylePosition();
  const auto anchorResolutionParams = AnchorPosResolutionParams::From(this);
  const auto bSize = position->BSize(wm, anchorResolutionParams);

  if (IsNonAutoNonZeroBSize(*position->MinBSize(wm, anchorResolutionParams)) ||
      IsNonAutoNonZeroBSize(*bSize)) {
    return false;
  }

  // FIXME: Bug 1646100 - Take intrinsic size into account.
  // FIXME: Handle the case that both inline and block sizes are auto.
  // https://github.com/w3c/csswg-drafts/issues/5060.
  // Note: block-size could be zero or auto/intrinsic keywords here.
  if (bSize->BehavesLikeInitialValueOnBlockAxis() &&
      position->mAspectRatio.HasFiniteRatio()) {
    return false;
  }

  const nsStyleBorder* border = StyleBorder();
  const nsStylePadding* padding = StylePadding();

  if (border->GetComputedBorderWidth(wm.PhysicalSide(LogicalSide::BStart)) !=
          0 ||
      border->GetComputedBorderWidth(wm.PhysicalSide(LogicalSide::BEnd)) != 0 ||
      !nsLayoutUtils::IsPaddingZero(padding->mPadding.GetBStart(wm)) ||
      !nsLayoutUtils::IsPaddingZero(padding->mPadding.GetBEnd(wm))) {
    return false;
  }

  nsIFrame* outsideMarker = GetOutsideMarker();
  if (outsideMarker && !MarkerIsEmpty(outsideMarker)) {
    return false;
  }

  return true;
}

bool nsBlockFrame::CachedIsEmpty() {
  if (!IsSelfEmpty()) {
    return false;
  }
  for (auto& line : mLines) {
    if (!line.CachedIsEmpty()) {
      return false;
    }
  }
  return true;
}

bool nsBlockFrame::IsEmpty() {
  if (!IsSelfEmpty()) {
    return false;
  }
  return LinesAreEmpty();
}

bool nsBlockFrame::ShouldApplyBStartMargin(BlockReflowState& aState,
                                           nsLineBox* aLine) {
  if (aLine->mFirstChild->IsPageBreakFrame()) {
    // A page break frame consumes margins adjacent to it.
    // https://drafts.csswg.org/css-break/#break-margins
    return false;
  }

  if (aState.mFlags.mShouldApplyBStartMargin) {
    // Apply short-circuit check to avoid searching the line list
    return true;
  }

  if (!aState.IsAdjacentWithBStart()) {
    // If we aren't at the start block-coordinate then something of non-zero
    // height must have been placed. Therefore the childs block-start margin
    // applies.
    aState.mFlags.mShouldApplyBStartMargin = true;
    return true;
  }

  // Determine if this line is "essentially" the first line
  LineIterator line = LinesBegin();
  if (aState.mFlags.mHasLineAdjacentToTop) {
    line = aState.mLineAdjacentToTop;
  }
  while (line != aLine) {
    if (!line->CachedIsEmpty() || line->HasClearance()) {
      // A line which precedes aLine is non-empty, or has clearance,
      // so therefore the block-start margin applies.
      aState.mFlags.mShouldApplyBStartMargin = true;
      return true;
    }
    // No need to apply the block-start margin if the line has floats.  We
    // should collapse anyway (bug 44419)
    ++line;
    aState.mFlags.mHasLineAdjacentToTop = true;
    aState.mLineAdjacentToTop = line;
  }

  // The line being reflowed is "essentially" the first line in the
  // block. Therefore its block-start margin will be collapsed by the
  // generational collapsing logic with its parent (us).
  return false;
}

void nsBlockFrame::ReflowBlockFrame(BlockReflowState& aState,
                                    LineIterator aLine,
                                    bool* aKeepReflowGoing) {
  MOZ_ASSERT(*aKeepReflowGoing, "bad caller");

  nsIFrame* frame = aLine->mFirstChild;
  if (!frame) {
    NS_ASSERTION(false, "program error - unexpected empty line");
    return;
  }

  // If the previous frame was a page-break-frame, then preemptively push this
  // frame to the next page.
  // This is primarily important for the placeholders for abspos frames, which
  // measure as zero height and then would be placed on this page.
  if (aState.ContentBSize() != NS_UNCONSTRAINEDSIZE) {
    const nsIFrame* const prev = frame->GetPrevSibling();
    if (prev && prev->IsPageBreakFrame()) {
      PushTruncatedLine(aState, aLine, aKeepReflowGoing);
      return;
    }
  }

  // Prepare the block reflow engine
  nsBlockReflowContext brc(aState.mPresContext, aState.mReflowInput);

  WritingMode cbWM = frame->GetContainingBlock()->GetWritingMode();
  UsedClear clearType = frame->StyleDisplay()->UsedClear(cbWM);
  if (aState.mTrailingClearFromPIF != UsedClear::None) {
    clearType = nsLayoutUtils::CombineClearType(clearType,
                                                aState.mTrailingClearFromPIF);
    aState.mTrailingClearFromPIF = UsedClear::None;
  }

  // Clear past floats before the block if the clear style is not none
  aLine->ClearForcedLineBreak();
  if (clearType != UsedClear::None) {
    aLine->SetFloatClearTypeBefore(clearType);
  }

  // See if we should apply the block-start margin. If the block frame being
  // reflowed is a continuation, then we don't apply its block-start margin
  // because it's not significant. Otherwise, dig deeper.
  bool applyBStartMargin =
      !frame->GetPrevContinuation() && ShouldApplyBStartMargin(aState, aLine);
  if (applyBStartMargin) {
    // The HasClearance setting is only valid if ShouldApplyBStartMargin
    // returned false (in which case the block-start margin-root set our
    // clearance flag). Otherwise clear it now. We'll set it later on
    // ourselves if necessary.
    aLine->ClearHasClearance();
  }
  bool treatWithClearance = aLine->HasClearance();

  bool mightClearFloats = clearType != UsedClear::None;
  nsIFrame* floatAvoidingBlock = nullptr;
  if (!nsBlockFrame::BlockCanIntersectFloats(frame)) {
    mightClearFloats = true;
    floatAvoidingBlock = frame;
  }

  // If our block-start margin was counted as part of some parent's block-start
  // margin collapse, and we are being speculatively reflowed assuming this
  // frame DID NOT need clearance, then we need to check that
  // assumption.
  if (!treatWithClearance && !applyBStartMargin && mightClearFloats &&
      aState.mReflowInput.mDiscoveredClearance) {
    nscoord curBCoord = aState.mBCoord + aState.mPrevBEndMargin.Get();
    if (auto [clearBCoord, result] =
            aState.ClearFloats(curBCoord, clearType, floatAvoidingBlock);
        result != ClearFloatsResult::BCoordNoChange) {
      (void)clearBCoord;

      // Only record the first frame that requires clearance
      if (!*aState.mReflowInput.mDiscoveredClearance) {
        *aState.mReflowInput.mDiscoveredClearance = frame;
      }
      aState.mPrevChild = frame;
      // Exactly what we do now is flexible since we'll definitely be
      // reflowed.
      return;
    }
  }
  if (treatWithClearance) {
    applyBStartMargin = true;
  }

  nsIFrame* clearanceFrame = nullptr;
  const nscoord startingBCoord = aState.mBCoord;
  const CollapsingMargin incomingMargin = aState.mPrevBEndMargin;
  nscoord clearance;
  while (true) {
    clearance = 0;
    nscoord bStartMargin = 0;
    bool mayNeedRetry = false;
    bool clearedFloats = false;
    bool clearedPushedOrSplitFloat = false;
    if (applyBStartMargin) {
      // Precompute the blocks block-start margin value so that we can get the
      // correct available space (there might be a float that's
      // already been placed below the aState.mPrevBEndMargin

      // Setup a reflowInput to get the style computed block-start margin
      // value. We'll use a reason of `resize' so that we don't fudge
      // any incremental reflow input.

      // The availSpace here is irrelevant to our needs - all we want
      // out if this setup is the block-start margin value which doesn't depend
      // on the childs available space.
      // XXX building a complete ReflowInput just to get the block-start
      // margin seems like a waste. And we do this for almost every block!
      WritingMode wm = frame->GetWritingMode();
      LogicalSize availSpace = aState.ContentSize(wm);
      ReflowInput reflowInput(aState.mPresContext, aState.mReflowInput, frame,
                              availSpace);

      if (treatWithClearance) {
        aState.mBCoord += aState.mPrevBEndMargin.Get();
        aState.mPrevBEndMargin.Zero();
      }

      // Now compute the collapsed margin-block-start value into
      // aState.mPrevBEndMargin, assuming that all child margins
      // collapse down to clearanceFrame.
      brc.ComputeCollapsedBStartMargin(reflowInput, &aState.mPrevBEndMargin,
                                       clearanceFrame, &mayNeedRetry);

      // XXX optimization; we could check the collapsing children to see if they
      // are sure to require clearance, and so avoid retrying them

      if (clearanceFrame) {
        // Don't allow retries on the second pass. The clearance decisions for
        // the blocks whose block-start margins collapse with ours are now
        // fixed.
        mayNeedRetry = false;
      }

      if (!treatWithClearance && !clearanceFrame && mightClearFloats) {
        // We don't know if we need clearance and this is the first,
        // optimistic pass.  So determine whether *this block* needs
        // clearance. Note that we do not allow the decision for whether
        // this block has clearance to change on the second pass; that
        // decision is only allowed to be made under the optimistic
        // first pass.
        nscoord curBCoord = aState.mBCoord + aState.mPrevBEndMargin.Get();
        if (auto [clearBCoord, result] =
                aState.ClearFloats(curBCoord, clearType, floatAvoidingBlock);
            result != ClearFloatsResult::BCoordNoChange) {
          (void)clearBCoord;

          // Looks like we need clearance and we didn't know about it already.
          // So recompute collapsed margin
          treatWithClearance = true;
          // Remember this decision, needed for incremental reflow
          aLine->SetHasClearance();

          // Apply incoming margins
          aState.mBCoord += aState.mPrevBEndMargin.Get();
          aState.mPrevBEndMargin.Zero();

          // Compute the collapsed margin again, ignoring the incoming margin
          // this time
          mayNeedRetry = false;
          brc.ComputeCollapsedBStartMargin(reflowInput, &aState.mPrevBEndMargin,
                                           clearanceFrame, &mayNeedRetry);
        }
      }

      // Temporarily advance the running block-direction value so that the
      // GetFloatAvailableSpace method will return the right available space.
      // This undone as soon as the horizontal margins are computed.
      bStartMargin = aState.mPrevBEndMargin.Get();

      if (treatWithClearance) {
        nscoord currentBCoord = aState.mBCoord;
        // advance mBCoord to the clear position.
        auto [clearBCoord, result] =
            aState.ClearFloats(aState.mBCoord, clearType, floatAvoidingBlock);
        aState.mBCoord = clearBCoord;

        clearedFloats = result != ClearFloatsResult::BCoordNoChange;
        clearedPushedOrSplitFloat =
            result == ClearFloatsResult::FloatsPushedOrSplit;

        // Compute clearance. It's the amount we need to add to the block-start
        // border-edge of the frame, after applying collapsed margins
        // from the frame and its children, to get it to line up with
        // the block-end of the floats. The former is
        // currentBCoord + bStartMargin, the latter is the current
        // aState.mBCoord.
        // Note that negative clearance is possible
        clearance = aState.mBCoord - (currentBCoord + bStartMargin);

        // Add clearance to our block-start margin while we compute available
        // space for the frame
        bStartMargin += clearance;

        // Note that aState.mBCoord should stay where it is: at the block-start
        // border-edge of the frame
      } else {
        // Advance aState.mBCoord to the block-start border-edge of the frame.
        aState.mBCoord += bStartMargin;
      }
    }

    aLine->SetLineIsImpactedByFloat(false);

    // Here aState.mBCoord is the block-start border-edge of the block.
    // Compute the available space for the block
    nsFlowAreaRect floatAvailableSpace = aState.GetFloatAvailableSpace(cbWM);
    WritingMode wm = aState.mReflowInput.GetWritingMode();
    LogicalRect availSpace = aState.ComputeBlockAvailSpace(
        frame, floatAvailableSpace, (floatAvoidingBlock));

    // The check for
    //   (!aState.mReflowInput.mFlags.mIsTopOfPage || clearedFloats)
    // is to some degree out of paranoia:  if we reliably eat up block-start
    // margins at the top of the page as we ought to, it wouldn't be
    // needed.
    if ((!aState.mReflowInput.mFlags.mIsTopOfPage || clearedFloats) &&
        (availSpace.BSize(wm) < 0 || clearedPushedOrSplitFloat)) {
      // We know already that this child block won't fit on this
      // page/column due to the block-start margin or the clearance.  So we
      // need to get out of here now.  (If we don't, most blocks will handle
      // things fine, and report break-before, but zero-height blocks
      // won't, and will thus make their parent overly-large and force
      // *it* to be pushed in its entirety.)
      aState.mBCoord = startingBCoord;
      aState.mPrevBEndMargin = incomingMargin;
      if (ShouldAvoidBreakInside(aState.mReflowInput)) {
        SetBreakBeforeStatusBeforeLine(aState, aLine, aKeepReflowGoing);
      } else {
        PushTruncatedLine(aState, aLine, aKeepReflowGoing);
      }
      return;
    }

    // Now put the block-dir coordinate back to the start of the
    // block-start-margin + clearance.
    aState.mBCoord -= bStartMargin;
    availSpace.BStart(wm) -= bStartMargin;
    if (NS_UNCONSTRAINEDSIZE != availSpace.BSize(wm)) {
      availSpace.BSize(wm) += bStartMargin;
    }

    // Construct the reflow input for the block.
    Maybe<ReflowInput> childReflowInput;
    Maybe<LogicalSize> cbSize;
    LogicalSize availSize = availSpace.Size(wm);
    bool columnSetWrapperHasNoBSizeLeft = false;
    if (Style()->GetPseudoType() == PseudoStyleType::MozColumnContent) {
      // Calculate the multicol containing block's block size so that the
      // children with percentage block size get correct percentage basis.
      const ReflowInput* cbReflowInput =
          aState.mReflowInput.mParentReflowInput->mCBReflowInput;
      MOZ_ASSERT(cbReflowInput->mFrame->StyleColumn()->IsColumnContainerStyle(),
                 "Get unexpected reflow input of multicol containing block!");

      // Use column-width as the containing block's inline-size, i.e. the column
      // content's computed inline-size.
      cbSize.emplace(LogicalSize(wm, aState.mReflowInput.ComputedISize(),
                                 cbReflowInput->ComputedBSize())
                         .ConvertTo(frame->GetWritingMode(), wm));

      // If a ColumnSetWrapper is in a balancing column content, it may be
      // pushed or pulled back and forth between column contents. Always add
      // NS_FRAME_HAS_DIRTY_CHILDREN bit to it so that its ColumnSet children
      // can have a chance to reflow under current block size constraint.
      if (aState.mReflowInput.mFlags.mIsColumnBalancing &&
          frame->IsColumnSetWrapperFrame()) {
        frame->AddStateBits(NS_FRAME_HAS_DIRTY_CHILDREN);
      }
    } else if (IsColumnSetWrapperFrame()) {
      // If we are reflowing our ColumnSet children, we want to apply our block
      // size constraint to the available block size when constructing reflow
      // input for ColumnSet so that ColumnSet can use it to compute its max
      // column block size.
      if (frame->IsColumnSetFrame()) {
        nscoord contentBSize = aState.mReflowInput.ComputedBSize();
        if (aState.mReflowInput.ComputedMaxBSize() != NS_UNCONSTRAINEDSIZE) {
          contentBSize =
              std::min(contentBSize, aState.mReflowInput.ComputedMaxBSize());
        }
        if (contentBSize != NS_UNCONSTRAINEDSIZE) {
          // To get the remaining content block-size, subtract the content
          // block-size consumed by our previous continuations.
          contentBSize -= aState.mConsumedBSize;

          // ColumnSet is not the outermost frame in the column container, so it
          // cannot have any margin. We don't need to consider any margin that
          // can be generated by "box-decoration-break: clone" as we do in
          // BlockReflowState::ComputeBlockAvailSpace().
          const nscoord availContentBSize = std::max(
              0, contentBSize - (aState.mBCoord - aState.ContentBStart()));
          if (availSize.BSize(wm) >= availContentBSize) {
            availSize.BSize(wm) = availContentBSize;
            columnSetWrapperHasNoBSizeLeft = true;
          }
        }
      }
    }

    childReflowInput.emplace(aState.mPresContext, aState.mReflowInput, frame,
                             availSize.ConvertTo(frame->GetWritingMode(), wm),
                             cbSize);

    childReflowInput->mFlags.mColumnSetWrapperHasNoBSizeLeft =
        columnSetWrapperHasNoBSizeLeft;

    if (aLine->MovedFragments()) {
      // We only need to set this the first reflow, since if we reflow
      // again (and replace childReflowInput) we'll be reflowing it
      // again in the same fragment as the previous time.
      childReflowInput->mFlags.mMovedBlockFragments = true;
    }

    nsFloatManager::SavedState floatManagerState;
    nsReflowStatus frameReflowStatus;
    do {
      if (floatAvailableSpace.HasFloats()) {
        // Set if floatAvailableSpace.HasFloats() is true for any
        // iteration of the loop.
        aLine->SetLineIsImpactedByFloat(true);
      }

      // We might need to store into mDiscoveredClearance later if it's
      // currently null; we want to overwrite any writes that
      // brc.ReflowBlock() below does, so we need to remember now
      // whether it's empty.
      const bool shouldStoreClearance =
          aState.mReflowInput.mDiscoveredClearance &&
          !*aState.mReflowInput.mDiscoveredClearance;

      // Reflow the block into the available space
      if (mayNeedRetry || floatAvoidingBlock) {
        aState.FloatManager()->PushState(&floatManagerState);
      }

      if (mayNeedRetry) {
        childReflowInput->mDiscoveredClearance = &clearanceFrame;
      } else if (!applyBStartMargin) {
        childReflowInput->mDiscoveredClearance =
            aState.mReflowInput.mDiscoveredClearance;
      }

      frameReflowStatus.Reset();
      brc.ReflowBlock(availSpace, applyBStartMargin, aState.mPrevBEndMargin,
                      clearance, aLine.get(), *childReflowInput,
                      frameReflowStatus, aState);

      if (frameReflowStatus.IsInlineBreakBefore()) {
        // No need to retry this loop if there is a break opportunity before the
        // child block.
        break;
      }

      // Now the block has a height.  Using that height, get the
      // available space again and call ComputeBlockAvailSpace again.
      // If ComputeBlockAvailSpace gives a different result, we need to
      // reflow again.
      if (!floatAvoidingBlock) {
        break;
      }

      LogicalRect oldFloatAvailableSpaceRect(floatAvailableSpace.mRect);
      floatAvailableSpace = aState.GetFloatAvailableSpaceForBSize(
          cbWM, aState.mBCoord + bStartMargin, brc.GetMetrics().BSize(wm),
          &floatManagerState);
      NS_ASSERTION(floatAvailableSpace.mRect.BStart(wm) ==
                       oldFloatAvailableSpaceRect.BStart(wm),
                   "yikes");
      // Restore the height to the position of the next band.
      floatAvailableSpace.mRect.BSize(wm) =
          oldFloatAvailableSpaceRect.BSize(wm);
      // Determine whether the available space shrunk on either side,
      // because (the first time round) we now know the block's height,
      // and it may intersect additional floats, or (on later
      // iterations) because narrowing the width relative to the
      // previous time may cause the block to become taller.  Note that
      // since we're reflowing the block, narrowing the width might also
      // make it shorter, so we must pass aCanGrow as true.
      if (!AvailableSpaceShrunk(wm, oldFloatAvailableSpaceRect,
                                floatAvailableSpace.mRect, true)) {
        // The size and position we chose before are fine (i.e., they
        // don't cause intersecting with floats that requires a change
        // in size or position), so we're done.
        break;
      }

      bool advanced = false;
      if (!aState.FloatAvoidingBlockFitsInAvailSpace(floatAvoidingBlock,
                                                     floatAvailableSpace)) {
        // Advance to the next band.
        nscoord newBCoord = aState.mBCoord;
        if (aState.AdvanceToNextBand(floatAvailableSpace.mRect, &newBCoord)) {
          advanced = true;
        }
        // ClearFloats might be able to advance us further once we're there.
        std::tie(aState.mBCoord, std::ignore) =
            aState.ClearFloats(newBCoord, UsedClear::None, floatAvoidingBlock);

        // Start over with a new available space rect at the new height.
        floatAvailableSpace = aState.GetFloatAvailableSpaceWithState(
            cbWM, aState.mBCoord, ShapeType::ShapeOutside, &floatManagerState);
      }

      const LogicalRect oldAvailSpace = availSpace;
      availSpace = aState.ComputeBlockAvailSpace(frame, floatAvailableSpace,
                                                 (floatAvoidingBlock));

      if (!advanced && availSpace.IsEqualEdges(oldAvailSpace)) {
        break;
      }

      // We need another reflow.
      aState.FloatManager()->PopState(&floatManagerState);

      if (!treatWithClearance && !applyBStartMargin &&
          aState.mReflowInput.mDiscoveredClearance) {
        // We set shouldStoreClearance above to record only the first
        // frame that requires clearance.
        if (shouldStoreClearance) {
          *aState.mReflowInput.mDiscoveredClearance = frame;
        }
        aState.mPrevChild = frame;
        // Exactly what we do now is flexible since we'll definitely be
        // reflowed.
        return;
      }

      if (advanced) {
        // We're pushing down the border-box, so we don't apply margin anymore.
        // This should never cause us to move up since the call to
        // GetFloatAvailableSpaceForBSize above included the margin.
        applyBStartMargin = false;
        bStartMargin = 0;
        treatWithClearance = true;  // avoid hitting test above
        clearance = 0;
      }

      childReflowInput.reset();
      childReflowInput.emplace(
          aState.mPresContext, aState.mReflowInput, frame,
          availSpace.Size(wm).ConvertTo(frame->GetWritingMode(), wm));
    } while (true);

    if (mayNeedRetry && clearanceFrame) {
      // Found a clearance frame, so we need to reflow |frame| a second time.
      // Restore the states and start over again.
      aState.FloatManager()->PopState(&floatManagerState);
      aState.mBCoord = startingBCoord;
      aState.mPrevBEndMargin = incomingMargin;
      continue;
    }

    aState.mPrevChild = frame;

    if (childReflowInput->WillReflowAgainForClearance()) {
      return;
    }

#if defined(REFLOW_STATUS_COVERAGE)
    RecordReflowStatus(true, frameReflowStatus);
#endif

    if (frameReflowStatus.IsInlineBreakBefore()) {
      // None of the child block fits.
      if (ShouldAvoidBreakInside(aState.mReflowInput)) {
        SetBreakBeforeStatusBeforeLine(aState, aLine, aKeepReflowGoing);
      } else {
        PushTruncatedLine(aState, aLine, aKeepReflowGoing);
      }
    } else {
      // Note: line-break-after a block is a nop

      // Try to place the child block.
      // Don't force the block to fit if we have positive clearance, because
      // pushing it to the next page would give it more room.
      // Don't force the block to fit if it's impacted by a float. If it is,
      // then pushing it to the next page would give it more room. Note that
      // isImpacted doesn't include impact from the block's own floats.
      bool forceFit = aState.IsAdjacentWithBStart() && clearance <= 0 &&
                      !floatAvailableSpace.HasFloats();
      CollapsingMargin collapsedBEndMargin;
      OverflowAreas overflowAreas;
      *aKeepReflowGoing =
          brc.PlaceBlock(*childReflowInput, forceFit, aLine.get(),
                         collapsedBEndMargin, overflowAreas, frameReflowStatus);
      if (!frameReflowStatus.IsFullyComplete() &&
          ShouldAvoidBreakInside(aState.mReflowInput)) {
        *aKeepReflowGoing = false;
        aLine->MarkDirty();
      }

      if (aLine->SetCarriedOutBEndMargin(collapsedBEndMargin)) {
        LineIterator nextLine = aLine;
        ++nextLine;
        if (nextLine != LinesEnd()) {
          nextLine->MarkPreviousMarginDirty();
        }
      }

      if (Style()->GetPseudoType() == PseudoStyleType::MozScrolledContent) {
        auto lineFrameBounds = GetLineFrameInFlowBounds(*aLine, *frame);
        MOZ_ASSERT(aLine->GetChildCount() == 1,
                   "More than one child in block line?");
        // Inline-line (i.e. Multiple frames in one line) handled in one of
        // other callsites.
        aLine->SetInFlowChildBounds(lineFrameBounds);
      }

      aLine->SetOverflowAreas(overflowAreas);
      if (*aKeepReflowGoing) {
        // Some of the child block fit

        // Advance to new Y position
        nscoord newBCoord = aLine->BEnd();
        aState.mBCoord = newBCoord;

        // Continue the block frame now if it didn't completely fit in
        // the available space.
        if (!frameReflowStatus.IsFullyComplete()) {
          bool madeContinuation = CreateContinuationFor(aState, nullptr, frame);

          nsIFrame* nextFrame = frame->GetNextInFlow();
          NS_ASSERTION(nextFrame,
                       "We're supposed to have a next-in-flow by now");

          if (frameReflowStatus.IsIncomplete()) {
            // If nextFrame used to be an overflow container, make it a normal
            // block
            if (!madeContinuation &&
                nextFrame->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
              nsOverflowContinuationTracker::AutoFinish fini(
                  aState.mOverflowTracker, frame);
              nsContainerFrame* parent = nextFrame->GetParent();
              parent->StealFrame(nextFrame);
              if (parent != this) {
                ReparentFrame(nextFrame, parent, this);
              }
              mFrames.InsertFrame(nullptr, frame, nextFrame);
              madeContinuation = true;  // needs to be added to mLines
              nextFrame->RemoveStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
              frameReflowStatus.SetNextInFlowNeedsReflow();
            }

            // Push continuation to a new line, but only if we actually made
            // one.
            if (madeContinuation) {
              nsLineBox* line = NewLineBox(nextFrame, true);
              mLines.after_insert(aLine, line);
            }

            PushTruncatedLine(aState, aLine.next(), aKeepReflowGoing);

            // If we need to reflow the continuation of the block child,
            // then we'd better reflow our continuation
            if (frameReflowStatus.NextInFlowNeedsReflow()) {
              aState.mReflowStatus.SetNextInFlowNeedsReflow();
              // We also need to make that continuation's line dirty so
              // it gets reflowed when we reflow our next in flow. The
              // nif's line must always be either a line of the nif's
              // parent block (only if we didn't make a continuation) or
              // else one of our own overflow lines. In the latter case
              // the line is already marked dirty, so just handle the
              // first case.
              if (!madeContinuation) {
                nsBlockFrame* nifBlock = do_QueryFrame(nextFrame->GetParent());
                NS_ASSERTION(
                    nifBlock,
                    "A block's child's next in flow's parent must be a block!");
                for (auto& line : nifBlock->Lines()) {
                  if (line.Contains(nextFrame)) {
                    line.MarkDirty();
                    break;
                  }
                }
              }
            }

            // The block-end margin for a block is only applied on the last
            // flow block. Since we just continued the child block frame,
            // we know that line->mFirstChild is not the last flow block
            // therefore zero out the running margin value.
#ifdef NOISY_BLOCK_DIR_MARGINS
            ListTag(stdout);
            printf(": reflow incomplete, frame=");
            frame->ListTag(stdout);
            printf(" prevBEndMargin=%d, setting to zero\n",
                   aState.mPrevBEndMargin.get());
#endif
            aState.mPrevBEndMargin.Zero();
          } else {  // frame is complete but its overflow is not complete
            // Disconnect the next-in-flow and put it in our overflow tracker
            if (!madeContinuation &&
                !nextFrame->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
              // It already exists, but as a normal next-in-flow, so we need
              // to dig it out of the child lists.
              nextFrame->GetParent()->StealFrame(nextFrame);
            } else if (madeContinuation) {
              mFrames.RemoveFrame(nextFrame);
            }

            // Put it in our overflow list
            aState.mOverflowTracker->Insert(nextFrame, frameReflowStatus);
            aState.mReflowStatus.MergeCompletionStatusFrom(frameReflowStatus);

#ifdef NOISY_BLOCK_DIR_MARGINS
            ListTag(stdout);
            printf(": reflow complete but overflow incomplete for ");
            frame->ListTag(stdout);
            printf(" prevBEndMargin=%d collapsedBEndMargin=%d\n",
                   aState.mPrevBEndMargin.get(), collapsedBEndMargin.get());
#endif
            aState.mPrevBEndMargin = collapsedBEndMargin;
          }
        } else {  // frame is fully complete
#ifdef NOISY_BLOCK_DIR_MARGINS
          ListTag(stdout);
          printf(": reflow complete for ");
          frame->ListTag(stdout);
          printf(" prevBEndMargin=%d collapsedBEndMargin=%d\n",
                 aState.mPrevBEndMargin.get(), collapsedBEndMargin.get());
#endif
          aState.mPrevBEndMargin = collapsedBEndMargin;
        }
#ifdef NOISY_BLOCK_DIR_MARGINS
        ListTag(stdout);
        printf(": frame=");
        frame->ListTag(stdout);
        printf(" carriedOutBEndMargin=%d collapsedBEndMargin=%d => %d\n",
               brc.GetCarriedOutBEndMargin().get(), collapsedBEndMargin.get(),
               aState.mPrevBEndMargin.get());
#endif
      } else {
        if (!frameReflowStatus.IsFullyComplete()) {
          // The frame reported an incomplete status, but then it also didn't
          // fit.  This means we need to reflow it again so that it can
          // (again) report the incomplete status.
          frame->AddStateBits(NS_FRAME_HAS_DIRTY_CHILDREN);
        }

        if ((aLine == mLines.front() && !GetPrevInFlow()) ||
            ShouldAvoidBreakInside(aState.mReflowInput)) {
          // If it's our very first line *or* we're not at the top of the page
          // and we have page-break-inside:avoid, then we need to be pushed to
          // our parent's next-in-flow.
          SetBreakBeforeStatusBeforeLine(aState, aLine, aKeepReflowGoing);
        } else {
          // Push the line that didn't fit and any lines that follow it
          // to our next-in-flow.
          PushTruncatedLine(aState, aLine, aKeepReflowGoing);
        }
      }
    }
    break;  // out of the reflow retry loop
  }

#ifdef DEBUG
  VerifyLines(true);
#endif
}

// Returns true if an overflow-wrap break was used.
bool nsBlockFrame::ReflowInlineFrames(BlockReflowState& aState,
                                      LineIterator aLine,
                                      bool* aKeepReflowGoing) {
  *aKeepReflowGoing = true;
  bool usedOverflowWrap = false;

  aLine->SetLineIsImpactedByFloat(false);

  // Setup initial coordinate system for reflowing the inline frames
  // into. Apply a previous block frame's block-end margin first.
  if (ShouldApplyBStartMargin(aState, aLine)) {
    aState.mBCoord += aState.mPrevBEndMargin.Get();
  }
  nsFlowAreaRect floatAvailableSpace =
      aState.GetFloatAvailableSpace(GetWritingMode());

  LineReflowStatus lineReflowStatus;
  do {
    nscoord availableSpaceBSize = 0;
    aState.mLineBSize.reset();
    do {
      bool allowPullUp = true;
      nsIFrame* forceBreakInFrame = nullptr;
      int32_t forceBreakOffset = -1;
      gfxBreakPriority forceBreakPriority = gfxBreakPriority::eNoBreak;
      do {
        nsFloatManager::SavedState floatManagerState;
        aState.FloatManager()->PushState(&floatManagerState);

        // Once upon a time we allocated the first 30 nsLineLayout objects
        // on the stack, and then we switched to the heap.  At that time
        // these objects were large (1100 bytes on a 32 bit system).
        // Then the nsLineLayout object was shrunk to 156 bytes by
        // removing some internal buffers.  Given that it is so much
        // smaller, the complexity of 2 different ways of allocating
        // no longer makes sense.  Now we always allocate on the stack.
        nsLineLayout lineLayout(aState.mPresContext, aState.FloatManager(),
                                aState.mReflowInput, &aLine, nullptr);
        lineLayout.Init(&aState, aState.mMinLineHeight, aState.mLineNumber);
        if (forceBreakInFrame) {
          lineLayout.ForceBreakAtPosition(forceBreakInFrame, forceBreakOffset);
        }
        DoReflowInlineFrames(aState, lineLayout, aLine, floatAvailableSpace,
                             availableSpaceBSize, &floatManagerState,
                             aKeepReflowGoing, &lineReflowStatus, allowPullUp);
        usedOverflowWrap = lineLayout.EndLineReflow();

        if (LineReflowStatus::RedoNoPull == lineReflowStatus ||
            LineReflowStatus::RedoMoreFloats == lineReflowStatus ||
            LineReflowStatus::RedoNextBand == lineReflowStatus) {
          if (lineLayout.NeedsBackup()) {
            NS_ASSERTION(!forceBreakInFrame,
                         "Backing up twice; this should never be necessary");
            // If there is no saved break position, then this will set
            // set forceBreakInFrame to null and we won't back up, which is
            // correct.
            forceBreakInFrame = lineLayout.GetLastOptionalBreakPosition(
                &forceBreakOffset, &forceBreakPriority);
          } else {
            forceBreakInFrame = nullptr;
          }
          // restore the float manager state
          aState.FloatManager()->PopState(&floatManagerState);
          // Clear out float lists
          aState.mCurrentLineFloats.Clear();
          aState.mBelowCurrentLineFloats.Clear();
          aState.mNoWrapFloats.Clear();
        }

        // Don't allow pullup on a subsequent LineReflowStatus::RedoNoPull pass
        allowPullUp = false;
      } while (LineReflowStatus::RedoNoPull == lineReflowStatus);
    } while (LineReflowStatus::RedoMoreFloats == lineReflowStatus);
  } while (LineReflowStatus::RedoNextBand == lineReflowStatus);

  return usedOverflowWrap;
}

void nsBlockFrame::SetBreakBeforeStatusBeforeLine(BlockReflowState& aState,
                                                  LineIterator aLine,
                                                  bool* aKeepReflowGoing) {
  aState.mReflowStatus.SetInlineLineBreakBeforeAndReset();
  // Reflow the line again when we reflow at our new position.
  aLine->MarkDirty();
  *aKeepReflowGoing = false;
}

void nsBlockFrame::PushTruncatedLine(
    BlockReflowState& aState, LineIterator aLine, bool* aKeepReflowGoing,
    ComputeNewPageNameIfNeeded aComputeNewPageName) {
  PushLines(aState, aLine.prev());
  *aKeepReflowGoing = false;

  if (aComputeNewPageName == ComputeNewPageNameIfNeeded::Yes) {
    // mCanHaveClassABreakpoints can only be true during paginated reflow, and
    // we expect this function to only be called when the available bsize is
    // constrained.
    const WritingMode wm = GetWritingMode();
    const bool canBreakForPageNames =
        aState.mReflowInput.mFlags.mCanHaveClassABreakpoints &&
        !PresShell()->GetRootFrame()->GetWritingMode().IsOrthogonalTo(wm);
    if (canBreakForPageNames) {
      PresShell()->FrameConstructor()->MaybeSetNextPageContentFramePageName(
          aLine->mFirstChild);
    }
  }
  aState.mReflowStatus.SetIncomplete();
}

void nsBlockFrame::DoReflowInlineFrames(
    BlockReflowState& aState, nsLineLayout& aLineLayout, LineIterator aLine,
    nsFlowAreaRect& aFloatAvailableSpace, nscoord& aAvailableSpaceBSize,
    nsFloatManager::SavedState* aFloatStateBeforeLine, bool* aKeepReflowGoing,
    LineReflowStatus* aLineReflowStatus, bool aAllowPullUp) {
  // Forget all of the floats on the line
  aLine->ClearFloats();
  aState.mFloatOverflowAreas.Clear();

  // We need to set this flag on the line if any of our reflow passes
  // are impacted by floats.
  if (aFloatAvailableSpace.HasFloats()) {
    aLine->SetLineIsImpactedByFloat(true);
  }
#ifdef REALLY_NOISY_REFLOW
  printf("nsBlockFrame::DoReflowInlineFrames %p impacted = %d\n", this,
         aFloatAvailableSpace.HasFloats());
#endif

  WritingMode outerWM = aState.mReflowInput.GetWritingMode();
  WritingMode lineWM = WritingModeForLine(outerWM, aLine->mFirstChild);
  LogicalRect lineRect = aFloatAvailableSpace.mRect.ConvertTo(
      lineWM, outerWM, aState.ContainerSize());

  nscoord iStart = lineRect.IStart(lineWM);
  nscoord availISize = lineRect.ISize(lineWM);
  nscoord availBSize;
  if (aState.mReflowInput.AvailableBSize() == NS_UNCONSTRAINEDSIZE) {
    availBSize = NS_UNCONSTRAINEDSIZE;
  } else {
    /* XXX get the height right! */
    availBSize = lineRect.BSize(lineWM);
  }

  // Make sure to enable resize optimization before we call BeginLineReflow
  // because it might get disabled there
  aLine->EnableResizeReflowOptimization();

  aLineLayout.BeginLineReflow(iStart, aState.mBCoord, availISize, availBSize,
                              aFloatAvailableSpace.HasFloats(),
                              false /*XXX isTopOfPage*/, lineWM,
                              aState.mContainerSize, aState.mInsetForBalance);

  aState.mFlags.mIsLineLayoutEmpty = false;

  // XXX Unfortunately we need to know this before reflowing the first
  // inline frame in the line. FIX ME.
  if (0 == aLineLayout.GetLineNumber() &&
      HasAllStateBits(NS_BLOCK_HAS_FIRST_LETTER_CHILD |
                      NS_BLOCK_HAS_FIRST_LETTER_STYLE)) {
    aLineLayout.SetFirstLetterStyleOK(true);
  }
  NS_ASSERTION(!(HasAnyStateBits(NS_BLOCK_HAS_FIRST_LETTER_CHILD) &&
                 GetPrevContinuation()),
               "first letter child bit should only be on first continuation");

  // Reflow the frames that are already on the line first
  LineReflowStatus lineReflowStatus = LineReflowStatus::OK;
  int32_t i;
  nsIFrame* frame = aLine->mFirstChild;

  if (aFloatAvailableSpace.HasFloats()) {
    // There is a soft break opportunity at the start of the line, because
    // we can always move this line down below float(s).
    if (aLineLayout.NotifyOptionalBreakPosition(
            frame, 0, true, gfxBreakPriority::eNormalBreak)) {
      lineReflowStatus = LineReflowStatus::RedoNextBand;
    }
  }

  // need to repeatedly call GetChildCount here, because the child
  // count can change during the loop!
  for (i = 0;
       LineReflowStatus::OK == lineReflowStatus && i < aLine->GetChildCount();
       i++, frame = frame->GetNextSibling()) {
    SetLineCursorForDisplay(aLine);
    ReflowInlineFrame(aState, aLineLayout, aLine, frame, &lineReflowStatus);
    if (LineReflowStatus::OK != lineReflowStatus) {
      // It is possible that one or more of next lines are empty
      // (because of DeleteNextInFlowChild). If so, delete them now
      // in case we are finished.
      ++aLine;
      while ((aLine != LinesEnd()) && (0 == aLine->GetChildCount())) {
        // XXX Is this still necessary now that DeleteNextInFlowChild
        // uses DoRemoveFrame?
        nsLineBox* toremove = aLine;
        aLine = mLines.erase(aLine);
        NS_ASSERTION(nullptr == toremove->mFirstChild, "bad empty line");
        FreeLineBox(toremove);
        ClearLineCursors();
      }
      --aLine;

      NS_ASSERTION(lineReflowStatus != LineReflowStatus::Truncated,
                   "ReflowInlineFrame should never determine that a line "
                   "needs to go to the next page/column");
    }
  }

  // Don't pull up new frames into lines with continuation placeholders
  if (aAllowPullUp) {
    // Pull frames and reflow them until we can't
    while (LineReflowStatus::OK == lineReflowStatus) {
      frame = PullFrame(aState, aLine);
      if (!frame) {
        break;
      }

      while (LineReflowStatus::OK == lineReflowStatus) {
        int32_t oldCount = aLine->GetChildCount();
        SetLineCursorForDisplay(aLine);
        ReflowInlineFrame(aState, aLineLayout, aLine, frame, &lineReflowStatus);
        if (aLine->GetChildCount() != oldCount) {
          // We just created a continuation for aFrame AND its going
          // to end up on this line (e.g. :first-letter
          // situation). Therefore we have to loop here before trying
          // to pull another frame.
          frame = frame->GetNextSibling();
        } else {
          break;
        }
      }
    }
  }
  ClearLineCursors();

  aState.mFlags.mIsLineLayoutEmpty = aLineLayout.LineIsEmpty();

  // We only need to backup if the line isn't going to be reflowed again anyway
  bool needsBackup = aLineLayout.NeedsBackup() &&
                     (lineReflowStatus == LineReflowStatus::Stop ||
                      lineReflowStatus == LineReflowStatus::OK);
  if (needsBackup && aLineLayout.HaveForcedBreakPosition()) {
    NS_WARNING(
        "We shouldn't be backing up more than once! "
        "Someone must have set a break opportunity beyond the available width, "
        "even though there were better break opportunities before it");
    needsBackup = false;
  }
  if (needsBackup) {
    // We need to try backing up to before a text run
    // XXX It's possible, in fact not unusual, for the break opportunity to
    // already be the end of the line. We should detect that and optimize to not
    // re-do the line.
    if (aLineLayout.HasOptionalBreakPosition()) {
      // We can back up!
      lineReflowStatus = LineReflowStatus::RedoNoPull;
    }
  } else {
    // In case we reflow this line again, remember that we don't
    // need to force any breaking
    aLineLayout.ClearOptionalBreakPosition();
  }

  if (LineReflowStatus::RedoNextBand == lineReflowStatus) {
    // This happens only when we have a line that is impacted by
    // floats and the first element in the line doesn't fit with
    // the floats.
    //
    // If there's block space available, we either try to reflow the line
    // past the current band (if it's non-zero and the band definitely won't
    // widen around a shape-outside), otherwise we try one pixel down. If
    // there's no block space available, we push the line to the next
    // page/column.
    NS_ASSERTION(
        NS_UNCONSTRAINEDSIZE != aFloatAvailableSpace.mRect.BSize(outerWM),
        "unconstrained block size on totally empty line");

    // See the analogous code for blocks in BlockReflowState::ClearFloats.
    nscoord bandBSize = aFloatAvailableSpace.mRect.BSize(outerWM);
    if (bandBSize > 0 ||
        NS_UNCONSTRAINEDSIZE == aState.mReflowInput.AvailableBSize()) {
      NS_ASSERTION(bandBSize == 0 || aFloatAvailableSpace.HasFloats(),
                   "redo line on totally empty line with non-empty band...");
      // We should never hit this case if we've placed floats on the
      // line; if we have, then the GetFloatAvailableSpace call is wrong
      // and needs to happen after the caller pops the float manager
      // state.
      aState.FloatManager()->AssertStateMatches(aFloatStateBeforeLine);

      if (!aFloatAvailableSpace.MayWiden() && bandBSize > 0) {
        // Move it down far enough to clear the current band.
        aState.mBCoord += bandBSize;
      } else {
        // Move it down by one dev pixel.
        aState.mBCoord += aState.mPresContext->DevPixelsToAppUnits(1);
      }

      aFloatAvailableSpace = aState.GetFloatAvailableSpace(GetWritingMode());
    } else {
      // There's nowhere to retry placing the line, so we want to push
      // it to the next page/column where its contents can fit not
      // next to a float.
      lineReflowStatus = LineReflowStatus::Truncated;
      PushTruncatedLine(aState, aLine, aKeepReflowGoing);
    }

    // XXX: a small optimization can be done here when paginating:
    // if the new Y coordinate is past the end of the block then
    // push the line and return now instead of later on after we are
    // past the float.
  } else if (LineReflowStatus::Truncated != lineReflowStatus &&
             LineReflowStatus::RedoNoPull != lineReflowStatus) {
    // If we are propagating out a break-before status then there is
    // no point in placing the line.
    if (!aState.mReflowStatus.IsInlineBreakBefore()) {
      if (!PlaceLine(aState, aLineLayout, aLine, aFloatStateBeforeLine,
                     aFloatAvailableSpace, aAvailableSpaceBSize,
                     aKeepReflowGoing)) {
        lineReflowStatus = LineReflowStatus::RedoMoreFloats;
        // PlaceLine already called GetFloatAvailableSpaceForBSize or its
        // variant for us.
      }
    }
  }
#ifdef DEBUG
  if (gNoisyReflow) {
    IndentBy(stdout, gNoiseIndent);
    fmt::println("LineReflowStatus={}", ToString(lineReflowStatus));
  }
#endif

  if (aLineLayout.GetDirtyNextLine()) {
    // aLine may have been pushed to the overflow lines.
    FrameLines* overflowLines = GetOverflowLines();
    // We can't just compare iterators front() to aLine here, since they may be
    // in different lists.
    bool pushedToOverflowLines =
        overflowLines && overflowLines->mLines.front() == aLine.get();
    if (pushedToOverflowLines) {
      // aLine is stale, it's associated with the main line list but it should
      // be associated with the overflow line list now
      aLine = overflowLines->mLines.begin();
    }
    nsBlockInFlowLineIterator iter(this, aLine, pushedToOverflowLines);
    if (iter.Next() && iter.GetLine()->IsInline()) {
      iter.GetLine()->MarkDirty();
      if (iter.GetContainer() != this) {
        aState.mReflowStatus.SetNextInFlowNeedsReflow();
      }
    }
  }

  *aLineReflowStatus = lineReflowStatus;
}

/**
 * Reflow an inline frame. The reflow status is mapped from the frames
 * reflow status to the lines reflow status (not to our reflow status).
 * The line reflow status is simple: true means keep placing frames
 * on the line; false means don't (the line is done). If the line
 * has some sort of breaking affect then aLine's break-type will be set
 * to something other than UsedClear::None.
 */
void nsBlockFrame::ReflowInlineFrame(BlockReflowState& aState,
                                     nsLineLayout& aLineLayout,
                                     LineIterator aLine, nsIFrame* aFrame,
                                     LineReflowStatus* aLineReflowStatus) {
  MOZ_ASSERT(aFrame);
  *aLineReflowStatus = LineReflowStatus::OK;

#ifdef NOISY_FIRST_LETTER
  ListTag(stdout);
  printf(": reflowing ");
  aFrame->ListTag(stdout);
  printf(" reflowingFirstLetter=%s\n",
         aLineLayout.GetFirstLetterStyleOK() ? "on" : "off");
#endif

  if (aFrame->IsPlaceholderFrame()) {
    auto ph = static_cast<nsPlaceholderFrame*>(aFrame);
    ph->ForgetLineIsEmptySoFar();
  }

  // Reflow the inline frame
  nsReflowStatus frameReflowStatus;
  bool pushedFrame;
  aLineLayout.ReflowFrame(aFrame, frameReflowStatus, nullptr, pushedFrame);

  if (frameReflowStatus.NextInFlowNeedsReflow()) {
    aLineLayout.SetDirtyNextLine();
  }

#ifdef REALLY_NOISY_REFLOW
  aFrame->ListTag(stdout);
  printf(": status=%s\n", ToString(frameReflowStatus).c_str());
#endif

#if defined(REFLOW_STATUS_COVERAGE)
  RecordReflowStatus(false, frameReflowStatus);
#endif

  // Send post-reflow notification
  aState.mPrevChild = aFrame;

  /* XXX
     This is where we need to add logic to handle some odd behavior.
     For one thing, we should usually place at least one thing next
     to a left float, even when that float takes up all the width on a line.
     see bug 22496
  */

  // Process the child frames reflow status. There are 5 cases:
  // complete, not-complete, break-before, break-after-complete,
  // break-after-not-complete. There are two situations: we are a
  // block or we are an inline. This makes a total of 10 cases
  // (fortunately, there is some overlap).
  aLine->ClearForcedLineBreak();
  if (frameReflowStatus.IsInlineBreak() ||
      aState.mTrailingClearFromPIF != UsedClear::None) {
    // Always abort the line reflow (because a line break is the
    // minimal amount of break we do).
    *aLineReflowStatus = LineReflowStatus::Stop;

    // XXX what should aLine's break-type be set to in all these cases?
    if (frameReflowStatus.IsInlineBreakBefore()) {
      // Break-before cases.
      if (aFrame == aLine->mFirstChild) {
        // If we break before the first frame on the line then we must
        // be trying to place content where there's no room (e.g. on a
        // line with wide floats). Inform the caller to reflow the
        // line after skipping past a float.
        *aLineReflowStatus = LineReflowStatus::RedoNextBand;
      } else {
        // It's not the first child on this line so go ahead and split
        // the line. We will see the frame again on the next-line.
        SplitLine(aState, aLineLayout, aLine, aFrame, aLineReflowStatus);

        // If we're splitting the line because the frame didn't fit and it
        // was pushed, then mark the line as having word wrapped. We need to
        // know that if we're shrink wrapping our width
        if (pushedFrame) {
          aLine->SetLineWrapped(true);
        }
      }
    } else {
      MOZ_ASSERT(frameReflowStatus.IsInlineBreakAfter() ||
                     aState.mTrailingClearFromPIF != UsedClear::None,
                 "We should've handled inline break-before in the if-branch!");

      // If a float split and its prev-in-flow was followed by a <BR>, then
      // combine the <BR>'s float clear type with the inline's float clear type
      // (the inline will be the very next frame after the split float).
      UsedClear clearType = frameReflowStatus.FloatClearType();
      if (aState.mTrailingClearFromPIF != UsedClear::None) {
        clearType = nsLayoutUtils::CombineClearType(
            clearType, aState.mTrailingClearFromPIF);
        aState.mTrailingClearFromPIF = UsedClear::None;
      }
      // Break-after cases
      if (clearType != UsedClear::None || aLineLayout.GetLineEndsInBR()) {
        aLine->SetForcedLineBreakAfter(clearType);
      }
      if (frameReflowStatus.IsComplete()) {
        // Split line, but after the frame just reflowed
        SplitLine(aState, aLineLayout, aLine, aFrame->GetNextSibling(),
                  aLineReflowStatus);

        if (frameReflowStatus.IsInlineBreakAfter() &&
            !aLineLayout.GetLineEndsInBR()) {
          aLineLayout.SetDirtyNextLine();
        }
      }
    }
  }

  if (!frameReflowStatus.IsFullyComplete()) {
    // Create a continuation for the incomplete frame. Note that the
    // frame may already have a continuation.
    CreateContinuationFor(aState, aLine, aFrame);

    // Remember that the line has wrapped
    if (!aLineLayout.GetLineEndsInBR()) {
      aLine->SetLineWrapped(true);
    }

    // If we just ended a first-letter frame or reflowed a placeholder then
    // don't split the line and don't stop the line reflow...
    // But if we are going to stop anyways we'd better split the line.
    if ((!frameReflowStatus.FirstLetterComplete() &&
         !aFrame->IsPlaceholderFrame()) ||
        *aLineReflowStatus == LineReflowStatus::Stop) {
      // Split line after the current frame
      *aLineReflowStatus = LineReflowStatus::Stop;
      SplitLine(aState, aLineLayout, aLine, aFrame->GetNextSibling(),
                aLineReflowStatus);
    }
  }
}

bool nsBlockFrame::CreateContinuationFor(BlockReflowState& aState,
                                         nsLineBox* aLine, nsIFrame* aFrame) {
  nsIFrame* newFrame = nullptr;

  if (!aFrame->GetNextInFlow()) {
    newFrame =
        PresShell()->FrameConstructor()->CreateContinuingFrame(aFrame, this);

    mFrames.InsertFrame(nullptr, aFrame, newFrame);

    if (aLine) {
      aLine->NoteFrameAdded(newFrame);
    }
  }
#ifdef DEBUG
  VerifyLines(false);
#endif
  return !!newFrame;
}

void nsBlockFrame::SplitFloat(BlockReflowState& aState, nsIFrame* aFloat,
                              const nsReflowStatus& aFloatStatus) {
  MOZ_ASSERT(!aFloatStatus.IsFullyComplete(),
             "why split the frame if it's fully complete?");
  MOZ_ASSERT(aState.mBlock == this);

  nsIFrame* nextInFlow = aFloat->GetNextInFlow();
  if (nextInFlow) {
    nsContainerFrame* oldParent = nextInFlow->GetParent();
    oldParent->StealFrame(nextInFlow);
    if (oldParent != this) {
      ReparentFrame(nextInFlow, oldParent, this);
    }
    if (!aFloatStatus.IsOverflowIncomplete()) {
      nextInFlow->RemoveStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
    }
  } else {
    nextInFlow =
        PresShell()->FrameConstructor()->CreateContinuingFrame(aFloat, this);
  }
  if (aFloatStatus.IsOverflowIncomplete()) {
    nextInFlow->AddStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
  }

  UsedFloat floatStyle =
      aFloat->StyleDisplay()->UsedFloat(aState.mReflowInput.GetWritingMode());
  if (floatStyle == UsedFloat::Left) {
    aState.FloatManager()->SetSplitLeftFloatAcrossBreak();
  } else {
    MOZ_ASSERT(floatStyle == UsedFloat::Right, "Unexpected float side!");
    aState.FloatManager()->SetSplitRightFloatAcrossBreak();
  }

  aState.AppendPushedFloatChain(nextInFlow);
  if (MOZ_LIKELY(!HasAnyStateBits(NS_BLOCK_BFC)) ||
      MOZ_UNLIKELY(IsTrueOverflowContainer())) {
    aState.mReflowStatus.SetOverflowIncomplete();
  } else {
    aState.mReflowStatus.SetIncomplete();
  }
}

static bool CheckPlaceholderInLine(nsIFrame* aBlock, nsLineBox* aLine,
                                   nsIFrame* aFloat) {
  if (!aFloat) {
    return true;
  }
  NS_ASSERTION(!aFloat->GetPrevContinuation(),
               "float in a line should never be a continuation");
  NS_ASSERTION(!aFloat->HasAnyStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW),
               "float in a line should never be a pushed float");
  nsIFrame* ph = aFloat->FirstInFlow()->GetPlaceholderFrame();
  for (nsIFrame* f = ph; f; f = f->GetParent()) {
    if (f->GetParent() == aBlock) {
      return aLine->Contains(f);
    }
  }
  NS_ASSERTION(false, "aBlock is not an ancestor of aFrame!");
  return true;
}

void nsBlockFrame::SplitLine(BlockReflowState& aState,
                             nsLineLayout& aLineLayout, LineIterator aLine,
                             nsIFrame* aFrame,
                             LineReflowStatus* aLineReflowStatus) {
  MOZ_ASSERT(aLine->IsInline(), "illegal SplitLine on block line");

  int32_t pushCount =
      aLine->GetChildCount() - aLineLayout.GetCurrentSpanCount();
  MOZ_ASSERT(pushCount >= 0, "bad push count");

#ifdef DEBUG
  if (gNoisyReflow) {
    nsIFrame::IndentBy(stdout, gNoiseIndent);
    printf("split line: from line=%p pushCount=%d aFrame=",
           static_cast<void*>(aLine.get()), pushCount);
    if (aFrame) {
      aFrame->ListTag(stdout);
    } else {
      printf("(null)");
    }
    printf("\n");
    if (gReallyNoisyReflow) {
      aLine->List(stdout, gNoiseIndent + 1);
    }
  }
#endif

  if (0 != pushCount) {
    MOZ_ASSERT(aLine->GetChildCount() > pushCount, "bad push");
    MOZ_ASSERT(nullptr != aFrame, "whoops");
#ifdef DEBUG
    {
      nsIFrame* f = aFrame;
      int32_t count = pushCount;
      while (f && count > 0) {
        f = f->GetNextSibling();
        --count;
      }
      NS_ASSERTION(count == 0, "Not enough frames to push");
    }
#endif

    // Put frames being split out into their own line
    nsLineBox* newLine = NewLineBox(aLine, aFrame, pushCount);
    mLines.after_insert(aLine, newLine);
#ifdef DEBUG
    if (gReallyNoisyReflow) {
      newLine->List(stdout, gNoiseIndent + 1);
    }
#endif

    // Let line layout know that some frames are no longer part of its
    // state.
    aLineLayout.SplitLineTo(aLine->GetChildCount());

    // If floats have been placed whose placeholders have been pushed to the new
    // line, we need to reflow the old line again. We don't want to look at the
    // frames in the new line, because as a large paragraph is laid out the
    // we'd get O(N^2) performance. So instead we just check that the last
    // float and the last below-current-line float are still in aLine.
    if (!CheckPlaceholderInLine(
            this, aLine,
            aLine->HasFloats() ? aLine->Floats().LastElement() : nullptr) ||
        !CheckPlaceholderInLine(
            this, aLine,
            aState.mBelowCurrentLineFloats.SafeLastElement(nullptr))) {
      *aLineReflowStatus = LineReflowStatus::RedoNoPull;
    }

#ifdef DEBUG
    VerifyLines(true);
#endif
  }
}

bool nsBlockFrame::IsLastLine(BlockReflowState& aState, LineIterator aLine) {
  while (++aLine != LinesEnd()) {
    // There is another line
    if (0 != aLine->GetChildCount()) {
      // If the next line is a block line then this line is the last in a
      // group of inline lines.
      return aLine->IsBlock();
    }
    // The next line is empty, try the next one
  }

  // Try our next-in-flows lines to answer the question
  nsBlockFrame* nextInFlow = (nsBlockFrame*)GetNextInFlow();
  while (nullptr != nextInFlow) {
    for (const auto& line : nextInFlow->Lines()) {
      if (0 != line.GetChildCount()) {
        return line.IsBlock();
      }
    }
    nextInFlow = (nsBlockFrame*)nextInFlow->GetNextInFlow();
  }

  // This is the last line - so don't allow justification
  return true;
}

bool nsBlockFrame::PlaceLine(BlockReflowState& aState,
                             nsLineLayout& aLineLayout, LineIterator aLine,
                             nsFloatManager::SavedState* aFloatStateBeforeLine,
                             nsFlowAreaRect& aFlowArea,
                             nscoord& aAvailableSpaceBSize,
                             bool* aKeepReflowGoing) {
  // Try to position the floats in a nowrap context.
  aLineLayout.FlushNoWrapFloats();

  // Trim extra white-space from the line before placing the frames
  aLineLayout.TrimTrailingWhiteSpace();

  // Vertically align the frames on this line.
  //
  // According to the CSS2 spec, section 12.6.1, the "marker" box
  // participates in the height calculation of the list-item box's
  // first line box.
  //
  // There are exactly two places a ::marker can be placed: near the
  // first or second line. It's only placed on the second line in a
  // rare case: when the first line is empty.
  WritingMode wm = aState.mReflowInput.GetWritingMode();
  bool addedMarker = false;
  nsIFrame* outsideMarker = GetOutsideMarker();
  if (outsideMarker &&
      ((aLine == mLines.front() &&
        (!aLineLayout.IsZeroBSize() || (aLine == mLines.back()))) ||
       (mLines.front() != mLines.back() && 0 == mLines.front()->BSize() &&
        aLine == mLines.begin().next()))) {
    ReflowOutput metrics(aState.mReflowInput);
    ReflowOutsideMarker(outsideMarker, aState, metrics, aState.mBCoord);
    NS_ASSERTION(!MarkerIsEmpty(outsideMarker) || metrics.BSize(wm) == 0,
                 "empty ::marker frame took up space");
    aLineLayout.AddMarkerFrame(outsideMarker, metrics);
    addedMarker = true;
  }
  aLineLayout.VerticalAlignLine();

  // We want to consider the floats in the current line when determining
  // whether the float available space is shrunk. If mLineBSize doesn't
  // exist, we are in the first pass trying to place the line. Calling
  // GetFloatAvailableSpace() like we did in BlockReflowState::AddFloat()
  // for UpdateBand().

  // floatAvailableSpaceWithOldLineBSize is the float available space with
  // the old BSize, but including the floats that were added in this line.
  LogicalRect floatAvailableSpaceWithOldLineBSize =
      aState.mLineBSize.isNothing()
          ? aState.GetFloatAvailableSpace(wm, aLine->BStart()).mRect
          : aState
                .GetFloatAvailableSpaceForBSize(
                    wm, aLine->BStart(), aState.mLineBSize.value(), nullptr)
                .mRect;

  // As we redo for floats, we can't reduce the amount of BSize we're
  // checking.
  aAvailableSpaceBSize = std::max(aAvailableSpaceBSize, aLine->BSize());
  LogicalRect floatAvailableSpaceWithLineBSize =
      aState
          .GetFloatAvailableSpaceForBSize(wm, aLine->BStart(),
                                          aAvailableSpaceBSize, nullptr)
          .mRect;

  // If the available space between the floats is smaller now that we
  // know the BSize, return false (and cause another pass with
  // LineReflowStatus::RedoMoreFloats).  We ensure aAvailableSpaceBSize
  // never decreases, which means that we can't reduce the set of floats
  // we intersect, which means that the available space cannot grow.
  if (AvailableSpaceShrunk(wm, floatAvailableSpaceWithOldLineBSize,
                           floatAvailableSpaceWithLineBSize, false)) {
    // Prepare data for redoing the line.
    aState.mLineBSize = Some(aLine->BSize());

    // Since we want to redo the line, we update aFlowArea by using the
    // aFloatStateBeforeLine, which is the float manager's state before the
    // line is placed.
    LogicalRect oldFloatAvailableSpace(aFlowArea.mRect);
    aFlowArea = aState.GetFloatAvailableSpaceForBSize(
        wm, aLine->BStart(), aAvailableSpaceBSize, aFloatStateBeforeLine);

    NS_ASSERTION(
        aFlowArea.mRect.BStart(wm) == oldFloatAvailableSpace.BStart(wm),
        "yikes");
    // Restore the BSize to the position of the next band.
    aFlowArea.mRect.BSize(wm) = oldFloatAvailableSpace.BSize(wm);

    // Enforce both IStart() and IEnd() never move outwards to prevent
    // infinite grow-shrink loops.
    const nscoord iStartDiff =
        aFlowArea.mRect.IStart(wm) - oldFloatAvailableSpace.IStart(wm);
    const nscoord iEndDiff =
        aFlowArea.mRect.IEnd(wm) - oldFloatAvailableSpace.IEnd(wm);
    if (iStartDiff < 0) {
      aFlowArea.mRect.IStart(wm) -= iStartDiff;
      aFlowArea.mRect.ISize(wm) += iStartDiff;
    }
    if (iEndDiff > 0) {
      aFlowArea.mRect.ISize(wm) -= iEndDiff;
    }

    return false;
  }

#ifdef DEBUG
  if (!GetParent()->IsAbsurdSizeAssertSuppressed()) {
    static nscoord lastHeight = 0;
    if (ABSURD_SIZE(aLine->BStart())) {
      lastHeight = aLine->BStart();
      if (abs(aLine->BStart() - lastHeight) > ABSURD_COORD / 10) {
        nsIFrame::ListTag(stdout);
        printf(": line=%p y=%d line.bounds.height=%d\n",
               static_cast<void*>(aLine.get()), aLine->BStart(),
               aLine->BSize());
      }
    } else {
      lastHeight = 0;
    }
  }
#endif

  // Only block frames horizontally align their children because
  // inline frames "shrink-wrap" around their children (therefore
  // there is no extra horizontal space).
  const nsStyleText* styleText = StyleText();

  /**
   * We don't care checking for IsLastLine properly if we don't care (if it
   * can't change the used text-align value for the line).
   *
   * In other words, isLastLine really means isLastLineAndWeCare.
   */
  const bool isLastLine =
      !IsInSVGTextSubtree() &&
      styleText->TextAlignForLastLine() != styleText->mTextAlign &&
      (aLineLayout.GetLineEndsInBR() || IsLastLine(aState, aLine));

  aLineLayout.TextAlignLine(aLine, isLastLine);

  // From here on, pfd->mBounds rectangles are incorrect because bidi
  // might have moved frames around!
  OverflowAreas overflowAreas;
  aLineLayout.RelativePositionFrames(overflowAreas);
  if (Style()->GetPseudoType() == PseudoStyleType::MozScrolledContent) {
    Maybe<nsRect> inFlowBounds;
    for (nsIFrame* lineFrame : aLine->ChildFrames()) {
      auto lineFrameBounds = GetLineFrameInFlowBounds(*aLine, *lineFrame);
      if (!lineFrameBounds) {
        continue;
      }
      if (inFlowBounds) {
        *inFlowBounds = inFlowBounds->UnionEdges(*lineFrameBounds);
      } else {
        inFlowBounds = Some(*lineFrameBounds);
      }
    }
    aLine->SetInFlowChildBounds(inFlowBounds);
  }
  aLine->SetOverflowAreas(overflowAreas);
  if (addedMarker) {
    aLineLayout.RemoveMarkerFrame(GetOutsideMarker());
  }

  // Inline lines do not have margins themselves; however they are
  // impacted by prior block margins. If this line ends up having some
  // height then we zero out the previous block-end margin value that was
  // already applied to the line's starting Y coordinate. Otherwise we
  // leave it be so that the previous blocks block-end margin can be
  // collapsed with a block that follows.
  nscoord newBCoord;

  if (!aLine->CachedIsEmpty()) {
    // This line has some height. Therefore the application of the
    // previous-bottom-margin should stick.
    aState.mPrevBEndMargin.Zero();
    newBCoord = aLine->BEnd();
  } else {
    // Don't let the previous-bottom-margin value affect the newBCoord
    // coordinate (it was applied in ReflowInlineFrames speculatively)
    // since the line is empty.
    // We already called |ShouldApplyBStartMargin|, and if we applied it
    // then mShouldApplyBStartMargin is set.
    nscoord dy = aState.mFlags.mShouldApplyBStartMargin
                     ? -aState.mPrevBEndMargin.Get()
                     : 0;
    newBCoord = aState.mBCoord + dy;
  }

  if (!aState.mReflowStatus.IsFullyComplete() &&
      ShouldAvoidBreakInside(aState.mReflowInput)) {
    aLine->AppendFloats(std::move(aState.mCurrentLineFloats));
    SetBreakBeforeStatusBeforeLine(aState, aLine, aKeepReflowGoing);
    return true;
  }

  // See if the line fit (our first line always does).
  if (mLines.front() != aLine &&
      aState.ContentBSize() != NS_UNCONSTRAINEDSIZE &&
      newBCoord > aState.ContentBEnd()) {
    NS_ASSERTION(aState.mCurrentLine == aLine, "oops");
    if (ShouldAvoidBreakInside(aState.mReflowInput)) {
      // All our content doesn't fit, start on the next page.
      SetBreakBeforeStatusBeforeLine(aState, aLine, aKeepReflowGoing);
    } else {
      // Push aLine and all of its children and anything else that
      // follows to our next-in-flow.
      PushTruncatedLine(aState, aLine, aKeepReflowGoing);
    }
    return true;
  }

  // Note that any early return before this update of aState.mBCoord
  // must either (a) return false or (b) set aKeepReflowGoing to false.
  // Otherwise we'll keep reflowing later lines at an incorrect
  // position, and we might not come back and clean up the damage later.
  aState.mBCoord = newBCoord;

  // Add the already placed current-line floats to the line
  aLine->AppendFloats(std::move(aState.mCurrentLineFloats));

  // Any below current line floats to place?
  if (!aState.mBelowCurrentLineFloats.IsEmpty()) {
    // Reflow the below-current-line floats, which places on the line's
    // float list.
    aState.PlaceBelowCurrentLineFloats(aLine);
  }

  // When a line has floats, factor them into the overflow areas computations.
  if (aLine->HasFloats()) {
    // Union the float overflow areas (stored in aState) and the value computed
    // by the line layout code.
    OverflowAreas lineOverflowAreas = aState.mFloatOverflowAreas;
    lineOverflowAreas.UnionWith(aLine->GetOverflowAreas());
    aLine->SetOverflowAreas(lineOverflowAreas);
    if (Style()->GetPseudoType() == PseudoStyleType::MozScrolledContent) {
      Span<const nsIFrame* const> floats(aLine->Floats());
      // Guaranteed to have at least 1 element since `HasFloats()` is true.
      auto floatRect = GetNormalMarginRect(*floats[0]);
      for (const nsIFrame* f : floats.From(1)) {
        floatRect = floatRect.UnionEdges(GetNormalMarginRect(*f));
      }
      auto inFlowBounds = aLine->GetInFlowChildBounds();
      aLine->SetInFlowChildBounds(
          Some(inFlowBounds ? inFlowBounds->UnionEdges(floatRect) : floatRect));
    }

#ifdef NOISY_OVERFLOW_AREAS
    printf("%s: Line %p, InkOverflowRect=%s, ScrollableOverflowRect=%s\n",
           ListTag().get(), aLine.get(),
           ToString(aLine->InkOverflowRect()).c_str(),
           ToString(aLine->ScrollableOverflowRect()).c_str());
#endif
  }

  // Apply break-after clearing if necessary
  // This must stay in sync with |ReflowDirtyLines|.
  if (aLine->HasFloatClearTypeAfter()) {
    std::tie(aState.mBCoord, std::ignore) =
        aState.ClearFloats(aState.mBCoord, aLine->FloatClearTypeAfter());
  }
  return true;
}

void nsBlockFrame::PushLines(BlockReflowState& aState,
                             nsLineList::iterator aLineBefore) {
  // NOTE: aLineBefore is always a normal line, not an overflow line.
  // The following expression will assert otherwise.
  DebugOnly<bool> check = aLineBefore == mLines.begin();

  nsLineList::iterator overBegin(aLineBefore.next());

  // PushTruncatedPlaceholderLine sometimes pushes the first line.  Ugh.
  bool firstLine = overBegin == LinesBegin();

  if (overBegin != LinesEnd()) {
    // Remove floats in the lines from floats list.
    nsFrameList floats;
    CollectFloats(overBegin->mFirstChild, floats, true);

    if (floats.NotEmpty()) {
#ifdef DEBUG
      for (nsIFrame* f : floats) {
        MOZ_ASSERT(!f->HasAnyStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW),
                   "CollectFloats should've removed that bit");
      }
#endif
      // Push the floats onto the front of the overflow out-of-flows list
      nsAutoOOFFrameList oofs(this);
      oofs.mList.InsertFrames(nullptr, nullptr, std::move(floats));
    }

    // overflow lines can already exist in some cases, in particular,
    // when shrinkwrapping and we discover that the shrinkwap causes
    // the height of some child block to grow which creates additional
    // overflowing content. In such cases we must prepend the new
    // overflow to the existing overflow.
    FrameLines* overflowLines = RemoveOverflowLines();
    if (!overflowLines) {
      // XXXldb use presshell arena!
      overflowLines = new FrameLines();
    }
    if (overflowLines) {
      nsIFrame* lineBeforeLastFrame;
      if (firstLine) {
        lineBeforeLastFrame = nullptr;  // removes all frames
      } else {
        nsIFrame* f = overBegin->mFirstChild;
        lineBeforeLastFrame = f ? f->GetPrevSibling() : mFrames.LastChild();
        NS_ASSERTION(!f || lineBeforeLastFrame == aLineBefore->LastChild(),
                     "unexpected line frames");
      }
      nsFrameList pushedFrames = mFrames.TakeFramesAfter(lineBeforeLastFrame);
      overflowLines->mFrames.InsertFrames(nullptr, nullptr,
                                          std::move(pushedFrames));

      overflowLines->mLines.splice(overflowLines->mLines.begin(), mLines,
                                   overBegin, LinesEnd());
      NS_ASSERTION(!overflowLines->mLines.empty(), "should not be empty");
      // this takes ownership but it won't delete it immediately so we
      // can keep using it.
      SetOverflowLines(overflowLines);

      // Mark all the overflow lines dirty so that they get reflowed when
      // they are pulled up by our next-in-flow.

      nsLineBox* cursor = GetLineCursorForDisplay();

      // XXXldb Can this get called O(N) times making the whole thing O(N^2)?
      for (LineIterator line = overflowLines->mLines.begin(),
                        line_end = overflowLines->mLines.end();
           line != line_end; ++line) {
        if (line == cursor) {
          ClearLineCursors();
        }
        line->MarkDirty();
        line->MarkPreviousMarginDirty();
        line->SetMovedFragments();
        line->SetBoundsEmpty();
        if (line->HasFloats()) {
          line->ClearFloats();
        }
      }
    }
  }

#ifdef DEBUG
  VerifyOverflowSituation();
#endif
}

// The overflowLines property is stored as a pointer to a line list,
// which must be deleted.  However, the following functions all maintain
// the invariant that the property is never set if the list is empty.

bool nsBlockFrame::DrainOverflowLines() {
#ifdef DEBUG
  VerifyOverflowSituation();
#endif

  // Steal the prev-in-flow's overflow lines and prepend them.
  bool didFindOverflow = false;
  nsBlockFrame* prevBlock = static_cast<nsBlockFrame*>(GetPrevInFlow());
  if (prevBlock) {
    prevBlock->ClearLineCursors();
    FrameLines* overflowLines = prevBlock->RemoveOverflowLines();
    if (overflowLines) {
      // Make all the frames on the overflow line list mine.
      ReparentFrames(overflowLines->mFrames, prevBlock, this);

      // Collect overflow containers from our OverflowContainers list that are
      // continuations from the frames we picked up from our prev-in-flow, then
      // prepend those to ExcessOverflowContainers to ensure the continuations
      // are ordered.
      if (GetOverflowContainers()) {
        nsFrameList ocContinuations;
        for (auto* f : overflowLines->mFrames) {
          auto* cont = f;
          bool done = false;
          while (!done && (cont = cont->GetNextContinuation()) &&
                 cont->GetParent() == this) {
            bool onlyChild = !cont->GetPrevSibling() && !cont->GetNextSibling();
            if (cont->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER) &&
                TryRemoveFrame(OverflowContainersProperty(), cont)) {
              ocContinuations.AppendFrame(nullptr, cont);
              done = onlyChild;
              continue;
            }
            break;
          }
          if (done) {
            break;
          }
        }
        if (!ocContinuations.IsEmpty()) {
          if (nsFrameList* eoc = GetExcessOverflowContainers()) {
            eoc->InsertFrames(nullptr, nullptr, std::move(ocContinuations));
          } else {
            SetExcessOverflowContainers(std::move(ocContinuations));
          }
        }
      }

      // Make the overflow out-of-flow frames mine too.
      nsAutoOOFFrameList oofs(prevBlock);
      if (oofs.mList.NotEmpty()) {
        // In case we own any next-in-flows of any of the drained frames, then
        // move those to the PushedFloat list.
        nsFrameList pushedFloats;
        for (nsIFrame* f : oofs.mList) {
          nsIFrame* nif = f->GetNextInFlow();
          for (; nif && nif->GetParent() == this; nif = nif->GetNextInFlow()) {
            MOZ_ASSERT(nif->HasAnyStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW));
            RemoveFloat(nif);
            pushedFloats.AppendFrame(nullptr, nif);
          }
        }
        ReparentFrames(oofs.mList, prevBlock, this);
        EnsureFloats()->InsertFrames(nullptr, nullptr, std::move(oofs.mList));
        if (!pushedFloats.IsEmpty()) {
          nsFrameList* pf = EnsurePushedFloats();
          pf->InsertFrames(nullptr, nullptr, std::move(pushedFloats));
        }
      }

      if (!mLines.empty()) {
        // Remember to recompute the margins on the first line. This will
        // also recompute the correct deltaBCoord if necessary.
        mLines.front()->MarkPreviousMarginDirty();
      }
      // The overflow lines have already been marked dirty and their previous
      // margins marked dirty also.

      // Prepend the overflow frames/lines to our principal list.
      mFrames.InsertFrames(nullptr, nullptr, std::move(overflowLines->mFrames));
      mLines.splice(mLines.begin(), overflowLines->mLines);
      NS_ASSERTION(overflowLines->mLines.empty(), "splice should empty list");
      delete overflowLines;
      didFindOverflow = true;
    }
  }

  // Now append our own overflow lines.
  return DrainSelfOverflowList() || didFindOverflow;
}

bool nsBlockFrame::DrainSelfOverflowList() {
  UniquePtr<FrameLines> ourOverflowLines(RemoveOverflowLines());
  if (!ourOverflowLines) {
    return false;
  }

  // No need to reparent frames in our own overflow lines/oofs, because they're
  // already ours. But we should put overflow floats back in our floats list.
  // (explicit scope to remove the OOF list before VerifyOverflowSituation)
  {
    nsAutoOOFFrameList oofs(this);
    if (oofs.mList.NotEmpty()) {
#ifdef DEBUG
      for (nsIFrame* f : oofs.mList) {
        MOZ_ASSERT(!f->HasAnyStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW),
                   "CollectFloats should've removed that bit");
      }
#endif
      // The overflow floats go after our regular floats.
      EnsureFloats()->AppendFrames(nullptr, std::move(oofs).mList);
    }
  }
  if (!ourOverflowLines->mLines.empty()) {
    mFrames.AppendFrames(nullptr, std::move(ourOverflowLines->mFrames));
    mLines.splice(mLines.end(), ourOverflowLines->mLines);
  }

#ifdef DEBUG
  VerifyOverflowSituation();
#endif
  return true;
}

/**
 * Pushed floats are floats whose placeholders are in a previous
 * continuation.  They might themselves be next-continuations of a float
 * that partially fit in an earlier continuation, or they might be the
 * first continuation of a float that couldn't be placed at all.
 *
 * Pushed floats live permanently at the beginning of a block's float
 * list, where they must live *before* any floats whose placeholders are
 * in that block.
 *
 * Temporarily, during reflow, they also live on the pushed floats list,
 * which only holds them between (a) when one continuation pushes them to
 * its pushed floats list because they don't fit and (b) when the next
 * continuation pulls them onto the beginning of its float list.
 *
 * DrainPushedFloats sets up pushed floats the way we need them at the
 * start of reflow; they are then reflowed by ReflowPushedFloats (which
 * might push some of them on).  Floats with placeholders in this block
 * are reflowed by (BlockReflowState/nsLineLayout)::AddFloat, which
 * also maintains these invariants.
 *
 * DrainSelfPushedFloats moves any pushed floats from this block's own
 * pushed floats list back into floats list.  DrainPushedFloats additionally
 * moves frames from its prev-in-flow's pushed floats list into floats list.
 */
void nsBlockFrame::DrainSelfPushedFloats() {
  // If we're getting reflowed multiple times without our
  // next-continuation being reflowed, we might need to pull back floats
  // that we just put in the list to be pushed to our next-in-flow.
  // We don't want to pull back any next-in-flows of floats on our own
  // float list, and we only need to pull back first-in-flows whose
  // placeholders were in earlier blocks (since first-in-flows whose
  // placeholders are in this block will get pulled appropriately by
  // AddFloat, and will then be more likely to be in the correct order).
  mozilla::PresShell* presShell = PresShell();
  nsFrameList* ourPushedFloats = GetPushedFloats();
  if (ourPushedFloats) {
    nsFrameList* floats = GetFloats();

    // When we pull back floats, we want to put them with the pushed
    // floats, which must live at the start of our float list, but we
    // want them at the end of those pushed floats.
    // FIXME: This isn't quite right!  What if they're all pushed floats?
    nsIFrame* insertionPrevSibling = nullptr; /* beginning of list */
    for (nsIFrame* f = floats ? floats->FirstChild() : nullptr;
         f && f->HasAnyStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW);
         f = f->GetNextSibling()) {
      insertionPrevSibling = f;
    }

    nsIFrame* f = ourPushedFloats->LastChild();
    while (f) {
      nsIFrame* prevSibling = f->GetPrevSibling();

      nsPlaceholderFrame* placeholder = f->GetPlaceholderFrame();
      nsIFrame* floatOriginalParent =
          presShell->FrameConstructor()->GetFloatContainingBlock(placeholder);
      if (floatOriginalParent != this) {
        // This is a first continuation that was pushed from one of our
        // previous continuations.  Take it out of the pushed floats
        // list and put it in our floats list, before any of our
        // floats, but after other pushed floats.
        ourPushedFloats->RemoveFrame(f);
        if (!floats) {
          floats = EnsureFloats();
        }
        floats->InsertFrame(nullptr, insertionPrevSibling, f);
      }

      f = prevSibling;
    }

    if (ourPushedFloats->IsEmpty()) {
      StealPushedFloats()->Delete(presShell);
    }
  }
}

void nsBlockFrame::DrainPushedFloats() {
  DrainSelfPushedFloats();

  // After our prev-in-flow has completed reflow, it may have a pushed
  // floats list, containing floats that we need to own.  Take these.
  nsBlockFrame* prevBlock = static_cast<nsBlockFrame*>(GetPrevInFlow());
  if (prevBlock) {
    AutoFrameListPtr list(PresContext(), prevBlock->StealPushedFloats());
    if (list && list->NotEmpty()) {
      EnsureFloats()->InsertFrames(this, nullptr, std::move(*list));
    }
  }
}

nsBlockFrame::FrameLines* nsBlockFrame::GetOverflowLines() const {
  if (!HasOverflowLines()) {
    return nullptr;
  }
  FrameLines* prop = GetProperty(OverflowLinesProperty());
  NS_ASSERTION(
      prop && !prop->mLines.empty() &&
              prop->mLines.front()->GetChildCount() == 0
          ? prop->mFrames.IsEmpty()
          : prop->mLines.front()->mFirstChild == prop->mFrames.FirstChild(),
      "value should always be stored and non-empty when state set");
  return prop;
}

nsBlockFrame::FrameLines* nsBlockFrame::RemoveOverflowLines() {
  if (!HasOverflowLines()) {
    return nullptr;
  }
  FrameLines* prop = TakeProperty(OverflowLinesProperty());
  NS_ASSERTION(
      prop && !prop->mLines.empty() &&
              prop->mLines.front()->GetChildCount() == 0
          ? prop->mFrames.IsEmpty()
          : prop->mLines.front()->mFirstChild == prop->mFrames.FirstChild(),
      "value should always be stored and non-empty when state set");
  RemoveStateBits(NS_BLOCK_HAS_OVERFLOW_LINES);
  return prop;
}

void nsBlockFrame::DestroyOverflowLines() {
  NS_ASSERTION(HasOverflowLines(), "huh?");
  FrameLines* prop = TakeProperty(OverflowLinesProperty());
  NS_ASSERTION(prop && prop->mLines.empty(),
               "value should always be stored but empty when destroying");
  RemoveStateBits(NS_BLOCK_HAS_OVERFLOW_LINES);
  delete prop;
}

// This takes ownership of aOverflowLines.
// XXX We should allocate overflowLines from presShell arena!
void nsBlockFrame::SetOverflowLines(FrameLines* aOverflowLines) {
  NS_ASSERTION(aOverflowLines, "null lines");
  NS_ASSERTION(!aOverflowLines->mLines.empty(), "empty lines");
  NS_ASSERTION(aOverflowLines->mLines.front()->mFirstChild ==
                   aOverflowLines->mFrames.FirstChild(),
               "invalid overflow lines / frames");
  NS_ASSERTION(!HasAnyStateBits(NS_BLOCK_HAS_OVERFLOW_LINES),
               "Overwriting existing overflow lines");

  // Verify that we won't overwrite an existing overflow list
  NS_ASSERTION(!GetProperty(OverflowLinesProperty()), "existing overflow list");
  SetProperty(OverflowLinesProperty(), aOverflowLines);
  AddStateBits(NS_BLOCK_HAS_OVERFLOW_LINES);
}

nsFrameList* nsBlockFrame::GetOverflowOutOfFlows() const {
  if (!HasAnyStateBits(NS_BLOCK_HAS_OVERFLOW_OUT_OF_FLOWS)) {
    return nullptr;
  }
  nsFrameList* result = GetProperty(OverflowOutOfFlowsProperty());
  NS_ASSERTION(result, "value should always be non-empty when state set");
  return result;
}

void nsBlockFrame::SetOverflowOutOfFlows(nsFrameList&& aList,
                                         nsFrameList* aPropValue) {
  MOZ_ASSERT(
      HasAnyStateBits(NS_BLOCK_HAS_OVERFLOW_OUT_OF_FLOWS) == !!aPropValue,
      "state does not match value");

  if (aList.IsEmpty()) {
    if (!HasAnyStateBits(NS_BLOCK_HAS_OVERFLOW_OUT_OF_FLOWS)) {
      return;
    }
    nsFrameList* list = TakeProperty(OverflowOutOfFlowsProperty());
    NS_ASSERTION(aPropValue == list, "prop value mismatch");
    list->Clear();
    list->Delete(PresShell());
    RemoveStateBits(NS_BLOCK_HAS_OVERFLOW_OUT_OF_FLOWS);
  } else if (HasAnyStateBits(NS_BLOCK_HAS_OVERFLOW_OUT_OF_FLOWS)) {
    NS_ASSERTION(aPropValue == GetProperty(OverflowOutOfFlowsProperty()),
                 "prop value mismatch");
    *aPropValue = std::move(aList);
  } else {
    SetProperty(OverflowOutOfFlowsProperty(),
                new (PresShell()) nsFrameList(std::move(aList)));
    AddStateBits(NS_BLOCK_HAS_OVERFLOW_OUT_OF_FLOWS);
  }
}

nsIFrame* nsBlockFrame::GetInsideMarker() const {
  if (!HasMarker()) {
    return nullptr;
  }
  if (nsIFrame* frame = GetProperty(InsideMarkerProperty())) {
    return frame;
  }
  return nullptr;
}

nsIFrame* nsBlockFrame::GetOutsideMarker() const {
  nsFrameList* list = GetOutsideMarkerList();
  return list ? list->FirstChild() : nullptr;
}

nsFrameList* nsBlockFrame::GetOutsideMarkerList() const {
  if (!HasMarker()) {
    return nullptr;
  }
  if (nsFrameList* list = GetProperty(OutsideMarkerProperty())) {
    MOZ_ASSERT(list->GetLength() == 1, "bogus outside ::marker list");
    return list;
  }
  return nullptr;
}

bool nsBlockFrame::HasFloats() const {
  const bool isStateBitSet = HasAnyStateBits(NS_BLOCK_HAS_FLOATS);
  MOZ_ASSERT(
      isStateBitSet == HasProperty(FloatsProperty()),
      "State bit should accurately reflect presence/absence of the property!");
  return isStateBitSet;
}

nsFrameList* nsBlockFrame::GetFloats() const {
  if (!HasFloats()) {
    return nullptr;
  }
  nsFrameList* list = GetProperty(FloatsProperty());
  MOZ_ASSERT(list, "List should always be valid when the property is set!");
  MOZ_ASSERT(list->NotEmpty(),
             "Someone forgot to delete the list when it is empty!");
  return list;
}

nsFrameList* nsBlockFrame::EnsureFloats() {
  nsFrameList* list = GetFloats();
  if (list) {
    return list;
  }
  list = new (PresShell()) nsFrameList;
  SetProperty(FloatsProperty(), list);
  AddStateBits(NS_BLOCK_HAS_FLOATS);
  return list;
}

nsFrameList* nsBlockFrame::StealFloats() {
  if (!HasFloats()) {
    return nullptr;
  }
  nsFrameList* list = TakeProperty(FloatsProperty());
  RemoveStateBits(NS_BLOCK_HAS_FLOATS);
  MOZ_ASSERT(list, "List should always be valid when the property is set!");
  return list;
}

bool nsBlockFrame::HasPushedFloats() const {
  const bool isStateBitSet = HasAnyStateBits(NS_BLOCK_HAS_PUSHED_FLOATS);
  MOZ_ASSERT(
      isStateBitSet == HasProperty(PushedFloatsProperty()),
      "State bit should accurately reflect presence/absence of the property!");
  return isStateBitSet;
}

nsFrameList* nsBlockFrame::GetPushedFloats() const {
  if (!HasPushedFloats()) {
    return nullptr;
  }
  nsFrameList* list = GetProperty(PushedFloatsProperty());
  MOZ_ASSERT(list, "List should always be valid when the property is set!");
  MOZ_ASSERT(list->NotEmpty(),
             "Someone forgot to delete the list when it is empty!");
  return list;
}

nsFrameList* nsBlockFrame::EnsurePushedFloats() {
  nsFrameList* result = GetPushedFloats();
  if (result) {
    return result;
  }

  result = new (PresShell()) nsFrameList;
  SetProperty(PushedFloatsProperty(), result);
  AddStateBits(NS_BLOCK_HAS_PUSHED_FLOATS);

  return result;
}

nsFrameList* nsBlockFrame::StealPushedFloats() {
  if (!HasPushedFloats()) {
    return nullptr;
  }
  nsFrameList* list = TakeProperty(PushedFloatsProperty());
  RemoveStateBits(NS_BLOCK_HAS_PUSHED_FLOATS);
  MOZ_ASSERT(list, "List should always be valid when the property is set!");
  return list;
}

//////////////////////////////////////////////////////////////////////
// Frame list manipulation routines

void nsBlockFrame::AppendFrames(ChildListID aListID, nsFrameList&& aFrameList) {
  if (aFrameList.IsEmpty()) {
    return;
  }
  if (aListID != FrameChildListID::Principal) {
    if (FrameChildListID::Float == aListID) {
      DrainSelfPushedFloats();  // ensure the last frame is in floats list.
      EnsureFloats()->AppendFrames(nullptr, std::move(aFrameList));
      return;
    }
    MOZ_ASSERT(FrameChildListID::NoReflowPrincipal == aListID,
               "unexpected child list");
  }

  // Find the proper last-child for where the append should go
  nsIFrame* lastKid = mFrames.LastChild();
  NS_ASSERTION(
      (mLines.empty() ? nullptr : mLines.back()->LastChild()) == lastKid,
      "out-of-sync mLines / mFrames");

#ifdef NOISY_REFLOW_REASON
  ListTag(stdout);
  printf(": append ");
  for (nsIFrame* frame : aFrameList) {
    frame->ListTag(stdout);
  }
  if (lastKid) {
    printf(" after ");
    lastKid->ListTag(stdout);
  }
  printf("\n");
#endif

  if (IsInSVGTextSubtree()) {
    MOZ_ASSERT(GetParent()->IsSVGTextFrame(),
               "unexpected block frame in SVG text");
    // Workaround for bug 1399425 in case this bit has been removed from the
    // SVGTextFrame just before the parser adds more descendant nodes.
    GetParent()->AddStateBits(NS_STATE_SVG_TEXT_CORRESPONDENCE_DIRTY);
  }

  AddFrames(std::move(aFrameList), lastKid, nullptr);
  if (aListID != FrameChildListID::NoReflowPrincipal) {
    PresShell()->FrameNeedsReflow(
        this, IntrinsicDirty::FrameAndAncestors,
        NS_FRAME_HAS_DIRTY_CHILDREN);  // XXX sufficient?
  }
}

void nsBlockFrame::InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                                const nsLineList::iterator* aPrevFrameLine,
                                nsFrameList&& aFrameList) {
  NS_ASSERTION(!aPrevFrame || aPrevFrame->GetParent() == this,
               "inserting after sibling frame with different parent");

  if (aListID != FrameChildListID::Principal) {
    if (FrameChildListID::Float == aListID) {
      DrainSelfPushedFloats();  // ensure aPrevFrame is in floats list.
      EnsureFloats()->InsertFrames(this, aPrevFrame, std::move(aFrameList));
      return;
    }
    MOZ_ASSERT(FrameChildListID::NoReflowPrincipal == aListID,
               "unexpected child list");
  }

#ifdef NOISY_REFLOW_REASON
  ListTag(stdout);
  printf(": insert ");
  for (nsIFrame* frame : aFrameList) {
    frame->ListTag(stdout);
  }
  if (aPrevFrame) {
    printf(" after ");
    aPrevFrame->ListTag(stdout);
  }
  printf("\n");
#endif

  AddFrames(std::move(aFrameList), aPrevFrame, aPrevFrameLine);
  if (aListID != FrameChildListID::NoReflowPrincipal) {
    PresShell()->FrameNeedsReflow(
        this, IntrinsicDirty::FrameAndAncestors,
        NS_FRAME_HAS_DIRTY_CHILDREN);  // XXX sufficient?
  }
}

void nsBlockFrame::RemoveFrame(DestroyContext& aContext, ChildListID aListID,
                               nsIFrame* aOldFrame) {
#ifdef NOISY_REFLOW_REASON
  ListTag(stdout);
  printf(": remove ");
  aOldFrame->ListTag(stdout);
  printf("\n");
#endif

  if (aListID == FrameChildListID::Principal) {
    bool hasFloats = BlockHasAnyFloats(aOldFrame);
    DoRemoveFrame(aContext, aOldFrame, REMOVE_FIXED_CONTINUATIONS);
    if (hasFloats) {
      MarkSameFloatManagerLinesDirty(this);
    }
  } else if (FrameChildListID::Float == aListID) {
    // Make sure to mark affected lines dirty for the float frame
    // we are removing; this way is a bit messy, but so is the rest of the code.
    // See bug 390762.
    NS_ASSERTION(!aOldFrame->GetPrevContinuation(),
                 "RemoveFrame should not be called on pushed floats.");
    for (nsIFrame* f = aOldFrame;
         f && !f->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
         f = f->GetNextContinuation()) {
      MarkSameFloatManagerLinesDirty(
          static_cast<nsBlockFrame*>(f->GetParent()));
    }
    DoRemoveFloats(aContext, aOldFrame);
  } else if (FrameChildListID::NoReflowPrincipal == aListID) {
    // Skip the call to |FrameNeedsReflow| below by returning now.
    DoRemoveFrame(aContext, aOldFrame, REMOVE_FIXED_CONTINUATIONS);
    return;
  } else {
    MOZ_CRASH("unexpected child list");
  }

  PresShell()->FrameNeedsReflow(
      this, IntrinsicDirty::FrameAndAncestors,
      NS_FRAME_HAS_DIRTY_CHILDREN);  // XXX sufficient?
}

static bool ShouldPutNextSiblingOnNewLine(nsIFrame* aLastFrame) {
  LayoutFrameType type = aLastFrame->Type();
  if (type == LayoutFrameType::Br) {
    return true;
  }
  // XXX the TEXT_OFFSETS_NEED_FIXING check is a wallpaper for bug 822910.
  if (type == LayoutFrameType::Text &&
      !aLastFrame->HasAnyStateBits(TEXT_OFFSETS_NEED_FIXING)) {
    return aLastFrame->HasSignificantTerminalNewline();
  }
  return false;
}

void nsBlockFrame::AddFrames(nsFrameList&& aFrameList, nsIFrame* aPrevSibling,
                             const nsLineList::iterator* aPrevSiblingLine) {
  // Clear our line cursor, since our lines may change.
  ClearLineCursors();

  if (aFrameList.IsEmpty()) {
    return;
  }

  // Attempt to find the line that contains the previous sibling
  nsLineList* lineList = &mLines;
  nsFrameList* frames = &mFrames;
  nsLineList::iterator prevSibLine;
  int32_t prevSiblingIndex;
  if (aPrevSiblingLine) {
    MOZ_ASSERT(aPrevSibling);
    prevSibLine = *aPrevSiblingLine;
    FrameLines* overflowLines = GetOverflowLines();
    MOZ_ASSERT(prevSibLine.IsInSameList(mLines.begin()) ||
                   (overflowLines &&
                    prevSibLine.IsInSameList(overflowLines->mLines.begin())),
               "must be one of our line lists");
    if (overflowLines) {
      // We need to find out which list it's actually in.  Assume that
      // *if* we have overflow lines, that our primary lines aren't
      // huge, but our overflow lines might be.
      nsLineList::iterator line = mLines.begin(), lineEnd = mLines.end();
      while (line != lineEnd) {
        if (line == prevSibLine) {
          break;
        }
        ++line;
      }
      if (line == lineEnd) {
        // By elimination, the line must be in our overflow lines.
        lineList = &overflowLines->mLines;
        frames = &overflowLines->mFrames;
      }
    }

    nsLineList::iterator nextLine = prevSibLine.next();
    nsIFrame* lastFrameInLine = nextLine == lineList->end()
                                    ? frames->LastChild()
                                    : nextLine->mFirstChild->GetPrevSibling();
    prevSiblingIndex = prevSibLine->RLIndexOf(aPrevSibling, lastFrameInLine);
    MOZ_ASSERT(prevSiblingIndex >= 0,
               "aPrevSibling must be in aPrevSiblingLine");
  } else {
    prevSibLine = lineList->end();
    prevSiblingIndex = -1;
    if (aPrevSibling) {
      // XXX_perf This is technically O(N^2) in some cases, but by using
      // RFind instead of Find, we make it O(N) in the most common case,
      // which is appending content.

      // Find the line that contains the previous sibling
      if (!nsLineBox::RFindLineContaining(aPrevSibling, lineList->begin(),
                                          prevSibLine, mFrames.LastChild(),
                                          &prevSiblingIndex)) {
        // Not in mLines - try overflow lines.
        FrameLines* overflowLines = GetOverflowLines();
        bool found = false;
        if (overflowLines) {
          prevSibLine = overflowLines->mLines.end();
          prevSiblingIndex = -1;
          found = nsLineBox::RFindLineContaining(
              aPrevSibling, overflowLines->mLines.begin(), prevSibLine,
              overflowLines->mFrames.LastChild(), &prevSiblingIndex);
        }
        if (MOZ_LIKELY(found)) {
          lineList = &overflowLines->mLines;
          frames = &overflowLines->mFrames;
        } else {
          // Note: defensive code! RFindLineContaining must not return
          // false in this case, so if it does...
          MOZ_ASSERT_UNREACHABLE("prev sibling not in line list");
          aPrevSibling = nullptr;
          prevSibLine = lineList->end();
        }
      }
    }
  }

  // Find the frame following aPrevSibling so that we can join up the
  // two lists of frames.
  if (aPrevSibling) {
    // Split line containing aPrevSibling in two if the insertion
    // point is somewhere in the middle of the line.
    int32_t rem = prevSibLine->GetChildCount() - prevSiblingIndex - 1;
    if (rem) {
      // Split the line in two where the frame(s) are being inserted.
      nsLineBox* line =
          NewLineBox(prevSibLine, aPrevSibling->GetNextSibling(), rem);
      lineList->after_insert(prevSibLine, line);
      // Mark prevSibLine dirty and as needing textrun invalidation, since
      // we may be breaking up text in the line. Its previous line may also
      // need to be invalidated because it may be able to pull some text up.
      MarkLineDirty(prevSibLine, lineList);
      // The new line will also need its textruns recomputed because of the
      // frame changes.
      line->MarkDirty();
      line->SetInvalidateTextRuns(true);
    }
  } else if (!lineList->empty()) {
    lineList->front()->MarkDirty();
    lineList->front()->SetInvalidateTextRuns(true);
  }
  const nsFrameList::Slice& newFrames =
      frames->InsertFrames(nullptr, aPrevSibling, std::move(aFrameList));

  // Walk through the new frames being added and update the line data
  // structures to fit.
  for (nsIFrame* newFrame : newFrames) {
    NS_ASSERTION(!aPrevSibling || aPrevSibling->GetNextSibling() == newFrame,
                 "Unexpected aPrevSibling");
    NS_ASSERTION(
        !newFrame->IsPlaceholderFrame() ||
            (!newFrame->IsAbsolutelyPositioned() && !newFrame->IsFloating()),
        "Placeholders should not float or be positioned");

    bool isBlock = newFrame->IsBlockOutside();

    // If the frame is a block frame, or if there is no previous line or if the
    // previous line is a block line we need to make a new line.  We also make
    // a new line, as an optimization, in the two cases we know we'll need it:
    // if the previous line ended with a <br>, or if it has significant
    // whitespace and ended in a newline.
    if (isBlock || prevSibLine == lineList->end() || prevSibLine->IsBlock() ||
        (aPrevSibling && ShouldPutNextSiblingOnNewLine(aPrevSibling))) {
      // Create a new line for the frame and add its line to the line
      // list.
      nsLineBox* line = NewLineBox(newFrame, isBlock);
      if (prevSibLine != lineList->end()) {
        // Append new line after prevSibLine
        lineList->after_insert(prevSibLine, line);
        ++prevSibLine;
      } else {
        // New line is going before the other lines
        lineList->push_front(line);
        prevSibLine = lineList->begin();
      }
    } else {
      prevSibLine->NoteFrameAdded(newFrame);
      // We're adding inline content to prevSibLine, so we need to mark it
      // dirty, ensure its textruns are recomputed, and possibly do the same
      // to its previous line since that line may be able to pull content up.
      MarkLineDirty(prevSibLine, lineList);
    }

    aPrevSibling = newFrame;
  }

#ifdef DEBUG
  MOZ_ASSERT(aFrameList.IsEmpty());
  VerifyLines(true);
#endif
}

nsContainerFrame* nsBlockFrame::GetRubyContentPseudoFrame() {
  auto* firstChild = PrincipalChildList().FirstChild();
  if (firstChild && firstChild->IsRubyFrame() &&
      firstChild->Style()->GetPseudoType() ==
          PseudoStyleType::MozBlockRubyContent) {
    return static_cast<nsContainerFrame*>(firstChild);
  }
  return nullptr;
}

nsContainerFrame* nsBlockFrame::GetContentInsertionFrame() {
  // 'display:block ruby' use the inner (Ruby) frame for insertions.
  if (auto* rubyContentPseudoFrame = GetRubyContentPseudoFrame()) {
    return rubyContentPseudoFrame;
  }
  return this;
}

void nsBlockFrame::AppendDirectlyOwnedAnonBoxes(
    nsTArray<OwnedAnonBox>& aResult) {
  if (auto* rubyContentPseudoFrame = GetRubyContentPseudoFrame()) {
    aResult.AppendElement(OwnedAnonBox(rubyContentPseudoFrame));
  }
}

void nsBlockFrame::RemoveFloatFromFloatCache(nsIFrame* aFloat) {
  // Find which line contains the float, so we can update
  // the float cache.
  for (auto& line : Lines()) {
    if (line.IsInline() && line.RemoveFloat(aFloat)) {
      break;
    }
  }
}

void nsBlockFrame::RemoveFloat(nsIFrame* aFloat) {
  MOZ_ASSERT(aFloat);

  // Floats live in floats list, pushed floats list, or overflow out-of-flow
  // list.
  MOZ_ASSERT(
      GetChildList(FrameChildListID::Float).ContainsFrame(aFloat) ||
          GetChildList(FrameChildListID::PushedFloats).ContainsFrame(aFloat) ||
          GetChildList(FrameChildListID::OverflowOutOfFlow)
              .ContainsFrame(aFloat),
      "aFloat is not our child or on an unexpected frame list");

  bool didStartRemovingFloat = false;
  if (nsFrameList* floats = GetFloats()) {
    didStartRemovingFloat = true;
    if (floats->StartRemoveFrame(aFloat)) {
      if (floats->IsEmpty()) {
        StealFloats()->Delete(PresShell());
      }
      return;
    }
  }

  if (nsFrameList* pushedFloats = GetPushedFloats()) {
    bool found;
    if (didStartRemovingFloat) {
      found = pushedFloats->ContinueRemoveFrame(aFloat);
    } else {
      didStartRemovingFloat = true;
      found = pushedFloats->StartRemoveFrame(aFloat);
    }
    if (found) {
      if (pushedFloats->IsEmpty()) {
        StealPushedFloats()->Delete(PresShell());
      }
      return;
    }
  }

  {
    nsAutoOOFFrameList oofs(this);
    if (didStartRemovingFloat ? oofs.mList.ContinueRemoveFrame(aFloat)
                              : oofs.mList.StartRemoveFrame(aFloat)) {
      return;
    }
  }
}

void nsBlockFrame::DoRemoveFloats(DestroyContext& aContext, nsIFrame* aFrame) {
  // Note: nsFloatingFirstLetterFrame might lose its floating style after
  // nsBlockFrame::UpdateFirstLetterStyle().
  MOZ_ASSERT(aFrame->IsFloating() || static_cast<nsFloatingFirstLetterFrame*>(
                                         do_QueryFrame(aFrame)),
             "DoRemoveFloats() can only remove float elements!");

  // The containing block is always the parent of aFrame.
  auto* block = static_cast<nsBlockFrame*>(aFrame->GetParent());

  // First remove aFrame's next-in-flows.
  if (nsIFrame* nif = aFrame->GetNextInFlow()) {
    nif->GetParent()->DeleteNextInFlowChild(aContext, nif, false);
  }
  // Now remove aFrame from its child list and Destroy it.
  block->RemoveFloatFromFloatCache(aFrame);
  block->RemoveFloat(aFrame);
  aFrame->Destroy(aContext);
}

/**
 * This helps us iterate over the list of all normal + overflow lines
 */
void nsBlockFrame::TryAllLines(nsLineList::iterator* aIterator,
                               nsLineList::iterator* aStartIterator,
                               nsLineList::iterator* aEndIterator,
                               bool* aInOverflowLines,
                               FrameLines** aOverflowLines) {
  if (*aIterator == *aEndIterator) {
    if (!*aInOverflowLines) {
      // Try the overflow lines
      *aInOverflowLines = true;
      FrameLines* lines = GetOverflowLines();
      if (lines) {
        *aStartIterator = lines->mLines.begin();
        *aIterator = *aStartIterator;
        *aEndIterator = lines->mLines.end();
        *aOverflowLines = lines;
      }
    }
  }
}

nsBlockInFlowLineIterator::nsBlockInFlowLineIterator(nsBlockFrame* aFrame,
                                                     LineIterator aLine)
    : mFrame(aFrame), mLine(aLine), mLineList(&aFrame->mLines) {
  // This will assert if aLine isn't in mLines of aFrame:
  DebugOnly<bool> check = aLine == mFrame->LinesBegin();
}

nsBlockInFlowLineIterator::nsBlockInFlowLineIterator(nsBlockFrame* aFrame,
                                                     LineIterator aLine,
                                                     bool aInOverflow)
    : mFrame(aFrame),
      mLine(aLine),
      mLineList(aInOverflow ? &aFrame->GetOverflowLines()->mLines
                            : &aFrame->mLines) {}

nsBlockInFlowLineIterator::nsBlockInFlowLineIterator(nsBlockFrame* aFrame,
                                                     bool* aFoundValidLine)
    : mFrame(aFrame), mLineList(&aFrame->mLines) {
  mLine = aFrame->LinesBegin();
  *aFoundValidLine = FindValidLine();
}

static bool AnonymousBoxIsBFC(const ComputedStyle* aStyle) {
  switch (aStyle->GetPseudoType()) {
    case PseudoStyleType::MozFieldsetContent:
    case PseudoStyleType::MozColumnContent:
    case PseudoStyleType::MozCellContent:
    case PseudoStyleType::MozScrolledContent:
    case PseudoStyleType::MozAnonymousItem:
      return true;
    default:
      return false;
  }
}

static bool StyleEstablishesBFC(const ComputedStyle* aStyle) {
  // paint/layout containment boxes and multi-column containers establish an
  // independent formatting context.
  // https://drafts.csswg.org/css-contain/#containment-paint
  // https://drafts.csswg.org/css-contain/#containment-layout
  // https://github.com/w3c/csswg-drafts/issues/10544
  // https://drafts.csswg.org/css-align/#distribution-block
  // https://drafts.csswg.org/css-multicol/#columns
  const auto* disp = aStyle->StyleDisplay();
  return disp->IsContainPaint() || disp->IsContainLayout() ||
         disp->mContainerType &
             (StyleContainerType::SIZE | StyleContainerType::INLINE_SIZE) ||
         disp->DisplayInside() == StyleDisplayInside::FlowRoot ||
         disp->IsAbsolutelyPositionedStyle() || disp->IsFloatingStyle() ||
         aStyle->IsRootElementStyle() || AnonymousBoxIsBFC(aStyle);
}

static bool EstablishesBFC(const nsBlockFrame* aFrame) {
  if (aFrame->HasAnyClassFlag(LayoutFrameClassFlags::BlockFormattingContext)) {
    return true;
  }

  if (nsIFrame* parent = aFrame->GetParent()) {
    if (parent->IsFieldSetFrame()) {
      // A rendered legend always establishes a new formatting context, and so
      // does the fieldset content frame, so we can just return true here.
      // https://html.spec.whatwg.org/#rendered-legend
      return true;
    }

    const auto wm = aFrame->GetWritingMode();
    const auto parentWM = parent->GetWritingMode();
    if (wm.GetBlockDir() != parentWM.GetBlockDir() ||
        wm.IsVerticalSideways() != parentWM.IsVerticalSideways()) {
      // If a box has a different writing-mode value than its containing block
      // [...] if the box is a block container, then it establishes a new block
      // formatting context.
      // https://drafts.csswg.org/css-writing-modes/#block-flow
      return true;
    }
  }

  if (aFrame->IsColumnSpan()) {
    return true;
  }

  if (aFrame->IsContentAligned()) {
    return true;
  }

  if (aFrame->IsSuppressedScrollableBlockForPrint()) {
    return true;
  }

  const auto* style = aFrame->Style();
  if (style->GetPseudoType() == PseudoStyleType::Marker) {
    if (aFrame->GetParent() &&
        aFrame->GetParent()->StyleList()->mListStylePosition ==
            StyleListStylePosition::Outside) {
      // An outside ::marker needs to be an independent formatting context
      // to avoid being influenced by the float manager etc.
      return true;
    }
  }
  return StyleEstablishesBFC(style);
}

void nsBlockFrame::DidSetComputedStyle(ComputedStyle* aOldStyle) {
  nsContainerFrame::DidSetComputedStyle(aOldStyle);
  if (IsInSVGTextSubtree() &&
      (StyleSVGReset()->HasNonScalingStroke() &&
       (!aOldStyle || !aOldStyle->StyleSVGReset()->HasNonScalingStroke()))) {
    nsIFrame* textFrame =
        nsLayoutUtils::GetClosestFrameOfType(this, LayoutFrameType::SVGText);
    MOZ_ASSERT(textFrame, "Expecting to find an SVG text frame");
    SVGUtils::UpdateNonScalingStrokeStateBit(textFrame);
  }
  if (!aOldStyle) {
    return;
  }

  const bool isBFC = EstablishesBFC(this);
  if (HasAnyStateBits(NS_BLOCK_BFC) != isBFC) {
    if (MaybeHasFloats()) {
      // If the frame contains floats, this update may change their float
      // manager. Be safe by dirtying all descendant lines of the nearest
      // ancestor's float manager.
      RemoveStateBits(NS_BLOCK_BFC);
      MarkSameFloatManagerLinesDirty(this);
    }
    AddOrRemoveStateBits(NS_BLOCK_BFC, isBFC);
  }
}

void nsBlockFrame::UpdateFirstLetterStyle(ServoRestyleState& aRestyleState) {
  nsIFrame* letterFrame = GetFirstLetter();
  if (!letterFrame) {
    return;
  }

  // Figure out what the right style parent is.  This needs to match
  // nsCSSFrameConstructor::CreateLetterFrame.
  nsIFrame* inFlowFrame = letterFrame;
  if (inFlowFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
    inFlowFrame = inFlowFrame->GetPlaceholderFrame();
  }
  nsIFrame* styleParent = CorrectStyleParentFrame(inFlowFrame->GetParent(),
                                                  PseudoStyleType::FirstLetter);
  ComputedStyle* parentStyle = styleParent->Style();
  RefPtr<ComputedStyle> firstLetterStyle =
      aRestyleState.StyleSet().ResolvePseudoElementStyle(
          *mContent->AsElement(), PseudoStyleType::FirstLetter, nullptr,
          parentStyle);
  // Note that we don't need to worry about changehints for the continuation
  // styles: those will be handled by the styleParent already.
  RefPtr<ComputedStyle> continuationStyle =
      aRestyleState.StyleSet().ResolveStyleForFirstLetterContinuation(
          parentStyle);
  UpdateStyleOfOwnedChildFrame(letterFrame, firstLetterStyle, aRestyleState,
                               Some(continuationStyle.get()));

  // We also want to update the style on the textframe inside the first-letter.
  // We don't need to compute a changehint for this, though, since any changes
  // to it are handled by the first-letter anyway.
  nsIFrame* textFrame = letterFrame->PrincipalChildList().FirstChild();
  RefPtr<ComputedStyle> firstTextStyle =
      aRestyleState.StyleSet().ResolveStyleForText(textFrame->GetContent(),
                                                   firstLetterStyle);
  textFrame->SetComputedStyle(firstTextStyle);

  // We don't need to update style for textFrame's continuations: it's already
  // set up to inherit from parentStyle, which is what we want.
}

static nsIFrame* FindChildContaining(nsBlockFrame* aFrame,
                                     nsIFrame* aFindFrame) {
  NS_ASSERTION(aFrame, "must have frame");
  nsIFrame* child;
  while (true) {
    nsIFrame* block = aFrame;
    do {
      child = nsLayoutUtils::FindChildContainingDescendant(block, aFindFrame);
      if (child) {
        break;
      }
      block = block->GetNextContinuation();
    } while (block);
    if (!child) {
      return nullptr;
    }
    if (!child->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
      break;
    }
    aFindFrame = child->GetPlaceholderFrame();
  }

  return child;
}

nsBlockInFlowLineIterator::nsBlockInFlowLineIterator(nsBlockFrame* aFrame,
                                                     nsIFrame* aFindFrame,
                                                     bool* aFoundValidLine)
    : mFrame(aFrame), mLineList(&aFrame->mLines) {
  *aFoundValidLine = false;

  nsIFrame* child = FindChildContaining(aFrame, aFindFrame);
  if (!child) {
    return;
  }

  LineIterator line_end = aFrame->LinesEnd();
  mLine = aFrame->LinesBegin();
  if (mLine != line_end && mLine.next() == line_end &&
      !aFrame->HasOverflowLines()) {
    // The block has a single line - that must be it!
    *aFoundValidLine = true;
    return;
  }

  // Try to use the cursor if it exists, otherwise fall back to the first line
  if (nsLineBox* const cursor = aFrame->GetLineCursorForQuery()) {
    mLine = line_end;
    // Perform a simultaneous forward and reverse search starting from the
    // line cursor.
    nsBlockFrame::LineIterator line = aFrame->LinesBeginFrom(cursor);
    nsBlockFrame::ReverseLineIterator rline = aFrame->LinesRBeginFrom(cursor);
    nsBlockFrame::ReverseLineIterator rline_end = aFrame->LinesREnd();
    // rline is positioned on the line containing 'cursor', so it's not
    // rline_end. So we can safely increment it (i.e. move it to one line
    // earlier) to start searching there.
    ++rline;
    while (line != line_end || rline != rline_end) {
      if (line != line_end) {
        if (line->Contains(child)) {
          mLine = line;
          break;
        }
        ++line;
      }
      if (rline != rline_end) {
        if (rline->Contains(child)) {
          mLine = rline;
          break;
        }
        ++rline;
      }
    }
    if (mLine != line_end) {
      *aFoundValidLine = true;
      if (mLine != cursor) {
        aFrame->SetProperty(nsBlockFrame::LineCursorPropertyQuery(), mLine);
      }
      return;
    }
  } else {
    for (mLine = aFrame->LinesBegin(); mLine != line_end; ++mLine) {
      if (mLine->Contains(child)) {
        *aFoundValidLine = true;
        return;
      }
    }
  }
  // Didn't find the line
  MOZ_ASSERT(mLine == line_end, "mLine should be line_end at this point");

  // If we reach here, it means that we have not been able to find the
  // desired frame in our in-flow lines.  So we should start looking at
  // our overflow lines. In order to do that, we set mLine to the end
  // iterator so that FindValidLine starts to look at overflow lines,
  // if any.

  if (!FindValidLine()) {
    return;
  }

  do {
    if (mLine->Contains(child)) {
      *aFoundValidLine = true;
      return;
    }
  } while (Next());
}

nsBlockFrame::LineIterator nsBlockInFlowLineIterator::End() {
  return mLineList->end();
}

bool nsBlockInFlowLineIterator::IsLastLineInList() {
  LineIterator end = End();
  return mLine != end && mLine.next() == end;
}

bool nsBlockInFlowLineIterator::Next() {
  ++mLine;
  return FindValidLine();
}

bool nsBlockInFlowLineIterator::Prev() {
  LineIterator begin = mLineList->begin();
  if (mLine != begin) {
    --mLine;
    return true;
  }
  bool currentlyInOverflowLines = GetInOverflow();
  while (true) {
    if (currentlyInOverflowLines) {
      mLineList = &mFrame->mLines;
      mLine = mLineList->end();
      if (mLine != mLineList->begin()) {
        --mLine;
        return true;
      }
    } else {
      mFrame = static_cast<nsBlockFrame*>(mFrame->GetPrevInFlow());
      if (!mFrame) {
        return false;
      }
      nsBlockFrame::FrameLines* overflowLines = mFrame->GetOverflowLines();
      if (overflowLines) {
        mLineList = &overflowLines->mLines;
        mLine = mLineList->end();
        NS_ASSERTION(mLine != mLineList->begin(), "empty overflow line list?");
        --mLine;
        return true;
      }
    }
    currentlyInOverflowLines = !currentlyInOverflowLines;
  }
}

bool nsBlockInFlowLineIterator::FindValidLine() {
  LineIterator end = mLineList->end();
  if (mLine != end) {
    return true;
  }
  bool currentlyInOverflowLines = GetInOverflow();
  while (true) {
    if (currentlyInOverflowLines) {
      mFrame = static_cast<nsBlockFrame*>(mFrame->GetNextInFlow());
      if (!mFrame) {
        return false;
      }
      mLineList = &mFrame->mLines;
      mLine = mLineList->begin();
      if (mLine != mLineList->end()) {
        return true;
      }
    } else {
      nsBlockFrame::FrameLines* overflowLines = mFrame->GetOverflowLines();
      if (overflowLines) {
        mLineList = &overflowLines->mLines;
        mLine = mLineList->begin();
        NS_ASSERTION(mLine != mLineList->end(), "empty overflow line list?");
        return true;
      }
    }
    currentlyInOverflowLines = !currentlyInOverflowLines;
  }
}

// This function removes aDeletedFrame and all its continuations.  It
// is optimized for deleting a whole series of frames. The easy
// implementation would invoke itself recursively on
// aDeletedFrame->GetNextContinuation, then locate the line containing
// aDeletedFrame and remove aDeletedFrame from that line. But here we
// start by locating aDeletedFrame and then scanning from that point
// on looking for continuations.
void nsBlockFrame::DoRemoveFrame(DestroyContext& aContext,
                                 nsIFrame* aDeletedFrame, uint32_t aFlags) {
  MOZ_ASSERT(!aDeletedFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW |
                                             NS_FRAME_IS_OVERFLOW_CONTAINER),
             "DoRemoveFrame() does not support removing out-of-flow frames or "
             "overflow containers!");

  // Find the line that contains deletedFrame. Start from the line cursor
  // (if available) and search to the end of the normal line list, then
  // from the start to the line cursor, and last the overflow lines.
  nsLineList::iterator line_start = mLines.begin(), line_end = mLines.end();
  nsLineList::iterator line = line_start;

  // We use the line cursor to attempt to optimize removal, but must ensure
  // it is cleared if lines change such that it may become invalid.
  bool found = false;
  if (nsLineBox* cursor = GetLineCursorForDisplay()) {
    for (line.SetPosition(cursor); line != line_end; ++line) {
      if (line->Contains(aDeletedFrame)) {
        found = true;
        break;
      }
    }
    if (!found) {
      // Setup for a shorter TryAllLines normal line search to avoid searching
      // the [cursor .. line_end] range again.
      line = line_start;
      line_end.SetPosition(cursor);
    }
  }

  FrameLines* overflowLines = nullptr;
  bool searchingOverflowList = false;
  if (!found) {
    // Make sure we look in the overflow lines even if the normal line
    // list is empty.
    TryAllLines(&line, &line_start, &line_end, &searchingOverflowList,
                &overflowLines);
    while (line != line_end) {
      if (line->Contains(aDeletedFrame)) {
        break;
      }
      ++line;
      TryAllLines(&line, &line_start, &line_end, &searchingOverflowList,
                  &overflowLines);
    }
    if (!searchingOverflowList && (GetStateBits() & NS_BLOCK_HAS_LINE_CURSOR)) {
      // Restore line_end since we shortened the search to the cursor.
      line_end = mLines.end();
      // Clear our line cursors, since our normal line list may change.
      ClearLineCursors();
    }
  }

  if (line == line_end) {
    NS_ERROR("can't find deleted frame in lines");
    return;
  }

  if (!(aFlags & FRAMES_ARE_EMPTY)) {
    if (line != line_start) {
      line.prev()->MarkDirty();
      line.prev()->SetInvalidateTextRuns(true);
    } else if (searchingOverflowList && !mLines.empty()) {
      mLines.back()->MarkDirty();
      mLines.back()->SetInvalidateTextRuns(true);
    }
  }

  while (line != line_end && aDeletedFrame) {
    MOZ_ASSERT(this == aDeletedFrame->GetParent(), "messed up delete code");
    MOZ_ASSERT(line->Contains(aDeletedFrame), "frame not in line");

    if (!(aFlags & FRAMES_ARE_EMPTY)) {
      line->MarkDirty();
      line->SetInvalidateTextRuns(true);
    }

    // If the frame being deleted is the last one on the line then
    // optimize away the line->Contains(next-in-flow) call below.
    bool isLastFrameOnLine = 1 == line->GetChildCount();
    if (!isLastFrameOnLine) {
      LineIterator next = line.next();
      nsIFrame* lastFrame =
          next != line_end
              ? next->mFirstChild->GetPrevSibling()
              : (searchingOverflowList ? overflowLines->mFrames.LastChild()
                                       : mFrames.LastChild());
      NS_ASSERTION(next == line_end || lastFrame == line->LastChild(),
                   "unexpected line frames");
      isLastFrameOnLine = lastFrame == aDeletedFrame;
    }

    // Remove aDeletedFrame from the line
    if (line->mFirstChild == aDeletedFrame) {
      // We should be setting this to null if aDeletedFrame
      // is the only frame on the line. HOWEVER in that case
      // we will be removing the line anyway, see below.
      line->mFirstChild = aDeletedFrame->GetNextSibling();
    }

    // Hmm, this won't do anything if we're removing a frame in the first
    // overflow line... Hopefully doesn't matter
    --line;
    if (line != line_end && !line->IsBlock()) {
      // Since we just removed a frame that follows some inline
      // frames, we need to reflow the previous line.
      line->MarkDirty();
    }
    ++line;

    // Take aDeletedFrame out of the sibling list. Note that
    // prevSibling will only be nullptr when we are deleting the very
    // first frame in the main or overflow list.
    if (searchingOverflowList) {
      overflowLines->mFrames.RemoveFrame(aDeletedFrame);
    } else {
      mFrames.RemoveFrame(aDeletedFrame);
    }

    // Update the child count of the line to be accurate
    line->NoteFrameRemoved(aDeletedFrame);

    // Destroy frame; capture its next continuation first in case we need
    // to destroy that too.
    nsIFrame* deletedNextContinuation =
        (aFlags & REMOVE_FIXED_CONTINUATIONS)
            ? aDeletedFrame->GetNextContinuation()
            : aDeletedFrame->GetNextInFlow();
#ifdef NOISY_REMOVE_FRAME
    printf("DoRemoveFrame: %s line=%p frame=",
           searchingOverflowList ? "overflow" : "normal", line.get());
    aDeletedFrame->ListTag(stdout);
    printf(" prevSibling=%p deletedNextContinuation=%p\n",
           aDeletedFrame->GetPrevSibling(), deletedNextContinuation);
#endif

    // If next-in-flow is an overflow container, must remove it first.
    // FIXME: Can we do this unconditionally?
    if (deletedNextContinuation && deletedNextContinuation->HasAnyStateBits(
                                       NS_FRAME_IS_OVERFLOW_CONTAINER)) {
      deletedNextContinuation->GetParent()->DeleteNextInFlowChild(
          aContext, deletedNextContinuation, false);
      deletedNextContinuation = nullptr;
    }

    aDeletedFrame->Destroy(aContext);
    aDeletedFrame = deletedNextContinuation;

    bool haveAdvancedToNextLine = false;
    // If line is empty, remove it now.
    if (0 == line->GetChildCount()) {
#ifdef NOISY_REMOVE_FRAME
      printf("DoRemoveFrame: %s line=%p became empty so it will be removed\n",
             searchingOverflowList ? "overflow" : "normal", line.get());
#endif
      nsLineBox* cur = line;
      if (!searchingOverflowList) {
        line = mLines.erase(line);
        ClearLineCursors();
        // Invalidate the space taken up by the line.
        // XXX We need to do this if we're removing a frame as a result of
        // a call to RemoveFrame(), but we may not need to do this in all
        // cases...
#ifdef NOISY_BLOCK_INVALIDATE
        nsRect inkOverflow(cur->InkOverflowRect());
        printf("%p invalidate 10 (%d, %d, %d, %d)\n", this, inkOverflow.x,
               inkOverflow.y, inkOverflow.width, inkOverflow.height);
#endif
      } else {
        line = overflowLines->mLines.erase(line);
        if (overflowLines->mLines.empty()) {
          DestroyOverflowLines();
          overflowLines = nullptr;
          // We just invalidated our iterators.  Since we were in
          // the overflow lines list, which is now empty, set them
          // so we're at the end of the regular line list.
          line_start = mLines.begin();
          line_end = mLines.end();
          line = line_end;
        }
      }
      FreeLineBox(cur);

      // If we're removing a line, ReflowDirtyLines isn't going to
      // know that it needs to slide lines unless something is marked
      // dirty.  So mark the previous margin of the next line dirty if
      // there is one.
      if (line != line_end) {
        line->MarkPreviousMarginDirty();
      }
      haveAdvancedToNextLine = true;
    } else {
      // Make the line that just lost a frame dirty, and advance to
      // the next line.
      if (!deletedNextContinuation || isLastFrameOnLine ||
          !line->Contains(deletedNextContinuation)) {
        line->MarkDirty();
        ++line;
        haveAdvancedToNextLine = true;
      }
    }

    if (deletedNextContinuation) {
      // See if we should keep looking in the current flow's line list.
      if (deletedNextContinuation->GetParent() != this) {
        // The deceased frames continuation is not a child of the
        // current block. So break out of the loop so that we advance
        // to the next parent.
        //
        // If we have a continuation in a different block then all bets are
        // off regarding whether we are deleting frames without actual content,
        // so don't propagate FRAMES_ARE_EMPTY any further.
        aFlags &= ~FRAMES_ARE_EMPTY;
        break;
      }

      // If we advanced to the next line then check if we should switch to the
      // overflow line list.
      if (haveAdvancedToNextLine) {
        if (line != line_end && !searchingOverflowList &&
            !line->Contains(deletedNextContinuation)) {
          // We have advanced to the next *normal* line but the next-in-flow
          // is not there - force a switch to the overflow line list.
          line = line_end;
        }

        TryAllLines(&line, &line_start, &line_end, &searchingOverflowList,
                    &overflowLines);
#ifdef NOISY_REMOVE_FRAME
        printf("DoRemoveFrame: now on %s line=%p\n",
               searchingOverflowList ? "overflow" : "normal", line.get());
#endif
      }
    }
  }

  if (!(aFlags & FRAMES_ARE_EMPTY) && line.next() != line_end) {
    line.next()->MarkDirty();
    line.next()->SetInvalidateTextRuns(true);
  }

#ifdef DEBUG
  VerifyLines(true);
  VerifyOverflowSituation();
#endif

  // Advance to next flow block if the frame has more continuations.
  if (!aDeletedFrame) {
    return;
  }
  nsBlockFrame* nextBlock = do_QueryFrame(aDeletedFrame->GetParent());
  NS_ASSERTION(nextBlock, "Our child's continuation's parent is not a block?");
  uint32_t flags = (aFlags & REMOVE_FIXED_CONTINUATIONS);
  nextBlock->DoRemoveFrame(aContext, aDeletedFrame, flags);
}

static bool FindBlockLineFor(nsIFrame* aChild, nsLineList::iterator aBegin,
                             nsLineList::iterator aEnd,
                             nsLineList::iterator* aResult) {
  MOZ_ASSERT(aChild->IsBlockOutside());
  for (nsLineList::iterator line = aBegin; line != aEnd; ++line) {
    MOZ_ASSERT(line->GetChildCount() > 0);
    if (line->IsBlock() && line->mFirstChild == aChild) {
      MOZ_ASSERT(line->GetChildCount() == 1);
      *aResult = line;
      return true;
    }
  }
  return false;
}

static bool FindInlineLineFor(nsIFrame* aChild, const nsFrameList& aFrameList,
                              nsLineList::iterator aBegin,
                              nsLineList::iterator aEnd,
                              nsLineList::iterator* aResult) {
  MOZ_ASSERT(!aChild->IsBlockOutside());
  for (nsLineList::iterator line = aBegin; line != aEnd; ++line) {
    MOZ_ASSERT(line->GetChildCount() > 0);
    if (!line->IsBlock()) {
      // Optimize by comparing the line's last child first.
      nsLineList::iterator next = line.next();
      if (aChild == (next == aEnd ? aFrameList.LastChild()
                                  : next->mFirstChild->GetPrevSibling()) ||
          line->Contains(aChild)) {
        *aResult = line;
        return true;
      }
    }
  }
  return false;
}

static bool FindLineFor(nsIFrame* aChild, const nsFrameList& aFrameList,
                        nsLineList::iterator aBegin, nsLineList::iterator aEnd,
                        nsLineList::iterator* aResult) {
  return aChild->IsBlockOutside()
             ? FindBlockLineFor(aChild, aBegin, aEnd, aResult)
             : FindInlineLineFor(aChild, aFrameList, aBegin, aEnd, aResult);
}

void nsBlockFrame::StealFrame(nsIFrame* aChild) {
  MOZ_ASSERT(aChild->GetParent() == this);

  if (aChild->IsFloating()) {
    RemoveFloat(aChild);
    return;
  }

  if (MaybeStealOverflowContainerFrame(aChild)) {
    return;
  }

  MOZ_ASSERT(!aChild->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW));

  nsLineList::iterator line;
  if (FindLineFor(aChild, mFrames, mLines.begin(), mLines.end(), &line)) {
    RemoveFrameFromLine(aChild, line, mFrames, mLines);
  } else {
    FrameLines* overflowLines = GetOverflowLines();
    DebugOnly<bool> found;
    found = FindLineFor(aChild, overflowLines->mFrames,
                        overflowLines->mLines.begin(),
                        overflowLines->mLines.end(), &line);
    MOZ_ASSERT(found, "Why can't we find aChild in our overflow lines?");
    RemoveFrameFromLine(aChild, line, overflowLines->mFrames,
                        overflowLines->mLines);
    if (overflowLines->mLines.empty()) {
      DestroyOverflowLines();
    }
  }
}

void nsBlockFrame::RemoveFrameFromLine(nsIFrame* aChild,
                                       nsLineList::iterator aLine,
                                       nsFrameList& aFrameList,
                                       nsLineList& aLineList) {
  aFrameList.RemoveFrame(aChild);
  if (aChild == aLine->mFirstChild) {
    aLine->mFirstChild = aChild->GetNextSibling();
  }
  aLine->NoteFrameRemoved(aChild);
  if (aLine->GetChildCount() > 0) {
    aLine->MarkDirty();
  } else {
    // The line became empty - destroy it.
    nsLineBox* lineBox = aLine;
    aLine = aLineList.erase(aLine);
    if (aLine != aLineList.end()) {
      aLine->MarkPreviousMarginDirty();
    }
    FreeLineBox(lineBox);
    ClearLineCursors();
  }
}

void nsBlockFrame::DeleteNextInFlowChild(DestroyContext& aContext,
                                         nsIFrame* aNextInFlow,
                                         bool aDeletingEmptyFrames) {
  MOZ_ASSERT(aNextInFlow->GetPrevInFlow(), "bad next-in-flow");

  if (aNextInFlow->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW |
                                   NS_FRAME_IS_OVERFLOW_CONTAINER)) {
    nsContainerFrame::DeleteNextInFlowChild(aContext, aNextInFlow,
                                            aDeletingEmptyFrames);
  } else {
#ifdef DEBUG
    if (aDeletingEmptyFrames) {
      nsLayoutUtils::AssertTreeOnlyEmptyNextInFlows(aNextInFlow);
    }
#endif
    DoRemoveFrame(aContext, aNextInFlow,
                  aDeletingEmptyFrames ? FRAMES_ARE_EMPTY : 0);
  }
}

const nsStyleText* nsBlockFrame::StyleTextForLineLayout() {
  // Return the pointer to an unmodified style text
  return StyleText();
}

void nsBlockFrame::ReflowFloat(BlockReflowState& aState, ReflowInput& aFloatRI,
                               nsIFrame* aFloat,
                               nsReflowStatus& aReflowStatus) {
  MOZ_ASSERT(aReflowStatus.IsEmpty(),
             "Caller should pass a fresh reflow status!");
  MOZ_ASSERT(aFloat->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW),
             "aFloat must be an out-of-flow frame");

  WritingMode wm = aState.mReflowInput.GetWritingMode();

  // Setup a block reflow context to reflow the float.
  nsBlockReflowContext brc(aState.mPresContext, aState.mReflowInput);

  nsIFrame* clearanceFrame = nullptr;
  do {
    CollapsingMargin margin;
    bool mayNeedRetry = false;
    aFloatRI.mDiscoveredClearance = nullptr;
    // Only first in flow gets a block-start margin.
    if (!aFloat->GetPrevInFlow()) {
      brc.ComputeCollapsedBStartMargin(aFloatRI, &margin, clearanceFrame,
                                       &mayNeedRetry);

      if (mayNeedRetry && !clearanceFrame) {
        aFloatRI.mDiscoveredClearance = &clearanceFrame;
        // We don't need to push the float manager state because the the block
        // has its own float manager that will be destroyed and recreated
      }
    }

    // When reflowing a float, aSpace argument doesn't matter because we pass
    // nullptr to aLine and we don't call nsBlockReflowContext::PlaceBlock()
    // later.
    brc.ReflowBlock(LogicalRect(wm), true, margin, 0, nullptr, aFloatRI,
                    aReflowStatus, aState);
  } while (clearanceFrame);

  if (aFloat->IsLetterFrame()) {
    // We never split floating first letters; an incomplete status for such
    // frames simply means that there is more content to be reflowed on the
    // line.
    if (aReflowStatus.IsIncomplete()) {
      aReflowStatus.Reset();
    }
  }

  NS_ASSERTION(aReflowStatus.IsFullyComplete() ||
                   aFloatRI.AvailableBSize() != NS_UNCONSTRAINEDSIZE,
               "The status can only be incomplete or overflow-incomplete if "
               "the available block-size is constrained!");

  if (aReflowStatus.NextInFlowNeedsReflow()) {
    aState.mReflowStatus.SetNextInFlowNeedsReflow();
  }

  const ReflowOutput& metrics = brc.GetMetrics();

  // Set the rect, make sure the view is properly sized and positioned,
  // and tell the frame we're done reflowing it
  // XXXldb This seems like the wrong place to be doing this -- shouldn't
  // we be doing this in BlockReflowState::FlowAndPlaceFloat after
  // we've positioned the float, and shouldn't we be doing the equivalent
  // of |PlaceFrameView| here?
  WritingMode metricsWM = metrics.GetWritingMode();
  aFloat->SetSize(metricsWM, metrics.Size(metricsWM));
  aFloat->DidReflow(aState.mPresContext, &aFloatRI);
}

UsedClear nsBlockFrame::FindTrailingClear() {
  for (nsBlockFrame* b = this; b;
       b = static_cast<nsBlockFrame*>(b->GetPrevInFlow())) {
    auto endLine = b->LinesRBegin();
    if (endLine != b->LinesREnd()) {
      return endLine->FloatClearTypeAfter();
    }
  }
  return UsedClear::None;
}

void nsBlockFrame::ReflowPushedFloats(BlockReflowState& aState,
                                      OverflowAreas& aOverflowAreas) {
  // Pushed floats live at the start of our float list; see comment
  // above nsBlockFrame::DrainPushedFloats.
  nsFrameList* floats = GetFloats();
  nsIFrame* f = floats ? floats->FirstChild() : nullptr;
  nsIFrame* prev = nullptr;
  while (f && f->HasAnyStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW)) {
    MOZ_ASSERT(prev == f->GetPrevSibling());
    // When we push a first-continuation float in a non-initial reflow,
    // it's possible that we end up with two continuations with the same
    // parent.  This happens if, on the previous reflow of the block or
    // a previous reflow of the line containing the block, the float was
    // split between continuations A and B of the parent, but on the
    // current reflow, none of the float can fit in A.
    //
    // When this happens, we might even have the two continuations
    // out-of-order due to the management of the pushed floats.  In
    // particular, if the float's placeholder was in a pushed line that
    // we reflowed before it was pushed, and we split the float during
    // that reflow, we might have the continuation of the float before
    // the float itself.  (In the general case, however, it's correct
    // for floats in the pushed floats list to come before floats
    // anchored in pushed lines; however, in this case it's wrong.  We
    // should probably find a way to fix it somehow, since it leads to
    // incorrect layout in some cases.)
    //
    // When we have these out-of-order continuations, we might hit the
    // next-continuation before the previous-continuation.  When that
    // happens, just push it.  When we reflow the next continuation,
    // we'll either pull all of its content back and destroy it (by
    // calling DeleteNextInFlowChild), or nsBlockFrame::SplitFloat will
    // pull it out of its current position and push it again (and
    // potentially repeat this cycle for the next continuation, although
    // hopefully then they'll be in the right order).
    //
    // We should also need this code for the in-order case if the first
    // continuation of a float gets moved across more than one
    // continuation of the containing block.  In this case we'd manage
    // to push the second continuation without this check, but not the
    // third and later.
    nsIFrame* prevContinuation = f->GetPrevContinuation();
    if (prevContinuation && prevContinuation->GetParent() == f->GetParent()) {
      floats->RemoveFrame(f);
      if (floats->IsEmpty()) {
        StealFloats()->Delete(PresShell());
        floats = nullptr;
      }
      aState.AppendPushedFloatChain(f);
      if (!floats) {
        // The floats list becomes empty after removing |f|. Bail out.
        f = prev = nullptr;
        break;
      }
      // Even if we think |floats| is valid, AppendPushedFloatChain() can also
      // push |f|'s next-in-flows in our floats list to our pushed floats list.
      // If all the floats in the floats list are pushed, the floats list will
      // be deleted, and |floats| will be stale and poisoned. Therefore, we need
      // to get the floats list again to check its validity.
      floats = GetFloats();
      if (!floats) {
        f = prev = nullptr;
        break;
      }
      f = !prev ? floats->FirstChild() : prev->GetNextSibling();
      continue;
    }

    // Always call FlowAndPlaceFloat; we might need to place this float if it
    // didn't belong to this block the last time it was reflowed.  Note that if
    // the float doesn't get placed, we don't consider its overflow areas.
    // (Not-getting-placed means it didn't fit and we pushed it instead of
    // placing it, and its position could be stale.)
    if (aState.FlowAndPlaceFloat(f) ==
        BlockReflowState::PlaceFloatResult::Placed) {
      ConsiderChildOverflow(aOverflowAreas, f);
    }

    // If f is the only child in the floats list, pushing it to the pushed
    // floats list in FlowAndPlaceFloat() can result in the floats list being
    // deleted. Get the floats list again.
    floats = GetFloats();
    if (!floats) {
      f = prev = nullptr;
      break;
    }

    nsIFrame* next = !prev ? floats->FirstChild() : prev->GetNextSibling();
    if (next == f) {
      // We didn't push |f| so its next-sibling is next.
      next = f->GetNextSibling();
      prev = f;
    }  // else: we did push |f| so |prev|'s new next-sibling is next.
    f = next;
  }

  // If there are pushed or split floats, then we may need to continue BR
  // clearance
  if (auto [bCoord, result] = aState.ClearFloats(0, UsedClear::Both);
      result != ClearFloatsResult::BCoordNoChange) {
    (void)bCoord;
    if (auto* prevBlock = static_cast<nsBlockFrame*>(GetPrevInFlow())) {
      aState.mTrailingClearFromPIF = prevBlock->FindTrailingClear();
    }
  }
}

void nsBlockFrame::RecoverFloats(nsFloatManager& aFloatManager, WritingMode aWM,
                                 const nsSize& aContainerSize) {
  // Recover our own floats
  nsIFrame* stop = nullptr;  // Stop before we reach pushed floats that
                             // belong to our next-in-flow
  const nsFrameList* floats = GetFloats();
  for (nsIFrame* f = floats ? floats->FirstChild() : nullptr; f && f != stop;
       f = f->GetNextSibling()) {
    LogicalRect region = nsFloatManager::GetRegionFor(aWM, f, aContainerSize);
    aFloatManager.AddFloat(f, region, aWM, aContainerSize);
    if (!stop && f->GetNextInFlow()) {
      stop = f->GetNextInFlow();
    }
  }

  // Recurse into our overflow container children
  for (nsIFrame* oc =
           GetChildList(FrameChildListID::OverflowContainers).FirstChild();
       oc; oc = oc->GetNextSibling()) {
    RecoverFloatsFor(oc, aFloatManager, aWM, aContainerSize);
  }

  // Recurse into our normal children
  for (const auto& line : Lines()) {
    if (line.IsBlock()) {
      RecoverFloatsFor(line.mFirstChild, aFloatManager, aWM, aContainerSize);
    }
  }
}

void nsBlockFrame::RecoverFloatsFor(nsIFrame* aFrame,
                                    nsFloatManager& aFloatManager,
                                    WritingMode aWM,
                                    const nsSize& aContainerSize) {
  MOZ_ASSERT(aFrame, "null frame");

  // Only blocks have floats
  nsBlockFrame* block = do_QueryFrame(aFrame);
  // Don't recover any state inside a block that has its own float manager
  // (we don't currently have any blocks like this, though, thanks to our
  // use of extra frames for 'overflow')
  if (block && !nsBlockFrame::BlockNeedsFloatManager(block)) {
    // If the element is relatively positioned, then adjust x and y
    // accordingly so that we consider relatively positioned frames
    // at their original position.

    const LogicalRect rect = block->GetLogicalNormalRect(aWM, aContainerSize);
    nscoord lineLeft = rect.LineLeft(aWM, aContainerSize);
    nscoord blockStart = rect.BStart(aWM);
    aFloatManager.Translate(lineLeft, blockStart);
    block->RecoverFloats(aFloatManager, aWM, aContainerSize);
    aFloatManager.Translate(-lineLeft, -blockStart);
  }
}

bool nsBlockFrame::HasPushedFloatsFromPrevContinuation() const {
  if (const nsFrameList* floats = GetFloats()) {
    // If we have pushed floats, then they should be at the beginning of our
    // float list.
    if (floats->FirstChild()->HasAnyStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW)) {
      return true;
    }
#ifdef DEBUG
    // Double-check the above assertion that pushed floats should be at the
    // beginning of our floats list.
    for (nsIFrame* f : *floats) {
      NS_ASSERTION(!f->HasAnyStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW),
                   "pushed floats must be at the beginning of the float list");
    }
#endif
  }

  // We may have a pending push of pushed floats, too.
  return HasPushedFloats();
}

//////////////////////////////////////////////////////////////////////
// Painting, event handling

#ifdef DEBUG
static void ComputeInkOverflowArea(nsLineList& aLines, nscoord aWidth,
                                   nscoord aHeight, nsRect& aResult) {
  nscoord xa = 0, ya = 0, xb = aWidth, yb = aHeight;
  for (const auto& line : aLines) {
    // Compute min and max x/y values for the reflowed frame's
    // combined areas
    nsRect inkOverflow(line.InkOverflowRect());
    nscoord x = inkOverflow.x;
    nscoord y = inkOverflow.y;
    nscoord xmost = x + inkOverflow.width;
    nscoord ymost = y + inkOverflow.height;
    if (x < xa) {
      xa = x;
    }
    if (xmost > xb) {
      xb = xmost;
    }
    if (y < ya) {
      ya = y;
    }
    if (ymost > yb) {
      yb = ymost;
    }
  }

  aResult.x = xa;
  aResult.y = ya;
  aResult.width = xb - xa;
  aResult.height = yb - ya;
}
#endif

#ifdef DEBUG
static void DebugOutputDrawLine(int32_t aDepth, nsLineBox* aLine, bool aDrawn) {
  if (nsBlockFrame::gNoisyDamageRepair) {
    nsIFrame::IndentBy(stdout, aDepth + 1);
    nsRect lineArea = aLine->InkOverflowRect();
    printf("%s line=%p bounds=%d,%d,%d,%d ca=%d,%d,%d,%d\n",
           aDrawn ? "draw" : "skip", static_cast<void*>(aLine), aLine->IStart(),
           aLine->BStart(), aLine->ISize(), aLine->BSize(), lineArea.x,
           lineArea.y, lineArea.width, lineArea.height);
  }
}
#endif

static void DisplayLine(nsDisplayListBuilder* aBuilder,
                        nsBlockFrame::LineIterator& aLine,
                        const bool aLineInLine, const nsDisplayListSet& aLists,
                        nsBlockFrame* aFrame, TextOverflow* aTextOverflow,
                        uint32_t aLineNumberForTextOverflow, int32_t aDepth,
                        int32_t& aDrawnLines, bool& aFoundLineClamp) {
#ifdef DEBUG
  if (nsBlockFrame::gLamePaintMetrics) {
    aDrawnLines++;
  }
  const bool intersect =
      aLine->InkOverflowRect().Intersects(aBuilder->GetDirtyRect());
  DebugOutputDrawLine(aDepth, aLine.get(), intersect);
#endif

  // Collect our line's display items in a temporary nsDisplayListCollection,
  // so that we can apply any "text-overflow" clipping to the entire collection
  // without affecting previous lines.
  nsDisplayListCollection collection(aBuilder);

  // Block-level child backgrounds go on the blockBorderBackgrounds list ...
  // Inline-level child backgrounds go on the regular child content list.
  nsDisplayListSet childLists(
      collection,
      aLineInLine ? collection.Content() : collection.BlockBorderBackgrounds());

  auto flags =
      aLineInLine
          ? nsIFrame::DisplayChildFlags(nsIFrame::DisplayChildFlag::Inline)
          : nsIFrame::DisplayChildFlags();

  for (nsIFrame* kid : aLine->ChildFrames()) {
    aFrame->BuildDisplayListForChild(aBuilder, kid, childLists, flags);
  }

  if (aFrame->HasLineClampEllipsisDescendant() && !aLineInLine) {
    if (nsBlockFrame* f = GetAsLineClampDescendant(aLine->mFirstChild)) {
      if (f->HasLineClampEllipsis() || f->HasLineClampEllipsisDescendant()) {
        aFoundLineClamp = true;
      }
    }
  }

  if (aTextOverflow && aLineInLine) {
    aTextOverflow->ProcessLine(collection, aLine.get(),
                               aLineNumberForTextOverflow);
  }

  collection.MoveTo(aLists);
}

void nsBlockFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                    const nsDisplayListSet& aLists) {
  int32_t drawnLines;  // Will only be used if set (gLamePaintMetrics).
  int32_t depth = 0;
#ifdef DEBUG
  if (gNoisyDamageRepair) {
    nsRect dirty = aBuilder->GetDirtyRect();
    depth = GetDepth();
    nsRect ca;
    ::ComputeInkOverflowArea(mLines, mRect.width, mRect.height, ca);
    nsIFrame::IndentBy(stdout, depth);
    ListTag(stdout);
    printf(": bounds=%d,%d,%d,%d dirty(absolute)=%d,%d,%d,%d ca=%d,%d,%d,%d\n",
           mRect.x, mRect.y, mRect.width, mRect.height, dirty.x, dirty.y,
           dirty.width, dirty.height, ca.x, ca.y, ca.width, ca.height);
  }
  PRTime start = 0;  // Initialize these variables to silence the compiler.
  if (gLamePaintMetrics) {
    start = PR_Now();
    drawnLines = 0;
  }
#endif

  // TODO(heycam): Should we boost the load priority of any shape-outside
  // images using CATEGORY_DISPLAY, now that this block is being displayed?
  // We don't have a float manager here.

  DisplayBorderBackgroundOutline(aBuilder, aLists);

  if (HidesContent()) {
    return;
  }

  if (GetPrevInFlow()) {
    DisplayOverflowContainers(aBuilder, aLists);
    // TODO(dshin, bug 2013284): Location of this doesn't align with observed
    // behaviour.
    for (nsIFrame* f : GetChildList(FrameChildListID::Float)) {
      if (f->HasAnyStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW)) {
        BuildDisplayListForChild(aBuilder, f, aLists);
      }
    }
  }

  aBuilder->MarkFramesForDisplayList(this,
                                     GetChildList(FrameChildListID::Float));

  if (nsIFrame* outsideMarker = GetOutsideMarker()) {
    // Display outside ::marker manually.
    BuildDisplayListForChild(aBuilder, outsideMarker, aLists);
  }

  // Prepare for text-overflow processing.
  UniquePtr<TextOverflow> textOverflow =
      TextOverflow::WillProcessLines(aBuilder, this);

  const bool hasDescendantPlaceHolders =
      HasAnyStateBits(NS_FRAME_FORCE_DISPLAY_LIST_DESCEND_INTO) ||
      ForceDescendIntoIfVisible() || aBuilder->GetIncludeAllOutOfFlows();

  const auto ShouldDescendIntoLine = [&](const nsRect& aLineArea) -> bool {
    // TODO(miko): Unfortunately |descendAlways| cannot be cached, because with
    // some frame trees, building display list for child lines can change it.
    // See bug 1552789.
    const bool descendAlways =
        HasAnyStateBits(NS_FRAME_FORCE_DISPLAY_LIST_DESCEND_INTO) ||
        aBuilder->GetIncludeAllOutOfFlows();

    return descendAlways || aLineArea.Intersects(aBuilder->GetDirtyRect()) ||
           (ForceDescendIntoIfVisible() &&
            aLineArea.Intersects(aBuilder->GetVisibleRect()));
  };

  Maybe<nscolor> backplateColor;

  // We'll try to draw an accessibility backplate behind text (to ensure it's
  // readable over any possible background-images), if all of the following
  // hold:
  //    (A) we are not honoring the document colors
  //    (B) the backplate feature is preffed on
  //    (C) the force color adjust property is set to auto
  if (PresContext()->ForcingColors() &&
      StaticPrefs::browser_display_permit_backplate() &&
      StyleText()->mForcedColorAdjust != StyleForcedColorAdjust::None) {
    backplateColor.emplace(GetBackplateColor(this));
  }

  const bool canUseCursor = [&] {
    if (hasDescendantPlaceHolders) {
      // Don't use the line cursor if we might have a descendant placeholder. It
      // might skip lines that contain placeholders but don't themselves
      // intersect with the dirty area.
      //
      // In particular, we really want to check ShouldDescendIntoFrame()
      // on all our child frames, but that might be expensive.  So we
      // approximate it by checking it on |this|; if it's true for any
      // frame in our child list, it's also true for |this|.
      return false;
    }
    if (textOverflow) {
      // Also skip the cursor if we're creating text overflow markers, since we
      // need to know what line number we're up to in order to generate unique
      // display item keys.
      return false;
    }
    if (backplateColor) {
      // Cursors should be skipped if we're drawing backplates behind text. When
      // backplating we consider consecutive runs of text as a whole, which
      // requires we iterate through all lines to find our backplate size.
      return false;
    }
    if ((HasLineClampEllipsis() || HasLineClampEllipsisDescendant()) &&
        StaticPrefs::layout_css_webkit_line_clamp_skip_paint()) {
      // We can't use the cursor if we're in a line-clamping situation, and
      // we're configured to not paint its clamped content, as we need to know
      // whether we've hit the clamp point which requires iterating over all
      // lines.
      return false;
    }
    return true;
  }();

  nsLineBox* cursor = canUseCursor
                          ? GetFirstLineContaining(aBuilder->GetDirtyRect().y)
                          : nullptr;
  LineIterator line_end = LinesEnd();

  TextOverflow* textOverflowPtr = textOverflow.get();
  bool foundClamp = false;

  if (cursor) {
    for (LineIterator line = mLines.begin(cursor); line != line_end; ++line) {
      const nsRect lineArea = line->InkOverflowRect();
      if (!lineArea.IsEmpty()) {
        // Because we have a cursor, the combinedArea.ys are non-decreasing.
        // Once we've passed aDirtyRect.YMost(), we can never see it again.
        if (lineArea.y >= aBuilder->GetDirtyRect().YMost()) {
          break;
        }
        MOZ_ASSERT(!textOverflow);

        if (ShouldDescendIntoLine(lineArea)) {
          DisplayLine(aBuilder, line, line->IsInline(), aLists, this, nullptr,
                      0, depth, drawnLines, foundClamp);
          MOZ_ASSERT(!foundClamp ||
                     !StaticPrefs::layout_css_webkit_line_clamp_skip_paint());
        }
      }
    }
  } else {
    bool nonDecreasingYs = true;
    uint32_t lineCount = 0;
    nscoord lastY = INT32_MIN;
    nscoord lastYMost = INT32_MIN;

    // A frame's display list cannot contain more than one copy of a
    // given display item unless the items are uniquely identifiable.
    // Because backplate occasionally requires multiple
    // SolidColor items, we use an index (backplateIndex) to maintain
    // uniqueness among them. Note this is a mapping of index to
    // item, and the mapping is stable even if the dirty rect changes.
    uint16_t backplateIndex = 0;
    nsRect curBackplateArea;

    auto AddBackplate = [&]() {
      aLists.BorderBackground()->AppendNewToTopWithIndex<nsDisplaySolidColor>(
          aBuilder, this, backplateIndex, curBackplateArea,
          backplateColor.value());
    };

    for (LineIterator line = LinesBegin(); line != line_end; ++line) {
      const nsRect lineArea = line->InkOverflowRect();
      const bool lineInLine = line->IsInline();

      if ((lineInLine && textOverflowPtr) || ShouldDescendIntoLine(lineArea)) {
        DisplayLine(aBuilder, line, lineInLine, aLists, this, textOverflowPtr,
                    lineCount, depth, drawnLines, foundClamp);
      }

      if (!lineInLine && !curBackplateArea.IsEmpty()) {
        // If we have encountered a non-inline line but were previously
        // forming a backplate, we should add the backplate to the display
        // list as-is and render future backplates disjointly.
        MOZ_ASSERT(backplateColor,
                   "if this master switch is off, curBackplateArea "
                   "must be empty and we shouldn't get here");
        AddBackplate();
        backplateIndex++;
        curBackplateArea = nsRect();
      }

      if (!lineArea.IsEmpty()) {
        if (lineArea.y < lastY || lineArea.YMost() < lastYMost) {
          nonDecreasingYs = false;
        }
        lastY = lineArea.y;
        lastYMost = lineArea.YMost();
        if (lineInLine && backplateColor && LineHasVisibleInlineText(line)) {
          nsRect lineBackplate = GetLineTextArea(line, aBuilder) +
                                 aBuilder->ToReferenceFrame(this);
          if (curBackplateArea.IsEmpty()) {
            curBackplateArea = lineBackplate;
          } else {
            curBackplateArea.OrWith(lineBackplate);
          }
        }
      }
      foundClamp = foundClamp || line->HasLineClampEllipsis();
      if (foundClamp &&
          StaticPrefs::layout_css_webkit_line_clamp_skip_paint()) {
        break;
      }
      lineCount++;
    }

    if (GetPrevInFlow() || GetNextInFlow()) {
      DisplayAbsoluteFramesNotBuiltByPlaceholder(aBuilder, aLists);
    }

    if (nonDecreasingYs && lineCount >= MIN_LINES_NEEDING_CURSOR) {
      SetupLineCursorForDisplay();
    }

    if (!curBackplateArea.IsEmpty()) {
      AddBackplate();
    }
  }

  if (textOverflow) {
    // Put any text-overflow:ellipsis markers on top of the non-positioned
    // content of the block's lines. (If we ever start sorting the Content()
    // list this will end up in the wrong place.)
    aLists.Content()->AppendToTop(&textOverflow->GetMarkers());
  }

#ifdef DEBUG
  if (gLamePaintMetrics) {
    PRTime end = PR_Now();

    int32_t numLines = mLines.size();
    if (!numLines) {
      numLines = 1;
    }
    PRTime lines, deltaPerLine, delta;
    lines = int64_t(numLines);
    delta = end - start;
    deltaPerLine = delta / lines;

    ListTag(stdout);
    char buf[400];
    SprintfLiteral(buf,
                   ": %" PRId64 " elapsed (%" PRId64
                   " per line) lines=%d drawn=%d skip=%d",
                   delta, deltaPerLine, numLines, drawnLines,
                   numLines - drawnLines);
    printf("%s\n", buf);
  }
#endif
}

#ifdef ACCESSIBILITY
a11y::AccType nsBlockFrame::AccessibleType() {
  if (IsTableCaption()) {
    return GetRect().IsEmpty() ? a11y::eNoType : a11y::eHTMLCaptionType;
  }

  // block frame may be for <hr>
  if (mContent->IsHTMLElement(nsGkAtoms::hr)) {
    return a11y::eHTMLHRType;
  }

  if (IsButtonLike()) {
    return a11y::eHTMLButtonType;
  }

  if (HasMarker()) {
    // Create special list item accessible since we have a ::marker.
    return a11y::eHTMLLiType;
  }

  // XXXsmaug What if we're in the shadow dom?
  if (!mContent->GetParent()) {
    // Don't create accessible objects for the root content node, they are
    // redundant with the nsDocAccessible object created with the document
    // node
    return a11y::eNoType;
  }

  if (mContent == mContent->OwnerDoc()->GetBody()) {
    // Don't create accessible objects for the body, they are redundant with
    // the nsDocAccessible object created with the document node
    return a11y::eNoType;
  }

  // Not a list item with a ::marker, treat as normal HTML container.
  return a11y::eHyperTextType;
}
#endif

void nsBlockFrame::SetupLineCursorForDisplay() {
  if (mLines.empty() || HasProperty(LineCursorPropertyDisplay())) {
    return;
  }

  SetProperty(LineCursorPropertyDisplay(), mLines.front());
  AddStateBits(NS_BLOCK_HAS_LINE_CURSOR);
}

void nsBlockFrame::SetupLineCursorForQuery() {
  if (mLines.empty() || HasProperty(LineCursorPropertyQuery())) {
    return;
  }

  SetProperty(LineCursorPropertyQuery(), mLines.front());
  AddStateBits(NS_BLOCK_HAS_LINE_CURSOR);
}

nsLineBox* nsBlockFrame::GetFirstLineContaining(nscoord y) {
  // Although this looks like a "querying" method, it is used by the
  // display-list building code, so uses the Display cursor.
  nsLineBox* property = GetLineCursorForDisplay();
  if (!property) {
    return nullptr;
  }
  LineIterator cursor = mLines.begin(property);
  nsRect cursorArea = cursor->InkOverflowRect();

  while ((cursorArea.IsEmpty() || cursorArea.YMost() > y) &&
         cursor != mLines.front()) {
    cursor = cursor.prev();
    cursorArea = cursor->InkOverflowRect();
  }
  while ((cursorArea.IsEmpty() || cursorArea.YMost() <= y) &&
         cursor != mLines.back()) {
    cursor = cursor.next();
    cursorArea = cursor->InkOverflowRect();
  }

  if (cursor.get() != property) {
    SetProperty(LineCursorPropertyDisplay(), cursor.get());
  }

  return cursor.get();
}

/* virtual */
void nsBlockFrame::ChildIsDirty(nsIFrame* aChild) {
  // See if the child is absolutely positioned
  if (aChild->IsAbsolutelyPositioned()) {
    // do nothing
  } else if (aChild == GetOutsideMarker()) {
    // The ::marker lives in the first line, unless the first line has
    // height 0 and there is a second line, in which case it lives
    // in the second line.
    LineIterator markerLine = LinesBegin();
    if (markerLine != LinesEnd() && markerLine->BSize() == 0 &&
        markerLine != mLines.back()) {
      markerLine = markerLine.next();
    }

    if (markerLine != LinesEnd()) {
      MarkLineDirty(markerLine, &mLines);
    }
    // otherwise we have an empty line list, and ReflowDirtyLines
    // will handle reflowing the ::marker.
  } else {
    // Note that we should go through our children to mark lines dirty
    // before the next reflow.  Doing it now could make things O(N^2)
    // since finding the right line is O(N).
    // We don't need to worry about marking lines on the overflow list
    // as dirty; we're guaranteed to reflow them if we take them off the
    // overflow list.
    // However, we might have gotten a float, in which case we need to
    // reflow the line containing its placeholder.  So find the
    // ancestor-or-self of the placeholder that's a child of the block,
    // and mark it as NS_FRAME_HAS_DIRTY_CHILDREN too, so that we mark
    // its line dirty when we handle NS_BLOCK_LOOK_FOR_DIRTY_FRAMES.
    // We need to take some care to handle the case where a float is in
    // a different continuation than its placeholder, including marking
    // an extra block with NS_BLOCK_LOOK_FOR_DIRTY_FRAMES.
    if (!aChild->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
      AddStateBits(NS_BLOCK_LOOK_FOR_DIRTY_FRAMES);
    } else {
      NS_ASSERTION(aChild->IsFloating(), "should be a float");
      nsIFrame* thisFC = FirstContinuation();
      nsIFrame* placeholderPath = aChild->GetPlaceholderFrame();
      // SVG code sometimes sends FrameNeedsReflow notifications during
      // frame destruction, leading to null placeholders, but we're safe
      // ignoring those.
      if (placeholderPath) {
        for (;;) {
          nsIFrame* parent = placeholderPath->GetParent();
          if (parent->GetContent() == mContent &&
              parent->FirstContinuation() == thisFC) {
            parent->AddStateBits(NS_BLOCK_LOOK_FOR_DIRTY_FRAMES);
            break;
          }
          placeholderPath = parent;
        }
        placeholderPath->AddStateBits(NS_FRAME_HAS_DIRTY_CHILDREN);
      }
    }
  }

  nsContainerFrame::ChildIsDirty(aChild);
}

void nsBlockFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                        nsIFrame* aPrevInFlow) {
  // These are all the block specific frame bits, they are copied from
  // the prev-in-flow to a newly created next-in-flow, except for the
  // NS_BLOCK_FLAGS_NON_INHERITED_MASK bits below.
  constexpr nsFrameState NS_BLOCK_FLAGS_MASK =
      NS_BLOCK_BFC | NS_BLOCK_HAS_FIRST_LETTER_STYLE |
      NS_BLOCK_HAS_FIRST_LETTER_CHILD | NS_BLOCK_HAS_MARKER;

  // This is the subset of NS_BLOCK_FLAGS_MASK that is NOT inherited
  // by default.  They should only be set on the first-in-flow.
  constexpr nsFrameState NS_BLOCK_FLAGS_NON_INHERITED_MASK =
      NS_BLOCK_HAS_FIRST_LETTER_CHILD | NS_BLOCK_HAS_MARKER;

  if (aPrevInFlow) {
    // Copy over the inherited block frame bits from the prev-in-flow.
    RemoveStateBits(NS_BLOCK_FLAGS_MASK);
    AddStateBits(aPrevInFlow->GetStateBits() &
                 (NS_BLOCK_FLAGS_MASK & ~NS_BLOCK_FLAGS_NON_INHERITED_MASK));
  }

  nsContainerFrame::Init(aContent, aParent, aPrevInFlow);

  if (!aPrevInFlow ||
      aPrevInFlow->HasAnyStateBits(NS_BLOCK_NEEDS_BIDI_RESOLUTION)) {
    AddStateBits(NS_BLOCK_NEEDS_BIDI_RESOLUTION);
  }

  if (EstablishesBFC(this)) {
    AddStateBits(NS_BLOCK_BFC);
  }

  if (HasAnyStateBits(NS_FRAME_FONT_INFLATION_CONTAINER) &&
      HasAnyStateBits(NS_BLOCK_BFC)) {
    AddStateBits(NS_FRAME_FONT_INFLATION_FLOW_ROOT);
  }
}

void nsBlockFrame::SetInitialChildList(ChildListID aListID,
                                       nsFrameList&& aChildList) {
  if (FrameChildListID::Float == aListID) {
    nsFrameList* floats = EnsureFloats();
    *floats = std::move(aChildList);
  } else if (FrameChildListID::Principal == aListID) {
#ifdef DEBUG
    // The only times a block that is an anonymous box is allowed to have a
    // first-letter frame are when it's the block inside a non-anonymous cell,
    // the block inside a fieldset, button or column set, or a scrolled content
    // block, except for <select>.  Note that this means that blocks which are
    // the anonymous block in {ib} splits do NOT get first-letter frames.
    // Note that NS_BLOCK_HAS_FIRST_LETTER_STYLE gets set on all continuations
    // of the block.
    auto pseudo = Style()->GetPseudoType();
    bool haveFirstLetterStyle =
        (pseudo == PseudoStyleType::NotPseudo ||
         PseudoStyle::IsElementBackedPseudo(pseudo) ||
         (pseudo == PseudoStyleType::MozCellContent &&
          !GetParent()->Style()->IsPseudoOrAnonBox()) ||
         pseudo == PseudoStyleType::MozFieldsetContent ||
         pseudo == PseudoStyleType::MozColumnContent ||
         pseudo == PseudoStyleType::MozScrolledContent ||
         pseudo == PseudoStyleType::MozSvgText) &&
        !IsMathMLFrame() && !IsColumnSetWrapperFrame() &&
        !IsComboboxControlFrame() &&
        RefPtr<ComputedStyle>(GetFirstLetterStyle(PresContext())) != nullptr;
    NS_ASSERTION(haveFirstLetterStyle ==
                     HasAnyStateBits(NS_BLOCK_HAS_FIRST_LETTER_STYLE),
                 "NS_BLOCK_HAS_FIRST_LETTER_STYLE state out of sync");
#endif

    AddFrames(std::move(aChildList), nullptr, nullptr);
  } else {
    nsContainerFrame::SetInitialChildList(aListID, std::move(aChildList));
  }
}

void nsBlockFrame::SetMarkerFrameForListItem(nsIFrame* aMarkerFrame) {
  MOZ_ASSERT(aMarkerFrame);
  MOZ_ASSERT(!HasMarker(), "How can we have a ::marker frame already?");

  if (StyleList()->mListStylePosition == StyleListStylePosition::Inside) {
    SetProperty(InsideMarkerProperty(), aMarkerFrame);
  } else {
    SetProperty(OutsideMarkerProperty(),
                new (PresShell()) nsFrameList(aMarkerFrame, aMarkerFrame));
  }
  AddStateBits(NS_BLOCK_HAS_MARKER);
}

bool nsBlockFrame::MarkerIsEmpty(const nsIFrame* aMarker) const {
  MOZ_ASSERT(mContent->GetPrimaryFrame()->StyleDisplay()->IsListItem() &&
                 aMarker == GetOutsideMarker(),
             "should only care about an outside ::marker");
  const nsStyleList* list = aMarker->StyleList();
  return aMarker->StyleContent()->mContent.IsNone() ||
         (list->mListStyleType.IsNone() && list->mListStyleImage.IsNone() &&
          aMarker->StyleContent()->NonAltContentItems().IsEmpty());
}

bool nsBlockFrame::HasOutsideMarker() const {
  return HasMarker() && HasProperty(OutsideMarkerProperty());
}

void nsBlockFrame::ReflowOutsideMarker(nsIFrame* aMarkerFrame,
                                       BlockReflowState& aState,
                                       ReflowOutput& aMetrics,
                                       nscoord aLineTop) {
  const ReflowInput& ri = aState.mReflowInput;

  WritingMode markerWM = aMarkerFrame->GetWritingMode();
  LogicalSize availSize(markerWM);
  // Make up an inline-size since it doesn't really matter (XXX).
  availSize.ISize(markerWM) = aState.ContentISize();
  availSize.BSize(markerWM) = NS_UNCONSTRAINEDSIZE;

  ReflowInput reflowInput(aState.mPresContext, ri, aMarkerFrame, availSize,
                          Nothing(), {}, {}, {ComputeSizeFlag::ShrinkWrap});
  nsReflowStatus status;
  aMarkerFrame->Reflow(aState.mPresContext, aMetrics, reflowInput, status);

  // Get the float available space using our saved state from before we
  // started reflowing the block, so that we ignore any floats inside
  // the block.
  // FIXME: aLineTop isn't actually set correctly by some callers, since
  // they reposition the line.
  LogicalRect floatAvailSpace =
      aState
          .GetFloatAvailableSpaceWithState(ri.GetWritingMode(), aLineTop,
                                           ShapeType::ShapeOutside,
                                           &aState.mFloatManagerStateBefore)
          .mRect;
  // FIXME (bug 25888): need to check the entire region that the first
  // line overlaps, not just the top pixel.

  // Place the ::marker now.  We want to place the ::marker relative to the
  // border-box of the associated block (using the right/left margin of
  // the ::marker frame as separation).  However, if a line box would be
  // displaced by floats that are *outside* the associated block, we
  // want to displace it by the same amount.  That is, we act as though
  // the edge of the floats is the content-edge of the block, and place
  // the ::marker at a position offset from there by the block's padding,
  // the block's border, and the ::marker frame's margin.

  // IStart from floatAvailSpace gives us the content/float start edge
  // in the current writing mode. Then we subtract out the start
  // border/padding and the ::marker's width and margin to offset the position.
  WritingMode wm = ri.GetWritingMode();
  // Get the ::marker's margin, converted to our writing mode so that we can
  // combine it with other logical values here.
  LogicalMargin markerMargin = reflowInput.ComputedLogicalMargin(wm);
  nscoord iStart = floatAvailSpace.IStart(wm) -
                   ri.ComputedLogicalBorderPadding(wm).IStart(wm) -
                   markerMargin.IEnd(wm) - aMetrics.ISize(wm);

  // Approximate the ::marker's position; vertical alignment will provide
  // the final vertical location. We pass our writing-mode here, because
  // it may be different from the ::marker frame's mode.
  nscoord bStart = floatAvailSpace.BStart(wm);
  aMarkerFrame->SetRect(
      wm,
      LogicalRect(wm, iStart, bStart, aMetrics.ISize(wm), aMetrics.BSize(wm)),
      aState.ContainerSize());
  aMarkerFrame->DidReflow(aState.mPresContext, &aState.mReflowInput);
}

// This is used to scan frames for any float placeholders, add their
// floats to the list represented by aList, and remove the
// floats from whatever list they might be in. We don't search descendants
// that are float containing blocks.  Floats that or not children of 'this'
// are ignored (they are not added to aList).
void nsBlockFrame::DoCollectFloats(nsIFrame* aFrame, nsFrameList& aList,
                                   bool aCollectSiblings) {
  while (aFrame) {
    // Don't descend into float containing blocks.
    if (!aFrame->IsFloatContainingBlock()) {
      nsIFrame* outOfFlowFrame =
          aFrame->IsPlaceholderFrame()
              ? nsLayoutUtils::GetFloatFromPlaceholder(aFrame)
              : nullptr;
      while (outOfFlowFrame && outOfFlowFrame->GetParent() == this) {
        RemoveFloat(outOfFlowFrame);
        // Remove the NS_FRAME_IS_PUSHED_OUT_OF_FLOW bit, in case
        // |outOfFlowFrame| came from the PushedFloats list.
        outOfFlowFrame->RemoveStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW);
        aList.AppendFrame(nullptr, outOfFlowFrame);
        outOfFlowFrame = outOfFlowFrame->GetNextInFlow();
        // FIXME: By not pulling floats whose parent is one of our
        // later siblings, are we risking the pushed floats getting
        // out-of-order?
        // XXXmats nsInlineFrame's lazy reparenting depends on NOT doing that.
      }

      DoCollectFloats(aFrame->PrincipalChildList().FirstChild(), aList, true);
      DoCollectFloats(
          aFrame->GetChildList(FrameChildListID::Overflow).FirstChild(), aList,
          true);
    }
    if (!aCollectSiblings) {
      break;
    }
    aFrame = aFrame->GetNextSibling();
  }
}

void nsBlockFrame::CheckFloats(BlockReflowState& aState) {
#ifdef DEBUG
  // If any line is still dirty, that must mean we're going to reflow this
  // block again soon (e.g. because we bailed out after noticing that
  // clearance was imposed), so don't worry if the floats are out of sync.
  bool anyLineDirty = false;

  // Check that the float list is what we would have built
  AutoTArray<nsIFrame*, 8> lineFloats;
  for (auto& line : Lines()) {
    if (line.HasFloats()) {
      lineFloats.AppendElements(line.Floats());
    }
    if (line.IsDirty()) {
      anyLineDirty = true;
    }
  }

  AutoTArray<nsIFrame*, 8> storedFloats;
  bool equal = true;
  bool hasHiddenFloats = false;
  uint32_t i = 0;
  for (nsIFrame* f : GetChildList(FrameChildListID::Float)) {
    if (f->HasAnyStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW)) {
      continue;
    }
    // There are chances that the float children won't be added to lines,
    // because in nsBlockFrame::ReflowLine, it skips reflow line if the first
    // child of the line is IsHiddenByContentVisibilityOfInFlowParentForLayout.
    // There are also chances that the floats in line are out of date, for
    // instance, lines could reflow if
    // PresShell::IsForcingLayoutForHiddenContent, and after forcingLayout is
    // off, the reflow of lines could be skipped, but the floats are still in
    // there. Here we can't know whether the floats hidden by c-v are included
    // in the lines or not. So we use hasHiddenFloats to skip the float length
    // checking.
    if (!hasHiddenFloats &&
        f->IsHiddenByContentVisibilityOfInFlowParentForLayout()) {
      hasHiddenFloats = true;
    }
    storedFloats.AppendElement(f);
    if (i < lineFloats.Length() && lineFloats.ElementAt(i) != f) {
      equal = false;
    }
    ++i;
  }

  if ((!equal || lineFloats.Length() != storedFloats.Length()) &&
      !anyLineDirty && !hasHiddenFloats) {
    NS_ERROR(
        "nsBlockFrame::CheckFloats: Explicit float list is out of sync with "
        "float cache");
  }
#endif

  const nsFrameList* oofs = GetOverflowOutOfFlows();
  if (oofs && oofs->NotEmpty()) {
    // Floats that were pushed should be removed from our float
    // manager.  Otherwise the float manager's YMost or XMost might
    // be larger than necessary, causing this block to get an
    // incorrect desired height (or width).  Some of these floats
    // may not actually have been added to the float manager because
    // they weren't reflowed before being pushed; that's OK,
    // RemoveRegions will ignore them. It is safe to do this here
    // because we know from here on the float manager will only be
    // used for its XMost and YMost, not to place new floats and
    // lines.
    aState.FloatManager()->RemoveTrailingRegions(oofs->FirstChild());
  }
}

void nsBlockFrame::IsMarginRoot(bool* aBStartMarginRoot,
                                bool* aBEndMarginRoot) {
  nsIFrame* parent = GetParent();
  if (!HasAnyStateBits(NS_BLOCK_BFC)) {
    if (!parent || parent->IsFloatContainingBlock()) {
      *aBStartMarginRoot = false;
      *aBEndMarginRoot = false;
      return;
    }
  }

  if (parent && parent->IsColumnSetFrame()) {
    // The first column is a start margin root and the last column is an end
    // margin root.  (If the column-set is split by a column-span:all box then
    // the first and last column in each column-set fragment are margin roots.)
    *aBStartMarginRoot = GetPrevInFlow() == nullptr;
    *aBEndMarginRoot = GetNextInFlow() == nullptr;
    return;
  }

  *aBStartMarginRoot = true;
  *aBEndMarginRoot = true;
}

/* static */
bool nsBlockFrame::BlockNeedsFloatManager(nsIFrame* aBlock) {
  MOZ_ASSERT(aBlock, "Must have a frame");
  NS_ASSERTION(aBlock->IsBlockFrameOrSubclass(), "aBlock must be a block");

  nsIFrame* parent = aBlock->GetParent();
  return aBlock->HasAnyStateBits(NS_BLOCK_BFC) ||
         (parent && !parent->IsFloatContainingBlock());
}

/* static */
bool nsBlockFrame::BlockCanIntersectFloats(nsIFrame* aFrame) {
  // NS_BLOCK_BFC is block specific bit, check first as an optimization, it's
  // okay because we also check that it is a block frame.
  return !aFrame->HasAnyStateBits(NS_BLOCK_BFC) && !aFrame->IsReplaced() &&
         aFrame->IsBlockFrameOrSubclass();
}

// Note that this width can vary based on the vertical position.
// However, the cases where it varies are the cases where the width fits
// in the available space given, which means that variation shouldn't
// matter.
/* static */
nsBlockFrame::FloatAvoidingISizeToClear nsBlockFrame::ISizeToClearPastFloats(
    const BlockReflowState& aState, const LogicalRect& aFloatAvailableSpace,
    nsIFrame* aFloatAvoidingBlock) {
  nscoord inlineStartOffset, inlineEndOffset;
  WritingMode wm = aState.mReflowInput.GetWritingMode();

  FloatAvoidingISizeToClear result;
  aState.ComputeFloatAvoidingOffsets(aFloatAvoidingBlock, aFloatAvailableSpace,
                                     inlineStartOffset, inlineEndOffset);
  nscoord availISize =
      aState.mContentArea.ISize(wm) - inlineStartOffset - inlineEndOffset;

  // We actually don't want the min width here; see bug 427782; we only
  // want to displace if the width won't compute to a value small enough
  // to fit.
  // All we really need here is the result of ComputeSize, and we
  // could *almost* get that from an SizeComputationInput, except for the
  // last argument.
  WritingMode frWM = aFloatAvoidingBlock->GetWritingMode();
  LogicalSize availSpace =
      LogicalSize(wm, availISize, NS_UNCONSTRAINEDSIZE).ConvertTo(frWM, wm);
  ReflowInput reflowInput(aState.mPresContext, aState.mReflowInput,
                          aFloatAvoidingBlock, availSpace);
  result.borderBoxISize =
      reflowInput.ComputedSizeWithBorderPadding(wm).ISize(wm);

  // Use the margins from sizingInput rather than reflowInput so that
  // they aren't reduced by ignoring margins in overconstrained cases.
  SizeComputationInput sizingInput(aFloatAvoidingBlock,
                                   aState.mReflowInput.mRenderingContext, wm,
                                   aState.mContentArea.ISize(wm));
  const LogicalMargin computedMargin = sizingInput.ComputedLogicalMargin(wm);

  nscoord marginISize = computedMargin.IStartEnd(wm);
  const auto iSize = reflowInput.mStylePosition->ISize(
      wm, AnchorPosResolutionParams::From(&reflowInput));
  if (marginISize < 0 &&
      (iSize->IsAuto() || iSize->BehavesLikeStretchOnInlineAxis())) {
    // If we get here, floatAvoidingBlock has a negative amount of inline-axis
    // margin and an 'auto' (or ~equivalently, -moz-available) inline
    // size. Under these circumstances, we use the margin to establish a
    // (positive) minimum size for the border-box, in order to satisfy the
    // equation in CSS2 10.3.3.  That equation essentially simplifies to the
    // following:
    //
    //   iSize of margins + iSize of borderBox = iSize of containingBlock
    //
    // ...where "iSize of borderBox" is the sum of floatAvoidingBlock's
    // inline-axis components of border, padding, and {width,height}.
    //
    // Right now, in the above equation, "iSize of margins" is the only term
    // that we know for sure. (And we also know that it's negative, since we
    // got here.) The other terms are as-yet unresolved, since the frame has an
    // 'auto' iSize, and since we aren't yet sure if we'll clear this frame
    // beyond floats or place it alongside them.
    //
    // However: we *do* know that the equation's "iSize of containingBlock"
    // term *must* be non-negative, since boxes' widths and heights generally
    // can't be negative in CSS.  To satisfy that requirement, we can then
    // infer that the equation's "iSize of borderBox" term *must* be large
    // enough to cancel out the (known-to-be-negative) "iSize of margins"
    // term. Therefore, marginISize value (negated to make it positive)
    // establishes a lower-bound for how much inline-axis space our border-box
    // will really require in order to fit alongside any floats.
    //
    // XXXdholbert This explanation is admittedly a bit hand-wavy and may not
    // precisely match what any particular spec requires. It's the best
    // reasoning I could come up with to explain engines' behavior.  Also, our
    // behavior with -moz-available doesn't seem particularly correct here, per
    // bug 1767217, though that's probably due to a bug elsewhere in our float
    // handling code...
    result.borderBoxISize = std::max(result.borderBoxISize, -marginISize);
  }

  result.marginIStart = computedMargin.IStart(wm);
  return result;
}

/* static */
nsBlockFrame* nsBlockFrame::GetNearestAncestorBlock(nsIFrame* aCandidate) {
  nsBlockFrame* block = nullptr;
  while (aCandidate) {
    block = do_QueryFrame(aCandidate);
    if (block) {
      // yay, candidate is a block!
      return block;
    }
    // Not a block. Check its parent next.
    aCandidate = aCandidate->GetParent();
  }
  MOZ_ASSERT_UNREACHABLE("Fell off frame tree looking for ancestor block!");
  return nullptr;
}

nscoord nsBlockFrame::ComputeFinalBSize(BlockReflowState& aState,
                                        nscoord aBEndEdgeOfChildren) {
  const WritingMode wm = aState.mReflowInput.GetWritingMode();

  const nscoord effectiveContentBoxBSize =
      GetEffectiveComputedBSize(aState.mReflowInput, aState.mConsumedBSize);
  const nscoord blockStartBP = aState.BorderPadding().BStart(wm);
  const nscoord blockEndBP = aState.BorderPadding().BEnd(wm);

  NS_ASSERTION(
      !IsTrueOverflowContainer() || (effectiveContentBoxBSize == 0 &&
                                     blockStartBP == 0 && blockEndBP == 0),
      "An overflow container's effective content-box block-size, block-start "
      "BP, and block-end BP should all be zero!");

  const nscoord effectiveContentBoxBSizeWithBStartBP =
      NSCoordSaturatingAdd(blockStartBP, effectiveContentBoxBSize);
  const nscoord effectiveBorderBoxBSize =
      NSCoordSaturatingAdd(effectiveContentBoxBSizeWithBStartBP, blockEndBP);

  if (HasColumnSpanSiblings()) {
    MOZ_ASSERT(LastInFlow()->GetNextContinuation(),
               "Frame constructor should've created column-span siblings!");

    // If a block is split by any column-spans, we calculate the final
    // block-size by shrinkwrapping our children's block-size for all the
    // fragments except for those after the final column-span, but we should
    // take no more than our effective border-box block-size. If there's any
    // leftover block-size, our next continuations will take up rest.
    //
    // We don't need to adjust aBri.mReflowStatus because our children's status
    // is the same as ours.
    return std::min(effectiveBorderBoxBSize, aBEndEdgeOfChildren);
  }

  const nscoord availBSize = aState.mReflowInput.AvailableBSize();
  if (availBSize == NS_UNCONSTRAINEDSIZE) {
    return effectiveBorderBoxBSize;
  }

  // Save our children's reflow status.
  const bool isChildStatusComplete = aState.mReflowStatus.IsComplete();
  if (isChildStatusComplete && effectiveContentBoxBSize > 0 &&
      effectiveBorderBoxBSize > availBSize &&
      ShouldAvoidBreakInside(aState.mReflowInput)) {
    aState.mReflowStatus.SetInlineLineBreakBeforeAndReset();
    return effectiveBorderBoxBSize;
  }

  const bool isBDBClone =
      aState.mReflowInput.mStyleBorder->mBoxDecorationBreak ==
      StyleBoxDecorationBreak::Clone;

  // The maximum value our content-box block-size can take within the given
  // available block-size.
  const nscoord maxContentBoxBSize = aState.ContentBSize();

  // The block-end edge of our content-box (relative to this frame's origin) if
  // we consumed the maximum block-size available to us (maxContentBoxBSize).
  const nscoord maxContentBoxBEnd = aState.ContentBEnd();

  // These variables are uninitialized intentionally so that the compiler can
  // check they are assigned in every if-else branch below.
  nscoord finalContentBoxBSizeWithBStartBP;
  bool isOurStatusComplete;

  if (effectiveBorderBoxBSize <= availBSize) {
    // Our effective border-box block-size can fit in the available block-size,
    // so we are complete.
    finalContentBoxBSizeWithBStartBP = effectiveContentBoxBSizeWithBStartBP;
    isOurStatusComplete = true;
  } else if (effectiveContentBoxBSizeWithBStartBP <= maxContentBoxBEnd) {
    // Note: The following assertion should generally hold because, for
    // box-decoration-break:clone, this "else if" branch is mathematically
    // equivalent to the initial "if".
    NS_ASSERTION(!isBDBClone,
                 "This else-if branch is handling a situation that's specific "
                 "to box-decoration-break:slice, i.e. a case when we can skip "
                 "our block-end border and padding!");

    // Our effective content-box block-size plus the block-start border and
    // padding can fit in the available block-size, but it cannot fit after
    // adding the block-end border and padding. Thus, we need a continuation
    // (unless we already weren't asking for any block-size, in which case we
    // stay complete to avoid looping forever).
    finalContentBoxBSizeWithBStartBP = effectiveContentBoxBSizeWithBStartBP;
    isOurStatusComplete = effectiveContentBoxBSize == 0;
  } else {
    // We aren't going to be able to fit our content-box in the space available
    // to it, which means we'll probably call ourselves incomplete to request a
    // continuation. But before making that decision, we check for certain
    // conditions which would force us to overflow beyond the available space --
    // these might result in us actually being complete if we're forced to
    // overflow far enough.
    if (MOZ_UNLIKELY(aState.mReflowInput.mFlags.mIsTopOfPage && isBDBClone &&
                     maxContentBoxBSize <= 0 &&
                     aBEndEdgeOfChildren == blockStartBP)) {
      // In this rare case, we are at the top of page/column, we have
      // box-decoration-break:clone and zero available block-size for our
      // content-box (e.g. our own block-start border and padding already exceed
      // the available block-size), and we didn't lay out any child to consume
      // our content-box block-size. To ensure we make progress (avoid looping
      // forever), use 1px as our content-box block-size regardless of our
      // effective content-box block-size, in the spirit of
      // https://drafts.csswg.org/css-break/#breaking-rules.
      finalContentBoxBSizeWithBStartBP = blockStartBP + AppUnitsPerCSSPixel();
      isOurStatusComplete = effectiveContentBoxBSize <= AppUnitsPerCSSPixel();
    } else if (aBEndEdgeOfChildren > maxContentBoxBEnd) {
      // We have a unbreakable child whose block-end edge exceeds the available
      // block-size for children.
      if (aBEndEdgeOfChildren >= effectiveContentBoxBSizeWithBStartBP) {
        // The unbreakable child's block-end edge forces us to consume all of
        // our effective content-box block-size.
        finalContentBoxBSizeWithBStartBP = effectiveContentBoxBSizeWithBStartBP;

        // Even though we've consumed all of our effective content-box
        // block-size, we may still need to report an incomplete status in order
        // to get another continuation, which will be responsible for laying out
        // & drawing our block-end border & padding. But if we have no such
        // border & padding, or if we're forced to apply that border & padding
        // on this frame due to box-decoration-break:clone, then we don't need
        // to bother with that additional continuation.
        isOurStatusComplete = (isBDBClone || blockEndBP == 0);
      } else {
        // The unbreakable child's block-end edge doesn't force us to consume
        // all of our effective content-box block-size.
        finalContentBoxBSizeWithBStartBP = aBEndEdgeOfChildren;
        isOurStatusComplete = false;
      }
    } else {
      // The children's block-end edge can fit in the content-box space that we
      // have available for it. Consume all the space that is available so that
      // our inline-start/inline-end borders extend all the way to the block-end
      // edge of column/page.
      finalContentBoxBSizeWithBStartBP = maxContentBoxBEnd;
      isOurStatusComplete = false;
    }
  }

  nscoord finalBorderBoxBSize = finalContentBoxBSizeWithBStartBP;
  if (isOurStatusComplete) {
    finalBorderBoxBSize = NSCoordSaturatingAdd(finalBorderBoxBSize, blockEndBP);
    if (isChildStatusComplete) {
      // We want to use children's reflow status as ours, which can be overflow
      // incomplete. Suppress the urge to call aBri.mReflowStatus.Reset() here.
    } else {
      aState.mReflowStatus.SetOverflowIncomplete();
    }
  } else {
    NS_ASSERTION(!IsTrueOverflowContainer(),
                 "An overflow container should always be complete because of "
                 "its zero border-box block-size!");
    if (isBDBClone) {
      finalBorderBoxBSize =
          NSCoordSaturatingAdd(finalBorderBoxBSize, blockEndBP);
    }
    aState.mReflowStatus.SetIncomplete();
    if (!GetNextInFlow()) {
      aState.mReflowStatus.SetNextInFlowNeedsReflow();
    }
  }

  return finalBorderBoxBSize;
}

nsresult nsBlockFrame::ResolveBidi() {
  NS_ASSERTION(!GetPrevInFlow(),
               "ResolveBidi called on non-first continuation");
  MOZ_ASSERT(PresContext()->BidiEnabled());
  return nsBidiPresUtils::Resolve(this);
}

void nsBlockFrame::UpdatePseudoElementStyles(ServoRestyleState& aRestyleState) {
  // first-letter needs to be updated before first-line, because first-line can
  // change the style of the first-letter.
  if (HasFirstLetterChild()) {
    UpdateFirstLetterStyle(aRestyleState);
  }

  if (nsIFrame* firstLineFrame = GetFirstLineFrame()) {
    nsIFrame* styleParent = CorrectStyleParentFrame(firstLineFrame->GetParent(),
                                                    PseudoStyleType::FirstLine);

    ComputedStyle* parentStyle = styleParent->Style();
    RefPtr<ComputedStyle> firstLineStyle =
        aRestyleState.StyleSet().ResolvePseudoElementStyle(
            *mContent->AsElement(), PseudoStyleType::FirstLine, nullptr,
            parentStyle);

    // FIXME(bz): Can we make first-line continuations be non-inheriting anon
    // boxes?
    RefPtr<ComputedStyle> continuationStyle =
        aRestyleState.StyleSet().ResolveInheritingAnonymousBoxStyle(
            PseudoStyleType::MozLineFrame, parentStyle);

    UpdateStyleOfOwnedChildFrame(firstLineFrame, firstLineStyle, aRestyleState,
                                 Some(continuationStyle.get()));

    // We also want to update the styles of the first-line's descendants.  We
    // don't need to compute a changehint for this, though, since any changes to
    // them are handled by the first-line anyway.
    RestyleManager* manager = PresContext()->RestyleManager();
    for (nsIFrame* kid : firstLineFrame->PrincipalChildList()) {
      manager->ReparentComputedStyleForFirstLine(kid);
    }
  }
}

nsIFrame* nsBlockFrame::GetFirstLetter() const {
  if (!HasAnyStateBits(NS_BLOCK_HAS_FIRST_LETTER_STYLE)) {
    // Certainly no first-letter frame.
    return nullptr;
  }

  return GetProperty(FirstLetterProperty());
}

nsIFrame* nsBlockFrame::GetFirstLineFrame() const {
  nsIFrame* maybeFirstLine = PrincipalChildList().FirstChild();
  if (maybeFirstLine && maybeFirstLine->IsLineFrame()) {
    return maybeFirstLine;
  }

  return nullptr;
}

#ifdef DEBUG
void nsBlockFrame::VerifyLines(bool aFinalCheckOK) {
  if (!gVerifyLines) {
    return;
  }
  if (mLines.empty()) {
    return;
  }

  nsLineBox* cursor = GetLineCursorForQuery();

  // Add up the counts on each line. Also validate that IsFirstLine is
  // set properly.
  int32_t count = 0;
  for (const auto& line : Lines()) {
    if (&line == cursor) {
      cursor = nullptr;
    }
    if (aFinalCheckOK) {
      MOZ_ASSERT(line.GetChildCount(), "empty line");
      if (line.IsBlock()) {
        NS_ASSERTION(1 == line.GetChildCount(), "bad first line");
      }
    }
    count += line.GetChildCount();
  }

  // Then count the frames
  int32_t frameCount = 0;
  nsIFrame* frame = mLines.front()->mFirstChild;
  while (frame) {
    frameCount++;
    frame = frame->GetNextSibling();
  }
  NS_ASSERTION(count == frameCount, "bad line list");

  // Next: test that each line has right number of frames on it
  for (LineIterator line = LinesBegin(), line_end = LinesEnd();
       line != line_end;) {
    count = line->GetChildCount();
    frame = line->mFirstChild;
    while (--count >= 0) {
      frame = frame->GetNextSibling();
    }
    ++line;
    if ((line != line_end) && (0 != line->GetChildCount())) {
      NS_ASSERTION(frame == line->mFirstChild, "bad line list");
    }
  }

  if (cursor) {
    FrameLines* overflowLines = GetOverflowLines();
    if (overflowLines) {
      LineIterator line = overflowLines->mLines.begin();
      LineIterator line_end = overflowLines->mLines.end();
      for (; line != line_end; ++line) {
        if (line == cursor) {
          cursor = nullptr;
          break;
        }
      }
    }
  }
  NS_ASSERTION(!cursor, "stale LineCursorProperty");
}

void nsBlockFrame::VerifyOverflowSituation() {
  // Overflow out-of-flows must not have a next-in-flow in floats list or
  // mFrames.
  nsFrameList* oofs = GetOverflowOutOfFlows();
  if (oofs) {
    for (nsIFrame* f : *oofs) {
      nsIFrame* nif = f->GetNextInFlow();
      MOZ_ASSERT(!nif ||
                 (!GetChildList(FrameChildListID::Float).ContainsFrame(nif) &&
                  !mFrames.ContainsFrame(nif)));
    }
  }

  // Pushed floats must not have a next-in-flow in floats list or mFrames.
  oofs = GetPushedFloats();
  if (oofs) {
    for (nsIFrame* f : *oofs) {
      nsIFrame* nif = f->GetNextInFlow();
      MOZ_ASSERT(!nif ||
                 (!GetChildList(FrameChildListID::Float).ContainsFrame(nif) &&
                  !mFrames.ContainsFrame(nif)));
    }
  }

  // A child float next-in-flow's parent must be |this| or a next-in-flow of
  // |this|. Later next-in-flows must have the same or later parents.
  ChildListID childLists[] = {FrameChildListID::Float,
                              FrameChildListID::PushedFloats};
  for (const auto childList : childLists) {
    const nsFrameList& children = GetChildList(childList);
    for (nsIFrame* f : children) {
      nsIFrame* parent = this;
      nsIFrame* nif = f->GetNextInFlow();
      for (; nif; nif = nif->GetNextInFlow()) {
        bool found = false;
        for (nsIFrame* p = parent; p; p = p->GetNextInFlow()) {
          if (nif->GetParent() == p) {
            parent = p;
            found = true;
            break;
          }
        }
        MOZ_ASSERT(
            found,
            "next-in-flow is a child of parent earlier in the frame tree?");
      }
    }
  }

  nsBlockFrame* flow = static_cast<nsBlockFrame*>(FirstInFlow());
  while (flow) {
    FrameLines* overflowLines = flow->GetOverflowLines();
    if (overflowLines) {
      NS_ASSERTION(!overflowLines->mLines.empty(),
                   "should not be empty if present");
      NS_ASSERTION(overflowLines->mLines.front()->mFirstChild,
                   "bad overflow lines");
      NS_ASSERTION(overflowLines->mLines.front()->mFirstChild ==
                       overflowLines->mFrames.FirstChild(),
                   "bad overflow frames / lines");
    }
    auto checkCursor = [&](nsLineBox* cursor) -> bool {
      if (!cursor) {
        return true;
      }
      LineIterator line = flow->LinesBegin();
      LineIterator line_end = flow->LinesEnd();
      for (; line != line_end && line != cursor; ++line);
      if (line == line_end && overflowLines) {
        line = overflowLines->mLines.begin();
        line_end = overflowLines->mLines.end();
        for (; line != line_end && line != cursor; ++line);
      }
      return line != line_end;
    };
    MOZ_ASSERT(checkCursor(flow->GetLineCursorForDisplay()),
               "stale LineCursorPropertyDisplay");
    MOZ_ASSERT(checkCursor(flow->GetLineCursorForQuery()),
               "stale LineCursorPropertyQuery");
    flow = static_cast<nsBlockFrame*>(flow->GetNextInFlow());
  }
}

int32_t nsBlockFrame::GetDepth() const {
  int32_t depth = 0;
  nsIFrame* parent = GetParent();
  while (parent) {
    parent = parent->GetParent();
    depth++;
  }
  return depth;
}

already_AddRefed<ComputedStyle> nsBlockFrame::GetFirstLetterStyle(
    nsPresContext* aPresContext) {
  return aPresContext->StyleSet()->ProbePseudoElementStyle(
      *mContent->AsElement(), PseudoStyleType::FirstLetter, nullptr, Style());
}
#endif
