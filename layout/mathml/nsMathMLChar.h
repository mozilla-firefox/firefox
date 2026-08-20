/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsMathMLChar_h_
#define nsMathMLChar_h_

#include "gfxTextRun.h"
#include "mozilla/EnumSet.h"
#include "nsBoundingMetrics.h"
#include "nsColor.h"
#include "nsMathMLOperators.h"
#include "nsPoint.h"
#include "nsRect.h"
#include "nsString.h"

class gfxContext;
class nsGlyphTable;
class nsIFrame;
class nsPresContext;
struct nsBoundingMetrics;
struct nsFont;

namespace mozilla {
class nsDisplayListBuilder;
class nsDisplayListSet;
class ComputedStyle;
}  // namespace mozilla

// Hints for Stretch() to indicate criteria for stretching
enum class MathMLStretchFlag : uint8_t {
  Normal,         // try to stretch to requested size.
  Nearer,         // Stretch very close to requested size.
  Smaller,        // Don't stretch more than requested size.
  Larger,         // Don't stretch less than requested size.
  LargeOperator,  // Draw as a large operator in displaystyle.
  MaxWidth,       // Find the widest metrics returned from a vertical stretch.
};
using MathMLStretchFlags = mozilla::EnumSet<MathMLStretchFlag>;
constexpr MathMLStretchFlags kMathMLStretchVariableSet(
    MathMLStretchFlag::Normal, MathMLStretchFlag::Nearer,
    MathMLStretchFlag::Smaller, MathMLStretchFlag::Larger);
constexpr MathMLStretchFlags kMathMLStretchSet =
    kMathMLStretchVariableSet + MathMLStretchFlag::LargeOperator;

// A single glyph in our internal representation is either
// 1) a code pair from the mathfontFONTFAMILY.properties table, interpreted
// as a Unicode point.
// 2) a glyph index from the Open Type MATH table.
struct nsGlyphCode {
  union {
    char16_t code;
    uint32_t glyphID;
  };
  bool isGlyphID = true;

  bool Exists() const { return isGlyphID ? glyphID != 0 : code != 0; }
  bool operator==(const nsGlyphCode& other) const {
    return (other.isGlyphID == isGlyphID &&
            (isGlyphID ? other.glyphID == glyphID : other.code == code));
  }
};

// Class used to handle stretchy symbols (accent, delimiter and boundary
// symbols).
class nsMathMLChar {
 public:
  typedef gfxTextRun::Range Range;
  typedef mozilla::gfx::DrawTarget DrawTarget;
  using imgDrawingParams = mozilla::image::imgDrawingParams;

  // constructor and destructor
  nsMathMLChar() : mDirection(StretchDirection::Default) {
    MOZ_COUNT_CTOR(nsMathMLChar);
    mComputedStyle = nullptr;
    mUnscaledAscent = 0;
    mScaleX = mScaleY = 1.0;
    mDrawingMethod = DrawingMethod::Normal;
    mMirroringMethod = MirroringMethod::None;
  }

  // not a virtual destructor: this class is not intended to be subclassed
  ~nsMathMLChar();

  void Display(mozilla::nsDisplayListBuilder* aBuilder, nsIFrame* aForFrame,
               const mozilla::nsDisplayListSet& aLists, uint32_t aIndex,
               const nsRect* aSelectedRect = nullptr);

  void PaintForeground(nsIFrame* aForFrame, gfxContext& aRenderingContext,
                       imgDrawingParams& aImgParams, nsPoint aPt,
                       bool aIsSelected);

  // This is the method called to ask the char to stretch itself.
  // @param aContainerSize - IN - suggested size for the stretched char
  // @param aDesiredStretchSize - OUT - the size that the char wants
  nsresult Stretch(nsIFrame* aForFrame, DrawTarget* aDrawTarget,
                   float aFontSizeInflation, StretchDirection aStretchDirection,
                   const nsBoundingMetrics& aContainerSize,
                   nsBoundingMetrics& aDesiredStretchSize,
                   MathMLStretchFlags aStretchFlags, bool aRTL);

  void SetData(nsString& aData);

  void GetData(nsString& aData) { aData = mData; }

  int32_t Length() { return mData.Length(); }

  StretchDirection GetStretchDirection() { return mDirection; }

  // Sometimes we only want to pass the data to another routine,
  // this function helps to avoid copying
  const char16_t* get() { return mData.get(); }

  void GetRect(nsRect& aRect) { aRect = mRect; }

  void SetRect(const nsRect& aRect) { mRect = aRect; }

  // Get the maximum width that the character might have after a vertical
  // Stretch().
  //
  // @param aStretchHint can be the value that will be passed to Stretch().
  // It is used to determine whether the operator is stretchy or a largeop.
  nscoord GetMaxWidth(
      nsIFrame* aForFrame, DrawTarget* aDrawTarget, float aFontSizeInflation,
      MathMLStretchFlags aStretchFlags = MathMLStretchFlag::Normal);

  // Metrics that _exactly_ enclose the char. The char *must* have *already*
  // being stretched before you can call the GetBoundingMetrics() method.
  // IMPORTANT: since chars have their own ComputedStyles, and may be rendered
  // with glyphs that are not in the parent font, just calling the default
  // aRenderingContext.GetBoundingMetrics(aChar) can give incorrect results.
  void GetBoundingMetrics(nsBoundingMetrics& aBoundingMetrics) {
    aBoundingMetrics = mBoundingMetrics;
  }

  void SetBoundingMetrics(nsBoundingMetrics& aBoundingMetrics) {
    mBoundingMetrics = aBoundingMetrics;
  }

  void SetComputedStyle(mozilla::ComputedStyle* aComputedStyle);

  nscoord ItalicCorrection() const { return mItalicCorrection; }

 protected:
  friend class nsGlyphTable;
  friend class nsPropertiesTable;
  friend class nsOpenTypeTable;
  nsString mData;

 private:
  nsRect mRect;
  StretchDirection mDirection;
  nsBoundingMetrics mBoundingMetrics;
  RefPtr<mozilla::ComputedStyle> mComputedStyle;
  // mGlyphs/mBmData are arrays describing the glyphs used to draw the operator.
  // See the drawing methods below.
  RefPtr<gfxTextRun> mGlyphs[4];
  nsBoundingMetrics mBmData[4];
  // mUnscaledAscent is the actual ascent of the char.
  nscoord mUnscaledAscent;
  // mScaleX, mScaleY are the factors by which we scale the char.
  float mScaleX, mScaleY;

  // mDrawingMethod indicates how we draw the stretchy operator:
  // - Normal: we render the mData string normally.
  // - Variant: we draw a larger size variant given by mGlyphs[0].
  // - Parts: we assemble several parts given by mGlyphs[0], ... mGlyphs[4]
  // XXXfredw: the MATH table can have any numbers of parts and extenders.
  enum class DrawingMethod : uint8_t { Normal, Variant, Parts };
  DrawingMethod mDrawingMethod;

  // mMirroringMethod indicates whether the character is mirrored.
  // - None: shouldn't be mirrored.
  // - Character: using unicode character mirroring.
  // - Glyph: using rtlm glyph mirroring.
  // - ScaleFallback: the font doesn't support this character, fall back
  //                  to applying a scale of -1 on the X axis and a scale
  //                  of 1 on the Y axis.
  enum class MirroringMethod : uint8_t {
    None,
    Character,
    Glyph,
    ScaleFallback,
  };
  MirroringMethod mMirroringMethod;

  nscoord mItalicCorrection = 0;

  class StretchEnumContext;
  friend class StretchEnumContext;

  // helper methods
  bool SetFontFamily(nsPresContext* aPresContext,
                     const nsGlyphTable* aGlyphTable,
                     const nsGlyphCode& aGlyphCode,
                     const mozilla::StyleFontFamilyList& aDefaultFamily,
                     nsFont& aFont, RefPtr<gfxFontGroup>* aFontGroup);

  nsresult StretchInternal(nsIFrame* aForFrame, DrawTarget* aDrawTarget,
                           float aFontSizeInflation,
                           StretchDirection& aStretchDirection,
                           const nsBoundingMetrics& aContainerSize,
                           nsBoundingMetrics& aDesiredStretchSize,
                           MathMLStretchFlags aStretchFlags,
                           float aMaxSize = kMathMLOperatorSizeInfinity,
                           bool aMaxSizeIsAbsolute = false);

  nsresult PaintVertically(nsPresContext* aPresContext,
                           gfxContext* aThebesContext,
                           imgDrawingParams& aImgParams, nsRect& aRect,
                           nscolor aColor);

  nsresult PaintHorizontally(nsPresContext* aPresContext,
                             gfxContext* aThebesContext,
                             imgDrawingParams& aImgParams, nsRect& aRect,
                             nscolor aColor);

  void ApplyTransforms(gfxContext* aThebesContext, int32_t aAppUnitsPerGfxUnit,
                       nsRect& r);
};

#endif /* nsMathMLChar_h_ */
