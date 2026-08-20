/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SVGGeometryElement.h"

#include "DOMSVGPoint.h"
#include "SVGAnimatedLength.h"
#include "SVGCircleElement.h"
#include "SVGEllipseElement.h"
#include "SVGGeometryProperty.h"
#include "SVGPathElement.h"
#include "SVGRectElement.h"
#include "gfxPlatform.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SVGContentUtils.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/DOMPoint.h"
#include "mozilla/dom/DOMPointBinding.h"
#include "mozilla/dom/SVGLengthBinding.h"
#include "mozilla/gfx/2D.h"
#include "nsCOMPtr.h"
#include "nsLayoutUtils.h"
#include "nsStyleTransformMatrix.h"

using namespace mozilla::gfx;

namespace mozilla::dom {

SVGElement::NumberInfo SVGGeometryElement::sNumberInfo = {nsGkAtoms::pathLength,
                                                          0};

//----------------------------------------------------------------------
// Implementation

SVGGeometryElement::SVGGeometryElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
    : SVGGeometryElementBase(std::move(aNodeInfo)) {}

SVGElement::NumberAttributesInfo SVGGeometryElement::GetNumberInfo() {
  return NumberAttributesInfo(&mPathLength, &sNumberInfo, 1);
}

void SVGGeometryElement::AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                      const nsAttrValue* aValue,
                                      const nsAttrValue* aOldValue,
                                      nsIPrincipal* aSubjectPrincipal,
                                      bool aNotify) {
  if (mCachedPath && aNamespaceID == kNameSpaceID_None &&
      AttributeDefinesGeometry(aName)) {
    ClearAnyCachedPath();
  }
  return SVGGeometryElementBase::AfterSetAttr(
      aNamespaceID, aName, aValue, aOldValue, aSubjectPrincipal, aNotify);
}

bool SVGGeometryElement::AttributeDefinesGeometry(const nsAtom* aName) {
  if (aName == nsGkAtoms::pathLength) {
    return true;
  }

  // Check for SVGAnimatedLength attribute
  LengthAttributesInfo info = GetLengthInfo();
  for (uint32_t i = 0; i < info.mCount; i++) {
    if (aName == info.mInfos[i].mName) {
      return true;
    }
  }

  return false;
}

bool SVGGeometryElement::GeometryDependsOnCoordCtx() {
  nsAtom* name = NodeInfo()->NameAtom();
  Maybe<bool> hasCtxDependentLength;
  if (name == nsGkAtoms::rect) {
    hasCtxDependentLength =
        static_cast<SVGRectElement*>(this)->HasCtxDependentLength();
  }
  if (name == nsGkAtoms::circle) {
    hasCtxDependentLength =
        static_cast<SVGCircleElement*>(this)->HasCtxDependentLength();
  }
  if (name == nsGkAtoms::ellipse) {
    hasCtxDependentLength =
        static_cast<SVGEllipseElement*>(this)->HasCtxDependentLength();
  }
  if (hasCtxDependentLength) {
    return *hasCtxDependentLength;
  }
  // Check the SVGAnimatedLength attribute
  LengthAttributesInfo info =
      const_cast<SVGGeometryElement*>(this)->GetLengthInfo();
  for (uint32_t i = 0; i < info.mCount; i++) {
    if (info.mValues[i].IsPercentage()) {
      return true;
    }
  }
  return false;
}

bool SVGGeometryElement::IsMarkable() { return false; }

void SVGGeometryElement::GetMarkPoints(nsTArray<SVGMark>* aMarks) {}

already_AddRefed<Path> SVGGeometryElement::GetOrBuildPath(
    const DrawTarget* aDrawTarget, FillRule aFillRule) {
  // We only cache the path if it matches the backend used for screen painting,
  // and it's not a capturing drawtarget. A capturing DT might start using the
  // the Path object on a different thread (OMTP), and we might have a data race
  // if we keep a handle to it.
  bool cacheable = aDrawTarget->GetBackendType() ==
                   gfxPlatform::GetPlatform()->GetDefaultContentBackend();

  if (cacheable && mCachedPath && mCachedPath->GetFillRule() == aFillRule &&
      aDrawTarget->GetBackendType() == mCachedPath->GetBackendType()) {
    RefPtr<Path> path(mCachedPath);
    return path.forget();
  }
  RefPtr<PathBuilder> builder = aDrawTarget->CreatePathBuilder(aFillRule);
  RefPtr<Path> path = BuildPath(builder);
  if (cacheable) {
    mCachedPath = path;
  }
  return path.forget();
}

already_AddRefed<Path> SVGGeometryElement::GetTransformedPath(
    const Matrix& aPathTransform) {
  FillRule fillRule = GetFillRule();
  RefPtr<DrawTarget> tmpDT =
      gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget();

  RefPtr<Path> path = GetOrBuildPath(tmpDT, fillRule);

  if (path && !aPathTransform.IsIdentity()) {
    RefPtr<PathBuilder> builder =
        path->TransformedCopyToBuilder(aPathTransform, fillRule);
    path = builder->Finish();
  }
  return path.forget();
}

Maybe<Rect> SVGGeometryElement::GetBounds(const Matrix& aPathTransform) {
  if (RefPtr<Path> path = GetTransformedPath(aPathTransform)) {
    Rect bbox = path->GetBounds();
    if (bbox.IsFinite()) {
      return Some(bbox);
    }
  }
  return Nothing();
}

Maybe<Rect> SVGGeometryElement::GetStrokedBounds(
    const StrokeOptions& aStrokeOptions, const Matrix& aPathTransform,
    const Matrix& aPathToBounds) {
  if (RefPtr<Path> path = GetTransformedPath(aPathTransform)) {
    Rect bbox = path->GetStrokedBounds(aStrokeOptions, aPathToBounds);
    if (bbox.IsFinite()) {
      return Some(bbox);
    }
  }
  return Nothing();
}

already_AddRefed<Path> SVGGeometryElement::GetOrBuildPathForMeasuring() {
  RefPtr<DrawTarget> drawTarget =
      gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget();
  return GetOrBuildPath(drawTarget, GetFillRule());
}

// This helper is currently identical to GetOrBuildPathForMeasuring.
// We keep it a separate method because for length measuring purpose,
// fillRule isn't really needed. Derived class (e.g. SVGPathElement)
// may override GetOrBuildPathForMeasuring() to ignore fillRule. And
// GetOrBuildPathForMeasuring() itself may be modified in the future.
already_AddRefed<Path> SVGGeometryElement::GetOrBuildPathForHitTest() {
  RefPtr<DrawTarget> drawTarget =
      gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget();
  return GetOrBuildPath(drawTarget, GetFillRule());
}

bool SVGGeometryElement::IsGeometryChangedViaCSS(
    ComputedStyle const& aNewStyle, ComputedStyle const& aOldStyle) const {
  nsAtom* name = NodeInfo()->NameAtom();
  if (name == nsGkAtoms::rect) {
    return SVGRectElement::IsLengthChangedViaCSS(aNewStyle, aOldStyle);
  }
  if (name == nsGkAtoms::circle) {
    return SVGCircleElement::IsLengthChangedViaCSS(aNewStyle, aOldStyle);
  }
  if (name == nsGkAtoms::ellipse) {
    return SVGEllipseElement::IsLengthChangedViaCSS(aNewStyle, aOldStyle);
  }
  if (name == nsGkAtoms::path) {
    return SVGPathElement::IsDPropertyChangedViaCSS(aNewStyle, aOldStyle);
  }
  return false;
}

FillRule SVGGeometryElement::GetFillRule() {
  if (mCachedPath) {
    return mCachedPath->GetFillRule();
  }

  FillRule fillRule =
      FillRule::FILL_WINDING;  // Equivalent to StyleFillRule::Nonzero

  bool res = SVGGeometryProperty::DoForComputedStyle(
      this, [&](const ComputedStyle* s) {
        const auto* styleSVG = s->StyleSVG();

        MOZ_ASSERT(styleSVG->mFillRule == StyleFillRule::Nonzero ||
                   styleSVG->mFillRule == StyleFillRule::Evenodd);

        if (styleSVG->mFillRule == StyleFillRule::Evenodd) {
          fillRule = FillRule::FILL_EVEN_ODD;
        }
      });

  if (!res) {
    NS_WARNING("Couldn't get ComputedStyle for content in GetFillRule");
  }

  return fillRule;
}

bool SVGGeometryElement::IsPointInFill(const DOMPointInit& aPoint) {
  FlushIfNeeded();

  RefPtr<Path> path = GetOrBuildPathForHitTest();
  if (!path) {
    return false;
  }

  auto point =
      DOMPointReadOnly::ToPoint(aPoint) * dom::UserSpaceMetrics::GetZoom(this);
  return path->ContainsPoint(point, {});
}

bool SVGGeometryElement::IsPointInStroke(const DOMPointInit& aPoint) {
  // stroke-* attributes and the d attribute are presentation attributes, so we
  // flush the layout before building the path.
  (void)GetPrimaryFrame(FlushType::Layout);

  RefPtr<Path> path = GetOrBuildPathForHitTest();
  if (!path) {
    return false;
  }

  auto point =
      DOMPointReadOnly::ToPoint(aPoint) * dom::UserSpaceMetrics::GetZoom(this);
  bool res = false;
  SVGGeometryProperty::DoForComputedStyle(this, [&](const ComputedStyle* s) {
    // Per spec, we should take vector-effect into account.
    if (s->StyleSVGReset()->HasNonScalingStroke()) {
      auto mat = SVGContentUtils::GetNonScalingStrokeCTM(this);
      if (mat.HasNonTranslation()) {
        // We have non-scaling-stroke as well as a non-translation transform.
        // We should transform the path first then apply the stroke on the
        // transformed path to preserve the stroke-width.
        Path::Transform(path, mat);
        point = mat.TransformPoint(point);
      }
    }

    SVGContentUtils::AutoStrokeOptions strokeOptions;
    SVGContentUtils::GetStrokeOptions(&strokeOptions, this, s, nullptr);

    res = path->StrokeContainsPoint(strokeOptions, point, {});
  });

  return res;
}

float SVGGeometryElement::GetTotalLengthForBinding() {
  FlushIfNeeded();
  return GetTotalLength() / dom::UserSpaceMetrics::GetZoom(this);
}

already_AddRefed<DOMSVGPoint> SVGGeometryElement::GetPointAtLength(
    float distance, ErrorResult& rv) {
  FlushIfNeeded();

  RefPtr<Path> path = GetOrBuildPathForMeasuring();
  if (!path) {
    rv.ThrowInvalidStateError("No path available for measuring");
    return nullptr;
  }
  float zoom = dom::UserSpaceMetrics::GetZoom(this);
  gfx::Point point = path->ComputePointAtLength(
      std::clamp(distance * zoom, 0.f, path->ComputeLength()));

  return MakeAndAddRef<DOMSVGPoint>(point / zoom);
}

gfx::Matrix SVGGeometryElement::LocalTransform() const {
  nsIFrame* f = GetPrimaryFrame();
  if (!f || !f->IsTransformed()) {
    return {};
  }
  return gfx::Matrix(SVGUtils::GetTransformMatrixInUserSpace(f));
}

float SVGGeometryElement::GetPathLengthScale(PathLengthScaleUsageType aFor) {
  MOZ_ASSERT(aFor == PathLengthScaleUsageType::TextPath ||
                 aFor == PathLengthScaleUsageType::Stroking,
             "Unknown enum");
  if (mPathLength.IsExplicitlySet()) {
    float zoom = UserSpaceMetrics::GetZoom(this);
    float authorsPathLengthEstimate = mPathLength.GetAnimValue() * zoom;
    if (std::isfinite(authorsPathLengthEstimate) &&
        authorsPathLengthEstimate >= 0) {
      RefPtr<Path> path = GetOrBuildPathForMeasuring();
      if (!path) {
        // The path is empty or invalid so its length must be zero and
        // we know that 0 / authorsPathLengthEstimate = 0.
        return 0.0;
      }
      if (aFor == PathLengthScaleUsageType::TextPath) {
        // For textPath, a transform on the referenced path affects the
        // textPath layout, so when calculating the actual path length
        // we need to take that into account.
        auto matrix = LocalTransform();
        if (!matrix.IsIdentity()) {
          Path::Transform(path, matrix);
        }
      }
      return path->ComputeLength() / authorsPathLengthEstimate;
    }
  }
  return 1.0;
}

already_AddRefed<DOMSVGAnimatedNumber> SVGGeometryElement::PathLength() {
  return mPathLength.ToDOMAnimatedNumber(this);
}

float SVGGeometryElement::GetTotalLength() {
  RefPtr<Path> flat = GetOrBuildPathForMeasuring();
  return flat ? flat->ComputeLength() : 0.f;
}

void SVGGeometryElement::FlushIfNeeded() {
  FlushType flushType =
      GeometryDependsOnCoordCtx() ? FlushType::Layout : FlushType::Style;
  (void)GetPrimaryFrame(flushType);
}

}  // namespace mozilla::dom
