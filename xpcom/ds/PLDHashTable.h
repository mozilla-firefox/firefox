/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// See the comment at the top of mfbt/HashTable.h for a comparison between
// PLDHashTable and mozilla::HashTable.

#ifndef PLDHashTable_h
#define PLDHashTable_h

#include <utility>

#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/CheckedArithmetic.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/Maybe.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/fallible.h"
#include "nscore.h"

using PLDHashNumber = mozilla::HashNumber;
static const uint32_t kPLDHashNumberBits = mozilla::kHashNumberBits;

#if defined(DEBUG) || defined(FUZZING)
#  define MOZ_HASH_TABLE_CHECKS_ENABLED 1
#endif

class PLDHashTable;
struct PLDHashTableOps;

// Table entry header structure.
//
// In order to allow in-line allocation of key and value, we do not declare
// either here. Instead, the API uses const void *key as a formal parameter.
// The key need not be stored in the entry; it may be part of the value, but
// need not be stored at all.
//
// Callback types are defined below and grouped into the PLDHashTableOps
// structure, for single static initialization per hash table sub-type.
//
// Each hash table sub-type should make its entry type a subclass of
// PLDHashEntryHdr. PLDHashEntryHdr is merely a common superclass to present a
// uniform interface to PLDHashTable clients. The zero-sized base class
// optimization, employed by all of our supported C++ compilers, will ensure
// that this abstraction does not make objects needlessly larger.
struct PLDHashEntryHdr {
  PLDHashEntryHdr() = default;
  PLDHashEntryHdr(const PLDHashEntryHdr&) = delete;
  PLDHashEntryHdr& operator=(const PLDHashEntryHdr&) = delete;
  PLDHashEntryHdr(PLDHashEntryHdr&&) = default;
  PLDHashEntryHdr& operator=(PLDHashEntryHdr&&) = default;

 private:
  friend class PLDHashTable;
};

#ifdef MOZ_HASH_TABLE_CHECKS_ENABLED

// This class does three kinds of checking:
//
// - that calls to one of |mOps| or to an enumerator do not cause re-entry into
//   the table in an unsafe way;
//
// - that multiple threads do not access the table in an unsafe way;
//
// - that a table marked as immutable is not modified.
//
// "Safe" here means that multiple concurrent read operations are ok, but a
// write operation (i.e. one that can cause the entry storage to be reallocated
// or destroyed) cannot safely run concurrently with another read or write
// operation. This meaning of "safe" is only partial; for example, it does not
// cover whether a single entry in the table is modified by two separate
// threads. (Doing such checking would be much harder.)
//
// It does this with two variables:
//
// - mState, which embodies a tri-stage tagged union with the following
//   variants:
//   - Idle
//   - Read(n), where 'n' is the number of concurrent read operations
//   - Write
//
// - mIsWritable, which indicates if the table is mutable.
//
class Checker {
 public:
  constexpr Checker() = default;

  Checker& operator=(Checker&& aOther) {
    // Atomic<> doesn't have an |operator=(Atomic<>&&)|.
    mState = aOther.mState.load();
    mIsWritable = aOther.mIsWritable.load();

    aOther.mState = kIdle;
    // XXX Shouldn't we set mIsWritable to true here for consistency?

    return *this;
  }

  static bool IsIdle(uint32_t aState) { return aState == kIdle; }
  static bool IsRead(uint32_t aState) {
    return kRead1 <= aState && aState <= kReadMax;
  }
  static bool IsRead1(uint32_t aState) { return aState == kRead1; }
  static bool IsWrite(uint32_t aState) { return aState == kWrite; }

  bool IsIdle() const { return mState == kIdle; }

  bool IsWritable() const { return mIsWritable; }

  void SetNonWritable() { mIsWritable = false; }

  // NOTE: the obvious way to implement these functions is to (a) check
  // |mState| is reasonable, and then (b) update |mState|. But the lack of
  // atomicity in such an implementation can cause problems if we get unlucky
  // thread interleaving between (a) and (b).
  //
  // So instead for |mState| we are careful to (a) first get |mState|'s old
  // value and assign it a new value in single atomic operation, and only then
  // (b) check the old value was reasonable. This ensures we don't have
  // interleaving problems.
  //
  // For |mIsWritable| we don't need to be as careful because it can only in
  // transition in one direction (from writable to non-writable).

  void StartReadOp() {
    uint32_t oldState = mState++;  // this is an atomic increment
    MOZ_RELEASE_ASSERT(IsIdle(oldState) || IsRead(oldState));
    MOZ_RELEASE_ASSERT(oldState < kReadMax);  // check for overflow
  }

  void EndReadOp() {
    uint32_t oldState = mState--;  // this is an atomic decrement
    MOZ_RELEASE_ASSERT(IsRead(oldState));
  }

  void StartWriteOp() {
    MOZ_RELEASE_ASSERT(IsWritable());
    uint32_t oldState = mState.exchange(kWrite);
    MOZ_RELEASE_ASSERT(IsIdle(oldState));
  }

  void EndWriteOp() {
    // Check again that the table is writable, in case it was marked as
    // non-writable just after the IsWritable() assertion in StartWriteOp()
    // occurred.
    MOZ_RELEASE_ASSERT(IsWritable());
    uint32_t oldState = mState.exchange(kIdle);
    MOZ_RELEASE_ASSERT(IsWrite(oldState));
  }

  void StartIteratorRemovalOp() {
    // When doing removals at the end of iteration, we go from Read1 state to
    // Write and then back.
    MOZ_RELEASE_ASSERT(IsWritable());
    uint32_t oldState = mState.exchange(kWrite);
    MOZ_RELEASE_ASSERT(IsRead1(oldState));
  }

  void EndIteratorRemovalOp() {
    // Check again that the table is writable, in case it was marked as
    // non-writable just after the IsWritable() assertion in
    // StartIteratorRemovalOp() occurred.
    MOZ_RELEASE_ASSERT(IsWritable());
    uint32_t oldState = mState.exchange(kRead1);
    MOZ_RELEASE_ASSERT(IsWrite(oldState));
  }

  void StartDestructorOp() {
    // A destructor op is like a write, but the table doesn't need to be
    // writable.
    uint32_t oldState = mState.exchange(kWrite);
    MOZ_RELEASE_ASSERT(IsIdle(oldState));
  }

  void EndDestructorOp() {
    uint32_t oldState = mState.exchange(kIdle);
    MOZ_RELEASE_ASSERT(IsWrite(oldState));
  }

 private:
  // Things of note about the representation of |mState|.
  // - The values between kRead1..kReadMax represent valid Read(n) values.
  // - kIdle and kRead1 are deliberately chosen so that incrementing the -
  //   former gives the latter.
  // - 9999 concurrent readers should be enough for anybody.
  static const uint32_t kIdle = 0;
  static const uint32_t kRead1 = 1;
  static const uint32_t kReadMax = 9999;
  static const uint32_t kWrite = 10000;

  mozilla::Atomic<uint32_t, mozilla::SequentiallyConsistent> mState{kIdle};
  mozilla::Atomic<bool, mozilla::SequentiallyConsistent> mIsWritable{true};
};
#endif

// A PLDHashTable may be allocated on the stack or within another structure or
// class. No entry storage is allocated until the first element is added. This
// means that empty hash tables are cheap, which is good because they are
// common.
//
// There used to be a long, math-heavy comment here about the merits of
// double hashing vs. chaining; it was removed in bug 1058335. In short, double
// hashing is more space-efficient unless the element size gets large (in which
// case you should keep using double hashing but switch to using pointer
// elements). Also, with double hashing, you can't safely hold an entry pointer
// and use it after an add or remove operation, unless you sample Generation()
// before adding or removing, and compare the sample after, dereferencing the
// entry pointer only if Generation() has not changed.
class PLDHashTable {
 private:
  // A slot represents a cached hash value and its associated entry stored in
  // the hash table. The hash value and the entry are not stored contiguously.
  struct Slot {
    Slot(PLDHashEntryHdr* aEntry, PLDHashNumber* aKeyHash)
        : mEntry(aEntry), mKeyHash(aKeyHash) {}

    Slot(const Slot&) = default;
    Slot(Slot&& aOther) = default;

    Slot& operator=(Slot&& aOther) = default;

    bool operator==(const Slot& aOther) const {
      return mEntry == aOther.mEntry;
    }

    PLDHashNumber KeyHash() const { return *HashPtr(); }
    void SetKeyHash(PLDHashNumber aHash) { *HashPtr() = aHash; }

    PLDHashEntryHdr* ToEntry() const { return mEntry; }

    bool IsFree() const { return KeyHash() == 0; }
    bool IsRemoved() const { return KeyHash() == 1; }
    bool IsLive() const { return IsLiveHash(KeyHash()); }
    static bool IsLiveHash(uint32_t aHash) { return aHash >= 2; }

    void MarkFree() { *HashPtr() = 0; }
    void MarkRemoved() { *HashPtr() = 1; }
    void MarkColliding() { *HashPtr() |= kCollisionFlag; }

    void Next(uint32_t aEntrySize) {
      char* p = reinterpret_cast<char*>(mEntry);
      p += aEntrySize;
      mEntry = reinterpret_cast<PLDHashEntryHdr*>(p);
      mKeyHash++;
    }
    PLDHashNumber* HashPtr() const { return mKeyHash; }

   private:
    PLDHashEntryHdr* mEntry;
    PLDHashNumber* mKeyHash;
  };

  // This class maintains the invariant that every time the entry store is
  // changed, the generation is updated.
  //
  // The data layout separates the cached hashes of entries and the entries
  // themselves to save space. We could store the entries thusly:
  //
  // +--------+--------+---------+
  // | entry0 | entry1 | ...     |
  // +--------+--------+---------+
  //
  // where the entries themselves contain the cached hash stored as their
  // first member. PLDHashTable did this for a long time, with entries looking
  // like:
  //
  // class PLDHashEntryHdr
  // {
  //   PLDHashNumber mKeyHash;
  // };
  //
  // class MyEntry : public PLDHashEntryHdr
  // {
  //   ...
  // };
  //
  // The problem with this setup is that, depending on the layout of
  // `MyEntry`, there may be platform ABI-mandated padding between `mKeyHash`
  // and the first member of `MyEntry`. This ABI-mandated padding is wasted
  // space, and was surprisingly common, e.g. when MyEntry contained a single
  // pointer on 64-bit platforms.
  //
  // As previously alluded to, the current setup stores things thusly:
  //
  // +-------+-------+-------+-------+--------+--------+---------+
  // | hash0 | hash1 | ..... | hashN | entry0 | entry1 | ...     |
  // +-------+-------+-------+-------+--------+--------+---------+
  //
  // which contains no wasted space between the hashes themselves, and no
  // wasted space between the entries themselves. malloc is guaranteed to
  // return blocks of memory with at least word alignment on all of our major
  // platforms. PLDHashTable mandates that the size of the hash table is
  // always a power of two, so the alignment of the memory containing the
  // first entry is always at least the alignment of the entire entry store.
  // That means the alignment of `entry0` should be its natural alignment.
  // Entries may have problems if they contain over-aligned members such as
  // SIMD vector types, but this has not been a problem in practice.
  //
  // Note: It would be natural to store the generation within this class, but
  // we can't do that without bloating sizeof(PLDHashTable) on 64-bit machines.
  // So instead we store it outside this class, and Set() takes a pointer to it
  // and ensures it is updated as necessary.
  class EntryStore {
   private:
    char* mEntryStore;

    static char* Entries(char* aStore, uint32_t aCapacity) {
      return aStore + aCapacity * sizeof(PLDHashNumber);
    }

    char* Entries(uint32_t aCapacity) const {
      return Entries(Get(), aCapacity);
    }

   public:
    constexpr EntryStore() : mEntryStore(nullptr) {}

    ~EntryStore() {
      free(mEntryStore);
      mEntryStore = nullptr;
    }

    char* Get() const { return mEntryStore; }
    bool IsAllocated() const { return !!mEntryStore; }

    Slot SlotForIndex(uint32_t aIndex, uint32_t aEntrySize,
                      uint32_t aCapacity) const {
      char* entries = Entries(aCapacity);
      auto entry =
          reinterpret_cast<PLDHashEntryHdr*>(entries + aIndex * aEntrySize);
      auto hashes = reinterpret_cast<PLDHashNumber*>(Get());
      return Slot(entry, &hashes[aIndex]);
    }

    Slot SlotForPLDHashEntry(PLDHashEntryHdr* aEntry, uint32_t aCapacity,
                             uint32_t aEntrySize) {
      char* entries = Entries(aCapacity);
      char* entry = reinterpret_cast<char*>(aEntry);
      uint32_t entryOffset = entry - entries;
      uint32_t slotIndex = entryOffset / aEntrySize;
      return SlotForIndex(slotIndex, aEntrySize, aCapacity);
    }

    template <typename F>
    void ForEachSlot(uint32_t aCapacity, uint32_t aEntrySize, F&& aFunc) {
      ForEachSlot(Get(), aCapacity, aEntrySize, std::forward<F>(aFunc));
    }

    template <typename F>
    static void ForEachSlot(char* aStore, uint32_t aCapacity,
                            uint32_t aEntrySize, F&& aFunc) {
      char* entries = Entries(aStore, aCapacity);
      Slot slot(reinterpret_cast<PLDHashEntryHdr*>(entries),
                reinterpret_cast<PLDHashNumber*>(aStore));
      for (size_t i = 0; i < aCapacity; ++i) {
        aFunc(slot);
        slot.Next(aEntrySize);
      }
    }

    void Set(char* aEntryStore, uint16_t* aGeneration) {
      mEntryStore = aEntryStore;
      *aGeneration += 1;
    }
  };

  // These fields are packed carefully. On 32-bit platforms,
  // sizeof(PLDHashTable) is 20. On 64-bit platforms, sizeof(PLDHashTable) is
  // 32; 28 bytes of data followed by 4 bytes of padding for alignment.
  const PLDHashTableOps* const mOps =
      nullptr;                   // Virtual operations; see below.
  EntryStore mEntryStore;        // (Lazy) entry storage and generation.
  uint16_t mGeneration = 0;      // The storage generation.
  uint8_t mHashShift = 0;        // Multiplicative hash shift.
  const uint8_t mEntrySize = 0;  // Number of bytes in an entry.
  uint32_t mEntryCount = 0;      // Number of entries in table.
  uint32_t mRemovedCount = 0;    // Removed entry sentinels in table.

#ifdef MOZ_HASH_TABLE_CHECKS_ENABLED
  mutable Checker mChecker;
#endif

 public:
  // Table capacity limit; do not exceed. The max capacity used to be 1<<23 but
  // that occasionally that wasn't enough. Making it much bigger than 1<<26
  // probably isn't worthwhile -- tables that big are kind of ridiculous.
  // Also, the growth operation will (deliberately) fail if |capacity *
  // mEntrySize| overflows a uint32_t, and mEntrySize is always at least 8
  // bytes.
  static const uint32_t kMaxCapacity = ((uint32_t)1 << 26);

  static const uint32_t kMinCapacity = 8;

  // Making this half of kMaxCapacity ensures it'll fit. Nobody should need an
  // initial length anywhere nearly this large, anyway.
  static const uint32_t kMaxInitialLength = kMaxCapacity / 2;

  // This gives a default initial capacity of 8.
  static const uint32_t kDefaultInitialLength = 4;

  // Initialize the table with |aOps| and |aEntrySize|. The table's initial
  // capacity is chosen such that |aLength| elements can be inserted without
  // rehashing; if |aLength| is a power-of-two, this capacity will be
  // |2*length|. However, because entry storage is allocated lazily, this
  // initial capacity won't be relevant until the first element is added; prior
  // to that the capacity will be zero.
  //
  // This will crash if |aEntrySize| and/or |aLength| are too large.
  constexpr PLDHashTable(const PLDHashTableOps* aOps, uint32_t aEntrySize,
                         uint32_t aLength = kDefaultInitialLength)
      : mOps(aOps),
        mHashShift(HashShift(aEntrySize, aLength)),
        mEntrySize(static_cast<uint8_t>(aEntrySize)) {
    // An entry size greater than 0xff is unlikely, but let's check anyway.
    // If you hit this, your hashtable would waste lots of space for unused
    // entries and you should change your hash table's entries to pointers.
    if (aEntrySize > std::numeric_limits<uint8_t>::max()) {
      MOZ_CRASH("Entry size is too large");
    }
  }

  PLDHashTable(PLDHashTable&& aOther)
  // Rely on default-initialize of fields which are checked by the move
  // assignment operator and the destructor (which the move assignment
  // operator calls).
  {
    *this = std::move(aOther);
  }

  PLDHashTable& operator=(PLDHashTable&& aOther);

  PLDHashTable(const PLDHashTable& aOther) = delete;
  PLDHashTable& operator=(const PLDHashTable& aOther) = delete;

  ~PLDHashTable();

  // This should be used rarely.
  const PLDHashTableOps* Ops() const { return mOps; }

  // Size in entries (gross, not net of free and removed sentinels) for table.
  // This can be zero if no elements have been added yet, in which case the
  // entry storage will not have yet been allocated.
  uint32_t Capacity() const {
    return mEntryStore.IsAllocated() ? CapacityFromHashShift() : 0;
  }

  uint32_t EntrySize() const { return mEntrySize; }
  uint32_t EntryCount() const { return mEntryCount; }
  bool IsEmpty() const { return mEntryCount == 0; }
  uint32_t Generation() const { return mGeneration; }

  // To search for a |key| in |table|, call:
  //
  //   entry = table.Search(key);
  //
  // If |entry| is non-null, |key| was found. If |entry| is null, key was not
  // found.
  PLDHashEntryHdr* Search(const void* aKey) const;

  // To add an entry identified by |key| to table, call:
  //
  //   entry = table.Add(key, mozilla::fallible);
  //
  // If |entry| is null upon return, then the table is severely overloaded and
  // memory can't be allocated for entry storage.
  //
  // Otherwise, if the initEntry hook was provided, |entry| will be
  // initialized.  If the initEntry hook was not provided, the caller
  // should initialize |entry| as appropriate.
  PLDHashEntryHdr* Add(const void* aKey, const mozilla::fallible_t&);

  // This is like the other Add() function, but infallible, and so never
  // returns null.
  PLDHashEntryHdr* Add(const void* aKey);

  // To remove an entry identified by |key| from table, call:
  //
  //   table.Remove(key);
  //
  // If |key|'s entry is found, it is cleared (via table->mOps->clearEntry).
  // The table's capacity may be reduced afterwards.
  void Remove(const void* aKey);

  // To remove an entry found by a prior search, call:
  //
  //   table.RemoveEntry(entry);
  //
  // The entry, which must be present and in use, is cleared (via
  // table->mOps->clearEntry). The table's capacity may be reduced afterwards.
  void RemoveEntry(PLDHashEntryHdr* aEntry);

  // Remove an entry already accessed via Search() or Add().
  //
  // NB: this is a "raw" or low-level method. It does not shrink the table if
  // it is underloaded. Don't use it unless necessary and you know what you are
  // doing, and if so, please explain in a comment why it is necessary instead
  // of RemoveEntry().
  void RawRemove(PLDHashEntryHdr* aEntry);

  // This function is equivalent to
  // ClearAndPrepareForLength(kDefaultInitialLength).
  void Clear();

  // Removes all entries from the table but keeps the entry storage allocated,
  // so the table retains its current capacity. Use this instead of Clear()
  // when the table is about to be re-populated and the repeated free/realloc
  // of the entry store would be wasteful (e.g. a cache that is invalidated and
  // rebuilt frequently). Unlike Clear(), the capacity is never reduced.
  void ClearAndRetainStorage();

  // This function clears the table's contents and frees its entry storage,
  // leaving it in a empty state ready to be used again. Afterwards, when the
  // first element is added the entry storage that gets allocated will have a
  // capacity large enough to fit |aLength| elements without rehashing.
  //
  // It's conceptually the same as calling the destructor and then re-calling
  // the constructor with the original |aOps| and |aEntrySize| arguments, and
  // a new |aLength| argument.
  void ClearAndPrepareForLength(uint32_t aLength);

  // Measure the size of the table's entry storage. If the entries contain
  // pointers to other heap blocks, you have to iterate over the table and
  // measure those separately; hence the "Shallow" prefix.
  size_t ShallowSizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

  // Like ShallowSizeOfExcludingThis(), but includes sizeof(*this).
  size_t ShallowSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

  // Mark a table as immutable for the remainder of its lifetime. This
  // changes the implementation from asserting one set of invariants to
  // asserting a different set.
  void MarkImmutable() {
#ifdef MOZ_HASH_TABLE_CHECKS_ENABLED
    mChecker.SetNonWritable();
#endif
  }

  // If you use PLDHashEntryStub or a subclass of it as your entry struct, and
  // if your entries move via memcpy and clear via memset(0), you can use these
  // stub operations.
  static const PLDHashTableOps* StubOps();

  // The individual stub operations in StubOps().
  static PLDHashNumber HashVoidPtrKeyStub(const void* aKey);
  static bool MatchEntryStub(const PLDHashEntryHdr* aEntry, const void* aKey);
  static void MoveEntryStub(PLDHashTable* aTable, const PLDHashEntryHdr* aFrom,
                            PLDHashEntryHdr* aTo);
  static void ClearEntryStub(PLDHashTable* aTable, PLDHashEntryHdr* aEntry);

  // Hash/match operations for tables holding C strings.
  static PLDHashNumber HashStringKey(const void* aKey);
  static bool MatchStringKey(const PLDHashEntryHdr* aEntry, const void* aKey);

  class EntryHandle {
   public:
    EntryHandle(EntryHandle&& aOther) noexcept;
#ifdef MOZ_HASH_TABLE_CHECKS_ENABLED
    ~EntryHandle();
#endif

    EntryHandle(const EntryHandle&) = delete;
    EntryHandle& operator=(const EntryHandle&) = delete;
    EntryHandle& operator=(EntryHandle&& aOther) = delete;

    // Is this slot currently occupied?
    bool HasEntry() const { return mSlot.IsLive(); }

    explicit operator bool() const { return HasEntry(); }

    // Get the entry stored in this slot. May not be called unless the slot is
    // currently occupied.
    PLDHashEntryHdr* Entry() {
      MOZ_ASSERT(HasEntry());
      return mSlot.ToEntry();
    }

    template <class F>
    void Insert(F&& aInitEntry) {
      MOZ_ASSERT(!HasEntry());
      OccupySlot();
      std::forward<F>(aInitEntry)(Entry());
    }

    // If the slot is currently vacant, the slot is occupied and `initEntry` is
    // invoked to initialize the entry. Returns the entry stored in now-occupied
    // slot.
    template <class F>
    PLDHashEntryHdr* OrInsert(F&& aInitEntry) {
      if (!HasEntry()) {
        Insert(std::forward<F>(aInitEntry));
      }
      return Entry();
    }

    /** Removes the entry. Note that the table won't shrink on destruction of
     * the EntryHandle.
     *
     * \pre HasEntry()
     * \post !HasEntry()
     */
    void Remove();

    /** Removes the entry, if it exists. Note that the table won't shrink on
     * destruction of the EntryHandle.
     *
     * \post !HasEntry()
     */
    void OrRemove();

   private:
    friend class PLDHashTable;

    EntryHandle(PLDHashTable* aTable, PLDHashNumber aKeyHash, Slot aSlot);

    void OccupySlot();

    PLDHashTable* mTable;
    PLDHashNumber mKeyHash;
    Slot mSlot;
  };

  template <class F>
  auto WithEntryHandle(const void* aKey, F&& aFunc)
      -> std::invoke_result_t<F, EntryHandle&&> {
    return std::forward<F>(aFunc)(MakeEntryHandle(aKey));
  }

  template <class F>
  auto WithEntryHandle(const void* aKey, const mozilla::fallible_t& aFallible,
                       F&& aFunc)
      -> std::invoke_result_t<F, mozilla::Maybe<EntryHandle>&&> {
    return std::forward<F>(aFunc)(MakeEntryHandle(aKey, aFallible));
  }

  // This is an iterator for PLDHashtable. Assertions will detect some, but not
  // all, mid-iteration table modifications that might invalidate (e.g.
  // reallocate) the entry storage.
  //
  // Any element can be removed during iteration using Remove(). If any
  // elements are removed, the table may be resized once iteration ends.
  //
  // Example usage:
  //
  //   for (auto iter = table.Iter(); !iter.Done(); iter.Next()) {
  //     auto entry = static_cast<FooEntry*>(iter.Get());
  //     // ... do stuff with |entry| ...
  //     // ... possibly call iter.Remove() once ...
  //   }
  //
  // or:
  //
  //   for (PLDHashTable::Iterator iter(&table); !iter.Done(); iter.Next()) {
  //     auto entry = static_cast<FooEntry*>(iter.Get());
  //     // ... do stuff with |entry| ...
  //     // ... possibly call iter.Remove() once ...
  //   }
  //
  // The latter form is more verbose but is easier to work with when
  // making subclasses of Iterator.
  //
  class Iterator {
   public:
    explicit Iterator(PLDHashTable* aTable);
    Iterator() = delete;
    Iterator& operator=(const Iterator&) = delete;
    Iterator& operator=(const Iterator&&) = delete;

    struct EndIteratorTag {};
    Iterator(PLDHashTable* aTable, EndIteratorTag aTag);
    Iterator(Iterator&& aOther);
    ~Iterator();

    // Have we finished?
    bool Done() const { return mNexts == mNextsLimit; }

    // Get the current entry.
    PLDHashEntryHdr* Get() const {
      MOZ_ASSERT(!Done());
      MOZ_ASSERT(mCurrent.IsLive());
      return mCurrent.ToEntry();
    }

    // Advance to the next entry.
    void Next();

    // Remove the current entry. Must only be called once per entry, and Get()
    // must not be called on that entry afterwards.
    void Remove();

    bool operator==(const Iterator& aOther) const {
      MOZ_ASSERT(mTable == aOther.mTable);
      return mNexts == aOther.mNexts;
    }

    Iterator Clone() const { return {*this}; }

   protected:
    PLDHashTable* mTable;  // Main table pointer.

   private:
    Slot mCurrent;         // Pointer to the current entry.
    uint32_t mNexts;       // Number of Next() calls.
    uint32_t mNextsLimit;  // Next() call limit.

    bool mHaveRemoved;   // Have any elements been removed?
    uint8_t mEntrySize;  // Size of entries.

    bool IsOnNonLiveEntry() const;

    void MoveToNextLiveEntry();

    Iterator(const Iterator&);
  };

  Iterator Iter() { return Iterator(this); }

  // Use this if you need to initialize an Iterator in a const method. If you
  // use this case, you should not call Remove() on the iterator.
  Iterator ConstIter() const {
    return Iterator(const_cast<PLDHashTable*>(this));
  }

 private:
  // Compute the minimum capacity (and the Log2 of that capacity) for a table
  // containing |aLength| elements while respecting the following constraints:
  // - table must be at most 75% full;
  // - capacity must be a power of two;
  // - capacity cannot be too small.
  static constexpr std::tuple<uint32_t, uint32_t> BestCapacity(
      uint32_t aLength) {
    // Callers should ensure this is true.
    MOZ_ASSERT(aLength <= PLDHashTable::kMaxInitialLength);

    // Compute the smallest capacity allowing |aLength| elements to be inserted
    // without rehashing.
    uint32_t capacity =
        (aLength * 4 + (3 - 1)) / 3;  // == ceil(aLength * 4 / 3)
    if (capacity < PLDHashTable::kMinCapacity) {
      capacity = PLDHashTable::kMinCapacity;
    }

    // Round up capacity to next power-of-two.
    uint32_t log2 = mozilla::CeilingLog2(capacity);
    capacity = 1u << log2;
    MOZ_ASSERT(capacity <= PLDHashTable::kMaxCapacity);

    return std::make_tuple(capacity, log2);
  }

  static constexpr bool SizeOfEntryStore(uint32_t aCapacity,
                                         uint32_t aEntrySize,
                                         uint32_t* aNbytes) {
    uint32_t slotSize = aEntrySize + sizeof(PLDHashNumber);
    return mozilla::SafeMul(aCapacity, slotSize, aNbytes);
  }

  static constexpr uint8_t HashShift(uint32_t aEntrySize, uint32_t aLength) {
    if (aLength > kMaxInitialLength) {
      MOZ_CRASH("Initial length is too large");
    }

    auto [capacity, log2] = BestCapacity(aLength);

    // Detect overflow.
    [[maybe_unused]] uint32_t nbytes;
    if (!SizeOfEntryStore(capacity, aEntrySize, &nbytes)) {
      MOZ_CRASH("Initial entry store size is too large");
    }

    // Compute the hashShift value.
    return static_cast<uint8_t>(kPLDHashNumberBits - log2);
  }

  static const PLDHashNumber kCollisionFlag = 1;

  PLDHashNumber Hash1(PLDHashNumber aHash0) const;
  void Hash2(PLDHashNumber aHash, uint32_t& aHash2Out,
             uint32_t& aSizeMaskOut) const;

  static bool MatchSlotKeyhash(Slot& aSlot, const PLDHashNumber aHash);
  Slot SlotForIndex(uint32_t aIndex) const;

  // We store mHashShift rather than sizeLog2 to optimize the collision-free
  // case in SearchTable.
  uint32_t CapacityFromHashShift() const {
    return ((uint32_t)1 << (kPLDHashNumberBits - mHashShift));
  }

  PLDHashNumber ComputeKeyHash(const void* aKey) const;

  enum SearchReason { ForSearchOrRemove, ForAdd };

  // Avoid using bare `Success` and `Failure`, as those names are commonly
  // defined as macros.
  template <SearchReason Reason, typename PLDSuccess, typename PLDFailure>
  auto SearchTable(const void* aKey, PLDHashNumber aKeyHash,
                   PLDSuccess&& aSucess, PLDFailure&& aFailure) const;

  Slot FindFreeSlot(PLDHashNumber aKeyHash) const;

  bool ChangeTable(int aDeltaLog2);

  void RawRemove(Slot& aSlot);
  void ShrinkIfAppropriate();

  mozilla::Maybe<EntryHandle> MakeEntryHandle(const void* aKey,
                                              const mozilla::fallible_t&);

  EntryHandle MakeEntryHandle(const void* aKey);
};

// Compute the hash code for a given key to be looked up, added, or removed.
// A hash code may have any PLDHashNumber value.
typedef PLDHashNumber (*PLDHashHashKey)(const void* aKey);

// Compare the key identifying aEntry with the provided key parameter. Return
// true if keys match, false otherwise.
typedef bool (*PLDHashMatchEntry)(const PLDHashEntryHdr* aEntry,
                                  const void* aKey);

// Copy the data starting at aFrom to the new entry storage at aTo. Do not add
// reference counts for any strong references in the entry, however, as this
// is a "move" operation: the old entry storage at from will be freed without
// any reference-decrementing callback shortly.
typedef void (*PLDHashMoveEntry)(PLDHashTable* aTable,
                                 const PLDHashEntryHdr* aFrom,
                                 PLDHashEntryHdr* aTo);

// Clear the entry and drop any strong references it holds. This callback is
// invoked by Remove(), but only if the given key is found in the table.
typedef void (*PLDHashClearEntry)(PLDHashTable* aTable,
                                  PLDHashEntryHdr* aEntry);

// Initialize a new entry. This function is called when
// Add() finds no existing entry for the given key, and must add a new one.
typedef void (*PLDHashInitEntry)(PLDHashEntryHdr* aEntry, const void* aKey);

// Finally, the "vtable" structure for PLDHashTable. The first four hooks
// must be provided by implementations; they're called unconditionally by the
// generic PLDHashTable.cpp code. Hooks after these may be null.
//
// Summary of allocation-related hook usage with C++ placement new emphasis:
//  initEntry           Call placement new using default key-based ctor.
//  moveEntry           Call placement new using copy ctor, run dtor on old
//                      entry storage.
//  clearEntry          Run dtor on entry.
//
// Note the reason why initEntry is optional: the default hooks (stubs) clear
// entry storage. On a successful Add(tbl, key), the returned entry pointer
// addresses an entry struct whose entry members are still clear (null). Add()
// callers can test such members to see whether the entry was newly created by
// the Add() call that just succeeded. If placement new or similar
// initialization is required, define an |initEntry| hook. Of course, the
// |clearEntry| hook must zero or null appropriately.
//
// XXX assumes 0 is null for pointer types.
struct PLDHashTableOps {
  // Mandatory hooks. All implementations must provide these.
  PLDHashHashKey hashKey;
  PLDHashMatchEntry matchEntry;
  PLDHashMoveEntry moveEntry;

  // Optional hooks start here. If null, these are not called.
  PLDHashClearEntry clearEntry;
  PLDHashInitEntry initEntry;
};

// A minimal entry is a subclass of PLDHashEntryHdr and has a void* key pointer.
struct PLDHashEntryStub : public PLDHashEntryHdr {
  const void* key;
};

#endif /* PLDHashTable_h */
