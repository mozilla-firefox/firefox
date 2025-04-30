/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DOMSVGPoint.h"

#include "DOMSVGPointList.h"
#include "gfx2DGlue.h"
#include "nsError.h"
#include "mozilla/dom/DOMMatrix.h"
#include "mozilla/dom/SVGPointBinding.h"

// See the architecture comment in DOMSVGPointList.h.

using namespace mozilla::gfx;

namespace mozilla::dom {

//----------------------------------------------------------------------
// Helper class: AutoChangePointNotifier
//
class MOZ_RAII AutoChangePointNotifier {
 public:
  explicit AutoChangePointNotifier(DOMSVGPoint* aValue) : mValue(aValue) {
    MOZ_ASSERT(mValue, "Expecting non-null value");
  }

  ~AutoChangePointNotifier() {
    if (mValue->IsTranslatePoint()) {
      mValue->DidChangeTranslate();
    }
  }

 private:
  DOMSVGPoint* const mValue;
};

MOZ_CONSTINIT static SVGAttrTearoffTable<SVGPoint, DOMSVGPoint>
    sSVGTranslateTearOffTable;

// We could use NS_IMPL_CYCLE_COLLECTION(, except that in Unlink() we need to
// clear our list's weak ref to us to be safe. (The other option would be to
// not unlink and rely on the breaking of the other edges in the cycle, as
// NS_SVG_VAL_IMPL_CYCLE_COLLECTION does.)
NS_IMPL_CYCLE_COLLECTION_CLASS(DOMSVGPoint)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(DOMSVGPoint)
  tmp->CleanupWeakRefs();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOwner)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(DOMSVGPoint)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOwner)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(DOMSVGPoint)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

JSObject* DOMSVGPoint::WrapObject(JSContext* aCx,
                                  JS::Handle<JSObject*> aGivenProto) {
  return SVGPoint_Binding::Wrap(aCx, this, aGivenProto);
}

float DOMSVGPoint::X() {
  if (mIsAnimValItem && IsInList()) {
    Element()->FlushAnimations();  // May make IsInList() == false
  }
  return InternalItem().mX;
}

void DOMSVGPoint::SetX(float aX, ErrorResult& aRv) {
  if (mIsAnimValItem) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }

  auto& val = InternalItem();

  if (val.mX == aX) {
    return;
  }

  AutoChangePointListNotifier listNotifier(this);
  AutoChangePointNotifier translateNotifier(this);

  val.mX = aX;
}

float DOMSVGPoint::Y() {
  if (mIsAnimValItem && IsInList()) {
    Element()->FlushAnimations();  // May make IsInList() == false
  }
  return InternalItem().mY;
}

void DOMSVGPoint::SetY(float aY, ErrorResult& aRv) {
  if (mIsAnimValItem) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }
  auto& val = InternalItem();

  if (val.mY == aY) {
    return;
  }

  AutoChangePointListNotifier listNotifier(this);
  AutoChangePointNotifier translateNotifier(this);

  val.mY = aY;
}

already_AddRefed<DOMSVGPoint> DOMSVGPoint::MatrixTransform(
    const DOMMatrix2DInit& aMatrix, ErrorResult& aRv) {
  RefPtr<DOMMatrixReadOnly> matrix =
      DOMMatrixReadOnly::FromMatrix(GetParentObject(), aMatrix, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  const auto* matrix2D = matrix->GetInternal2D();
  if (!matrix2D->IsFinite()) {
    aRv.ThrowTypeError<MSG_NOT_FINITE>("MatrixTransform matrix");
    return nullptr;
  }
  auto pt = matrix2D->TransformPoint(InternalItem());
  return do_AddRef(new DOMSVGPoint(ToPoint(pt)));
}

void DOMSVGPoint::InsertingIntoList(DOMSVGPointList* aList, uint32_t aListIndex,
                                    bool aIsAnimValItem) {
  MOZ_RELEASE_ASSERT(!IsInList(), "Inserting item that is already in a list");
  MOZ_RELEASE_ASSERT(!mIsTranslatePoint,
                     "Inserting item that is a currentTranslate");

  delete mVal;
  mVal = nullptr;

  mOwner = aList;
  mListIndex = aListIndex;
  mIsAnimValItem = aIsAnimValItem;

  MOZ_ASSERT(IndexIsValid(), "Bad index for DOMSVGPoint!");
}

void DOMSVGPoint::RemovingFromList() {
  MOZ_ASSERT(
      IsInList(),
      "We should start in a list if we're going to be removed from one.");
  mVal = new SVGPoint(InternalItem());
  mOwner = nullptr;
  mIsAnimValItem = false;
}

SVGPoint& DOMSVGPoint::InternalItem() {
  if (nsCOMPtr<DOMSVGPointList> pointList = do_QueryInterface(mOwner)) {
    return pointList->InternalList().mItems[mListIndex];
  }
  return *mVal;
}

already_AddRefed<DOMSVGPoint> DOMSVGPoint::GetTranslateTearOff(
    SVGPoint* aVal, SVGSVGElement* aSVGSVGElement) {
  RefPtr<DOMSVGPoint> domPoint = sSVGTranslateTearOffTable.GetTearoff(aVal);
  if (!domPoint) {
    domPoint = new DOMSVGPoint(aVal, aSVGSVGElement);
    sSVGTranslateTearOffTable.AddTearoff(aVal, domPoint);
    domPoint->mIsInTearoffTable = true;
  }

  return domPoint.forget();
}

bool DOMSVGPoint::AttrIsAnimating() const {
  nsCOMPtr<DOMSVGPointList> pointList = do_QueryInterface(mOwner);
  return pointList && pointList->AttrIsAnimating();
}

void DOMSVGPoint::DidChangeTranslate() {
  nsCOMPtr<SVGSVGElement> svg = do_QueryInterface(mOwner);
  MOZ_ASSERT(svg);
  nsContentUtils::AddScriptRunner(
      NewRunnableMethod("dom::SVGSVGElement::DidChangeTranslate", svg,
                        &SVGSVGElement::DidChangeTranslate));
}

SVGElement* DOMSVGPoint::Element() {
  if (nsCOMPtr<DOMSVGPointList> pointList = do_QueryInterface(mOwner)) {
    return pointList->Element();
  }
  nsCOMPtr<SVGSVGElement> svg = do_QueryInterface(mOwner);
  return svg;
}

void DOMSVGPoint::CleanupWeakRefs() {
  // Our mList's weak ref to us must be nulled out when we die (or when we're
  // cycle collected), so we that don't leave behind a pointer to
  // free / soon-to-be-free memory.
  if (nsCOMPtr<DOMSVGPointList> pointList = do_QueryInterface(mOwner)) {
    MOZ_ASSERT(pointList->mItems[mListIndex] == this,
               "Clearing out the wrong list index...?");
    pointList->mItems[mListIndex] = nullptr;
  }

  if (mIsInTearoffTable) {
    // Similarly, we must update the tearoff table to remove its (non-owning)
    // pointer to mVal.
    MOZ_ASSERT(mVal && mIsTranslatePoint,
               "Tearoff table should only be used for translate-point objects "
               "with non-null mVal (see GetTranslateTearOff and its callers)");
    sSVGTranslateTearOffTable.RemoveTearoff(mVal);
    mIsInTearoffTable = false;
  }

  if (mVal) {
    if (!mIsTranslatePoint) {
      // In this case we own mVal
      delete mVal;
    }
    mVal = nullptr;
  }
}

#ifdef DEBUG
bool DOMSVGPoint::IndexIsValid() {
  nsCOMPtr<DOMSVGPointList> pointList = do_QueryInterface(mOwner);
  return mListIndex < pointList->InternalList().Length();
}
#endif

}  // namespace mozilla::dom
