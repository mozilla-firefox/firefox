/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsBidiPresUtils_h_
#define nsBidiPresUtils_h_

#include "gfxContext.h"
#include "mozilla/intl/BidiClass.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "nsBidiUtils.h"
#include "nsCoord.h"
#include "nsHashKeys.h"
#include "nsLineBox.h"
#include "nsTArray.h"

#ifdef DrawText
#  undef DrawText
#endif

struct BidiParagraphData;
class BidiLineData;
class gfxContext;
class nsFontMetrics;
class nsIFrame;
class nsBlockFrame;
class nsPresContext;
struct nsSize;
template <class T>
class nsTHashtable;
namespace mozilla {
namespace intl {
class Bidi;
}
class ComputedStyle;
class LogicalMargin;
class WritingMode;
}  // namespace mozilla

/**
 * A structure representing some continuation state for each frame on the line,
 * used to determine the first and the last continuation frame for each
 * continuation chain on the line.
 */
struct nsFrameContinuationState : public nsVoidPtrHashKey {
  explicit nsFrameContinuationState(const void* aFrame)
      : nsVoidPtrHashKey(aFrame) {}

  /**
   * The first visual frame in the continuation chain containing this frame, or
   * nullptr if this frame is the first visual frame in the chain.
   */
  nsIFrame* mFirstVisualFrame{nullptr};

  /**
   * The number of frames in the continuation chain containing this frame, if
   * this frame is the first visual frame of the chain, or 0 otherwise.
   */
  uint32_t mFrameCount{0};

  /**
   * TRUE if this frame is the first visual frame of its continuation chain on
   * this line and the chain has some frames on the previous lines.
   */
  bool mHasContOnPrevLines{false};

  /**
   * TRUE if this frame is the first visual frame of its continuation chain on
   * this line and the chain has some frames left for next lines.
   */
  bool mHasContOnNextLines{false};
};

// A table of nsFrameContinuationState objects.
//
// This state is used between calls to nsBidiPresUtils::IsFirstOrLast.
struct nsContinuationStates {
  static constexpr size_t kArrayMax = 32;

  // We use the array to gather up all the continuation state objects.  If in
  // the end there are more than kArrayMax of them, we convert it to a hash
  // table for faster lookup.
  bool mUseTable = false;
  AutoTArray<nsFrameContinuationState, kArrayMax> mValues;
  nsTHashtable<nsFrameContinuationState> mTable;

  void Insert(nsIFrame* aFrame) {
    if (MOZ_UNLIKELY(mUseTable)) {
      mTable.PutEntry(aFrame);
      return;
    }
    if (MOZ_LIKELY(mValues.Length() < kArrayMax)) {
      mValues.AppendElement(aFrame);
      return;
    }
    for (const auto& entry : mValues) {
      mTable.PutEntry(entry.GetKey());
    }
    mTable.PutEntry(aFrame);
    mValues.Clear();
    mUseTable = true;
  }

  nsFrameContinuationState* Get(nsIFrame* aFrame) {
    MOZ_ASSERT(mValues.IsEmpty() != mTable.IsEmpty(),
               "expect entries to either be in mValues or in mTable");
    if (mUseTable) {
      return mTable.GetEntry(aFrame);
    }
    for (size_t i = 0, len = mValues.Length(); i != len; ++i) {
      if (mValues[i].GetKey() == aFrame) {
        return &mValues[i];
      }
    }
    return nullptr;
  }
};

/**
 * A structure representing a logical position which should be resolved
 * into its visual position during BiDi processing.
 */
struct nsBidiPositionResolve {
  // [in] Logical index within string.
  int32_t logicalIndex;
  // [out] Visual index within string.
  // If the logical position was not found, set to kNotFound.
  int32_t visualIndex;
  // [out] Visual position of the character, from the left (on the X axis), in
  // twips. Eessentially, this is the X position (relative to the rendering
  // context) where the text was drawn + the font metric of the visual string to
  // the left of the given logical position. If the logical position was not
  // found, set to kNotFound.
  int32_t visualLeftTwips;
  // [out] Visual width of the character, in twips.
  // If the logical position was not found, set to kNotFound.
  int32_t visualWidth;
};

class nsBidiPresUtils {
 public:
  typedef mozilla::gfx::DrawTarget DrawTarget;

  nsBidiPresUtils();
  ~nsBidiPresUtils();

  /**
   * Interface for the processor used by ProcessText. Used by process text to
   * collect information about the width of subruns and to notify where each
   * subrun should be rendered.
   */
  class BidiProcessor {
   public:
    virtual ~BidiProcessor() = default;

    /**
     * Sets the current text with the given length and the given direction.
     *
     * @remark The reason that the function gives a string instead of an index
     *  is that ProcessText copies and modifies the string passed to it, so
     *  passing an index would be impossible.
     *
     * @param aText The string of text.
     * @param aLength The length of the string of text.
     * @param aDirection The direction of the text. The string will never have
     *  mixed direction.
     */
    virtual void SetText(const char16_t* aText, int32_t aLength,
                         mozilla::intl::BidiDirection aDirection) = 0;

    /**
     * Returns the measured width of the text given in SetText. If SetText was
     * not called with valid parameters, the result of this call is undefined.
     * This call is guaranteed to only be called once between SetText calls.
     * Will be invoked before DrawText.
     */
    virtual nscoord GetWidth() = 0;

    /**
     * Draws the text given in SetText to a rendering context. If SetText was
     * not called with valid parameters, the result of this call is undefined.
     * This call is guaranteed to only be called once between SetText calls.
     *
     * @param aXOffset The offset of the left side of the substring to be drawn
     *  from the beginning of the overall string passed to ProcessText.
     */
    virtual void DrawText(nscoord aXOffset) = 0;
  };

  /**
   * Make Bidi engine calculate the embedding levels of the frames that are
   * descendants of a given block frame.
   *
   * @param aBlockFrame          The block frame
   *
   *  @lina 06/18/2000
   */
  static nsresult Resolve(nsBlockFrame* aBlockFrame);
  static nsresult ResolveParagraph(BidiParagraphData* aBpd);
  static void ResolveParagraphWithinBlock(BidiParagraphData* aBpd);

  /**
   * Reorder this line using Bidi engine.
   * Update frame array, following the new visual sequence.
   *
   * @return total inline size
   *
   * @lina 05/02/2000
   */
  static nscoord ReorderFrames(nsIFrame* aFirstFrameOnLine,
                               int32_t aNumFramesOnLine,
                               mozilla::WritingMode aLineWM,
                               const nsSize& aContainerSize, nscoord aStart);

  /**
   * Format Unicode text, taking into account bidi capabilities
   * of the platform. The formatting includes: reordering, Arabic shaping,
   * symmetric and numeric swapping, removing control characters.
   *
   * @lina 06/18/2000
   */
  static nsresult FormatUnicodeText(nsPresContext* aPresContext,
                                    char16_t* aText, int32_t& aTextLength,
                                    mozilla::intl::BidiClass aBidiClass);

  /**
   * Reorder plain text using the Unicode Bidi algorithm and send it to
   * a rendering context for rendering.
   *
   * @param[in] aText  the string to be rendered (in logical order)
   * @param aLength the number of characters in the string
   * @param aBaseLevel the base embedding level of the string
   * @param aPresContext the presentation context
   * @param aRenderingContext the rendering context to render to
   * @param aTextRunConstructionContext the rendering context to be used to
   * construct the textrun (affects font hinting)
   * @param aX the x-coordinate to render the string
   * @param aY the y-coordinate to render the string
   * @param[in,out] aPosResolve array of logical positions to resolve into
   * visual positions; can be nullptr if this functionality is not required
   * @param aPosResolveCount number of items in the aPosResolve array
   */
  static nsresult RenderText(const char16_t* aText, int32_t aLength,
                             mozilla::intl::BidiEmbeddingLevel aBaseLevel,
                             nsPresContext* aPresContext,
                             gfxContext& aRenderingContext,
                             DrawTarget* aTextRunConstructionDrawTarget,
                             nsFontMetrics& aFontMetrics, nscoord aX,
                             nscoord aY,
                             nsBidiPositionResolve* aPosResolve = nullptr,
                             int32_t aPosResolveCount = 0) {
    return ProcessTextForRenderingContext(
        aText, aLength, aBaseLevel, aPresContext, aRenderingContext,
        aTextRunConstructionDrawTarget, aFontMetrics, MODE_DRAW, aX, aY,
        aPosResolve, aPosResolveCount, nullptr);
  }

  static nscoord MeasureTextWidth(const char16_t* aText, int32_t aLength,
                                  mozilla::intl::BidiEmbeddingLevel aBaseLevel,
                                  nsPresContext* aPresContext,
                                  gfxContext& aRenderingContext,
                                  nsFontMetrics& aFontMetrics) {
    nscoord length;
    nsresult rv = ProcessTextForRenderingContext(
        aText, aLength, aBaseLevel, aPresContext, aRenderingContext,
        aRenderingContext.GetDrawTarget(), aFontMetrics, MODE_MEASURE, 0, 0,
        nullptr, 0, &length);
    return NS_SUCCEEDED(rv) ? length : 0;
  }

  /**
   * Check if a line is reordered, i.e., if the child frames are not
   * all laid out left-to-right.
   * @param aFirstFrameOnLine : first frame of the line to be tested
   * @param aNumFramesOnLine : number of frames on this line
   * @param[out] aLeftMost : leftmost frame on this line
   * @param[out] aRightMost : rightmost frame on this line
   */
  static bool CheckLineOrder(nsIFrame* aFirstFrameOnLine,
                             int32_t aNumFramesOnLine, nsIFrame** aLeftmost,
                             nsIFrame** aRightmost);

  /**
   * Get the frame to the right of the given frame, on the same line.
   * @param aFrame : We're looking for the frame to the right of this frame.
   *                 If null, return the leftmost frame on the line.
   * @param aFirstFrameOnLine : first frame of the line to be tested
   * @param aNumFramesOnLine : number of frames on this line
   */
  static nsIFrame* GetFrameToRightOf(const nsIFrame* aFrame,
                                     nsIFrame* aFirstFrameOnLine,
                                     int32_t aNumFramesOnLine);

  /**
   * Get the frame to the left of the given frame, on the same line.
   * @param aFrame : We're looking for the frame to the left of this frame.
   *                 If null, return the rightmost frame on the line.
   * @param aFirstFrameOnLine : first frame of the line to be tested
   * @param aNumFramesOnLine : number of frames on this line
   */
  static nsIFrame* GetFrameToLeftOf(const nsIFrame* aFrame,
                                    nsIFrame* aFirstFrameOnLine,
                                    int32_t aNumFramesOnLine);

  static nsIFrame* GetFirstLeaf(nsIFrame* aFrame);

  /**
   * Get the bidi data of the given (inline) frame.
   */
  static mozilla::FrameBidiData GetFrameBidiData(nsIFrame* aFrame);

  /**
   * Get the bidi embedding level of the given (inline) frame.
   */
  static mozilla::intl::BidiEmbeddingLevel GetFrameEmbeddingLevel(
      nsIFrame* aFrame);

  /**
   * Get the bidi base level of the given (inline) frame.
   */
  static mozilla::intl::BidiEmbeddingLevel GetFrameBaseLevel(
      const nsIFrame* aFrame);

  /**
   * Get the bidi base level (i.e. paragraph level) to use for the line that
   * starts with aFirstFrameOnLine, based on the first frame that will actually
   * have content on the line. Unlike GetFrameBaseLevel, this is safe to call
   * from line layout before the line's frames have been reflowed: it skips
   * empty text frames and looks at children that are about to be moved into
   * inline continuations.
   */
  static mozilla::intl::BidiEmbeddingLevel GetLineBaseLevel(
      const nsIFrame* aFirstFrameOnLine);

  /**
   * Get a mozilla::intl::BidiDirection representing the direction implied by
   * the bidi base level of the frame.
   * @return mozilla::intl::BidiDirection
   */
  static mozilla::intl::BidiDirection ParagraphDirection(
      const nsIFrame* aFrame) {
    return GetFrameBaseLevel(aFrame).Direction();
  }

  /**
   * Get a mozilla::intl::BidiDirection representing the direction implied by
   * the bidi embedding level of the frame.
   * @return mozilla::intl::BidiDirection
   */
  static mozilla::intl::BidiDirection FrameDirection(nsIFrame* aFrame) {
    return GetFrameEmbeddingLevel(aFrame).Direction();
  }

  static bool IsFrameInParagraphDirection(nsIFrame* aFrame) {
    return ParagraphDirection(aFrame) == FrameDirection(aFrame);
  }

  // This is faster than nsBidiPresUtils::IsFrameInParagraphDirection,
  // because it uses the frame pointer passed in without drilling down to
  // the leaf frame.
  static bool IsReversedDirectionFrame(const nsIFrame* aFrame) {
    mozilla::FrameBidiData bidiData = aFrame->GetBidiData();
    return !bidiData.embeddingLevel.IsSameDirection(bidiData.baseLevel);
  }

  enum Mode { MODE_DRAW, MODE_MEASURE };

  /**
   * Reorder plain text using the Unicode Bidi algorithm and send it to
   * a processor for rendering or measuring
   *
   * @param[in] aText  the string to be processed (in logical order)
   * @param aLength the number of characters in the string
   * @param aBaseLevel the base embedding level of the string
   * @param aPresContext the presentation context
   * @param aprocessor the bidi processor
   * @param aMode the operation to process
   *  MODE_DRAW - invokes DrawText on the processor for each substring
   *  MODE_MEASURE - does not invoke DrawText on the processor
   *  Note that the string is always measured, regardless of mode
   * @param[in,out] aPosResolve array of logical positions to resolve into
   *  visual positions; can be nullptr if this functionality is not required
   * @param aPosResolveCount number of items in the aPosResolve array
   * @param[out] aWidth Pointer to where the width will be stored (may be null)
   */
  static nsresult ProcessText(const char16_t* aText, size_t aLength,
                              mozilla::intl::BidiEmbeddingLevel aBaseLevel,
                              nsPresContext* aPresContext,
                              BidiProcessor& aprocessor, Mode aMode,
                              nsBidiPositionResolve* aPosResolve,
                              int32_t aPosResolveCount, nscoord* aWidth,
                              mozilla::intl::Bidi& aBidiEngine);

  /**
   * Use style attributes to determine the base paragraph level to pass to the
   * bidi algorithm.
   *
   * If |unicode-bidi| is set to "[-moz-]plaintext", returns
   * BidiEmbeddingLevel::DefaultLTR, in other words the direction is determined
   * from the first strong character in the text according to rules P2 and P3 of
   * the bidi algorithm, or LTR if there is no strong character.
   *
   * Otherwise returns BidiEmbeddingLevel::LTR or BidiEmbeddingLevel::RTL
   * depending on the value of |direction|
   */
  static mozilla::intl::BidiEmbeddingLevel BidiLevelFromStyle(
      mozilla::ComputedStyle* aComputedStyle);

 private:
  friend class BidiLineData;

  static nsresult ProcessTextForRenderingContext(
      const char16_t* aText, int32_t aLength,
      mozilla::intl::BidiEmbeddingLevel aBaseLevel, nsPresContext* aPresContext,
      gfxContext& aRenderingContext, DrawTarget* aTextRunConstructionDrawTarget,
      nsFontMetrics& aFontMetrics, Mode aMode,
      nscoord aX,                         // DRAW only
      nscoord aY,                         // DRAW only
      nsBidiPositionResolve* aPosResolve, /* may be null */
      int32_t aPosResolveCount, nscoord* aWidth /* may be null */);

  /**
   * Simplified form of ProcessText body, used when aText is a single Unicode
   * character (one UTF-16 codepoint, or a surrogate pair), or a run that is
   * known to have no bidi content.
   */
  static void ProcessSimpleRun(const char16_t* aText, size_t aLength,
                               mozilla::intl::BidiEmbeddingLevel aBaseLevel,
                               nsPresContext* aPresContext,
                               BidiProcessor& aprocessor, Mode aMode,
                               nsBidiPositionResolve* aPosResolve,
                               int32_t aPosResolveCount, nscoord* aWidth);

  /**
   * Traverse the child frames of the block element and:
   *  Set up an array of the frames in logical order
   *  Create a string containing the text content of all the frames
   *  If we encounter content that requires us to split the element into more
   *  than one paragraph for bidi resolution, resolve the paragraph up to that
   *  point.
   */
  static void TraverseFrames(nsIFrame* aCurrentFrame, BidiParagraphData* aBpd);

  /**
   * Perform a recursive "pre-traversal" of the child frames of a block or
   * inline container frame, to determine whether full bidi resolution is
   * actually needed.
   * This explores the same frames as TraverseFrames (above), but is less
   * expensive and may allow us to avoid performing the full TraverseFrames
   * operation.
   * @param   aFirstChild  frame to start traversal from
   * @param[in/out]  aCurrContent  the content node that we've most recently
   *          scanned for RTL characters (so that when descendant frames refer
   *          to the same content, we can avoid repeatedly scanning it).
   * @return  true if it finds that bidi is (or may be) required,
   *          false if no potentially-bidi content is present.
   */
  static bool ChildListMayRequireBidi(nsIFrame* aFirstChild,
                                      nsIContent** aCurrContent);

  /**
   * Position ruby content frames (ruby base/text frame).
   * Called from RepositionRubyFrame.
   */
  static void RepositionRubyContentFrame(
      nsIFrame* aFrame, mozilla::WritingMode aFrameWM,
      const mozilla::LogicalMargin& aBorderPadding);

  /*
   * Position ruby frames. Called from RepositionFrame.
   */
  static nscoord RepositionRubyFrame(
      nsIFrame* aFrame, nsContinuationStates* aContinuationStates,
      const mozilla::WritingMode aContainerWM,
      const mozilla::LogicalMargin& aBorderPadding);

  /*
   * Position aFrame and its descendants to their visual places. Also if aFrame
   * is not leaf, resize it to embrace its children.
   *
   * @param aFrame               The frame which itself and its children are
   *                             going to be repositioned
   * @param aIsEvenLevel         TRUE means the embedding level of this frame
   *                             is even (LTR)
   * @param aStartOrEnd          The distance to the start or the end of aFrame
   *                             without considering its inline margin. If the
   *                             container is reordering frames in reverse
   *                             direction, it's the distance to the end,
   *                             otherwise, it's the distance to the start.
   * @param aContinuationStates  A map from nsIFrame* to
   *                             nsFrameContinuationState
   * @return                     The isize aFrame takes, including margins.
   */
  static nscoord RepositionFrame(nsIFrame* aFrame, bool aIsEvenLevel,
                                 nscoord aStartOrEnd,
                                 nsContinuationStates* aContinuationStates,
                                 mozilla::WritingMode aContainerWM,
                                 bool aContainerReverseOrder,
                                 const nsSize& aContainerSize);

  /*
   * Initialize the continuation state(nsFrameContinuationState) to
   * (nullptr, 0) for aFrame and its descendants.
   *
   * @param aFrame               The frame which itself and its descendants will
   *                             be initialized
   * @param aContinuationStates  A map from nsIFrame* to
   *                             nsFrameContinuationState
   */
  static void InitContinuationStates(nsIFrame* aFrame,
                                     nsContinuationStates* aContinuationStates);

  /*
   * Determine if aFrame is first or last, and set aIsFirst and
   * aIsLast values. Also set continuation states of
   * aContinuationStates.
   *
   * A frame is first if it's the first appearance of its continuation
   * chain on the line and the chain is on its first line.
   * A frame is last if it's the last appearance of its continuation
   * chain on the line and the chain is on its last line.
   *
   * N.B: "First appearance" and "Last appearance" in the previous
   * paragraph refer to the frame's inline direction, not necessarily
   * the line's.
   *
   * @param aContinuationStates        A map from nsIFrame* to
   *                                    nsFrameContinuationState
   * @param[in] aSpanDirMatchesLineDir TRUE means that the inline
   *                                    direction of aFrame is the same
   *                                    as its container
   * @param[out] aIsFirst              TRUE means aFrame is first frame
   *                                    or continuation
   * @param[out] aIsLast               TRUE means aFrame is last frame
   *                                    or continuation
   */
  static void IsFirstOrLast(nsIFrame* aFrame,
                            nsContinuationStates* aContinuationStates,
                            bool aSpanInLineOrder /* in */,
                            bool& aIsFirst /* out */, bool& aIsLast /* out */);

  /**
   *  Adjust frame positions following their visual order
   *
   *  @param aFirstChild the first kid
   *  @return total inline size
   *
   *  @lina 04/11/2000
   */
  static nscoord RepositionInlineFrames(const BidiLineData& aBld,
                                        mozilla::WritingMode aLineWM,
                                        const nsSize& aContainerSize,
                                        nscoord aStart);

  /**
   * Helper method for Resolve()
   * Truncate a text frame to the end of a single-directional run and possibly
   * create a continuation frame for the remainder of its content.
   *
   * @param aFrame       the original frame
   * @param aLine        the line box containing aFrame
   * @param aNewFrame    [OUT] the new frame that was created
   * @param aStart       [IN] the start of the content mapped by aFrame (and
   *                          any fluid continuations)
   * @param aEnd         [IN] the offset of the end of the single-directional
   *                          text run.
   * @see Resolve()
   * @see RemoveBidiContinuation()
   */
  static inline void EnsureBidiContinuation(nsIFrame* aFrame,
                                            const nsLineList::iterator aLine,
                                            nsIFrame** aNewFrame,
                                            int32_t aStart, int32_t aEnd);

  /**
   * Helper method for Resolve()
   * Convert one or more bidi continuation frames created in a previous reflow
   * by EnsureBidiContinuation() into fluid continuations.
   * @param aFrame       the frame whose continuations are to be removed
   * @param aFirstIndex  index of aFrame in mLogicalFrames
   * @param aLastIndex   index of the last frame to be removed
   *
   * @see Resolve()
   * @see EnsureBidiContinuation()
   */
  static void RemoveBidiContinuation(BidiParagraphData* aBpd, nsIFrame* aFrame,
                                     int32_t aFirstIndex, int32_t aLastIndex);

  static void CalculateBidiClass(const char16_t* aText, int32_t& aOffset,
                                 int32_t aBidiClassLimit, int32_t& aRunLimit,
                                 int32_t& aRunLength, int32_t& aRunCount,
                                 mozilla::intl::BidiClass& aBidiClass,
                                 mozilla::intl::BidiClass& aPrevBidiClass);

  static void StripBidiControlCharacters(char16_t* aText, int32_t& aTextLength);
};

#endif /* nsBidiPresUtils_h_ */
