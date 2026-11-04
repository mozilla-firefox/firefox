/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is PRIVATE to SSL.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nss.h"
#include "prnetdb.h"
#include "ssl.h"
#include "sslimpl.h"
#include "sslproto.h"

/* Helper function to encode an unsigned integer into a buffer. */
static void
ssl_EncodeUintX(PRUint8 *to, PRUint64 value, unsigned int bytes)
{
    PRUint64 encoded;

    PORT_Assert(bytes > 0 && bytes <= sizeof(encoded));

    encoded = PR_htonll(value);
    PORT_Memcpy(to, ((unsigned char *)(&encoded)) + (sizeof(encoded) - bytes),
                bytes);
}

/* Grow a buffer to hold newLen bytes of data.  When used for recv/xmit buffers,
 * the caller must hold xmitBufLock or recvBufLock, as appropriate. */
SECStatus
sslBuffer_Grow(sslBuffer *b, unsigned int newLen)
{
    PORT_Assert(b);
    if (b->fixed) {
        PORT_Assert(newLen <= b->space);
        if (newLen > b->space) {
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }
        return SECSuccess;
    }

    /* If buf is non-NULL, space must be non-zero;
     * if buf is NULL, space must be zero. */
    PORT_Assert((b->buf && b->space) || (!b->buf && !b->space));
    if (newLen > b->space) {
        newLen = PR_MAX(newLen, b->space + 2048);
        unsigned char *newBuf;
        if (b->buf) {
            newBuf = (unsigned char *)PORT_Realloc(b->buf, newLen);
        } else {
            newBuf = (unsigned char *)PORT_Alloc(newLen);
        }
        if (!newBuf) {
            return SECFailure;
        }
        b->buf = newBuf;
        b->space = newLen;
    }
    return SECSuccess;
}

/* Appends len copies of c to b */
SECStatus
sslBuffer_Fill(sslBuffer *b, PRUint8 c, size_t len)
{
    PORT_Assert(b);
    SECStatus rv = sslBuffer_Grow(b, b->len + len);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    if (len > 0) {
        memset(SSL_BUFFER_NEXT(b), c, len);
    }
    b->len += len;
    return SECSuccess;
}

SECStatus
sslBuffer_Append(sslBuffer *b, const void *data, unsigned int len)
{
    SECStatus rv = sslBuffer_Grow(b, b->len + len);
    if (rv != SECSuccess) {
        return SECFailure; /* Code already set. */
    }
    if (len > 0) {
        PORT_Assert(data);
        PORT_Memcpy(SSL_BUFFER_NEXT(b), data, len);
    }
    b->len += len;
    return SECSuccess;
}

SECStatus
sslBuffer_AppendNumber(sslBuffer *b, PRUint64 v, unsigned int size)
{
    SECStatus rv = sslBuffer_Grow(b, b->len + size);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    ssl_EncodeUintX(SSL_BUFFER_NEXT(b), v, size);
    b->len += size;
    return SECSuccess;
}

SECStatus
sslBuffer_AppendVariable(sslBuffer *b, const PRUint8 *data, unsigned int len,
                         unsigned int size)
{
    PORT_Assert(size <= 4 && size > 0);
    PORT_Assert(b);
    if (len >= (1ULL << (8 * size))) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    if (sslBuffer_Grow(b, b->len + len + size) != SECSuccess) {
        return SECFailure;
    }

    ssl_EncodeUintX(SSL_BUFFER_NEXT(b), len, size);
    b->len += size;
    if (len != 0) {
        PORT_Assert(data);
        /* We sometimes pass NULL, 0 and memcpy() doesn't want NULL. */
        PORT_Memcpy(SSL_BUFFER_NEXT(b), data, len);
    }
    b->len += len;
    return SECSuccess;
}

SECStatus
sslBuffer_AppendBuffer(sslBuffer *b, const sslBuffer *append)
{
    return sslBuffer_Append(b, append->buf, append->len);
}

SECStatus
sslBuffer_AppendBufferVariable(sslBuffer *b, const sslBuffer *append,
                               unsigned int size)
{
    return sslBuffer_AppendVariable(b, append->buf, append->len, size);
}

SECStatus
sslBuffer_Skip(sslBuffer *b, unsigned int size, unsigned int *savedOffset)
{
    if (sslBuffer_Grow(b, b->len + size) != SECSuccess) {
        return SECFailure;
    }

    if (savedOffset) {
        *savedOffset = b->len;
    }
    b->len += size;
    return SECSuccess;
}

/* A common problem is that a buffer is used to construct a variable length
 * structure of unknown length.  The length field for that structure is then
 * populated afterwards.  This function makes this process a little easier.
 *
 * To use this, before encoding the variable length structure, skip the spot
 * where the length would be using sslBuffer_Skip().  After encoding the
 * structure, and before encoding anything else, call this function passing the
 * value returned from sslBuffer_Skip() as |at| to have the length inserted.
 */
SECStatus
sslBuffer_InsertLength(sslBuffer *b, unsigned int at, unsigned int size)
{
    unsigned int len;

    PORT_Assert(b->len >= at + size);
    PORT_Assert(b->space >= at + size);
    len = b->len - (at + size);

    PORT_Assert(size <= 4 && size > 0);
    if (len >= (1ULL << (8 * size))) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    ssl_EncodeUintX(SSL_BUFFER_BASE(b) + at, len, size);
    return SECSuccess;
}

SECStatus
sslBuffer_InsertNumber(sslBuffer *b, unsigned int at,
                       PRUint64 v, unsigned int size)
{
    PORT_Assert(b->len >= at + size);
    PORT_Assert(b->space >= at + size);

    PORT_Assert(size <= 4 && size > 0);
    if (v >= (1ULL << (8 * size))) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    ssl_EncodeUintX(SSL_BUFFER_BASE(b) + at, v, size);
    return SECSuccess;
}

void
sslBuffer_Clear(sslBuffer *b)
{
    if (!b->fixed) {
        if (b->buf) {
            PORT_Free(b->buf);
            b->buf = NULL;
        }
        b->space = 0;
    }
    b->len = 0;
}

void
sslBuffer_ZeroClear(sslBuffer *b)
{
    if (b->buf) {
        PORT_SafeZero(b->buf, b->space);
    }
    sslBuffer_Clear(b);
}

SECStatus
sslRead_Read(sslReader *reader, unsigned int count, sslReadBuffer *out)
{
    if (!reader || !out) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (reader->buf.len < reader->offset ||
        count > SSL_READER_REMAINING(reader)) {
        PORT_SetError(SEC_ERROR_BAD_DATA);
        return SECFailure;
    }

    out->buf = SSL_READER_CURRENT(reader);
    out->len = count;
    reader->offset += count;

    return SECSuccess;
}

SECStatus
sslRead_ReadVariable(sslReader *reader, unsigned int sizeLen, sslReadBuffer *out)
{
    PRUint64 variableLen = 0;
    SECStatus rv = sslRead_ReadNumber(reader, sizeLen, &variableLen);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_BAD_DATA);
        return SECFailure;
    }
    if (!variableLen) {
        // It is ok to have an empty variable.
        out->len = variableLen;
        return SECSuccess;
    }
    return sslRead_Read(reader, variableLen, out);
}

SECStatus
sslRead_ReadNumber(sslReader *reader, unsigned int bytes, PRUint64 *num)
{
    if (!reader || !num) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (reader->buf.len < reader->offset ||
        bytes > SSL_READER_REMAINING(reader) ||
        bytes > 8) {
        PORT_SetError(SEC_ERROR_BAD_DATA);
        return SECFailure;
    }
    unsigned int i;
    PRUint64 number = 0;
    for (i = 0; i < bytes; i++) {
        number = (number << 8) + reader->buf.buf[i + reader->offset];
    }

    reader->offset = reader->offset + bytes;
    *num = number;
    return SECSuccess;
}

/**************************************************************************
 * Append Handshake functions.
 * All these functions set appropriate error codes.
 * Most rely on ssl3_AppendHandshake to set the error code.
 **************************************************************************/
#define MAX_SEND_BUF_LENGTH 32000 /* watch for 16-bit integer overflow */
#define MIN_SEND_BUF_LENGTH 4000

static SECStatus
ssl3_AppendHandshakeInternal(sslSocket *ss, const void *void_src, unsigned int bytes, PRBool suppressHash)
{
    unsigned char *src = (unsigned char *)void_src;
    int room = ss->sec.ci.sendBuf.space - ss->sec.ci.sendBuf.len;
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss)); /* protects sendBuf. */

    if (!bytes)
        return SECSuccess;
    if (IS_DTLS(ss)) {
        /* Each flush of sendBuf is queued as a separate DTLSQueuedMessage, and
         * the fragmentation code assumes every queued message is a complete
         * handshake message beginning with a handshake header.  A DTLS
         * handshake message must therefore stay whole in sendBuf and never be
         * flushed partway through, so grow to hold it rather than capping at
         * MAX_SEND_BUF_LENGTH. */
        if (room < bytes) {
            rv = sslBuffer_Grow(&ss->sec.ci.sendBuf,
                                ss->sec.ci.sendBuf.len + bytes);
            if (rv != SECSuccess)
                return SECFailure; /* sslBuffer_Grow sets a memory error code. */
            room = ss->sec.ci.sendBuf.space - ss->sec.ci.sendBuf.len;
        }
    } else if (ss->sec.ci.sendBuf.space < MAX_SEND_BUF_LENGTH && room < bytes) {
        rv = sslBuffer_Grow(&ss->sec.ci.sendBuf, PR_MAX(MIN_SEND_BUF_LENGTH,
                                                        PR_MIN(MAX_SEND_BUF_LENGTH, ss->sec.ci.sendBuf.len + bytes)));
        if (rv != SECSuccess)
            return SECFailure; /* sslBuffer_Grow sets a memory error code. */
        room = ss->sec.ci.sendBuf.space - ss->sec.ci.sendBuf.len;
    }

    PRINT_BUF(60, (ss, "Append to Handshake", (unsigned char *)void_src, bytes));
    // TODO: Move firstHsDone and version check into callers as a suppression.
    if (!suppressHash && (!ss->firstHsDone || ss->version < SSL_LIBRARY_VERSION_TLS_1_3)) {
        rv = ssl3_UpdateHandshakeHashes(ss, src, bytes);
        if (rv != SECSuccess)
            return SECFailure; /* error code set by ssl3_UpdateHandshakeHashes */
    }

    while (bytes > room) {
        if (room > 0)
            PORT_Memcpy(ss->sec.ci.sendBuf.buf + ss->sec.ci.sendBuf.len, src,
                        room);
        ss->sec.ci.sendBuf.len += room;
        rv = ssl3_FlushHandshake(ss, ssl_SEND_FLAG_FORCE_INTO_BUFFER);
        if (rv != SECSuccess) {
            return SECFailure; /* error code set by ssl3_FlushHandshake */
        }
        bytes -= room;
        src += room;
        room = ss->sec.ci.sendBuf.space;
        PORT_Assert(ss->sec.ci.sendBuf.len == 0);
    }
    PORT_Memcpy(ss->sec.ci.sendBuf.buf + ss->sec.ci.sendBuf.len, src, bytes);
    ss->sec.ci.sendBuf.len += bytes;
    return SECSuccess;
}

SECStatus
ssl3_AppendHandshakeSuppressHash(sslSocket *ss, const void *void_src, unsigned int bytes)
{
    return ssl3_AppendHandshakeInternal(ss, void_src, bytes, PR_TRUE);
}

SECStatus
ssl3_AppendHandshake(sslSocket *ss, const void *void_src, unsigned int bytes)
{
    return ssl3_AppendHandshakeInternal(ss, void_src, bytes, PR_FALSE);
}

SECStatus
ssl3_AppendHandshakeNumberSuppressHash(sslSocket *ss, PRUint64 num, unsigned int lenSize, PRBool suppressHash)
{
    if ((lenSize > 8) || ((lenSize < 8) && (num >= (1ULL << (8 * lenSize))))) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    PRUint8 b[sizeof(num)];
    SSL_TRC(60, ("%d: number:", SSL_GETPID()));
    ssl_EncodeUintX(b, num, lenSize);
    return ssl3_AppendHandshakeInternal(ss, b, lenSize, suppressHash);
}

SECStatus
ssl3_AppendHandshakeNumber(sslSocket *ss, PRUint64 num, unsigned int lenSize)
{
    return ssl3_AppendHandshakeNumberSuppressHash(ss, num, lenSize, PR_FALSE);
}

SECStatus
ssl3_AppendHandshakeVariable(sslSocket *ss, const PRUint8 *src,
                             unsigned int bytes, unsigned int lenSize)
{
    SECStatus rv;

    PORT_Assert((bytes < (1 << 8) && lenSize == 1) ||
                (bytes < (1L << 16) && lenSize == 2) ||
                (bytes < (1L << 24) && lenSize == 3));

    SSL_TRC(60, ("%d: append variable:", SSL_GETPID()));
    rv = ssl3_AppendHandshakeNumber(ss, bytes, lenSize);
    if (rv != SECSuccess) {
        return SECFailure; /* error code set by AppendHandshake. */
    }
    SSL_TRC(60, ("data:"));
    return ssl3_AppendHandshake(ss, src, bytes);
}

SECStatus
ssl3_AppendBufferToHandshake(sslSocket *ss, sslBuffer *buf)
{
    return ssl3_AppendHandshake(ss, buf->buf, buf->len);
}

SECStatus
ssl3_AppendBufferToHandshakeVariable(sslSocket *ss, sslBuffer *buf,
                                     unsigned int lenSize)
{
    return ssl3_AppendHandshakeVariable(ss, buf->buf, buf->len, lenSize);
}

SECStatus
ssl3_MaybeUpdateHashWithSavedRecord(sslSocket *ss)
{
    SECStatus rv;
    /* dtls13ClientMessageBuffer is not empty if ClientHello has sent DTLS1.3 */
    if (ss->ssl3.hs.dtls13ClientMessageBuffer.len == 0) {
        return SECSuccess;
    }

    size_t offset = 0;

    /* the first clause checks the version that was received in ServerHello:
     * only if it's DTLS1.3, we remove the necessary fields.
     * the second clause checks if we send 0rtt (see TestTls13ZeroRttDowngrade).
     */
    if ((ss->version == ss->ssl3.cwSpec->version || ss->ssl3.hs.zeroRttState == ssl_0rtt_sent)) {
        if (ss->ssl3.hs.dtls13ClientMessageBuffer.len < 12) {
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            return SECFailure;
        }

        rv = ssl3_UpdateHandshakeHashes(ss, ss->ssl3.hs.dtls13ClientMessageBuffer.buf, 4);
        if (rv != SECSuccess) {
            return SECFailure;
        }
        offset = 12;
    }

    PORT_Assert(offset < ss->ssl3.hs.dtls13ClientMessageBuffer.len);
    rv = ssl3_UpdateHandshakeHashes(ss, ss->ssl3.hs.dtls13ClientMessageBuffer.buf + offset,
                                    ss->ssl3.hs.dtls13ClientMessageBuffer.len - offset);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    sslBuffer_Clear(&ss->ssl3.hs.dtls13ClientMessageBuffer);
    ss->ssl3.hs.dtls13ClientMessageBuffer.len = 0;
    return SECSuccess;
}

SECStatus
ssl3_CopyToSECItem(sslBuffer *buf, SECItem *i)
{
    return SECITEM_MakeItem(NULL, i, buf->buf, buf->len);
}
