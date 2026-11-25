/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PermissionsPolicyUtils.h"

#include "ipc/IPCMessageUtilsSpecializations.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/PermissionMessageUtils.h"
#include "mozilla/dom/PermissionsPolicyViolationReportBody.h"
#include "mozilla/dom/ReportingUtils.h"
#include "nsContentUtils.h"
#include "nsIOService.h"
#include "nsJSUtils.h"

namespace mozilla {
namespace dom {

struct FeatureMap {
  const char* mFeatureName;
  PermissionsPolicyUtils::PermissionsPolicyValue mDefaultAllowList;
};

/*
 * IMPORTANT: Do not change this list without review from a DOM peer _AND_ a
 * DOM Security peer!
 */
static FeatureMap sSupportedFeatures[] = {
    {"camera", PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"geolocation", PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"microphone", PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"on-device-speech-recognition",
     PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"digital-credentials-create",
     PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"digital-credentials-get",
     PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"display-capture", PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"fullscreen", PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"web-share", PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"gamepad", PermissionsPolicyUtils::PermissionsPolicyValue::eAll},
    {"publickey-credentials-create",
     PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"publickey-credentials-get",
     PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"serial", PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"speaker-selection",
     PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"storage-access", PermissionsPolicyUtils::PermissionsPolicyValue::eAll},
    {"screen-wake-lock", PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"loopback-network", PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"local-network", PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"aria-notify", PermissionsPolicyUtils::PermissionsPolicyValue::eAll},
    {"picture-in-picture",
     PermissionsPolicyUtils::PermissionsPolicyValue::eAll},
};

/*
 * This is experimental features list, which is disabled by default by pref
 * dom.security.permissionsPolicy.experimental.enabled.
 */
static FeatureMap sExperimentalFeatures[] = {
    // We don't support 'autoplay' for now, because it would be overwrote by
    // 'user-gesture-activation' policy. However, we can still keep it in the
    // list as we might start supporting it after we use different autoplay
    // policy.
    {"autoplay", PermissionsPolicyUtils::PermissionsPolicyValue::eAll},
    {"encrypted-media", PermissionsPolicyUtils::PermissionsPolicyValue::eAll},
    {"midi", PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
    {"payment", PermissionsPolicyUtils::PermissionsPolicyValue::eAll},
    {"document-domain", PermissionsPolicyUtils::PermissionsPolicyValue::eAll},
    {"vr", PermissionsPolicyUtils::PermissionsPolicyValue::eAll},
    // https://immersive-web.github.io/webxr/#permissions-policy
    {"xr-spatial-tracking",
     PermissionsPolicyUtils::PermissionsPolicyValue::eSelf},
};

/* static */
bool PermissionsPolicyUtils::IsExperimentalFeature(
    const nsAString& aFeatureName) {
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
bool PermissionsPolicyUtils::IsSupportedFeature(const nsAString& aFeatureName) {
  uint32_t numFeatures =
      (sizeof(sSupportedFeatures) / sizeof(sSupportedFeatures[0]));
  for (uint32_t i = 0; i < numFeatures; ++i) {
    if (aFeatureName.LowerCaseEqualsASCII(sSupportedFeatures[i].mFeatureName)) {
      return true;
    }
  }

  return StaticPrefs::dom_security_permissionsPolicy_experimental_enabled() &&
         IsExperimentalFeature(aFeatureName);
}

/* static */
void PermissionsPolicyUtils::ForEachFeature(
    const std::function<void(const char*)>& aCallback) {
  uint32_t numFeatures =
      (sizeof(sSupportedFeatures) / sizeof(sSupportedFeatures[0]));
  for (uint32_t i = 0; i < numFeatures; ++i) {
    aCallback(sSupportedFeatures[i].mFeatureName);
  }

  if (StaticPrefs::dom_security_permissionsPolicy_experimental_enabled()) {
    numFeatures =
        (sizeof(sExperimentalFeatures) / sizeof(sExperimentalFeatures[0]));
    for (uint32_t i = 0; i < numFeatures; ++i) {
      aCallback(sExperimentalFeatures[i].mFeatureName);
    }
  }
}

/* static */ PermissionsPolicyUtils::PermissionsPolicyValue
PermissionsPolicyUtils::DefaultAllowListFeature(const nsAString& aFeatureName) {
  uint32_t numFeatures =
      (sizeof(sSupportedFeatures) / sizeof(sSupportedFeatures[0]));
  for (uint32_t i = 0; i < numFeatures; ++i) {
    if (aFeatureName.LowerCaseEqualsASCII(sSupportedFeatures[i].mFeatureName)) {
      return sSupportedFeatures[i].mDefaultAllowList;
    }
  }

  if (StaticPrefs::dom_security_permissionsPolicy_experimental_enabled()) {
    numFeatures =
        (sizeof(sExperimentalFeatures) / sizeof(sExperimentalFeatures[0]));
    for (uint32_t i = 0; i < numFeatures; ++i) {
      if (aFeatureName.LowerCaseEqualsASCII(
              sExperimentalFeatures[i].mFeatureName)) {
        return sExperimentalFeatures[i].mDefaultAllowList;
      }
    }
  }

  return PermissionsPolicyValue::eNone;
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
bool PermissionsPolicyUtils::IsFeatureUnsafeAllowedAll(
    Document* aDocument, const nsAString& aFeatureName) {
  MOZ_ASSERT(aDocument);

  PermissionsPolicy* policy = aDocument->PermissionsPolicy();
  MOZ_ASSERT(policy);

  return policy->HasFeatureUnsafeAllowsAll(aFeatureName) &&
         !policy->IsSameOriginAsSrc(aDocument->NodePrincipal()) &&
         !policy->AllowsFeatureExplicitlyInAncestorChain(
             aFeatureName, policy->DefaultOrigin()) &&
         !IsSameOriginAsTop(aDocument);
}

/* static */
bool PermissionsPolicyUtils::IsFeatureAllowed(Document* aDocument,
                                              const nsAString& aFeatureName) {
  MOZ_ASSERT(aDocument);

  // Skip apply features in experimental phase
  if (!StaticPrefs::dom_security_permissionsPolicy_experimental_enabled() &&
      IsExperimentalFeature(aFeatureName)) {
    return true;
  }

  PermissionsPolicy* policy = aDocument->PermissionsPolicy();
  MOZ_ASSERT(policy);

  if (policy->AllowsFeatureInternal(aFeatureName, policy->DefaultOrigin())) {
    return true;
  }

  ReportViolation(aDocument, aFeatureName);
  return false;
}

/* static */
void PermissionsPolicyUtils::ReportViolation(Document* aDocument,
                                             const nsAString& aFeatureName) {
  MOZ_ASSERT(aDocument);

  nsCOMPtr<nsIURI> uri = aDocument->GetDocumentURI();
  if (NS_WARN_IF(!uri)) {
    return;
  }

  nsAutoCString url;
  ReportingUtils::StripURL(uri, url);

  JSContext* cx = nsContentUtils::GetCurrentJSContext();
  if (NS_WARN_IF(!cx)) {
    return;
  }

  Nullable<int32_t> lineNumber;
  Nullable<int32_t> columnNumber;
  nsAutoCString sourceFile;
  if (auto loc = JSCallingLocation::Get()) {
    lineNumber.SetValue(static_cast<int32_t>(loc.mLine));
    columnNumber.SetValue(static_cast<int32_t>(loc.mColumn));
    ReportingUtils::StripLocationFileName(loc, sourceFile);
  }

  nsPIDOMWindowInner* window = aDocument->GetInnerWindow();
  if (NS_WARN_IF(!window)) {
    return;
  }

  RefPtr<PermissionsPolicyViolationReportBody> body =
      new PermissionsPolicyViolationReportBody(window->AsGlobal(), aFeatureName,
                                               sourceFile, lineNumber,
                                               columnNumber, u"enforce"_ns);

  ReportingUtils::Report(window->AsGlobal(),
                         nsGkAtoms::permissionsPolicyViolation, "default"_ns,
                         url, body);
}

}  // namespace dom
}  // namespace mozilla

namespace IPC {

IMPLEMENT_IPC_SERIALIZER_WITH_FIELDS(mozilla::dom::PermissionsPolicyInfo,
                                     mInheritedDeniedFeatureNames,
                                     mAttributeEnabledFeatureNames,
                                     mDeclaredString, mDefaultOrigin,
                                     mSelfOrigin, mSrcOrigin);

}  // namespace IPC
