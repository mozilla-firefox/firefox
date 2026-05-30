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

#ifndef mozilla_pkix_pkixder_h
#define mozilla_pkix_pkixder_h

// Expect* functions advance the input mark and return Success if the input
// matches the given criteria; they fail with the input mark in an undefined
// state if the input does not match the criteria.
//
// Match* functions advance the input mark and return true if the input matches
// the given criteria; they return false without changing the input mark if the
// input does not match the criteria.
//
// Skip* functions unconditionally advance the input mark and return Success if
// they are able to do so; otherwise they fail with the input mark in an
// undefined state.

#include "mozpkix/Input.h"
#include "mozpkix/pkixtypes.h"

namespace mozilla {
namespace pkix {
namespace der {

enum Class : uint8_t {
  UNIVERSAL = 0 << 6,
  // APPLICATION = 1 << 6, // unused
  CONTEXT_SPECIFIC = 2 << 6,
  // PRIVATE = 3 << 6 // unused
};

enum Constructed { CONSTRUCTED = 1 << 5 };

enum Tag : uint8_t {
  BOOLEAN = UNIVERSAL | 0x01,
  INTEGER = UNIVERSAL | 0x02,
  BIT_STRING = UNIVERSAL | 0x03,
  OCTET_STRING = UNIVERSAL | 0x04,
  NULLTag = UNIVERSAL | 0x05,
  OIDTag = UNIVERSAL | 0x06,
  ENUMERATED = UNIVERSAL | 0x0a,
  UTF8String = UNIVERSAL | 0x0c,
  SEQUENCE = UNIVERSAL | CONSTRUCTED | 0x10,  // 0x30
  SET = UNIVERSAL | CONSTRUCTED | 0x11,       // 0x31
  PrintableString = UNIVERSAL | 0x13,
  TeletexString = UNIVERSAL | 0x14,
  IA5String = UNIVERSAL | 0x16,
  UTCTime = UNIVERSAL | 0x17,
  GENERALIZED_TIME = UNIVERSAL | 0x18,
};

enum class EmptyAllowed { No = 0, Yes = 1 };

Result ReadTagAndGetValue(Reader& input, /*out*/ uint8_t& tag,
                          /*out*/ Input& value);
Result End(Reader& input);

inline Result ExpectTagAndGetValue(Reader& input, uint8_t tag,
                                   /*out*/ Input& value) {
  uint8_t actualTag;
  Result rv = ReadTagAndGetValue(input, actualTag, value);
  if (rv != Success) {
    return rv;
  }
  if (tag != actualTag) {
    return Result::ERROR_BAD_DER;
  }
  return Success;
}

inline Result ExpectTagAndGetValue(Reader& input, uint8_t tag,
                                   /*out*/ Reader& value) {
  Input valueInput;
  Result rv = ExpectTagAndGetValue(input, tag, valueInput);
  if (rv != Success) {
    return rv;
  }
  return value.Init(valueInput);
}

inline Result ExpectTagAndEmptyValue(Reader& input, uint8_t tag) {
  Reader value;
  Result rv = ExpectTagAndGetValue(input, tag, value);
  if (rv != Success) {
    return rv;
  }
  return End(value);
}

inline Result ExpectTagAndSkipValue(Reader& input, uint8_t tag) {
  Input ignoredValue;
  return ExpectTagAndGetValue(input, tag, ignoredValue);
}

// This skips IMPLICIT OPTIONAL tags that are "primitive" (not constructed),
// given the number in the class of the tag (i.e. the number in the brackets in
// `issuerUniqueID  [1]  IMPLICIT UniqueIdentifier OPTIONAL`).
inline Result SkipOptionalImplicitPrimitiveTag(Reader& input,
                                               uint8_t numberInClass) {
  if (input.Peek(CONTEXT_SPECIFIC | numberInClass)) {
    return ExpectTagAndSkipValue(input, CONTEXT_SPECIFIC | numberInClass);
  }
  return Success;
}

// Like ExpectTagAndGetValue, except the output Input will contain the
// encoded tag and length along with the value.
inline Result ExpectTagAndGetTLV(Reader& input, uint8_t tag,
                                 /*out*/ Input& tlv) {
  Reader::Mark mark(input.GetMark());
  Result rv = ExpectTagAndSkipValue(input, tag);
  if (rv != Success) {
    return rv;
  }
  return input.GetInput(mark, tlv);
}

inline Result End(Reader& input) {
  if (!input.AtEnd()) {
    return Result::ERROR_BAD_DER;
  }

  return Success;
}

template <typename Decoder>
inline Result Nested(Reader& input, uint8_t tag, Decoder decoder) {
  Reader nested;
  Result rv = ExpectTagAndGetValue(input, tag, nested);
  if (rv != Success) {
    return rv;
  }
  rv = decoder(nested);
  if (rv != Success) {
    return rv;
  }
  return End(nested);
}

template <typename Decoder>
inline Result Nested(Reader& input, uint8_t outerTag, uint8_t innerTag,
                     Decoder decoder) {
  Reader nestedInput;
  Result rv = ExpectTagAndGetValue(input, outerTag, nestedInput);
  if (rv != Success) {
    return rv;
  }
  rv = Nested(nestedInput, innerTag, decoder);
  if (rv != Success) {
    return rv;
  }
  return End(nestedInput);
}

// This can be used to decode constructs like this:
//
//     ...
//     foos SEQUENCE OF Foo,
//     ...
//     Foo ::= SEQUENCE {
//     }
//
// using code like this:
//
//    Result Foo(Reader& r) { /*...*/ }
//
//    rv = der::NestedOf(input, der::SEQEUENCE, der::SEQUENCE, Foo);
//
// or:
//
//    Result Bar(Reader& r, int value) { /*...*/ }
//
//    int value = /*...*/;
//
//    rv = der::NestedOf(input, der::SEQUENCE, [value](Reader& r) {
//      return Bar(r, value);
//    });
//
// In these examples the function will get called once for each element of
// foos.
//
template <typename Decoder>
inline Result NestedOf(Reader& input, uint8_t outerTag, uint8_t innerTag,
                       EmptyAllowed mayBeEmpty, Decoder decoder) {
  Reader inner;
  Result rv = ExpectTagAndGetValue(input, outerTag, inner);
  if (rv != Success) {
    return rv;
  }

  if (inner.AtEnd()) {
    if (mayBeEmpty != EmptyAllowed::Yes) {
      return Result::ERROR_BAD_DER;
    }
    return Success;
  }

  do {
    rv = Nested(inner, innerTag, decoder);
    if (rv != Success) {
      return rv;
    }
  } while (!inner.AtEnd());

  return Success;
}

// Often, a function will need to decode an Input or Reader that contains
// DER-encoded data wrapped in a SEQUENCE (or similar) with nothing after it.
// This function reduces the boilerplate necessary for stripping the outermost
// SEQUENCE (or similar) and ensuring that nothing follows it.
inline Result ExpectTagAndGetValueAtEnd(Reader& outer, uint8_t expectedTag,
                                        /*out*/ Reader& inner) {
  Result rv = der::ExpectTagAndGetValue(outer, expectedTag, inner);
  if (rv != Success) {
    return rv;
  }
  return der::End(outer);
}

// Similar to the above, but takes an Input instead of a Reader&.
inline Result ExpectTagAndGetValueAtEnd(Input outer, uint8_t expectedTag,
                                        /*out*/ Reader& inner) {
  Reader outerReader(outer);
  return ExpectTagAndGetValueAtEnd(outerReader, expectedTag, inner);
}

// Universal types

namespace internal {

enum class IntegralValueRestriction {
  NoRestriction,
  MustBePositive,
  MustBe0To127,
};

Result IntegralBytes(
    Reader& input, uint8_t tag, IntegralValueRestriction valueRestriction,
    /*out*/ Input& value,
    /*optional out*/ Input::size_type* significantBytes = nullptr);

// This parser will only parse values between 0..127. If this range is
// increased then callers will need to be changed.
Result IntegralValue(Reader& input, uint8_t tag, /*out*/ uint8_t& value);

}  // namespace internal

Result BitStringWithNoUnusedBits(Reader& input, /*out*/ Input& value);

inline Result Boolean(Reader& input, /*out*/ bool& value) {
  Reader valueReader;
  Result rv = ExpectTagAndGetValue(input, BOOLEAN, valueReader);
  if (rv != Success) {
    return rv;
  }

  uint8_t intValue;
  rv = valueReader.Read(intValue);
  if (rv != Success) {
    return rv;
  }
  rv = End(valueReader);
  if (rv != Success) {
    return rv;
  }
  switch (intValue) {
    case 0:
      value = false;
      return Success;
    case 0xFF:
      value = true;
      return Success;
    default:
      return Result::ERROR_BAD_DER;
  }
}

// This is for BOOLEAN DEFAULT FALSE.
// The standard stipulates that "The encoding of a set value or sequence value
// shall not include an encoding for any component value which is equal to its
// default value." However, it appears to be common that other libraries
// incorrectly include the value of a BOOLEAN even when it's equal to the
// default value, so we allow invalid explicit encodings here.
inline Result OptionalBoolean(Reader& input, /*out*/ bool& value) {
  value = false;
  if (input.Peek(BOOLEAN)) {
    Result rv = Boolean(input, value);
    if (rv != Success) {
      return rv;
    }
  }
  return Success;
}

// This parser will only parse values between 0..127. If this range is
// increased then callers will need to be changed.
inline Result Enumerated(Reader& input, uint8_t& value) {
  return internal::IntegralValue(input, ENUMERATED | 0, value);
}

namespace internal {

// internal::TimeChoice implements the shared functionality of GeneralizedTime
// and TimeChoice. tag must be either UTCTime or GENERALIZED_TIME.
//
// Only times from 1970-01-01-00:00:00 onward are accepted, in order to
// eliminate the chance for complications in converting times to traditional
// time formats that start at 1970.
Result TimeChoice(Reader& input, uint8_t tag, /*out*/ Time& time);

}  // namespace internal

// Only times from 1970-01-01-00:00:00 onward are accepted, in order to
// eliminate the chance for complications in converting times to traditional
// time formats that start at 1970.
inline Result GeneralizedTime(Reader& input, /*out*/ Time& time) {
  return internal::TimeChoice(input, GENERALIZED_TIME, time);
}

// Only times from 1970-01-01-00:00:00 onward are accepted, in order to
// eliminate the chance for complications in converting times to traditional
// time formats that start at 1970.
inline Result TimeChoice(Reader& input, /*out*/ Time& time) {
  uint8_t expectedTag = input.Peek(UTCTime) ? UTCTime : GENERALIZED_TIME;
  return internal::TimeChoice(input, expectedTag, time);
}

// Parse a DER integer value into value. Empty values, negative values, and
// zero are rejected. If significantBytes is not null, then it will be set to
// the number of significant bytes in the value (the length of the value, less
// the length of any leading padding), which is useful for key size checks.
inline Result PositiveInteger(
    Reader& input, /*out*/ Input& value,
    /*optional out*/ Input::size_type* significantBytes = nullptr) {
  return internal::IntegralBytes(
      input, INTEGER, internal::IntegralValueRestriction::MustBePositive, value,
      significantBytes);
}

// This parser will only parse values between 0..127. If this range is
// increased then callers will need to be changed.
inline Result Integer(Reader& input, /*out*/ uint8_t& value) {
  return internal::IntegralValue(input, INTEGER, value);
}

// This parser will only parse values between 0..127. If this range is
// increased then callers will need to be changed. The default value must be
// -1; defaultValue is only a parameter to make it clear in the calling code
// what the default value is.
inline Result OptionalInteger(Reader& input, long defaultValue,
                              /*out*/ long& value) {
  // If we need to support a different default value in the future, we need to
  // test that parsedValue != defaultValue.
  if (defaultValue != -1) {
    return Result::FATAL_ERROR_INVALID_ARGS;
  }

  if (!input.Peek(INTEGER)) {
    value = defaultValue;
    return Success;
  }

  uint8_t parsedValue;
  Result rv = Integer(input, parsedValue);
  if (rv != Success) {
    return rv;
  }
  value = parsedValue;
  return Success;
}

inline Result Null(Reader& input) {
  return ExpectTagAndEmptyValue(input, NULLTag);
}

template <uint8_t Len>
Result OID(Reader& input, const uint8_t (&expectedOid)[Len]) {
  Reader value;
  Result rv = ExpectTagAndGetValue(input, OIDTag, value);
  if (rv != Success) {
    return rv;
  }
  if (!value.MatchRest(expectedOid)) {
    return Result::ERROR_BAD_DER;
  }
  return Success;
}

// PKI-specific types

inline Result CertificateSerialNumber(Reader& input, /*out*/ Input& value) {
  // http://tools.ietf.org/html/rfc5280#section-4.1.2.2:
  //
  // * "The serial number MUST be a positive integer assigned by the CA to
  //   each certificate."
  // * "Certificate users MUST be able to handle serialNumber values up to 20
  //   octets. Conforming CAs MUST NOT use serialNumber values longer than 20
  //   octets."
  // * "Note: Non-conforming CAs may issue certificates with serial numbers
  //   that are negative or zero.  Certificate users SHOULD be prepared to
  //   gracefully handle such certificates."
  return internal::IntegralBytes(
      input, INTEGER, internal::IntegralValueRestriction::NoRestriction, value);
}

// x.509 and OCSP both use this same version numbering scheme, though OCSP
// only supports v1.
enum class Version { v1 = 0, v2 = 1, v3 = 2, v4 = 3, Uninitialized = 255 };

// X.509 Certificate and OCSP ResponseData both use
// "[0] EXPLICIT Version DEFAULT v1". Although an explicit encoding of v1 is
// illegal, we support it because some real-world OCSP responses explicitly
// encode it.
Result OptionalVersion(Reader& input, /*out*/ Version& version);

template <typename ExtensionHandler>
inline Result OptionalExtensions(Reader& input, uint8_t tag,
                                 ExtensionHandler extensionHandler) {
  if (!input.Peek(tag)) {
    return Success;
  }

  return Nested(input, tag, [extensionHandler](Reader& tagged) {
    // Extensions ::= SEQUENCE SIZE (1..MAX) OF Extension
    //
    // TODO(bug 997994): According to the specification, there should never be
    // an empty sequence of extensions but we've found OCSP responses that have
    // that (see bug 991898).
    return NestedOf(
        tagged, SEQUENCE, SEQUENCE, EmptyAllowed::Yes,
        [extensionHandler](Reader& extension) -> Result {
          // Extension  ::=  SEQUENCE  {
          //      extnID      OBJECT IDENTIFIER,
          //      critical    BOOLEAN DEFAULT FALSE,
          //      extnValue   OCTET STRING
          //      }
          Reader extnID;
          Result rv = ExpectTagAndGetValue(extension, OIDTag, extnID);
          if (rv != Success) {
            return rv;
          }
          bool critical;
          rv = OptionalBoolean(extension, critical);
          if (rv != Success) {
            return rv;
          }
          Input extnValue;
          rv = ExpectTagAndGetValue(extension, OCTET_STRING, extnValue);
          if (rv != Success) {
            return rv;
          }
          bool understood = false;
          rv = extensionHandler(extnID, extnValue, critical, understood);
          if (rv != Success) {
            return rv;
          }
          if (critical && !understood) {
            return Result::ERROR_UNKNOWN_CRITICAL_EXTENSION;
          }
          return Success;
        });
  });
}

Result DigestAlgorithmIdentifier(Reader& input,
                                 /*out*/ DigestAlgorithm& algorithm);

enum class PublicKeyAlgorithm { RSA_PKCS1, RSA_PSS, ECDSA, ML_DSA };

Result SignatureAlgorithmIdentifierValue(
    Reader& input,
    /*out*/ PublicKeyAlgorithm& publicKeyAlgorithm,
    /*out*/ DigestAlgorithm& digestAlgorithm);

struct SignedDataWithSignature final {
 public:
  Input data;
  Input algorithm;
  Input signature;

  void operator=(const SignedDataWithSignature&) = delete;
};

// Parses a SEQUENCE into tbs and then parses an AlgorithmIdentifier followed
// by a BIT STRING into signedData. This handles the commonality between
// parsing the signed/signature fields of certificates and OCSP responses. In
// the case of an OCSP response, the caller needs to parse the certs
// separately.
//
// Note that signatureAlgorithm is NOT parsed or validated.
//
// Certificate  ::=  SEQUENCE  {
//        tbsCertificate       TBSCertificate,
//        signatureAlgorithm   AlgorithmIdentifier,
//        signatureValue       BIT STRING  }
//
// BasicOCSPResponse       ::= SEQUENCE {
//    tbsResponseData      ResponseData,
//    signatureAlgorithm   AlgorithmIdentifier,
//    signature            BIT STRING,
//    certs            [0] EXPLICIT SEQUENCE OF Certificate OPTIONAL }
Result SignedData(Reader& input, /*out*/ Reader& tbs,
                  /*out*/ SignedDataWithSignature& signedDataWithSignature);

// Parses an ECDSASigValue (RFC 5480) into its components r and s.
Result ECDSASigValue(Input ecdsaSignature, /*out*/ Input& r, /*out*/ Input& s);
}  // namespace der
}  // namespace pkix
}  // namespace mozilla

#endif  // mozilla_pkix_pkixder_h
