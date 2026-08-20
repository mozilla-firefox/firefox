/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* A class holding a pair of objects that tries to conserve storage space. */

#ifndef mozilla_CompactPair_h
#define mozilla_CompactPair_h

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "mozilla/Attributes.h"

namespace mozilla {

/**
 * CompactPair is the logical concatenation of an instance of A with an instance
 * B. Space is conserved when possible.
 *
 * In general if space conservation is not critical is preferred to use
 * std::pair.
 *
 */
template <typename A, typename B>
class CompactPair {
  MOZ_NO_UNIQUE_ADDRESS A mFirst;
  MOZ_NO_UNIQUE_ADDRESS B mSecond;

  template <class APack, size_t... AIs, class BPack, size_t... BIs>
  constexpr CompactPair(APack&& aFirst, std::index_sequence<AIs...>,
                        BPack&& aSecond, std::index_sequence<BIs...>)
      : mFirst(std::get<AIs>(aFirst)...), mSecond(std::get<BIs>(aSecond)...) {}

 public:
  template <typename... AArgs, typename... BArgs>
  constexpr CompactPair(std::piecewise_construct_t, std::tuple<AArgs...> aFirst,
                        std::tuple<BArgs...> aSecond)
      : CompactPair(aFirst, std::make_index_sequence<sizeof...(AArgs)>(),
                    aSecond, std::make_index_sequence<sizeof...(BArgs)>()) {}

  template <typename U, typename V>
  explicit constexpr CompactPair(U&& aFirst, V&& aSecond)
      : mFirst(std::forward<U>(aFirst)), mSecond(std::forward<V>(aSecond)) {}
  CompactPair(CompactPair&& aOther) = default;
  CompactPair(const CompactPair& aOther) = default;

  CompactPair& operator=(CompactPair&& aOther) = default;
  CompactPair& operator=(const CompactPair& aOther) = default;

  bool operator==(const CompactPair& aOther) const = default;

  constexpr A& first() { return mFirst; }
  constexpr const A& first() const { return mFirst; }
  constexpr B& second() { return mSecond; }
  constexpr const B& second() const { return mSecond; }

  /** Swap this pair with another pair. */
  void swap(CompactPair& aOther) {
    using std::swap;
    swap(mFirst, aOther.mFirst);
    swap(mSecond, aOther.mSecond);
  }
};

/**
 * MakeCompactPair allows you to construct a CompactPair instance using type
 * inference. A call like this:
 *
 *   MakeCompactPair(Foo(), Bar())
 *
 * will return a CompactPair<Foo, Bar>.
 */
template <typename A, typename B>
CompactPair<std::remove_cvref_t<A>, std::remove_cvref_t<B>> MakeCompactPair(
    A&& aA, B&& aB) {
  return CompactPair<std::remove_cvref_t<A>, std::remove_cvref_t<B>>(
      std::forward<A>(aA), std::forward<B>(aB));
}

}  // namespace mozilla

namespace std {

template <typename A, class B>
void swap(mozilla::CompactPair<A, B>& aX, mozilla::CompactPair<A, B>& aY) {
  aX.swap(aY);
}

}  // namespace std

#endif /* mozilla_CompactPair_h */
