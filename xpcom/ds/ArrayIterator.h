/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Common iterator implementation for array classes e.g. nsTArray.

#ifndef mozilla_ArrayIterator_h
#define mozilla_ArrayIterator_h

#include <iterator>
#include <type_traits>

namespace mozilla {

namespace detail {
template <typename T>
struct AddInnerConst;

template <typename T>
struct AddInnerConst<T&> {
  using Type = const T&;
};

template <typename T>
struct AddInnerConst<T*> {
  using Type = const T*;
};

template <typename T>
using AddInnerConstT = typename AddInnerConst<T>::Type;
}  // namespace detail

// We have implemented a custom iterator class for array rather than using
// raw pointers into the backing storage to improve the safety of C++11-style
// range based iteration in the presence of array mutation, or script execution
// (bug 1299489).
//
// Mutating an array which is being iterated is still wrong, and will either
// cause elements to be missed or firefox to crash, but will not trigger memory
// safety problems due to the release-mode bounds checking found in ElementAt.
//
// Dereferencing this iterator returns type Element. When Element is a reference
// type, this iterator implements the full standard random access iterator spec,
// and can be treated in many ways as though it is a pointer. Otherwise, it is
// just enough to be used in range-based for loop.
template <class Element, class ArrayType>
class ArrayIterator {
 public:
  typedef ArrayType array_type;
  typedef ArrayIterator<Element, ArrayType> iterator_type;
  typedef typename array_type::index_type index_type;
  typedef std::remove_reference_t<Element> value_type;
  typedef ptrdiff_t difference_type;
  typedef value_type* pointer;
  typedef value_type& reference;
  typedef std::random_access_iterator_tag iterator_category;
  typedef ArrayIterator<detail::AddInnerConstT<Element>, ArrayType>
      const_iterator_type;

 private:
  const array_type* mArray;
  index_type mIndex;

 public:
  ArrayIterator() : mArray(nullptr), mIndex(0) {}
  ArrayIterator(const iterator_type& aOther)
      : mArray(aOther.mArray), mIndex(aOther.mIndex) {}
  ArrayIterator(const array_type& aArray, index_type aIndex)
      : mArray(&aArray), mIndex(aIndex) {}

  iterator_type& operator=(const iterator_type& aOther) = default;

  constexpr operator const_iterator_type() const {
    return mArray ? const_iterator_type{*mArray, mIndex}
                  : const_iterator_type{};
  }

  bool operator==(const iterator_type& aRhs) const {
    return mIndex == aRhs.mIndex;
  }
  bool operator!=(const iterator_type& aRhs) const { return !(*this == aRhs); }
  bool operator<(const iterator_type& aRhs) const {
    return mIndex < aRhs.mIndex;
  }
  bool operator>(const iterator_type& aRhs) const {
    return mIndex > aRhs.mIndex;
  }
  bool operator<=(const iterator_type& aRhs) const {
    return mIndex <= aRhs.mIndex;
  }
  bool operator>=(const iterator_type& aRhs) const {
    return mIndex >= aRhs.mIndex;
  }

  // These operators depend on the release mode bounds checks in
  // ArrayIterator::ElementAt for safety.
  value_type* operator->() const {
    return const_cast<value_type*>(&mArray->ElementAt(mIndex));
  }
  Element operator*() const {
    return const_cast<Element>(mArray->ElementAt(mIndex));
  }

  iterator_type& operator++() {
    ++mIndex;
    return *this;
  }
  iterator_type operator++(int) {
    iterator_type it = *this;
    ++*this;
    return it;
  }
  iterator_type& operator--() {
    --mIndex;
    return *this;
  }
  iterator_type operator--(int) {
    iterator_type it = *this;
    --*this;
    return it;
  }

  iterator_type& operator+=(difference_type aDiff) {
    mIndex += aDiff;
    return *this;
  }
  iterator_type& operator-=(difference_type aDiff) {
    mIndex -= aDiff;
    return *this;
  }

  iterator_type operator+(difference_type aDiff) const {
    iterator_type it = *this;
    it += aDiff;
    return it;
  }
  iterator_type operator-(difference_type aDiff) const {
    iterator_type it = *this;
    it -= aDiff;
    return it;
  }

  difference_type operator-(const iterator_type& aOther) const {
    return static_cast<difference_type>(mIndex) -
           static_cast<difference_type>(aOther.mIndex);
  }

  Element operator[](difference_type aIndex) const {
    return *this->operator+(aIndex);
  }

  constexpr const array_type* GetArray() const { return mArray; }

  constexpr index_type GetIndex() const { return mIndex; }
};

}  // namespace mozilla

#endif  // mozilla_ArrayIterator_h
