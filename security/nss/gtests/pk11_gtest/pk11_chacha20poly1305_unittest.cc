/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include "nss.h"
#include "pk11pub.h"
#include "pk11priv.h"
#include "sechash.h"
#include "secerr.h"

#include "cpputil.h"
#include "json_reader.h"
#include "nss_scoped_ptrs.h"

#include "testvectors/chachapoly-long-vectors.h"
#include "testvectors_base/test-structs.h"
#include "testvectors_base/chachapoly-vectors_base.h"
#include "gtest/gtest.h"
#include "pk11_import.h"

namespace nss_test {

static const CK_MECHANISM_TYPE kMech = CKM_CHACHA20_POLY1305;
static const CK_MECHANISM_TYPE kMechLegacy = CKM_NSS_CHACHA20_POLY1305;
static const CK_MECHANISM_TYPE kMechXor = CKM_CHACHA20;
static const CK_MECHANISM_TYPE kMechXorLegacy = CKM_NSS_CHACHA20_CTR;
// Some test data for simple tests.
static const uint8_t kKeyData[32] = {'k'};
static const uint8_t kXorParamsLegacy[16] = {'c', 0, 0, 0, 'n'};
static const uint8_t kCounter[4] = {'c', 0};
static const uint8_t kNonce[12] = {'n', 0};
static const CK_CHACHA20_PARAMS kXorParams{
    /* pBlockCounter */ const_cast<CK_BYTE_PTR>(kCounter),
    /* blockCounterBits */ sizeof(kCounter) * 8,
    /* pNonce */ const_cast<CK_BYTE_PTR>(kNonce),
    /* ulNonceBits */ sizeof(kNonce) * 8,
};
static const uint8_t kData[16] = {'d'};
static const uint8_t kExpectedXor[sizeof(kData)] = {
    0xd8, 0x15, 0xd3, 0xb3, 0xe9, 0x34, 0x3b, 0x7a,
    0x24, 0xf6, 0x5f, 0xd7, 0x95, 0x3d, 0xd3, 0x51};
static const size_t kTagLen = 16;
// Above the sizes from which freebl switches to vectorized implementations.
static const size_t kLongDataLen = 1000;

struct ChaChaWycheproofTestVector {
  uint64_t id;
  std::vector<uint8_t> key;
  std::vector<uint8_t> iv;
  std::vector<uint8_t> aad;
  std::vector<uint8_t> msg;
  std::vector<uint8_t> ct;
  std::vector<uint8_t> tag;
  bool valid;
};

class Pkcs11ChaCha20Poly1305Test
    : public ::testing::TestWithParam<ChaChaTestVector> {
 public:
  void EncryptDecrypt(const ScopedPK11SymKey& key, const bool invalid_iv,
                      const bool invalid_tag, const uint8_t* data,
                      size_t data_len, CK_MECHANISM_TYPE mech, SECItem* params,
                      std::vector<uint8_t>* nonce, std::vector<uint8_t>* aad,
                      const uint8_t* ct = nullptr, size_t ct_len = 0) {
    std::vector<uint8_t> encrypted(data_len + kTagLen);
    unsigned int encrypted_len = 0;
    // Encrypt.
    SECStatus rv =
        PK11_Encrypt(key.get(), mech, params, encrypted.data(), &encrypted_len,
                     encrypted.size(), data, data_len);

    // Return if encryption failure was expected due to invalid IV.
    // Without valid ciphertext, all further tests can be skipped.
    if (invalid_iv) {
      EXPECT_EQ(rv, SECFailure);
      EXPECT_EQ(0U, encrypted_len)
          << "encrypted_len is unmodified after failure";
      return;
    }

    EXPECT_EQ(rv, SECSuccess);
    EXPECT_EQ(encrypted.size(), static_cast<size_t>(encrypted_len));

    // Check ciphertext and tag.
    if (ct) {
      ASSERT_EQ(ct_len, encrypted_len);
      EXPECT_TRUE(!memcmp(ct, encrypted.data(), encrypted.size() - 16));
      EXPECT_TRUE(!memcmp(ct, encrypted.data(), encrypted.size()) !=
                  invalid_tag);
    }

    // Get the *estimated* plaintext length. This value should
    // never be zero as it could lead to a NULL outPtr being
    // passed to a subsequent decryption call (for AEAD we
    // must authenticate even when the pt is zero-length).
    unsigned int decrypt_bytes_needed = 0;
    rv = PK11_Decrypt(key.get(), mech, params, nullptr, &decrypt_bytes_needed,
                      0, encrypted.data(), encrypted_len);
    EXPECT_EQ(rv, SECSuccess);
    EXPECT_GT(decrypt_bytes_needed, data_len);

    // Now decrypt it
    std::vector<uint8_t> decrypted(decrypt_bytes_needed);
    unsigned int decrypted_len = 0;
    rv = PK11_Decrypt(key.get(), mech, params, decrypted.data(), &decrypted_len,
                      decrypted.size(), encrypted.data(), encrypted.size());
    EXPECT_EQ(rv, SECSuccess);

    // Check the plaintext.
    ASSERT_EQ(data_len, decrypted_len);
    EXPECT_TRUE(!memcmp(data, decrypted.data(), decrypted_len));

    // Decrypt with bogus data.
    // Skip if there's no data to modify.
    if (encrypted_len > 0) {
      decrypted_len = 0;
      std::vector<uint8_t> bogus_ciphertext(encrypted);
      bogus_ciphertext[0] ^= 0xff;
      rv = PK11_Decrypt(key.get(), mech, params, decrypted.data(),
                        &decrypted_len, decrypted.size(),
                        bogus_ciphertext.data(), encrypted_len);
      EXPECT_EQ(rv, SECFailure);
      EXPECT_EQ(0U, decrypted_len);
    }

    // Decrypt with bogus tag.
    // Skip if there's no tag to modify.
    if (encrypted_len > 0) {
      decrypted_len = 0;
      std::vector<uint8_t> bogus_tag(encrypted);
      bogus_tag[encrypted_len - 1] ^= 0xff;
      rv = PK11_Decrypt(key.get(), mech, params, decrypted.data(),
                        &decrypted_len, decrypted.size(), bogus_tag.data(),
                        encrypted_len);
      EXPECT_EQ(rv, SECFailure);
      EXPECT_EQ(0U, decrypted_len);
    }

    // Decrypt with bogus nonce.
    // A nonce length of 0 is invalid and should be caught earlier.
    ASSERT_NE(0U, nonce->size());
    decrypted_len = 0;
    nonce->data()[0] ^= 0xff;
    rv = PK11_Decrypt(key.get(), mech, params, decrypted.data(), &decrypted_len,
                      data_len, encrypted.data(), encrypted.size());
    EXPECT_EQ(rv, SECFailure);
    EXPECT_EQ(0U, decrypted_len);
    nonce->data()[0] ^= 0xff;  // restore value

    // Decrypt with bogus additional data.
    // Skip when AAD was empty and can't be modified.
    // Alternatively we could generate random aad.
    if (aad->size() != 0) {
      decrypted_len = 0;
      aad->data()[0] ^= 0xff;

      rv = PK11_Decrypt(key.get(), mech, params, decrypted.data(),
                        &decrypted_len, data_len, encrypted.data(),
                        encrypted.size());
      EXPECT_EQ(rv, SECFailure);
      EXPECT_EQ(0U, decrypted_len);
    }
  }

  void EncryptDecrypt(const ScopedPK11SymKey& key, const bool invalid_iv,
                      const bool invalid_tag, const uint8_t* data,
                      size_t data_len, const uint8_t* aad_ptr, size_t aad_len,
                      const uint8_t* iv_ptr, size_t iv_len,
                      const uint8_t* ct = nullptr, size_t ct_len = 0) {
    std::vector<uint8_t> nonce(iv_ptr, iv_ptr + iv_len);
    std::vector<uint8_t> aad(aad_ptr, aad_ptr + aad_len);
    // Prepare AEAD params.
    CK_SALSA20_CHACHA20_POLY1305_PARAMS aead_params;
    aead_params.pNonce = toUcharPtr(nonce.data());
    aead_params.ulNonceLen = nonce.size();
    aead_params.pAAD = toUcharPtr(aad.data());
    aead_params.ulAADLen = aad.size();

    SECItem params = {siBuffer, reinterpret_cast<unsigned char*>(&aead_params),
                      sizeof(aead_params)};

    EncryptDecrypt(key, invalid_iv, invalid_tag, data, data_len, kMech, &params,
                   &nonce, &aad, ct, ct_len);
  }

  void EncryptDecryptLegacy(const ScopedPK11SymKey& key, const bool invalid_iv,
                            const bool invalid_tag, const uint8_t* data,
                            size_t data_len, const uint8_t* aad_ptr,
                            size_t aad_len, const uint8_t* iv_ptr,
                            size_t iv_len, const uint8_t* ct = nullptr,
                            size_t ct_len = 0) {
    std::vector<uint8_t> nonce(iv_ptr, iv_ptr + iv_len);
    std::vector<uint8_t> aad(aad_ptr, aad_ptr + aad_len);
    // Prepare AEAD params.
    CK_NSS_AEAD_PARAMS aead_params;
    aead_params.pNonce = toUcharPtr(nonce.data());
    aead_params.ulNonceLen = nonce.size();
    aead_params.pAAD = toUcharPtr(aad.data());
    aead_params.ulAADLen = aad.size();
    aead_params.ulTagLen = kTagLen;

    SECItem params = {siBuffer, reinterpret_cast<unsigned char*>(&aead_params),
                      sizeof(aead_params)};

    // Encrypt with bad parameters (TagLen is too long).
    unsigned int encrypted_len = 0;
    std::vector<uint8_t> encrypted(data_len + aead_params.ulTagLen);
    aead_params.ulTagLen = 158072;
    SECStatus rv =
        PK11_Encrypt(key.get(), kMechLegacy, &params, encrypted.data(),
                     &encrypted_len, encrypted.size(), data, data_len);
    EXPECT_EQ(SECFailure, rv);
    EXPECT_EQ(0U, encrypted_len);

    // Encrypt with bad parameters (TagLen is too short).
    aead_params.ulTagLen = 2;
    rv = PK11_Encrypt(key.get(), kMechLegacy, &params, encrypted.data(),
                      &encrypted_len, encrypted.size(), data, data_len);
    EXPECT_EQ(SECFailure, rv);
    EXPECT_EQ(0U, encrypted_len);

    // Encrypt.
    aead_params.ulTagLen = kTagLen;
    EncryptDecrypt(key, invalid_iv, invalid_tag, data, data_len, kMechLegacy,
                   &params, &nonce, &aad, ct, ct_len);
  }

  void EncryptDecrypt(const ChaChaTestVector testvector) {
    ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
    SECItem keyItem = {siBuffer, toUcharPtr(testvector.key.data()),
                       static_cast<unsigned int>(testvector.key.size())};

    // Import key.
    ScopedPK11SymKey key(nss_test::ImportSymKey(
        slot.get(), kMech, PK11_OriginUnwrap, CKA_ENCRYPT, &keyItem, nullptr));
    EXPECT_TRUE(!!key);

    // Check.
    EncryptDecrypt(key, testvector.invalid_iv, testvector.invalid_tag,
                   testvector.plaintext.data(), testvector.plaintext.size(),
                   testvector.aad.data(), testvector.aad.size(),
                   testvector.iv.data(), testvector.iv.size(),
                   testvector.ciphertext.data(), testvector.ciphertext.size());
    XorMatchesAead(testvector);
  }

  // ChaCha20-Poly1305 encrypts with the ChaCha20 keystream starting at block
  // counter 1, so CKM_CHACHA20 has to reproduce the AEAD ciphertext.
  void XorMatchesAead(const ChaChaTestVector& testvector) {
    if (testvector.invalid_iv || testvector.plaintext.empty()) {
      return;
    }

    ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
    SECItem key_item = {siBuffer, toUcharPtr(testvector.key.data()),
                        static_cast<unsigned int>(testvector.key.size())};
    ScopedPK11SymKey key(nss_test::ImportSymKey(slot.get(), kMechXor,
                                                PK11_OriginUnwrap, CKA_ENCRYPT,
                                                &key_item, nullptr));
    ASSERT_TRUE(!!key);

    uint8_t counter[4] = {1, 0, 0, 0};
    std::vector<uint8_t> nonce(testvector.iv);
    CK_CHACHA20_PARAMS chacha_params;
    chacha_params.pBlockCounter = counter;
    chacha_params.blockCounterBits = sizeof(counter) * 8;
    chacha_params.pNonce = nonce.data();
    chacha_params.ulNonceBits = static_cast<CK_ULONG>(nonce.size() * 8);
    SECItem params = {siBuffer,
                      reinterpret_cast<unsigned char*>(&chacha_params),
                      sizeof(chacha_params)};

    unsigned int len = static_cast<unsigned int>(testvector.plaintext.size());
    std::vector<uint8_t> encrypted(len);
    unsigned int encrypted_len = 0;
    ASSERT_EQ(SECSuccess, PK11_Encrypt(key.get(), kMechXor, &params,
                                       encrypted.data(), &encrypted_len, len,
                                       testvector.plaintext.data(), len));
    ASSERT_EQ(len, encrypted_len);
    ASSERT_GE(testvector.ciphertext.size(), len);
    std::vector<uint8_t> expected(testvector.ciphertext.data(),
                                  testvector.ciphertext.data() + len);
    EXPECT_EQ(expected, encrypted);
  }

  void MessageInterfaceTest(CK_MECHANISM_TYPE mech, int iterations,
                            PRBool separateTag, size_t data_len) {
    // Generate a random key.
    ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
    ASSERT_NE(nullptr, slot);
    ScopedPK11SymKey sym_key(
        PK11_KeyGen(slot.get(), mech, nullptr, 32, nullptr));
    ASSERT_NE(nullptr, sym_key);

    int tagSize = kTagLen;
    int cipher_simulated_size;
    int output_len_message = 0;
    int output_len_simulated = 0;
    unsigned int output_len_v24 = 0;

    std::vector<uint8_t> plainIn(data_len);
    std::vector<uint8_t> plainOut_message(data_len);
    std::vector<uint8_t> plainOut_simulated(data_len);
    std::vector<uint8_t> plainOut_v24(data_len);
    std::vector<uint8_t> nonce(12);
    std::vector<uint8_t> cipher_message(data_len + kTagLen);
    std::vector<uint8_t> cipher_simulated(data_len + kTagLen);
    std::vector<uint8_t> cipher_v24(data_len + kTagLen);
    std::vector<uint8_t> aad(16);
    std::vector<uint8_t> tag_message(kTagLen);
    std::vector<uint8_t> tag_simulated(kTagLen);

    // Prepare AEAD v2.40 params.
    CK_SALSA20_CHACHA20_POLY1305_PARAMS chacha_params;
    chacha_params.pNonce = nonce.data();
    chacha_params.ulNonceLen = nonce.size();
    chacha_params.pAAD = aad.data();
    chacha_params.ulAADLen = aad.size();

    // Prepare AEAD MESSAGE params.
    CK_SALSA20_CHACHA20_POLY1305_MSG_PARAMS chacha_message_params;
    chacha_message_params.pNonce = nonce.data();
    chacha_message_params.ulNonceLen = nonce.size();
    if (separateTag) {
      chacha_message_params.pTag = tag_message.data();
    } else {
      chacha_message_params.pTag = cipher_message.data() + plainIn.size();
    }

    // Prepare AEAD MESSAGE params for simulated case
    CK_SALSA20_CHACHA20_POLY1305_MSG_PARAMS chacha_simulated_params;
    chacha_simulated_params = chacha_message_params;
    if (separateTag) {
      // The simulated case, we have to allocate temp bufs for separate
      // tags, make sure that works in both the encrypt and the decrypt
      // cases.
      chacha_simulated_params.pTag = tag_simulated.data();
      cipher_simulated_size = cipher_simulated.size() - tagSize;
    } else {
      chacha_simulated_params.pTag = cipher_simulated.data() + plainIn.size();
      cipher_simulated_size = cipher_simulated.size();
    }
    SECItem params = {siBuffer,
                      reinterpret_cast<unsigned char*>(&chacha_params),
                      sizeof(chacha_params)};
    SECItem empty = {siBuffer, NULL, 0};

    // initialize our plain text, IV and aad.
    ASSERT_EQ(PK11_GenerateRandom(plainIn.data(), plainIn.size()), SECSuccess);
    ASSERT_EQ(PK11_GenerateRandom(aad.data(), aad.size()), SECSuccess);

    // Initialize message encrypt context
    ScopedPK11Context encrypt_message_context(PK11_CreateContextBySymKey(
        mech, CKA_NSS_MESSAGE | CKA_ENCRYPT, sym_key.get(), &empty));
    ASSERT_NE(nullptr, encrypt_message_context);
    ASSERT_FALSE(_PK11_ContextGetAEADSimulation(encrypt_message_context.get()));

    // Initialize simulated encrypt context
    ScopedPK11Context encrypt_simulated_context(PK11_CreateContextBySymKey(
        mech, CKA_NSS_MESSAGE | CKA_ENCRYPT, sym_key.get(), &empty));
    ASSERT_NE(nullptr, encrypt_simulated_context);
    ASSERT_EQ(SECSuccess,
              _PK11_ContextSetAEADSimulation(encrypt_simulated_context.get()));

    // Initialize message decrypt context
    ScopedPK11Context decrypt_message_context(PK11_CreateContextBySymKey(
        mech, CKA_NSS_MESSAGE | CKA_DECRYPT, sym_key.get(), &empty));
    ASSERT_NE(nullptr, decrypt_message_context);
    ASSERT_FALSE(_PK11_ContextGetAEADSimulation(decrypt_message_context.get()));

    // Initialize simulated decrypt context
    ScopedPK11Context decrypt_simulated_context(PK11_CreateContextBySymKey(
        mech, CKA_NSS_MESSAGE | CKA_DECRYPT, sym_key.get(), &empty));
    ASSERT_NE(nullptr, decrypt_simulated_context);
    EXPECT_EQ(SECSuccess,
              _PK11_ContextSetAEADSimulation(decrypt_simulated_context.get()));

    // Now walk down our iterations. Each method of calculating the operation
    // should agree at each step.
    for (int i = 0; i < iterations; i++) {
      // get a unique nonce for each iteration
      EXPECT_EQ(PK11_GenerateRandom(nonce.data(), nonce.size()), SECSuccess);
      EXPECT_EQ(SECSuccess,
                PK11_AEADRawOp(
                    encrypt_message_context.get(), &chacha_message_params,
                    sizeof(chacha_message_params), aad.data(), aad.size(),
                    cipher_message.data(), &output_len_message,
                    cipher_message.size(), plainIn.data(), plainIn.size()));
      EXPECT_EQ(SECSuccess,
                PK11_AEADRawOp(
                    encrypt_simulated_context.get(), &chacha_simulated_params,
                    sizeof(chacha_simulated_params), aad.data(), aad.size(),
                    cipher_simulated.data(), &output_len_simulated,
                    cipher_simulated_size, plainIn.data(), plainIn.size()));
      // make sure simulated and message is the same
      EXPECT_EQ(output_len_message, output_len_simulated);
      EXPECT_EQ(0, memcmp(cipher_message.data(), cipher_simulated.data(),
                          output_len_message));
      EXPECT_EQ(0, memcmp(chacha_message_params.pTag,
                          chacha_simulated_params.pTag, tagSize));
      // make sure v2.40 is the same.
      EXPECT_EQ(SECSuccess,
                PK11_Encrypt(sym_key.get(), mech, &params, cipher_v24.data(),
                             &output_len_v24, cipher_v24.size(), plainIn.data(),
                             plainIn.size()));
      EXPECT_EQ(output_len_message, (int)output_len_v24 - tagSize);
      EXPECT_EQ(0, memcmp(cipher_message.data(), cipher_v24.data(),
                          output_len_message));
      EXPECT_EQ(0, memcmp(chacha_message_params.pTag,
                          cipher_v24.data() + output_len_message, tagSize));
      // now make sure we can decrypt
      EXPECT_EQ(
          SECSuccess,
          PK11_AEADRawOp(decrypt_message_context.get(), &chacha_message_params,
                         sizeof(chacha_message_params), aad.data(), aad.size(),
                         plainOut_message.data(), &output_len_message,
                         plainOut_message.size(), cipher_message.data(),
                         output_len_message));
      EXPECT_EQ(output_len_message, (int)plainIn.size());
      EXPECT_EQ(
          0, memcmp(plainOut_message.data(), plainIn.data(), plainIn.size()));
      EXPECT_EQ(SECSuccess,
                PK11_AEADRawOp(decrypt_simulated_context.get(),
                               &chacha_simulated_params,
                               sizeof(chacha_simulated_params), aad.data(),
                               aad.size(), plainOut_simulated.data(),
                               &output_len_simulated, plainOut_simulated.size(),
                               cipher_message.data(), output_len_simulated));
      EXPECT_EQ(output_len_simulated, (int)plainIn.size());
      EXPECT_EQ(
          0, memcmp(plainOut_simulated.data(), plainIn.data(), plainIn.size()));
      if (separateTag) {
        // in the separateTag case, we need to copy the tag back to the
        // end of the cipher_message.data() before using the v2.4 interface
        memcpy(cipher_message.data() + output_len_message,
               chacha_message_params.pTag, tagSize);
      }
      EXPECT_EQ(SECSuccess,
                PK11_Decrypt(sym_key.get(), mech, &params, plainOut_v24.data(),
                             &output_len_v24, plainOut_v24.size(),
                             cipher_message.data(), output_len_v24));
      EXPECT_EQ(output_len_v24, plainIn.size());
      EXPECT_EQ(0, memcmp(plainOut_v24.data(), plainIn.data(), plainIn.size()));
    }
    return;
  }

  void RunWycheproof() {
    WycheproofHeader("chacha20_poly1305", "CHACHA20-POLY1305",
                     "aead_test_schema_v1.json",
                     [this](JsonReader& r) { RunWycheproofGroup(r); });
  }

 private:
  void RunWycheproofGroup(JsonReader& r) {
    std::vector<ChaChaWycheproofTestVector> tests;
    uint64_t iv_size = 0;

    while (r.NextItem()) {
      std::string n = r.ReadLabel();
      if (n == "") {
        break;
      }

      if (n == "ivSize") {
        iv_size = r.ReadInt();
      } else if (n == "keySize") {
        ASSERT_EQ(256U, r.ReadInt());
      } else if (n == "tagSize") {
        ASSERT_EQ(kTagLen * 8, r.ReadInt());
      } else if (n == "source") {
        r.SkipValue();
      } else if (n == "type") {
        ASSERT_EQ("AeadTest", r.ReadString());
      } else if (n == "tests") {
        WycheproofReadTests(r, &tests, ReadWycheproofTestAttr);
      } else {
        FAIL() << "unknown label in group: " << n;
      }
    }

    for (auto& t : tests) {
      std::cout << "Running test " << t.id << std::endl;
      std::vector<uint8_t> ct_and_tag(t.ct);
      ct_and_tag.insert(ct_and_tag.end(), t.tag.begin(), t.tag.end());
      // Only 96-bit nonces are supported. Every test case with a different
      // nonce size is invalid.
      ChaChaTestVector vec = {static_cast<uint32_t>(t.id),
                              t.msg,
                              t.aad,
                              t.key,
                              t.iv,
                              ct_and_tag,
                              !t.valid,
                              iv_size != 96};
      EncryptDecrypt(vec);
    }
  }

  static void ReadWycheproofTestAttr(ChaChaWycheproofTestVector& t,
                                     const std::string& n, JsonReader& r) {
    if (n == "key") {
      t.key = r.ReadHex();
    } else if (n == "iv") {
      t.iv = r.ReadHex();
    } else if (n == "aad") {
      t.aad = r.ReadHex();
    } else if (n == "msg") {
      t.msg = r.ReadHex();
    } else if (n == "ct") {
      t.ct = r.ReadHex();
    } else if (n == "tag") {
      t.tag = r.ReadHex();
    } else {
      FAIL() << "unknown test key: " << n;
    }
  }
};

TEST_F(Pkcs11ChaCha20Poly1305Test, GenerateEncryptDecrypt) {
  // Generate a random key.
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ScopedPK11SymKey key(PK11_KeyGen(slot.get(), kMech, nullptr, 32, nullptr));
  EXPECT_TRUE(!!key);

  // Generate random data.
  std::vector<uint8_t> input(512);
  SECStatus rv =
      PK11_GenerateRandomOnSlot(slot.get(), input.data(), input.size());
  EXPECT_EQ(rv, SECSuccess);

  // Generate random AAD.
  std::vector<uint8_t> aad(16);
  rv = PK11_GenerateRandomOnSlot(slot.get(), aad.data(), aad.size());
  EXPECT_EQ(rv, SECSuccess);

  // Generate random IV.
  std::vector<uint8_t> iv(12);
  rv = PK11_GenerateRandomOnSlot(slot.get(), iv.data(), iv.size());
  EXPECT_EQ(rv, SECSuccess);

  // Check.
  EncryptDecrypt(key, false, false, input.data(), input.size(), aad.data(),
                 aad.size(), iv.data(), iv.size());
}

TEST_F(Pkcs11ChaCha20Poly1305Test, Xor) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  SECItem keyItem = {siBuffer, toUcharPtr(kKeyData),
                     static_cast<unsigned int>(sizeof(kKeyData))};
  ScopedPK11SymKey key(nss_test::ImportSymKey(
      slot.get(), kMechXor, PK11_OriginUnwrap, CKA_ENCRYPT, &keyItem, nullptr));
  EXPECT_TRUE(!!key);

  SECItem params = {siBuffer,
                    toUcharPtr(reinterpret_cast<const uint8_t*>(&kXorParams)),
                    static_cast<unsigned int>(sizeof(kXorParams))};
  uint8_t encrypted[sizeof(kData)];
  unsigned int encrypted_len = 88;  // This should be overwritten.
  SECStatus rv =
      PK11_Encrypt(key.get(), kMechXor, &params, encrypted, &encrypted_len,
                   sizeof(encrypted), kData, sizeof(kData));
  ASSERT_EQ(SECSuccess, rv);
  ASSERT_EQ(sizeof(kExpectedXor), static_cast<size_t>(encrypted_len));
  EXPECT_EQ(0, memcmp(kExpectedXor, encrypted, sizeof(kExpectedXor)));

  // Decrypting has the same effect.
  rv = PK11_Decrypt(key.get(), kMechXor, &params, encrypted, &encrypted_len,
                    sizeof(encrypted), kData, sizeof(kData));
  ASSERT_EQ(SECSuccess, rv);
  ASSERT_EQ(sizeof(kData), static_cast<size_t>(encrypted_len));
  EXPECT_EQ(0, memcmp(kExpectedXor, encrypted, sizeof(kExpectedXor)));

  // Operating in reverse too.
  rv = PK11_Encrypt(key.get(), kMechXor, &params, encrypted, &encrypted_len,
                    sizeof(encrypted), kExpectedXor, sizeof(kExpectedXor));
  ASSERT_EQ(SECSuccess, rv);
  ASSERT_EQ(sizeof(kExpectedXor), static_cast<size_t>(encrypted_len));
  EXPECT_EQ(0, memcmp(kData, encrypted, sizeof(kData)));
}

TEST_F(Pkcs11ChaCha20Poly1305Test, XorLegacy) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  SECItem keyItem = {siBuffer, toUcharPtr(kKeyData),
                     static_cast<unsigned int>(sizeof(kKeyData))};
  ScopedPK11SymKey key(nss_test::ImportSymKey(slot.get(), kMechXorLegacy,
                                              PK11_OriginUnwrap, CKA_ENCRYPT,
                                              &keyItem, nullptr));
  EXPECT_TRUE(!!key);

  SECItem ctrNonceItem = {siBuffer, toUcharPtr(kXorParamsLegacy),
                          static_cast<unsigned int>(sizeof(kXorParamsLegacy))};
  uint8_t encrypted[sizeof(kData)];
  unsigned int encrypted_len = 88;  // This should be overwritten.
  SECStatus rv =
      PK11_Encrypt(key.get(), kMechXorLegacy, &ctrNonceItem, encrypted,
                   &encrypted_len, sizeof(encrypted), kData, sizeof(kData));
  ASSERT_EQ(SECSuccess, rv);
  ASSERT_EQ(sizeof(kExpectedXor), static_cast<size_t>(encrypted_len));
  EXPECT_EQ(0, memcmp(kExpectedXor, encrypted, sizeof(kExpectedXor)));

  // Decrypting has the same effect.
  rv = PK11_Decrypt(key.get(), kMechXorLegacy, &ctrNonceItem, encrypted,
                    &encrypted_len, sizeof(encrypted), kData, sizeof(kData));
  ASSERT_EQ(SECSuccess, rv);
  ASSERT_EQ(sizeof(kData), static_cast<size_t>(encrypted_len));
  EXPECT_EQ(0, memcmp(kExpectedXor, encrypted, sizeof(kExpectedXor)));

  // Operating in reverse too.
  rv = PK11_Encrypt(key.get(), kMechXorLegacy, &ctrNonceItem, encrypted,
                    &encrypted_len, sizeof(encrypted), kExpectedXor,
                    sizeof(kExpectedXor));
  ASSERT_EQ(SECSuccess, rv);
  ASSERT_EQ(sizeof(kExpectedXor), static_cast<size_t>(encrypted_len));
  EXPECT_EQ(0, memcmp(kData, encrypted, sizeof(kData)));
}

// This test just ensures that a key can be generated for use with the XOR
// function.  The result is random and therefore cannot be checked.
TEST_F(Pkcs11ChaCha20Poly1305Test, GenerateXor) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ScopedPK11SymKey key(PK11_KeyGen(slot.get(), kMechXor, nullptr, 32, nullptr));
  EXPECT_TRUE(!!key);

  std::vector<uint8_t> iv(16);
  SECStatus rv = PK11_GenerateRandomOnSlot(slot.get(), iv.data(), iv.size());
  EXPECT_EQ(SECSuccess, rv);

  CK_CHACHA20_PARAMS chacha_params;
  chacha_params.pBlockCounter = iv.data();
  chacha_params.blockCounterBits = 32;
  chacha_params.pNonce = iv.data() + 4;
  chacha_params.ulNonceBits = 96;

  SECItem params = {
      siBuffer, toUcharPtr(reinterpret_cast<const uint8_t*>(&chacha_params)),
      static_cast<unsigned int>(sizeof(chacha_params))};
  uint8_t encrypted[sizeof(kData)];
  unsigned int encrypted_len = 88;  // This should be overwritten.
  rv = PK11_Encrypt(key.get(), kMechXor, &params, encrypted, &encrypted_len,
                    sizeof(encrypted), kData, sizeof(kData));
  ASSERT_EQ(SECSuccess, rv);
  ASSERT_EQ(sizeof(kData), static_cast<size_t>(encrypted_len));
}

TEST_F(Pkcs11ChaCha20Poly1305Test, GenerateXorLegacy) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ScopedPK11SymKey key(
      PK11_KeyGen(slot.get(), kMechXorLegacy, nullptr, 32, nullptr));
  EXPECT_TRUE(!!key);

  std::vector<uint8_t> iv(16);
  SECStatus rv = PK11_GenerateRandomOnSlot(slot.get(), iv.data(), iv.size());
  EXPECT_EQ(SECSuccess, rv);

  SECItem params = {siBuffer, toUcharPtr(iv.data()),
                    static_cast<unsigned int>(iv.size())};
  uint8_t encrypted[sizeof(kData)];
  unsigned int encrypted_len = 88;  // This should be overwritten.
  rv = PK11_Encrypt(key.get(), kMechXorLegacy, &params, encrypted,
                    &encrypted_len, sizeof(encrypted), kData, sizeof(kData));
  ASSERT_EQ(SECSuccess, rv);
  ASSERT_EQ(sizeof(kData), static_cast<size_t>(encrypted_len));
}

TEST_F(Pkcs11ChaCha20Poly1305Test, XorInvalidParams) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ScopedPK11SymKey key(PK11_KeyGen(slot.get(), kMech, nullptr, 32, nullptr));
  EXPECT_TRUE(!!key);

  SECItem params = {siBuffer,
                    toUcharPtr(reinterpret_cast<const uint8_t*>(&kXorParams)),
                    static_cast<unsigned int>(sizeof(kXorParams)) - 1};
  uint8_t encrypted[sizeof(kData)];
  unsigned int encrypted_len = 88;
  SECStatus rv =
      PK11_Encrypt(key.get(), kMechXor, &params, encrypted, &encrypted_len,
                   sizeof(encrypted), kData, sizeof(kData));
  EXPECT_EQ(SECFailure, rv);

  params.data = nullptr;
  rv = PK11_Encrypt(key.get(), kMechXor, &params, encrypted, &encrypted_len,
                    sizeof(encrypted), kData, sizeof(kData));
  EXPECT_EQ(SECFailure, rv);
  EXPECT_EQ(SEC_ERROR_BAD_DATA, PORT_GetError());
}

TEST_F(Pkcs11ChaCha20Poly1305Test, XorLegacyInvalidParams) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ScopedPK11SymKey key(PK11_KeyGen(slot.get(), kMech, nullptr, 32, nullptr));
  EXPECT_TRUE(!!key);

  SECItem params = {siBuffer, toUcharPtr(kXorParamsLegacy),
                    static_cast<unsigned int>(sizeof(kXorParamsLegacy)) - 1};
  uint8_t encrypted[sizeof(kData)];
  unsigned int encrypted_len = 88;
  SECStatus rv =
      PK11_Encrypt(key.get(), kMechXor, &params, encrypted, &encrypted_len,
                   sizeof(encrypted), kData, sizeof(kData));
  EXPECT_EQ(SECFailure, rv);

  params.data = nullptr;
  rv = PK11_Encrypt(key.get(), kMechXor, &params, encrypted, &encrypted_len,
                    sizeof(encrypted), kData, sizeof(kData));
  EXPECT_EQ(SECFailure, rv);
  EXPECT_EQ(SEC_ERROR_BAD_DATA, PORT_GetError());
}

TEST_P(Pkcs11ChaCha20Poly1305Test, TestVectors) { EncryptDecrypt(GetParam()); }

INSTANTIATE_TEST_SUITE_P(NSSTestVector, Pkcs11ChaCha20Poly1305Test,
                         ::testing::ValuesIn(kChaCha20Vectors));

INSTANTIATE_TEST_SUITE_P(LongTestVector, Pkcs11ChaCha20Poly1305Test,
                         ::testing::ValuesIn(kChaCha20LongVectors));

TEST_F(Pkcs11ChaCha20Poly1305Test, WycheproofVectors) { RunWycheproof(); }

// basic message interface it's the most common configuration
TEST_F(Pkcs11ChaCha20Poly1305Test, ChaCha201305MessageInterfaceBasic) {
  MessageInterfaceTest(CKM_CHACHA20_POLY1305, 16, PR_FALSE, 17);
}

TEST_F(Pkcs11ChaCha20Poly1305Test, ChaCha20Poly1305MessageInterfaceLong) {
  MessageInterfaceTest(CKM_CHACHA20_POLY1305, 16, PR_FALSE, kLongDataLen);
}

// basic interface, but return the tags in a separate buffer. This triggers
// different behaviour in the simulated case, which has to buffer the
// intermediate values in a separate buffer.
TEST_F(Pkcs11ChaCha20Poly1305Test,
       ChaCha20Poly1305MessageInterfaceSeparateTags) {
  MessageInterfaceTest(CKM_CHACHA20_POLY1305, 16, PR_TRUE, 17);
}

TEST_F(Pkcs11ChaCha20Poly1305Test,
       ChaCha20Poly1305MessageInterfaceSeparateTagsLong) {
  MessageInterfaceTest(CKM_CHACHA20_POLY1305, 16, PR_TRUE, kLongDataLen);
}

// PK11_AEADOp must reject ChaCha20-Poly1305 tags that are not 16 bytes long,
// rather than reading or writing 16 bytes through the tag pointer.
TEST_F(Pkcs11ChaCha20Poly1305Test, AEADOpInvalidTagLength) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  SECItem key_item = {siBuffer, toUcharPtr(kKeyData),
                      static_cast<unsigned int>(sizeof(kKeyData))};
  ScopedPK11SymKey key(nss_test::ImportSymKey(
      slot.get(), kMech, PK11_OriginUnwrap, CKA_ENCRYPT, &key_item, nullptr));
  ASSERT_TRUE(!!key);
  SECItem empty = {siBuffer, NULL, 0};

  for (bool simulate : {false, true}) {
    for (CK_ATTRIBUTE_TYPE op : {CKA_ENCRYPT, CKA_DECRYPT}) {
      ScopedPK11Context context(PK11_CreateContextBySymKey(
          kMech, CKA_NSS_MESSAGE | op, key.get(), &empty));
      ASSERT_NE(nullptr, context);
      if (simulate) {
        ASSERT_EQ(SECSuccess, _PK11_ContextSetAEADSimulation(context.get()));
      }

      for (size_t tag_len : {0, 1, 15, 17}) {
        std::vector<uint8_t> nonce(kNonce, kNonce + sizeof(kNonce));
        std::vector<uint8_t> in(kData, kData + sizeof(kData));
        std::vector<uint8_t> out(sizeof(kData));
        std::vector<uint8_t> tag(tag_len);
        int out_len = 0;
        EXPECT_EQ(SECFailure,
                  PK11_AEADOp(context.get(), CKG_NO_GENERATE, 0, nonce.data(),
                              nonce.size(), nullptr, 0, out.data(), &out_len,
                              out.size(), tag.data(), tag.size(), in.data(),
                              in.size()))
            << "simulate=" << simulate << " op=" << op
            << " tag_len=" << tag_len;
        EXPECT_EQ(SEC_ERROR_INVALID_ARGS, PORT_GetError());
        EXPECT_EQ(0, out_len);
      }
    }
  }
}

}  // namespace nss_test
