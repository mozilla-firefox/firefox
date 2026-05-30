/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * TLS 1.3 Protocol
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "sslimpl.h"
#include "cryptohi.h"
#include "keyhi.h"

/* we put this here because it only affects TLS 1.3, and not TLS 1.2 and earlier
 * which use the old sign hashes interface. The TLS 1.3 protocol is friendly to
 * algorthims that don't have a signed hashes interface */
/* we generate an algorithm ID rather than just use an OID to support RSAPSS.
 * It's generated completely from the scheme */
static SECAlgorithmID *
tls_GetSignatureAlgorithmId(PLArenaPool *arena, SSLSignatureScheme scheme,
                            SECKEYPrivateKey *privKey, SECKEYPublicKey *pubKey)
{
    SECAlgorithmID *newAlgID = PORT_ArenaZNew(arena, SECAlgorithmID);
    SECOidTag algTag = SEC_OID_UNKNOWN;
    SECOidTag hashAlgTag = SEC_OID_UNKNOWN;
    SECStatus rv;

    switch (scheme) {
        /* For the algTag the difference between rsa_pss_rsae and
         * rsa_pss_pss is in the selection of the cert.
         * At this stage, the signatures are the same, so for our
         * purposed they are equivalent */
        case ssl_sig_rsa_pss_rsae_sha256:
        case ssl_sig_rsa_pss_pss_sha256:
            algTag = SEC_OID_PKCS1_RSA_PSS_SIGNATURE;
            hashAlgTag = SEC_OID_SHA256;
            break;
        case ssl_sig_rsa_pss_rsae_sha384:
        case ssl_sig_rsa_pss_pss_sha384:
            algTag = SEC_OID_PKCS1_RSA_PSS_SIGNATURE;
            hashAlgTag = SEC_OID_SHA384;
            break;
        case ssl_sig_rsa_pss_rsae_sha512:
        case ssl_sig_rsa_pss_pss_sha512:
            algTag = SEC_OID_PKCS1_RSA_PSS_SIGNATURE;
            hashAlgTag = SEC_OID_SHA512;
            break;
        /* the curve comes from the key and should have already been
         * enforced at a different level */
        case ssl_sig_ecdsa_secp256r1_sha256:
            algTag = SEC_OID_ANSIX962_ECDSA_SHA256_SIGNATURE;
            hashAlgTag = SEC_OID_SHA256;
            break;
        case ssl_sig_ecdsa_secp384r1_sha384:
            algTag = SEC_OID_ANSIX962_ECDSA_SHA384_SIGNATURE;
            hashAlgTag = SEC_OID_SHA384;
            break;
        case ssl_sig_ecdsa_secp521r1_sha512:
            algTag = SEC_OID_ANSIX962_ECDSA_SHA512_SIGNATURE;
            hashAlgTag = SEC_OID_SHA512;
            break;

        /* ML-DSA-87 (NIST FIPS 204, draft-ietf-tls-mldsa 0x0906).
         * Pure signature — no pre-hash. hashAlgTag must be the paramset OID,
         * not SEC_OID_UNKNOWN, because secsign.c rejects UNKNOWN for mldsaKey. */
        case ssl_sig_mldsa_87:
            algTag = SEC_OID_ML_DSA_87;
            hashAlgTag = SEC_OID_ML_DSA_87;
            break;

        /* the following is unsupported in tls 1.3 and greater, just break.
         * We include them here explicitly so we get the compiler warning about
         * missing enums in the switch statement. default would be a break anyway.
         * That way we'll know to update this table when new algorithms are
         * added */

        /* as of now edward curve signatures are not supported in NSS. That
         * could change, and this is part the code that would pick up the
         * change (need OIDS for the hash variants of these signature, and
         * then add them here) */
        case ssl_sig_ed25519:
        case ssl_sig_ed448:

        /* sha1 hashes in sigs are explicitly disallowed in TLS 1.3 or greater */
        case ssl_sig_ecdsa_sha1:

        /* rsa pkcs1 sigs are explicitly disallowed in TLS 1.3 and greater */
        case ssl_sig_rsa_pkcs1_sha1:
        case ssl_sig_rsa_pkcs1_sha256:
        case ssl_sig_rsa_pkcs1_sha384:
        case ssl_sig_rsa_pkcs1_sha512:

        /* dsa sigs are explicitly disallowed in TLS 1.3 and greater */
        case ssl_sig_dsa_sha1:
        case ssl_sig_dsa_sha256:
        case ssl_sig_dsa_sha384:
        case ssl_sig_dsa_sha512:

        /* special sig variants that aren't supported in TLS 1.3 or greater */
        case ssl_sig_rsa_pkcs1_sha1md5:
        case ssl_sig_none:
            break;
    }

    /* the earlier code should have made sure none of the unsupported
     * algorithms were accepted */
    PORT_Assert(algTag != SEC_OID_UNKNOWN);
    if (algTag == SEC_OID_UNKNOWN) {
        PORT_SetError(SEC_ERROR_INVALID_ALGORITHM);
        return NULL;
    }

    /* now get the algorithm ID algTag will override whatever is normally
     * selected from the key */
    rv = SEC_CreateSignatureAlgorithmID(arena, newAlgID, algTag, hashAlgTag,
                                        NULL, privKey, pubKey);
    if (rv != SECSuccess) {
        return NULL;
    }
    return newAlgID;
}

tlsSignOrVerifyContext
tls_CreateSignOrVerifyContext(SECKEYPrivateKey *privKey,
                              SECKEYPublicKey *pubKey,
                              SSLSignatureScheme scheme, sslSignOrVerify type,
                              SECItem *signature, void *pwArg)
{
    PLArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    tlsSignOrVerifyContext newCtx = { type, { NULL } };
    SECStatus rv;

    if (!arena) {
        goto loser;
    }

    if (type == sig_sign) {
        PORT_Assert(privKey);
    } else {
        PORT_Assert(pubKey);
    }

    /* we use sigAlgID here because it automatically formats parameters
     * for PSS. */
    SECAlgorithmID *sigAlgID = tls_GetSignatureAlgorithmId(arena, scheme,
                                                           privKey, pubKey);
    if (sigAlgID == NULL) {
        goto loser;
    }
    if (type == sig_sign) {
        newCtx.u.sig = SGN_NewContextWithAlgorithmID(sigAlgID, privKey);
        if (!newCtx.u.sig) {
            goto loser;
        }
        rv = SGN_Begin(newCtx.u.sig);
    } else {
        newCtx.u.vfy = VFY_CreateContextWithAlgorithmID(pubKey, signature,
                                                        sigAlgID, NULL, pwArg);
        if (!newCtx.u.vfy) {
            goto loser;
        }
        rv = VFY_Begin(newCtx.u.vfy);
    }
    if (rv != SECSuccess) {
        goto loser;
    }
    PORT_FreeArena(arena, PR_FALSE);
    return newCtx;

loser:
    tls_DestroySignOrVerifyContext(&newCtx);
    if (arena) {
        PORT_FreeArena(arena, PR_FALSE);
    }
    return newCtx; /* pointer already set to NULL by destroy */
}

SECStatus
tls_SignOrVerifyUpdate(tlsSignOrVerifyContext ctx, const unsigned char *buf,
                       int len)
{
    SECStatus rv;
    if (ctx.type == sig_sign) {
        rv = SGN_Update(ctx.u.sig, buf, len);
    } else {
        rv = VFY_Update(ctx.u.vfy, buf, len);
    }
    return rv;
}

SECStatus
tls_SignOrVerifyEnd(tlsSignOrVerifyContext ctx, SECItem *sig)
{
    SECStatus rv;
    if (ctx.type == sig_sign) {
        rv = SGN_End(ctx.u.sig, sig);
    } else {
        /* sig was already set in the context VFY_CreateContext */
        rv = VFY_End(ctx.u.vfy);
    }
    /* destroy the context on success */
    if (rv == SECSuccess) {
        tls_DestroySignOrVerifyContext(&ctx);
    }
    return rv;
}

/* Take ctx by pointer so we can NULL the freed pointer in the caller's
 * struct; tls_CreateSignOrVerifyContext relies on this to return a
 * NULL-ptr context on failure rather than a dangling one. */
void
tls_DestroySignOrVerifyContext(tlsSignOrVerifyContext *ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->type == sig_sign) {
        if (ctx->u.sig) {
            SGN_DestroyContext(ctx->u.sig, PR_TRUE);
        }
    } else {
        if (ctx->u.vfy) {
            VFY_DestroyContext(ctx->u.vfy, PR_TRUE);
        }
    }
    ctx->u.ptr = NULL;
}
