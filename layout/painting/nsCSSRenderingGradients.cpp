/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* utility functions for drawing borders and backgrounds */

#include "nsCSSRenderingGradients.h"

#include <tuple>

#include "Units.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxGradientCache.h"
#include "gfxUtils.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Helpers.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsCSSColorUtils.h"
#include "nsCSSProps.h"
#include "nsLayoutUtils.h"
#include "nsPoint.h"
#include "nsPresContext.h"
#include "nsRect.h"
#include "nsStyleConsts.h"
#include "nsStyleStructInlines.h"

using namespace mozilla;
using namespace mozilla::gfx;

static CSSPoint ResolvePosition(const Position& aPos, const CSSSize& aSize) {
  CSSCoord h = aPos.horizontal.ResolveToCSSPixels(aSize.width);
  CSSCoord v = aPos.vertical.ResolveToCSSPixels(aSize.height);
  return CSSPoint(h, v);
}

// Given a box with size aBoxSize and origin (0,0), and an angle aAngle,
// and a starting point for the gradient line aStart, find the endpoint of
// the gradient line --- the intersection of the gradient line with a line
// perpendicular to aAngle that passes through the farthest corner in the
// direction aAngle.
static CSSPoint ComputeGradientLineEndFromAngle(const CSSPoint& aStart,
                                                double aAngle,
                                                const CSSSize& aBoxSize) {
  double dx = cos(-aAngle);
  double dy = sin(-aAngle);
  CSSPoint farthestCorner(dx > 0 ? aBoxSize.width : 0,
                          dy > 0 ? aBoxSize.height : 0);
  CSSPoint delta = farthestCorner - aStart;
  double u = delta.x * dy - delta.y * dx;
  return farthestCorner + CSSPoint(-u * dy, u * dx);
}

// Compute the start and end points of the gradient line for a linear gradient.
static std::tuple<CSSPoint, CSSPoint> ComputeLinearGradientLine(
    nsPresContext* aPresContext, const StyleGradient& aGradient,
    const CSSSize& aBoxSize) {
  using X = StyleHorizontalPositionKeyword;
  using Y = StyleVerticalPositionKeyword;

  const StyleLineDirection& direction = aGradient.AsLinear().direction;
  const bool isModern =
      aGradient.AsLinear().compat_mode == StyleGradientCompatMode::Modern;

  CSSPoint center(aBoxSize.width / 2, aBoxSize.height / 2);
  switch (direction.tag) {
    case StyleLineDirection::Tag::Angle: {
      double angle = direction.AsAngle().ToRadians();
      if (isModern) {
        angle = M_PI_2 - angle;
      }
      CSSPoint end = ComputeGradientLineEndFromAngle(center, angle, aBoxSize);
      CSSPoint start = CSSPoint(aBoxSize.width, aBoxSize.height) - end;
      return {start, end};
    }
    case StyleLineDirection::Tag::Vertical: {
      CSSPoint start(center.x, 0);
      CSSPoint end(center.x, aBoxSize.height);
      if (isModern == (direction.AsVertical() == Y::Top)) {
        std::swap(start.y, end.y);
      }
      return {start, end};
    }
    case StyleLineDirection::Tag::Horizontal: {
      CSSPoint start(0, center.y);
      CSSPoint end(aBoxSize.width, center.y);
      if (isModern == (direction.AsHorizontal() == X::Left)) {
        std::swap(start.x, end.x);
      }
      return {start, end};
    }
    case StyleLineDirection::Tag::Corner: {
      const auto& corner = direction.AsCorner();
      const X& h = corner._0;
      const Y& v = corner._1;

      if (isModern) {
        float xSign = h == X::Right ? 1.0 : -1.0;
        float ySign = v == Y::Top ? 1.0 : -1.0;
        double angle = atan2(ySign * aBoxSize.width, xSign * aBoxSize.height);
        CSSPoint end = ComputeGradientLineEndFromAngle(center, angle, aBoxSize);
        CSSPoint start = CSSPoint(aBoxSize.width, aBoxSize.height) - end;
        return {start, end};
      }

      CSSCoord startX = h == X::Left ? 0.0 : aBoxSize.width;
      CSSCoord startY = v == Y::Top ? 0.0 : aBoxSize.height;

      CSSPoint start(startX, startY);
      CSSPoint end = CSSPoint(aBoxSize.width, aBoxSize.height) - start;
      return {start, end};
    }
    default:
      break;
  }
  MOZ_ASSERT_UNREACHABLE("Unknown line direction");
  return {CSSPoint(), CSSPoint()};
}

using EndingShape = StyleGenericEndingShape<Length, LengthPercentage>;
using RadialGradientRadii =
    Variant<StyleShapeExtent, std::pair<CSSCoord, CSSCoord>>;

static RadialGradientRadii ComputeRadialGradientRadii(const EndingShape& aShape,
                                                      const CSSSize& aSize) {
  if (aShape.IsCircle()) {
    auto& circle = aShape.AsCircle();
    if (circle.IsExtent()) {
      return RadialGradientRadii(circle.AsExtent());
    }
    CSSCoord radius = circle.AsRadius().ToCSSPixels();
    return RadialGradientRadii(std::make_pair(radius, radius));
  }
  auto& ellipse = aShape.AsEllipse();
  if (ellipse.IsExtent()) {
    return RadialGradientRadii(ellipse.AsExtent());
  }

  auto& radii = ellipse.AsRadii();
  return RadialGradientRadii(
      std::make_pair(radii._0.ResolveToCSSPixels(aSize.width),
                     radii._1.ResolveToCSSPixels(aSize.height)));
}

// Compute the start and end points of the gradient line for a radial gradient.
// Also returns the horizontal and vertical radii defining the circle or
// ellipse to use.
static std::tuple<CSSPoint, CSSPoint, CSSCoord, CSSCoord>
ComputeRadialGradientLine(const StyleGradient& aGradient,
                          const CSSSize& aBoxSize) {
  const auto& radial = aGradient.AsRadial();
  const EndingShape& endingShape = radial.shape;
  const Position& position = radial.position;
  CSSPoint start = ResolvePosition(position, aBoxSize);

  // Compute gradient shape: the x and y radii of an ellipse.
  CSSCoord radiusX, radiusY;
  CSSCoord leftDistance = Abs(start.x);
  CSSCoord rightDistance = Abs(aBoxSize.width - start.x);
  CSSCoord topDistance = Abs(start.y);
  CSSCoord bottomDistance = Abs(aBoxSize.height - start.y);

  auto radii = ComputeRadialGradientRadii(endingShape, aBoxSize);
  if (radii.is<StyleShapeExtent>()) {
    switch (radii.as<StyleShapeExtent>()) {
      case StyleShapeExtent::ClosestSide:
        radiusX = std::min(leftDistance, rightDistance);
        radiusY = std::min(topDistance, bottomDistance);
        if (endingShape.IsCircle()) {
          radiusX = radiusY = std::min(radiusX, radiusY);
        }
        break;
      case StyleShapeExtent::ClosestCorner: {
        // Compute x and y distances to nearest corner
        CSSCoord offsetX = std::min(leftDistance, rightDistance);
        CSSCoord offsetY = std::min(topDistance, bottomDistance);
        if (endingShape.IsCircle()) {
          radiusX = radiusY = NS_hypot(offsetX, offsetY);
        } else {
          // maintain aspect ratio
          radiusX = offsetX * M_SQRT2;
          radiusY = offsetY * M_SQRT2;
        }
        break;
      }
      case StyleShapeExtent::FarthestSide:
        radiusX = std::max(leftDistance, rightDistance);
        radiusY = std::max(topDistance, bottomDistance);
        if (endingShape.IsCircle()) {
          radiusX = radiusY = std::max(radiusX, radiusY);
        }
        break;
      case StyleShapeExtent::FarthestCorner: {
        // Compute x and y distances to nearest corner
        CSSCoord offsetX = std::max(leftDistance, rightDistance);
        CSSCoord offsetY = std::max(topDistance, bottomDistance);
        if (endingShape.IsCircle()) {
          radiusX = radiusY = NS_hypot(offsetX, offsetY);
        } else {
          // maintain aspect ratio
          radiusX = offsetX * M_SQRT2;
          radiusY = offsetY * M_SQRT2;
        }
        break;
      }
      default:
        MOZ_ASSERT_UNREACHABLE("Unknown shape extent keyword?");
        radiusX = radiusY = 0;
    }
  } else {
    auto pair = radii.as<std::pair<CSSCoord, CSSCoord>>();
    radiusX = pair.first;
    radiusY = pair.second;
  }

  // The gradient line end point is where the gradient line intersects
  // the ellipse.
  CSSPoint end = start + CSSPoint(radiusX, 0);
  return {start, end, radiusX, radiusY};
}

// Compute the center and the start angle of the conic gradient.
static std::tuple<CSSPoint, float> ComputeConicGradientProperties(
    const StyleGradient& aGradient, const CSSSize& aBoxSize) {
  const auto& conic = aGradient.AsConic();
  const Position& position = conic.position;
  float angle = static_cast<float>(conic.angle.ToRadians());
  CSSPoint center = ResolvePosition(position, aBoxSize);

  return {center, angle};
}

static float Interpolate(float aF1, float aF2, float aFrac) {
  return aF1 + aFrac * (aF2 - aF1);
}

static StyleAbsoluteColor Interpolate(const StyleAbsoluteColor& aLeft,
                                      const StyleAbsoluteColor& aRight,
                                      float aFrac) {
  // NOTE: This has to match the interpolation method that WebRender uses which
  // right now is sRGB. In the future we should implement interpolation in more
  // gradient color-spaces.
  static constexpr auto kMethod = StyleColorInterpolationMethod{
      StyleColorSpace::Srgb,
      StyleHueInterpolationMethod::Shorter,
  };
  return Servo_InterpolateColor(kMethod, &aLeft, &aRight, aFrac);
}

static nscoord FindTileStart(nscoord aDirtyCoord, nscoord aTilePos,
                             nscoord aTileDim) {
  NS_ASSERTION(aTileDim > 0, "Non-positive tile dimension");
  double multiples = floor(double(aDirtyCoord - aTilePos) / aTileDim);
  return NSToCoordRound(multiples * aTileDim + aTilePos);
}

static gfxFloat LinearGradientStopPositionForPoint(
    const gfxPoint& aGradientStart, const gfxPoint& aGradientEnd,
    const gfxPoint& aPoint) {
  gfxPoint d = aGradientEnd - aGradientStart;
  gfxPoint p = aPoint - aGradientStart;
  /**
   * Compute a parameter t such that a line perpendicular to the
   * d vector, passing through aGradientStart + d*t, also
   * passes through aPoint.
   *
   * t is given by
   *   (p.x - d.x*t)*d.x + (p.y - d.y*t)*d.y = 0
   *
   * Solving for t we get
   *   numerator = d.x*p.x + d.y*p.y
   *   denominator = d.x^2 + d.y^2
   *   t = numerator/denominator
   *
   * In nsCSSGradientRenderer::Paint we know the length of d
   * is not zero.
   */
  double numerator = d.x.value * p.x.value + d.y.value * p.y.value;
  double denominator = d.x.value * d.x.value + d.y.value * d.y.value;
  return numerator / denominator;
}

static bool RectIsBeyondLinearGradientEdge(const gfxRect& aRect,
                                           const gfxMatrix& aPatternMatrix,
                                           const nsTArray<ColorStop>& aStops,
                                           const gfxPoint& aGradientStart,
                                           const gfxPoint& aGradientEnd,
                                           StyleAbsoluteColor* aOutEdgeColor) {
  gfxFloat topLeft = LinearGradientStopPositionForPoint(
      aGradientStart, aGradientEnd,
      aPatternMatrix.TransformPoint(aRect.TopLeft()));
  gfxFloat topRight = LinearGradientStopPositionForPoint(
      aGradientStart, aGradientEnd,
      aPatternMatrix.TransformPoint(aRect.TopRight()));
  gfxFloat bottomLeft = LinearGradientStopPositionForPoint(
      aGradientStart, aGradientEnd,
      aPatternMatrix.TransformPoint(aRect.BottomLeft()));
  gfxFloat bottomRight = LinearGradientStopPositionForPoint(
      aGradientStart, aGradientEnd,
      aPatternMatrix.TransformPoint(aRect.BottomRight()));

  const ColorStop& firstStop = aStops[0];
  if (topLeft < firstStop.mPosition && topRight < firstStop.mPosition &&
      bottomLeft < firstStop.mPosition && bottomRight < firstStop.mPosition) {
    *aOutEdgeColor = firstStop.mColor;
    return true;
  }

  const ColorStop& lastStop = aStops.LastElement();
  if (topLeft >= lastStop.mPosition && topRight >= lastStop.mPosition &&
      bottomLeft >= lastStop.mPosition && bottomRight >= lastStop.mPosition) {
    *aOutEdgeColor = lastStop.mColor;
    return true;
  }

  return false;
}

static void ResolveMidpoints(nsTArray<ColorStop>& stops) {
  for (size_t x = 1; x < stops.Length() - 1;) {
    if (!stops[x].mIsMidpoint) {
      x++;
      continue;
    }

    const auto& color1 = stops[x - 1].mColor;
    const auto& color2 = stops[x + 1].mColor;
    float offset1 = stops[x - 1].mPosition;
    float offset2 = stops[x + 1].mPosition;
    float offset = stops[x].mPosition;
    // check if everything coincides. If so, ignore the midpoint.
    if (offset - offset1 == offset2 - offset) {
      stops.RemoveElementAt(x);
      continue;
    }

    // Check if we coincide with the left colorstop.
    if (offset1 == offset) {
      // Morph the midpoint to a regular stop with the color of the next
      // color stop.
      stops[x].mColor = color2;
      stops[x].mIsMidpoint = false;
      continue;
    }

    // Check if we coincide with the right colorstop.
    if (offset2 == offset) {
      // Morph the midpoint to a regular stop with the color of the previous
      // color stop.
      stops[x].mColor = color1;
      stops[x].mIsMidpoint = false;
      continue;
    }

    // Calculate the intermediate color stops per the formula of the CSS
    // images spec. http://dev.w3.org/csswg/css-images/#color-stop-syntax
    // 9 points were chosen since it is the minimum number of stops that always
    // give the smoothest appearace regardless of midpoint position and
    // difference in luminance of the end points.
    float midpoint = (offset - offset1) / (offset2 - offset1);
    ColorStop newStops[9];
    if (midpoint > .5f) {
      for (size_t y = 0; y < 7; y++) {
        newStops[y].mPosition = offset1 + (offset - offset1) * (7 + y) / 13;
      }

      newStops[7].mPosition = offset + (offset2 - offset) / 3;
      newStops[8].mPosition = offset + (offset2 - offset) * 2 / 3;
    } else {
      newStops[0].mPosition = offset1 + (offset - offset1) / 3;
      newStops[1].mPosition = offset1 + (offset - offset1) * 2 / 3;

      for (size_t y = 0; y < 7; y++) {
        newStops[y + 2].mPosition = offset + (offset2 - offset) * y / 13;
      }
    }

    // calculate colors
    for (auto& newStop : newStops) {
      const float relativeOffset =
          (newStop.mPosition - offset1) / (offset2 - offset1);
      const float multiplier = powf(relativeOffset, logf(.5f) / logf(midpoint));
      newStop.mColor = Interpolate(color1, color2, multiplier);
    }

    stops.ReplaceElementsAt(x, 1, newStops, 9);
    x += 9;
  }
}

static StyleAbsoluteColor TransparentColor(const StyleAbsoluteColor& aColor) {
  auto color = aColor;
  color.alpha = 0.0f;
  return color;
}

// Adjusts and adds color stops in such a way that drawing the gradient with
// unpremultiplied interpolation looks nearly the same as if it were drawn with
// premultiplied interpolation.
static const float kAlphaIncrementPerGradientStep = 0.1f;
static void ResolvePremultipliedAlpha(nsTArray<ColorStop>& aStops) {
  for (size_t x = 1; x < aStops.Length(); x++) {
    const ColorStop leftStop = aStops[x - 1];
    const ColorStop rightStop = aStops[x];

    // if the left and right stop have the same alpha value, we don't need
    // to do anything. Hardstops should be instant, and also should never
    // require dealing with interpolation.
    if (leftStop.mColor.alpha == rightStop.mColor.alpha ||
        leftStop.mPosition == rightStop.mPosition) {
      continue;
    }

    // Is the stop on the left 100% transparent? If so, have it adopt the color
    // of the right stop
    if (leftStop.mColor.alpha == 0) {
      aStops[x - 1].mColor = TransparentColor(rightStop.mColor);
      continue;
    }

    // Is the stop on the right completely transparent?
    // If so, duplicate it and assign it the color on the left.
    if (rightStop.mColor.alpha == 0) {
      ColorStop newStop = rightStop;
      newStop.mColor = TransparentColor(leftStop.mColor);
      aStops.InsertElementAt(x, newStop);
      x++;
      continue;
    }

    // Now handle cases where one or both of the stops are partially
    // transparent.
    if (leftStop.mColor.alpha != 1.0f || rightStop.mColor.alpha != 1.0f) {
      // Calculate how many extra steps. We do a step per 10% transparency.
      size_t stepCount =
          NSToIntFloor(fabsf(leftStop.mColor.alpha - rightStop.mColor.alpha) /
                       kAlphaIncrementPerGradientStep);
      for (size_t y = 1; y < stepCount; y++) {
        float frac = static_cast<float>(y) / stepCount;
        ColorStop newStop(
            Interpolate(leftStop.mPosition, rightStop.mPosition, frac), false,
            Interpolate(leftStop.mColor, rightStop.mColor, frac));
        aStops.InsertElementAt(x, newStop);
        x++;
      }
    }
  }
}

static ColorStop InterpolateColorStop(const ColorStop& aFirst,
                                      const ColorStop& aSecond,
                                      double aPosition,
                                      const StyleAbsoluteColor& aDefault) {
  MOZ_ASSERT(aFirst.mPosition <= aPosition);
  MOZ_ASSERT(aPosition <= aSecond.mPosition);

  double delta = aSecond.mPosition - aFirst.mPosition;
  if (delta < 1e-6) {
    return ColorStop(aPosition, false, aDefault);
  }

  return ColorStop(aPosition, false,
                   Interpolate(aFirst.mColor, aSecond.mColor,
                               (aPosition - aFirst.mPosition) / delta));
}

// Clamp and extend the given ColorStop array in-place to fit exactly into the
// range [0, 1].
static void ClampColorStops(nsTArray<ColorStop>& aStops) {
  MOZ_ASSERT(aStops.Length() > 0);

  // If all stops are outside the range, then get rid of everything and replace
  // with a single colour.
  if (aStops.Length() < 2 || aStops[0].mPosition > 1 ||
      aStops.LastElement().mPosition < 0) {
    const auto c = aStops[0].mPosition > 1 ? aStops[0].mColor
                                           : aStops.LastElement().mColor;
    aStops.Clear();
    aStops.AppendElement(ColorStop(0, false, c));
    return;
  }

  // Create the 0 and 1 points if they fall in the range of |aStops|, and
  // discard all stops outside the range [0, 1].
  // XXX: If we have stops positioned at 0 or 1, we only keep the innermost of
  // those stops. This should be fine for the current user(s) of this function.
  for (size_t i = aStops.Length() - 1; i > 0; i--) {
    if (aStops[i - 1].mPosition < 1 && aStops[i].mPosition >= 1) {
      // Add a point to position 1.
      aStops[i] =
          InterpolateColorStop(aStops[i - 1], aStops[i],
                               /* aPosition = */ 1, aStops[i - 1].mColor);
      // Remove all the elements whose position is greater than 1.
      aStops.RemoveLastElements(aStops.Length() - (i + 1));
    }
    if (aStops[i - 1].mPosition <= 0 && aStops[i].mPosition > 0) {
      // Add a point to position 0.
      aStops[i - 1] =
          InterpolateColorStop(aStops[i - 1], aStops[i],
                               /* aPosition = */ 0, aStops[i].mColor);
      // Remove all of the preceding stops -- they are all negative.
      aStops.RemoveElementsAt(0, i - 1);
      break;
    }
  }

  MOZ_ASSERT(aStops[0].mPosition >= -1e6);
  MOZ_ASSERT(aStops.LastElement().mPosition - 1 <= 1e6);

  // The end points won't exist yet if they don't fall in the original range of
  // |aStops|. Create them if needed.
  if (aStops[0].mPosition > 0) {
    aStops.InsertElementAt(0, ColorStop(0, false, aStops[0].mColor));
  }
  if (aStops.LastElement().mPosition < 1) {
    aStops.AppendElement(ColorStop(1, false, aStops.LastElement().mColor));
  }
}

namespace mozilla {

template <typename T>
static StyleAbsoluteColor GetSpecifiedColor(
    const StyleGenericGradientItem<StyleColor, T>& aItem,
    const ComputedStyle& aStyle) {
  if (aItem.IsInterpolationHint()) {
    return StyleAbsoluteColor::TRANSPARENT_BLACK;
  }
  const StyleColor& c = aItem.IsSimpleColorStop()
                            ? aItem.AsSimpleColorStop()
                            : aItem.AsComplexColorStop().color;

  return c.ResolveColor(aStyle.StyleText()->mColor);
}

static Maybe<double> GetSpecifiedGradientPosition(
    const StyleGenericGradientItem<StyleColor, StyleLengthPercentage>& aItem,
    CSSCoord aLineLength) {
  if (aItem.IsSimpleColorStop()) {
    return Nothing();
  }

  const LengthPercentage& pos = aItem.IsComplexColorStop()
                                    ? aItem.AsComplexColorStop().position
                                    : aItem.AsInterpolationHint();

  if (pos.ConvertsToPercentage()) {
    return Some(pos.ToPercentage());
  }

  if (aLineLength < 1e-6) {
    return Some(0.0);
  }
  return Some(pos.ResolveToCSSPixels(aLineLength) / aLineLength);
}

// aLineLength argument is unused for conic-gradients.
static Maybe<double> GetSpecifiedGradientPosition(
    const StyleGenericGradientItem<StyleColor, StyleAngleOrPercentage>& aItem,
    CSSCoord aLineLength) {
  if (aItem.IsSimpleColorStop()) {
    return Nothing();
  }

  const StyleAngleOrPercentage& pos = aItem.IsComplexColorStop()
                                          ? aItem.AsComplexColorStop().position
                                          : aItem.AsInterpolationHint();

  if (pos.IsPercentage()) {
    return Some(pos.AsPercentage()._0);
  }

  return Some(pos.AsAngle().ToRadians() / (2 * M_PI));
}

template <typename T>
static nsTArray<ColorStop> ComputeColorStopsForItems(
    ComputedStyle* aComputedStyle,
    Span<const StyleGenericGradientItem<StyleColor, T>> aItems,
    CSSCoord aLineLength) {
  MOZ_ASSERT(!aItems.IsEmpty(),
             "The parser should reject gradients with no stops");

  nsTArray<ColorStop> stops(aItems.Length());

  // If there is a run of stops before stop i that did not have specified
  // positions, then this is the index of the first stop in that run.
  Maybe<size_t> firstUnsetPosition;
  for (size_t i = 0; i < aItems.Length(); ++i) {
    const auto& stop = aItems[i];
    double position;

    Maybe<double> specifiedPosition =
        GetSpecifiedGradientPosition(stop, aLineLength);

    if (specifiedPosition) {
      position = *specifiedPosition;
    } else if (i == 0) {
      // First stop defaults to position 0.0
      position = 0.0;
    } else if (i == aItems.Length() - 1) {
      // Last stop defaults to position 1.0
      position = 1.0;
    } else {
      // Other stops with no specified position get their position assigned
      // later by interpolation, see below.
      // Remember where the run of stops with no specified position starts,
      // if it starts here.
      if (firstUnsetPosition.isNothing()) {
        firstUnsetPosition.emplace(i);
      }
      MOZ_ASSERT(!stop.IsInterpolationHint(),
                 "Interpolation hints always specify position");
      auto color = GetSpecifiedColor(stop, *aComputedStyle);
      stops.AppendElement(ColorStop(0, false, color));
      continue;
    }

    if (i > 0) {
      // Prevent decreasing stop positions by advancing this position
      // to the previous stop position, if necessary
      double previousPosition = firstUnsetPosition
                                    ? stops[*firstUnsetPosition - 1].mPosition
                                    : stops[i - 1].mPosition;
      position = std::max(position, previousPosition);
    }
    auto stopColor = GetSpecifiedColor(stop, *aComputedStyle);
    stops.AppendElement(
        ColorStop(position, stop.IsInterpolationHint(), stopColor));
    if (firstUnsetPosition) {
      // Interpolate positions for all stops that didn't have a specified
      // position
      double p = stops[*firstUnsetPosition - 1].mPosition;
      double d = (stops[i].mPosition - p) / (i - *firstUnsetPosition + 1);
      for (size_t j = *firstUnsetPosition; j < i; ++j) {
        p += d;
        stops[j].mPosition = p;
      }
      firstUnsetPosition.reset();
    }
  }

  return stops;
}

static nsTArray<ColorStop> ComputeColorStops(ComputedStyle* aComputedStyle,
                                             const StyleGradient& aGradient,
                                             CSSCoord aLineLength) {
  if (aGradient.IsLinear()) {
    return ComputeColorStopsForItems(
        aComputedStyle, aGradient.AsLinear().items.AsSpan(), aLineLength);
  }
  if (aGradient.IsRadial()) {
    return ComputeColorStopsForItems(
        aComputedStyle, aGradient.AsRadial().items.AsSpan(), aLineLength);
  }
  return ComputeColorStopsForItems(
      aComputedStyle, aGradient.AsConic().items.AsSpan(), aLineLength);
}

nsCSSGradientRenderer nsCSSGradientRenderer::Create(
    nsPresContext* aPresContext, ComputedStyle* aComputedStyle,
    const StyleGradient& aGradient, const nsSize& aIntrinsicSize) {
  auto srcSize = CSSSize::FromAppUnits(aIntrinsicSize);

  // Compute "gradient line" start and end relative to the intrinsic size of
  // the gradient.
  CSSPoint lineStart, lineEnd, center;  // center is for conic gradients only
  CSSCoord radiusX = 0, radiusY = 0;    // for radial gradients only
  float angle = 0.0;                    // for conic gradients only
  if (aGradient.IsLinear()) {
    std::tie(lineStart, lineEnd) =
        ComputeLinearGradientLine(aPresContext, aGradient, srcSize);
  } else if (aGradient.IsRadial()) {
    std::tie(lineStart, lineEnd, radiusX, radiusY) =
        ComputeRadialGradientLine(aGradient, srcSize);
  } else {
    MOZ_ASSERT(aGradient.IsConic());
    std::tie(center, angle) =
        ComputeConicGradientProperties(aGradient, srcSize);
  }
  // Avoid sending Infs or Nans to downwind draw targets.
  if (!lineStart.IsFinite() || !lineEnd.IsFinite()) {
    lineStart = lineEnd = CSSPoint(0, 0);
  }
  if (!center.IsFinite()) {
    center = CSSPoint(0, 0);
  }
  CSSCoord lineLength =
      NS_hypot(lineEnd.x - lineStart.x, lineEnd.y - lineStart.y);

  // Build color stop array and compute stop positions
  nsTArray<ColorStop> stops =
      ComputeColorStops(aComputedStyle, aGradient, lineLength);

  ResolveMidpoints(stops);

  nsCSSGradientRenderer renderer;
  renderer.mPresContext = aPresContext;
  renderer.mGradient = &aGradient;
  renderer.mStops = std::move(stops);
  renderer.mLineStart = {
      aPresContext->CSSPixelsToDevPixels(lineStart.x),
      aPresContext->CSSPixelsToDevPixels(lineStart.y),
  };
  renderer.mLineEnd = {
      aPresContext->CSSPixelsToDevPixels(lineEnd.x),
      aPresContext->CSSPixelsToDevPixels(lineEnd.y),
  };
  renderer.mRadiusX = aPresContext->CSSPixelsToDevPixels(radiusX);
  renderer.mRadiusY = aPresContext->CSSPixelsToDevPixels(radiusY);
  renderer.mCenter = {
      aPresContext->CSSPixelsToDevPixels(center.x),
      aPresContext->CSSPixelsToDevPixels(center.y),
  };
  renderer.mAngle = angle;
  return renderer;
}

void nsCSSGradientRenderer::Paint(gfxContext& aContext, const nsRect& aDest,
                                  const nsRect& aFillArea,
                                  const nsSize& aRepeatSize,
                                  const CSSIntRect& aSrc,
                                  const nsRect& aDirtyRect, float aOpacity) {
  AUTO_PROFILER_LABEL("nsCSSGradientRenderer::Paint", GRAPHICS);

  if (aDest.IsEmpty() || aFillArea.IsEmpty()) {
    return;
  }

  nscoord appUnitsPerDevPixel = mPresContext->AppUnitsPerDevPixel();

  gfxFloat lineLength =
      NS_hypot(mLineEnd.x - mLineStart.x, mLineEnd.y - mLineStart.y);
  bool cellContainsFill = aDest.Contains(aFillArea);

  // If a non-repeating linear gradient is axis-aligned and there are no gaps
  // between tiles, we can optimise away most of the work by converting to a
  // repeating linear gradient and filling the whole destination rect at once.
  bool forceRepeatToCoverTiles =
      mGradient->IsLinear() &&
      (mLineStart.x == mLineEnd.x) != (mLineStart.y == mLineEnd.y) &&
      aRepeatSize.width == aDest.width && aRepeatSize.height == aDest.height &&
      !(mGradient->Repeating()) && !aSrc.IsEmpty() && !cellContainsFill;

  gfxMatrix matrix;
  if (forceRepeatToCoverTiles) {
    // Length of the source rectangle along the gradient axis.
    double rectLen;
    // The position of the start of the rectangle along the gradient.
    double offset;

    // The gradient line is "backwards". Flip the line upside down to make
    // things easier, and then rotate the matrix to turn everything back the
    // right way up.
    if (mLineStart.x > mLineEnd.x || mLineStart.y > mLineEnd.y) {
      std::swap(mLineStart, mLineEnd);
      matrix.PreScale(-1, -1);
    }

    // Fit the gradient line exactly into the source rect.
    // aSrc is relative to aIntrinsincSize.
    // srcRectDev will be relative to srcSize, so in the same coordinate space
    // as lineStart / lineEnd.
    gfxRect srcRectDev = nsLayoutUtils::RectToGfxRect(
        CSSPixel::ToAppUnits(aSrc), appUnitsPerDevPixel);
    if (mLineStart.x != mLineEnd.x) {
      rectLen = srcRectDev.width;
      offset = (srcRectDev.x - mLineStart.x) / lineLength;
      mLineStart.x = srcRectDev.x;
      mLineEnd.x = srcRectDev.XMost();
    } else {
      rectLen = srcRectDev.height;
      offset = (srcRectDev.y - mLineStart.y) / lineLength;
      mLineStart.y = srcRectDev.y;
      mLineEnd.y = srcRectDev.YMost();
    }

    // Adjust gradient stop positions for the new gradient line.
    double scale = lineLength / rectLen;
    for (size_t i = 0; i < mStops.Length(); i++) {
      mStops[i].mPosition = (mStops[i].mPosition - offset) * fabs(scale);
    }

    // Clamp or extrapolate gradient stops to exactly [0, 1].
    ClampColorStops(mStops);

    lineLength = rectLen;
  }

  // Eliminate negative-position stops if the gradient is radial.
  double firstStop = mStops[0].mPosition;
  if (mGradient->IsRadial() && firstStop < 0.0) {
    if (mGradient->AsRadial().flags & StyleGradientFlags::REPEATING) {
      // Choose an instance of the repeated pattern that gives us all positive
      // stop-offsets.
      double lastStop = mStops[mStops.Length() - 1].mPosition;
      double stopDelta = lastStop - firstStop;
      // If all the stops are in approximately the same place then logic below
      // will kick in that makes us draw just the last stop color, so don't
      // try to do anything in that case. We certainly need to avoid
      // dividing by zero.
      if (stopDelta >= 1e-6) {
        double instanceCount = ceil(-firstStop / stopDelta);
        // Advance stops by instanceCount multiples of the period of the
        // repeating gradient.
        double offset = instanceCount * stopDelta;
        for (uint32_t i = 0; i < mStops.Length(); i++) {
          mStops[i].mPosition += offset;
        }
      }
    } else {
      // Move negative-position stops to position 0.0. We may also need
      // to set the color of the stop to the color the gradient should have
      // at the center of the ellipse.
      for (uint32_t i = 0; i < mStops.Length(); i++) {
        double pos = mStops[i].mPosition;
        if (pos < 0.0) {
          mStops[i].mPosition = 0.0;
          // If this is the last stop, we don't need to adjust the color,
          // it will fill the entire area.
          if (i < mStops.Length() - 1) {
            double nextPos = mStops[i + 1].mPosition;
            // If nextPos is approximately equal to pos, then we don't
            // need to adjust the color of this stop because it's
            // not going to be displayed.
            // If nextPos is negative, we don't need to adjust the color of
            // this stop since it's not going to be displayed because
            // nextPos will also be moved to 0.0.
            if (nextPos >= 0.0 && nextPos - pos >= 1e-6) {
              // Compute how far the new position 0.0 is along the interval
              // between pos and nextPos.
              // XXX Color interpolation (in cairo, too) should use the
              // CSS 'color-interpolation' property!
              float frac = float((0.0 - pos) / (nextPos - pos));
              mStops[i].mColor =
                  Interpolate(mStops[i].mColor, mStops[i + 1].mColor, frac);
            }
          }
        }
      }
    }
    firstStop = mStops[0].mPosition;
    MOZ_ASSERT(firstStop >= 0.0, "Failed to fix stop offsets");
  }

  if (mGradient->IsRadial() &&
      !(mGradient->AsRadial().flags & StyleGradientFlags::REPEATING)) {
    // Direct2D can only handle a particular class of radial gradients because
    // of the way the it specifies gradients. Setting firstStop to 0, when we
    // can, will help us stay on the fast path. Currently we don't do this
    // for repeating gradients but we could by adjusting the stop collection
    // to start at 0
    firstStop = 0;
  }

  double lastStop = mStops[mStops.Length() - 1].mPosition;
  // Cairo gradients must have stop positions in the range [0, 1]. So,
  // stop positions will be normalized below by subtracting firstStop and then
  // multiplying by stopScale.
  double stopScale;
  double stopOrigin = firstStop;
  double stopEnd = lastStop;
  double stopDelta = lastStop - firstStop;
  bool zeroRadius =
      mGradient->IsRadial() && (mRadiusX < 1e-6 || mRadiusY < 1e-6);
  if (stopDelta < 1e-6 || (!mGradient->IsConic() && lineLength < 1e-6) ||
      zeroRadius) {
    // Stops are all at the same place. Map all stops to 0.0.
    // For repeating radial gradients, or for any radial gradients with
    // a zero radius, we need to fill with the last stop color, so just set
    // both radii to 0.
    if (mGradient->Repeating() || zeroRadius) {
      mRadiusX = mRadiusY = 0.0;
    }
    stopDelta = 0.0;
  }

  // Don't normalize non-repeating or degenerate gradients below 0..1
  // This keeps the gradient line as large as the box and doesn't
  // lets us avoiding having to get padding correct for stops
  // at 0 and 1
  if (!mGradient->Repeating() || stopDelta == 0.0) {
    stopOrigin = std::min(stopOrigin, 0.0);
    stopEnd = std::max(stopEnd, 1.0);
  }
  stopScale = 1.0 / (stopEnd - stopOrigin);

  // Create the gradient pattern.
  RefPtr<gfxPattern> gradientPattern;
  gfxPoint gradientStart;
  gfxPoint gradientEnd;
  if (mGradient->IsLinear()) {
    // Compute the actual gradient line ends we need to pass to cairo after
    // stops have been normalized.
    gradientStart = mLineStart + (mLineEnd - mLineStart) * stopOrigin;
    gradientEnd = mLineStart + (mLineEnd - mLineStart) * stopEnd;

    if (stopDelta == 0.0) {
      // Stops are all at the same place. For repeating gradients, this will
      // just paint the last stop color. We don't need to do anything.
      // For non-repeating gradients, this should render as two colors, one
      // on each "side" of the gradient line segment, which is a point. All
      // our stops will be at 0.0; we just need to set the direction vector
      // correctly.
      gradientEnd = gradientStart + (mLineEnd - mLineStart);
    }

    gradientPattern = new gfxPattern(gradientStart.x, gradientStart.y,
                                     gradientEnd.x, gradientEnd.y);
  } else if (mGradient->IsRadial()) {
    NS_ASSERTION(firstStop >= 0.0,
                 "Negative stops not allowed for radial gradients");

    // To form an ellipse, we'll stretch a circle vertically, if necessary.
    // So our radii are based on radiusX.
    double innerRadius = mRadiusX * stopOrigin;
    double outerRadius = mRadiusX * stopEnd;
    if (stopDelta == 0.0) {
      // Stops are all at the same place.  See above (except we now have
      // the inside vs. outside of an ellipse).
      outerRadius = innerRadius + 1;
    }
    gradientPattern = new gfxPattern(mLineStart.x, mLineStart.y, innerRadius,
                                     mLineStart.x, mLineStart.y, outerRadius);
    if (mRadiusX != mRadiusY) {
      // Stretch the circles into ellipses vertically by setting a transform
      // in the pattern.
      // Recall that this is the transform from user space to pattern space.
      // So to stretch the ellipse by factor of P vertically, we scale
      // user coordinates by 1/P.
      matrix.PreTranslate(mLineStart);
      matrix.PreScale(1.0, mRadiusX / mRadiusY);
      matrix.PreTranslate(-mLineStart);
    }
  } else {
    gradientPattern =
        new gfxPattern(mCenter.x, mCenter.y, mAngle, stopOrigin, stopEnd);
  }
  // Use a pattern transform to take account of source and dest rects
  matrix.PreTranslate(gfxPoint(mPresContext->CSSPixelsToDevPixels(aSrc.x),
                               mPresContext->CSSPixelsToDevPixels(aSrc.y)));
  matrix.PreScale(
      gfxFloat(nsPresContext::CSSPixelsToAppUnits(aSrc.width)) / aDest.width,
      gfxFloat(nsPresContext::CSSPixelsToAppUnits(aSrc.height)) / aDest.height);
  gradientPattern->SetMatrix(matrix);

  if (stopDelta == 0.0) {
    // Non-repeating gradient with all stops in same place -> just add
    // first stop and last stop, both at position 0.
    // Repeating gradient with all stops in the same place, or radial
    // gradient with radius of 0 -> just paint the last stop color.
    // We use firstStop offset to keep |stops| with same units (will later
    // normalize to 0).
    auto firstColor(mStops[0].mColor);
    auto lastColor(mStops.LastElement().mColor);
    mStops.Clear();

    if (!mGradient->Repeating() && !zeroRadius) {
      mStops.AppendElement(ColorStop(firstStop, false, firstColor));
    }
    mStops.AppendElement(ColorStop(firstStop, false, lastColor));
  }

  ResolvePremultipliedAlpha(mStops);

  bool isRepeat = mGradient->Repeating() || forceRepeatToCoverTiles;

  // Now set normalized color stops in pattern.
  // Offscreen gradient surface cache (not a tile):
  // On some backends (e.g. D2D), the GradientStops object holds an offscreen
  // surface which is a lookup table used to evaluate the gradient. This surface
  // can use much memory (ram and/or GPU ram) and can be expensive to create. So
  // we cache it. The cache key correlates 1:1 with the arguments for
  // CreateGradientStops (also the implied backend type) Note that GradientStop
  // is a simple struct with a stop value (while GradientStops has the surface).
  nsTArray<gfx::GradientStop> rawStops(mStops.Length());
  StyleColorInterpolationMethod styleColorInterpolationMethod =
      mGradient->ColorInterpolationMethod();
  if (styleColorInterpolationMethod.space != StyleColorSpace::Srgb ||
      gfxPlatform::GetCMSMode() == CMSMode::All) {
    class MOZ_STACK_CLASS GradientStopInterpolator final
        : public ColorStopInterpolator<GradientStopInterpolator> {
     public:
      GradientStopInterpolator(
          const nsTArray<ColorStop>& aStops,
          const StyleColorInterpolationMethod& aStyleColorInterpolationMethod,
          bool aExtend, nsTArray<gfx::GradientStop>& aResult)
          : ColorStopInterpolator(aStops, aStyleColorInterpolationMethod,
                                  aExtend),
            mStops(aResult) {}
      void CreateStop(float aPosition, gfx::DeviceColor aColor) {
        mStops.AppendElement(gfx::GradientStop{aPosition, aColor});
      }

     private:
      nsTArray<gfx::GradientStop>& mStops;
    };

    bool extend = !isRepeat && styleColorInterpolationMethod.hue ==
                                   StyleHueInterpolationMethod::Longer;
    GradientStopInterpolator interpolator(mStops, styleColorInterpolationMethod,
                                          extend, rawStops);
    interpolator.CreateStops();
  } else {
    rawStops.SetLength(mStops.Length());
    for (uint32_t i = 0; i < mStops.Length(); i++) {
      rawStops[i].color = ToDeviceColor(mStops[i].mColor);
      rawStops[i].color.a *= aOpacity;
      rawStops[i].offset = stopScale * (mStops[i].mPosition - stopOrigin);
    }
  }
  RefPtr<mozilla::gfx::GradientStops> gs =
      gfxGradientCache::GetOrCreateGradientStops(
          aContext.GetDrawTarget(), rawStops,
          isRepeat ? gfx::ExtendMode::REPEAT : gfx::ExtendMode::CLAMP);
  gradientPattern->SetColorStops(gs);

  // Paint gradient tiles. This isn't terribly efficient, but doing it this
  // way is simple and sure to get pixel-snapping right. We could speed things
  // up by drawing tiles into temporary surfaces and copying those to the
  // destination, but after pixel-snapping tiles may not all be the same size.
  nsRect dirty;
  if (!dirty.IntersectRect(aDirtyRect, aFillArea)) {
    return;
  }

  gfxRect areaToFill =
      nsLayoutUtils::RectToGfxRect(aFillArea, appUnitsPerDevPixel);
  gfxRect dirtyAreaToFill =
      nsLayoutUtils::RectToGfxRect(dirty, appUnitsPerDevPixel);
  dirtyAreaToFill.RoundOut();

  Matrix ctm = aContext.CurrentMatrix();
  bool isCTMPreservingAxisAlignedRectangles =
      ctm.PreservesAxisAlignedRectangles();

  // xStart/yStart are the top-left corner of the top-left tile.
  nscoord xStart = FindTileStart(dirty.x, aDest.x, aRepeatSize.width);
  nscoord yStart = FindTileStart(dirty.y, aDest.y, aRepeatSize.height);
  nscoord xEnd = forceRepeatToCoverTiles ? xStart + aDest.width : dirty.XMost();
  nscoord yEnd =
      forceRepeatToCoverTiles ? yStart + aDest.height : dirty.YMost();

  if (TryPaintTilesWithExtendMode(aContext, gradientPattern, xStart, yStart,
                                  dirtyAreaToFill, aDest, aRepeatSize,
                                  forceRepeatToCoverTiles)) {
    return;
  }

  // x and y are the top-left corner of the tile to draw
  for (nscoord y = yStart; y < yEnd; y += aRepeatSize.height) {
    for (nscoord x = xStart; x < xEnd; x += aRepeatSize.width) {
      // The coordinates of the tile
      gfxRect tileRect = nsLayoutUtils::RectToGfxRect(
          nsRect(x, y, aDest.width, aDest.height), appUnitsPerDevPixel);
      // The actual area to fill with this tile is the intersection of this
      // tile with the overall area we're supposed to be filling
      gfxRect fillRect =
          forceRepeatToCoverTiles ? areaToFill : tileRect.Intersect(areaToFill);
      // Try snapping the fill rect. Snap its top-left and bottom-right
      // independently to preserve the orientation.
      gfxPoint snappedFillRectTopLeft = fillRect.TopLeft();
      gfxPoint snappedFillRectTopRight = fillRect.TopRight();
      gfxPoint snappedFillRectBottomRight = fillRect.BottomRight();
      // Snap three points instead of just two to ensure we choose the
      // correct orientation if there's a reflection.
      if (isCTMPreservingAxisAlignedRectangles &&
          aContext.UserToDevicePixelSnapped(snappedFillRectTopLeft, true) &&
          aContext.UserToDevicePixelSnapped(snappedFillRectBottomRight, true) &&
          aContext.UserToDevicePixelSnapped(snappedFillRectTopRight, true)) {
        if (snappedFillRectTopLeft.x == snappedFillRectBottomRight.x ||
            snappedFillRectTopLeft.y == snappedFillRectBottomRight.y) {
          // Nothing to draw; avoid scaling by zero and other weirdness that
          // could put the context in an error state.
          continue;
        }
        // Set the context's transform to the transform that maps fillRect to
        // snappedFillRect. The part of the gradient that was going to
        // exactly fill fillRect will fill snappedFillRect instead.
        gfxMatrix transform = gfxUtils::TransformRectToRect(
            fillRect, snappedFillRectTopLeft, snappedFillRectTopRight,
            snappedFillRectBottomRight);
        aContext.SetMatrixDouble(transform);
      }
      aContext.NewPath();
      aContext.Rectangle(fillRect);

      gfxRect dirtyFillRect = fillRect.Intersect(dirtyAreaToFill);
      gfxRect fillRectRelativeToTile = dirtyFillRect - tileRect.TopLeft();
      auto edgeColor = StyleAbsoluteColor::TRANSPARENT_BLACK;
      if (mGradient->IsLinear() && !isRepeat &&
          RectIsBeyondLinearGradientEdge(fillRectRelativeToTile, matrix, mStops,
                                         gradientStart, gradientEnd,
                                         &edgeColor)) {
        edgeColor.alpha *= aOpacity;
        aContext.SetColor(ToSRGBColor(edgeColor));
      } else {
        aContext.SetMatrixDouble(
            aContext.CurrentMatrixDouble().Copy().PreTranslate(
                tileRect.TopLeft()));
        aContext.SetPattern(gradientPattern);
      }
      aContext.Fill();
      aContext.SetMatrix(ctm);
    }
  }
}

bool nsCSSGradientRenderer::TryPaintTilesWithExtendMode(
    gfxContext& aContext, gfxPattern* aGradientPattern, nscoord aXStart,
    nscoord aYStart, const gfxRect& aDirtyAreaToFill, const nsRect& aDest,
    const nsSize& aRepeatSize, bool aForceRepeatToCoverTiles) {
  // If we have forced a non-repeating gradient to repeat to cover tiles,
  // then it will be faster to just paint it once using that optimization
  if (aForceRepeatToCoverTiles) {
    return false;
  }

  nscoord appUnitsPerDevPixel = mPresContext->AppUnitsPerDevPixel();

  // We can only use this fast path if we don't have to worry about pixel
  // snapping, and there is no spacing between tiles. We could handle spacing
  // by increasing the size of tileSurface and leaving it transparent, but I'm
  // not sure it's worth it
  bool canUseExtendModeForTiling = (aXStart % appUnitsPerDevPixel == 0) &&
                                   (aYStart % appUnitsPerDevPixel == 0) &&
                                   (aDest.width % appUnitsPerDevPixel == 0) &&
                                   (aDest.height % appUnitsPerDevPixel == 0) &&
                                   (aRepeatSize.width == aDest.width) &&
                                   (aRepeatSize.height == aDest.height);

  if (!canUseExtendModeForTiling) {
    return false;
  }

  IntSize tileSize{
      NSAppUnitsToIntPixels(aDest.width, appUnitsPerDevPixel),
      NSAppUnitsToIntPixels(aDest.height, appUnitsPerDevPixel),
  };

  // Check whether this is a reasonable surface size and doesn't overflow
  // before doing calculations with the tile size
  if (!Factory::ReasonableSurfaceSize(tileSize)) {
    return false;
  }

  // We only want to do this when there are enough tiles to justify the
  // overhead of painting to an offscreen surface. The heuristic here
  // is when we will be painting at least 16 tiles or more, this is kind
  // of arbitrary
  bool shouldUseExtendModeForTiling =
      aDirtyAreaToFill.Area() > (tileSize.width * tileSize.height) * 16.0;

  if (!shouldUseExtendModeForTiling) {
    return false;
  }

  // Draw the gradient pattern into a surface for our single tile
  RefPtr<gfx::SourceSurface> tileSurface;
  {
    RefPtr<gfx::DrawTarget> tileTarget =
        aContext.GetDrawTarget()->CreateSimilarDrawTarget(
            tileSize, gfx::SurfaceFormat::B8G8R8A8);
    if (!tileTarget || !tileTarget->IsValid()) {
      return false;
    }

    {
      gfxContext tileContext(tileTarget);

      tileContext.SetPattern(aGradientPattern);
      tileContext.Paint();
    }

    tileSurface = tileTarget->Snapshot();
    tileTarget = nullptr;
  }

  // Draw the gradient using tileSurface as a repeating pattern masked by
  // the dirtyRect
  Matrix tileTransform = Matrix::Translation(
      NSAppUnitsToFloatPixels(aXStart, appUnitsPerDevPixel),
      NSAppUnitsToFloatPixels(aYStart, appUnitsPerDevPixel));

  aContext.NewPath();
  aContext.Rectangle(aDirtyAreaToFill);
  aContext.Fill(SurfacePattern(tileSurface, ExtendMode::REPEAT, tileTransform));

  return true;
}

class MOZ_STACK_CLASS WrColorStopInterpolator
    : public ColorStopInterpolator<WrColorStopInterpolator> {
 public:
  WrColorStopInterpolator(
      const nsTArray<ColorStop>& aStops,
      const StyleColorInterpolationMethod& aStyleColorInterpolationMethod,
      float aOpacity, nsTArray<wr::GradientStop>& aResult, bool aExtend)
      : ColorStopInterpolator(aStops, aStyleColorInterpolationMethod, aExtend),
        mResult(aResult),
        mOpacity(aOpacity),
        mOutputStop(0) {}

  void CreateStops() {
    mResult.SetLengthAndRetainStorage(0);
    // We always emit at least two stops (start and end) for each input stop,
    // which avoids ambiguity with incomplete oklch/lch/hsv/hsb color stops for
    // the last stop pair, where the last color stop can't be interpreted on its
    // own because it actually depends on the previous stop.
    mResult.SetLength(mStops.Length() * 2 + kFullRangeExtraStops);
    mOutputStop = 0;
    ColorStopInterpolator::CreateStops();
    mResult.SetLength(mOutputStop);
  }

  void CreateStop(float aPosition, DeviceColor aColor) {
    if (mOutputStop < mResult.Capacity()) {
      mResult[mOutputStop].color = wr::ToColorF(aColor);
      mResult[mOutputStop].color.a *= mOpacity;
      mResult[mOutputStop].offset = aPosition;
      mOutputStop++;
    }
  }

 private:
  nsTArray<wr::GradientStop>& mResult;
  float mOpacity;
  uint32_t mOutputStop;
};

void nsCSSGradientRenderer::BuildWebRenderParameters(
    float aOpacity, wr::ExtendMode& aMode, nsTArray<wr::GradientStop>& aStops,
    LayoutDevicePoint& aLineStart, LayoutDevicePoint& aLineEnd,
    LayoutDeviceSize& aGradientRadius, LayoutDevicePoint& aGradientCenter,
    float& aGradientAngle) {
  aMode =
      mGradient->Repeating() ? wr::ExtendMode::Repeat : wr::ExtendMode::Clamp;

  // If the interpolation space is not sRGB, or if color management is active,
  // we need to add additional stops so that the sRGB interpolation in WebRender
  // still closely approximates the correct curves.  We prefer avoiding this if
  // the gradient is simple because WebRender has fast rendering of linear
  // gradients with 2 stops (which represent >99% of all gradients on the web).
  //
  // WebRender doesn't have easy access to StyleAbsoluteColor and CMS display
  // color correction, so we just expand the gradient stop table significantly
  // so that gamma and hue interpolation errors become imperceptible.
  //
  // This always turns into 128 pairs of stops inside WebRender as an
  // implementation detail, so the number of stops we generate here should have
  // very little impact on performance as the texture upload is always the same,
  // except for the special linear gradient 2-stop case, and it is gpucache so
  // if it does not change it is not re-uploaded.
  //
  // Color management bugs that this addresses:
  // * https://bugzilla.mozilla.org/show_bug.cgi?id=939387
  // * https://bugzilla.mozilla.org/show_bug.cgi?id=1248178
  StyleColorInterpolationMethod styleColorInterpolationMethod =
      mGradient->ColorInterpolationMethod();
  // For colorspaces supported by WebRender (Srgb, Hsl, Hwb) we technically do
  // not need to add extra stops, but the only one of those colorspaces that
  // appears frequently is Srgb, and Srgb still needs extra stops if CMS is
  // enabled.  Hsl/Hwb need extra stops if StyleHueInterpolationMethod is not
  // Shorter, or if CMS is enabled.
  //
  // It's probably best to keep this logic as simple as possible, see
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1885716 for an example of
  // what can happen if we try to be clever here.
  if (styleColorInterpolationMethod.space != StyleColorSpace::Srgb ||
      gfxPlatform::GetCMSMode() == CMSMode::All) {
    // For the specific case of longer hue interpolation on a CSS non-repeating
    // gradient, we have to pretend there is another stop at position=1.0 that
    // duplicates the last stop, this is probably only used for things like a
    // color wheel.  No such problem for SVG as it doesn't have that complexity.
    bool extend = aMode == wr::ExtendMode::Clamp &&
                  styleColorInterpolationMethod.hue ==
                      StyleHueInterpolationMethod::Longer;
    WrColorStopInterpolator interpolator(mStops, styleColorInterpolationMethod,
                                         aOpacity, aStops, extend);
    interpolator.CreateStops();
  } else {
    aStops.SetLength(mStops.Length());
    for (uint32_t i = 0; i < mStops.Length(); i++) {
      aStops[i].color = wr::ToColorF(ToDeviceColor(mStops[i].mColor));
      aStops[i].color.a *= aOpacity;
      aStops[i].offset = (float)mStops[i].mPosition;
    }
  }

  aLineStart = LayoutDevicePoint(mLineStart.x, mLineStart.y);
  aLineEnd = LayoutDevicePoint(mLineEnd.x, mLineEnd.y);
  aGradientRadius = LayoutDeviceSize(mRadiusX, mRadiusY);
  aGradientCenter = LayoutDevicePoint(mCenter.x, mCenter.y);
  aGradientAngle = mAngle;
}

void nsCSSGradientRenderer::BuildWebRenderDisplayItems(
    wr::DisplayListBuilder& aBuilder, const layers::StackingContextHelper& aSc,
    const nsRect& aDest, const nsRect& aFillArea, const nsSize& aRepeatSize,
    const CSSIntRect& aSrc, bool aIsBackfaceVisible, float aOpacity) {
  if (aDest.IsEmpty() || aFillArea.IsEmpty()) {
    return;
  }

  wr::ExtendMode extendMode;
  nsTArray<wr::GradientStop> stops;
  LayoutDevicePoint lineStart;
  LayoutDevicePoint lineEnd;
  LayoutDeviceSize gradientRadius;
  LayoutDevicePoint gradientCenter;
  float gradientAngle;
  BuildWebRenderParameters(aOpacity, extendMode, stops, lineStart, lineEnd,
                           gradientRadius, gradientCenter, gradientAngle);

  nscoord appUnitsPerDevPixel = mPresContext->AppUnitsPerDevPixel();

  nsPoint firstTile =
      nsPoint(FindTileStart(aFillArea.x, aDest.x, aRepeatSize.width),
              FindTileStart(aFillArea.y, aDest.y, aRepeatSize.height));

  // Translate the parameters into device coordinates
  LayoutDeviceRect clipBounds =
      LayoutDevicePixel::FromAppUnits(aFillArea, appUnitsPerDevPixel);
  LayoutDeviceRect firstTileBounds = LayoutDevicePixel::FromAppUnits(
      nsRect(firstTile, aDest.Size()), appUnitsPerDevPixel);
  LayoutDeviceSize tileRepeat =
      LayoutDevicePixel::FromAppUnits(aRepeatSize, appUnitsPerDevPixel);

  // Calculate the bounds of the gradient display item, which starts at the
  // first tile and extends to the end of clip bounds
  LayoutDevicePoint tileToClip =
      clipBounds.BottomRight() - firstTileBounds.TopLeft();
  LayoutDeviceRect gradientBounds = LayoutDeviceRect(
      firstTileBounds.TopLeft(), LayoutDeviceSize(tileToClip.x, tileToClip.y));

  // Calculate the tile spacing, which is the repeat size minus the tile size
  LayoutDeviceSize tileSpacing = tileRepeat - firstTileBounds.Size();

  // srcTransform is used for scaling the gradient to match aSrc
  LayoutDeviceRect srcTransform = LayoutDeviceRect(
      nsPresContext::CSSPixelsToAppUnits(aSrc.x),
      nsPresContext::CSSPixelsToAppUnits(aSrc.y),
      aDest.width / ((float)nsPresContext::CSSPixelsToAppUnits(aSrc.width)),
      aDest.height / ((float)nsPresContext::CSSPixelsToAppUnits(aSrc.height)));

  lineStart.x = (lineStart.x - srcTransform.x) * srcTransform.width;
  lineStart.y = (lineStart.y - srcTransform.y) * srcTransform.height;

  gradientCenter.x = (gradientCenter.x - srcTransform.x) * srcTransform.width;
  gradientCenter.y = (gradientCenter.y - srcTransform.y) * srcTransform.height;

  if (mGradient->IsLinear()) {
    lineEnd.x = (lineEnd.x - srcTransform.x) * srcTransform.width;
    lineEnd.y = (lineEnd.y - srcTransform.y) * srcTransform.height;

    aBuilder.PushLinearGradient(
        mozilla::wr::ToLayoutRect(gradientBounds),
        mozilla::wr::ToLayoutRect(clipBounds), aIsBackfaceVisible,
        mozilla::wr::ToLayoutVector2D(lineStart),
        mozilla::wr::ToLayoutVector2D(lineEnd), stops, extendMode,
        mozilla::wr::ToLayoutSize(firstTileBounds.Size()),
        mozilla::wr::ToLayoutSize(tileSpacing));
  } else if (mGradient->IsRadial()) {
    gradientRadius.width *= srcTransform.width;
    gradientRadius.height *= srcTransform.height;

    aBuilder.PushRadialGradient(
        mozilla::wr::ToLayoutRect(gradientBounds),
        mozilla::wr::ToLayoutRect(clipBounds), aIsBackfaceVisible,
        mozilla::wr::ToLayoutVector2D(lineStart),
        mozilla::wr::ToLayoutSize(gradientRadius), stops, extendMode,
        mozilla::wr::ToLayoutSize(firstTileBounds.Size()),
        mozilla::wr::ToLayoutSize(tileSpacing));
  } else {
    MOZ_ASSERT(mGradient->IsConic());
    aBuilder.PushConicGradient(
        mozilla::wr::ToLayoutRect(gradientBounds),
        mozilla::wr::ToLayoutRect(clipBounds), aIsBackfaceVisible,
        mozilla::wr::ToLayoutVector2D(gradientCenter), gradientAngle, stops,
        extendMode, mozilla::wr::ToLayoutSize(firstTileBounds.Size()),
        mozilla::wr::ToLayoutSize(tileSpacing));
  }
}

}  // namespace mozilla
