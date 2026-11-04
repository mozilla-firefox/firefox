/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "cryptohi.h"
#include "keyhi.h"
#include "pkcs11t.h"
#include "secoid.h"
#include "eccutil.h"
#include "secitem.h"
#include "secder.h"
#include "base64.h"
#include "secasn1.h"
#include "cert.h"
#include "pk11func.h"
#include "secerr.h"
#include "secdig.h"
#include "prtime.h"
#include "keyi.h"
#include "nss.h"

SEC_ASN1_MKSUB(SECOID_AlgorithmIDTemplate)
SEC_ASN1_MKSUB(SEC_IntegerTemplate)

const SEC_ASN1Template CERT_SubjectPublicKeyInfoTemplate[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(CERTSubjectPublicKeyInfo) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(CERTSubjectPublicKeyInfo, algorithm),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_BIT_STRING,
      offsetof(CERTSubjectPublicKeyInfo, subjectPublicKey) },
    { 0 }
};

const SEC_ASN1Template CERT_PublicKeyAndChallengeTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(CERTPublicKeyAndChallenge) },
    { SEC_ASN1_ANY, offsetof(CERTPublicKeyAndChallenge, spki) },
    { SEC_ASN1_IA5_STRING, offsetof(CERTPublicKeyAndChallenge, challenge) },
    { 0 }
};

const SEC_ASN1Template SECKEY_RSAPublicKeyTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(SECKEYPublicKey) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYPublicKey, u.rsa.modulus) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYPublicKey, u.rsa.publicExponent) },
    { 0 }
};

static const SEC_ASN1Template seckey_PointerToAlgorithmIDTemplate[] = {
    { SEC_ASN1_POINTER | SEC_ASN1_XTRN, 0,
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) }
};

/* Parameters for SEC_OID_PKCS1_RSA_PSS_SIGNATURE */
const SEC_ASN1Template SECKEY_RSAPSSParamsTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(SECKEYRSAPSSParams) },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED | SEC_ASN1_EXPLICIT |
          SEC_ASN1_CONTEXT_SPECIFIC | 0,
      offsetof(SECKEYRSAPSSParams, hashAlg),
      seckey_PointerToAlgorithmIDTemplate },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED | SEC_ASN1_EXPLICIT |
          SEC_ASN1_CONTEXT_SPECIFIC | 1,
      offsetof(SECKEYRSAPSSParams, maskAlg),
      seckey_PointerToAlgorithmIDTemplate },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED | SEC_ASN1_EXPLICIT |
          SEC_ASN1_XTRN | SEC_ASN1_CONTEXT_SPECIFIC | 2,
      offsetof(SECKEYRSAPSSParams, saltLength),
      SEC_ASN1_SUB(SEC_IntegerTemplate) },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED | SEC_ASN1_EXPLICIT |
          SEC_ASN1_XTRN | SEC_ASN1_CONTEXT_SPECIFIC | 3,
      offsetof(SECKEYRSAPSSParams, trailerField),
      SEC_ASN1_SUB(SEC_IntegerTemplate) },
    { 0 }
};

const SEC_ASN1Template SECKEY_DSAPublicKeyTemplate[] = {
    { SEC_ASN1_INTEGER, offsetof(SECKEYPublicKey, u.dsa.publicValue) },
    { 0 }
};

const SEC_ASN1Template SECKEY_PQGParamsTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(SECKEYPQGParams) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYPQGParams, prime) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYPQGParams, subPrime) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYPQGParams, base) },
    { 0 }
};

const SEC_ASN1Template SECKEY_DHPublicKeyTemplate[] = {
    { SEC_ASN1_INTEGER, offsetof(SECKEYPublicKey, u.dh.publicValue) },
    { 0 }
};

const SEC_ASN1Template SECKEY_DHParamKeyTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(SECKEYPublicKey) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYPublicKey, u.dh.prime) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYPublicKey, u.dh.base) },
    /* XXX chrisk: this needs to be expanded for decoding of j and validationParms (RFC2459 7.3.2) */
    { SEC_ASN1_SKIP_REST },
    { 0 }
};

SEC_ASN1_CHOOSER_IMPLEMENT(SECKEY_DSAPublicKeyTemplate)
SEC_ASN1_CHOOSER_IMPLEMENT(SECKEY_RSAPublicKeyTemplate)
SEC_ASN1_CHOOSER_IMPLEMENT(SECKEY_RSAPSSParamsTemplate)
SEC_ASN1_CHOOSER_IMPLEMENT(CERT_SubjectPublicKeyInfoTemplate)

/*
 * See bugzilla bug 125359
 * Since NSS (via PKCS#11) wants to handle big integers as unsigned ints,
 * all of the templates above that en/decode into integers must be converted
 * from ASN.1's signed integer type.  This is done by marking either the
 * source or destination (encoding or decoding, respectively) type as
 * siUnsignedInteger.
 */
static void
prepare_rsa_pub_key_for_asn1(SECKEYPublicKey *pubk)
{
    pubk->u.rsa.modulus.type = siUnsignedInteger;
    pubk->u.rsa.publicExponent.type = siUnsignedInteger;
}

static void
prepare_dsa_pub_key_for_asn1(SECKEYPublicKey *pubk)
{
    pubk->u.dsa.publicValue.type = siUnsignedInteger;
}

static void
prepare_pqg_params_for_asn1(SECKEYPQGParams *params)
{
    params->prime.type = siUnsignedInteger;
    params->subPrime.type = siUnsignedInteger;
    params->base.type = siUnsignedInteger;
}

static void
prepare_dh_pub_key_for_asn1(SECKEYPublicKey *pubk)
{
    pubk->u.dh.prime.type = siUnsignedInteger;
    pubk->u.dh.base.type = siUnsignedInteger;
    pubk->u.dh.publicValue.type = siUnsignedInteger;
}

static const char *const keyTypeName[] = {
    "null", "rsa", "dsa", "fortezza", "dh", "kea", "ec", "rsaPss", "rsaOaep",
    "mlkem", "ed", "ecMont", "mldsa"
};
static size_t keyTypeNameMax = PR_ARRAY_SIZE(keyTypeName);

const char *
SECKEY_GetKeyTypeString(KeyType keyType)
{
    if (keyType < keyTypeNameMax) {
        return keyTypeName[keyType];
    }
    return "unknown";
}

/* Create an RSA key pair is any slot able to do so.
** The created keys are "session" (temporary), not "token" (permanent),
** and they are "sensitive", which makes them costly to move to another token.
*/
SECKEYPrivateKey *
SECKEY_CreateRSAPrivateKey(int keySizeInBits, SECKEYPublicKey **pubk, void *cx)
{
    SECKEYPrivateKey *privk;
    PK11RSAGenParams param;
    PK11SlotInfo *slot = PK11_GetBestSlot(CKM_RSA_PKCS_KEY_PAIR_GEN, cx);
    if (!slot) {
        return NULL;
    }

    param.keySizeInBits = keySizeInBits;
    param.pe = 65537L;

    privk = PK11_GenerateKeyPair(slot, CKM_RSA_PKCS_KEY_PAIR_GEN, &param, pubk,
                                 PR_FALSE, PR_TRUE, cx);
    PK11_FreeSlot(slot);
    return (privk);
}

/* Create a DH key pair in any slot able to do so,
** This is a "session" (temporary), not "token" (permanent) key.
** Because of the high probability that this key will need to be moved to
** another token, and the high cost of moving "sensitive" keys, we attempt
** to create this key pair without the "sensitive" attribute, but revert to
** creating a "sensitive" key if necessary.
*/
SECKEYPrivateKey *
SECKEY_CreateDHPrivateKey(SECKEYDHParams *param, SECKEYPublicKey **pubk, void *cx)
{
    SECKEYPrivateKey *privk;
    PK11SlotInfo *slot;

    if (!param || !param->base.data || !param->prime.data ||
        SECKEY_BigIntegerBitLength(&param->prime) < DH_MIN_P_BITS ||
        param->base.len == 0 || param->base.len > param->prime.len + 1 ||
        (param->base.len == 1 && param->base.data[0] == 0)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    slot = PK11_GetBestSlot(CKM_DH_PKCS_KEY_PAIR_GEN, cx);
    if (!slot) {
        return NULL;
    }

    privk = PK11_GenerateKeyPair(slot, CKM_DH_PKCS_KEY_PAIR_GEN, param,
                                 pubk, PR_FALSE, PR_FALSE, cx);
    if (!privk)
        privk = PK11_GenerateKeyPair(slot, CKM_DH_PKCS_KEY_PAIR_GEN, param,
                                     pubk, PR_FALSE, PR_TRUE, cx);

    PK11_FreeSlot(slot);
    return (privk);
}

/* Create an EC key pair in any slot able to do so,
** This is a "session" (temporary), not "token" (permanent) key.
** Because of the high probability that this key will need to be moved to
** another token, and the high cost of moving "sensitive" keys, we attempt
** to create this key pair without the "sensitive" attribute, but revert to
** creating a "sensitive" key if necessary.
*/
SECKEYPrivateKey *
SECKEY_CreateECPrivateKey(SECKEYECParams *param, SECKEYPublicKey **pubk, void *cx)
{
    SECKEYPrivateKey *privk;
    PK11SlotInfo *slot = PK11_GetBestSlot(CKM_EC_KEY_PAIR_GEN, cx);
    if (!slot) {
        return NULL;
    }

    privk = PK11_GenerateKeyPairWithOpFlags(slot, CKM_EC_KEY_PAIR_GEN,
                                            param, pubk,
                                            PK11_ATTR_SESSION |
                                                PK11_ATTR_INSENSITIVE |
                                                PK11_ATTR_PUBLIC,
                                            CKF_DERIVE, CKF_DERIVE | CKF_SIGN,
                                            cx);
    if (!privk)
        privk = PK11_GenerateKeyPairWithOpFlags(slot, CKM_EC_KEY_PAIR_GEN,
                                                param, pubk,
                                                PK11_ATTR_SESSION |
                                                    PK11_ATTR_SENSITIVE |
                                                    PK11_ATTR_PRIVATE,
                                                CKF_DERIVE, CKF_DERIVE | CKF_SIGN,
                                                cx);

    PK11_FreeSlot(slot);
    return (privk);
}

void
SECKEY_DestroyPrivateKey(SECKEYPrivateKey *privk)
{
    if (privk) {
        if (privk->pkcs11Slot) {
            if (SECKEYPRIVATEKEY_IS_OWNED(privk)) {
                PK11_DestroyObject(privk->pkcs11Slot, privk->pkcs11ID);
            }
            PK11_FreeSlot(privk->pkcs11Slot);
        }
        if (privk->arena) {
            PORT_FreeArena(privk->arena, PR_TRUE);
        }
    }
}

void
SECKEY_DestroyPublicKey(SECKEYPublicKey *pubk)
{
    if (pubk) {
        if (pubk->pkcs11Slot) {
            if (!PK11_IsPermObject(pubk->pkcs11Slot, pubk->pkcs11ID)) {
                PK11_DestroyObject(pubk->pkcs11Slot, pubk->pkcs11ID);
            }
            PK11_FreeSlot(pubk->pkcs11Slot);
        }
        if (pubk->arena) {
            PORT_FreeArena(pubk->arena, PR_FALSE);
        }
    }
}

SECStatus
SECKEY_CopySubjectPublicKeyInfo(PLArenaPool *arena,
                                CERTSubjectPublicKeyInfo *to,
                                CERTSubjectPublicKeyInfo *from)
{
    SECStatus rv;
    SECItem spk;

    rv = SECOID_CopyAlgorithmID(arena, &to->algorithm, &from->algorithm);
    if (rv == SECSuccess) {
        /*
         * subjectPublicKey is a bit string, whose length is in bits.
         * Convert the length from bits to bytes for SECITEM_CopyItem.
         */
        spk = from->subjectPublicKey;
        DER_ConvertBitString(&spk);
        rv = SECITEM_CopyItem(arena, &to->subjectPublicKey, &spk);
        /* Set the length back to bits. */
        if (rv == SECSuccess) {
            to->subjectPublicKey.len = from->subjectPublicKey.len;
        }
    }

    return rv;
}

/* Procedure to update the pqg parameters for a cert's public key.
 * pqg parameters only need to be updated for DSA certificates.
 * The procedure uses calls to itself recursively to update a certificate
 * issuer's pqg parameters.  Some important rules are:
 *    - Do nothing if the cert already has PQG parameters.
 *    - If the cert does not have PQG parameters, obtain them from the issuer.
 *    - A valid cert chain cannot have a DSA cert without
 *      pqg parameters that has a parent that is not a DSA cert.  */

static SECStatus
seckey_UpdateCertPQGChain(CERTCertificate *subjectCert, int count)
{
    SECStatus rv;
    SECOidData *oid = NULL;
    int tag;
    CERTSubjectPublicKeyInfo *subjectSpki = NULL;
    CERTSubjectPublicKeyInfo *issuerSpki = NULL;
    CERTCertificate *issuerCert = NULL;

    /* increment cert chain length counter*/
    count++;

    /* check if cert chain length exceeds the maximum length*/
    if (count > CERT_MAX_CERT_CHAIN) {
        return SECFailure;
    }

    oid = SECOID_FindOID(&subjectCert->subjectPublicKeyInfo.algorithm.algorithm);
    if (oid != NULL) {
        tag = oid->offset;

        /* Check if cert has a DSA or EC public key. If not, return
         * success since no PQG params need to be updated.
         *
         * Question: do we really need to do this for EC keys. They don't have
         * PQG parameters, but they do have parameters. The question is does
         * the child cert inherit those parameters for EC from the parent, or
         * do we always include those parameters in each cert.
         */

        if ((tag != SEC_OID_ANSIX9_DSA_SIGNATURE) &&
            (tag != SEC_OID_ANSIX9_DSA_SIGNATURE_WITH_SHA1_DIGEST) &&
            (tag != SEC_OID_NIST_DSA_SIGNATURE_WITH_SHA224_DIGEST) &&
            (tag != SEC_OID_NIST_DSA_SIGNATURE_WITH_SHA256_DIGEST) &&
            (tag != SEC_OID_BOGUS_DSA_SIGNATURE_WITH_SHA1_DIGEST) &&
            (tag != SEC_OID_SDN702_DSA_SIGNATURE) &&
            (tag != SEC_OID_ED25519_PUBLIC_KEY) &&
            (tag != SEC_OID_ANSIX962_EC_PUBLIC_KEY)) {

            return SECSuccess;
        }
    } else {
        return SECFailure; /* return failure if oid is NULL */
    }

    /* if cert has PQG parameters, return success */

    subjectSpki = &subjectCert->subjectPublicKeyInfo;

    if (subjectSpki->algorithm.parameters.len != 0) {
        return SECSuccess;
    }

    /* check if the cert is self-signed */
    if (subjectCert->isRoot) {
        /* fail since cert is self-signed and has no pqg params. */
        return SECFailure;
    }

    /* get issuer cert */
    issuerCert = CERT_FindCertIssuer(subjectCert, PR_Now(), certUsageAnyCA);
    if (!issuerCert) {
        return SECFailure;
    }

    /* if parent is not DSA, return failure since
       we don't allow this case. */

    oid = SECOID_FindOID(&issuerCert->subjectPublicKeyInfo.algorithm.algorithm);
    if (oid != NULL) {
        tag = oid->offset;

        /* Check if issuer cert has a DSA public key. If not,
         * return failure.   */

        if ((tag != SEC_OID_ANSIX9_DSA_SIGNATURE) &&
            (tag != SEC_OID_ANSIX9_DSA_SIGNATURE_WITH_SHA1_DIGEST) &&
            (tag != SEC_OID_NIST_DSA_SIGNATURE_WITH_SHA224_DIGEST) &&
            (tag != SEC_OID_NIST_DSA_SIGNATURE_WITH_SHA256_DIGEST) &&
            (tag != SEC_OID_BOGUS_DSA_SIGNATURE_WITH_SHA1_DIGEST) &&
            (tag != SEC_OID_SDN702_DSA_SIGNATURE) &&
            (tag != SEC_OID_ED25519_PUBLIC_KEY) &&
            (tag != SEC_OID_ANSIX962_EC_PUBLIC_KEY)) {
            rv = SECFailure;
            goto loser;
        }
    } else {
        rv = SECFailure; /* return failure if oid is NULL */
        goto loser;
    }

    /* at this point the subject cert has no pqg parameters and the
     * issuer cert has a DSA public key.  Update the issuer's
     * pqg parameters with a recursive call to this same function. */

    rv = seckey_UpdateCertPQGChain(issuerCert, count);
    if (rv != SECSuccess) {
        rv = SECFailure;
        goto loser;
    }

    /* ensure issuer has pqg parameters */

    issuerSpki = &issuerCert->subjectPublicKeyInfo;
    if (issuerSpki->algorithm.parameters.len == 0) {
        rv = SECFailure;
    }

    /* if update was successful and pqg params present, then copy the
     * parameters to the subject cert's key. */

    if (rv == SECSuccess) {
        rv = SECITEM_CopyItem(subjectCert->arena,
                              &subjectSpki->algorithm.parameters,
                              &issuerSpki->algorithm.parameters);
    }

loser:
    if (issuerCert) {
        CERT_DestroyCertificate(issuerCert);
    }
    return rv;
}

SECStatus
SECKEY_UpdateCertPQG(CERTCertificate *subjectCert)
{
    if (!subjectCert) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    return seckey_UpdateCertPQGChain(subjectCert, 0);
}

/* Decode the DSA PQG parameters.  The params could be stored in two
 * possible formats, the old fortezza-only wrapped format or
 * the normal standard format.  Store the decoded parameters in
 * a V3 certificate data structure.  */

static SECStatus
seckey_DSADecodePQG(PLArenaPool *arena, SECKEYPublicKey *pubk,
                    const SECItem *params)
{
    SECStatus rv;
    SECItem newparams;

    if (params == NULL) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    /* Absent parameters, e.g. inherited from the issuer (RFC 3279). */
    if (params->data == NULL) {
        PORT_SetError(SEC_ERROR_INPUT_LEN);
        return SECFailure;
    }

    PORT_Assert(arena);

    /* make a copy of the data into the arena so QuickDER output is valid */
    rv = SECITEM_CopyItem(arena, &newparams, params);

    /* Check if params use the standard format.
     * The value 0xa1 will appear in the first byte of the parameter data
     * if the PQG parameters are not using the standard format.  This
     * code should be changed to use a better method to detect non-standard
     * parameters.    */

    if ((newparams.data[0] != 0xa1) &&
        (newparams.data[0] != 0xa0)) {

        if (SECSuccess == rv) {
            /* PQG params are in the standard format */
            prepare_pqg_params_for_asn1(&pubk->u.dsa.params);
            rv = SEC_QuickDERDecodeItem(arena, &pubk->u.dsa.params,
                                        SECKEY_PQGParamsTemplate,
                                        &newparams);
        }
    } else {

        if (SECSuccess == rv) {
            /* else the old fortezza-only wrapped format is used. */
            PORT_SetError(SEC_ERROR_BAD_DER);
            rv = SECFailure;
        }
    }
    return rv;
}

/* Function used to make an oid tag to a key type */
KeyType
seckey_GetKeyType(SECOidTag tag)
{
    KeyType keyType;

    switch (tag) {
        case SEC_OID_X500_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_RSA_ENCRYPTION:
            keyType = rsaKey;
            break;
        case SEC_OID_PKCS1_RSA_PSS_SIGNATURE:
            keyType = rsaPssKey;
            break;
        case SEC_OID_PKCS1_RSA_OAEP_ENCRYPTION:
            keyType = rsaOaepKey;
            break;
        case SEC_OID_ANSIX9_DSA_SIGNATURE:
            keyType = dsaKey;
            break;
        case SEC_OID_MISSI_KEA_DSS_OLD:
        case SEC_OID_MISSI_KEA_DSS:
        case SEC_OID_MISSI_DSS_OLD:
        case SEC_OID_MISSI_DSS:
            keyType = fortezzaKey;
            break;
        case SEC_OID_MISSI_KEA:
        case SEC_OID_MISSI_ALT_KEA:
            keyType = keaKey;
            break;
        case SEC_OID_X942_DIFFIE_HELMAN_KEY:
            keyType = dhKey;
            break;
        case SEC_OID_ANSIX962_EC_PUBLIC_KEY:
            keyType = ecKey;
            break;
        case SEC_OID_ED25519_PUBLIC_KEY:
            keyType = edKey;
            break;
        case SEC_OID_ML_DSA_44_PUBLIC_KEY:
        case SEC_OID_ML_DSA_65_PUBLIC_KEY:
        case SEC_OID_ML_DSA_87_PUBLIC_KEY:
            keyType = mldsaKey;
            break;
        case SEC_OID_ML_KEM_512:
        case SEC_OID_ML_KEM_768:
        case SEC_OID_ML_KEM_1024:
            keyType = kyberKey;
            break;
        /* accommodate applications that hand us a signature type when they
         * should be handing us a cipher type */
        case SEC_OID_PKCS1_MD5_WITH_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_SHA1_WITH_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_SHA224_WITH_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_SHA256_WITH_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_SHA384_WITH_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_SHA512_WITH_RSA_ENCRYPTION:
            keyType = rsaKey;
            break;
        default:
            keyType = nullKey;
    }
    return keyType;
}

/* Function used to determine what kind of cert we are dealing with. */
KeyType
CERT_GetCertKeyType(const CERTSubjectPublicKeyInfo *spki)
{
    return seckey_GetKeyType(SECOID_GetAlgorithmTag(&spki->algorithm));
}

/* Ensure pubKey contains an OID */
static SECStatus
seckey_HasCurveOID(const SECKEYPublicKey *pubKey)
{
    SECItem oid;
    SECStatus rv;
    PORTCheapArenaPool tmpArena;

    PORT_InitCheapArena(&tmpArena, DER_DEFAULT_CHUNKSIZE);
    /* If we can decode it, an OID is available. */
    rv = SEC_QuickDERDecodeItem(&tmpArena.arena, &oid,
                                SEC_ASN1_GET(SEC_ObjectIDTemplate),
                                &pubKey->u.ec.DEREncodedParams);
    PORT_DestroyCheapArena(&tmpArena);
    return rv;
}

CK_ML_DSA_PARAMETER_SET_TYPE
SECKEY_GetMLDSAPkcs11ParamSetByOidTag(SECOidTag tag)
{
    switch (tag) {
        case SEC_OID_ML_DSA_44:
            return CKP_ML_DSA_44;
        case SEC_OID_ML_DSA_65:
            return CKP_ML_DSA_65;
        case SEC_OID_ML_DSA_87:
            return CKP_ML_DSA_87;
        default:
            return CKP_INVALID_ID;
    }
}

SECOidTag
SECKEY_GetMLDSAOidTagByPkcs11ParamSet(CK_ML_DSA_PARAMETER_SET_TYPE paramSet)
{
    switch (paramSet) {
        case CKP_ML_DSA_44:
            return SEC_OID_ML_DSA_44;
        case CKP_ML_DSA_65:
            return SEC_OID_ML_DSA_65;
        case CKP_ML_DSA_87:
            return SEC_OID_ML_DSA_87;
        default:
            return SEC_OID_UNKNOWN;
    }
}

unsigned int
SECKEY_MLDSAOidParamsToLen(SECOidTag oid, SECKEYSizeType type)
{
    switch (type) {
        case SECKEYPubKeyType:
            switch (oid) {
                case SEC_OID_ML_DSA_44:
                    return ML_DSA_44_PUBLICKEY_LEN;
                case SEC_OID_ML_DSA_65:
                    return ML_DSA_65_PUBLICKEY_LEN;
                case SEC_OID_ML_DSA_87:
                    return ML_DSA_87_PUBLICKEY_LEN;
                default:
                    break;
            }
            break;
        case SECKEYPrivKeyType:
            switch (oid) {
                case SEC_OID_ML_DSA_44:
                    return ML_DSA_44_PRIVATEKEY_LEN;
                case SEC_OID_ML_DSA_65:
                    return ML_DSA_65_PRIVATEKEY_LEN;
                case SEC_OID_ML_DSA_87:
                    return ML_DSA_87_PRIVATEKEY_LEN;
                default:
                    break;
            }
            break;
        case SECKEYSignatureType:
            switch (oid) {
                case SEC_OID_ML_DSA_44:
                    return ML_DSA_44_SIGNATURE_LEN;
                case SEC_OID_ML_DSA_65:
                    return ML_DSA_65_SIGNATURE_LEN;
                case SEC_OID_ML_DSA_87:
                    return ML_DSA_87_SIGNATURE_LEN;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    return 0;
}

SECOidTag
SECKEY_MLDSAOidParamsFromLen(unsigned int len, SECKEYSizeType type)
{
    switch (type) {
        case SECKEYPubKeyType:
            switch (len) {
                case ML_DSA_44_PUBLICKEY_LEN:
                    return SEC_OID_ML_DSA_44;
                case ML_DSA_65_PUBLICKEY_LEN:
                    return SEC_OID_ML_DSA_65;
                case ML_DSA_87_PUBLICKEY_LEN:
                    return SEC_OID_ML_DSA_87;
                default:
                    break;
            }
            break;
        case SECKEYPrivKeyType:
            switch (len) {
                case ML_DSA_44_PRIVATEKEY_LEN:
                    return SEC_OID_ML_DSA_44;
                case ML_DSA_65_PRIVATEKEY_LEN:
                    return SEC_OID_ML_DSA_65;
                case ML_DSA_87_PRIVATEKEY_LEN:
                    return SEC_OID_ML_DSA_87;
                default:
                    break;
            }
            break;
        case SECKEYSignatureType:
            switch (len) {
                case ML_DSA_44_SIGNATURE_LEN:
                    return SEC_OID_ML_DSA_44;
                case ML_DSA_65_SIGNATURE_LEN:
                    return SEC_OID_ML_DSA_65;
                case ML_DSA_87_SIGNATURE_LEN:
                    return SEC_OID_ML_DSA_87;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    return SEC_OID_UNKNOWN;
}

KyberParams
seckey_GetKyberParamsByOidTag(SECOidTag tag)
{
    switch (tag) {
        case SEC_OID_ML_KEM_512:
            return params_ml_kem512;
        case SEC_OID_ML_KEM_768:
            return params_ml_kem768;
        case SEC_OID_ML_KEM_1024:
            return params_ml_kem1024;
        default:
            return params_kyber_invalid;
    }
}

KyberParams
seckey_GetKyberParamsByPkcs11ParamSet(CK_ML_KEM_PARAMETER_SET_TYPE paramSet)
{
    switch (paramSet) {
        case CKP_ML_KEM_512:
            return params_ml_kem512;
        case CKP_NSS_ML_KEM_768:
        case CKP_ML_KEM_768:
            return params_ml_kem768;
        case CKP_ML_KEM_1024:
            return params_ml_kem1024;
        default:
            return params_kyber_invalid;
    }
}

CK_ML_KEM_PARAMETER_SET_TYPE
seckey_GetMLKEMPkcs11ParamsByKyberParams(KyberParams kyberParams)
{
    switch (kyberParams) {
        case params_ml_kem512:
        case params_ml_kem512_test_mode:
            return CKP_ML_KEM_512;
        case params_ml_kem768:
        case params_ml_kem768_test_mode:
            return CKP_ML_KEM_768;
        case params_ml_kem1024:
        case params_ml_kem1024_test_mode:
            return CKP_ML_KEM_1024;
        default:
            return CKP_INVALID_ID;
    }
}

SECOidTag
seckey_GetMLKEMOidTagByPkcs11ParamSet(CK_ML_KEM_PARAMETER_SET_TYPE paramSet)
{
    switch (paramSet) {
        case CKP_ML_KEM_512:
            return SEC_OID_ML_KEM_512;
        case CKP_NSS_ML_KEM_768:
        case CKP_ML_KEM_768:
            return SEC_OID_ML_KEM_768;
        case CKP_ML_KEM_1024:
            return SEC_OID_ML_KEM_1024;
        default:
            return SEC_OID_UNKNOWN;
    }
}

unsigned int
seckey_KyberParamsToLen(KyberParams kyberParams, SECKEYSizeType type)
{
    switch (type) {
        case SECKEYPubKeyType:
            switch (kyberParams) {
                case params_ml_kem512:
                case params_ml_kem512_test_mode:
                    return MLKEM512_PUBLIC_KEY_BYTES;
                case params_ml_kem768:
                case params_ml_kem768_test_mode:
                    return KYBER768_PUBLIC_KEY_BYTES;
                case params_ml_kem1024:
                case params_ml_kem1024_test_mode:
                    return MLKEM1024_PUBLIC_KEY_BYTES;
                default:
                    break;
            }
            break;
        case SECKEYPrivKeyType:
            switch (kyberParams) {
                case params_ml_kem512:
                case params_ml_kem512_test_mode:
                    return MLKEM512_PRIVATE_KEY_BYTES;
                case params_ml_kem768:
                case params_ml_kem768_test_mode:
                    return KYBER768_PRIVATE_KEY_BYTES;
                case params_ml_kem1024:
                case params_ml_kem1024_test_mode:
                    return MLKEM1024_PRIVATE_KEY_BYTES;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    return 0;
}

KyberParams
seckey_KyberParamsFromLen(unsigned int len, SECKEYSizeType type)
{
    switch (type) {
        case SECKEYPubKeyType:
            switch (len) {
                case MLKEM512_PUBLIC_KEY_BYTES:
                    return params_ml_kem512;
                case KYBER768_PUBLIC_KEY_BYTES:
                    return params_ml_kem768;
                case MLKEM1024_PUBLIC_KEY_BYTES:
                    return params_ml_kem1024;
                default:
                    break;
            }
            break;
        case SECKEYPrivKeyType:
            switch (len) {
                case MLKEM512_PRIVATE_KEY_BYTES:
                    return params_ml_kem512;
                case KYBER768_PRIVATE_KEY_BYTES:
                    return params_ml_kem768;
                case MLKEM1024_PRIVATE_KEY_BYTES:
                    return params_ml_kem1024;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    return 0;
}

/* make this function generic. multiple key types will be able to use
 * it (ml-kem, ml=dsa, shl-dsa, fn-dsa, etc. ) */
SECOidTag
seckey_GetParameterSet(const SECKEYPrivateKey *key)
{
    CK_ULONG paramSet = PK11_ReadULongAttribute(key->pkcs11Slot,
                                                key->pkcs11ID,
                                                CKA_PARAMETER_SET);
    if (paramSet == CK_UNAVAILABLE_INFORMATION) {
        paramSet = PK11_ReadULongAttribute(key->pkcs11Slot,
                                           key->pkcs11ID,
                                           CKA_NSS_PARAMETER_SET);
        if (paramSet == CK_UNAVAILABLE_INFORMATION) {
            return SEC_OID_UNKNOWN;
        }
    }
    switch (key->keyType) {
        case mldsaKey:
            return SECKEY_GetMLDSAOidTagByPkcs11ParamSet(paramSet);
        case kyberKey:
            return seckey_GetMLKEMOidTagByPkcs11ParamSet(paramSet);
        default:
            break;
    }
    return SEC_OID_UNKNOWN;
}

static SECKEYPublicKey *
seckey_ExtractPublicKey(const CERTSubjectPublicKeyInfo *spki)
{
    SECKEYPublicKey *pubk;
    SECItem os, newOs, newParms;
    SECStatus rv;
    PLArenaPool *arena;
    SECOidTag tag;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL)
        return NULL;

    pubk = (SECKEYPublicKey *)PORT_ArenaZAlloc(arena, sizeof(SECKEYPublicKey));
    if (pubk == NULL) {
        PORT_FreeArena(arena, PR_FALSE);
        return NULL;
    }

    pubk->arena = arena;
    pubk->pkcs11Slot = 0;
    pubk->pkcs11ID = CK_INVALID_HANDLE;

    /* Convert bit string length from bits to bytes */
    os = spki->subjectPublicKey;
    DER_ConvertBitString(&os);

    tag = SECOID_GetAlgorithmTag(&spki->algorithm);

    /* copy the DER into the arena, since Quick DER returns data that points
       into the DER input, which may get freed by the caller */
    rv = SECITEM_CopyItem(arena, &newOs, &os);
    if (rv == SECSuccess)
        switch (tag) {
            case SEC_OID_X500_RSA_ENCRYPTION:
            case SEC_OID_PKCS1_RSA_ENCRYPTION:
            case SEC_OID_PKCS1_RSA_PSS_SIGNATURE:
                pubk->keyType = rsaKey;
                prepare_rsa_pub_key_for_asn1(pubk);
                rv = SEC_QuickDERDecodeItem(arena, pubk, SECKEY_RSAPublicKeyTemplate, &newOs);
                if (rv == SECSuccess)
                    return pubk;
                break;
            case SEC_OID_ANSIX9_DSA_SIGNATURE:
            case SEC_OID_SDN702_DSA_SIGNATURE:
                pubk->keyType = dsaKey;
                prepare_dsa_pub_key_for_asn1(pubk);
                rv = SEC_QuickDERDecodeItem(arena, pubk, SECKEY_DSAPublicKeyTemplate, &newOs);
                if (rv != SECSuccess)
                    break;

                rv = seckey_DSADecodePQG(arena, pubk,
                                         &spki->algorithm.parameters);

                if (rv == SECSuccess)
                    return pubk;
                break;
            case SEC_OID_X942_DIFFIE_HELMAN_KEY:
                pubk->keyType = dhKey;
                prepare_dh_pub_key_for_asn1(pubk);
                rv = SEC_QuickDERDecodeItem(arena, pubk, SECKEY_DHPublicKeyTemplate, &newOs);
                if (rv != SECSuccess)
                    break;

                /* copy the DER into the arena, since Quick DER returns data that points
                   into the DER input, which may get freed by the caller */
                rv = SECITEM_CopyItem(arena, &newParms, &spki->algorithm.parameters);
                if (rv != SECSuccess)
                    break;

                rv = SEC_QuickDERDecodeItem(arena, pubk, SECKEY_DHParamKeyTemplate,
                                            &newParms);

                if (rv == SECSuccess)
                    return pubk;
                break;
            case SEC_OID_ML_DSA_44_PUBLIC_KEY:
            case SEC_OID_ML_DSA_65_PUBLIC_KEY:
            case SEC_OID_ML_DSA_87_PUBLIC_KEY:
                /* A basic consistency check on inputs. */
                if (newOs.len == 0) {
                    PORT_SetError(SEC_ERROR_INPUT_LEN);
                    break;
                }

                /* RFC 9881: the parameters MUST be absent. */
                if (spki->algorithm.parameters.len != 0) {
                    PORT_SetError(SEC_ERROR_INVALID_ALGORITHM);
                    break;
                }

                pubk->keyType = mldsaKey;
                pubk->u.mldsa.paramSet = tag;

                /* newOS is already in the arena, we can just copy the data */
                pubk->u.mldsa.publicValue = newOs;
                return pubk;
            case SEC_OID_ML_KEM_512:
            case SEC_OID_ML_KEM_768:
            case SEC_OID_ML_KEM_1024:
                /* A basic consistency check on inputs. */
                if (newOs.len == 0) {
                    PORT_SetError(SEC_ERROR_INPUT_LEN);
                    break;
                }

                /* RFC 9935: the parameters MUST be absent. */
                if (spki->algorithm.parameters.len != 0) {
                    PORT_SetError(SEC_ERROR_INVALID_ALGORITHM);
                    break;
                }

                pubk->keyType = kyberKey;
                pubk->u.kyber.params = seckey_GetKyberParamsByOidTag(tag);
                if (pubk->u.kyber.params == params_kyber_invalid) {
                    PORT_SetError(SEC_ERROR_INPUT_LEN);
                    break;
                }
                /* newOS is already in the arena, we can just copy the data */
                pubk->u.kyber.publicValue = newOs;
                return pubk;
            case SEC_OID_X25519:
            case SEC_OID_ED25519_PUBLIC_KEY:
                /* A basic consistency check on inputs. */
                if (newOs.len == 0) {
                    PORT_SetError(SEC_ERROR_INPUT_LEN);
                    break;
                }

                /* Currently supporting only (Pure)Ed25519 .*/
                if (spki->algorithm.parameters.len != 0) {
                    PORT_SetError(SEC_ERROR_UNSUPPORTED_KEYALG);
                    break;
                }

                /* edKey corresponds to Ed25519 mechanism.
                 * ecMontKey corresponds to X25519 mechanism.
                 * The other options are not supported. */
                if (tag == SEC_OID_X25519) {
                    pubk->keyType = ecMontKey;
                } else {
                    /* tag == SEC_OID_ED25519_PUBLIC_KEY */
                    pubk->keyType = edKey;
                }

                pubk->u.ec.size = 0;

                SECOidData *oid25519 = SECOID_FindOIDByTag(tag);
                if (!oid25519) {
                    break;
                }

                if (!SECITEM_AllocItem(arena, &pubk->u.ec.DEREncodedParams, oid25519->oid.len + 2)) {
                    break;
                }
                pubk->u.ec.DEREncodedParams.data[0] = SEC_ASN1_OBJECT_ID;
                pubk->u.ec.DEREncodedParams.data[1] = oid25519->oid.len;
                PORT_Memcpy(pubk->u.ec.DEREncodedParams.data + 2, oid25519->oid.data, oid25519->oid.len);

                rv = SECITEM_CopyItem(arena, &pubk->u.ec.publicValue, &newOs);
                if (rv != SECSuccess) {
                    break;
                }
                return pubk;
            case SEC_OID_ANSIX962_EC_PUBLIC_KEY:
                /* A basic sanity check on inputs. */
                if (spki->algorithm.parameters.len == 0 || newOs.len == 0) {
                    PORT_SetError(SEC_ERROR_INPUT_LEN);
                    break;
                }
                pubk->keyType = ecKey;
                pubk->u.ec.size = 0;

                /* Since PKCS#11 directly takes the DER encoding of EC params
                 * and public value, we don't need any decoding here.
                 */
                rv = SECITEM_CopyItem(arena, &pubk->u.ec.DEREncodedParams,
                                      &spki->algorithm.parameters);
                if (rv != SECSuccess) {
                    break;
                }
                rv = SECITEM_CopyItem(arena, &pubk->u.ec.publicValue, &newOs);
                if (rv != SECSuccess) {
                    break;
                }
                pubk->u.ec.encoding = ECPoint_Undefined;
                rv = seckey_HasCurveOID(pubk);
                if (rv == SECSuccess) {
                    return pubk;
                }
                break;

            default:
                PORT_SetError(SEC_ERROR_UNSUPPORTED_KEYALG);
                break;
        }

    SECKEY_DestroyPublicKey(pubk);
    return NULL;
}

/* required for JSS */
SECKEYPublicKey *
SECKEY_ExtractPublicKey(const CERTSubjectPublicKeyInfo *spki)
{
    return seckey_ExtractPublicKey(spki);
}

SECKEYPublicKey *
CERT_ExtractPublicKey(CERTCertificate *cert)
{
    return seckey_ExtractPublicKey(&cert->subjectPublicKeyInfo);
}

int
SECKEY_ECParamsToKeySize(const SECItem *encodedParams)
{
    SECOidTag tag;
    tag = SECKEY_GetECCOid(encodedParams);

    switch (tag) {
        case SEC_OID_SECG_EC_SECP112R1:
        case SEC_OID_SECG_EC_SECP112R2:
            return 112;

        case SEC_OID_SECG_EC_SECT113R1:
        case SEC_OID_SECG_EC_SECT113R2:
            return 113;

        case SEC_OID_SECG_EC_SECP128R1:
        case SEC_OID_SECG_EC_SECP128R2:
            return 128;

        case SEC_OID_SECG_EC_SECT131R1:
        case SEC_OID_SECG_EC_SECT131R2:
            return 131;

        case SEC_OID_SECG_EC_SECP160K1:
        case SEC_OID_SECG_EC_SECP160R1:
        case SEC_OID_SECG_EC_SECP160R2:
            return 160;

        case SEC_OID_SECG_EC_SECT163K1:
        case SEC_OID_SECG_EC_SECT163R1:
        case SEC_OID_SECG_EC_SECT163R2:
        case SEC_OID_ANSIX962_EC_C2PNB163V1:
        case SEC_OID_ANSIX962_EC_C2PNB163V2:
        case SEC_OID_ANSIX962_EC_C2PNB163V3:
            return 163;

        case SEC_OID_ANSIX962_EC_C2PNB176V1:
            return 176;

        case SEC_OID_ANSIX962_EC_C2TNB191V1:
        case SEC_OID_ANSIX962_EC_C2TNB191V2:
        case SEC_OID_ANSIX962_EC_C2TNB191V3:
        case SEC_OID_ANSIX962_EC_C2ONB191V4:
        case SEC_OID_ANSIX962_EC_C2ONB191V5:
            return 191;

        case SEC_OID_SECG_EC_SECP192K1:
        case SEC_OID_ANSIX962_EC_PRIME192V1:
        case SEC_OID_ANSIX962_EC_PRIME192V2:
        case SEC_OID_ANSIX962_EC_PRIME192V3:
            return 192;

        case SEC_OID_SECG_EC_SECT193R1:
        case SEC_OID_SECG_EC_SECT193R2:
            return 193;

        case SEC_OID_ANSIX962_EC_C2PNB208W1:
            return 208;

        case SEC_OID_SECG_EC_SECP224K1:
        case SEC_OID_SECG_EC_SECP224R1:
            return 224;

        case SEC_OID_SECG_EC_SECT233K1:
        case SEC_OID_SECG_EC_SECT233R1:
            return 233;

        case SEC_OID_SECG_EC_SECT239K1:
        case SEC_OID_ANSIX962_EC_C2TNB239V1:
        case SEC_OID_ANSIX962_EC_C2TNB239V2:
        case SEC_OID_ANSIX962_EC_C2TNB239V3:
        case SEC_OID_ANSIX962_EC_C2ONB239V4:
        case SEC_OID_ANSIX962_EC_C2ONB239V5:
        case SEC_OID_ANSIX962_EC_PRIME239V1:
        case SEC_OID_ANSIX962_EC_PRIME239V2:
        case SEC_OID_ANSIX962_EC_PRIME239V3:
            return 239;

        case SEC_OID_SECG_EC_SECP256K1:
        case SEC_OID_ANSIX962_EC_PRIME256V1:
            return 256;

        case SEC_OID_ANSIX962_EC_C2PNB272W1:
            return 272;

        case SEC_OID_SECG_EC_SECT283K1:
        case SEC_OID_SECG_EC_SECT283R1:
            return 283;

        case SEC_OID_ANSIX962_EC_C2PNB304W1:
            return 304;

        case SEC_OID_ANSIX962_EC_C2TNB359V1:
            return 359;

        case SEC_OID_ANSIX962_EC_C2PNB368W1:
            return 368;

        case SEC_OID_SECG_EC_SECP384R1:
            return 384;

        case SEC_OID_SECG_EC_SECT409K1:
        case SEC_OID_SECG_EC_SECT409R1:
            return 409;

        case SEC_OID_ANSIX962_EC_C2TNB431R1:
            return 431;

        case SEC_OID_SECG_EC_SECP521R1:
            return 521;

        case SEC_OID_SECG_EC_SECT571K1:
        case SEC_OID_SECG_EC_SECT571R1:
            return 571;

        case SEC_OID_X25519:
        case SEC_OID_CURVE25519:
        case SEC_OID_ED25519_PUBLIC_KEY:
            return 255;

        default:
            PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
            return 0;
    }
}

int
SECKEY_ECParamsToBasePointOrderLen(const SECItem *encodedParams)
{
    SECOidTag tag = SECKEY_GetECCOid(encodedParams);
    if (tag == SEC_OID_UNKNOWN) {
        return 0; /* error set by SECKEY_GetECCOid */
    }
    switch (tag) {
        case SEC_OID_SECG_EC_SECP112R1:
            return 112;
        case SEC_OID_SECG_EC_SECP112R2:
            return 110;

        case SEC_OID_SECG_EC_SECT113R1:
        case SEC_OID_SECG_EC_SECT113R2:
            return 113;

        case SEC_OID_SECG_EC_SECP128R1:
            return 128;
        case SEC_OID_SECG_EC_SECP128R2:
            return 126;

        case SEC_OID_SECG_EC_SECT131R1:
        case SEC_OID_SECG_EC_SECT131R2:
            return 131;

        case SEC_OID_SECG_EC_SECP160K1:
        case SEC_OID_SECG_EC_SECP160R1:
        case SEC_OID_SECG_EC_SECP160R2:
            return 161;

        case SEC_OID_SECG_EC_SECT163K1:
            return 163;
        case SEC_OID_SECG_EC_SECT163R1:
            return 162;
        case SEC_OID_SECG_EC_SECT163R2:
        case SEC_OID_ANSIX962_EC_C2PNB163V1:
            return 163;
        case SEC_OID_ANSIX962_EC_C2PNB163V2:
        case SEC_OID_ANSIX962_EC_C2PNB163V3:
            return 162;

        case SEC_OID_ANSIX962_EC_C2PNB176V1:
            return 161;

        case SEC_OID_ANSIX962_EC_C2TNB191V1:
            return 191;
        case SEC_OID_ANSIX962_EC_C2TNB191V2:
            return 190;
        case SEC_OID_ANSIX962_EC_C2TNB191V3:
            return 189;
        case SEC_OID_ANSIX962_EC_C2ONB191V4:
            return 191;
        case SEC_OID_ANSIX962_EC_C2ONB191V5:
            return 188;

        case SEC_OID_SECG_EC_SECP192K1:
        case SEC_OID_ANSIX962_EC_PRIME192V1:
        case SEC_OID_ANSIX962_EC_PRIME192V2:
        case SEC_OID_ANSIX962_EC_PRIME192V3:
            return 192;

        case SEC_OID_SECG_EC_SECT193R1:
        case SEC_OID_SECG_EC_SECT193R2:
            return 193;

        case SEC_OID_ANSIX962_EC_C2PNB208W1:
            return 193;

        case SEC_OID_SECG_EC_SECP224K1:
            return 225;
        case SEC_OID_SECG_EC_SECP224R1:
            return 224;

        case SEC_OID_SECG_EC_SECT233K1:
            return 232;
        case SEC_OID_SECG_EC_SECT233R1:
            return 233;

        case SEC_OID_SECG_EC_SECT239K1:
        case SEC_OID_ANSIX962_EC_C2TNB239V1:
            return 238;
        case SEC_OID_ANSIX962_EC_C2TNB239V2:
            return 237;
        case SEC_OID_ANSIX962_EC_C2TNB239V3:
            return 236;
        case SEC_OID_ANSIX962_EC_C2ONB239V4:
            return 238;
        case SEC_OID_ANSIX962_EC_C2ONB239V5:
            return 237;
        case SEC_OID_ANSIX962_EC_PRIME239V1:
        case SEC_OID_ANSIX962_EC_PRIME239V2:
        case SEC_OID_ANSIX962_EC_PRIME239V3:
            return 239;

        case SEC_OID_SECG_EC_SECP256K1:
        case SEC_OID_ANSIX962_EC_PRIME256V1:
            return 256;

        case SEC_OID_ANSIX962_EC_C2PNB272W1:
            return 257;

        case SEC_OID_SECG_EC_SECT283K1:
            return 281;
        case SEC_OID_SECG_EC_SECT283R1:
            return 282;

        case SEC_OID_ANSIX962_EC_C2PNB304W1:
            return 289;

        case SEC_OID_ANSIX962_EC_C2TNB359V1:
            return 353;

        case SEC_OID_ANSIX962_EC_C2PNB368W1:
            return 353;

        case SEC_OID_SECG_EC_SECP384R1:
            return 384;

        case SEC_OID_SECG_EC_SECT409K1:
            return 407;
        case SEC_OID_SECG_EC_SECT409R1:
            return 409;

        case SEC_OID_ANSIX962_EC_C2TNB431R1:
            return 418;

        case SEC_OID_SECG_EC_SECP521R1:
            return 521;

        case SEC_OID_SECG_EC_SECT571K1:
        case SEC_OID_SECG_EC_SECT571R1:
            return 570;

        case SEC_OID_X25519:
        case SEC_OID_CURVE25519:
        case SEC_OID_ED25519_PUBLIC_KEY:
            return 255;

        default:
            PORT_SetError(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
            return 0;
    }
}

/* The number of bits in the number from the first non-zero bit onward. */
unsigned
SECKEY_BigIntegerBitLength(const SECItem *number)
{
    const unsigned char *p;
    unsigned octets;
    unsigned bits;

    if (!number || !number->data) {
        PORT_SetError(SEC_ERROR_INVALID_KEY);
        return 0;
    }

    p = number->data;
    octets = number->len;
    while (octets > 0 && !*p) {
        ++p;
        --octets;
    }
    if (octets == 0) {
        return 0;
    }
    /* bits = 7..1 because we know at least one bit is set already */
    /* Note: This could do a binary search, but this is faster for keys if we
     * assume that good keys will have the MSB set. */
    for (bits = 7; bits > 0; --bits) {
        if (*p & (1 << bits)) {
            break;
        }
    }
    return octets * 8 + bits - 7;
}

/* returns key strength in bytes (not bits) */
unsigned
SECKEY_PublicKeyStrength(const SECKEYPublicKey *pubk)
{
    return (SECKEY_PublicKeyStrengthInBits(pubk) + 7) / 8;
}

/* returns key strength in bits */
unsigned
SECKEY_PublicKeyStrengthInBits(const SECKEYPublicKey *pubk)
{
    unsigned bitSize = 0;

    if (!pubk) {
        PORT_SetError(SEC_ERROR_INVALID_KEY);
        return 0;
    }

    /* interpret modulus length as key strength */
    switch (pubk->keyType) {
        case rsaKey:
            bitSize = SECKEY_BigIntegerBitLength(&pubk->u.rsa.modulus);
            break;
        case dsaKey:
            bitSize = SECKEY_BigIntegerBitLength(&pubk->u.dsa.params.prime);
            break;
        case dhKey:
            bitSize = SECKEY_BigIntegerBitLength(&pubk->u.dh.prime);
            break;
        case ecKey:
        case edKey:
        case ecMontKey:
            bitSize = SECKEY_ECParamsToKeySize(&pubk->u.ec.DEREncodedParams);
            break;
        case mldsaKey:
            bitSize = SECKEY_MLDSAOidParamsToLen(pubk->u.mldsa.paramSet,
                                                 SECKEYPubKeyType) *
                      8;
            break;
        case kyberKey:
            bitSize = seckey_KyberParamsToLen(pubk->u.kyber.params,
                                              SECKEYPubKeyType) *
                      8;
            break;
        default:
            PORT_SetError(SEC_ERROR_INVALID_KEY);
            break;
    }
    return bitSize;
}

unsigned
SECKEY_PrivateKeyStrengthInBits(const SECKEYPrivateKey *privk)
{
    unsigned bitSize = 0;
    SECItem params = { siBuffer, NULL, 0 };
    SECStatus rv;
    SECOidTag paramSetOid;
    KyberParams kyberParams;

    if (!privk) {
        PORT_SetError(SEC_ERROR_INVALID_KEY);
        return 0;
    }

    /* interpret modulus length as key strength */
    switch (privk->keyType) {
        case rsaKey:
        case rsaPssKey:
        case rsaOaepKey:
            rv = PK11_ReadAttribute(privk->pkcs11Slot, privk->pkcs11ID,
                                    CKA_MODULUS, NULL, &params);
            if ((rv != SECSuccess) || (params.data == NULL)) {
                /* some tokens don't export CKA_MODULUS on the private key,
                 * PK11_SignatureLen works around this if necessary. This
                 * method is less percise because it returns bytes instead
                 * of bits, so we only do it if we can't get the modulus */
                bitSize = PK11_SignatureLen((SECKEYPrivateKey *)privk) * PR_BITS_PER_BYTE;
                if (bitSize == -1) {
                    return 0;
                }
                return bitSize;
            }
            bitSize = SECKEY_BigIntegerBitLength(&params);
            PORT_Free(params.data);
            return bitSize;
        case dsaKey:
        case fortezzaKey:
        case dhKey:
        case keaKey:
            rv = PK11_ReadAttribute(privk->pkcs11Slot, privk->pkcs11ID,
                                    CKA_PRIME, NULL, &params);
            if ((rv != SECSuccess) || (params.data == NULL)) {
                PORT_SetError(SEC_ERROR_INVALID_KEY);
                return 0;
            }
            bitSize = SECKEY_BigIntegerBitLength(&params);
            PORT_Free(params.data);
            return bitSize;
        case ecKey:
        case edKey:
        case ecMontKey:
            rv = PK11_ReadAttribute(privk->pkcs11Slot, privk->pkcs11ID,
                                    CKA_EC_PARAMS, NULL, &params);
            if ((rv != SECSuccess) || (params.data == NULL)) {
                return 0;
            }
            bitSize = SECKEY_ECParamsToKeySize(&params);
            PORT_Free(params.data);
            return bitSize;
        case mldsaKey:
            paramSetOid = seckey_GetParameterSet(privk);
            if (paramSetOid == SEC_OID_UNKNOWN) {
                break;
            }
            return SECKEY_MLDSAOidParamsToLen(paramSetOid, SECKEYPrivKeyType) *
                   8;
            break;
        case kyberKey:
            kyberParams = seckey_GetKyberParamsByOidTag(
                seckey_GetParameterSet(privk));
            if (kyberParams == params_kyber_invalid) {
                break;
            }
            return seckey_KyberParamsToLen(kyberParams, SECKEYPrivKeyType) * 8;
            break;
        default:
            break;
    }
    PORT_SetError(SEC_ERROR_INVALID_KEY);
    return 0;
}

/* returns signature length in bytes (not bits) */
unsigned
SECKEY_SignatureLen(const SECKEYPublicKey *pubk)
{
    unsigned size;

    switch (pubk->keyType) {
        case rsaKey:
        case rsaPssKey:
            if (pubk->u.rsa.modulus.len == 0) {
                return 0;
            }
            if (pubk->u.rsa.modulus.data[0] == 0) {
                return pubk->u.rsa.modulus.len - 1;
            }
            return pubk->u.rsa.modulus.len;
        case dsaKey:
            return pubk->u.dsa.params.subPrime.len * 2;
        case ecKey:
        case edKey:
        case ecMontKey:
            /* Get the base point order length in bits and adjust */
            size = SECKEY_ECParamsToBasePointOrderLen(
                &pubk->u.ec.DEREncodedParams);
            return ((size + 7) / 8) * 2;
        case mldsaKey:
            return SECKEY_MLDSAOidParamsToLen(pubk->u.mldsa.paramSet,
                                              SECKEYSignatureType);
            break;
        default:
            break;
    }
    PORT_SetError(SEC_ERROR_INVALID_KEY);
    return 0;
}

SECKEYPrivateKey *
SECKEY_CopyPrivateKey(const SECKEYPrivateKey *privk)
{
    SECKEYPrivateKey *copyk;
    PLArenaPool *arena;

    if (!privk || !privk->pkcs11Slot) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        return NULL;
    }

    copyk = (SECKEYPrivateKey *)PORT_ArenaZAlloc(arena, sizeof(SECKEYPrivateKey));
    if (copyk) {
        copyk->arena = arena;
        copyk->keyType = privk->keyType;

        /* copy the PKCS #11 parameters */
        copyk->pkcs11Slot = PK11_ReferenceSlot(privk->pkcs11Slot);
        /* if the key we're referencing was a temparary key we have just
         * it may go away when we're through, we need
         * to make a copy of it */
        copyk->pkcs11IsTemp = privk->pkcs11IsTemp;
        if (SECKEYPRIVATEKEY_IS_TEMP(privk)) {
            copyk->pkcs11ID =
                PK11_CopyKey(privk->pkcs11Slot, privk->pkcs11ID);
            if (copyk->pkcs11ID == CK_INVALID_HANDLE)
                goto fail;
            /* since we made a copy, we own that copy (even if we
             * didn't own the original */
            SECKEYPRIVATEKEY_SET_OWNED(copyk, PR_TRUE);
        } else {
            copyk->pkcs11ID = privk->pkcs11ID;
            SECKEYPRIVATEKEY_SET_OWNED(copyk, PR_FALSE);
        }
        copyk->wincx = privk->wincx;
        copyk->staticflags = privk->staticflags;
        return copyk;
    } else {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
    }

fail:
    PORT_FreeArena(arena, PR_FALSE);
    return NULL;
}

SECKEYPublicKey *
SECKEY_CopyPublicKey(const SECKEYPublicKey *pubk)
{
    SECKEYPublicKey *copyk;
    PLArenaPool *arena;
    SECStatus rv = SECSuccess;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return NULL;
    }

    copyk = (SECKEYPublicKey *)PORT_ArenaZAlloc(arena, sizeof(SECKEYPublicKey));
    if (!copyk) {
        PORT_FreeArena(arena, PR_FALSE);
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return NULL;
    }

    copyk->arena = arena;
    copyk->keyType = pubk->keyType;
    if (pubk->pkcs11Slot &&
        PK11_IsPermObject(pubk->pkcs11Slot, pubk->pkcs11ID)) {
        copyk->pkcs11Slot = PK11_ReferenceSlot(pubk->pkcs11Slot);
        copyk->pkcs11ID = pubk->pkcs11ID;
    } else {
        copyk->pkcs11Slot = NULL; /* go get own reference */
        copyk->pkcs11ID = CK_INVALID_HANDLE;
    }
    switch (pubk->keyType) {
        case rsaKey:
            rv = SECITEM_CopyItem(arena, &copyk->u.rsa.modulus,
                                  &pubk->u.rsa.modulus);
            if (rv == SECSuccess) {
                rv = SECITEM_CopyItem(arena, &copyk->u.rsa.publicExponent,
                                      &pubk->u.rsa.publicExponent);
                if (rv == SECSuccess)
                    return copyk;
            }
            break;
        case dsaKey:
            rv = SECITEM_CopyItem(arena, &copyk->u.dsa.publicValue,
                                  &pubk->u.dsa.publicValue);
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(arena, &copyk->u.dsa.params.prime,
                                  &pubk->u.dsa.params.prime);
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(arena, &copyk->u.dsa.params.subPrime,
                                  &pubk->u.dsa.params.subPrime);
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(arena, &copyk->u.dsa.params.base,
                                  &pubk->u.dsa.params.base);
            break;
        case dhKey:
            rv = SECITEM_CopyItem(arena, &copyk->u.dh.prime, &pubk->u.dh.prime);
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(arena, &copyk->u.dh.base, &pubk->u.dh.base);
            if (rv != SECSuccess)
                break;
            rv = SECITEM_CopyItem(arena, &copyk->u.dh.publicValue,
                                  &pubk->u.dh.publicValue);
            break;
        case ecKey:
        case edKey:
        case ecMontKey:
            copyk->u.ec.size = pubk->u.ec.size;
            rv = seckey_HasCurveOID(pubk);
            if (rv != SECSuccess) {
                break;
            }
            rv = SECITEM_CopyItem(arena, &copyk->u.ec.DEREncodedParams,
                                  &pubk->u.ec.DEREncodedParams);
            if (rv != SECSuccess) {
                break;
            }
            copyk->u.ec.encoding = ECPoint_Undefined;
            rv = SECITEM_CopyItem(arena, &copyk->u.ec.publicValue,
                                  &pubk->u.ec.publicValue);
            break;
        case mldsaKey:
            copyk->u.mldsa.paramSet = pubk->u.mldsa.paramSet;
            rv = SECITEM_CopyItem(arena, &copyk->u.mldsa.publicValue,
                                  &pubk->u.mldsa.publicValue);
            break;
        case kyberKey:
            copyk->u.kyber.params = pubk->u.kyber.params;
            rv = SECITEM_CopyItem(arena, &copyk->u.kyber.publicValue,
                                  &pubk->u.kyber.publicValue);
            break;
        case nullKey:
            return copyk;
        default:
            PORT_SetError(SEC_ERROR_INVALID_KEY);
            rv = SECFailure;
            break;
    }
    if (rv == SECSuccess)
        return copyk;

    SECKEY_DestroyPublicKey(copyk);
    return NULL;
}

/*
 * Check that a given key meets the policy limits for the given key
 * size.
 */
SECStatus
SECKEY_EnforceKeySize(KeyType keyType, unsigned keyLength, SECErrorCodes error)
{
    PRInt32 opt = -1;
    PRInt32 optVal;
    SECStatus rv;

    switch (keyType) {
        case rsaKey:
        case rsaPssKey:
        case rsaOaepKey:
            opt = NSS_RSA_MIN_KEY_SIZE;
            break;
        case dsaKey:
        case fortezzaKey:
            opt = NSS_DSA_MIN_KEY_SIZE;
            break;
        case dhKey:
        case keaKey:
            opt = NSS_DH_MIN_KEY_SIZE;
            break;
        case ecKey:
            opt = NSS_ECC_MIN_KEY_SIZE;
            break;
        case edKey:
        case mldsaKey:
        case kyberKey:
            return SECSuccess; /* mldsa, ed, and mlkem handles key size policy
                                * by having separate controls on
                                * key params */
        case nullKey:
        default:
            PORT_SetError(SEC_ERROR_INVALID_KEY);
            return SECFailure;
    }
    PORT_Assert(opt != -1);
    rv = NSS_OptionGet(opt, &optVal);
    if (rv != SECSuccess) {
        return rv;
    }
    if (optVal > keyLength) {
        PORT_SetError(error);
        return SECFailure;
    }
    return SECSuccess;
}

/*
 * Use the private key to find a public key handle. The handle will be on
 * the same slot as the private key.
 */
static CK_OBJECT_HANDLE
seckey_FindPublicKeyHandle(SECKEYPrivateKey *privk, SECKEYPublicKey *pubk)
{
    CK_OBJECT_HANDLE keyID;

    /* this helper function is only used below. If we want to make this more
     * general, we would need to free up any already cached handles if the
     * slot doesn't match up with the private key slot */
    PORT_Assert(pubk->pkcs11ID == CK_INVALID_HANDLE);

    /* first look for a matching public key */
    keyID = PK11_MatchItem(privk->pkcs11Slot, privk->pkcs11ID, CKO_PUBLIC_KEY);
    if (keyID != CK_INVALID_HANDLE) {
        return keyID;
    }

    /* none found, create a temp one, make the pubk the owner */
    pubk->pkcs11ID = PK11_DerivePubKeyFromPrivKey(privk);
    if (pubk->pkcs11ID == CK_INVALID_HANDLE) {
        /* end of the road. Token doesn't have matching public key, nor can
         * token regenerate a new public key from and existing private key. */
        return CK_INVALID_HANDLE;
    }
    pubk->pkcs11Slot = PK11_ReferenceSlot(privk->pkcs11Slot);
    return pubk->pkcs11ID;
}

SECKEYPublicKey *
SECKEY_ConvertToPublicKey(SECKEYPrivateKey *privk)
{
    SECKEYPublicKey *pubk;
    PLArenaPool *arena;
    CERTCertificate *cert;
    SECStatus rv;
    CK_OBJECT_HANDLE pubKeyHandle;
    SECItem decodedPoint = { siBuffer, NULL, 0 };
    SECItem *point;
    unsigned int fieldLen;

    /*
     * First try to look up the cert.
     */
    cert = PK11_GetCertFromPrivateKey(privk);
    if (cert) {
        pubk = CERT_ExtractPublicKey(cert);
        CERT_DestroyCertificate(cert);
        return pubk;
    }

    /* couldn't find the cert, build pub key by hand */
    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return NULL;
    }
    pubk = (SECKEYPublicKey *)PORT_ArenaZAlloc(arena,
                                               sizeof(SECKEYPublicKey));
    if (pubk == NULL) {
        PORT_FreeArena(arena, PR_FALSE);
        return NULL;
    }
    pubk->keyType = privk->keyType;
    pubk->pkcs11Slot = NULL;
    pubk->pkcs11ID = CK_INVALID_HANDLE;
    pubk->arena = arena;

    switch (privk->keyType) {
        case nullKey:
            /* Nothing to query, if the cert isn't there, we're done -- no way
             * to get the public key */
            break;
        case dsaKey:
            pubKeyHandle = seckey_FindPublicKeyHandle(privk, pubk);
            if (pubKeyHandle == CK_INVALID_HANDLE)
                break;
            rv = PK11_ReadAttribute(privk->pkcs11Slot, pubKeyHandle,
                                    CKA_BASE, arena, &pubk->u.dsa.params.base);
            if (rv != SECSuccess)
                break;
            rv = PK11_ReadAttribute(privk->pkcs11Slot, pubKeyHandle,
                                    CKA_PRIME, arena, &pubk->u.dsa.params.prime);
            if (rv != SECSuccess)
                break;
            rv = PK11_ReadAttribute(privk->pkcs11Slot, pubKeyHandle,
                                    CKA_SUBPRIME, arena, &pubk->u.dsa.params.subPrime);
            if (rv != SECSuccess)
                break;
            rv = PK11_ReadAttribute(privk->pkcs11Slot, pubKeyHandle,
                                    CKA_VALUE, arena, &pubk->u.dsa.publicValue);
            if (rv != SECSuccess)
                break;
            return pubk;
        case dhKey:
            pubKeyHandle = seckey_FindPublicKeyHandle(privk, pubk);
            if (pubKeyHandle == CK_INVALID_HANDLE)
                break;
            rv = PK11_ReadAttribute(privk->pkcs11Slot, pubKeyHandle,
                                    CKA_BASE, arena, &pubk->u.dh.base);
            if (rv != SECSuccess)
                break;
            rv = PK11_ReadAttribute(privk->pkcs11Slot, pubKeyHandle,
                                    CKA_PRIME, arena, &pubk->u.dh.prime);
            if (rv != SECSuccess)
                break;
            rv = PK11_ReadAttribute(privk->pkcs11Slot, pubKeyHandle,
                                    CKA_VALUE, arena, &pubk->u.dh.publicValue);
            if (rv != SECSuccess)
                break;
            return pubk;
        case rsaKey:
            rv = PK11_ReadAttribute(privk->pkcs11Slot, privk->pkcs11ID,
                                    CKA_MODULUS, arena, &pubk->u.rsa.modulus);
            if (rv != SECSuccess)
                break;
            rv = PK11_ReadAttribute(privk->pkcs11Slot, privk->pkcs11ID,
                                    CKA_PUBLIC_EXPONENT, arena, &pubk->u.rsa.publicExponent);
            if (rv != SECSuccess)
                break;
            return pubk;
        case ecKey:
            rv = PK11_ReadAttribute(privk->pkcs11Slot, privk->pkcs11ID,
                                    CKA_EC_PARAMS, arena, &pubk->u.ec.DEREncodedParams);
            if (rv != SECSuccess) {
                break;
            }
            rv = PK11_ReadAttribute(privk->pkcs11Slot, privk->pkcs11ID,
                                    CKA_EC_POINT, arena, &pubk->u.ec.publicValue);
            if (rv != SECSuccess || pubk->u.ec.publicValue.len == 0) {
                pubKeyHandle = seckey_FindPublicKeyHandle(privk, pubk);
                if (pubKeyHandle == CK_INVALID_HANDLE)
                    break;
                rv = PK11_ReadAttribute(privk->pkcs11Slot, pubKeyHandle,
                                        CKA_EC_POINT, arena, &pubk->u.ec.publicValue);
                if (rv != SECSuccess)
                    break;
            }
            /* ec.publicValue should be a bare SEC#1 encoding. PKCS #11 defines
             * CKA_EC_POINT with a DER octet string wrapper. Some earlier NSS
             * versions stored bare points, so we need to handle both cases. */
            fieldLen = (SECKEY_ECParamsToKeySize(&pubk->u.ec.DEREncodedParams) +
                        7) /
                       8;
            point = &pubk->u.ec.publicValue;
            if (!ECPoint_IsBare(point, fieldLen) && point->len != 0 &&
                point->data[0] == SEC_ASN1_OCTET_STRING) {
                rv = SEC_QuickDERDecodeItem(arena, &decodedPoint,
                                            SEC_ASN1_GET(SEC_OctetStringTemplate), point);
                if (rv == SECSuccess) {
                    /* both values are in the public key arena, so it's safe to
                     * overwrite  the old value */
                    *point = decodedPoint;
                }
            }

            pubk->u.ec.encoding = ECPoint_Undefined;
            return pubk;
        case edKey:
        case ecMontKey:
            rv = PK11_ReadAttribute(privk->pkcs11Slot, privk->pkcs11ID,
                                    CKA_EC_PARAMS, arena, &pubk->u.ec.DEREncodedParams);
            if (rv != SECSuccess) {
                break;
            }
            rv = PK11_ReadAttribute(privk->pkcs11Slot, privk->pkcs11ID,
                                    CKA_EC_POINT, arena, &pubk->u.ec.publicValue);
            if (rv != SECSuccess || pubk->u.ec.publicValue.len == 0) {
                pubKeyHandle = seckey_FindPublicKeyHandle(privk, pubk);
                if (pubKeyHandle == CK_INVALID_HANDLE) {
                    break;
                }
                rv = PK11_ReadAttribute(privk->pkcs11Slot, pubKeyHandle,
                                        CKA_EC_POINT, arena, &pubk->u.ec.publicValue);
                if (rv != SECSuccess) {
                    break;
                }
            }
            pubk->u.ec.encoding = ECPoint_Undefined;
            return pubk;
        case mldsaKey:
            pubKeyHandle = seckey_FindPublicKeyHandle(privk, pubk);
            if (pubKeyHandle == CK_INVALID_HANDLE) {
                break;
            }
            pubk->u.mldsa.paramSet = seckey_GetParameterSet(privk);
            if (pubk->u.mldsa.paramSet == SEC_OID_UNKNOWN) {
                PORT_SetError(SEC_ERROR_INVALID_KEY);
                break;
            }
            rv = PK11_ReadAttribute(privk->pkcs11Slot, pubKeyHandle,
                                    CKA_VALUE, arena, &pubk->u.mldsa.publicValue);
            if (rv != SECSuccess) {
                break;
            }
            return pubk;
        case kyberKey:
            pubKeyHandle = seckey_FindPublicKeyHandle(privk, pubk);
            if (pubKeyHandle == CK_INVALID_HANDLE) {
                break;
            }
            pubk->u.kyber.params = seckey_GetKyberParamsByOidTag(
                seckey_GetParameterSet(privk));
            if (pubk->u.kyber.params == params_kyber_invalid) {
                PORT_SetError(SEC_ERROR_INVALID_KEY);
                break;
            }
            rv = PK11_ReadAttribute(privk->pkcs11Slot, pubKeyHandle,
                                    CKA_VALUE, arena, &pubk->u.kyber.publicValue);
            if (rv != SECSuccess) {
                break;
            }
            return pubk;

        default:
            break;
    }

    /* must use Destroy public key here, because some paths create temporary
     * PKCS #11 objects which need to be freed */
    SECKEY_DestroyPublicKey(pubk);
    return NULL;
}

/* Return the AlgorithmIdentifier OID to use in a SubjectPublicKeyInfo for an
 * edKey or ecMontKey public key.
 *
 * RFC 8410 identifies these keys by the algorithm OID alone (id-X25519 =
 * 1.3.101.110, id-Ed25519 = 1.3.101.112); the curve is not named separately.
 * NSS additionally recognizes the pre-RFC 8410 curve25519 OID
 * (1.3.6.1.4.1.11591.15.1, from the expired draft-josefsson-pkix-newcurves)
 * as a named curve, and CKM_EC_MONTGOMERY_KEY_PAIR_GEN will generate a key
 * whose CKA_EC_PARAMS carries it. That OID has no SubjectPublicKeyInfo
 * encoding of its own, so map it to id-X25519 rather than emitting an SPKI
 * that no parser -- including seckey_ExtractPublicKey -- will accept.
 */
static SECOidTag
seckey_GetRFC8410AlgTag(const SECKEYPublicKey *pubk)
{
    SECOidTag curve = SECKEY_GetECCOid(&pubk->u.ec.DEREncodedParams);

    switch (pubk->keyType) {
        case ecMontKey:
            if (curve == SEC_OID_X25519 || curve == SEC_OID_CURVE25519) {
                return SEC_OID_X25519;
            }
            break;
        case edKey:
            if (curve == SEC_OID_ED25519) {
                return SEC_OID_ED25519_PUBLIC_KEY;
            }
            break;
        default:
            break;
    }

    PORT_SetError(SEC_ERROR_UNSUPPORTED_KEYALG);
    return SEC_OID_UNKNOWN;
}

static CERTSubjectPublicKeyInfo *
seckey_CreateSubjectPublicKeyInfo_helper(SECKEYPublicKey *pubk)
{
    CERTSubjectPublicKeyInfo *spki;
    PLArenaPool *arena;
    SECItem params = { siBuffer, NULL, 0 };
    SECOidTag tag;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return NULL;
    }

    spki = (CERTSubjectPublicKeyInfo *)PORT_ArenaZAlloc(arena, sizeof(*spki));
    if (spki != NULL) {
        SECStatus rv;
        SECItem *rv_item;

        spki->arena = arena;
        switch (pubk->keyType) {
            case rsaKey:
                rv = SECOID_SetAlgorithmID(arena, &spki->algorithm,
                                           SEC_OID_PKCS1_RSA_ENCRYPTION, 0);
                if (rv == SECSuccess) {
                    /*
                     * DER encode the public key into the subjectPublicKeyInfo.
                     */
                    prepare_rsa_pub_key_for_asn1(pubk);
                    rv_item = SEC_ASN1EncodeItem(arena, &spki->subjectPublicKey,
                                                 pubk, SECKEY_RSAPublicKeyTemplate);
                    if (rv_item != NULL) {
                        /*
                         * The stored value is supposed to be a BIT_STRING,
                         * so convert the length.
                         */
                        spki->subjectPublicKey.len <<= 3;
                        /*
                         * We got a good one; return it.
                         */
                        return spki;
                    }
                }
                break;
            case dsaKey:
                /* DER encode the params. */
                prepare_pqg_params_for_asn1(&pubk->u.dsa.params);
                rv_item = SEC_ASN1EncodeItem(arena, &params, &pubk->u.dsa.params,
                                             SECKEY_PQGParamsTemplate);
                if (rv_item != NULL) {
                    rv = SECOID_SetAlgorithmID(arena, &spki->algorithm,
                                               SEC_OID_ANSIX9_DSA_SIGNATURE,
                                               &params);
                    if (rv == SECSuccess) {
                        /*
                         * DER encode the public key into the subjectPublicKeyInfo.
                         */
                        prepare_dsa_pub_key_for_asn1(pubk);
                        rv_item = SEC_ASN1EncodeItem(arena, &spki->subjectPublicKey,
                                                     pubk,
                                                     SECKEY_DSAPublicKeyTemplate);
                        if (rv_item != NULL) {
                            /*
                             * The stored value is supposed to be a BIT_STRING,
                             * so convert the length.
                             */
                            spki->subjectPublicKey.len <<= 3;
                            /*
                             * We got a good one; return it.
                             */
                            return spki;
                        }
                    }
                }
                SECITEM_FreeItem(&params, PR_FALSE);
                break;
            case ecKey:
                rv = SECITEM_CopyItem(arena, &params,
                                      &pubk->u.ec.DEREncodedParams);
                if (rv != SECSuccess) {
                    break;
                }

                tag = SEC_OID_ANSIX962_EC_PUBLIC_KEY;
                rv = SECOID_SetAlgorithmID(arena, &spki->algorithm,
                                           tag,
                                           &params);
                if (rv != SECSuccess) {
                    break;
                }

                rv = SECITEM_CopyItem(arena, &spki->subjectPublicKey,
                                      &pubk->u.ec.publicValue);

                if (rv == SECSuccess) {
                    /*
                     * The stored value is supposed to be a BIT_STRING,
                     * so convert the length.
                     */
                    spki->subjectPublicKey.len <<= 3;
                    /*
                     * We got a good one; return it.
                     */
                    return spki;
                }
                break;
            case edKey:
            case ecMontKey:
                tag = seckey_GetRFC8410AlgTag(pubk);
                if (tag == SEC_OID_UNKNOWN) {
                    break;
                }
                rv = SECOID_SetAlgorithmID(arena, &spki->algorithm,
                                           tag,
                                           &params);
                if (rv != SECSuccess) {
                    break;
                }

                rv = SECITEM_CopyItem(arena, &spki->subjectPublicKey,
                                      &pubk->u.ec.publicValue);

                if (rv == SECSuccess) {
                    /*
                     * The stored value is supposed to be a BIT_STRING,
                     * so convert the length.
                     */
                    spki->subjectPublicKey.len <<= 3;
                    /*
                     * We got a good one; return it.
                     */
                    return spki;
                }
                break;
            case mldsaKey:
                tag = pubk->u.mldsa.paramSet;
                rv = SECOID_SetAlgorithmID(arena, &spki->algorithm,
                                           tag, NULL);
                if (rv != SECSuccess) {
                    break;
                }

                rv = SECITEM_CopyItem(arena, &spki->subjectPublicKey,
                                      &pubk->u.mldsa.publicValue);

                if (rv == SECSuccess) {
                    /*
                     * The stored value is supposed to be a BIT_STRING,
                     * so convert the length.
                     */
                    spki->subjectPublicKey.len <<= 3;
                    /*
                     * We got a good one; return it.
                     */
                    return spki;
                }
                break;
            case kyberKey:
                tag = seckey_GetMLKEMOidTagByPkcs11ParamSet(
                    seckey_GetMLKEMPkcs11ParamsByKyberParams(
                        pubk->u.kyber.params));
                rv = SECOID_SetAlgorithmID(arena, &spki->algorithm,
                                           tag, NULL);
                if (rv != SECSuccess) {
                    break;
                }

                rv = SECITEM_CopyItem(arena, &spki->subjectPublicKey,
                                      &pubk->u.kyber.publicValue);

                if (rv == SECSuccess) {
                    /*
                     * The stored value is supposed to be a BIT_STRING,
                     * so convert the length.
                     */
                    spki->subjectPublicKey.len <<= 3;
                    /*
                     * We got a good one; return it.
                     */
                    return spki;
                }
                break;
            case dhKey: /* later... */

                break;
            default:
                break;
        }
    } else {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
    }

    PORT_FreeArena(arena, PR_FALSE);
    return NULL;
}

CERTSubjectPublicKeyInfo *
SECKEY_CreateSubjectPublicKeyInfo(const SECKEYPublicKey *pubk)
{
    CERTSubjectPublicKeyInfo *spki;
    SECKEYPublicKey *tempKey;

    if (!pubk) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    tempKey = SECKEY_CopyPublicKey(pubk);
    if (!tempKey) {
        return NULL;
    }
    spki = seckey_CreateSubjectPublicKeyInfo_helper(tempKey);
    SECKEY_DestroyPublicKey(tempKey);
    return spki;
}

void
SECKEY_DestroySubjectPublicKeyInfo(CERTSubjectPublicKeyInfo *spki)
{
    if (spki && spki->arena) {
        PORT_FreeArena(spki->arena, PR_FALSE);
    }
}

SECItem *
SECKEY_EncodeDERSubjectPublicKeyInfo(const SECKEYPublicKey *pubk)
{
    CERTSubjectPublicKeyInfo *spki = NULL;
    SECItem *spkiDER = NULL;

    /* get the subjectpublickeyinfo */
    spki = SECKEY_CreateSubjectPublicKeyInfo(pubk);
    if (spki == NULL) {
        goto finish;
    }

    /* DER-encode the subjectpublickeyinfo */
    spkiDER = SEC_ASN1EncodeItem(NULL /*arena*/, NULL /*dest*/, spki,
                                 CERT_SubjectPublicKeyInfoTemplate);

    SECKEY_DestroySubjectPublicKeyInfo(spki);

finish:
    return spkiDER;
}

CERTSubjectPublicKeyInfo *
SECKEY_DecodeDERSubjectPublicKeyInfo(const SECItem *spkider)
{
    PLArenaPool *arena;
    CERTSubjectPublicKeyInfo *spki;
    SECStatus rv;
    SECItem newSpkider;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return NULL;
    }

    spki = (CERTSubjectPublicKeyInfo *)
        PORT_ArenaZAlloc(arena, sizeof(CERTSubjectPublicKeyInfo));
    if (spki != NULL) {
        spki->arena = arena;

        /* copy the DER into the arena, since Quick DER returns data that points
           into the DER input, which may get freed by the caller */
        rv = SECITEM_CopyItem(arena, &newSpkider, spkider);
        if (rv == SECSuccess) {
            rv = SEC_QuickDERDecodeItem(arena, spki,
                                        CERT_SubjectPublicKeyInfoTemplate, &newSpkider);
        }
        if (rv == SECSuccess) {
            return spki;
        }
    } else {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
    }

    PORT_FreeArena(arena, PR_FALSE);
    return NULL;
}

void
SECKEY_DestroyPrivateKeyInfo(SECKEYPrivateKeyInfo *pvk,
                             PRBool freeit)
{
    PLArenaPool *poolp;

    if (pvk != NULL) {
        if (pvk->arena) {
            poolp = pvk->arena;
            /* zero structure since PORT_FreeArena does not support
             * this yet.
             */
            PORT_Memset(pvk->privateKey.data, 0, pvk->privateKey.len);
            PORT_Memset(pvk, 0, sizeof(*pvk));
            if (freeit == PR_TRUE) {
                PORT_FreeArena(poolp, PR_TRUE);
            } else {
                pvk->arena = poolp;
            }
        } else {
            SECITEM_ZfreeItem(&pvk->version, PR_FALSE);
            SECITEM_ZfreeItem(&pvk->privateKey, PR_FALSE);
            SECOID_DestroyAlgorithmID(&pvk->algorithm, PR_FALSE);
            PORT_Memset(pvk, 0, sizeof(*pvk));
            if (freeit == PR_TRUE) {
                PORT_Free(pvk);
            }
        }
    }
}

void
SECKEY_DestroyEncryptedPrivateKeyInfo(SECKEYEncryptedPrivateKeyInfo *epki,
                                      PRBool freeit)
{
    PLArenaPool *poolp;

    if (epki != NULL) {
        if (epki->arena) {
            poolp = epki->arena;
            /* zero structure since PORT_FreeArena does not support
             * this yet.
             */
            PORT_Memset(epki->encryptedData.data, 0, epki->encryptedData.len);
            PORT_Memset(epki, 0, sizeof(*epki));
            if (freeit == PR_TRUE) {
                PORT_FreeArena(poolp, PR_TRUE);
            } else {
                epki->arena = poolp;
            }
        } else {
            SECITEM_ZfreeItem(&epki->encryptedData, PR_FALSE);
            SECOID_DestroyAlgorithmID(&epki->algorithm, PR_FALSE);
            PORT_Memset(epki, 0, sizeof(*epki));
            if (freeit == PR_TRUE) {
                PORT_Free(epki);
            }
        }
    }
}

SECStatus
SECKEY_CopyPrivateKeyInfo(PLArenaPool *poolp,
                          SECKEYPrivateKeyInfo *to,
                          const SECKEYPrivateKeyInfo *from)
{
    SECStatus rv = SECFailure;

    if ((to == NULL) || (from == NULL)) {
        return SECFailure;
    }

    rv = SECOID_CopyAlgorithmID(poolp, &to->algorithm, &from->algorithm);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    rv = SECITEM_CopyItem(poolp, &to->privateKey, &from->privateKey);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    rv = SECITEM_CopyItem(poolp, &to->version, &from->version);

    return rv;
}

SECStatus
SECKEY_CopyEncryptedPrivateKeyInfo(PLArenaPool *poolp,
                                   SECKEYEncryptedPrivateKeyInfo *to,
                                   const SECKEYEncryptedPrivateKeyInfo *from)
{
    SECStatus rv = SECFailure;

    if ((to == NULL) || (from == NULL)) {
        return SECFailure;
    }

    rv = SECOID_CopyAlgorithmID(poolp, &to->algorithm, &from->algorithm);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    rv = SECITEM_CopyItem(poolp, &to->encryptedData, &from->encryptedData);

    return rv;
}

KeyType
SECKEY_GetPrivateKeyType(const SECKEYPrivateKey *privKey)
{
    return privKey->keyType;
}

KeyType
SECKEY_GetPublicKeyType(const SECKEYPublicKey *pubKey)
{
    return pubKey->keyType;
}

SECKEYPublicKey *
SECKEY_ImportDERPublicKey(const SECItem *derKey, CK_KEY_TYPE type)
{
    SECKEYPublicKey *pubk = NULL;
    SECStatus rv = SECFailure;
    SECItem newDerKey;
    PLArenaPool *arena = NULL;

    if (!derKey) {
        return NULL;
    }

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        goto finish;
    }

    pubk = PORT_ArenaZNew(arena, SECKEYPublicKey);
    if (pubk == NULL) {
        goto finish;
    }
    pubk->arena = arena;

    rv = SECITEM_CopyItem(pubk->arena, &newDerKey, derKey);
    if (SECSuccess != rv) {
        goto finish;
    }

    pubk->pkcs11Slot = NULL;
    pubk->pkcs11ID = CK_INVALID_HANDLE;

    switch (type) {
        case CKK_RSA:
            prepare_rsa_pub_key_for_asn1(pubk);
            rv = SEC_QuickDERDecodeItem(pubk->arena, pubk, SECKEY_RSAPublicKeyTemplate, &newDerKey);
            pubk->keyType = rsaKey;
            break;
        case CKK_DSA:
            prepare_dsa_pub_key_for_asn1(pubk);
            rv = SEC_QuickDERDecodeItem(pubk->arena, pubk, SECKEY_DSAPublicKeyTemplate, &newDerKey);
            pubk->keyType = dsaKey;
            break;
        case CKK_DH:
            prepare_dh_pub_key_for_asn1(pubk);
            rv = SEC_QuickDERDecodeItem(pubk->arena, pubk, SECKEY_DHPublicKeyTemplate, &newDerKey);
            pubk->keyType = dhKey;
            break;
        case CKK_ML_DSA:
            pubk->keyType = mldsaKey;
            /* ml_dsa has no derencoding */
            pubk->u.mldsa.publicValue = newDerKey;
            pubk->u.mldsa.paramSet = SECKEY_MLDSAOidParamsFromLen(newDerKey.len,
                                                                  SECKEYPubKeyType);
            if (pubk->u.mldsa.paramSet == SEC_OID_UNKNOWN) {
                PORT_SetError(SEC_ERROR_BAD_KEY);
                rv = SECFailure;
            }
            break;
        case CKK_ML_KEM:
            pubk->keyType = kyberKey;
            /* ml_dsa has no derencoding */
            pubk->u.kyber.publicValue = newDerKey;
            pubk->u.kyber.params = seckey_KyberParamsFromLen(newDerKey.len,
                                                             SECKEYPubKeyType);
            if (pubk->u.kyber.params == params_kyber_invalid) {
                PORT_SetError(SEC_ERROR_BAD_KEY);
                rv = SECFailure;
            }
            break;
        default:
            rv = SECFailure;
            break;
    }

finish:
    if (rv != SECSuccess) {
        if (arena != NULL) {
            PORT_FreeArena(arena, PR_FALSE);
        }
        pubk = NULL;
    }
    return pubk;
}

SECKEYPrivateKeyList *
SECKEY_NewPrivateKeyList(void)
{
    PLArenaPool *arena = NULL;
    SECKEYPrivateKeyList *ret = NULL;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        goto loser;
    }

    ret = (SECKEYPrivateKeyList *)PORT_ArenaZAlloc(arena,
                                                   sizeof(SECKEYPrivateKeyList));
    if (ret == NULL) {
        goto loser;
    }

    ret->arena = arena;

    PR_INIT_CLIST(&ret->list);

    return (ret);

loser:
    if (arena != NULL) {
        PORT_FreeArena(arena, PR_FALSE);
    }

    return (NULL);
}

void
SECKEY_DestroyPrivateKeyList(SECKEYPrivateKeyList *keys)
{
    while (!PR_CLIST_IS_EMPTY(&keys->list)) {
        SECKEY_RemovePrivateKeyListNode(
            (SECKEYPrivateKeyListNode *)(PR_LIST_HEAD(&keys->list)));
    }

    PORT_FreeArena(keys->arena, PR_FALSE);

    return;
}

void
SECKEY_RemovePrivateKeyListNode(SECKEYPrivateKeyListNode *node)
{
    PR_ASSERT(node->key);
    SECKEY_DestroyPrivateKey(node->key);
    node->key = NULL;
    PR_REMOVE_LINK(&node->links);
    return;
}

SECStatus
SECKEY_AddPrivateKeyToListTail(SECKEYPrivateKeyList *list,
                               SECKEYPrivateKey *key)
{
    SECKEYPrivateKeyListNode *node;

    node = (SECKEYPrivateKeyListNode *)PORT_ArenaZAlloc(list->arena,
                                                        sizeof(SECKEYPrivateKeyListNode));
    if (node == NULL) {
        goto loser;
    }

    PR_INSERT_BEFORE(&node->links, &list->list);
    node->key = key;
    return (SECSuccess);

loser:
    return (SECFailure);
}

SECKEYPublicKeyList *
SECKEY_NewPublicKeyList(void)
{
    PLArenaPool *arena = NULL;
    SECKEYPublicKeyList *ret = NULL;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        goto loser;
    }

    ret = (SECKEYPublicKeyList *)PORT_ArenaZAlloc(arena,
                                                  sizeof(SECKEYPublicKeyList));
    if (ret == NULL) {
        goto loser;
    }

    ret->arena = arena;

    PR_INIT_CLIST(&ret->list);

    return (ret);

loser:
    if (arena != NULL) {
        PORT_FreeArena(arena, PR_FALSE);
    }

    return (NULL);
}

void
SECKEY_DestroyPublicKeyList(SECKEYPublicKeyList *keys)
{
    while (!PR_CLIST_IS_EMPTY(&keys->list)) {
        SECKEY_RemovePublicKeyListNode(
            (SECKEYPublicKeyListNode *)(PR_LIST_HEAD(&keys->list)));
    }

    PORT_FreeArena(keys->arena, PR_FALSE);

    return;
}

void
SECKEY_RemovePublicKeyListNode(SECKEYPublicKeyListNode *node)
{
    PR_ASSERT(node->key);
    SECKEY_DestroyPublicKey(node->key);
    node->key = NULL;
    PR_REMOVE_LINK(&node->links);
    return;
}

SECStatus
SECKEY_AddPublicKeyToListTail(SECKEYPublicKeyList *list,
                              SECKEYPublicKey *key)
{
    SECKEYPublicKeyListNode *node;

    node = (SECKEYPublicKeyListNode *)PORT_ArenaZAlloc(list->arena,
                                                       sizeof(SECKEYPublicKeyListNode));
    if (node == NULL) {
        goto loser;
    }

    PR_INSERT_BEFORE(&node->links, &list->list);
    node->key = key;
    return (SECSuccess);

loser:
    return (SECFailure);
}

#define SECKEY_CacheAttribute(key, attribute)                                                   \
    if (CK_TRUE == PK11_HasAttributeSet(key->pkcs11Slot, key->pkcs11ID, attribute, PR_FALSE)) { \
        key->staticflags |= SECKEY_##attribute;                                                 \
    } else {                                                                                    \
        key->staticflags &= (~SECKEY_##attribute);                                              \
    }

SECStatus
SECKEY_CacheStaticFlags(SECKEYPrivateKey *key)
{
    SECStatus rv = SECFailure;
    if (key && key->pkcs11Slot && key->pkcs11ID) {
        key->staticflags |= SECKEY_Attributes_Cached;
        SECKEY_CacheAttribute(key, CKA_PRIVATE);
        SECKEY_CacheAttribute(key, CKA_ALWAYS_AUTHENTICATE);
        rv = SECSuccess;
    }
    return rv;
}

SECOidTag
SECKEY_GetECCOid(const SECKEYECParams *params)
{
    SECItem oid = { siBuffer, NULL, 0 };
    SECOidData *oidData = NULL;

    /*
     * params->data needs to contain the ASN encoding of an object ID (OID)
     * representing a named curve. Here, we strip away everything
     * before the actual OID and use the OID to look up a named curve.
     */
    if (!params || !params->data || params->len < 2 ||
        params->data[0] != SEC_ASN1_OBJECT_ID ||
        (params->data[1] & 0x80) != 0 ||
        params->data[1] != (params->len - 2)) {
        PORT_SetError(SEC_ERROR_BAD_DER);
        return 0;
    }
    oid.len = params->len - 2;
    oid.data = params->data + 2;
    if ((oidData = SECOID_FindOID(&oid)) == NULL)
        return 0;

    return oidData->offset;
}

static CK_MECHANISM_TYPE
sec_GetHashMechanismByOidTag(SECOidTag tag)
{
    switch (tag) {
        case SEC_OID_SHA512:
            return CKM_SHA512;
        case SEC_OID_SHA384:
            return CKM_SHA384;
        case SEC_OID_SHA256:
            return CKM_SHA256;
        case SEC_OID_SHA224:
            return CKM_SHA224;
        case SEC_OID_SHA1:
            return CKM_SHA_1;
        default:
            PORT_SetError(SEC_ERROR_INVALID_ALGORITHM);
            return CKM_INVALID_MECHANISM;
    }
}

CK_RSA_PKCS_MGF_TYPE
SEC_GetMgfTypeByOidTag(SECOidTag tag)
{
    switch (tag) {
        case SEC_OID_SHA3_512:
            return CKG_MGF1_SHA3_512;
        case SEC_OID_SHA3_384:
            return CKG_MGF1_SHA3_384;
        case SEC_OID_SHA3_256:
            return CKG_MGF1_SHA3_256;
        case SEC_OID_SHA3_224:
            return CKG_MGF1_SHA3_224;
        case SEC_OID_SHA512:
            return CKG_MGF1_SHA512;
        case SEC_OID_SHA384:
            return CKG_MGF1_SHA384;
        case SEC_OID_SHA256:
            return CKG_MGF1_SHA256;
        case SEC_OID_SHA224:
            return CKG_MGF1_SHA224;
        case SEC_OID_SHA1:
            return CKG_MGF1_SHA1;
        default:
            PORT_SetError(SEC_ERROR_INVALID_ALGORITHM);
            return 0;
    }
}

SECStatus
sec_DecodeRSAPSSParams(PLArenaPool *arena,
                       const SECItem *params,
                       SECOidTag *retHashAlg, SECOidTag *retMaskHashAlg,
                       unsigned long *retSaltLength)
{
    SECKEYRSAPSSParams pssParams;
    SECOidTag hashAlg;
    SECOidTag maskHashAlg;
    unsigned long saltLength;
    unsigned long trailerField;
    SECStatus rv;

    PORT_Memset(&pssParams, 0, sizeof(pssParams));
    rv = SEC_QuickDERDecodeItem(arena, &pssParams,
                                SECKEY_RSAPSSParamsTemplate,
                                params);
    if (rv != SECSuccess) {
        return rv;
    }

    if (pssParams.hashAlg) {
        hashAlg = SECOID_GetAlgorithmTag(pssParams.hashAlg);
    } else {
        hashAlg = SEC_OID_SHA1; /* default, SHA-1 */
    }

    if (pssParams.maskAlg) {
        SECAlgorithmID algId;

        if (SECOID_GetAlgorithmTag(pssParams.maskAlg) != SEC_OID_PKCS1_MGF1) {
            /* only MGF1 is known to PKCS#11 */
            PORT_SetError(SEC_ERROR_INVALID_ALGORITHM);
            return SECFailure;
        }

        rv = SEC_QuickDERDecodeItem(arena, &algId,
                                    SEC_ASN1_GET(SECOID_AlgorithmIDTemplate),
                                    &pssParams.maskAlg->parameters);
        if (rv != SECSuccess) {
            return rv;
        }
        maskHashAlg = SECOID_GetAlgorithmTag(&algId);
    } else {
        maskHashAlg = SEC_OID_SHA1; /* default, MGF1 with SHA-1 */
    }

    if (pssParams.saltLength.data) {
        rv = SEC_ASN1DecodeInteger((SECItem *)&pssParams.saltLength, &saltLength);
        if (rv != SECSuccess) {
            return rv;
        }
    } else {
        saltLength = 20; /* default, 20 */
    }

    if (pssParams.trailerField.data) {
        rv = SEC_ASN1DecodeInteger((SECItem *)&pssParams.trailerField, &trailerField);
        if (rv != SECSuccess) {
            return rv;
        }
        if (trailerField != 1) {
            /* the value must be 1, which represents the trailer field
             * with hexadecimal value 0xBC */
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            return SECFailure;
        }
    }

    if (retHashAlg) {
        *retHashAlg = hashAlg;
    }
    if (retMaskHashAlg) {
        *retMaskHashAlg = maskHashAlg;
    }
    if (retSaltLength) {
        *retSaltLength = saltLength;
    }

    return SECSuccess;
}

SECStatus
sec_DecodeRSAPSSParamsToMechanism(PLArenaPool *arena,
                                  const SECItem *params,
                                  CK_RSA_PKCS_PSS_PARAMS *mech,
                                  SECOidTag *hashAlgp)
{
    SECOidTag hashAlg;
    SECOidTag maskHashAlg;
    unsigned long saltLength;
    SECStatus rv;

    rv = sec_DecodeRSAPSSParams(arena, params,
                                &hashAlg, &maskHashAlg, &saltLength);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    *hashAlgp = hashAlg;

    mech->hashAlg = sec_GetHashMechanismByOidTag(hashAlg);
    if (mech->hashAlg == CKM_INVALID_MECHANISM) {
        return SECFailure;
    }

    mech->mgf = SEC_GetMgfTypeByOidTag(maskHashAlg);
    if (mech->mgf == 0) {
        return SECFailure;
    }

    mech->sLen = saltLength;

    return SECSuccess;
}
