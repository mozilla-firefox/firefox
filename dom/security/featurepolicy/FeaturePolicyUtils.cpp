/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FeaturePolicyUtils.h"

#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/FeaturePolicyBinding.h"
#include "mozilla/dom/PermissionMessageUtils.h"
#include "mozilla/dom/ReportingUtils.h"
#include "mozilla/ipc/IPDLParamTraits.h"
#include "nsContentUtils.h"
#include "nsIOService.h"
#include "nsJSUtils.h"

namespace mozilla {
namespace dom {

struct FeatureMap {
  const char* mFeatureName;
  FeaturePolicyUtils::FeaturePolicyValue mDefaultAllowList;
};

/*
 * IMPORTANT: Do not change this list without review from a DOM peer _AND_ a
 * DOM Security peer!
 */
static FeatureMap sSupportedFeatures[] = {
    {"camera", FeaturePolicyUtils::FeaturePolicyValue::eSelf},
    {"geolocation", FeaturePolicyUtils::FeaturePolicyValue::eSelf},
    {"microphone", FeaturePolicyUtils::FeaturePolicyValue::eSelf},
    {"display-capture", FeaturePolicyUtils::FeaturePolicyValue::eSelf},
    {"fullscreen", FeaturePolicyUtils::FeaturePolicyValue::eSelf},
    {"web-share", FeaturePolicyUtils::FeaturePolicyValue::eSelf},
    {"gamepad", FeaturePolicyUtils::FeaturePolicyValue::eAll},
    {"publickey-credentials-create",
     FeaturePolicyUtils::FeaturePolicyValue::eSelf},
    {"publickey-credentials-get",
     FeaturePolicyUtils::FeaturePolicyValue::eSelf},
    {"speaker-selection", FeaturePolicyUtils::FeaturePolicyValue::eSelf},
    {"storage-access", FeaturePolicyUtils::FeaturePolicyValue::eAll},
    {"screen-wake-lock", FeaturePolicyUtils::FeaturePolicyValue::eSelf},
};

/*
 * This is experimental features list, which is disabled by default by pref
 * dom.security.featurePolicy.experimental.enabled.
 */
static FeatureMap sExperimentalFeatures[] = {
    // We don't support 'autoplay' for now, because it would be overwrote by
    // 'user-gesture-activation' policy. However, we can still keep it in the
    // list as we might start supporting it after we use different autoplay
    // policy.
    {"autoplay", FeaturePolicyUtils::FeaturePolicyValue::eAll},
    {"encrypted-media", FeaturePolicyUtils::FeaturePolicyValue::eAll},
    {"midi", FeaturePolicyUtils::FeaturePolicyValue::eSelf},
    {"payment", FeaturePolicyUtils::FeaturePolicyValue::eAll},
    {"document-domain", FeaturePolicyUtils::FeaturePolicyValue::eAll},
    {"vr", FeaturePolicyUtils::FeaturePolicyValue::eAll},
    // https://immersive-web.github.io/webxr/#feature-policy
    {"xr-spatial-tracking", FeaturePolicyUtils::FeaturePolicyValue::eSelf},
};

/* static */
bool FeaturePolicyUtils::IsExperimentalFeature(const nsAString& aFeatureName) {
  uint32_t numFeatures =
      (sizeof(sExperimentalFeatures) / sizeof(sExperimentalFeatures[0]));
  for (uint32_t i = 0; i < numFeatures; ++i) {
    if (aFeatureName.LowerCaseEqualsASCII(
            sExperimentalFeatures[i].mFeatureName)) {
      return true;
    }
  }

  return false;
}

/* static */
bool FeaturePolicyUtils::IsSupportedFeature(const nsAString& aFeatureName) {
  uint32_t numFeatures =
      (sizeof(sSupportedFeatures) / sizeof(sSupportedFeatures[0]));
  for (uint32_t i = 0; i < numFeatures; ++i) {
    if (aFeatureName.LowerCaseEqualsASCII(sSupportedFeatures[i].mFeatureName)) {
      return true;
    }
  }

  return StaticPrefs::dom_security_featurePolicy_experimental_enabled() &&
         IsExperimentalFeature(aFeatureName);
}

/* static */
void FeaturePolicyUtils::ForEachFeature(
    const std::function<void(const char*)>& aCallback) {
  uint32_t numFeatures =
      (sizeof(sSupportedFeatures) / sizeof(sSupportedFeatures[0]));
  for (uint32_t i = 0; i < numFeatures; ++i) {
    aCallback(sSupportedFeatures[i].mFeatureName);
  }

  if (StaticPrefs::dom_security_featurePolicy_experimental_enabled()) {
    numFeatures =
        (sizeof(sExperimentalFeatures) / sizeof(sExperimentalFeatures[0]));
    for (uint32_t i = 0; i < numFeatures; ++i) {
      aCallback(sExperimentalFeatures[i].mFeatureName);
    }
  }
}

/* static */ FeaturePolicyUtils::FeaturePolicyValue
FeaturePolicyUtils::DefaultAllowListFeature(const nsAString& aFeatureName) {
  uint32_t numFeatures =
      (sizeof(sSupportedFeatures) / sizeof(sSupportedFeatures[0]));
  for (uint32_t i = 0; i < numFeatures; ++i) {
    if (aFeatureName.LowerCaseEqualsASCII(sSupportedFeatures[i].mFeatureName)) {
      return sSupportedFeatures[i].mDefaultAllowList;
    }
  }

  if (StaticPrefs::dom_security_featurePolicy_experimental_enabled()) {
    numFeatures =
        (sizeof(sExperimentalFeatures) / sizeof(sExperimentalFeatures[0]));
    for (uint32_t i = 0; i < numFeatures; ++i) {
      if (aFeatureName.LowerCaseEqualsASCII(
              sExperimentalFeatures[i].mFeatureName)) {
        return sExperimentalFeatures[i].mDefaultAllowList;
      }
    }
  }

  return FeaturePolicyValue::eNone;
}

static bool IsSameOriginAsTop(Document* aDocument) {
  MOZ_ASSERT(aDocument);

  BrowsingContext* browsingContext = aDocument->GetBrowsingContext();
  if (!browsingContext) {
    return false;
  }

  nsPIDOMWindowOuter* topWindow = browsingContext->Top()->GetDOMWindow();
  if (!topWindow) {
    // If we don't have a DOMWindow, We are not in same origin.
    return false;
  }

  Document* topLevelDocument = topWindow->GetExtantDoc();
  if (!topLevelDocument) {
    return false;
  }

  return NS_SUCCEEDED(
      nsContentUtils::CheckSameOrigin(topLevelDocument, aDocument));
}

/* static */
bool FeaturePolicyUtils::IsFeatureUnsafeAllowedAll(
    Document* aDocument, const nsAString& aFeatureName) {
  MOZ_ASSERT(aDocument);

  if (!aDocument->IsHTMLDocument()) {
    return false;
  }

  FeaturePolicy* policy = aDocument->FeaturePolicy();
  MOZ_ASSERT(policy);

  return policy->HasFeatureUnsafeAllowsAll(aFeatureName) &&
         !policy->IsSameOriginAsSrc(aDocument->NodePrincipal()) &&
         !policy->AllowsFeatureExplicitlyInAncestorChain(
             aFeatureName, policy->DefaultOrigin()) &&
         !IsSameOriginAsTop(aDocument);
}

/* static */
bool FeaturePolicyUtils::IsFeatureAllowed(Document* aDocument,
                                          const nsAString& aFeatureName) {
  MOZ_ASSERT(aDocument);

  // Skip apply features in experimental phase
  if (!StaticPrefs::dom_security_featurePolicy_experimental_enabled() &&
      IsExperimentalFeature(aFeatureName)) {
    return true;
  }

  FeaturePolicy* policy = aDocument->FeaturePolicy();
  MOZ_ASSERT(policy);

  if (policy->AllowsFeatureInternal(aFeatureName, policy->DefaultOrigin())) {
    return true;
  }

  ReportViolation(aDocument, aFeatureName);
  return false;
}

/* static */
void FeaturePolicyUtils::ReportViolation(Document* aDocument,
                                         const nsAString& aFeatureName) {
  MOZ_ASSERT(aDocument);

  nsCOMPtr<nsIURI> uri = aDocument->GetDocumentURI();
  if (NS_WARN_IF(!uri)) {
    return;
  }

  // Strip the URL of any possible username/password and make it ready to be
  // presented in the UI.
  nsCOMPtr<nsIURI> exposableURI = net::nsIOService::CreateExposableURI(uri);
  nsAutoCString spec;
  nsresult rv = exposableURI->GetSpec(spec);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }
  JSContext* cx = nsContentUtils::GetCurrentJSContext();
  if (NS_WARN_IF(!cx)) {
    return;
  }

  Nullable<int32_t> lineNumber;
  Nullable<int32_t> columnNumber;
  auto loc = JSCallingLocation::Get();
  if (loc) {
    lineNumber.SetValue(static_cast<int32_t>(loc.mLine));
    columnNumber.SetValue(static_cast<int32_t>(loc.mColumn));
  }

  nsPIDOMWindowInner* window = aDocument->GetInnerWindow();
  if (NS_WARN_IF(!window)) {
    return;
  }

  FeaturePolicyViolationReportBody body;
  body.mFeatureId.Construct(aFeatureName);
  body.mSourceFile.Construct(loc.FileName());
  body.mLineNumber.Construct(lineNumber.Value());
  body.mColumnNumber.Construct(columnNumber.Value());
  body.mDisposition.Construct(u"enforce"_ns);

  ReportingUtils::Report(window->AsGlobal(), nsGkAtoms::featurePolicyViolation,
                         u"default"_ns, NS_ConvertUTF8toUTF16(spec), &body);
}

}  // namespace dom

namespace ipc {

void IPDLParamTraits<dom::FeaturePolicyInfo>::Write(
    IPC::MessageWriter* aWriter, IProtocol* aActor,
    const dom::FeaturePolicyInfo& aParam) {
  WriteIPDLParam(aWriter, aActor, aParam.mInheritedDeniedFeatureNames);
  WriteIPDLParam(aWriter, aActor, aParam.mAttributeEnabledFeatureNames);
  WriteIPDLParam(aWriter, aActor, aParam.mDeclaredString);
  WriteIPDLParam(aWriter, aActor, aParam.mDefaultOrigin);
  WriteIPDLParam(aWriter, aActor, aParam.mSelfOrigin);
  WriteIPDLParam(aWriter, aActor, aParam.mSrcOrigin);
}

bool IPDLParamTraits<dom::FeaturePolicyInfo>::Read(
    IPC::MessageReader* aReader, IProtocol* aActor,
    dom::FeaturePolicyInfo* aResult) {
  if (!ReadIPDLParam(aReader, aActor, &aResult->mInheritedDeniedFeatureNames)) {
    return false;
  }

  if (!ReadIPDLParam(aReader, aActor,
                     &aResult->mAttributeEnabledFeatureNames)) {
    return false;
  }

  if (!ReadIPDLParam(aReader, aActor, &aResult->mDeclaredString)) {
    return false;
  }

  if (!ReadIPDLParam(aReader, aActor, &aResult->mDefaultOrigin)) {
    return false;
  }

  if (!ReadIPDLParam(aReader, aActor, &aResult->mSelfOrigin)) {
    return false;
  }

  if (!ReadIPDLParam(aReader, aActor, &aResult->mSrcOrigin)) {
    return false;
  }

  return true;
}
}  // namespace ipc

}  // namespace mozilla
