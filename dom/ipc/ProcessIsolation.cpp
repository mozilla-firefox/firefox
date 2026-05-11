/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ProcessIsolation.h"

#include "mozilla/AppShutdown.h"
#include "mozilla/Assertions.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ContentPrincipal.h"
#include "mozilla/ExtensionPolicyService.h"
#include "mozilla/Logging.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/PermissionManager.h"
#include "mozilla/Preferences.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_fission.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/RemoteType.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/extensions/WebExtensionPolicy.h"
#include "nsAboutProtocolUtils.h"
#include "nsDocShell.h"
#include "nsError.h"
#include "nsIChromeRegistry.h"
#include "nsIEnterprisePolicies.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIProtocolHandler.h"
#include "nsIXULRuntime.h"
#include "nsNetUtil.h"
#include "nsSHistory.h"
#include "nsServiceManagerUtils.h"
#include "nsURLHelper.h"

namespace mozilla::dom {

mozilla::LazyLogModule gProcessIsolationLog{"ProcessIsolation"};

namespace {

// Strategy used to determine whether or not a particular site should load into
// a webIsolated content process. The particular strategy chosen is controlled
// by the `fission.webContentIsolationStrategy` pref, which must hold one of the
// following values.
enum class WebContentIsolationStrategy : uint32_t {
  // All web content is loaded into a shared `web` content process. This is
  // similar to the non-Fission behaviour, however remote subframes may still
  // be used for sites with special isolation behaviour, such as extension or
  // mozillaweb content processes.
  IsolateNothing = 0,
  // Web content is always isolated into its own `webIsolated` content process
  // based on site-origin, and will only load in a shared `web` content process
  // if site-origin could not be determined.
  IsolateEverything = 1,
  // Only isolates web content loaded by sites which are considered "high
  // value". A site is considered "high value" if it has been granted a
  // `highValue*` permission by the permission manager, which is done in
  // response to certain actions.
  IsolateHighValue = 2,
};

/**
 * Helper class for caching the result of splitting prefs which are represented
 * as a comma-separated list of strings.
 */
struct CommaSeparatedPref {
 public:
  explicit constexpr CommaSeparatedPref(nsLiteralCString aPrefName)
      : mPrefName(aPrefName) {}

  void OnChange() {
    if (mValues) {
      mValues->Clear();
      nsAutoCString prefValue;
      if (NS_SUCCEEDED(Preferences::GetCString(mPrefName.get(), prefValue))) {
        for (const auto& value :
             nsCCharSeparatedTokenizer(prefValue, ',').ToRange()) {
          mValues->EmplaceBack(value);
        }
      }
    }
  }

  const nsTArray<nsCString>& Get() {
    if (!mValues) {
      mValues = new nsTArray<nsCString>;
      Preferences::RegisterCallbackAndCall(
          [](const char*, void* aData) {
            static_cast<CommaSeparatedPref*>(aData)->OnChange();
          },
          mPrefName, this);
      RunOnShutdown([this] {
        delete this->mValues;
        this->mValues = nullptr;
      });
    }
    return *mValues;
  }

  auto begin() { return Get().cbegin(); }
  auto end() { return Get().cend(); }

 private:
  nsLiteralCString mPrefName;
  nsTArray<nsCString>* MOZ_OWNING_REF mValues = nullptr;
};

CommaSeparatedPref sSeparatedMozillaDomains{
    "browser.tabs.remote.separatedMozillaDomains"_ns};

bool AllowJITForSiteOrigin(const nsACString& aSiteOriginNoSuffix,
                           WindowGlobalParent* aParentWindow) {
  nsresult rv;

  nsCOMPtr<nsIEnterprisePolicies> policyService =
      do_GetService("@mozilla.org/enterprisepolicies;1");
  if (!policyService) {
    return true;
  }

  nsAutoCString topSiteOriginNoSuffix(aSiteOriginNoSuffix);

  // If this is a subframe then use the principal of the top window.
  if (aParentWindow) {
    rv = aParentWindow->TopWindowContext()
             ->DocumentPrincipal()
             ->GetSiteOriginNoSuffix(topSiteOriginNoSuffix);
    if (NS_FAILED(rv)) {
      topSiteOriginNoSuffix = aSiteOriginNoSuffix;
    }
  }

  nsCOMPtr<nsIURI> topSite;
  rv = NS_NewURI(getter_AddRefs(topSite), topSiteOriginNoSuffix);
  NS_ENSURE_SUCCESS(rv, true);

  bool isJitAllowed = true;
  if (NS_FAILED(
          policyService->IsAllowedForURI("jit"_ns, topSite, &isJitAllowed))) {
    return true;
  }

  if (!isJitAllowed) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Debug,
            ("JIT is disabled for site %s by enterprise policy",
             topSiteOriginNoSuffix.get()));
  }

  return isJitAllowed;
}

/**
 * Certain URIs have special isolation behaviour, and need to be loaded within
 * specific process types.
 */
enum class IsolationBehavior {
  // This URI loads web content and should be treated as a content load, being
  // isolated based on the response principal if enabled.
  WebContent,
  // Forcibly load in a process with the "web" remote type. This will ignore the
  // response principal completely.
  // This is generally reserved for internal documents which are loaded in
  // content, but not in the privilegedabout content process.
  ForceWebRemoteType,
  // Load this URI in the privileged about content process.
  PrivilegedAbout,
  // Load this URI in the extension process.
  Extension,
  // Load this URI in the file content process.
  File,
  // Load this URI in the priviliged mozilla content process.
  PrivilegedMozilla,
  // Load this URI explicitly in the parent process.
  Parent,
  // Load this URI wherever the browsing context is currently loaded. This is
  // generally used for error pages.
  Anywhere,
  // May only be returned for subframes. Inherits the remote type of the parent
  // document which is embedding this document.
  Inherit,
  // Special case for the `about:reader` URI which should be loaded in the same
  // process which would be used for the "url" query parameter.
  AboutReader,
  // There was a fatal error, and the load should be aborted.
  Error,
};

/**
 * Returns a static string with the name of the given isolation behaviour. For
 * use in logging code.
 */
static const char* IsolationBehaviorName(IsolationBehavior aBehavior) {
  switch (aBehavior) {
    case IsolationBehavior::WebContent:
      return "WebContent";
    case IsolationBehavior::ForceWebRemoteType:
      return "ForceWebRemoteType";
    case IsolationBehavior::PrivilegedAbout:
      return "PrivilegedAbout";
    case IsolationBehavior::Extension:
      return "Extension";
    case IsolationBehavior::File:
      return "File";
    case IsolationBehavior::PrivilegedMozilla:
      return "PrivilegedMozilla";
    case IsolationBehavior::Parent:
      return "Parent";
    case IsolationBehavior::Anywhere:
      return "Anywhere";
    case IsolationBehavior::Inherit:
      return "Inherit";
    case IsolationBehavior::AboutReader:
      return "AboutReader";
    case IsolationBehavior::Error:
      return "Error";
    default:
      return "Unknown";
  }
}

/**
 * Returns a static string with the name of the given worker kind. For use in
 * logging code.
 */
static const char* WorkerKindName(WorkerKind aWorkerKind) {
  switch (aWorkerKind) {
    case WorkerKindDedicated:
      return "Dedicated";
    case WorkerKindShared:
      return "Shared";
    case WorkerKindService:
      return "Service";
    default:
      return "Unknown";
  }
}

/**
 * Check if a given URI has specialized process isolation behaviour, such as
 * needing to be loaded within a specific type of content process.
 *
 * When handling a navigation, this method will be called twice: first with the
 * channel's creation URI, and then it will be called with a result principal's
 * URI.
 */
static IsolationBehavior IsolationBehaviorForURI(nsIURI* aURI, bool aIsSubframe,
                                                 bool aForChannelCreationURI) {
  MOZ_ASSERT(NS_IsMainThread());

  nsAutoCString scheme;
  MOZ_ALWAYS_SUCCEEDS(aURI->GetScheme(scheme));

  if (scheme == "chrome"_ns) {
    // `chrome://` URIs are always loaded in the parent process, unless they
    // have opted in to loading in a content process. This is currently only
    // done in tests.
    //
    // FIXME: These flags should be removed from `chrome` URIs at some point.
    nsCOMPtr<nsIXULChromeRegistry> chromeReg =
        do_GetService("@mozilla.org/chrome/chrome-registry;1");
    bool mustLoadRemotely = false;
    if (NS_SUCCEEDED(chromeReg->MustLoadURLRemotely(aURI, &mustLoadRemotely)) &&
        mustLoadRemotely) {
      return IsolationBehavior::ForceWebRemoteType;
    }
    bool canLoadRemotely = false;
    if (NS_SUCCEEDED(chromeReg->CanLoadURLRemotely(aURI, &canLoadRemotely)) &&
        canLoadRemotely) {
      return IsolationBehavior::Anywhere;
    }
    return IsolationBehavior::Parent;
  }

  if (scheme == "about"_ns) {
    nsAutoCString path;
    MOZ_ALWAYS_SUCCEEDS(NS_GetAboutModuleName(aURI, path));

    // The `about:blank` and `about:srcdoc` pages are loaded by normal web
    // content, and should be allocated processes based on their simple content
    // principals.
    if (path == "blank"_ns || path == "srcdoc"_ns) {
      MOZ_ASSERT(NS_IsContentAccessibleAboutURI(aURI));
      return IsolationBehavior::WebContent;
    }

    MOZ_ASSERT(!NS_IsContentAccessibleAboutURI(aURI));
    // If we're loading an `about:reader` URI, perform isolation based on the
    // principal of the URI being loaded.
    if (path == "reader"_ns && aForChannelCreationURI) {
      return IsolationBehavior::AboutReader;
    }

    // Otherwise, we're going to be loading an about: page. Consult the module.
    nsCOMPtr<nsIAboutModule> aboutModule;
    if (NS_FAILED(NS_GetAboutModule(aURI, getter_AddRefs(aboutModule))) ||
        !aboutModule) {
      // If we don't know of an about: module for this load, it's going to end
      // up being a network error. Allow the load to finish as normal.
      return IsolationBehavior::WebContent;
    }

    // NOTE: about modules can be implemented in JS, so this may run script, and
    // therefore can spuriously fail.
    uint32_t flags = 0;
    if (NS_FAILED(aboutModule->GetURIFlags(aURI, &flags))) {
      NS_WARNING(
          "nsIAboutModule::GetURIFlags unexpectedly failed. Abort the load");
      return IsolationBehavior::Error;
    }

    if (flags & nsIAboutModule::URI_MUST_LOAD_IN_EXTENSION_PROCESS) {
      return IsolationBehavior::Extension;
    }

    if (flags & nsIAboutModule::URI_MUST_LOAD_IN_CHILD) {
      if (flags & nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS) {
        return IsolationBehavior::PrivilegedAbout;
      }
      return IsolationBehavior::ForceWebRemoteType;
    }

    if (flags & nsIAboutModule::URI_CAN_LOAD_IN_CHILD) {
      return IsolationBehavior::Anywhere;
    }

    return IsolationBehavior::Parent;
  }

  // If the test-only `dataUriInDefaultWebProcess` pref is enabled, dump all
  // `data:` URIs in a "web" content process, rather than loading them in
  // content processes based on their precursor origins.
  if (StaticPrefs::browser_tabs_remote_dataUriInDefaultWebProcess() &&
      scheme == "data"_ns) {
    return IsolationBehavior::ForceWebRemoteType;
  }

  // Make sure to unwrap nested URIs before we early return for channel creation
  // URI. The checks past this point are intended to operate on the principal,
  // which has it's origin constructed from the innermost URI.
  nsCOMPtr<nsIURI> inner;
  if (nsCOMPtr<nsINestedURI> nested = do_QueryInterface(aURI);
      nested && NS_SUCCEEDED(nested->GetInnerURI(getter_AddRefs(inner)))) {
    return IsolationBehaviorForURI(inner, aIsSubframe, aForChannelCreationURI);
  }

  // If we're doing the initial check based on the channel creation URI, stop
  // here as we want to only perform the following checks on the true channel
  // result principal.
  if (aForChannelCreationURI) {
    return IsolationBehavior::WebContent;
  }

  // Protocols used by Thunderbird to display email messages.
  if (scheme == "imap"_ns || scheme == "mailbox"_ns || scheme == "news"_ns ||
      scheme == "nntp"_ns || scheme == "snews"_ns || scheme == "x-moz-ews"_ns ||
      scheme == "x-moz-graph"_ns) {
    return IsolationBehavior::Parent;
  }

  // There is more handling for extension content processes in the caller, but
  // they should load in an extension content process unless we're loading a
  // subframe.
  if (scheme == "moz-extension"_ns) {
    if (aIsSubframe) {
      // As a temporary measure, extension iframes must be loaded within the
      // same process as their parent document.
      return IsolationBehavior::Inherit;
    }
    return IsolationBehavior::Extension;
  }

  if (scheme == "file"_ns) {
    return IsolationBehavior::File;
  }

  // Check if the URI is listed as a privileged mozilla content process.
  if (scheme == "https"_ns &&
      StaticPrefs::
          browser_tabs_remote_separatePrivilegedMozillaWebContentProcess()) {
    nsAutoCString host;
    if (NS_SUCCEEDED(aURI->GetAsciiHost(host))) {
      // This code is duplicated in E10SUtils.sys.mjs, please update both
      for (const auto& separatedDomain : sSeparatedMozillaDomains) {
        // If the domain exactly matches our host, or our host ends with "." +
        // separatedDomain, we consider it matching.
        if (separatedDomain == host ||
            (separatedDomain.Length() < host.Length() &&
             host.CharAt(host.Length() - separatedDomain.Length() - 1) == '.' &&
             StringEndsWith(host, separatedDomain))) {
          return IsolationBehavior::PrivilegedMozilla;
        }
      }
    }
  }

  nsCOMPtr<nsIScriptSecurityManager> secMan =
      nsContentUtils::GetSecurityManager();
  bool inFileURIAllowList = false;
  if (NS_SUCCEEDED(secMan->InFileURIAllowlist(aURI, &inFileURIAllowList)) &&
      inFileURIAllowList) {
    return IsolationBehavior::File;
  }

  return IsolationBehavior::WebContent;
}

/**
 * Helper method for logging the origin of a principal as a string.
 */
static nsAutoCString OriginString(nsIPrincipal* aPrincipal) {
  nsAutoCString origin;
  aPrincipal->GetOrigin(origin);
  return origin;
}

/**
 * Trim the OriginAttributes from aPrincipal, and use it to create a
 * OriginSuffix string appropriate to use within a remoteType string.
 */
static nsAutoCString OriginSuffixForRemoteType(nsIPrincipal* aPrincipal,
                                               bool aDisableJit) {
  nsAutoCString originSuffix;
  OriginAttributes attrs = aPrincipal->OriginAttributesRef();
  attrs.StripAttributes(OriginAttributes::STRIP_FIRST_PARTY_DOMAIN |
                        OriginAttributes::STRIP_PARITION_KEY);
  attrs.CreateSuffix(originSuffix);

  if (aDisableJit) {
    if (originSuffix.IsEmpty()) {
      originSuffix = "^"_ns + DISABLE_JIT_REMOTE_TYPE_SUFFIX;
    } else {
      originSuffix += "&"_ns + DISABLE_JIT_REMOTE_TYPE_SUFFIX;
    }
  }

  return originSuffix;
}

/**
 * Given an about:reader URI, extract the "url" query parameter, and use it to
 * construct a principal which should be used for process selection.
 */
static already_AddRefed<BasePrincipal> GetAboutReaderURLPrincipal(
    nsIURI* aURI, const OriginAttributes& aAttrs) {
#ifdef DEBUG
  MOZ_ASSERT(aURI->SchemeIs("about"));
  nsAutoCString path;
  MOZ_ALWAYS_SUCCEEDS(NS_GetAboutModuleName(aURI, path));
  MOZ_ASSERT(path == "reader"_ns);
#endif

  nsAutoCString query;
  MOZ_ALWAYS_SUCCEEDS(aURI->GetQuery(query));

  // Extract the "url" parameter from the `about:reader`'s query parameters,
  // and recover a content principal from it.
  nsAutoCString readerSpec;
  if (URLParams::Extract(query, "url"_ns, readerSpec)) {
    nsCOMPtr<nsIURI> readerUri;
    if (NS_SUCCEEDED(NS_NewURI(getter_AddRefs(readerUri), readerSpec))) {
      return BasePrincipal::CreateContentPrincipal(readerUri, aAttrs);
    }
  }
  return nullptr;
}

/**
 * Check the Cross-Origin-Opener-Policy of the given channel or ancestor
 * BrowsingContext, checking if the response should be cross-origin isolated.
 */
static bool ShouldCrossOriginIsolate(nsIChannel* aChannel,
                                     WindowGlobalParent* aParentWindow) {
  nsILoadInfo::CrossOriginOpenerPolicy coop =
      nsILoadInfo::OPENER_POLICY_UNSAFE_NONE;
  if (aParentWindow) {
    coop = aParentWindow->BrowsingContext()->Top()->GetOpenerPolicy();
  } else if (nsCOMPtr<nsIHttpChannelInternal> httpChannel =
                 do_QueryInterface(aChannel)) {
    MOZ_ALWAYS_SUCCEEDS(httpChannel->GetCrossOriginOpenerPolicy(&coop));
  }
  return coop ==
         nsILoadInfo::OPENER_POLICY_SAME_ORIGIN_EMBEDDER_POLICY_REQUIRE_CORP;
}

/**
 * Returns `true` if loads for this site should be isolated on a per-site basis.
 * If `aTopBC` is nullptr, this is being called to check if a shared or service
 * worker should be isolated.
 */
static bool ShouldIsolateSite(nsIPrincipal* aPrincipal,
                              bool aUseRemoteSubframes) {
  // If Fission is disabled, we never want to isolate. We check the toplevel BC
  // if it's available, or the global pref if checking for shared or service
  // workers.
  if (!aUseRemoteSubframes) {
    return false;
  }

  // non-content principals currently can't have webIsolated remote types
  // assigned to them, so should not be isolated.
  if (!aPrincipal->GetIsContentPrincipal()) {
    return false;
  }

  switch (WebContentIsolationStrategy(
      StaticPrefs::fission_webContentIsolationStrategy())) {
    case WebContentIsolationStrategy::IsolateNothing:
      MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
              ("Not isolating '%s' as isolation is disabled",
               OriginString(aPrincipal).get()));
      return false;
    case WebContentIsolationStrategy::IsolateEverything:
      MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
              ("Isolating '%s' as isolation is enabled for all sites",
               OriginString(aPrincipal).get()));
      return true;
    case WebContentIsolationStrategy::IsolateHighValue: {
      RefPtr<PermissionManager> perms = PermissionManager::GetInstance();
      if (NS_WARN_IF(!perms)) {
        // If we somehow have no permission manager, fall back to the safest
        // option, and try to isolate.
        MOZ_ASSERT_UNREACHABLE("Permission manager is missing");
        return true;
      }

      static constexpr nsLiteralCString kHighValuePermissions[] = {
          mozilla::dom::kHighValueCOOPPermission,
          mozilla::dom::kHighValueHasSavedLoginPermission,
          mozilla::dom::kHighValueIsLoggedInPermission,
      };

      for (const auto& type : kHighValuePermissions) {
        uint32_t permission = nsIPermissionManager::UNKNOWN_ACTION;
        if (NS_SUCCEEDED(perms->TestPermissionFromPrincipal(aPrincipal, type,
                                                            &permission)) &&
            permission == nsIPermissionManager::ALLOW_ACTION) {
          MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
                  ("Isolating '%s' due to high-value permission '%s'",
                   OriginString(aPrincipal).get(), type.get()));
          return true;
        }
      }
      MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
              ("Not isolating '%s' as it is not high-value",
               OriginString(aPrincipal).get()));
      return false;
    }
    default:
      // An invalid pref value was used. Fall back to the safest option and
      // isolate everything.
      NS_WARNING("Invalid pref value for fission.webContentIsolationStrategy");
      MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
              ("Isolating '%s' due to unknown strategy pref value",
               OriginString(aPrincipal).get()));
      return true;
  }
}

static Result<nsCString, nsresult> SpecialBehaviorRemoteType(
    IsolationBehavior aBehavior, const nsACString& aCurrentRemoteType,
    WindowGlobalParent* aParentWindow) {
  switch (aBehavior) {
    case IsolationBehavior::ForceWebRemoteType:
      return {WEB_REMOTE_TYPE};
    case IsolationBehavior::PrivilegedAbout:
      // The privileged about: content process cannot be disabled, as it
      // causes various actors to break.
      return {PRIVILEGEDABOUT_REMOTE_TYPE};
    case IsolationBehavior::Extension:
      if (ExtensionPolicyService::GetSingleton().UseRemoteExtensions()) {
        return {EXTENSION_REMOTE_TYPE};
      }
      return {NOT_REMOTE_TYPE};
    case IsolationBehavior::File:
      if (StaticPrefs::browser_tabs_remote_separateFileUriProcess()) {
        return {FILE_REMOTE_TYPE};
      }
      return {WEB_REMOTE_TYPE};
    case IsolationBehavior::PrivilegedMozilla:
      return {PRIVILEGEDMOZILLA_REMOTE_TYPE};
    case IsolationBehavior::Parent:
      return {NOT_REMOTE_TYPE};
    case IsolationBehavior::Anywhere:
      return {nsCString(aCurrentRemoteType)};
    case IsolationBehavior::Inherit:
      MOZ_DIAGNOSTIC_ASSERT(aParentWindow);
      return {nsCString(aParentWindow->GetRemoteType())};

    case IsolationBehavior::Error:
      return Err(NS_ERROR_UNEXPECTED);

    default:
      MOZ_ASSERT_UNREACHABLE();
      return Err(NS_ERROR_UNEXPECTED);
  }
}

enum class WebProcessType {
  Web,
  WebIsolated,
  WebCoopCoep,
};

}  // namespace

Result<NavigationIsolationOptions, nsresult> IsolationOptionsForNavigation(
    CanonicalBrowsingContext* aTopBC, WindowGlobalParent* aParentWindow,
    nsIURI* aChannelCreationURI, nsIChannel* aChannel,
    const nsACString& aCurrentRemoteType, bool aHasCOOPMismatch,
    bool aForNewTab, uint32_t aLoadStateLoadType,
    const Maybe<uint64_t>& aChannelId,
    const Maybe<nsCString>& aRemoteTypeOverride) {
  // Get the final principal, used to select which process to load into.
  nsCOMPtr<nsIPrincipal> resultPrincipal;
  nsresult rv = nsContentUtils::GetSecurityManager()->GetChannelResultPrincipal(
      aChannel, getter_AddRefs(resultPrincipal));
  if (NS_FAILED(rv)) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Error,
            ("failed to get channel result principal"));
    return Err(rv);
  }

  MOZ_LOG(
      gProcessIsolationLog, LogLevel::Verbose,
      ("IsolationOptionsForNavigation principal:%s, uri:%s, parentUri:%s",
       OriginString(resultPrincipal).get(),
       aChannelCreationURI->GetSpecOrDefault().get(),
       aParentWindow ? aParentWindow->GetDocumentURI()->GetSpecOrDefault().get()
                     : ""));

  // If we're loading a null principal, we can't easily make a process
  // selection decision off ot it. Instead, we'll use our null principal's
  // precursor principal to make process selection decisions.
  bool isNullPrincipalPrecursor = false;
  nsCOMPtr<nsIPrincipal> resultOrPrecursor(resultPrincipal);
  if (nsCOMPtr<nsIPrincipal> precursor =
          resultOrPrecursor->GetPrecursorPrincipal()) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
            ("using null principal precursor origin %s",
             OriginString(precursor).get()));
    resultOrPrecursor = precursor;
    isNullPrincipalPrecursor = true;
  }

  NavigationIsolationOptions options;
  options.mReplaceBrowsingContext = aHasCOOPMismatch;
  options.mShouldCrossOriginIsolate =
      ShouldCrossOriginIsolate(aChannel, aParentWindow);

  // Check if this load has an explicit remote type override. This is used to
  // perform an about:blank load within a specific content process.
  if (aRemoteTypeOverride) {
    MOZ_DIAGNOSTIC_ASSERT(
        NS_IsAboutBlank(aChannelCreationURI),
        "Should only have aRemoteTypeOverride for about:blank URIs");
    if (NS_WARN_IF(!resultPrincipal->GetIsNullPrincipal())) {
      MOZ_LOG(gProcessIsolationLog, LogLevel::Error,
              ("invalid remote type override on non-null principal"));
      return Err(NS_ERROR_DOM_SECURITY_ERR);
    }

    MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
            ("using remote type override (%s) for load",
             aRemoteTypeOverride->get()));
    options.mRemoteType = *aRemoteTypeOverride;
    return options;
  }

  // First, check for any special cases which should be handled using the
  // channel creation URI, and handle them.
  auto behavior = IsolationBehaviorForURI(aChannelCreationURI, aParentWindow,
                                          /* aForChannelCreationURI */ true);
  MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
          ("Channel Creation Isolation Behavior: %s",
           IsolationBehaviorName(behavior)));

  // In the about:reader special case, we want to fetch the relevant information
  // from the URI, an then treat it as a normal web content load.
  if (behavior == IsolationBehavior::AboutReader) {
    if (RefPtr<BasePrincipal> readerURIPrincipal = GetAboutReaderURLPrincipal(
            aChannelCreationURI, resultOrPrecursor->OriginAttributesRef())) {
      MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
              ("using about:reader's url origin %s",
               OriginString(readerURIPrincipal).get()));
      resultOrPrecursor = readerURIPrincipal;
    }
    behavior = IsolationBehavior::WebContent;
    // If loading an about:reader page in a BrowsingContext which shares a
    // BrowsingContextGroup with other toplevel documents, replace the
    // BrowsingContext to destroy any references.
    // With SHIP we can apply this to all about:reader loads.
    options.mReplaceBrowsingContext = true;
  }

  // If we're running in a test which is requesting that system-triggered
  // about:blank documents load within the current process, override the
  // behaviour for loads which meet the requirements.
  if (StaticPrefs::browser_tabs_remote_systemTriggeredAboutBlankAnywhere() &&
      NS_IsAboutBlank(aChannelCreationURI)) {
    nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
    if (loadInfo->TriggeringPrincipal()->IsSystemPrincipal() &&
        resultOrPrecursor->GetIsNullPrincipal()) {
      MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
              ("Forcing system-principal triggered about:blank load to "
               "complete in the current process"));
      behavior = IsolationBehavior::Anywhere;
    }
  }

#ifdef MOZ_WIDGET_ANDROID
  // If we're loading an error page on android, it must complete within the same
  // process as the errored page load would complete in due to code expecting
  // that behavior. See bug 1673763.
  if (aLoadStateLoadType == LOAD_ERROR_PAGE) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
            ("Forcing error page load to complete in the current process"));
    behavior = IsolationBehavior::Anywhere;
  }
#endif

  // If we're loading for a specific extension, we'll need to perform a
  // BCG-switching load to get our toplevel extension window in the correct
  // BrowsingContextGroup.
  if (auto* addonPolicy =
          BasePrincipal::Cast(resultOrPrecursor)->AddonPolicy()) {
    if (aParentWindow) {
      // As a temporary measure, extension iframes must be loaded within the
      // same process as their parent document.
      MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
              ("Loading extension subframe in same process as parent"));
      behavior = IsolationBehavior::Inherit;
    } else {
      MOZ_LOG(
          gProcessIsolationLog, LogLevel::Verbose,
          ("Found extension frame with addon policy. Will use group id %" PRIx64
           " (currentId: %" PRIx64 ")",
           addonPolicy->GetBrowsingContextGroupId(), aTopBC->Group()->Id()));
      behavior = IsolationBehavior::Extension;
      if (aTopBC->Group()->Id() != addonPolicy->GetBrowsingContextGroupId()) {
        options.mReplaceBrowsingContext = true;
        options.mSpecificGroupId = addonPolicy->GetBrowsingContextGroupId();
      }
    }
  }

  // Do a second run of `GetIsolationBehavior`, this time using the
  // principal's URI to handle additional special cases such as the file and
  // privilegedmozilla content process.
  if (behavior == IsolationBehavior::WebContent) {
    if (resultOrPrecursor->IsSystemPrincipal()) {
      // We're loading something with a system principal which isn't caught in
      // one of our other edge-cases. If the load started in the parent process,
      // and it's safe for it to end in the parent process, we should finish the
      // load there.
      bool isUIResource = false;
      if (aCurrentRemoteType.IsEmpty() &&
          (aChannelCreationURI->SchemeIs("about") ||
           (NS_SUCCEEDED(NS_URIChainHasFlags(
                aChannelCreationURI, nsIProtocolHandler::URI_IS_UI_RESOURCE,
                &isUIResource)) &&
            isUIResource))) {
        behavior = IsolationBehavior::Parent;
      } else {
        // In general, we don't want to load documents with a system principal
        // in a content process, however we need to in some cases, such as when
        // loading blob: URLs created by system code. We can force the load to
        // finish in a content process instead.
        behavior = IsolationBehavior::ForceWebRemoteType;
      }
    } else if (nsCOMPtr<nsIURI> principalURI = resultOrPrecursor->GetURI()) {
      behavior = IsolationBehaviorForURI(principalURI, aParentWindow,
                                         /* aForChannelCreationURI */ false);
    }
  }

  // If we're currently loaded in the extension process, and are going to switch
  // to some other remote type, make sure we leave the extension's BCG which we
  // may have entered earlier to separate extension and non-extension BCGs from
  // each-other.
  if (!aParentWindow && aCurrentRemoteType == EXTENSION_REMOTE_TYPE &&
      behavior != IsolationBehavior::Extension &&
      behavior != IsolationBehavior::Anywhere) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
            ("Forcing BC replacement to leave extension BrowsingContextGroup "
             "%" PRIx64 " on navigation",
             aTopBC->Group()->Id()));
    options.mReplaceBrowsingContext = true;
  }

  // We don't want to load documents with sandboxed null principals, like
  // `data:` URIs, in the parent process, even if they were created by a
  // document which would otherwise be loaded in the parent process.
  if (behavior == IsolationBehavior::Parent && isNullPrincipalPrecursor) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Debug,
            ("Ensuring sandboxed null-principal load doesn't occur in the "
             "parent process"));
    behavior = IsolationBehavior::ForceWebRemoteType;
  }

  MOZ_LOG(
      gProcessIsolationLog, LogLevel::Debug,
      ("Using IsolationBehavior %s for %s (original uri %s)",
       IsolationBehaviorName(behavior), OriginString(resultOrPrecursor).get(),
       aChannelCreationURI->GetSpecOrDefault().get()));

  // Check if we can put the previous document into the BFCache.
  if (mozilla::BFCacheInParent() && nsSHistory::GetMaxTotalViewers() > 0 &&
      !aForNewTab && !aParentWindow && !aTopBC->HadOriginalOpener() &&
      behavior != IsolationBehavior::Parent &&
      (ExtensionPolicyService::GetSingleton().UseRemoteExtensions() ||
       behavior != IsolationBehavior::Extension) &&
      !aCurrentRemoteType.IsEmpty() &&
      aTopBC->GetHasLoadedNonInitialDocument() &&
      (aLoadStateLoadType == LOAD_NORMAL ||
       aLoadStateLoadType == LOAD_HISTORY || aLoadStateLoadType == LOAD_LINK ||
       aLoadStateLoadType == LOAD_STOP_CONTENT ||
       aLoadStateLoadType == LOAD_STOP_CONTENT_AND_REPLACE) &&
      (!aTopBC->GetActiveSessionHistoryEntry() ||
       aTopBC->GetActiveSessionHistoryEntry()->GetSaveLayoutStateFlag())) {
    if (nsCOMPtr<nsIURI> uri = aTopBC->GetCurrentURI()) {
      MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
              ("current uri: %s", uri->GetSpecOrDefault().get()));
    }
    options.mTryUseBFCache =
        aTopBC->AllowedInBFCache(aChannelId, aChannelCreationURI);
    if (options.mTryUseBFCache) {
      options.mReplaceBrowsingContext = true;
      options.mActiveSessionHistoryEntry =
          aTopBC->GetActiveSessionHistoryEntry();
    }
  }

  // If the load has any special remote type handling, do so at this point.
  if (behavior != IsolationBehavior::WebContent) {
    options.mRemoteType = MOZ_TRY(
        SpecialBehaviorRemoteType(behavior, aCurrentRemoteType, aParentWindow));

    if (options.mRemoteType != aCurrentRemoteType &&
        (options.mRemoteType.IsEmpty() || aCurrentRemoteType.IsEmpty())) {
      options.mReplaceBrowsingContext = true;
    }

    MOZ_LOG(
        gProcessIsolationLog, LogLevel::Debug,
        ("Selecting specific remote type (%s) due to a special case isolation "
         "behavior %s",
         options.mRemoteType.get(), IsolationBehaviorName(behavior)));
    return options;
  }

  // At this point we're definitely not going to be loading in the parent
  // process anymore, so we're definitely going to be replacing BrowsingContext
  // if we're in the parent process.
  if (aCurrentRemoteType.IsEmpty()) {
    MOZ_ASSERT(!aParentWindow);
    options.mReplaceBrowsingContext = true;
  }

  // NOTE: Currently we always perform process isolation based on the
  // siteOrigin, not based on the full origin, even if the
  // `Origin-Agent-Cluster` header is provided and we are keying DocGroups
  // by-origin.
  //
  // If in the future we want to start keying based on full origin in some
  // cases, the logic below will need to be updated to handle this. Note that
  // the UseOriginAgentCluster bit may not have been set on the
  // BrowsingContextGroup when this check is being evaluated (as it is set after
  // process selection, which may cause a BrowsingContextGroup switch).

  nsAutoCString siteOriginNoSuffix;
  MOZ_TRY(resultOrPrecursor->GetSiteOriginNoSuffix(siteOriginNoSuffix));

  // Check if we've already loaded a document with the given principal in some
  // content process. We want to finish the load in the same process in that
  // case.
  //
  // The exception to that is with extension loads and the system principal,
  // where we may have multiple documents with the same principal in different
  // processes. Those have been handled above, and will not be reaching here.
  //
  // If we're doing a replace load or opening a new tab, we won't be staying in
  // the same BrowsingContextGroup, so ignore this step.
  if (!options.mReplaceBrowsingContext && !aForNewTab) {
    // Helper for efficiently determining if a given origin is same-site. This
    // will attempt to do a fast equality check, and will only fall back to
    // computing the site-origin for content principals.
    auto principalIsSameSite = [&](nsIPrincipal* aDocumentPrincipal) -> bool {
      // If we're working with a null principal with a precursor, compare
      // precursors, as `resultOrPrecursor` has already been stripped to its
      // precursor.
      nsCOMPtr<nsIPrincipal> documentPrincipal(aDocumentPrincipal);
      if (nsCOMPtr<nsIPrincipal> precursor =
              documentPrincipal->GetPrecursorPrincipal()) {
        documentPrincipal = precursor;
      }

      // First, attempt to use `Equals` to compare principals, and if that
      // fails compare siteOrigins. Only compare siteOrigin for content
      // principals, as non-content principals will never have siteOrigin !=
      // origin.
      nsAutoCString documentSiteOrigin;
      return resultOrPrecursor->Equals(documentPrincipal) ||
             (documentPrincipal->GetIsContentPrincipal() &&
              resultOrPrecursor->GetIsContentPrincipal() &&
              NS_SUCCEEDED(documentPrincipal->GetSiteOriginNoSuffix(
                  documentSiteOrigin)) &&
              documentSiteOrigin == siteOriginNoSuffix);
    };

    // XXX: Consider also checking in-flight process switches to see if any have
    // matching principals?
    AutoTArray<RefPtr<BrowsingContext>, 8> contexts;
    aTopBC->Group()->GetToplevels(contexts);
    while (!contexts.IsEmpty()) {
      auto bc = contexts.PopLastElement();
      for (const auto& wc : bc->GetWindowContexts()) {
        WindowGlobalParent* wgp = wc->Canonical();

        // Check if this WindowGlobalParent has the given resultPrincipal, and
        // if it does, we need to load in that process.
        if (!wgp->GetRemoteType().IsEmpty() &&
            principalIsSameSite(wgp->DocumentPrincipal())) {
          MOZ_LOG(gProcessIsolationLog, LogLevel::Debug,
                  ("Found existing frame with matching principal "
                   "(remoteType:(%s), origin:%s)",
                   PromiseFlatCString(wgp->GetRemoteType()).get(),
                   OriginString(wgp->DocumentPrincipal()).get()));
          options.mRemoteType = wgp->GetRemoteType();
          return options;
        }

        // Also enumerate over this WindowContexts' subframes.
        contexts.AppendElements(wc->Children());
      }
    }
  }

  bool isJitAllowed = AllowJITForSiteOrigin(siteOriginNoSuffix, aParentWindow);
  nsAutoCString originSuffix =
      OriginSuffixForRemoteType(resultOrPrecursor, !isJitAllowed);

  WebProcessType webProcessType = WebProcessType::Web;
  if (ShouldIsolateSite(resultOrPrecursor, aTopBC->UseRemoteSubframes())) {
    webProcessType = WebProcessType::WebIsolated;
  }

  // Check if we should be cross-origin isolated.
  if (options.mShouldCrossOriginIsolate) {
    webProcessType = WebProcessType::WebCoopCoep;
  }

  switch (webProcessType) {
    case WebProcessType::Web:
      options.mRemoteType = WEB_REMOTE_TYPE;
      break;
    case WebProcessType::WebIsolated:
      options.mRemoteType =
          FISSION_WEB_REMOTE_TYPE "="_ns + siteOriginNoSuffix + originSuffix;
      break;
    case WebProcessType::WebCoopCoep:
      options.mRemoteType =
          WITH_COOP_COEP_REMOTE_TYPE "="_ns + siteOriginNoSuffix + originSuffix;
      break;
  }
  return options;
}

Result<WorkerIsolationOptions, nsresult> IsolationOptionsForWorker(
    nsIPrincipal* aPrincipal, WorkerKind aWorkerKind,
    const nsACString& aCurrentRemoteType, bool aUseRemoteSubframes) {
  MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
          ("IsolationOptionsForWorker principal:%s, kind:%s, current:%s",
           OriginString(aPrincipal).get(), WorkerKindName(aWorkerKind),
           PromiseFlatCString(aCurrentRemoteType).get()));

  MOZ_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(
      aWorkerKind == WorkerKindService || aWorkerKind == WorkerKindShared,
      "Unexpected remote worker kind");

  if (aWorkerKind == WorkerKindService &&
      !aPrincipal->GetIsContentPrincipal()) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
            ("Rejecting service worker with non-content principal"));
    return Err(NS_ERROR_UNEXPECTED);
  }

  if (aPrincipal->GetIsExpandedPrincipal()) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
            ("Rejecting remote worker with expanded principal"));
    return Err(NS_ERROR_UNEXPECTED);
  }

  // In some cases, such as for null principals without precursors, we will want
  // to load a shared worker in a process based on the current process. This is
  // not done for service workers - process selection for those should function
  // the same in all processes.
  //
  // We only allow the current remote type to be used if it is not a COOP+COEP
  // remote type, in order to avoid loading a shared worker in one of these
  // processes. Currently process selection for workers occurs before response
  // headers are available, so we will never select to load a shared worker in a
  // COOP+COEP content process.
  nsCString preferredRemoteType = DEFAULT_REMOTE_TYPE;
  if (aWorkerKind == WorkerKind::WorkerKindShared &&
      !StringBeginsWith(aCurrentRemoteType,
                        WITH_COOP_COEP_REMOTE_TYPE_PREFIX)) {
    preferredRemoteType = aCurrentRemoteType;
  }

  WorkerIsolationOptions options;

  // If we're loading a null principal, we can't easily make a process
  // selection decision off ot it. Instead, we'll use our null principal's
  // precursor principal to make process selection decisions.
  bool isNullPrincipalPrecursor = false;
  nsCOMPtr<nsIPrincipal> resultOrPrecursor(aPrincipal);
  if (nsCOMPtr<nsIPrincipal> precursor =
          resultOrPrecursor->GetPrecursorPrincipal()) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
            ("using null principal precursor origin %s",
             OriginString(precursor).get()));
    resultOrPrecursor = precursor;
    isNullPrincipalPrecursor = true;
  }

  IsolationBehavior behavior = IsolationBehavior::WebContent;
  if (resultOrPrecursor->GetIsContentPrincipal()) {
    nsCOMPtr<nsIURI> uri = resultOrPrecursor->GetURI();
    behavior = IsolationBehaviorForURI(uri, /* aIsSubframe */ false,
                                       /* aForChannelCreationURI */ false);
  } else if (resultOrPrecursor->IsSystemPrincipal()) {
    MOZ_ASSERT(aWorkerKind == WorkerKindShared);

    // Allow system principal shared workers to load within either the
    // parent process or privilegedabout process, depending on the
    // responsible process.
    if (preferredRemoteType == NOT_REMOTE_TYPE) {
      MOZ_LOG(gProcessIsolationLog, LogLevel::Debug,
              ("Loading system principal shared worker in parent process"));
      behavior = IsolationBehavior::Parent;
    } else if (preferredRemoteType == PRIVILEGEDABOUT_REMOTE_TYPE) {
      MOZ_LOG(gProcessIsolationLog, LogLevel::Debug,
              ("Loading system principal shared worker in privilegedabout "
               "process"));
      behavior = IsolationBehavior::PrivilegedAbout;
    } else {
      MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
              ("Cannot load system-principal shared worker in "
               "non-privilegedabout content process"));
      return Err(NS_ERROR_UNEXPECTED);
    }
  } else {
    MOZ_ASSERT(resultOrPrecursor->GetIsNullPrincipal());
    MOZ_ASSERT(aWorkerKind == WorkerKindShared);

    if (preferredRemoteType == NOT_REMOTE_TYPE) {
      MOZ_LOG(gProcessIsolationLog, LogLevel::Debug,
              ("Ensuring precursorless null principal shared worker loads in a "
               "content process"));
      behavior = IsolationBehavior::ForceWebRemoteType;
    } else {
      MOZ_LOG(gProcessIsolationLog, LogLevel::Debug,
              ("Loading precursorless null principal shared worker within "
               "current remotetype: (%s)",
               preferredRemoteType.get()));
      behavior = IsolationBehavior::Anywhere;
    }
  }

  if (behavior == IsolationBehavior::Parent && isNullPrincipalPrecursor) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Debug,
            ("Ensuring sandboxed null-principal shared worker doesn't load in "
             "the parent process"));
    behavior = IsolationBehavior::ForceWebRemoteType;
  }

  if (behavior != IsolationBehavior::WebContent) {
    options.mRemoteType = MOZ_TRY(
        SpecialBehaviorRemoteType(behavior, preferredRemoteType, nullptr));

    MOZ_LOG(
        gProcessIsolationLog, LogLevel::Debug,
        ("Selecting specific %s worker remote type (%s) due to a special case "
         "isolation behavior %s",
         WorkerKindName(aWorkerKind), options.mRemoteType.get(),
         IsolationBehaviorName(behavior)));
    return options;
  }

  // If we should be isolating this site, we can determine the correct fission
  // remote type from the principal's site-origin.
  if (ShouldIsolateSite(resultOrPrecursor, aUseRemoteSubframes)) {
    nsAutoCString siteOriginNoSuffix;
    MOZ_TRY(resultOrPrecursor->GetSiteOriginNoSuffix(siteOriginNoSuffix));

    bool isJitAllowed = AllowJITForSiteOrigin(siteOriginNoSuffix, nullptr);

    nsAutoCString originSuffix =
        OriginSuffixForRemoteType(resultOrPrecursor, !isJitAllowed);

    nsCString prefix = aWorkerKind == WorkerKindService
                           ? SERVICEWORKER_REMOTE_TYPE
                           : FISSION_WEB_REMOTE_TYPE;
    options.mRemoteType = prefix + "="_ns + siteOriginNoSuffix + originSuffix;

    MOZ_LOG(gProcessIsolationLog, LogLevel::Debug,
            ("Isolating web content %s worker in remote type (%s)",
             WorkerKindName(aWorkerKind), options.mRemoteType.get()));
  } else {
    options.mRemoteType = WEB_REMOTE_TYPE;

    MOZ_LOG(gProcessIsolationLog, LogLevel::Debug,
            ("Loading web content %s worker in shared web remote type",
             WorkerKindName(aWorkerKind)));
  }
  return options;
}

void AddHighValuePermission(nsIPrincipal* aResultPrincipal,
                            const nsACString& aPermissionType) {
  RefPtr<PermissionManager> perms = PermissionManager::GetInstance();
  if (NS_WARN_IF(!perms)) {
    return;
  }

  // We can't act on non-content principals, so if the load was sandboxed, try
  // to use the unsandboxed precursor principal to add the highValue permission.
  nsCOMPtr<nsIPrincipal> resultOrPrecursor(aResultPrincipal);
  if (!aResultPrincipal->GetIsContentPrincipal()) {
    resultOrPrecursor = aResultPrincipal->GetPrecursorPrincipal();
    if (!resultOrPrecursor) {
      return;
    }
  }

  // Use the site-origin principal as we want to add the permission for the
  // entire site, rather than a specific subdomain, as process isolation acts on
  // a site granularity.
  nsAutoCString siteOrigin;
  if (NS_FAILED(resultOrPrecursor->GetSiteOrigin(siteOrigin))) {
    return;
  }

  nsCOMPtr<nsIPrincipal> sitePrincipal =
      BasePrincipal::CreateContentPrincipal(siteOrigin);
  if (!sitePrincipal || !sitePrincipal->GetIsContentPrincipal()) {
    return;
  }

  MOZ_LOG(dom::gProcessIsolationLog, LogLevel::Verbose,
          ("Adding %s Permission for site '%s'",
           PromiseFlatCString(aPermissionType).get(), siteOrigin.get()));

  uint32_t expiration = 0;
  if (aPermissionType.Equals(mozilla::dom::kHighValueCOOPPermission)) {
    expiration = StaticPrefs::fission_highValue_coop_expiration();
  } else if (aPermissionType.Equals(
                 mozilla::dom::kHighValueHasSavedLoginPermission) ||
             aPermissionType.Equals(
                 mozilla::dom::kHighValueIsLoggedInPermission)) {
    expiration = StaticPrefs::fission_highValue_login_expiration();
  } else {
    MOZ_ASSERT_UNREACHABLE("Unknown permission type");
  }

  // XXX: Would be nice if we could use `TimeStamp` here, but there's
  // unfortunately no convenient way to recover a time in milliseconds since the
  // unix epoch from `TimeStamp`.
  int64_t expirationTime =
      (PR_Now() / PR_USEC_PER_MSEC) + (int64_t(expiration) * PR_MSEC_PER_SEC);
  (void)perms->AddFromPrincipal(
      sitePrincipal, aPermissionType, nsIPermissionManager::ALLOW_ACTION,
      nsIPermissionManager::EXPIRE_TIME, expirationTime);
}

void AddHighValuePermission(const nsACString& aOrigin,
                            const nsACString& aPermissionType) {
  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  nsCOMPtr<nsIPrincipal> principal;
  nsresult rv =
      ssm->CreateContentPrincipalFromOrigin(aOrigin, getter_AddRefs(principal));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  AddHighValuePermission(principal, aPermissionType);
}

bool IsIsolateHighValueSiteEnabled() {
  return mozilla::FissionAutostart() &&
         WebContentIsolationStrategy(
             StaticPrefs::fission_webContentIsolationStrategy()) ==
             WebContentIsolationStrategy::IsolateHighValue;
}

bool ValidatePrincipalCouldPotentiallyBeLoadedBy(
    nsIPrincipal* aPrincipal, const nsACString& aRemoteType,
    const EnumSet<ValidatePrincipalOptions>& aOptions) {
  // Don't bother validating principals from the parent process.
  if (aRemoteType == NOT_REMOTE_TYPE) {
    return true;
  }

  // If there is no principal, only allow it if AllowNullPtr is specified.
  if (!aPrincipal) {
    return aOptions.contains(ValidatePrincipalOptions::AllowNullPtr);
  }

  // We currently do not track relationships between specific null principals
  // and content processes, so we can not validate much here.
  if (aPrincipal->GetIsNullPrincipal()) {
    return true;
  }

  // If we have a system principal, only allow it if AllowSystem is passed.
  if (aPrincipal->IsSystemPrincipal()) {
    return aOptions.contains(ValidatePrincipalOptions::AllowSystem);
  }

  // Performing checks against the remote type requires the IOService and
  // ThirdPartyService to be available, check we're not late in shutdown.
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdownFinal)) {
    return true;
  }

  // Only allow expanded principals if AllowExpanded is passed. Each
  // sub-principal will be validated independently.
  if (aPrincipal->GetIsExpandedPrincipal()) {
    if (!aOptions.contains(ValidatePrincipalOptions::AllowExpanded)) {
      return false;
    }
    // FIXME: There are more constraints on expanded principals in-practice,
    // such as the structure of extension expanded principals. This may need
    // to be investigated more in the future.
    nsCOMPtr<nsIExpandedPrincipal> expandedPrincipal =
        do_QueryInterface(aPrincipal);
    const auto& allowList = expandedPrincipal->AllowList();
    for (const auto& innerPrincipal : allowList) {
      if (!ValidatePrincipalCouldPotentiallyBeLoadedBy(innerPrincipal,
                                                       aRemoteType, aOptions)) {
        return false;
      }
    }
    return true;
  }

  // At this point we know we're working with a content principal.
  MOZ_ASSERT(aPrincipal->GetIsContentPrincipal());
  nsAutoCString originNoSuffix;
  MOZ_ALWAYS_SUCCEEDS(aPrincipal->GetOriginNoSuffix(originNoSuffix));

  // NOTE: We intentionally do scheme checks against the origin here, rather
  // than the principal's URI. This is because nested URIs like `view-source:`
  // are preserved in the principal, but do not impact the origin.
  nsAutoCString originScheme;
  MOZ_ALWAYS_SUCCEEDS(net_ExtractURLScheme(originNoSuffix, originScheme));

  // We can load a `resource://` URI in any process. This usually comes up due
  // to pdf.js and the JSON viewer. See bug 1686200.
  if (originScheme == "resource"_ns) {
    return true;
  }

  // A URI with a file:// scheme can never load in a non-file content process
  // due to sandboxing.
  if (originScheme == "file"_ns) {
    // If we don't support a separate 'file' process, then we can return here.
    if (!StaticPrefs::browser_tabs_remote_separateFileUriProcess()) {
      return true;
    }
    return aRemoteType == FILE_REMOTE_TYPE;
  }

  if (originScheme == "about"_ns) {
    nsCOMPtr<nsIURI> aboutURI;
    if (NS_FAILED(NS_NewURI(getter_AddRefs(aboutURI), originNoSuffix))) {
      MOZ_DIAGNOSTIC_ASSERT(false, "The originNoSuffix isn't a valid URI?");
      return false;
    }
    MOZ_ASSERT(aboutURI->SchemeIs("about"));

    // We cannot validate about module flags off-main-thread, as about modules
    // are not threadsafe, and can be implemented in JS.
    if (!NS_IsMainThread()) {
      return true;
    }

    // NOTE: The logic for about URIs is somewhat complex, so we lean on
    // IsolationBehaviorForURI to ensure it matches.
    switch (IsolationBehaviorForURI(aboutURI, /* aIsSubframe */ false,
                                    /* aForChannelCreationURI */ true)) {
      case IsolationBehavior::Parent:
        return false;
      case IsolationBehavior::Anywhere:
        return true;
      case IsolationBehavior::AboutReader:
        // Allow about:reader to load anywhere, as it is process-allocated based
        // on the content it displays, and which content is being displayed is
        // unfortunately not part of the principal.
        return true;
      case IsolationBehavior::Extension:
        return aRemoteType == EXTENSION_REMOTE_TYPE;
      case IsolationBehavior::PrivilegedAbout:
        return aRemoteType == PRIVILEGEDABOUT_REMOTE_TYPE;
      case IsolationBehavior::ForceWebRemoteType:
        return aRemoteType == WEB_REMOTE_TYPE;
      case IsolationBehavior::WebContent:
      case IsolationBehavior::Error:
        // NOTE: We can encounter races around about: pages being unregistered.
        // To avoid false positives, we fail open if no about module is present.
        return true;
      default:
        MOZ_CRASH("Unexpected IsolationBehaviorForURI for about: URI");
        return false;
    }
  }

  // Web content can contain extension content frames, so any content process
  // may send us an extension's principal.
  // NOTE: We don't check AddonPolicy here, as that can disappear if the add-on
  // is disabled or uninstalled. As this is a lax check, looking at the scheme
  // should be sufficient.
  if (originScheme == "moz-extension"_ns) {
    return true;
  }

  // If the remote type doesn't have an origin suffix, we can do no further
  // principal validation with it.
  int32_t equalIdx = aRemoteType.FindChar('=');
  if (equalIdx == kNotFound) {
    return true;
  }

  // Split out the remote type prefix and the origin suffix.
  nsDependentCSubstring typePrefix(aRemoteType, 0, equalIdx);
  nsDependentCSubstring typeOrigin(aRemoteType, equalIdx + 1);

  // Only validate webIsolated and webServiceWorker remote types for now. This
  // should be expanded in the future.
  if (typePrefix != FISSION_WEB_REMOTE_TYPE &&
      typePrefix != SERVICEWORKER_REMOTE_TYPE) {
    return true;
  }

  // Trim any OriginAttributes from the origin, as those will not be validated.
  int32_t suffixIdx = typeOrigin.RFindChar('^');
  nsDependentCSubstring typeOriginNoSuffix(typeOrigin, 0, suffixIdx);

  // If the origin perfectly matches, we can skip computing the site origin.
  if (typeOriginNoSuffix == originNoSuffix) {
    return true;
  }

  // NOTE: Currently every webIsolated remote type is site-origin keyed, meaning
  // we can unconditionally compare site origins. If this changes in the future,
  // this logic will need to be updated to reflect that.
  nsAutoCString siteOriginNoSuffix;
  if (NS_FAILED(aPrincipal->GetSiteOriginNoSuffix(siteOriginNoSuffix))) {
    MOZ_ASSERT_UNREACHABLE("Failed when not late in shutdown?");
    return false;
  }
  return siteOriginNoSuffix == typeOriginNoSuffix;
}

}  // namespace mozilla::dom
