/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Various SSL functions.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "cert.h"
#include "secitem.h"
#include "keyhi.h"
#include "ssl.h"
#include "sslimpl.h"
#include "sslproto.h"
#include "secoid.h"   /* for SECOID_GetALgorithmTag */
#include "pk11func.h" /* for PK11_GenerateRandom */
#include "nss.h"      /* for NSS_RegisterShutdown */
#include "prinit.h"   /* for PR_CallOnceWithArg */
#include "tls13ech.h"
#include "tls13psk.h"

/* Step through the handshake functions.
 *
 * Called from: SSL_ForceHandshake  (below),
 *              ssl_SecureRecv      (below) and
 *              ssl_SecureSend      (below)
 *    from: WaitForResponse     in sslsocks.c
 *          ssl_SocksRecv       in sslsocks.c
 *              ssl_SocksSend       in sslsocks.c
 *
 * Caller must hold the (write) handshakeLock.
 */
SECStatus
ssl_Do1stHandshake(sslSocket *ss)
{
    SECStatus rv = SECSuccess;

    while (ss->handshake && rv == SECSuccess) {
        PORT_Assert(ss->opt.noLocks || ssl_Have1stHandshakeLock(ss));
        PORT_Assert(ss->opt.noLocks || !ssl_HaveRecvBufLock(ss));
        PORT_Assert(ss->opt.noLocks || !ssl_HaveXmitBufLock(ss));
        PORT_Assert(ss->opt.noLocks || !ssl_HaveSSL3HandshakeLock(ss));

        rv = (*ss->handshake)(ss);
    };

    PORT_Assert(ss->opt.noLocks || !ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || !ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || !ssl_HaveSSL3HandshakeLock(ss));

    return rv;
}

SECStatus
ssl_FinishHandshake(sslSocket *ss)
{
    PORT_Assert(ss->opt.noLocks || ssl_Have1stHandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->ssl3.hs.echAccepted ||
                (ss->opt.enableTls13BackendEch &&
                 ss->xtnData.ech &&
                 ss->xtnData.ech->receivedInnerXtn) ==
                    ssl3_ExtensionNegotiated(ss, ssl_tls13_encrypted_client_hello_xtn));

    /* If ECH was OFFERED to (echHpkeCtx is set on the client) DISABLED by the
     * server through negotiation of a TLS version < 1.3, an 'ech_required'
     * alert MUST be sent to inform the server about the intention / possible
     * misconfiguration. */
    if (!ss->sec.isServer && ss->ssl3.hs.echHpkeCtx && !ss->ssl3.hs.echAccepted) {
        SSL3_SendAlert(ss, alert_fatal, ech_required);
        /* "If [one, none] of the retry_configs contains a supported version,
         * the client can regard ECH as securely [replaced, disabled] by the
         * server." */
        if (ss->xtnData.ech && ss->xtnData.ech->retryConfigs.len) {
            PORT_SetError(SSL_ERROR_ECH_RETRY_WITH_ECH);
            ss->xtnData.ech->retryConfigsValid = PR_TRUE;
        } else {
            PORT_SetError(SSL_ERROR_ECH_RETRY_WITHOUT_ECH);
        }
        return SECFailure;
    }

    SSL_TRC(3, ("%d: SSL[%d]: handshake is completed", SSL_GETPID(), ss->fd));

    ss->firstHsDone = PR_TRUE;
    ss->enoughFirstHsDone = PR_TRUE;
    ss->gs.writeOffset = 0;
    ss->gs.readOffset = 0;

    if (ss->handshakeCallback) {
        PORT_Assert((ss->ssl3.hs.preliminaryInfo & ssl_preinfo_all) ==
                    ssl_preinfo_all);
        (ss->handshakeCallback)(ss->fd, ss->handshakeCallbackData);
    }

    ssl_FreeEphemeralKeyPairs(ss);

    return SECSuccess;
}

/*
 * Handshake function that blocks.  Used to force a
 * retry on a connection on the next read/write.
 */
static SECStatus
ssl3_AlwaysBlock(sslSocket *ss)
{
    PORT_SetError(PR_WOULD_BLOCK_ERROR);
    return SECFailure;
}

/*
 * set the initial handshake state machine to block
 */
void
ssl3_SetAlwaysBlock(sslSocket *ss)
{
    if (!ss->firstHsDone) {
        ss->handshake = ssl3_AlwaysBlock;
    }
}

static SECStatus
ssl_SetTimeout(PRFileDesc *fd, PRIntervalTime timeout)
{
    sslSocket *ss;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SetTimeout", SSL_GETPID(), fd));
        return SECFailure;
    }
    SSL_LOCK_READER(ss);
    ss->rTimeout = timeout;
    if (ss->opt.fdx) {
        SSL_LOCK_WRITER(ss);
    }
    ss->wTimeout = timeout;
    if (ss->opt.fdx) {
        SSL_UNLOCK_WRITER(ss);
    }
    SSL_UNLOCK_READER(ss);
    return SECSuccess;
}

/* Acquires and releases HandshakeLock.
 */
SECStatus
SSL_ResetHandshake(PRFileDesc *s, PRBool asServer)
{
    sslSocket *ss;
    SECStatus status;
    PRNetAddr addr;

    ss = ssl_FindSocket(s);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in ResetHandshake", SSL_GETPID(), s));
        return SECFailure;
    }

    /* Don't waste my time */
    if (!ss->opt.useSecurity)
        return SECSuccess;

    SSL_LOCK_READER(ss);
    SSL_LOCK_WRITER(ss);

    /* Reset handshake state */
    ssl_Get1stHandshakeLock(ss);

    ss->firstHsDone = PR_FALSE;
    ss->enoughFirstHsDone = PR_FALSE;
    ss->sec.isServer = asServer;
    if (asServer) {
        ss->handshake = ssl_BeginServerHandshake;
    } else {
        ss->handshake = ssl_BeginClientHandshake;
    }

    ssl_GetRecvBufLock(ss);
    status = ssl3_InitGather(&ss->gs);
    ssl_ReleaseRecvBufLock(ss);
    if (status != SECSuccess) {
        ssl_Release1stHandshakeLock(ss);
        goto loser;
    }

    ssl_GetSSL3HandshakeLock(ss);
    ss->ssl3.hs.canFalseStart = PR_FALSE;
    ss->ssl3.hs.restartTarget = NULL;

    /*
    ** Blow away old security state and get a fresh setup.
    */
    ssl_GetXmitBufLock(ss);
    ssl_ResetSecurityInfo(&ss->sec, PR_TRUE);
    status = ssl_CreateSecurityInfo(ss);
    ssl_ReleaseXmitBufLock(ss);

    ssl3_DestroyRemoteExtensions(&ss->ssl3.hs.remoteExtensions);
    ssl3_DestroyRemoteExtensions(&ss->ssl3.hs.echOuterExtensions);
    ssl3_ResetExtensionData(&ss->xtnData, ss);
    tls13_ResetHandshakePsks(ss, &ss->ssl3.hs.psks);

    if (ss->ssl3.hs.echHpkeCtx) {
        PK11_HPKE_DestroyContext(ss->ssl3.hs.echHpkeCtx, PR_TRUE);
        ss->ssl3.hs.echHpkeCtx = NULL;
        PORT_Assert(ss->ssl3.hs.echPublicName);
        PORT_Free((void *)ss->ssl3.hs.echPublicName); /* CONST */
        ss->ssl3.hs.echPublicName = NULL;
    }
    /* Make sure greaseEchBuf is freed in ECH setups without echHpkeCtx. */
    if (ss->ssl3.hs.echHpkeCtx ||
        ss->opt.enableTls13BackendEch ||
        ss->opt.enableTls13GreaseEch) {
        sslBuffer_Clear(&ss->ssl3.hs.greaseEchBuf);
    }

    tls13_ClientGreaseDestroy(ss);

    tls_ClientHelloExtensionPermutationDestroy(ss);

    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);

    if (!ss->TCPconnected)
        ss->TCPconnected = (PR_SUCCESS == ssl_DefGetpeername(ss, &addr));

loser:
    SSL_UNLOCK_WRITER(ss);
    SSL_UNLOCK_READER(ss);

    return status;
}

/* For SSLv2, does nothing but return an error.
** For SSLv3, flushes SID cache entry (if requested),
** and then starts new client hello or hello request.
** Acquires and releases HandshakeLock.
*/
SECStatus
SSL_ReHandshake(PRFileDesc *fd, PRBool flushCache)
{
    sslSocket *ss;
    SECStatus rv;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in RedoHandshake", SSL_GETPID(), fd));
        return SECFailure;
    }

    if (!ss->opt.useSecurity)
        return SECSuccess;

    ssl_Get1stHandshakeLock(ss);

    ssl_GetSSL3HandshakeLock(ss);
    rv = ssl3_RedoHandshake(ss, flushCache); /* force full handshake. */
    ssl_ReleaseSSL3HandshakeLock(ss);

    ssl_Release1stHandshakeLock(ss);

    return rv;
}

/*
** Same as above, but with an I/O timeout.
*/
SSL_IMPORT SECStatus
SSL_ReHandshakeWithTimeout(PRFileDesc *fd,
                           PRBool flushCache,
                           PRIntervalTime timeout)
{
    if (SECSuccess != ssl_SetTimeout(fd, timeout)) {
        return SECFailure;
    }
    return SSL_ReHandshake(fd, flushCache);
}

SECStatus
SSL_RedoHandshake(PRFileDesc *fd)
{
    return SSL_ReHandshake(fd, PR_TRUE);
}

/* Register an application callback to be called when SSL handshake completes.
** Acquires and releases HandshakeLock.
*/
SECStatus
SSL_HandshakeCallback(PRFileDesc *fd, SSLHandshakeCallback cb,
                      void *client_data)
{
    sslSocket *ss;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in HandshakeCallback",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    if (!ss->opt.useSecurity) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);

    ss->handshakeCallback = cb;
    ss->handshakeCallbackData = client_data;

    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);

    return SECSuccess;
}

/* Register an application callback to be called when false start may happen.
** Acquires and releases HandshakeLock.
*/
SECStatus
SSL_SetCanFalseStartCallback(PRFileDesc *fd, SSLCanFalseStartCallback cb,
                             void *arg)
{
    sslSocket *ss;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SetCanFalseStartCallback",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    if (!ss->opt.useSecurity) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);

    ss->canFalseStartCallback = cb;
    ss->canFalseStartCallbackData = arg;

    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);

    return SECSuccess;
}

SECStatus
SSL_RecommendedCanFalseStart(PRFileDesc *fd, PRBool *canFalseStart)
{
    sslSocket *ss;

    *canFalseStart = PR_FALSE;
    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_RecommendedCanFalseStart",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    /* Require a forward-secret key exchange. */
    *canFalseStart = ss->ssl3.hs.kea_def->kea == kea_dhe_dss ||
                     ss->ssl3.hs.kea_def->kea == kea_dhe_rsa ||
                     ss->ssl3.hs.kea_def->kea == kea_ecdhe_ecdsa ||
                     ss->ssl3.hs.kea_def->kea == kea_ecdhe_rsa;

    return SECSuccess;
}

/* Try to make progress on an SSL handshake by attempting to read the
** next handshake from the peer, and sending any responses.
** For non-blocking sockets, returns PR_ERROR_WOULD_BLOCK  if it cannot
** read the next handshake from the underlying socket.
** Returns when handshake is complete, or application data has
** arrived that must be taken by application before handshake can continue,
** or a fatal error occurs.
** Application should use handshake completion callback to tell which.
*/
SECStatus
SSL_ForceHandshake(PRFileDesc *fd)
{
    sslSocket *ss;
    SECStatus rv = SECFailure;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in ForceHandshake",
                 SSL_GETPID(), fd));
        return rv;
    }

    /* Don't waste my time */
    if (!ss->opt.useSecurity)
        return SECSuccess;

    if (!ssl_SocketIsBlocking(ss)) {
        ssl_GetXmitBufLock(ss);
        if (ss->pendingBuf.len != 0) {
            int sent = ssl_SendSavedWriteData(ss);
            if ((sent < 0) && (PORT_GetError() != PR_WOULD_BLOCK_ERROR)) {
                ssl_ReleaseXmitBufLock(ss);
                return SECFailure;
            }
        }
        ssl_ReleaseXmitBufLock(ss);
    }

    ssl_Get1stHandshakeLock(ss);

    if (ss->version >= SSL_LIBRARY_VERSION_3_0) {
        int gatherResult;

        ssl_GetRecvBufLock(ss);
        gatherResult = ssl3_GatherCompleteHandshake(ss, 0);
        ssl_ReleaseRecvBufLock(ss);
        if (gatherResult > 0) {
            rv = SECSuccess;
        } else {
            if (gatherResult == 0) {
                PORT_SetError(PR_END_OF_FILE_ERROR);
            }
            /* We can rely on ssl3_GatherCompleteHandshake to set
             * PR_WOULD_BLOCK_ERROR as needed here. */
            rv = SECFailure;
        }
    } else {
        PORT_Assert(!ss->firstHsDone);
        rv = ssl_Do1stHandshake(ss);
    }

    ssl_Release1stHandshakeLock(ss);

    return rv;
}

/*
 ** Same as above, but with an I/O timeout.
 */
SSL_IMPORT SECStatus
SSL_ForceHandshakeWithTimeout(PRFileDesc *fd,
                              PRIntervalTime timeout)
{
    if (SECSuccess != ssl_SetTimeout(fd, timeout)) {
        return SECFailure;
    }
    return SSL_ForceHandshake(fd);
}

/************************************************************************/

/*
** Save away write data that is trying to be written before the security
** handshake has been completed. When the handshake is completed, we will
** flush this data out.
** Caller must hold xmitBufLock
*/
SECStatus
ssl_SaveWriteData(sslSocket *ss, const void *data, unsigned int len)
{
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    rv = sslBuffer_Append(&ss->pendingBuf, data, len);
    SSL_TRC(5, ("%d: SSL[%d]: saving %u bytes of data (%u total saved so far)",
                SSL_GETPID(), ss->fd, len, ss->pendingBuf.len));
    return rv;
}

/*
** Send saved write data. This will flush out data sent prior to a
** complete security handshake. Hopefully there won't be too much of it.
** Returns count of the bytes sent, NOT a SECStatus.
** Caller must hold xmitBufLock
*/
int
ssl_SendSavedWriteData(sslSocket *ss)
{
    int rv = 0;

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    if (ss->pendingBuf.len != 0) {
        SSL_TRC(5, ("%d: SSL[%d]: sending %d bytes of saved data",
                    SSL_GETPID(), ss->fd, ss->pendingBuf.len));
        rv = ssl_DefSend(ss, ss->pendingBuf.buf, ss->pendingBuf.len, 0);
        if (rv < 0) {
            return rv;
        }
        if (rv > ss->pendingBuf.len) {
            PORT_Assert(0); /* This shouldn't happen */
            ss->pendingBuf.len = 0;
        } else {
            ss->pendingBuf.len -= rv;
        }
        if (ss->pendingBuf.len > 0 && rv > 0) {
            /* UGH !! This shifts the whole buffer down by copying it */
            PORT_Memmove(ss->pendingBuf.buf, ss->pendingBuf.buf + rv,
                         ss->pendingBuf.len);
        }
    }
    return rv;
}

/************************************************************************/

/*
** Receive some application data on a socket.  Reads SSL records from the input
** stream, decrypts them and then copies them to the output buffer.
** Called from ssl_SecureRecv() below.
**
** Caller does NOT hold 1stHandshakeLock because that handshake is over.
** Caller doesn't call this until initial handshake is complete.
** The call to ssl3_GatherAppDataRecord may encounter handshake
** messages from a subsequent handshake.
**
** This code is similar to, and easily confused with,
**   ssl_GatherRecord1stHandshake() in sslcon.c
*/
static int
DoRecv(sslSocket *ss, unsigned char *out, int len, int flags)
{
    int rv;
    int amount;
    int available;

    /* ssl3_GatherAppDataRecord may call ssl_FinishHandshake, which needs the
     * 1stHandshakeLock. */
    ssl_Get1stHandshakeLock(ss);
    ssl_GetRecvBufLock(ss);

    available = ss->gs.writeOffset - ss->gs.readOffset;
    if (available == 0) {
        /* Wait for application data to arrive.  */
        rv = ssl3_GatherAppDataRecord(ss, 0);
        if (rv <= 0) {
            if (rv == 0) {
                /* EOF */
                SSL_TRC(10, ("%d: SSL[%d]: ssl_recv EOF",
                             SSL_GETPID(), ss->fd));
                goto done;
            }
            if (PR_GetError() != PR_WOULD_BLOCK_ERROR) {
                /* Some random error */
                goto done;
            }

            /*
            ** Gather record is blocked waiting for more record data to
            ** arrive. Try to process what we have already received
            */
        } else {
            /* Gather record has finished getting a complete record */
        }

        /* See if any clear data is now available */
        available = ss->gs.writeOffset - ss->gs.readOffset;
        if (available == 0) {
            /*
            ** No partial data is available. Force error code to
            ** EWOULDBLOCK so that caller will try again later. Note
            ** that the error code is probably EWOULDBLOCK already,
            ** but if it isn't (for example, if we received a zero
            ** length record) then this will force it to be correct.
            */
            PORT_SetError(PR_WOULD_BLOCK_ERROR);
            rv = SECFailure;
            goto done;
        }
        SSL_TRC(30, ("%d: SSL[%d]: partial data ready, available=%d",
                     SSL_GETPID(), ss->fd, available));
    }

    if (IS_DTLS(ss) && (len < available)) {
        /* DTLS does not allow you to do partial reads */
        SSL_TRC(30, ("%d: SSL[%d]: DTLS short read. len=%d available=%d",
                     SSL_GETPID(), ss->fd, len, available));
        ss->gs.readOffset += available;
        PORT_SetError(SSL_ERROR_RX_SHORT_DTLS_READ);
        rv = SECFailure;
        goto done;
    }

    /* Dole out clear data to reader */
    amount = PR_MIN(len, available);
    PORT_Memcpy(out, ss->gs.buf.buf + ss->gs.readOffset, amount);
    if (!(flags & PR_MSG_PEEK)) {
        ss->gs.readOffset += amount;
    }
    PORT_Assert(ss->gs.readOffset <= ss->gs.writeOffset);
    rv = amount;

#ifdef DEBUG
    /* In Debug builds free and zero gather plaintext buffer after its content
     * has been used/copied for advanced ASAN coverage/utilization.
     * This frees the buffer after reception of application data,
     * non-application data is freed at the end of
     * ssl3con.c/ssl3_HandleRecord(). */
    if (ss->gs.writeOffset == ss->gs.readOffset) {
        sslBuffer_Clear(&ss->gs.buf);
    }
#endif

    SSL_TRC(30, ("%d: SSL[%d]: amount=%d available=%d",
                 SSL_GETPID(), ss->fd, amount, available));
    PRINT_BUF(4, (ss, "DoRecv receiving plaintext:", out, amount));

done:
    ssl_ReleaseRecvBufLock(ss);
    ssl_Release1stHandshakeLock(ss);
    return rv;
}

/************************************************************************/

SECStatus
ssl_CreateSecurityInfo(sslSocket *ss)
{
    SECStatus status;

    ssl_GetXmitBufLock(ss);
    status = sslBuffer_Grow(&ss->sec.writeBuf, 4096);
    ssl_ReleaseXmitBufLock(ss);

    return status;
}

SECStatus
ssl_CopySecurityInfo(sslSocket *ss, sslSocket *os)
{
    ss->sec.isServer = os->sec.isServer;

    if (os->sec.peerCertDER.data) {
        if (ssl_SetPeerCertificate(&ss->sec, &os->sec.peerCertDER) !=
            SECSuccess) {
            goto loser;
        }
    }

    return SECSuccess;

loser:
    return SECFailure;
}

/* Reset sec back to its initial state.
** Caller holds any relevant locks.
*/
void
ssl_ResetSecurityInfo(sslSecurityInfo *sec, PRBool doMemset)
{
    if (sec->localCert) {
        CERT_DestroyCertificate(sec->localCert);
        sec->localCert = NULL;
    }
    ssl_ClearPeerCertificate(sec);
    if (sec->peerKey) {
        SECKEY_DestroyPublicKey(sec->peerKey);
        sec->peerKey = NULL;
    }

    /* cleanup the ci */
    if (sec->ci.sid != NULL) {
        ssl_FreeSID(sec->ci.sid);
    }
    PORT_ZFree(sec->ci.sendBuf.buf, sec->ci.sendBuf.space);
    if (doMemset) {
        memset(&sec->ci, 0, sizeof sec->ci);
    }
}

/*
** Called from SSL_ResetHandshake (above), and
**        from ssl_FreeSocket     in sslsock.c
** Caller should hold relevant locks (e.g. XmitBufLock)
*/
void
ssl_DestroySecurityInfo(sslSecurityInfo *sec)
{
    ssl_ResetSecurityInfo(sec, PR_FALSE);

    PORT_ZFree(sec->writeBuf.buf, sec->writeBuf.space);
    sec->writeBuf.buf = 0;

    memset(sec, 0, sizeof *sec);
}

/************************************************************************/

int
ssl_SecureConnect(sslSocket *ss, const PRNetAddr *sa)
{
    PRFileDesc *osfd = ss->fd->lower;
    int rv;

    ss->sec.isServer = ss->opt.handshakeAsServer ? PR_TRUE : PR_FALSE;
    if (ss->opt.handshakeAsServer) {
        ss->handshake = ssl_BeginServerHandshake;
    } else {
        ss->handshake = ssl_BeginClientHandshake;
    }

    /* connect to server */
    rv = osfd->methods->connect(osfd, sa, ss->cTimeout);
    if (rv == PR_SUCCESS) {
        ss->TCPconnected = 1;
    } else {
        int err = PR_GetError();
        SSL_DBG(("%d: SSL[%d]: connect failed, errno=%d",
                 SSL_GETPID(), ss->fd, err));
        if (err == PR_IS_CONNECTED_ERROR) {
            ss->TCPconnected = 1;
        }
    }

    SSL_TRC(5, ("%d: SSL[%d]: secure connect completed, rv == %d",
                SSL_GETPID(), ss->fd, rv));
    return rv;
}

/*
 * Also, in the unlikely event that the TCP pipe is full and the peer stops
 * reading, the SSL3_SendAlert call in ssl_SecureClose and ssl_SecureShutdown
 * may block indefinitely in blocking mode, and may fail (without retrying)
 * in non-blocking mode.
 */

int
ssl_SecureClose(sslSocket *ss)
{
    int rv;

    if (!(ss->shutdownHow & ssl_SHUTDOWN_SEND) &&
        ss->firstHsDone) {

        /* We don't want the final alert to be Nagle delayed. */
        if (!ss->delayDisabled) {
            ssl_EnableNagleDelay(ss, PR_FALSE);
            ss->delayDisabled = 1;
        }

        (void)SSL3_SendAlert(ss, alert_warning, close_notify);
    }
    rv = ssl_DefClose(ss);
    return rv;
}

/* Caller handles all locking */
int
ssl_SecureShutdown(sslSocket *ss, int nsprHow)
{
    PRFileDesc *osfd = ss->fd->lower;
    int rv;
    PRIntn sslHow = nsprHow + 1;

    if ((unsigned)nsprHow > PR_SHUTDOWN_BOTH) {
        PORT_SetError(PR_INVALID_ARGUMENT_ERROR);
        return PR_FAILURE;
    }

    if ((sslHow & ssl_SHUTDOWN_SEND) != 0 &&
        !(ss->shutdownHow & ssl_SHUTDOWN_SEND) &&
        ss->firstHsDone) {

        (void)SSL3_SendAlert(ss, alert_warning, close_notify);
    }

    rv = osfd->methods->shutdown(osfd, nsprHow);

    ss->shutdownHow |= sslHow;

    return rv;
}

/************************************************************************/

static SECStatus
tls13_CheckKeyUpdate(sslSocket *ss, SSLSecretDirection dir)
{
    PRBool keyUpdate;
    ssl3CipherSpec *spec;
    sslSequenceNumber seqNum;
    sslSequenceNumber margin;
    tls13KeyUpdateRequest keyUpdateRequest;
    SECStatus rv = SECSuccess;

    if (ss->version < SSL_LIBRARY_VERSION_TLS_1_3) {
        return SECSuccess;
    }

    /* If both sides update at the same number, then this will cause two updates
     * to happen at once. The problem is that the KeyUpdate itself consumes a
     * sequence number, and that will trigger the reading side to request an
     * update.
     *
     * If we have the writing side update first, the writer will be the one that
     * drives the update.  An update by the writer doesn't need a response, so
     * it is more efficient overall.  The margins here are pretty arbitrary, but
     * having the write margin larger reduces the number of times that a
     * KeyUpdate is sent by a reader. */
    ssl_GetSpecReadLock(ss);
    if (dir == ssl_secret_read) {
        spec = ss->ssl3.crSpec;
        margin = spec->cipherDef->max_records / 8;
    } else {
        spec = ss->ssl3.cwSpec;
        margin = spec->cipherDef->max_records / 4;
    }
    seqNum = spec->nextSeqNum;
    keyUpdate = seqNum > spec->cipherDef->max_records - margin;
    ssl_ReleaseSpecReadLock(ss);
    if (!keyUpdate) {
        return SECSuccess;
    }

    SSL_TRC(5, ("%d: SSL[%d]: automatic key update at %llx for %s cipher spec",
                SSL_GETPID(), ss->fd, seqNum,
                (dir == ssl_secret_read) ? "read" : "write"));
    keyUpdateRequest = (dir == ssl_secret_read) ? update_requested : update_not_requested;
    ssl_GetSSL3HandshakeLock(ss);
    if (ss->ssl3.clientCertRequested) {
        ss->ssl3.hs.keyUpdateDeferred = PR_TRUE;
        ss->ssl3.hs.deferredKeyUpdateRequest = keyUpdateRequest;
    } else {
        rv = tls13_SendKeyUpdate(ss, keyUpdateRequest,
                                 dir == ssl_secret_write /* buffer */);
    }
    ssl_ReleaseSSL3HandshakeLock(ss);
    return rv;
}

int
ssl_SecureRecv(sslSocket *ss, unsigned char *buf, int len, int flags)
{
    int rv = 0;

    if (ss->shutdownHow & ssl_SHUTDOWN_RCV) {
        PORT_SetError(PR_SOCKET_SHUTDOWN_ERROR);
        return PR_FAILURE;
    }
    if (flags & ~PR_MSG_PEEK) {
        PORT_SetError(PR_INVALID_ARGUMENT_ERROR);
        return PR_FAILURE;
    }

    if (!ssl_SocketIsBlocking(ss) && !ss->opt.fdx) {
        ssl_GetXmitBufLock(ss);
        if (ss->pendingBuf.len != 0) {
            rv = ssl_SendSavedWriteData(ss);
            if ((rv < 0) && (PORT_GetError() != PR_WOULD_BLOCK_ERROR)) {
                ssl_ReleaseXmitBufLock(ss);
                return SECFailure;
            }
        }
        ssl_ReleaseXmitBufLock(ss);
    }

    rv = 0;
    if (!PR_CLIST_IS_EMPTY(&ss->ssl3.hs.bufferedEarlyData)) {
        PORT_Assert(ss->version >= SSL_LIBRARY_VERSION_TLS_1_3);
        return tls13_Read0RttData(ss, buf, len);
    }

    /* If any of these is non-zero, the initial handshake is not done. */
    if (!ss->firstHsDone) {
        ssl_Get1stHandshakeLock(ss);
        if (ss->handshake) {
            rv = ssl_Do1stHandshake(ss);
        }
        ssl_Release1stHandshakeLock(ss);
    } else {
        if (tls13_CheckKeyUpdate(ss, ssl_secret_read) != SECSuccess) {
            rv = PR_FAILURE;
        }
    }
    if (rv < 0) {
        if (PORT_GetError() == PR_WOULD_BLOCK_ERROR &&
            !PR_CLIST_IS_EMPTY(&ss->ssl3.hs.bufferedEarlyData)) {
            PORT_Assert(ss->version >= SSL_LIBRARY_VERSION_TLS_1_3);
            return tls13_Read0RttData(ss, buf, len);
        }
        return rv;
    }

    if (len == 0)
        return 0;

    rv = DoRecv(ss, (unsigned char *)buf, len, flags);
    SSL_TRC(2, ("%d: SSL[%d]: recving %d bytes securely (errno=%d)",
                SSL_GETPID(), ss->fd, rv, PORT_GetError()));
    return rv;
}

int
ssl_SecureRead(sslSocket *ss, unsigned char *buf, int len)
{
    return ssl_SecureRecv(ss, buf, len, 0);
}

/* Caller holds the SSL Socket's write lock. SSL_LOCK_WRITER(ss) */
int
ssl_SecureSend(sslSocket *ss, const unsigned char *buf, int len, int flags)
{
    int rv = 0;
    PRBool zeroRtt = PR_FALSE;

    SSL_TRC(2, ("%d: SSL[%d]: SecureSend: sending %d bytes",
                SSL_GETPID(), ss->fd, len));

    if (ss->shutdownHow & ssl_SHUTDOWN_SEND) {
        PORT_SetError(PR_SOCKET_SHUTDOWN_ERROR);
        rv = PR_FAILURE;
        goto done;
    }
    if (flags) {
        PORT_SetError(PR_INVALID_ARGUMENT_ERROR);
        rv = PR_FAILURE;
        goto done;
    }

    ssl_GetXmitBufLock(ss);
    if (ss->pendingBuf.len != 0) {
        PORT_Assert(ss->pendingBuf.len > 0);
        rv = ssl_SendSavedWriteData(ss);
        if (rv >= 0 && ss->pendingBuf.len != 0) {
            PORT_Assert(ss->pendingBuf.len > 0);
            PORT_SetError(PR_WOULD_BLOCK_ERROR);
            rv = SECFailure;
        }
    }
    ssl_ReleaseXmitBufLock(ss);
    if (rv < 0) {
        goto done;
    }

    if (len > 0)
        ss->writerThread = PR_GetCurrentThread();

    /* Check to see if we can write even though we're not finished.
     *
     * Case 1: False start
     * Case 2: TLS 1.3 0-RTT
     */
    if (!ss->firstHsDone) {
        PRBool allowEarlySend = PR_FALSE;
        PRBool firstClientWrite = PR_FALSE;

        ssl_Get1stHandshakeLock(ss);
        /* The client can sometimes send before the handshake is fully
         * complete. In TLS 1.2: false start; in TLS 1.3: 0-RTT. */
        if (!ss->sec.isServer &&
            (ss->opt.enableFalseStart || ss->opt.enable0RttData)) {
            ssl_GetSSL3HandshakeLock(ss);
            zeroRtt = ss->ssl3.hs.zeroRttState == ssl_0rtt_sent ||
                      ss->ssl3.hs.zeroRttState == ssl_0rtt_accepted;
            allowEarlySend = ss->ssl3.hs.canFalseStart || zeroRtt;
            firstClientWrite = ss->ssl3.hs.ws == idle_handshake;
            ssl_ReleaseSSL3HandshakeLock(ss);
        }
        /* Allow the server to send 0.5 RTT data in TLS 1.3. Requesting a
         * certificate implies that the server might condition its sending on
         * client authentication, so force servers that do that to wait.
         *
         * What might not be obvious here is that this allows 0.5 RTT when doing
         * PSK-based resumption.  As a result, 0.5 RTT is always enabled when
         * early data is accepted.
         *
         * This check might be more conservative than absolutely necessary.
         * It's possible that allowing 0.5 RTT data when the server requests,
         * but does not require client authentication is safe because we can
         * expect the server to check for a client certificate properly. */
        if (ss->sec.isServer &&
            ss->version >= SSL_LIBRARY_VERSION_TLS_1_3 &&
            !tls13_ShouldRequestClientAuth(ss)) {
            ssl_GetSSL3HandshakeLock(ss);
            allowEarlySend = TLS13_IN_HS_STATE(ss, wait_finished);
            ssl_ReleaseSSL3HandshakeLock(ss);
        }
        if (!allowEarlySend && ss->handshake) {
            rv = ssl_Do1stHandshake(ss);
        }
        if (firstClientWrite) {
            /* Wait until after sending ClientHello and double-check 0-RTT. */
            ssl_GetSSL3HandshakeLock(ss);
            zeroRtt = ss->ssl3.hs.zeroRttState == ssl_0rtt_sent ||
                      ss->ssl3.hs.zeroRttState == ssl_0rtt_accepted;
            ssl_ReleaseSSL3HandshakeLock(ss);
        }
        ssl_Release1stHandshakeLock(ss);
    }

    if (rv < 0) {
        ss->writerThread = NULL;
        goto done;
    }

    if (ss->firstHsDone) {
        if (tls13_CheckKeyUpdate(ss, ssl_secret_write) != SECSuccess) {
            rv = PR_FAILURE;
            goto done;
        }
    }

    if (zeroRtt) {
        /* There's a limit to the number of early data octets we can send.
         *
         * Note that taking this lock doesn't prevent the cipher specs from
         * being changed out between here and when records are ultimately
         * encrypted.  The only effect of that is to occasionally do an
         * unnecessary short write when data is identified as 0-RTT here but
         * 1-RTT later.
         */
        ssl_GetSpecReadLock(ss);
        len = tls13_LimitEarlyData(ss, ssl_ct_application_data, len);
        ssl_ReleaseSpecReadLock(ss);
    }

    /* Check for zero length writes after we do housekeeping so we make forward
     * progress.
     */
    if (len == 0) {
        rv = 0;
        goto done;
    }
    PORT_Assert(buf != NULL);
    if (!buf) {
        PORT_SetError(PR_INVALID_ARGUMENT_ERROR);
        rv = PR_FAILURE;
        goto done;
    }

    ssl_GetXmitBufLock(ss);
    rv = ssl3_SendApplicationData(ss, buf, len, flags);
    ssl_ReleaseXmitBufLock(ss);
    ss->writerThread = NULL;
done:
    if (rv < 0) {
        SSL_TRC(2, ("%d: SSL[%d]: SecureSend: returning %d count, error %d",
                    SSL_GETPID(), ss->fd, rv, PORT_GetError()));
    } else {
        SSL_TRC(2, ("%d: SSL[%d]: SecureSend: returning %d count",
                    SSL_GETPID(), ss->fd, rv));
    }
    return rv;
}

int
ssl_SecureWrite(sslSocket *ss, const unsigned char *buf, int len)
{
    return ssl_SecureSend(ss, buf, len, 0);
}

SECStatus
SSLExp_RecordLayerWriteCallback(PRFileDesc *fd, SSLRecordWriteCallback cb,
                                void *arg)
{
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: invalid socket for SSL_RecordLayerWriteCallback",
                 SSL_GETPID(), fd));
        return SECFailure;
    }
    if (IS_DTLS(ss)) {
        SSL_DBG(("%d: SSL[%d]: DTLS socket for SSL_RecordLayerWriteCallback",
                 SSL_GETPID(), fd));
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    /* This needs both HS and Xmit locks because this value is checked under
     * both locks. HS to disable reading from the underlying IO layer; Xmit to
     * prevent writing. */
    ssl_GetSSL3HandshakeLock(ss);
    ssl_GetXmitBufLock(ss);
    ss->recordWriteCallback = cb;
    ss->recordWriteCallbackArg = arg;
    ssl_ReleaseXmitBufLock(ss);
    ssl_ReleaseSSL3HandshakeLock(ss);
    return SECSuccess;
}

SECStatus
SSL_AlertReceivedCallback(PRFileDesc *fd, SSLAlertCallback cb, void *arg)
{
    sslSocket *ss;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: unable to find socket in SSL_AlertReceivedCallback",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    ss->alertReceivedCallback = cb;
    ss->alertReceivedCallbackArg = arg;

    return SECSuccess;
}

SECStatus
SSL_AlertSentCallback(PRFileDesc *fd, SSLAlertCallback cb, void *arg)
{
    sslSocket *ss;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: unable to find socket in SSL_AlertSentCallback",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    ss->alertSentCallback = cb;
    ss->alertSentCallbackArg = arg;

    return SECSuccess;
}

SECStatus
SSL_BadCertHook(PRFileDesc *fd, SSLBadCertHandler f, void *arg)
{
    sslSocket *ss;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSLBadCertHook",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    ss->handleBadCert = f;
    ss->badCertArg = arg;

    return SECSuccess;
}

/*
 * Allow the application to pass the url or hostname into the SSL library
 * so that we can do some checking on it. It will be used for the value in
 * SNI extension of client hello message.
 */
SECStatus
SSL_SetURL(PRFileDesc *fd, const char *url)
{
    sslSocket *ss = ssl_FindSocket(fd);
    SECStatus rv = SECSuccess;

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSLSetURL",
                 SSL_GETPID(), fd));
        return SECFailure;
    }
    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);

    if (ss->url) {
        PORT_Free((void *)ss->url); /* CONST */
    }

    ss->url = (const char *)PORT_Strdup(url);
    if (ss->url == NULL) {
        rv = SECFailure;
    }

    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);

    return rv;
}

/*
 * Allow the application to pass the set of trust anchors
 */
SECStatus
SSL_SetTrustAnchors(PRFileDesc *fd, CERTCertList *certList)
{
    sslSocket *ss = ssl_FindSocket(fd);
    CERTDistNames *names = NULL;

    if (!certList) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SetTrustAnchors",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    names = CERT_DistNamesFromCertList(certList);
    if (names == NULL) {
        return SECFailure;
    }
    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);
    if (ss->ssl3.ca_list) {
        CERT_FreeDistNames(ss->ssl3.ca_list);
    }
    ss->ssl3.ca_list = names;
    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);

    return SECSuccess;
}

/*
** Returns Negative number on error, zero or greater on success.
** Returns the amount of data immediately available to be read.
*/
int
SSL_DataPending(PRFileDesc *fd)
{
    sslSocket *ss;
    int rv = 0;

    ss = ssl_FindSocket(fd);

    if (ss && ss->opt.useSecurity) {
        ssl_GetRecvBufLock(ss);
        rv = ss->gs.writeOffset - ss->gs.readOffset;
        ssl_ReleaseRecvBufLock(ss);
    }

    return rv;
}

SECStatus
SSL_InvalidateSession(PRFileDesc *fd)
{
    sslSocket *ss = ssl_FindSocket(fd);
    SECStatus rv = SECFailure;

    if (ss) {
        ssl_Get1stHandshakeLock(ss);
        ssl_GetSSL3HandshakeLock(ss);

        if (ss->sec.ci.sid) {
            ssl_UncacheSessionID(ss);
            rv = SECSuccess;
        }

        ssl_ReleaseSSL3HandshakeLock(ss);
        ssl_Release1stHandshakeLock(ss);
    }
    return rv;
}

SECItem *
SSL_GetSessionID(PRFileDesc *fd)
{
    sslSocket *ss;
    SECItem *item = NULL;

    ss = ssl_FindSocket(fd);
    if (ss) {
        ssl_Get1stHandshakeLock(ss);
        ssl_GetSSL3HandshakeLock(ss);

        if (ss->opt.useSecurity && ss->firstHsDone && ss->sec.ci.sid) {
            item = (SECItem *)PORT_Alloc(sizeof(SECItem));
            if (item) {
                sslSessionID *sid = ss->sec.ci.sid;
                item->len = sid->u.ssl3.sessionIDLength;
                item->data = (unsigned char *)PORT_Alloc(item->len);
                PORT_Memcpy(item->data, sid->u.ssl3.sessionID, item->len);
            }
        }

        ssl_ReleaseSSL3HandshakeLock(ss);
        ssl_Release1stHandshakeLock(ss);
    }
    return item;
}

SECStatus
SSL_CertDBHandleSet(PRFileDesc *fd, CERTCertDBHandle *dbHandle)
{
    sslSocket *ss;

    ss = ssl_FindSocket(fd);
    if (!ss)
        return SECFailure;
    if (!dbHandle) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    ss->dbHandle = dbHandle;
    return SECSuccess;
}

/* DO NOT USE. This function was exported in ssl.def with the wrong signature;
 * this implementation exists to maintain link-time compatibility.
 */
int
SSL_RestartHandshakeAfterCertReq(sslSocket *ss,
                                 CERTCertificate *cert,
                                 SECKEYPrivateKey *key,
                                 CERTCertificateList *certChain)
{
    PORT_SetError(PR_NOT_IMPLEMENTED_ERROR);
    return -1;
}

/* DO NOT USE. This function was exported in ssl.def with the wrong signature;
 * this implementation exists to maintain link-time compatibility.
 */
int
SSL_RestartHandshakeAfterServerCert(sslSocket *ss)
{
    PORT_SetError(PR_NOT_IMPLEMENTED_ERROR);
    return -1;
}

/* See documentation in ssl.h */
SECStatus
SSL_AuthCertificateComplete(PRFileDesc *fd, PRErrorCode error)
{
    SECStatus rv;
    sslSocket *ss = ssl_FindSocket(fd);

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_AuthCertificateComplete",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    ssl_Get1stHandshakeLock(ss);
    rv = ssl3_AuthCertificateComplete(ss, error);
    ssl_Release1stHandshakeLock(ss);

    return rv;
}

SECStatus
SSL_ClientCertCallbackComplete(PRFileDesc *fd, SECStatus outcome, SECKEYPrivateKey *clientPrivateKey,
                               CERTCertificate *clientCertificate)
{
    SECStatus rv;
    sslSocket *ss = ssl_FindSocket(fd);

    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_ClientCertCallbackComplete",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    /* There exists a codepath which exercises each lock.
     * Socket is blocked whilst waiting on this callback anyway. */
    ssl_Get1stHandshakeLock(ss);
    ssl_GetRecvBufLock(ss);
    ssl_GetSSL3HandshakeLock(ss);

    if (!ss->ssl3.hs.clientCertificatePending) {
        /* Application invoked callback at wrong time */
        SSL_DBG(("%d: SSL[%d]: socket not waiting for SSL_ClientCertCallbackComplete",
                 SSL_GETPID(), fd));
        PORT_SetError(PR_INVALID_STATE_ERROR);
        rv = SECFailure;
        goto cleanup;
    }

    rv = ssl3_ClientCertCallbackComplete(ss, outcome, clientPrivateKey, clientCertificate);

cleanup:
    ssl_ReleaseRecvBufLock(ss);
    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);
    return rv;
}

/* For more info see ssl.h */
SECStatus
SSL_SNISocketConfigHook(PRFileDesc *fd, SSLSNISocketConfig func,
                        void *arg)
{
    sslSocket *ss;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SNISocketConfigHook",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    ss->sniSocketConfig = func;
    ss->sniSocketConfigArg = arg;
    return SECSuccess;
}
