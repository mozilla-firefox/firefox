/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file implements the CLIENT Session ID cache.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "cert.h"
#include "pk11pub.h"
#include "secitem.h"
#include "ssl.h"
#include "nss.h"

#include "sslimpl.h"
#include "sslproto.h"
#include "sslencode.h"
#if defined(XP_UNIX) || defined(XP_WIN) || defined(_WINDOWS)
#include <time.h>
#endif

static sslSessionID *cache = NULL;
static PRLock *cacheLock = NULL;

/* sids can be in one of 5 states:
 *
 * never_cached,        created, but not yet put into cache.
 * in_client_cache,     in the client cache's linked list.
 * in_server_cache,     entry came from the server's cache file.
 * invalid_cache        has been removed from the cache.
 * in_external_cache    sid comes from an external cache.
 */

#define LOCK_CACHE lock_cache()
#define UNLOCK_CACHE PR_Unlock(cacheLock)

static SECStatus
ssl_InitClientSessionCacheLock(void)
{
    cacheLock = PR_NewLock();
    return cacheLock ? SECSuccess : SECFailure;
}

static SECStatus
ssl_FreeClientSessionCacheLock(void)
{
    if (cacheLock) {
        PR_DestroyLock(cacheLock);
        cacheLock = NULL;
        return SECSuccess;
    }
    PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
    return SECFailure;
}

static PRBool LocksInitializedEarly = PR_FALSE;

static SECStatus
FreeSessionCacheLocks()
{
    SECStatus rv1, rv2;
    rv1 = ssl_FreeSymWrapKeysLock();
    rv2 = ssl_FreeClientSessionCacheLock();
    if ((SECSuccess == rv1) && (SECSuccess == rv2)) {
        return SECSuccess;
    }
    return SECFailure;
}

static SECStatus
InitSessionCacheLocks(void)
{
    SECStatus rv1, rv2;
    PRErrorCode rc;
    rv1 = ssl_InitSymWrapKeysLock();
    rv2 = ssl_InitClientSessionCacheLock();
    if ((SECSuccess == rv1) && (SECSuccess == rv2)) {
        return SECSuccess;
    }
    rc = PORT_GetError();
    FreeSessionCacheLocks();
    PORT_SetError(rc);
    return SECFailure;
}

/* free the session cache locks if they were initialized early */
SECStatus
ssl_FreeSessionCacheLocks()
{
    PORT_Assert(PR_TRUE == LocksInitializedEarly);
    if (!LocksInitializedEarly) {
        PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
        return SECFailure;
    }
    FreeSessionCacheLocks();
    LocksInitializedEarly = PR_FALSE;
    return SECSuccess;
}

static PRCallOnceType lockOnce;

/* free the session cache locks if they were initialized lazily */
static SECStatus
ssl_ShutdownLocks(void *appData, void *nssData)
{
    PORT_Assert(PR_FALSE == LocksInitializedEarly);
    if (LocksInitializedEarly) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    FreeSessionCacheLocks();
    memset(&lockOnce, 0, sizeof(lockOnce));
    return SECSuccess;
}

static PRStatus
initSessionCacheLocksLazily(void)
{
    SECStatus rv = InitSessionCacheLocks();
    if (SECSuccess != rv) {
        return PR_FAILURE;
    }
    rv = NSS_RegisterShutdown(ssl_ShutdownLocks, NULL);
    PORT_Assert(SECSuccess == rv);
    if (SECSuccess != rv) {
        return PR_FAILURE;
    }
    return PR_SUCCESS;
}

/* lazyInit means that the call is not happening during a 1-time
 * initialization function, but rather during dynamic, lazy initialization
 */
SECStatus
ssl_InitSessionCacheLocks(PRBool lazyInit)
{
    if (LocksInitializedEarly) {
        return SECSuccess;
    }

    if (lazyInit) {
        return (PR_SUCCESS ==
                PR_CallOnce(&lockOnce, initSessionCacheLocksLazily))
                   ? SECSuccess
                   : SECFailure;
    }

    if (SECSuccess == InitSessionCacheLocks()) {
        LocksInitializedEarly = PR_TRUE;
        return SECSuccess;
    }

    return SECFailure;
}

static void
lock_cache(void)
{
    ssl_InitSessionCacheLocks(PR_TRUE);
    PR_Lock(cacheLock);
}

/* BEWARE: This function gets called for both client and server SIDs !!
 * If the unreferenced sid is not in the cache, Free sid and its contents.
 */
void
ssl_DestroySID(sslSessionID *sid, PRBool freeIt)
{
    SSL_TRC(8, ("SSL: destroy sid: sid=0x%x cached=%d", sid, sid->cached));
    PORT_Assert(sid->references == 0);
    PORT_Assert(sid->cached != in_client_cache);

    if (sid->u.ssl3.locked.sessionTicket.ticket.data) {
        SECITEM_FreeItem(&sid->u.ssl3.locked.sessionTicket.ticket,
                         PR_FALSE);
    }
    if (sid->u.ssl3.srvName.data) {
        SECITEM_FreeItem(&sid->u.ssl3.srvName, PR_FALSE);
    }
    if (sid->u.ssl3.signedCertTimestamps.data) {
        SECITEM_FreeItem(&sid->u.ssl3.signedCertTimestamps, PR_FALSE);
    }

    if (sid->u.ssl3.lock) {
        PR_DestroyRWLock(sid->u.ssl3.lock);
    }

    PORT_Free((void *)sid->peerID);
    PORT_Free((void *)sid->urlSvrName);

    if (sid->peerCert) {
        CERT_DestroyCertificate(sid->peerCert);
    }
    if (sid->peerCertStatus.items) {
        SECITEM_FreeArray(&sid->peerCertStatus, PR_FALSE);
    }

    if (sid->localCert) {
        CERT_DestroyCertificate(sid->localCert);
    }

    SECITEM_FreeItem(&sid->u.ssl3.alpnSelection, PR_FALSE);

    if (freeIt) {
        PORT_ZFree(sid, sizeof(sslSessionID));
    }
}

/* BEWARE: This function gets called for both client and server SIDs !!
 * Decrement reference count, and
 *    free sid if ref count is zero, and sid is not in the cache.
 * Does NOT remove from the cache first.
 * If the sid is still in the cache, it is left there until next time
 * the cache list is traversed.
 */
static void
ssl_FreeLockedSID(sslSessionID *sid)
{
    PORT_Assert(sid->references >= 1);
    if (--sid->references == 0) {
        ssl_DestroySID(sid, PR_TRUE);
    }
}

/* BEWARE: This function gets called for both client and server SIDs !!
 * Decrement reference count, and
 *    free sid if ref count is zero, and sid is not in the cache.
 * Does NOT remove from the cache first.
 * These locks are necessary because the sid _might_ be in the cache list.
 */
void
ssl_FreeSID(sslSessionID *sid)
{
    if (sid) {
        LOCK_CACHE;
        ssl_FreeLockedSID(sid);
        UNLOCK_CACHE;
    }
}

sslSessionID *
ssl_ReferenceSID(sslSessionID *sid)
{
    LOCK_CACHE;
    sid->references++;
    UNLOCK_CACHE;
    return sid;
}

/************************************************************************/

/*
**  Lookup sid entry in cache by Address, port, and peerID string.
**  If found, Increment reference count, and return pointer to caller.
**  If it has timed out or ref count is zero, remove from list and free it.
*/

sslSessionID *
ssl_LookupSID(PRTime now, const PRIPv6Addr *addr, PRUint16 port, const char *peerID,
              const char *urlSvrName)
{
    sslSessionID **sidp;
    sslSessionID *sid;

    if (!urlSvrName)
        return NULL;
    LOCK_CACHE;
    sidp = &cache;
    while ((sid = *sidp) != 0) {
        PORT_Assert(sid->cached == in_client_cache);
        PORT_Assert(sid->references >= 1);

        SSL_TRC(8, ("SSL: lookup: sid=0x%x", sid));

        if (sid->expirationTime < now) {
            /*
            ** This session-id timed out.
            ** Don't even care who it belongs to, blow it out of our cache.
            */
            SSL_TRC(7, ("SSL: lookup, throwing sid out, age=%d refs=%d",
                        now - sid->creationTime, sid->references));

            *sidp = sid->next;                                      /* delink it from the list. */
            sid->cached = invalid_cache;                            /* mark not on list. */
            ssl_FreeLockedSID(sid);                                 /* drop ref count, free. */
        } else if (!memcmp(&sid->addr, addr, sizeof(PRIPv6Addr)) && /* server IP addr matches */
                   (sid->port == port) &&                           /* server port matches */
                   /* proxy (peerID) matches */
                   (((peerID == NULL) && (sid->peerID == NULL)) ||
                    ((peerID != NULL) && (sid->peerID != NULL) &&
                     PORT_Strcmp(sid->peerID, peerID) == 0)) &&
                   /* is cacheable */
                   (sid->u.ssl3.keys.resumable) &&
                   /* server hostname matches. */
                   (sid->urlSvrName != NULL) &&
                   (0 == PORT_Strcmp(urlSvrName, sid->urlSvrName))) {
            /* Hit */
            sid->lastAccessTime = now;
            sid->references++;
            break;
        } else {
            sidp = &sid->next;
        }
    }
    UNLOCK_CACHE;
    return sid;
}

/*
** Add an sid to the cache or return a previously cached entry to the cache.
** Although this is static, it is called via ss->sec.cache().
*/
static void
CacheSID(sslSessionID *sid, PRTime creationTime)
{
    PORT_Assert(sid);
    PORT_Assert(sid->cached == never_cached);

    SSL_TRC(8, ("SSL: Cache: sid=0x%x cached=%d addr=0x%08x%08x%08x%08x port=0x%04x "
                "time=%x cached=%d",
                sid, sid->cached, sid->addr.pr_s6_addr32[0],
                sid->addr.pr_s6_addr32[1], sid->addr.pr_s6_addr32[2],
                sid->addr.pr_s6_addr32[3], sid->port, sid->creationTime,
                sid->cached));

    if (!sid->urlSvrName) {
        /* don't cache this SID because it can never be matched */
        return;
    }

    if (sid->u.ssl3.sessionIDLength == 0 &&
        sid->u.ssl3.locked.sessionTicket.ticket.data == NULL)
        return;

    /* Client generates the SessionID if this was a stateless resume. */
    if (sid->u.ssl3.sessionIDLength == 0) {
        SECStatus rv;
        rv = PK11_GenerateRandom(sid->u.ssl3.sessionID,
                                 SSL3_SESSIONID_BYTES);
        if (rv != SECSuccess)
            return;
        sid->u.ssl3.sessionIDLength = SSL3_SESSIONID_BYTES;
    }
    PRINT_BUF(8, (0, "sessionID:",
                  sid->u.ssl3.sessionID, sid->u.ssl3.sessionIDLength));

    sid->u.ssl3.lock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, NULL);
    if (!sid->u.ssl3.lock) {
        return;
    }
    PORT_Assert(sid->creationTime != 0);
    if (!sid->creationTime) {
        sid->lastAccessTime = sid->creationTime = creationTime;
    }
    PORT_Assert(sid->expirationTime != 0);
    if (!sid->expirationTime) {
        sid->expirationTime = sid->creationTime + (PR_MIN(ssl_ticket_lifetime,
                                                          sid->u.ssl3.locked.sessionTicket.ticket_lifetime_hint) *
                                                   PR_USEC_PER_SEC);
    }

    /*
     * Put sid into the cache.  Bump reference count to indicate that
     * cache is holding a reference. Uncache will reduce the cache
     * reference.
     */
    LOCK_CACHE;
    sid->references++;
    sid->cached = in_client_cache;
    sid->next = cache;
    cache = sid;
    UNLOCK_CACHE;
}

/*
 * If sid "zap" is in the cache,
 *    removes sid from cache, and decrements reference count.
 * Caller must hold cache lock.
 */
static void
UncacheSID(sslSessionID *zap)
{
    sslSessionID **sidp = &cache;
    sslSessionID *sid;

    if (zap->cached != in_client_cache) {
        return;
    }

    SSL_TRC(8, ("SSL: Uncache: zap=0x%x cached=%d addr=0x%08x%08x%08x%08x port=0x%04x "
                "time=%x cipherSuite=%d",
                zap, zap->cached, zap->addr.pr_s6_addr32[0],
                zap->addr.pr_s6_addr32[1], zap->addr.pr_s6_addr32[2],
                zap->addr.pr_s6_addr32[3], zap->port, zap->creationTime,
                zap->u.ssl3.cipherSuite));

    /* See if it's in the cache, if so nuke it */
    while ((sid = *sidp) != 0) {
        if (sid == zap) {
            /*
            ** Bingo. Reduce reference count by one so that when
            ** everyone is done with the sid we can free it up.
            */
            *sidp = zap->next;
            zap->cached = invalid_cache;
            ssl_FreeLockedSID(zap);
            return;
        }
        sidp = &sid->next;
    }
}

/* If sid "zap" is in the cache,
 *    removes sid from cache, and decrements reference count.
 * Although this function is static, it is called externally via
 *    ssl_UncacheSessionID.
 */
static void
LockAndUncacheSID(sslSessionID *zap)
{
    LOCK_CACHE;
    UncacheSID(zap);
    UNLOCK_CACHE;
}

SECStatus
ReadVariableFromBuffer(sslReader *reader, sslReadBuffer *readerBuffer,
                       uint8_t lenBytes, SECItem *dest)
{
    if (sslRead_ReadVariable(reader, lenBytes, readerBuffer) != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (readerBuffer->len) {
        SECItem tempItem = { siBuffer, (unsigned char *)readerBuffer->buf,
                             readerBuffer->len };
        SECStatus rv = SECITEM_CopyItem(NULL, dest, &tempItem);
        if (rv != SECSuccess) {
            return rv;
        }
    }
    return SECSuccess;
}

/* Fill sid with the values from the encoded resumption token.
 * sid has to be allocated.
 * We don't care about locks here as this cache entry is externally stored.
 */
SECStatus
ssl_DecodeResumptionToken(sslSessionID *sid, const PRUint8 *encodedToken,
                          PRUint32 encodedTokenLen)
{
    PORT_Assert(encodedTokenLen);
    PORT_Assert(encodedToken);
    PORT_Assert(sid);
    if (!sid || !encodedToken || !encodedTokenLen) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (encodedToken[0] != SSLResumptionTokenVersion) {
        /* Unknown token format version. */
        PORT_SetError(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR);
        return SECFailure;
    }

    /* These variables are used across macros. Don't use them outside. */
    sslReader reader = SSL_READER(encodedToken, encodedTokenLen);
    reader.offset += 1; // We read the version already. Skip the first byte.
    sslReadBuffer readerBuffer = { 0 };
    PRUint64 tmpInt = 0;

    if (sslRead_ReadNumber(&reader, 8, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->lastAccessTime = (PRTime)tmpInt;
    if (sslRead_ReadNumber(&reader, 8, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->expirationTime = (PRTime)tmpInt;
    if (sslRead_ReadNumber(&reader, 8, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.locked.sessionTicket.received_timestamp = (PRTime)tmpInt;

    if (sslRead_ReadNumber(&reader, 4, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.locked.sessionTicket.ticket_lifetime_hint = (PRUint32)tmpInt;
    if (sslRead_ReadNumber(&reader, 4, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.locked.sessionTicket.flags = (PRUint32)tmpInt;
    if (sslRead_ReadNumber(&reader, 4, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.locked.sessionTicket.ticket_age_add = (PRUint32)tmpInt;
    if (sslRead_ReadNumber(&reader, 4, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.locked.sessionTicket.max_early_data_size = (PRUint32)tmpInt;

    if (sslRead_ReadVariable(&reader, 3, &readerBuffer) != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (readerBuffer.len) {
        PORT_Assert(!sid->peerCert);
        SECItem tempItem = { siBuffer, (unsigned char *)readerBuffer.buf,
                             readerBuffer.len };
        sid->peerCert = CERT_NewTempCertificate(NULL, /* dbHandle */
                                                &tempItem,
                                                NULL, PR_FALSE, PR_TRUE);
        if (!sid->peerCert) {
            return SECFailure;
        }
    }

    if (sslRead_ReadVariable(&reader, 2, &readerBuffer) != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (readerBuffer.len) {
        SECITEM_AllocArray(NULL, &sid->peerCertStatus, 1);
        if (!sid->peerCertStatus.items) {
            return SECFailure;
        }
        SECItem tempItem = { siBuffer, (unsigned char *)readerBuffer.buf,
                             readerBuffer.len };
        if (SECITEM_CopyItem(NULL, &sid->peerCertStatus.items[0], &tempItem) != SECSuccess) {
            return SECFailure;
        }
    }

    if (sslRead_ReadVariable(&reader, 1, &readerBuffer) != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (readerBuffer.len) {
        PORT_Assert(readerBuffer.buf);
        if (sid->peerID) {
            PORT_Free((void *)sid->peerID);
        }
        sid->peerID = PORT_ZAlloc(readerBuffer.len + 1);
        if (!sid->peerID) {
            return SECFailure;
        }
        PORT_Memcpy((void *)sid->peerID, readerBuffer.buf, readerBuffer.len);
    }

    if (sslRead_ReadVariable(&reader, 1, &readerBuffer) != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (readerBuffer.len) {
        if (sid->urlSvrName) {
            PORT_Free((void *)sid->urlSvrName);
        }
        PORT_Assert(readerBuffer.buf);
        sid->urlSvrName = PORT_ZAlloc(readerBuffer.len + 1);
        if (!sid->urlSvrName) {
            return SECFailure;
        }
        PORT_Memcpy((void *)sid->urlSvrName, readerBuffer.buf, readerBuffer.len);
    }

    if (sslRead_ReadVariable(&reader, 3, &readerBuffer) != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (readerBuffer.len) {
        PORT_Assert(!sid->localCert);
        SECItem tempItem = { siBuffer, (unsigned char *)readerBuffer.buf,
                             readerBuffer.len };
        sid->localCert = CERT_NewTempCertificate(NULL, /* dbHandle */
                                                 &tempItem,
                                                 NULL, PR_FALSE, PR_TRUE);
        if (!sid->localCert) {
            return SECFailure;
        }
    }

    if (sslRead_ReadNumber(&reader, 8, &sid->addr.pr_s6_addr64[0]) != SECSuccess) {
        return SECFailure;
    }
    if (sslRead_ReadNumber(&reader, 8, &sid->addr.pr_s6_addr64[1]) != SECSuccess) {
        return SECFailure;
    }

    if (sslRead_ReadNumber(&reader, 2, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->port = (PRUint16)tmpInt;
    if (sslRead_ReadNumber(&reader, 2, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->version = (PRUint16)tmpInt;

    if (sslRead_ReadNumber(&reader, 8, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->creationTime = (PRTime)tmpInt;

    if (sslRead_ReadNumber(&reader, 2, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->authType = (SSLAuthType)tmpInt;
    if (sslRead_ReadNumber(&reader, 4, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->authKeyBits = (PRUint32)tmpInt;
    if (sslRead_ReadNumber(&reader, 2, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->keaType = (SSLKEAType)tmpInt;
    if (sslRead_ReadNumber(&reader, 4, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->keaKeyBits = (PRUint32)tmpInt;
    if (sslRead_ReadNumber(&reader, 3, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->keaGroup = (SSLNamedGroup)tmpInt;

    if (sslRead_ReadNumber(&reader, 3, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->sigScheme = (SSLSignatureScheme)tmpInt;

    if (sslRead_ReadNumber(&reader, 1, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.sessionIDLength = (PRUint8)tmpInt;

    if (sslRead_ReadVariable(&reader, 1, &readerBuffer) != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (readerBuffer.len) {
        PORT_Assert(readerBuffer.buf);
        if (readerBuffer.len > SSL3_SESSIONID_BYTES) {
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            return SECFailure;
        }
        PORT_Memcpy(sid->u.ssl3.sessionID, readerBuffer.buf, readerBuffer.len);
    }

    if (sslRead_ReadNumber(&reader, 2, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.cipherSuite = (PRUint16)tmpInt;
    if (sslRead_ReadNumber(&reader, 1, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.policy = (PRUint8)tmpInt;

    if (sslRead_ReadVariable(&reader, 1, &readerBuffer) != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    PORT_Assert(readerBuffer.len == WRAPPED_MASTER_SECRET_SIZE);
    if (readerBuffer.len != WRAPPED_MASTER_SECRET_SIZE) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    PORT_Assert(readerBuffer.buf);
    PORT_Memcpy(sid->u.ssl3.keys.wrapped_master_secret, readerBuffer.buf,
                readerBuffer.len);

    if (sslRead_ReadNumber(&reader, 1, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.keys.wrapped_master_secret_len = (PRUint8)tmpInt;
    if (sslRead_ReadNumber(&reader, 1, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.keys.extendedMasterSecretUsed = (PRUint8)tmpInt;

    if (sslRead_ReadNumber(&reader, 8, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.masterWrapMech = (unsigned long)tmpInt;
    if (sslRead_ReadNumber(&reader, 8, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.masterModuleID = (unsigned long)tmpInt;
    if (sslRead_ReadNumber(&reader, 8, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.masterSlotID = (unsigned long)tmpInt;

    if (sslRead_ReadNumber(&reader, 4, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.masterWrapIndex = (PRUint32)tmpInt;
    if (sslRead_ReadNumber(&reader, 2, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.masterWrapSeries = (PRUint16)tmpInt;

    if (sslRead_ReadNumber(&reader, 1, &tmpInt) != SECSuccess) {
        return SECFailure;
    }
    sid->u.ssl3.masterValid = (char)tmpInt;

    if (ReadVariableFromBuffer(&reader, &readerBuffer, 1,
                               &sid->u.ssl3.srvName) != SECSuccess) {
        return SECFailure;
    }
    if (ReadVariableFromBuffer(&reader, &readerBuffer, 2,
                               &sid->u.ssl3.signedCertTimestamps) != SECSuccess) {
        return SECFailure;
    }
    if (ReadVariableFromBuffer(&reader, &readerBuffer, 1,
                               &sid->u.ssl3.alpnSelection) != SECSuccess) {
        return SECFailure;
    }
    if (ReadVariableFromBuffer(&reader, &readerBuffer, 2,
                               &sid->u.ssl3.locked.sessionTicket.ticket) != SECSuccess) {
        return SECFailure;
    }
    if (!sid->u.ssl3.locked.sessionTicket.ticket.len) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    /* At this point we must have read everything. */
    PORT_Assert(reader.offset == reader.buf.len);
    if (reader.offset != reader.buf.len) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    return SECSuccess;
}

PRBool
ssl_IsResumptionTokenUsable(sslSocket *ss, sslSessionID *sid)
{
    PORT_Assert(ss);
    PORT_Assert(sid);

    // Check that the ticket didn't expire.
    PRTime endTime = 0;
    NewSessionTicket *ticket = &sid->u.ssl3.locked.sessionTicket;
    if (ticket->ticket_lifetime_hint != 0) {
        endTime = ticket->received_timestamp +
                  (PRTime)(ticket->ticket_lifetime_hint * PR_USEC_PER_SEC);
        if (endTime <= ssl_Time(ss)) {
            return PR_FALSE;
        }
    }

    // Check that the session entry didn't expire.
    if (sid->expirationTime < ssl_Time(ss)) {
        return PR_FALSE;
    }

    // Check that the server name (SNI) matches the one set for this session.
    // Don't use the token if there's no server name.
    if (sid->urlSvrName == NULL || PORT_Strcmp(ss->url, sid->urlSvrName) != 0) {
        return PR_FALSE;
    }

    // This shouldn't be false, but let's check it anyway.
    if (!sid->u.ssl3.keys.resumable) {
        return PR_FALSE;
    }

    return PR_TRUE;
}

/* Encode a session ticket into a byte array that can be handed out to a cache.
 * Needed memory in encodedToken has to be allocated according to
 * *encodedTokenLen. */
static SECStatus
ssl_EncodeResumptionToken(sslSessionID *sid, sslBuffer *encodedTokenBuf)
{
    PORT_Assert(encodedTokenBuf);
    PORT_Assert(sid);
    if (!sid || !sid->u.ssl3.locked.sessionTicket.ticket.len ||
        !encodedTokenBuf || !sid->u.ssl3.keys.resumable || !sid->urlSvrName) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    /* Encoding format:
     * 0-byte: version
     * Integers are encoded according to their length.
     * SECItems are prepended with a 64-bit length field followed by the bytes.
     * Optional bytes are encoded as a 0-length item if not present.
     */
    SECStatus rv = sslBuffer_AppendNumber(encodedTokenBuf,
                                          SSLResumptionTokenVersion, 1);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->lastAccessTime, 8);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->expirationTime, 8);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    // session ticket
    rv = sslBuffer_AppendNumber(encodedTokenBuf,
                                sid->u.ssl3.locked.sessionTicket.received_timestamp,
                                8);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf,
                                sid->u.ssl3.locked.sessionTicket.ticket_lifetime_hint,
                                4);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf,
                                sid->u.ssl3.locked.sessionTicket.flags,
                                4);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf,
                                sid->u.ssl3.locked.sessionTicket.ticket_age_add,
                                4);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf,
                                sid->u.ssl3.locked.sessionTicket.max_early_data_size,
                                4);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    rv = sslBuffer_AppendVariable(encodedTokenBuf, sid->peerCert->derCert.data,
                                  sid->peerCert->derCert.len, 3);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (sid->peerCertStatus.len > 1) {
        /* This is not implemented so it shouldn't happen.
         * If it gets implemented, this has to change.
         */
        PORT_Assert(0);
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    if (sid->peerCertStatus.len == 1 && sid->peerCertStatus.items[0].len) {
        rv = sslBuffer_AppendVariable(encodedTokenBuf,
                                      sid->peerCertStatus.items[0].data,
                                      sid->peerCertStatus.items[0].len, 2);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    } else {
        rv = sslBuffer_AppendVariable(encodedTokenBuf, NULL, 0, 2);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    PRUint64 len = sid->peerID ? strlen(sid->peerID) : 0;
    if (len > PR_UINT8_MAX) {
        // This string really shouldn't be that long.
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    rv = sslBuffer_AppendVariable(encodedTokenBuf,
                                  (const unsigned char *)sid->peerID, len, 1);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    len = sid->urlSvrName ? strlen(sid->urlSvrName) : 0;
    if (!len) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (len > PR_UINT8_MAX) {
        // This string really shouldn't be that long.
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    rv = sslBuffer_AppendVariable(encodedTokenBuf,
                                  (const unsigned char *)sid->urlSvrName,
                                  len, 1);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (sid->localCert) {
        rv = sslBuffer_AppendVariable(encodedTokenBuf,
                                      sid->localCert->derCert.data,
                                      sid->localCert->derCert.len, 3);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    } else {
        rv = sslBuffer_AppendVariable(encodedTokenBuf, NULL, 0, 3);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->addr.pr_s6_addr64[0], 8);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->addr.pr_s6_addr64[1], 8);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->port, 2);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->version, 2);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->creationTime, 8);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->authType, 2);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->authKeyBits, 4);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->keaType, 2);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->keaKeyBits, 4);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->keaGroup, 3);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->sigScheme, 3);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->u.ssl3.sessionIDLength, 1);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendVariable(encodedTokenBuf, sid->u.ssl3.sessionID,
                                  SSL3_SESSIONID_BYTES, 1);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->u.ssl3.cipherSuite, 2);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->u.ssl3.policy, 1);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    rv = sslBuffer_AppendVariable(encodedTokenBuf,
                                  sid->u.ssl3.keys.wrapped_master_secret,
                                  WRAPPED_MASTER_SECRET_SIZE, 1);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = sslBuffer_AppendNumber(encodedTokenBuf,
                                sid->u.ssl3.keys.wrapped_master_secret_len,
                                1);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf,
                                sid->u.ssl3.keys.extendedMasterSecretUsed,
                                1);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->u.ssl3.masterWrapMech, 8);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->u.ssl3.masterModuleID, 8);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->u.ssl3.masterSlotID, 8);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->u.ssl3.masterWrapIndex, 4);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->u.ssl3.masterWrapSeries, 2);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    rv = sslBuffer_AppendNumber(encodedTokenBuf, sid->u.ssl3.masterValid, 1);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    rv = sslBuffer_AppendVariable(encodedTokenBuf, sid->u.ssl3.srvName.data,
                                  sid->u.ssl3.srvName.len, 1);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    rv = sslBuffer_AppendVariable(encodedTokenBuf,
                                  sid->u.ssl3.signedCertTimestamps.data,
                                  sid->u.ssl3.signedCertTimestamps.len, 2);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = sslBuffer_AppendVariable(encodedTokenBuf,
                                  sid->u.ssl3.alpnSelection.data,
                                  sid->u.ssl3.alpnSelection.len, 1);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    PORT_Assert(sid->u.ssl3.locked.sessionTicket.ticket.len > 1);
    rv = sslBuffer_AppendVariable(encodedTokenBuf,
                                  sid->u.ssl3.locked.sessionTicket.ticket.data,
                                  sid->u.ssl3.locked.sessionTicket.ticket.len,
                                  2);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    return SECSuccess;
}

void
ssl_CacheExternalToken(sslSocket *ss)
{
    PORT_Assert(ss);
    sslSessionID *sid = ss->sec.ci.sid;
    PORT_Assert(sid);
    PORT_Assert(sid->cached == never_cached);
    PORT_Assert(ss->resumptionTokenCallback);

    SSL_TRC(8, ("SSL [%d]: Cache External: sid=0x%x cached=%d "
                "addr=0x%08x%08x%08x%08x port=0x%04x time=%x cached=%d",
                ss->fd,
                sid, sid->cached, sid->addr.pr_s6_addr32[0],
                sid->addr.pr_s6_addr32[1], sid->addr.pr_s6_addr32[2],
                sid->addr.pr_s6_addr32[3], sid->port, sid->creationTime,
                sid->cached));

    /* This is only available for stateless resumption. */
    if (sid->u.ssl3.locked.sessionTicket.ticket.data == NULL) {
        return;
    }

    /* Don't export token if the session used client authentication. */
    if (sid->u.ssl3.clAuthValid) {
        return;
    }

    if (!sid->creationTime) {
        sid->lastAccessTime = sid->creationTime = ssl_Time(ss);
    }
    if (!sid->expirationTime) {
        sid->expirationTime = sid->creationTime + (PR_MIN(ssl_ticket_lifetime,
                                                          sid->u.ssl3.locked.sessionTicket.ticket_lifetime_hint) *
                                                   PR_USEC_PER_SEC);
    }

    sslBuffer encodedToken = SSL_BUFFER_EMPTY;

    if (ssl_EncodeResumptionToken(sid, &encodedToken) != SECSuccess) {
        SSL_TRC(3, ("SSL [%d]: encoding resumption token failed", ss->fd));
        return;
    }
    PORT_Assert(SSL_BUFFER_LEN(&encodedToken) > 0);
    PRINT_BUF(40, (ss, "SSL: encoded resumption token",
                   SSL_BUFFER_BASE(&encodedToken),
                   SSL_BUFFER_LEN(&encodedToken)));
    SECStatus rv = ss->resumptionTokenCallback(
        ss->fd, SSL_BUFFER_BASE(&encodedToken), SSL_BUFFER_LEN(&encodedToken),
        ss->resumptionTokenContext);
    if (rv == SECSuccess) {
        sid->cached = in_external_cache;
    }
    sslBuffer_Clear(&encodedToken);
}

void
ssl_CacheSessionID(sslSocket *ss)
{
    sslSecurityInfo *sec = &ss->sec;
    PORT_Assert(sec);
    PORT_Assert(sec->ci.sid->cached == never_cached);

    if (sec->ci.sid && !sec->ci.sid->u.ssl3.keys.resumable) {
        return;
    }

    /* A client whose ECH offer was rejected has authenticated the server only
     * against the ECHConfig public name, so the session must not be reusable
     * for the real host name. */
    if (!sec->isServer && ss->ssl3.hs.echHpkeCtx && !ss->ssl3.hs.echAccepted) {
        return;
    }

    if (!sec->isServer && ss->resumptionTokenCallback) {
        ssl_CacheExternalToken(ss);
        return;
    }

    PORT_Assert(!ss->resumptionTokenCallback);
    if (sec->isServer) {
        ssl_ServerCacheSessionID(sec->ci.sid, ssl_Time(ss));
        return;
    }

    CacheSID(sec->ci.sid, ssl_Time(ss));
}

void
ssl_UncacheSessionID(sslSocket *ss)
{
    if (ss->opt.noCache) {
        return;
    }

    sslSecurityInfo *sec = &ss->sec;
    PORT_Assert(sec);

    if (sec->ci.sid) {
        if (sec->isServer) {
            ssl_ServerUncacheSessionID(sec->ci.sid);
        } else if (!ss->resumptionTokenCallback) {
            LockAndUncacheSID(sec->ci.sid);
        }
    }
}

/* wipe out the entire client session cache. */
void
SSL_ClearSessionCache(void)
{
    LOCK_CACHE;
    while (cache != NULL)
        UncacheSID(cache);
    UNLOCK_CACHE;
}

PRBool
ssl_TicketTimeValid(const sslSocket *ss, const NewSessionTicket *ticket)
{
    PRTime endTime;

    if (ticket->ticket_lifetime_hint == 0) {
        return PR_TRUE;
    }

    endTime = ticket->received_timestamp +
              (PRTime)(ticket->ticket_lifetime_hint * PR_USEC_PER_SEC);
    return endTime > ssl_Time(ss);
}

void
ssl3_SetSIDSessionTicket(sslSessionID *sid,
                         /*in/out*/ NewSessionTicket *newSessionTicket)
{
    PORT_Assert(sid);
    PORT_Assert(newSessionTicket);
    PORT_Assert(newSessionTicket->ticket.data);
    PORT_Assert(newSessionTicket->ticket.len != 0);

    /* If this is in the client cache, we are updating an existing entry that is
     * already cached or was once cached, so we need to acquire and release the
     * write lock. Otherwise, this is a new session that isn't shared with
     * anything yet, so no locking is needed.
     */
    if (sid->u.ssl3.lock) {
        PR_RWLock_Wlock(sid->u.ssl3.lock);
        /* Another thread may have evicted, or it may be in external cache. */
        PORT_Assert(sid->cached != never_cached);
    }
    /* If this was in the client cache, then we might have to free the old
     * ticket.  In TLS 1.3, we might get a replacement ticket if the server
     * sends more than one ticket. */
    if (sid->u.ssl3.locked.sessionTicket.ticket.data) {
        PORT_Assert(sid->cached != never_cached ||
                    sid->version >= SSL_LIBRARY_VERSION_TLS_1_3);
        SECITEM_FreeItem(&sid->u.ssl3.locked.sessionTicket.ticket,
                         PR_FALSE);
    }

    PORT_Assert(!sid->u.ssl3.locked.sessionTicket.ticket.data);

    /* Do a shallow copy, moving the ticket data. */
    sid->u.ssl3.locked.sessionTicket = *newSessionTicket;
    newSessionTicket->ticket.data = NULL;
    newSessionTicket->ticket.len = 0;

    if (sid->u.ssl3.lock) {
        PR_RWLock_Unlock(sid->u.ssl3.lock);
    }
}
