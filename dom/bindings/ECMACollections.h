/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// C++ implementations of the ECMAScript Set (ECMA-262 sec-set-objects) and Map
// (ECMA-262 sec-map-objects) data structures, for use as the backing store of
// WebIDL setlike/maplike interfaces.
//
// Architecture:
//
//   mEntries:    [ entry0 | entry1 | Nothing | ... ]   insertion-ordered
//   mPositions:  { key -> index into mEntries }         O(1) lookup
//
// mEntries owns all data.  mPositions is a secondary index that maps
// key -> physical index; it contains NO tombstones (entries are removed on
// Delete).  mEntries uses tombstones (Nothing) to preserve iterator positions.
//
// Compaction reclaims tombstones once they outnumber the live entries.
// Iterations go through a cursor registered with the collection, which moves
// it when it compacts, so an iteration that is never finished cannot pin
// tombstones, and a suspended iteration continues exactly as ES iterators do.
// Per ECMA-262 sec-set.prototype.clear the [[SetData]] List survives a clear
// for the sake of such iterations; Clear() empties the storage and moves their
// cursors to its end instead, which lets them see the entries added afterwards
// all the same.

#ifndef mozilla_dom_ECMACollections_h
#define mozilla_dom_ECMACollections_h

#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

#include "js/RootingAPI.h"
#include "mozilla/Assertions.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsTHashMap.h"

class nsAtom;

namespace mozilla::dom {

// Opt-out of cycle-collection traversal for RefPtr<T> stored in ECMASet or
// ECMAMap. Specialize to true_type for T that is not a cycle collection
// participant. The default is to traverse; only non-CC types need opt-outs.
template <typename T>
struct ECMACollectionNoTraverse : std::false_type {};
template <>
struct ECMACollectionNoTraverse<nsAtom> : std::true_type {};

namespace binding_detail {

enum class ECMAIterationDirection : bool { Forward, Reverse };

// Entries are neither traced nor barriered, and keys are hashed by value,
// which a moving GC would invalidate, so GC pointers, JS::Value and JS::Heap
// cannot be stored. Aggregates holding them are not detected.
template <typename T>
struct ECMAStorable
    : std::bool_constant<!js::IsHeapConstructibleType<T>::value> {};
template <typename T>
struct ECMAStorable<JS::Heap<T>> : std::false_type {};

// How the generated bindings pass a stored type T, which ECMASet and ECMAMap
// accept as well:
//
//   - Type is the argument type. A stored RefPtr<U> is passed as a U&, like
//     every other non-nullable interface argument, and a stored string as a
//     substring, since that is all a caller has to offer.
//   - ToLookup and ToStored convert the argument to the form an entry is looked
//     up by and stored as, which differ for strings: looking up by substring is
//     free, storing needs a string of its own.
//   - FromStored goes the other way.
//   - kDistinct is whether Type differs from const T&, in which case ECMASet
//     and ECMAMap have overloads that take it.
template <typename T>
struct ECMAArgTraits {
  using Type = const T&;
  static constexpr bool kDistinct = false;
  static Type ToLookup(Type aArg) { return aArg; }
  static Type ToStored(Type aArg) { return aArg; }
  static Type FromStored(const T& aStored) { return aStored; }
};
template <typename U>
struct ECMAArgTraits<RefPtr<U>> {
  using Type = U&;
  static constexpr bool kDistinct = true;
  static U* ToLookup(Type aArg) { return &aArg; }
  static RefPtr<U> ToStored(Type aArg) { return RefPtr<U>(&aArg); }
  static Type FromStored(const RefPtr<U>& aStored) { return *aStored; }
};
template <typename C>
struct ECMAArgTraits<nsTString<C>> {
  using Type = const nsTSubstring<C>&;
  static constexpr bool kDistinct = true;
  static Type ToLookup(Type aArg) { return aArg; }
  static nsTString<C> ToStored(Type aArg) { return nsTString<C>(aArg); }
  static Type FromStored(const nsTString<C>& aStored) { return aStored; }
};

// True if StoredType is a RefPtr<T> whose target should be CC-traversed.
template <typename StoredType>
struct ShouldECMATraverse : std::false_type {};
template <typename T>
struct ShouldECMATraverse<RefPtr<T>>
    : std::bool_constant<!ECMACollectionNoTraverse<T>::value> {};

// What ECMAMap::Get() returns for a value type V: a pointer to the stored
// value, except that a stored RefPtr<T> is returned as the T* it holds. That
// is only unambiguous because ECMAMap never stores a null RefPtr, which
// AssertStorable() checks.
template <typename V>
struct ECMAMapValueTraits {
  using GetResult = V*;
  using ConstGetResult = const V*;
  static GetResult Unwrap(V& aValue) { return &aValue; }
  static ConstGetResult Unwrap(const V& aValue) { return &aValue; }
  static void AssertStorable(const V&) {}
};

template <typename T>
struct ECMAMapValueTraits<RefPtr<T>> {
  using GetResult = T*;
  using ConstGetResult = T*;
  static GetResult Unwrap(RefPtr<T>& aValue) { return aValue.get(); }
  static ConstGetResult Unwrap(const RefPtr<T>& aValue) { return aValue.get(); }
  static void AssertStorable(const RefPtr<T>& aValue) {
    MOZ_ASSERT(aValue, "Get() could not tell a null value from a missing key");
  }
};

template <typename T>
struct ECMASetEntry {
  using KeyType = T;

  T mValue;

  const T& GetKey() const { return mValue; }
};

template <typename K, typename V>
struct ECMAMapEntry {
  using KeyType = K;

  K mKey;
  V mValue;

  const K& GetKey() const { return mKey; }
};

// Hash key class mPositions indexes a stored key type K with, which is what
// nsTHashMap<K, ...> would pick, except that mPositions is a secondary index
// and must not own references: a stored RefPtr<T> is indexed by the raw T*, so
// cycle collection can still unlink through mEntries.
template <typename K>
struct ECMADefaultHashKey {
  using Type = typename mozilla::detail::nsKeyClass<K>::type;
};
template <typename T>
struct ECMADefaultHashKey<RefPtr<T>> {
  using Type = typename mozilla::detail::nsKeyClass<T*>::type;
};

// Base for ECMASet and ECMAMap. Not intended for direct use.
//
// EntryT provides KeyType and GetKey(). HashKeyT is the hash key class of
// mPositions; its KeyType is the form an entry is looked up by (T* for a
// stored RefPtr<T>, const nsAString& for a stored nsString), and a stored key
// has to convert to it so that entries can be indexed.
template <typename EntryT, typename HashKeyT>
class ECMACollectionBase {
  using KeyArgTraits = ECMAArgTraits<typename EntryT::KeyType>;

 public:
  using EntryType = EntryT;
  using EntryKeyType = typename EntryT::KeyType;
  using HashKeyType = typename HashKeyT::KeyType;
  using KeyArg = typename KeyArgTraits::Type;
  static_assert(std::is_convertible_v<const EntryKeyType&, HashKeyType>,
                "HashKeyT must accept the stored key type");
  static_assert(ECMAStorable<EntryKeyType>::value,
                "GC things cannot be stored");

 protected:
  // The overloads taking the argument type of the bindings exist where it is
  // neither the stored key type nor the lookup type already.
  static constexpr bool kHasKeyArgOverloads =
      KeyArgTraits::kDistinct && !std::is_same_v<KeyArg, HashKeyType>;

 public:
  // Position of an iteration over the collection: Next() returns each entry
  // that is live when the cursor reaches it, whatever mutations happen in
  // between, and nullptr while no entry is left. The entry is only valid until
  // the collection is next mutated.
  //
  // Unlike an ES Set/Map iterator, which stays done once it has finished, a
  // forward cursor that returned nullptr returns the entries added afterwards.
  // An iterator with ES semantics, such as ConstIterator, drops its cursor at
  // the first nullptr.
  //
  // The collection keeps track of its cursors and moves them when it compacts.
  // A cursor that outlives the entries of its collection, because the
  // collection is unlinked or destroyed, is exhausted.
  class Cursor final : public LinkedListElement<Cursor> {
   public:
    explicit Cursor(
        const ECMACollectionBase& aCollection,
        ECMAIterationDirection aDirection = ECMAIterationDirection::Forward)
        : mCollection(&aCollection),
          mIndex(aDirection == ECMAIterationDirection::Forward ? 0
                 : aCollection.mEntries.IsEmpty()
                     ? kReverseEnd
                     : aCollection.mEntries.Length() - 1),
          mReverse(aDirection == ECMAIterationDirection::Reverse) {
      aCollection.mCursors.insertBack(this);
    }

    // The copy continues from the same position, independently.
    Cursor(const Cursor& aOther)
        : mCollection(aOther.mCollection),
          mIndex(aOther.mIndex),
          mReverse(aOther.mReverse) {
      if (mCollection) {
        mCollection->mCursors.insertBack(this);
      }
    }
    Cursor& operator=(const Cursor&) = delete;

    const EntryT* Next() {
      if (!mCollection) {
        return nullptr;
      }
      const auto& entries = mCollection->mEntries;
      if (mReverse) {
        while (mIndex != kReverseEnd) {
          const uint32_t index = mIndex;
          mIndex = index == 0 ? kReverseEnd : index - 1;
          if (index < entries.Length() && entries[index]) {
            return entries[index].ptr();
          }
        }
        return nullptr;
      }
      while (mIndex < entries.Length()) {
        const uint32_t index = mIndex++;
        if (entries[index]) {
          return entries[index].ptr();
        }
      }
      return nullptr;
    }

    // Whether both iterate the same collection in the same direction and
    // continue at the same position.
    bool operator==(const Cursor& aOther) const {
      return mCollection == aOther.mCollection && mIndex == aOther.mIndex &&
             mReverse == aOther.mReverse;
    }

   private:
    friend class ECMACollectionBase;

    static constexpr uint32_t kReverseEnd = UINT32_MAX;

    void MoveToEnd() {
      mIndex = mReverse ? kReverseEnd : mCollection->mEntries.Length();
    }

    // Null once the cursor is detached from its collection.
    const ECMACollectionBase* mCollection;
    // Physical index into mEntries of the next entry to examine, in the
    // direction of travel. A forward cursor ends at Length(), so it still
    // visits the entries added afterwards. A reverse cursor ends at
    // kReverseEnd and stays there, as nothing is ever prepended.
    uint32_t mIndex;
    bool mReverse;
  };

  // C++ iterator over an ECMASet or ECMAMap, a standard const forward iterator
  // on top of the collection's Cursor: operator* is the entry Next() last
  // returned.
  // A default-constructed iterator is exhausted, which is what end() returns.
  //
  // Like references into any container, what operator* returns is only valid
  // until the collection is mutated. The iterator itself stays usable, so
  // incrementing it after a mutation visits exactly the entries an ES iterator
  // would.
  template <ECMAIterationDirection Direction>
  class ConstIterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = EntryT;
    using difference_type = std::ptrdiff_t;
    using pointer = const EntryT*;
    using reference = const EntryT&;

    ConstIterator() = default;

    explicit ConstIterator(const ECMACollectionBase& aCollection) {
      mCursor.emplace(aCollection, Direction);
      Advance();
    }

    ConstIterator(const ConstIterator& aOther) = default;

    ConstIterator& operator=(const ConstIterator& aOther) {
      if (this == &aOther) {
        return *this;
      }
      mCursor.reset();
      if (aOther.mCursor) {
        mCursor.emplace(*aOther.mCursor);
      }
      mCurrent = aOther.mCurrent;
      return *this;
    }

    reference operator*() const {
      MOZ_ASSERT(mCurrent, "dereferencing an exhausted iterator");
      return *mCurrent;
    }
    pointer operator->() const { return &**this; }

    ConstIterator& operator++() {
      Advance();
      return *this;
    }
    ConstIterator operator++(int) {
      ConstIterator old(*this);
      Advance();
      return old;
    }

    // Exhausted iterators, which includes end(), are all equal. Iterators that
    // are not are equal if they are at the same position.
    bool operator==(const ConstIterator& aOther) const {
      return mCursor == aOther.mCursor;
    }
    bool operator!=(const ConstIterator& aOther) const {
      return !(*this == aOther);
    }

   private:
    void Advance() {
      const auto* entry = mCursor->Next();
      if (!entry) {
        mCurrent = nullptr;
        mCursor.reset();
      } else {
        mCurrent = entry;
      }
    }

    Maybe<Cursor> mCursor;
    const EntryT* mCurrent = nullptr;
  };

  Cursor CreateCursor() const { return Cursor(*this); }
  Cursor CreateReverseCursor() const {
    return Cursor(*this, ECMAIterationDirection::Reverse);
  }

 protected:
  ECMACollectionBase() = default;

  ~ECMACollectionBase() {
    // A cursor left linked here would unlink itself through freed memory
    // later on.
    DetachCursors();
  }

 public:
  // Registered cursors point at a specific collection, so a collection cannot
  // be relocated or duplicated.
  ECMACollectionBase(ECMACollectionBase&&) = delete;
  ECMACollectionBase& operator=(ECMACollectionBase&&) = delete;
  ECMACollectionBase(const ECMACollectionBase&) = delete;
  ECMACollectionBase& operator=(const ECMACollectionBase&) = delete;

  uint32_t Size() const { return mPositions.Count(); }
  bool IsEmpty() const { return mPositions.IsEmpty(); }

  bool Has(HashKeyType aKey) const { return mPositions.Contains(aKey); }
  bool Has(KeyArg aKey) const
    requires(kHasKeyArgOverloads)
  {
    return Has(KeyArgTraits::ToLookup(aKey));
  }

  // Destroying an entry can release the last reference to an object whose
  // destructor uses this collection again, so the mutating methods move the
  // entries they drop out and only destroy them once they are done.

  bool Delete(HashKeyType aKey) {
    auto index = mPositions.Extract(aKey);
    if (!index) {
      return false;
    }
    MOZ_DIAGNOSTIC_ASSERT(mEntries[*index]);
    [[maybe_unused]] auto deleted = mEntries[*index].extract();
    MaybeCompact();
    return true;
  }
  bool Delete(KeyArg aKey)
    requires(kHasKeyArgOverloads)
  {
    return Delete(KeyArgTraits::ToLookup(aKey));
  }

  // What compacting after tombstoning every entry would leave: no entries,
  // and every iteration resuming at the (new) end.
  void Clear() {
    nsTArray<Maybe<EntryT>> cleared = std::move(mEntries);
    mPositions.Clear();
    for (Cursor* cursor = mCursors.getFirst(); cursor;
         cursor = cursor->getNext()) {
      cursor->MoveToEnd();
    }
  }

  // --- Cycle collection support ---

  void Unlink() {
    // Cycle collection unlinks in no particular order, so a cursor can
    // outlive this call.
    DetachCursors();
    nsTArray<Maybe<EntryT>> unlinked = std::move(mEntries);
    mPositions.Clear();
  }

  template <typename F>
  void ForEachLiveEntry(F&& aCallback) const {
    for (const auto& entry : mEntries) {
      if (entry) {
        aCallback(*entry);
      }
    }
  }

  template <typename F>
  void ForEachLiveEntry(F&& aCallback) {
    for (auto& entry : mEntries) {
      if (entry) {
        aCallback(*entry);
      }
    }
  }

 protected:
  // A detached cursor is exhausted instead of reading entries that are
  // gone.
  void DetachCursors() {
    while (Cursor* cursor = mCursors.popFirst()) {
      cursor->mCollection = nullptr;
    }
  }

  void MaybeCompact() {
    const uint32_t oldLength = mEntries.Length();
    const uint32_t liveCount = mPositions.Count();
    if (oldLength - liveCount <= liveCount) {
      return;
    }

    // liveBefore[i] is the number of live entries before old index i, which is
    // where the first live entry at or after i ends up.
    nsTArray<uint32_t> liveBefore(oldLength + 1);
    nsTArray<Maybe<EntryT>> compacted(liveCount);
    for (uint32_t i = 0; i < oldLength; ++i) {
      liveBefore.AppendElement(compacted.Length());
      if (mEntries[i]) {
        auto position = mPositions.Lookup(mEntries[i]->GetKey());
        MOZ_DIAGNOSTIC_ASSERT(position);
        position.Data() = compacted.Length();
        compacted.AppendElement(std::move(mEntries[i]));
      }
    }
    liveBefore.AppendElement(compacted.Length());

    for (Cursor* cursor = mCursors.getFirst(); cursor;
         cursor = cursor->getNext()) {
      if (!cursor->mReverse) {
        // A forward cursor resumes on the first live entry at or after its
        // position.
        MOZ_DIAGNOSTIC_ASSERT(cursor->mIndex <= oldLength);
        cursor->mIndex = liveBefore[cursor->mIndex];
        continue;
      }
      if (cursor->mIndex == Cursor::kReverseEnd) {
        continue;
      }
      // A reverse cursor resumes on the last live entry at or before its
      // position.
      MOZ_DIAGNOSTIC_ASSERT(cursor->mIndex < oldLength);
      const uint32_t liveUpTo = liveBefore[cursor->mIndex + 1];
      cursor->mIndex = liveUpTo == 0 ? Cursor::kReverseEnd : liveUpTo - 1;
    }

    mEntries = std::move(compacted);
  }

  // mEntries owns the data in insertion order and holds the tombstones;
  // mPositions indexes only the live entries.
  nsTArray<Maybe<EntryT>> mEntries;
  nsTHashMap<HashKeyT, uint32_t> mPositions;
  mutable LinkedList<Cursor> mCursors;
};

}  // namespace binding_detail

// ============================================================================
// ECMASet
// ============================================================================

template <typename T, typename HashKeyT =
                          typename binding_detail::ECMADefaultHashKey<T>::Type>
class ECMASet
    : public binding_detail::ECMACollectionBase<binding_detail::ECMASetEntry<T>,
                                                HashKeyT> {
  using Base =
      binding_detail::ECMACollectionBase<binding_detail::ECMASetEntry<T>,
                                         HashKeyT>;
  using ArgTraits = binding_detail::ECMAArgTraits<T>;

 public:
  // Returns true if the key was newly inserted.
  bool Add(const T& aKey) {
    return AddImpl(aKey, [&]() -> const T& { return aKey; });
  }
  bool Add(T&& aKey) {
    return AddImpl(aKey, [&]() -> T&& { return std::move(aKey); });
  }
  bool Add(typename ArgTraits::Type aKey)
    requires(ArgTraits::kDistinct)
  {
    return AddImpl(ArgTraits::ToLookup(aKey),
                   [&]() { return ArgTraits::ToStored(aKey); });
  }

  template <binding_detail::ECMAIterationDirection Direction>
  class ConstValueIterator : public Base::template ConstIterator<Direction> {
    using IteratorBase = typename Base::template ConstIterator<Direction>;

   public:
    using value_type = T;
    using pointer = const T*;
    using reference = const T&;
    using IteratorBase::IteratorBase;

    ConstValueIterator() = default;
    reference operator*() const { return IteratorBase::operator*().mValue; }
    pointer operator->() const { return &**this; }
    ConstValueIterator& operator++() {
      IteratorBase::operator++();
      return *this;
    }
    ConstValueIterator operator++(int) {
      ConstValueIterator old(*this);
      IteratorBase::operator++();
      return old;
    }
  };

  using value_type = T;
  using const_iterator =
      ConstValueIterator<binding_detail::ECMAIterationDirection::Forward>;
  using const_reverse_iterator =
      ConstValueIterator<binding_detail::ECMAIterationDirection::Reverse>;
  using iterator = const_iterator;
  using reverse_iterator = const_reverse_iterator;

  // Only const iteration is provided. Mutating keys through an iterator
  // would desync mPositions; use Add/Delete instead.
  const_iterator begin() const { return const_iterator(*this); }
  const_iterator end() const { return const_iterator(); }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(*this);
  }
  const_reverse_iterator rend() const { return const_reverse_iterator(); }

 private:
  // aMakeKeyFunc produces the key to store, which is only needed if aLookup is
  // not present yet.
  template <typename MakeKeyF>
  bool AddImpl(typename Base::HashKeyType aLookup, MakeKeyF&& aMakeKeyFunc) {
    return this->mPositions.WithEntryHandle(aLookup, [&](auto&& entry) {
      if (entry) {
        return false;
      }
      entry.Insert(this->mEntries.Length());
      this->mEntries.AppendElement()->emplace(aMakeKeyFunc());
      return true;
    });
  }
};

// ============================================================================
// ECMAMap
// ============================================================================

template <typename K, typename V,
          typename HashKeyK =
              typename binding_detail::ECMADefaultHashKey<K>::Type>
class ECMAMap : public binding_detail::ECMACollectionBase<
                    binding_detail::ECMAMapEntry<K, V>, HashKeyK> {
  using Base =
      binding_detail::ECMACollectionBase<binding_detail::ECMAMapEntry<K, V>,
                                         HashKeyK>;
  using Entry = binding_detail::ECMAMapEntry<K, V>;
  using ValueTraits = binding_detail::ECMAMapValueTraits<V>;
  using KeyArgTraits = binding_detail::ECMAArgTraits<K>;
  using ValueArgTraits = binding_detail::ECMAArgTraits<V>;
  static_assert(binding_detail::ECMAStorable<V>::value,
                "GC things cannot be stored");

 public:
  using HashKeyType = typename Base::HashKeyType;
  using KeyArg = typename Base::KeyArg;

  // Returns the value for aKey, or nullptr if aKey is not present. The pointer
  // is only valid until the map is next mutated.
  typename ValueTraits::GetResult Get(HashKeyType aKey) {
    auto index = this->mPositions.MaybeGet(aKey);
    if (!index) {
      return nullptr;
    }
    return ValueTraits::Unwrap(this->mEntries[*index]->mValue);
  }

  typename ValueTraits::ConstGetResult Get(HashKeyType aKey) const {
    auto index = this->mPositions.MaybeGet(aKey);
    if (!index) {
      return nullptr;
    }
    return ValueTraits::Unwrap(this->mEntries[*index]->mValue);
  }

  typename ValueTraits::GetResult Get(KeyArg aKey)
    requires(Base::kHasKeyArgOverloads)
  {
    return Get(KeyArgTraits::ToLookup(aKey));
  }

  typename ValueTraits::ConstGetResult Get(KeyArg aKey) const
    requires(Base::kHasKeyArgOverloads)
  {
    return Get(KeyArgTraits::ToLookup(aKey));
  }

  // If the key already exists, updates the value in-place (preserving
  // insertion order). Returns true if newly inserted.
  bool Set(const K& aKey, const V& aValue) {
    return SetImpl(aKey, [&]() -> const K& { return aKey; }, aValue);
  }
  bool Set(const K& aKey, V&& aValue) {
    return SetImpl(aKey, [&]() -> const K& { return aKey; }, std::move(aValue));
  }
  bool Set(K&& aKey, V&& aValue) {
    return SetImpl(
        aKey, [&]() -> K&& { return std::move(aKey); }, std::move(aValue));
  }
  bool Set(KeyArg aKey, typename ValueArgTraits::Type aValue)
    requires(KeyArgTraits::kDistinct || ValueArgTraits::kDistinct)
  {
    return SetImpl(
        KeyArgTraits::ToLookup(aKey),
        [&]() -> decltype(auto) { return KeyArgTraits::ToStored(aKey); },
        ValueArgTraits::ToStored(aValue));
  }

  using value_type = Entry;
  using const_iterator = typename Base::template ConstIterator<
      binding_detail::ECMAIterationDirection::Forward>;
  using const_reverse_iterator = typename Base::template ConstIterator<
      binding_detail::ECMAIterationDirection::Reverse>;
  using iterator = const_iterator;
  using reverse_iterator = const_reverse_iterator;

  // Only const iteration is provided. Mutating keys through an iterator
  // would desync mPositions; use Set/Delete instead. The iterators yield
  // entries with mKey and mValue members, so structured bindings work.
  const_iterator begin() const { return const_iterator(*this); }
  const_iterator end() const { return const_iterator(); }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(*this);
  }
  const_reverse_iterator rend() const { return const_reverse_iterator(); }

 private:
  // aMakeKeyFunc produces the key to store, which is only needed if aLookup is
  // not present yet.
  template <typename MakeKeyF, typename ValueArg>
  bool SetImpl(HashKeyType aLookup, MakeKeyF&& aMakeKeyFunc,
               ValueArg&& aValue) {
    ValueTraits::AssertStorable(aValue);
    // Released only once the map is consistent again, see Delete().
    Maybe<V> replaced;
    return this->mPositions.WithEntryHandle(aLookup, [&](auto&& entry) {
      if (entry) {
        replaced = Some(std::exchange(this->mEntries[entry.Data()]->mValue,
                                      std::forward<ValueArg>(aValue)));
        return false;
      }
      entry.Insert(this->mEntries.Length());
      this->mEntries.AppendElement()->emplace(aMakeKeyFunc(),
                                              std::forward<ValueArg>(aValue));
      return true;
    });
  }
};

}  // namespace mozilla::dom

// --- Cycle collection support ---

template <typename T, typename HashKeyT>
inline void ImplCycleCollectionUnlink(
    mozilla::dom::ECMASet<T, HashKeyT>& aField) {
  aField.Unlink();
}

template <typename T, typename HashKeyT, typename Callback>
inline void ImplCycleCollectionIndexedContainer(
    mozilla::dom::ECMASet<T, HashKeyT>& aField, Callback&& aCallback) {
  if constexpr (mozilla::dom::binding_detail::ShouldECMATraverse<T>::value) {
    aField.ForEachLiveEntry([&](auto& aEntry) { aCallback(aEntry.mValue); });
  }
}

template <typename K, typename V, typename HashKeyK>
inline void ImplCycleCollectionUnlink(
    mozilla::dom::ECMAMap<K, V, HashKeyK>& aField) {
  aField.Unlink();
}

template <typename K, typename V, typename HashKeyK, typename Callback>
inline void ImplCycleCollectionIndexedContainer(
    mozilla::dom::ECMAMap<K, V, HashKeyK>& aField, Callback&& aCallback) {
  constexpr bool traverseKeys =
      mozilla::dom::binding_detail::ShouldECMATraverse<K>::value;
  constexpr bool traverseValues =
      mozilla::dom::binding_detail::ShouldECMATraverse<V>::value;
  if constexpr (traverseKeys || traverseValues) {
    aField.ForEachLiveEntry([&](auto& aEntry) {
      if constexpr (traverseKeys) {
        aCallback(aEntry.mKey);
      }
      if constexpr (traverseValues) {
        aCallback(aEntry.mValue);
      }
    });
  }
}

#endif  // mozilla_dom_ECMACollections_h
