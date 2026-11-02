/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SelectionMovementUtils_h
#define mozilla_SelectionMovementUtils_h

#include "mozilla/Attributes.h"
#include "mozilla/EnumSet.h"
#include "mozilla/RangeBoundary.h"
#include "mozilla/Result.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "nsIFrame.h"

struct nsPrevNextBidiLevels;

namespace mozilla {

class PresShell;
enum class PeekOffsetOption : uint16_t;

namespace intl {
class BidiEmbeddingLevel;
}

struct MOZ_STACK_CLASS FrameAndOffset {
  [[nodiscard]] nsIContent* GetFrameContent() const {
    return mFrame ? mFrame->GetContent() : nullptr;
  }

  operator nsIFrame*() const { return mFrame; }

  explicit operator bool() const { return !!mFrame; }
  [[nodiscard]] bool operator!() const { return !mFrame; }

  nsIFrame* operator->() const {
    MOZ_ASSERT(mFrame);
    return mFrame;
  }

  /**
   * If mFrame is for a generated content, return the primary frame and offset
   * at before or after the generated content root. I.e., return a frame for the
   * generated content host. Otherwise, returns a copy of `this`.
   *
   * NOTE: This returns primary frame of the generated content host rather than
   * a continuation frame for mOffsetInFrameContent.
   */
  FrameAndOffset GetNonGeneratedFrameAndOffset() const;

  nsIFrame* mFrame = nullptr;
  // The offset in mFrame->GetContent().
  uint32_t mOffsetInFrameContent = 0;
};

struct MOZ_STACK_CLASS PrimaryFrameData : public FrameAndOffset {
  // Whether the caret should be put before or after the point. This is valid
  // only when mFrame is not nullptr.
  CaretAssociationHint mHint{0};  // Before
};

struct MOZ_STACK_CLASS CaretFrameData : public PrimaryFrameData {
  // The frame which is found only from a DOM point.  This frame becomes
  // different from mFrame when the point is around end of a line or
  // at a bidi text boundary.
  nsIFrame* mUnadjustedFrame = nullptr;
};

enum class ForceEditableRegion : bool { No, Yes };

class SelectionMovementUtils final {
 public:
  using PeekOffsetOptions = EnumSet<PeekOffsetOption>;

  /**
   * @brief Creates a new `RangeBoundary` which moves `aAmount` into
   * `aDirection` from the input range boundary.
   *
   * @param aRangeBoundary   The input range boundary.
   * @param aDirection       The direction into which the new boundary should be
   *                         moved.
   * @param aHint            The `CaretAssociationHint` (is the caret before or
   *                         after the boundary point)
   * @param aCaretBidiLevel  The `BidiEmbeddingLevel`.
   * @param aAmount          The amount which the range boundary should be
   *                         moved.
   * @param aOptions         Additional options, see `PeekOffsetOption`.
   * @param aAncestorLimiter The content node that limits where Selection may be
   *                         expanded to.
   *
   * @return Returns a new `RangeBoundary` which is moved from `aRangeBoundary`
   *         by `aAmount` into `aDirection`.
   */
  template <typename ParentType, typename RefType>
  static Result<RangeBoundaryBase<ParentType, RefType>, nsresult>
  MoveRangeBoundaryToSomewhere(
      const RangeBoundaryBase<ParentType, RefType>& aRangeBoundary,
      nsDirection aDirection, CaretAssociationHint aHint,
      intl::BidiEmbeddingLevel aCaretBidiLevel, nsSelectionAmount aAmount,
      PeekOffsetOptions aOptions,
      const dom::Element* aAncestorLimiter = nullptr);

  /**
   * Given a node and its child offset, return the nsIFrame and the offset into
   * that frame.
   *
   * @param aNode input parameter for the node to look at
   * @param aOffset offset into above node.
   */
  static FrameAndOffset GetFrameForNodeOffset(const nsIContent* aNode,
                                              uint32_t aOffset,
                                              CaretAssociationHint aHint);

  /**
   * Return the first visible point in or at a leaf node in aRange or the first
   * unselectable content if aRange starts from a selectable container. E.g.,
   * return the start of the first visible `Text` or the position of the first
   * visible leaf element.  I.e., the result may be a good point to put a UI for
   * showing something around the start boundary.
   *
   * NOTE: This won't return any boundary point in subtrees from the tree
   * containing the start container of aRange due to ContentIteratorBase's
   * limitation. See bug 2001511.
   *
   * @param aRange Must not be collapsed because this returns a point in aRange
   * so that this requires the limitation of scanning forward.
   * @return A position in a `Text` or a position at an element.
   */
  [[nodiscard]] static RawRangeBoundary GetFirstVisiblePointAtLeaf(
      const dom::AbstractRange& aRange);

  /**
   * Return the last visible point in or at a leaf node in aRange or the last
   * unselectable content if aRange ends in a selectable container. E.g., return
   * the end of the last visible `Text` or the position of the last visible leaf
   * element. I.e., the result may be a good point to put a UI for showing
   * something around the end boundary.
   *
   * NOTE: This won't return any boundary point in subtrees of the tree
   * containing the end container of aRange due to ContentIteratorBase's
   * limitation. See bug 2001511.
   *
   * @param aRange Must not be collapsed because this returns a point in aRange
   * so that this requires the limitation of scanning forward.
   * @return A position in a `Text` or a position at an element.
   */
  [[nodiscard]] static RawRangeBoundary GetLastVisiblePointAtLeaf(
      const dom::AbstractRange& aRange);

  /**
   * GetPrevNextBidiLevels will return the frames and associated Bidi levels of
   * the characters logically before and after a (collapsed) selection.
   *
   * @param aNode is the node containing the selection
   * @param aContentOffset is the offset of the selection in the node
   * @param aJumpLines
   *   If true, look across line boundaries.
   *   If false, behave as if there were base-level frames at line edges.
   * @param aAncestorLimiter  If set, this refers only the descendants.
   *
   * @return A struct holding the before/after frame and the before/after
   * level.
   *
   * At the beginning and end of each line there is assumed to be a frame with
   * Bidi level equal to the paragraph embedding level.
   *
   * In these cases the before frame and after frame respectively will be
   * nullptr.
   */
  static nsPrevNextBidiLevels GetPrevNextBidiLevels(
      nsIContent* aNode, uint32_t aContentOffset, CaretAssociationHint aHint,
      bool aJumpLines, const dom::Element* aAncestorLimiter);

  /**
   * PeekOffsetForCaretMove() only peek offset for caret move from the specified
   * point of the normal selection.  I.e., won't change selection ranges nor
   * bidi information.
   */
  static Result<PeekOffsetStruct, nsresult> PeekOffsetForCaretMove(
      nsIContent* aContent, uint32_t aOffset, nsDirection aDirection,
      CaretAssociationHint aHint, intl::BidiEmbeddingLevel aCaretBidiLevel,
      const nsSelectionAmount aAmount, const nsPoint& aDesiredCaretPos,
      PeekOffsetOptions aOptions, const dom::Element* aAncestorLimiter);

  /**
   * IsIntraLineCaretMove() is a helper method for PeekOffsetForCaretMove()
   * and CreateRangeExtendedToSomwhereFromNormalSelection().  This returns
   * whether aAmount is intra line move or is crossing hard line break.
   * This returns error if aMount is not supported by the methods.
   */
  static Result<bool, nsresult> IsIntraLineCaretMove(
      nsSelectionAmount aAmount) {
    switch (aAmount) {
      case eSelectCharacter:
      case eSelectCluster:
      case eSelectWord:
      case eSelectWordNoSpace:
      case eSelectBeginLine:
      case eSelectEndLine:
      case eSelectParagraph:
        return true;
      case eSelectLine:
        return false;
      default:
        return Err(NS_ERROR_FAILURE);
    }
  }

  /**
   * Return a frame for considering caret geometry.
   *
   * @param aFrameSelection     [optional] If this is specified and selection in
   *                            aContent is not managed by the specified
   *                            instance, return nullptr.
   * @param aContentNode        The content node where selection is collapsed.
   * @param aOffset             Collapsed position in aContentNode
   * @param aFrameHint          Caret association hint.
   * @param aBidiLevel
   * @param aForceEditableRegion    Whether selection should be limited in
   *                                editable region or not.
   */
  static CaretFrameData GetCaretFrameForNodeOffset(
      const nsFrameSelection* aFrameSelection, nsIContent* aContentNode,
      uint32_t aOffset, CaretAssociationHint aFrameHint,
      intl::BidiEmbeddingLevel aBidiLevel,
      ForceEditableRegion aForceEditableRegion);

  static bool AdjustFrameForLineStart(nsIFrame*& aFrame,
                                      uint32_t& aFrameOffset);

  /**
   * Get primary frame and some other data for putting caret or extending
   * selection at the point.
   */
  static PrimaryFrameData GetPrimaryFrameForCaret(
      nsIContent* aContent, uint32_t aOffset, bool aVisual,
      CaretAssociationHint aHint, intl::BidiEmbeddingLevel aCaretBidiLevel);

 private:
  /**
   * GetFrameFromLevel will scan in a given direction
   * until it finds a frame with a Bidi level less than or equal to a given
   * level. It will return the last frame before this.
   *
   * @param aPresContext is the context to use
   * @param aFrameIn is the frame to start from
   * @param aDirection is the direction to scan
   * @param aBidiLevel is the level to search for
   */
  static Result<nsIFrame*, nsresult> GetFrameFromLevel(
      nsIFrame* aFrameIn, nsDirection aDirection,
      intl::BidiEmbeddingLevel aBidiLevel);

  // This is helper method for GetPrimaryFrameForCaret.
  // If aVisual is true, this returns caret frame.
  // If false, this returns primary frame.
  static PrimaryFrameData GetPrimaryOrCaretFrameForNodeOffset(
      nsIContent* aContent, uint32_t aOffset, bool aVisual,
      CaretAssociationHint aHint, intl::BidiEmbeddingLevel aCaretBidiLevel);
};

}  // namespace mozilla

#endif  // #ifndef mozilla_SelectionMovementUtils_h
