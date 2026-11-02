/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AlternateServices.h"

#include <algorithm>

#include "HttpLog.h"
#include "mozilla/Atomics.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/PContent.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "nsEscape.h"
#include "nsHttpConnectionInfo.h"
#include "nsHttpHandler.h"
#include "nsHttpTransaction.h"
#include "nsITLSSocketControl.h"
#include "nsThreadUtils.h"
#include "xpcpublic.h"

/* RFC 7838 Alternative Services
   http://httpwg.org/http-extensions/opsec.html
    note that connections currently do not do mixed-scheme (the I attribute
    in the ConnectionInfo prevents it) but could, do not honor tls-commit and
   should not, and always require authentication
*/

namespace mozilla {
namespace net {

// function places true in outIsHTTPS if scheme is https, false if
// http, and returns an error if neither. originScheme passed into
// alternate service should already be normalized to those lower case
// strings by the URI parser (and so there is an assert)- this is an extra
// check.
static nsresult SchemeIsHTTPS(const nsACString& originScheme,
                              bool& outIsHTTPS) {
  outIsHTTPS = originScheme.EqualsLiteral("https");

  if (!outIsHTTPS && !originScheme.EqualsLiteral("http")) {
    MOZ_ASSERT(!originScheme.LowerCaseEqualsLiteral("https") &&
                   !originScheme.LowerCaseEqualsLiteral("http"),
               "The scheme should already be lowercase");
    return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

bool AltSvcMapping::AcceptableProxy(nsProxyInfo* proxyInfo) {
  // TODO: We also need to make sure the inner connection will connect to the
  // routed host.
  return !proxyInfo || proxyInfo->IsDirect() || proxyInfo->IsSOCKS() ||
         proxyInfo->IsHttp3Proxy();
}

void AltSvcMapping::ProcessHeader(
    const nsCString& buf, const nsCString& originScheme,
    const nsCString& originHost, int32_t originPort, const nsACString& username,
    bool privateBrowsing, nsIInterfaceRequestor* callbacks,
    nsProxyInfo* proxyInfo, uint32_t caps,
    const OriginAttributes& originAttributes,
    nsHttpConnectionInfo* aTransConnInfo, bool aDontValidate /* = false */) {
  LOG(("AltSvcMapping::ProcessHeader: %s\n", buf.get()));
  // In tests, this might be called off the main thread. If so, dispatch it
  // synchronously to the main thread.
  if (!NS_IsMainThread() && xpc::AreNonLocalConnectionsDisabled()) {
    nsCOMPtr<nsIThread> mainThread;
    nsresult rv = NS_GetMainThread(getter_AddRefs(mainThread));
    if (NS_FAILED(rv)) {
      return;
    }

    nsCString userName(username);
    nsCOMPtr<nsIInterfaceRequestor> cb = callbacks;
    RefPtr<nsProxyInfo> info = proxyInfo;
    RefPtr<nsHttpConnectionInfo> connInfo = aTransConnInfo;
    // Forward to the main thread synchronously.
    mozilla::SyncRunnable::DispatchToThread(
        mainThread,
        NS_NewRunnableFunction(
            "AltSvcMapping::ProcessHeader",
            [buf(buf), originScheme(originScheme), originHost(originHost),
             originPort, userName = std::move(userName), privateBrowsing,
             cb = std::move(cb), info = std::move(info), caps, originAttributes,
             connInfo = std::move(connInfo), aDontValidate]() {
              AltSvcMapping::ProcessHeader(
                  buf, originScheme, originHost, originPort, userName,
                  privateBrowsing, cb, info, caps, originAttributes, connInfo,
                  aDontValidate);
            }));

    return;
  }

  // AltSvcMapping::ProcessHeader is not thread-safe.
  if (!NS_IsMainThread()) {
    return;
  }

  if (StaticPrefs::network_http_altsvc_proxy_checks() &&
      !AcceptableProxy(proxyInfo)) {
    LOG(("AltSvcMapping::ProcessHeader ignoring due to proxy\n"));
    return;
  }

  bool isHTTPS;
  if (NS_FAILED(SchemeIsHTTPS(originScheme, isHTTPS))) {
    return;
  }
  if (!isHTTPS) {
    LOG(("Alt-Svc Response Header for http:// origin ignored\n"));
    return;
  }

  LOG(("Alt-Svc Response Header %s\n", buf.get()));
  ParsedHeaderValueListList parsedAltSvc(buf);

  nsTArray<RefPtr<AltSvcMapping>> h3Mappings;
  nsTArray<RefPtr<AltSvcMapping>> otherMappings;
  for (uint32_t index = 0; index < parsedAltSvc.mValues.Length(); ++index) {
    uint32_t maxage = 86400;  // default
    nsAutoCString hostname;
    nsAutoCString npnToken;
    int32_t portno = originPort;
    bool clearEntry = false;
    SupportedAlpnRank alpnRank = SupportedAlpnRank::NOT_SUPPORTED;

    for (uint32_t pairIndex = 0;
         pairIndex < parsedAltSvc.mValues[index].mValues.Length();
         ++pairIndex) {
      nsDependentCSubstring& currentName =
          parsedAltSvc.mValues[index].mValues[pairIndex].mName;
      nsDependentCSubstring& currentValue =
          parsedAltSvc.mValues[index].mValues[pairIndex].mValue;

      if (!pairIndex) {
        if (currentName.EqualsLiteral("clear")) {
          clearEntry = true;
          break;
        }

        // h2=[hostname]:443 or h3-xx=[hostname]:port
        // XX is current version we support and it is define in nsHttp.h.
        alpnRank = IsAlpnSupported(currentName);
        npnToken = currentName;

        int32_t colonIndex = currentValue.FindChar(':');
        if (colonIndex >= 0) {
          portno =
              atoi(PromiseFlatCString(currentValue).get() + colonIndex + 1);
        } else {
          colonIndex = 0;
        }
        hostname.Assign(currentValue.BeginReading(), colonIndex);
      } else if (currentName.EqualsLiteral("ma")) {
        maxage = atoi(PromiseFlatCString(currentValue).get());
      } else {
        LOG(("Alt Svc ignoring parameter %s",
             PromiseFlatCString(currentName).get()));
      }
    }

    if (clearEntry) {
      nsCString suffix;
      originAttributes.CreateSuffix(suffix);
      LOG(("Alt Svc clearing mapping for %s:%d:%s", originHost.get(),
           originPort, suffix.get()));
      gHttpHandler->AltServiceCache()->ClearHostMapping(originHost, originPort,
                                                        originAttributes);
      continue;
    }

    if (NS_FAILED(NS_CheckPortSafety(portno, originScheme.get()))) {
      LOG(("Alt Svc doesn't allow port %d, ignoring", portno));
      continue;
    }

    // unescape modifies a c string in place, so afterwards
    // update nsCString length
    nsUnescape(npnToken.BeginWriting());
    npnToken.SetLength(strlen(npnToken.get()));
    bool isHttp3 = net::IsHttp3(alpnRank);
    SpdyInformation* spdyInfo = gHttpHandler->SpdyInfo();
    if (!(npnToken.Equals(spdyInfo->VersionString) &&
          StaticPrefs::network_http_http2_enabled()) &&
        !(isHttp3 && nsHttpHandler::IsHttp3Enabled() &&
          !gHttpHandler->IsHttp3Excluded(hostname.IsEmpty() ? originHost
                                                            : hostname))) {
      LOG(("Alt Svc unknown protocol %s, ignoring", npnToken.get()));
      continue;
    }

    LOG(("AltSvcMapping created npnToken=%s", npnToken.get()));
    RefPtr<AltSvcMapping> mapping = new AltSvcMapping(
        gHttpHandler->AltServiceCache()->GetStoragePtr(),
        gHttpHandler->AltServiceCache()->StorageEpoch(), originScheme,
        originHost, originPort, username, privateBrowsing,
        NowInSeconds() + maxage, hostname, portno, npnToken, originAttributes,
        isHttp3, alpnRank);
    if (mapping->TTL() <= 0) {
      LOG(("Alt Svc invalid map"));
      mapping = nullptr;
      // since this isn't a parse error, let's clear any existing mapping
      // as that would have happened if we had accepted the parameters.
      gHttpHandler->AltServiceCache()->ClearHostMapping(originHost, originPort,
                                                        originAttributes);
    } else {
      if (isHttp3) {
        h3Mappings.AppendElement(std::move(mapping));
      } else {
        otherMappings.AppendElement(std::move(mapping));
      }
    }
  }

  auto doUpdateAltSvcMapping = [&](AltSvcMapping* aMapping) {
    if (aTransConnInfo) {
      if (!aTransConnInfo->GetEchConfig().IsEmpty()) {
        LOG(("Server has ECH, use HTTPS RR to connect instead"));
        return;
      }
      if (StaticPrefs::network_http_skip_alt_svc_validation_on_https_rr()) {
        RefPtr<nsHttpConnectionInfo> ci;
        aMapping->GetConnectionInfo(getter_AddRefs(ci), proxyInfo,
                                    originAttributes);
        // Skip only when the transaction already reached the alternate (e.g.
        // via HTTPS RR). Under Happy Eyeballs BuildHashKey drops the routed
        // host/NPN token, so hash keys collide and this would skip every
        // ordinary Alt-Svc header (bug 2051272). For h3 we want real validation
        // (to warm the h3 connection), so additionally require the routed
        // endpoint to match; under non-HE those fields are in the hash key, so
        // this is a no-op. For h2, AltSvcTransaction validation can't complete
        // under HE (bug 2051272 #2), so keep skipping on a hash-key match.
        bool sameHashKey = ci->HashKey().Equals(aTransConnInfo->HashKey());
        if (ci->IsHttp3()) {
          if (sameHashKey &&
              ci->GetRoutedHost().Equals(aTransConnInfo->GetRoutedHost()) &&
              ci->RoutedPort() == aTransConnInfo->RoutedPort() &&
              ci->GetNPNToken().Equals(aTransConnInfo->GetNPNToken())) {
            aDontValidate = true;
          }
        } else if (sameHashKey) {
          aDontValidate = true;
        }
        if (aDontValidate) {
          LOG(("The transaction's conninfo is the same, no need to validate"));
        }
      }
    }
    if (!aDontValidate) {
      gHttpHandler->UpdateAltServiceMapping(aMapping, proxyInfo, callbacks,
                                            caps, originAttributes);
    } else {
      gHttpHandler->UpdateAltServiceMappingWithoutValidation(
          aMapping, proxyInfo, callbacks, caps, originAttributes);
    }
  };

  if (!h3Mappings.IsEmpty()) {
    // Select the HTTP/3 (h3) AltSvcMapping with the highest ALPN rank from
    // h3Mappings.
    RefPtr<AltSvcMapping> latestH3Mapping = *std::max_element(
        h3Mappings.begin(), h3Mappings.end(),
        [](const RefPtr<AltSvcMapping>& a, const RefPtr<AltSvcMapping>& b) {
          return a->AlpnRank() < b->AlpnRank();
        });
    doUpdateAltSvcMapping(latestH3Mapping);
  }

  std::for_each(otherMappings.begin(), otherMappings.end(),
                doUpdateAltSvcMapping);
}

AltSvcMapping::AltSvcMapping(nsIDataStorage* storage, int32_t epoch,
                             const nsACString& originScheme,
                             const nsACString& originHost, int32_t originPort,
                             const nsACString& username, bool privateBrowsing,
                             uint32_t expiresAt,
                             const nsACString& alternateHost,
                             int32_t alternatePort, const nsACString& npnToken,
                             const OriginAttributes& originAttributes,
                             bool aIsHttp3, SupportedAlpnRank aRank)
    : mStorage(storage),
      mStorageEpoch(epoch),
      mAlternateHost(alternateHost),
      mAlternatePort(alternatePort),
      mOriginHost(originHost),
      mOriginPort(originPort),
      mUsername(username),
      mPrivate(privateBrowsing),
      mExpiresAt(expiresAt),
      mNPNToken(npnToken),
      mOriginAttributes(originAttributes),
      mIsHttp3(aIsHttp3),
      mAlpnRank(aRank) {
  MOZ_ASSERT(NS_IsMainThread());

  if (NS_FAILED(SchemeIsHTTPS(originScheme, mHttps))) {
    LOG(("AltSvcMapping ctor %p invalid scheme\n", this));
    mExpiresAt = 0;  // invalid
  }

  if (mAlternatePort == -1) {
    mAlternatePort = mHttps ? NS_HTTPS_DEFAULT_PORT : NS_HTTP_DEFAULT_PORT;
  }
  if (mOriginPort == -1) {
    mOriginPort = mHttps ? NS_HTTPS_DEFAULT_PORT : NS_HTTP_DEFAULT_PORT;
  }

  LOG(("AltSvcMapping ctor %p %s://%s:%d to %s:%d\n", this,
       PromiseFlatCString(originScheme).get(), mOriginHost.get(), mOriginPort,
       mAlternateHost.get(), mAlternatePort));

  if (mAlternateHost.IsEmpty()) {
    mAlternateHost = mOriginHost;
  }

  if ((mAlternatePort == mOriginPort) &&
      mAlternateHost.EqualsIgnoreCase(mOriginHost.get()) && !mIsHttp3) {
    // Http2 on the same host:port does not make sense because we are
    // connecting to the same end point over the same protocol (TCP) as with
    // original host. On the other hand, for Http3 alt-svc can be hosted on
    // the same host:port because protocol(UDP vs. TCP) is always different and
    // we are not connecting to the same end point.
    LOG(("Alt Svc is also origin Svc - ignoring\n"));
    mExpiresAt = 0;  // invalid
  }

  if (mExpiresAt) {
    MakeHashKey(mHashKey, originScheme, mOriginHost, mOriginPort, mPrivate,
                mOriginAttributes, mIsHttp3);
  }
}

AltSvcMapping::AltSvcMapping(const nsACString& originScheme,
                             const nsACString& originHost, int32_t originPort,
                             const nsACString& username, bool privateBrowsing,
                             const nsACString& alternateHost,
                             int32_t alternatePort, const nsACString& npnToken,
                             const OriginAttributes& originAttributes,
                             bool aIsHttp3, SupportedAlpnRank aRank)
    : mAlternateHost(alternateHost),
      mAlternatePort(alternatePort),
      mOriginHost(originHost),
      mOriginPort(originPort),
      mUsername(username),
      mPrivate(privateBrowsing),
      mNPNToken(npnToken),
      mOriginAttributes(originAttributes),
      mIsHttp3(aIsHttp3),
      mAlpnRank(aRank) {}

void AltSvcMapping::MakeHashKey(nsCString& outKey,
                                const nsACString& originScheme,
                                const nsACString& originHost,
                                int32_t originPort, bool privateBrowsing,
                                const OriginAttributes& originAttributes,
                                bool aHttp3) {
  outKey.Truncate();

  if (originPort == -1) {
    bool isHttps = originScheme.EqualsLiteral("https");
    originPort = isHttps ? NS_HTTPS_DEFAULT_PORT : NS_HTTP_DEFAULT_PORT;
  }

  outKey.Append(originScheme);
  outKey.Append(':');
  outKey.Append(originHost);
  outKey.Append(':');
  outKey.AppendInt(originPort);
  outKey.Append(':');
  outKey.Append(privateBrowsing ? 'P' : '.');
  outKey.Append(':');
  nsAutoCString suffix;
  originAttributes.CreateSuffix(suffix);
  outKey.Append(suffix);
  outKey.Append(':');

  outKey.Append(aHttp3 ? '3' : '.');
}

int32_t AltSvcMapping::TTL() { return mExpiresAt - NowInSeconds(); }

void AltSvcMapping::SyncString(const nsCString& str) {
  MOZ_ASSERT(NS_IsMainThread());
  (void)mStorage->Put(HashKey(), str,
                      mPrivate ? nsIDataStorage::DataType::Private
                               : nsIDataStorage::DataType::Persistent);
}

void AltSvcMapping::Sync() {
  if (!mStorage) {
    return;
  }
  if (mSyncOnlyOnSuccess && !mValidated) {
    return;
  }
  nsCString value;
  Serialize(value);

  if (!NS_IsMainThread()) {
    nsCOMPtr<nsIRunnable> r;
    r = NewRunnableMethod<nsCString>("net::AltSvcMapping::SyncString", this,
                                     &AltSvcMapping::SyncString, value);
    NS_DispatchToMainThread(r, NS_DISPATCH_NORMAL);
    return;
  }

  (void)mStorage->Put(HashKey(), value,
                      mPrivate ? nsIDataStorage::DataType::Private
                               : nsIDataStorage::DataType::Persistent);
}

void AltSvcMapping::SetValidated(bool val) {
  mValidated = val;
  Sync();
}

void AltSvcMapping::SetMixedScheme(bool val) {
  mMixedScheme = val;
  Sync();
}

void AltSvcMapping::SetExpiresAt(int32_t val) {
  mExpiresAt = val;
  Sync();
}

void AltSvcMapping::SetExpired() {
  LOG(("AltSvcMapping SetExpired %p origin %s alternate %s\n", this,
       mOriginHost.get(), mAlternateHost.get()));
  mExpiresAt = NowInSeconds() - 1;
  Sync();
}

bool AltSvcMapping::RouteEquals(AltSvcMapping* map) {
  MOZ_ASSERT(map->mHashKey.Equals(mHashKey));
  return mAlternateHost.Equals(map->mAlternateHost) &&
         (mAlternatePort == map->mAlternatePort) &&
         mNPNToken.Equals(map->mNPNToken);
}

void AltSvcMapping::GetConnectionInfo(
    nsHttpConnectionInfo** outCI, nsProxyInfo* pi,
    const OriginAttributes& originAttributes) {
  RefPtr<nsHttpConnectionInfo> ci = new nsHttpConnectionInfo(
      mOriginHost, mOriginPort, mNPNToken, mUsername, pi, originAttributes,
      mAlternateHost, mAlternatePort, mIsHttp3, false);

  // http:// without the mixed-scheme attribute needs to be segmented in the
  // connection manager connection information hash with this attribute
  ci = ci->Mutate()
           .SetInsecureScheme(!mHttps && !mMixedScheme)
           .SetHappyEyeballsEnabled(
               StaticPrefs::network_http_happy_eyeballs_enabled() && !pi)
           .SetPrivate(mPrivate)
           .Finalize();
  ci.forget(outCI);
}

void AltSvcMapping::Serialize(nsCString& out) {
  // Be careful, when serializing new members, add them to the end of this list.
  out = mHttps ? "https:"_ns : "http:"_ns;
  out.Append(mOriginHost);
  out.Append(':');
  out.AppendInt(mOriginPort);
  out.Append(':');
  out.Append(mAlternateHost);
  out.Append(':');
  out.AppendInt(mAlternatePort);
  out.Append(':');
  out.Append(mUsername);
  out.Append(':');
  out.Append(mPrivate ? 'y' : 'n');
  out.Append(':');
  out.AppendInt(mExpiresAt);
  out.Append(':');
  out.Append(mNPNToken);
  out.Append(':');
  out.Append(mValidated ? 'y' : 'n');
  out.Append(':');
  out.AppendInt(mStorageEpoch);
  out.Append(':');
  out.Append(mMixedScheme ? 'y' : 'n');
  out.Append(':');
  nsAutoCString suffix;
  mOriginAttributes.CreateSuffix(suffix);
  out.Append(suffix);
  out.Append(':');
  out.Append(""_ns);  // Formerly topWindowOrigin. Now unused empty string.
  out.Append('|');    // Be careful, the top window origin may contain colons!
  out.Append('n');  // Formerly mIsolated. Now always 'n'. Should remove someday
  out.Append(':');
  out.Append(mIsHttp3 ? 'y' : 'n');
  out.Append(':');
  // Add code to serialize new members here!
}

AltSvcMapping::AltSvcMapping(nsIDataStorage* storage, int32_t epoch,
                             const nsCString& str)
    : mStorage(storage), mStorageEpoch(epoch) {
  mValidated = false;
  nsresult code;
  char separator = ':';

  // The the do {} while(0) loop acts like try/catch(e){} with the break in
  // _NS_NEXT_TOKEN
  do {
#ifdef _NS_NEXT_TOKEN
    COMPILER ERROR
#endif
#define _NS_NEXT_TOKEN                  \
  start = idx + 1;                      \
  idx = str.FindChar(separator, start); \
  if (idx < 0) break;
        int32_t start = 0;
    int32_t idx;
    idx = str.FindChar(separator, start);
    if (idx < 0) break;
    // Be careful, when deserializing new members, add them to the end of this
    // list.
    mHttps = Substring(str, start, idx - start).EqualsLiteral("https");
    _NS_NEXT_TOKEN;
    mOriginHost = Substring(str, start, idx - start);
    _NS_NEXT_TOKEN;
    mOriginPort = Substring(str, start, idx - start).ToInteger(&code);
    _NS_NEXT_TOKEN;
    mAlternateHost = Substring(str, start, idx - start);
    _NS_NEXT_TOKEN;
    mAlternatePort = Substring(str, start, idx - start).ToInteger(&code);
    _NS_NEXT_TOKEN;
    mUsername = Substring(str, start, idx - start);
    _NS_NEXT_TOKEN;
    mPrivate = Substring(str, start, idx - start).EqualsLiteral("y");
    _NS_NEXT_TOKEN;
    mExpiresAt = Substring(str, start, idx - start).ToInteger(&code);
    _NS_NEXT_TOKEN;
    mNPNToken = Substring(str, start, idx - start);
    _NS_NEXT_TOKEN;
    mValidated = Substring(str, start, idx - start).EqualsLiteral("y");
    _NS_NEXT_TOKEN;
    mStorageEpoch =
        nsCString(Substring(str, start, idx - start)).ToInteger(&code);
    _NS_NEXT_TOKEN;
    mMixedScheme = Substring(str, start, idx - start).EqualsLiteral("y");
    _NS_NEXT_TOKEN;
    (void)mOriginAttributes.PopulateFromSuffix(
        Substring(str, start, idx - start));
    // The separator after the top window origin is a pipe character since the
    // origin string can contain colons.
    separator = '|';
    _NS_NEXT_TOKEN;
    // TopWindowOrigin used to be encoded here. Now it's unused.
    separator = ':';
    _NS_NEXT_TOKEN;
    // mIsolated used to be encoded here. Now it's unused.
    _NS_NEXT_TOKEN;
    mIsHttp3 = Substring(str, start, idx - start).EqualsLiteral("y");
    // Add code to deserialize new members here!
#undef _NS_NEXT_TOKEN

    MakeHashKey(mHashKey, mHttps ? "https"_ns : "http"_ns, mOriginHost,
                mOriginPort, mPrivate, mOriginAttributes, mIsHttp3);
  } while (false);
}

AltSvcTransaction::AltSvcTransaction(nsHttpConnectionInfo* ci,
                                     nsIInterfaceRequestor* callbacks,
                                     uint32_t caps, AltSvcMapping* aMap)
    : SpeculativeTransaction(ci, callbacks, caps),
      mMapping(aMap),
      mIsHttp3(aMap->IsHttp3()),
      mTriedToValidate(false),
      mTriedToWrite(false),
      mValidatedResult(false) {
  LOG(("AltSvcTransaction ctor %p map %p [%s -> %s]", this, aMap,
       aMap->OriginHost().get(), aMap->AlternateHost().get()));
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(mMapping->HTTPS());  // http:// uses the .wk path
  // We don't want to let this transaction use consistent connection.
  mCaps &= ~NS_HTTP_ALLOW_KEEPALIVE;
}

AltSvcTransaction::~AltSvcTransaction() {
  mValidatedResult = MaybeValidate(NS_OK);
  mMapping->SetValidated(mValidatedResult);
  if (!mMapping->Validated()) {
    // try again later
    mMapping->SetExpiresAt(NowInSeconds() + 2);
  }
  LOG(("AltSvcTransaction dtor %p map %p validated %d [%s]", this,
       mMapping.get(), mMapping->Validated(), mMapping->HashKey().get()));
}

bool AltSvcTransaction::MaybeValidate(nsresult reason) {
  if (mTriedToValidate) {
    return mValidatedResult;
  }
  mTriedToValidate = true;

  LOG(("AltSvcTransaction::MaybeValidate() %p reason=%" PRIx32
       " conn=%p write=%d",
       this, static_cast<uint32_t>(reason), mConnection.get(), mTriedToWrite));

  if (mTriedToWrite && reason == NS_BASE_STREAM_CLOSED) {
    // The normal course of events is to cause the transaction to fail with
    // CLOSED on a write - so that's a success that means the HTTP/2 session
    // is setup.
    reason = NS_OK;
  }

  if (NS_FAILED(reason) || !mConnection) {
    LOG(("AltSvcTransaction::MaybeValidate %p Failed due to precondition",
         this));
    return false;
  }

  // insist on >= http/2
  HttpVersion version = mConnection->Version();
  LOG(("AltSvcTransaction::MaybeValidate() %p version %d\n", this,
       static_cast<int32_t>(version)));
  if ((!mIsHttp3 && (version != HttpVersion::v2_0)) ||
      (mIsHttp3 && (version != HttpVersion::v3_0))) {
    LOG(
        ("AltSvcTransaction::MaybeValidate %p Failed due to protocol version"
         " expacted %s.",
         this, mIsHttp3 ? "Http3" : "Http2"));
    return false;
  }

  nsCOMPtr<nsITLSSocketControl> socketControl;
  mConnection->GetTLSSocketControl(getter_AddRefs(socketControl));

  LOG(("AltSvcTransaction::MaybeValidate() %p socketControl=%p\n", this,
       socketControl.get()));

  if (socketControl->GetFailedVerification()) {
    LOG(
        ("AltSvcTransaction::MaybeValidate() %p "
         "not validated due to auth error",
         this));
    return false;
  }

  LOG(
      ("AltSvcTransaction::MaybeValidate() %p "
       "validating alternate service with successful auth check",
       this));

  return true;
}

void AltSvcTransaction::Close(nsresult reason) {
  LOG(("AltSvcTransaction::Close() %p reason=%" PRIx32, this,
       static_cast<uint32_t>(reason)));

  mValidatedResult = MaybeValidate(reason);
  mMapping->SetValidated(mValidatedResult);
  LOG(("AltSvcTransaction::Close() %p map %p validated %d [%s]", this,
       mMapping.get(), mMapping->Validated(), mMapping->HashKey().get()));
  if (!mValidatedResult && mConnection) {
    mConnection->DontReuse();
  }
  NullHttpTransaction::Close(reason);
}

nsresult AltSvcTransaction::ReadSegments(nsAHttpSegmentReader* reader,
                                         uint32_t count, uint32_t* countRead) {
  LOG(("AltSvcTransaction::ReadSegements() %p\n", this));
  mTriedToWrite = true;
  return NullHttpTransaction::ReadSegments(reader, count, countRead);
}

void AltSvcCache::EnsureStorageInited() {
  static Atomic<bool> initialized(false);

  if (initialized) {
    return;
  }

  auto initTask = [&]() {
    MOZ_ASSERT(NS_IsMainThread());

    // nsIDataStorage gives synchronous access to a memory based hash table
    // that is backed by disk where those writes are done asynchronously
    // on another thread
    nsCOMPtr<nsIDataStorageManager> dataStorageManager(
        do_GetService("@mozilla.org/security/datastoragemanager;1"));
    if (!dataStorageManager) {
      LOG(("AltSvcCache::EnsureStorageInited WARN NO STORAGE MANAGER\n"));
      return;
    }
    nsresult rv = dataStorageManager->Get(
        nsIDataStorageManager::AlternateServices, getter_AddRefs(mStorage));
    if (NS_FAILED(rv) || !mStorage) {
      LOG(("AltSvcCache::EnsureStorageInited WARN NO STORAGE\n"));
      return;
    }
    initialized = true;

    mStorageEpoch = NowInSeconds();
  };

  if (NS_IsMainThread()) {
    initTask();
    return;
  }

  nsCOMPtr<nsIEventTarget> main = GetMainThreadSerialEventTarget();
  if (!main) {
    return;
  }

  SyncRunnable::DispatchToThread(
      main,
      NS_NewRunnableFunction("AltSvcCache::EnsureStorageInited", initTask));
}

already_AddRefed<AltSvcMapping> AltSvcCache::LookupMapping(
    const nsCString& key, bool privateBrowsing) {
  LOG(("AltSvcCache::LookupMapping %p %s\n", this, key.get()));
  if (!mStorage) {
    LOG(("AltSvcCache::LookupMapping %p no backing store\n", this));
    return nullptr;
  }

  if (NS_IsMainThread()) {
    bool isReady;
    nsresult rv = mStorage->IsReady(&isReady);
    if (NS_FAILED(rv)) {
      LOG(("AltSvcCache::LookupMapping %p mStorage->IsReady failed\n", this));
      return nullptr;
    }
    if (!isReady) {
      LOG(("AltSvcCache::LookupMapping %p skip when storage is not ready\n",
           this));
      return nullptr;
    }
  }

  nsAutoCString val;
  nsresult rv =
      mStorage->Get(key,
                    privateBrowsing ? nsIDataStorage::DataType::Private
                                    : nsIDataStorage::DataType::Persistent,
                    val);
  if (NS_FAILED(rv) && rv != NS_ERROR_NOT_AVAILABLE) {
    LOG(("AltSvcCache::LookupMapping %p mStorage->Get failed \n", this));
    return nullptr;
  }
  if (rv == NS_ERROR_NOT_AVAILABLE || val.IsEmpty()) {
    LOG(("AltSvcCache::LookupMapping %p MISS\n", this));
    return nullptr;
  }
  RefPtr<AltSvcMapping> mapping =
      new AltSvcMapping(mStorage, mStorageEpoch, val);
  if (!mapping->Validated() && (mapping->StorageEpoch() != mStorageEpoch)) {
    // this was an in progress validation abandoned in a different session
    // rare edge case will not detect session change - that's ok as only impact
    // will be loss of alt-svc to this origin for this session.
    LOG(("AltSvcCache::LookupMapping %p invalid hit - MISS\n", this));
    (void)mStorage->Remove(key, mapping->Private()
                                    ? nsIDataStorage::DataType::Private
                                    : nsIDataStorage::DataType::Persistent);
    return nullptr;
  }

  if (mapping->IsHttp3() &&
      (!nsHttpHandler::IsHttp3Enabled() ||
       !gHttpHandler->IsHttp3VersionSupported(mapping->NPNToken()) ||
       gHttpHandler->IsHttp3Excluded(mapping->AlternateHost()))) {
    // If Http3 is disabled or the version not supported anymore, remove the
    // mapping.
    (void)mStorage->Remove(key, mapping->Private()
                                    ? nsIDataStorage::DataType::Private
                                    : nsIDataStorage::DataType::Persistent);
    return nullptr;
  }

  int32_t ttl = mapping->TTL();
  if (ttl <= 0) {
    LOG(("AltSvcCache::LookupMapping %p expired hit - MISS\n", this));
    if (mapping->IsHttp3() && mapping->NPNToken() == "h3"_ns) {
      glean::http::altsvc_h3_expired_staleness.AccumulateRawDuration(
          TimeDuration::FromSeconds(-ttl));
    }
    (void)mStorage->Remove(key, mapping->Private()
                                    ? nsIDataStorage::DataType::Private
                                    : nsIDataStorage::DataType::Persistent);
    return nullptr;
  }

  MOZ_ASSERT(mapping->Private() == privateBrowsing);
  LOG(("AltSvcCache::LookupMapping %p HIT %p\n", this, mapping.get()));
  return mapping.forget();
}

// For cases where the connection's hash key matches the hash key generated
// from the alt-svc header, validation is skipped since an equivalent connection
// already exists.
void AltSvcCache::UpdateAltServiceMappingWithoutValidation(
    AltSvcMapping* map, nsProxyInfo* pi, nsIInterfaceRequestor* aCallbacks,
    uint32_t caps, const OriginAttributes& originAttributes) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mStorage) {
    return;
  }
  RefPtr<AltSvcMapping> existing =
      LookupMapping(map->HashKey(), map->Private());
  LOG(
      ("AltSvcCache::UpdateAltServiceMappingWithoutValidation %p map %p "
       "existing %p %s",
       this, map, existing.get(), map->AlternateHost().get()));
  if (!existing) {
    map->SetValidated(true);
  }
}

void AltSvcCache::UpdateAltServiceMapping(
    AltSvcMapping* map, nsProxyInfo* pi, nsIInterfaceRequestor* aCallbacks,
    uint32_t caps, const OriginAttributes& originAttributes) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mStorage) {
    return;
  }
  RefPtr<AltSvcMapping> existing =
      LookupMapping(map->HashKey(), map->Private());
  LOG(
      ("AltSvcCache::UpdateAltServiceMapping %p map %p existing %p %s "
       "validated=%d",
       this, map, existing.get(), map->AlternateHost().get(),
       existing ? existing->Validated() : 0));

  if (existing && existing->Validated()) {
    if (existing->RouteEquals(map)) {
      MOZ_ASSERT(existing->HTTPS());
      LOG(
          ("AltSvcCache::UpdateAltServiceMapping %p map %p updates ttl of "
           "%p\n",
           this, map, existing.get()));
      existing->SetExpiresAt(map->GetExpiresAt());
      glean::http::altsvc_mapping_changed_target
          .EnumGet(glean::http::AltsvcMappingChangedTargetLabel::eFalse)
          .Add();
      return;
    }

    if (map->GetExpiresAt() < existing->GetExpiresAt()) {
      LOG(
          ("AltSvcCache::UpdateAltServiceMapping %p map %p ttl shorter than "
           "%p, ignoring",
           this, map, existing.get()));
      return;
    }

    // new alternate. start new validation
    LOG(("AltSvcCache::UpdateAltServiceMapping %p map %p may overwrite %p\n",
         this, map, existing.get()));
    glean::http::altsvc_mapping_changed_target
        .EnumGet(glean::http::AltsvcMappingChangedTargetLabel::eTrue)
        .Add();
  }

  if (existing && !existing->Validated()) {
    LOG(
        ("AltSvcCache::UpdateAltServiceMapping %p map %p ignored because %p "
         "still in progress\n",
         this, map, existing.get()));
    return;
  }

  if (map->IsHttp3()) {
    bool isProxyAllowed = pi ? (pi->IsDirect() || pi->IsHttp3Proxy()) : true;
    if (!isProxyAllowed) {
      LOG(
          ("AltSvcCache::UpdateAltServiceMapping %p map %p ignored h3 because "
           "proxy is in use %p\n",
           this, map, existing.get()));
      return;
    }
  }

  // start new validation, but don't overwrite a valid existing mapping unless
  // this completes successfully
  MOZ_ASSERT(!map->Validated());
  if (!existing) {
    map->Sync();
  } else {
    map->SetSyncOnlyOnSuccess(true);
  }

  RefPtr<nsHttpConnectionInfo> ci;
  map->GetConnectionInfo(getter_AddRefs(ci), pi, originAttributes);
  caps |= ci->GetAnonymous() ? NS_HTTP_LOAD_ANONYMOUS : 0;
  caps |= NS_HTTP_ERROR_SOFTLY;

  // Only enable Happy Eyeballs when there is no proxy (!pi), matching
  // AltSvcMapping::GetConnectionInfo and Http3FirstAltSvcMapping. Otherwise
  // validation would resolve the host client-side and leak it outside a SOCKS
  // remote-DNS proxy. TODO: handle this in the Happy Eyeballs code once it
  // supports establishing proxy connections.
  if (StaticPrefs::network_http_happy_eyeballs_enabled() && !pi) {
    // Validating an h3 alternate must establish an h3 connection; don't let
    // Happy Eyeballs race h1/h2 and settle on a non-h3 connection.
    ci = ci->Mutate()
             .SetHappyEyeballsEnabled(true)
             .SetHttp3Policy(map->IsHttp3() ? Http3Policy::Only
                                            : ci->GetHttp3Policy())
             .Finalize();
  }

  MOZ_ASSERT(map->HTTPS());
  LOG(
      ("AltSvcCache::UpdateAltServiceMapping %p validation via "
       "speculative connect started http3=%d\n",
       this, map->IsHttp3()));
  nsCOMPtr<nsIInterfaceRequestor> callbacks = new AltSvcOverride(aCallbacks);
  RefPtr<SpeculativeTransaction> transaction =
      new AltSvcTransaction(ci, aCallbacks, caps, map);

  nsresult rv =
      gHttpHandler->SpeculativeConnect(ci, callbacks, caps, transaction);
  if (NS_FAILED(rv)) {
    LOG(
        ("AltSvcCache::UpdateAltServiceMapping %p "
         "speculative connect failed with code %08x\n",
         this, static_cast<uint32_t>(rv)));
  }
}

already_AddRefed<AltSvcMapping> AltSvcCache::GetAltServiceMapping(
    const nsACString& scheme, const nsACString& host, int32_t port,
    bool privateBrowsing, const OriginAttributes& originAttributes,
    bool aHttp2Allowed, bool aHttp3Allowed, bool aForceHttp3First) {
  EnsureStorageInited();

  bool isHTTPS;
  if (NS_FAILED(SchemeIsHTTPS(scheme, isHTTPS))) {
    return nullptr;
  }
  if (!gHttpHandler->AllowAltSvc()) {
    return nullptr;
  }
  if (!isHTTPS) {
    return nullptr;
  }

  // First look for HTTP3
  if (aHttp3Allowed) {
    if (aForceHttp3First) {
      RefPtr<Http3FirstAltSvcMapping> mapping = new Http3FirstAltSvcMapping(
          "https"_ns, host, port, ""_ns, privateBrowsing, host, port, "h3"_ns,
          originAttributes, true, SupportedAlpnRank::HTTP_3_VER_1);
      return mapping.forget();
    }
    nsAutoCString key;
    AltSvcMapping::MakeHashKey(key, scheme, host, port, privateBrowsing,
                               originAttributes, true);
    RefPtr<AltSvcMapping> existing = LookupMapping(key, privateBrowsing);
    LOG(
        ("AltSvcCache::GetAltServiceMapping %p key=%s "
         "existing=%p validated=%d ttl=%d",
         this, key.get(), existing.get(), existing ? existing->Validated() : 0,
         existing ? existing->TTL() : 0));
    if (existing && existing->Validated()) {
      return existing.forget();
    }
  }

  // Now look for HTTP2.
  if (aHttp2Allowed) {
    nsAutoCString key;
    AltSvcMapping::MakeHashKey(key, scheme, host, port, privateBrowsing,
                               originAttributes, false);
    RefPtr<AltSvcMapping> existing = LookupMapping(key, privateBrowsing);
    LOG(
        ("AltSvcCache::GetAltServiceMapping %p key=%s "
         "existing=%p validated=%d ttl=%d",
         this, key.get(), existing.get(), existing ? existing->Validated() : 0,
         existing ? existing->TTL() : 0));
    if (existing && existing->Validated()) {
      return existing.forget();
    }
  }

  return nullptr;
}

class ProxyClearHostMapping : public Runnable {
 public:
  explicit ProxyClearHostMapping(const nsACString& host, int32_t port,
                                 const OriginAttributes& originAttributes)
      : Runnable("net::ProxyClearHostMapping"),
        mHost(host),
        mPort(port),
        mOriginAttributes(originAttributes) {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread());
    gHttpHandler->AltServiceCache()->ClearHostMapping(mHost, mPort,
                                                      mOriginAttributes);
    return NS_OK;
  }

 private:
  nsCString mHost;
  int32_t mPort;
  OriginAttributes mOriginAttributes;
};

void AltSvcCache::ClearHostMapping(const nsACString& host, int32_t port,
                                   const OriginAttributes& originAttributes) {
  MOZ_ASSERT(XRE_IsParentProcess());

  if (!NS_IsMainThread()) {
    nsCOMPtr<nsIRunnable> event =
        new ProxyClearHostMapping(host, port, originAttributes);
    if (event) {
      NS_DispatchToMainThread(event);
    }
    return;
  }
  nsAutoCString key;
  for (int secure = 0; secure < 2; ++secure) {
    constexpr auto http = "http"_ns;
    constexpr auto https = "https"_ns;
    const nsLiteralCString& scheme = secure ? https : http;
    for (int pb = 1; pb >= 0; --pb) {
      AltSvcMapping::MakeHashKey(key, scheme, host, port, bool(pb),
                                 originAttributes, false);
      RefPtr<AltSvcMapping> existing = LookupMapping(key, bool(pb));
      if (existing) {
        existing->SetExpired();
      }
      AltSvcMapping::MakeHashKey(key, scheme, host, port, bool(pb),
                                 originAttributes, true);
      existing = LookupMapping(key, bool(pb));
      if (existing) {
        existing->SetExpired();
      }
    }
  }
}

void AltSvcCache::ClearHostMapping(nsHttpConnectionInfo* ci) {
  if (!ci->GetOrigin().IsEmpty()) {
    ClearHostMapping(ci->GetOrigin(), ci->OriginPort(),
                     ci->GetOriginAttributes());
  }
}

void AltSvcCache::ClearAltServiceMappings() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mStorage) {
    (void)mStorage->Clear();
  }
}

void AltSvcCache::ClearAltServiceMappingsMatchingOA(
    const OriginAttributes& aOriginAttributes) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mStorage) {
    return;
  }
  nsTArray<RefPtr<nsIDataStorageItem>> items;
  if (NS_FAILED(mStorage->GetAll(items))) {
    return;
  }

  for (const auto& item : items) {
    nsAutoCString key;
    nsAutoCString val;
    if (NS_FAILED(item->GetKey(key)) || NS_FAILED(item->GetValue(val))) {
      continue;
    }
    RefPtr<AltSvcMapping> mapping =
        new AltSvcMapping(mStorage, mStorageEpoch, val);

    if (mapping->GetOriginAttributes() == aOriginAttributes) {
      (void)mStorage->Remove(key, mapping->Private()
                                      ? nsIDataStorage::DataType::Private
                                      : nsIDataStorage::DataType::Persistent);
    }
  }
}

nsresult AltSvcCache::GetAltSvcCacheKeys(nsTArray<nsCString>& value) {
  MOZ_ASSERT(NS_IsMainThread());
  if (gHttpHandler->AllowAltSvc() && mStorage) {
    nsTArray<RefPtr<nsIDataStorageItem>> items;
    nsresult rv = mStorage->GetAll(items);
    if (NS_FAILED(rv)) {
      return rv;
    }

    for (const auto& item : items) {
      nsAutoCString key;
      rv = item->GetKey(key);
      if (NS_FAILED(rv)) {
        return rv;
      }
      value.AppendElement(key);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
AltSvcOverride::GetInterface(const nsIID& iid, void** result) {
  if (NS_SUCCEEDED(QueryInterface(iid, result)) && *result) {
    return NS_OK;
  }

  if (mCallbacks) {
    return mCallbacks->GetInterface(iid, result);
  }

  return NS_ERROR_NO_INTERFACE;
}

NS_IMETHODIMP
AltSvcOverride::GetIgnoreIdle(bool* ignoreIdle) {
  *ignoreIdle = true;
  return NS_OK;
}

NS_IMETHODIMP
AltSvcOverride::GetParallelSpeculativeConnectLimit(
    uint32_t* parallelSpeculativeConnectLimit) {
  *parallelSpeculativeConnectLimit = 32;
  return NS_OK;
}

NS_IMETHODIMP
AltSvcOverride::GetAllow1918(bool* allow) {
  // normally we don't do speculative connects to 1918.. and we use
  // speculative connects for the mapping validation, so override
  // that default here for alt-svc
  *allow = true;
  return NS_OK;
}

NS_IMPL_ISUPPORTS(AltSvcOverride, nsIInterfaceRequestor,
                  nsISpeculativeConnectionOverrider)

Http3FirstAltSvcMapping::Http3FirstAltSvcMapping(
    const nsACString& originScheme, const nsACString& originHost,
    int32_t originPort, const nsACString& username, bool privateBrowsing,
    const nsACString& alternateHost, int32_t alternatePort,
    const nsACString& npnToken, const OriginAttributes& originAttributes,
    bool aIsHttp3, SupportedAlpnRank aRank)
    : AltSvcMapping(originScheme, originHost, originPort, username,
                    privateBrowsing, alternateHost, alternatePort, npnToken,
                    originAttributes, aIsHttp3, aRank) {}

Http3FirstAltSvcMapping::~Http3FirstAltSvcMapping() = default;

void Http3FirstAltSvcMapping::GetConnectionInfo(
    nsHttpConnectionInfo** outCI, nsProxyInfo* pi,
    const OriginAttributes& originAttributes) {
  RefPtr<nsHttpConnectionInfo> ci = new nsHttpConnectionInfo(
      mOriginHost, mOriginPort, mNPNToken, mUsername, pi, originAttributes,
      mAlternateHost, mAlternatePort, mIsHttp3, false);
  ci = ci->Mutate()
           .SetHappyEyeballsEnabled(
               StaticPrefs::network_http_happy_eyeballs_enabled() && !pi)
           .SetPrivate(mPrivate)
           .Finalize();
  ci.forget(outCI);
}

}  // namespace net
}  // namespace mozilla
