/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsStyleAutoArray_h_
#define nsStyleAutoArray_h_

#include "mozilla/Assertions.h"
#include "nsTArray.h"

/**
 * An array of objects, similar to AutoTArray<T,1> but which is memmovable. It
 * always has length >= 1.
 */
template <typename T>
class nsStyleAutoArray {
 public:
  // This constructor places a single element in mFirstElement.
  enum WithSingleInitialElement { WITH_SINGLE_INITIAL_ELEMENT };
  explicit nsStyleAutoArray(WithSingleInitialElement) {}

  nsStyleAutoArray(const nsStyleAutoArray&) = delete;
  nsStyleAutoArray& operator=(const nsStyleAutoArray&) = delete;

  nsStyleAutoArray(nsStyleAutoArray&&) = default;
  nsStyleAutoArray& operator=(nsStyleAutoArray&&) = default;

  bool Assign(const nsStyleAutoArray& aOther, mozilla::fallible_t) {
    mFirstElement = aOther.mFirstElement;
    return mOtherElements.Assign(aOther.mOtherElements, mozilla::fallible);
  }

  nsStyleAutoArray Clone() const {
    nsStyleAutoArray res(WITH_SINGLE_INITIAL_ELEMENT);
    res.mFirstElement = mFirstElement;
    res.mOtherElements = mOtherElements.Clone();
    return res;
  }

  bool operator==(const nsStyleAutoArray&) const = default;

  size_t Length() const { return mOtherElements.Length() + 1; }
  const T& operator[](size_t aIndex) const {
    return aIndex == 0 ? mFirstElement : mOtherElements[aIndex - 1];
  }
  T& operator[](size_t aIndex) {
    return aIndex == 0 ? mFirstElement : mOtherElements[aIndex - 1];
  }

  void EnsureLengthAtLeast(size_t aMinLen) {
    if (aMinLen > 0) {
      mOtherElements.EnsureLengthAtLeast(aMinLen - 1);
    }
  }

  void SetLengthNonZero(size_t aNewLen) {
    MOZ_ASSERT(aNewLen > 0);
    mOtherElements.SetLength(aNewLen - 1);
  }

  void TruncateLengthNonZero(size_t aNewLen) {
    MOZ_ASSERT(aNewLen > 0);
    MOZ_ASSERT(aNewLen <= Length());
    mOtherElements.TruncateLength(aNewLen - 1);
  }

 private:
  T mFirstElement;
  nsTArray<T> mOtherElements;
};

#endif /* nsStyleAutoArray_h_ */
