/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_NodeOwnedObjectList_h
#define mozilla_dom_NodeOwnedObjectList_h

#include <concepts>

#include "MainThreadUtils.h"
#include "mozilla/DoublyLinkedList.h"

namespace mozilla::dom {

template <typename T>
concept NodeOwnedObjectListEntry =
    std::derived_from<T, DoublyLinkedListElement<T>> &&
    requires(const T& aEntry, T& aMutableEntry) {
      { aEntry.HasOwnerInCCGenerationDocument() } -> std::same_as<bool>;
      aMutableEntry.MarkForCC();
    };

// Main thread list of objects which nodes keep alive. An object owned by a node
// in a document marked uncollectable for the current CC generation is alive
// too, so unmarking its gray JS lets the cycle collector skip whatever is kept
// alive only through that JS.
template <NodeOwnedObjectListEntry T>
class NodeOwnedObjectList final {
 public:
  static void Add(T* aEntry) {
    MOZ_ASSERT(NS_IsMainThread());
    sEntries.pushBack(aEntry);
  }

  static void Remove(T* aEntry) {
    MOZ_ASSERT(NS_IsMainThread());
    // Destructors pass here entries which are not in the list.
    if (sEntries.ElementProbablyInList(aEntry)) {
      sEntries.remove(aEntry);
    }
  }

  static bool IsEmpty() { return sEntries.isEmpty(); }

  static void UnmarkGrayInCCGenerationDocuments() {
    for (T& entry : sEntries) {
      if (entry.HasOwnerInCCGenerationDocument()) {
        entry.MarkForCC();
      }
    }
  }

 private:
  static constinit inline DoublyLinkedList<T> sEntries;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_NodeOwnedObjectList_h
