/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 *  The following code handles the storage of PKCS 11 modules used by the
 * NSS. For the rest of NSS, only one kind of database handle exists:
 *
 *     SFTKDBHandle
 *
 * There is one SFTKDBHandle for the each key database and one for each cert
 * database. These databases are opened as associated pairs, one pair per
 * slot. SFTKDBHandles are reference counted objects.
 *
 * Each SFTKDBHandle points to a low level database handle (SDB). This handle
 * represents the underlying physical database. These objects are not
 * reference counted, an are 'owned' by their respective SFTKDBHandles.
 *
 *
 */
#include "sftkdb.h"
#include "sftkdbti.h"
#include "pkcs11t.h"
#include "pkcs11i.h"
#include "sdb.h"
#include "prprf.h"
#include "secasn1.h"
#include "pratom.h"
#include "blapi.h"
#include "secoid.h"
#include "lowpbe.h"
#include "secdert.h"
#include "prsystem.h"
#include "lgglue.h"
#include "secerr.h"
#include "softoken.h"

static const int NSS_MP_PBE_ITERATION_COUNT = 10000;

static int
getPBEIterationCount(void)
{
    int c = NSS_MP_PBE_ITERATION_COUNT;

    char *val = getenv("NSS_MIN_MP_PBE_ITERATION_COUNT");
    if (val && *val) {
        int minimum = atoi(val);
        if (c < minimum) {
            c = minimum;
        }
    }

    val = getenv("NSS_MAX_MP_PBE_ITERATION_COUNT");
    if (val && *val) {
        int maximum = atoi(val);
        if (c > maximum) {
            c = maximum;
        }
    }
    /* never let c be less than one, no matter what the environment
     * variable is set to */
    if (c < 1) {
        c = 1;
    }
    return c;
}

PRBool
sftk_isLegacyIterationCountAllowed(void)
{
    static const char *legacyCountEnvVar =
        "NSS_ALLOW_LEGACY_DBM_ITERATION_COUNT";
    char *iterEnv = getenv(legacyCountEnvVar);
    return (iterEnv && strcmp("0", iterEnv) != 0);
}

/******************************************************************
 *
 * Key DB password handling functions
 *
 * These functions manage the key db password (set, reset, initialize, use).
 *
 * The key is managed on 'this side' of the database. All private data is
 * encrypted before it is sent to the database itself. Besides PBE's, the
 * database management code can also mix in various fixed keys so the data
 * in the database is no longer considered 'plain text'.
 */

/* take string password and turn it into a key. The key is dependent
 * on a global salt entry acquired from the database. This salted
 * value will be based to a pkcs5 pbe function before it is used
 * in an actual encryption */
static SECStatus
sftkdb_passwordToKey(SFTKDBHandle *keydb, SECItem *salt,
                     const char *pw, SECItem *key)
{
    HASH_HashType hType;
    const SECHashObject *hashObj;
    void *ctx = NULL;
    SECStatus rv = SECFailure;

    hType = salt->len == SHA384_LENGTH ? HASH_AlgSHA384 : HASH_AlgSHA1;
    hashObj = HASH_GetRawHashObject(hType);

    if (!pw) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    key->data = PORT_Alloc(hashObj->length);
    if (key->data == NULL) {
        goto loser;
    }
    key->len = hashObj->length;

    ctx = hashObj->create();
    if (ctx == NULL) {
        goto loser;
    }
    hashObj->begin(ctx);
    if (salt && salt->data) {
        hashObj->update(ctx, salt->data, salt->len);
    }
    hashObj->update(ctx, (unsigned char *)pw, PORT_Strlen(pw));
    hashObj->end(ctx, key->data, &key->len, key->len);
    rv = SECSuccess;

loser:
    if (ctx) {
        hashObj->destroy(ctx, PR_TRUE);
    }
    if (rv != SECSuccess) {
        if (key->data != NULL) {
            PORT_ZFree(key->data, key->len);
        }
        key->data = NULL;
    }
    return rv;
}

/*
 * Cipher text stored in the database contains 3 elements:
 * 1) an identifier describing the encryption algorithm.
 * 2) an entry specific salt value.
 * 3) the encrypted value.
 *
 * The following data structure represents the encrypted data in a decoded
 * (but still encrypted) form.
 */
typedef struct sftkCipherValueStr sftkCipherValue;
struct sftkCipherValueStr {
    PLArenaPool *arena;
    SECOidTag alg;
    NSSPKCS5PBEParameter *param;
    SECItem salt;
    SECItem value;
};

#define SFTK_CIPHERTEXT_VERSION 3

struct SFTKDBEncryptedDataInfoStr {
    SECAlgorithmID algorithm;
    SECItem encryptedData;
};
typedef struct SFTKDBEncryptedDataInfoStr SFTKDBEncryptedDataInfo;

SEC_ASN1_MKSUB(SECOID_AlgorithmIDTemplate)

const SEC_ASN1Template sftkdb_EncryptedDataInfoTemplate[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(SFTKDBEncryptedDataInfo) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(SFTKDBEncryptedDataInfo, algorithm),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_OCTET_STRING,
      offsetof(SFTKDBEncryptedDataInfo, encryptedData) },
    { 0 }
};

/*
 * This parses the cipherText into cipher value. NOTE: cipherValue will point
 * to data in cipherText, if cipherText is freed, cipherValue will be invalid.
 */
static SECStatus
sftkdb_decodeCipherText(const SECItem *cipherText, sftkCipherValue *cipherValue)
{
    PLArenaPool *arena = NULL;
    SFTKDBEncryptedDataInfo edi;
    SECStatus rv;

    PORT_Assert(cipherValue);
    cipherValue->arena = NULL;
    cipherValue->param = NULL;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        return SECFailure;
    }

    rv = SEC_QuickDERDecodeItem(arena, &edi, sftkdb_EncryptedDataInfoTemplate,
                                cipherText);
    if (rv != SECSuccess) {
        goto loser;
    }
    cipherValue->alg = SECOID_GetAlgorithmTag(&edi.algorithm);
    cipherValue->param = nsspkcs5_AlgidToParam(&edi.algorithm);
    if (cipherValue->param == NULL) {
        goto loser;
    }
    cipherValue->value = edi.encryptedData;
    cipherValue->arena = arena;

    return SECSuccess;
loser:
    if (cipherValue->param) {
        nsspkcs5_DestroyPBEParameter(cipherValue->param);
        cipherValue->param = NULL;
    }
    if (arena) {
        PORT_FreeArena(arena, PR_FALSE);
    }
    return SECFailure;
}

/*
 * unlike decode, Encode actually allocates a SECItem the caller must free
 * The caller can pass an optional arena to to indicate where to place
 * the resultant cipherText.
 */
static SECStatus
sftkdb_encodeCipherText(PLArenaPool *arena, sftkCipherValue *cipherValue,
                        SECItem **cipherText)
{
    SFTKDBEncryptedDataInfo edi;
    SECAlgorithmID *algid;
    SECStatus rv;
    PLArenaPool *localArena = NULL;

    localArena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (localArena == NULL) {
        return SECFailure;
    }

    algid = nsspkcs5_CreateAlgorithmID(localArena, cipherValue->alg,
                                       cipherValue->param);
    if (algid == NULL) {
        rv = SECFailure;
        goto loser;
    }
    rv = SECOID_CopyAlgorithmID(localArena, &edi.algorithm, algid);
    SECOID_DestroyAlgorithmID(algid, PR_TRUE);
    if (rv != SECSuccess) {
        goto loser;
    }
    edi.encryptedData = cipherValue->value;

    *cipherText = SEC_ASN1EncodeItem(arena, NULL, &edi,
                                     sftkdb_EncryptedDataInfoTemplate);
    if (*cipherText == NULL) {
        rv = SECFailure;
    }

loser:
    if (localArena) {
        PORT_FreeArena(localArena, PR_TRUE);
    }

    return rv;
}

/*
 * Use our key to decode a cipherText block from the database.
 *
 * plain text is allocated by nsspkcs5_CipherData and must be freed
 * with SECITEM_FreeItem by the caller.
 */
SECStatus
sftkdb_DecryptAttribute(SFTKDBHandle *handle, SECItem *passKey,
                        CK_OBJECT_HANDLE id, CK_ATTRIBUTE_TYPE type,
                        SECItem *cipherText, SECItem **plain)
{
    SECStatus rv;
    sftkCipherValue cipherValue;

    /* First get the cipher type */
    *plain = NULL;
    rv = sftkdb_decodeCipherText(cipherText, &cipherValue);
    if (rv != SECSuccess) {
        goto loser;
    }

    *plain = nsspkcs5_CipherData(cipherValue.param, passKey, &cipherValue.value,
                                 PR_FALSE, NULL);
    if (*plain == NULL) {
        rv = SECFailure;
        goto loser;
    }

    /* If we are using aes 256, we need to check authentication as well.*/
    if ((type != CKT_INVALID_TYPE) &&
        (cipherValue.alg == SEC_OID_PKCS5_PBES2) &&
        (cipherValue.param->encAlg == SEC_OID_AES_256_CBC)) {
        SECItem signature;
        unsigned char signData[SDB_MAX_META_DATA_LEN];
        CK_RV crv;

        /* if we get here from the old legacy db, there is clearly an
         * error, don't return the plaintext */
        if (handle == NULL) {
            rv = SECFailure;
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            goto loser;
        }

        signature.data = signData;
        signature.len = sizeof(signData);
        rv = SECFailure;
        /* sign sftkdb_GetAttriibuteSignature returns a crv, not an rv */
        crv = sftkdb_GetAttributeSignature(handle, handle, id, type,
                                           &signature);
        if (crv == CKR_OK) {
            rv = sftkdb_VerifyAttribute(handle, passKey, CK_INVALID_HANDLE,
                                        type, *plain, &signature);
        }
        if (rv != SECSuccess) {
            /*  handle bug 1720226 where old versions of NSS misfiled the signature
             *  attribute on password update */
            id |= SFTK_KEYDB_TYPE | SFTK_TOKEN_TYPE;
            signature.len = sizeof(signData);
            crv = sftkdb_GetAttributeSignature(handle, handle, id, type,
                                               &signature);
            if (crv != CKR_OK) {
                rv = SECFailure;
                PORT_SetError(SEC_ERROR_BAD_SIGNATURE);
                goto loser;
            }
            rv = sftkdb_VerifyAttribute(handle, passKey, CK_INVALID_HANDLE,
                                        type, *plain, &signature);
        }
    }

loser:
    if (cipherValue.param) {
        nsspkcs5_DestroyPBEParameter(cipherValue.param);
    }
    if (cipherValue.arena) {
        PORT_FreeArena(cipherValue.arena, PR_FALSE);
    }
    /* Item decrypted, but failed integrity, clear it out */
    if (*plain && rv != SECSuccess) {
        SECITEM_ZfreeItem(*plain, PR_TRUE);
        *plain = NULL;
    }
    return rv;
}

/* If the database can't store the integrity check, it's a non-FIPS database
 * and we use the old encryption scheme for it */
static PRBool
sftkdb_useLegacyEncryption(SFTKDBHandle *handle, SDB *db)
{
    if ((handle == NULL) || (db == NULL)) {
        /* this is the case where the legacy db is calling back to us to
         * encrypt or decrypt attributes inside the lower level db code.
         * This is because the legacy db stored keys as pkcs #8 encrypted
         * blobs rather than individual encrypted attributes */
        return PR_TRUE;
    }
    /* currently, only the legacy db can't store meta data, but if we
     * add a new db that also can't store meta data, then it to wouldn't
     * be able to do the integrity checks. In both cases use the old encryption
     * algorithms. */
    if ((db->sdb_flags & SDB_HAS_META) == 0) {
        return PR_TRUE;
    }
    return PR_FALSE;
}

/*
 * encrypt a block. This function returned the encrypted ciphertext which
 * the caller must free. If the caller provides an arena, cipherText will
 * be allocated out of that arena. This also generated the per entry
 * salt automatically.
 */
SECStatus
sftkdb_EncryptAttribute(PLArenaPool *arena, SFTKDBHandle *handle, SDB *db,
                        SECItem *passKey, int iterationCount,
                        CK_OBJECT_HANDLE id, CK_ATTRIBUTE_TYPE type,
                        SECItem *plainText, SECItem **cipherText)
{
    SECStatus rv;
    sftkCipherValue cipherValue;
    SECItem *cipher = NULL;
    NSSPKCS5PBEParameter *param = NULL;
    unsigned char saltData[HASH_LENGTH_MAX];
    SECItem *signature = NULL;
    HASH_HashType hashType = HASH_AlgNULL;

    if (sftkdb_useLegacyEncryption(handle, db)) {
        cipherValue.alg = SEC_OID_PKCS12_PBE_WITH_SHA1_AND_TRIPLE_DES_CBC;
        cipherValue.salt.len = SHA1_LENGTH;
        hashType = HASH_AlgSHA1;
    } else {
        cipherValue.alg = SEC_OID_AES_256_CBC;
        cipherValue.salt.len = SHA256_LENGTH;
        hashType = HASH_AlgSHA256;
    }
    cipherValue.salt.data = saltData;
    RNG_GenerateGlobalRandomBytes(saltData, cipherValue.salt.len);

    param = nsspkcs5_NewParam(cipherValue.alg, hashType, &cipherValue.salt,
                              iterationCount);
    if (param == NULL) {
        rv = SECFailure;
        goto loser;
    }
    cipher = nsspkcs5_CipherData(param, passKey, plainText, PR_TRUE, NULL);
    if (cipher == NULL) {
        rv = SECFailure;
        goto loser;
    }
    cipherValue.value = *cipher;
    cipherValue.param = param;

    rv = sftkdb_encodeCipherText(arena, &cipherValue, cipherText);
    if (rv != SECSuccess) {
        goto loser;
    }

    /* If we are using aes 256, we need to add authentication as well */
    if ((type != CKT_INVALID_TYPE) &&
        (cipherValue.param->encAlg == SEC_OID_AES_256_CBC)) {
        rv = sftkdb_SignAttribute(arena, handle, db, passKey, iterationCount,
                                  CK_INVALID_HANDLE, type, plainText,
                                  &signature);
        if (rv != SECSuccess) {
            goto loser;
        }
        rv = sftkdb_PutAttributeSignature(handle, db, id, type,
                                          signature);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

loser:
    if ((arena == NULL) && signature) {
        SECITEM_ZfreeItem(signature, PR_TRUE);
    }
    if (cipher) {
        SECITEM_FreeItem(cipher, PR_TRUE);
    }
    if (param) {
        nsspkcs5_DestroyPBEParameter(param);
    }
    return rv;
}

/*
 * use the password and the pbe parameters to generate an HMAC for the
 * given plain text data. This is used by sftkdb_VerifyAttribute and
 * sftkdb_SignAttribute. Signature is returned in signData. The caller
 * must preallocate the space in the secitem.
 */
static SECStatus
sftkdb_pbehash(SECOidTag sigOid, SECItem *passKey,
               NSSPKCS5PBEParameter *param,
               CK_OBJECT_HANDLE objectID, CK_ATTRIBUTE_TYPE attrType,
               SECItem *plainText, SECItem *signData)
{
    SECStatus rv = SECFailure;
    SECItem *key = NULL;
    HMACContext *hashCx = NULL;
    HASH_HashType hashType = HASH_AlgNULL;
    const SECHashObject *hashObj;
    unsigned char addressData[SDB_ULONG_SIZE];
    hashType = HASH_FromHMACOid(param->encAlg);
    if (hashType == HASH_AlgNULL) {
        PORT_SetError(SEC_ERROR_INVALID_ALGORITHM);
        return SECFailure;
    }

    hashObj = HASH_GetRawHashObject(hashType);
    if (hashObj == NULL) {
        goto loser;
    }

    key = nsspkcs5_ComputeKeyAndIV(param, passKey, NULL, PR_FALSE);
    if (!key) {
        goto loser;
    }

    hashCx = HMAC_Create(hashObj, key->data, key->len, PR_TRUE);
    if (!hashCx) {
        goto loser;
    }
    HMAC_Begin(hashCx);
    /* Tie this value to a particular object. This is most important for
     * the trust attributes, where and attacker could copy a value for
     * 'validCA' from another cert in the database */
    sftk_ULong2SDBULong(addressData, objectID);
    HMAC_Update(hashCx, addressData, SDB_ULONG_SIZE);
    sftk_ULong2SDBULong(addressData, attrType);
    HMAC_Update(hashCx, addressData, SDB_ULONG_SIZE);

    HMAC_Update(hashCx, plainText->data, plainText->len);
    rv = HMAC_Finish(hashCx, signData->data, &signData->len, signData->len);

loser:
    if (hashCx) {
        HMAC_Destroy(hashCx, PR_TRUE);
    }
    if (key) {
        SECITEM_ZfreeItem(key, PR_TRUE);
    }
    return rv;
}

/*
 * Use our key to verify a signText block from the database matches
 * the plainText from the database. The signText is a PKCS 5 v2 pbe.
 * plainText is the plainText of the attribute.
 */
SECStatus
sftkdb_VerifyAttribute(SFTKDBHandle *handle,
                       SECItem *passKey, CK_OBJECT_HANDLE objectID,
                       CK_ATTRIBUTE_TYPE attrType,
                       SECItem *plainText, SECItem *signText)
{
    SECStatus rv;
    sftkCipherValue signValue;
    SECItem signature;
    unsigned char signData[HASH_LENGTH_MAX];

    /* First get the cipher type */
    rv = sftkdb_decodeCipherText(signText, &signValue);
    if (rv != SECSuccess) {
        goto loser;
    }
    signature.data = signData;
    signature.len = sizeof(signData);

    rv = sftkdb_pbehash(signValue.alg, passKey, signValue.param,
                        objectID, attrType, plainText, &signature);
    if (rv != SECSuccess) {
        goto loser;
    }
    if (SECITEM_CompareItem(&signValue.value, &signature) != 0) {
        PORT_SetError(SEC_ERROR_BAD_SIGNATURE);
        rv = SECFailure;
    }

loser:
    PORT_Memset(signData, 0, sizeof signData);
    if (signValue.param) {
        nsspkcs5_DestroyPBEParameter(signValue.param);
    }
    if (signValue.arena) {
        PORT_FreeArena(signValue.arena, PR_TRUE);
    }
    return rv;
}

/*
 * Use our key to create a signText block the plain text of an
 * attribute. The signText is a PKCS 5 v2 pbe.
 */
SECStatus
sftkdb_SignAttribute(PLArenaPool *arena, SFTKDBHandle *keyDB, SDB *db,
                     SECItem *passKey, int iterationCount,
                     CK_OBJECT_HANDLE objectID,
                     CK_ATTRIBUTE_TYPE attrType,
                     SECItem *plainText, SECItem **signature)
{
    SECStatus rv;
    sftkCipherValue signValue;
    NSSPKCS5PBEParameter *param = NULL;
    unsigned char saltData[HASH_LENGTH_MAX];
    unsigned char signData[HASH_LENGTH_MAX];
    SECOidTag hmacAlg = SEC_OID_HMAC_SHA256; /* hash for authentication */
    SECOidTag prfAlg = SEC_OID_HMAC_SHA256;  /* hash for pb key generation */
    HASH_HashType prfType;
    unsigned int hmacLength;
    unsigned int prfLength;

    /* this code allows us to fetch the lengths and hashes on the fly
     * by simply changing the OID above */
    prfType = HASH_FromHMACOid(prfAlg);
    PORT_Assert(prfType != HASH_AlgNULL);
    prfLength = HASH_GetRawHashObject(prfType)->length;
    PORT_Assert(prfLength <= HASH_LENGTH_MAX);

    hmacLength = HASH_GetRawHashObject(HASH_FromHMACOid(hmacAlg))->length;
    PORT_Assert(hmacLength <= HASH_LENGTH_MAX);

    /* initialize our CipherValue structure */
    signValue.alg = SEC_OID_PKCS5_PBMAC1;
    signValue.salt.len = prfLength;
    signValue.salt.data = saltData;
    signValue.value.data = signData;
    signValue.value.len = hmacLength;
    RNG_GenerateGlobalRandomBytes(saltData, prfLength);

    /* initialize our pkcs5 parameter */
    param = nsspkcs5_NewParam(signValue.alg, HASH_AlgSHA1, &signValue.salt,
                              iterationCount);
    if (param == NULL) {
        rv = SECFailure;
        goto loser;
    }
    param->keyID = pbeBitGenIntegrityKey;
    /* set the PKCS 5 v2 parameters, not extractable from the
     * data passed into nsspkcs5_NewParam */
    param->encAlg = hmacAlg;
    param->hashType = prfType;
    param->keyLen = hmacLength;
    rv = SECOID_SetAlgorithmID(param->poolp, &param->prfAlg, prfAlg, NULL);
    if (rv != SECSuccess) {
        goto loser;
    }

    /* calculate the mac */
    rv = sftkdb_pbehash(signValue.alg, passKey, param, objectID, attrType,
                        plainText, &signValue.value);
    if (rv != SECSuccess) {
        goto loser;
    }
    signValue.param = param;

    /* write it out */
    rv = sftkdb_encodeCipherText(arena, &signValue, signature);
    if (rv != SECSuccess) {
        goto loser;
    }

loser:
    PORT_Memset(signData, 0, sizeof signData);
    if (param) {
        nsspkcs5_DestroyPBEParameter(param);
    }
    return rv;
}

/*
 * safely swith the passed in key for the one caches in the keydb handle
 *
 * A key attached to the handle tells us the the token is logged in.
 * We can used the key attached to the handle in sftkdb_EncryptAttribute
 *  and sftkdb_DecryptAttribute calls.
 */
static void
sftkdb_switchKeys(SFTKDBHandle *keydb, SECItem *passKey, int iterationCount)
{
    unsigned char *data;
    int len;

    if (keydb->passwordLock == NULL) {
        PORT_Assert(keydb->type != SFTK_KEYDB_TYPE);
        return;
    }

    /* an atomic pointer set would be nice */
    SKIP_AFTER_FORK(PR_Lock(keydb->passwordLock));
    data = keydb->passwordKey.data;
    len = keydb->passwordKey.len;
    keydb->passwordKey.data = passKey->data;
    keydb->passwordKey.len = passKey->len;
    keydb->defaultIterationCount = iterationCount;
    passKey->data = data;
    passKey->len = len;
    SKIP_AFTER_FORK(PR_Unlock(keydb->passwordLock));
}

/*
 * returns true if we are in a middle of a merge style update.
 */
PRBool
sftkdb_InUpdateMerge(SFTKDBHandle *keydb)
{
    return keydb->updateID ? PR_TRUE : PR_FALSE;
}

/*
 * returns true if we are looking for the password for the user's old source
 * database as part of a merge style update.
 */
PRBool
sftkdb_NeedUpdateDBPassword(SFTKDBHandle *keydb)
{
    if (!sftkdb_InUpdateMerge(keydb)) {
        return PR_FALSE;
    }
    if (keydb->updateDBIsInit && !keydb->updatePasswordKey) {
        return PR_TRUE;
    }
    return PR_FALSE;
}

/*
 * fetch an update password key from a handle.
 */
SECItem *
sftkdb_GetUpdatePasswordKey(SFTKDBHandle *handle)
{
    SECItem *key = NULL;

    /* if we're a cert db, fetch it from our peer key db */
    if (handle->type == SFTK_CERTDB_TYPE) {
        handle = handle->peerDB;
    }

    /* don't have one */
    if (!handle) {
        return NULL;
    }

    PR_Lock(handle->passwordLock);
    if (handle->updatePasswordKey) {
        key = SECITEM_DupItem(handle->updatePasswordKey);
    }
    PR_Unlock(handle->passwordLock);

    return key;
}

/*
 * free the update password key from a handle.
 */
void
sftkdb_FreeUpdatePasswordKey(SFTKDBHandle *handle)
{
    SECItem *key = NULL;

    /* don't have one */
    if (!handle) {
        return;
    }

    /* if we're a cert db, we don't have one */
    if (handle->type == SFTK_CERTDB_TYPE) {
        return;
    }

    PR_Lock(handle->passwordLock);
    if (handle->updatePasswordKey) {
        key = handle->updatePasswordKey;
        handle->updatePasswordKey = NULL;
    }
    PR_Unlock(handle->passwordLock);

    if (key) {
        SECITEM_ZfreeItem(key, PR_TRUE);
    }

    return;
}

/*
 * what password db we use depends heavily on the update state machine
 *
 *  1) no update db, return the normal database.
 *  2) update db and no merge return the update db.
 *  3) update db and in merge:
 *      return the update db if we need the update db's password,
 *      otherwise return our normal datbase.
 */
static SDB *
sftk_getPWSDB(SFTKDBHandle *keydb)
{
    if (!keydb->update) {
        return keydb->db;
    }
    if (!sftkdb_InUpdateMerge(keydb)) {
        return keydb->update;
    }
    if (sftkdb_NeedUpdateDBPassword(keydb)) {
        return keydb->update;
    }
    return keydb->db;
}

/*
 * return success if we have a valid password entry.
 * This is will show up outside of PKCS #11 as CKF_USER_PIN_INIT
 * in the token flags.
 */
SECStatus
sftkdb_HasPasswordSet(SFTKDBHandle *keydb)
{
    SECItem salt, value;
    unsigned char saltData[SDB_MAX_META_DATA_LEN];
    unsigned char valueData[SDB_MAX_META_DATA_LEN];
    CK_RV crv;
    SDB *db;

    if (keydb == NULL) {
        return SECFailure;
    }

    db = sftk_getPWSDB(keydb);
    if (db == NULL) {
        return SECFailure;
    }

    salt.data = saltData;
    salt.len = sizeof(saltData);
    value.data = valueData;
    value.len = sizeof(valueData);
    crv = (*db->sdb_GetMetaData)(db, "password", &salt, &value);

    /* If no password is set, we can update right away */
    if (((keydb->db->sdb_flags & SDB_RDONLY) == 0) && keydb->update && crv != CKR_OK) {
        /* update the peer certdb if it exists */
        if (keydb->peerDB) {
            sftkdb_Update(keydb->peerDB, NULL);
        }
        sftkdb_Update(keydb, NULL);
    }
    return (crv == CKR_OK) ? SECSuccess : SECFailure;
}

/* pull out the common final part of checking a password */
static SECStatus
sftkdb_finishPasswordCheck(SFTKDBHandle *keydb, SECItem *key,
                           const char *pw, SECItem *value,
                           PRBool *tokenRemoved, PRBool allowKDFUpgrade);
static SECStatus sftkdb_checkPassword(SFTKDBHandle *keydb, const char *pw,
                                      PRBool *tokenRemoved,
                                      PRBool allowKDFUpgrade);
static SECStatus sftkdb_upgradeKDF(SFTKDBHandle *keydb, const char *pw);

/*
 * check to see if we have the NULL password set.
 * We special case the NULL password so that if you have no password set, you
 * don't do thousands of hash rounds. This allows us to startup and get
 * webpages without slowdown in normal mode.
 */
SECStatus
sftkdb_CheckPasswordNull(SFTKDBHandle *keydb, PRBool *tokenRemoved)
{
    /* just like sftkdb_CheckPassowd, we get the salt and value, and
     * create a dbkey */
    SECStatus rv;
    SECItem salt, value;
    unsigned char saltData[SDB_MAX_META_DATA_LEN];
    unsigned char valueData[SDB_MAX_META_DATA_LEN];
    SECItem key;
    SDB *db;
    CK_RV crv;
    sftkCipherValue cipherValue;

    cipherValue.param = NULL;
    cipherValue.arena = NULL;

    if (keydb == NULL) {
        return SECFailure;
    }

    db = sftk_getPWSDB(keydb);
    if (db == NULL) {
        return SECFailure;
    }

    key.data = NULL;
    key.len = 0;

    /* get the entry from the database */
    salt.data = saltData;
    salt.len = sizeof(saltData);
    value.data = valueData;
    value.len = sizeof(valueData);
    crv = (*db->sdb_GetMetaData)(db, "password", &salt, &value);
    if (crv != CKR_OK) {
        rv = SECFailure;
        goto done;
    }

    /* get our intermediate key based on the entry salt value */
    rv = sftkdb_passwordToKey(keydb, &salt, "", &key);
    if (rv != SECSuccess) {
        goto done;
    }

    /* First get the cipher type */
    rv = sftkdb_decodeCipherText(&value, &cipherValue);
    if (rv != SECSuccess) {
        goto done;
    }

    if (cipherValue.param->iter != 1) {
        rv = SECFailure;
        goto done;
    }

    rv = sftkdb_finishPasswordCheck(keydb, &key, "", &value, tokenRemoved,
                                    PR_FALSE);

done:
    if (key.data) {
        PORT_ZFree(key.data, key.len);
    }
    if (cipherValue.param) {
        nsspkcs5_DestroyPBEParameter(cipherValue.param);
    }
    if (cipherValue.arena) {
        PORT_FreeArena(cipherValue.arena, PR_FALSE);
    }
    return rv;
}

#define SFTK_PW_CHECK_STRING "password-check"
#define SFTK_PW_CHECK_LEN 14

/*
 * Check if the supplied password is valid.  allowKDFUpgrade is PR_FALSE when
 * the caller is already re-encrypting the database: sftkdb_reencryptDatabase
 * verifies the old password from inside its own SDB transaction, and the
 * silent upgrade must not re-enter it from there.
 */
static SECStatus
sftkdb_checkPassword(SFTKDBHandle *keydb, const char *pw, PRBool *tokenRemoved,
                     PRBool allowKDFUpgrade)
{
    SECStatus rv;
    SECItem salt, value;
    unsigned char saltData[SDB_MAX_META_DATA_LEN];
    unsigned char valueData[SDB_MAX_META_DATA_LEN];
    SECItem key;
    SDB *db;
    CK_RV crv;

    if (keydb == NULL) {
        return SECFailure;
    }

    db = sftk_getPWSDB(keydb);
    if (db == NULL) {
        return SECFailure;
    }

    key.data = NULL;
    key.len = 0;

    if (pw == NULL)
        pw = "";

    /* get the entry from the database */
    salt.data = saltData;
    salt.len = sizeof(saltData);
    value.data = valueData;
    value.len = sizeof(valueData);
    crv = (*db->sdb_GetMetaData)(db, "password", &salt, &value);
    if (crv != CKR_OK) {
        rv = SECFailure;
        goto done;
    }

    /* get our intermediate key based on the entry salt value */
    rv = sftkdb_passwordToKey(keydb, &salt, pw, &key);
    if (rv != SECSuccess) {
        goto done;
    }

    rv = sftkdb_finishPasswordCheck(keydb, &key, pw, &value, tokenRemoved,
                                    allowKDFUpgrade);

done:
    if (key.data) {
        PORT_ZFree(key.data, key.len);
    }
    return rv;
}

/*
 * check if the supplied password is valid
 */
SECStatus
sftkdb_CheckPassword(SFTKDBHandle *keydb, const char *pw, PRBool *tokenRemoved)
{
    return sftkdb_checkPassword(keydb, pw, tokenRemoved, PR_TRUE);
}

/* we need to pass iterationCount in case we are updating a new database
 * and from an old one. */
static SECStatus
sftkdb_finishPasswordCheck(SFTKDBHandle *keydb, SECItem *key, const char *pw,
                           SECItem *value, PRBool *tokenRemoved,
                           PRBool allowKDFUpgrade)
{
    SECItem *result = NULL;
    SECStatus rv;
    int iterationCount = getPBEIterationCount();

    if (*pw == 0) {
        iterationCount = 1;
    } else if (keydb->usesLegacyStorage && !sftk_isLegacyIterationCountAllowed()) {
        iterationCount = 1;
    }

    /* decrypt the entry value */
    rv = sftkdb_DecryptAttribute(keydb, key, CK_INVALID_HANDLE,
                                 CKT_INVALID_TYPE, value, &result);
    if (rv != SECSuccess) {
        goto done;
    }

    /* if it's what we expect, update our key in the database handle and
     * return Success */
    if ((result->len == SFTK_PW_CHECK_LEN) &&
        PORT_Memcmp(result->data, SFTK_PW_CHECK_STRING, SFTK_PW_CHECK_LEN) == 0) {
        /*
         * We have a password, now lets handle any potential update cases..
         *
         * First, the normal case: no update. In this case we only need the
         *  the password for our only DB, which we now have, we switch
         *  the keys and fall through.
         * Second regular (non-merge) update: The target DB does not yet have
         *  a password initialized, we now have the password for the source DB,
         *  so we can switch the keys and simply update the target database.
         * Merge update case: This one is trickier.
         *   1) If we need the source DB password, then we just got it here.
         *       We need to save that password,
         *       then we need to check to see if we need or have the target
         *         database password.
         *       If we have it (it's the same as the source), or don't need
         *         it (it's not set or is ""), we can start the update now.
         *       If we don't have it, we need the application to get it from
         *         the user. Clear our sessions out to simulate a token
         *         removal. C_GetTokenInfo will change the token description
         *         and the token will still appear to be logged out.
         *   2) If we already have the source DB  password, this password is
         *         for the target database. We can now move forward with the
         *         update, as we now have both required passwords.
         *
         */
        PR_Lock(keydb->passwordLock);
        if (sftkdb_NeedUpdateDBPassword(keydb)) {
            /* Squirrel this special key away.
             * This has the side effect of turning sftkdb_NeedLegacyPW off,
             * as well as changing which database is returned from
             * SFTK_GET_PW_DB (thus effecting both sftkdb_CheckPassword()
             * and sftkdb_HasPasswordSet()) */
            keydb->updatePasswordKey = SECITEM_DupItem(key);
            PR_Unlock(keydb->passwordLock);
            if (keydb->updatePasswordKey == NULL) {
                /* PORT_Error set by SECITEM_DupItem */
                rv = SECFailure;
                goto done;
            }

            /* Simulate a token removal -- we need to do this any
             * any case at this point so the token name is correct. */
            *tokenRemoved = PR_TRUE;

            /*
             * OK, we got the update DB password, see if we need a password
             * for the target...
             */
            if (sftkdb_HasPasswordSet(keydb) == SECSuccess) {
                /* We have a password, do we know what the password is?
                 *  check 1) for the password the user supplied for the
                 *           update DB,
                 *    and 2) for the null password.
                 *
                 * RECURSION NOTE: we are calling ourselves here. This means
                 *  any updates, switchKeys, etc will have been completed
                 *  if these functions return successfully, in those cases
                 *  just exit returning Success. We don't recurse infinitely
                 *  because we are making this call from a NeedUpdateDBPassword
                 *  block and we've already set that update password at this
                 *  point.  */
                rv = sftkdb_CheckPassword(keydb, pw, tokenRemoved);
                if (rv == SECSuccess) {
                    /* source and target databases have the same password, we
                     * are good to go */
                    goto done;
                }
                sftkdb_CheckPasswordNull(keydb, tokenRemoved);

                /*
                 * Important 'NULL' code here. At this point either we
                 * succeeded in logging in with "" or we didn't.
                 *
                 *  If we did succeed at login, our machine state will be set
                 * to logged in appropriately. The application will find that
                 * it's logged in as soon as it opens a new session. We have
                 * also completed the update. Life is good.
                 *
                 *  If we did not succeed, well the user still successfully
                 * logged into the update database, since we faked the token
                 * removal it's just like the user logged into his smart card
                 * then removed it. the actual login work, so we report that
                 * success back to the user, but we won't actually be
                 * logged in. The application will find this out when it
                 * checks it's login state, thus triggering another password
                 * prompt so we can get the real target DB password.
                 *
                 * summary, we exit from here with SECSuccess no matter what.
                 */
                rv = SECSuccess;
                goto done;
            } else {
                /* there is no password, just fall through to update.
                 * update will write the source DB's password record
                 * into the target DB just like it would in a non-merge
                 * update case. */
            }
        } else {
            PR_Unlock(keydb->passwordLock);
        }
        /* load the keys, so the keydb can parse it's key set */
        sftkdb_switchKeys(keydb, key, iterationCount);

        /* we need to update, do it now */
        if (((keydb->db->sdb_flags & SDB_RDONLY) == 0) && keydb->update) {
            /* update the peer certdb if it exists */
            if (keydb->peerDB) {
                sftkdb_Update(keydb->peerDB, key);
            }
            sftkdb_Update(keydb, key);
        }

        /* Silently re-encrypt with the current iteration count if the stored
         * count is below the minimum. Only for non-null passwords on writable
         * non-legacy databases not undergoing a DB migration, and never when
         * the caller is already re-encrypting the database. */
        if (allowKDFUpgrade && *pw != 0 && !keydb->usesLegacyStorage &&
            !(keydb->db->sdb_flags & SDB_RDONLY) && !keydb->update) {
            sftkCipherValue cipherValue;
            cipherValue.param = NULL;
            cipherValue.arena = NULL;
            if (sftkdb_decodeCipherText(value, &cipherValue) == SECSuccess &&
                cipherValue.param->iter < iterationCount) {
                if (sftkdb_upgradeKDF(keydb, pw) != SECSuccess) {
                    /* Upgrade is best-effort; login already succeeded. */
                    PORT_SetError(0);
                }
            }
            if (cipherValue.param) {
                nsspkcs5_DestroyPBEParameter(cipherValue.param);
            }
            if (cipherValue.arena) {
                PORT_FreeArena(cipherValue.arena, PR_FALSE);
            }
        }
    } else {
        rv = SECFailure;
        /*PORT_SetError( bad password); */
    }

done:
    if (result) {
        SECITEM_ZfreeItem(result, PR_TRUE);
    }
    return rv;
}

/*
 * return Success if the there is a cached password key.
 */
SECStatus
sftkdb_PWCached(SFTKDBHandle *keydb)
{
    SECStatus rv;
    PR_Lock(keydb->passwordLock);
    rv = keydb->passwordKey.data ? SECSuccess : SECFailure;
    PR_Unlock(keydb->passwordLock);
    return rv;
}

/*
 * Did reading an attribute fail only because its mac is missing or doesn't
 * match? Anything else is a real error, also a mac we can't decode or check:
 * it may come from a newer NSS.
 */
static PRBool
sftk_isUnverifiableMac(SFTKDBHandle *handle, SFTKDBHandle *keyHandle,
                       CK_OBJECT_HANDLE id, CK_ATTRIBUTE_TYPE type, CK_RV crv)
{
    if (crv == CKR_SIGNATURE_INVALID) {
        return PORT_GetError() == SEC_ERROR_BAD_SIGNATURE;
    }
    // sdb reports a missing signature like a missing object.
    if (crv == CKR_OBJECT_HANDLE_INVALID) {
        unsigned char signData[SDB_MAX_META_DATA_LEN];
        SECItem signText = { siBuffer, signData, sizeof(signData) };
        return sftkdb_GetAttributeSignature(handle, keyHandle, id, type,
                                            &signText) == CKR_OBJECT_HANDLE_INVALID;
    }
    return PR_FALSE;
}

static CK_RV
sftk_updateMacs(PLArenaPool *arena, SFTKDBHandle *handle,
                CK_OBJECT_HANDLE id, SECItem *newKey, int iterationCount)
{
    SFTKDBHandle *keyHandle = handle;
    SDB *keyTarget = NULL;
    if (handle->type != SFTK_KEYDB_TYPE) {
        keyHandle = handle->peerDB;
    }
    if (keyHandle == NULL) {
        return CKR_OK;
    }
    // Old DBs don't have metadata, so we can return early here.
    keyTarget = SFTK_GET_SDB(keyHandle);
    if ((keyTarget->sdb_flags & SDB_HAS_META) == 0) {
        return CKR_OK;
    }

    id &= SFTK_OBJ_ID_MASK;

    CK_ATTRIBUTE_TYPE authAttrTypes[] = {
        CKA_MODULUS,
        CKA_PUBLIC_EXPONENT,
        CKA_NSS_CERT_SHA1_HASH,
        CKA_NSS_CERT_MD5_HASH,
        CKA_NAME_HASH_ALGORITHM,
        CKA_HASH_OF_CERTIFICATE,
        CKA_PKCS_TRUST_SERVER_AUTH,
        CKA_PKCS_TRUST_CLIENT_AUTH,
        CKA_PKCS_TRUST_EMAIL_PROTECTION,
        CKA_PKCS_TRUST_CODE_SIGNING,
        CKA_NSS_TRUST_SERVER_AUTH,
        CKA_NSS_TRUST_CLIENT_AUTH,
        CKA_NSS_TRUST_EMAIL_PROTECTION,
        CKA_NSS_TRUST_CODE_SIGNING,
        CKA_NSS_TRUST_STEP_UP_APPROVED,
        CKA_NSS_OVERRIDE_EXTENSIONS,
    };
    const CK_ULONG authAttrTypeCount = sizeof(authAttrTypes) / sizeof(authAttrTypes[0]);

    // We don't know what attributes this object has, so we update them one at a
    // time.
    unsigned int i;
    for (i = 0; i < authAttrTypeCount; i++) {
        CK_ATTRIBUTE authAttr = { authAttrTypes[i], NULL, 0 };
        CK_RV rv = sftkdb_GetAttributeValue(handle, id, &authAttr, 1);
        if (rv != CKR_OK) {
            continue;
        }
        if ((authAttr.ulValueLen == -1) || (authAttr.ulValueLen == 0)) {
            continue;
        }
        authAttr.pValue = PORT_ArenaAlloc(arena, authAttr.ulValueLen);
        if (authAttr.pValue == NULL) {
            return CKR_HOST_MEMORY;
        }
        PORT_SetError(0);
        rv = sftkdb_GetAttributeValue(handle, id, &authAttr, 1);
        // Re-signing would vouch for an unchecked value; leave it as is.
        if (sftk_isUnverifiableMac(handle, keyHandle, id, authAttr.type, rv)) {
            PORT_SetError(0);
            continue;
        }
        if (rv != CKR_OK) {
            return rv;
        }
        if ((authAttr.ulValueLen == -1) || (authAttr.ulValueLen == 0)) {
            return CKR_GENERAL_ERROR;
        }
        // GetAttributeValue just verified the old macs, so it is safe to write
        // them out now.
        if (authAttr.ulValueLen == sizeof(CK_ULONG) &&
            sftkdb_isULONGAttribute(authAttr.type)) {
            CK_ULONG value = *(CK_ULONG *)authAttr.pValue;
            sftk_ULong2SDBULong(authAttr.pValue, value);
            authAttr.ulValueLen = SDB_ULONG_SIZE;
        }
        SECItem *signText;
        SECItem plainText;
        plainText.data = authAttr.pValue;
        plainText.len = authAttr.ulValueLen;
        if (sftkdb_SignAttribute(arena, handle, keyTarget, newKey,
                                 iterationCount, id, authAttr.type,
                                 &plainText, &signText) != SECSuccess) {
            return CKR_GENERAL_ERROR;
        }
        if (sftkdb_PutAttributeSignature(handle, keyTarget, id, authAttr.type,
                                         signText) != SECSuccess) {
            return CKR_GENERAL_ERROR;
        }
    }

    return CKR_OK;
}

static CK_RV
sftk_updateEncrypted(PLArenaPool *arena, SFTKDBHandle *keydb,
                     CK_OBJECT_HANDLE id, SECItem *newKey, int iterationCount)
{
    CK_ATTRIBUTE_TYPE privAttrTypes[] = {
        CKA_VALUE,
        CKA_SEED,
        CKA_PRIVATE_EXPONENT,
        CKA_PRIME_1,
        CKA_PRIME_2,
        CKA_EXPONENT_1,
        CKA_EXPONENT_2,
        CKA_COEFFICIENT,
    };
    const CK_ULONG privAttrCount = sizeof(privAttrTypes) / sizeof(privAttrTypes[0]);

    // We don't know what attributes this object has, so we update them one at a
    // time.
    unsigned int i;
    for (i = 0; i < privAttrCount; i++) {
        // Read the old attribute in the clear.
        CK_OBJECT_HANDLE sdbId = id & SFTK_OBJ_ID_MASK;
        CK_ATTRIBUTE privAttr = { privAttrTypes[i], NULL, 0 };
        CK_RV crv = sftkdb_GetAttributeValue(keydb, id, &privAttr, 1);
        if (crv != CKR_OK) {
            continue;
        }
        if ((privAttr.ulValueLen == -1) || (privAttr.ulValueLen == 0)) {
            continue;
        }
        privAttr.pValue = PORT_ArenaAlloc(arena, privAttr.ulValueLen);
        if (privAttr.pValue == NULL) {
            return CKR_HOST_MEMORY;
        }
        crv = sftkdb_GetAttributeValue(keydb, id, &privAttr, 1);
        if (crv != CKR_OK) {
            return crv;
        }
        if ((privAttr.ulValueLen == -1) || (privAttr.ulValueLen == 0)) {
            return CKR_GENERAL_ERROR;
        }
        SECItem plainText;
        SECItem *result;
        plainText.data = privAttr.pValue;
        plainText.len = privAttr.ulValueLen;
        if (sftkdb_EncryptAttribute(arena, keydb, keydb->db, newKey,
                                    iterationCount, sdbId, privAttr.type,
                                    &plainText, &result) != SECSuccess) {
            return CKR_GENERAL_ERROR;
        }
        privAttr.pValue = result->data;
        privAttr.ulValueLen = result->len;
        // Clear sensitive data.
        PORT_Memset(plainText.data, 0, plainText.len);

        // Write the newly encrypted attributes out directly.
        keydb->newKey = newKey;
        keydb->newDefaultIterationCount = iterationCount;
        crv = (*keydb->db->sdb_SetAttributeValue)(keydb->db, sdbId, &privAttr, 1);
        keydb->newKey = NULL;
        if (crv != CKR_OK) {
            return crv;
        }
    }

    return CKR_OK;
}

static CK_RV
sftk_convertAttributes(SFTKDBHandle *handle, CK_OBJECT_HANDLE id,
                       SECItem *newKey, int iterationCount)
{
    CK_RV crv = CKR_OK;
    PLArenaPool *arena = NULL;

    /* get a new arena to simplify cleanup */
    arena = PORT_NewArena(1024);
    if (!arena) {
        return CKR_HOST_MEMORY;
    }

    /*
     * first handle the MACS
     */
    crv = sftk_updateMacs(arena, handle, id, newKey, iterationCount);
    if (crv != CKR_OK) {
        goto loser;
    }

    if (handle->type == SFTK_KEYDB_TYPE) {
        crv = sftk_updateEncrypted(arena, handle, id, newKey,
                                   iterationCount);
        if (crv != CKR_OK) {
            goto loser;
        }
    }

    /* free up our mess */
    PORT_FreeArena(arena, PR_TRUE);
    return CKR_OK;

loser:
    /* there may be unencrypted data, clear it out down */
    PORT_FreeArena(arena, PR_TRUE);
    return crv;
}

/*
 * must be called with the old key active.
 */
CK_RV
sftkdb_convertObjects(SFTKDBHandle *handle, CK_ATTRIBUTE *template,
                      CK_ULONG count, SECItem *newKey, int iterationCount)
{
    SDBFind *find = NULL;
    CK_ULONG idCount = SFTK_MAX_IDS;
    CK_OBJECT_HANDLE ids[SFTK_MAX_IDS];
    CK_RV crv, crv2;
    unsigned int i;

    crv = sftkdb_FindObjectsInit(handle, template, count, &find);

    if (crv != CKR_OK) {
        return crv;
    }
    while ((crv == CKR_OK) && (idCount == SFTK_MAX_IDS)) {
        crv = sftkdb_FindObjects(handle, find, ids, SFTK_MAX_IDS, &idCount);
        for (i = 0; (crv == CKR_OK) && (i < idCount); i++) {
            crv = sftk_convertAttributes(handle, ids[i], newKey,
                                         iterationCount);
        }
    }
    crv2 = sftkdb_FindObjectsFinal(handle, find);
    if (crv == CKR_OK)
        crv = crv2;

    return crv;
}

/*
 * Re-encrypt the database with a new key derived from newPin.  When
 * skipOldPinCheck is PR_FALSE this is a normal password change: the old
 * password is verified before proceeding.  When skipOldPinCheck is PR_TRUE
 * the caller has already verified the password (used by sftkdb_upgradeKDF to
 * avoid infinite recursion through sftkdb_finishPasswordCheck) and a fresh
 * salt is always generated.
 */
static SECStatus
sftkdb_reencryptDatabase(SFTKDBHandle *keydb, const char *oldPin,
                         const char *newPin, PRBool *tokenRemoved,
                         PRBool skipOldPinCheck)
{
    SECStatus rv = SECSuccess;
    SECItem plainText;
    SECItem newKey;
    SECItem *result = NULL;
    SECItem salt, value;
    SFTKDBHandle *certdb;
    unsigned char saltData[SDB_MAX_META_DATA_LEN];
    unsigned char valueData[SDB_MAX_META_DATA_LEN];
    int iterationCount = getPBEIterationCount();
    int preferred_salt_length;
    CK_RV crv;
    SDB *db;

    if (keydb == NULL) {
        return SECFailure;
    }

    db = SFTK_GET_SDB(keydb);
    if (db == NULL) {
        return SECFailure;
    }

    newKey.data = NULL;

    crv = (*keydb->db->sdb_Begin)(keydb->db);
    if (crv != CKR_OK) {
        rv = SECFailure;
        goto loser;
    }
    salt.data = saltData;
    salt.len = sizeof(saltData);
    value.data = valueData;
    value.len = sizeof(valueData);

    if (skipOldPinCheck) {
        /* Password already verified by caller; always generate a fresh salt. */
        salt.len = 0;
    } else {
        crv = (*db->sdb_GetMetaData)(db, "password", &salt, &value);
        if (crv == CKR_OK) {
            rv = sftkdb_checkPassword(keydb, oldPin, tokenRemoved, PR_FALSE);
            if (rv == SECFailure) {
                goto loser;
            }
        } else {
            salt.len = 0;
        }
    }

    preferred_salt_length = SHA384_LENGTH;
    /* Prefer SHA-1 salt for empty passwords (iter=1 path) */
    if (!newPin || *newPin == 0) {
        preferred_salt_length = SHA1_LENGTH;
    }

    PORT_Assert(preferred_salt_length <= SDB_MAX_META_DATA_LEN);
    if (salt.len != preferred_salt_length) {
        salt.len = preferred_salt_length;
        RNG_GenerateGlobalRandomBytes(salt.data, salt.len);
    }

    if (newPin && *newPin == 0) {
        iterationCount = 1;
    } else if (keydb->usesLegacyStorage && !sftk_isLegacyIterationCountAllowed()) {
        iterationCount = 1;
    }

    rv = sftkdb_passwordToKey(keydb, &salt, newPin, &newKey);
    if (rv != SECSuccess) {
        goto loser;
    }

    crv = sftkdb_convertObjects(keydb, NULL, 0, &newKey, iterationCount);
    if (crv != CKR_OK) {
        rv = SECFailure;
        goto loser;
    }
    /* fix up certdb macs */
    certdb = keydb->peerDB;
    if (certdb) {
        CK_ATTRIBUTE objectType = { CKA_CLASS, 0, sizeof(CK_OBJECT_CLASS) };
        CK_OBJECT_CLASS myClass = CKO_NSS_TRUST;

        objectType.pValue = &myClass;
        crv = sftkdb_convertObjects(certdb, &objectType, 1, &newKey,
                                    iterationCount);
        if (crv != CKR_OK) {
            rv = SECFailure;
            goto loser;
        }
        myClass = CKO_PUBLIC_KEY;
        crv = sftkdb_convertObjects(certdb, &objectType, 1, &newKey,
                                    iterationCount);
        if (crv != CKR_OK) {
            rv = SECFailure;
            goto loser;
        }
        myClass = CKO_TRUST;
        crv = sftkdb_convertObjects(certdb, &objectType, 1, &newKey,
                                    iterationCount);
        if (crv != CKR_OK) {
            rv = SECFailure;
            goto loser;
        }
    }

    plainText.data = (unsigned char *)SFTK_PW_CHECK_STRING;
    plainText.len = SFTK_PW_CHECK_LEN;

    rv = sftkdb_EncryptAttribute(NULL, keydb, keydb->db, &newKey,
                                 iterationCount, CK_INVALID_HANDLE,
                                 CKT_INVALID_TYPE, &plainText, &result);
    if (rv != SECSuccess) {
        goto loser;
    }
    value.data = result->data;
    value.len = result->len;
    crv = (*keydb->db->sdb_PutMetaData)(keydb->db, "password", &salt, &value);
    if (crv != CKR_OK) {
        rv = SECFailure;
        goto loser;
    }
    crv = (*keydb->db->sdb_Commit)(keydb->db);
    if (crv != CKR_OK) {
        rv = SECFailure;
        goto loser;
    }

    keydb->newKey = NULL;
    sftkdb_switchKeys(keydb, &newKey, iterationCount);

loser:
    if (newKey.data) {
        PORT_ZFree(newKey.data, newKey.len);
    }
    if (result) {
        SECITEM_FreeItem(result, PR_TRUE);
    }
    if (rv != SECSuccess) {
        (*keydb->db->sdb_Abort)(keydb->db);
    }
    return rv;
}

/*
 * Silently upgrade the KDF iteration count on login when it is below the
 * current minimum (bug 1719827).  The password has already been verified by
 * sftkdb_finishPasswordCheck; passing skipOldPinCheck=PR_TRUE avoids the
 * recursive call back into sftkdb_CheckPassword.
 */
static SECStatus
sftkdb_upgradeKDF(SFTKDBHandle *keydb, const char *pw)
{
    PRBool tokenRemoved = PR_FALSE;
    return sftkdb_reencryptDatabase(keydb, pw, pw, &tokenRemoved, PR_TRUE);
}

/*
 * change the database password.
 */
SECStatus
sftkdb_ChangePassword(SFTKDBHandle *keydb,
                      char *oldPin, char *newPin, PRBool *tokenRemoved)
{
    return sftkdb_reencryptDatabase(keydb, oldPin, newPin, tokenRemoved,
                                    PR_FALSE);
}

/*
 * lose our cached password
 */
SECStatus
sftkdb_ClearPassword(SFTKDBHandle *keydb)
{
    SECItem oldKey;
    oldKey.data = NULL;
    oldKey.len = 0;
    sftkdb_switchKeys(keydb, &oldKey, 1);
    if (oldKey.data) {
        PORT_ZFree(oldKey.data, oldKey.len);
    }
    return SECSuccess;
}
