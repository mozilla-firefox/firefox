// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gtest/gtest.h"

#include <stdint.h>
#include <string>
#include <vector>

#include "blapi.h"
#include "json_reader.h"

extern "C" {
#include "blapii.h"
}

#ifdef HACL_CAN_COMPILE_VEC128

#include "Hacl_Chacha20Poly1305_128.h"

namespace nss_test {

static const size_t kKeyLen = 32;
static const size_t kNonceLen = 12;
static const size_t kTagLen = 16;

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

// ChaCha20_Xor and ChaCha20Poly1305_{Seal,Open,Encrypt,Decrypt} only use the
// 128-bit implementations above a size threshold on some platforms, and not at
// all where a wider one is available, so call them directly to give them every
// case.
class HaclChaCha20Poly1305_128Test : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(SECSuccess, BL_Init());
#ifdef NSS_X64
    if (!(ssse3_support() && sse4_1_support() && avx_support())) {
      GTEST_SKIP() << "CPU lacks SSSE3, SSE4.1 or AVX";
    }
#else
    if (!arm_neon_support()) {
      GTEST_SKIP() << "CPU lacks NEON";
    }
#endif
  }

  void RunWycheproof() {
    WycheproofHeader("chacha20_poly1305", "CHACHA20-POLY1305",
                     "aead_test_schema_v1.json",
                     [this](JsonReader& r) { RunWycheproofGroup(r); });
    ASSERT_GT(checked_, 0U) << "no test cases with a 96-bit nonce";
  }

 private:
  size_t checked_ = 0;

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
        ASSERT_EQ(kKeyLen * 8, r.ReadInt());
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

    // Other nonce sizes are rejected before reaching HACL, which can't take
    // them.
    if (iv_size != kNonceLen * 8) {
      return;
    }
    for (auto& t : tests) {
      std::cout << "Running test " << t.id << std::endl;
      Check(t);
      checked_++;
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

  // HACL is never handed a null pointer by NSS, even for empty inputs.
  static uint8_t* Data(std::vector<uint8_t>& v) {
    static uint8_t empty;
    return v.empty() ? &empty : v.data();
  }

  void Check(ChaChaWycheproofTestVector& t) {
    SCOPED_TRACE("tcId " + std::to_string(t.id));
    ASSERT_EQ(kKeyLen, t.key.size());
    ASSERT_EQ(kNonceLen, t.iv.size());
    ASSERT_EQ(kTagLen, t.tag.size());
    ASSERT_EQ(t.msg.size(), t.ct.size());
    uint32_t aad_len = static_cast<uint32_t>(t.aad.size());
    uint32_t len = static_cast<uint32_t>(t.msg.size());

    std::vector<uint8_t> ct(len);
    std::vector<uint8_t> tag(kTagLen);
    Hacl_Chacha20Poly1305_128_aead_encrypt(t.key.data(), t.iv.data(), aad_len,
                                           Data(t.aad), len, Data(t.msg),
                                           Data(ct), tag.data());
    // Every invalid case with a 96-bit nonce has a modified tag, so the
    // ciphertext still matches.
    EXPECT_EQ(t.ct, ct);
    EXPECT_EQ(t.valid, t.tag == tag);

    // The AEAD encrypts with the keystream starting at block counter 1.
    std::vector<uint8_t> xored(len);
    Hacl_Chacha20_Vec128_chacha20_encrypt_128(len, Data(xored), Data(t.msg),
                                              t.key.data(), t.iv.data(), 1);
    EXPECT_EQ(t.ct, xored);

    std::vector<uint8_t> pt(len);
    uint32_t res = Hacl_Chacha20Poly1305_128_aead_decrypt(
        t.key.data(), t.iv.data(), aad_len, Data(t.aad), len, Data(pt),
        Data(t.ct), t.tag.data());
    if (t.valid) {
      EXPECT_EQ(0U, res);
      EXPECT_EQ(t.msg, pt);
    } else {
      EXPECT_NE(0U, res);
    }
  }
};

TEST_F(HaclChaCha20Poly1305_128Test, WycheproofVectors) { RunWycheproof(); }

}  // namespace nss_test

#endif
