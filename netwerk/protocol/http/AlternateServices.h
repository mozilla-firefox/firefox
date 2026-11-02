/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
Alt-Svc allows separation of transport routing from the origin host without
using a proxy. See https://httpwg.github.io/http-extensions/alt-svc.html and
https://tools.ietf.org/html/draft-ietf-httpbis-alt-svc-06

 Nice To Have Future Enhancements::
 * flush on network change event when we have an indicator
 * use established https channel for http instead separate of conninfo hash
 * pin via http-tls header
 * clear based on origin when a random fail happens not just 421
 * upon establishment of channel, cancel and retry trans that have not yet
     written anything
 * persistent storage (including private browsing filter)
 * memory reporter for cache, but this is rather tiny
*/

#ifndef mozilla_net_AlternateServices_h
#define mozilla_net_AlternateServices_h

#include "SpeculativeTransaction.h"
#include "mozilla/BasePrincipal.h"
#include "nsHttp.h"
#include "nsIDataStorage.h"
#include "nsIInterfaceRequestor.h"
#include "nsISpeculativeConnect.h"
#include "nsRefPtrHashtable.h"
#include "nsString.h"

class nsILoadInfo;

namespace mozilla {
namespace net {

class nsProxyInfo;
class nsHttpConnectionInfo;
class nsHttpTransaction;

class AltSvcMapping {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AltSvcMapping)

 private:  // ctor from ProcessHeader
  AltSvcMapping(nsIDataStorage* storage, int32_t storageEpoch,
                const nsACString& originScheme, const nsACString& originHost,
                int32_t originPort, const nsACString& username,
                bool privateBrowsing, uint32_t expiresAt,
                const nsACString& alternateHost, int32_t alternatePort,
                const nsACString& npnToken,
                const OriginAttributes& originAttributes, bool aIsHttp3,
                SupportedAlpnRank aRank);

 public:
  AltSvcMapping(nsIDataStorage* storage, int32_t storageEpoch,
                const nsCString& str);

  static void ProcessHeader(
      const nsCString& buf, const nsCString& originScheme,
      const nsCString& originHost, int32_t originPort,
      const nsACString& username, bool privateBrowsing,
      nsIInterfaceRequestor* callbacks, nsProxyInfo* proxyInfo, uint32_t caps,
      const OriginAttributes& originAttributes,
      nsHttpConnectionInfo* aTransConnInfo,
      bool aDontValidate = false);  // aDontValidate is only used for testing!

  // AcceptableProxy() decides whether a particular proxy configuration (pi) is
  // suitable for use with Alt-Svc. No proxy (including a null pi) is suitable.
  static bool AcceptableProxy(nsProxyInfo* proxyInfo);

  const nsCString& AlternateHost() const { return mAlternateHost; }
  const nsCString& OriginHost() const { return mOriginHost; }
  uint32_t OriginPort() const { return mOriginPort; }
  const nsCString& HashKey() const { return mHashKey; }
  uint32_t AlternatePort() const { return mAlternatePort; }
  bool Validated() { return mValidated; }
  int32_t GetExpiresAt() { return mExpiresAt; }
  bool RouteEquals(AltSvcMapping* map);
  bool HTTPS() { return mHttps; }

  virtual void GetConnectionInfo(nsHttpConnectionInfo** outCI, nsProxyInfo* pi,
                                 const OriginAttributes& originAttributes);

  int32_t TTL();
  int32_t StorageEpoch() { return mStorageEpoch; }
  bool Private() { return mPrivate; }

  void SetValidated(bool val);
  void SetMixedScheme(bool val);
  void SetExpiresAt(int32_t val);
  void SetExpired();
  void Sync();
  void SetSyncOnlyOnSuccess(bool aSOOS) { mSyncOnlyOnSuccess = aSOOS; }

  static void MakeHashKey(nsCString& outKey, const nsACString& originScheme,
                          const nsACString& originHost, int32_t originPort,
                          bool privateBrowsing,
                          const OriginAttributes& originAttributes,
                          bool aHttp3);

  bool IsHttp3() { return mIsHttp3; }
  const nsCString& NPNToken() const { return mNPNToken; }
  const OriginAttributes& GetOriginAttributes() const {
    return mOriginAttributes;
  }
  SupportedAlpnRank AlpnRank() const { return mAlpnRank; }

 protected:
  // Used by Http3FirstAltSvcMapping
  AltSvcMapping(const nsACString& originScheme, const nsACString& originHost,
                int32_t originPort, const nsACString& username,
                bool privateBrowsing, const nsACString& alternateHost,
                int32_t alternatePort, const nsACString& npnToken,
                const OriginAttributes& originAttributes, bool aIsHttp3,
                SupportedAlpnRank aRank);
  virtual ~AltSvcMapping() = default;
  void SyncString(const nsCString& str);
  nsCOMPtr<nsIDataStorage> mStorage;
  int32_t mStorageEpoch = 0;
  void Serialize(nsCString& out);

  nsCString mHashKey;

  // If you change any of these members, update Serialize()
  nsCString mAlternateHost;
  int32_t mAlternatePort{-1};

  nsCString mOriginHost;
  int32_t mOriginPort{-1};

  nsCString mUsername;
  bool mPrivate{false};

  // alt-svc mappping
  uint32_t mExpiresAt{0};

  bool mValidated{false};
  // origin is https://
  MOZ_INIT_OUTSIDE_CTOR bool mHttps{false};
  // .wk allows http and https on same con
  MOZ_INIT_OUTSIDE_CTOR bool mMixedScheme{false};

  nsCString mNPNToken;

  OriginAttributes mOriginAttributes;

  bool mSyncOnlyOnSuccess{false};
  bool mIsHttp3{false};
  SupportedAlpnRank mAlpnRank{SupportedAlpnRank::NOT_SUPPORTED};
};

class Http3FirstAltSvcMapping : public AltSvcMapping {
 public:
  Http3FirstAltSvcMapping(const nsACString& originScheme,
                          const nsACString& originHost, int32_t originPort,
                          const nsACString& username, bool privateBrowsing,
                          const nsACString& alternateHost,
                          int32_t alternatePort, const nsACString& npnToken,
                          const OriginAttributes& originAttributes,
                          bool aIsHttp3, SupportedAlpnRank aRank);
  void GetConnectionInfo(nsHttpConnectionInfo** outCI, nsProxyInfo* pi,
                         const OriginAttributes& originAttributes) override;

 private:
  ~Http3FirstAltSvcMapping();
};

class AltSvcOverride : public nsIInterfaceRequestor,
                       public nsISpeculativeConnectionOverrider {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSISPECULATIVECONNECTIONOVERRIDER
  NS_DECL_NSIINTERFACEREQUESTOR

  explicit AltSvcOverride(nsIInterfaceRequestor* aRequestor)
      : mCallbacks(aRequestor) {}

 private:
  virtual ~AltSvcOverride() = default;
  nsCOMPtr<nsIInterfaceRequestor> mCallbacks;
};

class AltSvcCache {
 public:
  AltSvcCache() = default;
  virtual ~AltSvcCache() = default;
  void UpdateAltServiceMapping(
      AltSvcMapping* map, nsProxyInfo* pi, nsIInterfaceRequestor*,
      uint32_t caps,
      const OriginAttributes& originAttributes);  // main thread
  void UpdateAltServiceMappingWithoutValidation(
      AltSvcMapping* map, nsProxyInfo* pi, nsIInterfaceRequestor*,
      uint32_t caps,
      const OriginAttributes& originAttributes);  // main thread
  already_AddRefed<AltSvcMapping> GetAltServiceMapping(
      const nsACString& scheme, const nsACString& host, int32_t port,
      bool privateBrowsing, const OriginAttributes& originAttributes,
      bool aHttp2Allowed, bool aHttp3Allowed, bool aForceHttp3First = false);
  void ClearAltServiceMappings();
  // Removes all Alt-Svc mappings whose origin attributes match
  // aOriginAttributes.
  void ClearAltServiceMappingsMatchingOA(
      const OriginAttributes& aOriginAttributes);
  void ClearHostMapping(const nsACString& host, int32_t port,
                        const OriginAttributes& originAttributes);
  void ClearHostMapping(nsHttpConnectionInfo* ci);
  nsIDataStorage* GetStoragePtr() { return mStorage.get(); }
  int32_t StorageEpoch() { return mStorageEpoch; }
  nsresult GetAltSvcCacheKeys(nsTArray<nsCString>& value);

 private:
  void EnsureStorageInited();
  already_AddRefed<AltSvcMapping> LookupMapping(const nsCString& key,
                                                bool privateBrowsing);
  nsCOMPtr<nsIDataStorage> mStorage;
  int32_t mStorageEpoch{0};
};

// This is the asynchronous null transaction used to validate
// an alt-svc advertisement only for https://
class AltSvcTransaction final : public SpeculativeTransaction {
 public:
  AltSvcTransaction(nsHttpConnectionInfo* ci, nsIInterfaceRequestor* callbacks,
                    uint32_t caps, AltSvcMapping* aMap);

  ~AltSvcTransaction() override;

  // AltSvcTransaction is used to validate the alt-svc record, so we don't want
  // to fetch HTTPS RR for this.
  virtual nsresult FetchHTTPSRR() override { return NS_ERROR_NOT_IMPLEMENTED; }

 private:
  // check on alternate route.
  // also evaluate 'reasonable assurances' for opportunistic security
  bool MaybeValidate(nsresult reason);

 public:
  void Close(nsresult reason) override;
  nsresult ReadSegments(nsAHttpSegmentReader* reader, uint32_t count,
                        uint32_t* countRead) override;

 private:
  RefPtr<AltSvcMapping> mMapping;
  uint32_t mIsHttp3 : 1;
  uint32_t mTriedToValidate : 1;
  uint32_t mTriedToWrite : 1;
  uint32_t mValidatedResult : 1;
};

}  // namespace net
}  // namespace mozilla

#endif  // include guard
