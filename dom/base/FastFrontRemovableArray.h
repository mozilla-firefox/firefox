/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FastFrontRemovableArray_h
#define mozilla_dom_FastFrontRemovableArray_h

// An nsTArray of pointers where removing from the front is amortized constant
// time.

#include "mozilla/Span.h"
#include "nsTArray.h"

namespace mozilla::dom {

template <typename T, size_t InlineCapacity = 0>
class FastFrontRemovableArray {
  using InternalList = AutoTArray<T, InlineCapacity>;

 public:
  static const size_t NoIndex = InternalList::NoIndex;

  FastFrontRemovableArray() = default;
  FastFrontRemovableArray(FastFrontRemovableArray&& aOther)
      : mList(std::move(aOther.mList)),
        mIndexOfFirstElement(std::exchange(aOther.mIndexOfFirstElement, 0)) {}
  FastFrontRemovableArray& operator=(FastFrontRemovableArray&& aOther) {
    mList = std::move(aOther.mList);
    mIndexOfFirstElement = std::exchange(aOther.mIndexOfFirstElement, 0);
    return *this;
  }

  operator Span<const T>() const { return AsSpan(); }
  operator Span<T>() { return AsSpan(); }

  Span<const T> AsSpan() const {
    return Span<const T>(mList).From(mIndexOfFirstElement);
  }
  Span<T> AsSpan() { return Span<T>(mList).From(mIndexOfFirstElement); }

  size_t Length() const { return mList.Length() - mIndexOfFirstElement; }

  bool IsEmpty() const { return Length() == 0; }

  void RemoveElementAt(size_t aIndex) {
    MOZ_ASSERT(mIndexOfFirstElement <= mList.Length());
    if (aIndex == 0) {
      mList[mIndexOfFirstElement++] = nullptr;
      if (mIndexOfFirstElement > std::max(size_t(4), mList.Length() / 4)) {
        // Compact the list if it gets too big. This shifts all the elements,
        // which is expensive, so only do it if we have more than 4 elements
        // wasted at the front, and more than a quarter of the list is wasted
        // space in the front.
        mList.RemoveElementsAt(0, mIndexOfFirstElement);
        mIndexOfFirstElement = 0;
      }
      return;
    }
    mList.RemoveElementAt(aIndex + mIndexOfFirstElement);
    if (IsEmpty()) {
      Clear();
    }
  }

  template <typename U>
  void InsertElementAt(size_t aIndex, U* aElem) {
    MOZ_ASSERT(mIndexOfFirstElement <= mList.Length());
    if (mIndexOfFirstElement && aIndex == 0) {
      mList[--mIndexOfFirstElement] = aElem;
      return;
    }
    mList.InsertElementAt(aIndex + mIndexOfFirstElement, aElem);
  }

  T& operator[](size_t aIndex) { return mList[aIndex + mIndexOfFirstElement]; }

  const T& operator[](size_t aIndex) const {
    return mList[aIndex + mIndexOfFirstElement];
  }
  T& ElementAt(size_t aIndex) { return mList[aIndex + mIndexOfFirstElement]; }
  const T& ElementAt(size_t aIndex) const {
    return mList[aIndex + mIndexOfFirstElement];
  }

  T& SafeElementAt(size_t aIndex, T& aDef) {
    return mList.SafeElementAt(aIndex + mIndexOfFirstElement, aDef);
  }

  const T& SafeElementAt(size_t aIndex, const T& aDef) const {
    return mList.SafeElementAt(aIndex + mIndexOfFirstElement, aDef);
  }

  const T& FirstElement() const { return ElementAt(0); }
  const T& LastElement() const { return mList.LastElement(); }
  T& FirstElement() { return ElementAt(0); }
  T& LastElement() { return mList.LastElement(); }

  template <typename U>
  void AppendElement(U* aElem) {
    mList.AppendElement(aElem);
  }

  template <typename Item>
  bool RemoveElement(const Item& aItem) {
    const auto len = Length();
    if (!len) {
      return false;
    }
    if (LastElement() == aItem) {
      // Removing the last element is common (and removing the first one is
      // already fast via IndexOf), so avoid the linear search in that case.
      RemoveElementAt(len - 1);
      return true;
    }
    auto i = IndexOf(aItem);
    if (i == NoIndex) {
      return false;
    }
    RemoveElementAt(i);
    return true;
  }

  template <typename Item>
  bool Contains(const Item& aItem) const {
    return IndexOf(aItem) != NoIndex;
  }

  void Clear() {
    mList.Clear();
    mIndexOfFirstElement = 0;
  }

  template <typename Item>
  size_t IndexOf(const Item& aItem) const {
    auto index = mList.IndexOf(aItem, mIndexOfFirstElement);
    if (index == NoIndex || mIndexOfFirstElement == 0) {
      return index;
    }
    return index - mIndexOfFirstElement;
  }

 private:
  AutoTArray<T, InlineCapacity> mList;
  size_t mIndexOfFirstElement = 0;
};

template <typename T, size_t InlineCap>
inline void ImplCycleCollectionUnlink(
    FastFrontRemovableArray<T, InlineCap>& aField) {
  aField.Clear();
}

template <typename T, size_t InlineCap, typename Callback>
inline void ImplCycleCollectionIndexedContainer(
    FastFrontRemovableArray<T, InlineCap>& aField, Callback&& aCallback) {
  for (auto& value : aField.AsSpan()) {
    aCallback(value);
  }
}

}  // namespace mozilla::dom

#endif
