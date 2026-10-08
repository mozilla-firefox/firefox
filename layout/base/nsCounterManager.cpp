/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* implementation of CSS counters (for numbering things) */

#include "nsCounterManager.h"

#include "mozilla/AutoRestore.h"
#include "mozilla/ContainStyleScopeManager.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/Likely.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/WritingModes.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Text.h"
#include "nsContainerFrame.h"
#include "nsContentUtils.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsIFrame.h"
#include "nsTArray.h"

using namespace mozilla;

bool nsCounterUseNode::InitTextFrame(nsGenConList* aList,
                                     nsIFrame* aPseudoFrame,
                                     nsIFrame* aTextFrame) {
  nsCounterNode::InitTextFrame(aList, aPseudoFrame, aTextFrame);

  auto* counterList = static_cast<nsCounterList*>(aList);
  counterList->Insert(this);
  aPseudoFrame->AddStateBits(NS_FRAME_HAS_CSS_COUNTER_STYLE);
  // If the list is already dirty, or the node is not at the end, just start
  // with an empty string for now and when we recalculate the list we'll change
  // the value to the right one.
  if (counterList->IsDirty()) {
    return false;
  }
  if (!counterList->IsLast(this)) {
    counterList->SetDirty();
    return true;
  }
  Calc(counterList, /* aNotify = */ false);
  return false;
}

// assign the correct |mValueAfter| value to a node that has been inserted
// Should be called immediately after calling |Insert|.
void nsCounterUseNode::Calc(nsCounterList* aList, bool aNotify) {
  NS_ASSERTION(aList->IsRecalculatingAll() || !aList->IsDirty(),
               "Why are we calculating with a dirty list?");

  mValueAfter = nsCounterList::ValueBefore(this);

  if (mText) {
    nsAutoString contentString;
    GetText(contentString);
    mText->SetText(contentString, aNotify);
  }
}

// assign the correct |mValueAfter| value to a node that has been inserted
// Should be called immediately after calling |Insert|.
void nsCounterChangeNode::Calc(nsCounterList* aList) {
  NS_ASSERTION(aList->IsRecalculatingAll() || !aList->IsDirty(),
               "Why are we calculating with a dirty list?");
  if (IsContentBasedReset()) {
    // RecalcAll takes care of this case.
  } else if (mType == RESET || mType == SET) {
    mValueAfter = mChangeValue;
  } else {
    NS_ASSERTION(mType == INCREMENT, "invalid type");
    mValueAfter = nsCounterManager::IncrementCounter(
        nsCounterList::ValueBefore(this), mChangeValue);
  }
}

void nsCounterUseNode::GetText(nsString& aResult) {
  CounterStyle* style =
      mPseudoFrame->PresContext()->CounterStyleManager()->ResolveCounterStyle(
          mCounterStyle);
  GetText(mPseudoFrame->GetWritingMode(), style, aResult);
}

void nsCounterUseNode::GetText(WritingMode aWM, CounterStyle* aStyle,
                               nsString& aResult) {
  const bool isBidiRTL = aWM.IsBidiRTL();
  auto AppendCounterText = [&aResult, isBidiRTL](const nsAutoString& aText,
                                                 bool aIsRTL) {
    if (MOZ_LIKELY(isBidiRTL == aIsRTL)) {
      aResult.Append(aText);
    } else {
      // RLM = 0x200f, LRM = 0x200e
      const char16_t mark = aIsRTL ? 0x200f : 0x200e;
      aResult.Append(mark);
      aResult.Append(aText);
      aResult.Append(mark);
    }
  };

  if (mForLegacyBullet) {
    nsAutoString prefix;
    aStyle->GetPrefix(prefix);
    aResult.Assign(prefix);
  }

  AutoTArray<nsCounterNode*, 8> stack;
  stack.AppendElement(static_cast<nsCounterNode*>(this));

  if (mAllCounters && mScopeStart) {
    for (nsCounterNode* n = mScopeStart; n->mScopePrev; n = n->mScopeStart) {
      stack.AppendElement(n->mScopePrev);
    }
  }

  for (nsCounterNode* n : Reversed(stack)) {
    nsAutoString text;
    bool isTextRTL;
    aStyle->GetCounterText(n->mValueAfter, aWM, text, isTextRTL);
    if (!mForLegacyBullet || aStyle->IsBullet()) {
      aResult.Append(text);
    } else {
      AppendCounterText(text, isTextRTL);
    }
    if (n == this) {
      break;
    }
    aResult.Append(mSeparator);
  }

  if (mForLegacyBullet) {
    nsAutoString suffix;
    aStyle->GetSuffix(suffix);
    aResult.Append(suffix);
  }
}

static const nsIContent* GetParentContentForScope(nsIFrame* frame) {
  // We do not want elements with `display: contents` to establish scope for
  // counters. We'd like to do something like
  // `nsIFrame::GetClosestFlattenedTreeAncestorPrimaryFrame()` above, but this
  // may be called before the primary frame is set on frames.
  nsIContent* content = frame->GetContent()->GetFlattenedTreeParent();
  while (content && content->IsElement() &&
         content->AsElement()->IsDisplayContents()) {
    content = content->GetFlattenedTreeParent();
  }

  return content;
}

bool nsCounterList::IsDirty() const {
  return mScope->GetScopeManager().CounterDirty(mCounterName);
}

void nsCounterList::SetDirty() {
  mScope->GetScopeManager().SetCounterDirty(mCounterName);
}

void nsCounterList::SetScope(nsCounterNode* aNode) {
  // This function is responsible for setting |mScopeStart| and
  // |mScopePrev| (whose purpose is described in nsCounterManager.h).
  // We do this by starting from the node immediately preceding
  // |aNode| in content tree order, which is reasonably likely to be
  // the previous element in our scope (or, for a reset, the previous
  // element in the containing scope, which is what we want).  If
  // we're not in the same scope that it is, then it's too deep in the
  // frame tree, so we walk up parent scopes until we find something
  // appropriate.

  auto setNullScopeFor = [](nsCounterNode* aNode) {
    aNode->mScopeStart = nullptr;
    aNode->mScopePrev = nullptr;
    aNode->mCrossesContainStyleBoundaries = false;
    if (aNode->IsUnitializedIncrementNode()) {
      aNode->ChangeNode()->mChangeValue = 1;
    }
  };

  if (aNode == First() && aNode->mType != nsCounterNode::USE) {
    setNullScopeFor(aNode);
    return;
  }

  auto didSetScopeFor = [this](nsCounterNode* aNode) {
    if (aNode->mType == nsCounterNode::USE) {
      return;
    }
    if (aNode->mScopeStart->IsContentBasedReset()) {
      SetDirty();
    }
    if (aNode->IsUnitializedIncrementNode()) {
      aNode->ChangeNode()->mChangeValue =
          aNode->mScopeStart->IsReversed() ? -1 : 1;
    }
  };

  // If there exist an explicit RESET scope created by an ancestor or
  // the element itself, then we use that scope.
  // Otherwise, fall through to consider scopes created by siblings (and
  // their descendants) in reverse document order.
  if (aNode->mType != nsCounterNode::USE &&
      StaticPrefs::layout_css_counter_ancestor_scope_enabled()) {
    for (auto* p = aNode->mPseudoFrame; p; p = p->GetParent()) {
      // This relies on the fact that a RESET node is always the first
      // CounterNode for a frame if it has any.
      auto* counter = GetFirstNodeFor(p);
      if (!counter || counter->mType != nsCounterNode::RESET) {
        continue;
      }
      if (p == aNode->mPseudoFrame) {
        break;
      }
      aNode->mScopeStart = counter;
      aNode->mScopePrev = counter;
      aNode->mCrossesContainStyleBoundaries = false;
      for (nsCounterNode* prev = Prev(aNode); prev; prev = prev->mScopePrev) {
        if (prev->mScopeStart == counter) {
          aNode->mScopePrev =
              prev->mType == nsCounterNode::RESET ? prev->mScopePrev : prev;
          break;
        }
        if (prev->mType != nsCounterNode::RESET) {
          prev = prev->mScopeStart;
          if (!prev) {
            break;
          }
        }
      }
      didSetScopeFor(aNode);
      return;
    }
  }

  // Get the content node for aNode's rendering object's *parent*,
  // since scope includes siblings, so we want a descendant check on
  // parents. Note here that mPseudoFrame is a bit of a misnomer, as it
  // might not be a pseudo element at all, but a normal element that
  // happens to increment a counter. We want to respect the flat tree
  // here, but skipping any <slot> element that happens to contain
  // mPseudoFrame. That's why this uses GetInFlowParent() instead
  // of GetFlattenedTreeParent().
  const nsIContent* nodeContent = GetParentContentForScope(aNode->mPseudoFrame);
  if (SetScopeByWalkingBackwardThroughList(aNode, nodeContent, Prev(aNode))) {
    aNode->mCrossesContainStyleBoundaries = false;
    didSetScopeFor(aNode);
    return;
  }

  // If this is a USE node there's a possibility that its counter scope starts
  // in a parent `contain: style` scope. Look upward in the `contain: style`
  // scope tree to find an appropriate node with which this node shares a
  // counter scope.
  if (aNode->mType == nsCounterNode::USE && aNode == First()) {
    for (auto* scope = mScope->GetParent(); scope; scope = scope->GetParent()) {
      if (auto* counterList =
              scope->GetCounterManager().GetCounterList(mCounterName)) {
        if (auto* node = static_cast<nsCounterNode*>(
                mScope->GetPrecedingElementInGenConList(counterList))) {
          if (SetScopeByWalkingBackwardThroughList(aNode, nodeContent, node)) {
            aNode->mCrossesContainStyleBoundaries = true;
            didSetScopeFor(aNode);
            return;
          }
        }
      }
    }
  }

  setNullScopeFor(aNode);
}

bool nsCounterList::SetScopeByWalkingBackwardThroughList(
    nsCounterNode* aNodeToSetScopeFor, const nsIContent* aNodeContent,
    nsCounterNode* aNodeToBeginLookingAt) {
  for (nsCounterNode *prev = aNodeToBeginLookingAt, *start; prev;
       prev = start->mScopePrev) {
    // There are two possibilities here:
    // 1. |prev| starts a new counter scope. This happens when:
    //  a. It's a reset node.
    //  b. It's an implied reset node which we know because mScopeStart is null.
    //  c. It follows one or more USE nodes at the start of the list which have
    //     a scope that starts in a parent `contain: style` context.
    //  In all of these cases, |prev| should be the start of this node's counter
    //  scope.
    // 2. |prev| does not start a new counter scope and this node should share a
    //   counter scope start with |prev|.
    start =
        (prev->mType == nsCounterNode::RESET || !prev->mScopeStart ||
         (prev->mScopePrev && prev->mScopePrev->mCrossesContainStyleBoundaries))
            ? prev
            : prev->mScopeStart;

    const nsIContent* startContent =
        GetParentContentForScope(start->mPseudoFrame);
    NS_ASSERTION(aNodeContent || !startContent,
                 "null check on startContent should be sufficient to "
                 "null check aNodeContent as well, since if aNodeContent "
                 "is for the root, startContent (which is before it) "
                 "must be too");

    // A reset's outer scope can't be a scope created by a sibling.
    if (!(aNodeToSetScopeFor->mType == nsCounterNode::RESET &&
          aNodeContent == startContent) &&
        // everything is inside the root (except the case above,
        // a second reset on the root)
        (!startContent ||
         aNodeContent->IsInclusiveFlatTreeDescendantOf(startContent))) {
      //  If this node is a USE node and the previous node was also a USE node
      //  which has a scope that starts in a parent `contain: style` context,
      //  this node's scope shares the same scope and crosses `contain: style`
      //  scope boundaries.
      if (aNodeToSetScopeFor->mType == nsCounterNode::USE) {
        aNodeToSetScopeFor->mCrossesContainStyleBoundaries =
            prev->mCrossesContainStyleBoundaries;
      }

      aNodeToSetScopeFor->mScopeStart = start;
      aNodeToSetScopeFor->mScopePrev = prev;
      return true;
    }
  }

  return false;
}

void nsCounterList::RecalcAll() {
  AutoRestore<bool> restoreRecalculatingAll(mRecalculatingAll);
  mRecalculatingAll = true;

  // Setup the scope and calculate the default start value for content-based
  // reversed() counters.  We need to track the last increment for each of
  // those scopes so that we can add it in an extra time at the end.
  // https://drafts.csswg.org/css-lists/#instantiating-counters
  nsTHashMap<nsPtrHashKey<nsCounterChangeNode>, int32_t> scopes;
  for (nsCounterNode* node = First(); node; node = Next(node)) {
    SetScope(node);
    if (node->IsContentBasedReset()) {
      node->ChangeNode()->mSeenSetNode = false;
      node->mValueAfter = 0;
      scopes.InsertOrUpdate(node->ChangeNode(), 0);
    } else if (node->mScopeStart && node->mScopeStart->IsContentBasedReset() &&
               !node->mScopeStart->ChangeNode()->mSeenSetNode) {
      if (node->mType == nsCounterChangeNode::INCREMENT) {
        auto incrementNegated = -node->ChangeNode()->mChangeValue;
        if (auto entry = scopes.Lookup(node->mScopeStart->ChangeNode())) {
          entry.Data() = incrementNegated;
        }
        auto* next = Next(node);
        if (next && next->mPseudoFrame == node->mPseudoFrame &&
            next->mType == nsCounterChangeNode::SET) {
          continue;
        }
        node->mScopeStart->mValueAfter += incrementNegated;
      } else if (node->mType == nsCounterChangeNode::SET) {
        node->mScopeStart->mValueAfter += node->ChangeNode()->mChangeValue;
        // We have a 'counter-set' for this scope so we're done.
        // The counter is incremented from that value for the remaining nodes.
        node->mScopeStart->ChangeNode()->mSeenSetNode = true;
      }
    }
  }

  // For all the content-based reversed() counters we found, add in the
  // incrementNegated from its last counter-increment.
  for (auto iter = scopes.ConstIter(); !iter.Done(); iter.Next()) {
    iter.Key()->mValueAfter += iter.Data();
  }

  for (nsCounterNode* node = First(); node; node = Next(node)) {
    node->Calc(this, /* aNotify = */ true);
  }
}

static bool AddCounterChangeNode(nsCounterManager& aManager, nsIFrame* aFrame,
                                 int32_t aIndex,
                                 const nsStyleContent::CounterPair& aPair,
                                 nsCounterNode::Type aType) {
  auto* node = new nsCounterChangeNode(aFrame, aType, aPair.value, aIndex,
                                       aPair.is_reversed);
  nsCounterList* counterList =
      aManager.GetOrCreateCounterList(aPair.name.AsAtom());
  counterList->Insert(node);
  if (!counterList->IsLast(node)) {
    // Tell the caller it's responsible for recalculating the entire list.
    counterList->SetDirty();
    return true;
  }

  // Don't call Calc() if the list is already dirty -- it'll be recalculated
  // anyway, and trying to calculate with a dirty list doesn't work.
  if (MOZ_LIKELY(!counterList->IsDirty())) {
    node->Calc(counterList);
  }
  return counterList->IsDirty();
}

static bool HasCounters(const nsStyleContent& aStyle) {
  return !aStyle.mCounterIncrement.IsEmpty() ||
         !aStyle.mCounterReset.IsEmpty() || !aStyle.mCounterSet.IsEmpty();
}

// For elements with 'display:list-item' we add a default
// 'counter-increment:list-item' unless 'counter-increment' already has a
// value for 'list-item'.
//
// https://drafts.csswg.org/css-lists-3/#declaring-a-list-item
//
// We inherit `display` for some anonymous boxes, but we don't want them to
// increment the list-item counter.
static bool RequiresListItemIncrement(const nsIFrame* aFrame) {
  return aFrame->StyleDisplay()->IsListItem() && !aFrame->Style()->IsAnonBox();
}

bool nsCounterManager::HasCounterChanges(const nsIFrame* aFrame) {
  return RequiresListItemIncrement(aFrame) ||
         HasCounters(*aFrame->StyleContent());
}

bool nsCounterManager::AddCounterChanges(nsIFrame* aFrame) {
  MOZ_ASSERT(HasCounterChanges(aFrame));

  const bool requiresListItemIncrement = RequiresListItemIncrement(aFrame);
  const nsStyleContent* styleContent = aFrame->StyleContent();

  aFrame->AddStateBits(NS_FRAME_HAS_CSS_COUNTER_STYLE);

  bool dirty = false;
  // Add in order, resets first, so all the comparisons will be optimized
  // for addition at the end of the list.
  {
    int32_t i = 0;
    for (const auto& pair : styleContent->mCounterReset.AsSpan()) {
      dirty |= AddCounterChangeNode(*this, aFrame, i++, pair,
                                    nsCounterChangeNode::RESET);
    }
  }
  bool hasListItemIncrement = false;
  {
    int32_t i = 0;
    for (const auto& pair : styleContent->mCounterIncrement.AsSpan()) {
      hasListItemIncrement |= pair.name.AsAtom() == nsGkAtoms::list_item;
      if (pair.value != 0) {
        dirty |= AddCounterChangeNode(*this, aFrame, i++, pair,
                                      nsCounterChangeNode::INCREMENT);
      }
    }
  }

  if (requiresListItemIncrement && !hasListItemIncrement) {
    RefPtr<nsAtom> atom = nsGkAtoms::list_item;
    // We use a magic value here to signal to SetScope() that it should
    // set the value to -1 or 1 depending on if the scope is reversed()
    // or not.
    auto listItemIncrement = nsStyleContent::CounterPair{
        {StyleAtom(atom.forget())}, std::numeric_limits<int32_t>::min()};
    dirty |= AddCounterChangeNode(
        *this, aFrame, styleContent->mCounterIncrement.Length(),
        listItemIncrement, nsCounterChangeNode::INCREMENT);
  }

  {
    int32_t i = 0;
    for (const auto& pair : styleContent->mCounterSet.AsSpan()) {
      dirty |= AddCounterChangeNode(*this, aFrame, i++, pair,
                                    nsCounterChangeNode::SET);
    }
  }
  return dirty;
}

nsCounterList* nsCounterManager::GetOrCreateCounterList(nsAtom* aCounterName) {
  MOZ_ASSERT(aCounterName);
  return mNames.GetOrInsertNew(aCounterName, aCounterName, mScope);
}

nsCounterList* nsCounterManager::GetCounterList(nsAtom* aCounterName) {
  MOZ_ASSERT(aCounterName);
  return mNames.Get(aCounterName);
}

void nsCounterManager::RecalcAll() {
  for (const auto& list : mNames.Values()) {
    if (list->IsDirty()) {
      list->RecalcAll();
    }
  }
}

void nsCounterManager::SetAllDirty() {
  for (const auto& list : mNames.Values()) {
    list->SetDirty();
  }
}

bool nsCounterManager::DestroyNodesFor(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame->HasAnyStateBits(NS_FRAME_HAS_CSS_COUNTER_STYLE),
             "why call me?");
  bool destroyedAny = false;
  for (const auto& list : mNames.Values()) {
    if (list->DestroyNodesFor(aFrame)) {
      destroyedAny = true;
      list->SetDirty();
    }
  }
  return destroyedAny;
}

#ifdef ACCESSIBILITY
bool nsCounterManager::GetFirstCounterValueForFrame(
    nsIFrame* aFrame, CounterValue& aOrdinal) const {
  if (const auto* list = mNames.Get(nsGkAtoms::list_item)) {
    for (nsCounterNode* n = list->GetFirstNodeFor(aFrame);
         n && n->mPseudoFrame == aFrame; n = list->Next(n)) {
      if (n->mType == nsCounterNode::USE) {
        aOrdinal = n->mValueAfter;
        return true;
      }
    }
  }

  return false;
}
#endif

#if defined(DEBUG) || defined(MOZ_LAYOUT_DEBUGGER)
void nsCounterManager::Dump() const {
  printf("\n\nCounter Manager Lists:\n");
  for (const auto& entry : mNames) {
    printf("Counter named \"%s\":\n", nsAtomCString(entry.GetKey()).get());

    nsCounterList* list = entry.GetWeak();
    int32_t i = 0;
    for (nsCounterNode* node = list->First(); node; node = list->Next(node)) {
      const char* types[] = {"RESET", "INCREMENT", "SET", "USE"};
      printf(
          "  Node #%d @%p frame=%p index=%d type=%s valAfter=%d\n"
          "       scope-start=%p scope-prev=%p",
          i++, (void*)node, (void*)node->mPseudoFrame, node->mContentIndex,
          types[node->mType], node->mValueAfter, (void*)node->mScopeStart,
          (void*)node->mScopePrev);
      if (node->mType == nsCounterNode::USE) {
        nsAutoString text;
        node->UseNode()->GetText(text);
        printf(" text=%s", NS_ConvertUTF16toUTF8(text).get());
      }
      printf("\n");
    }
  }
  printf("\n\n");
}
#endif
