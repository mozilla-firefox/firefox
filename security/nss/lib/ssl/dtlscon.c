/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * DTLS Protocol
 */

#include "ssl.h"
#include "sslimpl.h"
#include "sslproto.h"
#include "dtls13con.h"

#ifndef PR_ARRAY_SIZE
#define PR_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static SECStatus dtls_StartRetransmitTimer(sslSocket *ss);
static void dtls_RetransmitTimerExpiredCb(sslSocket *ss);
static SECStatus dtls_SendSavedWriteData(sslSocket *ss);
static void dtls_FinishedTimerCb(sslSocket *ss);
static void dtls_CancelAllTimers(sslSocket *ss);

/* -28 adjusts for the IP/UDP header */
static const PRUint16 COMMON_MTU_VALUES[] = {
    1500 - 28, /* Ethernet MTU */
    1280 - 28, /* IPv6 minimum MTU */
    576 - 28,  /* Common assumption */
    256 - 28   /* We're in serious trouble now */
};

#define DTLS_COOKIE_BYTES 32
/* Maximum DTLS expansion = header + IV + max CBC padding +
 * maximum MAC. */
#define DTLS_MAX_EXPANSION (DTLS_RECORD_HEADER_LENGTH + 16 + 16 + 32)

/* List copied from ssl3con.c:cipherSuites */
static const ssl3CipherSuite nonDTLSSuites[] = {
    TLS_ECDHE_ECDSA_WITH_RC4_128_SHA,
    TLS_ECDHE_RSA_WITH_RC4_128_SHA,
    TLS_DHE_DSS_WITH_RC4_128_SHA,
    TLS_ECDH_RSA_WITH_RC4_128_SHA,
    TLS_ECDH_ECDSA_WITH_RC4_128_SHA,
    TLS_RSA_WITH_RC4_128_MD5,
    TLS_RSA_WITH_RC4_128_SHA,
    0 /* End of list marker */
};

/* Map back and forth between TLS and DTLS versions in wire format.
 * Mapping table is:
 *
 * TLS             DTLS
 * 1.1 (0302)      1.0 (feff)
 * 1.2 (0303)      1.2 (fefd)
 * 1.3 (0304)      1.3 (fefc)
 */
SSL3ProtocolVersion
dtls_TLSVersionToDTLSVersion(SSL3ProtocolVersion tlsv)
{
    if (tlsv == SSL_LIBRARY_VERSION_TLS_1_1) {
        return SSL_LIBRARY_VERSION_DTLS_1_0_WIRE;
    }
    if (tlsv == SSL_LIBRARY_VERSION_TLS_1_2) {
        return SSL_LIBRARY_VERSION_DTLS_1_2_WIRE;
    }
    if (tlsv == SSL_LIBRARY_VERSION_TLS_1_3) {
        return SSL_LIBRARY_VERSION_DTLS_1_3_WIRE;
    }

    /* Anything else is an error, so return
     * the invalid version 0xffff. */
    return 0xffff;
}

/* Map known DTLS versions to known TLS versions.
 * - Invalid versions (< 1.0) return a version of 0
 * - Versions > known return a version one higher than we know of
 * to accomodate a theoretically newer version */
SSL3ProtocolVersion
dtls_DTLSVersionToTLSVersion(SSL3ProtocolVersion dtlsv)
{
    if (MSB(dtlsv) == 0xff) {
        return 0;
    }

    if (dtlsv == SSL_LIBRARY_VERSION_DTLS_1_0_WIRE) {
        return SSL_LIBRARY_VERSION_TLS_1_1;
    }
    /* Handle the skipped version of DTLS 1.1 by returning
     * an error. */
    if (dtlsv == ((~0x0101) & 0xffff)) {
        return 0;
    }
    if (dtlsv == SSL_LIBRARY_VERSION_DTLS_1_2_WIRE) {
        return SSL_LIBRARY_VERSION_TLS_1_2;
    }
    if (dtlsv == SSL_LIBRARY_VERSION_DTLS_1_3_WIRE) {
        return SSL_LIBRARY_VERSION_TLS_1_3;
    }

    /* Return a fictional higher version than we know of */
    return SSL_LIBRARY_VERSION_MAX_SUPPORTED + 1;
}

/* On this socket, Disable non-DTLS cipher suites in the argument's list */
SECStatus
ssl3_DisableNonDTLSSuites(sslSocket *ss)
{
    const ssl3CipherSuite *suite;

    for (suite = nonDTLSSuites; *suite; ++suite) {
        PORT_CheckSuccess(ssl3_CipherPrefSet(ss, *suite, PR_FALSE));
    }
    return SECSuccess;
}

/* Allocate a DTLSQueuedMessage.
 *
 * Called from dtls_QueueMessage()
 */
static DTLSQueuedMessage *
dtls_AllocQueuedMessage(ssl3CipherSpec *cwSpec, SSLContentType ct,
                        const unsigned char *data, PRUint32 len)
{
    DTLSQueuedMessage *msg;

    msg = PORT_ZNew(DTLSQueuedMessage);
    if (!msg)
        return NULL;

    msg->data = PORT_Alloc(len);
    if (!msg->data) {
        PORT_Free(msg);
        return NULL;
    }
    PORT_Memcpy(msg->data, data, len);

    msg->len = len;
    msg->cwSpec = cwSpec;
    msg->type = ct;
    /* Safe if we are < 1.3, since the refct is
     * already very high. */
    ssl_CipherSpecAddRef(cwSpec);

    return msg;
}

/*
 * Free a handshake message
 *
 * Called from dtls_FreeHandshakeMessages()
 */
void
dtls_FreeHandshakeMessage(DTLSQueuedMessage *msg)
{
    if (!msg)
        return;

    /* Safe if we are < 1.3, since the refct is
     * already very high. */
    ssl_CipherSpecRelease(msg->cwSpec);
    PORT_ZFree(msg->data, msg->len);
    PORT_Free(msg);
}

/*
 * Free a list of handshake messages
 *
 * Called from:
 *              dtls_HandleHandshake()
 *              ssl3_DestroySSL3Info()
 */
void
dtls_FreeHandshakeMessages(PRCList *list)
{
    PRCList *cur_p;

    while (!PR_CLIST_IS_EMPTY(list)) {
        cur_p = PR_LIST_TAIL(list);
        PR_REMOVE_LINK(cur_p);
        dtls_FreeHandshakeMessage((DTLSQueuedMessage *)cur_p);
    }
}

/* Called by dtls_HandleHandshake() and dtls_MaybeRetransmitHandshake() if a
 * handshake message retransmission is detected. */
static SECStatus
dtls_RetransmitDetected(sslSocket *ss)
{
    dtlsTimer *rtTimer = ss->ssl3.hs.rtTimer;
    dtlsTimer *hdTimer = ss->ssl3.hs.hdTimer;
    SECStatus rv = SECSuccess;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (rtTimer->cb == dtls_RetransmitTimerExpiredCb) {
        /* Check to see if we retransmitted recently. If so,
         * suppress the triggered retransmit. This avoids
         * retransmit wars after packet loss.
         * This is not in RFC 5346 but it should be.
         */
        if ((PR_IntervalNow() - rtTimer->started) >
            (rtTimer->timeout / 4)) {
            SSL_TRC(30,
                    ("%d: SSL3[%d]: Shortcutting retransmit timer",
                     SSL_GETPID(), ss->fd));

            /* Cancel the timer and call the CB,
             * which re-arms the timer */
            dtls_CancelTimer(ss, ss->ssl3.hs.rtTimer);
            dtls_RetransmitTimerExpiredCb(ss);
        } else {
            SSL_TRC(30,
                    ("%d: SSL3[%d]: Ignoring retransmission: "
                     "last retransmission %dms ago, suppressed for %dms",
                     SSL_GETPID(), ss->fd,
                     PR_IntervalNow() - rtTimer->started,
                     rtTimer->timeout / 4));
        }
    } else if (hdTimer->cb == dtls_FinishedTimerCb) {
        SSL_TRC(30, ("%d: SSL3[%d]: Retransmit detected in holddown",
                     SSL_GETPID(), ss->fd));
        /* Retransmit the messages and re-arm the timer
         * Note that we are not backing off the timer here.
         * The spec isn't clear and my reasoning is that this
         * may be a re-ordered packet rather than slowness,
         * so let's be aggressive. */
        dtls_CancelTimer(ss, ss->ssl3.hs.hdTimer);
        rv = dtls_TransmitMessageFlight(ss);
        if (rv == SECSuccess) {
            rv = dtls_StartHolddownTimer(ss);
        }
    } else {
        /* Otherwise handled in dtls13_HandleOutOfEpochRecord. */
        if (ss->version < SSL_LIBRARY_VERSION_TLS_1_3) {
            PORT_Assert(hdTimer->cb == NULL);
        }

        PORT_Assert(rtTimer->cb == NULL);
        /* ... and ignore it. */
    }
    return rv;
}

static SECStatus
dtls_HandleHandshakeMessage(sslSocket *ss, PRUint8 *data, PRBool last)
{
    ss->ssl3.hs.recvdHighWater = -1;

    return ssl3_HandleHandshakeMessage(ss, data, ss->ssl3.hs.msg_len,
                                       last);
}

/* Called only from ssl3_HandleRecord, for each (deciphered) DTLS record.
 * origBuf is the decrypted ssl record content and is expected to contain
 * complete handshake records
 * Caller must hold the handshake and RecvBuf locks.
 *
 * Note that this code uses msg_len for two purposes:
 *
 * (1) To pass the length to ssl3_HandleHandshakeMessage()
 * (2) To carry the length of a message currently being reassembled
 *
 * However, unlike ssl3_HandleHandshake(), it is not used to carry
 * the state of reassembly (i.e., whether one is in progress). That
 * is carried in recvdHighWater and recvdFragments.
 */
#define OFFSET_BYTE(o) (o / 8)
#define OFFSET_MASK(o) (1 << (o % 8))

SECStatus
dtls_HandleHandshake(sslSocket *ss, DTLSEpoch epoch, sslSequenceNumber seqNum,
                     sslBuffer *origBuf)
{
    sslBuffer buf = *origBuf;
    SECStatus rv = SECSuccess;
    PRBool discarded = PR_FALSE;

    ss->ssl3.hs.endOfFlight = PR_FALSE;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    while (buf.len > 0) {
        PRUint8 type;
        PRUint32 message_length;
        PRUint16 message_seq;
        PRUint32 fragment_offset;
        PRUint32 fragment_length;
        PRUint32 offset;

        if (buf.len < 12) {
            PORT_SetError(SSL_ERROR_RX_MALFORMED_HANDSHAKE);
            rv = SECFailure;
            goto loser;
        }

        /* Parse the header */
        type = buf.buf[0];
        message_length = (buf.buf[1] << 16) | (buf.buf[2] << 8) | buf.buf[3];
        message_seq = (buf.buf[4] << 8) | buf.buf[5];
        fragment_offset = (buf.buf[6] << 16) | (buf.buf[7] << 8) | buf.buf[8];
        fragment_length = (buf.buf[9] << 16) | (buf.buf[10] << 8) | buf.buf[11];

#define MAX_HANDSHAKE_MSG_LEN 0x1ffff /* 128k - 1 */
        if (message_length > MAX_HANDSHAKE_MSG_LEN) {
            (void)ssl3_DecodeError(ss);
            PORT_SetError(SSL_ERROR_RX_MALFORMED_HANDSHAKE);
            rv = SECFailure;
            goto loser;
        }
#undef MAX_HANDSHAKE_MSG_LEN

        buf.buf += 12;
        buf.len -= 12;

        /* This fragment must be complete */
        if (buf.len < fragment_length) {
            PORT_SetError(SSL_ERROR_RX_MALFORMED_HANDSHAKE);
            rv = SECFailure;
            goto loser;
        }

        /* Sanity check the packet contents */
        if ((fragment_length + fragment_offset) > message_length) {
            PORT_SetError(SSL_ERROR_RX_MALFORMED_HANDSHAKE);
            rv = SECFailure;
            goto loser;
        }

        /* If we're a server and we receive what appears to be a retried
         * ClientHello, and we are expecting a ClientHello, move the receive
         * sequence number forward.  This allows for a retried ClientHello if we
         * send a stateless HelloRetryRequest. */
        if (message_seq > ss->ssl3.hs.recvMessageSeq &&
            message_seq == 1 &&
            fragment_offset == 0 &&
            ss->ssl3.hs.ws == wait_client_hello &&
            (SSLHandshakeType)type == ssl_hs_client_hello) {
            SSL_TRC(5, ("%d: DTLS[%d]: Received apparent 2nd ClientHello",
                        SSL_GETPID(), ss->fd));
            ss->ssl3.hs.recvMessageSeq = 1;
            ss->ssl3.hs.helloRetry = PR_TRUE;
        }

        /* There are three ways we could not be ready for this packet.
         *
         * 1. It's a partial next message.
         * 2. It's a partial or complete message beyond the next
         * 3. It's a message we've already seen
         *
         * If it's the complete next message we accept it right away.
         * This is the common case for short messages
         */
        if ((message_seq == ss->ssl3.hs.recvMessageSeq) &&
            (fragment_offset == 0) &&
            (fragment_length == message_length)) {
            /* Complete next message. Process immediately */
            ss->ssl3.hs.msg_type = (SSLHandshakeType)type;
            ss->ssl3.hs.msg_len = message_length;

            rv = dtls_HandleHandshakeMessage(ss, buf.buf,
                                             buf.len == fragment_length);
            if (rv != SECSuccess) {
                goto loser;
            }
        } else {
            if (message_seq < ss->ssl3.hs.recvMessageSeq) {
                /* Case 3: we do an immediate retransmit if we're
                 * in a waiting state. */
                rv = dtls_RetransmitDetected(ss);
                goto loser;
            } else if (message_seq > ss->ssl3.hs.recvMessageSeq) {
                /* Case 2
                 *
                 * Ignore this message. This means we don't handle out of
                 * order complete messages that well, but we're still
                 * compliant and this probably does not happen often
                 *
                 * XXX OK for now. Maybe do something smarter at some point?
                 */
                SSL_TRC(10, ("%d: SSL3[%d]: dtls_HandleHandshake, discarding handshake message",
                             SSL_GETPID(), ss->fd));
                discarded = PR_TRUE;
            } else {
                PRInt32 end = fragment_offset + fragment_length;

                /* Case 1
                 *
                 * Buffer the fragment for reassembly
                 */
                /* Make room for the message */
                if (ss->ssl3.hs.recvdHighWater == -1) {
                    PRUint32 map_length = OFFSET_BYTE(message_length) + 1;

                    rv = sslBuffer_Grow(&ss->ssl3.hs.msg_body, message_length);
                    if (rv != SECSuccess)
                        goto loser;
                    /* Make room for the fragment map */
                    rv = sslBuffer_Grow(&ss->ssl3.hs.recvdFragments,
                                        map_length);
                    if (rv != SECSuccess)
                        goto loser;

                    /* Reset the reassembly map */
                    ss->ssl3.hs.recvdHighWater = 0;
                    PORT_Memset(ss->ssl3.hs.recvdFragments.buf, 0,
                                ss->ssl3.hs.recvdFragments.space);
                    ss->ssl3.hs.msg_type = (SSLHandshakeType)type;
                    ss->ssl3.hs.msg_len = message_length;
                }

                /* If we have a message length mismatch, abandon the reassembly
                 * in progress and hope that the next retransmit will give us
                 * something sane
                 */
                if (message_length != ss->ssl3.hs.msg_len) {
                    ss->ssl3.hs.recvdHighWater = -1;
                    PORT_SetError(SSL_ERROR_RX_MALFORMED_HANDSHAKE);
                    rv = SECFailure;
                    goto loser;
                }

                /* Now copy this fragment into the buffer. */
                if (end > ss->ssl3.hs.recvdHighWater) {
                    PORT_Memcpy(ss->ssl3.hs.msg_body.buf + fragment_offset,
                                buf.buf, fragment_length);
                }

                /* This logic is a bit tricky. We have two values for
                 * reassembly state:
                 *
                 * - recvdHighWater contains the highest contiguous number of
                 *   bytes received
                 * - recvdFragments contains a bitmask of packets received
                 *   above recvdHighWater
                 *
                 * This avoids having to fill in the bitmask in the common
                 * case of adjacent fragments received in sequence
                 */
                if (fragment_offset <= (unsigned int)ss->ssl3.hs.recvdHighWater) {
                    /* Either this is the adjacent fragment or an overlapping
                     * fragment */
                    if (end > ss->ssl3.hs.recvdHighWater) {
                        ss->ssl3.hs.recvdHighWater = end;
                    }
                } else {
                    for (offset = fragment_offset; offset < end; offset++) {
                        ss->ssl3.hs.recvdFragments.buf[OFFSET_BYTE(offset)] |=
                            OFFSET_MASK(offset);
                    }
                }

                /* Now figure out the new high water mark if appropriate */
                for (offset = ss->ssl3.hs.recvdHighWater;
                     offset < ss->ssl3.hs.msg_len; offset++) {
                    /* Note that this loop is not efficient, since it counts
                     * bit by bit. If we have a lot of out-of-order packets,
                     * we should optimize this */
                    if (ss->ssl3.hs.recvdFragments.buf[OFFSET_BYTE(offset)] &
                        OFFSET_MASK(offset)) {
                        ss->ssl3.hs.recvdHighWater++;
                    } else {
                        break;
                    }
                }

                /* If we have all the bytes, then we are good to go */
                if (ss->ssl3.hs.recvdHighWater == ss->ssl3.hs.msg_len) {
                    rv = dtls_HandleHandshakeMessage(ss, ss->ssl3.hs.msg_body.buf,
                                                     buf.len == fragment_length);

                    if (rv != SECSuccess) {
                        goto loser;
                    }
                }
            }
        }

        buf.buf += fragment_length;
        buf.len -= fragment_length;
    }

    // This should never happen, but belt and suspenders.
    if (rv != SECSuccess) {
        PORT_Assert(0);
        goto loser;
    }

    /* If we processed all the fragments in this message, then mark it as remembered.
     * TODO(ekr@rtfm.com): Store out of order messages for DTLS 1.3 so ACKs work
     * better. Bug 1392620.*/
    if (!discarded && tls13_MaybeTls13(ss)) {
        rv = dtls13_RememberFragment(ss, &ss->ssl3.hs.dtlsRcvdHandshake,
                                     0, 0, 0, epoch, seqNum);
    }
    if (rv != SECSuccess) {
        goto loser;
    }
    rv = dtls13_SetupAcks(ss);

loser:
    origBuf->len = 0; /* So ssl3_GatherAppDataRecord will keep looping. */
    return rv;
}

/* Enqueue a message (either handshake or CCS)
 *
 * Called from:
 *              dtls_StageHandshakeMessage()
 *              ssl3_SendChangeCipherSpecs()
 */
SECStatus
dtls_QueueMessage(sslSocket *ss, SSLContentType ct,
                  const PRUint8 *pIn, PRInt32 nIn)
{
    SECStatus rv = SECSuccess;
    DTLSQueuedMessage *msg = NULL;
    ssl3CipherSpec *spec;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));

    spec = ss->ssl3.cwSpec;
    msg = dtls_AllocQueuedMessage(spec, ct, pIn, nIn);

    if (!msg) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        rv = SECFailure;
    } else {
        PR_APPEND_LINK(&msg->link, &ss->ssl3.hs.lastMessageFlight);
    }

    return rv;
}

/* Add DTLS handshake message to the pending queue
 * Empty the sendBuf buffer.
 * Always set sendBuf.len to 0, even when returning SECFailure.
 *
 * Called from:
 *              ssl3_AppendHandshakeHeader()
 *              dtls_FlushHandshake()
 */
SECStatus
dtls_StageHandshakeMessage(sslSocket *ss)
{
    SECStatus rv = SECSuccess;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));

    /* This function is sometimes called when no data is actually to
     * be staged, so just return SECSuccess. */
    if (!ss->sec.ci.sendBuf.buf || !ss->sec.ci.sendBuf.len)
        return rv;

    rv = dtls_QueueMessage(ss, ssl_ct_handshake,
                           ss->sec.ci.sendBuf.buf, ss->sec.ci.sendBuf.len);

    /* Whether we succeeded or failed, toss the old handshake data. */
    ss->sec.ci.sendBuf.len = 0;
    return rv;
}

/* Enqueue the handshake message in sendBuf (if any) and then
 * transmit the resulting flight of handshake messages.
 *
 * Called from:
 *              ssl3_FlushHandshake()
 */
SECStatus
dtls_FlushHandshakeMessages(sslSocket *ss, PRInt32 flags)
{
    SECStatus rv = SECSuccess;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));

    rv = dtls_StageHandshakeMessage(ss);
    if (rv != SECSuccess) {
        return rv;
    }

    if (!(flags & ssl_SEND_FLAG_FORCE_INTO_BUFFER)) {
        rv = dtls_TransmitMessageFlight(ss);
        if (rv != SECSuccess) {
            return rv;
        }

        if (!(flags & ssl_SEND_FLAG_NO_RETRANSMIT)) {
            rv = dtls_StartRetransmitTimer(ss);
        } else {
            PORT_Assert(ss->version < SSL_LIBRARY_VERSION_TLS_1_3);
        }
    }

    return rv;
}

/* The callback for when the retransmit timer expires
 *
 * Called from:
 *              dtls_CheckTimer()
 *              dtls_HandleHandshake()
 */
static void
dtls_RetransmitTimerExpiredCb(sslSocket *ss)
{
    SECStatus rv;
    dtlsTimer *timer = ss->ssl3.hs.rtTimer;
    ss->ssl3.hs.rtRetries++;

    if (!(ss->ssl3.hs.rtRetries % 3)) {
        /* If one of the messages was potentially greater than > MTU,
         * then downgrade. Do this every time we have retransmitted a
         * message twice, per RFC 9147 Sec. 4.4 */
        dtls_SetMTU(ss, ss->ssl3.hs.maxMessageSent - 1);
    }

    rv = dtls_TransmitMessageFlight(ss);
    if (rv == SECSuccess) {
        /* Re-arm the timer */
        timer->timeout *= 2;
        if (timer->timeout > DTLS_RETRANSMIT_MAX_MS) {
            timer->timeout = DTLS_RETRANSMIT_MAX_MS;
        }

        timer->started = PR_IntervalNow();
        timer->cb = dtls_RetransmitTimerExpiredCb;

        SSL_TRC(30,
                ("%d: SSL3[%d]: Retransmit #%d, next in %d",
                 SSL_GETPID(), ss->fd,
                 ss->ssl3.hs.rtRetries, timer->timeout));
    }
    /* else: OK for now. In future maybe signal the stack that we couldn't
     * transmit. For now, let the read handle any real network errors */
}

#define DTLS_HS_HDR_LEN 12
#define DTLS_MIN_FRAGMENT (DTLS_HS_HDR_LEN + 1 + DTLS_MAX_EXPANSION)

/* Encrypt and encode a handshake message fragment.  Flush the data out to the
 * network if there is insufficient space for any fragment. */
static SECStatus
dtls_SendFragment(sslSocket *ss, DTLSQueuedMessage *msg, PRUint8 *data,
                  unsigned int len)
{
    PRInt32 sent;
    SECStatus rv;

    PRINT_BUF(40, (ss, "dtls_SendFragment", data, len));
    sent = ssl3_SendRecord(ss, msg->cwSpec, msg->type, data, len,
                           ssl_SEND_FLAG_FORCE_INTO_BUFFER);
    if (sent != len) {
        if (sent != -1) {
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        }
        return SECFailure;
    }

    /* If another fragment won't fit, flush. */
    if (ss->ssl3.mtu < ss->pendingBuf.len + DTLS_MIN_FRAGMENT) {
        SSL_TRC(20, ("%d: DTLS[%d]: dtls_SendFragment: flush",
                     SSL_GETPID(), ss->fd));
        rv = dtls_SendSavedWriteData(ss);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }
    return SECSuccess;
}

/* Fragment a handshake message into multiple records and send them. */
static SECStatus
dtls_FragmentHandshake(sslSocket *ss, DTLSQueuedMessage *msg)
{
    PRBool fragmentWritten = PR_FALSE;
    PRUint16 msgSeq;
    PRUint8 *fragment;
    PRUint32 fragmentOffset = 0;
    PRUint32 fragmentLen;
    const PRUint8 *content;
    PRUint32 contentLen;
    SECStatus rv;

    /* The headers consume 12 bytes so the smallest possible message (i.e., an
     * empty one) is 12 bytes.  A shorter message would underflow contentLen
     * and cause an out-of-bounds read, so reject it. */
    if (msg->len < DTLS_HS_HDR_LEN) {
        PORT_Assert(0);
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    content = msg->data + DTLS_HS_HDR_LEN;
    contentLen = msg->len - DTLS_HS_HDR_LEN;

    /* DTLS only supports fragmenting handshaking messages. */
    PORT_Assert(msg->type == ssl_ct_handshake);

    msgSeq = (msg->data[4] << 8) | msg->data[5];

    /* do {} while() so that empty messages are sent at least once. */
    do {
        PRUint8 buf[DTLS_MAX_MTU]; /* >= than largest plausible MTU */
        PRBool hasUnackedRange;
        PRUint32 end;

        hasUnackedRange = dtls_NextUnackedRange(ss, msgSeq,
                                                fragmentOffset, contentLen,
                                                &fragmentOffset, &end);
        if (!hasUnackedRange) {
            SSL_TRC(20, ("%d: SSL3[%d]: FragmentHandshake %d: all acknowledged",
                         SSL_GETPID(), ss->fd, msgSeq));
            break;
        }

        SSL_TRC(20, ("%d: SSL3[%d]: FragmentHandshake %d: unacked=%u-%u",
                     SSL_GETPID(), ss->fd, msgSeq, fragmentOffset, end));

        /* Cut down to the data we have available. */
        PORT_Assert(fragmentOffset <= contentLen);
        PORT_Assert(fragmentOffset <= end);
        PORT_Assert(end <= contentLen);
        fragmentLen = PR_MIN(end, contentLen) - fragmentOffset;

        /* Limit further by the record size limit.  Account for the header. */
        fragmentLen = PR_MIN(fragmentLen,
                             msg->cwSpec->recordSizeLimit - DTLS_HS_HDR_LEN);

        /* Reduce to the space remaining in the MTU. */
        fragmentLen = PR_MIN(fragmentLen,
                             ss->ssl3.mtu -           /* MTU estimate. */
                                 ss->pendingBuf.len - /* Less any unsent records. */
                                 DTLS_MAX_EXPANSION - /* Allow for expansion. */
                                 DTLS_HS_HDR_LEN);    /* And the handshake header. */
        PORT_Assert(fragmentLen > 0 || fragmentOffset == 0);

        /* Make totally sure that we will fit in the buffer. This should be
         * impossible; DTLS_MAX_MTU should always be more than ss->ssl3.mtu. */
        if (fragmentLen >= (DTLS_MAX_MTU - DTLS_HS_HDR_LEN)) {
            PORT_Assert(0);
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }

        if (fragmentLen == contentLen) {
            fragment = msg->data;
        } else {
            sslBuffer tmp = SSL_BUFFER_FIXED(buf, sizeof(buf));

            /* Construct an appropriate-sized fragment */
            /* Type, length, sequence */
            rv = sslBuffer_Append(&tmp, msg->data, 6);
            if (rv != SECSuccess) {
                return SECFailure;
            }
            /* Offset. */
            rv = sslBuffer_AppendNumber(&tmp, fragmentOffset, 3);
            if (rv != SECSuccess) {
                return SECFailure;
            }
            /* Length. */
            rv = sslBuffer_AppendNumber(&tmp, fragmentLen, 3);
            if (rv != SECSuccess) {
                return SECFailure;
            }
            /* Data. */
            rv = sslBuffer_Append(&tmp, content + fragmentOffset, fragmentLen);
            if (rv != SECSuccess) {
                return SECFailure;
            }

            fragment = SSL_BUFFER_BASE(&tmp);
        }

        /* Record that we are sending first, because encrypting
         * increments the sequence number. */
        rv = dtls13_RememberFragment(ss, &ss->ssl3.hs.dtlsSentHandshake,
                                     msgSeq, fragmentOffset, fragmentLen,
                                     msg->cwSpec->epoch,
                                     msg->cwSpec->nextSeqNum);
        if (rv != SECSuccess) {
            return SECFailure;
        }

        rv = dtls_SendFragment(ss, msg, fragment,
                               fragmentLen + DTLS_HS_HDR_LEN);
        if (rv != SECSuccess) {
            return SECFailure;
        }

        fragmentWritten = PR_TRUE;
        fragmentOffset += fragmentLen;
    } while (fragmentOffset < contentLen);

    if (!fragmentWritten) {
        /* Nothing was written if we got here, so the whole message must have
         * been acknowledged.  Discard it. */
        SSL_TRC(10, ("%d: SSL3[%d]: FragmentHandshake %d: removed",
                     SSL_GETPID(), ss->fd, msgSeq));
        PR_REMOVE_LINK(&msg->link);
        dtls_FreeHandshakeMessage(msg);
    }

    return SECSuccess;
}

/* Transmit a flight of handshake messages, stuffing them
 * into as few records as seems reasonable.
 *
 * TODO: Space separate UDP packets out a little.
 *
 * Called from:
 *             dtls_FlushHandshake()
 *             dtls_RetransmitTimerExpiredCb()
 */
SECStatus
dtls_TransmitMessageFlight(sslSocket *ss)
{
    SECStatus rv = SECSuccess;
    PRCList *msg_p;

    SSL_TRC(10, ("%d: SSL3[%d]: dtls_TransmitMessageFlight",
                 SSL_GETPID(), ss->fd));

    ssl_GetXmitBufLock(ss);
    ssl_GetSpecReadLock(ss);

    /* DTLS does not buffer its handshake messages in ss->pendingBuf, but rather
     * in the lastMessageFlight structure. This is just a sanity check that some
     * programming error hasn't inadvertantly stuffed something in
     * ss->pendingBuf.  This function uses ss->pendingBuf temporarily and it
     * needs to be empty to start.
     */
    PORT_Assert(!ss->pendingBuf.len);

    for (msg_p = PR_LIST_HEAD(&ss->ssl3.hs.lastMessageFlight);
         msg_p != &ss->ssl3.hs.lastMessageFlight;) {
        DTLSQueuedMessage *msg = (DTLSQueuedMessage *)msg_p;

        /* Move the pointer forward so that the functions below are free to
         * remove messages from the list. */
        msg_p = PR_NEXT_LINK(msg_p);

        /* Note: This function fragments messages so that each record is close
         * to full.  This produces fewer records, but it means that messages can
         * be quite fragmented.  Adding an extra flush here would push new
         * messages into new records and reduce fragmentation. */
        if (msg->type == ssl_ct_handshake) {
            rv = dtls_FragmentHandshake(ss, msg);
        } else {
            PORT_Assert(!tls13_MaybeTls13(ss));
            rv = dtls_SendFragment(ss, msg, msg->data, msg->len);
        }
        if (rv != SECSuccess) {
            break;
        }
    }

    /* Finally, flush any data that wasn't flushed already. */
    if (rv == SECSuccess) {
        rv = dtls_SendSavedWriteData(ss);
    }

    /* Give up the locks */
    ssl_ReleaseSpecReadLock(ss);
    ssl_ReleaseXmitBufLock(ss);

    return rv;
}

/* Flush the data in the pendingBuf and update the max message sent
 * so we can adjust the MTU estimate if we need to.
 * Wrapper for ssl_SendSavedWriteData.
 *
 * Called from dtls_TransmitMessageFlight()
 */
static SECStatus
dtls_SendSavedWriteData(sslSocket *ss)
{
    PRInt32 sent;

    sent = ssl_SendSavedWriteData(ss);
    if (sent < 0)
        return SECFailure;

    /* We should always have complete writes b/c datagram sockets
     * don't really block */
    if (ss->pendingBuf.len > 0) {
        ssl_MapLowLevelError(SSL_ERROR_SOCKET_WRITE_FAILURE);
        return SECFailure;
    }

    /* Update the largest message sent so we can adjust the MTU
     * estimate if necessary */
    if (sent > ss->ssl3.hs.maxMessageSent)
        ss->ssl3.hs.maxMessageSent = sent;

    return SECSuccess;
}

void
dtls_InitTimers(sslSocket *ss)
{
    unsigned int i;
    dtlsTimer **timers[PR_ARRAY_SIZE(ss->ssl3.hs.timers)] = {
        &ss->ssl3.hs.rtTimer,
        &ss->ssl3.hs.ackTimer,
        &ss->ssl3.hs.hdTimer
    };
    static const char *timerLabels[] = {
        "retransmit", "ack", "holddown"
    };

    PORT_Assert(PR_ARRAY_SIZE(timers) == PR_ARRAY_SIZE(timerLabels));
    for (i = 0; i < PR_ARRAY_SIZE(ss->ssl3.hs.timers); ++i) {
        *timers[i] = &ss->ssl3.hs.timers[i];
        ss->ssl3.hs.timers[i].label = timerLabels[i];
    }
}

SECStatus
dtls_StartTimer(sslSocket *ss, dtlsTimer *timer, PRUint32 time, DTLSTimerCb cb)
{
    PORT_Assert(timer->cb == NULL);

    SSL_TRC(10, ("%d: SSL3[%d]: %s dtls_StartTimer %s timeout=%d",
                 SSL_GETPID(), ss->fd, SSL_ROLE(ss), timer->label, time));

    timer->started = PR_IntervalNow();
    timer->timeout = time;
    timer->cb = cb;
    return SECSuccess;
}

SECStatus
dtls_RestartTimer(sslSocket *ss, dtlsTimer *timer)
{
    timer->started = PR_IntervalNow();
    return SECSuccess;
}

PRBool
dtls_TimerActive(sslSocket *ss, dtlsTimer *timer)
{
    return timer->cb != NULL;
}

/* Start a timer for retransmission. */
static SECStatus
dtls_StartRetransmitTimer(sslSocket *ss)
{
    dtlsTimer *timer = ss->ssl3.hs.rtTimer;
    PRUint32 timeout = DTLS_RETRANSMIT_INITIAL_MS;

    if (dtls_TimerActive(ss, timer)) {
        SSL_TRC(10, ("%d: SSL3[%d]: %s dtls timer %s is already active, restarting. New timeout is %d",
                     SSL_GETPID(), ss->fd, SSL_ROLE(ss),
                     timer->label, timeout));
        // If a post-handshake message has already been sent (thus activating the
        // timer) and a second one is queued, reset the timer so no message waits
        // longer than the minimum delay. The first message is retransmitted a
        // bit more aggressively than it otherwise would be, but this is
        // unlikely to be a problem.
        (void)dtls_RestartTimer(ss, timer);
        ss->ssl3.hs.rtRetries = 0;
        timer->timeout = timeout;
        return SECSuccess;
    }

    ss->ssl3.hs.rtRetries = 0;
    return dtls_StartTimer(ss, timer,
                           timeout,
                           dtls_RetransmitTimerExpiredCb);
}

/* Start a timer for holding an old cipher spec. */
SECStatus
dtls_StartHolddownTimer(sslSocket *ss)
{
    /* DTLS1.3 starts the timer without calling this function, see tls13_ServerHandleFinished.*/
    PORT_Assert(ss->version < SSL_LIBRARY_VERSION_TLS_1_3);
    return dtls_StartTimer(ss, ss->ssl3.hs.hdTimer,
                           DTLS_RETRANSMIT_FINISHED_MS,
                           dtls_FinishedTimerCb);
}

/* Cancel a pending timer
 *
 * Called from:
 *              dtls_HandleHandshake()
 *              dtls_CheckTimer()
 */
void
dtls_CancelTimer(sslSocket *ss, dtlsTimer *timer)
{
    SSL_TRC(30, ("%d: SSL3[%d]: %s dtls_CancelTimer %s",
                 SSL_GETPID(), ss->fd, SSL_ROLE(ss),
                 timer->label));

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));

    timer->cb = NULL;
}

static void
dtls_CancelAllTimers(sslSocket *ss)
{
    unsigned int i;

    for (i = 0; i < PR_ARRAY_SIZE(ss->ssl3.hs.timers); ++i) {
        dtls_CancelTimer(ss, &ss->ssl3.hs.timers[i]);
    }
}

/* Check the pending timer and fire the callback if it expired
 *
 * Called from ssl3_GatherCompleteHandshake()
 */
void
dtls_CheckTimer(sslSocket *ss)
{
    unsigned int i;
    SSL_TRC(30, ("%d: SSL3[%d]: dtls_CheckTimer (%s)",
                 SSL_GETPID(), ss->fd, ss->sec.isServer ? "server" : "client"));

    ssl_GetSSL3HandshakeLock(ss);

    for (i = 0; i < PR_ARRAY_SIZE(ss->ssl3.hs.timers); ++i) {
        dtlsTimer *timer = &ss->ssl3.hs.timers[i];
        if (!timer->cb) {
            continue;
        }

        if ((PR_IntervalNow() - timer->started) >=
            PR_MillisecondsToInterval(timer->timeout)) {
            /* Timer has expired */
            DTLSTimerCb cb = timer->cb;

            SSL_TRC(10, ("%d: SSL3[%d]: %s firing timer %s",
                         SSL_GETPID(), ss->fd, SSL_ROLE(ss),
                         timer->label));

            /* Cancel the timer so that we can call the CB safely */
            dtls_CancelTimer(ss, timer);

            /* Now call the CB */
            cb(ss);
        }
    }
    ssl_ReleaseSSL3HandshakeLock(ss);
}

/* The callback to fire when the holddown timer for the Finished
 * message expires and we can delete it
 *
 * Called from dtls_CheckTimer()
 */
static void
dtls_FinishedTimerCb(sslSocket *ss)
{
    dtls_FreeHandshakeMessages(&ss->ssl3.hs.lastMessageFlight);
}

/* Cancel the Finished hold-down timer and destroy the
 * pending cipher spec. Note that this means that
 * successive rehandshakes will fail if the Finished is
 * lost.
 *
 * XXX OK for now. Figure out how to handle the combination
 * of Finished lost and rehandshake
 */
void
dtls_RehandshakeCleanup(sslSocket *ss)
{
    /* Skip this if we are handling a second ClientHello. */
    if (ss->ssl3.hs.helloRetry) {
        return;
    }
    PORT_Assert((ss->version < SSL_LIBRARY_VERSION_TLS_1_3));
    dtls_CancelAllTimers(ss);
    dtls_FreeHandshakeMessages(&ss->ssl3.hs.lastMessageFlight);
    ss->ssl3.hs.sendMessageSeq = 0;
    ss->ssl3.hs.recvMessageSeq = 0;
}

/* Set the MTU to the next step less than or equal to the
 * advertised value. Also used to downgrade the MTU by
 * doing dtls_SetMTU(ss, biggest packet set).
 *
 * Passing 0 means set this to the largest MTU known
 * (effectively resetting the PMTU backoff value).
 *
 * Called by:
 *            ssl3_InitState()
 *            dtls_RetransmitTimerExpiredCb()
 */
void
dtls_SetMTU(sslSocket *ss, PRUint16 advertised)
{
    int i;

    if (advertised == 0) {
        ss->ssl3.mtu = COMMON_MTU_VALUES[0];
        SSL_TRC(30, ("Resetting MTU to %d", ss->ssl3.mtu));
        return;
    }

    for (i = 0; i < PR_ARRAY_SIZE(COMMON_MTU_VALUES); i++) {
        if (COMMON_MTU_VALUES[i] <= advertised) {
            ss->ssl3.mtu = COMMON_MTU_VALUES[i];
            SSL_TRC(30, ("Resetting MTU to %d", ss->ssl3.mtu));
            return;
        }
    }

    /* Fallback */
    ss->ssl3.mtu = COMMON_MTU_VALUES[PR_ARRAY_SIZE(COMMON_MTU_VALUES) - 1];
    SSL_TRC(30, ("Resetting MTU to %d", ss->ssl3.mtu));
}

/* Called from ssl3_HandleHandshakeMessage() when it has deciphered a
 * DTLS hello_verify_request
 * Caller must hold Handshake and RecvBuf locks.
 */
SECStatus
dtls_HandleHelloVerifyRequest(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    int errCode = SSL_ERROR_RX_MALFORMED_HELLO_VERIFY_REQUEST;
    SECStatus rv;
    SSL3ProtocolVersion version;
    SSL3AlertDescription desc = illegal_parameter;

    SSL_TRC(3, ("%d: SSL3[%d]: handle hello_verify_request handshake",
                SSL_GETPID(), ss->fd));
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (ss->ssl3.hs.ws != wait_server_hello) {
        errCode = SSL_ERROR_RX_UNEXPECTED_HELLO_VERIFY_REQUEST;
        desc = unexpected_message;
        goto alert_loser;
    }

    dtls_ReceivedFirstMessageInFlight(ss);

    /* The version.
     *
     * RFC 4347 required that you verify that the server versions
     * match (Section 4.2.1) in the HelloVerifyRequest and the
     * ServerHello.
     *
     * RFC 6347 (Section 4.2.1) suggests (SHOULD) that servers always use 1.0 in
     * HelloVerifyRequest and allows the versions not to match,
     * especially when 1.2 is being negotiated.
     *
     * Therefore we do not do anything to enforce a match, just
     * read and check that this value is sane.
     */
    rv = ssl_ClientReadVersion(ss, &b, &length, &version);
    if (rv != SECSuccess) {
        goto loser; /* alert has been sent */
    }

    /* Read the cookie. */
    SECItem cookie;
    rv = ssl3_ConsumeHandshakeVariable(ss, &cookie, 1, &b, &length);
    if (rv != SECSuccess) {
        goto loser; /* alert has been sent */
    }
    if (cookie.len > DTLS_COOKIE_BYTES) {
        desc = decode_error;
        goto alert_loser; /* malformed. */
    }
    PORT_Assert(!ss->ssl3.hs.cookie.data && !ss->ssl3.hs.cookie.len);
    SECITEM_FreeItem(&ss->ssl3.hs.cookie, PR_FALSE);
    rv = SECITEM_CopyItem(NULL, &ss->ssl3.hs.cookie, &cookie);
    if (rv != SECSuccess) {
        goto loser;
    }

    ss->ssl3.hs.dtlsReceivedHVR = PR_TRUE;

    ssl_GetXmitBufLock(ss); /*******************************/

    /* Now re-send the client hello */
    rv = ssl3_SendClientHello(ss, client_hello_retransmit);

    ssl_ReleaseXmitBufLock(ss); /*******************************/

    SECITEM_FreeItem(&ss->ssl3.hs.cookie, PR_FALSE);

    if (rv == SECSuccess)
        return rv;

alert_loser:
    (void)SSL3_SendAlert(ss, alert_fatal, desc);

loser:
    ssl_MapLowLevelError(errCode);
    return SECFailure;
}

/* Initialize the DTLS anti-replay window
 *
 * Called from:
 *              ssl3_SetupPendingCipherSpec()
 *              ssl3_InitCipherSpec()
 */
void
dtls_InitRecvdRecords(DTLSRecvdRecords *records)
{
    PORT_Memset(records->data, 0, sizeof(records->data));
    records->left = 0;
    records->right = DTLS_RECVD_RECORDS_WINDOW - 1;
}

/*
 * Has this DTLS record been received? Return values are:
 * -1 -- out of range to the left
 *  0 -- not received yet
 *  1 -- replay
 *
 *  Called from: ssl3_HandleRecord()
 */
int
dtls_RecordGetRecvd(const DTLSRecvdRecords *records, sslSequenceNumber seq)
{
    PRUint64 offset;

    /* Out of range to the left */
    if (seq < records->left) {
        return -1;
    }

    /* Out of range to the right; since we advance the window on
     * receipt, that means that this packet has not been received
     * yet */
    if (seq > records->right)
        return 0;

    offset = seq % DTLS_RECVD_RECORDS_WINDOW;

    return !!(records->data[offset / 8] & (1 << (offset % 8)));
}

/* Update the DTLS anti-replay window
 *
 * Called from ssl3_HandleRecord()
 */
void
dtls_RecordSetRecvd(DTLSRecvdRecords *records, sslSequenceNumber seq)
{
    PRUint64 offset;

    if (seq < records->left)
        return;

    if (seq > records->right) {
        sslSequenceNumber new_left;
        sslSequenceNumber new_right;
        sslSequenceNumber right;

        /* Slide to the right; this is the tricky part
         *
         * 1. new_top is set to have room for seq, on the
         *    next byte boundary by setting the right 8
         *    bits of seq
         * 2. new_left is set to compensate.
         * 3. Zero all bits between top and new_top. Since
         *    this is a ring, this zeroes everything as-yet
         *    unseen. Because we always operate on byte
         *    boundaries, we can zero one byte at a time
         */
        new_right = seq | 0x07;
        new_left = (new_right - DTLS_RECVD_RECORDS_WINDOW) + 1;

        if (new_right > records->right + DTLS_RECVD_RECORDS_WINDOW) {
            PORT_Memset(records->data, 0, sizeof(records->data));
        } else {
            for (right = records->right + 8; right <= new_right; right += 8) {
                offset = right % DTLS_RECVD_RECORDS_WINDOW;
                records->data[offset / 8] = 0;
            }
        }

        records->right = new_right;
        records->left = new_left;
    }

    offset = seq % DTLS_RECVD_RECORDS_WINDOW;

    records->data[offset / 8] |= (1 << (offset % 8));
}

SECStatus
DTLS_GetHandshakeTimeout(PRFileDesc *socket, PRIntervalTime *timeout)
{
    sslSocket *ss = NULL;
    PRBool found = PR_FALSE;
    PRIntervalTime now = PR_IntervalNow();
    PRIntervalTime to;
    unsigned int i;

    *timeout = PR_INTERVAL_NO_TIMEOUT;

    ss = ssl_FindSocket(socket);

    if (!ss) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (!IS_DTLS(ss)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    for (i = 0; i < PR_ARRAY_SIZE(ss->ssl3.hs.timers); ++i) {
        PRIntervalTime elapsed;
        PRIntervalTime desired;
        dtlsTimer *timer = &ss->ssl3.hs.timers[i];

        if (!timer->cb) {
            continue;
        }
        found = PR_TRUE;

        elapsed = now - timer->started;
        desired = PR_MillisecondsToInterval(timer->timeout);
        if (elapsed > desired) {
            /* Timer expired */
            *timeout = PR_INTERVAL_NO_WAIT;
            return SECSuccess;
        } else {
            to = desired - elapsed;
        }

        if (*timeout > to) {
            *timeout = to;
        }
    }

    if (!found) {
        PORT_SetError(SSL_ERROR_NO_TIMERS_FOUND);
        return SECFailure;
    }

    return SECSuccess;
}

PRBool
dtls_IsLongHeader(SSL3ProtocolVersion version, PRUint8 firstOctet)
{
#ifndef UNSAFE_FUZZER_MODE
    return version < SSL_LIBRARY_VERSION_TLS_1_3 ||
           firstOctet == ssl_ct_handshake ||
           firstOctet == ssl_ct_ack ||
           firstOctet == ssl_ct_alert;
#else
    return PR_TRUE;
#endif
}

PRBool
dtls_IsDtls13Ciphertext(SSL3ProtocolVersion version, PRUint8 firstOctet)
{
    // Allow no version in case we haven't negotiated one yet.
    return (version == 0 || version >= SSL_LIBRARY_VERSION_TLS_1_3) &&
           (firstOctet & 0xe0) == 0x20;
}

DTLSEpoch
dtls_ReadEpoch(const SSL3ProtocolVersion version, const DTLSEpoch specEpoch, const PRUint8 *hdr)
{
    if (dtls_IsLongHeader(version, hdr[0])) {
        return ((DTLSEpoch)hdr[3] << 8) | hdr[4];
    }

    DTLSEpoch epoch = (specEpoch & ~3) | (hdr[0] & 3);
    /* The epoch cannot be higher than the current read epoch,
        though guard against underflow. */
    if (epoch > specEpoch && epoch > 4) {
        epoch -= 4;
    }

    return epoch;
}

static sslSequenceNumber
dtls_ReadSequenceNumber(const ssl3CipherSpec *spec, const PRUint8 *hdr)
{
    sslSequenceNumber cap;
    sslSequenceNumber partial;
    sslSequenceNumber seqNum;
    sslSequenceNumber mask;

    if (dtls_IsLongHeader(spec->version, hdr[0])) {
        static const unsigned int seqNumOffset = 5; /* type, version, epoch */
        static const unsigned int seqNumLength = 6;
        sslReader r = SSL_READER(hdr + seqNumOffset, seqNumLength);
        (void)sslRead_ReadNumber(&r, seqNumLength, &seqNum);
        return seqNum;
    }

    /* Only the least significant bits of the sequence number is available here.
     * This recovers the value based on the next expected sequence number.
     *
     * This works by determining the maximum possible sequence number, which is
     * half the range of possible values above the expected next value (the
     * expected next value is in |spec->seqNum|).  Then, the last part of the
     * sequence number is replaced.  If that causes the value to exceed the
     * maximum, subtract an entire range.
     */
    if (hdr[0] & 0x08) {
        cap = spec->nextSeqNum + (1ULL << 15);
        partial = (((sslSequenceNumber)hdr[1]) << 8) |
                  (sslSequenceNumber)hdr[2];
        mask = (1ULL << 16) - 1;
    } else {
        cap = spec->nextSeqNum + (1ULL << 7);
        partial = (sslSequenceNumber)hdr[1];
        mask = (1ULL << 8) - 1;
    }
    seqNum = (cap & ~mask) | partial;
    /* The second check prevents the value from underflowing if we get a large
     * gap at the start of a connection, where this subtraction would cause the
     * sequence number to wrap to near UINT64_MAX. */
    if ((partial > (cap & mask)) && (seqNum > mask)) {
        seqNum -= mask + 1;
    }
    return seqNum;
}

/*
 * DTLS relevance checks:
 * Note that this code currently ignores all out-of-epoch packets,
 * which means we lose some in the case of rehandshake +
 * loss/reordering. Since DTLS is explicitly unreliable, this
 * seems like a good tradeoff for implementation effort and is
 * consistent with the guidance of RFC 6347 Sections 4.1 and 4.2.4.1.
 *
 * If the packet is not relevant, this function returns PR_FALSE.  If the packet
 * is relevant, this function returns PR_TRUE and sets |*seqNumOut| to the
 * packet sequence number (removing the epoch).
 */
PRBool
dtls_IsRelevant(sslSocket *ss, const ssl3CipherSpec *spec,
                const SSL3Ciphertext *cText,
                sslSequenceNumber *seqNumOut)
{
    sslSequenceNumber seqNum = dtls_ReadSequenceNumber(spec, cText->hdr);
    if (dtls_RecordGetRecvd(&spec->recvdRecords, seqNum) != 0) {
        SSL_TRC(10, ("%d: SSL3[%d]: dtls_IsRelevant, rejecting "
                     "potentially replayed packet",
                     SSL_GETPID(), ss->fd));
        return PR_FALSE;
    }

    *seqNumOut = seqNum;
    return PR_TRUE;
}

void
dtls_ReceivedFirstMessageInFlight(sslSocket *ss)
{
    if (!IS_DTLS(ss))
        return;

    /* At this point we are advancing our state machine, so we can free our last
     * flight of messages. */
    if (ss->ssl3.hs.ws != idle_handshake ||
        ss->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
        /* We need to keep our last flight around in DTLS 1.2 and below,
         * so we can retransmit it in response to other people's
         * retransmits. */
        dtls_FreeHandshakeMessages(&ss->ssl3.hs.lastMessageFlight);

        /* Reset the timer to the initial value if the retry counter
         * is 0, per RFC 6347, Sec. 4.2.4.1 */
        dtls_CancelTimer(ss, ss->ssl3.hs.rtTimer);
        if (ss->ssl3.hs.rtRetries == 0) {
            ss->ssl3.hs.rtTimer->timeout = DTLS_RETRANSMIT_INITIAL_MS;
        }
    }

    /* Empty the ACK queue (TLS 1.3 only). */
    ssl_ClearPRCList(&ss->ssl3.hs.dtlsRcvdHandshake, NULL);
}
