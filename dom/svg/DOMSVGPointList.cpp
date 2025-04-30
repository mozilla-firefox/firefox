/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DOMSVGPointList.h"

#include "nsContentUtils.h"
#include "DOMSVGPoint.h"
#include "nsError.h"
#include "SVGAnimatedPointList.h"
#include "SVGAttrTearoffTable.h"
#include "SVGPolyElement.h"
#include "mozilla/dom/SVGElement.h"
#include "mozilla/dom/SVGPointListBinding.h"
#include <algorithm>

// See the comment in this file's header.

// local helper functions
namespace {

void UpdateListIndicesFromIndex(
    FallibleTArray<mozilla::dom::DOMSVGPoint*>& aItemsArray,
    uint32_t aStartingIndex) {
  uint32_t length = aItemsArray.Length();

  for (uint32_t i = aStartingIndex; i < length; ++i) {
    if (aItemsArray[i]) {
      aItemsArray[i]->UpdateListIndex(i);
    }
  }
}

}  // namespace

namespace mozilla::dom {

static inline SVGAttrTearoffTable<void, DOMSVGPointList>&
SVGPointListTearoffTable() {
  static SVGAttrTearoffTable<void, DOMSVGPointList> sSVGPointListTearoffTable;
  return sSVGPointListTearoffTable;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(DOMSVGPointList)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(DOMSVGPointList)
  // No unlinking of mElement, we'd need to null out the value pointer (the
  // object it points to is held by the element) and null-check it everywhere.
  tmp->RemoveFromTearoffTable();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(DOMSVGPointList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mElement)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(DOMSVGPointList)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(DOMSVGPointList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(DOMSVGPointList)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DOMSVGPointList)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(DOMSVGPointList)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

/* static */
already_AddRefed<DOMSVGPointList> DOMSVGPointList::GetDOMWrapper(
    void* aList, SVGPolyElement* aElement) {
  RefPtr<DOMSVGPointList> wrapper =
      SVGPointListTearoffTable().GetTearoff(aList);
  if (!wrapper) {
    wrapper = new DOMSVGPointList(
        aElement, aElement->GetAnimatedPointList()->GetAnimValKey() == aList);
    SVGPointListTearoffTable().AddTearoff(aList, wrapper);
  }
  return wrapper.forget();
}

/* static */
DOMSVGPointList* DOMSVGPointList::GetDOMWrapperIfExists(void* aList) {
  return SVGPointListTearoffTable().GetTearoff(aList);
}

void DOMSVGPointList::RemoveFromTearoffTable() {
  // Called from Unlink and the destructor.
  //
  // There are now no longer any references to us held by script or list items.
  // Note we must use GetAnimValKey/GetBaseValKey here, NOT InternalList()!
  if (mIsInTearoffTable) {
    void* key = mIsAnimValList ? InternalAList().GetAnimValKey()
                               : InternalAList().GetBaseValKey();
    SVGPointListTearoffTable().RemoveTearoff(key);
    mIsInTearoffTable = false;
  }
}

DOMSVGPointList::~DOMSVGPointList() { RemoveFromTearoffTable(); }

JSObject* DOMSVGPointList::WrapObject(JSContext* cx,
                                      JS::Handle<JSObject*> aGivenProto) {
  return mozilla::dom::SVGPointList_Binding::Wrap(cx, this, aGivenProto);
}

void DOMSVGPointList::InternalListWillChangeTo(const SVGPointList& aNewValue) {
  // When the number of items in our internal counterpart changes, we MUST stay
  // in sync. Everything in the scary comment in
  // DOMSVGLengthList::InternalBaseValListWillChangeTo applies here too!

  uint32_t oldLength = mItems.Length();

  uint32_t newLength = aNewValue.Length();
  if (newLength > DOMSVGPoint::MaxListIndex()) {
    // It's safe to get out of sync with our internal list as long as we have
    // FEWER items than it does.
    newLength = DOMSVGPoint::MaxListIndex();
  }

  RefPtr<DOMSVGPointList> kungFuDeathGrip;
  if (newLength < oldLength) {
    // RemovingFromList() might clear last reference to |this|.
    // Retain a temporary reference to keep from dying before returning.
    kungFuDeathGrip = this;
  }

  // If our length will decrease, notify the items that will be removed:
  for (uint32_t i = newLength; i < oldLength; ++i) {
    if (mItems[i]) {
      mItems[i]->RemovingFromList();
    }
  }

  if (!mItems.SetLength(newLength, fallible)) {
    // We silently ignore SetLength OOM failure since being out of sync is safe
    // so long as we have *fewer* items than our internal list.
    mItems.Clear();
    return;
  }

  // If our length has increased, null out the new pointers:
  for (uint32_t i = oldLength; i < newLength; ++i) {
    mItems[i] = nullptr;
  }
}

bool DOMSVGPointList::AttrIsAnimating() const {
  return InternalAList().IsAnimating();
}

bool DOMSVGPointList::AnimListMirrorsBaseList() const {
  return GetDOMWrapperIfExists(InternalAList().GetAnimValKey()) &&
         !AttrIsAnimating();
}

SVGPointList& DOMSVGPointList::InternalList() const {
  SVGAnimatedPointList* alist = mElement->GetAnimatedPointList();
  return mIsAnimValList && alist->IsAnimating() ? *alist->mAnimVal
                                                : alist->mBaseVal;
}

SVGAnimatedPointList& DOMSVGPointList::InternalAList() const {
  MOZ_ASSERT(mElement->GetAnimatedPointList(), "Internal error");
  return *mElement->GetAnimatedPointList();
}

// ----------------------------------------------------------------------------
// nsIDOMSVGPointList implementation:

void DOMSVGPointList::Clear(ErrorResult& aRv) {
  if (IsAnimValList()) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }

  if (LengthNoFlush() > 0) {
    AutoChangePointListNotifier notifier(this);
    // DOM list items that are to be removed must be removed before we change
    // the internal list, otherwise they wouldn't be able to copy their
    // internal counterparts' values!

    InternalListWillChangeTo(SVGPointList());  // clears mItems

    if (!AttrIsAnimating()) {
      // The anim val list is in sync with the base val list
      DOMSVGPointList* animList =
          GetDOMWrapperIfExists(InternalAList().GetAnimValKey());
      if (animList) {
        animList->InternalListWillChangeTo(
            SVGPointList());  // clears its mItems
      }
    }

    InternalList().Clear();
  }
}

already_AddRefed<DOMSVGPoint> DOMSVGPointList::Initialize(DOMSVGPoint& aNewItem,
                                                          ErrorResult& aRv) {
  if (IsAnimValList()) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return nullptr;
  }

  // If aNewItem is already in a list we should insert a clone of aNewItem,
  // and for consistency, this should happen even if *this* is the list that
  // aNewItem is currently in. Note that in the case of aNewItem being in this
  // list, the Clear() call before the InsertItemBefore() call would remove it
  // from this list, and so the InsertItemBefore() call would not insert a
  // clone of aNewItem, it would actually insert aNewItem. To prevent that
  // from happening we have to do the clone here, if necessary.

  RefPtr<DOMSVGPoint> domItem = &aNewItem;
  if (domItem->HasOwner()) {
    domItem = domItem->Copy();  // must do this before changing anything!
  }

  ErrorResult rv;
  Clear(rv);
  MOZ_ASSERT(!rv.Failed());
  return InsertItemBefore(*domItem, 0, aRv);
}

already_AddRefed<DOMSVGPoint> DOMSVGPointList::GetItem(uint32_t index,
                                                       ErrorResult& aRv) {
  bool found;
  RefPtr<DOMSVGPoint> item = IndexedGetter(index, found, aRv);
  if (!found) {
    aRv.ThrowIndexSizeError("Index out of range");
  }
  return item.forget();
}

already_AddRefed<DOMSVGPoint> DOMSVGPointList::IndexedGetter(uint32_t aIndex,
                                                             bool& aFound,
                                                             ErrorResult& aRv) {
  if (IsAnimValList()) {
    Element()->FlushAnimations();
  }
  aFound = aIndex < LengthNoFlush();
  if (aFound) {
    return GetItemAt(aIndex);
  }
  return nullptr;
}

already_AddRefed<DOMSVGPoint> DOMSVGPointList::InsertItemBefore(
    DOMSVGPoint& aNewItem, uint32_t aIndex, ErrorResult& aRv) {
  if (IsAnimValList()) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return nullptr;
  }

  aIndex = std::min(aIndex, LengthNoFlush());
  if (aIndex >= DOMSVGPoint::MaxListIndex()) {
    aRv.ThrowIndexSizeError("Index out of range");
    return nullptr;
  }

  RefPtr<DOMSVGPoint> domItem = &aNewItem;
  if (domItem->HasOwner()) {
    domItem = domItem->Copy();  // must do this before changing anything!
  }

  // Ensure we have enough memory so we can avoid complex error handling below:
  if (!mItems.SetCapacity(mItems.Length() + 1, fallible) ||
      !InternalList().SetCapacity(InternalList().Length() + 1)) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return nullptr;
  }
  if (AnimListMirrorsBaseList()) {
    DOMSVGPointList* animVal =
        GetDOMWrapperIfExists(InternalAList().GetAnimValKey());
    MOZ_ASSERT(animVal, "animVal must be a valid pointer");
    if (!animVal->mItems.SetCapacity(animVal->mItems.Length() + 1, fallible)) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return nullptr;
    }
  }

  AutoChangePointListNotifier notifier(this);
  // Now that we know we're inserting, keep animVal list in sync as necessary.
  MaybeInsertNullInAnimValListAt(aIndex);

  InternalList().InsertItem(aIndex, domItem->ToSVGPoint());
  MOZ_ALWAYS_TRUE(mItems.InsertElementAt(aIndex, domItem, fallible));

  // This MUST come after the insertion into InternalList(), or else under the
  // insertion into InternalList() the values read from domItem would be bad
  // data from InternalList() itself!:
  domItem->InsertingIntoList(this, aIndex, IsAnimValList());

  UpdateListIndicesFromIndex(mItems, aIndex + 1);

  return domItem.forget();
}

already_AddRefed<DOMSVGPoint> DOMSVGPointList::ReplaceItem(
    DOMSVGPoint& aNewItem, uint32_t aIndex, ErrorResult& aRv) {
  if (IsAnimValList()) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return nullptr;
  }

  if (aIndex >= LengthNoFlush()) {
    aRv.ThrowIndexSizeError("Index out of range");
    return nullptr;
  }

  RefPtr<DOMSVGPoint> domItem = &aNewItem;
  if (domItem->HasOwner()) {
    domItem = domItem->Copy();  // must do this before changing anything!
  }

  AutoChangePointListNotifier notifier(this);
  if (mItems[aIndex]) {
    // Notify any existing DOM item of removal *before* modifying the lists so
    // that the DOM item can copy the *old* value at its index:
    mItems[aIndex]->RemovingFromList();
  }

  InternalList()[aIndex] = domItem->ToSVGPoint();
  mItems[aIndex] = domItem;

  // This MUST come after the ToSVGPoint() call, otherwise that call
  // would end up reading bad data from InternalList()!
  domItem->InsertingIntoList(this, aIndex, IsAnimValList());

  return domItem.forget();
}

already_AddRefed<DOMSVGPoint> DOMSVGPointList::RemoveItem(uint32_t aIndex,
                                                          ErrorResult& aRv) {
  if (IsAnimValList()) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return nullptr;
  }

  if (aIndex >= LengthNoFlush()) {
    aRv.ThrowIndexSizeError("Index out of range");
    return nullptr;
  }

  AutoChangePointListNotifier notifier(this);
  // Now that we know we're removing, keep animVal list in sync as necessary.
  // Do this *before* touching InternalList() so the removed item can get its
  // internal value.
  MaybeRemoveItemFromAnimValListAt(aIndex);

  // We have to return the removed item, so get it, creating it if necessary:
  RefPtr<DOMSVGPoint> result = GetItemAt(aIndex);

  // Notify the DOM item of removal *before* modifying the lists so that the
  // DOM item can copy its *old* value:
  mItems[aIndex]->RemovingFromList();

  InternalList().RemoveItem(aIndex);
  mItems.RemoveElementAt(aIndex);

  UpdateListIndicesFromIndex(mItems, aIndex);

  return result.forget();
}

already_AddRefed<DOMSVGPoint> DOMSVGPointList::GetItemAt(uint32_t aIndex) {
  MOZ_ASSERT(aIndex < mItems.Length());

  if (!mItems[aIndex]) {
    mItems[aIndex] = new DOMSVGPoint(this, aIndex, IsAnimValList());
  }
  RefPtr<DOMSVGPoint> result = mItems[aIndex];
  return result.forget();
}

void DOMSVGPointList::MaybeInsertNullInAnimValListAt(uint32_t aIndex) {
  MOZ_ASSERT(!IsAnimValList(), "call from baseVal to animVal");

  if (!AnimListMirrorsBaseList()) {
    return;
  }

  // The anim val list is in sync with the base val list
  DOMSVGPointList* animVal =
      GetDOMWrapperIfExists(InternalAList().GetAnimValKey());

  MOZ_ASSERT(animVal, "AnimListMirrorsBaseList() promised a non-null animVal");
  MOZ_ASSERT(animVal->mItems.Length() == mItems.Length(),
             "animVal list not in sync!");
  MOZ_ALWAYS_TRUE(animVal->mItems.InsertElementAt(aIndex, nullptr, fallible));

  UpdateListIndicesFromIndex(animVal->mItems, aIndex + 1);
}

void DOMSVGPointList::MaybeRemoveItemFromAnimValListAt(uint32_t aIndex) {
  MOZ_ASSERT(!IsAnimValList(), "call from baseVal to animVal");

  if (!AnimListMirrorsBaseList()) {
    return;
  }

  // This needs to be a strong reference; otherwise, the RemovingFromList call
  // below might drop the last reference to animVal before we're done with it.
  RefPtr<DOMSVGPointList> animVal =
      GetDOMWrapperIfExists(InternalAList().GetAnimValKey());

  MOZ_ASSERT(animVal, "AnimListMirrorsBaseList() promised a non-null animVal");
  MOZ_ASSERT(animVal->mItems.Length() == mItems.Length(),
             "animVal list not in sync!");

  if (animVal->mItems[aIndex]) {
    animVal->mItems[aIndex]->RemovingFromList();
  }
  animVal->mItems.RemoveElementAt(aIndex);

  UpdateListIndicesFromIndex(animVal->mItems, aIndex);
}

}  // namespace mozilla::dom
