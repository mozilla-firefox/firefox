/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WritingModes_h_
#define WritingModes_h_

#include <ostream>

#include "mozilla/ComputedStyle.h"
#include "mozilla/EnumSet.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "nsRect.h"
#include "nsStyleStruct.h"

// It is the caller's responsibility to operate on logical-coordinate objects
// with matched writing modes. Failure to do so will be a runtime bug; the
// compiler can't catch it, but in debug mode, we'll throw an assertion.
// NOTE that in non-debug builds, a writing mode mismatch error will NOT be
// detected, yet the results will be nonsense (and may lead to further layout
// failures). Therefore, it is important to test (and fuzz-test) writing-mode
// support using debug builds.

// Methods in logical-coordinate classes that take another logical-coordinate
// object as a parameter should call CHECK_WRITING_MODE on it to verify that
// the writing modes match.
// (In some cases, there are internal (private) methods that don't do this;
// such methods should only be used by other methods that have already checked
// the writing modes.)
// The check ignores the StyleWritingMode::VERTICAL_SIDEWAYS and
// StyleWritingMode::TEXT_SIDEWAYS bit of writing mode, because
// this does not affect the interpretation of logical coordinates.

#define CHECK_WRITING_MODE(param)                                           \
  NS_ASSERTION(param.IgnoreSideways() == GetWritingMode().IgnoreSideways(), \
               "writing-mode mismatch")

namespace mozilla {

namespace widget {
struct IMENotification;
}  // namespace widget

// Logical axis, edge, side and corner constants for use in various places.
enum class LogicalAxis : uint8_t {
  Block,
  Inline,
};
enum class LogicalEdge : uint8_t { Start, End };

enum class LogicalSide : uint8_t {
  BStart,
  BEnd,
  IStart,
  IEnd,
};

enum class LogicalCorner : uint8_t {
  BStartIStart,
  BStartIEnd,
  BEndIEnd,
  BEndIStart,
};

// Physical axis constants.
enum class PhysicalAxis : uint8_t { Vertical, Horizontal };

using PhysicalAxes = EnumSet<PhysicalAxis>;
static constexpr PhysicalAxes kPhysicalAxesBoth{PhysicalAxis::Vertical,
                                                PhysicalAxis::Horizontal};

inline StyleLogicalAxis ToStyleLogicalAxis(LogicalAxis aLogicalAxis) {
  return StyleLogicalAxis(aLogicalAxis == LogicalAxis::Block
                              ? StyleLogicalAxis::Block
                              : StyleLogicalAxis::Inline);
}

inline LogicalAxis GetOrthogonalAxis(LogicalAxis aAxis) {
  return aAxis == LogicalAxis::Block ? LogicalAxis::Inline : LogicalAxis::Block;
}

inline bool IsInline(LogicalSide aSide) {
  return (aSide == LogicalSide::IStart) || (aSide == LogicalSide::IEnd);
}

inline bool IsBlock(LogicalSide aSide) { return !IsInline(aSide); }

inline bool IsEnd(LogicalSide aSide) {
  return (aSide == LogicalSide::BEnd) || (aSide == LogicalSide::IEnd);
}

inline bool IsStart(LogicalSide aSide) { return !IsEnd(aSide); }

inline LogicalAxis GetAxis(LogicalSide aSide) {
  return IsInline(aSide) ? LogicalAxis::Inline : LogicalAxis::Block;
}

inline LogicalEdge GetEdge(LogicalSide aSide) {
  return IsEnd(aSide) ? LogicalEdge::End : LogicalEdge::Start;
}

inline LogicalEdge GetOppositeEdge(LogicalEdge aEdge) {
  return aEdge == LogicalEdge::Start ? LogicalEdge::End : LogicalEdge::Start;
}

inline LogicalSide MakeLogicalSide(LogicalAxis aAxis, LogicalEdge aEdge) {
  if (aAxis == LogicalAxis::Inline) {
    return aEdge == LogicalEdge::Start ? LogicalSide::IStart
                                       : LogicalSide::IEnd;
  }
  return aEdge == LogicalEdge::Start ? LogicalSide::BStart : LogicalSide::BEnd;
}

inline LogicalSide GetOppositeSide(LogicalSide aSide) {
  return MakeLogicalSide(GetAxis(aSide), GetOppositeEdge(GetEdge(aSide)));
}

enum class LineRelativeDir : uint8_t {
  Over = static_cast<uint8_t>(LogicalSide::BStart),
  Under = static_cast<uint8_t>(LogicalSide::BEnd),
  Left = static_cast<uint8_t>(LogicalSide::IStart),
  Right = static_cast<uint8_t>(LogicalSide::IEnd)
};

/**
 * mozilla::WritingMode is an immutable class representing a
 * writing mode.
 *
 * It efficiently stores the writing mode and can rapidly compute
 * interesting things about it for use in layout.
 *
 * Writing modes are computed from the CSS 'direction',
 * 'writing-mode', and 'text-orientation' properties.
 * See CSS3 Writing Modes for more information
 *   http://www.w3.org/TR/css3-writing-modes/
 */
class WritingMode {
 public:
  /**
   * Absolute inline flow direction
   */
  enum class InlineDir : uint8_t {
    LTR,  // text flows horizontally left to right
    RTL,  // text flows horizontally right to left
    TTB,  // text flows vertically top to bottom
    BTT,  // text flows vertically bottom to top
  };

  /**
   * Absolute block flow direction
   */
  enum class BlockDir : uint8_t {
    TB,  // horizontal lines stack top to bottom
    RL,  // vertical lines stack right to left
    LR,  // vertical lines stack left to right
  };

  /**
   * Return the absolute inline flow direction as an InlineDir
   */
  InlineDir GetInlineDir() const {
    if (IsVertical()) {
      return IsInlineReversed() ? InlineDir::BTT : InlineDir::TTB;
    }
    return IsInlineReversed() ? InlineDir::RTL : InlineDir::LTR;
  }

  /**
   * Return the absolute block flow direction as a BlockDir
   */
  BlockDir GetBlockDir() const {
    if (IsVertical()) {
      return mWritingMode & StyleWritingMode::VERTICAL_LR ? BlockDir::LR
                                                          : BlockDir::RL;
    }
    return BlockDir::TB;
  }

  /**
   * Return true if the inline flow direction is against physical direction
   * (i.e. right-to-left or bottom-to-top).
   * This occurs when writing-mode is sideways-lr OR direction is rtl (but not
   * if both of those are true).
   */
  bool IsInlineReversed() const {
    return !!(mWritingMode & StyleWritingMode::INLINE_REVERSED);
  }

  /**
   * Return true if bidi direction is LTR.
   */
  bool IsBidiLTR() const { return !IsBidiRTL(); }

  /**
   * Return true if bidi direction is RTL.
   */
  bool IsBidiRTL() const { return !!(mWritingMode & StyleWritingMode::RTL); }

  /**
   * True if it is vertical and vertical-lr, or is horizontal and bidi LTR.
   */
  bool IsPhysicalLTR() const {
    return IsVertical() ? IsVerticalLR() : IsBidiLTR();
  }

  /**
   * True if it is vertical and vertical-rl, or is horizontal and bidi RTL.
   */
  bool IsPhysicalRTL() const {
    return IsVertical() ? IsVerticalRL() : IsBidiRTL();
  }

  /**
   * True if vertical-mode block direction is LR (convenience method).
   */
  bool IsVerticalLR() const { return GetBlockDir() == BlockDir::LR; }

  /**
   * True if vertical-mode block direction is RL (convenience method).
   */
  bool IsVerticalRL() const { return GetBlockDir() == BlockDir::RL; }

  /**
   * True if vertical writing mode, i.e. when
   * writing-mode: vertical-lr | vertical-rl | sideways-lr | sideways-rl.
   */
  bool IsVertical() const {
    return !!(mWritingMode & StyleWritingMode::VERTICAL);
  }

  /**
   * True if line-over/line-under are inverted from block-start/block-end.
   * This is true only when writing-mode is vertical-lr.
   */
  bool IsLineInverted() const {
    return !!(mWritingMode & StyleWritingMode::LINE_INVERTED);
  }

  /**
   * Block-axis flow-relative to line-relative factor.
   * May be used as a multiplication factor for block-axis coordinates
   * to convert between flow- and line-relative coordinate systems (e.g.
   * positioning an over- or under-line decoration).
   */
  int FlowRelativeToLineRelativeFactor() const {
    return IsLineInverted() ? -1 : 1;
  }

  /**
   * True if vertical sideways writing mode, i.e. when
   * writing-mode: sideways-lr | sideways-rl.
   */
  bool IsVerticalSideways() const {
    return !!(mWritingMode & StyleWritingMode::VERTICAL_SIDEWAYS);
  }

  bool IsUpright() const {
    return !!(mWritingMode & StyleWritingMode::UPRIGHT);
  }

  /**
   * True if this is writing-mode: sideways-rl (convenience method).
   */
  bool IsSidewaysRL() const { return IsVerticalRL() && IsVerticalSideways(); }

  /**
   * True if this is writing-mode: sideways-lr (convenience method).
   */
  bool IsSidewaysLR() const { return IsVerticalLR() && IsVerticalSideways(); }

  /**
   * True if either text-orientation or writing-mode will force all text to be
   * rendered sideways in vertical lines, in which case we should prefer an
   * alphabetic baseline; otherwise, the default is centered.
   *
   * Note that some glyph runs may be rendered sideways even if this is false,
   * due to text-orientation:mixed resolution, but in that case the dominant
   * baseline remains centered.
   */
  bool IsSideways() const {
    return !!(mWritingMode & (StyleWritingMode::VERTICAL_SIDEWAYS |
                              StyleWritingMode::TEXT_SIDEWAYS));
  }

#ifdef DEBUG
  // Used by CHECK_WRITING_MODE to compare modes without regard for the
  // StyleWritingMode::VERTICAL_SIDEWAYS or StyleWritingMode::TEXT_SIDEWAYS
  // flags.
  WritingMode IgnoreSideways() const {
    return WritingMode(mWritingMode._0 & ~(StyleWritingMode::VERTICAL_SIDEWAYS |
                                           StyleWritingMode::TEXT_SIDEWAYS)
                                              ._0);
  }
#endif

  /**
   * Return true if boxes with this writing mode should use central baselines.
   */
  bool IsCentralBaseline() const { return IsVertical() && !IsSideways(); }

  /**
   * Return true if boxes with this writing mode should use alphabetical
   * baselines.
   */
  bool IsAlphabeticalBaseline() const { return !IsCentralBaseline(); }

  /**
   * Convert LogicalAxis to PhysicalAxis given the current writing mode.
   */
  mozilla::PhysicalAxis PhysicalAxis(LogicalAxis aAxis) const {
    const bool isInline = aAxis == LogicalAxis::Inline;
    return isInline == IsVertical() ? PhysicalAxis::Vertical
                                    : PhysicalAxis::Horizontal;
  }

  static mozilla::Side PhysicalSideForBlockAxis(uint8_t aWritingModeValue,
                                                LogicalEdge aEdge) {
    // indexes are StyleWritingModeProperty values, which are the same as these
    // two-bit values:
    //   bit 0 = the StyleWritingMode::VERTICAL value
    //   bit 1 = the StyleWritingMode::VERTICAL_LR value
    static const mozilla::Side kLogicalBlockSides[][2] = {
        {eSideTop, eSideBottom},  // horizontal-tb
        {eSideRight, eSideLeft},  // vertical-rl
        {eSideBottom, eSideTop},  // (horizontal-bt)
        {eSideLeft, eSideRight},  // vertical-lr
    };

    // Ignore the SidewaysMask bit of the writing-mode value, as this has no
    // effect on the side mappings.
    aWritingModeValue &= ~kWritingModeSidewaysMask;

    // What's left of the writing-mode should be in the range 0-3:
    NS_ASSERTION(aWritingModeValue < 4, "invalid aWritingModeValue value");

    return kLogicalBlockSides[aWritingModeValue][static_cast<uint8_t>(aEdge)];
  }

  mozilla::Side PhysicalSideForInlineAxis(LogicalEdge aEdge) const {
    // indexes are four-bit values:
    //   bit 0 = the StyleWritingMode::VERTICAL value
    //   bit 1 = the StyleWritingMode::INLINE_REVERSED value
    //   bit 2 = the StyleWritingMode::VERTICAL_LR value
    //   bit 3 = the StyleWritingMode::LINE_INVERTED value
    // Not all of these combinations can actually be specified via CSS: there
    // is no horizontal-bt writing-mode, and no text-orientation value that
    // produces "inverted" text. (The former 'sideways-left' value, no longer
    // in the spec, would have produced this in vertical-rl mode.)
    static const mozilla::Side kLogicalInlineSides[][2] = {
        {eSideLeft, eSideRight},  // horizontal-tb               ltr
        {eSideTop, eSideBottom},  // vertical-rl                 ltr
        {eSideRight, eSideLeft},  // horizontal-tb               rtl
        {eSideBottom, eSideTop},  // vertical-rl                 rtl
        {eSideRight, eSideLeft},  // (horizontal-bt)  (inverted) ltr
        {eSideTop, eSideBottom},  // sideways-lr                 rtl
        {eSideLeft, eSideRight},  // (horizontal-bt)  (inverted) rtl
        {eSideBottom, eSideTop},  // sideways-lr                 ltr
        {eSideLeft, eSideRight},  // horizontal-tb    (inverted) rtl
        {eSideTop, eSideBottom},  // vertical-rl      (inverted) rtl
        {eSideRight, eSideLeft},  // horizontal-tb    (inverted) ltr
        {eSideBottom, eSideTop},  // vertical-rl      (inverted) ltr
        {eSideLeft, eSideRight},  // (horizontal-bt)             ltr
        {eSideTop, eSideBottom},  // vertical-lr                 ltr
        {eSideRight, eSideLeft},  // (horizontal-bt)             rtl
        {eSideBottom, eSideTop},  // vertical-lr                 rtl
    };

    // Inline axis sides depend on all three of writing-mode, text-orientation
    // and direction, which are encoded in the StyleWritingMode::VERTICAL,
    // StyleWritingMode::INLINE_REVERSED, StyleWritingMode::VERTICAL_LR and
    // StyleWritingMode::LINE_INVERTED bits.  Use these four bits to index into
    // kLogicalInlineSides.
    static_assert(StyleWritingMode::VERTICAL._0 == 0x01 &&
                      StyleWritingMode::INLINE_REVERSED._0 == 0x02 &&
                      StyleWritingMode::VERTICAL_LR._0 == 0x04 &&
                      StyleWritingMode::LINE_INVERTED._0 == 0x08,
                  "Unexpected values for StyleWritingMode constants!");
    uint8_t index = mWritingMode._0 & 0x0F;
    return kLogicalInlineSides[index][static_cast<uint8_t>(aEdge)];
  }

  /**
   * Returns the physical side corresponding to the specified logical side,
   * given the current writing mode.
   */
  mozilla::Side PhysicalSide(LogicalSide aSide) const {
    if (IsBlock(aSide)) {
      static_assert(StyleWritingMode::VERTICAL._0 == 0x01 &&
                        StyleWritingMode::VERTICAL_LR._0 == 0x04,
                    "Unexpected values for StyleWritingMode constants!");
      const uint8_t wm =
          ((mWritingMode & StyleWritingMode::VERTICAL_LR)._0 >> 1) |
          (mWritingMode & StyleWritingMode::VERTICAL)._0;
      return PhysicalSideForBlockAxis(wm, GetEdge(aSide));
    }

    return PhysicalSideForInlineAxis(GetEdge(aSide));
  }

  /**
   * Returns the logical side corresponding to the specified physical side,
   * given the current writing mode.
   * (This is the inverse of the PhysicalSide() method above.)
   */
  LogicalSide LogicalSideForPhysicalSide(mozilla::Side aSide) const {
    // clang-format off
    // indexes are four-bit values:
    //   bit 0 = the StyleWritingMode::VERTICAL value
    //   bit 1 = the StyleWritingMode::INLINE_REVERSED value
    //   bit 2 = the StyleWritingMode::VERTICAL_LR value
    //   bit 3 = the StyleWritingMode::LINE_INVERTED value
    static const LogicalSide kPhysicalToLogicalSides[][4] = {
      // top                right
      // bottom             left
      { LogicalSide::BStart, LogicalSide::IEnd,
        LogicalSide::BEnd,   LogicalSide::IStart },  // horizontal-tb         ltr
      { LogicalSide::IStart, LogicalSide::BStart,
        LogicalSide::IEnd,   LogicalSide::BEnd   },  // vertical-rl           ltr
      { LogicalSide::BStart, LogicalSide::IStart,
        LogicalSide::BEnd,   LogicalSide::IEnd   },  // horizontal-tb         rtl
      { LogicalSide::IEnd,   LogicalSide::BStart,
        LogicalSide::IStart, LogicalSide::BEnd   },  // vertical-rl           rtl
      { LogicalSide::BEnd,   LogicalSide::IStart,
        LogicalSide::BStart, LogicalSide::IEnd   },  // (horizontal-bt) (inv) ltr
      { LogicalSide::IStart, LogicalSide::BEnd,
        LogicalSide::IEnd,   LogicalSide::BStart },  // vertical-lr   sw-left rtl
      { LogicalSide::BEnd,   LogicalSide::IEnd,
        LogicalSide::BStart, LogicalSide::IStart },  // (horizontal-bt) (inv) rtl
      { LogicalSide::IEnd,   LogicalSide::BEnd,
        LogicalSide::IStart, LogicalSide::BStart },  // vertical-lr   sw-left ltr
      { LogicalSide::BStart, LogicalSide::IEnd,
        LogicalSide::BEnd,   LogicalSide::IStart },  // horizontal-tb   (inv) rtl
      { LogicalSide::IStart, LogicalSide::BStart,
        LogicalSide::IEnd,   LogicalSide::BEnd   },  // vertical-rl   sw-left rtl
      { LogicalSide::BStart, LogicalSide::IStart,
        LogicalSide::BEnd,   LogicalSide::IEnd   },  // horizontal-tb   (inv) ltr
      { LogicalSide::IEnd,   LogicalSide::BStart,
        LogicalSide::IStart, LogicalSide::BEnd   },  // vertical-rl   sw-left ltr
      { LogicalSide::BEnd,   LogicalSide::IEnd,
        LogicalSide::BStart, LogicalSide::IStart },  // (horizontal-bt)       ltr
      { LogicalSide::IStart, LogicalSide::BEnd,
        LogicalSide::IEnd,   LogicalSide::BStart },  // vertical-lr           ltr
      { LogicalSide::BEnd,   LogicalSide::IStart,
        LogicalSide::BStart, LogicalSide::IEnd   },  // (horizontal-bt)       rtl
      { LogicalSide::IEnd,   LogicalSide::BEnd,
        LogicalSide::IStart, LogicalSide::BStart },  // vertical-lr           rtl
    };
    // clang-format on

    static_assert(StyleWritingMode::VERTICAL._0 == 0x01 &&
                      StyleWritingMode::INLINE_REVERSED._0 == 0x02 &&
                      StyleWritingMode::VERTICAL_LR._0 == 0x04 &&
                      StyleWritingMode::LINE_INVERTED._0 == 0x08,
                  "Unexpected values for StyleWritingMode constants!");
    uint8_t index = mWritingMode._0 & 0x0F;
    return kPhysicalToLogicalSides[index][aSide];
  }

  /**
   * Returns the logical side corresponding to the specified
   * line-relative direction, given the current writing mode.
   */
  LogicalSide LogicalSideForLineRelativeDir(LineRelativeDir aDir) const {
    auto side = static_cast<LogicalSide>(aDir);
    if (IsInline(side)) {
      return IsBidiLTR() ? side : GetOppositeSide(side);
    }
    return !IsLineInverted() ? side : GetOppositeSide(side);
  }

  /**
   * Construct a default WritingMode, equivalent to specifying
   * 'writing-mode: horizontal-tb' and 'direction: ltr' in CSS.
   */
  constexpr WritingMode() : mWritingMode{0} {}

  /**
   * Construct writing mode based on a ComputedStyle.
   */
  explicit WritingMode(const ComputedStyle* aComputedStyle) {
    NS_ASSERTION(aComputedStyle, "we need an ComputedStyle here");
    mWritingMode = aComputedStyle->WritingMode();
  }

  inline StyleWritingMode ToStyleWritingMode() const {
    return StyleWritingMode(GetBits());
  }

  /**
   * This function performs fixup for elements with 'unicode-bidi: plaintext',
   * where inline directionality is derived from the Unicode bidi categories
   * of the element's content, and not the CSS 'direction' property.
   *
   * The WritingMode constructor will have already incorporated the 'direction'
   * property into our flag bits, so such elements need to use this method
   * (after resolving the bidi level of their content) to update the direction
   * bits as needed.
   *
   * If it turns out that our bidi direction already matches what plaintext
   * resolution determined, there's nothing to do here. If it didn't (i.e. if
   * the rtl-ness doesn't match), then we correct the direction by flipping the
   * same bits that get flipped in the constructor's CSS 'direction'-based
   * chunk.
   */
  void SetDirectionFromBidiLevel(mozilla::intl::BidiEmbeddingLevel level) {
    if (level.IsRTL() == IsBidiLTR()) {
      mWritingMode ^= StyleWritingMode::RTL | StyleWritingMode::INLINE_REVERSED;
    }
  }

  /**
   * Compare two WritingModes for equality.
   */
  bool operator==(const WritingMode&) const = default;

  /**
   * Check whether two modes are orthogonal to each other.
   */
  bool IsOrthogonalTo(const WritingMode& aOther) const {
    return IsVertical() != aOther.IsVertical();
  }

  /**
   * Convert aAxis in current writing mode to the axis in aToMode.
   */
  LogicalAxis ConvertAxisTo(LogicalAxis aAxis, WritingMode aToMode) const {
    return IsOrthogonalTo(aToMode) ? GetOrthogonalAxis(aAxis) : aAxis;
  }

  /**
   * Returns true if this WritingMode's aLogicalAxis has the same physical
   * start side as the parallel axis of WritingMode |aOther|.
   *
   * @param aLogicalAxis The axis to compare from this WritingMode.
   * @param aOther The other WritingMode (from which we'll choose the axis
   *               that's parallel to this WritingMode's aLogicalAxis, for
   *               comparison).
   */
  bool ParallelAxisStartsOnSameSide(LogicalAxis aLogicalAxis,
                                    const WritingMode& aOther) const {
    if (MOZ_LIKELY(*this == aOther)) {
      // Dedicated short circuit for the common case.
      return true;
    }

    mozilla::Side myStartSide =
        this->PhysicalSide(MakeLogicalSide(aLogicalAxis, LogicalEdge::Start));

    // Figure out which of aOther's axes is parallel to |this| WritingMode's
    // aLogicalAxis, and get its physical start side as well.
    const LogicalAxis otherWMAxis = ConvertAxisTo(aLogicalAxis, aOther);
    mozilla::Side otherWMStartSide =
        aOther.PhysicalSide(MakeLogicalSide(otherWMAxis, LogicalEdge::Start));

    NS_ASSERTION(myStartSide % 2 == otherWMStartSide % 2,
                 "Should end up with sides in the same physical axis");
    return myStartSide == otherWMStartSide;
  }

  /**
   * Determine a writing mode for determining the line-over/line-under side of a
   * box to use for baseline alignment, based on the container and item's
   * writing modes and the alignment axis.
   *
   * See: https://drafts.csswg.org/css-align-3/#baseline-export
   *
   * @param aContainerWM writing mode of the container (alignment context).
   * @param aItemWM writing mode of the item being aligned.
   * @param aAlignmentAxis axis of alignment, in the container’s writing mode.
   *        LogicalAxis::Inline (rows) for align-{items,self}:[last] baseline.
   *        LogicalAxis::Block (columns) for justify-{items,self}:[last]
   *        baseline.
   */
  static WritingMode DetermineWritingModeForBaselineSynthesis(
      const WritingMode& aContainerWM, const WritingMode& aItemWM,
      LogicalAxis aAlignmentAxis) {
    // Use the physical alignment axis for comparison:
    auto physicalAlignmentAxis = aContainerWM.PhysicalAxis(aAlignmentAxis);

    // If the item's block flow direction is orthogonal to the alignment axis,
    // use its writing mode.
    auto itemAxis = aItemWM.PhysicalAxis(LogicalAxis::Block);
    if (itemAxis != physicalAlignmentAxis) {
      return aItemWM;
    }

    // If the container's block flow direction is orthogonal to the alignment
    // axis, use its writing mode.
    auto containerAxis = aContainerWM.PhysicalAxis(LogicalAxis::Block);
    if (containerAxis != physicalAlignmentAxis) {
      return aContainerWM;
    }

    // If not using the item or container writing mode, synthesize an
    // axis-compatible writing mode based on the container's writing mode.
    //
    // From https://drafts.csswg.org/css-align-3/#baseline-export:
    //
    // - If the box’s own writing mode is vertical, assume `horizontal-tb`.
    // - If the box’s own writing mode is horizontal, assume `vertical-lr` if
    //   direction is `ltr` and `vertical-rl` if direction is `rtl`.

    if (aContainerWM.IsVertical()) {
      return WritingMode{StyleWritingMode::WRITING_MODE_HORIZONTAL_TB._0};
    }

    return (aContainerWM.IsBidiLTR())
               ? WritingMode{StyleWritingMode::WRITING_MODE_VERTICAL_LR._0}
               : WritingMode{StyleWritingMode::WRITING_MODE_VERTICAL_RL._0};
  }

  uint8_t GetBits() const { return mWritingMode._0; }

 private:
  friend class LogicalPoint;
  friend class LogicalSize;
  friend struct LogicalSides;
  friend class LogicalMargin;
  friend class LogicalRect;

  friend struct IPC::ParamTraits<WritingMode>;
  // IMENotification cannot store this class directly since this has some
  // constructors.  Therefore, it stores mWritingMode and recreate the
  // instance from it.
  friend struct widget::IMENotification;

  /**
   * Unknown writing mode (should never actually be stored or used anywhere).
   */
  static constexpr uint8_t kUnknownWritingMode = 0xff;

  /**
   * Return a WritingMode representing an unknown value.
   */
  static inline WritingMode Unknown() {
    return WritingMode(kUnknownWritingMode);
  }

  /**
   * Constructing a WritingMode with an arbitrary value is a private operation.
   * This is currently only used by the Unknown() and IgnoreSideways() methods,
   * and a friend struct IMENotification.
   */
  explicit WritingMode(uint8_t aValue) : mWritingMode{aValue} {}

  StyleWritingMode mWritingMode;
};

inline std::ostream& operator<<(std::ostream& aStream, const WritingMode& aWM) {
  return aStream << (aWM.IsVertical()
                         ? aWM.IsVerticalLR() ? aWM.IsBidiLTR()
                                                    ? aWM.IsSideways()
                                                          ? "sw-lr-ltr"
                                                          : "v-lr-ltr"
                                                : aWM.IsSideways() ? "sw-lr-rtl"
                                                                   : "v-lr-rtl"
                           : aWM.IsBidiLTR()
                               ? aWM.IsSideways() ? "sw-rl-ltr" : "v-rl-ltr"
                           : aWM.IsSideways() ? "sw-rl-rtl"
                                              : "v-rl-rtl"
                     : aWM.IsBidiLTR() ? "h-ltr"
                                       : "h-rtl");
}

/**
 * Logical-coordinate classes:
 *
 * There are three sets of coordinate space:
 *   - physical (top, left, bottom, right)
 *       relative to graphics coord system
 *   - flow-relative (block-start, inline-start, block-end, inline-end)
 *       relative to block/inline flow directions
 *   - line-relative (line-over, line-left, line-under, line-right)
 *       relative to glyph orientation / inline bidi directions
 * See CSS3 Writing Modes for more information
 *   http://www.w3.org/TR/css3-writing-modes/#abstract-box
 *
 * For shorthand, B represents the block-axis
 *                I represents the inline-axis
 *
 * The flow-relative geometric classes store coords in flow-relative space.
 * They use a private ns{Point,Size,Rect,Margin} member to store the actual
 * coordinate values, but reinterpret them as logical instead of physical.
 * This allows us to easily perform calculations in logical space (provided
 * writing modes of the operands match), by simply mapping to nsPoint (etc)
 * methods.
 *
 * Physical-coordinate accessors/setters are responsible to translate these
 * internal logical values as necessary.
 *
 * In DEBUG builds, the logical types store their WritingMode and check
 * that the same WritingMode is passed whenever callers ask them to do a
 * writing-mode-dependent operation. Non-DEBUG builds do NOT check this,
 * to avoid the overhead of storing WritingMode fields.
 *
 * Open question: do we need a different set optimized for line-relative
 * math, for use in nsLineLayout and the like? Or is multiplying values
 * by FlowRelativeToLineRelativeFactor() enough?
 */

/**
 * Flow-relative point
 */
class LogicalPoint {
 public:
  explicit LogicalPoint(WritingMode aWritingMode)
      :
#ifdef DEBUG
        mWritingMode(aWritingMode),
#endif
        mPoint(0, 0) {
  }

  // Construct from a writing mode and individual coordinates (which MUST be
  // values in that writing mode, NOT physical coordinates!)
  LogicalPoint(WritingMode aWritingMode, nscoord aI, nscoord aB)
      :
#ifdef DEBUG
        mWritingMode(aWritingMode),
#endif
        mPoint(aI, aB) {
  }

  // Construct from a writing mode and a physical point, within a given
  // containing rectangle's size (defining the conversion between LTR
  // and RTL coordinates, and between TTB and BTT coordinates).
  LogicalPoint(WritingMode aWritingMode, const nsPoint& aPoint,
               const nsSize& aContainerSize)
#ifdef DEBUG
      : mWritingMode(aWritingMode)
#endif
  {
    if (aWritingMode.IsVertical()) {
      I() = aWritingMode.IsInlineReversed() ? aContainerSize.height - aPoint.y
                                            : aPoint.y;
      B() = aWritingMode.IsVerticalLR() ? aPoint.x
                                        : aContainerSize.width - aPoint.x;
    } else {
      I() = aWritingMode.IsInlineReversed() ? aContainerSize.width - aPoint.x
                                            : aPoint.x;
      B() = aPoint.y;
    }
  }

  /**
   * Read-only (const) access to the logical coordinates.
   */
  nscoord I(WritingMode aWritingMode) const  // inline-axis
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mPoint.x;
  }
  nscoord B(WritingMode aWritingMode) const  // block-axis
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mPoint.y;
  }
  nscoord Pos(LogicalAxis aAxis, WritingMode aWM) const {
    return aAxis == LogicalAxis::Inline ? I(aWM) : B(aWM);
  }
  nscoord LineRelative(WritingMode aWritingMode,
                       const nsSize& aContainerSize) const  // line-axis
  {
    CHECK_WRITING_MODE(aWritingMode);
    if (aWritingMode.IsBidiLTR()) {
      return I();
    }
    return (aWritingMode.IsVertical() ? aContainerSize.height
                                      : aContainerSize.width) -
           I();
  }

  /**
   * These non-const accessors return a reference (lvalue) that can be
   * assigned to by callers.
   */
  nscoord& I(WritingMode aWritingMode)  // inline-axis
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mPoint.x;
  }
  nscoord& B(WritingMode aWritingMode)  // block-axis
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mPoint.y;
  }
  nscoord& Pos(LogicalAxis aAxis, WritingMode aWM) {
    return aAxis == LogicalAxis::Inline ? I(aWM) : B(aWM);
  }

  /**
   * Return a physical point corresponding to our logical coordinates,
   * converted according to our writing mode.
   */
  nsPoint GetPhysicalPoint(WritingMode aWritingMode,
                           const nsSize& aContainerSize) const {
    CHECK_WRITING_MODE(aWritingMode);
    if (aWritingMode.IsVertical()) {
      return nsPoint(
          aWritingMode.IsVerticalLR() ? B() : aContainerSize.width - B(),
          aWritingMode.IsInlineReversed() ? aContainerSize.height - I() : I());
    } else {
      return nsPoint(
          aWritingMode.IsInlineReversed() ? aContainerSize.width - I() : I(),
          B());
    }
  }

  /**
   * Return the equivalent point in a different writing mode.
   */
  LogicalPoint ConvertTo(WritingMode aToMode, WritingMode aFromMode,
                         const nsSize& aContainerSize) const {
    CHECK_WRITING_MODE(aFromMode);
    return aToMode == aFromMode
               ? *this
               : LogicalPoint(aToMode,
                              GetPhysicalPoint(aFromMode, aContainerSize),
                              aContainerSize);
  }

  /**
   * Considering 'this' LogicalPoint as the origin of some rect, as expressed
   * in writing mode aFromMode: this method returns the origin of that same
   * rect, as expressed in writing mode aToMode.
   *
   * The two points ('this' and the return value) may correspond to *different*
   * physical corners of the rect.  Each point is using its own writing-mode
   * to determine which corner is the origin.
   *
   * @param aToMode The writing mode to use for the return value.
   * @param aFromMode The writing mode for 'this' LogicalPoint.
   * @param aRectSize The physical size of the rectangle whose origin
   *                  is being requested.
   * @param aContainerSize The physical size of the container that defines the
   *                       local coordinate space.  (Our points' coordinates are
   *                       relative to the bounds of this container.)
   */
  LogicalPoint ConvertRectOriginTo(WritingMode aToMode, WritingMode aFromMode,
                                   const nsSize& aRectSize,
                                   const nsSize& aContainerSize) const {
    CHECK_WRITING_MODE(aFromMode);
    if (aFromMode == aToMode) {
      return *this;
    }

    // Note: this might *look* like it's abusing ConvertTo(), since the last
    // param to ConvertTo() is named as if it's just a container-size rather
    // than the difference-in-sizes that we're passing here.  But this calling
    // pattern is actually correct for what we're trying to do here.
    //
    // Conceptually, ConvertTo()'s aContainerSize param just represents how
    // much extra space the container has *around* the converted thing in each
    // physical axis. When we're just converting a LogicalPoint with zero
    // thickness -- the way ConvertTo() expects to be called -- the extra space
    // around it in its container is simply the container size. But here, we're
    // converting the origin of a *rect*, and the rect has some
    // potentially-nonzero-thickness; so here, the extra space is the
    // container's size *minus* the rect's size.
    return ConvertTo(aToMode, aFromMode, aContainerSize - aRectSize);
  }

  bool operator==(const LogicalPoint& aOther) const {
    CHECK_WRITING_MODE(aOther.GetWritingMode());
    return mPoint == aOther.mPoint;
  }

  LogicalPoint operator+(const LogicalPoint& aOther) const {
    CHECK_WRITING_MODE(aOther.GetWritingMode());
    // In non-debug builds, LogicalPoint does not store the WritingMode,
    // so the first parameter here (which will always be WritingMode::Unknown())
    // is ignored.
    return LogicalPoint(GetWritingMode(), mPoint.x + aOther.mPoint.x,
                        mPoint.y + aOther.mPoint.y);
  }

  LogicalPoint& operator+=(const LogicalPoint& aOther) {
    CHECK_WRITING_MODE(aOther.GetWritingMode());
    I() += aOther.I();
    B() += aOther.B();
    return *this;
  }

  LogicalPoint operator-(const LogicalPoint& aOther) const {
    CHECK_WRITING_MODE(aOther.GetWritingMode());
    // In non-debug builds, LogicalPoint does not store the WritingMode,
    // so the first parameter here (which will always be WritingMode::Unknown())
    // is ignored.
    return LogicalPoint(GetWritingMode(), mPoint.x - aOther.mPoint.x,
                        mPoint.y - aOther.mPoint.y);
  }

  LogicalPoint& operator-=(const LogicalPoint& aOther) {
    CHECK_WRITING_MODE(aOther.GetWritingMode());
    I() -= aOther.I();
    B() -= aOther.B();
    return *this;
  }

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const LogicalPoint& aPoint) {
    return aStream << aPoint.mPoint;
  }

 private:
  friend class LogicalRect;

  /**
   * NOTE that in non-DEBUG builds, GetWritingMode() always returns
   * WritingMode::Unknown(), as the current mode is not stored in the logical-
   * geometry classes. Therefore, this method is private; it is used ONLY
   * by the DEBUG-mode checking macros in this class and its friends;
   * other code is not allowed to ask a logical point for its writing mode,
   * as this info will simply not be available in non-DEBUG builds.
   *
   * Also, in non-DEBUG builds, CHECK_WRITING_MODE does nothing, and the
   * WritingMode parameter to logical methods will generally be optimized
   * away altogether.
   */
#ifdef DEBUG
  WritingMode GetWritingMode() const { return mWritingMode; }
#else
  WritingMode GetWritingMode() const { return WritingMode::Unknown(); }
#endif

  // We don't allow construction of a LogicalPoint with no writing mode.
  LogicalPoint() = delete;

  // Accessors that don't take or check a WritingMode value.
  // These are for internal use only; they are called by methods that have
  // themselves already checked the WritingMode passed by the caller.
  nscoord I() const  // inline-axis
  {
    return mPoint.x;
  }
  nscoord B() const  // block-axis
  {
    return mPoint.y;
  }

  nscoord& I()  // inline-axis
  {
    return mPoint.x;
  }
  nscoord& B()  // block-axis
  {
    return mPoint.y;
  }

#ifdef DEBUG
  WritingMode mWritingMode;
#endif

  // We use an nsPoint to hold the coordinates, but reinterpret its .x and .y
  // fields as the inline and block directions. Hence, this is not exposed
  // directly, but only through accessors that will map them according to the
  // writing mode.
  nsPoint mPoint;
};

/**
 * Flow-relative size
 */
class LogicalSize {
 public:
  explicit LogicalSize(WritingMode aWritingMode)
      :
#ifdef DEBUG
        mWritingMode(aWritingMode),
#endif
        mSize(0, 0) {
  }

  LogicalSize(WritingMode aWritingMode, nscoord aISize, nscoord aBSize)
      :
#ifdef DEBUG
        mWritingMode(aWritingMode),
#endif
        mSize(aISize, aBSize) {
  }

  LogicalSize(WritingMode aWritingMode, const nsSize& aPhysicalSize)
#ifdef DEBUG
      : mWritingMode(aWritingMode)
#endif
  {
    if (aWritingMode.IsVertical()) {
      ISize() = aPhysicalSize.height;
      BSize() = aPhysicalSize.width;
    } else {
      ISize() = aPhysicalSize.width;
      BSize() = aPhysicalSize.height;
    }
  }

  void SizeTo(WritingMode aWritingMode, nscoord aISize, nscoord aBSize) {
    CHECK_WRITING_MODE(aWritingMode);
    mSize.SizeTo(aISize, aBSize);
  }

  /**
   * Dimensions in logical and physical terms
   */
  nscoord ISize(WritingMode aWritingMode) const  // inline-size
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mSize.width;
  }
  nscoord BSize(WritingMode aWritingMode) const  // block-size
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mSize.height;
  }
  nscoord Size(LogicalAxis aAxis, WritingMode aWM) const {
    return aAxis == LogicalAxis::Inline ? ISize(aWM) : BSize(aWM);
  }

  nscoord Width(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return aWritingMode.IsVertical() ? BSize() : ISize();
  }
  nscoord Height(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return aWritingMode.IsVertical() ? ISize() : BSize();
  }

  /**
   * Writable references to the logical dimensions
   */
  nscoord& ISize(WritingMode aWritingMode)  // inline-size
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mSize.width;
  }
  nscoord& BSize(WritingMode aWritingMode)  // block-size
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mSize.height;
  }
  nscoord& Size(LogicalAxis aAxis, WritingMode aWM) {
    return aAxis == LogicalAxis::Inline ? ISize(aWM) : BSize(aWM);
  }

  /**
   * Return an nsSize containing our physical dimensions
   */
  nsSize GetPhysicalSize(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return aWritingMode.IsVertical() ? nsSize(BSize(), ISize())
                                     : nsSize(ISize(), BSize());
  }

  /**
   * Return a LogicalSize representing this size in a different writing mode
   */
  LogicalSize ConvertTo(WritingMode aToMode, WritingMode aFromMode) const {
#ifdef DEBUG
    // In DEBUG builds make sure to return a LogicalSize with the
    // expected writing mode
    CHECK_WRITING_MODE(aFromMode);
    return aToMode == aFromMode
               ? *this
               : LogicalSize(aToMode, GetPhysicalSize(aFromMode));
#else
    // optimization for non-DEBUG builds where LogicalSize doesn't store
    // the writing mode
    return (aToMode == aFromMode || !aToMode.IsOrthogonalTo(aFromMode))
               ? *this
               : LogicalSize(aToMode, BSize(), ISize());
#endif
  }

  /**
   * Test if a size is (0, 0).
   */
  bool IsAllZero() const { return IsAllValues(0); }
  bool IsAllValues(nscoord aValue) const {
    return ISize() == aValue && BSize() == aValue;
  }

  /**
   * Various binary operators on LogicalSize. These are valid ONLY for operands
   * that share the same writing mode.
   */
  bool operator==(const LogicalSize& aOther) const {
    CHECK_WRITING_MODE(aOther.GetWritingMode());
    return mSize == aOther.mSize;
  }

  LogicalSize operator+(const LogicalSize& aOther) const {
    CHECK_WRITING_MODE(aOther.GetWritingMode());
    return LogicalSize(GetWritingMode(), ISize() + aOther.ISize(),
                       BSize() + aOther.BSize());
  }
  LogicalSize& operator+=(const LogicalSize& aOther) {
    CHECK_WRITING_MODE(aOther.GetWritingMode());
    ISize() += aOther.ISize();
    BSize() += aOther.BSize();
    return *this;
  }

  LogicalSize operator-(const LogicalSize& aOther) const {
    CHECK_WRITING_MODE(aOther.GetWritingMode());
    return LogicalSize(GetWritingMode(), ISize() - aOther.ISize(),
                       BSize() - aOther.BSize());
  }
  LogicalSize& operator-=(const LogicalSize& aOther) {
    CHECK_WRITING_MODE(aOther.GetWritingMode());
    ISize() -= aOther.ISize();
    BSize() -= aOther.BSize();
    return *this;
  }

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const LogicalSize& aSize) {
    return aStream << aSize.mSize;
  }

 private:
  friend class LogicalRect;

  LogicalSize() = delete;

#ifdef DEBUG
  WritingMode GetWritingMode() const { return mWritingMode; }
#else
  WritingMode GetWritingMode() const { return WritingMode::Unknown(); }
#endif

  nscoord ISize() const  // inline-size
  {
    return mSize.width;
  }
  nscoord BSize() const  // block-size
  {
    return mSize.height;
  }

  nscoord& ISize()  // inline-size
  {
    return mSize.width;
  }
  nscoord& BSize()  // block-size
  {
    return mSize.height;
  }

#ifdef DEBUG
  WritingMode mWritingMode;
#endif
  nsSize mSize;
};

/**
 * LogicalSides represents a set of logical sides.
 */
struct LogicalSides final {
  static constexpr EnumSet<LogicalSide> BBoth{LogicalSide::BStart,
                                              LogicalSide::BEnd};
  static constexpr EnumSet<LogicalSide> IBoth{LogicalSide::IStart,
                                              LogicalSide::IEnd};
  static constexpr EnumSet<LogicalSide> All{
      LogicalSide::BStart, LogicalSide::BEnd, LogicalSide::IStart,
      LogicalSide::IEnd};

  explicit LogicalSides(WritingMode aWritingMode)
#ifdef DEBUG
      : mWritingMode(aWritingMode)
#endif
  {
  }
  LogicalSides(WritingMode aWritingMode, LogicalSides aSides)
      :
#ifdef DEBUG
        mWritingMode(aWritingMode),
#endif
        mSides(aSides.mSides) {
  }
  LogicalSides(WritingMode aWritingMode, EnumSet<LogicalSide> aSides)
      :
#ifdef DEBUG
        mWritingMode(aWritingMode),
#endif
        mSides(aSides) {
  }
  bool IsEmpty() const { return mSides.isEmpty(); }
  bool BStart() const { return mSides.contains(LogicalSide::BStart); }
  bool BEnd() const { return mSides.contains(LogicalSide::BEnd); }
  bool IStart() const { return mSides.contains(LogicalSide::IStart); }
  bool IEnd() const { return mSides.contains(LogicalSide::IEnd); }
  bool Contains(LogicalSide aSide) const { return mSides.contains(aSide); }
  LogicalSides& operator+=(LogicalSides aOther) {
    mSides += aOther.mSides;
    return *this;
  }
  LogicalSides& operator+=(LogicalSide aOther) {
    mSides += aOther;
    return *this;
  }
  bool operator==(const LogicalSides& aOther) const {
    CHECK_WRITING_MODE(aOther.GetWritingMode());
    return mSides == aOther.mSides;
  }

#ifdef DEBUG
  WritingMode GetWritingMode() const { return mWritingMode; }
#else
  WritingMode GetWritingMode() const { return WritingMode::Unknown(); }
#endif

 private:
#ifdef DEBUG
  WritingMode mWritingMode;
#endif
  EnumSet<LogicalSide> mSides;
};

/**
 * Flow-relative margin
 */
class LogicalMargin {
 public:
  explicit LogicalMargin(WritingMode aWritingMode)
      :
#ifdef DEBUG
        mWritingMode(aWritingMode),
#endif
        mMargin(0, 0, 0, 0) {
  }

  LogicalMargin(WritingMode aWritingMode, nscoord aBStart, nscoord aIEnd,
                nscoord aBEnd, nscoord aIStart)
      :
#ifdef DEBUG
        mWritingMode(aWritingMode),
#endif
        mMargin(aBStart, aIEnd, aBEnd, aIStart) {
  }

  LogicalMargin(WritingMode aWritingMode, const nsMargin& aPhysicalMargin)
#ifdef DEBUG
      : mWritingMode(aWritingMode)
#endif
  {
    if (aWritingMode.IsVertical()) {
      if (aWritingMode.IsVerticalLR()) {
        mMargin.top = aPhysicalMargin.left;
        mMargin.bottom = aPhysicalMargin.right;
      } else {
        mMargin.top = aPhysicalMargin.right;
        mMargin.bottom = aPhysicalMargin.left;
      }
      if (aWritingMode.IsInlineReversed()) {
        mMargin.left = aPhysicalMargin.bottom;
        mMargin.right = aPhysicalMargin.top;
      } else {
        mMargin.left = aPhysicalMargin.top;
        mMargin.right = aPhysicalMargin.bottom;
      }
    } else {
      mMargin.top = aPhysicalMargin.top;
      mMargin.bottom = aPhysicalMargin.bottom;
      if (aWritingMode.IsInlineReversed()) {
        mMargin.left = aPhysicalMargin.right;
        mMargin.right = aPhysicalMargin.left;
      } else {
        mMargin.left = aPhysicalMargin.left;
        mMargin.right = aPhysicalMargin.right;
      }
    }
  }

  nscoord IStart(WritingMode aWritingMode) const  // inline-start margin
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mMargin.left;
  }
  nscoord IEnd(WritingMode aWritingMode) const  // inline-end margin
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mMargin.right;
  }
  nscoord BStart(WritingMode aWritingMode) const  // block-start margin
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mMargin.top;
  }
  nscoord BEnd(WritingMode aWritingMode) const  // block-end margin
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mMargin.bottom;
  }
  nscoord Start(LogicalAxis aAxis, WritingMode aWM) const {
    return aAxis == LogicalAxis::Inline ? IStart(aWM) : BStart(aWM);
  }
  nscoord End(LogicalAxis aAxis, WritingMode aWM) const {
    return aAxis == LogicalAxis::Inline ? IEnd(aWM) : BEnd(aWM);
  }

  nscoord& IStart(WritingMode aWritingMode)  // inline-start margin
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mMargin.left;
  }
  nscoord& IEnd(WritingMode aWritingMode)  // inline-end margin
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mMargin.right;
  }
  nscoord& BStart(WritingMode aWritingMode)  // block-start margin
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mMargin.top;
  }
  nscoord& BEnd(WritingMode aWritingMode)  // block-end margin
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mMargin.bottom;
  }
  nscoord& Start(LogicalAxis aAxis, WritingMode aWM) {
    return aAxis == LogicalAxis::Inline ? IStart(aWM) : BStart(aWM);
  }
  nscoord& End(LogicalAxis aAxis, WritingMode aWM) {
    return aAxis == LogicalAxis::Inline ? IEnd(aWM) : BEnd(aWM);
  }

  nscoord IStartEnd(WritingMode aWritingMode) const  // inline margins
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mMargin.LeftRight();
  }
  nscoord BStartEnd(WritingMode aWritingMode) const  // block margins
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mMargin.TopBottom();
  }
  nscoord StartEnd(LogicalAxis aAxis, WritingMode aWM) const {
    return aAxis == LogicalAxis::Inline ? IStartEnd(aWM) : BStartEnd(aWM);
  }

  nscoord Side(LogicalSide aSide, WritingMode aWM) const {
    switch (aSide) {
      case LogicalSide::BStart:
        return BStart(aWM);
      case LogicalSide::BEnd:
        return BEnd(aWM);
      case LogicalSide::IStart:
        return IStart(aWM);
      case LogicalSide::IEnd:
        return IEnd(aWM);
    }

    MOZ_ASSERT_UNREACHABLE("We should handle all sides!");
    return BStart(aWM);
  }
  nscoord& Side(LogicalSide aSide, WritingMode aWM) {
    switch (aSide) {
      case LogicalSide::BStart:
        return BStart(aWM);
      case LogicalSide::BEnd:
        return BEnd(aWM);
      case LogicalSide::IStart:
        return IStart(aWM);
      case LogicalSide::IEnd:
        return IEnd(aWM);
    }

    MOZ_ASSERT_UNREACHABLE("We should handle all sides!");
    return BStart(aWM);
  }

  /*
   * Return margin values for line-relative sides, as defined in
   * http://www.w3.org/TR/css-writing-modes-3/#line-directions:
   *
   * line-left
   *     Nominally the side from which LTR text would start.
   * line-right
   *     Nominally the side from which RTL text would start. (Opposite of
   *     line-left.)
   */
  nscoord LineLeft(WritingMode aWritingMode) const {
    // We don't need to CHECK_WRITING_MODE here because the IStart or IEnd
    // accessor that we call will do it.
    return aWritingMode.IsBidiLTR() ? IStart(aWritingMode) : IEnd(aWritingMode);
  }
  nscoord LineRight(WritingMode aWritingMode) const {
    return aWritingMode.IsBidiLTR() ? IEnd(aWritingMode) : IStart(aWritingMode);
  }

  /**
   * Return a LogicalSize representing the total size of the inline-
   * and block-dimension margins.
   */
  LogicalSize Size(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return LogicalSize(aWritingMode, IStartEnd(), BStartEnd());
  }

  /**
   * Return a LogicalPoint representing an offset to the start-sides, i.e.
   * inline-start and block-start.
   */
  LogicalPoint StartOffset(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return LogicalPoint(aWritingMode, IStart(), BStart());
  }

  /**
   * Accessors for physical margins, using our writing mode to convert from
   * logical values.
   */
  nscoord Top(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return aWritingMode.IsVertical()
               ? (aWritingMode.IsInlineReversed() ? IEnd() : IStart())
               : BStart();
  }

  nscoord Bottom(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return aWritingMode.IsVertical()
               ? (aWritingMode.IsInlineReversed() ? IStart() : IEnd())
               : BEnd();
  }

  nscoord Left(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return aWritingMode.IsVertical()
               ? (aWritingMode.IsVerticalLR() ? BStart() : BEnd())
               : (aWritingMode.IsInlineReversed() ? IEnd() : IStart());
  }

  nscoord Right(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return aWritingMode.IsVertical()
               ? (aWritingMode.IsVerticalLR() ? BEnd() : BStart())
               : (aWritingMode.IsInlineReversed() ? IStart() : IEnd());
  }

  nscoord LeftRight(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return aWritingMode.IsVertical() ? BStartEnd() : IStartEnd();
  }

  nscoord TopBottom(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return aWritingMode.IsVertical() ? IStartEnd() : BStartEnd();
  }

  void SizeTo(WritingMode aWritingMode, nscoord aBStart, nscoord aIEnd,
              nscoord aBEnd, nscoord aIStart) {
    CHECK_WRITING_MODE(aWritingMode);
    mMargin.SizeTo(aBStart, aIEnd, aBEnd, aIStart);
  }

  /**
   * Return an nsMargin containing our physical coordinates
   */
  nsMargin GetPhysicalMargin(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return aWritingMode.IsVertical()
               ? (aWritingMode.IsVerticalLR()
                      ? (aWritingMode.IsInlineReversed()
                             ? nsMargin(IEnd(), BEnd(), IStart(), BStart())
                             : nsMargin(IStart(), BEnd(), IEnd(), BStart()))
                      : (aWritingMode.IsInlineReversed()
                             ? nsMargin(IEnd(), BStart(), IStart(), BEnd())
                             : nsMargin(IStart(), BStart(), IEnd(), BEnd())))
               : (aWritingMode.IsInlineReversed()
                      ? nsMargin(BStart(), IStart(), BEnd(), IEnd())
                      : nsMargin(BStart(), IEnd(), BEnd(), IStart()));
  }

  /**
   * Return a LogicalMargin representing this margin in a different
   * writing mode
   */
  LogicalMargin ConvertTo(WritingMode aToMode, WritingMode aFromMode) const {
    CHECK_WRITING_MODE(aFromMode);
    return aToMode == aFromMode
               ? *this
               : LogicalMargin(aToMode, GetPhysicalMargin(aFromMode));
  }

  LogicalMargin& ApplySkipSides(LogicalSides aSkipSides) {
    CHECK_WRITING_MODE(aSkipSides.GetWritingMode());
    if (aSkipSides.BStart()) {
      BStart() = 0;
    }
    if (aSkipSides.BEnd()) {
      BEnd() = 0;
    }
    if (aSkipSides.IStart()) {
      IStart() = 0;
    }
    if (aSkipSides.IEnd()) {
      IEnd() = 0;
    }
    return *this;
  }

  bool IsAllZero() const { return mMargin.IsAllZero(); }

  bool operator==(const LogicalMargin& aMargin) const {
    CHECK_WRITING_MODE(aMargin.GetWritingMode());
    return mMargin == aMargin.mMargin;
  }

  LogicalMargin operator+(const LogicalMargin& aMargin) const {
    CHECK_WRITING_MODE(aMargin.GetWritingMode());
    return LogicalMargin(GetWritingMode(), BStart() + aMargin.BStart(),
                         IEnd() + aMargin.IEnd(), BEnd() + aMargin.BEnd(),
                         IStart() + aMargin.IStart());
  }

  LogicalMargin operator+=(const LogicalMargin& aMargin) {
    CHECK_WRITING_MODE(aMargin.GetWritingMode());
    mMargin += aMargin.mMargin;
    return *this;
  }

  LogicalMargin operator-(const LogicalMargin& aMargin) const {
    CHECK_WRITING_MODE(aMargin.GetWritingMode());
    return LogicalMargin(GetWritingMode(), BStart() - aMargin.BStart(),
                         IEnd() - aMargin.IEnd(), BEnd() - aMargin.BEnd(),
                         IStart() - aMargin.IStart());
  }

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const LogicalMargin& aMargin) {
    return aStream << aMargin.mMargin;
  }

 private:
  friend class LogicalRect;

  LogicalMargin() = delete;

#ifdef DEBUG
  WritingMode GetWritingMode() const { return mWritingMode; }
#else
  WritingMode GetWritingMode() const { return WritingMode::Unknown(); }
#endif

  nscoord IStart() const  // inline-start margin
  {
    return mMargin.left;
  }
  nscoord IEnd() const  // inline-end margin
  {
    return mMargin.right;
  }
  nscoord BStart() const  // block-start margin
  {
    return mMargin.top;
  }
  nscoord BEnd() const  // block-end margin
  {
    return mMargin.bottom;
  }

  nscoord& IStart()  // inline-start margin
  {
    return mMargin.left;
  }
  nscoord& IEnd()  // inline-end margin
  {
    return mMargin.right;
  }
  nscoord& BStart()  // block-start margin
  {
    return mMargin.top;
  }
  nscoord& BEnd()  // block-end margin
  {
    return mMargin.bottom;
  }

  nscoord IStartEnd() const  // inline margins
  {
    return mMargin.LeftRight();
  }
  nscoord BStartEnd() const  // block margins
  {
    return mMargin.TopBottom();
  }

#ifdef DEBUG
  WritingMode mWritingMode;
#endif
  nsMargin mMargin;
};

/**
 * Flow-relative rectangle
 */
class LogicalRect {
 public:
  explicit LogicalRect(WritingMode aWritingMode)
      :
#ifdef DEBUG
        mWritingMode(aWritingMode),
#endif
        mIStart(0),
        mBStart(0),
        mISize(0),
        mBSize(0) {
  }

  LogicalRect(WritingMode aWritingMode, nscoord aIStart, nscoord aBStart,
              nscoord aISize, nscoord aBSize)
      :
#ifdef DEBUG
        mWritingMode(aWritingMode),
#endif
        mIStart(aIStart),
        mBStart(aBStart),
        mISize(aISize),
        mBSize(aBSize) {
  }

  LogicalRect(WritingMode aWritingMode, const LogicalPoint& aOrigin,
              const LogicalSize& aSize)
      :
#ifdef DEBUG
        mWritingMode(aWritingMode),
#endif
        mIStart(aOrigin.mPoint.x),
        mBStart(aOrigin.mPoint.y),
        mISize(aSize.mSize.width),
        mBSize(aSize.mSize.height) {
    CHECK_WRITING_MODE(aOrigin.GetWritingMode());
    CHECK_WRITING_MODE(aSize.GetWritingMode());
  }

  LogicalRect(WritingMode aWritingMode, const nsRect& aRect,
              const nsSize& aContainerSize)
#ifdef DEBUG
      : mWritingMode(aWritingMode)
#endif
  {
    if (aWritingMode.IsVertical()) {
      mBStart = aWritingMode.IsVerticalLR()
                    ? aRect.X()
                    : aContainerSize.width - aRect.XMost();
      mIStart = aWritingMode.IsInlineReversed()
                    ? aContainerSize.height - aRect.YMost()
                    : aRect.Y();
      mBSize = aRect.Width();
      mISize = aRect.Height();
    } else {
      mIStart = aWritingMode.IsInlineReversed()
                    ? aContainerSize.width - aRect.XMost()
                    : aRect.X();
      mBStart = aRect.Y();
      mISize = aRect.Width();
      mBSize = aRect.Height();
    }
  }

  /**
   * Inline- and block-dimension geometry.
   */
  nscoord IStart(WritingMode aWritingMode) const  // inline-start edge
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mIStart;
  }
  nscoord IEnd(WritingMode aWritingMode) const  // inline-end edge
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mIStart + mISize;
  }
  nscoord ISize(WritingMode aWritingMode) const  // inline-size
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mISize;
  }

  nscoord BStart(WritingMode aWritingMode) const  // block-start edge
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mBStart;
  }
  nscoord BEnd(WritingMode aWritingMode) const  // block-end edge
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mBStart + mBSize;
  }
  nscoord BSize(WritingMode aWritingMode) const  // block-size
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mBSize;
  }

  nscoord Start(LogicalAxis aAxis, WritingMode aWM) const {
    return aAxis == LogicalAxis::Inline ? IStart(aWM) : BStart(aWM);
  }
  nscoord End(LogicalAxis aAxis, WritingMode aWM) const {
    return aAxis == LogicalAxis::Inline ? IEnd(aWM) : BEnd(aWM);
  }
  nscoord Size(LogicalAxis aAxis, WritingMode aWM) const {
    return aAxis == LogicalAxis::Inline ? ISize(aWM) : BSize(aWM);
  }

  /**
   * Writable (reference) accessors are only available for the basic logical
   * fields (Start and Size), not derivatives like End.
   */
  nscoord& IStart(WritingMode aWritingMode)  // inline-start edge
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mIStart;
  }
  nscoord& ISize(WritingMode aWritingMode)  // inline-size
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mISize;
  }
  nscoord& BStart(WritingMode aWritingMode)  // block-start edge
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mBStart;
  }
  nscoord& BSize(WritingMode aWritingMode)  // block-size
  {
    CHECK_WRITING_MODE(aWritingMode);
    return mBSize;
  }
  nscoord& Start(LogicalAxis aAxis, WritingMode aWM) {
    return aAxis == LogicalAxis::Inline ? IStart(aWM) : BStart(aWM);
  }
  nscoord& Size(LogicalAxis aAxis, WritingMode aWM) {
    return aAxis == LogicalAxis::Inline ? ISize(aWM) : BSize(aWM);
  }

  /**
   * Accessors for line-relative coordinates
   */
  nscoord LineLeft(WritingMode aWritingMode,
                   const nsSize& aContainerSize) const {
    CHECK_WRITING_MODE(aWritingMode);
    if (aWritingMode.IsBidiLTR()) {
      return IStart();
    }
    nscoord containerISize = aWritingMode.IsVertical() ? aContainerSize.height
                                                       : aContainerSize.width;
    return containerISize - IEnd();
  }
  nscoord LineRight(WritingMode aWritingMode,
                    const nsSize& aContainerSize) const {
    CHECK_WRITING_MODE(aWritingMode);
    if (aWritingMode.IsBidiLTR()) {
      return IEnd();
    }
    nscoord containerISize = aWritingMode.IsVertical() ? aContainerSize.height
                                                       : aContainerSize.width;
    return containerISize - IStart();
  }

  /**
   * Physical coordinates of the rect.
   */
  nscoord X(WritingMode aWritingMode, nscoord aContainerWidth) const {
    CHECK_WRITING_MODE(aWritingMode);
    if (aWritingMode.IsVertical()) {
      return aWritingMode.IsVerticalLR() ? mBStart : aContainerWidth - BEnd();
    }
    return aWritingMode.IsInlineReversed() ? aContainerWidth - IEnd() : mIStart;
  }

  nscoord Y(WritingMode aWritingMode, nscoord aContainerHeight) const {
    CHECK_WRITING_MODE(aWritingMode);
    if (aWritingMode.IsVertical()) {
      return aWritingMode.IsInlineReversed() ? aContainerHeight - IEnd()
                                             : mIStart;
    }
    return mBStart;
  }

  nscoord Width(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return aWritingMode.IsVertical() ? mBSize : mISize;
  }

  nscoord Height(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return aWritingMode.IsVertical() ? mISize : mBSize;
  }

  nscoord XMost(WritingMode aWritingMode, nscoord aContainerWidth) const {
    CHECK_WRITING_MODE(aWritingMode);
    if (aWritingMode.IsVertical()) {
      return aWritingMode.IsVerticalLR() ? BEnd() : aContainerWidth - mBStart;
    }
    return aWritingMode.IsInlineReversed() ? aContainerWidth - mIStart : IEnd();
  }

  nscoord YMost(WritingMode aWritingMode, nscoord aContainerHeight) const {
    CHECK_WRITING_MODE(aWritingMode);
    if (aWritingMode.IsVertical()) {
      return aWritingMode.IsInlineReversed() ? aContainerHeight - mIStart
                                             : IEnd();
    }
    return BEnd();
  }

  bool IsEmpty() const { return mISize <= 0 || mBSize <= 0; }

  bool IsAllZero() const {
    return (mIStart == 0 && mBStart == 0 && mISize == 0 && mBSize == 0);
  }

  bool IsZeroSize() const { return (mISize == 0 && mBSize == 0); }

  void SetEmpty() { mISize = mBSize = 0; }

  bool IsEqualEdges(const LogicalRect aOther) const {
    CHECK_WRITING_MODE(aOther.GetWritingMode());
    bool result = mIStart == aOther.mIStart && mBStart == aOther.mBStart &&
                  mISize == aOther.mISize && mBSize == aOther.mBSize;

    // We want the same result as nsRect, so assert we get it.
    MOZ_ASSERT(result ==
               nsRect(mIStart, mBStart, mISize, mBSize)
                   .IsEqualEdges(nsRect(aOther.mIStart, aOther.mBStart,
                                        aOther.mISize, aOther.mBSize)));
    return result;
  }

  LogicalPoint Origin(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return LogicalPoint(aWritingMode, IStart(), BStart());
  }
  void SetOrigin(WritingMode aWritingMode, const LogicalPoint& aPoint) {
    IStart(aWritingMode) = aPoint.I(aWritingMode);
    BStart(aWritingMode) = aPoint.B(aWritingMode);
  }

  LogicalSize Size(WritingMode aWritingMode) const {
    CHECK_WRITING_MODE(aWritingMode);
    return LogicalSize(aWritingMode, ISize(), BSize());
  }

  LogicalRect operator+(const LogicalPoint& aPoint) const {
    CHECK_WRITING_MODE(aPoint.GetWritingMode());
    return LogicalRect(GetWritingMode(), IStart() + aPoint.I(),
                       BStart() + aPoint.B(), ISize(), BSize());
  }

  LogicalRect& operator+=(const LogicalPoint& aPoint) {
    CHECK_WRITING_MODE(aPoint.GetWritingMode());
    mIStart += aPoint.mPoint.x;
    mBStart += aPoint.mPoint.y;
    return *this;
  }

  LogicalRect operator-(const LogicalPoint& aPoint) const {
    CHECK_WRITING_MODE(aPoint.GetWritingMode());
    return LogicalRect(GetWritingMode(), IStart() - aPoint.I(),
                       BStart() - aPoint.B(), ISize(), BSize());
  }

  LogicalRect& operator-=(const LogicalPoint& aPoint) {
    CHECK_WRITING_MODE(aPoint.GetWritingMode());
    mIStart -= aPoint.mPoint.x;
    mBStart -= aPoint.mPoint.y;
    return *this;
  }

  void MoveBy(WritingMode aWritingMode, const LogicalPoint& aDelta) {
    CHECK_WRITING_MODE(aWritingMode);
    CHECK_WRITING_MODE(aDelta.GetWritingMode());
    IStart() += aDelta.I();
    BStart() += aDelta.B();
  }

  void Inflate(nscoord aD) {
#ifdef DEBUG
    // Compute using nsRect and assert the results match
    nsRect rectDebug(mIStart, mBStart, mISize, mBSize);
    rectDebug.Inflate(aD);
#endif
    mIStart -= aD;
    mBStart -= aD;
    mISize += 2 * aD;
    mBSize += 2 * aD;
    MOZ_ASSERT(
        rectDebug.IsEqualEdges(nsRect(mIStart, mBStart, mISize, mBSize)));
  }
  void Inflate(nscoord aDI, nscoord aDB) {
#ifdef DEBUG
    // Compute using nsRect and assert the results match
    nsRect rectDebug(mIStart, mBStart, mISize, mBSize);
    rectDebug.Inflate(aDI, aDB);
#endif
    mIStart -= aDI;
    mBStart -= aDB;
    mISize += 2 * aDI;
    mBSize += 2 * aDB;
    MOZ_ASSERT(
        rectDebug.IsEqualEdges(nsRect(mIStart, mBStart, mISize, mBSize)));
  }
  void Inflate(WritingMode aWritingMode, const LogicalMargin& aMargin) {
    CHECK_WRITING_MODE(aWritingMode);
    CHECK_WRITING_MODE(aMargin.GetWritingMode());
#ifdef DEBUG
    // Compute using nsRect and assert the results match
    nsRect rectDebug(mIStart, mBStart, mISize, mBSize);
    rectDebug.Inflate(aMargin.mMargin);
#endif
    mIStart -= aMargin.mMargin.left;
    mBStart -= aMargin.mMargin.top;
    mISize += aMargin.mMargin.LeftRight();
    mBSize += aMargin.mMargin.TopBottom();
    MOZ_ASSERT(
        rectDebug.IsEqualEdges(nsRect(mIStart, mBStart, mISize, mBSize)));
  }

  void Deflate(nscoord aD) {
#ifdef DEBUG
    // Compute using nsRect and assert the results match
    nsRect rectDebug(mIStart, mBStart, mISize, mBSize);
    rectDebug.Deflate(aD);
#endif
    mIStart += aD;
    mBStart += aD;
    mISize = std::max(0, mISize - 2 * aD);
    mBSize = std::max(0, mBSize - 2 * aD);
    MOZ_ASSERT(
        rectDebug.IsEqualEdges(nsRect(mIStart, mBStart, mISize, mBSize)));
  }
  void Deflate(nscoord aDI, nscoord aDB) {
#ifdef DEBUG
    // Compute using nsRect and assert the results match
    nsRect rectDebug(mIStart, mBStart, mISize, mBSize);
    rectDebug.Deflate(aDI, aDB);
#endif
    mIStart += aDI;
    mBStart += aDB;
    mISize = std::max(0, mISize - 2 * aDI);
    mBSize = std::max(0, mBSize - 2 * aDB);
    MOZ_ASSERT(
        rectDebug.IsEqualEdges(nsRect(mIStart, mBStart, mISize, mBSize)));
  }
  void Deflate(WritingMode aWritingMode, const LogicalMargin& aMargin) {
    CHECK_WRITING_MODE(aWritingMode);
    CHECK_WRITING_MODE(aMargin.GetWritingMode());
#ifdef DEBUG
    // Compute using nsRect and assert the results match
    nsRect rectDebug(mIStart, mBStart, mISize, mBSize);
    rectDebug.Deflate(aMargin.mMargin);
#endif
    mIStart += aMargin.mMargin.left;
    mBStart += aMargin.mMargin.top;
    mISize = std::max(0, mISize - aMargin.mMargin.LeftRight());
    mBSize = std::max(0, mBSize - aMargin.mMargin.TopBottom());
    MOZ_ASSERT(
        rectDebug.IsEqualEdges(nsRect(mIStart, mBStart, mISize, mBSize)));
  }

  /**
   * Return an nsRect containing our physical coordinates within the given
   * container size.
   */
  nsRect GetPhysicalRect(WritingMode aWritingMode,
                         const nsSize& aContainerSize) const {
    CHECK_WRITING_MODE(aWritingMode);
    if (aWritingMode.IsVertical()) {
      return nsRect(aWritingMode.IsVerticalLR() ? BStart()
                                                : aContainerSize.width - BEnd(),
                    aWritingMode.IsInlineReversed()
                        ? aContainerSize.height - IEnd()
                        : IStart(),
                    BSize(), ISize());
    } else {
      return nsRect(aWritingMode.IsInlineReversed()
                        ? aContainerSize.width - IEnd()
                        : IStart(),
                    BStart(), ISize(), BSize());
    }
  }

  /**
   * Return a LogicalRect representing this rect in a different writing mode
   */
  LogicalRect ConvertTo(WritingMode aToMode, WritingMode aFromMode,
                        const nsSize& aContainerSize) const {
    CHECK_WRITING_MODE(aFromMode);
    return aToMode == aFromMode
               ? *this
               : LogicalRect(aToMode,
                             GetPhysicalRect(aFromMode, aContainerSize),
                             aContainerSize);
  }

  /**
   * Set *this to be the rectangle containing the intersection of aRect1
   * and aRect2, return whether the intersection is non-empty.
   */
  bool IntersectRect(const LogicalRect& aRect1, const LogicalRect& aRect2) {
    CHECK_WRITING_MODE(aRect1.mWritingMode);
    CHECK_WRITING_MODE(aRect2.mWritingMode);
#ifdef DEBUG
    // Compute using nsRect and assert the results match
    nsRect rectDebug;
    rectDebug.IntersectRect(
        nsRect(aRect1.mIStart, aRect1.mBStart, aRect1.mISize, aRect1.mBSize),
        nsRect(aRect2.mIStart, aRect2.mBStart, aRect2.mISize, aRect2.mBSize));
#endif

    nscoord iEnd = std::min(aRect1.IEnd(), aRect2.IEnd());
    mIStart = std::max(aRect1.mIStart, aRect2.mIStart);
    mISize = iEnd - mIStart;

    nscoord bEnd = std::min(aRect1.BEnd(), aRect2.BEnd());
    mBStart = std::max(aRect1.mBStart, aRect2.mBStart);
    mBSize = bEnd - mBStart;

    if (mISize < 0 || mBSize < 0) {
      mISize = 0;
      mBSize = 0;
    }

    MOZ_ASSERT(
        (rectDebug.IsEmpty() && (mISize == 0 || mBSize == 0)) ||
        rectDebug.IsEqualEdges(nsRect(mIStart, mBStart, mISize, mBSize)));
    return mISize > 0 && mBSize > 0;
  }

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const LogicalRect& aRect) {
    return aStream << '(' << aRect.IStart() << ',' << aRect.BStart() << ','
                   << aRect.ISize() << ',' << aRect.BSize() << ')';
  }

 private:
  LogicalRect() = delete;

#ifdef DEBUG
  WritingMode GetWritingMode() const { return mWritingMode; }
#else
  WritingMode GetWritingMode() const { return WritingMode::Unknown(); }
#endif

  nscoord IStart() const  // inline-start edge
  {
    return mIStart;
  }
  nscoord IEnd() const  // inline-end edge
  {
    return mIStart + mISize;
  }
  nscoord ISize() const  // inline-size
  {
    return mISize;
  }

  nscoord BStart() const  // block-start edge
  {
    return mBStart;
  }
  nscoord BEnd() const  // block-end edge
  {
    return mBStart + mBSize;
  }
  nscoord BSize() const  // block-size
  {
    return mBSize;
  }

  nscoord& IStart()  // inline-start edge
  {
    return mIStart;
  }
  nscoord& ISize()  // inline-size
  {
    return mISize;
  }
  nscoord& BStart()  // block-start edge
  {
    return mBStart;
  }
  nscoord& BSize()  // block-size
  {
    return mBSize;
  }

#ifdef DEBUG
  WritingMode mWritingMode;
#endif
  // Inline- and block-geometry dimension
  nscoord mIStart;  // inline-start edge
  nscoord mBStart;  // block-start edge
  nscoord mISize;   // inline-size
  nscoord mBSize;   // block-size
};

template <typename T>
const T& StyleRect<T>::Get(LogicalSide aSide, WritingMode aWM) const {
  return Get(aWM.PhysicalSide(aSide));
}

template <typename T>
const T& StyleRect<T>::GetIStart(WritingMode aWM) const {
  return Get(LogicalSide::IStart, aWM);
}

template <typename T>
const T& StyleRect<T>::GetBStart(WritingMode aWM) const {
  return Get(LogicalSide::BStart, aWM);
}

template <typename T>
const T& StyleRect<T>::GetIEnd(WritingMode aWM) const {
  return Get(LogicalSide::IEnd, aWM);
}

template <typename T>
const T& StyleRect<T>::GetBEnd(WritingMode aWM) const {
  return Get(LogicalSide::BEnd, aWM);
}

template <typename T>
T& StyleRect<T>::Get(LogicalSide aSide, WritingMode aWM) {
  return Get(aWM.PhysicalSide(aSide));
}

template <typename T>
T& StyleRect<T>::GetIStart(WritingMode aWM) {
  return Get(LogicalSide::IStart, aWM);
}

template <typename T>
T& StyleRect<T>::GetBStart(WritingMode aWM) {
  return Get(LogicalSide::BStart, aWM);
}

template <typename T>
T& StyleRect<T>::GetIEnd(WritingMode aWM) {
  return Get(LogicalSide::IEnd, aWM);
}

template <typename T>
T& StyleRect<T>::GetBEnd(WritingMode aWM) {
  return Get(LogicalSide::BEnd, aWM);
}

template <typename T>
const T& StyleRect<T>::Start(mozilla::LogicalAxis aAxis,
                             mozilla::WritingMode aWM) const {
  return aAxis == LogicalAxis::Inline ? GetIStart(aWM) : GetBStart(aWM);
}

template <typename T>
const T& StyleRect<T>::End(mozilla::LogicalAxis aAxis,
                           mozilla::WritingMode aWM) const {
  return aAxis == LogicalAxis::Inline ? GetIEnd(aWM) : GetBEnd(aWM);
}

inline AspectRatio AspectRatio::ConvertToWritingMode(
    const WritingMode& aWM) const {
  return aWM.IsVertical() ? Inverted() : *this;
}

template <>
inline bool StyleSize::BehavesLikeInitialValue(LogicalAxis aAxis) const {
  return aAxis == LogicalAxis::Inline ? IsAuto()
                                      : BehavesLikeInitialValueOnBlockAxis();
}

template <>
inline bool StyleMaxSize::BehavesLikeInitialValue(LogicalAxis aAxis) const {
  return aAxis == LogicalAxis::Inline ? IsNone()
                                      : BehavesLikeInitialValueOnBlockAxis();
}

}  // namespace mozilla

// Definitions of inline methods for nsStylePosition, declared in
// nsStyleStruct.h but not defined there because they need WritingMode.
inline AnchorResolvedInset nsStylePosition::GetAnchorResolvedInset(
    mozilla::LogicalSide aSide, WritingMode aWM,
    const AnchorPosOffsetResolutionParams& aParams) const {
  return GetAnchorResolvedInset(aWM.PhysicalSide(aSide), aParams);
}

inline mozilla::Maybe<mozilla::Side> nsStylePosition::GetSingleAutoInsetInAxis(
    LogicalAxis aAxis, WritingMode aWM,
    const AnchorPosOffsetResolutionParams& aParams) const {
  const bool isInlineAxis = (aAxis == LogicalAxis::Inline);
  const mozilla::StylePhysicalAxis physicalAxis =
      (isInlineAxis == aWM.IsVertical())
          ? mozilla::StylePhysicalAxis::Vertical
          : mozilla::StylePhysicalAxis::Horizontal;
  return GetSingleAutoInsetInAxis(physicalAxis, aParams);
}

inline AnchorResolvedSize nsStylePosition::ISize(
    WritingMode aWM, const AnchorPosResolutionParams& aParams) const {
  return aWM.IsVertical() ? GetHeight(aParams) : GetWidth(aParams);
}

inline AnchorResolvedSize nsStylePosition::MinISize(
    WritingMode aWM, const AnchorPosResolutionParams& aParams) const {
  return aWM.IsVertical() ? GetMinHeight(aParams) : GetMinWidth(aParams);
}

inline AnchorResolvedMaxSize nsStylePosition::MaxISize(
    WritingMode aWM, const AnchorPosResolutionParams& aParams) const {
  return aWM.IsVertical() ? GetMaxHeight(aParams) : GetMaxWidth(aParams);
}

inline AnchorResolvedSize nsStylePosition::BSize(
    WritingMode aWM, const AnchorPosResolutionParams& aParams) const {
  return aWM.IsVertical() ? GetWidth(aParams) : GetHeight(aParams);
}

inline AnchorResolvedSize nsStylePosition::MinBSize(
    WritingMode aWM, const AnchorPosResolutionParams& aParams) const {
  return aWM.IsVertical() ? GetMinWidth(aParams) : GetMinHeight(aParams);
}

inline AnchorResolvedMaxSize nsStylePosition::MaxBSize(
    WritingMode aWM, const AnchorPosResolutionParams& aParams) const {
  return aWM.IsVertical() ? GetMaxWidth(aParams) : GetMaxHeight(aParams);
}

inline AnchorResolvedSize nsStylePosition::Size(
    mozilla::LogicalAxis aAxis, WritingMode aWM,
    const AnchorPosResolutionParams& aParams) const {
  return aAxis == mozilla::LogicalAxis::Inline ? ISize(aWM, aParams)
                                               : BSize(aWM, aParams);
}

inline AnchorResolvedSize nsStylePosition::MinSize(
    mozilla::LogicalAxis aAxis, WritingMode aWM,
    const AnchorPosResolutionParams& aParams) const {
  return aAxis == mozilla::LogicalAxis::Inline ? MinISize(aWM, aParams)
                                               : MinBSize(aWM, aParams);
}

inline AnchorResolvedMaxSize nsStylePosition::MaxSize(
    mozilla::LogicalAxis aAxis, mozilla::WritingMode aWM,
    const AnchorPosResolutionParams& aParams) const {
  return aAxis == mozilla::LogicalAxis::Inline ? MaxISize(aWM, aParams)
                                               : MaxBSize(aWM, aParams);
}

inline bool nsStylePosition::ISizeDependsOnContainer(
    const AnchorResolvedSize& aSize) {
  return aSize->IsAuto() || ISizeCoordDependsOnContainer(*aSize);
}

inline bool nsStylePosition::MinISizeDependsOnContainer(
    const AnchorResolvedSize& aSize) {
  // NOTE: For a flex item, "min-inline-size:auto" is supposed to behave like
  // "min-content", which does depend on the container, so you might think we'd
  // need a special case for "flex item && min-inline-size:auto" here. However,
  // we don't actually need that special-case code, because flex items are
  // explicitly supposed to *ignore* their min-inline-size (i.e. behave like
  // it's 0) until the flex container explicitly considers it. So -- since the
  // flex container doesn't rely on this method, we don't need to worry about
  // special behavior for flex items' "min-inline-size:auto" values here.
  return ISizeCoordDependsOnContainer(*aSize);
}

inline bool nsStylePosition::MaxISizeDependsOnContainer(
    const AnchorResolvedMaxSize& aSize) {
  // NOTE: The comment above MinISizeDependsOnContainer about flex items
  // applies here, too.
  return ISizeCoordDependsOnContainer(*aSize);
}

// Note that these functions count `auto` as depending on the container
// since that's the case for absolutely positioned elements.
// However, some callers do not care about this case and should check
// for it, since it is the most common case.
// FIXME: We should probably change the assumption to be the other way
// around.
inline bool nsStylePosition::BSizeDependsOnContainer(
    const AnchorResolvedSize& aSize) {
  return aSize->BehavesLikeInitialValueOnBlockAxis() ||
         BSizeCoordDependsOnContainer(*aSize);
}

inline bool nsStylePosition::MinBSizeDependsOnContainer(
    const AnchorResolvedSize& aSize) {
  return BSizeCoordDependsOnContainer(*aSize);
}

inline bool nsStylePosition::MaxBSizeDependsOnContainer(
    const AnchorResolvedMaxSize& aSize) {
  return BSizeCoordDependsOnContainer(*aSize);
}

inline bool nsStyleMargin::HasBlockAxisAuto(
    mozilla::WritingMode aWM, const AnchorPosResolutionParams& aParams) const {
  return GetMargin(mozilla::LogicalSide::BStart, aWM, aParams)->IsAuto() ||
         GetMargin(mozilla::LogicalSide::BEnd, aWM, aParams)->IsAuto();
}

inline bool nsStyleMargin::HasInlineAxisAuto(
    mozilla::WritingMode aWM, const AnchorPosResolutionParams& aParams) const {
  return GetMargin(mozilla::LogicalSide::IStart, aWM, aParams)->IsAuto() ||
         GetMargin(mozilla::LogicalSide::IEnd, aWM, aParams)->IsAuto();
}

inline bool nsStyleMargin::HasAuto(
    mozilla::LogicalAxis aAxis, mozilla::WritingMode aWM,
    const AnchorPosResolutionParams& aParams) const {
  return aAxis == mozilla::LogicalAxis::Inline ? HasInlineAxisAuto(aWM, aParams)
                                               : HasBlockAxisAuto(aWM, aParams);
}

inline AnchorResolvedMargin nsStyleMargin::GetMargin(
    mozilla::LogicalSide aSide, mozilla::WritingMode aWM,
    const AnchorPosResolutionParams& aParams) const {
  return GetMargin(aWM.PhysicalSide(aSide), aParams);
}

inline mozilla::StyleAlignFlags nsStylePosition::UsedSelfAlignment(
    LogicalAxis aAlignContainerAxis,
    const ComputedStyle* aAlignContainerStyle) const {
  return aAlignContainerAxis == LogicalAxis::Block
             ? UsedAlignSelf(aAlignContainerStyle)._0
             : UsedJustifySelf(aAlignContainerStyle)._0;
}

inline mozilla::StyleAlignFlags nsStylePosition::UsedSelfAlignment(
    WritingMode aAlignSubjectWM, LogicalAxis aAlignSubjectAxis,
    WritingMode aAlignContainerWM,
    const ComputedStyle* aAlignContainerStyle) const {
  const auto alignContainerAxis =
      aAlignSubjectWM.ConvertAxisTo(aAlignSubjectAxis, aAlignContainerWM);
  return UsedSelfAlignment(alignContainerAxis, aAlignContainerStyle);
}

inline mozilla::StyleContentDistribution nsStylePosition::UsedContentAlignment(
    mozilla::LogicalAxis aAxis) const {
  return aAxis == mozilla::LogicalAxis::Block ? mAlignContent : mJustifyContent;
}

inline mozilla::UsedFloat nsStyleDisplay::UsedFloat(
    mozilla::WritingMode aCBWM) const {
  switch (mFloat) {
    case mozilla::StyleFloat::None:
      return mozilla::UsedFloat::None;
    case mozilla::StyleFloat::Left:
      return mozilla::UsedFloat::Left;
    case mozilla::StyleFloat::Right:
      return mozilla::UsedFloat::Right;
    case mozilla::StyleFloat::InlineStart:
      return aCBWM.IsBidiLTR() ? mozilla::UsedFloat::Left
                               : mozilla::UsedFloat::Right;
    case mozilla::StyleFloat::InlineEnd:
      return aCBWM.IsBidiLTR() ? mozilla::UsedFloat::Right
                               : mozilla::UsedFloat::Left;
  }
  MOZ_ASSERT_UNREACHABLE("all cases are handled above!");
  return mozilla::UsedFloat::None;
}

inline mozilla::UsedClear nsStyleDisplay::UsedClear(
    mozilla::WritingMode aCBWM) const {
  switch (mClear) {
    case mozilla::StyleClear::None:
      return mozilla::UsedClear::None;
    case mozilla::StyleClear::Left:
      return mozilla::UsedClear::Left;
    case mozilla::StyleClear::Right:
      return mozilla::UsedClear::Right;
    case mozilla::StyleClear::Both:
      return mozilla::UsedClear::Both;
    case mozilla::StyleClear::InlineStart:
      return aCBWM.IsBidiLTR() ? mozilla::UsedClear::Left
                               : mozilla::UsedClear::Right;
    case mozilla::StyleClear::InlineEnd:
      return aCBWM.IsBidiLTR() ? mozilla::UsedClear::Right
                               : mozilla::UsedClear::Left;
  }
  MOZ_ASSERT_UNREACHABLE("all cases are handled above!");
  return mozilla::UsedClear::None;
}

#endif  // WritingModes_h_
