/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include "nss.h"
#include "pk11pub.h"

#include "cpputil.h"

#include "gtest/gtest.h"
#include "nss_scoped_ptrs.h"
#include "pk11_import.h"

namespace nss_test {

const size_t kPmsSize = 48;
const size_t kMasterSecretSize = 48;
const size_t kPrfSeedSizeSha256 = 32;
const size_t kPrfSeedSizeTlsPrf = 36;

// This is not the right size for anything
const size_t kIncorrectSize = 17;

const uint8_t kPmsData[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f};

const uint8_t kPrfSeed[] = {
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb,
    0xfc, 0xfd, 0xfe, 0xff, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xd0, 0xd1, 0xd2, 0xd3};

const uint8_t kExpectedOutputEmsSha256[] = {
    0x75, 0xa7, 0xa5, 0x98, 0xef, 0xab, 0x90, 0xe7, 0x7c, 0x67, 0x80, 0xde,
    0xab, 0x3a, 0x11, 0xf3, 0x5d, 0xb2, 0xf8, 0x47, 0xff, 0x09, 0x01, 0xec,
    0xf8, 0x93, 0x89, 0xfc, 0x98, 0x2e, 0x6e, 0xf9, 0x2c, 0xf5, 0x9b, 0x04,
    0x04, 0x6f, 0xd7, 0x28, 0x6e, 0xea, 0xe3, 0x83, 0xc4, 0x4a, 0xff, 0x03};

const uint8_t kExpectedOutputEmsTlsPrf[] = {
    0x06, 0xbf, 0x29, 0x86, 0x5d, 0xf3, 0x3e, 0x38, 0xfd, 0xfa, 0x91, 0x10,
    0x2a, 0x20, 0xff, 0xd6, 0xb9, 0xd5, 0x72, 0x5a, 0x6d, 0x42, 0x20, 0x16,
    0xde, 0xa4, 0xa0, 0x51, 0xe5, 0x53, 0xc1, 0x28, 0x04, 0x99, 0xbc, 0xb1,
    0x2c, 0x9d, 0xe8, 0x0b, 0x18, 0xa2, 0x0e, 0x48, 0x52, 0x8d, 0x61, 0x13};

class TlsPrfTest : public ::testing::Test {
 public:
  TlsPrfTest()
      : params_({siBuffer, nullptr, 0}),
        pms_item_({siBuffer, toUcharPtr(kPmsData), kPmsSize}),
        key_mech_(0),
        pms_version_({0, 0}) {}

  void Init() {
    params_.type = siBuffer;

    pms_item_.type = siBuffer;
    pms_item_.data =
        const_cast<unsigned char*>(static_cast<const unsigned char*>(kPmsData));

    slot_.reset(PK11_GetInternalSlot());
    ASSERT_NE(nullptr, slot_.get());
  }

  void CheckForError(CK_MECHANISM_TYPE hash_mech, size_t seed_len,
                     size_t pms_len, size_t output_len) {
    // Error tests don't depend on the derivation mechansim
    Inner(CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE, hash_mech, seed_len, pms_len,
          output_len, nullptr, nullptr);
  }

  void ComputeAndVerifyMs(CK_MECHANISM_TYPE derive_mech,
                          CK_MECHANISM_TYPE hash_mech, CK_VERSION* version,
                          const uint8_t* expected) {
    // Infer seed length from mechanism
    int seed_len = 0;
    switch (hash_mech) {
      case CKM_TLS_PRF:
        seed_len = kPrfSeedSizeTlsPrf;
        break;
      case CKM_SHA256:
        seed_len = kPrfSeedSizeSha256;
        break;
      default:
        ASSERT_TRUE(false);
    }

    Inner(derive_mech, hash_mech, seed_len, kPmsSize, 0, version, expected);
  }

  // Set output == nullptr to test when errors occur
  void Inner(CK_MECHANISM_TYPE derive_mech, CK_MECHANISM_TYPE hash_mech,
             size_t seed_len, size_t pms_len, size_t output_len,
             CK_VERSION* version, const uint8_t* expected) {
    // Infer the key mechanism from the hash type
    switch (hash_mech) {
      case CKM_TLS_PRF:
        key_mech_ = CKM_TLS_KEY_AND_MAC_DERIVE;
        break;
      case CKM_SHA256:
        key_mech_ = CKM_NSS_TLS_KEY_AND_MAC_DERIVE_SHA256;
        break;
      default:
        ASSERT_TRUE(false);
    }

    // Import the params
    CK_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE_PARAMS master_params = {
        hash_mech, toUcharPtr(kPrfSeed), static_cast<CK_ULONG>(seed_len),
        version};
    params_.data = reinterpret_cast<unsigned char*>(&master_params);
    params_.len = sizeof(master_params);

    // Import the PMS
    pms_item_.len = pms_len;
    pms_.reset(nss_test::ImportSymKey(slot_.get(), derive_mech,
                                      PK11_OriginUnwrap, CKA_DERIVE, &pms_item_,
                                      NULL));
    ASSERT_NE(nullptr, pms_.get());

    // Compute the EMS
    ms_.reset(PK11_DeriveWithFlags(pms_.get(), derive_mech, &params_, key_mech_,
                                   CKA_DERIVE, output_len,
                                   CKF_SIGN | CKF_VERIFY));

    // Verify the EMS has the expected value (null or otherwise)
    if (!expected) {
      EXPECT_EQ(nullptr, ms_.get());
    } else {
      ASSERT_NE(nullptr, ms_.get());

      SECStatus rv = nss_test::ExtractKeyValue(ms_.get());
      ASSERT_EQ(SECSuccess, rv);

      SECItem* msData = nss_test::GetKeyData(ms_.get());
      ASSERT_NE(nullptr, msData);

      ASSERT_EQ(kMasterSecretSize, msData->len);
      EXPECT_EQ(0, memcmp(msData->data, expected, kMasterSecretSize));
    }
  }

 protected:
  SECItem params_;
  SECItem pms_item_;
  CK_MECHANISM_TYPE key_mech_;
  ScopedPK11SlotInfo slot_;
  ScopedPK11SymKey pms_;
  ScopedPK11SymKey ms_;
  CK_VERSION pms_version_;
};

TEST_F(TlsPrfTest, ExtendedMsParamErr) {
  Init();

  // This should fail; it's the correct set from which the below are derived
  // CheckForError(CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE, CKM_TLS_PRF,
  // kPrfSeedSizeTlsPrf, kPmsSize, 0);

  // Output key size != 0, SSL3_MASTER_SECRET_LENGTH
  CheckForError(CKM_TLS_PRF, kPrfSeedSizeTlsPrf, kPmsSize, kIncorrectSize);

  // not-DH && pms size != SSL3_PMS_LENGTH
  CheckForError(CKM_TLS_PRF, kPrfSeedSizeTlsPrf, kIncorrectSize, 0);

  // CKM_TLS_PRF && seed length != MD5_LENGTH + SHA1_LENGTH
  CheckForError(CKM_TLS_PRF, kIncorrectSize, kPmsSize, 0);

  // !CKM_TLS_PRF && seed length != hash output length
  CheckForError(CKM_SHA256, kIncorrectSize, kPmsSize, 0);
}

// Test matrix:
//
//            DH  RSA
//  TLS_PRF   1   2
//  SHA256    3   4
TEST_F(TlsPrfTest, ExtendedMsDhTlsPrf) {
  Init();
  ComputeAndVerifyMs(CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE_DH, CKM_TLS_PRF,
                     nullptr, kExpectedOutputEmsTlsPrf);
}

TEST_F(TlsPrfTest, ExtendedMsRsaTlsPrf) {
  Init();
  ComputeAndVerifyMs(CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE, CKM_TLS_PRF,
                     &pms_version_, kExpectedOutputEmsTlsPrf);
  EXPECT_EQ(0, pms_version_.major);
  EXPECT_EQ(1, pms_version_.minor);
}

TEST_F(TlsPrfTest, ExtendedMsDhSha256) {
  Init();
  ComputeAndVerifyMs(CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE_DH, CKM_SHA256,
                     nullptr, kExpectedOutputEmsSha256);
}

TEST_F(TlsPrfTest, ExtendedMsRsaSha256) {
  Init();
  ComputeAndVerifyMs(CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE, CKM_SHA256,
                     &pms_version_, kExpectedOutputEmsSha256);
  EXPECT_EQ(0, pms_version_.major);
  EXPECT_EQ(1, pms_version_.minor);
}

}  // namespace nss_test
