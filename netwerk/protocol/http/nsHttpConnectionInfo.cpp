/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

// Log on level :5, instead of default :4.
#undef LOG
#define LOG(args) LOG5(args)
#undef LOG_ENABLED
#define LOG_ENABLED() LOG5_ENABLED()

#include "mozilla/HashFunctions.h"
#include "mozilla/net/DNS.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "nsComponentManagerUtils.h"
#include "nsHttpConnectionInfo.h"
#include "nsHttpHandler.h"
#include "nsICryptoHash.h"
#include "nsIDNSByTypeRecord.h"
#include "nsIProtocolProxyService.h"
#include "nsNetCID.h"
#include "nsProxyInfo.h"
#include "prnetdb.h"

static nsresult SHA256(const char* aPlainText, nsAutoCString& aResult) {
  nsresult rv;
  nsCOMPtr<nsICryptoHash> hasher =
      do_CreateInstance(NS_CRYPTO_HASH_CONTRACTID, &rv);
  if (NS_FAILED(rv)) {
    LOG(("nsHttpDigestAuth: no crypto hash!\n"));
    return rv;
  }
  rv = hasher->Init(nsICryptoHash::SHA256);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = hasher->Update((unsigned char*)aPlainText, strlen(aPlainText));
  NS_ENSURE_SUCCESS(rv, rv);
  return hasher->Finish(false, aResult);
}

namespace mozilla {
namespace net {

nsHttpConnectionInfo::nsHttpConnectionInfo(
    const nsACString& originHost, int32_t originPort,
    const nsACString& npnToken, const nsACString& username,
    nsProxyInfo* proxyInfo, const OriginAttributes& originAttributes,
    bool endToEndSSL, bool aIsHttp3, bool aWebTransport)
    : mRoutedPort(443), mLessThanTls13(false) {
  Init(originHost, originPort, npnToken, username, proxyInfo, originAttributes,
       endToEndSSL, aIsHttp3, aWebTransport);
}

nsHttpConnectionInfo::nsHttpConnectionInfo(
    const nsACString& originHost, int32_t originPort,
    const nsACString& npnToken, const nsACString& username,
    nsProxyInfo* proxyInfo, const OriginAttributes& originAttributes,
    const nsACString& routedHost, int32_t routedPort, bool aIsHttp3,
    bool aWebTransport)
    : mLessThanTls13(false) {
  mEndToEndSSL = true;  // so DefaultPort() works
  mRoutedPort = routedPort == -1 ? DefaultPort() : routedPort;

  if (!originHost.Equals(routedHost) || (originPort != routedPort) ||
      aIsHttp3) {
    mRoutedHost = routedHost;
  }
  Init(originHost, originPort, npnToken, username, proxyInfo, originAttributes,
       true, aIsHttp3, aWebTransport);
}

// static
uint64_t nsHttpConnectionInfo::GenerateNewWebTransportId() {
  // Used for generating unique IDSs for dedicated connections, currently used
  // by WebTransport
  MOZ_ASSERT(XRE_IsParentProcess());
  static Atomic<uint64_t> id(0);
  return ++id;
}

void nsHttpConnectionInfo::Init(const nsACString& host, int32_t port,
                                const nsACString& npnToken,
                                const nsACString& username,
                                nsProxyInfo* proxyInfo,
                                const OriginAttributes& originAttributes,
                                bool e2eSSL, bool aIsHttp3,
                                bool aWebTransport) {
  LOG(("Init nsHttpConnectionInfo @%p\n", this));

  mUsername = username;
  mProxyInfo = proxyInfo;
  mEndToEndSSL = e2eSSL;
  mUsingConnect = false;
  mNPNToken = npnToken;
  mIsHttp3 = aIsHttp3;
  mWebTransport = aWebTransport;
  mOriginAttributes = originAttributes;
  mTlsFlags = 0x0;
  mIsTrrServiceChannel = false;
  mTRRMode = nsIRequest::TRR_DEFAULT_MODE;
  mIPv4Disabled = false;
  mIPv6Disabled = false;
  mHttp3Policy = Http3Policy::Allowed;
  mHasIPHintAddress = false;
  mIsWildCard = host.Equals("*"_ns);

  mUsingHttpsProxy = (proxyInfo && proxyInfo->IsHTTPS());
  mUsingHttpProxy = mUsingHttpsProxy || (proxyInfo && proxyInfo->IsHTTP());

  if (mUsingHttpProxy) {
    mUsingConnect = mEndToEndSSL || proxyInfo->IsHttp3Proxy();
    if (!mUsingConnect) {
      uint32_t resolveFlags = 0;
      if (NS_SUCCEEDED(mProxyInfo->GetResolveFlags(&resolveFlags)) &&
          resolveFlags & nsIProtocolProxyService::RESOLVE_ALWAYS_TUNNEL) {
        mUsingConnect = true;
      }
    }
  }

  if (mUsingHttpsProxy) {
    mIsHttp3ProxyConnection = "masque"_ns.Equals(proxyInfo->Type());
    if (mIsHttp3ProxyConnection) {
      mProxyNPNToken = "h3"_ns;
    }
  }

  SetOriginServer(host, port);
}

void nsHttpConnectionInfo::BuildHashKey() {
  //
  // build hash key:
  //
  // the hash key uniquely identifies the connection type.  two connections
  // are "equal" if they end up talking the same protocol to the same server
  // and are both used for anonymous or non-anonymous connection only;
  // anonymity of the connection is setup later from nsHttpChannel::AsyncOpen
  // where we know we use anonymous connection (LOAD_ANONYMOUS load flag)
  //

  const char* keyHost;
  int32_t keyPort;

  if (mUsingHttpProxy && !mUsingConnect) {
    keyHost = ProxyHost();
    keyPort = ProxyPort();
  } else {
    keyHost = Origin();
    keyPort = OriginPort();
  }

  // The hashkey has 9 fields followed by host connection info
  // byte 0 is P/T/. {P,T} for Plaintext/TLS Proxy over HTTP
  // byte 1 is S/. S is for end to end ssl such as https:// uris
  // byte 2 is A/. A is for an anonymous channel (no cookies, etc..)
  // byte 3 is P/. P is for a private browising channel
  // byte 4 is I/. I is for insecure scheme on TLS for http:// uris
  // byte 5 is X/. X is for disallow_spdy flag
  // byte 6 is C/. C is for be Conservative
  // byte 7 is B/. B is for allowing client certs on an anonymous channel
  // byte 8 is F/. F is for indicating a fallback connection
  // byte 9 is W/. W is for indicating a webTransport
  // byte 10 is H/. H is for indicating HappyEyeballs is used
  // Note: when adding/removing fields from this list which do not have
  // corresponding data fields on the object itself, you may also need to
  // modify RebuildHashKey.

  static_assert(static_cast<uint32_t>(HashKeyIndex::End) == 11,
                "Update dot string in BuildHashKey if HashKeyIndex changes");
  mHashKey.AssignLiteral("...........[tlsflags0x00000000]");

  mHashKey.Append(keyHost);
  mHashKey.Append(':');
  mHashKey.AppendInt(keyPort);
  if (!mUsername.IsEmpty()) {
    mHashKey.Append('[');
    mHashKey.Append(mUsername);
    mHashKey.Append(']');
  }

  if (mUsingHttpsProxy) {
    SetHashCharAt('T', HashKeyIndex::Proxy);
  } else if (mUsingHttpProxy) {
    SetHashCharAt('P', HashKeyIndex::Proxy);
  }
  if (mEndToEndSSL) {
    SetHashCharAt('S', HashKeyIndex::EndToEndSSL);
  }

  if (mWebTransport) {
    SetHashCharAt('W', HashKeyIndex::WebTransport);
  }

  // NOTE: for transparent proxies (e.g., SOCKS) we need to encode the proxy
  // info in the hash key (this ensures that we will continue to speak the
  // right protocol even if our proxy preferences change).
  //
  // NOTE: for SSL tunnels add the proxy information to the cache key.
  // We cannot use the proxy as the host parameter (as we do for non SSL)
  // because this is a single host tunnel, but we need to include the proxy
  // information so that a change in proxy config will mean this connection
  // is not reused

  // NOTE: Adding the username and the password provides a means to isolate
  // keep-alive to the URL bar domain as well: If the username is the URL bar
  // domain, keep-alive connections are not reused by resources bound to
  // different URL bar domains as the respective hash keys are not matching.

  if ((!mUsingHttpProxy && ProxyHost()) || (mUsingHttpProxy && mUsingConnect)) {
    mHashKey.AppendLiteral(" (");
    mHashKey.Append(ProxyType());
    mHashKey.Append(':');
    mHashKey.Append(ProxyHost());
    mHashKey.Append(':');
    mHashKey.AppendInt(ProxyPort());
    mHashKey.Append(')');
    mHashKey.Append('[');
    mHashKey.Append(ProxyUsername());
    mHashKey.Append(':');
    const char* password = ProxyPassword();
    if (strlen(password) > 0) {
      nsAutoCString digestedPassword;
      nsresult rv = SHA256(password, digestedPassword);
      if (rv == NS_OK) {
        mHashKey.Append(digestedPassword);
      }
    }
    mHashKey.Append(']');
  }

  // When HappyEyeballs is enabled, don't encode routedHost and NPN-TOKEN.
  if (!mHappyEyeballsEnabled) {
    if (!mRoutedHost.IsEmpty()) {
      mHashKey.AppendLiteral(" <ROUTE-via ");
      mHashKey.Append(mRoutedHost);
      mHashKey.Append(':');
      mHashKey.AppendInt(mRoutedPort);
      mHashKey.Append('>');
    }

    if (!mNPNToken.IsEmpty()) {
      mHashKey.AppendLiteral(" {NPN-TOKEN ");
      mHashKey.Append(mNPNToken);
      mHashKey.AppendLiteral("}");
    }
  }

  if (GetTRRMode() != nsIRequest::TRR_DEFAULT_MODE) {
    // When connecting with another TRR mode, we enforce a separate connection
    // hashkey so that we also can trigger a fresh DNS resolver that then
    // doesn't use TRR as the previous connection might have.
    mHashKey.AppendLiteral("[TRR:");
    mHashKey.AppendInt(GetTRRMode());
    mHashKey.AppendLiteral("]");
  }

  if (GetIPv4Disabled()) {
    mHashKey.AppendLiteral("[!v4]");
  }

  if (GetIPv6Disabled()) {
    mHashKey.AppendLiteral("[!v6]");
  }

  if (GetHttp3Disabled()) {
    mHashKey.AppendLiteral("[!h3]");
  } else if (GetHttp3Only()) {
    mHashKey.AppendLiteral("[h3only]");
  }

  if (mProxyInfo) {
    const nsCString& connectionIsolationKey =
        mProxyInfo->ConnectionIsolationKey();
    if (!connectionIsolationKey.IsEmpty()) {
      mHashKey.AppendLiteral("{CIK ");
      mHashKey.Append(connectionIsolationKey);
      mHashKey.AppendLiteral("}");
    }
    if (mProxyInfo->Flags() & nsIProxyInfo::TRANSPARENT_PROXY_RESOLVES_HOST) {
      mHashKey.AppendLiteral("{TPRH}");
    }
  }

  if (mWebTransportId) {
    mHashKey.AppendLiteral("{wId");
    mHashKey.AppendInt(mWebTransportId, 16);
    mHashKey.AppendLiteral("}");
  }

  nsAutoCString originAttributes;
  mOriginAttributes.CreateSuffix(originAttributes);
  mHashKey.Append(originAttributes);
}

void nsHttpConnectionInfo::RebuildHashKey() {
  // Create copies of all properties stored in our hash key.
  bool isAnonymous = GetAnonymous();
  bool isPrivate = GetPrivate();
  bool isInsecureScheme = GetInsecureScheme();
  bool isNoSpdy = GetNoSpdy();
  bool isBeConservative = GetBeConservative();
  bool isAnonymousAllowClientCert = GetAnonymousAllowClientCert();
  bool isFallback = GetFallbackConnection();
  bool isHappyEyeballs = GetHappyEyeballsEnabled();

  BuildHashKey();

  // Restore all of those properties.
  SetAnonymous(isAnonymous);
  SetPrivate(isPrivate);
  SetInsecureScheme(isInsecureScheme);
  SetNoSpdy(isNoSpdy);
  SetBeConservative(isBeConservative);
  SetAnonymousAllowClientCert(isAnonymousAllowClientCert);
  SetFallbackConnection(isFallback);
  SetTlsFlags(mTlsFlags);
  SetHappyEyeballsEnabled(isHappyEyeballs);
}

void nsHttpConnectionInfo::SetOriginServer(const nsACString& host,
                                           int32_t port) {
  mOrigin = host;
  mOriginPort = port == -1 ? DefaultPort() : port;
  // Use BuildHashKey() since this can only be called when constructing an
  // nsHttpConnectionInfo object.
  MOZ_DIAGNOSTIC_ASSERT(mHashKey.IsEmpty());
  BuildHashKey();
}

already_AddRefed<nsHttpConnectionInfo> nsHttpConnectionInfo::Clone() const {
  RefPtr<nsHttpConnectionInfo> clone;
  if (mRoutedHost.IsEmpty()) {
    clone = new nsHttpConnectionInfo(mOrigin, mOriginPort, mNPNToken, mUsername,
                                     mProxyInfo, mOriginAttributes,
                                     mEndToEndSSL, mIsHttp3, mWebTransport);
  } else {
    MOZ_ASSERT(mEndToEndSSL);
    clone = new nsHttpConnectionInfo(mOrigin, mOriginPort, mNPNToken, mUsername,
                                     mProxyInfo, mOriginAttributes, mRoutedHost,
                                     mRoutedPort, mIsHttp3, mWebTransport);
  }

  // Make sure the anonymous, insecure-scheme, and private flags are transferred
  clone->SetAnonymous(GetAnonymous());
  clone->SetPrivate(GetPrivate());
  clone->SetInsecureScheme(GetInsecureScheme());
  clone->SetNoSpdy(GetNoSpdy());
  clone->SetBeConservative(GetBeConservative());
  clone->SetAnonymousAllowClientCert(GetAnonymousAllowClientCert());
  clone->SetFallbackConnection(GetFallbackConnection());
  clone->SetTlsFlags(GetTlsFlags());
  clone->SetIsTrrServiceChannel(GetIsTrrServiceChannel());
  clone->SetTRRMode(GetTRRMode());
  clone->SetIPv4Disabled(GetIPv4Disabled());
  clone->SetIPv6Disabled(GetIPv6Disabled());
  clone->SetHttp3Policy(GetHttp3Policy());
  clone->SetHasIPHintAddress(HasIPHintAddress());
  clone->SetEchConfig(GetEchConfig());
  clone->SetWebTransportId(GetWebTransportId());
  clone->SetHappyEyeballsEnabled(GetHappyEyeballsEnabled());

  MOZ_ASSERT(clone->Equals(this));

  return clone.forget();
}

already_AddRefed<nsHttpConnectionInfo>
nsHttpConnectionInfo::CloneAndAdoptHTTPSSVCRecord(
    nsISVCBRecord* aRecord) const {
  MOZ_ASSERT(aRecord);

  // Get the domain name of this HTTPS RR. This name will be assigned to
  // mRoutedHost in the new connection info.
  nsAutoCString name;
  aRecord->GetName(name);

  // Try to get the port and Alpn. If this record has SvcParamKeyPort defined,
  // the new port will be used as mRoutedPort.
  Maybe<uint16_t> port = aRecord->GetPort();
  Maybe<std::tuple<nsCString, SupportedAlpnRank>> alpn = aRecord->GetAlpn();

  // Let the new conn info learn h3 will be used.
  bool isHttp3 = alpn ? mozilla::net::IsHttp3(std::get<1>(*alpn)) : false;

  LOG(("HTTPSSVC: use new routed host (%s) and new npnToken (%s)", name.get(),
       alpn ? std::get<0>(*alpn).get() : "None"));

  RefPtr<nsHttpConnectionInfo> clone;
  if (name.IsEmpty()) {
    clone = new nsHttpConnectionInfo(mOrigin, mOriginPort,
                                     alpn ? std::get<0>(*alpn) : EmptyCString(),
                                     mUsername, mProxyInfo, mOriginAttributes,
                                     mEndToEndSSL, isHttp3, mWebTransport);
  } else {
    MOZ_ASSERT(mEndToEndSSL);
    clone = new nsHttpConnectionInfo(
        mOrigin, mOriginPort, alpn ? std::get<0>(*alpn) : EmptyCString(),
        mUsername, mProxyInfo, mOriginAttributes, name,
        port ? *port : mOriginPort, isHttp3, mWebTransport);
  }

  // Make sure the anonymous, insecure-scheme, and private flags are transferred
  clone->SetAnonymous(GetAnonymous());
  clone->SetPrivate(GetPrivate());
  clone->SetInsecureScheme(GetInsecureScheme());
  clone->SetNoSpdy(GetNoSpdy());
  clone->SetBeConservative(GetBeConservative());
  clone->SetAnonymousAllowClientCert(GetAnonymousAllowClientCert());
  clone->SetFallbackConnection(GetFallbackConnection());
  clone->SetTlsFlags(GetTlsFlags());
  clone->SetIsTrrServiceChannel(GetIsTrrServiceChannel());
  clone->SetTRRMode(GetTRRMode());
  clone->SetIPv4Disabled(GetIPv4Disabled());
  clone->SetIPv6Disabled(GetIPv6Disabled());
  clone->SetHttp3Policy(GetHttp3Policy());
  clone->SetHappyEyeballsEnabled(GetHappyEyeballsEnabled());

  bool hasIPHint = false;
  (void)aRecord->GetHasIPHintAddress(&hasIPHint);
  if (hasIPHint) {
    clone->SetHasIPHintAddress(hasIPHint);
  }

  nsAutoCString echConfig;
  (void)aRecord->GetEchConfig(echConfig);
  clone->SetEchConfig(echConfig);

  return clone.forget();
}

already_AddRefed<nsHttpConnectionInfo>
nsHttpConnectionInfo::CloneAndAdoptPortAndAlpn(
    uint16_t aPort,
    happy_eyeballs::ConnectionAttemptHttpVersions aProtocol) const {
  // See TlsHandshaker::SetupNPNList, "http/1.1" and "h2" are added
  // automatically, so we only need to set "h3".
  nsAutoCString alpnStr(
      aProtocol == happy_eyeballs::ConnectionAttemptHttpVersions::H3
          ? "h3"_ns
          : EmptyCString());
  bool isHttp3 = aProtocol == happy_eyeballs::ConnectionAttemptHttpVersions::H3;
  int32_t port = aPort != 0 ? aPort : mOriginPort;
  RefPtr<nsHttpConnectionInfo> clone;
  if (mEndToEndSSL && port != mOriginPort) {
    const nsACString& routedHost =
        mRoutedHost.IsEmpty() ? mOrigin : mRoutedHost;
    clone = new nsHttpConnectionInfo(mOrigin, mOriginPort, alpnStr, mUsername,
                                     mProxyInfo, mOriginAttributes, routedHost,
                                     port, isHttp3, mWebTransport);
  } else {
    clone = new nsHttpConnectionInfo(mOrigin, port, alpnStr, mUsername,
                                     mProxyInfo, mOriginAttributes,
                                     mEndToEndSSL, isHttp3, mWebTransport);
  }

  clone->SetAnonymous(GetAnonymous());
  clone->SetPrivate(GetPrivate());
  clone->SetInsecureScheme(GetInsecureScheme());
  clone->SetNoSpdy(GetNoSpdy());
  clone->SetBeConservative(GetBeConservative());
  clone->SetAnonymousAllowClientCert(GetAnonymousAllowClientCert());
  clone->SetFallbackConnection(GetFallbackConnection());
  clone->SetTlsFlags(GetTlsFlags());
  clone->SetIsTrrServiceChannel(GetIsTrrServiceChannel());
  clone->SetTRRMode(GetTRRMode());
  clone->SetIPv4Disabled(GetIPv4Disabled());
  clone->SetIPv6Disabled(GetIPv6Disabled());
  clone->SetHttp3Policy(GetHttp3Policy());
  clone->SetHappyEyeballsEnabled(GetHappyEyeballsEnabled());
  // Preserve the WebTransport id so the clone hashes to the same connection
  // entry as the origin connection info — WebTransport server cert hashes are
  // stored under that entry and looked up by Http3Session::Init.
  clone->SetWebTransportId(GetWebTransportId());

  // IPHints and echConfig are handled in HappyEyeballsConnectionAttempt.
  return clone.forget();
}

void nsHttpConnectionInfo::CloneAsDirectRoute(nsHttpConnectionInfo** outCI,
                                              nsProxyInfo* aProxyInfo) {
  // Explicitly use an empty npnToken when |mIsHttp3| is true, since we want to
  // create a non-http3 connection info.
  RefPtr<nsHttpConnectionInfo> clone = new nsHttpConnectionInfo(
      mOrigin, mOriginPort,
      (mRoutedHost.IsEmpty() && !mIsHttp3) ? mNPNToken : ""_ns, mUsername,
      aProxyInfo ? aProxyInfo : mProxyInfo.get(), mOriginAttributes,
      mEndToEndSSL, false, mWebTransport);
  // Make sure the anonymous, insecure-scheme, and private flags are transferred
  clone->SetAnonymous(GetAnonymous());
  clone->SetPrivate(GetPrivate());
  clone->SetInsecureScheme(GetInsecureScheme());
  clone->SetNoSpdy(GetNoSpdy());
  clone->SetBeConservative(GetBeConservative());
  clone->SetAnonymousAllowClientCert(GetAnonymousAllowClientCert());
  clone->SetFallbackConnection(GetFallbackConnection());
  clone->SetTlsFlags(GetTlsFlags());
  clone->SetIsTrrServiceChannel(GetIsTrrServiceChannel());
  clone->SetTRRMode(GetTRRMode());
  clone->SetIPv4Disabled(GetIPv4Disabled());
  clone->SetIPv6Disabled(GetIPv6Disabled());
  // This clone is deliberately not h3 (empty npnToken, isHttp3 = false), so an
  // Only policy would contradict it. Unlike CloneAndAdoptHTTPSSVCRecord and
  // CloneAndAdoptPortAndAlpn, which describe the endpoint an h3-only attempt is
  // dialling and must keep it.
  clone->SetHttp3Policy(GetHttp3Only() ? Http3Policy::Allowed
                                       : GetHttp3Policy());
  clone->SetHasIPHintAddress(HasIPHintAddress());
  clone->SetEchConfig(GetEchConfig());
  clone->SetHappyEyeballsEnabled(GetHappyEyeballsEnabled());

  clone.forget(outCI);
}

already_AddRefed<nsHttpConnectionInfo>
nsHttpConnectionInfo::CreateConnectUDPFallbackConnInfo() {
  if (!mProxyInfo || !mProxyInfo->IsHttp3Proxy()) {
    return nullptr;
  }

  RefPtr<nsProxyInfo> proxyInfo = mProxyInfo->CreateFallbackProxyInfo();
  RefPtr<nsHttpConnectionInfo> clone;
  CloneAsDirectRoute(getter_AddRefs(clone), proxyInfo);
  return clone.forget();
}

nsresult nsHttpConnectionInfo::CreateWildCard(nsHttpConnectionInfo** outParam) {
  // T???mozilla.org:443 (https:proxy.ducksong.com:3128) [specifc form]
  // TS??*:0 (https:proxy.ducksong.com:3128)   [wildcard form]

  if (!mUsingHttpsProxy) {
    MOZ_ASSERT(false);
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  RefPtr<nsHttpConnectionInfo> clone;
  clone = new nsHttpConnectionInfo("*"_ns, 0, mNPNToken, mUsername, mProxyInfo,
                                   mOriginAttributes, true, mIsHttp3,
                                   mWebTransport);
  // Make sure the anonymous and private flags are transferred!
  clone->SetAnonymous(GetAnonymous());
  clone->SetPrivate(GetPrivate());
  clone->SetFallbackConnection(GetFallbackConnection());
  clone.forget(outParam);
  return NS_OK;
}

void nsHttpConnectionInfo::SetTRRMode(nsIRequest::TRRMode aTRRMode) {
  if (mTRRMode != aTRRMode) {
    mTRRMode = aTRRMode;
    RebuildHashKey();
  }
}

void nsHttpConnectionInfo::SetIPv4Disabled(bool aNoIPv4) {
  if (mIPv4Disabled != aNoIPv4) {
    mIPv4Disabled = aNoIPv4;
    RebuildHashKey();
  }
}

void nsHttpConnectionInfo::SetIPv6Disabled(bool aNoIPv6) {
  if (mIPv6Disabled != aNoIPv6) {
    mIPv6Disabled = aNoIPv6;
    RebuildHashKey();
  }
}

void nsHttpConnectionInfo::SetHttp3Policy(Http3Policy aPolicy) {
  if (mHttp3Policy != aPolicy) {
    mHttp3Policy = aPolicy;
    RebuildHashKey();
  }
}

void nsHttpConnectionInfo::SetWebTransport(bool aWebTransport) {
  if (mWebTransport != aWebTransport) {
    mWebTransport = aWebTransport;
    RebuildHashKey();
  }
}

void nsHttpConnectionInfo::SetWebTransportId(uint64_t id) {
  if (mWebTransportId != id) {
    mWebTransportId = id;
    RebuildHashKey();
  }
}

void nsHttpConnectionInfo::SetTlsFlags(uint32_t aTlsFlags) {
  mTlsFlags = aTlsFlags;
  const uint32_t tlsFlagsLength = 8;
  const uint32_t tlsFlagsIndex =
      UnderlyingIndex(HashKeyIndex::End) + strlen("[tlsflags0x");
  mHashKey.Replace(tlsFlagsIndex, tlsFlagsLength,
                   nsPrintfCString("%08x", mTlsFlags));
}

bool nsHttpConnectionInfo::UsingProxy() const {
  if (!mProxyInfo) return false;
  return !mProxyInfo->IsDirect();
}

bool nsHttpConnectionInfo::HostIsLocalIPLiteral() const {
  NetAddr netAddr;
  // If the host/proxy host is not an IP address literal, return false.
  nsAutoCString host(ProxyHost() ? ProxyHost() : Origin());
  if (NS_FAILED(netAddr.InitFromString(host))) {
    return false;
  }
  return netAddr.IsIPAddrLocal();
}

// static
CoalescingKey nsHttpConnectionInfo::BuildOriginFrameHashKey(
    nsHttpConnectionInfo* ci, const nsACString& host, int32_t port) {
  nsCString newKey(host);
  newKey.Append(ci->GetAnonymous() ? "~A:" : "~.:");
  newKey.Append(ci->GetFallbackConnection() ? "~F:" : "~.:");
  newKey.AppendInt(port);
  newKey.AppendLiteral("/[");
  nsAutoCString suffix;
  ci->GetOriginAttributes().CreateSuffix(suffix);
  newKey.Append(suffix);
  newKey.AppendLiteral("]viaORIGIN.FRAME");
  return CoalescingKey{HashString(newKey), std::move(newKey)};
}

}  // namespace net
}  // namespace mozilla
