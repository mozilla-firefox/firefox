/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsScriptSecurityManager.h"

#include <stdint.h>

#include "DomainPolicy.h"
#include "ExpandedPrincipal.h"
#include "SystemPrincipal.h"
#include "XPCWrapper.h"
#include "js/ColumnNumber.h"  // JS::ColumnNumberOneOrigin
#include "js/GCVector.h"
#include "js/Value.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/ContentPrincipal.h"
#include "mozilla/ExtensionPolicyService.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/Preferences.h"
#include "mozilla/SourceLocation.h"
#include "mozilla/StaticPrefs_extensions.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Exceptions.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/TrustedTypeUtils.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/nsCSPContext.h"
#include "nsAboutProtocolUtils.h"
#include "nsCRT.h"
#include "nsCRTGlue.h"
#include "nsContentSecurityUtils.h"
#include "nsContentUtils.h"
#include "nsDOMCID.h"
#include "nsDOMJSUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDocShell.h"
#include "nsError.h"
#include "nsGlobalWindowInner.h"
#include "nsIChromeRegistry.h"
#include "nsIClassInfo.h"
#include "nsIConsoleService.h"
#include "nsIContent.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIDocShell.h"
#include "nsIEffectiveTLDService.h"
#include "nsILoadContext.h"
#include "nsILoadInfo.h"
#include "nsINestedURI.h"
#include "nsIOService.h"
#include "nsIResProtocolHandler.h"
#include "nsIScriptContext.h"
#include "nsIScriptError.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIStringBundle.h"
#include "nsIURIFixup.h"
#include "nsIURIMutator.h"
#include "nsJSPrincipals.h"
#include "nsJSUtils.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"
#include "nsPIDOMWindowInlines.h"
#include "nsString.h"
#include "nsTextFormatter.h"
#include "nspr.h"
#include "xpcpublic.h"

// This should be probably defined on some other place... but I couldn't find it
#define WEBAPPS_PERM_NAME "webapps-manage"

using namespace mozilla;
using namespace mozilla::dom;

StaticRefPtr<nsIIOService> nsScriptSecurityManager::sIOService;
std::atomic<bool> nsScriptSecurityManager::sStrictFileOriginPolicy = true;

namespace {

class BundleHelper {
 public:
  NS_INLINE_DECL_REFCOUNTING(BundleHelper)

  static nsIStringBundle* GetOrCreate() {
    MOZ_ASSERT(!sShutdown);

    // Already shutting down. Nothing should require the use of the string
    // bundle when shutting down.
    if (sShutdown) {
      return nullptr;
    }

    if (!sSelf) {
      sSelf = new BundleHelper();
    }

    return sSelf->GetOrCreateInternal();
  }

  static void Shutdown() {
    sSelf = nullptr;
    sShutdown = true;
  }

 private:
  ~BundleHelper() = default;

  nsIStringBundle* GetOrCreateInternal() {
    if (!mBundle) {
      nsCOMPtr<nsIStringBundleService> bundleService =
          mozilla::components::StringBundle::Service();
      if (NS_WARN_IF(!bundleService)) {
        return nullptr;
      }

      nsresult rv = bundleService->CreateBundle(
          "chrome://global/locale/security/caps.properties",
          getter_AddRefs(mBundle));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return nullptr;
      }
    }

    return mBundle;
  }

  nsCOMPtr<nsIStringBundle> mBundle;

  static StaticRefPtr<BundleHelper> sSelf;
  static bool sShutdown;
};

StaticRefPtr<BundleHelper> BundleHelper::sSelf;
bool BundleHelper::sShutdown = false;

}  // namespace

///////////////////////////
// Convenience Functions //
///////////////////////////

class nsAutoInPrincipalDomainOriginSetter {
 public:
  nsAutoInPrincipalDomainOriginSetter() { ++sInPrincipalDomainOrigin; }
  ~nsAutoInPrincipalDomainOriginSetter() { --sInPrincipalDomainOrigin; }
  static uint32_t sInPrincipalDomainOrigin;
};
uint32_t nsAutoInPrincipalDomainOriginSetter::sInPrincipalDomainOrigin;

static nsresult GetOriginFromURI(nsIURI* aURI, nsACString& aOrigin) {
  if (!aURI) {
    return NS_ERROR_NULL_POINTER;
  }
  if (nsAutoInPrincipalDomainOriginSetter::sInPrincipalDomainOrigin > 1) {
    // Allow a single recursive call to GetPrincipalDomainOrigin, since that
    // might be happening on a different principal from the first call.  But
    // after that, cut off the recursion; it just indicates that something
    // we're doing in this method causes us to reenter a security check here.
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsAutoInPrincipalDomainOriginSetter autoSetter;

  nsCOMPtr<nsIURI> uri = NS_GetInnermostURI(aURI);
  NS_ENSURE_TRUE(uri, NS_ERROR_UNEXPECTED);

  nsAutoCString hostPort;

  nsresult rv = uri->GetHostPort(hostPort);
  if (NS_SUCCEEDED(rv)) {
    nsAutoCString scheme;
    rv = uri->GetScheme(scheme);
    NS_ENSURE_SUCCESS(rv, rv);
    aOrigin = scheme + "://"_ns + hostPort;
  } else {
    // Some URIs (e.g., nsSimpleURI) don't support host. Just
    // get the full spec.
    rv = uri->GetSpec(aOrigin);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

static nsresult GetPrincipalDomainOrigin(nsIPrincipal* aPrincipal,
                                         nsACString& aOrigin) {
  aOrigin.Truncate();
  nsCOMPtr<nsIURI> uri;
  aPrincipal->GetDomain(getter_AddRefs(uri));
  nsresult rv = GetOriginFromURI(uri, aOrigin);
  if (NS_SUCCEEDED(rv)) {
    return rv;
  }
  // If there is no Domain fallback to the Principals Origin
  return aPrincipal->GetOriginNoSuffix(aOrigin);
}

inline void SetPendingExceptionASCII(JSContext* cx, const char* aMsg) {
  JS_ReportErrorASCII(cx, "%s", aMsg);
}

inline void SetPendingException(JSContext* cx, const char16_t* aMsg) {
  NS_ConvertUTF16toUTF8 msg(aMsg);
  JS_ReportErrorUTF8(cx, "%s", msg.get());
}

/* static */
bool nsScriptSecurityManager::SecurityCompareURIs(nsIURI* aSourceURI,
                                                  nsIURI* aTargetURI) {
  return NS_SecurityCompareURIs(aSourceURI, aTargetURI,
                                sStrictFileOriginPolicy);
}

bool nsScriptSecurityManager::IsHttpOrHttpsAndCrossOrigin(nsIURI* aUriA,
                                                          nsIURI* aUriB) {
  if (!aUriA || !net::SchemeIsHttpOrHttps(aUriA) || !aUriB ||
      !net::SchemeIsHttpOrHttps(aUriB)) {
    return false;
  }
  if (!SecurityCompareURIs(aUriA, aUriB)) {
    return true;
  }
  return false;
}

/*
 * GetChannelResultPrincipal will return the principal that the resource
 * returned by this channel will use.  For example, if the resource is in
 * a sandbox, it will return the nullprincipal.  If the resource is forced
 * to inherit principal, it will return the principal of its parent.  If
 * the load doesn't require sandboxing or inheriting, it will return the same
 * principal as GetChannelURIPrincipal. Namely the principal of the URI
 * that is being loaded.
 */
NS_IMETHODIMP
nsScriptSecurityManager::GetChannelResultPrincipal(nsIChannel* aChannel,
                                                   nsIPrincipal** aPrincipal) {
  return GetChannelResultPrincipal(aChannel, aPrincipal,
                                   /*aIgnoreSandboxing*/ false);
}

nsresult nsScriptSecurityManager::GetChannelResultPrincipalIfNotSandboxed(
    nsIChannel* aChannel, nsIPrincipal** aPrincipal) {
  return GetChannelResultPrincipal(aChannel, aPrincipal,
                                   /*aIgnoreSandboxing*/ true);
}

NS_IMETHODIMP
nsScriptSecurityManager::GetChannelResultStoragePrincipal(
    nsIChannel* aChannel, nsIPrincipal** aPrincipal) {
  nsCOMPtr<nsIPrincipal> principal;
  nsresult rv = GetChannelResultPrincipal(aChannel, getter_AddRefs(principal),
                                          /*aIgnoreSandboxing*/ false);
  if (NS_WARN_IF(NS_FAILED(rv) || !principal)) {
    return rv;
  }

  if (!(principal->GetIsContentPrincipal())) {
    // If for some reason we don't have a content principal here, just reuse our
    // principal for the storage principal too, since attempting to create a
    // storage principal would fail anyway.
    principal.forget(aPrincipal);
    return NS_OK;
  }

  return StoragePrincipalHelper::Create(
      aChannel, principal, /* aForceIsolation */ false, aPrincipal);
}

NS_IMETHODIMP
nsScriptSecurityManager::GetChannelResultPrincipals(
    nsIChannel* aChannel, nsIPrincipal** aPrincipal,
    nsIPrincipal** aPartitionedPrincipal) {
  nsresult rv = GetChannelResultPrincipal(aChannel, aPrincipal,
                                          /*aIgnoreSandboxing*/ false);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!(*aPrincipal)->GetIsContentPrincipal()) {
    // If for some reason we don't have a content principal here, just reuse our
    // principal for the storage principal too, since attempting to create a
    // storage principal would fail anyway.
    nsCOMPtr<nsIPrincipal> copy = *aPrincipal;
    copy.forget(aPartitionedPrincipal);
    return NS_OK;
  }

  return StoragePrincipalHelper::Create(
      aChannel, *aPrincipal, /* aForceIsolation */ true, aPartitionedPrincipal);
}

nsresult nsScriptSecurityManager::GetChannelResultPrincipal(
    nsIChannel* aChannel, nsIPrincipal** aPrincipal, bool aIgnoreSandboxing) {
  MOZ_ASSERT(aChannel, "Must have channel!");

  // Check whether we have an nsILoadInfo that says what we should do.
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  if (loadInfo->GetForceInheritPrincipalOverruleOwner()) {
    nsCOMPtr<nsIPrincipal> principalToInherit =
        loadInfo->FindPrincipalToInherit(aChannel);
    principalToInherit.forget(aPrincipal);
    return NS_OK;
  }

  nsCOMPtr<nsISupports> owner;
  aChannel->GetOwner(getter_AddRefs(owner));
  if (owner) {
    CallQueryInterface(owner, aPrincipal);
    if (*aPrincipal) {
      return NS_OK;
    }
  }

  if (!aIgnoreSandboxing && loadInfo->GetLoadingSandboxed()) {
    // Determine the unsandboxed result principal to use as this null
    // principal's precursor. Ignore errors here, as the precursor isn't
    // required.
    nsCOMPtr<nsIPrincipal> precursor;
    GetChannelResultPrincipal(aChannel, getter_AddRefs(precursor),
                              /*aIgnoreSandboxing*/ true);

    // Construct a deterministic null principal URI from the precursor and the
    // loadinfo's nullPrincipalID.
    nsCOMPtr<nsIURI> nullPrincipalURI = NullPrincipal::CreateURI(
        precursor, &loadInfo->GetSandboxedNullPrincipalID());

    // Use the URI to construct the sandboxed result principal.
    OriginAttributes attrs;
    loadInfo->GetOriginAttributes(&attrs);
    nsCOMPtr<nsIPrincipal> sandboxedPrincipal =
        NullPrincipal::Create(attrs, nullPrincipalURI);
    sandboxedPrincipal.forget(aPrincipal);
    return NS_OK;
  }

  bool forceInherit = loadInfo->GetForceInheritPrincipal();
  if (aIgnoreSandboxing && !forceInherit) {
    // Check if SEC_FORCE_INHERIT_PRINCIPAL was dropped because of
    // sandboxing:
    if (loadInfo->GetLoadingSandboxed() &&
        loadInfo->GetForceInheritPrincipalDropped()) {
      forceInherit = true;
    }
  }
  if (forceInherit) {
    nsCOMPtr<nsIPrincipal> principalToInherit =
        loadInfo->FindPrincipalToInherit(aChannel);
    principalToInherit.forget(aPrincipal);
    return NS_OK;
  }

  auto securityMode = loadInfo->GetSecurityMode();
  // The data: inheritance flags should only apply to the initial load,
  // not to loads that it might have redirected to.
  if (loadInfo->RedirectChain().IsEmpty() &&
      (securityMode ==
           nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_INHERITS_SEC_CONTEXT ||
       securityMode ==
           nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT ||
       securityMode == nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT)) {
    nsCOMPtr<nsIURI> uri;
    nsresult rv = NS_GetFinalChannelURI(aChannel, getter_AddRefs(uri));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIPrincipal> principalToInherit =
        loadInfo->FindPrincipalToInherit(aChannel);
    bool inheritForAboutBlank = loadInfo->GetAboutBlankInherits();

    if (nsContentUtils::ChannelShouldInheritPrincipal(
            principalToInherit, uri, inheritForAboutBlank, false)) {
      principalToInherit.forget(aPrincipal);
      return NS_OK;
    }
  }
  return GetChannelURIPrincipal(aChannel, aPrincipal);
}

/* The principal of the URI that this channel is loading. This is never
 * affected by things like sandboxed loads, or loads where we forcefully
 * inherit the principal.  Think of this as the principal of the server
 * which this channel is loading from.  Most callers should use
 * GetChannelResultPrincipal instead of GetChannelURIPrincipal.  Only
 * call GetChannelURIPrincipal if you are sure that you want the
 * principal that matches the uri, even in cases when the load is
 * sandboxed or when the load could be a blob or data uri (i.e even when
 * you encounter loads that may or may not be sandboxed and loads
 * that may or may not inherit)."
 */
NS_IMETHODIMP
nsScriptSecurityManager::GetChannelURIPrincipal(nsIChannel* aChannel,
                                                nsIPrincipal** aPrincipal) {
  MOZ_ASSERT(aChannel, "Must have channel!");

  // Get the principal from the URI.  Make sure this does the same thing
  // as Document::Reset and PrototypeDocumentContentSink::Init.
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_GetFinalChannelURI(aChannel, getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();

  // Inherit the origin attributes from loadInfo.
  // If this is a top-level document load, the origin attributes of the
  // loadInfo will be set from nsDocShell::DoURILoad.
  // For subresource loading, the origin attributes of the loadInfo is from
  // its loadingPrincipal.
  OriginAttributes attrs = loadInfo->GetOriginAttributes();

  // If the URI is supposed to inherit the security context of whoever loads it,
  // we shouldn't make a content principal for it, so instead return a null
  // principal.
  bool inheritsPrincipal = false;
  rv = NS_URIChainHasFlags(uri,
                           nsIProtocolHandler::URI_INHERITS_SECURITY_CONTEXT,
                           &inheritsPrincipal);
  if (NS_FAILED(rv) || inheritsPrincipal) {
    // Find a precursor principal to credit for the load. This won't impact
    // security checks, but makes tracking the source of related loads easier.
    nsCOMPtr<nsIPrincipal> precursorPrincipal =
        loadInfo->FindPrincipalToInherit(aChannel);
    nsCOMPtr<nsIURI> nullPrincipalURI =
        NullPrincipal::CreateURI(precursorPrincipal);
    *aPrincipal = NullPrincipal::Create(attrs, nullPrincipalURI).take();
    return *aPrincipal ? NS_OK : NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPrincipal> prin =
      BasePrincipal::CreateContentPrincipal(uri, attrs);
  prin.forget(aPrincipal);
  return *aPrincipal ? NS_OK : NS_ERROR_FAILURE;
}

/////////////////////////////
// nsScriptSecurityManager //
/////////////////////////////

////////////////////////////////////
// Methods implementing ISupports //
////////////////////////////////////
NS_IMPL_ISUPPORTS(nsScriptSecurityManager, nsIScriptSecurityManager)

///////////////////////////////////////////////////
// Methods implementing nsIScriptSecurityManager //
///////////////////////////////////////////////////

///////////////// Security Checks /////////////////

bool nsScriptSecurityManager::ContentSecurityPolicyPermitsJSAction(
    JSContext* cx, JS::RuntimeCode aKind, JS::Handle<JSString*> aCodeString,
    JS::CompilationType aCompilationType,
    JS::Handle<JS::StackGCVector<JSString*>> aParameterStrings,
    JS::Handle<JSString*> aBodyString,
    JS::Handle<JS::StackGCVector<JS::Value>> aParameterArgs,
    JS::Handle<JS::Value> aBodyArg, bool* aOutCanCompileStrings) {
  MOZ_ASSERT(cx == nsContentUtils::GetCurrentJSContext());

  nsCOMPtr<nsIPrincipal> subjectPrincipal = nsContentUtils::SubjectPrincipal();

  if (aKind == JS::RuntimeCode::JS) {
    ErrorResult error;
    bool areArgumentsTrusted = TrustedTypeUtils::
        AreArgumentsTrustedForEnsureCSPDoesNotBlockStringCompilation(
            cx, aCodeString, aCompilationType, aParameterStrings, aBodyString,
            aParameterArgs, aBodyArg, subjectPrincipal, error);
    if (error.MaybeSetPendingException(cx)) {
      return false;
    }
    if (!areArgumentsTrusted) {
      *aOutCanCompileStrings = false;
      return true;
    }
  }

  // Check if Eval is allowed per firefox hardening policy
  bool contextForbidsEval =
      (subjectPrincipal->IsSystemPrincipal() || XRE_IsE10sParentProcess());
  if (contextForbidsEval) {
    nsAutoJSString scriptSample;
    if (aKind == JS::RuntimeCode::JS &&
        NS_WARN_IF(!scriptSample.init(cx, aCodeString))) {
      return false;
    }

    if (!nsContentSecurityUtils::IsEvalAllowed(
            cx, subjectPrincipal->IsSystemPrincipal(), scriptSample)) {
      *aOutCanCompileStrings = false;
      return true;
    }
  }

  // Get the window, if any, corresponding to the current global
  nsCOMPtr<nsIContentSecurityPolicy> csp;
  if (nsGlobalWindowInner* win = xpc::CurrentWindowOrNull(cx)) {
    csp = PolicyContainer::GetCSP(win->GetPolicyContainer());
  }

  if (!csp) {
    // Get the CSP for addon sandboxes.  If the principal is expanded and has a
    // csp, we're probably in luck.
    auto* basePrin = BasePrincipal::Cast(subjectPrincipal);
    // TODO bug 1548468: Move CSP off ExpandedPrincipal.
    if (basePrin->Is<ExpandedPrincipal>()) {
      basePrin->As<ExpandedPrincipal>()->GetCsp(getter_AddRefs(csp));
    }
    // don't do anything unless there's a CSP
    if (!csp) {
      *aOutCanCompileStrings = true;
      return true;
    }
  }

  nsCOMPtr<nsICSPEventListener> cspEventListener;
  if (!NS_IsMainThread()) {
    WorkerPrivate* workerPrivate =
        mozilla::dom::GetWorkerPrivateFromContext(cx);
    if (workerPrivate) {
      cspEventListener = workerPrivate->CSPEventListener();
    }
  }

  bool evalOK = true;
  bool reportViolation = false;
  if (aKind == JS::RuntimeCode::JS) {
    nsresult rv = csp->GetAllowsEval(&reportViolation, &evalOK);
    if (NS_FAILED(rv)) {
      NS_WARNING("CSP: failed to get allowsEval");
      *aOutCanCompileStrings = true;  // fail open to not break sites.
      return true;
    }
  } else {
    if (NS_FAILED(csp->GetAllowsWasmEval(&reportViolation, &evalOK))) {
      return false;
    }
    if (!evalOK) {
      // Historically, CSP did not block WebAssembly in Firefox, and some
      // add-ons use wasm and a stricter CSP. To avoid breaking them, ignore
      // 'wasm-unsafe-eval' violations for MV2 extensions.
      // TODO bug 1770909: remove this exception.
      auto* addonPolicy = BasePrincipal::Cast(subjectPrincipal)->AddonPolicy();
      if (addonPolicy && addonPolicy->ManifestVersion() == 2) {
        reportViolation = true;
        evalOK = true;
      }
    }
  }

  if (reportViolation) {
    auto caller = JSCallingLocation::Get(cx);
    nsAutoJSString scriptSample;
    if (aKind == JS::RuntimeCode::JS &&
        NS_WARN_IF(!scriptSample.init(cx, aCodeString))) {
      return false;
    }
    uint16_t violationType =
        aKind == JS::RuntimeCode::JS
            ? nsIContentSecurityPolicy::VIOLATION_TYPE_EVAL
            : nsIContentSecurityPolicy::VIOLATION_TYPE_WASM_EVAL;
    csp->LogViolationDetails(violationType,
                             nullptr,  // triggering element
                             cspEventListener, caller.FileName(), scriptSample,
                             caller.mLine, caller.mColumn, u""_ns, u""_ns);
  }

  *aOutCanCompileStrings = evalOK;
  return true;
}

// static
bool nsScriptSecurityManager::JSPrincipalsSubsume(JSPrincipals* first,
                                                  JSPrincipals* second) {
  return nsJSPrincipals::get(first)->Subsumes(nsJSPrincipals::get(second));
}

NS_IMETHODIMP
nsScriptSecurityManager::CheckSameOriginURI(nsIURI* aSourceURI,
                                            nsIURI* aTargetURI,
                                            bool reportError,
                                            bool aFromPrivateWindow) {
  // Please note that aFromPrivateWindow is only 100% accurate if
  // reportError is true.
  if (!SecurityCompareURIs(aSourceURI, aTargetURI)) {
    if (reportError) {
      ReportError("CheckSameOriginError", aSourceURI, aTargetURI,
                  aFromPrivateWindow);
    }
    return NS_ERROR_DOM_BAD_URI;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsScriptSecurityManager::CheckLoadURIFromScript(JSContext* cx, nsIURI* aURI) {
  // Get principal of currently executing script.
  MOZ_ASSERT(cx == nsContentUtils::GetCurrentJSContext());
  nsIPrincipal* principal = nsContentUtils::SubjectPrincipal();
  nsresult rv = CheckLoadURIWithPrincipal(
      // Passing 0 for the window ID here is OK, because we will report a
      // script-visible exception anyway.
      principal, aURI, nsIScriptSecurityManager::STANDARD, 0);
  if (NS_SUCCEEDED(rv)) {
    // OK to load
    return NS_OK;
  }

  // Report error.
  nsAutoCString spec;
  if (NS_FAILED(aURI->GetAsciiSpec(spec))) return NS_ERROR_FAILURE;
  nsAutoCString msg("Access to '");
  msg.Append(spec);
  msg.AppendLiteral("' from script denied");
  SetPendingExceptionASCII(cx, msg.get());
  return NS_ERROR_DOM_BAD_URI;
}

/**
 * Helper method to handle cases where a flag passed to
 * CheckLoadURIWithPrincipal means denying loading if the given URI has certain
 * nsIProtocolHandler flags set.
 * @return if success, access is allowed. Otherwise, deny access
 */
static nsresult DenyAccessIfURIHasFlags(nsIURI* aURI, uint32_t aURIFlags) {
  MOZ_ASSERT(aURI, "Must have URI!");

  bool uriHasFlags;
  nsresult rv = NS_URIChainHasFlags(aURI, aURIFlags, &uriHasFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  if (uriHasFlags) {
    return NS_ERROR_DOM_BAD_URI;
  }

  return NS_OK;
}

static bool EqualOrSubdomain(nsIURI* aProbeArg, nsIURI* aBase) {
  nsresult rv;
  nsCOMPtr<nsIURI> probe = aProbeArg;

  nsCOMPtr<nsIEffectiveTLDService> tldService =
      do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID);
  NS_ENSURE_TRUE(tldService, false);
  while (true) {
    if (nsScriptSecurityManager::SecurityCompareURIs(probe, aBase)) {
      return true;
    }

    nsAutoCString host, newHost;
    rv = probe->GetHost(host);
    NS_ENSURE_SUCCESS(rv, false);

    rv = tldService->GetNextSubDomain(host, newHost);
    if (rv == NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS) {
      return false;
    }
    NS_ENSURE_SUCCESS(rv, false);
    rv = NS_MutateURI(probe).SetHost(newHost).Finalize(probe);
    NS_ENSURE_SUCCESS(rv, false);
  }
}

NS_IMETHODIMP
nsScriptSecurityManager::CheckLoadURIWithPrincipal(nsIPrincipal* aPrincipal,
                                                   nsIURI* aTargetURI,
                                                   uint32_t aFlags,
                                                   uint64_t aInnerWindowID) {
  MOZ_ASSERT(aPrincipal, "CheckLoadURIWithPrincipal must have a principal");

  // If someone passes a flag that we don't understand, we should
  // fail, because they may need a security check that we don't
  // provide.
  NS_ENSURE_FALSE(
      aFlags &
          ~(nsIScriptSecurityManager::LOAD_IS_AUTOMATIC_DOCUMENT_REPLACEMENT |
            nsIScriptSecurityManager::ALLOW_CHROME |
            nsIScriptSecurityManager::DISALLOW_SCRIPT |
            nsIScriptSecurityManager::DISALLOW_INHERIT_PRINCIPAL |
            nsIScriptSecurityManager::DONT_REPORT_ERRORS),
      NS_ERROR_UNEXPECTED);
  NS_ENSURE_ARG_POINTER(aPrincipal);
  NS_ENSURE_ARG_POINTER(aTargetURI);

  // If DISALLOW_INHERIT_PRINCIPAL is set, we prevent loading of URIs which
  // would do such inheriting. That would be URIs that do not have their own
  // security context. We do this even for the system principal.
  if (aFlags & nsIScriptSecurityManager::DISALLOW_INHERIT_PRINCIPAL) {
    nsresult rv = DenyAccessIfURIHasFlags(
        aTargetURI, nsIProtocolHandler::URI_INHERITS_SECURITY_CONTEXT);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aPrincipal == mSystemPrincipal) {
    // Allow access
    return NS_OK;
  }

  nsCOMPtr<nsIURI> sourceURI;
  auto* basePrin = BasePrincipal::Cast(aPrincipal);
  basePrin->GetURI(getter_AddRefs(sourceURI));
  if (!sourceURI) {
    if (basePrin->Is<ExpandedPrincipal>()) {
      // If the target addon is MV3 or the pref is on we require extension
      // resources loaded from content to be listed in web_accessible_resources.
      auto* targetPolicy =
          ExtensionPolicyService::GetSingleton().GetByURL(aTargetURI);
      bool contentAccessRequired =
          targetPolicy &&
          (targetPolicy->ManifestVersion() > 2 ||
           StaticPrefs::extensions_content_web_accessible_enabled());
      auto expanded = basePrin->As<ExpandedPrincipal>();
      const auto& allowList = expanded->AllowList();
      // Only report errors when all principals fail.
      // With expanded principals, which are used by extension content scripts,
      // we check only against non-extension principals for access to extension
      // resource to enforce making those resources explicitly web accessible.
      uint32_t flags = aFlags | nsIScriptSecurityManager::DONT_REPORT_ERRORS;
      for (size_t i = 0; i < allowList.Length() - 1; i++) {
        if (contentAccessRequired &&
            BasePrincipal::Cast(allowList[i])->AddonPolicy()) {
          continue;
        }
        nsresult rv = CheckLoadURIWithPrincipal(allowList[i], aTargetURI, flags,
                                                aInnerWindowID);
        if (NS_SUCCEEDED(rv)) {
          // Allow access if it succeeded with one of the allowlisted principals
          return NS_OK;
        }
      }

      if (contentAccessRequired &&
          BasePrincipal::Cast(allowList.LastElement())->AddonPolicy()) {
        bool reportErrors =
            !(aFlags & nsIScriptSecurityManager::DONT_REPORT_ERRORS);
        if (reportErrors) {
          ReportError("CheckLoadURI", sourceURI, aTargetURI,
                      allowList.LastElement()
                          ->OriginAttributesRef()
                          .IsPrivateBrowsing(),
                      aInnerWindowID);
        }
        return NS_ERROR_DOM_BAD_URI;
      }
      // Report errors (if requested) for the last principal.
      return CheckLoadURIWithPrincipal(allowList.LastElement(), aTargetURI,
                                       aFlags, aInnerWindowID);
    }
    NS_ERROR(
        "Non-system principals or expanded principal passed to "
        "CheckLoadURIWithPrincipal "
        "must have a URI!");
    return NS_ERROR_UNEXPECTED;
  }

  // Automatic loads are not allowed from certain protocols.
  if (aFlags &
      nsIScriptSecurityManager::LOAD_IS_AUTOMATIC_DOCUMENT_REPLACEMENT) {
    nsresult rv = DenyAccessIfURIHasFlags(
        sourceURI,
        nsIProtocolHandler::URI_FORBIDS_AUTOMATIC_DOCUMENT_REPLACEMENT);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // If either URI is a nested URI, get the base URI
  nsCOMPtr<nsIURI> sourceBaseURI = NS_GetInnermostURI(sourceURI);
  nsCOMPtr<nsIURI> targetBaseURI = NS_GetInnermostURI(aTargetURI);

  //-- get the target scheme
  nsAutoCString targetScheme;
  nsresult rv = targetBaseURI->GetScheme(targetScheme);
  if (NS_FAILED(rv)) return rv;

  //-- Some callers do not allow loading javascript:
  if ((aFlags & nsIScriptSecurityManager::DISALLOW_SCRIPT) &&
      targetScheme.EqualsLiteral("javascript")) {
    return NS_ERROR_DOM_BAD_URI;
  }

  // Check for uris that are only loadable by principals that subsume them
  bool targetURIIsLoadableBySubsumers = false;
  rv = NS_URIChainHasFlags(targetBaseURI,
                           nsIProtocolHandler::URI_LOADABLE_BY_SUBSUMERS,
                           &targetURIIsLoadableBySubsumers);
  NS_ENSURE_SUCCESS(rv, rv);

  if (targetURIIsLoadableBySubsumers) {
    // check nothing else in the URI chain has flags that prevent
    // access:
    rv = CheckLoadURIFlags(
        sourceURI, aTargetURI, sourceBaseURI, targetBaseURI, aFlags,
        aPrincipal->OriginAttributesRef().IsPrivateBrowsing(), aInnerWindowID);
    NS_ENSURE_SUCCESS(rv, rv);
    // Check the principal is allowed to load the target.
    if (aFlags & nsIScriptSecurityManager::DONT_REPORT_ERRORS) {
      return aPrincipal->CheckMayLoad(targetBaseURI, false);
    }
    return aPrincipal->CheckMayLoadWithReporting(targetBaseURI, false,
                                                 aInnerWindowID);
  }

  //-- get the source scheme
  nsAutoCString sourceScheme;
  rv = sourceBaseURI->GetScheme(sourceScheme);
  if (NS_FAILED(rv)) return rv;

  if (sourceScheme.LowerCaseEqualsLiteral(NS_NULLPRINCIPAL_SCHEME)) {
    // A null principal can target its own URI.
    if (sourceURI == aTargetURI) {
      return NS_OK;
    }
  } else if (sourceScheme.EqualsIgnoreCase("file") &&
             targetScheme.EqualsIgnoreCase("moz-icon")) {
    // exception for file: linking to moz-icon://.ext?size=...
    // Note that because targetScheme is the base (innermost) URI scheme,
    // this does NOT allow file -> moz-icon:file:///... links.
    // This is intentional.
    return NS_OK;
  }

  // Check for webextension
  bool targetURIIsLoadableByExtensions = false;
  rv = NS_URIChainHasFlags(aTargetURI,
                           nsIProtocolHandler::URI_LOADABLE_BY_EXTENSIONS,
                           &targetURIIsLoadableByExtensions);
  NS_ENSURE_SUCCESS(rv, rv);

  if (targetURIIsLoadableByExtensions &&
      BasePrincipal::Cast(aPrincipal)->AddonPolicy()) {
    return NS_OK;
  }

  // If we get here, check all the schemes can link to each other, from the top
  // down:
  nsCOMPtr<nsIURI> currentURI = sourceURI;
  nsCOMPtr<nsIURI> currentOtherURI = aTargetURI;

  bool denySameSchemeLinks = false;
  rv = NS_URIChainHasFlags(aTargetURI,
                           nsIProtocolHandler::URI_SCHEME_NOT_SELF_LINKABLE,
                           &denySameSchemeLinks);
  if (NS_FAILED(rv)) return rv;

  while (currentURI && currentOtherURI) {
    nsAutoCString scheme, otherScheme;
    currentURI->GetScheme(scheme);
    currentOtherURI->GetScheme(otherScheme);

    bool schemesMatch =
        scheme.Equals(otherScheme, nsCaseInsensitiveCStringComparator);
    bool isSamePage = false;
    bool isExtensionMismatch = false;
    // about: URIs are special snowflakes.
    if (scheme.EqualsLiteral("about") && schemesMatch) {
      nsAutoCString moduleName, otherModuleName;
      // about: pages can always link to themselves:
      isSamePage =
          NS_SUCCEEDED(NS_GetAboutModuleName(currentURI, moduleName)) &&
          NS_SUCCEEDED(
              NS_GetAboutModuleName(currentOtherURI, otherModuleName)) &&
          moduleName.Equals(otherModuleName);
      if (!isSamePage) {
        // We will have allowed the load earlier if the source page has
        // system principal. So we know the source has a content
        // principal, and it's trying to link to something else.
        // Linkable about: pages are always reachable, even if we hit
        // the CheckLoadURIFlags call below.
        // We punch only 1 other hole: iff the source is unlinkable,
        // we let them link to other pages explicitly marked SAFE
        // for content. This avoids world-linkable about: pages linking
        // to non-world-linkable about: pages.
        nsCOMPtr<nsIAboutModule> module, otherModule;
        bool knowBothModules =
            NS_SUCCEEDED(
                NS_GetAboutModule(currentURI, getter_AddRefs(module))) &&
            NS_SUCCEEDED(NS_GetAboutModule(currentOtherURI,
                                           getter_AddRefs(otherModule)));
        uint32_t aboutModuleFlags = 0;
        uint32_t otherAboutModuleFlags = 0;
        knowBothModules =
            knowBothModules &&
            NS_SUCCEEDED(module->GetURIFlags(currentURI, &aboutModuleFlags)) &&
            NS_SUCCEEDED(otherModule->GetURIFlags(currentOtherURI,
                                                  &otherAboutModuleFlags));
        if (knowBothModules) {
          isSamePage = !(aboutModuleFlags & nsIAboutModule::MAKE_LINKABLE) &&
                       (otherAboutModuleFlags &
                        nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT);
          if (isSamePage &&
              otherAboutModuleFlags & nsIAboutModule::MAKE_LINKABLE) {
            // XXXgijs: this is a hack. The target will be nested
            // (with innerURI of moz-safe-about:whatever), and
            // the source isn't, so we won't pass if we finish
            // the loop. We *should* pass, though, so return here.
            // This hack can go away when bug 1228118 is fixed.
            return NS_OK;
          }
        }
      }
    } else if (schemesMatch && scheme.EqualsLiteral("moz-extension")) {
      // If it is not the same exension, we want to ensure we end up
      // calling CheckLoadURIFlags
      nsAutoCString host, otherHost;
      currentURI->GetHost(host);
      currentOtherURI->GetHost(otherHost);
      isExtensionMismatch = !host.Equals(otherHost);
    } else {
      bool equalExceptRef = false;
      rv = currentURI->EqualsExceptRef(currentOtherURI, &equalExceptRef);
      isSamePage = NS_SUCCEEDED(rv) && equalExceptRef;
    }

    // If schemes are not equal, or they're equal but the target URI
    // is different from the source URI and doesn't always allow linking
    // from the same scheme, or this is two different extensions, check
    // if the URI flags of the current target URI allow the current
    // source URI to link to it.
    // The policy is specified by the protocol flags on both URIs.
    if (!schemesMatch || (denySameSchemeLinks && !isSamePage) ||
        isExtensionMismatch) {
      return CheckLoadURIFlags(
          currentURI, currentOtherURI, sourceBaseURI, targetBaseURI, aFlags,
          aPrincipal->OriginAttributesRef().IsPrivateBrowsing(),
          aInnerWindowID);
    }
    // Otherwise... check if we can nest another level:
    nsCOMPtr<nsINestedURI> nestedURI = do_QueryInterface(currentURI);
    nsCOMPtr<nsINestedURI> nestedOtherURI = do_QueryInterface(currentOtherURI);

    // If schemes match and neither URI is nested further, we're OK.
    if (!nestedURI && !nestedOtherURI) {
      return NS_OK;
    }
    // If one is nested and the other isn't, something is wrong.
    if (!nestedURI != !nestedOtherURI) {
      return NS_ERROR_DOM_BAD_URI;
    }
    // Otherwise, both should be nested and we'll go through the loop again.
    nestedURI->GetInnerURI(getter_AddRefs(currentURI));
    nestedOtherURI->GetInnerURI(getter_AddRefs(currentOtherURI));
  }

  // We should never get here. We should always return from inside the loop.
  return NS_ERROR_DOM_BAD_URI;
}

// Whether aSourceURI is one of our own unprivileged about: pages, pdf.js, or a
// privileged extension, which can load non-content-accessible chrome: and
// resource: URIs.
static bool IsTrustedUISource(nsIURI* aSourceURI, nsIURI* aSourceBaseURI) {
  if (aSourceURI->SchemeIs("about")) {
    return !NS_IsContentAccessibleAboutURI(aSourceURI);
  }
  nsAutoCString host;
  if (aSourceBaseURI->SchemeIs("resource")) {
    return NS_SUCCEEDED(aSourceBaseURI->GetHost(host)) &&
           host.EqualsLiteral("pdf.js");
  }
  if (aSourceBaseURI->SchemeIs("moz-extension")) {
    if (NS_FAILED(aSourceBaseURI->GetHost(host))) {
      return false;
    }
    auto* policy = ExtensionPolicyService::GetSingleton().GetByHost(host);
    return policy && policy->IsPrivileged();
  }
  return false;
}

/**
 * Helper method to check whether the target URI and its innermost ("base") URI
 * has protocol flags that should stop it from being loaded by the source URI
 * (and/or the source URI's innermost ("base") URI), taking into account any
 * nsIScriptSecurityManager flags originally passed to
 * CheckLoadURIWithPrincipal and friends.
 *
 * @return if success, access is allowed. Otherwise, deny access
 */
nsresult nsScriptSecurityManager::CheckLoadURIFlags(
    nsIURI* aSourceURI, nsIURI* aTargetURI, nsIURI* aSourceBaseURI,
    nsIURI* aTargetBaseURI, uint32_t aFlags, bool aFromPrivateWindow,
    uint64_t aInnerWindowID) {
  // Note that the order of policy checks here is very important!
  // We start from most restrictive and work our way down.
  bool reportErrors = !(aFlags & nsIScriptSecurityManager::DONT_REPORT_ERRORS);
  const char* errorTag = "CheckLoadURIError";

  nsAutoCString targetScheme;
  nsresult rv = aTargetBaseURI->GetScheme(targetScheme);
  if (NS_FAILED(rv)) return rv;

  // Check for system target URI.
  rv = DenyAccessIfURIHasFlags(aTargetURI,
                               nsIProtocolHandler::URI_DANGEROUS_TO_LOAD);
  if (NS_FAILED(rv)) {
    // Deny access, since the origin principal is not system
    if (reportErrors) {
      ReportError(errorTag, aSourceURI, aTargetURI, aFromPrivateWindow,
                  aInnerWindowID);
    }
    return rv;
  }

  // WebExtension URIs are only accessible if the ExtensionPolicyService allows
  // the source URI to load them.
  bool targetURIIsWebExtensionResource = false;
  rv = NS_URIChainHasFlags(aTargetURI,
                           nsIProtocolHandler::URI_IS_WEBEXTENSION_RESOURCE,
                           &targetURIIsWebExtensionResource);
  NS_ENSURE_SUCCESS(rv, rv);
  if (targetURIIsWebExtensionResource) {
    bool isAccessible = false;
    rv = ExtensionPolicyService::GetSingleton().SourceMayLoadExtensionURI(
        aSourceURI, aTargetURI, aFromPrivateWindow, &isAccessible);
    if (NS_SUCCEEDED(rv) && isAccessible) {
      return NS_OK;
    }
    if (reportErrors) {
      ReportError(errorTag, aSourceURI, aTargetURI, aFromPrivateWindow,
                  aInnerWindowID);
    }
    return NS_ERROR_DOM_BAD_URI;
  }

  // Check for chrome target URI
  bool targetURIIsUIResource = false;
  rv = NS_URIChainHasFlags(aTargetURI, nsIProtocolHandler::URI_IS_UI_RESOURCE,
                           &targetURIIsUIResource);
  NS_ENSURE_SUCCESS(rv, rv);
  if (targetURIIsUIResource) {
    // ALLOW_CHROME is a flag that we pass on all loads _except_ docshell
    // loads (since docshell loads run the loaded content with its origin
    // principal). We are effectively allowing resource:// and chrome://
    // URIs to load as long as they are content accessible and as long
    // they're not loading it as a document.
    if (aFlags & nsIScriptSecurityManager::ALLOW_CHROME) {
      bool sourceIsUIResource = false;
      rv = NS_URIChainHasFlags(aSourceBaseURI,
                               nsIProtocolHandler::URI_IS_UI_RESOURCE,
                               &sourceIsUIResource);
      NS_ENSURE_SUCCESS(rv, rv);
      if (sourceIsUIResource) {
        // Special case for moz-icon URIs loaded by a local resources like
        // e.g. chrome: or resource:
        if (targetScheme.EqualsLiteral("moz-icon")) {
          return NS_OK;
        }
      }

      if (targetScheme.EqualsLiteral("resource")) {
        if (IsTrustedUISource(aSourceURI, aSourceBaseURI)) {
          return NS_OK;
        }

        nsCOMPtr<nsIProtocolHandler> ph;
        rv = sIOService->GetProtocolHandler("resource", getter_AddRefs(ph));
        NS_ENSURE_SUCCESS(rv, rv);
        if (!ph) {
          return NS_ERROR_DOM_BAD_URI;
        }

        nsCOMPtr<nsIResProtocolHandler> rph = do_QueryInterface(ph);
        if (!rph) {
          return NS_ERROR_DOM_BAD_URI;
        }

        bool accessAllowed = false;
        rph->AllowContentToAccess(aTargetBaseURI, &accessAllowed);
        if (accessAllowed) {
          return NS_OK;
        }
      } else if (targetScheme.EqualsLiteral("chrome")) {
        if (IsTrustedUISource(aSourceURI, aSourceBaseURI)) {
          return NS_OK;
        }
        // Otherwise allow the load only if the chrome package is allowlisted.
        nsCOMPtr<nsIXULChromeRegistry> reg(
            do_GetService(NS_CHROMEREGISTRY_CONTRACTID));
        if (reg) {
          bool accessAllowed = false;
          reg->AllowContentToAccess(aTargetBaseURI, &accessAllowed);
          if (accessAllowed) {
            return NS_OK;
          }
        }
      } else if (targetScheme.EqualsLiteral("moz-page-thumb") ||
                 targetScheme.EqualsLiteral("page-icon") ||
                 targetScheme.EqualsLiteral("moz-newtab-wallpaper") ||
                 targetScheme.EqualsLiteral("moz-newtab-remote-renderer")) {
        if (XRE_IsParentProcess()) {
          return NS_OK;
        }

        auto& remoteType = dom::ContentChild::GetSingleton()->GetRemoteType();
        if (remoteType.IsPrivilegedAbout()) {
          return NS_OK;
        }
      }
    }

    if (reportErrors) {
      ReportError(errorTag, aSourceURI, aTargetURI, aFromPrivateWindow,
                  aInnerWindowID);
    }
    return NS_ERROR_DOM_BAD_URI;
  }

  // Check for target URI pointing to a file
  bool targetURIIsLocalFile = false;
  rv = NS_URIChainHasFlags(aTargetURI, nsIProtocolHandler::URI_IS_LOCAL_FILE,
                           &targetURIIsLocalFile);
  NS_ENSURE_SUCCESS(rv, rv);
  if (targetURIIsLocalFile) {
    // Allow domains that were allowlisted in the prefs. In 99.9% of cases,
    // this array is empty.
    bool isAllowlisted;
    MOZ_ALWAYS_SUCCEEDS(InFileURIAllowlist(aSourceURI, &isAllowlisted));
    if (isAllowlisted) {
      return NS_OK;
    }

    // Allow chrome://
    if (aSourceBaseURI->SchemeIs("chrome")) {
      return NS_OK;
    }

    // Nothing else.
    if (reportErrors) {
      ReportError(errorTag, aSourceURI, aTargetURI, aFromPrivateWindow,
                  aInnerWindowID);
    }
    return NS_ERROR_DOM_BAD_URI;
  }

#ifdef DEBUG
  {
    // Everyone is allowed to load this. The case URI_LOADABLE_BY_SUBSUMERS
    // is handled by the caller which is just delegating to us as a helper.
    bool hasSubsumersFlag = false;
    NS_URIChainHasFlags(aTargetBaseURI,
                        nsIProtocolHandler::URI_LOADABLE_BY_SUBSUMERS,
                        &hasSubsumersFlag);
    bool hasLoadableByAnyone = false;
    NS_URIChainHasFlags(aTargetBaseURI,
                        nsIProtocolHandler::URI_LOADABLE_BY_ANYONE,
                        &hasLoadableByAnyone);
    MOZ_ASSERT(hasLoadableByAnyone || hasSubsumersFlag,
               "why do we get here and do not have any of the two flags set?");
  }
#endif

  return NS_OK;
}

nsresult nsScriptSecurityManager::ReportError(const char* aMessageTag,
                                              const nsACString& aSourceSpec,
                                              const nsACString& aTargetSpec,
                                              bool aFromPrivateWindow,
                                              uint64_t aInnerWindowID) {
  if (aSourceSpec.IsEmpty() || aTargetSpec.IsEmpty()) {
    return NS_OK;
  }

  nsCOMPtr<nsIStringBundle> bundle = BundleHelper::GetOrCreate();
  if (NS_WARN_IF(!bundle)) {
    return NS_OK;
  }

  // Localize the error message
  nsAutoString message;
  AutoTArray<nsString, 2> formatStrings;
  CopyASCIItoUTF16(aSourceSpec, *formatStrings.AppendElement());
  CopyASCIItoUTF16(aTargetSpec, *formatStrings.AppendElement());
  nsresult rv =
      bundle->FormatStringFromName(aMessageTag, formatStrings, message);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIConsoleService> console(
      do_GetService(NS_CONSOLESERVICE_CONTRACTID));
  NS_ENSURE_TRUE(console, NS_ERROR_FAILURE);
  nsCOMPtr<nsIScriptError> error(do_CreateInstance(NS_SCRIPTERROR_CONTRACTID));
  NS_ENSURE_TRUE(error, NS_ERROR_FAILURE);

  // using category of "SOP" so we can link to MDN
  if (aInnerWindowID != 0) {
    rv = error->InitWithWindowID(
        message, ""_ns, 0, 0, nsIScriptError::errorFlag, "SOP"_ns,
        aInnerWindowID, true /* From chrome context */);
  } else {
    rv = error->Init(message, ""_ns, 0, 0, nsIScriptError::errorFlag, "SOP"_ns,
                     aFromPrivateWindow, true /* From chrome context */);
  }
  NS_ENSURE_SUCCESS(rv, rv);
  console->LogMessage(error);
  return NS_OK;
}

nsresult nsScriptSecurityManager::ReportError(const char* aMessageTag,
                                              nsIURI* aSource, nsIURI* aTarget,
                                              bool aFromPrivateWindow,
                                              uint64_t aInnerWindowID) {
  NS_ENSURE_TRUE(aSource && aTarget, NS_ERROR_NULL_POINTER);

  // Get the source URL spec
  nsAutoCString sourceSpec;
  nsresult rv = aSource->GetAsciiSpec(sourceSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  // Get the target URL spec
  nsAutoCString targetSpec;
  rv = aTarget->GetAsciiSpec(targetSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  return ReportError(aMessageTag, sourceSpec, targetSpec, aFromPrivateWindow,
                     aInnerWindowID);
}

NS_IMETHODIMP
nsScriptSecurityManager::CheckLoadURIStrWithPrincipal(
    nsIPrincipal* aPrincipal, const nsACString& aTargetURIStr,
    uint32_t aFlags) {
  nsresult rv;
  nsCOMPtr<nsIURI> target;
  rv = NS_NewURI(getter_AddRefs(target), aTargetURIStr);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = CheckLoadURIWithPrincipal(aPrincipal, target, aFlags, 0);
  if (rv == NS_ERROR_DOM_BAD_URI) {
    // Don't warn because NS_ERROR_DOM_BAD_URI is one of the expected
    // return values.
    return rv;
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // Now start testing fixup -- since aTargetURIStr is a string, not
  // an nsIURI, we may well end up fixing it up before loading.
  // Note: This needs to stay in sync with the nsIURIFixup api.
  nsCOMPtr<nsIURIFixup> fixup = components::URIFixup::Service();
  if (!fixup) {
    return rv;
  }

  // URIFixup's keyword and alternate flags can only fixup to http/https, so we
  // can skip testing them. This simplifies our life because this code can be
  // invoked from the content process where the search service would not be
  // available.
  uint32_t flags[] = {nsIURIFixup::FIXUP_FLAG_NONE,
                      nsIURIFixup::FIXUP_FLAG_FIX_SCHEME_TYPOS};
  for (unsigned int fixupFlags : flags) {
    if (aPrincipal->OriginAttributesRef().IsPrivateBrowsing()) {
      fixupFlags |= nsIURIFixup::FIXUP_FLAG_PRIVATE_CONTEXT;
    }
    nsCOMPtr<nsIURIFixupInfo> fixupInfo;
    rv = fixup->GetFixupURIInfo(aTargetURIStr, fixupFlags,
                                getter_AddRefs(fixupInfo));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = fixupInfo->GetPreferredURI(getter_AddRefs(target));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = CheckLoadURIWithPrincipal(aPrincipal, target, aFlags, 0);
    if (rv == NS_ERROR_DOM_BAD_URI) {
      // Don't warn because NS_ERROR_DOM_BAD_URI is one of the expected
      // return values.
      return rv;
    }
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return rv;
}

NS_IMETHODIMP
nsScriptSecurityManager::CheckLoadURIWithPrincipalFromJS(
    nsIPrincipal* aPrincipal, nsIURI* aTargetURI, uint32_t aFlags,
    uint64_t aInnerWindowID, JSContext* aCx) {
  MOZ_ASSERT(aPrincipal,
             "CheckLoadURIWithPrincipalFromJS must have a principal");
  NS_ENSURE_ARG_POINTER(aPrincipal);
  NS_ENSURE_ARG_POINTER(aTargetURI);

  nsresult rv =
      CheckLoadURIWithPrincipal(aPrincipal, aTargetURI, aFlags, aInnerWindowID);
  if (NS_FAILED(rv)) {
    nsAutoCString uriStr;
    (void)aTargetURI->GetSpec(uriStr);

    nsAutoCString message("Load of ");
    message.Append(uriStr);

    nsAutoCString principalStr;
    (void)aPrincipal->GetSpec(principalStr);
    if (!principalStr.IsEmpty()) {
      message.AppendPrintf(" from %s", principalStr.get());
    }

    message.Append(" denied");

    dom::Throw(aCx, rv, message);
  }

  return rv;
}

NS_IMETHODIMP
nsScriptSecurityManager::CheckLoadURIStrWithPrincipalFromJS(
    nsIPrincipal* aPrincipal, const nsACString& aTargetURIStr, uint32_t aFlags,
    JSContext* aCx) {
  nsCOMPtr<nsIURI> targetURI;
  MOZ_TRY(NS_NewURI(getter_AddRefs(targetURI), aTargetURIStr));

  return CheckLoadURIWithPrincipalFromJS(aPrincipal, targetURI, aFlags, 0, aCx);
}

NS_IMETHODIMP
nsScriptSecurityManager::InFileURIAllowlist(nsIURI* aUri, bool* aResult) {
  MOZ_ASSERT(aUri);
  MOZ_ASSERT(aResult);

  *aResult = false;
  for (nsIURI* uri : EnsureFileURIAllowlist()) {
    if (EqualOrSubdomain(aUri, uri)) {
      *aResult = true;
      return NS_OK;
    }
  }

  return NS_OK;
}

///////////////// Principals ///////////////////////

NS_IMETHODIMP
nsScriptSecurityManager::GetSystemPrincipal(nsIPrincipal** result) {
  NS_ADDREF(*result = mSystemPrincipal);

  return NS_OK;
}

NS_IMETHODIMP
nsScriptSecurityManager::CreateContentPrincipal(
    nsIURI* aURI, JS::Handle<JS::Value> aOriginAttributes, JSContext* aCx,
    nsIPrincipal** aPrincipal) {
  OriginAttributes attrs;
  if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsCOMPtr<nsIPrincipal> prin =
      BasePrincipal::CreateContentPrincipal(aURI, attrs);
  prin.forget(aPrincipal);
  return *aPrincipal ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsScriptSecurityManager::CreateContentPrincipalFromOrigin(
    const nsACString& aOrigin, nsIPrincipal** aPrincipal) {
  if (StringBeginsWith(aOrigin, "["_ns)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (StringBeginsWith(aOrigin,
                       nsLiteralCString(NS_NULLPRINCIPAL_SCHEME ":"))) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsIPrincipal> prin = BasePrincipal::CreateContentPrincipal(aOrigin);
  prin.forget(aPrincipal);
  return *aPrincipal ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsScriptSecurityManager::PrincipalToJSON(nsIPrincipal* aPrincipal,
                                         nsACString& aJSON) {
  aJSON.Truncate();
  if (!aPrincipal) {
    return NS_ERROR_FAILURE;
  }

  BasePrincipal::Cast(aPrincipal)->ToJSON(aJSON);

  if (aJSON.IsEmpty()) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsScriptSecurityManager::JSONToPrincipal(const nsACString& aJSON,
                                         nsIPrincipal** aPrincipal) {
  if (aJSON.IsEmpty()) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPrincipal> principal = BasePrincipal::FromJSON(aJSON);

  if (!principal) {
    return NS_ERROR_FAILURE;
  }

  principal.forget(aPrincipal);
  return NS_OK;
}

NS_IMETHODIMP
nsScriptSecurityManager::CreateNullPrincipal(
    JS::Handle<JS::Value> aOriginAttributes, JSContext* aCx,
    nsIPrincipal** aPrincipal) {
  OriginAttributes attrs;
  if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsCOMPtr<nsIPrincipal> prin = NullPrincipal::Create(attrs);
  prin.forget(aPrincipal);
  return NS_OK;
}

NS_IMETHODIMP
nsScriptSecurityManager::GetLoadContextContentPrincipal(
    nsIURI* aURI, nsILoadContext* aLoadContext, nsIPrincipal** aPrincipal) {
  NS_ENSURE_STATE(aLoadContext);
  OriginAttributes docShellAttrs;
  aLoadContext->GetOriginAttributes(docShellAttrs);

  nsCOMPtr<nsIPrincipal> prin =
      BasePrincipal::CreateContentPrincipal(aURI, docShellAttrs);
  prin.forget(aPrincipal);
  return *aPrincipal ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsScriptSecurityManager::GetDocShellContentPrincipal(
    nsIURI* aURI, nsIDocShell* aDocShell, nsIPrincipal** aPrincipal) {
  nsCOMPtr<nsIPrincipal> prin = BasePrincipal::CreateContentPrincipal(
      aURI, nsDocShell::Cast(aDocShell)->GetOriginAttributes());
  prin.forget(aPrincipal);
  return *aPrincipal ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsScriptSecurityManager::PrincipalWithOA(
    nsIPrincipal* aPrincipal, JS::Handle<JS::Value> aOriginAttributes,
    JSContext* aCx, nsIPrincipal** aReturnPrincipal) {
  if (!aPrincipal) {
    return NS_OK;
  }
  if (aPrincipal->GetIsContentPrincipal()) {
    OriginAttributes attrs;
    if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
      return NS_ERROR_INVALID_ARG;
    }
    auto* contentPrincipal = static_cast<ContentPrincipal*>(aPrincipal);
    RefPtr<ContentPrincipal> copy =
        new ContentPrincipal(contentPrincipal, attrs);
    NS_ENSURE_TRUE(copy, NS_ERROR_FAILURE);
    copy.forget(aReturnPrincipal);
  } else {
    // We do this for null principals, system principals (both fine)
    // ... and expanded principals, where we should probably do something
    // cleverer, but I also don't think we care too much.
    nsCOMPtr<nsIPrincipal> prin = aPrincipal;
    prin.forget(aReturnPrincipal);
  }

  return *aReturnPrincipal ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsScriptSecurityManager::CanCreateWrapper(JSContext* cx, const nsIID& aIID,
                                          nsISupports* aObj,
                                          nsIClassInfo* aClassInfo) {
  // XXX Special case for Exception ?

  // We give remote-XUL allowlisted domains a free pass here. See bug 932906.
  JS::Rooted<JS::Realm*> contextRealm(cx, JS::GetCurrentRealmOrNull(cx));
  MOZ_RELEASE_ASSERT(contextRealm);
  if (!xpc::AllowContentXBLScope(contextRealm)) {
    return NS_OK;
  }

  if (nsContentUtils::IsCallerChrome()) {
    return NS_OK;
  }

  //-- Access denied, report an error
  nsAutoCString originUTF8;
  nsIPrincipal* subjectPrincipal = nsContentUtils::SubjectPrincipal();
  GetPrincipalDomainOrigin(subjectPrincipal, originUTF8);
  NS_ConvertUTF8toUTF16 originUTF16(originUTF8);
  nsAutoCString classInfoNameUTF8;
  if (aClassInfo) {
    aClassInfo->GetClassDescription(classInfoNameUTF8);
  }
  if (classInfoNameUTF8.IsEmpty()) {
    classInfoNameUTF8.AssignLiteral("UnnamedClass");
  }

  nsCOMPtr<nsIStringBundle> bundle = BundleHelper::GetOrCreate();
  if (NS_WARN_IF(!bundle)) {
    return NS_OK;
  }

  NS_ConvertUTF8toUTF16 classInfoUTF16(classInfoNameUTF8);
  nsresult rv;
  nsAutoString errorMsg;
  if (originUTF16.IsEmpty()) {
    AutoTArray<nsString, 1> formatStrings = {std::move(classInfoUTF16)};
    rv = bundle->FormatStringFromName("CreateWrapperDenied", formatStrings,
                                      errorMsg);
  } else {
    AutoTArray<nsString, 2> formatStrings = {std::move(classInfoUTF16),
                                             std::move(originUTF16)};
    rv = bundle->FormatStringFromName("CreateWrapperDeniedForOrigin",
                                      formatStrings, errorMsg);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  SetPendingException(cx, errorMsg.get());
  return NS_ERROR_DOM_XPCONNECT_ACCESS_DENIED;
}

NS_IMETHODIMP
nsScriptSecurityManager::CanCreateInstance(JSContext* cx, const nsCID& aCID) {
  if (nsContentUtils::IsCallerChrome()) {
    return NS_OK;
  }

  //-- Access denied, report an error
  nsAutoCString errorMsg("Permission denied to create instance of class. CID=");
  char cidStr[NSID_LENGTH];
  aCID.ToProvidedString(cidStr);
  errorMsg.Append(cidStr);
  SetPendingExceptionASCII(cx, errorMsg.get());
  return NS_ERROR_DOM_XPCONNECT_ACCESS_DENIED;
}

NS_IMETHODIMP
nsScriptSecurityManager::CanGetService(JSContext* cx, const nsCID& aCID) {
  if (nsContentUtils::IsCallerChrome()) {
    return NS_OK;
  }

  //-- Access denied, report an error
  nsAutoCString errorMsg("Permission denied to get service. CID=");
  char cidStr[NSID_LENGTH];
  aCID.ToProvidedString(cidStr);
  errorMsg.Append(cidStr);
  SetPendingExceptionASCII(cx, errorMsg.get());
  return NS_ERROR_DOM_XPCONNECT_ACCESS_DENIED;
}

const char sJSEnabledPrefName[] = "javascript.enabled";
const char sFileOriginPolicyPrefName[] =
    "security.fileuri.strict_origin_policy";

static const char* kObservedPrefs[] = {
    sJSEnabledPrefName, sFileOriginPolicyPrefName, "capability.policy."};

/////////////////////////////////////////////
// Constructor, Destructor, Initialization //
/////////////////////////////////////////////
nsScriptSecurityManager::nsScriptSecurityManager(void)
    : mPrefInitialized(false), mIsJavaScriptEnabled(false) {
  static_assert(
      sizeof(intptr_t) == sizeof(void*),
      "intptr_t and void* have different lengths on this platform. "
      "This may cause a security failure with the SecurityLevel union.");
}

nsresult nsScriptSecurityManager::Init() {
  nsresult rv;
  RefPtr<nsIIOService> io = mozilla::components::IO::Service(&rv);
  if (NS_FAILED(rv)) {
    return rv;
  }
  sIOService = std::move(io);
  InitPrefs();

  // Create our system principal singleton
  mSystemPrincipal = SystemPrincipal::Init();

  return NS_OK;
}

void nsScriptSecurityManager::InitJSCallbacks(JSContext* aCx) {
  //-- Register security check callback in the JS engine
  //   Currently this is used to control access to function.caller

  static const JSSecurityCallbacks securityCallbacks = {
      ContentSecurityPolicyPermitsJSAction,
      TrustedTypeUtils::HostGetCodeForEval,
      JSPrincipalsSubsume,
  };

  MOZ_ASSERT(!JS_GetSecurityCallbacks(aCx));
  JS_SetSecurityCallbacks(aCx, &securityCallbacks);
  JS_InitDestroyPrincipalsCallback(aCx, nsJSPrincipals::Destroy);

  JS_SetTrustedPrincipals(aCx, BasePrincipal::Cast(mSystemPrincipal));
}

/* static */
void nsScriptSecurityManager::ClearJSCallbacks(JSContext* aCx) {
  JS_SetSecurityCallbacks(aCx, nullptr);
  JS_SetTrustedPrincipals(aCx, nullptr);
}

static StaticRefPtr<nsScriptSecurityManager> gScriptSecMan;

nsScriptSecurityManager::~nsScriptSecurityManager(void) {
  Preferences::UnregisterPrefixCallbacks(
      nsScriptSecurityManager::ScriptSecurityPrefChanged, kObservedPrefs, this);
  if (mDomainPolicy) {
    mDomainPolicy->Deactivate();
  }
  // ContentChild might hold a reference to the domain policy,
  // and it might release it only after the security manager is
  // gone. But we can still assert this for the main process.
  MOZ_ASSERT_IF(XRE_IsParentProcess(), !mDomainPolicy);
}

void nsScriptSecurityManager::Shutdown() {
  sIOService = nullptr;
  BundleHelper::Shutdown();
  SystemPrincipal::Shutdown();
}

nsScriptSecurityManager* nsScriptSecurityManager::GetScriptSecurityManager() {
  return gScriptSecMan;
}

/* static */
void nsScriptSecurityManager::InitStatics() {
  RefPtr<nsScriptSecurityManager> ssManager = new nsScriptSecurityManager();
  nsresult rv = ssManager->Init();
  if (NS_FAILED(rv)) {
    MOZ_CRASH("ssManager->Init() failed");
  }

  ClearOnShutdown(&gScriptSecMan);
  gScriptSecMan = ssManager;
}

// Currently this nsGenericFactory constructor is used only from FastLoad
// (XPCOM object deserialization) code, when "creating" the system principal
// singleton.
already_AddRefed<SystemPrincipal>
nsScriptSecurityManager::SystemPrincipalSingletonConstructor() {
  if (gScriptSecMan)
    return do_AddRef(gScriptSecMan->mSystemPrincipal)
        .downcast<SystemPrincipal>();
  return nullptr;
}

struct IsWhitespace {
  static bool Test(char aChar) { return NS_IsAsciiWhitespace(aChar); };
};
struct IsWhitespaceOrComma {
  static bool Test(char aChar) {
    return aChar == ',' || NS_IsAsciiWhitespace(aChar);
  };
};

template <typename Predicate>
uint32_t SkipPast(const nsCString& str, uint32_t base) {
  while (base < str.Length() && Predicate::Test(str[base])) {
    ++base;
  }
  return base;
}

template <typename Predicate>
uint32_t SkipUntil(const nsCString& str, uint32_t base) {
  while (base < str.Length() && !Predicate::Test(str[base])) {
    ++base;
  }
  return base;
}

// static
void nsScriptSecurityManager::ScriptSecurityPrefChanged(const char* aPref,
                                                        void* aSelf) {
  static_cast<nsScriptSecurityManager*>(aSelf)->ScriptSecurityPrefChanged(
      aPref);
}

inline void nsScriptSecurityManager::ScriptSecurityPrefChanged(
    const char* aPref) {
  MOZ_ASSERT(mPrefInitialized);
  mIsJavaScriptEnabled =
      Preferences::GetBool(sJSEnabledPrefName, mIsJavaScriptEnabled);
  sStrictFileOriginPolicy =
      Preferences::GetBool(sFileOriginPolicyPrefName, false);
  mFileURIAllowlist.reset();
}

void nsScriptSecurityManager::AddSitesToFileURIAllowlist(
    const nsCString& aSiteList) {
  for (uint32_t base = SkipPast<IsWhitespace>(aSiteList, 0), bound = 0;
       base < aSiteList.Length();
       base = SkipPast<IsWhitespace>(aSiteList, bound)) {
    // Grab the current site.
    bound = SkipUntil<IsWhitespace>(aSiteList, base);
    nsAutoCString site(Substring(aSiteList, base, bound - base));

    // Check if the URI is schemeless. If so, add both http and https.
    nsAutoCString unused;
    if (NS_FAILED(sIOService->ExtractScheme(site, unused))) {
      AddSitesToFileURIAllowlist("http://"_ns + site);
      AddSitesToFileURIAllowlist("https://"_ns + site);
      continue;
    }

    // Convert it to a URI and add it to our list.
    nsCOMPtr<nsIURI> uri;
    nsresult rv = NS_NewURI(getter_AddRefs(uri), site);
    if (NS_SUCCEEDED(rv)) {
      mFileURIAllowlist.ref().AppendElement(uri);
    } else {
      nsCOMPtr<nsIConsoleService> console(
          do_GetService("@mozilla.org/consoleservice;1"));
      if (console) {
        nsAutoString msg =
            u"Unable to to add site to file:// URI allowlist: "_ns +
            NS_ConvertASCIItoUTF16(site);
        console->LogStringMessage(msg.get());
      }
    }
  }
}

nsresult nsScriptSecurityManager::InitPrefs() {
  nsIPrefBranch* branch = Preferences::GetRootBranch();
  NS_ENSURE_TRUE(branch, NS_ERROR_FAILURE);

  mPrefInitialized = true;

  // Set the initial value of the "javascript.enabled" prefs
  ScriptSecurityPrefChanged();

  // set observer callbacks in case the value of the prefs change
  Preferences::RegisterPrefixCallbacks(
      nsScriptSecurityManager::ScriptSecurityPrefChanged, kObservedPrefs, this);

  return NS_OK;
}

NS_IMETHODIMP
nsScriptSecurityManager::GetDomainPolicyActive(bool* aRv) {
  *aRv = !!mDomainPolicy;
  return NS_OK;
}

NS_IMETHODIMP
nsScriptSecurityManager::ActivateDomainPolicy(nsIDomainPolicy** aRv) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_SERVICE_NOT_AVAILABLE;
  }

  return ActivateDomainPolicyInternal(aRv);
}

NS_IMETHODIMP
nsScriptSecurityManager::ActivateDomainPolicyInternal(nsIDomainPolicy** aRv) {
  // We only allow one domain policy at a time. The holder of the previous
  // policy must explicitly deactivate it first.
  if (mDomainPolicy) {
    return NS_ERROR_SERVICE_NOT_AVAILABLE;
  }

  mDomainPolicy = new DomainPolicy();
  nsCOMPtr<nsIDomainPolicy> ptr = mDomainPolicy;
  ptr.forget(aRv);
  return NS_OK;
}

// Intentionally non-scriptable. Script must have a reference to the
// nsIDomainPolicy to deactivate it.
void nsScriptSecurityManager::DeactivateDomainPolicy() {
  mDomainPolicy = nullptr;
}

void nsScriptSecurityManager::CloneDomainPolicy(DomainPolicyClone* aClone) {
  MOZ_ASSERT(aClone);
  if (mDomainPolicy) {
    mDomainPolicy->CloneDomainPolicy(aClone);
  } else {
    aClone->active() = false;
  }
}

NS_IMETHODIMP
nsScriptSecurityManager::PolicyAllowsScript(nsIURI* aURI, bool* aRv) {
  nsresult rv;

  // Compute our rule. If we don't have any domain policy set up that might
  // provide exceptions to this rule, we're done.
  *aRv = mIsJavaScriptEnabled;
  if (!mDomainPolicy) {
    return NS_OK;
  }

  // We have a domain policy. Grab the appropriate set of exceptions to the
  // rule (either the blocklist or the allowlist, depending on whether script
  // is enabled or disabled by default).
  nsCOMPtr<nsIDomainSet> exceptions;
  nsCOMPtr<nsIDomainSet> superExceptions;
  if (*aRv) {
    mDomainPolicy->GetBlocklist(getter_AddRefs(exceptions));
    mDomainPolicy->GetSuperBlocklist(getter_AddRefs(superExceptions));
  } else {
    mDomainPolicy->GetAllowlist(getter_AddRefs(exceptions));
    mDomainPolicy->GetSuperAllowlist(getter_AddRefs(superExceptions));
  }

  bool contains;
  rv = exceptions->Contains(aURI, &contains);
  NS_ENSURE_SUCCESS(rv, rv);
  if (contains) {
    *aRv = !*aRv;
    return NS_OK;
  }
  rv = superExceptions->ContainsSuperDomain(aURI, &contains);
  NS_ENSURE_SUCCESS(rv, rv);
  if (contains) {
    *aRv = !*aRv;
  }

  return NS_OK;
}

const nsTArray<nsCOMPtr<nsIURI>>&
nsScriptSecurityManager::EnsureFileURIAllowlist() {
  if (mFileURIAllowlist.isSome()) {
    return mFileURIAllowlist.ref();
  }

  //
  // Rebuild the set of principals for which we allow file:// URI loads. This
  // implements a small subset of an old pref-based CAPS people that people
  // have come to depend on. See bug 995943.
  //

  mFileURIAllowlist.emplace();
  nsAutoCString policies;
  mozilla::Preferences::GetCString("capability.policy.policynames", policies);
  for (uint32_t base = SkipPast<IsWhitespaceOrComma>(policies, 0), bound = 0;
       base < policies.Length();
       base = SkipPast<IsWhitespaceOrComma>(policies, bound)) {
    // Grab the current policy name.
    bound = SkipUntil<IsWhitespaceOrComma>(policies, base);
    auto policyName = Substring(policies, base, bound - base);

    // Figure out if this policy allows loading file:// URIs. If not, we can
    // skip it.
    nsCString checkLoadURIPrefName =
        "capability.policy."_ns + policyName + ".checkloaduri.enabled"_ns;
    nsAutoString value;
    nsresult rv = Preferences::GetString(checkLoadURIPrefName.get(), value);
    if (NS_FAILED(rv) || !value.LowerCaseEqualsLiteral("allaccess")) {
      continue;
    }

    // Grab the list of domains associated with this policy.
    nsCString domainPrefName =
        "capability.policy."_ns + policyName + ".sites"_ns;
    nsAutoCString siteList;
    Preferences::GetCString(domainPrefName.get(), siteList);
    AddSitesToFileURIAllowlist(siteList);
  }

  return mFileURIAllowlist.ref();
}
