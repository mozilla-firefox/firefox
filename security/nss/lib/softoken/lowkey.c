/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "lowkeyi.h"
#include "secoid.h"
#include "secitem.h"
#include "secder.h"
#include "base64.h"
#include "secasn1.h"
#include "secerr.h"
#include "softoken.h"
#include "ec.h"
#include "kem.h"

SEC_ASN1_MKSUB(SEC_AnyTemplate)
SEC_ASN1_MKSUB(SEC_BitStringTemplate)
SEC_ASN1_MKSUB(SEC_ObjectIDTemplate)
SEC_ASN1_MKSUB(SECOID_AlgorithmIDTemplate)
SEC_ASN1_MKSUB(SEC_OctetStringTemplate)

const SEC_ASN1Template nsslowkey_AttributeTemplate[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(NSSLOWKEYAttribute) },
    { SEC_ASN1_OBJECT_ID, offsetof(NSSLOWKEYAttribute, attrType) },
    { SEC_ASN1_SET_OF | SEC_ASN1_XTRN,
      offsetof(NSSLOWKEYAttribute, attrValue),
      SEC_ASN1_SUB(SEC_AnyTemplate) },
    { 0 }
};

const SEC_ASN1Template nsslowkey_SetOfAttributeTemplate[] = {
    { SEC_ASN1_SET_OF, 0, nsslowkey_AttributeTemplate },
};
/* ASN1 Templates for new decoder/encoder */
const SEC_ASN1Template nsslowkey_PrivateKeyInfoTemplate[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(NSSLOWKEYPrivateKeyInfo) },
    { SEC_ASN1_INTEGER,
      offsetof(NSSLOWKEYPrivateKeyInfo, version) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(NSSLOWKEYPrivateKeyInfo, algorithm),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_OCTET_STRING,
      offsetof(NSSLOWKEYPrivateKeyInfo, privateKey) },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED | SEC_ASN1_CONTEXT_SPECIFIC | 0,
      offsetof(NSSLOWKEYPrivateKeyInfo, attributes),
      nsslowkey_SetOfAttributeTemplate },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 1,
      offsetof(NSSLOWKEYPrivateKeyInfo, publicKey),
      SEC_ASN1_SUB(SEC_BitStringTemplate) },
    { 0 }
};

const SEC_ASN1Template nsslowkey_SubjectPublicKeyInfoTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(NSSLOWKEYSubjectPublicKeyInfo) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(NSSLOWKEYSubjectPublicKeyInfo, algorithm),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_BIT_STRING,
      offsetof(NSSLOWKEYSubjectPublicKeyInfo, subjectPublicKey) },
    { 0 }
};

const SEC_ASN1Template nsslowkey_RSAPublicKeyTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(NSSLOWKEYPublicKey) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPublicKey, u.rsa.modulus) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPublicKey, u.rsa.publicExponent) },
    { 0 }
};

const SEC_ASN1Template nsslowkey_PQGParamsTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(PQGParams) },
    { SEC_ASN1_INTEGER, offsetof(PQGParams, prime) },
    { SEC_ASN1_INTEGER, offsetof(PQGParams, subPrime) },
    { SEC_ASN1_INTEGER, offsetof(PQGParams, base) },
    { 0 }
};

const SEC_ASN1Template nsslowkey_RSAPrivateKeyTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(NSSLOWKEYPrivateKey) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.rsa.version) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.rsa.modulus) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.rsa.publicExponent) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.rsa.privateExponent) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.rsa.prime1) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.rsa.prime2) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.rsa.exponent1) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.rsa.exponent2) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.rsa.coefficient) },
    { 0 }
};

const SEC_ASN1Template nsslowkey_DSAPrivateKeyTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(NSSLOWKEYPrivateKey) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.dsa.publicValue) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.dsa.privateValue) },
    { 0 }
};

const SEC_ASN1Template nsslowkey_PQBothSeedAndPrivateKeyTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(NSSLOWKEYPrivateKey) },
    { SEC_ASN1_OCTET_STRING, offsetof(NSSLOWKEYPrivateKey, u.genpq.seedItem) },
    { SEC_ASN1_OCTET_STRING, offsetof(NSSLOWKEYPrivateKey, u.genpq.keyItem) },
    { 0 }
};

const SEC_ASN1Template nsslowkey_PQSeedTemplate[] = {
    /* the explicit | 0 here is source code doumentation, tell
     * clang warnings to let it ride */
    { SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 0, // NOLINT(misc-redundant-expression)
      offsetof(NSSLOWKEYPrivateKey, u.genpq.seedItem),
      SEC_ASN1_SUB(SEC_OctetStringTemplate) },
    { 0 }
};
const SEC_ASN1Template nsslowkey_PQPrivateKeyTemplate[] = {
    { SEC_ASN1_OCTET_STRING, offsetof(NSSLOWKEYPrivateKey, u.genpq.keyItem) },
    { 0 }
};

const SEC_ASN1Template nsslowkey_DSAPrivateKeyExportTemplate[] = {
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.dsa.privateValue) },
};

const SEC_ASN1Template nsslowkey_DHPrivateKeyTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(NSSLOWKEYPrivateKey) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.dh.publicValue) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.dh.privateValue) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.dh.base) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.dh.prime) },
    { 0 }
};

/* NOTE: The SECG specification allows the private key structure
 * to contain curve parameters but recommends that they be stored
 * in the PrivateKeyAlgorithmIdentifier field of the PrivateKeyInfo
 * instead.
 */
const SEC_ASN1Template nsslowkey_ECPrivateKeyTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(NSSLOWKEYPrivateKey) },
    { SEC_ASN1_INTEGER, offsetof(NSSLOWKEYPrivateKey, u.ec.version) },
    { SEC_ASN1_OCTET_STRING,
      offsetof(NSSLOWKEYPrivateKey, u.ec.privateValue) },
    /* We only support named curves for which the parameters are
     * encoded as an object ID.
     */
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED |
          SEC_ASN1_EXPLICIT | SEC_ASN1_CONTEXT_SPECIFIC |
          SEC_ASN1_XTRN | 0,
      offsetof(NSSLOWKEYPrivateKey, u.ec.ecParams.curveOID),
      SEC_ASN1_SUB(SEC_ObjectIDTemplate) },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED |
          SEC_ASN1_EXPLICIT | SEC_ASN1_CONTEXT_SPECIFIC |
          SEC_ASN1_XTRN | 1,
      offsetof(NSSLOWKEYPrivateKey, u.ec.publicValue),
      SEC_ASN1_SUB(SEC_BitStringTemplate) },
    { 0 }
};
/*
 * See bugzilla bug 125359
 * Since NSS (via PKCS#11) wants to handle big integers as unsigned ints,
 * all of the templates above that en/decode into integers must be converted
 * from ASN.1's signed integer type.  This is done by marking either the
 * source or destination (encoding or decoding, respectively) type as
 * siUnsignedInteger.
 */

void
prepare_low_rsa_priv_key_for_asn1(NSSLOWKEYPrivateKey *key)
{
    key->u.rsa.modulus.type = siUnsignedInteger;
    key->u.rsa.publicExponent.type = siUnsignedInteger;
    key->u.rsa.privateExponent.type = siUnsignedInteger;
    key->u.rsa.prime1.type = siUnsignedInteger;
    key->u.rsa.prime2.type = siUnsignedInteger;
    key->u.rsa.exponent1.type = siUnsignedInteger;
    key->u.rsa.exponent2.type = siUnsignedInteger;
    key->u.rsa.coefficient.type = siUnsignedInteger;
}

void
prepare_low_rsa_pub_key_for_asn1(NSSLOWKEYPublicKey *key)
{
    key->u.rsa.modulus.type = siUnsignedInteger;
    key->u.rsa.publicExponent.type = siUnsignedInteger;
}

void
prepare_low_pqg_params_for_asn1(PQGParams *params)
{
    params->prime.type = siUnsignedInteger;
    params->subPrime.type = siUnsignedInteger;
    params->base.type = siUnsignedInteger;
}

void
prepare_low_dsa_priv_key_for_asn1(NSSLOWKEYPrivateKey *key)
{
    key->u.dsa.publicValue.type = siUnsignedInteger;
    key->u.dsa.privateValue.type = siUnsignedInteger;
    key->u.dsa.params.prime.type = siUnsignedInteger;
    key->u.dsa.params.subPrime.type = siUnsignedInteger;
    key->u.dsa.params.base.type = siUnsignedInteger;
}

void
prepare_low_dsa_priv_key_export_for_asn1(NSSLOWKEYPrivateKey *key)
{
    key->u.dsa.privateValue.type = siUnsignedInteger;
}

void
prepare_low_dh_priv_key_for_asn1(NSSLOWKEYPrivateKey *key)
{
    key->u.dh.prime.type = siUnsignedInteger;
    key->u.dh.base.type = siUnsignedInteger;
    key->u.dh.publicValue.type = siUnsignedInteger;
    key->u.dh.privateValue.type = siUnsignedInteger;
}

void
prepare_low_ecparams_for_asn1(ECParams *params)
{
    params->DEREncoding.type = siUnsignedInteger;
    params->curveOID.type = siUnsignedInteger;
}

void
prepare_low_ec_priv_key_for_asn1(NSSLOWKEYPrivateKey *key)
{
    key->u.ec.version.type = siUnsignedInteger;
    key->u.ec.ecParams.DEREncoding.type = siUnsignedInteger;
    key->u.ec.ecParams.curveOID.type = siUnsignedInteger;
    key->u.ec.privateValue.type = siUnsignedInteger;
    key->u.ec.publicValue.type = siUnsignedInteger;
}

void
nsslowkey_DestroyPrivateKey(NSSLOWKEYPrivateKey *privk)
{
    if (privk && privk->arena) {
        PORT_FreeArena(privk->arena, PR_TRUE);
    }
}

void
nsslowkey_DestroyPublicKey(NSSLOWKEYPublicKey *pubk)
{
    if (pubk && pubk->arena) {
        PORT_FreeArena(pubk->arena, PR_TRUE);
    }
}
unsigned
nsslowkey_PublicModulusLen(NSSLOWKEYPublicKey *pubk)
{
    /* interpret modulus length as key strength... in
     * fortezza that's the public key length */

    switch (pubk->keyType) {
        case NSSLOWKEYRSAKey:
            if (pubk->u.rsa.modulus.len == 0) {
                return 0;
            }
            if (pubk->u.rsa.modulus.data[0] == 0) {
                return pubk->u.rsa.modulus.len - 1;
            }
            return pubk->u.rsa.modulus.len;
        default:
            break;
    }
    return 0;
}

unsigned
nsslowkey_PrivateModulusLen(NSSLOWKEYPrivateKey *privk)
{
    switch (privk->keyType) {
        case NSSLOWKEYRSAKey:
            if (privk->u.rsa.modulus.len == 0) {
                return 0;
            }
            if (privk->u.rsa.modulus.data[0] == 0) {
                return privk->u.rsa.modulus.len - 1;
            }
            return privk->u.rsa.modulus.len;
        default:
            break;
    }
    return 0;
}

NSSLOWKEYPublicKey *
nsslowkey_ConvertToPublicKey(NSSLOWKEYPrivateKey *privk)
{
    NSSLOWKEYPublicKey *pubk;
    SECItem publicValue = { siBuffer, NULL, 0 };
    PLArenaPool *arena;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return NULL;
    }

    switch (privk->keyType) {
        case NSSLOWKEYRSAKey:
        case NSSLOWKEYNullKey:
            pubk = (NSSLOWKEYPublicKey *)PORT_ArenaZAlloc(arena,
                                                          sizeof(NSSLOWKEYPublicKey));
            if (pubk != NULL) {
                SECStatus rv;

                pubk->arena = arena;
                pubk->keyType = privk->keyType;
                if (privk->keyType == NSSLOWKEYNullKey)
                    return pubk;
                rv = SECITEM_CopyItem(arena, &pubk->u.rsa.modulus,
                                      &privk->u.rsa.modulus);
                if (rv == SECSuccess) {
                    rv = SECITEM_CopyItem(arena, &pubk->u.rsa.publicExponent,
                                          &privk->u.rsa.publicExponent);
                    if (rv == SECSuccess) {
                        /* this key was already verified fully as
                         * a private key */
                        pubk->u.rsa.needVerify = PR_FALSE;
                        return pubk;
                    }
                }
            } else {
                PORT_SetError(SEC_ERROR_NO_MEMORY);
            }
            break;
        case NSSLOWKEYDSAKey:
            pubk = (NSSLOWKEYPublicKey *)PORT_ArenaZAlloc(arena,
                                                          sizeof(NSSLOWKEYPublicKey));
            if (pubk != NULL) {
                SECStatus rv;

                pubk->arena = arena;
                pubk->keyType = privk->keyType;
                /* if the public key value doesn't exist, calculate it */
                if (privk->u.dsa.publicValue.len == 0) {
                    rv = DH_Derive(&privk->u.dsa.params.base, &privk->u.dsa.params.prime,
                                   &privk->u.dsa.privateValue, &publicValue, 0);
                    if (rv != SECSuccess) {
                        break;
                    }
                    rv = SECITEM_CopyItem(privk->arena, &privk->u.dsa.publicValue, &publicValue);
                    SECITEM_ZfreeItem(&publicValue, PR_FALSE);
                    if (rv != SECSuccess) {
                        break;
                    }
                }
                rv = SECITEM_CopyItem(arena, &pubk->u.dsa.publicValue,
                                      &privk->u.dsa.publicValue);
                if (rv != SECSuccess)
                    break;
                rv = SECITEM_CopyItem(arena, &pubk->u.dsa.params.prime,
                                      &privk->u.dsa.params.prime);
                if (rv != SECSuccess)
                    break;
                rv = SECITEM_CopyItem(arena, &pubk->u.dsa.params.subPrime,
                                      &privk->u.dsa.params.subPrime);
                if (rv != SECSuccess)
                    break;
                rv = SECITEM_CopyItem(arena, &pubk->u.dsa.params.base,
                                      &privk->u.dsa.params.base);
                if (rv == SECSuccess)
                    return pubk;
            }
            break;
        case NSSLOWKEYDHKey:
            pubk = (NSSLOWKEYPublicKey *)PORT_ArenaZAlloc(arena,
                                                          sizeof(NSSLOWKEYPublicKey));
            if (pubk != NULL) {
                SECStatus rv;

                pubk->arena = arena;
                pubk->keyType = privk->keyType;
                /* if the public key value doesn't exist, calculate it */
                if (privk->u.dh.publicValue.len == 0) {
                    rv = DH_Derive(&privk->u.dh.base, &privk->u.dh.prime,
                                   &privk->u.dh.privateValue, &publicValue, 0);
                    if (rv != SECSuccess) {
                        break;
                    }
                    rv = SECITEM_CopyItem(privk->arena, &privk->u.dh.publicValue, &publicValue);
                    SECITEM_ZfreeItem(&publicValue, PR_FALSE);
                    if (rv != SECSuccess) {
                        break;
                    }
                }
                rv = SECITEM_CopyItem(arena, &pubk->u.dh.publicValue,
                                      &privk->u.dh.publicValue);
                if (rv != SECSuccess)
                    break;
                rv = SECITEM_CopyItem(arena, &pubk->u.dh.prime,
                                      &privk->u.dh.prime);
                if (rv != SECSuccess)
                    break;
                rv = SECITEM_CopyItem(arena, &pubk->u.dh.base,
                                      &privk->u.dh.base);
                if (rv == SECSuccess)
                    return pubk;
            }
            break;
        case NSSLOWKEYECKey:
        case NSSLOWKEYECEdwardsKey:
        case NSSLOWKEYECMontgomeryKey:
            pubk = (NSSLOWKEYPublicKey *)PORT_ArenaZAlloc(arena,
                                                          sizeof(NSSLOWKEYPublicKey));
            if (pubk != NULL) {
                SECStatus rv;

                pubk->arena = arena;
                pubk->keyType = privk->keyType;

                /* if the public key value doesn't exist, calculate it */
                if (privk->u.ec.publicValue.len == 0) {
                    /* Checking if it's an ed25519 or x25519 key.
                       If it's the case, we derive the public key using the private key. */
                    if (privk->u.ec.ecParams.name == ECCurve_Ed25519) {
                        if (SECITEM_AllocItem(arena, &publicValue, Ed25519_PUBLIC_KEYLEN) == NULL) {
                            break;
                        }

                        rv = ED_DerivePublicKey(&privk->u.ec.privateValue, &publicValue);
                        if (rv != SECSuccess) {
                            break;
                        }
                        rv = SECITEM_CopyItem(privk->arena, &privk->u.ec.publicValue, &publicValue);
                        if (rv != SECSuccess) {
                            break;
                        }
                    } else if (privk->u.ec.ecParams.name == ECCurve25519) {
                        if (SECITEM_AllocItem(arena, &publicValue, X25519_PUBLIC_KEYLEN) == NULL) {
                            break;
                        }

                        rv = X25519_DerivePublicKey(&privk->u.ec.privateValue, &publicValue);
                        if (rv != SECSuccess) {
                            break;
                        }
                        rv = SECITEM_CopyItem(privk->arena, &privk->u.ec.publicValue, &publicValue);
                        if (rv != SECSuccess) {
                            break;
                        }
                    }
                }

                rv = SECITEM_CopyItem(arena, &pubk->u.ec.publicValue,
                                      &privk->u.ec.publicValue);
                if (rv != SECSuccess)
                    break;
                pubk->u.ec.ecParams.arena = arena;
                /* Copy the rest of the params */
                rv = EC_CopyParams(arena, &(pubk->u.ec.ecParams),
                                   &(privk->u.ec.ecParams));
                if (rv == SECSuccess)
                    return pubk;
            }
            break;
        case NSSLOWKEYMLDSAKey:
            pubk = (NSSLOWKEYPublicKey *)PORT_ArenaZAlloc(arena,
                                                          sizeof(NSSLOWKEYPublicKey));
            if (pubk != NULL) {
                SECStatus rv;
                SECItem seed = { siBuffer, NULL, 0 };
                MLDSAPrivateKey newPrivKey;

                pubk->arena = arena;
                pubk->keyType = privk->keyType;

                /* privatekey value is encoded (rho, K, tr, s1, s2, t0) */
                /* publickey value is encoded (rho, t1) */
                /* Future, we can calculate public key directly from
                 * privatekey value as follows :
                 * A^ = ExpandA(rho);
                 * t = NTT-1(A^ o NSS(s1)) + s2
                 * (t1, t0) = Power2Round(t)
                 * we now have rho and t1 so we can encode public key.
                 * these functions are all specified in FIPS-204. For now
                 * we just use the seed if it's a available and regenerate
                 * both keys, and discard the private key. */
                if (privk->u.mldsa.seedLen == 0) {
                    PORT_SetError(SEC_ERROR_PKCS11_FUNCTION_FAILED);
                    rv = SECFailure;
                    break;
                }
                seed.data = privk->u.mldsa.seed;
                seed.len = privk->u.mldsa.seedLen;
                rv = MLDSA_NewKey(privk->u.mldsa.paramSet, &seed,
                                  &newPrivKey, &pubk->u.mldsa);
                if (rv != SECSuccess) {
                    break;
                }
                PORT_SafeZero(&newPrivKey, sizeof(newPrivKey));
                return pubk;
            }
            break;
        case NSSLOWKEYMLKEMKey:
            pubk = (NSSLOWKEYPublicKey *)PORT_ArenaZAlloc(arena,
                                                          sizeof(NSSLOWKEYPublicKey));
            if (pubk != NULL) {
                size_t pubKeyLen;
                SECItem *item = NULL;

                pubk->arena = arena;
                pubk->keyType = privk->keyType;
                pubk->u.mlkem.mlkemParams = privk->u.mlkem.mlkemParams;
                /* privatekey value is encoded (dPKE||ePKE||H(ePKE)||z) */
                /* publickey value is encoded (ePKE) */
                /* size(dPKE) = 384k and size(ePKE)=384k+32,
                 * so size(dPKE) = size(ePKE)-32 */
                pubKeyLen = sftk_kyber_pubKeyLen(pubk->u.mlkem.mlkemParams);
                if (privk->u.mlkem.key.len < 2 * pubKeyLen) {
                    PORT_SetError(SEC_ERROR_BAD_KEY);
                    break;
                }
                item = SECITEM_AllocItem(arena, &pubk->u.mlkem.key, (int)pubKeyLen);
                if (item == NULL) {
                    break;
                }
                PORT_Memcpy(pubk->u.mlkem.key.data,
                            privk->u.mlkem.key.data + pubKeyLen - 32,
                            pubKeyLen);
                return pubk;
            }
            break;
        /* No Fortezza in Low Key implementations (Fortezza keys aren't
         * stored in our data base */
        default:
            break;
    }

    PORT_FreeArena(arena, PR_TRUE);
    return NULL;
}

NSSLOWKEYPrivateKey *
nsslowkey_CopyPrivateKey(NSSLOWKEYPrivateKey *privKey)
{
    NSSLOWKEYPrivateKey *returnKey = NULL;
    SECStatus rv = SECFailure;
    PLArenaPool *poolp;

    if (!privKey) {
        return NULL;
    }

    poolp = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (!poolp) {
        return NULL;
    }

    returnKey = (NSSLOWKEYPrivateKey *)PORT_ArenaZAlloc(poolp, sizeof(NSSLOWKEYPrivateKey));
    if (!returnKey) {
        rv = SECFailure;
        goto loser;
    }

    returnKey->keyType = privKey->keyType;
    returnKey->arena = poolp;

    switch (privKey->keyType) {
        case NSSLOWKEYRSAKey:
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.rsa.modulus),
                                  &(privKey->u.rsa.modulus));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.rsa.version),
                                  &(privKey->u.rsa.version));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.rsa.publicExponent),
                                  &(privKey->u.rsa.publicExponent));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.rsa.privateExponent),
                                  &(privKey->u.rsa.privateExponent));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.rsa.prime1),
                                  &(privKey->u.rsa.prime1));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.rsa.prime2),
                                  &(privKey->u.rsa.prime2));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.rsa.exponent1),
                                  &(privKey->u.rsa.exponent1));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.rsa.exponent2),
                                  &(privKey->u.rsa.exponent2));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.rsa.coefficient),
                                  &(privKey->u.rsa.coefficient));
            if (rv != SECSuccess)
                break;
            break;
        case NSSLOWKEYDSAKey:
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.dsa.publicValue),
                                  &(privKey->u.dsa.publicValue));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.dsa.privateValue),
                                  &(privKey->u.dsa.privateValue));
            if (rv != SECSuccess)
                break;
            returnKey->u.dsa.params.arena = poolp;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.dsa.params.prime),
                                  &(privKey->u.dsa.params.prime));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.dsa.params.subPrime),
                                  &(privKey->u.dsa.params.subPrime));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.dsa.params.base),
                                  &(privKey->u.dsa.params.base));
            if (rv != SECSuccess)
                break;
            break;
        case NSSLOWKEYDHKey:
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.dh.publicValue),
                                  &(privKey->u.dh.publicValue));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.dh.privateValue),
                                  &(privKey->u.dh.privateValue));
            if (rv != SECSuccess)
                break;
            returnKey->u.dsa.params.arena = poolp;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.dh.prime),
                                  &(privKey->u.dh.prime));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.dh.base),
                                  &(privKey->u.dh.base));
            if (rv != SECSuccess)
                break;
            break;
        case NSSLOWKEYECKey:
        case NSSLOWKEYECEdwardsKey:
        case NSSLOWKEYECMontgomeryKey:
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.ec.version),
                                  &(privKey->u.ec.version));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.ec.publicValue),
                                  &(privKey->u.ec.publicValue));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.ec.privateValue),
                                  &(privKey->u.ec.privateValue));
            if (rv != SECSuccess)
                break;
            returnKey->u.ec.ecParams.arena = poolp;
            /* Copy the rest of the params */
            rv = EC_CopyParams(poolp, &(returnKey->u.ec.ecParams),
                               &(privKey->u.ec.ecParams));
            if (rv != SECSuccess)
                break;
            break;
        case NSSLOWKEYMLDSAKey:
            returnKey->u.mldsa = privKey->u.mldsa;
            rv = SECSuccess;
            break;
        case NSSLOWKEYMLKEMKey:
            returnKey->u.mlkem.mlkemParams = privKey->u.mlkem.mlkemParams;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.mlkem.key),
                                  &(privKey->u.mlkem.key));
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(poolp, &(returnKey->u.mlkem.seed),
                                  &(privKey->u.mlkem.seed));
            if (rv != SECSuccess)
                break;
            break;
        default:
            rv = SECFailure;
    }

loser:

    if (rv != SECSuccess) {
        PORT_FreeArena(poolp, PR_TRUE);
        returnKey = NULL;
    }

    return returnKey;
}
