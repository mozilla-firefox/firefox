/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DOMSVGTransform.h"

#include "SVGAnimatedTransformList.h"
#include "SVGAttrTearoffTable.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/dom/DOMMatrix.h"
#include "mozilla/dom/DOMMatrixBinding.h"
#include "mozilla/dom/SVGMatrix.h"
#include "mozilla/dom/SVGTransformBinding.h"
#include "nsError.h"

namespace mozilla::dom {

using namespace SVGTransform_Binding;

static SVGAttrTearoffTable<DOMSVGTransform, SVGMatrix>&
SVGMatrixTearoffTable() {
  static SVGAttrTearoffTable<DOMSVGTransform, SVGMatrix> sSVGMatrixTearoffTable;
  return sSVGMatrixTearoffTable;
}

//----------------------------------------------------------------------

// We could use NS_IMPL_CYCLE_COLLECTION(, except that in Unlink() we need to
// clear our list's weak ref to us to be safe. (The other option would be to
// not unlink and rely on the breaking of the other edges in the cycle, as
// NS_SVG_VAL_IMPL_CYCLE_COLLECTION does.)
NS_IMPL_CYCLE_COLLECTION_CLASS(DOMSVGTransform)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(DOMSVGTransform)
  // We may not belong to a list, so we must null check tmp->mList.
  if (tmp->mList) {
    tmp->mList->mItems[tmp->mListIndex] = nullptr;
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mList)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(DOMSVGTransform)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mList)
  SVGMatrix* matrix = SVGMatrixTearoffTable().GetTearoff(tmp);
  CycleCollectionNoteChild(cb, matrix, "matrix");
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(DOMSVGTransform)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

JSObject* DOMSVGTransform::WrapObject(JSContext* aCx,
                                      JS::Handle<JSObject*> aGivenProto) {
  return SVGTransform_Binding::Wrap(aCx, this, aGivenProto);
}

//----------------------------------------------------------------------
// Ctors:

DOMSVGTransform::DOMSVGTransform(DOMSVGTransformList* aList,
                                 uint32_t aListIndex, bool aIsAnimValItem)
    : mList(aList),
      mListIndex(aListIndex),
      mIsAnimValItem(aIsAnimValItem),
      mTransform(nullptr) {
  // These shifts are in sync with the members in the header.
  MOZ_ASSERT(aList && aListIndex <= MaxListIndex(), "bad arg");

  MOZ_ASSERT(IndexIsValid(), "Bad index for DOMSVGNumber!");
}

DOMSVGTransform::DOMSVGTransform()
    : mList(nullptr),
      mListIndex(0),
      mIsAnimValItem(false),
      mTransform(new SVGTransform())  // Default ctor for objects not in a
                                      // list initialises to matrix type with
                                      // identity matrix
{}

DOMSVGTransform::DOMSVGTransform(const gfxMatrix& aMatrix)
    : mList(nullptr),
      mListIndex(0),
      mIsAnimValItem(false),
      mTransform(new SVGTransform(aMatrix)) {}

DOMSVGTransform::DOMSVGTransform(const DOMMatrix2DInit& aMatrix,
                                 ErrorResult& aRv)
    : mList(nullptr),
      mListIndex(0),
      mIsAnimValItem(false),
      mTransform(new SVGTransform()) {
  SetMatrix(aMatrix, aRv);
}

DOMSVGTransform::DOMSVGTransform(const SVGTransform& aTransform)
    : mList(nullptr),
      mListIndex(0),
      mIsAnimValItem(false),
      mTransform(new SVGTransform(aTransform)) {}

DOMSVGTransform::~DOMSVGTransform() {
  SVGMatrix* matrix = SVGMatrixTearoffTable().GetTearoff(this);
  if (matrix) {
    SVGMatrixTearoffTable().RemoveTearoff(this);
    NS_RELEASE(matrix);
  }
  // Our mList's weak ref to us must be nulled out when we die. If GC has
  // unlinked us using the cycle collector code, then that has already
  // happened, and mList is null.
  if (mList) {
    mList->mItems[mListIndex] = nullptr;
  }
}

uint16_t DOMSVGTransform::Type() const { return Transform().Type(); }

SVGMatrix* DOMSVGTransform::GetMatrix() {
  SVGMatrix* wrapper = SVGMatrixTearoffTable().GetTearoff(this);
  if (!wrapper) {
    NS_ADDREF(wrapper = new SVGMatrix(*this));
    SVGMatrixTearoffTable().AddTearoff(this, wrapper);
  }
  return wrapper;
}

float DOMSVGTransform::Angle() const { return Transform().Angle(); }

void DOMSVGTransform::SetMatrix(const DOMMatrix2DInit& aMatrix,
                                ErrorResult& aRv) {
  if (mIsAnimValItem) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }
  auto matrix2D = DOMMatrixReadOnly::ToValidatedMatrixDouble(aMatrix, aRv);
  if (aRv.Failed()) {
    return;
  }
  if (!matrix2D.IsFinite()) {
    aRv.ThrowTypeError<MSG_NOT_FINITE>("Matrix setter");
    return;
  }
  SetMatrix(matrix2D);
}

void DOMSVGTransform::SetTranslate(float tx, float ty, ErrorResult& aRv) {
  if (mIsAnimValItem) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }

  if (Transform().Type() == SVG_TRANSFORM_TRANSLATE && Matrixgfx()._31 == tx &&
      Matrixgfx()._32 == ty) {
    return;
  }

  AutoChangeTransformListNotifier notifier(this);
  Transform().SetTranslate(tx, ty);
}

void DOMSVGTransform::SetScale(float sx, float sy, ErrorResult& aRv) {
  if (mIsAnimValItem) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }

  if (Transform().Type() == SVG_TRANSFORM_SCALE && Matrixgfx()._11 == sx &&
      Matrixgfx()._22 == sy) {
    return;
  }
  AutoChangeTransformListNotifier notifier(this);
  Transform().SetScale(sx, sy);
}

void DOMSVGTransform::SetRotate(float angle, float cx, float cy,
                                ErrorResult& aRv) {
  if (mIsAnimValItem) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }

  if (Transform().Type() == SVG_TRANSFORM_ROTATE) {
    float currentCx, currentCy;
    Transform().GetRotationOrigin(currentCx, currentCy);
    if (Transform().Angle() == angle && currentCx == cx && currentCy == cy) {
      return;
    }
  }

  AutoChangeTransformListNotifier notifier(this);
  Transform().SetRotate(angle, cx, cy);
}

void DOMSVGTransform::SetSkewX(float angle, ErrorResult& aRv) {
  if (mIsAnimValItem) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }

  if (Transform().Type() == SVG_TRANSFORM_SKEWX &&
      Transform().Angle() == angle) {
    return;
  }

  if (!std::isfinite(tan(angle * kRadPerDegree))) {
    aRv.ThrowRangeError<MSG_INVALID_TRANSFORM_ANGLE_ERROR>();
    return;
  }

  AutoChangeTransformListNotifier notifier(this);
  DebugOnly<nsresult> result = Transform().SetSkewX(angle);
  MOZ_ASSERT(NS_SUCCEEDED(result), "SetSkewX unexpectedly failed");
}

void DOMSVGTransform::SetSkewY(float angle, ErrorResult& aRv) {
  if (mIsAnimValItem) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }

  if (Transform().Type() == SVG_TRANSFORM_SKEWY &&
      Transform().Angle() == angle) {
    return;
  }

  if (!std::isfinite(tan(angle * kRadPerDegree))) {
    aRv.ThrowRangeError<MSG_INVALID_TRANSFORM_ANGLE_ERROR>();
    return;
  }

  AutoChangeTransformListNotifier notifier(this);
  DebugOnly<nsresult> result = Transform().SetSkewY(angle);
  MOZ_ASSERT(NS_SUCCEEDED(result), "SetSkewY unexpectedly failed");
}

//----------------------------------------------------------------------
// List management methods:

void DOMSVGTransform::InsertingIntoList(DOMSVGTransformList* aList,
                                        uint32_t aListIndex,
                                        bool aIsAnimValItem) {
  MOZ_ASSERT(!HasOwner(), "Inserting item that is already in a list");

  mList = aList;
  mListIndex = aListIndex;
  mIsAnimValItem = aIsAnimValItem;
  mTransform = nullptr;

  MOZ_ASSERT(IndexIsValid(), "Bad index for DOMSVGLength!");
}

void DOMSVGTransform::RemovingFromList() {
  MOZ_ASSERT(!mTransform,
             "Item in list also has another non-list value associated with it");

  mTransform = std::make_unique<SVGTransform>(InternalItem());
  mList = nullptr;
  mIsAnimValItem = false;
}

SVGTransform& DOMSVGTransform::InternalItem() {
  SVGAnimatedTransformList* alist =
      Element()->GetExistingAnimatedTransformList();
  return mIsAnimValItem && alist->mAnimVal ? (*alist->mAnimVal)[mListIndex]
                                           : alist->mBaseVal[mListIndex];
}

const SVGTransform& DOMSVGTransform::InternalItem() const {
  return const_cast<DOMSVGTransform*>(this)->InternalItem();
}

#ifdef DEBUG
bool DOMSVGTransform::IndexIsValid() {
  SVGAnimatedTransformList* alist =
      Element()->GetExistingAnimatedTransformList();
  return (mIsAnimValItem && mListIndex < alist->GetAnimValue().Length()) ||
         (!mIsAnimValItem && mListIndex < alist->GetBaseValue().Length());
}
#endif  // DEBUG

//----------------------------------------------------------------------
// Interface for SVGMatrix's use

void DOMSVGTransform::SetMatrix(const gfxMatrix& aMatrix) {
  MOZ_ASSERT(!mIsAnimValItem, "Attempting to modify read-only transform");

  if (Transform().Type() == SVG_TRANSFORM_MATRIX &&
      SVGTransform::MatricesEqual(Matrixgfx(), aMatrix)) {
    return;
  }

  AutoChangeTransformListNotifier notifier(this);
  Transform().SetMatrix(aMatrix);
}

}  // namespace mozilla::dom
