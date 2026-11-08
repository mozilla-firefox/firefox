/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIOService.h"

#include "IPv4Parser.h"
#include "MainThreadUtils.h"
#include "SerializedLoadContext.h"
#include "StaticComponents.h"
#include "SuspendableChannelWrapper.h"
#include "WebTransportSessionProxy.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/ChromeUtilsBinding.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ServiceWorkerDescriptor.h"
#include "mozilla/dom/nsHTTPSOnlyUtils.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/CacheControlParser.h"
#include "mozilla/net/CaptivePortalService.h"
#include "mozilla/net/DNS.h"
#include "mozilla/net/NeckoChild.h"
#include "mozilla/net/NeckoCommon.h"
#include "mozilla/net/NeckoParent.h"
#include "mozilla/net/NetworkConnectivityService.h"
#include "mozilla/net/SSLTokensCache.h"
#include "netCore.h"
#include "nsAsyncRedirectVerifyHelper.h"
#include "nsCRT.h"
#include "nsContentSecurityManager.h"
#include "nsContentUtils.h"
#include "nsDNSService2.h"
#include "nsEscape.h"
#include "nsICancelable.h"
#include "nsIFileProtocolHandler.h"
#include "nsINetworkLinkService.h"
#include "nsINode.h"
#include "nsIObserverService.h"
#include "nsIProtocolHandler.h"
#include "nsIProtocolProxyCallback.h"
#include "nsIProtocolProxyService2.h"
#include "nsIProxiedProtocolHandler.h"
#include "nsIProxyInfo.h"
#include "nsIURI.h"
#include "nsIUploadChannel2.h"
#include "nsIWebTransport.h"
#include "nsIWidget.h"
#include "nsNSSComponent.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsSimpleNestedURI.h"
#include "nsSocketTransport2.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsURLHelper.h"
#include "nsXPCOM.h"
#include "nsXULAppAPI.h"
#include "nscore.h"
#include "prprf.h"
#include "ssl.h"

#ifdef MOZ_WIDGET_ANDROID
#  include <regex>

#  include "AndroidBridge.h"
#  include "mozilla/java/GeckoAppShellWrappers.h"
#  include "mozilla/jni/Utils.h"
#endif

namespace mozilla {
namespace net {

using mozilla::Maybe;
using mozilla::dom::ClientInfo;
using mozilla::dom::ServiceWorkerDescriptor;

#define PORT_PREF_PREFIX "network.security.ports."
#define PORT_PREF(x) PORT_PREF_PREFIX x
#define MANAGE_OFFLINE_STATUS_PREF "network.manage-offline-status"

// Nb: these have been misnomers since bug 715770 removed the buffer cache.
// "network.segment.count" and "network.segment.size" would be better names,
// but the old names are still used to preserve backward compatibility.
#define NECKO_BUFFER_CACHE_COUNT_PREF "network.buffer.cache.count"
#define NECKO_BUFFER_CACHE_SIZE_PREF "network.buffer.cache.size"
#define NETWORK_CAPTIVE_PORTAL_PREF "network.captive-portal-service.enabled"
#define FORCE_EXTERNAL_PREF_PREFIX "network.protocol-handler.external."
// prefs for overriding IPAddress->IpAddressSpace mapping
#define PREF_LNA_IP_ADDR_SPACE_PUBLIC \
  "network.lna.address_space.public.override"
#define PREF_LNA_IP_ADDR_SPACE_PRIVATE \
  "network.lna.address_space.private.override"
#define PREF_LNA_IP_ADDR_SPACE_LOCAL "network.lna.address_space.local.override"
#define PREF_LNA_SKIP_DOMAINS "network.lna.skip-domains"

nsIOService* gIOService;
static LazyLogModule gIOServiceLog("nsIOService");
#undef LOG
#define LOG(args) MOZ_LOG(gIOServiceLog, LogLevel::Debug, args)

// A general port blacklist.  Connections to these ports will not be allowed
// unless the protocol overrides.
//
// This list is to be kept in sync with "bad ports" as defined in the
// WHATWG Fetch standard at <https://fetch.spec.whatwg.org/#port-blocking>

int16_t gBadPortList[] = {
    1,      // tcpmux
    7,      // echo
    9,      // discard
    11,     // systat
    13,     // daytime
    15,     // netstat
    17,     // qotd
    19,     // chargen
    20,     // ftp-data
    21,     // ftp
    22,     // ssh
    23,     // telnet
    25,     // smtp
    37,     // time
    42,     // name
    43,     // nicname
    53,     // domain
    69,     // tftp
    77,     // priv-rjs
    79,     // finger
    87,     // ttylink
    95,     // supdup
    101,    // hostriame
    102,    // iso-tsap
    103,    // gppitnp
    104,    // acr-nema
    109,    // pop2
    110,    // pop3
    111,    // sunrpc
    113,    // auth
    115,    // sftp
    117,    // uucp-path
    119,    // nntp
    123,    // ntp
    135,    // loc-srv / epmap
    137,    // netbios
    139,    // netbios
    143,    // imap2
    161,    // snmp
    179,    // bgp
    389,    // ldap
    427,    // afp (alternate)
    465,    // smtp (alternate)
    512,    // print / exec
    513,    // login
    514,    // shell
    515,    // printer
    526,    // tempo
    530,    // courier
    531,    // chat
    532,    // netnews
    540,    // uucp
    548,    // afp
    554,    // rtsp
    556,    // remotefs
    563,    // nntp+ssl
    587,    // smtp (outgoing)
    601,    // syslog-conn
    636,    // ldap+ssl
    989,    // ftps-data
    990,    // ftps
    993,    // imap+ssl
    995,    // pop3+ssl
    1719,   // h323gatestat
    1720,   // h323hostcall
    1723,   // pptp
    2049,   // nfs
    3659,   // apple-sasl
    4045,   // lockd
    4190,   // sieve
    5060,   // sip
    5061,   // sips
    6000,   // x11
    6566,   // sane-port
    6665,   // irc (alternate)
    6666,   // irc (alternate)
    6667,   // irc (default)
    6668,   // irc (alternate)
    6669,   // irc (alternate)
    6679,   // osaut
    6697,   // irc+tls
    10080,  // amanda
    0,      // Sentinel value: This MUST be zero
};

static const char kProfileChangeNetTeardownTopic[] =
    "profile-change-net-teardown";
static const char kProfileDoChange[] = "profile-do-change";

// Necko buffer defaults
uint32_t nsIOService::gDefaultSegmentSize = 4096;
uint32_t nsIOService::gDefaultSegmentCount = 24;

////////////////////////////////////////////////////////////////////////////////

nsIOService::nsIOService()
    : mLastOfflineStateChange(PR_IntervalNow()),
      mLastConnectivityChange(PR_IntervalNow()),
      mLastNetworkLinkChange(PR_IntervalNow()) {}

static const char* gCallbackPrefs[] = {
    PORT_PREF_PREFIX,
    MANAGE_OFFLINE_STATUS_PREF,
    NECKO_BUFFER_CACHE_COUNT_PREF,
    NECKO_BUFFER_CACHE_SIZE_PREF,
    NETWORK_CAPTIVE_PORTAL_PREF,
    FORCE_EXTERNAL_PREF_PREFIX,
    // [pref-trie-audit] "network.url.simple_uri_unknown_schemes" is an
    // ambiguous prefix of "network.url.simple_uri_unknown_schemes_enabled";
    // triggers only for the exact pref and its dot-bounded children ("_enabled"
    // is a StaticPref and needs no callback).
    SIMPLE_URI_SCHEMES_PREF,
    PREF_LNA_IP_ADDR_SPACE_PUBLIC,
    PREF_LNA_IP_ADDR_SPACE_PRIVATE,
    PREF_LNA_IP_ADDR_SPACE_LOCAL,
    PREF_LNA_SKIP_DOMAINS,
    nullptr,
};

nsresult nsIOService::Init() {
  if (XRE_IsParentProcess()) {
    SSLTokensCache::Init();
  }

  InitializeCaptivePortalService();

  // setup our bad port list stuff
  for (int i = 0; gBadPortList[i]; i++) {
    // We can't be accessed by another thread yet
    MOZ_PUSH_IGNORE_THREAD_SAFETY
    mRestrictedPortList.AppendElement(gBadPortList[i]);
    MOZ_POP_THREAD_SAFETY
  }

  // Further modifications to the port list come from prefs
  Preferences::RegisterPrefixCallbacks(nsIOService::PrefsChanged,
                                       gCallbackPrefs, this);
  PrefsChanged();

  // Register for profile change notifications
  mObserverService = services::GetObserverService();
  if (mObserverService) {
    MOZ_ALWAYS_SUCCEEDS(mObserverService->AddObserver(
        this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, true));

    MOZ_ALWAYS_SUCCEEDS(mObserverService->AddObserver(
        this, kProfileChangeNetTeardownTopic, true));
    MOZ_ALWAYS_SUCCEEDS(
        mObserverService->AddObserver(this, kProfileDoChange, true));
    MOZ_ALWAYS_SUCCEEDS(
        mObserverService->AddObserver(this, NS_NETWORK_LINK_TOPIC, true));
    MOZ_ALWAYS_SUCCEEDS(
        mObserverService->AddObserver(this, NS_NETWORK_ID_CHANGED_TOPIC, true));
    MOZ_ALWAYS_SUCCEEDS(mObserverService->AddObserver(
        this, NS_WIDGET_WAKE_OBSERVER_TOPIC, true));

    // Register observers for sending notifications to nsSocketTransportService
    if (XRE_IsParentProcess()) {
      mObserverService->AddObserver(this, "profile-initial-state", true);
      mObserverService->AddObserver(this, NS_WIDGET_SLEEP_OBSERVER_TOPIC, true);
    }
  }

  gIOService = this;

  InitializeNetworkLinkService();
  InitializeProtocolProxyService();
  SetOffline(false);

  // This is just to start the DNS service to make it fast to get later.
  // Don't invoke directly since we're already in GetService.  RefPtr needed
  // because already_AddRefed<> doesn't like to be dropped
  NS_DispatchToCurrentThread(NS_NewRunnableFunction(
      __func__, []() { RefPtr<nsIDNSService> dns = GetOrInitDNSService(); }));

  return NS_OK;
}

nsIOService::~nsIOService() {
  if (gIOService) {
    MOZ_ASSERT(gIOService == this);
    gIOService = nullptr;
  }
}

#ifdef MOZ_WIDGET_ANDROID
bool nsIOService::ShouldAddAdditionalSearchHeaders(nsIURI* aURI,
                                                   bool* aHeaderVal) {
  if (!(mozilla::AndroidBridge::Bridge())) {
    return false;
  }

  if (!aURI->SchemeIs("https")) {
    return false;
  }

  // We need to improve below logic for matching google domains
  // See Bug 1894642
  // Is URI same as google ^https://www\\.google\\..+
  nsAutoCString host;
  aURI->GetHost(host);
  LOG(("nsIOService::ShouldAddAdditionalSearchHeaders() checking host %s\n",
       PromiseFlatCString(host).get()));

  std::regex pattern("^www\\.google\\..+");
  if (std::regex_match(host.get(), pattern)) {
    LOG(("Google domain detected for host %s\n",
         PromiseFlatCString(host).get()));
    static bool ramAboveThreshold =
        java::GeckoAppShell::IsDeviceRamThresholdOkay();
    *aHeaderVal = ramAboveThreshold;
    return true;
  }

  return false;
}
#endif

nsresult nsIOService::InitializeCaptivePortalService() {
  if (XRE_GetProcessType() != GeckoProcessType_Default) {
    // We only initalize a captive portal service in the main process
    return NS_OK;
  }

  mCaptivePortalService = mozilla::components::CaptivePortal::Service();
  if (mCaptivePortalService) {
    static_cast<CaptivePortalService*>(mCaptivePortalService.get())
        ->Initialize();
  }

  // Instantiate and initialize the service
  RefPtr<NetworkConnectivityService> ncs =
      NetworkConnectivityService::GetSingleton();

  return NS_OK;
}

nsresult nsIOService::InitializeSocketTransportService() {
  nsresult rv = NS_OK;

  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    LOG(
        ("nsIOService aborting InitializeSocketTransportService because of app "
         "shutdown"));
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  if (!mSocketTransportService) {
    mSocketTransportService =
        mozilla::components::SocketTransport::Service(&rv);
    if (NS_FAILED(rv)) {
      NS_WARNING("failed to get socket transport service");
    }
  }

  if (mSocketTransportService) {
    rv = mSocketTransportService->Init();
    NS_ASSERTION(NS_SUCCEEDED(rv), "socket transport service init failed");
    mSocketTransportService->SetOffline(false);
  }

  return rv;
}

nsresult nsIOService::InitializeNetworkLinkService() {
  nsresult rv = NS_OK;

  if (mNetworkLinkServiceInitialized) return rv;

  if (!NS_IsMainThread()) {
    NS_WARNING("Network link service should be created on main thread");
    return NS_ERROR_FAILURE;
  }

  // go into managed mode if we can, and chrome process
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mNetworkLinkService = do_GetService(NS_NETWORK_LINK_SERVICE_CONTRACTID, &rv);

  if (mNetworkLinkService) {
    mNetworkLinkServiceInitialized = true;
  }

  // After initializing the networkLinkService, query the connectivity state
  OnNetworkLinkEvent(NS_NETWORK_LINK_DATA_UNKNOWN);

  return rv;
}

nsresult nsIOService::InitializeProtocolProxyService() {
  nsresult rv = NS_OK;

  if (XRE_IsParentProcess()) {
    // for early-initialization
    (void)mozilla::components::ProtocolProxy::Service(&rv);
  }

  return rv;
}

already_AddRefed<nsIOService> nsIOService::GetInstance() {
  if (!gIOService) {
    RefPtr<nsIOService> ios = new nsIOService();
    if (NS_SUCCEEDED(ios->Init())) {
      MOZ_ASSERT(gIOService == ios.get());
      return ios.forget();
    }
  }
  return do_AddRef(gIOService);
}

NS_IMPL_ISUPPORTS(nsIOService, nsIIOService, nsINetUtil, nsISpeculativeConnect,
                  nsIObserver, nsIIOServiceInternal, nsISupportsWeakReference)

////////////////////////////////////////////////////////////////////////////////

nsresult nsIOService::RecheckCaptivePortal() {
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on the main thread");
  if (!mCaptivePortalService ||
      !StaticPrefs::network_captive_portal_service_enabled()) {
    return NS_OK;
  }
  nsCOMPtr<nsIRunnable> task = NewRunnableMethod(
      "nsIOService::RecheckCaptivePortal", mCaptivePortalService,
      &nsICaptivePortalService::RecheckCaptivePortal);
  return NS_DispatchToMainThread(task);
}

nsresult nsIOService::RecheckCaptivePortalIfLocalRedirect(nsIChannel* newChan) {
  nsresult rv;

  if (!mCaptivePortalService ||
      !StaticPrefs::network_captive_portal_service_enabled()) {
    return NS_OK;
  }

  nsCOMPtr<nsIURI> uri;
  rv = newChan->GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCString host;
  rv = uri->GetHost(host);
  if (NS_FAILED(rv)) {
    return rv;
  }

  NetAddr addr;
  // If the redirect wasn't to an IP literal, so there's probably no need
  // to trigger the captive portal detection right now. It can wait.
  if (NS_SUCCEEDED(addr.InitFromString(host)) && addr.IsIPAddrLocal()) {
    RecheckCaptivePortal();
  }

  return NS_OK;
}

nsresult nsIOService::AsyncOnChannelRedirect(
    nsIChannel* oldChan, nsIChannel* newChan, uint32_t flags,
    nsAsyncRedirectVerifyHelper* helper) {
  // If a redirect to a local network address occurs, then chances are we
  // are in a captive portal, so we trigger a recheck.
  RecheckCaptivePortalIfLocalRedirect(newChan);

  // This is silly. I wish there was a simpler way to get at the global
  // reference of the contentSecurityManager. But it lives in the XPCOM
  // service registry.
  nsCOMPtr<nsIChannelEventSink> sink;
  sink = mozilla::components::ContentSecurityManager::Service();
  if (sink) {
    nsresult rv =
        helper->DelegateOnChannelRedirect(sink, oldChan, newChan, flags);
    if (NS_FAILED(rv)) return rv;
  }

  // Finally, our category
  nsCOMArray<nsIChannelEventSink> entries;
  mChannelEventSinks.GetEntries(entries);
  int32_t len = entries.Count();
  for (int32_t i = 0; i < len; ++i) {
    nsresult rv =
        helper->DelegateOnChannelRedirect(entries[i], oldChan, newChan, flags);
    if (NS_FAILED(rv)) return rv;
  }

  nsCOMPtr<nsIHttpChannel> httpChan(do_QueryInterface(oldChan));

  // Collect the redirection from HTTP(S) only.
  if (httpChan) {
    MOZ_ASSERT(NS_IsMainThread());
    nsCOMPtr<nsIURI> newURI;
    newChan->GetURI(getter_AddRefs(newURI));
    MOZ_ASSERT(newURI);

    nsAutoCString scheme;
    newURI->GetScheme(scheme);
    MOZ_ASSERT(!scheme.IsEmpty());

    if (oldChan->IsDocument()) {
      mozilla::glean::networking::http_redirect_to_scheme_top_level.Get(scheme)
          .Add(1);
    } else {
      mozilla::glean::networking::http_redirect_to_scheme_subresource
          .Get(scheme)
          .Add(1);
    }
  }
  return NS_OK;
}

bool nsIOService::UsesExternalProtocolHandler(const nsACString& aScheme) {
  if (aScheme == "file"_ns || aScheme == "chrome"_ns ||
      aScheme == "resource"_ns || aScheme == "moz-src"_ns) {
    // Don't allow file:, chrome: or resource: URIs to be handled with
    // nsExternalProtocolHandler, since internally we rely on being able to
    // use and read from these URIs.
    return false;
  }

  if (aScheme == "place"_ns || aScheme == "fake-favicon-uri"_ns ||
      aScheme == "favicon"_ns || aScheme == "moz-nullprincipal"_ns) {
    // Force place: fake-favicon-uri: favicon: and moz-nullprincipal: URIs to be
    // handled with nsExternalProtocolHandler, and not with a dynamically
    // registered handler.
    return true;
  }

  // If prefs configure the URI to be handled externally, do so.
  for (const auto& scheme : mForceExternalSchemes) {
    if (aScheme == scheme) {
      return true;
    }
  }
  return false;
}

ProtocolHandlerInfo nsIOService::LookupProtocolHandler(
    const nsACString& aScheme) {
  // Look-ups are ASCII-case-insensitive, so lower-case the string before
  // continuing.
  nsAutoCString scheme(aScheme);
  ToLowerCase(scheme);

  // NOTE: If we could get rid of mForceExternalSchemes (or prevent them from
  // disabling static protocols), we could avoid locking mLock until we need to
  // check `mRuntimeProtocolHandlers.
  AutoReadLock lock(mLock);
  if (!UsesExternalProtocolHandler(scheme)) {
    // Try the static protocol handler first - they cannot be overridden by
    // dynamic protocols.
    if (const xpcom::StaticProtocolHandler* handler =
            xpcom::StaticProtocolHandler::Lookup(scheme)) {
      return ProtocolHandlerInfo(*handler);
    }
    if (auto handler = mRuntimeProtocolHandlers.Lookup(scheme)) {
      return ProtocolHandlerInfo(handler.Data());
    }
  }
  return ProtocolHandlerInfo(xpcom::StaticProtocolHandler::Default());
}

NS_IMETHODIMP
nsIOService::GetProtocolHandler(const char* scheme,
                                nsIProtocolHandler** result) {
  AssertIsOnMainThread();
  NS_ENSURE_ARG_POINTER(scheme);

  *result = LookupProtocolHandler(nsDependentCString(scheme)).Handler().take();
  return *result ? NS_OK : NS_ERROR_UNKNOWN_PROTOCOL;
}

NS_IMETHODIMP
nsIOService::ExtractScheme(const nsACString& inURI, nsACString& scheme) {
  return net_ExtractURLScheme(inURI, scheme);
}

NS_IMETHODIMP
nsIOService::HostnameIsLocalIPAddress(nsIURI* aURI, bool* aResult) {
  NS_ENSURE_ARG_POINTER(aURI);

  nsCOMPtr<nsIURI> innerURI = NS_GetInnermostURI(aURI);
  NS_ENSURE_ARG_POINTER(innerURI);

  nsAutoCString host;
  nsresult rv = innerURI->GetAsciiHost(host);
  if (NS_FAILED(rv)) {
    return rv;
  }

  *aResult = false;

  NetAddr addr;
  if (NS_SUCCEEDED(addr.InitFromString(host)) && addr.IsIPAddrLocal()) {
    *aResult = true;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsIOService::HostnameIsIPAddressAny(nsIURI* aURI, bool* aResult) {
  NS_ENSURE_ARG_POINTER(aURI);

  nsCOMPtr<nsIURI> innerURI = NS_GetInnermostURI(aURI);
  NS_ENSURE_ARG_POINTER(innerURI);

  nsAutoCString host;
  nsresult rv = innerURI->GetAsciiHost(host);
  if (NS_FAILED(rv)) {
    return rv;
  }

  *aResult = false;

  NetAddr addr;
  if (NS_SUCCEEDED(addr.InitFromString(host)) && addr.IsIPAddrAny()) {
    *aResult = true;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsIOService::HostnameIsSharedIPAddress(nsIURI* aURI, bool* aResult) {
  NS_ENSURE_ARG_POINTER(aURI);

  nsCOMPtr<nsIURI> innerURI = NS_GetInnermostURI(aURI);
  NS_ENSURE_ARG_POINTER(innerURI);

  nsAutoCString host;
  nsresult rv = innerURI->GetAsciiHost(host);
  if (NS_FAILED(rv)) {
    return rv;
  }

  *aResult = false;

  NetAddr addr;
  if (NS_SUCCEEDED(addr.InitFromString(host)) && addr.IsIPAddrShared()) {
    *aResult = true;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsIOService::IsValidHostname(const nsACString& inHostname, bool* aResult) {
  if (!net_IsValidDNSHost(inHostname)) {
    *aResult = false;
    return NS_OK;
  }

  // hostname ending with a "." delimited octet that is a number
  // must be IPv4 or IPv6 dual address
  nsAutoCString host(inHostname);
  if (IPv4Parser::EndsInANumber(host)) {
    // ipv6 dual address; for example "::1.2.3.4"
    if (net_IsValidIPv6Addr(host)) {
      *aResult = true;
      return NS_OK;
    }

    nsAutoCString normalized;
    nsresult rv = IPv4Parser::NormalizeIPv4(host, normalized);
    if (NS_FAILED(rv)) {
      *aResult = false;
      return NS_OK;
    }
  }
  *aResult = true;
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::GetProtocolFlags(const char* scheme, uint32_t* flags) {
  NS_ENSURE_ARG_POINTER(scheme);

  *flags =
      LookupProtocolHandler(nsDependentCString(scheme)).StaticProtocolFlags();
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::GetDynamicProtocolFlags(nsIURI* uri, uint32_t* flags) {
  AssertIsOnMainThread();
  NS_ENSURE_ARG(uri);

  nsAutoCString scheme;
  nsresult rv = uri->GetScheme(scheme);
  NS_ENSURE_SUCCESS(rv, rv);

  return LookupProtocolHandler(scheme).DynamicProtocolFlags(uri, flags);
}

NS_IMETHODIMP
nsIOService::GetDefaultPort(const char* scheme, int32_t* defaultPort) {
  NS_ENSURE_ARG_POINTER(scheme);

  *defaultPort =
      LookupProtocolHandler(nsDependentCString(scheme)).DefaultPort();
  return NS_OK;
}

nsresult nsIOService::NewURI(const nsACString& aSpec, const char* aCharset,
                             nsIURI* aBaseURI, nsIURI** result) {
  return NS_NewURI(result, aSpec, aCharset, aBaseURI);
}

NS_IMETHODIMP
nsIOService::NewFileURI(nsIFile* file, nsIURI** result) {
  nsresult rv;
  NS_ENSURE_ARG_POINTER(file);

  nsCOMPtr<nsIProtocolHandler> handler;

  rv = GetProtocolHandler("file", getter_AddRefs(handler));
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIFileProtocolHandler> fileHandler(do_QueryInterface(handler, &rv));
  if (NS_FAILED(rv)) return rv;

  return fileHandler->NewFileURI(file, result);
}

// static
already_AddRefed<nsIURI> nsIOService::CreateExposableURI(nsIURI* aURI) {
  MOZ_ASSERT(aURI, "Must have a URI");
  nsCOMPtr<nsIURI> uri = aURI;
  bool hasUserPass;
  if (NS_SUCCEEDED(aURI->GetHasUserPass(&hasUserPass)) && hasUserPass) {
    DebugOnly<nsresult> rv = NS_MutateURI(uri).SetUserPass(""_ns).Finalize(uri);
    MOZ_ASSERT(NS_SUCCEEDED(rv) && uri, "Mutating URI should never fail");
  }
  return uri.forget();
}

NS_IMETHODIMP
nsIOService::CreateExposableURI(nsIURI* aURI, nsIURI** _result) {
  NS_ENSURE_ARG_POINTER(aURI);
  NS_ENSURE_ARG_POINTER(_result);
  nsCOMPtr<nsIURI> exposableURI = CreateExposableURI(aURI);
  exposableURI.forget(_result);
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::NewChannelFromURI(nsIURI* aURI, nsINode* aLoadingNode,
                               nsIPrincipal* aLoadingPrincipal,
                               nsIPrincipal* aTriggeringPrincipal,
                               uint32_t aSecurityFlags,
                               nsContentPolicyType aContentPolicyType,
                               nsIChannel** result) {
  return NewChannelFromURIWithProxyFlags(aURI,
                                         nullptr,  // aProxyURI
                                         0,        // aProxyFlags
                                         aLoadingNode, aLoadingPrincipal,
                                         aTriggeringPrincipal, aSecurityFlags,
                                         aContentPolicyType, result);
}
nsresult nsIOService::NewChannelFromURIWithClientAndController(
    nsIURI* aURI, nsINode* aLoadingNode, nsIPrincipal* aLoadingPrincipal,
    nsIPrincipal* aTriggeringPrincipal,
    const Maybe<ClientInfo>& aLoadingClientInfo,
    const Maybe<ServiceWorkerDescriptor>& aController, uint32_t aSecurityFlags,
    nsContentPolicyType aContentPolicyType, uint32_t aSandboxFlags,
    uint64_t aAssociatedBrowsingContextID, nsIChannel** aResult) {
  return NewChannelFromURIWithProxyFlagsInternal(
      aURI,
      nullptr,  // aProxyURI
      0,        // aProxyFlags
      aLoadingNode, aLoadingPrincipal, aTriggeringPrincipal, aLoadingClientInfo,
      aController, aSecurityFlags, aContentPolicyType, aSandboxFlags,
      aAssociatedBrowsingContextID, aResult);
}

NS_IMETHODIMP
nsIOService::NewChannelFromURIWithLoadInfo(nsIURI* aURI, nsILoadInfo* aLoadInfo,
                                           nsIChannel** result) {
  return NewChannelFromURIWithProxyFlagsInternal(aURI,
                                                 nullptr,  // aProxyURI
                                                 0,        // aProxyFlags
                                                 aLoadInfo, result);
}

NS_IMETHODIMP
nsIOService::NewChannelFromURIWithProxyFlagsAndLoadInfo(nsIURI* aURI,
                                                        nsIURI* aProxyURI,
                                                        uint32_t aProxyFlags,
                                                        nsILoadInfo* aLoadInfo,
                                                        nsIChannel** result) {
  return NewChannelFromURIWithProxyFlagsInternal(aURI, aProxyURI, aProxyFlags,
                                                 aLoadInfo, result);
}

nsresult nsIOService::NewChannelFromURIWithProxyFlagsInternal(
    nsIURI* aURI, nsIURI* aProxyURI, uint32_t aProxyFlags,
    nsINode* aLoadingNode, nsIPrincipal* aLoadingPrincipal,
    nsIPrincipal* aTriggeringPrincipal,
    const Maybe<ClientInfo>& aLoadingClientInfo,
    const Maybe<ServiceWorkerDescriptor>& aController, uint32_t aSecurityFlags,
    nsContentPolicyType aContentPolicyType, uint32_t aSandboxFlags,
    uint64_t aAssociatedBrowsingContextID, nsIChannel** result) {
  nsCOMPtr<nsILoadInfo> loadInfo = MOZ_TRY(LoadInfo::Create(
      aLoadingPrincipal, aTriggeringPrincipal, aLoadingNode, aSecurityFlags,
      aContentPolicyType, aLoadingClientInfo, aController, aSandboxFlags));
  if (aAssociatedBrowsingContextID) {
    MOZ_ALWAYS_SUCCEEDS(
        loadInfo->SetAssociatedBrowsingContextID(aAssociatedBrowsingContextID));
  }
  return NewChannelFromURIWithProxyFlagsInternal(aURI, aProxyURI, aProxyFlags,
                                                 loadInfo, result);
}

nsresult nsIOService::NewChannelFromURIWithProxyFlagsInternal(
    nsIURI* aURI, nsIURI* aProxyURI, uint32_t aProxyFlags,
    nsILoadInfo* aLoadInfo, nsIChannel** result) {
  nsresult rv;
  NS_ENSURE_ARG_POINTER(aURI);
  // all channel creations must provide a valid loadinfo
  MOZ_ASSERT(aLoadInfo, "can not create channel without aLoadInfo");
  NS_ENSURE_ARG_POINTER(aLoadInfo);

  nsAutoCString scheme;
  rv = aURI->GetScheme(scheme);
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIProtocolHandler> handler;
  rv = GetProtocolHandler(scheme.get(), getter_AddRefs(handler));
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIChannel> channel;
  nsCOMPtr<nsIProxiedProtocolHandler> pph = do_QueryInterface(handler);
  if (pph) {
    rv = pph->NewProxiedChannel(aURI, nullptr, aProxyFlags, aProxyURI,
                                aLoadInfo, getter_AddRefs(channel));
  } else {
    rv = handler->NewChannel(aURI, aLoadInfo, getter_AddRefs(channel));
  }
  if (NS_FAILED(rv)) return rv;

  // Make sure that all the individual protocolhandlers attach a loadInfo.
  nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
  if (aLoadInfo != loadInfo) {
    MOZ_ASSERT(false, "newly created channel must have a loadinfo attached");
    return NS_ERROR_UNEXPECTED;
  }

  // If we're sandboxed, make sure to clear any owner the channel
  // might already have.
  if (loadInfo->GetLoadingSandboxed()) {
    channel->SetOwner(nullptr);
  }
  channel.forget(result);
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::NewChannelFromURIWithProxyFlags(
    nsIURI* aURI, nsIURI* aProxyURI, uint32_t aProxyFlags,
    nsINode* aLoadingNode, nsIPrincipal* aLoadingPrincipal,
    nsIPrincipal* aTriggeringPrincipal, uint32_t aSecurityFlags,
    nsContentPolicyType aContentPolicyType, nsIChannel** result) {
  return NewChannelFromURIWithProxyFlagsInternal(
      aURI, aProxyURI, aProxyFlags, aLoadingNode, aLoadingPrincipal,
      aTriggeringPrincipal, Maybe<ClientInfo>(),
      Maybe<ServiceWorkerDescriptor>(), aSecurityFlags, aContentPolicyType,
      /* aSandboxFlags */ 0, /* aAssociatedBrowsingContextID */ 0, result);
}

NS_IMETHODIMP
nsIOService::NewChannel(const nsACString& aSpec, const char* aCharset,
                        nsIURI* aBaseURI, nsINode* aLoadingNode,
                        nsIPrincipal* aLoadingPrincipal,
                        nsIPrincipal* aTriggeringPrincipal,
                        uint32_t aSecurityFlags,
                        nsContentPolicyType aContentPolicyType,
                        nsIChannel** result) {
  nsresult rv;
  nsCOMPtr<nsIURI> uri;
  rv = NewURI(aSpec, aCharset, aBaseURI, getter_AddRefs(uri));
  if (NS_FAILED(rv)) return rv;

  return NewChannelFromURI(uri, aLoadingNode, aLoadingPrincipal,
                           aTriggeringPrincipal, aSecurityFlags,
                           aContentPolicyType, result);
}

NS_IMETHODIMP
nsIOService::NewSuspendableChannelWrapper(
    nsIChannel* aInnerChannel, nsISuspendableChannelWrapper** result) {
  NS_ENSURE_ARG_POINTER(aInnerChannel);

  nsCOMPtr<nsISuspendableChannelWrapper> wrapper =
      new SuspendableChannelWrapper(aInnerChannel);
  wrapper.forget(result);
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::NewWebTransport(nsIWebTransport** result) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsIWebTransport> webTransport = new WebTransportSessionProxy();

  webTransport.forget(result);
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::OriginAttributesForNetworkState(
    nsIChannel* aChannel, JSContext* cx, JS::MutableHandle<JS::Value> _retval) {
  OriginAttributes attrs;
  if (!StoragePrincipalHelper::GetOriginAttributesForNetworkState(aChannel,
                                                                  attrs)) {
    return NS_ERROR_FAILURE;
  }

  if (NS_WARN_IF(!mozilla::dom::ToJSValue(cx, attrs, _retval))) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

bool nsIOService::IsLinkUp() {
  InitializeNetworkLinkService();

  if (!mNetworkLinkService) {
    // We cannot decide, assume the link is up
    return true;
  }

  bool isLinkUp;
  nsresult rv;
  rv = mNetworkLinkService->GetIsLinkUp(&isLinkUp);
  if (NS_FAILED(rv)) {
    return true;
  }

  return isLinkUp;
}

NS_IMETHODIMP
nsIOService::GetOffline(bool* offline) {
  if (StaticPrefs::network_offline_mirrors_connectivity()) {
    *offline = mOffline || !mConnectivity;
  } else {
    *offline = mOffline;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::SetOffline(bool offline) { return SetOfflineInternal(offline); }

nsresult nsIOService::SetOfflineInternal(bool offline) {
  LOG(("nsIOService::SetOffline offline=%d\n", offline));
  // When someone wants to go online (!offline) after we got XPCOM shutdown
  // throw ERROR_NOT_AVAILABLE to prevent return to online state.
  if ((mShutdown || mOfflineForProfileChange) && !offline) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // SetOffline() may re-enter while it's shutting down services.
  // If that happens, save the most recent value and it will be
  // processed when the first SetOffline() call is done bringing
  // down the service.
  mSetOfflineValue = offline;
  if (mSettingOffline) {
    return NS_OK;
  }

  mSettingOffline = true;

  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();

  NS_ASSERTION(observerService, "The observer service should not be null");

  if (XRE_IsParentProcess() && observerService) {
    (void)observerService->NotifyObservers(nullptr,
                                           NS_IPC_IOSERVICE_SET_OFFLINE_TOPIC,
                                           offline ? u"true" : u"false");
  }

  nsIIOService* subject = static_cast<nsIIOService*>(this);
  while (mSetOfflineValue != mOffline) {
    offline = mSetOfflineValue;

    if (offline && !mOffline) {
      mOffline = true;  // indicate we're trying to shutdown

      // don't care if notifications fail
      if (observerService) {
        observerService->NotifyObservers(subject,
                                         NS_IOSERVICE_GOING_OFFLINE_TOPIC,
                                         u"" NS_IOSERVICE_OFFLINE);
      }

      if (mSocketTransportService) mSocketTransportService->SetOffline(true);

      mLastOfflineStateChange = PR_IntervalNow();
      if (observerService) {
        observerService->NotifyObservers(subject,
                                         NS_IOSERVICE_OFFLINE_STATUS_TOPIC,
                                         u"" NS_IOSERVICE_OFFLINE);
      }
    } else if (!offline && mOffline) {
      // go online
      InitializeSocketTransportService();
      mOffline = false;  // indicate success only AFTER we've
                         // brought up the services

      mLastOfflineStateChange = PR_IntervalNow();
      // don't care if notification fails
      // Only send the ONLINE notification if there is connectivity
      if (observerService && mConnectivity) {
        observerService->NotifyObservers(subject,
                                         NS_IOSERVICE_OFFLINE_STATUS_TOPIC,
                                         (u"" NS_IOSERVICE_ONLINE));
      }
    }
  }

  // Don't notify here, as the above notifications (if used) suffice.
  if ((mShutdown || mOfflineForProfileChange) && mOffline) {
    if (mSocketTransportService) {
      DebugOnly<nsresult> rv = mSocketTransportService->Shutdown(mShutdown);
      NS_ASSERTION(NS_SUCCEEDED(rv),
                   "socket transport service shutdown failed");
    }
  }

  mSettingOffline = false;

  return NS_OK;
}

NS_IMETHODIMP
nsIOService::GetConnectivity(bool* aConnectivity) {
  *aConnectivity = mConnectivity;
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::SetConnectivity(bool aConnectivity) {
  LOG(("nsIOService::SetConnectivity aConnectivity=%d\n", aConnectivity));
  // This should only be called from ContentChild to pass the connectivity
  // value from the chrome process to the content process.
  if (XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return SetConnectivityInternal(aConnectivity);
}

NS_IMETHODIMP
nsIOService::SetConnectivityForTesting(bool aConnectivity) {
  return SetConnectivityInternal(aConnectivity);
}

nsresult nsIOService::SetConnectivityInternal(bool aConnectivity) {
  LOG(("nsIOService::SetConnectivityInternal aConnectivity=%d\n",
       aConnectivity));
  if (mConnectivity == aConnectivity) {
    // Nothing to do here.
    return NS_OK;
  }
  mConnectivity = aConnectivity;

  // This is used for PR_Connect PR_Close telemetry so it is important that
  // we have statistic about network change event even if we are offline.
  mLastConnectivityChange = PR_IntervalNow();

  if (mCaptivePortalService) {
    if (aConnectivity &&
        StaticPrefs::network_captive_portal_service_enabled()) {
      // This will also trigger a captive portal check for the new network
      static_cast<CaptivePortalService*>(mCaptivePortalService.get())->Start();
    } else {
      static_cast<CaptivePortalService*>(mCaptivePortalService.get())->Stop();
    }
  }

  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (!observerService) {
    return NS_OK;
  }
  // This notification sends the connectivity to the child processes
  if (XRE_IsParentProcess()) {
    observerService->NotifyObservers(nullptr,
                                     NS_IPC_IOSERVICE_SET_CONNECTIVITY_TOPIC,
                                     aConnectivity ? u"true" : u"false");
  }

  if (mOffline) {
    // We don't need to send any notifications if we're offline
    return NS_OK;
  }

  if (aConnectivity) {
    // If we were previously offline due to connectivity=false,
    // send the ONLINE notification
    observerService->NotifyObservers(static_cast<nsIIOService*>(this),
                                     NS_IOSERVICE_OFFLINE_STATUS_TOPIC,
                                     (u"" NS_IOSERVICE_ONLINE));
  } else {
    // If we were previously online and lost connectivity
    // send the OFFLINE notification
    observerService->NotifyObservers(static_cast<nsIIOService*>(this),
                                     NS_IOSERVICE_GOING_OFFLINE_TOPIC,
                                     u"" NS_IOSERVICE_OFFLINE);
    observerService->NotifyObservers(static_cast<nsIIOService*>(this),
                                     NS_IOSERVICE_OFFLINE_STATUS_TOPIC,
                                     u"" NS_IOSERVICE_OFFLINE);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::AllowPort(int32_t inPort, const char* scheme, bool* _retval) {
  int32_t port = inPort;
  if (port == -1) {
    *_retval = true;
    return NS_OK;
  }

  if (port <= 0 || port > std::numeric_limits<uint16_t>::max()) {
    *_retval = false;
    return NS_OK;
  }

  nsTArray<int32_t> restrictedPortList;
  {
    AutoReadLock lock(mLock);
    restrictedPortList.Assign(mRestrictedPortList);
  }
  // first check to see if the port is in our blacklist:
  int32_t badPortListCnt = restrictedPortList.Length();
  for (int i = 0; i < badPortListCnt; i++) {
    if (port == restrictedPortList[i]) {
      *_retval = false;

      // check to see if the protocol wants to override
      if (!scheme) return NS_OK;

      // We don't support get protocol handler off main thread.
      if (!NS_IsMainThread()) {
        return NS_OK;
      }
      nsCOMPtr<nsIProtocolHandler> handler;
      nsresult rv = GetProtocolHandler(scheme, getter_AddRefs(handler));
      if (NS_FAILED(rv)) return rv;

      // let the protocol handler decide
      return handler->AllowPort(port, scheme, _retval);
    }
  }

  *_retval = true;
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::AddBlockedLocalPort(int32_t aPort) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (aPort <= 0 || aPort > std::numeric_limits<uint16_t>::max()) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoWriteLock lock(mLock);
  if (!mBlockedLocalPorts.Contains(aPort)) {
    mBlockedLocalPorts.AppendElement(aPort);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::RemoveBlockedLocalPort(int32_t aPort) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (aPort <= 0 || aPort > std::numeric_limits<uint16_t>::max()) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoWriteLock lock(mLock);
  mBlockedLocalPorts.RemoveElement(aPort);
  return NS_OK;
}

bool nsIOService::IsLocalPortBlocked(uint16_t aPort) {
  AutoReadLock lock(mLock);
  return mBlockedLocalPorts.Contains(aPort);
}

////////////////////////////////////////////////////////////////////////////////

// static
void nsIOService::PrefsChanged(const char* pref, void* self) {
  static_cast<nsIOService*>(self)->PrefsChanged(pref);
}

void nsIOService::PrefsChanged(const char* pref) {
  // Look for extra ports to block
  if (!pref || strcmp(pref, PORT_PREF("banned")) == 0) {
    ParsePortList(PORT_PREF("banned"), false);
  }

  // ...as well as previous blocks to remove.
  if (!pref || strcmp(pref, PORT_PREF("banned.override")) == 0) {
    ParsePortList(PORT_PREF("banned.override"), true);
  }

  if (!pref || strcmp(pref, MANAGE_OFFLINE_STATUS_PREF) == 0) {
    bool manage;
    if (mNetworkLinkServiceInitialized &&
        NS_SUCCEEDED(
            Preferences::GetBool(MANAGE_OFFLINE_STATUS_PREF, &manage))) {
      LOG(("nsIOService::PrefsChanged ManageOfflineStatus manage=%d\n",
           manage));
      SetManageOfflineStatus(manage);
    }
  }

  if (!pref || strcmp(pref, NECKO_BUFFER_CACHE_COUNT_PREF) == 0) {
    int32_t count;
    if (NS_SUCCEEDED(
            Preferences::GetInt(NECKO_BUFFER_CACHE_COUNT_PREF, &count))) {
      /* check for bogus values and default if we find such a value */
      if (count > 0) gDefaultSegmentCount = count;
    }
  }

  if (!pref || strcmp(pref, NECKO_BUFFER_CACHE_SIZE_PREF) == 0) {
    int32_t size;
    if (NS_SUCCEEDED(
            Preferences::GetInt(NECKO_BUFFER_CACHE_SIZE_PREF, &size))) {
      /* check for bogus values and default if we find such a value
       * the upper limit here is arbitrary. having a 1mb segment size
       * is pretty crazy.  if you remove this, consider adding some
       * integer rollover test.
       */
      if (size > 0 && size < 1024 * 1024) gDefaultSegmentSize = size;
    }
    NS_WARNING_ASSERTION(!(size & (size - 1)),
                         "network segment size is not a power of 2!");
  }

  if (!pref || strcmp(pref, NETWORK_CAPTIVE_PORTAL_PREF) == 0) {
    if (mCaptivePortalService) {
      if (StaticPrefs::network_captive_portal_service_enabled()) {
        static_cast<CaptivePortalService*>(mCaptivePortalService.get())
            ->Start();
      } else {
        static_cast<CaptivePortalService*>(mCaptivePortalService.get())->Stop();
      }
    }
  }

  if (!pref || strncmp(pref, FORCE_EXTERNAL_PREF_PREFIX,
                       strlen(FORCE_EXTERNAL_PREF_PREFIX)) == 0) {
    nsTArray<nsCString> prefs;
    if (nsIPrefBranch* prefRootBranch = Preferences::GetRootBranch()) {
      prefRootBranch->GetChildList(FORCE_EXTERNAL_PREF_PREFIX, prefs);
    }
    nsTArray<nsCString> forceExternalSchemes;
    for (const auto& pref : prefs) {
      if (Preferences::GetBool(pref.get(), false)) {
        forceExternalSchemes.AppendElement(
            Substring(pref, strlen(FORCE_EXTERNAL_PREF_PREFIX)));
      }
    }
    AutoWriteLock lock(mLock);
    mForceExternalSchemes = std::move(forceExternalSchemes);
  }

  if (!pref || strncmp(pref, SIMPLE_URI_SCHEMES_PREF,
                       strlen(SIMPLE_URI_SCHEMES_PREF)) == 0) {
    LOG(("simple_uri_unknown_schemes pref changed, updating the scheme list"));
    mSimpleURIUnknownSchemes.ParseAndMergePrefSchemes();
    // runs on parent and child, no need to broadcast
  }

  if (!pref || strncmp(pref, PREF_LNA_IP_ADDR_SPACE_PUBLIC,
                       strlen(PREF_LNA_IP_ADDR_SPACE_PUBLIC)) == 0) {
    AutoWriteLock lock(mLock);
    UpdateAddressSpaceOverrideList(PREF_LNA_IP_ADDR_SPACE_PUBLIC,
                                   mPublicAddressSpaceOverridesList);
  }

  if (!pref || strncmp(pref, PREF_LNA_IP_ADDR_SPACE_PRIVATE,
                       strlen(PREF_LNA_IP_ADDR_SPACE_PRIVATE)) == 0) {
    AutoWriteLock lock(mLock);
    UpdateAddressSpaceOverrideList(PREF_LNA_IP_ADDR_SPACE_PRIVATE,
                                   mPrivateAddressSpaceOverridesList);
  }
  if (!pref || strncmp(pref, PREF_LNA_IP_ADDR_SPACE_LOCAL,
                       strlen(PREF_LNA_IP_ADDR_SPACE_LOCAL)) == 0) {
    AutoWriteLock lock(mLock);
    UpdateAddressSpaceOverrideList(PREF_LNA_IP_ADDR_SPACE_LOCAL,
                                   mLocalAddressSpaceOverrideList);
  }
  if (!pref || strncmp(pref, PREF_LNA_SKIP_DOMAINS,
                       strlen(PREF_LNA_SKIP_DOMAINS)) == 0) {
    UpdateSkipDomainsList();
  }
}

void nsIOService::UpdateAddressSpaceOverrideList(
    const char* aPrefName, nsTArray<nsCString>& aTargetList) {
  nsAutoCString aAddressSpaceOverrides;
  Preferences::GetCString(aPrefName, aAddressSpaceOverrides);

  nsTArray<nsCString> addressSpaceOverridesArray;
  nsCCharSeparatedTokenizer tokenizer(aAddressSpaceOverrides, ',');
  while (tokenizer.hasMoreTokens()) {
    nsAutoCString token(tokenizer.nextToken());
    token.StripWhitespace();
    addressSpaceOverridesArray.AppendElement(token);
  }

  aTargetList = std::move(addressSpaceOverridesArray);
}

void nsIOService::UpdateSkipDomainsList() {
  nsAutoCString skipDomains;
  Preferences::GetCString(PREF_LNA_SKIP_DOMAINS, skipDomains);

  nsTArray<nsCString> skipDomainsArray;
  nsCCharSeparatedTokenizer tokenizer(skipDomains, ',');
  while (tokenizer.hasMoreTokens()) {
    nsAutoCString token(tokenizer.nextToken());
    token.StripWhitespace();
    if (!token.IsEmpty()) {
      skipDomainsArray.AppendElement(token);
    }
  }

  AutoWriteLock lock(mLock);
  mLNASkipDomainsList = std::move(skipDomainsArray);
}

bool nsIOService::ShouldSkipDomainForLNA(const nsACString& aDomain) {
  AutoReadLock lock(mLock);

  // Check each domain pattern
  for (const auto& pattern : mLNASkipDomainsList) {
    // Special case: plain "*" matches all domains
    if (pattern.Equals("*"_ns)) {
      return true;
    }

    // Suffix wildcard pattern (starts with *.)
    if (StringBeginsWith(pattern, "*."_ns)) {
      nsDependentCSubstring suffix(Substring(pattern, 2));
      nsDependentCSubstring suffixWithDot(Substring(pattern, 1));
      if (aDomain == suffix || StringEndsWith(aDomain, suffixWithDot)) {
        return true;
      }
    }

    // Exact match
    if (pattern == aDomain) {
      return true;
    }
  }

  return false;
}

void nsIOService::ParsePortList(const char* pref, bool remove) {
  nsAutoCString portList;
  nsTArray<int32_t> restrictedPortList;
  {
    AutoWriteLock lock(mLock);
    restrictedPortList.Assign(std::move(mRestrictedPortList));
  }
  // Get a pref string and chop it up into a list of ports.
  Preferences::GetCString(pref, portList);
  if (!portList.IsVoid()) {
    nsTArray<nsCString> portListArray;
    ParseString(portList, ',', portListArray);
    uint32_t index;
    for (index = 0; index < portListArray.Length(); index++) {
      portListArray[index].StripWhitespace();
      int32_t portBegin, portEnd;

      if (PR_sscanf(portListArray[index].get(), "%d-%d", &portBegin,
                    &portEnd) == 2) {
        if ((portBegin < 65536) && (portEnd < 65536)) {
          int32_t curPort;
          if (remove) {
            for (curPort = portBegin; curPort <= portEnd; curPort++) {
              restrictedPortList.RemoveElement(curPort);
            }
          } else {
            for (curPort = portBegin; curPort <= portEnd; curPort++) {
              restrictedPortList.AppendElement(curPort);
            }
          }
        }
      } else {
        nsresult aErrorCode;
        int32_t port = portListArray[index].ToInteger(&aErrorCode);
        if (NS_SUCCEEDED(aErrorCode) && port < 65536) {
          if (remove) {
            restrictedPortList.RemoveElement(port);
          } else {
            restrictedPortList.AppendElement(port);
          }
        }
      }
    }
  }

  AutoWriteLock lock(mLock);
  mRestrictedPortList.Assign(std::move(restrictedPortList));
}

class nsWakeupNotifier : public Runnable {
 public:
  explicit nsWakeupNotifier(nsIIOServiceInternal* ioService)
      : Runnable("net::nsWakeupNotifier"), mIOService(ioService) {}

  NS_IMETHOD Run() override { return mIOService->NotifyWakeup(); }

 private:
  virtual ~nsWakeupNotifier() = default;
  nsCOMPtr<nsIIOServiceInternal> mIOService;
};

NS_IMETHODIMP
nsIOService::NotifyWakeup() {
  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();

  NS_ASSERTION(observerService, "The observer service should not be null");

  if (observerService && StaticPrefs::network_notify_changed()) {
    (void)observerService->NotifyObservers(nullptr, NS_NETWORK_LINK_TOPIC,
                                           (u"" NS_NETWORK_LINK_DATA_CHANGED));
  }

  RecheckCaptivePortal();

  return NS_OK;
}

void nsIOService::SetHttpHandlerAlreadyShutingDown() {
  if (!mShutdown && !mOfflineForProfileChange) {
    mNetTearingDownStarted = PR_IntervalNow();
    mHttpHandlerAlreadyShutingDown = true;
  }
}

// nsIObserver interface
NS_IMETHODIMP
nsIOService::Observe(nsISupports* subject, const char* topic,
                     const char16_t* data) {
  if (!strcmp(topic, kProfileChangeNetTeardownTopic)) {
    if (!mHttpHandlerAlreadyShutingDown) {
      mNetTearingDownStarted = PR_IntervalNow();
    }
    mHttpHandlerAlreadyShutingDown = false;
    if (!mOffline) {
      mOfflineForProfileChange = true;
      SetOfflineInternal(true);
    }
  } else if (!strcmp(topic, kProfileDoChange)) {
    if (data && u"startup"_ns.Equals(data)) {
      // Lazy initialization of network link service (see bug 620472)
      InitializeNetworkLinkService();
      // Set up the initilization flag regardless the actuall result.
      // If we fail here, we will fail always on.
      mNetworkLinkServiceInitialized = true;

      // And now reflect the preference setting
      PrefsChanged(MANAGE_OFFLINE_STATUS_PREF);

      // Bug 870460 - Read cookie database at an early-as-possible time
      // off main thread. Hence, we have more chance to finish db query
      // before something calls into the cookie service.
      nsCOMPtr<nsISupports> cookieServ =
          do_GetService(NS_COOKIESERVICE_CONTRACTID);
    }
  } else if (!strcmp(topic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    // Remember we passed XPCOM shutdown notification to prevent any
    // changes of the offline status from now. We must not allow going
    // online after this point.
    mShutdown = true;

    if (!mHttpHandlerAlreadyShutingDown && !mOfflineForProfileChange) {
      mNetTearingDownStarted = PR_IntervalNow();
    }
    mHttpHandlerAlreadyShutingDown = false;

    SetOfflineInternal(true);

    if (mCaptivePortalService) {
      static_cast<CaptivePortalService*>(mCaptivePortalService.get())->Stop();
      mCaptivePortalService = nullptr;
    }

    if (XRE_IsParentProcess()) {
      SSLTokensCache::Shutdown();
    }

    // We're in XPCOM shutdown now. Unregister any dynamic protocol handlers
    // after this point to avoid leaks.
    {
      AutoWriteLock lock(mLock);
      mRuntimeProtocolHandlers.Clear();
    }
  } else if (!strcmp(topic, NS_NETWORK_LINK_TOPIC)) {
    OnNetworkLinkEvent(NS_ConvertUTF16toUTF8(data).get());
  } else if (!strcmp(topic, NS_NETWORK_ID_CHANGED_TOPIC)) {
    LOG(("nsIOService::OnNetworkLinkEvent Network id changed"));
  } else if (!strcmp(topic, NS_WIDGET_WAKE_OBSERVER_TOPIC)) {
    // coming back alive from sleep
    // this indirection brought to you by:
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1152048#c19
    nsCOMPtr<nsIRunnable> wakeupNotifier = new nsWakeupNotifier(this);
    NS_DispatchToMainThread(wakeupNotifier);
    mInSleepMode = false;
  } else if (!strcmp(topic, NS_WIDGET_SLEEP_OBSERVER_TOPIC)) {
    mInSleepMode = true;
  }

  return NS_OK;
}

// nsINetUtil interface
NS_IMETHODIMP
nsIOService::ParseRequestContentType(const nsACString& aTypeHeader,
                                     nsACString& aCharset, bool* aHadCharset,
                                     nsACString& aContentType) {
  net_ParseRequestContentType(aTypeHeader, aContentType, aCharset, aHadCharset);
  return NS_OK;
}

// nsINetUtil interface
NS_IMETHODIMP
nsIOService::ParseResponseContentType(const nsACString& aTypeHeader,
                                      nsACString& aCharset, bool* aHadCharset,
                                      nsACString& aContentType) {
  net_ParseContentType(aTypeHeader, aContentType, aCharset, aHadCharset);
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::ProtocolHasFlags(nsIURI* uri, uint32_t flags, bool* result) {
  NS_ENSURE_ARG(uri);

  *result = false;
  nsAutoCString scheme;
  nsresult rv = uri->GetScheme(scheme);
  NS_ENSURE_SUCCESS(rv, rv);

  auto handler = LookupProtocolHandler(scheme);

  uint32_t protocolFlags;
  if (flags & nsIProtocolHandler::DYNAMIC_URI_FLAGS) {
    AssertIsOnMainThread();
    rv = handler.DynamicProtocolFlags(uri, &protocolFlags);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    protocolFlags = handler.StaticProtocolFlags();
  }

  *result = (protocolFlags & flags) == flags;
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::URIChainHasFlags(nsIURI* uri, uint32_t flags, bool* result) {
  nsresult rv = ProtocolHasFlags(uri, flags, result);
  NS_ENSURE_SUCCESS(rv, rv);

  if (*result) {
    return rv;
  }

  // Dig deeper into the chain.  Note that this is not a do/while loop to
  // avoid the extra addref/release on |uri| in the common (non-nested) case.
  nsCOMPtr<nsINestedURI> nestedURI = do_QueryInterface(uri);
  while (nestedURI) {
    nsCOMPtr<nsIURI> innerURI;
    rv = nestedURI->GetInnerURI(getter_AddRefs(innerURI));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = ProtocolHasFlags(innerURI, flags, result);

    if (*result) {
      return rv;
    }

    nestedURI = do_QueryInterface(innerURI);
  }

  return rv;
}

NS_IMETHODIMP
nsIOService::SetManageOfflineStatus(bool aManage) {
  LOG(("nsIOService::SetManageOfflineStatus aManage=%d\n", aManage));
  mManageLinkStatus = aManage;

  // When detection is not activated, the default connectivity state is true.
  if (!mManageLinkStatus) {
    SetConnectivityInternal(true);
    return NS_OK;
  }

  InitializeNetworkLinkService();
  // If the NetworkLinkService is already initialized, it does not call
  // OnNetworkLinkEvent. This is needed, when mManageLinkStatus goes from
  // false to true.
  OnNetworkLinkEvent(NS_NETWORK_LINK_DATA_UNKNOWN);
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::GetManageOfflineStatus(bool* aManage) {
  *aManage = mManageLinkStatus;
  return NS_OK;
}

// input argument 'data' is already UTF8'ed
nsresult nsIOService::OnNetworkLinkEvent(const char* data) {
  if (IsNeckoChild()) {
    // There is nothing IO service could do on the child process
    // with this at the moment.  Feel free to add functionality
    // here at will, though.
    return NS_OK;
  }

  if (mShutdown) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCString dataAsString(data);
  for (auto* cp : mozilla::dom::ContentParent::AllProcesses(
           mozilla::dom::ContentParent::eLive)) {
    PNeckoParent* neckoParent = SingleManagedOrNull(cp->ManagedPNeckoParent());
    if (!neckoParent) {
      continue;
    }
    (void)neckoParent->SendNetworkChangeNotification(dataAsString);
  }

  LOG(("nsIOService::OnNetworkLinkEvent data:%s\n", data));
  if (!mNetworkLinkService) {
    return NS_ERROR_FAILURE;
  }

  if (!mManageLinkStatus) {
    LOG(("nsIOService::OnNetworkLinkEvent mManageLinkStatus=false\n"));
    return NS_OK;
  }

  bool isUp = true;
  if (!strcmp(data, NS_NETWORK_LINK_DATA_CHANGED)) {
    mLastNetworkLinkChange = PR_IntervalNow();
    // CHANGED means UP/DOWN didn't change
    // but the status of the captive portal may have changed.
    RecheckCaptivePortal();
    return NS_OK;
  }
  if (!strcmp(data, NS_NETWORK_LINK_DATA_DOWN)) {
    isUp = false;
  } else if (!strcmp(data, NS_NETWORK_LINK_DATA_UP)) {
    isUp = true;
  } else if (!strcmp(data, NS_NETWORK_LINK_DATA_UNKNOWN)) {
    nsresult rv = mNetworkLinkService->GetIsLinkUp(&isUp);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    NS_WARNING("Unhandled network event!");
    return NS_OK;
  }

  return SetConnectivityInternal(isUp);
}

NS_IMETHODIMP
nsIOService::EscapeString(const nsACString& aString, uint32_t aEscapeType,
                          nsACString& aResult) {
  NS_ENSURE_ARG_MAX(aEscapeType, 4);

  nsAutoCString stringCopy(aString);
  nsCString result;

  if (!NS_Escape(stringCopy, result, (nsEscapeMask)aEscapeType)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  aResult.Assign(result);

  return NS_OK;
}

NS_IMETHODIMP
nsIOService::EscapeURL(const nsACString& aStr, uint32_t aFlags,
                       nsACString& aResult) {
  aResult.Truncate();
  NS_EscapeURL(aStr.BeginReading(), aStr.Length(), aFlags | esc_AlwaysCopy,
               aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::UnescapeString(const nsACString& aStr, uint32_t aFlags,
                            nsACString& aResult) {
  aResult.Truncate();
  NS_UnescapeURL(aStr.BeginReading(), aStr.Length(), aFlags | esc_AlwaysCopy,
                 aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::ExtractCharsetFromContentType(const nsACString& aTypeHeader,
                                           nsACString& aCharset,
                                           int32_t* aCharsetStart,
                                           int32_t* aCharsetEnd,
                                           bool* aHadCharset) {
  nsAutoCString ignored;
  net_ParseContentType(aTypeHeader, ignored, aCharset, aHadCharset,
                       aCharsetStart, aCharsetEnd);
  if (*aHadCharset && *aCharsetStart == *aCharsetEnd) {
    *aHadCharset = false;
  }
  return NS_OK;
}

// nsISpeculativeConnect
class IOServiceProxyCallback final : public nsIProtocolProxyCallback {
  ~IOServiceProxyCallback() = default;

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPROTOCOLPROXYCALLBACK

  IOServiceProxyCallback(nsIInterfaceRequestor* aCallbacks,
                         nsIOService* aIOService,
                         Maybe<OriginAttributes>&& aOriginAttributes)
      : mCallbacks(aCallbacks),
        mIOService(aIOService),
        mOriginAttributes(std::move(aOriginAttributes)) {}

 private:
  RefPtr<nsIInterfaceRequestor> mCallbacks;
  RefPtr<nsIOService> mIOService;
  Maybe<OriginAttributes> mOriginAttributes;
};

NS_IMPL_ISUPPORTS(IOServiceProxyCallback, nsIProtocolProxyCallback)

NS_IMETHODIMP
IOServiceProxyCallback::OnProxyAvailable(nsICancelable* request,
                                         nsIChannel* channel, nsIProxyInfo* pi,
                                         nsresult status) {
  // Checking proxy status for speculative connect
  nsAutoCString type;
  if (NS_SUCCEEDED(status) && pi && NS_SUCCEEDED(pi->GetType(type)) &&
      !type.EqualsLiteral("direct")) {
    // proxies dont do speculative connect
    return NS_OK;
  }

  nsCOMPtr<nsIURI> uri;
  nsresult rv = channel->GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv)) {
    return NS_OK;
  }

  nsAutoCString scheme;
  rv = uri->GetScheme(scheme);
  if (NS_FAILED(rv)) return NS_OK;

  nsCOMPtr<nsIProtocolHandler> handler;
  rv = mIOService->GetProtocolHandler(scheme.get(), getter_AddRefs(handler));
  if (NS_FAILED(rv)) return NS_OK;

  nsCOMPtr<nsISpeculativeConnect> speculativeHandler =
      do_QueryInterface(handler);
  if (!speculativeHandler) return NS_OK;

  nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
  nsCOMPtr<nsIPrincipal> principal = loadInfo->GetLoadingPrincipal();

  nsLoadFlags loadFlags = 0;
  channel->GetLoadFlags(&loadFlags);
  bool anonymous = !!(loadFlags & nsIRequest::LOAD_ANONYMOUS);
  if (mOriginAttributes) {
    speculativeHandler->SpeculativeConnectWithOriginAttributesNative(
        uri, std::move(mOriginAttributes.ref()), mCallbacks, anonymous);
  } else {
    speculativeHandler->SpeculativeConnect(uri, principal, mCallbacks,
                                           anonymous);
  }

  return NS_OK;
}

nsresult nsIOService::SpeculativeConnectInternal(
    nsIURI* aURI, nsIPrincipal* aPrincipal,
    Maybe<OriginAttributes>&& aOriginAttributes,
    nsIInterfaceRequestor* aCallbacks, bool aAnonymous) {
  NS_ENSURE_ARG(aURI);

  if (!SchemeIsHttpOrHttps(aURI)) {
    // We don't speculatively connect to non-HTTP[S] URIs.
    return NS_OK;
  }

  if (IsNeckoChild()) {
    nsCOMPtr<nsILoadContext> loadContext = do_GetInterface(aCallbacks);

    gNeckoChild->SendSpeculativeConnect(
        nullptr, IPC::SerializedLoadContext(loadContext), aURI, aPrincipal,
        std::move(aOriginAttributes), aAnonymous);
    return NS_OK;
  }

  // Check for proxy information. If there is a proxy configured then a
  // speculative connect should not be performed because the potential
  // reward is slim with tcp peers closely located to the browser.
  nsresult rv;
  nsCOMPtr<nsIProtocolProxyService> pps;
  pps = mozilla::components::ProtocolProxy::Service(&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrincipal> loadingPrincipal = aPrincipal;

  MOZ_ASSERT(aPrincipal || aOriginAttributes,
             "We expect passing a principal or OriginAttributes here.");

  if (!aPrincipal && !aOriginAttributes) {
    return NS_ERROR_INVALID_ARG;
  }

  if (aOriginAttributes) {
    loadingPrincipal =
        BasePrincipal::CreateContentPrincipal(aURI, aOriginAttributes.ref());
  }

  // XXX Bug 1724080: Avoid TCP connections on port 80 when https-only
  // or https-first is enabled. Let's create a dummy loadinfo which we
  // only use to determine whether we need ot upgrade the speculative
  // connection from http to https.
  nsCOMPtr<nsIURI> httpsURI;
  if (aURI->SchemeIs("http")) {
    nsCOMPtr<nsILoadInfo> httpsOnlyCheckLoadInfo = MOZ_TRY(
        LoadInfo::Create(loadingPrincipal, loadingPrincipal, nullptr,
                         nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK,
                         nsIContentPolicy::TYPE_SPECULATIVE));

    // Check if https-only, or https-first would upgrade the request
    if (nsHTTPSOnlyUtils::ShouldUpgradeRequest(aURI, httpsOnlyCheckLoadInfo) ||
        nsHTTPSOnlyUtils::ShouldUpgradeHttpsFirstRequest(
            aURI, httpsOnlyCheckLoadInfo)) {
      rv = NS_GetSecureUpgradedURI(aURI, getter_AddRefs(httpsURI));
      NS_ENSURE_SUCCESS(rv, rv);
      aURI = httpsURI.get();
    }
  }

  // dummy channel used to create a TCP connection.
  // we perform security checks on the *real* channel, responsible
  // for any network loads. this real channel just checks the TCP
  // pool if there is an available connection created by the
  // channel we create underneath - hence it's safe to use
  // the systemPrincipal as the loadingPrincipal for this channel.
  nsCOMPtr<nsIChannel> channel;
  rv = NewChannelFromURI(
      aURI,
      nullptr,  // aLoadingNode,
      loadingPrincipal,
      nullptr,  // aTriggeringPrincipal,
      nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      nsIContentPolicy::TYPE_SPECULATIVE, getter_AddRefs(channel));
  NS_ENSURE_SUCCESS(rv, rv);

  if (aAnonymous) {
    nsLoadFlags loadFlags = 0;
    channel->GetLoadFlags(&loadFlags);
    loadFlags |= nsIRequest::LOAD_ANONYMOUS;
    channel->SetLoadFlags(loadFlags);
  }

  if (!aCallbacks) {
    // Proxy filters are registered, but no callbacks were provided.
    // When proxyDNS is true, this speculative connection would likely leak a
    // DNS lookup, so we should return early to avoid that.
    bool hasProxyFilterRegistered = false;
    (void)pps->GetHasProxyFilterRegistered(&hasProxyFilterRegistered);
    if (hasProxyFilterRegistered) {
      return NS_ERROR_FAILURE;
    }
  } else {
    rv = channel->SetNotificationCallbacks(aCallbacks);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<nsICancelable> cancelable;
  RefPtr<IOServiceProxyCallback> callback = new IOServiceProxyCallback(
      aCallbacks, this, std::move(aOriginAttributes));
  nsCOMPtr<nsIProtocolProxyService2> pps2 = do_QueryInterface(pps);
  if (pps2) {
    return pps2->AsyncResolve2(channel, 0, callback, nullptr,
                               getter_AddRefs(cancelable));
  }
  return pps->AsyncResolve(channel, 0, callback, nullptr,
                           getter_AddRefs(cancelable));
}

NS_IMETHODIMP
nsIOService::SpeculativeConnect(nsIURI* aURI, nsIPrincipal* aPrincipal,
                                nsIInterfaceRequestor* aCallbacks,
                                bool aAnonymous) {
  return SpeculativeConnectInternal(aURI, aPrincipal, Nothing(), aCallbacks,
                                    aAnonymous);
}

NS_IMETHODIMP nsIOService::SpeculativeConnectWithOriginAttributes(
    nsIURI* aURI, JS::Handle<JS::Value> aOriginAttributes,
    nsIInterfaceRequestor* aCallbacks, bool aAnonymous, JSContext* aCx) {
  OriginAttributes attrs;
  if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  SpeculativeConnectWithOriginAttributesNative(aURI, std::move(attrs),
                                               aCallbacks, aAnonymous);
  return NS_OK;
}

NS_IMETHODIMP_(void)
nsIOService::SpeculativeConnectWithOriginAttributesNative(
    nsIURI* aURI, OriginAttributes&& aOriginAttributes,
    nsIInterfaceRequestor* aCallbacks, bool aAnonymous) {
  Maybe<OriginAttributes> originAttributes;
  originAttributes.emplace(aOriginAttributes);
  (void)SpeculativeConnectInternal(aURI, nullptr, std::move(originAttributes),
                                   aCallbacks, aAnonymous);
}

NS_IMETHODIMP
nsIOService::NotImplemented() { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
nsIOService::RegisterProtocolHandler(const nsACString& aScheme,
                                     nsIProtocolHandler* aHandler,
                                     uint32_t aProtocolFlags,
                                     int32_t aDefaultPort) {
  if (mShutdown) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (aScheme.IsEmpty()) {
    return NS_ERROR_INVALID_ARG;
  }

  nsAutoCString scheme(aScheme);
  ToLowerCase(scheme);

  AutoWriteLock lock(mLock);
  return mRuntimeProtocolHandlers.WithEntryHandle(scheme, [&](auto&& entry) {
    if (entry) {
      NS_WARNING("Cannot override an existing dynamic protocol handler");
      return NS_ERROR_FACTORY_EXISTS;
    }
    if (xpcom::StaticProtocolHandler::Lookup(scheme)) {
      NS_WARNING("Cannot override an existing static protocol handler");
      return NS_ERROR_FACTORY_EXISTS;
    }
    nsMainThreadPtrHandle<nsIProtocolHandler> handler(
        new nsMainThreadPtrHolder<nsIProtocolHandler>("RuntimeProtocolHandler",
                                                      aHandler));
    entry.Insert(RuntimeProtocolHandler{
        .mHandler = std::move(handler),
        .mProtocolFlags = aProtocolFlags,
        .mDefaultPort = aDefaultPort,
    });
    return NS_OK;
  });
}

NS_IMETHODIMP
nsIOService::UnregisterProtocolHandler(const nsACString& aScheme) {
  if (mShutdown) {
    return NS_OK;
  }
  if (aScheme.IsEmpty()) {
    return NS_ERROR_INVALID_ARG;
  }

  nsAutoCString scheme(aScheme);
  ToLowerCase(scheme);

  AutoWriteLock lock(mLock);
  return mRuntimeProtocolHandlers.Remove(scheme)
             ? NS_OK
             : NS_ERROR_FACTORY_NOT_REGISTERED;
}

NS_IMETHODIMP
nsIOService::SetSimpleURIUnknownRemoteSchemes(
    const nsTArray<nsCString>& aRemoteSchemes) {
  LOG(("nsIOService::SetSimpleUriUnknownRemoteSchemes"));
  mSimpleURIUnknownSchemes.SetAndMergeRemoteSchemes(aRemoteSchemes);

  if (XRE_IsParentProcess()) {
    // since we only expect parent and content processes to create URLs
    // that need to check the bypass list
    // we only broadcast the list to content processes
    //
    // sending only the remote-settings schemes to the content,
    // which already has the pref list
    for (auto* cp : mozilla::dom::ContentParent::AllProcesses(
             mozilla::dom::ContentParent::eLive)) {
      (void)cp->SendSimpleURIUnknownRemoteSchemes(aRemoteSchemes);
    }
  }
  return NS_OK;
}

// Check for any address space overrides for Local Network Access Checks
// The override prefs should be set only for tests (controlled by
// network_lna_blocking pref).
NS_IMETHODIMP
nsIOService::GetOverridenIpAddressSpace(
    nsILoadInfo::IPAddressSpace* aIpAddressSpace, const NetAddr& aAddr) {
  nsAutoCString addrPortString;

  if (!StaticPrefs::network_lna_enabled()) {
    return NS_ERROR_FAILURE;
  }

  {
    AutoReadLock lock(mLock);
    if (mPublicAddressSpaceOverridesList.IsEmpty() &&
        mPrivateAddressSpaceOverridesList.IsEmpty() &&
        mLocalAddressSpaceOverrideList.IsEmpty()) {
      return NS_ERROR_FAILURE;
    }
  }

  aAddr.ToAddrPortString(addrPortString);
  addrPortString.StripWhitespace();
  AutoReadLock lock(mLock);

  for (const auto& ipAddr : mPublicAddressSpaceOverridesList) {
    if (addrPortString.Equals(ipAddr)) {
      *aIpAddressSpace = nsILoadInfo::IPAddressSpace::Public;
      return NS_OK;
    }
  }

  for (const auto& ipAddr : mPrivateAddressSpaceOverridesList) {
    if (addrPortString.Equals(ipAddr)) {
      *aIpAddressSpace = nsILoadInfo::IPAddressSpace::Private;
      return NS_OK;
    }
  }

  for (const auto& ipAddr : mLocalAddressSpaceOverrideList) {
    if (addrPortString.Equals(ipAddr)) {
      *aIpAddressSpace = nsILoadInfo::IPAddressSpace::Local;
      return NS_OK;
    }
  }

  *aIpAddressSpace = nsILoadInfo::IPAddressSpace::Unknown;
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsIOService::IsSimpleURIUnknownScheme(const nsACString& aScheme,
                                      bool* _retval) {
  *_retval = mSimpleURIUnknownSchemes.IsSimpleURIUnknownScheme(aScheme);
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::GetSimpleURIUnknownRemoteSchemes(nsTArray<nsCString>& _retval) {
  mSimpleURIUnknownSchemes.GetRemoteSchemes(_retval);
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::AddEssentialDomainMapping(const nsACString& aFrom,
                                       const nsACString& aTo) {
  MOZ_ASSERT(NS_IsMainThread());
  mEssentialDomainMapping.InsertOrUpdate(aFrom, aTo);
  return NS_OK;
}

NS_IMETHODIMP
nsIOService::ClearEssentialDomainMapping() {
  MOZ_ASSERT(NS_IsMainThread());
  mEssentialDomainMapping.Clear();
  return NS_OK;
}

bool nsIOService::GetFallbackDomain(const nsACString& aDomain,
                                    nsACString& aFallbackDomain) {
  MOZ_ASSERT(NS_IsMainThread());
  if (auto entry = mEssentialDomainMapping.Lookup(aDomain)) {
    aFallbackDomain = entry.Data();
    return true;
  }
  return false;
}

NS_IMETHODIMP
nsIOService::ParseCacheControlHeader(const nsACString& aCacheControlHeader,
                                     JSContext* cx,
                                     JS::MutableHandle<JS::Value> _retval) {
  MOZ_ASSERT(NS_IsMainThread());

  mozilla::dom::HTTPCacheControlParseResult result;
  CacheControlParser parser(aCacheControlHeader);

  bool didParseValue = false;

  uint32_t maxAge = 0;
  didParseValue = parser.MaxAge(&maxAge);
  if (didParseValue) {
    result.mMaxAge = maxAge;
  }

  uint32_t maxStale = 0;
  didParseValue = parser.MaxStale(&maxStale);
  if (didParseValue) {
    result.mMaxStale = maxStale;
  }

  uint32_t minFresh = 0;
  didParseValue = parser.MaxStale(&minFresh);
  if (didParseValue) {
    result.mMinFresh = minFresh;
  }

  uint32_t staleWhileRevalidate = 0;
  didParseValue = parser.StaleWhileRevalidate(&staleWhileRevalidate);
  if (didParseValue) {
    result.mStaleWhileRevalidate = staleWhileRevalidate;
  }

  result.mNoCache = parser.NoCache();
  result.mNoStore = parser.NoStore();
  result.mPublic = parser.Public();
  result.mPrivate = parser.Private();
  result.mImmutable = parser.Immutable();

  if (!ToJSValue(cx, result, _retval)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla
