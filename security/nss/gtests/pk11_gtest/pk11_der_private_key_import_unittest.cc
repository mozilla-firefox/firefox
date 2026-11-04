/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <climits>
#include <memory>
#include "keyhi.h"
#include "nss.h"
#include "pk11pub.h"
#include "secutil.h"

#include "gtest/gtest.h"
#include "nss_scoped_ptrs.h"
#include "pk11_keygen.h"
#include "pq_pkcs8.h"
#include "pk11_import.h"

namespace nss_test {

const std::vector<uint8_t> kValidP256Key = {
    0x30, 0x81, 0x87, 0x02, 0x01, 0x00, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86,
    0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d,
    0x03, 0x01, 0x07, 0x04, 0x6d, 0x30, 0x6b, 0x02, 0x01, 0x01, 0x04, 0x20,
    0xc9, 0xaf, 0xa9, 0xd8, 0x45, 0xba, 0x75, 0x16, 0x6b, 0x5c, 0x21, 0x57,
    0x67, 0xb1, 0xd6, 0x93, 0x4e, 0x50, 0xc3, 0xdb, 0x36, 0xe8, 0x9b, 0x12,
    0x7b, 0x8a, 0x62, 0x2b, 0x12, 0x0f, 0x67, 0x21, 0xa1, 0x44, 0x03, 0x42,
    0x00, 0x04, 0x60, 0xfe, 0xd4, 0xba, 0x25, 0x5a, 0x9d, 0x31, 0xc9, 0x61,
    0xeb, 0x74, 0xc6, 0x35, 0x6d, 0x68, 0xc0, 0x49, 0xb8, 0x92, 0x3b, 0x61,
    0xfa, 0x6c, 0xe6, 0x69, 0x62, 0x2e, 0x60, 0xf2, 0x9f, 0xb6, 0x79, 0x03,
    0xfe, 0x10, 0x08, 0xb8, 0xbc, 0x99, 0xa4, 0x1a, 0xe9, 0xe9, 0x56, 0x28,
    0xbc, 0x64, 0xf2, 0xf1, 0xb2, 0x0c, 0x2d, 0x7e, 0x9f, 0x51, 0x77, 0xa3,
    0xc2, 0x94, 0xd4, 0x46, 0x22, 0x99};

const std::vector<uint8_t> kValidRSAKey = {
    // 512-bit RSA private key (PKCS#8)
    0x30, 0x82, 0x01, 0x54, 0x02, 0x01, 0x00, 0x30, 0x0d, 0x06, 0x09, 0x2a,
    0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x04, 0x82,
    0x01, 0x3e, 0x30, 0x82, 0x01, 0x3a, 0x02, 0x01, 0x00, 0x02, 0x41, 0x00,
    0xa2, 0x40, 0xce, 0xb5, 0x4e, 0x70, 0xdc, 0x14, 0x82, 0x5b, 0x58, 0x7d,
    0x2f, 0x5d, 0xfd, 0x46, 0x3c, 0x4b, 0x82, 0x50, 0xb6, 0x96, 0x00, 0x4a,
    0x1a, 0xca, 0xaf, 0xe4, 0x9b, 0xcf, 0x38, 0x4a, 0x46, 0xaa, 0x9f, 0xb4,
    0xd9, 0xc7, 0xee, 0x88, 0xe9, 0xef, 0x0a, 0x31, 0x5f, 0x53, 0x86, 0x8f,
    0x63, 0x68, 0x0b, 0x58, 0x34, 0x72, 0x49, 0xba, 0xed, 0xd9, 0x34, 0x15,
    0x16, 0xc4, 0xca, 0xb7, 0x02, 0x03, 0x01, 0x00, 0x01, 0x02, 0x40, 0x34,
    0xe6, 0xdc, 0x7e, 0xd0, 0xec, 0x8b, 0x55, 0x44, 0x8b, 0x73, 0xf6, 0x9d,
    0x13, 0x10, 0x19, 0x6e, 0x5f, 0x50, 0x45, 0xf0, 0xc2, 0x47, 0xa5, 0xe1,
    0xc6, 0x64, 0x43, 0x2d, 0x6a, 0x0a, 0xf7, 0xe7, 0xda, 0x40, 0xb8, 0x3a,
    0xf0, 0x47, 0xdd, 0x01, 0xf5, 0xe0, 0xa9, 0x0e, 0x47, 0xc2, 0x24, 0xd7,
    0xb5, 0x13, 0x3a, 0x35, 0x4d, 0x11, 0xaa, 0x50, 0x03, 0xb3, 0xe8, 0x54,
    0x6c, 0x99, 0x01, 0x02, 0x21, 0x00, 0xcd, 0xb2, 0xd7, 0xa7, 0x43, 0x5b,
    0xcb, 0x45, 0xe5, 0x0e, 0x86, 0xf6, 0xc1, 0x4e, 0x97, 0xed, 0x78, 0x1f,
    0x09, 0x56, 0xcd, 0x26, 0xe6, 0xf7, 0x5e, 0xd9, 0xfc, 0x88, 0x12, 0x5f,
    0x84, 0x07, 0x02, 0x21, 0x00, 0xc9, 0xee, 0x30, 0xaf, 0x6c, 0xb9, 0x5a,
    0xc9, 0xc1, 0x14, 0x9e, 0xd8, 0x4b, 0x33, 0x38, 0x48, 0x17, 0x41, 0x35,
    0x94, 0x09, 0xf3, 0x69, 0xc4, 0x97, 0xbe, 0x17, 0x7d, 0x95, 0x0f, 0xb7,
    0xd1, 0x02, 0x21, 0x00, 0x8b, 0x0e, 0xf9, 0x8d, 0x61, 0x13, 0x20, 0x63,
    0x9b, 0x0b, 0x6c, 0x20, 0x4a, 0xe4, 0xa7, 0xfe, 0xe8, 0xf3, 0x0a, 0x6c,
    0x3c, 0xfa, 0xac, 0xaf, 0xd4, 0xd6, 0xc7, 0x4a, 0xf2, 0x28, 0xd2, 0x67,
    0x02, 0x20, 0x6b, 0x0e, 0x1d, 0xbf, 0x93, 0x5b, 0xbd, 0x77, 0x43, 0x27,
    0x24, 0x83, 0xb5, 0x72, 0xa5, 0x3f, 0x0b, 0x1d, 0x26, 0x43, 0xa2, 0xf6,
    0xea, 0xb7, 0x30, 0x5f, 0xb6, 0x62, 0x7c, 0xf9, 0x85, 0x51, 0x02, 0x20,
    0x3d, 0x22, 0x63, 0x15, 0x6b, 0x32, 0x41, 0x46, 0x44, 0x78, 0xb7, 0x13,
    0xeb, 0x85, 0x4c, 0x4f, 0x6b, 0x3e, 0xf0, 0x52, 0xf0, 0x46, 0x3b, 0x65,
    0xd8, 0x21, 0x7d, 0xae, 0xc0, 0x09, 0x98, 0x34};

const std::vector<uint8_t> kInvalidLengthKey = {
    0x30, 0x1b,        // SEQUENCE(len=27)
    0x02, 0x01, 0x00,  // INT(len=1) = 0
    0x30, 0x13,        // SEQUENCE(len=19)
    0x06, 0x07,        // OID(len=7)
    // dhPublicKey (1.2.840.10046.2.1)
    0x2a, 0x86, 0x48, 0xce, 0x3e, 0x02, 0x01, 0x06, 0x08,  // OID(len=8)
    // prime256v1 (1.2.840.10045.3.1.7) */
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x04,
    0x00  // OCTET STRING(len=0)
};

// Ed25519 PKCS#8 PrivateKeyInfo with non-empty algorithm parameters.
// RFC 8410 section 3 says parameters MUST be absent for Ed25519, so NSS rejects
// this at the parameters-length check in PK11_ImportPrivateKeyInfoAndReturnKey.
//
//   SEQUENCE {
//     INTEGER 0
//     SEQUENCE {
//       OBJECT IDENTIFIER 1.3.101.112 (Ed25519)
//       NULL                            <-- params.len != 0, triggers rejection
//     }
//     OCTET STRING { OCTET STRING { 32 zero bytes } }
//   }
const std::vector<uint8_t> kEd25519WithNonEmptyAlgorithmParams = {
    0x30, 0x30,                    // SEQUENCE(48)
    0x02, 0x01, 0x00,              // INTEGER(0) -- version
    0x30, 0x07,                    // SEQUENCE(7) -- AlgorithmIdentifier
    0x06, 0x03, 0x2b, 0x65, 0x70,  // OID 1.3.101.112 (Ed25519)
    0x05, 0x00,  // NULL -- parameters must be absent per RFC 8410
    0x04, 0x22,  // OCTET STRING(34) -- privateKey
    0x04, 0x20,  // OCTET STRING(32) -- CurvePrivateKey
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// kValidP256Key with the optional [1] publicKey field removed. Without it
// pk11pk12.c leaves CKA_NSS_DB unset, so softoken has to derive the point
// itself in sftk_mkPrivKey().
const std::vector<uint8_t> kValidP256KeyNoPublicKey = {
    0x30, 0x41,                                            // SEQUENCE(65)
    0x02, 0x01, 0x00,                                      // INTEGER 0
    0x30, 0x13,                                            // SEQUENCE(19)
    0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01,  // id-ecPublicKey
    0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07,  // prime256v1
    0x04, 0x27,        // OCTET STRING(39) -- privateKey
    0x30, 0x25,        // SEQUENCE(37) -- ECPrivateKey
    0x02, 0x01, 0x01,  // INTEGER 1 -- version
    0x04, 0x20,        // OCTET STRING(32) -- privateKey
    0xc9, 0xaf, 0xa9, 0xd8, 0x45, 0xba, 0x75, 0x16, 0x6b, 0x5c, 0x21,
    0x57, 0x67, 0xb1, 0xd6, 0x93, 0x4e, 0x50, 0xc3, 0xdb, 0x36, 0xe8,
    0x9b, 0x12, 0x7b, 0x8a, 0x62, 0x2b, 0x12, 0x0f, 0x67, 0x21};

const std::vector<uint8_t> kValidP256PublicKey = {
    0x04, 0x60, 0xfe, 0xd4, 0xba, 0x25, 0x5a, 0x9d, 0x31, 0xc9, 0x61,
    0xeb, 0x74, 0xc6, 0x35, 0x6d, 0x68, 0xc0, 0x49, 0xb8, 0x92, 0x3b,
    0x61, 0xfa, 0x6c, 0xe6, 0x69, 0x62, 0x2e, 0x60, 0xf2, 0x9f, 0xb6,
    0x79, 0x03, 0xfe, 0x10, 0x08, 0xb8, 0xbc, 0x99, 0xa4, 0x1a, 0xe9,
    0xe9, 0x56, 0x28, 0xbc, 0x64, 0xf2, 0xf1, 0xb2, 0x0c, 0x2d, 0x7e,
    0x9f, 0x51, 0x77, 0xa3, 0xc2, 0x94, 0xd4, 0x46, 0x22, 0x99};

// P-256 with a 31-byte privateKey OCTET STRING rather than the 32 bytes RFC
// 5915 requires, and no [1] publicKey field. The missing publicKey sends
// sftk_mkPrivKey() down the derive path, where the short scalar makes
// EC_DerivePublicKey() fail on ec_secp256r1_pt_mul()'s k->len check.
const std::vector<uint8_t> kP256ShortScalarNoPublicKey = {
    0x30, 0x40,                                            // SEQUENCE(64)
    0x02, 0x01, 0x00,                                      // INTEGER 0
    0x30, 0x13,                                            // SEQUENCE(19)
    0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01,  // id-ecPublicKey
    0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07,  // prime256v1
    0x04, 0x26,        // OCTET STRING(38) -- privateKey
    0x30, 0x24,        // SEQUENCE(36) -- ECPrivateKey
    0x02, 0x01, 0x01,  // INTEGER 1 -- version
    0x04, 0x1f,        // OCTET STRING(31) -- one byte short
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};

// A 1024-bit DSA PrivateKeyInfo. A DSAPrivateKey is just the INTEGER x, so
// there is never a public value in the encoding and the token always has to
// regenerate y = g^x mod p for us.
const std::vector<uint8_t> kDsa1024KeyNoPublicKey = {
    0x30, 0x82, 0x01, 0x4b, 0x02, 0x01, 0x00, 0x30, 0x82, 0x01, 0x2b, 0x06,
    0x07, 0x2a, 0x86, 0x48, 0xce, 0x38, 0x04, 0x01, 0x30, 0x82, 0x01, 0x1e,
    0x02, 0x81, 0x81, 0x00, 0x84, 0x89, 0xf5, 0xd0, 0xf2, 0xd9, 0x1b, 0x3c,
    0x1f, 0xb0, 0x17, 0x5c, 0xd1, 0xeb, 0xf6, 0xef, 0x46, 0x0a, 0x6b, 0xf7,
    0x17, 0x8a, 0x45, 0x98, 0x08, 0x58, 0xa8, 0xf7, 0x61, 0x52, 0x6e, 0x41,
    0xb4, 0x24, 0x05, 0x53, 0xbe, 0x60, 0x2a, 0x6a, 0xe8, 0xd1, 0x77, 0xaf,
    0xf3, 0x94, 0x7d, 0x30, 0xe1, 0xd8, 0xf4, 0x5a, 0x28, 0x97, 0x88, 0x3e,
    0x88, 0x1a, 0x53, 0x9f, 0x92, 0x57, 0xb7, 0x34, 0x50, 0x58, 0x89, 0xcb,
    0x59, 0x46, 0xf2, 0x3a, 0xc2, 0x68, 0x39, 0xa6, 0x2e, 0xd9, 0x8a, 0x95,
    0x3b, 0x22, 0x3c, 0x29, 0x83, 0x7b, 0xc3, 0xc2, 0x75, 0xcb, 0x4c, 0x0e,
    0x87, 0x67, 0xd4, 0xf9, 0x4d, 0xfe, 0x55, 0xce, 0xd9, 0x96, 0xa5, 0xd9,
    0x8c, 0xe7, 0xf0, 0x2c, 0x9d, 0x75, 0x96, 0xdf, 0x5a, 0xd6, 0x6d, 0x08,
    0x8f, 0x5c, 0x10, 0x83, 0x94, 0x78, 0x4b, 0x81, 0xf3, 0xf8, 0xa9, 0xf9,
    0x02, 0x15, 0x00, 0x9e, 0x52, 0x2b, 0xcd, 0xb8, 0xed, 0x8d, 0x22, 0x43,
    0xfb, 0xcc, 0xa7, 0x38, 0xf1, 0xde, 0xa2, 0x74, 0xcc, 0x71, 0xaf, 0x02,
    0x81, 0x80, 0x45, 0x5f, 0x19, 0xa6, 0xe7, 0x07, 0xcb, 0x55, 0x05, 0x96,
    0x3d, 0x88, 0x85, 0x46, 0x8f, 0x3a, 0xa3, 0xc1, 0x74, 0x5b, 0xc9, 0x61,
    0xb9, 0x05, 0x69, 0xf9, 0x94, 0xe4, 0xb5, 0x9b, 0x4e, 0x0f, 0x79, 0x61,
    0x7d, 0xd1, 0x32, 0xbf, 0x06, 0x53, 0xef, 0x9a, 0x93, 0xc1, 0x69, 0xf4,
    0x85, 0x7d, 0x53, 0x55, 0x22, 0x37, 0xf3, 0xd6, 0xa1, 0x6c, 0x7c, 0x82,
    0x48, 0x5c, 0x9d, 0x25, 0xea, 0x2a, 0x34, 0xdd, 0x74, 0x15, 0x40, 0x69,
    0x0e, 0xa4, 0xa5, 0x21, 0xda, 0x17, 0xb8, 0xd4, 0xf9, 0x37, 0x7e, 0xa4,
    0x98, 0xd0, 0x8d, 0x2d, 0x89, 0x15, 0x20, 0x2a, 0xe2, 0x0e, 0x07, 0x1d,
    0xc5, 0xdf, 0x79, 0x0e, 0xf0, 0x2a, 0xbd, 0x16, 0x77, 0x91, 0xa6, 0xb2,
    0xb0, 0xe5, 0xee, 0x17, 0x04, 0x30, 0xac, 0x71, 0xbc, 0x93, 0xca, 0xf5,
    0xe0, 0x75, 0x05, 0xd9, 0x71, 0x4f, 0xb4, 0x58, 0x8b, 0xdd, 0x04, 0x17,
    0x02, 0x15, 0x00, 0x87, 0x43, 0x3b, 0x76, 0x62, 0x12, 0x92, 0xfa, 0xcc,
    0xd4, 0x64, 0xaa, 0x12, 0xc5, 0x38, 0xc9, 0x1b, 0x72, 0x24, 0x84};

// y for the key above, as CKA_VALUE of the public key: a bare unsigned
// big-endian integer, which is what PK11_MakeIDFromPubKey hashes.
const std::vector<uint8_t> kDsa1024PublicValue = {
    0x64, 0x06, 0x6b, 0x54, 0xd4, 0x6d, 0x23, 0xbf, 0x66, 0x55, 0xff, 0x95,
    0xbe, 0x9d, 0x7b, 0x4b, 0x15, 0x62, 0x33, 0x94, 0xe4, 0xe1, 0xaa, 0xa4,
    0x85, 0x2e, 0xea, 0x2d, 0x1d, 0xcc, 0xa8, 0xd1, 0x39, 0xb1, 0x3b, 0x64,
    0xf8, 0xc3, 0x9c, 0xed, 0x6f, 0x56, 0xac, 0xba, 0x71, 0x61, 0x0c, 0x0f,
    0x4c, 0xcb, 0x81, 0x9e, 0x03, 0x18, 0x84, 0xd0, 0xf9, 0x69, 0xeb, 0x50,
    0x6c, 0xd4, 0x38, 0x03, 0x67, 0x68, 0xe6, 0xda, 0xfc, 0x60, 0xe1, 0x61,
    0x0f, 0x5f, 0x91, 0xa4, 0x75, 0xe7, 0x87, 0x5e, 0x45, 0xa5, 0x48, 0xc8,
    0xf0, 0x44, 0xff, 0x05, 0xb4, 0xa3, 0x1f, 0x94, 0x49, 0xcf, 0xb0, 0xff,
    0x7f, 0xf7, 0x13, 0x3c, 0x12, 0xa6, 0x2b, 0xa6, 0xea, 0xce, 0x75, 0xab,
    0x42, 0xdd, 0x62, 0x73, 0xe8, 0x3e, 0x9a, 0x39, 0xa7, 0x0e, 0xce, 0xa6,
    0x88, 0x56, 0xd5, 0x07, 0xf0, 0x87, 0xcf, 0x7f};

// A P-256 key whose uncompressed public point 04 || X || Y happens to start
// 04 3f, so the raw 65 bytes are also a well formed DER OCTET STRING of
// length 0x3f. Roughly one EC key in 256 looks like this, which is what made
// the SECKEY_ConvertToPublicKey heuristic strip two bytes at random. The [1]
// publicKey field is omitted so the token has to recompute the point.
const std::vector<uint8_t> kP256AmbiguousPointKeyNoPublicKey = {
    0x30, 0x41, 0x02, 0x01, 0x00, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48,
    0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03,
    0x01, 0x07, 0x04, 0x27, 0x30, 0x25, 0x02, 0x01, 0x01, 0x04, 0x20, 0xb2,
    0x7f, 0xa3, 0xc3, 0x08, 0x09, 0x95, 0xa9, 0x6e, 0x7b, 0x3d, 0x9c, 0x53,
    0xac, 0x6a, 0x50, 0xb1, 0xf5, 0x5b, 0x34, 0x4e, 0x11, 0xf8, 0xae, 0x2d,
    0x1a, 0xd6, 0xed, 0x49, 0x52, 0x25, 0xa0};

const std::vector<uint8_t> kP256AmbiguousPoint = {
    0x04, 0x3f, 0xb0, 0x67, 0x4b, 0x4b, 0xd3, 0xc3, 0xf7, 0xb6, 0x30,
    0xed, 0xf3, 0xea, 0xf7, 0x9d, 0xde, 0x73, 0x4d, 0x37, 0x8b, 0x89,
    0x04, 0x67, 0xf7, 0x57, 0x8d, 0x94, 0xbb, 0xdf, 0x6e, 0x7c, 0xfb,
    0xf5, 0xf1, 0x50, 0xf7, 0x1b, 0xd4, 0x51, 0xf3, 0x3a, 0xf3, 0x20,
    0x08, 0xd7, 0x62, 0x3c, 0xc3, 0xe7, 0x82, 0x73, 0x98, 0x6f, 0xe9,
    0x69, 0x5d, 0x63, 0x73, 0x40, 0xd1, 0x04, 0xf4, 0xc3, 0x37};

const std::vector<uint8_t> kInvalidZeroLengthKey = {
    0x30, 0x1a,        // SEQUENCE(len=26)
    0x02, 0x01, 0x00,  // INT(len=1) = 0
    0x30, 0x13,        // SEQUENCE(len=19)
    0x06, 0x07,        // OID(len=7)
    // dhPublicKey (1.2.840.10046.2.1)
    0x2a, 0x86, 0x48, 0xce, 0x3e, 0x02, 0x01, 0x06, 0x08,  // OID(len=8)
    // prime256v1 (1.2.840.10045.3.1.7) */
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x04,
    0x00  // OCTET STRING(len=0)
};

class DERPrivateKeyImportTest : public ::testing::Test {
 public:
  bool ParsePrivateKey(const std::vector<uint8_t>& data, bool expect_success) {
    SECKEYPrivateKey* key = nullptr;
    SECStatus rv = SECFailure;
    std::string nick_str =
        ::testing::UnitTest::GetInstance()->current_test_info()->name() +
        std::to_string(rand());
    SECItem item = {siBuffer, const_cast<unsigned char*>(data.data()),
                    static_cast<unsigned int>(data.size())};
    SECItem nick = {
        siBuffer,
        reinterpret_cast<unsigned char*>(const_cast<char*>(nick_str.data())),
        static_cast<unsigned int>(nick_str.length())};

    ScopedPK11SlotInfo slot(PK11_GetInternalKeySlot());
    EXPECT_TRUE(slot);
    if (!slot) {
      return false;
    }

    if (PK11_NeedUserInit(slot.get())) {
      if (PK11_InitPin(slot.get(), nullptr, nullptr) != SECSuccess) {
        EXPECT_EQ(rv, SECSuccess) << "PK11_InitPin failed";
      }
    }
    rv = PK11_Authenticate(slot.get(), PR_TRUE, nullptr);
    EXPECT_EQ(rv, SECSuccess);

    /* SEC_ASN1DecoderFinish() (lib/util/secasn1d.c:3001) only reports
     * SEC_ERROR_BAD_DER when no error is already pending. */
    PORT_SetError(0);
    // pk11wrap rejects bad input before it reaches the token, so failures can
    // be tested against the real thing in FIPS mode too.
    if (expect_success) {
      rv = nss_test::ImportDERPrivateKeyInfoAndReturnKey(
          slot.get(), &item, &nick, nullptr, true, false, KU_ALL, &key,
          nullptr);
    } else {
      rv = PK11_ImportDERPrivateKeyInfoAndReturnKey(slot.get(), &item, &nick,
                                                    nullptr, true, false,
                                                    KU_ALL, &key, nullptr);
    }
    EXPECT_EQ(rv == SECSuccess, key != nullptr);

    if (expect_success) {
      // Try to find the key via its label
      ScopedSECKEYPrivateKeyList list(PK11_ListPrivKeysInSlot(
          slot.get(), const_cast<char*>(nick_str.c_str()), nullptr));
      EXPECT_FALSE(!list);
    }

    if (key) {
      rv = PK11_DeleteTokenPrivateKey(key, true);
      EXPECT_EQ(SECSuccess, rv);

      // PK11_DeleteTokenPrivateKey leaves an errorCode set when there's
      // no cert. This is expected, so clear it.
      if (PORT_GetError() == SSL_ERROR_NO_CERTIFICATE) {
        PORT_SetError(0);
      }
    }

    return rv == SECSuccess;
  }

  SECStatus BuildPrivateKeyInfoAndImportIt(SECOidTag algTag) {
    ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
    EXPECT_TRUE(slot);
    if (!slot) {
      return SECFailure;
    }

    ScopedPLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    EXPECT_TRUE(arena);
    if (!arena) {
      return SECFailure;
    }

    SECKEYPrivateKeyInfo* pki = (SECKEYPrivateKeyInfo*)PORT_ArenaZAlloc(
        arena.get(), sizeof(SECKEYPrivateKeyInfo));
    EXPECT_TRUE(pki);
    if (!pki) {
      return SECFailure;
    }

    pki->version.data = (unsigned char*)PORT_ArenaAlloc(arena.get(), 1);
    EXPECT_TRUE(pki->version.data);
    if (!pki->version.data) {
      return SECFailure;
    }

    pki->version.data[0] = 0x00;
    pki->version.len = 1;

    EXPECT_EQ(
        SECOID_SetAlgorithmID(arena.get(), &pki->algorithm, algTag, nullptr),
        SECSuccess);

    // Empty private key
    pki->privateKey.data = (unsigned char*)PORT_ArenaAlloc(arena.get(), 1);
    EXPECT_TRUE(pki->privateKey.data);
    if (!pki->privateKey.data) {
      return SECFailure;
    }
    pki->privateKey.len = 0;

    SECKEYPrivateKey* privk = nullptr;
    PORT_SetError(0);
    SECStatus rv = PK11_ImportPrivateKeyInfoAndReturnKey(
        slot.get(), pki, nullptr, nullptr, PR_FALSE, PR_FALSE, KU_ALL, &privk,
        nullptr);

    if (privk) {
      SECKEY_DestroyPrivateKey(privk);
    }
    return rv;
  }
};

TEST_F(DERPrivateKeyImportTest, ImportPrivateRSAKey) {
  EXPECT_TRUE(ParsePrivateKey(kValidRSAKey, true));
  EXPECT_FALSE(PORT_GetError()) << PORT_GetError();
}

TEST_F(DERPrivateKeyImportTest, ImportRSAPSSKey) {
  // An id-RSASSA-PSS (1.2.840.113549.1.1.10) PrivateKeyInfo wraps an ordinary
  // RSAPrivateKey, byte-identical to rsaEncryption; only the algorithm OID
  // differs (and NSS represents both as CKK_RSA). Reuse the valid RSA key and
  // swap only the OID's terminal arc (1 -> 10) to exercise the PSS path.
  std::vector<uint8_t> pss_key = kValidRSAKey;
  ASSERT_EQ(pss_key[19], 0x01);  // rsaEncryption OID terminal arc
  pss_key[19] = 0x0a;            // id-RSASSA-PSS
  EXPECT_TRUE(ParsePrivateKey(pss_key, true));
  EXPECT_FALSE(PORT_GetError()) << PORT_GetError();
}

TEST_F(DERPrivateKeyImportTest, ImportEcdsaKey) {
  EXPECT_TRUE(ParsePrivateKey(kValidP256Key, true));
  EXPECT_FALSE(PORT_GetError()) << PORT_GetError();
}

// The input has no public key, so CKA_NSS_DB is left unset,
// and CKA_TOKEN is false, so the point derivation happens during the import.
TEST_F(DERPrivateKeyImportTest, ImportEcdsaKeyWithoutPublicKey) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);

  SECItem item = {siBuffer,
                  const_cast<unsigned char*>(kValidP256KeyNoPublicKey.data()),
                  static_cast<unsigned int>(kValidP256KeyNoPublicKey.size())};
  SECKEYPrivateKey* privk = nullptr;
  PORT_SetError(0);
  ASSERT_EQ(SECSuccess, nss_test::ImportDERPrivateKeyInfoAndReturnKey(
                            slot.get(), &item, nullptr, nullptr, PR_FALSE,
                            PR_FALSE, KU_ALL, &privk, nullptr));
  ScopedSECKEYPrivateKey key(privk);
  ASSERT_TRUE(key);

  SECItem point = {siBuffer, nullptr, 0};
  ASSERT_EQ(SECSuccess, PK11_ReadRawAttribute(PK11_TypePrivKey, key.get(),
                                              CKA_NSS_DB, &point));
  EXPECT_EQ(kValidP256PublicKey,
            std::vector<uint8_t>(point.data, point.data + point.len));
  SECITEM_FreeItem(&point, PR_FALSE);
}

// Same derive path, but the scalar is a byte short, so EC_DerivePublicKey()
// fails.
TEST_F(DERPrivateKeyImportTest, DeriveFailsOnShortScalar) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);

  SECItem item = {
      siBuffer, const_cast<unsigned char*>(kP256ShortScalarNoPublicKey.data()),
      static_cast<unsigned int>(kP256ShortScalarNoPublicKey.size())};
  SECKEYPrivateKey* privk = nullptr;
  PORT_SetError(0);
  EXPECT_EQ(SECFailure, nss_test::ImportDERPrivateKeyInfoAndReturnKey(
                            slot.get(), &item, nullptr, nullptr, PR_FALSE,
                            PR_FALSE, KU_ALL, &privk, nullptr));
  EXPECT_EQ(nullptr, privk);
  EXPECT_EQ(PORT_GetError(), SEC_ERROR_BAD_DATA);
}

TEST_F(DERPrivateKeyImportTest, ImportInvalidPrivateKey) {
  EXPECT_FALSE(ParsePrivateKey(kInvalidLengthKey, false));
  EXPECT_EQ(PORT_GetError(), SEC_ERROR_BAD_DER) << PORT_GetError();
}

TEST_F(DERPrivateKeyImportTest, ImportZeroLengthPrivateKey) {
  EXPECT_FALSE(ParsePrivateKey(kInvalidZeroLengthKey, false));
  EXPECT_EQ(PORT_GetError(), SEC_ERROR_BAD_KEY) << PORT_GetError();
}

TEST_F(DERPrivateKeyImportTest, ImportZeroLengthMLDSAPrivateKey) {
  EXPECT_EQ(BuildPrivateKeyInfoAndImportIt(SEC_OID_ML_DSA_44), SECFailure);
  EXPECT_EQ(PORT_GetError(), SEC_ERROR_BAD_KEY);
}

TEST_F(DERPrivateKeyImportTest, ImportEd25519WithNonEmptyAlgorithmParams) {
  EXPECT_FALSE(ParsePrivateKey(kEd25519WithNonEmptyAlgorithmParams, false));
  EXPECT_EQ(PORT_GetError(), SEC_ERROR_UNSUPPORTED_KEYALG);
}

struct PqAlgorithm {
  const char* name;
  SECOidTag oid;
  KeyType keyType;
  size_t seedLen;
  size_t publicKeyLen;
};

// NULL, an empty OCTET STRING and an empty SEQUENCE: any parameters at all
// have to be rejected, not only NULL.
static const std::vector<std::vector<uint8_t>> kPresentParameters = {
    {0x05, 0x00}, {0x04, 0x00}, {0x30, 0x00}};

class PqAlgorithmParametersTest
    : public DERPrivateKeyImportTest,
      public ::testing::WithParamInterface<PqAlgorithm> {
 protected:
  std::vector<uint8_t> Pkcs8(const std::vector<uint8_t>& parameters) {
    std::vector<uint8_t> seed(GetParam().seedLen, 0x2a);
    return BuildPqPkcs8(GetParam().oid, PqSeedChoice(seed), parameters);
  }

  ScopedSECKEYPublicKey ExtractPublicKey(
      size_t publicKeyLen, const std::vector<uint8_t>& parameters) {
    std::vector<uint8_t> publicKey(publicKeyLen, 0x2a);
    std::vector<uint8_t> der =
        BuildPqSpki(GetParam().oid, publicKey, parameters);
    SECItem item = {siBuffer, der.data(),
                    static_cast<unsigned int>(der.size())};
    ScopedCERTSubjectPublicKeyInfo spki(
        SECKEY_DecodeDERSubjectPublicKeyInfo(&item));
    EXPECT_TRUE(spki) << PORT_ErrorToString(PORT_GetError());
    if (!spki) {
      return nullptr;
    }
    PORT_SetError(0);
    return ScopedSECKEYPublicKey(SECKEY_ExtractPublicKey(spki.get()));
  }

  void SetUp() override {
    slot_.reset(PK11_GetInternalSlot());
    ASSERT_TRUE(slot_);
    kek_.reset(PK11_KeyGen(slot_.get(), CKM_AES_CBC, nullptr, 16, nullptr));
    ASSERT_TRUE(kek_);
    param_.reset(PK11_ParamFromIV(CKM_NSS_AES_KEY_WRAP_PAD, nullptr));
    ASSERT_TRUE(param_);
  }

  // Encrypts |pkcs8| as is and unwraps it, which takes softoken's own PKCS#8
  // decoder rather than pk11wrap's.
  ScopedSECKEYPrivateKey Unwrap(const std::vector<uint8_t>& pkcs8) {
    std::vector<uint8_t> wrapped(pkcs8.size() + 16);
    unsigned int wrappedLen = 0;
    SECStatus rv = PK11_Encrypt(
        kek_.get(), CKM_NSS_AES_KEY_WRAP_PAD, param_.get(), wrapped.data(),
        &wrappedLen, static_cast<unsigned int>(wrapped.size()), pkcs8.data(),
        static_cast<unsigned int>(pkcs8.size()));
    EXPECT_EQ(SECSuccess, rv) << PORT_ErrorToString(PORT_GetError());
    if (rv != SECSuccess) {
      return nullptr;
    }
    SECItem wrappedItem = {siBuffer, wrapped.data(), wrappedLen};

    // Only used for the CKA_ID of the unwrapped key.
    std::vector<uint8_t> id(32, 0x2a);
    SECItem idItem = {siBuffer, id.data(),
                      static_cast<unsigned int>(id.size())};
    return ScopedSECKEYPrivateKey(PK11_UnwrapPrivKeyByKeyType(
        slot_.get(), kek_.get(), CKM_NSS_AES_KEY_WRAP_PAD, param_.get(),
        &wrappedItem, nullptr, &idItem, PR_FALSE, PK11_IsFIPS(),
        GetParam().keyType, KU_ALL, nullptr));
  }

  ScopedPK11SlotInfo slot_;
  ScopedPK11SymKey kek_;
  ScopedSECItem param_;
};

TEST_P(PqAlgorithmParametersTest, Pkcs8RejectsParameters) {
  ASSERT_TRUE(ParsePrivateKey(Pkcs8({}), true));

  for (const auto& parameters : kPresentParameters) {
    SCOPED_TRACE(::testing::PrintToString(parameters));
    EXPECT_FALSE(ParsePrivateKey(Pkcs8(parameters), false));
    EXPECT_EQ(PORT_GetError(), SEC_ERROR_INVALID_ALGORITHM);
  }
}

TEST_P(PqAlgorithmParametersTest, UnwrapRejectsParameters) {
  ScopedSECKEYPrivateKey key = Unwrap(Pkcs8({}));
  ASSERT_TRUE(key) << PORT_ErrorToString(PORT_GetError());
  EXPECT_EQ(GetParam().keyType, key->keyType);

  for (const auto& parameters : kPresentParameters) {
    SCOPED_TRACE(::testing::PrintToString(parameters));
    EXPECT_FALSE(Unwrap(Pkcs8(parameters)));
  }
}

TEST_P(PqAlgorithmParametersTest, SpkiRejectsParameters) {
  ScopedSECKEYPublicKey key = ExtractPublicKey(GetParam().publicKeyLen, {});
  ASSERT_TRUE(key) << PORT_ErrorToString(PORT_GetError());
  EXPECT_EQ(GetParam().keyType, key->keyType);

  for (const auto& parameters : kPresentParameters) {
    SCOPED_TRACE(::testing::PrintToString(parameters));
    EXPECT_FALSE(ExtractPublicKey(GetParam().publicKeyLen, parameters));
    EXPECT_EQ(PORT_GetError(), SEC_ERROR_INVALID_ALGORITHM);
  }
}

TEST_P(PqAlgorithmParametersTest, SpkiRejectsEmptyPublicKey) {
  EXPECT_FALSE(ExtractPublicKey(0, {}));
  EXPECT_EQ(PORT_GetError(), SEC_ERROR_INPUT_LEN);
}

INSTANTIATE_TEST_SUITE_P(
    PqAlgorithms, PqAlgorithmParametersTest,
    ::testing::Values(
        PqAlgorithm{"ML_DSA_44", SEC_OID_ML_DSA_44, mldsaKey, 32, 1312},
        PqAlgorithm{"ML_DSA_65", SEC_OID_ML_DSA_65, mldsaKey, 32, 1952},
        PqAlgorithm{"ML_DSA_87", SEC_OID_ML_DSA_87, mldsaKey, 32, 2592},
        PqAlgorithm{"ML_KEM_512", SEC_OID_ML_KEM_512, kyberKey, 64, 800},
        PqAlgorithm{"ML_KEM_768", SEC_OID_ML_KEM_768, kyberKey, 64, 1184},
        PqAlgorithm{"ML_KEM_1024", SEC_OID_ML_KEM_1024, kyberKey, 64, 1568}),
    [](const ::testing::TestParamInfo<PqAlgorithm>& i) {
      return std::string(i.param.name);
    });

// An imported private key needs a CKA_ID, or it can never be found again.
// PK11_ImportAndReturnPrivateKey builds one from the public value when it has
// one; when the PrivateKeyInfo carries none (and the caller passes none), it
// has the token regenerate the public key and uses that.
//
// ML-KEM and ML-DSA are covered the same way in pk11_kem_unittest.cc and
// pk11_mldsa_unittest.cc, which have to assemble their PKCS#8 by hand.
class PrivateKeyImportCkaIdTest : public ::testing::Test {
 protected:
  void SetUp() override {
    slot_.reset(PK11_GetInternalKeySlot());
    ASSERT_TRUE(slot_);
    if (PK11_NeedUserInit(slot_.get())) {
      ASSERT_EQ(SECSuccess, PK11_InitPin(slot_.get(), nullptr, nullptr));
    }
    ASSERT_EQ(SECSuccess, PK11_Authenticate(slot_.get(), PR_TRUE, nullptr));
  }

  ScopedSECKEYPrivateKey Import(const std::vector<uint8_t>& pkcs8,
                                PRBool isPerm) {
    SECItem der = {siBuffer, const_cast<unsigned char*>(pkcs8.data()),
                   static_cast<unsigned int>(pkcs8.size())};
    SECKEYPrivateKey* privk = nullptr;
    PORT_SetError(0);
    SECStatus rv = nss_test::ImportDERPrivateKeyInfoAndReturnKey(
        slot_.get(), &der, nullptr, nullptr, isPerm, PR_FALSE, KU_ALL, &privk,
        nullptr);
    EXPECT_EQ(rv == SECSuccess, privk != nullptr);
    return ScopedSECKEYPrivateKey(privk);
  }

  // The key's CKA_ID should be the hash of publicValue, and looking that ID
  // up in the slot should come back to this same key.
  void ExpectCkaIdFor(SECKEYPrivateKey* privKey, const SECItem* publicValue) {
    ScopedSECItem expected(PK11_MakeIDFromPubKey(publicValue));
    ASSERT_TRUE(expected);

    ScopedSECItem ckaId(SECITEM_AllocItem(nullptr, nullptr, 0));
    ASSERT_TRUE(ckaId);
    ASSERT_EQ(SECSuccess, PK11_ReadRawAttribute(PK11_TypePrivKey, privKey,
                                                CKA_ID, ckaId.get()))
        << PORT_ErrorToString(PORT_GetError());
    EXPECT_EQ(0, SECITEM_CompareItem(expected.get(), ckaId.get()));

    ScopedSECKEYPrivateKey found(
        PK11_FindKeyByKeyID(slot_.get(), expected.get(), nullptr));
    ASSERT_TRUE(found) << PORT_ErrorToString(PORT_GetError());
    EXPECT_EQ(privKey->pkcs11ID, found->pkcs11ID);
  }

  // When the token can't reconstruct the public key there is nothing to make
  // an ID out of, so the key is usable but can't be looked up.
  void ExpectNoCkaId(SECKEYPrivateKey* privKey, const SECItem* publicValue) {
    ScopedSECItem ckaId(SECITEM_AllocItem(nullptr, nullptr, 0));
    ASSERT_TRUE(ckaId);
    ASSERT_EQ(SECSuccess, PK11_ReadRawAttribute(PK11_TypePrivKey, privKey,
                                                CKA_ID, ckaId.get()))
        << PORT_ErrorToString(PORT_GetError());
    EXPECT_EQ(0U, ckaId->len);

    ScopedSECItem wanted(PK11_MakeIDFromPubKey(publicValue));
    ASSERT_TRUE(wanted);
    ScopedSECKEYPrivateKey found(
        PK11_FindKeyByKeyID(slot_.get(), wanted.get(), nullptr));
    EXPECT_FALSE(found);
  }

  // Importing a token key should also leave a matching token public key
  // behind, the same way PK11_ImportEncryptedPrivateKeyInfoAndReturnKey does.
  bool HasMatchingTokenPublicKey(const SECItem* publicValue) {
    ScopedSECItem wanted(PK11_MakeIDFromPubKey(publicValue));
    EXPECT_TRUE(wanted);
    if (!wanted) {
      return false;
    }

    bool found = false;
    PK11GenericObject* objs =
        PK11_FindGenericObjects(slot_.get(), CKO_PUBLIC_KEY);
    for (PK11GenericObject* obj = objs; obj && !found;
         obj = PK11_GetNextGenericObject(obj)) {
      SECItem id = {siBuffer, nullptr, 0};
      if (PK11_ReadRawAttribute(PK11_TypeGeneric, obj, CKA_ID, &id) !=
          SECSuccess) {
        continue;
      }
      bool matches = SECITEM_ItemsAreEqual(wanted.get(), &id);
      SECITEM_FreeItem(&id, PR_FALSE);
      if (!matches) {
        continue;
      }
      SECItem token = {siBuffer, nullptr, 0};
      if (PK11_ReadRawAttribute(PK11_TypeGeneric, obj, CKA_TOKEN, &token) ==
          SECSuccess) {
        found = token.len == 1 && token.data[0];
        SECITEM_FreeItem(&token, PR_FALSE);
      }
    }
    if (objs) {
      PK11_DestroyGenericObjects(objs);
    }
    return found;
  }

  // Token keys outlive the test, so take them back out again.
  void DeleteTokenKey(ScopedSECKEYPrivateKey& key) {
    EXPECT_EQ(SECSuccess, PK11_DeleteTokenPrivateKey(key.release(), true));
    // PK11_DeleteTokenPrivateKey leaves an error set when the key has no
    // cert, which is the normal case here.
    if (PORT_GetError() == SSL_ERROR_NO_CERTIFICATE) {
      PORT_SetError(0);
    }
  }

  ScopedPK11SlotInfo slot_;
};

struct CkaIdKeyType {
  const char* name;
  CK_MECHANISM_TYPE mech;
  SECOidTag curve;
};

class PrivateKeyImportCkaIdRoundTripTest
    : public PrivateKeyImportCkaIdTest,
      public ::testing::WithParamInterface<CkaIdKeyType> {};

// Generate a key, round-trip it through an unencrypted PrivateKeyInfo, and
// check that what comes back can be found by its CKA_ID. RSA and EC carry a
// public value in the encoding; Ed25519 and X25519 do not, so those two go
// through the regenerate-from-the-token path.
TEST_P(PrivateKeyImportCkaIdRoundTripTest, SessionKeyGetsCkaId) {
  const CkaIdKeyType& keyType = GetParam();
  Pkcs11KeyPairGenerator generator(keyType.mech, keyType.curve);
  ScopedSECKEYPrivateKey priv;
  ScopedSECKEYPublicKey pub;
  // an unencrypted export reads the private attributes out of the token, so
  // the key it reads them from can't be sensitive
  generator.GenerateKey(&priv, &pub, /*sensitive=*/false);
  ASSERT_TRUE(priv);
  ASSERT_TRUE(pub);

  const SECItem* publicValue = PK11_GetPublicValueFromPublicKey(pub.get());
  ASSERT_TRUE(publicValue);
  std::vector<uint8_t> pubBytes(publicValue->data,
                                publicValue->data + publicValue->len);
  SECItem expectedPub = {siBuffer, pubBytes.data(),
                         static_cast<unsigned int>(pubBytes.size())};

  ScopedSECItem der(nss_test::ExportDERPrivateKeyInfo(priv.get(), nullptr));
  ASSERT_TRUE(der) << PORT_ErrorToString(PORT_GetError());
  std::vector<uint8_t> pkcs8(der->data, der->data + der->len);

  // drop the generated pair, so the lookup below can only find the import
  priv.reset();
  pub.reset();

  ScopedSECKEYPrivateKey imported(Import(pkcs8, PR_FALSE));
  ASSERT_TRUE(imported) << PORT_ErrorToString(PORT_GetError());
  ExpectCkaIdFor(imported.get(), &expectedPub);
}

// Same, but as a token key, which should also get a matching token public key.
TEST_P(PrivateKeyImportCkaIdRoundTripTest, TokenKeyGetsCkaIdAndPublicKey) {
  const CkaIdKeyType& keyType = GetParam();
  Pkcs11KeyPairGenerator generator(keyType.mech, keyType.curve);
  ScopedSECKEYPrivateKey priv;
  ScopedSECKEYPublicKey pub;
  generator.GenerateKey(&priv, &pub, /*sensitive=*/false);
  ASSERT_TRUE(priv);
  ASSERT_TRUE(pub);

  const SECItem* publicValue = PK11_GetPublicValueFromPublicKey(pub.get());
  ASSERT_TRUE(publicValue);
  std::vector<uint8_t> pubBytes(publicValue->data,
                                publicValue->data + publicValue->len);
  SECItem expectedPub = {siBuffer, pubBytes.data(),
                         static_cast<unsigned int>(pubBytes.size())};

  ScopedSECItem der(nss_test::ExportDERPrivateKeyInfo(priv.get(), nullptr));
  ASSERT_TRUE(der) << PORT_ErrorToString(PORT_GetError());
  std::vector<uint8_t> pkcs8(der->data, der->data + der->len);

  priv.reset();
  pub.reset();

  ScopedSECKEYPrivateKey imported(Import(pkcs8, PR_TRUE));
  ASSERT_TRUE(imported) << PORT_ErrorToString(PORT_GetError());
  ExpectCkaIdFor(imported.get(), &expectedPub);
  EXPECT_TRUE(HasMatchingTokenPublicKey(&expectedPub));
  DeleteTokenKey(imported);
}

INSTANTIATE_TEST_SUITE_P(
    PrivateKeyImportCkaIdRoundTripTest, PrivateKeyImportCkaIdRoundTripTest,
    ::testing::Values(
        CkaIdKeyType{"RSA", CKM_RSA_PKCS_KEY_PAIR_GEN, SEC_OID_UNKNOWN},
        CkaIdKeyType{"ECDSA_P256", CKM_EC_KEY_PAIR_GEN,
                     SEC_OID_SECG_EC_SECP256R1},
        CkaIdKeyType{"ECDSA_P384", CKM_EC_KEY_PAIR_GEN,
                     SEC_OID_SECG_EC_SECP384R1},
        CkaIdKeyType{"Ed25519", CKM_EC_EDWARDS_KEY_PAIR_GEN,
                     SEC_OID_ED25519_PUBLIC_KEY},
        CkaIdKeyType{"X25519", CKM_EC_MONTGOMERY_KEY_PAIR_GEN, SEC_OID_X25519}),
    [](const ::testing::TestParamInfo<CkaIdKeyType>& i) {
      return std::string(i.param.name);
    });

// An ECPrivateKey may omit the optional [1] publicKey. The token has to
// recompute the point before we can make a CKA_ID out of it.
TEST_F(PrivateKeyImportCkaIdTest, EcdsaWithoutPublicKey) {
  ScopedSECKEYPrivateKey imported(Import(kValidP256KeyNoPublicKey, PR_FALSE));
  ASSERT_TRUE(imported) << PORT_ErrorToString(PORT_GetError());

  SECItem expected = {siBuffer,
                      const_cast<unsigned char*>(kValidP256PublicKey.data()),
                      static_cast<unsigned int>(kValidP256PublicKey.size())};
  ExpectCkaIdFor(imported.get(), &expected);
}

// A DSAPrivateKey is only the private scalar, so this always goes through the
// regenerate path. This used to be rejected outright.
TEST_F(PrivateKeyImportCkaIdTest, DsaWithoutPublicKey) {
  if (PK11_IsFIPS()) {
    GTEST_SKIP() << "softoken can't unwrap a DSA key without its public value";
  }
  ScopedSECKEYPrivateKey imported(Import(kDsa1024KeyNoPublicKey, PR_FALSE));
  ASSERT_TRUE(imported) << PORT_ErrorToString(PORT_GetError());

  SECItem expected = {siBuffer,
                      const_cast<unsigned char*>(kDsa1024PublicValue.data()),
                      static_cast<unsigned int>(kDsa1024PublicValue.size())};
  ExpectCkaIdFor(imported.get(), &expected);
}

TEST_F(PrivateKeyImportCkaIdTest, DsaTokenKeyWithoutPublicKey) {
  if (PK11_IsFIPS()) {
    GTEST_SKIP() << "softoken can't unwrap a DSA key without its public value";
  }
  ScopedSECKEYPrivateKey imported(Import(kDsa1024KeyNoPublicKey, PR_TRUE));
  ASSERT_TRUE(imported) << PORT_ErrorToString(PORT_GetError());

  SECItem expected = {siBuffer,
                      const_cast<unsigned char*>(kDsa1024PublicValue.data()),
                      static_cast<unsigned int>(kDsa1024PublicValue.size())};
  ExpectCkaIdFor(imported.get(), &expected);
  EXPECT_TRUE(HasMatchingTokenPublicKey(&expected));
  DeleteTokenKey(imported);
}

// ML-DSA and ML-KEM never carry a public value in a PrivateKeyInfo, so they
// always go through the regenerate path. PK11_ExportPrivKeyInfo can't encode
// either algorithm, so build the three private key CHOICEs by hand.
struct PqCkaIdKeyType {
  const char* name;
  CK_MECHANISM_TYPE mech;
  CK_ULONG paramSet;
  SECOidTag oid;
  // Whether the token can get back to the public key from an expandedKey with
  // no seed. For ML-KEM the encapsulation key is a substring of the
  // decapsulation key; for ML-DSA it would mean recomputing t = A*s1 + s2,
  // which libcrux doesn't expose, so softoken needs the seed.
  bool derivesFromExpandedKey;
};

class PrivateKeyImportPqCkaIdTest
    : public PrivateKeyImportCkaIdTest,
      public ::testing::WithParamInterface<PqCkaIdKeyType> {
 protected:
  void SetUp() override {
    if (PK11_IsFIPS()) {
      GTEST_SKIP() << "FIPS tokens don't reveal CKA_SEED or CKA_VALUE";
    }
    PrivateKeyImportCkaIdTest::SetUp();
  }

  // Generate a key pair and pull out the pieces a PrivateKeyInfo can carry,
  // along with the public value the imported key has to hash to.
  void GenerateAndExtract() {
    const PqCkaIdKeyType& keyType = GetParam();
    CK_ULONG paramSet = keyType.paramSet;
    SECKEYPublicKey* pub = nullptr;
    ScopedSECKEYPrivateKey priv(PK11_GenerateKeyPair(slot_.get(), keyType.mech,
                                                     &paramSet, &pub, PR_FALSE,
                                                     PR_FALSE, nullptr));
    ScopedSECKEYPublicKey pubKey(pub);
    ASSERT_TRUE(priv) << PORT_ErrorToString(PORT_GetError());
    ASSERT_TRUE(pubKey);

    ScopedSECItem seed(SECITEM_AllocItem(nullptr, nullptr, 0));
    ScopedSECItem value(SECITEM_AllocItem(nullptr, nullptr, 0));
    ASSERT_TRUE(seed);
    ASSERT_TRUE(value);
    ASSERT_EQ(SECSuccess, PK11_ReadRawAttribute(PK11_TypePrivKey, priv.get(),
                                                CKA_SEED, seed.get()))
        << PORT_ErrorToString(PORT_GetError());
    ASSERT_EQ(SECSuccess, PK11_ReadRawAttribute(PK11_TypePrivKey, priv.get(),
                                                CKA_VALUE, value.get()))
        << PORT_ErrorToString(PORT_GetError());
    ASSERT_NE(0U, seed->len);
    ASSERT_NE(0U, value->len);
    seed_.assign(seed->data, seed->data + seed->len);
    key_.assign(value->data, value->data + value->len);

    const SECItem* publicValue = PK11_GetPublicValueFromPublicKey(pubKey.get());
    ASSERT_TRUE(publicValue);
    pubBytes_.assign(publicValue->data, publicValue->data + publicValue->len);
    expectedPub_ = {siBuffer, pubBytes_.data(),
                    static_cast<unsigned int>(pubBytes_.size())};

    // priv and pubKey go away here, so the lookups below can only be
    // satisfied by the key each case imports.
  }

  std::vector<uint8_t> seed_;
  std::vector<uint8_t> key_;
  std::vector<uint8_t> pubBytes_;
  SECItem expectedPub_ = {siBuffer, nullptr, 0};
};

TEST_P(PrivateKeyImportPqCkaIdTest, SessionKeyGetsCkaId) {
  ASSERT_NO_FATAL_FAILURE(GenerateAndExtract());
  const PqCkaIdKeyType& keyType = GetParam();

  const struct {
    const char* name;
    std::vector<uint8_t> pkcs8;
    bool expectId;
  } cases[] = {
      {"seed", BuildPqPkcs8(keyType.oid, PqSeedChoice(seed_)), true},
      {"both", BuildPqPkcs8(keyType.oid, PqBothChoice(seed_, key_)), true},
      {"expandedKey", BuildPqPkcs8(keyType.oid, PqExpandedKeyChoice(key_)),
       keyType.derivesFromExpandedKey},
  };

  for (const auto& testCase : cases) {
    SCOPED_TRACE(testCase.name);
    ScopedSECKEYPrivateKey imported(Import(testCase.pkcs8, PR_FALSE));
    ASSERT_TRUE(imported) << PORT_ErrorToString(PORT_GetError());
    if (testCase.expectId) {
      ExpectCkaIdFor(imported.get(), &expectedPub_);
    } else {
      ExpectNoCkaId(imported.get(), &expectedPub_);
    }
  }
}

TEST_P(PrivateKeyImportPqCkaIdTest, TokenKeyGetsCkaIdAndPublicKey) {
  ASSERT_NO_FATAL_FAILURE(GenerateAndExtract());
  const PqCkaIdKeyType& keyType = GetParam();

  ScopedSECKEYPrivateKey imported(
      Import(BuildPqPkcs8(keyType.oid, PqSeedChoice(seed_)), PR_TRUE));
  ASSERT_TRUE(imported) << PORT_ErrorToString(PORT_GetError());
  ExpectCkaIdFor(imported.get(), &expectedPub_);
  EXPECT_TRUE(HasMatchingTokenPublicKey(&expectedPub_));
  DeleteTokenKey(imported);
}

INSTANTIATE_TEST_SUITE_P(
    PrivateKeyImportPqCkaIdTest, PrivateKeyImportPqCkaIdTest,
    ::testing::Values(PqCkaIdKeyType{"ML_KEM_768", CKM_ML_KEM_KEY_PAIR_GEN,
                                     CKP_ML_KEM_768, SEC_OID_ML_KEM_768, true},
                      PqCkaIdKeyType{"ML_KEM_1024", CKM_ML_KEM_KEY_PAIR_GEN,
                                     CKP_ML_KEM_1024, SEC_OID_ML_KEM_1024,
                                     true},
                      PqCkaIdKeyType{"ML_DSA_44", CKM_ML_DSA_KEY_PAIR_GEN,
                                     CKP_ML_DSA_44, SEC_OID_ML_DSA_44, false},
                      PqCkaIdKeyType{"ML_DSA_65", CKM_ML_DSA_KEY_PAIR_GEN,
                                     CKP_ML_DSA_65, SEC_OID_ML_DSA_65, false},
                      PqCkaIdKeyType{"ML_DSA_87", CKM_ML_DSA_KEY_PAIR_GEN,
                                     CKP_ML_DSA_87, SEC_OID_ML_DSA_87, false}),
    [](const ::testing::TestParamInfo<PqCkaIdKeyType>& i) {
      return std::string(i.param.name);
    });

// The CKA_EC_POINT a token hands back may or may not be wrapped in an OCTET
// STRING, and SECKEY_ConvertToPublicKey has to tell the two apart. A bare
// uncompressed point is itself a well formed OCTET STRING once in every 256
// keys, so these check the whole EC public value survives a round trip rather
// than losing its first two bytes. Both paths reach the same heuristic:
// PK11_ExportPrivKeyInfo falls back to SECKEY_ConvertToPublicKey because
// softoken keeps no CKA_EC_POINT on an EC private key, and the CKA_ID that
// PK11_ImportAndReturnPrivateKey derives comes from there too.
struct EcCurve {
  const char* name;
  SECOidTag curve;
  unsigned int pointLen;  // 2 * fieldLen + 1
};

class EcPublicValueTest : public PrivateKeyImportCkaIdTest,
                          public ::testing::WithParamInterface<EcCurve> {
 protected:
  void GenerateKey(ScopedSECKEYPrivateKey* priv, ScopedSECKEYPublicKey* pub) {
    Pkcs11KeyPairGenerator generator(CKM_EC_KEY_PAIR_GEN, GetParam().curve);
    generator.GenerateKey(priv, pub, /*sensitive=*/false);
  }
};

// SECKEY_ConvertToPublicKey is where the wrapped/bare decision is made, so
// check it against a key whose public value we already know.
TEST_P(EcPublicValueTest, ConvertToPublicKeyKeepsWholePoint) {
  ScopedSECKEYPrivateKey priv;
  ScopedSECKEYPublicKey pub;
  ASSERT_NO_FATAL_FAILURE(GenerateKey(&priv, &pub));
  ASSERT_TRUE(priv);
  ASSERT_EQ(GetParam().pointLen, pub->u.ec.publicValue.len);

  ScopedSECKEYPublicKey converted(SECKEY_ConvertToPublicKey(priv.get()));
  ASSERT_TRUE(converted) << PORT_ErrorToString(PORT_GetError());
  EXPECT_EQ(GetParam().pointLen, converted->u.ec.publicValue.len);
  EXPECT_EQ(0, SECITEM_CompareItem(&pub->u.ec.publicValue,
                                   &converted->u.ec.publicValue));
}

// An exported PrivateKeyInfo has to carry the point the key actually has. The
// [1] publicKey is a BIT STRING, so a truncated point shows up as a length
// that doesn't match the curve.
TEST_P(EcPublicValueTest, ExportedPkcs8CarriesWholePoint) {
  ScopedSECKEYPrivateKey priv;
  ScopedSECKEYPublicKey pub;
  ASSERT_NO_FATAL_FAILURE(GenerateKey(&priv, &pub));
  ASSERT_TRUE(priv);

  ScopedSECItem der(nss_test::ExportDERPrivateKeyInfo(priv.get(), nullptr));
  ASSERT_TRUE(der) << PORT_ErrorToString(PORT_GetError());

  // the point is the tail of the encoding, so just look for it there
  const std::vector<uint8_t> encoded(der->data, der->data + der->len);
  const std::vector<uint8_t> point(
      pub->u.ec.publicValue.data,
      pub->u.ec.publicValue.data + pub->u.ec.publicValue.len);
  EXPECT_NE(encoded.end(), std::search(encoded.begin(), encoded.end(),
                                       point.begin(), point.end()))
      << "exported PrivateKeyInfo does not contain the full public point";
}

INSTANTIATE_TEST_SUITE_P(
    EcPublicValueTest, EcPublicValueTest,
    ::testing::Values(EcCurve{"P256", SEC_OID_SECG_EC_SECP256R1, 65},
                      EcCurve{"P384", SEC_OID_SECG_EC_SECP384R1, 97},
                      EcCurve{"P521", SEC_OID_SECG_EC_SECP521R1, 133}),
    [](const ::testing::TestParamInfo<EcCurve>& i) {
      return std::string(i.param.name);
    });

// The deterministic version of the above: this key's point starts 04 3f, so
// the raw 65 bytes parse as an OCTET STRING of length 0x3f. The PrivateKeyInfo
// omits the public key, so the CKA_ID can only come from the point the token
// recomputes, which is exactly where the two forms get confused.
TEST_F(PrivateKeyImportCkaIdTest, EcdsaAmbiguousPointIsNotUnwrapped) {
  ScopedSECKEYPrivateKey imported(
      Import(kP256AmbiguousPointKeyNoPublicKey, PR_FALSE));
  ASSERT_TRUE(imported) << PORT_ErrorToString(PORT_GetError());

  SECItem expected = {siBuffer,
                      const_cast<unsigned char*>(kP256AmbiguousPoint.data()),
                      static_cast<unsigned int>(kP256AmbiguousPoint.size())};
  ExpectCkaIdFor(imported.get(), &expected);

  ScopedSECKEYPublicKey converted(SECKEY_ConvertToPublicKey(imported.get()));
  ASSERT_TRUE(converted) << PORT_ErrorToString(PORT_GetError());
  EXPECT_EQ(0, SECITEM_CompareItem(&expected, &converted->u.ec.publicValue));

  // and exporting it again has to write the whole point back out
  ScopedSECItem der(nss_test::ExportDERPrivateKeyInfo(imported.get(), nullptr));
  ASSERT_TRUE(der) << PORT_ErrorToString(PORT_GetError());
  const std::vector<uint8_t> encoded(der->data, der->data + der->len);
  EXPECT_NE(encoded.end(),
            std::search(encoded.begin(), encoded.end(),
                        kP256AmbiguousPoint.begin(), kP256AmbiguousPoint.end()))
      << "exported PrivateKeyInfo does not contain the full public point";
}

}  // namespace nss_test
