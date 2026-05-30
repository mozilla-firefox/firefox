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

#include "mozpkix/pkixder.h"

#include "mozpkix/pkixutil.h"

namespace mozilla { namespace pkix { namespace der {

// Too complicated to be inline
Result
ReadTagAndGetValue(Reader& input, /*out*/ uint8_t& tag, /*out*/ Input& value)
{
  Result rv;

  rv = input.Read(tag);
  if (rv != Success) {
    return rv;
  }
  if ((tag & 0x1F) == 0x1F) {
    return Result::ERROR_BAD_DER; // high tag number form not allowed
  }

  uint16_t length;

  // The short form of length is a single byte with the high order bit set
  // to zero. The long form of length is one byte with the high order bit
  // set, followed by N bytes, where N is encoded in the lowest 7 bits of
  // the first byte.
  uint8_t length1;
  rv = input.Read(length1);
  if (rv != Success) {
    return rv;
  }
  if (!(length1 & 0x80)) {
    length = length1;
  } else if (length1 == 0x81) {
    uint8_t length2;
    rv = input.Read(length2);
    if (rv != Success) {
      return rv;
    }
    if (length2 < 128) {
      // Not shortest possible encoding
      return Result::ERROR_BAD_DER;
    }
    length = length2;
  } else if (length1 == 0x82) {
    rv = input.Read(length);
    if (rv != Success) {
      return rv;
    }
    if (length < 256) {
      // Not shortest possible encoding
      return Result::ERROR_BAD_DER;
    }
  } else {
    // We don't support lengths larger than 2^16 - 1.
    return Result::ERROR_BAD_DER;
  }

  return input.Skip(length, value);
}

Result
SignatureAlgorithmIdentifierValue(Reader& input,
                                 /*out*/ PublicKeyAlgorithm& publicKeyAlgorithm,
                                 /*out*/ DigestAlgorithm& digestAlgorithm)
{
  // RFC 5758 Section 3.2 (ECDSA with SHA-2), and RFC 3279 Section 2.2.3
  // (ECDSA with SHA-1) say that parameters must be omitted.
  //
  // RFC 4055 Section 5 and RFC 3279 Section 2.2.1 both say that parameters for
  // RSA must be encoded as NULL; we relax that requirement by allowing the
  // NULL to be omitted.
  //
  // RFC 8017 Appendix A.2.3 specifies the format of the parameters for
  // RSA-PSS. The CA/Browser Forum Baseline Requirements (as of 1.8.1)
  // specify three specific parameters that may be used with RSA-PSS:
  //  * SHA-256, MGF-1 with SHA-256, and a salt length of 32 bytes
  //  * SHA-384, MGF-1 with SHA-384, and a salt length of 48 bytes
  //  * SHA-512, MGF-1 with SHA-512, and a salt length of 64 bytes
  // as well as the corresponding specific encodings that must be used
  // for the entire AlgorithmIdentifier:
  //  * 304106092a864886f70d01010a3034a00f300d0609608648016503040201
  //    0500a11c301a06092a864886f70d010108300d0609608648016503040201
  //    0500a203020120
  //  * 304106092a864886f70d01010a3034a00f300d0609608648016503040202
  //    0500a11c301a06092a864886f70d010108300d0609608648016503040202
  //    0500a203020130
  //  * 304106092a864886f70d01010a3034a00f300d0609608648016503040203
  //    0500a11c301a06092a864886f70d010108300d0609608648016503040203
  //    0500a203020140
  // Note that these encodings include the outer SEQUENCE and length bytes;
  // in this implementation, the caller has already validated those bytes.
  // Currently these are the only sets of parameters mozilla::pkix supports.
  Reader algorithmID;
  Result rv = ExpectTagAndGetValue(input, der::OIDTag, algorithmID);
  if (rv != Success) {
    return rv;
  }
  Input algorithmParamsInput;
  rv = input.SkipToEnd(algorithmParamsInput);
  if (rv != Success) {
    return rv;
  }
  Reader algorithmParams(algorithmParamsInput);

  // RFC 5758 Section 3.2 (ecdsa-with-SHA224 is intentionally excluded)
  // python DottedOIDToCode.py ecdsa-with-SHA256 1.2.840.10045.4.3.2
  static const uint8_t ecdsa_with_SHA256[] = {
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02
  };
  // python DottedOIDToCode.py ecdsa-with-SHA384 1.2.840.10045.4.3.3
  static const uint8_t ecdsa_with_SHA384[] = {
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x03
  };
  // python DottedOIDToCode.py ecdsa-with-SHA512 1.2.840.10045.4.3.4
  static const uint8_t ecdsa_with_SHA512[] = {
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x04
  };

  // RFC 4055 Section 5 (sha224WithRSAEncryption is intentionally excluded)
  // python DottedOIDToCode.py sha256WithRSAEncryption 1.2.840.113549.1.1.11
  static const uint8_t sha256WithRSAEncryption[] = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b
  };
  // python DottedOIDToCode.py sha384WithRSAEncryption 1.2.840.113549.1.1.12
  static const uint8_t sha384WithRSAEncryption[] = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0c
  };
  // python DottedOIDToCode.py sha512WithRSAEncryption 1.2.840.113549.1.1.13
  static const uint8_t sha512WithRSAEncryption[] = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0d
  };

  // RFC 3279 Section 2.2.1
  // python DottedOIDToCode.py sha-1WithRSAEncryption 1.2.840.113549.1.1.5
  static const uint8_t sha_1WithRSAEncryption[] = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05
  };

  // NIST Open Systems Environment (OSE) Implementor's Workshop (OIW)
  // http://www.oiw.org/agreements/stable/12s-9412.txt (no longer works).
  // http://www.imc.org/ietf-pkix/old-archive-97/msg01166.html
  // We need to support this this non-PKIX OID for compatibility.
  // python DottedOIDToCode.py sha1WithRSASignature 1.3.14.3.2.29
  static const uint8_t sha1WithRSASignature[] = {
    0x2b, 0x0e, 0x03, 0x02, 0x1d
  };

  // RFC 3279 Section 2.2.3
  // python DottedOIDToCode.py ecdsa-with-SHA1 1.2.840.10045.4.1
  static const uint8_t ecdsa_with_SHA1[] = {
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x01
  };

  // RFC 8017 Appendix A.2.3
  // python DottedOIDToCode.py id-RSA-PSS 1.2.840.113549.1.1.10
  static const uint8_t id_RSA_PSS[] = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a
  };

  // NIST FIPS 204, draft-ietf-lamps-dilithium-certificates
  // python DottedOIDToCode.py id-ML-DSA-87 2.16.840.1.101.3.4.3.19
  static const uint8_t id_ML_DSA_87[] = {
    0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x13
  };

  // CA/B Forum BR 1.8.1 Section 7.1.3.2.1
  // Params for RSA-PSS with SHA-256, MGF-1 with SHA-256, and a salt length
  // of 32 bytes:
  static const uint8_t rsaPSSWithSHA256MGF1WithSHA256Salt32[] = {
    0x30, 0x34, 0xa0, 0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
    0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0xa1, 0x1c, 0x30, 0x1a, 0x06,
    0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30, 0x0d,
    0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
    0x00, 0xa2, 0x03, 0x02, 0x01, 0x20
  };
  // Params for RSA-PSS with SHA-384, MGF-1 with SHA-384, and a salt length
  // of 48 bytes:
  static const uint8_t rsaPSSWithSHA384MGF1WithSHA384Salt48[] = {
    0x30, 0x34, 0xa0, 0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
    0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0xa1, 0x1c, 0x30, 0x1a, 0x06,
    0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30, 0x0d,
    0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05,
    0x00, 0xa2, 0x03, 0x02, 0x01, 0x30
  };
  // Params for RSA-PSS with SHA-512, MGF-1 with SHA-512, and a salt length
  // of 64 bytes:
  static const uint8_t rsaPSSWithSHA512MGF1WithSHA512Salt64[] = {
    0x30, 0x34, 0xa0, 0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
    0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0xa1, 0x1c, 0x30, 0x1a, 0x06,
    0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30, 0x0d,
    0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05,
    0x00, 0xa2, 0x03, 0x02, 0x01, 0x40
  };

  // Matching is attempted based on a rough estimate of the commonality of the
  // algorithm, to minimize the number of MatchRest calls.
  if (algorithmID.MatchRest(sha256WithRSAEncryption)) {
    publicKeyAlgorithm = PublicKeyAlgorithm::RSA_PKCS1;
    digestAlgorithm = DigestAlgorithm::sha256;
  } else if (algorithmID.MatchRest(ecdsa_with_SHA256)) {
    publicKeyAlgorithm = PublicKeyAlgorithm::ECDSA;
    digestAlgorithm = DigestAlgorithm::sha256;
  } else if (algorithmID.MatchRest(sha_1WithRSAEncryption)) {
    publicKeyAlgorithm = PublicKeyAlgorithm::RSA_PKCS1;
    digestAlgorithm = DigestAlgorithm::sha1;
  } else if (algorithmID.MatchRest(ecdsa_with_SHA1)) {
    publicKeyAlgorithm = PublicKeyAlgorithm::ECDSA;
    digestAlgorithm = DigestAlgorithm::sha1;
  } else if (algorithmID.MatchRest(ecdsa_with_SHA384)) {
    publicKeyAlgorithm = PublicKeyAlgorithm::ECDSA;
    digestAlgorithm = DigestAlgorithm::sha384;
  } else if (algorithmID.MatchRest(ecdsa_with_SHA512)) {
    publicKeyAlgorithm = PublicKeyAlgorithm::ECDSA;
    digestAlgorithm = DigestAlgorithm::sha512;
  } else if (algorithmID.MatchRest(sha384WithRSAEncryption)) {
    publicKeyAlgorithm = PublicKeyAlgorithm::RSA_PKCS1;
    digestAlgorithm = DigestAlgorithm::sha384;
  } else if (algorithmID.MatchRest(sha512WithRSAEncryption)) {
    publicKeyAlgorithm = PublicKeyAlgorithm::RSA_PKCS1;
    digestAlgorithm = DigestAlgorithm::sha512;
  } else if (algorithmID.MatchRest(sha1WithRSASignature)) {
    // This old OID is recognized for compatibility (see bug 1042479).
    publicKeyAlgorithm = PublicKeyAlgorithm::RSA_PKCS1;
    digestAlgorithm = DigestAlgorithm::sha1;
  } else if (algorithmID.MatchRest(id_RSA_PSS)) {
    publicKeyAlgorithm = PublicKeyAlgorithm::RSA_PSS;
    if (algorithmParams.MatchRest(rsaPSSWithSHA256MGF1WithSHA256Salt32)) {
      digestAlgorithm = DigestAlgorithm::sha256;
    } else if (algorithmParams.MatchRest(rsaPSSWithSHA384MGF1WithSHA384Salt48)) {
      digestAlgorithm = DigestAlgorithm::sha384;
    } else if (algorithmParams.MatchRest(rsaPSSWithSHA512MGF1WithSHA512Salt64)) {
      digestAlgorithm = DigestAlgorithm::sha512;
    } else {
      return Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED;
    }
  } else if (algorithmID.MatchRest(id_ML_DSA_87)) {
    /* ML-DSA-87 is a pure signature — no pre-hash and no AlgorithmIdentifier
     * parameters (RFC: params MUST be absent). */
    publicKeyAlgorithm = PublicKeyAlgorithm::ML_DSA;
    digestAlgorithm = DigestAlgorithm::none;
  } else {
    return Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED;
  }

  // Ensure that for non-RSA-PSS, non-ML-DSA algorithms, the params are NULL or omitted.
  if (publicKeyAlgorithm != PublicKeyAlgorithm::RSA_PSS &&
      publicKeyAlgorithm != PublicKeyAlgorithm::ML_DSA) {
    if (algorithmParams.Peek(NULLTag)) {
      rv = Null(algorithmParams);
      if (rv != Success) {
        return rv;
      }
    }
    if (!algorithmParams.AtEnd()) {
      return Result::ERROR_BAD_DER;
    }
  }

  return Success;
}

Result
DigestAlgorithmIdentifier(Reader& input, /*out*/ DigestAlgorithm& algorithm)
{
  // AlgorithmIdentifier  ::=  SEQUENCE  {
  //      algorithm               OBJECT IDENTIFIER,
  //      parameters              ANY DEFINED BY algorithm OPTIONAL  }
  return der::Nested(input, SEQUENCE, [&algorithm](Reader& r) -> Result {
    Reader algorithmID;
    Result rv = ExpectTagAndGetValue(r, der::OIDTag, algorithmID);
    if (rv != Success) {
      return rv;
    }
    // The parameters are expected to be NULL or omitted.
    if (r.Peek(NULLTag)) {
      rv = Null(r);
      if (rv != Success) {
        return rv;
      }
    }
    if (!r.AtEnd()) {
      return Result::ERROR_BAD_DER;
    }

    // RFC 4055 Section 2.1
    // python DottedOIDToCode.py id-sha1 1.3.14.3.2.26
    static const uint8_t id_sha1[] = {
      0x2b, 0x0e, 0x03, 0x02, 0x1a
    };
    // python DottedOIDToCode.py id-sha256 2.16.840.1.101.3.4.2.1
    static const uint8_t id_sha256[] = {
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01
    };
    // python DottedOIDToCode.py id-sha384 2.16.840.1.101.3.4.2.2
    static const uint8_t id_sha384[] = {
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02
    };
    // python DottedOIDToCode.py id-sha512 2.16.840.1.101.3.4.2.3
    static const uint8_t id_sha512[] = {
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03
    };

    // Matching is attempted based on a rough estimate of the commonality of the
    // algorithm, to minimize the number of MatchRest calls.
    if (algorithmID.MatchRest(id_sha1)) {
      algorithm = DigestAlgorithm::sha1;
    } else if (algorithmID.MatchRest(id_sha256)) {
      algorithm = DigestAlgorithm::sha256;
    } else if (algorithmID.MatchRest(id_sha384)) {
      algorithm = DigestAlgorithm::sha384;
    } else if (algorithmID.MatchRest(id_sha512)) {
      algorithm = DigestAlgorithm::sha512;
    } else {
      return Result::ERROR_INVALID_ALGORITHM;
    }

    return Success;
  });
}

Result
SignedData(Reader& input, /*out*/ Reader& tbs,
           /*out*/ SignedDataWithSignature& signedData)
{
  Reader::Mark mark(input.GetMark());

  Result rv;
  rv = ExpectTagAndGetValue(input, SEQUENCE, tbs);
  if (rv != Success) {
    return rv;
  }

  rv = input.GetInput(mark, signedData.data);
  if (rv != Success) {
    return rv;
  }

  rv = ExpectTagAndGetValue(input, der::SEQUENCE, signedData.algorithm);
  if (rv != Success) {
    return rv;
  }

  rv = BitStringWithNoUnusedBits(input, signedData.signature);
  if (rv == Result::ERROR_BAD_DER) {
    rv = Result::ERROR_BAD_SIGNATURE;
  }
  return rv;
}

Result
BitStringWithNoUnusedBits(Reader& input, /*out*/ Input& value)
{
  Reader valueWithUnusedBits;
  Result rv = ExpectTagAndGetValue(input, BIT_STRING, valueWithUnusedBits);
  if (rv != Success) {
    return rv;
  }

  uint8_t unusedBitsAtEnd;
  if (valueWithUnusedBits.Read(unusedBitsAtEnd) != Success) {
    return Result::ERROR_BAD_DER;
  }
  // XXX: Really the constraint should be that unusedBitsAtEnd must be less
  // than 7. But, we suspect there are no real-world values in OCSP responses
  // or certificates with non-zero unused bits. It seems like NSS assumes this
  // in various places, so we enforce it too in order to simplify this code. If
  // we find compatibility issues, we'll know we're wrong and we'll have to
  // figure out how to shift the bits around.
  if (unusedBitsAtEnd != 0) {
    return Result::ERROR_BAD_DER;
  }
  return valueWithUnusedBits.SkipToEnd(value);
}

static inline Result
ReadDigit(Reader& input, /*out*/ unsigned int& value)
{
  uint8_t b;
  if (input.Read(b) != Success) {
    return Result::ERROR_INVALID_DER_TIME;
  }
  if (b < '0' || b > '9') {
    return Result::ERROR_INVALID_DER_TIME;
  }
  value = static_cast<unsigned int>(b - static_cast<uint8_t>('0'));
  return Success;
}

static inline Result
ReadTwoDigits(Reader& input, unsigned int minValue, unsigned int maxValue,
              /*out*/ unsigned int& value)
{
  unsigned int hi;
  Result rv = ReadDigit(input, hi);
  if (rv != Success) {
    return rv;
  }
  unsigned int lo;
  rv = ReadDigit(input, lo);
  if (rv != Success) {
    return rv;
  }
  value = (hi * 10) + lo;
  if (value < minValue || value > maxValue) {
    return Result::ERROR_INVALID_DER_TIME;
  }
  return Success;
}

namespace internal {

// We parse GeneralizedTime and UTCTime according to RFC 5280 and we do not
// accept all time formats allowed in the ASN.1 spec. That is,
// GeneralizedTime must always be in the format YYYYMMDDHHMMSSZ and UTCTime
// must always be in the format YYMMDDHHMMSSZ. Timezone formats of the form
// +HH:MM or -HH:MM or NOT accepted.
Result
TimeChoice(Reader& tagged, uint8_t expectedTag, /*out*/ Time& time)
{
  unsigned int days;

  Reader input;
  Result rv = ExpectTagAndGetValue(tagged, expectedTag, input);
  if (rv != Success) {
    return rv;
  }

  unsigned int yearHi;
  unsigned int yearLo;
  if (expectedTag == GENERALIZED_TIME) {
    rv = ReadTwoDigits(input, 0, 99, yearHi);
    if (rv != Success) {
      return rv;
    }
    rv = ReadTwoDigits(input, 0, 99, yearLo);
    if (rv != Success) {
      return rv;
    }
  } else if (expectedTag == UTCTime) {
    rv = ReadTwoDigits(input, 0, 99, yearLo);
    if (rv != Success) {
      return rv;
    }
    yearHi = yearLo >= 50u ? 19u : 20u;
  } else {
    return NotReached("invalid tag given to TimeChoice",
                      Result::ERROR_INVALID_DER_TIME);
  }
  unsigned int year = (yearHi * 100u) + yearLo;
  if (year < 1970u) {
    // We don't support dates before January 1, 1970 because that is the epoch.
    return Result::ERROR_INVALID_DER_TIME;
  }
  days = DaysBeforeYear(year);

  unsigned int month;
  rv = ReadTwoDigits(input, 1u, 12u, month);
  if (rv != Success) {
    return rv;
  }
  unsigned int daysInMonth;
  static const unsigned int jan = 31u;
  const unsigned int feb = ((year % 4u == 0u) &&
                           ((year % 100u != 0u) || (year % 400u == 0u)))
                         ? 29u
                         : 28u;
  static const unsigned int mar = 31u;
  static const unsigned int apr = 30u;
  static const unsigned int may = 31u;
  static const unsigned int jun = 30u;
  static const unsigned int jul = 31u;
  static const unsigned int aug = 31u;
  static const unsigned int sep = 30u;
  static const unsigned int oct = 31u;
  static const unsigned int nov = 30u;
  static const unsigned int dec = 31u;
  switch (month) {
    case 1:  daysInMonth = jan; break;
    case 2:  daysInMonth = feb; days += jan; break;
    case 3:  daysInMonth = mar; days += jan + feb; break;
    case 4:  daysInMonth = apr; days += jan + feb + mar; break;
    case 5:  daysInMonth = may; days += jan + feb + mar + apr; break;
    case 6:  daysInMonth = jun; days += jan + feb + mar + apr + may; break;
    case 7:  daysInMonth = jul; days += jan + feb + mar + apr + may + jun;
             break;
    case 8:  daysInMonth = aug; days += jan + feb + mar + apr + may + jun +
                                        jul;
             break;
    case 9:  daysInMonth = sep; days += jan + feb + mar + apr + may + jun +
                                        jul + aug;
             break;
    case 10: daysInMonth = oct; days += jan + feb + mar + apr + may + jun +
                                        jul + aug + sep;
             break;
    case 11: daysInMonth = nov; days += jan + feb + mar + apr + may + jun +
                                        jul + aug + sep + oct;
             break;
    case 12: daysInMonth = dec; days += jan + feb + mar + apr + may + jun +
                                        jul + aug + sep + oct + nov;
             break;
    default:
      return NotReached("month already bounds-checked by ReadTwoDigits",
                        Result::FATAL_ERROR_INVALID_STATE);
  }

  unsigned int dayOfMonth;
  rv = ReadTwoDigits(input, 1u, daysInMonth, dayOfMonth);
  if (rv != Success) {
    return rv;
  }
  days += dayOfMonth - 1;

  unsigned int hours;
  rv = ReadTwoDigits(input, 0u, 23u, hours);
  if (rv != Success) {
    return rv;
  }
  unsigned int minutes;
  rv = ReadTwoDigits(input, 0u, 59u, minutes);
  if (rv != Success) {
    return rv;
  }
  unsigned int seconds;
  rv = ReadTwoDigits(input, 0u, 59u, seconds);
  if (rv != Success) {
    return rv;
  }

  uint8_t b;
  if (input.Read(b) != Success) {
    return Result::ERROR_INVALID_DER_TIME;
  }
  if (b != 'Z') {
    return Result::ERROR_INVALID_DER_TIME;
  }
  if (End(input) != Success) {
    return Result::ERROR_INVALID_DER_TIME;
  }

  uint64_t totalSeconds = (static_cast<uint64_t>(days) * 24u * 60u * 60u) +
                          (static_cast<uint64_t>(hours)      * 60u * 60u) +
                          (static_cast<uint64_t>(minutes)          * 60u) +
                          seconds;

  time = TimeFromElapsedSecondsAD(totalSeconds);
  return Success;
}

Result
IntegralBytes(Reader& input, uint8_t tag,
              IntegralValueRestriction valueRestriction,
              /*out*/ Input& value,
              /*optional out*/ Input::size_type* significantBytes)
{
  Result rv = ExpectTagAndGetValue(input, tag, value);
  if (rv != Success) {
    return rv;
  }
  Reader reader(value);

  // There must be at least one byte in the value. (Zero is encoded with a
  // single 0x00 value byte.)
  uint8_t firstByte;
  rv = reader.Read(firstByte);
  if (rv != Success) {
    if (rv == Result::ERROR_BAD_DER) {
      return Result::ERROR_INVALID_INTEGER_ENCODING;
    }

    return rv;
  }

  // If there is a byte after an initial 0x00/0xFF, then the initial byte
  // indicates a positive/negative integer value with its high bit set/unset.
  bool prefixed = !reader.AtEnd() && (firstByte == 0 || firstByte == 0xff);

  if (prefixed) {
    uint8_t nextByte;
    if (reader.Read(nextByte) != Success) {
      return NotReached("Read of one byte failed but not at end.",
                        Result::FATAL_ERROR_LIBRARY_FAILURE);
    }
    if ((firstByte & 0x80) == (nextByte & 0x80)) {
      return Result::ERROR_INVALID_INTEGER_ENCODING;
    }
  }

  switch (valueRestriction) {
    case IntegralValueRestriction::MustBe0To127:
      if (value.GetLength() != 1 || (firstByte & 0x80) != 0) {
        return Result::ERROR_INVALID_INTEGER_ENCODING;
      }
      break;

    case IntegralValueRestriction::MustBePositive:
      if ((value.GetLength() == 1 && firstByte == 0) ||
          (firstByte & 0x80) != 0) {
        return Result::ERROR_INVALID_INTEGER_ENCODING;
      }
      break;

    case IntegralValueRestriction::NoRestriction:
      break;
  }

  if (significantBytes) {
    *significantBytes = value.GetLength();
    if (prefixed) {
      assert(*significantBytes > 1);
      --*significantBytes;
    }

    assert(*significantBytes > 0);
  }

  return Success;
}

// This parser will only parse values between 0..127. If this range is
// increased then callers will need to be changed.
Result
IntegralValue(Reader& input, uint8_t tag, /*out*/ uint8_t& value)
{
  // Conveniently, all the Integers that we actually have to be able to parse
  // are positive and very small. Consequently, this parser is *much* simpler
  // than a general Integer parser would need to be.
  Input valueBytes;
  Result rv = IntegralBytes(input, tag, IntegralValueRestriction::MustBe0To127,
                            valueBytes, nullptr);
  if (rv != Success) {
    return rv;
  }
  Reader valueReader(valueBytes);
  rv = valueReader.Read(value);
  if (rv != Success) {
    return NotReached("IntegralBytes already validated the value.", rv);
  }
  rv = End(valueReader);
  assert(rv == Success); // guaranteed by IntegralBytes's range checks.
  return rv;
}

} // namespace internal

Result
OptionalVersion(Reader& input, /*out*/ Version& version)
{
  static const uint8_t TAG = CONTEXT_SPECIFIC | CONSTRUCTED | 0;
  if (!input.Peek(TAG)) {
    version = Version::v1;
    return Success;
  }
  return Nested(input, TAG, [&version](Reader& value) -> Result {
    uint8_t integerValue;
    Result rv = Integer(value, integerValue);
    if (rv != Success) {
      return rv;
    }
    // XXX(bug 1031093): We shouldn't accept an explicit encoding of v1,
    // but we do here for compatibility reasons.
    switch (integerValue) {
      case static_cast<uint8_t>(Version::v3): version = Version::v3; break;
      case static_cast<uint8_t>(Version::v2): version = Version::v2; break;
      case static_cast<uint8_t>(Version::v1): version = Version::v1; break;
      case static_cast<uint8_t>(Version::v4): version = Version::v4; break;
      default:
        return Result::ERROR_BAD_DER;
    }
    return Success;
  });
}

// From RFC 5480 Appendix A:
// ECDSA-Sig-Value ::= SEQUENCE {
//   r  INTEGER,
//   s  INTEGER
// }
Result
ECDSASigValue(Input ecdsaSignature, /*out*/ Input& r, /*out*/ Input& s) {
  Reader rAndS;
  Result rv = ExpectTagAndGetValueAtEnd(ecdsaSignature, SEQUENCE, rAndS);
  if (rv != Success) {
    return rv;
  }

  Input rInput;
  Input::size_type rSignificantBytes;
  rv = PositiveInteger(rAndS, rInput, &rSignificantBytes);
  if (rv != Success) {
    return rv;
  }
  Reader rReader(rInput);
  // Address potential leading 0 byte due to DER encoding.
  if (rSignificantBytes + 1 == rInput.GetLength()) {
    rv = rReader.Skip(1);
    if (rv != Success) {
      return rv;
    }
  }
  rv = rReader.SkipToEnd(r);
  if (rv != Success) {
    return rv;
  }

  Input sInput;
  Input::size_type sSignificantBytes;
  rv = PositiveInteger(rAndS, sInput, &sSignificantBytes);
  if (rv != Success) {
    return rv;
  }
  Reader sReader(sInput);
  // Address potential leading 0 byte due to DER encoding.
  if (sSignificantBytes + 1 == sInput.GetLength()) {
    rv = sReader.Skip(1);
    if (rv != Success) {
      return rv;
    }
  }
  rv = sReader.SkipToEnd(s);
  if (rv != Success) {
    return rv;
  }

  return End(rAndS);
}

} } } // namespace mozilla::pkix::der
