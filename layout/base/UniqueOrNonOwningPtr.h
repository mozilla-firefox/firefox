/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_UniqueOrNonOwningPtr_h
#define mozilla_UniqueOrNonOwningPtr_h

#include <cstdint>
#include <utility>

#include "mozilla/Assertions.h"

namespace mozilla {

template <typename T>
class UniqueOrNonOwningPtr;

namespace detail {

template <typename T>
struct UniqueOfUniqueOrNonOwningSelector {
  using SingleObject = UniqueOrNonOwningPtr<T>;
};

template <typename T>
struct UniqueOfUniqueOrNonOwningSelector<T[]>;

template <typename T, decltype(sizeof(int)) N>
struct UniqueOfUniqueOrNonOwningSelector<T[N]>;

}  // namespace detail

// `mozilla::MakeUnique` equivalent, with the same set of advantages.
// Non-owning case doesn't need this since there's no allocation.
// See below as to why only SingleObject case is supported.
template <typename T, typename... Args>
typename detail::UniqueOfUniqueOrNonOwningSelector<T>::SingleObject
MakeUniqueOfUniqueOrNonOwning(Args&&... aArgs) {
  return UniqueOrNonOwningPtr<T>::UniquelyOwning(new T(std::forward<Args>(aArgs)...));
}

/**
 * A pointer that is either:
 *   * Uniquely-owning, as if `std::unique_ptr`/`mozilla::UniquePtr`, or
 *   * Non-owning, as if raw pointer.
 *
 * Overall, it behaves like `mozilla::Variant<T*, UniquePtr<T>>`, but more
 * compact. It may be helpful if you are mostly referencing existing data type
 * of significant size, but sometimes generate a modified copy and refer to that.
 *
 * Usage notes:
 *   * Ownership: This structure makes ownership tracking harder. It is the
 *     caller's responsibility to ensure that, in the non-owning case, the data
 *     outlives this pointer.
 *   * (Ab)using the lowest bit: Owning state is tagged inline in the lowest bit,
 *     which is set for uniquely-owning data. It does not work with a byte-aligned
 *     data types, or members of a packed struct. There are asserts to try and catch
 *     this as early as possible.
 *
 * TODO(dshin): This lacks support for things that `mozilla::UniquePtr` supports -
 * however, these cases will fail to compile.
 *   * Deleter support (Even stateless ones)
 *   * Interconversion (Pointing to derived from base pointer)
 *   * T[]
 */
template <typename T>
class UniqueOrNonOwningPtr {
 public:
  // Check to make sure we can take on non-owning pointer to stack.
  static_assert(alignof(T) != 1, "Can't support data aligned to byte boundaries.");
  // Standard guarantees the null pointer value to be integer 0.
  UniqueOrNonOwningPtr() : mBits{0} {}
  UniqueOrNonOwningPtr(const UniqueOrNonOwningPtr&) = delete;
  UniqueOrNonOwningPtr(UniqueOrNonOwningPtr&& aOther) : mBits{aOther.mBits} {
    // "Release" the other one.
    aOther.mBits = 0;
  }
  ~UniqueOrNonOwningPtr() {
    if (IsUniquelyOwning()) {
      delete get();
    }
  }
  UniqueOrNonOwningPtr& operator=(const UniqueOrNonOwningPtr& aOther) = delete;
  UniqueOrNonOwningPtr& operator=(UniqueOrNonOwningPtr&& aOther) {
    mBits = aOther.mBits;
    // "Release" the other one.
    aOther.mBits = 0;
    return *this;
  }

  static UniqueOrNonOwningPtr UniquelyOwning(T* aPtr) {
    MOZ_ASSERT(aPtr, "Passing in null pointer as owning?");
    const uintptr_t bits = reinterpret_cast<uintptr_t>(aPtr);
    MOZ_ASSERT((bits & kUniquelyOwningBit) == 0, "Odd-aligned owning pointer?");
    return UniqueOrNonOwningPtr{bits | kUniquelyOwningBit};
  }

  static UniqueOrNonOwningPtr NonOwning(T* aPtr) {
    const uintptr_t bits = reinterpret_cast<uintptr_t>(aPtr);
    MOZ_ASSERT((bits & kUniquelyOwningBit) == 0, "Odd-aligned non-owning pointer?");
    return UniqueOrNonOwningPtr{bits};
  }

  std::add_lvalue_reference_t<T> operator*() const {
    MOZ_ASSERT(get(),
               "dereferencing a UniqueOrNonOwningPtr containing nullptr with *");
    return *get();
  }

  T* operator->() const {
    MOZ_ASSERT(get(),
               "dereferencing a UniqueOrNonOwningPtr containing nullptr with ->");
    return get();
  }

  explicit operator bool() const { return get() != nullptr; }

  T* get() const { return reinterpret_cast<T*>(mBits & ~kUniquelyOwningBit); }

 private:
  bool IsUniquelyOwning() const {
    return (mBits & kUniquelyOwningBit) != 0;
  }

  // Bit for tracking uniquely-owning vs non-owning status. Check usage notes
  // in the main comment block.
  // NOTE: A null pointer constant has a guarantee on being integer literal 0.
  static constexpr uintptr_t kUniquelyOwningBit = 1;
  explicit UniqueOrNonOwningPtr(uintptr_t aValue) : mBits{aValue} {}
  uintptr_t mBits;
};

// Unsupported
template <typename T>
class UniqueOrNonOwningPtr<T[]>;

}  // namespace mozilla

#endif
