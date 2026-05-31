/*
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef FREEBL_NO_DEPEND
#include "stubs.h"
#endif

#include "prerror.h"
#include "secerr.h"

#include "prtypes.h"
#include "blapi.h"
#include "secitem.h"
#include "blapit.h"
#include "secport.h"
#include "secrng.h"
#include "ml_dsat.h"

#include <string.h>

#include "dilithium-pqcrystals-ref.h"

/*
 * freebl currently ships only the ML-DSA-87 (Dilithium mode 5) parameter set,
 * provided by the amalgamated pq-crystals reference in
 * dilithium-pqcrystals-ref.c. The reference signs and verifies a complete
 * message in one call, so the streaming MLDSAContext below buffers the message
 * across Update calls and runs sign/verify in Final.
 */

/* Entropy source for the vendored reference (key generation and hedged
 * signatures). Backed by freebl's RNG. */
void
randombytes(uint8_t *out, size_t outlen)
{
    if (RNG_GenerateGlobalRandomBytes(out, outlen) != SECSuccess) {
        /* The reference API cannot report this; zero the buffer so we never
         * operate on uninitialized memory. The resulting key/signature will be
         * unusable, which is the safe failure mode. */
        PORT_Memset(out, 0, outlen);
    }
}

struct MLDSAContextStr {
    CK_ML_DSA_PARAMETER_SET_TYPE paramSet;
    PRBool isSign;
    CK_HEDGE_TYPE hedgeType; /* sign only */
    unsigned char *key;      /* packed secret key (sign) or public key (verify) */
    unsigned int keyLen;
    unsigned char *sgnCtx; /* signature context string (may be NULL) */
    unsigned int sgnCtxLen;
    unsigned char *msg; /* buffered message */
    unsigned int msgLen;
    unsigned int msgCap;
};

static PRBool
mldsa_supported(CK_ML_DSA_PARAMETER_SET_TYPE paramSet)
{
    return paramSet == CKP_ML_DSA_87;
}

/* Build the FIPS 204 pure-signature prefix pre = (0x00, ctxlen, ctx). */
static SECStatus
mldsa_build_pre(const unsigned char *ctx, unsigned int ctxLen,
                unsigned char pre[257], size_t *preLen)
{
    if (ctxLen > 255) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    pre[0] = 0;
    pre[1] = (unsigned char)ctxLen;
    if (ctxLen) {
        memcpy(pre + 2, ctx, ctxLen);
    }
    *preLen = 2 + ctxLen;
    return SECSuccess;
}

static MLDSAContext *
mldsa_context_new(CK_ML_DSA_PARAMETER_SET_TYPE paramSet, PRBool isSign,
                  const unsigned char *key, unsigned int keyLen,
                  const SECItem *sgnCtx)
{
    MLDSAContext *c = PORT_ZNew(MLDSAContext);
    if (!c) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return NULL;
    }
    c->paramSet = paramSet;
    c->isSign = isSign;
    c->key = (unsigned char *)PORT_Alloc(keyLen);
    if (!c->key) {
        PORT_ZFree(c, sizeof(*c));
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return NULL;
    }
    memcpy(c->key, key, keyLen);
    c->keyLen = keyLen;
    if (sgnCtx && sgnCtx->data && sgnCtx->len) {
        c->sgnCtx = (unsigned char *)PORT_Alloc(sgnCtx->len);
        if (!c->sgnCtx) {
            PORT_ZFree(c->key, keyLen);
            PORT_ZFree(c, sizeof(*c));
            PORT_SetError(SEC_ERROR_NO_MEMORY);
            return NULL;
        }
        memcpy(c->sgnCtx, sgnCtx->data, sgnCtx->len);
        c->sgnCtxLen = sgnCtx->len;
    }
    return c;
}

static void
mldsa_context_free(MLDSAContext *ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->key) {
        PORT_ZFree(ctx->key, ctx->keyLen);
    }
    if (ctx->sgnCtx) {
        PORT_ZFree(ctx->sgnCtx, ctx->sgnCtxLen);
    }
    if (ctx->msg) {
        PORT_ZFree(ctx->msg, ctx->msgCap);
    }
    PORT_ZFree(ctx, sizeof(*ctx));
}

static SECStatus
mldsa_buffer_append(MLDSAContext *ctx, const SECItem *data)
{
    if (!ctx || !data) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (data->len == 0) {
        return SECSuccess;
    }
    /* Guard against unsigned overflow of the running length. */
    if (ctx->msgLen + data->len < ctx->msgLen) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (ctx->msgLen + data->len > ctx->msgCap) {
        unsigned int newCap = ctx->msgCap ? ctx->msgCap : 1024;
        unsigned char *newMsg;
        while (newCap < ctx->msgLen + data->len) {
            newCap *= 2;
        }
        newMsg = (unsigned char *)PORT_Alloc(newCap);
        if (!newMsg) {
            PORT_SetError(SEC_ERROR_NO_MEMORY);
            return SECFailure;
        }
        if (ctx->msgLen) {
            memcpy(newMsg, ctx->msg, ctx->msgLen);
        }
        if (ctx->msg) {
            PORT_ZFree(ctx->msg, ctx->msgCap);
        }
        ctx->msg = newMsg;
        ctx->msgCap = newCap;
    }
    memcpy(ctx->msg + ctx->msgLen, data->data, data->len);
    ctx->msgLen += data->len;
    return SECSuccess;
}

SECStatus
MLDSA_NewKey(CK_ML_DSA_PARAMETER_SET_TYPE paramSet, SECItem *seed,
             MLDSAPrivateKey *privKey, MLDSAPublicKey *pubKey)
{
    uint8_t coins[DILITHIUM_SEEDBYTES];

    if (!privKey || !pubKey || !mldsa_supported(paramSet)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (seed && seed->data) {
        if (seed->len != DILITHIUM_SEEDBYTES) {
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            return SECFailure;
        }
        memcpy(coins, seed->data, DILITHIUM_SEEDBYTES);
    } else {
        randombytes(coins, DILITHIUM_SEEDBYTES);
    }

    if (pqcrystals_dilithium5_ref_keypair_internal(pubKey->keyVal,
                                                   privKey->keyVal,
                                                   coins) != 0) {
        PORT_Memset(coins, 0, sizeof(coins));
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    pubKey->paramSet = paramSet;
    pubKey->keyValLen = DILITHIUM5_PUBLICKEYBYTES;
    privKey->paramSet = paramSet;
    privKey->keyValLen = DILITHIUM5_SECRETKEYBYTES;
    /* Retain the seed so it can be returned in the private key. */
    memcpy(privKey->seed, coins, DILITHIUM_SEEDBYTES);
    privKey->seedLen = DILITHIUM_SEEDBYTES;
    PORT_Memset(coins, 0, sizeof(coins));
    return SECSuccess;
}

SECStatus
MLDSA_SignInit(MLDSAPrivateKey *key, CK_HEDGE_TYPE hedgeType,
               const SECItem *sgnCtx, MLDSAContext **ctx)
{
    MLDSAContext *c;

    if (!key || !ctx || !mldsa_supported(key->paramSet)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    c = mldsa_context_new(key->paramSet, PR_TRUE, key->keyVal, key->keyValLen,
                          sgnCtx);
    if (!c) {
        return SECFailure;
    }
    c->hedgeType = hedgeType;
    *ctx = c;
    return SECSuccess;
}

SECStatus
MLDSA_SignUpdate(MLDSAContext *ctx, const SECItem *data)
{
    if (!ctx || !ctx->isSign) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    return mldsa_buffer_append(ctx, data);
}

SECStatus
MLDSA_SignFinal(MLDSAContext *ctx, SECItem *signature)
{
    unsigned char pre[257];
    size_t preLen;
    uint8_t rnd[DILITHIUM_RNDBYTES];
    size_t sigLen = 0;
    int rv;

    if (!ctx || !ctx->isSign || !signature || !signature->data) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        if (ctx) {
            mldsa_context_free(ctx);
        }
        return SECFailure;
    }
    if (signature->len < DILITHIUM5_SIGNATUREBYTES) {
        PORT_SetError(SEC_ERROR_OUTPUT_LEN);
        mldsa_context_free(ctx);
        return SECFailure;
    }
    if (mldsa_build_pre(ctx->sgnCtx, ctx->sgnCtxLen, pre, &preLen) !=
        SECSuccess) {
        mldsa_context_free(ctx);
        return SECFailure;
    }

    if (ctx->hedgeType == CKH_DETERMINISTIC_REQUIRED) {
        PORT_Memset(rnd, 0, sizeof(rnd));
    } else {
        randombytes(rnd, sizeof(rnd));
    }

    rv = pqcrystals_dilithium5_ref_signature_internal(
        signature->data, &sigLen, ctx->msg, ctx->msgLen, pre, preLen, rnd,
        ctx->key);
    PORT_Memset(rnd, 0, sizeof(rnd));
    mldsa_context_free(ctx);
    if (rv != 0) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    signature->len = (unsigned int)sigLen;
    return SECSuccess;
}

SECStatus
MLDSA_VerifyInit(MLDSAPublicKey *key, const SECItem *sgnCtx, MLDSAContext **ctx)
{
    MLDSAContext *c;

    if (!key || !ctx || !mldsa_supported(key->paramSet)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    c = mldsa_context_new(key->paramSet, PR_FALSE, key->keyVal, key->keyValLen,
                          sgnCtx);
    if (!c) {
        return SECFailure;
    }
    *ctx = c;
    return SECSuccess;
}

SECStatus
MLDSA_VerifyUpdate(MLDSAContext *ctx, const SECItem *data)
{
    if (!ctx || ctx->isSign) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    return mldsa_buffer_append(ctx, data);
}

SECStatus
MLDSA_VerifyFinal(MLDSAContext *ctx, const SECItem *signature)
{
    unsigned char pre[257];
    size_t preLen;
    int rv;

    if (!ctx || ctx->isSign || !signature || !signature->data) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        if (ctx) {
            mldsa_context_free(ctx);
        }
        return SECFailure;
    }
    if (mldsa_build_pre(ctx->sgnCtx, ctx->sgnCtxLen, pre, &preLen) !=
        SECSuccess) {
        mldsa_context_free(ctx);
        return SECFailure;
    }

    rv = pqcrystals_dilithium5_ref_verify_internal(
        signature->data, signature->len, ctx->msg, ctx->msgLen, pre, preLen,
        ctx->key);
    mldsa_context_free(ctx);
    if (rv != 0) {
        PORT_SetError(SEC_ERROR_BAD_SIGNATURE);
        return SECFailure;
    }
    return SECSuccess;
}
