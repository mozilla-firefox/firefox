
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * This file PKCS #12 fuctions that should really be moved to the
 * PKCS #12 directory, however we can't do that in a point release
 * because that will break binary compatibility, so we keep them here for now.
 */

#include "seccomon.h"
#include "secmod.h"
#include "secmodi.h"
#include "secmodti.h"
#include "secmodt.h"
#include "pkcs11.h"
#include "pk11func.h"
#include "secitem.h"
#include "keyhi.h"
#include "keyi.h"
#include "secoid.h"
#include "secasn1.h"
#include "secerr.h"
#include "prerror.h"
#include "secder.h"

/* These data structures should move to a common .h file shared between the
 * wrappers and the pkcs 12 code. */

/*
** RSA Raw Private Key structures
*/

/* member names from PKCS#1, section 7.2 */
struct SECKEYRSAPrivateKeyStr {
    PLArenaPool *arena;
    SECItem version;
    SECItem modulus;
    SECItem publicExponent;
    SECItem privateExponent;
    SECItem prime1;
    SECItem prime2;
    SECItem exponent1;
    SECItem exponent2;
    SECItem coefficient;
};
typedef struct SECKEYRSAPrivateKeyStr SECKEYRSAPrivateKey;

/*
** DSA Raw Private Key structures
*/

struct SECKEYDSAPrivateKeyStr {
    SECKEYPQGParams params;
    SECItem privateValue;
};
typedef struct SECKEYDSAPrivateKeyStr SECKEYDSAPrivateKey;

/*
** Diffie-Hellman Raw Private Key structures
** Structure member names suggested by PKCS#3.
*/
struct SECKEYDHPrivateKeyStr {
    PLArenaPool *arena;
    SECItem prime;
    SECItem base;
    SECItem privateValue;
};
typedef struct SECKEYDHPrivateKeyStr SECKEYDHPrivateKey;

/*
** Elliptic Curve Private Key structures
** <https://tools.ietf.org/html/rfc5915#section-3>
*/
struct SECKEYECPrivateKeyStr {
    PLArenaPool *arena;
    SECItem version;
    SECItem curveOID;    /* optional/ignored */
    SECItem publicValue; /* required (for now) */
    SECItem privateValue;
};
typedef struct SECKEYECPrivateKeyStr SECKEYECPrivateKey;

struct SECKEYPQSeedPrivateKeyStr {
    SECOidTag params;
    SECItem privateValue;
    SECItem seed;
};
typedef struct SECKEYPQSeedPrivateKeyStr SECKEYPQSeedPrivateKey;
typedef struct SECKEYPQSeedPrivateKeyStr SECKEYMLDSAPrivateKey;
typedef struct SECKEYPQSeedPrivateKeyStr SECKEYMLKEMPrivateKey;

/*
** raw private key object
*/
struct SECKEYRawPrivateKeyStr {
    PLArenaPool *arena;
    KeyType keyType;
    union {
        SECKEYRSAPrivateKey rsa;
        SECKEYDSAPrivateKey dsa;
        SECKEYDHPrivateKey dh;
        SECKEYECPrivateKey ec;
        SECKEYPQSeedPrivateKey pq;
        SECKEYMLDSAPrivateKey mldsa;
        SECKEYMLKEMPrivateKey mlkem;
    } u;
};
typedef struct SECKEYRawPrivateKeyStr SECKEYRawPrivateKey;

SEC_ASN1_MKSUB(SEC_AnyTemplate)
SEC_ASN1_MKSUB(SEC_BitStringTemplate)
SEC_ASN1_MKSUB(SEC_ObjectIDTemplate)
SEC_ASN1_MKSUB(SEC_OctetStringTemplate)
SEC_ASN1_MKSUB(SECOID_AlgorithmIDTemplate)

/* ASN1 Templates for new decoder/encoder */
/*
 * Attribute value for PKCS8 entries (static?)
 */
const SEC_ASN1Template SECKEY_AttributeTemplate[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(SECKEYAttribute) },
    { SEC_ASN1_OBJECT_ID, offsetof(SECKEYAttribute, attrType) },
    { SEC_ASN1_SET_OF | SEC_ASN1_XTRN, offsetof(SECKEYAttribute, attrValue),
      SEC_ASN1_SUB(SEC_AnyTemplate) },
    { 0 }
};

const SEC_ASN1Template SECKEY_SetOfAttributeTemplate[] = {
    { SEC_ASN1_SET_OF, 0, SECKEY_AttributeTemplate },
};

/*
 * keep the publicly exported version one of this template.
 * We use v2 for decode only, since it can decode both v1 and
 * v2. We encode with v1.
 */
const SEC_ASN1Template SECKEY_PrivateKeyInfoTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(SECKEYPrivateKeyInfo) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYPrivateKeyInfo, version) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(SECKEYPrivateKeyInfo, algorithm),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_OCTET_STRING, offsetof(SECKEYPrivateKeyInfo, privateKey) },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED | SEC_ASN1_CONTEXT_SPECIFIC | 0,
      offsetof(SECKEYPrivateKeyInfo, attributes),
      SECKEY_SetOfAttributeTemplate },
    { 0 }
};

/* keep the V2 struct private. In case we need to add new parameters in the future */
struct SECKEYPrivateKeyInfoV2Str {
    SECKEYPrivateKeyInfo v1;
    SECItem publicKey;
};

typedef struct SECKEYPrivateKeyInfoV2Str SECKEYPrivateKeyInfoV2;

/* PrivateKeyInfo is a public value, so we can't add parameters. so we need a new template
 * To hold the publicKey */
static const SEC_ASN1Template seckey_PrivateKeyInfoV2Template[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(SECKEYPrivateKeyInfoV2) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYPrivateKeyInfoV2, v1.version) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(SECKEYPrivateKeyInfoV2, v1.algorithm),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_OCTET_STRING, offsetof(SECKEYPrivateKeyInfoV2, v1.privateKey) },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED | SEC_ASN1_CONTEXT_SPECIFIC | 0,
      offsetof(SECKEYPrivateKeyInfoV2, v1.attributes),
      SECKEY_SetOfAttributeTemplate },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 1,
      offsetof(SECKEYPrivateKeyInfoV2, publicKey),
      SEC_ASN1_SUB(SEC_BitStringTemplate) },
    { 0 }
};

const SEC_ASN1Template SECKEY_PointerToPrivateKeyInfoTemplate[] = {
    { SEC_ASN1_POINTER, 0, SECKEY_PrivateKeyInfoTemplate }
};

const SEC_ASN1Template SECKEY_RSAPrivateKeyExportTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(SECKEYRawPrivateKey) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.rsa.version) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.rsa.modulus) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.rsa.publicExponent) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.rsa.privateExponent) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.rsa.prime1) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.rsa.prime2) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.rsa.exponent1) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.rsa.exponent2) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.rsa.coefficient) },
    { 0 }
};

const SEC_ASN1Template SECKEY_DSAPrivateKeyExportTemplate[] = {
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.dsa.privateValue) },
};

const SEC_ASN1Template SECKEY_DHPrivateKeyExportTemplate[] = {
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.dh.privateValue) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.dh.base) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.dh.prime) },
};

const SEC_ASN1Template SECKEY_PQPrivateKeyBothExportTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(SECKEYRawPrivateKey) },
    { SEC_ASN1_OCTET_STRING, offsetof(SECKEYRawPrivateKey, u.pq.seed) },
    { SEC_ASN1_OCTET_STRING, offsetof(SECKEYRawPrivateKey, u.pq.privateValue) },
    { 0 }
};

const SEC_ASN1Template SECKEY_PQPrivateKeySeedExportTemplate[] = {
    { SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 0, // NOLINT(misc-redundant-expression)
      offsetof(SECKEYRawPrivateKey, u.pq.seed),
      SEC_ASN1_SUB(SEC_OctetStringTemplate) },
    { 0 }
};

const SEC_ASN1Template SECKEY_PQPrivateKeyKeyExportTemplate[] = {
    { SEC_ASN1_OCTET_STRING, offsetof(SECKEYRawPrivateKey, u.pq.privateValue) },
    { 0 }
};

const SEC_ASN1Template SECKEY_ECPrivateKeyExportTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(SECKEYRawPrivateKey) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYRawPrivateKey, u.ec.version) },
    { SEC_ASN1_OCTET_STRING,
      offsetof(SECKEYRawPrivateKey, u.ec.privateValue) },
    /* This value will always be ignored. u.ec.curveOID will always be
     * overriden with the outer AlgorithmID.parameters. */
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED |
          SEC_ASN1_EXPLICIT | SEC_ASN1_CONTEXT_SPECIFIC |
          SEC_ASN1_XTRN | 0,
      offsetof(SECKEYRawPrivateKey, u.ec.curveOID),
      SEC_ASN1_SUB(SEC_ObjectIDTemplate) },
    /* The public value is optional per RFC, but required in NSS. We
     * can't do scalar mult on ECs to get a raw point with PK11 APIs. */
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED |
          SEC_ASN1_EXPLICIT | SEC_ASN1_CONTEXT_SPECIFIC |
          SEC_ASN1_XTRN | 1,
      offsetof(SECKEYRawPrivateKey, u.ec.publicValue),
      SEC_ASN1_SUB(SEC_BitStringTemplate) },
    { 0 }
};

/* The template operates a private key consisting only of private key. */
const SEC_ASN1Template SECKEY_ECRawPrivateKeyTemplate[] = {
    { SEC_ASN1_OCTET_STRING,
      offsetof(SECKEYRawPrivateKey, u.ec.privateValue) },
    { 0 }
};

const SEC_ASN1Template SECKEY_EncryptedPrivateKeyInfoTemplate[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(SECKEYEncryptedPrivateKeyInfo) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(SECKEYEncryptedPrivateKeyInfo, algorithm),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_OCTET_STRING,
      offsetof(SECKEYEncryptedPrivateKeyInfo, encryptedData) },
    { 0 }
};

const SEC_ASN1Template SECKEY_PointerToEncryptedPrivateKeyInfoTemplate[] = {
    { SEC_ASN1_POINTER, 0, SECKEY_EncryptedPrivateKeyInfoTemplate }
};

SEC_ASN1_CHOOSER_IMPLEMENT(SECKEY_EncryptedPrivateKeyInfoTemplate)
SEC_ASN1_CHOOSER_IMPLEMENT(SECKEY_PointerToEncryptedPrivateKeyInfoTemplate)
SEC_ASN1_CHOOSER_IMPLEMENT(SECKEY_PrivateKeyInfoTemplate)
SEC_ASN1_CHOOSER_IMPLEMENT(SECKEY_PointerToPrivateKeyInfoTemplate)

/*
 * See bugzilla bug 125359
 * Since NSS (via PKCS#11) wants to handle big integers as unsigned ints,
 * all of the templates above that en/decode into integers must be converted
 * from ASN.1's signed integer type.  This is done by marking either the
 * source or destination (encoding or decoding, respectively) type as
 * siUnsignedInteger.
 */

static void
prepare_rsa_priv_key_export_for_asn1(SECKEYRawPrivateKey *key)
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

static void
prepare_dsa_priv_key_export_for_asn1(SECKEYRawPrivateKey *key)
{
    key->u.dsa.privateValue.type = siUnsignedInteger;
    key->u.dsa.params.prime.type = siUnsignedInteger;
    key->u.dsa.params.subPrime.type = siUnsignedInteger;
    key->u.dsa.params.base.type = siUnsignedInteger;
}

static void
prepare_dh_priv_key_export_for_asn1(SECKEYRawPrivateKey *key)
{
    key->u.dh.privateValue.type = siUnsignedInteger;
    key->u.dh.prime.type = siUnsignedInteger;
    key->u.dh.base.type = siUnsignedInteger;
}

static void
prepare_ec_priv_key_export_for_asn1(SECKEYRawPrivateKey *key)
{
    key->u.ec.version.type = siUnsignedInteger;
    key->u.ec.curveOID.type = siUnsignedInteger;
    key->u.ec.privateValue.type = siUnsignedInteger;
    key->u.ec.publicValue.type = siUnsignedInteger;
}

/*
 * Finish off a private key we just imported: give it a CKA_ID, and, if it is
 * a token key, a matching public key on the token.
 *
 * publicValue is what the caller had to work with at import time, and may be
 * NULL. If it is, we ask the token to regenerate the public key from the
 * private key and take the public value from that.
 */
static SECStatus
pk11_SetIDAndPublicKey(SECKEYPrivateKey *privKey, const SECItem *publicValue,
                       PRBool isPerm)
{
    SECKEYPublicKey *pubKey = NULL;
    SECItem *ck_id = NULL;
    SECStatus rv = SECFailure;

    if (publicValue == NULL) {
        /* if the token had to derive a public key object to answer this, it
         * belongs to pubKey and goes away with it. */
        pubKey = SECKEY_ConvertToPublicKey(privKey);
        if (pubKey == NULL) {
            goto loser;
        }
        publicValue = PK11_GetPublicValueFromPublicKey(pubKey);
        if (publicValue == NULL) {
            goto loser;
        }
        ck_id = PK11_MakeIDFromPubKey(publicValue);
        if (ck_id == NULL) {
            goto loser;
        }
        rv = PK11_WriteRawAttribute(PK11_TypePrivKey, privKey, CKA_ID, ck_id);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    /* if we are importing a token object, create the corresponding public
     * key, just as PK11_ImportEncryptedPrivateKeyInfoAndReturnKey does. */
    rv = SECSuccess;
    if (isPerm) {
        rv = SECKEY_SetPublicValue(privKey, publicValue);
    }

loser:
    if (pubKey) {
        SECKEY_DestroyPublicKey(pubKey);
    }
    if (ck_id) {
        SECITEM_ZfreeItem(ck_id, PR_TRUE);
    }
    return rv;
}

SECStatus
PK11_ImportDERPrivateKeyInfo(PK11SlotInfo *slot, SECItem *derPKI,
                             SECItem *nickname, const SECItem *publicValue, PRBool isPerm,
                             PRBool isPrivate, unsigned int keyUsage, void *wincx)
{
    return PK11_ImportDERPrivateKeyInfoAndReturnKey(slot, derPKI,
                                                    nickname, publicValue,
                                                    isPerm, isPrivate, keyUsage,
                                                    NULL, wincx);
}

SECKEYPrivateKeyInfo *
_PK11_DERPrivateKeyToPrivateKeyInfo(SECItem *derPKI)
{
    SECKEYPrivateKeyInfoV2 *pkiV2 = NULL;
    PLArenaPool *temparena = NULL;
    SECStatus rv = SECFailure;
    unsigned long version = 0;

    temparena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (!temparena) {
        return NULL;
    }

    pkiV2 = PORT_ArenaZNew(temparena, SECKEYPrivateKeyInfoV2);
    if (!pkiV2) {
        goto loser;
    }
    pkiV2->v1.arena = temparena;

    rv = SEC_ASN1DecodeItem(pkiV2->v1.arena, pkiV2, seckey_PrivateKeyInfoV2Template,
                            derPKI);
    if (rv != SECSuccess) {
        /* If SEC_ASN1DecodeItem fails, we cannot assume anything about the
         * validity of the data in pki. The best we can do is free the arena
         * and return. */
        goto loser;
    }
    /* do some sanity checks on the decoded key */
    if (SEC_ASN1DecodeInteger(&pkiV2->v1.version, &version) != SECSuccess) {
        goto loser; /* error already set */
    }
    if ((version > 1) || ((version == 0) && (pkiV2->publicKey.data != NULL))) {
        PORT_SetError(SEC_ERROR_BAD_DER);
        goto loser;
    }

    return &(pkiV2->v1);

loser:
    PORT_FreeArena(temparena, PR_TRUE);
    return NULL;
}

SECStatus
PK11_ImportDERPrivateKeyInfoAndReturnKey(PK11SlotInfo *slot, SECItem *derPKI,
                                         SECItem *nickname, const SECItem *publicValue,
                                         PRBool isPerm, PRBool isPrivate, unsigned int keyUsage,
                                         SECKEYPrivateKey **privk, void *wincx)
{
    SECKEYPrivateKeyInfo *pki = _PK11_DERPrivateKeyToPrivateKeyInfo(derPKI);
    SECStatus rv = SECFailure;

    if (pki == NULL) {
        return rv;
    }
    if (pki->privateKey.data == NULL || pki->privateKey.len == 0) {
        /* If SEC_ASN1DecodeItems succeeds but SECKEYPrivateKeyInfo.privateKey
         * is a zero-length octet string, free the arena and return a failure
         * to avoid trying to zero the corresponding SECItem in
         * SECKEY_DestroyPrivateKeyInfo(). */
        PORT_FreeArena(pki->arena, PR_TRUE);
        PORT_SetError(SEC_ERROR_BAD_KEY);
        return SECFailure;
    }

    /* if we want to be able to read the public key for v2 decodes, we need
     * a new PK11_Import that takes the V2 struct, and the old entry can
     * create a V2 struct with a NULL public key */
    rv = PK11_ImportPrivateKeyInfoAndReturnKey(slot, pki, nickname,
                                               publicValue, isPerm, isPrivate,
                                               keyUsage, privk, wincx);

    /* this zeroes the key and frees the arena */
    SECKEY_DestroyPrivateKeyInfo(pki, PR_TRUE /*freeit*/);
    return rv;
}

SECStatus
PK11_ImportAndReturnPrivateKey(PK11SlotInfo *slot, SECKEYRawPrivateKey *lpk,
                               SECItem *nickname, const SECItem *publicValue, PRBool isPerm,
                               PRBool isPrivate, unsigned int keyUsage, SECKEYPrivateKey **privk,
                               void *wincx)
{
    CK_BBOOL cktrue = CK_TRUE;
    CK_BBOOL ckfalse = CK_FALSE;
    CK_OBJECT_CLASS keyClass = CKO_PRIVATE_KEY;
    CK_KEY_TYPE keyType = CKK_RSA;
    CK_OBJECT_HANDLE objectID;
    CK_ATTRIBUTE theTemplate[20];
    int templateCount = 0;
    SECStatus rv = SECFailure;
    CK_ATTRIBUTE *attrs;
    CK_ATTRIBUTE *signedattr = NULL;
    int signedcount = 0;
    CK_ATTRIBUTE *ap;
    SECItem *ck_id = NULL;
    CK_ULONG paramSet;
    SECKEYPrivateKey *privKey = NULL;
    /* the public value we end up using: either the caller's or one carried
     * in the PrivateKeyInfo. If we have neither, we leave this NULL and let
     * the token regenerate the public key once the private key is imported */
    const SECItem *pubValue = publicValue;

    attrs = theTemplate;

    PK11_SETATTRS(attrs, CKA_CLASS, &keyClass, sizeof(keyClass));
    attrs++;
    PK11_SETATTRS(attrs, CKA_KEY_TYPE, &keyType, sizeof(keyType));
    attrs++;
    PK11_SETATTRS(attrs, CKA_TOKEN, isPerm ? &cktrue : &ckfalse,
                  sizeof(CK_BBOOL));
    attrs++;
    PK11_SETATTRS(attrs, CKA_SENSITIVE, isPrivate ? &cktrue : &ckfalse,
                  sizeof(CK_BBOOL));
    attrs++;
    PK11_SETATTRS(attrs, CKA_PRIVATE, isPrivate ? &cktrue : &ckfalse,
                  sizeof(CK_BBOOL));
    attrs++;

    switch (lpk->keyType) {
        case rsaKey:
            keyType = CKK_RSA;
            PK11_SETATTRS(attrs, CKA_UNWRAP, (keyUsage & KU_KEY_ENCIPHERMENT) ? &cktrue : &ckfalse,
                          sizeof(CK_BBOOL));
            attrs++;
            PK11_SETATTRS(attrs, CKA_DECRYPT, (keyUsage & KU_DATA_ENCIPHERMENT) ? &cktrue : &ckfalse,
                          sizeof(CK_BBOOL));
            attrs++;
            PK11_SETATTRS(attrs, CKA_SIGN, (keyUsage & KU_DIGITAL_SIGNATURE) ? &cktrue : &ckfalse,
                          sizeof(CK_BBOOL));
            attrs++;
            PK11_SETATTRS(attrs, CKA_SIGN_RECOVER,
                          (keyUsage & KU_DIGITAL_SIGNATURE) ? &cktrue
                                                            : &ckfalse,
                          sizeof(CK_BBOOL));
            attrs++;
            pubValue = &lpk->u.rsa.modulus;
            ck_id = PK11_MakeIDFromPubKey(pubValue);
            if (ck_id == NULL) {
                goto loser;
            }
            PK11_SETATTRS(attrs, CKA_ID, ck_id->data, ck_id->len);
            attrs++;
            if (nickname) {
                PK11_SETATTRS(attrs, CKA_LABEL, nickname->data, nickname->len);
                attrs++;
            }
            signedattr = attrs;
            PK11_SETATTRS(attrs, CKA_MODULUS, lpk->u.rsa.modulus.data,
                          lpk->u.rsa.modulus.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_PUBLIC_EXPONENT,
                          lpk->u.rsa.publicExponent.data,
                          lpk->u.rsa.publicExponent.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_PRIVATE_EXPONENT,
                          lpk->u.rsa.privateExponent.data,
                          lpk->u.rsa.privateExponent.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_PRIME_1,
                          lpk->u.rsa.prime1.data,
                          lpk->u.rsa.prime1.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_PRIME_2,
                          lpk->u.rsa.prime2.data,
                          lpk->u.rsa.prime2.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_EXPONENT_1,
                          lpk->u.rsa.exponent1.data,
                          lpk->u.rsa.exponent1.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_EXPONENT_2,
                          lpk->u.rsa.exponent2.data,
                          lpk->u.rsa.exponent2.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_COEFFICIENT,
                          lpk->u.rsa.coefficient.data,
                          lpk->u.rsa.coefficient.len);
            attrs++;
            break;
        case dsaKey:
            keyType = CKK_DSA;
            PK11_SETATTRS(attrs, CKA_SIGN, &cktrue, sizeof(CK_BBOOL));
            attrs++;
            PK11_SETATTRS(attrs, CKA_SIGN_RECOVER, &ckfalse, sizeof(CK_BBOOL));
            attrs++;
            if (nickname) {
                PK11_SETATTRS(attrs, CKA_LABEL, nickname->data, nickname->len);
                attrs++;
            }
            /* To make our intenal PKCS #11 module work correctly with
             * our database, we need to pass in the public key value for
             * this dsa key. We have a netscape only CKA_ value to do this.
             * Only send it to internal slots */
            if (pubValue != NULL) {
                if (PK11_IsInternal(slot)) {
                    PK11_SETATTRS(attrs, CKA_NSS_DB,
                                  pubValue->data, pubValue->len);
                    attrs++;
                }
                ck_id = PK11_MakeIDFromPubKey(pubValue);
                if (ck_id == NULL) {
                    goto loser;
                }
                PK11_SETATTRS(attrs, CKA_ID, ck_id->data, ck_id->len);
                attrs++;
            }
            signedattr = attrs;
            PK11_SETATTRS(attrs, CKA_PRIME, lpk->u.dsa.params.prime.data,
                          lpk->u.dsa.params.prime.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_SUBPRIME, lpk->u.dsa.params.subPrime.data,
                          lpk->u.dsa.params.subPrime.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_BASE, lpk->u.dsa.params.base.data,
                          lpk->u.dsa.params.base.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_VALUE, lpk->u.dsa.privateValue.data,
                          lpk->u.dsa.privateValue.len);
            attrs++;
            break;
        case dhKey:
            keyType = CKK_DH;
            PK11_SETATTRS(attrs, CKA_DERIVE, &cktrue, sizeof(CK_BBOOL));
            attrs++;
            if (nickname) {
                PK11_SETATTRS(attrs, CKA_LABEL, nickname->data, nickname->len);
                attrs++;
            }
            /* To make our intenal PKCS #11 module work correctly with
             * our database, we need to pass in the public key value for
             * this dh key. We have a netscape only CKA_ value to do this.
             * Only send it to internal slots */
            if (pubValue != NULL) {
                if (PK11_IsInternal(slot)) {
                    PK11_SETATTRS(attrs, CKA_NSS_DB,
                                  pubValue->data, pubValue->len);
                    attrs++;
                }
                ck_id = PK11_MakeIDFromPubKey(pubValue);
                if (ck_id == NULL) {
                    goto loser;
                }
                PK11_SETATTRS(attrs, CKA_ID, ck_id->data, ck_id->len);
                attrs++;
            }
            signedattr = attrs;
            PK11_SETATTRS(attrs, CKA_PRIME, lpk->u.dh.prime.data,
                          lpk->u.dh.prime.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_BASE, lpk->u.dh.base.data,
                          lpk->u.dh.base.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_VALUE, lpk->u.dh.privateValue.data,
                          lpk->u.dh.privateValue.len);
            attrs++;
            break;
        case ecKey:
            keyType = CKK_EC;
            PK11_SETATTRS(attrs, CKA_SIGN, (keyUsage & KU_DIGITAL_SIGNATURE) ? &cktrue : &ckfalse,
                          sizeof(CK_BBOOL));
            attrs++;
            PK11_SETATTRS(attrs, CKA_SIGN_RECOVER, &ckfalse,
                          sizeof(CK_BBOOL));
            attrs++;
            PK11_SETATTRS(attrs, CKA_DERIVE, (keyUsage & KU_KEY_AGREEMENT) ? &cktrue : &ckfalse,
                          sizeof(CK_BBOOL));
            attrs++;
            if (nickname) {
                PK11_SETATTRS(attrs, CKA_LABEL, nickname->data, nickname->len);
                attrs++;
            }
            /* the public value is optional in an ECPrivateKey. Without it we
             * have to wait until the key is on the token and let the token
             * regenerate the public key for us (below). */
            if (lpk->u.ec.publicValue.len != 0) {
                pubValue = &lpk->u.ec.publicValue;
            }
            if (pubValue != NULL) {
                if (PK11_IsInternal(slot)) {
                    PK11_SETATTRS(attrs, CKA_NSS_DB,
                                  pubValue->data, pubValue->len);
                    attrs++;
                }
                ck_id = PK11_MakeIDFromPubKey(pubValue);
                if (ck_id == NULL) {
                    goto loser;
                }
                PK11_SETATTRS(attrs, CKA_ID, ck_id->data, ck_id->len);
                attrs++;
                PK11_SETATTRS(attrs, CKA_EC_POINT, pubValue->data,
                              pubValue->len);
                attrs++;
            }
            /* No signed attrs for EC */
            /* curveOID always is a copy of AlgorithmID.parameters. */
            PK11_SETATTRS(attrs, CKA_EC_PARAMS, lpk->u.ec.curveOID.data,
                          lpk->u.ec.curveOID.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_VALUE, lpk->u.ec.privateValue.data,
                          lpk->u.ec.privateValue.len);
            attrs++;
            break;
        case edKey:
            keyType = CKK_EC_EDWARDS;
            PK11_SETATTRS(attrs, CKA_SIGN, &cktrue, sizeof(CK_BBOOL));
            attrs++;
            if (nickname) {
                PK11_SETATTRS(attrs, CKA_LABEL, nickname->data, nickname->len);
                attrs++;
            }
            /* a CurvePrivateKey carries no public value, so unless the caller
             * handed us one we have to let the token regenerate the public
             * key for us (below). */
            if (pubValue != NULL) {
                ck_id = PK11_MakeIDFromPubKey(pubValue);
                if (ck_id == NULL) {
                    goto loser;
                }
                PK11_SETATTRS(attrs, CKA_ID, ck_id->data, ck_id->len);
                attrs++;
            }

            /* No signed attrs for EC */
            /* curveOID always is a copy of AlgorithmID.parameters. */
            PK11_SETATTRS(attrs, CKA_EC_PARAMS, lpk->u.ec.curveOID.data,
                          lpk->u.ec.curveOID.len);
            attrs++;
            PK11_SETATTRS(attrs, CKA_VALUE, lpk->u.ec.privateValue.data,
                          lpk->u.ec.privateValue.len);
            attrs++;
            break;
        case ecMontKey:
            keyType = CKK_EC_MONTGOMERY;

            PK11_SETATTRS(attrs, CKA_DERIVE, &cktrue, sizeof(CK_BBOOL));
            attrs++;

            if (nickname) {
                PK11_SETATTRS(attrs, CKA_LABEL, nickname->data, nickname->len);
                attrs++;
            }
            /* a CurvePrivateKey carries no public value, so unless the caller
             * handed us one we have to let the token regenerate the public
             * key for us (below). */
            if (pubValue != NULL) {
                ck_id = PK11_MakeIDFromPubKey(pubValue);
                if (ck_id == NULL) {
                    goto loser;
                }
                PK11_SETATTRS(attrs, CKA_ID, ck_id->data, ck_id->len);
                attrs++;
            }

            /* No signed attrs for EC */
            /* curveOID always is a copy of AlgorithmID.parameters. */
            PK11_SETATTRS(attrs, CKA_EC_PARAMS, lpk->u.ec.curveOID.data,
                          lpk->u.ec.curveOID.len);
            attrs++;

            PK11_SETATTRS(attrs, CKA_VALUE, lpk->u.ec.privateValue.data,
                          lpk->u.ec.privateValue.len);
            attrs++;
            break;
        case mldsaKey:
            keyType = CKK_ML_DSA;
            /* we need at least one of these to import into PKCS #11.
             * if we have only one, it may still fail, but that is up
             * to the token */
            if ((lpk->u.mldsa.seed.len == 0) &&
                (lpk->u.mldsa.privateValue.len == 0)) {
                PORT_SetError(SEC_ERROR_BAD_KEY);
                goto loser;
            }
            PK11_SETATTRS(attrs, CKA_SIGN, &cktrue, sizeof(CK_BBOOL));
            attrs++;
            PK11_SETATTRS(attrs, CKA_SIGN_RECOVER, &ckfalse, sizeof(CK_BBOOL));
            attrs++;
            /* the public value is optional in an ML-DSA PrivateKey. Without
             * it we have to wait until the key is on the token and let the
             * token regenerate the public key for us (below). CKA_NSS_DB is
             * an artifact of the old dbm database, which never supported
             * ML-DSA, so we don't set it here. */
            if (pubValue != NULL) {
                ck_id = PK11_MakeIDFromPubKey(pubValue);
                if (ck_id == NULL) {
                    goto loser;
                }
                PK11_SETATTRS(attrs, CKA_ID, ck_id->data, ck_id->len);
                attrs++;
            }
            if (nickname) {
                PK11_SETATTRS(attrs, CKA_LABEL, nickname->data, nickname->len);
                attrs++;
            }
            paramSet = SECKEY_GetMLDSAPkcs11ParamSetByOidTag(lpk->u.mldsa.params);
            PK11_SETATTRS(attrs, CKA_PARAMETER_SET, (unsigned char *)&paramSet,
                          sizeof(CK_ML_DSA_PARAMETER_SET_TYPE));
            attrs++;
            if (lpk->u.mldsa.seed.len) {
                PK11_SETATTRS(attrs, CKA_SEED, lpk->u.mldsa.seed.data,
                              lpk->u.mldsa.seed.len);
                attrs++;
            }
            if (lpk->u.mldsa.privateValue.len) {
                PK11_SETATTRS(attrs, CKA_VALUE, lpk->u.mldsa.privateValue.data,
                              lpk->u.mldsa.privateValue.len);
                attrs++;
            }
            break;
        case kyberKey:
            keyType = CKK_ML_KEM;
            /* we need at least one of these to import into PKCS #11.
             * if we have only one, it may still fail, but that is up
             * to the token */
            if ((lpk->u.mlkem.seed.len == 0) &&
                (lpk->u.mlkem.privateValue.len == 0)) {
                PORT_SetError(SEC_ERROR_BAD_KEY);
                goto loser;
            }
            PK11_SETATTRS(attrs, CKA_DECAPSULATE, &cktrue, sizeof(CK_BBOOL));
            attrs++;
            /* the public value is optional in an ML-KEM PrivateKey. Without
             * it we have to wait until the key is on the token and let the
             * token regenerate the public key for us (below). CKA_NSS_DB is
             * an artifact of the old dbm database, which never supported
             * ML-KEM, so we don't set it here. */
            if (pubValue != NULL) {
                ck_id = PK11_MakeIDFromPubKey(pubValue);
                if (ck_id == NULL) {
                    goto loser;
                }
                PK11_SETATTRS(attrs, CKA_ID, ck_id->data, ck_id->len);
                attrs++;
            }
            if (nickname) {
                PK11_SETATTRS(attrs, CKA_LABEL, nickname->data, nickname->len);
                attrs++;
            }
            paramSet = seckey_GetMLKEMPkcs11ParamsByKyberParams(
                seckey_GetKyberParamsByOidTag(lpk->u.mlkem.params));
            PK11_SETATTRS(attrs, CKA_PARAMETER_SET, (unsigned char *)&paramSet,
                          sizeof(CK_ML_KEM_PARAMETER_SET_TYPE));
            attrs++;
            if (lpk->u.mlkem.seed.len) {
                PK11_SETATTRS(attrs, CKA_SEED, lpk->u.mlkem.seed.data,
                              lpk->u.mlkem.seed.len);
                attrs++;
            }
            if (lpk->u.mlkem.privateValue.len) {
                PK11_SETATTRS(attrs, CKA_VALUE, lpk->u.mlkem.privateValue.data,
                              lpk->u.mlkem.privateValue.len);
                attrs++;
            }
            break;
        default:
            PORT_SetError(SEC_ERROR_BAD_KEY);
            goto loser;
    }
    templateCount = attrs - theTemplate;
    PORT_Assert(templateCount <= sizeof(theTemplate) / sizeof(CK_ATTRIBUTE));
    /* we used to assert unless the key didn't need signedattrs, but that's
     * now true of almost all modern keys, so now if the key has signedattrs
     * the just need to set the value */
    if (signedattr) {
        signedcount = attrs - signedattr;
        for (ap = signedattr; signedcount; ap++, signedcount--) {
            pk11_SignedToUnsigned(ap);
        }
    }

    rv = PK11_CreateNewObject(slot, CK_INVALID_HANDLE,
                              theTemplate, templateCount, isPerm, &objectID);
    if (rv != SECSuccess) {
        goto loser;
    }

    privKey = pk11_MakePrivKey(slot, lpk->keyType, !isPerm, objectID, wincx);
    if (privKey == NULL) {
        rv = SECFailure;
        goto loser;
    }

    /* Best effort. A token needn't be able to regenerate a public key from a
     * private one (softoken can only do it for ML-DSA if it has the seed),
     * and it needn't support permanent public keys either. Without a CKA_ID
     * the key can't be found again, but it is still usable through the
     * handle we return here. */
    (void)pk11_SetIDAndPublicKey(privKey, pubValue, isPerm);

    if (privk != NULL) {
        *privk = privKey;
        privKey = NULL;
    }
loser:
    if (privKey) {
        SECKEY_DestroyPrivateKey(privKey);
    }
    if (ck_id) {
        SECITEM_ZfreeItem(ck_id, PR_TRUE);
    }
    return rv;
}

SECStatus
PK11_ImportPrivateKeyInfoAndReturnKey(PK11SlotInfo *slot,
                                      SECKEYPrivateKeyInfo *pki, SECItem *nickname, const SECItem *publicValue,
                                      PRBool isPerm, PRBool isPrivate, unsigned int keyUsage,
                                      SECKEYPrivateKey **privk, void *wincx)
{
    SECStatus rv = SECFailure;
    SECKEYRawPrivateKey *lpk = NULL;
    const SEC_ASN1Template *keyTemplate, *paramTemplate;
    void *paramDest = NULL;
    PLArenaPool *arena = NULL;
    SECOidTag algTag;

    arena = PORT_NewArena(2048);
    if (!arena) {
        return SECFailure;
    }

    /* need to change this to use RSA/DSA keys */
    lpk = (SECKEYRawPrivateKey *)PORT_ArenaZAlloc(arena,
                                                  sizeof(SECKEYRawPrivateKey));
    if (lpk == NULL) {
        goto loser;
    }
    lpk->arena = arena;

    algTag = SECOID_GetAlgorithmTag(&pki->algorithm);
    switch (algTag) {
        case SEC_OID_PKCS1_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_RSA_PSS_SIGNATURE:
            /* An id-RSASSA-PSS PrivateKeyInfo wraps an ordinary RSAPrivateKey,
             * identical to rsaEncryption; only the algorithm OID (and optional
             * PSS parameters, which don't affect the raw key) differ. NSS
             * represents RSA-PSS keys as rsaKey/CKK_RSA, so decode it as RSA. */
            prepare_rsa_priv_key_export_for_asn1(lpk);
            keyTemplate = SECKEY_RSAPrivateKeyExportTemplate;
            paramTemplate = NULL;
            paramDest = NULL;
            lpk->keyType = rsaKey;
            break;
        case SEC_OID_ANSIX9_DSA_SIGNATURE:
            prepare_dsa_priv_key_export_for_asn1(lpk);
            keyTemplate = SECKEY_DSAPrivateKeyExportTemplate;
            paramTemplate = SECKEY_PQGParamsTemplate;
            paramDest = &(lpk->u.dsa.params);
            lpk->keyType = dsaKey;
            break;
        case SEC_OID_X942_DIFFIE_HELMAN_KEY:
            if (!publicValue) {
                goto loser;
            }
            prepare_dh_priv_key_export_for_asn1(lpk);
            keyTemplate = SECKEY_DHPrivateKeyExportTemplate;
            paramTemplate = NULL;
            paramDest = NULL;
            lpk->keyType = dhKey;
            break;
        case SEC_OID_ED25519_PUBLIC_KEY:
            keyTemplate = SECKEY_ECRawPrivateKeyTemplate;
            paramTemplate = NULL;
            paramDest = NULL;
            lpk->keyType = edKey;
            break;
        case SEC_OID_X25519:
            keyTemplate = SECKEY_ECRawPrivateKeyTemplate;
            paramTemplate = NULL;
            paramDest = NULL;
            lpk->keyType = ecMontKey;
            break;
        case SEC_OID_ANSIX962_EC_PUBLIC_KEY:
            prepare_ec_priv_key_export_for_asn1(lpk);
            keyTemplate = SECKEY_ECPrivateKeyExportTemplate;
            paramTemplate = NULL;
            paramDest = NULL;
            lpk->keyType = ecKey;
            break;
        case SEC_OID_ML_DSA_44:
        case SEC_OID_ML_DSA_65:
        case SEC_OID_ML_DSA_87:
            lpk->keyType = mldsaKey;
            lpk->u.mldsa.params = algTag;
            goto handlePQKey;
        case SEC_OID_ML_KEM_512:
        case SEC_OID_ML_KEM_768:
        case SEC_OID_ML_KEM_1024:
            lpk->keyType = kyberKey;
            lpk->u.mlkem.params = algTag;
        handlePQKey:
            /* RFC 9881 and RFC 9935: the parameters MUST be absent. */
            if (pki->algorithm.parameters.len != 0) {
                PORT_SetError(SEC_ERROR_INVALID_ALGORITHM);
                goto loser;
            }
            if (pki->privateKey.data == NULL || pki->privateKey.len == 0) {
                PORT_SetError(SEC_ERROR_BAD_KEY);
                goto loser;
            }
            /* choice */
            switch (pki->privateKey.data[0]) {
                case SEC_ASN1_CONTEXT_SPECIFIC | 0:
                    keyTemplate = SECKEY_PQPrivateKeySeedExportTemplate;
                    break;
                case SEC_ASN1_OCTET_STRING:
                    keyTemplate = SECKEY_PQPrivateKeyKeyExportTemplate;
                    break;
                case SEC_ASN1_CONSTRUCTED | SEC_ASN1_SEQUENCE:
                    keyTemplate = SECKEY_PQPrivateKeyBothExportTemplate;
                    break;
                default:
                    keyTemplate = NULL;
                    PORT_SetError(SEC_ERROR_BAD_DER);
                    break;
            }
            paramTemplate = NULL;
            paramDest = NULL;
            break;
        default:
            keyTemplate = NULL;
            paramTemplate = NULL;
            paramDest = NULL;
            break;
    }

    if (!keyTemplate) {
        goto loser;
    }

    /* decode the private key and any algorithm parameters */
    rv = SEC_QuickDERDecodeItem(arena, lpk, keyTemplate, &pki->privateKey);
    if (rv != SECSuccess) {
        goto loser;
    }

    if (lpk->keyType == ecKey) {
        /* Convert length in bits to length in bytes. */
        lpk->u.ec.publicValue.len >>= 3;

        /* Always override curveOID, we're ignoring any given value. */
        rv = SECITEM_CopyItem(arena, &lpk->u.ec.curveOID,
                              &pki->algorithm.parameters);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    if (lpk->keyType == edKey || lpk->keyType == ecMontKey) {
        /* SECKEY_ECRawPrivateKeyTemplate (used for both key types) does not reference
           publicKey, curveOID, ec verion. */
        if (pki->algorithm.parameters.len != 0) {
            /* Currently supporting only (Pure)Ed25519/X25519 .*/
            PORT_SetError(SEC_ERROR_UNSUPPORTED_KEYALG);
            goto loser;
        }

        SECOidData *oid25519 = SECOID_FindOIDByTag(SECOID_GetAlgorithmTag(&pki->algorithm));
        if (!oid25519) {
            goto loser;
        }

        if (!SECITEM_AllocItem(arena, &lpk->u.ec.curveOID, oid25519->oid.len + 2)) {
            goto loser;
        }
        lpk->u.ec.curveOID.data[0] = SEC_ASN1_OBJECT_ID;
        lpk->u.ec.curveOID.data[1] = oid25519->oid.len;
        PORT_Memcpy(lpk->u.ec.curveOID.data + 2, oid25519->oid.data, oid25519->oid.len);
    }

    if (paramDest && paramTemplate) {
        rv = SEC_ASN1DecodeItem(arena, paramDest, paramTemplate,
                                &(pki->algorithm.parameters));
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    rv = PK11_ImportAndReturnPrivateKey(slot, lpk, nickname, publicValue, isPerm,
                                        isPrivate, keyUsage, privk, wincx);
    if (rv != SECSuccess) {
        goto loser;
    }
    PORT_FreeArena(arena, PR_TRUE);
    return SECSuccess;

loser:
    PORT_FreeArena(arena, PR_TRUE);
    return SECFailure;
}

SECStatus
PK11_ImportPrivateKeyInfo(PK11SlotInfo *slot, SECKEYPrivateKeyInfo *pki,
                          SECItem *nickname, const SECItem *publicValue, PRBool isPerm,
                          PRBool isPrivate, unsigned int keyUsage, void *wincx)
{
    return PK11_ImportPrivateKeyInfoAndReturnKey(slot, pki, nickname,
                                                 publicValue, isPerm, isPrivate, keyUsage, NULL, wincx);
}

SECItem *
PK11_ExportDERPrivateKeyInfo(SECKEYPrivateKey *pk, void *wincx)
{
    SECKEYPrivateKeyInfo *pki = PK11_ExportPrivKeyInfo(pk, wincx);
    SECItem *derPKI;

    if (!pki) {
        return NULL;
    }
    derPKI = SEC_ASN1EncodeItem(NULL, NULL, pki,
                                SECKEY_PrivateKeyInfoTemplate);
    SECKEY_DestroyPrivateKeyInfo(pki, PR_TRUE);
    return derPKI;
}

static PRBool
ReadAttribute(SECKEYPrivateKey *key, CK_ATTRIBUTE_TYPE type,
              PLArenaPool *arena, SECItem *output)
{
    SECStatus rv = PK11_ReadAttribute(key->pkcs11Slot, key->pkcs11ID, type,
                                      arena, output);
    return rv == SECSuccess;
}

/*
 * The caller is responsible for freeing the return value by passing it to
 * SECKEY_DestroyPrivateKeyInfo(..., PR_TRUE).
 */
SECKEYPrivateKeyInfo *
PK11_ExportPrivKeyInfo(SECKEYPrivateKey *pk, void *wincx)
{
    /* PrivateKeyInfo version (always zero) */
    const unsigned char pkiVersion = 0;
    /* RSAPrivateKey version (always zero) */
    const unsigned char rsaVersion = 0;
    /* ECPrivateKey version (always one) */
    const unsigned char ecVersion = 1;
    PLArenaPool *arena = NULL;
    SECKEYRawPrivateKey rawKey;
    SECKEYPrivateKeyInfo *pki;
    SECItem *encoded;
    const SEC_ASN1Template *keyTemplate;
    SECStatus rv;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (!arena) {
        goto loser;
    }
    memset(&rawKey, 0, sizeof(rawKey));
    rawKey.keyType = pk->keyType;
    pki = PORT_ArenaZNew(arena, SECKEYPrivateKeyInfo);
    if (!pki) {
        goto loser;
    }

    switch (pk->keyType) {
        case rsaKey: {
            rawKey.u.rsa.version.type = siUnsignedInteger;
            rawKey.u.rsa.version.data = (unsigned char *)PORT_ArenaAlloc(arena, 1);
            if (!rawKey.u.rsa.version.data) {
                goto loser;
            }

            rawKey.u.rsa.version.data[0] = rsaVersion;
            rawKey.u.rsa.version.len = 1;

            /* Read the component attributes of the private key */
            prepare_rsa_priv_key_export_for_asn1(&rawKey);
            if (!ReadAttribute(pk, CKA_MODULUS, arena, &rawKey.u.rsa.modulus) ||
                !ReadAttribute(pk, CKA_PUBLIC_EXPONENT, arena,
                               &rawKey.u.rsa.publicExponent) ||
                !ReadAttribute(pk, CKA_PRIVATE_EXPONENT, arena,
                               &rawKey.u.rsa.privateExponent) ||
                !ReadAttribute(pk, CKA_PRIME_1, arena, &rawKey.u.rsa.prime1) ||
                !ReadAttribute(pk, CKA_PRIME_2, arena, &rawKey.u.rsa.prime2) ||
                !ReadAttribute(pk, CKA_EXPONENT_1, arena,
                               &rawKey.u.rsa.exponent1) ||
                !ReadAttribute(pk, CKA_EXPONENT_2, arena,
                               &rawKey.u.rsa.exponent2) ||
                !ReadAttribute(pk, CKA_COEFFICIENT, arena,
                               &rawKey.u.rsa.coefficient)) {
                goto loser;
            }

            keyTemplate = SECKEY_RSAPrivateKeyExportTemplate;

            rv = SECOID_SetAlgorithmID(arena, &pki->algorithm, SEC_OID_PKCS1_RSA_ENCRYPTION, NULL);
            if (rv != SECSuccess) {
                goto loser;
            }

        } break;
        case ecKey: {
            rawKey.u.ec.version.type = siUnsignedInteger;
            rawKey.u.ec.version.data = (unsigned char *)PORT_ArenaAlloc(arena, 1);
            if (!rawKey.u.ec.version.data) {
                goto loser;
            }
            rawKey.u.ec.version.data[0] = ecVersion;
            rawKey.u.ec.version.len = 1;

            SECItem curveOID;
            /* Read the component attributes of the private key */
            prepare_ec_priv_key_export_for_asn1(&rawKey);
            if (!ReadAttribute(pk, CKA_VALUE, arena,
                               &rawKey.u.ec.privateValue) ||
                !ReadAttribute(pk, CKA_EC_PARAMS, arena, &curveOID)) {
                goto loser;
            }
            if (!ReadAttribute(pk, CKA_EC_POINT, arena,
                               &rawKey.u.ec.publicValue)) {
                SECKEYPublicKey *pubk = SECKEY_ConvertToPublicKey(pk);
                if (pubk == NULL)
                    goto loser;
                rv = SECITEM_CopyItem(arena, &rawKey.u.ec.publicValue, &pubk->u.ec.publicValue);
                SECKEY_DestroyPublicKey(pubk);
                if (rv != SECSuccess) {
                    goto loser;
                }
            }

            keyTemplate = SECKEY_ECPrivateKeyExportTemplate;
            /* Convert length in bytes to length in bits. */
            rawKey.u.ec.publicValue.len <<= 3;

            rv = SECOID_SetAlgorithmID(arena, &pki->algorithm, SEC_OID_ANSIX962_EC_PUBLIC_KEY, &curveOID);
            if (rv != SECSuccess) {
                goto loser;
            }

        } break;
        case edKey: {
            if (!ReadAttribute(pk, CKA_VALUE, arena,
                               &rawKey.u.ec.privateValue)) {
                goto loser;
            }

            keyTemplate = SECKEY_ECRawPrivateKeyTemplate;
            /* Currently, Ed25519 does not support any parameter.  */
            rv = SECOID_SetAlgorithmID(arena, &pki->algorithm, SEC_OID_ED25519_PUBLIC_KEY, NULL);
            if (rv != SECSuccess) {
                goto loser;
            }
        } break;
        case ecMontKey: {
            if (!ReadAttribute(pk, CKA_VALUE, arena,
                               &rawKey.u.ec.privateValue)) {
                goto loser;
            }

            keyTemplate = SECKEY_ECRawPrivateKeyTemplate;
            /* Currently, X25519 does not support any parameter.  */
            rv = SECOID_SetAlgorithmID(arena, &pki->algorithm, SEC_OID_X25519, NULL);
            if (rv != SECSuccess) {
                goto loser;
            }
        } break;
        case mldsaKey:
        case kyberKey: {
            SECOidTag paramSet = seckey_GetParameterSet(pk);
            if (paramSet == SEC_OID_UNKNOWN) {
                PORT_SetError(SEC_ERROR_INVALID_ALGORITHM);
                goto loser;
            }

            /* Prefer the seed CHOICE; fall back to expandedKey without one. */
            if (ReadAttribute(pk, CKA_SEED, arena, &rawKey.u.pq.seed) &&
                rawKey.u.pq.seed.len != 0) {
                keyTemplate = SECKEY_PQPrivateKeySeedExportTemplate;
            } else {
                if (!ReadAttribute(pk, CKA_VALUE, arena,
                                   &rawKey.u.pq.privateValue)) {
                    goto loser;
                }
                if (rawKey.u.pq.privateValue.len == 0) {
                    PORT_SetError(SEC_ERROR_BAD_KEY);
                    goto loser;
                }
                keyTemplate = SECKEY_PQPrivateKeyKeyExportTemplate;
            }

            rv = SECOID_SetAlgorithmID(arena, &pki->algorithm, paramSet, NULL);
            if (rv != SECSuccess) {
                goto loser;
            }
        } break;
        default: {
            PORT_SetError(PR_NOT_IMPLEMENTED_ERROR);
            goto loser;
        }
    }

    encoded = SEC_ASN1EncodeItem(arena, &pki->privateKey, &rawKey, keyTemplate);
    if (!encoded) {
        goto loser;
    }
    pki->version.type = siUnsignedInteger;
    pki->version.data = (unsigned char *)PORT_ArenaAlloc(arena, 1);
    if (!pki->version.data) {
        goto loser;
    }
    pki->version.data[0] = pkiVersion;
    pki->version.len = 1;
    pki->arena = arena;

    return pki;

loser:
    if (arena) {
        PORT_FreeArena(arena, PR_TRUE);
    }
    return NULL;
}
