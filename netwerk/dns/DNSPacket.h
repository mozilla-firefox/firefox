/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_DNSPacket_h_
#define mozilla_net_DNSPacket_h_

#include <functional>

#include "DNS.h"
#include "DNSByTypeRecord.h"
#include "mozilla/Maybe.h"
#include "mozilla/Result.h"
#include "nsClassHashtable.h"
#include "nsIDNSService.h"

namespace mozilla {
namespace net {

class DOHresp {
 public:
  nsresult Add(uint32_t TTL, unsigned char const* dns, unsigned int index,
               uint16_t len, bool aLocalAllowed);
  nsTArray<NetAddr> mAddresses;
  uint32_t mTtl = 0;
};

// the values map to RFC1035 type identifiers
enum TrrType {
  TRRTYPE_A = 1,
  TRRTYPE_NS = 2,
  TRRTYPE_CNAME = 5,
  TRRTYPE_AAAA = 28,
  TRRTYPE_OPT = 41,
  TRRTYPE_TXT = 16,
  TRRTYPE_HTTPSSVC = nsIDNSService::RESOLVE_TYPE_HTTPSSVC,  // 65
};

class DNSPacket {
 public:
  // Never accept larger DOH responses than this as that would indicate
  // something is wrong. Typical ones are much smaller.
  static const unsigned int MAX_SIZE = 3200;

  DNSPacket() = default;
  virtual ~DNSPacket() = default;

  Result<uint8_t, nsresult> GetRCode() const;
  Result<bool, nsresult> RecursionAvailable() const;

  // Called in order to feed data into the buffer.
  nsresult OnDataAvailable(nsIRequest* aRequest, nsIInputStream* aInputStream,
                           uint64_t aOffset, const uint32_t aCount);

  // Encode the input host name into a sequence of labels.
  static nsresult EncodeHost(nsCString& aBody, const nsACString& aHost);
  // Encodes the name request into a buffer that represents a DNS packet
  virtual nsresult EncodeRequest(nsCString& aBody, const nsACString& aHost,
                                 uint16_t aType, bool aDisableECS);

  // Decodes the DNS response and extracts the responses, additional records,
  // etc. XXX: This should probably be refactored to reduce the number of
  // output parameters and have a common format for different record types.
  virtual nsresult Decode(
      nsCString& aHost, enum TrrType aType, nsCString& aCname,
      bool aAllowRFC1918, DOHresp& aResp, TypeRecordResultType& aTypeResult,
      nsClassHashtable<nsCStringHashKey, DOHresp>& aAdditionalRecords,
      uint32_t& aTTL);

  void SetOriginHost(const Maybe<nsCString>& aHost) { mOriginHost = aHost; }

  // True when the aCname returned by the last Decode() call came from an HTTPS
  // AliasMode (SvcPriority 0) record rather than a plain CNAME record. Only an
  // AliasMode TargetName has to be chased by the client (RFC 9460); a CNAME is
  // already resolved by the recursive resolver.
  bool CnameIsHTTPSAlias() const { return mCnameIsHTTPSAlias; }

  nsresult FillBuffer(std::function<int(unsigned char response[MAX_SIZE])>&&);

  static nsresult ParseHTTPS(uint16_t aRDLen, struct SVCB& aParsed,
                             unsigned int aIndex, const unsigned char* aBuffer,
                             unsigned int aBodySize,
                             const nsACString& aOriginHost,
                             bool aAllowRFC1918 = true);
  void SetNativePacket(bool aNative) { mNativePacket = aNative; }

  // Returns true if both DNS names are equal, ignoring case (RFC 4343) and a
  // trailing root dot.
  static bool IsSameName(const nsACString& aName, const nsACString& aOther);

  static nsresult GetQname(nsACString& aQname, unsigned int& aIndex,
                           const unsigned char* aBuffer,
                           unsigned int aBodySize);

 protected:
  nsresult PassQName(unsigned int& index, const unsigned char* aBuffer);
  static nsresult ParseSvcParam(unsigned int svcbIndex, uint16_t key,
                                SvcFieldValue& field, uint16_t length,
                                const unsigned char* aBuffer,
                                bool aAllowRFC1918 = true);
  nsresult DecodeInternal(
      nsCString& aHost, enum TrrType aType, nsCString& aCname,
      bool aAllowRFC1918, DOHresp& aResp, TypeRecordResultType& aTypeResult,
      nsClassHashtable<nsCStringHashKey, DOHresp>& aAdditionalRecords,
      uint32_t& aTTL, const unsigned char* aBuffer, uint32_t aLen);

  // The response buffer.
  unsigned char mResponse[MAX_SIZE]{};
  unsigned int mBodySize = 0;
  // True when decoding a DNS packet received from OS. Decoding will
  // not panic if packet ID is not zero.
  bool mNativePacket = false;
  nsresult mStatus = NS_OK;
  Maybe<nsCString> mOriginHost;
  bool mCnameIsHTTPSAlias = false;
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_DNSPacket_h_
