/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DOMSVGStringList.h"

#include "mozAutoDocUpdate.h"
#include "mozilla/dom/SVGStringListBinding.h"
#include "mozilla/dom/SVGTests.h"
#include "nsCOMPtr.h"
#include "nsError.h"
#include "nsQueryObject.h"
#include "SVGAttrTearoffTable.h"
#include <algorithm>

// See the architecture comment in this file's header.

namespace mozilla::dom {

static inline SVGAttrTearoffTable<SVGStringList, DOMSVGStringList>&
SVGStringListTearoffTable() {
  static SVGAttrTearoffTable<SVGStringList, DOMSVGStringList>
      sSVGStringListTearoffTable;
  return sSVGStringListTearoffTable;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(DOMSVGStringList)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(DOMSVGStringList)
  // No unlinking of mElement, we'd need to null out the value pointer (the
  // object it points to is held by the element) and null-check it everywhere.
  tmp->RemoveFromTearoffTable();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(DOMSVGStringList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mElement)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(DOMSVGStringList)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(DOMSVGStringList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(DOMSVGStringList)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DOMSVGStringList)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

//----------------------------------------------------------------------
// Helper class: AutoChangeStringListNotifier
// Stack-based helper class to pair calls to WillChangeStringListList and
// DidChangeStringListList.
class MOZ_RAII AutoChangeStringListNotifier : public mozAutoDocUpdate {
 public:
  explicit AutoChangeStringListNotifier(DOMSVGStringList* aStringList)
      : mozAutoDocUpdate(aStringList->mElement->GetComposedDoc(), true),
        mStringList(aStringList) {
    MOZ_ASSERT(mStringList, "Expecting non-null stringList");
    mEmptyOrOldValue = mStringList->mElement->WillChangeStringList(
        mStringList->mIsConditionalProcessingAttribute, mStringList->mAttrEnum,
        *this);
  }

  ~AutoChangeStringListNotifier() {
    mStringList->mElement->DidChangeStringList(
        mStringList->mIsConditionalProcessingAttribute, mStringList->mAttrEnum,
        mEmptyOrOldValue, *this);
  }

 private:
  DOMSVGStringList* const mStringList;
  nsAttrValue mEmptyOrOldValue;
};

/* static */
already_AddRefed<DOMSVGStringList> DOMSVGStringList::GetDOMWrapper(
    SVGStringList* aList, SVGElement* aElement,
    bool aIsConditionalProcessingAttribute, uint8_t aAttrEnum) {
  RefPtr<DOMSVGStringList> wrapper =
      SVGStringListTearoffTable().GetTearoff(aList);
  if (!wrapper) {
    wrapper = new DOMSVGStringList(aElement, aIsConditionalProcessingAttribute,
                                   aAttrEnum);
    SVGStringListTearoffTable().AddTearoff(aList, wrapper);
  }
  return wrapper.forget();
}

void DOMSVGStringList::RemoveFromTearoffTable() {
  // Script no longer has any references to us.
  if (mIsInTearoffTable) {
    SVGStringListTearoffTable().RemoveTearoff(&InternalList());
    mIsInTearoffTable = false;
  }
}

DOMSVGStringList::~DOMSVGStringList() { RemoveFromTearoffTable(); }

/* virtual */
JSObject* DOMSVGStringList::WrapObject(JSContext* aCx,
                                       JS::Handle<JSObject*> aGivenProto) {
  return SVGStringList_Binding::Wrap(aCx, this, aGivenProto);
}

// ----------------------------------------------------------------------------
// SVGStringList implementation:

uint32_t DOMSVGStringList::NumberOfItems() const {
  return InternalList().Length();
}

uint32_t DOMSVGStringList::Length() const { return NumberOfItems(); }

void DOMSVGStringList::Clear() {
  if (InternalList().IsExplicitlySet()) {
    AutoChangeStringListNotifier notifier(this);
    InternalList().Clear();
  }
}

void DOMSVGStringList::Initialize(const nsAString& aNewItem, nsAString& aRetval,
                                  ErrorResult& aRv) {
  if (InternalList().IsExplicitlySet()) {
    InternalList().Clear();
  }
  InsertItemBefore(aNewItem, 0, aRetval, aRv);
}

void DOMSVGStringList::GetItem(uint32_t aIndex, nsAString& aRetval,
                               ErrorResult& aRv) {
  bool found;
  IndexedGetter(aIndex, found, aRetval);
  if (!found) {
    aRv.ThrowIndexSizeError("Index out of range");
  }
}

void DOMSVGStringList::IndexedGetter(uint32_t aIndex, bool& aFound,
                                     nsAString& aRetval) {
  aFound = aIndex < InternalList().Length();
  if (aFound) {
    aRetval = InternalList()[aIndex];
  }
}

void DOMSVGStringList::InsertItemBefore(const nsAString& aNewItem,
                                        uint32_t aIndex, nsAString& aRetval,
                                        ErrorResult& aRv) {
  if (aNewItem.IsEmpty()) {
    aRv.ThrowSyntaxError("Cannot insert empty string");
    return;
  }
  aIndex = std::min(aIndex, InternalList().Length());

  // Ensure we have enough memory so we can avoid complex error handling below:
  if (!InternalList().SetCapacity(InternalList().Length() + 1)) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  AutoChangeStringListNotifier notifier(this);
  InternalList().InsertItem(aIndex, aNewItem);
  aRetval = aNewItem;
}

void DOMSVGStringList::ReplaceItem(const nsAString& aNewItem, uint32_t aIndex,
                                   nsAString& aRetval, ErrorResult& aRv) {
  if (aNewItem.IsEmpty()) {
    aRv.ThrowSyntaxError("Cannot replace with empty string");
    return;
  }
  if (aIndex >= InternalList().Length()) {
    aRv.ThrowIndexSizeError("Index out of range");
    return;
  }

  aRetval = InternalList()[aIndex];
  AutoChangeStringListNotifier notifier(this);
  InternalList().ReplaceItem(aIndex, aNewItem);
}

void DOMSVGStringList::RemoveItem(uint32_t aIndex, nsAString& aRetval,
                                  ErrorResult& aRv) {
  if (aIndex >= InternalList().Length()) {
    aRv.ThrowIndexSizeError("Index out of range");
    return;
  }

  AutoChangeStringListNotifier notifier(this);
  InternalList().RemoveItem(aIndex);
}

void DOMSVGStringList::AppendItem(const nsAString& aNewItem, nsAString& aRetval,
                                  ErrorResult& aRv) {
  InsertItemBefore(aNewItem, InternalList().Length(), aRetval, aRv);
}

SVGStringList& DOMSVGStringList::InternalList() const {
  if (mIsConditionalProcessingAttribute) {
    nsCOMPtr<dom::SVGTests> tests = do_QueryObject(mElement);
    return tests->mStringListAttributes[mAttrEnum];
  }
  return mElement->GetStringListInfo().mValues[mAttrEnum];
}

}  // namespace mozilla::dom
