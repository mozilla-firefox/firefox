/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_MATRIX_H_
#define MOZILLA_GFX_MATRIX_H_

#include "Types.h"
#include "Triangle.h"
#include "Rect.h"
#include "Point.h"
#include "Quaternion.h"
#include <iosfwd>
#include <math.h>
#include "mozilla/Attributes.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/gfx/ScaleFactors2D.h"
#include "mozilla/Span.h"

namespace mozilla {

constexpr double kRadPerDegree = 2.0 * M_PI / 360.0;

namespace gfx {

static inline bool FuzzyEqual(Float aV1, Float aV2) {
  // XXX - Check if fabs does the smart thing and just negates the sign bit.
  return fabs(aV2 - aV1) < 1e-6;
}

template <typename F>
Span<Point4DTyped<UnknownUnits, F>> IntersectPolygon(
    Span<Point4DTyped<UnknownUnits, F>> aPoints,
    const Point4DTyped<UnknownUnits, F>& aPlaneNormal,
    Span<Point4DTyped<UnknownUnits, F>> aDestBuffer);

template <class T>
using BaseMatrixScales = BaseScaleFactors2D<UnknownUnits, UnknownUnits, T>;

using MatrixScales = BaseMatrixScales<float>;
using MatrixScalesDouble = BaseMatrixScales<double>;

template <class T>
class BaseMatrix {
  // Alias that maps to either Point or PointDouble depending on whether T is a
  // float or a double.
  typedef PointTyped<UnknownUnits, T> MatrixPoint;
  // Same for size and rect
  typedef SizeTyped<UnknownUnits, T> MatrixSize;
  typedef RectTyped<UnknownUnits, T> MatrixRect;

 public:
  BaseMatrix() : _11(1.0f), _12(0), _21(0), _22(1.0f), _31(0), _32(0) {}
  BaseMatrix(T a11, T a12, T a21, T a22, T a31, T a32)
      : _11(a11), _12(a12), _21(a21), _22(a22), _31(a31), _32(a32) {}
  union {
    struct {
      T _11, _12;
      T _21, _22;
      T _31, _32;
    };
    T components[6];
  };

  template <class T2>
  explicit BaseMatrix(const BaseMatrix<T2>& aOther)
      : _11(aOther._11),
        _12(aOther._12),
        _21(aOther._21),
        _22(aOther._22),
        _31(aOther._31),
        _32(aOther._32) {}

  MOZ_ALWAYS_INLINE BaseMatrix Copy() const { return BaseMatrix<T>(*this); }

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const BaseMatrix& aMatrix) {
    if (aMatrix.IsIdentity()) {
      return aStream << "[ I ]";
    }
    return aStream << "[ " << aMatrix._11 << " " << aMatrix._12 << "; "
                   << aMatrix._21 << " " << aMatrix._22 << "; " << aMatrix._31
                   << " " << aMatrix._32 << "; ]";
  }

  MatrixPoint TransformPoint(const MatrixPoint& aPoint) const {
    MatrixPoint retPoint;

    retPoint.x = aPoint.x * _11 + aPoint.y * _21 + _31;
    retPoint.y = aPoint.x * _12 + aPoint.y * _22 + _32;

    return retPoint;
  }

  MatrixSize TransformSize(const MatrixSize& aSize) const {
    MatrixSize retSize;

    retSize.width = aSize.width * _11 + aSize.height * _21;
    retSize.height = aSize.width * _12 + aSize.height * _22;

    return retSize;
  }

  /**
   * In most cases you probably want to use TransformBounds. This function
   * just transforms the top-left and size separately and constructs a rect
   * from those results.
   */
  MatrixRect TransformRect(const MatrixRect& aRect) const {
    return MatrixRect(TransformPoint(aRect.TopLeft()),
                      TransformSize(aRect.Size()));
  }

  GFX2D_API MatrixRect TransformBounds(const MatrixRect& aRect) const {
    int i;
    MatrixPoint quad[4];
    T min_x, max_x;
    T min_y, max_y;

    quad[0] = TransformPoint(aRect.TopLeft());
    quad[1] = TransformPoint(aRect.TopRight());
    quad[2] = TransformPoint(aRect.BottomLeft());
    quad[3] = TransformPoint(aRect.BottomRight());

    min_x = max_x = quad[0].x;
    min_y = max_y = quad[0].y;

    for (i = 1; i < 4; i++) {
      if (quad[i].x < min_x) min_x = quad[i].x;
      if (quad[i].x > max_x) max_x = quad[i].x;

      if (quad[i].y < min_y) min_y = quad[i].y;
      if (quad[i].y > max_y) max_y = quad[i].y;
    }

    return MatrixRect(min_x, min_y, max_x - min_x, max_y - min_y);
  }

  static BaseMatrix<T> Translation(T aX, T aY) {
    return BaseMatrix<T>(1.0f, 0.0f, 0.0f, 1.0f, aX, aY);
  }

  static BaseMatrix<T> Translation(MatrixPoint aPoint) {
    return Translation(aPoint.x, aPoint.y);
  }

  /**
   * Apply a translation to this matrix.
   *
   * The "Pre" in this method's name means that the translation is applied
   * -before- this matrix's existing transformation. That is, any vector that
   * is multiplied by the resulting matrix will first be translated, then be
   * transformed by the original transform.
   *
   * Calling this method will result in this matrix having the same value as
   * the result of:
   *
   *   BaseMatrix<T>::Translation(x, y) * this
   *
   * (Note that in performance critical code multiplying by the result of a
   * Translation()/Scaling() call is not recommended since that results in a
   * full matrix multiply involving 12 floating-point multiplications. Calling
   * this method would be preferred since it only involves four floating-point
   * multiplications.)
   */
  BaseMatrix<T>& PreTranslate(T aX, T aY) {
    _31 += _11 * aX + _21 * aY;
    _32 += _12 * aX + _22 * aY;

    return *this;
  }

  BaseMatrix<T>& PreTranslate(const MatrixPoint& aPoint) {
    return PreTranslate(aPoint.x, aPoint.y);
  }

  /**
   * Similar to PreTranslate, but the translation is applied -after- this
   * matrix's existing transformation instead of before it.
   *
   * This method is generally less used than PreTranslate since typically code
   * want to adjust an existing user space to device space matrix to create a
   * transform to device space from a -new- user space (translated from the
   * previous user space). In that case consumers will need to use the Pre*
   * variants of the matrix methods rather than using the Post* methods, since
   * the Post* methods add a transform to the device space end of the
   * transformation.
   */
  BaseMatrix<T>& PostTranslate(T aX, T aY) {
    _31 += aX;
    _32 += aY;
    return *this;
  }

  BaseMatrix<T>& PostTranslate(const MatrixPoint& aPoint) {
    return PostTranslate(aPoint.x, aPoint.y);
  }

  static BaseMatrix<T> Scaling(T aScaleX, T aScaleY) {
    return BaseMatrix<T>(aScaleX, 0.0f, 0.0f, aScaleY, 0.0f, 0.0f);
  }

  static BaseMatrix<T> Scaling(const BaseMatrixScales<T>& scale) {
    return Scaling(scale.xScale, scale.yScale);
  }

  /**
   * Similar to PreTranslate, but applies a scale instead of a translation.
   */
  BaseMatrix<T>& PreScale(T aX, T aY) {
    _11 *= aX;
    _12 *= aX;
    _21 *= aY;
    _22 *= aY;

    return *this;
  }

  BaseMatrix<T>& PreScale(const BaseMatrixScales<T>& scale) {
    return PreScale(scale.xScale, scale.yScale);
  }

  /**
   * Similar to PostTranslate, but applies a scale instead of a translation.
   */
  BaseMatrix<T>& PostScale(T aScaleX, T aScaleY) {
    _11 *= aScaleX;
    _12 *= aScaleY;
    _21 *= aScaleX;
    _22 *= aScaleY;
    _31 *= aScaleX;
    _32 *= aScaleY;

    return *this;
  }

  GFX2D_API static BaseMatrix<T> Rotation(T aAngle);

  /**
   * Similar to PreTranslate, but applies a rotation instead of a translation.
   */
  BaseMatrix<T>& PreRotate(T aAngle) {
    return *this = BaseMatrix<T>::Rotation(aAngle) * *this;
  }

  bool Invert() {
    // Compute co-factors.
    T A = _22;
    T B = -_21;
    T C = _21 * _32 - _22 * _31;
    T D = -_12;
    T E = _11;
    T F = _31 * _12 - _11 * _32;

    T det = Determinant();

    if (!det) {
      return false;
    }

    T inv_det = 1 / det;

    _11 = inv_det * A;
    _12 = inv_det * D;
    _21 = inv_det * B;
    _22 = inv_det * E;
    _31 = inv_det * C;
    _32 = inv_det * F;

    return true;
  }

  BaseMatrix<T> Inverse() const {
    BaseMatrix<T> clone = *this;
    DebugOnly<bool> inverted = clone.Invert();
    MOZ_ASSERT(inverted,
               "Attempted to get the inverse of a non-invertible matrix");
    return clone;
  }

  T Determinant() const { return _11 * _22 - _12 * _21; }

  BaseMatrix<T> operator*(const BaseMatrix<T>& aMatrix) const {
    BaseMatrix<T> resultMatrix;

    resultMatrix._11 = this->_11 * aMatrix._11 + this->_12 * aMatrix._21;
    resultMatrix._12 = this->_11 * aMatrix._12 + this->_12 * aMatrix._22;
    resultMatrix._21 = this->_21 * aMatrix._11 + this->_22 * aMatrix._21;
    resultMatrix._22 = this->_21 * aMatrix._12 + this->_22 * aMatrix._22;
    resultMatrix._31 =
        this->_31 * aMatrix._11 + this->_32 * aMatrix._21 + aMatrix._31;
    resultMatrix._32 =
        this->_31 * aMatrix._12 + this->_32 * aMatrix._22 + aMatrix._32;

    return resultMatrix;
  }

  BaseMatrix<T>& operator*=(const BaseMatrix<T>& aMatrix) {
    *this = *this * aMatrix;
    return *this;
  }

  /**
   * Multiplies *this with aMatrix and returns the result.
   */
  Matrix4x4 operator*(const Matrix4x4& aMatrix) const;

  /**
   * Multiplies in the opposite order to operator=*.
   */
  BaseMatrix<T>& PreMultiply(const BaseMatrix<T>& aMatrix) {
    *this = aMatrix * *this;
    return *this;
  }

  /**
   * Please explicitly use either FuzzyEquals or ExactlyEquals.
   */
  bool operator==(const BaseMatrix<T>& other) const = delete;
  bool operator!=(const BaseMatrix<T>& other) const = delete;

  /* Returns true if the other matrix is fuzzy-equal to this matrix.
   * Note that this isn't a cheap comparison!
   */
  bool FuzzyEquals(const BaseMatrix<T>& o) const {
    return FuzzyEqual(_11, o._11) && FuzzyEqual(_12, o._12) &&
           FuzzyEqual(_21, o._21) && FuzzyEqual(_22, o._22) &&
           FuzzyEqual(_31, o._31) && FuzzyEqual(_32, o._32);
  }

  bool ExactlyEquals(const BaseMatrix<T>& o) const {
    return _11 == o._11 && _12 == o._12 && _21 == o._21 && _22 == o._22 &&
           _31 == o._31 && _32 == o._32;
  }

  /* Verifies that the matrix contains no Infs or NaNs. */
  bool IsFinite() const {
    return std::isfinite(_11) && std::isfinite(_12) && std::isfinite(_21) &&
           std::isfinite(_22) && std::isfinite(_31) && std::isfinite(_32);
  }

  /* Returns true if the matrix is a rectilinear transformation (i.e.
   * grid-aligned rectangles are transformed to grid-aligned rectangles)
   */
  bool IsRectilinear() const {
    if (FuzzyEqual(_12, 0) && FuzzyEqual(_21, 0)) {
      return true;
    } else if (FuzzyEqual(_22, 0) && FuzzyEqual(_11, 0)) {
      return true;
    }

    return false;
  }

  /**
   * Returns true if the matrix is anything other than a straight
   * translation by integers.
   */
  bool HasNonIntegerTranslation() const {
    return HasNonTranslation() || !FuzzyEqual(_31, floor(_31 + 0.5f)) ||
           !FuzzyEqual(_32, floor(_32 + 0.5f));
  }

  /**
   * Returns true if the matrix only has an integer translation.
   */
  bool HasOnlyIntegerTranslation() const { return !HasNonIntegerTranslation(); }

  /**
   * Returns true if the matrix has any transform other
   * than a straight translation.
   */
  bool HasNonTranslation() const {
    return !FuzzyEqual(_11, 1.0) || !FuzzyEqual(_22, 1.0) ||
           !FuzzyEqual(_12, 0.0) || !FuzzyEqual(_21, 0.0);
  }

  /**
   * Returns true if the matrix has any transform other
   * than a translation or a -1 y scale (y axis flip)
   */
  bool HasNonTranslationOrFlip() const {
    return !FuzzyEqual(_11, 1.0) ||
           (!FuzzyEqual(_22, 1.0) && !FuzzyEqual(_22, -1.0)) ||
           !FuzzyEqual(_21, 0.0) || !FuzzyEqual(_12, 0.0);
  }

  /* Returns true if the matrix is an identity matrix.
   */
  bool IsIdentity() const {
    return _11 == 1.0f && _12 == 0.0f && _21 == 0.0f && _22 == 1.0f &&
           _31 == 0.0f && _32 == 0.0f;
  }

  /* Returns true if the matrix is singular.
   */
  bool IsSingular() const {
    T det = Determinant();
    return !std::isfinite(det) || det == 0;
  }

  GFX2D_API BaseMatrix<T>& NudgeToIntegers() {
    NudgeToInteger(&_11);
    NudgeToInteger(&_12);
    NudgeToInteger(&_21);
    NudgeToInteger(&_22);
    NudgeToInteger(&_31);
    NudgeToInteger(&_32);
    return *this;
  }

  bool IsTranslation() const {
    return FuzzyEqual(_11, 1.0f) && FuzzyEqual(_12, 0.0f) &&
           FuzzyEqual(_21, 0.0f) && FuzzyEqual(_22, 1.0f);
  }

  static bool FuzzyIsInteger(T aValue) {
    return FuzzyEqual(aValue, floorf(aValue + 0.5f));
  }

  bool IsIntegerTranslation() const {
    return IsTranslation() && FuzzyIsInteger(_31) && FuzzyIsInteger(_32);
  }

  bool IsAllIntegers() const {
    return FuzzyIsInteger(_11) && FuzzyIsInteger(_12) && FuzzyIsInteger(_21) &&
           FuzzyIsInteger(_22) && FuzzyIsInteger(_31) && FuzzyIsInteger(_32);
  }

  MatrixPoint GetTranslation() const { return MatrixPoint(_31, _32); }

  /**
   * Returns true if matrix is multiple of 90 degrees rotation with flipping,
   * scaling and translation.
   */
  bool PreservesAxisAlignedRectangles() const {
    return ((FuzzyEqual(_11, 0.0) && FuzzyEqual(_22, 0.0)) ||
            (FuzzyEqual(_12, 0.0) && FuzzyEqual(_21, 0.0)));
  }

  /**
   * Returns true if the matrix has any transform other
   * than a translation or scale; this is, if there is
   * rotation.
   */
  bool HasNonAxisAlignedTransform() const {
    return !FuzzyEqual(_21, 0.0) || !FuzzyEqual(_12, 0.0);
  }

  /**
   * Returns true if the matrix has negative scaling (i.e. flip).
   */
  bool HasNegativeScaling() const { return (_11 < 0.0) || (_22 < 0.0); }

  /**
   * Computes the scale factors of this matrix; that is,
   * the amounts each basis vector is scaled by.
   */
  BaseMatrixScales<T> ScaleFactors() const {
    T det = Determinant();

    if (det == 0.0) {
      return BaseMatrixScales<T>(0.0, 0.0);
    }

    MatrixSize sz = MatrixSize(1.0, 0.0);
    sz = TransformSize(sz);

    T major = sqrt(sz.width * sz.width + sz.height * sz.height);
    T minor = 0.0;

    // ignore mirroring
    if (det < 0.0) {
      det = -det;
    }

    if (major) {
      minor = det / major;
    }

    return BaseMatrixScales<T>(major, minor);
  }

  /**
   * Returns true if the matrix preserves distances, i.e. a rigid transformation
   * that doesn't change size or shape). Such a matrix has uniform unit scaling
   * and an orthogonal basis.
   */
  bool PreservesDistance() const {
    return FuzzyEqual(_11 * _11 + _12 * _12, 1.0) &&
           FuzzyEqual(_21 * _21 + _22 * _22, 1.0) &&
           FuzzyEqual(_11 * _21 + _12 * _22, 0.0);
  }
};

typedef BaseMatrix<Float> Matrix;
typedef BaseMatrix<Double> MatrixDouble;

// Helper functions used by Matrix4x4Typed defined in Matrix.cpp
double SafeTangent(double aTheta);
double FlushToZero(double aVal);

template <class Units, class F>
Point4DTyped<Units, F> ComputePerspectivePlaneIntercept(
    const Point4DTyped<Units, F>& aFirst,
    const Point4DTyped<Units, F>& aSecond) {
  // This function will always return a point with a w value of 0.
  // The X, Y, and Z components will point towards an infinite vanishing
  // point.

  // We want to interpolate aFirst and aSecond to find the point intersecting
  // with the w=0 plane.

  // Since we know what we want the w component to be, we can rearrange the
  // interpolation equation and solve for t.
  float t = -aFirst.w / (aSecond.w - aFirst.w);

  // Use t to find the remainder of the components
  return aFirst + (aSecond - aFirst) * t;
}

template <class SourceUnits, class TargetUnits, class T>
class Matrix4x4Typed {
 public:
  typedef PointTyped<SourceUnits, T> SourcePoint;
  typedef PointTyped<TargetUnits, T> TargetPoint;
  typedef Point3DTyped<SourceUnits, T> SourcePoint3D;
  typedef Point3DTyped<TargetUnits, T> TargetPoint3D;
  typedef Point4DTyped<SourceUnits, T> SourcePoint4D;
  typedef Point4DTyped<TargetUnits, T> TargetPoint4D;
  typedef RectTyped<SourceUnits, T> SourceRect;
  typedef RectTyped<TargetUnits, T> TargetRect;

  Matrix4x4Typed()
      : _11(1.0f),
        _12(0.0f),
        _13(0.0f),
        _14(0.0f),
        _21(0.0f),
        _22(1.0f),
        _23(0.0f),
        _24(0.0f),
        _31(0.0f),
        _32(0.0f),
        _33(1.0f),
        _34(0.0f),
        _41(0.0f),
        _42(0.0f),
        _43(0.0f),
        _44(1.0f) {}

  Matrix4x4Typed(T a11, T a12, T a13, T a14, T a21, T a22, T a23, T a24, T a31,
                 T a32, T a33, T a34, T a41, T a42, T a43, T a44)
      : _11(a11),
        _12(a12),
        _13(a13),
        _14(a14),
        _21(a21),
        _22(a22),
        _23(a23),
        _24(a24),
        _31(a31),
        _32(a32),
        _33(a33),
        _34(a34),
        _41(a41),
        _42(a42),
        _43(a43),
        _44(a44) {}

  explicit Matrix4x4Typed(const T aArray[16]) {
    memcpy(components, aArray, sizeof(components));
  }

  Matrix4x4Typed(const Matrix4x4Typed& aOther) {
    memcpy(components, aOther.components, sizeof(components));
  }

  template <class T2>
  explicit Matrix4x4Typed(
      const Matrix4x4Typed<SourceUnits, TargetUnits, T2>& aOther)
      : _11(aOther._11),
        _12(aOther._12),
        _13(aOther._13),
        _14(aOther._14),
        _21(aOther._21),
        _22(aOther._22),
        _23(aOther._23),
        _24(aOther._24),
        _31(aOther._31),
        _32(aOther._32),
        _33(aOther._33),
        _34(aOther._34),
        _41(aOther._41),
        _42(aOther._42),
        _43(aOther._43),
        _44(aOther._44) {}

  union {
    struct {
      T _11, _12, _13, _14;
      T _21, _22, _23, _24;
      T _31, _32, _33, _34;
      T _41, _42, _43, _44;
    };
    T components[16];
  };

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const Matrix4x4Typed& aMatrix) {
    if (aMatrix.Is2D()) {
      BaseMatrix<T> matrix = aMatrix.As2D();
      return aStream << matrix;
    }
    const T* f = &aMatrix._11;
    aStream << "[ " << f[0] << ' ' << f[1] << ' ' << f[2] << ' ' << f[3] << ';';
    f += 4;
    aStream << ' ' << f[0] << ' ' << f[1] << ' ' << f[2] << ' ' << f[3] << ';';
    f += 4;
    aStream << ' ' << f[0] << ' ' << f[1] << ' ' << f[2] << ' ' << f[3] << ';';
    f += 4;
    aStream << ' ' << f[0] << ' ' << f[1] << ' ' << f[2] << ' ' << f[3]
            << "; ]";
    return aStream;
  }

  Point4DTyped<UnknownUnits, T>& operator[](int aIndex) {
    MOZ_ASSERT(aIndex >= 0 && aIndex <= 3, "Invalid matrix array index");
    return *reinterpret_cast<Point4DTyped<UnknownUnits, T>*>((&_11) +
                                                             4 * aIndex);
  }
  const Point4DTyped<UnknownUnits, T>& operator[](int aIndex) const {
    MOZ_ASSERT(aIndex >= 0 && aIndex <= 3, "Invalid matrix array index");
    return *reinterpret_cast<const Point4DTyped<UnknownUnits, T>*>((&_11) +
                                                                   4 * aIndex);
  }

  // External code should avoid calling this, and instead use
  // ViewAs() from UnitTransforms.h, which requires providing
  // a justification.
  template <typename NewMatrix4x4Typed>
  [[nodiscard]] NewMatrix4x4Typed Cast() const {
    return NewMatrix4x4Typed(_11, _12, _13, _14, _21, _22, _23, _24, _31, _32,
                             _33, _34, _41, _42, _43, _44);
  }

  /**
   * Returns true if the matrix is isomorphic to a 2D affine transformation.
   */
  bool Is2D() const {
    if (_13 != 0.0f || _14 != 0.0f || _23 != 0.0f || _24 != 0.0f ||
        _31 != 0.0f || _32 != 0.0f || _33 != 1.0f || _34 != 0.0f ||
        _43 != 0.0f || _44 != 1.0f) {
      return false;
    }
    return true;
  }

  bool Is2D(BaseMatrix<T>* aMatrix) const {
    if (!Is2D()) {
      return false;
    }
    if (aMatrix) {
      aMatrix->_11 = _11;
      aMatrix->_12 = _12;
      aMatrix->_21 = _21;
      aMatrix->_22 = _22;
      aMatrix->_31 = _41;
      aMatrix->_32 = _42;
    }
    return true;
  }

  BaseMatrix<T> As2D() const {
    MOZ_ASSERT(Is2D(), "Matrix is not a 2D affine transform");

    return BaseMatrix<T>(_11, _12, _21, _22, _41, _42);
  }

  bool CanDraw2D(BaseMatrix<T>* aMatrix = nullptr) const {
    if (_14 != 0.0f || _24 != 0.0f || _44 != 1.0f) {
      return false;
    }
    if (aMatrix) {
      aMatrix->_11 = _11;
      aMatrix->_12 = _12;
      aMatrix->_21 = _21;
      aMatrix->_22 = _22;
      aMatrix->_31 = _41;
      aMatrix->_32 = _42;
    }
    return true;
  }

  Matrix4x4Typed& ProjectTo2D() {
    _31 = 0.0f;
    _32 = 0.0f;
    _13 = 0.0f;
    _23 = 0.0f;
    _33 = 1.0f;
    _43 = 0.0f;
    _34 = 0.0f;
    // Some matrices, such as those derived from perspective transforms,
    // can modify _44 from 1, while leaving the rest of the fourth column
    // (_14, _24) at 0. In this case, after resetting the third row and
    // third column above, the value of _44 functions only to scale the
    // coordinate transform divide by W. The matrix can be converted to
    // a true 2D matrix by normalizing out the scaling effect of _44 on
    // the remaining components ahead of time.
    if (_14 == 0.0f && _24 == 0.0f && _44 != 1.0f && _44 != 0.0f) {
      T scale = 1.0f / _44;
      _11 *= scale;
      _12 *= scale;
      _21 *= scale;
      _22 *= scale;
      _41 *= scale;
      _42 *= scale;
      _44 = 1.0f;
    }
    return *this;
  }

  template <class F>
  Point4DTyped<TargetUnits, F> ProjectPoint(
      const PointTyped<SourceUnits, F>& aPoint) const {
    // Find a value for z that will transform to 0.

    // The transformed value of z is computed as:
    // z' = aPoint.x * _13 + aPoint.y * _23 + z * _33 + _43;

    // Solving for z when z' = 0 gives us:
    F z = -(aPoint.x * _13 + aPoint.y * _23 + _43) / _33;

    // Compute the transformed point
    return this->TransformPoint(
        Point4DTyped<SourceUnits, F>(aPoint.x, aPoint.y, z, 1));
  }

  template <class F>
  RectTyped<TargetUnits, F> ProjectRectBounds(
      const RectTyped<SourceUnits, F>& aRect,
      const RectTyped<TargetUnits, F>& aClip) const {
    // This function must never return std::numeric_limits<Float>::max() or any
    // other arbitrary large value in place of inifinity.  This often occurs
    // when aRect is an inversed projection matrix or when aRect is transformed
    // to be partly behind and in front of the camera (w=0 plane in homogenous
    // coordinates) - See Bug 1035611

    // Some call-sites will call RoundGfxRectToAppRect which clips both the
    // extents and dimensions of the rect to be bounded by nscoord_MAX.
    // If we return a Rect that, when converted to nscoords, has a width or
    // height greater than nscoord_MAX, RoundGfxRectToAppRect will clip the
    // overflow off both the min and max end of the rect after clipping the
    // extents of the rect, resulting in a translation of the rect towards the
    // infinite end.

    // The bounds returned by ProjectRectBounds are expected to be clipped only
    // on the edges beyond the bounds of the coordinate system; otherwise, the
    // clipped bounding box would be smaller than the correct one and result
    // bugs such as incorrect culling (eg. Bug 1073056)

    // To address this without requiring all code to work in homogenous
    // coordinates or interpret infinite values correctly, a specialized
    // clipping function is integrated into ProjectRectBounds.

    // Callers should pass an aClip value that represents the extents to clip
    // the result to, in the same coordinate system as aRect.
    Point4DTyped<TargetUnits, F> points[4];

    points[0] = ProjectPoint(aRect.TopLeft());
    points[1] = ProjectPoint(aRect.TopRight());
    points[2] = ProjectPoint(aRect.BottomRight());
    points[3] = ProjectPoint(aRect.BottomLeft());

    F min_x = std::numeric_limits<F>::max();
    F min_y = std::numeric_limits<F>::max();
    F max_x = -std::numeric_limits<F>::max();
    F max_y = -std::numeric_limits<F>::max();

    for (int i = 0; i < 4; i++) {
      // Only use points that exist above the w=0 plane
      if (points[i].HasPositiveWCoord()) {
        PointTyped<TargetUnits, F> point2d =
            aClip.ClampPoint(points[i].As2DPoint());
        min_x = std::min<F>(point2d.x, min_x);
        max_x = std::max<F>(point2d.x, max_x);
        min_y = std::min<F>(point2d.y, min_y);
        max_y = std::max<F>(point2d.y, max_y);
      }

      int next = (i == 3) ? 0 : i + 1;
      if (points[i].HasPositiveWCoord() != points[next].HasPositiveWCoord()) {
        // If the line between two points crosses the w=0 plane, then
        // interpolate to find the point of intersection with the w=0 plane and
        // use that instead.
        Point4DTyped<TargetUnits, F> intercept =
            ComputePerspectivePlaneIntercept(points[i], points[next]);
        // Since intercept.w will always be 0 here, we interpret x,y,z as a
        // direction towards an infinite vanishing point.
        if (intercept.x < 0.0f) {
          min_x = aClip.X();
        } else if (intercept.x > 0.0f) {
          max_x = aClip.XMost();
        }
        if (intercept.y < 0.0f) {
          min_y = aClip.Y();
        } else if (intercept.y > 0.0f) {
          max_y = aClip.YMost();
        }
      }
    }

    if (max_x < min_x || max_y < min_y) {
      return RectTyped<TargetUnits, F>(0, 0, 0, 0);
    }

    return RectTyped<TargetUnits, F>(min_x, min_y, max_x - min_x,
                                     max_y - min_y);
  }

  /**
   * TransformAndClipBounds transforms aRect as a bounding box, while clipping
   * the transformed bounds to the extents of aClip.
   */
  template <class F>
  RectTyped<TargetUnits, F> TransformAndClipBounds(
      const RectTyped<SourceUnits, F>& aRect,
      const RectTyped<TargetUnits, F>& aClip) const {
    PointTyped<UnknownUnits, F> verts[kTransformAndClipRectMaxVerts];
    size_t vertCount = TransformAndClipRect(aRect, aClip, verts);

    F min_x = std::numeric_limits<F>::max();
    F min_y = std::numeric_limits<F>::max();
    F max_x = -std::numeric_limits<F>::max();
    F max_y = -std::numeric_limits<F>::max();
    for (size_t i = 0; i < vertCount; i++) {
      min_x = std::min(min_x, verts[i].x.value);
      max_x = std::max(max_x, verts[i].x.value);
      min_y = std::min(min_y, verts[i].y.value);
      max_y = std::max(max_y, verts[i].y.value);
    }

    if (max_x < min_x || max_y < min_y) {
      return RectTyped<TargetUnits, F>(0, 0, 0, 0);
    }

    return RectTyped<TargetUnits, F>(min_x, min_y, max_x - min_x,
                                     max_y - min_y);
  }

  template <class F>
  RectTyped<TargetUnits, F> TransformAndClipBounds(
      const TriangleTyped<SourceUnits, F>& aTriangle,
      const RectTyped<TargetUnits, F>& aClip) const {
    return TransformAndClipBounds(aTriangle.BoundingBox(), aClip);
  }

  /**
   * TransformAndClipRect projects a rectangle and clips against view frustum
   * clipping planes in homogenous space so that its projected vertices are
   * constrained within the 2d rectangle passed in aClip.
   * The resulting vertices are populated in aVerts.  aVerts must be
   * pre-allocated to hold at least kTransformAndClipRectMaxVerts Points.
   * The vertex count is returned by TransformAndClipRect.  It is possible to
   * emit fewer than 3 vertices, indicating that aRect will not be visible
   * within aClip.
   */
  template <class F>
  size_t TransformAndClipRect(const RectTyped<SourceUnits, F>& aRect,
                              const RectTyped<TargetUnits, F>& aClip,
                              PointTyped<TargetUnits, F>* aVerts) const {
    typedef Point4DTyped<UnknownUnits, F> P4D;

    // The initial polygon is made up by the corners of aRect in homogenous
    // space, mapped into the destination space of this transform.
    P4D rectCorners[] = {
        TransformPoint(P4D(aRect.X(), aRect.Y(), 0, 1)),
        TransformPoint(P4D(aRect.XMost(), aRect.Y(), 0, 1)),
        TransformPoint(P4D(aRect.XMost(), aRect.YMost(), 0, 1)),
        TransformPoint(P4D(aRect.X(), aRect.YMost(), 0, 1)),
    };

    // Cut off pieces of the polygon that are outside of aClip (the "view
    // frustrum"), by consecutively intersecting the polygon with the half space
    // induced by the clipping plane for each side of aClip.
    // View frustum clipping planes are described as normals originating from
    // the 0,0,0,0 origin.
    // Each pass can increase or decrease the number of points that make up the
    // current clipped polygon. We double buffer the set of points, alternating
    // between polygonBufA and polygonBufB. Duplicated points in the polygons
    // are kept around until all clipping is done. The loop at the end filters
    // out any consecutive duplicates.
    P4D polygonBufA[kTransformAndClipRectMaxVerts];
    P4D polygonBufB[kTransformAndClipRectMaxVerts];

    Span<P4D> polygon(rectCorners);
    polygon = IntersectPolygon<F>(polygon, P4D(1.0, 0.0, 0.0, -aClip.X()),
                                  polygonBufA);
    polygon = IntersectPolygon<F>(polygon, P4D(-1.0, 0.0, 0.0, aClip.XMost()),
                                  polygonBufB);
    polygon = IntersectPolygon<F>(polygon, P4D(0.0, 1.0, 0.0, -aClip.Y()),
                                  polygonBufA);
    polygon = IntersectPolygon<F>(polygon, P4D(0.0, -1.0, 0.0, aClip.YMost()),
                                  polygonBufB);

    size_t vertCount = 0;
    for (const auto& srcPoint : polygon) {
      PointTyped<TargetUnits, F> p;
      if (srcPoint.w == 0.0) {
        // If a point lies on the intersection of the clipping planes at
        // (0,0,0,0), we must avoid a division by zero w component.
        p = PointTyped<TargetUnits, F>(0.0, 0.0);
      } else {
        p = srcPoint.As2DPoint();
      }
      // Emit only unique points
      if (vertCount == 0 || p != aVerts[vertCount - 1]) {
        aVerts[vertCount++] = p;
      }
    }

    return vertCount;
  }

  static const int kTransformAndClipRectMaxVerts = 32;

  static Matrix4x4Typed From2D(const BaseMatrix<T>& aMatrix) {
    Matrix4x4Typed matrix;
    matrix._11 = aMatrix._11;
    matrix._12 = aMatrix._12;
    matrix._21 = aMatrix._21;
    matrix._22 = aMatrix._22;
    matrix._41 = aMatrix._31;
    matrix._42 = aMatrix._32;
    return matrix;
  }

  bool Is2DIntegerTranslation() const {
    return Is2D() && As2D().IsIntegerTranslation();
  }

  TargetPoint4D TransposeTransform4D(const SourcePoint4D& aPoint) const {
    Float x = aPoint.x * _11 + aPoint.y * _12 + aPoint.z * _13 + aPoint.w * _14;
    Float y = aPoint.x * _21 + aPoint.y * _22 + aPoint.z * _23 + aPoint.w * _24;
    Float z = aPoint.x * _31 + aPoint.y * _32 + aPoint.z * _33 + aPoint.w * _34;
    Float w = aPoint.x * _41 + aPoint.y * _42 + aPoint.z * _43 + aPoint.w * _44;

    return TargetPoint4D(x, y, z, w);
  }

  template <class F>
  Point4DTyped<TargetUnits, F> TransformPoint(
      const Point4DTyped<SourceUnits, F>& aPoint) const {
    Point4DTyped<TargetUnits, F> retPoint;

    retPoint.x =
        aPoint.x * _11 + aPoint.y * _21 + aPoint.z * _31 + aPoint.w * _41;
    retPoint.y =
        aPoint.x * _12 + aPoint.y * _22 + aPoint.z * _32 + aPoint.w * _42;
    retPoint.z =
        aPoint.x * _13 + aPoint.y * _23 + aPoint.z * _33 + aPoint.w * _43;
    retPoint.w =
        aPoint.x * _14 + aPoint.y * _24 + aPoint.z * _34 + aPoint.w * _44;

    return retPoint;
  }

  template <class F>
  Point3DTyped<TargetUnits, F> TransformPoint(
      const Point3DTyped<SourceUnits, F>& aPoint) const {
    Point3DTyped<TargetUnits, F> result;
    result.x = aPoint.x * _11 + aPoint.y * _21 + aPoint.z * _31 + _41;
    result.y = aPoint.x * _12 + aPoint.y * _22 + aPoint.z * _32 + _42;
    result.z = aPoint.x * _13 + aPoint.y * _23 + aPoint.z * _33 + _43;

    result /= (aPoint.x * _14 + aPoint.y * _24 + aPoint.z * _34 + _44);

    return result;
  }

  template <class F>
  PointTyped<TargetUnits, F> TransformPoint(
      const PointTyped<SourceUnits, F>& aPoint) const {
    Point4DTyped<SourceUnits, F> temp(aPoint.x, aPoint.y, 0, 1);
    return TransformPoint(temp).As2DPoint();
  }

  template <class F>
  GFX2D_API RectTyped<TargetUnits, F> TransformBounds(
      const RectTyped<SourceUnits, F>& aRect) const {
    // If you change this also change Matrix4x4TypedFlagged::TransformBounds to
    // match.
    PointTyped<TargetUnits, F> quad[4];
    F min_x, max_x;
    F min_y, max_y;

    quad[0] = TransformPoint(aRect.TopLeft());
    quad[1] = TransformPoint(aRect.TopRight());
    quad[2] = TransformPoint(aRect.BottomLeft());
    quad[3] = TransformPoint(aRect.BottomRight());

    min_x = max_x = quad[0].x;
    min_y = max_y = quad[0].y;

    for (int i = 1; i < 4; i++) {
      if (quad[i].x < min_x) {
        min_x = quad[i].x;
      }
      if (quad[i].x > max_x) {
        max_x = quad[i].x;
      }

      if (quad[i].y < min_y) {
        min_y = quad[i].y;
      }
      if (quad[i].y > max_y) {
        max_y = quad[i].y;
      }
    }

    return RectTyped<TargetUnits, F>(min_x, min_y, max_x - min_x,
                                     max_y - min_y);
  }

  static Matrix4x4Typed Translation(T aX, T aY, T aZ) {
    return Matrix4x4Typed(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                          0.0f, 1.0f, 0.0f, aX, aY, aZ, 1.0f);
  }

  static Matrix4x4Typed Translation(const TargetPoint3D& aP) {
    return Translation(aP.x, aP.y, aP.z);
  }

  static Matrix4x4Typed Translation(const TargetPoint& aP) {
    return Translation(aP.x, aP.y, 0);
  }

  /**
   * Apply a translation to this matrix.
   *
   * The "Pre" in this method's name means that the translation is applied
   * -before- this matrix's existing transformation. That is, any vector that
   * is multiplied by the resulting matrix will first be translated, then be
   * transformed by the original transform.
   *
   * Calling this method will result in this matrix having the same value as
   * the result of:
   *
   *   Matrix4x4::Translation(x, y) * this
   *
   * (Note that in performance critical code multiplying by the result of a
   * Translation()/Scaling() call is not recommended since that results in a
   * full matrix multiply involving 64 floating-point multiplications. Calling
   * this method would be preferred since it only involves 12 floating-point
   * multiplications.)
   */
  Matrix4x4Typed& PreTranslate(T aX, T aY, T aZ) {
    _41 += aX * _11 + aY * _21 + aZ * _31;
    _42 += aX * _12 + aY * _22 + aZ * _32;
    _43 += aX * _13 + aY * _23 + aZ * _33;
    _44 += aX * _14 + aY * _24 + aZ * _34;

    return *this;
  }

  Matrix4x4Typed& PreTranslate(const Point3DTyped<UnknownUnits, T>& aPoint) {
    return PreTranslate(aPoint.x, aPoint.y, aPoint.z);
  }

  /**
   * Similar to PreTranslate, but the translation is applied -after- this
   * matrix's existing transformation instead of before it.
   *
   * This method is generally less used than PreTranslate since typically code
   * wants to adjust an existing user space to device space matrix to create a
   * transform to device space from a -new- user space (translated from the
   * previous user space). In that case consumers will need to use the Pre*
   * variants of the matrix methods rather than using the Post* methods, since
   * the Post* methods add a transform to the device space end of the
   * transformation.
   */
  Matrix4x4Typed& PostTranslate(T aX, T aY, T aZ) {
    _11 += _14 * aX;
    _21 += _24 * aX;
    _31 += _34 * aX;
    _41 += _44 * aX;
    _12 += _14 * aY;
    _22 += _24 * aY;
    _32 += _34 * aY;
    _42 += _44 * aY;
    _13 += _14 * aZ;
    _23 += _24 * aZ;
    _33 += _34 * aZ;
    _43 += _44 * aZ;

    return *this;
  }

  Matrix4x4Typed& PostTranslate(const TargetPoint3D& aPoint) {
    return PostTranslate(aPoint.x, aPoint.y, aPoint.z);
  }

  Matrix4x4Typed& PostTranslate(const TargetPoint& aPoint) {
    return PostTranslate(aPoint.x, aPoint.y, 0);
  }

  static Matrix4x4Typed Scaling(T aScaleX, T aScaleY, T aScaleZ) {
    return Matrix4x4Typed(aScaleX, 0.0f, 0.0f, 0.0f, 0.0f, aScaleY, 0.0f, 0.0f,
                          0.0f, 0.0f, aScaleZ, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
  }

  /**
   * Similar to PreTranslate, but applies a scale instead of a translation.
   */
  Matrix4x4Typed& PreScale(T aX, T aY, T aZ) {
    _11 *= aX;
    _12 *= aX;
    _13 *= aX;
    _14 *= aX;
    _21 *= aY;
    _22 *= aY;
    _23 *= aY;
    _24 *= aY;
    _31 *= aZ;
    _32 *= aZ;
    _33 *= aZ;
    _34 *= aZ;

    return *this;
  }

  template <typename NewSourceUnits>
  [[nodiscard]] Matrix4x4Typed<NewSourceUnits, TargetUnits> PreScale(
      const ScaleFactor<NewSourceUnits, SourceUnits>& aScale) const {
    auto clone = Cast<Matrix4x4Typed<NewSourceUnits, TargetUnits>>();
    clone.PreScale(aScale.scale, aScale.scale, 1);
    return clone;
  }

  template <typename NewSourceUnits>
  [[nodiscard]] Matrix4x4Typed<NewSourceUnits, TargetUnits> PreScale(
      const BaseScaleFactors2D<NewSourceUnits, SourceUnits, T>& aScale) const {
    auto clone = Cast<Matrix4x4Typed<NewSourceUnits, TargetUnits>>();
    clone.PreScale(aScale.xScale, aScale.yScale, 1);
    return clone;
  }

  /**
   * Similar to PostTranslate, but applies a scale instead of a translation.
   */
  Matrix4x4Typed& PostScale(T aScaleX, T aScaleY, T aScaleZ) {
    _11 *= aScaleX;
    _21 *= aScaleX;
    _31 *= aScaleX;
    _41 *= aScaleX;
    _12 *= aScaleY;
    _22 *= aScaleY;
    _32 *= aScaleY;
    _42 *= aScaleY;
    _13 *= aScaleZ;
    _23 *= aScaleZ;
    _33 *= aScaleZ;
    _43 *= aScaleZ;

    return *this;
  }

  template <typename NewTargetUnits>
  [[nodiscard]] Matrix4x4Typed<SourceUnits, NewTargetUnits> PostScale(
      const ScaleFactor<TargetUnits, NewTargetUnits>& aScale) const {
    auto clone = Cast<Matrix4x4Typed<SourceUnits, NewTargetUnits>>();
    clone.PostScale(aScale.scale, aScale.scale, 1);
    return clone;
  }

  template <typename NewTargetUnits>
  [[nodiscard]] Matrix4x4Typed<SourceUnits, NewTargetUnits> PostScale(
      const BaseScaleFactors2D<TargetUnits, NewTargetUnits, T>& aScale) const {
    auto clone = Cast<Matrix4x4Typed<SourceUnits, NewTargetUnits>>();
    clone.PostScale(aScale.xScale, aScale.yScale, 1);
    return clone;
  }

  void SkewXY(T aSkew) { (*this)[1] += (*this)[0] * aSkew; }

  void SkewXZ(T aSkew) { (*this)[2] += (*this)[0] * aSkew; }

  void SkewYZ(T aSkew) { (*this)[2] += (*this)[1] * aSkew; }

  Matrix4x4Typed& ChangeBasis(const Point3DTyped<UnknownUnits, T>& aOrigin) {
    return ChangeBasis(aOrigin.x, aOrigin.y, aOrigin.z);
  }

  Matrix4x4Typed& ChangeBasis(T aX, T aY, T aZ) {
    // Translate to the origin before applying this matrix
    PreTranslate(-aX, -aY, -aZ);

    // Translate back into position after applying this matrix
    PostTranslate(aX, aY, aZ);

    return *this;
  }

  Matrix4x4Typed& Transpose() {
    std::swap(_12, _21);
    std::swap(_13, _31);
    std::swap(_14, _41);

    std::swap(_23, _32);
    std::swap(_24, _42);

    std::swap(_34, _43);

    return *this;
  }

  bool operator==(const Matrix4x4Typed& o) const {
    // XXX would be nice to memcmp here, but that breaks IEEE 754 semantics
    return _11 == o._11 && _12 == o._12 && _13 == o._13 && _14 == o._14 &&
           _21 == o._21 && _22 == o._22 && _23 == o._23 && _24 == o._24 &&
           _31 == o._31 && _32 == o._32 && _33 == o._33 && _34 == o._34 &&
           _41 == o._41 && _42 == o._42 && _43 == o._43 && _44 == o._44;
  }

  bool operator!=(const Matrix4x4Typed& o) const { return !((*this) == o); }

  Matrix4x4Typed& operator=(const Matrix4x4Typed& aOther) = default;

  template <typename NewTargetUnits>
  Matrix4x4Typed<SourceUnits, NewTargetUnits, T> operator*(
      const Matrix4x4Typed<TargetUnits, NewTargetUnits, T>& aMatrix) const {
    Matrix4x4Typed<SourceUnits, NewTargetUnits, T> matrix;

    matrix._11 = _11 * aMatrix._11 + _12 * aMatrix._21 + _13 * aMatrix._31 +
                 _14 * aMatrix._41;
    matrix._21 = _21 * aMatrix._11 + _22 * aMatrix._21 + _23 * aMatrix._31 +
                 _24 * aMatrix._41;
    matrix._31 = _31 * aMatrix._11 + _32 * aMatrix._21 + _33 * aMatrix._31 +
                 _34 * aMatrix._41;
    matrix._41 = _41 * aMatrix._11 + _42 * aMatrix._21 + _43 * aMatrix._31 +
                 _44 * aMatrix._41;
    matrix._12 = _11 * aMatrix._12 + _12 * aMatrix._22 + _13 * aMatrix._32 +
                 _14 * aMatrix._42;
    matrix._22 = _21 * aMatrix._12 + _22 * aMatrix._22 + _23 * aMatrix._32 +
                 _24 * aMatrix._42;
    matrix._32 = _31 * aMatrix._12 + _32 * aMatrix._22 + _33 * aMatrix._32 +
                 _34 * aMatrix._42;
    matrix._42 = _41 * aMatrix._12 + _42 * aMatrix._22 + _43 * aMatrix._32 +
                 _44 * aMatrix._42;
    matrix._13 = _11 * aMatrix._13 + _12 * aMatrix._23 + _13 * aMatrix._33 +
                 _14 * aMatrix._43;
    matrix._23 = _21 * aMatrix._13 + _22 * aMatrix._23 + _23 * aMatrix._33 +
                 _24 * aMatrix._43;
    matrix._33 = _31 * aMatrix._13 + _32 * aMatrix._23 + _33 * aMatrix._33 +
                 _34 * aMatrix._43;
    matrix._43 = _41 * aMatrix._13 + _42 * aMatrix._23 + _43 * aMatrix._33 +
                 _44 * aMatrix._43;
    matrix._14 = _11 * aMatrix._14 + _12 * aMatrix._24 + _13 * aMatrix._34 +
                 _14 * aMatrix._44;
    matrix._24 = _21 * aMatrix._14 + _22 * aMatrix._24 + _23 * aMatrix._34 +
                 _24 * aMatrix._44;
    matrix._34 = _31 * aMatrix._14 + _32 * aMatrix._24 + _33 * aMatrix._34 +
                 _34 * aMatrix._44;
    matrix._44 = _41 * aMatrix._14 + _42 * aMatrix._24 + _43 * aMatrix._34 +
                 _44 * aMatrix._44;

    return matrix;
  }

  Matrix4x4Typed& operator*=(
      const Matrix4x4Typed<TargetUnits, TargetUnits, T>& aMatrix) {
    *this = *this * aMatrix;
    return *this;
  }

  /* Returns true if the matrix is an identity matrix.
   */
  bool IsIdentity() const {
    return _11 == 1.0f && _12 == 0.0f && _13 == 0.0f && _14 == 0.0f &&
           _21 == 0.0f && _22 == 1.0f && _23 == 0.0f && _24 == 0.0f &&
           _31 == 0.0f && _32 == 0.0f && _33 == 1.0f && _34 == 0.0f &&
           _41 == 0.0f && _42 == 0.0f && _43 == 0.0f && _44 == 1.0f;
  }

  bool IsSingular() const { return Determinant() == 0.0; }

  T Determinant() const {
    return _14 * _23 * _32 * _41 - _13 * _24 * _32 * _41 -
           _14 * _22 * _33 * _41 + _12 * _24 * _33 * _41 +
           _13 * _22 * _34 * _41 - _12 * _23 * _34 * _41 -
           _14 * _23 * _31 * _42 + _13 * _24 * _31 * _42 +
           _14 * _21 * _33 * _42 - _11 * _24 * _33 * _42 -
           _13 * _21 * _34 * _42 + _11 * _23 * _34 * _42 +
           _14 * _22 * _31 * _43 - _12 * _24 * _31 * _43 -
           _14 * _21 * _32 * _43 + _11 * _24 * _32 * _43 +
           _12 * _21 * _34 * _43 - _11 * _22 * _34 * _43 -
           _13 * _22 * _31 * _44 + _12 * _23 * _31 * _44 +
           _13 * _21 * _32 * _44 - _11 * _23 * _32 * _44 -
           _12 * _21 * _33 * _44 + _11 * _22 * _33 * _44;
  }

  // Invert() is not unit-correct. Prefer Inverse() where possible.
  bool Invert() {
    T det = Determinant();
    if (!det) {
      return false;
    }

    Matrix4x4Typed<SourceUnits, TargetUnits, T> result;
    result._11 = _23 * _34 * _42 - _24 * _33 * _42 + _24 * _32 * _43 -
                 _22 * _34 * _43 - _23 * _32 * _44 + _22 * _33 * _44;
    result._12 = _14 * _33 * _42 - _13 * _34 * _42 - _14 * _32 * _43 +
                 _12 * _34 * _43 + _13 * _32 * _44 - _12 * _33 * _44;
    result._13 = _13 * _24 * _42 - _14 * _23 * _42 + _14 * _22 * _43 -
                 _12 * _24 * _43 - _13 * _22 * _44 + _12 * _23 * _44;
    result._14 = _14 * _23 * _32 - _13 * _24 * _32 - _14 * _22 * _33 +
                 _12 * _24 * _33 + _13 * _22 * _34 - _12 * _23 * _34;
    result._21 = _24 * _33 * _41 - _23 * _34 * _41 - _24 * _31 * _43 +
                 _21 * _34 * _43 + _23 * _31 * _44 - _21 * _33 * _44;
    result._22 = _13 * _34 * _41 - _14 * _33 * _41 + _14 * _31 * _43 -
                 _11 * _34 * _43 - _13 * _31 * _44 + _11 * _33 * _44;
    result._23 = _14 * _23 * _41 - _13 * _24 * _41 - _14 * _21 * _43 +
                 _11 * _24 * _43 + _13 * _21 * _44 - _11 * _23 * _44;
    result._24 = _13 * _24 * _31 - _14 * _23 * _31 + _14 * _21 * _33 -
                 _11 * _24 * _33 - _13 * _21 * _34 + _11 * _23 * _34;
    result._31 = _22 * _34 * _41 - _24 * _32 * _41 + _24 * _31 * _42 -
                 _21 * _34 * _42 - _22 * _31 * _44 + _21 * _32 * _44;
    result._32 = _14 * _32 * _41 - _12 * _34 * _41 - _14 * _31 * _42 +
                 _11 * _34 * _42 + _12 * _31 * _44 - _11 * _32 * _44;
    result._33 = _12 * _24 * _41 - _14 * _22 * _41 + _14 * _21 * _42 -
                 _11 * _24 * _42 - _12 * _21 * _44 + _11 * _22 * _44;
    result._34 = _14 * _22 * _31 - _12 * _24 * _31 - _14 * _21 * _32 +
                 _11 * _24 * _32 + _12 * _21 * _34 - _11 * _22 * _34;
    result._41 = _23 * _32 * _41 - _22 * _33 * _41 - _23 * _31 * _42 +
                 _21 * _33 * _42 + _22 * _31 * _43 - _21 * _32 * _43;
    result._42 = _12 * _33 * _41 - _13 * _32 * _41 + _13 * _31 * _42 -
                 _11 * _33 * _42 - _12 * _31 * _43 + _11 * _32 * _43;
    result._43 = _13 * _22 * _41 - _12 * _23 * _41 - _13 * _21 * _42 +
                 _11 * _23 * _42 + _12 * _21 * _43 - _11 * _22 * _43;
    result._44 = _12 * _23 * _31 - _13 * _22 * _31 + _13 * _21 * _32 -
                 _11 * _23 * _32 - _12 * _21 * _33 + _11 * _22 * _33;

    result._11 /= det;
    result._12 /= det;
    result._13 /= det;
    result._14 /= det;
    result._21 /= det;
    result._22 /= det;
    result._23 /= det;
    result._24 /= det;
    result._31 /= det;
    result._32 /= det;
    result._33 /= det;
    result._34 /= det;
    result._41 /= det;
    result._42 /= det;
    result._43 /= det;
    result._44 /= det;
    *this = result;

    return true;
  }

  Matrix4x4Typed<TargetUnits, SourceUnits, T> Inverse() const {
    typedef Matrix4x4Typed<TargetUnits, SourceUnits, T> InvertedMatrix;
    InvertedMatrix clone = Cast<InvertedMatrix>();
    DebugOnly<bool> inverted = clone.Invert();
    MOZ_ASSERT(inverted,
               "Attempted to get the inverse of a non-invertible matrix");
    return clone;
  }

  Maybe<Matrix4x4Typed<TargetUnits, SourceUnits, T>> MaybeInverse() const {
    typedef Matrix4x4Typed<TargetUnits, SourceUnits, T> InvertedMatrix;
    InvertedMatrix clone = Cast<InvertedMatrix>();
    if (clone.Invert()) {
      return Some(clone);
    }
    return Nothing();
  }

  void Normalize() {
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        (*this)[i][j] /= (*this)[3][3];
      }
    }
  }

  bool FuzzyEqual(const Matrix4x4Typed& o) const {
    return gfx::FuzzyEqual(_11, o._11) && gfx::FuzzyEqual(_12, o._12) &&
           gfx::FuzzyEqual(_13, o._13) && gfx::FuzzyEqual(_14, o._14) &&
           gfx::FuzzyEqual(_21, o._21) && gfx::FuzzyEqual(_22, o._22) &&
           gfx::FuzzyEqual(_23, o._23) && gfx::FuzzyEqual(_24, o._24) &&
           gfx::FuzzyEqual(_31, o._31) && gfx::FuzzyEqual(_32, o._32) &&
           gfx::FuzzyEqual(_33, o._33) && gfx::FuzzyEqual(_34, o._34) &&
           gfx::FuzzyEqual(_41, o._41) && gfx::FuzzyEqual(_42, o._42) &&
           gfx::FuzzyEqual(_43, o._43) && gfx::FuzzyEqual(_44, o._44);
  }

  bool FuzzyEqualsMultiplicative(const Matrix4x4Typed& o) const {
    return ::mozilla::FuzzyEqualsMultiplicative(_11, o._11) &&
           ::mozilla::FuzzyEqualsMultiplicative(_12, o._12) &&
           ::mozilla::FuzzyEqualsMultiplicative(_13, o._13) &&
           ::mozilla::FuzzyEqualsMultiplicative(_14, o._14) &&
           ::mozilla::FuzzyEqualsMultiplicative(_21, o._21) &&
           ::mozilla::FuzzyEqualsMultiplicative(_22, o._22) &&
           ::mozilla::FuzzyEqualsMultiplicative(_23, o._23) &&
           ::mozilla::FuzzyEqualsMultiplicative(_24, o._24) &&
           ::mozilla::FuzzyEqualsMultiplicative(_31, o._31) &&
           ::mozilla::FuzzyEqualsMultiplicative(_32, o._32) &&
           ::mozilla::FuzzyEqualsMultiplicative(_33, o._33) &&
           ::mozilla::FuzzyEqualsMultiplicative(_34, o._34) &&
           ::mozilla::FuzzyEqualsMultiplicative(_41, o._41) &&
           ::mozilla::FuzzyEqualsMultiplicative(_42, o._42) &&
           ::mozilla::FuzzyEqualsMultiplicative(_43, o._43) &&
           ::mozilla::FuzzyEqualsMultiplicative(_44, o._44);
  }

  bool IsBackfaceVisible() const {
    // Inverse()._33 < 0;
    T det = Determinant();
    T __33 = _12 * _24 * _41 - _14 * _22 * _41 + _14 * _21 * _42 -
             _11 * _24 * _42 - _12 * _21 * _44 + _11 * _22 * _44;
    return (__33 * det) < 0;
  }

  Matrix4x4Typed& NudgeToIntegersFixedEpsilon() {
    NudgeToInteger(&_11);
    NudgeToInteger(&_12);
    NudgeToInteger(&_13);
    NudgeToInteger(&_14);
    NudgeToInteger(&_21);
    NudgeToInteger(&_22);
    NudgeToInteger(&_23);
    NudgeToInteger(&_24);
    NudgeToInteger(&_31);
    NudgeToInteger(&_32);
    NudgeToInteger(&_33);
    NudgeToInteger(&_34);
    static const float error = 1e-5f;
    NudgeToInteger(&_41, error);
    NudgeToInteger(&_42, error);
    NudgeToInteger(&_43, error);
    NudgeToInteger(&_44, error);
    return *this;
  }

  Point4D TransposedVector(int aIndex) const {
    MOZ_ASSERT(aIndex >= 0 && aIndex <= 3, "Invalid matrix array index");
    return Point4DTyped<UnknownUnits, T>(*((&_11) + aIndex), *((&_21) + aIndex),
                                         *((&_31) + aIndex),
                                         *((&_41) + aIndex));
  }

  void SetTransposedVector(int aIndex, Point4DTyped<UnknownUnits, T>& aVector) {
    MOZ_ASSERT(aIndex >= 0 && aIndex <= 3, "Invalid matrix array index");
    *((&_11) + aIndex) = aVector.x;
    *((&_21) + aIndex) = aVector.y;
    *((&_31) + aIndex) = aVector.z;
    *((&_41) + aIndex) = aVector.w;
  }

  bool Decompose(Point3DTyped<UnknownUnits, T>& translation,
                 BaseQuaternion<T>& rotation,
                 Point3DTyped<UnknownUnits, T>& scale) const {
    // Ensure matrix can be normalized
    if (gfx::FuzzyEqual(_44, 0.0f)) {
      return false;
    }
    Matrix4x4Typed mat = *this;
    mat.Normalize();
    if (HasPerspectiveComponent()) {
      // We do not support projection matrices
      return false;
    }

    // Extract translation
    translation.x = mat._41;
    translation.y = mat._42;
    translation.z = mat._43;

    // Remove translation
    mat._41 = 0.0f;
    mat._42 = 0.0f;
    mat._43 = 0.0f;

    // Extract scale
    scale.x = sqrtf(_11 * _11 + _21 * _21 + _31 * _31);
    scale.y = sqrtf(_12 * _12 + _22 * _22 + _32 * _32);
    scale.z = sqrtf(_13 * _13 + _23 * _23 + _33 * _33);

    // Remove scale
    if (gfx::FuzzyEqual(scale.x, 0.0f) || gfx::FuzzyEqual(scale.y, 0.0f) ||
        gfx::FuzzyEqual(scale.z, 0.0f)) {
      // We do not support matrices with a zero scale component
      return false;
    }

    // Extract rotation
    rotation.SetFromRotationMatrix(this->ToUnknownMatrix());
    return true;
  }

  // Sets this matrix to a rotation matrix given by aQuat.
  // This quaternion *MUST* be normalized!
  // Implemented in Quaternion.cpp
  void SetRotationFromQuaternion(const BaseQuaternion<T>& q) {
    const T x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    const T xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
    const T yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
    const T wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;

    _11 = 1.0f - (yy + zz);
    _21 = xy - wz;
    _31 = xz + wy;
    _41 = 0.0f;

    _12 = xy + wz;
    _22 = 1.0f - (xx + zz);
    _32 = yz - wx;
    _42 = 0.0f;

    _13 = xz - wy;
    _23 = yz + wx;
    _33 = 1.0f - (xx + yy);
    _43 = 0.0f;

    _14 = _42 = _43 = 0.0f;
    _44 = 1.0f;
  }

  // Set all the members of the matrix to NaN
  void SetNAN() {
    _11 = UnspecifiedNaN<T>();
    _21 = UnspecifiedNaN<T>();
    _31 = UnspecifiedNaN<T>();
    _41 = UnspecifiedNaN<T>();
    _12 = UnspecifiedNaN<T>();
    _22 = UnspecifiedNaN<T>();
    _32 = UnspecifiedNaN<T>();
    _42 = UnspecifiedNaN<T>();
    _13 = UnspecifiedNaN<T>();
    _23 = UnspecifiedNaN<T>();
    _33 = UnspecifiedNaN<T>();
    _43 = UnspecifiedNaN<T>();
    _14 = UnspecifiedNaN<T>();
    _24 = UnspecifiedNaN<T>();
    _34 = UnspecifiedNaN<T>();
    _44 = UnspecifiedNaN<T>();
  }

  // Verifies that the matrix contains no Infs or NaNs
  bool IsFinite() const {
    return std::isfinite(_11) && std::isfinite(_12) && std::isfinite(_13) &&
           std::isfinite(_14) && std::isfinite(_21) && std::isfinite(_22) &&
           std::isfinite(_23) && std::isfinite(_24) && std::isfinite(_31) &&
           std::isfinite(_32) && std::isfinite(_33) && std::isfinite(_34) &&
           std::isfinite(_41) && std::isfinite(_42) && std::isfinite(_43) &&
           std::isfinite(_44);
  }

  void SkewXY(double aXSkew, double aYSkew) {
    // XXX Is double precision really necessary here
    T tanX = SafeTangent(aXSkew);
    T tanY = SafeTangent(aYSkew);
    T temp;

    temp = _11;
    _11 += tanY * _21;
    _21 += tanX * temp;

    temp = _12;
    _12 += tanY * _22;
    _22 += tanX * temp;

    temp = _13;
    _13 += tanY * _23;
    _23 += tanX * temp;

    temp = _14;
    _14 += tanY * _24;
    _24 += tanX * temp;
  }

  void RotateX(double aTheta) {
    // XXX Is double precision really necessary here
    double cosTheta = FlushToZero(cos(aTheta));
    double sinTheta = FlushToZero(sin(aTheta));

    T temp;

    temp = _21;
    _21 = cosTheta * _21 + sinTheta * _31;
    _31 = -sinTheta * temp + cosTheta * _31;

    temp = _22;
    _22 = cosTheta * _22 + sinTheta * _32;
    _32 = -sinTheta * temp + cosTheta * _32;

    temp = _23;
    _23 = cosTheta * _23 + sinTheta * _33;
    _33 = -sinTheta * temp + cosTheta * _33;

    temp = _24;
    _24 = cosTheta * _24 + sinTheta * _34;
    _34 = -sinTheta * temp + cosTheta * _34;
  }

  void RotateY(double aTheta) {
    // XXX Is double precision really necessary here
    double cosTheta = FlushToZero(cos(aTheta));
    double sinTheta = FlushToZero(sin(aTheta));

    T temp;

    temp = _11;
    _11 = cosTheta * _11 + -sinTheta * _31;
    _31 = sinTheta * temp + cosTheta * _31;

    temp = _12;
    _12 = cosTheta * _12 + -sinTheta * _32;
    _32 = sinTheta * temp + cosTheta * _32;

    temp = _13;
    _13 = cosTheta * _13 + -sinTheta * _33;
    _33 = sinTheta * temp + cosTheta * _33;

    temp = _14;
    _14 = cosTheta * _14 + -sinTheta * _34;
    _34 = sinTheta * temp + cosTheta * _34;
  }

  void RotateZ(double aTheta) {
    // XXX Is double precision really necessary here
    double cosTheta = FlushToZero(cos(aTheta));
    double sinTheta = FlushToZero(sin(aTheta));

    T temp;

    temp = _11;
    _11 = cosTheta * _11 + sinTheta * _21;
    _21 = -sinTheta * temp + cosTheta * _21;

    temp = _12;
    _12 = cosTheta * _12 + sinTheta * _22;
    _22 = -sinTheta * temp + cosTheta * _22;

    temp = _13;
    _13 = cosTheta * _13 + sinTheta * _23;
    _23 = -sinTheta * temp + cosTheta * _23;

    temp = _14;
    _14 = cosTheta * _14 + sinTheta * _24;
    _24 = -sinTheta * temp + cosTheta * _24;
  }

  // Sets this matrix to a rotation matrix about a
  // vector [x,y,z] by angle theta. The vector is normalized
  // to a unit vector.
  // https://drafts.csswg.org/css-transforms-2/#Rotate3dDefined
  void SetRotateAxisAngle(double aX, double aY, double aZ, double aTheta) {
    Point3DTyped<UnknownUnits, T> vector(aX, aY, aZ);
    if (!vector.Length()) {
      return;
    }
    vector.RobustNormalize();

    double x = vector.x;
    double y = vector.y;
    double z = vector.z;

    double cosTheta = FlushToZero(cos(aTheta));
    double sinTheta = FlushToZero(sin(aTheta));

    // sin(aTheta / 2) * cos(aTheta / 2)
    double sc = sinTheta / 2;
    // pow(sin(aTheta / 2), 2)
    double sq = (1 - cosTheta) / 2;

    _11 = 1 - 2 * (y * y + z * z) * sq;
    _12 = 2 * (x * y * sq + z * sc);
    _13 = 2 * (x * z * sq - y * sc);
    _14 = 0.0f;
    _21 = 2 * (x * y * sq - z * sc);
    _22 = 1 - 2 * (x * x + z * z) * sq;
    _23 = 2 * (y * z * sq + x * sc);
    _24 = 0.0f;
    _31 = 2 * (x * z * sq + y * sc);
    _32 = 2 * (y * z * sq - x * sc);
    _33 = 1 - 2 * (x * x + y * y) * sq;
    _34 = 0.0f;
    _41 = 0.0f;
    _42 = 0.0f;
    _43 = 0.0f;
    _44 = 1.0f;
  }

  void Perspective(T aDepth) {
    MOZ_ASSERT(aDepth > 0.0f, "Perspective must be positive!");
    _31 += -1.0 / aDepth * _41;
    _32 += -1.0 / aDepth * _42;
    _33 += -1.0 / aDepth * _43;
    _34 += -1.0 / aDepth * _44;
  }

  Point3D GetNormalVector() const {
    // Define a plane in transformed space as the transformations
    // of 3 points on the z=0 screen plane.
    Point3DTyped<UnknownUnits, T> a =
        TransformPoint(Point3DTyped<UnknownUnits, T>(0, 0, 0));
    Point3DTyped<UnknownUnits, T> b =
        TransformPoint(Point3DTyped<UnknownUnits, T>(0, 1, 0));
    Point3DTyped<UnknownUnits, T> c =
        TransformPoint(Point3DTyped<UnknownUnits, T>(1, 0, 0));

    // Convert to two vectors on the surface of the plane.
    Point3DTyped<UnknownUnits, T> ab = b - a;
    Point3DTyped<UnknownUnits, T> ac = c - a;

    return ac.CrossProduct(ab);
  }

  /**
   * Returns true if the matrix has any transform other
   * than a straight translation.
   */
  bool HasNonTranslation() const {
    return !gfx::FuzzyEqual(_11, 1.0) || !gfx::FuzzyEqual(_22, 1.0) ||
           !gfx::FuzzyEqual(_12, 0.0) || !gfx::FuzzyEqual(_21, 0.0) ||
           !gfx::FuzzyEqual(_13, 0.0) || !gfx::FuzzyEqual(_23, 0.0) ||
           !gfx::FuzzyEqual(_31, 0.0) || !gfx::FuzzyEqual(_32, 0.0) ||
           !gfx::FuzzyEqual(_33, 1.0);
  }

  /**
   * Returns true if the matrix is anything other than a straight
   * translation by integers.
   */
  bool HasNonIntegerTranslation() const {
    return HasNonTranslation() || !gfx::FuzzyEqual(_41, floor(_41 + 0.5)) ||
           !gfx::FuzzyEqual(_42, floor(_42 + 0.5)) ||
           !gfx::FuzzyEqual(_43, floor(_43 + 0.5));
  }

  /**
   * Return true if the matrix is with perspective (w).
   */
  bool HasPerspectiveComponent() const {
    return _14 != 0 || _24 != 0 || _34 != 0 || _44 != 1;
  }

  /* Returns true if the matrix is a rectilinear transformation (i.e.
   * grid-aligned rectangles are transformed to grid-aligned rectangles).
   * This should only be called on 2D matrices.
   */
  bool IsRectilinear() const {
    MOZ_ASSERT(Is2D());
    if (gfx::FuzzyEqual(_12, 0) && gfx::FuzzyEqual(_21, 0)) {
      return true;
    } else if (gfx::FuzzyEqual(_22, 0) && gfx::FuzzyEqual(_11, 0)) {
      return true;
    }
    return false;
  }

  /**
   * Convert between typed and untyped matrices.
   */
  using UnknownMatrix = Matrix4x4Typed<UnknownUnits, UnknownUnits, T>;
  UnknownMatrix ToUnknownMatrix() const {
    return UnknownMatrix{_11, _12, _13, _14, _21, _22, _23, _24,
                         _31, _32, _33, _34, _41, _42, _43, _44};
  }
  static Matrix4x4Typed FromUnknownMatrix(const UnknownMatrix& aUnknown) {
    return Matrix4x4Typed{
        aUnknown._11, aUnknown._12, aUnknown._13, aUnknown._14,
        aUnknown._21, aUnknown._22, aUnknown._23, aUnknown._24,
        aUnknown._31, aUnknown._32, aUnknown._33, aUnknown._34,
        aUnknown._41, aUnknown._42, aUnknown._43, aUnknown._44};
  }
  /**
   * For convenience, overload FromUnknownMatrix() for Maybe<Matrix>.
   */
  static Maybe<Matrix4x4Typed> FromUnknownMatrix(
      const Maybe<UnknownMatrix>& aUnknown) {
    if (aUnknown.isSome()) {
      return Some(FromUnknownMatrix(*aUnknown));
    }
    return Nothing();
  }
};

typedef Matrix4x4Typed<UnknownUnits, UnknownUnits> Matrix4x4;
typedef Matrix4x4Typed<UnknownUnits, UnknownUnits, double> Matrix4x4Double;

class Matrix5x4 {
 public:
  Matrix5x4()
      : _11(1.0f),
        _12(0),
        _13(0),
        _14(0),
        _21(0),
        _22(1.0f),
        _23(0),
        _24(0),
        _31(0),
        _32(0),
        _33(1.0f),
        _34(0),
        _41(0),
        _42(0),
        _43(0),
        _44(1.0f),
        _51(0),
        _52(0),
        _53(0),
        _54(0) {}
  Matrix5x4(Float a11, Float a12, Float a13, Float a14, Float a21, Float a22,
            Float a23, Float a24, Float a31, Float a32, Float a33, Float a34,
            Float a41, Float a42, Float a43, Float a44, Float a51, Float a52,
            Float a53, Float a54)
      : _11(a11),
        _12(a12),
        _13(a13),
        _14(a14),
        _21(a21),
        _22(a22),
        _23(a23),
        _24(a24),
        _31(a31),
        _32(a32),
        _33(a33),
        _34(a34),
        _41(a41),
        _42(a42),
        _43(a43),
        _44(a44),
        _51(a51),
        _52(a52),
        _53(a53),
        _54(a54) {}

  bool operator==(const Matrix5x4& o) const {
    return _11 == o._11 && _12 == o._12 && _13 == o._13 && _14 == o._14 &&
           _21 == o._21 && _22 == o._22 && _23 == o._23 && _24 == o._24 &&
           _31 == o._31 && _32 == o._32 && _33 == o._33 && _34 == o._34 &&
           _41 == o._41 && _42 == o._42 && _43 == o._43 && _44 == o._44 &&
           _51 == o._51 && _52 == o._52 && _53 == o._53 && _54 == o._54;
  }

  bool operator!=(const Matrix5x4& aMatrix) const {
    return !(*this == aMatrix);
  }

  Matrix5x4 operator*(const Matrix5x4& aMatrix) const {
    Matrix5x4 resultMatrix;

    resultMatrix._11 = this->_11 * aMatrix._11 + this->_12 * aMatrix._21 +
                       this->_13 * aMatrix._31 + this->_14 * aMatrix._41;
    resultMatrix._12 = this->_11 * aMatrix._12 + this->_12 * aMatrix._22 +
                       this->_13 * aMatrix._32 + this->_14 * aMatrix._42;
    resultMatrix._13 = this->_11 * aMatrix._13 + this->_12 * aMatrix._23 +
                       this->_13 * aMatrix._33 + this->_14 * aMatrix._43;
    resultMatrix._14 = this->_11 * aMatrix._14 + this->_12 * aMatrix._24 +
                       this->_13 * aMatrix._34 + this->_14 * aMatrix._44;
    resultMatrix._21 = this->_21 * aMatrix._11 + this->_22 * aMatrix._21 +
                       this->_23 * aMatrix._31 + this->_24 * aMatrix._41;
    resultMatrix._22 = this->_21 * aMatrix._12 + this->_22 * aMatrix._22 +
                       this->_23 * aMatrix._32 + this->_24 * aMatrix._42;
    resultMatrix._23 = this->_21 * aMatrix._13 + this->_22 * aMatrix._23 +
                       this->_23 * aMatrix._33 + this->_24 * aMatrix._43;
    resultMatrix._24 = this->_21 * aMatrix._14 + this->_22 * aMatrix._24 +
                       this->_23 * aMatrix._34 + this->_24 * aMatrix._44;
    resultMatrix._31 = this->_31 * aMatrix._11 + this->_32 * aMatrix._21 +
                       this->_33 * aMatrix._31 + this->_34 * aMatrix._41;
    resultMatrix._32 = this->_31 * aMatrix._12 + this->_32 * aMatrix._22 +
                       this->_33 * aMatrix._32 + this->_34 * aMatrix._42;
    resultMatrix._33 = this->_31 * aMatrix._13 + this->_32 * aMatrix._23 +
                       this->_33 * aMatrix._33 + this->_34 * aMatrix._43;
    resultMatrix._34 = this->_31 * aMatrix._14 + this->_32 * aMatrix._24 +
                       this->_33 * aMatrix._34 + this->_34 * aMatrix._44;
    resultMatrix._41 = this->_41 * aMatrix._11 + this->_42 * aMatrix._21 +
                       this->_43 * aMatrix._31 + this->_44 * aMatrix._41;
    resultMatrix._42 = this->_41 * aMatrix._12 + this->_42 * aMatrix._22 +
                       this->_43 * aMatrix._32 + this->_44 * aMatrix._42;
    resultMatrix._43 = this->_41 * aMatrix._13 + this->_42 * aMatrix._23 +
                       this->_43 * aMatrix._33 + this->_44 * aMatrix._43;
    resultMatrix._44 = this->_41 * aMatrix._14 + this->_42 * aMatrix._24 +
                       this->_43 * aMatrix._34 + this->_44 * aMatrix._44;
    resultMatrix._51 = this->_51 * aMatrix._11 + this->_52 * aMatrix._21 +
                       this->_53 * aMatrix._31 + this->_54 * aMatrix._41 +
                       aMatrix._51;
    resultMatrix._52 = this->_51 * aMatrix._12 + this->_52 * aMatrix._22 +
                       this->_53 * aMatrix._32 + this->_54 * aMatrix._42 +
                       aMatrix._52;
    resultMatrix._53 = this->_51 * aMatrix._13 + this->_52 * aMatrix._23 +
                       this->_53 * aMatrix._33 + this->_54 * aMatrix._43 +
                       aMatrix._53;
    resultMatrix._54 = this->_51 * aMatrix._14 + this->_52 * aMatrix._24 +
                       this->_53 * aMatrix._34 + this->_54 * aMatrix._44 +
                       aMatrix._54;

    return resultMatrix;
  }

  Matrix5x4& operator*=(const Matrix5x4& aMatrix) {
    *this = *this * aMatrix;
    return *this;
  }

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const Matrix5x4& aMatrix) {
    const Float* f = &aMatrix._11;
    aStream << "[ " << f[0] << ' ' << f[1] << ' ' << f[2] << ' ' << f[3] << ';';
    f += 4;
    aStream << ' ' << f[0] << ' ' << f[1] << ' ' << f[2] << ' ' << f[3] << ';';
    f += 4;
    aStream << ' ' << f[0] << ' ' << f[1] << ' ' << f[2] << ' ' << f[3] << ';';
    f += 4;
    aStream << ' ' << f[0] << ' ' << f[1] << ' ' << f[2] << ' ' << f[3] << ';';
    f += 4;
    aStream << ' ' << f[0] << ' ' << f[1] << ' ' << f[2] << ' ' << f[3]
            << "; ]";
    return aStream;
  }

  union {
    struct {
      Float _11, _12, _13, _14;
      Float _21, _22, _23, _24;
      Float _31, _32, _33, _34;
      Float _41, _42, _43, _44;
      Float _51, _52, _53, _54;
    };
    Float components[20];
  };
};

/* This Matrix class will carry one additional type field in order to
 * track what type of 4x4 matrix we're dealing with, it can then execute
 * simplified versions of certain operations when applicable.
 * This does not allow access to the parent class directly, as a caller
 * could then mutate the parent class without updating the type.
 */

enum class MatrixType : uint8_t {
  Identity,
  Simple,  // 2x3 Matrix
  Full     // 4x4 Matrix
};

template <typename SourceUnits, typename TargetUnits>
class Matrix4x4TypedFlagged
    : protected Matrix4x4Typed<SourceUnits, TargetUnits> {
 public:
  using Parent = Matrix4x4Typed<SourceUnits, TargetUnits>;
  using Parent::_11;
  using Parent::_12;
  using Parent::_13;
  using Parent::_14;
  using Parent::_21;
  using Parent::_22;
  using Parent::_23;
  using Parent::_24;
  using Parent::_31;
  using Parent::_32;
  using Parent::_33;
  using Parent::_34;
  using Parent::_41;
  using Parent::_42;
  using Parent::_43;
  using Parent::_44;

  Matrix4x4TypedFlagged() : mType(MatrixType::Identity) {}

  Matrix4x4TypedFlagged(Float a11, Float a12, Float a13, Float a14, Float a21,
                        Float a22, Float a23, Float a24, Float a31, Float a32,
                        Float a33, Float a34, Float a41, Float a42, Float a43,
                        Float a44)
      : Parent(a11, a12, a13, a14, a21, a22, a23, a24, a31, a32, a33, a34, a41,
               a42, a43, a44) {
    Analyze();
  }

  MOZ_IMPLICIT Matrix4x4TypedFlagged(const Parent& aOther) : Parent(aOther) {
    Analyze();
  }

  template <typename NewMatrix4x4TypedFlagged>
  [[nodiscard]] NewMatrix4x4TypedFlagged Cast() const {
    return NewMatrix4x4TypedFlagged(_11, _12, _13, _14, _21, _22, _23, _24, _31,
                                    _32, _33, _34, _41, _42, _43, _44, mType);
  }

  static Matrix4x4TypedFlagged Translation2d(Float aX, Float aY) {
    MatrixType matrixType = MatrixType::Simple;
    if (aX == 0.0 && aY == 0.0) {
      matrixType = MatrixType::Identity;
    }
    return Matrix4x4TypedFlagged(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f, 0.0f, aX, aY, 0.0f, 1.0f,
                                 matrixType);
  }

  static Matrix4x4TypedFlagged Scaling(Float aScaleX, Float aScaleY,
                                       Float aScaleZ) {
    MatrixType matrixType = MatrixType::Full;
    if (aScaleZ == 1.0) {
      if (aScaleX == 1.0 && aScaleY == 1.0) {
        matrixType = MatrixType::Identity;
      } else {
        matrixType = MatrixType::Simple;
      }
    }
    return Matrix4x4TypedFlagged(aScaleX, 0.0f, 0.0f, 0.0f, 0.0f, aScaleY, 0.0f,
                                 0.0f, 0.0f, 0.0f, aScaleZ, 0.0f, 0.0f, 0.0f,
                                 0.0f, 1.0f, matrixType);
  }

  template <class F>
  PointTyped<TargetUnits, F> TransformPoint(
      const PointTyped<SourceUnits, F>& aPoint) const {
    if (mType == MatrixType::Identity) {
      return aPoint;
    }

    if (mType == MatrixType::Simple) {
      return TransformPointSimple(aPoint);
    }

    return Parent::TransformPoint(aPoint);
  }

  template <class F>
  RectTyped<TargetUnits, F> TransformBounds(
      const RectTyped<SourceUnits, F>& aRect) const {
    if (mType == MatrixType::Identity) {
      return aRect;
    }

    if (mType == MatrixType::Simple) {
      PointTyped<TargetUnits, F> quad[4];
      F min_x, max_x;
      F min_y, max_y;

      quad[0] = TransformPointSimple(aRect.TopLeft());
      quad[1] = TransformPointSimple(aRect.TopRight());
      quad[2] = TransformPointSimple(aRect.BottomLeft());
      quad[3] = TransformPointSimple(aRect.BottomRight());

      min_x = max_x = quad[0].x;
      min_y = max_y = quad[0].y;

      for (int i = 1; i < 4; i++) {
        if (quad[i].x < min_x) {
          min_x = quad[i].x;
        }
        if (quad[i].x > max_x) {
          max_x = quad[i].x;
        }

        if (quad[i].y < min_y) {
          min_y = quad[i].y;
        }
        if (quad[i].y > max_y) {
          max_y = quad[i].y;
        }
      }

      return RectTyped<TargetUnits, F>(min_x, min_y, max_x - min_x,
                                       max_y - min_y);
    }

    return Parent::TransformBounds(aRect);
  }

  template <class F>
  RectTyped<TargetUnits, F> TransformAndClipBounds(
      const RectTyped<SourceUnits, F>& aRect,
      const RectTyped<TargetUnits, F>& aClip) const {
    if (mType == MatrixType::Identity) {
      const RectTyped<SourceUnits, F>& clipped = aRect.Intersect(aClip);
      return RectTyped<TargetUnits, F>(clipped.X(), clipped.Y(),
                                       clipped.Width(), clipped.Height());
    }

    if (mType == MatrixType::Simple) {
      PointTyped<UnknownUnits, F> p1 = TransformPointSimple(aRect.TopLeft());
      PointTyped<UnknownUnits, F> p2 = TransformPointSimple(aRect.TopRight());
      PointTyped<UnknownUnits, F> p3 = TransformPointSimple(aRect.BottomLeft());
      PointTyped<UnknownUnits, F> p4 =
          TransformPointSimple(aRect.BottomRight());

      F min_x = std::min(std::min(std::min(p1.x, p2.x), p3.x), p4.x);
      F max_x = std::max(std::max(std::max(p1.x, p2.x), p3.x), p4.x);
      F min_y = std::min(std::min(std::min(p1.y, p2.y), p3.y), p4.y);
      F max_y = std::max(std::max(std::max(p1.y, p2.y), p3.y), p4.y);

      PointTyped<TargetUnits, F> topLeft(
          std::min(std::max(min_x, aClip.x), aClip.XMost()),
          std::min(std::max(min_y, aClip.y), aClip.YMost()));
      F width = std::min(std::max(max_x, aClip.x), aClip.XMost()) - topLeft.x;
      F height = std::min(std::max(max_y, aClip.y), aClip.YMost()) - topLeft.y;

      return RectTyped<TargetUnits, F>(topLeft.x, topLeft.y, width, height);
    }
    return Parent::TransformAndClipBounds(aRect, aClip);
  }

  bool FuzzyEqual(const Parent& o) const { return Parent::FuzzyEqual(o); }

  bool FuzzyEqual(const Matrix4x4TypedFlagged& o) const {
    if (mType == MatrixType::Identity && o.mType == MatrixType::Identity) {
      return true;
    }
    return Parent::FuzzyEqual(o);
  }

  Matrix4x4TypedFlagged& PreTranslate(Float aX, Float aY, Float aZ) {
    if (mType == MatrixType::Identity) {
      _41 = aX;
      _42 = aY;
      _43 = aZ;

      if (!aZ) {
        mType = MatrixType::Simple;
        return *this;
      }
      mType = MatrixType::Full;
      return *this;
    }

    Parent::PreTranslate(aX, aY, aZ);

    if (aZ != 0) {
      mType = MatrixType::Full;
    }

    return *this;
  }

  Matrix4x4TypedFlagged& PostTranslate(Float aX, Float aY, Float aZ) {
    if (mType == MatrixType::Identity) {
      _41 = aX;
      _42 = aY;
      _43 = aZ;

      if (!aZ) {
        mType = MatrixType::Simple;
        return *this;
      }
      mType = MatrixType::Full;
      return *this;
    }

    Parent::PostTranslate(aX, aY, aZ);

    if (aZ != 0) {
      mType = MatrixType::Full;
    }

    return *this;
  }

  Matrix4x4TypedFlagged& ChangeBasis(Float aX, Float aY, Float aZ) {
    // Translate to the origin before applying this matrix
    PreTranslate(-aX, -aY, -aZ);

    // Translate back into position after applying this matrix
    PostTranslate(aX, aY, aZ);

    return *this;
  }

  bool IsIdentity() const { return mType == MatrixType::Identity; }

  template <class F>
  Point4DTyped<TargetUnits, F> ProjectPoint(
      const PointTyped<SourceUnits, F>& aPoint) const {
    if (mType == MatrixType::Identity) {
      return Point4DTyped<TargetUnits, F>(aPoint.x, aPoint.y, 0, 1);
    }

    if (mType == MatrixType::Simple) {
      PointTyped<TargetUnits, F> point = TransformPointSimple(aPoint);
      return Point4DTyped<TargetUnits, F>(point.x, point.y, 0, 1);
    }

    return Parent::ProjectPoint(aPoint);
  }

  Matrix4x4TypedFlagged& ProjectTo2D() {
    if (mType == MatrixType::Full) {
      Parent::ProjectTo2D();
    }
    return *this;
  }

  bool IsSingular() const {
    if (mType == MatrixType::Identity) {
      return false;
    }
    return Parent::Determinant() == 0.0;
  }

  bool Invert() {
    if (mType == MatrixType::Identity) {
      return true;
    }

    return Parent::Invert();
  }

  Matrix4x4TypedFlagged<TargetUnits, SourceUnits> Inverse() const {
    typedef Matrix4x4TypedFlagged<TargetUnits, SourceUnits> InvertedMatrix;
    InvertedMatrix clone = Cast<InvertedMatrix>();
    if (mType == MatrixType::Identity) {
      return clone;
    }
    DebugOnly<bool> inverted = clone.Invert();
    MOZ_ASSERT(inverted,
               "Attempted to get the inverse of a non-invertible matrix");

    // Inverting a 2D Matrix should result in a 2D matrix, ergo mType doesn't
    // change.
    return clone;
  }

  Maybe<Matrix4x4TypedFlagged<TargetUnits, SourceUnits>> MaybeInverse() const {
    typedef Matrix4x4TypedFlagged<TargetUnits, SourceUnits> InvertedMatrix;
    InvertedMatrix clone = Cast<InvertedMatrix>();
    if (clone.Invert()) {
      return Some(clone);
    }
    return Nothing();
  }

  template <typename NewTargetUnits>
  bool operator==(
      const Matrix4x4TypedFlagged<TargetUnits, NewTargetUnits>& aMatrix) const {
    if (mType == MatrixType::Identity &&
        aMatrix.mType == MatrixType::Identity) {
      return true;
    }
    // Depending on the usage it may make sense to compare more flags.
    return Parent::operator==(aMatrix);
  }

  template <typename NewTargetUnits>
  bool operator!=(
      const Matrix4x4TypedFlagged<TargetUnits, NewTargetUnits>& aMatrix) const {
    if (mType == MatrixType::Identity &&
        aMatrix.mType == MatrixType::Identity) {
      return false;
    }
    // Depending on the usage it may make sense to compare more flags.
    return Parent::operator!=(aMatrix);
  }

  template <typename NewTargetUnits>
  Matrix4x4TypedFlagged<SourceUnits, NewTargetUnits> operator*(
      const Matrix4x4Typed<TargetUnits, NewTargetUnits>& aMatrix) const {
    if (mType == MatrixType::Identity) {
      return aMatrix;
    }

    if (mType == MatrixType::Simple) {
      Matrix4x4TypedFlagged<SourceUnits, NewTargetUnits> matrix;
      matrix._11 = _11 * aMatrix._11 + _12 * aMatrix._21;
      matrix._21 = _21 * aMatrix._11 + _22 * aMatrix._21;
      matrix._31 = aMatrix._31;
      matrix._41 = _41 * aMatrix._11 + _42 * aMatrix._21 + aMatrix._41;
      matrix._12 = _11 * aMatrix._12 + _12 * aMatrix._22;
      matrix._22 = _21 * aMatrix._12 + _22 * aMatrix._22;
      matrix._32 = aMatrix._32;
      matrix._42 = _41 * aMatrix._12 + _42 * aMatrix._22 + aMatrix._42;
      matrix._13 = _11 * aMatrix._13 + _12 * aMatrix._23;
      matrix._23 = _21 * aMatrix._13 + _22 * aMatrix._23;
      matrix._33 = aMatrix._33;
      matrix._43 = _41 * aMatrix._13 + _42 * aMatrix._23 + aMatrix._43;
      matrix._14 = _11 * aMatrix._14 + _12 * aMatrix._24;
      matrix._24 = _21 * aMatrix._14 + _22 * aMatrix._24;
      matrix._34 = aMatrix._34;
      matrix._44 = _41 * aMatrix._14 + _42 * aMatrix._24 + aMatrix._44;
      matrix.Analyze();
      return matrix;
    }

    return Parent::operator*(aMatrix);
  }

  template <typename NewTargetUnits>
  Matrix4x4TypedFlagged<SourceUnits, NewTargetUnits> operator*(
      const Matrix4x4TypedFlagged<TargetUnits, NewTargetUnits>& aMatrix) const {
    if (mType == MatrixType::Identity) {
      return aMatrix;
    }

    if (aMatrix.mType == MatrixType::Identity) {
      return Cast<Matrix4x4TypedFlagged<SourceUnits, NewTargetUnits>>();
    }

    if (mType == MatrixType::Simple && aMatrix.mType == MatrixType::Simple) {
      Matrix4x4TypedFlagged<SourceUnits, NewTargetUnits> matrix;
      matrix._11 = _11 * aMatrix._11 + _12 * aMatrix._21;
      matrix._21 = _21 * aMatrix._11 + _22 * aMatrix._21;
      matrix._41 = _41 * aMatrix._11 + _42 * aMatrix._21 + aMatrix._41;
      matrix._12 = _11 * aMatrix._12 + _12 * aMatrix._22;
      matrix._22 = _21 * aMatrix._12 + _22 * aMatrix._22;
      matrix._42 = _41 * aMatrix._12 + _42 * aMatrix._22 + aMatrix._42;
      matrix.mType = MatrixType::Simple;
      return matrix;
    } else if (mType == MatrixType::Simple) {
      Matrix4x4TypedFlagged<SourceUnits, NewTargetUnits> matrix;
      matrix._11 = _11 * aMatrix._11 + _12 * aMatrix._21;
      matrix._21 = _21 * aMatrix._11 + _22 * aMatrix._21;
      matrix._31 = aMatrix._31;
      matrix._41 = _41 * aMatrix._11 + _42 * aMatrix._21 + aMatrix._41;
      matrix._12 = _11 * aMatrix._12 + _12 * aMatrix._22;
      matrix._22 = _21 * aMatrix._12 + _22 * aMatrix._22;
      matrix._32 = aMatrix._32;
      matrix._42 = _41 * aMatrix._12 + _42 * aMatrix._22 + aMatrix._42;
      matrix._13 = _11 * aMatrix._13 + _12 * aMatrix._23;
      matrix._23 = _21 * aMatrix._13 + _22 * aMatrix._23;
      matrix._33 = aMatrix._33;
      matrix._43 = _41 * aMatrix._13 + _42 * aMatrix._23 + aMatrix._43;
      matrix._14 = _11 * aMatrix._14 + _12 * aMatrix._24;
      matrix._24 = _21 * aMatrix._14 + _22 * aMatrix._24;
      matrix._34 = aMatrix._34;
      matrix._44 = _41 * aMatrix._14 + _42 * aMatrix._24 + aMatrix._44;
      matrix.mType = MatrixType::Full;
      return matrix;
    } else if (aMatrix.mType == MatrixType::Simple) {
      Matrix4x4TypedFlagged<SourceUnits, NewTargetUnits> matrix;
      matrix._11 = _11 * aMatrix._11 + _12 * aMatrix._21 + _14 * aMatrix._41;
      matrix._21 = _21 * aMatrix._11 + _22 * aMatrix._21 + _24 * aMatrix._41;
      matrix._31 = _31 * aMatrix._11 + _32 * aMatrix._21 + _34 * aMatrix._41;
      matrix._41 = _41 * aMatrix._11 + _42 * aMatrix._21 + _44 * aMatrix._41;
      matrix._12 = _11 * aMatrix._12 + _12 * aMatrix._22 + _14 * aMatrix._42;
      matrix._22 = _21 * aMatrix._12 + _22 * aMatrix._22 + _24 * aMatrix._42;
      matrix._32 = _31 * aMatrix._12 + _32 * aMatrix._22 + _34 * aMatrix._42;
      matrix._42 = _41 * aMatrix._12 + _42 * aMatrix._22 + _44 * aMatrix._42;
      matrix._13 = _13;
      matrix._23 = _23;
      matrix._33 = _33;
      matrix._43 = _43;
      matrix._14 = _14;
      matrix._24 = _24;
      matrix._34 = _34;
      matrix._44 = _44;
      matrix.mType = MatrixType::Full;
      return matrix;
    }

    return Parent::operator*(aMatrix);
  }

  bool Is2D() const { return mType != MatrixType::Full; }

  bool CanDraw2D(Matrix* aMatrix = nullptr) const {
    if (mType != MatrixType::Full) {
      if (aMatrix) {
        aMatrix->_11 = _11;
        aMatrix->_12 = _12;
        aMatrix->_21 = _21;
        aMatrix->_22 = _22;
        aMatrix->_31 = _41;
        aMatrix->_32 = _42;
      }
      return true;
    }
    return Parent::CanDraw2D(aMatrix);
  }

  bool Is2D(Matrix* aMatrix) const {
    if (!Is2D()) {
      return false;
    }
    if (aMatrix) {
      aMatrix->_11 = _11;
      aMatrix->_12 = _12;
      aMatrix->_21 = _21;
      aMatrix->_22 = _22;
      aMatrix->_31 = _41;
      aMatrix->_32 = _42;
    }
    return true;
  }

  template <class F>
  RectTyped<TargetUnits, F> ProjectRectBounds(
      const RectTyped<SourceUnits, F>& aRect,
      const RectTyped<TargetUnits, F>& aClip) const {
    return Parent::ProjectRectBounds(aRect, aClip);
  }

  const Parent& GetMatrix() const { return *this; }

  Matrix4x4Flagged ToUnknownMatrix() const {
    return Matrix4x4Flagged{_11, _12, _13, _14, _21, _22, _23, _24,  _31,
                            _32, _33, _34, _41, _42, _43, _44, mType};
  }

  static Matrix4x4TypedFlagged FromUnknownMatrix(
      const Matrix4x4Flagged& aUnknown) {
    return Matrix4x4TypedFlagged{
        aUnknown._11, aUnknown._12,  aUnknown._13, aUnknown._14, aUnknown._21,
        aUnknown._22, aUnknown._23,  aUnknown._24, aUnknown._31, aUnknown._32,
        aUnknown._33, aUnknown._34,  aUnknown._41, aUnknown._42, aUnknown._43,
        aUnknown._44, aUnknown.mType};
  }

 private:
  Matrix4x4TypedFlagged(Float a11, Float a12, Float a13, Float a14, Float a21,
                        Float a22, Float a23, Float a24, Float a31, Float a32,
                        Float a33, Float a34, Float a41, Float a42, Float a43,
                        Float a44, const MatrixType aType)
      : Parent(a11, a12, a13, a14, a21, a22, a23, a24, a31, a32, a33, a34, a41,
               a42, a43, a44),
        mType(aType) {}

  template <class F>
  PointTyped<TargetUnits, F> TransformPointSimple(
      const PointTyped<SourceUnits, F>& aPoint) const {
    PointTyped<SourceUnits, F> temp;
    temp.x = aPoint.x * _11 + aPoint.y * _21 + _41;
    temp.y = aPoint.x * _12 + aPoint.y * _22 + _42;
    return temp;
  }

  void Analyze() {
    if (Parent::IsIdentity()) {
      mType = MatrixType::Identity;
      return;
    }

    if (Parent::Is2D()) {
      mType = MatrixType::Simple;
      return;
    }

    mType = MatrixType::Full;
  }

  MatrixType mType;

  template <typename, typename>
  friend class Matrix4x4TypedFlagged;
};

using Matrix4x4Flagged = Matrix4x4TypedFlagged<UnknownUnits, UnknownUnits>;

}  // namespace gfx
}  // namespace mozilla

#endif /* MOZILLA_GFX_MATRIX_H_ */
