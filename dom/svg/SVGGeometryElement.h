/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SVG_SVGGEOMETRYELEMENT_H_
#define DOM_SVG_SVGGEOMETRYELEMENT_H_

#include "Units.h"
#include "mozilla/EnumeratedArray.h"
#include "mozilla/dom/SVGAnimatedNumber.h"
#include "mozilla/dom/SVGGraphicsElement.h"
#include "mozilla/gfx/2D.h"

namespace mozilla {

class SVGMarkerFrame;

struct SVGMark {
  enum class Type {
    Start,
    Mid,
    End,
  };

  gfx::Point pos;
  float angle;
  Type type;
  SVGMark(const gfx::Point& aPos, float aAngle, Type aType)
      : pos(aPos), angle(aAngle), type(aType) {}
};

// Glue to make EnumeratedArray work with SVGMark::Type.
template <>
struct MaxContiguousEnumValue<SVGMark::Type> {
  static constexpr auto value = SVGMark::Type::End;
};

using SVGMarkerFrames = EnumeratedArray<SVGMark::Type, SVGMarkerFrame*>;

namespace dom {

class DOMSVGAnimatedNumber;
class DOMSVGPoint;

using SVGGeometryElementBase = mozilla::dom::SVGGraphicsElement;

class SVGGeometryElement : public SVGGeometryElementBase {
 protected:
  using CapStyle = mozilla::gfx::CapStyle;
  using DrawTarget = mozilla::gfx::DrawTarget;
  using FillRule = mozilla::gfx::FillRule;
  using Float = mozilla::gfx::Float;
  using Matrix = mozilla::gfx::Matrix;
  using Path = mozilla::gfx::Path;
  using Point = mozilla::gfx::Point;
  using PathBuilder = mozilla::gfx::PathBuilder;
  using Rect = mozilla::gfx::Rect;
  using StrokeOptions = mozilla::gfx::StrokeOptions;

 public:
  explicit SVGGeometryElement(
      already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);

  NS_IMPL_FROMNODE_HELPER(SVGGeometryElement, IsSVGGeometryElement())

  void AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                    const nsAttrValue* aValue, const nsAttrValue* aOldValue,
                    nsIPrincipal* aSubjectPrincipal, bool aNotify) override;
  bool IsSVGGeometryElement() const override { return true; }

  /**
   * Causes this element to discard any Path object that GetOrBuildPath may
   * have cached.
   */
  void ClearAnyCachedPath() final { mCachedPath = nullptr; }

  virtual bool AttributeDefinesGeometry(const nsAtom* aName);

  /**
   * Returns true if this element's geometry depends on the width or height of
   * its coordinate context (typically the viewport established by its nearest
   * <svg> ancestor). In other words, returns true if one of the attributes for
   * which AttributeDefinesGeometry returns true has a percentage value.
   *
   * This could be moved up to a more general class so it can be used for
   * non-leaf elements, but that would require care and for now there's no need.
   */
  bool GeometryDependsOnCoordCtx();

  virtual bool IsMarkable();
  virtual void GetMarkPoints(nsTArray<SVGMark>* aMarks);

  /**
   * A method that can be faster than using a Moz2D Path and calling GetBounds/
   * GetStrokedBounds on it.  It also helps us avoid rounding error for simple
   * shapes and simple transforms where the Moz2D Path backends can fail to
   * produce the clean integer bounds that content authors expect in some cases.
   *
   * If |aToNonScalingStrokeSpace| is non-null then |aBounds|, which is computed
   * in bounds space, has the property that it's the smallest (axis-aligned)
   * rectangular bound containing the image of this shape as stroked in
   * non-scaling-stroke space.  (When all transforms involved are rectilinear
   * the bounds of the image of |aBounds| in non-scaling-stroke space will be
   * tight, but if there are non-rectilinear transforms involved then that may
   * be impossible and this method will return Nothing()).
   *
   * If |aToNonScalingStrokeSpace| is non-null then |*aToNonScalingStrokeSpace|
   * must be non-singular.
   */
  virtual Maybe<Rect> GetGeometryBounds(
      const StrokeOptions& aStrokeOptions, const Matrix& aToBoundsSpace,
      const Matrix* aToNonScalingStrokeSpace = nullptr) {
    return Nothing();
  }

  /**
   * Geometry in the element's user space that can be drawn without building a
   * Path: axis aligned rectangles with optional elliptical corner radii (this
   * includes circles and ellipses, whose radii equal half their size) and
   * lines. Lines can have any orientation.
   */
  class SimpleShape {
   public:
    bool IsRoundedRect() const { return mType == Type::RoundedRect; }
    bool IsLine() const { return mType == Type::Line; }
    // A rounded rect whose radii are all zero.
    bool IsRect() const {
      return mType == Type::RoundedRect && mRadii == CSSSize();
    }

    void SetRoundedRect(const CSSRect& aRect, const CSSSize& aRadii) {
      mRect = aRect;
      mRadii = aRadii;
      mType = Type::RoundedRect;
    }
    void SetEllipse(const CSSPoint& aCenter, const CSSSize& aRadii) {
      SetRoundedRect(
          CSSRect(aCenter - CSSPoint(aRadii.width, aRadii.height), aRadii * 2),
          aRadii);
    }
    void SetLine(const CSSPoint& aStart, const CSSPoint& aEnd) {
      mRect = CSSRect(aStart.x, aStart.y, aEnd.x, aEnd.y);
      mType = Type::Line;
    }
    void Reset() { mType = Type::None; }

    const CSSRect& AsRect() const {
      MOZ_ASSERT(IsRoundedRect());
      return mRect;
    }
    const CSSSize& Radii() const {
      MOZ_ASSERT(IsRoundedRect());
      return mRadii;
    }
    CSSPoint Point1() const {
      MOZ_ASSERT(IsLine());
      return mRect.TopLeft();
    }
    CSSPoint Point2() const {
      MOZ_ASSERT(IsLine());
      return CSSPoint(mRect.width, mRect.height);
    }

   private:
    enum class Type { None, RoundedRect, Line };
    // For lines, the origin is the first point and the size holds the
    // coordinates of the second point.
    CSSRect mRect;
    CSSSize mRadii;
    Type mType = Type::None;
  };

  /**
   * For some platforms there is significant overhead to creating and painting
   * a Moz2D Path object. For simple shapes it is better to get the geometry
   * using this method and then use the optimized DrawTarget methods for
   * filling/stroking rects and lines, or WebRender primitives.
   */
  virtual void GetAsSimpleShape(SimpleShape* aShape) { aShape->Reset(); }

  /**
   * Returns a Path that can be used to paint, hit-test or calculate bounds for
   * this element. May return nullptr if there is no [valid] path. The path
   * that is created may be cached and returned on subsequent calls.
   */
  virtual already_AddRefed<Path> GetOrBuildPath(const DrawTarget* aDrawTarget,
                                                FillRule fillRule);

  /**
   * The same as GetOrBuildPath, but bypasses the cache (neither returns any
   * previously cached Path, nor caches the Path that in does return).
   * this element. May return nullptr if there is no [valid] path.
   */
  virtual already_AddRefed<Path> BuildPath(PathBuilder* aBuilder) = 0;

  /**
   * Returns the bounds of this element's path, mapped into bounds
   * space or Nothing() if no path could be built.
   *
   * aPathInUserSpace must be a Path for this element in its own user space.
   * aPathTransform is applied to it.
   */
  Maybe<Rect> GetBounds(const Matrix& aPathTransform);

  /**
   * Returns the bounds of this element's stroked path, mapped into bounds
   * space by aPathToBounds, or Nothing() if no path could be built.
   *
   * aPathTransform is applied to the path before stroking (the identity matrix
   * unless the element has non-scaling-stroke, in which case it is the
   * transform to the space the stroke is defined in), and aPathToBounds maps
   * from that space to bounds space.
   */
  Maybe<Rect> GetStrokedBounds(const StrokeOptions& aStrokeOptions,
                               const Matrix& aPathTransform,
                               const Matrix& aPathToBounds);

  /**
   * Get the distances from the origin of the path segments.
   * For non-path elements that's just 0 and the total length of the shape.
   */
  virtual bool GetDistancesFromOriginToEndsOfVisibleSegments(
      FallibleTArray<double>* aOutput) {
    aOutput->Clear();
    double distances[] = {0.0, GetTotalLength()};
    return aOutput->AppendElements(Span<double>(distances), fallible);
  }

  /**
   * Returns a Path that can be used to measure the length of this elements
   * path, or to find the position at a given distance along it.
   *
   * This is currently equivalent to calling GetOrBuildPath, but it may not be
   * in the future. The reason for this function to be separate from
   * GetOrBuildPath is because SVGPathData::BuildPath inserts small lines into
   * the path if zero length subpaths are encountered, in order to implement
   * the SVG specifications requirements that zero length subpaths should
   * render circles/squares if stroke-linecap is round/square, respectively.
   * In principle these inserted lines could interfere with path measurement,
   * so we keep callers that are looking to do measurement separate in case we
   * run into problems with the inserted lines negatively affecting measuring
   * for content.
   */
  virtual already_AddRefed<Path> GetOrBuildPathForMeasuring();

  /**
   * If this shape element is a closed loop, this returns true. If it is an
   * unclosed interval, this returns false. This function is used for motion
   * path especially.
   *
   * 1. SVG Paths are closed loops only if the final command in the path list is
   *    a closepath command ("z" or "Z"), otherwise they are unclosed intervals.
   * 2. SVG circles, ellipses, polygons and rects are closed loops.
   * 3. SVG lines and polylines are unclosed intervals.
   *
   * https://drafts.fxtf.org/motion/#path-distance
   */
  virtual bool IsClosedLoop() const { return false; }

  /**
   * Return |true| if some geometry properties (|x|, |y|, etc) are changed
   * because of CSS change.
   */
  bool IsGeometryChangedViaCSS(ComputedStyle const& aNewStyle,
                               ComputedStyle const& aOldStyle) const;

  /**
   * Returns the current computed value of the CSS property 'fill-rule' for
   * this element.
   */
  FillRule GetFillRule();

  enum class PathLengthScaleUsageType { TextPath, Stroking };

  /**
   * Gets the ratio of the actual element's length to the content author's
   * estimated length (as provided by the element's 'pathLength' attribute).
   * This is used to scale stroke dashing, and to scale offsets along a
   * textPath.
   */
  float GetPathLengthScale(PathLengthScaleUsageType aFor);

  // WebIDL
  already_AddRefed<DOMSVGAnimatedNumber> PathLength();
  MOZ_CAN_RUN_SCRIPT bool IsPointInFill(const DOMPointInit& aPoint);
  MOZ_CAN_RUN_SCRIPT bool IsPointInStroke(const DOMPointInit& aPoint);
  MOZ_CAN_RUN_SCRIPT float GetTotalLengthForBinding();
  MOZ_CAN_RUN_SCRIPT already_AddRefed<DOMSVGPoint> GetPointAtLength(
      float distance, ErrorResult& rv);

  gfx::Matrix LocalTransform() const;

 protected:
  // SVGElement method
  NumberAttributesInfo GetNumberInfo() override;

  MOZ_CAN_RUN_SCRIPT void FlushIfNeeded();

  float GetTotalLength();

  static NumberInfo sNumberInfo;
  SVGAnimatedNumber mPathLength;
  mutable RefPtr<Path> mCachedPath;

 private:
  already_AddRefed<Path> GetOrBuildPathForHitTest();

  already_AddRefed<Path> GetTransformedPath(const Matrix& aPathTransform);
};

}  // namespace dom
}  // namespace mozilla

#endif  // DOM_SVG_SVGGEOMETRYELEMENT_H_
