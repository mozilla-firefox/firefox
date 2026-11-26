/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* bit vectors for sets of CSS properties */

#ifndef nsCSSPropertyIDSet_h_
#define nsCSSPropertyIDSet_h_

#include <limits.h>  // for CHAR_BIT

#include <initializer_list>
#include <ostream>

#include "mozilla/Array.h"
// For COMPOSITOR_ANIMATABLE_PROPERTY_LIST and
// COMPOSITOR_ANIMATABLE_PROPERTY_LIST_LENGTH
#include "NonCustomCSSPropertyId.h"
#include "mozilla/CSSPropertyId.h"
#include "mozilla/CompositorAnimatableProperties.h"
#include "nsCSSProps.h"  // For operator<< for NonCustomCSSPropertyId

/**
 * nsCSSPropertyIDSet maintains a set of non-shorthand CSS properties.  In
 * other words, for each longhand CSS property we support, it has a bit
 * for whether that property is in the set.
 */
class nsCSSPropertyIDSet {
 public:
  constexpr nsCSSPropertyIDSet() : mProperties{} {}
  // auto-generated copy-constructor OK

  explicit constexpr nsCSSPropertyIDSet(
      std::initializer_list<NonCustomCSSPropertyId> aProperties)
      : mProperties{} {
    for (auto property : aProperties) {
      size_t p = property;
      mProperties[p / kBitsInChunk] |= property_set_type(1)
                                       << (p % kBitsInChunk);
    }
  }

  void AssertInSetRange(NonCustomCSSPropertyId aProperty) const {
    MOZ_DIAGNOSTIC_ASSERT(aProperty != eCSSProperty_UNKNOWN &&
                              aProperty < eCSSProperty_COUNT_no_shorthands,
                          "out of bounds");
  }

  // Conversion of aProperty to |size_t| after AssertInSetRange
  // lets the compiler generate significantly tighter code.

  void AddProperty(NonCustomCSSPropertyId aProperty) {
    AssertInSetRange(aProperty);
    size_t p = aProperty;
    mProperties[p / kBitsInChunk] |= property_set_type(1) << (p % kBitsInChunk);
  }

  void RemoveProperty(NonCustomCSSPropertyId aProperty) {
    AssertInSetRange(aProperty);
    size_t p = aProperty;
    mProperties[p / kBitsInChunk] &=
        ~(property_set_type(1) << (p % kBitsInChunk));
  }

  bool HasProperty(const mozilla::CSSPropertyId& aProperty) const {
    return !aProperty.IsCustom() && HasProperty(aProperty.mId);
  }

  bool HasProperty(NonCustomCSSPropertyId aProperty) const {
    AssertInSetRange(aProperty);
    size_t p = aProperty;
    return (mProperties[p / kBitsInChunk] &
            (property_set_type(1) << (p % kBitsInChunk))) != 0;
  }

  // Returns an nsCSSPropertyIDSet including all properties that can be run
  // on the compositor.
  static constexpr nsCSSPropertyIDSet CompositorAnimatables() {
    return nsCSSPropertyIDSet(COMPOSITOR_ANIMATABLE_PROPERTY_LIST);
  }

  static constexpr size_t CompositorAnimatableCount() {
    return COMPOSITOR_ANIMATABLE_PROPERTY_LIST_LENGTH;
  }

  static constexpr size_t CompositorAnimatableDisplayItemCount() {
    // We have 3 individual transforms and 5 motion path properties, and they
    // also use DisplayItemType::TYPE_TRANSFORM.
    return COMPOSITOR_ANIMATABLE_PROPERTY_LIST_LENGTH - 8;
  }

  static constexpr nsCSSPropertyIDSet CSSTransformProperties() {
    return nsCSSPropertyIDSet{eCSSProperty_transform, eCSSProperty_translate,
                              eCSSProperty_rotate, eCSSProperty_scale};
  }

  static constexpr nsCSSPropertyIDSet MotionPathProperties() {
    return nsCSSPropertyIDSet{
        eCSSProperty_offset_path, eCSSProperty_offset_distance,
        eCSSProperty_offset_rotate, eCSSProperty_offset_anchor,
        eCSSProperty_offset_position};
  }

  static constexpr nsCSSPropertyIDSet TransformLikeProperties() {
    return nsCSSPropertyIDSet{
        eCSSProperty_transform,      eCSSProperty_translate,
        eCSSProperty_rotate,         eCSSProperty_scale,
        eCSSProperty_offset_path,    eCSSProperty_offset_distance,
        eCSSProperty_offset_rotate,  eCSSProperty_offset_anchor,
        eCSSProperty_offset_position};
  }

  static constexpr nsCSSPropertyIDSet OpacityProperties() {
    return nsCSSPropertyIDSet{eCSSProperty_opacity};
  }

  bool Intersects(const nsCSSPropertyIDSet& aOther) const {
    for (size_t i = 0; i < std::size(mProperties); ++i) {
      if (mProperties[i] & aOther.mProperties[i]) {
        return true;
      }
    }
    return false;
  }

  void Empty() { mProperties = {}; }

  void AssertIsEmpty(const char* aText) const {
    for (size_t i = 0; i < std::size(mProperties); ++i) {
      NS_ASSERTION(mProperties[i] == 0, aText);
    }
  }

  bool Equals(const nsCSSPropertyIDSet& aOther) const {
    return mProperties == aOther.mProperties;
  }

  bool IsEmpty() const {
    for (size_t i = 0; i < std::size(mProperties); ++i) {
      if (mProperties[i] != 0) {
        return false;
      }
    }
    return true;
  }

  bool IsSubsetOf(const nsCSSPropertyIDSet& aOther) const {
    for (size_t i = 0; i < std::size(mProperties); ++i) {
      if (mProperties[i] & ~aOther.mProperties[i]) {
        return false;
      }
    }
    return true;
  }

  // Returns a new nsCSSPropertyIDSet with all properties that are both in
  // this set and |aOther|.
  nsCSSPropertyIDSet Intersect(const nsCSSPropertyIDSet& aOther) const {
    nsCSSPropertyIDSet result;
    for (size_t i = 0; i < std::size(mProperties); ++i) {
      result.mProperties[i] = mProperties[i] & aOther.mProperties[i];
    }
    return result;
  }

  // Returns a new nsCSSPropertyIDSet with all properties that are in either
  // this set or |aOther| but not both.
  nsCSSPropertyIDSet Xor(const nsCSSPropertyIDSet& aOther) const {
    nsCSSPropertyIDSet result;
    for (size_t i = 0; i < std::size(mProperties); ++i) {
      result.mProperties[i] = mProperties[i] ^ aOther.mProperties[i];
    }
    return result;
  }

  nsCSSPropertyIDSet& operator|=(const nsCSSPropertyIDSet& aOther) {
    for (size_t i = 0; i < std::size(mProperties); ++i) {
      mProperties[i] |= aOther.mProperties[i];
    }
    return *this;
  }

 private:
  typedef unsigned long property_set_type;

 public:
  // number of bits in |property_set_type|.
  static const size_t kBitsInChunk = sizeof(property_set_type) * CHAR_BIT;
  // number of |property_set_type|s in the set
  static const size_t kChunkCount =
      (eCSSProperty_COUNT_no_shorthands + kBitsInChunk - 1) / kBitsInChunk;

  /*
   * For fast enumeration of all the bits that are set, callers can
   * check each chunk against zero (since in normal cases few bits are
   * likely to be set).
   */
  bool HasPropertyInChunk(size_t aChunk) const {
    return mProperties[aChunk] != 0;
  }
  bool HasPropertyAt(size_t aChunk, size_t aBit) const {
    return (mProperties[aChunk] & (property_set_type(1) << aBit)) != 0;
  }
  static NonCustomCSSPropertyId CSSPropertyAt(size_t aChunk, size_t aBit) {
    return NonCustomCSSPropertyId(aChunk * kBitsInChunk + aBit);
  }

  // Iterator for use in range-based for loops
  class Iterator {
   public:
    Iterator(Iterator&& aOther)
        : mPropertySet(aOther.mPropertySet),
          mChunk(aOther.mChunk),
          mBit(aOther.mBit) {}

    Iterator() = delete;
    Iterator(const Iterator&) = delete;
    Iterator& operator=(const Iterator&) = delete;
    Iterator& operator=(const Iterator&&) = delete;

    static Iterator BeginIterator(const nsCSSPropertyIDSet& aPropertySet) {
      Iterator result(aPropertySet);

      // Search for the first property.
      // Unsigned integer overflow is defined so the following is safe.
      result.mBit = -1;
      ++result;

      return result;
    }

    static Iterator EndIterator(const nsCSSPropertyIDSet& aPropertySet) {
      Iterator result(aPropertySet);
      result.mChunk = kChunkCount;
      result.mBit = 0;
      return result;
    }

    bool operator!=(const Iterator& aOther) const {
      return mChunk != aOther.mChunk || mBit != aOther.mBit;
    }

    Iterator& operator++() {
      MOZ_ASSERT(mChunk < kChunkCount, "Should not iterate beyond end");

      do {
        mBit++;
      } while (mBit < kBitsInChunk &&
               !mPropertySet.HasPropertyAt(mChunk, mBit));
      if (mBit != kBitsInChunk) {
        return *this;
      }

      do {
        mChunk++;
      } while (mChunk < kChunkCount &&
               !mPropertySet.HasPropertyInChunk(mChunk));
      mBit = 0;
      if (mChunk != kChunkCount) {
        while (mBit < kBitsInChunk &&
               !mPropertySet.HasPropertyAt(mChunk, mBit)) {
          mBit++;
        }
      }

      return *this;
    }

    NonCustomCSSPropertyId operator*() {
      MOZ_ASSERT(mChunk < kChunkCount, "Should not dereference beyond end");
      return nsCSSPropertyIDSet::CSSPropertyAt(mChunk, mBit);
    }

   private:
    explicit Iterator(const nsCSSPropertyIDSet& aPropertySet)
        : mPropertySet(aPropertySet) {}

    const nsCSSPropertyIDSet& mPropertySet;
    size_t mChunk = 0;
    size_t mBit = 0;
  };

  Iterator begin() const { return Iterator::BeginIterator(*this); }
  Iterator end() const { return Iterator::EndIterator(*this); }

 private:
  mozilla::Array<property_set_type, kChunkCount> mProperties;
};

// MOZ_DBG support

inline std::ostream& operator<<(std::ostream& aOut,
                                const nsCSSPropertyIDSet& aPropertySet) {
  AutoTArray<NonCustomCSSPropertyId, 16> properties;
  for (NonCustomCSSPropertyId property : aPropertySet) {
    properties.AppendElement(property);
  }
  return aOut << properties;
}

#endif /* !defined(nsCSSPropertyIDSet_h_) */
