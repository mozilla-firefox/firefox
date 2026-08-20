/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsTreeStyleCache_h_
#define nsTreeStyleCache_h_

#include "mozilla/AtomArray.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/UniquePtr.h"
#include "nsCOMArray.h"
#include "nsRefPtrHashtable.h"
#include "nsTHashMap.h"

class nsIContent;

namespace mozilla {
enum class PseudoStyleType : uint8_t;
}

class nsTreeStyleCache {
 public:
  nsTreeStyleCache() : mNextState(0) {}

  ~nsTreeStyleCache() { Clear(); }

  void Clear() {
    mTransitionTable = nullptr;
    mCache = nullptr;
    mNextState = 0;
  }

  mozilla::ComputedStyle* GetComputedStyle(
      nsPresContext* aPresContext, nsIContent* aContent,
      mozilla::ComputedStyle* aStyle, mozilla::PseudoStyleType aPseudoType,
      const mozilla::AtomArray& aInputWord);

 protected:
  typedef uint32_t DFAState;

  class Transition final {
   public:
    Transition(DFAState aState, nsAtom* aSymbol);
    bool operator==(const Transition&) const = default;
    uint32_t Hash() const;

   private:
    DFAState mState;
    RefPtr<nsAtom> mInputSymbol;
  };

  typedef nsTHashMap<nsGenericHashKey<Transition>, DFAState> TransitionTable;

  // A transition table for a deterministic finite automaton.  The DFA
  // takes as its input a single pseudoelement and an ordered set of properties.
  // It transitions on an input word that is the concatenation of the
  // pseudoelement supplied with the properties in the array.
  //
  // It transitions from state to state by looking up entries in the transition
  // table (which is a mapping from (S,i)->S', where S is the current state, i
  // is the next property in the input word, and S' is the state to transition
  // to.
  //
  // If S' is not found, it is constructed and entered into the hashtable
  // under the key (S,i).
  //
  // Once the entire word has been consumed, the final state is used
  // to reference the cache table to locate the ComputedStyle.
  mozilla::UniquePtr<TransitionTable> mTransitionTable;

  // The cache of all active ComputedStyles.  This is a hash from
  // a final state in the DFA, Sf, to the resultant ComputedStyle.
  typedef nsRefPtrHashtable<nsUint32HashKey, mozilla::ComputedStyle>
      ComputedStyleCache;
  mozilla::UniquePtr<ComputedStyleCache> mCache;

  // An integer counter that is used when we need to make new states in the
  // DFA.
  DFAState mNextState;
};

#endif  // nsTreeStyleCache_h_
