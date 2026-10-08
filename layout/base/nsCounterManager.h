/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
// vim:cindent:ai:sw=4:ts=4:et:
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* implementation of CSS counters (for numbering things) */

#ifndef nsCounterManager_h_
#define nsCounterManager_h_

#include "mozilla/Attributes.h"
#include "nsGenConList.h"
#include "nsClassHashtable.h"
#include "mozilla/Likely.h"
#include "CounterStyleManager.h"

class nsCounterList;
struct nsCounterUseNode;
struct nsCounterChangeNode;

namespace mozilla {

class ContainStyleScope;

}  // namespace mozilla

struct nsCounterNode : public nsGenConNode {
  enum Type {
    RESET,      // a "counter number" pair in 'counter-reset'
    INCREMENT,  // a "counter number" pair in 'counter-increment'
    SET,        // a "counter number" pair in 'counter-set'
    USE         // counter() or counters() in 'content'
  };

  Type mType;

  // Counter value after this node
  int32_t mValueAfter = 0;

  // mScopeStart points to the node (usually a RESET, but not in the
  // case of an implied 'counter-reset') that created the scope for
  // this element (for a RESET, its outer scope, i.e., the one it is
  // inside rather than the one it creates).

  // May be null for all types, but only when mScopePrev is also null.
  // Being null for a non-RESET means that it is an implied
  // 'counter-reset'.  Being null for a RESET means it has no outer
  // scope.
  nsCounterNode* mScopeStart = nullptr;

  // mScopePrev points to the previous node that is in the same scope,
  // or for a RESET, the previous node in the scope outside of the
  // reset.

  // May be null for all types, but only when mScopeStart is also
  // null.  Following the mScopePrev links will eventually lead to
  // mScopeStart.  Being null for a non-RESET means that it is an
  // implied 'counter-reset'.  Being null for a RESET means it has no
  // outer scope.
  nsCounterNode* mScopePrev = nullptr;

  // Whether or not this node's scope crosses `contain: style` boundaries.
  // This can happen for USE nodes that come before any other types of
  // nodes in a `contain: style` boundary's list.
  bool mCrossesContainStyleBoundaries = false;

  inline nsCounterUseNode* UseNode();
  inline nsCounterChangeNode* ChangeNode();

  // For RESET, INCREMENT and SET nodes, aPseudoFrame need not be a
  // pseudo-element, and aContentIndex represents the index within the
  // 'counter-reset', 'counter-increment' or 'counter-set'  property
  // instead of within the 'content' property but offset to ensure
  // that (reset, increment, set, use) sort in that order.
  // It is zero for legacy bullet USE counter nodes.
  // (This slight weirdness allows sharing a lot of code with 'quotes'.)
  nsCounterNode(int32_t aContentIndex, Type aType)
      : nsGenConNode(aContentIndex), mType(aType) {}

  // to avoid virtual function calls in the common case
  inline void Calc(nsCounterList* aList, bool aNotify);

  // Is this a RESET node for a content-based (i.e. without a start value)
  // reversed() counter?
  inline bool IsContentBasedReset();

  // Is this a RESET node for a reversed() counter?
  inline bool IsReversed();

  // Is this an INCREMENT node that needs to be initialized to -1 or 1
  // depending on if our scope is reversed() or not?
  inline bool IsUnitializedIncrementNode();
};

struct nsCounterUseNode : public nsCounterNode {
  mozilla::CounterStylePtr mCounterStyle;
  nsString mSeparator;

  // false for counter(), true for counters()
  bool mAllCounters = false;

  bool mForLegacyBullet = false;

  enum ForLegacyBullet { ForLegacyBullet };
  nsCounterUseNode(enum ForLegacyBullet, mozilla::CounterStylePtr aCounterStyle)
      : nsCounterNode(0, USE),
        mCounterStyle(std::move(aCounterStyle)),
        mForLegacyBullet(true) {}

  // args go directly to member variables here and of nsGenConNode
  nsCounterUseNode(mozilla::CounterStylePtr aCounterStyle, nsString aSeparator,
                   uint32_t aContentIndex, bool aAllCounters)
      : nsCounterNode(aContentIndex, USE),
        mCounterStyle(std::move(aCounterStyle)),
        mSeparator(std::move(aSeparator)),
        mAllCounters(aAllCounters) {
    NS_ASSERTION(aContentIndex <= INT32_MAX, "out of range");
  }

  bool InitTextFrame(nsGenConList* aList, nsIFrame* aPseudoFrame,
                     nsIFrame* aTextFrame) override;

  // assign the correct |mValueAfter| value to a node that has been inserted,
  // and update the value of the text node, notifying if `aNotify` is true.
  // Should be called immediately after calling |Insert|.
  void Calc(nsCounterList* aList, bool aNotify);

  // The text that should be displayed for this counter.
  void GetText(nsString& aResult);
  void GetText(mozilla::WritingMode aWM, mozilla::CounterStyle* aStyle,
               nsString& aResult);
};

struct nsCounterChangeNode : public nsCounterNode {
  // |aPseudoFrame| is not necessarily a pseudo-element's frame, but
  // since it is for every other subclass of nsGenConNode, we follow
  // the naming convention here.
  // |aPropIndex| is the index of the value within the list in the
  // 'counter-increment', 'counter-reset' or 'counter-set' property.
  nsCounterChangeNode(nsIFrame* aPseudoFrame, nsCounterNode::Type aChangeType,
                      int32_t aChangeValue, int32_t aPropIndex,
                      bool aIsReversed)
      : nsCounterNode(  // Fake a content index for resets, increments and sets
                        // that comes before all the real content, with
                        // the resets first, in order, and then the increments
                        // and then the sets.
            aPropIndex + (aChangeType == RESET ? (INT32_MIN)
                                               : (aChangeType == INCREMENT
                                                      ? ((INT32_MIN / 3) * 2)
                                                      : INT32_MIN / 3)),
            aChangeType),
        mChangeValue(aChangeValue),
        mIsReversed(aIsReversed),
        mSeenSetNode(false) {
    NS_ASSERTION(aPropIndex >= 0, "out of range");
    NS_ASSERTION(
        aChangeType == INCREMENT || aChangeType == SET || aChangeType == RESET,
        "bad type");
    mPseudoFrame = aPseudoFrame;
    CheckFrameAssertions();
  }

  // assign the correct |mValueAfter| value to a node that has been inserted
  // Should be called immediately after calling |Insert|.
  void Calc(nsCounterList* aList);

  // The numeric value of the INCREMENT, SET or RESET.
  // Note: numeric_limits<int32_t>::min() is used for content-based reversed()
  // RESET nodes, and temporarily on INCREMENT nodes to signal that it should be
  // initialized to -1 or 1 depending on if the scope is reversed() or not.
  int32_t mChangeValue;

  // True if the counter is reversed(). Only used on RESET nodes.
  bool mIsReversed : 1;
  // True if we've seen a SET node during the initialization of
  // an IsContentBasedReset() node; always false on other nodes.
  bool mSeenSetNode : 1;
};

inline nsCounterUseNode* nsCounterNode::UseNode() {
  NS_ASSERTION(mType == USE, "wrong type");
  return static_cast<nsCounterUseNode*>(this);
}

inline nsCounterChangeNode* nsCounterNode::ChangeNode() {
  MOZ_ASSERT(mType == INCREMENT || mType == SET || mType == RESET);
  return static_cast<nsCounterChangeNode*>(this);
}

inline void nsCounterNode::Calc(nsCounterList* aList, bool aNotify) {
  if (mType == USE)
    UseNode()->Calc(aList, aNotify);
  else
    ChangeNode()->Calc(aList);
}

inline bool nsCounterNode::IsContentBasedReset() {
  return mType == RESET &&
         ChangeNode()->mChangeValue == std::numeric_limits<int32_t>::min();
}

inline bool nsCounterNode::IsReversed() {
  return mType == RESET && ChangeNode()->mIsReversed;
}

inline bool nsCounterNode::IsUnitializedIncrementNode() {
  return mType == INCREMENT &&
         ChangeNode()->mChangeValue == std::numeric_limits<int32_t>::min();
}

class nsCounterList : public nsGenConList {
 public:
  nsCounterList(nsAtom* aCounterName, mozilla::ContainStyleScope* aScope)
      : nsGenConList(), mCounterName(aCounterName), mScope(aScope) {
    MOZ_ASSERT(aScope);
  }

  // Return the first node for aFrame on this list, or nullptr.
  nsCounterNode* GetFirstNodeFor(nsIFrame* aFrame) const {
    return static_cast<nsCounterNode*>(nsGenConList::GetFirstNodeFor(aFrame));
  }

  void Insert(nsCounterNode* aNode) {
    nsGenConList::Insert(aNode);
    // Don't SetScope if we're dirty -- we'll reset all the scopes anyway,
    // and we can't usefully compute scopes right now.
    if (MOZ_LIKELY(!IsDirty())) {
      SetScope(aNode);
    }
  }

  nsCounterNode* First() {
    return static_cast<nsCounterNode*>(mList.getFirst());
  }

  static nsCounterNode* Next(nsCounterNode* aNode) {
    return static_cast<nsCounterNode*>(nsGenConList::Next(aNode));
  }
  static nsCounterNode* Prev(nsCounterNode* aNode) {
    return static_cast<nsCounterNode*>(nsGenConList::Prev(aNode));
  }

  static int32_t ValueBefore(nsCounterNode* aNode) {
    if (!aNode->mScopePrev) {
      return 0;
    }

    if (aNode->mType != nsCounterNode::USE &&
        aNode->mScopePrev->mCrossesContainStyleBoundaries) {
      return 0;
    }

    return aNode->mScopePrev->mValueAfter;
  }

  // Correctly set |aNode->mScopeStart| and |aNode->mScopePrev|
  void SetScope(nsCounterNode* aNode);

  // Recalculate |mScopeStart|, |mScopePrev|, and |mValueAfter| for
  // all nodes and update text in text content nodes.
  void RecalcAll();

  bool IsDirty() const;
  void SetDirty();
  bool IsRecalculatingAll() const { return mRecalculatingAll; }

 private:
  bool SetScopeByWalkingBackwardThroughList(
      nsCounterNode* aNodeToSetScopeFor, const nsIContent* aNodeContent,
      nsCounterNode* aNodeToBeginLookingAt);

  RefPtr<nsAtom> mCounterName;
  mozilla::ContainStyleScope* mScope;
  bool mRecalculatingAll = false;
};

/**
 * The counter manager maintains an |nsCounterList| for each named
 * counter to keep track of all scopes with that name.
 */
class nsCounterManager {
 public:
  explicit nsCounterManager(mozilla::ContainStyleScope* scope)
      : mScope(scope) {}

  // Whether aFrame resets, increments or sets any counter.
  static bool HasCounterChanges(const nsIFrame* aFrame);

  // Returns true if dirty
  bool AddCounterChanges(nsIFrame* aFrame);

  // Gets the appropriate counter list, creating it if necessary.
  // Guaranteed to return non-null. (Uses an infallible hashtable API.)
  nsCounterList* GetOrCreateCounterList(nsAtom* aCounterName);

  // Gets the appropriate counter list, returning null if it doesn't exist.
  nsCounterList* GetCounterList(nsAtom* aCounterName);

  // Clean up data in any dirty counter lists.
  void RecalcAll();

  // Set all counter lists dirty
  void SetAllDirty();

  // Destroy nodes for the frame in any lists, and return whether any
  // nodes were destroyed.
  bool DestroyNodesFor(nsIFrame* aFrame);

  // Clear all data.
  void Clear() { mNames.Clear(); }

#ifdef ACCESSIBILITY
  // Set |aOrdinal| to the first used counter value for the given frame and
  // return true. If no USE node for the given frame can be found, return false
  // and do not change the value of |aOrdinal|.
  bool GetFirstCounterValueForFrame(nsIFrame* aFrame,
                                    mozilla::CounterValue& aOrdinal) const;
#endif

#if defined(DEBUG) || defined(MOZ_LAYOUT_DEBUGGER)
  void Dump() const;
#endif

  static int32_t IncrementCounter(int32_t aOldValue, int32_t aIncrement) {
    // Addition of unsigned values is defined to be arithmetic
    // modulo 2^bits (C++ 2011, 3.9.1 [basic.fundamental], clause 4);
    // addition of signed values is undefined (and clang does
    // something very strange if we use it here).  Likewise integral
    // conversion from signed to unsigned is also defined as modulo
    // 2^bits (C++ 2011, 4.7 [conv.integral], clause 2); conversion
    // from unsigned to signed is however undefined (ibid., clause 3),
    // but to do what we want we must nonetheless depend on that
    // small piece of undefined behavior.
    int32_t newValue = int32_t(uint32_t(aOldValue) + uint32_t(aIncrement));
    // The CSS Working Group resolved that a counter-increment that
    // exceeds internal limits should not increment at all.
    // http://lists.w3.org/Archives/Public/www-style/2013Feb/0392.html
    // (This means, for example, that if aIncrement is 5, the
    // counter will get stuck at the largest multiple of 5 less than
    // the maximum 32-bit integer.)
    if ((aIncrement > 0) != (newValue > aOldValue)) {
      newValue = aOldValue;
    }
    return newValue;
  }

 private:
  mozilla::ContainStyleScope* mScope;
  nsClassHashtable<nsRefPtrHashKey<nsAtom>, nsCounterList> mNames;
};

#endif /* nsCounterManager_h_ */
