// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gtest/gtest.h"
#include "nss_scoped_ptrs.h"
#include "pk11_import.h"

#include <assert.h>
#include <limits.h>
#include <prinit.h>
#include <nss.h>
#include <pk11pub.h>

namespace nss_test {

uint8_t kKeyData[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
SECItem kFull = {siBuffer, (unsigned char*)kKeyData, 16};
SECItem kLeftHalf = {siBuffer, (unsigned char*)kKeyData, 8};
SECItem kRightHalf = {siBuffer, (unsigned char*)kKeyData + 8, 8};

class Pkcs11SymKeyTest : public ::testing::Test {
 protected:
  PK11SymKey* ImportSymKey(PK11SlotInfo* slot, SECItem* key_data) {
    PK11SymKey* out = nss_test::ImportSymKey(slot, CKM_NULL, PK11_OriginUnwrap,
                                             CKA_DERIVE, key_data, nullptr);
    EXPECT_NE(nullptr, out);
    return out;
  }

  void CheckKeyData(SECItem& expected, PK11SymKey* actual) {
    ASSERT_NE(nullptr, actual);

    SECStatus rv = nss_test::ExtractKeyValue(actual);
    ASSERT_EQ(SECSuccess, rv);

    SECItem* keyData = nss_test::GetKeyData(actual);
    ASSERT_NE(nullptr, keyData);
    ASSERT_NE(nullptr, keyData->data);
    ASSERT_EQ(expected.len, keyData->len);
    ASSERT_EQ(0, memcmp(expected.data, keyData->data, keyData->len));
  }

  void SetSensitive(PK11SymKey* key) {
    ASSERT_NE(nullptr, key);

    CK_BBOOL cktrue = CK_TRUE;
    SECItem attrValue = {siBuffer, &cktrue, sizeof(CK_BBOOL)};
    EXPECT_EQ(SECSuccess, PK11_WriteRawAttribute(PK11_TypeSymKey, key,
                                                 CKA_SENSITIVE, &attrValue));
  }

  void CheckIsSensitive(PK11SymKey* key) {
    ASSERT_NE(nullptr, key);

    StackSECItem attrValue;
    ASSERT_EQ(SECSuccess, PK11_ReadRawAttribute(PK11_TypeSymKey, key,
                                                CKA_SENSITIVE, &attrValue));
    ASSERT_EQ(attrValue.len, sizeof(CK_BBOOL));
    EXPECT_EQ(*(CK_BBOOL*)attrValue.data, CK_TRUE);
  }

  void SetNotExtractable(PK11SymKey* key) {
    ASSERT_NE(nullptr, key);

    CK_BBOOL ckfalse = CK_FALSE;
    SECItem attrValue = {siBuffer, &ckfalse, sizeof(CK_BBOOL)};
    EXPECT_EQ(SECSuccess, PK11_WriteRawAttribute(PK11_TypeSymKey, key,
                                                 CKA_EXTRACTABLE, &attrValue));
  }

  void CheckIsNotExtractable(PK11SymKey* key) {
    ASSERT_NE(nullptr, key);

    StackSECItem attrValue;
    ASSERT_EQ(SECSuccess, PK11_ReadRawAttribute(PK11_TypeSymKey, key,
                                                CKA_EXTRACTABLE, &attrValue));
    ASSERT_EQ(attrValue.len, sizeof(CK_BBOOL));
    EXPECT_EQ(*(CK_BBOOL*)attrValue.data, CK_FALSE);
  }
};

TEST_F(Pkcs11SymKeyTest, ConcatSymKeyTest) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_NE(nullptr, slot);

  ScopedPK11SymKey left(ImportSymKey(slot.get(), &kLeftHalf));

  ScopedPK11SymKey right(ImportSymKey(slot.get(), &kRightHalf));

  ScopedPK11SymKey key(
      PK11_ConcatSymKeys(left.get(), right.get(), CKM_HKDF_DERIVE, CKA_DERIVE));
  CheckKeyData(kFull, key.get());
}

// Test that the derived key is sensitive if either input is.
TEST_F(Pkcs11SymKeyTest, SensitiveConcatSymKeyTest) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_NE(nullptr, slot);

  ScopedPK11SymKey left(ImportSymKey(slot.get(), &kLeftHalf));
  SetSensitive(left.get());

  ScopedPK11SymKey right(ImportSymKey(slot.get(), &kRightHalf));

  ScopedPK11SymKey key(
      PK11_ConcatSymKeys(left.get(), right.get(), CKM_HKDF_DERIVE, CKA_DERIVE));
  CheckIsSensitive(key.get());

  // Again with left and right swapped
  ScopedPK11SymKey key2(
      PK11_ConcatSymKeys(right.get(), left.get(), CKM_HKDF_DERIVE, CKA_DERIVE));
  CheckIsSensitive(key2.get());
}

// Test that the derived key is extractable if either input is.
TEST_F(Pkcs11SymKeyTest, NotExtractableConcatSymKeyTest) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_NE(nullptr, slot);

  ScopedPK11SymKey left(ImportSymKey(slot.get(), &kLeftHalf));
  SetNotExtractable(left.get());

  ScopedPK11SymKey right(ImportSymKey(slot.get(), &kRightHalf));

  ScopedPK11SymKey key(
      PK11_ConcatSymKeys(left.get(), right.get(), CKM_HKDF_DERIVE, CKA_DERIVE));
  CheckIsNotExtractable(key.get());

  ScopedPK11SymKey key2(
      PK11_ConcatSymKeys(right.get(), left.get(), CKM_HKDF_DERIVE, CKA_DERIVE));
  CheckIsNotExtractable(key2.get());
}

// Test that keys can be concatenated on the key slot.
TEST_F(Pkcs11SymKeyTest, KeySlotConcatSymKeyTest) {
  ScopedPK11SlotInfo slot(PK11_GetInternalKeySlot());
  ASSERT_NE(nullptr, slot);

  ScopedPK11SymKey left(ImportSymKey(slot.get(), &kLeftHalf));

  ScopedPK11SymKey right(ImportSymKey(slot.get(), &kRightHalf));

  ScopedPK11SymKey key(
      PK11_ConcatSymKeys(left.get(), right.get(), CKM_HKDF_DERIVE, CKA_DERIVE));
  CheckKeyData(kFull, key.get());
}

// Test that keys in different slots are moved to the same slot for derivation.
// The PK11SymKey.data fields are set in PK11_ImportSymKey, so this just
// re-imports the key data.
TEST_F(Pkcs11SymKeyTest, CrossSlotConcatSymKeyTest) {
  if (PK11_IsFIPS()) {
    GTEST_SKIP() << "FIPS mode has a single internal slot";
  }
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_NE(nullptr, slot);

  ScopedPK11SlotInfo slot2(PK11_GetInternalKeySlot());
  ASSERT_NE(nullptr, slot2);

  EXPECT_NE(slot, slot2);

  ScopedPK11SymKey left(ImportSymKey(slot.get(), &kLeftHalf));

  ScopedPK11SymKey right(ImportSymKey(slot2.get(), &kRightHalf));

  ScopedPK11SymKey key(
      PK11_ConcatSymKeys(left.get(), right.get(), CKM_HKDF_DERIVE, CKA_DERIVE));
  CheckKeyData(kFull, key.get());
}

// Attribute values move between inline storage and the heap as they are
// rewritten; every transition must survive reads and object destruction.
TEST_F(Pkcs11SymKeyTest, AttributeValueResize) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_NE(nullptr, slot);

  uint8_t longKey[64];
  for (size_t i = 0; i < sizeof(longKey); i++) {
    longKey[i] = static_cast<uint8_t>(i);
  }
  SECItem longKeyItem = {siBuffer, longKey, sizeof(longKey)};

  uint8_t label[300];
  for (size_t i = 0; i < sizeof(label); i++) {
    label[i] = static_cast<uint8_t>('a' + i % 26);
  }
  for (SECItem* keyData : {&kFull, &longKeyItem}) {
    ScopedPK11SymKey key(ImportSymKey(slot.get(), keyData));
    ASSERT_NE(nullptr, key);
    for (unsigned int len : {1, 50, 51, 300, 50, 1, 0, 300, 0, 51}) {
      SECItem value = {siBuffer, label, len};
      ASSERT_EQ(SECSuccess, PK11_WriteRawAttribute(PK11_TypeSymKey, key.get(),
                                                   CKA_LABEL, &value));
      StackSECItem readBack;
      ASSERT_EQ(SECSuccess, PK11_ReadRawAttribute(PK11_TypeSymKey, key.get(),
                                                  CKA_LABEL, &readBack));
      ASSERT_EQ(len, readBack.len);
      if (len) {
        EXPECT_EQ(0, memcmp(label, readBack.data, len));
      }
    }
    CheckKeyData(*keyData, key.get());
  }
}

}  // namespace nss_test
