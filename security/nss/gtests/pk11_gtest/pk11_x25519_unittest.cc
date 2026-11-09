/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include "nss.h"

#include "json_reader.h"
#include "nss_scoped_ptrs.h"

#include "cpputil.h"
#include "pk11_x25519_vectors.h"
#include "pk11_signature_test.h"
#include "pk11_keygen.h"
#include "pk11_import.h"

namespace nss_test {

// For test vectors.
struct Pkcs11X25519ImportParams {
  const DataBuffer pkcs8_;
  const DataBuffer spki_;
};

static const Pkcs11X25519ImportParams kX25519Vectors[] = {
    {
        DataBuffer(kX25519Pkcs8_1, sizeof(kX25519Pkcs8_1)),
        DataBuffer(kX25519Spki_1, sizeof(kX25519Spki_1)),
    },
};

class Pkcs11X25519Test
    : public ::testing::Test,
      public ::testing::WithParamInterface<Pkcs11X25519ImportParams> {
 protected:
  ScopedSECKEYPrivateKey ImportPrivateKey(const DataBuffer& pkcs8) {
    ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
    if (!slot) {
      ADD_FAILURE() << "No slot";
      return nullptr;
    }

    SECItem pkcs8Item = {siBuffer, toUcharPtr(pkcs8.data()),
                         static_cast<unsigned int>(pkcs8.len())};

    SECKEYPrivateKey* key = nullptr;
    SECStatus rv = nss_test::ImportDERPrivateKeyInfoAndReturnKey(
        slot.get(), &pkcs8Item, nullptr, nullptr, false, false, KU_ALL, &key,
        nullptr);

    if (rv != SECSuccess) {
      return nullptr;
    }

    return ScopedSECKEYPrivateKey(key);
  }

  bool ExportPrivateKey(ScopedSECKEYPrivateKey* key, DataBuffer& pkcs8) {
    ScopedSECItem pkcs8Item(
        nss_test::ExportDERPrivateKeyInfo(key->get(), nullptr));
    if (!pkcs8Item) {
      return false;
    }
    pkcs8.Assign(pkcs8Item->data, pkcs8Item->len);
    return true;
  }

  ScopedSECKEYPublicKey ImportPublicKey(const DataBuffer& spki) {
    SECItem spkiItem = {siBuffer, toUcharPtr(spki.data()),
                        static_cast<unsigned int>(spki.len())};

    ScopedCERTSubjectPublicKeyInfo certSpki(
        SECKEY_DecodeDERSubjectPublicKeyInfo(&spkiItem));
    if (!certSpki) {
      return nullptr;
    }

    return ScopedSECKEYPublicKey(SECKEY_ExtractPublicKey(certSpki.get()));
  }

  bool CheckAlgIsX25519(SECItem* algorithm) {
    SECOidTag tag = SECOID_FindOIDTag(algorithm);
    if (tag != SEC_OID_X25519) {
      return false;
    }

    return true;
  }
};

TEST_P(Pkcs11X25519Test, ImportExportPkcs8) {
  DataBuffer exported;
  ScopedSECKEYPrivateKey key = ImportPrivateKey(GetParam().pkcs8_);
  EXPECT_EQ(key.get()->keyType, ecMontKey);

  ScopedSECKEYPrivateKeyInfo pkInfo(
      nss_test::ExportPrivKeyInfo(key.get(), nullptr));
  ASSERT_TRUE(pkInfo);
  /* empty parameters for X25519*/
  ASSERT_EQ(pkInfo->algorithm.parameters.len, (unsigned int)0);
  ASSERT_TRUE(CheckAlgIsX25519(&pkInfo->algorithm.algorithm));
  ExportPrivateKey(&key, exported);
  EXPECT_EQ(GetParam().pkcs8_, exported);
}

TEST_P(Pkcs11X25519Test, ImportExportSpki) {
  DataBuffer exported;
  ScopedSECKEYPublicKey key = ImportPublicKey(GetParam().spki_);

  ScopedSECItem spki(SECKEY_EncodeDERSubjectPublicKeyInfo(key.get()));
  ASSERT_TRUE(spki);
  ASSERT_EQ(spki->len, GetParam().spki_.len());
  ASSERT_EQ(0, memcmp(spki->data, GetParam().spki_.data(), spki->len));
}

TEST_P(Pkcs11X25519Test, ImportConvertToPublicExport) {
  ScopedSECKEYPrivateKey privKey(ImportPrivateKey(GetParam().pkcs8_));
  ASSERT_TRUE(privKey);

  ScopedSECKEYPublicKey pubKey(SECKEY_ConvertToPublicKey(privKey.get()));
  ASSERT_TRUE(pubKey);

  ScopedSECItem der_spki(SECKEY_EncodeDERSubjectPublicKeyInfo(pubKey.get()));
  ASSERT_TRUE(der_spki);
  ASSERT_EQ(der_spki->len, GetParam().spki_.len());
  ASSERT_EQ(0, memcmp(der_spki->data, GetParam().spki_.data(), der_spki->len));
}

TEST_P(Pkcs11X25519Test, GenImportExport) {
  Pkcs11KeyPairGenerator generator(CKM_EC_MONTGOMERY_KEY_PAIR_GEN);
  ScopedSECKEYPrivateKey priv;
  ScopedSECKEYPublicKey pub;

  generator.GenerateKey(&priv, &pub, false);
  ASSERT_TRUE(priv);
  ASSERT_TRUE(pub);

  DataBuffer exportedPrivateKey, twiceExportedPrKey;
  ExportPrivateKey(&priv, exportedPrivateKey);
  ScopedSECKEYPrivateKey privExportedImported =
      ImportPrivateKey(exportedPrivateKey);
  ExportPrivateKey(&privExportedImported, twiceExportedPrKey);
  EXPECT_EQ(exportedPrivateKey, twiceExportedPrKey);

  ScopedSECItem spki(SECKEY_EncodeDERSubjectPublicKeyInfo(pub.get()));
  ASSERT_TRUE(spki);

  DataBuffer publicKeyDb(spki.get()->data, spki.get()->len);
  ScopedSECKEYPublicKey exportedImportedPublicKey =
      ImportPublicKey(publicKeyDb);
  ScopedSECItem spkiTwice(
      SECKEY_EncodeDERSubjectPublicKeyInfo(exportedImportedPublicKey.get()));
  ASSERT_TRUE(spkiTwice);

  ASSERT_EQ(spkiTwice->len, spki->len);
  ASSERT_EQ(0, memcmp(spki->data, spkiTwice->data, spki->len));
}

INSTANTIATE_TEST_SUITE_P(Pkcs11X25519Test, Pkcs11X25519Test,
                         ::testing::ValuesIn(kX25519Vectors));

/*
    RFC 8410 describes several scenarios with the potential errors during
   exporting/encoding of the keys. See:
   https://www.rfc-editor.org/rfc/rfc8410#appendix-A. */

/*
  PKCS8 (private) X25519 key explanation:
  NB: NSS does not currently support PKCS8 keys with the public key as an
  attribute.

  const uint8_t kX25519Pkcs8_1[] = {
    0x30, 0x2e, // where 0x2e is the length of the buffer
      0x02, 0x01, 0x00, // EC key version
        id-X25519    OBJECT IDENTIFIER ::= { 1 3 101 110 }
      0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x6e, // algorithm identifier
      0x04, 0x22, // Outer octet string of length 0x22
      0x04, 0x20, // Inner octet string of length 0x20

    0xc8, 0x83, 0x8e, 0x76, 0xd0, 0x57, 0xdf, 0xb7, // Raw key
    0xd8, 0xc9, 0x5a, 0x69, 0xe1, 0x38, 0x16, 0x0a,
    0xdd, 0x63, 0x73, 0xfd, 0x71, 0xa4, 0xd2, 0x76,
    0xbb, 0x56, 0xe3, 0xa8, 0x1b, 0x64, 0xff, 0x61};

*/

/* Private Key ASN.1 encoding errors */
TEST_F(Pkcs11X25519Test, ImportPkcs8BitStringInsteadOfOctetString) {
  const uint8_t kX25519BitString[] = {
      0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x6e,
      0x04, 0x22, 0x03, 0x20, 0xc8, 0x83, 0x8e, 0x76, 0xd0, 0x57, 0xdf, 0xb7,
      0xd8, 0xc9, 0x5a, 0x69, 0xe1, 0x38, 0x16, 0x0a, 0xdd, 0x63, 0x73, 0xfd,
      0x71, 0xa4, 0xd2, 0x76, 0xbb, 0x56, 0xe3, 0xa8, 0x1b, 0x64, 0xff, 0x61};

  DataBuffer privateKeyPkcs8(
      DataBuffer(kX25519BitString, sizeof(kX25519BitString)));
  ScopedSECKEYPrivateKey key = ImportPrivateKey(privateKeyPkcs8);
  ASSERT_FALSE(key);
}

TEST_F(Pkcs11X25519Test, ImportPkcs8WrongLen) {
  /* The pkcs8 encoding has a wrong length (0x2d instead of 0x2e) */
  const uint8_t x25519_wrongLen[] = {
      0x30, 0x2d, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x6e,
      0x04, 0x22, 0x04, 0x20, 0xc8, 0x83, 0x8e, 0x76, 0xd0, 0x57, 0xdf, 0xb7,
      0xd8, 0xc9, 0x5a, 0x69, 0xe1, 0x38, 0x16, 0x0a, 0xdd, 0x63, 0x73, 0xfd,
      0x71, 0xa4, 0xd2, 0x76, 0xbb, 0x56, 0xe3, 0xa8, 0x1b, 0x64, 0xff, 0x61};

  DataBuffer privateKeyPkcs8(
      DataBuffer(x25519_wrongLen, sizeof(x25519_wrongLen)));
  ScopedSECKEYPrivateKey key = ImportPrivateKey(privateKeyPkcs8);
  ASSERT_FALSE(key);
}

/* Key encoding errors */
TEST_F(Pkcs11X25519Test, ImportPkcs8NotSupportedOID) {
  /* The modified oid corresponds to not-supported x448:
    id-X448      OBJECT IDENTIFIER ::= { 1 3 101 111 }. */
  const uint8_t x25519_wrongOID[] = {
      0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x6f,
      0x04, 0x22, 0x04, 0x20, 0xc8, 0x83, 0x8e, 0x76, 0xd0, 0x57, 0xdf, 0xb7,
      0xd8, 0xc9, 0x5a, 0x69, 0xe1, 0x38, 0x16, 0x0a, 0xdd, 0x63, 0x73, 0xfd,
      0x71, 0xa4, 0xd2, 0x76, 0xbb, 0x56, 0xe3, 0xa8, 0x1b, 0x64, 0xff, 0x61};

  DataBuffer privateKeyPkcs8(
      DataBuffer(x25519_wrongOID, sizeof(x25519_wrongOID)));
  ScopedSECKEYPrivateKey key = ImportPrivateKey(privateKeyPkcs8);
  ASSERT_FALSE(key);
}

TEST_F(Pkcs11X25519Test, ImportPkcs8ShortLenPrivateKey) {
  // FIPS tokens import by unwrapping, and softoken's PKCS#8 parser is
  // stricter about this than pk11wrap's.
  if (PK11_IsFIPS()) {
    GTEST_SKIP() << "softoken rejects short X25519 keys on unwrap";
  }
  /* We change the length of the private key from 0x20 to 0x1f.
     Such way all the lengths will be decreased by one */
  const uint8_t x25519_shortPrivateKey[] = {
      0x30, 0x2d,  // the length is decreased by one
      0x02, 0x01, 0x00, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x6e, 0x04,
      0x21,        // the length is decreased by one
      0x04, 0x1f,  // the length is decreased by one
      0xc8, 0x83, 0x8e, 0x76, 0xd0, 0x57, 0xdf, 0xb7, 0xd8, 0xc9, 0x5a, 0x69,
      0xe1, 0x38, 0x16, 0x0a, 0xdd, 0x63, 0x73, 0xfd, 0x71, 0xa4, 0xd2, 0x76,
      // removed the last byte of the key
      0xbb, 0x56, 0xe3, 0xa8, 0x1b, 0x64, 0xff};

  DataBuffer privateKeyPkcs8(
      DataBuffer(x25519_shortPrivateKey, sizeof(x25519_shortPrivateKey)));
  ScopedSECKEYPrivateKey key = ImportPrivateKey(privateKeyPkcs8);
  ASSERT_TRUE(key);
}

/* We allow importing all-zero keys*/
TEST_F(Pkcs11X25519Test, ImportPkcs8ZeroKey) {
  const uint8_t x25519_ZeroKey[] = {
      0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x6e,
      0x04, 0x22, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  DataBuffer privateKeyPkcs8(
      DataBuffer(x25519_ZeroKey, sizeof(x25519_ZeroKey)));
  ScopedSECKEYPrivateKey key = ImportPrivateKey(privateKeyPkcs8);
  ASSERT_TRUE(key);
}

TEST_P(Pkcs11X25519Test, KeyGeneration) {
  Pkcs11KeyPairGenerator generator(CKM_EC_MONTGOMERY_KEY_PAIR_GEN);
  ScopedSECKEYPrivateKey priv;
  ScopedSECKEYPublicKey pub;

  generator.GenerateKey(&priv, &pub, false);
  ASSERT_TRUE(priv);
  ASSERT_TRUE(pub);

  ScopedSECKEYPrivateKeyInfo pkInfo(
      nss_test::ExportPrivKeyInfo(priv.get(), nullptr));
  ASSERT_TRUE(pkInfo);
  /* 0x04 + len +  32 bytes the key */
  ASSERT_EQ(pkInfo->privateKey.len, (unsigned int)34);
  /* empty parameters for X25519*/
  ASSERT_EQ(pkInfo->algorithm.parameters.len, (unsigned int)0);
  ASSERT_TRUE(CheckAlgIsX25519(&pkInfo->algorithm.algorithm));

  ScopedCERTSubjectPublicKeyInfo spki(
      SECKEY_CreateSubjectPublicKeyInfo(pub.get()));
  ASSERT_TRUE(CheckAlgIsX25519(&spki->algorithm.algorithm));
  /* empty parameters for X25519*/
  ASSERT_EQ(spki->algorithm.parameters.len, (unsigned int)0);
}

/* Public Key ASN.1 encoding errors */
TEST_F(Pkcs11X25519Test, ImportExportSpkiWrongLen) {
  const uint8_t pk[] = {0x30, 0x2b, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x6e,
                        0x03, 0x21, 0x00, 0x1c, 0xf2, 0xb1, 0xe6, 0x02, 0x2e,
                        0xc5, 0x37, 0x37, 0x1e, 0xd7, 0xf5, 0x3e, 0x54, 0xfa,
                        0x11, 0x54, 0xd8, 0x3e, 0x98, 0xeb, 0x64, 0xea, 0x51,
                        0xfa, 0xe5, 0xb3, 0x30, 0x7c, 0xfe, 0x97, 0x06};

  DataBuffer publicKey(DataBuffer(pk, sizeof(pk)));

  ScopedSECKEYPublicKey key = ImportPublicKey(publicKey);
  ASSERT_FALSE(key);
}

/* Key encoding errors */
TEST_F(Pkcs11X25519Test, ImportExportSpkiWrongOID) {
  /*0x2b, 0x65, 0x6d instead of 0x2b, 0x65, 0x6e */
  const uint8_t pk[] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x6d,
                        0x03, 0x21, 0x00, 0x1c, 0xf2, 0xb1, 0xe6, 0x02, 0x2e,
                        0xc5, 0x37, 0x37, 0x1e, 0xd7, 0xf5, 0x3e, 0x54, 0xfa,
                        0x11, 0x54, 0xd8, 0x3e, 0x98, 0xeb, 0x64, 0xea, 0x51,
                        0xfa, 0xe5, 0xb3, 0x30, 0x7c, 0xfe, 0x97, 0x06};

  DataBuffer publicKey(DataBuffer(pk, sizeof(pk)));
  ScopedSECKEYPublicKey key = ImportPublicKey(publicKey);
  ASSERT_FALSE(key);
}

/* Key encoding errors */
TEST_F(Pkcs11X25519Test, ImportExportSpkiWrongKeyID) {
  /*0x2b, 0x65, 0x6d instead of 0x2b, 0x65, 0x6e */
  const uint8_t pk[] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x6d,
                        0x04,  // 0x04 instead of 0x03
                        0x21, 0x00, 0x1c, 0xf2, 0xb1, 0xe6, 0x02, 0x2e, 0xc5,
                        0x37, 0x37, 0x1e, 0xd7, 0xf5, 0x3e, 0x54, 0xfa, 0x11,
                        0x54, 0xd8, 0x3e, 0x98, 0xeb, 0x64, 0xea, 0x51, 0xfa,
                        0xe5, 0xb3, 0x30, 0x7c, 0xfe, 0x97, 0x06};

  DataBuffer publicKey(DataBuffer(pk, sizeof(pk)));
  ScopedSECKEYPublicKey key = ImportPublicKey(publicKey);
  ASSERT_FALSE(key);
}

/* We allow to import all-zero keys. */
TEST_F(Pkcs11X25519Test, ImportExportSpkiZeroKey) {
  const uint8_t pk[] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x6e,
                        0x03, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  DataBuffer publicKey(DataBuffer(pk, sizeof(pk)));
  ScopedSECKEYPublicKey key = ImportPublicKey(publicKey);
  ASSERT_TRUE(key);
}

}  // namespace nss_test
