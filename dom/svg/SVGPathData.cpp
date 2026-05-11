/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SVGPathData.h"

#include "SVGArcConverter.h"
#include "SVGContentUtils.h"
#include "SVGGeometryElement.h"
#include "SVGPathSegUtils.h"
#include "gfx2DGlue.h"
#include "gfxPlatform.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/SVGPathSegment.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Types.h"
#include "nsError.h"
#include "nsString.h"
#include "nsStyleConsts.h"

using namespace mozilla::gfx;

namespace mozilla {

nsresult SVGPathData::SetValueFromString(const nsACString& aValue) {
  // We don't use a temp variable since the spec says to parse everything up to
  // the first error. We still return any error though so that callers know if
  // there's a problem.
  bool ok = Servo_SVGPathData_Parse(&aValue, &mData);
  return ok ? NS_OK : NS_ERROR_DOM_SYNTAX_ERR;
}

void SVGPathData::GetValueAsString(nsACString& aValue) const {
  Servo_SVGPathData_ToString(&mData, &aValue);
}

bool SVGPathData::GetDistancesFromOriginToEndsOfVisibleSegments(
    FallibleTArray<double>* aOutput) const {
  return GetDistancesFromOriginToEndsOfVisibleSegments(AsSpan(), aOutput);
}

/* static */
bool SVGPathData::GetDistancesFromOriginToEndsOfVisibleSegments(
    Span<const StylePathCommand> aPath, FallibleTArray<double>* aOutput) {
  SVGPathTraversalState state;

  aOutput->Clear();

  bool firstMoveToIsChecked = false;
  for (const auto& cmd : aPath) {
    SVGPathSegUtils::TraversePathSegment(cmd, state);
    if (!std::isfinite(state.length)) {
      return false;
    }

    // We skip all moveto commands except for the initial moveto.
    if (!cmd.IsMove() || !firstMoveToIsChecked) {
      if (!aOutput->AppendElement(state.length, fallible)) {
        return false;
      }
    }

    if (cmd.IsMove() && !firstMoveToIsChecked) {
      firstMoveToIsChecked = true;
    }
  }

  return true;
}

/* static */
already_AddRefed<dom::SVGPathSegment> SVGPathData::GetPathSegmentAtLength(
    dom::SVGPathElement* aPathElement, Span<const StylePathCommand> aPath,
    float aDistance) {
  SVGPathTraversalState state;

  for (const auto& cmd : aPath) {
    SVGPathSegUtils::TraversePathSegment(cmd, state);
    if (state.length >= aDistance) {
      return MakeAndAddRef<dom::SVGPathSegment>(aPathElement, cmd);
    }
  }
  return nullptr;
}

/**
 * The SVG spec says we have to paint stroke caps for zero length subpaths:
 *
 *   http://www.w3.org/TR/SVG11/implnote.html#PathElementImplementationNotes
 *
 * Cairo only does this for |stroke-linecap: round| and not for
 * |stroke-linecap: square| (since that's what Adobe Acrobat has always done).
 * Most likely the other backends that DrawTarget uses have the same behavior.
 *
 * To help us conform to the SVG spec we have this helper function to draw an
 * approximation of square caps for zero length subpaths. It does this by
 * inserting a subpath containing a single user space axis aligned straight
 * line that is as small as it can be while minimizing the risk of it being
 * thrown away by the DrawTarget's backend for being too small to affect
 * rendering. The idea is that we'll then get stroke caps drawn for this axis
 * aligned line, creating an axis aligned rectangle that approximates the
 * square that would ideally be drawn.
 *
 * Since we don't have any information about transforms from user space to
 * device space, we choose the length of the small line that we insert by
 * making it a small percentage of the stroke width of the path. This should
 * hopefully allow us to make the line as long as possible (to avoid rounding
 * issues in the backend resulting in the backend seeing it as having zero
 * length) while still avoiding the small rectangle being noticeably different
 * from a square.
 *
 * Note that this function inserts a subpath into the current gfx path that
 * will be present during both fill and stroke operations.
 */
static void ApproximateZeroLengthSubpathSquareCaps(PathBuilder* aPB,
                                                   const Point& aPoint,
                                                   Float aStrokeWidth) {
  // Note that caps are proportional to stroke width, so if stroke width is
  // zero it's actually fine for |tinyLength| below to end up being zero.
  // However, it would be a waste to inserting a LineTo in that case, so better
  // not to.
  MOZ_ASSERT(aStrokeWidth > 0.0f,
             "Make the caller check for this, or check it here");

  // The fraction of the stroke width that we choose for the length of the
  // line is rather arbitrary, other than being chosen to meet the requirements
  // described in the comment above.

  Float tinyLength = aStrokeWidth / SVG_ZERO_LENGTH_PATH_FIX_FACTOR;

  aPB->LineTo(aPoint + Point(tinyLength, 0));
  aPB->MoveTo(aPoint);
}

#define MAYBE_APPROXIMATE_ZERO_LENGTH_SUBPATH_SQUARE_CAPS_TO_DT  \
  do {                                                           \
    if (!subpathHasLength && hasLineCaps && aStrokeWidth > 0 &&  \
        subpathContainsNonMoveTo && IsValidType(prevSegType) &&  \
        (!IsMoveto(prevSegType) || IsClosePath(segType))) {      \
      ApproximateZeroLengthSubpathSquareCaps(aBuilder, segStart, \
                                             aStrokeWidth);      \
    }                                                            \
  } while (0)

already_AddRefed<Path> SVGPathData::BuildPath(PathBuilder* aBuilder,
                                              StyleStrokeLinecap aStrokeLineCap,
                                              Float aStrokeWidth,
                                              float aZoom) const {
  return BuildPath(AsSpan(), aBuilder, aStrokeLineCap, aStrokeWidth, {}, {},
                   aZoom);
}

#undef MAYBE_APPROXIMATE_ZERO_LENGTH_SUBPATH_SQUARE_CAPS_TO_DT

already_AddRefed<Path> SVGPathData::BuildPathForMeasuring(float aZoom) const {
  // Since the path that we return will not be used for painting it doesn't
  // matter what we pass to CreatePathBuilder as aFillRule. Hawever, we do want
  // to pass something other than NS_STYLE_STROKE_LINECAP_SQUARE as
  // aStrokeLineCap to avoid the insertion of extra little lines (by
  // ApproximateZeroLengthSubpathSquareCaps), in which case the value that we
  // pass as aStrokeWidth doesn't matter (since it's only used to determine the
  // length of those extra little lines).

  RefPtr<DrawTarget> drawTarget =
      gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget();
  RefPtr<PathBuilder> builder =
      drawTarget->CreatePathBuilder(FillRule::FILL_WINDING);
  return BuildPath(builder, StyleStrokeLinecap::Butt, 0, aZoom);
}

/* static */
already_AddRefed<Path> SVGPathData::BuildPathForMeasuring(
    Span<const StylePathCommand> aPath, float aZoom) {
  RefPtr<DrawTarget> drawTarget =
      gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget();
  RefPtr<PathBuilder> builder =
      drawTarget->CreatePathBuilder(FillRule::FILL_WINDING);
  return BuildPath(aPath, builder, StyleStrokeLinecap::Butt, 0, {}, {}, aZoom);
}

static inline StyleCSSFloat GetRotate(const StyleCSSFloat& aAngle) {
  return aAngle;
}

static inline StyleCSSFloat GetRotate(const StyleAngle& aAngle) {
  return aAngle.ToDegrees();
}

template <typename Angle, typename Position, typename LP>
static already_AddRefed<Path> BuildPathInternal(
    Span<const StyleGenericShapeCommand<Angle, Position, LP>> aPath,
    PathBuilder* aBuilder, StyleStrokeLinecap aStrokeLineCap,
    Float aStrokeWidth, const CSSSize& aPercentageBasis, const Point& aOffset,
    float aZoomFactor) {
  using Command = StyleGenericShapeCommand<Angle, Position, LP>;

  if (aPath.IsEmpty() || !aPath[0].IsMove()) {
    return nullptr;  // paths without an initial moveto are invalid
  }

  bool hasLineCaps = aStrokeLineCap != StyleStrokeLinecap::Butt;
  bool subpathHasLength = false;  // visual length
  bool subpathContainsNonMoveTo = false;

  const Command* seg = nullptr;
  const Command* prevSeg = nullptr;
  Point pathStart(0.0, 0.0);  // start point of [sub]path
  Point segStart(0.0, 0.0);
  Point segEnd;
  Point cp1, cp2;    // previous bezier's control points
  Point tcp1, tcp2;  // temporaries

  auto maybeApproximateZeroLengthSubpathSquareCaps =
      [&](const Command* aPrevSeg, const Command* aSeg) {
        if (!subpathHasLength && hasLineCaps && aStrokeWidth > 0 &&
            subpathContainsNonMoveTo && aPrevSeg && aSeg &&
            (!aPrevSeg->IsMove() || aSeg->IsClose())) {
          ApproximateZeroLengthSubpathSquareCaps(aBuilder, segStart,
                                                 aStrokeWidth);
        }
      };

  auto scale = [aOffset, aZoomFactor](const Point& p) {
    return Point(p.x * aZoomFactor, p.y * aZoomFactor) + aOffset;
  };

  // Regarding cp1 and cp2: If the previous segment was a cubic bezier curve,
  // then cp2 is its second control point. If the previous segment was a
  // quadratic curve, then cp1 is its (only) control point.

  for (const auto& cmd : aPath) {
    seg = &cmd;
    switch (cmd.tag) {
      case Command::Tag::Close:
        // set this early to allow drawing of square caps for "M{x},{y} Z":
        subpathContainsNonMoveTo = true;
        maybeApproximateZeroLengthSubpathSquareCaps(prevSeg, seg);
        segEnd = pathStart;
        aBuilder->Close();
        break;
      case Command::Tag::Move: {
        maybeApproximateZeroLengthSubpathSquareCaps(prevSeg, seg);
        const Point& p = cmd.move.point.ToGfxPoint(aPercentageBasis);
        pathStart = segEnd = cmd.move.point.IsToPosition() ? p : segStart + p;
        aBuilder->MoveTo(scale(segEnd));
        subpathHasLength = false;
        break;
      }
      case Command::Tag::Line: {
        const Point& p = cmd.line.point.ToGfxPoint(aPercentageBasis);
        segEnd = cmd.line.point.IsToPosition() ? p : segStart + p;
        if (segEnd != segStart) {
          subpathHasLength = true;
          aBuilder->LineTo(scale(segEnd));
        }
        break;
      }
      case Command::Tag::CubicCurve:
        segEnd = cmd.cubic_curve.point.ToGfxPoint(aPercentageBasis);
        segEnd =
            cmd.cubic_curve.point.IsByCoordinate() ? segEnd + segStart : segEnd;
        cp1 = cmd.cubic_curve.control1.ToGfxPoint(segStart, segEnd,
                                                  aPercentageBasis);
        cp2 = cmd.cubic_curve.control2.ToGfxPoint(segStart, segEnd,
                                                  aPercentageBasis);

        if (segEnd != segStart || segEnd != cp1 || segEnd != cp2) {
          subpathHasLength = true;
          aBuilder->BezierTo(scale(cp1), scale(cp2), scale(segEnd));
        }
        break;

      case Command::Tag::QuadCurve:
        segEnd = cmd.quad_curve.point.ToGfxPoint(aPercentageBasis);
        segEnd = cmd.quad_curve.point.IsByCoordinate()
                     ? segEnd + segStart
                     : segEnd;  // set before setting tcp2!
        cp1 = cmd.quad_curve.control1.ToGfxPoint(segStart, segEnd,
                                                 aPercentageBasis);

        // Convert quadratic curve to cubic curve:
        tcp1 = segStart + (cp1 - segStart) * 2 / 3;
        tcp2 = cp1 + (segEnd - cp1) / 3;

        if (segEnd != segStart || segEnd != cp1) {
          subpathHasLength = true;
          aBuilder->BezierTo(scale(tcp1), scale(tcp2), scale(segEnd));
        }
        break;

      case Command::Tag::Arc: {
        const auto& arc = cmd.arc;
        const Point& radii = arc.radii.ToGfxPoint(aPercentageBasis);
        segEnd = arc.point.ToGfxPoint(aPercentageBasis);
        if (arc.point.IsByCoordinate()) {
          segEnd += segStart;
        }
        if (segEnd != segStart) {
          subpathHasLength = true;
          if (radii.x == 0.0f || radii.y == 0.0f) {
            aBuilder->LineTo(scale(segEnd));
          } else {
            const bool arc_is_large = arc.arc_size == StyleArcSize::Large;
            const bool arc_is_cw = arc.arc_sweep == StyleArcSweep::Cw;
            SVGArcConverter converter(segStart, segEnd, radii,
                                      GetRotate(arc.rotate), arc_is_large,
                                      arc_is_cw);
            while (converter.GetNextSegment(&cp1, &cp2, &segEnd)) {
              aBuilder->BezierTo(scale(cp1), scale(cp2), scale(segEnd));
            }
          }
        }
        break;
      }
      case Command::Tag::HLine: {
        const auto x = cmd.h_line.x.ToGfxCoord(aPercentageBasis.width);
        if (cmd.h_line.x.IsToPosition()) {
          segEnd = Point(x, segStart.y);
        } else {
          segEnd = segStart + Point(x, 0.0f);
        }

        if (segEnd != segStart) {
          subpathHasLength = true;
          aBuilder->LineTo(scale(segEnd));
        }
        break;
      }
      case Command::Tag::VLine: {
        const auto y = cmd.v_line.y.ToGfxCoord(aPercentageBasis.height);
        if (cmd.v_line.y.IsToPosition()) {
          segEnd = Point(segStart.x, y);
        } else {
          segEnd = segStart + Point(0.0f, y);
        }

        if (segEnd != segStart) {
          subpathHasLength = true;
          aBuilder->LineTo(scale(segEnd));
        }
        break;
      }
      case Command::Tag::SmoothCubic:
        segEnd = cmd.smooth_cubic.point.ToGfxPoint(aPercentageBasis);
        segEnd = cmd.smooth_cubic.point.IsByCoordinate() ? segEnd + segStart
                                                         : segEnd;
        cp1 = prevSeg && prevSeg->IsCubicType() ? segStart * 2 - cp2 : segStart;
        cp2 = cmd.smooth_cubic.control2.ToGfxPoint(segStart, segEnd,
                                                   aPercentageBasis);

        if (segEnd != segStart || segEnd != cp1 || segEnd != cp2) {
          subpathHasLength = true;
          aBuilder->BezierTo(scale(cp1), scale(cp2), scale(segEnd));
        }
        break;

      case Command::Tag::SmoothQuad: {
        cp1 = prevSeg && prevSeg->IsQuadraticType() ? segStart * 2 - cp1
                                                    : segStart;
        // Convert quadratic curve to cubic curve:
        tcp1 = segStart + (cp1 - segStart) * 2 / 3;

        const Point& p = cmd.smooth_quad.point.ToGfxPoint(aPercentageBasis);
        // set before setting tcp2!
        segEnd = cmd.smooth_quad.point.IsToPosition() ? p : segStart + p;
        tcp2 = cp1 + (segEnd - cp1) / 3;

        if (segEnd != segStart || segEnd != cp1) {
          subpathHasLength = true;
          aBuilder->BezierTo(scale(tcp1), scale(tcp2), scale(segEnd));
        }
        break;
      }
    }

    subpathContainsNonMoveTo = !cmd.IsMove();
    prevSeg = seg;
    segStart = segEnd;
  }

  MOZ_ASSERT(prevSeg == seg, "prevSegType should be left at the final segType");

  maybeApproximateZeroLengthSubpathSquareCaps(prevSeg, seg);

  return aBuilder->Finish();
}

/* static */
already_AddRefed<Path> SVGPathData::BuildPath(
    Span<const StylePathCommand> aPath, PathBuilder* aBuilder,
    StyleStrokeLinecap aStrokeLineCap, Float aStrokeWidth,
    const CSSSize& aBasis, const gfx::Point& aOffset, float aZoomFactor) {
  return BuildPathInternal(aPath, aBuilder, aStrokeLineCap, aStrokeWidth,
                           aBasis, aOffset, aZoomFactor);
}

/* static */
already_AddRefed<Path> SVGPathData::BuildPath(
    Span<const StyleShapeCommand> aShape, PathBuilder* aBuilder,
    StyleStrokeLinecap aStrokeLineCap, Float aStrokeWidth,
    const CSSSize& aBasis, const gfx::Point& aOffset, float aZoomFactor) {
  return BuildPathInternal(aShape, aBuilder, aStrokeLineCap, aStrokeWidth,
                           aBasis, aOffset, aZoomFactor);
}

static double AngleOfVector(const Point& aVector) {
  // C99 says about atan2 "A domain error may occur if both arguments are
  // zero" and "On a domain error, the function returns an implementation-
  // defined value". In the case of atan2 the implementation-defined value
  // seems to commonly be zero, but it could just as easily be a NaN value.
  // We specifically want zero in this case, hence the check:

  return (aVector != Point(0.0, 0.0)) ? atan2(aVector.y, aVector.x) : 0.0;
}

static float AngleOfVector(const Point& cp1, const Point& cp2) {
  return static_cast<float>(AngleOfVector(cp1 - cp2));
}

// This implements F.6.5 and F.6.6 of
// http://www.w3.org/TR/SVG11/implnote.html#ArcImplementationNotes
static std::tuple<float, float, float, float>
/* rx, ry, segStartAngle, segEndAngle */
ComputeSegAnglesAndCorrectRadii(const Point& aSegStart, const Point& aSegEnd,
                                const float aAngle, const bool aLargeArcFlag,
                                const bool aSweepFlag, const float aRx,
                                const float aRy) {
  float rx = std::abs(aRx);  // F.6.6.1
  float ry = std::abs(aRy);

  // F.6.5.1:
  const float angle = static_cast<float>(aAngle * kRadPerDegree);
  double x1p = cos(angle) * (aSegStart.x - aSegEnd.x) / 2.0 +
               sin(angle) * (aSegStart.y - aSegEnd.y) / 2.0;
  double y1p = -sin(angle) * (aSegStart.x - aSegEnd.x) / 2.0 +
               cos(angle) * (aSegStart.y - aSegEnd.y) / 2.0;

  // This is the root in F.6.5.2 and the numerator under that root:
  double root;
  double numerator =
      rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;

  if (numerator >= 0.0) {
    root = sqrt(numerator / (rx * rx * y1p * y1p + ry * ry * x1p * x1p));
    if (aLargeArcFlag == aSweepFlag) root = -root;
  } else {
    // F.6.6 step 3 - |numerator < 0.0|. This is equivalent to the result
    // of F.6.6.2 (lamedh) being greater than one. What we have here is
    // ellipse radii that are too small for the ellipse to reach between
    // segStart and segEnd. We scale the radii up uniformly so that the
    // ellipse is just big enough to fit (i.e. to the point where there is
    // exactly one solution).

    double lamedh =
        1.0 - numerator / (rx * rx * ry * ry);  // equiv to eqn F.6.6.2
    double s = sqrt(lamedh);
    rx = static_cast<float>((double)rx * s);  // F.6.6.3
    ry = static_cast<float>((double)ry * s);
    root = 0.0;
  }

  double cxp = root * rx * y1p / ry;  // F.6.5.2
  double cyp = -root * ry * x1p / rx;

  double theta =
      AngleOfVector(Point(static_cast<float>((x1p - cxp) / rx),
                          static_cast<float>((y1p - cyp) / ry)));  // F.6.5.5
  double delta =
      AngleOfVector(Point(static_cast<float>((-x1p - cxp) / rx),
                          static_cast<float>((-y1p - cyp) / ry))) -  // F.6.5.6
      theta;
  if (!aSweepFlag && delta > 0) {
    delta -= 2.0 * M_PI;
  } else if (aSweepFlag && delta < 0) {
    delta += 2.0 * M_PI;
  }

  double tx1, ty1, tx2, ty2;
  tx1 = -cos(angle) * rx * sin(theta) - sin(angle) * ry * cos(theta);
  ty1 = -sin(angle) * rx * sin(theta) + cos(angle) * ry * cos(theta);
  tx2 = -cos(angle) * rx * sin(theta + delta) -
        sin(angle) * ry * cos(theta + delta);
  ty2 = -sin(angle) * rx * sin(theta + delta) +
        cos(angle) * ry * cos(theta + delta);

  if (delta < 0.0f) {
    tx1 = -tx1;
    ty1 = -ty1;
    tx2 = -tx2;
    ty2 = -ty2;
  }

  return {rx, ry, static_cast<float>(atan2(ty1, tx1)),
          static_cast<float>(atan2(ty2, tx2))};
}

void SVGPathData::GetMarkerPositioningData(float aZoom,
                                           nsTArray<SVGMark>* aMarks) const {
  return GetMarkerPositioningData(AsSpan(), aZoom, aMarks);
}

// Basically, this is identical to the above function, but replace |mData| with
// |aPath|. We probably can factor out some identical calculation, but I believe
// the above one will be removed because we will use any kind of array of
// StylePathCommand for SVG d attribute in the future.
/* static */
void SVGPathData::GetMarkerPositioningData(Span<const StylePathCommand> aPath,
                                           float aZoom,
                                           nsTArray<SVGMark>* aMarks) {
  if (aPath.IsEmpty()) {
    return;
  }

  // info on current [sub]path (reset every M command):
  Point pathStart(0.0, 0.0);
  float pathStartAngle = 0.0f;
  uint32_t pathStartIndex = 0;

  // info on previous segment:
  const StylePathCommand* prevSeg = nullptr;
  Point prevSegEnd(0.0, 0.0);
  float prevSegEndAngle = 0.0f;
  Point prevCP;  // if prev seg was a bezier, this was its last control point

  for (const StylePathCommand& cmd : aPath) {
    Point& segStart = prevSegEnd;
    Point segEnd;
    float segStartAngle, segEndAngle;

    switch (cmd.tag)  // to find segStartAngle, segEnd and segEndAngle
    {
      case StylePathCommand::Tag::Close:
        segEnd = pathStart;
        segStartAngle = segEndAngle = AngleOfVector(segEnd, segStart);
        break;

      case StylePathCommand::Tag::Move: {
        const Point& p = cmd.move.point.ToGfxPoint() * aZoom;
        pathStart = segEnd = cmd.move.point.IsToPosition() ? p : segStart + p;
        pathStartIndex = aMarks->Length();
        // If authors are going to specify multiple consecutive moveto commands
        // with markers, me might as well make the angle do something useful:
        segStartAngle = segEndAngle = AngleOfVector(segEnd, segStart);
        break;
      }
      case StylePathCommand::Tag::Line: {
        const Point& p = cmd.line.point.ToGfxPoint() * aZoom;
        segEnd = cmd.line.point.IsToPosition() ? p : segStart + p;
        segStartAngle = segEndAngle = AngleOfVector(segEnd, segStart);
        break;
      }
      case StylePathCommand::Tag::CubicCurve: {
        segEnd = cmd.cubic_curve.point.ToGfxPoint() * aZoom;
        segEnd =
            cmd.cubic_curve.point.IsByCoordinate() ? segEnd + segStart : segEnd;
        Point cp1 =
            cmd.cubic_curve.control1.ToGfxPoint(segStart, segEnd) * aZoom;
        Point cp2 =
            cmd.cubic_curve.control2.ToGfxPoint(segStart, segEnd) * aZoom;

        prevCP = cp2;
        segStartAngle = AngleOfVector(
            cp1 == segStart ? (cp1 == cp2 ? segEnd : cp2) : cp1, segStart);
        segEndAngle = AngleOfVector(
            segEnd, cp2 == segEnd ? (cp1 == cp2 ? segStart : cp1) : cp2);
        break;
      }
      case StylePathCommand::Tag::QuadCurve: {
        segEnd = cmd.quad_curve.point.ToGfxPoint() * aZoom;
        segEnd = cmd.quad_curve.point.IsByCoordinate()
                     ? segEnd + segStart
                     : segEnd;  // set before setting tcp2!
        Point cp1 =
            cmd.quad_curve.control1.ToGfxPoint(segStart, segEnd) * aZoom;

        prevCP = cp1;
        segStartAngle = AngleOfVector(cp1 == segStart ? segEnd : cp1, segStart);
        segEndAngle = AngleOfVector(segEnd, cp1 == segEnd ? segStart : cp1);
        break;
      }
      case StylePathCommand::Tag::Arc: {
        const auto& arc = cmd.arc;
        auto radii = arc.radii.ToGfxPoint() * aZoom;
        float rx = radii.x;
        float ry = radii.y;
        float angle = arc.rotate;
        bool largeArcFlag = arc.arc_size == StyleArcSize::Large;
        bool sweepFlag = arc.arc_sweep == StyleArcSweep::Cw;
        segEnd = arc.point.ToGfxPoint() * aZoom;
        if (arc.point.IsByCoordinate()) {
          segEnd += segStart;
        }

        // See section F.6 of SVG 1.1 for details on what we're doing here:
        // http://www.w3.org/TR/SVG11/implnote.html#ArcImplementationNotes

        if (segStart == segEnd) {
          // F.6.2 says "If the endpoints (x1, y1) and (x2, y2) are identical,
          // then this is equivalent to omitting the elliptical arc segment
          // entirely." We take that very literally here, not adding a mark, and
          // not even setting any of the 'prev' variables so that it's as if
          // this arc had never existed; note the difference this will make e.g.
          // if the arc is proceeded by a bezier curve and followed by a
          // "smooth" bezier curve of the same degree!
          continue;
        }

        // Below we have funny interleaving of F.6.6 (Correction of out-of-range
        // radii) and F.6.5 (Conversion from endpoint to center
        // parameterization) which is designed to avoid some unnecessary
        // calculations.

        if (rx == 0.0 || ry == 0.0) {
          // F.6.6 step 1 - straight line or coincidental points
          segStartAngle = segEndAngle = AngleOfVector(segEnd, segStart);
          break;
        }

        std::tie(rx, ry, segStartAngle, segEndAngle) =
            ComputeSegAnglesAndCorrectRadii(segStart, segEnd, angle,
                                            largeArcFlag, sweepFlag, rx, ry);
        break;
      }
      case StylePathCommand::Tag::HLine: {
        const auto x = cmd.h_line.x.ToGfxCoord();
        if (cmd.h_line.x.IsToPosition()) {
          segEnd = Point(x, segStart.y) * aZoom;
        } else {
          segEnd = segStart + Point(x, 0.0f) * aZoom;
        }
        segStartAngle = segEndAngle = AngleOfVector(segEnd, segStart);
        break;
      }
      case StylePathCommand::Tag::VLine: {
        const auto y = cmd.v_line.y.ToGfxCoord();
        if (cmd.v_line.y.IsToPosition()) {
          segEnd = Point(segStart.x, y) * aZoom;
        } else {
          segEnd = segStart + Point(0.0f, y) * aZoom;
        }
        segStartAngle = segEndAngle = AngleOfVector(segEnd, segStart);
        break;
      }
      case StylePathCommand::Tag::SmoothCubic: {
        const Point& cp1 = prevSeg && prevSeg->IsCubicType()
                               ? segStart * 2 - prevCP
                               : segStart;
        segEnd = cmd.smooth_cubic.point.ToGfxPoint() * aZoom;
        segEnd = cmd.smooth_cubic.point.IsByCoordinate() ? segEnd + segStart
                                                         : segEnd;
        Point cp2 =
            cmd.smooth_cubic.control2.ToGfxPoint(segStart, segEnd) * aZoom;

        prevCP = cp2;
        segStartAngle = AngleOfVector(
            cp1 == segStart ? (cp1 == cp2 ? segEnd : cp2) : cp1, segStart);
        segEndAngle = AngleOfVector(
            segEnd, cp2 == segEnd ? (cp1 == cp2 ? segStart : cp1) : cp2);
        break;
      }
      case StylePathCommand::Tag::SmoothQuad: {
        const Point& cp1 = prevSeg && prevSeg->IsQuadraticType()
                               ? segStart * 2 - prevCP
                               : segStart;
        segEnd = cmd.smooth_quad.point.IsToPosition()
                     ? cmd.smooth_quad.point.ToGfxPoint() * aZoom
                     : segStart + cmd.smooth_quad.point.ToGfxPoint() * aZoom;

        prevCP = cp1;
        segStartAngle = AngleOfVector(cp1 == segStart ? segEnd : cp1, segStart);
        segEndAngle = AngleOfVector(segEnd, cp1 == segEnd ? segStart : cp1);
        break;
      }
    }

    // Set the angle of the mark at the start of this segment:
    if (aMarks->Length()) {
      SVGMark& mark = aMarks->LastElement();
      if (!cmd.IsMove() && prevSeg && prevSeg->IsMove()) {
        // start of new subpath
        pathStartAngle = mark.angle = segStartAngle;
      } else if (cmd.IsMove() && !(prevSeg && prevSeg->IsMove())) {
        // end of a subpath
        if (!(prevSeg && prevSeg->IsClose())) {
          mark.angle = prevSegEndAngle;
        }
      } else if (!(cmd.IsClose() && prevSeg && prevSeg->IsClose())) {
        mark.angle =
            SVGContentUtils::AngleBisect(prevSegEndAngle, segStartAngle);
      }
    }

    // Add the mark at the end of this segment, and set its position:
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier.
    aMarks->AppendElement(SVGMark(static_cast<float>(segEnd.x),
                                  static_cast<float>(segEnd.y), 0.0f,
                                  SVGMark::Type::Mid));

    if (cmd.IsClose() && !(prevSeg && prevSeg->IsClose())) {
      aMarks->LastElement().angle = aMarks->ElementAt(pathStartIndex).angle =
          SVGContentUtils::AngleBisect(segEndAngle, pathStartAngle);
    }

    prevSeg = &cmd;
    prevSegEnd = segEnd;
    prevSegEndAngle = segEndAngle;
  }

  if (!aMarks->IsEmpty()) {
    if (!(prevSeg && prevSeg->IsClose())) {
      aMarks->LastElement().angle = prevSegEndAngle;
    }
    aMarks->LastElement().type = SVGMark::Type::End;
    aMarks->ElementAt(0).type = SVGMark::Type::Start;
  }
}

size_t SVGPathData::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
  // TODO: measure mData if unshared?
  return 0;
}

size_t SVGPathData::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

}  // namespace mozilla
