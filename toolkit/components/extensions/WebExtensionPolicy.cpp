/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MainThreadUtils.h"
#include "mozilla/ExtensionPolicyService.h"
#include "mozilla/extensions/DocumentObserver.h"
#include "mozilla/extensions/WebExtensionContentScript.h"
#include "mozilla/extensions/WebExtensionPolicy.h"

#include "mozilla/glean/ExtensionsMetrics.h"
#include "mozilla/AddonManagerWebAPI.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/StaticPrefs_extensions.h"
#include "mozilla/Try.h"
#include "nsAboutProtocolUtils.h"
#include "nsContentUtils.h"
#include "nsEscape.h"
#include "nsGlobalWindowInner.h"
#include "nsIConsoleService.h"
#include "nsIObserver.h"
#include "nsISubstitutingProtocolHandler.h"
#include "nsLiteralString.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindowInlines.h"
#include "nsPrintfCString.h"

namespace mozilla {
namespace extensions {

using namespace dom;

static const char kProto[] = "moz-extension";

static const char kBackgroundScriptTypeDefault[] = "text/javascript";

static const char kBackgroundScriptTypeModule[] = "module";

static const char kBackgroundPageHTMLStart[] =
    "<!DOCTYPE html>\n\
<html>\n\
  <head><meta charset=\"utf-8\"></head>\n\
  <body>";

static const char kBackgroundPageHTMLScript[] =
    "\n\
    <script type=\"%s\" src=\"%s\"></script>";

static const char kBackgroundPageHTMLEnd[] =
    "\n\
  </body>\n\
</html>";

#define BASE_CSP_PREF_V2 "extensions.webextensions.base-content-security-policy"
#define DEFAULT_BASE_CSP_V2                                            \
  "script-src 'self' https://* http://localhost:* http://127.0.0.1:* " \
  "moz-extension: blob: filesystem: 'unsafe-eval' 'wasm-unsafe-eval' " \
  "'unsafe-inline';"

#define BASE_CSP_PREF_V3 \
  "extensions.webextensions.base-content-security-policy.v3"
#define DEFAULT_BASE_CSP_V3 "script-src 'self' 'wasm-unsafe-eval';"

#define BASE_CSP_PREF_V3_WITH_LOCALHOST \
  "extensions.webextensions.base-content-security-policy.v3-with-localhost"
#define DEFAULT_BASE_CSP_V3_WITH_LOCALHOST                   \
  "script-src 'self' 'wasm-unsafe-eval' http://localhost:* " \
  "http://127.0.0.1:*;"

#define DEFAULT_SANDBOX_CSP "sandbox allow-scripts; script-src 'self';"

static inline ExtensionPolicyService& EPS() {
  return ExtensionPolicyService::GetSingleton();
}

static nsISubstitutingProtocolHandler* Proto() {
  static nsCOMPtr<nsISubstitutingProtocolHandler> sHandler;

  if (MOZ_UNLIKELY(!sHandler)) {
    nsCOMPtr<nsIIOService> ios = do_GetIOService();
    MOZ_RELEASE_ASSERT(ios);

    nsCOMPtr<nsIProtocolHandler> handler;
    ios->GetProtocolHandler(kProto, getter_AddRefs(handler));

    sHandler = do_QueryInterface(handler);
    MOZ_RELEASE_ASSERT(sHandler);

    ClearOnShutdown(&sHandler);
  }

  return sHandler;
}

bool ParseGlobs(GlobalObject& aGlobal,
                Sequence<OwningMatchGlobOrUTF8String> aGlobs,
                nsTArray<RefPtr<MatchGlobCore>>& aResult, ErrorResult& aRv,
                bool aAllowQuestion = true) {
  for (auto& elem : aGlobs) {
    if (elem.IsMatchGlob()) {
      aResult.AppendElement(elem.GetAsMatchGlob()->Core());
    } else {
      RefPtr<MatchGlobCore> glob =
          new MatchGlobCore(elem.GetAsUTF8String(), aAllowQuestion, false, aRv);
      if (aRv.Failed()) {
        return false;
      }
      aResult.AppendElement(glob);
    }
  }
  return true;
}

enum class ErrorBehavior {
  CreateEmptyPattern,
  Fail,
};

already_AddRefed<MatchPatternSet> ParseMatches(
    GlobalObject& aGlobal,
    const OwningMatchPatternSetOrStringSequence& aMatches,
    const MatchPatternOptions& aOptions, ErrorBehavior aErrorBehavior,
    ErrorResult& aRv) {
  if (aMatches.IsMatchPatternSet()) {
    return do_AddRef(aMatches.GetAsMatchPatternSet().get());
  }

  const auto& strings = aMatches.GetAsStringSequence();

  nsTArray<OwningStringOrMatchPattern> patterns;
  if (!patterns.SetCapacity(strings.Length(), fallible)) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return nullptr;
  }

  for (auto& string : strings) {
    OwningStringOrMatchPattern elt;
    elt.SetAsString() = string;
    patterns.AppendElement(elt);
  }

  RefPtr<MatchPatternSet> result =
      MatchPatternSet::Constructor(aGlobal, patterns, aOptions, aRv);

  if (aRv.Failed() && aErrorBehavior == ErrorBehavior::CreateEmptyPattern) {
    aRv.SuppressException();
    result = MatchPatternSet::Constructor(aGlobal, {}, aOptions, aRv);
  }

  return result.forget();
}

WebAccessibleResource::WebAccessibleResource(
    GlobalObject& aGlobal, const WebAccessibleResourceInit& aInit,
    ErrorResult& aRv) {
  ParseGlobs(aGlobal, aInit.mResources, mWebAccessiblePaths, aRv);
  if (aRv.Failed()) {
    return;
  }

  if (!aInit.mMatches.IsNull()) {
    MatchPatternOptions options;
    options.mRestrictSchemes = true;
    RefPtr<MatchPatternSet> matches =
        ParseMatches(aGlobal, aInit.mMatches.Value(), options,
                     ErrorBehavior::CreateEmptyPattern, aRv);
    MOZ_DIAGNOSTIC_ASSERT(!aRv.Failed());
    mMatches = matches->Core();
  }

  if (!aInit.mExtension_ids.IsNull()) {
    mExtensionIDs = new AtomSet(aInit.mExtension_ids.Value());
  }
}

bool WebAccessibleResource::IsHostMatch(const URLInfo& aURI) {
  if (!mMatches) {
    return false;
  }
  return mMatches->Matches(aURI) ||
         // If aURI is not matching schemes of <all_urls>, permit access anyway
         // if the match pattern includes <all_urls> or *://*/*. This allows
         // requests from sandboxed documents (moz-nullprincipal) to pass.
         //
         // Note: this check means that file:-documents can create a sandboxed
         // iframe to get access to a resource that is restricted to *://*/*,
         // despite "*://*/*" supposed to only match http(s). This is acceptable
         // because a file can already easily bypass the check by embedding a
         // web page to fetch the resource.
         (!MatchPattern::MatchesAllURLs(aURI) && mMatches->MatchesAllWebUrls());
}

bool WebAccessibleResource::IsExtensionMatch(const URLInfo& aURI) {
  if (!mExtensionIDs) {
    return false;
  }
  RefPtr<WebExtensionPolicyCore> policy =
      ExtensionPolicyService::GetCoreByHost(aURI.Host());
  return policy && (mExtensionIDs->Contains(nsGkAtoms::_asterisk) ||
                    mExtensionIDs->Contains(policy->Id()));
}

/*****************************************************************************
 * WebExtensionPolicyCore
 *****************************************************************************/

WebExtensionPolicyCore::WebExtensionPolicyCore(GlobalObject& aGlobal,
                                               WebExtensionPolicy* aPolicy,
                                               const WebExtensionInit& aInit,
                                               ErrorResult& aRv)
    : mPolicy(aPolicy),
      mId(NS_AtomizeMainThread(aInit.mId)),
      mName(aInit.mName),
      mVersion(aInit.mVersion),
      mType(NS_AtomizeMainThread(aInit.mType)),
      mManifestVersion(aInit.mManifestVersion),
      mExtensionPageCSP(aInit.mExtensionPageCSP),
      mSandboxPageCSP(aInit.mSandboxPageCSP),
      mIsPrivileged(aInit.mIsPrivileged),
      mAboutNewTabMayAccess(aInit.mIsPrivileged || EPS().UseRemoteExtensions()),
      mTemporarilyInstalled(aInit.mTemporarilyInstalled),
      mBackgroundWorkerScript(aInit.mBackgroundWorkerScript),
      mIgnoreQuarantine(aInit.mIsPrivileged || aInit.mIgnoreQuarantine),
      mHasRecommendedState(aInit.mHasRecommendedState),
      mPermissions(new AtomSet(aInit.mPermissions)) {
  // In practice this is not necessary, but in tests where the uuid
  // passed in is not lowercased various tests can fail.
  ToLowerCase(aInit.mMozExtensionHostname, mHostname);

  // Initialize the base CSP and extension page CSP
  if (mManifestVersion < 3) {
    nsresult rv = Preferences::GetString(BASE_CSP_PREF_V2, mBaseCSP);
    if (NS_FAILED(rv)) {
      mBaseCSP = NS_LITERAL_STRING_FROM_CSTRING(DEFAULT_BASE_CSP_V2);
    }
  } else if (mTemporarilyInstalled) {
    nsresult rv =
        Preferences::GetString(BASE_CSP_PREF_V3_WITH_LOCALHOST, mBaseCSP);
    if (NS_FAILED(rv)) {
      mBaseCSP =
          NS_LITERAL_STRING_FROM_CSTRING(DEFAULT_BASE_CSP_V3_WITH_LOCALHOST);
    }
  } else {
    nsresult rv = Preferences::GetString(BASE_CSP_PREF_V3, mBaseCSP);
    if (NS_FAILED(rv)) {
      mBaseCSP = NS_LITERAL_STRING_FROM_CSTRING(DEFAULT_BASE_CSP_V3);
    }
  }

  if (!aInit.mSandboxPages.IsNull()) {
    if (!ParseGlobs(aGlobal, aInit.mSandboxPages.Value(),
                    mSandboxPages.SetValue(), aRv, false)) {
      return;
    }
  }

  if (mSandboxPageCSP.IsVoid()) {
    mSandboxPageCSP.AssignLiteral(DEFAULT_SANDBOX_CSP);
  }

  if (mExtensionPageCSP.IsVoid()) {
    if (mManifestVersion < 3) {
      EPS().GetDefaultCSP(mExtensionPageCSP);
    } else {
      EPS().GetDefaultCSPV3(mExtensionPageCSP);
    }
  }

  mWebAccessibleResources.SetCapacity(aInit.mWebAccessibleResources.Length());
  for (const auto& resourceInit : aInit.mWebAccessibleResources) {
    RefPtr<WebAccessibleResource> resource =
        new WebAccessibleResource(aGlobal, resourceInit, aRv);
    if (aRv.Failed()) {
      return;
    }
    mWebAccessibleResources.AppendElement(std::move(resource));
  }

  nsresult rv = NS_NewURI(getter_AddRefs(mBaseURI), aInit.mBaseURL);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
  }
}

bool WebExtensionPolicyCore::IsSandboxPage(nsIURI* aURI) const {
  extensions::URLInfo urlInfo(aURI);
  return aURI && !mSandboxPages.IsNull() &&
         urlInfo.Scheme() == nsGkAtoms::moz_extension &&
         MozExtensionHostname().Equals(urlInfo.Host()) &&
         mSandboxPages.Value().Matches(urlInfo.FilePath());
}

bool WebExtensionPolicyCore::SourceMayAccessPath(
    const URLInfo& aURI, const nsACString& aPath) const {
  if (aURI.Scheme() == nsGkAtoms::moz_extension &&
      MozExtensionHostname().Equals(aURI.Host())) {
    // An extension can always access it's own paths.
    return true;
  }
  // Bug 1786564 Static themes need to allow access to theme resources.
  if (Type() == nsGkAtoms::theme) {
    RefPtr<WebExtensionPolicyCore> policyCore =
        ExtensionPolicyService::GetCoreByHost(aURI.Host());
    return policyCore != nullptr;
  }

  if (aURI.Scheme() == nsGkAtoms::about) {
    nsAutoCString aboutModule;
    if (NS_SUCCEEDED(NS_GetAboutModuleName(aURI.URI(), aboutModule)) &&
        (aboutModule.EqualsLiteral("newtab") ||
         aboutModule.EqualsLiteral("home"))) {
      // For about:newtab/about:home source URIs, allow access to extension
      // resources without requiring the moz-extension:// URL to be listed as
      // web_accessible_resources in the extension manifest.
      //
      // Unprivileged extensions are only allowed when remote extensions are
      // enabled, because moz-extension:// subframes of privilegedabout pages
      // are only isolated in the extension process in that configuration
      // (see Bug 2070189), and unprivileged code must never run in the
      // privilegedabout process. Non-frame loads are restricted by the
      // about:newtab CSP, which only allows moz-extension: in frame-src.
      return mAboutNewTabMayAccess;
    }
  }

  if (ManifestVersion() < 3) {
    return IsWebAccessiblePath(aPath);
  }
  for (const auto& resource : mWebAccessibleResources) {
    if (resource->SourceMayAccessPath(aURI, aPath)) {
      return true;
    }
  }
  return false;
}

bool WebExtensionPolicyCore::FileSchemeAllowed() const {
  if (!StaticPrefs::extensions_webextensions_fileSchemeAccess_requireOptIn()) {
    // Before we required an opt-in to "internal:fileSchemeAccess", file access
    // was automatically allowed if the host permissions allowed it.
    return true;
  }
  // Only "internal:fileSchemeAccess" is checked here; most callers should also
  // check for the presence of file: or <all_urls> host permissions as needed.
  return HasPermission(nsGkAtoms::fileSchemeAllowedPermission);
}

bool WebExtensionPolicyCore::CanAccessURI(const URLInfo& aURI, bool aExplicit,
                                          bool aCheckRestricted,
                                          bool aAllowFilePermission) const {
  if (aCheckRestricted && WebExtensionPolicy::IsRestrictedURI(aURI)) {
    return false;
  }
  if (aCheckRestricted && QuarantinedFromURI(aURI)) {
    return false;
  }
  if (!aAllowFilePermission && aURI.Scheme() == nsGkAtoms::file) {
    // aAllowFilePermission defaults to false. If you call CanAccessURI with
    // it set to true, make sure to check FileSchemeAllowed() if needed.
    return false;
  }
  if (CheckGuarded(aURI).isSome()) {
    return false;
  }

  AutoReadLock lock(mLock);
  return mHostPermissions && mHostPermissions->Matches(aURI, aExplicit);
}

bool WebExtensionPolicyCore::QuarantinedFromDoc(const DocInfo& aDoc) const {
  return QuarantinedFromURI(aDoc.PrincipalURL());
}

bool WebExtensionPolicyCore::QuarantinedFromURI(const URLInfo& aURI) const {
  return !IgnoreQuarantine() && WebExtensionPolicy::IsQuarantinedURI(aURI);
}

bool WebExtensionPolicyCore::PrivateBrowsingAllowed() const {
  return HasPermission(nsGkAtoms::privateBrowsingAllowedPermission);
}

Maybe<dom::ExtensionGuardSource> WebExtensionPolicyCore::CheckGuarded(
    const URLInfo& aURI) const {
  AutoReadLock lock(mLock);
  for (const auto& guard : mGuardSets) {
    if (guard->Denies(aURI)) {
      return Some(guard->Source());
    }
  }
  return Nothing();
}

/*****************************************************************************
 * WebExtensionPolicy
 *****************************************************************************/

WebExtensionPolicy::WebExtensionPolicy(GlobalObject& aGlobal,
                                       const WebExtensionInit& aInit,
                                       ErrorResult& aRv)
    : mCore(new WebExtensionPolicyCore(aGlobal, this, aInit, aRv)),
      mLocalizeCallback(aInit.mLocalizeCallback) {
  if (aRv.Failed()) {
    return;
  }

  MatchPatternOptions options;
  options.mRestrictSchemes = !HasPermission(nsGkAtoms::mozillaAddons);

  // Set host permissions with SetAllowedOrigins to make sure the copy in core
  // and WebExtensionPolicy stay in sync.
  RefPtr<MatchPatternSet> hostPermissions =
      ParseMatches(aGlobal, aInit.mAllowedOrigins, options,
                   ErrorBehavior::CreateEmptyPattern, aRv);
  if (aRv.Failed()) {
    return;
  }
  SetAllowedOrigins(*hostPermissions);

  if (!aInit.mBackgroundScripts.IsNull()) {
    mBackgroundScripts.SetValue().AppendElements(
        aInit.mBackgroundScripts.Value());
  }

  mBackgroundTypeModule = aInit.mBackgroundTypeModule;

  mContentScripts.SetCapacity(aInit.mContentScripts.Length());
  for (const auto& scriptInit : aInit.mContentScripts) {
    // The activeTab permission is only for dynamically injected scripts,
    // it cannot be used for declarative content scripts.
    if (scriptInit.mHasActiveTabPermission) {
      aRv.Throw(NS_ERROR_INVALID_ARG);
      return;
    }

    RefPtr<WebExtensionContentScript> contentScript =
        new WebExtensionContentScript(aGlobal, *this, scriptInit, aRv);
    if (aRv.Failed()) {
      return;
    }
    mContentScripts.AppendElement(std::move(contentScript));
  }

  if (aInit.mReadyPromise.WasPassed()) {
    mReadyPromise = &aInit.mReadyPromise.Value();
  }
}

already_AddRefed<WebExtensionPolicy> WebExtensionPolicy::Constructor(
    GlobalObject& aGlobal, const WebExtensionInit& aInit, ErrorResult& aRv) {
  RefPtr<WebExtensionPolicy> policy =
      new WebExtensionPolicy(aGlobal, aInit, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  return policy.forget();
}

/* static */
void WebExtensionPolicy::GetActiveExtensions(
    dom::GlobalObject& aGlobal,
    nsTArray<RefPtr<WebExtensionPolicy>>& aResults) {
  EPS().GetAll(aResults);
}

/* static */
already_AddRefed<WebExtensionPolicy> WebExtensionPolicy::GetByID(
    dom::GlobalObject& aGlobal, const nsAString& aID) {
  return do_AddRef(EPS().GetByID(aID));
}

/* static */
already_AddRefed<WebExtensionPolicy> WebExtensionPolicy::GetByHostname(
    dom::GlobalObject& aGlobal, const nsACString& aHostname) {
  return do_AddRef(EPS().GetByHost(aHostname));
}

/* static */
already_AddRefed<WebExtensionPolicy> WebExtensionPolicy::GetByURI(
    dom::GlobalObject& aGlobal, nsIURI* aURI) {
  return do_AddRef(EPS().GetByURL(aURI));
}

void WebExtensionPolicy::SetActive(bool aActive, ErrorResult& aRv) {
  if (aActive == mActive) {
    return;
  }

  bool ok = aActive ? Enable() : Disable();

  if (!ok) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
  }
}

bool WebExtensionPolicy::Enable() {
  MOZ_ASSERT(!mActive);

  if (!EPS().RegisterExtension(*this)) {
    return false;
  }

  if (XRE_IsParentProcess()) {
    // Reserve a BrowsingContextGroup for use by this WebExtensionPolicy.
    RefPtr<BrowsingContextGroup> group = BrowsingContextGroup::Create();
    mBrowsingContextGroup = group->MakeKeepAlivePtr();
  }

  (void)Proto()->SetSubstitution(MozExtensionHostname(), BaseURI());

  mActive = true;
  return true;
}

bool WebExtensionPolicy::Disable() {
  MOZ_ASSERT(mActive);
  MOZ_ASSERT(EPS().GetByID(Id()) == this);

  if (!EPS().UnregisterExtension(*this)) {
    return false;
  }

  if (XRE_IsParentProcess()) {
    // Clear our BrowsingContextGroup reference. A new instance will be created
    // when the extension is next activated.
    mBrowsingContextGroup = nullptr;
  }

  (void)Proto()->SetSubstitution(MozExtensionHostname(), nullptr);

  mActive = false;
  return true;
}

void WebExtensionPolicy::GetURL(const nsAString& aPath, nsAString& aResult,
                                ErrorResult& aRv) const {
  auto result = GetURL(aPath);
  if (result.isOk()) {
    aResult = result.unwrap();
  } else {
    aRv.Throw(result.unwrapErr());
  }
}

Result<nsString, nsresult> WebExtensionPolicy::GetURL(
    const nsAString& aPath) const {
  nsPrintfCString spec("%s://%s/", kProto, MozExtensionHostname().get());

  nsCOMPtr<nsIURI> uri;
  MOZ_TRY(NS_NewURI(getter_AddRefs(uri), spec));

  MOZ_TRY(uri->Resolve(NS_ConvertUTF16toUTF8(aPath), spec));

  return NS_ConvertUTF8toUTF16(spec);
}

void WebExtensionPolicy::SetIgnoreQuarantine(bool aIgnore) {
  WebExtensionPolicy_Binding::ClearCachedIgnoreQuarantineValue(this);
  mCore->SetIgnoreQuarantine(aIgnore);
}

void WebExtensionPolicy::SetHasRecommendedState(bool aHasRecommendedState) {
  mCore->SetHasRecommendedState(aHasRecommendedState);
}

void WebExtensionPolicy::RegisterContentScript(
    WebExtensionContentScript& script, ErrorResult& aRv) {
  // Raise an "invalid argument" error if the script is not related to
  // the expected extension or if it is already registered.
  if (script.mExtension != this || mContentScripts.Contains(&script)) {
    aRv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  RefPtr<WebExtensionContentScript> newScript = &script;

  if (!mContentScripts.AppendElement(std::move(newScript), fallible)) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  WebExtensionPolicy_Binding::ClearCachedContentScriptsValue(this);
}

void WebExtensionPolicy::UnregisterContentScript(
    const WebExtensionContentScript& script, ErrorResult& aRv) {
  if (script.mExtension != this || !mContentScripts.RemoveElement(&script)) {
    aRv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  WebExtensionPolicy_Binding::ClearCachedContentScriptsValue(this);
}

void WebExtensionPolicy::SetAllowedOrigins(MatchPatternSet& aAllowedOrigins) {
  // Make sure to keep the version in `WebExtensionPolicy` (which can be exposed
  // back to script using AllowedOrigins()), and the version in
  // `WebExtensionPolicyCore` (which is threadsafe) in sync.
  AutoWriteLock lock(mCore->mLock);
  mHostPermissions = &aAllowedOrigins;
  mCore->mHostPermissions = aAllowedOrigins.Core();
}

void WebExtensionPolicy::GetGuardSets(
    nsTArray<RefPtr<ExtensionGuardSet>>& aResult) const {
  aResult.Assign(mGuardSets);
}

void WebExtensionPolicy::SetGuardSets(
    const nsTArray<OwningNonNull<ExtensionGuardSet>>& aLists) {
  nsTArray<RefPtr<ExtensionGuardSetCore>> cores(aLists.Length());
  mGuardSets.ClearAndRetainStorage();
  mGuardSets.SetCapacity(aLists.Length());
  for (const auto& guard : aLists) {
    cores.AppendElement(guard->Core());
    mGuardSets.AppendElement(&*guard);
  }
  AutoWriteLock lock(mCore->mLock);
  mCore->mGuardSets = std::move(cores);
  WebExtensionPolicy_Binding::ClearCachedGuardSetsValue(this);
}

void WebExtensionPolicy::InjectContentScripts(ErrorResult& aRv) {
  EPS().InjectContentScripts(this, aRv);
}

/* static */
bool WebExtensionPolicy::UseRemoteWebExtensions(GlobalObject& aGlobal) {
  return EPS().UseRemoteExtensions();
}

/* static */
bool WebExtensionPolicy::IsExtensionProcess(GlobalObject& aGlobal) {
  return EPS().IsExtensionProcess();
}

/* static */
bool WebExtensionPolicy::BackgroundServiceWorkerEnabled(GlobalObject& aGlobal) {
  // When MOZ_WEBEXT_WEBIDL_ENABLED is not set at compile time, extension APIs
  // are not available to extension service workers. To avoid confusion, the
  // extensions.backgroundServiceWorker.enabled pref is locked to false
  // in modules/libpref/init/all.js when MOZ_WEBEXT_WEBIDL_ENABLED is not set.
  return StaticPrefs::extensions_backgroundServiceWorker_enabled_AtStartup();
}

/* static */
bool WebExtensionPolicy::QuarantinedDomainsEnabled(GlobalObject& aGlobal) {
  return EPS().GetQuarantinedDomainsEnabled();
}

/* static */
bool WebExtensionPolicy::IsRestrictedDoc(const DocInfo& aDoc) {
  if (aDoc.Principal() && aDoc.Principal()->IsSystemPrincipal()) {
    return true;
  }

  return IsRestrictedURI(aDoc.PrincipalURL());
}

/* static */
bool WebExtensionPolicy::IsRestrictedURI(const URLInfo& aURI) {
  RefPtr<AtomSet> restrictedDomains =
      ExtensionPolicyService::RestrictedDomains();

  if (restrictedDomains && restrictedDomains->Contains(aURI.HostAtom())) {
    return true;
  }

  if (AddonManagerWebAPI::IsValidSite(aURI.URI())) {
    return true;
  }

  return false;
}

/* static */
bool WebExtensionPolicy::IsQuarantinedDoc(const DocInfo& aDoc) {
  return IsQuarantinedURI(aDoc.PrincipalURL());
}

/* static */
bool WebExtensionPolicy::IsQuarantinedURI(const URLInfo& aURI) {
  // Ensure EPS is initialized before asking it about quarantined domains.
  (void)EPS();

  RefPtr<AtomSet> quarantinedDomains =
      ExtensionPolicyService::QuarantinedDomains();

  return quarantinedDomains && quarantinedDomains->Contains(aURI.HostAtom());
}

nsCString WebExtensionPolicy::BackgroundPageHTML() const {
  nsCString result;

  if (mBackgroundScripts.IsNull()) {
    result.SetIsVoid(true);
    return result;
  }

  result.AppendLiteral(kBackgroundPageHTMLStart);

  const char* scriptType = mBackgroundTypeModule ? kBackgroundScriptTypeModule
                                                 : kBackgroundScriptTypeDefault;

  for (auto& script : mBackgroundScripts.Value()) {
    nsCString escaped;
    nsAppendEscapedHTML(NS_ConvertUTF16toUTF8(script), escaped);
    result.AppendPrintf(kBackgroundPageHTMLScript, scriptType, escaped.get());
  }

  result.AppendLiteral(kBackgroundPageHTMLEnd);
  return result;
}

void WebExtensionPolicy::Localize(const nsAString& aInput,
                                  nsString& aOutput) const {
  RefPtr<WebExtensionLocalizeCallback> callback(mLocalizeCallback);
  callback->Call(aInput, aOutput);
}

JSObject* WebExtensionPolicy::WrapObject(JSContext* aCx,
                                         JS::Handle<JSObject*> aGivenProto) {
  return WebExtensionPolicy_Binding::Wrap(aCx, this, aGivenProto);
}

void WebExtensionPolicy::GetContentScripts(
    nsTArray<RefPtr<WebExtensionContentScript>>& aScripts) const {
  aScripts.AppendElements(mContentScripts);
}

bool WebExtensionPolicy::CanAccessContext(nsILoadContext* aContext) const {
  MOZ_ASSERT(aContext);
  return PrivateBrowsingAllowed() || !aContext->UsePrivateBrowsing();
}

bool WebExtensionPolicy::CanAccessWindow(
    const dom::WindowProxyHolder& aWindow) const {
  return PrivateBrowsingAllowed() || !aWindow.get()->UsePrivateBrowsing();
}

void WebExtensionPolicy::GetReadyPromise(
    JSContext* aCx, JS::MutableHandle<JSObject*> aResult) const {
  if (mReadyPromise) {
    aResult.set(mReadyPromise->PromiseObj());
  } else {
    aResult.set(nullptr);
  }
}

uint64_t WebExtensionPolicy::GetBrowsingContextGroupId() const {
  MOZ_ASSERT(XRE_IsParentProcess() && mActive);
  return mBrowsingContextGroup ? mBrowsingContextGroup->Id() : 0;
}

uint64_t WebExtensionPolicy::GetBrowsingContextGroupId(ErrorResult& aRv) {
  if (XRE_IsParentProcess() && mActive) {
    return GetBrowsingContextGroupId();
  }
  aRv.ThrowInvalidAccessError(
      "browsingContextGroupId only available for active policies in the "
      "parent process");
  return 0;
}

WebExtensionPolicy::~WebExtensionPolicy() { mCore->ClearPolicyWeakRef(); }

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(WebExtensionPolicy)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(WebExtensionPolicy)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mBrowsingContextGroup)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLocalizeCallback)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mHostPermissions)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mGuardSets)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mContentScripts)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mReadyPromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  AssertIsOnMainThread();
  tmp->mCore->ClearPolicyWeakRef();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(WebExtensionPolicy)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mBrowsingContextGroup)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLocalizeCallback)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mHostPermissions)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGuardSets)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mContentScripts)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReadyPromise)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WebExtensionPolicy)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(WebExtensionPolicy)
NS_IMPL_CYCLE_COLLECTING_RELEASE(WebExtensionPolicy)

/*****************************************************************************
 * WebExtensionContentScript / MozDocumentMatcher
 *****************************************************************************/

/* static */
already_AddRefed<MozDocumentMatcher> MozDocumentMatcher::Constructor(
    GlobalObject& aGlobal, const dom::MozDocumentMatcherInit& aInit,
    ErrorResult& aRv) {
  RefPtr<MozDocumentMatcher> matcher =
      new MozDocumentMatcher(aGlobal, aInit, false, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  return matcher.forget();
}

/* static */
already_AddRefed<WebExtensionContentScript>
WebExtensionContentScript::Constructor(GlobalObject& aGlobal,
                                       WebExtensionPolicy& aExtension,
                                       const ContentScriptInit& aInit,
                                       ErrorResult& aRv) {
  RefPtr<WebExtensionContentScript> script =
      new WebExtensionContentScript(aGlobal, aExtension, aInit, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  return script.forget();
}

void WebExtensionContentScript::GetWorldId(nsAString& aWorldId) const {
  if (!mWorldId.IsNull()) {
    aWorldId = mWorldId.Value();
  } else {
    SetDOMStringToNull(aWorldId);
  }
}

MozDocumentMatcher::MozDocumentMatcher(GlobalObject& aGlobal,
                                       const dom::MozDocumentMatcherInit& aInit,
                                       bool aRestricted, ErrorResult& aRv)
    : mHasActiveTabPermission(aInit.mHasActiveTabPermission),
      mRestricted(aRestricted),
      mIsUserScript(aInit.mIsUserScript),
      mAllFrames(aInit.mAllFrames),
      mCheckPermissions(aInit.mCheckPermissions),
      mFrameID(aInit.mFrameID),
      mMatchAboutBlank(aInit.mMatchAboutBlank || aInit.mMatchOriginAsFallback),
      mMatchOriginAsFallback(aInit.mMatchOriginAsFallback) {
  MatchPatternOptions options;
  options.mRestrictSchemes = mRestricted;

  mMatches = ParseMatches(aGlobal, aInit.mMatches, options,
                          ErrorBehavior::CreateEmptyPattern, aRv);
  if (aRv.Failed()) {
    return;
  }

  if (!aInit.mExcludeMatches.IsNull()) {
    mExcludeMatches =
        ParseMatches(aGlobal, aInit.mExcludeMatches.Value(), options,
                     ErrorBehavior::CreateEmptyPattern, aRv);
    if (aRv.Failed()) {
      return;
    }
  }

  if (!aInit.mIncludeGlobs.IsNull()) {
    if (!ParseGlobs(aGlobal, aInit.mIncludeGlobs.Value(),
                    mIncludeGlobs.SetValue(), aRv)) {
      return;
    }
  }

  if (!aInit.mExcludeGlobs.IsNull()) {
    if (!ParseGlobs(aGlobal, aInit.mExcludeGlobs.Value(),
                    mExcludeGlobs.SetValue(), aRv)) {
      return;
    }
  }

  if (!aInit.mOriginAttributesPatterns.IsNull()) {
    Sequence<OriginAttributesPattern>& arr =
        mOriginAttributesPatterns.SetValue();
    for (const auto& pattern : aInit.mOriginAttributesPatterns.Value()) {
      if (!arr.AppendElement(OriginAttributesPattern(pattern), fallible)) {
        aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
        return;
      }
    }
  }
}

WebExtensionContentScript::WebExtensionContentScript(
    GlobalObject& aGlobal, WebExtensionPolicy& aExtension,
    const ContentScriptInit& aInit, ErrorResult& aRv)
    : MozDocumentMatcher(aGlobal, aInit,
                         !aExtension.HasPermission(nsGkAtoms::mozillaAddons),
                         aRv),
      mRunAt(aInit.mRunAt),
      mWorld(aInit.mWorld),
      mWorldId(aInit.mWorldId),
      mCssOrigin(aInit.mCssOrigin) {
  mCssPaths.Assign(aInit.mCssPaths);
  mJsPaths.Assign(aInit.mJsPaths);
  mExtension = &aExtension;

  // Origin permissions are optional in mv3, so always check them at runtime.
  if (mExtension->ManifestVersion() >= 3) {
    mCheckPermissions = true;
  }

  // The USER_SCRIPT world is not supported for regular content scripts.
  MOZ_ASSERT_IF(!mIsUserScript,
                mWorld != ContentScriptExecutionWorld::USER_SCRIPT);
  // User scripts should never run in privileged content script worlds.
  MOZ_ASSERT_IF(mIsUserScript, mWorld != ContentScriptExecutionWorld::ISOLATED);
}

bool MozDocumentMatcher::Matches(const DocInfo& aDoc,
                                 bool aIgnorePermissions) const {
  if (!mFrameID.IsNull()) {
    if (aDoc.FrameID() != mFrameID.Value()) {
      return false;
    }
  } else {
    if (!mAllFrames && !aDoc.IsTopLevel()) {
      return false;
    }
  }

  // match browsing mode with policy
  nsCOMPtr<nsILoadContext> loadContext = aDoc.GetLoadContext();
  if (loadContext && mExtension && !mExtension->CanAccessContext(loadContext)) {
    return false;
  }

  if (loadContext && !mOriginAttributesPatterns.IsNull()) {
    OriginAttributes docShellAttrs;
    loadContext->GetOriginAttributes(docShellAttrs);
    bool patternMatch = false;
    for (const auto& pattern : mOriginAttributesPatterns.Value()) {
      if (pattern.Matches(docShellAttrs)) {
        patternMatch = true;
        break;
      }
    }
    if (!patternMatch) {
      return false;
    }
  }

  if (!mMatchAboutBlank && aDoc.URL().InheritsPrincipal()) {
    return false;
  }

  if (mIsUserScript && mExtension &&
      !mExtension->HasPermission(nsGkAtoms::userScripts)) {
    // The "userScripts" permission can be revoked after script registration.
    return false;
  }

  // Top-level about:blank is a special case. Unlike about:blank frames/windows
  // opened by web pages, these do not have an origin that could be matched by
  // a match pattern (they have a null principal instead). To allow extensions
  // that intend to run scripts "everywhere", consider the document matched if
  // the match pattern describe a very broad pattern (such as "<all_urls>").
  if (mMatchAboutBlank && aDoc.IsTopLevelOpaqueAboutBlank()) {
    if (mHasActiveTabPermission) {
      return true;
    }
    if (mMatches->MatchesAllWebUrls() && mIncludeGlobs.IsNull()) {
      // When mIncludeGlobs is present, mMatches does not necessarily match
      // everything (except possibly if include_globs is just ["*"]). So we
      // only match if mMatches is present without mIncludeGlobs.
      return true;
    }
    // Continue below: when mMatchOriginAsFallback is true, a null principal
    // with a precursor may result in a match with the specific pattern.
  }

  if (!mMatchOriginAsFallback && aDoc.RequiresMatchOriginAsFallback()) {
    // TODO bug 1899134: We should unconditionally return false here. But we
    // had accidental support for matching blob:-URLs (by the content
    // principal's URL) for a long time, so we have a temporary pref to fall
    // back to the original behavior if needed.
    if (aDoc.URL().Scheme() != nsGkAtoms::blob || !mExtension ||
        mExtension->ManifestVersion() != 2 ||
        !StaticPrefs::
            extensions_script_blob_without_match_origin_as_fallback()) {
      return false;
    }
    // Fall-through implies that we have a MV2 extension and a blob:-URL, with
    // extensions.script_blob_without_match_origin_as_fallback set to true.
  }

  if (mRestricted && WebExtensionPolicy::IsRestrictedDoc(aDoc)) {
    return false;
  }

  if (mRestricted && mExtension && mExtension->QuarantinedFromDoc(aDoc)) {
    return false;
  }

  auto& urlinfo = aDoc.PrincipalURL();
  if (mExtension && mExtension->ManifestVersion() >= 3) {
    // In MV3, activeTab only allows access to same-origin iframes.
    if (mHasActiveTabPermission && aDoc.IsSameOriginWithTop() &&
        MatchPattern::MatchesAllURLs(urlinfo)) {
      return mExtension->CheckGuarded(urlinfo).IsNull();
    }
  } else {
    if (mHasActiveTabPermission && aDoc.ShouldMatchActiveTabPermission() &&
        MatchPattern::MatchesAllURLs(urlinfo)) {
      return !mExtension || mExtension->CheckGuarded(urlinfo).IsNull();
    }
  }

  return MatchesURI(urlinfo, aIgnorePermissions, Some(aDoc));
}

bool MozDocumentMatcher::MatchesURI(const URLInfo& aURL,
                                    bool aIgnorePermissions,
                                    const Maybe<DocInfo>& aDoc) const {
  MOZ_ASSERT((!mRestricted && !mCheckPermissions) || mExtension);

  if (MOZ_LIKELY(!mIsUserScript)) {
    // mMatches must always match for normal content scripts.
    if (!mMatches->Matches(aURL)) {
      return false;
    }
    // mIncludeGlobs is optional, but if specified must match.
    if (!mIncludeGlobs.IsNull() &&
        !mIncludeGlobs.Value().Matches(aURL.CSpec())) {
      return false;
    }
  } else {
    // Unlike normal content scripts that match by (mMatches AND mIncludeGlobs),
    // user scripts accept if there is any match: (mMatches OR mIncludeGlobs).
    // This implies that mMatches does not have to be specified.
    // mMatches is not a Nullable because we want it to be specified for content
    // scripts (which is by far the most common case). An empty MatchPatternSet
    // is equivalent to an unspecified (non-matching) mMatches.
    if (!mMatches->Matches(aURL) &&
        (mIncludeGlobs.IsNull() ||
         !mIncludeGlobs.Value().Matches(aURL.CSpec()))) {
      return false;
    }
  }

  if (mExcludeMatches && mExcludeMatches->Matches(aURL)) {
    return false;
  }

  if (!mExcludeGlobs.IsNull() && mExcludeGlobs.Value().Matches(aURL.CSpec())) {
    return false;
  }

  if (mRestricted && WebExtensionPolicy::IsRestrictedURI(aURL)) {
    return false;
  }

  if (mRestricted && mExtension->QuarantinedFromURI(aURL)) {
    return false;
  }

  if (mCheckPermissions && !aIgnorePermissions) {
    // CanAccessURI() also calls CheckGuarded() internally.
    if (!mExtension->CanAccessURI(aURL, false, false, true)) {
      return false;
    }
  } else if (mExtension && !mExtension->CheckGuarded(aURL).IsNull()) {
    // Guards must be enforced regardless of mCheckPermissions.
    return false;
  }

  if (aURL.Scheme() == nsGkAtoms::file && mExtension &&
      !mExtension->FileSchemeAllowed()) {
    // Although CanAccessURI above accepts an aAllowFilePermission parameter,
    // the logic is conditional on mCheckPermissions. We want the file access
    // to be enforced unconditionally, so check it here instead.
    return false;
  }

  if (aURL.Scheme() == nsGkAtoms::moz_extension) {
    bool allowed = StaticPrefs::
        extensions_webextensions_allow_executeScript_in_moz_extension();

    // Logging the deprecation warning and collecting telemetry if
    // the call was originated from MozDocumentMatcher::Matches
    // (and skip it if the method MatchesURI was called directly
    // without passing the related DocInfo instance or if the
    // innerWindowID could not be retrieved).
    if (aDoc.isSome()) {
      uint64_t innerWindowID;
      nsresult rv = aDoc.value().GetInnerWindowID(&innerWindowID);
      if (NS_SUCCEEDED(rv)) {
        LogMozExtExecuteScriptDeprecationWarning(aURL, innerWindowID, allowed);

        // Record telemetry events when a moz-extension Window global
        // is matching a content script in addition to log the deprecation
        // warning in the webconsole.
        glean::extensions::match_moz_extension_document.Record(
            Some(glean::extensions::MatchMozExtensionDocumentExtra(
                Some(nsAtomCString(mExtension->Id()).get()),
                Some(aDoc.value().IsTopLevel()), Some(!allowed))));
      }
    }

    return allowed;
  }

  return true;
}

void MozDocumentMatcher::LogMozExtExecuteScriptDeprecationWarning(
    const URLInfo& aURL, uint64_t aInnerWindowID, bool aAllowed) const {
  nsCOMPtr<nsIConsoleService> console(
      do_GetService(NS_CONSOLESERVICE_CONTRACTID));
  NS_ENSURE_TRUE_VOID(console);

  nsCOMPtr<nsIScriptError> error(do_CreateInstance(NS_SCRIPTERROR_CONTRACTID));
  NS_ENSURE_TRUE_VOID(error);

  nsPrintfCString warnMsg(
      "Content Script execution in moz-extension document "
      "has been deprecated %s (Extension ID: %s).",
      aAllowed ? "and will be removed in Firefox 152"
               : "and it has been blocked",
      nsAtomCString(mExtension->Id()).get());

  nsresult rv = error->InitWithWindowID(
      NS_ConvertUTF8toUTF16(warnMsg), aURL.CSpec(), 0, 0,
      nsIScriptError::warningFlag, "content javascript"_ns, aInnerWindowID,
      true /* from chrome context */);
  NS_ENSURE_TRUE_VOID(NS_SUCCEEDED(rv));
  console->LogMessage(error);
}

bool MozDocumentMatcher::MatchesWindowGlobal(WindowGlobalChild& aWindow,
                                             bool aIgnorePermissions) const {
  if (aWindow.IsClosed() || !aWindow.IsCurrentGlobal()) {
    return false;
  }
  nsGlobalWindowInner* inner = aWindow.GetWindowGlobal();
  if (!inner || !inner->GetDocShell()) {
    return false;
  }
  return Matches(inner->GetOuterWindow(), aIgnorePermissions);
}

void MozDocumentMatcher::GetOriginAttributesPatterns(
    JSContext* aCx, JS::MutableHandle<JS::Value> aVal,
    ErrorResult& aError) const {
  if (!ToJSValue(aCx, mOriginAttributesPatterns, aVal)) {
    aError.NoteJSContextException(aCx);
  }
}

JSObject* MozDocumentMatcher::WrapObject(JSContext* aCx,
                                         JS::Handle<JSObject*> aGivenProto) {
  return MozDocumentMatcher_Binding::Wrap(aCx, this, aGivenProto);
}

JSObject* WebExtensionContentScript::WrapObject(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return WebExtensionContentScript_Binding::Wrap(aCx, this, aGivenProto);
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(MozDocumentMatcher, mMatches,
                                      mExcludeMatches, mExtension)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MozDocumentMatcher)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(MozDocumentMatcher)
NS_IMPL_CYCLE_COLLECTING_RELEASE(MozDocumentMatcher)

/*****************************************************************************
 * MozDocumentObserver
 *****************************************************************************/

/* static */
already_AddRefed<DocumentObserver> DocumentObserver::Constructor(
    GlobalObject& aGlobal, dom::MozDocumentCallback& aCallbacks) {
  RefPtr<DocumentObserver> matcher =
      new DocumentObserver(aGlobal.GetAsSupports(), aCallbacks);
  return matcher.forget();
}

void DocumentObserver::Observe(
    const dom::Sequence<OwningNonNull<MozDocumentMatcher>>& matchers,
    ErrorResult& aRv) {
  if (!EPS().RegisterObserver(*this)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }
  mMatchers.Clear();
  for (auto& matcher : matchers) {
    if (!mMatchers.AppendElement(matcher, fallible)) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
  }
}

void DocumentObserver::Disconnect() { (void)EPS().UnregisterObserver(*this); }

void DocumentObserver::NotifyMatch(MozDocumentMatcher& aMatcher,
                                   nsPIDOMWindowOuter* aWindow) {
  IgnoredErrorResult rv;
  mCallbacks->OnNewDocument(
      aMatcher, WindowProxyHolder(aWindow->GetBrowsingContext()), rv);
}

void DocumentObserver::NotifyMatch(MozDocumentMatcher& aMatcher,
                                   nsILoadInfo* aLoadInfo) {
  IgnoredErrorResult rv;
  mCallbacks->OnPreloadDocument(aMatcher, aLoadInfo, rv);
}

JSObject* DocumentObserver::WrapObject(JSContext* aCx,
                                       JS::Handle<JSObject*> aGivenProto) {
  return MozDocumentObserver_Binding::Wrap(aCx, this, aGivenProto);
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(DocumentObserver, mCallbacks, mMatchers,
                                      mParent)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DocumentObserver)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(DocumentObserver)
NS_IMPL_CYCLE_COLLECTING_RELEASE(DocumentObserver)

/*****************************************************************************
 * DocInfo
 *****************************************************************************/

DocInfo::DocInfo(const URLInfo& aURL, nsILoadInfo* aLoadInfo)
    : mURL(aURL), mObj(AsVariant(aLoadInfo)) {}

DocInfo::DocInfo(nsPIDOMWindowOuter* aWindow)
    : mURL(aWindow->GetDocumentURI()), mObj(AsVariant(aWindow)) {}

bool DocInfo::IsTopLevel() const {
  if (mIsTopLevel.isNothing()) {
    struct Matcher {
      bool operator()(Window aWin) {
        return aWin->GetBrowsingContext()->IsTop();
      }
      bool operator()(LoadInfo aLoadInfo) {
        return aLoadInfo->GetIsTopLevelLoad();
      }
    };
    mIsTopLevel.emplace(mObj.match(Matcher()));
  }
  return mIsTopLevel.ref();
}

bool DocInfo::IsTopLevelOpaqueAboutBlank() const {
  if (mIsTopLevelOpaqueAboutBlank.isNothing()) {
    struct Matcher {
      explicit Matcher(const DocInfo& aThis) : mThis(aThis) {}
      const DocInfo& mThis;

      bool operator()(Window aWin) {
        if (!mThis.IsTopLevel()) {
          return false;
        }

        bool isFinalAboutBlankDoc =
            mThis.URL().Scheme() == nsGkAtoms::about &&
            mThis.URL().Spec().EqualsLiteral("about:blank") &&
            // Exclude uncommitted initial about:blank to avoid matching
            // about:blank of pending non-blank loads in the parent process,
            // see bug 1901894.
            !aWin->GetDoc()->IsUncommittedInitialDocument();

        // Principal() is expected to never be nullptr given a Window.
        MOZ_ASSERT(mThis.Principal());

        return (isFinalAboutBlankDoc ||
                // TODO bug 1902635: drop support for toplevel data: here.
                mThis.URL().Scheme() == nsGkAtoms::data) &&
               mThis.Principal()->GetIsNullPrincipal();
      }
      bool operator()(LoadInfo aLoadInfo) {
        // The current implementation is unable to reliably estimate the
        // principal that the about:blank document will have. For about:blank
        // opened via web content, the opener document would have a similar
        // principal and preloading would already have been triggered through
        // that document (via match_about_blank or match_origin_as_fallback).
        //
        // Top-level documents opened by the user do not have an opener, and
        // will have a null principal, which is exactly the scenario for which
        // this IsTopLevelOpaqueAboutBlank() would ideally return true. Because
        // we cannot tell for certain whether this is the case, we do still not
        // preload for this case. In practice, only broadly matching content
        // scripts (<all_urls>) can match this, which means that any other
        // document load has most likely already triggered preloading.
        //
        // The non-preloading of about:blank is documented at
        // DocInfo::PrincipalURL() and covered by tests
        // test_preload_at_about_blank_iframe and test_preload_at_data_url in
        // toolkit/components/extensions/test/xpcshell/test_ext_contentscript_preloading.js
        return false;
      }
    };
    mIsTopLevelOpaqueAboutBlank.emplace(mObj.match(Matcher(*this)));
  }
  return mIsTopLevelOpaqueAboutBlank.ref();
}

bool WindowShouldMatchActiveTab(nsPIDOMWindowOuter* aWin) {
  WindowContext* wc = aWin->GetCurrentInnerWindow()->GetWindowContext();
  if (wc && wc->SameOriginWithTop()) {
    // If the frame is same-origin to top, accept the match regardless of
    // whether the frame was populated dynamically.
    return true;
  }
  for (; wc; wc = wc->GetParentWindowContext()) {
    BrowsingContext* bc = wc->GetBrowsingContext();
    if (bc->IsTopContent()) {
      return true;
    }

    if (bc->CreatedDynamically() || !wc->GetIsOriginalFrameSource()) {
      return false;
    }
  }
  MOZ_ASSERT_UNREACHABLE("Should reach top content before end of loop");
  return false;
}

bool DocInfo::ShouldMatchActiveTabPermission() const {
  struct Matcher {
    bool operator()(Window aWin) { return WindowShouldMatchActiveTab(aWin); }
    bool operator()(LoadInfo aLoadInfo) { return false; }
  };
  return mObj.match(Matcher());
}

bool DocInfo::IsSameOriginWithTop() const {
  struct Matcher {
    bool operator()(Window aWin) {
      WindowContext* wc = aWin->GetCurrentInnerWindow()->GetWindowContext();
      return wc && wc->SameOriginWithTop();
    }
    bool operator()(LoadInfo aLoadInfo) { return false; }
  };
  return mObj.match(Matcher());
}

uint64_t DocInfo::FrameID() const {
  if (mFrameID.isNothing()) {
    if (IsTopLevel()) {
      mFrameID.emplace(0);
    } else {
      struct Matcher {
        uint64_t operator()(Window aWin) {
          return aWin->GetBrowsingContext()->Id();
        }
        uint64_t operator()(LoadInfo aLoadInfo) {
          return aLoadInfo->GetBrowsingContextID();
        }
      };
      mFrameID.emplace(mObj.match(Matcher()));
    }
  }
  return mFrameID.ref();
}

nsresult DocInfo::GetInnerWindowID(uint64_t* aInnerWindowID) const {
  // This method is currently only going to retrieve the innerWindowID
  // for non-preloading scripts and a Window global is actually available.
  nsPIDOMWindowOuter* piWindow = GetWindow();
  NS_ENSURE_TRUE(piWindow, NS_ERROR_FAILURE);
  RefPtr<dom::Document> docNode = piWindow->GetDoc();
  NS_ENSURE_TRUE(docNode, NS_ERROR_FAILURE);
  *aInnerWindowID = docNode->InnerWindowID();
  return NS_OK;
}

nsIPrincipal* DocInfo::Principal() const {
  if (mPrincipal.isNothing()) {
    struct Matcher {
      explicit Matcher(const DocInfo& aThis) : mThis(aThis) {}
      const DocInfo& mThis;

      nsIPrincipal* operator()(Window aWin) {
        RefPtr<Document> doc = aWin->GetDoc();
        return doc->NodePrincipal();
      }
      nsIPrincipal* operator()(LoadInfo aLoadInfo) {
        // This method tries to return a principal when the principal cannot be
        // derived from URL(). See PrincipalURL().
        if (!(mThis.URL().InheritsPrincipal() ||
              aLoadInfo->GetForceInheritPrincipal())) {
          // E.g. http(s):-URLs, data:, or any other arbitrary scheme.
          return nullptr;
        }
        // E.g. about:srcdoc. In this case the principal cannot be derived from
        // the URL, so we return the most likely principal here.
        if (auto principal = aLoadInfo->PrincipalToInherit()) {
          return principal;
        }
        return aLoadInfo->TriggeringPrincipal();
      }
    };
    mPrincipal.emplace(mObj.match(Matcher(*this)));
  }
  return mPrincipal.ref();
}

const URLInfo& DocInfo::PrincipalURL() const {
  if (!Principal()) {
    // This is only possible via non-DOMWindow (see Principal()). We may end up
    // here via ExtensionPolicyService::CheckRequest(), called before a network
    // request ("http-on-opening-request" / "document-on-opening-request").
    // In practice, http(s):, about:srcdoc, data:, and blob: may reach here.
    // about:blank (and javascript:) does not enter this code path.
    //
    // Falling back to the URL is almost always correct, except in these cases:
    // - documents that end up having a null principal. We cannot know for sure,
    //   e.g. because it can be forced later by a http header (CSP sandbox).
    //   In this case we may preload when we should not.
    //
    // - URLs that contain the principal such as blob:-URLs. In theory we could
    //   extract the origin from the URL, but we do not for simplicity.
    //   In this case we do not preload even though we could.
    //   ( In practice, blob:-URLs can only be created and loaded by the same
    //     origin, so it is likely that the content script had been preloaded
    //     before for that document. )
    return URL();
  }

  if (mPrincipalURL.isNothing()) {
    nsIPrincipal* prin = Principal();
    nsCOMPtr<nsIPrincipal> precursor;
    if (prin->GetIsContentPrincipal()) {
      // Most common case.
      nsCOMPtr<nsIURI> uri;
      BasePrincipal::Cast(prin)->GetURI(getter_AddRefs(uri));
      mPrincipalURL.emplace(uri);
    } else if (prin->GetIsNullPrincipal() && !URL().IsNonOpaqueURL() &&
               (precursor = prin->GetPrecursorPrincipal()) &&
               precursor->GetIsContentPrincipal()) {
      // Use precursor from null principal, unless the URL itself is not opaque.
      // We want to use URL() when IsNonOpaqueURL() because the URL may have
      // more details such as path and query components, whereas the precursor
      // URI only has an origin.
      // This enables matching of sandboxed about:blank / about:srcdoc / blob:
      // when match_origin_as_fallback:true is used.
      nsCOMPtr<nsIURI> uri;
      BasePrincipal::Cast(precursor)->GetURI(getter_AddRefs(uri));
      mPrincipalURL.emplace(uri);
    } else {
      mPrincipalURL.emplace(URL());
    }
  }

  return mPrincipalURL.ref();
}

bool DocInfo::RequiresMatchOriginAsFallback() const {
  if (mRequiresMatchOriginAsFallback.isNothing()) {
    mRequiresMatchOriginAsFallback.emplace(
        // Special-case blob:-URLs because their principal is indistinguishable
        // from the principals that created them.
        URL().Scheme() == nsGkAtoms::blob ||
        (Principal() && Principal()->GetIsNullPrincipal() &&
         !URL().IsNonOpaqueURL()));
  }
  return mRequiresMatchOriginAsFallback.ref();
}

}  // namespace extensions
}  // namespace mozilla
