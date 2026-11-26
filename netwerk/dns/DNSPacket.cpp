/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DNSPacket.h"

#include "DNS.h"
#include "mozilla/StaticPrefs_network.h"
// Put DNSLogging.h at the end to avoid LOG being overwritten by other headers.
#include "DNSLogging.h"
#include "nsIInputStream.h"

namespace mozilla {
namespace net {

static uint16_t get16bit(const unsigned char* aData, unsigned int index) {
  return (static_cast<uint16_t>(aData[index]) << 8) |
         static_cast<uint16_t>(aData[index + 1]);
}

static uint32_t get32bit(const unsigned char* aData, unsigned int index) {
  return (static_cast<uint32_t>(aData[index]) << 24) |
         (static_cast<uint32_t>(aData[index + 1]) << 16) |
         (static_cast<uint32_t>(aData[index + 2]) << 8) |
         static_cast<uint32_t>(aData[index + 3]);
}

// https://datatracker.ietf.org/doc/html/rfc8914#name-defined-extended-dns-errors
// This is a list of errors for which we should not fallback to Do53.
// These are normally explicit filtering performed by the recursive resolver.
bool hardFail(uint16_t code) {
  const uint16_t noFallbackErrors[] = {
      4,   // Forged answer (malware filtering)
      17,  // Filtered
  };

  for (const auto& err : noFallbackErrors) {
    if (code == err) {
      return true;
    }
  }
  return false;
}

nsresult DNSPacket::FillBuffer(
    std::function<int(unsigned char response[MAX_SIZE])>&& aPredicate) {
  int response_length = aPredicate(mResponse);
  if (response_length < 0) {
    LOG(("FillBuffer response len < 0"));
    mBodySize = 0;
    mStatus = NS_ERROR_UNEXPECTED;
    return mStatus;
  }

  if (static_cast<uint32_t>(response_length) > MAX_SIZE) {
    LOG(("FillBuffer response len %d > MAX_SIZE %u", response_length,
         MAX_SIZE));
    mBodySize = 0;
    mStatus = NS_ERROR_UNEXPECTED;
    return mStatus;
  }

  mBodySize = response_length;
  return NS_OK;
}
// static
nsresult DNSPacket::ParseSvcParam(unsigned int svcbIndex, uint16_t key,
                                  SvcFieldValue& field, uint16_t length,
                                  const unsigned char* aBuffer,
                                  bool aAllowRFC1918) {
  switch (key) {
    case SvcParamKeyMandatory: {
      if (length % 2 != 0) {
        // This key should encode a list of uint16_t
        return NS_ERROR_UNEXPECTED;
      }
      while (length > 0) {
        uint16_t mandatoryKey = get16bit(aBuffer, svcbIndex);
        length -= 2;
        svcbIndex += 2;

        if (!IsValidSvcParamKey(mandatoryKey)) {
          LOG(("The mandatory field includes a key we don't support %u",
               mandatoryKey));
          return NS_ERROR_UNEXPECTED;
        }
      }
      break;
    }
    case SvcParamKeyAlpn: {
      field.mValue = AsVariant(SvcParamAlpn());
      auto& alpnArray = field.mValue.as<SvcParamAlpn>().mValue;
      while (length > 0) {
        uint8_t alpnIdLength = aBuffer[svcbIndex++];
        length -= 1;
        if (alpnIdLength > length) {
          return NS_ERROR_UNEXPECTED;
        }

        alpnArray.AppendElement(
            nsCString((const char*)&aBuffer[svcbIndex], alpnIdLength));
        length -= alpnIdLength;
        svcbIndex += alpnIdLength;
      }
      break;
    }
    case SvcParamKeyNoDefaultAlpn: {
      if (length != 0) {
        // This key should not contain a value
        return NS_ERROR_UNEXPECTED;
      }
      field.mValue = AsVariant(SvcParamNoDefaultAlpn{});
      break;
    }
    case SvcParamKeyPort: {
      if (length != 2) {
        // This key should only encode a uint16_t
        return NS_ERROR_UNEXPECTED;
      }
      field.mValue =
          AsVariant(SvcParamPort{.mValue = get16bit(aBuffer, svcbIndex)});
      break;
    }
    case SvcParamKeyIpv4Hint: {
      if (length % 4 != 0) {
        // This key should only encode IPv4 addresses
        return NS_ERROR_UNEXPECTED;
      }

      field.mValue = AsVariant(SvcParamIpv4Hint());
      auto& ipv4array = field.mValue.as<SvcParamIpv4Hint>().mValue;
      while (length > 0) {
        NetAddr addr;
        addr.inet.family = AF_INET;
        addr.inet.port = 0;
        addr.inet.ip = ntohl(get32bit(aBuffer, svcbIndex));
        if (aAllowRFC1918 || !addr.IsIPAddrLocal()) {
          ipv4array.AppendElement(addr);
        }
        length -= 4;
        svcbIndex += 4;
      }
      break;
    }
    case SvcParamKeyEchConfig: {
      field.mValue = AsVariant(SvcParamEchConfig{
          .mValue = nsCString((const char*)(&aBuffer[svcbIndex]), length)});
      break;
    }
    case SvcParamKeyIpv6Hint: {
      if (length % 16 != 0) {
        // This key should only encode IPv6 addresses
        return NS_ERROR_UNEXPECTED;
      }

      field.mValue = AsVariant(SvcParamIpv6Hint());
      auto& ipv6array = field.mValue.as<SvcParamIpv6Hint>().mValue;
      while (length > 0) {
        NetAddr addr;
        addr.inet6.family = AF_INET6;
        addr.inet6.port = 0;      // unknown
        addr.inet6.flowinfo = 0;  // unknown
        addr.inet6.scope_id = 0;  // unknown
        for (int i = 0; i < 16; i++, svcbIndex++) {
          addr.inet6.ip.u8[i] = aBuffer[svcbIndex];
        }
        if (aAllowRFC1918 || !addr.IsIPAddrLocal()) {
          ipv6array.AppendElement(addr);
        }
        length -= 16;
        // no need to increase svcbIndex - we did it in the for above.
      }
      break;
    }
    case SvcParamKeyODoHConfig: {
      field.mValue = AsVariant(SvcParamODoHConfig{
          .mValue = nsCString((const char*)(&aBuffer[svcbIndex]), length)});
      break;
    }
    default: {
      // Unespected type. We'll just ignore it.
      return NS_OK;
      break;
    }
  }
  return NS_OK;
}

nsresult DNSPacket::PassQName(unsigned int& index,
                              const unsigned char* aBuffer) {
  uint8_t length;
  do {
    if (mBodySize < (index + 1)) {
      LOG(("TRR: PassQName:%d fail at index %d\n", __LINE__, index));
      return NS_ERROR_ILLEGAL_VALUE;
    }
    length = static_cast<uint8_t>(aBuffer[index]);
    if ((length & 0xc0) == 0xc0) {
      // name pointer, advance over it and be done
      if (mBodySize < (index + 2)) {
        return NS_ERROR_ILLEGAL_VALUE;
      }
      index += 2;
      break;
    }
    if (length & 0xc0) {
      LOG(("TRR: illegal label length byte (%x) at index %d\n", length, index));
      return NS_ERROR_ILLEGAL_VALUE;
    }
    // pass label
    if (mBodySize < (index + 1 + length)) {
      LOG(("TRR: PassQName:%d fail at index %d\n", __LINE__, index));
      return NS_ERROR_ILLEGAL_VALUE;
    }
    index += 1 + length;
  } while (length);
  return NS_OK;
}

// static
bool DNSPacket::IsSameName(const nsACString& aName, const nsACString& aOther) {
  auto withoutRootDot = [](const nsACString& aStr) {
    return StringEndsWith(aStr, "."_ns) ? Substring(aStr, 0, aStr.Length() - 1)
                                        : Substring(aStr, 0);
  };
  return withoutRootDot(aName).Equals(withoutRootDot(aOther),
                                      nsCaseInsensitiveCStringComparator);
}

// GetQname: retrieves the qname (stores in 'aQname') and stores the index
// after qname was parsed into the 'aIndex'.
// static
nsresult DNSPacket::GetQname(nsACString& aQname, unsigned int& aIndex,
                             const unsigned char* aBuffer,
                             unsigned int aBodySize) {
  uint8_t clength = 0;
  unsigned int cindex = aIndex;
  unsigned int loop = 128;    // a valid DNS name can never loop this much
  unsigned int endindex = 0;  // index position after this data
  do {
    if (cindex >= aBodySize) {
      LOG(("TRR: bad Qname packet\n"));
      return NS_ERROR_ILLEGAL_VALUE;
    }
    clength = static_cast<uint8_t>(aBuffer[cindex]);
    if ((clength & 0xc0) == 0xc0) {
      // name pointer, get the new offset (14 bits)
      if ((cindex + 1) >= aBodySize) {
        return NS_ERROR_ILLEGAL_VALUE;
      }
      // extract the new index position for the next label
      uint16_t newpos = (clength & 0x3f) << 8 | aBuffer[cindex + 1];
      if (!endindex) {
        // only update on the first "jump"
        endindex = cindex + 2;
      }
      cindex = newpos;
      continue;
    }
    if (clength & 0xc0) {
      // any of those bits set individually is an error
      LOG(("TRR: bad Qname packet\n"));
      return NS_ERROR_ILLEGAL_VALUE;
    }

    cindex++;

    if (clength) {
      if (!aQname.IsEmpty()) {
        aQname.Append(".");
      }
      if ((cindex + clength) > aBodySize) {
        return NS_ERROR_ILLEGAL_VALUE;
      }
      aQname.Append((const char*)(&aBuffer[cindex]), clength);
      cindex += clength;  // skip label
    }
  } while (clength && --loop);

  if (!loop) {
    LOG(("DNSPacket::DohDecode pointer loop error\n"));
    return NS_ERROR_ILLEGAL_VALUE;
  }
  if (!endindex) {
    // there was no "jump"
    endindex = cindex;
  }
  aIndex = endindex;
  return NS_OK;
}

nsresult DOHresp::Add(uint32_t TTL, unsigned char const* dns,
                      unsigned int index, uint16_t len, bool aLocalAllowed) {
  NetAddr addr;
  if (4 == len) {
    // IPv4
    addr.inet.family = AF_INET;
    addr.inet.port = 0;  // unknown
    addr.inet.ip = ntohl(get32bit(dns, index));
  } else if (16 == len) {
    // IPv6
    addr.inet6.family = AF_INET6;
    addr.inet6.port = 0;      // unknown
    addr.inet6.flowinfo = 0;  // unknown
    addr.inet6.scope_id = 0;  // unknown
    for (int i = 0; i < 16; i++, index++) {
      addr.inet6.ip.u8[i] = dns[index];
    }
  } else {
    return NS_ERROR_UNEXPECTED;
  }

  if (addr.IsIPAddrLocal() && !aLocalAllowed) {
    return NS_ERROR_FAILURE;
  }

  // While the DNS packet might return individual TTLs for each address,
  // we can only return one value in the AddrInfo class so pick the
  // lowest number. Seed from the first successful add (mAddresses
  // empty means no record's TTL is in mTtl yet), otherwise take the
  // running minimum. mTtl stays at 0 if no record is ever added so
  // downstream callers that read it as a "don't cache" sentinel
  // continue to work.
  if (mAddresses.IsEmpty() || TTL < mTtl) {
    mTtl = TTL;
  }

  if (LOG_ENABLED()) {
    LOG(("DOHresp:Add %s\n", addr.ToString().get()));
  }
  mAddresses.AppendElement(addr);
  return NS_OK;
}

nsresult DNSPacket::OnDataAvailable(nsIRequest* aRequest,
                                    nsIInputStream* aInputStream,
                                    uint64_t aOffset, const uint32_t aCount) {
  if (aCount > MAX_SIZE - mBodySize) {
    LOG(("DNSPacket::OnDataAvailable:%d fail\n", __LINE__));
    return NS_ERROR_FAILURE;
  }
  uint32_t count;
  nsresult rv =
      aInputStream->Read((char*)mResponse + mBodySize, aCount, &count);
  if (NS_FAILED(rv)) {
    return rv;
  }
  mBodySize += count;
  return NS_OK;
}

const uint8_t kDNS_CLASS_IN = 1;

// static
nsresult DNSPacket::EncodeHost(nsCString& aBody, const nsACString& aHost) {
  // The input host name should be converted to a sequence of labels, where
  // each label consists of a length octet followed by that number of
  // octets.  The domain name terminates with the zero length octet for the
  // null label of the root.

  int32_t index = 0;
  int32_t offset = 0;
  do {
    bool dotFound = false;
    int32_t labelLength;
    index = aHost.FindChar('.', offset);
    if (kNotFound != index) {
      dotFound = true;
      labelLength = index - offset;
    } else {
      labelLength = aHost.Length() - offset;
    }
    if (labelLength > 63) {
      // too long label!
      return NS_ERROR_ILLEGAL_VALUE;
    }
    if (labelLength > 0) {
      aBody += static_cast<unsigned char>(labelLength);
      nsDependentCSubstring label = Substring(aHost, offset, labelLength);
      aBody.Append(label);
    }
    if (!dotFound) {
      aBody += '\0';  // terminate with a final zero
      break;
    }
    offset += labelLength + 1;  // move over label and dot
  } while (true);

  return NS_OK;
}

nsresult DNSPacket::EncodeRequest(nsCString& aBody, const nsACString& aHost,
                                  uint16_t aType, bool aDisableECS) {
  aBody.Truncate();
  // Header
  aBody += '\0';
  aBody += '\0';  // 16 bit id
  aBody += 0x01;  // |QR|   Opcode  |AA|TC|RD| Set the RD bit
  aBody += '\0';  // |RA|   Z    |   RCODE   |
  aBody += '\0';
  aBody += 1;  // QDCOUNT (number of entries in the question section)
  aBody += '\0';
  aBody += '\0';  // ANCOUNT
  aBody += '\0';
  aBody += '\0';  // NSCOUNT

  char additionalRecords =
      (aDisableECS || StaticPrefs::network_trr_padding()) ? 1 : 0;
  aBody += '\0';               // ARCOUNT
  aBody += additionalRecords;  // ARCOUNT low byte for EDNS(0)

  // Question

  nsresult rv = EncodeHost(aBody, aHost);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Followed by 16 bit QTYPE and 16 bit QCLASS
  aBody += static_cast<uint8_t>(aType >> 8);  // upper 8 bit TYPE
  aBody += static_cast<uint8_t>(aType);
  aBody += '\0';           // upper 8 bit CLASS
  aBody += kDNS_CLASS_IN;  // IN - "the Internet"

  if (additionalRecords) {
    // EDNS(0) is RFC 6891, ECS is RFC 7871
    aBody += '\0';  // NAME       | domain name  | MUST be 0 (root domain) |
    aBody += '\0';
    aBody += 41;  // TYPE       | u_int16_t    | OPT (41)                     |
    aBody += 16;  // CLASS      | u_int16_t    | requestor's UDP payload size |
    aBody +=
        '\0';  // advertise 4K (high-byte: 16 | low-byte: 0), ignored by DoH
    aBody += '\0';  // TTL        | u_int32_t    | extended RCODE and flags |
    aBody += '\0';
    aBody += '\0';
    aBody += '\0';

    // calculate padding length
    unsigned int paddingLen = 0;
    unsigned int rdlen = 0;
    bool padding = StaticPrefs::network_trr_padding();
    if (padding) {
      // always add padding specified in rfc 7830 when this config is enabled
      // to allow the reponse to be padded as well

      // two bytes RDLEN, 4 bytes padding header
      unsigned int packetLen = aBody.Length() + 2 + 4;
      if (aDisableECS) {
        // 8 bytes for disabling ecs
        packetLen += 8;
      }

      // clamp the padding length, because the padding extension only allows up
      // to 2^16 - 1 bytes padding and adding too much padding wastes resources
      uint32_t padTo = std::clamp<uint32_t>(
          StaticPrefs::network_trr_padding_length(), 0, 1024);

      // Calculate number of padding bytes. The second '%'-operator is necessary
      // because we prefer to add 0 bytes padding rather than padTo bytes
      if (padTo > 0) {
        paddingLen = (padTo - (packetLen % padTo)) % padTo;
      }
      // padding header + padding length
      rdlen += 4 + paddingLen;
    }
    if (aDisableECS) {
      rdlen += 8;
    }

    // RDLEN      | u_int16_t    | length of all RDATA          |
    aBody += (char)((rdlen >> 8) & 0xff);  // upper 8 bit RDLEN
    aBody += (char)(rdlen & 0xff);

    // RDATA      | octet stream | {attribute,value} pairs      |
    // The RDATA is just the ECS option setting zero subnet prefix

    if (aDisableECS) {
      aBody += '\0';  // upper 8 bit OPTION-CODE ECS
      aBody += 8;     // OPTION-CODE, 2 octets, for ECS is 8

      aBody += '\0';  // upper 8 bit OPTION-LENGTH
      aBody += 4;     // OPTION-LENGTH, 2 octets, contains the length of the
                      // payload after OPTION-LENGTH
      aBody += '\0';  // upper 8 bit FAMILY. IANA Address Family Numbers
                      // registry, not the AF_* constants!
      aBody += 1;     // FAMILY (Ipv4), 2 octets

      aBody += '\0';  // SOURCE PREFIX-LENGTH      |     SCOPE PREFIX-LENGTH |
      aBody += '\0';

      // ADDRESS, minimum number of octets == nothing because zero bits
    }

    if (padding) {
      aBody += '\0';  // upper 8 bit option OPTION-CODE PADDING
      aBody += 12;    // OPTION-CODE, 2 octets, for PADDING is 12

      // OPTION-LENGTH, 2 octets
      aBody += (char)((paddingLen >> 8) & 0xff);
      aBody += (char)(paddingLen & 0xff);
      for (unsigned int i = 0; i < paddingLen; i++) {
        aBody += '\0';
      }
    }
  }

  return NS_OK;
}

// static
nsresult DNSPacket::ParseHTTPS(uint16_t aRDLen, struct SVCB& aParsed,
                               unsigned int aIndex,
                               const unsigned char* aBuffer,
                               unsigned int aBodySize,
                               const nsACString& aOriginHost,
                               bool aAllowRFC1918) {
  int32_t lastSvcParamKey = -1;
  nsresult rv = NS_OK;
  unsigned int svcbIndex = aIndex;
  CheckedInt<uint16_t> available = aRDLen;

  // Should have at least 2 bytes for the priority and one for the
  // qname length.
  if (available.value() < 3) {
    return NS_ERROR_UNEXPECTED;
  }

  aParsed.mSvcFieldPriority = get16bit(aBuffer, svcbIndex);
  svcbIndex += 2;

  rv = GetQname(aParsed.mSvcDomainName, svcbIndex, aBuffer, aBodySize);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (aParsed.mSvcDomainName.IsEmpty()) {
    if (aParsed.mSvcFieldPriority == 0) {
      // For AliasMode SVCB RRs, a TargetName of "." indicates that
      // the service is not available or does not exist.
      return NS_OK;
    }

    // For ServiceMode SVCB RRs, if TargetName has the value ".",
    // then the owner name of this record MUST be used as
    // the effective TargetName.
    // When the qname is port prefix name, we need to use the
    // original host name as TargetName.
    aParsed.mSvcDomainName = aOriginHost;
  }

  available -= (svcbIndex - aIndex);
  if (!available.isValid()) {
    return NS_ERROR_UNEXPECTED;
  }
  while (available.value() >= 4) {
    // Every SvcFieldValues must have at least 4 bytes for the
    // SvcParamKey (2 bytes) and length of SvcParamValue (2 bytes)
    // If the length ever goes above the available data, meaning if
    // available ever underflows, then that is an error.
    struct SvcFieldValue value;
    uint16_t key = get16bit(aBuffer, svcbIndex);
    svcbIndex += 2;

    // 2.2 Clients MUST consider an RR malformed if SvcParamKeys are
    // not in strictly increasing numeric order.
    if (key <= lastSvcParamKey) {
      LOG(("SvcParamKeys not in increasing order"));
      return NS_ERROR_UNEXPECTED;
    }
    lastSvcParamKey = key;

    uint16_t len = get16bit(aBuffer, svcbIndex);
    svcbIndex += 2;

    available -= 4 + len;
    if (!available.isValid()) {
      return NS_ERROR_UNEXPECTED;
    }

    rv = ParseSvcParam(svcbIndex, key, value, len, aBuffer, aAllowRFC1918);
    if (NS_FAILED(rv)) {
      return rv;
    }
    svcbIndex += len;

    // If this is an unknown key, we will simply ignore it.
    // We also don't need to record SvcParamKeyMandatory
    if (key == SvcParamKeyMandatory || !IsValidSvcParamKey(key)) {
      continue;
    }

    if (value.mValue.is<SvcParamIpv4Hint>() &&
        !value.mValue.as<SvcParamIpv4Hint>().mValue.IsEmpty()) {
      aParsed.mHasIPHints = true;
    }
    if (value.mValue.is<SvcParamIpv6Hint>() &&
        !value.mValue.as<SvcParamIpv6Hint>().mValue.IsEmpty()) {
      aParsed.mHasIPHints = true;
    }
    if (value.mValue.is<SvcParamEchConfig>()) {
      aParsed.mHasEchConfig = true;
      aParsed.mEchConfig = value.mValue.as<SvcParamEchConfig>().mValue;
    }
    if (value.mValue.is<SvcParamODoHConfig>()) {
      aParsed.mODoHConfig = value.mValue.as<SvcParamODoHConfig>().mValue;
    }
    aParsed.mSvcFieldValue.AppendElement(value);
  }

  return NS_OK;
}

Result<uint8_t, nsresult> DNSPacket::GetRCode() const {
  if (mBodySize < 12) {
    LOG(("DNSPacket::GetRCode - packet too small"));
    return Err(NS_ERROR_ILLEGAL_VALUE);
  }

  return mResponse[3] & 0x0F;
}

Result<bool, nsresult> DNSPacket::RecursionAvailable() const {
  if (mBodySize < 12) {
    LOG(("DNSPacket::GetRCode - packet too small"));
    return Err(NS_ERROR_ILLEGAL_VALUE);
  }

  return mResponse[3] & 0x80;
}

nsresult DNSPacket::DecodeInternal(
    nsCString& aHost, enum TrrType aType, nsCString& aCname, bool aAllowRFC1918,
    DOHresp& aResp, TypeRecordResultType& aTypeResult,
    nsClassHashtable<nsCStringHashKey, DOHresp>& aAdditionalRecords,
    uint32_t& aTTL, const unsigned char* aBuffer, uint32_t aLen) {
  // The response has a 12 byte header followed by the question (returned)
  // and then the answer. The answer section itself contains the name, type
  // and class again and THEN the record data.

  // www.example.com response:
  // header:
  // abcd 8180 0001 0001 0000 0000
  // the question:
  // 0377 7777 0765 7861 6d70 6c65 0363 6f6d 0000 0100 01
  // the answer:
  // 03 7777 7707 6578 616d 706c 6503 636f 6d00 0001 0001
  // 0000 0080 0004 5db8 d822

  unsigned int index = 12;
  nsAutoCString host;
  nsresult rv;
  uint16_t extendedError = UINT16_MAX;

  LOG(("doh decode %s %d bytes\n", aHost.get(), aLen));

  aCname.Truncate();
  mCnameIsHTTPSAlias = false;

  // Reset any type record accumulated by a previous decode of this packet.
  aTypeResult = mozilla::AsVariant(Nothing());

  if (aLen < 12) {
    LOG(("TRR bad incoming DOH, eject!\n"));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  if (!mNativePacket && (aBuffer[0] || aBuffer[1])) {
    LOG(("Packet ID is unexpectedly non-zero"));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  uint8_t rcode = mResponse[3] & 0x0F;
  LOG(("TRR Decode %s RCODE %d\n", PromiseFlatCString(aHost).get(), rcode));

  uint16_t questionRecords = get16bit(aBuffer, 4);  // qdcount
  // iterate over the host name(s) in the question. Use GetQname so a
  // server-side compression pointer (a label byte with the top two
  // bits set, legal per RFC 1035 4.1.4) is followed instead of being
  // treated as a 192-byte label, which would desync `index` and let
  // the answer-section parsing below read at the wrong offset.
  while (questionRecords) {
    nsAutoCString qname;
    rv = GetQname(qname, index, aBuffer, mBodySize);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (host.IsEmpty()) {
      host = qname;
    } else if (!qname.IsEmpty()) {
      host.Append(".");
      host.Append(qname);
    }
    if (aLen < (index + 4)) {
      LOG(("TRR Decode 3 index: %u size: %u", index, aLen));
      return NS_ERROR_ILLEGAL_VALUE;
    }
    index += 4;  // skip question's type, class
    questionRecords--;
  }

  // Figure out the number of answer records from ANCOUNT
  uint16_t answerRecords = get16bit(aBuffer, 6);

  LOG(("TRR Decode: %d answer records (%u bytes body) %s index=%u\n",
       answerRecords, aLen, host.get(), index));

  while (answerRecords) {
    nsAutoCString qname;
    rv = GetQname(qname, index, aBuffer, mBodySize);
    if (NS_FAILED(rv)) {
      return rv;
    }
    // 16 bit TYPE
    if (aLen < (index + 2)) {
      LOG(("TRR: Dohdecode:%d fail at index %d\n", __LINE__, index + 2));
      return NS_ERROR_ILLEGAL_VALUE;
    }
    uint16_t TYPE = get16bit(aBuffer, index);

    index += 2;

    // 16 bit class
    if (aLen < (index + 2)) {
      LOG(("TRR: Dohdecode:%d fail at index %d\n", __LINE__, index + 2));
      return NS_ERROR_ILLEGAL_VALUE;
    }
    uint16_t CLASS = get16bit(aBuffer, index);
    if (kDNS_CLASS_IN != CLASS) {
      LOG(("TRR bad CLASS (%u) at index %d\n", CLASS, index));
      return NS_ERROR_UNEXPECTED;
    }
    index += 2;

    // 32 bit TTL (seconds)
    if (aLen < (index + 4)) {
      LOG(("TRR: Dohdecode:%d fail at index %d\n", __LINE__, index));
      return NS_ERROR_ILLEGAL_VALUE;
    }
    uint32_t TTL = get32bit(aBuffer, index);
    index += 4;

    // 16 bit RDLENGTH
    if (aLen < (index + 2)) {
      LOG(("TRR: Dohdecode:%d fail at index %d\n", __LINE__, index));
      return NS_ERROR_ILLEGAL_VALUE;
    }
    uint16_t RDLENGTH = get16bit(aBuffer, index);
    index += 2;

    if (aLen < (index + RDLENGTH)) {
      LOG(("TRR: Dohdecode:%d fail RDLENGTH=%d at index %d\n", __LINE__,
           RDLENGTH, index));
      return NS_ERROR_ILLEGAL_VALUE;
    }

    if ((TYPE != TRRTYPE_CNAME) && (TYPE != TRRTYPE_HTTPSSVC) &&
        (TYPE != static_cast<uint16_t>(aType))) {
      // Not the same type as was asked for nor CNAME
      LOG(("TRR: Dohdecode:%d asked for type %d got %d\n", __LINE__, aType,
           TYPE));
      index += RDLENGTH;
      answerRecords--;
      continue;
    }

    // We check if the qname is a case-insensitive match for the host or the
    // FQDN version of the host
    bool responseMatchesQuestion = IsSameName(qname, aHost);

    if (responseMatchesQuestion) {
      // RDATA
      // - A (TYPE 1):  4 bytes
      // - AAAA (TYPE 28): 16 bytes
      // - NS (TYPE 2): N bytes

      switch (TYPE) {
        case TRRTYPE_A:
          if (RDLENGTH != 4) {
            LOG(("TRR bad length for A (%u)\n", RDLENGTH));
            return NS_ERROR_UNEXPECTED;
          }
          rv = aResp.Add(TTL, aBuffer, index, RDLENGTH, aAllowRFC1918);
          if (NS_FAILED(rv)) {
            LOG(
                ("TRR:DohDecode failed: local IP addresses or unknown IP "
                 "family\n"));
            return rv;
          }
          break;
        case TRRTYPE_AAAA:
          if (RDLENGTH != 16) {
            LOG(("TRR bad length for AAAA (%u)\n", RDLENGTH));
            return NS_ERROR_UNEXPECTED;
          }
          rv = aResp.Add(TTL, aBuffer, index, RDLENGTH, aAllowRFC1918);
          if (NS_FAILED(rv)) {
            LOG(("TRR got unique/local IPv6 address!\n"));
            return rv;
          }
          break;

        case TRRTYPE_NS:
          break;
        case TRRTYPE_CNAME:
          if (aCname.IsEmpty()) {
            nsAutoCString qname;
            unsigned int qnameindex = index;
            rv = GetQname(qname, qnameindex, aBuffer, mBodySize);
            if (NS_FAILED(rv)) {
              return rv;
            }
            if (!qname.IsEmpty()) {
              ToLowerCase(qname);
              aCname = qname;
              LOG(("DNSPacket::DohDecode CNAME host %s => %s\n", host.get(),
                   aCname.get()));
            } else {
              LOG(("DNSPacket::DohDecode empty CNAME for host %s!\n",
                   host.get()));
            }
          } else {
            LOG(("DNSPacket::DohDecode CNAME - ignoring another entry\n"));
          }
          break;
        case TRRTYPE_TXT: {
          // TXT record RRDATA sections are a series of character-strings
          // each character string is a length byte followed by that many data
          // bytes
          nsAutoCString txt;
          unsigned int txtIndex = index;
          uint16_t available = RDLENGTH;

          while (available > 0) {
            uint8_t characterStringLen = aBuffer[txtIndex++];
            available--;
            if (characterStringLen > available) {
              LOG(("DNSPacket::DohDecode MALFORMED TXT RECORD\n"));
              break;
            }
            txt.Append((const char*)(&aBuffer[txtIndex]), characterStringLen);
            txtIndex += characterStringLen;
            available -= characterStringLen;
          }

          if (!aTypeResult.is<TypeRecordTxt>()) {
            aTypeResult = AsVariant(CopyableTArray<nsCString>());
          }

          {
            auto& results = aTypeResult.as<TypeRecordTxt>();
            results.AppendElement(txt);
          }
          if (aTTL > TTL) {
            aTTL = TTL;
          }
          LOG(("DNSPacket::DohDecode TXT host %s => %s\n", host.get(),
               txt.get()));

          break;
        }
        case TRRTYPE_HTTPSSVC: {
          struct SVCB parsed;

          if (aType != TRRTYPE_HTTPSSVC) {
            // Ignore the entry that we just parsed if we didn't ask for it.
            break;
          }

          rv = ParseHTTPS(RDLENGTH, parsed, index, aBuffer, mBodySize,
                          mOriginHost ? *mOriginHost : qname, aAllowRFC1918);
          if (NS_FAILED(rv)) {
            return rv;
          }

          if (parsed.mSvcDomainName.IsEmpty() &&
              parsed.mSvcFieldPriority == 0) {
            // For AliasMode SVCB RRs, a TargetName of "." indicates that the
            // service is not available or does not exist.
            break;
          }

          // Check for AliasForm
          if (aCname.IsEmpty() && parsed.mSvcFieldPriority == 0) {
            // Alias form SvcDomainName must not have the "." value (empty)
            if (parsed.mSvcDomainName.IsEmpty()) {
              return NS_ERROR_UNEXPECTED;
            }
            aCname = parsed.mSvcDomainName;
            mCnameIsHTTPSAlias = true;
            // If aliasForm is present, Service form must be ignored.
            aTypeResult = mozilla::AsVariant(Nothing());
            ToLowerCase(aCname);
            LOG(("DNSPacket::DohDecode HTTPSSVC AliasForm host %s => %s\n",
                 host.get(), aCname.get()));
            break;
          }

          aTTL = TTL;

          if (!aTypeResult.is<TypeRecordHTTPSSVC>()) {
            aTypeResult = mozilla::AsVariant(CopyableTArray<SVCB>());
          }
          {
            auto& results = aTypeResult.as<TypeRecordHTTPSSVC>();
            results.AppendElement(parsed);
          }

          break;
        }
        default:
          // skip unknown record types
          LOG(("TRR unsupported TYPE (%u) RDLENGTH %u\n", TYPE, RDLENGTH));
          break;
      }
    } else {
      LOG(("TRR asked for %s data but got %s\n", aHost.get(), qname.get()));
    }

    index += RDLENGTH;
    LOG(("done with record type %u len %u index now %u of %u\n", TYPE, RDLENGTH,
         index, aLen));
    answerRecords--;
  }

  // NSCOUNT
  uint16_t nsRecords = get16bit(aBuffer, 8);
  LOG(("TRR Decode: %d ns records (%u bytes body)\n", nsRecords, aLen));
  while (nsRecords) {
    rv = PassQName(index, aBuffer);
    if (NS_FAILED(rv)) {
      return rv;
    }

    if (aLen < (index + 8)) {
      return NS_ERROR_ILLEGAL_VALUE;
    }
    index += 2;  // type
    index += 2;  // class
    index += 4;  // ttl

    // 16 bit RDLENGTH
    if (aLen < (index + 2)) {
      return NS_ERROR_ILLEGAL_VALUE;
    }
    uint16_t RDLENGTH = get16bit(aBuffer, index);
    index += 2;
    if (aLen < (index + RDLENGTH)) {
      return NS_ERROR_ILLEGAL_VALUE;
    }
    index += RDLENGTH;
    LOG(("done with nsRecord now %u of %u\n", index, aLen));
    nsRecords--;
  }

  // additional resource records
  uint16_t arRecords = get16bit(aBuffer, 10);
  LOG(("TRR Decode: %d additional resource records (%u bytes body)\n",
       arRecords, aLen));

  while (arRecords) {
    nsAutoCString qname;
    rv = GetQname(qname, index, aBuffer, mBodySize);
    if (NS_FAILED(rv)) {
      LOG(("Bad qname for additional record"));
      return rv;
    }

    if (aLen < (index + 8)) {
      return NS_ERROR_ILLEGAL_VALUE;
    }
    uint16_t type = get16bit(aBuffer, index);
    index += 2;
    // The next two bytes encode class
    // (or udpPayloadSize when type is TRRTYPE_OPT)
    uint16_t cls = get16bit(aBuffer, index);
    index += 2;
    // The next 4 bytes encode TTL
    // (or extRCode + ednsVersion + flags when type is TRRTYPE_OPT)
    uint32_t ttl = get32bit(aBuffer, index);
    index += 4;
    // cls and ttl are unused when type is TRRTYPE_OPT

    // 16 bit RDLENGTH
    if (aLen < (index + 2)) {
      LOG(("Record too small"));
      return NS_ERROR_ILLEGAL_VALUE;
    }

    uint16_t rdlength = get16bit(aBuffer, index);
    index += 2;
    if (aLen < (index + rdlength)) {
      LOG(("rdlength too big"));
      return NS_ERROR_ILLEGAL_VALUE;
    }

    auto parseRecord = [&]() {
      LOG(("Parsing additional record type: %u", type));
      auto* entry = aAdditionalRecords.GetOrInsertNew(qname);

      switch (type) {
        case TRRTYPE_A:
          if (kDNS_CLASS_IN != cls) {
            LOG(("NOT IN - returning"));
            return;
          }
          if (rdlength != 4) {
            LOG(("TRR bad length for A (%u)\n", rdlength));
            return;
          }
          rv = entry->Add(ttl, aBuffer, index, rdlength, aAllowRFC1918);
          if (NS_FAILED(rv)) {
            LOG(
                ("TRR:DohDecode failed: local IP addresses or unknown IP "
                 "family\n"));
            return;
          }
          break;
        case TRRTYPE_AAAA:
          if (kDNS_CLASS_IN != cls) {
            LOG(("NOT IN - returning"));
            return;
          }
          if (rdlength != 16) {
            LOG(("TRR bad length for AAAA (%u)\n", rdlength));
            return;
          }
          rv = entry->Add(ttl, aBuffer, index, rdlength, aAllowRFC1918);
          if (NS_FAILED(rv)) {
            LOG(("TRR got unique/local IPv6 address!\n"));
            return;
          }
          break;
        case TRRTYPE_OPT: {  // OPT
          LOG(("Parsing opt rdlen: %u", rdlength));
          unsigned int offset = 0;
          while (offset + 2 <= rdlength) {
            uint16_t optCode = get16bit(aBuffer, index + offset);
            LOG(("optCode: %u", optCode));
            offset += 2;
            if (offset + 2 > rdlength) {
              break;
            }
            uint16_t optLen = get16bit(aBuffer, index + offset);
            LOG(("optLen: %u", optLen));
            offset += 2;
            if (offset + optLen > rdlength) {
              LOG(("offset: %u, optLen: %u, rdlen: %u", offset, optLen,
                   rdlength));
              break;
            }

            LOG(("OPT: code: %u len:%u", optCode, optLen));

            if (optCode != 15) {
              offset += optLen;
              continue;
            }

            // optCode == 15; Extended DNS error

            if (offset + 2 > rdlength || optLen < 2) {
              break;
            }
            extendedError = get16bit(aBuffer, index + offset);

            LOG(("Extended error code: %u message: %s", extendedError,
                 nsAutoCString((char*)aBuffer + index + offset + 2, optLen - 2)
                     .get()));
            offset += optLen;
          }
          break;
        }
        default:
          break;
      }
    };

    parseRecord();

    index += rdlength;
    LOG(("done with additional rr now %u of %u\n", index, aLen));
    arRecords--;
  }

  if (index != aLen) {
    LOG(("DohDecode failed to parse entire response body, %u out of %u bytes\n",
         index, aLen));
    // failed to parse 100%, do not continue
    return NS_ERROR_ILLEGAL_VALUE;
  }

  if (aType == TRRTYPE_NS && rcode != 0) {
    return NS_ERROR_UNKNOWN_HOST;
  }

  if ((aType != TRRTYPE_NS) && aCname.IsEmpty() && aResp.mAddresses.IsEmpty() &&
      aTypeResult.is<TypeRecordEmpty>()) {
    // no entries were stored!
    LOG(("TRR: No entries were stored!\n"));

    if (extendedError != UINT16_MAX &&
        StaticPrefs::network_trr_hard_fail_on_extended_error() &&
        hardFail(extendedError)) {
      return NS_ERROR_DEFINITIVE_UNKNOWN_HOST;
    }
    return NS_ERROR_UNKNOWN_HOST;
  }

  // https://tools.ietf.org/html/draft-ietf-dnsop-svcb-httpssvc-03#page-14
  // If one or more SVCB records of ServiceForm SvcRecordType are returned for
  // HOST, clients should select the highest-priority option with acceptable
  // parameters.
  if (aTypeResult.is<TypeRecordHTTPSSVC>()) {
    auto& results = aTypeResult.as<TypeRecordHTTPSSVC>();
    results.Sort();
  }

  return NS_OK;
}

//
// DohDecode() collects the TTL and the IP addresses in the response
//
nsresult DNSPacket::Decode(
    nsCString& aHost, enum TrrType aType, nsCString& aCname, bool aAllowRFC1918,
    DOHresp& aResp, TypeRecordResultType& aTypeResult,
    nsClassHashtable<nsCStringHashKey, DOHresp>& aAdditionalRecords,
    uint32_t& aTTL) {
  nsresult rv =
      DecodeInternal(aHost, aType, aCname, aAllowRFC1918, aResp, aTypeResult,
                     aAdditionalRecords, aTTL, mResponse, mBodySize);
  mStatus = rv;
  return rv;
}

}  // namespace net
}  // namespace mozilla
