/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <limits.h> /* for UINT_MAX */

#include "plarena.h"

#include "seccomon.h"
#include "secitem.h"
#include "secport.h"
#include "hasht.h"
#include "pkcs11t.h"
#include "blapi.h"
#include "hasht.h"
#include "secasn1.h"
#include "secder.h"
#include "lowpbe.h"
#include "secoid.h"
#include "alghmac.h"
#include "softoken.h"
#include "secerr.h"
#include "pkcs11i.h"

SEC_ASN1_MKSUB(SECOID_AlgorithmIDTemplate)

/* Upper bound on PBE iteration counts accepted from parsed algorithm
 * parameters.  This prevents denial-of-service from crafted input
 * (e.g. PKCS#12 files with extreme iteration counts). */
#ifdef DEBUG
#define MAX_ITERATION_COUNT 600000
#else
#define MAX_ITERATION_COUNT 100000000
#endif

/* how much a crypto encrypt/decryption may expand a buffer */
#define MAX_CRYPTO_EXPANSION 64

/* template for PKCS 5 PBE Parameter.  This template has been expanded
 * based upon the additions in PKCS 12.  This should eventually be moved
 * if RSA updates PKCS 5.
 */
static const SEC_ASN1Template NSSPKCS5PBEParameterTemplate[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(NSSPKCS5PBEParameter) },
    { SEC_ASN1_OCTET_STRING,
      offsetof(NSSPKCS5PBEParameter, salt) },
    { SEC_ASN1_INTEGER,
      offsetof(NSSPKCS5PBEParameter, iteration) },
    { 0 }
};

static const SEC_ASN1Template NSSPKCS5PKCS12V2PBEParameterTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(NSSPKCS5PBEParameter) },
    { SEC_ASN1_OCTET_STRING, offsetof(NSSPKCS5PBEParameter, salt) },
    { SEC_ASN1_INTEGER, offsetof(NSSPKCS5PBEParameter, iteration) },
    { 0 }
};

/* PKCS5 v2 */

struct nsspkcs5V2PBEParameterStr {
    SECAlgorithmID keyParams; /* parameters of the key generation */
    SECAlgorithmID algParams; /* parameters for the encryption or mac op */
};

typedef struct nsspkcs5V2PBEParameterStr nsspkcs5V2PBEParameter;

static const SEC_ASN1Template NSSPKCS5V2PBES2ParameterTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(nsspkcs5V2PBEParameter) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(nsspkcs5V2PBEParameter, keyParams),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(nsspkcs5V2PBEParameter, algParams),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { 0 }
};

static const SEC_ASN1Template NSSPKCS5V2PBEParameterTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(NSSPKCS5PBEParameter) },
    /* this is really a choice, but since we don't understand any other
     * choice, just inline it. */
    { SEC_ASN1_OCTET_STRING, offsetof(NSSPKCS5PBEParameter, salt) },
    { SEC_ASN1_INTEGER, offsetof(NSSPKCS5PBEParameter, iteration) },
    { SEC_ASN1_INTEGER, offsetof(NSSPKCS5PBEParameter, keyLength) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(NSSPKCS5PBEParameter, prfAlg),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { 0 }
};

SECStatus
nsspkcs5_HashBuf(const SECHashObject *hashObj, unsigned char *dest,
                 unsigned char *src, int len)
{
    void *ctx;
    unsigned int retLen;

    ctx = hashObj->create();
    if (ctx == NULL) {
        return SECFailure;
    }
    hashObj->begin(ctx);
    hashObj->update(ctx, src, len);
    hashObj->end(ctx, dest, &retLen, hashObj->length);
    hashObj->destroy(ctx, PR_TRUE);
    return SECSuccess;
}

/* generate bits using any hash
 */
static SECItem *
nsspkcs5_PBKDF1(const SECHashObject *hashObj, SECItem *salt, SECItem *pwd,
                int iter, PRBool faulty3DES)
{
    SECItem *hash = NULL, *pre_hash = NULL;
    SECStatus rv = SECFailure;

    if ((salt == NULL) || (pwd == NULL) || (iter < 0)) {
        return NULL;
    }

    /* salt->len + pwd->len is used below to size pre_hash and is also copied
     * out in two pieces; reject inputs where the sum would wrap so we can't
     * under-allocate and then overflow the buffer. */
    if (salt->len >= UINT_MAX - pwd->len) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    hash = (SECItem *)PORT_ZAlloc(sizeof(SECItem));
    pre_hash = (SECItem *)PORT_ZAlloc(sizeof(SECItem));

    if ((hash != NULL) && (pre_hash != NULL)) {
        int i, ph_len;

        ph_len = hashObj->length;
        if ((salt->len + pwd->len) > hashObj->length) {
            ph_len = salt->len + pwd->len;
        }

        rv = SECFailure;

        /* allocate buffers */
        hash->len = hashObj->length;
        hash->data = (unsigned char *)PORT_ZAlloc(hash->len);
        pre_hash->data = (unsigned char *)PORT_ZAlloc(ph_len);

        /* in pbeSHA1TripleDESCBC there was an allocation error that made
         * it into the caller.  We do not want to propagate those errors
         * further, so we are doing it correctly, but reading the old method.
         */
        if (faulty3DES) {
            pre_hash->len = ph_len;
        } else {
            pre_hash->len = salt->len + pwd->len;
        }

        /* preform hash */
        if ((hash->data != NULL) && (pre_hash->data != NULL)) {
            rv = SECSuccess;
            /* check for 0 length password */
            if (pwd->len > 0) {
                PORT_Memcpy(pre_hash->data, pwd->data, pwd->len);
            }
            if (salt->len > 0) {
                PORT_Memcpy((pre_hash->data + pwd->len), salt->data, salt->len);
            }
            for (i = 0; ((i < iter) && (rv == SECSuccess)); i++) {
                rv = nsspkcs5_HashBuf(hashObj, hash->data,
                                      pre_hash->data, pre_hash->len);
                if (rv != SECFailure) {
                    pre_hash->len = hashObj->length;
                    PORT_Memcpy(pre_hash->data, hash->data, hashObj->length);
                }
            }
        }
    }

    if (pre_hash != NULL) {
        SECITEM_ZfreeItem(pre_hash, PR_TRUE);
    }

    if ((rv != SECSuccess) && (hash != NULL)) {
        SECITEM_ZfreeItem(hash, PR_TRUE);
        hash = NULL;
    }

    return hash;
}

/* this bit generation routine is described in PKCS 12 and the proposed
 * extensions to PKCS 5.  an initial hash is generated following the
 * instructions laid out in PKCS 5.  If the number of bits generated is
 * insufficient, then the method discussed in the proposed extensions to
 * PKCS 5 in PKCS 12 are used.  This extension makes use of the HMAC
 * function.  And the P_Hash function from the TLS standard.
 */
static SECItem *
nsspkcs5_PFXPBE(const SECHashObject *hashObj, NSSPKCS5PBEParameter *pbe_param,
                SECItem *init_hash, unsigned int bytes_needed)
{
    SECItem *ret_bits = NULL;
    int hash_size = 0;
    unsigned int i;
    unsigned int hash_iter;
    unsigned int dig_len;
    SECStatus rv = SECFailure;
    unsigned char *state = NULL;
    unsigned int state_len;
    HMACContext *cx = NULL;

    hash_size = hashObj->length;
    hash_iter = (bytes_needed + (unsigned int)hash_size - 1) / hash_size;

    /* allocate return buffer */
    ret_bits = (SECItem *)PORT_ZAlloc(sizeof(SECItem));
    if (ret_bits == NULL)
        return NULL;
    ret_bits->data = (unsigned char *)PORT_ZAlloc((hash_iter * hash_size) + 1);
    ret_bits->len = (hash_iter * hash_size);
    if (ret_bits->data == NULL) {
        PORT_Free(ret_bits);
        return NULL;
    }

    /* allocate intermediate hash buffer.  8 is for the 8 bytes of
     * data which are added based on iteration number
     */

    if ((unsigned int)hash_size > pbe_param->salt.len) {
        state_len = hash_size;
    } else {
        state_len = pbe_param->salt.len;
    }
    state = (unsigned char *)PORT_ZAlloc(state_len);
    if (state == NULL) {
        rv = SECFailure;
        goto loser;
    }
    if (pbe_param->salt.len > 0) {
        PORT_Memcpy(state, pbe_param->salt.data, pbe_param->salt.len);
    }

    cx = HMAC_Create(hashObj, init_hash->data, init_hash->len, PR_TRUE);
    if (cx == NULL) {
        rv = SECFailure;
        goto loser;
    }

    for (i = 0; i < hash_iter; i++) {

        /* generate output bits */
        HMAC_Begin(cx);
        HMAC_Update(cx, state, state_len);
        HMAC_Update(cx, pbe_param->salt.data, pbe_param->salt.len);
        rv = HMAC_Finish(cx, ret_bits->data + (i * hash_size),
                         &dig_len, hash_size);
        if (rv != SECSuccess)
            goto loser;
        PORT_Assert((unsigned int)hash_size == dig_len);

        /* generate new state */
        HMAC_Begin(cx);
        HMAC_Update(cx, state, state_len);
        rv = HMAC_Finish(cx, state, &state_len, state_len);
        if (rv != SECSuccess)
            goto loser;
        PORT_Assert(state_len == dig_len);
    }

loser:
    if (state != NULL)
        PORT_ZFree(state, state_len);
    HMAC_Destroy(cx, PR_TRUE);

    if (rv != SECSuccess) {
        SECITEM_ZfreeItem(ret_bits, PR_TRUE);
        ret_bits = NULL;
    }

    return ret_bits;
}

/* generate bits for the key and iv determination.  if enough bits
 * are not generated using PKCS 5, then we need to generate more bits
 * based on the extension proposed in PKCS 12
 */
static SECItem *
nsspkcs5_PBKDF1Extended(const SECHashObject *hashObj,
                        NSSPKCS5PBEParameter *pbe_param, SECItem *pwitem, PRBool faulty3DES)
{
    SECItem *hash = NULL;
    SECItem *newHash = NULL;
    int bytes_needed;
    int bytes_available;

    bytes_needed = pbe_param->ivLen + pbe_param->keyLen;
    bytes_available = hashObj->length;

    hash = nsspkcs5_PBKDF1(hashObj, &pbe_param->salt, pwitem,
                           pbe_param->iter, faulty3DES);

    if (hash == NULL) {
        return NULL;
    }

    if (bytes_needed <= bytes_available) {
        return hash;
    }

    newHash = nsspkcs5_PFXPBE(hashObj, pbe_param, hash, bytes_needed);
    if (hash != newHash)
        SECITEM_ZfreeItem(hash, PR_TRUE);
    return newHash;
}

/*
 * PBDKDF2 is PKCS #5 v2.0 it's currently not used by NSS
 */
static void
do_xor(unsigned char *dest, unsigned char *src, int len)
{
    /* use byt xor, not all platforms are happy about inaligned
     * integer fetches */
    while (len--) {
        *dest = *dest ^ *src;
        dest++;
        src++;
    }
}

static SECStatus
nsspkcs5_PBKDF2_F(const SECHashObject *hashobj, SECItem *pwitem, SECItem *salt,
                  int iterations, unsigned int i, unsigned char *T)
{
    int j;
    HMACContext *cx = NULL;
    unsigned int hLen = hashobj->length;
    SECStatus rv = SECFailure;
    unsigned char *last = NULL;
    unsigned int lastLength;
    unsigned int lastBufLength;

    /* salt->len + 4 (the 4-byte block index is appended) must not wrap;
     * otherwise lastBufLength would undersize the buffer that the
     * PORT_Memcpy below fills with salt->len bytes. */
    if (salt->len >= UINT_MAX - 4) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        goto loser;
    }
    lastLength = salt->len + 4;

    cx = HMAC_Create(hashobj, pwitem->data, pwitem->len, PR_FALSE);
    if (cx == NULL) {
        goto loser;
    }
    PORT_Memset(T, 0, hLen);
    lastBufLength = PR_MAX(lastLength, hLen);
    last = PORT_Alloc(lastBufLength);
    if (last == NULL) {
        goto loser;
    }
    PORT_Memcpy(last, salt->data, salt->len);
    last[salt->len] = (i >> 24) & 0xff;
    last[salt->len + 1] = (i >> 16) & 0xff;
    last[salt->len + 2] = (i >> 8) & 0xff;
    last[salt->len + 3] = i & 0xff;

    /* NOTE: we need at least one iteration to return success! */
    for (j = 0; j < iterations; j++) {
        HMAC_Begin(cx);
        HMAC_Update(cx, last, lastLength);
        rv = HMAC_Finish(cx, last, &lastLength, hLen);
        if (rv != SECSuccess) {
            break;
        }
        do_xor(T, last, hLen);
    }
loser:
    if (cx) {
        HMAC_Destroy(cx, PR_TRUE);
    }
    if (last) {
        PORT_ZFree(last, lastBufLength);
    }
    return rv;
}

static SECItem *
nsspkcs5_PBKDF2(const SECHashObject *hashobj, NSSPKCS5PBEParameter *pbe_param,
                SECItem *pwitem)
{
    int iterations = pbe_param->iter;
    int bytesNeeded = pbe_param->keyLen;
    unsigned int dkLen = bytesNeeded;
    unsigned int hLen = hashobj->length;
    unsigned int nblocks = (dkLen + hLen - 1) / hLen;
    unsigned int i;
    unsigned char *rp;
    unsigned char *T = NULL;
    SECItem *result = NULL;
    SECItem *salt = &pbe_param->salt;
    SECStatus rv = SECFailure;

    result = SECITEM_AllocItem(NULL, NULL, nblocks * hLen);
    if (result == NULL) {
        return NULL;
    }

    T = PORT_Alloc(hLen);
    if (T == NULL) {
        goto loser;
    }

    for (i = 1, rp = result->data; i <= nblocks; i++, rp += hLen) {
        rv = nsspkcs5_PBKDF2_F(hashobj, pwitem, salt, iterations, i, T);
        if (rv != SECSuccess) {
            break;
        }
        PORT_Memcpy(rp, T, hLen);
    }

loser:
    if (T) {
        PORT_ZFree(T, hLen);
    }
    if (rv != SECSuccess) {
        SECITEM_ZfreeItem(result, PR_TRUE);
        result = NULL;
    } else {
        result->len = dkLen;
    }

    return result;
}

#define NSSPBE_ROUNDUP(x, y) ((((x) + ((y)-1)) / (y)) * (y))
#define NSSPBE_MIN(x, y) ((x) < (y) ? (x) : (y))
/*
 * This is the extended PBE function defined by the final PKCS #12 spec.
 */
static SECItem *
nsspkcs5_PKCS12PBE(const SECHashObject *hashObject,
                   NSSPKCS5PBEParameter *pbe_param, SECItem *pwitem,
                   PBEBitGenID bitGenPurpose, unsigned int bytesNeeded)
{
    PLArenaPool *arena = NULL;
    unsigned int SLen, PLen;
    unsigned int hashLength = hashObject->length;
    unsigned char *S, *P;
    SECItem *A = NULL, B, D, I;
    SECItem *salt = &pbe_param->salt;
    unsigned int c, i = 0;
    unsigned int hashLen;
    int iter;
    unsigned char *iterBuf;
    void *hash = NULL;
    unsigned int bufferLength;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (!arena) {
        return NULL;
    }

    /* how many hash object lengths are needed */
    c = (bytesNeeded + (hashLength - 1)) / hashLength;

    /* 64 if 0 < hashLength <= 32, 128 if 32 < hashLength <= 64 */
    bufferLength = NSSPBE_ROUNDUP(hashLength * 2, 64);

    /* initialize our buffers */
    D.len = bufferLength;
    /* B and D are the same length, use one alloc go get both */
    D.data = (unsigned char *)PORT_ArenaZAlloc(arena, D.len * 2);
    B.len = D.len;
    B.data = D.data + D.len;

    /* if all goes well, A will be returned, so don't use our temp arena */
    A = SECITEM_AllocItem(NULL, NULL, c * hashLength);
    if (A == NULL) {
        goto loser;
    }

    if (salt->len >= UINT_MAX - bufferLength ||
        pwitem->len >= UINT_MAX - bufferLength) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        goto loser;
    }
    SLen = NSSPBE_ROUNDUP(salt->len, bufferLength);
    PLen = NSSPBE_ROUNDUP(pwitem->len, bufferLength);
    if (SLen > UINT_MAX - PLen) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        goto loser;
    }
    I.len = SLen + PLen;
    I.data = (unsigned char *)PORT_ArenaZAlloc(arena, I.len);
    if (I.data == NULL) {
        goto loser;
    }

    /* S & P are only used to initialize I */
    S = I.data;
    P = S + SLen;

    PORT_Memset(D.data, (char)bitGenPurpose, D.len);
    if (SLen) {
        for (i = 0; i < SLen; i += salt->len) {
            PORT_Memcpy(S + i, salt->data, NSSPBE_MIN(SLen - i, salt->len));
        }
    }
    if (PLen) {
        for (i = 0; i < PLen; i += pwitem->len) {
            PORT_Memcpy(P + i, pwitem->data, NSSPBE_MIN(PLen - i, pwitem->len));
        }
    }

    iterBuf = (unsigned char *)PORT_ArenaZAlloc(arena, hashLength);
    if (iterBuf == NULL) {
        goto loser;
    }

    hash = hashObject->create();
    if (!hash) {
        goto loser;
    }
    /* calculate the PBE now */
    for (i = 0; i < c; i++) {
        int Bidx; /* must be signed or the for loop won't terminate */
        unsigned int k, j;
        unsigned char *Ai = A->data + i * hashLength;

        for (iter = 0; iter < pbe_param->iter; iter++) {
            hashObject->begin(hash);

            if (iter) {
                hashObject->update(hash, iterBuf, hashLen);
            } else {
                hashObject->update(hash, D.data, D.len);
                hashObject->update(hash, I.data, I.len);
            }

            hashObject->end(hash, iterBuf, &hashLen, hashObject->length);
            if (hashLen != hashObject->length) {
                break;
            }
        }

        PORT_Memcpy(Ai, iterBuf, hashLength);
        for (Bidx = 0; Bidx < (int)B.len; Bidx += hashLength) {
            PORT_Memcpy(B.data + Bidx, iterBuf, NSSPBE_MIN(B.len - Bidx, hashLength));
        }

        k = I.len / B.len;
        for (j = 0; j < k; j++) {
            unsigned int q, carryBit;
            unsigned char *Ij = I.data + j * B.len;

            /* (Ij = Ij+B+1) */
            for (Bidx = (B.len - 1), q = 1, carryBit = 0; Bidx >= 0; Bidx--, q = 0) {
                q += (unsigned int)Ij[Bidx];
                q += (unsigned int)B.data[Bidx];
                q += carryBit;

                carryBit = (q > 0xff);
                Ij[Bidx] = (unsigned char)(q & 0xff);
            }
        }
    }
loser:
    if (hash) {
        hashObject->destroy(hash, PR_TRUE);
    }
    if (arena) {
        PORT_FreeArena(arena, PR_TRUE);
    }

    if (A) {
        /* if i != c, then we didn't complete the loop above and must of failed
         * somwhere along the way */
        if (i != c) {
            SECITEM_ZfreeItem(A, PR_TRUE);
            A = NULL;
        } else {
            A->len = bytesNeeded;
        }
    }

    return A;
}

struct KDFCacheItemStr {
    SECItem *hash;
    SECItem *salt;
    SECItem *pwItem;
    HASH_HashType hashType;
    int iterations;
    int keyLen;
};
typedef struct KDFCacheItemStr KDFCacheItem;

/* Bug 1606992 - Cache the hash result for the common case that we're
 * asked to repeatedly compute the key for the same password item,
 * hash, iterations and salt. */
#define KDF2_CACHE_COUNT 150
static struct {
    PRLock *lock;
    struct {
        KDFCacheItem common;
        int ivLen;
        PRBool faulty3DES;
    } cacheKDF1;
    struct {
        KDFCacheItem common[KDF2_CACHE_COUNT];
        int next;
    } cacheKDF2;
} PBECache;

void
sftk_PBELockInit(void)
{
    if (!PBECache.lock) {
        PBECache.lock = PR_NewLock();
    }
}

static void
sftk_clearPBECommonCacheItemsLocked(KDFCacheItem *item)
{
    if (item->hash) {
        SECITEM_ZfreeItem(item->hash, PR_TRUE);
        item->hash = NULL;
    }
    if (item->salt) {
        SECITEM_ZfreeItem(item->salt, PR_TRUE);
        item->salt = NULL;
    }
    if (item->pwItem) {
        SECITEM_ZfreeItem(item->pwItem, PR_TRUE);
        item->pwItem = NULL;
    }
}

static void
sftk_setPBECommonCacheItemsKDFLocked(KDFCacheItem *cacheItem,
                                     const SECItem *hash,
                                     const NSSPKCS5PBEParameter *pbe_param,
                                     const SECItem *pwItem)
{
    cacheItem->hash = SECITEM_DupItem(hash);
    cacheItem->hashType = pbe_param->hashType;
    cacheItem->iterations = pbe_param->iter;
    cacheItem->keyLen = pbe_param->keyLen;
    cacheItem->salt = SECITEM_DupItem(&pbe_param->salt);
    cacheItem->pwItem = SECITEM_DupItem(pwItem);
}

static void
sftk_setPBECacheKDF2(const SECItem *hash,
                     const NSSPKCS5PBEParameter *pbe_param,
                     const SECItem *pwItem)
{
    PR_Lock(PBECache.lock);
    KDFCacheItem *next = &PBECache.cacheKDF2.common[PBECache.cacheKDF2.next];

    sftk_clearPBECommonCacheItemsLocked(next);

    sftk_setPBECommonCacheItemsKDFLocked(next, hash, pbe_param, pwItem);
    PBECache.cacheKDF2.next++;
    if (PBECache.cacheKDF2.next >= KDF2_CACHE_COUNT) {
        PBECache.cacheKDF2.next = 0;
    }

    PR_Unlock(PBECache.lock);
}

static void
sftk_setPBECacheKDF1(const SECItem *hash,
                     const NSSPKCS5PBEParameter *pbe_param,
                     const SECItem *pwItem,
                     PRBool faulty3DES)
{
    PR_Lock(PBECache.lock);

    sftk_clearPBECommonCacheItemsLocked(&PBECache.cacheKDF1.common);

    sftk_setPBECommonCacheItemsKDFLocked(&PBECache.cacheKDF1.common,
                                         hash, pbe_param, pwItem);
    PBECache.cacheKDF1.faulty3DES = faulty3DES;
    PBECache.cacheKDF1.ivLen = pbe_param->ivLen;

    PR_Unlock(PBECache.lock);
}

static PRBool
sftk_comparePBECommonCacheItemLocked(const KDFCacheItem *cacheItem,
                                     const NSSPKCS5PBEParameter *pbe_param,
                                     const SECItem *pwItem)
{
    return (cacheItem->hash &&
            cacheItem->salt &&
            cacheItem->pwItem &&
            pbe_param->hashType == cacheItem->hashType &&
            pbe_param->iter == cacheItem->iterations &&
            pbe_param->keyLen == cacheItem->keyLen &&
            SECITEM_ItemsAreEqual(&pbe_param->salt, cacheItem->salt) &&
            SECITEM_ItemsAreEqual(pwItem, cacheItem->pwItem));
}

static SECItem *
sftk_getPBECacheKDF2(const NSSPKCS5PBEParameter *pbe_param,
                     const SECItem *pwItem)
{
    SECItem *result = NULL;
    int i;

    PR_Lock(PBECache.lock);
    for (i = 0; i < KDF2_CACHE_COUNT; i++) {
        const KDFCacheItem *cacheItem = &PBECache.cacheKDF2.common[i];
        if (sftk_comparePBECommonCacheItemLocked(cacheItem,
                                                 pbe_param, pwItem)) {
            result = SECITEM_DupItem(cacheItem->hash);
            break;
        }
    }
    PR_Unlock(PBECache.lock);

    return result;
}

static SECItem *
sftk_getPBECacheKDF1(const NSSPKCS5PBEParameter *pbe_param,
                     const SECItem *pwItem,
                     PRBool faulty3DES)
{
    SECItem *result = NULL;
    const KDFCacheItem *cacheItem = &PBECache.cacheKDF1.common;

    PR_Lock(PBECache.lock);
    if (sftk_comparePBECommonCacheItemLocked(cacheItem, pbe_param, pwItem) &&
        PBECache.cacheKDF1.faulty3DES == faulty3DES &&
        PBECache.cacheKDF1.ivLen == pbe_param->ivLen) {
        result = SECITEM_DupItem(cacheItem->hash);
    }
    PR_Unlock(PBECache.lock);

    return result;
}

void
sftk_PBELockShutdown(void)
{
    int i;
    if (PBECache.lock) {
        PR_DestroyLock(PBECache.lock);
        PBECache.lock = 0;
    }
    sftk_clearPBECommonCacheItemsLocked(&PBECache.cacheKDF1.common);
    for (i = 0; i < KDF2_CACHE_COUNT; i++) {
        sftk_clearPBECommonCacheItemsLocked(&PBECache.cacheKDF2.common[i]);
    }
    PBECache.cacheKDF2.next = 0;
}

/*
 * generate key as per PKCS 5
 */
SECItem *
nsspkcs5_ComputeKeyAndIV(NSSPKCS5PBEParameter *pbe_param, SECItem *pwitem,
                         SECItem *iv, PRBool faulty3DES)
{
    SECItem *hash = NULL, *key = NULL;
    const SECHashObject *hashObj;
    PRBool getIV = PR_FALSE;

    if ((pbe_param == NULL) || (pwitem == NULL)) {
        return NULL;
    }

    if (pbe_param->iter > MAX_ITERATION_COUNT) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    key = SECITEM_AllocItem(NULL, NULL, pbe_param->keyLen);
    if (key == NULL) {
        return NULL;
    }

    if (iv && (pbe_param->ivLen) && (iv->data == NULL)) {
        getIV = PR_TRUE;
        iv->data = (unsigned char *)PORT_Alloc(pbe_param->ivLen);
        if (iv->data == NULL) {
            goto loser;
        }
        iv->len = pbe_param->ivLen;
    }

    hashObj = HASH_GetRawHashObject(pbe_param->hashType);
    switch (pbe_param->pbeType) {
        case NSSPKCS5_PBKDF1:
            hash = sftk_getPBECacheKDF1(pbe_param, pwitem, faulty3DES);
            if (!hash) {
                hash = nsspkcs5_PBKDF1Extended(hashObj, pbe_param, pwitem, faulty3DES);
                sftk_setPBECacheKDF1(hash, pbe_param, pwitem, faulty3DES);
            }
            if (hash == NULL) {
                goto loser;
            }
            PORT_Assert(hash->len >= key->len + (getIV ? iv->len : 0));
            if (getIV) {
                PORT_Memcpy(iv->data, hash->data + (hash->len - iv->len), iv->len);
            }

            break;
        case NSSPKCS5_PBKDF2:
            hash = sftk_getPBECacheKDF2(pbe_param, pwitem);
            if (!hash) {
                hash = nsspkcs5_PBKDF2(hashObj, pbe_param, pwitem);
                sftk_setPBECacheKDF2(hash, pbe_param, pwitem);
            }
            if (getIV) {
                PORT_Memcpy(iv->data, pbe_param->ivData, iv->len);
            }
            break;
        case NSSPKCS5_PKCS12_V2:
            if (getIV) {
                hash = nsspkcs5_PKCS12PBE(hashObj, pbe_param, pwitem,
                                          pbeBitGenCipherIV, iv->len);
                if (hash == NULL) {
                    goto loser;
                }
                PORT_Memcpy(iv->data, hash->data, iv->len);
                SECITEM_ZfreeItem(hash, PR_TRUE);
                hash = NULL;
            }
            hash = nsspkcs5_PKCS12PBE(hashObj, pbe_param, pwitem,
                                      pbe_param->keyID, key->len);
        default:
            break;
    }

    if (hash == NULL) {
        goto loser;
    }

    PORT_Memcpy(key->data, hash->data, key->len);

    SECITEM_ZfreeItem(hash, PR_TRUE);
    return key;

loser:
    if (getIV && iv->data) {
        PORT_ZFree(iv->data, iv->len);
        iv->data = NULL;
    }

    SECITEM_ZfreeItem(key, PR_TRUE);
    return NULL;
}

#define MAX_IV_LENGTH 64
/* get a random IV into the parameters */
static SECStatus
nsspkcs5_SetIVParam(NSSPKCS5PBEParameter *pbe_param, int ivLen)
{
    SECStatus rv;
    SECItem derIV;
    SECItem iv;
    SECItem *dummy = NULL;
    unsigned char ivData[MAX_IV_LENGTH];

    PORT_Assert(ivLen <= MAX_IV_LENGTH);

    /* Because of a bug in the decode section, the IV's not are expected
     * to be der encoded, but still need to parse as if they were der data.
     * because we want to be compatible with existing versions of nss that
     * have that bug, create an IV that looks like der data. That still
     * leaves 14 bytes of entropy in the IV  */
    rv = RNG_GenerateGlobalRandomBytes(ivData, ivLen - 2);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    derIV.data = NULL;
    derIV.len = 0;
    iv.data = ivData;
    iv.len = ivLen - 2;
    dummy = SEC_ASN1EncodeItem(pbe_param->poolp, &derIV, &iv,
                               SEC_ASN1_GET(SEC_OctetStringTemplate));
    if (dummy == NULL) {
        return SECFailure;
    }
    pbe_param->ivData = derIV.data;
    pbe_param->ivLen = derIV.len;
    PORT_Assert(pbe_param->ivLen == ivLen);
    return SECSuccess;
}

static SECStatus
nsspkcs5_FillInParam(SECOidTag algorithm, HASH_HashType hashType,
                     NSSPKCS5PBEParameter *pbe_param)
{
    PRBool skipType = PR_FALSE;
    SECStatus rv;

    pbe_param->keyLen = 5;
    pbe_param->ivLen = 8;
    pbe_param->hashType = hashType;
    pbe_param->pbeType = NSSPKCS5_PBKDF1;
    pbe_param->encAlg = SEC_OID_RC2_CBC;
    pbe_param->is2KeyDES = PR_FALSE;
    switch (algorithm) {
        /* DES3 Algorithms */
        case SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_2KEY_TRIPLE_DES_CBC:
            pbe_param->is2KeyDES = PR_TRUE;
            pbe_param->pbeType = NSSPKCS5_PKCS12_V2;
            pbe_param->keyLen = 16;
            pbe_param->encAlg = SEC_OID_DES_EDE3_CBC;
            break;
        case SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_3KEY_TRIPLE_DES_CBC:
            pbe_param->pbeType = NSSPKCS5_PKCS12_V2;
            pbe_param->keyLen = 24;
            pbe_param->encAlg = SEC_OID_DES_EDE3_CBC;
            break;
        case SEC_OID_PKCS12_PBE_WITH_SHA1_AND_TRIPLE_DES_CBC:
            pbe_param->keyLen = 24;
            pbe_param->encAlg = SEC_OID_DES_EDE3_CBC;
            break;

        /* DES Algorithms */
        case SEC_OID_PKCS5_PBE_WITH_MD2_AND_DES_CBC:
            pbe_param->hashType = HASH_AlgMD2;
            goto finish_des;
        case SEC_OID_PKCS5_PBE_WITH_MD5_AND_DES_CBC:
            pbe_param->hashType = HASH_AlgMD5;
        /* fall through */
        case SEC_OID_PKCS5_PBE_WITH_SHA1_AND_DES_CBC:
        finish_des:
            pbe_param->keyLen = 8;
            pbe_param->encAlg = SEC_OID_DES_CBC;
            break;

#ifndef NSS_DISABLE_DEPRECATED_RC2
        /* RC2 Algorithms */
        case SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_128_BIT_RC2_CBC:
            pbe_param->keyLen = 16;
        /* fall through */
        case SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_40_BIT_RC2_CBC:
            pbe_param->pbeType = NSSPKCS5_PKCS12_V2;
            break;
        case SEC_OID_PKCS12_PBE_WITH_SHA1_AND_128_BIT_RC2_CBC:
            pbe_param->keyLen = 16;
        /* fall through */
        case SEC_OID_PKCS12_PBE_WITH_SHA1_AND_40_BIT_RC2_CBC:
            break;
#endif

        /* RC4 algorithms */
        case SEC_OID_PKCS12_PBE_WITH_SHA1_AND_128_BIT_RC4:
            skipType = PR_TRUE;
        /* fall through */
        case SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_128_BIT_RC4:
            pbe_param->keyLen = 16;
        /* fall through */
        case SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_40_BIT_RC4:
            if (!skipType) {
                pbe_param->pbeType = NSSPKCS5_PKCS12_V2;
            }
        /* fall through */
        case SEC_OID_PKCS12_PBE_WITH_SHA1_AND_40_BIT_RC4:
            pbe_param->ivLen = 0;
            pbe_param->encAlg = SEC_OID_RC4;
            break;

        case SEC_OID_PKCS5_PBKDF2:
        case SEC_OID_PKCS5_PBES2:
        case SEC_OID_PKCS5_PBMAC1:
            /* everything else will be filled in by the template */
            pbe_param->ivLen = 0;
            pbe_param->pbeType = NSSPKCS5_PBKDF2;
            pbe_param->encAlg = SEC_OID_PKCS5_PBKDF2;
            pbe_param->keyLen = 0; /* needs to be set by caller after return */
            break;
        /* AES uses PBKDF2 */
        case SEC_OID_AES_128_CBC:
            rv = nsspkcs5_SetIVParam(pbe_param, 16);
            if (rv != SECSuccess) {
                return rv;
            }
            pbe_param->ivLen = 16;
            pbe_param->pbeType = NSSPKCS5_PBKDF2;
            pbe_param->encAlg = algorithm;
            pbe_param->keyLen = 128 / 8;
            break;
        case SEC_OID_AES_192_CBC:
            rv = nsspkcs5_SetIVParam(pbe_param, 16);
            if (rv != SECSuccess) {
                return rv;
            }
            pbe_param->pbeType = NSSPKCS5_PBKDF2;
            pbe_param->encAlg = algorithm;
            pbe_param->keyLen = 192 / 8;
            break;
        case SEC_OID_AES_256_CBC:
            rv = nsspkcs5_SetIVParam(pbe_param, 16);
            if (rv != SECSuccess) {
                return rv;
            }
            pbe_param->pbeType = NSSPKCS5_PBKDF2;
            pbe_param->encAlg = algorithm;
            pbe_param->keyLen = 256 / 8;
            break;
        case SEC_OID_AES_128_KEY_WRAP:
            pbe_param->ivLen = 0;
            pbe_param->pbeType = NSSPKCS5_PBKDF2;
            pbe_param->encAlg = algorithm;
            pbe_param->keyLen = 128 / 8;
            break;
        case SEC_OID_AES_192_KEY_WRAP:
            pbe_param->ivLen = 0;
            pbe_param->pbeType = NSSPKCS5_PBKDF2;
            pbe_param->encAlg = algorithm;
            pbe_param->keyLen = 192 / 8;
            break;
        case SEC_OID_AES_256_KEY_WRAP:
            pbe_param->ivLen = 0;
            pbe_param->pbeType = NSSPKCS5_PBKDF2;
            pbe_param->encAlg = algorithm;
            pbe_param->keyLen = 256 / 8;
            break;

        default:
            return SECFailure;
    }
    if (pbe_param->pbeType == NSSPKCS5_PBKDF2) {
        SECOidTag prfAlg = HASH_HMACOidFromHash(pbe_param->hashType);
        if (prfAlg == SEC_OID_UNKNOWN) {
            return SECFailure;
        }
        rv = SECOID_SetAlgorithmID(pbe_param->poolp, &pbe_param->prfAlg,
                                   prfAlg, NULL);
        if (rv != SECSuccess) {
            return rv;
        }
    }
    return SECSuccess;
}

/* decode the algid and generate a PKCS 5 parameter from it
 */
NSSPKCS5PBEParameter *
nsspkcs5_NewParam(SECOidTag alg, HASH_HashType hashType, SECItem *salt,
                  int iterationCount)
{
    PLArenaPool *arena = NULL;
    NSSPKCS5PBEParameter *pbe_param = NULL;
    SECStatus rv = SECFailure;

    arena = PORT_NewArena(SEC_ASN1_DEFAULT_ARENA_SIZE);
    if (arena == NULL)
        return NULL;

    /* allocate memory for the parameter */
    pbe_param = (NSSPKCS5PBEParameter *)PORT_ArenaZAlloc(arena,
                                                         sizeof(NSSPKCS5PBEParameter));

    if (pbe_param == NULL) {
        goto loser;
    }

    pbe_param->poolp = arena;

    rv = nsspkcs5_FillInParam(alg, hashType, pbe_param);
    if (rv != SECSuccess) {
        goto loser;
    }

    pbe_param->iter = iterationCount;
    if (salt) {
        rv = SECITEM_CopyItem(arena, &pbe_param->salt, salt);
    }

    /* default key gen */
    pbe_param->keyID = pbeBitGenCipherKey;

loser:
    if (rv != SECSuccess) {
        PORT_FreeArena(arena, PR_TRUE);
        pbe_param = NULL;
    }

    return pbe_param;
}

/*
 * find the hash type needed to implement a specific HMAC.
 * OID definitions are from pkcs 5 v2.0 and 2.1
 */
HASH_HashType
HASH_FromHMACOid(SECOidTag hmac)
{
    switch (hmac) {
        case SEC_OID_HMAC_SHA1:
            return HASH_AlgSHA1;
        case SEC_OID_HMAC_SHA256:
            return HASH_AlgSHA256;
        case SEC_OID_HMAC_SHA384:
            return HASH_AlgSHA384;
        case SEC_OID_HMAC_SHA512:
            return HASH_AlgSHA512;
        case SEC_OID_HMAC_SHA224:
        default:
            break;
    }
    return HASH_AlgNULL;
}

SECOidTag
HASH_HMACOidFromHash(HASH_HashType hashType)
{
    switch (hashType) {
        case HASH_AlgSHA1:
            return SEC_OID_HMAC_SHA1;
        case HASH_AlgSHA256:
            return SEC_OID_HMAC_SHA256;
        case HASH_AlgSHA384:
            return SEC_OID_HMAC_SHA384;
        case HASH_AlgSHA512:
            return SEC_OID_HMAC_SHA512;
        case HASH_AlgSHA224:
            return SEC_OID_HMAC_SHA224;
        case HASH_AlgMD2:
        case HASH_AlgMD5:
        case HASH_AlgTOTAL:
        default:
            break;
    }
    return SEC_OID_UNKNOWN;
}

/* decode the algid and generate a PKCS 5 parameter from it
 */
NSSPKCS5PBEParameter *
nsspkcs5_AlgidToParam(SECAlgorithmID *algid)
{
    NSSPKCS5PBEParameter *pbe_param = NULL;
    nsspkcs5V2PBEParameter pbev2_param;
    SECOidTag algorithm;
    SECStatus rv = SECFailure;

    if (algid == NULL) {
        return NULL;
    }

    algorithm = SECOID_GetAlgorithmTag(algid);
    if (algorithm == SEC_OID_UNKNOWN) {
        goto loser;
    }

    pbe_param = nsspkcs5_NewParam(algorithm, HASH_AlgSHA1, NULL, 1);
    if (pbe_param == NULL) {
        goto loser;
    }

    /* decode parameter */
    rv = SECFailure;
    switch (pbe_param->pbeType) {
        case NSSPKCS5_PBKDF1:
            rv = SEC_ASN1DecodeItem(pbe_param->poolp, pbe_param,
                                    NSSPKCS5PBEParameterTemplate, &algid->parameters);
            break;
        case NSSPKCS5_PKCS12_V2:
            rv = SEC_ASN1DecodeItem(pbe_param->poolp, pbe_param,
                                    NSSPKCS5PKCS12V2PBEParameterTemplate, &algid->parameters);
            break;
        case NSSPKCS5_PBKDF2:
            PORT_Memset(&pbev2_param, 0, sizeof(pbev2_param));
            /* just the PBE */
            if (algorithm == SEC_OID_PKCS5_PBKDF2) {
                rv = SEC_ASN1DecodeItem(pbe_param->poolp, pbe_param,
                                        NSSPKCS5V2PBEParameterTemplate, &algid->parameters);
            } else {
                /* PBE data an others */
                rv = SEC_ASN1DecodeItem(pbe_param->poolp, &pbev2_param,
                                        NSSPKCS5V2PBES2ParameterTemplate, &algid->parameters);
                if (rv != SECSuccess) {
                    break;
                }
                pbe_param->encAlg = SECOID_GetAlgorithmTag(&pbev2_param.algParams);
                rv = SEC_ASN1DecodeItem(pbe_param->poolp, pbe_param,
                                        NSSPKCS5V2PBEParameterTemplate,
                                        &pbev2_param.keyParams.parameters);
                if (rv != SECSuccess) {
                    break;
                }
                PORT_SetError(0);
                pbe_param->keyLen = DER_GetInteger(&pbe_param->keyLength);
                if (PORT_GetError() != 0) {
                    rv = SECFailure;
                    break;
                }
            }
            /* we we are encrypting, save any iv's */
            if (algorithm == SEC_OID_PKCS5_PBES2) {
                pbe_param->ivLen = pbev2_param.algParams.parameters.len;
                pbe_param->ivData = pbev2_param.algParams.parameters.data;
            }
            pbe_param->hashType =
                HASH_FromHMACOid(SECOID_GetAlgorithmTag(&pbe_param->prfAlg));
            if (pbe_param->hashType == HASH_AlgNULL) {
                PORT_SetError(SEC_ERROR_INVALID_ALGORITHM);
                rv = SECFailure;
            }
            break;
    }

loser:
    PORT_Memset(&pbev2_param, 0, sizeof(pbev2_param));
    if (rv == SECSuccess) {
        PORT_SetError(0);
        pbe_param->iter = DER_GetInteger(&pbe_param->iteration);
        if (PORT_GetError() != 0) {
            nsspkcs5_DestroyPBEParameter(pbe_param);
            pbe_param = NULL;
        }
    } else {
        nsspkcs5_DestroyPBEParameter(pbe_param);
        pbe_param = NULL;
    }

    return pbe_param;
}

/* destroy a pbe parameter.  it assumes that the parameter was
 * generated using the appropriate create function and therefor
 * contains an arena pool.
 */
void
nsspkcs5_DestroyPBEParameter(NSSPKCS5PBEParameter *pbe_param)
{
    if (pbe_param != NULL) {
        PORT_FreeArena(pbe_param->poolp, PR_TRUE);
    }
}

/* crypto routines */
/* perform DES encryption and decryption.  these routines are called
 * by nsspkcs5_CipherData.  In the case of an error, NULL is returned.
 */
static SECItem *
sec_pkcs5_des(SECItem *key, SECItem *iv, SECItem *src, PRBool triple_des,
              PRBool encrypt)
{
    SECItem *dest;
    SECItem *dup_src;
    CK_RV crv = CKR_DEVICE_ERROR;
    int error;
    SECStatus rv = SECFailure;
    DESContext *ctxt;
    unsigned int pad;

    if ((src == NULL) || (key == NULL) || (iv == NULL)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    dup_src = SECITEM_DupItem(src);
    if (dup_src == NULL) {
        return NULL;
    }

    if (encrypt != PR_FALSE) {
        void *dummy;

        dummy = CBC_PadBuffer(NULL, dup_src->data,
                              dup_src->len, &dup_src->len, DES_BLOCK_SIZE);
        if (dummy == NULL) {
            SECITEM_ZfreeItem(dup_src, PR_TRUE);
            return NULL;
        }
        dup_src->data = (unsigned char *)dummy;
    }

    dest = SECITEM_AllocItem(NULL, NULL, dup_src->len + MAX_CRYPTO_EXPANSION);
    if (dest == NULL) {
        goto loser;
    }
    ctxt = DES_CreateContext(key->data, iv->data,
                             (triple_des ? NSS_DES_EDE3_CBC : NSS_DES_CBC),
                             encrypt);
    if (ctxt == NULL) {
        goto loser;
    }
    rv = (encrypt ? DES_Encrypt : DES_Decrypt)(
        ctxt, dest->data, &dest->len,
        dest->len, dup_src->data, dup_src->len);

    crv = (rv == SECSuccess) ? CKR_OK : CKR_DEVICE_ERROR;
    error = PORT_GetError();

    /* remove padding */
    if ((encrypt == PR_FALSE) && (rv == SECSuccess)) {
        crv = sftk_CheckCBCPadding(dest->data, dest->len, DES_BLOCK_SIZE, &pad);
        dest->len = PORT_CT_SEL(sftk_CKRVToMask(crv), dest->len - pad, dest->len);
        PORT_SetError(PORT_CT_SEL(sftk_CKRVToMask(crv), error, SEC_ERROR_BAD_PASSWORD));
    }
    DES_DestroyContext(ctxt, PR_TRUE);

loser:
    if (crv != CKR_OK) {
        if (dest != NULL) {
            SECITEM_ZfreeItem(dest, PR_TRUE);
        }
        dest = NULL;
    }

    if (dup_src != NULL) {
        SECITEM_ZfreeItem(dup_src, PR_TRUE);
    }

    return dest;
}

/* perform aes encryption/decryption if an error occurs, NULL is returned
 */
static SECItem *
sec_pkcs5_aes(SECItem *key, SECItem *iv, SECItem *src, PRBool triple_des,
              PRBool encrypt)
{
    SECItem *dest;
    SECItem *dup_src;
    CK_RV crv = CKR_DEVICE_ERROR;
    int error;
    SECStatus rv = SECFailure;
    AESContext *ctxt;
    unsigned int pad;

    if ((src == NULL) || (key == NULL) || (iv == NULL)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    dup_src = SECITEM_DupItem(src);
    if (dup_src == NULL) {
        return NULL;
    }

    if (encrypt != PR_FALSE) {
        void *dummy;

        dummy = CBC_PadBuffer(NULL, dup_src->data,
                              dup_src->len, &dup_src->len, AES_BLOCK_SIZE);
        if (dummy == NULL) {
            SECITEM_ZfreeItem(dup_src, PR_TRUE);
            return NULL;
        }
        dup_src->data = (unsigned char *)dummy;
    }

    dest = SECITEM_AllocItem(NULL, NULL, dup_src->len + MAX_CRYPTO_EXPANSION);
    if (dest == NULL) {
        goto loser;
    }
    ctxt = AES_CreateContext(key->data, iv->data, NSS_AES_CBC,
                             encrypt, key->len, AES_BLOCK_SIZE);
    if (ctxt == NULL) {
        goto loser;
    }
    rv = (encrypt ? AES_Encrypt : AES_Decrypt)(
        ctxt, dest->data, &dest->len,
        dest->len, dup_src->data, dup_src->len);

    crv = (rv == SECSuccess) ? CKR_OK : CKR_DEVICE_ERROR;
    error = PORT_GetError();

    /* remove padding */
    if ((encrypt == PR_FALSE) && (rv == SECSuccess)) {
        crv = sftk_CheckCBCPadding(dest->data, dest->len, AES_BLOCK_SIZE, &pad);
        dest->len = PORT_CT_SEL(sftk_CKRVToMask(crv), dest->len - pad, dest->len);
        PORT_SetError(PORT_CT_SEL(sftk_CKRVToMask(crv), error, SEC_ERROR_BAD_PASSWORD));
    }
    AES_DestroyContext(ctxt, PR_TRUE);

loser:
    if (crv != CKR_OK) {
        if (dest != NULL) {
            SECITEM_ZfreeItem(dest, PR_TRUE);
        }
        dest = NULL;
    }

    if (dup_src != NULL) {
        SECITEM_ZfreeItem(dup_src, PR_TRUE);
    }

    return dest;
}

/* perform aes encryption/decryption if an error occurs, NULL is returned
 */
static SECItem *
sec_pkcs5_aes_key_wrap(SECItem *key, SECItem *iv, SECItem *src, PRBool triple_des,
                       PRBool encrypt)
{
    SECItem *dest;
    SECItem *dup_src;
    CK_RV crv = CKR_DEVICE_ERROR;
    int error;
    SECStatus rv = SECFailure;
    AESKeyWrapContext *ctxt;
    unsigned int pad;

    if ((src == NULL) || (key == NULL) || (iv == NULL)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    dup_src = SECITEM_DupItem(src);
    if (dup_src == NULL) {
        return NULL;
    }

    if (encrypt != PR_FALSE) {
        void *dummy;

        dummy = CBC_PadBuffer(NULL, dup_src->data,
                              dup_src->len, &dup_src->len, AES_BLOCK_SIZE);
        if (dummy == NULL) {
            SECITEM_ZfreeItem(dup_src, PR_TRUE);
            return NULL;
        }
        dup_src->data = (unsigned char *)dummy;
    }

    dest = SECITEM_AllocItem(NULL, NULL, dup_src->len + MAX_CRYPTO_EXPANSION);
    if (dest == NULL) {
        goto loser;
    }
    ctxt = AESKeyWrap_CreateContext(key->data, iv->data, encrypt,
                                    key->len);

    if (ctxt == NULL) {
        goto loser;
    }
    rv = (encrypt ? AESKeyWrap_Encrypt : AESKeyWrap_Decrypt)(
        ctxt, dest->data, &dest->len,
        dest->len, dup_src->data, dup_src->len);

    crv = (rv == SECSuccess) ? CKR_OK : CKR_DEVICE_ERROR;
    error = PORT_GetError();

    /* remove padding */
    if ((encrypt == PR_FALSE) && (rv == SECSuccess)) {
        crv = sftk_CheckCBCPadding(dest->data, dest->len, AES_BLOCK_SIZE, &pad);
        dest->len = PORT_CT_SEL(sftk_CKRVToMask(crv), dest->len - pad, dest->len);
        PORT_SetError(PORT_CT_SEL(sftk_CKRVToMask(crv), error, SEC_ERROR_BAD_PASSWORD));
    }
    AESKeyWrap_DestroyContext(ctxt, PR_TRUE);

loser:
    if (crv != CKR_OK) {
        if (dest != NULL) {
            SECITEM_ZfreeItem(dest, PR_TRUE);
        }
        dest = NULL;
    }

    if (dup_src != NULL) {
        SECITEM_ZfreeItem(dup_src, PR_TRUE);
    }

    return dest;
}

#ifndef NSS_DISABLE_DEPRECATED_RC2
/* perform rc2 encryption/decryption if an error occurs, NULL is returned
 */
static SECItem *
sec_pkcs5_rc2(SECItem *key, SECItem *iv, SECItem *src, PRBool dummy,
              PRBool encrypt)
{
    SECItem *dest;
    SECItem *dup_src;
    SECStatus rv = SECFailure;
    int pad;

    if ((src == NULL) || (key == NULL) || (iv == NULL)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    dup_src = SECITEM_DupItem(src);
    if (dup_src == NULL) {
        return NULL;
    }

    if (encrypt != PR_FALSE) {
        void *v;

        v = CBC_PadBuffer(NULL, dup_src->data,
                          dup_src->len, &dup_src->len, 8 /* RC2_BLOCK_SIZE */);
        if (v == NULL) {
            SECITEM_ZfreeItem(dup_src, PR_TRUE);
            return NULL;
        }
        dup_src->data = (unsigned char *)v;
    }

    dest = (SECItem *)PORT_ZAlloc(sizeof(SECItem));
    if (dest != NULL) {
        dest->data = (unsigned char *)PORT_ZAlloc(dup_src->len + 64);
        if (dest->data != NULL) {
            RC2Context *ctxt;

            ctxt = RC2_CreateContext(key->data, key->len, iv->data,
                                     NSS_RC2_CBC, key->len);

            if (ctxt != NULL) {
                rv = (encrypt ? RC2_Encrypt : RC2_Decrypt)(
                    ctxt, dest->data, &dest->len,
                    dup_src->len + 64, dup_src->data, dup_src->len);

                /* assumes 8 byte blocks  -- remove padding */
                if ((rv == SECSuccess) && (encrypt != PR_TRUE)) {
                    /* a padded plaintext holds at least one block; an empty
                     * decryption result would make dest->len - 1 wrap. */
                    if (dest->len < 8 /* RC2_BLOCK_SIZE */) {
                        PORT_SetError(SEC_ERROR_BAD_PASSWORD);
                        rv = SECFailure;
                    } else {
                        pad = dest->data[dest->len - 1];
                        if ((pad > 0) && (pad <= 8)) {
                            if (dest->data[dest->len - pad] != pad) {
                                PORT_SetError(SEC_ERROR_BAD_PASSWORD);
                                rv = SECFailure;
                            } else {
                                dest->len -= pad;
                            }
                        } else {
                            PORT_SetError(SEC_ERROR_BAD_PASSWORD);
                            rv = SECFailure;
                        }
                    }
                }
                RC2_DestroyContext(ctxt, PR_TRUE);
            }
        }
    }

    if ((rv != SECSuccess) && (dest != NULL)) {
        SECITEM_ZfreeItem(dest, PR_TRUE);
        dest = NULL;
    }

    if (dup_src != NULL) {
        SECITEM_ZfreeItem(dup_src, PR_TRUE);
    }

    return dest;
}
#endif /* NSS_DISABLE_DEPRECATED_RC2 */

/* perform rc4 encryption and decryption */
static SECItem *
sec_pkcs5_rc4(SECItem *key, SECItem *iv, SECItem *src, PRBool dummy_op,
              PRBool encrypt)
{
    SECItem *dest;
    SECStatus rv = SECFailure;

    if ((src == NULL) || (key == NULL) || (iv == NULL)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    dest = (SECItem *)PORT_ZAlloc(sizeof(SECItem));
    if (dest != NULL) {
        dest->data = (unsigned char *)PORT_ZAlloc(sizeof(unsigned char) *
                                                  (src->len + 64));
        if (dest->data != NULL) {
            RC4Context *ctxt;

            ctxt = RC4_CreateContext(key->data, key->len);
            if (ctxt) {
                rv = (encrypt ? RC4_Encrypt : RC4_Decrypt)(
                    ctxt, dest->data, &dest->len,
                    src->len + 64, src->data, src->len);
                RC4_DestroyContext(ctxt, PR_TRUE);
            }
        }
    }

    if ((rv != SECSuccess) && (dest)) {
        SECITEM_ZfreeItem(dest, PR_TRUE);
        dest = NULL;
    }

    return dest;
}
/* function pointer template for crypto functions */
typedef SECItem *(*pkcs5_crypto_func)(SECItem *key, SECItem *iv,
                                      SECItem *src, PRBool op1, PRBool op2);

/* performs the cipher operation on the src and returns the result.
 * if an error occurs, NULL is returned.
 *
 * a null length password is allowed.  this corresponds to encrypting
 * the data with ust the salt.
 */
/* change this to use PKCS 11? */
SECItem *
nsspkcs5_CipherData(NSSPKCS5PBEParameter *pbe_param, SECItem *pwitem,
                    SECItem *src, PRBool encrypt, PRBool *update)
{
    SECItem *key = NULL, iv;
    SECItem *dest = NULL;
    PRBool tripleDES = PR_TRUE;
    pkcs5_crypto_func cryptof;

    iv.data = NULL;

    if (update) {
        *update = PR_FALSE;
    }

    if ((pwitem == NULL) || (src == NULL)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    /* get key, and iv */
    key = nsspkcs5_ComputeKeyAndIV(pbe_param, pwitem, &iv, PR_FALSE);
    if (key == NULL) {
        return NULL;
    }

    switch (pbe_param->encAlg) {
        /* PKCS 5 v2 only */
        case SEC_OID_AES_128_KEY_WRAP:
        case SEC_OID_AES_192_KEY_WRAP:
        case SEC_OID_AES_256_KEY_WRAP:
            cryptof = sec_pkcs5_aes_key_wrap;
            break;
        case SEC_OID_AES_128_CBC:
        case SEC_OID_AES_192_CBC:
        case SEC_OID_AES_256_CBC:
            cryptof = sec_pkcs5_aes;
            break;
        case SEC_OID_DES_EDE3_CBC:
            cryptof = sec_pkcs5_des;
            tripleDES = PR_TRUE;
            break;
        case SEC_OID_DES_CBC:
            cryptof = sec_pkcs5_des;
            tripleDES = PR_FALSE;
            break;
#ifndef NSS_DISABLE_DEPRECATED_RC2
        case SEC_OID_RC2_CBC:
            cryptof = sec_pkcs5_rc2;
            break;
#endif
        case SEC_OID_RC4:
            cryptof = sec_pkcs5_rc4;
            break;
        default:
            cryptof = NULL;
            break;
    }

    if (cryptof == NULL) {
        goto loser;
    }

    dest = (*cryptof)(key, &iv, src, tripleDES, encrypt);
    /*
     * it's possible for some keys and keydb's to claim to
     * be triple des when they're really des. In this case
     * we simply try des. If des works we set the update flag
     * so the key db knows it needs to update all it's entries.
     *  The case can only happen on decrypted of a
     *  SEC_OID_DES_EDE3_CBD.
     */
    if ((pbe_param->encAlg == SEC_OID_DES_EDE3_CBC) &&
        (dest == NULL) && (encrypt == PR_FALSE)) {
        dest = (*cryptof)(key, &iv, src, PR_FALSE, encrypt);
        if (update && (dest != NULL))
            *update = PR_TRUE;
    }

loser:
    if (key != NULL) {
        SECITEM_ZfreeItem(key, PR_TRUE);
    }
    if (iv.data != NULL) {
        SECITEM_ZfreeItem(&iv, PR_FALSE);
    }

    return dest;
}

/* creates a algorithm ID containing the PBE algorithm and appropriate
 * parameters.  the required parameter is the algorithm.  if salt is
 * not specified, it is generated randomly.  if IV is specified, it overrides
 * the PKCS 5 generation of the IV.
 *
 * the returned SECAlgorithmID should be destroyed using
 * SECOID_DestroyAlgorithmID
 */
SECAlgorithmID *
nsspkcs5_CreateAlgorithmID(PLArenaPool *arena, SECOidTag algorithm,
                           NSSPKCS5PBEParameter *pbe_param)
{
    SECAlgorithmID *algid, *ret_algid = NULL;
    SECItem der_param;
    nsspkcs5V2PBEParameter pkcs5v2_param;

    SECStatus rv = SECFailure;
    void *dummy = NULL;

    if (arena == NULL) {
        return NULL;
    }

    der_param.data = NULL;
    der_param.len = 0;

    /* generate the algorithm id */
    algid = (SECAlgorithmID *)PORT_ArenaZAlloc(arena, sizeof(SECAlgorithmID));
    if (algid == NULL) {
        goto loser;
    }

    if (pbe_param->iteration.data == NULL) {
        dummy = SEC_ASN1EncodeInteger(pbe_param->poolp, &pbe_param->iteration,
                                      pbe_param->iter);
        if (dummy == NULL) {
            goto loser;
        }
    }
    switch (pbe_param->pbeType) {
        case NSSPKCS5_PBKDF1:
            dummy = SEC_ASN1EncodeItem(arena, &der_param, pbe_param,
                                       NSSPKCS5PBEParameterTemplate);
            break;
        case NSSPKCS5_PKCS12_V2:
            dummy = SEC_ASN1EncodeItem(arena, &der_param, pbe_param,
                                       NSSPKCS5PKCS12V2PBEParameterTemplate);
            break;
        case NSSPKCS5_PBKDF2:
            if (pbe_param->keyLength.data == NULL) {
                dummy = SEC_ASN1EncodeInteger(pbe_param->poolp,
                                              &pbe_param->keyLength, pbe_param->keyLen);
                if (dummy == NULL) {
                    goto loser;
                }
            }
            PORT_Memset(&pkcs5v2_param, 0, sizeof(pkcs5v2_param));
            dummy = SEC_ASN1EncodeItem(arena, &der_param, pbe_param,
                                       NSSPKCS5V2PBEParameterTemplate);
            if (dummy == NULL) {
                break;
            }
            dummy = NULL;
            rv = SECOID_SetAlgorithmID(arena, &pkcs5v2_param.keyParams,
                                       SEC_OID_PKCS5_PBKDF2, &der_param);
            if (rv != SECSuccess) {
                break;
            }
            der_param.data = pbe_param->ivData;
            der_param.len = pbe_param->ivLen;
            rv = SECOID_SetAlgorithmID(arena, &pkcs5v2_param.algParams,
                                       pbe_param->encAlg, pbe_param->ivLen ? &der_param : NULL);
            if (rv != SECSuccess) {
                dummy = NULL;
                break;
            }
            der_param.data = NULL;
            der_param.len = 0;
            dummy = SEC_ASN1EncodeItem(arena, &der_param, &pkcs5v2_param,
                                       NSSPKCS5V2PBES2ParameterTemplate);
            /* If the algorithm was set to some encryption oid, set it
             * to PBES2 */
            if ((algorithm != SEC_OID_PKCS5_PBKDF2) &&
                (algorithm != SEC_OID_PKCS5_PBMAC1)) {
                algorithm = SEC_OID_PKCS5_PBES2;
            }
            break;
        default:
            break;
    }

    if (dummy == NULL) {
        goto loser;
    }

    rv = SECOID_SetAlgorithmID(arena, algid, algorithm, &der_param);
    if (rv != SECSuccess) {
        goto loser;
    }

    ret_algid = (SECAlgorithmID *)PORT_ZAlloc(sizeof(SECAlgorithmID));
    if (ret_algid == NULL) {
        goto loser;
    }

    rv = SECOID_CopyAlgorithmID(NULL, ret_algid, algid);
    if (rv != SECSuccess) {
        SECOID_DestroyAlgorithmID(ret_algid, PR_TRUE);
        ret_algid = NULL;
    }

loser:

    return ret_algid;
}

#define TEST_KEY "pbkdf test key"
SECStatus
sftk_fips_pbkdf_PowerUpSelfTests(void)
{
    SECItem *result;
    SECItem inKey;
    NSSPKCS5PBEParameter pbe_params;
    unsigned char iteration_count = 5;
    unsigned char keyLen = 64;
    char *inKeyData = TEST_KEY;
    static const unsigned char saltData[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };

    static const unsigned char pbkdf_known_answer[] = {
        0x73, 0x8c, 0xfa, 0x02, 0xe8, 0xdb, 0x43, 0xe4,
        0x99, 0xc5, 0xfd, 0xd9, 0x4d, 0x8e, 0x3e, 0x7b,
        0xc4, 0xda, 0x22, 0x1b, 0xe1, 0xae, 0x23, 0x7a,
        0x21, 0x27, 0xbd, 0xcc, 0x78, 0xc4, 0xe6, 0xc5,
        0x33, 0x38, 0x35, 0xe0, 0x68, 0x1a, 0x1e, 0x06,
        0xad, 0xaf, 0x7f, 0xd7, 0x3f, 0x0e, 0xc0, 0x90,
        0x17, 0x97, 0x73, 0x75, 0x7b, 0x88, 0x49, 0xd8,
        0x6f, 0x78, 0x5a, 0xde, 0x50, 0x20, 0x55, 0x33
    };

    sftk_PBELockInit();

    inKey.data = (unsigned char *)inKeyData;
    inKey.len = sizeof(TEST_KEY) - 1;

    pbe_params.salt.data = (unsigned char *)saltData;
    pbe_params.salt.len = sizeof(saltData);
    /* the interation and keyLength are used as intermediate
     * values when decoding the Algorithm ID, set them for completeness,
     * but they are not used */
    pbe_params.iteration.data = &iteration_count;
    pbe_params.iteration.len = 1;
    pbe_params.keyLength.data = &keyLen;
    pbe_params.keyLength.len = 1;
    /* pkcs5v2 stores the key in the AlgorithmID, so we don't need to
     * generate it here */
    pbe_params.ivLen = 0;
    pbe_params.ivData = NULL;
    /* keyID is only used by pkcs12 extensions to pkcs5v1 */
    pbe_params.keyID = pbeBitGenCipherKey;
    /* Algorithm is used by the decryption code after get get our key */
    pbe_params.encAlg = SEC_OID_AES_256_CBC;
    /* these are the fields actually used in nsspkcs5_ComputeKeyAndIV
     * for NSSPKCS5_PBKDF2 */
    pbe_params.iter = iteration_count;
    pbe_params.keyLen = keyLen;
    pbe_params.hashType = HASH_AlgSHA256;
    pbe_params.pbeType = NSSPKCS5_PBKDF2;
    pbe_params.is2KeyDES = PR_FALSE;

    result = nsspkcs5_ComputeKeyAndIV(&pbe_params, &inKey, NULL, PR_FALSE);
    if ((result == NULL) || (result->len != sizeof(pbkdf_known_answer)) ||
        (PORT_Memcmp(result->data, pbkdf_known_answer, sizeof(pbkdf_known_answer)) != 0)) {
        SECITEM_FreeItem(result, PR_TRUE);
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    SECITEM_FreeItem(result, PR_TRUE);
    return SECSuccess;
}
