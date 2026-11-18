/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef XPCOM_DS_NSTHASHSET_H_
#define XPCOM_DS_NSTHASHSET_H_

#include "nsHashtablesFwd.h"
#include "nsTHashMap.h"  // for nsKeyClass

/**
 * Templated hash set. Don't use this directly, but use nsTHashSet instead
 * (defined as a type alias in nsHashtablesFwd.h).
 *
 * @param KeyClass a wrapper-class for the hashtable key, see nsHashKeys.h
 *   for a complete specification.
 */
template <class KeyClass>
class nsTBaseHashSet : protected nsTHashtable<KeyClass> {
  using Base = nsTHashtable<KeyClass>;
  typedef mozilla::fallible_t fallible_t;

 public:
  // XXX We have a similar situation here like with DataType/UserDataType in
  // nsBaseHashtable. It's less problematic here due to the more constrained
  // interface, but it may still be confusing. KeyType is not the stored key
  // type, but the one exposed to the user, i.e. as a parameter type, and as the
  // value type when iterating. It is currently impossible to move-insert a
  // RefPtr<T>, e.g., since the KeyType is T* in that case.
  using ValueType = typename KeyClass::KeyType;

  // KeyType is defined just for compatibility with nsTHashMap. For a set, the
  // key type is conceptually equivalent to the value type.
  using KeyType = typename KeyClass::KeyType;

  using Base::Base;

  // Query operations.

  using Base::Contains;
  using Base::GetGeneration;
  using Base::ShallowSizeOfExcludingThis;
  using Base::ShallowSizeOfIncludingThis;
  using Base::SizeOfExcludingThis;
  using Base::SizeOfIncludingThis;

  /**
   * Return the number of entries in the table.
   * @return    number of entries
   */
  [[nodiscard]] uint32_t Count() const { return Base::Count(); }

  /**
   * Return whether the table is empty.
   * @return    whether empty
   */
  [[nodiscard]] bool IsEmpty() const { return Base::IsEmpty(); }

  using iterator = ::detail::nsTHashtableKeyIterator<KeyClass>;
  using const_iterator = iterator;

  [[nodiscard]] auto begin() const { return Base::Keys().begin(); }

  [[nodiscard]] auto end() const { return Base::Keys().end(); }

  [[nodiscard]] auto cbegin() const { return Base::Keys().cbegin(); }

  [[nodiscard]] auto cend() const { return Base::Keys().cend(); }

  // Mutating operations.

  using Base::Clear;
  using Base::MarkImmutable;

  /**
   * Inserts a value into the set. Has no effect if the value is already in the
   * set. This overload is infallible and crashes if memory is exhausted.
   *
   * \note For strict consistency with nsTHashtable::EntryHandle method naming,
   * this should rather be called OrInsert, as it is legal to call it when the
   * value is already in the set. For simplicity, as we don't have two methods,
   * we still use "Insert" instead.
   */
  void Insert(ValueType aValue) { Base::PutEntry(aValue); }

  /**
   * Inserts a value into the set. Has no effect if the value is already in the
   * set. This overload is fallible and returns false if memory is exhausted.
   *
   * \note See note on infallible overload.
   */
  [[nodiscard]] bool Insert(ValueType aValue, const mozilla::fallible_t&) {
    return Base::PutEntry(aValue, mozilla::fallible);
  }

  /**
   * Inserts a value into the set. Has no effect if the value is already in the
   * set. This member function is infallible and crashes if memory is exhausted.
   *
   * \return true if the value was actually inserted, false if it was already in
   * the set.
   */
  bool EnsureInserted(ValueType aValue) { return Base::EnsureInserted(aValue); }

  using Base::Remove;

  /**
   * Removes a value from the set. Has no effect if the value is not in the set.
   *
   * \note For strict consistency with nsTHashtable::EntryHandle method naming,
   * this should rather be called OrRemove, as it is legal to call it when the
   * value is not in the set. For simplicity, as we don't have two methods,
   * we still use "Remove" instead.
   */
  void Remove(ValueType aValue) { Base::RemoveEntry(aValue); }

  using Base::EnsureRemoved;

  /**
   * Removes all elements matching a predicate.
   *
   * The predicate must be compatible with signature bool (ValueType).
   */
  template <typename Pred>
  void RemoveIf(Pred&& aPred) {
    for (auto it = cbegin(), end = cend(); it != end; ++it) {
      if (aPred(static_cast<ValueType>(*it))) {
        Remove(it);
      }
    }
  }
};

template <typename KeyClass>
size_t RangeSizeEstimate(const nsTBaseHashSet<KeyClass>& aRange) {
  return aRange.Count();
}

class nsCycleCollectionTraversalCallback;

template <class KeyClass>
inline void ImplCycleCollectionUnlink(nsTBaseHashSet<KeyClass>& aField) {
  aField.Clear();
}

template <class KeyClass>
inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCallback,
    const nsTBaseHashSet<KeyClass>& aField, const char* aName,
    uint32_t aFlags = 0) {
  for (const auto& entry : aField) {
    CycleCollectionNoteChild(aCallback, mozilla::detail::GetRawPtr(entry),
                             aName, aFlags);
  }
}

namespace mozilla {
template <typename SetT>
class nsTSetInserter {
  SetT* mSet;

  class Proxy {
    SetT& mSet;

   public:
    explicit Proxy(SetT& aSet) : mSet{aSet} {}

    template <typename E2>
    void operator=(E2&& aValue) {
      mSet.Insert(std::forward<E2>(aValue));
    }
  };

 public:
  using iterator_category = std::output_iterator_tag;

  explicit nsTSetInserter(SetT& aSet) : mSet{&aSet} {}

  Proxy operator*() { return Proxy(*mSet); }

  nsTSetInserter& operator++() { return *this; }
  nsTSetInserter& operator++(int) { return *this; }
};
}  // namespace mozilla

template <typename E>
auto MakeInserter(nsTBaseHashSet<E>& aSet) {
  return mozilla::nsTSetInserter<nsTBaseHashSet<E>>{aSet};
}

#endif  // XPCOM_DS_NSTHASHSET_H_
