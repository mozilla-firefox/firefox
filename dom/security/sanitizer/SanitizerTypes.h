/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SanitizerTypes_h
#define mozilla_dom_SanitizerTypes_h

#include "fmt/format.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/Maybe.h"
#include "mozilla/dom/SanitizerBinding.h"
#include "nsHashtablesFwd.h"
#include "nsTHashSet.h"

namespace mozilla::dom::sanitizer {

struct CanonicalElementAttributes;

class CanonicalAttribute : public PLDHashEntryHdr {
 public:
  using KeyType = const CanonicalAttribute&;
  using KeyTypePointer = const CanonicalAttribute*;

  explicit CanonicalAttribute(const CanonicalAttribute* aKey)
      : mLocalName(aKey->mLocalName), mNamespace(aKey->mNamespace) {}
  CanonicalAttribute(RefPtr<nsAtom> aLocalName, RefPtr<nsAtom> aNamespace)
      : mLocalName(std::move(aLocalName)), mNamespace(std::move(aNamespace)) {}
  CanonicalAttribute(nsStaticAtom* aLocalName, nsStaticAtom* aNamespace)
      : mLocalName(aLocalName), mNamespace(aNamespace) {}
  CanonicalAttribute(CanonicalAttribute&&) = default;
  ~CanonicalAttribute() = default;

  KeyType GetKey() const { return *this; }
  bool KeyEquals(const CanonicalAttribute* aKey) const {
    return mLocalName == aKey->mLocalName && mNamespace == aKey->mNamespace;
  }

  static KeyTypePointer KeyToPointer(KeyType aKey) { return &aKey; }
  static PLDHashNumber HashKey(KeyTypePointer aKey) {
    return AddToHash(aKey->mLocalName->hash(),
                     aKey->mNamespace ? aKey->mNamespace->hash() : 0);
  }

  enum { ALLOW_MEMMOVE = true };

  // Returns true for names that start with data-* and have a null namespace.
  bool IsDataAttribute() const;
  SanitizerAttributeNamespace ToSanitizerAttributeNamespace() const;

  CanonicalAttribute Clone() const {
    return CanonicalAttribute(mLocalName, mNamespace);
  }

  nsAtom* LocalName() const { return mLocalName; }
  nsAtom* GetNamespace() const { return mNamespace; }

 protected:
  friend std::ostream& operator<<(std::ostream& aStream,
                                  const CanonicalAttribute& aName);
  RefPtr<nsAtom> mLocalName;
  // A "null" namespace is represented by the nullptr.
  RefPtr<nsAtom> mNamespace;
};

class CanonicalElement : public PLDHashEntryHdr {
 public:
  using KeyType = const CanonicalElement&;
  using KeyTypePointer = const CanonicalElement*;

  explicit CanonicalElement(const CanonicalElement* aKey)
      : mLocalName(aKey->mLocalName), mNamespace(aKey->mNamespace) {}
  CanonicalElement(RefPtr<nsAtom> aLocalName, RefPtr<nsAtom> aNamespace)
      : mLocalName(std::move(aLocalName)), mNamespace(std::move(aNamespace)) {}
  CanonicalElement(nsStaticAtom* aLocalName, nsStaticAtom* aNamespace)
      : mLocalName(aLocalName), mNamespace(aNamespace) {}
  CanonicalElement(CanonicalElement&&) = default;
  ~CanonicalElement() = default;

  KeyType GetKey() const { return *this; }
  bool KeyEquals(const CanonicalElement* aKey) const { return *this == *aKey; }

  static KeyTypePointer KeyToPointer(KeyType aKey) { return &aKey; }
  static PLDHashNumber HashKey(KeyTypePointer aKey) {
    return AddToHash(aKey->mLocalName->hash(),
                     aKey->mNamespace ? aKey->mNamespace->hash() : 0);
  }

  enum { ALLOW_MEMMOVE = true };

  SanitizerElementNamespace ToSanitizerElementNamespace() const;
  SanitizerElementNamespaceWithAttributes
  ToSanitizerElementNamespaceWithAttributes(
      const CanonicalElementAttributes& aElementAttributes) const;

  nsAtom* LocalName() const { return mLocalName; }
  nsAtom* GetNamespace() const { return mNamespace; }

  bool operator==(const CanonicalElement& aOther) const {
    return mLocalName == aOther.mLocalName && mNamespace == aOther.mNamespace;
  }

 protected:
  friend std::ostream& operator<<(std::ostream& aStream,
                                  const CanonicalElement& aName);

  RefPtr<nsAtom> mLocalName;
  // A "null" namespace is represented by the nullptr.
  RefPtr<nsAtom> mNamespace;
};

std::ostream& operator<<(std::ostream& aStream,
                         const CanonicalAttribute& aName);
std::ostream& operator<<(std::ostream& aStream, const CanonicalElement& aName);

using CanonicalAttributeSet = nsTHashSet<CanonicalAttribute>;
using CanonicalElementSet = nsTHashSet<CanonicalElement>;
using CanonicalPISet = nsTHashSet<RefPtr<nsAtom>>;

struct CanonicalElementAttributes {
  Maybe<CanonicalAttributeSet> mAttributes;
  Maybe<CanonicalAttributeSet> mRemoveAttributes;

  bool Equals(const CanonicalElementAttributes& aOther) const;
};

using CanonicalElementMap =
    nsTHashMap<CanonicalElement, CanonicalElementAttributes>;

nsTArray<OwningStringOrSanitizerAttributeNamespace> ToSanitizerAttributes(
    const CanonicalAttributeSet& aSet);

inline const auto& GetAsDictionary(
    const OwningStringOrSanitizerAttributeNamespace& aOwning) {
  return aOwning.GetAsSanitizerAttributeNamespace();
}

inline const auto& GetAsDictionary(
    const OwningStringOrSanitizerElementNamespace& aOwning) {
  return aOwning.GetAsSanitizerElementNamespace();
}

inline const auto& GetAsDictionary(
    const OwningStringOrSanitizerElementNamespaceWithAttributes& aOwning) {
  return aOwning.GetAsSanitizerElementNamespaceWithAttributes();
}

inline const auto& GetAsDictionary(
    const OwningStringOrSanitizerProcessingInstruction& aOwning) {
  return aOwning.GetAsSanitizerProcessingInstruction();
}

inline const auto& GetAsDictionary(
    const StringOrSanitizerElementNamespace& aElement) {
  return aElement.GetAsSanitizerElementNamespace();
}

inline const auto& GetAsDictionary(
    const StringOrSanitizerElementNamespaceWithAttributes& aElement) {
  return aElement.GetAsSanitizerElementNamespaceWithAttributes();
}

inline const auto& GetAsDictionary(
    const StringOrSanitizerProcessingInstruction& aElement) {
  return aElement.GetAsSanitizerProcessingInstruction();
}

template <typename SanitizerNameNamespace>
class MOZ_STACK_CLASS SanitizerComparator final {
 public:
  bool Equals(const SanitizerNameNamespace& aItemA,
              const SanitizerNameNamespace& aItemB) const {
    const auto& itemA = GetAsDictionary(aItemA);
    const auto& itemB = GetAsDictionary(aItemB);

    return itemA.mNamespace.IsVoid() == itemB.mNamespace.IsVoid() &&
           itemA.mNamespace == itemB.mNamespace && itemA.mName == itemB.mName;
  }

  // https://wicg.github.io/sanitizer-api/#sanitizerconfig-less-than-item
  bool LessThan(const SanitizerNameNamespace& aItemA,
                const SanitizerNameNamespace& aItemB) const {
    const auto& itemA = GetAsDictionary(aItemA);
    const auto& itemB = GetAsDictionary(aItemB);

    // Step 1. If itemA["namespace"] is null:
    if (itemA.mNamespace.IsVoid()) {
      // Step 1.1. If itemB["namespace"] is not null, return true.
      if (!itemB.mNamespace.IsVoid()) {
        return true;
      }
    } else {
      // Step 2. Otherwise:
      // Step 2.1. If itemB["namespace"] is null, return false.
      if (itemB.mNamespace.IsVoid()) {
        return false;
      }

      int result = Compare(itemA.mNamespace, itemB.mNamespace);
      // Step 2.2. If itemA["namespace"] is code unit less than
      // itemB["namespace"], return true.
      if (result < 0) {
        return true;
      }
      // Step 2.3. If itemA["namespace"] is not equal itemB["namespace"], return
      // false.
      // XXX https://github.com/WICG/sanitizer-api/pull/341
      if (result != 0) {
        return false;
      }
    }

    // Step 3. Return itemA["name"] is code unit less thanitemB["name"].
    return itemA.mName < itemB.mName;
  }
};

class MOZ_STACK_CLASS PIComparator final {
 public:
  bool Equals(
      const OwningStringOrSanitizerProcessingInstruction& aItemA,
      const OwningStringOrSanitizerProcessingInstruction& aItemB) const {
    const auto& itemA = GetAsDictionary(aItemA);
    const auto& itemB = GetAsDictionary(aItemB);
    return itemA.mTarget == itemB.mTarget;
  }

  bool LessThan(
      const OwningStringOrSanitizerProcessingInstruction& aItemA,
      const OwningStringOrSanitizerProcessingInstruction& aItemB) const {
    const auto& itemA = GetAsDictionary(aItemA);
    const auto& itemB = GetAsDictionary(aItemB);
    return itemA.mTarget < itemB.mTarget;
  }
};

}  // namespace mozilla::dom::sanitizer

template <>
struct fmt::formatter<mozilla::dom::sanitizer::CanonicalAttribute>
    : ostream_formatter {};

template <>
struct fmt::formatter<mozilla::dom::sanitizer::CanonicalElement>
    : ostream_formatter {};

#endif
