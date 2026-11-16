/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef FREEBL_NO_DEPEND
#include "stubs.h"
#endif

#include "blapi.h"
#include "blapii.h"
#include "prerr.h"
#include "secerr.h"
#include "secmpi.h"
#include "secitem.h"
#include "mplogic.h"
#include "ec.h"
#include "ecl.h"
#include "verified/Hacl_P384.h"
#include "verified/Hacl_P521.h"
#include "secport.h"
#include "verified/Hacl_Ed25519.h"

#define EC_DOUBLECHECK PR_FALSE

SECStatus
ec_ED25519_pt_validate(const SECItem *px)
{
    if (!px || !px->data || px->len != Ed25519_PUBLIC_KEYLEN) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    return SECSuccess;
}

SECStatus
ec_ED25519_scalar_validate(const SECItem *scalar)
{
    if (!scalar || !scalar->data || scalar->len != Ed25519_PRIVATE_KEYLEN) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    return SECSuccess;
}

static const ECMethod kMethods[] = {
    { ECCurve25519,
      ec_Curve25519_pt_mul,
      ec_Curve25519_pt_validate,
      ec_Curve25519_scalar_validate,
      NULL,
      NULL },
    {
        ECCurve_NIST_P256,
        ec_secp256r1_pt_mul,
        ec_secp256r1_pt_validate,
        ec_secp256r1_scalar_validate,
        ec_secp256r1_sign_digest,
        ec_secp256r1_verify_digest,
    },
    {
        ECCurve_NIST_P384,
        ec_secp384r1_pt_mul,
        ec_secp384r1_pt_validate,
        ec_secp384r1_scalar_validate,
        ec_secp384r1_sign_digest,
        ec_secp384r1_verify_digest,
    },
    {
        ECCurve_NIST_P521,
        ec_secp521r1_pt_mul,
        ec_secp521r1_pt_validate,
        ec_secp521r1_scalar_validate,
        ec_secp521r1_sign_digest,
        ec_secp521r1_verify_digest,
    },
    { ECCurve_Ed25519,
      NULL,
      ec_ED25519_pt_validate,
      ec_ED25519_scalar_validate,
      NULL,
      NULL },
};

static const ECMethod *
ec_get_method_from_name(ECCurveName name)
{
    unsigned long i;
    for (i = 0; i < sizeof(kMethods) / sizeof(kMethods[0]); ++i) {
        if (kMethods[i].name == name) {
            return &kMethods[i];
        }
    }
    return NULL;
}

/* Generates a new EC key pair. The private key is a supplied
 * value and the public key is the result of performing a scalar
 * point multiplication of that value with the curve's base point.
 */
SECStatus
ec_NewKey(ECParams *ecParams, ECPrivateKey **privKey,
          const unsigned char *privKeyBytes, int privKeyLen)
{
    SECStatus rv = SECFailure;
    PLArenaPool *arena;
    ECPrivateKey *key;
    int len;

    if (!ecParams || ecParams->name == ECCurve_noName ||
        !privKey || !privKeyBytes || privKeyLen <= 0) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (ecParams->fieldID.type != ec_field_plain) {
        PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
        return SECFailure;
    }

    /* Initialize an arena for the EC key. */
    if (!(arena = PORT_NewArena(NSS_FREEBL_DEFAULT_CHUNKSIZE)))
        return SECFailure;

    key = (ECPrivateKey *)PORT_ArenaZAlloc(arena, sizeof(ECPrivateKey));
    if (!key) {
        goto cleanup;
    }

    /* Set the version number (SEC 1 section C.4 says it should be 1) */
    SECITEM_AllocItem(arena, &key->version, 1);
    key->version.data[0] = 1;

    /* Copy all of the fields from the ECParams argument to the
     * ECParams structure within the private key.
     */
    key->ecParams.arena = arena;
    key->ecParams.type = ecParams->type;
    key->ecParams.fieldID.size = ecParams->fieldID.size;
    key->ecParams.fieldID.type = ecParams->fieldID.type;
    CHECK_SEC_OK(SECITEM_CopyItem(arena, &key->ecParams.fieldID.u.prime,
                                  &ecParams->fieldID.u.prime));
    key->ecParams.fieldID.k1 = ecParams->fieldID.k1;
    key->ecParams.fieldID.k2 = ecParams->fieldID.k2;
    key->ecParams.fieldID.k3 = ecParams->fieldID.k3;
    CHECK_SEC_OK(SECITEM_CopyItem(arena, &key->ecParams.curve.a,
                                  &ecParams->curve.a));
    CHECK_SEC_OK(SECITEM_CopyItem(arena, &key->ecParams.curve.b,
                                  &ecParams->curve.b));
    CHECK_SEC_OK(SECITEM_CopyItem(arena, &key->ecParams.curve.seed,
                                  &ecParams->curve.seed));
    CHECK_SEC_OK(SECITEM_CopyItem(arena, &key->ecParams.base,
                                  &ecParams->base));
    CHECK_SEC_OK(SECITEM_CopyItem(arena, &key->ecParams.order,
                                  &ecParams->order));
    key->ecParams.cofactor = ecParams->cofactor;
    CHECK_SEC_OK(SECITEM_CopyItem(arena, &key->ecParams.DEREncoding,
                                  &ecParams->DEREncoding));
    key->ecParams.name = ecParams->name;
    CHECK_SEC_OK(SECITEM_CopyItem(arena, &key->ecParams.curveOID,
                                  &ecParams->curveOID));

    SECITEM_AllocItem(arena, &key->publicValue, EC_GetPointSize(ecParams));
    len = ecParams->order.len;
    SECITEM_AllocItem(arena, &key->privateValue, len);

    /* Copy private key */
    if (privKeyLen >= len) {
        memcpy(key->privateValue.data, privKeyBytes, len);
    } else {
        memset(key->privateValue.data, 0, (len - privKeyLen));
        memcpy(key->privateValue.data + (len - privKeyLen), privKeyBytes, privKeyLen);
    }

    /* Compute corresponding public key */

    /* Use curve specific code for point multiplication */
    if (ecParams->name == ECCurve_Ed25519) {
        CHECK_SEC_OK(ED_DerivePublicKey(&key->privateValue, &key->publicValue));
    } else {
        const ECMethod *method = ec_get_method_from_name(ecParams->name);
        if (method == NULL || method->pt_mul == NULL) {
            PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
            rv = SECFailure;
            goto cleanup;
        }
        CHECK_SEC_OK(method->pt_mul(&key->publicValue, &key->privateValue, NULL));
    }

    NSS_DECLASSIFY(key->publicValue.data, key->publicValue.len); /* Declassifying public key to avoid false positive */
    *privKey = key;
    return SECSuccess;

cleanup:
    PORT_FreeArena(arena, PR_TRUE);
    return rv;
}

/* Generates a new EC key pair. The private key is a supplied
 * random value (in seed) and the public key is the result of
 * performing a scalar point multiplication of that value with
 * the curve's base point.
 */
SECStatus
EC_NewKeyFromSeed(ECParams *ecParams, ECPrivateKey **privKey,
                  const unsigned char *seed, int seedlen)
{
    return ec_NewKey(ecParams, privKey, seed, seedlen);
}

/* Generate a random private key using the algorithm A.4.1 or A.4.2 of ANSI X9.62,
 * modified a la FIPS 186-2 Change Notice 1 to eliminate the bias in the
 * random number generator.
 */

SECStatus
ec_GenerateRandomPrivateKey(ECParams *ecParams, SECItem *privKey)
{
    SECStatus rv = SECFailure;

    unsigned int len = EC_GetScalarSize(ecParams);

    if (privKey->len != len || privKey->data == NULL) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    const ECMethod *method = ec_get_method_from_name(ecParams->name);
    if (method == NULL || method->scalar_validate == NULL) {
        PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
        return SECFailure;
    }

    uint8_t leading_coeff_mask;
    switch (ecParams->name) {
        case ECCurve_Ed25519:
        case ECCurve25519:
        case ECCurve_NIST_P256:
        case ECCurve_NIST_P384:
            leading_coeff_mask = 0xff;
            break;
        case ECCurve_NIST_P521:
            leading_coeff_mask = 0x01;
            break;
        default:
            PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
            return SECFailure;
    }

    /* The rejection sampling method from FIPS 186-5 A.4.2 */
    int count = 100;
    do {
        rv = RNG_GenerateGlobalRandomBytes(privKey->data, len);
        if (rv != SECSuccess) {
            PORT_SetError(SEC_ERROR_NEED_RANDOM);
            return SECFailure;
        }
        privKey->data[0] &= leading_coeff_mask;
        NSS_CLASSIFY(privKey->data, privKey->len);
        rv = method->scalar_validate(privKey);
    } while (rv != SECSuccess && --count > 0);

    if (rv != SECSuccess) { // implies count == 0
        PORT_SetError(SEC_ERROR_BAD_KEY);
    }

    return rv;
}

/* Generates a new EC key pair. The private key is a random value and
 * the public key is the result of performing a scalar point multiplication
 * of that value with the curve's base point.
 */
SECStatus
EC_NewKey(ECParams *ecParams, ECPrivateKey **privKey)
{
    SECStatus rv = SECFailure;
    SECItem privKeyRand = { siBuffer, NULL, 0 };

    if (!ecParams || ecParams->name == ECCurve_noName || !privKey) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    SECITEM_AllocItem(NULL, &privKeyRand, EC_GetScalarSize(ecParams));
    if (privKeyRand.data == NULL) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        rv = SECFailure;
        goto cleanup;
    }
    rv = ec_GenerateRandomPrivateKey(ecParams, &privKeyRand);
    if (rv != SECSuccess || privKeyRand.data == NULL) {
        goto cleanup;
    }
    /* generate public key */
    CHECK_SEC_OK(ec_NewKey(ecParams, privKey, privKeyRand.data, privKeyRand.len));

cleanup:
    if (privKeyRand.data) {
        SECITEM_ZfreeItem(&privKeyRand, PR_FALSE);
    }
#if EC_DEBUG
    printf("EC_NewKey returning %s\n",
           (rv == SECSuccess) ? "success" : "failure");
#endif

    return rv;
}

/* Validates an EC public key as described in Section 5.2.2 of
 * X9.62. The ECDH primitive when used without the cofactor does
 * not address small subgroup attacks, which may occur when the
 * public key is not valid. These attacks can be prevented by
 * validating the public key before using ECDH.
 */
SECStatus
EC_ValidatePublicKey(ECParams *ecParams, SECItem *publicValue)
{
    if (!ecParams || ecParams->name == ECCurve_noName ||
        !publicValue || !publicValue->len) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    /* Uses curve specific code for point validation. */
    if (ecParams->fieldID.type != ec_field_plain) {
        PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
        return SECFailure;
    }

    const ECMethod *method = ec_get_method_from_name(ecParams->name);
    if (method == NULL || method->pt_validate == NULL) {
        /* unknown curve */
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    SECStatus rv = method->pt_validate(publicValue);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_BAD_KEY);
    }
    return rv;
}

/*
** Performs an ECDH key derivation by computing the scalar point
** multiplication of privateValue and publicValue (with or without the
** cofactor) and returns the x-coordinate of the resulting elliptic
** curve point in derived secret.  If successful, derivedSecret->data
** is set to the address of the newly allocated buffer containing the
** derived secret, and derivedSecret->len is the size of the secret
** produced. It is the caller's responsibility to free the allocated
** buffer containing the derived secret.
*/
SECStatus
ECDH_Derive(SECItem *publicValue,
            ECParams *ecParams,
            SECItem *privateValue,
            PRBool withCofactor,
            SECItem *derivedSecret)
{
    if (!publicValue || !publicValue->len ||
        !ecParams || ecParams->name == ECCurve_noName ||
        !privateValue || !privateValue->len || !derivedSecret) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    /*
     * Make sure the point is on the requested curve to avoid
     * certain small subgroup attacks.
     */
    if (EC_ValidatePublicKey(ecParams, publicValue) != SECSuccess) {
        PORT_SetError(SEC_ERROR_BAD_KEY);
        return SECFailure;
    }

    /* Perform curve specific multiplication using ECMethod */
    if (ecParams->fieldID.type != ec_field_plain) {
        PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
        return SECFailure;
    }

    const ECMethod *method = ec_get_method_from_name(ecParams->name);
    if (method == NULL || method->pt_validate == NULL ||
        method->pt_mul == NULL) {
        PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
        return SECFailure;
    }

    memset(derivedSecret, 0, sizeof(*derivedSecret));
    derivedSecret = SECITEM_AllocItem(NULL, derivedSecret, EC_GetScalarSize(ecParams));
    if (derivedSecret == NULL) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return SECFailure;
    }

    SECStatus rv = method->pt_mul(derivedSecret, privateValue, publicValue);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_BAD_KEY);
        SECITEM_ZfreeItem(derivedSecret, PR_FALSE);
    }
    return rv;
}

/* Computes the ECDSA signature (a concatenation of two values r and s)
 * on the digest using the given key and the random value kb (used in
 * computing s).
 */

static SECStatus
ec_SignDigestWithSeed(ECPrivateKey *key, SECItem *signature,
                      const SECItem *digest, const unsigned char *kb, const int kblen)
{
    ECParams *ecParams = NULL;
    unsigned olen; /* length in bytes of the base point order */

    /* Check args */
    if (!key || !signature || !digest || !kb || (kblen <= 0)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    ecParams = &(key->ecParams);
    olen = ecParams->order.len;
    if (signature->data == NULL) {
        /* a call to get the signature length only */
        signature->len = 2 * olen;
        return SECSuccess;
    }
    if (signature->len < 2 * olen) {
        PORT_SetError(SEC_ERROR_OUTPUT_LEN);
        return SECFailure;
    }

    /* Perform curve specific signature using ECMethod */
    if (ecParams->fieldID.type != ec_field_plain) {
        PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
        return SECFailure;
    }

    const ECMethod *method = ec_get_method_from_name(ecParams->name);
    if (method == NULL || method->sign_digest == NULL) {
        PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
        return SECFailure;
    }

    SECStatus rv = method->sign_digest(key, signature, digest, kb, kblen);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
    }

#if EC_DEBUG
    printf("ECDSA signing with seed %s\n",
           (rv == SECSuccess) ? "succeeded" : "failed");
#endif
    return rv;
}

SECStatus
ECDSA_SignDigestWithSeed(ECPrivateKey *key, SECItem *signature,
                         const SECItem *digest, const unsigned char *kb, const int kblen)
{
#if EC_DEBUG || EC_DOUBLECHECK
    SECItem *signature2 = SECITEM_AllocItem(NULL, NULL, signature->len);
    SECStatus signSuccess = ec_SignDigestWithSeed(key, signature, digest, kb, kblen);
    SECStatus signSuccessDouble = ec_SignDigestWithSeed(key, signature2, digest, kb, kblen);
    int signaturesEqual = NSS_SecureMemcmp(signature->data, signature2->data, signature->len);
    SECStatus rv;

    if ((signaturesEqual == 0) && (signSuccess == SECSuccess) && (signSuccessDouble == SECSuccess)) {
        rv = SECSuccess;
    } else {
        rv = SECFailure;
    }

#if EC_DEBUG
    printf("ECDSA signing with seed %s after signing twice\n", (rv == SECSuccess) ? "succeeded" : "failed");
#endif

    SECITEM_FreeItem(signature2, PR_TRUE);
    return rv;
#else
    return ec_SignDigestWithSeed(key, signature, digest, kb, kblen);
#endif
}

/*
** Computes the ECDSA signature on the digest using the given key
** and a random seed.
*/
SECStatus
ECDSA_SignDigest(ECPrivateKey *key, SECItem *signature, const SECItem *digest)
{
    SECItem nonceRand = { siBuffer, NULL, 0 };

    if (!key) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    /* Generate random value k */
    SECITEM_AllocItem(NULL, &nonceRand, EC_GetScalarSize(&key->ecParams));
    if (nonceRand.data == NULL) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return SECFailure;
    }

    SECStatus rv = ec_GenerateRandomPrivateKey(&key->ecParams, &nonceRand);
    if (rv != SECSuccess) {
        goto cleanup;
    }

    /* Generate ECDSA signature with the specified k value */
    rv = ECDSA_SignDigestWithSeed(key, signature, digest, nonceRand.data, nonceRand.len);
    NSS_DECLASSIFY(signature->data, signature->len);

cleanup:
    SECITEM_ZfreeItem(&nonceRand, PR_FALSE);

#if EC_DEBUG
    printf("ECDSA signing %s\n",
           (rv == SECSuccess) ? "succeeded" : "failed");
#endif

    return rv;
}

/*
** Checks the signature on the given digest using the key provided.
**
** The key argument must represent a valid EC public key (a point on
** the relevant curve).  If it is not a valid point, then the behavior
** of this function is undefined.  In cases where a public key might
** not be valid, use EC_ValidatePublicKey to check.
*/
SECStatus
ECDSA_VerifyDigest(ECPublicKey *key, const SECItem *signature,
                   const SECItem *digest)
{
    SECStatus rv = SECFailure;
    ECParams *ecParams = NULL;

    /* Check args */
    if (!key || !signature || !digest) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    ecParams = &(key->ecParams);

    /* Perform curve specific signature verification using ECMethod */
    if (ecParams->fieldID.type != ec_field_plain) {
        PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
        return SECFailure;
    }

    const ECMethod *method = ec_get_method_from_name(ecParams->name);
    if (method == NULL || method->verify_digest == NULL) {
        PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
        return SECFailure;
    }

    rv = method->verify_digest(key, signature, digest);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_BAD_SIGNATURE);
    }

#if EC_DEBUG
    printf("ECDSA verification %s\n",
           (rv == SECSuccess) ? "succeeded" : "failed");
#endif

    return rv;
}

/*EdDSA: Currently only Ed22519 is implemented.*/

/*
** Computes the EdDSA signature on the message using the given key.
*/

SECStatus
ec_ED25519_public_key_validate(const ECPublicKey *key)
{
    if (!key || !(key->ecParams.name == ECCurve_Ed25519)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    return ec_ED25519_pt_validate(&key->publicValue);
}

SECStatus
ec_ED25519_private_key_validate(const ECPrivateKey *key)
{
    if (!key || !(key->ecParams.name == ECCurve_Ed25519)) {

        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    return ec_ED25519_scalar_validate(&key->privateValue);
}

SECStatus
ED_SignMessage(ECPrivateKey *key, SECItem *signature, const SECItem *msg)
{
    if (!msg || !signature || signature->len != Ed25519_SIGN_LEN) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (ec_ED25519_private_key_validate(key) != SECSuccess) {
        return SECFailure; /* error code set by ec_ED25519_scalar_validate. */
    }

    if (signature->data) {
        Hacl_Ed25519_sign(signature->data, key->privateValue.data, msg->len,
                          msg->data);
    }
    signature->len = ED25519_SIGN_LEN;
    BLAPI_CLEAR_STACK(2048);
    return SECSuccess;
}

/*
** Checks the signature on the given message using the key provided.
*/

SECStatus
ED_VerifyMessage(ECPublicKey *key, const SECItem *signature,
                 const SECItem *msg)
{
    if (!msg || !signature) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    /* A signature of the wrong length is a bad signature, not a bad
     * argument. */
    if (!signature->data || signature->len != Ed25519_SIGN_LEN) {
        PORT_SetError(SEC_ERROR_BAD_SIGNATURE);
        return SECFailure;
    }

    if (ec_ED25519_public_key_validate(key) != SECSuccess) {
        return SECFailure; /* error code set by ec_ED25519_pt_validate. */
    }

    bool rv = Hacl_Ed25519_verify(key->publicValue.data, msg->len, msg->data,
                                  signature->data);
    BLAPI_CLEAR_STACK(2048);

#if EC_DEBUG
    printf("ED_VerifyMessage returning %s\n",
           (rv) ? "success" : "failure");
#endif

    if (rv) {
        return SECSuccess;
    }

    PORT_SetError(SEC_ERROR_BAD_SIGNATURE);
    return SECFailure;
}

SECStatus
ED_DerivePublicKey(const SECItem *privateKey, SECItem *publicKey)
{
    /* Currently supporting only Ed25519.*/
    if (!privateKey || privateKey->len == 0 || !publicKey || publicKey->len != Ed25519_PUBLIC_KEYLEN) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (ec_ED25519_scalar_validate(privateKey) != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    Hacl_Ed25519_secret_to_public(publicKey->data, privateKey->data);
    return SECSuccess;
}

SECStatus
X25519_DerivePublicKey(const SECItem *privateKey, SECItem *publicKey)
{
    SECStatus rv = SECFailure;
    /* Currently supporting only X25519.*/
    if (!privateKey || privateKey->len == 0 || !publicKey || publicKey->len != X25519_PUBLIC_KEYLEN) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    const ECMethod *method = ec_get_method_from_name(ECCurve25519);
    if (method == NULL || method->pt_mul == NULL) {
        PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
        return SECFailure;
    }

    rv = method->pt_mul(publicKey, (SECItem *)privateKey, NULL);
    return rv;
}

SECStatus
EC_DerivePublicKey(const SECItem *privateKey, const ECParams *ecParams, SECItem *publicKey)
{
    if (!privateKey || privateKey->len == 0 || !publicKey || publicKey->len != EC_GetPointSize(ecParams)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    const ECMethod *method = ec_get_method_from_name(ecParams->name);
    if (method == NULL || method->pt_mul == NULL) {
        PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
        return SECFailure;
    }

    return method->pt_mul(publicKey, (SECItem *)privateKey, NULL);
}

/* Supported only for P-256, P-384 and P-521*/
SECStatus
EC_DecompressPublicKey(const SECItem *publicCompressed, const ECParams *ecParams, SECItem *publicUncompressed)
{
    /* I don't think that we need a special ECMethod extension for decompression. */
    switch (ecParams->name) {
        case ECCurve_NIST_P256:
            return ec_secp256r1_decompress(publicCompressed, publicUncompressed);
        case ECCurve_NIST_P384:
            return ec_secp384r1_decompress(publicCompressed, publicUncompressed);
        case ECCurve_NIST_P521:
            return ec_secp521r1_decompress(publicCompressed, publicUncompressed);
        default:
            PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
            return SECFailure;
    }
}
