/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "kyber.h"
#include "pk11_keygen.h"

#include "pk11pub.h"
#include "pk11pqg.h"
#include "prerror.h"

#include "gtest/gtest.h"

namespace nss_test {

class ParamHolder {
 public:
  virtual void* get() = 0;
  virtual ~ParamHolder() = default;

 protected:
  ParamHolder() = default;
};

void Pkcs11KeyPairGenerator::GenerateKey(ScopedSECKEYPrivateKey* priv_key,
                                         ScopedSECKEYPublicKey* pub_key,
                                         bool sensitive) const {
  // This function returns if an assertion fails, so don't leak anything.
  priv_key->reset(nullptr);
  pub_key->reset(nullptr);

  auto params = MakeParams();
  ASSERT_NE(nullptr, params);

  ScopedPK11SlotInfo slot(PK11_GetInternalKeySlot());
  ASSERT_TRUE(slot);

  // A FIPS token only generates sensitive private keys.
  PRBool isSensitive = (sensitive || PK11_IsFIPS()) ? PR_TRUE : PR_FALSE;
  SECKEYPublicKey* pub_tmp = NULL;
  ScopedSECKEYPrivateKey priv_tmp(
      PK11_GenerateKeyPair(slot.get(), mech_, params->get(), &pub_tmp, PR_FALSE,
                           isSensitive, nullptr));
#ifdef NSS_DISABLE_DSA
  if (mech_ == CKM_DSA_KEY_PAIR_GEN) {
    ASSERT_EQ(nullptr, priv_tmp);
    ASSERT_EQ(nullptr, pub_tmp);
    ASSERT_EQ(PORT_GetError(), SEC_ERROR_INVALID_ALGORITHM);
    return;
  }
#endif
  ASSERT_NE(nullptr, priv_tmp)
      << "PK11_GenerateKeyPair failed: " << PORT_ErrorToName(PORT_GetError());
  ASSERT_NE(nullptr, pub_tmp);

  priv_key->swap(priv_tmp);
  pub_key->reset(pub_tmp);
}

class RsaParamHolder : public ParamHolder {
 public:
  RsaParamHolder() : params_({1024, 0x010001}) {}
  ~RsaParamHolder() = default;

  void* get() override { return &params_; }

 private:
  PK11RSAGenParams params_;
};

class PqgParamHolder : public ParamHolder {
 public:
  PqgParamHolder(PQGParams* params) : params_(params) {}
  ~PqgParamHolder() = default;

  void* get() override { return params_.get(); }

 private:
  ScopedPQGParams params_;
};

class DhParamHolder : public PqgParamHolder {
 public:
  DhParamHolder(PQGParams* params)
      : PqgParamHolder(params),
        params_({nullptr, params->prime, params->base}) {}
  ~DhParamHolder() = default;

  void* get() override { return &params_; }

 private:
  SECKEYDHParams params_;
};

// RFC 7919 ffdhe2048: FIPS mode only takes DH keys over approved groups.
static const uint8_t kFfdhe2048Prime[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xAD, 0xF8, 0x54, 0x58,
    0xA2, 0xBB, 0x4A, 0x9A, 0xAF, 0xDC, 0x56, 0x20, 0x27, 0x3D, 0x3C, 0xF1,
    0xD8, 0xB9, 0xC5, 0x83, 0xCE, 0x2D, 0x36, 0x95, 0xA9, 0xE1, 0x36, 0x41,
    0x14, 0x64, 0x33, 0xFB, 0xCC, 0x93, 0x9D, 0xCE, 0x24, 0x9B, 0x3E, 0xF9,
    0x7D, 0x2F, 0xE3, 0x63, 0x63, 0x0C, 0x75, 0xD8, 0xF6, 0x81, 0xB2, 0x02,
    0xAE, 0xC4, 0x61, 0x7A, 0xD3, 0xDF, 0x1E, 0xD5, 0xD5, 0xFD, 0x65, 0x61,
    0x24, 0x33, 0xF5, 0x1F, 0x5F, 0x06, 0x6E, 0xD0, 0x85, 0x63, 0x65, 0x55,
    0x3D, 0xED, 0x1A, 0xF3, 0xB5, 0x57, 0x13, 0x5E, 0x7F, 0x57, 0xC9, 0x35,
    0x98, 0x4F, 0x0C, 0x70, 0xE0, 0xE6, 0x8B, 0x77, 0xE2, 0xA6, 0x89, 0xDA,
    0xF3, 0xEF, 0xE8, 0x72, 0x1D, 0xF1, 0x58, 0xA1, 0x36, 0xAD, 0xE7, 0x35,
    0x30, 0xAC, 0xCA, 0x4F, 0x48, 0x3A, 0x79, 0x7A, 0xBC, 0x0A, 0xB1, 0x82,
    0xB3, 0x24, 0xFB, 0x61, 0xD1, 0x08, 0xA9, 0x4B, 0xB2, 0xC8, 0xE3, 0xFB,
    0xB9, 0x6A, 0xDA, 0xB7, 0x60, 0xD7, 0xF4, 0x68, 0x1D, 0x4F, 0x42, 0xA3,
    0xDE, 0x39, 0x4D, 0xF4, 0xAE, 0x56, 0xED, 0xE7, 0x63, 0x72, 0xBB, 0x19,
    0x0B, 0x07, 0xA7, 0xC8, 0xEE, 0x0A, 0x6D, 0x70, 0x9E, 0x02, 0xFC, 0xE1,
    0xCD, 0xF7, 0xE2, 0xEC, 0xC0, 0x34, 0x04, 0xCD, 0x28, 0x34, 0x2F, 0x61,
    0x91, 0x72, 0xFE, 0x9C, 0xE9, 0x85, 0x83, 0xFF, 0x8E, 0x4F, 0x12, 0x32,
    0xEE, 0xF2, 0x81, 0x83, 0xC3, 0xFE, 0x3B, 0x1B, 0x4C, 0x6F, 0xAD, 0x73,
    0x3B, 0xB5, 0xFC, 0xBC, 0x2E, 0xC2, 0x20, 0x05, 0xC5, 0x8E, 0xF1, 0x83,
    0x7D, 0x16, 0x83, 0xB2, 0xC6, 0xF3, 0x4A, 0x26, 0xC1, 0xB2, 0xEF, 0xFA,
    0x88, 0x6B, 0x42, 0x38, 0x61, 0x28, 0x5C, 0x97, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
};
static const uint8_t kFfdhe2048Base[] = {0x02};

class FfdheParamHolder : public ParamHolder {
 public:
  FfdheParamHolder()
      : params_({nullptr,
                 {siBuffer, const_cast<uint8_t*>(kFfdhe2048Prime),
                  sizeof(kFfdhe2048Prime)},
                 {siBuffer, const_cast<uint8_t*>(kFfdhe2048Base),
                  sizeof(kFfdhe2048Base)}}) {}
  ~FfdheParamHolder() = default;

  void* get() override { return &params_; }

 private:
  SECKEYDHParams params_;
};

std::vector<uint8_t> EcParamsForCurve(SECOidTag curve_oid) {
  SECOidData* curve = SECOID_FindOIDByTag(curve_oid);
  EXPECT_NE(nullptr, curve);
  if (!curve) {
    return {};
  }
  std::vector<uint8_t> params = {SEC_ASN1_OBJECT_ID,
                                 static_cast<uint8_t>(curve->oid.len)};
  params.insert(params.end(), curve->oid.data,
                curve->oid.data + curve->oid.len);
  return params;
}

/* Also used for EdDSA. */
class EcParamHolder : public ParamHolder {
 public:
  EcParamHolder(SECOidTag curve_oid) : params_(EcParamsForCurve(curve_oid)) {
    ec_params_ = {siBuffer, params_.data(),
                  static_cast<unsigned int>(params_.size())};
  }
  ~EcParamHolder() = default;

  void* get() override { return &ec_params_; }

 private:
  std::vector<uint8_t> params_;
  SECKEYECParams ec_params_;
};

class KyberParamHolder : public ParamHolder {
 public:
  KyberParamHolder(CK_NSS_KEM_PARAMETER_SET_TYPE aParams) : mParams(aParams) {}
  void* get() override { return &mParams; }

 private:
  CK_NSS_KEM_PARAMETER_SET_TYPE mParams;
};

std::unique_ptr<ParamHolder> Pkcs11KeyPairGenerator::MakeParams() const {
  switch (mech_) {
    case CKM_RSA_PKCS_KEY_PAIR_GEN:
      std::cerr << "Generate RSA pair" << std::endl;
      return std::unique_ptr<ParamHolder>(new RsaParamHolder());

    case CKM_DSA_KEY_PAIR_GEN:
    case CKM_DH_PKCS_KEY_PAIR_GEN: {
      if (mech_ == CKM_DH_PKCS_KEY_PAIR_GEN && PK11_IsFIPS()) {
        std::cerr << "Generate DH pair over ffdhe2048" << std::endl;
        return std::unique_ptr<ParamHolder>(new FfdheParamHolder());
      }
      PQGParams* pqg_params = nullptr;
      PQGVerify* pqg_verify = nullptr;
      const unsigned int key_size = 1024;
      SECStatus rv = PK11_PQG_ParamGenV2(key_size, 0, key_size / 16,
                                         &pqg_params, &pqg_verify);
      if (rv != SECSuccess) {
        ADD_FAILURE() << "PK11_PQG_ParamGenV2 failed";
        return nullptr;
      }
      EXPECT_NE(nullptr, pqg_verify);
      EXPECT_NE(nullptr, pqg_params);
      PK11_PQG_DestroyVerify(pqg_verify);
      if (mech_ == CKM_DSA_KEY_PAIR_GEN) {
        std::cerr << "Generate DSA pair" << std::endl;
        return std::unique_ptr<ParamHolder>(new PqgParamHolder(pqg_params));
      }
      std::cerr << "Generate DH pair" << std::endl;
      return std::unique_ptr<ParamHolder>(new DhParamHolder(pqg_params));
    }

    case CKM_EC_EDWARDS_KEY_PAIR_GEN:
      std::cerr << "Generate ED pair on " << curve_ << std::endl;
      return std::unique_ptr<ParamHolder>(
          new EcParamHolder(SEC_OID_ED25519_PUBLIC_KEY));

    case CKM_EC_MONTGOMERY_KEY_PAIR_GEN:
      std::cerr << "Generate X25519 pair on " << curve_ << std::endl;
      return std::unique_ptr<ParamHolder>(new EcParamHolder(SEC_OID_X25519));

    case CKM_EC_KEY_PAIR_GEN:
      std::cerr << "Generate EC pair on " << curve_ << std::endl;
      return std::unique_ptr<ParamHolder>(new EcParamHolder(curve_));

    case CKM_NSS_KYBER_KEY_PAIR_GEN:
    case CKM_NSS_ML_KEM_KEY_PAIR_GEN:
    case CKM_ML_KEM_KEY_PAIR_GEN: {
      // A single mechanism covers more than one parameter set, so inferring
      // one from the mechanism would silently generate the wrong key.
      if (kem_params_ == CKP_INVALID_ID) {
        ADD_FAILURE() << "KEM key generation needs an explicit parameter set";
        return nullptr;
      }
      std::cerr << "Generate KEM pair, parameter set " << kem_params_
                << std::endl;
      return std::unique_ptr<ParamHolder>(new KyberParamHolder(kem_params_));
    }

    default:
      ADD_FAILURE() << "unknown OID " << mech_;
  }
  return nullptr;
}

}  // namespace nss_test
