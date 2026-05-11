/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SVGTransform.h"

#include "nsContentUtils.h"  // for NS_ENSURE_FINITE
#include "nsError.h"
#include "nsTextFormatter.h"

namespace mozilla {

using namespace dom::SVGTransform_Binding;

void SVGTransform::GetValueAsString(nsAString& aValue) const {
  switch (mType) {
    case SVG_TRANSFORM_TRANSLATE:
      // The spec say that if Y is not provided, it is assumed to be zero.
      if (mMatrix._32 != 0)
        nsTextFormatter::ssprintf(aValue, u"translate(%g, %g)", mMatrix._31,
                                  mMatrix._32);
      else
        nsTextFormatter::ssprintf(aValue, u"translate(%g)", mMatrix._31);
      break;
    case SVG_TRANSFORM_ROTATE:
      if (mOriginX != 0.0f || mOriginY != 0.0f)
        nsTextFormatter::ssprintf(aValue, u"rotate(%g, %g, %g)", mAngle,
                                  mOriginX, mOriginY);
      else
        nsTextFormatter::ssprintf(aValue, u"rotate(%g)", mAngle);
      break;
    case SVG_TRANSFORM_SCALE:
      if (mMatrix._11 != mMatrix._22)
        nsTextFormatter::ssprintf(aValue, u"scale(%g, %g)", mMatrix._11,
                                  mMatrix._22);
      else
        nsTextFormatter::ssprintf(aValue, u"scale(%g)", mMatrix._11);
      break;
    case SVG_TRANSFORM_SKEWX:
      nsTextFormatter::ssprintf(aValue, u"skewX(%g)", mAngle);
      break;
    case SVG_TRANSFORM_SKEWY:
      nsTextFormatter::ssprintf(aValue, u"skewY(%g)", mAngle);
      break;
    case SVG_TRANSFORM_MATRIX:
      nsTextFormatter::ssprintf(aValue, u"matrix(%g, %g, %g, %g, %g, %g)",
                                mMatrix._11, mMatrix._12, mMatrix._21,
                                mMatrix._22, mMatrix._31, mMatrix._32);
      break;
    default:
      aValue.Truncate();
      NS_ERROR("unknown transformation type");
      break;
  }
}

void SVGTransform::SetMatrix(const gfxMatrix& aMatrix) {
  mType = SVG_TRANSFORM_MATRIX;
  mMatrix = aMatrix;
  // We set the other members here too, since operator== requires it and
  // the DOM requires it for mAngle.
  mAngle = 0.f;
  mOriginX = 0.f;
  mOriginY = 0.f;
}

void SVGTransform::SetTranslate(float aTx, float aTy) {
  mType = SVG_TRANSFORM_TRANSLATE;
  mMatrix = gfxMatrix::Translation(aTx, aTy);
  mAngle = 0.f;
  mOriginX = 0.f;
  mOriginY = 0.f;
}

void SVGTransform::SetScale(float aSx, float aSy) {
  mType = SVG_TRANSFORM_SCALE;
  mMatrix = gfxMatrix::Scaling(aSx, aSy);
  mAngle = 0.f;
  mOriginX = 0.f;
  mOriginY = 0.f;
}

void SVGTransform::SetRotate(float aAngle, float aCx, float aCy) {
  mType = SVG_TRANSFORM_ROTATE;
  mMatrix = gfxMatrix::Translation(aCx, aCy)
                .PreRotate(aAngle * kRadPerDegree)
                .PreTranslate(-aCx, -aCy);
  mAngle = aAngle;
  mOriginX = aCx;
  mOriginY = aCy;
}

nsresult SVGTransform::SetSkewX(float aAngle) {
  double ta = tan(aAngle * kRadPerDegree);
  // No one actually cares about the exact error return type here.
  NS_ENSURE_FINITE(ta, NS_ERROR_INVALID_ARG);

  mType = SVG_TRANSFORM_SKEWX;
  mMatrix = gfxMatrix();
  mMatrix._21 = ta;
  mAngle = aAngle;
  mOriginX = 0.f;
  mOriginY = 0.f;
  return NS_OK;
}

nsresult SVGTransform::SetSkewY(float aAngle) {
  double ta = tan(aAngle * kRadPerDegree);
  // No one actually cares about the exact error return type here.
  NS_ENSURE_FINITE(ta, NS_ERROR_INVALID_ARG);

  mType = SVG_TRANSFORM_SKEWY;
  mMatrix = gfxMatrix();
  mMatrix._12 = ta;
  mAngle = aAngle;
  mOriginX = 0.f;
  mOriginY = 0.f;
  return NS_OK;
}

SVGTransformSMILData::SVGTransformSMILData(const SVGTransform& aTransform)
    : mTransformType(aTransform.Type()) {
  MOZ_ASSERT(mTransformType >= SVG_TRANSFORM_MATRIX &&
                 mTransformType <= SVG_TRANSFORM_SKEWY,
             "Unexpected transform type");

  for (float& mParam : mParams) {
    mParam = 0.f;
  }

  switch (mTransformType) {
    case SVG_TRANSFORM_MATRIX: {
      const gfxMatrix& mx = aTransform.GetMatrix();
      mParams[0] = static_cast<float>(mx._11);
      mParams[1] = static_cast<float>(mx._12);
      mParams[2] = static_cast<float>(mx._21);
      mParams[3] = static_cast<float>(mx._22);
      mParams[4] = static_cast<float>(mx._31);
      mParams[5] = static_cast<float>(mx._32);
      break;
    }
    case SVG_TRANSFORM_TRANSLATE: {
      const gfxMatrix& mx = aTransform.GetMatrix();
      mParams[0] = static_cast<float>(mx._31);
      mParams[1] = static_cast<float>(mx._32);
      break;
    }
    case SVG_TRANSFORM_SCALE: {
      const gfxMatrix& mx = aTransform.GetMatrix();
      mParams[0] = static_cast<float>(mx._11);
      mParams[1] = static_cast<float>(mx._22);
      break;
    }
    case SVG_TRANSFORM_ROTATE:
      mParams[0] = aTransform.Angle();
      aTransform.GetRotationOrigin(mParams[1], mParams[2]);
      break;

    case SVG_TRANSFORM_SKEWX:
    case SVG_TRANSFORM_SKEWY:
      mParams[0] = aTransform.Angle();
      break;

    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected transform type");
      break;
  }
}

SVGTransform SVGTransformSMILData::ToSVGTransform() const {
  SVGTransform result;

  switch (mTransformType) {
    case SVG_TRANSFORM_MATRIX:
      result.SetMatrix(gfxMatrix(mParams[0], mParams[1], mParams[2], mParams[3],
                                 mParams[4], mParams[5]));
      break;

    case SVG_TRANSFORM_TRANSLATE:
      result.SetTranslate(mParams[0], mParams[1]);
      break;

    case SVG_TRANSFORM_SCALE:
      result.SetScale(mParams[0], mParams[1]);
      break;

    case SVG_TRANSFORM_ROTATE:
      result.SetRotate(mParams[0], mParams[1], mParams[2]);
      break;

    case SVG_TRANSFORM_SKEWX:
      result.SetSkewX(mParams[0]);
      break;

    case SVG_TRANSFORM_SKEWY:
      result.SetSkewY(mParams[0]);
      break;

    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected transform type");
      break;
  }
  return result;
}

}  // namespace mozilla
