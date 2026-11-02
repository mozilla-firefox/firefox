/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include <tuple>

#include "nss.h"
#include "pk11pub.h"
#include "secerr.h"
#include "sechash.h"
#include "gtest/gtest.h"
#include "nss_scoped_ptrs.h"
#include "testvectors/hmac-sha256-vectors.h"
#include "testvectors/hmac-sha384-vectors.h"
#include "testvectors/hmac-sha512-vectors.h"
#include "testvectors/hmac-sha3-224-vectors.h"
#include "testvectors/hmac-sha3-256-vectors.h"
#include "testvectors/hmac-sha3-384-vectors.h"
#include "testvectors/hmac-sha3-512-vectors.h"
#include "util.h"

namespace nss_test {

class Pkcs11HmacTest : public ::testing::TestWithParam<
                           std::tuple<HmacTestVector, CK_MECHANISM_TYPE>> {
 protected:
  void RunTestVector(const HmacTestVector &vec, CK_MECHANISM_TYPE mech) {
    std::string err = "Test #" + std::to_string(vec.id) + " failed";
    std::vector<uint8_t> vec_key = hex_string_to_bytes(vec.key);
    std::vector<uint8_t> vec_mac = hex_string_to_bytes(vec.tag);
    std::vector<uint8_t> vec_msg = hex_string_to_bytes(vec.msg);
    std::vector<uint8_t> output(vec_mac.size());

    // Don't provide a null pointer, even if the input is empty.
    uint8_t tmp;
    SECItem key = {siBuffer, vec_key.data() ? vec_key.data() : &tmp,
                   static_cast<unsigned int>(vec_key.size())};
    SECItem mac = {siBuffer, vec_mac.data() ? vec_mac.data() : &tmp,
                   static_cast<unsigned int>(vec_mac.size())};
    SECItem msg = {siBuffer, vec_msg.data() ? vec_msg.data() : &tmp,
                   static_cast<unsigned int>(vec_msg.size())};
    SECItem out = {siBuffer, output.data() ? output.data() : &tmp,
                   static_cast<unsigned int>(output.size())};

    ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
    ASSERT_NE(nullptr, slot) << err;

    ScopedPK11SymKey p11_key(PK11_ImportSymKey(
        slot.get(), mech, PK11_OriginUnwrap, CKA_SIGN, &key, nullptr));
    ASSERT_NE(nullptr, p11_key.get()) << err;

    SECStatus rv = PK11_SignWithSymKey(p11_key.get(), mech, NULL, &out, &msg);
    EXPECT_EQ(SECSuccess, rv) << err;
    EXPECT_EQ(!vec.invalid, 0 == SECITEM_CompareItem(&out, &mac)) << err;

    // Now verify the given mac using PK11_CreateContextBySymKey/PK11_Digest*
    ScopedPK11SymKey p11_verify_key(PK11_ImportSymKey(
        slot.get(), mech, PK11_OriginUnwrap, CKA_VERIFY, &key, nullptr));
    ASSERT_NE(nullptr, p11_verify_key.get()) << err;

    SECItem param = {siBuffer, nullptr, 0};
    ScopedPK11Context verify_context(PK11_CreateContextBySymKey(
        mech, CKA_VERIFY, p11_verify_key.get(), &param));
    ASSERT_NE(nullptr, verify_context.get()) << err;

    rv = PK11_DigestBegin(verify_context.get());
    EXPECT_EQ(SECSuccess, rv) << err;

    rv = PK11_DigestOp(verify_context.get(), msg.data, msg.len);
    EXPECT_EQ(SECSuccess, rv) << err;

    unsigned int unusedOutLen = 0;
    rv = PK11_DigestFinal(verify_context.get(), mac.data, &unusedOutLen,
                          mac.len);
    EXPECT_EQ(!vec.invalid, SECSuccess == rv) << err;
  }
};

TEST_P(Pkcs11HmacTest, WycheproofVectors) {
  RunTestVector(std::get<0>(GetParam()), std::get<1>(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(
    HmacSha256, Pkcs11HmacTest,
    ::testing::Combine(::testing::ValuesIn(kHmacSha256WycheproofVectors),
                       ::testing::Values(CKM_SHA256_HMAC)));
INSTANTIATE_TEST_SUITE_P(
    HmacSha384, Pkcs11HmacTest,
    ::testing::Combine(::testing::ValuesIn(kHmacSha384WycheproofVectors),
                       ::testing::Values(CKM_SHA384_HMAC)));
INSTANTIATE_TEST_SUITE_P(
    HmacSha512, Pkcs11HmacTest,
    ::testing::Combine(::testing::ValuesIn(kHmacSha512WycheproofVectors),
                       ::testing::Values(CKM_SHA512_HMAC)));
INSTANTIATE_TEST_SUITE_P(
    HmacSha3224, Pkcs11HmacTest,
    ::testing::Combine(::testing::ValuesIn(kHmacSha3224WycheproofVectors),
                       ::testing::Values(CKM_SHA3_224_HMAC)));
INSTANTIATE_TEST_SUITE_P(
    HmacSha3256, Pkcs11HmacTest,
    ::testing::Combine(::testing::ValuesIn(kHmacSha3256WycheproofVectors),
                       ::testing::Values(CKM_SHA3_256_HMAC)));
INSTANTIATE_TEST_SUITE_P(
    HmacSha3384, Pkcs11HmacTest,
    ::testing::Combine(::testing::ValuesIn(kHmacSha3384WycheproofVectors),
                       ::testing::Values(CKM_SHA3_384_HMAC)));
INSTANTIATE_TEST_SUITE_P(
    HmacSha3512, Pkcs11HmacTest,
    ::testing::Combine(::testing::ValuesIn(kHmacSha3512WycheproofVectors),
                       ::testing::Values(CKM_SHA3_512_HMAC)));
}  // namespace nss_test
