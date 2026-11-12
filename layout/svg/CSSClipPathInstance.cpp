/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "CSSClipPathInstance.h"

#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxPlatform.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/ShapeUtils.h"
#include "mozilla/dom/SVGElement.h"
#include "mozilla/dom/SVGPathData.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/PathHelpers.h"
#include "nsIFrame.h"
#include "nsLayoutUtils.h"

using namespace mozilla::dom;
using namespace mozilla::gfx;

namespace mozilla {

/* static*/
void CSSClipPathInstance::ApplyBasicShapeOrPathClip(
    gfxContext& aContext, nsIFrame* aFrame, const gfxMatrix& aTransform) {
  RefPtr<Path> path =
      CreateClipPathForFrame(aContext.GetDrawTarget(), aFrame, aTransform);
  if (!path) {
    // This behavior matches |SVGClipPathFrame::ApplyClipPath()|.
    // https://www.w3.org/TR/css-masking-1/#ClipPathElement:
    // "An empty clipping path will completely clip away the element that had
    // the clip-path property applied."
    aContext.Clip(Rect());
    return;
  }
  aContext.Clip(path);
}

/* static*/
RefPtr<Path> CSSClipPathInstance::CreateClipPathForFrame(
    gfx::DrawTarget* aDt, nsIFrame* aFrame, const gfxMatrix& aTransform) {
  const auto& clipPathStyle = aFrame->StyleSVGReset()->mClipPath;
  MOZ_ASSERT(clipPathStyle.IsShape() || clipPathStyle.IsBox(),
             "This is used with basic-shape, and geometry-box only");

  CSSClipPathInstance instance(aFrame, clipPathStyle);

  return instance.CreateClipPath(aDt, aTransform);
}

/* static*/
bool CSSClipPathInstance::HitTestBasicShapeOrPathClip(nsIFrame* aFrame,
                                                      const gfxPoint& aPoint) {
  const auto& clipPathStyle = aFrame->StyleSVGReset()->mClipPath;
  MOZ_ASSERT(!clipPathStyle.IsNone(), "unexpected none value");
  MOZ_ASSERT(!clipPathStyle.IsUrl(), "unexpected url value");

  CSSClipPathInstance instance(aFrame, clipPathStyle);

  RefPtr<DrawTarget> drawTarget =
      gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget();
  RefPtr<Path> path = instance.CreateClipPath(
      drawTarget, SVGUtils::GetCSSPxToDevPxMatrix(aFrame));
  float pixelRatio = float(AppUnitsPerCSSPixel()) /
                     float(aFrame->PresContext()->AppUnitsPerDevPixel());
  return path && path->ContainsPoint(ToPoint(aPoint) * pixelRatio, Matrix());
}

/* static */
Maybe<Rect> CSSClipPathInstance::GetBoundingRectForBasicShapeOrPathClip(
    nsIFrame* aFrame, const StyleClipPath& aClipPathStyle) {
  MOZ_ASSERT(aClipPathStyle.IsShape() || aClipPathStyle.IsBox());

  CSSClipPathInstance instance(aFrame, aClipPathStyle);

  RefPtr<DrawTarget> drawTarget =
      gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget();
  RefPtr<Path> path = instance.CreateClipPath(
      drawTarget, SVGUtils::GetCSSPxToDevPxMatrix(aFrame));
  return path ? Some(path->GetBounds()) : Nothing();
}

already_AddRefed<Path> CSSClipPathInstance::CreateClipPath(
    DrawTarget* aDrawTarget, const gfxMatrix& aTransform) {
  nscoord appUnitsPerDevPixel =
      mTargetFrame->PresContext()->AppUnitsPerDevPixel();

  nsRect r = nsLayoutUtils::ComputeClipPathGeometryBox(
      mTargetFrame, mClipPathStyle.IsBox() ? mClipPathStyle.AsBox()
                                           : mClipPathStyle.AsShape()._1);

  gfxRect rr(r.x, r.y, r.width, r.height);
  rr.Scale(1.0 / AppUnitsPerCSSPixel());
  rr = aTransform.TransformRect(rr);
  rr.Scale(appUnitsPerDevPixel);
  rr.Round();

  r = nsRect(int(rr.x), int(rr.y), int(rr.width), int(rr.height));

  if (mClipPathStyle.IsBox()) {
    RefPtr<PathBuilder> builder = aDrawTarget->CreatePathBuilder();
    AppendRectToPath(builder, NSRectToRect(r, appUnitsPerDevPixel), true);
    return builder->Finish();
  }

  MOZ_ASSERT(mClipPathStyle.IsShape());

  r = ToAppUnits(r.ToNearestPixels(appUnitsPerDevPixel), appUnitsPerDevPixel);

  return CreateBasicShapePath(aDrawTarget, mTargetFrame,
                              *mClipPathStyle.AsShape()._0, r);
}

/* static */
already_AddRefed<Path> CSSClipPathInstance::CreateBasicShapePath(
    DrawTarget* aDrawTarget, nsIFrame* aFrame, const StyleBasicShape& aShape,
    const nsRect& aRefBox) {
  switch (aShape.tag) {
    case StyleBasicShape::Tag::Circle:
      return CreateClipPathCircle(aDrawTarget, aFrame, aShape, aRefBox);
    case StyleBasicShape::Tag::Ellipse:
      return CreateClipPathEllipse(aDrawTarget, aFrame, aShape, aRefBox);
    case StyleBasicShape::Tag::Polygon:
      return CreateClipPathPolygon(aDrawTarget, aFrame, aShape, aRefBox);
    case StyleBasicShape::Tag::Rect:
      return CreateClipPathInset(aDrawTarget, aFrame, aShape, aRefBox);
    case StyleBasicShape::Tag::PathOrShape:
      return aShape.AsPathOrShape().IsPath()
                 ? CreateClipPathPath(aDrawTarget, aFrame, aShape, aRefBox)
                 : CreateClipPathShape(aDrawTarget, aFrame, aShape, aRefBox);
    default:
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Unexpected shape type");
  }
  // Return an empty Path:
  RefPtr<PathBuilder> builder = aDrawTarget->CreatePathBuilder();
  return builder->Finish();
}

/* static */
already_AddRefed<Path> CSSClipPathInstance::CreateClipPathCircle(
    DrawTarget* aDrawTarget, nsIFrame* aFrame, const StyleBasicShape& aShape,
    const nsRect& aRefBox) {
  const nsPoint& center =
      ShapeUtils::ComputeCircleOrEllipseCenter(aShape, aRefBox);
  RefPtr<PathBuilder> builder = aDrawTarget->CreatePathBuilder();
  return ShapeUtils::BuildCirclePath(
      aShape, aRefBox, center, aFrame->PresContext()->AppUnitsPerDevPixel(),
      builder);
}

/* static */
already_AddRefed<Path> CSSClipPathInstance::CreateClipPathEllipse(
    DrawTarget* aDrawTarget, nsIFrame* aFrame, const StyleBasicShape& aShape,
    const nsRect& aRefBox) {
  const nsPoint& center =
      ShapeUtils::ComputeCircleOrEllipseCenter(aShape, aRefBox);
  RefPtr<PathBuilder> builder = aDrawTarget->CreatePathBuilder();
  return ShapeUtils::BuildEllipsePath(
      aShape, aRefBox, center, aFrame->PresContext()->AppUnitsPerDevPixel(),
      builder);
}

/* static */
already_AddRefed<Path> CSSClipPathInstance::CreateClipPathPolygon(
    DrawTarget* aDrawTarget, nsIFrame* aFrame, const StyleBasicShape& aShape,
    const nsRect& aRefBox) {
  auto fillRule = aShape.AsPolygon().fill == StyleFillRule::Nonzero
                      ? FillRule::FILL_WINDING
                      : FillRule::FILL_EVEN_ODD;
  RefPtr<PathBuilder> builder = aDrawTarget->CreatePathBuilder(fillRule);
  return ShapeUtils::BuildPolygonPath(
      aShape, aRefBox, aFrame->PresContext()->AppUnitsPerDevPixel(), builder);
}

/* static */
already_AddRefed<Path> CSSClipPathInstance::CreateClipPathInset(
    DrawTarget* aDrawTarget, nsIFrame* aFrame, const StyleBasicShape& aShape,
    const nsRect& aRefBox) {
  RefPtr<PathBuilder> builder = aDrawTarget->CreatePathBuilder();
  return ShapeUtils::BuildInsetPath(
      aShape, aRefBox, aFrame->PresContext()->AppUnitsPerDevPixel(), builder);
}

/* static */
already_AddRefed<Path> CSSClipPathInstance::CreateClipPathPath(
    DrawTarget* aDrawTarget, nsIFrame* aFrame, const StyleBasicShape& aShape,
    const nsRect& aRefBox) {
  const auto& path = aShape.AsPathOrShape().AsPath();

  RefPtr<PathBuilder> builder = aDrawTarget->CreatePathBuilder(
      path.fill == StyleFillRule::Nonzero ? FillRule::FILL_WINDING
                                          : FillRule::FILL_EVEN_ODD);
  const nscoord appUnitsPerDevPixel =
      aFrame->PresContext()->AppUnitsPerDevPixel();
  const Point offset =
      LayoutDevicePoint::FromAppUnits(aRefBox.TopLeft(), appUnitsPerDevPixel)
          .ToUnknownPoint();
  const float scale = aFrame->Style()->EffectiveZoom().Zoom(
      float(AppUnitsPerCSSPixel()) / float(appUnitsPerDevPixel));
  return SVGPathData::BuildPath(path.path._0.AsSpan(), builder,
                                StyleStrokeLinecap::Butt, 0.0, {}, offset,
                                scale);
}

/* static */
already_AddRefed<Path> CSSClipPathInstance::CreateClipPathShape(
    DrawTarget* aDrawTarget, nsIFrame* aFrame, const StyleBasicShape& aShape,
    const nsRect& aRefBox) {
  const auto& shape = aShape.AsPathOrShape().AsShape();

  RefPtr<PathBuilder> builder = aDrawTarget->CreatePathBuilder(
      shape.fill == StyleFillRule::Nonzero ? FillRule::FILL_WINDING
                                           : FillRule::FILL_EVEN_ODD);
  const nscoord appUnitsPerDevPixel =
      aFrame->PresContext()->AppUnitsPerDevPixel();
  const CSSSize basis = CSSSize::FromAppUnits(aRefBox.Size());
  const Point offset =
      LayoutDevicePoint::FromAppUnits(aRefBox.TopLeft(), appUnitsPerDevPixel)
          .ToUnknownPoint();
  // For path() we would need to take into consideration the current CSS zoom,
  // but this is a shape() so the zoom was already applied to the shape
  // commands.
  const float scale = float(AppUnitsPerCSSPixel()) / float(appUnitsPerDevPixel);
  return SVGPathData::BuildPath(shape.commands.AsSpan(), builder,
                                StyleStrokeLinecap::Butt, 0.0, basis, offset,
                                scale);
}

}  // namespace mozilla
