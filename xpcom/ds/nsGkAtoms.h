/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsGkAtoms_h_
#define nsGkAtoms_h_

#include "mozilla/StaticAtoms.h"
#include "nsAtom.h"

// Static atoms are structured carefully to satisfy a lot of constraints.
//
// - We have ~2700 static atoms.
//
// - We want them to be constexpr and not use relocations, so they end up in
//   .rodata, and thus shared between processes, minimizing memory usage.
//
// - We need them to be in an array, so we can iterate over them (for
//   registration / lookups / representation as an index).
//
// - Each static atom has a string literal associated with it. We can't use a
//   pointer to the string literal because then the atoms won't end up in
//   .rodata. Therefore the string literals and the atoms must be arranged in a
//   way such that a numeric index can be used instead. This numeric index
//   (nsStaticAtom::mStringOffset) must be computable at compile-time to keep
//   the static atom constexpr. It should also not be too large (a uint32_t is
//   reasonable).
//
// - As well as accessing each static atom via array indexing, we need an
//   individual pointer, e.g. nsGkAtoms::foo. We want this to be constexpr so
//   it doesn't take up any space in memory.
//
// - We want to minimize the header size / complexity, but the individual
//   pointers for the static atoms must be in a .h file so they are public.
//
// StaticAtoms.h below defines static atoms in a way that satisfies these
// constraints: A GkAtoms struct which arranges the strings and atoms together,
// defined in nsGkAtoms.cpp.

namespace nsGkAtoms {

constexpr nsStaticAtom* GetAtomByIndex(size_t aIndex) {
  MOZ_ASSERT(aIndex < kStaticAtomCount);
  return const_cast<nsStaticAtom*>(&detail::gGkAtoms.mAtoms[aIndex]);
}

constexpr size_t IndexOf(const nsStaticAtom* atom) {
  nsStaticAtom* firstAtom = GetAtomByIndex(0);
  size_t ret = atom - firstAtom;
  MOZ_ASSERT(ret < kStaticAtomCount);
  return ret;
}

}  // namespace nsGkAtoms

inline bool nsAtom::IsEmpty() const { return this == nsGkAtoms::_empty; }

#endif /* nsGkAtoms_h_ */
