/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ContentParent.h"
#include "RegistryMessageUtils.h"
#include "nsResProtocolHandler.h"

#include "nsChromeRegistryChrome.h"

#if defined(XP_WIN)
#  include <windows.h>
#elif defined(XP_MACOSX)
#  include <CoreServices/CoreServices.h>
#endif

#include "nsArrayEnumerator.h"
#include "nsComponentManager.h"
#include "nsEnumeratorUtils.h"
#include "nsNetUtil.h"
#include "nsStringEnumerator.h"
#include "nsTextFormatter.h"
#include "nsXPCOMCIDInternal.h"

#include "mozilla/LookAndFeel.h"

#include "nsIObserverService.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/Preferences.h"
#include "nsIResProtocolHandler.h"
#include "nsIScriptError.h"
#include "nsIXULRuntime.h"

#define PACKAGE_OVERRIDE_BRANCH "chrome.override_package."
#define SKIN "classic/1.0"_ns

using namespace mozilla;
using mozilla::dom::ContentParent;
using mozilla::dom::PContentParent;
using mozilla::intl::LocaleService;

// We use a "best-fit" algorithm for matching locales and themes.
// 1) the exact selected locale/theme
// 2) (locales only) same language, different country
//    e.g. en-GB is the selected locale, only en-US is available
// 3) any available locale/theme

/**
 * Match the language-part of two lang-COUNTRY codes, hopefully but
 * not guaranteed to be in the form ab-CD or abz-CD. "ab" should also
 * work, any other garbage-in will produce undefined results as long
 * as it does not crash.
 */
static bool LanguagesMatch(const nsACString& a, const nsACString& b) {
  if (a.Length() < 2 || b.Length() < 2) return false;

  nsACString::const_iterator as, ae, bs, be;
  a.BeginReading(as);
  a.EndReading(ae);
  b.BeginReading(bs);
  b.EndReading(be);

  while (*as == *bs) {
    if (*as == '-') return true;

    ++as;
    ++bs;

    // reached the end
    if (as == ae && bs == be) return true;

    // "a" is short
    if (as == ae) return (*bs == '-');

    // "b" is short
    if (bs == be) return (*as == '-');
  }

  return false;
}

nsChromeRegistryChrome::nsChromeRegistryChrome()
    : mProfileLoaded(false), mDynamicRegistration(true) {}

nsChromeRegistryChrome::~nsChromeRegistryChrome() = default;

nsresult nsChromeRegistryChrome::Init() {
  nsresult rv = nsChromeRegistry::Init();
  if (NS_FAILED(rv)) return rv;

  bool safeMode = false;
  nsCOMPtr<nsIXULRuntime> xulrun(do_GetService(XULAPPINFO_SERVICE_CONTRACTID));
  if (xulrun) xulrun->GetInSafeMode(&safeMode);

  nsCOMPtr<nsIObserverService> obsService =
      mozilla::services::GetObserverService();
  if (obsService) {
    obsService->AddObserver(this, "profile-initial-state", true);
    obsService->AddObserver(this, "intl:app-locales-changed", true);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsChromeRegistryChrome::GetLocalesForPackage(
    const nsACString& aPackage, nsIUTF8StringEnumerator** aResult) {
  nsCString realpackage;
  nsresult rv = OverrideLocalePackage(aPackage, realpackage);
  if (NS_FAILED(rv)) return rv;

  nsTArray<nsCString>* a = new nsTArray<nsCString>;
  if (!a) return NS_ERROR_OUT_OF_MEMORY;

  PackageEntry* entry;
  if (mPackagesHash.Get(realpackage, &entry)) {
    entry->locales.EnumerateToArray(a);
  }

  rv = NS_NewAdoptingUTF8StringEnumerator(aResult, a);
  if (NS_FAILED(rv)) delete a;

  return rv;
}

NS_IMETHODIMP
nsChromeRegistryChrome::IsLocaleRTL(const nsACString& package, bool* aResult) {
  *aResult = false;

  nsAutoCString locale;
  GetSelectedLocale(package, locale);
  if (locale.Length() < 2) return NS_OK;

  *aResult = LocaleService::IsLocaleRTL(locale);
  return NS_OK;
}

/**
 * This method negotiates only between the app locale and the available
 * chrome packages.
 *
 * If you want to get the current application's UI locale, please use
 * LocaleService::GetAppLocaleAsBCP47.
 */
nsresult nsChromeRegistryChrome::GetSelectedLocale(const nsACString& aPackage,
                                                   nsACString& aLocale) {
  nsAutoCString reqLocale;
  if (aPackage.EqualsLiteral("global")) {
    LocaleService::GetInstance()->GetAppLocaleAsBCP47(reqLocale);
  } else {
    AutoTArray<nsCString, 10> requestedLocales;
    LocaleService::GetInstance()->GetRequestedLocales(requestedLocales);
    reqLocale.Assign(requestedLocales[0]);
  }

  nsCString realpackage;
  nsresult rv = OverrideLocalePackage(aPackage, realpackage);
  if (NS_FAILED(rv)) return rv;
  PackageEntry* entry;
  if (!mPackagesHash.Get(realpackage, &entry)) return NS_ERROR_FILE_NOT_FOUND;

  aLocale = entry->locales.GetSelected(reqLocale, nsProviderArray::LOCALE);
  if (aLocale.IsEmpty()) return NS_ERROR_FAILURE;

  return NS_OK;
}

nsresult nsChromeRegistryChrome::OverrideLocalePackage(
    const nsACString& aPackage, nsACString& aOverride) {
  const nsACString& pref = nsLiteralCString(PACKAGE_OVERRIDE_BRANCH) + aPackage;
  nsAutoCString override;
  nsresult rv = mozilla::Preferences::GetCString(PromiseFlatCString(pref).get(),
                                                 override);
  if (NS_SUCCEEDED(rv)) {
    aOverride = std::move(override);
  } else {
    aOverride = aPackage;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsChromeRegistryChrome::Observe(nsISupports* aSubject, const char* aTopic,
                                const char16_t* someData) {
  nsresult rv = NS_OK;

  if (!strcmp("profile-initial-state", aTopic)) {
    mProfileLoaded = true;
  } else if (!strcmp("intl:app-locales-changed", aTopic)) {
    if (mProfileLoaded) {
      FlushAllCaches();
    }
  } else {
    NS_ERROR("Unexpected observer topic!");
  }

  return rv;
}

NS_IMETHODIMP
nsChromeRegistryChrome::CheckForNewChrome() {
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    MOZ_ASSERT(false, "checking for new chrome during shutdown");
    return NS_ERROR_UNEXPECTED;
  }

  mPackagesHash.Clear();
  mOverrideTable.Clear();

  mDynamicRegistration = false;

  nsComponentManagerImpl::gComponentManager->RereadChromeManifests();

  mDynamicRegistration = true;

  SendRegisteredChrome(nullptr);
  return NS_OK;
}

static void SerializeURI(nsIURI* aURI, SerializedURI& aSerializedURI) {
  if (!aURI) return;

  aURI->GetSpec(aSerializedURI.spec);
}

void nsChromeRegistryChrome::SendRegisteredChrome(
    mozilla::dom::PContentParent* aParent) {
  nsTArray<ChromePackage> packages;
  nsTArray<SubstitutionMapping> resources;
  nsTArray<OverrideMapping> overrides;

  for (const auto& entry : mPackagesHash) {
    ChromePackage chromePackage;
    ChromePackageFromPackageEntry(entry.GetKey(), entry.GetRawPtr(),
                                  &chromePackage, SKIN);
    packages.AppendElement(chromePackage);
  }

  // If we were passed a parent then a new child process has been created and
  // has requested all of the chrome so send it the resources too. Otherwise
  // resource mappings are sent by the resource protocol handler dynamically.
  if (aParent) {
    nsCOMPtr<nsIIOService> io(do_GetIOService());
    NS_ENSURE_TRUE_VOID(io);

    nsCOMPtr<nsIProtocolHandler> ph;
    nsresult rv = io->GetProtocolHandler("resource", getter_AddRefs(ph));
    NS_ENSURE_SUCCESS_VOID(rv);

    nsCOMPtr<nsIResProtocolHandler> irph(do_QueryInterface(ph));
    nsResProtocolHandler* rph = static_cast<nsResProtocolHandler*>(irph.get());
    rv = rph->CollectSubstitutions(resources);
    NS_ENSURE_SUCCESS_VOID(rv);
  }

  for (const auto& entry : mOverrideTable) {
    SerializedURI chromeURI, overrideURI;

    SerializeURI(entry.GetKey(), chromeURI);
    SerializeURI(entry.GetRawPtr(), overrideURI);

    overrides.AppendElement(
        OverrideMapping{std::move(chromeURI), std::move(overrideURI)});
  }

  nsAutoCString appLocale;
  LocaleService::GetInstance()->GetAppLocaleAsBCP47(appLocale);

  if (aParent) {
    bool success = aParent->SendRegisterChrome(packages, resources, overrides,
                                               appLocale, false);
    NS_ENSURE_TRUE_VOID(success);
  } else {
    nsTArray<ContentParent*> parents;
    ContentParent::GetAll(parents);
    if (!parents.Length()) return;

    for (uint32_t i = 0; i < parents.Length(); i++) {
      DebugOnly<bool> success = parents[i]->SendRegisterChrome(
          packages, resources, overrides, appLocale, true);
      NS_WARNING_ASSERTION(success,
                           "couldn't reset a child's registered chrome");
    }
  }
}

/* static */
void nsChromeRegistryChrome::ChromePackageFromPackageEntry(
    const nsACString& aPackageName, PackageEntry* aPackage,
    ChromePackage* aChromePackage, const nsCString& aSelectedSkin) {
  nsAutoCString appLocale;
  LocaleService::GetInstance()->GetAppLocaleAsBCP47(appLocale);

  SerializeURI(aPackage->baseURI, aChromePackage->contentBaseURI);
  SerializeURI(aPackage->locales.GetBase(appLocale, nsProviderArray::LOCALE),
               aChromePackage->localeBaseURI);
  SerializeURI(aPackage->skins.GetBase(aSelectedSkin, nsProviderArray::ANY),
               aChromePackage->skinBaseURI);
  aChromePackage->package = aPackageName;
  aChromePackage->flags = aPackage->flags;
}

static bool CanLoadResource(nsIURI* aResourceURI) {
  bool isLocalResource = false;
  (void)NS_URIChainHasFlags(aResourceURI,
                            nsIProtocolHandler::URI_IS_LOCAL_RESOURCE,
                            &isLocalResource);
  return isLocalResource;
}

nsIURI* nsChromeRegistryChrome::GetBaseURIFromPackage(
    const nsCString& aPackage, const nsCString& aProvider,
    const nsCString& aPath) {
  PackageEntry* entry;
  if (!mPackagesHash.Get(aPackage, &entry)) {
    if (!mInitialized) return nullptr;

    LogMessage("No chrome package registered for chrome://%s/%s/%s",
               aPackage.get(), aProvider.get(), aPath.get());

    return nullptr;
  }

  if (aProvider.EqualsLiteral("locale")) {
    nsAutoCString appLocale;
    LocaleService::GetInstance()->GetAppLocaleAsLangTag(appLocale);
    return entry->locales.GetBase(appLocale, nsProviderArray::LOCALE);
  }

  if (aProvider.EqualsLiteral("skin")) {
    return entry->skins.GetBase(SKIN, nsProviderArray::ANY);
  }

  if (aProvider.EqualsLiteral("content")) {
    return entry->baseURI;
  }
  return nullptr;
}

nsresult nsChromeRegistryChrome::GetFlagsFromPackage(const nsCString& aPackage,
                                                     uint32_t* aFlags) {
  PackageEntry* entry;
  if (!mPackagesHash.Get(aPackage, &entry)) return NS_ERROR_FILE_NOT_FOUND;

  *aFlags = entry->flags;
  return NS_OK;
}

nsChromeRegistryChrome::ProviderEntry*
nsChromeRegistryChrome::nsProviderArray::GetProvider(
    const nsACString& aPreferred, MatchType aType) {
  size_t i = mArray.Length();
  if (!i) return nullptr;

  ProviderEntry* found = nullptr;  // Only set if we find a partial-match locale
  ProviderEntry* entry = nullptr;

  while (i--) {
    entry = &mArray[i];
    if (aPreferred.Equals(entry->provider)) return entry;

    if (aType != LOCALE) continue;

    if (LanguagesMatch(aPreferred, entry->provider)) {
      found = entry;
      continue;
    }

    if (!found && entry->provider.EqualsLiteral("en-US")) found = entry;
  }

  if (!found && aType != EXACT) return entry;

  return found;
}

nsIURI* nsChromeRegistryChrome::nsProviderArray::GetBase(
    const nsACString& aPreferred, MatchType aType) {
  ProviderEntry* provider = GetProvider(aPreferred, aType);

  if (!provider) return nullptr;

  return provider->baseURI;
}

const nsACString& nsChromeRegistryChrome::nsProviderArray::GetSelected(
    const nsACString& aPreferred, MatchType aType) {
  ProviderEntry* entry = GetProvider(aPreferred, aType);

  if (entry) return entry->provider;

  return EmptyCString();
}

void nsChromeRegistryChrome::nsProviderArray::SetBase(
    const nsACString& aProvider, nsIURI* aBaseURL) {
  ProviderEntry* provider = GetProvider(aProvider, EXACT);

  if (provider) {
    provider->baseURI = aBaseURL;
    return;
  }

  // no existing entries, add a new one
  mArray.AppendElement(ProviderEntry(aProvider, aBaseURL));
}

void nsChromeRegistryChrome::nsProviderArray::EnumerateToArray(
    nsTArray<nsCString>* a) {
  int32_t i = mArray.Length();
  while (i--) {
    a->AppendElement(mArray[i].provider);
  }
}

nsIURI* nsChromeRegistry::ManifestProcessingContext::GetManifestURI() {
  if (!mManifestURI) {
    nsCString uri;
    mFile.GetURIString(uri);
    NS_NewURI(getter_AddRefs(mManifestURI), uri);
  }
  return mManifestURI;
}

already_AddRefed<nsIURI>
nsChromeRegistry::ManifestProcessingContext::ResolveURI(const char* uri) {
  nsIURI* baseuri = GetManifestURI();
  if (!baseuri) return nullptr;

  nsCOMPtr<nsIURI> resolved;
  nsresult rv = NS_NewURI(getter_AddRefs(resolved), uri, baseuri);
  if (NS_FAILED(rv)) return nullptr;

  return resolved.forget();
}

static void EnsureLowerCase(char* aBuf) {
  for (; *aBuf; ++aBuf) {
    char ch = *aBuf;
    if (ch >= 'A' && ch <= 'Z') *aBuf = ch + 'a' - 'A';
  }
}

static void SendManifestEntry(const ChromeRegistryItem& aItem) {
  nsTArray<ContentParent*> parents;
  ContentParent::GetAll(parents);
  if (!parents.Length()) return;

  for (uint32_t i = 0; i < parents.Length(); i++) {
    (void)parents[i]->SendRegisterChromeItem(aItem);
  }
}

void nsChromeRegistryChrome::ManifestContent(ManifestProcessingContext& cx,
                                             int lineno, char* const* argv,
                                             int flags) {
  char* package = argv[0];
  char* uri = argv[1];

  EnsureLowerCase(package);

  nsCOMPtr<nsIURI> resolved = cx.ResolveURI(uri);
  if (!resolved) {
    LogMessageWithContext(
        cx.GetManifestURI(), lineno, nsIScriptError::warningFlag,
        "During chrome registration, unable to create URI '%s'.", uri);
    return;
  }

  if (!CanLoadResource(resolved)) {
    LogMessageWithContext(resolved, lineno, nsIScriptError::warningFlag,
                          "During chrome registration, cannot register "
                          "non-local URI '%s' as content.",
                          uri);
    return;
  }

  nsDependentCString packageName(package);
  PackageEntry* entry = mPackagesHash.GetOrInsertNew(packageName);
  entry->baseURI = std::move(resolved);
  entry->flags = flags;

  if (mDynamicRegistration) {
    ChromePackage chromePackage;
    ChromePackageFromPackageEntry(packageName, entry, &chromePackage, SKIN);
    SendManifestEntry(chromePackage);
  }
}

void nsChromeRegistryChrome::ManifestLocale(ManifestProcessingContext& cx,
                                            int lineno, char* const* argv,
                                            int flags) {
  char* package = argv[0];
  char* provider = argv[1];
  char* uri = argv[2];

  EnsureLowerCase(package);

  nsCOMPtr<nsIURI> resolved = cx.ResolveURI(uri);
  if (!resolved) {
    LogMessageWithContext(
        cx.GetManifestURI(), lineno, nsIScriptError::warningFlag,
        "During chrome registration, unable to create URI '%s'.", uri);
    return;
  }

  if (!CanLoadResource(resolved)) {
    LogMessageWithContext(resolved, lineno, nsIScriptError::warningFlag,
                          "During chrome registration, cannot register "
                          "non-local URI '%s' as content.",
                          uri);
    return;
  }

  nsDependentCString packageName(package);
  PackageEntry* entry = mPackagesHash.GetOrInsertNew(packageName);
  entry->locales.SetBase(nsDependentCString(provider), resolved);

  if (mDynamicRegistration) {
    ChromePackage chromePackage;
    ChromePackageFromPackageEntry(packageName, entry, &chromePackage, SKIN);
    SendManifestEntry(chromePackage);
  }

  // We use mainPackage as the package we track for reporting new locales being
  // registered. For most cases it will be "global", but for Fennec it will be
  // "browser".
  nsAutoCString mainPackage;
  nsresult rv = OverrideLocalePackage("global"_ns, mainPackage);
  if (NS_FAILED(rv)) {
    return;
  }
}

void nsChromeRegistryChrome::ManifestSkin(ManifestProcessingContext& cx,
                                          int lineno, char* const* argv,
                                          int flags) {
  char* package = argv[0];
  char* provider = argv[1];
  char* uri = argv[2];

  EnsureLowerCase(package);

  nsCOMPtr<nsIURI> resolved = cx.ResolveURI(uri);
  if (!resolved) {
    LogMessageWithContext(
        cx.GetManifestURI(), lineno, nsIScriptError::warningFlag,
        "During chrome registration, unable to create URI '%s'.", uri);
    return;
  }

  if (!CanLoadResource(resolved)) {
    LogMessageWithContext(resolved, lineno, nsIScriptError::warningFlag,
                          "During chrome registration, cannot register "
                          "non-local URI '%s' as content.",
                          uri);
    return;
  }

  nsDependentCString packageName(package);
  PackageEntry* entry = mPackagesHash.GetOrInsertNew(packageName);
  entry->skins.SetBase(nsDependentCString(provider), resolved);

  if (mDynamicRegistration) {
    ChromePackage chromePackage;
    ChromePackageFromPackageEntry(packageName, entry, &chromePackage, SKIN);
    SendManifestEntry(chromePackage);
  }
}

void nsChromeRegistryChrome::ManifestOverride(ManifestProcessingContext& cx,
                                              int lineno, char* const* argv,
                                              int flags) {
  char* chrome = argv[0];
  char* resolved = argv[1];

  nsCOMPtr<nsIURI> chromeuri = cx.ResolveURI(chrome);
  nsCOMPtr<nsIURI> resolveduri = cx.ResolveURI(resolved);
  if (!chromeuri || !resolveduri) {
    LogMessageWithContext(cx.GetManifestURI(), lineno,
                          nsIScriptError::warningFlag,
                          "During chrome registration, unable to create URI.");
    return;
  }

  if (!CanLoadResource(resolveduri)) {
    LogMessageWithContext(
        cx.GetManifestURI(), lineno, nsIScriptError::warningFlag,
        "Cannot register non-local URI '%s' for an override.", resolved);
    return;
  }
  mOverrideTable.InsertOrUpdate(chromeuri, resolveduri);

  if (mDynamicRegistration) {
    SerializedURI serializedChrome;
    SerializedURI serializedOverride;

    SerializeURI(chromeuri, serializedChrome);
    SerializeURI(resolveduri, serializedOverride);

    OverrideMapping override = {std::move(serializedChrome),
                                std::move(serializedOverride)};
    SendManifestEntry(override);
  }
}

void nsChromeRegistryChrome::ManifestResource(ManifestProcessingContext& cx,
                                              int lineno, char* const* argv,
                                              int flags) {
  char* package = argv[0];
  char* uri = argv[1];

  EnsureLowerCase(package);
  nsDependentCString host(package);

  nsCOMPtr<nsIIOService> io = mozilla::components::IO::Service();
  if (!io) {
    NS_WARNING("No IO service trying to process chrome manifests");
    return;
  }

  nsCOMPtr<nsIProtocolHandler> ph;
  nsresult rv = io->GetProtocolHandler("resource", getter_AddRefs(ph));
  if (NS_FAILED(rv)) return;

  nsCOMPtr<nsIResProtocolHandler> rph = do_QueryInterface(ph);

  nsCOMPtr<nsIURI> resolved = cx.ResolveURI(uri);
  if (!resolved) {
    LogMessageWithContext(
        cx.GetManifestURI(), lineno, nsIScriptError::warningFlag,
        "During chrome registration, unable to create URI '%s'.", uri);
    return;
  }

  if (!CanLoadResource(resolved)) {
    LogMessageWithContext(
        cx.GetManifestURI(), lineno, nsIScriptError::warningFlag,
        "Warning: cannot register non-local URI '%s' as a resource.", uri);
    return;
  }

  // By default, Firefox resources are not content-accessible unless the
  // manifests opts in.
  bool contentAccessible = (flags & nsChromeRegistry::CONTENT_ACCESSIBLE);

  uint32_t substitutionFlags = 0;
  if (contentAccessible) {
    substitutionFlags |= nsIResProtocolHandler::ALLOW_CONTENT_ACCESS;
  }
  rv = rph->SetSubstitutionWithFlags(host, resolved, substitutionFlags);
  if (NS_FAILED(rv)) {
    LogMessageWithContext(cx.GetManifestURI(), lineno,
                          nsIScriptError::warningFlag,
                          "Warning: cannot set substitution for '%s'.", uri);
  }
}
