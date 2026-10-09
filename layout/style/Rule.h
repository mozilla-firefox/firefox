/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* base class for all rule types in a CSS style sheet */

#ifndef mozilla_css_Rule_h_
#define mozilla_css_Rule_h_

#include "mozilla/MemoryReporting.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/CSSRuleBinding.h"
#include "mozilla/dom/DocumentOrShadowRoot.h"
#include "nsISupports.h"
#include "nsWrapperCache.h"

template <class T>
struct already_AddRefed;

namespace mozilla {

enum class StyleCssRuleType : uint8_t;

namespace css {
class GroupRule;

class Rule : public nsISupports, public nsWrapperCache, public SupportsWeakPtr {
 protected:
  Rule(StyleSheet* aSheet, Rule* aParentRule, uint32_t aLineNumber,
       uint32_t aColumnNumber)
      : mSheet(aSheet),
        mParentRule(aParentRule),
        mLineNumber(aLineNumber),
        mColumnNumber(aColumnNumber) {
#ifdef DEBUG
    AssertParentRuleType();
#endif
  }

#ifdef DEBUG
  void AssertParentRuleType();
#endif

  Rule(const Rule& aCopy)
      : mSheet(aCopy.mSheet),
        mParentRule(aCopy.mParentRule),
        mLineNumber(aCopy.mLineNumber),
        mColumnNumber(aCopy.mColumnNumber) {}

  virtual ~Rule() = default;

 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_WRAPPERCACHE_CLASS(Rule)
  // Return true if this rule is known to be a cycle collection leaf, in the
  // sense that it doesn't have any outgoing owning edges.
  virtual bool IsCCLeaf() const MOZ_MUST_OVERRIDE;

  virtual bool IsGroupRule() const { return false; }

#ifdef DEBUG
  virtual void List(FILE* out = stdout, int32_t aIndent = 0) const = 0;
#endif

  StyleSheet* GetStyleSheet() const { return mSheet; }

  // Clear the mSheet pointer on this rule and descendants.
  virtual void DropSheetReference();

  // Clear the mParentRule pointer on this rule.
  void DropParentRuleReference() { mParentRule = nullptr; }

  void DropReferences() {
    DropSheetReference();
    DropParentRuleReference();
  }

  uint32_t GetLineNumber() const { return mLineNumber; }
  uint32_t GetColumnNumber() const { return mColumnNumber; }

  // Whether this a rule in a read only style sheet.
  bool IsReadOnly() const;

  // Whether this rule is an @import rule that hasn't loaded yet (and thus
  // doesn't affect the style of the page).
  bool IsIncompleteImportRule() const;

  // This is pure virtual because all of Rule's data members are non-owning and
  // thus measured elsewhere.
  virtual size_t SizeOfIncludingThis(MallocSizeOf) const MOZ_MUST_OVERRIDE = 0;

  virtual StyleCssRuleType Type() const = 0;

  // WebIDL interface
  uint16_t TypeForBindings() const {
    auto type = uint16_t(Type());
    // Per https://drafts.csswg.org/cssom/#dom-cssrule-type for constants > 15
    // we return 0.
    return type > 15 ? 0 : type;
  }
  virtual void GetCssText(nsACString& aCssText) const = 0;
  void SetCssText(const nsACString& aCssText);
  Rule* GetParentRule() const;
  already_AddRefed<Rule> GetParentRuleForBindings() const;
  StyleSheet* GetParentStyleSheet() const { return GetStyleSheet(); }
  nsINode* GetAssociatedDocumentOrShadowRoot() const {
    if (!mSheet) {
      return nullptr;
    }
    auto* associated = mSheet->GetAssociatedDocumentOrShadowRoot();
    return associated ? &associated->AsNode() : nullptr;
  }
  nsISupports* GetParentObject() const { return mSheet; }

  struct ContainingRuleState {
    uint32_t mContainingTypes = 0;
    Maybe<StyleCssRuleType> mParseRelativeType;

    static ContainingRuleState From(Rule* aRule) {
      return aRule ? aRule->GetContainingRuleStateForParsing()
                   : ContainingRuleState();
    }
  };
  ContainingRuleState GetContainingRuleStateForParsing() const;

 protected:
  // True if we're known-live for cycle collection purposes.
  bool IsKnownLive() const;

  // Hook subclasses can use to properly unlink the nsWrapperCache of
  // their declarations.
  void UnlinkDeclarationWrapper(nsWrapperCache& aDecl);

  // mSheet should only ever be null when we create a synthetic CSSFontFaceRule
  // for an InspectorFontFace.
  //
  // mSheet and mParentRule will be cleared when they are detached from the
  // parent object, either because the rule is removed or the parent is
  // destroyed.
  StyleSheet* MOZ_NON_OWNING_REF mSheet;
  Rule* MOZ_NON_OWNING_REF mParentRule;

  // Keep the same type so that MSVC packs them.
  uint32_t mLineNumber;
  uint32_t mColumnNumber;
};

}  // namespace css
}  // namespace mozilla

#endif /* mozilla_css_Rule_h_ */
