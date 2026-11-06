/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include "nss.h"
#include "nspr.h"
#include "pk11pub.h"
#include "prerror.h"
#include "prsystem.h"
#include "secoid.h"

#include "nss_scoped_ptrs.h"
#include "gtest/gtest.h"
#include "databuffer.h"
#include "pk11_import.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace nss_test {

class Pkcs11ModuleTest : public ::testing::Test {
 public:
  Pkcs11ModuleTest() {}

  void SetUp() override {
    ASSERT_EQ(SECSuccess, SECMOD_AddNewModule(
                              "Pkcs11ModuleTest",
                              DLL_PREFIX "pkcs11testmodule." DLL_SUFFIX, 0, 0))
        << PORT_ErrorToName(PORT_GetError());
  }

  void TearDown() override {
    int type;

#ifndef NSS_FIPS_DISABLED
    /* restore non-fips if we assert out of the FIPS test */
    if (PK11_IsFIPS() && !SECMOD_GetSystemFIPSEnabled()) {
      char* internal_name =
          PR_smprintf("%s", SECMOD_GetInternalModule()->commonName);
      ASSERT_EQ(SECSuccess, SECMOD_DeleteInternalModule(internal_name))
          << PORT_ErrorToName(PORT_GetError());
      PR_smprintf_free(internal_name);
    }
#endif
    ASSERT_EQ(!!SECMOD_GetSystemFIPSEnabled(), PK11_IsFIPS());
    ASSERT_EQ(SECSuccess, SECMOD_DeleteModule("Pkcs11ModuleTest", &type));
    ASSERT_EQ(SECMOD_EXTERNAL, type);
  }
};

TEST_F(Pkcs11ModuleTest, LoadUnload) {
  ScopedSECMODModule module(SECMOD_FindModule("Pkcs11ModuleTest"));
  EXPECT_NE(nullptr, module);
}

TEST_F(Pkcs11ModuleTest, ListSlots) {
  ScopedPK11SlotList slots(
      PK11_GetAllTokens(CKM_INVALID_MECHANISM, PR_FALSE, PR_FALSE, nullptr));
  ASSERT_NE(nullptr, slots);

  PK11SlotListElement* element = PK11_GetFirstSafe(slots.get());
  ASSERT_NE(nullptr, element);

  // These tokens are always present. In FIPS mode one internal slot does
  // both jobs.
  std::vector<std::string> kSlotsWithToken = {
      "NSS Internal Cryptographic Services",
      "NSS User Private Key and Certificate Services", "Test PKCS11 Auth Slot",
      "Test PKCS11 Public Certs Slot", "Test PKCS11 Slot 二"};
  if (PK11_IsFIPS()) {
    kSlotsWithToken = {"NSS FIPS 140-2 User Private Key Services",
                       "Test PKCS11 Auth Slot", "Test PKCS11 Public Certs Slot",
                       "Test PKCS11 Slot 二"};
  }
  std::vector<std::string> foundSlots;

  do {
    std::string name = PK11_GetSlotName(element->slot);
    foundSlots.push_back(name);
    std::cerr << "loaded slot: " << name << std::endl;
  } while ((element = PK11_GetNextSafe(slots.get(), element, PR_FALSE)) !=
           nullptr);

  std::sort(foundSlots.begin(), foundSlots.end());
  EXPECT_TRUE(std::equal(kSlotsWithToken.begin(), kSlotsWithToken.end(),
                         foundSlots.begin()));
}

TEST_F(Pkcs11ModuleTest, UnwrapPrivateKey) {
  if (PK11_IsFIPS()) {
    GTEST_SKIP() << "FIPS tokens won't hand a private key to another token";
  }
  // test unwrapping a priate key using
  // softoken to do the unwrap and then
  // importing it into a token. Test that
  // this automatically happens through
  // the PK11_Unwrap interface
  const std::string kPublicCertificatesToken = "Test PKCS11 Public Certs Token";
  ScopedPK11SlotInfo targetSlot(
      PK11_FindSlotByName(kPublicCertificatesToken.c_str()));
  ASSERT_NE(nullptr, targetSlot);
  ScopedPK11SlotInfo sourceSlot(PK11_GetInternalSlot());
  ASSERT_NE(nullptr, sourceSlot);

  /* test encrypted key with wrapping key and IV */
  const unsigned char raw_aes_key[] = {
      0x06, 0xd7, 0xb7, 0xd2, 0xbc, 0x84, 0x17, 0xc3, 0x28, 0x42, 0xc4, 0x93,
      0x68, 0xe9, 0xcd, 0x0b, 0xdd, 0x56, 0xf9, 0xab, 0xb1, 0x18, 0x13, 0xbd,
  };
  const unsigned char raw_iv[] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
      0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
      0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  };
  const unsigned char raw_encrypted_key[] = {
      0x88, 0xaa, 0x71, 0x82, 0x5e, 0x29, 0xd9, 0x06, 0x53, 0xae, 0xf7, 0xc6,
      0xe9, 0x38, 0xef, 0x96, 0x3a, 0x19, 0xc6, 0x0e, 0x27, 0x30, 0xff, 0xe4,
      0xb3, 0xff, 0x2f, 0x32, 0x68, 0xd6, 0x0b, 0xc2, 0x8b, 0xce, 0xd3, 0xdc,
      0x46, 0x0f, 0x46, 0x34, 0xa7, 0x00, 0x8f, 0xf6, 0x3d, 0x87, 0xf2, 0x06,
      0x95, 0x7b, 0x2b, 0xf5, 0xc0, 0x5d, 0xcc, 0x79, 0xb7, 0xee, 0x34, 0x77,
      0xa1, 0x72, 0x19, 0xea, 0x70, 0x4f, 0x02, 0xd3, 0x3c, 0x2c, 0xc5, 0x3c,
      0x8a, 0x0b, 0xb8, 0xe1, 0x05, 0x8f, 0x56, 0xf4, 0xcd, 0xd8, 0x88, 0x29,
      0x66, 0x74, 0x15, 0x20, 0xbb, 0xa6, 0xd2, 0x5c, 0xe9, 0x8a, 0xd6, 0x7e,
      0xb4, 0xdd, 0x04, 0xb7, 0xf9, 0x58, 0x8a, 0x13, 0xd1, 0x3f, 0x2b, 0x4b,
      0x35, 0x1c, 0x4b, 0x97, 0x0b, 0xa0, 0xd3, 0xe5, 0x59, 0x3e, 0x61, 0xcc,
      0xaa, 0x51, 0xb6, 0xa6, 0x59, 0x22, 0x98, 0xaa, 0x5a, 0xc2, 0x03, 0xed,
      0xe1, 0xba, 0xbd, 0xfa, 0x7b, 0xe3, 0x82, 0x3f, 0x18, 0x8b, 0xc9, 0xac,
      0x8e, 0x02, 0x72, 0xa0, 0x77, 0x5d, 0xb2, 0x6b, 0xe1, 0x45, 0xe6, 0x66,
      0xbf, 0x37, 0xbc, 0x34, 0x4b, 0xb8, 0xcc, 0x64, 0xdc, 0x89, 0x65, 0xa5,
      0x45, 0x59, 0x22, 0x4b, 0xdc, 0x5e, 0xfe, 0xdc, 0x4a, 0xd6, 0xba, 0x50,
      0xa9, 0x40, 0x87, 0xa3, 0x2c, 0x4f, 0x9e, 0xda, 0x3c, 0xfc, 0x70, 0x1c,
      0x70, 0xc7, 0x63, 0xe4, 0x9b, 0x5f, 0x76, 0x69, 0x02, 0x5a, 0xd0, 0xc2,
      0x04, 0x6a, 0x75, 0x2a, 0x72, 0x92, 0xad, 0x5d, 0x36, 0x1d, 0xa8, 0x9e,
      0x6c, 0xa8, 0xa6, 0xce, 0x34, 0xc4, 0xc9, 0xae, 0xf9, 0x53, 0x95, 0x71,
      0x8a, 0x66, 0xd4, 0xd7, 0x10, 0x7d, 0x90, 0xf7, 0xd6, 0xd2, 0x8e, 0xbd,
      0x94, 0x90, 0xeb, 0x58, 0x72, 0x1a, 0x2a, 0xfd, 0xb2, 0xcb, 0x0d, 0xf6,
      0xf7, 0xcd, 0x4f, 0xdb, 0xfe, 0x32, 0x2f, 0x9a, 0xfd, 0x0f, 0x0e, 0x62,
      0x02, 0x94, 0x19, 0xc6, 0xdf, 0xb1, 0x3d, 0x38, 0x0e, 0x90, 0x81, 0x61,
      0x7e, 0xbc, 0x9e, 0x88, 0xcf, 0xd9, 0x54, 0xdc, 0xf8, 0xf0, 0x91, 0x26,
      0x0e, 0x6b, 0x01, 0xff, 0x7a, 0xf0, 0x4a, 0x1f, 0x04, 0xdd, 0x7d, 0x48,
      0x06, 0xcf, 0x37, 0x2e, 0x9c, 0xec, 0x4f, 0x6c, 0x3c, 0x22, 0x63, 0xf1,
      0xc1, 0xd5, 0xf8, 0x82, 0x83, 0x9a, 0x53, 0x57, 0xa8, 0x62, 0x2e, 0x94,
      0xeb, 0x74, 0x07, 0x95, 0x56, 0xa5, 0xae, 0xba, 0x88, 0x19, 0x9f, 0x78,
      0x9b, 0x47, 0x3d, 0xbd, 0xcc, 0xb5, 0x63, 0x8d, 0xc9, 0xfd, 0x79, 0x66,
      0x07, 0x94, 0xbc, 0x25, 0xb0, 0x31, 0xf1, 0x5c, 0xa4, 0x75, 0xd1, 0x77,
      0xf6, 0xd7, 0x35, 0x94, 0x54, 0x82, 0x13, 0x94, 0x10, 0xba, 0xb8, 0x54,
      0x2b, 0x63, 0x01, 0x44, 0xb1, 0xdd, 0x25, 0xcd, 0xe4, 0x90, 0x3f, 0x43,
      0x8e, 0x28, 0x48, 0x35, 0x7b, 0x9c, 0x6f, 0xdb, 0xdf, 0xce, 0xda, 0xa7,
      0x80, 0xe1, 0xb9, 0x54, 0xaa, 0x16, 0x2a, 0x01, 0xdf, 0x5b, 0x2c, 0xd4,
      0x6d, 0x19, 0xd6, 0x5f, 0x9f, 0x1c, 0xdd, 0x03, 0xe8, 0xbd, 0x45, 0x90,
      0xe9, 0x65, 0x44, 0x3b, 0xde, 0x8f, 0xd9, 0x61, 0x5e, 0x83, 0x06, 0x3c,
      0x59, 0x71, 0x52, 0xd8, 0x81, 0x1e, 0x49, 0x93, 0x4e, 0x47, 0xc2, 0x93,
      0xf6, 0x50, 0x91, 0x94, 0x70, 0x56, 0x68, 0x9d, 0x0f, 0x44, 0x8a, 0x93,
      0xc4, 0x8e, 0x84, 0x42, 0x14, 0x87, 0x56, 0x04, 0x58, 0x36, 0x27, 0x94,
      0x22, 0xfa, 0x8d, 0xe7, 0x21, 0x23, 0xdb, 0x44, 0x7d, 0xe3, 0xae, 0x9c,
      0x67, 0x6c, 0x38, 0xa1, 0x40, 0x1c, 0xf4, 0xb0, 0x23, 0x1a, 0xf3, 0xb8,
      0x01, 0xf6, 0x8b, 0x6b, 0xcb, 0xfa, 0xd9, 0xbf, 0x17, 0x12, 0xcb, 0x5b,
      0x75, 0x37, 0x2b, 0x23, 0x8e, 0xbc, 0xf5, 0xcc, 0xff, 0xc8, 0x87, 0x45,
      0x51, 0x6c, 0xd0, 0xaa, 0x5b, 0x62, 0x58, 0x36, 0x2d, 0x46, 0x1d, 0xb7,
      0x47, 0x9c, 0x08, 0x63, 0xb2, 0xcf, 0x0b, 0xb2, 0x77, 0x2e, 0x43, 0x3d,
      0xfb, 0x50, 0x9c, 0x75, 0xd6, 0xfe, 0x6b, 0x36, 0xee, 0x1c, 0xb5, 0xed,
      0xcb, 0x94, 0xa8, 0xb8, 0xea, 0xb4, 0x3c, 0x71, 0x6c, 0x3b, 0x98, 0x2c,
      0xdf, 0x4d, 0x00, 0xdc, 0x52, 0x0d, 0xce, 0xde, 0x72, 0x70, 0x81, 0x85,
      0x56, 0xbb, 0x2d, 0x28, 0xca, 0xf7, 0x32, 0x0b, 0xda, 0xb8, 0x5b, 0x9f,
      0x31, 0x09, 0x32, 0x33, 0x0e, 0x28, 0x5b, 0x38, 0xa6, 0x55, 0xc4, 0x3a,
      0xce, 0xa5, 0x03, 0x77, 0xce, 0x40, 0x64, 0xb7, 0xc7, 0x99, 0x87, 0x80,
      0x3c, 0x48, 0xcf, 0xcf, 0x40, 0xc2, 0x26, 0xe6, 0xaf, 0xe6, 0x71, 0xf9,
      0x16, 0xa1, 0xcc, 0x2d, 0x61, 0x47, 0xea, 0xd2, 0xfb, 0x47, 0x08, 0xdb,
      0x65, 0x70, 0xbc, 0x6f,
  };
  SECItem aes_key = {siBuffer, (unsigned char*)raw_aes_key,
                     sizeof(raw_aes_key)};
  SECItem encrypted_key = {siBuffer, (unsigned char*)raw_encrypted_key,
                           sizeof(raw_encrypted_key)};
  SECItem iv = {siBuffer, (unsigned char*)raw_iv, sizeof(raw_iv)};

  ScopedPK11SymKey wrappingKey(
      nss_test::ImportSymKey(sourceSlot.get(), CKM_AES_CBC_PAD,
                             PK11_OriginUnwrap, CKA_UNWRAP, &aes_key, NULL));
  ASSERT_NE(nullptr, wrappingKey);

  ScopedSECKEYPrivateKey privKey(PK11_UnwrapPrivKey(
      targetSlot.get(), wrappingKey.get(), CKM_AES_CBC_PAD, &iv, &encrypted_key,
      NULL, NULL, PR_TRUE, PR_TRUE, CKK_RSA, NULL, 0, NULL));
  EXPECT_NE(nullptr, privKey) << PORT_ErrorToName(PORT_GetError()) << ": "
                              << PORT_ErrorToString(PORT_GetError());
}

TEST_F(Pkcs11ModuleTest, PublicCertificatesToken) {
  const std::string kRegularToken = "Test PKCS11 Tokeñ 2 Label";
  const std::string kPublicCertificatesToken = "Test PKCS11 Public Certs Token";

  ScopedPK11SlotInfo slot1(PK11_FindSlotByName(kRegularToken.c_str()));
  ASSERT_NE(nullptr, slot1);
  EXPECT_FALSE(PK11_IsFriendly(slot1.get()));

  ScopedPK11SlotInfo slot2(
      PK11_FindSlotByName(kPublicCertificatesToken.c_str()));
  ASSERT_NE(nullptr, slot2);
  EXPECT_TRUE(PK11_IsFriendly(slot2.get()));
}

TEST_F(Pkcs11ModuleTest, PublicCertificatesTokenLookup) {
  const std::string kCertUrl =
      "pkcs11:id=%10%11%12%13%14%15%16%17%18%19%1a%1b%1c%1d%1e%1f";

  ScopedCERTCertList certsByUrl(
      PK11_FindCertsFromURI(kCertUrl.c_str(), nullptr));
  EXPECT_NE(nullptr, certsByUrl.get());

  size_t count = 0;
  CERTCertificate* certByUrl = nullptr;
  for (CERTCertListNode* node = CERT_LIST_HEAD(certsByUrl);
       !CERT_LIST_END(node, certsByUrl); node = CERT_LIST_NEXT(node)) {
    if (count == 0) {
      certByUrl = node->cert;
    }
    count++;
  }
  EXPECT_EQ(1UL, count);
  EXPECT_NE(nullptr, certByUrl);

  EXPECT_EQ(
      0, strcmp(certByUrl->nickname, "Test PKCS11 Public Certs Token:cert2"));
}

TEST_F(Pkcs11ModuleTest, PublicCertificatesTokenLookupNoMatch) {
  const std::string kCertUrl =
      "pkcs11:id=%00%01%02%03%04%05%06%07%08%09%0a%0b%0c%0d%0e%0e";

  ScopedCERTCertList certsByUrl(
      PK11_FindCertsFromURI(kCertUrl.c_str(), nullptr));
  EXPECT_EQ(nullptr, certsByUrl.get());
}

TEST_F(Pkcs11ModuleTest, PK11_UnwrapTestFIPS) {
#ifndef NSS_FIPS_DISABLED
  // fips mode, unless the system already forces it
  char* internal_name;
  bool toggleFips = !SECMOD_GetSystemFIPSEnabled();
  if (toggleFips) {
    ASSERT_FALSE(PK11_IsFIPS());
    internal_name = PR_smprintf("%s", SECMOD_GetInternalModule()->commonName);
    ASSERT_EQ(SECSuccess, SECMOD_DeleteInternalModule(internal_name))
        << PORT_ErrorToName(PORT_GetError());
    PR_smprintf_free(internal_name);
    internal_name = NULL;
  }
  ASSERT_TRUE(PK11_IsFIPS());
#endif

  const std::string kCertUrl =
      "pkcs11:id=%10%11%12%13%14%15%16%17%18%19%1a%1b%1c%1d%1e%1f";

  ScopedCERTCertList certsByUrl(
      PK11_FindCertsFromURI(kCertUrl.c_str(), nullptr));
  EXPECT_NE(nullptr, certsByUrl.get());

  size_t count = 0;
  CERTCertificate* certByUrl = nullptr;
  for (CERTCertListNode* node = CERT_LIST_HEAD(certsByUrl);
       !CERT_LIST_END(node, certsByUrl); node = CERT_LIST_NEXT(node)) {
    if (count == 0) {
      certByUrl = node->cert;
    }
    count++;
  }
  EXPECT_EQ(1UL, count);
  ASSERT_NE(nullptr, certByUrl);

  /* get private key */
  ScopedSECKEYPrivateKey privkey(
      PK11_FindPrivateKeyFromCert(certByUrl->slot, certByUrl, NULL));
  ASSERT_NE(nullptr, privkey.get()) << PORT_ErrorToName(PORT_GetError());

  /* symkey = PK11 Unwrap */
  unsigned char wrappedKeyData[1024] = {0};
  SECItem wrapppedKey = {siBuffer, wrappedKeyData, sizeof(wrappedKeyData)};
  ScopedPK11SymKey symkey(PK11_PubUnwrapSymKey(privkey.get(), &wrapppedKey,
                                               CKM_AES_ECB, CKA_ENCRYPT, 0));
  /* verify symkey is in the slot */
  ASSERT_NE(nullptr, symkey.get()) << PORT_ErrorToName(PORT_GetError());
  ScopedPK11SlotInfo slot(PK11_GetSlotFromKey(symkey.get()));
  ASSERT_NE(nullptr, slot);
  EXPECT_TRUE(PK11_IsInternal(slot.get()));

  /* encrypt a sample date with the key to verify unwrap operated correctly */
#define DATA_SIZE 32
  unsigned char expectedDataOut[DATA_SIZE] = {
      0x4c, 0x60, 0x97, 0x79, 0xd6, 0xfe, 0x9e, 0x36, 0xda, 0x3a, 0xbe,
      0x85, 0x9a, 0xaf, 0x7f, 0xfa, 0x5f, 0x4e, 0xb9, 0x3e, 0x2e, 0x0d,
      0xe5, 0xc4, 0x6a, 0xf7, 0x22, 0xf7, 0x4c, 0xaf, 0xa6, 0x50,
  };
  unsigned int expectedDataOutLen = sizeof(expectedDataOut);
  unsigned char dataIn[DATA_SIZE] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
      0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
      0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  };
  unsigned char dataOut[DATA_SIZE];
  unsigned int dataOutLen;
  SECItem param = {siBuffer, nullptr, 0};

  SECStatus rv =
      PK11_Encrypt(symkey.get(), CKM_AES_ECB, &param, dataOut, &dataOutLen,
                   sizeof(dataOut), dataIn, sizeof(dataIn));
  ASSERT_EQ(SECSuccess, rv) << PORT_ErrorToName(PORT_GetError());
  ASSERT_EQ(expectedDataOutLen, dataOutLen) << "dataOutlen =" << dataOutLen;
  EXPECT_EQ(0, PORT_Memcmp(dataOut, expectedDataOut, expectedDataOutLen));

#ifndef NSS_FIPS_DISABLED
  ASSERT_TRUE(PK11_IsFIPS());
  if (toggleFips) {
    internal_name = PR_smprintf("%s", SECMOD_GetInternalModule()->commonName);
    ASSERT_EQ(SECSuccess, SECMOD_DeleteInternalModule(internal_name))
        << PORT_ErrorToName(PORT_GetError());
    PR_smprintf_free(internal_name);
    internal_name = NULL;
    ASSERT_FALSE(PK11_IsFIPS());
  }
#endif
}

#if defined(_WIN32)

class Pkcs11NonAsciiTest : public ::testing::Test {
  WCHAR nonAsciiModuleName[MAX_PATH];

 public:
  Pkcs11NonAsciiTest() {}

  void SetUp() override {
    WCHAR originalModuleName[MAX_PATH];
    LPWSTR filePart;
    DWORD count = SearchPathW(NULL, L"pkcs11testmodule.dll", NULL, MAX_PATH,
                              nonAsciiModuleName, &filePart);
    ASSERT_TRUE(count);
    wcscpy(originalModuleName, nonAsciiModuleName);
    wcscpy(filePart, L"pkcs11testmodule\u2665.dll");
    BOOL result = CopyFileW(originalModuleName, nonAsciiModuleName, TRUE);
    ASSERT_TRUE(result);
    ASSERT_EQ(SECSuccess,
              SECMOD_AddNewModule(
                  "Pkcs11NonAsciiTest",
                  DLL_PREFIX "pkcs11testmodule\xE2\x99\xA5." DLL_SUFFIX, 0, 0))
        << PORT_ErrorToName(PORT_GetError());
  }

  void TearDown() override {
    int type;
    ASSERT_EQ(SECSuccess, SECMOD_DeleteModule("Pkcs11NonAsciiTest", &type));
    ASSERT_EQ(SECMOD_EXTERNAL, type);
    BOOL result = DeleteFileW(nonAsciiModuleName);
    ASSERT_TRUE(result);
  }
};

TEST_F(Pkcs11NonAsciiTest, LoadUnload) {
  ScopedSECMODModule module(SECMOD_FindModule("Pkcs11NonAsciiTest"));
  EXPECT_NE(nullptr, module);
}
#endif  // defined(_WIN32)

class Pkcs11ModuleLoadFunctionTest : public ::testing::Test {
 public:
  Pkcs11ModuleLoadFunctionTest() { library = NULL; };

  void TearDown() override {
    if (library != NULL) {
      PR_UnloadLibrary(library);
    }
  }
  PRLibrary* library;
};

CK_RV NotSuppoted_GetFunctionList(CK_FUNCTION_LIST_PTR_PTR ppFunctionList) {
  return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV SupportedButNull(CK_FUNCTION_LIST_PTR_PTR ppFunctionList) {
  ppFunctionList = NULL;
  return CKR_OK;
}

TEST_F(Pkcs11ModuleLoadFunctionTest, LoadModuleWithNullFunc) {
  ScopedSECMODModule userModule(
      SECMOD_LoadUserModuleWithFunction("LoadFunctionModule", NULL));
  EXPECT_NE(userModule, nullptr);
  EXPECT_FALSE(userModule->loaded);
}

TEST_F(Pkcs11ModuleLoadFunctionTest, LoadModuleWithUnsupportedFunc) {
  ScopedSECMODModule userModule(SECMOD_LoadUserModuleWithFunction(
      "LoadFunctionModule", &NotSuppoted_GetFunctionList));
  EXPECT_FALSE(userModule->loaded);
}

TEST_F(Pkcs11ModuleLoadFunctionTest, LoadModuleWithEmptyFunctionList) {
  ScopedSECMODModule userModule(SECMOD_LoadUserModuleWithFunction(
      "LoadFunctionModule", &SupportedButNull));
  EXPECT_NE(userModule, nullptr);
  EXPECT_FALSE(userModule->loaded);
}

TEST_F(Pkcs11ModuleLoadFunctionTest, SuccessLoadModuleWithFunction) {
  library = PR_LoadLibrary(DLL_PREFIX "pkcs11testmodule." DLL_SUFFIX);
  EXPECT_NE(nullptr, library);

  CK_C_GetFunctionList fentry = NULL;
  fentry = (CK_C_GetFunctionList)PR_FindSymbol(library, "C_GetFunctionList");
  EXPECT_NE(nullptr, fentry);

  ScopedSECMODModule userModule(
      SECMOD_LoadUserModuleWithFunction("LoadFunctionModule", fentry));
  EXPECT_NE(nullptr, userModule);
  EXPECT_EQ(userModule->loaded, PR_TRUE);

  /* We can find the module*/
  ScopedSECMODModule module(SECMOD_FindModule("LoadFunctionModule"));
  EXPECT_NE(nullptr, module);

  CK_INFO info;
  EXPECT_EQ(SECSuccess, PK11_GetModInfo(userModule.get(), &info));
  /* See pkcs11testmodule.cpp */
  CK_VERSION expectedCryptokiVersion = {2, 2};
  CK_VERSION expectedLibraryVersion = {0, 0};
  EXPECT_EQ(info.cryptokiVersion.minor, expectedCryptokiVersion.minor);
  EXPECT_EQ(info.cryptokiVersion.major, expectedCryptokiVersion.major);

  EXPECT_EQ(
      0, PORT_Memcmp((char*)info.manufacturerID, "Test PKCS11 Manufacturer ID",
                     sizeof("Test PKCS11 Manufacturer ID") - 1));
  // EXPECT_EQ takes its arguments by reference, and on Windows, where the
  // PKCS#11 structures are packed, info.flags is misaligned. Copy it.
  EXPECT_EQ(static_cast<CK_FLAGS>(info.flags), 0UL);

  EXPECT_EQ(0,
            PORT_Memcmp((char*)info.libraryDescription, "Test PKCS11 Library",
                        sizeof("Test PKCS11 Library") - 1));
  EXPECT_EQ(info.libraryVersion.minor, expectedLibraryVersion.minor);
  EXPECT_EQ(info.libraryVersion.major, expectedLibraryVersion.major);

  EXPECT_EQ(SECSuccess, SECMOD_UnloadUserModule(userModule.get()));
}

}  // namespace nss_test
