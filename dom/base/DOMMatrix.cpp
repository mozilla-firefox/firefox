/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/DOMMatrix.h"

#include <cmath>
#include <cstdint>
#include <new>

#include "ErrorList.h"
#include "js/Conversions.h"
#include "js/Equality.h"
#include "js/StructuredClone.h"
#include "js/Value.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ServoCSSParser.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/DOMMatrixBinding.h"
#include "mozilla/dom/DOMPoint.h"
#include "mozilla/dom/DOMPointBinding.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/gfx/BasePoint.h"
#include "mozilla/gfx/BasePoint4D.h"
#include "mozilla/gfx/Point.h"
#include "nsIGlobalObject.h"
#include "nsPIDOMWindow.h"
#include "nsString.h"
#include "nsStringFlags.h"
#include "nsTArray.h"
#include "nsTLiteralString.h"

namespace mozilla::dom {

template <typename T>
static void SetDataInMatrix(DOMMatrixReadOnly* aMatrix, const T* aData,
                            int aLength, ErrorResult& aRv);

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(DOMMatrixReadOnly, mParent)

JSObject* DOMMatrixReadOnly::WrapObject(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return DOMMatrixReadOnly_Binding::Wrap(aCx, this, aGivenProto);
}

// https://drafts.fxtf.org/geometry/#matrix-validate-and-fixup-2d
static bool ValidateAndFixupMatrix2DInit(DOMMatrix2DInit& aMatrixInit,
                                         ErrorResult& aRv) {
#define ValidateAliases(field, alias, fieldName, aliasName)             \
  if ((field).WasPassed() && (alias).WasPassed() &&                     \
      !JS::SameValueZero((field).Value(), (alias).Value())) {           \
    aRv.ThrowTypeError<MSG_MATRIX_INIT_CONFLICTING_VALUE>((fieldName),  \
                                                          (aliasName)); \
    return false;                                                       \
  }
#define SetFromAliasOrDefault(field, alias, defaultValue) \
  if (!(field).WasPassed()) {                             \
    if ((alias).WasPassed()) {                            \
      (field).Construct((alias).Value());                 \
    } else {                                              \
      (field).Construct(defaultValue);                    \
    }                                                     \
  }
#define ValidateAndSet(field, alias, fieldName, aliasName, defaultValue) \
  ValidateAliases((field), (alias), fieldName, aliasName);               \
  SetFromAliasOrDefault((field), (alias), (defaultValue));

  ValidateAndSet(aMatrixInit.mM11, aMatrixInit.mA, "m11", "a", 1);
  ValidateAndSet(aMatrixInit.mM12, aMatrixInit.mB, "m12", "b", 0);
  ValidateAndSet(aMatrixInit.mM21, aMatrixInit.mC, "m21", "c", 0);
  ValidateAndSet(aMatrixInit.mM22, aMatrixInit.mD, "m22", "d", 1);
  ValidateAndSet(aMatrixInit.mM41, aMatrixInit.mE, "m41", "e", 0);
  ValidateAndSet(aMatrixInit.mM42, aMatrixInit.mF, "m42", "f", 0);

  return true;

#undef ValidateAliases
#undef SetFromAliasOrDefault
#undef ValidateAndSet
}

// https://drafts.fxtf.org/geometry/#matrix-validate-and-fixup
static bool ValidateAndFixupMatrixInit(DOMMatrixInit& aMatrixInit,
                                       ErrorResult& aRv) {
#define Check3DField(field, fieldName, defaultValue)             \
  if ((field) != (defaultValue)) {                               \
    if (!aMatrixInit.mIs2D.WasPassed()) {                        \
      aMatrixInit.mIs2D.Construct(false);                        \
      return true;                                               \
    }                                                            \
    if (aMatrixInit.mIs2D.Value()) {                             \
      aRv.ThrowTypeError<MSG_MATRIX_INIT_EXCEEDS_2D>(fieldName); \
      return false;                                              \
    }                                                            \
  }

  if (!ValidateAndFixupMatrix2DInit(aMatrixInit, aRv)) {
    return false;
  }

  Check3DField(aMatrixInit.mM13, "m13", 0);
  Check3DField(aMatrixInit.mM14, "m14", 0);
  Check3DField(aMatrixInit.mM23, "m23", 0);
  Check3DField(aMatrixInit.mM24, "m24", 0);
  Check3DField(aMatrixInit.mM31, "m31", 0);
  Check3DField(aMatrixInit.mM32, "m32", 0);
  Check3DField(aMatrixInit.mM34, "m34", 0);
  Check3DField(aMatrixInit.mM43, "m43", 0);
  Check3DField(aMatrixInit.mM33, "m33", 1);
  Check3DField(aMatrixInit.mM44, "m44", 1);

  if (!aMatrixInit.mIs2D.WasPassed()) {
    aMatrixInit.mIs2D.Construct(true);
  }
  return true;

#undef Check3DField
}

void DOMMatrixReadOnly::SetDataFromMatrix2DInit(
    const DOMMatrix2DInit& aMatrixInit) {
  MOZ_ASSERT(Is2D());
  *mMatrix2D = ToMatrixDouble(aMatrixInit);
}

gfx::MatrixDouble DOMMatrixReadOnly::ToMatrixDouble(
    const DOMMatrix2DInit& aMatrixInit) {
  gfx::MatrixDouble matrix{};
  matrix._11 = aMatrixInit.mM11.Value();
  matrix._12 = aMatrixInit.mM12.Value();
  matrix._21 = aMatrixInit.mM21.Value();
  matrix._22 = aMatrixInit.mM22.Value();
  matrix._31 = aMatrixInit.mM41.Value();
  matrix._32 = aMatrixInit.mM42.Value();
  return matrix;
}

void DOMMatrixReadOnly::SetDataFromMatrixInit(
    const DOMMatrixInit& aMatrixInit) {
  const bool is2D = aMatrixInit.mIs2D.Value();
  MOZ_ASSERT(is2D == Is2D());
  if (is2D) {
    SetDataFromMatrix2DInit(aMatrixInit);
  } else {
    mMatrix3D->_11 = aMatrixInit.mM11.Value();
    mMatrix3D->_12 = aMatrixInit.mM12.Value();
    mMatrix3D->_13 = aMatrixInit.mM13;
    mMatrix3D->_14 = aMatrixInit.mM14;
    mMatrix3D->_21 = aMatrixInit.mM21.Value();
    mMatrix3D->_22 = aMatrixInit.mM22.Value();
    mMatrix3D->_23 = aMatrixInit.mM23;
    mMatrix3D->_24 = aMatrixInit.mM24;
    mMatrix3D->_31 = aMatrixInit.mM31;
    mMatrix3D->_32 = aMatrixInit.mM32;
    mMatrix3D->_33 = aMatrixInit.mM33;
    mMatrix3D->_34 = aMatrixInit.mM34;
    mMatrix3D->_41 = aMatrixInit.mM41.Value();
    mMatrix3D->_42 = aMatrixInit.mM42.Value();
    mMatrix3D->_43 = aMatrixInit.mM43;
    mMatrix3D->_44 = aMatrixInit.mM44;
  }
}

already_AddRefed<DOMMatrixReadOnly> DOMMatrixReadOnly::FromMatrix(
    nsISupports* aParent, const DOMMatrix2DInit& aMatrixInit,
    ErrorResult& aRv) {
  DOMMatrix2DInit matrixInit(aMatrixInit);
  if (!ValidateAndFixupMatrix2DInit(matrixInit, aRv)) {
    return nullptr;
  };

  RefPtr<DOMMatrixReadOnly> matrix =
      new DOMMatrixReadOnly(aParent, /* is2D */ true);
  matrix->SetDataFromMatrix2DInit(matrixInit);
  return matrix.forget();
}

gfx::MatrixDouble DOMMatrixReadOnly::ToValidatedMatrixDouble(
    const DOMMatrix2DInit& aMatrixInit, ErrorResult& aRv) {
  DOMMatrix2DInit matrixInit(aMatrixInit);
  if (!ValidateAndFixupMatrix2DInit(matrixInit, aRv)) {
    return gfx::MatrixDouble{};
  };

  return ToMatrixDouble(matrixInit);
}

already_AddRefed<DOMMatrixReadOnly> DOMMatrixReadOnly::FromMatrix(
    nsISupports* aParent, const DOMMatrixInit& aMatrixInit, ErrorResult& aRv) {
  DOMMatrixInit matrixInit(aMatrixInit);
  if (!ValidateAndFixupMatrixInit(matrixInit, aRv)) {
    return nullptr;
  };

  RefPtr<DOMMatrixReadOnly> rval =
      new DOMMatrixReadOnly(aParent, matrixInit.mIs2D.Value());
  rval->SetDataFromMatrixInit(matrixInit);
  return rval.forget();
}

already_AddRefed<DOMMatrixReadOnly> DOMMatrixReadOnly::FromMatrix(
    const GlobalObject& aGlobal, const DOMMatrixInit& aMatrixInit,
    ErrorResult& aRv) {
  RefPtr<DOMMatrixReadOnly> matrix =
      FromMatrix(aGlobal.GetAsSupports(), aMatrixInit, aRv);
  return matrix.forget();
}

already_AddRefed<DOMMatrixReadOnly> DOMMatrixReadOnly::FromFloat32Array(
    const GlobalObject& aGlobal, const Float32Array& aArray32,
    ErrorResult& aRv) {
  nsCOMPtr<nsISupports> global = aGlobal.GetAsSupports();
  return aArray32.ProcessData(
      [&](const Span<float>& aData, JS::AutoCheckCannotGC&& nogc) {
        const int length = aData.Length();
        const bool is2D = length == 6;
        RefPtr<DOMMatrixReadOnly> obj;
        {
          JS::AutoSuppressGCAnalysis suppress;
          obj = new DOMMatrixReadOnly(global.forget(), is2D);
        }
        SetDataInMatrix(obj, aData.Elements(), length, aRv);
        nogc.reset();  // Done with aData
        return obj.forget();
      });
}

already_AddRefed<DOMMatrixReadOnly> DOMMatrixReadOnly::FromFloat64Array(
    const GlobalObject& aGlobal, const Float64Array& aArray64,
    ErrorResult& aRv) {
  nsCOMPtr<nsISupports> global = aGlobal.GetAsSupports();
  return aArray64.ProcessData(
      [&](const Span<double>& aData, JS::AutoCheckCannotGC&& nogc) {
        const int length = aData.Length();
        const bool is2D = length == 6;
        RefPtr<DOMMatrixReadOnly> obj;
        {
          JS::AutoSuppressGCAnalysis suppress;
          obj = new DOMMatrixReadOnly(global.forget(), is2D);
        }
        SetDataInMatrix(obj, aData.Elements(), length, aRv);
        nogc.reset();  // Done with aData
        return obj.forget();
      });
}

already_AddRefed<DOMMatrixReadOnly> DOMMatrixReadOnly::Constructor(
    const GlobalObject& aGlobal,
    const Optional<UTF8StringOrUnrestrictedDoubleSequenceOrDOMMatrixReadOnly>&
        aArg,
    ErrorResult& aRv) {
  if (!aArg.WasPassed()) {
    RefPtr<DOMMatrixReadOnly> rval =
        new DOMMatrixReadOnly(aGlobal.GetAsSupports());
    return rval.forget();
  }

  const auto& arg = aArg.Value();
  if (arg.IsUTF8String()) {
    nsCOMPtr<nsPIDOMWindowInner> win =
        do_QueryInterface(aGlobal.GetAsSupports());
    if (!win) {
      aRv.ThrowTypeError<MSG_ILLEGAL_CONSTRUCTOR>();
      return nullptr;
    }
    RefPtr<DOMMatrixReadOnly> rval =
        new DOMMatrixReadOnly(aGlobal.GetAsSupports());
    rval->SetMatrixValue(arg.GetAsUTF8String(), aRv);
    return rval.forget();
  }
  if (arg.IsDOMMatrixReadOnly()) {
    RefPtr<DOMMatrixReadOnly> obj = new DOMMatrixReadOnly(
        aGlobal.GetAsSupports(), arg.GetAsDOMMatrixReadOnly());
    return obj.forget();
  }

  const auto& sequence = arg.GetAsUnrestrictedDoubleSequence();
  const int length = sequence.Length();
  const bool is2D = length == 6;
  RefPtr<DOMMatrixReadOnly> rval =
      new DOMMatrixReadOnly(aGlobal.GetAsSupports(), is2D);
  SetDataInMatrix(rval, sequence.Elements(), length, aRv);
  return rval.forget();
}

already_AddRefed<DOMMatrixReadOnly> DOMMatrixReadOnly::ReadStructuredClone(
    JSContext* aCx, nsIGlobalObject* aGlobal,
    JSStructuredCloneReader* aReader) {
  uint8_t is2D;

  if (!JS_ReadBytes(aReader, &is2D, 1)) {
    return nullptr;
  }

  RefPtr<DOMMatrixReadOnly> rval = new DOMMatrixReadOnly(aGlobal, is2D);

  if (!ReadStructuredCloneElements(aReader, rval)) {
    return nullptr;
  };

  return rval.forget();
}

already_AddRefed<DOMMatrix> DOMMatrixReadOnly::Translate(double aTx, double aTy,
                                                         double aTz) const {
  RefPtr<DOMMatrix> retval = new DOMMatrix(mParent, *this);
  retval->TranslateSelf(aTx, aTy, aTz);

  return retval.forget();
}

already_AddRefed<DOMMatrix> DOMMatrixReadOnly::Scale(
    double aScaleX, const Optional<double>& aScaleY, double aScaleZ,
    double aOriginX, double aOriginY, double aOriginZ) const {
  RefPtr<DOMMatrix> retval = new DOMMatrix(mParent, *this);
  retval->ScaleSelf(aScaleX, aScaleY, aScaleZ, aOriginX, aOriginY, aOriginZ);

  return retval.forget();
}

already_AddRefed<DOMMatrix> DOMMatrixReadOnly::Scale3d(double aScale,
                                                       double aOriginX,
                                                       double aOriginY,
                                                       double aOriginZ) const {
  RefPtr<DOMMatrix> retval = new DOMMatrix(mParent, *this);
  retval->Scale3dSelf(aScale, aOriginX, aOriginY, aOriginZ);

  return retval.forget();
}

already_AddRefed<DOMMatrix> DOMMatrixReadOnly::ScaleNonUniform(
    double aScaleX, double aScaleY) const {
  RefPtr<DOMMatrix> retval = new DOMMatrix(mParent, *this);
  retval->ScaleSelf(aScaleX, Optional<double>(aScaleY), 1, 0, 0, 0);

  return retval.forget();
}

already_AddRefed<DOMMatrix> DOMMatrixReadOnly::Rotate(
    double aRotX, const Optional<double>& aRotY,
    const Optional<double>& aRotZ) const {
  RefPtr<DOMMatrix> retval = new DOMMatrix(mParent, *this);
  retval->RotateSelf(aRotX, aRotY, aRotZ);

  return retval.forget();
}

already_AddRefed<DOMMatrix> DOMMatrixReadOnly::RotateFromVector(
    double x, double y) const {
  RefPtr<DOMMatrix> retval = new DOMMatrix(mParent, *this);
  retval->RotateFromVectorSelf(x, y);

  return retval.forget();
}

already_AddRefed<DOMMatrix> DOMMatrixReadOnly::RotateAxisAngle(
    double aX, double aY, double aZ, double aAngle) const {
  RefPtr<DOMMatrix> retval = new DOMMatrix(mParent, *this);
  retval->RotateAxisAngleSelf(aX, aY, aZ, aAngle);

  return retval.forget();
}

already_AddRefed<DOMMatrix> DOMMatrixReadOnly::SkewX(double aSx) const {
  RefPtr<DOMMatrix> retval = new DOMMatrix(mParent, *this);
  retval->SkewXSelf(aSx);

  return retval.forget();
}

already_AddRefed<DOMMatrix> DOMMatrixReadOnly::SkewY(double aSy) const {
  RefPtr<DOMMatrix> retval = new DOMMatrix(mParent, *this);
  retval->SkewYSelf(aSy);

  return retval.forget();
}

already_AddRefed<DOMMatrix> DOMMatrixReadOnly::Multiply(
    const DOMMatrixInit& other, ErrorResult& aRv) const {
  RefPtr<DOMMatrix> retval = new DOMMatrix(mParent, *this);
  retval->MultiplySelf(other, aRv);

  return retval.forget();
}

already_AddRefed<DOMMatrix> DOMMatrixReadOnly::FlipX() const {
  RefPtr<DOMMatrix> retval = new DOMMatrix(mParent, *this);
  if (mMatrix3D) {
    gfx::Matrix4x4Double m;
    m._11 = -1;
    retval->mMatrix3D = MakeUnique<gfx::Matrix4x4Double>(m * *mMatrix3D);
  } else {
    gfx::MatrixDouble m;
    m._11 = -1;
    retval->mMatrix2D =
        MakeUnique<gfx::MatrixDouble>(mMatrix2D ? m * *mMatrix2D : m);
  }

  return retval.forget();
}

already_AddRefed<DOMMatrix> DOMMatrixReadOnly::FlipY() const {
  RefPtr<DOMMatrix> retval = new DOMMatrix(mParent, *this);
  if (mMatrix3D) {
    gfx::Matrix4x4Double m;
    m._22 = -1;
    retval->mMatrix3D = MakeUnique<gfx::Matrix4x4Double>(m * *mMatrix3D);
  } else {
    gfx::MatrixDouble m;
    m._22 = -1;
    retval->mMatrix2D =
        MakeUnique<gfx::MatrixDouble>(mMatrix2D ? m * *mMatrix2D : m);
  }

  return retval.forget();
}

already_AddRefed<DOMMatrix> DOMMatrixReadOnly::Inverse() const {
  RefPtr<DOMMatrix> retval = new DOMMatrix(mParent, *this);
  retval->InvertSelf();

  return retval.forget();
}

bool DOMMatrixReadOnly::Is2D() const { return !mMatrix3D; }

bool DOMMatrixReadOnly::IsIdentity() const {
  if (mMatrix3D) {
    return mMatrix3D->IsIdentity();
  }

  return mMatrix2D->IsIdentity();
}

already_AddRefed<DOMPoint> DOMMatrixReadOnly::TransformPoint(
    const DOMPointInit& point) const {
  RefPtr<DOMPoint> retval = new DOMPoint(mParent);

  if (mMatrix3D) {
    gfx::Point4D transformedPoint;
    transformedPoint.x = point.mX;
    transformedPoint.y = point.mY;
    transformedPoint.z = point.mZ;
    transformedPoint.w = point.mW;

    transformedPoint = mMatrix3D->TransformPoint(transformedPoint);

    retval->SetX(transformedPoint.x);
    retval->SetY(transformedPoint.y);
    retval->SetZ(transformedPoint.z);
    retval->SetW(transformedPoint.w);
  } else if (point.mZ != 0 || point.mW != 1.0) {
    gfx::Matrix4x4Double tempMatrix(gfx::Matrix4x4Double::From2D(*mMatrix2D));

    gfx::PointDouble4D transformedPoint;
    transformedPoint.x = point.mX;
    transformedPoint.y = point.mY;
    transformedPoint.z = point.mZ;
    transformedPoint.w = point.mW;

    transformedPoint = tempMatrix.TransformPoint(transformedPoint);

    retval->SetX(transformedPoint.x);
    retval->SetY(transformedPoint.y);
    retval->SetZ(transformedPoint.z);
    retval->SetW(transformedPoint.w);
  } else {
    gfx::PointDouble transformedPoint;
    transformedPoint.x = point.mX;
    transformedPoint.y = point.mY;

    transformedPoint = mMatrix2D->TransformPoint(transformedPoint);

    retval->SetX(transformedPoint.x);
    retval->SetY(transformedPoint.y);
    retval->SetZ(point.mZ);
    retval->SetW(point.mW);
  }
  return retval.forget();
}

template <typename T>
void GetDataFromMatrix(const DOMMatrixReadOnly* aMatrix, T* aData) {
  aData[0] = static_cast<T>(aMatrix->M11());
  aData[1] = static_cast<T>(aMatrix->M12());
  aData[2] = static_cast<T>(aMatrix->M13());
  aData[3] = static_cast<T>(aMatrix->M14());
  aData[4] = static_cast<T>(aMatrix->M21());
  aData[5] = static_cast<T>(aMatrix->M22());
  aData[6] = static_cast<T>(aMatrix->M23());
  aData[7] = static_cast<T>(aMatrix->M24());
  aData[8] = static_cast<T>(aMatrix->M31());
  aData[9] = static_cast<T>(aMatrix->M32());
  aData[10] = static_cast<T>(aMatrix->M33());
  aData[11] = static_cast<T>(aMatrix->M34());
  aData[12] = static_cast<T>(aMatrix->M41());
  aData[13] = static_cast<T>(aMatrix->M42());
  aData[14] = static_cast<T>(aMatrix->M43());
  aData[15] = static_cast<T>(aMatrix->M44());
}

void DOMMatrixReadOnly::ToFloat32Array(JSContext* aCx,
                                       JS::MutableHandle<JSObject*> aResult,
                                       ErrorResult& aRv) const {
  AutoTArray<float, 16> arr;
  arr.SetLength(16);
  GetDataFromMatrix(this, arr.Elements());
  JS::Rooted<JS::Value> value(aCx);
  if (!ToJSValue(aCx, TypedArrayCreator<Float32Array>(arr), &value)) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }
  aResult.set(&value.toObject());
}

void DOMMatrixReadOnly::ToFloat64Array(JSContext* aCx,
                                       JS::MutableHandle<JSObject*> aResult,
                                       ErrorResult& aRv) const {
  AutoTArray<double, 16> arr;
  arr.SetLength(16);
  GetDataFromMatrix(this, arr.Elements());
  JS::Rooted<JS::Value> value(aCx);
  if (!ToJSValue(aCx, TypedArrayCreator<Float64Array>(arr), &value)) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }
  aResult.set(&value.toObject());
}

void DOMMatrixReadOnly::Stringify(nsAString& aResult, ErrorResult& aRv) {
  char cbuf[JS::MaximumNumberToStringLength];
  nsAutoString matrixStr;
  auto AppendDouble = [&aRv, &cbuf, &matrixStr](double d,
                                                bool isLastItem = false) {
    if (!std::isfinite(d)) {
      aRv.ThrowInvalidStateError(
          "Matrix with a non-finite element cannot be stringified.");
      return false;
    }
    JS::NumberToString(d, cbuf);
    matrixStr.AppendASCII(cbuf);
    if (!isLastItem) {
      matrixStr.AppendLiteral(", ");
    }
    return true;
  };

  if (mMatrix3D) {
    // We can't use AppendPrintf here, because it does locale-specific
    // formatting of floating-point values.
    matrixStr.AssignLiteral("matrix3d(");
    if (!AppendDouble(M11()) || !AppendDouble(M12()) || !AppendDouble(M13()) ||
        !AppendDouble(M14()) || !AppendDouble(M21()) || !AppendDouble(M22()) ||
        !AppendDouble(M23()) || !AppendDouble(M24()) || !AppendDouble(M31()) ||
        !AppendDouble(M32()) || !AppendDouble(M33()) || !AppendDouble(M34()) ||
        !AppendDouble(M41()) || !AppendDouble(M42()) || !AppendDouble(M43()) ||
        !AppendDouble(M44(), true)) {
      return;
    }
    matrixStr.AppendLiteral(")");
  } else {
    // We can't use AppendPrintf here, because it does locale-specific
    // formatting of floating-point values.
    matrixStr.AssignLiteral("matrix(");
    if (!AppendDouble(A()) || !AppendDouble(B()) || !AppendDouble(C()) ||
        !AppendDouble(D()) || !AppendDouble(E()) || !AppendDouble(F(), true)) {
      return;
    }
    matrixStr.AppendLiteral(")");
  }

  aResult = std::move(matrixStr);
}

// https://drafts.fxtf.org/geometry/#structured-serialization
bool DOMMatrixReadOnly::WriteStructuredClone(
    JSContext* aCx, JSStructuredCloneWriter* aWriter) const {
  const uint8_t is2D = Is2D();

  if (!JS_WriteBytes(aWriter, &is2D, 1)) {
    return false;
  }

  if (is2D == 1) {
    return JS_WriteDouble(aWriter, mMatrix2D->_11) &&
           JS_WriteDouble(aWriter, mMatrix2D->_12) &&
           JS_WriteDouble(aWriter, mMatrix2D->_21) &&
           JS_WriteDouble(aWriter, mMatrix2D->_22) &&
           JS_WriteDouble(aWriter, mMatrix2D->_31) &&
           JS_WriteDouble(aWriter, mMatrix2D->_32);
  }

  return JS_WriteDouble(aWriter, mMatrix3D->_11) &&
         JS_WriteDouble(aWriter, mMatrix3D->_12) &&
         JS_WriteDouble(aWriter, mMatrix3D->_13) &&
         JS_WriteDouble(aWriter, mMatrix3D->_14) &&
         JS_WriteDouble(aWriter, mMatrix3D->_21) &&
         JS_WriteDouble(aWriter, mMatrix3D->_22) &&
         JS_WriteDouble(aWriter, mMatrix3D->_23) &&
         JS_WriteDouble(aWriter, mMatrix3D->_24) &&
         JS_WriteDouble(aWriter, mMatrix3D->_31) &&
         JS_WriteDouble(aWriter, mMatrix3D->_32) &&
         JS_WriteDouble(aWriter, mMatrix3D->_33) &&
         JS_WriteDouble(aWriter, mMatrix3D->_34) &&
         JS_WriteDouble(aWriter, mMatrix3D->_41) &&
         JS_WriteDouble(aWriter, mMatrix3D->_42) &&
         JS_WriteDouble(aWriter, mMatrix3D->_43) &&
         JS_WriteDouble(aWriter, mMatrix3D->_44);
}

bool DOMMatrixReadOnly::ReadStructuredCloneElements(
    JSStructuredCloneReader* aReader, DOMMatrixReadOnly* matrix) {
  if (matrix->Is2D() == 1) {
    JS_ReadDouble(aReader, &(matrix->mMatrix2D->_11));
    JS_ReadDouble(aReader, &(matrix->mMatrix2D->_12));
    JS_ReadDouble(aReader, &(matrix->mMatrix2D->_21));
    JS_ReadDouble(aReader, &(matrix->mMatrix2D->_22));
    JS_ReadDouble(aReader, &(matrix->mMatrix2D->_31));
    JS_ReadDouble(aReader, &(matrix->mMatrix2D->_32));
  } else {
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_11));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_12));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_13));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_14));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_21));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_22));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_23));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_24));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_31));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_32));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_33));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_34));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_41));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_42));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_43));
    JS_ReadDouble(aReader, &(matrix->mMatrix3D->_44));
  }

  return true;
}

already_AddRefed<DOMMatrix> DOMMatrix::FromMatrix(
    nsISupports* aParent, const DOMMatrixInit& aMatrixInit, ErrorResult& aRv) {
  DOMMatrixInit matrixInit(aMatrixInit);
  if (!ValidateAndFixupMatrixInit(matrixInit, aRv)) {
    return nullptr;
  };

  RefPtr<DOMMatrix> matrix = new DOMMatrix(aParent, matrixInit.mIs2D.Value());
  matrix->SetDataFromMatrixInit(matrixInit);
  return matrix.forget();
}

already_AddRefed<DOMMatrix> DOMMatrix::FromMatrix(
    const GlobalObject& aGlobal, const DOMMatrixInit& aMatrixInit,
    ErrorResult& aRv) {
  RefPtr<DOMMatrix> matrix =
      FromMatrix(aGlobal.GetAsSupports(), aMatrixInit, aRv);
  return matrix.forget();
}

already_AddRefed<DOMMatrix> DOMMatrix::FromFloat32Array(
    const GlobalObject& aGlobal, const Float32Array& aArray32,
    ErrorResult& aRv) {
  nsCOMPtr<nsISupports> global = aGlobal.GetAsSupports();
  return aArray32.ProcessData(
      [&](const Span<float>& aData, JS::AutoCheckCannotGC&& nogc) {
        const int length = aData.Length();
        const bool is2D = length == 6;
        RefPtr<DOMMatrix> obj;
        {
          JS::AutoSuppressGCAnalysis suppress;
          obj = new DOMMatrix(global.forget(), is2D);
        }
        SetDataInMatrix(obj, aData.Elements(), length, aRv);
        nogc.reset();  // Done with aData
        return obj.forget();
      });
}

already_AddRefed<DOMMatrix> DOMMatrix::FromFloat64Array(
    const GlobalObject& aGlobal, const Float64Array& aArray64,
    ErrorResult& aRv) {
  nsCOMPtr<nsISupports> global = aGlobal.GetAsSupports();
  return aArray64.ProcessData(
      [&](const Span<double>& aData, JS::AutoCheckCannotGC&& nogc) {
        const int length = aData.Length();
        const bool is2D = length == 6;
        RefPtr<DOMMatrix> obj;
        {
          JS::AutoSuppressGCAnalysis suppress;
          obj = new DOMMatrix(global.forget(), is2D);
        }
        SetDataInMatrix(obj, aData.Elements(), length, aRv);
        nogc.reset();  // Done with aData
        return obj.forget();
      });
}

already_AddRefed<DOMMatrix> DOMMatrix::Constructor(
    const GlobalObject& aGlobal,
    const Optional<UTF8StringOrUnrestrictedDoubleSequenceOrDOMMatrixReadOnly>&
        aArg,
    ErrorResult& aRv) {
  if (!aArg.WasPassed()) {
    RefPtr<DOMMatrix> rval = new DOMMatrix(aGlobal.GetAsSupports());
    return rval.forget();
  }

  const auto& arg = aArg.Value();
  if (arg.IsUTF8String()) {
    nsCOMPtr<nsPIDOMWindowInner> win =
        do_QueryInterface(aGlobal.GetAsSupports());
    if (!win) {
      aRv.ThrowTypeError<MSG_ILLEGAL_CONSTRUCTOR>();
      return nullptr;
    }
    RefPtr<DOMMatrix> rval = new DOMMatrix(aGlobal.GetAsSupports());
    rval->SetMatrixValue(arg.GetAsUTF8String(), aRv);
    return rval.forget();
  }
  if (arg.IsDOMMatrixReadOnly()) {
    RefPtr<DOMMatrix> obj =
        new DOMMatrix(aGlobal.GetAsSupports(), arg.GetAsDOMMatrixReadOnly());
    return obj.forget();
  }

  const auto& sequence = arg.GetAsUnrestrictedDoubleSequence();
  const int length = sequence.Length();
  const bool is2D = length == 6;
  RefPtr<DOMMatrix> rval = new DOMMatrix(aGlobal.GetAsSupports(), is2D);
  SetDataInMatrix(rval, sequence.Elements(), length, aRv);
  return rval.forget();
}

template <typename T>
static void SetDataInMatrix(DOMMatrixReadOnly* aMatrix, const T* aData,
                            int aLength, ErrorResult& aRv) {
  if (aLength == 16) {
    aMatrix->SetM11(aData[0]);
    aMatrix->SetM12(aData[1]);
    aMatrix->SetM13(aData[2]);
    aMatrix->SetM14(aData[3]);
    aMatrix->SetM21(aData[4]);
    aMatrix->SetM22(aData[5]);
    aMatrix->SetM23(aData[6]);
    aMatrix->SetM24(aData[7]);
    aMatrix->SetM31(aData[8]);
    aMatrix->SetM32(aData[9]);
    aMatrix->SetM33(aData[10]);
    aMatrix->SetM34(aData[11]);
    aMatrix->SetM41(aData[12]);
    aMatrix->SetM42(aData[13]);
    aMatrix->SetM43(aData[14]);
    aMatrix->SetM44(aData[15]);
  } else if (aLength == 6) {
    aMatrix->SetA(aData[0]);
    aMatrix->SetB(aData[1]);
    aMatrix->SetC(aData[2]);
    aMatrix->SetD(aData[3]);
    aMatrix->SetE(aData[4]);
    aMatrix->SetF(aData[5]);
  } else {
    nsAutoCString lengthStr;
    lengthStr.AppendInt(aLength);
    aRv.ThrowTypeError<MSG_MATRIX_INIT_LENGTH_WRONG>(lengthStr);
  }
}

already_AddRefed<DOMMatrix> DOMMatrix::ReadStructuredClone(
    JSContext* aCx, nsIGlobalObject* aGlobal,
    JSStructuredCloneReader* aReader) {
  uint8_t is2D;

  if (!JS_ReadBytes(aReader, &is2D, 1)) {
    return nullptr;
  }

  RefPtr<DOMMatrix> rval = new DOMMatrix(aGlobal, is2D);

  if (!ReadStructuredCloneElements(aReader, rval)) {
    return nullptr;
  };

  return rval.forget();
}

void DOMMatrixReadOnly::Ensure3DMatrix() {
  if (!mMatrix3D) {
    mMatrix3D = MakeUnique<gfx::Matrix4x4Double>(
        gfx::Matrix4x4Double::From2D(*mMatrix2D));
    mMatrix2D = nullptr;
  }
}

DOMMatrix* DOMMatrix::MultiplySelf(const DOMMatrixInit& aOtherInit,
                                   ErrorResult& aRv) {
  RefPtr<DOMMatrix> other = FromMatrix(mParent, aOtherInit, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  MOZ_ASSERT(other);
  if (other->IsIdentity()) {
    return this;
  }

  if (other->Is2D()) {
    if (mMatrix3D) {
      *mMatrix3D = gfx::Matrix4x4Double::From2D(*other->mMatrix2D) * *mMatrix3D;
    } else {
      *mMatrix2D = *other->mMatrix2D * *mMatrix2D;
    }
  } else {
    Ensure3DMatrix();
    *mMatrix3D = *other->mMatrix3D * *mMatrix3D;
  }

  return this;
}

DOMMatrix* DOMMatrix::PreMultiplySelf(const DOMMatrixInit& aOtherInit,
                                      ErrorResult& aRv) {
  RefPtr<DOMMatrix> other = FromMatrix(mParent, aOtherInit, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  MOZ_ASSERT(other);
  if (other->IsIdentity()) {
    return this;
  }

  if (other->Is2D()) {
    if (mMatrix3D) {
      *mMatrix3D = *mMatrix3D * gfx::Matrix4x4Double::From2D(*other->mMatrix2D);
    } else {
      *mMatrix2D = *mMatrix2D * *other->mMatrix2D;
    }
  } else {
    Ensure3DMatrix();
    *mMatrix3D = *mMatrix3D * *other->mMatrix3D;
  }

  return this;
}

DOMMatrix* DOMMatrix::TranslateSelf(double aTx, double aTy, double aTz) {
  if (aTx == 0 && aTy == 0 && aTz == 0) {
    return this;
  }

  if (mMatrix3D || aTz != 0) {
    Ensure3DMatrix();
    mMatrix3D->PreTranslate(aTx, aTy, aTz);
  } else {
    mMatrix2D->PreTranslate(aTx, aTy);
  }

  return this;
}

DOMMatrix* DOMMatrix::ScaleSelf(double aScaleX, const Optional<double>& aScaleY,
                                double aScaleZ, double aOriginX,
                                double aOriginY, double aOriginZ) {
  const double scaleY = aScaleY.WasPassed() ? aScaleY.Value() : aScaleX;

  TranslateSelf(aOriginX, aOriginY, aOriginZ);

  if (mMatrix3D || aScaleZ != 1.0) {
    Ensure3DMatrix();
    gfx::Matrix4x4Double m;
    m._11 = aScaleX;
    m._22 = scaleY;
    m._33 = aScaleZ;
    *mMatrix3D = m * *mMatrix3D;
  } else {
    gfx::MatrixDouble m;
    m._11 = aScaleX;
    m._22 = scaleY;
    *mMatrix2D = m * *mMatrix2D;
  }

  TranslateSelf(-aOriginX, -aOriginY, -aOriginZ);

  return this;
}

DOMMatrix* DOMMatrix::Scale3dSelf(double aScale, double aOriginX,
                                  double aOriginY, double aOriginZ) {
  ScaleSelf(aScale, Optional<double>(aScale), aScale, aOriginX, aOriginY,
            aOriginZ);

  return this;
}

DOMMatrix* DOMMatrix::RotateFromVectorSelf(double aX, double aY) {
  const double angle = (aX == 0.0 && aY == 0.0) ? 0 : atan2(aY, aX);

  if (fmod(angle, 2 * M_PI) == 0) {
    return this;
  }

  if (mMatrix3D) {
    RotateAxisAngleSelf(0, 0, 1, angle / kRadPerDegree);
  } else {
    *mMatrix2D = mMatrix2D->PreRotate(angle);
  }

  return this;
}

DOMMatrix* DOMMatrix::RotateSelf(double aRotX, const Optional<double>& aRotY,
                                 const Optional<double>& aRotZ) {
  double rotY;
  double rotZ;
  if (!aRotY.WasPassed() && !aRotZ.WasPassed()) {
    rotZ = aRotX;
    aRotX = 0;
    rotY = 0;
  } else {
    rotY = aRotY.WasPassed() ? aRotY.Value() : 0;
    rotZ = aRotZ.WasPassed() ? aRotZ.Value() : 0;
  }

  if (aRotX != 0 || rotY != 0) {
    Ensure3DMatrix();
  }

  if (mMatrix3D) {
    if (fmod(rotZ, 360) != 0) {
      mMatrix3D->RotateZ(rotZ * kRadPerDegree);
    }
    if (fmod(rotY, 360) != 0) {
      mMatrix3D->RotateY(rotY * kRadPerDegree);
    }
    if (fmod(aRotX, 360) != 0) {
      mMatrix3D->RotateX(aRotX * kRadPerDegree);
    }
  } else if (fmod(rotZ, 360) != 0) {
    *mMatrix2D = mMatrix2D->PreRotate(rotZ * kRadPerDegree);
  }

  return this;
}

// https://drafts.fxtf.org/geometry/#dom-dommatrix-rotateaxisangleself
DOMMatrix* DOMMatrix::RotateAxisAngleSelf(double aX, double aY, double aZ,
                                          double aAngle) {
  // (Unspecified but rather obvious optimization)
  if (fmod(aAngle, 360) == 0) {
    return this;
  }

  aAngle *= kRadPerDegree;

  // Step 1: Post-multiply a rotation transformation on the current matrix
  // around the specified vector x, y, z by the specified rotation angle in
  // degrees.
  // (But actual multiplication happens below with step 2)
  gfx::Matrix4x4Double m;
  m.SetRotateAxisAngle(aX, aY, aZ, aAngle);

  // Step 2: If x or y are not 0 or -0, set is 2D of the current matrix to
  // false.
  // (But we don't have "is 2D" flag and need to multiply either 2D or 3D
  // matrix)
  if (mMatrix3D || aX != 0 || aY != 0) {
    Ensure3DMatrix();
    *mMatrix3D = m * *mMatrix3D;
    return this;
  }

  gfx::Matrix4x4Double result = m * gfx::Matrix4x4Double::From2D(*mMatrix2D);
  mMatrix2D->_11 = result._11;
  mMatrix2D->_12 = result._12;
  mMatrix2D->_21 = result._21;
  mMatrix2D->_22 = result._22;

  // Different field names, see ::From2D implementation
  mMatrix2D->_31 = result._41;
  mMatrix2D->_32 = result._42;

  return this;
}

DOMMatrix* DOMMatrix::SkewXSelf(double aSx) {
  if (fmod(aSx, 360) == 0) {
    return this;
  }

  if (mMatrix3D) {
    gfx::Matrix4x4Double m;
    m._21 = tan(aSx * kRadPerDegree);
    *mMatrix3D = m * *mMatrix3D;
  } else {
    gfx::MatrixDouble m;
    m._21 = tan(aSx * kRadPerDegree);
    *mMatrix2D = m * *mMatrix2D;
  }

  return this;
}

DOMMatrix* DOMMatrix::SkewYSelf(double aSy) {
  if (fmod(aSy, 360) == 0) {
    return this;
  }

  if (mMatrix3D) {
    gfx::Matrix4x4Double m;
    m._12 = tan(aSy * kRadPerDegree);
    *mMatrix3D = m * *mMatrix3D;
  } else {
    gfx::MatrixDouble m;
    m._12 = tan(aSy * kRadPerDegree);
    *mMatrix2D = m * *mMatrix2D;
  }

  return this;
}

DOMMatrix* DOMMatrix::InvertSelf() {
  if (mMatrix3D) {
    if (!mMatrix3D->Invert()) {
      mMatrix3D->SetNAN();
    }
  } else if (!mMatrix2D->Invert()) {
    mMatrix2D = nullptr;

    mMatrix3D = MakeUnique<gfx::Matrix4x4Double>();
    mMatrix3D->SetNAN();
  }

  return this;
}

DOMMatrixReadOnly* DOMMatrixReadOnly::SetMatrixValue(
    const nsACString& aTransformList, ErrorResult& aRv) {
  // An empty string is a no-op.
  if (aTransformList.IsEmpty()) {
    return this;
  }

  gfx::Matrix4x4 transform;
  bool contains3dTransform = false;
  if (!ServoCSSParser::ParseTransformIntoMatrix(
          aTransformList, contains3dTransform, transform)) {
    aRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
    return nullptr;
  }

  if (!contains3dTransform) {
    mMatrix3D = nullptr;
    if (!mMatrix2D) {
      mMatrix2D = MakeUnique<gfx::MatrixDouble>();
    }

    SetA(transform._11);
    SetB(transform._12);
    SetC(transform._21);
    SetD(transform._22);
    SetE(transform._41);
    SetF(transform._42);
  } else {
    mMatrix3D = MakeUnique<gfx::Matrix4x4Double>(transform);
    mMatrix2D = nullptr;
  }

  return this;
}

DOMMatrix* DOMMatrix::SetMatrixValue(const nsACString& aTransformList,
                                     ErrorResult& aRv) {
  DOMMatrixReadOnly::SetMatrixValue(aTransformList, aRv);
  return this;
}

JSObject* DOMMatrix::WrapObject(JSContext* aCx,
                                JS::Handle<JSObject*> aGivenProto) {
  return DOMMatrix_Binding::Wrap(aCx, this, aGivenProto);
}

}  // namespace mozilla::dom
