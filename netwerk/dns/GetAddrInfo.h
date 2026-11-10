/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef netwerk_dns_GetAddrInfo_h
#define netwerk_dns_GetAddrInfo_h

#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/RWLock.h"
#include "mozilla/net/DNS.h"
#include "nsError.h"
#include "nsHashKeys.h"
#include "nsIDNSByTypeRecord.h"
#include "nsIDNSService.h"
#include "nsINativeDNSResolverOverride.h"
#include "nsTArray.h"
#include "nsTHashMap.h"
#include "nscore.h"
#include "prio.h"

#if defined(XP_WIN)
#  define DNSQUERY_AVAILABLE 1
struct _DnsRecordA;
#else
#  undef DNSQUERY_AVAILABLE
#endif

#ifdef HAVE_RES_NINIT
struct __res_state;
#endif

namespace mozilla {
namespace net {

extern LazyLogModule gGetAddrInfoLog;
class AddrInfo;
class DNSPacket;

/**
 * Look up a host by name. Mostly equivalent to getaddrinfo(host, NULL, ...) of
 * RFC 3493.
 *
 * @param aHost[in] Character string defining the host name of interest
 * @param aAddressFamily[in] May be AF_INET, AF_INET6, or AF_UNSPEC.
 * @param aFlags[in] See nsIDNSService::DNSFlags
 * @param aAddrInfo[out] Will point to the results of the host lookup, or be
 *     null if the lookup failed.
 * @param aGetTtl[in] If true, the TTL will be retrieved if DNS provides the
 *     answers..
 */
nsresult GetAddrInfo(const nsACString& aHost, uint16_t aAddressFamily,
                     nsIDNSService::DNSFlags aFlags, AddrInfo** aAddrInfo,
                     bool aGetTtl);

/**
 * Initialize the GetAddrInfo module.
 *
 * GetAddrInfoShutdown() should be called for every time this function is
 * called.
 */
nsresult GetAddrInfoInit();

/**
 * Shutdown the GetAddrInfo module.
 *
 * This function should be called for every time GetAddrInfoInit() is called.
 * An assertion may throw (but is not guarenteed) if this function is called
 * too many times.
 */
nsresult GetAddrInfoShutdown();

void DNSThreadShutdown();

/**
 * Called when the network configuration may have changed, so that per-thread
 * native resolver state gets reinitialized before its next use.
 */
void DNSNetworkChanged();
uint32_t DNSNetworkGeneration();

#ifdef HAVE_RES_NINIT
/**
 * Returns the resolver state of the calling thread, reinitializing it if the
 * network changed since it was last initialized. Returns null on failure.
 */
struct __res_state* GetThreadResState();
#endif

/**
 * Resolves a HTTPS record. Will check overrides before calling the
 * native OS implementation.
 *
 * aOriginHost is set when aHost is a port-prefixed name; it is then used as
 * the TargetName of ServiceMode records whose TargetName is ".".
 */
nsresult ResolveHTTPSRecord(const nsACString& aHost,
                            nsIDNSService::DNSFlags aFlags,
                            TypeRecordResultType& aResult, uint32_t& aTTL,
                            const Maybe<nsCString>& aOriginHost = Nothing());

/**
 * A target name that could not be resolved within the same response, along with
 * whether it was reached through an HTTPS AliasMode (SvcPriority 0) record.
 * Only an AliasMode TargetName has to be chased by the client (RFC 9460); a
 * plain CNAME target is already resolved by the recursive resolver, so a target
 * without an HTTPS record means there is no HTTPS RR at all.
 */
struct HTTPSAliasTarget {
  nsCString mName;
  bool mFromAliasMode = false;
};

/**
 * The platform specific implementation of HTTPS resolution.
 *
 * If the record for aHost could not be resolved within the same response,
 * aAlias is set to the target name so the caller can issue a fresh lookup.
 */
nsresult ResolveHTTPSRecordImpl(const nsACString& aHost,
                                const Maybe<nsCString>& aOriginHost,
                                nsIDNSService::DNSFlags aFlags,
                                TypeRecordResultType& aResult, uint32_t& aTTL,
                                HTTPSAliasTarget& aAlias);

nsresult ParseHTTPSRecord(nsCString& aHost, DNSPacket& aDNSPacket,
                          TypeRecordResultType& aResult, uint32_t& aTTL,
                          HTTPSAliasTarget& aAlias);

#ifdef DNSQUERY_AVAILABLE
/**
 * Extracts the HTTPS records for aHost from the record list returned by
 * DnsQuery_A, following CNAME and AliasMode targets present in the list.
 */
nsresult ParseHTTPSRecordList(const _DnsRecordA* aRecords,
                              const nsACString& aHost,
                              const Maybe<nsCString>& aOriginHost,
                              TypeRecordResultType& aResult, uint32_t& aTTL,
                              HTTPSAliasTarget& aAlias);
#endif

// Use the provided aHost to create a mock HTTPS record.
nsresult CreateAndResolveMockHTTPSRecord(const nsACString& aHost,
                                         nsIDNSService::DNSFlags aFlags,
                                         TypeRecordResultType& aResult,
                                         uint32_t& aTTL);

class NativeDNSResolverOverride : public nsINativeDNSResolverOverride {
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSINATIVEDNSRESOLVEROVERRIDE
 public:
  NativeDNSResolverOverride() = default;

  static already_AddRefed<nsINativeDNSResolverOverride> GetSingleton();

 private:
  virtual ~NativeDNSResolverOverride() = default;
  mozilla::RWLock mLock{"NativeDNSResolverOverride"};

  nsTHashMap<nsCStringHashKey, nsTArray<NetAddr>> mOverrides
      MOZ_GUARDED_BY(mLock);
  nsTHashMap<nsCStringHashKey, nsCString> mCnames MOZ_GUARDED_BY(mLock);
  nsTHashMap<nsCStringHashKey, nsTArray<uint8_t>> mHTTPSRecordOverrides
      MOZ_GUARDED_BY(mLock);

  friend bool FindAddrOverride(const nsACString& aHost, uint16_t aAddressFamily,
                               nsIDNSService::DNSFlags aFlags,
                               AddrInfo** aAddrInfo);
  friend bool FindHTTPSRecordOverride(const nsACString& aHost,
                                      const Maybe<nsCString>& aOriginHost,
                                      TypeRecordResultType& aResult,
                                      HTTPSAliasTarget& aAlias);
};

}  // namespace net
}  // namespace mozilla

#endif  // netwerk_dns_GetAddrInfo_h
