/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTTPSSVC.h"

#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "mozilla/net/DNS.h"
#include "nsHttp.h"
#include "nsHttpHandler.h"
#include "nsIDNSService.h"
#include "nsNetAddr.h"
#include "nsNetUtil.h"

namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS(SVCBRecord, nsISVCBRecord)

class SvcParam : public nsISVCParam,
                 public nsISVCParamAlpn,
                 public nsISVCParamNoDefaultAlpn,
                 public nsISVCParamPort,
                 public nsISVCParamIPv4Hint,
                 public nsISVCParamEchConfig,
                 public nsISVCParamIPv6Hint,
                 public nsISVCParamODoHConfig {
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSISVCPARAM
  NS_DECL_NSISVCPARAMALPN
  NS_DECL_NSISVCPARAMNODEFAULTALPN
  NS_DECL_NSISVCPARAMPORT
  NS_DECL_NSISVCPARAMIPV4HINT
  NS_DECL_NSISVCPARAMECHCONFIG
  NS_DECL_NSISVCPARAMIPV6HINT
  NS_DECL_NSISVCPARAMODOHCONFIG
 public:
  explicit SvcParam(const SvcParamType& value) : mValue(value) {};

 private:
  virtual ~SvcParam() = default;
  SvcParamType mValue;
};

NS_IMPL_ADDREF(SvcParam)
NS_IMPL_RELEASE(SvcParam)
NS_INTERFACE_MAP_BEGIN(SvcParam)
  NS_INTERFACE_MAP_ENTRY(nsISVCParam)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsISVCParam)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsISVCParamAlpn, mValue.is<SvcParamAlpn>())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsISVCParamNoDefaultAlpn,
                                     mValue.is<SvcParamNoDefaultAlpn>())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsISVCParamPort, mValue.is<SvcParamPort>())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsISVCParamIPv4Hint,
                                     mValue.is<SvcParamIpv4Hint>())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsISVCParamEchConfig,
                                     mValue.is<SvcParamEchConfig>())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsISVCParamIPv6Hint,
                                     mValue.is<SvcParamIpv6Hint>())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsISVCParamODoHConfig,
                                     mValue.is<SvcParamODoHConfig>())
NS_INTERFACE_MAP_END

NS_IMETHODIMP
SvcParam::GetType(uint16_t* aType) {
  *aType = mValue.match(
      [](Nothing&) { return SvcParamKeyMandatory; },
      [](SvcParamAlpn&) { return SvcParamKeyAlpn; },
      [](SvcParamNoDefaultAlpn&) { return SvcParamKeyNoDefaultAlpn; },
      [](SvcParamPort&) { return SvcParamKeyPort; },
      [](SvcParamIpv4Hint&) { return SvcParamKeyIpv4Hint; },
      [](SvcParamEchConfig&) { return SvcParamKeyEchConfig; },
      [](SvcParamIpv6Hint&) { return SvcParamKeyIpv6Hint; },
      [](SvcParamODoHConfig&) { return SvcParamKeyODoHConfig; });
  return NS_OK;
}

NS_IMETHODIMP
SvcParam::GetAlpn(nsTArray<nsCString>& aAlpn) {
  if (!mValue.is<SvcParamAlpn>()) {
    MOZ_ASSERT(false, "Unexpected type for variant");
    return NS_ERROR_NOT_AVAILABLE;
  }
  aAlpn.AppendElements(mValue.as<SvcParamAlpn>().mValue);
  return NS_OK;
}

NS_IMETHODIMP
SvcParam::GetPort(uint16_t* aPort) {
  if (!mValue.is<SvcParamPort>()) {
    MOZ_ASSERT(false, "Unexpected type for variant");
    return NS_ERROR_NOT_AVAILABLE;
  }
  *aPort = mValue.as<SvcParamPort>().mValue;
  return NS_OK;
}

NS_IMETHODIMP
SvcParam::GetEchconfig(nsACString& aEchConfig) {
  if (!mValue.is<SvcParamEchConfig>()) {
    MOZ_ASSERT(false, "Unexpected type for variant");
    return NS_ERROR_NOT_AVAILABLE;
  }
  aEchConfig = mValue.as<SvcParamEchConfig>().mValue;
  return NS_OK;
}

NS_IMETHODIMP
SvcParam::GetIpv4Hint(nsTArray<RefPtr<nsINetAddr>>& aIpv4Hint) {
  if (!mValue.is<SvcParamIpv4Hint>()) {
    MOZ_ASSERT(false, "Unexpected type for variant");
    return NS_ERROR_NOT_AVAILABLE;
  }
  const auto& results = mValue.as<SvcParamIpv4Hint>().mValue;
  for (const auto& ip : results) {
    if (ip.raw.family != AF_INET) {
      return NS_ERROR_UNEXPECTED;
    }
    RefPtr<nsINetAddr> hint = new nsNetAddr(&ip);
    aIpv4Hint.AppendElement(hint);
  }

  return NS_OK;
}

NS_IMETHODIMP
SvcParam::GetIpv6Hint(nsTArray<RefPtr<nsINetAddr>>& aIpv6Hint) {
  if (!mValue.is<SvcParamIpv6Hint>()) {
    MOZ_ASSERT(false, "Unexpected type for variant");
    return NS_ERROR_NOT_AVAILABLE;
  }
  const auto& results = mValue.as<SvcParamIpv6Hint>().mValue;
  for (const auto& ip : results) {
    if (ip.raw.family != AF_INET6) {
      return NS_ERROR_UNEXPECTED;
    }
    RefPtr<nsINetAddr> hint = new nsNetAddr(&ip);
    aIpv6Hint.AppendElement(hint);
  }
  return NS_OK;
}

NS_IMETHODIMP
SvcParam::GetODoHConfig(nsACString& aODoHConfig) {
  if (!mValue.is<SvcParamODoHConfig>()) {
    MOZ_ASSERT(false, "Unexpected type for variant");
    return NS_ERROR_NOT_AVAILABLE;
  }
  aODoHConfig = mValue.as<SvcParamODoHConfig>().mValue;
  return NS_OK;
}

bool SVCB::operator<(const SVCB& aOther) const {
  if (nsHttpHandler::EchConfigEnabled()) {
    if (mHasEchConfig && !aOther.mHasEchConfig) {
      return true;
    }
    if (!mHasEchConfig && aOther.mHasEchConfig) {
      return false;
    }
  }

  return mSvcFieldPriority < aOther.mSvcFieldPriority;
}

Maybe<uint16_t> SVCB::GetPort() const {
  Maybe<uint16_t> port;
  for (const auto& value : mSvcFieldValue) {
    if (value.mValue.is<SvcParamPort>()) {
      port.emplace(value.mValue.as<SvcParamPort>().mValue);
      if (NS_FAILED(NS_CheckPortSafety(*port, "https"))) {
        *port = 0;
      }
      return port;
    }
  }

  return Nothing();
}

bool SVCB::NoDefaultAlpn() const {
  for (const auto& value : mSvcFieldValue) {
    if (value.mValue.is<SvcParamKeyNoDefaultAlpn>()) {
      return true;
    }
  }

  return false;
}

void SVCB::GetIPHints(CopyableTArray<mozilla::net::NetAddr>& aAddresses) const {
  if (mSvcFieldPriority == 0) {
    return;
  }

  for (const auto& value : mSvcFieldValue) {
    if (value.mValue.is<SvcParamIpv4Hint>()) {
      aAddresses.AppendElements(value.mValue.as<SvcParamIpv4Hint>().mValue);
    } else if (value.mValue.is<SvcParamIpv6Hint>()) {
      aAddresses.AppendElements(value.mValue.as<SvcParamIpv6Hint>().mValue);
    }
  }
}

class AlpnComparator {
 public:
  bool Equals(const std::tuple<nsCString, SupportedAlpnRank>& aA,
              const std::tuple<nsCString, SupportedAlpnRank>& aB) const {
    return std::get<1>(aA) == std::get<1>(aB);
  }
  bool LessThan(const std::tuple<nsCString, SupportedAlpnRank>& aA,
                const std::tuple<nsCString, SupportedAlpnRank>& aB) const {
    return std::get<1>(aA) > std::get<1>(aB);
  }
};

nsTArray<std::tuple<nsCString, SupportedAlpnRank>> SVCB::GetAllAlpn(
    bool& aHasNoDefaultAlpn) const {
  aHasNoDefaultAlpn = false;
  nsTArray<std::tuple<nsCString, SupportedAlpnRank>> alpnList;
  for (const auto& value : mSvcFieldValue) {
    if (value.mValue.is<SvcParamAlpn>()) {
      for (const auto& alpn : value.mValue.as<SvcParamAlpn>().mValue) {
        alpnList.AppendElement(std::make_tuple(alpn, IsAlpnSupported(alpn)));
      }
    } else if (value.mValue.is<SvcParamKeyNoDefaultAlpn>()) {
      // Found "no-default-alpn".
      aHasNoDefaultAlpn = true;
    }
  }
  alpnList.Sort(AlpnComparator());
  return alpnList;
}

SVCBRecord::SVCBRecord(const SVCB& data,
                       Maybe<std::tuple<nsCString, SupportedAlpnRank>> aAlpn)
    : mData(data), mAlpn(std::move(aAlpn)) {
  mPort = mData.GetPort();
}

NS_IMETHODIMP SVCBRecord::GetPriority(uint16_t* aPriority) {
  *aPriority = mData.mSvcFieldPriority;
  return NS_OK;
}

NS_IMETHODIMP SVCBRecord::GetName(nsACString& aName) {
  aName = mData.mSvcDomainName;
  return NS_OK;
}

Maybe<uint16_t> SVCBRecord::GetPort() { return mPort; }

Maybe<std::tuple<nsCString, SupportedAlpnRank>> SVCBRecord::GetAlpn() {
  return mAlpn;
}

NS_IMETHODIMP SVCBRecord::GetSelectedAlpn(nsACString& aAlpn) {
  if (!mAlpn) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  aAlpn = std::get<0>(*mAlpn);
  return NS_OK;
}

NS_IMETHODIMP SVCBRecord::GetEchConfig(nsACString& aEchConfig) {
  aEchConfig = mData.mEchConfig;
  return NS_OK;
}

NS_IMETHODIMP SVCBRecord::GetODoHConfig(nsACString& aODoHConfig) {
  aODoHConfig = mData.mODoHConfig;
  return NS_OK;
}

NS_IMETHODIMP SVCBRecord::GetValues(nsTArray<RefPtr<nsISVCParam>>& aValues) {
  for (const auto& v : mData.mSvcFieldValue) {
    RefPtr<nsISVCParam> param = new SvcParam(v.mValue);
    aValues.AppendElement(param);
  }
  return NS_OK;
}

NS_IMETHODIMP SVCBRecord::GetHasIPHintAddress(bool* aHasIPHintAddress) {
  *aHasIPHintAddress = mData.mHasIPHints;
  return NS_OK;
}

static bool CheckRecordIsUsableWithCname(const SVCB& aRecord,
                                         const nsACString& aCname) {
  if (StaticPrefs::network_dns_https_rr_check_record_with_cname() &&
      !aCname.IsEmpty() && !aRecord.mSvcDomainName.Equals(aCname)) {
    return false;
  }

  return true;
}

static bool CheckRecordIsUsable(const SVCB& aRecord, nsIDNSService* aDNSService,
                                const nsACString& aHost,
                                uint32_t& aExcludedCount) {
  if (!aHost.IsEmpty()) {
    bool excluded = false;
    if (NS_SUCCEEDED(aDNSService->IsSVCDomainNameFailed(
            aHost, aRecord.mSvcDomainName, &excluded)) &&
        excluded) {
      // Skip if the domain name of this record was failed to connect before.
      ++aExcludedCount;
      return false;
    }
  }

  Maybe<uint16_t> port = aRecord.GetPort();
  if (port && *port == 0) {
    // Found an unsafe port, skip this record.
    return false;
  }

  return true;
}

static bool CheckAlpnIsUsable(SupportedAlpnRank aAlpnType, bool aNoHttp2,
                              bool aNoHttp3, bool aCheckHttp3ExcludedList,
                              const nsACString& aTargetName,
                              uint32_t& aExcludedCount) {
  // Skip if this alpn is not supported.
  if (aAlpnType == SupportedAlpnRank::NOT_SUPPORTED) {
    return false;
  }

  // Skip if we don't want to use http2.
  if (aNoHttp2 && aAlpnType == SupportedAlpnRank::HTTP_2) {
    return false;
  }

  if (IsHttp3(aAlpnType)) {
    if (aCheckHttp3ExcludedList && gHttpHandler->IsHttp3Excluded(aTargetName)) {
      aExcludedCount++;
      return false;
    }

    if (aNoHttp3) {
      return false;
    }
  }

  return true;
}

static nsTArray<SVCBWrapper> FlattenRecords(const nsACString& aHost,
                                            const nsTArray<SVCB>& aRecords,
                                            uint32_t& aH3RecordCount) {
  nsTArray<SVCBWrapper> result;
  aH3RecordCount = 0;
  for (const auto& record : aRecords) {
    bool hasNoDefaultAlpn = false;
    nsTArray<std::tuple<nsCString, SupportedAlpnRank>> alpnList =
        record.GetAllAlpn(hasNoDefaultAlpn);
    if (alpnList.IsEmpty()) {
      result.AppendElement(SVCBWrapper(record));
    } else {
      bool h1AlpnAdded = false;
      if (!hasNoDefaultAlpn) {
        // Consider two scenarios when "no-default-alpn" is not found:
        // 1. If echConfig is present in the record:
        //    - Firefox should always attempt to connect using echConfig without
        //    fallback.
        //    - Therefore, we add an additional record with an empty ALPN to
        //    allow Firefox to retry using HTTP/1.1 or h2 with echConfig.
        //
        // 2. If echConfig is not present in the record::
        //    - We allow fallback to connections that do not use HTTPS RR.
        //    - In this case, adding another record with the same target name as
        //    the host name is unnecessary.
        if (!aHost.Equals(record.mSvcDomainName) || record.mHasEchConfig) {
          alpnList.AppendElement(
              std::make_tuple(""_ns, SupportedAlpnRank::HTTP_1_1));
          h1AlpnAdded = true;
        }
      }
      for (const auto& alpn : alpnList) {
        const auto alpnRank = std::get<1>(alpn);
        if (IsHttp3(alpnRank)) {
          aH3RecordCount++;
        }

        // Skip explicit h2 if h1 is already present.
        if (alpnRank == SupportedAlpnRank::HTTP_2 && h1AlpnAdded) {
          continue;
        }

        // For h2, normalize the tuple to use an empty ALPN. If both h1 and h2
        // are available, the ALPN negotiation will automatically select h2 when
        // the server supports it, otherwise it will fall back to h1.
        // However, if `no-default-alpn` is present, fallback to h1 is not
        // possible, so we must explicitly set h2.
        auto chosen =
            (alpnRank == SupportedAlpnRank::HTTP_2 && !hasNoDefaultAlpn)
                ? std::make_tuple(""_ns, alpnRank)
                : alpn;
        SVCBWrapper wrapper(record);
        wrapper.mAlpn = Some(chosen);
        result.AppendElement(std::move(wrapper));
      }
    }
  }
  return result;
}

static void TelemetryForServiceModeRecord(const nsACString& aKey) {
#ifndef ANDROID
  glean::networking::https_record_state.Get(aKey).Add(1);
#endif
}

already_AddRefed<nsISVCBRecord>
DNSHTTPSSVCRecordBase::GetServiceModeRecordInternal(
    bool aNoHttp2, bool aNoHttp3, const nsTArray<SVCB>& aRecords,
    bool& aRecordsAllExcluded, bool aCheckHttp3ExcludedList,
    const nsACString& aCname) {
  RefPtr<SVCBRecord> selectedRecord;
  RefPtr<SVCBRecord> h3RecordWithEchConfig;
  uint32_t recordHasNoDefaultAlpnCount = 0;
  uint32_t recordExcludedCount = 0;
  uint32_t recordHasUnmatchedCname = 0;
  aRecordsAllExcluded = false;
  nsCOMPtr<nsIDNSService> dns = do_GetService(NS_DNSSERVICE_CONTRACTID);
  uint32_t h3ExcludedCount = 0;
  uint32_t h3RecordCount = 0;
  nsTArray<SVCBWrapper> records =
      FlattenRecords(mHost, aRecords, h3RecordCount);
  for (const auto& record : records) {
    if (record.mRecord.mSvcFieldPriority == 0) {
      // In ServiceMode, the SvcPriority should never be 0.
      TelemetryForServiceModeRecord("invalid"_ns);
      return nullptr;
    }

    if (record.mRecord.NoDefaultAlpn()) {
      ++recordHasNoDefaultAlpnCount;
    }

    if (!CheckRecordIsUsable(record.mRecord, dns, mHost, recordExcludedCount)) {
      // Skip if this record is not usable.
      continue;
    }

    if (!CheckRecordIsUsableWithCname(record.mRecord, aCname)) {
      recordHasUnmatchedCname++;
      continue;
    }

    if (record.mAlpn) {
      if (!CheckAlpnIsUsable(std::get<1>(*(record.mAlpn)), aNoHttp2, aNoHttp3,
                             aCheckHttp3ExcludedList,
                             record.mRecord.mSvcDomainName, h3ExcludedCount)) {
        continue;
      }

      if (IsHttp3(std::get<1>(*(record.mAlpn)))) {
        // If the selected alpn is h3 and ech for h3 is disabled, we want
        // to find out if there is another non-h3 record that has
        // echConfig. If yes, we'll use the non-h3 record with echConfig
        // to connect. If not, we'll use h3 to connect without echConfig.
        if (record.mRecord.mHasEchConfig &&
            (nsHttpHandler::EchConfigEnabled() &&
             !nsHttpHandler::EchConfigEnabled(true))) {
          if (!h3RecordWithEchConfig) {
            // Save this h3 record for later use.
            h3RecordWithEchConfig =
                new SVCBRecord(record.mRecord, record.mAlpn);
            // Make sure the next record is not h3.
            aNoHttp3 = true;
            continue;
          }
        }
      }
    }

    if (!selectedRecord) {
      selectedRecord = new SVCBRecord(record.mRecord, record.mAlpn);
    }
  }

  if (!selectedRecord && !h3RecordWithEchConfig) {
    // If all records indicate "no-default-alpn", we should not use this RRSet.
    if (recordHasNoDefaultAlpnCount == records.Length()) {
      TelemetryForServiceModeRecord("no_default_alpn"_ns);
      return nullptr;
    }

    if (recordExcludedCount == records.Length()) {
      aRecordsAllExcluded = true;
      TelemetryForServiceModeRecord("all_excluded"_ns);
      return nullptr;
    }

    if (recordHasUnmatchedCname == records.Length()) {
      TelemetryForServiceModeRecord("unmatched_cname"_ns);
      return nullptr;
    }

    // If all records are in http3 excluded list, try again without checking the
    // excluded list. This is better than returning nothing.
    if (h3ExcludedCount && h3ExcludedCount == h3RecordCount &&
        aCheckHttp3ExcludedList) {
      return GetServiceModeRecordInternal(aNoHttp2, aNoHttp3, aRecords,
                                          aRecordsAllExcluded, false, aCname);
    }
  }

  if (h3RecordWithEchConfig) {
    TelemetryForServiceModeRecord("succeeded"_ns);
    if (selectedRecord && selectedRecord->mData.mHasEchConfig) {
      return selectedRecord.forget();
    }

    return h3RecordWithEchConfig.forget();
  }

  if (selectedRecord) {
    TelemetryForServiceModeRecord("succeeded"_ns);
  } else {
    TelemetryForServiceModeRecord("others"_ns);
  }
  return selectedRecord.forget();
}

void DNSHTTPSSVCRecordBase::GetAllRecordsInternal(
    bool aNoHttp2, bool aNoHttp3, const nsACString& aCname,
    const nsTArray<SVCB>& aRecords, bool aOnlyRecordsWithECH,
    bool* aAllRecordsHaveEchConfig, bool* aAllRecordsInH3ExcludedList,
    nsTArray<RefPtr<nsISVCBRecord>>& aResult, bool aCheckHttp3ExcludedList) {
  if (aRecords.IsEmpty()) {
    return;
  }

  *aAllRecordsHaveEchConfig = aRecords[0].mHasEchConfig;
  *aAllRecordsInH3ExcludedList = false;
  // The first record should have echConfig.
  if (aOnlyRecordsWithECH && !(*aAllRecordsHaveEchConfig)) {
    return;
  }

  uint32_t h3ExcludedCount = 0;
  uint32_t h3RecordCount = 0;
  nsTArray<SVCBWrapper> records =
      FlattenRecords(mHost, aRecords, h3RecordCount);
  for (const auto& record : records) {
    if (record.mRecord.mSvcFieldPriority == 0) {
      // This should not happen, since GetAllRecordsInternal()
      // should be called only if GetServiceModeRecordInternal() returns a
      // non-null record.
      MOZ_ASSERT(false);
      return;
    }

    // Records with echConfig are in front of records without echConfig, so we
    // don't have to continue.
    *aAllRecordsHaveEchConfig &= record.mRecord.mHasEchConfig;
    if (aOnlyRecordsWithECH && !(*aAllRecordsHaveEchConfig)) {
      aResult.Clear();
      return;
    }

    if (!CheckRecordIsUsableWithCname(record.mRecord, aCname)) {
      continue;
    }

    Maybe<uint16_t> port = record.mRecord.GetPort();
    if (port && *port == 0) {
      // Found an unsafe port, skip this record.
      continue;
    }

    if (record.mAlpn) {
      if (!CheckAlpnIsUsable(std::get<1>(*(record.mAlpn)), aNoHttp2, aNoHttp3,
                             aCheckHttp3ExcludedList,
                             record.mRecord.mSvcDomainName, h3ExcludedCount)) {
        continue;
      }
    }

    RefPtr<nsISVCBRecord> svcbRecord =
        new SVCBRecord(record.mRecord, record.mAlpn);
    aResult.AppendElement(svcbRecord);
  }

  // If all records are in http3 excluded list, try again without checking the
  // excluded list. This is better than returning nothing.
  if (h3ExcludedCount && h3ExcludedCount == h3RecordCount &&
      aCheckHttp3ExcludedList) {
    GetAllRecordsInternal(aNoHttp2, aNoHttp3, aCname, aRecords,
                          aOnlyRecordsWithECH, aAllRecordsHaveEchConfig,
                          aAllRecordsInH3ExcludedList, aResult, false);
    *aAllRecordsInH3ExcludedList = true;
  }
}

bool DNSHTTPSSVCRecordBase::HasIPAddressesInternal(
    const nsTArray<SVCB>& aRecords) {
  for (const SVCB& record : aRecords) {
    if (record.mSvcFieldPriority != 0) {
      for (const auto& value : record.mSvcFieldValue) {
        if (value.mValue.is<SvcParamIpv4Hint>()) {
          return true;
        }
        if (value.mValue.is<SvcParamIpv6Hint>()) {
          return true;
        }
      }
    }
  }

  return false;
}

}  // namespace net
}  // namespace mozilla
