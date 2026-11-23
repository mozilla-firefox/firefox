/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SVG_SVGCONTENTUTILS_H_
#define DOM_SVG_SVGCONTENTUTILS_H_

// include math.h to pick up definition of M_ maths defines e.g. M_PI
#include <math.h>

#include "gfx2DGlue.h"
#include "mozilla/EnumSet.h"
#include "mozilla/dom/SVGLength.h"
#include "mozilla/gfx/2D.h"  // for StrokeOptions
#include "mozilla/gfx/Matrix.h"
#include "nsDependentSubstring.h"
#include "nsError.h"
#include "nsStringFwd.h"
#include "nsTArray.h"

class nsIContent;

class nsIFrame;
class nsPresContext;

namespace mozilla {
class ComputedStyle;
class SVGAnimatedTransformList;
class SVGAnimatedPreserveAspectRatio;
class SVGContextPaint;
class SVGPreserveAspectRatio;
struct StyleLengthPercentage;
namespace dom {
class Document;
class Element;
class SVGElement;
class SVGSVGElement;
class SVGViewportElement;
}  // namespace dom

#define SVG_ZERO_LENGTH_PATH_FIX_FACTOR 512

/**
 * Functions generally used by SVG Content classes. Functions here
 * should not generally depend on layout methods/classes e.g. SVGUtils
 */
class SVGContentUtils {
 public:
  using Float = gfx::Float;
  using Matrix = gfx::Matrix;
  using Rect = gfx::Rect;
  using StrokeOptions = gfx::StrokeOptions;

  /*
   * Get the outer SVG element of an nsIContent
   */
  static dom::SVGSVGElement* GetOuterSVGElement(dom::SVGElement* aSVGElement);

  /**
   * Moz2D's StrokeOptions requires someone else to own its mDashPattern
   * buffer, which is a pain when you want to initialize a StrokeOptions object
   * in a helper function and pass it out. This sub-class owns the mDashPattern
   * buffer so that consumers of such a helper function don't need to worry
   * about creating it, passing it in, or deleting it. (An added benefit is
   * that in the typical case when stroke-dasharray is short it will avoid
   * allocating.)
   */
  struct AutoStrokeOptions : public StrokeOptions {
    AutoStrokeOptions() {
      MOZ_ASSERT(mDashLength == 0, "InitDashPattern() depends on this");
    }
    ~AutoStrokeOptions() {
      if (mDashPattern && mDashPattern != mSmallArray) {
        delete[] mDashPattern;
      }
    }
    /**
     * Creates the buffer to store the stroke-dasharray, assuming out-of-memory
     * does not occur. The buffer's address is assigned to mDashPattern and
     * returned to the caller as a non-const pointer (so that the caller can
     * initialize the values in the buffer, since mDashPattern is const).
     */
    Float* InitDashPattern(size_t aDashCount) {
      if (aDashCount <= std::size(mSmallArray)) {
        mDashPattern = mSmallArray;
        return mSmallArray;
      }
      Float* nonConstArray = new (fallible) Float[aDashCount];
      mDashPattern = nonConstArray;
      return nonConstArray;
    }
    void DiscardDashPattern() {
      if (mDashPattern && mDashPattern != mSmallArray) {
        delete[] mDashPattern;
      }
      mDashLength = 0;
      mDashPattern = nullptr;
    }

   private:
    // Most dasharrays will fit in this and save us allocating
    Float mSmallArray[16];
  };

  enum class StrokeOptionFlag { IgnoreStrokeDashing };
  using StrokeOptionFlags = EnumSet<StrokeOptionFlag>;

  /**
   * Note: the linecap style returned in aStrokeOptions is not valid when
   * ShapeTypeHasNoCorners(aElement) == true && aFlags == eIgnoreStrokeDashing,
   * since when aElement has no corners the rendered linecap style depends on
   * whether or not the stroke is dashed.
   */
  static void GetStrokeOptions(AutoStrokeOptions* aStrokeOptions,
                               dom::SVGElement* aElement,
                               const ComputedStyle* aComputedStyle,
                               const SVGContextPaint* aContextPaint,
                               StrokeOptionFlags aFlags = {});

  /**
   * Returns the current computed value of the CSS property 'stroke-width' for
   * the given element. aComputedStyle may be provided as an optimization.
   * aContextPaint is also optional.
   *
   * Note that this function does NOT take account of the value of the 'stroke'
   * and 'stroke-opacity' properties to, say, return zero if they are "none" or
   * "0", respectively.
   */
  static Float GetStrokeWidth(const dom::SVGElement* aElement,
                              const ComputedStyle* aComputedStyle,
                              const SVGContextPaint* aContextPaint);

  /**
   * Returns true if some aspect of the stroke depends on percentages.
   */
  static bool HasPercentageDependentStroke(
      const ComputedStyle* aComputedStyle,
      const SVGContextPaint* aContextPaint);
  /*
   * Get the number of CSS px (user units) per em (i.e. the em-height in user
   * units) for an nsIContent
   *
   * XXX document the conditions under which these may fail, and what they
   * return in those cases.
   */
  static float GetFontSize(const dom::Element* aElement);
  static float GetFontSize(const nsIFrame* aFrame);
  static float GetFontSize(const ComputedStyle*, nsPresContext*);
  /*
   * Get the number of CSS px (user units) per ex (i.e. the x-height in user
   * units) for an nsIContent
   *
   * XXX document the conditions under which these may fail, and what they
   * return in those cases.
   */
  static float GetFontXHeight(const dom::Element* aElement);
  static float GetFontXHeight(const nsIFrame* aFrame);
  static float GetFontXHeight(const ComputedStyle*, nsPresContext*);

  /*
   * Get the number of CSS px (user units) per lh (i.e. the line-height in
   * user units) for an nsIContent.
   *
   * Requires the element be styled - if not, a default value assuming
   * the font-size of 16px and line-height of 1.2 is returned.
   */
  static float GetLineHeight(const dom::Element* aElement);

  /*
   * Report a localized error message to the error console.
   */
  static nsresult ReportToConsole(const dom::Document* doc,
                                  const char* aWarning,
                                  const nsTArray<nsString>& aParams);

  static Matrix GetCTM(dom::SVGElement* aElement);

  static Matrix GetNonScalingStrokeCTM(dom::SVGElement* aElement);

  static Matrix GetScreenCTM(dom::SVGElement* aElement);

  /**
   * Gets the tight bounds-space stroke bounds of the non-scaling-stroked rect
   * aRect.
   * @param aToBoundsSpace transforms from source space to the space aBounds
   *        should be computed in.  Must be rectilinear.
   * @param aToNonScalingStrokeSpace transforms from source
   *        space to the space in which non-scaling stroke should be applied.
   *        Must be rectilinear.
   */
  static Rect RectilinearGetStrokeBounds(const Rect& aRect,
                                         const Matrix& aToBoundsSpace,
                                         const Matrix& aToNonScalingStrokeSpace,
                                         float aStrokeWidth);

  static dom::SVGViewportElement* GetNearestViewportElement(
      const nsIContent* aContent);

  /**
   * Computes sqrt((aWidth^2 + aHeight^2)/2);
   */
  static double ComputeNormalizedHypotenuse(double aWidth, double aHeight);

  /**
   * Returns the axis length for the given axis size.
   */
  static double AxisLength(const gfxSize& aAxisSize, SVGLength::Axis aAxis);

  /* Returns the angle halfway between the two specified angles */
  static float AngleBisect(float a1, float a2);

  /* Generate a viewbox to viewport transformation matrix */

  static Matrix GetViewBoxTransform(
      float aViewportWidth, float aViewportHeight, float aViewboxX,
      float aViewboxY, float aViewboxWidth, float aViewboxHeight,
      const SVGAnimatedPreserveAspectRatio& aPreserveAspectRatio);

  static Matrix GetViewBoxTransform(
      float aViewportWidth, float aViewportHeight, float aViewboxX,
      float aViewboxY, float aViewboxWidth, float aViewboxHeight,
      const SVGPreserveAspectRatio& aPreserveAspectRatio);

  /**
   * Convert -0.0 to 0.0, otherwise leave the number unchanged.
   */
  static void NormalizeFloat(float& aValue) {
    if (IsNegativeZero(aValue)) {
      aValue = 0.0f;
    }
  }

  /**
   * Parses the sign (+ or -) of a number and moves aIter to the next
   * character if a sign is found.
   * @param aSignMultiplier [outparam] -1 if the sign is negative otherwise 1
   * @return false if we hit the end of the string (i.e. if aIter is initially
   *         at aEnd, or if we reach aEnd right after the sign character).
   */
  static inline bool ParseOptionalSign(nsAString::const_iterator& aIter,
                                       const nsAString::const_iterator& aEnd,
                                       int32_t& aSignMultiplier) {
    if (aIter == aEnd) {
      return false;
    }
    aSignMultiplier = *aIter == '-' ? -1 : 1;

    nsAString::const_iterator iter(aIter);

    if (*iter == '-' || *iter == '+') {
      ++iter;
      if (iter == aEnd) {
        return false;
      }
    }
    aIter = iter;
    return true;
  }

  /**
   * Parse a number of the form:
   * number ::= integer ([Ee] integer)? | [+-]? [0-9]* "." [0-9]+ ([Ee]
   * integer)? Parsing fails if the number cannot be represented by a floatType.
   * If parsing succeeds, aIter is updated so that it points to the character
   * after the end of the number, otherwise it is left unchanged
   */
  template <class floatType>
  static bool ParseNumber(nsAString::const_iterator& aIter,
                          const nsAString::const_iterator& aEnd,
                          floatType& aValue);

  /**
   * Parse a number of the form:
   * number ::= integer ([Ee] integer)? | [+-]? [0-9]* "." [0-9]+ ([Ee]
   * integer)? Parsing fails if there is anything left over after the number, or
   * the number cannot be represented by a floatType.
   */
  template <class floatType>
  static bool ParseNumber(const nsAString& aString, floatType& aValue);

  /**
   * Parse an integer of the form:
   * integer ::= [+-]? [0-9]+
   * The returned number is clamped to an int32_t if outside that range.
   * If parsing succeeds, aIter is updated so that it points to the character
   * after the end of the number, otherwise it is left unchanged
   */
  static bool ParseInteger(nsAString::const_iterator& aIter,
                           const nsAString::const_iterator& aEnd,
                           int32_t& aValue);

  /**
   * Parse an integer of the form:
   * integer ::= [+-]? [0-9]+
   * The returned number is clamped to an int32_t if outside that range.
   * Parsing fails if there is anything left over after the number.
   */
  static bool ParseInteger(const nsAString& aString, int32_t& aValue);

  /**
   * Converts a LengthPercentage into a userspace value, resolving percentage
   * values relative to aContent's SVG viewport.
   */
  static float CoordToFloat(const dom::SVGElement* aContent,
                            const StyleLengthPercentage&,
                            SVGLength::Axis aAxis = SVGLength::Axis::XY);
  /**
   * Parse the SVG path string
   * Returns a path
   * string formatted as an SVG path
   */
  static already_AddRefed<gfx::Path> GetPath(const nsACString& aPathString);

  /**
   *  Returns true if aContent is one of the elements whose stroke is guaranteed
   *  to have no corners: circle or ellipse
   */
  static bool ShapeTypeHasNoCorners(const nsIContent* aContent);

  /**
   *  Return one token in aString, aString may have leading and trailing
   * whitespace; aSuccess will be set to false if there is no token or more than
   * one token, otherwise it's set to true.
   */
  static nsDependentSubstring GetAndEnsureOneToken(const nsAString& aString,
                                                   bool& aSuccess);
};

}  // namespace mozilla

#endif  // DOM_SVG_SVGCONTENTUTILS_H_
