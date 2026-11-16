/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Feature.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "nsIURI.h"
#include "nsNetUtil.h"

namespace mozilla::dom {

void Feature::GetAllowList(nsTArray<nsCOMPtr<nsIPrincipal>>& aList) const {
  MOZ_ASSERT(mPolicy == eAllowList);
  aList.AppendElements(mAllowList);
}

void Feature::GetSourceExpressions(nsTArray<nsString>& aList) const {
  MOZ_ASSERT(mPolicy == eAllowList);
  for (const RefPtr<ParsedSourceExpression>& expression : mSourceExpressions) {
    nsAutoString serialized;
    expression->Source().toString(serialized);
    aList.AppendElement(serialized);
  }
}

bool Feature::Allows(nsIPrincipal* aPrincipal) const {
  if (mPolicy == eNone) {
    return false;
  }

  if (mPolicy == eAll) {
    return true;
  }

  return AllowListContains(aPrincipal);
}

Feature::Feature(const nsAString& aFeatureName)
    : mFeatureName(aFeatureName), mPolicy(eAllowList) {}

Feature::~Feature() = default;

const nsAString& Feature::Name() const { return mFeatureName; }

void Feature::SetAllowsNone() {
  mPolicy = eNone;
  mAllowList.Clear();
  mSourceExpressions.Clear();
}

bool Feature::AllowsNone() const { return mPolicy == eNone; }

void Feature::SetAllowsAll() {
  mPolicy = eAll;
  mAllowList.Clear();
  mSourceExpressions.Clear();
}

bool Feature::AllowsAll() const { return mPolicy == eAll; }

void Feature::AppendToAllowList(nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(aPrincipal);

  mPolicy = eAllowList;
  mAllowList.AppendElement(aPrincipal);
}

void Feature::AppendSourceExpression(
    UniquePtr<nsCSPBaseSrc> aSourceExpression) {
  MOZ_ASSERT(aSourceExpression);

  mPolicy = eAllowList;
  mSourceExpressions.AppendElement(
      MakeRefPtr<ParsedSourceExpression>(std::move(aSourceExpression)));
}

void Feature::AppendSourceExpressionsFromFeature(const Feature& aOther) {
  MOZ_ASSERT(aOther.mPolicy == eAllowList);

  mPolicy = eAllowList;
  mSourceExpressions.AppendElements(aOther.mSourceExpressions);
}

bool Feature::AllowListContains(nsIPrincipal* aPrincipal) const {
  MOZ_ASSERT(aPrincipal);

  if (!HasAllowList()) {
    return false;
  }

  // https://w3c.github.io/webappsec-permissions-policy/#matches
  // 1 and 2. Match self-origin and src-origin.
  for (nsIPrincipal* principal : mAllowList) {
    if (BasePrincipal::Cast(principal)->Subsumes(
            aPrincipal, BasePrincipal::ConsiderDocumentDomain)) {
      return true;
    }
  }

  if (mSourceExpressions.IsEmpty()) {
    return false;
  }

  // 3. If origin is an opaque origin, return false.
  nsAutoCString serializedOrigin;
  if (NS_FAILED(
          aPrincipal->GetWebExposedOriginSerialization(serializedOrigin)) ||
      serializedOrigin.EqualsLiteral("null")) {
    return false;
  }

  // 4. Parse the serialized origin as a URL.
  nsCOMPtr<nsIURI> originURL;
  if (NS_FAILED(NS_NewURI(getter_AddRefs(originURL), serializedOrigin))) {
    return false;
  }

  // 5. For each permissions-source-expression item in the allowlist’s
  // expressions:
  for (const RefPtr<ParsedSourceExpression>& expression : mSourceExpressions) {
    // 5.1. If the result of running Does url match expression in origin with
    // redirect count? on url, item, origin, and 0 is true then return true.
    if (expression->Source().permits(originURL, false, false, false)) {
      return true;
    }
  }

  // 6. Return false.
  return false;
}

bool Feature::HasAllowList() const { return mPolicy == eAllowList; }

}  // namespace mozilla::dom
