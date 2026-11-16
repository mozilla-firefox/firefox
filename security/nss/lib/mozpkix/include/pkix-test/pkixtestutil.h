/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This code is made available to you under your choice of the following sets
 * of licensing terms:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/* Copyright 2013 Mozilla Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef mozilla_pkix_test_pkixtestutil_h
#define mozilla_pkix_test_pkixtestutil_h

#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>

#include "mozpkix/pkixtypes.h"

namespace mozilla {
namespace pkix {
namespace test {

class ByteString : public std::string {
 public:
  ByteString() {}
  ByteString(size_t count, uint8_t value) : std::string(count, char(value)) {}
  explicit ByteString(const uint8_t* data)
      : std::string(reinterpret_cast<const char*>(data)) {}
  ByteString(const uint8_t* data, size_t length)
      : std::string(reinterpret_cast<const char*>(data), length) {}
  ByteString operator+(const ByteString& rhs) const {
    ByteString result = *this;
    result.std::string::append(rhs);
    return result;
  }
  const uint8_t* data() const {
    return reinterpret_cast<const uint8_t*>(std::string::data());
  }
  void assign(const uint8_t* data, size_t length) {
    std::string::assign(reinterpret_cast<const char*>(data), length);
  }
  void append(const ByteString& other) { std::string::append(other); }
  void append(const uint8_t* data, size_t length) {
    std::string::append(reinterpret_cast<const char*>(data), length);
  }
  void push_back(uint8_t c) { std::string::push_back(char(c)); }
};

inline bool ENCODING_FAILED(const ByteString& bs) { return bs.empty(); }

template <size_t L>
inline ByteString BytesToByteString(const uint8_t (&bytes)[L]) {
  return ByteString(bytes, L);
}

// XXX: Ideally, we should define this instead:
//
//   template <typename T, std::size_t N>
//   constexpr inline std::size_t
//   ArrayLength(T (&)[N])
//   {
//     return N;
//   }
//
// However, we don't because not all supported compilers support constexpr,
// and we need to calculate array lengths in static_assert sometimes.
//
// XXX: Evaluates its argument twice
#define MOZILLA_PKIX_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

bool InputEqualsByteString(Input input, const ByteString& bs);
ByteString InputToByteString(Input input);

// python DottedOIDToCode.py --tlv id-kp-OCSPSigning 1.3.6.1.5.5.7.3.9
static const uint8_t tlv_id_kp_OCSPSigning[] = {0x06, 0x08, 0x2b, 0x06, 0x01,
                                                0x05, 0x05, 0x07, 0x03, 0x09};

// python DottedOIDToCode.py --tlv id-kp-serverAuth 1.3.6.1.5.5.7.3.1
static const uint8_t tlv_id_kp_serverAuth[] = {0x06, 0x08, 0x2b, 0x06, 0x01,
                                               0x05, 0x05, 0x07, 0x03, 0x01};

enum class TestDigestAlgorithmID {
  MD2,
  MD5,
  SHA1,
  SHA224,
  SHA256,
  SHA384,
  SHA512,
  // For signature algorithms without a pre-hash step (i.e. pure ML-DSA).
  NONE,
};

struct TestPublicKeyAlgorithm {
  explicit TestPublicKeyAlgorithm(const ByteString& aAlgorithmIdentifier)
      : algorithmIdentifier(aAlgorithmIdentifier) {}
  bool operator==(const TestPublicKeyAlgorithm& other) const {
    return algorithmIdentifier == other.algorithmIdentifier;
  }
  ByteString algorithmIdentifier;
};

ByteString DSS_P();
ByteString DSS_Q();
ByteString DSS_G();

TestPublicKeyAlgorithm DSS();
TestPublicKeyAlgorithm RSA_PKCS1();
TestPublicKeyAlgorithm ML_DSA(MLDSAParameterSet parameterSet);

struct TestSignatureAlgorithm {
  TestSignatureAlgorithm(const TestPublicKeyAlgorithm& publicKeyAlg,
                         TestDigestAlgorithmID digestAlg,
                         const ByteString& algorithmIdentifier, bool accepted);

  TestPublicKeyAlgorithm publicKeyAlg;
  TestDigestAlgorithmID digestAlg;
  ByteString algorithmIdentifier;
  bool accepted;
};

TestSignatureAlgorithm md2WithRSAEncryption();
TestSignatureAlgorithm md5WithRSAEncryption();
TestSignatureAlgorithm sha1WithRSAEncryption();
TestSignatureAlgorithm sha256WithRSAEncryption();
TestSignatureAlgorithm mlDSA(MLDSAParameterSet parameterSet);

// e.g. YMDHMS(2016, 12, 31, 1, 23, 45) => 2016-12-31:01:23:45 (GMT)
mozilla::pkix::Time YMDHMS(uint16_t year, uint16_t month, uint16_t day,
                           uint16_t hour, uint16_t minutes, uint16_t seconds);

ByteString TLV(uint8_t tag, size_t length, const ByteString& value);

inline ByteString TLV(uint8_t tag, const ByteString& value) {
  return TLV(tag, value.length(), value);
}

// Although we can't enforce it without relying on Cuser-defined literals,
// which aren't supported by all of our compilers yet, you should only pass
// string literals as the last parameter to the following two functions.

template <size_t N>
inline ByteString TLV(uint8_t tag, const char (&value)[N]) {
  static_assert(N > 0, "cannot have string literal of size 0");
  assert(value[N - 1] == 0);
  return TLV(tag, ByteString(reinterpret_cast<const uint8_t*>(&value), N - 1));
}

template <size_t N>
inline ByteString TLV(uint8_t tag, size_t length, const char (&value)[N]) {
  static_assert(N > 0, "cannot have string literal of size 0");
  assert(value[N - 1] == 0);
  return TLV(tag, length,
             ByteString(reinterpret_cast<const uint8_t*>(&value), N - 1));
}

ByteString Boolean(bool value);
ByteString Integer(long value);

ByteString CN(const ByteString&, uint8_t encodingTag = 0x0c /*UTF8String*/);

inline ByteString CN(const char* value,
                     uint8_t encodingTag = 0x0c /*UTF8String*/) {
  return CN(
      ByteString(reinterpret_cast<const uint8_t*>(value), std::strlen(value)),
      encodingTag);
}

ByteString OU(const ByteString&, uint8_t encodingTag = 0x0c /*UTF8String*/);

inline ByteString OU(const char* value,
                     uint8_t encodingTag = 0x0c /*UTF8String*/) {
  return OU(
      ByteString(reinterpret_cast<const uint8_t*>(value), std::strlen(value)),
      encodingTag);
}

ByteString emailAddress(const ByteString&);

inline ByteString emailAddress(const char* value) {
  return emailAddress(
      ByteString(reinterpret_cast<const uint8_t*>(value), std::strlen(value)));
}

// RelativeDistinguishedName ::=
//   SET SIZE (1..MAX) OF AttributeTypeAndValue
//
ByteString RDN(const ByteString& avas);

// Name ::= CHOICE { -- only one possibility for now --
//   rdnSequence  RDNSequence }
//
// RDNSequence ::= SEQUENCE OF RelativeDistinguishedName
//
ByteString Name(const ByteString& rdns);

inline ByteString CNToDERName(const ByteString& cn) {
  return Name(RDN(CN(cn)));
}

inline ByteString CNToDERName(const char* cn) { return Name(RDN(CN(cn))); }

// GeneralName ::= CHOICE {
//      otherName                       [0]     OtherName,
//      rfc822Name                      [1]     IA5String,
//      dNSName                         [2]     IA5String,
//      x400Address                     [3]     ORAddress,
//      directoryName                   [4]     Name,
//      ediPartyName                    [5]     EDIPartyName,
//      uniformResourceIdentifier       [6]     IA5String,
//      iPAddress                       [7]     OCTET STRING,
//      registeredID                    [8]     OBJECT IDENTIFIER }

inline ByteString RFC822Name(const ByteString& name) {
  // (2 << 6) means "context-specific", 1 is the GeneralName tag.
  return TLV((2 << 6) | 1, name);
}

template <size_t L>
inline ByteString RFC822Name(const char (&bytes)[L]) {
  return RFC822Name(
      ByteString(reinterpret_cast<const uint8_t*>(&bytes), L - 1));
}

inline ByteString DNSName(const ByteString& name) {
  // (2 << 6) means "context-specific", 2 is the GeneralName tag.
  return TLV((2 << 6) | 2, name);
}

template <size_t L>
inline ByteString DNSName(const char (&bytes)[L]) {
  return DNSName(ByteString(reinterpret_cast<const uint8_t*>(&bytes), L - 1));
}

inline ByteString DirectoryName(const ByteString& name) {
  // (2 << 6) means "context-specific", (1 << 5) means "constructed", and 4 is
  // the DirectoryName tag.
  return TLV((2 << 6) | (1 << 5) | 4, name);
}

inline ByteString IPAddress() {
  // (2 << 6) means "context-specific", 7 is the GeneralName tag.
  return TLV((2 << 6) | 7, ByteString());
}

template <size_t L>
inline ByteString IPAddress(const uint8_t (&bytes)[L]) {
  // (2 << 6) means "context-specific", 7 is the GeneralName tag.
  return TLV((2 << 6) | 7, ByteString(bytes, L));
}

// Names should be zero or more GeneralNames, like DNSName and IPAddress return,
// concatenated together.
//
// CreatedEncodedSubjectAltName(ByteString()) results in a SAN with an empty
// sequence. CreateEmptyEncodedSubjectName() results in a SAN without any
// sequence.
ByteString CreateEncodedSubjectAltName(const ByteString& names);
ByteString CreateEncodedEmptySubjectAltName();

class TestKeyPair {
 public:
  virtual ~TestKeyPair() {}

  const TestPublicKeyAlgorithm publicKeyAlg;

  // The DER encoding of the entire SubjectPublicKeyInfo structure. This is
  // what is encoded in certificates.
  const ByteString subjectPublicKeyInfo;

  // The DER encoding of subjectPublicKeyInfo.subjectPublicKey. This is what is
  // hashed to create CertIDs for OCSP.
  const ByteString subjectPublicKey;

  virtual Result SignData(const ByteString& tbs,
                          const TestSignatureAlgorithm& signatureAlgorithm,
                          /*out*/ ByteString& signature) const = 0;

  virtual TestKeyPair* Clone() const = 0;

 protected:
  TestKeyPair(const TestPublicKeyAlgorithm& publicKeyAlg,
              const ByteString& spk);
  TestKeyPair(const TestKeyPair&) = delete;
  void operator=(const TestKeyPair&) = delete;
};

TestKeyPair* CloneReusedKeyPair();
TestKeyPair* GenerateKeyPair();
TestKeyPair* GenerateDSSKeyPair();
TestKeyPair* GenerateMLDSAKeyPair(MLDSAParameterSet parameterSet);
inline void DeleteTestKeyPair(TestKeyPair* keyPair) { delete keyPair; }
typedef std::unique_ptr<TestKeyPair> ScopedTestKeyPair;

Result TestVerifyECDSASignedData(Input data, DigestAlgorithm digestAlgorithm,
                                 Input signature, Input subjectPublicKeyInfo);
Result TestVerifyRSAPKCS1SignedData(Input data, DigestAlgorithm digestAlgorithm,
                                    Input signature,
                                    Input subjectPublicKeyInfo);
Result TestVerifyMLDSASignedData(Input data, MLDSAParameterSet parameterSet,
                                 Input signature, Input subjectPublicKeyInfo);
Result TestDigestBuf(Input item, DigestAlgorithm digestAlg,
                     /*out*/ uint8_t* digestBuf, size_t digestBufLen);

// Replace one substring in item with another of the same length, but only if
// the substring was found exactly once. The "same length" restriction is
// useful for avoiding invalidating lengths encoded within the item. The
// "only once" restriction is helpful for avoiding making accidental changes.
//
// The string to search for must be 8 or more bytes long so that it is
// extremely unlikely that there will ever be any false positive matches
// in digital signatures, keys, hashes, etc.
Result TamperOnce(/*in/out*/ ByteString& item, const ByteString& from,
                  const ByteString& to);

///////////////////////////////////////////////////////////////////////////////
// Encode Certificates

enum Version { v1 = 0, v2 = 1, v3 = 2 };

// signature is assumed to be the DER encoding of an AlgorithmIdentifer. It is
// put into the signature field of the TBSCertificate. In most cases, it will
// be the same as signatureAlgorithm, which is the algorithm actually used
// to sign the certificate.
// serialNumber is assumed to be the DER encoding of an INTEGER.
//
// If extensions is null, then no extensions will be encoded. Otherwise,
// extensions must point to an array of ByteStrings, terminated with an empty
// ByteString. (If the first item of the array is empty then an empty
// Extensions sequence will be encoded.)
ByteString CreateEncodedCertificate(
    long version, const TestSignatureAlgorithm& signature,
    const ByteString& serialNumber, const ByteString& issuerNameDER,
    time_t notBefore, time_t notAfter, const ByteString& subjectNameDER,
    const TestKeyPair& subjectKeyPair,
    /*optional*/ const ByteString* extensions, const TestKeyPair& issuerKeyPair,
    const TestSignatureAlgorithm& signatureAlgorithm);

ByteString CreateEncodedSerialNumber(long value);

enum class Critical { No = 0, Yes = 1 };

ByteString CreateEncodedBasicConstraints(
    bool isCA,
    /*optional in*/ const long* pathLenConstraint, Critical critical);

// Creates a DER-encoded extKeyUsage extension with one EKU OID.
ByteString CreateEncodedEKUExtension(Input eku, Critical critical);

///////////////////////////////////////////////////////////////////////////////
// Encode OCSP responses

class OCSPResponseExtension final {
 public:
  OCSPResponseExtension();

  ByteString id;
  bool critical;
  ByteString value;
  OCSPResponseExtension* next;
};

class OCSPResponseContext final {
 public:
  OCSPResponseContext(const CertID& certID, std::time_t time);

  const CertID& certID;
  // What digest algorithm to use to produce issuerNameHash and issuerKeyHash.
  // Defaults to sha1.
  DigestAlgorithm certIDHashAlgorithm;
  // If non-empty, the sequence of bytes to use for hashAlgorithm when encoding
  // this response. If empty, the sequence of bytes corresponding to
  // certIDHashAlgorithm will be used. Defaults to empty.
  ByteString certIDHashAlgorithmEncoded;

  // TODO(bug 980538): add a way to specify what certificates are included.

  // The fields below are in the order that they appear in an OCSP response.

  enum OCSPResponseStatus {
    successful = 0,
    malformedRequest = 1,
    internalError = 2,
    tryLater = 3,
    // 4 is not used
    sigRequired = 5,
    unauthorized = 6,
  };
  uint8_t responseStatus;  // an OCSPResponseStatus or an invalid value
  bool skipResponseBytes;  // If true, don't include responseBytes

  // responderID
  ByteString signerNameDER;  // If set, responderID will use the byName
                             // form; otherwise responderID will use the
                             // byKeyHash form.

  std::time_t producedAt;

  // SingleResponse extensions (for the certID given in the constructor).
  OCSPResponseExtension* singleExtensions;
  // ResponseData extensions.
  OCSPResponseExtension* responseExtensions;
  const ByteString* trailingResponseData;  // optional; trailing data to include
                                           // at the end of the ResponseData.
  bool includeEmptyExtensions;  // If true, include the extension wrapper
                                // regardless of if there are any actual
                                // extensions.
  ScopedTestKeyPair signerKeyPair;
  TestSignatureAlgorithm signatureAlgorithm;
  bool badSignature;        // If true, alter the signature to fail verification
  const ByteString* certs;  // optional; array terminated by an empty string

  // The following fields are on a per-SingleResponse basis. In the future we
  // may support including multiple SingleResponses per response.
  enum CertStatus {
    good = 0,
    revoked = 1,
    unknown = 2,
  };
  uint8_t certStatus;          // CertStatus or an invalid value
  std::time_t revocationTime;  // For certStatus == revoked
  std::time_t thisUpdate;
  std::time_t nextUpdate;
  bool includeNextUpdate;
};

ByteString CreateEncodedOCSPResponse(OCSPResponseContext& context);
}  // namespace test
}  // namespace pkix
}  // namespace mozilla

#endif  // mozilla_pkix_test_pkixtestutil_h
