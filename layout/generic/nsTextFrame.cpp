/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for textual content of elements */

#include "nsTextFrame.h"

#include "gfx2DGlue.h"

#include "gfxUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/CaretAssociationHint.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/Likely.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPresData.h"
#include "mozilla/SVGTextFrame.h"
#include "mozilla/TextEditor.h"
#include "mozilla/TextEvents.h"
#include "mozilla/BinarySearch.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/Unused.h"
#include "mozilla/PodOperations.h"
#include "mozilla/dom/PerformanceMainThread.h"

#include "nsCOMPtr.h"
#include "nsBlockFrame.h"
#include "nsFontMetrics.h"
#include "nsSplittableFrame.h"
#include "nsLineLayout.h"
#include "nsString.h"
#include "nsUnicharUtils.h"
#include "nsPresContext.h"
#include "nsIContent.h"
#include "nsStyleConsts.h"
#include "nsStyleStruct.h"
#include "nsStyleStructInlines.h"
#include "nsCoord.h"
#include "gfxContext.h"
#include "nsTArray.h"
#include "nsCSSPseudoElements.h"
#include "nsCSSFrameConstructor.h"
#include "nsCompatibility.h"
#include "nsCSSColorUtils.h"
#include "nsLayoutUtils.h"
#include "nsDisplayList.h"
#include "nsIFrame.h"
#include "nsIMathMLFrame.h"
#include "nsFirstLetterFrame.h"
#include "nsPlaceholderFrame.h"
#include "nsTextFrameUtils.h"
#include "nsTextPaintStyle.h"
#include "nsTextRunTransformations.h"
#include "MathMLTextRunFactory.h"
#include "nsUnicodeProperties.h"
#include "nsStyleUtil.h"
#include "nsRubyFrame.h"
#include "PresShellInlines.h"
#include "TextDrawTarget.h"

#include "nsTextFragment.h"
#include "nsGkAtoms.h"
#include "nsFrameSelection.h"
#include "nsRange.h"
#include "nsCSSRendering.h"
#include "nsContentUtils.h"
#include "nsLineBreaker.h"
#include "nsIFrameInlines.h"
#include "mozilla/intl/Bidi.h"
#include "mozilla/intl/Segmenter.h"
#include "mozilla/intl/UnicodeProperties.h"
#include "mozilla/ServoStyleSet.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif

#include "nsPrintfCString.h"

#include "mozilla/gfx/DrawTargetRecording.h"

#include "mozilla/UniquePtr.h"
#include "mozilla/dom/Element.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/ProfilerLabels.h"

#ifdef DEBUG
#  undef NOISY_REFLOW
#  undef NOISY_TRIM
#else
#  undef NOISY_REFLOW
#  undef NOISY_TRIM
#endif

#ifdef DrawText
#  undef DrawText
#endif

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;

typedef mozilla::layout::TextDrawTarget TextDrawTarget;

static bool NeedsToMaskPassword(nsTextFrame* aFrame) {
  MOZ_ASSERT(aFrame);
  MOZ_ASSERT(aFrame->GetContent());
  if (!aFrame->GetContent()->HasFlag(NS_MAYBE_MASKED)) {
    return false;
  }
  nsIFrame* frame =
      nsLayoutUtils::GetClosestFrameOfType(aFrame, LayoutFrameType::TextInput);
  MOZ_ASSERT(frame, "How do we have a masked text node without a text input?");
  return !frame || !frame->GetContent()->AsElement()->State().HasState(
                       ElementState::REVEALED);
}

struct TabWidth {
  TabWidth(uint32_t aOffset, uint32_t aWidth)
      : mOffset(aOffset), mWidth(float(aWidth)) {}

  uint32_t mOffset;  // DOM offset relative to the current frame's offset.
  float mWidth;      // extra space to be added at this position (in app units)
};

struct nsTextFrame::TabWidthStore {
  explicit TabWidthStore(int32_t aValidForContentOffset)
      : mLimit(0), mValidForContentOffset(aValidForContentOffset) {}

  // Apply tab widths to the aSpacing array, which corresponds to characters
  // beginning at aOffset and has length aLength. (Width records outside this
  // range will be ignored.)
  void ApplySpacing(gfxTextRun::PropertyProvider::Spacing* aSpacing,
                    uint32_t aOffset, uint32_t aLength);

  // Offset up to which tabs have been measured; positions beyond this have not
  // been calculated yet but may be appended if needed later.  It's a DOM
  // offset relative to the current frame's offset.
  uint32_t mLimit;

  // Need to recalc tab offsets if frame content offset differs from this.
  int32_t mValidForContentOffset;

  // A TabWidth record for each tab character measured so far.
  nsTArray<TabWidth> mWidths;
};

namespace {

struct TabwidthAdaptor {
  const nsTArray<TabWidth>& mWidths;
  explicit TabwidthAdaptor(const nsTArray<TabWidth>& aWidths)
      : mWidths(aWidths) {}
  uint32_t operator[](size_t aIdx) const { return mWidths[aIdx].mOffset; }
};

}  // namespace

void nsTextFrame::TabWidthStore::ApplySpacing(
    gfxTextRun::PropertyProvider::Spacing* aSpacing, uint32_t aOffset,
    uint32_t aLength) {
  size_t i = 0;
  const size_t len = mWidths.Length();

  // If aOffset is non-zero, do a binary search to find where to start
  // processing the tab widths, in case the list is really long. (See bug
  // 953247.)
  // We need to start from the first entry where mOffset >= aOffset.
  if (aOffset > 0) {
    mozilla::BinarySearch(TabwidthAdaptor(mWidths), 0, len, aOffset, &i);
  }

  uint32_t limit = aOffset + aLength;
  while (i < len) {
    const TabWidth& tw = mWidths[i];
    if (tw.mOffset >= limit) {
      break;
    }
    aSpacing[tw.mOffset - aOffset].mAfter += tw.mWidth;
    i++;
  }
}

NS_DECLARE_FRAME_PROPERTY_DELETABLE(TabWidthProperty,
                                    nsTextFrame::TabWidthStore)

NS_DECLARE_FRAME_PROPERTY_WITHOUT_DTOR(OffsetToFrameProperty, nsTextFrame)

NS_DECLARE_FRAME_PROPERTY_RELEASABLE(UninflatedTextRunProperty, gfxTextRun)

NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(FontSizeInflationProperty, float)

NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(HangableWhitespaceProperty, nscoord)
NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(TrimmableWhitespaceProperty,
                                      gfxTextRun::TrimmableWS)

struct nsTextFrame::PaintTextSelectionParams : nsTextFrame::PaintTextParams {
  Point textBaselinePt;
  PropertyProvider* provider = nullptr;
  Range contentRange;
  nsTextPaintStyle* textPaintStyle = nullptr;
  Range glyphRange;
  explicit PaintTextSelectionParams(const PaintTextParams& aParams)
      : PaintTextParams(aParams) {}
};

struct nsTextFrame::DrawTextRunParams {
  gfxContext* context;
  mozilla::gfx::PaletteCache& paletteCache;
  PropertyProvider* provider = nullptr;
  gfxFloat* advanceWidth = nullptr;
  mozilla::SVGContextPaint* contextPaint = nullptr;
  DrawPathCallbacks* callbacks = nullptr;
  nscolor textColor = NS_RGBA(0, 0, 0, 0);
  nscolor textStrokeColor = NS_RGBA(0, 0, 0, 0);
  nsAtom* fontPalette = nullptr;
  float textStrokeWidth = 0.0f;
  bool drawSoftHyphen = false;
  bool hasTextShadow = false;
  bool paintingShadows = false;
  DrawTextRunParams(gfxContext* aContext,
                    mozilla::gfx::PaletteCache& aPaletteCache)
      : context(aContext), paletteCache(aPaletteCache) {}
};

struct nsTextFrame::ClipEdges {
  ClipEdges(const nsIFrame* aFrame, const nsPoint& aToReferenceFrame,
            nscoord aVisIStartEdge, nscoord aVisIEndEdge) {
    nsRect r = aFrame->ScrollableOverflowRect() + aToReferenceFrame;
    if (aFrame->GetWritingMode().IsVertical()) {
      mVisIStart = aVisIStartEdge > 0 ? r.y + aVisIStartEdge : nscoord_MIN;
      mVisIEnd = aVisIEndEdge > 0
                     ? std::max(r.YMost() - aVisIEndEdge, mVisIStart)
                     : nscoord_MAX;
    } else {
      mVisIStart = aVisIStartEdge > 0 ? r.x + aVisIStartEdge : nscoord_MIN;
      mVisIEnd = aVisIEndEdge > 0
                     ? std::max(r.XMost() - aVisIEndEdge, mVisIStart)
                     : nscoord_MAX;
    }
  }

  void Intersect(nscoord* aVisIStart, nscoord* aVisISize) const {
    nscoord end = *aVisIStart + *aVisISize;
    *aVisIStart = std::max(*aVisIStart, mVisIStart);
    *aVisISize = std::max(std::min(end, mVisIEnd) - *aVisIStart, 0);
  }

  nscoord mVisIStart;
  nscoord mVisIEnd;
};

struct nsTextFrame::DrawTextParams : nsTextFrame::DrawTextRunParams {
  Point framePt;
  LayoutDeviceRect dirtyRect;
  const nsTextPaintStyle* textStyle = nullptr;
  const ClipEdges* clipEdges = nullptr;
  const nscolor* decorationOverrideColor = nullptr;
  Range glyphRange;
  DrawTextParams(gfxContext* aContext,
                 mozilla::gfx::PaletteCache& aPaletteCache)
      : DrawTextRunParams(aContext, aPaletteCache) {}
};

struct nsTextFrame::PaintShadowParams {
  gfxTextRun::Range range;
  LayoutDeviceRect dirtyRect;
  Point framePt;
  Point textBaselinePt;
  gfxContext* context;
  DrawPathCallbacks* callbacks = nullptr;
  nscolor foregroundColor = NS_RGBA(0, 0, 0, 0);
  const ClipEdges* clipEdges = nullptr;
  PropertyProvider* provider = nullptr;
  nscoord leftSideOffset = 0;
  explicit PaintShadowParams(const PaintTextParams& aParams)
      : dirtyRect(aParams.dirtyRect),
        framePt(aParams.framePt),
        context(aParams.context) {}
};

/**
 * A glyph observer for the change of a font glyph in a text run.
 *
 * This is stored in {Simple, Complex}TextRunUserData.
 */
class GlyphObserver final : public gfxFont::GlyphChangeObserver {
 public:
  GlyphObserver(gfxFont* aFont, gfxTextRun* aTextRun)
      : gfxFont::GlyphChangeObserver(aFont), mTextRun(aTextRun) {
    MOZ_ASSERT(aTextRun->GetUserData());
  }
  void NotifyGlyphsChanged() override;

 private:
  gfxTextRun* mTextRun;
};

static const nsFrameState TEXT_REFLOW_FLAGS =
    TEXT_FIRST_LETTER | TEXT_START_OF_LINE | TEXT_END_OF_LINE |
    TEXT_HYPHEN_BREAK | TEXT_TRIMMED_TRAILING_WHITESPACE |
    TEXT_JUSTIFICATION_ENABLED | TEXT_HAS_NONCOLLAPSED_CHARACTERS |
    TEXT_SELECTION_UNDERLINE_OVERFLOWED | TEXT_NO_RENDERED_GLYPHS;

static const nsFrameState TEXT_WHITESPACE_FLAGS =
    TEXT_IS_ONLY_WHITESPACE | TEXT_ISNOT_ONLY_WHITESPACE;

/*
 * Some general notes
 *
 * Text frames delegate work to gfxTextRun objects. The gfxTextRun object
 * transforms text to positioned glyphs. It can report the geometry of the
 * glyphs and paint them. Text frames configure gfxTextRuns by providing text,
 * spacing, language, and other information.
 *
 * A gfxTextRun can cover more than one DOM text node. This is necessary to
 * get kerning, ligatures and shaping for text that spans multiple text nodes
 * but is all the same font.
 *
 * The userdata for a gfxTextRun object can be:
 *
 *   - A nsTextFrame* in the case a text run maps to only one flow. In this
 *   case, the textrun's user data pointer is a pointer to mStartFrame for that
 *   flow, mDOMOffsetToBeforeTransformOffset is zero, and mContentLength is the
 *   length of the text node.
 *
 *   - A SimpleTextRunUserData in the case a text run maps to one flow, but we
 *   still have to keep a list of glyph observers.
 *
 *   - A ComplexTextRunUserData in the case a text run maps to multiple flows,
 *   but we need to keep a list of glyph observers.
 *
 *   - A TextRunUserData in the case a text run maps multiple flows, but it
 *   doesn't have any glyph observer for changes in SVG fonts.
 *
 * You can differentiate between the four different cases with the
 * IsSimpleFlow and MightHaveGlyphChanges flags.
 *
 * We go to considerable effort to make sure things work even if in-flow
 * siblings have different ComputedStyles (i.e., first-letter and first-line).
 *
 * Our convention is that unsigned integer character offsets are offsets into
 * the transformed string. Signed integer character offsets are offsets into
 * the DOM string.
 *
 * XXX currently we don't handle hyphenated breaks between text frames where the
 * hyphen occurs at the end of the first text frame, e.g.
 *   <b>Kit&shy;</b>ty
 */

/**
 * This is our user data for the textrun, when textRun->GetFlags2() has
 * IsSimpleFlow set, and also MightHaveGlyphChanges.
 *
 * This allows having an array of observers if there are fonts whose glyphs
 * might change, but also avoid allocation in the simple case that there aren't.
 */
struct SimpleTextRunUserData {
  nsTArray<UniquePtr<GlyphObserver>> mGlyphObservers;
  nsTextFrame* mFrame;
  explicit SimpleTextRunUserData(nsTextFrame* aFrame) : mFrame(aFrame) {}
};

/**
 * We use an array of these objects to record which text frames
 * are associated with the textrun. mStartFrame is the start of a list of
 * text frames. Some sequence of its continuations are covered by the textrun.
 * A content textnode can have at most one TextRunMappedFlow associated with it
 * for a given textrun.
 *
 * mDOMOffsetToBeforeTransformOffset is added to DOM offsets for those frames to
 * obtain the offset into the before-transformation text of the textrun. It can
 * be positive (when a text node starts in the middle of a text run) or negative
 * (when a text run starts in the middle of a text node). Of course it can also
 * be zero.
 */
struct TextRunMappedFlow {
  nsTextFrame* mStartFrame;
  int32_t mDOMOffsetToBeforeTransformOffset;
  // The text mapped starts at mStartFrame->GetContentOffset() and is this long
  uint32_t mContentLength;
};

/**
 * This is the type in the gfxTextRun's userdata field in the common case that
 * the text run maps to multiple flows, but no fonts have been found with
 * animatable glyphs.
 *
 * This way, we avoid allocating and constructing the extra nsTArray.
 */
struct TextRunUserData {
#ifdef DEBUG
  TextRunMappedFlow* mMappedFlows;
#endif
  uint32_t mMappedFlowCount;
  uint32_t mLastFlowIndex;
};

/**
 * This is our user data for the textrun, when textRun->GetFlags2() does not
 * have IsSimpleFlow set and has the MightHaveGlyphChanges flag.
 */
struct ComplexTextRunUserData : public TextRunUserData {
  nsTArray<UniquePtr<GlyphObserver>> mGlyphObservers;
};

static TextRunUserData* CreateUserData(uint32_t aMappedFlowCount) {
  TextRunUserData* data = static_cast<TextRunUserData*>(moz_xmalloc(
      sizeof(TextRunUserData) + aMappedFlowCount * sizeof(TextRunMappedFlow)));
#ifdef DEBUG
  data->mMappedFlows = reinterpret_cast<TextRunMappedFlow*>(data + 1);
#endif
  data->mMappedFlowCount = aMappedFlowCount;
  data->mLastFlowIndex = 0;
  return data;
}

static void DestroyUserData(TextRunUserData* aUserData) {
  if (aUserData) {
    free(aUserData);
  }
}

static ComplexTextRunUserData* CreateComplexUserData(
    uint32_t aMappedFlowCount) {
  ComplexTextRunUserData* data = static_cast<ComplexTextRunUserData*>(
      moz_xmalloc(sizeof(ComplexTextRunUserData) +
                  aMappedFlowCount * sizeof(TextRunMappedFlow)));
  new (data) ComplexTextRunUserData();
#ifdef DEBUG
  data->mMappedFlows = reinterpret_cast<TextRunMappedFlow*>(data + 1);
#endif
  data->mMappedFlowCount = aMappedFlowCount;
  data->mLastFlowIndex = 0;
  return data;
}

static void DestroyComplexUserData(ComplexTextRunUserData* aUserData) {
  if (aUserData) {
    aUserData->~ComplexTextRunUserData();
    free(aUserData);
  }
}

static void DestroyTextRunUserData(gfxTextRun* aTextRun) {
  MOZ_ASSERT(aTextRun->GetUserData());
  if (aTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsSimpleFlow) {
    if (aTextRun->GetFlags2() &
        nsTextFrameUtils::Flags::MightHaveGlyphChanges) {
      delete static_cast<SimpleTextRunUserData*>(aTextRun->GetUserData());
    }
  } else {
    if (aTextRun->GetFlags2() &
        nsTextFrameUtils::Flags::MightHaveGlyphChanges) {
      DestroyComplexUserData(
          static_cast<ComplexTextRunUserData*>(aTextRun->GetUserData()));
    } else {
      DestroyUserData(static_cast<TextRunUserData*>(aTextRun->GetUserData()));
    }
  }
  aTextRun->ClearFlagBits(nsTextFrameUtils::Flags::MightHaveGlyphChanges);
  aTextRun->SetUserData(nullptr);
}

static TextRunMappedFlow* GetMappedFlows(const gfxTextRun* aTextRun) {
  MOZ_ASSERT(aTextRun->GetUserData(), "UserData must exist.");
  MOZ_ASSERT(!(aTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsSimpleFlow),
             "The method should not be called for simple flows.");
  TextRunMappedFlow* flows;
  if (aTextRun->GetFlags2() & nsTextFrameUtils::Flags::MightHaveGlyphChanges) {
    flows = reinterpret_cast<TextRunMappedFlow*>(
        static_cast<ComplexTextRunUserData*>(aTextRun->GetUserData()) + 1);
  } else {
    flows = reinterpret_cast<TextRunMappedFlow*>(
        static_cast<TextRunUserData*>(aTextRun->GetUserData()) + 1);
  }
  MOZ_ASSERT(
      static_cast<TextRunUserData*>(aTextRun->GetUserData())->mMappedFlows ==
          flows,
      "GetMappedFlows should return the same pointer as mMappedFlows.");
  return flows;
}

/**
 * These are utility functions just for helping with the complexity related with
 * the text runs user data.
 */
static nsTextFrame* GetFrameForSimpleFlow(const gfxTextRun* aTextRun) {
  MOZ_ASSERT(aTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsSimpleFlow,
             "Not so simple flow?");
  if (aTextRun->GetFlags2() & nsTextFrameUtils::Flags::MightHaveGlyphChanges) {
    return static_cast<SimpleTextRunUserData*>(aTextRun->GetUserData())->mFrame;
  }

  return static_cast<nsTextFrame*>(aTextRun->GetUserData());
}

/**
 * Remove |aTextRun| from the frame continuation chain starting at
 * |aStartContinuation| if non-null, otherwise starting at |aFrame|.
 * Unmark |aFrame| as a text run owner if it's the frame we start at.
 * Return true if |aStartContinuation| is non-null and was found
 * in the next-continuation chain of |aFrame|.
 */
static bool ClearAllTextRunReferences(nsTextFrame* aFrame, gfxTextRun* aTextRun,
                                      nsTextFrame* aStartContinuation,
                                      nsFrameState aWhichTextRunState) {
  MOZ_ASSERT(aFrame, "null frame");
  MOZ_ASSERT(!aStartContinuation ||
                 (!aStartContinuation->GetTextRun(nsTextFrame::eInflated) ||
                  aStartContinuation->GetTextRun(nsTextFrame::eInflated) ==
                      aTextRun) ||
                 (!aStartContinuation->GetTextRun(nsTextFrame::eNotInflated) ||
                  aStartContinuation->GetTextRun(nsTextFrame::eNotInflated) ==
                      aTextRun),
             "wrong aStartContinuation for this text run");

  if (!aStartContinuation || aStartContinuation == aFrame) {
    aFrame->RemoveStateBits(aWhichTextRunState);
  } else {
    do {
      NS_ASSERTION(aFrame->IsTextFrame(), "Bad frame");
      aFrame = aFrame->GetNextContinuation();
    } while (aFrame && aFrame != aStartContinuation);
  }
  bool found = aStartContinuation == aFrame;
  while (aFrame) {
    NS_ASSERTION(aFrame->IsTextFrame(), "Bad frame");
    if (!aFrame->RemoveTextRun(aTextRun)) {
      break;
    }
    aFrame = aFrame->GetNextContinuation();
  }

  MOZ_ASSERT(!found || aStartContinuation, "how did we find null?");
  return found;
}

/**
 * Kill all references to |aTextRun| starting at |aStartContinuation|.
 * It could be referenced by any of its owners, and all their in-flows.
 * If |aStartContinuation| is null then process all userdata frames
 * and their continuations.
 * @note the caller is expected to take care of possibly destroying the
 * text run if all userdata frames were reset (userdata is deallocated
 * by this function though). The caller can detect this has occured by
 * checking |aTextRun->GetUserData() == nullptr|.
 */
static void UnhookTextRunFromFrames(gfxTextRun* aTextRun,
                                    nsTextFrame* aStartContinuation) {
  if (!aTextRun->GetUserData()) {
    return;
  }

  if (aTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsSimpleFlow) {
    nsTextFrame* userDataFrame = GetFrameForSimpleFlow(aTextRun);
    nsFrameState whichTextRunState =
        userDataFrame->GetTextRun(nsTextFrame::eInflated) == aTextRun
            ? TEXT_IN_TEXTRUN_USER_DATA
            : TEXT_IN_UNINFLATED_TEXTRUN_USER_DATA;
    DebugOnly<bool> found = ClearAllTextRunReferences(
        userDataFrame, aTextRun, aStartContinuation, whichTextRunState);
    NS_ASSERTION(!aStartContinuation || found,
                 "aStartContinuation wasn't found in simple flow text run");
    if (!userDataFrame->HasAnyStateBits(whichTextRunState)) {
      DestroyTextRunUserData(aTextRun);
    }
  } else {
    auto userData = static_cast<TextRunUserData*>(aTextRun->GetUserData());
    TextRunMappedFlow* userMappedFlows = GetMappedFlows(aTextRun);
    int32_t destroyFromIndex = aStartContinuation ? -1 : 0;
    for (uint32_t i = 0; i < userData->mMappedFlowCount; ++i) {
      nsTextFrame* userDataFrame = userMappedFlows[i].mStartFrame;
      nsFrameState whichTextRunState =
          userDataFrame->GetTextRun(nsTextFrame::eInflated) == aTextRun
              ? TEXT_IN_TEXTRUN_USER_DATA
              : TEXT_IN_UNINFLATED_TEXTRUN_USER_DATA;
      bool found = ClearAllTextRunReferences(
          userDataFrame, aTextRun, aStartContinuation, whichTextRunState);
      if (found) {
        if (userDataFrame->HasAnyStateBits(whichTextRunState)) {
          destroyFromIndex = i + 1;
        } else {
          destroyFromIndex = i;
        }
        aStartContinuation = nullptr;
      }
    }
    NS_ASSERTION(destroyFromIndex >= 0,
                 "aStartContinuation wasn't found in multi flow text run");
    if (destroyFromIndex == 0) {
      DestroyTextRunUserData(aTextRun);
    } else {
      userData->mMappedFlowCount = uint32_t(destroyFromIndex);
      if (userData->mLastFlowIndex >= uint32_t(destroyFromIndex)) {
        userData->mLastFlowIndex = uint32_t(destroyFromIndex) - 1;
      }
    }
  }
}

static void InvalidateFrameDueToGlyphsChanged(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame);

  PresShell* presShell = aFrame->PresShell();
  for (nsIFrame* f = aFrame; f;
       f = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(f)) {
    f->InvalidateFrame();

    // If this is a non-display text frame within SVG <text>, we need
    // to reflow the SVGTextFrame. (This is similar to reflowing the
    // SVGTextFrame in response to style changes, in
    // SVGTextFrame::DidSetComputedStyle.)
    if (f->IsInSVGTextSubtree() && f->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
      auto* svgTextFrame = static_cast<SVGTextFrame*>(
          nsLayoutUtils::GetClosestFrameOfType(f, LayoutFrameType::SVGText));
      svgTextFrame->ScheduleReflowSVGNonDisplayText(IntrinsicDirty::None);
    } else {
      // Theoretically we could just update overflow areas, perhaps using
      // OverflowChangedTracker, but that would do a bunch of work eagerly that
      // we should probably do lazily here since there could be a lot
      // of text frames affected and we'd like to coalesce the work. So that's
      // not easy to do well.
      presShell->FrameNeedsReflow(f, IntrinsicDirty::None, NS_FRAME_IS_DIRTY);
    }
  }
}

void GlyphObserver::NotifyGlyphsChanged() {
  if (mTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsSimpleFlow) {
    InvalidateFrameDueToGlyphsChanged(GetFrameForSimpleFlow(mTextRun));
    return;
  }

  auto data = static_cast<TextRunUserData*>(mTextRun->GetUserData());
  TextRunMappedFlow* userMappedFlows = GetMappedFlows(mTextRun);
  for (uint32_t i = 0; i < data->mMappedFlowCount; ++i) {
    InvalidateFrameDueToGlyphsChanged(userMappedFlows[i].mStartFrame);
  }
}

int32_t nsTextFrame::GetContentEnd() const {
  nsTextFrame* next = GetNextContinuation();
  // In case of allocation failure when setting/modifying the textfragment,
  // it's possible our text might be missing. So we check the fragment length,
  // in addition to the offset of the next continuation (if any).
  int32_t fragLen = TextFragment()->GetLength();
  return next ? std::min(fragLen, next->GetContentOffset()) : fragLen;
}

struct FlowLengthProperty {
  int32_t mStartOffset;
  // The offset of the next fixed continuation after mStartOffset, or
  // of the end of the text if there is none
  int32_t mEndFlowOffset;
};

int32_t nsTextFrame::GetInFlowContentLength() {
  if (!HasAnyStateBits(NS_FRAME_IS_BIDI)) {
    return mContent->TextLength() - mContentOffset;
  }

  FlowLengthProperty* flowLength =
      mContent->HasFlag(NS_HAS_FLOWLENGTH_PROPERTY)
          ? static_cast<FlowLengthProperty*>(
                mContent->GetProperty(nsGkAtoms::flowlength))
          : nullptr;
  MOZ_ASSERT(mContent->HasFlag(NS_HAS_FLOWLENGTH_PROPERTY) == !!flowLength,
             "incorrect NS_HAS_FLOWLENGTH_PROPERTY flag");
  /**
   * This frame must start inside the cached flow. If the flow starts at
   * mContentOffset but this frame is empty, logically it might be before the
   * start of the cached flow.
   */
  if (flowLength &&
      (flowLength->mStartOffset < mContentOffset ||
       (flowLength->mStartOffset == mContentOffset &&
        GetContentEnd() > mContentOffset)) &&
      flowLength->mEndFlowOffset > mContentOffset) {
#ifdef DEBUG
    NS_ASSERTION(flowLength->mEndFlowOffset >= GetContentEnd(),
                 "frame crosses fixed continuation boundary");
#endif
    return flowLength->mEndFlowOffset - mContentOffset;
  }

  nsTextFrame* nextBidi = LastInFlow()->GetNextContinuation();
  int32_t endFlow =
      nextBidi ? nextBidi->GetContentOffset() : GetContent()->TextLength();

  if (!flowLength) {
    flowLength = new FlowLengthProperty;
    if (NS_FAILED(mContent->SetProperty(
            nsGkAtoms::flowlength, flowLength,
            nsINode::DeleteProperty<FlowLengthProperty>))) {
      delete flowLength;
      flowLength = nullptr;
    } else {
      mContent->SetFlags(NS_HAS_FLOWLENGTH_PROPERTY);
    }
  }
  if (flowLength) {
    flowLength->mStartOffset = mContentOffset;
    flowLength->mEndFlowOffset = endFlow;
  }

  return endFlow - mContentOffset;
}

// Smarter versions of dom::IsSpaceCharacter.
// Unicode is really annoying; sometimes a space character isn't whitespace ---
// when it combines with another character
// So we have several versions of IsSpace for use in different contexts.

static bool IsSpaceCombiningSequenceTail(const nsTextFragment* aFrag,
                                         uint32_t aPos) {
  NS_ASSERTION(aPos <= aFrag->GetLength(), "Bad offset");
  if (!aFrag->Is2b()) {
    return false;
  }
  return nsTextFrameUtils::IsSpaceCombiningSequenceTail(
      aFrag->Get2b() + aPos, aFrag->GetLength() - aPos);
}

// Check whether aPos is a space for CSS 'word-spacing' purposes
static bool IsCSSWordSpacingSpace(const nsTextFragment* aFrag, uint32_t aPos,
                                  const nsTextFrame* aFrame,
                                  const nsStyleText* aStyleText) {
  NS_ASSERTION(aPos < aFrag->GetLength(), "No text for IsSpace!");

  char16_t ch = aFrag->CharAt(aPos);
  switch (ch) {
    case ' ':
    case CH_NBSP:
      return !IsSpaceCombiningSequenceTail(aFrag, aPos + 1);
    case '\r':
    case '\t':
      return !aStyleText->WhiteSpaceIsSignificant();
    case '\n':
      return !aStyleText->NewlineIsSignificant(aFrame);
    default:
      return false;
  }
}

constexpr char16_t kOghamSpaceMark = 0x1680;

// Check whether the string aChars/aLength starts with space that's
// trimmable according to CSS 'white-space:normal/nowrap'.
static bool IsTrimmableSpace(const char16_t* aChars, uint32_t aLength) {
  NS_ASSERTION(aLength > 0, "No text for IsSpace!");

  char16_t ch = *aChars;
  if (ch == ' ' || ch == kOghamSpaceMark) {
    return !nsTextFrameUtils::IsSpaceCombiningSequenceTail(aChars + 1,
                                                           aLength - 1);
  }
  return ch == '\t' || ch == '\f' || ch == '\n' || ch == '\r';
}

// Check whether the character aCh is trimmable according to CSS
// 'white-space:normal/nowrap'
static bool IsTrimmableSpace(char aCh) {
  return aCh == ' ' || aCh == '\t' || aCh == '\f' || aCh == '\n' || aCh == '\r';
}

static bool IsTrimmableSpace(const nsTextFragment* aFrag, uint32_t aPos,
                             const nsStyleText* aStyleText,
                             bool aAllowHangingWS = false) {
  NS_ASSERTION(aPos < aFrag->GetLength(), "No text for IsSpace!");

  switch (aFrag->CharAt(aPos)) {
    case ' ':
    case kOghamSpaceMark:
      return (!aStyleText->WhiteSpaceIsSignificant() || aAllowHangingWS) &&
             !IsSpaceCombiningSequenceTail(aFrag, aPos + 1);
    case '\n':
      return !aStyleText->NewlineIsSignificantStyle() &&
             aStyleText->mWhiteSpaceCollapse !=
                 StyleWhiteSpaceCollapse::PreserveSpaces;
    case '\t':
    case '\r':
    case '\f':
      return !aStyleText->WhiteSpaceIsSignificant() || aAllowHangingWS;
    default:
      return false;
  }
}

static bool IsSelectionInlineWhitespace(const nsTextFragment* aFrag,
                                        uint32_t aPos) {
  NS_ASSERTION(aPos < aFrag->GetLength(),
               "No text for IsSelectionInlineWhitespace!");
  char16_t ch = aFrag->CharAt(aPos);
  if (ch == ' ' || ch == CH_NBSP) {
    return !IsSpaceCombiningSequenceTail(aFrag, aPos + 1);
  }
  return ch == '\t' || ch == '\f';
}

static bool IsSelectionNewline(const nsTextFragment* aFrag, uint32_t aPos) {
  NS_ASSERTION(aPos < aFrag->GetLength(), "No text for IsSelectionNewline!");
  char16_t ch = aFrag->CharAt(aPos);
  return ch == '\n' || ch == '\r';
}

// Count the amount of trimmable whitespace (as per CSS
// 'white-space:normal/nowrap') in a text fragment. The first
// character is at offset aStartOffset; the maximum number of characters
// to check is aLength. aDirection is -1 or 1 depending on whether we should
// progress backwards or forwards.
static uint32_t GetTrimmableWhitespaceCount(const nsTextFragment* aFrag,
                                            int32_t aStartOffset,
                                            int32_t aLength,
                                            int32_t aDirection) {
  if (!aLength) {
    return 0;
  }

  int32_t count = 0;
  if (aFrag->Is2b()) {
    const char16_t* str = aFrag->Get2b() + aStartOffset;
    int32_t fragLen = aFrag->GetLength() - aStartOffset;
    for (; count < aLength; ++count) {
      if (!IsTrimmableSpace(str, fragLen)) {
        break;
      }
      str += aDirection;
      fragLen -= aDirection;
    }
  } else {
    const char* str = aFrag->Get1b() + aStartOffset;
    for (; count < aLength; ++count) {
      if (!IsTrimmableSpace(*str)) {
        break;
      }
      str += aDirection;
    }
  }
  return count;
}

static bool IsAllWhitespace(const nsTextFragment* aFrag, bool aAllowNewline) {
  if (aFrag->Is2b()) {
    return false;
  }
  int32_t len = aFrag->GetLength();
  const char* str = aFrag->Get1b();
  for (int32_t i = 0; i < len; ++i) {
    char ch = str[i];
    if (ch == ' ' || ch == '\t' || ch == '\r' ||
        (ch == '\n' && aAllowNewline)) {
      continue;
    }
    return false;
  }
  return true;
}

static void ClearObserversFromTextRun(gfxTextRun* aTextRun) {
  if (!(aTextRun->GetFlags2() &
        nsTextFrameUtils::Flags::MightHaveGlyphChanges)) {
    return;
  }

  if (aTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsSimpleFlow) {
    static_cast<SimpleTextRunUserData*>(aTextRun->GetUserData())
        ->mGlyphObservers.Clear();
  } else {
    static_cast<ComplexTextRunUserData*>(aTextRun->GetUserData())
        ->mGlyphObservers.Clear();
  }
}

static void CreateObserversForAnimatedGlyphs(gfxTextRun* aTextRun) {
  if (!aTextRun->GetUserData()) {
    return;
  }

  ClearObserversFromTextRun(aTextRun);

  nsTArray<gfxFont*> fontsWithAnimatedGlyphs;
  uint32_t numGlyphRuns;
  const gfxTextRun::GlyphRun* glyphRuns = aTextRun->GetGlyphRuns(&numGlyphRuns);
  for (uint32_t i = 0; i < numGlyphRuns; ++i) {
    gfxFont* font = glyphRuns[i].mFont;
    if (font->GlyphsMayChange() && !fontsWithAnimatedGlyphs.Contains(font)) {
      fontsWithAnimatedGlyphs.AppendElement(font);
    }
  }
  if (fontsWithAnimatedGlyphs.IsEmpty()) {
    // NB: Theoretically, we should clear the MightHaveGlyphChanges
    // here. That would involve de-allocating the simple user data struct if
    // present too, and resetting the pointer to the frame. In practice, I
    // don't think worth doing that work here, given the flag's only purpose is
    // to distinguish what kind of user data is there.
    return;
  }

  nsTArray<UniquePtr<GlyphObserver>>* observers;

  if (aTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsSimpleFlow) {
    // Swap the frame pointer for a just-allocated SimpleTextRunUserData if
    // appropriate.
    if (!(aTextRun->GetFlags2() &
          nsTextFrameUtils::Flags::MightHaveGlyphChanges)) {
      auto frame = static_cast<nsTextFrame*>(aTextRun->GetUserData());
      aTextRun->SetUserData(new SimpleTextRunUserData(frame));
    }

    auto data = static_cast<SimpleTextRunUserData*>(aTextRun->GetUserData());
    observers = &data->mGlyphObservers;
  } else {
    if (!(aTextRun->GetFlags2() &
          nsTextFrameUtils::Flags::MightHaveGlyphChanges)) {
      auto oldData = static_cast<TextRunUserData*>(aTextRun->GetUserData());
      TextRunMappedFlow* oldMappedFlows = GetMappedFlows(aTextRun);
      ComplexTextRunUserData* data =
          CreateComplexUserData(oldData->mMappedFlowCount);
      TextRunMappedFlow* dataMappedFlows =
          reinterpret_cast<TextRunMappedFlow*>(data + 1);
      data->mLastFlowIndex = oldData->mLastFlowIndex;
      for (uint32_t i = 0; i < oldData->mMappedFlowCount; ++i) {
        dataMappedFlows[i] = oldMappedFlows[i];
      }
      DestroyUserData(oldData);
      aTextRun->SetUserData(data);
    }
    auto data = static_cast<ComplexTextRunUserData*>(aTextRun->GetUserData());
    observers = &data->mGlyphObservers;
  }

  aTextRun->SetFlagBits(nsTextFrameUtils::Flags::MightHaveGlyphChanges);

  observers->SetCapacity(observers->Length() +
                         fontsWithAnimatedGlyphs.Length());
  for (auto font : fontsWithAnimatedGlyphs) {
    observers->AppendElement(MakeUnique<GlyphObserver>(font, aTextRun));
  }
}

/**
 * This class accumulates state as we scan a paragraph of text. It detects
 * textrun boundaries (changes from text to non-text, hard
 * line breaks, and font changes) and builds a gfxTextRun at each boundary.
 * It also detects linebreaker run boundaries (changes from text to non-text,
 * and hard line breaks) and at each boundary runs the linebreaker to compute
 * potential line breaks. It also records actual line breaks to store them in
 * the textruns.
 */
class BuildTextRunsScanner {
 public:
  BuildTextRunsScanner(nsPresContext* aPresContext, DrawTarget* aDrawTarget,
                       nsIFrame* aLineContainer,
                       nsTextFrame::TextRunType aWhichTextRun,
                       bool aDoLineBreaking)
      : mDrawTarget(aDrawTarget),
        mLineContainer(aLineContainer),
        mCommonAncestorWithLastFrame(nullptr),
        mMissingFonts(aPresContext->MissingFontRecorder()),
        mBidiEnabled(aPresContext->BidiEnabled()),
        mStartOfLine(true),
        mSkipIncompleteTextRuns(false),
        mCanStopOnThisLine(false),
        mDoLineBreaking(aDoLineBreaking),
        mWhichTextRun(aWhichTextRun),
        mNextRunContextInfo(nsTextFrameUtils::INCOMING_NONE),
        mCurrentRunContextInfo(nsTextFrameUtils::INCOMING_NONE) {
    ResetRunInfo();
  }
  ~BuildTextRunsScanner() {
    NS_ASSERTION(mBreakSinks.IsEmpty(), "Should have been cleared");
    NS_ASSERTION(mLineBreakBeforeFrames.IsEmpty(), "Should have been cleared");
    NS_ASSERTION(mMappedFlows.IsEmpty(), "Should have been cleared");
  }

  void SetAtStartOfLine() {
    mStartOfLine = true;
    mCanStopOnThisLine = false;
  }
  void SetSkipIncompleteTextRuns(bool aSkip) {
    mSkipIncompleteTextRuns = aSkip;
  }
  void SetCommonAncestorWithLastFrame(nsIFrame* aFrame) {
    mCommonAncestorWithLastFrame = aFrame;
  }
  bool CanStopOnThisLine() { return mCanStopOnThisLine; }
  nsIFrame* GetCommonAncestorWithLastFrame() {
    return mCommonAncestorWithLastFrame;
  }
  void LiftCommonAncestorWithLastFrameToParent(nsIFrame* aFrame) {
    if (mCommonAncestorWithLastFrame &&
        mCommonAncestorWithLastFrame->GetParent() == aFrame) {
      mCommonAncestorWithLastFrame = aFrame;
    }
  }
  void ScanFrame(nsIFrame* aFrame);
  bool IsTextRunValidForMappedFlows(const gfxTextRun* aTextRun);
  void FlushFrames(bool aFlushLineBreaks, bool aSuppressTrailingBreak);
  void FlushLineBreaks(gfxTextRun* aTrailingTextRun);
  void ResetRunInfo() {
    mLastFrame = nullptr;
    mMappedFlows.Clear();
    mLineBreakBeforeFrames.Clear();
    mMaxTextLength = 0;
    mDoubleByteText = false;
  }
  void AccumulateRunInfo(nsTextFrame* aFrame);
  /**
   * @return null to indicate either textrun construction failed or
   * we constructed just a partial textrun to set up linebreaker and other
   * state for following textruns.
   */
  already_AddRefed<gfxTextRun> BuildTextRunForFrames(void* aTextBuffer);
  bool SetupLineBreakerContext(gfxTextRun* aTextRun);
  void AssignTextRun(gfxTextRun* aTextRun, float aInflation);
  nsTextFrame* GetNextBreakBeforeFrame(uint32_t* aIndex);
  void SetupBreakSinksForTextRun(gfxTextRun* aTextRun, const void* aTextPtr);
  void SetupTextEmphasisForTextRun(gfxTextRun* aTextRun, const void* aTextPtr);
  struct FindBoundaryState {
    nsIFrame* mStopAtFrame;
    nsTextFrame* mFirstTextFrame;
    nsTextFrame* mLastTextFrame;
    bool mSeenTextRunBoundaryOnLaterLine;
    bool mSeenTextRunBoundaryOnThisLine;
    bool mSeenSpaceForLineBreakingOnThisLine;
    nsTArray<char16_t>& mBuffer;
  };
  enum FindBoundaryResult {
    FB_CONTINUE,
    FB_STOPPED_AT_STOP_FRAME,
    FB_FOUND_VALID_TEXTRUN_BOUNDARY
  };
  FindBoundaryResult FindBoundaries(nsIFrame* aFrame,
                                    FindBoundaryState* aState);

  bool ContinueTextRunAcrossFrames(nsTextFrame* aFrame1, nsTextFrame* aFrame2);

  // Like TextRunMappedFlow but with some differences. mStartFrame to mEndFrame
  // (exclusive) are a sequence of in-flow frames (if mEndFrame is null, then
  // continuations starting from mStartFrame are a sequence of in-flow frames).
  struct MappedFlow {
    nsTextFrame* mStartFrame;
    nsTextFrame* mEndFrame;
    // When we consider breaking between elements, the nearest common
    // ancestor of the elements containing the characters is the one whose
    // CSS 'white-space' property governs. So this records the nearest common
    // ancestor of mStartFrame and the previous text frame, or null if there
    // was no previous text frame on this line.
    nsIFrame* mAncestorControllingInitialBreak;

    int32_t GetContentEnd() const {
      int32_t fragLen = mStartFrame->TextFragment()->GetLength();
      return mEndFrame ? std::min(fragLen, mEndFrame->GetContentOffset())
                       : fragLen;
    }
  };

  class BreakSink final : public nsILineBreakSink {
   public:
    BreakSink(gfxTextRun* aTextRun, DrawTarget* aDrawTarget,
              uint32_t aOffsetIntoTextRun)
        : mTextRun(aTextRun),
          mDrawTarget(aDrawTarget),
          mOffsetIntoTextRun(aOffsetIntoTextRun) {}

    void SetBreaks(uint32_t aOffset, uint32_t aLength,
                   uint8_t* aBreakBefore) final {
      gfxTextRun::Range range(aOffset + mOffsetIntoTextRun,
                              aOffset + mOffsetIntoTextRun + aLength);
      if (mTextRun->SetPotentialLineBreaks(range, aBreakBefore)) {
        // Be conservative and assume that some breaks have been set
        mTextRun->ClearFlagBits(nsTextFrameUtils::Flags::NoBreaks);
      }
    }

    void SetCapitalization(uint32_t aOffset, uint32_t aLength,
                           bool* aCapitalize) final {
      MOZ_ASSERT(mTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsTransformed,
                 "Text run should be transformed!");
      if (mTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsTransformed) {
        nsTransformedTextRun* transformedTextRun =
            static_cast<nsTransformedTextRun*>(mTextRun.get());
        transformedTextRun->SetCapitalization(aOffset + mOffsetIntoTextRun,
                                              aLength, aCapitalize);
      }
    }

    void Finish(gfxMissingFontRecorder* aMFR) {
      if (mTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsTransformed) {
        nsTransformedTextRun* transformedTextRun =
            static_cast<nsTransformedTextRun*>(mTextRun.get());
        transformedTextRun->FinishSettingProperties(mDrawTarget, aMFR);
      }
      // The way nsTransformedTextRun is implemented, its glyph runs aren't
      // available until after nsTransformedTextRun::FinishSettingProperties()
      // is called. So that's why we defer checking for animated glyphs to here.
      CreateObserversForAnimatedGlyphs(mTextRun);
    }

    RefPtr<gfxTextRun> mTextRun;
    DrawTarget* mDrawTarget;
    uint32_t mOffsetIntoTextRun;
  };

 private:
  AutoTArray<MappedFlow, 10> mMappedFlows;
  AutoTArray<nsTextFrame*, 50> mLineBreakBeforeFrames;
  AutoTArray<UniquePtr<BreakSink>, 10> mBreakSinks;
  nsLineBreaker mLineBreaker;
  RefPtr<gfxTextRun> mCurrentFramesAllSameTextRun;
  DrawTarget* mDrawTarget;
  nsIFrame* mLineContainer;
  nsTextFrame* mLastFrame;
  // The common ancestor of the current frame and the previous leaf frame
  // on the line, or null if there was no previous leaf frame.
  nsIFrame* mCommonAncestorWithLastFrame;
  gfxMissingFontRecorder* mMissingFonts;
  // mMaxTextLength is an upper bound on the size of the text in all mapped
  // frames The value UINT32_MAX represents overflow; text will be discarded
  uint32_t mMaxTextLength;
  bool mDoubleByteText;
  bool mBidiEnabled;
  bool mStartOfLine;
  bool mSkipIncompleteTextRuns;
  bool mCanStopOnThisLine;
  bool mDoLineBreaking;
  nsTextFrame::TextRunType mWhichTextRun;
  uint8_t mNextRunContextInfo;
  uint8_t mCurrentRunContextInfo;
};

static const nsIFrame* FindLineContainer(const nsIFrame* aFrame) {
  while (aFrame &&
         (aFrame->IsLineParticipant() || aFrame->CanContinueTextRun())) {
    aFrame = aFrame->GetParent();
  }
  return aFrame;
}

static nsIFrame* FindLineContainer(nsIFrame* aFrame) {
  return const_cast<nsIFrame*>(
      FindLineContainer(const_cast<const nsIFrame*>(aFrame)));
}

static bool IsLineBreakingWhiteSpace(char16_t aChar) {
  // 0x0A (\n) is not handled as white-space by the line breaker, since
  // we break before it, if it isn't transformed to a normal space.
  // (If we treat it as normal white-space then we'd only break after it.)
  // However, it does induce a line break or is converted to a regular
  // space, and either way it can be used to bound the region of text
  // that needs to be analyzed for line breaking.
  return nsLineBreaker::IsSpace(aChar) || aChar == 0x0A;
}

static bool TextContainsLineBreakerWhiteSpace(const void* aText,
                                              uint32_t aLength,
                                              bool aIsDoubleByte) {
  if (aIsDoubleByte) {
    const char16_t* chars = static_cast<const char16_t*>(aText);
    for (uint32_t i = 0; i < aLength; ++i) {
      if (IsLineBreakingWhiteSpace(chars[i])) {
        return true;
      }
    }
    return false;
  } else {
    const uint8_t* chars = static_cast<const uint8_t*>(aText);
    for (uint32_t i = 0; i < aLength; ++i) {
      if (IsLineBreakingWhiteSpace(chars[i])) {
        return true;
      }
    }
    return false;
  }
}

static nsTextFrameUtils::CompressionMode GetCSSWhitespaceToCompressionMode(
    nsTextFrame* aFrame, const nsStyleText* aStyleText) {
  switch (aStyleText->mWhiteSpaceCollapse) {
    case StyleWhiteSpaceCollapse::Collapse:
      return nsTextFrameUtils::COMPRESS_WHITESPACE_NEWLINE;
    case StyleWhiteSpaceCollapse::PreserveBreaks:
      return nsTextFrameUtils::COMPRESS_WHITESPACE;
    case StyleWhiteSpaceCollapse::Preserve:
    case StyleWhiteSpaceCollapse::PreserveSpaces:
    case StyleWhiteSpaceCollapse::BreakSpaces:
      if (!aStyleText->NewlineIsSignificant(aFrame)) {
        // If newline is set to be preserved, but then suppressed,
        // transform newline to space.
        return nsTextFrameUtils::COMPRESS_NONE_TRANSFORM_TO_SPACE;
      }
      return nsTextFrameUtils::COMPRESS_NONE;
  }
  MOZ_ASSERT_UNREACHABLE("Unknown white-space-collapse value");
  return nsTextFrameUtils::COMPRESS_WHITESPACE_NEWLINE;
}

struct FrameTextTraversal {
  FrameTextTraversal()
      : mFrameToScan(nullptr),
        mOverflowFrameToScan(nullptr),
        mScanSiblings(false),
        mLineBreakerCanCrossFrameBoundary(false),
        mTextRunCanCrossFrameBoundary(false) {}

  // These fields identify which frames should be recursively scanned
  // The first normal frame to scan (or null, if no such frame should be
  // scanned)
  nsIFrame* mFrameToScan;
  // The first overflow frame to scan (or null, if no such frame should be
  // scanned)
  nsIFrame* mOverflowFrameToScan;
  // Whether to scan the siblings of
  // mFrameToDescendInto/mOverflowFrameToDescendInto
  bool mScanSiblings;

  // These identify the boundaries of the context required for
  // line breaking or textrun construction
  bool mLineBreakerCanCrossFrameBoundary;
  bool mTextRunCanCrossFrameBoundary;

  nsIFrame* NextFrameToScan() {
    nsIFrame* f;
    if (mFrameToScan) {
      f = mFrameToScan;
      mFrameToScan = mScanSiblings ? f->GetNextSibling() : nullptr;
    } else if (mOverflowFrameToScan) {
      f = mOverflowFrameToScan;
      mOverflowFrameToScan = mScanSiblings ? f->GetNextSibling() : nullptr;
    } else {
      f = nullptr;
    }
    return f;
  }
};

static FrameTextTraversal CanTextCrossFrameBoundary(nsIFrame* aFrame) {
  FrameTextTraversal result;

  bool continuesTextRun = aFrame->CanContinueTextRun();
  if (aFrame->IsPlaceholderFrame()) {
    // placeholders are "invisible", so a text run should be able to span
    // across one. But don't descend into the out-of-flow.
    result.mLineBreakerCanCrossFrameBoundary = true;
    if (continuesTextRun) {
      // ... Except for first-letter floats, which are really in-flow
      // from the point of view of capitalization etc, so we'd better
      // descend into them. But we actually need to break the textrun for
      // first-letter floats since things look bad if, say, we try to make a
      // ligature across the float boundary.
      result.mFrameToScan =
          (static_cast<nsPlaceholderFrame*>(aFrame))->GetOutOfFlowFrame();
    } else {
      result.mTextRunCanCrossFrameBoundary = true;
    }
  } else {
    if (continuesTextRun) {
      result.mFrameToScan = aFrame->PrincipalChildList().FirstChild();
      result.mOverflowFrameToScan =
          aFrame->GetChildList(FrameChildListID::Overflow).FirstChild();
      NS_WARNING_ASSERTION(
          !result.mOverflowFrameToScan,
          "Scanning overflow inline frames is something we should avoid");
      result.mScanSiblings = true;
      result.mTextRunCanCrossFrameBoundary = true;
      result.mLineBreakerCanCrossFrameBoundary = true;
    } else {
      MOZ_ASSERT(!aFrame->IsRubyTextContainerFrame(),
                 "Shouldn't call this method for ruby text container");
    }
  }
  return result;
}

BuildTextRunsScanner::FindBoundaryResult BuildTextRunsScanner::FindBoundaries(
    nsIFrame* aFrame, FindBoundaryState* aState) {
  LayoutFrameType frameType = aFrame->Type();
  if (frameType == LayoutFrameType::RubyTextContainer) {
    // Don't stop a text run for ruby text container. We want ruby text
    // containers to be skipped, but continue the text run across them.
    return FB_CONTINUE;
  }

  nsTextFrame* textFrame = frameType == LayoutFrameType::Text
                               ? static_cast<nsTextFrame*>(aFrame)
                               : nullptr;
  if (textFrame) {
    if (aState->mLastTextFrame &&
        textFrame != aState->mLastTextFrame->GetNextInFlow() &&
        !ContinueTextRunAcrossFrames(aState->mLastTextFrame, textFrame)) {
      aState->mSeenTextRunBoundaryOnThisLine = true;
      if (aState->mSeenSpaceForLineBreakingOnThisLine) {
        return FB_FOUND_VALID_TEXTRUN_BOUNDARY;
      }
    }
    if (!aState->mFirstTextFrame) {
      aState->mFirstTextFrame = textFrame;
    }
    aState->mLastTextFrame = textFrame;
  }

  if (aFrame == aState->mStopAtFrame) {
    return FB_STOPPED_AT_STOP_FRAME;
  }

  if (textFrame) {
    if (aState->mSeenSpaceForLineBreakingOnThisLine) {
      return FB_CONTINUE;
    }
    const nsTextFragment* frag = textFrame->TextFragment();
    uint32_t start = textFrame->GetContentOffset();
    uint32_t length = textFrame->GetContentLength();
    const void* text;
    const nsAtom* language = textFrame->StyleFont()->mLanguage;
    if (frag->Is2b()) {
      // It is possible that we may end up removing all whitespace in
      // a piece of text because of The White Space Processing Rules,
      // so we need to transform it before we can check existence of
      // such whitespaces.
      aState->mBuffer.EnsureLengthAtLeast(length);
      nsTextFrameUtils::CompressionMode compression =
          GetCSSWhitespaceToCompressionMode(textFrame, textFrame->StyleText());
      uint8_t incomingFlags = 0;
      gfxSkipChars skipChars;
      nsTextFrameUtils::Flags analysisFlags;
      char16_t* bufStart = aState->mBuffer.Elements();
      char16_t* bufEnd = nsTextFrameUtils::TransformText(
          frag->Get2b() + start, length, bufStart, compression, &incomingFlags,
          &skipChars, &analysisFlags, language);
      text = bufStart;
      length = bufEnd - bufStart;
    } else {
      // If the text only contains ASCII characters, it is currently
      // impossible that TransformText would remove all whitespaces,
      // and thus the check below should return the same result for
      // transformed text and original text. So we don't need to try
      // transforming it here.
      text = static_cast<const void*>(frag->Get1b() + start);
    }
    if (TextContainsLineBreakerWhiteSpace(text, length, frag->Is2b())) {
      aState->mSeenSpaceForLineBreakingOnThisLine = true;
      if (aState->mSeenTextRunBoundaryOnLaterLine) {
        return FB_FOUND_VALID_TEXTRUN_BOUNDARY;
      }
    }
    return FB_CONTINUE;
  }

  FrameTextTraversal traversal = CanTextCrossFrameBoundary(aFrame);
  if (!traversal.mTextRunCanCrossFrameBoundary) {
    aState->mSeenTextRunBoundaryOnThisLine = true;
    if (aState->mSeenSpaceForLineBreakingOnThisLine) {
      return FB_FOUND_VALID_TEXTRUN_BOUNDARY;
    }
  }

  for (nsIFrame* f = traversal.NextFrameToScan(); f;
       f = traversal.NextFrameToScan()) {
    FindBoundaryResult result = FindBoundaries(f, aState);
    if (result != FB_CONTINUE) {
      return result;
    }
  }

  if (!traversal.mTextRunCanCrossFrameBoundary) {
    aState->mSeenTextRunBoundaryOnThisLine = true;
    if (aState->mSeenSpaceForLineBreakingOnThisLine) {
      return FB_FOUND_VALID_TEXTRUN_BOUNDARY;
    }
  }

  return FB_CONTINUE;
}

// build text runs for the 200 lines following aForFrame, and stop after that
// when we get a chance.
#define NUM_LINES_TO_BUILD_TEXT_RUNS 200

/**
 * General routine for building text runs. This is hairy because of the need
 * to build text runs that span content nodes.
 *
 * @param aContext The gfxContext we're using to construct this text run.
 * @param aForFrame The nsTextFrame for which we're building this text run.
 * @param aLineContainer the line container containing aForFrame; if null,
 *        we'll walk the ancestors to find it.  It's required to be non-null
 *        when aForFrameLine is non-null.
 * @param aForFrameLine the line containing aForFrame; if null, we'll figure
 *        out the line (slowly)
 * @param aWhichTextRun The type of text run we want to build. If font inflation
 *        is enabled, this will be eInflated, otherwise it's eNotInflated.
 */
static void BuildTextRuns(DrawTarget* aDrawTarget, nsTextFrame* aForFrame,
                          nsIFrame* aLineContainer,
                          const nsLineList::iterator* aForFrameLine,
                          nsTextFrame::TextRunType aWhichTextRun) {
  MOZ_ASSERT(aForFrame, "for no frame?");
  NS_ASSERTION(!aForFrameLine || aLineContainer, "line but no line container");

  nsIFrame* lineContainerChild = aForFrame;
  if (!aLineContainer) {
    if (aForFrame->IsFloatingFirstLetterChild()) {
      lineContainerChild = aForFrame->GetParent()->GetPlaceholderFrame();
    }
    aLineContainer = FindLineContainer(lineContainerChild);
  } else {
    NS_ASSERTION(
        (aLineContainer == FindLineContainer(aForFrame) ||
         (aLineContainer->IsLetterFrame() && aLineContainer->IsFloating())),
        "Wrong line container hint");
  }

  if (aForFrame->HasAnyStateBits(TEXT_IS_IN_TOKEN_MATHML)) {
    aLineContainer->AddStateBits(TEXT_IS_IN_TOKEN_MATHML);
    if (aForFrame->HasAnyStateBits(NS_FRAME_IS_IN_SINGLE_CHAR_MI)) {
      aLineContainer->AddStateBits(NS_FRAME_IS_IN_SINGLE_CHAR_MI);
    }
  }
  if (aForFrame->HasAnyStateBits(NS_FRAME_MATHML_SCRIPT_DESCENDANT)) {
    aLineContainer->AddStateBits(NS_FRAME_MATHML_SCRIPT_DESCENDANT);
  }

  nsPresContext* presContext = aLineContainer->PresContext();
  bool doLineBreaking = !aForFrame->IsInSVGTextSubtree();
  BuildTextRunsScanner scanner(presContext, aDrawTarget, aLineContainer,
                               aWhichTextRun, doLineBreaking);

  nsBlockFrame* block = do_QueryFrame(aLineContainer);

  if (!block) {
    nsIFrame* textRunContainer = aLineContainer;
    if (aLineContainer->IsRubyTextContainerFrame()) {
      textRunContainer = aForFrame;
      while (textRunContainer && !textRunContainer->IsRubyTextFrame()) {
        textRunContainer = textRunContainer->GetParent();
      }
      MOZ_ASSERT(textRunContainer &&
                 textRunContainer->GetParent() == aLineContainer);
    } else {
      NS_ASSERTION(
          !aLineContainer->GetPrevInFlow() && !aLineContainer->GetNextInFlow(),
          "Breakable non-block line containers other than "
          "ruby text container is not supported");
    }
    // Just loop through all the children of the linecontainer ... it's really
    // just one line
    scanner.SetAtStartOfLine();
    scanner.SetCommonAncestorWithLastFrame(nullptr);
    for (nsIFrame* child : textRunContainer->PrincipalChildList()) {
      scanner.ScanFrame(child);
    }
    // Set mStartOfLine so FlushFrames knows its textrun ends a line
    scanner.SetAtStartOfLine();
    scanner.FlushFrames(true, false);
    return;
  }

  // Find the line containing 'lineContainerChild'.

  bool isValid = true;
  nsBlockInFlowLineIterator backIterator(block, &isValid);
  if (aForFrameLine) {
    backIterator = nsBlockInFlowLineIterator(block, *aForFrameLine);
  } else {
    backIterator =
        nsBlockInFlowLineIterator(block, lineContainerChild, &isValid);
    NS_ASSERTION(isValid, "aForFrame not found in block, someone lied to us");
    NS_ASSERTION(backIterator.GetContainer() == block,
                 "Someone lied to us about the block");
  }
  nsBlockFrame::LineIterator startLine = backIterator.GetLine();

  // Find a line where we can start building text runs. We choose the last line
  // where:
  // -- there is a textrun boundary between the start of the line and the
  // start of aForFrame
  // -- there is a space between the start of the line and the textrun boundary
  // (this is so we can be sure the line breaks will be set properly
  // on the textruns we construct).
  // The possibly-partial text runs up to and including the first space
  // are not reconstructed. We construct partial text runs for that text ---
  // for the sake of simplifying the code and feeding the linebreaker ---
  // but we discard them instead of assigning them to frames.
  // This is a little awkward because we traverse lines in the reverse direction
  // but we traverse the frames in each line in the forward direction.
  nsBlockInFlowLineIterator forwardIterator = backIterator;
  nsIFrame* stopAtFrame = lineContainerChild;
  nsTextFrame* nextLineFirstTextFrame = nullptr;
  AutoTArray<char16_t, BIG_TEXT_NODE_SIZE> buffer;
  bool seenTextRunBoundaryOnLaterLine = false;
  bool mayBeginInTextRun = true;
  while (true) {
    forwardIterator = backIterator;
    nsBlockFrame::LineIterator line = backIterator.GetLine();
    if (!backIterator.Prev() || backIterator.GetLine()->IsBlock()) {
      mayBeginInTextRun = false;
      break;
    }

    BuildTextRunsScanner::FindBoundaryState state = {
        stopAtFrame, nullptr, nullptr, bool(seenTextRunBoundaryOnLaterLine),
        false,       false,   buffer};
    nsIFrame* child = line->mFirstChild;
    bool foundBoundary = false;
    for (int32_t i = line->GetChildCount() - 1; i >= 0; --i) {
      BuildTextRunsScanner::FindBoundaryResult result =
          scanner.FindBoundaries(child, &state);
      if (result == BuildTextRunsScanner::FB_FOUND_VALID_TEXTRUN_BOUNDARY) {
        foundBoundary = true;
        break;
      } else if (result == BuildTextRunsScanner::FB_STOPPED_AT_STOP_FRAME) {
        break;
      }
      child = child->GetNextSibling();
    }
    if (foundBoundary) {
      break;
    }
    if (!stopAtFrame && state.mLastTextFrame && nextLineFirstTextFrame &&
        !scanner.ContinueTextRunAcrossFrames(state.mLastTextFrame,
                                             nextLineFirstTextFrame)) {
      // Found a usable textrun boundary at the end of the line
      if (state.mSeenSpaceForLineBreakingOnThisLine) {
        break;
      }
      seenTextRunBoundaryOnLaterLine = true;
    } else if (state.mSeenTextRunBoundaryOnThisLine) {
      seenTextRunBoundaryOnLaterLine = true;
    }
    stopAtFrame = nullptr;
    if (state.mFirstTextFrame) {
      nextLineFirstTextFrame = state.mFirstTextFrame;
    }
  }
  scanner.SetSkipIncompleteTextRuns(mayBeginInTextRun);

  // Now iterate over all text frames starting from the current line.
  // First-in-flow text frames will be accumulated into textRunFrames as we go.
  // When a text run boundary is required we flush textRunFrames ((re)building
  // their gfxTextRuns as necessary).
  bool seenStartLine = false;
  uint32_t linesAfterStartLine = 0;
  do {
    nsBlockFrame::LineIterator line = forwardIterator.GetLine();
    if (line->IsBlock()) {
      break;
    }
    line->SetInvalidateTextRuns(false);
    scanner.SetAtStartOfLine();
    scanner.SetCommonAncestorWithLastFrame(nullptr);
    nsIFrame* child = line->mFirstChild;
    for (int32_t i = line->GetChildCount() - 1; i >= 0; --i) {
      scanner.ScanFrame(child);
      child = child->GetNextSibling();
    }
    if (line.get() == startLine.get()) {
      seenStartLine = true;
    }
    if (seenStartLine) {
      ++linesAfterStartLine;
      if (linesAfterStartLine >= NUM_LINES_TO_BUILD_TEXT_RUNS &&
          scanner.CanStopOnThisLine()) {
        // Don't flush frames; we may be in the middle of a textrun
        // that we can't end here. That's OK, we just won't build it.
        // Note that we must already have finished the textrun for aForFrame,
        // because we've seen the end of a textrun in a line after the line
        // containing aForFrame.
        scanner.FlushLineBreaks(nullptr);
        // This flushes out mMappedFlows and mLineBreakBeforeFrames, which
        // silences assertions in the scanner destructor.
        scanner.ResetRunInfo();
        return;
      }
    }
  } while (forwardIterator.Next());

  // Set mStartOfLine so FlushFrames knows its textrun ends a line
  scanner.SetAtStartOfLine();
  scanner.FlushFrames(true, false);
}

static char16_t* ExpandBuffer(char16_t* aDest, uint8_t* aSrc, uint32_t aCount) {
  while (aCount) {
    *aDest = *aSrc;
    ++aDest;
    ++aSrc;
    --aCount;
  }
  return aDest;
}

bool BuildTextRunsScanner::IsTextRunValidForMappedFlows(
    const gfxTextRun* aTextRun) {
  if (aTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsSimpleFlow) {
    return mMappedFlows.Length() == 1 &&
           mMappedFlows[0].mStartFrame == GetFrameForSimpleFlow(aTextRun) &&
           mMappedFlows[0].mEndFrame == nullptr;
  }

  auto userData = static_cast<TextRunUserData*>(aTextRun->GetUserData());
  TextRunMappedFlow* userMappedFlows = GetMappedFlows(aTextRun);
  if (userData->mMappedFlowCount != mMappedFlows.Length()) {
    return false;
  }
  for (uint32_t i = 0; i < mMappedFlows.Length(); ++i) {
    if (userMappedFlows[i].mStartFrame != mMappedFlows[i].mStartFrame ||
        int32_t(userMappedFlows[i].mContentLength) !=
            mMappedFlows[i].GetContentEnd() -
                mMappedFlows[i].mStartFrame->GetContentOffset()) {
      return false;
    }
  }
  return true;
}

/**
 * This gets called when we need to make a text run for the current list of
 * frames.
 */
void BuildTextRunsScanner::FlushFrames(bool aFlushLineBreaks,
                                       bool aSuppressTrailingBreak) {
  RefPtr<gfxTextRun> textRun;
  if (!mMappedFlows.IsEmpty()) {
    if (!mSkipIncompleteTextRuns && mCurrentFramesAllSameTextRun &&
        !!(mCurrentFramesAllSameTextRun->GetFlags2() &
           nsTextFrameUtils::Flags::IncomingWhitespace) ==
            !!(mCurrentRunContextInfo &
               nsTextFrameUtils::INCOMING_WHITESPACE) &&
        !!(mCurrentFramesAllSameTextRun->GetFlags() &
           gfx::ShapedTextFlags::TEXT_INCOMING_ARABICCHAR) ==
            !!(mCurrentRunContextInfo &
               nsTextFrameUtils::INCOMING_ARABICCHAR) &&
        IsTextRunValidForMappedFlows(mCurrentFramesAllSameTextRun)) {
      // Optimization: We do not need to (re)build the textrun.
      textRun = mCurrentFramesAllSameTextRun;

      if (mDoLineBreaking) {
        // Feed this run's text into the linebreaker to provide context.
        if (!SetupLineBreakerContext(textRun)) {
          return;
        }
      }

      // Update mNextRunContextInfo appropriately
      mNextRunContextInfo = nsTextFrameUtils::INCOMING_NONE;
      if (textRun->GetFlags2() & nsTextFrameUtils::Flags::TrailingWhitespace) {
        mNextRunContextInfo |= nsTextFrameUtils::INCOMING_WHITESPACE;
      }
      if (textRun->GetFlags() &
          gfx::ShapedTextFlags::TEXT_TRAILING_ARABICCHAR) {
        mNextRunContextInfo |= nsTextFrameUtils::INCOMING_ARABICCHAR;
      }
    } else {
      AutoTArray<uint8_t, BIG_TEXT_NODE_SIZE> buffer;
      uint32_t bufferSize = mMaxTextLength * (mDoubleByteText ? 2 : 1);
      if (bufferSize < mMaxTextLength || bufferSize == UINT32_MAX ||
          !buffer.AppendElements(bufferSize, fallible)) {
        return;
      }
      textRun = BuildTextRunForFrames(buffer.Elements());
    }
  }

  if (aFlushLineBreaks) {
    FlushLineBreaks(aSuppressTrailingBreak ? nullptr : textRun.get());
    if (!mDoLineBreaking && textRun) {
      CreateObserversForAnimatedGlyphs(textRun.get());
    }
  }

  mCanStopOnThisLine = true;
  ResetRunInfo();
}

void BuildTextRunsScanner::FlushLineBreaks(gfxTextRun* aTrailingTextRun) {
  // If the line-breaker is buffering a potentially-unfinished word,
  // preserve the state of being in-word so that we don't spuriously
  // capitalize the next letter.
  bool inWord = mLineBreaker.InWord();
  bool trailingLineBreak;
  nsresult rv = mLineBreaker.Reset(&trailingLineBreak);
  mLineBreaker.SetWordContinuation(inWord);
  // textRun may be null for various reasons, including because we constructed
  // a partial textrun just to get the linebreaker and other state set up
  // to build the next textrun.
  if (NS_SUCCEEDED(rv) && trailingLineBreak && aTrailingTextRun) {
    aTrailingTextRun->SetFlagBits(nsTextFrameUtils::Flags::HasTrailingBreak);
  }

  for (uint32_t i = 0; i < mBreakSinks.Length(); ++i) {
    // TODO cause frames associated with the textrun to be reflowed, if they
    // aren't being reflowed already!
    mBreakSinks[i]->Finish(mMissingFonts);
  }
  mBreakSinks.Clear();
}

void BuildTextRunsScanner::AccumulateRunInfo(nsTextFrame* aFrame) {
  if (mMaxTextLength != UINT32_MAX) {
    NS_ASSERTION(mMaxTextLength < UINT32_MAX - aFrame->GetContentLength(),
                 "integer overflow");
    if (mMaxTextLength >= UINT32_MAX - aFrame->GetContentLength()) {
      mMaxTextLength = UINT32_MAX;
    } else {
      mMaxTextLength += aFrame->GetContentLength();
    }
  }
  mDoubleByteText |= aFrame->TextFragment()->Is2b();
  mLastFrame = aFrame;
  mCommonAncestorWithLastFrame = aFrame->GetParent();

  MappedFlow* mappedFlow = &mMappedFlows[mMappedFlows.Length() - 1];
  NS_ASSERTION(mappedFlow->mStartFrame == aFrame ||
                   mappedFlow->GetContentEnd() == aFrame->GetContentOffset(),
               "Overlapping or discontiguous frames => BAD");
  mappedFlow->mEndFrame = aFrame->GetNextContinuation();
  if (mCurrentFramesAllSameTextRun != aFrame->GetTextRun(mWhichTextRun)) {
    mCurrentFramesAllSameTextRun = nullptr;
  }

  if (mStartOfLine) {
    mLineBreakBeforeFrames.AppendElement(aFrame);
    mStartOfLine = false;
  }

  // Default limits used by `hyphenate-limit-chars` for `auto` components, as
  // suggested by the CSS Text spec.
  // TODO: consider making these sensitive to the context, e.g. increasing the
  // values for long line lengths to reduce the tendency to hyphenate too much.
  const uint32_t kDefaultHyphenateTotalWordLength = 5;
  const uint32_t kDefaultHyphenatePreBreakLength = 2;
  const uint32_t kDefaultHyphenatePostBreakLength = 2;

  const auto& hyphenateLimitChars = aFrame->StyleText()->mHyphenateLimitChars;
  uint32_t pre =
      hyphenateLimitChars.pre_hyphen_length.IsAuto()
          ? kDefaultHyphenatePreBreakLength
          : std::max(0, hyphenateLimitChars.pre_hyphen_length.AsNumber());
  uint32_t post =
      hyphenateLimitChars.post_hyphen_length.IsAuto()
          ? kDefaultHyphenatePostBreakLength
          : std::max(0, hyphenateLimitChars.post_hyphen_length.AsNumber());
  uint32_t total =
      hyphenateLimitChars.total_word_length.IsAuto()
          ? kDefaultHyphenateTotalWordLength
          : std::max(0, hyphenateLimitChars.total_word_length.AsNumber());
  total = std::max(total, pre + post);
  mLineBreaker.SetHyphenateLimitChars(total, pre, post);
}

static bool HasTerminalNewline(const nsTextFrame* aFrame) {
  if (aFrame->GetContentLength() == 0) {
    return false;
  }
  const nsTextFragment* frag = aFrame->TextFragment();
  return frag->CharAt(AssertedCast<uint32_t>(aFrame->GetContentEnd()) - 1) ==
         '\n';
}

static gfxFont::Metrics GetFirstFontMetrics(gfxFontGroup* aFontGroup,
                                            bool aVerticalMetrics) {
  if (!aFontGroup) {
    return gfxFont::Metrics();
  }
  RefPtr<gfxFont> font = aFontGroup->GetFirstValidFont();
  return font->GetMetrics(aVerticalMetrics ? nsFontMetrics::eVertical
                                           : nsFontMetrics::eHorizontal);
}

static nscoord GetSpaceWidthAppUnits(const gfxTextRun* aTextRun) {
  // Round the space width when converting to appunits the same way textruns
  // do.
  gfxFloat spaceWidthAppUnits =
      NS_round(GetFirstFontMetrics(aTextRun->GetFontGroup(),
                                   aTextRun->UseCenterBaseline())
                   .spaceWidth *
               aTextRun->GetAppUnitsPerDevUnit());

  return spaceWidthAppUnits;
}

static gfxFloat GetMinTabAdvanceAppUnits(const gfxTextRun* aTextRun) {
  gfxFloat chWidthAppUnits = NS_round(
      GetFirstFontMetrics(aTextRun->GetFontGroup(), aTextRun->IsVertical())
          .ZeroOrAveCharWidth() *
      aTextRun->GetAppUnitsPerDevUnit());
  return 0.5 * chWidthAppUnits;
}

static float GetSVGFontSizeScaleFactor(nsIFrame* aFrame) {
  if (!aFrame->IsInSVGTextSubtree()) {
    return 1.0f;
  }
  auto* container =
      nsLayoutUtils::GetClosestFrameOfType(aFrame, LayoutFrameType::SVGText);
  MOZ_ASSERT(container);
  return static_cast<SVGTextFrame*>(container)->GetFontSizeScaleFactor();
}

static nscoord LetterSpacing(nsIFrame* aFrame, const nsStyleText& aStyleText) {
  if (aFrame->IsInSVGTextSubtree()) {
    // SVG text can have a scaling factor applied so that very small or very
    // large font-sizes don't suffer from poor glyph placement due to app unit
    // rounding. The used letter-spacing value must be scaled by the same
    // factor. Unlike word-spacing (below), this applies to both lengths and
    // percentages, as the percentage basis is 1em, not an already-scaled glyph
    // dimension.
    return GetSVGFontSizeScaleFactor(aFrame) *
           aStyleText.mLetterSpacing.Resolve(
               [&] { return aFrame->StyleFont()->mSize.ToAppUnits(); });
  }

  return aStyleText.mLetterSpacing.Resolve(
      [&] { return aFrame->StyleFont()->mSize.ToAppUnits(); });
}

// This function converts non-coord values (e.g. percentages) to nscoord.
static nscoord WordSpacing(nsIFrame* aFrame, const gfxTextRun* aTextRun,
                           const nsStyleText& aStyleText) {
  if (aFrame->IsInSVGTextSubtree()) {
    // SVG text can have a scaling factor applied so that very small or very
    // large font-sizes don't suffer from poor glyph placement due to app unit
    // rounding. The used word-spacing value must be scaled by the same
    // factor, although any percentage basis has already effectively been
    // scaled, since it's the space glyph width, which is based on the already-
    // scaled font-size.
    auto spacing = aStyleText.mWordSpacing;
    spacing.ScaleLengthsBy(GetSVGFontSizeScaleFactor(aFrame));
    return spacing.Resolve([&] { return GetSpaceWidthAppUnits(aTextRun); });
  }

  return aStyleText.mWordSpacing.Resolve(
      [&] { return GetSpaceWidthAppUnits(aTextRun); });
}

// Returns gfxTextRunFactory::TEXT_ENABLE_SPACING if non-standard
// letter-spacing or word-spacing is present.
static gfx::ShapedTextFlags GetSpacingFlags(
    nsIFrame* aFrame, const nsStyleText* aStyleText = nullptr) {
  const nsStyleText* styleText = aFrame->StyleText();
  const auto& ls = styleText->mLetterSpacing;
  const auto& ws = styleText->mWordSpacing;

  // It's possible to have a calc() value that computes to zero but for which
  // IsDefinitelyZero() is false, in which case we'll return
  // TEXT_ENABLE_SPACING unnecessarily. That's ok because such cases are likely
  // to be rare, and avoiding TEXT_ENABLE_SPACING is just an optimization.
  bool nonStandardSpacing = !ls.IsDefinitelyZero() || !ws.IsDefinitelyZero();
  return nonStandardSpacing ? gfx::ShapedTextFlags::TEXT_ENABLE_SPACING
                            : gfx::ShapedTextFlags();
}

bool BuildTextRunsScanner::ContinueTextRunAcrossFrames(nsTextFrame* aFrame1,
                                                       nsTextFrame* aFrame2) {
  // We don't need to check font size inflation, since
  // |FindLineContainer| above (via |nsIFrame::CanContinueTextRun|)
  // ensures that text runs never cross block boundaries.  This means
  // that the font size inflation on all text frames in the text run is
  // already guaranteed to be the same as each other (and for the line
  // container).
  if (mBidiEnabled) {
    FrameBidiData data1 = aFrame1->GetBidiData();
    FrameBidiData data2 = aFrame2->GetBidiData();
    if (data1.embeddingLevel != data2.embeddingLevel ||
        data2.precedingControl != kBidiLevelNone) {
      return false;
    }
  }

  ComputedStyle* sc1 = aFrame1->Style();
  ComputedStyle* sc2 = aFrame2->Style();

  // Any difference in writing-mode/directionality inhibits shaping across
  // the boundary.
  WritingMode wm(sc1);
  if (wm != WritingMode(sc2)) {
    return false;
  }

  const nsStyleText* textStyle1 = sc1->StyleText();
  // If the first frame ends in a preformatted newline, then we end the textrun
  // here. This avoids creating giant textruns for an entire plain text file.
  // Note that we create a single text frame for a preformatted text node,
  // even if it has newlines in it, so typically we won't see trailing newlines
  // until after reflow has broken up the frame into one (or more) frames per
  // line. That's OK though.
  if (textStyle1->NewlineIsSignificant(aFrame1) &&
      HasTerminalNewline(aFrame1)) {
    return false;
  }

  if (aFrame1->GetParent()->GetContent() !=
      aFrame2->GetParent()->GetContent()) {
    // Does aFrame, or any ancestor between it and aAncestor, have a property
    // that should inhibit cross-element-boundary shaping on aSide?
    auto PreventCrossBoundaryShaping = [](const nsIFrame* aFrame,
                                          const nsIFrame* aAncestor,
                                          Side aSide) {
      while (aFrame != aAncestor) {
        ComputedStyle* ctx = aFrame->Style();
        const auto positionProperty = ctx->StyleDisplay()->mPosition;
        // According to https://drafts.csswg.org/css-text/#boundary-shaping:
        //
        // Text shaping must be broken at inline box boundaries when any of
        // the following are true for any box whose boundary separates the
        // two typographic character units:
        //
        // 1. Any of margin/border/padding separating the two typographic
        //    character units in the inline axis is non-zero.
        const auto margin =
            ctx->StyleMargin()->GetMargin(aSide, positionProperty);
        if (!margin->ConvertsToLength() ||
            margin->AsLengthPercentage().ToLength() != 0) {
          return true;
        }
        const auto& padding = ctx->StylePadding()->mPadding.Get(aSide);
        if (!padding.ConvertsToLength() || padding.ToLength() != 0) {
          return true;
        }
        if (ctx->StyleBorder()->GetComputedBorderWidth(aSide) != 0) {
          return true;
        }

        // 2. vertical-align is not baseline.
        //
        // FIXME: Should this use VerticalAlignEnum()?
        const auto& verticalAlign = ctx->StyleDisplay()->mVerticalAlign;
        if (!verticalAlign.IsKeyword() ||
            verticalAlign.AsKeyword() != StyleVerticalAlignKeyword::Baseline) {
          return true;
        }

        // 3. The boundary is a bidi isolation boundary.
        const auto unicodeBidi = ctx->StyleTextReset()->mUnicodeBidi;
        if (unicodeBidi == StyleUnicodeBidi::Isolate ||
            unicodeBidi == StyleUnicodeBidi::IsolateOverride) {
          return true;
        }

        aFrame = aFrame->GetParent();
      }
      return false;
    };

    const nsIFrame* ancestor =
        nsLayoutUtils::FindNearestCommonAncestorFrameWithinBlock(aFrame1,
                                                                 aFrame2);

    if (!ancestor) {
      // The two frames are within different blocks, e.g. due to block
      // fragmentation. In theory we shouldn't prevent cross-frame shaping
      // here, but it's an edge case where we should rarely decide to allow
      // cross-frame shaping, so we don't try harder here.
      return false;
    }

    // We inhibit cross-element-boundary shaping if we're in SVG content,
    // as there are too many things SVG might be doing (like applying per-
    // element positioning) that wouldn't make sense with shaping across
    // the boundary.
    if (ancestor->IsInSVGTextSubtree()) {
      return false;
    }

    // Map inline-end and inline-start to physical sides for checking presence
    // of non-zero margin/border/padding.
    Side side1 = wm.PhysicalSide(LogicalSide::IEnd);
    Side side2 = wm.PhysicalSide(LogicalSide::IStart);
    // If the frames have an embedding level that is opposite to the writing
    // mode, we need to swap which sides we're checking.
    if (aFrame1->GetEmbeddingLevel().IsRTL() == wm.IsBidiLTR()) {
      std::swap(side1, side2);
    }

    if (PreventCrossBoundaryShaping(aFrame1, ancestor, side1) ||
        PreventCrossBoundaryShaping(aFrame2, ancestor, side2)) {
      return false;
    }
  }

  if (aFrame1->GetContent() == aFrame2->GetContent() &&
      aFrame1->GetNextInFlow() != aFrame2) {
    // aFrame2 must be a non-fluid continuation of aFrame1. This can happen
    // sometimes when the unicode-bidi property is used; the bidi resolver
    // breaks text into different frames even though the text has the same
    // direction. We can't allow these two frames to share the same textrun
    // because that would violate our invariant that two flows in the same
    // textrun have different content elements.
    return false;
  }

  if (sc1 == sc2) {
    return true;
  }

  const nsStyleText* textStyle2 = sc2->StyleText();
  if (textStyle1->mTextTransform != textStyle2->mTextTransform ||
      textStyle1->EffectiveWordBreak() != textStyle2->EffectiveWordBreak() ||
      textStyle1->mLineBreak != textStyle2->mLineBreak) {
    return false;
  }

  nsPresContext* pc = aFrame1->PresContext();
  MOZ_ASSERT(pc == aFrame2->PresContext());

  const nsStyleFont* fontStyle1 = sc1->StyleFont();
  const nsStyleFont* fontStyle2 = sc2->StyleFont();
  nscoord letterSpacing1 = LetterSpacing(aFrame1, *textStyle1);
  nscoord letterSpacing2 = LetterSpacing(aFrame2, *textStyle2);
  return fontStyle1->mFont == fontStyle2->mFont &&
         fontStyle1->mLanguage == fontStyle2->mLanguage &&
         nsLayoutUtils::GetTextRunFlagsForStyle(sc1, pc, fontStyle1, textStyle1,
                                                letterSpacing1) ==
             nsLayoutUtils::GetTextRunFlagsForStyle(sc2, pc, fontStyle2,
                                                    textStyle2, letterSpacing2);
}

void BuildTextRunsScanner::ScanFrame(nsIFrame* aFrame) {
  LayoutFrameType frameType = aFrame->Type();
  if (frameType == LayoutFrameType::RubyTextContainer) {
    // Don't include any ruby text container into the text run.
    return;
  }

  // First check if we can extend the current mapped frame block. This is
  // common.
  if (mMappedFlows.Length() > 0) {
    MappedFlow* mappedFlow = &mMappedFlows[mMappedFlows.Length() - 1];
    if (mappedFlow->mEndFrame == aFrame &&
        aFrame->HasAnyStateBits(NS_FRAME_IS_FLUID_CONTINUATION)) {
      NS_ASSERTION(frameType == LayoutFrameType::Text,
                   "Flow-sibling of a text frame is not a text frame?");

      // Don't do this optimization if mLastFrame has a terminal newline...
      // it's quite likely preformatted and we might want to end the textrun
      // here. This is almost always true:
      if (mLastFrame->Style() == aFrame->Style() &&
          !HasTerminalNewline(mLastFrame)) {
        AccumulateRunInfo(static_cast<nsTextFrame*>(aFrame));
        return;
      }
    }
  }

  // Now see if we can add a new set of frames to the current textrun
  if (frameType == LayoutFrameType::Text) {
    nsTextFrame* frame = static_cast<nsTextFrame*>(aFrame);

    if (mLastFrame) {
      if (!ContinueTextRunAcrossFrames(mLastFrame, frame)) {
        FlushFrames(false, false);
      } else {
        if (mLastFrame->GetContent() == frame->GetContent()) {
          AccumulateRunInfo(frame);
          return;
        }
      }
    }

    MappedFlow* mappedFlow = mMappedFlows.AppendElement();
    mappedFlow->mStartFrame = frame;
    mappedFlow->mAncestorControllingInitialBreak = mCommonAncestorWithLastFrame;

    AccumulateRunInfo(frame);
    if (mMappedFlows.Length() == 1) {
      mCurrentFramesAllSameTextRun = frame->GetTextRun(mWhichTextRun);
      mCurrentRunContextInfo = mNextRunContextInfo;
    }
    return;
  }

  if (frameType == LayoutFrameType::Placeholder &&
      aFrame->HasAnyStateBits(PLACEHOLDER_FOR_ABSPOS |
                              PLACEHOLDER_FOR_FIXEDPOS)) {
    // Somewhat hacky fix for bug 1418472:
    // If this is a placeholder for an absolute-positioned frame, we need to
    // flush the line-breaker to prevent the placeholder becoming separated
    // from the immediately-following content.
    // XXX This will interrupt text shaping (ligatures, etc) if an abs-pos
    // element occurs within a word where shaping should be in effect, but
    // that's an edge case, unlikely to occur in real content. A more precise
    // fix might require better separation of line-breaking from textrun setup,
    // but that's a big invasive change (and potentially expensive for perf, as
    // it might introduce an additional pass over all the frames).
    FlushFrames(true, false);
  }

  FrameTextTraversal traversal = CanTextCrossFrameBoundary(aFrame);
  bool isBR = frameType == LayoutFrameType::Br;
  if (!traversal.mLineBreakerCanCrossFrameBoundary) {
    // BR frames are special. We do not need or want to record a break
    // opportunity before a BR frame.
    FlushFrames(true, isBR);
    mCommonAncestorWithLastFrame = aFrame;
    mNextRunContextInfo &= ~nsTextFrameUtils::INCOMING_WHITESPACE;
    mStartOfLine = false;
  } else if (!traversal.mTextRunCanCrossFrameBoundary) {
    FlushFrames(false, false);
  }

  for (nsIFrame* f = traversal.NextFrameToScan(); f;
       f = traversal.NextFrameToScan()) {
    ScanFrame(f);
  }

  if (!traversal.mLineBreakerCanCrossFrameBoundary) {
    // Really if we're a BR frame this is unnecessary since descendInto will be
    // false. In fact this whole "if" statement should move into the
    // descendInto.
    FlushFrames(true, isBR);
    mCommonAncestorWithLastFrame = aFrame;
    mNextRunContextInfo &= ~nsTextFrameUtils::INCOMING_WHITESPACE;
  } else if (!traversal.mTextRunCanCrossFrameBoundary) {
    FlushFrames(false, false);
  }

  LiftCommonAncestorWithLastFrameToParent(aFrame->GetParent());
}

nsTextFrame* BuildTextRunsScanner::GetNextBreakBeforeFrame(uint32_t* aIndex) {
  uint32_t index = *aIndex;
  if (index >= mLineBreakBeforeFrames.Length()) {
    return nullptr;
  }
  *aIndex = index + 1;
  return static_cast<nsTextFrame*>(mLineBreakBeforeFrames.ElementAt(index));
}

static gfxFontGroup* GetFontGroupForFrame(
    const nsIFrame* aFrame, float aFontSizeInflation,
    nsFontMetrics** aOutFontMetrics = nullptr) {
  RefPtr<nsFontMetrics> metrics =
      nsLayoutUtils::GetFontMetricsForFrame(aFrame, aFontSizeInflation);
  gfxFontGroup* fontGroup = metrics->GetThebesFontGroup();

  // Populate outparam before we return:
  if (aOutFontMetrics) {
    metrics.forget(aOutFontMetrics);
  }
  // XXX this is a bit bogus, we're releasing 'metrics' so the
  // returned font-group might actually be torn down, although because
  // of the way the device context caches font metrics, this seems to
  // not actually happen. But we should fix this.
  return fontGroup;
}

nsFontMetrics* nsTextFrame::InflatedFontMetrics() const {
  if (!mFontMetrics) {
    float inflation = nsLayoutUtils::FontSizeInflationFor(this);
    mFontMetrics = nsLayoutUtils::GetFontMetricsForFrame(this, inflation);
  }
  return mFontMetrics;
}

static gfxFontGroup* GetInflatedFontGroupForFrame(nsTextFrame* aFrame) {
  gfxTextRun* textRun = aFrame->GetTextRun(nsTextFrame::eInflated);
  if (textRun) {
    return textRun->GetFontGroup();
  }
  return aFrame->InflatedFontMetrics()->GetThebesFontGroup();
}

static already_AddRefed<DrawTarget> CreateReferenceDrawTarget(
    const nsTextFrame* aTextFrame) {
  UniquePtr<gfxContext> ctx =
      aTextFrame->PresShell()->CreateReferenceRenderingContext();
  RefPtr<DrawTarget> dt = ctx->GetDrawTarget();
  return dt.forget();
}

static already_AddRefed<gfxTextRun> GetHyphenTextRun(nsTextFrame* aTextFrame,
                                                     DrawTarget* aDrawTarget) {
  RefPtr<DrawTarget> dt = aDrawTarget;
  if (!dt) {
    dt = CreateReferenceDrawTarget(aTextFrame);
    if (!dt) {
      return nullptr;
    }
  }

  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetInflatedFontMetricsForFrame(aTextFrame);
  auto* fontGroup = fm->GetThebesFontGroup();
  auto appPerDev = aTextFrame->PresContext()->AppUnitsPerDevPixel();
  const auto& hyphenateChar = aTextFrame->StyleText()->mHyphenateCharacter;
  gfx::ShapedTextFlags flags =
      nsLayoutUtils::GetTextRunOrientFlagsForStyle(aTextFrame->Style());
  // Make the directionality of the hyphen run (in case it is multi-char) match
  // the text frame.
  if (aTextFrame->GetWritingMode().IsBidiRTL()) {
    flags |= gfx::ShapedTextFlags::TEXT_IS_RTL;
  }
  if (hyphenateChar.IsAuto()) {
    return fontGroup->MakeHyphenTextRun(dt, flags, appPerDev);
  }
  auto* missingFonts = aTextFrame->PresContext()->MissingFontRecorder();
  const NS_ConvertUTF8toUTF16 hyphenStr(hyphenateChar.AsString().AsString());
  return fontGroup->MakeTextRun(hyphenStr.BeginReading(), hyphenStr.Length(),
                                dt, appPerDev, flags, nsTextFrameUtils::Flags(),
                                missingFonts);
}

already_AddRefed<gfxTextRun> BuildTextRunsScanner::BuildTextRunForFrames(
    void* aTextBuffer) {
  gfxSkipChars skipChars;

  const void* textPtr = aTextBuffer;
  bool anyTextTransformStyle = false;
  bool anyMathMLStyling = false;
  bool anyTextEmphasis = false;
  uint8_t sstyScriptLevel = 0;
  uint32_t mathFlags = 0;
  gfx::ShapedTextFlags flags = gfx::ShapedTextFlags();
  nsTextFrameUtils::Flags flags2 = nsTextFrameUtils::Flags::NoBreaks;

  if (mCurrentRunContextInfo & nsTextFrameUtils::INCOMING_WHITESPACE) {
    flags2 |= nsTextFrameUtils::Flags::IncomingWhitespace;
  }
  if (mCurrentRunContextInfo & nsTextFrameUtils::INCOMING_ARABICCHAR) {
    flags |= gfx::ShapedTextFlags::TEXT_INCOMING_ARABICCHAR;
  }

  AutoTArray<int32_t, 50> textBreakPoints;
  TextRunUserData dummyData;
  TextRunMappedFlow dummyMappedFlow;
  TextRunMappedFlow* userMappedFlows;
  TextRunUserData* userData;
  TextRunUserData* userDataToDestroy;
  // If the situation is particularly simple (and common) we don't need to
  // allocate userData.
  if (mMappedFlows.Length() == 1 && !mMappedFlows[0].mEndFrame &&
      mMappedFlows[0].mStartFrame->GetContentOffset() == 0) {
    userData = &dummyData;
    userMappedFlows = &dummyMappedFlow;
    userDataToDestroy = nullptr;
    dummyData.mMappedFlowCount = mMappedFlows.Length();
    dummyData.mLastFlowIndex = 0;
  } else {
    userData = CreateUserData(mMappedFlows.Length());
    userMappedFlows = reinterpret_cast<TextRunMappedFlow*>(userData + 1);
    userDataToDestroy = userData;
  }

  uint32_t currentTransformedTextOffset = 0;

  uint32_t nextBreakIndex = 0;
  nsTextFrame* nextBreakBeforeFrame = GetNextBreakBeforeFrame(&nextBreakIndex);
  bool isSVG = mLineContainer->IsInSVGTextSubtree();
  bool enabledJustification =
      (mLineContainer->StyleText()->mTextAlign == StyleTextAlign::Justify ||
       mLineContainer->StyleText()->mTextAlignLast ==
           StyleTextAlignLast::Justify);

  const nsStyleText* textStyle = nullptr;
  const nsStyleFont* fontStyle = nullptr;
  ComputedStyle* lastComputedStyle = nullptr;
  for (uint32_t i = 0; i < mMappedFlows.Length(); ++i) {
    MappedFlow* mappedFlow = &mMappedFlows[i];
    nsTextFrame* f = mappedFlow->mStartFrame;

    lastComputedStyle = f->Style();
    // Detect use of text-transform or font-variant anywhere in the run
    textStyle = f->StyleText();
    if (!textStyle->mTextTransform.IsNone() ||
        textStyle->mWebkitTextSecurity != StyleTextSecurity::None ||
        // text-combine-upright requires converting from full-width
        // characters to non-full-width correspendent in some cases.
        lastComputedStyle->IsTextCombined()) {
      anyTextTransformStyle = true;
    }
    if (textStyle->HasEffectiveTextEmphasis()) {
      anyTextEmphasis = true;
    }
    flags |= GetSpacingFlags(f);
    nsTextFrameUtils::CompressionMode compression =
        GetCSSWhitespaceToCompressionMode(f, textStyle);
    if ((enabledJustification || f->ShouldSuppressLineBreak()) && !isSVG) {
      flags |= gfx::ShapedTextFlags::TEXT_ENABLE_SPACING;
    }
    fontStyle = f->StyleFont();
    nsIFrame* parent = mLineContainer->GetParent();
    if (StyleMathVariant::None != fontStyle->mMathVariant) {
      if (StyleMathVariant::Normal != fontStyle->mMathVariant) {
        anyMathMLStyling = true;
      }
    } else if (mLineContainer->HasAnyStateBits(NS_FRAME_IS_IN_SINGLE_CHAR_MI)) {
      flags2 |= nsTextFrameUtils::Flags::IsSingleCharMi;
      anyMathMLStyling = true;
    }
    if (mLineContainer->HasAnyStateBits(TEXT_IS_IN_TOKEN_MATHML)) {
      // All MathML tokens except <mtext> use 'math' script.
      if (!(parent && parent->GetContent() &&
            parent->GetContent()->IsMathMLElement(nsGkAtoms::mtext))) {
        flags |= gfx::ShapedTextFlags::TEXT_USE_MATH_SCRIPT;
      }
      nsIMathMLFrame* mathFrame = do_QueryFrame(parent);
      if (mathFrame) {
        nsPresentationData presData;
        mathFrame->GetPresentationData(presData);
        if (NS_MATHML_IS_DTLS_SET(presData.flags)) {
          mathFlags |= MathMLTextRunFactory::MATH_FONT_FEATURE_DTLS;
          anyMathMLStyling = true;
        }
      }
    }
    nsIFrame* child = mLineContainer;
    uint8_t oldScriptLevel = 0;
    while (parent &&
           child->HasAnyStateBits(NS_FRAME_MATHML_SCRIPT_DESCENDANT)) {
      // Reconstruct the script level ignoring any user overrides. It is
      // calculated this way instead of using scriptlevel to ensure the
      // correct ssty font feature setting is used even if the user sets a
      // different (especially negative) scriptlevel.
      nsIMathMLFrame* mathFrame = do_QueryFrame(parent);
      if (mathFrame) {
        sstyScriptLevel += mathFrame->ScriptIncrement(child);
      }
      if (sstyScriptLevel < oldScriptLevel) {
        // overflow
        sstyScriptLevel = UINT8_MAX;
        break;
      }
      child = parent;
      parent = parent->GetParent();
      oldScriptLevel = sstyScriptLevel;
    }
    if (sstyScriptLevel) {
      anyMathMLStyling = true;
    }

    // Figure out what content is included in this flow.
    nsIContent* content = f->GetContent();
    const nsTextFragment* frag = f->TextFragment();
    int32_t contentStart = mappedFlow->mStartFrame->GetContentOffset();
    int32_t contentEnd = mappedFlow->GetContentEnd();
    int32_t contentLength = contentEnd - contentStart;
    const nsAtom* language = f->StyleFont()->mLanguage;

    TextRunMappedFlow* newFlow = &userMappedFlows[i];
    newFlow->mStartFrame = mappedFlow->mStartFrame;
    newFlow->mDOMOffsetToBeforeTransformOffset =
        skipChars.GetOriginalCharCount() -
        mappedFlow->mStartFrame->GetContentOffset();
    newFlow->mContentLength = contentLength;

    while (nextBreakBeforeFrame &&
           nextBreakBeforeFrame->GetContent() == content) {
      textBreakPoints.AppendElement(nextBreakBeforeFrame->GetContentOffset() +
                                    newFlow->mDOMOffsetToBeforeTransformOffset);
      nextBreakBeforeFrame = GetNextBreakBeforeFrame(&nextBreakIndex);
    }

    nsTextFrameUtils::Flags analysisFlags;
    if (frag->Is2b()) {
      NS_ASSERTION(mDoubleByteText, "Wrong buffer char size!");
      char16_t* bufStart = static_cast<char16_t*>(aTextBuffer);
      char16_t* bufEnd = nsTextFrameUtils::TransformText(
          frag->Get2b() + contentStart, contentLength, bufStart, compression,
          &mNextRunContextInfo, &skipChars, &analysisFlags, language);
      aTextBuffer = bufEnd;
      currentTransformedTextOffset =
          bufEnd - static_cast<const char16_t*>(textPtr);
    } else {
      if (mDoubleByteText) {
        // Need to expand the text. First transform it into a temporary buffer,
        // then expand.
        AutoTArray<uint8_t, BIG_TEXT_NODE_SIZE> tempBuf;
        uint8_t* bufStart = tempBuf.AppendElements(contentLength, fallible);
        if (!bufStart) {
          DestroyUserData(userDataToDestroy);
          return nullptr;
        }
        uint8_t* end = nsTextFrameUtils::TransformText(
            reinterpret_cast<const uint8_t*>(frag->Get1b()) + contentStart,
            contentLength, bufStart, compression, &mNextRunContextInfo,
            &skipChars, &analysisFlags, language);
        aTextBuffer =
            ExpandBuffer(static_cast<char16_t*>(aTextBuffer),
                         tempBuf.Elements(), end - tempBuf.Elements());
        currentTransformedTextOffset = static_cast<char16_t*>(aTextBuffer) -
                                       static_cast<const char16_t*>(textPtr);
      } else {
        uint8_t* bufStart = static_cast<uint8_t*>(aTextBuffer);
        uint8_t* end = nsTextFrameUtils::TransformText(
            reinterpret_cast<const uint8_t*>(frag->Get1b()) + contentStart,
            contentLength, bufStart, compression, &mNextRunContextInfo,
            &skipChars, &analysisFlags, language);
        aTextBuffer = end;
        currentTransformedTextOffset =
            end - static_cast<const uint8_t*>(textPtr);
      }
    }
    flags2 |= analysisFlags;
  }

  void* finalUserData;
  if (userData == &dummyData) {
    flags2 |= nsTextFrameUtils::Flags::IsSimpleFlow;
    userData = nullptr;
    finalUserData = mMappedFlows[0].mStartFrame;
  } else {
    finalUserData = userData;
  }

  uint32_t transformedLength = currentTransformedTextOffset;

  // Now build the textrun
  nsTextFrame* firstFrame = mMappedFlows[0].mStartFrame;
  float fontInflation;
  gfxFontGroup* fontGroup;
  if (mWhichTextRun == nsTextFrame::eNotInflated) {
    fontInflation = 1.0f;
    fontGroup = GetFontGroupForFrame(firstFrame, fontInflation);
  } else {
    fontInflation = nsLayoutUtils::FontSizeInflationFor(firstFrame);
    fontGroup = GetInflatedFontGroupForFrame(firstFrame);
  }
  MOZ_ASSERT(fontGroup);

  if (flags2 & nsTextFrameUtils::Flags::HasTab) {
    flags |= gfx::ShapedTextFlags::TEXT_ENABLE_SPACING;
  }
  if (flags2 & nsTextFrameUtils::Flags::HasShy) {
    flags |= gfx::ShapedTextFlags::TEXT_ENABLE_HYPHEN_BREAKS;
  }
  if (mBidiEnabled && (firstFrame->GetEmbeddingLevel().IsRTL())) {
    flags |= gfx::ShapedTextFlags::TEXT_IS_RTL;
  }
  if (mNextRunContextInfo & nsTextFrameUtils::INCOMING_WHITESPACE) {
    flags2 |= nsTextFrameUtils::Flags::TrailingWhitespace;
  }
  if (mNextRunContextInfo & nsTextFrameUtils::INCOMING_ARABICCHAR) {
    flags |= gfx::ShapedTextFlags::TEXT_TRAILING_ARABICCHAR;
  }
  // ContinueTextRunAcrossFrames guarantees that it doesn't matter which
  // frame's style is used, so we use a mixture of the first frame and
  // last frame's style
  flags |= nsLayoutUtils::GetTextRunFlagsForStyle(
      lastComputedStyle, firstFrame->PresContext(), fontStyle, textStyle,
      LetterSpacing(firstFrame, *textStyle));
  // XXX this is a bit of a hack. For performance reasons, if we're favouring
  // performance over quality, don't try to get accurate glyph extents.
  if (!(flags & gfx::ShapedTextFlags::TEXT_OPTIMIZE_SPEED)) {
    flags |= gfx::ShapedTextFlags::TEXT_NEED_BOUNDING_BOX;
  }

  // Convert linebreak coordinates to transformed string offsets
  NS_ASSERTION(nextBreakIndex == mLineBreakBeforeFrames.Length(),
               "Didn't find all the frames to break-before...");
  gfxSkipCharsIterator iter(skipChars);
  AutoTArray<uint32_t, 50> textBreakPointsAfterTransform;
  for (uint32_t i = 0; i < textBreakPoints.Length(); ++i) {
    nsTextFrameUtils::AppendLineBreakOffset(
        &textBreakPointsAfterTransform,
        iter.ConvertOriginalToSkipped(textBreakPoints[i]));
  }
  if (mStartOfLine) {
    nsTextFrameUtils::AppendLineBreakOffset(&textBreakPointsAfterTransform,
                                            transformedLength);
  }

  // Setup factory chain
  bool needsToMaskPassword = NeedsToMaskPassword(firstFrame);
  UniquePtr<nsTransformingTextRunFactory> transformingFactory;
  if (anyTextTransformStyle || needsToMaskPassword) {
    char16_t maskChar =
        needsToMaskPassword ? 0 : textStyle->TextSecurityMaskChar();
    transformingFactory = MakeUnique<nsCaseTransformTextRunFactory>(
        std::move(transformingFactory), false, maskChar);
  }
  if (anyMathMLStyling) {
    transformingFactory = MakeUnique<MathMLTextRunFactory>(
        std::move(transformingFactory), mathFlags, sstyScriptLevel,
        fontInflation);
  }
  nsTArray<RefPtr<nsTransformedCharStyle>> styles;
  if (transformingFactory) {
    uint32_t unmaskStart = 0, unmaskEnd = UINT32_MAX;
    if (needsToMaskPassword) {
      unmaskStart = unmaskEnd = UINT32_MAX;
      const TextEditor* const passwordEditor =
          nsContentUtils::GetExtantTextEditorFromAnonymousNode(
              firstFrame->GetContent());
      if (passwordEditor && !passwordEditor->IsAllMasked()) {
        unmaskStart = passwordEditor->UnmaskedStart();
        unmaskEnd = passwordEditor->UnmaskedEnd();
      }
    }

    iter.SetOriginalOffset(0);
    for (uint32_t i = 0; i < mMappedFlows.Length(); ++i) {
      MappedFlow* mappedFlow = &mMappedFlows[i];
      nsTextFrame* f;
      ComputedStyle* sc = nullptr;
      RefPtr<nsTransformedCharStyle> defaultStyle;
      RefPtr<nsTransformedCharStyle> unmaskStyle;
      for (f = mappedFlow->mStartFrame; f != mappedFlow->mEndFrame;
           f = f->GetNextContinuation()) {
        uint32_t skippedOffset = iter.GetSkippedOffset();
        // Text-combined frames have content-dependent transform, so we
        // want to create new nsTransformedCharStyle for them anyway.
        if (sc != f->Style() || sc->IsTextCombined()) {
          sc = f->Style();
          defaultStyle = new nsTransformedCharStyle(sc, f->PresContext());
          if (sc->IsTextCombined() && f->CountGraphemeClusters() > 1) {
            defaultStyle->mForceNonFullWidth = true;
          }
          if (needsToMaskPassword) {
            defaultStyle->mMaskPassword = true;
            if (unmaskStart != unmaskEnd) {
              unmaskStyle = new nsTransformedCharStyle(sc, f->PresContext());
              unmaskStyle->mForceNonFullWidth =
                  defaultStyle->mForceNonFullWidth;
            }
          }
        }
        iter.AdvanceOriginal(f->GetContentLength());
        uint32_t skippedEnd = iter.GetSkippedOffset();
        if (unmaskStyle) {
          uint32_t skippedUnmaskStart =
              iter.ConvertOriginalToSkipped(unmaskStart);
          uint32_t skippedUnmaskEnd = iter.ConvertOriginalToSkipped(unmaskEnd);
          iter.SetSkippedOffset(skippedEnd);
          for (; skippedOffset < std::min(skippedEnd, skippedUnmaskStart);
               ++skippedOffset) {
            styles.AppendElement(defaultStyle);
          }
          for (; skippedOffset < std::min(skippedEnd, skippedUnmaskEnd);
               ++skippedOffset) {
            styles.AppendElement(unmaskStyle);
          }
          for (; skippedOffset < skippedEnd; ++skippedOffset) {
            styles.AppendElement(defaultStyle);
          }
        } else {
          for (; skippedOffset < skippedEnd; ++skippedOffset) {
            styles.AppendElement(defaultStyle);
          }
        }
      }
    }
    flags2 |= nsTextFrameUtils::Flags::IsTransformed;
    NS_ASSERTION(iter.GetSkippedOffset() == transformedLength,
                 "We didn't cover all the characters in the text run!");
  }

  RefPtr<gfxTextRun> textRun;
  gfxTextRunFactory::Parameters params = {
      mDrawTarget,
      finalUserData,
      &skipChars,
      textBreakPointsAfterTransform.Elements(),
      uint32_t(textBreakPointsAfterTransform.Length()),
      int32_t(firstFrame->PresContext()->AppUnitsPerDevPixel())};

  if (mDoubleByteText) {
    const char16_t* text = static_cast<const char16_t*>(textPtr);
    if (transformingFactory) {
      textRun = transformingFactory->MakeTextRun(
          text, transformedLength, &params, fontGroup, flags, flags2,
          std::move(styles), true);
    } else {
      textRun = fontGroup->MakeTextRun(text, transformedLength, &params, flags,
                                       flags2, mMissingFonts);
    }
  } else {
    const uint8_t* text = static_cast<const uint8_t*>(textPtr);
    flags |= gfx::ShapedTextFlags::TEXT_IS_8BIT;
    if (transformingFactory) {
      textRun = transformingFactory->MakeTextRun(
          text, transformedLength, &params, fontGroup, flags, flags2,
          std::move(styles), true);
    } else {
      textRun = fontGroup->MakeTextRun(text, transformedLength, &params, flags,
                                       flags2, mMissingFonts);
    }
  }
  if (!textRun) {
    DestroyUserData(userDataToDestroy);
    return nullptr;
  }

  // We have to set these up after we've created the textrun, because
  // the breaks may be stored in the textrun during this very call.
  // This is a bit annoying because it requires another loop over the frames
  // making up the textrun, but I don't see a way to avoid this.
  // We have to do this if line-breaking is required OR if a text-transform
  // is in effect, because we depend on the line-breaker's scanner (via
  // BreakSink::Finish) to finish building transformed textruns.
  if (mDoLineBreaking || transformingFactory) {
    SetupBreakSinksForTextRun(textRun.get(), textPtr);
  }

  // Ownership of the factory has passed to the textrun
  // TODO: bug 1285316: clean up ownership transfer from the factory to
  // the textrun
  Unused << transformingFactory.release();

  if (anyTextEmphasis) {
    SetupTextEmphasisForTextRun(textRun.get(), textPtr);
  }

  if (mSkipIncompleteTextRuns) {
    mSkipIncompleteTextRuns = !TextContainsLineBreakerWhiteSpace(
        textPtr, transformedLength, mDoubleByteText);
    // Since we're doing to destroy the user data now, avoid a dangling
    // pointer. Strictly speaking we don't need to do this since it should
    // not be used (since this textrun will not be used and will be
    // itself deleted soon), but it's always better to not have dangling
    // pointers around.
    textRun->SetUserData(nullptr);
    DestroyUserData(userDataToDestroy);
    return nullptr;
  }

  // Actually wipe out the textruns associated with the mapped frames and
  // associate those frames with this text run.
  AssignTextRun(textRun.get(), fontInflation);
  return textRun.forget();
}

// This is a cut-down version of BuildTextRunForFrames used to set up
// context for the line-breaker, when the textrun has already been created.
// So it does the same walk over the mMappedFlows, but doesn't actually
// build a new textrun.
bool BuildTextRunsScanner::SetupLineBreakerContext(gfxTextRun* aTextRun) {
  AutoTArray<uint8_t, BIG_TEXT_NODE_SIZE> buffer;
  uint32_t bufferSize = mMaxTextLength * (mDoubleByteText ? 2 : 1);
  if (bufferSize < mMaxTextLength || bufferSize == UINT32_MAX) {
    return false;
  }
  void* textPtr = buffer.AppendElements(bufferSize, fallible);
  if (!textPtr) {
    return false;
  }

  gfxSkipChars skipChars;
  const nsAtom* language = mMappedFlows[0].mStartFrame->StyleFont()->mLanguage;

  for (uint32_t i = 0; i < mMappedFlows.Length(); ++i) {
    MappedFlow* mappedFlow = &mMappedFlows[i];
    nsTextFrame* f = mappedFlow->mStartFrame;

    const nsStyleText* textStyle = f->StyleText();
    nsTextFrameUtils::CompressionMode compression =
        GetCSSWhitespaceToCompressionMode(f, textStyle);

    // Figure out what content is included in this flow.
    const nsTextFragment* frag = f->TextFragment();
    int32_t contentStart = mappedFlow->mStartFrame->GetContentOffset();
    int32_t contentEnd = mappedFlow->GetContentEnd();
    int32_t contentLength = contentEnd - contentStart;

    nsTextFrameUtils::Flags analysisFlags;
    if (frag->Is2b()) {
      NS_ASSERTION(mDoubleByteText, "Wrong buffer char size!");
      char16_t* bufStart = static_cast<char16_t*>(textPtr);
      char16_t* bufEnd = nsTextFrameUtils::TransformText(
          frag->Get2b() + contentStart, contentLength, bufStart, compression,
          &mNextRunContextInfo, &skipChars, &analysisFlags, language);
      textPtr = bufEnd;
    } else {
      if (mDoubleByteText) {
        // Need to expand the text. First transform it into a temporary buffer,
        // then expand.
        AutoTArray<uint8_t, BIG_TEXT_NODE_SIZE> tempBuf;
        uint8_t* bufStart = tempBuf.AppendElements(contentLength, fallible);
        if (!bufStart) {
          return false;
        }
        uint8_t* end = nsTextFrameUtils::TransformText(
            reinterpret_cast<const uint8_t*>(frag->Get1b()) + contentStart,
            contentLength, bufStart, compression, &mNextRunContextInfo,
            &skipChars, &analysisFlags, language);
        textPtr = ExpandBuffer(static_cast<char16_t*>(textPtr),
                               tempBuf.Elements(), end - tempBuf.Elements());
      } else {
        uint8_t* bufStart = static_cast<uint8_t*>(textPtr);
        uint8_t* end = nsTextFrameUtils::TransformText(
            reinterpret_cast<const uint8_t*>(frag->Get1b()) + contentStart,
            contentLength, bufStart, compression, &mNextRunContextInfo,
            &skipChars, &analysisFlags, language);
        textPtr = end;
      }
    }
  }

  // We have to set these up after we've created the textrun, because
  // the breaks may be stored in the textrun during this very call.
  // This is a bit annoying because it requires another loop over the frames
  // making up the textrun, but I don't see a way to avoid this.
  SetupBreakSinksForTextRun(aTextRun, buffer.Elements());

  return true;
}

static bool HasCompressedLeadingWhitespace(
    nsTextFrame* aFrame, const nsStyleText* aStyleText,
    int32_t aContentEndOffset, const gfxSkipCharsIterator& aIterator) {
  if (!aIterator.IsOriginalCharSkipped()) {
    return false;
  }

  gfxSkipCharsIterator iter = aIterator;
  int32_t frameContentOffset = aFrame->GetContentOffset();
  const nsTextFragment* frag = aFrame->TextFragment();
  while (frameContentOffset < aContentEndOffset &&
         iter.IsOriginalCharSkipped()) {
    if (IsTrimmableSpace(frag, frameContentOffset, aStyleText)) {
      return true;
    }
    ++frameContentOffset;
    iter.AdvanceOriginal(1);
  }
  return false;
}

void BuildTextRunsScanner::SetupBreakSinksForTextRun(gfxTextRun* aTextRun,
                                                     const void* aTextPtr) {
  using mozilla::intl::LineBreakRule;
  using mozilla::intl::WordBreakRule;

  // textruns have uniform language
  const nsStyleFont* styleFont = mMappedFlows[0].mStartFrame->StyleFont();
  // We should only use a language for hyphenation if it was specified
  // explicitly.
  nsAtom* hyphenationLanguage =
      styleFont->mExplicitLanguage ? styleFont->mLanguage.get() : nullptr;
  // We keep this pointed at the skip-chars data for the current mappedFlow.
  // This lets us cheaply check whether the flow has compressed initial
  // whitespace...
  gfxSkipCharsIterator iter(aTextRun->GetSkipChars());

  for (uint32_t i = 0; i < mMappedFlows.Length(); ++i) {
    MappedFlow* mappedFlow = &mMappedFlows[i];
    // The CSS word-break value may change within a word, so we reset it for
    // each MappedFlow. The line-breaker will flush its text if the property
    // actually changes.
    const auto* styleText = mappedFlow->mStartFrame->StyleText();
    auto wordBreak = styleText->EffectiveWordBreak();
    switch (wordBreak) {
      case StyleWordBreak::BreakAll:
        mLineBreaker.SetWordBreak(WordBreakRule::BreakAll);
        break;
      case StyleWordBreak::KeepAll:
        mLineBreaker.SetWordBreak(WordBreakRule::KeepAll);
        break;
      case StyleWordBreak::Normal:
      default:
        MOZ_ASSERT(wordBreak == StyleWordBreak::Normal);
        mLineBreaker.SetWordBreak(WordBreakRule::Normal);
        break;
    }
    switch (styleText->mLineBreak) {
      case StyleLineBreak::Auto:
        mLineBreaker.SetStrictness(LineBreakRule::Auto);
        break;
      case StyleLineBreak::Normal:
        mLineBreaker.SetStrictness(LineBreakRule::Normal);
        break;
      case StyleLineBreak::Loose:
        mLineBreaker.SetStrictness(LineBreakRule::Loose);
        break;
      case StyleLineBreak::Strict:
        mLineBreaker.SetStrictness(LineBreakRule::Strict);
        break;
      case StyleLineBreak::Anywhere:
        mLineBreaker.SetStrictness(LineBreakRule::Anywhere);
        break;
    }

    uint32_t offset = iter.GetSkippedOffset();
    gfxSkipCharsIterator iterNext = iter;
    iterNext.AdvanceOriginal(mappedFlow->GetContentEnd() -
                             mappedFlow->mStartFrame->GetContentOffset());

    UniquePtr<BreakSink>* breakSink = mBreakSinks.AppendElement(
        MakeUnique<BreakSink>(aTextRun, mDrawTarget, offset));

    uint32_t length = iterNext.GetSkippedOffset() - offset;
    uint32_t flags = 0;
    nsIFrame* initialBreakController =
        mappedFlow->mAncestorControllingInitialBreak;
    if (!initialBreakController) {
      initialBreakController = mLineContainer;
    }
    if (!initialBreakController->StyleText()->WhiteSpaceCanWrap(
            initialBreakController)) {
      flags |= nsLineBreaker::BREAK_SUPPRESS_INITIAL;
    }
    nsTextFrame* startFrame = mappedFlow->mStartFrame;
    const nsStyleText* textStyle = startFrame->StyleText();
    if (!textStyle->WhiteSpaceCanWrap(startFrame)) {
      flags |= nsLineBreaker::BREAK_SUPPRESS_INSIDE;
    }
    if (aTextRun->GetFlags2() & nsTextFrameUtils::Flags::NoBreaks) {
      flags |= nsLineBreaker::BREAK_SKIP_SETTING_NO_BREAKS;
    }
    if (textStyle->mTextTransform & StyleTextTransform::CAPITALIZE) {
      flags |= nsLineBreaker::BREAK_NEED_CAPITALIZATION;
    }
    if (textStyle->mHyphens == StyleHyphens::Auto &&
        textStyle->mLineBreak != StyleLineBreak::Anywhere) {
      flags |= nsLineBreaker::BREAK_USE_AUTO_HYPHENATION;
    }

    if (HasCompressedLeadingWhitespace(startFrame, textStyle,
                                       mappedFlow->GetContentEnd(), iter)) {
      mLineBreaker.AppendInvisibleWhitespace(flags);
    }

    if (length > 0) {
      BreakSink* sink = mSkipIncompleteTextRuns ? nullptr : (*breakSink).get();
      if (mDoubleByteText) {
        const char16_t* text = reinterpret_cast<const char16_t*>(aTextPtr);
        mLineBreaker.AppendText(hyphenationLanguage, text + offset, length,
                                flags, sink);
      } else {
        const uint8_t* text = reinterpret_cast<const uint8_t*>(aTextPtr);
        mLineBreaker.AppendText(hyphenationLanguage, text + offset, length,
                                flags, sink);
      }
    }

    iter = iterNext;
  }
}

static bool MayCharacterHaveEmphasisMark(uint32_t aCh) {
  // Punctuation characters that *can* take emphasis marks (exceptions to the
  // rule that characters with GeneralCategory=P* do not take emphasis), as per
  // https://drafts.csswg.org/css-text-decor/#text-emphasis-style-property
  // There are no non-BMP codepoints in the punctuation exceptions, so we can
  // just use a 16-bit string to list & check them.
  constexpr nsLiteralString kPunctuationAcceptsEmphasis =
      u"\x0023"  // #  NUMBER SIGN
      u"\x0025"  // %  PERCENT SIGN
      u"\x0026"  // &  AMPERSAND
      u"\x0040"  // @  COMMERCIAL AT
      u"\x00A7"  // §  SECTION SIGN
      u"\x00B6"  // ¶  PILCROW SIGN
      u"\x0609"  // ؉  ARABIC-INDIC PER MILLE SIGN
      u"\x060A"  // ؊  ARABIC-INDIC PER TEN THOUSAND SIGN
      u"\x066A"  // ٪  ARABIC PERCENT SIGN
      u"\x2030"  // ‰  PER MILLE SIGN
      u"\x2031"  // ‱  PER TEN THOUSAND SIGN
      u"\x204A"  // ⁊  TIRONIAN SIGN ET
      u"\x204B"  // ⁋  REVERSED PILCROW SIGN
      u"\x2053"  // ⁓  SWUNG DASH
      u"\x303D"  // 〽️  PART ALTERNATION MARK
      // Characters that are NFKD-equivalent to the above, extracted from
      // UnicodeData.txt.
      u"\xFE5F"      // SMALL NUMBER SIGN;Po;0;ET;<small> 0023;;;;N;;;;;
      u"\xFE60"      // SMALL AMPERSAND;Po;0;ON;<small> 0026;;;;N;;;;;
      u"\xFE6A"      // SMALL PERCENT SIGN;Po;0;ET;<small> 0025;;;;N;;;;;
      u"\xFE6B"      // SMALL COMMERCIAL AT;Po;0;ON;<small> 0040;;;;N;;;;;
      u"\xFF03"      // FULLWIDTH NUMBER SIGN;Po;0;ET;<wide> 0023;;;;N;;;;;
      u"\xFF05"      // FULLWIDTH PERCENT SIGN;Po;0;ET;<wide> 0025;;;;N;;;;;
      u"\xFF06"      // FULLWIDTH AMPERSAND;Po;0;ON;<wide> 0026;;;;N;;;;;
      u"\xFF20"_ns;  // FULLWIDTH COMMERCIAL AT;Po;0;ON;<wide> 0040;;;;N;;;;;

  switch (unicode::GetGenCategory(aCh)) {
    case nsUGenCategory::kSeparator:  // whitespace, line- & para-separators
      return false;
    case nsUGenCategory::kOther:  // control categories
      return false;
    case nsUGenCategory::kPunctuation:
      return aCh <= 0xffff &&
             kPunctuationAcceptsEmphasis.Contains(char16_t(aCh));
    default:
      return true;
  }
}

void BuildTextRunsScanner::SetupTextEmphasisForTextRun(gfxTextRun* aTextRun,
                                                       const void* aTextPtr) {
  if (!mDoubleByteText) {
    auto text = reinterpret_cast<const uint8_t*>(aTextPtr);
    for (auto i : IntegerRange(aTextRun->GetLength())) {
      if (!MayCharacterHaveEmphasisMark(text[i])) {
        aTextRun->SetNoEmphasisMark(i);
      }
    }
  } else {
    auto text = reinterpret_cast<const char16_t*>(aTextPtr);
    auto length = aTextRun->GetLength();
    for (size_t i = 0; i < length; ++i) {
      if (i + 1 < length && NS_IS_SURROGATE_PAIR(text[i], text[i + 1])) {
        uint32_t ch = SURROGATE_TO_UCS4(text[i], text[i + 1]);
        if (!MayCharacterHaveEmphasisMark(ch)) {
          aTextRun->SetNoEmphasisMark(i);
          aTextRun->SetNoEmphasisMark(i + 1);
        }
        ++i;
      } else {
        if (!MayCharacterHaveEmphasisMark(uint32_t(text[i]))) {
          aTextRun->SetNoEmphasisMark(i);
        }
      }
    }
  }
}

// Find the flow corresponding to aContent in aUserData
static inline TextRunMappedFlow* FindFlowForContent(
    TextRunUserData* aUserData, nsIContent* aContent,
    TextRunMappedFlow* userMappedFlows) {
  // Find the flow that contains us
  int32_t i = aUserData->mLastFlowIndex;
  int32_t delta = 1;
  int32_t sign = 1;
  // Search starting at the current position and examine close-by
  // positions first, moving further and further away as we go.
  while (i >= 0 && uint32_t(i) < aUserData->mMappedFlowCount) {
    TextRunMappedFlow* flow = &userMappedFlows[i];
    if (flow->mStartFrame->GetContent() == aContent) {
      return flow;
    }

    i += delta;
    sign = -sign;
    delta = -delta + sign;
  }

  // We ran into an array edge.  Add |delta| to |i| once more to get
  // back to the side where we still need to search, then step in
  // the |sign| direction.
  i += delta;
  if (sign > 0) {
    for (; i < int32_t(aUserData->mMappedFlowCount); ++i) {
      TextRunMappedFlow* flow = &userMappedFlows[i];
      if (flow->mStartFrame->GetContent() == aContent) {
        return flow;
      }
    }
  } else {
    for (; i >= 0; --i) {
      TextRunMappedFlow* flow = &userMappedFlows[i];
      if (flow->mStartFrame->GetContent() == aContent) {
        return flow;
      }
    }
  }

  return nullptr;
}

void BuildTextRunsScanner::AssignTextRun(gfxTextRun* aTextRun,
                                         float aInflation) {
  for (uint32_t i = 0; i < mMappedFlows.Length(); ++i) {
    MappedFlow* mappedFlow = &mMappedFlows[i];
    nsTextFrame* startFrame = mappedFlow->mStartFrame;
    nsTextFrame* endFrame = mappedFlow->mEndFrame;
    nsTextFrame* f;
    for (f = startFrame; f != endFrame; f = f->GetNextContinuation()) {
#ifdef DEBUG_roc
      if (f->GetTextRun(mWhichTextRun)) {
        gfxTextRun* textRun = f->GetTextRun(mWhichTextRun);
        if (textRun->GetFlags2() & nsTextFrameUtils::Flags::IsSimpleFlow) {
          if (mMappedFlows[0].mStartFrame != GetFrameForSimpleFlow(textRun)) {
            NS_WARNING("REASSIGNING SIMPLE FLOW TEXT RUN!");
          }
        } else {
          auto userData =
              static_cast<TextRunUserData*>(aTextRun->GetUserData());
          TextRunMappedFlow* userMappedFlows = GetMappedFlows(aTextRun);
          if (userData->mMappedFlowCount >= mMappedFlows.Length() ||
              userMappedFlows[userData->mMappedFlowCount - 1].mStartFrame !=
                  mMappedFlows[userdata->mMappedFlowCount - 1].mStartFrame) {
            NS_WARNING("REASSIGNING MULTIFLOW TEXT RUN (not append)!");
          }
        }
      }
#endif

      gfxTextRun* oldTextRun = f->GetTextRun(mWhichTextRun);
      if (oldTextRun) {
        nsTextFrame* firstFrame = nullptr;
        uint32_t startOffset = 0;
        if (oldTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsSimpleFlow) {
          firstFrame = GetFrameForSimpleFlow(oldTextRun);
        } else {
          auto userData =
              static_cast<TextRunUserData*>(oldTextRun->GetUserData());
          TextRunMappedFlow* userMappedFlows = GetMappedFlows(oldTextRun);
          firstFrame = userMappedFlows[0].mStartFrame;
          if (MOZ_UNLIKELY(f != firstFrame)) {
            TextRunMappedFlow* flow =
                FindFlowForContent(userData, f->GetContent(), userMappedFlows);
            if (flow) {
              startOffset = flow->mDOMOffsetToBeforeTransformOffset;
            } else {
              NS_ERROR("Can't find flow containing frame 'f'");
            }
          }
        }

        // Optimization: if |f| is the first frame in the flow then there are no
        // prev-continuations that use |oldTextRun|.
        nsTextFrame* clearFrom = nullptr;
        if (MOZ_UNLIKELY(f != firstFrame)) {
          // If all the frames in the mapped flow starting at |f| (inclusive)
          // are empty then we let the prev-continuations keep the old text run.
          gfxSkipCharsIterator iter(oldTextRun->GetSkipChars(), startOffset,
                                    f->GetContentOffset());
          uint32_t textRunOffset =
              iter.ConvertOriginalToSkipped(f->GetContentOffset());
          clearFrom = textRunOffset == oldTextRun->GetLength() ? f : nullptr;
        }
        f->ClearTextRun(clearFrom, mWhichTextRun);

#ifdef DEBUG
        if (firstFrame && !firstFrame->GetTextRun(mWhichTextRun)) {
          // oldTextRun was destroyed - assert that we don't reference it.
          for (uint32_t j = 0; j < mBreakSinks.Length(); ++j) {
            NS_ASSERTION(oldTextRun != mBreakSinks[j]->mTextRun,
                         "destroyed text run is still in use");
          }
        }
#endif
      }
      f->SetTextRun(aTextRun, mWhichTextRun, aInflation);
    }
    // Set this bit now; we can't set it any earlier because
    // f->ClearTextRun() might clear it out.
    nsFrameState whichTextRunState =
        startFrame->GetTextRun(nsTextFrame::eInflated) == aTextRun
            ? TEXT_IN_TEXTRUN_USER_DATA
            : TEXT_IN_UNINFLATED_TEXTRUN_USER_DATA;
    startFrame->AddStateBits(whichTextRunState);
  }
}

NS_QUERYFRAME_HEAD(nsTextFrame)
  NS_QUERYFRAME_ENTRY(nsTextFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsIFrame)

gfxSkipCharsIterator nsTextFrame::EnsureTextRun(
    TextRunType aWhichTextRun, DrawTarget* aRefDrawTarget,
    nsIFrame* aLineContainer, const nsLineList::iterator* aLine,
    uint32_t* aFlowEndInTextRun) {
  gfxTextRun* textRun = GetTextRun(aWhichTextRun);
  if (!textRun || (aLine && (*aLine)->GetInvalidateTextRuns())) {
    RefPtr<DrawTarget> refDT = aRefDrawTarget;
    if (!refDT) {
      refDT = CreateReferenceDrawTarget(this);
    }
    if (refDT) {
      BuildTextRuns(refDT, this, aLineContainer, aLine, aWhichTextRun);
    }
    textRun = GetTextRun(aWhichTextRun);
    if (!textRun) {
      // A text run was not constructed for this frame. This is bad. The caller
      // will check mTextRun.
      return gfxSkipCharsIterator(gfxPlatform::GetPlatform()->EmptySkipChars(),
                                  0);
    }
    TabWidthStore* tabWidths = GetProperty(TabWidthProperty());
    if (tabWidths && tabWidths->mValidForContentOffset != GetContentOffset()) {
      RemoveProperty(TabWidthProperty());
    }
  }

  if (textRun->GetFlags2() & nsTextFrameUtils::Flags::IsSimpleFlow) {
    if (aFlowEndInTextRun) {
      *aFlowEndInTextRun = textRun->GetLength();
    }
    return gfxSkipCharsIterator(textRun->GetSkipChars(), 0, mContentOffset);
  }

  auto userData = static_cast<TextRunUserData*>(textRun->GetUserData());
  TextRunMappedFlow* userMappedFlows = GetMappedFlows(textRun);
  TextRunMappedFlow* flow =
      FindFlowForContent(userData, mContent, userMappedFlows);
  if (flow) {
    // Since textruns can only contain one flow for a given content element,
    // this must be our flow.
    uint32_t flowIndex = flow - userMappedFlows;
    userData->mLastFlowIndex = flowIndex;
    gfxSkipCharsIterator iter(textRun->GetSkipChars(),
                              flow->mDOMOffsetToBeforeTransformOffset,
                              mContentOffset);
    if (aFlowEndInTextRun) {
      if (flowIndex + 1 < userData->mMappedFlowCount) {
        gfxSkipCharsIterator end(textRun->GetSkipChars());
        *aFlowEndInTextRun = end.ConvertOriginalToSkipped(
            flow[1].mStartFrame->GetContentOffset() +
            flow[1].mDOMOffsetToBeforeTransformOffset);
      } else {
        *aFlowEndInTextRun = textRun->GetLength();
      }
    }
    return iter;
  }

  NS_ERROR("Can't find flow containing this frame???");
  return gfxSkipCharsIterator(gfxPlatform::GetPlatform()->EmptySkipChars(), 0);
}

static uint32_t GetEndOfTrimmedText(const nsTextFragment* aFrag,
                                    const nsStyleText* aStyleText,
                                    uint32_t aStart, uint32_t aEnd,
                                    gfxSkipCharsIterator* aIterator,
                                    bool aAllowHangingWS = false) {
  aIterator->SetSkippedOffset(aEnd);
  while (aIterator->GetSkippedOffset() > aStart) {
    aIterator->AdvanceSkipped(-1);
    if (!IsTrimmableSpace(aFrag, aIterator->GetOriginalOffset(), aStyleText,
                          aAllowHangingWS)) {
      return aIterator->GetSkippedOffset() + 1;
    }
  }
  return aStart;
}

nsTextFrame::TrimmedOffsets nsTextFrame::GetTrimmedOffsets(
    const nsTextFragment* aFrag, TrimmedOffsetFlags aFlags) const {
  NS_ASSERTION(mTextRun, "Need textrun here");
  if (!(aFlags & TrimmedOffsetFlags::NotPostReflow)) {
    // This should not be used during reflow. We need our TEXT_REFLOW_FLAGS
    // to be set correctly.  If our parent wasn't reflowed due to the frame
    // tree being too deep then the return value doesn't matter.
    NS_ASSERTION(
        !HasAnyStateBits(NS_FRAME_FIRST_REFLOW) ||
            GetParent()->HasAnyStateBits(NS_FRAME_TOO_DEEP_IN_FRAME_TREE),
        "Can only call this on frames that have been reflowed");
    NS_ASSERTION(!HasAnyStateBits(NS_FRAME_IN_REFLOW),
                 "Can only call this on frames that are not being reflowed");
  }

  TrimmedOffsets offsets = {GetContentOffset(), GetContentLength()};
  const nsStyleText* textStyle = StyleText();
  // Note that pre-line newlines should still allow us to trim spaces
  // for display
  if (textStyle->WhiteSpaceIsSignificant()) {
    return offsets;
  }

  if (!(aFlags & TrimmedOffsetFlags::NoTrimBefore) &&
      ((aFlags & TrimmedOffsetFlags::NotPostReflow) ||
       HasAnyStateBits(TEXT_START_OF_LINE))) {
    int32_t whitespaceCount =
        GetTrimmableWhitespaceCount(aFrag, offsets.mStart, offsets.mLength, 1);
    offsets.mStart += whitespaceCount;
    offsets.mLength -= whitespaceCount;
  }

  if (!(aFlags & TrimmedOffsetFlags::NoTrimAfter) &&
      ((aFlags & TrimmedOffsetFlags::NotPostReflow) ||
       HasAnyStateBits(TEXT_END_OF_LINE))) {
    // This treats a trailing 'pre-line' newline as trimmable. That's fine,
    // it's actually what we want since we want whitespace before it to
    // be trimmed.
    int32_t whitespaceCount = GetTrimmableWhitespaceCount(
        aFrag, offsets.GetEnd() - 1, offsets.mLength, -1);
    offsets.mLength -= whitespaceCount;
  }
  return offsets;
}

static bool IsJustifiableCharacter(const nsStyleText* aTextStyle,
                                   const nsTextFragment* aFrag, int32_t aPos,
                                   bool aLangIsCJ) {
  NS_ASSERTION(aPos >= 0, "negative position?!");

  StyleTextJustify justifyStyle = aTextStyle->mTextJustify;
  if (justifyStyle == StyleTextJustify::None) {
    return false;
  }

  const char16_t ch = aFrag->CharAt(AssertedCast<uint32_t>(aPos));
  if (ch == '\n' || ch == '\t' || ch == '\r') {
    return !aTextStyle->WhiteSpaceIsSignificant();
  }
  if (ch == ' ' || ch == CH_NBSP) {
    // Don't justify spaces that are combined with diacriticals
    if (!aFrag->Is2b()) {
      return true;
    }
    return !nsTextFrameUtils::IsSpaceCombiningSequenceTail(
        aFrag->Get2b() + aPos + 1, aFrag->GetLength() - (aPos + 1));
  }

  if (justifyStyle == StyleTextJustify::InterCharacter) {
    return true;
  } else if (justifyStyle == StyleTextJustify::InterWord) {
    return false;
  }

  // text-justify: auto
  if (ch < 0x2150u) {
    return false;
  }
  if (aLangIsCJ) {
    if (  // Number Forms, Arrows, Mathematical Operators
        (0x2150u <= ch && ch <= 0x22ffu) ||
        // Enclosed Alphanumerics
        (0x2460u <= ch && ch <= 0x24ffu) ||
        // Block Elements, Geometric Shapes, Miscellaneous Symbols, Dingbats
        (0x2580u <= ch && ch <= 0x27bfu) ||
        // Supplemental Arrows-A, Braille Patterns, Supplemental Arrows-B,
        // Miscellaneous Mathematical Symbols-B,
        // Supplemental Mathematical Operators, Miscellaneous Symbols and Arrows
        (0x27f0u <= ch && ch <= 0x2bffu) ||
        // CJK Radicals Supplement, CJK Radicals Supplement, Ideographic
        // Description Characters, CJK Symbols and Punctuation, Hiragana,
        // Katakana, Bopomofo
        (0x2e80u <= ch && ch <= 0x312fu) ||
        // Kanbun, Bopomofo Extended, Katakana Phonetic Extensions,
        // Enclosed CJK Letters and Months, CJK Compatibility,
        // CJK Unified Ideographs Extension A, Yijing Hexagram Symbols,
        // CJK Unified Ideographs, Yi Syllables, Yi Radicals
        (0x3190u <= ch && ch <= 0xabffu) ||
        // CJK Compatibility Ideographs
        (0xf900u <= ch && ch <= 0xfaffu) ||
        // Halfwidth and Fullwidth Forms (a part)
        (0xff5eu <= ch && ch <= 0xff9fu)) {
      return true;
    }
    if (NS_IS_HIGH_SURROGATE(ch)) {
      if (char32_t u = aFrag->ScalarValueAt(AssertedCast<uint32_t>(aPos))) {
        // CJK Unified Ideographs Extension B,
        // CJK Unified Ideographs Extension C,
        // CJK Unified Ideographs Extension D,
        // CJK Compatibility Ideographs Supplement
        if (0x20000u <= u && u <= 0x2ffffu) {
          return true;
        }
      }
    }
  }
  return false;
}

void nsTextFrame::ClearMetrics(ReflowOutput& aMetrics) {
  aMetrics.ClearSize();
  aMetrics.SetBlockStartAscent(0);
  mAscent = 0;

  AddStateBits(TEXT_NO_RENDERED_GLYPHS);
}

static int32_t FindChar(const nsTextFragment* frag, int32_t aOffset,
                        int32_t aLength, char16_t ch) {
  int32_t i = 0;
  if (frag->Is2b()) {
    const char16_t* str = frag->Get2b() + aOffset;
    for (; i < aLength; ++i) {
      if (*str == ch) {
        return i + aOffset;
      }
      ++str;
    }
  } else {
    if (uint16_t(ch) <= 0xFF) {
      const char* str = frag->Get1b() + aOffset;
      const void* p = memchr(str, ch, aLength);
      if (p) {
        return (static_cast<const char*>(p) - str) + aOffset;
      }
    }
  }
  return -1;
}

static bool IsChineseOrJapanese(const nsTextFrame* aFrame) {
  if (aFrame->ShouldSuppressLineBreak()) {
    // Always treat ruby as CJ language so that those characters can
    // be expanded properly even when surrounded by other language.
    return true;
  }

  nsAtom* language = aFrame->StyleFont()->mLanguage;
  if (!language) {
    return false;
  }
  return nsStyleUtil::MatchesLanguagePrefix(language, u"ja") ||
         nsStyleUtil::MatchesLanguagePrefix(language, u"zh");
}

#ifdef DEBUG
static bool IsInBounds(const gfxSkipCharsIterator& aStart,
                       int32_t aContentLength, gfxTextRun::Range aRange) {
  if (aStart.GetSkippedOffset() > aRange.start) {
    return false;
  }
  if (aContentLength == INT32_MAX) {
    return true;
  }
  gfxSkipCharsIterator iter(aStart);
  iter.AdvanceOriginal(aContentLength);
  return iter.GetSkippedOffset() >= aRange.end;
}
#endif

nsTextFrame::PropertyProvider::PropertyProvider(
    gfxTextRun* aTextRun, const nsStyleText* aTextStyle,
    const nsTextFragment* aFrag, nsTextFrame* aFrame,
    const gfxSkipCharsIterator& aStart, int32_t aLength,
    nsIFrame* aLineContainer, nscoord aOffsetFromBlockOriginForTabs,
    nsTextFrame::TextRunType aWhichTextRun, bool aAtStartOfLine)
    : mTextRun(aTextRun),
      mFontGroup(nullptr),
      mTextStyle(aTextStyle),
      mFrag(aFrag),
      mLineContainer(aLineContainer),
      mFrame(aFrame),
      mStart(aStart),
      mTempIterator(aStart),
      mTabWidths(nullptr),
      mTabWidthsAnalyzedLimit(0),
      mLength(aLength),
      mWordSpacing(WordSpacing(aFrame, mTextRun, *aTextStyle)),
      mLetterSpacing(LetterSpacing(aFrame, *aTextStyle)),
      mMinTabAdvance(-1.0),
      mHyphenWidth(-1),
      mOffsetFromBlockOriginForTabs(aOffsetFromBlockOriginForTabs),
      mJustificationArrayStart(0),
      mReflowing(true),
      mWhichTextRun(aWhichTextRun) {
  NS_ASSERTION(mStart.IsInitialized(), "Start not initialized?");
  if (aAtStartOfLine) {
    mStartOfLineOffset = mStart.GetSkippedOffset();
  }
}

nsTextFrame::PropertyProvider::PropertyProvider(
    nsTextFrame* aFrame, const gfxSkipCharsIterator& aStart,
    nsTextFrame::TextRunType aWhichTextRun, nsFontMetrics* aFontMetrics)
    : mTextRun(aFrame->GetTextRun(aWhichTextRun)),
      mFontGroup(nullptr),
      mFontMetrics(aFontMetrics),
      mTextStyle(aFrame->StyleText()),
      mFrag(aFrame->TextFragment()),
      mLineContainer(nullptr),
      mFrame(aFrame),
      mStart(aStart),
      mTempIterator(aStart),
      mTabWidths(nullptr),
      mTabWidthsAnalyzedLimit(0),
      mLength(aFrame->GetContentLength()),
      mWordSpacing(WordSpacing(aFrame, mTextRun, *mTextStyle)),
      mLetterSpacing(LetterSpacing(aFrame, *mTextStyle)),
      mMinTabAdvance(-1.0),
      mHyphenWidth(-1),
      mOffsetFromBlockOriginForTabs(0),
      mJustificationArrayStart(0),
      mReflowing(false),
      mWhichTextRun(aWhichTextRun) {
  NS_ASSERTION(mTextRun, "Textrun not initialized!");
}

gfx::ShapedTextFlags nsTextFrame::PropertyProvider::GetShapedTextFlags() const {
  return nsLayoutUtils::GetTextRunOrientFlagsForStyle(mFrame->Style());
}

already_AddRefed<DrawTarget> nsTextFrame::PropertyProvider::GetDrawTarget()
    const {
  return CreateReferenceDrawTarget(GetFrame());
}

gfxFloat nsTextFrame::PropertyProvider::MinTabAdvance() const {
  if (mMinTabAdvance < 0.0) {
    mMinTabAdvance = GetMinTabAdvanceAppUnits(mTextRun);
  }
  return mMinTabAdvance;
}

/**
 * Finds the offset of the first character of the cluster containing aPos
 */
static void FindClusterStart(const gfxTextRun* aTextRun, int32_t aOriginalStart,
                             gfxSkipCharsIterator* aPos) {
  while (aPos->GetOriginalOffset() > aOriginalStart) {
    if (aPos->IsOriginalCharSkipped() ||
        aTextRun->IsClusterStart(aPos->GetSkippedOffset())) {
      break;
    }
    aPos->AdvanceOriginal(-1);
  }
}

/**
 * Finds the offset of the last character of the cluster containing aPos.
 * If aAllowSplitLigature is false, we also check for a ligature-group
 * start.
 */
static void FindClusterEnd(const gfxTextRun* aTextRun, int32_t aOriginalEnd,
                           gfxSkipCharsIterator* aPos,
                           bool aAllowSplitLigature = true) {
  MOZ_ASSERT(aPos->GetOriginalOffset() < aOriginalEnd,
             "character outside string");

  aPos->AdvanceOriginal(1);
  while (aPos->GetOriginalOffset() < aOriginalEnd) {
    if (aPos->IsOriginalCharSkipped() ||
        (aTextRun->IsClusterStart(aPos->GetSkippedOffset()) &&
         (aAllowSplitLigature ||
          aTextRun->IsLigatureGroupStart(aPos->GetSkippedOffset())))) {
      break;
    }
    aPos->AdvanceOriginal(1);
  }
  aPos->AdvanceOriginal(-1);
}

// Get the line number of aFrame in the lines referenced by aLineIter, if
// known (returning -1 if we don't find it).
static int32_t GetFrameLineNum(nsIFrame* aFrame, nsILineIterator* aLineIter) {
  if (!aLineIter) {
    return -1;
  }
  int32_t n = aLineIter->FindLineContaining(aFrame);
  if (n >= 0) {
    return n;
  }
  // If we didn't find the frame directly, but its parent is an inline,
  // we want the line that the inline ancestor is on.
  nsIFrame* ancestor = aFrame->GetParent();
  while (ancestor && ancestor->IsInlineFrame()) {
    n = aLineIter->FindLineContaining(ancestor);
    if (n >= 0) {
      return n;
    }
    ancestor = ancestor->GetParent();
  }
  return -1;
}

// Get the position of the first preserved newline in aFrame, if any,
// returning -1 if none.
static int32_t FindFirstNewlinePosition(const nsTextFrame* aFrame) {
  MOZ_ASSERT(aFrame->StyleText()->NewlineIsSignificantStyle(),
             "how did the HasNewline flag get set?");
  const auto* textFragment = aFrame->TextFragment();
  for (auto i = aFrame->GetContentOffset(); i < aFrame->GetContentEnd(); ++i) {
    if (textFragment->CharAt(i) == '\n') {
      return i;
    }
  }
  return -1;
}

// Get the position of the last preserved tab in aFrame that is before the
// preserved newline at aNewlinePos.
// Passing -1 for aNewlinePos means there is no preserved newline, so we look
// for the last preserved tab in the whole content.
// Returns -1 if no such preserved tab is present.
static int32_t FindLastTabPositionBeforeNewline(const nsTextFrame* aFrame,
                                                int32_t aNewlinePos) {
  // We only call this if white-space is not being collapsed.
  MOZ_ASSERT(aFrame->StyleText()->WhiteSpaceIsSignificant(),
             "how did the HasTab flag get set?");
  const auto* textFragment = aFrame->TextFragment();
  // If a non-negative newline position was given, we only need to search the
  // text before that offset.
  for (auto i = aNewlinePos < 0 ? aFrame->GetContentEnd() : aNewlinePos;
       i > aFrame->GetContentOffset(); --i) {
    if (textFragment->CharAt(i - 1) == '\t') {
      return i;
    }
  }
  return -1;
}

// Look for preserved tab or newline in the given frame or its following
// siblings on the same line, to determine whether justification should be
// suppressed in order to avoid disrupting tab-stop positions.
// Returns the first such preserved whitespace char, or 0 if none found.
static char NextPreservedWhiteSpaceOnLine(nsIFrame* aSibling,
                                          nsILineIterator* aLineIter,
                                          int32_t aLineNum) {
  while (aSibling) {
    // If we find a <br>, treat it like a newline.
    if (aSibling->IsBrFrame()) {
      return '\n';
    }
    // If we've moved on to a later line, stop searching.
    if (GetFrameLineNum(aSibling, aLineIter) > aLineNum) {
      return 0;
    }
    // If we encounter an inline frame, recurse into it.
    if (aSibling->IsInlineFrame()) {
      auto* child = aSibling->PrincipalChildList().FirstChild();
      char result = NextPreservedWhiteSpaceOnLine(child, aLineIter, aLineNum);
      if (result) {
        return result;
      }
    }
    // If we have a text frame, and whitespace is not collapsed, we need to
    // check its contents.
    if (aSibling->IsTextFrame()) {
      const auto* textStyle = aSibling->StyleText();
      if (textStyle->WhiteSpaceOrNewlineIsSignificant()) {
        const auto* textFrame = static_cast<nsTextFrame*>(aSibling);
        const auto* textFragment = textFrame->TextFragment();
        for (auto i = textFrame->GetContentOffset();
             i < textFrame->GetContentEnd(); ++i) {
          const char16_t ch = textFragment->CharAt(i);
          if (ch == '\n' && textStyle->NewlineIsSignificantStyle()) {
            return '\n';
          }
          if (ch == '\t' && textStyle->WhiteSpaceIsSignificant()) {
            return '\t';
          }
        }
      }
    }
    aSibling = aSibling->GetNextSibling();
  }
  return 0;
}

static bool HasPreservedTabInFollowingSiblingOnLine(nsTextFrame* aFrame) {
  bool foundTab = false;

  nsIFrame* lineContainer = FindLineContainer(aFrame);
  nsILineIterator* iter = lineContainer->GetLineIterator();
  int32_t line = GetFrameLineNum(aFrame, iter);
  char ws = NextPreservedWhiteSpaceOnLine(aFrame->GetNextSibling(), iter, line);
  if (ws == '\t') {
    foundTab = true;
  } else if (!ws) {
    // Didn't find a preserved tab or newline in our siblings; if our parent
    // (and its parent, etc) is an inline, we need to look at their following
    // siblings, too, as long as they're on the same line.
    const nsIFrame* maybeInline = aFrame->GetParent();
    while (maybeInline && maybeInline->IsInlineFrame()) {
      ws = NextPreservedWhiteSpaceOnLine(maybeInline->GetNextSibling(), iter,
                                         line);
      if (ws == '\t') {
        foundTab = true;
        break;
      }
      if (ws == '\n') {
        break;
      }
      maybeInline = maybeInline->GetParent();
    }
  }

  // We called lineContainer->GetLineIterator() above, but we mustn't
  // allow a block frame to retain this iterator if we're currently in
  // reflow, as it will become invalid as the line list is reflowed.
  if (lineContainer->HasAnyStateBits(NS_FRAME_IN_REFLOW) &&
      lineContainer->IsBlockFrameOrSubclass()) {
    static_cast<nsBlockFrame*>(lineContainer)->ClearLineIterator();
  }

  return foundTab;
}

JustificationInfo nsTextFrame::PropertyProvider::ComputeJustification(
    Range aRange, nsTArray<JustificationAssignment>* aAssignments) {
  JustificationInfo info;

  // Horizontal-in-vertical frame is orthogonal to the line, so it
  // doesn't actually include any justification opportunity inside.
  // The spec says such frame should be treated as a U+FFFC. Since we
  // do not insert justification opportunities on the sides of that
  // character, the sides of this frame are not justifiable either.
  if (mFrame->Style()->IsTextCombined()) {
    return info;
  }

  int32_t lastTab = -1;
  if (StaticPrefs::layout_css_text_align_justify_only_after_last_tab()) {
    // If there is a preserved tab on the line, we don't apply justification
    // until we're past its position.
    if (mTextStyle->WhiteSpaceIsSignificant()) {
      // If there is a preserved newline within the text, we don't need to look
      // beyond this frame, as following frames will not be on the same line.
      int32_t newlinePos =
          (mTextRun->GetFlags2() & nsTextFrameUtils::Flags::HasNewline)
              ? FindFirstNewlinePosition(mFrame)
              : -1;
      if (newlinePos < 0) {
        // There's no preserved newline within this frame; if there's a tab
        // in a later sibling frame on the same line, we won't apply any
        // justification to this one.
        if (HasPreservedTabInFollowingSiblingOnLine(mFrame)) {
          return info;
        }
      }

      if (mTextRun->GetFlags2() & nsTextFrameUtils::Flags::HasTab) {
        // Find last tab character in the content; we won't justify anything
        // before that position, so that tab alignment remains correct.
        lastTab = FindLastTabPositionBeforeNewline(mFrame, newlinePos);
      }
    }
  }

  bool isCJ = IsChineseOrJapanese(mFrame);
  nsSkipCharsRunIterator run(
      mStart, nsSkipCharsRunIterator::LENGTH_INCLUDES_SKIPPED, aRange.Length());
  run.SetOriginalOffset(aRange.start);
  mJustificationArrayStart = run.GetSkippedOffset();

  nsTArray<JustificationAssignment> assignments;
  assignments.SetCapacity(aRange.Length());
  while (run.NextRun()) {
    uint32_t originalOffset = run.GetOriginalOffset();
    uint32_t skippedOffset = run.GetSkippedOffset();
    uint32_t length = run.GetRunLength();
    assignments.SetLength(skippedOffset + length - mJustificationArrayStart);

    gfxSkipCharsIterator iter = run.GetPos();
    for (uint32_t i = 0; i < length; ++i) {
      uint32_t offset = originalOffset + i;
      if (!IsJustifiableCharacter(mTextStyle, mFrag, offset, isCJ) ||
          (lastTab >= 0 && offset <= uint32_t(lastTab))) {
        continue;
      }

      iter.SetOriginalOffset(offset);

      FindClusterStart(mTextRun, originalOffset, &iter);
      uint32_t firstCharOffset = iter.GetSkippedOffset();
      uint32_t firstChar = firstCharOffset > mJustificationArrayStart
                               ? firstCharOffset - mJustificationArrayStart
                               : 0;
      if (!firstChar) {
        info.mIsStartJustifiable = true;
      } else {
        auto& assign = assignments[firstChar];
        auto& prevAssign = assignments[firstChar - 1];
        if (prevAssign.mGapsAtEnd) {
          prevAssign.mGapsAtEnd = 1;
          assign.mGapsAtStart = 1;
        } else {
          assign.mGapsAtStart = 2;
          info.mInnerOpportunities++;
        }
      }

      FindClusterEnd(mTextRun, originalOffset + length, &iter);
      uint32_t lastChar = iter.GetSkippedOffset() - mJustificationArrayStart;
      // Assign the two gaps temporary to the last char. If the next cluster is
      // justifiable as well, one of the gaps will be removed by code above.
      assignments[lastChar].mGapsAtEnd = 2;
      info.mInnerOpportunities++;

      // Skip the whole cluster
      i = iter.GetOriginalOffset() - originalOffset;
    }
  }

  if (!assignments.IsEmpty() && assignments.LastElement().mGapsAtEnd) {
    // We counted the expansion opportunity after the last character,
    // but it is not an inner opportunity.
    MOZ_ASSERT(info.mInnerOpportunities > 0);
    info.mInnerOpportunities--;
    info.mIsEndJustifiable = true;
  }

  if (aAssignments) {
    *aAssignments = std::move(assignments);
  }
  return info;
}

// aStart, aLength in transformed string offsets
void nsTextFrame::PropertyProvider::GetSpacing(Range aRange,
                                               Spacing* aSpacing) const {
  GetSpacingInternal(
      aRange, aSpacing,
      !(mTextRun->GetFlags2() & nsTextFrameUtils::Flags::HasTab));
}

static bool CanAddSpacingBefore(const gfxTextRun* aTextRun, uint32_t aOffset,
                                bool aNewlineIsSignificant) {
  const auto* g = aTextRun->GetCharacterGlyphs();
  MOZ_ASSERT(aOffset < aTextRun->GetLength());
  if (aNewlineIsSignificant && g[aOffset].CharIsNewline()) {
    return false;
  }
  if (!aOffset) {
    return true;
  }
  return g[aOffset].IsClusterStart() && g[aOffset].IsLigatureGroupStart() &&
         !g[aOffset - 1].CharIsFormattingControl() && !g[aOffset].CharIsTab();
}

static bool CanAddSpacingAfter(const gfxTextRun* aTextRun, uint32_t aOffset,
                               bool aNewlineIsSignificant) {
  const auto* g = aTextRun->GetCharacterGlyphs();
  MOZ_ASSERT(aOffset < aTextRun->GetLength());
  if (aNewlineIsSignificant && g[aOffset].CharIsNewline()) {
    return false;
  }
  if (aOffset + 1 >= aTextRun->GetLength()) {
    return true;
  }
  return g[aOffset + 1].IsClusterStart() &&
         g[aOffset + 1].IsLigatureGroupStart() &&
         !g[aOffset].CharIsFormattingControl() && !g[aOffset].CharIsTab();
}

static gfxFloat ComputeTabWidthAppUnits(const nsIFrame* aFrame) {
  const auto& tabSize = aFrame->StyleText()->mTabSize;
  if (tabSize.IsLength()) {
    nscoord w = tabSize.length._0.ToAppUnits();
    MOZ_ASSERT(w >= 0);
    return w;
  }

  MOZ_ASSERT(tabSize.IsNumber());
  gfxFloat spaces = tabSize.number._0;
  MOZ_ASSERT(spaces >= 0);

  const nsIFrame* cb = aFrame->GetContainingBlock(0, aFrame->StyleDisplay());
  const auto* styleText = cb->StyleText();

  // Round the space width when converting to appunits the same way textruns do.
  // We don't use GetFirstFontMetrics here because that may return a font that
  // does not actually have the <space> character, yet is considered the "first
  // available font" per CSS Fonts. Here, we want the font that would be used
  // to render <space>, even if that means looking further down the font-family
  // list.
  RefPtr fm = nsLayoutUtils::GetFontMetricsForFrame(cb, 1.0f);
  bool vertical = cb->GetWritingMode().IsCentralBaseline();
  RefPtr font = fm->GetThebesFontGroup()->GetFirstValidFont(' ');
  auto metrics = font->GetMetrics(vertical ? nsFontMetrics::eVertical
                                           : nsFontMetrics::eHorizontal);
  nscoord spaceWidth = nscoord(
      NS_round(metrics.spaceWidth * cb->PresContext()->AppUnitsPerDevPixel()));
  return spaces *
         (spaceWidth + styleText->mLetterSpacing.Resolve(fm->EmHeight()) +
          styleText->mWordSpacing.Resolve(spaceWidth));
}

void nsTextFrame::PropertyProvider::GetSpacingInternal(Range aRange,
                                                       Spacing* aSpacing,
                                                       bool aIgnoreTabs) const {
  MOZ_ASSERT(IsInBounds(mStart, mLength, aRange), "Range out of bounds");

  uint32_t index;
  for (index = 0; index < aRange.Length(); ++index) {
    aSpacing[index].mBefore = 0.0;
    aSpacing[index].mAfter = 0.0;
  }

  if (mFrame->Style()->IsTextCombined()) {
    return;
  }

  // Find our offset into the original+transformed string
  gfxSkipCharsIterator start(mStart);
  start.SetSkippedOffset(aRange.start);

  // First, compute the word and letter spacing
  if (mWordSpacing || mLetterSpacing) {
    // Iterate over non-skipped characters
    nsSkipCharsRunIterator run(
        start, nsSkipCharsRunIterator::LENGTH_UNSKIPPED_ONLY, aRange.Length());
    bool newlineIsSignificant = mTextStyle->NewlineIsSignificant(mFrame);
    // Which letter-spacing model are we using?
    //   0 - Gecko legacy model, spacing added to trailing side of letter
    //   1 - WebKit/Blink-compatible, spacing added to right-hand side
    //   2 - Symmetrical spacing, half added to each side
    gfxFloat before, after;
    switch (StaticPrefs::layout_css_letter_spacing_model()) {
      default:  // use Gecko legacy behavior if pref value is unknown
      case 0:
        before = 0.0;
        after = mLetterSpacing;
        break;
      case 1:
        if (mTextRun->IsRightToLeft()) {
          before = mLetterSpacing;
          after = 0.0;
        } else {
          before = 0.0;
          after = mLetterSpacing;
        }
        break;
      case 2:
        before = mLetterSpacing / 2.0;
        after = mLetterSpacing - before;
        break;
    }
    bool atStart = mStartOfLineOffset == start.GetSkippedOffset() &&
                   !mFrame->IsInSVGTextSubtree();
    while (run.NextRun()) {
      uint32_t runOffsetInSubstring = run.GetSkippedOffset() - aRange.start;
      gfxSkipCharsIterator iter = run.GetPos();
      for (int32_t i = 0; i < run.GetRunLength(); ++i) {
        if (!atStart && before != 0.0 &&
            CanAddSpacingBefore(mTextRun, run.GetSkippedOffset() + i,
                                newlineIsSignificant)) {
          aSpacing[runOffsetInSubstring + i].mBefore += before;
        }
        if (after != 0.0 &&
            CanAddSpacingAfter(mTextRun, run.GetSkippedOffset() + i,
                               newlineIsSignificant)) {
          // End of a cluster, not in a ligature: put letter-spacing after it
          aSpacing[runOffsetInSubstring + i].mAfter += after;
        }
        if (IsCSSWordSpacingSpace(mFrag, i + run.GetOriginalOffset(), mFrame,
                                  mTextStyle)) {
          // It kinda sucks, but space characters can be part of clusters,
          // and even still be whitespace (I think!)
          iter.SetSkippedOffset(run.GetSkippedOffset() + i);
          FindClusterEnd(mTextRun, run.GetOriginalOffset() + run.GetRunLength(),
                         &iter);
          uint32_t runOffset = iter.GetSkippedOffset() - aRange.start;
          aSpacing[runOffset].mAfter += mWordSpacing;
        }
        atStart = false;
      }
    }
  }

  // Now add tab spacing, if there is any
  if (!aIgnoreTabs) {
    gfxFloat tabWidth = ComputeTabWidthAppUnits(mFrame);
    if (tabWidth > 0) {
      CalcTabWidths(aRange, tabWidth);
      if (mTabWidths) {
        mTabWidths->ApplySpacing(aSpacing,
                                 aRange.start - mStart.GetSkippedOffset(),
                                 aRange.Length());
      }
    }
  }

  // Now add in justification spacing
  if (mJustificationSpacings.Length() > 0) {
    // If there is any spaces trimmed at the end, aStart + aLength may
    // be larger than the flags array. When that happens, we can simply
    // ignore those spaces.
    auto arrayEnd = mJustificationArrayStart +
                    static_cast<uint32_t>(mJustificationSpacings.Length());
    auto end = std::min(aRange.end, arrayEnd);
    MOZ_ASSERT(aRange.start >= mJustificationArrayStart);
    for (auto i = aRange.start; i < end; i++) {
      const auto& spacing =
          mJustificationSpacings[i - mJustificationArrayStart];
      uint32_t offset = i - aRange.start;
      aSpacing[offset].mBefore += spacing.mBefore;
      aSpacing[offset].mAfter += spacing.mAfter;
    }
  }
}

// aX and the result are in whole appunits.
static gfxFloat AdvanceToNextTab(gfxFloat aX, gfxFloat aTabWidth,
                                 gfxFloat aMinAdvance) {
  // Advance aX to the next multiple of aTabWidth. We must advance
  // by at least aMinAdvance.
  gfxFloat nextPos = aX + aMinAdvance;
  return aTabWidth > 0.0 ? ceil(nextPos / aTabWidth) * aTabWidth : nextPos;
}

void nsTextFrame::PropertyProvider::CalcTabWidths(Range aRange,
                                                  gfxFloat aTabWidth) const {
  MOZ_ASSERT(aTabWidth > 0);

  if (!mTabWidths) {
    if (mReflowing && !mLineContainer) {
      // Intrinsic width computation does its own tab processing. We
      // just don't do anything here.
      return;
    }
    if (!mReflowing) {
      mTabWidths = mFrame->GetProperty(TabWidthProperty());
#ifdef DEBUG
      // If we're not reflowing, we should have already computed the
      // tab widths; check that they're available as far as the last
      // tab character present (if any)
      for (uint32_t i = aRange.end; i > aRange.start; --i) {
        if (mTextRun->CharIsTab(i - 1)) {
          uint32_t startOffset = mStart.GetSkippedOffset();
          NS_ASSERTION(mTabWidths && mTabWidths->mLimit + startOffset >= i,
                       "Precomputed tab widths are missing!");
          break;
        }
      }
#endif
      return;
    }
  }

  uint32_t startOffset = mStart.GetSkippedOffset();
  MOZ_ASSERT(aRange.start >= startOffset, "wrong start offset");
  MOZ_ASSERT(aRange.end <= startOffset + mLength, "beyond the end");
  uint32_t tabsEnd =
      (mTabWidths ? mTabWidths->mLimit : mTabWidthsAnalyzedLimit) + startOffset;
  if (tabsEnd < aRange.end) {
    NS_ASSERTION(mReflowing,
                 "We need precomputed tab widths, but don't have enough.");

    for (uint32_t i = tabsEnd; i < aRange.end; ++i) {
      Spacing spacing;
      GetSpacingInternal(Range(i, i + 1), &spacing, true);
      mOffsetFromBlockOriginForTabs += spacing.mBefore;

      if (!mTextRun->CharIsTab(i)) {
        if (mTextRun->IsClusterStart(i)) {
          uint32_t clusterEnd = i + 1;
          while (clusterEnd < mTextRun->GetLength() &&
                 !mTextRun->IsClusterStart(clusterEnd)) {
            ++clusterEnd;
          }
          mOffsetFromBlockOriginForTabs +=
              mTextRun->GetAdvanceWidth(Range(i, clusterEnd), nullptr);
        }
      } else {
        if (!mTabWidths) {
          mTabWidths = new TabWidthStore(mFrame->GetContentOffset());
          mFrame->SetProperty(TabWidthProperty(), mTabWidths);
        }
        double nextTab = AdvanceToNextTab(mOffsetFromBlockOriginForTabs,
                                          aTabWidth, MinTabAdvance());
        mTabWidths->mWidths.AppendElement(
            TabWidth(i - startOffset,
                     NSToIntRound(nextTab - mOffsetFromBlockOriginForTabs)));
        mOffsetFromBlockOriginForTabs = nextTab;
      }

      mOffsetFromBlockOriginForTabs += spacing.mAfter;
    }

    if (mTabWidths) {
      mTabWidths->mLimit = aRange.end - startOffset;
    }
  }

  if (!mTabWidths) {
    // Delete any stale property that may be left on the frame
    mFrame->RemoveProperty(TabWidthProperty());
    mTabWidthsAnalyzedLimit =
        std::max(mTabWidthsAnalyzedLimit, aRange.end - startOffset);
  }
}

gfxFloat nsTextFrame::PropertyProvider::GetHyphenWidth() const {
  if (mHyphenWidth < 0) {
    const auto& hyphenateChar = mTextStyle->mHyphenateCharacter;
    if (hyphenateChar.IsAuto()) {
      mHyphenWidth = GetFontGroup()->GetHyphenWidth(this);
    } else {
      RefPtr<gfxTextRun> hyphRun = GetHyphenTextRun(mFrame, nullptr);
      mHyphenWidth = hyphRun ? hyphRun->GetAdvanceWidth() : 0;
    }
  }
  return mHyphenWidth + mLetterSpacing;
}

static inline bool IS_HYPHEN(char16_t u) {
  return u == char16_t('-') ||  // HYPHEN-MINUS
         u == 0x058A ||         // ARMENIAN HYPHEN
         u == 0x2010 ||         // HYPHEN
         u == 0x2012 ||         // FIGURE DASH
         u == 0x2013;           // EN DASH
}

void nsTextFrame::PropertyProvider::GetHyphenationBreaks(
    Range aRange, HyphenType* aBreakBefore) const {
  MOZ_ASSERT(IsInBounds(mStart, mLength, aRange), "Range out of bounds");
  MOZ_ASSERT(mLength != INT32_MAX, "Can't call this with undefined length");

  if (!mTextStyle->WhiteSpaceCanWrap(mFrame) ||
      mTextStyle->mHyphens == StyleHyphens::None) {
    memset(aBreakBefore, static_cast<uint8_t>(HyphenType::None),
           aRange.Length() * sizeof(HyphenType));
    return;
  }

  // Iterate through the original-string character runs
  nsSkipCharsRunIterator run(
      mStart, nsSkipCharsRunIterator::LENGTH_UNSKIPPED_ONLY, aRange.Length());
  run.SetSkippedOffset(aRange.start);
  // We need to visit skipped characters so that we can detect SHY
  run.SetVisitSkipped();

  int32_t prevTrailingCharOffset = run.GetPos().GetOriginalOffset() - 1;
  bool allowHyphenBreakBeforeNextChar =
      prevTrailingCharOffset >= mStart.GetOriginalOffset() &&
      prevTrailingCharOffset < mStart.GetOriginalOffset() + mLength &&
      mFrag->CharAt(AssertedCast<uint32_t>(prevTrailingCharOffset)) == CH_SHY;

  while (run.NextRun()) {
    NS_ASSERTION(run.GetRunLength() > 0, "Shouldn't return zero-length runs");
    if (run.IsSkipped()) {
      // Check if there's a soft hyphen which would let us hyphenate before
      // the next non-skipped character. Don't look at soft hyphens followed
      // by other skipped characters, we won't use them.
      allowHyphenBreakBeforeNextChar =
          mFrag->CharAt(AssertedCast<uint32_t>(
              run.GetOriginalOffset() + run.GetRunLength() - 1)) == CH_SHY;
    } else {
      int32_t runOffsetInSubstring = run.GetSkippedOffset() - aRange.start;
      memset(aBreakBefore + runOffsetInSubstring,
             static_cast<uint8_t>(HyphenType::None),
             run.GetRunLength() * sizeof(HyphenType));
      // Don't allow hyphen breaks at the start of the line
      aBreakBefore[runOffsetInSubstring] =
          allowHyphenBreakBeforeNextChar &&
                  (!mFrame->HasAnyStateBits(TEXT_START_OF_LINE) ||
                   run.GetSkippedOffset() > mStart.GetSkippedOffset())
              ? HyphenType::Soft
              : HyphenType::None;
      allowHyphenBreakBeforeNextChar = false;
    }
  }

  if (mTextStyle->mHyphens == StyleHyphens::Auto) {
    gfxSkipCharsIterator skipIter(mStart);
    for (uint32_t i = 0; i < aRange.Length(); ++i) {
      if (IS_HYPHEN(mFrag->CharAt(AssertedCast<uint32_t>(
              skipIter.ConvertSkippedToOriginal(aRange.start + i))))) {
        if (i < aRange.Length() - 1) {
          aBreakBefore[i + 1] = HyphenType::Explicit;
        }
        continue;
      }

      if (mTextRun->CanHyphenateBefore(aRange.start + i) &&
          aBreakBefore[i] == HyphenType::None) {
        aBreakBefore[i] = HyphenType::AutoWithoutManualInSameWord;
      }
    }
  }
}

void nsTextFrame::PropertyProvider::InitializeForDisplay(bool aTrimAfter) {
  nsTextFrame::TrimmedOffsets trimmed = mFrame->GetTrimmedOffsets(
      mFrag, (aTrimAfter ? nsTextFrame::TrimmedOffsetFlags::Default
                         : nsTextFrame::TrimmedOffsetFlags::NoTrimAfter));
  mStart.SetOriginalOffset(trimmed.mStart);
  mLength = trimmed.mLength;
  if (mFrame->HasAnyStateBits(TEXT_START_OF_LINE)) {
    mStartOfLineOffset = mStart.GetSkippedOffset();
  }
  SetupJustificationSpacing(true);
}

void nsTextFrame::PropertyProvider::InitializeForMeasure() {
  nsTextFrame::TrimmedOffsets trimmed = mFrame->GetTrimmedOffsets(
      mFrag, nsTextFrame::TrimmedOffsetFlags::NotPostReflow);
  mStart.SetOriginalOffset(trimmed.mStart);
  mLength = trimmed.mLength;
  if (mFrame->HasAnyStateBits(TEXT_START_OF_LINE)) {
    mStartOfLineOffset = mStart.GetSkippedOffset();
  }
  SetupJustificationSpacing(false);
}

void nsTextFrame::PropertyProvider::SetupJustificationSpacing(
    bool aPostReflow) {
  MOZ_ASSERT(mLength != INT32_MAX, "Can't call this with undefined length");

  if (!mFrame->HasAnyStateBits(TEXT_JUSTIFICATION_ENABLED)) {
    return;
  }

  gfxSkipCharsIterator start(mStart), end(mStart);
  // We can't just use our mLength here; when InitializeForDisplay is
  // called with false for aTrimAfter, we still shouldn't be assigning
  // justification space to any trailing whitespace.
  nsTextFrame::TrimmedOffsets trimmed = mFrame->GetTrimmedOffsets(
      mFrag, (aPostReflow ? nsTextFrame::TrimmedOffsetFlags::Default
                          : nsTextFrame::TrimmedOffsetFlags::NotPostReflow));
  end.AdvanceOriginal(trimmed.mLength);
  gfxSkipCharsIterator realEnd(end);

  Range range(uint32_t(start.GetOriginalOffset()),
              uint32_t(end.GetOriginalOffset()));
  nsTArray<JustificationAssignment> assignments;
  JustificationInfo info = ComputeJustification(range, &assignments);

  auto assign = mFrame->GetJustificationAssignment();
  auto totalGaps = JustificationUtils::CountGaps(info, assign);
  if (!totalGaps || assignments.IsEmpty()) {
    // Nothing to do, nothing is justifiable and we shouldn't have any
    // justification space assigned
    return;
  }

  // Remember that textrun measurements are in the run's orientation,
  // so its advance "width" is actually a height in vertical writing modes,
  // corresponding to the inline-direction of the frame.
  gfxFloat naturalWidth = mTextRun->GetAdvanceWidth(
      Range(mStart.GetSkippedOffset(), realEnd.GetSkippedOffset()), this);
  if (mFrame->HasAnyStateBits(TEXT_HYPHEN_BREAK)) {
    naturalWidth += GetHyphenWidth();
  }
  nscoord totalSpacing = mFrame->ISize() - naturalWidth;
  if (totalSpacing <= 0) {
    // No space available
    return;
  }

  assignments[0].mGapsAtStart = assign.mGapsAtStart;
  assignments.LastElement().mGapsAtEnd = assign.mGapsAtEnd;

  MOZ_ASSERT(mJustificationSpacings.IsEmpty());
  JustificationApplicationState state(totalGaps, totalSpacing);
  mJustificationSpacings.SetCapacity(assignments.Length());
  for (const JustificationAssignment& assign : assignments) {
    Spacing* spacing = mJustificationSpacings.AppendElement();
    spacing->mBefore = state.Consume(assign.mGapsAtStart);
    spacing->mAfter = state.Consume(assign.mGapsAtEnd);
  }
}

void nsTextFrame::PropertyProvider::InitFontGroupAndFontMetrics() const {
  if (!mFontMetrics) {
    if (mWhichTextRun == nsTextFrame::eInflated) {
      mFontMetrics = mFrame->InflatedFontMetrics();
    } else {
      mFontMetrics = nsLayoutUtils::GetFontMetricsForFrame(mFrame, 1.0f);
    }
  }
  mFontGroup = mFontMetrics->GetThebesFontGroup();
}

#ifdef ACCESSIBILITY
a11y::AccType nsTextFrame::AccessibleType() {
  if (IsEmpty()) {
    RenderedText text =
        GetRenderedText(0, UINT32_MAX, TextOffsetType::OffsetsInContentText,
                        TrailingWhitespace::DontTrim);
    if (text.mString.IsEmpty()) {
      return a11y::eNoType;
    }
  }

  return a11y::eTextLeafType;
}
#endif

//-----------------------------------------------------------------------------
void nsTextFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                       nsIFrame* aPrevInFlow) {
  NS_ASSERTION(!aPrevInFlow, "Can't be a continuation!");
  MOZ_ASSERT(aContent->IsText(), "Bogus content!");

  // Remove any NewlineOffsetProperty or InFlowContentLengthProperty since they
  // might be invalid if the content was modified while there was no frame
  if (aContent->HasFlag(NS_HAS_NEWLINE_PROPERTY)) {
    aContent->RemoveProperty(nsGkAtoms::newline);
    aContent->UnsetFlags(NS_HAS_NEWLINE_PROPERTY);
  }
  if (aContent->HasFlag(NS_HAS_FLOWLENGTH_PROPERTY)) {
    aContent->RemoveProperty(nsGkAtoms::flowlength);
    aContent->UnsetFlags(NS_HAS_FLOWLENGTH_PROPERTY);
  }

  // Since our content has a frame now, this flag is no longer needed.
  aContent->UnsetFlags(NS_CREATE_FRAME_IF_NON_WHITESPACE);

  // We're not a continuing frame.
  // mContentOffset = 0; not necessary since we get zeroed out at init
  nsIFrame::Init(aContent, aParent, aPrevInFlow);
}

void nsTextFrame::ClearFrameOffsetCache() {
  // See if we need to remove ourselves from the offset cache
  if (HasAnyStateBits(TEXT_IN_OFFSET_CACHE)) {
    nsIFrame* primaryFrame = mContent->GetPrimaryFrame();
    if (primaryFrame) {
      // The primary frame might be null here.  For example,
      // nsLineBox::DeleteLineList just destroys the frames in order, which
      // means that the primary frame is already dead if we're a continuing text
      // frame, in which case, all of its properties are gone, and we don't need
      // to worry about deleting this property here.
      primaryFrame->RemoveProperty(OffsetToFrameProperty());
    }
    RemoveStateBits(TEXT_IN_OFFSET_CACHE);
  }
}

void nsTextFrame::Destroy(DestroyContext& aContext) {
  ClearFrameOffsetCache();

  // We might want to clear NS_CREATE_FRAME_IF_NON_WHITESPACE or
  // NS_REFRAME_IF_WHITESPACE on mContent here, since our parent frame
  // type might be changing.  Not clear whether it's worth it.
  ClearTextRuns();
  if (mNextContinuation) {
    mNextContinuation->SetPrevInFlow(nullptr);
  }
  // Let the base class destroy the frame
  nsIFrame::Destroy(aContext);
}

nsTArray<nsTextFrame*>* nsTextFrame::GetContinuations() {
  MOZ_ASSERT(NS_IsMainThread());
  // Only for use on the primary frame, which has no prev-continuation.
  MOZ_ASSERT(!GetPrevContinuation());
  if (!mNextContinuation) {
    return nullptr;
  }
  if (mPropertyFlags & PropertyFlags::Continuations) {
    return GetProperty(ContinuationsProperty());
  }
  size_t count = 0;
  for (nsIFrame* f = this; f; f = f->GetNextContinuation()) {
    ++count;
  }
  auto* continuations = new nsTArray<nsTextFrame*>;
  if (continuations->SetCapacity(count, fallible)) {
    for (nsTextFrame* f = this; f;
         f = static_cast<nsTextFrame*>(f->GetNextContinuation())) {
      continuations->AppendElement(f);
    }
  } else {
    delete continuations;
    continuations = nullptr;
  }
  AddProperty(ContinuationsProperty(), continuations);
  mPropertyFlags |= PropertyFlags::Continuations;
  return continuations;
}

class nsContinuingTextFrame final : public nsTextFrame {
 public:
  NS_DECL_FRAMEARENA_HELPERS(nsContinuingTextFrame)

  friend nsIFrame* NS_NewContinuingTextFrame(mozilla::PresShell* aPresShell,
                                             ComputedStyle* aStyle);

  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) final;

  void Destroy(DestroyContext&) override;

  nsTextFrame* GetPrevContinuation() const final { return mPrevContinuation; }

  void SetPrevContinuation(nsIFrame* aPrevContinuation) final {
    NS_ASSERTION(!aPrevContinuation || Type() == aPrevContinuation->Type(),
                 "setting a prev continuation with incorrect type!");
    NS_ASSERTION(
        !nsSplittableFrame::IsInPrevContinuationChain(aPrevContinuation, this),
        "creating a loop in continuation chain!");
    mPrevContinuation = static_cast<nsTextFrame*>(aPrevContinuation);
    RemoveStateBits(NS_FRAME_IS_FLUID_CONTINUATION);
    UpdateCachedContinuations();
  }

  nsTextFrame* GetPrevInFlow() const final {
    return HasAnyStateBits(NS_FRAME_IS_FLUID_CONTINUATION) ? mPrevContinuation
                                                           : nullptr;
  }

  void SetPrevInFlow(nsIFrame* aPrevInFlow) final {
    NS_ASSERTION(!aPrevInFlow || Type() == aPrevInFlow->Type(),
                 "setting a prev in flow with incorrect type!");
    NS_ASSERTION(
        !nsSplittableFrame::IsInPrevContinuationChain(aPrevInFlow, this),
        "creating a loop in continuation chain!");
    mPrevContinuation = static_cast<nsTextFrame*>(aPrevInFlow);
    AddStateBits(NS_FRAME_IS_FLUID_CONTINUATION);
    UpdateCachedContinuations();
  }

  // Call this helper to update cache after mPrevContinuation is changed.
  void UpdateCachedContinuations() {
    nsTextFrame* prevFirst = mFirstContinuation;
    if (mPrevContinuation) {
      mFirstContinuation = mPrevContinuation->FirstContinuation();
      if (mFirstContinuation) {
        mFirstContinuation->ClearCachedContinuations();
      }
    } else {
      mFirstContinuation = nullptr;
    }
    if (mFirstContinuation != prevFirst) {
      if (prevFirst) {
        prevFirst->ClearCachedContinuations();
      }
      auto* f = static_cast<nsContinuingTextFrame*>(mNextContinuation);
      while (f) {
        f->mFirstContinuation = mFirstContinuation;
        f = static_cast<nsContinuingTextFrame*>(f->mNextContinuation);
      }
    }
  }

  nsIFrame* FirstInFlow() const final;
  nsTextFrame* FirstContinuation() const final {
#if DEBUG
    // If we have a prev-continuation pointer, then our first-continuation
    // must be the same as that frame's.
    if (mPrevContinuation) {
      // If there's a prev-prev, then we can safely cast mPrevContinuation to
      // an nsContinuingTextFrame and access its mFirstContinuation pointer
      // directly, to avoid recursively calling FirstContinuation(), leading
      // to exponentially-slow behavior in the assertion.
      if (mPrevContinuation->GetPrevContinuation()) {
        auto* prev = static_cast<nsContinuingTextFrame*>(mPrevContinuation);
        MOZ_ASSERT(mFirstContinuation == prev->mFirstContinuation);
      } else {
        MOZ_ASSERT(mFirstContinuation ==
                   mPrevContinuation->FirstContinuation());
      }
    } else {
      MOZ_ASSERT(!mFirstContinuation);
    }
#endif
    return mFirstContinuation;
  };

  void AddInlineMinISize(const IntrinsicSizeInput& aInput,
                         InlineMinISizeData* aData) final {
    // Do nothing, since the first-in-flow accounts for everything.
  }
  void AddInlinePrefISize(const IntrinsicSizeInput& aInput,
                          InlinePrefISizeData* aData) final {
    // Do nothing, since the first-in-flow accounts for everything.
  }

 protected:
  explicit nsContinuingTextFrame(ComputedStyle* aStyle,
                                 nsPresContext* aPresContext)
      : nsTextFrame(aStyle, aPresContext, kClassID) {}

  nsTextFrame* mPrevContinuation = nullptr;
  nsTextFrame* mFirstContinuation = nullptr;
};

void nsContinuingTextFrame::Init(nsIContent* aContent,
                                 nsContainerFrame* aParent,
                                 nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aPrevInFlow, "Must be a continuation!");

  // Hook the frame into the flow
  nsTextFrame* prev = static_cast<nsTextFrame*>(aPrevInFlow);
  nsTextFrame* nextContinuation = prev->GetNextContinuation();
  SetPrevInFlow(aPrevInFlow);
  aPrevInFlow->SetNextInFlow(this);

  // NOTE: bypassing nsTextFrame::Init!!!
  nsIFrame::Init(aContent, aParent, aPrevInFlow);

  mContentOffset = prev->GetContentOffset() + prev->GetContentLengthHint();
  NS_ASSERTION(mContentOffset < int32_t(aContent->GetText()->GetLength()),
               "Creating ContinuingTextFrame, but there is no more content");
  if (prev->Style() != Style()) {
    // We're taking part of prev's text, and its style may be different
    // so clear its textrun which may no longer be valid (and don't set ours)
    prev->ClearTextRuns();
  } else {
    float inflation = prev->GetFontSizeInflation();
    SetFontSizeInflation(inflation);
    mTextRun = prev->GetTextRun(nsTextFrame::eInflated);
    if (inflation != 1.0f) {
      gfxTextRun* uninflatedTextRun =
          prev->GetTextRun(nsTextFrame::eNotInflated);
      if (uninflatedTextRun) {
        SetTextRun(uninflatedTextRun, nsTextFrame::eNotInflated, 1.0f);
      }
    }
  }
  if (aPrevInFlow->HasAnyStateBits(NS_FRAME_IS_BIDI)) {
    FrameBidiData bidiData = aPrevInFlow->GetBidiData();
    bidiData.precedingControl = kBidiLevelNone;
    SetProperty(BidiDataProperty(), bidiData);

    if (nextContinuation) {
      SetNextContinuation(nextContinuation);
      nextContinuation->SetPrevContinuation(this);
      // Adjust next-continuations' content offset as needed.
      while (nextContinuation &&
             nextContinuation->GetContentOffset() < mContentOffset) {
#ifdef DEBUG
        FrameBidiData nextBidiData = nextContinuation->GetBidiData();
        NS_ASSERTION(bidiData.embeddingLevel == nextBidiData.embeddingLevel &&
                         bidiData.baseLevel == nextBidiData.baseLevel,
                     "stealing text from different type of BIDI continuation");
        MOZ_ASSERT(nextBidiData.precedingControl == kBidiLevelNone,
                   "There shouldn't be any virtual bidi formatting character "
                   "between continuations");
#endif
        nextContinuation->mContentOffset = mContentOffset;
        nextContinuation = nextContinuation->GetNextContinuation();
      }
    }
    AddStateBits(NS_FRAME_IS_BIDI);
  }  // prev frame is bidi
}

void nsContinuingTextFrame::Destroy(DestroyContext& aContext) {
  ClearFrameOffsetCache();

  // The text associated with this frame will become associated with our
  // prev-continuation. If that means the text has changed style, then
  // we need to wipe out the text run for the text.
  // Note that mPrevContinuation can be null if we're destroying the whole
  // frame chain from the start to the end.
  // If this frame is mentioned in the userData for a textrun (say
  // because there's a direction change at the start of this frame), then
  // we have to clear the textrun because we're going away and the
  // textrun had better not keep a dangling reference to us.
  if (IsInTextRunUserData() ||
      (mPrevContinuation && mPrevContinuation->Style() != Style())) {
    ClearTextRuns();
    // Clear the previous continuation's text run also, so that it can rebuild
    // the text run to include our text.
    if (mPrevContinuation) {
      mPrevContinuation->ClearTextRuns();
    }
  }
  nsSplittableFrame::RemoveFromFlow(this);
  // Let the base class destroy the frame
  nsIFrame::Destroy(aContext);
}

nsIFrame* nsContinuingTextFrame::FirstInFlow() const {
  // Can't cast to |nsContinuingTextFrame*| because the first one isn't.
  nsIFrame *firstInFlow,
      *previous = const_cast<nsIFrame*>(static_cast<const nsIFrame*>(this));
  do {
    firstInFlow = previous;
    previous = firstInFlow->GetPrevInFlow();
  } while (previous);
  MOZ_ASSERT(firstInFlow, "post-condition failed");
  return firstInFlow;
}

// XXX Do we want to do all the work for the first-in-flow or do the
// work for each part?  (Be careful of first-letter / first-line, though,
// especially first-line!)  Doing all the work on the first-in-flow has
// the advantage of avoiding the potential for incremental reflow bugs,
// but depends on our maintining the frame tree in reasonable ways even
// for edge cases (block-within-inline splits, nextBidi, etc.)

// XXX We really need to make :first-letter happen during frame
// construction.

nscoord nsTextFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                    IntrinsicISizeType aType) {
  return IntrinsicISizeFromInline(aInput, aType);
}

//----------------------------------------------------------------------

#if defined(DEBUG_rbs) || defined(DEBUG_bzbarsky)
static void VerifyNotDirty(nsFrameState state) {
  bool isZero = state & NS_FRAME_FIRST_REFLOW;
  bool isDirty = state & NS_FRAME_IS_DIRTY;
  if (!isZero && isDirty) {
    NS_WARNING("internal offsets may be out-of-sync");
  }
}
#  define DEBUG_VERIFY_NOT_DIRTY(state) VerifyNotDirty(state)
#else
#  define DEBUG_VERIFY_NOT_DIRTY(state)
#endif

nsIFrame* NS_NewTextFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new (aPresShell) nsTextFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsTextFrame)

nsIFrame* NS_NewContinuingTextFrame(PresShell* aPresShell,
                                    ComputedStyle* aStyle) {
  return new (aPresShell)
      nsContinuingTextFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsContinuingTextFrame)

nsTextFrame::~nsTextFrame() = default;

nsIFrame::Cursor nsTextFrame::GetCursor(const nsPoint& aPoint) {
  StyleCursorKind kind = StyleUI()->Cursor().keyword;
  if (kind == StyleCursorKind::Auto) {
    if (!IsSelectable(nullptr)) {
      kind = StyleCursorKind::Default;
    } else {
      kind = GetWritingMode().IsVertical() ? StyleCursorKind::VerticalText
                                           : StyleCursorKind::Text;
    }
  }
  return Cursor{kind, AllowCustomCursorImage::Yes};
}

nsTextFrame* nsTextFrame::LastInFlow() const {
  nsTextFrame* lastInFlow = const_cast<nsTextFrame*>(this);
  while (lastInFlow->GetNextInFlow()) {
    lastInFlow = lastInFlow->GetNextInFlow();
  }
  MOZ_ASSERT(lastInFlow, "post-condition failed");
  return lastInFlow;
}

nsTextFrame* nsTextFrame::LastContinuation() const {
  nsTextFrame* lastContinuation = const_cast<nsTextFrame*>(this);
  while (lastContinuation->mNextContinuation) {
    lastContinuation = lastContinuation->mNextContinuation;
  }
  MOZ_ASSERT(lastContinuation, "post-condition failed");
  return lastContinuation;
}

bool nsTextFrame::ShouldSuppressLineBreak() const {
  // If the parent frame of the text frame is ruby content box, it must
  // suppress line break inside. This check is necessary, because when
  // a whitespace is only contained by pseudo ruby frames, its style
  // context won't have SuppressLineBreak bit set.
  if (mozilla::RubyUtils::IsRubyContentBox(GetParent()->Type())) {
    return true;
  }
  return Style()->ShouldSuppressLineBreak();
}

void nsTextFrame::InvalidateFrame(uint32_t aDisplayItemKey,
                                  bool aRebuildDisplayItems) {
  InvalidateSelectionState();

  if (IsInSVGTextSubtree()) {
    nsIFrame* svgTextFrame = nsLayoutUtils::GetClosestFrameOfType(
        GetParent(), LayoutFrameType::SVGText);
    svgTextFrame->InvalidateFrame();
    return;
  }
  nsIFrame::InvalidateFrame(aDisplayItemKey, aRebuildDisplayItems);
}

void nsTextFrame::InvalidateFrameWithRect(const nsRect& aRect,
                                          uint32_t aDisplayItemKey,
                                          bool aRebuildDisplayItems) {
  InvalidateSelectionState();

  if (IsInSVGTextSubtree()) {
    nsIFrame* svgTextFrame = nsLayoutUtils::GetClosestFrameOfType(
        GetParent(), LayoutFrameType::SVGText);
    svgTextFrame->InvalidateFrame();
    return;
  }
  nsIFrame::InvalidateFrameWithRect(aRect, aDisplayItemKey,
                                    aRebuildDisplayItems);
}

gfxTextRun* nsTextFrame::GetUninflatedTextRun() const {
  return GetProperty(UninflatedTextRunProperty());
}

void nsTextFrame::SetTextRun(gfxTextRun* aTextRun, TextRunType aWhichTextRun,
                             float aInflation) {
  NS_ASSERTION(aTextRun, "must have text run");

  // Our inflated text run is always stored in mTextRun.  In the cases
  // where our current inflation is not 1.0, however, we store two text
  // runs, and the uninflated one goes in a frame property.  We never
  // store a single text run in both.
  if (aWhichTextRun == eInflated) {
    if (HasFontSizeInflation() && aInflation == 1.0f) {
      // FIXME: Probably shouldn't do this within each SetTextRun
      // method, but it doesn't hurt.
      ClearTextRun(nullptr, nsTextFrame::eNotInflated);
    }
    SetFontSizeInflation(aInflation);
  } else {
    MOZ_ASSERT(aInflation == 1.0f, "unexpected inflation");
    if (HasFontSizeInflation()) {
      // Setting the property will not automatically increment the textrun's
      // reference count, so we need to do it here.
      aTextRun->AddRef();
      SetProperty(UninflatedTextRunProperty(), aTextRun);
      return;
    }
    // fall through to setting mTextRun
  }

  mTextRun = aTextRun;

  // FIXME: Add assertions testing the relationship between
  // GetFontSizeInflation() and whether we have an uninflated text run
  // (but be aware that text runs can go away).
}

bool nsTextFrame::RemoveTextRun(gfxTextRun* aTextRun) {
  if (aTextRun == mTextRun) {
    mTextRun = nullptr;
    mFontMetrics = nullptr;
    return true;
  }
  if (HasAnyStateBits(TEXT_HAS_FONT_INFLATION) &&
      GetProperty(UninflatedTextRunProperty()) == aTextRun) {
    RemoveProperty(UninflatedTextRunProperty());
    return true;
  }
  return false;
}

void nsTextFrame::ClearTextRun(nsTextFrame* aStartContinuation,
                               TextRunType aWhichTextRun) {
  RefPtr<gfxTextRun> textRun = GetTextRun(aWhichTextRun);
  if (!textRun) {
    return;
  }

  if (aWhichTextRun == nsTextFrame::eInflated) {
    mFontMetrics = nullptr;
  }

  DebugOnly<bool> checkmTextrun = textRun == mTextRun;
  UnhookTextRunFromFrames(textRun, aStartContinuation);
  MOZ_ASSERT(checkmTextrun ? !mTextRun
                           : !GetProperty(UninflatedTextRunProperty()));
}

void nsTextFrame::DisconnectTextRuns() {
  MOZ_ASSERT(!IsInTextRunUserData(),
             "Textrun mentions this frame in its user data so we can't just "
             "disconnect");
  mTextRun = nullptr;
  if (HasAnyStateBits(TEXT_HAS_FONT_INFLATION)) {
    RemoveProperty(UninflatedTextRunProperty());
  }
}

void nsTextFrame::NotifyNativeAnonymousTextnodeChange(uint32_t aOldLength) {
  MOZ_ASSERT(mContent->IsInNativeAnonymousSubtree());

  MarkIntrinsicISizesDirty();

  // This is to avoid making a new Reflow request in CharacterDataChanged:
  for (nsTextFrame* f = this; f; f = f->GetNextContinuation()) {
    f->MarkSubtreeDirty();
    f->mReflowRequestedForCharDataChange = true;
  }

  // Pretend that all the text changed.
  CharacterDataChangeInfo info;
  info.mAppend = false;
  info.mChangeStart = 0;
  info.mChangeEnd = aOldLength;
  info.mReplaceLength = GetContent()->TextLength();
  CharacterDataChanged(info);
}

nsresult nsTextFrame::CharacterDataChanged(
    const CharacterDataChangeInfo& aInfo) {
  if (mContent->HasFlag(NS_HAS_NEWLINE_PROPERTY)) {
    mContent->RemoveProperty(nsGkAtoms::newline);
    mContent->UnsetFlags(NS_HAS_NEWLINE_PROPERTY);
  }
  if (mContent->HasFlag(NS_HAS_FLOWLENGTH_PROPERTY)) {
    mContent->RemoveProperty(nsGkAtoms::flowlength);
    mContent->UnsetFlags(NS_HAS_FLOWLENGTH_PROPERTY);
  }

  // Find the first frame whose text has changed. Frames that are entirely
  // before the text change are completely unaffected.
  nsTextFrame* next;
  nsTextFrame* textFrame = this;
  while (true) {
    next = textFrame->GetNextContinuation();
    if (!next || next->GetContentOffset() > int32_t(aInfo.mChangeStart)) {
      break;
    }
    textFrame = next;
  }

  int32_t endOfChangedText = aInfo.mChangeStart + aInfo.mReplaceLength;

  // Parent of the last frame that we passed to FrameNeedsReflow (or noticed
  // had already received an earlier FrameNeedsReflow call).
  // (For subsequent frames with this same parent, we can just set their
  // dirty bit without bothering to call FrameNeedsReflow again.)
  nsIFrame* lastDirtiedFrameParent = nullptr;

  mozilla::PresShell* presShell = PresShell();
  do {
    // textFrame contained deleted text (or the insertion point,
    // if this was a pure insertion).
    textFrame->RemoveStateBits(TEXT_WHITESPACE_FLAGS);
    textFrame->ClearTextRuns();

    nsIFrame* parentOfTextFrame = textFrame->GetParent();
    bool areAncestorsAwareOfReflowRequest = false;
    if (lastDirtiedFrameParent == parentOfTextFrame) {
      // An earlier iteration of this loop already called
      // FrameNeedsReflow for a sibling of |textFrame|.
      areAncestorsAwareOfReflowRequest = true;
    } else {
      lastDirtiedFrameParent = parentOfTextFrame;
    }

    if (textFrame->mReflowRequestedForCharDataChange) {
      // We already requested a reflow for this frame; nothing to do.
      MOZ_ASSERT(textFrame->HasAnyStateBits(NS_FRAME_IS_DIRTY),
                 "mReflowRequestedForCharDataChange should only be set "
                 "on dirty frames");
    } else {
      // Make sure textFrame is queued up for a reflow.  Also set a flag so we
      // don't waste time doing this again in repeated calls to this method.
      textFrame->mReflowRequestedForCharDataChange = true;
      if (!areAncestorsAwareOfReflowRequest) {
        // Ask the parent frame to reflow me.
        presShell->FrameNeedsReflow(
            textFrame, IntrinsicDirty::FrameAncestorsAndDescendants,
            NS_FRAME_IS_DIRTY);
      } else {
        // We already called FrameNeedsReflow on behalf of an earlier sibling,
        // so we can just mark this frame as dirty and don't need to bother
        // telling its ancestors.
        // Note: if the parent is a block, we're cheating here because we should
        // be marking our line dirty, but we're not. nsTextFrame::SetLength will
        // do that when it gets called during reflow.
        textFrame->MarkSubtreeDirty();
      }
    }
    textFrame->InvalidateFrame();

    // Below, frames that start after the deleted text will be adjusted so that
    // their offsets move with the trailing unchanged text. If this change
    // deletes more text than it inserts, those frame offsets will decrease.
    // We need to maintain the invariant that mContentOffset is non-decreasing
    // along the continuation chain. So we need to ensure that frames that
    // started in the deleted text are all still starting before the
    // unchanged text.
    if (textFrame->mContentOffset > endOfChangedText) {
      textFrame->mContentOffset = endOfChangedText;
    }

    textFrame = textFrame->GetNextContinuation();
  } while (textFrame &&
           textFrame->GetContentOffset() < int32_t(aInfo.mChangeEnd));

  // This is how much the length of the string changed by --- i.e.,
  // how much the trailing unchanged text moved.
  int32_t sizeChange =
      aInfo.mChangeStart + aInfo.mReplaceLength - aInfo.mChangeEnd;

  if (sizeChange) {
    // Fix the offsets of the text frames that start in the trailing
    // unchanged text.
    while (textFrame) {
      textFrame->mContentOffset += sizeChange;
      // XXX we could rescue some text runs by adjusting their user data
      // to reflect the change in DOM offsets
      textFrame->ClearTextRuns();
      textFrame = textFrame->GetNextContinuation();
    }
  }

  return NS_OK;
}

NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(TextCombineScaleFactorProperty, float)

float nsTextFrame::GetTextCombineScaleFactor(nsTextFrame* aFrame) {
  float factor = aFrame->GetProperty(TextCombineScaleFactorProperty());
  return factor ? factor : 1.0f;
}

void nsTextFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                   const nsDisplayListSet& aLists) {
  if (!IsVisibleForPainting()) {
    return;
  }

  DO_GLOBAL_REFLOW_COUNT_DSP("nsTextFrame");

  const nsStyleText* st = StyleText();
  bool isTextTransparent =
      NS_GET_A(st->mWebkitTextFillColor.CalcColor(this)) == 0 &&
      NS_GET_A(st->mWebkitTextStrokeColor.CalcColor(this)) == 0;
  if ((HasAnyStateBits(TEXT_NO_RENDERED_GLYPHS) ||
       (isTextTransparent && !StyleText()->HasTextShadow())) &&
      aBuilder->IsForPainting() && !IsInSVGTextSubtree()) {
    if (!IsSelected()) {
      TextDecorations textDecs;
      GetTextDecorations(PresContext(), eResolvedColors, textDecs);
      if (!textDecs.HasDecorationLines()) {
        if (auto* currentPresContext = aBuilder->CurrentPresContext()) {
          currentPresContext->SetBuiltInvisibleText();
        }
        return;
      }
    }
  }

  aLists.Content()->AppendNewToTop<nsDisplayText>(aBuilder, this);
}

UniquePtr<SelectionDetails> nsTextFrame::GetSelectionDetails() {
  const nsFrameSelection* frameSelection = GetConstFrameSelection();
  if (frameSelection->IsInTableSelectionMode()) {
    return nullptr;
  }
  UniquePtr<SelectionDetails> details = frameSelection->LookUpSelection(
      mContent, GetContentOffset(), GetContentLength(), false);
  for (SelectionDetails* sd = details.get(); sd; sd = sd->mNext.get()) {
    sd->mStart += mContentOffset;
    sd->mEnd += mContentOffset;
  }
  return details;
}

static void PaintSelectionBackground(
    DrawTarget& aDrawTarget, nscolor aColor, const LayoutDeviceRect& aDirtyRect,
    const LayoutDeviceRect& aRect, nsTextFrame::DrawPathCallbacks* aCallbacks) {
  Rect rect = aRect.Intersect(aDirtyRect).ToUnknownRect();
  MaybeSnapToDevicePixels(rect, aDrawTarget);

  if (aCallbacks) {
    aCallbacks->NotifySelectionBackgroundNeedsFill(rect, aColor, aDrawTarget);
  } else {
    ColorPattern color(ToDeviceColor(aColor));
    aDrawTarget.FillRect(rect, color);
  }
}

// Attempt to get the LineBaselineOffset property of aChildFrame
// If not set, calculate this value for all child frames of aBlockFrame
static nscoord LazyGetLineBaselineOffset(nsIFrame* aChildFrame,
                                         nsBlockFrame* aBlockFrame) {
  bool offsetFound;
  nscoord offset =
      aChildFrame->GetProperty(nsIFrame::LineBaselineOffset(), &offsetFound);

  if (!offsetFound) {
    for (const auto& line : aBlockFrame->Lines()) {
      if (line.IsInline()) {
        int32_t n = line.GetChildCount();
        nscoord lineBaseline = line.BStart() + line.GetLogicalAscent();
        for (auto* lineFrame = line.mFirstChild; n > 0;
             lineFrame = lineFrame->GetNextSibling(), --n) {
          offset = lineBaseline - lineFrame->GetNormalPosition().y;
          lineFrame->SetProperty(nsIFrame::LineBaselineOffset(), offset);
        }
      }
    }
    return aChildFrame->GetProperty(nsIFrame::LineBaselineOffset(),
                                    &offsetFound);
  } else {
    return offset;
  }
}

static bool IsUnderlineRight(const ComputedStyle& aStyle) {
  // Check for 'left' or 'right' explicitly specified in the property;
  // if neither is there, we use auto positioning based on lang.
  const auto position = aStyle.StyleText()->mTextUnderlinePosition;
  if (position.IsLeft()) {
    return false;
  }
  if (position.IsRight()) {
    return true;
  }
  // If neither 'left' nor 'right' was specified, check the language.
  nsAtom* langAtom = aStyle.StyleFont()->mLanguage;
  if (!langAtom) {
    return false;
  }
  return nsStyleUtil::MatchesLanguagePrefix(langAtom, u"ja") ||
         nsStyleUtil::MatchesLanguagePrefix(langAtom, u"ko") ||
         nsStyleUtil::MatchesLanguagePrefix(langAtom, u"mn");
}

void nsTextFrame::GetTextDecorations(
    nsPresContext* aPresContext,
    nsTextFrame::TextDecorationColorResolution aColorResolution,
    nsTextFrame::TextDecorations& aDecorations) {
  const nsCompatibility compatMode = aPresContext->CompatibilityMode();

  bool useOverride = false;
  nscolor overrideColor = NS_RGBA(0, 0, 0, 0);

  bool nearestBlockFound = false;
  // Use writing mode of parent frame for orthogonal text frame to work.
  // See comment in nsTextFrame::DrawTextRunAndDecorations.
  WritingMode wm = GetParent()->GetWritingMode();
  bool vertical = wm.IsVertical();

  nscoord ascent = GetLogicalBaseline(wm);
  // physicalBlockStartOffset represents the offset from our baseline
  // to f's physical block start, which is top in horizontal writing
  // mode, and left in vertical writing modes, in our coordinate space.
  // This physical block start is logical block start in most cases,
  // but for vertical-rl, it is logical block end, and consequently in
  // that case, it starts from the descent instead of ascent.
  nscoord physicalBlockStartOffset =
      wm.IsVerticalRL() ? GetSize().width - ascent : ascent;
  // baselineOffset represents the offset from our baseline to f's baseline or
  // the nearest block's baseline, in our coordinate space, whichever is closest
  // during the particular iteration
  nscoord baselineOffset = 0;

  for (nsIFrame *f = this, *fChild = nullptr; f;
       fChild = f, f = nsLayoutUtils::GetParentOrPlaceholderFor(f)) {
    ComputedStyle* const context = f->Style();
    if (!context->HasTextDecorationLines()) {
      break;
    }

    if (context->GetPseudoType() == PseudoStyleType::marker &&
        (context->StyleList()->mListStylePosition ==
             StyleListStylePosition::Outside ||
         !context->StyleDisplay()->IsInlineOutsideStyle())) {
      // Outside ::marker pseudos, and inside markers that aren't inlines, don't
      // have text decorations.
      break;
    }

    const nsStyleTextReset* const styleTextReset = context->StyleTextReset();
    StyleTextDecorationLine textDecorations =
        styleTextReset->mTextDecorationLine;
    bool ignoreSubproperties = false;

    auto lineStyle = styleTextReset->mTextDecorationStyle;
    if (textDecorations == StyleTextDecorationLine::SPELLING_ERROR ||
        textDecorations == StyleTextDecorationLine::GRAMMAR_ERROR) {
      nscolor lineColor;
      float relativeSize;
      useOverride = nsTextPaintStyle::GetSelectionUnderline(
          this, nsTextPaintStyle::SelectionStyleIndex::SpellChecker, &lineColor,
          &relativeSize, &lineStyle);
      if (useOverride) {
        // We don't currently have a SelectionStyleIndex::GrammarChecker; for
        // now just use SpellChecker and change its color to green.
        overrideColor =
            textDecorations == StyleTextDecorationLine::SPELLING_ERROR
                ? lineColor
                : NS_RGBA(0, 128, 0, 255);
        textDecorations = StyleTextDecorationLine::UNDERLINE;
        ignoreSubproperties = true;
      }
    }

    if (!useOverride &&
        (StyleTextDecorationLine::COLOR_OVERRIDE & textDecorations)) {
      // This handles the <a href="blah.html"><font color="green">La
      // la la</font></a> case. The link underline should be green.
      useOverride = true;
      overrideColor = nsLayoutUtils::GetTextColor(
          f, &nsStyleTextReset::mTextDecorationColor);
    }

    nsBlockFrame* fBlock = do_QueryFrame(f);
    const bool firstBlock = !nearestBlockFound && fBlock;

    // Not updating positions once we hit a parent block is equivalent to
    // the CSS 2.1 spec that blocks should propagate decorations down to their
    // children (albeit the style should be preserved)
    // However, if we're vertically aligned within a block, then we need to
    // recover the correct baseline from the line by querying the FrameProperty
    // that should be set (see nsLineLayout::VerticalAlignLine).
    if (firstBlock) {
      // At this point, fChild can't be null since TextFrames can't be blocks
      Maybe<StyleVerticalAlignKeyword> verticalAlign =
          fChild->VerticalAlignEnum();
      if (verticalAlign != Some(StyleVerticalAlignKeyword::Baseline)) {
        // Since offset is the offset in the child's coordinate space, we have
        // to undo the accumulation to bring the transform out of the block's
        // coordinate space
        const nscoord lineBaselineOffset =
            LazyGetLineBaselineOffset(fChild, fBlock);

        baselineOffset = physicalBlockStartOffset - lineBaselineOffset -
                         (vertical ? fChild->GetNormalPosition().x
                                   : fChild->GetNormalPosition().y);
      }
    } else if (!nearestBlockFound) {
      // offset here is the offset from f's baseline to f's top/left
      // boundary. It's descent for vertical-rl, and ascent otherwise.
      nscoord offset = wm.IsVerticalRL()
                           ? f->GetSize().width - f->GetLogicalBaseline(wm)
                           : f->GetLogicalBaseline(wm);
      baselineOffset = physicalBlockStartOffset - offset;
    }

    nearestBlockFound = nearestBlockFound || firstBlock;
    physicalBlockStartOffset +=
        vertical ? f->GetNormalPosition().x : f->GetNormalPosition().y;

    if (textDecorations) {
      nscolor color;
      if (useOverride) {
        color = overrideColor;
      } else if (IsInSVGTextSubtree()) {
        // XXX We might want to do something with text-decoration-color when
        //     painting SVG text, but it's not clear what we should do.  We
        //     at least need SVG text decorations to paint with 'fill' if
        //     text-decoration-color has its initial value currentColor.
        //     We could choose to interpret currentColor as "currentFill"
        //     for SVG text, and have e.g. text-decoration-color:red to
        //     override the fill paint of the decoration.
        color = aColorResolution == eResolvedColors
                    ? nsLayoutUtils::GetTextColor(f, &nsStyleSVG::mFill)
                    : NS_SAME_AS_FOREGROUND_COLOR;
      } else {
        color = nsLayoutUtils::GetTextColor(
            f, &nsStyleTextReset::mTextDecorationColor);
      }

      bool swapUnderlineAndOverline =
          wm.IsCentralBaseline() && IsUnderlineRight(*context);
      const auto kUnderline = swapUnderlineAndOverline
                                  ? StyleTextDecorationLine::OVERLINE
                                  : StyleTextDecorationLine::UNDERLINE;
      const auto kOverline = swapUnderlineAndOverline
                                 ? StyleTextDecorationLine::UNDERLINE
                                 : StyleTextDecorationLine::OVERLINE;

      const nsStyleText* const styleText = context->StyleText();
      const auto position = ignoreSubproperties
                                ? StyleTextUnderlinePosition::AUTO
                                : styleText->mTextUnderlinePosition;
      const auto offset = ignoreSubproperties ? LengthPercentageOrAuto::Auto()
                                              : styleText->mTextUnderlineOffset;
      const auto thickness = ignoreSubproperties
                                 ? StyleTextDecorationLength::Auto()
                                 : styleTextReset->mTextDecorationThickness;

      if (textDecorations & kUnderline) {
        aDecorations.mUnderlines.AppendElement(nsTextFrame::LineDecoration(
            f, baselineOffset, position, offset, thickness, color, lineStyle,
            !ignoreSubproperties));
      }
      if (textDecorations & kOverline) {
        aDecorations.mOverlines.AppendElement(nsTextFrame::LineDecoration(
            f, baselineOffset, position, offset, thickness, color, lineStyle,
            !ignoreSubproperties));
      }
      if (textDecorations & StyleTextDecorationLine::LINE_THROUGH) {
        aDecorations.mStrikes.AppendElement(nsTextFrame::LineDecoration(
            f, baselineOffset, position, offset, thickness, color, lineStyle,
            !ignoreSubproperties));
      }
    }

    // In all modes, if we're on an inline-block/table/grid/flex (or
    // -moz-inline-box), we're done.
    // If we're on a ruby frame other than ruby text container, we
    // should continue.
    mozilla::StyleDisplay display = f->GetDisplay();
    if (!display.IsInlineFlow() &&
        (!display.IsRuby() ||
         display == mozilla::StyleDisplay::RubyTextContainer) &&
        display.IsInlineOutside()) {
      break;
    }

    // In quirks mode, if we're on an HTML table element, we're done.
    if (compatMode == eCompatibility_NavQuirks &&
        f->GetContent()->IsHTMLElement(nsGkAtoms::table)) {
      break;
    }

    // If we're on an absolutely-positioned element or a floating
    // element, we're done.
    if (f->IsFloating() || f->IsAbsolutelyPositioned()) {
      break;
    }

    // If we're an outer <svg> element, which is classified as an atomic
    // inline-level element, we're done.
    if (f->IsSVGOuterSVGFrame()) {
      break;
    }
  }
}

static float GetInflationForTextDecorations(nsIFrame* aFrame,
                                            nscoord aInflationMinFontSize) {
  if (aFrame->IsInSVGTextSubtree()) {
    auto* container =
        nsLayoutUtils::GetClosestFrameOfType(aFrame, LayoutFrameType::SVGText);
    MOZ_ASSERT(container);
    return static_cast<SVGTextFrame*>(container)->GetFontSizeScaleFactor();
  }
  return nsLayoutUtils::FontSizeInflationInner(aFrame, aInflationMinFontSize);
}

struct EmphasisMarkInfo {
  RefPtr<gfxTextRun> textRun;
  gfxFloat advance;
  gfxFloat baselineOffset;
};

NS_DECLARE_FRAME_PROPERTY_DELETABLE(EmphasisMarkProperty, EmphasisMarkInfo)

static void ComputeTextEmphasisStyleString(const StyleTextEmphasisStyle& aStyle,
                                           nsAString& aOut) {
  MOZ_ASSERT(!aStyle.IsNone());
  if (aStyle.IsString()) {
    nsDependentCSubstring string = aStyle.AsString().AsString();
    AppendUTF8toUTF16(string, aOut);
    return;
  }
  const auto& keyword = aStyle.AsKeyword();
  const bool fill = keyword.fill == StyleTextEmphasisFillMode::Filled;
  switch (keyword.shape) {
    case StyleTextEmphasisShapeKeyword::Dot:
      return aOut.AppendLiteral(fill ? u"\u2022" : u"\u25e6");
    case StyleTextEmphasisShapeKeyword::Circle:
      return aOut.AppendLiteral(fill ? u"\u25cf" : u"\u25cb");
    case StyleTextEmphasisShapeKeyword::DoubleCircle:
      return aOut.AppendLiteral(fill ? u"\u25c9" : u"\u25ce");
    case StyleTextEmphasisShapeKeyword::Triangle:
      return aOut.AppendLiteral(fill ? u"\u25b2" : u"\u25b3");
    case StyleTextEmphasisShapeKeyword::Sesame:
      return aOut.AppendLiteral(fill ? u"\ufe45" : u"\ufe46");
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown emphasis style shape");
  }
}

static already_AddRefed<gfxTextRun> GenerateTextRunForEmphasisMarks(
    nsTextFrame* aFrame, gfxFontGroup* aFontGroup,
    ComputedStyle* aComputedStyle, const nsStyleText* aStyleText) {
  nsAutoString string;
  ComputeTextEmphasisStyleString(aStyleText->mTextEmphasisStyle, string);

  RefPtr<DrawTarget> dt = CreateReferenceDrawTarget(aFrame);
  auto appUnitsPerDevUnit = aFrame->PresContext()->AppUnitsPerDevPixel();
  gfx::ShapedTextFlags flags =
      nsLayoutUtils::GetTextRunOrientFlagsForStyle(aComputedStyle);
  if (flags == gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_MIXED) {
    // The emphasis marks should always be rendered upright per spec.
    flags = gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT;
  }
  return aFontGroup->MakeTextRun<char16_t>(string.get(), string.Length(), dt,
                                           appUnitsPerDevUnit, flags,
                                           nsTextFrameUtils::Flags(), nullptr);
}

static nsRubyFrame* FindFurthestInlineRubyAncestor(nsTextFrame* aFrame) {
  nsRubyFrame* rubyFrame = nullptr;
  for (nsIFrame* frame = aFrame->GetParent();
       frame && frame->IsLineParticipant(); frame = frame->GetParent()) {
    if (frame->IsRubyFrame()) {
      rubyFrame = static_cast<nsRubyFrame*>(frame);
    }
  }
  return rubyFrame;
}

nsRect nsTextFrame::UpdateTextEmphasis(WritingMode aWM,
                                       PropertyProvider& aProvider) {
  const nsStyleText* styleText = StyleText();
  if (!styleText->HasEffectiveTextEmphasis()) {
    RemoveProperty(EmphasisMarkProperty());
    return nsRect();
  }

  ComputedStyle* computedStyle = Style();
  bool isTextCombined = computedStyle->IsTextCombined();
  if (isTextCombined) {
    computedStyle = GetParent()->Style();
  }
  RefPtr<nsFontMetrics> fm = nsLayoutUtils::GetFontMetricsOfEmphasisMarks(
      computedStyle, PresContext(), GetFontSizeInflation());
  EmphasisMarkInfo* info = new EmphasisMarkInfo;
  info->textRun = GenerateTextRunForEmphasisMarks(
      this, fm->GetThebesFontGroup(), computedStyle, styleText);
  info->advance = info->textRun->GetAdvanceWidth();

  // Calculate the baseline offset
  LogicalSide side = styleText->TextEmphasisSide(aWM, StyleFont()->mLanguage);
  LogicalSize frameSize = GetLogicalSize(aWM);
  // The overflow rect is inflated in the inline direction by half
  // advance of the emphasis mark on each side, so that even if a mark
  // is drawn for a zero-width character, it won't be clipped.
  LogicalRect overflowRect(aWM, -info->advance / 2,
                           /* BStart to be computed below */ 0,
                           frameSize.ISize(aWM) + info->advance,
                           fm->MaxAscent() + fm->MaxDescent());
  RefPtr<nsFontMetrics> baseFontMetrics =
      isTextCombined
          ? nsLayoutUtils::GetInflatedFontMetricsForFrame(GetParent())
          : do_AddRef(aProvider.GetFontMetrics());
  // When the writing mode is vertical-lr the line is inverted, and thus
  // the ascent and descent are swapped.
  nscoord absOffset = (side == LogicalSide::BStart) != aWM.IsLineInverted()
                          ? baseFontMetrics->MaxAscent() + fm->MaxDescent()
                          : baseFontMetrics->MaxDescent() + fm->MaxAscent();
  RubyBlockLeadings leadings;
  if (nsRubyFrame* ruby = FindFurthestInlineRubyAncestor(this)) {
    leadings = ruby->GetBlockLeadings();
  }
  if (side == LogicalSide::BStart) {
    info->baselineOffset = -absOffset - leadings.mStart;
    overflowRect.BStart(aWM) = -overflowRect.BSize(aWM) - leadings.mStart;
  } else {
    MOZ_ASSERT(side == LogicalSide::BEnd);
    info->baselineOffset = absOffset + leadings.mEnd;
    overflowRect.BStart(aWM) = frameSize.BSize(aWM) + leadings.mEnd;
  }
  // If text combined, fix the gap between the text frame and its parent.
  if (isTextCombined) {
    nscoord gap = (baseFontMetrics->MaxHeight() - frameSize.BSize(aWM)) / 2;
    overflowRect.BStart(aWM) += gap * (side == LogicalSide::BStart ? -1 : 1);
  }

  SetProperty(EmphasisMarkProperty(), info);
  return overflowRect.GetPhysicalRect(aWM, frameSize.GetPhysicalSize(aWM));
}

// helper function for implementing text-decoration-thickness
// https://drafts.csswg.org/css-text-decor-4/#text-decoration-width-property
// Returns the thickness in device pixels.
static gfxFloat ComputeDecorationLineThickness(
    const StyleTextDecorationLength& aThickness, const gfxFloat aAutoValue,
    const gfxFont::Metrics& aFontMetrics, const gfxFloat aAppUnitsPerDevPixel,
    const nsIFrame* aFrame) {
  if (aThickness.IsAuto()) {
    return aAutoValue;
  }

  if (aThickness.IsFromFont()) {
    return aFontMetrics.underlineSize;
  }
  auto em = [&] { return aFrame->StyleFont()->mSize.ToAppUnits(); };
  return aThickness.AsLengthPercentage().Resolve(em) / aAppUnitsPerDevPixel;
}

// Helper function for implementing text-underline-offset and -position
// https://drafts.csswg.org/css-text-decor-4/#underline-offset
// Returns the offset in device pixels.
static gfxFloat ComputeDecorationLineOffset(
    StyleTextDecorationLine aLineType,
    const StyleTextUnderlinePosition& aPosition,
    const LengthPercentageOrAuto& aOffset, const gfxFont::Metrics& aFontMetrics,
    const gfxFloat aAppUnitsPerDevPixel, const nsIFrame* aFrame,
    bool aIsCentralBaseline, bool aSwappedUnderline) {
  // Em value to use if we need to resolve a percentage length.
  auto em = [&] { return aFrame->StyleFont()->mSize.ToAppUnits(); };
  // If we're in vertical-upright typographic mode, we need to compute the
  // offset of the decoration line from the default central baseline.
  if (aIsCentralBaseline) {
    // Line-through simply goes at the (central) baseline.
    if (aLineType == StyleTextDecorationLine::LINE_THROUGH) {
      return 0;
    }

    // Compute "zero position" for the under- or overline.
    gfxFloat zeroPos = 0.5 * aFontMetrics.emHeight;

    // aOffset applies to underline only; for overline (or offset:auto) we use
    // a somewhat arbitrary offset of half the font's (horziontal-mode) value
    // for underline-offset, to get a little bit of separation between glyph
    // edges and the line in typical cases.
    // If we have swapped under-/overlines for text-underline-position:right,
    // we need to take account of this to determine which decoration lines are
    // "real" underlines which should respect the text-underline-* values.
    bool isUnderline =
        (aLineType == StyleTextDecorationLine::UNDERLINE) != aSwappedUnderline;
    gfxFloat offset =
        isUnderline && !aOffset.IsAuto()
            ? aOffset.AsLengthPercentage().Resolve(em) / aAppUnitsPerDevPixel
            : aFontMetrics.underlineOffset * -0.5;

    // Direction of the decoration line's offset from the central baseline.
    gfxFloat dir = aLineType == StyleTextDecorationLine::OVERLINE ? 1.0 : -1.0;
    return dir * (zeroPos + offset);
  }

  // Compute line offset for horizontal typographic mode.
  if (aLineType == StyleTextDecorationLine::UNDERLINE) {
    if (aPosition.IsFromFont()) {
      gfxFloat zeroPos = aFontMetrics.underlineOffset;
      gfxFloat offset =
          aOffset.IsAuto()
              ? 0
              : aOffset.AsLengthPercentage().Resolve(em) / aAppUnitsPerDevPixel;
      return zeroPos - offset;
    }

    if (aPosition.IsUnder()) {
      gfxFloat zeroPos = -aFontMetrics.maxDescent;
      gfxFloat offset =
          aOffset.IsAuto()
              ? -0.5 * aFontMetrics.underlineOffset
              : aOffset.AsLengthPercentage().Resolve(em) / aAppUnitsPerDevPixel;
      return zeroPos - offset;
    }

    // text-underline-position must be 'auto', so zero position is the
    // baseline and 'auto' offset will apply the font's underline-offset.
    //
    // If offset is `auto`, we clamp the offset (in horizontal typographic mode)
    // to a minimum of 1/16 em (equivalent to 1px at font-size 16px) to mitigate
    // skip-ink issues with fonts that leave the underlineOffset field as zero.
    MOZ_ASSERT(aPosition.IsAuto());
    return aOffset.IsAuto() ? std::min(aFontMetrics.underlineOffset,
                                       -aFontMetrics.emHeight / 16.0)
                            : -aOffset.AsLengthPercentage().Resolve(em) /
                                  aAppUnitsPerDevPixel;
  }

  if (aLineType == StyleTextDecorationLine::OVERLINE) {
    return aFontMetrics.maxAscent;
  }

  if (aLineType == StyleTextDecorationLine::LINE_THROUGH) {
    return aFontMetrics.strikeoutOffset;
  }

  MOZ_ASSERT_UNREACHABLE("unknown decoration line type");
  return 0;
}

void nsTextFrame::UnionAdditionalOverflow(nsPresContext* aPresContext,
                                          nsIFrame* aBlock,
                                          PropertyProvider& aProvider,
                                          nsRect* aInkOverflowRect,
                                          bool aIncludeTextDecorations,
                                          bool aIncludeShadows) {
  const WritingMode wm = GetWritingMode();
  bool verticalRun = mTextRun->IsVertical();
  const gfxFloat appUnitsPerDevUnit = aPresContext->AppUnitsPerDevPixel();

  if (IsFloatingFirstLetterChild()) {
    bool inverted = wm.IsLineInverted();
    // The underline/overline drawable area must be contained in the overflow
    // rect when this is in floating first letter frame at *both* modes.
    // In this case, aBlock is the ::first-letter frame.
    auto decorationStyle =
        aBlock->Style()->StyleTextReset()->mTextDecorationStyle;
    // If the style is none, let's include decoration line rect as solid style
    // since changing the style from none to solid/dotted/dashed doesn't cause
    // reflow.
    if (decorationStyle == StyleTextDecorationStyle::None) {
      decorationStyle = StyleTextDecorationStyle::Solid;
    }
    nsCSSRendering::DecorationRectParams params;

    bool useVerticalMetrics = verticalRun && mTextRun->UseCenterBaseline();
    nsFontMetrics* fontMetrics = aProvider.GetFontMetrics();
    RefPtr<gfxFont> font =
        fontMetrics->GetThebesFontGroup()->GetFirstValidFont();
    const gfxFont::Metrics& metrics =
        font->GetMetrics(useVerticalMetrics ? nsFontMetrics::eVertical
                                            : nsFontMetrics::eHorizontal);

    params.defaultLineThickness = metrics.underlineSize;
    params.lineSize.height = ComputeDecorationLineThickness(
        aBlock->Style()->StyleTextReset()->mTextDecorationThickness,
        params.defaultLineThickness, metrics, appUnitsPerDevUnit, this);

    const auto* styleText = aBlock->StyleText();
    bool swapUnderline =
        wm.IsCentralBaseline() && IsUnderlineRight(*aBlock->Style());
    params.offset = ComputeDecorationLineOffset(
        StyleTextDecorationLine::UNDERLINE, styleText->mTextUnderlinePosition,
        styleText->mTextUnderlineOffset, metrics, appUnitsPerDevUnit, this,
        wm.IsCentralBaseline(), swapUnderline);

    nscoord maxAscent =
        inverted ? fontMetrics->MaxDescent() : fontMetrics->MaxAscent();

    Float gfxWidth =
        (verticalRun ? aInkOverflowRect->height : aInkOverflowRect->width) /
        appUnitsPerDevUnit;
    params.lineSize.width = gfxWidth;
    params.ascent = gfxFloat(mAscent) / appUnitsPerDevUnit;
    params.style = decorationStyle;
    params.vertical = verticalRun;
    params.sidewaysLeft = mTextRun->IsSidewaysLeft();
    params.decoration = StyleTextDecorationLine::UNDERLINE;
    nsRect underlineRect =
        nsCSSRendering::GetTextDecorationRect(aPresContext, params);

    // TODO(jfkthame):
    // Should we actually be calling ComputeDecorationLineOffset again here?
    params.offset = maxAscent / appUnitsPerDevUnit;
    params.decoration = StyleTextDecorationLine::OVERLINE;
    nsRect overlineRect =
        nsCSSRendering::GetTextDecorationRect(aPresContext, params);

    aInkOverflowRect->UnionRect(*aInkOverflowRect, underlineRect);
    aInkOverflowRect->UnionRect(*aInkOverflowRect, overlineRect);

    // XXX If strikeoutSize is much thicker than the underlineSize, it may
    //     cause overflowing from the overflow rect.  However, such case
    //     isn't realistic, we don't need to compute it now.
  }
  if (aIncludeTextDecorations) {
    // Use writing mode of parent frame for orthogonal text frame to
    // work. See comment in nsTextFrame::DrawTextRunAndDecorations.
    WritingMode parentWM = GetParent()->GetWritingMode();
    bool verticalDec = parentWM.IsVertical();
    bool useVerticalMetrics =
        verticalDec != verticalRun
            ? verticalDec
            : verticalRun && mTextRun->UseCenterBaseline();

    // Since CSS 2.1 requires that text-decoration defined on ancestors maintain
    // style and position, they can be drawn at virtually any y-offset, so
    // maxima and minima are required to reliably generate the rectangle for
    // them
    TextDecorations textDecs;
    GetTextDecorations(aPresContext, eResolvedColors, textDecs);
    if (textDecs.HasDecorationLines()) {
      nscoord inflationMinFontSize =
          nsLayoutUtils::InflationMinFontSizeFor(aBlock);

      const nscoord measure = verticalDec ? GetSize().height : GetSize().width;
      gfxFloat gfxWidth = measure / appUnitsPerDevUnit;
      gfxFloat ascent =
          gfxFloat(GetLogicalBaseline(parentWM)) / appUnitsPerDevUnit;
      nscoord frameBStart = 0;
      if (parentWM.IsVerticalRL()) {
        frameBStart = GetSize().width;
        ascent = -ascent;
      }

      nsCSSRendering::DecorationRectParams params;
      params.lineSize = Size(gfxWidth, 0);
      params.ascent = ascent;
      params.vertical = verticalDec;
      params.sidewaysLeft = mTextRun->IsSidewaysLeft();

      nscoord topOrLeft(nscoord_MAX), bottomOrRight(nscoord_MIN);
      typedef gfxFont::Metrics Metrics;
      auto accumulateDecorationRect =
          [&](const LineDecoration& dec, gfxFloat Metrics::* lineSize,
              mozilla::StyleTextDecorationLine lineType) {
            params.style = dec.mStyle;
            // If the style is solid, let's include decoration line rect of
            // solid style since changing the style from none to
            // solid/dotted/dashed doesn't cause reflow.
            if (params.style == StyleTextDecorationStyle::None) {
              params.style = StyleTextDecorationStyle::Solid;
            }

            float inflation = GetInflationForTextDecorations(
                dec.mFrame, inflationMinFontSize);
            const Metrics metrics =
                GetFirstFontMetrics(GetFontGroupForFrame(dec.mFrame, inflation),
                                    useVerticalMetrics);

            params.defaultLineThickness = metrics.*lineSize;
            params.lineSize.height = ComputeDecorationLineThickness(
                dec.mTextDecorationThickness, params.defaultLineThickness,
                metrics, appUnitsPerDevUnit, this);

            bool swapUnderline =
                parentWM.IsCentralBaseline() && IsUnderlineRight(*Style());
            params.offset = ComputeDecorationLineOffset(
                lineType, dec.mTextUnderlinePosition, dec.mTextUnderlineOffset,
                metrics, appUnitsPerDevUnit, this, parentWM.IsCentralBaseline(),
                swapUnderline);

            const nsRect decorationRect =
                nsCSSRendering::GetTextDecorationRect(aPresContext, params) +
                (verticalDec ? nsPoint(frameBStart - dec.mBaselineOffset, 0)
                             : nsPoint(0, -dec.mBaselineOffset));

            if (verticalDec) {
              topOrLeft = std::min(decorationRect.x, topOrLeft);
              bottomOrRight = std::max(decorationRect.XMost(), bottomOrRight);
            } else {
              topOrLeft = std::min(decorationRect.y, topOrLeft);
              bottomOrRight = std::max(decorationRect.YMost(), bottomOrRight);
            }
          };

      // Below we loop through all text decorations and compute the rectangle
      // containing all of them, in this frame's coordinate space
      params.decoration = StyleTextDecorationLine::UNDERLINE;
      for (const LineDecoration& dec : textDecs.mUnderlines) {
        accumulateDecorationRect(dec, &Metrics::underlineSize,
                                 params.decoration);
      }
      params.decoration = StyleTextDecorationLine::OVERLINE;
      for (const LineDecoration& dec : textDecs.mOverlines) {
        accumulateDecorationRect(dec, &Metrics::underlineSize,
                                 params.decoration);
      }
      params.decoration = StyleTextDecorationLine::LINE_THROUGH;
      for (const LineDecoration& dec : textDecs.mStrikes) {
        accumulateDecorationRect(dec, &Metrics::strikeoutSize,
                                 params.decoration);
      }

      aInkOverflowRect->UnionRect(
          *aInkOverflowRect,
          verticalDec
              ? nsRect(topOrLeft, 0, bottomOrRight - topOrLeft, measure)
              : nsRect(0, topOrLeft, measure, bottomOrRight - topOrLeft));
    }

    aInkOverflowRect->UnionRect(*aInkOverflowRect,
                                UpdateTextEmphasis(parentWM, aProvider));
  }

  // text-stroke overflows: add half of text-stroke-width on all sides
  nscoord textStrokeWidth = StyleText()->mWebkitTextStrokeWidth;
  if (textStrokeWidth > 0) {
    // Inflate rect by stroke-width/2; we add an extra pixel to allow for
    // antialiasing, rounding errors, etc.
    nsRect strokeRect = *aInkOverflowRect;
    strokeRect.Inflate(textStrokeWidth / 2 + appUnitsPerDevUnit);
    aInkOverflowRect->UnionRect(*aInkOverflowRect, strokeRect);
  }

  // Text-shadow overflows
  if (aIncludeShadows) {
    *aInkOverflowRect =
        nsLayoutUtils::GetTextShadowRectsUnion(*aInkOverflowRect, this);
  }

  // When this frame is not selected, the text-decoration area must be in
  // frame bounds.
  if (!IsSelected() ||
      !CombineSelectionUnderlineRect(aPresContext, *aInkOverflowRect)) {
    return;
  }
  AddStateBits(TEXT_SELECTION_UNDERLINE_OVERFLOWED);
}

nscoord nsTextFrame::ComputeLineHeight() const {
  return ReflowInput::CalcLineHeight(*Style(), PresContext(), GetContent(),
                                     NS_UNCONSTRAINEDSIZE,
                                     GetFontSizeInflation());
}

gfxFloat nsTextFrame::ComputeDescentLimitForSelectionUnderline(
    nsPresContext* aPresContext, const gfxFont::Metrics& aFontMetrics) {
  const gfxFloat lineHeight =
      gfxFloat(ComputeLineHeight()) / aPresContext->AppUnitsPerDevPixel();
  if (lineHeight <= aFontMetrics.maxHeight) {
    return aFontMetrics.maxDescent;
  }
  return aFontMetrics.maxDescent + (lineHeight - aFontMetrics.maxHeight) / 2;
}

// Make sure this stays in sync with DrawSelectionDecorations below
static constexpr SelectionTypeMask kSelectionTypesWithDecorations =
    ToSelectionTypeMask(SelectionType::eSpellCheck) |
    ToSelectionTypeMask(SelectionType::eURLStrikeout) |
    ToSelectionTypeMask(SelectionType::eIMERawClause) |
    ToSelectionTypeMask(SelectionType::eIMESelectedRawClause) |
    ToSelectionTypeMask(SelectionType::eIMEConvertedClause) |
    ToSelectionTypeMask(SelectionType::eIMESelectedClause);

/* static */
gfxFloat nsTextFrame::ComputeSelectionUnderlineHeight(
    nsPresContext* aPresContext, const gfxFont::Metrics& aFontMetrics,
    SelectionType aSelectionType) {
  switch (aSelectionType) {
    case SelectionType::eIMERawClause:
    case SelectionType::eIMESelectedRawClause:
    case SelectionType::eIMEConvertedClause:
    case SelectionType::eIMESelectedClause:
      return aFontMetrics.underlineSize;
    case SelectionType::eSpellCheck: {
      // The thickness of the spellchecker underline shouldn't honor the font
      // metrics.  It should be constant pixels value which is decided from the
      // default font size.  Note that if the actual font size is smaller than
      // the default font size, we should use the actual font size because the
      // computed value from the default font size can be too thick for the
      // current font size.
      Length defaultFontSize =
          aPresContext->Document()
              ->GetFontPrefsForLang(nullptr)
              ->GetDefaultFont(StyleGenericFontFamily::None)
              ->size;
      int32_t zoomedFontSize = aPresContext->CSSPixelsToDevPixels(
          nsStyleFont::ZoomText(*aPresContext->Document(), defaultFontSize)
              .ToCSSPixels());
      gfxFloat fontSize =
          std::min(gfxFloat(zoomedFontSize), aFontMetrics.emHeight);
      fontSize = std::max(fontSize, 1.0);
      return ceil(fontSize / 20);
    }
    default:
      NS_WARNING("Requested underline style is not valid");
      return aFontMetrics.underlineSize;
  }
}

enum class DecorationType { Normal, Selection };
struct nsTextFrame::PaintDecorationLineParams
    : nsCSSRendering::DecorationRectParams {
  gfxContext* context = nullptr;
  LayoutDeviceRect dirtyRect;
  Point pt;
  const nscolor* overrideColor = nullptr;
  nscolor color = NS_RGBA(0, 0, 0, 0);
  gfxFloat icoordInFrame = 0.0f;
  gfxFloat baselineOffset = 0.0f;
  DecorationType decorationType = DecorationType::Normal;
  DrawPathCallbacks* callbacks = nullptr;
  bool paintingShadows = false;
  bool allowInkSkipping = true;
};

void nsTextFrame::PaintDecorationLine(
    const PaintDecorationLineParams& aParams) {
  nsCSSRendering::PaintDecorationLineParams params;
  static_cast<nsCSSRendering::DecorationRectParams&>(params) = aParams;
  params.dirtyRect = aParams.dirtyRect.ToUnknownRect();
  params.pt = aParams.pt;
  params.color = aParams.overrideColor ? *aParams.overrideColor : aParams.color;
  params.icoordInFrame = Float(aParams.icoordInFrame);
  params.baselineOffset = Float(aParams.baselineOffset);
  params.allowInkSkipping = aParams.allowInkSkipping;
  if (aParams.callbacks) {
    Rect path = nsCSSRendering::DecorationLineToPath(params);
    if (aParams.decorationType == DecorationType::Normal) {
      aParams.callbacks->PaintDecorationLine(path, aParams.paintingShadows,
                                             params.color);
    } else {
      aParams.callbacks->PaintSelectionDecorationLine(
          path, aParams.paintingShadows, params.color);
    }
  } else {
    nsCSSRendering::PaintDecorationLine(this, *aParams.context->GetDrawTarget(),
                                        params);
  }
}

static StyleTextDecorationStyle ToStyleLineStyle(const TextRangeStyle& aStyle) {
  switch (aStyle.mLineStyle) {
    case TextRangeStyle::LineStyle::None:
      return StyleTextDecorationStyle::None;
    case TextRangeStyle::LineStyle::Solid:
      return StyleTextDecorationStyle::Solid;
    case TextRangeStyle::LineStyle::Dotted:
      return StyleTextDecorationStyle::Dotted;
    case TextRangeStyle::LineStyle::Dashed:
      return StyleTextDecorationStyle::Dashed;
    case TextRangeStyle::LineStyle::Double:
      return StyleTextDecorationStyle::Double;
    case TextRangeStyle::LineStyle::Wavy:
      return StyleTextDecorationStyle::Wavy;
  }
  MOZ_ASSERT_UNREACHABLE("Invalid line style");
  return StyleTextDecorationStyle::None;
}

/**
 * This, plus kSelectionTypesWithDecorations, encapsulates all knowledge
 * about drawing text decoration for selections.
 */
void nsTextFrame::DrawSelectionDecorations(
    gfxContext* aContext, const LayoutDeviceRect& aDirtyRect,
    SelectionType aSelectionType, nsTextPaintStyle& aTextPaintStyle,
    const TextRangeStyle& aRangeStyle, const Point& aPt,
    gfxFloat aICoordInFrame, gfxFloat aWidth, gfxFloat aAscent,
    const gfxFont::Metrics& aFontMetrics, DrawPathCallbacks* aCallbacks,
    bool aVertical, StyleTextDecorationLine aDecoration) {
  PaintDecorationLineParams params;
  params.context = aContext;
  params.dirtyRect = aDirtyRect;
  params.pt = aPt;
  params.lineSize.width = aWidth;
  params.ascent = aAscent;
  params.decoration = aDecoration;
  params.decorationType = DecorationType::Selection;
  params.callbacks = aCallbacks;
  params.vertical = aVertical;
  params.sidewaysLeft = mTextRun->IsSidewaysLeft();
  params.descentLimit = ComputeDescentLimitForSelectionUnderline(
      aTextPaintStyle.PresContext(), aFontMetrics);

  float relativeSize;
  const auto& decThickness = StyleTextReset()->mTextDecorationThickness;
  const gfxFloat appUnitsPerDevPixel =
      aTextPaintStyle.PresContext()->AppUnitsPerDevPixel();

  const WritingMode wm = GetWritingMode();
  switch (aSelectionType) {
    case SelectionType::eIMERawClause:
    case SelectionType::eIMESelectedRawClause:
    case SelectionType::eIMEConvertedClause:
    case SelectionType::eIMESelectedClause:
    case SelectionType::eSpellCheck:
    case SelectionType::eHighlight: {
      auto index = nsTextPaintStyle::GetUnderlineStyleIndexForSelectionType(
          aSelectionType);
      bool weDefineSelectionUnderline =
          aTextPaintStyle.GetSelectionUnderlineForPaint(
              index, &params.color, &relativeSize, &params.style);
      params.defaultLineThickness = ComputeSelectionUnderlineHeight(
          aTextPaintStyle.PresContext(), aFontMetrics, aSelectionType);
      params.lineSize.height = ComputeDecorationLineThickness(
          decThickness, params.defaultLineThickness, aFontMetrics,
          appUnitsPerDevPixel, this);

      bool swapUnderline = wm.IsCentralBaseline() && IsUnderlineRight(*Style());
      const auto* styleText = StyleText();
      params.offset = ComputeDecorationLineOffset(
          aDecoration, styleText->mTextUnderlinePosition,
          styleText->mTextUnderlineOffset, aFontMetrics, appUnitsPerDevPixel,
          this, wm.IsCentralBaseline(), swapUnderline);

      bool isIMEType = aSelectionType != SelectionType::eSpellCheck &&
                       aSelectionType != SelectionType::eHighlight;

      if (isIMEType) {
        // IME decoration lines should not be drawn on the both ends, i.e., we
        // need to cut both edges of the decoration lines.  Because same style
        // IME selections can adjoin, but the users need to be able to know
        // where are the boundaries of the selections.
        //
        //  X: underline
        //
        //     IME selection #1        IME selection #2      IME selection #3
        //  |                     |                      |
        //  | XXXXXXXXXXXXXXXXXXX | XXXXXXXXXXXXXXXXXXXX | XXXXXXXXXXXXXXXXXXX
        //  +---------------------+----------------------+--------------------
        //   ^                   ^ ^                    ^ ^
        //  gap                  gap                    gap
        params.pt.x += 1.0;
        params.lineSize.width -= 2.0;
      }
      if (isIMEType && aRangeStyle.IsDefined()) {
        // If IME defines the style, that should override our definition.
        if (aRangeStyle.IsLineStyleDefined()) {
          if (aRangeStyle.mLineStyle == TextRangeStyle::LineStyle::None) {
            return;
          }
          params.style = ToStyleLineStyle(aRangeStyle);
          relativeSize = aRangeStyle.mIsBoldLine ? 2.0f : 1.0f;
        } else if (!weDefineSelectionUnderline) {
          // There is no underline style definition.
          return;
        }
        // If underline color is defined and that doesn't depend on the
        // foreground color, we should use the color directly.
        if (aRangeStyle.IsUnderlineColorDefined() &&
            (!aRangeStyle.IsForegroundColorDefined() ||
             aRangeStyle.mUnderlineColor != aRangeStyle.mForegroundColor)) {
          params.color = aRangeStyle.mUnderlineColor;
        }
        // If foreground color or background color is defined, the both colors
        // are computed by GetSelectionTextColors().  Then, we should use its
        // foreground color always.  The color should have sufficient contrast
        // with the background color.
        else if (aRangeStyle.IsForegroundColorDefined() ||
                 aRangeStyle.IsBackgroundColorDefined()) {
          nscolor bg;
          GetSelectionTextColors(aSelectionType, nullptr, aTextPaintStyle,
                                 aRangeStyle, &params.color, &bg);
        }
        // Otherwise, use the foreground color of the frame.
        else {
          params.color = aTextPaintStyle.GetTextColor();
        }
      } else if (!weDefineSelectionUnderline) {
        // IME doesn't specify the selection style and we don't define selection
        // underline.
        return;
      }
      break;
    }
    case SelectionType::eURLStrikeout: {
      nscoord inflationMinFontSize =
          nsLayoutUtils::InflationMinFontSizeFor(this);
      float inflation =
          GetInflationForTextDecorations(this, inflationMinFontSize);
      const gfxFont::Metrics metrics =
          GetFirstFontMetrics(GetFontGroupForFrame(this, inflation), aVertical);

      relativeSize = 2.0f;
      aTextPaintStyle.GetURLSecondaryColor(&params.color);
      params.style = StyleTextDecorationStyle::Solid;
      params.defaultLineThickness = metrics.strikeoutSize;
      params.lineSize.height = ComputeDecorationLineThickness(
          decThickness, params.defaultLineThickness, metrics,
          appUnitsPerDevPixel, this);
      // TODO(jfkthame): ComputeDecorationLineOffset? check vertical mode!
      params.offset = metrics.strikeoutOffset + 0.5;
      params.decoration = StyleTextDecorationLine::LINE_THROUGH;
      break;
    }
    default:
      NS_WARNING("Requested selection decorations when there aren't any");
      return;
  }
  params.lineSize.height *= relativeSize;
  params.defaultLineThickness *= relativeSize;
  params.icoordInFrame =
      (aVertical ? params.pt.y - aPt.y : params.pt.x - aPt.x) + aICoordInFrame;
  PaintDecorationLine(params);
}

/* static */
bool nsTextFrame::GetSelectionTextColors(SelectionType aSelectionType,
                                         nsAtom* aHighlightName,
                                         nsTextPaintStyle& aTextPaintStyle,
                                         const TextRangeStyle& aRangeStyle,
                                         nscolor* aForeground,
                                         nscolor* aBackground) {
  switch (aSelectionType) {
    case SelectionType::eNormal:
      return aTextPaintStyle.GetSelectionColors(aForeground, aBackground);
    case SelectionType::eFind:
      aTextPaintStyle.GetHighlightColors(aForeground, aBackground);
      return true;
    case SelectionType::eHighlight: {
      // Intentionally not short-cutting here because the called methods have
      // side-effects that affect outparams.
      bool hasForeground = aTextPaintStyle.GetCustomHighlightTextColor(
          aHighlightName, aForeground);
      bool hasBackground = aTextPaintStyle.GetCustomHighlightBackgroundColor(
          aHighlightName, aBackground);
      return hasForeground || hasBackground;
    }
    case SelectionType::eTargetText: {
      aTextPaintStyle.GetTargetTextColors(aForeground, aBackground);
      return true;
    }
    case SelectionType::eURLSecondary:
      aTextPaintStyle.GetURLSecondaryColor(aForeground);
      *aBackground = NS_RGBA(0, 0, 0, 0);
      return true;
    case SelectionType::eIMERawClause:
    case SelectionType::eIMESelectedRawClause:
    case SelectionType::eIMEConvertedClause:
    case SelectionType::eIMESelectedClause:
      if (aRangeStyle.IsDefined()) {
        if (!aRangeStyle.IsForegroundColorDefined() &&
            !aRangeStyle.IsBackgroundColorDefined()) {
          *aForeground = aTextPaintStyle.GetTextColor();
          *aBackground = NS_RGBA(0, 0, 0, 0);
          return false;
        }
        if (aRangeStyle.IsForegroundColorDefined()) {
          *aForeground = aRangeStyle.mForegroundColor;
          if (aRangeStyle.IsBackgroundColorDefined()) {
            *aBackground = aRangeStyle.mBackgroundColor;
          } else {
            // If foreground color is defined but background color isn't
            // defined, we can guess that IME must expect that the background
            // color is system's default field background color.
            *aBackground = aTextPaintStyle.GetSystemFieldBackgroundColor();
          }
        } else {  // aRangeStyle.IsBackgroundColorDefined() is true
          *aBackground = aRangeStyle.mBackgroundColor;
          // If background color is defined but foreground color isn't defined,
          // we can assume that IME must expect that the foreground color is
          // same as system's field text color.
          *aForeground = aTextPaintStyle.GetSystemFieldForegroundColor();
        }
        return true;
      }
      aTextPaintStyle.GetIMESelectionColors(
          nsTextPaintStyle::GetUnderlineStyleIndexForSelectionType(
              aSelectionType),
          aForeground, aBackground);
      return true;
    default:
      *aForeground = aTextPaintStyle.GetTextColor();
      *aBackground = NS_RGBA(0, 0, 0, 0);
      return false;
  }
}

/**
 * This sets *aShadows to the appropriate shadows, if any, for the given
 * type of selection.
 * If text-shadow was not specified, *aShadows is left untouched.
 */
void nsTextFrame::GetSelectionTextShadow(
    SelectionType aSelectionType, nsTextPaintStyle& aTextPaintStyle,
    Span<const StyleSimpleShadow>* aShadows) {
  if (aSelectionType != SelectionType::eNormal) {
    return;
  }
  aTextPaintStyle.GetSelectionShadow(aShadows);
}

/**
 * This class lets us iterate over chunks of text recorded in an array of
 * resolved selection ranges, observing cluster boundaries, in content order,
 * maintaining the current x-offset as we go, and telling whether the text
 * chunk has a hyphen after it or not.
 * In addition to returning the selected chunks, the iterator is responsible
 * to interpolate unselected chunks in any gaps between them.
 * The caller is responsible for actually computing the advance width of each
 * chunk.
 */
class MOZ_STACK_CLASS SelectionRangeIterator {
  using PropertyProvider = nsTextFrame::PropertyProvider;
  using CombinedSelectionRange = nsTextFrame::PriorityOrderedSelectionsForRange;

 public:
  // aSelectionRanges and aRange are according to the original string.
  SelectionRangeIterator(
      const nsTArray<CombinedSelectionRange>& aSelectionRanges,
      gfxTextRun::Range aRange, PropertyProvider& aProvider,
      gfxTextRun* aTextRun, gfxFloat aXOffset);

  bool GetNextSegment(gfxFloat* aXOffset, gfxTextRun::Range* aRange,
                      gfxFloat* aHyphenWidth,
                      nsTArray<SelectionType>& aSelectionType,
                      nsTArray<RefPtr<nsAtom>>& aHighlightName,
                      nsTArray<TextRangeStyle>& aStyle);

  void UpdateWithAdvance(gfxFloat aAdvance) {
    mXOffset += aAdvance * mTextRun->GetDirection();
  }

 private:
  const nsTArray<CombinedSelectionRange>& mSelectionRanges;
  PropertyProvider& mProvider;
  gfxTextRun* mTextRun;
  gfxSkipCharsIterator mIterator;
  gfxTextRun::Range mOriginalRange;
  gfxFloat mXOffset;
  uint32_t mIndex;
};

SelectionRangeIterator::SelectionRangeIterator(
    const nsTArray<nsTextFrame::PriorityOrderedSelectionsForRange>&
        aSelectionRanges,
    gfxTextRun::Range aRange, PropertyProvider& aProvider, gfxTextRun* aTextRun,
    gfxFloat aXOffset)
    : mSelectionRanges(aSelectionRanges),
      mProvider(aProvider),
      mTextRun(aTextRun),
      mIterator(aProvider.GetStart()),
      mOriginalRange(aRange),
      mXOffset(aXOffset),
      mIndex(0) {
  mIterator.SetOriginalOffset(int32_t(aRange.start));
}

bool SelectionRangeIterator::GetNextSegment(
    gfxFloat* aXOffset, gfxTextRun::Range* aRange, gfxFloat* aHyphenWidth,
    nsTArray<SelectionType>& aSelectionType,
    nsTArray<RefPtr<nsAtom>>& aHighlightName,
    nsTArray<TextRangeStyle>& aStyle) {
  if (mIterator.GetOriginalOffset() >= int32_t(mOriginalRange.end)) {
    return false;
  }

  uint32_t runOffset = mIterator.GetSkippedOffset();
  uint32_t segmentEnd = mOriginalRange.end;

  aSelectionType.Clear();
  aHighlightName.Clear();
  aStyle.Clear();

  if (mIndex == mSelectionRanges.Length() ||
      mIterator.GetOriginalOffset() <
          int32_t(mSelectionRanges[mIndex].mRange.start)) {
    // There's an unselected segment before the next range (or at the end).
    aSelectionType.AppendElement(SelectionType::eNone);
    aHighlightName.AppendElement();
    aStyle.AppendElement(TextRangeStyle());
    if (mIndex < mSelectionRanges.Length()) {
      segmentEnd = mSelectionRanges[mIndex].mRange.start;
    }
  } else {
    // Get the selection details for the next segment, and increment index.
    for (const SelectionDetails* sdptr :
         mSelectionRanges[mIndex].mSelectionRanges) {
      aSelectionType.AppendElement(sdptr->mSelectionType);
      aHighlightName.AppendElement(sdptr->mHighlightData.mHighlightName);
      aStyle.AppendElement(sdptr->mTextRangeStyle);
    }
    segmentEnd = mSelectionRanges[mIndex].mRange.end;
    ++mIndex;
  }

  // Advance iterator to the end of the segment.
  mIterator.SetOriginalOffset(int32_t(segmentEnd));

  // Further advance if necessary to a cluster boundary.
  while (mIterator.GetOriginalOffset() < int32_t(mOriginalRange.end) &&
         !mIterator.IsOriginalCharSkipped() &&
         !mTextRun->IsClusterStart(mIterator.GetSkippedOffset())) {
    mIterator.AdvanceOriginal(1);
  }

  aRange->start = runOffset;
  aRange->end = mIterator.GetSkippedOffset();
  *aXOffset = mXOffset;
  *aHyphenWidth = 0;
  if (mIterator.GetOriginalOffset() == int32_t(mOriginalRange.end) &&
      mProvider.GetFrame()->HasAnyStateBits(TEXT_HYPHEN_BREAK)) {
    *aHyphenWidth = mProvider.GetHyphenWidth();
  }

  return true;
}

static void AddHyphenToMetrics(nsTextFrame* aTextFrame, bool aIsRightToLeft,
                               gfxTextRun::Metrics* aMetrics,
                               gfxFont::BoundingBoxType aBoundingBoxType,
                               DrawTarget* aDrawTarget) {
  // Fix up metrics to include hyphen
  RefPtr<gfxTextRun> hyphenTextRun = GetHyphenTextRun(aTextFrame, aDrawTarget);
  if (!hyphenTextRun) {
    return;
  }

  gfxTextRun::Metrics hyphenMetrics =
      hyphenTextRun->MeasureText(aBoundingBoxType, aDrawTarget);
  if (aTextFrame->GetWritingMode().IsLineInverted()) {
    hyphenMetrics.mBoundingBox.y = -hyphenMetrics.mBoundingBox.YMost();
  }
  aMetrics->CombineWith(hyphenMetrics, aIsRightToLeft);
}

void nsTextFrame::PaintOneShadow(const PaintShadowParams& aParams,
                                 const StyleSimpleShadow& aShadowDetails,
                                 gfxRect& aBoundingBox, uint32_t aBlurFlags) {
  AUTO_PROFILER_LABEL("nsTextFrame::PaintOneShadow", GRAPHICS);

  nsPoint shadowOffset(aShadowDetails.horizontal.ToAppUnits(),
                       aShadowDetails.vertical.ToAppUnits());
  nscoord blurRadius = std::max(aShadowDetails.blur.ToAppUnits(), 0);

  nscolor shadowColor = aShadowDetails.color.CalcColor(aParams.foregroundColor);

  if (auto* textDrawer = aParams.context->GetTextDrawer()) {
    wr::Shadow wrShadow;

    wrShadow.offset = {PresContext()->AppUnitsToFloatDevPixels(shadowOffset.x),
                       PresContext()->AppUnitsToFloatDevPixels(shadowOffset.y)};

    wrShadow.blur_radius = PresContext()->AppUnitsToFloatDevPixels(blurRadius);
    wrShadow.color = wr::ToColorF(ToDeviceColor(shadowColor));

    bool inflate = true;
    textDrawer->AppendShadow(wrShadow, inflate);
    return;
  }

  // This rect is the box which is equivalent to where the shadow will be
  // painted. The origin of aBoundingBox is the text baseline left, so we must
  // translate it by that much in order to make the origin the top-left corner
  // of the text bounding box. Note that aLeftSideOffset is line-left, so
  // actually means top offset in vertical writing modes.
  gfxRect shadowGfxRect;
  WritingMode wm = GetWritingMode();
  if (wm.IsVertical()) {
    shadowGfxRect = aBoundingBox;
    if (wm.IsVerticalRL()) {
      // for vertical-RL, reverse direction of x-coords of bounding box
      shadowGfxRect.x = -shadowGfxRect.XMost();
    }
    shadowGfxRect += gfxPoint(aParams.textBaselinePt.x,
                              aParams.framePt.y + aParams.leftSideOffset);
  } else {
    shadowGfxRect =
        aBoundingBox + gfxPoint(aParams.framePt.x + aParams.leftSideOffset,
                                aParams.textBaselinePt.y);
  }
  Point shadowGfxOffset(shadowOffset.x, shadowOffset.y);
  shadowGfxRect += gfxPoint(shadowGfxOffset.x, shadowOffset.y);

  nsRect shadowRect(NSToCoordRound(shadowGfxRect.X()),
                    NSToCoordRound(shadowGfxRect.Y()),
                    NSToCoordRound(shadowGfxRect.Width()),
                    NSToCoordRound(shadowGfxRect.Height()));

  nsContextBoxBlur contextBoxBlur;
  const auto A2D = PresContext()->AppUnitsPerDevPixel();
  gfxContext* shadowContext =
      contextBoxBlur.Init(shadowRect, 0, blurRadius, A2D, aParams.context,
                          LayoutDevicePixel::ToAppUnits(aParams.dirtyRect, A2D),
                          nullptr, aBlurFlags);
  if (!shadowContext) {
    return;
  }

  aParams.context->Save();
  aParams.context->SetColor(sRGBColor::FromABGR(shadowColor));

  // Draw the text onto our alpha-only surface to capture the alpha values.
  // Remember that the box blur context has a device offset on it, so we don't
  // need to translate any coordinates to fit on the surface.
  gfxFloat advanceWidth;
  nsTextPaintStyle textPaintStyle(this);
  DrawTextParams params(shadowContext, PresContext()->FontPaletteCache());
  params.paintingShadows = true;
  params.advanceWidth = &advanceWidth;
  params.dirtyRect = aParams.dirtyRect;
  params.framePt = aParams.framePt + shadowGfxOffset;
  params.provider = aParams.provider;
  params.textStyle = &textPaintStyle;
  params.textColor =
      aParams.context == shadowContext ? shadowColor : NS_RGB(0, 0, 0);
  params.callbacks = aParams.callbacks;
  params.clipEdges = aParams.clipEdges;
  params.drawSoftHyphen = HasAnyStateBits(TEXT_HYPHEN_BREAK);
  // Multi-color shadow is not allowed, so we use the same color as the text
  // color.
  params.decorationOverrideColor = &params.textColor;
  params.fontPalette = StyleFont()->GetFontPaletteAtom();

  DrawText(aParams.range, aParams.textBaselinePt + shadowGfxOffset, params);

  contextBoxBlur.DoPaint();
  aParams.context->Restore();
}

/* static */
SelectionTypeMask nsTextFrame::CreateSelectionRangeList(
    const SelectionDetails* aDetails, SelectionType aSelectionType,
    const PaintTextSelectionParams& aParams,
    nsTArray<SelectionRange>& aSelectionRanges, bool* aAnyBackgrounds) {
  SelectionTypeMask allTypes = 0;
  bool anyBackgrounds = false;

  uint32_t priorityOfInsertionOrder = 0;
  for (const SelectionDetails* sd = aDetails; sd; sd = sd->mNext.get()) {
    MOZ_ASSERT(sd->mStart >= 0 && sd->mEnd >= 0);  // XXX make unsigned?
    uint32_t start = std::max(aParams.contentRange.start, uint32_t(sd->mStart));
    uint32_t end = std::min(aParams.contentRange.end, uint32_t(sd->mEnd));
    if (start < end) {
      // The PaintTextWithSelectionColors caller passes SelectionType::eNone,
      // so we collect all selections that set colors, and prioritize them
      // according to selection type (lower types take precedence).
      if (aSelectionType == SelectionType::eNone) {
        allTypes |= ToSelectionTypeMask(sd->mSelectionType);
        // Ignore selections that don't set colors.
        nscolor foreground(0), background(0);
        if (GetSelectionTextColors(sd->mSelectionType,
                                   sd->mHighlightData.mHighlightName,
                                   *aParams.textPaintStyle, sd->mTextRangeStyle,
                                   &foreground, &background)) {
          if (NS_GET_A(background) > 0) {
            anyBackgrounds = true;
          }
          aSelectionRanges.AppendElement(
              SelectionRange{sd, {start, end}, priorityOfInsertionOrder++});
        }
      } else if (sd->mSelectionType == aSelectionType) {
        // The PaintSelectionTextDecorations caller passes a specific type,
        // so we include only ranges of that type, and keep them in order
        // so that later ones take precedence over earlier.
        aSelectionRanges.AppendElement(
            SelectionRange{sd, {start, end}, priorityOfInsertionOrder++});
      }
    }
  }
  if (aAnyBackgrounds) {
    *aAnyBackgrounds = anyBackgrounds;
  }
  return allTypes;
}

/* static */
void nsTextFrame::CombineSelectionRanges(
    const nsTArray<SelectionRange>& aSelectionRanges,
    nsTArray<PriorityOrderedSelectionsForRange>& aCombinedSelectionRanges) {
  struct SelectionRangeEndCmp {
    bool Equals(const SelectionRange* a, const SelectionRange* b) const {
      return a->mRange.end == b->mRange.end;
    }
    bool LessThan(const SelectionRange* a, const SelectionRange* b) const {
      return a->mRange.end < b->mRange.end;
    }
  };

  struct SelectionRangePriorityCmp {
    bool Equals(const SelectionRange* a, const SelectionRange* b) const {
      const SelectionDetails* aDetails = a->mDetails;
      const SelectionDetails* bDetails = b->mDetails;
      if (aDetails->mSelectionType != bDetails->mSelectionType) {
        return false;
      }
      if (aDetails->mSelectionType != SelectionType::eHighlight) {
        return a->mPriority == b->mPriority;
      }
      if (aDetails->mHighlightData.mHighlight->Priority() !=
          bDetails->mHighlightData.mHighlight->Priority()) {
        return false;
      }
      return a->mPriority == b->mPriority;
    }

    bool LessThan(const SelectionRange* a, const SelectionRange* b) const {
      if (a->mDetails->mSelectionType != b->mDetails->mSelectionType) {
        // Even though this looks counter-intuitive,
        // this is intended, as values in `SelectionType` are inverted:
        // a lower value indicates a higher priority.
        return a->mDetails->mSelectionType > b->mDetails->mSelectionType;
      }

      if (a->mDetails->mSelectionType != SelectionType::eHighlight) {
        // for non-highlights, the selection which was added later
        // has a higher priority.
        return a->mPriority < b->mPriority;
      }

      if (a->mDetails->mHighlightData.mHighlight->Priority() !=
          b->mDetails->mHighlightData.mHighlight->Priority()) {
        // For highlights, first compare the priorities set by the user.
        return a->mDetails->mHighlightData.mHighlight->Priority() <
               b->mDetails->mHighlightData.mHighlight->Priority();
      }
      // only if the user priorities are equal, let the highlight that was added
      // later take precedence.
      return a->mPriority < b->mPriority;
    }
  };

  uint32_t currentOffset = 0;
  AutoTArray<const SelectionRange*, 1> activeSelectionsForCurrentSegment;
  size_t rangeIndex = 0;

  // Divide the given selection ranges into segments which share the same
  // set of selections.
  // The following algorithm iterates `aSelectionRanges`, assuming
  // that its elements are sorted by their start offset.
  // Each time a new selection starts, it is pushed into an array of
  // "currently present" selections, sorted by their *end* offset.
  // For each iteration the next segment end offset is determined,
  // which is either the start offset of the next selection or
  // the next end offset of all "currently present" selections
  // (which is always the first element of the array because of its order).
  // Then, a `CombinedSelectionRange` can be constructed, which describes
  // the text segment until its end offset (as determined above), and contains
  // all elements of the "currently present" selection list, now sorted
  // by their priority.
  // If a range ends at the given offset, it is removed from the array.
  while (rangeIndex < aSelectionRanges.Length() ||
         !activeSelectionsForCurrentSegment.IsEmpty()) {
    uint32_t currentSegmentEndOffset =
        activeSelectionsForCurrentSegment.IsEmpty()
            ? -1
            : activeSelectionsForCurrentSegment[0]->mRange.end;
    uint32_t nextRangeStartOffset =
        rangeIndex < aSelectionRanges.Length()
            ? aSelectionRanges[rangeIndex].mRange.start
            : -1;
    uint32_t nextOffset =
        std::min(currentSegmentEndOffset, nextRangeStartOffset);
    if (!activeSelectionsForCurrentSegment.IsEmpty() &&
        currentOffset != nextOffset) {
      auto activeSelectionRangesSortedByPriority =
          activeSelectionsForCurrentSegment.Clone();
      activeSelectionRangesSortedByPriority.Sort(SelectionRangePriorityCmp());

      AutoTArray<const SelectionDetails*, 1> selectionDetails;
      selectionDetails.SetCapacity(
          activeSelectionRangesSortedByPriority.Length());
      // ensure that overlapping highlights which have the same name
      // are only added once. If added each time, they would be painted
      // several times (see wpt
      // /css/css-highlight-api/painting/custom-highlight-painting-003.html)
      // Comparing the highlight name with the previous one is
      // sufficient here because selections are already sorted
      // in a way that ensures that highlights of the same name are
      // grouped together.
      nsAtom* currentHighlightName = nullptr;
      for (const auto* selectionRange : activeSelectionRangesSortedByPriority) {
        if (selectionRange->mDetails->mSelectionType ==
            SelectionType::eHighlight) {
          if (selectionRange->mDetails->mHighlightData.mHighlightName ==
              currentHighlightName) {
            continue;
          }
          currentHighlightName =
              selectionRange->mDetails->mHighlightData.mHighlightName;
        }
        selectionDetails.AppendElement(selectionRange->mDetails);
      }
      aCombinedSelectionRanges.AppendElement(PriorityOrderedSelectionsForRange{
          std::move(selectionDetails), {currentOffset, nextOffset}});
    }
    currentOffset = nextOffset;

    if (nextRangeStartOffset < currentSegmentEndOffset) {
      activeSelectionsForCurrentSegment.InsertElementSorted(
          &aSelectionRanges[rangeIndex++], SelectionRangeEndCmp());
    } else {
      activeSelectionsForCurrentSegment.RemoveElementAt(0);
    }
  }
}

SelectionTypeMask nsTextFrame::ResolveSelections(
    const PaintTextSelectionParams& aParams, const SelectionDetails* aDetails,
    nsTArray<PriorityOrderedSelectionsForRange>& aResult,
    SelectionType aSelectionType, bool* aAnyBackgrounds) const {
  AutoTArray<SelectionRange, 4> selectionRanges;

  SelectionTypeMask allTypes = CreateSelectionRangeList(
      aDetails, aSelectionType, aParams, selectionRanges, aAnyBackgrounds);

  if (selectionRanges.IsEmpty()) {
    return allTypes;
  }

  struct SelectionRangeStartCmp {
    bool Equals(const SelectionRange& a, const SelectionRange& b) const {
      return a.mRange.start == b.mRange.start;
    }
    bool LessThan(const SelectionRange& a, const SelectionRange& b) const {
      return a.mRange.start < b.mRange.start;
    }
  };
  selectionRanges.Sort(SelectionRangeStartCmp());

  CombineSelectionRanges(selectionRanges, aResult);

  return allTypes;
}

// Paints selection backgrounds and text in the correct colors. Also computes
// aAllSelectionTypeMask, the union of all selection types that are applying to
// this text.
bool nsTextFrame::PaintTextWithSelectionColors(
    const PaintTextSelectionParams& aParams,
    const UniquePtr<SelectionDetails>& aDetails,
    SelectionTypeMask* aAllSelectionTypeMask, const ClipEdges& aClipEdges) {
  bool anyBackgrounds = false;
  AutoTArray<PriorityOrderedSelectionsForRange, 8> selectionRanges;

  *aAllSelectionTypeMask =
      ResolveSelections(aParams, aDetails.get(), selectionRanges,
                        SelectionType::eNone, &anyBackgrounds);
  bool vertical = mTextRun->IsVertical();
  const gfxFloat startIOffset =
      vertical ? aParams.textBaselinePt.y - aParams.framePt.y
               : aParams.textBaselinePt.x - aParams.framePt.x;
  gfxFloat iOffset, hyphenWidth;
  Range range;  // in transformed string

  const gfxTextRun::Range& contentRange = aParams.contentRange;
  auto* textDrawer = aParams.context->GetTextDrawer();

  if (anyBackgrounds && !aParams.IsGenerateTextMask()) {
    int32_t appUnitsPerDevPixel =
        aParams.textPaintStyle->PresContext()->AppUnitsPerDevPixel();
    SelectionRangeIterator iterator(selectionRanges, contentRange,
                                    *aParams.provider, mTextRun, startIOffset);
    AutoTArray<SelectionType, 1> selectionTypes;
    AutoTArray<RefPtr<nsAtom>, 1> highlightNames;
    AutoTArray<TextRangeStyle, 1> rangeStyles;
    while (iterator.GetNextSegment(&iOffset, &range, &hyphenWidth,
                                   selectionTypes, highlightNames,
                                   rangeStyles)) {
      nscolor foreground(0), background(0);
      gfxFloat advance =
          hyphenWidth + mTextRun->GetAdvanceWidth(range, aParams.provider);
      nsRect bgRect;
      gfxFloat offs = iOffset - (mTextRun->IsInlineReversed() ? advance : 0);
      if (vertical) {
        bgRect = nsRect(nscoord(aParams.framePt.x),
                        nscoord(aParams.framePt.y + offs), GetSize().width,
                        nscoord(advance));
      } else {
        bgRect = nsRect(nscoord(aParams.framePt.x + offs),
                        nscoord(aParams.framePt.y), nscoord(advance),
                        GetSize().height);
      }

      LayoutDeviceRect selectionRect =
          LayoutDeviceRect::FromAppUnits(bgRect, appUnitsPerDevPixel);
      // The elements in `selectionTypes` are ordered ascending by their
      // priority. To account for non-opaque overlapping selections, all
      // selection backgrounds are painted.
      for (size_t index = 0; index < selectionTypes.Length(); ++index) {
        GetSelectionTextColors(selectionTypes[index], highlightNames[index],
                               *aParams.textPaintStyle, rangeStyles[index],
                               &foreground, &background);

        // Draw background color
        if (NS_GET_A(background) > 0) {
          if (textDrawer) {
            textDrawer->AppendSelectionRect(selectionRect,
                                            ToDeviceColor(background));
          } else {
            PaintSelectionBackground(*aParams.context->GetDrawTarget(),
                                     background, aParams.dirtyRect,
                                     selectionRect, aParams.callbacks);
          }
        }
      }
      iterator.UpdateWithAdvance(advance);
    }
  }

  gfxFloat advance;
  DrawTextParams params(aParams.context, PresContext()->FontPaletteCache());
  params.dirtyRect = aParams.dirtyRect;
  params.framePt = aParams.framePt;
  params.provider = aParams.provider;
  params.textStyle = aParams.textPaintStyle;
  params.clipEdges = &aClipEdges;
  params.advanceWidth = &advance;
  params.callbacks = aParams.callbacks;
  params.glyphRange = aParams.glyphRange;
  params.fontPalette = StyleFont()->GetFontPaletteAtom();
  params.hasTextShadow = !StyleText()->mTextShadow.IsEmpty();

  PaintShadowParams shadowParams(aParams);
  shadowParams.provider = aParams.provider;
  shadowParams.callbacks = aParams.callbacks;
  shadowParams.clipEdges = &aClipEdges;

  // Draw text
  const nsStyleText* textStyle = StyleText();
  SelectionRangeIterator iterator(selectionRanges, contentRange,
                                  *aParams.provider, mTextRun, startIOffset);
  AutoTArray<SelectionType, 1> selectionTypes;
  AutoTArray<RefPtr<nsAtom>, 1> highlightNames;
  AutoTArray<TextRangeStyle, 1> rangeStyles;
  while (iterator.GetNextSegment(&iOffset, &range, &hyphenWidth, selectionTypes,
                                 highlightNames, rangeStyles)) {
    nscolor foreground(0), background(0);
    if (aParams.IsGenerateTextMask()) {
      foreground = NS_RGBA(0, 0, 0, 255);
    } else {
      nscolor tmpForeground(0);
      bool colorHasBeenSet = false;
      for (size_t index = 0; index < selectionTypes.Length(); ++index) {
        if (selectionTypes[index] == SelectionType::eHighlight) {
          if (aParams.textPaintStyle->GetCustomHighlightTextColor(
                  highlightNames[index], &tmpForeground)) {
            foreground = tmpForeground;
            colorHasBeenSet = true;
          }

        } else {
          GetSelectionTextColors(selectionTypes[index], highlightNames[index],
                                 *aParams.textPaintStyle, rangeStyles[index],
                                 &foreground, &background);
          colorHasBeenSet = true;
        }
      }
      if (!colorHasBeenSet) {
        foreground = tmpForeground;
      }
    }

    gfx::Point textBaselinePt =
        vertical
            ? gfx::Point(aParams.textBaselinePt.x, aParams.framePt.y + iOffset)
            : gfx::Point(aParams.framePt.x + iOffset, aParams.textBaselinePt.y);

    // Determine what shadow, if any, to draw - either from textStyle
    // or from the ::-moz-selection pseudo-class if specified there
    Span<const StyleSimpleShadow> shadows = textStyle->mTextShadow.AsSpan();
    for (auto selectionType : selectionTypes) {
      GetSelectionTextShadow(selectionType, *aParams.textPaintStyle, &shadows);
    }
    if (!shadows.IsEmpty()) {
      nscoord startEdge = iOffset;
      if (mTextRun->IsInlineReversed()) {
        startEdge -=
            hyphenWidth + mTextRun->GetAdvanceWidth(range, aParams.provider);
      }
      shadowParams.range = range;
      shadowParams.textBaselinePt = textBaselinePt;
      shadowParams.foregroundColor = foreground;
      shadowParams.leftSideOffset = startEdge;
      PaintShadows(shadows, shadowParams);
    }

    // Draw text segment
    params.textColor = foreground;
    params.textStrokeColor = aParams.textPaintStyle->GetWebkitTextStrokeColor();
    params.textStrokeWidth = aParams.textPaintStyle->GetWebkitTextStrokeWidth();
    params.drawSoftHyphen = hyphenWidth > 0;
    DrawText(range, textBaselinePt, params);
    advance += hyphenWidth;
    iterator.UpdateWithAdvance(advance);
  }
  return true;
}

void nsTextFrame::PaintTextSelectionDecorations(
    const PaintTextSelectionParams& aParams,
    const UniquePtr<SelectionDetails>& aDetails, SelectionType aSelectionType) {
  // Hide text decorations if we're currently hiding @font-face fallback text
  if (aParams.provider->GetFontGroup()->ShouldSkipDrawing()) {
    return;
  }

  AutoTArray<PriorityOrderedSelectionsForRange, 8> selectionRanges;
  ResolveSelections(aParams, aDetails.get(), selectionRanges, aSelectionType);

  RefPtr<gfxFont> firstFont =
      aParams.provider->GetFontGroup()->GetFirstValidFont();
  bool verticalRun = mTextRun->IsVertical();
  bool useVerticalMetrics = verticalRun && mTextRun->UseCenterBaseline();
  bool rightUnderline = useVerticalMetrics && IsUnderlineRight(*Style());
  const auto kDecoration = rightUnderline ? StyleTextDecorationLine::OVERLINE
                                          : StyleTextDecorationLine::UNDERLINE;
  gfxFont::Metrics decorationMetrics(
      firstFont->GetMetrics(useVerticalMetrics ? nsFontMetrics::eVertical
                                               : nsFontMetrics::eHorizontal));
  decorationMetrics.underlineOffset =
      aParams.provider->GetFontGroup()->GetUnderlineOffset();

  const gfxTextRun::Range& contentRange = aParams.contentRange;
  gfxFloat startIOffset = verticalRun
                              ? aParams.textBaselinePt.y - aParams.framePt.y
                              : aParams.textBaselinePt.x - aParams.framePt.x;
  SelectionRangeIterator iterator(selectionRanges, contentRange,
                                  *aParams.provider, mTextRun, startIOffset);
  gfxFloat iOffset, hyphenWidth;
  Range range;
  int32_t app = aParams.textPaintStyle->PresContext()->AppUnitsPerDevPixel();
  // XXX aTextBaselinePt is in AppUnits, shouldn't it be nsFloatPoint?
  Point pt;
  if (verticalRun) {
    pt.x = (aParams.textBaselinePt.x - mAscent) / app;
  } else {
    pt.y = (aParams.textBaselinePt.y - mAscent) / app;
  }
  AutoTArray<SelectionType, 1> nextSelectionTypes;
  AutoTArray<RefPtr<nsAtom>, 1> highlightNames;
  AutoTArray<TextRangeStyle, 1> selectedStyles;

  while (iterator.GetNextSegment(&iOffset, &range, &hyphenWidth,
                                 nextSelectionTypes, highlightNames,
                                 selectedStyles)) {
    gfxFloat advance =
        hyphenWidth + mTextRun->GetAdvanceWidth(range, aParams.provider);
    for (size_t index = 0; index < nextSelectionTypes.Length(); ++index) {
      if (nextSelectionTypes[index] == aSelectionType) {
        if (verticalRun) {
          pt.y = (aParams.framePt.y + iOffset -
                  (mTextRun->IsInlineReversed() ? advance : 0)) /
                 app;
        } else {
          pt.x = (aParams.framePt.x + iOffset -
                  (mTextRun->IsInlineReversed() ? advance : 0)) /
                 app;
        }
        gfxFloat width = Abs(advance) / app;
        gfxFloat xInFrame = pt.x - (aParams.framePt.x / app);
        DrawSelectionDecorations(aParams.context, aParams.dirtyRect,
                                 aSelectionType, *aParams.textPaintStyle,
                                 selectedStyles[index], pt, xInFrame, width,
                                 mAscent / app, decorationMetrics,
                                 aParams.callbacks, verticalRun, kDecoration);
      }
    }
    iterator.UpdateWithAdvance(advance);
  }
}

bool nsTextFrame::PaintTextWithSelection(
    const PaintTextSelectionParams& aParams, const ClipEdges& aClipEdges) {
  NS_ASSERTION(GetContent()->IsMaybeSelected(), "wrong paint path");

  UniquePtr<SelectionDetails> details = GetSelectionDetails();
  if (!details) {
    return false;
  }

  SelectionTypeMask allSelectionTypeMask;
  if (!PaintTextWithSelectionColors(aParams, details, &allSelectionTypeMask,
                                    aClipEdges)) {
    return false;
  }
  // Iterate through just the selection rawSelectionTypes that paint decorations
  // and paint decorations for any that actually occur in this frame. Paint
  // higher-numbered selection rawSelectionTypes below lower-numered ones on the
  // general principal that lower-numbered selections are higher priority.
  allSelectionTypeMask &= kSelectionTypesWithDecorations;
  MOZ_ASSERT(kPresentSelectionTypes[0] == SelectionType::eNormal,
             "The following for loop assumes that the first item of "
             "kPresentSelectionTypes is SelectionType::eNormal");
  for (size_t i = std::size(kPresentSelectionTypes) - 1; i >= 1; --i) {
    SelectionType selectionType = kPresentSelectionTypes[i];
    if (ToSelectionTypeMask(selectionType) & allSelectionTypeMask) {
      // There is some selection of this selectionType. Try to paint its
      // decorations (there might not be any for this type but that's OK,
      // PaintTextSelectionDecorations will exit early).
      PaintTextSelectionDecorations(aParams, details, selectionType);
    }
  }

  return true;
}

void nsTextFrame::DrawEmphasisMarks(gfxContext* aContext, WritingMode aWM,
                                    const gfx::Point& aTextBaselinePt,
                                    const gfx::Point& aFramePt, Range aRange,
                                    const nscolor* aDecorationOverrideColor,
                                    PropertyProvider* aProvider) {
  const EmphasisMarkInfo* info = GetProperty(EmphasisMarkProperty());
  if (!info) {
    return;
  }

  bool isTextCombined = Style()->IsTextCombined();
  if (isTextCombined && !aWM.IsVertical()) {
    // XXX This only happens when the parent is display:contents with an
    // orthogonal writing mode. This should be rare, and don't have use
    // cases, so we don't care. It is non-trivial to implement a sane
    // behavior for that case: if you treat the text as not combined,
    // the marks would spread wider than the text (which is rendered as
    // combined); if you try to draw a single mark, selecting part of
    // the text could dynamically create multiple new marks.
    NS_WARNING("Give up on combined text with horizontal wm");
    return;
  }
  nscolor color =
      aDecorationOverrideColor
          ? *aDecorationOverrideColor
          : nsLayoutUtils::GetTextColor(this, &nsStyleText::mTextEmphasisColor);
  aContext->SetColor(sRGBColor::FromABGR(color));
  gfx::Point pt;
  if (!isTextCombined) {
    pt = aTextBaselinePt;
  } else {
    MOZ_ASSERT(aWM.IsVertical());
    pt = aFramePt;
    if (aWM.IsVerticalRL()) {
      pt.x += GetSize().width - GetLogicalBaseline(aWM);
    } else {
      pt.x += GetLogicalBaseline(aWM);
    }
  }
  if (!aWM.IsVertical()) {
    pt.y += info->baselineOffset;
  } else {
    if (aWM.IsVerticalRL()) {
      pt.x -= info->baselineOffset;
    } else {
      pt.x += info->baselineOffset;
    }
  }
  if (!isTextCombined) {
    mTextRun->DrawEmphasisMarks(aContext, info->textRun.get(), info->advance,
                                pt, aRange, aProvider,
                                PresContext()->FontPaletteCache());
  } else {
    pt.y += (GetSize().height - info->advance) / 2;
    gfxTextRun::DrawParams params(aContext, PresContext()->FontPaletteCache());
    info->textRun->Draw(Range(info->textRun.get()), pt, params);
  }
}

nscolor nsTextFrame::GetCaretColorAt(int32_t aOffset) {
  MOZ_ASSERT(aOffset >= 0, "aOffset must be positive");

  nscolor result = nsIFrame::GetCaretColorAt(aOffset);
  gfxSkipCharsIterator iter = EnsureTextRun(nsTextFrame::eInflated);
  PropertyProvider provider(this, iter, nsTextFrame::eInflated, mFontMetrics);
  int32_t contentOffset = provider.GetStart().GetOriginalOffset();
  int32_t contentLength = provider.GetOriginalLength();
  MOZ_ASSERT(
      aOffset >= contentOffset && aOffset <= contentOffset + contentLength,
      "aOffset must be in the frame's range");

  int32_t offsetInFrame = aOffset - contentOffset;
  if (offsetInFrame < 0 || offsetInFrame >= contentLength) {
    return result;
  }

  bool isSolidTextColor = true;
  if (IsInSVGTextSubtree()) {
    const nsStyleSVG* style = StyleSVG();
    if (!style->mFill.kind.IsNone() && !style->mFill.kind.IsColor()) {
      isSolidTextColor = false;
    }
  }

  nsTextPaintStyle textPaintStyle(this);
  textPaintStyle.SetResolveColors(isSolidTextColor);
  UniquePtr<SelectionDetails> details = GetSelectionDetails();
  SelectionType selectionType = SelectionType::eNone;
  for (SelectionDetails* sdptr = details.get(); sdptr;
       sdptr = sdptr->mNext.get()) {
    int32_t start = std::max(0, sdptr->mStart - contentOffset);
    int32_t end = std::min(contentLength, sdptr->mEnd - contentOffset);
    if (start <= offsetInFrame && offsetInFrame < end &&
        (selectionType == SelectionType::eNone ||
         sdptr->mSelectionType < selectionType)) {
      nscolor foreground, background;
      if (GetSelectionTextColors(sdptr->mSelectionType,
                                 sdptr->mHighlightData.mHighlightName,
                                 textPaintStyle, sdptr->mTextRangeStyle,
                                 &foreground, &background)) {
        if (!isSolidTextColor && NS_IS_SELECTION_SPECIAL_COLOR(foreground)) {
          result = NS_RGBA(0, 0, 0, 255);
        } else {
          result = foreground;
        }
        selectionType = sdptr->mSelectionType;
      }
    }
  }

  return result;
}

static gfxTextRun::Range ComputeTransformedRange(
    nsTextFrame::PropertyProvider& aProvider) {
  gfxSkipCharsIterator iter(aProvider.GetStart());
  uint32_t start = iter.GetSkippedOffset();
  iter.AdvanceOriginal(aProvider.GetOriginalLength());
  return gfxTextRun::Range(start, iter.GetSkippedOffset());
}

bool nsTextFrame::MeasureCharClippedText(nscoord aVisIStartEdge,
                                         nscoord aVisIEndEdge,
                                         nscoord* aSnappedStartEdge,
                                         nscoord* aSnappedEndEdge) {
  // We need a *reference* rendering context (not one that might have a
  // transform), so we don't have a rendering context argument.
  // XXX get the block and line passed to us somehow! This is slow!
  gfxSkipCharsIterator iter = EnsureTextRun(nsTextFrame::eInflated);
  if (!mTextRun) {
    return false;
  }

  PropertyProvider provider(this, iter, nsTextFrame::eInflated, mFontMetrics);
  // Trim trailing whitespace
  provider.InitializeForDisplay(true);

  Range range = ComputeTransformedRange(provider);
  uint32_t startOffset = range.start;
  uint32_t maxLength = range.Length();
  return MeasureCharClippedText(provider, aVisIStartEdge, aVisIEndEdge,
                                &startOffset, &maxLength, aSnappedStartEdge,
                                aSnappedEndEdge);
}

static uint32_t GetClusterLength(const gfxTextRun* aTextRun,
                                 uint32_t aStartOffset, uint32_t aMaxLength) {
  uint32_t clusterLength = 0;
  while (++clusterLength < aMaxLength) {
    if (aTextRun->IsClusterStart(aStartOffset + clusterLength)) {
      return clusterLength;
    }
  }
  return aMaxLength;
}

bool nsTextFrame::MeasureCharClippedText(
    PropertyProvider& aProvider, nscoord aVisIStartEdge, nscoord aVisIEndEdge,
    uint32_t* aStartOffset, uint32_t* aMaxLength, nscoord* aSnappedStartEdge,
    nscoord* aSnappedEndEdge) {
  *aSnappedStartEdge = 0;
  *aSnappedEndEdge = 0;
  if (aVisIStartEdge <= 0 && aVisIEndEdge <= 0) {
    return true;
  }

  uint32_t offset = *aStartOffset;
  uint32_t maxLength = *aMaxLength;
  const nscoord frameISize = ISize();
  const bool rtl = mTextRun->IsRightToLeft();
  gfxFloat advanceWidth = 0;
  const nscoord startEdge = rtl ? aVisIEndEdge : aVisIStartEdge;
  if (startEdge > 0) {
    const gfxFloat maxAdvance = gfxFloat(startEdge);
    while (maxLength > 0) {
      uint32_t clusterLength = GetClusterLength(mTextRun, offset, maxLength);
      advanceWidth += mTextRun->GetAdvanceWidth(
          Range(offset, offset + clusterLength), &aProvider);
      maxLength -= clusterLength;
      offset += clusterLength;
      if (advanceWidth >= maxAdvance) {
        break;
      }
    }
    nscoord* snappedStartEdge = rtl ? aSnappedEndEdge : aSnappedStartEdge;
    *snappedStartEdge = NSToCoordFloor(advanceWidth);
    *aStartOffset = offset;
  }

  const nscoord endEdge = rtl ? aVisIStartEdge : aVisIEndEdge;
  if (endEdge > 0) {
    const gfxFloat maxAdvance = gfxFloat(frameISize - endEdge);
    while (maxLength > 0) {
      uint32_t clusterLength = GetClusterLength(mTextRun, offset, maxLength);
      gfxFloat nextAdvance =
          advanceWidth + mTextRun->GetAdvanceWidth(
                             Range(offset, offset + clusterLength), &aProvider);
      if (nextAdvance > maxAdvance) {
        break;
      }
      // This cluster fits, include it.
      advanceWidth = nextAdvance;
      maxLength -= clusterLength;
      offset += clusterLength;
    }
    maxLength = offset - *aStartOffset;
    nscoord* snappedEndEdge = rtl ? aSnappedStartEdge : aSnappedEndEdge;
    *snappedEndEdge = NSToCoordFloor(gfxFloat(frameISize) - advanceWidth);
  }
  *aMaxLength = maxLength;
  return maxLength != 0;
}

void nsTextFrame::PaintShadows(Span<const StyleSimpleShadow> aShadows,
                               const PaintShadowParams& aParams) {
  if (aShadows.IsEmpty()) {
    return;
  }

  gfxTextRun::Metrics shadowMetrics = mTextRun->MeasureText(
      aParams.range, gfxFont::LOOSE_INK_EXTENTS, nullptr, aParams.provider);
  if (GetWritingMode().IsLineInverted()) {
    std::swap(shadowMetrics.mAscent, shadowMetrics.mDescent);
    shadowMetrics.mBoundingBox.y = -shadowMetrics.mBoundingBox.YMost();
  }
  if (HasAnyStateBits(TEXT_HYPHEN_BREAK)) {
    AddHyphenToMetrics(this, mTextRun->IsRightToLeft(), &shadowMetrics,
                       gfxFont::LOOSE_INK_EXTENTS,
                       aParams.context->GetDrawTarget());
  }
  // Add bounds of text decorations
  gfxRect decorationRect(0, -shadowMetrics.mAscent, shadowMetrics.mAdvanceWidth,
                         shadowMetrics.mAscent + shadowMetrics.mDescent);
  shadowMetrics.mBoundingBox.UnionRect(shadowMetrics.mBoundingBox,
                                       decorationRect);

  // If the textrun uses any color or SVG fonts, we need to force use of a mask
  // for shadow rendering even if blur radius is zero.
  // Force disable hardware acceleration for text shadows since it's usually
  // more expensive than just doing it on the CPU.
  uint32_t blurFlags = nsContextBoxBlur::DISABLE_HARDWARE_ACCELERATION_BLUR;
  uint32_t numGlyphRuns;
  const gfxTextRun::GlyphRun* run = mTextRun->GetGlyphRuns(&numGlyphRuns);
  while (numGlyphRuns-- > 0) {
    if (run->mFont->AlwaysNeedsMaskForShadow()) {
      blurFlags |= nsContextBoxBlur::FORCE_MASK;
      break;
    }
    run++;
  }

  if (mTextRun->IsVertical()) {
    std::swap(shadowMetrics.mBoundingBox.x, shadowMetrics.mBoundingBox.y);
    std::swap(shadowMetrics.mBoundingBox.width,
              shadowMetrics.mBoundingBox.height);
  }

  for (const auto& shadow : Reversed(aShadows)) {
    PaintOneShadow(aParams, shadow, shadowMetrics.mBoundingBox, blurFlags);
  }
}

void nsTextFrame::PaintText(const PaintTextParams& aParams,
                            const nscoord aVisIStartEdge,
                            const nscoord aVisIEndEdge,
                            const nsPoint& aToReferenceFrame,
                            const bool aIsSelected,
                            float aOpacity /* = 1.0f */) {
#ifdef DEBUG
  if (IsInSVGTextSubtree()) {
    auto* container =
        nsLayoutUtils::GetClosestFrameOfType(this, LayoutFrameType::SVGText);
    MOZ_ASSERT(container);
    MOZ_ASSERT(!container->HasAnyStateBits(NS_STATE_SVG_CLIPPATH_CHILD) ||
                   !aParams.IsPaintText(),
               "Expecting IsPaintText to be false for a clipPath");
  }
#endif

  // Don't pass in the rendering context here, because we need a
  // *reference* context and rendering context might have some transform
  // in it
  // XXX get the block and line passed to us somehow! This is slow!
  gfxSkipCharsIterator iter = EnsureTextRun(nsTextFrame::eInflated);
  if (!mTextRun) {
    return;
  }

  PropertyProvider provider(this, iter, nsTextFrame::eInflated, mFontMetrics);

  // Trim trailing whitespace, unless we're painting a selection highlight,
  // which should include trailing spaces if present (bug 1146754).
  provider.InitializeForDisplay(!aIsSelected);

  const bool reversed = mTextRun->IsInlineReversed();
  const bool verticalRun = mTextRun->IsVertical();
  WritingMode wm = GetWritingMode();
  const float frameWidth = GetSize().width;
  const float frameHeight = GetSize().height;
  gfx::Point textBaselinePt;
  if (verticalRun) {
    if (wm.IsVerticalLR()) {
      textBaselinePt.x = nsLayoutUtils::GetSnappedBaselineX(
          this, aParams.context, nscoord(aParams.framePt.x), mAscent);
    } else {
      textBaselinePt.x = nsLayoutUtils::GetSnappedBaselineX(
          this, aParams.context, nscoord(aParams.framePt.x) + frameWidth,
          -mAscent);
    }
    textBaselinePt.y = reversed ? aParams.framePt.y.value + frameHeight
                                : aParams.framePt.y.value;
  } else {
    textBaselinePt =
        gfx::Point(reversed ? aParams.framePt.x.value + frameWidth
                            : aParams.framePt.x.value,
                   nsLayoutUtils::GetSnappedBaselineY(
                       this, aParams.context, aParams.framePt.y, mAscent));
  }
  Range range = ComputeTransformedRange(provider);
  uint32_t startOffset = range.start;
  uint32_t maxLength = range.Length();
  nscoord snappedStartEdge, snappedEndEdge;
  if (!MeasureCharClippedText(provider, aVisIStartEdge, aVisIEndEdge,
                              &startOffset, &maxLength, &snappedStartEdge,
                              &snappedEndEdge)) {
    return;
  }
  if (verticalRun) {
    textBaselinePt.y += reversed ? -snappedEndEdge : snappedStartEdge;
  } else {
    textBaselinePt.x += reversed ? -snappedEndEdge : snappedStartEdge;
  }
  const ClipEdges clipEdges(this, aToReferenceFrame, snappedStartEdge,
                            snappedEndEdge);
  nsTextPaintStyle textPaintStyle(this);
  textPaintStyle.SetResolveColors(!aParams.callbacks);

  // Fork off to the (slower) paint-with-selection path if necessary.
  if (aIsSelected) {
    MOZ_ASSERT(aOpacity == 1.0f, "We don't support opacity with selections!");
    gfxSkipCharsIterator tmp(provider.GetStart());
    Range contentRange(
        uint32_t(tmp.ConvertSkippedToOriginal(startOffset)),
        uint32_t(tmp.ConvertSkippedToOriginal(startOffset + maxLength)));
    PaintTextSelectionParams params(aParams);
    params.textBaselinePt = textBaselinePt;
    params.provider = &provider;
    params.contentRange = contentRange;
    params.textPaintStyle = &textPaintStyle;
    params.glyphRange = range;
    if (PaintTextWithSelection(params, clipEdges)) {
      return;
    }
  }

  nscolor foregroundColor = aParams.IsGenerateTextMask()
                                ? NS_RGBA(0, 0, 0, 255)
                                : textPaintStyle.GetTextColor();
  if (aOpacity != 1.0f) {
    gfx::sRGBColor gfxColor = gfx::sRGBColor::FromABGR(foregroundColor);
    gfxColor.a *= aOpacity;
    foregroundColor = gfxColor.ToABGR();
  }

  nscolor textStrokeColor = aParams.IsGenerateTextMask()
                                ? NS_RGBA(0, 0, 0, 255)
                                : textPaintStyle.GetWebkitTextStrokeColor();
  if (aOpacity != 1.0f) {
    gfx::sRGBColor gfxColor = gfx::sRGBColor::FromABGR(textStrokeColor);
    gfxColor.a *= aOpacity;
    textStrokeColor = gfxColor.ToABGR();
  }

  range = Range(startOffset, startOffset + maxLength);
  if (aParams.IsPaintText()) {
    const nsStyleText* textStyle = StyleText();
    PaintShadowParams shadowParams(aParams);
    shadowParams.range = range;
    shadowParams.textBaselinePt = textBaselinePt;
    shadowParams.leftSideOffset = snappedStartEdge;
    shadowParams.provider = &provider;
    shadowParams.callbacks = aParams.callbacks;
    shadowParams.foregroundColor = foregroundColor;
    shadowParams.clipEdges = &clipEdges;
    PaintShadows(textStyle->mTextShadow.AsSpan(), shadowParams);
  }

  gfxFloat advanceWidth;
  DrawTextParams params(aParams.context, PresContext()->FontPaletteCache());
  params.dirtyRect = aParams.dirtyRect;
  params.framePt = aParams.framePt;
  params.provider = &provider;
  params.advanceWidth = &advanceWidth;
  params.textStyle = &textPaintStyle;
  params.textColor = foregroundColor;
  params.textStrokeColor = textStrokeColor;
  params.textStrokeWidth = textPaintStyle.GetWebkitTextStrokeWidth();
  params.clipEdges = &clipEdges;
  params.drawSoftHyphen = HasAnyStateBits(TEXT_HYPHEN_BREAK);
  params.contextPaint = aParams.contextPaint;
  params.callbacks = aParams.callbacks;
  params.glyphRange = range;
  params.fontPalette = StyleFont()->GetFontPaletteAtom();
  params.hasTextShadow = !StyleText()->mTextShadow.IsEmpty();

  DrawText(range, textBaselinePt, params);
}

static void DrawTextRun(const gfxTextRun* aTextRun,
                        const gfx::Point& aTextBaselinePt,
                        gfxTextRun::Range aRange,
                        const nsTextFrame::DrawTextRunParams& aParams,
                        nsTextFrame* aFrame) {
  gfxTextRun::DrawParams params(aParams.context, aParams.paletteCache);
  params.provider = aParams.provider;
  params.advanceWidth = aParams.advanceWidth;
  params.contextPaint = aParams.contextPaint;
  params.fontPalette = aParams.fontPalette;
  params.callbacks = aParams.callbacks;
  params.hasTextShadow = aParams.hasTextShadow;
  if (aParams.callbacks) {
    aParams.callbacks->NotifyBeforeText(aParams.paintingShadows,
                                        aParams.textColor);
    params.drawMode = DrawMode::GLYPH_PATH;
    aTextRun->Draw(aRange, aTextBaselinePt, params);
    aParams.callbacks->NotifyAfterText();
  } else {
    auto* textDrawer = aParams.context->GetTextDrawer();
    if (NS_GET_A(aParams.textColor) != 0 || textDrawer ||
        aParams.textStrokeWidth == 0.0f) {
      aParams.context->SetColor(sRGBColor::FromABGR(aParams.textColor));
    } else {
      params.drawMode = DrawMode::GLYPH_STROKE;
    }

    if ((NS_GET_A(aParams.textStrokeColor) != 0 || textDrawer) &&
        aParams.textStrokeWidth != 0.0f) {
      if (textDrawer) {
        textDrawer->FoundUnsupportedFeature();
        return;
      }
      params.drawMode |= DrawMode::GLYPH_STROKE;

      // Check the paint-order property; if we find stroke before fill,
      // then change mode to GLYPH_STROKE_UNDERNEATH.
      uint32_t paintOrder = aFrame->StyleSVG()->mPaintOrder;
      while (paintOrder) {
        auto component = StylePaintOrder(paintOrder & kPaintOrderMask);
        switch (component) {
          case StylePaintOrder::Fill:
            // Just break the loop, no need to check further
            paintOrder = 0;
            break;
          case StylePaintOrder::Stroke:
            params.drawMode |= DrawMode::GLYPH_STROKE_UNDERNEATH;
            paintOrder = 0;
            break;
          default:
            MOZ_FALLTHROUGH_ASSERT("Unknown paint-order variant, how?");
          case StylePaintOrder::Markers:
          case StylePaintOrder::Normal:
            break;
        }
        paintOrder >>= kPaintOrderShift;
      }

      // Use ROUND joins as they are less likely to produce ugly artifacts
      // when stroking glyphs with sharp angles (see bug 1546985).
      StrokeOptions strokeOpts(aParams.textStrokeWidth, JoinStyle::ROUND);
      params.textStrokeColor = aParams.textStrokeColor;
      params.strokeOpts = &strokeOpts;
      aTextRun->Draw(aRange, aTextBaselinePt, params);
    } else {
      aTextRun->Draw(aRange, aTextBaselinePt, params);
    }
  }
}

void nsTextFrame::DrawTextRun(Range aRange, const gfx::Point& aTextBaselinePt,
                              const DrawTextRunParams& aParams) {
  MOZ_ASSERT(aParams.advanceWidth, "Must provide advanceWidth");

  ::DrawTextRun(mTextRun, aTextBaselinePt, aRange, aParams, this);

  if (aParams.drawSoftHyphen) {
    // Don't use ctx as the context, because we need a reference context here,
    // ctx may be transformed.
    DrawTextRunParams params = aParams;
    params.provider = nullptr;
    params.advanceWidth = nullptr;
    RefPtr<gfxTextRun> hyphenTextRun = GetHyphenTextRun(this, nullptr);
    if (hyphenTextRun) {
      gfx::Point p(aTextBaselinePt);
      bool vertical = GetWritingMode().IsVertical();
      // For right-to-left text runs, the soft-hyphen is positioned at the left
      // of the text.
      float shift = mTextRun->GetDirection() * (*aParams.advanceWidth);
      if (vertical) {
        p.y += shift;
      } else {
        p.x += shift;
      }
      ::DrawTextRun(hyphenTextRun.get(), p, Range(hyphenTextRun.get()), params,
                    this);
    }
  }
}

void nsTextFrame::DrawTextRunAndDecorations(
    Range aRange, const gfx::Point& aTextBaselinePt,
    const DrawTextParams& aParams, const TextDecorations& aDecorations) {
  const gfxFloat app = aParams.textStyle->PresContext()->AppUnitsPerDevPixel();
  // Writing mode of parent frame is used because the text frame may
  // be orthogonal to its parent when text-combine-upright is used or
  // its parent has "display: contents", and in those cases, we want
  // to draw the decoration lines according to parents' direction
  // rather than ours.
  const WritingMode wm = GetParent()->GetWritingMode();
  bool verticalDec = wm.IsVertical();
  bool verticalRun = mTextRun->IsVertical();
  // If the text run and the decoration is orthogonal, we choose the
  // metrics for decoration so that decoration line won't be broken.
  bool useVerticalMetrics = verticalDec != verticalRun
                                ? verticalDec
                                : verticalRun && mTextRun->UseCenterBaseline();

  // XXX aFramePt is in AppUnits, shouldn't it be nsFloatPoint?
  nscoord x = NSToCoordRound(aParams.framePt.x);
  nscoord y = NSToCoordRound(aParams.framePt.y);

  // 'measure' here is textrun-relative, so for a horizontal run it's the
  // width, while for a vertical run it's the height of the decoration
  const nsSize frameSize = GetSize();
  nscoord measure = verticalDec ? frameSize.height : frameSize.width;

  if (verticalDec) {
    aParams.clipEdges->Intersect(&y, &measure);
  } else {
    aParams.clipEdges->Intersect(&x, &measure);
  }

  // decSize is a textrun-relative size, so its 'width' field is actually
  // the run-relative measure, and 'height' will be the line thickness
  gfxFloat ascent = gfxFloat(GetLogicalBaseline(wm)) / app;
  // The starting edge of the frame in block direction
  gfxFloat frameBStart = verticalDec ? aParams.framePt.x : aParams.framePt.y;

  // In vertical-rl mode, block coordinates are measured from the
  // right, so we need to adjust here.
  if (wm.IsVerticalRL()) {
    frameBStart += frameSize.width;
    ascent = -ascent;
  }

  nscoord inflationMinFontSize = nsLayoutUtils::InflationMinFontSizeFor(this);

  PaintDecorationLineParams params;
  params.context = aParams.context;
  params.dirtyRect = aParams.dirtyRect;
  params.overrideColor = aParams.decorationOverrideColor;
  params.callbacks = aParams.callbacks;
  params.glyphRange = aParams.glyphRange;
  params.provider = aParams.provider;
  params.paintingShadows = aParams.paintingShadows;
  // pt is the physical point where the decoration is to be drawn,
  // relative to the frame; one of its coordinates will be updated below.
  params.pt = Point(x / app, y / app);
  Float& bCoord = verticalDec ? params.pt.x.value : params.pt.y.value;
  params.lineSize = Size(measure / app, 0);
  params.ascent = ascent;
  params.vertical = verticalDec;
  params.sidewaysLeft = mTextRun->IsSidewaysLeft();

  // The matrix of the context may have been altered for text-combine-
  // upright. However, we want to draw decoration lines unscaled, thus
  // we need to revert the scaling here.
  gfxContextMatrixAutoSaveRestore scaledRestorer;
  if (Style()->IsTextCombined()) {
    float scaleFactor = GetTextCombineScaleFactor(this);
    if (scaleFactor != 1.0f) {
      scaledRestorer.SetContext(aParams.context);
      gfxMatrix unscaled = aParams.context->CurrentMatrixDouble();
      gfxPoint pt(x / app, y / app);
      if (GetTextRun(nsTextFrame::eInflated)->IsRightToLeft()) {
        pt.x += gfxFloat(frameSize.width) / app;
      }
      unscaled.PreTranslate(pt)
          .PreScale(1.0f / scaleFactor, 1.0f)
          .PreTranslate(-pt);
      aParams.context->SetMatrixDouble(unscaled);
    }
  }

  typedef gfxFont::Metrics Metrics;
  auto paintDecorationLine = [&](const LineDecoration& dec,
                                 gfxFloat Metrics::* lineSize,
                                 StyleTextDecorationLine lineType) {
    if (dec.mStyle == StyleTextDecorationStyle::None) {
      return;
    }

    float inflation =
        GetInflationForTextDecorations(dec.mFrame, inflationMinFontSize);
    const Metrics metrics = GetFirstFontMetrics(
        GetFontGroupForFrame(dec.mFrame, inflation), useVerticalMetrics);

    bCoord = (frameBStart - dec.mBaselineOffset) / app;

    params.color = dec.mColor;
    params.baselineOffset = dec.mBaselineOffset / app;
    params.defaultLineThickness = metrics.*lineSize;
    params.lineSize.height = ComputeDecorationLineThickness(
        dec.mTextDecorationThickness, params.defaultLineThickness, metrics, app,
        dec.mFrame);

    bool swapUnderline = wm.IsCentralBaseline() && IsUnderlineRight(*Style());
    params.offset = ComputeDecorationLineOffset(
        lineType, dec.mTextUnderlinePosition, dec.mTextUnderlineOffset, metrics,
        app, dec.mFrame, wm.IsCentralBaseline(), swapUnderline);

    params.style = dec.mStyle;
    params.allowInkSkipping = dec.mAllowInkSkipping;
    PaintDecorationLine(params);
  };

  // We create a clip region in order to draw the decoration lines only in the
  // range of the text. Restricting the draw area prevents the decoration lines
  // to be drawn multiple times when a part of the text is selected.

  // We skip clipping for the following cases:
  // - drawing the whole text
  // - having different orientation of the text and the writing-mode, such as
  //   "text-combine-upright" (Bug 1408825)
  bool skipClipping =
      aRange.Length() == mTextRun->GetLength() || verticalDec != verticalRun;

  gfxRect clipRect;
  if (!skipClipping) {
    // Get the inline-size according to the specified range.
    gfxFloat clipLength = mTextRun->GetAdvanceWidth(aRange, aParams.provider);
    nsRect visualRect = InkOverflowRect();

    const bool isInlineReversed = mTextRun->IsInlineReversed();
    if (verticalDec) {
      clipRect.x = aParams.framePt.x + visualRect.x;
      clipRect.y = isInlineReversed ? aTextBaselinePt.y.value - clipLength
                                    : aTextBaselinePt.y.value;
      clipRect.width = visualRect.width;
      clipRect.height = clipLength;
    } else {
      clipRect.x = isInlineReversed ? aTextBaselinePt.x.value - clipLength
                                    : aTextBaselinePt.x.value;
      clipRect.y = aParams.framePt.y + visualRect.y;
      clipRect.width = clipLength;
      clipRect.height = visualRect.height;
    }

    clipRect.Scale(1 / app);
    clipRect.Round();
    params.context->Clip(clipRect);
  }

  // Underlines
  params.decoration = StyleTextDecorationLine::UNDERLINE;
  for (const LineDecoration& dec : Reversed(aDecorations.mUnderlines)) {
    paintDecorationLine(dec, &Metrics::underlineSize, params.decoration);
  }

  // Overlines
  params.decoration = StyleTextDecorationLine::OVERLINE;
  for (const LineDecoration& dec : Reversed(aDecorations.mOverlines)) {
    paintDecorationLine(dec, &Metrics::underlineSize, params.decoration);
  }

  // Some glyphs and emphasis marks may extend outside the region, so we reset
  // the clip region here. For an example, italic glyphs.
  if (!skipClipping) {
    params.context->PopClip();
  }

  {
    gfxContextMatrixAutoSaveRestore unscaledRestorer;
    if (scaledRestorer.HasMatrix()) {
      unscaledRestorer.SetContext(aParams.context);
      aParams.context->SetMatrix(scaledRestorer.Matrix());
    }

    // CSS 2.1 mandates that text be painted after over/underlines,
    // and *then* line-throughs
    DrawTextRun(aRange, aTextBaselinePt, aParams);
  }

  // Emphasis marks
  DrawEmphasisMarks(aParams.context, wm, aTextBaselinePt, aParams.framePt,
                    aRange, aParams.decorationOverrideColor, aParams.provider);

  // Re-apply the clip region when the line-through is being drawn.
  if (!skipClipping) {
    params.context->Clip(clipRect);
  }

  // Line-throughs
  params.decoration = StyleTextDecorationLine::LINE_THROUGH;
  for (const LineDecoration& dec : Reversed(aDecorations.mStrikes)) {
    paintDecorationLine(dec, &Metrics::strikeoutSize, params.decoration);
  }

  if (!skipClipping) {
    params.context->PopClip();
  }
}

void nsTextFrame::DrawText(Range aRange, const gfx::Point& aTextBaselinePt,
                           const DrawTextParams& aParams) {
  TextDecorations decorations;
  GetTextDecorations(aParams.textStyle->PresContext(),
                     aParams.callbacks ? eUnresolvedColors : eResolvedColors,
                     decorations);

  // Hide text decorations if we're currently hiding @font-face fallback text
  const bool drawDecorations =
      !aParams.provider->GetFontGroup()->ShouldSkipDrawing() &&
      (decorations.HasDecorationLines() ||
       StyleText()->HasEffectiveTextEmphasis());
  if (drawDecorations) {
    DrawTextRunAndDecorations(aRange, aTextBaselinePt, aParams, decorations);
  } else {
    DrawTextRun(aRange, aTextBaselinePt, aParams);
  }

  if (auto* textDrawer = aParams.context->GetTextDrawer()) {
    textDrawer->TerminateShadows();
  }
}

NS_DECLARE_FRAME_PROPERTY_DELETABLE(WebRenderTextBounds, nsRect)

nsRect nsTextFrame::WebRenderBounds() {
  // WR text bounds is just our ink overflow rect but without shadows. So if we
  // have no shadows, just use the layout bounds.
  if (!StyleText()->HasTextShadow()) {
    return InkOverflowRect();
  }
  nsRect* cachedBounds = GetProperty(WebRenderTextBounds());
  if (!cachedBounds) {
    OverflowAreas overflowAreas;
    ComputeCustomOverflowInternal(overflowAreas, false);
    cachedBounds = new nsRect(overflowAreas.InkOverflow());
    SetProperty(WebRenderTextBounds(), cachedBounds);
  }
  return *cachedBounds;
}

int16_t nsTextFrame::GetSelectionStatus(int16_t* aSelectionFlags) {
  // get the selection controller
  nsCOMPtr<nsISelectionController> selectionController;
  nsresult rv = GetSelectionController(PresContext(),
                                       getter_AddRefs(selectionController));
  if (NS_FAILED(rv) || !selectionController) {
    return nsISelectionController::SELECTION_OFF;
  }

  selectionController->GetSelectionFlags(aSelectionFlags);

  int16_t selectionValue;
  selectionController->GetDisplaySelection(&selectionValue);

  return selectionValue;
}

bool nsTextFrame::IsEntirelyWhitespace() const {
  const nsTextFragment& text = mContent->AsText()->TextFragment();
  for (uint32_t index = 0; index < text.GetLength(); ++index) {
    const char16_t ch = text.CharAt(index);
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == 0xa0) {
      continue;
    }
    return false;
  }
  return true;
}

/**
 * Compute the longest prefix of text whose width is <= aWidth. Return
 * the length of the prefix. Also returns the width of the prefix in aFitWidth.
 */
static uint32_t CountCharsFit(const gfxTextRun* aTextRun,
                              gfxTextRun::Range aRange, gfxFloat aWidth,
                              nsTextFrame::PropertyProvider* aProvider,
                              gfxFloat* aFitWidth) {
  uint32_t last = 0;
  gfxFloat width = 0;
  for (uint32_t i = 1; i <= aRange.Length(); ++i) {
    if (i == aRange.Length() || aTextRun->IsClusterStart(aRange.start + i)) {
      gfxTextRun::Range range(aRange.start + last, aRange.start + i);
      gfxFloat nextWidth = width + aTextRun->GetAdvanceWidth(range, aProvider);
      if (nextWidth > aWidth) {
        break;
      }
      last = i;
      width = nextWidth;
    }
  }
  *aFitWidth = width;
  return last;
}

nsIFrame::ContentOffsets nsTextFrame::CalcContentOffsetsFromFramePoint(
    const nsPoint& aPoint) {
  return GetCharacterOffsetAtFramePointInternal(aPoint, true);
}

nsIFrame::ContentOffsets nsTextFrame::GetCharacterOffsetAtFramePoint(
    const nsPoint& aPoint) {
  return GetCharacterOffsetAtFramePointInternal(aPoint, false);
}

nsIFrame::ContentOffsets nsTextFrame::GetCharacterOffsetAtFramePointInternal(
    const nsPoint& aPoint, bool aForInsertionPoint) {
  ContentOffsets offsets;

  gfxSkipCharsIterator iter = EnsureTextRun(nsTextFrame::eInflated);
  if (!mTextRun) {
    return offsets;
  }

  PropertyProvider provider(this, iter, nsTextFrame::eInflated, mFontMetrics);
  // Trim leading but not trailing whitespace if possible
  provider.InitializeForDisplay(false);
  gfxFloat width =
      mTextRun->IsVertical()
          ? (mTextRun->IsInlineReversed() ? mRect.height - aPoint.y : aPoint.y)
          : (mTextRun->IsInlineReversed() ? mRect.width - aPoint.x : aPoint.x);
  if (Style()->IsTextCombined()) {
    width /= GetTextCombineScaleFactor(this);
  }
  gfxFloat fitWidth;
  Range skippedRange = ComputeTransformedRange(provider);

  uint32_t charsFit =
      CountCharsFit(mTextRun, skippedRange, width, &provider, &fitWidth);

  int32_t selectedOffset;
  if (charsFit < skippedRange.Length()) {
    // charsFit characters fitted, but no more could fit. See if we're
    // more than halfway through the cluster.. If we are, choose the next
    // cluster.
    gfxSkipCharsIterator extraCluster(provider.GetStart());
    extraCluster.AdvanceSkipped(charsFit);

    bool allowSplitLigature = true;  // Allow selection of partial ligature...

    // ...but don't let selection/insertion-point split two Regional Indicator
    // chars that are ligated in the textrun to form a single flag symbol.
    uint32_t offs = extraCluster.GetOriginalOffset();
    const nsTextFragment* frag = TextFragment();
    if (frag->IsHighSurrogateFollowedByLowSurrogateAt(offs) &&
        gfxFontUtils::IsRegionalIndicator(frag->ScalarValueAt(offs))) {
      allowSplitLigature = false;
      if (extraCluster.GetSkippedOffset() > 1 &&
          !mTextRun->IsLigatureGroupStart(extraCluster.GetSkippedOffset())) {
        // CountCharsFit() left us in the middle of the flag; back up over the
        // first character of the ligature, and adjust fitWidth accordingly.
        extraCluster.AdvanceSkipped(-2);  // it's a surrogate pair: 2 code units
        fitWidth -= mTextRun->GetAdvanceWidth(
            Range(extraCluster.GetSkippedOffset(),
                  extraCluster.GetSkippedOffset() + 2),
            &provider);
      }
    }

    gfxSkipCharsIterator extraClusterLastChar(extraCluster);
    FindClusterEnd(
        mTextRun,
        provider.GetStart().GetOriginalOffset() + provider.GetOriginalLength(),
        &extraClusterLastChar, allowSplitLigature);
    PropertyProvider::Spacing spacing;
    Range extraClusterRange(extraCluster.GetSkippedOffset(),
                            extraClusterLastChar.GetSkippedOffset() + 1);
    gfxFloat charWidth =
        mTextRun->GetAdvanceWidth(extraClusterRange, &provider, &spacing);
    charWidth -= spacing.mBefore + spacing.mAfter;
    selectedOffset = !aForInsertionPoint ||
                             width <= fitWidth + spacing.mBefore + charWidth / 2
                         ? extraCluster.GetOriginalOffset()
                         : extraClusterLastChar.GetOriginalOffset() + 1;
  } else {
    // All characters fitted, we're at (or beyond) the end of the text.
    // XXX This could be some pathological situation where negative spacing
    // caused characters to move backwards. We can't really handle that
    // in the current frame system because frames can't have negative
    // intrinsic widths.
    selectedOffset =
        provider.GetStart().GetOriginalOffset() + provider.GetOriginalLength();
    // If we're at the end of a preformatted line which has a terminating
    // linefeed, we want to reduce the offset by one to make sure that the
    // selection is placed before the linefeed character.
    if (HasSignificantTerminalNewline()) {
      --selectedOffset;
    }
  }

  offsets.content = GetContent();
  offsets.offset = offsets.secondaryOffset = selectedOffset;
  offsets.associate = mContentOffset == offsets.offset
                          ? CaretAssociationHint::After
                          : CaretAssociationHint::Before;
  return offsets;
}

bool nsTextFrame::CombineSelectionUnderlineRect(nsPresContext* aPresContext,
                                                nsRect& aRect) {
  if (aRect.IsEmpty()) {
    return false;
  }

  nsRect givenRect = aRect;

  gfxFontGroup* fontGroup = GetInflatedFontGroupForFrame(this);
  RefPtr<gfxFont> firstFont = fontGroup->GetFirstValidFont();
  WritingMode wm = GetWritingMode();
  bool verticalRun = wm.IsVertical();
  bool useVerticalMetrics = verticalRun && !wm.IsSideways();
  const gfxFont::Metrics& metrics =
      firstFont->GetMetrics(useVerticalMetrics ? nsFontMetrics::eVertical
                                               : nsFontMetrics::eHorizontal);

  nsCSSRendering::DecorationRectParams params;
  params.ascent = aPresContext->AppUnitsToGfxUnits(mAscent);

  params.offset = fontGroup->GetUnderlineOffset();

  TextDecorations textDecs;
  GetTextDecorations(aPresContext, eResolvedColors, textDecs);

  params.descentLimit =
      ComputeDescentLimitForSelectionUnderline(aPresContext, metrics);
  params.vertical = verticalRun;

  if (verticalRun) {
    EnsureTextRun(nsTextFrame::eInflated);
    params.sidewaysLeft = mTextRun ? mTextRun->IsSidewaysLeft() : false;
  } else {
    params.sidewaysLeft = false;
  }

  UniquePtr<SelectionDetails> details = GetSelectionDetails();
  for (SelectionDetails* sd = details.get(); sd; sd = sd->mNext.get()) {
    if (sd->mStart == sd->mEnd ||
        sd->mSelectionType == SelectionType::eInvalid ||
        !(ToSelectionTypeMask(sd->mSelectionType) &
          kSelectionTypesWithDecorations) ||
        // URL strikeout does not use underline.
        sd->mSelectionType == SelectionType::eURLStrikeout) {
      continue;
    }

    float relativeSize;
    auto index = nsTextPaintStyle::GetUnderlineStyleIndexForSelectionType(
        sd->mSelectionType);
    if (sd->mSelectionType == SelectionType::eSpellCheck) {
      if (!nsTextPaintStyle::GetSelectionUnderline(
              this, index, nullptr, &relativeSize, &params.style)) {
        continue;
      }
    } else {
      // IME selections
      TextRangeStyle& rangeStyle = sd->mTextRangeStyle;
      if (rangeStyle.IsDefined()) {
        if (!rangeStyle.IsLineStyleDefined() ||
            rangeStyle.mLineStyle == TextRangeStyle::LineStyle::None) {
          continue;
        }
        params.style = ToStyleLineStyle(rangeStyle);
        relativeSize = rangeStyle.mIsBoldLine ? 2.0f : 1.0f;
      } else if (!nsTextPaintStyle::GetSelectionUnderline(
                     this, index, nullptr, &relativeSize, &params.style)) {
        continue;
      }
    }
    nsRect decorationArea;

    const auto& decThickness = StyleTextReset()->mTextDecorationThickness;
    params.lineSize.width = aPresContext->AppUnitsToGfxUnits(aRect.width);
    params.defaultLineThickness = ComputeSelectionUnderlineHeight(
        aPresContext, metrics, sd->mSelectionType);

    params.lineSize.height = ComputeDecorationLineThickness(
        decThickness, params.defaultLineThickness, metrics,
        aPresContext->AppUnitsPerDevPixel(), this);

    bool swapUnderline = wm.IsCentralBaseline() && IsUnderlineRight(*Style());
    const auto* styleText = StyleText();
    params.offset = ComputeDecorationLineOffset(
        textDecs.HasUnderline() ? StyleTextDecorationLine::UNDERLINE
                                : StyleTextDecorationLine::OVERLINE,
        styleText->mTextUnderlinePosition, styleText->mTextUnderlineOffset,
        metrics, aPresContext->AppUnitsPerDevPixel(), this,
        wm.IsCentralBaseline(), swapUnderline);

    relativeSize = std::max(relativeSize, 1.0f);
    params.lineSize.height *= relativeSize;
    params.defaultLineThickness *= relativeSize;
    decorationArea =
        nsCSSRendering::GetTextDecorationRect(aPresContext, params);
    aRect.UnionRect(aRect, decorationArea);
  }

  return !aRect.IsEmpty() && !givenRect.Contains(aRect);
}

bool nsTextFrame::IsFrameSelected() const {
  NS_ASSERTION(!GetContent() || GetContent()->IsMaybeSelected(),
               "use the public IsSelected() instead");
  if (mIsSelected == nsTextFrame::SelectionState::Unknown) {
    const bool isSelected =
        GetContent()->IsSelected(GetContentOffset(), GetContentEnd(),
                                 PresShell()->GetSelectionNodeCache());
    mIsSelected = isSelected ? nsTextFrame::SelectionState::Selected
                             : nsTextFrame::SelectionState::NotSelected;
  } else {
#ifdef DEBUG
    // Assert that the selection caching works.
    const bool isReallySelected =
        GetContent()->IsSelected(GetContentOffset(), GetContentEnd());
    MOZ_ASSERT((mIsSelected == nsTextFrame::SelectionState::Selected) ==
                   isReallySelected,
               "Should have called InvalidateSelectionState()");
#endif
  }

  return mIsSelected == nsTextFrame::SelectionState::Selected;
}

nsTextFrame* nsTextFrame::FindContinuationForOffset(int32_t aOffset) {
  // Use a continuations array to accelerate finding the first continuation
  // of interest, if possible.
  MOZ_ASSERT(!GetPrevContinuation(), "should be called on the primary frame");
  auto* continuations = GetContinuations();
  nsTextFrame* f = this;
  if (continuations) {
    size_t index;
    if (BinarySearchIf(
            *continuations, 0, continuations->Length(),
            [=](nsTextFrame* aFrame) -> int {
              return aOffset - aFrame->GetContentOffset();
            },
            &index)) {
      f = (*continuations)[index];
    } else {
      f = (*continuations)[index ? index - 1 : 0];
    }
  }

  while (f && f->GetContentEnd() <= aOffset) {
    f = f->GetNextContinuation();
  }

  return f;
}

void nsTextFrame::SelectionStateChanged(uint32_t aStart, uint32_t aEnd,
                                        bool aSelected,
                                        SelectionType aSelectionType) {
  NS_ASSERTION(!GetPrevContinuation(),
               "Should only be called for primary frame");
  DEBUG_VERIFY_NOT_DIRTY(GetStateBits());

  InvalidateSelectionState();

  // Selection is collapsed, which can't affect text frame rendering
  if (aStart == aEnd) {
    return;
  }

  nsTextFrame* f = FindContinuationForOffset(aStart);

  nsPresContext* presContext = PresContext();
  while (f && f->GetContentOffset() < int32_t(aEnd)) {
    // We may need to reflow to recompute the overflow area for
    // spellchecking or IME underline if their underline is thicker than
    // the normal decoration line.
    if (ToSelectionTypeMask(aSelectionType) & kSelectionTypesWithDecorations) {
      bool didHaveOverflowingSelection =
          f->HasAnyStateBits(TEXT_SELECTION_UNDERLINE_OVERFLOWED);
      nsRect r(nsPoint(0, 0), GetSize());
      if (didHaveOverflowingSelection ||
          (aSelected && f->CombineSelectionUnderlineRect(presContext, r))) {
        presContext->PresShell()->FrameNeedsReflow(
            f, IntrinsicDirty::FrameAncestorsAndDescendants, NS_FRAME_IS_DIRTY);
      }
    }
    // Selection might change anything. Invalidate the overflow area.
    f->InvalidateFrame();

    f = f->GetNextContinuation();
  }
}

void nsTextFrame::UpdateIteratorFromOffset(const PropertyProvider& aProperties,
                                           int32_t& aInOffset,
                                           gfxSkipCharsIterator& aIter) {
  if (aInOffset < GetContentOffset()) {
    NS_WARNING("offset before this frame's content");
    aInOffset = GetContentOffset();
  } else if (aInOffset > GetContentEnd()) {
    NS_WARNING("offset after this frame's content");
    aInOffset = GetContentEnd();
  }

  int32_t trimmedOffset = aProperties.GetStart().GetOriginalOffset();
  int32_t trimmedEnd = trimmedOffset + aProperties.GetOriginalLength();
  aInOffset = std::max(aInOffset, trimmedOffset);
  aInOffset = std::min(aInOffset, trimmedEnd);

  aIter.SetOriginalOffset(aInOffset);

  if (aInOffset < trimmedEnd && !aIter.IsOriginalCharSkipped() &&
      !mTextRun->IsClusterStart(aIter.GetSkippedOffset())) {
    // Called for non-cluster boundary
    FindClusterStart(mTextRun, trimmedOffset, &aIter);
  }
}

nsPoint nsTextFrame::GetPointFromIterator(const gfxSkipCharsIterator& aIter,
                                          PropertyProvider& aProperties) {
  Range range(aProperties.GetStart().GetSkippedOffset(),
              aIter.GetSkippedOffset());
  gfxFloat advance = mTextRun->GetAdvanceWidth(range, &aProperties);
  nscoord iSize = NSToCoordCeilClamped(advance);
  nsPoint point;

  if (mTextRun->IsVertical()) {
    point.x = 0;
    if (mTextRun->IsInlineReversed()) {
      point.y = mRect.height - iSize;
    } else {
      point.y = iSize;
    }
  } else {
    point.y = 0;
    if (Style()->IsTextCombined()) {
      iSize *= GetTextCombineScaleFactor(this);
    }
    if (mTextRun->IsInlineReversed()) {
      point.x = mRect.width - iSize;
    } else {
      point.x = iSize;
    }
  }
  return point;
}

nsresult nsTextFrame::GetPointFromOffset(int32_t inOffset, nsPoint* outPoint) {
  if (!outPoint) {
    return NS_ERROR_NULL_POINTER;
  }

  DEBUG_VERIFY_NOT_DIRTY(GetStateBits());
  if (HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
    return NS_ERROR_UNEXPECTED;
  }

  if (GetContentLength() <= 0) {
    outPoint->x = 0;
    outPoint->y = 0;
    return NS_OK;
  }

  gfxSkipCharsIterator iter = EnsureTextRun(nsTextFrame::eInflated);
  if (!mTextRun) {
    return NS_ERROR_FAILURE;
  }

  PropertyProvider properties(this, iter, nsTextFrame::eInflated, mFontMetrics);
  // Don't trim trailing whitespace, we want the caret to appear in the right
  // place if it's positioned there
  properties.InitializeForDisplay(false);

  UpdateIteratorFromOffset(properties, inOffset, iter);

  *outPoint = GetPointFromIterator(iter, properties);

  return NS_OK;
}

nsresult nsTextFrame::GetCharacterRectsInRange(int32_t aInOffset,
                                               int32_t aLength,
                                               nsTArray<nsRect>& aRects) {
  DEBUG_VERIFY_NOT_DIRTY(GetStateBits());
  if (HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
    return NS_ERROR_UNEXPECTED;
  }

  if (GetContentLength() <= 0) {
    return NS_OK;
  }

  if (!mTextRun) {
    return NS_ERROR_FAILURE;
  }

  gfxSkipCharsIterator iter = EnsureTextRun(nsTextFrame::eInflated);
  PropertyProvider properties(this, iter, nsTextFrame::eInflated, mFontMetrics);
  // Don't trim trailing whitespace, we want the caret to appear in the right
  // place if it's positioned there
  properties.InitializeForDisplay(false);

  // Initialize iter; this will call FindClusterStart if necessary to align
  // iter to a cluster boundary.
  UpdateIteratorFromOffset(properties, aInOffset, iter);
  nsPoint point = GetPointFromIterator(iter, properties);

  const int32_t kContentEnd = GetContentEnd();
  const int32_t kEndOffset = std::min(aInOffset + aLength, kContentEnd);

  if (aInOffset >= kEndOffset) {
    return NS_OK;
  }

  if (!aRects.SetCapacity(aRects.Length() + kEndOffset - aInOffset,
                          mozilla::fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  do {
    // We'd like to assert here that |point| matches
    // |GetPointFromIterator(iter, properties)|, which in principle should be
    // true; however, testcases with vast dimensions can lead to coordinate
    // overflow and disrupt the calculations. So we've dropped the assertion
    // to avoid tripping the fuzzer unnecessarily.

    // Measure to the end of the cluster.
    nscoord iSize = 0;
    gfxSkipCharsIterator nextIter(iter);
    if (aInOffset < kContentEnd) {
      nextIter.AdvanceOriginal(1);
      if (!nextIter.IsOriginalCharSkipped() &&
          !mTextRun->IsClusterStart(nextIter.GetSkippedOffset()) &&
          nextIter.GetOriginalOffset() < kContentEnd) {
        FindClusterEnd(mTextRun, kContentEnd, &nextIter);
      }

      gfxFloat advance = mTextRun->GetAdvanceWidth(
          Range(iter.GetSkippedOffset(), nextIter.GetSkippedOffset()),
          &properties);
      iSize = NSToCoordCeilClamped(advance);
    }

    // Compute the cluster rect, depending on directionality, and update
    // point to the origin we'll need for the next cluster.
    nsRect rect;
    rect.x = point.x;
    rect.y = point.y;

    if (mTextRun->IsVertical()) {
      rect.width = mRect.width;
      rect.height = iSize;
      if (mTextRun->IsInlineReversed()) {
        // The iterator above returns a point with the origin at the
        // bottom left instead of the top left. Move the origin to the top left
        // by subtracting the character's height.
        rect.y -= rect.height;
        point.y -= iSize;
      } else {
        point.y += iSize;
      }
    } else {
      if (Style()->IsTextCombined()) {
        // The scale factor applies to the inline advance of the glyphs, so it
        // affects both the rect width and the origin point for the next glyph.
        iSize *= GetTextCombineScaleFactor(this);
      }
      rect.width = iSize;
      rect.height = mRect.height;
      if (mTextRun->IsInlineReversed()) {
        // The iterator above returns a point with the origin at the
        // top right instead of the top left. Move the origin to the top left by
        // subtracting the character's width.
        rect.x -= iSize;
        point.x -= iSize;
      } else {
        point.x += iSize;
      }
    }

    // Set the rect for all characters in the cluster.
    int32_t end = std::min(kEndOffset, nextIter.GetOriginalOffset());
    while (aInOffset < end) {
      aRects.AppendElement(rect);
      aInOffset++;
    }

    // Advance iter for the next cluster.
    iter = nextIter;
  } while (aInOffset < kEndOffset);

  return NS_OK;
}

nsresult nsTextFrame::GetChildFrameContainingOffset(int32_t aContentOffset,
                                                    bool aHint,
                                                    int32_t* aOutOffset,
                                                    nsIFrame** aOutFrame) {
  DEBUG_VERIFY_NOT_DIRTY(GetStateBits());
#if 0  // XXXrbs disable due to bug 310227
  if (HasAnyStateBits(NS_FRAME_IS_DIRTY))
    return NS_ERROR_UNEXPECTED;
#endif

  NS_ASSERTION(aOutOffset && aOutFrame, "Bad out parameters");
  NS_ASSERTION(aContentOffset >= 0,
               "Negative content offset, existing code was very broken!");
  nsIFrame* primaryFrame = mContent->GetPrimaryFrame();
  if (this != primaryFrame) {
    // This call needs to happen on the primary frame
    return primaryFrame->GetChildFrameContainingOffset(aContentOffset, aHint,
                                                       aOutOffset, aOutFrame);
  }

  nsTextFrame* f = this;
  int32_t offset = mContentOffset;

  // Try to look up the offset to frame property
  nsTextFrame* cachedFrame = GetProperty(OffsetToFrameProperty());

  if (cachedFrame) {
    f = cachedFrame;
    offset = f->GetContentOffset();

    f->RemoveStateBits(TEXT_IN_OFFSET_CACHE);
  }

  if ((aContentOffset >= offset) && (aHint || aContentOffset != offset)) {
    while (true) {
      nsTextFrame* next = f->GetNextContinuation();
      if (!next || aContentOffset < next->GetContentOffset()) {
        break;
      }
      if (aContentOffset == next->GetContentOffset()) {
        if (aHint) {
          f = next;
          if (f->GetContentLength() == 0) {
            continue;  // use the last of the empty frames with this offset
          }
        }
        break;
      }
      f = next;
    }
  } else {
    while (true) {
      nsTextFrame* prev = f->GetPrevContinuation();
      if (!prev || aContentOffset > f->GetContentOffset()) {
        break;
      }
      if (aContentOffset == f->GetContentOffset()) {
        if (!aHint) {
          f = prev;
          if (f->GetContentLength() == 0) {
            continue;  // use the first of the empty frames with this offset
          }
        }
        break;
      }
      f = prev;
    }
  }

  *aOutOffset = aContentOffset - f->GetContentOffset();
  *aOutFrame = f;

  // cache the frame we found
  SetProperty(OffsetToFrameProperty(), f);
  f->AddStateBits(TEXT_IN_OFFSET_CACHE);

  return NS_OK;
}

nsIFrame::FrameSearchResult nsTextFrame::PeekOffsetNoAmount(bool aForward,
                                                            int32_t* aOffset) {
  NS_ASSERTION(aOffset && *aOffset <= GetContentLength(),
               "aOffset out of range");

  gfxSkipCharsIterator iter = EnsureTextRun(nsTextFrame::eInflated);
  if (!mTextRun) {
    return CONTINUE_EMPTY;
  }

  TrimmedOffsets trimmed = GetTrimmedOffsets(TextFragment());
  // Check whether there are nonskipped characters in the trimmmed range
  return (iter.ConvertOriginalToSkipped(trimmed.GetEnd()) >
          iter.ConvertOriginalToSkipped(trimmed.mStart))
             ? FOUND
             : CONTINUE;
}

/**
 * This class iterates through the clusters before or after the given
 * aPosition (which is a content offset). You can test each cluster
 * to see if it's whitespace (as far as selection/caret movement is concerned),
 * or punctuation, or if there is a word break before the cluster. ("Before"
 * is interpreted according to aDirection, so if aDirection is -1, "before"
 * means actually *after* the cluster content.)
 */
class MOZ_STACK_CLASS ClusterIterator {
 public:
  ClusterIterator(nsTextFrame* aTextFrame, int32_t aPosition,
                  int32_t aDirection, nsString& aContext,
                  bool aTrimSpaces = true);

  bool NextCluster();
  bool IsInlineWhitespace() const;
  bool IsNewline() const;
  bool IsPunctuation() const;
  bool HaveWordBreakBefore() const { return mHaveWordBreak; }

  // Get the charIndex that corresponds to the "before" side of the current
  // character, according to the direction of iteration: so for a forward
  // iterator, this is simply mCharIndex, while for a reverse iterator it will
  // be mCharIndex + <number of code units in the character>.
  int32_t GetBeforeOffset() const {
    MOZ_ASSERT(mCharIndex >= 0);
    return mDirection < 0 ? GetAfterInternal() : mCharIndex;
  }
  // Get the charIndex that corresponds to the "before" side of the current
  // character, according to the direction of iteration: the opposite side
  // to what GetBeforeOffset returns.
  int32_t GetAfterOffset() const {
    MOZ_ASSERT(mCharIndex >= 0);
    return mDirection > 0 ? GetAfterInternal() : mCharIndex;
  }

 private:
  // Helper for Get{After,Before}Offset; returns the charIndex after the
  // current position in the text, accounting for surrogate pairs.
  int32_t GetAfterInternal() const;

  gfxSkipCharsIterator mIterator;
  // Usually, mFrag is pointer to `dom::CharacterData::mText`.  However, if
  // we're in a password field, this points `mMaskedFrag`.
  const nsTextFragment* mFrag;
  // If we're in a password field, this is initialized with mask characters.
  nsTextFragment mMaskedFrag;
  nsTextFrame* mTextFrame;
  int32_t mDirection;  // +1 or -1, or 0 to indicate failure
  int32_t mCharIndex;
  nsTextFrame::TrimmedOffsets mTrimmed;
  nsTArray<bool> mWordBreaks;
  bool mHaveWordBreak;
};

static bool IsAcceptableCaretPosition(const gfxSkipCharsIterator& aIter,
                                      bool aRespectClusters,
                                      const gfxTextRun* aTextRun,
                                      nsTextFrame* aFrame) {
  if (aIter.IsOriginalCharSkipped()) {
    return false;
  }
  uint32_t index = aIter.GetSkippedOffset();
  if (aRespectClusters && !aTextRun->IsClusterStart(index)) {
    return false;
  }
  if (index > 0) {
    // Check whether the proposed position is in between the two halves of a
    // surrogate pair, before a Variation Selector character, or within a
    // ligated emoji sequence; if so, this is not a valid character boundary.
    // (In the case where we are respecting clusters, we won't actually get
    // this far because the low surrogate is also marked as non-clusterStart
    // so we'll return FALSE above.)
    const uint32_t offs = AssertedCast<uint32_t>(aIter.GetOriginalOffset());
    const nsTextFragment* frag = aFrame->TextFragment();
    const char16_t ch = frag->CharAt(offs);

    if (gfxFontUtils::IsVarSelector(ch) ||
        frag->IsLowSurrogateFollowingHighSurrogateAt(offs) ||
        (!aTextRun->IsLigatureGroupStart(index) &&
         (unicode::GetEmojiPresentation(ch) == unicode::EmojiDefault ||
          (unicode::GetEmojiPresentation(ch) == unicode::TextDefault &&
           offs + 1 < frag->GetLength() &&
           frag->CharAt(offs + 1) == gfxFontUtils::kUnicodeVS16)))) {
      return false;
    }

    // If the proposed position is before a high surrogate, we need to decode
    // the surrogate pair (if valid) and check the resulting character.
    if (NS_IS_HIGH_SURROGATE(ch)) {
      if (const char32_t ucs4 = frag->ScalarValueAt(offs)) {
        // If the character is a (Plane-14) variation selector,
        // or an emoji character that is ligated with the previous
        // character (i.e. part of a Regional-Indicator flag pair,
        // or an emoji-ZWJ sequence), this is not a valid boundary.
        if (gfxFontUtils::IsVarSelector(ucs4) ||
            (!aTextRun->IsLigatureGroupStart(index) &&
             unicode::GetEmojiPresentation(ucs4) == unicode::EmojiDefault)) {
          return false;
        }
      }
    }
  }
  return true;
}

nsIFrame::FrameSearchResult nsTextFrame::PeekOffsetCharacter(
    bool aForward, int32_t* aOffset, PeekOffsetCharacterOptions aOptions) {
  int32_t contentLength = GetContentLength();
  NS_ASSERTION(aOffset && *aOffset <= contentLength, "aOffset out of range");

  if (!aOptions.mIgnoreUserStyleAll) {
    StyleUserSelect selectStyle;
    Unused << IsSelectable(&selectStyle);
    if (selectStyle == StyleUserSelect::All) {
      return CONTINUE_UNSELECTABLE;
    }
  }

  gfxSkipCharsIterator iter = EnsureTextRun(nsTextFrame::eInflated);
  if (!mTextRun) {
    return CONTINUE_EMPTY;
  }

  TrimmedOffsets trimmed =
      GetTrimmedOffsets(TextFragment(), TrimmedOffsetFlags::NoTrimAfter);

  // A negative offset means "end of frame".
  int32_t startOffset =
      GetContentOffset() + (*aOffset < 0 ? contentLength : *aOffset);

  if (!aForward) {
    // If at the beginning of the line, look at the previous continuation
    for (int32_t i = std::min(trimmed.GetEnd(), startOffset) - 1;
         i >= trimmed.mStart; --i) {
      iter.SetOriginalOffset(i);
      if (IsAcceptableCaretPosition(iter, aOptions.mRespectClusters, mTextRun,
                                    this)) {
        *aOffset = i - mContentOffset;
        return FOUND;
      }
    }
    *aOffset = 0;
  } else {
    // If we're at the end of a line, look at the next continuation
    iter.SetOriginalOffset(startOffset);
    if (startOffset <= trimmed.GetEnd() &&
        !(startOffset < trimmed.GetEnd() &&
          StyleText()->NewlineIsSignificant(this) &&
          iter.GetSkippedOffset() < mTextRun->GetLength() &&
          mTextRun->CharIsNewline(iter.GetSkippedOffset()))) {
      for (int32_t i = startOffset + 1; i <= trimmed.GetEnd(); ++i) {
        iter.SetOriginalOffset(i);
        if (i == trimmed.GetEnd() ||
            IsAcceptableCaretPosition(iter, aOptions.mRespectClusters, mTextRun,
                                      this)) {
          *aOffset = i - mContentOffset;
          return FOUND;
        }
      }
    }
    *aOffset = contentLength;
  }

  return CONTINUE;
}

bool ClusterIterator::IsInlineWhitespace() const {
  NS_ASSERTION(mCharIndex >= 0, "No cluster selected");
  return IsSelectionInlineWhitespace(mFrag, mCharIndex);
}

bool ClusterIterator::IsNewline() const {
  NS_ASSERTION(mCharIndex >= 0, "No cluster selected");
  return IsSelectionNewline(mFrag, mCharIndex);
}

bool ClusterIterator::IsPunctuation() const {
  NS_ASSERTION(mCharIndex >= 0, "No cluster selected");
  const char16_t ch = mFrag->CharAt(AssertedCast<uint32_t>(mCharIndex));
  return mozilla::IsPunctuationForWordSelect(ch);
}

int32_t ClusterIterator::GetAfterInternal() const {
  if (mFrag->IsHighSurrogateFollowedByLowSurrogateAt(
          AssertedCast<uint32_t>(mCharIndex))) {
    return mCharIndex + 2;
  }
  return mCharIndex + 1;
}

bool ClusterIterator::NextCluster() {
  if (!mDirection) {
    return false;
  }
  const gfxTextRun* textRun = mTextFrame->GetTextRun(nsTextFrame::eInflated);

  mHaveWordBreak = false;
  while (true) {
    bool keepGoing = false;
    if (mDirection > 0) {
      if (mIterator.GetOriginalOffset() >= mTrimmed.GetEnd()) {
        return false;
      }
      keepGoing = mIterator.IsOriginalCharSkipped() ||
                  mIterator.GetOriginalOffset() < mTrimmed.mStart ||
                  !textRun->IsClusterStart(mIterator.GetSkippedOffset());
      mCharIndex = mIterator.GetOriginalOffset();
      mIterator.AdvanceOriginal(1);
    } else {
      if (mIterator.GetOriginalOffset() <= mTrimmed.mStart) {
        // Trimming can skip backward word breakers, see bug 1667138
        return mHaveWordBreak;
      }
      mIterator.AdvanceOriginal(-1);
      keepGoing = mIterator.IsOriginalCharSkipped() ||
                  mIterator.GetOriginalOffset() >= mTrimmed.GetEnd() ||
                  !textRun->IsClusterStart(mIterator.GetSkippedOffset());
      mCharIndex = mIterator.GetOriginalOffset();
    }

    if (mWordBreaks[GetBeforeOffset() - mTextFrame->GetContentOffset()]) {
      mHaveWordBreak = true;
    }
    if (!keepGoing) {
      return true;
    }
  }
}

ClusterIterator::ClusterIterator(nsTextFrame* aTextFrame, int32_t aPosition,
                                 int32_t aDirection, nsString& aContext,
                                 bool aTrimSpaces)
    : mIterator(aTextFrame->EnsureTextRun(nsTextFrame::eInflated)),
      mTextFrame(aTextFrame),
      mDirection(aDirection),
      mCharIndex(-1),
      mHaveWordBreak(false) {
  gfxTextRun* textRun = aTextFrame->GetTextRun(nsTextFrame::eInflated);
  if (!textRun) {
    mDirection = 0;  // signal failure
    return;
  }

  mFrag = aTextFrame->TextFragment();

  const uint32_t textOffset =
      AssertedCast<uint32_t>(aTextFrame->GetContentOffset());
  const uint32_t textLen =
      AssertedCast<uint32_t>(aTextFrame->GetContentLength());

  // If we're in a password field, some characters may be masked.  In such
  // case, we need to treat each masked character as a mask character since
  // we shouldn't expose word boundary which is hidden by the masking.
  if (aTextFrame->GetContent() && mFrag->GetLength() > 0 &&
      aTextFrame->GetContent()->HasFlag(NS_MAYBE_MASKED) &&
      (textRun->GetFlags2() & nsTextFrameUtils::Flags::IsTransformed)) {
    const char16_t kPasswordMask = TextEditor::PasswordMask();
    const nsTransformedTextRun* transformedTextRun =
        static_cast<const nsTransformedTextRun*>(textRun);
    // Use nsString and not nsAutoString so that we get a nsStringBuffer which
    // can be just AddRefed in `mMaskedFrag`.
    nsString maskedText;
    maskedText.SetCapacity(mFrag->GetLength());
    // Note that aTextFrame may not cover the whole of mFrag (in cases with
    // bidi continuations), so we cannot rely on its textrun (and associated
    // styles) being available for the entire fragment.
    uint32_t i = 0;
    // Just copy any text that precedes what aTextFrame covers.
    while (i < textOffset) {
      maskedText.Append(mFrag->CharAt(i++));
    }
    // For the range covered by aTextFrame, mask chars if appropriate.
    while (i < textOffset + textLen) {
      uint32_t skippedOffset = mIterator.ConvertOriginalToSkipped(i);
      bool mask =
          skippedOffset < transformedTextRun->GetLength()
              ? transformedTextRun->mStyles[skippedOffset]->mMaskPassword
              : false;
      if (mFrag->IsHighSurrogateFollowedByLowSurrogateAt(i)) {
        if (mask) {
          maskedText.Append(kPasswordMask);
          maskedText.Append(kPasswordMask);
        } else {
          maskedText.Append(mFrag->CharAt(i));
          maskedText.Append(mFrag->CharAt(i + 1));
        }
        i += 2;
      } else {
        maskedText.Append(mask ? kPasswordMask : mFrag->CharAt(i));
        ++i;
      }
    }
    // Copy any trailing text from the fragment.
    while (i < mFrag->GetLength()) {
      maskedText.Append(mFrag->CharAt(i++));
    }
    mMaskedFrag.SetTo(maskedText, mFrag->IsBidi(), true);
    mFrag = &mMaskedFrag;
  }

  mIterator.SetOriginalOffset(aPosition);
  mTrimmed = aTextFrame->GetTrimmedOffsets(
      mFrag, aTrimSpaces ? nsTextFrame::TrimmedOffsetFlags::Default
                         : nsTextFrame::TrimmedOffsetFlags::NoTrimAfter |
                               nsTextFrame::TrimmedOffsetFlags::NoTrimBefore);

  // Allocate an extra element to record the word break at the end of the line
  // or text run in mWordBreak[textLen].
  mWordBreaks.AppendElements(textLen + 1);
  PodZero(mWordBreaks.Elements(), textLen + 1);
  uint32_t textStart;
  if (aDirection > 0) {
    if (aContext.IsEmpty()) {
      // No previous context, so it must be the start of a line or text run
      mWordBreaks[0] = true;
    }
    textStart = aContext.Length();
    mFrag->AppendTo(aContext, textOffset, textLen);
  } else {
    if (aContext.IsEmpty()) {
      // No following context, so it must be the end of a line or text run
      mWordBreaks[textLen] = true;
    }
    textStart = 0;
    nsAutoString str;
    mFrag->AppendTo(str, textOffset, textLen);
    aContext.Insert(str, 0);
  }

  const uint32_t textEnd = textStart + textLen;
  intl::WordBreakIteratorUtf16 wordBreakIter(aContext);
  Maybe<uint32_t> nextBreak =
      wordBreakIter.Seek(textStart > 0 ? textStart - 1 : textStart);
  while (nextBreak && *nextBreak <= textEnd) {
    mWordBreaks[*nextBreak - textStart] = true;
    nextBreak = wordBreakIter.Next();
  }

  MOZ_ASSERT(textEnd != aContext.Length() || mWordBreaks[textLen],
             "There should be a word break at the end of a line or text run!");
}

nsIFrame::FrameSearchResult nsTextFrame::PeekOffsetWord(
    bool aForward, bool aWordSelectEatSpace, bool aIsKeyboardSelect,
    int32_t* aOffset, PeekWordState* aState, bool aTrimSpaces) {
  int32_t contentLength = GetContentLength();
  NS_ASSERTION(aOffset && *aOffset <= contentLength, "aOffset out of range");

  StyleUserSelect selectStyle;
  Unused << IsSelectable(&selectStyle);
  if (selectStyle == StyleUserSelect::All) {
    return CONTINUE_UNSELECTABLE;
  }

  int32_t offset =
      GetContentOffset() + (*aOffset < 0 ? contentLength : *aOffset);
  ClusterIterator cIter(this, offset, aForward ? 1 : -1, aState->mContext,
                        aTrimSpaces);

  if (!cIter.NextCluster()) {
    return CONTINUE_EMPTY;
  }

  do {
    bool isPunctuation = cIter.IsPunctuation();
    bool isInlineWhitespace = cIter.IsInlineWhitespace();
    bool isWhitespace = isInlineWhitespace || cIter.IsNewline();
    bool isWordBreakBefore = cIter.HaveWordBreakBefore();
    if (!isWhitespace || isInlineWhitespace) {
      aState->SetSawInlineCharacter();
    }
    if (aWordSelectEatSpace == isWhitespace && !aState->mSawBeforeType) {
      aState->SetSawBeforeType();
      aState->Update(isPunctuation, isWhitespace);
      continue;
    }
    // See if we can break before the current cluster
    if (!aState->mAtStart) {
      bool canBreak;
      if (isPunctuation != aState->mLastCharWasPunctuation) {
        canBreak = BreakWordBetweenPunctuation(aState, aForward, isPunctuation,
                                               isWhitespace, aIsKeyboardSelect);
      } else if (!aState->mLastCharWasWhitespace && !isWhitespace &&
                 !isPunctuation && isWordBreakBefore) {
        // if both the previous and the current character are not white
        // space but this can be word break before, we don't need to eat
        // a white space in this case. This case happens in some languages
        // that their words are not separated by white spaces. E.g.,
        // Japanese and Chinese.
        canBreak = true;
      } else {
        canBreak = isWordBreakBefore && aState->mSawBeforeType &&
                   (aWordSelectEatSpace != isWhitespace);
      }
      if (canBreak) {
        *aOffset = cIter.GetBeforeOffset() - mContentOffset;
        return FOUND;
      }
    }
    aState->Update(isPunctuation, isWhitespace);
  } while (cIter.NextCluster());

  *aOffset = cIter.GetAfterOffset() - mContentOffset;
  return CONTINUE;
}

bool nsTextFrame::HasVisibleText() {
  // Text in the range is visible if there is at least one character in the
  // range that is not skipped and is mapped by this frame (which is the primary
  // frame) or one of its continuations.
  for (nsTextFrame* f = this; f; f = f->GetNextContinuation()) {
    int32_t dummyOffset = 0;
    if (f->PeekOffsetNoAmount(true, &dummyOffset) == FOUND) {
      return true;
    }
  }
  return false;
}

std::pair<int32_t, int32_t> nsTextFrame::GetOffsets() const {
  return std::make_pair(GetContentOffset(), GetContentEnd());
}

static bool IsFirstLetterPrefixPunctuation(uint32_t aChar) {
  switch (mozilla::unicode::GetGeneralCategory(aChar)) {
    case HB_UNICODE_GENERAL_CATEGORY_CONNECT_PUNCTUATION: /* Pc */
    case HB_UNICODE_GENERAL_CATEGORY_DASH_PUNCTUATION:    /* Pd */
    case HB_UNICODE_GENERAL_CATEGORY_CLOSE_PUNCTUATION:   /* Pe */
    case HB_UNICODE_GENERAL_CATEGORY_FINAL_PUNCTUATION:   /* Pf */
    case HB_UNICODE_GENERAL_CATEGORY_INITIAL_PUNCTUATION: /* Pi */
    case HB_UNICODE_GENERAL_CATEGORY_OTHER_PUNCTUATION:   /* Po */
    case HB_UNICODE_GENERAL_CATEGORY_OPEN_PUNCTUATION:    /* Ps */
      return true;
    default:
      return false;
  }
}

static bool IsFirstLetterSuffixPunctuation(uint32_t aChar) {
  switch (mozilla::unicode::GetGeneralCategory(aChar)) {
    case HB_UNICODE_GENERAL_CATEGORY_CONNECT_PUNCTUATION: /* Pc */
    case HB_UNICODE_GENERAL_CATEGORY_CLOSE_PUNCTUATION:   /* Pe */
    case HB_UNICODE_GENERAL_CATEGORY_FINAL_PUNCTUATION:   /* Pf */
    case HB_UNICODE_GENERAL_CATEGORY_INITIAL_PUNCTUATION: /* Pi */
    case HB_UNICODE_GENERAL_CATEGORY_OTHER_PUNCTUATION:   /* Po */
      return true;
    default:
      return false;
  }
}

static int32_t FindEndOfPrefixPunctuationRun(const nsTextFragment* aFrag,
                                             const gfxTextRun* aTextRun,
                                             gfxSkipCharsIterator* aIter,
                                             int32_t aOffset, int32_t aStart,
                                             int32_t aEnd) {
  int32_t i;
  for (i = aStart; i < aEnd - aOffset; ++i) {
    if (IsFirstLetterPrefixPunctuation(
            aFrag->ScalarValueAt(AssertedCast<uint32_t>(aOffset + i)))) {
      aIter->SetOriginalOffset(aOffset + i);
      FindClusterEnd(aTextRun, aEnd, aIter);
      i = aIter->GetOriginalOffset() - aOffset;
    } else {
      break;
    }
  }
  return i;
}

static int32_t FindEndOfSuffixPunctuationRun(const nsTextFragment* aFrag,
                                             const gfxTextRun* aTextRun,
                                             gfxSkipCharsIterator* aIter,
                                             int32_t aOffset, int32_t aStart,
                                             int32_t aEnd) {
  int32_t i;
  for (i = aStart; i < aEnd - aOffset; ++i) {
    if (IsFirstLetterSuffixPunctuation(
            aFrag->ScalarValueAt(AssertedCast<uint32_t>(aOffset + i)))) {
      aIter->SetOriginalOffset(aOffset + i);
      FindClusterEnd(aTextRun, aEnd, aIter);
      i = aIter->GetOriginalOffset() - aOffset;
    } else {
      break;
    }
  }
  return i;
}

/**
 * Returns true if this text frame completes the first-letter, false
 * if it does not contain a true "letter".
 * If returns true, then it also updates aLength to cover just the first-letter
 * text.
 *
 * XXX :first-letter should be handled during frame construction
 * (and it has a good bit in common with nextBidi)
 *
 * @param aLength an in/out parameter: on entry contains the maximum length to
 * return, on exit returns length of the first-letter fragment (which may
 * include leading and trailing punctuation, for example)
 */
static bool FindFirstLetterRange(const nsTextFragment* aFrag,
                                 const nsAtom* aLang,
                                 const gfxTextRun* aTextRun, int32_t aOffset,
                                 const gfxSkipCharsIterator& aIter,
                                 int32_t* aLength) {
  int32_t length = *aLength;
  int32_t endOffset = aOffset + length;
  gfxSkipCharsIterator iter(aIter);

  // Currently the only language-specific special case we handle here is the
  // Dutch "IJ" digraph.
  auto LangTagIsDutch = [](const nsAtom* aLang) -> bool {
    if (!aLang) {
      return false;
    }
    if (aLang == nsGkAtoms::nl) {
      return true;
    }
    // We don't need to fully parse as a Locale; just check the initial subtag.
    nsDependentAtomString langStr(aLang);
    int32_t index = langStr.FindChar('-');
    if (index > 0) {
      langStr.Truncate(index);
      return langStr.EqualsLiteral("nl");
    }
    return false;
  };

  // Skip any trimmable leading whitespace.
  int32_t i = GetTrimmableWhitespaceCount(aFrag, aOffset, length, 1);
  while (true) {
    // Scan past any leading punctuation. This leaves `j` at the first
    // non-punctuation character.
    int32_t j = FindEndOfPrefixPunctuationRun(aFrag, aTextRun, &iter, aOffset,
                                              i, endOffset);
    if (j == length) {
      return false;
    }

    // Scan past any Unicode whitespace characters after punctuation.
    while (j < length) {
      char16_t ch = aFrag->CharAt(AssertedCast<uint32_t>(aOffset + j));
      // The spec says to allow "characters that belong to the `Zs` Unicode
      // general category _other than_ U+3000" here.
      if (unicode::GetGeneralCategory(ch) ==
              HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR &&
          ch != 0x3000) {
        ++j;
      } else {
        break;
      }
    }
    if (j == length) {
      return false;
    }
    if (j == i) {
      // If no whitespace was found, we've finished the first-letter prefix;
      // if there was some, then go back to check for more punctuation.
      break;
    }
    i = j;
  }

  // If the next character is not a letter, number or symbol, there is no
  // first-letter.
  // Return true so that we don't go on looking, but set aLength to 0.
  const char32_t usv =
      aFrag->ScalarValueAt(AssertedCast<uint32_t>(aOffset + i));
  if (!nsContentUtils::IsAlphanumericOrSymbol(usv)) {
    *aLength = 0;
    return true;
  }

  // Consume another cluster (the actual first letter):

  // For complex scripts such as Indic and SEAsian, where first-letter
  // should extend to entire orthographic "syllable" clusters, we don't
  // want to allow this to split a ligature.
  bool allowSplitLigature;
  bool usesIndicHalfForms = false;

  typedef intl::Script Script;
  Script script = intl::UnicodeProperties::GetScriptCode(usv);
  switch (script) {
    default:
      allowSplitLigature = true;
      break;

    // Don't break regional-indicator ligatures.
    case Script::COMMON:
      allowSplitLigature = !gfxFontUtils::IsRegionalIndicator(usv);
      break;

    // For now, lacking any definitive specification of when to apply this
    // behavior, we'll base the decision on the HarfBuzz shaping engine
    // used for each script: those that are handled by the Indic, Tibetan,
    // Myanmar and SEAsian shapers will apply the "don't split ligatures"
    // rule.

    // Indic
    case Script::BENGALI:
    case Script::DEVANAGARI:
    case Script::GUJARATI:
      usesIndicHalfForms = true;
      [[fallthrough]];

    case Script::GURMUKHI:
    case Script::KANNADA:
    case Script::MALAYALAM:
    case Script::ORIYA:
    case Script::TAMIL:
    case Script::TELUGU:
    case Script::SINHALA:
    case Script::BALINESE:
    case Script::LEPCHA:
    case Script::REJANG:
    case Script::SUNDANESE:
    case Script::JAVANESE:
    case Script::KAITHI:
    case Script::MEETEI_MAYEK:
    case Script::CHAKMA:
    case Script::SHARADA:
    case Script::TAKRI:
    case Script::KHMER:

    // Tibetan
    case Script::TIBETAN:

    // Myanmar
    case Script::MYANMAR:

    // Other SEAsian
    case Script::BUGINESE:
    case Script::NEW_TAI_LUE:
    case Script::CHAM:
    case Script::TAI_THAM:

      // What about Thai/Lao - any special handling needed?
      // Should we special-case Arabic lam-alef?

      allowSplitLigature = false;
      break;
  }

  // NOTE that FindClusterEnd sets the iterator to the last character that is
  // part of the cluster, NOT to the first character beyond it.
  iter.SetOriginalOffset(aOffset + i);
  FindClusterEnd(aTextRun, endOffset, &iter, allowSplitLigature);

  // Index of the last character included in the first-letter cluster.
  i = iter.GetOriginalOffset() - aOffset;

  // Heuristic for Indic scripts that like to form conjuncts:
  // If we ended at a virama that is ligated with the preceding character
  // (e.g. creating a half-form), then don't stop here; include the next
  // cluster as well so that we don't break a conjunct.
  //
  // Unfortunately this cannot distinguish between a letter+virama that ligate
  // to create a half-form (in which case we have a conjunct that should not
  // be broken) and a letter+virama that ligate purely for presentational
  // reasons to position the (visible) virama component (in which case breaking
  // after the virama would be acceptable). So results may be imperfect,
  // depending how the font has chosen to implement visible viramas.
  if (usesIndicHalfForms) {
    while (i + 1 < length &&
           !aTextRun->IsLigatureGroupStart(iter.GetSkippedOffset())) {
      char32_t c = aFrag->ScalarValueAt(AssertedCast<uint32_t>(aOffset + i));
      if (intl::UnicodeProperties::GetCombiningClass(c) ==
          HB_UNICODE_COMBINING_CLASS_VIRAMA) {
        iter.AdvanceOriginal(1);
        FindClusterEnd(aTextRun, endOffset, &iter, allowSplitLigature);
        i = iter.GetOriginalOffset() - aOffset;
      } else {
        break;
      }
    }
  }

  if (i + 1 == length) {
    return true;
  }

  // Check for Dutch "ij" digraph special case, but only if both letters have
  // the same case.
  if (script == Script::LATIN && LangTagIsDutch(aLang)) {
    char16_t ch1 = aFrag->CharAt(AssertedCast<uint32_t>(aOffset + i));
    char16_t ch2 = aFrag->CharAt(AssertedCast<uint32_t>(aOffset + i + 1));
    if ((ch1 == 'i' && ch2 == 'j') || (ch1 == 'I' && ch2 == 'J')) {
      iter.SetOriginalOffset(aOffset + i + 1);
      FindClusterEnd(aTextRun, endOffset, &iter, allowSplitLigature);
      i = iter.GetOriginalOffset() - aOffset;
      if (i + 1 == length) {
        return true;
      }
    }
  }

  // When we reach here, `i` points to the last character of the first-letter
  // cluster, NOT to the first character beyond it. Advance to the next char,
  // ready to check for following whitespace/punctuation:
  ++i;

  while (i < length) {
    // Skip over whitespace, except for word separator characters, before the
    // check for following punctuation. But remember the position before the
    // whitespace, in case we need to reset.
    const int32_t preWS = i;
    while (i < length) {
      char16_t ch = aFrag->CharAt(AssertedCast<uint32_t>(aOffset + i));
      // The spec says the first-letter suffix includes "any intervening
      // typographic space -- characters belonging to the Zs Unicode general
      // category other than U+3000 IDEOGRAPHIC SPACE or a word separator",
      // where "word separator" includes U+0020 and U+00A0.
      if (ch == 0x0020 || ch == 0x00A0 || ch == 0x3000 ||
          unicode::GetGeneralCategory(ch) !=
              HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR) {
        break;
      } else {
        ++i;
      }
    }

    // Consume clusters that start with punctuation.
    const int32_t prePunct = i;
    i = FindEndOfSuffixPunctuationRun(aFrag, aTextRun, &iter, aOffset, i,
                                      endOffset);

    // If we didn't find punctuation here, then we also don't want to include
    // any preceding whitespace, so reset our index.
    if (i == prePunct) {
      i = preWS;
      break;
    }
  }

  if (i < length) {
    *aLength = i;
  }
  return true;
}

static uint32_t FindStartAfterSkippingWhitespace(
    nsTextFrame::PropertyProvider* aProvider,
    nsIFrame::InlineIntrinsicISizeData* aData, const nsStyleText* aTextStyle,
    gfxSkipCharsIterator* aIterator, uint32_t aFlowEndInTextRun) {
  if (aData->mSkipWhitespace) {
    while (aIterator->GetSkippedOffset() < aFlowEndInTextRun &&
           IsTrimmableSpace(aProvider->GetFragment(),
                            aIterator->GetOriginalOffset(), aTextStyle)) {
      aIterator->AdvanceOriginal(1);
    }
  }
  return aIterator->GetSkippedOffset();
}

float nsTextFrame::GetFontSizeInflation() const {
  if (!HasFontSizeInflation()) {
    return 1.0f;
  }
  return GetProperty(FontSizeInflationProperty());
}

void nsTextFrame::SetFontSizeInflation(float aInflation) {
  if (aInflation == 1.0f) {
    if (HasFontSizeInflation()) {
      RemoveStateBits(TEXT_HAS_FONT_INFLATION);
      RemoveProperty(FontSizeInflationProperty());
    }
    return;
  }

  AddStateBits(TEXT_HAS_FONT_INFLATION);
  SetProperty(FontSizeInflationProperty(), aInflation);
}

void nsTextFrame::SetHangableISize(nscoord aISize) {
  MOZ_ASSERT(aISize >= 0, "unexpected negative hangable advance");
  if (aISize <= 0) {
    ClearHangableISize();
    return;
  }
  SetProperty(HangableWhitespaceProperty(), aISize);
  mPropertyFlags |= PropertyFlags::HangableWS;
}

nscoord nsTextFrame::GetHangableISize() const {
  MOZ_ASSERT(!!(mPropertyFlags & PropertyFlags::HangableWS) ==
                 HasProperty(HangableWhitespaceProperty()),
             "flag/property mismatch!");
  return (mPropertyFlags & PropertyFlags::HangableWS)
             ? GetProperty(HangableWhitespaceProperty())
             : 0;
}

void nsTextFrame::ClearHangableISize() {
  if (mPropertyFlags & PropertyFlags::HangableWS) {
    RemoveProperty(HangableWhitespaceProperty());
    mPropertyFlags &= ~PropertyFlags::HangableWS;
  }
}

void nsTextFrame::SetTrimmableWS(gfxTextRun::TrimmableWS aTrimmableWS) {
  MOZ_ASSERT(aTrimmableWS.mAdvance >= 0, "negative trimmable size");
  if (aTrimmableWS.mAdvance <= 0) {
    ClearTrimmableWS();
    return;
  }
  SetProperty(TrimmableWhitespaceProperty(), aTrimmableWS);
  mPropertyFlags |= PropertyFlags::TrimmableWS;
}

gfxTextRun::TrimmableWS nsTextFrame::GetTrimmableWS() const {
  MOZ_ASSERT(!!(mPropertyFlags & PropertyFlags::TrimmableWS) ==
                 HasProperty(TrimmableWhitespaceProperty()),
             "flag/property mismatch!");
  return (mPropertyFlags & PropertyFlags::TrimmableWS)
             ? GetProperty(TrimmableWhitespaceProperty())
             : gfxTextRun::TrimmableWS{};
}

void nsTextFrame::ClearTrimmableWS() {
  if (mPropertyFlags & PropertyFlags::TrimmableWS) {
    RemoveProperty(TrimmableWhitespaceProperty());
    mPropertyFlags &= ~PropertyFlags::TrimmableWS;
  }
}

/* virtual */
void nsTextFrame::MarkIntrinsicISizesDirty() {
  ClearTextRuns();
  nsIFrame::MarkIntrinsicISizesDirty();
}

// XXX this doesn't handle characters shaped by line endings. We need to
// temporarily override the "current line ending" settings.
void nsTextFrame::AddInlineMinISizeForFlow(gfxContext* aRenderingContext,
                                           InlineMinISizeData* aData,
                                           TextRunType aTextRunType) {
  uint32_t flowEndInTextRun;
  gfxSkipCharsIterator iter =
      EnsureTextRun(aTextRunType, aRenderingContext->GetDrawTarget(),
                    aData->LineContainer(), aData->mLine, &flowEndInTextRun);
  gfxTextRun* textRun = GetTextRun(aTextRunType);
  if (!textRun) {
    return;
  }

  // Pass null for the line container. This will disable tab spacing, but that's
  // OK since we can't really handle tabs for intrinsic sizing anyway.
  const nsStyleText* textStyle = StyleText();
  const nsTextFragment* frag = TextFragment();

  // If we're hyphenating, the PropertyProvider needs the actual length;
  // otherwise we can just pass INT32_MAX to mean "all the text"
  int32_t len = INT32_MAX;
  bool hyphenating = frag->GetLength() > 0 &&
                     (textStyle->mHyphens == StyleHyphens::Auto ||
                      (textStyle->mHyphens == StyleHyphens::Manual &&
                       !!(textRun->GetFlags() &
                          gfx::ShapedTextFlags::TEXT_ENABLE_HYPHEN_BREAKS)));
  if (hyphenating) {
    gfxSkipCharsIterator tmp(iter);
    len = std::min<int32_t>(GetContentOffset() + GetInFlowContentLength(),
                            tmp.ConvertSkippedToOriginal(flowEndInTextRun)) -
          iter.GetOriginalOffset();
  }
  PropertyProvider provider(textRun, textStyle, frag, this, iter, len, nullptr,
                            0, aTextRunType, aData->mAtStartOfLine);

  bool collapseWhitespace = !textStyle->WhiteSpaceIsSignificant();
  bool preformatNewlines = textStyle->NewlineIsSignificant(this);
  bool preformatTabs = textStyle->WhiteSpaceIsSignificant();
  bool whitespaceCanHang = textStyle->WhiteSpaceCanHangOrVisuallyCollapse();
  gfxFloat tabWidth = -1;
  uint32_t start = FindStartAfterSkippingWhitespace(&provider, aData, textStyle,
                                                    &iter, flowEndInTextRun);

  // text-combine-upright frame is constantly 1em on inline-axis.
  if (Style()->IsTextCombined()) {
    if (start < flowEndInTextRun && textRun->CanBreakLineBefore(start)) {
      aData->OptionallyBreak();
    }
    aData->mCurrentLine += provider.GetFontMetrics()->EmHeight();
    aData->mTrailingWhitespace = 0;
    return;
  }

  if (textStyle->EffectiveOverflowWrap() == StyleOverflowWrap::Anywhere &&
      textStyle->WordCanWrap(this)) {
    aData->OptionallyBreak();
    aData->mCurrentLine +=
        textRun->GetMinAdvanceWidth(Range(start, flowEndInTextRun));
    aData->mTrailingWhitespace = 0;
    aData->mAtStartOfLine = false;
    aData->OptionallyBreak();
    return;
  }

  AutoTArray<gfxTextRun::HyphenType, BIG_TEXT_NODE_SIZE> hyphBuffer;
  if (hyphenating) {
    if (hyphBuffer.AppendElements(flowEndInTextRun - start, fallible)) {
      provider.GetHyphenationBreaks(Range(start, flowEndInTextRun),
                                    hyphBuffer.Elements());
    } else {
      hyphenating = false;
    }
  }

  for (uint32_t i = start, wordStart = start; i <= flowEndInTextRun; ++i) {
    bool preformattedNewline = false;
    bool preformattedTab = false;
    if (i < flowEndInTextRun) {
      // XXXldb Shouldn't we be including the newline as part of the
      // segment that it ends rather than part of the segment that it
      // starts?
      preformattedNewline = preformatNewlines && textRun->CharIsNewline(i);
      preformattedTab = preformatTabs && textRun->CharIsTab(i);
      if (!textRun->CanBreakLineBefore(i) && !preformattedNewline &&
          !preformattedTab &&
          (!hyphenating ||
           !gfxTextRun::IsOptionalHyphenBreak(hyphBuffer[i - start]))) {
        // we can't break here (and it's not the end of the flow)
        continue;
      }
    }

    if (i > wordStart) {
      nscoord width = NSToCoordCeilClamped(
          textRun->GetAdvanceWidth(Range(wordStart, i), &provider));
      width = std::max(0, width);
      aData->mCurrentLine = NSCoordSaturatingAdd(aData->mCurrentLine, width);
      aData->mAtStartOfLine = false;

      if (collapseWhitespace || whitespaceCanHang) {
        uint32_t trimStart = GetEndOfTrimmedText(frag, textStyle, wordStart, i,
                                                 &iter, whitespaceCanHang);
        if (trimStart == start) {
          // This is *all* trimmable whitespace, so whatever trailingWhitespace
          // we saw previously is still trailing...
          aData->mTrailingWhitespace += width;
        } else {
          // Some non-whitespace so the old trailingWhitespace is no longer
          // trailing
          nscoord wsWidth = NSToCoordCeilClamped(
              textRun->GetAdvanceWidth(Range(trimStart, i), &provider));
          aData->mTrailingWhitespace = std::max(0, wsWidth);
        }
      } else {
        aData->mTrailingWhitespace = 0;
      }
    }

    if (preformattedTab) {
      PropertyProvider::Spacing spacing;
      provider.GetSpacing(Range(i, i + 1), &spacing);
      aData->mCurrentLine += nscoord(spacing.mBefore);
      if (tabWidth < 0) {
        tabWidth = ComputeTabWidthAppUnits(this);
      }
      gfxFloat afterTab = AdvanceToNextTab(aData->mCurrentLine, tabWidth,
                                           provider.MinTabAdvance());
      aData->mCurrentLine = nscoord(afterTab + spacing.mAfter);
      wordStart = i + 1;
    } else if (i < flowEndInTextRun ||
               (i == textRun->GetLength() &&
                (textRun->GetFlags2() &
                 nsTextFrameUtils::Flags::HasTrailingBreak))) {
      if (preformattedNewline) {
        aData->ForceBreak();
      } else if (i < flowEndInTextRun && hyphenating &&
                 gfxTextRun::IsOptionalHyphenBreak(hyphBuffer[i - start])) {
        aData->OptionallyBreak(NSToCoordRound(provider.GetHyphenWidth()));
      } else {
        aData->OptionallyBreak();
      }
      if (aData->mSkipWhitespace) {
        iter.SetSkippedOffset(i);
        wordStart = FindStartAfterSkippingWhitespace(
            &provider, aData, textStyle, &iter, flowEndInTextRun);
      } else {
        wordStart = i;
      }
      provider.SetStartOfLine(iter);
    }
  }

  if (start < flowEndInTextRun) {
    // Check if we have collapsible whitespace at the end
    aData->mSkipWhitespace = IsTrimmableSpace(
        provider.GetFragment(),
        iter.ConvertSkippedToOriginal(flowEndInTextRun - 1), textStyle);
  }
}

bool nsTextFrame::IsCurrentFontInflation(float aInflation) const {
  return fabsf(aInflation - GetFontSizeInflation()) < 1e-6;
}

void nsTextFrame::MaybeSplitFramesForFirstLetter() {
  if (!StaticPrefs::layout_css_intrinsic_size_first_letter_enabled()) {
    return;
  }

  if (GetParent()->IsFloating() && GetContentLength() > 0) {
    // We've already claimed our first-letter content, don't try again.
    return;
  }
  if (GetPrevContinuation()) {
    // This isn't the first part of the first-letter.
    return;
  }

  // Find the length of the first-letter. We need a textrun for this; just bail
  // out if we fail to create it.
  // But in the floating first-letter case, the text is initially all in our
  // next-in-flow, and the float itself is empty. So we need to look at that
  // textrun instead of our own during FindFirstLetterRange.
  nsTextFrame* f = GetParent()->IsFloating() ? GetNextInFlow() : this;
  gfxSkipCharsIterator iter = f->EnsureTextRun(nsTextFrame::eInflated);
  const gfxTextRun* textRun = f->GetTextRun(nsTextFrame::eInflated);

  const nsTextFragment* frag = TextFragment();
  const int32_t length = GetInFlowContentLength();
  const int32_t offset = GetContentOffset();
  int32_t firstLetterLength = length;
  NewlineProperty* cachedNewlineOffset = nullptr;
  int32_t newLineOffset = -1;  // this will be -1 or a content offset
  // This will just return -1 if newlines are not significant.
  int32_t contentNewLineOffset =
      GetContentNewLineOffset(offset, cachedNewlineOffset);
  if (contentNewLineOffset < offset + length) {
    // The new line offset could be outside this frame if the frame has been
    // split by bidi resolution. In that case we won't use it in this reflow
    // (newLineOffset will remain -1), but we will still cache it in mContent
    newLineOffset = contentNewLineOffset;
    if (newLineOffset >= 0) {
      firstLetterLength = newLineOffset - offset;
    }
  }

  if (contentNewLineOffset >= 0 && contentNewLineOffset < offset) {
    // We're in a first-letter frame's first in flow, so if there
    // was a first-letter, we'd be it. However, for one reason
    // or another (e.g., preformatted line break before this text),
    // we're not actually supposed to have first-letter style. So
    // just make a zero-length first-letter.
    firstLetterLength = 0;
  } else {
    // We only pass a language code to FindFirstLetterRange if it was
    // explicit in the content.
    const nsStyleFont* styleFont = StyleFont();
    const nsAtom* lang =
        styleFont->mExplicitLanguage ? styleFont->mLanguage.get() : nullptr;
    FindFirstLetterRange(frag, lang, textRun, offset, iter, &firstLetterLength);
    if (newLineOffset >= 0) {
      // Don't allow a preformatted newline to be part of a first-letter.
      firstLetterLength = std::min(firstLetterLength, length - 1);
    }
  }
  if (firstLetterLength) {
    AddStateBits(TEXT_FIRST_LETTER);
  }

  // Change this frame's length to the first-letter length right now
  // so that when we rebuild the textrun it will be built with the
  // right first-letter boundary.
  SetFirstLetterLength(firstLetterLength);
}

static bool IsUnreflowedLetterFrame(nsIFrame* aFrame) {
  return aFrame->IsLetterFrame() &&
         aFrame->HasAnyStateBits(NS_FRAME_FIRST_REFLOW);
}

// XXX Need to do something here to avoid incremental reflow bugs due to
// first-line changing min-width
/* virtual */
void nsTextFrame::AddInlineMinISize(const IntrinsicSizeInput& aInput,
                                    InlineMinISizeData* aData) {
  // Check if this textframe belongs to a first-letter frame that has not yet
  // been reflowed; if so, we need to deal with splitting off a continuation
  // before we can measure the advance correctly.
  if (IsUnreflowedLetterFrame(GetParent())) {
    MaybeSplitFramesForFirstLetter();
  }

  float inflation = nsLayoutUtils::FontSizeInflationFor(this);
  TextRunType trtype = (inflation == 1.0f) ? eNotInflated : eInflated;

  if (trtype == eInflated && !IsCurrentFontInflation(inflation)) {
    // FIXME: Ideally, if we already have a text run, we'd move it to be
    // the uninflated text run.
    ClearTextRun(nullptr, nsTextFrame::eInflated);
    mFontMetrics = nullptr;
  }

  nsTextFrame* f;
  const gfxTextRun* lastTextRun = nullptr;
  // nsContinuingTextFrame does nothing for AddInlineMinISize; all text frames
  // in the flow are handled right here.
  for (f = this; f; f = f->GetNextContinuation()) {
    // f->GetTextRun(nsTextFrame::eNotInflated) could be null if we
    // haven't set up textruns yet for f.  Except in OOM situations,
    // lastTextRun will only be null for the first text frame.
    if (f == this || f->GetTextRun(trtype) != lastTextRun) {
      nsIFrame* lc;
      if (aData->LineContainer() &&
          aData->LineContainer() != (lc = f->FindLineContainer())) {
        NS_ASSERTION(f != this,
                     "wrong InlineMinISizeData container"
                     " for first continuation");
        aData->mLine = nullptr;
        aData->SetLineContainer(lc);
      }

      // This will process all the text frames that share the same textrun as f.
      f->AddInlineMinISizeForFlow(aInput.mContext, aData, trtype);
      lastTextRun = f->GetTextRun(trtype);
    }
  }
}

// XXX this doesn't handle characters shaped by line endings. We need to
// temporarily override the "current line ending" settings.
void nsTextFrame::AddInlinePrefISizeForFlow(gfxContext* aRenderingContext,
                                            InlinePrefISizeData* aData,
                                            TextRunType aTextRunType) {
  if (IsUnreflowedLetterFrame(GetParent())) {
    MaybeSplitFramesForFirstLetter();
  }

  uint32_t flowEndInTextRun;
  gfxSkipCharsIterator iter =
      EnsureTextRun(aTextRunType, aRenderingContext->GetDrawTarget(),
                    aData->LineContainer(), aData->mLine, &flowEndInTextRun);
  gfxTextRun* textRun = GetTextRun(aTextRunType);
  if (!textRun) {
    return;
  }

  // Pass null for the line container. This will disable tab spacing, but that's
  // OK since we can't really handle tabs for intrinsic sizing anyway.

  const nsStyleText* textStyle = StyleText();
  const nsTextFragment* frag = TextFragment();
  PropertyProvider provider(textRun, textStyle, frag, this, iter, INT32_MAX,
                            nullptr, 0, aTextRunType, aData->mLineIsEmpty);

  // text-combine-upright frame is constantly 1em on inline-axis.
  if (Style()->IsTextCombined()) {
    aData->mCurrentLine += provider.GetFontMetrics()->EmHeight();
    aData->mTrailingWhitespace = 0;
    aData->mLineIsEmpty = false;
    return;
  }

  bool collapseWhitespace = !textStyle->WhiteSpaceIsSignificant();
  bool preformatNewlines = textStyle->NewlineIsSignificant(this);
  bool preformatTabs = textStyle->TabIsSignificant();
  gfxFloat tabWidth = -1;
  uint32_t start = FindStartAfterSkippingWhitespace(&provider, aData, textStyle,
                                                    &iter, flowEndInTextRun);
  if (aData->mLineIsEmpty) {
    provider.SetStartOfLine(iter);
  }

  // XXX Should we consider hyphenation here?
  // If newlines and tabs aren't preformatted, nothing to do inside
  // the loop so make i skip to the end
  uint32_t loopStart =
      (preformatNewlines || preformatTabs) ? start : flowEndInTextRun;
  for (uint32_t i = loopStart, lineStart = start; i <= flowEndInTextRun; ++i) {
    bool preformattedNewline = false;
    bool preformattedTab = false;
    if (i < flowEndInTextRun) {
      // XXXldb Shouldn't we be including the newline as part of the
      // segment that it ends rather than part of the segment that it
      // starts?
      NS_ASSERTION(preformatNewlines || preformatTabs,
                   "We can't be here unless newlines are "
                   "hard breaks or there are tabs");
      preformattedNewline = preformatNewlines && textRun->CharIsNewline(i);
      preformattedTab = preformatTabs && textRun->CharIsTab(i);
      if (!preformattedNewline && !preformattedTab) {
        // we needn't break here (and it's not the end of the flow)
        continue;
      }
    }

    if (i > lineStart) {
      nscoord width = NSToCoordCeilClamped(
          textRun->GetAdvanceWidth(Range(lineStart, i), &provider));
      width = std::max(0, width);
      aData->mCurrentLine = NSCoordSaturatingAdd(aData->mCurrentLine, width);
      aData->mLineIsEmpty = false;

      if (collapseWhitespace) {
        uint32_t trimStart =
            GetEndOfTrimmedText(frag, textStyle, lineStart, i, &iter);
        if (trimStart == start) {
          // This is *all* trimmable whitespace, so whatever trailingWhitespace
          // we saw previously is still trailing...
          aData->mTrailingWhitespace += width;
        } else {
          // Some non-whitespace so the old trailingWhitespace is no longer
          // trailing
          nscoord wsWidth = NSToCoordCeilClamped(
              textRun->GetAdvanceWidth(Range(trimStart, i), &provider));
          aData->mTrailingWhitespace = std::max(0, wsWidth);
        }
      } else {
        aData->mTrailingWhitespace = 0;
      }
    }

    if (preformattedTab) {
      PropertyProvider::Spacing spacing;
      provider.GetSpacing(Range(i, i + 1), &spacing);
      aData->mCurrentLine += nscoord(spacing.mBefore);
      if (tabWidth < 0) {
        tabWidth = ComputeTabWidthAppUnits(this);
      }
      gfxFloat afterTab = AdvanceToNextTab(aData->mCurrentLine, tabWidth,
                                           provider.MinTabAdvance());
      aData->mCurrentLine = nscoord(afterTab + spacing.mAfter);
      aData->mLineIsEmpty = false;
      lineStart = i + 1;
    } else if (preformattedNewline) {
      aData->ForceBreak();
      lineStart = i;
    }
  }

  // Check if we have collapsible whitespace at the end
  if (start < flowEndInTextRun) {
    aData->mSkipWhitespace = IsTrimmableSpace(
        provider.GetFragment(),
        iter.ConvertSkippedToOriginal(flowEndInTextRun - 1), textStyle);
  }
}

// XXX Need to do something here to avoid incremental reflow bugs due to
// first-line and first-letter changing pref-width
/* virtual */
void nsTextFrame::AddInlinePrefISize(const IntrinsicSizeInput& aInput,
                                     InlinePrefISizeData* aData) {
  float inflation = nsLayoutUtils::FontSizeInflationFor(this);
  TextRunType trtype = (inflation == 1.0f) ? eNotInflated : eInflated;

  if (trtype == eInflated && !IsCurrentFontInflation(inflation)) {
    // FIXME: Ideally, if we already have a text run, we'd move it to be
    // the uninflated text run.
    ClearTextRun(nullptr, nsTextFrame::eInflated);
    mFontMetrics = nullptr;
  }

  nsTextFrame* f;
  const gfxTextRun* lastTextRun = nullptr;
  // nsContinuingTextFrame does nothing for AddInlineMinISize; all text frames
  // in the flow are handled right here.
  for (f = this; f; f = f->GetNextContinuation()) {
    // f->GetTextRun(nsTextFrame::eNotInflated) could be null if we
    // haven't set up textruns yet for f.  Except in OOM situations,
    // lastTextRun will only be null for the first text frame.
    if (f == this || f->GetTextRun(trtype) != lastTextRun) {
      nsIFrame* lc;
      if (aData->LineContainer() &&
          aData->LineContainer() != (lc = f->FindLineContainer())) {
        NS_ASSERTION(f != this,
                     "wrong InlinePrefISizeData container"
                     " for first continuation");
        aData->mLine = nullptr;
        aData->SetLineContainer(lc);
      }

      // This will process all the text frames that share the same textrun as f.
      f->AddInlinePrefISizeForFlow(aInput.mContext, aData, trtype);
      lastTextRun = f->GetTextRun(trtype);
    }
  }
}

/* virtual */
nsIFrame::SizeComputationResult nsTextFrame::ComputeSize(
    gfxContext* aRenderingContext, WritingMode aWM, const LogicalSize& aCBSize,
    nscoord aAvailableISize, const LogicalSize& aMargin,
    const LogicalSize& aBorderPadding, const StyleSizeOverrides& aSizeOverrides,
    ComputeSizeFlags aFlags) {
  // Inlines and text don't compute size before reflow.
  return {LogicalSize(aWM, NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE),
          AspectRatioUsage::None};
}

static nsRect RoundOut(const gfxRect& aRect) {
  nsRect r;
  r.x = NSToCoordFloor(aRect.X());
  r.y = NSToCoordFloor(aRect.Y());
  r.width = NSToCoordCeil(aRect.XMost()) - r.x;
  r.height = NSToCoordCeil(aRect.YMost()) - r.y;
  return r;
}

nsRect nsTextFrame::ComputeTightBounds(DrawTarget* aDrawTarget) const {
  if (Style()->HasTextDecorationLines() || HasAnyStateBits(TEXT_HYPHEN_BREAK)) {
    // This is conservative, but OK.
    return InkOverflowRect();
  }

  gfxSkipCharsIterator iter =
      const_cast<nsTextFrame*>(this)->EnsureTextRun(nsTextFrame::eInflated);
  if (!mTextRun) {
    return nsRect();
  }

  PropertyProvider provider(const_cast<nsTextFrame*>(this), iter,
                            nsTextFrame::eInflated, mFontMetrics);
  // Trim trailing whitespace
  provider.InitializeForDisplay(true);

  gfxTextRun::Metrics metrics = mTextRun->MeasureText(
      ComputeTransformedRange(provider), gfxFont::TIGHT_HINTED_OUTLINE_EXTENTS,
      aDrawTarget, &provider);
  if (GetWritingMode().IsLineInverted()) {
    metrics.mBoundingBox.y = -metrics.mBoundingBox.YMost();
  }
  // mAscent should be the same as metrics.mAscent, but it's what we use to
  // paint so that's the one we'll use.
  nsRect boundingBox = RoundOut(metrics.mBoundingBox);
  boundingBox += nsPoint(0, mAscent);
  if (mTextRun->IsVertical()) {
    // Swap line-relative textMetrics dimensions to physical coordinates.
    std::swap(boundingBox.x, boundingBox.y);
    std::swap(boundingBox.width, boundingBox.height);
  }
  return boundingBox;
}

/* virtual */
nsresult nsTextFrame::GetPrefWidthTightBounds(gfxContext* aContext, nscoord* aX,
                                              nscoord* aXMost) {
  gfxSkipCharsIterator iter = EnsureTextRun(nsTextFrame::eInflated);
  if (!mTextRun) {
    return NS_ERROR_FAILURE;
  }

  PropertyProvider provider(this, iter, nsTextFrame::eInflated, mFontMetrics);
  provider.InitializeForMeasure();

  gfxTextRun::Metrics metrics = mTextRun->MeasureText(
      ComputeTransformedRange(provider), gfxFont::TIGHT_HINTED_OUTLINE_EXTENTS,
      aContext->GetDrawTarget(), &provider);
  // Round it like nsTextFrame::ComputeTightBounds() to ensure consistency.
  *aX = NSToCoordFloor(metrics.mBoundingBox.x);
  *aXMost = NSToCoordCeil(metrics.mBoundingBox.XMost());

  return NS_OK;
}

static bool HasSoftHyphenBefore(const nsTextFragment* aFrag,
                                const gfxTextRun* aTextRun,
                                int32_t aStartOffset,
                                const gfxSkipCharsIterator& aIter) {
  if (aIter.GetSkippedOffset() < aTextRun->GetLength() &&
      aTextRun->CanHyphenateBefore(aIter.GetSkippedOffset())) {
    return true;
  }
  if (!(aTextRun->GetFlags2() & nsTextFrameUtils::Flags::HasShy)) {
    return false;
  }
  gfxSkipCharsIterator iter = aIter;
  while (iter.GetOriginalOffset() > aStartOffset) {
    iter.AdvanceOriginal(-1);
    if (!iter.IsOriginalCharSkipped()) {
      break;
    }
    if (aFrag->CharAt(AssertedCast<uint32_t>(iter.GetOriginalOffset())) ==
        CH_SHY) {
      return true;
    }
  }
  return false;
}

/**
 * Removes all frames from aFrame up to (but not including) aFirstToNotRemove,
 * because their text has all been taken and reflowed by earlier frames.
 */
static void RemoveEmptyInFlows(nsTextFrame* aFrame,
                               nsTextFrame* aFirstToNotRemove) {
  MOZ_ASSERT(aFrame != aFirstToNotRemove, "This will go very badly");
  // We have to be careful here, because some RemoveFrame implementations
  // remove and destroy not only the passed-in frame but also all its following
  // in-flows (and sometimes all its following continuations in general).  So
  // we remove |f| and everything up to but not including firstToNotRemove from
  // the flow first, to make sure that only the things we want destroyed are
  // destroyed.

  // This sadly duplicates some of the logic from
  // nsSplittableFrame::RemoveFromFlow.  We can get away with not duplicating
  // all of it, because we know that the prev-continuation links of
  // firstToNotRemove and f are fluid, and non-null.
  NS_ASSERTION(aFirstToNotRemove->GetPrevContinuation() ==
                       aFirstToNotRemove->GetPrevInFlow() &&
                   aFirstToNotRemove->GetPrevInFlow() != nullptr,
               "aFirstToNotRemove should have a fluid prev continuation");
  NS_ASSERTION(aFrame->GetPrevContinuation() == aFrame->GetPrevInFlow() &&
                   aFrame->GetPrevInFlow() != nullptr,
               "aFrame should have a fluid prev continuation");

  nsTextFrame* prevContinuation = aFrame->GetPrevContinuation();
  nsTextFrame* lastRemoved = aFirstToNotRemove->GetPrevContinuation();

  for (nsTextFrame* f = aFrame; f != aFirstToNotRemove;
       f = f->GetNextContinuation()) {
    // f is going to be destroyed soon, after it is unlinked from the
    // continuation chain. If its textrun is going to be destroyed we need to
    // do it now, before we unlink the frames to remove from the flow,
    // because Destroy calls ClearTextRuns() and that will start at the
    // first frame with the text run and walk the continuations.
    if (f->IsInTextRunUserData()) {
      f->ClearTextRuns();
    } else {
      f->DisconnectTextRuns();
    }
  }

  prevContinuation->SetNextInFlow(aFirstToNotRemove);
  aFirstToNotRemove->SetPrevInFlow(prevContinuation);

  // **Note: it is important here that we clear the Next link from lastRemoved
  // BEFORE clearing the Prev link from aFrame, because SetPrevInFlow() will
  // follow the Next pointers, wiping out the cached mFirstContinuation field
  // from each following frame in the list. We need this to stop when it
  // reaches lastRemoved!
  lastRemoved->SetNextInFlow(nullptr);
  aFrame->SetPrevInFlow(nullptr);

  nsContainerFrame* parent = aFrame->GetParent();
  nsIFrame::DestroyContext context(aFrame->PresShell());
  nsBlockFrame* parentBlock = do_QueryFrame(parent);
  if (parentBlock) {
    // Manually call DoRemoveFrame so we can tell it that we're
    // removing empty frames; this will keep it from blowing away
    // text runs.
    parentBlock->DoRemoveFrame(context, aFrame, nsBlockFrame::FRAMES_ARE_EMPTY);
  } else {
    // Just remove it normally; use FrameChildListID::NoReflowPrincipal to avoid
    // posting new reflows.
    parent->RemoveFrame(context, FrameChildListID::NoReflowPrincipal, aFrame);
  }
}

void nsTextFrame::SetLength(int32_t aLength, nsLineLayout* aLineLayout,
                            uint32_t aSetLengthFlags) {
  mContentLengthHint = aLength;
  int32_t end = GetContentOffset() + aLength;
  nsTextFrame* f = GetNextInFlow();
  if (!f) {
    return;
  }

  // If our end offset is moving, then even if frames are not being pushed or
  // pulled, content is moving to or from the next line and the next line
  // must be reflowed.
  // If the next-continuation is dirty, then we should dirty the next line now
  // because we may have skipped doing it if we dirtied it in
  // CharacterDataChanged. This is ugly but teaching FrameNeedsReflow
  // and ChildIsDirty to handle a range of frames would be worse.
  if (aLineLayout &&
      (end != f->mContentOffset || f->HasAnyStateBits(NS_FRAME_IS_DIRTY))) {
    aLineLayout->SetDirtyNextLine();
  }

  if (end < f->mContentOffset) {
    // Our frame is shrinking. Give the text to our next in flow.
    if (aLineLayout && HasSignificantTerminalNewline() &&
        !GetParent()->IsLetterFrame() &&
        (aSetLengthFlags & ALLOW_FRAME_CREATION_AND_DESTRUCTION)) {
      // Whatever text we hand to our next-in-flow will end up in a frame all of
      // its own, since it ends in a forced linebreak.  Might as well just put
      // it in a separate frame now.  This is important to prevent text run
      // churn; if we did not do that, then we'd likely end up rebuilding
      // textruns for all our following continuations.
      // We skip this optimization when the parent is a first-letter frame
      // because it doesn't deal well with more than one child frame.
      // We also skip this optimization if we were called during bidi
      // resolution, so as not to create a new frame which doesn't appear in
      // the bidi resolver's list of frames
      nsIFrame* newFrame =
          PresShell()->FrameConstructor()->CreateContinuingFrame(this,
                                                                 GetParent());
      nsTextFrame* next = static_cast<nsTextFrame*>(newFrame);
      GetParent()->InsertFrames(FrameChildListID::NoReflowPrincipal, this,
                                aLineLayout->GetLine(),
                                nsFrameList(next, next));
      f = next;
    }

    f->mContentOffset = end;
    if (f->GetTextRun(nsTextFrame::eInflated) != mTextRun) {
      ClearTextRuns();
      f->ClearTextRuns();
    }
    return;
  }
  // Our frame is growing. Take text from our in-flow(s).
  // We can take text from frames in lines beyond just the next line.
  // We don't dirty those lines. That's OK, because when we reflow
  // our empty next-in-flow, it will take text from its next-in-flow and
  // dirty that line.

  // Note that in the process we may end up removing some frames from
  // the flow if they end up empty.
  nsTextFrame* framesToRemove = nullptr;
  while (f && f->mContentOffset < end) {
    f->mContentOffset = end;
    if (f->GetTextRun(nsTextFrame::eInflated) != mTextRun) {
      ClearTextRuns();
      f->ClearTextRuns();
    }
    nsTextFrame* next = f->GetNextInFlow();
    // Note: the "f->GetNextSibling() == next" check below is to restrict
    // this optimization to the case where they are on the same child list.
    // Otherwise we might remove the only child of a nsFirstLetterFrame
    // for example and it can't handle that.  See bug 597627 for details.
    if (next && next->mContentOffset <= end && f->GetNextSibling() == next &&
        (aSetLengthFlags & ALLOW_FRAME_CREATION_AND_DESTRUCTION)) {
      // |f| is now empty.  We may as well remove it, instead of copying all
      // the text from |next| into it instead; the latter leads to use
      // rebuilding textruns for all following continuations.
      // We skip this optimization if we were called during bidi resolution,
      // since the bidi resolver may try to handle the destroyed frame later
      // and crash
      if (!framesToRemove) {
        // Remember that we have to remove this frame.
        framesToRemove = f;
      }
    } else if (framesToRemove) {
      RemoveEmptyInFlows(framesToRemove, f);
      framesToRemove = nullptr;
    }
    f = next;
  }

  MOZ_ASSERT(!framesToRemove || (f && f->mContentOffset == end),
             "How did we exit the loop if we null out framesToRemove if "
             "!next || next->mContentOffset > end ?");

  if (framesToRemove) {
    // We are guaranteed that we exited the loop with f not null, per the
    // postcondition above
    RemoveEmptyInFlows(framesToRemove, f);
  }

#ifdef DEBUG
  f = this;
  int32_t iterations = 0;
  while (f && iterations < 10) {
    f->GetContentLength();  // Assert if negative length
    f = f->GetNextContinuation();
    ++iterations;
  }
  f = this;
  iterations = 0;
  while (f && iterations < 10) {
    f->GetContentLength();  // Assert if negative length
    f = f->GetPrevContinuation();
    ++iterations;
  }
#endif
}

void nsTextFrame::SetFirstLetterLength(int32_t aLength) {
  if (aLength == GetContentLength()) {
    return;
  }

  mContentLengthHint = aLength;
  nsTextFrame* next = static_cast<nsTextFrame*>(GetNextInFlow());
  if (!aLength && !next) {
    return;
  }

  if (aLength > GetContentLength()) {
    // Stealing some text from our next-in-flow; this happens with floating
    // first-letter, which is initially given a zero-length range, with all
    // the text being in its continuation.
    if (!next) {
      MOZ_ASSERT_UNREACHABLE("Expected a next-in-flow; first-letter broken?");
      return;
    }
  } else if (!next) {
    // We need to create a continuation for the parent first-letter frame,
    // and move any kids after this frame to the new one; if there are none,
    // a new continuing text frame will be created there.
    MOZ_ASSERT(GetParent()->IsLetterFrame());
    auto* letterFrame = static_cast<nsFirstLetterFrame*>(GetParent());
    next = letterFrame->CreateContinuationForFramesAfter(this);
  }

  next->mContentOffset = GetContentOffset() + aLength;

  ClearTextRuns();
}

bool nsTextFrame::IsFloatingFirstLetterChild() const {
  nsIFrame* frame = GetParent();
  return frame && frame->IsFloating() && frame->IsLetterFrame();
}

bool nsTextFrame::IsInitialLetterChild() const {
  nsIFrame* frame = GetParent();
  return frame && frame->StyleTextReset()->mInitialLetter.size != 0.0f &&
         frame->IsLetterFrame();
}

struct nsTextFrame::NewlineProperty {
  int32_t mStartOffset;
  // The offset of the first \n after mStartOffset, or -1 if there is none
  int32_t mNewlineOffset;
};

int32_t nsTextFrame::GetContentNewLineOffset(
    int32_t aOffset, NewlineProperty*& aCachedNewlineOffset) {
  int32_t contentNewLineOffset = -1;  // this will be -1 or a content offset
  if (StyleText()->NewlineIsSignificant(this)) {
    // Pointer to the nsGkAtoms::newline set on this frame's element
    aCachedNewlineOffset = mContent->HasFlag(NS_HAS_NEWLINE_PROPERTY)
                               ? static_cast<NewlineProperty*>(
                                     mContent->GetProperty(nsGkAtoms::newline))
                               : nullptr;
    if (aCachedNewlineOffset && aCachedNewlineOffset->mStartOffset <= aOffset &&
        (aCachedNewlineOffset->mNewlineOffset == -1 ||
         aCachedNewlineOffset->mNewlineOffset >= aOffset)) {
      contentNewLineOffset = aCachedNewlineOffset->mNewlineOffset;
    } else {
      contentNewLineOffset = FindChar(
          TextFragment(), aOffset, GetContent()->TextLength() - aOffset, '\n');
    }
  }

  return contentNewLineOffset;
}

void nsTextFrame::Reflow(nsPresContext* aPresContext, ReflowOutput& aMetrics,
                         const ReflowInput& aReflowInput,
                         nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsTextFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  InvalidateSelectionState();

  // XXX If there's no line layout, we shouldn't even have created this
  // frame. This may happen if, for example, this is text inside a table
  // but not inside a cell. For now, just don't reflow.
  if (!aReflowInput.mLineLayout) {
    ClearMetrics(aMetrics);
    return;
  }

  ReflowText(*aReflowInput.mLineLayout, aReflowInput.AvailableWidth(),
             aReflowInput.mRenderingContext->GetDrawTarget(), aMetrics,
             aStatus);
}

#ifdef ACCESSIBILITY
/**
 * Notifies accessibility about text reflow. Used by nsTextFrame::ReflowText.
 */
class MOZ_STACK_CLASS ReflowTextA11yNotifier {
 public:
  ReflowTextA11yNotifier(nsPresContext* aPresContext, nsIContent* aContent)
      : mContent(aContent), mPresContext(aPresContext) {}
  ~ReflowTextA11yNotifier() {
    if (nsAccessibilityService* accService = GetAccService()) {
      accService->UpdateText(mPresContext->PresShell(), mContent);
    }
  }

 private:
  ReflowTextA11yNotifier();
  ReflowTextA11yNotifier(const ReflowTextA11yNotifier&);
  ReflowTextA11yNotifier& operator=(const ReflowTextA11yNotifier&);

  nsIContent* mContent;
  nsPresContext* mPresContext;
};
#endif

void nsTextFrame::ReflowText(nsLineLayout& aLineLayout, nscoord aAvailableWidth,
                             DrawTarget* aDrawTarget, ReflowOutput& aMetrics,
                             nsReflowStatus& aStatus) {
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

#ifdef NOISY_REFLOW
  ListTag(stdout);
  printf(": BeginReflow: availableWidth=%d\n", aAvailableWidth);
#endif

  nsPresContext* presContext = PresContext();

#ifdef ACCESSIBILITY
  // Schedule the update of accessible tree since rendered text might be
  // changed.
  if (StyleVisibility()->IsVisible()) {
    ReflowTextA11yNotifier(presContext, mContent);
  }
#endif

  /////////////////////////////////////////////////////////////////////
  // Set up flags and clear out state
  /////////////////////////////////////////////////////////////////////

  // Clear out the reflow input flags in mState. We also clear the whitespace
  // flags because this can change whether the frame maps whitespace-only text
  // or not. We also clear the flag that tracks whether we had a pending
  // reflow request from CharacterDataChanged (since we're reflowing now).
  RemoveStateBits(TEXT_REFLOW_FLAGS | TEXT_WHITESPACE_FLAGS);
  mReflowRequestedForCharDataChange = false;
  RemoveProperty(WebRenderTextBounds());

  // Discard cached continuations array that will be invalidated by the reflow.
  if (nsTextFrame* first = FirstContinuation()) {
    first->ClearCachedContinuations();
  }

  // Temporarily map all possible content while we construct our new textrun.
  // so that when doing reflow our styles prevail over any part of the
  // textrun we look at. Note that next-in-flows may be mapping the same
  // content; gfxTextRun construction logic will ensure that we take priority.
  int32_t maxContentLength = GetInFlowContentLength();

  InvalidateSelectionState();

  // We don't need to reflow if there is no content.
  if (!maxContentLength) {
    ClearMetrics(aMetrics);
    return;
  }

#ifdef NOISY_BIDI
  printf("Reflowed textframe\n");
#endif

  const nsStyleText* textStyle = StyleText();

  bool atStartOfLine = aLineLayout.LineAtStart();
  if (atStartOfLine) {
    AddStateBits(TEXT_START_OF_LINE);
  }

  uint32_t flowEndInTextRun;
  nsIFrame* lineContainer = aLineLayout.LineContainerFrame();
  const nsTextFragment* frag = TextFragment();

  // DOM offsets of the text range we need to measure, after trimming
  // whitespace, restricting to first-letter, and restricting preformatted text
  // to nearest newline
  int32_t length = maxContentLength;
  int32_t offset = GetContentOffset();

  // Restrict preformatted text to the nearest newline
  NewlineProperty* cachedNewlineOffset = nullptr;
  int32_t newLineOffset = -1;  // this will be -1 or a content offset
  // This will just return -1 if newlines are not significant.
  int32_t contentNewLineOffset =
      GetContentNewLineOffset(offset, cachedNewlineOffset);
  if (contentNewLineOffset < offset + length) {
    // The new line offset could be outside this frame if the frame has been
    // split by bidi resolution. In that case we won't use it in this reflow
    // (newLineOffset will remain -1), but we will still cache it in mContent
    newLineOffset = contentNewLineOffset;
  }
  if (newLineOffset >= 0) {
    length = newLineOffset + 1 - offset;
  }

  if ((atStartOfLine && !textStyle->WhiteSpaceIsSignificant()) ||
      HasAnyStateBits(TEXT_IS_IN_TOKEN_MATHML)) {
    // Skip leading whitespace. Make sure we don't skip a 'pre-line'
    // newline if there is one.
    int32_t skipLength = newLineOffset >= 0 ? length - 1 : length;
    int32_t whitespaceCount =
        GetTrimmableWhitespaceCount(frag, offset, skipLength, 1);
    if (whitespaceCount) {
      offset += whitespaceCount;
      length -= whitespaceCount;
      // Make sure this frame maps the trimmable whitespace.
      if (MOZ_UNLIKELY(offset > GetContentEnd())) {
        SetLength(offset - GetContentOffset(), &aLineLayout,
                  ALLOW_FRAME_CREATION_AND_DESTRUCTION);
      }
    }
  }

  // If trimming whitespace left us with nothing to do, return early.
  if (length == 0) {
    ClearMetrics(aMetrics);
    return;
  }

  bool completedFirstLetter = false;
  // Layout dependent styles are a problem because we need to reconstruct
  // the gfxTextRun based on our layout.
  if (aLineLayout.GetInFirstLetter() || aLineLayout.GetInFirstLine()) {
    SetLength(maxContentLength, &aLineLayout,
              ALLOW_FRAME_CREATION_AND_DESTRUCTION);

    if (aLineLayout.GetInFirstLetter()) {
      // floating first-letter boundaries are significant in textrun
      // construction, so clear the textrun out every time we hit a first-letter
      // and have changed our length (which controls the first-letter boundary)
      ClearTextRuns();
      // Find the length of the first-letter. We need a textrun for this.
      // REVIEW: maybe-bogus inflation should be ok (fixed below)
      gfxSkipCharsIterator iter =
          EnsureTextRun(nsTextFrame::eInflated, aDrawTarget, lineContainer,
                        aLineLayout.GetLine(), &flowEndInTextRun);

      if (mTextRun) {
        int32_t firstLetterLength = length;
        if (aLineLayout.GetFirstLetterStyleOK()) {
          // We only pass a language code to FindFirstLetterRange if it was
          // explicit in the content.
          const nsStyleFont* styleFont = StyleFont();
          const nsAtom* lang = styleFont->mExplicitLanguage
                                   ? styleFont->mLanguage.get()
                                   : nullptr;
          completedFirstLetter = FindFirstLetterRange(
              frag, lang, mTextRun, offset, iter, &firstLetterLength);
          if (newLineOffset >= 0) {
            // Don't allow a preformatted newline to be part of a first-letter.
            firstLetterLength = std::min(firstLetterLength, length - 1);
            if (length == 1) {
              // There is no text to be consumed by the first-letter before the
              // preformatted newline. Note that the first letter is therefore
              // complete (FindFirstLetterRange will have returned false).
              completedFirstLetter = true;
            }
          }
        } else {
          // We're in a first-letter frame's first in flow, so if there
          // was a first-letter, we'd be it. However, for one reason
          // or another (e.g., preformatted line break before this text),
          // we're not actually supposed to have first-letter style. So
          // just make a zero-length first-letter.
          firstLetterLength = 0;
          completedFirstLetter = true;
        }
        length = firstLetterLength;
        if (length) {
          AddStateBits(TEXT_FIRST_LETTER);
        }
        // Change this frame's length to the first-letter length right now
        // so that when we rebuild the textrun it will be built with the
        // right first-letter boundary
        SetLength(offset + length - GetContentOffset(), &aLineLayout,
                  ALLOW_FRAME_CREATION_AND_DESTRUCTION);
        // Ensure that the textrun will be rebuilt
        ClearTextRuns();
      }
    }
  }

  float fontSizeInflation = nsLayoutUtils::FontSizeInflationFor(this);

  if (!IsCurrentFontInflation(fontSizeInflation)) {
    // FIXME: Ideally, if we already have a text run, we'd move it to be
    // the uninflated text run.
    ClearTextRun(nullptr, nsTextFrame::eInflated);
    mFontMetrics = nullptr;
  }

  gfxSkipCharsIterator iter =
      EnsureTextRun(nsTextFrame::eInflated, aDrawTarget, lineContainer,
                    aLineLayout.GetLine(), &flowEndInTextRun);

  NS_ASSERTION(IsCurrentFontInflation(fontSizeInflation),
               "EnsureTextRun should have set font size inflation");

  if (mTextRun && iter.GetOriginalEnd() < offset + length) {
    // The textrun does not map enough text for this frame. This can happen
    // when the textrun was ended in the middle of a text node because a
    // preformatted newline was encountered, and prev-in-flow frames have
    // consumed all the text of the textrun. We need a new textrun.
    ClearTextRuns();
    iter = EnsureTextRun(nsTextFrame::eInflated, aDrawTarget, lineContainer,
                         aLineLayout.GetLine(), &flowEndInTextRun);
  }

  if (!mTextRun) {
    ClearMetrics(aMetrics);
    return;
  }

  NS_ASSERTION(gfxSkipCharsIterator(iter).ConvertOriginalToSkipped(
                   offset + length) <= mTextRun->GetLength(),
               "Text run does not map enough text for our reflow");

  /////////////////////////////////////////////////////////////////////
  // See how much text should belong to this text frame, and measure it
  /////////////////////////////////////////////////////////////////////

  iter.SetOriginalOffset(offset);
  nscoord xOffsetForTabs =
      (mTextRun->GetFlags2() & nsTextFrameUtils::Flags::HasTab)
          ? (aLineLayout.GetCurrentFrameInlineDistanceFromBlock() -
             lineContainer->GetUsedBorderAndPadding().left)
          : -1;
  PropertyProvider provider(mTextRun, textStyle, frag, this, iter, length,
                            lineContainer, xOffsetForTabs,
                            nsTextFrame::eInflated,
                            HasAnyStateBits(TEXT_START_OF_LINE));

  uint32_t transformedOffset = provider.GetStart().GetSkippedOffset();

  gfxFont::BoundingBoxType boundingBoxType = gfxFont::LOOSE_INK_EXTENTS;
  if (IsFloatingFirstLetterChild() || IsInitialLetterChild()) {
    if (nsFirstLetterFrame* firstLetter = do_QueryFrame(GetParent())) {
      if (firstLetter->UseTightBounds()) {
        boundingBoxType = gfxFont::TIGHT_HINTED_OUTLINE_EXTENTS;
      }
    }
  }

  int32_t limitLength = length;
  int32_t forceBreak = aLineLayout.GetForcedBreakPosition(this);
  bool forceBreakAfter = false;
  if (forceBreak >= length) {
    forceBreakAfter = forceBreak == length;
    // The break is not within the text considered for this textframe.
    forceBreak = -1;
  }
  if (forceBreak >= 0) {
    limitLength = forceBreak;
  }
  // This is the heart of text reflow right here! We don't know where
  // to break, so we need to see how much text fits in the available width.
  uint32_t transformedLength;
  if (offset + limitLength >= int32_t(frag->GetLength())) {
    NS_ASSERTION(offset + limitLength == int32_t(frag->GetLength()),
                 "Content offset/length out of bounds");
    NS_ASSERTION(flowEndInTextRun >= transformedOffset,
                 "Negative flow length?");
    transformedLength = flowEndInTextRun - transformedOffset;
  } else {
    // we're not looking at all the content, so we need to compute the
    // length of the transformed substring we're looking at
    gfxSkipCharsIterator iter(provider.GetStart());
    iter.SetOriginalOffset(offset + limitLength);
    transformedLength = iter.GetSkippedOffset() - transformedOffset;
  }
  gfxTextRun::Metrics textMetrics;
  uint32_t transformedLastBreak = 0;
  bool usedHyphenation = false;
  gfxTextRun::TrimmableWS trimmableWS;
  gfxFloat availWidth = aAvailableWidth;
  if (Style()->IsTextCombined()) {
    // If text-combine-upright is 'all', we would compress whatever long
    // text into ~1em width, so there is no limited on the avail width.
    availWidth = std::numeric_limits<gfxFloat>::infinity();
  }
  bool canTrimTrailingWhitespace = !textStyle->WhiteSpaceIsSignificant() ||
                                   HasAnyStateBits(TEXT_IS_IN_TOKEN_MATHML);
  bool isBreakSpaces =
      textStyle->mWhiteSpaceCollapse == StyleWhiteSpaceCollapse::BreakSpaces;
  // allow whitespace to overflow the container
  bool whitespaceCanHang = textStyle->WhiteSpaceCanHangOrVisuallyCollapse();
  gfxBreakPriority breakPriority = aLineLayout.LastOptionalBreakPriority();
  gfxTextRun::SuppressBreak suppressBreak = gfxTextRun::eNoSuppressBreak;
  bool shouldSuppressLineBreak = ShouldSuppressLineBreak();
  if (shouldSuppressLineBreak) {
    suppressBreak = gfxTextRun::eSuppressAllBreaks;
  } else if (!aLineLayout.LineIsBreakable()) {
    suppressBreak = gfxTextRun::eSuppressInitialBreak;
  }
  uint32_t transformedCharsFit = mTextRun->BreakAndMeasureText(
      transformedOffset, transformedLength, HasAnyStateBits(TEXT_START_OF_LINE),
      availWidth, provider, suppressBreak, boundingBoxType, aDrawTarget,
      textStyle->WordCanWrap(this), textStyle->WhiteSpaceCanWrap(this),
      isBreakSpaces,
      // The following are output parameters:
      canTrimTrailingWhitespace || whitespaceCanHang ? &trimmableWS : nullptr,
      textMetrics, usedHyphenation, transformedLastBreak,
      // In/out
      breakPriority);
  if (!length && !textMetrics.mAscent && !textMetrics.mDescent) {
    // If we're measuring a zero-length piece of text, update
    // the height manually.
    nsFontMetrics* fm = provider.GetFontMetrics();
    if (fm) {
      textMetrics.mAscent = gfxFloat(fm->MaxAscent());
      textMetrics.mDescent = gfxFloat(fm->MaxDescent());
    }
  }
  if (GetWritingMode().IsLineInverted()) {
    std::swap(textMetrics.mAscent, textMetrics.mDescent);
    textMetrics.mBoundingBox.y = -textMetrics.mBoundingBox.YMost();
  }
  // The "end" iterator points to the first character after the string mapped
  // by this frame. Basically, its original-string offset is offset+charsFit
  // after we've computed charsFit.
  gfxSkipCharsIterator end(provider.GetEndHint());
  end.SetSkippedOffset(transformedOffset + transformedCharsFit);
  int32_t charsFit = end.GetOriginalOffset() - offset;
  if (offset + charsFit == newLineOffset) {
    // We broke before a trailing preformatted '\n'. The newline should
    // be assigned to this frame. Note that newLineOffset will be -1 if
    // there was no preformatted newline, so we wouldn't get here in that
    // case.
    ++charsFit;
  }
  // That might have taken us beyond our assigned content range (because
  // we might have advanced over some skipped chars that extend outside
  // this frame), so get back in.
  int32_t lastBreak = -1;
  if (charsFit >= limitLength) {
    charsFit = limitLength;
    if (transformedLastBreak != UINT32_MAX) {
      // lastBreak is needed.
      // This may set lastBreak greater than 'length', but that's OK
      lastBreak = end.ConvertSkippedToOriginal(transformedOffset +
                                               transformedLastBreak);
    }
    end.SetOriginalOffset(offset + charsFit);
    // If we were forced to fit, and the break position is after a soft hyphen,
    // note that this is a hyphenation break.
    if ((forceBreak >= 0 || forceBreakAfter) &&
        HasSoftHyphenBefore(frag, mTextRun, offset, end)) {
      usedHyphenation = true;
    }
  }
  if (usedHyphenation) {
    // Fix up metrics to include hyphen
    AddHyphenToMetrics(this, mTextRun->IsRightToLeft(), &textMetrics,
                       boundingBoxType, aDrawTarget);
    AddStateBits(TEXT_HYPHEN_BREAK | TEXT_HAS_NONCOLLAPSED_CHARACTERS);
  }
  if (textMetrics.mBoundingBox.IsEmpty()) {
    AddStateBits(TEXT_NO_RENDERED_GLYPHS);
  }

  bool brokeText = forceBreak >= 0 || transformedCharsFit < transformedLength;
  if (trimmableWS.mAdvance > 0.0) {
    if (canTrimTrailingWhitespace) {
      // Optimization: if we we can be sure this frame will be at end of line,
      // then trim the whitespace now.
      if (brokeText || HasAnyStateBits(TEXT_IS_IN_TOKEN_MATHML)) {
        // We're definitely going to break so our trailing whitespace should
        // definitely be trimmed. Record that we've already done it.
        AddStateBits(TEXT_TRIMMED_TRAILING_WHITESPACE);
        textMetrics.mAdvanceWidth -= trimmableWS.mAdvance;
        trimmableWS.mAdvance = 0.0;
      }
      ClearHangableISize();
      ClearTrimmableWS();
    } else if (whitespaceCanHang) {
      // Figure out how much whitespace will hang if at end-of-line.
      gfxFloat hang =
          std::min(std::max(0.0, textMetrics.mAdvanceWidth - availWidth),
                   gfxFloat(trimmableWS.mAdvance));
      SetHangableISize(NSToCoordRound(trimmableWS.mAdvance - hang));
      // nsLineLayout only needs the TrimmableWS property if justifying, so
      // check whether this is relevant.
      if (textStyle->mTextAlign == StyleTextAlign::Justify ||
          textStyle->mTextAlignLast == StyleTextAlignLast::Justify) {
        SetTrimmableWS(trimmableWS);
      }
      textMetrics.mAdvanceWidth -= hang;
      trimmableWS.mAdvance = 0.0;
    } else {
      MOZ_ASSERT_UNREACHABLE("How did trimmableWS get set?!");
      ClearHangableISize();
      ClearTrimmableWS();
      trimmableWS.mAdvance = 0.0;
    }
  } else {
    // Remove any stale frame properties.
    ClearHangableISize();
    ClearTrimmableWS();
  }

  if (!brokeText && lastBreak >= 0) {
    // Since everything fit and no break was forced,
    // record the last break opportunity
    NS_ASSERTION(textMetrics.mAdvanceWidth - trimmableWS.mAdvance <= availWidth,
                 "If the text doesn't fit, and we have a break opportunity, "
                 "why didn't MeasureText use it?");
    MOZ_ASSERT(lastBreak >= offset, "Strange break position");
    aLineLayout.NotifyOptionalBreakPosition(this, lastBreak - offset, true,
                                            breakPriority);
  }

  int32_t contentLength = offset + charsFit - GetContentOffset();

  /////////////////////////////////////////////////////////////////////
  // Compute output metrics
  /////////////////////////////////////////////////////////////////////

  // first-letter frames should use the tight bounding box metrics for
  // ascent/descent for good drop-cap effects
  if (HasAnyStateBits(TEXT_FIRST_LETTER)) {
    textMetrics.mAscent =
        std::max(gfxFloat(0.0), -textMetrics.mBoundingBox.Y());
    textMetrics.mDescent =
        std::max(gfxFloat(0.0), textMetrics.mBoundingBox.YMost());
  }

  // Setup metrics for caller
  // Disallow negative widths
  WritingMode wm = GetWritingMode();
  LogicalSize finalSize(wm);
  finalSize.ISize(wm) =
      NSToCoordCeilClamped(std::max(gfxFloat(0.0), textMetrics.mAdvanceWidth));

  nscoord fontBaseline;
  // Note(dshin): Baseline should tecnhically be halfway through the em box for
  // a central baseline. It is simply half of the text run block size so that it
  // can be easily calculated in `GetNaturalBaselineBOffset`.
  if (transformedCharsFit == 0 && !usedHyphenation) {
    aMetrics.SetBlockStartAscent(0);
    finalSize.BSize(wm) = 0;
    fontBaseline = 0;
  } else if (boundingBoxType != gfxFont::LOOSE_INK_EXTENTS) {
    fontBaseline = NSToCoordCeil(textMetrics.mAscent);
    const auto size = fontBaseline + NSToCoordCeil(textMetrics.mDescent);
    // Use actual text metrics for floating first letter frame.
    aMetrics.SetBlockStartAscent(wm.IsAlphabeticalBaseline() ? fontBaseline
                                                             : size / 2);
    finalSize.BSize(wm) = size;
  } else {
    // Otherwise, ascent should contain the overline drawable area.
    // And also descent should contain the underline drawable area.
    // nsFontMetrics::GetMaxAscent/GetMaxDescent contains them.
    nsFontMetrics* fm = provider.GetFontMetrics();
    nscoord fontAscent =
        wm.IsLineInverted() ? fm->MaxDescent() : fm->MaxAscent();
    nscoord fontDescent =
        wm.IsLineInverted() ? fm->MaxAscent() : fm->MaxDescent();
    fontBaseline = std::max(NSToCoordCeil(textMetrics.mAscent), fontAscent);
    const auto size =
        fontBaseline +
        std::max(NSToCoordCeil(textMetrics.mDescent), fontDescent);
    aMetrics.SetBlockStartAscent(wm.IsAlphabeticalBaseline() ? fontBaseline
                                                             : size / 2);
    finalSize.BSize(wm) = size;
  }
  if (Style()->IsTextCombined()) {
    nsFontMetrics* fm = provider.GetFontMetrics();
    nscoord width = finalSize.ISize(wm);
    nscoord em = fm->EmHeight();
    // Compress the characters in horizontal axis if necessary.
    if (width <= em) {
      RemoveProperty(TextCombineScaleFactorProperty());
    } else {
      SetProperty(TextCombineScaleFactorProperty(),
                  static_cast<float>(em) / static_cast<float>(width));
      finalSize.ISize(wm) = em;
    }
    // Make the characters be in an 1em square.
    if (finalSize.BSize(wm) != em) {
      fontBaseline =
          aMetrics.BlockStartAscent() + (em - finalSize.BSize(wm)) / 2;
      aMetrics.SetBlockStartAscent(fontBaseline);
      finalSize.BSize(wm) = em;
    }
  }
  aMetrics.SetSize(wm, finalSize);

  NS_ASSERTION(aMetrics.BlockStartAscent() >= 0, "Negative ascent???");
  NS_ASSERTION(
      (Style()->IsTextCombined() ? aMetrics.ISize(aMetrics.GetWritingMode())
                                 : aMetrics.BSize(aMetrics.GetWritingMode())) -
              aMetrics.BlockStartAscent() >=
          0,
      "Negative descent???");

  mAscent = fontBaseline;

  // Handle text that runs outside its normal bounds.
  nsRect boundingBox = RoundOut(textMetrics.mBoundingBox);
  if (mTextRun->IsVertical()) {
    // Swap line-relative textMetrics dimensions to physical coordinates.
    std::swap(boundingBox.x, boundingBox.y);
    std::swap(boundingBox.width, boundingBox.height);
    if (GetWritingMode().IsVerticalRL()) {
      boundingBox.x = -boundingBox.XMost();
      boundingBox.x += aMetrics.Width() - mAscent;
    } else {
      boundingBox.x += mAscent;
    }
  } else {
    boundingBox.y += mAscent;
  }
  aMetrics.SetOverflowAreasToDesiredBounds();
  aMetrics.InkOverflow().UnionRect(aMetrics.InkOverflow(), boundingBox);

  // When we have text decorations, we don't need to compute their overflow now
  // because we're guaranteed to do it later
  // (see nsLineLayout::RelativePositionFrames)
  UnionAdditionalOverflow(presContext, aLineLayout.LineContainerFrame(),
                          provider, &aMetrics.InkOverflow(), false, true);

  /////////////////////////////////////////////////////////////////////
  // Clean up, update state
  /////////////////////////////////////////////////////////////////////

  // If all our characters are discarded or collapsed, then trimmable width
  // from the last textframe should be preserved. Otherwise the trimmable width
  // from this textframe overrides. (Currently in CSS trimmable width can be
  // at most one space so there's no way for trimmable width from a previous
  // frame to accumulate with trimmable width from this frame.)
  if (transformedCharsFit > 0) {
    aLineLayout.SetTrimmableISize(NSToCoordFloor(trimmableWS.mAdvance));
    AddStateBits(TEXT_HAS_NONCOLLAPSED_CHARACTERS);
  }
  bool breakAfter = forceBreakAfter;
  if (!shouldSuppressLineBreak) {
    if (charsFit > 0 && charsFit == length &&
        textStyle->mHyphens != StyleHyphens::None &&
        HasSoftHyphenBefore(frag, mTextRun, offset, end)) {
      bool fits =
          textMetrics.mAdvanceWidth + provider.GetHyphenWidth() <= availWidth;
      // Record a potential break after final soft hyphen
      aLineLayout.NotifyOptionalBreakPosition(this, length, fits,
                                              gfxBreakPriority::eNormalBreak);
    }
    // length == 0 means either the text is empty or it's all collapsed away
    bool emptyTextAtStartOfLine = atStartOfLine && length == 0;
    if (!breakAfter && charsFit == length && !emptyTextAtStartOfLine &&
        transformedOffset + transformedLength == mTextRun->GetLength() &&
        (mTextRun->GetFlags2() & nsTextFrameUtils::Flags::HasTrailingBreak)) {
      // We placed all the text in the textrun and we have a break opportunity
      // at the end of the textrun. We need to record it because the following
      // content may not care about nsLineBreaker.

      // Note that because we didn't break, we can be sure that (thanks to the
      // code up above) textMetrics.mAdvanceWidth includes the width of any
      // trailing whitespace. So we need to subtract trimmableWidth here
      // because if we did break at this point, that much width would be
      // trimmed.
      if (textMetrics.mAdvanceWidth - trimmableWS.mAdvance > availWidth) {
        breakAfter = true;
      } else {
        aLineLayout.NotifyOptionalBreakPosition(this, length, true,
                                                gfxBreakPriority::eNormalBreak);
      }
    }
  }

  // Compute reflow status
  if (contentLength != maxContentLength) {
    aStatus.SetIncomplete();
  }

  if (charsFit == 0 && length > 0 && !usedHyphenation) {
    // Couldn't place any text
    aStatus.SetInlineLineBreakBeforeAndReset();
  } else if (contentLength > 0 &&
             mContentOffset + contentLength - 1 == newLineOffset) {
    // Ends in \n
    aStatus.SetInlineLineBreakAfter();
    aLineLayout.SetLineEndsInBR(true);
  } else if (breakAfter) {
    aStatus.SetInlineLineBreakAfter();
  }
  if (completedFirstLetter) {
    aLineLayout.SetFirstLetterStyleOK(false);
    aStatus.SetFirstLetterComplete();
  }
  if (brokeText && breakPriority == gfxBreakPriority::eWordWrapBreak) {
    aLineLayout.SetUsedOverflowWrap();
  }

  // Updated the cached NewlineProperty, or delete it.
  if (contentLength < maxContentLength &&
      textStyle->NewlineIsSignificant(this) &&
      (contentNewLineOffset < 0 ||
       mContentOffset + contentLength <= contentNewLineOffset)) {
    if (!cachedNewlineOffset) {
      cachedNewlineOffset = new NewlineProperty;
      if (NS_FAILED(mContent->SetProperty(
              nsGkAtoms::newline, cachedNewlineOffset,
              nsINode::DeleteProperty<NewlineProperty>))) {
        delete cachedNewlineOffset;
        cachedNewlineOffset = nullptr;
      }
      mContent->SetFlags(NS_HAS_NEWLINE_PROPERTY);
    }
    if (cachedNewlineOffset) {
      cachedNewlineOffset->mStartOffset = offset;
      cachedNewlineOffset->mNewlineOffset = contentNewLineOffset;
    }
  } else if (cachedNewlineOffset) {
    mContent->RemoveProperty(nsGkAtoms::newline);
    mContent->UnsetFlags(NS_HAS_NEWLINE_PROPERTY);
  }

  // Compute space and letter counts for justification, if required
  if ((lineContainer->StyleText()->mTextAlign == StyleTextAlign::Justify ||
       lineContainer->StyleText()->mTextAlignLast ==
           StyleTextAlignLast::Justify ||
       shouldSuppressLineBreak) &&
      !lineContainer->IsInSVGTextSubtree()) {
    AddStateBits(TEXT_JUSTIFICATION_ENABLED);
    Range range(uint32_t(offset), uint32_t(offset + charsFit));
    aLineLayout.SetJustificationInfo(provider.ComputeJustification(range));
  }

  SetLength(contentLength, &aLineLayout, ALLOW_FRAME_CREATION_AND_DESTRUCTION);

  InvalidateFrame();

#ifdef NOISY_REFLOW
  ListTag(stdout);
  printf(": desiredSize=%d,%d(b=%d) status=%x\n", aMetrics.Width(),
         aMetrics.Height(), aMetrics.BlockStartAscent(), aStatus);
#endif
}

/* virtual */
bool nsTextFrame::CanContinueTextRun() const {
  // We can continue a text run through a text frame
  return true;
}

nsTextFrame::TrimOutput nsTextFrame::TrimTrailingWhiteSpace(
    DrawTarget* aDrawTarget) {
  MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_IS_DIRTY | NS_FRAME_FIRST_REFLOW),
             "frame should have been reflowed");

  TrimOutput result;
  result.mChanged = false;
  result.mDeltaWidth = 0;

  AddStateBits(TEXT_END_OF_LINE);

  if (!GetTextRun(nsTextFrame::eInflated)) {
    // If reflow didn't create a textrun, there must have been no content once
    // leading whitespace was trimmed, so nothing more to do here.
    return result;
  }

  int32_t contentLength = GetContentLength();
  if (!contentLength) {
    return result;
  }

  gfxSkipCharsIterator start =
      EnsureTextRun(nsTextFrame::eInflated, aDrawTarget);
  NS_ENSURE_TRUE(mTextRun, result);

  uint32_t trimmedStart = start.GetSkippedOffset();

  const nsTextFragment* frag = TextFragment();
  TrimmedOffsets trimmed = GetTrimmedOffsets(frag);
  gfxSkipCharsIterator trimmedEndIter = start;
  const nsStyleText* textStyle = StyleText();
  gfxFloat delta = 0;
  uint32_t trimmedEnd =
      trimmedEndIter.ConvertOriginalToSkipped(trimmed.GetEnd());

  if (!HasAnyStateBits(TEXT_TRIMMED_TRAILING_WHITESPACE) &&
      trimmed.GetEnd() < GetContentEnd()) {
    gfxSkipCharsIterator end = trimmedEndIter;
    uint32_t endOffset =
        end.ConvertOriginalToSkipped(GetContentOffset() + contentLength);
    if (trimmedEnd < endOffset) {
      // We can't be dealing with tabs here ... they wouldn't be trimmed. So
      // it's OK to pass null for the line container.
      PropertyProvider provider(
          mTextRun, textStyle, frag, this, start, contentLength, nullptr, 0,
          nsTextFrame::eInflated, HasAnyStateBits(TEXT_START_OF_LINE));
      delta =
          mTextRun->GetAdvanceWidth(Range(trimmedEnd, endOffset), &provider);
      result.mChanged = true;
    }
  }

  gfxFloat advanceDelta;
  mTextRun->SetLineBreaks(Range(trimmedStart, trimmedEnd),
                          HasAnyStateBits(TEXT_START_OF_LINE), true,
                          &advanceDelta);
  if (advanceDelta != 0) {
    result.mChanged = true;
  }

  // aDeltaWidth is *subtracted* from our width.
  // If advanceDelta is positive then setting the line break made us longer,
  // so aDeltaWidth could go negative.
  result.mDeltaWidth = NSToCoordFloor(delta - advanceDelta);
  // If aDeltaWidth goes negative, that means this frame might not actually fit
  // anymore!!! We need higher level line layout to recover somehow.
  // If it's because the frame has a soft hyphen that is now being displayed,
  // this should actually be OK, because our reflow recorded the break
  // opportunity that allowed the soft hyphen to be used, and we wouldn't
  // have recorded the opportunity unless the hyphen fit (or was the first
  // opportunity on the line).
  // Otherwise this can/ really only happen when we have glyphs with special
  // shapes at the end of lines, I think. Breaking inside a kerning pair won't
  // do it because that would mean we broke inside this textrun, and
  // BreakAndMeasureText should make sure the resulting shaped substring fits.
  // Maybe if we passed a maxTextLength? But that only happens at direction
  // changes (so we wouldn't kern across the boundary) or for first-letter
  // (which always fits because it starts the line!).
  NS_WARNING_ASSERTION(result.mDeltaWidth >= 0,
                       "Negative deltawidth, something odd is happening");

#ifdef NOISY_TRIM
  ListTag(stdout);
  printf(": trim => %d\n", result.mDeltaWidth);
#endif
  return result;
}

OverflowAreas nsTextFrame::RecomputeOverflow(nsIFrame* aBlockFrame,
                                             bool aIncludeShadows) {
  RemoveProperty(WebRenderTextBounds());

  nsRect bounds(nsPoint(0, 0), GetSize());
  OverflowAreas result(bounds, bounds);

  gfxSkipCharsIterator iter = EnsureTextRun(nsTextFrame::eInflated);
  if (!mTextRun) {
    return result;
  }

  PropertyProvider provider(this, iter, nsTextFrame::eInflated, mFontMetrics);
  // Don't trim trailing space, in case we need to paint it as selected.
  provider.InitializeForDisplay(false);

  gfxTextRun::Metrics textMetrics =
      mTextRun->MeasureText(ComputeTransformedRange(provider),
                            gfxFont::LOOSE_INK_EXTENTS, nullptr, &provider);
  if (GetWritingMode().IsLineInverted()) {
    textMetrics.mBoundingBox.y = -textMetrics.mBoundingBox.YMost();
  }
  nsRect boundingBox = RoundOut(textMetrics.mBoundingBox);
  boundingBox += nsPoint(0, mAscent);
  if (mTextRun->IsVertical()) {
    // Swap line-relative textMetrics dimensions to physical coordinates.
    std::swap(boundingBox.x, boundingBox.y);
    std::swap(boundingBox.width, boundingBox.height);
  }
  nsRect& vis = result.InkOverflow();
  vis.UnionRect(vis, boundingBox);
  UnionAdditionalOverflow(PresContext(), aBlockFrame, provider, &vis, true,
                          aIncludeShadows);
  return result;
}

static void TransformChars(nsTextFrame* aFrame, const nsStyleText* aStyle,
                           const gfxTextRun* aTextRun, uint32_t aSkippedOffset,
                           const nsTextFragment* aFrag, int32_t aFragOffset,
                           int32_t aFragLen, nsAString& aOut) {
  nsAutoString fragString;
  char16_t* out;
  bool needsToMaskPassword = NeedsToMaskPassword(aFrame);
  if (aStyle->mTextTransform.IsNone() && !needsToMaskPassword &&
      aStyle->mWebkitTextSecurity == StyleTextSecurity::None) {
    // No text-transform, so we can copy directly to the output string.
    aOut.SetLength(aOut.Length() + aFragLen);
    out = aOut.EndWriting() - aFragLen;
  } else {
    // Use a temporary string as source for the transform.
    fragString.SetLength(aFragLen);
    out = fragString.BeginWriting();
  }

  // Copy the text, with \n and \t replaced by <space> if appropriate.
  MOZ_ASSERT(aFragOffset >= 0);
  for (uint32_t i = 0; i < static_cast<uint32_t>(aFragLen); ++i) {
    char16_t ch = aFrag->CharAt(static_cast<uint32_t>(aFragOffset) + i);
    if ((ch == '\n' && !aStyle->NewlineIsSignificant(aFrame)) ||
        (ch == '\t' && !aStyle->TabIsSignificant())) {
      ch = ' ';
    }
    out[i] = ch;
  }

  if (!aStyle->mTextTransform.IsNone() || needsToMaskPassword ||
      aStyle->mWebkitTextSecurity != StyleTextSecurity::None) {
    MOZ_ASSERT(aTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsTransformed);
    if (aTextRun->GetFlags2() & nsTextFrameUtils::Flags::IsTransformed) {
      // Apply text-transform according to style in the transformed run.
      char16_t maskChar =
          needsToMaskPassword ? 0 : aStyle->TextSecurityMaskChar();
      auto transformedTextRun =
          static_cast<const nsTransformedTextRun*>(aTextRun);
      nsAutoString convertedString;
      AutoTArray<bool, 50> charsToMergeArray;
      AutoTArray<bool, 50> deletedCharsArray;
      nsCaseTransformTextRunFactory::TransformString(
          fragString, convertedString, /* aGlobalTransform = */ Nothing(),
          maskChar, /* aCaseTransformsOnly = */ true, nullptr,
          charsToMergeArray, deletedCharsArray, transformedTextRun,
          aSkippedOffset);
      aOut.Append(convertedString);
    } else {
      // Should not happen (see assertion above), but as a fallback...
      aOut.Append(fragString);
    }
  }
}

static void LineStartsOrEndsAtHardLineBreak(nsTextFrame* aFrame,
                                            nsBlockFrame* aLineContainer,
                                            bool* aStartsAtHardBreak,
                                            bool* aEndsAtHardBreak) {
  bool foundValidLine;
  nsBlockInFlowLineIterator iter(aLineContainer, aFrame, &foundValidLine);
  if (!foundValidLine) {
    NS_ERROR("Invalid line!");
    *aStartsAtHardBreak = *aEndsAtHardBreak = true;
    return;
  }

  *aEndsAtHardBreak = !iter.GetLine()->IsLineWrapped();
  if (iter.Prev()) {
    *aStartsAtHardBreak = !iter.GetLine()->IsLineWrapped();
  } else {
    // Hit block boundary
    *aStartsAtHardBreak = true;
  }
}

bool nsTextFrame::AppendRenderedText(AppendRenderedTextState& aState,
                                     RenderedText& aResult) {
  if (HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
    // We don't trust dirty frames, especially when computing rendered text.
    return false;
  }

  // Ensure the text run and grab the gfxSkipCharsIterator for it
  gfxSkipCharsIterator iter = EnsureTextRun(nsTextFrame::eInflated);
  if (!mTextRun) {
    return false;
  }
  gfxSkipCharsIterator tmpIter = iter;

  // Check if the frame starts/ends at a hard line break, to determine
  // whether whitespace should be trimmed.
  bool startsAtHardBreak, endsAtHardBreak;
  if (!HasAnyStateBits(TEXT_START_OF_LINE | TEXT_END_OF_LINE)) {
    startsAtHardBreak = endsAtHardBreak = false;
  } else if (nsBlockFrame* thisLc = do_QueryFrame(FindLineContainer())) {
    if (thisLc != aState.mLineContainer) {
      // Setup line cursor when needed.
      aState.mLineContainer = thisLc;
      aState.mLineContainer->SetupLineCursorForQuery();
    }
    LineStartsOrEndsAtHardLineBreak(this, aState.mLineContainer,
                                    &startsAtHardBreak, &endsAtHardBreak);
  } else {
    // Weird situation where we have a line layout without a block.
    // No soft breaks occur in this situation.
    startsAtHardBreak = endsAtHardBreak = true;
  }

  // Whether we need to trim whitespaces after the text frame.
  // TrimmedOffsetFlags::Default will allow trimming; we set NoTrim* flags
  // in the cases where this should not occur.
  TrimmedOffsetFlags trimFlags = TrimmedOffsetFlags::Default;
  if (!IsAtEndOfLine() ||
      aState.mTrimTrailingWhitespace != TrailingWhitespace::Trim ||
      !endsAtHardBreak) {
    trimFlags |= TrimmedOffsetFlags::NoTrimAfter;
  }

  // Whether to trim whitespaces before the text frame.
  if (!startsAtHardBreak) {
    trimFlags |= TrimmedOffsetFlags::NoTrimBefore;
  }

  TrimmedOffsets trimmedOffsets =
      GetTrimmedOffsets(aState.mTextFrag, trimFlags);
  bool trimmedSignificantNewline =
      (trimmedOffsets.GetEnd() < GetContentEnd() ||
       (aState.mTrimTrailingWhitespace == TrailingWhitespace::Trim &&
        StyleText()->mWhiteSpaceCollapse ==
            StyleWhiteSpaceCollapse::PreserveBreaks)) &&
      HasSignificantTerminalNewline();
  uint32_t skippedToRenderedStringOffset =
      aState.mOffsetInRenderedString -
      tmpIter.ConvertOriginalToSkipped(trimmedOffsets.mStart);
  uint32_t nextOffsetInRenderedString =
      tmpIter.ConvertOriginalToSkipped(trimmedOffsets.GetEnd()) +
      (trimmedSignificantNewline ? 1 : 0) + skippedToRenderedStringOffset;

  if (aState.mOffsetType == TextOffsetType::OffsetsInRenderedText) {
    if (nextOffsetInRenderedString <= aState.mStartOffset) {
      aState.mOffsetInRenderedString = nextOffsetInRenderedString;
      return true;
    }
    if (!aState.mHaveOffsets) {
      aResult.mOffsetWithinNodeText = tmpIter.ConvertSkippedToOriginal(
          aState.mStartOffset - skippedToRenderedStringOffset);
      aResult.mOffsetWithinNodeRenderedText = aState.mStartOffset;
      aState.mHaveOffsets = true;
    }
    if (aState.mOffsetInRenderedString >= aState.mEndOffset) {
      return false;
    }
  } else {
    if (uint32_t(GetContentEnd()) <= aState.mStartOffset) {
      aState.mOffsetInRenderedString = nextOffsetInRenderedString;
      return true;
    }
    if (!aState.mHaveOffsets) {
      aResult.mOffsetWithinNodeText = aState.mStartOffset;
      // Skip trimmed space when computed the rendered text offset.
      int32_t clamped =
          std::max<int32_t>(aState.mStartOffset, trimmedOffsets.mStart);
      aResult.mOffsetWithinNodeRenderedText =
          tmpIter.ConvertOriginalToSkipped(clamped) +
          skippedToRenderedStringOffset;
      MOZ_ASSERT(aResult.mOffsetWithinNodeRenderedText >=
                         aState.mOffsetInRenderedString &&
                     aResult.mOffsetWithinNodeRenderedText <= INT32_MAX,
                 "Bad offset within rendered text");
      aState.mHaveOffsets = true;
    }
    if (uint32_t(mContentOffset) >= aState.mEndOffset) {
      return false;
    }
  }

  int32_t startOffset;
  int32_t endOffset;
  if (aState.mOffsetType == TextOffsetType::OffsetsInRenderedText) {
    startOffset = tmpIter.ConvertSkippedToOriginal(
        aState.mStartOffset - skippedToRenderedStringOffset);
    endOffset = tmpIter.ConvertSkippedToOriginal(aState.mEndOffset -
                                                 skippedToRenderedStringOffset);
  } else {
    startOffset = aState.mStartOffset;
    endOffset = std::min<uint32_t>(INT32_MAX, aState.mEndOffset);
  }

  // If startOffset and/or endOffset are inside of trimmedOffsets' range,
  // then clamp the edges of trimmedOffsets accordingly.
  int32_t origTrimmedOffsetsEnd = trimmedOffsets.GetEnd();
  trimmedOffsets.mStart =
      std::max<uint32_t>(trimmedOffsets.mStart, startOffset);
  trimmedOffsets.mLength =
      std::min<uint32_t>(origTrimmedOffsetsEnd, endOffset) -
      trimmedOffsets.mStart;

  if (trimmedOffsets.mLength > 0) {
    const nsStyleText* textStyle = StyleText();
    iter.SetOriginalOffset(trimmedOffsets.mStart);
    while (iter.GetOriginalOffset() < trimmedOffsets.GetEnd()) {
      int32_t runLength;
      bool isSkipped = iter.IsOriginalCharSkipped(&runLength);
      runLength = std::min(runLength,
                           trimmedOffsets.GetEnd() - iter.GetOriginalOffset());
      if (isSkipped) {
        MOZ_ASSERT(runLength >= 0);
        for (uint32_t i = 0; i < static_cast<uint32_t>(runLength); ++i) {
          const char16_t ch = aState.mTextFrag->CharAt(
              AssertedCast<uint32_t>(iter.GetOriginalOffset() + i));
          if (ch == CH_SHY) {
            // We should preserve soft hyphens. They can't be transformed.
            aResult.mString.Append(ch);
          }
        }
      } else {
        TransformChars(this, textStyle, mTextRun, iter.GetSkippedOffset(),
                       aState.mTextFrag, iter.GetOriginalOffset(), runLength,
                       aResult.mString);
      }
      iter.AdvanceOriginal(runLength);
    }
  }

  if (trimmedSignificantNewline && GetContentEnd() <= endOffset) {
    // A significant newline was trimmed off (we must be
    // white-space:pre-line). Put it back.
    aResult.mString.Append('\n');
  }
  aState.mOffsetInRenderedString = nextOffsetInRenderedString;

  return true;
}

nsIFrame::RenderedText nsTextFrame::GetRenderedText(
    uint32_t aStartOffset, uint32_t aEndOffset, TextOffsetType aOffsetType,
    TrailingWhitespace aTrimTrailingWhitespace) {
  MOZ_ASSERT(aStartOffset <= aEndOffset, "bogus offsets");
  MOZ_ASSERT(!GetPrevContinuation() ||
                 (aOffsetType == TextOffsetType::OffsetsInContentText &&
                  aStartOffset >= (uint32_t)GetContentOffset() &&
                  aEndOffset <= (uint32_t)GetContentEnd()),
             "Must be called on first-in-flow, or content offsets must be "
             "given and be within this frame.");

  // The handling of offsets could be more efficient...
  RenderedText result;
  AppendRenderedTextState state{aStartOffset, aEndOffset, aOffsetType,
                                aTrimTrailingWhitespace, TextFragment()};

  for (nsTextFrame* textFrame = this; textFrame;
       textFrame = textFrame->GetNextContinuation()) {
    if (!textFrame->AppendRenderedText(state, result)) {
      break;
    }
  }

  if (!state.mHaveOffsets) {
    result.mOffsetWithinNodeText = state.mTextFrag->GetLength();
    result.mOffsetWithinNodeRenderedText = state.mOffsetInRenderedString;
  }

  return result;
}

/* virtual */
bool nsTextFrame::IsEmpty() {
  NS_ASSERTION(
      !HasAllStateBits(TEXT_IS_ONLY_WHITESPACE | TEXT_ISNOT_ONLY_WHITESPACE),
      "Invalid state");

  // XXXldb Should this check compatibility mode as well???
  const nsStyleText* textStyle = StyleText();
  if (textStyle->WhiteSpaceIsSignificant()) {
    // When WhiteSpaceIsSignificant styles are in effect, we only treat the
    // frame as empty if its content really is entirely *empty* (not just
    // whitespace).
    return !GetContentLength();
  }

  if (HasAnyStateBits(TEXT_ISNOT_ONLY_WHITESPACE)) {
    return false;
  }

  if (HasAnyStateBits(TEXT_IS_ONLY_WHITESPACE)) {
    return true;
  }

  bool isEmpty = IsAllWhitespace(TextFragment(),
                                 textStyle->mWhiteSpaceCollapse !=
                                     StyleWhiteSpaceCollapse::PreserveBreaks);
  AddStateBits(isEmpty ? TEXT_IS_ONLY_WHITESPACE : TEXT_ISNOT_ONLY_WHITESPACE);
  return isEmpty;
}

#ifdef DEBUG_FRAME_DUMP
// Translate the mapped content into a string that's printable
void nsTextFrame::ToCString(nsCString& aBuf) const {
  // Get the frames text content
  const nsTextFragment* frag = TextFragment();
  if (!frag) {
    return;
  }

  const int32_t length = GetContentEnd() - mContentOffset;
  if (length <= 0) {
    // Negative lengths are possible during invalidation.
    return;
  }

  const uint32_t fragLength = AssertedCast<uint32_t>(GetContentEnd());
  uint32_t fragOffset = AssertedCast<uint32_t>(GetContentOffset());

  while (fragOffset < fragLength) {
    char16_t ch = frag->CharAt(fragOffset++);
    if (ch == '\r') {
      aBuf.AppendLiteral("\\r");
    } else if (ch == '\n') {
      aBuf.AppendLiteral("\\n");
    } else if (ch == '\t') {
      aBuf.AppendLiteral("\\t");
    } else if ((ch < ' ') || (ch >= 127)) {
      aBuf.Append(nsPrintfCString("\\u%04x", ch));
    } else {
      aBuf.Append(ch);
    }
  }
}

nsresult nsTextFrame::GetFrameName(nsAString& aResult) const {
  MakeFrameName(u"Text"_ns, aResult);
  nsAutoCString tmp;
  ToCString(tmp);
  tmp.SetLength(std::min<size_t>(tmp.Length(), 50u));
  aResult += u"\""_ns + NS_ConvertASCIItoUTF16(tmp) + u"\""_ns;
  return NS_OK;
}

void nsTextFrame::List(FILE* out, const char* aPrefix, ListFlags aFlags) const {
  nsCString str;
  ListGeneric(str, aPrefix, aFlags);

  if (!aFlags.contains(ListFlag::OnlyListDeterministicInfo)) {
    str += nsPrintfCString(" [run=%p]", static_cast<void*>(mTextRun));
  }

  // Output the first/last content offset and prev/next in flow info
  bool isComplete = uint32_t(GetContentEnd()) == GetContent()->TextLength();
  str += nsPrintfCString("[%d,%d,%c] ", GetContentOffset(), GetContentLength(),
                         isComplete ? 'T' : 'F');

  if (IsSelected()) {
    str += " SELECTED";
  }
  fprintf_stderr(out, "%s\n", str.get());
}

void nsTextFrame::ListTextRuns(FILE* out,
                               nsTHashSet<const void*>& aSeen) const {
  if (!mTextRun || aSeen.Contains(mTextRun)) {
    return;
  }
  aSeen.Insert(mTextRun);
  mTextRun->Dump(out);
}
#endif

void nsTextFrame::AdjustOffsetsForBidi(int32_t aStart, int32_t aEnd) {
  AddStateBits(NS_FRAME_IS_BIDI);
  if (mContent->HasFlag(NS_HAS_FLOWLENGTH_PROPERTY)) {
    mContent->RemoveProperty(nsGkAtoms::flowlength);
    mContent->UnsetFlags(NS_HAS_FLOWLENGTH_PROPERTY);
  }

  /*
   * After Bidi resolution we may need to reassign text runs.
   * This is called during bidi resolution from the block container, so we
   * shouldn't be holding a local reference to a textrun anywhere.
   */
  ClearTextRuns();

  nsTextFrame* prev = GetPrevContinuation();
  if (prev) {
    // the bidi resolver can be very evil when columns/pages are involved. Don't
    // let it violate our invariants.
    int32_t prevOffset = prev->GetContentOffset();
    aStart = std::max(aStart, prevOffset);
    aEnd = std::max(aEnd, prevOffset);
    prev->ClearTextRuns();
  }

  mContentOffset = aStart;
  SetLength(aEnd - aStart, nullptr, 0);
}

/**
 * @return true if this text frame ends with a newline character.  It should
 * return false if it is not a text frame.
 */
bool nsTextFrame::HasSignificantTerminalNewline() const {
  return ::HasTerminalNewline(this) && StyleText()->NewlineIsSignificant(this);
}

bool nsTextFrame::IsAtEndOfLine() const {
  return HasAnyStateBits(TEXT_END_OF_LINE);
}

Maybe<nscoord> nsTextFrame::GetNaturalBaselineBOffset(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext) const {
  if (aBaselineGroup == BaselineSharingGroup::Last) {
    return Nothing{};
  }

  if (!aWM.IsOrthogonalTo(GetWritingMode())) {
    if (aWM.IsCentralBaseline()) {
      return Some(GetLogicalUsedBorderAndPadding(aWM).BStart(aWM) +
                  ContentBSize(aWM) / 2);
    }
    return Some(mAscent);
  }

  // When the text frame has a writing mode orthogonal to the desired
  // writing mode, return a baseline coincides its parent frame.
  nsIFrame* parent = GetParent();
  nsPoint position = GetNormalPosition();
  nscoord parentAscent = parent->GetLogicalBaseline(aWM);
  if (aWM.IsVerticalRL()) {
    nscoord parentDescent = parent->GetSize().width - parentAscent;
    nscoord descent = parentDescent - position.x;
    return Some(GetSize().width - descent);
  }
  return Some(parentAscent - (aWM.IsVertical() ? position.x : position.y));
}

nscoord nsTextFrame::GetCaretBaseline() const {
  if (mAscent == 0 && HasAnyStateBits(TEXT_NO_RENDERED_GLYPHS)) {
    nsBlockFrame* container = do_QueryFrame(FindLineContainer());
    // TODO(emilio): Ideally we'd want to find out if only our line is empty,
    // but that's non-trivial to do, and realistically empty inlines and text
    // will get placed into a non-empty line unless all lines are empty, I
    // believe...
    if (container && container->LinesAreEmpty()) {
      nscoord blockSize = container->ContentBSize(GetWritingMode());
      return GetFontMetricsDerivedCaretBaseline(blockSize);
    }
  }
  return nsIFrame::GetCaretBaseline();
}

bool nsTextFrame::HasAnyNoncollapsedCharacters() {
  gfxSkipCharsIterator iter = EnsureTextRun(nsTextFrame::eInflated);
  int32_t offset = GetContentOffset(), offsetEnd = GetContentEnd();
  int32_t skippedOffset = iter.ConvertOriginalToSkipped(offset);
  int32_t skippedOffsetEnd = iter.ConvertOriginalToSkipped(offsetEnd);
  return skippedOffset != skippedOffsetEnd;
}

bool nsTextFrame::ComputeCustomOverflow(OverflowAreas& aOverflowAreas) {
  return ComputeCustomOverflowInternal(aOverflowAreas, true);
}

bool nsTextFrame::ComputeCustomOverflowInternal(OverflowAreas& aOverflowAreas,
                                                bool aIncludeShadows) {
  if (HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    return true;
  }

  nsIFrame* decorationsBlock;
  if (IsFloatingFirstLetterChild()) {
    decorationsBlock = GetParent();
  } else {
    nsIFrame* f = this;
    for (;;) {
      nsBlockFrame* fBlock = do_QueryFrame(f);
      if (fBlock) {
        decorationsBlock = fBlock;
        break;
      }

      f = f->GetParent();
      if (!f) {
        NS_ERROR("Couldn't find any block ancestor (for text decorations)");
        return nsIFrame::ComputeCustomOverflow(aOverflowAreas);
      }
    }
  }

  aOverflowAreas = RecomputeOverflow(decorationsBlock, aIncludeShadows);
  return nsIFrame::ComputeCustomOverflow(aOverflowAreas);
}

NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(JustificationAssignmentProperty, int32_t)

void nsTextFrame::AssignJustificationGaps(
    const mozilla::JustificationAssignment& aAssign) {
  int32_t encoded = (aAssign.mGapsAtStart << 8) | aAssign.mGapsAtEnd;
  static_assert(sizeof(aAssign) == 1,
                "The encoding might be broken if JustificationAssignment "
                "is larger than 1 byte");
  SetProperty(JustificationAssignmentProperty(), encoded);
}

mozilla::JustificationAssignment nsTextFrame::GetJustificationAssignment()
    const {
  int32_t encoded = GetProperty(JustificationAssignmentProperty());
  mozilla::JustificationAssignment result;
  result.mGapsAtStart = encoded >> 8;
  result.mGapsAtEnd = encoded & 0xFF;
  return result;
}

uint32_t nsTextFrame::CountGraphemeClusters() const {
  const nsTextFragment* frag = TextFragment();
  MOZ_ASSERT(frag, "Text frame must have text fragment");
  nsAutoString content;
  frag->AppendTo(content, AssertedCast<uint32_t>(GetContentOffset()),
                 AssertedCast<uint32_t>(GetContentLength()));
  return unicode::CountGraphemeClusters(content);
}

bool nsTextFrame::HasNonSuppressedText() const {
  if (HasAnyStateBits(TEXT_ISNOT_ONLY_WHITESPACE |
                      // If we haven't reflowed yet, or are currently doing so,
                      // just return true because we can't be sure.
                      NS_FRAME_FIRST_REFLOW | NS_FRAME_IN_REFLOW)) {
    return true;
  }

  if (!GetTextRun(nsTextFrame::eInflated)) {
    return false;
  }

  TrimmedOffsets offsets =
      GetTrimmedOffsets(TextFragment(), TrimmedOffsetFlags::NoTrimAfter);
  return offsets.mLength != 0;
}
