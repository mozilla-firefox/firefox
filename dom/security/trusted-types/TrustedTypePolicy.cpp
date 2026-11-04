/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/TrustedTypePolicy.h"

#include <utility>

#include "mozilla/dom/DOMString.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/TrustedTypePolicyFactory.h"
#include "mozilla/dom/TrustedTypesBinding.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(TrustedTypePolicy, mParentObject,
                                      mOptions.mCreateHTMLCallback,
                                      mOptions.mCreateScriptCallback,
                                      mOptions.mCreateScriptURLCallback)

TrustedTypePolicy::TrustedTypePolicy(TrustedTypePolicyFactory* aParentObject,
                                     const nsAString& aName, Options&& aOptions)
    : mParentObject{aParentObject},
      mName{aName},
      mOptions{std::move(aOptions)} {}

JSObject* TrustedTypePolicy::WrapObject(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return TrustedTypePolicy_Binding::Wrap(aCx, this, aGivenProto);
}

void TrustedTypePolicy::GetName(DOMString& aResult) const {
  aResult.SetKnownLiveString(mName);
}

// Bug 2072122: Trusted Types readiness experiment only, to be removed: the
// UsedPolicy signal set by this macro.
#define IMPL_CREATE_TRUSTED_TYPE(_trustedTypeSuffix)                         \
  already_AddRefed<Trusted##_trustedTypeSuffix>                              \
      TrustedTypePolicy::Create##_trustedTypeSuffix(                         \
          JSContext* aJSContext, const nsAString& aInput,                    \
          const Sequence<JS::Value>& aArguments, ErrorResult& aErrorResult)  \
          const {                                                            \
    if (auto* piDOMWindowInner =                                             \
            mParentObject->GetParentObject()->GetAsInnerWindow()) {          \
      if (Document* doc = piDOMWindowInner->GetExtantDoc()) {                \
        doc->SetTrustedTypesReadinessSignal(                                 \
            TrustedTypesReadinessSignal::UsedPolicy);                        \
      }                                                                      \
    }                                                                        \
                                                                             \
    /* Invoking the callback could delete the policy and hence the callback. \
     * Hence keep a strong reference to it on the stack.                     \
     */                                                                      \
    RefPtr<Create##_trustedTypeSuffix##Callback> callbackObject =            \
        mOptions.mCreate##_trustedTypeSuffix##Callback;                      \
                                                                             \
    return CreateTrustedType<Trusted##_trustedTypeSuffix>(                   \
        callbackObject, aInput, aArguments, aErrorResult);                   \
  }

IMPL_CREATE_TRUSTED_TYPE(HTML)
IMPL_CREATE_TRUSTED_TYPE(Script)
IMPL_CREATE_TRUSTED_TYPE(ScriptURL)

template <typename T, typename CallbackObject>
already_AddRefed<T> TrustedTypePolicy::CreateTrustedType(
    const RefPtr<CallbackObject>& aCallbackObject, const nsAString& aValue,
    const Sequence<JS::Value>& aArguments, ErrorResult& aErrorResult) const {
  nsString policyValue;
  DetermineTrustedPolicyValue(aCallbackObject, aValue, aArguments, true,
                              aErrorResult, policyValue);

  if (aErrorResult.Failed()) {
    return nullptr;
  }

  if (policyValue.IsVoid()) {
    policyValue = EmptyString();
  }

  RefPtr<T> trustedObject = new T(std::move(policyValue));

  // TODO: add special handling for `TrustedScript` when default policy support
  // is added.

  return trustedObject.forget();
}

template <typename CallbackObject>
void TrustedTypePolicy::DetermineTrustedPolicyValue(
    const RefPtr<CallbackObject>& aCallbackObject, const nsAString& aValue,
    const nsTArray<JS::Value>& aArguments, bool aThrowIfMissing,
    ErrorResult& aErrorResult, nsAString& aResult) const {
  if (!aCallbackObject) {
    aResult.SetIsVoid(true);

    if (aThrowIfMissing) {
      aErrorResult.ThrowTypeError("Function missing.");
    }

    return;
  }

  nsString callbackResult;
  aCallbackObject->Call(aValue, aArguments, callbackResult, aErrorResult,
                        nullptr, CallbackObject::eRethrowExceptions);

  if (!aErrorResult.Failed()) {
    aResult = std::move(callbackResult);
  }
}

}  // namespace mozilla::dom
