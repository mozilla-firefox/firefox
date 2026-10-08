/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ContainStyleScopeManager.h"

#include "mozilla/ComputedStyle.h"
#include "mozilla/ServoStyleSet.h"
#include "CounterStyleManager.h"
#include "nsCounterManager.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsIFrame.h"
#include "nsLayoutUtils.h"
#include "nsQuoteList.h"

namespace mozilla {

nsGenConNode* ContainStyleScope::GetPrecedingElementInGenConList(
    nsGenConList* aList) {
  auto IsAfter = [this](nsGenConNode* aNode) {
    return nsLayoutUtils::CompareTreePosition(
               mContent, aNode->mPseudoFrame->GetContent()) > 0;
  };
  return aList->BinarySearch(IsAfter);
}

void ContainStyleScope::RecalcAllCounters() {
  GetCounterManager().RecalcAll();
  for (auto* child : mChildren) {
    child->RecalcAllCounters();
  }
}

void ContainStyleScope::RecalcAllQuotes() {
  GetQuoteList().RecalcAll();
  for (auto* child : mChildren) {
    child->RecalcAllQuotes();
  }
}

ContainStyleScope& ContainStyleScopeManager::GetOrCreateScopeForContent(
    nsIContent* aContent) {
  for (; aContent; aContent = aContent->GetFlattenedTreeParent()) {
    auto* element = dom::Element::FromNode(*aContent);
    if (!element) {
      continue;
    }

    // Do not allow elements which have `display: contents` to create style
    // boundaries. See https://github.com/w3c/csswg-drafts/issues/7392.
    if (element->IsDisplayContents()) {
      continue;
    }

    const auto* style = Servo_Element_GetMaybeOutOfDateStyle(element);
    if (!style) {
      continue;
    }

    if (!style->SelfOrAncestorHasContainStyle()) {
      return GetRootScope();
    }

    if (!style->StyleDisplay()->IsContainStyle()) {
      continue;
    }

    if (auto* scope = mScopes.Get(aContent)) {
      return *scope;
    }

    auto& parentScope =
        GetOrCreateScopeForContent(aContent->GetFlattenedTreeParent());
    return *mScopes.InsertOrUpdate(
        aContent, MakeUnique<ContainStyleScope>(this, &parentScope, aContent));
  }

  return GetRootScope();
}

ContainStyleScope& ContainStyleScopeManager::GetScopeForContent(
    nsIContent* aContent) {
  MOZ_ASSERT(aContent);

  if (auto* element = dom::Element::FromNode(*aContent)) {
    if (const auto* style = Servo_Element_GetMaybeOutOfDateStyle(element)) {
      if (!style->SelfOrAncestorHasContainStyle()) {
        return GetRootScope();
      }
    }
  }

  for (; aContent; aContent = aContent->GetFlattenedTreeParent()) {
    if (auto* scope = mScopes.Get(aContent)) {
      return *scope;
    }
  }

  return GetRootScope();
}

void ContainStyleScopeManager::Clear() {
  GetRootScope().GetQuoteList().Clear();
  GetRootScope().GetCounterManager().Clear();

  DestroyScope(&GetRootScope());
  MOZ_DIAGNOSTIC_ASSERT(mScopes.IsEmpty(),
                        "Destroying the root scope should destroy all scopes.");
}

void ContainStyleScopeManager::DestroyScopesFor(nsIFrame* aFrame) {
  if (auto* scope = mScopes.Get(aFrame->GetContent())) {
    DestroyScope(scope);
  }
}

void ContainStyleScopeManager::DestroyScope(ContainStyleScope* aScope) {
  // Deleting a scope modifies the array of children in its parent, so we don't
  // use an iterator here.
  while (!aScope->GetChildren().IsEmpty()) {
    DestroyScope(aScope->GetChildren().ElementAt(0));
  }
  mScopes.Remove(aScope->GetContent());
}

bool ContainStyleScopeManager::DestroyCounterNodesFor(nsIFrame* aFrame) {
  bool result = false;
  for (auto* scope = &GetScopeForContent(aFrame->GetContent()); scope;
       scope = scope->GetParent()) {
    result |= scope->GetCounterManager().DestroyNodesFor(aFrame);
  }
  return result;
}

bool ContainStyleScopeManager::AddCounterChanges(nsIFrame* aNewFrame) {
  if (!nsCounterManager::HasCounterChanges(aNewFrame)) {
    MOZ_ASSERT(!aNewFrame->HasAnyStateBits(NS_FRAME_HAS_CSS_COUNTER_STYLE));
    return false;
  }
  return GetOrCreateScopeForContent(
             aNewFrame->GetContent()->GetFlattenedTreeParent())
      .GetCounterManager()
      .AddCounterChanges(aNewFrame);
}

nsCounterList* ContainStyleScopeManager::GetOrCreateCounterList(
    dom::Element& aElement, nsAtom* aCounterName) {
  return GetOrCreateScopeForContent(&aElement)
      .GetCounterManager()
      .GetOrCreateCounterList(aCounterName);
}

bool ContainStyleScopeManager::CounterDirty(nsAtom* aCounterName) {
  return mDirtyCounters.Contains(aCounterName);
}

void ContainStyleScopeManager::SetCounterDirty(nsAtom* aCounterName) {
  mDirtyCounters.Insert(aCounterName);
}

void ContainStyleScopeManager::RecalcAllCounters() {
  GetRootScope().RecalcAllCounters();
  mDirtyCounters.Clear();
}

#if defined(DEBUG) || defined(MOZ_LAYOUT_DEBUGGER)
void ContainStyleScopeManager::DumpCounters() {
  GetRootScope().GetCounterManager().Dump();
  for (auto& entry : mScopes) {
    entry.GetWeak()->GetCounterManager().Dump();
  }
}
#endif

#ifdef ACCESSIBILITY
static bool GetFirstCounterValueForScopeAndFrame(ContainStyleScope* aScope,
                                                 nsIFrame* aFrame,
                                                 CounterValue& aOrdinal) {
  if (aScope->GetCounterManager().GetFirstCounterValueForFrame(aFrame,
                                                               aOrdinal)) {
    return true;
  }
  for (auto* child : aScope->GetChildren()) {
    if (GetFirstCounterValueForScopeAndFrame(child, aFrame, aOrdinal)) {
      return true;
    }
  }

  return false;
}

void ContainStyleScopeManager::GetSpokenCounterText(nsIFrame* aFrame,
                                                    nsAString& aText) {
  CounterValue ordinal = 1;
  GetFirstCounterValueForScopeAndFrame(&GetRootScope(), aFrame, ordinal);

  CounterStyle* counterStyle =
      aFrame->PresContext()->CounterStyleManager()->ResolveCounterStyle(
          aFrame->StyleList()->mCounterStyle);
  nsAutoString text;
  bool isBullet;
  counterStyle->GetSpokenCounterText(ordinal, aFrame->GetWritingMode(), text,
                                     isBullet);
  if (isBullet) {
    aText = text;
    if (!counterStyle->IsNone()) {
      aText.Append(' ');
    }
  } else {
    counterStyle->GetPrefix(aText);
    aText += text;
    nsAutoString suffix;
    counterStyle->GetSuffix(suffix);
    aText += suffix;
  }
}
#endif

void ContainStyleScopeManager::SetAllCountersDirty() {
  GetRootScope().GetCounterManager().SetAllDirty();
  for (auto& entry : mScopes) {
    entry.GetWeak()->GetCounterManager().SetAllDirty();
  }
}

bool ContainStyleScopeManager::DestroyQuoteNodesFor(nsIFrame* aFrame) {
  bool result = false;
  for (auto* scope = &GetScopeForContent(aFrame->GetContent()); scope;
       scope = scope->GetParent()) {
    result |= scope->GetQuoteList().DestroyNodesFor(aFrame);
  }
  return result;
}

nsQuoteList* ContainStyleScopeManager::QuoteListFor(dom::Element& aElement) {
  return &GetOrCreateScopeForContent(&aElement).GetQuoteList();
}

void ContainStyleScopeManager::RecalcAllQuotes() {
  GetRootScope().RecalcAllQuotes();
}

}  // namespace mozilla
