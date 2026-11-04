/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "seccomon.h"
#include "secerr.h"
#include "blapi.h"
#include "pkcs11i.h"
#include "softoken.h"
#include "hmacct.h"

/* Wrappers to avoid undefined behavior calling functions through a pointer of incorrect type. */
static void
SFTKMAC_CMAC_Destroy(void *ctx, PRBool freeit)
{
    CMACContext *cctx = ctx;
    CMAC_Destroy(cctx, freeit);
}

static void
SFTKMAC_HMAC_Destroy(void *ctx, PRBool freeit)
{
    HMACContext *hctx = ctx;
    HMAC_Destroy(hctx, freeit);
}

/* sftk_HMACMechanismToHash converts a PKCS#11 MAC mechanism into a freebl hash
 * type. */
HASH_HashType
sftk_HMACMechanismToHash(CK_MECHANISM_TYPE mech)
{
    switch (mech) {
        case CKM_MD2_HMAC:
            return HASH_AlgMD2;
        case CKM_MD5_HMAC:
        case CKM_SSL3_MD5_MAC:
            return HASH_AlgMD5;
        case CKM_SHA_1_HMAC:
        case CKM_SSL3_SHA1_MAC:
            return HASH_AlgSHA1;
        case CKM_SHA224_HMAC:
            return HASH_AlgSHA224;
        case CKM_SHA256_HMAC:
            return HASH_AlgSHA256;
        case CKM_SHA384_HMAC:
            return HASH_AlgSHA384;
        case CKM_SHA512_HMAC:
            return HASH_AlgSHA512;
        case CKM_SHA3_224_HMAC:
            return HASH_AlgSHA3_224;
        case CKM_SHA3_256_HMAC:
            return HASH_AlgSHA3_256;
        case CKM_SHA3_384_HMAC:
            return HASH_AlgSHA3_384;
        case CKM_SHA3_512_HMAC:
            return HASH_AlgSHA3_512;
    }
    return HASH_AlgNULL;
}

static sftk_MACConstantTimeCtx *
SetupMAC(CK_MECHANISM_PTR mech, SFTKObject *key)
{
    CK_NSS_MAC_CONSTANT_TIME_PARAMS *params =
        (CK_NSS_MAC_CONSTANT_TIME_PARAMS *)mech->pParameter;
    sftk_MACConstantTimeCtx *ctx;
    HASH_HashType alg;
    SFTKAttribute *keyval;
    unsigned char secret[sizeof(ctx->secret)];
    unsigned int secretLength;

    if (mech->ulParameterLen != sizeof(CK_NSS_MAC_CONSTANT_TIME_PARAMS)) {
        return NULL;
    }

    alg = sftk_HMACMechanismToHash(params->macAlg);
    if (alg == HASH_AlgNULL) {
        return NULL;
    }

    keyval = sftk_FindAttribute(key, CKA_VALUE);
    if (keyval == NULL) {
        return NULL;
    }
    secretLength = keyval->attrib.ulValueLen;
    if (secretLength > sizeof(secret)) {
        sftk_FreeAttribute(keyval);
        return NULL;
    }
    memcpy(secret, keyval->attrib.pValue, secretLength);
    sftk_FreeAttribute(keyval);

    ctx = PORT_Alloc(sizeof(sftk_MACConstantTimeCtx));
    if (!ctx) {
        PORT_Memset(secret, 0, secretLength);
        return NULL;
    }

    memcpy(ctx->secret, secret, secretLength);
    ctx->secretLength = secretLength;
    ctx->hash = HASH_GetRawHashObject(alg);
    ctx->totalLength = params->ulBodyTotalLen;
    PORT_Memset(secret, 0, secretLength);

    return ctx;
}

sftk_MACConstantTimeCtx *
sftk_HMACConstantTime_New(CK_MECHANISM_PTR mech, SFTKObject *key)
{
    CK_NSS_MAC_CONSTANT_TIME_PARAMS *params =
        (CK_NSS_MAC_CONSTANT_TIME_PARAMS *)mech->pParameter;
    sftk_MACConstantTimeCtx *ctx;

    if (params->ulHeaderLen > sizeof(ctx->header)) {
        return NULL;
    }
    ctx = SetupMAC(mech, key);
    if (!ctx) {
        return NULL;
    }

    ctx->headerLength = params->ulHeaderLen;
    memcpy(ctx->header, params->pHeader, params->ulHeaderLen);
    return ctx;
}

sftk_MACConstantTimeCtx *
sftk_SSLv3MACConstantTime_New(CK_MECHANISM_PTR mech, SFTKObject *key)
{
    CK_NSS_MAC_CONSTANT_TIME_PARAMS *params =
        (CK_NSS_MAC_CONSTANT_TIME_PARAMS *)mech->pParameter;
    unsigned int padLength = 40, j;
    sftk_MACConstantTimeCtx *ctx;

    if (params->macAlg != CKM_SSL3_MD5_MAC &&
        params->macAlg != CKM_SSL3_SHA1_MAC) {
        return NULL;
    }
    ctx = SetupMAC(mech, key);
    if (!ctx) {
        return NULL;
    }

    if (params->macAlg == CKM_SSL3_MD5_MAC) {
        padLength = 48;
    }

    ctx->headerLength =
        ctx->secretLength +
        padLength +
        params->ulHeaderLen;

    if (ctx->headerLength > sizeof(ctx->header)) {
        goto loser;
    }

    j = 0;
    memcpy(&ctx->header[j], ctx->secret, ctx->secretLength);
    j += ctx->secretLength;
    memset(&ctx->header[j], 0x36, padLength);
    j += padLength;
    memcpy(&ctx->header[j], params->pHeader, params->ulHeaderLen);

    return ctx;

loser:
    PORT_Free(ctx);
    return NULL;
}

void
sftk_HMACConstantTime_Update(void *pctx, const unsigned char *data, unsigned int len)
{
    sftk_MACConstantTimeCtx *ctx = (sftk_MACConstantTimeCtx *)pctx;
    PORT_CheckSuccess(HMAC_ConstantTime(
        ctx->mac, NULL, sizeof(ctx->mac),
        ctx->hash,
        ctx->secret, ctx->secretLength,
        ctx->header, ctx->headerLength,
        data, len,
        ctx->totalLength));
}

void
sftk_SSLv3MACConstantTime_Update(void *pctx, const unsigned char *data, unsigned int len)
{
    sftk_MACConstantTimeCtx *ctx = (sftk_MACConstantTimeCtx *)pctx;
    PORT_CheckSuccess(SSLv3_MAC_ConstantTime(
        ctx->mac, NULL, sizeof(ctx->mac),
        ctx->hash,
        ctx->secret, ctx->secretLength,
        ctx->header, ctx->headerLength,
        data, len,
        ctx->totalLength));
}

void
sftk_MACConstantTime_EndHash(void *pctx, unsigned char *out, unsigned int *outLength,
                             unsigned int maxLength)
{
    const sftk_MACConstantTimeCtx *ctx = (sftk_MACConstantTimeCtx *)pctx;
    unsigned int toCopy = ctx->hash->length;
    if (toCopy > maxLength) {
        toCopy = maxLength;
    }
    memcpy(out, ctx->mac, toCopy);
    if (outLength) {
        *outLength = toCopy;
    }
}

void
sftk_MACConstantTime_DestroyContext(void *pctx, PRBool free)
{
    PORT_ZFree(pctx, sizeof(sftk_MACConstantTimeCtx));
}

CK_RV
sftk_MAC_Create(CK_MECHANISM_TYPE mech, SFTKObject *key, sftk_MACCtx **ret_ctx)
{
    CK_RV ret;

    if (ret_ctx == NULL || key == NULL) {
        return CKR_HOST_MEMORY;
    }

    *ret_ctx = PORT_New(sftk_MACCtx);
    if (*ret_ctx == NULL) {
        return CKR_HOST_MEMORY;
    }

    ret = sftk_MAC_Init(*ret_ctx, mech, key);
    if (ret != CKR_OK) {
        sftk_MAC_DestroyContext(*ret_ctx, PR_TRUE);
        *ret_ctx = NULL;
    }

    return ret;
}

CK_RV
sftk_MAC_Init(sftk_MACCtx *ctx, CK_MECHANISM_TYPE mech, SFTKObject *key)
{
    SFTKAttribute *keyval = NULL;
    PRBool isFIPS = sftk_isFIPS(key->slot->slotID);
    CK_RV ret = CKR_OK;

    /* Clear the context before use. */
    PORT_Memset(ctx, 0, sizeof(*ctx));

    /* Find the actual value of the key. */
    keyval = sftk_FindAttribute(key, CKA_VALUE);
    if (keyval == NULL) {
        ret = CKR_KEY_SIZE_RANGE;
        goto done;
    }

    ret = sftk_MAC_InitRaw(ctx, mech,
                           (const unsigned char *)keyval->attrib.pValue,
                           keyval->attrib.ulValueLen, isFIPS);

done:
    if (keyval) {
        sftk_FreeAttribute(keyval);
    }
    return ret;
}

CK_RV
sftk_MAC_InitRaw(sftk_MACCtx *ctx, CK_MECHANISM_TYPE mech, const unsigned char *key, unsigned int key_len, PRBool isFIPS)
{
    const SECHashObject *hashObj = NULL;
    CK_RV ret = CKR_OK;

    if (ctx == NULL) {
        return CKR_HOST_MEMORY;
    }

    /* Clear the context before use. */
    PORT_Memset(ctx, 0, sizeof(*ctx));

    /* Save the mech. */
    ctx->mech = mech;

    /* Initialize the correct MAC context. */
    switch (mech) {
        case CKM_MD2_HMAC:
        case CKM_MD5_HMAC:
        case CKM_SHA_1_HMAC:
        case CKM_SHA224_HMAC:
        case CKM_SHA256_HMAC:
        case CKM_SHA384_HMAC:
        case CKM_SHA512_HMAC:
        case CKM_SHA3_224_HMAC:
        case CKM_SHA3_256_HMAC:
        case CKM_SHA3_384_HMAC:
        case CKM_SHA3_512_HMAC:
            hashObj = HASH_GetRawHashObject(sftk_HMACMechanismToHash(mech));

            /* Because we condition above only on hashes we know to be valid,
             * hashObj should never be NULL. This assert is only useful when
             * adding a new hash function (for which only partial support has
             * been added); thus there is no need to turn it into an if and
             * avoid the NULL dereference on the following line. */
            PR_ASSERT(hashObj != NULL);
            ctx->mac_size = hashObj->length;

            goto hmac;
        case CKM_AES_CMAC:
            ctx->mac.cmac = CMAC_Create(CMAC_AES, key, key_len);
            ctx->destroy_func = SFTKMAC_CMAC_Destroy;

            /* Copy the behavior of sftk_doCMACInit here. */
            if (ctx->mac.cmac == NULL) {
                if (PORT_GetError() == SEC_ERROR_INVALID_ARGS) {
                    ret = CKR_KEY_SIZE_RANGE;
                    goto done;
                }

                ret = CKR_HOST_MEMORY;
                goto done;
            }

            ctx->mac_size = AES_BLOCK_SIZE;

            goto done;
        default:
            ret = CKR_MECHANISM_PARAM_INVALID;
            goto done;
    }

hmac:
    ctx->mac.hmac = HMAC_Create(hashObj, key, key_len, isFIPS);
    ctx->destroy_func = SFTKMAC_HMAC_Destroy;

    /* Copy the behavior of sftk_doHMACInit here. */
    if (ctx->mac.hmac == NULL) {
        if (PORT_GetError() == SEC_ERROR_INVALID_ARGS) {
            ret = CKR_KEY_SIZE_RANGE;
            goto done;
        }
        ret = CKR_HOST_MEMORY;
        goto done;
    }

    /* Semantics: HMAC and CMAC should behave the same. Begin HMAC now. */
    HMAC_Begin(ctx->mac.hmac);

done:
    /* Handle a failure: ctx->mac.raw should be NULL, but make sure
     * destroy_func isn't set. */
    if (ret != CKR_OK) {
        ctx->destroy_func = NULL;
    }

    return ret;
}

CK_RV
sftk_MAC_Reset(sftk_MACCtx *ctx)
{
    /* Useful for resetting the state of MAC prior to calling update again
     *
     * This lets the caller keep a single MAC instance and re-use it as long
     * as the key stays the same. */
    switch (ctx->mech) {
        case CKM_MD2_HMAC:
        case CKM_MD5_HMAC:
        case CKM_SHA_1_HMAC:
        case CKM_SHA224_HMAC:
        case CKM_SHA256_HMAC:
        case CKM_SHA384_HMAC:
        case CKM_SHA512_HMAC:
        case CKM_SHA3_224_HMAC:
        case CKM_SHA3_256_HMAC:
        case CKM_SHA3_384_HMAC:
        case CKM_SHA3_512_HMAC:
            HMAC_Begin(ctx->mac.hmac);
            break;
        case CKM_AES_CMAC:
            if (CMAC_Begin(ctx->mac.cmac) != SECSuccess) {
                return CKR_FUNCTION_FAILED;
            }
            break;
        default:
            /* This shouldn't happen -- asserting indicates partial support
             * for a new MAC type. */
            PR_ASSERT(PR_FALSE);
            return CKR_FUNCTION_FAILED;
    }

    return CKR_OK;
}

CK_RV
sftk_MAC_Update(sftk_MACCtx *ctx, const CK_BYTE *data, unsigned int data_len)
{
    switch (ctx->mech) {
        case CKM_MD2_HMAC:
        case CKM_MD5_HMAC:
        case CKM_SHA_1_HMAC:
        case CKM_SHA224_HMAC:
        case CKM_SHA256_HMAC:
        case CKM_SHA384_HMAC:
        case CKM_SHA512_HMAC:
        case CKM_SHA3_224_HMAC:
        case CKM_SHA3_256_HMAC:
        case CKM_SHA3_384_HMAC:
        case CKM_SHA3_512_HMAC:
            /* HMAC doesn't indicate failure in the return code. */
            HMAC_Update(ctx->mac.hmac, data, data_len);
            break;
        case CKM_AES_CMAC:
            /* CMAC indicates failure in the return code, however this is
             * unlikely to occur. */
            if (CMAC_Update(ctx->mac.cmac, data, data_len) != SECSuccess) {
                return CKR_FUNCTION_FAILED;
            }
            break;
        default:
            /* This shouldn't happen -- asserting indicates partial support
             * for a new MAC type. */
            PR_ASSERT(PR_FALSE);
            return CKR_FUNCTION_FAILED;
    }
    return CKR_OK;
}

CK_RV
sftk_MAC_End(sftk_MACCtx *ctx, CK_BYTE_PTR result, unsigned int *result_len, unsigned int max_result_len)
{
    unsigned int actual_result_len;

    switch (ctx->mech) {
        case CKM_MD2_HMAC:
        case CKM_MD5_HMAC:
        case CKM_SHA_1_HMAC:
        case CKM_SHA224_HMAC:
        case CKM_SHA256_HMAC:
        case CKM_SHA384_HMAC:
        case CKM_SHA512_HMAC:
        case CKM_SHA3_224_HMAC:
        case CKM_SHA3_256_HMAC:
        case CKM_SHA3_384_HMAC:
        case CKM_SHA3_512_HMAC:
            /* HMAC doesn't indicate failure in the return code. Additionally,
             * unlike CMAC, it doesn't support partial results. This means that we
             * need to allocate a buffer if max_result_len < ctx->mac_size. */
            if (max_result_len >= ctx->mac_size) {
                /* Split this into two calls to avoid an unnecessary stack
                 * allocation and memcpy when possible. */
                HMAC_Finish(ctx->mac.hmac, result, &actual_result_len, max_result_len);
            } else {
                uint8_t tmp_buffer[SFTK_MAX_MAC_LENGTH];

                /* Assumption: buffer is large enough to hold this HMAC's
                 * output. */
                PR_ASSERT(SFTK_MAX_MAC_LENGTH >= ctx->mac_size);

                HMAC_Finish(ctx->mac.hmac, tmp_buffer, &actual_result_len, SFTK_MAX_MAC_LENGTH);

                if (actual_result_len > max_result_len) {
                    /* This should always be true since:
                     *
                     *   (SFTK_MAX_MAC_LENGTH >= ctx->mac_size =
                     *       actual_result_len) > max_result_len,
                     *
                     * but guard this truncation just in case. */
                    actual_result_len = max_result_len;
                }

                PORT_Memcpy(result, tmp_buffer, actual_result_len);
            }
            break;
        case CKM_AES_CMAC:
            /* CMAC indicates failure in the return code, however this is
             * unlikely to occur. */
            if (CMAC_Finish(ctx->mac.cmac, result, &actual_result_len, max_result_len) != SECSuccess) {
                return CKR_FUNCTION_FAILED;
            }
            break;
        default:
            /* This shouldn't happen -- asserting indicates partial support
             * for a new MAC type. */
            PR_ASSERT(PR_FALSE);
            return CKR_FUNCTION_FAILED;
    }

    if (result_len) {
        /* When result length is passed, inform the caller of its value. */
        *result_len = actual_result_len;
    } else if (max_result_len == ctx->mac_size) {
        /* Validate that the amount requested was what was actually given; the
         * caller assumes that what they passed was the output size of the
         * underlying MAC and that they got all the bytes the asked for. */
        PR_ASSERT(actual_result_len == max_result_len);
    }

    return CKR_OK;
}

void
sftk_MAC_DestroyContext(sftk_MACCtx *ctx, PRBool free_it)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->mac.raw != NULL && ctx->destroy_func != NULL) {
        ctx->destroy_func(ctx->mac.raw, PR_TRUE);
    }

    /* Clean up the struct so we don't double free accidentally. */
    PORT_Memset(ctx, 0, sizeof(sftk_MACCtx));

    if (free_it == PR_TRUE) {
        PORT_Free(ctx);
    }
}
