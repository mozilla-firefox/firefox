/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SpeculationRuleSet.h"

#include "js/friend/ErrorMessages.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/PrefetchCandidates.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/speculationrules_ffi_generated.h"
#include "nsContentUtils.h"
#include "nsIURI.h"

namespace mozilla::dom {

/* static */ void SpeculationRuleSet::operator delete(
    void* aSpeculationRuleSet) {
  speculation_rule_set_destroy(
      reinterpret_cast<SpeculationRuleSet*>(aSpeculationRuleSet));
}

// https://html.spec.whatwg.org/#parse-a-speculation-rule-set-string
/* static */
Result<UniquePtr<SpeculationRuleSet>, SpeculationRuleParseError>
SpeculationRuleSet::Parse(const nsACString& aSource, nsIURI* aDocumentBaseUri,
                          nsIURI* aBaseUri) {
  MOZ_ASSERT(aDocumentBaseUri && aBaseUri);
  nsAutoCString documentBaseUri;
  aDocumentBaseUri->GetSpec(documentBaseUri);
  nsAutoCString baseUri;
  aBaseUri->GetSpec(baseUri);

  SpeculationRuleParseError parseError = SpeculationRuleParseError::None;
  SpeculationRuleSet* parsedRuleSet = parse_speculation_rule_set(
      &aSource, &documentBaseUri, &baseUri, &parseError);
  if (!parsedRuleSet) {
    // Steps 2 and 5.1
    return Err(parseError);
  }
  return UniquePtr<SpeculationRuleSet>(parsedRuleSet);
}

/* static */ void SpeculationRuleSet::ReportParseError(
    nsIGlobalObject* aGlobal, SpeculationRuleParseError aError) {
  MOZ_ASSERT(aGlobal);
  MOZ_ASSERT(aError != SpeculationRuleParseError::None);
  nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
      "SpeculationRuleSet::ReportParseError",
      [global = nsCOMPtr<nsIGlobalObject>(aGlobal), aError]() {
        AutoJSAPI jsapi;
        if (!jsapi.Init(global)) {
          return;
        }
        JSErrNum errorNumber = JSMSG_SPECULATION_RULES_NOT_A_MAP;
        switch (aError) {
          case SpeculationRuleParseError::TopLevelValueMustBeJsonObject:
            errorNumber = JSMSG_SPECULATION_RULES_NOT_A_MAP;
            break;
          case SpeculationRuleParseError::InvalidTag:
            errorNumber = JSMSG_SPECULATION_RULES_INVALID_TAG;
            break;
          case SpeculationRuleParseError::InvalidBaseUrl:
            errorNumber = JSMSG_SPECULATION_RULES_INVALID_BASE_URL;
            break;
          case SpeculationRuleParseError::None:
            MOZ_ASSERT_UNREACHABLE();
            return;
        }
        JS_ReportErrorNumberASCII(jsapi.cx(), js::GetErrorMessage, nullptr,
                                  errorNumber);
      }));
}

void SpeculationRuleSet::ConsiderLoads(PrefetchCandidates* aCandidates,
                                       const nsTArray<const Element*>& aLinks) {
  consider_speculative_loads_for_rule_set(this, aCandidates, &aLinks);
}

bool SpeculationRuleSet::HasDocumentRules() const {
  return speculation_rule_set_has_document_rules(this);
}

void SpeculationRuleSet::SetUseCounters(Document& aDocument) const {
  SpeculationRulesUsage counters = speculation_rule_set_use_counters(this);
  if (counters.used_prefetch) {
    aDocument.SetUseCounter(eUseCounter_custom_SpeculationRulesPrefetch);
  }
  if (counters.used_list_source) {
    aDocument.SetUseCounter(eUseCounter_custom_SpeculationRulesListSource);
  }
  if (counters.used_document_source) {
    aDocument.SetUseCounter(eUseCounter_custom_SpeculationRulesDocumentSource);
  }
  if (counters.used_eagerness_conservative) {
    aDocument.SetUseCounter(
        eUseCounter_custom_SpeculationRulesEagernessConservative);
  }
  if (counters.used_eagerness_moderate) {
    aDocument.SetUseCounter(
        eUseCounter_custom_SpeculationRulesEagernessModerate);
  }
  if (counters.used_eagerness_eager) {
    aDocument.SetUseCounter(eUseCounter_custom_SpeculationRulesEagernessEager);
  }
  if (counters.used_eagerness_immediate) {
    aDocument.SetUseCounter(
        eUseCounter_custom_SpeculationRulesEagernessImmediate);
  }
  if (counters.used_tag) {
    aDocument.SetUseCounter(eUseCounter_custom_SpeculationRulesTag);
  }
}

}  // namespace mozilla::dom
