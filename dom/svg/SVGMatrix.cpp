/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGMatrix.h"

#include <math.h>

#include "mozilla/dom/DOMMatrix.h"
#include "mozilla/dom/SVGMatrixBinding.h"
#include "nsError.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(SVGMatrix, mTransform)

DOMSVGTransform* SVGMatrix::GetParentObject() const { return mTransform; }

JSObject* SVGMatrix::WrapObject(JSContext* aCx,
                                JS::Handle<JSObject*> aGivenProto) {
  return SVGMatrix_Binding::Wrap(aCx, this, aGivenProto);
}

void SVGMatrix::SetA(float aA, ErrorResult& aRv) {
  if (IsAnimVal()) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }

  gfxMatrix mx = GetMatrix();
  mx._11 = aA;
  SetMatrix(mx);
}

void SVGMatrix::SetB(float aB, ErrorResult& aRv) {
  if (IsAnimVal()) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }

  gfxMatrix mx = GetMatrix();
  mx._12 = aB;
  SetMatrix(mx);
}

void SVGMatrix::SetC(float aC, ErrorResult& aRv) {
  if (IsAnimVal()) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }

  gfxMatrix mx = GetMatrix();
  mx._21 = aC;
  SetMatrix(mx);
}

void SVGMatrix::SetD(float aD, ErrorResult& aRv) {
  if (IsAnimVal()) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }

  gfxMatrix mx = GetMatrix();
  mx._22 = aD;
  SetMatrix(mx);
}

void SVGMatrix::SetE(float aE, ErrorResult& aRv) {
  if (IsAnimVal()) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }

  gfxMatrix mx = GetMatrix();
  mx._31 = aE;
  SetMatrix(mx);
}

void SVGMatrix::SetF(float aF, ErrorResult& aRv) {
  if (IsAnimVal()) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }

  gfxMatrix mx = GetMatrix();
  mx._32 = aF;
  SetMatrix(mx);
}

already_AddRefed<SVGMatrix> SVGMatrix::Multiply(const DOMMatrix2DInit& aMatrix,
                                                ErrorResult& aRv) {
  auto matrix2D = DOMMatrixReadOnly::ToValidatedMatrixDouble(aMatrix, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  if (!matrix2D.IsFinite()) {
    aRv.ThrowTypeError<MSG_NOT_FINITE>("SVGMatrix::Multiply matrix");
    return nullptr;
  }
  return MakeAndAddRef<SVGMatrix>(matrix2D * GetMatrix());
}

already_AddRefed<SVGMatrix> SVGMatrix::Inverse(ErrorResult& aRv) {
  gfxMatrix mat = GetMatrix();
  if (!mat.Invert()) {
    aRv.ThrowInvalidStateError("Matrix is not invertible");
    return nullptr;
  }
  return MakeAndAddRef<SVGMatrix>(mat);
}

already_AddRefed<SVGMatrix> SVGMatrix::Translate(float x, float y) {
  return MakeAndAddRef<SVGMatrix>(
      gfxMatrix(GetMatrix()).PreTranslate(gfxPoint(x, y)));
}

already_AddRefed<SVGMatrix> SVGMatrix::Scale(float scaleFactor) {
  return ScaleNonUniform(scaleFactor, scaleFactor);
}

already_AddRefed<SVGMatrix> SVGMatrix::ScaleNonUniform(float scaleFactorX,
                                                       float scaleFactorY) {
  return MakeAndAddRef<SVGMatrix>(
      gfxMatrix(GetMatrix()).PreScale(scaleFactorX, scaleFactorY));
}

already_AddRefed<SVGMatrix> SVGMatrix::Rotate(float angle) {
  return MakeAndAddRef<SVGMatrix>(
      gfxMatrix(GetMatrix()).PreRotate(angle * kRadPerDegree));
}

already_AddRefed<SVGMatrix> SVGMatrix::RotateFromVector(float x, float y,
                                                        ErrorResult& aRv) {
  if (x == 0.0 || y == 0.0) {
    aRv.ThrowInvalidAccessError("Neither input parameter may be zero");
    return nullptr;
  }

  return MakeAndAddRef<SVGMatrix>(
      gfxMatrix(GetMatrix()).PreRotate(atan2(y, x)));
}

already_AddRefed<SVGMatrix> SVGMatrix::FlipX() {
  const gfxMatrix& mx = GetMatrix();
  return MakeAndAddRef<SVGMatrix>(
      gfxMatrix(-mx._11, -mx._12, mx._21, mx._22, mx._31, mx._32));
}

already_AddRefed<SVGMatrix> SVGMatrix::FlipY() {
  const gfxMatrix& mx = GetMatrix();
  return MakeAndAddRef<SVGMatrix>(
      gfxMatrix(mx._11, mx._12, -mx._21, -mx._22, mx._31, mx._32));
}

already_AddRefed<SVGMatrix> SVGMatrix::SkewX(float angle, ErrorResult& aRv) {
  double ta = tan(angle * kRadPerDegree);
  if (!std::isfinite(ta)) {
    aRv.ThrowInvalidAccessError("Invalid angle");
    return nullptr;
  }

  const gfxMatrix& mx = GetMatrix();
  gfxMatrix skewMx(mx._11, mx._12, mx._21 + mx._11 * ta, mx._22 + mx._12 * ta,
                   mx._31, mx._32);
  return MakeAndAddRef<SVGMatrix>(skewMx);
}

already_AddRefed<SVGMatrix> SVGMatrix::SkewY(float angle, ErrorResult& aRv) {
  double ta = tan(angle * kRadPerDegree);
  if (!std::isfinite(ta)) {
    aRv.ThrowInvalidAccessError("Invalid angle");
    return nullptr;
  }

  const gfxMatrix& mx = GetMatrix();
  gfxMatrix skewMx(mx._11 + mx._21 * ta, mx._12 + mx._22 * ta, mx._21, mx._22,
                   mx._31, mx._32);

  return MakeAndAddRef<SVGMatrix>(skewMx);
}

}  // namespace mozilla::dom
