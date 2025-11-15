/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCSPContext.h"

#include <string>
#include <unordered_set>
#include <utility>

#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/dom/CSPDictionariesBinding.h"
#include "mozilla/dom/CSPReportBinding.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ReportingBinding.h"
#include "mozilla/dom/ReportingUtils.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/glean/DomSecurityMetrics.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsCOMPtr.h"
#include "nsCSPParser.h"
#include "nsCSPService.h"
#include "nsCSPUtils.h"
#include "nsContentPolicyUtils.h"
#include "nsContentSecurityUtils.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsGlobalWindowOuter.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIClassInfoImpl.h"
#include "nsIContentPolicy.h"
#include "nsIEventTarget.h"
#include "nsIHttpChannel.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsINetworkInterceptController.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsIScriptElement.h"
#include "nsIScriptError.h"
#include "nsIStringStream.h"
#include "nsISupportsPrimitives.h"
#include "nsIURIMutator.h"
#include "nsIUploadChannel.h"
#include "nsJSUtils.h"
#include "nsMimeTypes.h"
#include "nsNetUtil.h"
#include "nsSandboxFlags.h"
#include "nsScriptSecurityManager.h"
#include "nsStreamUtils.h"
#include "nsString.h"
#include "nsStringStream.h"
#include "nsSupportsPrimitives.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::ipc;

static LogModule* GetCspContextLog() {
  static LazyLogModule gCspContextPRLog("CSPContext");
  return gCspContextPRLog;
}

#define CSPCONTEXTLOG(args) \
  MOZ_LOG(GetCspContextLog(), mozilla::LogLevel::Debug, args)
#define CSPCONTEXTLOGENABLED() \
  MOZ_LOG_TEST(GetCspContextLog(), mozilla::LogLevel::Debug)

static LogModule* GetCspOriginLogLog() {
  static LazyLogModule gCspOriginPRLog("CSPOrigin");
  return gCspOriginPRLog;
}

#define CSPORIGINLOG(args) \
  MOZ_LOG(GetCspOriginLogLog(), mozilla::LogLevel::Debug, args)
#define CSPORIGINLOGENABLED() \
  MOZ_LOG_TEST(GetCspOriginLogLog(), mozilla::LogLevel::Debug)

#ifdef DEBUG
/**
 * This function is only used for verification purposes within
 * GatherSecurityPolicyViolationEventData.
 */
static bool ValidateDirectiveName(const nsAString& aDirective) {
  static const auto directives = []() {
    std::unordered_set<std::string> directives;
    constexpr size_t dirLen =
        sizeof(CSPStrDirectives) / sizeof(CSPStrDirectives[0]);
    for (size_t i = 0; i < dirLen; ++i) {
      directives.insert(CSPStrDirectives[i]);
    }
    return directives;
  }();

  nsAutoString directive(aDirective);
  auto itr = directives.find(NS_ConvertUTF16toUTF8(directive).get());
  return itr != directives.end();
}
#endif  // DEBUG

static void BlockedContentSourceToString(
    CSPViolationData::BlockedContentSource aSource, nsACString& aString) {
  switch (aSource) {
    case CSPViolationData::BlockedContentSource::Unknown:
      aString.Truncate();
      break;

    case CSPViolationData::BlockedContentSource::Inline:
      aString.AssignLiteral("inline");
      break;

    case CSPViolationData::BlockedContentSource::Eval:
      aString.AssignLiteral("eval");
      break;

    case CSPViolationData::BlockedContentSource::Self:
      aString.AssignLiteral("self");
      break;

    case CSPViolationData::BlockedContentSource::WasmEval:
      aString.AssignLiteral("wasm-eval");
      break;
    case CSPViolationData::BlockedContentSource::TrustedTypesPolicy:
      aString.AssignLiteral("trusted-types-policy");
      break;
    case CSPViolationData::BlockedContentSource::TrustedTypesSink:
      aString.AssignLiteral("trusted-types-sink");
      break;
  }
}

/* =====  nsIContentSecurityPolicy impl ====== */

NS_IMETHODIMP
nsCSPContext::ShouldLoad(nsContentPolicyType aContentType,
                         nsICSPEventListener* aCSPEventListener,
                         nsILoadInfo* aLoadInfo, nsIURI* aContentLocation,
                         nsIURI* aOriginalURIIfRedirect,
                         bool aSendViolationReports, int16_t* outDecision) {
  if (CSPCONTEXTLOGENABLED()) {
    CSPCONTEXTLOG(("nsCSPContext::ShouldLoad, aContentLocation: %s",
                   aContentLocation->GetSpecOrDefault().get()));
    CSPCONTEXTLOG((">>>>                      aContentType: %s",
                   NS_CP_ContentTypeName(aContentType)));
  }

  // This ShouldLoad function is called from nsCSPService::ShouldLoad,
  // which already checked a number of things, including:
  // * aContentLocation is not null; we can consume this without further checks
  // * scheme is not a allowlisted scheme (about: chrome:, etc).
  // * CSP is enabled
  // * Content Type is not allowlisted (CSP Reports, TYPE_DOCUMENT, etc).
  // * Fast Path for Apps

  // Default decision, CSP can revise it if there's a policy to enforce
  *outDecision = nsIContentPolicy::ACCEPT;

  // If the content type doesn't map to a CSP directive, there's nothing for
  // CSP to do.
  CSPDirective dir = CSP_ContentTypeToDirective(aContentType);
  if (dir == nsIContentSecurityPolicy::NO_DIRECTIVE) {
    return NS_OK;
  }

  bool permitted = permitsInternal(
      dir,
      nullptr,  // aTriggeringElement
      aCSPEventListener, aLoadInfo, aContentLocation, aOriginalURIIfRedirect,
      false,  // allow fallback to default-src
      aSendViolationReports,
      true);  // send blocked URI in violation reports

  *outDecision =
      permitted ? nsIContentPolicy::ACCEPT : nsIContentPolicy::REJECT_SERVER;

  if (CSPCONTEXTLOGENABLED()) {
    CSPCONTEXTLOG(
        ("nsCSPContext::ShouldLoad, decision: %s, "
         "aContentLocation: %s",
         *outDecision > 0 ? "load" : "deny",
         aContentLocation->GetSpecOrDefault().get()));
  }
  return NS_OK;
}

bool nsCSPContext::permitsInternal(
    CSPDirective aDir, Element* aTriggeringElement,
    nsICSPEventListener* aCSPEventListener, nsILoadInfo* aLoadInfo,
    nsIURI* aContentLocation, nsIURI* aOriginalURIIfRedirect, bool aSpecific,
    bool aSendViolationReports, bool aSendContentLocationInViolationReports) {
  EnsureIPCPoliciesRead();
  bool permits = true;

  nsAutoString violatedDirective;
  nsAutoString violatedDirectiveString;
  for (uint32_t p = 0; p < mPolicies.Length(); p++) {
    if (!mPolicies[p]->permits(aDir, aLoadInfo, aContentLocation,
                               !!aOriginalURIIfRedirect, aSpecific,
                               violatedDirective, violatedDirectiveString)) {
      // If the policy is violated and not report-only, reject the load and
      // report to the console
      if (!mPolicies[p]->getReportOnlyFlag()) {
        CSPCONTEXTLOG(("nsCSPContext::permitsInternal, false"));
        permits = false;
      }

      // Callers should set |aSendViolationReports| to false if this is a
      // preload - the decision may be wrong due to the inability to get the
      // nonce, and will incorrectly fail the unit tests.
      if (aSendViolationReports) {
        auto loc = JSCallingLocation::Get();

        using Resource = CSPViolationData::Resource;
        Resource resource =
            aSendContentLocationInViolationReports
                ? Resource{nsCOMPtr<nsIURI>{aContentLocation}}
                : Resource{CSPViolationData::BlockedContentSource::Unknown};

        CSPViolationData cspViolationData{p,
                                          std::move(resource),
                                          aDir,
                                          loc.FileName(),
                                          loc.mLine,
                                          loc.mColumn,
                                          aTriggeringElement,
                                          /* aSample */ u""_ns};

        AsyncReportViolation(
            aCSPEventListener, std::move(cspViolationData),
            aOriginalURIIfRedirect, /* in case of redirect originalURI is not
                                       null */
            violatedDirective, violatedDirectiveString,
            u""_ns,  // no observer subject
            false);  // aReportSample (no sample)
      }
    }
  }

  return permits;
}

/* ===== nsISupports implementation ========== */

NS_IMPL_CLASSINFO(nsCSPContext, nullptr, 0, NS_CSPCONTEXT_CID)

NS_IMPL_ISUPPORTS_CI(nsCSPContext, nsIContentSecurityPolicy, nsISerializable)

nsCSPContext::nsCSPContext()
    : mInnerWindowID(0),
      mSkipAllowInlineStyleCheck(false),
      mLoadingContext(nullptr),
      mLoadingPrincipal(nullptr),
      mQueueUpMessages(true) {
  CSPCONTEXTLOG(("nsCSPContext::nsCSPContext"));
}

nsCSPContext::~nsCSPContext() {
  CSPCONTEXTLOG(("nsCSPContext::~nsCSPContext"));
  for (uint32_t i = 0; i < mPolicies.Length(); i++) {
    delete mPolicies[i];
  }
}

/* static */
bool nsCSPContext::Equals(nsIContentSecurityPolicy* aCSP,
                          nsIContentSecurityPolicy* aOtherCSP) {
  if (aCSP == aOtherCSP) {
    // fast path for pointer equality
    return true;
  }

  uint32_t policyCount = 0;
  if (aCSP) {
    aCSP->GetPolicyCount(&policyCount);
  }

  uint32_t otherPolicyCount = 0;
  if (aOtherCSP) {
    aOtherCSP->GetPolicyCount(&otherPolicyCount);
  }

  if (policyCount != otherPolicyCount) {
    return false;
  }

  nsAutoString policyStr, otherPolicyStr;
  for (uint32_t i = 0; i < policyCount; ++i) {
    aCSP->GetPolicyString(i, policyStr);
    aOtherCSP->GetPolicyString(i, otherPolicyStr);
    if (!policyStr.Equals(otherPolicyStr)) {
      return false;
    }
  }

  return true;
}

nsresult nsCSPContext::InitFromOther(nsCSPContext* aOtherContext) {
  NS_ENSURE_ARG(aOtherContext);

  nsresult rv = NS_OK;
  nsCOMPtr<Document> doc = do_QueryReferent(aOtherContext->mLoadingContext);
  if (doc) {
    rv = SetRequestContextWithDocument(doc);
  } else {
    rv = SetRequestContextWithPrincipal(
        aOtherContext->mLoadingPrincipal, aOtherContext->mSelfURI,
        aOtherContext->mReferrer, aOtherContext->mInnerWindowID);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  mSkipAllowInlineStyleCheck = aOtherContext->mSkipAllowInlineStyleCheck;

  // This policy was already parsed somewhere else, don't emit parsing errors.
  mSuppressParserLogMessages = true;
  for (auto policy : aOtherContext->mPolicies) {
    nsAutoString policyStr;
    policy->toString(policyStr);
    AppendPolicy(policyStr, policy->getReportOnlyFlag(),
                 policy->getDeliveredViaMetaTagFlag());
  }

  mSuppressParserLogMessages = aOtherContext->mSuppressParserLogMessages;

  mIPCPolicies = aOtherContext->mIPCPolicies.Clone();
  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::EnsureIPCPoliciesRead() {
  // Most likely the parser errors already happened before serializing
  // the policy for IPC.
  bool previous = mSuppressParserLogMessages;
  mSuppressParserLogMessages = true;

  if (mIPCPolicies.Length() > 0) {
    nsresult rv;
    for (auto& policy : mIPCPolicies) {
      rv = AppendPolicy(policy.policy(), policy.reportOnlyFlag(),
                        policy.deliveredViaMetaTagFlag());
      Unused << NS_WARN_IF(NS_FAILED(rv));
    }
    mIPCPolicies.Clear();
  }

  mSuppressParserLogMessages = previous;
  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::GetPolicyString(uint32_t aIndex, nsAString& outStr) {
  outStr.Truncate();
  EnsureIPCPoliciesRead();
  if (aIndex >= mPolicies.Length()) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  mPolicies[aIndex]->toString(outStr);
  return NS_OK;
}

const nsCSPPolicy* nsCSPContext::GetPolicy(uint32_t aIndex) {
  EnsureIPCPoliciesRead();
  if (aIndex >= mPolicies.Length()) {
    return nullptr;
  }
  return mPolicies[aIndex];
}

NS_IMETHODIMP
nsCSPContext::GetPolicyCount(uint32_t* outPolicyCount) {
  EnsureIPCPoliciesRead();
  *outPolicyCount = mPolicies.Length();
  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::GetUpgradeInsecureRequests(bool* outUpgradeRequest) {
  EnsureIPCPoliciesRead();
  *outUpgradeRequest = false;
  for (uint32_t i = 0; i < mPolicies.Length(); i++) {
    if (mPolicies[i]->hasDirective(
            nsIContentSecurityPolicy::UPGRADE_IF_INSECURE_DIRECTIVE) &&
        !mPolicies[i]->getReportOnlyFlag()) {
      *outUpgradeRequest = true;
      return NS_OK;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::GetBlockAllMixedContent(bool* outBlockAllMixedContent) {
  EnsureIPCPoliciesRead();
  *outBlockAllMixedContent = false;
  for (uint32_t i = 0; i < mPolicies.Length(); i++) {
    if (!mPolicies[i]->getReportOnlyFlag() &&
        mPolicies[i]->hasDirective(
            nsIContentSecurityPolicy::BLOCK_ALL_MIXED_CONTENT)) {
      *outBlockAllMixedContent = true;
      return NS_OK;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::GetEnforcesFrameAncestors(bool* outEnforcesFrameAncestors) {
  EnsureIPCPoliciesRead();
  *outEnforcesFrameAncestors = false;
  for (uint32_t i = 0; i < mPolicies.Length(); i++) {
    if (!mPolicies[i]->getReportOnlyFlag() &&
        mPolicies[i]->hasDirective(
            nsIContentSecurityPolicy::FRAME_ANCESTORS_DIRECTIVE)) {
      *outEnforcesFrameAncestors = true;
      return NS_OK;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::AppendPolicy(const nsAString& aPolicyString, bool aReportOnly,
                           bool aDeliveredViaMetaTag) {
  CSPCONTEXTLOG(("nsCSPContext::AppendPolicy: %s",
                 NS_ConvertUTF16toUTF8(aPolicyString).get()));

  // Use mSelfURI from setRequestContextWith{Document,Principal} (bug 991474)
  MOZ_ASSERT(
      mLoadingPrincipal,
      "did you forget to call setRequestContextWith{Document,Principal}?");
  MOZ_ASSERT(
      mSelfURI,
      "did you forget to call setRequestContextWith{Document,Principal}?");
  NS_ENSURE_TRUE(mLoadingPrincipal, NS_ERROR_UNEXPECTED);
  NS_ENSURE_TRUE(mSelfURI, NS_ERROR_UNEXPECTED);

  if (CSPORIGINLOGENABLED()) {
    nsAutoCString selfURISpec;
    mSelfURI->GetSpec(selfURISpec);
    CSPORIGINLOG(("CSP - AppendPolicy"));
    CSPORIGINLOG((" * selfURI: %s", selfURISpec.get()));
    CSPORIGINLOG((" * reportOnly: %s", aReportOnly ? "yes" : "no"));
    CSPORIGINLOG(
        (" * deliveredViaMetaTag: %s", aDeliveredViaMetaTag ? "yes" : "no"));
    CSPORIGINLOG(
        (" * policy: %s\n", NS_ConvertUTF16toUTF8(aPolicyString).get()));
  }

  nsCSPPolicy* policy = nsCSPParser::parseContentSecurityPolicy(
      aPolicyString, mSelfURI, aReportOnly, this, aDeliveredViaMetaTag,
      mSuppressParserLogMessages);
  if (policy) {
    if (policy->hasDirective(
            nsIContentSecurityPolicy::UPGRADE_IF_INSECURE_DIRECTIVE)) {
      nsAutoCString selfURIspec;
      if (mSelfURI) {
        mSelfURI->GetAsciiSpec(selfURIspec);
      }
      CSPCONTEXTLOG(
          ("nsCSPContext::AppendPolicy added UPGRADE_IF_INSECURE_DIRECTIVE "
           "self-uri=%s referrer=%s",
           selfURIspec.get(), mReferrer.get()));
    }
    if (policy->hasDirective(
            nsIContentSecurityPolicy::REQUIRE_TRUSTED_TYPES_FOR_DIRECTIVE)) {
      if (mRequireTrustedTypesForDirectiveState !=
          RequireTrustedTypesForDirectiveState::ENFORCE) {
        mRequireTrustedTypesForDirectiveState =
            policy->getReportOnlyFlag()
                ? RequireTrustedTypesForDirectiveState::REPORT_ONLY
                : RequireTrustedTypesForDirectiveState::ENFORCE;
      }
      if (nsCOMPtr<Document> doc = do_QueryReferent(mLoadingContext)) {
        doc->SetHasPolicyWithRequireTrustedTypesForDirective(true);
      }
    }

    mPolicies.AppendElement(policy);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::GetRequireTrustedTypesForDirectiveState(
    RequireTrustedTypesForDirectiveState*
        aRequireTrustedTypesForDirectiveState) {
  *aRequireTrustedTypesForDirectiveState =
      mRequireTrustedTypesForDirectiveState;
  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::GetAllowsEval(bool* outShouldReportViolation,
                            bool* outAllowsEval) {
  EnsureIPCPoliciesRead();
  *outShouldReportViolation = false;
  *outAllowsEval = true;

  if (CSP_IsBrowserXHTML(mSelfURI)) {
    // Allow eval in browser.xhtml, just like
    // nsContentSecurityUtils::IsEvalAllowed allows it for other privileged
    // contexts.
    if (StaticPrefs::
            security_allow_unsafe_dangerous_privileged_evil_eval_AtStartup()) {
      return NS_OK;
    }
  }

  bool trustedTypesRequired = (mRequireTrustedTypesForDirectiveState ==
                               RequireTrustedTypesForDirectiveState::ENFORCE);

  for (uint32_t i = 0; i < mPolicies.Length(); i++) {
    if (!(trustedTypesRequired &&
          mPolicies[i]->allows(SCRIPT_SRC_DIRECTIVE, CSP_TRUSTED_TYPES_EVAL,
                               u""_ns)) &&
        !mPolicies[i]->allows(SCRIPT_SRC_DIRECTIVE, CSP_UNSAFE_EVAL, u""_ns)) {
      // policy is violated: must report the violation and allow the inline
      // script if the policy is report-only.
      *outShouldReportViolation = true;
      if (!mPolicies[i]->getReportOnlyFlag()) {
        *outAllowsEval = false;
      }
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::GetAllowsWasmEval(bool* outShouldReportViolation,
                                bool* outAllowsWasmEval) {
  EnsureIPCPoliciesRead();
  *outShouldReportViolation = false;
  *outAllowsWasmEval = true;

  for (uint32_t i = 0; i < mPolicies.Length(); i++) {
    // Either 'unsafe-eval' or 'wasm-unsafe-eval' can allow this
    if (!mPolicies[i]->allows(SCRIPT_SRC_DIRECTIVE, CSP_WASM_UNSAFE_EVAL,
                              u""_ns) &&
        !mPolicies[i]->allows(SCRIPT_SRC_DIRECTIVE, CSP_UNSAFE_EVAL, u""_ns)) {
      // policy is violated: must report the violation and allow the inline
      // script if the policy is report-only.
      *outShouldReportViolation = true;
      if (!mPolicies[i]->getReportOnlyFlag()) {
        *outAllowsWasmEval = false;
      }
    }
  }

  return NS_OK;
}

// Helper function to report inline violations
void nsCSPContext::ReportInlineViolation(
    CSPDirective aDirective, Element* aTriggeringElement,
    nsICSPEventListener* aCSPEventListener, const nsAString& aNonce,
    bool aReportSample, const nsAString& aSourceCode,
    const nsAString& aViolatedDirective,
    const nsAString& aViolatedDirectiveString, CSPDirective aEffectiveDirective,
    uint32_t aViolatedPolicyIndex,  // TODO, use report only flag for that
    uint32_t aLineNumber, uint32_t aColumnNumber) {
  nsString observerSubject;
  // if the nonce is non empty, then we report the nonce error, otherwise
  // let's report the hash error; no need to report the unsafe-inline error
  // anymore.
  if (!aNonce.IsEmpty()) {
    observerSubject = (aDirective == SCRIPT_SRC_ELEM_DIRECTIVE ||
                       aDirective == SCRIPT_SRC_ATTR_DIRECTIVE)
                          ? NS_LITERAL_STRING_FROM_CSTRING(
                                SCRIPT_NONCE_VIOLATION_OBSERVER_TOPIC)
                          : NS_LITERAL_STRING_FROM_CSTRING(
                                STYLE_NONCE_VIOLATION_OBSERVER_TOPIC);
  } else {
    observerSubject = (aDirective == SCRIPT_SRC_ELEM_DIRECTIVE ||
                       aDirective == SCRIPT_SRC_ATTR_DIRECTIVE)
                          ? NS_LITERAL_STRING_FROM_CSTRING(
                                SCRIPT_HASH_VIOLATION_OBSERVER_TOPIC)
                          : NS_LITERAL_STRING_FROM_CSTRING(
                                STYLE_HASH_VIOLATION_OBSERVER_TOPIC);
  }

  auto loc = JSCallingLocation::Get();
  if (!loc) {
    nsCString sourceFile;
    // use selfURI as the source
    if (mSelfURI) {
      mSelfURI->GetSpec(sourceFile);
      loc.mResource = AsVariant(std::move(sourceFile));
    }
    loc.mLine = aLineNumber;
    loc.mColumn = aColumnNumber;
  }

  nsAutoCString hashSHA256;
  // We optionally include the hash to create more helpful error messages.
  nsCOMPtr<nsICryptoHash> hasher;
  if (NS_SUCCEEDED(
          NS_NewCryptoHash(nsICryptoHash::SHA256, getter_AddRefs(hasher)))) {
    NS_ConvertUTF16toUTF8 source(aSourceCode);
    if (NS_SUCCEEDED(hasher->Update(
            reinterpret_cast<const uint8_t*>(source.get()), source.Length()))) {
      (void)hasher->Finish(true, hashSHA256);
    }
  }

  CSPViolationData cspViolationData{
      aViolatedPolicyIndex,
      CSPViolationData::Resource{
          CSPViolationData::BlockedContentSource::Inline},
      aEffectiveDirective,
      loc.FileName(),
      loc.mLine,
      loc.mColumn,
      aTriggeringElement,
      aSourceCode,
      hashSHA256};

  AsyncReportViolation(aCSPEventListener, std::move(cspViolationData),
                       mSelfURI,            // aOriginalURI
                       aViolatedDirective,  // aViolatedDirective
                       aViolatedDirectiveString,
                       observerSubject,  // aObserverSubject
                       aReportSample);   // aReportSample
}

NS_IMETHODIMP
nsCSPContext::GetAllowsInline(CSPDirective aDirective, bool aHasUnsafeHash,
                              const nsAString& aNonce, bool aParserCreated,
                              Element* aTriggeringElement,
                              nsICSPEventListener* aCSPEventListener,
                              const nsAString& aSourceText,
                              uint32_t aLineNumber, uint32_t aColumnNumber,
                              bool* outAllowsInline) {
  *outAllowsInline = true;

  if (aDirective != SCRIPT_SRC_ELEM_DIRECTIVE &&
      aDirective != SCRIPT_SRC_ATTR_DIRECTIVE &&
      aDirective != STYLE_SRC_ELEM_DIRECTIVE &&
      aDirective != STYLE_SRC_ATTR_DIRECTIVE) {
    MOZ_ASSERT(false,
               "can only allow inline for (script/style)-src-(attr/elem)");
    return NS_OK;
  }

  EnsureIPCPoliciesRead();
  nsAutoString content;

  // always iterate all policies, otherwise we might not send out all reports
  for (uint32_t i = 0; i < mPolicies.Length(); i++) {
    // https://w3c.github.io/webappsec-csp/#match-element-to-source-list

    // Step 1. If §6.7.3.2 Does a source list allow all inline behavior for
    // type? returns "Allows" given list and type, return "Matches".
    if (mPolicies[i]->allowsAllInlineBehavior(aDirective)) {
      continue;
    }

    // Step 2. If type is "script" or "style", and §6.7.3.1 Is element
    // nonceable? returns "Nonceable" when executed upon element:
    if ((aDirective == SCRIPT_SRC_ELEM_DIRECTIVE ||
         aDirective == STYLE_SRC_ELEM_DIRECTIVE) &&
        aTriggeringElement && !aNonce.IsEmpty()) {
#ifdef DEBUG
      // NOTE: Folllowing Chrome "Is element nonceable?" doesn't apply to
      // <style>.
      if (aDirective == SCRIPT_SRC_ELEM_DIRECTIVE) {
        // Our callers should have checked this.
        MOZ_ASSERT(nsContentSecurityUtils::GetIsElementNonceableNonce(
                       *aTriggeringElement) == aNonce);
      }
#endif

      // Step 2.1. For each expression of list: [...]
      if (mPolicies[i]->allows(aDirective, CSP_NONCE, aNonce)) {
        continue;
      }
    }

    // Check the content length to ensure the content is not allocated more than
    // once. Even though we are in a for loop, it is probable that there is only
    // one policy, so this check may be unnecessary.
    if (content.IsEmpty()) {
      if (aSourceText.IsVoid()) {
        // Lazily retrieve the text of inline script, see bug 1376651.
        nsCOMPtr<nsIScriptElement> element =
            do_QueryInterface(aTriggeringElement);
        MOZ_ASSERT(element);
        element->GetScriptText(content);
      } else {
        content = aSourceText;
      }
    }

    // Step 3. Let unsafe-hashes flag be false.
    // Step 4. For each expression of list: [...]
    bool unsafeHashesFlag =
        mPolicies[i]->allows(aDirective, CSP_UNSAFE_HASHES, u""_ns);

    // Step 5. If type is "script" or "style", or unsafe-hashes flag is true:
    //
    // aHasUnsafeHash is true for event handlers (type "script attribute"),
    // style= attributes (type "style attribute") and the javascript: protocol.
    if (!aHasUnsafeHash || unsafeHashesFlag) {
      if (mPolicies[i]->allows(aDirective, CSP_HASH, content)) {
        continue;
      }
    }

    // TODO(Bug 1844290): Figure out how/if strict-dynamic for inline scripts is
    // specified
    bool allowed = false;
    if ((aDirective == SCRIPT_SRC_ELEM_DIRECTIVE ||
         aDirective == SCRIPT_SRC_ATTR_DIRECTIVE) &&
        mPolicies[i]->allows(aDirective, CSP_STRICT_DYNAMIC, u""_ns)) {
      allowed = !aParserCreated;
    }

    if (!allowed) {
      // policy is violoated: deny the load unless policy is report only and
      // report the violation.
      if (!mPolicies[i]->getReportOnlyFlag()) {
        *outAllowsInline = false;
      }
      nsAutoString violatedDirective;
      nsAutoString violatedDirectiveString;
      bool reportSample = false;
      mPolicies[i]->getViolatedDirectiveInformation(
          aDirective, violatedDirective, violatedDirectiveString,
          &reportSample);

      ReportInlineViolation(aDirective, aTriggeringElement, aCSPEventListener,
                            aNonce, reportSample, content, violatedDirective,
                            violatedDirectiveString, aDirective, i, aLineNumber,
                            aColumnNumber);
    }
  }

  return NS_OK;
}

/**
 * For each policy, log any violation on the Error Console and send a report
 * if a report-uri is present in the policy
 *
 * @param aViolationType
 *     one of the VIOLATION_TYPE_* constants, e.g. inline-script or eval
 * @param aSourceFile
 *     name of the source file containing the violation (if available)
 * @param aContentSample
 *     sample of the violating content (to aid debugging)
 * @param aLineNum
 *     source line number of the violation (if available)
 * @param aColumnNum
 *     source column number of the violation (if available)
 * @param aNonce
 *     (optional) If this is a nonce violation, include the nonce so we can
 *     recheck to determine which policies were violated and send the
 *     appropriate reports.
 * @param aContent
 *     (optional) If this is a hash violation, include contents of the inline
 *     resource in the question so we can recheck the hash in order to
 *     determine which policies were violated and send the appropriate
 *     reports.
 */
NS_IMETHODIMP
nsCSPContext::LogViolationDetails(
    uint16_t aViolationType, Element* aTriggeringElement,
    nsICSPEventListener* aCSPEventListener, const nsACString& aSourceFile,
    const nsAString& aScriptSample, int32_t aLineNum, int32_t aColumnNum,
    const nsAString& aNonce, const nsAString& aContent) {
  EnsureIPCPoliciesRead();

  CSPViolationData::BlockedContentSource blockedContentSource;
  enum CSPKeyword keyword;
  nsAutoString observerSubject;
  if (aViolationType == nsIContentSecurityPolicy::VIOLATION_TYPE_EVAL) {
    blockedContentSource = CSPViolationData::BlockedContentSource::Eval;
    keyword = CSP_UNSAFE_EVAL;
    observerSubject.AssignLiteral(EVAL_VIOLATION_OBSERVER_TOPIC);
  } else {
    NS_ASSERTION(
        aViolationType == nsIContentSecurityPolicy::VIOLATION_TYPE_WASM_EVAL,
        "unexpected aViolationType");
    blockedContentSource = CSPViolationData::BlockedContentSource::WasmEval;
    keyword = CSP_WASM_UNSAFE_EVAL;
    observerSubject.AssignLiteral(WASM_EVAL_VIOLATION_OBSERVER_TOPIC);
  }

  for (uint32_t p = 0; p < mPolicies.Length(); p++) {
    NS_ASSERTION(mPolicies[p], "null pointer in nsTArray<nsCSPPolicy>");

    if (mPolicies[p]->allows(SCRIPT_SRC_DIRECTIVE, keyword, u""_ns)) {
      continue;
    }

    CSPViolationData cspViolationData{
        p,
        CSPViolationData::Resource{blockedContentSource},
        /* aEffectiveDirective */ CSPDirective::SCRIPT_SRC_DIRECTIVE,
        aSourceFile,
        static_cast<uint32_t>(aLineNum),
        static_cast<uint32_t>(aColumnNum),
        aTriggeringElement,
        aScriptSample};

    LogViolationDetailsUnchecked(aCSPEventListener, std::move(cspViolationData),
                                 observerSubject, ForceReportSample::No);
  }
  return NS_OK;
}

void nsCSPContext::LogViolationDetailsUnchecked(
    nsICSPEventListener* aCSPEventListener,
    mozilla::dom::CSPViolationData&& aCSPViolationData,
    const nsAString& aObserverSubject, ForceReportSample aForceReportSample) {
  EnsureIPCPoliciesRead();

  nsAutoString violatedDirectiveName;
  nsAutoString violatedDirectiveNameAndValue;
  bool reportSample = false;
  mPolicies[aCSPViolationData.mViolatedPolicyIndex]
      ->getViolatedDirectiveInformation(
          aCSPViolationData.mEffectiveDirective, violatedDirectiveName,
          violatedDirectiveNameAndValue, &reportSample);

  if (aForceReportSample == ForceReportSample::Yes) {
    reportSample = true;
  }

  AsyncReportViolation(aCSPEventListener, std::move(aCSPViolationData), nullptr,
                       violatedDirectiveName, violatedDirectiveNameAndValue,
                       aObserverSubject, reportSample);
}

NS_IMETHODIMP nsCSPContext::LogTrustedTypesViolationDetailsUnchecked(
    CSPViolationData&& aCSPViolationData, const nsAString& aObserverSubject,
    nsICSPEventListener* aCSPEventListener) {
  EnsureIPCPoliciesRead();

  // Trusted types don't support the "report-sample" keyword
  // (https://github.com/w3c/trusted-types/issues/531#issuecomment-2194166146).
  LogViolationDetailsUnchecked(aCSPEventListener, std::move(aCSPViolationData),
                               aObserverSubject, ForceReportSample::Yes);
  return NS_OK;
}

#undef CASE_CHECK_AND_REPORT

NS_IMETHODIMP
nsCSPContext::SetRequestContextWithDocument(Document* aDocument) {
  MOZ_ASSERT(aDocument, "Can't set context without doc");
  NS_ENSURE_ARG(aDocument);

  mLoadingContext = do_GetWeakReference(aDocument);
  mSelfURI = aDocument->GetDocumentURI();
  mLoadingPrincipal = aDocument->NodePrincipal();
  aDocument->GetReferrer(mReferrer);
  mInnerWindowID = aDocument->InnerWindowID();
  // the innerWindowID is not available for CSPs delivered through the
  // header at the time setReqeustContext is called - let's queue up
  // console messages until it becomes available, see flushConsoleMessages
  mQueueUpMessages = !mInnerWindowID;
  mCallingChannelLoadGroup = aDocument->GetDocumentLoadGroup();
  // set the flag on the document for CSP telemetry
  mEventTarget = GetMainThreadSerialEventTarget();

  MOZ_ASSERT(mLoadingPrincipal, "need a valid requestPrincipal");
  MOZ_ASSERT(mSelfURI, "need mSelfURI to translate 'self' into actual URI");
  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::SetRequestContextWithPrincipal(nsIPrincipal* aRequestPrincipal,
                                             nsIURI* aSelfURI,
                                             const nsACString& aReferrer,
                                             uint64_t aInnerWindowId) {
  NS_ENSURE_ARG(aRequestPrincipal);

  mLoadingPrincipal = aRequestPrincipal;
  mSelfURI = aSelfURI;
  mReferrer = aReferrer;
  mInnerWindowID = aInnerWindowId;
  // if no document is available, then it also does not make sense to queue
  // console messages sending messages to the browser console instead of the web
  // console in that case.
  mQueueUpMessages = false;
  mCallingChannelLoadGroup = nullptr;
  mEventTarget = nullptr;

  MOZ_ASSERT(mLoadingPrincipal, "need a valid requestPrincipal");
  MOZ_ASSERT(mSelfURI, "need mSelfURI to translate 'self' into actual URI");
  return NS_OK;
}

nsIPrincipal* nsCSPContext::GetRequestPrincipal() { return mLoadingPrincipal; }

nsIURI* nsCSPContext::GetSelfURI() { return mSelfURI; }

NS_IMETHODIMP
nsCSPContext::GetReferrer(nsACString& outReferrer) {
  outReferrer.Assign(mReferrer);
  return NS_OK;
}

uint64_t nsCSPContext::GetInnerWindowID() { return mInnerWindowID; }

bool nsCSPContext::GetSkipAllowInlineStyleCheck() {
  return mSkipAllowInlineStyleCheck;
}

void nsCSPContext::SetSkipAllowInlineStyleCheck(
    bool aSkipAllowInlineStyleCheck) {
  mSkipAllowInlineStyleCheck = aSkipAllowInlineStyleCheck;
}

NS_IMETHODIMP
nsCSPContext::EnsureEventTarget(nsIEventTarget* aEventTarget) {
  NS_ENSURE_ARG(aEventTarget);
  // Don't bother if we did have a valid event target (if the csp object is
  // tied to a document in SetRequestContextWithDocument)
  if (mEventTarget) {
    return NS_OK;
  }

  mEventTarget = aEventTarget;
  return NS_OK;
}

struct ConsoleMsgQueueElem {
  nsString mMsg;
  nsCString mSourceName;
  nsString mSourceLine;
  uint32_t mLineNumber;
  uint32_t mColumnNumber;
  uint32_t mSeverityFlag;
  nsCString mCategory;
};

void nsCSPContext::flushConsoleMessages() {
  bool privateWindow = false;

  // should flush messages even if doc is not available
  nsCOMPtr<Document> doc = do_QueryReferent(mLoadingContext);
  if (doc) {
    mInnerWindowID = doc->InnerWindowID();
    privateWindow =
        doc->NodePrincipal()->OriginAttributesRef().IsPrivateBrowsing();
  }

  mQueueUpMessages = false;

  for (uint32_t i = 0; i < mConsoleMsgQueue.Length(); i++) {
    ConsoleMsgQueueElem& elem = mConsoleMsgQueue[i];
    CSP_LogMessage(elem.mMsg, elem.mSourceName, elem.mSourceLine,
                   elem.mLineNumber, elem.mColumnNumber, elem.mSeverityFlag,
                   elem.mCategory, mInnerWindowID, privateWindow);
  }
  mConsoleMsgQueue.Clear();
}

void nsCSPContext::logToConsole(const char* aName,
                                const nsTArray<nsString>& aParams,
                                const nsACString& aSourceName,
                                const nsAString& aSourceLine,
                                uint32_t aLineNumber, uint32_t aColumnNumber,
                                uint32_t aSeverityFlag) {
  // we are passing aName as the category so we can link to the
  // appropriate MDN docs depending on the specific error.
  nsDependentCString category(aName);

  // Fallback
  nsAutoCString spec;
  if (aSourceName.IsEmpty() && mSelfURI) {
    mSelfURI->GetSpec(spec);
  }

  const auto& sourceName = aSourceName.IsEmpty() ? spec : aSourceName;

  // let's check if we have to queue up console messages
  if (mQueueUpMessages) {
    nsAutoString msg;
    CSP_GetLocalizedStr(aName, aParams, msg);
    ConsoleMsgQueueElem& elem = *mConsoleMsgQueue.AppendElement();
    elem.mMsg = msg;
    elem.mSourceName = sourceName;
    elem.mSourceLine = PromiseFlatString(aSourceLine);
    elem.mLineNumber = aLineNumber;
    elem.mColumnNumber = aColumnNumber;
    elem.mSeverityFlag = aSeverityFlag;
    elem.mCategory = category;
    return;
  }

  bool privateWindow = false;
  if (nsCOMPtr<Document> doc = do_QueryReferent(mLoadingContext)) {
    privateWindow =
        doc->NodePrincipal()->OriginAttributesRef().IsPrivateBrowsing();
  }

  CSP_LogLocalizedStr(aName, aParams, sourceName, aSourceLine, aLineNumber,
                      aColumnNumber, aSeverityFlag, category, mInnerWindowID,
                      privateWindow);
}

/**
 * Strip URI for reporting according to:
 * https://w3c.github.io/webappsec-csp/#strip-url-for-use-in-reports
 *
 * @param aSelfURI
 *        The URI of the CSP policy. Used for cross-origin checks.
 * @param aURI
 *        The URI of the blocked resource. In case of a redirect, this it the
 *        initial URI the request started out with, not the redirected URI.
 * @param aEffectiveDirective
 *        The effective directive that triggered this report
 * @return The ASCII serialization of the uri to be reported ignoring
 *         the ref part of the URI.
 */
void StripURIForReporting(nsIURI* aSelfURI, nsIURI* aURI,
                          const nsAString& aEffectiveDirective,
                          nsACString& outStrippedURI) {
  // Non-standard: For reports going to internal chrome: documents include the
  // whole URI.
  if (aSelfURI->SchemeIs("chrome")) {
    aURI->GetSpecIgnoringRef(outStrippedURI);
    return;
  }

  // Step 1. If url’s scheme is not an HTTP(S) scheme, then return url’s scheme.
  // https://github.com/w3c/webappsec-csp/issues/735: We also allow WS(S)
  // schemes.
  if (!net::SchemeIsHttpOrHttps(aURI) &&
      !(aURI->SchemeIs("ws") || aURI->SchemeIs("wss"))) {
    aURI->GetScheme(outStrippedURI);
    return;
  }

  // Step 2. Set url’s fragment to the empty string.
  // Step 3. Set url’s username to the empty string.
  // Step 3. Set url’s password to the empty string.
  nsCOMPtr<nsIURI> stripped;
  if (NS_FAILED(NS_MutateURI(aURI).SetRef(""_ns).SetUserPass(""_ns).Finalize(
          stripped))) {
    // Mutating the URI failed for some reason, just return the scheme.
    aURI->GetScheme(outStrippedURI);
    return;
  }

  // Non-standard: https://github.com/w3c/webappsec-csp/issues/735
  // For cross-origin URIs in frame-src also strip the path.
  // This prevents detailed tracking of pages loaded into an iframe
  // by the embedding page using a report-only policy.
  if (aEffectiveDirective.EqualsLiteral("frame-src") ||
      aEffectiveDirective.EqualsLiteral("object-src")) {
    nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
    if (NS_FAILED(ssm->CheckSameOriginURI(aSelfURI, stripped, false, false))) {
      stripped->GetPrePath(outStrippedURI);
      return;
    }
  }

  // Step 4. Return the result of executing the URL serializer on url.
  stripped->GetSpec(outStrippedURI);
}

nsresult nsCSPContext::GatherSecurityPolicyViolationEventData(
    nsIURI* aOriginalURI, const nsAString& aEffectiveDirective,
    const mozilla::dom::CSPViolationData& aCSPViolationData, bool aReportSample,
    mozilla::dom::SecurityPolicyViolationEventInit& aViolationEventInit) {
  EnsureIPCPoliciesRead();
  NS_ENSURE_ARG_MAX(aCSPViolationData.mViolatedPolicyIndex,
                    mPolicies.Length() - 1);

  MOZ_ASSERT(ValidateDirectiveName(aEffectiveDirective),
             "Invalid directive name");

  nsresult rv;

  // document-uri
  nsAutoCString reportDocumentURI;
  StripURIForReporting(mSelfURI, mSelfURI, aEffectiveDirective,
                       reportDocumentURI);
  CopyUTF8toUTF16(reportDocumentURI, aViolationEventInit.mDocumentURI);

  // referrer
  CopyUTF8toUTF16(mReferrer, aViolationEventInit.mReferrer);

  // blocked-uri
  // Corresponds to
  // <https://w3c.github.io/webappsec-csp/#obtain-violation-blocked-uri>.
  if (aCSPViolationData.mResource.is<nsCOMPtr<nsIURI>>()) {
    nsAutoCString reportBlockedURI;
    StripURIForReporting(
        mSelfURI,
        aOriginalURI ? aOriginalURI
                     : aCSPViolationData.mResource.as<nsCOMPtr<nsIURI>>().get(),
        aEffectiveDirective, reportBlockedURI);
    CopyUTF8toUTF16(reportBlockedURI, aViolationEventInit.mBlockedURI);
  } else {
    nsAutoCString blockedContentSource;
    BlockedContentSourceToString(
        aCSPViolationData.mResource
            .as<CSPViolationData::BlockedContentSource>(),
        blockedContentSource);
    CopyUTF8toUTF16(blockedContentSource, aViolationEventInit.mBlockedURI);
  }

  // effective-directive
  // The name of the policy directive that was violated.
  aViolationEventInit.mEffectiveDirective = aEffectiveDirective;

  // violated-directive
  // In CSP2, the policy directive that was violated, as it appears in the
  // policy. In CSP3, the same as effective-directive.
  aViolationEventInit.mViolatedDirective = aEffectiveDirective;

  // original-policy
  nsAutoString originalPolicy;
  rv = this->GetPolicyString(aCSPViolationData.mViolatedPolicyIndex,
                             originalPolicy);
  NS_ENSURE_SUCCESS(rv, rv);
  aViolationEventInit.mOriginalPolicy = originalPolicy;

  // source-file
  if (!aCSPViolationData.mSourceFile.IsEmpty()) {
    // if aSourceFile is a URI, we have to make sure to strip fragments
    nsCOMPtr<nsIURI> sourceURI;
    NS_NewURI(getter_AddRefs(sourceURI), aCSPViolationData.mSourceFile);
    if (sourceURI) {
      nsAutoCString stripped;
      StripURIForReporting(mSelfURI, sourceURI, aEffectiveDirective, stripped);
      CopyUTF8toUTF16(stripped, aViolationEventInit.mSourceFile);
    } else {
      CopyUTF8toUTF16(aCSPViolationData.mSourceFile,
                      aViolationEventInit.mSourceFile);
    }
  }

  // sample (already truncated)
  aViolationEventInit.mSample =
      aReportSample ? aCSPViolationData.mSample : EmptyString();

  // disposition
  aViolationEventInit.mDisposition =
      mPolicies[aCSPViolationData.mViolatedPolicyIndex]->getReportOnlyFlag()
          ? mozilla::dom::SecurityPolicyViolationEventDisposition::Report
          : mozilla::dom::SecurityPolicyViolationEventDisposition::Enforce;

  // status-code
  uint16_t statusCode = 0;
  {
    nsCOMPtr<Document> doc = do_QueryReferent(mLoadingContext);
    if (doc) {
      nsCOMPtr<nsIHttpChannel> channel = do_QueryInterface(doc->GetChannel());
      if (channel) {
        uint32_t responseStatus = 0;
        nsresult rv = channel->GetResponseStatus(&responseStatus);
        if (NS_SUCCEEDED(rv) && (responseStatus <= UINT16_MAX)) {
          statusCode = static_cast<uint16_t>(responseStatus);
        }
      }
    }
  }
  aViolationEventInit.mStatusCode = statusCode;

  // line-number
  aViolationEventInit.mLineNumber = aCSPViolationData.mLineNumber;

  // column-number
  aViolationEventInit.mColumnNumber = aCSPViolationData.mColumnNumber;

  aViolationEventInit.mBubbles = true;
  aViolationEventInit.mComposed = true;

  return NS_OK;
}

bool nsCSPContext::ShouldThrottleReport(
    const mozilla::dom::SecurityPolicyViolationEventInit& aViolationEventInit) {
  // Fetch the rate limit preference.
  const uint32_t kLimitCount =
      StaticPrefs::security_csp_reporting_limit_count();

  // Disable throttling if the preference is set to 0.
  if (kLimitCount == 0) {
    return false;
  }

  const uint32_t kTimeSpanSeconds = 2;
  TimeDuration throttleSpan = TimeDuration::FromSeconds(kTimeSpanSeconds);
  if (mSendReportLimitSpanStart.IsNull() ||
      ((TimeStamp::Now() - mSendReportLimitSpanStart) > throttleSpan)) {
    // Initial call or timespan exceeded, reset counter and timespan.
    mSendReportLimitSpanStart = TimeStamp::Now();
    mSendReportLimitCount = 1;
    // Also make sure we warn about omitted messages. (XXX or only do this once
    // per context?)
    mWarnedAboutTooManyReports = false;
    return false;
  }

  if (mSendReportLimitCount < kLimitCount) {
    mSendReportLimitCount++;
    return false;
  }

  // Rate limit reached
  if (!mWarnedAboutTooManyReports) {
    logToConsole("tooManyReports", {},
                 NS_ConvertUTF16toUTF8(aViolationEventInit.mSourceFile),
                 aViolationEventInit.mSample, aViolationEventInit.mLineNumber,
                 aViolationEventInit.mColumnNumber, nsIScriptError::errorFlag);
    mWarnedAboutTooManyReports = true;
  }
  return true;
}

nsresult nsCSPContext::SendReports(
    const mozilla::dom::SecurityPolicyViolationEventInit& aViolationEventInit,
    uint32_t aViolatedPolicyIndex) {
  EnsureIPCPoliciesRead();
  NS_ENSURE_ARG_MAX(aViolatedPolicyIndex, mPolicies.Length() - 1);

  if (!StaticPrefs::security_csp_reporting_enabled() ||
      ShouldThrottleReport(aViolationEventInit)) {
    return NS_OK;
  }

  nsAutoString reportGroup;
  mPolicies[aViolatedPolicyIndex]->getReportGroup(reportGroup);

  // CSP Level 3 Reporting
  if (StaticPrefs::dom_reporting_enabled() && !reportGroup.IsEmpty()) {
    return SendReportsToEndpoints(reportGroup, aViolationEventInit);
  }

  nsTArray<nsString> reportURIs;
  mPolicies[aViolatedPolicyIndex]->getReportURIs(reportURIs);

  // [Deprecated] CSP Level 2 Reporting
  if (!reportURIs.IsEmpty()) {
    return SendReportsToURIs(reportURIs, aViolationEventInit);
  }

  return NS_OK;
}

nsresult nsCSPContext::SendReportsToEndpoints(
    nsAutoString& reportGroup,
    const mozilla::dom::SecurityPolicyViolationEventInit& aViolationEventInit) {
  nsCOMPtr<Document> doc = do_QueryReferent(mLoadingContext);
  if (!doc) {
    return NS_ERROR_FAILURE;
  }
  nsPIDOMWindowInner* window = doc->GetInnerWindow();
  if (NS_WARN_IF(!window)) {
    return NS_ERROR_FAILURE;
  }

  CSPViolationReportBody body;
  body.mDocumentURL.Value().Assign(aViolationEventInit.mDocumentURI);
  body.mReferrer.Value().Assign(aViolationEventInit.mReferrer);
  body.mBlockedURL.Value().Assign(aViolationEventInit.mBlockedURI);
  body.mEffectiveDirective.Value().Assign(aViolationEventInit.mEffectiveDirective);
  body.mOriginalPolicy.Value().Assign(aViolationEventInit.mOriginalPolicy);
  body.mSourceFile.Value().Assign(aViolationEventInit.mSourceFile);
  body.mSample.Value().Assign(aViolationEventInit.mSample);
  body.mDisposition.Value() = aViolationEventInit.mDisposition;
  body.mStatusCode.Value() = aViolationEventInit.mStatusCode;
  body.mLineNumber.Value().SetValue(aViolationEventInit.mLineNumber);
  body.mColumnNumber.Value().SetValue(aViolationEventInit.mColumnNumber);

  ReportingUtils::Report(window->AsGlobal(), nsGkAtoms::cspViolation,
                         reportGroup, aViolationEventInit.mDocumentURI, &body);
  return NS_OK;
}

nsresult nsCSPContext::SendReportsToURIs(
    const nsTArray<nsString>& reportURIs,
    const mozilla::dom::SecurityPolicyViolationEventInit& aViolationEventInit) {
  dom::CSPReport report;

  // blocked-uri
  report.mCsp_report.mBlocked_uri = aViolationEventInit.mBlockedURI;

  // document-uri
  report.mCsp_report.mDocument_uri = aViolationEventInit.mDocumentURI;

  // original-policy
  report.mCsp_report.mOriginal_policy = aViolationEventInit.mOriginalPolicy;

  // referrer
  report.mCsp_report.mReferrer = aViolationEventInit.mReferrer;

  // effective-directive
  report.mCsp_report.mEffective_directive =
      aViolationEventInit.mEffectiveDirective;

  // violated-directive
  report.mCsp_report.mViolated_directive =
      aViolationEventInit.mEffectiveDirective;

  // disposition
  report.mCsp_report.mDisposition = aViolationEventInit.mDisposition;

  // status-code
  report.mCsp_report.mStatus_code = aViolationEventInit.mStatusCode;

  // source-file
  if (!aViolationEventInit.mSourceFile.IsEmpty()) {
    report.mCsp_report.mSource_file.Construct();
    CopyUTF16toUTF8(aViolationEventInit.mSourceFile,
                    report.mCsp_report.mSource_file.Value());
  }

  // script-sample
  if (!aViolationEventInit.mSample.IsEmpty()) {
    report.mCsp_report.mScript_sample.Construct();
    report.mCsp_report.mScript_sample.Value() = aViolationEventInit.mSample;
  }

  // line-number
  if (aViolationEventInit.mLineNumber != 0) {
    report.mCsp_report.mLine_number.Construct();
    report.mCsp_report.mLine_number.Value() = aViolationEventInit.mLineNumber;
  }

  if (aViolationEventInit.mColumnNumber != 0) {
    report.mCsp_report.mColumn_number.Construct();
    report.mCsp_report.mColumn_number.Value() =
        aViolationEventInit.mColumnNumber;
  }

  nsString csp_report;
  if (!report.ToJSON(csp_report)) {
    return NS_ERROR_FAILURE;
  }

  // ---------- Assembled, now send it to all the report URIs ----------- //
  nsCOMPtr<Document> doc = do_QueryReferent(mLoadingContext);
  nsCOMPtr<nsIURI> reportURI;
  nsCOMPtr<nsIChannel> reportChannel;

  nsresult rv;
  for (uint32_t r = 0; r < reportURIs.Length(); r++) {
    NS_ConvertUTF16toUTF8 reportURICstring(reportURIs[r]);
    // try to create a new uri from every report-uri string
    rv = NS_NewURI(getter_AddRefs(reportURI), reportURIs[r]);
    if (NS_FAILED(rv)) {
      AutoTArray<nsString, 1> params = {reportURIs[r]};
      CSPCONTEXTLOG(("Could not create nsIURI for report URI %s",
                     reportURICstring.get()));
      logToConsole("triedToSendReport", params,
                   NS_ConvertUTF16toUTF8(aViolationEventInit.mSourceFile),
                   aViolationEventInit.mSample, aViolationEventInit.mLineNumber,
                   aViolationEventInit.mColumnNumber,
                   nsIScriptError::errorFlag);
      continue;  // don't return yet, there may be more URIs
    }

    // try to create a new channel for every report-uri
    if (doc) {
      rv =
          NS_NewChannel(getter_AddRefs(reportChannel), reportURI, doc,
                        nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                        nsIContentPolicy::TYPE_CSP_REPORT);
    } else {
      rv = NS_NewChannel(
          getter_AddRefs(reportChannel), reportURI, mLoadingPrincipal,
          nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
          nsIContentPolicy::TYPE_CSP_REPORT);
    }

    if (NS_FAILED(rv)) {
      CSPCONTEXTLOG(("Could not create new channel for report URI %s",
                     reportURICstring.get()));
      continue;  // don't return yet, there may be more URIs
    }

    // log a warning to console if scheme is not http or https
    if (!net::SchemeIsHttpOrHttps(reportURI)) {
      AutoTArray<nsString, 1> params = {reportURIs[r]};
      logToConsole("reportURInotHttpsOrHttp2", params,
                   NS_ConvertUTF16toUTF8(aViolationEventInit.mSourceFile),
                   aViolationEventInit.mSample, aViolationEventInit.mLineNumber,
                   aViolationEventInit.mColumnNumber,
                   nsIScriptError::errorFlag);
      continue;
    }

    // make sure this is an anonymous request (no cookies) so in case the
    // policy URI is injected, it can't be abused for CSRF.
    nsLoadFlags flags;
    rv = reportChannel->GetLoadFlags(&flags);
    NS_ENSURE_SUCCESS(rv, rv);
    flags |= nsIRequest::LOAD_ANONYMOUS | nsIChannel::LOAD_BACKGROUND |
             nsIChannel::LOAD_BYPASS_SERVICE_WORKER;
    rv = reportChannel->SetLoadFlags(flags);
    NS_ENSURE_SUCCESS(rv, rv);

    // we need to set an nsIChannelEventSink on the channel object
    // so we can tell it to not follow redirects when posting the reports
    RefPtr<CSPReportRedirectSink> reportSink = new CSPReportRedirectSink();
    if (doc && doc->GetDocShell()) {
      nsCOMPtr<nsINetworkInterceptController> interceptController =
          do_QueryInterface(doc->GetDocShell());
      reportSink->SetInterceptController(interceptController);
    }
    reportChannel->SetNotificationCallbacks(reportSink);

    // apply the loadgroup taken by setRequestContextWithDocument. If there's
    // no loadgroup, AsyncOpen will fail on process-split necko (since the
    // channel cannot query the iBrowserChild).
    rv = reportChannel->SetLoadGroup(mCallingChannelLoadGroup);
    NS_ENSURE_SUCCESS(rv, rv);

    // wire in the string input stream to send the report
    nsCOMPtr<nsIStringInputStream> sis(
        do_CreateInstance(NS_STRINGINPUTSTREAM_CONTRACTID));
    NS_ASSERTION(sis,
                 "nsIStringInputStream is needed but not available to send CSP "
                 "violation reports");
    rv = sis->SetUTF8Data(NS_ConvertUTF16toUTF8(csp_report));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIUploadChannel> uploadChannel(do_QueryInterface(reportChannel));
    if (!uploadChannel) {
      // It's possible the URI provided can't be uploaded to, in which case
      // we skip this one. We'll already have warned about a non-HTTP URI
      // earlier.
      continue;
    }

    rv = uploadChannel->SetUploadStream(sis, "application/csp-report"_ns, -1);
    NS_ENSURE_SUCCESS(rv, rv);

    // if this is an HTTP channel, set the request method to post
    nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(reportChannel));
    if (httpChannel) {
      rv = httpChannel->SetRequestMethod("POST"_ns);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }

    RefPtr<CSPViolationReportListener> listener =
        new CSPViolationReportListener();
    rv = reportChannel->AsyncOpen(listener);

    // AsyncOpen should not fail, but could if there's no load group (like if
    // SetRequestContextWith{Document,Principal} is not given a channel). This
    // should fail quietly and not return an error since it's really ok if
    // reports don't go out, but it's good to log the error locally.

    if (NS_FAILED(rv)) {
      AutoTArray<nsString, 1> params = {reportURIs[r]};
      CSPCONTEXTLOG(("AsyncOpen failed for report URI %s",
                     NS_ConvertUTF16toUTF8(params[0]).get()));
      logToConsole("triedToSendReport", params,
                   NS_ConvertUTF16toUTF8(aViolationEventInit.mSourceFile),
                   aViolationEventInit.mSample, aViolationEventInit.mLineNumber,
                   aViolationEventInit.mColumnNumber,
                   nsIScriptError::errorFlag);
    } else {
      CSPCONTEXTLOG(
          ("Sent violation report to URI %s", reportURICstring.get()));
    }
  }
  return NS_OK;
}

void nsCSPContext::HandleInternalPageViolation(
    const CSPViolationData& aCSPViolationData,
    const SecurityPolicyViolationEventInit& aInit,
    const nsAString& aViolatedDirectiveNameAndValue) {
  if (!mSelfURI || !mSelfURI->SchemeIs("chrome")) {
    return;
  }

  nsAutoCString selfURISpec;
  mSelfURI->GetSpec(selfURISpec);

  glean::security::CspViolationInternalPageExtra extra;
  extra.directive = Some(NS_ConvertUTF16toUTF8(aInit.mEffectiveDirective));

  FilenameTypeAndDetails self =
      nsContentSecurityUtils::FilenameToFilenameType(selfURISpec, true);
  extra.selftype = Some(self.first);
  extra.selfdetails = self.second;

  FilenameTypeAndDetails source =
      nsContentSecurityUtils::FilenameToFilenameType(
          NS_ConvertUTF16toUTF8(aInit.mSourceFile), true);
  extra.sourcetype = Some(source.first);
  extra.sourcedetails = source.second;

  extra.linenumber = Some(aInit.mLineNumber);
  extra.columnnumber = Some(aInit.mColumnNumber);

  // Don't collect samples for code that is probably not shipped by us.
  if (source.first.EqualsLiteral("chromeuri") ||
      source.first.EqualsLiteral("resourceuri") ||
      source.first.EqualsLiteral("abouturi")) {
    // aInit's sample requires the 'report-sample' keyword.
    extra.sample = Some(NS_ConvertUTF16toUTF8(aCSPViolationData.mSample));
  }

  if (aInit.mBlockedURI.EqualsLiteral("inline")) {
    extra.blockeduritype = Some("inline"_ns);
  } else {
    FilenameTypeAndDetails blocked =
        nsContentSecurityUtils::FilenameToFilenameType(
            NS_ConvertUTF16toUTF8(aInit.mBlockedURI), true);
    extra.blockeduritype = Some(blocked.first);
    extra.blockeduridetails = blocked.second;
  }

  glean::security::csp_violation_internal_page.Record(Some(extra));

#ifdef DEBUG
  if (!StaticPrefs::security_csp_testing_allow_internal_csp_violation()) {
    NS_ConvertUTF16toUTF8 directive(aViolatedDirectiveNameAndValue);
    nsAutoCString effectiveDirective;
    effectiveDirective.Assign(
        CSP_CSPDirectiveToString(aCSPViolationData.mEffectiveDirective));
    nsFmtCString s(
        FMT_STRING("Unexpected CSP violation on page {} caused by {} (URL: {}, "
                   "Source: {}) violating the directive: \"{}\" (file: {} "
                   "line: {}). For debugging you can set the pref "
                   "security.csp.testing.allow_internal_csp_violation=true."),
        selfURISpec.get(), effectiveDirective.get(),
        NS_ConvertUTF16toUTF8(aInit.mBlockedURI).get(),
        NS_ConvertUTF16toUTF8(aCSPViolationData.mSample).get(), directive.get(),
        aCSPViolationData.mSourceFile.get(), aCSPViolationData.mLineNumber);
    MOZ_CRASH_UNSAFE(s.get());
  }
#endif
}

nsresult nsCSPContext::FireViolationEvent(
    Element* aTriggeringElement, nsICSPEventListener* aCSPEventListener,
    const mozilla::dom::SecurityPolicyViolationEventInit& aViolationEventInit) {
  if (aCSPEventListener) {
    nsAutoString json;
    if (aViolationEventInit.ToJSON(json)) {
      aCSPEventListener->OnCSPViolationEvent(json);
    }

    return NS_OK;
  }

  // 1. If target is not null, and global is a Window, and target’s
  // shadow-including root is not global’s associated Document, set target to
  // null.
  RefPtr<EventTarget> eventTarget = aTriggeringElement;

  nsCOMPtr<Document> doc = do_QueryReferent(mLoadingContext);
  if (doc && aTriggeringElement &&
      aTriggeringElement->GetComposedDoc() != doc) {
    eventTarget = nullptr;
  }

  if (!eventTarget) {
    // If target is a Window, set target to target’s associated Document.
    eventTarget = doc;
  }

  if (!eventTarget && mInnerWindowID && XRE_IsParentProcess()) {
    if (RefPtr<WindowGlobalParent> parent =
            WindowGlobalParent::GetByInnerWindowId(mInnerWindowID)) {
      nsAutoString json;
      if (aViolationEventInit.ToJSON(json)) {
        Unused << parent->SendDispatchSecurityPolicyViolation(json);
      }
    }
    return NS_OK;
  }

  if (!eventTarget) {
    // If we are here, we are probably dealing with workers. Those are handled
    // via nsICSPEventListener. Nothing to do here.
    return NS_OK;
  }

  RefPtr<mozilla::dom::Event> event =
      mozilla::dom::SecurityPolicyViolationEvent::Constructor(
          eventTarget, u"securitypolicyviolation"_ns, aViolationEventInit);
  event->SetTrusted(true);

  ErrorResult rv;
  eventTarget->DispatchEvent(*event, rv);
  return rv.StealNSResult();
}

/**
 * Dispatched from the main thread to send reports for one CSP violation.
 */
class CSPReportSenderRunnable final : public Runnable {
 public:
  CSPReportSenderRunnable(nsICSPEventListener* aCSPEventListener,
                          CSPViolationData&& aCSPViolationData,
                          nsIURI* aOriginalURI, bool aReportOnlyFlag,
                          const nsAString& aViolatedDirectiveName,
                          const nsAString& aViolatedDirectiveNameAndValue,
                          const nsAString& aObserverSubject, bool aReportSample,
                          nsCSPContext* aCSPContext)
      : mozilla::Runnable("CSPReportSenderRunnable"),
        mCSPEventListener(aCSPEventListener),
        mCSPViolationData(std::move(aCSPViolationData)),
        mOriginalURI(aOriginalURI),
        mReportOnlyFlag(aReportOnlyFlag),
        mReportSample(aReportSample),
        mViolatedDirectiveName(aViolatedDirectiveName),
        mViolatedDirectiveNameAndValue(aViolatedDirectiveNameAndValue),
        mCSPContext(aCSPContext) {
    NS_ASSERTION(!aViolatedDirectiveName.IsEmpty(),
                 "Can not send reports without a violated directive");
    // the observer subject is an nsISupports: either an nsISupportsCString
    // from the arg passed in directly, or if that's empty, it's the blocked
    // source.
    if (aObserverSubject.IsEmpty() &&
        mCSPViolationData.mResource.is<nsCOMPtr<nsIURI>>()) {
      mObserverSubject = mCSPViolationData.mResource.as<nsCOMPtr<nsIURI>>();
      return;
    }

    nsAutoCString subject;
    if (aObserverSubject.IsEmpty()) {
      BlockedContentSourceToString(
          mCSPViolationData.BlockedContentSourceOrUnknown(), subject);
    } else {
      CopyUTF16toUTF8(aObserverSubject, subject);
    }

    nsCOMPtr<nsISupportsCString> supportscstr =
        do_CreateInstance(NS_SUPPORTS_CSTRING_CONTRACTID);
    if (supportscstr) {
      supportscstr->SetData(subject);
      mObserverSubject = do_QueryInterface(supportscstr);
    }
  }

  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread());

    // 0) prepare violation data
    mozilla::dom::SecurityPolicyViolationEventInit init;

    nsAutoString effectiveDirective;
    effectiveDirective.AssignASCII(
        CSP_CSPDirectiveToString(mCSPViolationData.mEffectiveDirective));

    nsresult rv = mCSPContext->GatherSecurityPolicyViolationEventData(
        mOriginalURI, effectiveDirective, mCSPViolationData, mReportSample,
        init);
    NS_ENSURE_SUCCESS(rv, rv);

    // 1) notify observers
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (mObserverSubject && observerService) {
      rv = observerService->NotifyObservers(
          mObserverSubject, CSP_VIOLATION_TOPIC, mViolatedDirectiveName.get());
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // 2) send reports for the policy that was violated
    mCSPContext->SendReports(init, mCSPViolationData.mViolatedPolicyIndex);

    // 3) log to console (one per policy violation)
    ReportToConsole();

    // 4) For internal pages we might send the failure to telemetry or crash.
    mCSPContext->HandleInternalPageViolation(mCSPViolationData, init,
                                             mViolatedDirectiveNameAndValue);

    // 5) fire violation event
    // A frame-ancestors violation has occurred, but we should not dispatch
    // the violation event to a potentially cross-origin ancestor.
    if (!mViolatedDirectiveName.EqualsLiteral("frame-ancestors")) {
      mCSPContext->FireViolationEvent(mCSPViolationData.mElement,
                                      mCSPEventListener, init);
    }

    return NS_OK;
  }

 private:
  void ReportToConsole() const {
    NS_ConvertUTF8toUTF16 effectiveDirective(
        CSP_CSPDirectiveToString(mCSPViolationData.mEffectiveDirective));

    const auto blockedContentSource =
        mCSPViolationData.BlockedContentSourceOrUnknown();

    switch (blockedContentSource) {
      case CSPViolationData::BlockedContentSource::Inline: {
        const char* errorName = nullptr;
        if (mCSPViolationData.mEffectiveDirective ==
                CSPDirective::STYLE_SRC_ATTR_DIRECTIVE ||
            mCSPViolationData.mEffectiveDirective ==
                CSPDirective::STYLE_SRC_ELEM_DIRECTIVE) {
          errorName = mReportOnlyFlag ? "CSPROInlineStyleViolation2"
                                      : "CSPInlineStyleViolation2";
        } else if (mCSPViolationData.mEffectiveDirective ==
                   CSPDirective::SCRIPT_SRC_ATTR_DIRECTIVE) {
          errorName = mReportOnlyFlag ? "CSPROEventHandlerScriptViolation2"
                                      : "CSPEventHandlerScriptViolation2";
        } else {
          MOZ_ASSERT(mCSPViolationData.mEffectiveDirective ==
                     CSPDirective::SCRIPT_SRC_ELEM_DIRECTIVE);
          errorName = mReportOnlyFlag ? "CSPROInlineScriptViolation2"
                                      : "CSPInlineScriptViolation2";
        }

        AutoTArray<nsString, 3> params = {
            mViolatedDirectiveNameAndValue, effectiveDirective,
            NS_ConvertUTF8toUTF16(mCSPViolationData.mHashSHA256)};
        mCSPContext->logToConsole(
            errorName, params, mCSPViolationData.mSourceFile,
            mCSPViolationData.mSample, mCSPViolationData.mLineNumber,
            mCSPViolationData.mColumnNumber, nsIScriptError::errorFlag);
        break;
      }

      case CSPViolationData::BlockedContentSource::Eval: {
        AutoTArray<nsString, 2> params = {mViolatedDirectiveNameAndValue,
                                          effectiveDirective};
        mCSPContext->logToConsole(
            mReportOnlyFlag ? "CSPROEvalScriptViolation"
                            : "CSPEvalScriptViolation",
            params, mCSPViolationData.mSourceFile, mCSPViolationData.mSample,
            mCSPViolationData.mLineNumber, mCSPViolationData.mColumnNumber,
            nsIScriptError::errorFlag);
        break;
      }

      case CSPViolationData::BlockedContentSource::WasmEval: {
        AutoTArray<nsString, 2> params = {mViolatedDirectiveNameAndValue,
                                          effectiveDirective};
        mCSPContext->logToConsole(
            mReportOnlyFlag ? "CSPROWasmEvalScriptViolation"
                            : "CSPWasmEvalScriptViolation",
            params, mCSPViolationData.mSourceFile, mCSPViolationData.mSample,
            mCSPViolationData.mLineNumber, mCSPViolationData.mColumnNumber,
            nsIScriptError::errorFlag);
        break;
      }

      case CSPViolationData::BlockedContentSource::TrustedTypesPolicy: {
        AutoTArray<nsString, 1> params = {mViolatedDirectiveNameAndValue};

        mCSPContext->logToConsole(
            mReportOnlyFlag ? "CSPROTrustedTypesPolicyViolation"
                            : "CSPTrustedTypesPolicyViolation",
            params, mCSPViolationData.mSourceFile, mCSPViolationData.mSample,
            mCSPViolationData.mLineNumber, mCSPViolationData.mColumnNumber,
            nsIScriptError::errorFlag);
        break;
      }

      case CSPViolationData::BlockedContentSource::TrustedTypesSink: {
        mCSPContext->logToConsole(
            mReportOnlyFlag ? "CSPROTrustedTypesSinkViolation"
                            : "CSPTrustedTypesSinkViolation",
            {}, mCSPViolationData.mSourceFile, mCSPViolationData.mSample,
            mCSPViolationData.mLineNumber, mCSPViolationData.mColumnNumber,
            nsIScriptError::errorFlag);
        break;
      }

      case CSPViolationData::BlockedContentSource::Self:
      case CSPViolationData::BlockedContentSource::Unknown: {
        nsAutoString source(u"<unknown>"_ns);
        if (mCSPViolationData.mResource.is<nsCOMPtr<nsIURI>>()) {
          nsAutoCString uri;
          auto blockedURI = mCSPViolationData.mResource.as<nsCOMPtr<nsIURI>>();
          blockedURI->GetSpec(uri);

          if (blockedURI->SchemeIs("data") &&
              uri.Length() > nsCSPContext::ScriptSampleMaxLength()) {
            uri.Truncate(nsCSPContext::ScriptSampleMaxLength());
            uri.Append(
                NS_ConvertUTF16toUTF8(nsContentUtils::GetLocalizedEllipsis()));
          }

          if (!uri.IsEmpty()) {
            CopyUTF8toUTF16(uri, source);
          }
        }

        const char* errorName = nullptr;
        switch (mCSPViolationData.mEffectiveDirective) {
          case CSPDirective::STYLE_SRC_ELEM_DIRECTIVE:
            errorName =
                mReportOnlyFlag ? "CSPROStyleViolation" : "CSPStyleViolation";
            break;
          case CSPDirective::SCRIPT_SRC_ELEM_DIRECTIVE:
            errorName =
                mReportOnlyFlag ? "CSPROScriptViolation" : "CSPScriptViolation";
            break;
          case CSPDirective::WORKER_SRC_DIRECTIVE:
            errorName =
                mReportOnlyFlag ? "CSPROWorkerViolation" : "CSPWorkerViolation";
            break;
          default:
            errorName = mReportOnlyFlag ? "CSPROGenericViolation"
                                        : "CSPGenericViolation";
        }

        AutoTArray<nsString, 3> params = {mViolatedDirectiveNameAndValue,
                                          source, effectiveDirective};
        mCSPContext->logToConsole(
            errorName, params, mCSPViolationData.mSourceFile,
            mCSPViolationData.mSample, mCSPViolationData.mLineNumber,
            mCSPViolationData.mColumnNumber, nsIScriptError::errorFlag);
      }
    }
  }

  nsCOMPtr<nsICSPEventListener> mCSPEventListener;
  CSPViolationData mCSPViolationData;
  nsCOMPtr<nsIURI> mOriginalURI;
  bool mReportOnlyFlag;
  bool mReportSample;
  nsString mViolatedDirectiveName;
  nsString mViolatedDirectiveNameAndValue;
  nsCOMPtr<nsISupports> mObserverSubject;
  RefPtr<nsCSPContext> mCSPContext;
};

nsresult nsCSPContext::AsyncReportViolation(
    nsICSPEventListener* aCSPEventListener,
    mozilla::dom::CSPViolationData&& aCSPViolationData, nsIURI* aOriginalURI,
    const nsAString& aViolatedDirectiveName,
    const nsAString& aViolatedDirectiveNameAndValue,
    const nsAString& aObserverSubject, bool aReportSample) {
  EnsureIPCPoliciesRead();
  NS_ENSURE_ARG_MAX(aCSPViolationData.mViolatedPolicyIndex,
                    mPolicies.Length() - 1);

  nsCOMPtr<nsIRunnable> task = new CSPReportSenderRunnable(
      aCSPEventListener, std::move(aCSPViolationData), aOriginalURI,
      mPolicies[aCSPViolationData.mViolatedPolicyIndex]->getReportOnlyFlag(),
      aViolatedDirectiveName, aViolatedDirectiveNameAndValue, aObserverSubject,
      aReportSample, this);

  if (XRE_IsContentProcess()) {
    if (mEventTarget) {
      mEventTarget->Dispatch(task.forget(), NS_DISPATCH_NORMAL);
      return NS_OK;
    }
  }

  NS_DispatchToMainThread(task.forget());
  return NS_OK;
}

/**
 * Based on the given loadinfo, determines if this CSP context allows the
 * ancestry.
 *
 * In order to determine the URI of the parent document (one causing the load
 * of this protected document), this function traverses all Browsing Contexts
 * until it reaches the top level browsing context.
 */
NS_IMETHODIMP
nsCSPContext::PermitsAncestry(nsILoadInfo* aLoadInfo,
                              bool* outPermitsAncestry) {
  nsresult rv;

  *outPermitsAncestry = true;

  RefPtr<mozilla::dom::BrowsingContext> ctx;
  aLoadInfo->GetBrowsingContext(getter_AddRefs(ctx));

  // extract the ancestry as an array
  nsCOMArray<nsIURI> ancestorsArray;
  nsCOMPtr<nsIURI> uriClone;

  while (ctx) {
    nsCOMPtr<nsIPrincipal> currentPrincipal;
    // Generally permitsAncestry is consulted from within the
    // DocumentLoadListener in the parent process. For loads of type object
    // and embed it's called from the Document in the content process.
    // After Bug 1646899 we should be able to remove that branching code for
    // querying the currentURI.
    if (XRE_IsParentProcess()) {
      WindowGlobalParent* window = ctx->Canonical()->GetCurrentWindowGlobal();
      if (window) {
        // Using the URI of the Principal and not the document because e.g.
        // about:blank inherits the principal and hence the URI of the
        // document does not reflect the security context of the document.
        currentPrincipal = window->DocumentPrincipal();
      }
    } else if (nsPIDOMWindowOuter* windowOuter = ctx->GetDOMWindow()) {
      currentPrincipal = nsGlobalWindowOuter::Cast(windowOuter)->GetPrincipal();
    }

    if (currentPrincipal) {
      nsCOMPtr<nsIURI> currentURI;
      auto* currentBasePrincipal = BasePrincipal::Cast(currentPrincipal);
      currentBasePrincipal->GetURI(getter_AddRefs(currentURI));

      if (currentURI) {
        nsAutoCString spec;
        currentURI->GetSpec(spec);
        // delete the userpass from the URI.
        rv = NS_MutateURI(currentURI)
                 .SetRef(""_ns)
                 .SetUserPass(""_ns)
                 .Finalize(uriClone);

        // If setUserPass fails for some reason, just return a clone of the
        // current URI
        if (NS_FAILED(rv)) {
          rv = NS_GetURIWithoutRef(currentURI, getter_AddRefs(uriClone));
          NS_ENSURE_SUCCESS(rv, rv);
        }
        ancestorsArray.AppendElement(uriClone);
      }
    }
    ctx = ctx->GetParent();
  }

  nsAutoString violatedDirective;

  // Now that we've got the ancestry chain in ancestorsArray, time to check
  // them against any CSP.
  // NOTE:  the ancestors are not allowed to be sent cross origin; this is a
  // restriction not placed on subresource loads.

  for (uint32_t a = 0; a < ancestorsArray.Length(); a++) {
    if (CSPCONTEXTLOGENABLED()) {
      CSPCONTEXTLOG(("nsCSPContext::PermitsAncestry, checking ancestor: %s",
                     ancestorsArray[a]->GetSpecOrDefault().get()));
    }
    // omit the ancestor URI in violation reports if cross-origin as per spec
    // (it is a violation of the same-origin policy).
    bool okToSendAncestor =
        NS_SecurityCompareURIs(ancestorsArray[a], mSelfURI, true);

    bool permits =
        permitsInternal(nsIContentSecurityPolicy::FRAME_ANCESTORS_DIRECTIVE,
                        nullptr,  // triggering element
                        nullptr,  // nsICSPEventListener
                        nullptr,  // nsILoadInfo
                        ancestorsArray[a],
                        nullptr,  // no redirect here.
                        true,     // specific, do not use default-src
                        true,     // send violation reports
                        okToSendAncestor);
    if (!permits) {
      *outPermitsAncestry = false;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::Permits(Element* aTriggeringElement,
                      nsICSPEventListener* aCSPEventListener, nsIURI* aURI,
                      CSPDirective aDir, bool aSpecific,
                      bool aSendViolationReports, bool* outPermits) {
  // Can't perform check without aURI
  if (aURI == nullptr) {
    return NS_ERROR_FAILURE;
  }

  if (aURI->SchemeIs("resource")) {
    // XXX Ideally we would call SubjectToCSP() here but that would also
    // allowlist e.g. javascript: URIs which should not be allowlisted here.
    // As a hotfix we just allowlist pdf.js internals here explicitly.
    nsAutoCString uriSpec;
    aURI->GetSpec(uriSpec);
    if (StringBeginsWith(uriSpec, "resource://pdf.js/"_ns)) {
      *outPermits = true;
      return NS_OK;
    }
  }

  *outPermits = permitsInternal(aDir, aTriggeringElement, aCSPEventListener,
                                nullptr,  // no nsILoadInfo
                                aURI,
                                nullptr,  // no original (pre-redirect) URI
                                aSpecific, aSendViolationReports,
                                true);  // send blocked URI in violation reports

  if (CSPCONTEXTLOGENABLED()) {
    CSPCONTEXTLOG(("nsCSPContext::Permits, aUri: %s, aDir: %s, isAllowed: %s",
                   aURI->GetSpecOrDefault().get(),
                   CSP_CSPDirectiveToString(aDir),
                   *outPermits ? "allow" : "deny"));
  }

  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::ToJSON(nsAString& outCSPinJSON) {
  outCSPinJSON.Truncate();
  dom::CSPPolicies jsonPolicies;
  jsonPolicies.mCsp_policies.Construct();
  EnsureIPCPoliciesRead();

  for (uint32_t p = 0; p < mPolicies.Length(); p++) {
    dom::CSP jsonCSP;
    mPolicies[p]->toDomCSPStruct(jsonCSP);
    if (!jsonPolicies.mCsp_policies.Value().AppendElement(jsonCSP, fallible)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  // convert the gathered information to JSON
  if (!jsonPolicies.ToJSON(outCSPinJSON)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::GetCSPSandboxFlags(uint32_t* aOutSandboxFlags) {
  if (!aOutSandboxFlags) {
    return NS_ERROR_FAILURE;
  }
  *aOutSandboxFlags = SANDBOXED_NONE;

  EnsureIPCPoliciesRead();
  for (uint32_t i = 0; i < mPolicies.Length(); i++) {
    uint32_t flags = mPolicies[i]->getSandboxFlags();

    // current policy doesn't have sandbox flag, check next policy
    if (!flags) {
      continue;
    }

    // current policy has sandbox flags, if the policy is in enforcement-mode
    // (i.e. not report-only) set these flags and check for policies with more
    // restrictions
    if (!mPolicies[i]->getReportOnlyFlag()) {
      *aOutSandboxFlags |= flags;
    } else {
      // sandbox directive is ignored in report-only mode, warn about it and
      // continue the loop checking for an enforcement policy.
      nsAutoString policy;
      mPolicies[i]->toString(policy);

      CSPCONTEXTLOG(
          ("nsCSPContext::GetCSPSandboxFlags, report only policy, ignoring "
           "sandbox in: %s",
           NS_ConvertUTF16toUTF8(policy).get()));

      AutoTArray<nsString, 1> params = {policy};
      logToConsole("ignoringReportOnlyDirective", params, ""_ns, u""_ns, 0, 1,
                   nsIScriptError::warningFlag);
    }
  }

  return NS_OK;
}

/* ========== CSPViolationReportListener implementation ========== */

NS_IMPL_ISUPPORTS(CSPViolationReportListener, nsIStreamListener,
                  nsIRequestObserver, nsISupports);

CSPViolationReportListener::CSPViolationReportListener() = default;

CSPViolationReportListener::~CSPViolationReportListener() = default;

nsresult AppendSegmentToString(nsIInputStream* aInputStream, void* aClosure,
                               const char* aRawSegment, uint32_t aToOffset,
                               uint32_t aCount, uint32_t* outWrittenCount) {
  nsCString* decodedData = static_cast<nsCString*>(aClosure);
  decodedData->Append(aRawSegment, aCount);
  *outWrittenCount = aCount;
  return NS_OK;
}

NS_IMETHODIMP
CSPViolationReportListener::OnDataAvailable(nsIRequest* aRequest,
                                            nsIInputStream* aInputStream,
                                            uint64_t aOffset, uint32_t aCount) {
  uint32_t read;
  nsCString decodedData;
  return aInputStream->ReadSegments(AppendSegmentToString, &decodedData, aCount,
                                    &read);
}

NS_IMETHODIMP
CSPViolationReportListener::OnStopRequest(nsIRequest* aRequest,
                                          nsresult aStatus) {
  return NS_OK;
}

NS_IMETHODIMP
CSPViolationReportListener::OnStartRequest(nsIRequest* aRequest) {
  return NS_OK;
}

/* ========== CSPReportRedirectSink implementation ========== */

NS_IMPL_ISUPPORTS(CSPReportRedirectSink, nsIChannelEventSink,
                  nsIInterfaceRequestor);

CSPReportRedirectSink::CSPReportRedirectSink() = default;

CSPReportRedirectSink::~CSPReportRedirectSink() = default;

NS_IMETHODIMP
CSPReportRedirectSink::AsyncOnChannelRedirect(
    nsIChannel* aOldChannel, nsIChannel* aNewChannel, uint32_t aRedirFlags,
    nsIAsyncVerifyRedirectCallback* aCallback) {
  if (aRedirFlags & nsIChannelEventSink::REDIRECT_INTERNAL) {
    aCallback->OnRedirectVerifyCallback(NS_OK);
    return NS_OK;
  }

  // cancel the old channel so XHR failure callback happens
  nsresult rv = aOldChannel->Cancel(NS_ERROR_ABORT);
  NS_ENSURE_SUCCESS(rv, rv);

  // notify an observer that we have blocked the report POST due to a
  // redirect, used in testing, do this async since we're in an async call now
  // to begin with
  nsCOMPtr<nsIURI> uri;
  rv = aOldChannel->GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  NS_ASSERTION(observerService,
               "Observer service required to log CSP violations");
  observerService->NotifyObservers(
      uri, CSP_VIOLATION_TOPIC,
      u"denied redirect while sending violation report");

  return NS_BINDING_REDIRECTED;
}

NS_IMETHODIMP
CSPReportRedirectSink::GetInterface(const nsIID& aIID, void** aResult) {
  if (aIID.Equals(NS_GET_IID(nsINetworkInterceptController)) &&
      mInterceptController) {
    nsCOMPtr<nsINetworkInterceptController> copy(mInterceptController);
    *aResult = copy.forget().take();

    return NS_OK;
  }

  return QueryInterface(aIID, aResult);
}

void CSPReportRedirectSink::SetInterceptController(
    nsINetworkInterceptController* aInterceptController) {
  mInterceptController = aInterceptController;
}

/* ===== nsISerializable implementation ====== */

NS_IMETHODIMP
nsCSPContext::Read(nsIObjectInputStream* aStream) {
  return ReadImpl(aStream, false);
}

nsresult nsCSPContext::PolicyContainerRead(nsIObjectInputStream* aInputStream) {
  return ReadImpl(aInputStream, true);
}

nsresult nsCSPContext::ReadImpl(nsIObjectInputStream* aStream,
                                bool aForPolicyContainer) {
  CSPCONTEXTLOG(("nsCSPContext::Read"));

  nsresult rv;
  nsCOMPtr<nsISupports> supports;

  rv = NS_ReadOptionalObject(aStream, true, getter_AddRefs(supports));
  NS_ENSURE_SUCCESS(rv, rv);

  mSelfURI = do_QueryInterface(supports);
  MOZ_ASSERT(mSelfURI, "need a self URI to de-serialize");

  nsAutoCString JSON;
  rv = aStream->ReadCString(JSON);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrincipal> principal = BasePrincipal::FromJSON(JSON);
  mLoadingPrincipal = principal;
  MOZ_ASSERT(mLoadingPrincipal, "need a loadingPrincipal to de-serialize");

  uint32_t numPolicies;
  rv = aStream->Read32(&numPolicies);
  NS_ENSURE_SUCCESS(rv, rv);

  if (numPolicies == 0) {
    return NS_OK;
  }

  if (aForPolicyContainer) {
    return TryReadPolicies(PolicyDataVersion::Post136, aStream, numPolicies,
                           true);
  }

  // Note: This assume that there is no other data following the CSP!
  // E10SUtils.deserializeCSP is the only user of this logic.
  nsTArray<uint8_t> data;
  rv = NS_ConsumeStream(aStream, UINT32_MAX, data);
  NS_ENSURE_SUCCESS(rv, rv);

  auto createStreamFromData =
      [&data]() -> already_AddRefed<nsIObjectInputStream> {
    nsCOMPtr<nsIInputStream> binaryStream;
    nsresult rv = NS_NewByteInputStream(
        getter_AddRefs(binaryStream),
        Span(reinterpret_cast<const char*>(data.Elements()), data.Length()),
        NS_ASSIGNMENT_DEPEND);
    NS_ENSURE_SUCCESS(rv, nullptr);

    nsCOMPtr<nsIObjectInputStream> stream =
        NS_NewObjectInputStream(binaryStream);

    return stream.forget();
  };

  // Because of accidental backwards incompatible changes we have to try and
  // parse multiple different versions of the CSP data. Starting with the
  // current data format.

  nsCOMPtr<nsIObjectInputStream> stream = createStreamFromData();
  NS_ENSURE_TRUE(stream, NS_ERROR_FAILURE);

  if (NS_SUCCEEDED(TryReadPolicies(PolicyDataVersion::Post136, stream,
                                   numPolicies, false))) {
    CSPCONTEXTLOG(("nsCSPContext::Read: Data was in version ::Post136."));
    return NS_OK;
  }

  stream = createStreamFromData();
  NS_ENSURE_TRUE(stream, NS_ERROR_FAILURE);
  if (NS_SUCCEEDED(TryReadPolicies(PolicyDataVersion::Pre136, stream,
                                   numPolicies, false))) {
    CSPCONTEXTLOG(("nsCSPContext::Read: Data was in version ::Pre136."));
    return NS_OK;
  }

  stream = createStreamFromData();
  NS_ENSURE_TRUE(stream, NS_ERROR_FAILURE);
  if (NS_SUCCEEDED(TryReadPolicies(PolicyDataVersion::V138_9PreRelease, stream,
                                   numPolicies, false))) {
    CSPCONTEXTLOG(
        ("nsCSPContext::Read: Data was in version ::V138_9PreRelease."));
    return NS_OK;
  }

  CSPCONTEXTLOG(("nsCSPContext::Read: Failed to read data!"));
  return NS_ERROR_FAILURE;
}

nsresult nsCSPContext::TryReadPolicies(PolicyDataVersion aVersion,
                                       nsIObjectInputStream* aStream,
                                       uint32_t aNumPolicies,
                                       bool aForPolicyContainer) {
  // Like ReadBoolean, but ensures the byte is actually 0 or 1.
  auto ReadBooleanSafe = [aStream](bool* aBoolean) {
    uint8_t raw = 0;
    nsresult rv = aStream->Read8(&raw);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!(raw == 0 || raw == 1)) {
      CSPCONTEXTLOG(("nsCSPContext::TryReadPolicies: Bad boolean value"));
      return NS_ERROR_FAILURE;
    }

    *aBoolean = !!raw;
    return NS_OK;
  };

  nsTArray<mozilla::ipc::ContentSecurityPolicy> policies;
  nsAutoString policyString;
  while (aNumPolicies > 0) {
    aNumPolicies--;

    nsresult rv = aStream->ReadString(policyString);
    NS_ENSURE_SUCCESS(rv, rv);

    // nsCSPParser::policy removed all non-ASCII tokens while parsing the CSP
    // that was serialized, so we shouldn't have any in this string. A non-ASCII
    // character is thus a strong indicator for some kind of deserialization
    // error.
    if (!IsAscii(Span(policyString))) {
      CSPCONTEXTLOG(
          ("nsCSPContext::TryReadPolicies: Unexpected non-ASCII policy "
           "string"));
      return NS_ERROR_FAILURE;
    }

    bool reportOnly = false;
    rv = ReadBooleanSafe(&reportOnly);
    NS_ENSURE_SUCCESS(rv, rv);

    bool deliveredViaMetaTag = false;
    rv = ReadBooleanSafe(&deliveredViaMetaTag);
    NS_ENSURE_SUCCESS(rv, rv);

    bool hasRequireTrustedTypesForDirective = false;
    if (aVersion == PolicyDataVersion::Post136 ||
        aVersion == PolicyDataVersion::V138_9PreRelease) {
      // Added in bug 1901492.
      rv = ReadBooleanSafe(&hasRequireTrustedTypesForDirective);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    if (aVersion == PolicyDataVersion::V138_9PreRelease) {
      // This was added in bug 1942306, but wasn't really necessary.
      // Removed again in bug 1958259.
      uint32_t numExpressions;
      rv = aStream->Read32(&numExpressions);
      NS_ENSURE_SUCCESS(rv, rv);
      // We assume that because Trusted Types was disabled by default
      // that no "trusted type expressions" were written during that time.
      if (numExpressions != 0) {
        return NS_ERROR_FAILURE;
      }
    }

    policies.AppendElement(
        ContentSecurityPolicy(policyString, reportOnly, deliveredViaMetaTag,
                              hasRequireTrustedTypesForDirective));
  }

  // PolicyContainer may contain extra stuff.
  if (!aForPolicyContainer) {
    // Make sure all data was consumed.
    uint64_t available = 0;
    nsresult rv = aStream->Available(&available);
    NS_ENSURE_SUCCESS(rv, rv);
    if (available) {
      return NS_ERROR_FAILURE;
    }
  }

  // Success! Add the policies now.
  for (auto policy : policies) {
    AddIPCPolicy(policy);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsCSPContext::Write(nsIObjectOutputStream* aStream) {
  nsresult rv = NS_WriteOptionalCompoundObject(aStream, mSelfURI,
                                               NS_GET_IID(nsIURI), true);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString JSON;
  BasePrincipal::Cast(mLoadingPrincipal)->ToJSON(JSON);
  rv = aStream->WriteStringZ(JSON.get());
  NS_ENSURE_SUCCESS(rv, rv);

  // Serialize all the policies.
  aStream->Write32(mPolicies.Length() + mIPCPolicies.Length());

  // WARNING: Any change here needs to be backwards compatible because
  // the serialized CSP data is used across different Firefox versions.
  // Better just don't touch this.

  // This writes data in the PolicyDataVersion::Post136 format.
  nsAutoString polStr;
  for (uint32_t p = 0; p < mPolicies.Length(); p++) {
    polStr.Truncate();
    mPolicies[p]->toString(polStr);
    aStream->WriteWStringZ(polStr.get());
    aStream->WriteBoolean(mPolicies[p]->getReportOnlyFlag());
    aStream->WriteBoolean(mPolicies[p]->getDeliveredViaMetaTagFlag());
    aStream->WriteBoolean(mPolicies[p]->hasRequireTrustedTypesForDirective());
  }
  for (auto& policy : mIPCPolicies) {
    aStream->WriteWStringZ(policy.policy().get());
    aStream->WriteBoolean(policy.reportOnlyFlag());
    aStream->WriteBoolean(policy.deliveredViaMetaTagFlag());
    aStream->WriteBoolean(policy.hasRequireTrustedTypesForDirective());
  }

  return NS_OK;
}

void nsCSPContext::AddIPCPolicy(const ContentSecurityPolicy& aPolicy) {
  mIPCPolicies.AppendElement(aPolicy);
  if (aPolicy.hasRequireTrustedTypesForDirective()) {
    if (mRequireTrustedTypesForDirectiveState !=
        RequireTrustedTypesForDirectiveState::ENFORCE) {
      mRequireTrustedTypesForDirectiveState =
          aPolicy.reportOnlyFlag()
              ? RequireTrustedTypesForDirectiveState::REPORT_ONLY
              : RequireTrustedTypesForDirectiveState::ENFORCE;
    }
  }
}

void nsCSPContext::SerializePolicies(
    nsTArray<ContentSecurityPolicy>& aPolicies) {
  for (auto* policy : mPolicies) {
    nsAutoString policyString;
    policy->toString(policyString);
    aPolicies.AppendElement(
        ContentSecurityPolicy(policyString, policy->getReportOnlyFlag(),
                              policy->getDeliveredViaMetaTagFlag(),
                              policy->hasRequireTrustedTypesForDirective()));
  }

  aPolicies.AppendElements(mIPCPolicies);
}
