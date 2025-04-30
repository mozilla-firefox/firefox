/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * vtables (and methods that call through them) for the 4 types of
 * SSLSockets supported.  Only one type is still supported.
 * Various other functions.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "seccomon.h"
#include "cert.h"
#include "keyhi.h"
#include "ssl.h"
#include "sslexp.h"
#include "sslimpl.h"
#include "sslproto.h"
#include "nspr.h"
#include "private/pprio.h"
#include "nss.h"
#include "pk11pqg.h"
#include "pk11pub.h"
#include "tls13ech.h"
#include "tls13psk.h"
#include "tls13subcerts.h"

static const sslSocketOps ssl_default_ops = { /* No SSL. */
                                              ssl_DefConnect,
                                              NULL,
                                              ssl_DefBind,
                                              ssl_DefListen,
                                              ssl_DefShutdown,
                                              ssl_DefClose,
                                              ssl_DefRecv,
                                              ssl_DefSend,
                                              ssl_DefRead,
                                              ssl_DefWrite,
                                              ssl_DefGetpeername,
                                              ssl_DefGetsockname
};

static const sslSocketOps ssl_secure_ops = { /* SSL. */
                                             ssl_SecureConnect,
                                             NULL,
                                             ssl_DefBind,
                                             ssl_DefListen,
                                             ssl_SecureShutdown,
                                             ssl_SecureClose,
                                             ssl_SecureRecv,
                                             ssl_SecureSend,
                                             ssl_SecureRead,
                                             ssl_SecureWrite,
                                             ssl_DefGetpeername,
                                             ssl_DefGetsockname
};

/*
** default settings for socket enables
*/
static sslOptions ssl_defaults = {
    .nextProtoNego = { siBuffer, NULL, 0 },
    .maxEarlyDataSize = 1 << 16,
    .recordSizeLimit = MAX_FRAGMENT_LENGTH + 1,
    .useSecurity = PR_TRUE,
    .useSocks = PR_FALSE,
    .requestCertificate = PR_FALSE,
    .requireCertificate = SSL_REQUIRE_FIRST_HANDSHAKE,
    .handshakeAsClient = PR_FALSE,
    .handshakeAsServer = PR_FALSE,
    .noCache = PR_FALSE,
    .fdx = PR_FALSE,
    .detectRollBack = PR_TRUE,
    .noLocks = PR_FALSE,
    .enableSessionTickets = PR_FALSE,
    .enableDeflate = PR_FALSE,
    .enableRenegotiation = SSL_RENEGOTIATE_REQUIRES_XTN,
    .requireSafeNegotiation = PR_FALSE,
    .enableFalseStart = PR_FALSE,
    .cbcRandomIV = PR_TRUE,
    .enableOCSPStapling = PR_FALSE,
    .enableDelegatedCredentials = PR_FALSE,
    .enableALPN = PR_TRUE,
    .reuseServerECDHEKey = PR_FALSE,
    .enableFallbackSCSV = PR_FALSE,
    .enableServerDhe = PR_TRUE,
    .enableExtendedMS = PR_TRUE,
    .enableSignedCertTimestamps = PR_FALSE,
    .requireDHENamedGroups = PR_FALSE,
    .enable0RttData = PR_FALSE,
    .enableTls13CompatMode = PR_FALSE,
    .enableDtls13VersionCompat = PR_FALSE,
    .enableDtlsShortHeader = PR_FALSE,
    .enableHelloDowngradeCheck = PR_TRUE,
    .enableV2CompatibleHello = PR_FALSE,
    .enablePostHandshakeAuth = PR_FALSE,
    .suppressEndOfEarlyData = PR_FALSE,
    .enableTls13GreaseEch = PR_FALSE,
    .enableTls13BackendEch = PR_FALSE,
    .callExtensionWriterOnEchInner = PR_FALSE,
    .enableGrease = PR_FALSE,
    .enableChXtnPermutation = PR_FALSE
};

/*
 * default range of enabled SSL/TLS protocols
 */
static SSLVersionRange versions_defaults_stream = {
    SSL_LIBRARY_VERSION_TLS_1_2,
    SSL_LIBRARY_VERSION_TLS_1_3
};

static SSLVersionRange versions_defaults_datagram = {
    SSL_LIBRARY_VERSION_TLS_1_2,
    SSL_LIBRARY_VERSION_TLS_1_2
};

#define VERSIONS_DEFAULTS(variant) \
    (variant == ssl_variant_stream ? &versions_defaults_stream : &versions_defaults_datagram)
#define VERSIONS_POLICY_MIN(variant) \
    (variant == ssl_variant_stream ? NSS_TLS_VERSION_MIN_POLICY : NSS_DTLS_VERSION_MIN_POLICY)
#define VERSIONS_POLICY_MAX(variant) \
    (variant == ssl_variant_stream ? NSS_TLS_VERSION_MAX_POLICY : NSS_DTLS_VERSION_MAX_POLICY)

sslSessionIDLookupFunc ssl_sid_lookup;

static PRDescIdentity ssl_layer_id;

static PRCallOnceType ssl_setDefaultsFromEnvironment = { 0 };

PRBool ssl_force_locks;   /* implicitly PR_FALSE */
int ssl_lock_readers = 1; /* default true. */
char ssl_debug;
char ssl_trace;
FILE *ssl_trace_iob;

#ifdef NSS_ALLOW_SSLKEYLOGFILE
FILE *ssl_keylog_iob;
PZLock *ssl_keylog_lock;
#endif

/* SRTP_NULL_HMAC_SHA1_80 and SRTP_NULL_HMAC_SHA1_32 are not implemented. */
static const PRUint16 srtpCiphers[] = {
    SRTP_AES128_CM_HMAC_SHA1_80,
    SRTP_AES128_CM_HMAC_SHA1_32,
    0
};

/* This list is in preference order.  Note that while some smaller groups appear
 * early in the list, smaller groups are generally ignored when iterating
 * through this list. ffdhe_custom must not appear in this list. */
#define ECGROUP(name, size, oid, assumeSupported)  \
    {                                              \
        ssl_grp_ec_##name, size, ssl_kea_ecdh,     \
            SEC_OID_SECG_EC_##oid, assumeSupported \
    }
#define FFGROUP(size)                           \
    {                                           \
        ssl_grp_ffdhe_##size, size, ssl_kea_dh, \
            SEC_OID_TLS_FFDHE_##size, PR_TRUE   \
    }

const sslNamedGroupDef ssl_named_groups[] = {
    /* Note that 256 for 25519 is a lie, but we only use it for checking bit
     * security and expect 256 bits there (not 255). */
    { ssl_grp_ec_curve25519, 256, ssl_kea_ecdh, SEC_OID_CURVE25519, PR_TRUE },
    ECGROUP(secp256r1, 256, SECP256R1, PR_TRUE),
    ECGROUP(secp384r1, 384, SECP384R1, PR_TRUE),
    ECGROUP(secp521r1, 521, SECP521R1, PR_TRUE),
    { ssl_grp_kem_xyber768d00, 256, ssl_kea_ecdh_hybrid, SEC_OID_XYBER768D00, PR_TRUE },
    { ssl_grp_kem_mlkem768x25519, 256, ssl_kea_ecdh_hybrid, SEC_OID_MLKEM768X25519, PR_TRUE },
    FFGROUP(2048),
    FFGROUP(3072),
    FFGROUP(4096),
    FFGROUP(6144),
    FFGROUP(8192),
    ECGROUP(secp192r1, 192, SECP192R1, PR_FALSE),
    ECGROUP(secp160r2, 160, SECP160R2, PR_FALSE),
    ECGROUP(secp160k1, 160, SECP160K1, PR_FALSE),
    ECGROUP(secp160r1, 160, SECP160R1, PR_FALSE),
    ECGROUP(sect163k1, 163, SECT163K1, PR_FALSE),
    ECGROUP(sect163r1, 163, SECT163R1, PR_FALSE),
    ECGROUP(sect163r2, 163, SECT163R2, PR_FALSE),
    ECGROUP(secp192k1, 192, SECP192K1, PR_FALSE),
    ECGROUP(sect193r1, 193, SECT193R1, PR_FALSE),
    ECGROUP(sect193r2, 193, SECT193R2, PR_FALSE),
    ECGROUP(secp224r1, 224, SECP224R1, PR_FALSE),
    ECGROUP(secp224k1, 224, SECP224K1, PR_FALSE),
    ECGROUP(sect233k1, 233, SECT233K1, PR_FALSE),
    ECGROUP(sect233r1, 233, SECT233R1, PR_FALSE),
    ECGROUP(sect239k1, 239, SECT239K1, PR_FALSE),
    ECGROUP(secp256k1, 256, SECP256K1, PR_FALSE),
    ECGROUP(sect283k1, 283, SECT283K1, PR_FALSE),
    ECGROUP(sect283r1, 283, SECT283R1, PR_FALSE),
    ECGROUP(sect409k1, 409, SECT409K1, PR_FALSE),
    ECGROUP(sect409r1, 409, SECT409R1, PR_FALSE),
    ECGROUP(sect571k1, 571, SECT571K1, PR_FALSE),
    ECGROUP(sect571r1, 571, SECT571R1, PR_FALSE),
};
PR_STATIC_ASSERT(SSL_NAMED_GROUP_COUNT == PR_ARRAY_SIZE(ssl_named_groups));

#undef ECGROUP
#undef FFGROUP

/* forward declarations. */
static sslSocket *ssl_NewSocket(PRBool makeLocks, SSLProtocolVariant variant);
static SECStatus ssl_MakeLocks(sslSocket *ss);
static PRStatus ssl_SetDefaultsFromEnvironmentCallOnce(void);
static void ssl_SetDefaultsFromEnvironment(void);
static PRStatus ssl_PushIOLayer(sslSocket *ns, PRFileDesc *stack,
                                PRDescIdentity id);

/************************************************************************/

/*
** Lookup a socket structure from a file descriptor.
** Only functions called through the PRIOMethods table should use this.
** Other app-callable functions should use ssl_FindSocket.
*/
static sslSocket *
ssl_GetPrivate(PRFileDesc *fd)
{
    sslSocket *ss;

    PORT_Assert(fd != NULL);
    PORT_Assert(fd->methods->file_type == PR_DESC_LAYERED);
    PORT_Assert(fd->identity == ssl_layer_id);

    if (fd->methods->file_type != PR_DESC_LAYERED ||
        fd->identity != ssl_layer_id) {
        PORT_SetError(PR_BAD_DESCRIPTOR_ERROR);
        return NULL;
    }

    ss = (sslSocket *)fd->secret;
    /* Set ss->fd lazily. We can't rely on the value of ss->fd set by
     * ssl_PushIOLayer because another PR_PushIOLayer call will switch the
     * contents of the PRFileDesc pointed by ss->fd and the new layer.
     * See bug 807250.
     */
    ss->fd = fd;
    return ss;
}

/* This function tries to find the SSL layer in the stack.
 * It searches for the first SSL layer at or below the argument fd,
 * and failing that, it searches for the nearest SSL layer above the
 * argument fd.  It returns the private sslSocket from the found layer.
 */
sslSocket *
ssl_FindSocket(PRFileDesc *fd)
{
    PRFileDesc *layer;
    sslSocket *ss;

    PORT_Assert(fd != NULL);
    PORT_Assert(ssl_layer_id != 0);

    layer = PR_GetIdentitiesLayer(fd, ssl_layer_id);
    if (layer == NULL) {
        PORT_SetError(PR_BAD_DESCRIPTOR_ERROR);
        return NULL;
    }

    ss = (sslSocket *)layer->secret;
    /* Set ss->fd lazily. We can't rely on the value of ss->fd set by
     * ssl_PushIOLayer because another PR_PushIOLayer call will switch the
     * contents of the PRFileDesc pointed by ss->fd and the new layer.
     * See bug 807250.
     */
    ss->fd = layer;
    return ss;
}

static sslSocket *
ssl_DupSocket(sslSocket *os)
{
    sslSocket *ss;
    SECStatus rv;

    ss = ssl_NewSocket((PRBool)(!os->opt.noLocks), os->protocolVariant);
    if (!ss) {
        return NULL;
    }

    ss->opt = os->opt;
    ss->opt.useSocks = PR_FALSE;
    rv = SECITEM_CopyItem(NULL, &ss->opt.nextProtoNego, &os->opt.nextProtoNego);
    if (rv != SECSuccess) {
        goto loser;
    }
    ss->vrange = os->vrange;
    ss->now = os->now;
    ss->nowArg = os->nowArg;

    ss->peerID = !os->peerID ? NULL : PORT_Strdup(os->peerID);
    ss->url = !os->url ? NULL : PORT_Strdup(os->url);

    ss->ops = os->ops;
    ss->rTimeout = os->rTimeout;
    ss->wTimeout = os->wTimeout;
    ss->cTimeout = os->cTimeout;
    ss->dbHandle = os->dbHandle;

    /* copy ssl2&3 policy & prefs, even if it's not selected (yet) */
    PORT_Memcpy(ss->cipherSuites, os->cipherSuites, sizeof os->cipherSuites);
    PORT_Memcpy(ss->ssl3.dtlsSRTPCiphers, os->ssl3.dtlsSRTPCiphers,
                sizeof(PRUint16) * os->ssl3.dtlsSRTPCipherCount);
    ss->ssl3.dtlsSRTPCipherCount = os->ssl3.dtlsSRTPCipherCount;
    PORT_Memcpy(ss->ssl3.signatureSchemes, os->ssl3.signatureSchemes,
                sizeof(ss->ssl3.signatureSchemes[0]) *
                    os->ssl3.signatureSchemeCount);
    ss->ssl3.signatureSchemeCount = os->ssl3.signatureSchemeCount;
    ss->ssl3.downgradeCheckVersion = os->ssl3.downgradeCheckVersion;

    ss->ssl3.dheWeakGroupEnabled = os->ssl3.dheWeakGroupEnabled;

    PORT_Memcpy(ss->ssl3.supportedCertCompressionAlgorithms,
                os->ssl3.supportedCertCompressionAlgorithms,
                sizeof(ss->ssl3.supportedCertCompressionAlgorithms[0]) *
                    os->ssl3.supportedCertCompressionAlgorithmsCount);
    ss->ssl3.supportedCertCompressionAlgorithmsCount =
        os->ssl3.supportedCertCompressionAlgorithmsCount;

    if (ss->opt.useSecurity) {
        PRCList *cursor;

        for (cursor = PR_NEXT_LINK(&os->serverCerts);
             cursor != &os->serverCerts;
             cursor = PR_NEXT_LINK(cursor)) {
            sslServerCert *sc = ssl_CopyServerCert((sslServerCert *)cursor);
            if (!sc)
                goto loser;
            PR_APPEND_LINK(&sc->link, &ss->serverCerts);
        }

        for (cursor = PR_NEXT_LINK(&os->ephemeralKeyPairs);
             cursor != &os->ephemeralKeyPairs;
             cursor = PR_NEXT_LINK(cursor)) {
            sslEphemeralKeyPair *okp = (sslEphemeralKeyPair *)cursor;
            sslEphemeralKeyPair *skp = ssl_CopyEphemeralKeyPair(okp);
            if (!skp)
                goto loser;
            PR_APPEND_LINK(&skp->link, &ss->ephemeralKeyPairs);
        }

        for (cursor = PR_NEXT_LINK(&os->extensionHooks);
             cursor != &os->extensionHooks;
             cursor = PR_NEXT_LINK(cursor)) {
            sslCustomExtensionHooks *oh = (sslCustomExtensionHooks *)cursor;
            sslCustomExtensionHooks *sh = PORT_ZNew(sslCustomExtensionHooks);
            if (!sh) {
                goto loser;
            }
            *sh = *oh;
            PR_APPEND_LINK(&sh->link, &ss->extensionHooks);
        }

        /*
         * XXX the preceding CERT_ and SECKEY_ functions can fail and return NULL.
         * XXX We should detect this, and not just march on with NULL pointers.
         */
        ss->authCertificate = os->authCertificate;
        ss->authCertificateArg = os->authCertificateArg;
        ss->getClientAuthData = os->getClientAuthData;
        ss->getClientAuthDataArg = os->getClientAuthDataArg;
        ss->sniSocketConfig = os->sniSocketConfig;
        ss->sniSocketConfigArg = os->sniSocketConfigArg;
        ss->alertReceivedCallback = os->alertReceivedCallback;
        ss->alertReceivedCallbackArg = os->alertReceivedCallbackArg;
        ss->alertSentCallback = os->alertSentCallback;
        ss->alertSentCallbackArg = os->alertSentCallbackArg;
        ss->handleBadCert = os->handleBadCert;
        ss->badCertArg = os->badCertArg;
        ss->handshakeCallback = os->handshakeCallback;
        ss->handshakeCallbackData = os->handshakeCallbackData;
        ss->canFalseStartCallback = os->canFalseStartCallback;
        ss->canFalseStartCallbackData = os->canFalseStartCallbackData;
        ss->pkcs11PinArg = os->pkcs11PinArg;
        ss->nextProtoCallback = os->nextProtoCallback;
        ss->nextProtoArg = os->nextProtoArg;
        PORT_Memcpy((void *)ss->namedGroupPreferences,
                    os->namedGroupPreferences,
                    sizeof(ss->namedGroupPreferences));
        ss->additionalShares = os->additionalShares;
        ss->resumptionTokenCallback = os->resumptionTokenCallback;
        ss->resumptionTokenContext = os->resumptionTokenContext;

        rv = tls13_CopyEchConfigs(&os->echConfigs, &ss->echConfigs);
        if (rv != SECSuccess) {
            goto loser;
        }
        if (os->echPrivKey && os->echPubKey) {
            ss->echPrivKey = SECKEY_CopyPrivateKey(os->echPrivKey);
            ss->echPubKey = SECKEY_CopyPublicKey(os->echPubKey);
            if (!ss->echPrivKey || !ss->echPubKey) {
                goto loser;
            }
        }

        if (os->antiReplay) {
            ss->antiReplay = tls13_RefAntiReplayContext(os->antiReplay);
            PORT_Assert(ss->antiReplay); /* Can't fail. */
            if (!ss->antiReplay) {
                goto loser;
            }
        }
        if (os->psk) {
            ss->psk = tls13_CopyPsk(os->psk);
            if (!ss->psk) {
                goto loser;
            }
        }

        /* Create security data */
        rv = ssl_CopySecurityInfo(ss, os);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    return ss;

loser:
    ssl_FreeSocket(ss);
    return NULL;
}

static void
ssl_DestroyLocks(sslSocket *ss)
{
    /* Destroy locks. */
    if (ss->firstHandshakeLock) {
        PZ_DestroyMonitor(ss->firstHandshakeLock);
        ss->firstHandshakeLock = NULL;
    }
    if (ss->ssl3HandshakeLock) {
        PZ_DestroyMonitor(ss->ssl3HandshakeLock);
        ss->ssl3HandshakeLock = NULL;
    }
    if (ss->specLock) {
        NSSRWLock_Destroy(ss->specLock);
        ss->specLock = NULL;
    }

    if (ss->recvLock) {
        PZ_DestroyLock(ss->recvLock);
        ss->recvLock = NULL;
    }
    if (ss->sendLock) {
        PZ_DestroyLock(ss->sendLock);
        ss->sendLock = NULL;
    }
    if (ss->xmitBufLock) {
        PZ_DestroyMonitor(ss->xmitBufLock);
        ss->xmitBufLock = NULL;
    }
    if (ss->recvBufLock) {
        PZ_DestroyMonitor(ss->recvBufLock);
        ss->recvBufLock = NULL;
    }
}

/* Caller holds any relevant locks */
static void
ssl_DestroySocketContents(sslSocket *ss)
{
    PRCList *cursor;

    /* Free up socket */
    ssl_DestroySecurityInfo(&ss->sec);

    ssl3_DestroySSL3Info(ss);

    PORT_Free(ss->saveBuf.buf);
    PORT_Free(ss->pendingBuf.buf);
    ssl3_DestroyGather(&ss->gs);

    if (ss->peerID != NULL)
        PORT_Free(ss->peerID);
    if (ss->url != NULL)
        PORT_Free((void *)ss->url); /* CONST */

    /* Clean up server certificates and sundries. */
    while (!PR_CLIST_IS_EMPTY(&ss->serverCerts)) {
        cursor = PR_LIST_TAIL(&ss->serverCerts);
        PR_REMOVE_LINK(cursor);
        ssl_FreeServerCert((sslServerCert *)cursor);
    }

    /* Remove extension handlers. */
    ssl_ClearPRCList(&ss->extensionHooks, NULL);

    ssl_FreeEphemeralKeyPairs(ss);
    SECITEM_FreeItem(&ss->opt.nextProtoNego, PR_FALSE);
    ssl3_FreeSniNameArray(&ss->xtnData);

    ssl_ClearPRCList(&ss->ssl3.hs.dtlsSentHandshake, NULL);
    ssl_ClearPRCList(&ss->ssl3.hs.dtlsRcvdHandshake, NULL);
    tls13_DestroyPskList(&ss->ssl3.hs.psks);

    tls13_ReleaseAntiReplayContext(ss->antiReplay);

    tls13_DestroyPsk(ss->psk);

    tls13_DestroyEchConfigs(&ss->echConfigs);
    SECKEY_DestroyPrivateKey(ss->echPrivKey);
    SECKEY_DestroyPublicKey(ss->echPubKey);
}

/*
 * free an sslSocket struct, and all the stuff that hangs off of it
 */
void
ssl_FreeSocket(sslSocket *ss)
{
    /* Get every lock you can imagine!
    ** Caller already holds these:
    **  SSL_LOCK_READER(ss);
    **  SSL_LOCK_WRITER(ss);
    */
    ssl_Get1stHandshakeLock(ss);
    ssl_GetRecvBufLock(ss);
    ssl_GetSSL3HandshakeLock(ss);
    ssl_GetXmitBufLock(ss);
    ssl_GetSpecWriteLock(ss);

    ssl_DestroySocketContents(ss);

    /* Release all the locks acquired above.  */
    SSL_UNLOCK_READER(ss);
    SSL_UNLOCK_WRITER(ss);
    ssl_Release1stHandshakeLock(ss);
    ssl_ReleaseRecvBufLock(ss);
    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_ReleaseXmitBufLock(ss);
    ssl_ReleaseSpecWriteLock(ss);

    ssl_DestroyLocks(ss);

#ifdef DEBUG
    PORT_Memset(ss, 0x1f, sizeof *ss);
#endif
    PORT_Free(ss);
    return;
}

/************************************************************************/
SECStatus
ssl_EnableNagleDelay(sslSocket *ss, PRBool enabled)
{
    PRFileDesc *osfd = ss->fd->lower;
    SECStatus rv = SECFailure;
    PRSocketOptionData opt;

    opt.option = PR_SockOpt_NoDelay;
    opt.value.no_delay = (PRBool)!enabled;

    if (osfd->methods->setsocketoption) {
        rv = (SECStatus)osfd->methods->setsocketoption(osfd, &opt);
    } else {
        PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
    }

    return rv;
}

static void
ssl_ChooseOps(sslSocket *ss)
{
    ss->ops = ss->opt.useSecurity ? &ssl_secure_ops : &ssl_default_ops;
}

/* Called from SSL_Enable (immediately below) */
static SECStatus
PrepareSocket(sslSocket *ss)
{
    SECStatus rv = SECSuccess;

    ssl_ChooseOps(ss);
    return rv;
}

SECStatus
SSL_Enable(PRFileDesc *fd, int which, PRIntn on)
{
    return SSL_OptionSet(fd, which, on);
}

static PRBool ssl_VersionIsSupportedByPolicy(
    SSLProtocolVariant protocolVariant, SSL3ProtocolVersion version);

/* Implements the semantics for SSL_OptionSet(SSL_ENABLE_TLS, on) described in
 * ssl.h in the section "SSL version range setting API".
 */
static void
ssl_EnableTLS(SSLVersionRange *vrange, PRIntn enable)
{
    if (enable) {
        /* don't turn it on if tls1.0 disallowed by by policy */
        if (!ssl_VersionIsSupportedByPolicy(ssl_variant_stream,
                                            SSL_LIBRARY_VERSION_TLS_1_0)) {
            return;
        }
    }
    if (SSL_ALL_VERSIONS_DISABLED(vrange)) {
        if (enable) {
            vrange->min = SSL_LIBRARY_VERSION_TLS_1_0;
            vrange->max = SSL_LIBRARY_VERSION_TLS_1_0;
        } /* else don't change anything */
        return;
    }

    if (enable) {
        /* Expand the range of enabled version to include TLS 1.0 */
        vrange->min = PR_MIN(vrange->min, SSL_LIBRARY_VERSION_TLS_1_0);
        vrange->max = PR_MAX(vrange->max, SSL_LIBRARY_VERSION_TLS_1_0);
    } else {
        /* Disable all TLS versions, leaving only SSL 3.0 if it was enabled */
        if (vrange->min == SSL_LIBRARY_VERSION_3_0) {
            vrange->max = SSL_LIBRARY_VERSION_3_0;
        } else {
            /* Only TLS was enabled, so now no versions are. */
            vrange->min = SSL_LIBRARY_VERSION_NONE;
            vrange->max = SSL_LIBRARY_VERSION_NONE;
        }
    }
}

/* Implements the semantics for SSL_OptionSet(SSL_ENABLE_SSL3, on) described in
 * ssl.h in the section "SSL version range setting API".
 */
static void
ssl_EnableSSL3(SSLVersionRange *vrange, PRIntn enable)
{
    if (enable) {
        /* don't turn it on if ssl3 disallowed by by policy */
        if (!ssl_VersionIsSupportedByPolicy(ssl_variant_stream,
                                            SSL_LIBRARY_VERSION_3_0)) {
            return;
        }
    }
    if (SSL_ALL_VERSIONS_DISABLED(vrange)) {
        if (enable) {
            vrange->min = SSL_LIBRARY_VERSION_3_0;
            vrange->max = SSL_LIBRARY_VERSION_3_0;
        } /* else don't change anything */
        return;
    }

    if (enable) {
        /* Expand the range of enabled versions to include SSL 3.0. We know
         * SSL 3.0 or some version of TLS is already enabled at this point, so
         * we don't need to change vrange->max.
         */
        vrange->min = SSL_LIBRARY_VERSION_3_0;
    } else {
        /* Disable SSL 3.0, leaving TLS unaffected. */
        if (vrange->max > SSL_LIBRARY_VERSION_3_0) {
            vrange->min = PR_MAX(vrange->min, SSL_LIBRARY_VERSION_TLS_1_0);
        } else {
            /* Only SSL 3.0 was enabled, so now no versions are. */
            vrange->min = SSL_LIBRARY_VERSION_NONE;
            vrange->max = SSL_LIBRARY_VERSION_NONE;
        }
    }
}

SECStatus
SSL_OptionSet(PRFileDesc *fd, PRInt32 which, PRIntn val)
{
    sslSocket *ss = ssl_FindSocket(fd);
    SECStatus rv = SECSuccess;
    PRBool holdingLocks;

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in Enable", SSL_GETPID(), fd));
        return SECFailure;
    }

    holdingLocks = (!ss->opt.noLocks);
    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);

    switch (which) {
        case SSL_SOCKS:
            ss->opt.useSocks = PR_FALSE;
            rv = PrepareSocket(ss);
            if (val) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                rv = SECFailure;
            }
            break;

        case SSL_SECURITY:
            ss->opt.useSecurity = val;
            rv = PrepareSocket(ss);
            break;

        case SSL_REQUEST_CERTIFICATE:
            ss->opt.requestCertificate = val;
            break;

        case SSL_REQUIRE_CERTIFICATE:
            ss->opt.requireCertificate = val;
            break;

        case SSL_HANDSHAKE_AS_CLIENT:
            if (ss->opt.handshakeAsServer && val) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                rv = SECFailure;
                break;
            }
            ss->opt.handshakeAsClient = val;
            break;

        case SSL_HANDSHAKE_AS_SERVER:
            if (ss->opt.handshakeAsClient && val) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                rv = SECFailure;
                break;
            }
            ss->opt.handshakeAsServer = val;
            break;

        case SSL_ENABLE_TLS:
            if (IS_DTLS(ss)) {
                if (val) {
                    PORT_SetError(SEC_ERROR_INVALID_ARGS);
                    rv = SECFailure; /* not allowed */
                }
                break;
            }
            ssl_EnableTLS(&ss->vrange, val);
            break;

        case SSL_ENABLE_SSL3:
            if (IS_DTLS(ss)) {
                if (val) {
                    PORT_SetError(SEC_ERROR_INVALID_ARGS);
                    rv = SECFailure; /* not allowed */
                }
                break;
            }
            ssl_EnableSSL3(&ss->vrange, val);
            break;

        case SSL_ENABLE_SSL2:
        case SSL_V2_COMPATIBLE_HELLO:
            /* We no longer support SSL v2.
             * However, if an old application requests to disable SSL v2,
             * we shouldn't fail.
             */
            if (val) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                rv = SECFailure;
            }
            break;

        case SSL_NO_CACHE:
            ss->opt.noCache = val;
            break;

        case SSL_ENABLE_FDX:
            if (val && ss->opt.noLocks) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                rv = SECFailure;
            }
            ss->opt.fdx = val;
            break;

        case SSL_ROLLBACK_DETECTION:
            ss->opt.detectRollBack = val;
            break;

        case SSL_NO_STEP_DOWN:
            break;

        case SSL_BYPASS_PKCS11:
            break;

        case SSL_NO_LOCKS:
            if (val && ss->opt.fdx) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                rv = SECFailure;
            }
            if (val && ssl_force_locks)
                val = PR_FALSE; /* silent override */
            ss->opt.noLocks = val;
            if (!val && !holdingLocks) {
                rv = ssl_MakeLocks(ss);
                if (rv != SECSuccess) {
                    ss->opt.noLocks = PR_TRUE;
                }
            }
            break;

        case SSL_ENABLE_SESSION_TICKETS:
            ss->opt.enableSessionTickets = val;
            break;

        case SSL_ENABLE_DEFLATE:
            ss->opt.enableDeflate = val;
            break;

        case SSL_ENABLE_RENEGOTIATION:
            if (IS_DTLS(ss) && val != SSL_RENEGOTIATE_NEVER) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                rv = SECFailure;
                break;
            }
            ss->opt.enableRenegotiation = val;
            break;

        case SSL_REQUIRE_SAFE_NEGOTIATION:
            ss->opt.requireSafeNegotiation = val;
            break;

        case SSL_ENABLE_FALSE_START:
            ss->opt.enableFalseStart = val;
            break;

        case SSL_CBC_RANDOM_IV:
            ss->opt.cbcRandomIV = val;
            break;

        case SSL_ENABLE_OCSP_STAPLING:
            ss->opt.enableOCSPStapling = val;
            break;

        case SSL_ENABLE_DELEGATED_CREDENTIALS:
            ss->opt.enableDelegatedCredentials = val;
            break;

        case SSL_ENABLE_NPN:
            break;

        case SSL_ENABLE_ALPN:
            ss->opt.enableALPN = val;
            break;

        case SSL_REUSE_SERVER_ECDHE_KEY:
            ss->opt.reuseServerECDHEKey = val;
            break;

        case SSL_ENABLE_FALLBACK_SCSV:
            ss->opt.enableFallbackSCSV = val;
            break;

        case SSL_ENABLE_SERVER_DHE:
            ss->opt.enableServerDhe = val;
            break;

        case SSL_ENABLE_EXTENDED_MASTER_SECRET:
            ss->opt.enableExtendedMS = val;
            break;

        case SSL_ENABLE_SIGNED_CERT_TIMESTAMPS:
            ss->opt.enableSignedCertTimestamps = val;
            break;

        case SSL_REQUIRE_DH_NAMED_GROUPS:
            ss->opt.requireDHENamedGroups = val;
            break;

        case SSL_ENABLE_0RTT_DATA:
            ss->opt.enable0RttData = val;
            break;

        case SSL_RECORD_SIZE_LIMIT:
            if (val < 64 || val > (MAX_FRAGMENT_LENGTH + 1)) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                rv = SECFailure;
            } else {
                ss->opt.recordSizeLimit = val;
            }
            break;

        case SSL_ENABLE_TLS13_COMPAT_MODE:
            ss->opt.enableTls13CompatMode = val;
            break;

        case SSL_ENABLE_DTLS_SHORT_HEADER:
            ss->opt.enableDtlsShortHeader = val;
            break;

        case SSL_ENABLE_HELLO_DOWNGRADE_CHECK:
            ss->opt.enableHelloDowngradeCheck = val;
            break;

        case SSL_ENABLE_V2_COMPATIBLE_HELLO:
            ss->opt.enableV2CompatibleHello = val;
            break;

        case SSL_ENABLE_POST_HANDSHAKE_AUTH:
            ss->opt.enablePostHandshakeAuth = val;
            break;

        case SSL_SUPPRESS_END_OF_EARLY_DATA:
            ss->opt.suppressEndOfEarlyData = val;
            break;

        case SSL_ENABLE_GREASE:
            ss->opt.enableGrease = val;
            break;

        case SSL_ENABLE_CH_EXTENSION_PERMUTATION:
            ss->opt.enableChXtnPermutation = val;
            break;

        default:
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            rv = SECFailure;
    }

    /* We can't use the macros for releasing the locks here,
     * because ss->opt.noLocks might have changed just above.
     * We must release these locks (monitors) here, if we aquired them above,
     * regardless of the current value of ss->opt.noLocks.
     */
    if (holdingLocks) {
        PZ_ExitMonitor((ss)->ssl3HandshakeLock);
        PZ_ExitMonitor((ss)->firstHandshakeLock);
    }

    return rv;
}

SECStatus
SSL_OptionGet(PRFileDesc *fd, PRInt32 which, PRIntn *pVal)
{
    sslSocket *ss = ssl_FindSocket(fd);
    SECStatus rv = SECSuccess;
    PRIntn val = PR_FALSE;

    if (!pVal) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in Enable", SSL_GETPID(), fd));
        *pVal = PR_FALSE;
        return SECFailure;
    }

    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);

    switch (which) {
        case SSL_SOCKS:
            val = PR_FALSE;
            break;
        case SSL_SECURITY:
            val = ss->opt.useSecurity;
            break;
        case SSL_REQUEST_CERTIFICATE:
            val = ss->opt.requestCertificate;
            break;
        case SSL_REQUIRE_CERTIFICATE:
            val = ss->opt.requireCertificate;
            break;
        case SSL_HANDSHAKE_AS_CLIENT:
            val = ss->opt.handshakeAsClient;
            break;
        case SSL_HANDSHAKE_AS_SERVER:
            val = ss->opt.handshakeAsServer;
            break;
        case SSL_ENABLE_TLS:
            val = ss->vrange.max >= SSL_LIBRARY_VERSION_TLS_1_0;
            break;
        case SSL_ENABLE_SSL3:
            val = ss->vrange.min == SSL_LIBRARY_VERSION_3_0;
            break;
        case SSL_ENABLE_SSL2:
        case SSL_V2_COMPATIBLE_HELLO:
            val = PR_FALSE;
            break;
        case SSL_NO_CACHE:
            val = ss->opt.noCache;
            break;
        case SSL_ENABLE_FDX:
            val = ss->opt.fdx;
            break;
        case SSL_ROLLBACK_DETECTION:
            val = ss->opt.detectRollBack;
            break;
        case SSL_NO_STEP_DOWN:
            val = PR_FALSE;
            break;
        case SSL_BYPASS_PKCS11:
            val = PR_FALSE;
            break;
        case SSL_NO_LOCKS:
            val = ss->opt.noLocks;
            break;
        case SSL_ENABLE_SESSION_TICKETS:
            val = ss->opt.enableSessionTickets;
            break;
        case SSL_ENABLE_DEFLATE:
            val = ss->opt.enableDeflate;
            break;
        case SSL_ENABLE_RENEGOTIATION:
            val = ss->opt.enableRenegotiation;
            break;
        case SSL_REQUIRE_SAFE_NEGOTIATION:
            val = ss->opt.requireSafeNegotiation;
            break;
        case SSL_ENABLE_FALSE_START:
            val = ss->opt.enableFalseStart;
            break;
        case SSL_CBC_RANDOM_IV:
            val = ss->opt.cbcRandomIV;
            break;
        case SSL_ENABLE_OCSP_STAPLING:
            val = ss->opt.enableOCSPStapling;
            break;
        case SSL_ENABLE_DELEGATED_CREDENTIALS:
            val = ss->opt.enableDelegatedCredentials;
            break;
        case SSL_ENABLE_NPN:
            val = PR_FALSE;
            break;
        case SSL_ENABLE_ALPN:
            val = ss->opt.enableALPN;
            break;
        case SSL_REUSE_SERVER_ECDHE_KEY:
            val = ss->opt.reuseServerECDHEKey;
            break;
        case SSL_ENABLE_FALLBACK_SCSV:
            val = ss->opt.enableFallbackSCSV;
            break;
        case SSL_ENABLE_SERVER_DHE:
            val = ss->opt.enableServerDhe;
            break;
        case SSL_ENABLE_EXTENDED_MASTER_SECRET:
            val = ss->opt.enableExtendedMS;
            break;
        case SSL_ENABLE_SIGNED_CERT_TIMESTAMPS:
            val = ss->opt.enableSignedCertTimestamps;
            break;
        case SSL_REQUIRE_DH_NAMED_GROUPS:
            val = ss->opt.requireDHENamedGroups;
            break;
        case SSL_ENABLE_0RTT_DATA:
            val = ss->opt.enable0RttData;
            break;
        case SSL_RECORD_SIZE_LIMIT:
            val = ss->opt.recordSizeLimit;
            break;
        case SSL_ENABLE_TLS13_COMPAT_MODE:
            val = ss->opt.enableTls13CompatMode;
            break;
        case SSL_ENABLE_DTLS_SHORT_HEADER:
            val = ss->opt.enableDtlsShortHeader;
            break;
        case SSL_ENABLE_HELLO_DOWNGRADE_CHECK:
            val = ss->opt.enableHelloDowngradeCheck;
            break;
        case SSL_ENABLE_V2_COMPATIBLE_HELLO:
            val = ss->opt.enableV2CompatibleHello;
            break;
        case SSL_ENABLE_POST_HANDSHAKE_AUTH:
            val = ss->opt.enablePostHandshakeAuth;
            break;
        case SSL_SUPPRESS_END_OF_EARLY_DATA:
            val = ss->opt.suppressEndOfEarlyData;
            break;
        default:
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            rv = SECFailure;
    }

    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);

    *pVal = val;
    return rv;
}

SECStatus
SSL_OptionGetDefault(PRInt32 which, PRIntn *pVal)
{
    SECStatus rv = SECSuccess;
    PRIntn val = PR_FALSE;

    if (!pVal) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    ssl_SetDefaultsFromEnvironment();

    switch (which) {
        case SSL_SOCKS:
            val = PR_FALSE;
            break;
        case SSL_SECURITY:
            val = ssl_defaults.useSecurity;
            break;
        case SSL_REQUEST_CERTIFICATE:
            val = ssl_defaults.requestCertificate;
            break;
        case SSL_REQUIRE_CERTIFICATE:
            val = ssl_defaults.requireCertificate;
            break;
        case SSL_HANDSHAKE_AS_CLIENT:
            val = ssl_defaults.handshakeAsClient;
            break;
        case SSL_HANDSHAKE_AS_SERVER:
            val = ssl_defaults.handshakeAsServer;
            break;
        case SSL_ENABLE_TLS:
            val = versions_defaults_stream.max >= SSL_LIBRARY_VERSION_TLS_1_0;
            break;
        case SSL_ENABLE_SSL3:
            val = versions_defaults_stream.min == SSL_LIBRARY_VERSION_3_0;
            break;
        case SSL_ENABLE_SSL2:
        case SSL_V2_COMPATIBLE_HELLO:
            val = PR_FALSE;
            break;
        case SSL_NO_CACHE:
            val = ssl_defaults.noCache;
            break;
        case SSL_ENABLE_FDX:
            val = ssl_defaults.fdx;
            break;
        case SSL_ROLLBACK_DETECTION:
            val = ssl_defaults.detectRollBack;
            break;
        case SSL_NO_STEP_DOWN:
            val = PR_FALSE;
            break;
        case SSL_BYPASS_PKCS11:
            val = PR_FALSE;
            break;
        case SSL_NO_LOCKS:
            val = ssl_defaults.noLocks;
            break;
        case SSL_ENABLE_SESSION_TICKETS:
            val = ssl_defaults.enableSessionTickets;
            break;
        case SSL_ENABLE_DEFLATE:
            val = ssl_defaults.enableDeflate;
            break;
        case SSL_ENABLE_RENEGOTIATION:
            val = ssl_defaults.enableRenegotiation;
            break;
        case SSL_REQUIRE_SAFE_NEGOTIATION:
            val = ssl_defaults.requireSafeNegotiation;
            break;
        case SSL_ENABLE_FALSE_START:
            val = ssl_defaults.enableFalseStart;
            break;
        case SSL_CBC_RANDOM_IV:
            val = ssl_defaults.cbcRandomIV;
            break;
        case SSL_ENABLE_OCSP_STAPLING:
            val = ssl_defaults.enableOCSPStapling;
            break;
        case SSL_ENABLE_DELEGATED_CREDENTIALS:
            val = ssl_defaults.enableDelegatedCredentials;
            break;
        case SSL_ENABLE_NPN:
            val = PR_FALSE;
            break;
        case SSL_ENABLE_ALPN:
            val = ssl_defaults.enableALPN;
            break;
        case SSL_REUSE_SERVER_ECDHE_KEY:
            val = ssl_defaults.reuseServerECDHEKey;
            break;
        case SSL_ENABLE_FALLBACK_SCSV:
            val = ssl_defaults.enableFallbackSCSV;
            break;
        case SSL_ENABLE_SERVER_DHE:
            val = ssl_defaults.enableServerDhe;
            break;
        case SSL_ENABLE_EXTENDED_MASTER_SECRET:
            val = ssl_defaults.enableExtendedMS;
            break;
        case SSL_ENABLE_SIGNED_CERT_TIMESTAMPS:
            val = ssl_defaults.enableSignedCertTimestamps;
            break;
        case SSL_ENABLE_0RTT_DATA:
            val = ssl_defaults.enable0RttData;
            break;
        case SSL_RECORD_SIZE_LIMIT:
            val = ssl_defaults.recordSizeLimit;
            break;
        case SSL_ENABLE_TLS13_COMPAT_MODE:
            val = ssl_defaults.enableTls13CompatMode;
            break;
        case SSL_ENABLE_DTLS_SHORT_HEADER:
            val = ssl_defaults.enableDtlsShortHeader;
            break;
        case SSL_ENABLE_HELLO_DOWNGRADE_CHECK:
            val = ssl_defaults.enableHelloDowngradeCheck;
            break;
        case SSL_ENABLE_V2_COMPATIBLE_HELLO:
            val = ssl_defaults.enableV2CompatibleHello;
            break;
        case SSL_ENABLE_POST_HANDSHAKE_AUTH:
            val = ssl_defaults.enablePostHandshakeAuth;
            break;
        case SSL_SUPPRESS_END_OF_EARLY_DATA:
            val = ssl_defaults.suppressEndOfEarlyData;
            break;
        default:
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            rv = SECFailure;
    }

    *pVal = val;
    return rv;
}

/* XXX Use Global Lock to protect this stuff. */
SECStatus
SSL_EnableDefault(int which, PRIntn val)
{
    return SSL_OptionSetDefault(which, val);
}

SECStatus
SSL_OptionSetDefault(PRInt32 which, PRIntn val)
{
    SECStatus status = ssl_Init();

    if (status != SECSuccess) {
        return status;
    }

    ssl_SetDefaultsFromEnvironment();

    switch (which) {
        case SSL_SOCKS:
            ssl_defaults.useSocks = PR_FALSE;
            if (val) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                return SECFailure;
            }
            break;

        case SSL_SECURITY:
            ssl_defaults.useSecurity = val;
            break;

        case SSL_REQUEST_CERTIFICATE:
            ssl_defaults.requestCertificate = val;
            break;

        case SSL_REQUIRE_CERTIFICATE:
            ssl_defaults.requireCertificate = val;
            break;

        case SSL_HANDSHAKE_AS_CLIENT:
            if (ssl_defaults.handshakeAsServer && val) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                return SECFailure;
            }
            ssl_defaults.handshakeAsClient = val;
            break;

        case SSL_HANDSHAKE_AS_SERVER:
            if (ssl_defaults.handshakeAsClient && val) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                return SECFailure;
            }
            ssl_defaults.handshakeAsServer = val;
            break;

        case SSL_ENABLE_TLS:
            ssl_EnableTLS(&versions_defaults_stream, val);
            break;

        case SSL_ENABLE_SSL3:
            ssl_EnableSSL3(&versions_defaults_stream, val);
            break;

        case SSL_ENABLE_SSL2:
        case SSL_V2_COMPATIBLE_HELLO:
            /* We no longer support SSL v2.
             * However, if an old application requests to disable SSL v2,
             * we shouldn't fail.
             */
            if (val) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                return SECFailure;
            }
            break;

        case SSL_NO_CACHE:
            ssl_defaults.noCache = val;
            break;

        case SSL_ENABLE_FDX:
            if (val && ssl_defaults.noLocks) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                return SECFailure;
            }
            ssl_defaults.fdx = val;
            break;

        case SSL_ROLLBACK_DETECTION:
            ssl_defaults.detectRollBack = val;
            break;

        case SSL_NO_STEP_DOWN:
            break;

        case SSL_BYPASS_PKCS11:
            break;

        case SSL_NO_LOCKS:
            if (val && ssl_defaults.fdx) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                return SECFailure;
            }
            if (val && ssl_force_locks)
                val = PR_FALSE; /* silent override */
            ssl_defaults.noLocks = val;
            break;

        case SSL_ENABLE_SESSION_TICKETS:
            ssl_defaults.enableSessionTickets = val;
            break;

        case SSL_ENABLE_DEFLATE:
            ssl_defaults.enableDeflate = val;
            break;

        case SSL_ENABLE_RENEGOTIATION:
            ssl_defaults.enableRenegotiation = val;
            break;

        case SSL_REQUIRE_SAFE_NEGOTIATION:
            ssl_defaults.requireSafeNegotiation = val;
            break;

        case SSL_ENABLE_FALSE_START:
            ssl_defaults.enableFalseStart = val;
            break;

        case SSL_CBC_RANDOM_IV:
            ssl_defaults.cbcRandomIV = val;
            break;

        case SSL_ENABLE_OCSP_STAPLING:
            ssl_defaults.enableOCSPStapling = val;
            break;

        case SSL_ENABLE_DELEGATED_CREDENTIALS:
            ssl_defaults.enableDelegatedCredentials = val;
            break;

        case SSL_ENABLE_NPN:
            break;

        case SSL_ENABLE_ALPN:
            ssl_defaults.enableALPN = val;
            break;

        case SSL_REUSE_SERVER_ECDHE_KEY:
            ssl_defaults.reuseServerECDHEKey = val;
            break;

        case SSL_ENABLE_FALLBACK_SCSV:
            ssl_defaults.enableFallbackSCSV = val;
            break;

        case SSL_ENABLE_SERVER_DHE:
            ssl_defaults.enableServerDhe = val;
            break;

        case SSL_ENABLE_EXTENDED_MASTER_SECRET:
            ssl_defaults.enableExtendedMS = val;
            break;

        case SSL_ENABLE_SIGNED_CERT_TIMESTAMPS:
            ssl_defaults.enableSignedCertTimestamps = val;
            break;

        case SSL_ENABLE_0RTT_DATA:
            ssl_defaults.enable0RttData = val;
            break;

        case SSL_RECORD_SIZE_LIMIT:
            if (val < 64 || val > (MAX_FRAGMENT_LENGTH + 1)) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                return SECFailure;
            }
            ssl_defaults.recordSizeLimit = val;
            break;

        case SSL_ENABLE_TLS13_COMPAT_MODE:
            ssl_defaults.enableTls13CompatMode = val;
            break;

        case SSL_ENABLE_DTLS_SHORT_HEADER:
            ssl_defaults.enableDtlsShortHeader = val;
            break;

        case SSL_ENABLE_HELLO_DOWNGRADE_CHECK:
            ssl_defaults.enableHelloDowngradeCheck = val;
            break;

        case SSL_ENABLE_V2_COMPATIBLE_HELLO:
            ssl_defaults.enableV2CompatibleHello = val;
            break;

        case SSL_ENABLE_POST_HANDSHAKE_AUTH:
            ssl_defaults.enablePostHandshakeAuth = val;
            break;

        case SSL_SUPPRESS_END_OF_EARLY_DATA:
            ssl_defaults.suppressEndOfEarlyData = val;
            break;

        default:
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            return SECFailure;
    }
    return SECSuccess;
}

SECStatus
SSLExp_SetMaxEarlyDataSize(PRFileDesc *fd, PRUint32 size)
{
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        return SECFailure; /* Error code already set. */
    }

    ss->opt.maxEarlyDataSize = size;
    return SECSuccess;
}

/* function tells us if the cipher suite is one that we no longer support. */
static PRBool
ssl_IsRemovedCipherSuite(PRInt32 suite)
{
    switch (suite) {
        case SSL_FORTEZZA_DMS_WITH_NULL_SHA:
        case SSL_FORTEZZA_DMS_WITH_FORTEZZA_CBC_SHA:
        case SSL_FORTEZZA_DMS_WITH_RC4_128_SHA:
            return PR_TRUE;
        default:
            return PR_FALSE;
    }
}

/* Part of the public NSS API.
 * Since this is a global (not per-socket) setting, we cannot use the
 * HandshakeLock to protect this.  Probably want a global lock.
 */
SECStatus
SSL_SetPolicy(long which, int policy)
{
    if (ssl_IsRemovedCipherSuite(which))
        return SECSuccess;
    return SSL_CipherPolicySet(which, policy);
}

SECStatus
ssl_CipherPolicySet(PRInt32 which, PRInt32 policy)
{
    SECStatus rv = SECSuccess;

    if (ssl_IsRemovedCipherSuite(which)) {
        rv = SECSuccess;
    } else {
        rv = ssl3_SetPolicy((ssl3CipherSuite)which, policy);
    }
    return rv;
}
SECStatus
SSL_CipherPolicySet(PRInt32 which, PRInt32 policy)
{
    SECStatus rv = ssl_Init();

    if (rv != SECSuccess) {
        return rv;
    }
    if (NSS_IsPolicyLocked()) {
        PORT_SetError(SEC_ERROR_POLICY_LOCKED);
        return SECFailure;
    }
    return ssl_CipherPolicySet(which, policy);
}

SECStatus
SSL_CipherPolicyGet(PRInt32 which, PRInt32 *oPolicy)
{
    SECStatus rv;

    if (!oPolicy) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (ssl_IsRemovedCipherSuite(which)) {
        *oPolicy = SSL_NOT_ALLOWED;
        rv = SECSuccess;
    } else {
        rv = ssl3_GetPolicy((ssl3CipherSuite)which, oPolicy);
    }
    return rv;
}

/* Part of the public NSS API.
 * Since this is a global (not per-socket) setting, we cannot use the
 * HandshakeLock to protect this.  Probably want a global lock.
 * These changes have no effect on any sslSockets already created.
 */
SECStatus
SSL_EnableCipher(long which, PRBool enabled)
{
    if (ssl_IsRemovedCipherSuite(which))
        return SECSuccess;
    return SSL_CipherPrefSetDefault(which, enabled);
}

SECStatus
ssl_CipherPrefSetDefault(PRInt32 which, PRBool enabled)
{
    if (ssl_IsRemovedCipherSuite(which))
        return SECSuccess;
    return ssl3_CipherPrefSetDefault((ssl3CipherSuite)which, enabled);
}

SECStatus
SSL_CipherPrefSetDefault(PRInt32 which, PRBool enabled)
{
    SECStatus rv = ssl_Init();
    PRInt32 locks;

    if (rv != SECSuccess) {
        return rv;
    }
    rv = NSS_OptionGet(NSS_DEFAULT_LOCKS, &locks);
    if ((rv == SECSuccess) && (locks & NSS_DEFAULT_SSL_LOCK)) {
        return SECSuccess;
    }
    return ssl_CipherPrefSetDefault(which, enabled);
}

SECStatus
SSL_CipherPrefGetDefault(PRInt32 which, PRBool *enabled)
{
    SECStatus rv;

    if (!enabled) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (ssl_IsRemovedCipherSuite(which)) {
        *enabled = PR_FALSE;
        rv = SECSuccess;
    } else {
        rv = ssl3_CipherPrefGetDefault((ssl3CipherSuite)which, enabled);
    }
    return rv;
}

SECStatus
SSL_CipherPrefSet(PRFileDesc *fd, PRInt32 which, PRBool enabled)
{
    sslSocket *ss = ssl_FindSocket(fd);
    PRInt32 locks;
    SECStatus rv;

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in CipherPrefSet", SSL_GETPID(), fd));
        return SECFailure;
    }
    rv = NSS_OptionGet(NSS_DEFAULT_LOCKS, &locks);
    if ((rv == SECSuccess) && (locks & NSS_DEFAULT_SSL_LOCK)) {
        return SECSuccess;
    }
    if (ssl_IsRemovedCipherSuite(which))
        return SECSuccess;
    return ssl3_CipherPrefSet(ss, (ssl3CipherSuite)which, enabled);
}

SECStatus
SSL_CipherPrefGet(PRFileDesc *fd, PRInt32 which, PRBool *enabled)
{
    SECStatus rv;
    sslSocket *ss = ssl_FindSocket(fd);

    if (!enabled) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in CipherPrefGet", SSL_GETPID(), fd));
        *enabled = PR_FALSE;
        return SECFailure;
    }
    if (ssl_IsRemovedCipherSuite(which)) {
        *enabled = PR_FALSE;
        rv = SECSuccess;
    } else {
        rv = ssl3_CipherPrefGet(ss, (ssl3CipherSuite)which, enabled);
    }
    return rv;
}

/* The client can call this function to be aware of the current
 * CipherSuites order. */
SECStatus
SSLExp_CipherSuiteOrderGet(PRFileDesc *fd, PRUint16 *cipherOrder,
                           unsigned int *numCiphers)
{
    if (!fd) {
        SSL_DBG(("%d: SSL: file descriptor in CipherSuiteOrderGet is null",
                 SSL_GETPID()));
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (!cipherOrder || !numCiphers) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in CipherSuiteOrderGet", SSL_GETPID(),
                 fd));
        return SECFailure; /* Error code already set. */
    }

    unsigned int enabled = 0;
    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);
    for (unsigned int i = 0; i < ssl_V3_SUITES_IMPLEMENTED; i++) {
        const ssl3CipherSuiteCfg *suiteCfg = &ss->cipherSuites[i];
        if (suiteCfg && suiteCfg->enabled &&
            suiteCfg->policy != SSL_NOT_ALLOWED) {
            cipherOrder[enabled++] = suiteCfg->cipher_suite;
        }
    }
    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);
    *numCiphers = enabled;
    return SECSuccess;
}

/* This function permits reorder the CipherSuites List for the Handshake
 * (Client Hello). */
SECStatus
SSLExp_CipherSuiteOrderSet(PRFileDesc *fd, const PRUint16 *cipherOrder,
                           PRUint16 numCiphers)
{
    if (!fd) {
        SSL_DBG(("%d: SSL: file descriptor in CipherSuiteOrderGet is null",
                 SSL_GETPID()));
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (!cipherOrder || !numCiphers || numCiphers > ssl_V3_SUITES_IMPLEMENTED) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in CipherSuiteOrderSet", SSL_GETPID(),
                 fd));
        return SECFailure; /* Error code already set. */
    }
    ssl3CipherSuiteCfg tmpSuiteCfg[ssl_V3_SUITES_IMPLEMENTED];
    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);
    /* For each cipherSuite given as input, verify that it is
     * known to NSS and only present in the list once. */
    for (unsigned int i = 0; i < numCiphers; i++) {
        const ssl3CipherSuiteCfg *suiteCfg =
            ssl_LookupCipherSuiteCfg(cipherOrder[i], ss->cipherSuites);
        if (!suiteCfg) {
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            ssl_ReleaseSSL3HandshakeLock(ss);
            ssl_Release1stHandshakeLock(ss);
            return SECFailure;
        }
        for (unsigned int j = i + 1; j < numCiphers; j++) {
            /* This is a duplicate entry. */
            if (cipherOrder[i] == cipherOrder[j]) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                ssl_ReleaseSSL3HandshakeLock(ss);
                ssl_Release1stHandshakeLock(ss);
                return SECFailure;
            }
        }
        tmpSuiteCfg[i] = *suiteCfg;
        tmpSuiteCfg[i].enabled = PR_TRUE;
    }
    /* Find all defined ciphersuites not present in the input list and append
     * them after the preferred. This guarantees that the socket will always
     * have a complete list of size ssl_V3_SUITES_IMPLEMENTED */
    unsigned int cfgIdx = numCiphers;
    for (unsigned int i = 0; i < ssl_V3_SUITES_IMPLEMENTED; i++) {
        PRBool received = PR_FALSE;
        for (unsigned int j = 0; j < numCiphers; j++) {
            if (ss->cipherSuites[i].cipher_suite ==
                tmpSuiteCfg[j].cipher_suite) {
                received = PR_TRUE;
                break;
            }
        }
        if (!received) {
            tmpSuiteCfg[cfgIdx] = ss->cipherSuites[i];
            tmpSuiteCfg[cfgIdx++].enabled = PR_FALSE;
        }
    }
    PORT_Assert(cfgIdx == ssl_V3_SUITES_IMPLEMENTED);
    /* now we can rewrite the socket with the desired order */
    PORT_Memcpy(ss->cipherSuites, tmpSuiteCfg, sizeof(tmpSuiteCfg));
    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);
    return SECSuccess;
}

SECStatus
NSS_SetDomesticPolicy(void)
{
    SECStatus status = SECSuccess;
    const PRUint16 *cipher;
    SECStatus rv;
    PRUint32 policy;

    /* If we've already defined some policy oids, skip changing them */
    rv = NSS_GetAlgorithmPolicy(SEC_OID_APPLY_SSL_POLICY, &policy);
    if ((rv == SECSuccess) && (policy & NSS_USE_POLICY_IN_SSL)) {
        return ssl_Init(); /* make sure the policies have been loaded */
    }

    for (cipher = SSL_ImplementedCiphers; *cipher != 0; ++cipher) {
        status = SSL_SetPolicy(*cipher, SSL_ALLOWED);
        if (status != SECSuccess)
            break;
    }
    return status;
}

SECStatus
NSS_SetExportPolicy(void)
{
    return NSS_SetDomesticPolicy();
}

SECStatus
NSS_SetFrancePolicy(void)
{
    return NSS_SetDomesticPolicy();
}

SECStatus
SSL_NamedGroupConfig(PRFileDesc *fd, const SSLNamedGroup *groups,
                     unsigned int numGroups)
{
    unsigned int i;
    unsigned int j = 0;
    sslSocket *ss = ssl_FindSocket(fd);

    if (!ss) {
        PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
        return SECFailure;
    }

    if (!groups) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (numGroups > SSL_NAMED_GROUP_COUNT) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    memset((void *)ss->namedGroupPreferences, 0,
           sizeof(ss->namedGroupPreferences));
    for (i = 0; i < numGroups; ++i) {
        const sslNamedGroupDef *groupDef = ssl_LookupNamedGroup(groups[i]);
        if (!ssl_NamedGroupEnabled(ss, groupDef)) {
            ss->namedGroupPreferences[j++] = groupDef;
        }
    }

    return SECSuccess;
}

SECStatus
SSL_DHEGroupPrefSet(PRFileDesc *fd, const SSLDHEGroupType *groups,
                    PRUint16 num_groups)
{
    sslSocket *ss;
    const SSLDHEGroupType *list;
    unsigned int count;
    int i, k, j;
    const sslNamedGroupDef *enabled[SSL_NAMED_GROUP_COUNT] = { 0 };
    static const SSLDHEGroupType default_dhe_groups[] = {
        ssl_ff_dhe_2048_group
    };

    if ((num_groups && !groups) || (!num_groups && groups) ||
        num_groups > SSL_NAMED_GROUP_COUNT) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_DHEGroupPrefSet", SSL_GETPID(), fd));
        return SECFailure;
    }

    if (groups) {
        list = groups;
        count = num_groups;
    } else {
        list = default_dhe_groups;
        count = PR_ARRAY_SIZE(default_dhe_groups);
    }

    /* save enabled ec groups and clear ss->namedGroupPreferences */
    k = 0;
    for (i = 0; i < SSL_NAMED_GROUP_COUNT; ++i) {
        if (ss->namedGroupPreferences[i] &&
            ss->namedGroupPreferences[i]->keaType != ssl_kea_dh) {
            enabled[k++] = ss->namedGroupPreferences[i];
        }
        ss->namedGroupPreferences[i] = NULL;
    }

    ss->ssl3.dhePreferredGroup = NULL;
    for (i = 0; i < count; ++i) {
        PRBool duplicate = PR_FALSE;
        SSLNamedGroup name;
        const sslNamedGroupDef *groupDef;
        switch (list[i]) {
            case ssl_ff_dhe_2048_group:
                name = ssl_grp_ffdhe_2048;
                break;
            case ssl_ff_dhe_3072_group:
                name = ssl_grp_ffdhe_3072;
                break;
            case ssl_ff_dhe_4096_group:
                name = ssl_grp_ffdhe_4096;
                break;
            case ssl_ff_dhe_6144_group:
                name = ssl_grp_ffdhe_6144;
                break;
            case ssl_ff_dhe_8192_group:
                name = ssl_grp_ffdhe_8192;
                break;
            default:
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                return SECFailure;
        }
        groupDef = ssl_LookupNamedGroup(name);
        PORT_Assert(groupDef);
        if (!ss->ssl3.dhePreferredGroup) {
            ss->ssl3.dhePreferredGroup = groupDef;
        }
        PORT_Assert(k < SSL_NAMED_GROUP_COUNT);
        for (j = 0; j < k; ++j) {
            /* skip duplicates */
            if (enabled[j] == groupDef) {
                duplicate = PR_TRUE;
                break;
            }
        }
        if (!duplicate) {
            enabled[k++] = groupDef;
        }
    }
    for (i = 0; i < k; ++i) {
        ss->namedGroupPreferences[i] = enabled[i];
    }

    return SECSuccess;
}

PRCallOnceType gWeakDHParamsRegisterOnce;
int gWeakDHParamsRegisterError;

PRCallOnceType gWeakDHParamsOnce;
int gWeakDHParamsError;
/* As our code allocates type PQGParams, we'll keep it around,
 * even though we only make use of it's parameters through gWeakDHParam. */
static PQGParams *gWeakParamsPQG;
static ssl3DHParams *gWeakDHParams;
#define WEAK_DHE_SIZE 1024

static PRStatus
ssl3_CreateWeakDHParams(void)
{
    PQGVerify *vfy;
    SECStatus rv, passed;

    PORT_Assert(!gWeakDHParams && !gWeakParamsPQG);

    rv = PK11_PQG_ParamGenV2(WEAK_DHE_SIZE, 160, 64 /*maximum seed that will work*/,
                             &gWeakParamsPQG, &vfy);
    if (rv != SECSuccess) {
        gWeakDHParamsError = PORT_GetError();
        return PR_FAILURE;
    }

    rv = PK11_PQG_VerifyParams(gWeakParamsPQG, vfy, &passed);
    if (rv != SECSuccess || passed != SECSuccess) {
        SSL_DBG(("%d: PK11_PQG_VerifyParams failed in ssl3_CreateWeakDHParams",
                 SSL_GETPID()));
        gWeakDHParamsError = PORT_GetError();
        return PR_FAILURE;
    }

    gWeakDHParams = PORT_ArenaNew(gWeakParamsPQG->arena, ssl3DHParams);
    if (!gWeakDHParams) {
        gWeakDHParamsError = PORT_GetError();
        return PR_FAILURE;
    }

    gWeakDHParams->name = ssl_grp_ffdhe_custom;
    gWeakDHParams->prime.data = gWeakParamsPQG->prime.data;
    gWeakDHParams->prime.len = gWeakParamsPQG->prime.len;
    gWeakDHParams->base.data = gWeakParamsPQG->base.data;
    gWeakDHParams->base.len = gWeakParamsPQG->base.len;

    PK11_PQG_DestroyVerify(vfy);
    return PR_SUCCESS;
}

static SECStatus
ssl3_WeakDHParamsShutdown(void *appData, void *nssData)
{
    if (gWeakParamsPQG) {
        PK11_PQG_DestroyParams(gWeakParamsPQG);
        gWeakParamsPQG = NULL;
        gWeakDHParams = NULL;
    }
    return SECSuccess;
}

static PRStatus
ssl3_WeakDHParamsRegisterShutdown(void)
{
    SECStatus rv;
    rv = NSS_RegisterShutdown(ssl3_WeakDHParamsShutdown, NULL);
    if (rv != SECSuccess) {
        gWeakDHParamsRegisterError = PORT_GetError();
    }
    return (PRStatus)rv;
}

/* global init strategy inspired by ssl3_CreateECDHEphemeralKeys */
SECStatus
SSL_EnableWeakDHEPrimeGroup(PRFileDesc *fd, PRBool enabled)
{
    sslSocket *ss;
    PRStatus status;

    if (enabled) {
        status = PR_CallOnce(&gWeakDHParamsRegisterOnce,
                             ssl3_WeakDHParamsRegisterShutdown);
        if (status != PR_SUCCESS) {
            PORT_SetError(gWeakDHParamsRegisterError);
            return SECFailure;
        }

        status = PR_CallOnce(&gWeakDHParamsOnce, ssl3_CreateWeakDHParams);
        if (status != PR_SUCCESS) {
            PORT_SetError(gWeakDHParamsError);
            return SECFailure;
        }
    }

    if (!fd)
        return SECSuccess;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_DHEGroupPrefSet", SSL_GETPID(), fd));
        return SECFailure;
    }

    ss->ssl3.dheWeakGroupEnabled = enabled;
    return SECSuccess;
}

#include "dhe-param.c"

const ssl3DHParams *
ssl_GetDHEParams(const sslNamedGroupDef *groupDef)
{
    switch (groupDef->name) {
        case ssl_grp_ffdhe_2048:
            return &ff_dhe_2048_params;
        case ssl_grp_ffdhe_3072:
            return &ff_dhe_3072_params;
        case ssl_grp_ffdhe_4096:
            return &ff_dhe_4096_params;
        case ssl_grp_ffdhe_6144:
            return &ff_dhe_6144_params;
        case ssl_grp_ffdhe_8192:
            return &ff_dhe_8192_params;
        case ssl_grp_ffdhe_custom:
            PORT_Assert(gWeakDHParams);
            return gWeakDHParams;
        default:
            PORT_Assert(0);
    }
    return NULL;
}

/* This validates dh_Ys against the group prime. */
PRBool
ssl_IsValidDHEShare(const SECItem *dh_p, const SECItem *dh_Ys)
{
    unsigned int size_p = SECKEY_BigIntegerBitLength(dh_p);
    unsigned int size_y = SECKEY_BigIntegerBitLength(dh_Ys);
    unsigned int commonPart;
    int cmp;

    if (dh_p->len == 0 || dh_Ys->len == 0) {
        return PR_FALSE;
    }
    /* Check that the prime is at least odd. */
    if ((dh_p->data[dh_p->len - 1] & 0x01) == 0) {
        return PR_FALSE;
    }
    /* dh_Ys can't be 1, or bigger than dh_p. */
    if (size_y <= 1 || size_y > size_p) {
        return PR_FALSE;
    }
    /* If dh_Ys is shorter, then it's definitely smaller than p-1. */
    if (size_y < size_p) {
        return PR_TRUE;
    }

    /* Compare the common part of each, minus the final octet. */
    commonPart = (size_p + 7) / 8;
    PORT_Assert(commonPart <= dh_Ys->len);
    PORT_Assert(commonPart <= dh_p->len);
    cmp = PORT_Memcmp(dh_Ys->data + dh_Ys->len - commonPart,
                      dh_p->data + dh_p->len - commonPart, commonPart - 1);
    if (cmp < 0) {
        return PR_TRUE;
    }
    if (cmp > 0) {
        return PR_FALSE;
    }

    /* The last octet of the prime is the only thing that is different and that
     * has to be two greater than the share, otherwise we have Ys == p - 1,
     * and that means small subgroups. */
    if (dh_Ys->data[dh_Ys->len - 1] >= (dh_p->data[dh_p->len - 1] - 1)) {
        return PR_FALSE;
    }

    return PR_TRUE;
}

/* Checks that the provided DH parameters match those in one of the named groups
 * that we have enabled.  The groups are defined in dhe-param.c and are those
 * defined in Appendix A of draft-ietf-tls-negotiated-ff-dhe.
 *
 * |groupDef| and |dhParams| are optional outparams that identify the group and
 * its parameters respectively (if this is successful). */
SECStatus
ssl_ValidateDHENamedGroup(sslSocket *ss,
                          const SECItem *dh_p,
                          const SECItem *dh_g,
                          const sslNamedGroupDef **groupDef,
                          const ssl3DHParams **dhParams)
{
    unsigned int i;

    for (i = 0; i < SSL_NAMED_GROUP_COUNT; ++i) {
        const ssl3DHParams *params;
        if (!ss->namedGroupPreferences[i]) {
            continue;
        }
        if (ss->namedGroupPreferences[i]->keaType != ssl_kea_dh) {
            continue;
        }

        params = ssl_GetDHEParams(ss->namedGroupPreferences[i]);
        PORT_Assert(params);
        if (SECITEM_ItemsAreEqual(&params->prime, dh_p)) {
            if (!SECITEM_ItemsAreEqual(&params->base, dh_g)) {
                return SECFailure;
            }
            if (groupDef)
                *groupDef = ss->namedGroupPreferences[i];
            if (dhParams)
                *dhParams = params;
            return SECSuccess;
        }
    }

    return SECFailure;
}

/* Ensure DH parameters have been selected.  This just picks the first enabled
 * FFDHE group in ssl_named_groups, or the weak one if it was enabled. */
SECStatus
ssl_SelectDHEGroup(sslSocket *ss, const sslNamedGroupDef **groupDef)
{
    unsigned int i;
    static const sslNamedGroupDef weak_group_def = {
        ssl_grp_ffdhe_custom, WEAK_DHE_SIZE, ssl_kea_dh,
        SEC_OID_TLS_DHE_CUSTOM, PR_TRUE
    };
    PRInt32 minDH;
    SECStatus rv;

    // make sure we select a group consistent with our
    // current policy policy
    rv = NSS_OptionGet(NSS_DH_MIN_KEY_SIZE, &minDH);
    if (rv != SECSuccess || minDH <= 0) {
        minDH = DH_MIN_P_BITS;
    }

    /* Only select weak groups in TLS 1.2 and earlier, but not if the client has
     * indicated that it supports an FFDHE named group. */
    if (ss->ssl3.dheWeakGroupEnabled &&
        ss->version < SSL_LIBRARY_VERSION_TLS_1_3 &&
        !ss->xtnData.peerSupportsFfdheGroups &&
        weak_group_def.bits >= minDH) {
        *groupDef = &weak_group_def;
        return SECSuccess;
    }
    if (ss->ssl3.dhePreferredGroup &&
        ssl_NamedGroupEnabled(ss, ss->ssl3.dhePreferredGroup) &&
        ss->ssl3.dhePreferredGroup->bits >= minDH) {
        *groupDef = ss->ssl3.dhePreferredGroup;
        return SECSuccess;
    }
    for (i = 0; i < SSL_NAMED_GROUP_COUNT; ++i) {
        if (ss->namedGroupPreferences[i] &&
            ss->namedGroupPreferences[i]->keaType == ssl_kea_dh &&
            ss->namedGroupPreferences[i]->bits >= minDH) {
            *groupDef = ss->namedGroupPreferences[i];
            return SECSuccess;
        }
    }

    *groupDef = NULL;
    PORT_SetError(SSL_ERROR_NO_CYPHER_OVERLAP);
    return SECFailure;
}

/* LOCKS ??? XXX */
static PRFileDesc *
ssl_ImportFD(PRFileDesc *model, PRFileDesc *fd, SSLProtocolVariant variant)
{
    sslSocket *ns = NULL;
    PRStatus rv;
    PRNetAddr addr;
    SECStatus status = ssl_Init();

    if (status != SECSuccess) {
        return NULL;
    }

    if (model == NULL) {
        /* Just create a default socket if we're given NULL for the model */
        ns = ssl_NewSocket((PRBool)(!ssl_defaults.noLocks), variant);
    } else {
        sslSocket *ss = ssl_FindSocket(model);
        if (ss == NULL || ss->protocolVariant != variant) {
            SSL_DBG(("%d: SSL[%d]: bad model socket in ssl_ImportFD",
                     SSL_GETPID(), model));
            return NULL;
        }
        ns = ssl_DupSocket(ss);
    }
    if (ns == NULL)
        return NULL;

    rv = ssl_PushIOLayer(ns, fd, PR_TOP_IO_LAYER);
    if (rv != PR_SUCCESS) {
        ssl_FreeSocket(ns);
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return NULL;
    }
#if defined(DEBUG) || defined(FORCE_PR_ASSERT)
    {
        sslSocket *ss = ssl_FindSocket(fd);
        PORT_Assert(ss == ns);
    }
#endif
    ns->TCPconnected = (PR_SUCCESS == ssl_DefGetpeername(ns, &addr));
    return fd;
}

PRFileDesc *
SSL_ImportFD(PRFileDesc *model, PRFileDesc *fd)
{
    return ssl_ImportFD(model, fd, ssl_variant_stream);
}

PRFileDesc *
DTLS_ImportFD(PRFileDesc *model, PRFileDesc *fd)
{
    return ssl_ImportFD(model, fd, ssl_variant_datagram);
}

/* SSL_SetNextProtoCallback is used to select an application protocol
 * for ALPN. */
SECStatus
SSL_SetNextProtoCallback(PRFileDesc *fd, SSLNextProtoCallback callback,
                         void *arg)
{
    sslSocket *ss = ssl_FindSocket(fd);

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SetNextProtoCallback", SSL_GETPID(),
                 fd));
        return SECFailure;
    }

    ssl_GetSSL3HandshakeLock(ss);
    ss->nextProtoCallback = callback;
    ss->nextProtoArg = arg;
    ssl_ReleaseSSL3HandshakeLock(ss);

    return SECSuccess;
}

/* ssl_NextProtoNegoCallback is set as an ALPN callback when
 * SSL_SetNextProtoNego is used.
 */
static SECStatus
ssl_NextProtoNegoCallback(void *arg, PRFileDesc *fd,
                          const unsigned char *protos, unsigned int protos_len,
                          unsigned char *protoOut, unsigned int *protoOutLen,
                          unsigned int protoMaxLen)
{
    unsigned int i, j;
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in ssl_NextProtoNegoCallback",
                 SSL_GETPID(), fd));
        return SECFailure;
    }
    if (ss->opt.nextProtoNego.len == 0) {
        SSL_DBG(("%d: SSL[%d]: ssl_NextProtoNegoCallback ALPN disabled",
                 SSL_GETPID(), fd));
        SSL3_SendAlert(ss, alert_fatal, unsupported_extension);
        return SECFailure;
    }

    PORT_Assert(protoMaxLen <= 255);
    if (protoMaxLen > 255) {
        PORT_SetError(SEC_ERROR_OUTPUT_LEN);
        return SECFailure;
    }

    /* For each protocol in client preference, see if we support it. */
    for (j = 0; j < ss->opt.nextProtoNego.len;) {
        for (i = 0; i < protos_len;) {
            if (protos[i] == ss->opt.nextProtoNego.data[j] &&
                PORT_Memcmp(&protos[i + 1], &ss->opt.nextProtoNego.data[j + 1],
                            protos[i]) == 0) {
                /* We found a match. */
                const unsigned char *result = &protos[i];
                memcpy(protoOut, result + 1, result[0]);
                *protoOutLen = result[0];
                return SECSuccess;
            }
            i += 1 + (unsigned int)protos[i];
        }
        j += 1 + (unsigned int)ss->opt.nextProtoNego.data[j];
    }

    return SECSuccess;
}

SECStatus
SSL_SetNextProtoNego(PRFileDesc *fd, const unsigned char *data,
                     unsigned int length)
{
    sslSocket *ss;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SetNextProtoNego",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    if (length > 0 && ssl3_ValidateAppProtocol(data, length) != SECSuccess) {
        return SECFailure;
    }

    /* NPN required that the client's fallback protocol is first in the
     * list. However, ALPN sends protocols in preference order. So move the
     * first protocol to the end of the list. */
    ssl_GetSSL3HandshakeLock(ss);
    SECITEM_FreeItem(&ss->opt.nextProtoNego, PR_FALSE);
    if (length > 0) {
        SECITEM_AllocItem(NULL, &ss->opt.nextProtoNego, length);
        size_t firstLen = data[0] + 1;
        /* firstLen <= length is ensured by ssl3_ValidateAppProtocol. */
        PORT_Memcpy(ss->opt.nextProtoNego.data + (length - firstLen), data, firstLen);
        PORT_Memcpy(ss->opt.nextProtoNego.data, data + firstLen, length - firstLen);
    }
    ssl_ReleaseSSL3HandshakeLock(ss);

    return SSL_SetNextProtoCallback(fd, ssl_NextProtoNegoCallback, NULL);
}

SECStatus
SSL_GetNextProto(PRFileDesc *fd, SSLNextProtoState *state, unsigned char *buf,
                 unsigned int *bufLen, unsigned int bufLenMax)
{
    sslSocket *ss = ssl_FindSocket(fd);

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_GetNextProto", SSL_GETPID(),
                 fd));
        return SECFailure;
    }

    if (!state || !buf || !bufLen) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    *state = ss->xtnData.nextProtoState;

    if (ss->xtnData.nextProtoState != SSL_NEXT_PROTO_NO_SUPPORT &&
        ss->xtnData.nextProto.data) {
        if (ss->xtnData.nextProto.len > bufLenMax) {
            PORT_SetError(SEC_ERROR_OUTPUT_LEN);
            return SECFailure;
        }
        PORT_Memcpy(buf, ss->xtnData.nextProto.data, ss->xtnData.nextProto.len);
        *bufLen = ss->xtnData.nextProto.len;
    } else {
        *bufLen = 0;
    }

    return SECSuccess;
}

SECStatus
SSL_SetSRTPCiphers(PRFileDesc *fd,
                   const PRUint16 *ciphers,
                   unsigned int numCiphers)
{
    sslSocket *ss;
    unsigned int i;

    ss = ssl_FindSocket(fd);
    if (!ss || !IS_DTLS(ss)) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SetSRTPCiphers",
                 SSL_GETPID(), fd));
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (numCiphers > MAX_DTLS_SRTP_CIPHER_SUITES) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    ss->ssl3.dtlsSRTPCipherCount = 0;
    for (i = 0; i < numCiphers; i++) {
        const PRUint16 *srtpCipher = srtpCiphers;

        while (*srtpCipher) {
            if (ciphers[i] == *srtpCipher)
                break;
            srtpCipher++;
        }
        if (*srtpCipher) {
            ss->ssl3.dtlsSRTPCiphers[ss->ssl3.dtlsSRTPCipherCount++] =
                ciphers[i];
        } else {
            SSL_DBG(("%d: SSL[%d]: invalid or unimplemented SRTP cipher "
                     "suite specified: 0x%04hx",
                     SSL_GETPID(), fd,
                     ciphers[i]));
        }
    }

    if (ss->ssl3.dtlsSRTPCipherCount == 0) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    return SECSuccess;
}

SECStatus
SSL_GetSRTPCipher(PRFileDesc *fd, PRUint16 *cipher)
{
    sslSocket *ss;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_GetSRTPCipher",
                 SSL_GETPID(), fd));
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (!ss->xtnData.dtlsSRTPCipherSuite) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    *cipher = ss->xtnData.dtlsSRTPCipherSuite;
    return SECSuccess;
}

PRFileDesc *
SSL_ReconfigFD(PRFileDesc *model, PRFileDesc *fd)
{
    sslSocket *sm = NULL, *ss = NULL;
    PRCList *cursor;
    SECStatus rv;

    if (model == NULL) {
        PR_SetError(SEC_ERROR_INVALID_ARGS, 0);
        return NULL;
    }
    sm = ssl_FindSocket(model);
    if (sm == NULL) {
        SSL_DBG(("%d: SSL[%d]: bad model socket in ssl_ReconfigFD",
                 SSL_GETPID(), model));
        return NULL;
    }
    ss = ssl_FindSocket(fd);
    PORT_Assert(ss);
    if (ss == NULL) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    ss->opt = sm->opt;
    ss->vrange = sm->vrange;
    ss->now = sm->now;
    ss->nowArg = sm->nowArg;
    PORT_Memcpy(ss->cipherSuites, sm->cipherSuites, sizeof sm->cipherSuites);
    PORT_Memcpy(ss->ssl3.dtlsSRTPCiphers, sm->ssl3.dtlsSRTPCiphers,
                sizeof(PRUint16) * sm->ssl3.dtlsSRTPCipherCount);
    ss->ssl3.dtlsSRTPCipherCount = sm->ssl3.dtlsSRTPCipherCount;
    PORT_Memcpy(ss->ssl3.signatureSchemes, sm->ssl3.signatureSchemes,
                sizeof(ss->ssl3.signatureSchemes[0]) *
                    sm->ssl3.signatureSchemeCount);
    ss->ssl3.signatureSchemeCount = sm->ssl3.signatureSchemeCount;
    ss->ssl3.downgradeCheckVersion = sm->ssl3.downgradeCheckVersion;

    if (!ss->opt.useSecurity) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }
    while (!PR_CLIST_IS_EMPTY(&ss->serverCerts)) {
        cursor = PR_LIST_TAIL(&ss->serverCerts);
        PR_REMOVE_LINK(cursor);
        ssl_FreeServerCert((sslServerCert *)cursor);
    }
    for (cursor = PR_NEXT_LINK(&sm->serverCerts);
         cursor != &sm->serverCerts;
         cursor = PR_NEXT_LINK(cursor)) {
        sslServerCert *sc = ssl_CopyServerCert((sslServerCert *)cursor);
        if (!sc)
            return NULL;
        PR_APPEND_LINK(&sc->link, &ss->serverCerts);
    }

    ssl_FreeEphemeralKeyPairs(ss);
    for (cursor = PR_NEXT_LINK(&sm->ephemeralKeyPairs);
         cursor != &sm->ephemeralKeyPairs;
         cursor = PR_NEXT_LINK(cursor)) {
        sslEphemeralKeyPair *mkp = (sslEphemeralKeyPair *)cursor;
        sslEphemeralKeyPair *skp = ssl_CopyEphemeralKeyPair(mkp);
        if (!skp)
            return NULL;
        PR_APPEND_LINK(&skp->link, &ss->ephemeralKeyPairs);
    }

    while (!PR_CLIST_IS_EMPTY(&ss->extensionHooks)) {
        cursor = PR_LIST_TAIL(&ss->extensionHooks);
        PR_REMOVE_LINK(cursor);
        PORT_Free(cursor);
    }
    for (cursor = PR_NEXT_LINK(&sm->extensionHooks);
         cursor != &sm->extensionHooks;
         cursor = PR_NEXT_LINK(cursor)) {
        sslCustomExtensionHooks *hook = (sslCustomExtensionHooks *)cursor;
        rv = SSL_InstallExtensionHooks(ss->fd, hook->type,
                                       hook->writer, hook->writerArg,
                                       hook->handler, hook->handlerArg);
        if (rv != SECSuccess) {
            return NULL;
        }
    }

    PORT_Memcpy((void *)ss->namedGroupPreferences,
                sm->namedGroupPreferences,
                sizeof(ss->namedGroupPreferences));
    ss->additionalShares = sm->additionalShares;

    /* copy trust anchor names */
    if (sm->ssl3.ca_list) {
        if (ss->ssl3.ca_list) {
            CERT_FreeDistNames(ss->ssl3.ca_list);
        }
        ss->ssl3.ca_list = CERT_DupDistNames(sm->ssl3.ca_list);
        if (!ss->ssl3.ca_list) {
            return NULL;
        }
    }

    /* Copy ECH. */
    tls13_DestroyEchConfigs(&ss->echConfigs);
    SECKEY_DestroyPrivateKey(ss->echPrivKey);
    SECKEY_DestroyPublicKey(ss->echPubKey);
    rv = tls13_CopyEchConfigs(&sm->echConfigs, &ss->echConfigs);
    if (rv != SECSuccess) {
        return NULL;
    }
    if (sm->echPrivKey && sm->echPubKey) {
        /* Might be client (no keys). */
        ss->echPrivKey = SECKEY_CopyPrivateKey(sm->echPrivKey);
        ss->echPubKey = SECKEY_CopyPublicKey(sm->echPubKey);
        if (!ss->echPrivKey || !ss->echPubKey) {
            return NULL;
        }
    }

    /* Copy anti-replay context. */
    if (ss->antiReplay) {
        tls13_ReleaseAntiReplayContext(ss->antiReplay);
        ss->antiReplay = NULL;
    }
    if (sm->antiReplay) {
        ss->antiReplay = tls13_RefAntiReplayContext(sm->antiReplay);
        PORT_Assert(ss->antiReplay);
        if (!ss->antiReplay) {
            return NULL;
        }
    }

    tls13_ResetHandshakePsks(sm, &ss->ssl3.hs.psks);

    if (sm->authCertificate)
        ss->authCertificate = sm->authCertificate;
    if (sm->authCertificateArg)
        ss->authCertificateArg = sm->authCertificateArg;
    if (sm->getClientAuthData)
        ss->getClientAuthData = sm->getClientAuthData;
    if (sm->getClientAuthDataArg)
        ss->getClientAuthDataArg = sm->getClientAuthDataArg;
    if (sm->sniSocketConfig)
        ss->sniSocketConfig = sm->sniSocketConfig;
    if (sm->sniSocketConfigArg)
        ss->sniSocketConfigArg = sm->sniSocketConfigArg;
    if (sm->alertReceivedCallback) {
        ss->alertReceivedCallback = sm->alertReceivedCallback;
        ss->alertReceivedCallbackArg = sm->alertReceivedCallbackArg;
    }
    if (sm->alertSentCallback) {
        ss->alertSentCallback = sm->alertSentCallback;
        ss->alertSentCallbackArg = sm->alertSentCallbackArg;
    }
    if (sm->handleBadCert)
        ss->handleBadCert = sm->handleBadCert;
    if (sm->badCertArg)
        ss->badCertArg = sm->badCertArg;
    if (sm->handshakeCallback)
        ss->handshakeCallback = sm->handshakeCallback;
    if (sm->handshakeCallbackData)
        ss->handshakeCallbackData = sm->handshakeCallbackData;
    if (sm->pkcs11PinArg)
        ss->pkcs11PinArg = sm->pkcs11PinArg;

    return fd;
}

SECStatus
ssl3_GetEffectiveVersionPolicy(SSLProtocolVariant variant,
                               SSLVersionRange *effectivePolicy)
{
    SECStatus rv;
    PRUint32 policyFlag;
    PRInt32 minPolicy, maxPolicy;

    if (variant == ssl_variant_stream) {
        effectivePolicy->min = SSL_LIBRARY_VERSION_MIN_SUPPORTED_STREAM;
        effectivePolicy->max = SSL_LIBRARY_VERSION_MAX_SUPPORTED;
    } else {
        effectivePolicy->min = SSL_LIBRARY_VERSION_MIN_SUPPORTED_DATAGRAM;
        effectivePolicy->max = SSL_LIBRARY_VERSION_MAX_SUPPORTED;
    }

    rv = NSS_GetAlgorithmPolicy(SEC_OID_APPLY_SSL_POLICY, &policyFlag);
    if ((rv != SECSuccess) || !(policyFlag & NSS_USE_POLICY_IN_SSL)) {
        /* Policy is not active, report library extents. */
        return SECSuccess;
    }

    rv = NSS_OptionGet(VERSIONS_POLICY_MIN(variant), &minPolicy);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    rv = NSS_OptionGet(VERSIONS_POLICY_MAX(variant), &maxPolicy);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (minPolicy > effectivePolicy->max ||
        maxPolicy < effectivePolicy->min ||
        minPolicy > maxPolicy) {
        return SECFailure;
    }
    effectivePolicy->min = PR_MAX(effectivePolicy->min, minPolicy);
    effectivePolicy->max = PR_MIN(effectivePolicy->max, maxPolicy);
    return SECSuccess;
}

/*
 * Assumes that rangeParam values are within the supported boundaries,
 * but should contain all potentially allowed versions, even if they contain
 * conflicting versions.
 * Will return the overlap, or a NONE range if system policy is invalid.
 */
static SECStatus
ssl3_CreateOverlapWithPolicy(SSLProtocolVariant protocolVariant,
                             SSLVersionRange *input,
                             SSLVersionRange *overlap)
{
    SECStatus rv;
    SSLVersionRange effectivePolicyBoundary;
    SSLVersionRange vrange;

    PORT_Assert(input != NULL);

    rv = ssl3_GetEffectiveVersionPolicy(protocolVariant,
                                        &effectivePolicyBoundary);
    if (rv == SECFailure) {
        /* SECFailure means internal failure or invalid configuration. */
        overlap->min = overlap->max = SSL_LIBRARY_VERSION_NONE;
        return SECFailure;
    }

    vrange.min = PR_MAX(input->min, effectivePolicyBoundary.min);
    vrange.max = PR_MIN(input->max, effectivePolicyBoundary.max);

    if (vrange.max < vrange.min) {
        /* there was no overlap, turn off range altogether */
        overlap->min = overlap->max = SSL_LIBRARY_VERSION_NONE;
        return SECFailure;
    }

    *overlap = vrange;
    return SECSuccess;
}

static PRBool
ssl_VersionIsSupportedByPolicy(SSLProtocolVariant protocolVariant,
                               SSL3ProtocolVersion version)
{
    SECStatus rv;
    SSLVersionRange effectivePolicyBoundary;

    rv = ssl3_GetEffectiveVersionPolicy(protocolVariant,
                                        &effectivePolicyBoundary);
    if (rv == SECFailure) {
        /* SECFailure means internal failure or invalid configuration. */
        return PR_FALSE;
    }
    return version >= effectivePolicyBoundary.min &&
           version <= effectivePolicyBoundary.max;
}

/*
 *  This is called at SSL init time to constrain the existing range based
 *  on user supplied policy.
 */
SECStatus
ssl3_ConstrainRangeByPolicy(void)
{
    /* We ignore failures in ssl3_CreateOverlapWithPolicy. Although an empty
     * overlap disables all connectivity, it's an allowed state.
     */
    ssl3_CreateOverlapWithPolicy(ssl_variant_stream,
                                 VERSIONS_DEFAULTS(ssl_variant_stream),
                                 VERSIONS_DEFAULTS(ssl_variant_stream));
    ssl3_CreateOverlapWithPolicy(ssl_variant_datagram,
                                 VERSIONS_DEFAULTS(ssl_variant_datagram),
                                 VERSIONS_DEFAULTS(ssl_variant_datagram));
    return SECSuccess;
}

PRBool
ssl3_VersionIsSupportedByCode(SSLProtocolVariant protocolVariant,
                              SSL3ProtocolVersion version)
{
    switch (protocolVariant) {
        case ssl_variant_stream:
            return (version >= SSL_LIBRARY_VERSION_MIN_SUPPORTED_STREAM &&
                    version <= SSL_LIBRARY_VERSION_MAX_SUPPORTED);
        case ssl_variant_datagram:
            return (version >= SSL_LIBRARY_VERSION_MIN_SUPPORTED_DATAGRAM &&
                    version <= SSL_LIBRARY_VERSION_MAX_SUPPORTED);
    }

    /* Can't get here */
    PORT_Assert(PR_FALSE);
    return PR_FALSE;
}

PRBool
ssl3_VersionIsSupported(SSLProtocolVariant protocolVariant,
                        SSL3ProtocolVersion version)
{
    if (!ssl_VersionIsSupportedByPolicy(protocolVariant, version)) {
        return PR_FALSE;
    }
    return ssl3_VersionIsSupportedByCode(protocolVariant, version);
}

const SECItem *
SSL_PeerSignedCertTimestamps(PRFileDesc *fd)
{
    sslSocket *ss = ssl_FindSocket(fd);

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_PeerSignedCertTimestamps",
                 SSL_GETPID(), fd));
        return NULL;
    }

    if (!ss->sec.ci.sid) {
        PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
        return NULL;
    }

    return &ss->sec.ci.sid->u.ssl3.signedCertTimestamps;
}

SECStatus
SSL_VersionRangeGetSupported(SSLProtocolVariant protocolVariant,
                             SSLVersionRange *vrange)
{
    SECStatus rv;

    if (!vrange) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    switch (protocolVariant) {
        case ssl_variant_stream:
            vrange->min = SSL_LIBRARY_VERSION_MIN_SUPPORTED_STREAM;
            vrange->max = SSL_LIBRARY_VERSION_MAX_SUPPORTED;
            /* We don't allow SSLv3 and TLSv1.3 together.
             * However, don't check yet, apply the policy first.
             * Because if the effective supported range doesn't use TLS 1.3,
             * then we don't need to increase the minimum. */
            break;
        case ssl_variant_datagram:
            vrange->min = SSL_LIBRARY_VERSION_MIN_SUPPORTED_DATAGRAM;
            vrange->max = SSL_LIBRARY_VERSION_MAX_SUPPORTED;
            break;
        default:
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            return SECFailure;
    }

    rv = ssl3_CreateOverlapWithPolicy(protocolVariant, vrange, vrange);
    if (rv != SECSuccess) {
        /* Library default and policy don't overlap. */
        return rv;
    }

    /* We don't allow SSLv3 and TLSv1.3 together */
    if (vrange->max >= SSL_LIBRARY_VERSION_TLS_1_3) {
        vrange->min = PR_MAX(vrange->min, SSL_LIBRARY_VERSION_TLS_1_0);
    }

    return SECSuccess;
}

SECStatus
SSL_VersionRangeGetDefault(SSLProtocolVariant protocolVariant,
                           SSLVersionRange *vrange)
{
    if ((protocolVariant != ssl_variant_stream &&
         protocolVariant != ssl_variant_datagram) ||
        !vrange) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    *vrange = *VERSIONS_DEFAULTS(protocolVariant);
    return ssl3_CreateOverlapWithPolicy(protocolVariant, vrange, vrange);
}

static PRBool
ssl3_HasConflictingSSLVersions(const SSLVersionRange *vrange)
{
    return (vrange->min <= SSL_LIBRARY_VERSION_3_0 &&
            vrange->max >= SSL_LIBRARY_VERSION_TLS_1_3);
}

static SECStatus
ssl3_CheckRangeValidAndConstrainByPolicy(SSLProtocolVariant protocolVariant,
                                         SSLVersionRange *vrange)
{
    SECStatus rv;

    if (vrange->min > vrange->max ||
        !ssl3_VersionIsSupportedByCode(protocolVariant, vrange->min) ||
        !ssl3_VersionIsSupportedByCode(protocolVariant, vrange->max) ||
        ssl3_HasConflictingSSLVersions(vrange)) {
        PORT_SetError(SSL_ERROR_INVALID_VERSION_RANGE);
        return SECFailure;
    }

    /* Try to adjust the received range using our policy.
     * If there's overlap, we'll use the (possibly reduced) range.
     * If there isn't overlap, it's failure. */

    rv = ssl3_CreateOverlapWithPolicy(protocolVariant, vrange, vrange);
    if (rv != SECSuccess) {
        return rv;
    }

    /* We don't allow SSLv3 and TLSv1.3 together */
    if (vrange->max >= SSL_LIBRARY_VERSION_TLS_1_3) {
        vrange->min = PR_MAX(vrange->min, SSL_LIBRARY_VERSION_TLS_1_0);
    }

    return SECSuccess;
}

SECStatus
SSL_VersionRangeSetDefault(SSLProtocolVariant protocolVariant,
                           const SSLVersionRange *vrange)
{
    SSLVersionRange constrainedRange;
    SECStatus rv;

    if (!vrange) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    constrainedRange = *vrange;
    rv = ssl3_CheckRangeValidAndConstrainByPolicy(protocolVariant,
                                                  &constrainedRange);
    if (rv != SECSuccess)
        return rv;

    *VERSIONS_DEFAULTS(protocolVariant) = constrainedRange;
    return SECSuccess;
}

SECStatus
SSL_VersionRangeGet(PRFileDesc *fd, SSLVersionRange *vrange)
{
    sslSocket *ss = ssl_FindSocket(fd);

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_VersionRangeGet",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    if (!vrange) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);

    *vrange = ss->vrange;

    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);

    return ssl3_CreateOverlapWithPolicy(ss->protocolVariant, vrange, vrange);
}

SECStatus
SSL_VersionRangeSet(PRFileDesc *fd, const SSLVersionRange *vrange)
{
    SSLVersionRange constrainedRange;
    sslSocket *ss;
    SECStatus rv;

    if (!vrange) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_VersionRangeSet",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    constrainedRange = *vrange;
    rv = ssl3_CheckRangeValidAndConstrainByPolicy(ss->protocolVariant,
                                                  &constrainedRange);
    if (rv != SECSuccess)
        return rv;

    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);

    if (ss->ssl3.downgradeCheckVersion &&
        ss->vrange.max > ss->ssl3.downgradeCheckVersion) {
        PORT_SetError(SSL_ERROR_INVALID_VERSION_RANGE);
        ssl_ReleaseSSL3HandshakeLock(ss);
        ssl_Release1stHandshakeLock(ss);
        return SECFailure;
    }

    ss->vrange = constrainedRange;

    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);

    return SECSuccess;
}

SECStatus
SSL_SetDowngradeCheckVersion(PRFileDesc *fd, PRUint16 version)
{
    sslSocket *ss = ssl_FindSocket(fd);
    SECStatus rv = SECFailure;

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SetDowngradeCheckVersion",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    if (version && !ssl3_VersionIsSupported(ss->protocolVariant, version)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);

    if (version && version < ss->vrange.max) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        goto loser;
    }
    ss->ssl3.downgradeCheckVersion = version;
    rv = SECSuccess;

loser:
    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);

    return rv;
}

const SECItemArray *
SSL_PeerStapledOCSPResponses(PRFileDesc *fd)
{
    sslSocket *ss = ssl_FindSocket(fd);

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_PeerStapledOCSPResponses",
                 SSL_GETPID(), fd));
        return NULL;
    }

    if (!ss->sec.ci.sid) {
        PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
        return NULL;
    }

    return &ss->sec.ci.sid->peerCertStatus;
}

/************************************************************************/
/* The following functions are the TOP LEVEL SSL functions.
** They all get called through the NSPRIOMethods table below.
*/

static PRFileDesc *PR_CALLBACK
ssl_Accept(PRFileDesc *fd, PRNetAddr *sockaddr, PRIntervalTime timeout)
{
    sslSocket *ss;
    sslSocket *ns = NULL;
    PRFileDesc *newfd = NULL;
    PRFileDesc *osfd;
    PRStatus status;

    ss = ssl_GetPrivate(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in accept", SSL_GETPID(), fd));
        return NULL;
    }

    /* IF this is a listen socket, there shouldn't be any I/O going on */
    SSL_LOCK_READER(ss);
    SSL_LOCK_WRITER(ss);
    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);

    ss->cTimeout = timeout;

    osfd = ss->fd->lower;

    /* First accept connection */
    newfd = osfd->methods->accept(osfd, sockaddr, timeout);
    if (newfd == NULL) {
        SSL_DBG(("%d: SSL[%d]: accept failed, errno=%d",
                 SSL_GETPID(), ss->fd, PORT_GetError()));
    } else {
        /* Create ssl module */
        ns = ssl_DupSocket(ss);
    }

    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);
    SSL_UNLOCK_WRITER(ss);
    SSL_UNLOCK_READER(ss); /* ss isn't used below here. */

    if (ns == NULL)
        goto loser;

    /* push ssl module onto the new socket */
    status = ssl_PushIOLayer(ns, newfd, PR_TOP_IO_LAYER);
    if (status != PR_SUCCESS)
        goto loser;

    /* Now start server connection handshake with client.
    ** Don't need locks here because nobody else has a reference to ns yet.
    */
    if (ns->opt.useSecurity) {
        if (ns->opt.handshakeAsClient) {
            ns->handshake = ssl_BeginClientHandshake;
            ss->handshaking = sslHandshakingAsClient;
        } else {
            ns->handshake = ssl_BeginServerHandshake;
            ss->handshaking = sslHandshakingAsServer;
        }
    }
    ns->TCPconnected = 1;
    return newfd;

loser:
    if (ns != NULL)
        ssl_FreeSocket(ns);
    if (newfd != NULL)
        PR_Close(newfd);
    return NULL;
}

static PRStatus PR_CALLBACK
ssl_Connect(PRFileDesc *fd, const PRNetAddr *sockaddr, PRIntervalTime timeout)
{
    sslSocket *ss;
    PRStatus rv;

    ss = ssl_GetPrivate(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in connect", SSL_GETPID(), fd));
        return PR_FAILURE;
    }

    /* IF this is a listen socket, there shouldn't be any I/O going on */
    SSL_LOCK_READER(ss);
    SSL_LOCK_WRITER(ss);

    ss->cTimeout = timeout;
    rv = (PRStatus)(*ss->ops->connect)(ss, sockaddr);

    SSL_UNLOCK_WRITER(ss);
    SSL_UNLOCK_READER(ss);

    return rv;
}

static PRStatus PR_CALLBACK
ssl_Bind(PRFileDesc *fd, const PRNetAddr *addr)
{
    sslSocket *ss = ssl_GetPrivate(fd);
    PRStatus rv;

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in bind", SSL_GETPID(), fd));
        return PR_FAILURE;
    }
    SSL_LOCK_READER(ss);
    SSL_LOCK_WRITER(ss);

    rv = (PRStatus)(*ss->ops->bind)(ss, addr);

    SSL_UNLOCK_WRITER(ss);
    SSL_UNLOCK_READER(ss);
    return rv;
}

static PRStatus PR_CALLBACK
ssl_Listen(PRFileDesc *fd, PRIntn backlog)
{
    sslSocket *ss = ssl_GetPrivate(fd);
    PRStatus rv;

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in listen", SSL_GETPID(), fd));
        return PR_FAILURE;
    }
    SSL_LOCK_READER(ss);
    SSL_LOCK_WRITER(ss);

    rv = (PRStatus)(*ss->ops->listen)(ss, backlog);

    SSL_UNLOCK_WRITER(ss);
    SSL_UNLOCK_READER(ss);
    return rv;
}

static PRStatus PR_CALLBACK
ssl_Shutdown(PRFileDesc *fd, PRIntn how)
{
    sslSocket *ss = ssl_GetPrivate(fd);
    PRStatus rv;

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in shutdown", SSL_GETPID(), fd));
        return PR_FAILURE;
    }
    if (how == PR_SHUTDOWN_RCV || how == PR_SHUTDOWN_BOTH) {
        SSL_LOCK_READER(ss);
    }
    if (how == PR_SHUTDOWN_SEND || how == PR_SHUTDOWN_BOTH) {
        SSL_LOCK_WRITER(ss);
    }

    rv = (PRStatus)(*ss->ops->shutdown)(ss, how);

    if (how == PR_SHUTDOWN_SEND || how == PR_SHUTDOWN_BOTH) {
        SSL_UNLOCK_WRITER(ss);
    }
    if (how == PR_SHUTDOWN_RCV || how == PR_SHUTDOWN_BOTH) {
        SSL_UNLOCK_READER(ss);
    }
    return rv;
}

static PRStatus PR_CALLBACK
ssl_Close(PRFileDesc *fd)
{
    sslSocket *ss;
    PRStatus rv;

    ss = ssl_GetPrivate(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in close", SSL_GETPID(), fd));
        return PR_FAILURE;
    }

    /* There must not be any I/O going on */
    SSL_LOCK_READER(ss);
    SSL_LOCK_WRITER(ss);

    /* By the time this function returns,
    ** ss is an invalid pointer, and the locks to which it points have
    ** been unlocked and freed.  So, this is the ONE PLACE in all of SSL
    ** where the LOCK calls and the corresponding UNLOCK calls are not in
    ** the same function scope.  The unlock calls are in ssl_FreeSocket().
    */
    rv = (PRStatus)(*ss->ops->close)(ss);

    return rv;
}

static int PR_CALLBACK
ssl_Recv(PRFileDesc *fd, void *buf, PRInt32 len, PRIntn flags,
         PRIntervalTime timeout)
{
    sslSocket *ss;
    int rv;

    ss = ssl_GetPrivate(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in recv", SSL_GETPID(), fd));
        return SECFailure;
    }
    SSL_LOCK_READER(ss);
    ss->rTimeout = timeout;
    if (!ss->opt.fdx)
        ss->wTimeout = timeout;
    rv = (*ss->ops->recv)(ss, (unsigned char *)buf, len, flags);
    SSL_UNLOCK_READER(ss);
    return rv;
}

static int PR_CALLBACK
ssl_Send(PRFileDesc *fd, const void *buf, PRInt32 len, PRIntn flags,
         PRIntervalTime timeout)
{
    sslSocket *ss;
    int rv;

    ss = ssl_GetPrivate(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in send", SSL_GETPID(), fd));
        return SECFailure;
    }
    SSL_LOCK_WRITER(ss);
    ss->wTimeout = timeout;
    if (!ss->opt.fdx)
        ss->rTimeout = timeout;
    rv = (*ss->ops->send)(ss, (const unsigned char *)buf, len, flags);
    SSL_UNLOCK_WRITER(ss);
    return rv;
}

static int PR_CALLBACK
ssl_Read(PRFileDesc *fd, void *buf, PRInt32 len)
{
    sslSocket *ss;
    int rv;

    ss = ssl_GetPrivate(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in read", SSL_GETPID(), fd));
        return SECFailure;
    }
    SSL_LOCK_READER(ss);
    ss->rTimeout = PR_INTERVAL_NO_TIMEOUT;
    if (!ss->opt.fdx)
        ss->wTimeout = PR_INTERVAL_NO_TIMEOUT;
    rv = (*ss->ops->read)(ss, (unsigned char *)buf, len);
    SSL_UNLOCK_READER(ss);
    return rv;
}

static int PR_CALLBACK
ssl_Write(PRFileDesc *fd, const void *buf, PRInt32 len)
{
    sslSocket *ss;
    int rv;

    ss = ssl_GetPrivate(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in write", SSL_GETPID(), fd));
        return SECFailure;
    }
    SSL_LOCK_WRITER(ss);
    ss->wTimeout = PR_INTERVAL_NO_TIMEOUT;
    if (!ss->opt.fdx)
        ss->rTimeout = PR_INTERVAL_NO_TIMEOUT;
    rv = (*ss->ops->write)(ss, (const unsigned char *)buf, len);
    SSL_UNLOCK_WRITER(ss);
    return rv;
}

static PRStatus PR_CALLBACK
ssl_GetPeerName(PRFileDesc *fd, PRNetAddr *addr)
{
    sslSocket *ss;

    ss = ssl_GetPrivate(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in getpeername", SSL_GETPID(), fd));
        return PR_FAILURE;
    }
    return (PRStatus)(*ss->ops->getpeername)(ss, addr);
}

/*
 */
SECStatus
ssl_GetPeerInfo(sslSocket *ss)
{
    PRFileDesc *osfd;
    int rv;
    PRNetAddr sin;

    osfd = ss->fd->lower;

    PORT_Memset(&sin, 0, sizeof(sin));
    rv = osfd->methods->getpeername(osfd, &sin);
    if (rv < 0) {
        return SECFailure;
    }
    ss->TCPconnected = 1;
    if (sin.inet.family == PR_AF_INET) {
        PR_ConvertIPv4AddrToIPv6(sin.inet.ip, &ss->sec.ci.peer);
        ss->sec.ci.port = sin.inet.port;
    } else if (sin.ipv6.family == PR_AF_INET6) {
        ss->sec.ci.peer = sin.ipv6.ip;
        ss->sec.ci.port = sin.ipv6.port;
    } else {
        PORT_SetError(PR_ADDRESS_NOT_SUPPORTED_ERROR);
        return SECFailure;
    }
    return SECSuccess;
}

static PRStatus PR_CALLBACK
ssl_GetSockName(PRFileDesc *fd, PRNetAddr *name)
{
    sslSocket *ss;

    ss = ssl_GetPrivate(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in getsockname", SSL_GETPID(), fd));
        return PR_FAILURE;
    }
    return (PRStatus)(*ss->ops->getsockname)(ss, name);
}

SECStatus
SSL_SetSockPeerID(PRFileDesc *fd, const char *peerID)
{
    sslSocket *ss;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SetSockPeerID",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    if (ss->peerID) {
        PORT_Free(ss->peerID);
        ss->peerID = NULL;
    }
    if (peerID)
        ss->peerID = PORT_Strdup(peerID);
    return (ss->peerID || !peerID) ? SECSuccess : SECFailure;
}

#define PR_POLL_RW (PR_POLL_WRITE | PR_POLL_READ)

static PRInt16 PR_CALLBACK
ssl_Poll(PRFileDesc *fd, PRInt16 how_flags, PRInt16 *p_out_flags)
{
    sslSocket *ss;
    PRInt16 new_flags = how_flags; /* should select on these flags. */
    PRNetAddr addr;

    *p_out_flags = 0;
    ss = ssl_GetPrivate(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_Poll",
                 SSL_GETPID(), fd));
        return 0; /* don't poll on this socket */
    }

    if (ss->opt.useSecurity &&
        ss->handshaking != sslHandshakingUndetermined &&
        !ss->firstHsDone &&
        (how_flags & PR_POLL_RW)) {
        if (!ss->TCPconnected) {
            ss->TCPconnected = (PR_SUCCESS == ssl_DefGetpeername(ss, &addr));
        }
        /* If it's not connected, then presumably the application is polling
        ** on read or write appropriately, so don't change it.
        */
        if (ss->TCPconnected) {
            if (!ss->handshakeBegun) {
                /* If the handshake has not begun, poll on read or write
                ** based on the local application's role in the handshake,
                ** not based on what the application requested.
                */
                new_flags &= ~PR_POLL_RW;
                if (ss->handshaking == sslHandshakingAsClient) {
                    new_flags |= PR_POLL_WRITE;
                } else { /* handshaking as server */
                    new_flags |= PR_POLL_READ;
                }
            } else if (ss->lastWriteBlocked) {
                /* First handshake is in progress */
                if (new_flags & PR_POLL_READ) {
                    /* The caller is waiting for data to be received,
                    ** but the initial handshake is blocked on write, or the
                    ** client's first handshake record has not been written.
                    ** The code should select on write, not read.
                    */
                    new_flags &= ~PR_POLL_READ; /* don't select on read. */
                    new_flags |= PR_POLL_WRITE; /* do    select on write. */
                }
            } else if (new_flags & PR_POLL_WRITE) {
                /* The caller is trying to write, but the handshake is
                ** blocked waiting for data to read, and the first
                ** handshake has been sent.  So do NOT to poll on write
                ** unless we did false start or we are doing 0-RTT.
                */
                if (!(ss->ssl3.hs.canFalseStart ||
                      ss->ssl3.hs.zeroRttState == ssl_0rtt_sent ||
                      ss->ssl3.hs.zeroRttState == ssl_0rtt_accepted)) {
                    new_flags &= ~PR_POLL_WRITE; /* don't select on write. */
                }
                new_flags |= PR_POLL_READ; /* do    select on read. */
            }
        }
    } else if ((new_flags & PR_POLL_READ) && (SSL_DataPending(fd) > 0)) {
        *p_out_flags = PR_POLL_READ; /* it's ready already. */
        return new_flags;
    } else if ((ss->lastWriteBlocked) && (how_flags & PR_POLL_READ) &&
               (ss->pendingBuf.len != 0)) { /* write data waiting to be sent */
        new_flags |= PR_POLL_WRITE;         /* also select on write. */
    }

    if (ss->ssl3.hs.restartTarget != NULL) {
        /* Read and write will block until the asynchronous callback completes
         * (e.g. until SSL_AuthCertificateComplete is called), so don't tell
         * the caller to poll the socket unless there is pending write data.
         */
        if (ss->lastWriteBlocked && ss->pendingBuf.len != 0) {
            /* Ignore any newly-received data on the socket, but do wait for
             * the socket to become writable again. Here, it is OK for an error
             * to be detected, because our logic for sending pending write data
             * will allow us to report the error to the caller without the risk
             * of the application spinning.
             */
            new_flags &= (PR_POLL_WRITE | PR_POLL_EXCEPT);
        } else {
            /* Unfortunately, clearing new_flags will make it impossible for
             * the application to detect errors that it would otherwise be
             * able to detect with PR_POLL_EXCEPT, until the asynchronous
             * callback completes. However, we must clear all the flags to
             * prevent the application from spinning (alternating between
             * calling PR_Poll that would return PR_POLL_EXCEPT, and send/recv
             * which won't actually report the I/O error while we are waiting
             * for the asynchronous callback to complete).
             */
            new_flags = 0;
        }
    }

    SSL_TRC(20, ("%d: SSL[%d]: ssl_Poll flags %x -> %x",
                 SSL_GETPID(), fd, how_flags, new_flags));

    if (new_flags && (fd->lower->methods->poll != NULL)) {
        PRInt16 lower_out_flags = 0;
        PRInt16 lower_new_flags;
        lower_new_flags = fd->lower->methods->poll(fd->lower, new_flags,
                                                   &lower_out_flags);
        if ((lower_new_flags & lower_out_flags) && (how_flags != new_flags)) {
            PRInt16 out_flags = lower_out_flags & ~PR_POLL_RW;
            if (lower_out_flags & PR_POLL_READ)
                out_flags |= PR_POLL_WRITE;
            if (lower_out_flags & PR_POLL_WRITE)
                out_flags |= PR_POLL_READ;
            *p_out_flags = out_flags;
            new_flags = how_flags;
        } else {
            *p_out_flags = lower_out_flags;
            new_flags = lower_new_flags;
        }
    }

    return new_flags;
}

static PRInt32 PR_CALLBACK
ssl_TransmitFile(PRFileDesc *sd, PRFileDesc *fd,
                 const void *headers, PRInt32 hlen,
                 PRTransmitFileFlags flags, PRIntervalTime timeout)
{
    PRSendFileData sfd;

    sfd.fd = fd;
    sfd.file_offset = 0;
    sfd.file_nbytes = 0;
    sfd.header = headers;
    sfd.hlen = hlen;
    sfd.trailer = NULL;
    sfd.tlen = 0;

    return sd->methods->sendfile(sd, &sfd, flags, timeout);
}

PRBool
ssl_FdIsBlocking(PRFileDesc *fd)
{
    PRSocketOptionData opt;
    PRStatus status;

    opt.option = PR_SockOpt_Nonblocking;
    opt.value.non_blocking = PR_FALSE;
    status = PR_GetSocketOption(fd, &opt);
    if (status != PR_SUCCESS)
        return PR_FALSE;
    return (PRBool)!opt.value.non_blocking;
}

PRBool
ssl_SocketIsBlocking(sslSocket *ss)
{
    return ssl_FdIsBlocking(ss->fd);
}

PRInt32 sslFirstBufSize = 8 * 1024;
PRInt32 sslCopyLimit = 1024;

static PRInt32 PR_CALLBACK
ssl_WriteV(PRFileDesc *fd, const PRIOVec *iov, PRInt32 vectors,
           PRIntervalTime timeout)
{
    PRInt32 i;
    PRInt32 bufLen;
    PRInt32 left;
    PRInt32 rv;
    PRInt32 sent = 0;
    const PRInt32 first_len = sslFirstBufSize;
    const PRInt32 limit = sslCopyLimit;
    PRBool blocking;
    PRIOVec myIov;
    char buf[MAX_FRAGMENT_LENGTH];

    if (vectors < 0) {
        PORT_SetError(PR_INVALID_ARGUMENT_ERROR);
        return -1;
    }
    if (vectors > PR_MAX_IOVECTOR_SIZE) {
        PORT_SetError(PR_BUFFER_OVERFLOW_ERROR);
        return -1;
    }
    for (i = 0; i < vectors; i++) {
        if (iov[i].iov_len < 0) {
            PORT_SetError(PR_INVALID_ARGUMENT_ERROR);
            return -1;
        }
    }
    blocking = ssl_FdIsBlocking(fd);

#define K16 ((int)sizeof(buf))
#define KILL_VECTORS                   \
    while (vectors && !iov->iov_len) { \
        ++iov;                         \
        --vectors;                     \
    }
#define GET_VECTOR      \
    do {                \
        myIov = *iov++; \
        --vectors;      \
        KILL_VECTORS    \
    } while (0)
#define HANDLE_ERR(rv, len)                                    \
    if (rv != len) {                                           \
        if (rv < 0) {                                          \
            if (!blocking &&                                   \
                (PR_GetError() == PR_WOULD_BLOCK_ERROR) &&     \
                (sent > 0)) {                                  \
                return sent;                                   \
            } else {                                           \
                return -1;                                     \
            }                                                  \
        }                                                      \
        /* Only a nonblocking socket can have partial sends */ \
        PR_ASSERT(!blocking);                                  \
        return sent + rv;                                      \
    }
#define SEND(bfr, len)                           \
    do {                                         \
        rv = ssl_Send(fd, bfr, len, 0, timeout); \
        HANDLE_ERR(rv, len)                      \
        sent += len;                             \
    } while (0)

    /* Make sure the first write is at least 8 KB, if possible. */
    KILL_VECTORS
    if (!vectors)
        return ssl_Send(fd, 0, 0, 0, timeout);
    GET_VECTOR;
    if (!vectors) {
        return ssl_Send(fd, myIov.iov_base, myIov.iov_len, 0, timeout);
    }
    if (myIov.iov_len < first_len) {
        PORT_Memcpy(buf, myIov.iov_base, myIov.iov_len);
        bufLen = myIov.iov_len;
        left = first_len - bufLen;
        while (vectors && left) {
            int toCopy;
            GET_VECTOR;
            toCopy = PR_MIN(left, myIov.iov_len);
            PORT_Memcpy(buf + bufLen, myIov.iov_base, toCopy);
            bufLen += toCopy;
            left -= toCopy;
            myIov.iov_base += toCopy;
            myIov.iov_len -= toCopy;
        }
        SEND(buf, bufLen);
    }

    while (vectors || myIov.iov_len) {
        PRInt32 addLen;
        if (!myIov.iov_len) {
            GET_VECTOR;
        }
        while (myIov.iov_len >= K16) {
            SEND(myIov.iov_base, K16);
            myIov.iov_base += K16;
            myIov.iov_len -= K16;
        }
        if (!myIov.iov_len)
            continue;

        if (!vectors || myIov.iov_len > limit) {
            addLen = 0;
        } else if ((addLen = iov->iov_len % K16) + myIov.iov_len <= limit) {
            /* Addlen is already computed. */;
        } else if (vectors > 1 &&
                   iov[1].iov_len % K16 + addLen + myIov.iov_len <= 2 * limit) {
            addLen = limit - myIov.iov_len;
        } else
            addLen = 0;

        if (!addLen) {
            SEND(myIov.iov_base, myIov.iov_len);
            myIov.iov_len = 0;
            continue;
        }
        PORT_Memcpy(buf, myIov.iov_base, myIov.iov_len);
        bufLen = myIov.iov_len;
        do {
            GET_VECTOR;
            PORT_Memcpy(buf + bufLen, myIov.iov_base, addLen);
            myIov.iov_base += addLen;
            myIov.iov_len -= addLen;
            bufLen += addLen;

            left = PR_MIN(limit, K16 - bufLen);
            if (!vectors             /* no more left */
                || myIov.iov_len > 0 /* we didn't use that one all up */
                || bufLen >= K16 /* it's full. */) {
                addLen = 0;
            } else if ((addLen = iov->iov_len % K16) <= left) {
                /* Addlen is already computed. */;
            } else if (vectors > 1 &&
                       iov[1].iov_len % K16 + addLen <= left + limit) {
                addLen = left;
            } else
                addLen = 0;

        } while (addLen);
        SEND(buf, bufLen);
    }
    return sent;
}

/*
 * These functions aren't implemented.
 */

static PRInt32 PR_CALLBACK
ssl_Available(PRFileDesc *fd)
{
    PORT_Assert(0);
    PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
    return SECFailure;
}

static PRInt64 PR_CALLBACK
ssl_Available64(PRFileDesc *fd)
{
    PRInt64 res;

    PORT_Assert(0);
    PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
    LL_I2L(res, -1L);
    return res;
}

static PRStatus PR_CALLBACK
ssl_FSync(PRFileDesc *fd)
{
    PORT_Assert(0);
    PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
    return PR_FAILURE;
}

static PRInt32 PR_CALLBACK
ssl_Seek(PRFileDesc *fd, PRInt32 offset, PRSeekWhence how)
{
    PORT_Assert(0);
    PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
    return SECFailure;
}

static PRInt64 PR_CALLBACK
ssl_Seek64(PRFileDesc *fd, PRInt64 offset, PRSeekWhence how)
{
    PRInt64 res;

    PORT_Assert(0);
    PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
    LL_I2L(res, -1L);
    return res;
}

static PRStatus PR_CALLBACK
ssl_FileInfo(PRFileDesc *fd, PRFileInfo *info)
{
    PORT_Assert(0);
    PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
    return PR_FAILURE;
}

static PRStatus PR_CALLBACK
ssl_FileInfo64(PRFileDesc *fd, PRFileInfo64 *info)
{
    PORT_Assert(0);
    PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
    return PR_FAILURE;
}

static PRInt32 PR_CALLBACK
ssl_RecvFrom(PRFileDesc *fd, void *buf, PRInt32 amount, PRIntn flags,
             PRNetAddr *addr, PRIntervalTime timeout)
{
    PORT_Assert(0);
    PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
    return SECFailure;
}

static PRInt32 PR_CALLBACK
ssl_SendTo(PRFileDesc *fd, const void *buf, PRInt32 amount, PRIntn flags,
           const PRNetAddr *addr, PRIntervalTime timeout)
{
    PORT_Assert(0);
    PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
    return SECFailure;
}

static const PRIOMethods ssl_methods = {
    PR_DESC_LAYERED,
    ssl_Close,            /* close        */
    ssl_Read,             /* read         */
    ssl_Write,            /* write        */
    ssl_Available,        /* available    */
    ssl_Available64,      /* available64  */
    ssl_FSync,            /* fsync        */
    ssl_Seek,             /* seek         */
    ssl_Seek64,           /* seek64       */
    ssl_FileInfo,         /* fileInfo     */
    ssl_FileInfo64,       /* fileInfo64   */
    ssl_WriteV,           /* writev       */
    ssl_Connect,          /* connect      */
    ssl_Accept,           /* accept       */
    ssl_Bind,             /* bind         */
    ssl_Listen,           /* listen       */
    ssl_Shutdown,         /* shutdown     */
    ssl_Recv,             /* recv         */
    ssl_Send,             /* send         */
    ssl_RecvFrom,         /* recvfrom     */
    ssl_SendTo,           /* sendto       */
    ssl_Poll,             /* poll         */
    PR_EmulateAcceptRead, /* acceptread   */
    ssl_TransmitFile,     /* transmitfile */
    ssl_GetSockName,      /* getsockname  */
    ssl_GetPeerName,      /* getpeername  */
    NULL,                 /* getsockopt   OBSOLETE */
    NULL,                 /* setsockopt   OBSOLETE */
    NULL,                 /* getsocketoption   */
    NULL,                 /* setsocketoption   */
    PR_EmulateSendFile,   /* Send a (partial) file with header/trailer*/
    NULL,                 /* reserved for future use */
    NULL,                 /* reserved for future use */
    NULL,                 /* reserved for future use */
    NULL,                 /* reserved for future use */
    NULL                  /* reserved for future use */
};

static PRIOMethods combined_methods;

static void
ssl_SetupIOMethods(void)
{
    PRIOMethods *new_methods = &combined_methods;
    const PRIOMethods *nspr_methods = PR_GetDefaultIOMethods();
    const PRIOMethods *my_methods = &ssl_methods;

    *new_methods = *nspr_methods;

    new_methods->file_type = my_methods->file_type;
    new_methods->close = my_methods->close;
    new_methods->read = my_methods->read;
    new_methods->write = my_methods->write;
    new_methods->available = my_methods->available;
    new_methods->available64 = my_methods->available64;
    new_methods->fsync = my_methods->fsync;
    new_methods->seek = my_methods->seek;
    new_methods->seek64 = my_methods->seek64;
    new_methods->fileInfo = my_methods->fileInfo;
    new_methods->fileInfo64 = my_methods->fileInfo64;
    new_methods->writev = my_methods->writev;
    new_methods->connect = my_methods->connect;
    new_methods->accept = my_methods->accept;
    new_methods->bind = my_methods->bind;
    new_methods->listen = my_methods->listen;
    new_methods->shutdown = my_methods->shutdown;
    new_methods->recv = my_methods->recv;
    new_methods->send = my_methods->send;
    new_methods->recvfrom = my_methods->recvfrom;
    new_methods->sendto = my_methods->sendto;
    new_methods->poll = my_methods->poll;
    new_methods->acceptread = my_methods->acceptread;
    new_methods->transmitfile = my_methods->transmitfile;
    new_methods->getsockname = my_methods->getsockname;
    new_methods->getpeername = my_methods->getpeername;
    /*  new_methods->getsocketoption   = my_methods->getsocketoption;       */
    /*  new_methods->setsocketoption   = my_methods->setsocketoption;       */
    new_methods->sendfile = my_methods->sendfile;
}

static PRCallOnceType initIoLayerOnce;

static PRStatus
ssl_InitIOLayer(void)
{
    ssl_layer_id = PR_GetUniqueIdentity("SSL");
    ssl_SetupIOMethods();
    return PR_SUCCESS;
}

static PRStatus
ssl_PushIOLayer(sslSocket *ns, PRFileDesc *stack, PRDescIdentity id)
{
    PRFileDesc *layer = NULL;
    PRStatus status;

    status = PR_CallOnce(&initIoLayerOnce, &ssl_InitIOLayer);
    if (status != PR_SUCCESS) {
        goto loser;
    }
    if (ns == NULL) {
        goto loser;
    }
    layer = PR_CreateIOLayerStub(ssl_layer_id, &combined_methods);
    if (layer == NULL)
        goto loser;
    layer->secret = (PRFilePrivate *)ns;

    /* Here, "stack" points to the PRFileDesc on the top of the stack.
    ** "layer" points to a new FD that is to be inserted into the stack.
    ** If layer is being pushed onto the top of the stack, then
    ** PR_PushIOLayer switches the contents of stack and layer, and then
    ** puts stack on top of layer, so that after it is done, the top of
    ** stack is the same "stack" as it was before, and layer is now the
    ** FD for the former top of stack.
    ** After this call, stack always points to the top PRFD on the stack.
    ** If this function fails, the contents of stack and layer are as
    ** they were before the call.
    */
    status = PR_PushIOLayer(stack, id, layer);
    if (status != PR_SUCCESS)
        goto loser;

    ns->fd = (id == PR_TOP_IO_LAYER) ? stack : layer;
    return PR_SUCCESS;

loser:
    if (layer) {
        layer->dtor(layer); /* free layer */
    }
    return PR_FAILURE;
}

/* if this fails, caller must destroy socket. */
static SECStatus
ssl_MakeLocks(sslSocket *ss)
{
    ss->firstHandshakeLock = PZ_NewMonitor(nssILockSSL);
    if (!ss->firstHandshakeLock)
        goto loser;
    ss->ssl3HandshakeLock = PZ_NewMonitor(nssILockSSL);
    if (!ss->ssl3HandshakeLock)
        goto loser;
    ss->specLock = NSSRWLock_New(SSL_LOCK_RANK_SPEC, NULL);
    if (!ss->specLock)
        goto loser;
    ss->recvBufLock = PZ_NewMonitor(nssILockSSL);
    if (!ss->recvBufLock)
        goto loser;
    ss->xmitBufLock = PZ_NewMonitor(nssILockSSL);
    if (!ss->xmitBufLock)
        goto loser;
    ss->writerThread = NULL;
    if (ssl_lock_readers) {
        ss->recvLock = PZ_NewLock(nssILockSSL);
        if (!ss->recvLock)
            goto loser;
        ss->sendLock = PZ_NewLock(nssILockSSL);
        if (!ss->sendLock)
            goto loser;
    }
    return SECSuccess;
loser:
    ssl_DestroyLocks(ss);
    return SECFailure;
}

#if defined(XP_UNIX) || defined(XP_WIN32)
#define NSS_HAVE_GETENV 1
#endif

#define LOWER(x) (x | 0x20) /* cheap ToLower function ignores LOCALE */

static PRStatus
ssl_SetDefaultsFromEnvironmentCallOnce(void)
{
#if defined(NSS_HAVE_GETENV)
    char *ev;
#ifdef DEBUG
    ssl_trace_iob = NULL;
    ev = PR_GetEnvSecure("SSLDEBUGFILE");
    if (ev && ev[0]) {
        ssl_trace_iob = fopen(ev, "w");
    }
    if (!ssl_trace_iob) {
        ssl_trace_iob = stderr;
    }
#ifdef TRACE
    ev = PR_GetEnvSecure("SSLTRACE");
    if (ev && ev[0]) {
        ssl_trace = atoi(ev);
        SSL_TRACE(("SSL: tracing set to %d", ssl_trace));
    }
#endif /* TRACE */
    ev = PR_GetEnvSecure("SSLDEBUG");
    if (ev && ev[0]) {
        ssl_debug = atoi(ev);
        SSL_TRACE(("SSL: debugging set to %d", ssl_debug));
    }
#endif /* DEBUG */
#ifdef NSS_ALLOW_SSLKEYLOGFILE
    ssl_keylog_iob = NULL;
    ev = PR_GetEnvSecure("SSLKEYLOGFILE");
    if (ev && ev[0]) {
        ssl_keylog_iob = fopen(ev, "a");
        if (!ssl_keylog_iob) {
            SSL_TRACE(("SSL: failed to open key log file"));
        } else {
            if (ftell(ssl_keylog_iob) == 0) {
                fputs("# SSL/TLS secrets log file, generated by NSS\n",
                      ssl_keylog_iob);
            }
            SSL_TRACE(("SSL: logging SSL/TLS secrets to %s", ev));
            ssl_keylog_lock = PR_NewLock();
            if (!ssl_keylog_lock) {
                SSL_TRACE(("SSL: failed to create key log lock"));
                fclose(ssl_keylog_iob);
                ssl_keylog_iob = NULL;
            }
        }
    }
#endif
    ev = PR_GetEnvSecure("SSLFORCELOCKS");
    if (ev && ev[0] == '1') {
        ssl_force_locks = PR_TRUE;
        ssl_defaults.noLocks = 0;
        SSL_TRACE(("SSL: force_locks set to %d", ssl_force_locks));
    }
    ev = PR_GetEnvSecure("NSS_SSL_ENABLE_RENEGOTIATION");
    if (ev) {
        if (ev[0] == '1' || LOWER(ev[0]) == 'u')
            ssl_defaults.enableRenegotiation = SSL_RENEGOTIATE_UNRESTRICTED;
        else if (ev[0] == '0' || LOWER(ev[0]) == 'n')
            ssl_defaults.enableRenegotiation = SSL_RENEGOTIATE_NEVER;
        else if (ev[0] == '2' || LOWER(ev[0]) == 'r')
            ssl_defaults.enableRenegotiation = SSL_RENEGOTIATE_REQUIRES_XTN;
        else if (ev[0] == '3' || LOWER(ev[0]) == 't')
            ssl_defaults.enableRenegotiation = SSL_RENEGOTIATE_TRANSITIONAL;
        SSL_TRACE(("SSL: enableRenegotiation set to %d",
                   ssl_defaults.enableRenegotiation));
    }
    ev = PR_GetEnvSecure("NSS_SSL_REQUIRE_SAFE_NEGOTIATION");
    if (ev && ev[0] == '1') {
        ssl_defaults.requireSafeNegotiation = PR_TRUE;
        SSL_TRACE(("SSL: requireSafeNegotiation set to %d",
                   PR_TRUE));
    }
    ev = PR_GetEnvSecure("NSS_SSL_CBC_RANDOM_IV");
    if (ev && ev[0] == '0') {
        ssl_defaults.cbcRandomIV = PR_FALSE;
        SSL_TRACE(("SSL: cbcRandomIV set to 0"));
    }
#endif /* NSS_HAVE_GETENV */
    return PR_SUCCESS;
}

static void
ssl_SetDefaultsFromEnvironment(void)
{
    PR_CallOnce(&ssl_setDefaultsFromEnvironment, ssl_SetDefaultsFromEnvironmentCallOnce);
}

const sslNamedGroupDef *
ssl_LookupNamedGroup(SSLNamedGroup group)
{
    unsigned int i;

    for (i = 0; i < SSL_NAMED_GROUP_COUNT; ++i) {
        if (ssl_named_groups[i].name == group) {
            return &ssl_named_groups[i];
        }
    }
    return NULL;
}

PRBool
ssl_NamedGroupEnabled(const sslSocket *ss, const sslNamedGroupDef *groupDef)
{
    unsigned int i;

    if (!groupDef) {
        return PR_FALSE;
    }

    for (i = 0; i < SSL_NAMED_GROUP_COUNT; ++i) {
        if (ss->namedGroupPreferences[i] &&
            ss->namedGroupPreferences[i] == groupDef) {
            return PR_TRUE;
        }
    }
    return PR_FALSE;
}

/* Returns a reference counted object that contains a key pair.
 * Or NULL on failure.  Initial ref count is 1.
 * Uses the keys in the pair as input.  Adopts the keys given.
 */
sslKeyPair *
ssl_NewKeyPair(SECKEYPrivateKey *privKey, SECKEYPublicKey *pubKey)
{
    sslKeyPair *pair;

    if (!privKey || !pubKey) {
        PORT_SetError(PR_INVALID_ARGUMENT_ERROR);
        return NULL;
    }
    pair = PORT_ZNew(sslKeyPair);
    if (!pair)
        return NULL; /* error code is set. */
    pair->privKey = privKey;
    pair->pubKey = pubKey;
    pair->refCount = 1;
    return pair; /* success */
}

sslKeyPair *
ssl_GetKeyPairRef(sslKeyPair *keyPair)
{
    PR_ATOMIC_INCREMENT(&keyPair->refCount);
    return keyPair;
}

void
ssl_FreeKeyPair(sslKeyPair *keyPair)
{
    if (!keyPair) {
        return;
    }

    PRInt32 newCount = PR_ATOMIC_DECREMENT(&keyPair->refCount);
    if (!newCount) {
        SECKEY_DestroyPrivateKey(keyPair->privKey);
        SECKEY_DestroyPublicKey(keyPair->pubKey);
        PORT_Free(keyPair);
    }
}

/* Ephemeral key handling. */
sslEphemeralKeyPair *
ssl_NewEphemeralKeyPair(const sslNamedGroupDef *group,
                        SECKEYPrivateKey *privKey, SECKEYPublicKey *pubKey)
{
    sslKeyPair *keys;
    sslEphemeralKeyPair *pair;

    if (!group) {
        PORT_SetError(PR_INVALID_ARGUMENT_ERROR);
        return NULL;
    }

    keys = ssl_NewKeyPair(privKey, pubKey);
    if (!keys) {
        return NULL;
    }

    pair = PORT_ZNew(sslEphemeralKeyPair);
    if (!pair) {
        ssl_FreeKeyPair(keys);
        return NULL; /* error already set */
    }

    PR_INIT_CLIST(&pair->link);
    pair->group = group;
    pair->keys = keys;
    pair->kemKeys = NULL;
    pair->kemCt = NULL;

    return pair;
}

sslEphemeralKeyPair *
ssl_CopyEphemeralKeyPair(sslEphemeralKeyPair *keyPair)
{
    sslEphemeralKeyPair *pair;

    pair = PORT_ZNew(sslEphemeralKeyPair);
    if (!pair) {
        return NULL; /* error already set */
    }

    pair->kemCt = NULL;
    if (keyPair->kemCt) {
        pair->kemCt = SECITEM_DupItem(keyPair->kemCt);
        if (!pair->kemCt) {
            PORT_Free(pair);
            return NULL;
        }
    }

    PR_INIT_CLIST(&pair->link);
    pair->group = keyPair->group;
    pair->keys = ssl_GetKeyPairRef(keyPair->keys);
    pair->kemKeys = keyPair->kemKeys ? ssl_GetKeyPairRef(keyPair->kemKeys) : NULL;

    return pair;
}

void
ssl_FreeEphemeralKeyPair(sslEphemeralKeyPair *keyPair)
{
    if (!keyPair) {
        return;
    }

    ssl_FreeKeyPair(keyPair->keys);
    ssl_FreeKeyPair(keyPair->kemKeys);
    SECITEM_FreeItem(keyPair->kemCt, PR_TRUE);
    PR_REMOVE_LINK(&keyPair->link);
    PORT_Free(keyPair);
}

PRBool
ssl_HaveEphemeralKeyPair(const sslSocket *ss, const sslNamedGroupDef *groupDef)
{
    return ssl_LookupEphemeralKeyPair((sslSocket *)ss, groupDef) != NULL;
}

sslEphemeralKeyPair *
ssl_LookupEphemeralKeyPair(sslSocket *ss, const sslNamedGroupDef *groupDef)
{
    PRCList *cursor;
    for (cursor = PR_NEXT_LINK(&ss->ephemeralKeyPairs);
         cursor != &ss->ephemeralKeyPairs;
         cursor = PR_NEXT_LINK(cursor)) {
        sslEphemeralKeyPair *keyPair = (sslEphemeralKeyPair *)cursor;
        if (keyPair->group == groupDef) {
            return keyPair;
        }
    }
    return NULL;
}

void
ssl_FreeEphemeralKeyPairs(sslSocket *ss)
{
    while (!PR_CLIST_IS_EMPTY(&ss->ephemeralKeyPairs)) {
        PRCList *cursor = PR_LIST_TAIL(&ss->ephemeralKeyPairs);
        ssl_FreeEphemeralKeyPair((sslEphemeralKeyPair *)cursor);
    }
}

PRTime
ssl_Time(const sslSocket *ss)
{
    if (!ss->now) {
        return PR_Now();
    }
    return ss->now(ss->nowArg);
}

/*
** Create a newsocket structure for a file descriptor.
*/
static sslSocket *
ssl_NewSocket(PRBool makeLocks, SSLProtocolVariant protocolVariant)
{
    SECStatus rv;
    sslSocket *ss;
    int i;
    ssl_SetDefaultsFromEnvironment();

    if (ssl_force_locks)
        makeLocks = PR_TRUE;

    /* Make a new socket and get it ready */
    ss = PORT_ZNew(sslSocket);
    if (!ss) {
        return NULL;
    }
    ss->opt = ssl_defaults;
    if (protocolVariant == ssl_variant_datagram) {
        ss->opt.enableRenegotiation = SSL_RENEGOTIATE_NEVER;
    }
    ss->opt.useSocks = PR_FALSE;
    ss->opt.noLocks = !makeLocks;
    ss->vrange = *VERSIONS_DEFAULTS(protocolVariant);
    ss->protocolVariant = protocolVariant;
    /* Ignore overlap failures, because returning NULL would trigger assertion
     * failures elsewhere. We don't want this scenario to be fatal, it's just
     * a state where no SSL connectivity is possible. */
    ssl3_CreateOverlapWithPolicy(ss->protocolVariant, &ss->vrange, &ss->vrange);
    ss->peerID = NULL;
    ss->rTimeout = PR_INTERVAL_NO_TIMEOUT;
    ss->wTimeout = PR_INTERVAL_NO_TIMEOUT;
    ss->cTimeout = PR_INTERVAL_NO_TIMEOUT;
    ss->url = NULL;

    PR_INIT_CLIST(&ss->serverCerts);
    PR_INIT_CLIST(&ss->ephemeralKeyPairs);
    PR_INIT_CLIST(&ss->extensionHooks);
    PR_INIT_CLIST(&ss->echConfigs);

    ss->dbHandle = CERT_GetDefaultCertDB();

    /* Provide default implementation of hooks */
    ss->authCertificate = SSL_AuthCertificate;
    ss->authCertificateArg = (void *)ss->dbHandle;
    ss->sniSocketConfig = NULL;
    ss->sniSocketConfigArg = NULL;
    ss->getClientAuthData = NULL;
    ss->alertReceivedCallback = NULL;
    ss->alertReceivedCallbackArg = NULL;
    ss->alertSentCallback = NULL;
    ss->alertSentCallbackArg = NULL;
    ss->handleBadCert = NULL;
    ss->badCertArg = NULL;
    ss->pkcs11PinArg = NULL;

    ssl_ChooseOps(ss);
    ssl3_InitSocketPolicy(ss);
    for (i = 0; i < SSL_NAMED_GROUP_COUNT; ++i) {
        ss->namedGroupPreferences[i] = &ssl_named_groups[i];
    }
    ss->additionalShares = 0;
    PR_INIT_CLIST(&ss->ssl3.hs.remoteExtensions);
    PR_INIT_CLIST(&ss->ssl3.hs.lastMessageFlight);
    PR_INIT_CLIST(&ss->ssl3.hs.cipherSpecs);
    PR_INIT_CLIST(&ss->ssl3.hs.bufferedEarlyData);
    ssl3_InitExtensionData(&ss->xtnData, ss);
    PR_INIT_CLIST(&ss->ssl3.hs.dtlsSentHandshake);
    PR_INIT_CLIST(&ss->ssl3.hs.dtlsRcvdHandshake);
    PR_INIT_CLIST(&ss->ssl3.hs.psks);
    dtls_InitTimers(ss);

    ss->echPrivKey = NULL;
    ss->echPubKey = NULL;
    ss->antiReplay = NULL;
    ss->psk = NULL;

    if (makeLocks) {
        rv = ssl_MakeLocks(ss);
        if (rv != SECSuccess)
            goto loser;
    }
    rv = ssl_CreateSecurityInfo(ss);
    if (rv != SECSuccess)
        goto loser;
    rv = ssl3_InitGather(&ss->gs);
    if (rv != SECSuccess)
        goto loser;
    rv = ssl3_InitState(ss);
    if (rv != SECSuccess) {
        goto loser;
    }
    return ss;

loser:
    ssl_DestroySocketContents(ss);
    ssl_DestroyLocks(ss);
    PORT_Free(ss);
    return NULL;
}

/**
 * DEPRECATED: Will always return false.
 */
SECStatus
SSL_CanBypass(CERTCertificate *cert, SECKEYPrivateKey *srvPrivkey,
              PRUint32 protocolmask, PRUint16 *ciphersuites, int nsuites,
              PRBool *pcanbypass, void *pwArg)
{
    if (!pcanbypass) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    *pcanbypass = PR_FALSE;
    return SECSuccess;
}

/* Functions that are truly experimental use EXP, functions that are no longer
 * experimental use PUB.
 *
 * When initially defining a new API, add that API here using the EXP() macro
 * and name the function with a SSLExp_ prefix.  Define the experimental API as
 * a macro in sslexp.h using the SSL_EXPERIMENTAL_API() macro defined there.
 *
 * Once an API is stable and proven, move the macro definition in sslexp.h to a
 * proper function declaration in ssl.h.  Keeping the function in this list
 * ensures that code built against the release that contained the experimental
 * API will continue to work; use PUB() to reference the public function.
 */
#define EXP(n)                \
    {                         \
        "SSL_" #n, SSLExp_##n \
    }
#define PUB(n)             \
    {                      \
        "SSL_" #n, SSL_##n \
    }
struct {
    const char *const name;
    void *function;
} ssl_experimental_functions[] = {
#ifndef SSL_DISABLE_EXPERIMENTAL_API
    EXP(AddExternalPsk),
    EXP(AddExternalPsk0Rtt),
    EXP(AeadDecrypt),
    EXP(AeadEncrypt),
    EXP(CallExtensionWriterOnEchInner),
    EXP(CipherSuiteOrderGet),
    EXP(CipherSuiteOrderSet),
    EXP(CreateAntiReplayContext),
    EXP(CreateMask),
    EXP(CreateMaskingContext),
    EXP(CreateVariantMaskingContext),
    EXP(DelegateCredential),
    EXP(DestroyAead),
    EXP(DestroyMaskingContext),
    EXP(DestroyResumptionTokenInfo),
    EXP(EnableTls13BackendEch),
    EXP(EnableTls13GreaseEch),
    EXP(SetTls13GreaseEchSize),
    EXP(EncodeEchConfigId),
    EXP(GetCurrentEpoch),
    EXP(GetEchRetryConfigs),
    EXP(GetExtensionSupport),
    EXP(GetResumptionTokenInfo),
    EXP(HelloRetryRequestCallback),
    EXP(InstallExtensionHooks),
    EXP(HkdfExtract),
    EXP(HkdfExpandLabel),
    EXP(HkdfExpandLabelWithMech),
    EXP(HkdfVariantExpandLabel),
    EXP(HkdfVariantExpandLabelWithMech),
    EXP(KeyUpdate),
    EXP(MakeAead),
    EXP(MakeVariantAead),
    EXP(RecordLayerData),
    EXP(RecordLayerWriteCallback),
    EXP(ReleaseAntiReplayContext),
    EXP(RemoveEchConfigs),
    EXP(RemoveExternalPsk),
    EXP(SecretCallback),
    EXP(SendCertificateRequest),
    EXP(SendSessionTicket),
    EXP(SetAntiReplayContext),
    EXP(SetClientEchConfigs),
    EXP(SetDtls13VersionWorkaround),
    EXP(SetMaxEarlyDataSize),
    EXP(SetResumptionTokenCallback),
    EXP(SetResumptionToken),
    EXP(SetServerEchConfigs),
    EXP(SetTimeFunc),
    EXP(SetCertificateCompressionAlgorithm),
    EXP(PeerCertificateChainDER),
#endif
    { "", NULL }
};
#undef EXP
#undef PUB

void *
SSL_GetExperimentalAPI(const char *name)
{
    unsigned int i;
    for (i = 0; i < PR_ARRAY_SIZE(ssl_experimental_functions); ++i) {
        if (strcmp(name, ssl_experimental_functions[i].name) == 0) {
            return ssl_experimental_functions[i].function;
        }
    }
    PORT_SetError(SSL_ERROR_UNSUPPORTED_EXPERIMENTAL_API);
    return NULL;
}

void
ssl_ClearPRCList(PRCList *list, void (*f)(void *))
{
    PRCList *cursor;

    while (!PR_CLIST_IS_EMPTY(list)) {
        cursor = PR_LIST_TAIL(list);

        PR_REMOVE_LINK(cursor);
        if (f) {
            f(cursor);
        }
        PORT_Free(cursor);
    }
}

SECStatus
SSLExp_EnableTls13GreaseEch(PRFileDesc *fd, PRBool enabled)
{
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        return SECFailure;
    }
    ss->opt.enableTls13GreaseEch = enabled;
    return SECSuccess;
}

SECStatus
SSLExp_SetTls13GreaseEchSize(PRFileDesc *fd, PRUint8 size)
{
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss || size == 0) {
        return SECFailure;
    }
    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);

    ss->ssl3.hs.greaseEchSize = size;

    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);

    return SECSuccess;
}

SECStatus
SSLExp_EnableTls13BackendEch(PRFileDesc *fd, PRBool enabled)
{
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        return SECFailure;
    }
    ss->opt.enableTls13BackendEch = enabled;
    return SECSuccess;
}

SECStatus
SSLExp_CallExtensionWriterOnEchInner(PRFileDesc *fd, PRBool enabled)
{
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        return SECFailure;
    }
    ss->opt.callExtensionWriterOnEchInner = enabled;
    return SECSuccess;
}

SECStatus
SSLExp_SetDtls13VersionWorkaround(PRFileDesc *fd, PRBool enabled)
{
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        return SECFailure;
    }
    ss->opt.enableDtls13VersionCompat = enabled;
    return SECSuccess;
}

SECStatus
SSLExp_SetTimeFunc(PRFileDesc *fd, SSLTimeFunc f, void *arg)
{
    sslSocket *ss = ssl_FindSocket(fd);

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SetTimeFunc",
                 SSL_GETPID(), fd));
        return SECFailure;
    }
    ss->now = f;
    ss->nowArg = arg;
    return SECSuccess;
}

/* Experimental APIs for session cache handling. */

SECStatus
SSLExp_SetResumptionTokenCallback(PRFileDesc *fd,
                                  SSLResumptionTokenCallback cb,
                                  void *ctx)
{
    sslSocket *ss = ssl_FindSocket(fd);

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SetResumptionTokenCallback",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);
    ss->resumptionTokenCallback = cb;
    ss->resumptionTokenContext = ctx;
    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);

    return SECSuccess;
}

SECStatus
SSLExp_SetResumptionToken(PRFileDesc *fd, const PRUint8 *token,
                          unsigned int len)
{
    sslSocket *ss = ssl_FindSocket(fd);
    sslSessionID *sid = NULL;

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SetResumptionToken",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);

    if (ss->firstHsDone || ss->ssl3.hs.ws != idle_handshake ||
        ss->sec.isServer || len == 0 || !token) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        goto loser;
    }

    // We override any previously set session.
    if (ss->sec.ci.sid) {
        ssl_FreeSID(ss->sec.ci.sid);
        ss->sec.ci.sid = NULL;
    }

    PRINT_BUF(50, (ss, "incoming resumption token", token, len));

    sid = ssl3_NewSessionID(ss, PR_FALSE);
    if (!sid) {
        goto loser;
    }

    /* Populate NewSessionTicket values */
    SECStatus rv = ssl_DecodeResumptionToken(sid, token, len);
    if (rv != SECSuccess) {
        // If decoding fails, we assume the token is bad.
        PORT_SetError(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR);
        goto loser;
    }

    // Make sure that the token is currently usable.
    if (!ssl_IsResumptionTokenUsable(ss, sid)) {
        PORT_SetError(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR);
        goto loser;
    }

    // Generate a new random session ID for this ticket.
    rv = PK11_GenerateRandom(sid->u.ssl3.sessionID, SSL3_SESSIONID_BYTES);
    if (rv != SECSuccess) {
        goto loser; // Code set by PK11_GenerateRandom.
    }
    sid->u.ssl3.sessionIDLength = SSL3_SESSIONID_BYTES;
    /* Use the sid->cached as marker that this is from an external cache and
     * we don't have to look up anything in the NSS internal cache. */
    sid->cached = in_external_cache;
    sid->lastAccessTime = ssl_Time(ss);

    ss->sec.ci.sid = sid;

    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);
    return SECSuccess;

loser:
    ssl_FreeSID(sid);
    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);

    return SECFailure;
}

SECStatus
SSLExp_DestroyResumptionTokenInfo(SSLResumptionTokenInfo *token)
{
    if (!token) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (token->peerCert) {
        CERT_DestroyCertificate(token->peerCert);
    }
    PORT_Free(token->alpnSelection);
    PORT_Memset(token, 0, token->length);
    return SECSuccess;
}

SECStatus
SSLExp_GetResumptionTokenInfo(const PRUint8 *tokenData, unsigned int tokenLen,
                              SSLResumptionTokenInfo *tokenOut, PRUintn len)
{
    if (!tokenData || !tokenOut || !tokenLen ||
        len > sizeof(SSLResumptionTokenInfo)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    sslSessionID sid = { 0 };
    SSLResumptionTokenInfo token;

    /* Populate sid values */
    if (ssl_DecodeResumptionToken(&sid, tokenData, tokenLen) != SECSuccess) {
        // If decoding fails, we assume the token is bad.
        PORT_SetError(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR);
        return SECFailure;
    }

    token.peerCert = CERT_DupCertificate(sid.peerCert);

    token.alpnSelectionLen = sid.u.ssl3.alpnSelection.len;
    token.alpnSelection = PORT_ZAlloc(token.alpnSelectionLen);
    if (!token.alpnSelection) {
        return SECFailure;
    }
    if (token.alpnSelectionLen > 0) {
        PORT_Assert(sid.u.ssl3.alpnSelection.data);
        PORT_Memcpy(token.alpnSelection, sid.u.ssl3.alpnSelection.data,
                    token.alpnSelectionLen);
    }

    if (sid.u.ssl3.locked.sessionTicket.flags & ticket_allow_early_data) {
        token.maxEarlyDataSize =
            sid.u.ssl3.locked.sessionTicket.max_early_data_size;
    } else {
        token.maxEarlyDataSize = 0;
    }
    token.expirationTime = sid.expirationTime;

    token.length = PR_MIN(sizeof(SSLResumptionTokenInfo), len);
    PORT_Memcpy(tokenOut, &token, token.length);

    ssl_DestroySID(&sid, PR_FALSE);
    return SECSuccess;
}
