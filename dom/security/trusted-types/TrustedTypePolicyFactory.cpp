/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/TrustedTypePolicyFactory.h"

#include <utility>

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/CSPViolationData.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/TrustedTypePolicy.h"
#include "mozilla/dom/TrustedTypeUtils.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "nsLiteralString.h"

using namespace mozilla::dom::TrustedTypeUtils;

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(TrustedTypePolicyFactory, mGlobalObject,
                                      mDefaultPolicy)

TrustedTypePolicyFactory::TrustedTypePolicyFactory(
    nsIGlobalObject* aGlobalObject)
    : mGlobalObject{aGlobalObject} {}

JSObject* TrustedTypePolicyFactory::WrapObject(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return TrustedTypePolicyFactory_Binding::Wrap(aCx, this, aGivenProto);
}

// Default implementation in the .cpp file required because `mDefaultPolicy`
// requires a definition for `TrustedTypePolicy`.
TrustedTypePolicyFactory::~TrustedTypePolicyFactory() = default;

// Implement reporting of violations for policy creation.
// https://w3c.github.io/trusted-types/dist/spec/#should-block-create-policy
static void ReportPolicyCreationViolations(
    nsIContentSecurityPolicy* aCSP, nsICSPEventListener* aCSPEventListener,
    const nsCString& aFileName, uint32_t aLine, uint32_t aColumn,
    const nsTArray<nsString>& aCreatedPolicyNames,
    const nsAString& aPolicyName) {
  MOZ_ASSERT(aCSP);
  uint32_t numPolicies = 0;
  aCSP->GetPolicyCount(&numPolicies);
  for (uint32_t i = 0; i < numPolicies; ++i) {
    const nsCSPPolicy* policy = aCSP->GetPolicy(i);
    if (policy->hasDirective(
            nsIContentSecurityPolicy::TRUSTED_TYPES_DIRECTIVE)) {
      if (policy->ShouldCreateViolationForNewTrustedTypesPolicy(
              aPolicyName, aCreatedPolicyNames)) {
        CSPViolationData cspViolationData{
            i,
            CSPViolationData::Resource{
                CSPViolationData::BlockedContentSource::TrustedTypesPolicy},
            nsIContentSecurityPolicy::TRUSTED_TYPES_DIRECTIVE,
            aFileName,
            aLine,
            aColumn,
            /* aElement */ nullptr,
            CSPViolationData::MaybeTruncateSample(aPolicyName)};
        aCSP->LogTrustedTypesViolationDetailsUnchecked(
            std::move(cspViolationData),
            NS_LITERAL_STRING_FROM_CSTRING(
                TRUSTED_TYPES_VIOLATION_OBSERVER_TOPIC),
            aCSPEventListener);
      }
    }
  }
}

class LogPolicyCreationViolationsRunnable final
    : public WorkerMainThreadRunnable {
 public:
  LogPolicyCreationViolationsRunnable(
      WorkerPrivate* aWorker, const nsCString& aFileName, uint32_t aLine,
      uint32_t aColumn, const nsTArray<nsString>& aCreatedPolicyNames,
      const nsAString& aPolicyName)
      : WorkerMainThreadRunnable(
            aWorker,
            "RuntimeService :: LogPolicyCreationViolationsRunnable"_ns),
        mFileName(aFileName),
        mLine(aLine),
        mColumn(aColumn),
        mCreatedPolicyNames(aCreatedPolicyNames),
        mPolicyName(aPolicyName) {
    MOZ_ASSERT(aWorker);
  }

  virtual bool MainThreadRun() override {
    AssertIsOnMainThread();
    MOZ_ASSERT(mWorkerRef);
    if (nsIContentSecurityPolicy* csp = mWorkerRef->Private()->GetCsp()) {
      ReportPolicyCreationViolations(
          csp, mWorkerRef->Private()->CSPEventListener(), mFileName, mLine,
          mColumn, mCreatedPolicyNames, mPolicyName);
    }
    return true;
  }

 private:
  ~LogPolicyCreationViolationsRunnable() = default;
  const nsCString& mFileName;
  uint32_t mLine;
  uint32_t mColumn;
  const nsTArray<nsString>& mCreatedPolicyNames;
  const nsString mPolicyName;
};

auto TrustedTypePolicyFactory::ShouldTrustedTypePolicyCreationBeBlockedByCSP(
    JSContext* aJSContext, const nsAString& aPolicyName) const
    -> PolicyCreation {
  auto shouldBlock = [this, &aPolicyName](const nsCSPPolicy* aPolicy) {
    return aPolicy->hasDirective(
               nsIContentSecurityPolicy::TRUSTED_TYPES_DIRECTIVE) &&
           aPolicy->getDisposition() == nsCSPPolicy::Disposition::Enforce &&
           aPolicy->ShouldCreateViolationForNewTrustedTypesPolicy(
               aPolicyName, mCreatedPolicyNames);
  };

  auto result = PolicyCreation::Allowed;
  auto location = JSCallingLocation::Get(aJSContext);
  if (auto* piDOMWindowInner = mGlobalObject->GetAsInnerWindow()) {
    if (auto* csp =
            PolicyContainer::GetCSP(piDOMWindowInner->GetPolicyContainer())) {
      ReportPolicyCreationViolations(
          csp, nullptr /* aCSPEventListener */, location.FileName(),
          location.mLine, location.mColumn, mCreatedPolicyNames, aPolicyName);
      uint32_t numPolicies = 0;
      csp->GetPolicyCount(&numPolicies);
      for (uint64_t i = 0; i < numPolicies; ++i) {
        const nsCSPPolicy* policy = csp->GetPolicy(i);
        if (shouldBlock(policy)) {
          result = PolicyCreation::Blocked;
          break;
        }
      }
    }
  } else {
    MOZ_ASSERT(IsWorkerGlobal(mGlobalObject->GetGlobalJSObject()));
    MOZ_ASSERT(!NS_IsMainThread());
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    RefPtr<LogPolicyCreationViolationsRunnable> runnable =
        new LogPolicyCreationViolationsRunnable(
            workerPrivate, location.FileName(), location.mLine,
            location.mColumn, mCreatedPolicyNames, aPolicyName);
    ErrorResult rv;
    runnable->Dispatch(workerPrivate, Killing, rv);
    if (NS_WARN_IF(rv.Failed())) {
      rv.SuppressException();
    }
    if (OffThreadCSPContext* ctx = workerPrivate->GetCSPContext()) {
      for (const UniquePtr<const nsCSPPolicy>& policy : ctx->Policies()) {
        if (shouldBlock(policy.get())) {
          result = PolicyCreation::Blocked;
          break;
        }
      }
    }
  }

  return result;
}

constexpr nsLiteralString kDefaultPolicyName = u"default"_ns;

// Bug 2072122: Trusted Types readiness experiment only, to be removed.
// Names of the Trusted Types policies created by libraries that support
// Trusted Types, for the security.trusted_types_readiness telemetry.
constexpr nsLiteralString kLibraryPolicyNames[] = {
    u"lit-html"_ns,
};

static bool IsLibraryPolicyName(const nsAString& aPolicyName) {
  for (const nsLiteralString& name : kLibraryPolicyNames) {
    if (aPolicyName.Equals(name)) {
      return true;
    }
  }
  return false;
}

already_AddRefed<TrustedTypePolicy> TrustedTypePolicyFactory::CreatePolicy(
    JSContext* aJSContext, const nsAString& aPolicyName,
    const TrustedTypePolicyOptions& aPolicyOptions, ErrorResult& aRv) {
  // Bug 2072122: Trusted Types readiness experiment only, to be removed.
  // Recorded before the CSP check: a library offering a policy counts towards
  // readiness even if the page's trusted-types directive rejects its name.
  if (auto* piDOMWindowInner = mGlobalObject->GetAsInnerWindow()) {
    if (Document* doc = piDOMWindowInner->GetExtantDoc();
        doc &&
        !doc->HasTrustedTypesReadinessSignal(
            TrustedTypesReadinessSignal::HasTrustedTypesLibrary) &&
        IsLibraryPolicyName(aPolicyName)) {
      doc->SetTrustedTypesReadinessSignal(
          TrustedTypesReadinessSignal::HasTrustedTypesLibrary);
    }
  }

  if (PolicyCreation::Blocked ==
      ShouldTrustedTypePolicyCreationBeBlockedByCSP(aJSContext, aPolicyName)) {
    nsCString errorMessage =
        "Content-Security-Policy blocked creating policy named '"_ns +
        NS_ConvertUTF16toUTF8(aPolicyName) + "'"_ns;

    // TODO: perhaps throw different TypeError messages,
    //       https://github.com/w3c/trusted-types/issues/511.
    aRv.ThrowTypeError(errorMessage);
    return nullptr;
  }

  if (aPolicyName.Equals(kDefaultPolicyName) && mDefaultPolicy) {
    aRv.ThrowTypeError("Tried to create a second default policy"_ns);
    return nullptr;
  }

  TrustedTypePolicy::Options options;

  if (aPolicyOptions.mCreateHTML.WasPassed()) {
    options.mCreateHTMLCallback = &aPolicyOptions.mCreateHTML.Value();
  }

  if (aPolicyOptions.mCreateScript.WasPassed()) {
    options.mCreateScriptCallback = &aPolicyOptions.mCreateScript.Value();
  }

  if (aPolicyOptions.mCreateScriptURL.WasPassed()) {
    options.mCreateScriptURLCallback = &aPolicyOptions.mCreateScriptURL.Value();
  }

  RefPtr<TrustedTypePolicy> policy =
      MakeRefPtr<TrustedTypePolicy>(this, aPolicyName, std::move(options));

  if (aPolicyName.Equals(kDefaultPolicyName)) {
    mDefaultPolicy = policy;
  }

  // Bug 2072122: Trusted Types readiness experiment only, to be removed.
  if (auto* piDOMWindowInner = mGlobalObject->GetAsInnerWindow()) {
    if (Document* doc = piDOMWindowInner->GetExtantDoc()) {
      doc->SetTrustedTypesReadinessSignal(
          TrustedTypesReadinessSignal::CreatedPolicy);
    }
  }

  mCreatedPolicyNames.AppendElement(aPolicyName);

  return policy.forget();
}

#define IS_TRUSTED_TYPE_IMPL(_trustedTypeSuffix)                                                                                                                         \
  bool TrustedTypePolicyFactory::Is##_trustedTypeSuffix(                                                                                                                 \
      JSContext*, const JS::Handle<JS::Value>& aValue) const {                                                                                                           \
    /**                                                                                                                                                                  \
     * No need to check the internal slot.                                                                                                                               \
     * Ensured by the corresponding test:                                                                                                                                \
     * <https://searchfox.org/mozilla-central/rev/b60cb73160843adb5a5a3ec8058e75a69b46acf7/testing/web-platform/tests/trusted-types/TrustedTypePolicyFactory-isXXX.html> \
     */                                                                                                                                                                  \
    return aValue.isObject() &&                                                                                                                                          \
           IS_INSTANCE_OF(Trusted##_trustedTypeSuffix, &aValue.toObject());                                                                                              \
  }

IS_TRUSTED_TYPE_IMPL(HTML);
IS_TRUSTED_TYPE_IMPL(Script);
IS_TRUSTED_TYPE_IMPL(ScriptURL);

already_AddRefed<TrustedHTML> TrustedTypePolicyFactory::EmptyHTML() {
  // Preserving the wrapper ensures:
  // ```
  //  const e = trustedTypes.emptyHTML;
  //  e === trustedTypes.emptyHTML;
  // ```
  // which comes with the cost of keeping the factory, one per global, alive.
  // An additional benefit is it saves the cost of re-instantiating potentially
  // multiple emptyHML objects. Both, the JS- and the C++-objects.
  dom::PreserveWrapper(this);

  RefPtr<TrustedHTML> result = new TrustedHTML(EmptyString());
  return result.forget();
}

already_AddRefed<TrustedScript> TrustedTypePolicyFactory::EmptyScript() {
  // See the explanation in `EmptyHTML()`.
  dom::PreserveWrapper(this);

  RefPtr<TrustedScript> result = new TrustedScript(EmptyString());
  return result.forget();
}

// TODO(fwang): Improve this API:
// - Rename aTagName parameter to use aLocalName instead
//   (https://github.com/w3c/trusted-types/issues/496)
// - Remove ASCII-case-insensitivity for aTagName and aAttribute
//  (https://github.com/w3c/trusted-types/issues/424)
// - Make aElementNs default to HTML namespace, so special handling for an empty
//   string is not needed (https://github.com/w3c/trusted-types/issues/381).
void TrustedTypePolicyFactory::GetAttributeType(const nsAString& aTagName,
                                                const nsAString& aAttribute,
                                                const nsAString& aElementNs,
                                                const nsAString& aAttrNs,
                                                DOMString& aResult) {
  // We first determine the namespace IDs for the element and attribute.
  // Currently, GetTrustedTypeDataForAttribute() only test a few of them so use
  // direct string comparisons instead of relying on
  // nsNameSpaceManager::GetNameSpaceID().

  // GetTrustedTypeDataForAttribute() can only return true for empty or XLink
  // attribute namespaces, so don't bother calling it for other namespaces.
  int32_t attributeNamespaceID = kNameSpaceID_Unknown;
  if (aAttrNs.IsEmpty()) {
    attributeNamespaceID = kNameSpaceID_None;
  } else if (nsGkAtoms::nsuri_xlink->Equals(aAttrNs)) {
    attributeNamespaceID = kNameSpaceID_XLink;
  } else {
    aResult.SetNull();
    return;
  }

  // GetTrustedTypeDataForAttribute() only return true for HTML, SVG or MathML
  // element namespaces, so don't bother calling it for other namespaces.
  int32_t elementNamespaceID = kNameSpaceID_Unknown;
  if (aElementNs.IsEmpty() || nsGkAtoms::nsuri_xhtml->Equals(aElementNs)) {
    elementNamespaceID = kNameSpaceID_XHTML;
  } else if (nsGkAtoms::nsuri_svg->Equals(aElementNs)) {
    elementNamespaceID = kNameSpaceID_SVG;
  } else if (nsGkAtoms::nsuri_mathml->Equals(aElementNs)) {
    elementNamespaceID = kNameSpaceID_MathML;
  } else {
    aResult.SetNull();
    return;
  }

  nsAutoString attribute;
  nsContentUtils::ASCIIToLower(aAttribute, attribute);
  RefPtr<nsAtom> attributeAtom = NS_Atomize(attribute);

  nsAutoString localName;
  nsContentUtils::ASCIIToLower(aTagName, localName);
  RefPtr<nsAtom> elementAtom = NS_Atomize(localName);

  TrustedType trustedType;
  nsAutoString unusedSink;
  if (GetTrustedTypeDataForAttribute(elementAtom, elementNamespaceID,
                                     attributeAtom, attributeNamespaceID,
                                     trustedType, unusedSink)) {
    aResult.SetKnownLiveString(GetTrustedTypeName(trustedType));
    return;
  }

  aResult.SetNull();
}

// TODO(fwang): Improve this API:
// - Rename aTagName parameter to use aLocalName instead
//   (https://github.com/w3c/trusted-types/issues/496)
// - Remove ASCII-case-insensitivity for aTagName
//  (https://github.com/w3c/trusted-types/issues/424)
// - Make aElementNs default to HTML namespace, so special handling for an empty
//   string is not needed (https://github.com/w3c/trusted-types/issues/381).
void TrustedTypePolicyFactory::GetPropertyType(const nsAString& aTagName,
                                               const nsAString& aProperty,
                                               const nsAString& aElementNs,
                                               DOMString& aResult) {
  RefPtr<nsAtom> propertyAtom = NS_Atomize(aProperty);
  if (aElementNs.IsEmpty() || nsGkAtoms::nsuri_xhtml->Equals(aElementNs)) {
    if (nsContentUtils::EqualsIgnoreASCIICase(
            aTagName, nsDependentAtomString(nsGkAtoms::iframe))) {
      // HTMLIFrameElement
      if (propertyAtom == nsGkAtoms::srcdoc) {
        aResult.SetKnownLiveString(GetTrustedTypeName<TrustedHTML>());
        return;
      }
    } else if (nsContentUtils::EqualsIgnoreASCIICase(
                   aTagName, nsDependentAtomString(nsGkAtoms::script))) {
      // HTMLScriptElement
      if (propertyAtom == nsGkAtoms::innerText ||
          propertyAtom == nsGkAtoms::text ||
          propertyAtom == nsGkAtoms::textContent) {
        aResult.SetKnownLiveString(GetTrustedTypeName<TrustedScript>());
        return;
      }
      if (propertyAtom == nsGkAtoms::src) {
        aResult.SetKnownLiveString(GetTrustedTypeName<TrustedScriptURL>());
        return;
      }
    }
  }
  // *
  if (propertyAtom == nsGkAtoms::innerHTML ||
      propertyAtom == nsGkAtoms::outerHTML) {
    aResult.SetKnownLiveString(GetTrustedTypeName<TrustedHTML>());
    return;
  }

  aResult.SetNull();
}

}  // namespace mozilla::dom
