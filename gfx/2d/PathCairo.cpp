/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PathCairo.h"

#include "DrawTargetCairo.h"
#include "HelpersCairo.h"
#include "Logging.h"
#include "PathHelpers.h"

namespace mozilla {
namespace gfx {

already_AddRefed<PathBuilder> PathBuilderCairo::Create(FillRule aFillRule) {
  return MakeAndAddRef<PathBuilderCairo>(aFillRule);
}

PathBuilderCairo::PathBuilderCairo(FillRule aFillRule)
    : PathBuilder(aFillRule) {}

void PathBuilderCairo::MoveTo(const Point& aPoint) {
  cairo_path_data_t data;
  data.header.type = CAIRO_PATH_MOVE_TO;
  data.header.length = 2;
  mPathData.push_back(data);
  data.point.x = aPoint.x;
  data.point.y = aPoint.y;
  mPathData.push_back(data);

  mBeginPoint = mCurrentPoint = aPoint;
}

void PathBuilderCairo::LineTo(const Point& aPoint) {
  cairo_path_data_t data;
  data.header.type = CAIRO_PATH_LINE_TO;
  data.header.length = 2;
  mPathData.push_back(data);
  data.point.x = aPoint.x;
  data.point.y = aPoint.y;
  mPathData.push_back(data);

  mCurrentPoint = aPoint;
}

void PathBuilderCairo::BezierTo(const Point& aCP1, const Point& aCP2,
                                const Point& aCP3) {
  cairo_path_data_t data;
  data.header.type = CAIRO_PATH_CURVE_TO;
  data.header.length = 4;
  mPathData.push_back(data);
  data.point.x = aCP1.x;
  data.point.y = aCP1.y;
  mPathData.push_back(data);
  data.point.x = aCP2.x;
  data.point.y = aCP2.y;
  mPathData.push_back(data);
  data.point.x = aCP3.x;
  data.point.y = aCP3.y;
  mPathData.push_back(data);

  mCurrentPoint = aCP3;
}

void PathBuilderCairo::QuadraticBezierTo(const Point& aCP1, const Point& aCP2) {
  // We need to elevate the degree of this quadratic Bézier to cubic, so we're
  // going to add an intermediate control point, and recompute control point 1.
  // The first and last control points remain the same.
  // This formula can be found on http://fontforge.sourceforge.net/bezier.html
  Point CP0 = CurrentPoint();
  Point CP1 = (CP0 + aCP1 * 2.0) / 3.0;
  Point CP2 = (aCP2 + aCP1 * 2.0) / 3.0;
  Point CP3 = aCP2;

  cairo_path_data_t data;
  data.header.type = CAIRO_PATH_CURVE_TO;
  data.header.length = 4;
  mPathData.push_back(data);
  data.point.x = CP1.x;
  data.point.y = CP1.y;
  mPathData.push_back(data);
  data.point.x = CP2.x;
  data.point.y = CP2.y;
  mPathData.push_back(data);
  data.point.x = CP3.x;
  data.point.y = CP3.y;
  mPathData.push_back(data);

  mCurrentPoint = aCP2;
}

void PathBuilderCairo::Close() {
  cairo_path_data_t data;
  data.header.type = CAIRO_PATH_CLOSE_PATH;
  data.header.length = 1;
  mPathData.push_back(data);

  mCurrentPoint = mBeginPoint;
}

void PathBuilderCairo::Arc(const Point& aOrigin, float aRadius,
                           float aStartAngle, float aEndAngle,
                           bool aAntiClockwise) {
  ArcToBezier(this, aOrigin, Size(aRadius, aRadius), aStartAngle, aEndAngle,
              aAntiClockwise);
}

already_AddRefed<Path> PathBuilderCairo::Finish() {
  return MakeAndAddRef<PathCairo>(mFillRule, mPathData, mCurrentPoint,
                                  mBeginPoint);
}

void PathBuilderCairo::Reset(FillRule aFillRule) {
  mPathData.clear();
  SetFillRule(aFillRule);
  mCurrentPoint = Point();
  mBeginPoint = Point();
}

void PathBuilderCairo::Transform(const Matrix& aTransform) {
  for (size_t i = 0; i < mPathData.size(); ++i) {
    for (int pointCount = mPathData[i].header.length - 1; pointCount > 0;
         --pointCount) {
      cairo_path_data_t& data = mPathData[++i];
      Point newPoint =
          aTransform.TransformPoint(Point(data.point.x, data.point.y));
      data.point.x = newPoint.x;
      data.point.y = newPoint.y;
    }
  }
  mCurrentPoint = aTransform.TransformPoint(mCurrentPoint);
  mBeginPoint = aTransform.TransformPoint(mBeginPoint);
}

PathCairo::PathCairo(FillRule aFillRule,
                     std::vector<cairo_path_data_t>& aPathData,
                     const Point& aCurrentPoint, const Point& aBeginPoint)
    : Path(aFillRule, aCurrentPoint, aBeginPoint), mContainingContext(nullptr) {
  mPathData.swap(aPathData);
}

PathCairo::PathCairo(cairo_path_t* aPath)
    : Path(FillRule::FILL_WINDING), mContainingContext(nullptr) {
  // XXX - mCurrentPoint is not properly set here, the same is true for the
  // D2D Path code, we never require current point when hitting this codepath
  // but this should be fixed.
  for (int i = 0; i < aPath->num_data; i++) {
    mPathData.push_back(aPath->data[i]);
  }
}

PathCairo::~PathCairo() {
  if (mContainingContext) {
    cairo_destroy(mContainingContext);
  }
}

already_AddRefed<PathBuilder> PathCairo::CopyToBuilder(
    FillRule aFillRule, already_AddRefed<PathBuilder> aBuilder) const {
  RefPtr builder(aBuilder.downcast<PathBuilderCairo>());
  if (builder) {
    builder->SetFillRule(aFillRule);
  } else {
    builder = MakeRefPtr<PathBuilderCairo>(aFillRule);
  }
  builder->mPathData = mPathData;
  builder->mCurrentPoint = mCurrentPoint;
  builder->mBeginPoint = mBeginPoint;
  return builder.forget();
}

bool PathCairo::ContainsPoint(const Point& aPoint,
                              const Matrix& aTransform) const {
  Matrix inverse = aTransform;
  if (!inverse.Invert()) {
    return false;
  }
  Point transformed = inverse.TransformPoint(aPoint);

  EnsureContainingContext(aTransform);

  return cairo_in_fill(mContainingContext, transformed.x, transformed.y);
}

bool PathCairo::StrokeContainsPoint(const StrokeOptions& aStrokeOptions,
                                    const Point& aPoint,
                                    const Matrix& aTransform) const {
  Matrix inverse = aTransform;
  if (!inverse.Invert()) {
    return false;
  }
  Point transformed = inverse.TransformPoint(aPoint);

  EnsureContainingContext(aTransform);

  SetCairoStrokeOptions(mContainingContext, aStrokeOptions);

  return cairo_in_stroke(mContainingContext, transformed.x, transformed.y);
}

Rect PathCairo::GetBounds(const Matrix& aTransform) const {
  EnsureContainingContext(aTransform);

  double x1, y1, x2, y2;

  cairo_path_extents(mContainingContext, &x1, &y1, &x2, &y2);
  Rect bounds(Float(x1), Float(y1), Float(x2 - x1), Float(y2 - y1));
  return aTransform.TransformBounds(bounds);
}

Rect PathCairo::GetStrokedBounds(const StrokeOptions& aStrokeOptions,
                                 const Matrix& aTransform) const {
  EnsureContainingContext(aTransform);

  double x1, y1, x2, y2;

  SetCairoStrokeOptions(mContainingContext, aStrokeOptions);

  cairo_stroke_extents(mContainingContext, &x1, &y1, &x2, &y2);
  Rect bounds((Float)x1, (Float)y1, (Float)(x2 - x1), (Float)(y2 - y1));
  return aTransform.TransformBounds(bounds);
}

void PathCairo::StreamToSink(PathSink* aSink) const {
  for (size_t i = 0; i < mPathData.size(); i++) {
    switch (mPathData[i].header.type) {
      case CAIRO_PATH_MOVE_TO:
        i++;
        aSink->MoveTo(Point(mPathData[i].point.x, mPathData[i].point.y));
        break;
      case CAIRO_PATH_LINE_TO:
        i++;
        aSink->LineTo(Point(mPathData[i].point.x, mPathData[i].point.y));
        break;
      case CAIRO_PATH_CURVE_TO:
        aSink->BezierTo(
            Point(mPathData[i + 1].point.x, mPathData[i + 1].point.y),
            Point(mPathData[i + 2].point.x, mPathData[i + 2].point.y),
            Point(mPathData[i + 3].point.x, mPathData[i + 3].point.y));
        i += 3;
        break;
      case CAIRO_PATH_CLOSE_PATH:
        aSink->Close();
        break;
      default:
        // Corrupt path data!
        MOZ_ASSERT(false);
    }
  }
}

bool PathCairo::IsEmpty() const {
  for (size_t i = 0; i < mPathData.size(); i++) {
    switch (mPathData[i].header.type) {
      case CAIRO_PATH_MOVE_TO:
        break;
      case CAIRO_PATH_CLOSE_PATH:
        break;
      default:
        return false;
    }
  }
  return true;
}

void PathCairo::EnsureContainingContext(const Matrix& aTransform) const {
  if (mContainingContext) {
    if (mContainingTransform.ExactlyEquals(aTransform)) {
      return;
    }
  } else {
    mContainingContext = cairo_create(DrawTargetCairo::GetDummySurface());
  }

  mContainingTransform = aTransform;

  cairo_matrix_t mat;
  GfxMatrixToCairoMatrix(mContainingTransform, mat);
  cairo_set_matrix(mContainingContext, &mat);

  SetPathOnContext(mContainingContext);
}

void PathCairo::SetPathOnContext(cairo_t* aContext) const {
  // Needs the correct fill rule set.
  cairo_set_fill_rule(aContext, GfxFillRuleToCairoFillRule(mFillRule));

  cairo_new_path(aContext);

  if (!mPathData.empty()) {
    cairo_path_t path;
    path.data = const_cast<cairo_path_data_t*>(&mPathData.front());
    path.num_data = mPathData.size();
    path.status = CAIRO_STATUS_SUCCESS;
    cairo_append_path(aContext, &path);
  }
}

void PathBuilderCairo::AppendPath(cairo_path_t* aPath) {
  for (int i = 0; i < aPath->num_data; i++) {
    mPathData.push_back(aPath->data[i]);
  }
}

}  // namespace gfx
}  // namespace mozilla
