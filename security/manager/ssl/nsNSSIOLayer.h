/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsNSSIOLayer_h
#define nsNSSIOLayer_h

#include "mozilla/Assertions.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/psm/PSMIPCUtils.h"
#include "nsCOMPtr.h"
#include "nsIObserver.h"
#include "nsIProxyInfo.h"
#include "nsITLSSocketControl.h"
#include "nsITlsHandshakeListener.h"
#include "nsNSSCertificate.h"
#include "nsTHashMap.h"
#include "nsTHashtable.h"
#include "sslt.h"

namespace mozilla {
class OriginAttributes;
}  // namespace mozilla

using mozilla::OriginAttributes;

// Order matters for UpdateEchExtensioNStatus.
enum class EchExtensionStatus {
  kNotPresent,  // No ECH Extension was sent
  kGREASE,      // A GREASE ECH Extension was sent
  kReal         // A 'real' ECH Extension was sent
};

enum class PublicOrPrivate { Public, Private };

class nsSSLIOLayerHelpers : public nsIObserver {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIOBSERVER

  explicit nsSSLIOLayerHelpers(PublicOrPrivate aPublicOrPrivate,
                               uint32_t aTlsFlags = 0);

  static void GlobalInit();
  static void GlobalCleanup();

  nsresult Init();

  static bool nsSSLIOLayerInitialized;
  static PRDescIdentity nsSSLIOLayerIdentity;
  static PRDescIdentity nsSSLPlaintextLayerIdentity;
  static PRIOMethods nsSSLIOLayerMethods;
  static PRIOMethods nsSSLPlaintextLayerMethods;

 protected:
  virtual ~nsSSLIOLayerHelpers();

 private:
  struct IntoleranceEntry {
    uint16_t tolerant;
    uint16_t intolerant;
    PRErrorCode intoleranceReason;

    void AssertInvariant() const {
      MOZ_ASSERT(intolerant == 0 || tolerant < intolerant);
    }
  };
  nsTHashMap<nsCStringHashKey, IntoleranceEntry> mTLSIntoleranceInfo;
  // Sites that require insecure fallback to TLS 1.0, set by the pref
  // security.tls.insecure_fallback_hosts, which is a comma-delimited
  // list of domain names.
  nsTHashtable<nsCStringHashKey> mInsecureFallbackSites;

 public:
  void rememberTolerantAtVersion(const nsACString& hostname, uint16_t port,
                                 uint16_t tolerant);
  bool fallbackLimitReached(const nsACString& hostname, uint16_t intolerant);
  bool rememberIntolerantAtVersion(const nsACString& hostname, uint16_t port,
                                   uint16_t intolerant, uint16_t minVersion,
                                   PRErrorCode intoleranceReason);
  void forgetIntolerance(const nsACString& hostname, uint16_t port);
  void adjustForTLSIntolerance(const nsACString& hostname, uint16_t port,
                               /*in/out*/ SSLVersionRange& range);
  PRErrorCode getIntoleranceReason(const nsACString& hostname, uint16_t port);

  void clearStoredData();
  void loadVersionFallbackLimit();
  void setInsecureFallbackSites(const nsCString& str);
  void initInsecureFallbackSites();
  bool isPublic() const;
  void removeInsecureFallbackSite(const nsACString& hostname, uint16_t port);
  bool isInsecureFallbackSite(const nsACString& hostname);

  uint16_t mVersionFallbackLimit;

 private:
  const PublicOrPrivate mPublicOrPrivate;

  mozilla::Mutex mutex MOZ_UNANNOTATED;
  uint32_t mTlsFlags;
#ifdef DEBUG
  bool mRegisteredPrefObservers = false;
#endif
};

namespace {
static mozilla::StaticRefPtr<nsSSLIOLayerHelpers> gPublicSSLIOLayerHelpers;
static mozilla::StaticRefPtr<nsSSLIOLayerHelpers> gPrivateSSLIOLayerHelpers;
}  // namespace

already_AddRefed<nsSSLIOLayerHelpers> PublicSSLIOLayerHelpers();
already_AddRefed<nsSSLIOLayerHelpers> PrivateSSLIOLayerHelpers();

nsresult nsSSLIOLayerNewSocket(int32_t family, const char* host, int32_t port,
                               nsIProxyInfo* proxy,
                               const OriginAttributes& originAttributes,
                               PRFileDesc** fd,
                               nsITLSSocketControl** tlsSocketControl,
                               bool forSTARTTLS, uint32_t flags,
                               uint32_t tlsFlags);

nsresult nsSSLIOLayerAddToSocket(int32_t family, const char* host, int32_t port,
                                 nsIProxyInfo* proxy,
                                 const OriginAttributes& originAttributes,
                                 PRFileDesc* fd,
                                 nsITLSSocketControl** tlsSocketControl,
                                 bool forSTARTTLS, uint32_t flags,
                                 uint32_t tlsFlags);

SECStatus abridgedCertificatePass1Decode(const SECItem* input,
                                         unsigned char* output,
                                         size_t outputLen, size_t* usedLen);
SECStatus abridgedCertificateDecode(const SECItem* input, unsigned char* output,
                                    size_t outputLen, size_t* usedLen);

SECStatus zlibCertificateDecode(const SECItem* input, unsigned char* output,
                                size_t outputLen, size_t* usedLen);

SECStatus brotliCertificateDecode(const SECItem* input, unsigned char* output,
                                  size_t outputLen, size_t* usedLen);

SECStatus zstdCertificateDecode(const SECItem* input, unsigned char* output,
                                size_t outputLen, size_t* usedLen);

#if !defined(MOZ_NO_SMART_CARDS)
void CacheRemoteCryptokiObjects(mozilla::psm::SearchingFor searchingFor);
#endif  // !MOZ_NO_SMART_CARDS

#endif  // nsNSSIOLayer_h
