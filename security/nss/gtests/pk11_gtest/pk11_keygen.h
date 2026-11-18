/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <vector>

#include "nss.h"
#include "secoid.h"

#include "nss_scoped_ptrs.h"

namespace nss_test {

// CKA_EC_PARAMS for a named curve: the DER encoding of its OID.
std::vector<uint8_t> EcParamsForCurve(SECOidTag curve_oid);

class ParamHolder;

class Pkcs11KeyPairGenerator {
 public:
  Pkcs11KeyPairGenerator(CK_MECHANISM_TYPE mech, SECOidTag curve_oid)
      : mech_(mech), curve_(curve_oid), kem_params_(CKP_INVALID_ID) {}
  Pkcs11KeyPairGenerator(CK_MECHANISM_TYPE mech)
      : Pkcs11KeyPairGenerator(mech, SEC_OID_UNKNOWN) {}
  // The KEM mechanisms take a parameter set where the others take a curve.
  // One mechanism can cover several parameter sets, so it has to be given
  // rather than inferred. SECOidTag is an enum and the parameter set is a
  // CK_ULONG, so this does not collide with the curve constructor.
  Pkcs11KeyPairGenerator(CK_MECHANISM_TYPE mech,
                         CK_NSS_KEM_PARAMETER_SET_TYPE kem_params)
      : mech_(mech), curve_(SEC_OID_UNKNOWN), kem_params_(kem_params) {}

  CK_MECHANISM_TYPE mechanism() const { return mech_; }
  SECOidTag curve() const { return curve_; }
  CK_NSS_KEM_PARAMETER_SET_TYPE kemParams() const { return kem_params_; }

  void GenerateKey(ScopedSECKEYPrivateKey* priv_key,
                   ScopedSECKEYPublicKey* pub_key, bool sensitive = true) const;

 private:
  std::unique_ptr<ParamHolder> MakeParams() const;

  CK_MECHANISM_TYPE mech_;
  SECOidTag curve_;
  CK_NSS_KEM_PARAMETER_SET_TYPE kem_params_;
};

}  // namespace nss_test
