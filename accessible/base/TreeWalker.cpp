/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TreeWalker.h"

#include "ARIAMap.h"
#include "nsAccessibilityService.h"
#include "DocAccessible.h"

#include "mozilla/dom/ChildIterator.h"
#include "mozilla/dom/Element.h"

namespace mozilla::a11y {

////////////////////////////////////////////////////////////////////////////////
// TreeWalker
////////////////////////////////////////////////////////////////////////////////

TreeWalker::TreeWalker(LocalAccessible* aContext)
    : mDoc(aContext->Document()),
      mContext(aContext),
      mAnchorNode(nullptr),
      mARIAOwnsIdx(0),
      mChildFilter(nsIContent::eSkipPlaceholderContent),
      mFlags(0),
      mPhase(eAtStart) {
  mChildFilter |= nsIContent::eAllChildren;

  mAnchorNode = mContext->IsDoc() ? mDoc->DocumentNode()->GetRootElement()
                                  : mContext->GetContent();

  MOZ_COUNT_CTOR(TreeWalker);
}

TreeWalker::TreeWalker(LocalAccessible* aContext, nsIContent* aAnchorNode,
                       uint32_t aFlags)
    : mDoc(aContext->Document()),
      mContext(aContext),
      mAnchorNode(aAnchorNode),
      mARIAOwnsIdx(0),
      mChildFilter(nsIContent::eSkipPlaceholderContent),
      mFlags(aFlags),
      mPhase(eAtStart) {
  MOZ_ASSERT(mFlags & eWalkCache,
             "This constructor cannot be used for tree creation");
  MOZ_ASSERT(aAnchorNode, "No anchor node for the accessible tree walker");

  mChildFilter |= nsIContent::eAllChildren;

  MOZ_COUNT_CTOR(TreeWalker);
}

TreeWalker::TreeWalker(DocAccessible* aDocument, nsIContent* aAnchorNode)
    : mDoc(aDocument),
      mContext(nullptr),
      mAnchorNode(aAnchorNode),
      mARIAOwnsIdx(0),
      mChildFilter(nsIContent::eSkipPlaceholderContent |
                   nsIContent::eAllChildren),
      mFlags(eWalkCache),
      mPhase(eAtStart) {
  MOZ_ASSERT(aAnchorNode, "No anchor node for the accessible tree walker");
  MOZ_COUNT_CTOR(TreeWalker);
}

TreeWalker::~TreeWalker() { MOZ_COUNT_DTOR(TreeWalker); }

LocalAccessible* TreeWalker::Scope(nsIContent* aAnchorNode) {
  Reset();

  mAnchorNode = aAnchorNode;

  mFlags |= eScoped;

  bool skipSubtree = false;
  LocalAccessible* acc = AccessibleFor(aAnchorNode, 0, &skipSubtree);
  if (acc) {
    mPhase = eAtEnd;
    return acc;
  }

  return skipSubtree ? nullptr : Next();
}

bool TreeWalker::Seek(nsIContent* aChildNode) {
  MOZ_ASSERT(aChildNode, "Child cannot be null");

  Reset();

  if (mAnchorNode == aChildNode) {
    return true;
  }

  nsIContent* childNode = nullptr;
  nsINode* parentNode = aChildNode;
  do {
    childNode = parentNode->AsContent();
    parentNode = childNode->GetFlattenedTreeParent();

    // Handle the special case of XBL binding child under a shadow root.
    if (parentNode && parentNode->IsShadowRoot()) {
      parentNode = childNode->GetFlattenedTreeParent();
      if (parentNode == mAnchorNode) {
        return true;
      }
      continue;
    }

    if (!parentNode || !parentNode->IsElement()) {
      return false;
    }

    // If ARIA owned child.
    LocalAccessible* child = mDoc->GetAccessible(childNode);
    if (child && child->IsRelocated()) {
      MOZ_ASSERT(
          !(mFlags & eScoped),
          "Walker should not be scoped when seeking into relocated children");
      if (child->LocalParent() != mContext) {
        return false;
      }

      LocalAccessible* ownedChild = nullptr;
      while ((ownedChild = mDoc->ARIAOwnedAt(mContext, mARIAOwnsIdx++)) &&
             ownedChild != child) {
        ;
      }

      MOZ_ASSERT(ownedChild, "A child has to be in ARIA owned elements");
      mPhase = eAtARIAOwns;
      return true;
    }

    // Look in DOM.
    dom::AllChildrenIterator* iter =
        PrependState(parentNode->AsElement(), true);
    if (!iter->Seek(childNode)) {
      return false;
    }

    if (parentNode == mAnchorNode) {
      mPhase = eAtDOM;
      return true;
    }
  } while (true);

  MOZ_ASSERT_UNREACHABLE("because the do-while loop never breaks");
}

LocalAccessible* TreeWalker::Next() {
  if (mStateStack.IsEmpty()) {
    if (mPhase == eAtEnd) {
      return nullptr;
    }

    if (mPhase == eAtDOM || mPhase == eAtARIAOwns) {
      if (!(mFlags & eScoped)) {
        mPhase = eAtARIAOwns;
        LocalAccessible* child = mDoc->ARIAOwnedAt(mContext, mARIAOwnsIdx);
        if (child) {
          mARIAOwnsIdx++;
          return child;
        }
      }
      MOZ_ASSERT(!(mFlags & eScoped) || mPhase != eAtARIAOwns,
                 "Don't walk relocated children in scoped mode");
      mPhase = eAtEnd;
      return nullptr;
    }

    if (!mAnchorNode) {
      mPhase = eAtEnd;
      return nullptr;
    }

    mPhase = eAtDOM;
    PushState(mAnchorNode, true);
  }

  dom::AllChildrenIterator* top = &mStateStack[mStateStack.Length() - 1];
  while (top) {
    while (nsIContent* childNode = top->GetNextChild()) {
      bool skipSubtree = false;
      LocalAccessible* child = AccessibleFor(childNode, mFlags, &skipSubtree);
      if (child) {
        return child;
      }

      // Walk down the subtree if allowed.
      if (!skipSubtree && childNode->IsElement()) {
        top = PushState(childNode, true);
      }
    }
    top = PopState();
  }

  // If we traversed the whole subtree of the anchor node. Move to next node
  // relative anchor node within the context subtree if asked.
  if (mFlags != eWalkContextTree) {
    // eWalkCache flag presence indicates that the search is scoped to the
    // anchor (no ARIA owns stuff).
    if (mFlags & eWalkCache) {
      mPhase = eAtEnd;
      return nullptr;
    }
    return Next();
  }

  nsINode* contextNode = mContext->GetNode();
  while (mAnchorNode != contextNode) {
    nsINode* parentNode = mAnchorNode->GetFlattenedTreeParent();
    if (!parentNode || !parentNode->IsElement()) return nullptr;

    nsIContent* parent = parentNode->AsElement();
    top = PushState(parent, true);
    if (top->Seek(mAnchorNode)) {
      mAnchorNode = parent;
      return Next();
    }

    // XXX We really should never get here, it means we're trying to find an
    // accessible for a dom node where iterating over its parent's children
    // doesn't return it. However this sometimes happens when we're asked for
    // the nearest accessible to place holder content which we ignore.
    mAnchorNode = parent;
  }

  return Next();
}

LocalAccessible* TreeWalker::Prev() {
  if (mStateStack.IsEmpty()) {
    if (mPhase == eAtStart || mPhase == eAtDOM) {
      mPhase = eAtStart;
      return nullptr;
    }

    if (mPhase == eAtEnd) {
      if (mFlags & eScoped) {
        mPhase = eAtDOM;
      } else {
        mPhase = eAtARIAOwns;
        mARIAOwnsIdx = mDoc->ARIAOwnedCount(mContext);
      }
    }

    if (mPhase == eAtARIAOwns) {
      MOZ_ASSERT(!(mFlags & eScoped),
                 "Should not walk relocated children in scoped mode");
      if (mARIAOwnsIdx > 0) {
        return mDoc->ARIAOwnedAt(mContext, --mARIAOwnsIdx);
      }

      if (!mAnchorNode) {
        mPhase = eAtStart;
        return nullptr;
      }

      mPhase = eAtDOM;
      PushState(mAnchorNode, false);
    }
  }

  dom::AllChildrenIterator* top = &mStateStack[mStateStack.Length() - 1];
  while (top) {
    while (nsIContent* childNode = top->GetPreviousChild()) {
      // No accessible creation on the way back.
      bool skipSubtree = false;
      LocalAccessible* child =
          AccessibleFor(childNode, eWalkCache, &skipSubtree);
      if (child) {
        return child;
      }

      // Walk down into subtree to find accessibles.
      if (!skipSubtree && childNode->IsElement()) {
        top = PushState(childNode, false);
      }
    }
    top = PopState();
  }

  // Move to a previous node relative the anchor node within the context
  // subtree if asked.
  if (mFlags != eWalkContextTree) {
    mPhase = eAtStart;
    return nullptr;
  }

  nsINode* contextNode = mContext->GetNode();
  while (mAnchorNode != contextNode) {
    nsINode* parentNode = mAnchorNode->GetFlattenedTreeParent();
    if (!parentNode || !parentNode->IsElement()) {
      return nullptr;
    }

    nsIContent* parent = parentNode->AsElement();
    top = PushState(parent, true);
    if (top->Seek(mAnchorNode)) {
      mAnchorNode = parent;
      return Prev();
    }

    mAnchorNode = parent;
  }

  mPhase = eAtStart;
  return nullptr;
}

LocalAccessible* TreeWalker::AccessibleFor(nsIContent* aNode, uint32_t aFlags,
                                           bool* aSkipSubtree) {
  // Ignore the accessible and its subtree if it was repositioned by means
  // of aria-owns.
  LocalAccessible* child = mDoc->GetAccessible(aNode);
  if (child) {
    if (child->IsRelocated()) {
      *aSkipSubtree = true;
      return nullptr;
    }
    return child;
  }

  // Create an accessible if allowed.
  if (!(aFlags & eWalkCache) && mContext->IsAcceptableChild(aNode) &&
      !aria::IsValidARIAHidden(mDoc)) {
    mDoc->RelocateARIAOwnedIfNeeded(aNode);
    return GetAccService()->CreateAccessible(aNode, mContext, aSkipSubtree);
  }

  return nullptr;
}

dom::AllChildrenIterator* TreeWalker::PopState() {
  mStateStack.RemoveLastElement();
  return mStateStack.IsEmpty() ? nullptr : &mStateStack.LastElement();
}

}  // namespace mozilla::a11y
