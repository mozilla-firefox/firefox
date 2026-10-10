/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* base class for all rule types in a CSS style sheet */

#include "Rule.h"

#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/css/GroupRule.h"
#include "mozilla/dom/CSSImportRule.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentOrShadowRoot.h"
#include "nsCCUncollectableMarker.h"
#include "nsWrapperCacheInlines.h"

using namespace mozilla;
using namespace mozilla::dom;

namespace mozilla::css {

NS_IMPL_CYCLE_COLLECTING_ADDREF(Rule)
NS_IMPL_CYCLE_COLLECTING_RELEASE(Rule)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Rule)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_WEAK_PTR(Rule)

bool Rule::IsCCLeaf() const { return !PreservingWrapper(); }

bool Rule::IsKnownLive() const {
  if (HasKnownLiveWrapper()) {
    return true;
  }

  StyleSheet* sheet = GetStyleSheet();
  if (!sheet) {
    return false;
  }

  Document* doc = sheet->GetKeptAliveByDocument();
  return doc &&
         nsCCUncollectableMarker::InGeneration(doc->GetMarkedCCGeneration());
}

void Rule::UnlinkDeclarationWrapper(nsWrapperCache& aDecl) {
  // We have to be a bit careful here. We have two separate nsWrapperCache
  // instances, aDecl and this, that both correspond to the same CC participant:
  // this. If we just used ReleaseWrapper() on one of them, that would
  // unpreserve that one wrapper, then trace us with a tracer that clears JS
  // things, and we would clear the wrapper on the cache that has not
  // unpreserved the wrapper yet. That would violate the invariant that the
  // cache keeps caching the wrapper until the wrapper dies.
  //
  // So instead we use a special case version of ReleaseWrapper to unpreserve
  // both wrappers before doing any clearing.
  bool needDrop = PreservingWrapper() || aDecl.PreservingWrapper();
  ReleaseWrapperWithoutDrop();
  aDecl.ReleaseWrapperWithoutDrop();
  if (needDrop) {
    DropJSObjects(this);
  }
}

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(Rule)
  return tmp->IsCCLeaf() || tmp->IsKnownLive();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(Rule)
  // Please see documentation for nsCycleCollectionParticipant::CanSkip* for why
  // we need to check HasNothingToTrace here but not in the other two CanSkip
  // methods.
  return tmp->IsCCLeaf() || (tmp->IsKnownLive() && tmp->HasNothingToTrace(tmp));
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(Rule)
  return tmp->IsCCLeaf() || tmp->IsKnownLive();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

/* virtual */
void Rule::DropSheetReference() { mSheet = nullptr; }

void Rule::SetCssText(const nsACString& aCssText) {
  // We used to throw for some rule types, but not all.  Specifically, we did
  // not throw for StyleRule.  Let's just always not throw.
}

Rule* Rule::GetParentRule() const { return mParentRule; }

already_AddRefed<Rule> Rule::GetParentRuleForBindings() const {
  return do_AddRef(mParentRule);
}

#ifdef DEBUG
void Rule::AssertParentRuleType() {
  // Would be nice to check that this->Type() is StyleCssRuleType::Keyframe
  // when mParentRule->Tye() is StyleCssRuleType::Keyframes, but we can't call
  // this->Type() here since it's virtual.
  // Same for StyleCssRuleType::Margin and StyleCssRuleType::Page.
  if (mParentRule) {
    auto type = mParentRule->Type();
    MOZ_ASSERT(type == StyleCssRuleType::Media ||
               type == StyleCssRuleType::Style ||
               type == StyleCssRuleType::Document ||
               type == StyleCssRuleType::Supports ||
               type == StyleCssRuleType::Keyframes ||
               type == StyleCssRuleType::LayerBlock ||
               type == StyleCssRuleType::Container ||
               type == StyleCssRuleType::Scope ||
               type == StyleCssRuleType::StartingStyle ||
               type == StyleCssRuleType::AppearanceBase ||
               type == StyleCssRuleType::Page);
  }
}
#endif

bool Rule::IsReadOnly() const {
  MOZ_ASSERT(!mSheet || !mParentRule ||
                 mSheet->IsReadOnly() == mParentRule->IsReadOnly(),
             "a parent rule should be read only iff the owning sheet is "
             "read only");
  return mSheet && mSheet->IsReadOnly();
}

bool Rule::IsIncompleteImportRule() const {
  if (Type() != StyleCssRuleType::Import) {
    return false;
  }
  auto* sheet = static_cast<const dom::CSSImportRule*>(this)->GetStyleSheet();
  return !sheet || !sheet->IsComplete();
}

auto Rule::GetContainingRuleStateForParsing() const -> ContainingRuleState {
  ContainingRuleState result;
  for (const auto* rule = this; rule; rule = rule->GetParentRule()) {
    auto type = rule->Type();
    result.mContainingTypes |= (1 << UnderlyingValue(type));
    if (result.mParseRelativeType.isNothing() &&
        (type == StyleCssRuleType::Style || type == StyleCssRuleType::Scope)) {
      result.mParseRelativeType.emplace(type);
    }
  }
  return result;
}

}  // namespace mozilla::css
