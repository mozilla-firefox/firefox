/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TRRServiceBase.h"

#include "AlternateServices.h"
#include "ProxyConfigLookup.h"
#include "TRRService.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_network.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsHostResolver.h"
#include "nsHttpConnectionInfo.h"
#include "nsHttpHandler.h"
#include "nsIDNSService.h"
#include "nsIOService.h"
#include "nsIProxyInfo.h"
#include "nsNetUtil.h"
// Put DNSLogging.h at the end to avoid LOG being overwritten by other headers.
#include "DNSLogging.h"

#if defined(XP_WIN)
#  include <shlobj.h>  // for SHGetSpecialFolderPathA
#endif                 // XP_WIN

namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS(TRRServiceBase, nsIProxyConfigChangedCallback)

TRRServiceBase::TRRServiceBase()
    : mDefaultTRRConnectionInfo("DataMutex::mDefaultTRRConnectionInfo") {}

TRRServiceBase::~TRRServiceBase() {
  if (mTRRConnectionInfoInited) {
    UnregisterProxyChangeListener();
  }
}

void TRRServiceBase::ProcessURITemplate(nsACString& aURI) {
  // URI Template, RFC 6570.
  if (aURI.IsEmpty()) {
    return;
  }
  nsAutoCString scheme;
  nsCOMPtr<nsIIOService> ios(do_GetIOService());
  if (ios) {
    ios->ExtractScheme(aURI, scheme);
  }
  if (!scheme.Equals("https")) {
    LOG(("TRRService TRR URI %s is not https. Not used.\n",
         PromiseFlatCString(aURI).get()));
    aURI.Truncate();
    return;
  }

  // cut off everything from "{" to "}" sequences (potentially multiple),
  // as a crude conversion from template into URI.
  nsAutoCString uri(aURI);

  do {
    nsCCharSeparatedTokenizer openBrace(uri, '{');
    if (openBrace.hasMoreTokens()) {
      // the 'nextToken' is the left side of the open brace (or full uri)
      nsAutoCString prefix(openBrace.nextToken());

      // if there is an open brace, there's another token
      const nsACString& endBrace = openBrace.nextToken();
      nsCCharSeparatedTokenizer closeBrace(endBrace, '}');
      if (closeBrace.hasMoreTokens()) {
        // there is a close brace as well, make a URI out of the prefix
        // and the suffix
        closeBrace.nextToken();
        nsAutoCString suffix(closeBrace.nextToken());
        uri = prefix + suffix;
      } else {
        // no (more) close brace
        break;
      }
    } else {
      // no (more) open brace
      break;
    }
  } while (true);

  aURI = std::move(uri);
}

void TRRServiceBase::CheckURIPrefs() {
  mURISetByDetection = false;

  if (StaticPrefs::network_trr_use_ohttp() && !mOHTTPURIPref.IsEmpty()) {
    MaybeSetPrivateURI(mOHTTPURIPref);
    return;
  }

  // The user has set a custom URI so it takes precedence.
  if (!mURIPref.IsEmpty()) {
    MaybeSetPrivateURI(mURIPref);
    return;
  }

  // Check if the rollout addon has set a pref.
  if (!mRolloutURIPref.IsEmpty()) {
    MaybeSetPrivateURI(mRolloutURIPref);
    return;
  }

  // Otherwise just use the default value.
  MaybeSetPrivateURI(mDefaultURIPref);
}

// static
nsIDNSService::ResolverMode ModeFromPrefs(
    nsIDNSService::ResolverMode& aTRRModePrefValue) {
  // 0 - off, 1 - reserved, 2 - TRR first, 3 - TRR only, 4 - reserved,
  // 5 - explicit off

  auto processPrefValue = [](uint32_t value) -> nsIDNSService::ResolverMode {
    if (value == nsIDNSService::MODE_RESERVED1 ||
        value == nsIDNSService::MODE_RESERVED4 ||
        value > nsIDNSService::MODE_TRROFF) {
      return nsIDNSService::MODE_TRROFF;
    }
    return static_cast<nsIDNSService::ResolverMode>(value);
  };

  uint32_t tmp;
  if (NS_FAILED(Preferences::GetUint("network.trr.mode", &tmp))) {
    tmp = 0;
  }
  nsIDNSService::ResolverMode modeFromPref = processPrefValue(tmp);
  aTRRModePrefValue = modeFromPref;

  if (modeFromPref != nsIDNSService::MODE_NATIVEONLY) {
    return modeFromPref;
  }

  if (NS_FAILED(Preferences::GetUint(kRolloutModePref, &tmp))) {
    tmp = 0;
  }
  modeFromPref = processPrefValue(tmp);

  return modeFromPref;
}

void TRRServiceBase::OnTRRModeChange() {
  uint32_t oldMode = mMode;
  // This is the value of the pref "network.trr.mode"
  nsIDNSService::ResolverMode trrModePrefValue;
  mMode = ModeFromPrefs(trrModePrefValue);
  if (mMode != oldMode) {
    LOG(("TRR Mode changed from %d to %d", oldMode, int(mMode)));
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
      obs->NotifyObservers(nullptr, NS_NETWORK_TRR_MODE_CHANGED_TOPIC, nullptr);
    }

    TRRService::SetCurrentTRRMode(trrModePrefValue);
  }

  static bool readHosts = false;
  // When native HTTPS query is enabled, we need to read etc/hosts.
  if ((mMode == nsIDNSService::MODE_TRRFIRST ||
       mMode == nsIDNSService::MODE_TRRONLY || mNativeHTTPSQueryEnabled) &&
      !readHosts) {
    readHosts = true;
    ReadEtcHostsFile();
  }
}

void TRRServiceBase::OnTRRURIChange() {
  Preferences::GetCString("network.trr.uri", mURIPref);
  Preferences::GetCString(kRolloutURIPref, mRolloutURIPref);
  Preferences::GetCString("network.trr.default_provider_uri", mDefaultURIPref);
  Preferences::GetCString("network.trr.ohttp.uri", mOHTTPURIPref);

  CheckURIPrefs();
}

already_AddRefed<nsHttpConnectionInfo> TRRServiceBase::CreateConnInfoHelper(
    nsIURI* aURI, nsIProxyInfo* aProxyInfo) {
  MOZ_ASSERT(NS_IsMainThread());

  nsAutoCString host;
  nsAutoCString scheme;
  nsAutoCString username;
  int32_t port = -1;
  bool isHttps = aURI->SchemeIs("https");

  nsresult rv = aURI->GetScheme(scheme);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  rv = aURI->GetAsciiHost(host);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  rv = aURI->GetPort(&port);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  // Just a warning here because some nsIURIs do not implement this method.
  if (NS_WARN_IF(NS_FAILED(aURI->GetUsername(username)))) {
    LOG(("Failed to get username for aURI(%s)",
         aURI->GetSpecOrDefault().get()));
  }

  gHttpHandler->MaybeAddAltSvcForTesting(aURI, username, false, nullptr,
                                         OriginAttributes());

  nsCOMPtr<nsProxyInfo> proxyInfo = do_QueryInterface(aProxyInfo);
  RefPtr<nsHttpConnectionInfo> connInfo = new nsHttpConnectionInfo(
      host, port, ""_ns, username, proxyInfo, OriginAttributes(), isHttps);
  bool http2Allowed = !gHttpHandler->IsHttp2Excluded(connInfo);
  bool http3Allowed = proxyInfo ? proxyInfo->IsDirect() : true;

  RefPtr<AltSvcMapping> mapping;
  if ((http2Allowed || http3Allowed) &&
      AltSvcMapping::AcceptableProxy(proxyInfo) &&
      (scheme.EqualsLiteral("http") || scheme.EqualsLiteral("https")) &&
      (mapping = gHttpHandler->GetAltServiceMapping(
           scheme, host, port, false, OriginAttributes(), http2Allowed,
           http3Allowed,
           StaticPrefs::network_trr_force_http3_first() ||
               (StaticPrefs::network_trr_allow_default_http3_first() &&
                GetHttp3FirstForServer(host))))) {
    mapping->GetConnectionInfo(getter_AddRefs(connInfo), proxyInfo,
                               OriginAttributes());
  }

  return connInfo->Mutate()
      .SetAnonymous(true)
      .SetIsTrrServiceChannel(true)
      .SetTRRMode(nsIRequest::TRR_DISABLED_MODE)
      .SetHappyEyeballsEnabled(
          StaticPrefs::network_http_happy_eyeballs_enabled() && !proxyInfo)
      .Finalize();
}

void TRRServiceBase::InitTRRConnectionInfo(bool aForceReinit) {
  if (mTRRConnectionInfoInited && !aForceReinit) {
    return;
  }

  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "TRRServiceBase::InitTRRConnectionInfo",
        [self = RefPtr{this}]() { self->InitTRRConnectionInfo(); }));
    return;
  }

  LOG(("TRRServiceBase::InitTRRConnectionInfo"));
  nsAutoCString uri;
  GetURI(uri);
  AsyncCreateTRRConnectionInfoInternal(uri);
}

void TRRServiceBase::AsyncCreateTRRConnectionInfo(const nsACString& aURI) {
  LOG(
      ("TRRServiceBase::AsyncCreateTRRConnectionInfo "
       "mTRRConnectionInfoInited=%d",
       bool(mTRRConnectionInfoInited)));
  if (!mTRRConnectionInfoInited) {
    return;
  }

  AsyncCreateTRRConnectionInfoInternal(aURI);
}

void TRRServiceBase::AsyncCreateTRRConnectionInfoInternal(
    const nsACString& aURI) {
  SetDefaultTRRConnectionInfo(nullptr);

  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIURI> dnsURI;
  nsresult rv = NS_NewURI(getter_AddRefs(dnsURI), aURI);
  if (NS_FAILED(rv)) {
    return;
  }

  // Tag this lookup with a generation. Consecutive proxy config changes start
  // overlapping ProxyConfigLookups that may complete out of order; only the
  // most recent one is allowed to store its result, so a stale lookup (e.g.
  // resolved before a PAC took effect) can't overwrite a newer one.
  uint32_t generation = ++mTRRConnectionInfoGeneration;

  rv = ProxyConfigLookup::Create(
      [self = RefPtr{this}, uri(dnsURI), generation](nsIProxyInfo* aProxyInfo,
                                                     nsresult aStatus) mutable {
        if (generation != self->mTRRConnectionInfoGeneration) {
          // A newer lookup has been started since; ignore this stale result.
          return;
        }

        if (NS_FAILED(aStatus)) {
          self->SetDefaultTRRConnectionInfo(nullptr);
          return;
        }

        RefPtr<nsHttpConnectionInfo> connInfo =
            self->CreateConnInfoHelper(uri, aProxyInfo);
        self->SetDefaultTRRConnectionInfo(connInfo);
        if (!self->mTRRConnectionInfoInited) {
          self->mTRRConnectionInfoInited = true;
          self->RegisterProxyChangeListener();
        }
      },
      dnsURI, 0, /* aIsTRRServiceChannel */ true, nullptr);

  // mDefaultTRRConnectionInfo is set to nullptr at the beginning of this
  // method, so we don't really care aobut the |rv| here. If it's failed,
  // mDefaultTRRConnectionInfo stays as nullptr and we'll create a new
  // connection info in TRRServiceChannel again.
  (void)NS_WARN_IF(NS_FAILED(rv));
}

already_AddRefed<nsHttpConnectionInfo> TRRServiceBase::TRRConnectionInfo() {
  RefPtr<nsHttpConnectionInfo> connInfo;
  {
    auto lock = mDefaultTRRConnectionInfo.Lock();
    connInfo = *lock;
  }
  return connInfo.forget();
}

void TRRServiceBase::SetDefaultTRRConnectionInfo(
    nsHttpConnectionInfo* aConnInfo) {
  LOG(("TRRService::SetDefaultTRRConnectionInfo aConnInfo=%s",
       aConnInfo ? aConnInfo->HashKey().get() : "none"));
  {
    auto lock = mDefaultTRRConnectionInfo.Lock();
    lock.ref() = aConnInfo;
  }
}

void TRRServiceBase::RegisterProxyChangeListener() {
  nsCOMPtr<nsIProtocolProxyService> pps =
      do_GetService(NS_PROTOCOLPROXYSERVICE_CONTRACTID);
  if (!pps) {
    return;
  }

  pps->AddProxyConfigCallback(this);
}

void TRRServiceBase::UnregisterProxyChangeListener() {
  nsCOMPtr<nsIProtocolProxyService> pps =
      do_GetService(NS_PROTOCOLPROXYSERVICE_CONTRACTID);
  if (!pps) {
    return;
  }

  pps->RemoveProxyConfigCallback(this);
}

void TRRServiceBase::SetHttp3FirstForServer(const nsACString& aServer,
                                            bool aEnabled) {
  MutexAutoLock lock(mLock);
  LOG(("SetHttp3FirstForServer %s %d", PromiseFlatCString(aServer).get(),
       aEnabled));
  mHttp3FirstServers.InsertOrUpdate(aServer, aEnabled);
}

bool TRRServiceBase::GetHttp3FirstForServer(const nsACString& aServer) {
  MutexAutoLock lock(mLock);
  bool res = mHttp3FirstServers.MaybeGet(aServer).valueOr(false);
  LOG(("GetHttp3FirstForServer %s %d", PromiseFlatCString(aServer).get(), res));
  return res;
}

}  // namespace net
}  // namespace mozilla
