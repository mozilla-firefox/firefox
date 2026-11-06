/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>

#include "nss.h"
#include "pk11pub.h"
#include "secerr.h"
#include "sechash.h"
#include "gtest/gtest.h"
#include "json_reader.h"
#include "nss_scoped_ptrs.h"
#include "testvectors_base/test-structs.h"
#include "pk11_import.h"
#include "pk11_keygen.h"

namespace nss_test {

class Pkcs11HmacTest : public ::testing::Test {
 protected:
  void Run(const std::string& name, const std::string& algorithm,
           CK_MECHANISM_TYPE mech) {
    WycheproofHeader(name, algorithm, "mac_test_schema_v1.json",
                     [this, mech](JsonReader& r) { RunGroup(r, mech); });
  }

 private:
  void RunGroup(JsonReader& r, CK_MECHANISM_TYPE mech) {
    std::vector<HmacTestVector> tests;

    while (r.NextItem()) {
      std::string n = r.ReadLabel();
      if (n == "") {
        break;
      }

      if (n == "keySize" || n == "tagSize" || n == "source") {
        r.SkipValue();
      } else if (n == "type") {
        ASSERT_EQ("MacTest", r.ReadString());
      } else if (n == "tests") {
        WycheproofReadTests(r, &tests, ReadTestAttr);
      } else {
        FAIL() << "unknown label in group: " << n;
      }
    }

    for (auto& t : tests) {
      std::cout << "Running test " << t.id << std::endl;
      RunTestVector(t, mech);
    }
  }

  static void ReadTestAttr(HmacTestVector& t, const std::string& n,
                           JsonReader& r) {
    if (n == "key") {
      t.key = r.ReadHex();
    } else if (n == "msg") {
      t.msg = r.ReadHex();
    } else if (n == "tag") {
      t.tag = r.ReadHex();
    } else {
      FAIL() << "unknown test key: " << n;
    }
  }

  void RunTestVector(const HmacTestVector& vec, CK_MECHANISM_TYPE mech) {
    std::string err = "Test #" + std::to_string(vec.id) + " failed";
    std::vector<uint8_t> vec_key(vec.key);
    std::vector<uint8_t> vec_mac(vec.tag);
    std::vector<uint8_t> vec_msg(vec.msg);
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

    ScopedPK11SymKey p11_key(nss_test::ImportSymKey(
        slot.get(), mech, PK11_OriginUnwrap, CKA_SIGN, &key, nullptr));
    ASSERT_NE(nullptr, p11_key.get()) << err;

    SECStatus rv = PK11_SignWithSymKey(p11_key.get(), mech, NULL, &out, &msg);
    EXPECT_EQ(SECSuccess, rv) << err;
    EXPECT_EQ(vec.valid, 0 == SECITEM_CompareItem(&out, &mac)) << err;

    // Now verify the given mac using PK11_CreateContextBySymKey/PK11_Digest*
    ScopedPK11SymKey p11_verify_key(nss_test::ImportSymKey(
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
    EXPECT_EQ(vec.valid, SECSuccess == rv) << err;
  }
};

TEST_F(Pkcs11HmacTest, Sha224) {
  Run("hmac_sha224", "HMACSHA224", CKM_SHA224_HMAC);
}
TEST_F(Pkcs11HmacTest, Sha256) {
  Run("hmac_sha256", "HMACSHA256", CKM_SHA256_HMAC);
}
TEST_F(Pkcs11HmacTest, Sha384) {
  Run("hmac_sha384", "HMACSHA384", CKM_SHA384_HMAC);
}
TEST_F(Pkcs11HmacTest, Sha512) {
  Run("hmac_sha512", "HMACSHA512", CKM_SHA512_HMAC);
}
TEST_F(Pkcs11HmacTest, Sha3224) {
  Run("hmac_sha3_224", "HMACSHA3-224", CKM_SHA3_224_HMAC);
}
TEST_F(Pkcs11HmacTest, Sha3256) {
  Run("hmac_sha3_256", "HMACSHA3-256", CKM_SHA3_256_HMAC);
}
TEST_F(Pkcs11HmacTest, Sha3384) {
  Run("hmac_sha3_384", "HMACSHA3-384", CKM_SHA3_384_HMAC);
}
TEST_F(Pkcs11HmacTest, Sha3512) {
  Run("hmac_sha3_512", "HMACSHA3-512", CKM_SHA3_512_HMAC);
}

class Pkcs11MacKeyTest : public ::testing::TestWithParam<CK_MECHANISM_TYPE> {};

// MAC mechanisms should refuse to sign with an RSA private key.
TEST_P(Pkcs11MacKeyTest, RsaKeyIsNotAMacKey) {
  ScopedSECKEYPrivateKey priv;
  ScopedSECKEYPublicKey pub;
  Pkcs11KeyPairGenerator(CKM_RSA_PKCS_KEY_PAIR_GEN).GenerateKey(&priv, &pub);
  ASSERT_TRUE(priv);

  uint8_t msg_buf[16] = {0};
  uint8_t mac_buf[HASH_LENGTH_MAX];
  SECItem msg = {siBuffer, msg_buf, sizeof(msg_buf)};
  SECItem mac = {siBuffer, mac_buf, sizeof(mac_buf)};
  EXPECT_EQ(SECFailure, PK11_SignWithMechanism(priv.get(), GetParam(), nullptr,
                                               &mac, &msg));
  EXPECT_EQ(SEC_ERROR_INVALID_KEY, PORT_GetError());
}

INSTANTIATE_TEST_SUITE_P(MacMechanisms, Pkcs11MacKeyTest,
                         ::testing::Values(CKM_SHA256_HMAC, CKM_AES_CMAC));

}  // namespace nss_test
