/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This file implements the SERVER Session ID cache.
 * NOTE:  The contents of this file are NOT used by the client.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Note: ssl_FreeSID() in sslnonce.c gets used for both client and server
 * cache sids!
 *
 * About record locking among different server processes:
 *
 * All processes that are part of the same conceptual server (serving on
 * the same address and port) MUST share a common SSL session cache.
 * This code makes the content of the shared cache accessible to all
 * processes on the same "server".  This code works on Unix and Win32 only.
 *
 * We use NSPR anonymous shared memory and move data to & from shared memory.
 * We must do explicit locking of the records for all reads and writes.
 * The set of Cache entries are divided up into "sets" of 128 entries.
 * Each set is protected by a lock.  There may be one or more sets protected
 * by each lock.  That is, locks to sets are 1:N.
 * There is one lock for the entire cert cache.
 * There is one lock for the set of wrapped sym wrap keys.
 *
 * The anonymous shared memory is laid out as if it were declared like this:
 *
 * struct {
 *     cacheDescriptor          desc;
 *     sidCacheLock             sidCacheLocks[ numSIDCacheLocks];
 *     sidCacheLock             keyCacheLock;
 *     sidCacheLock             certCacheLock;
 *     sidCacheSet              sidCacheSets[ numSIDCacheSets ];
 *     sidCacheEntry            sidCacheData[ numSIDCacheEntries];
 *     certCacheEntry           certCacheData[numCertCacheEntries];
 *     SSLWrappedSymWrappingKey keyCacheData[SSL_NUM_WRAP_KEYS][SSL_NUM_WRAP_MECHS];
 *     PRUint8                  keyNameSuffix[SELF_ENCRYPT_KEY_VAR_NAME_LEN]
 *     encKeyCacheEntry         ticketEncKey; // Wrapped
 *     encKeyCacheEntry         ticketMacKey; // Wrapped
 *     PRBool                   ticketKeysValid;
 *     sidCacheLock             srvNameCacheLock;
 *     srvNameCacheEntry        srvNameData[ numSrvNameCacheEntries ];
 * } cacheMemCacheData;
 */
#include "seccomon.h"

#if defined(XP_UNIX) || defined(XP_WIN32)

#include "cert.h"
#include "ssl.h"
#include "sslimpl.h"
#include "sslproto.h"
#include "pk11func.h"
#include "base64.h"
#include "keyhi.h"
#include "blapit.h"
#include "nss.h" /* for NSS_RegisterShutdown */
#include "sechash.h"
#include "selfencrypt.h"
#include <stdio.h>

#if defined(XP_UNIX)

#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include "unix_err.h"

#else

#ifdef XP_WIN32
#include <wtypes.h>
#include "win32err.h"
#endif

#endif
#include <sys/types.h>

#include "nspr.h"
#include "sslmutex.h"

/*
** Format of a cache entry in the shared memory.
*/
PR_STATIC_ASSERT(sizeof(PRTime) == 8);
struct sidCacheEntryStr {
    /* 16 */ PRIPv6Addr addr; /* client's IP address */
    /*  8 */ PRTime creationTime;
    /*  8 */ PRTime lastAccessTime;
    /*  8 */ PRTime expirationTime;
    /*  2 */ PRUint16 version;
    /*  1 */ PRUint8 valid;
    /*  1 */ PRUint8 sessionIDLength;
    /* 32 */ PRUint8 sessionID[SSL3_SESSIONID_BYTES];
    /*  2 */ PRUint16 authType;
    /*  2 */ PRUint16 authKeyBits;
    /*  2 */ PRUint16 keaType;
    /*  2 */ PRUint16 keaKeyBits;
    /*  4 */ PRUint32 signatureScheme;
    /*  4 */ PRUint32 keaGroup;
    /* 92  - common header total */

    union {
        struct {
            /*  2 */ ssl3CipherSuite cipherSuite;
            /* 52 */ ssl3SidKeys keys; /* keys, wrapped as needed. */

            /*  4 */ PRUint32 masterWrapMech;
            /*  4 */ PRInt32 certIndex;
            /*  4 */ PRInt32 srvNameIndex;
            /* 32 */ PRUint8 srvNameHash[SHA256_LENGTH]; /* SHA256 name hash */
            /* 32 */ PRUint8 certHash[SHA256_LENGTH];    /* SHA256 cert hash */
            /*  2 */ PRUint16 namedCurve;
        /*132 */ } ssl3;

        /* force sizeof(sidCacheEntry) to be a multiple of cache line size */
        struct {
            /*148 */ PRUint8 filler[148]; /* 92+148==240, a multiple of 16 */
        } forceSize;
    } u;
};
typedef struct sidCacheEntryStr sidCacheEntry;

/* The length of this struct is supposed to be a power of 2, e.g. 4KB */
struct certCacheEntryStr {
    PRUint32 certLength;                   /*    4 */
    PRUint8 certHash[SHA256_LENGTH];       /*   32 */
    PRUint8 cert[SSL_MAX_CACHED_CERT_LEN]; /* 4060 */
}; /* total   4096 */
typedef struct certCacheEntryStr certCacheEntry;

PR_STATIC_ASSERT(sizeof(sidCacheEntry) % 16 == 0);
PR_STATIC_ASSERT(sizeof(certCacheEntry) == 4096);

struct sidCacheLockStr {
    PRUint32 timeStamp;
    sslMutex mutex;
    sslPID pid;
};
typedef struct sidCacheLockStr sidCacheLock;

struct sidCacheSetStr {
    PRIntn next;
};
typedef struct sidCacheSetStr sidCacheSet;

struct encKeyCacheEntryStr {
    PRUint8 bytes[512];
    PRInt32 length;
};
typedef struct encKeyCacheEntryStr encKeyCacheEntry;

#define SSL_MAX_DNS_HOST_NAME 1024

struct srvNameCacheEntryStr {
    PRUint16 type;                            /*    2 */
    PRUint16 nameLen;                         /*    2 */
    PRUint8 name[SSL_MAX_DNS_HOST_NAME + 12]; /* 1034 */
    PRUint8 nameHash[SHA256_LENGTH];          /*   32 */
                                              /* 1072 */
};
typedef struct srvNameCacheEntryStr srvNameCacheEntry;

struct cacheDescStr {

    PRUint32 cacheMemSize;

    PRUint32 numSIDCacheLocks;
    PRUint32 numSIDCacheSets;
    PRUint32 numSIDCacheSetsPerLock;

    PRUint32 numSIDCacheEntries;
    PRUint32 sidCacheSize;

    PRUint32 numCertCacheEntries;
    PRUint32 certCacheSize;

    PRUint32 numKeyCacheEntries;
    PRUint32 keyCacheSize;

    PRUint32 numSrvNameCacheEntries;
    PRUint32 srvNameCacheSize;

    PRUint32 ssl3Timeout;

    PRUint32 numSIDCacheLocksInitialized;

    /* These values are volatile, and are accessed through sharedCache-> */
    PRUint32 nextCertCacheEntry; /* certCacheLock protects */
    PRBool stopPolling;
    PRBool everInherited;

    /* The private copies of these values are pointers into shared mem */
    /* The copies of these values in shared memory are merely offsets */
    sidCacheLock *sidCacheLocks;
    sidCacheLock *keyCacheLock;
    sidCacheLock *certCacheLock;
    sidCacheLock *srvNameCacheLock;
    sidCacheSet *sidCacheSets;
    sidCacheEntry *sidCacheData;
    certCacheEntry *certCacheData;
    SSLWrappedSymWrappingKey *keyCacheData;
    PRUint8 *ticketKeyNameSuffix;
    encKeyCacheEntry *ticketEncKey;
    encKeyCacheEntry *ticketMacKey;
    PRUint32 *ticketKeysValid;
    srvNameCacheEntry *srvNameCacheData;

    /* Only the private copies of these pointers are valid */
    char *cacheMem;
    struct cacheDescStr *sharedCache; /* shared copy of this struct */
    PRFileMap *cacheMemMap;
    PRThread *poller;
    PRUint32 mutexTimeout;
    PRBool shared;
};
typedef struct cacheDescStr cacheDesc;

static cacheDesc globalCache;

static const char envVarName[] = { SSL_ENV_VAR_NAME };

static PRBool isMultiProcess = PR_FALSE;

#define DEF_SID_CACHE_ENTRIES 10000
#define DEF_CERT_CACHE_ENTRIES 250
#define MIN_CERT_CACHE_ENTRIES 125 /* the effective size in old releases. */
#define DEF_KEY_CACHE_ENTRIES 250
#define DEF_NAME_CACHE_ENTRIES 1000

#define SID_CACHE_ENTRIES_PER_SET 128
#define SID_ALIGNMENT 16

#define DEF_SSL3_TIMEOUT 86400L /* 24 hours */
#define MAX_SSL3_TIMEOUT 86400L /* 24 hours */
#define MIN_SSL3_TIMEOUT 5      /* seconds  */

#if defined(AIX) || defined(LINUX) || defined(NETBSD) || defined(OPENBSD)
#define MAX_SID_CACHE_LOCKS 8 /* two FDs per lock */
#else
#define MAX_SID_CACHE_LOCKS 256
#endif

#define SID_HOWMANY(val, size) (((val) + ((size) - 1)) / (size))
#define SID_ROUNDUP(val, size) ((size) * SID_HOWMANY((val), (size)))

static sslPID myPid;
static PRUint32 ssl_max_sid_cache_locks = MAX_SID_CACHE_LOCKS;

/* forward static function declarations */
static PRUint32 SIDindex(cacheDesc *cache, const PRIPv6Addr *addr, PRUint8 *s,
                         unsigned nl);
#if defined(XP_UNIX)
static SECStatus LaunchLockPoller(cacheDesc *cache);
static SECStatus StopLockPoller(cacheDesc *cache);
#endif

struct inheritanceStr {
    PRUint32 cacheMemSize;
    PRUint32 fmStrLen;
};

typedef struct inheritanceStr inheritance;

#if defined(_WIN32)

#define DEFAULT_CACHE_DIRECTORY "\\temp"

#endif /* _win32 */

#if defined(XP_UNIX)

#define DEFAULT_CACHE_DIRECTORY "/tmp"

#endif /* XP_UNIX */

/************************************************************************/

/* SSL Session Cache has a smaller set of functions to initialize than
 * ssl does. some ssl_functions can't be initialized before NSS has been
 * initialized, and the cache may be configured before NSS is initialized
 * so thus the special init function */
static SECStatus
ssl_InitSessionCache()
{
    /* currently only one function, which is itself idempotent */
    return ssl_InitializePRErrorTable();
}

/* This is used to set locking times for the cache.  It is not used to set the
 * PRTime attributes of sessions, which are driven by ss->now(). */
static PRUint32
ssl_CacheNow()
{
    return PR_Now() / PR_USEC_PER_SEC;
}

static PRUint32
LockSidCacheLock(sidCacheLock *lock, PRUint32 now)
{
    SECStatus rv = sslMutex_Lock(&lock->mutex);
    if (rv != SECSuccess)
        return 0;
    if (!now) {
        now = ssl_CacheNow();
    }

    lock->timeStamp = now;
    lock->pid = myPid;
    return now;
}

static SECStatus
UnlockSidCacheLock(sidCacheLock *lock)
{
    SECStatus rv;

    lock->pid = 0;
    rv = sslMutex_Unlock(&lock->mutex);
    return rv;
}

/* Returns non-zero |now| or ssl_CacheNow() on success, zero on failure. */
static PRUint32
LockSet(cacheDesc *cache, PRUint32 set, PRUint32 now)
{
    PRUint32 lockNum = set % cache->numSIDCacheLocks;
    sidCacheLock *lock = cache->sidCacheLocks + lockNum;

    return LockSidCacheLock(lock, now);
}

static SECStatus
UnlockSet(cacheDesc *cache, PRUint32 set)
{
    PRUint32 lockNum = set % cache->numSIDCacheLocks;
    sidCacheLock *lock = cache->sidCacheLocks + lockNum;

    return UnlockSidCacheLock(lock);
}

/************************************************************************/

/* Put a certificate in the cache.  Record its slot index and SHA-256 hash
 * in the sce.
 */
static PRUint32
CacheCert(cacheDesc *cache, const SECItem *derCert, sidCacheEntry *sce)
{
    PRUint32 now;
    certCacheEntry cce = { 0 };

    if ((derCert->len > SSL_MAX_CACHED_CERT_LEN) ||
        (derCert->len <= 0) ||
        (derCert->data == NULL)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return 0;
    }

    cce.certLength = derCert->len;
    PORT_Memcpy(cce.cert, derCert->data, cce.certLength);
    if (HASH_HashBuf(HASH_AlgSHA256, cce.certHash, cce.cert,
                     cce.certLength) != SECSuccess) {
        return 0;
    }

    /* get lock on cert cache */
    now = LockSidCacheLock(cache->certCacheLock, 0);
    if (now) {

        /* Find where to place the next cert cache entry. */
        cacheDesc *sharedCache = cache->sharedCache;
        PRUint32 ndx = sharedCache->nextCertCacheEntry;

        /* write the entry */
        cache->certCacheData[ndx] = cce;

        /* remember where we put it. */
        sce->u.ssl3.certIndex = ndx;
        PORT_Memcpy(sce->u.ssl3.certHash, cce.certHash,
                    sizeof(sce->u.ssl3.certHash));

        /* update the "next" cache entry index */
        sharedCache->nextCertCacheEntry =
            (ndx + 1) % cache->numCertCacheEntries;

        UnlockSidCacheLock(cache->certCacheLock);
    }
    return now;
}

/* Server configuration hash tables need to account the SECITEM.type
 * field as well. These functions accomplish that. */
static PLHashNumber
Get32BitNameHash(const SECItem *name)
{
    PLHashNumber rv = SECITEM_Hash(name);

    PRUint8 *rvc = (PRUint8 *)&rv;
    rvc[name->len % sizeof(rv)] ^= name->type;

    return rv;
}

/* Put a name in the cache.  Update the cert index in the sce.
 */
static PRUint32
CacheSrvName(cacheDesc *cache, SECItem *name, sidCacheEntry *sce)
{
    PRUint32 now;
    PRUint32 ndx;
    srvNameCacheEntry snce;

    if (!name || name->len <= 0 ||
        name->len > SSL_MAX_DNS_HOST_NAME) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return 0;
    }

    snce.type = name->type;
    snce.nameLen = name->len;
    PORT_Memcpy(snce.name, name->data, snce.nameLen);
    HASH_HashBuf(HASH_AlgSHA256, snce.nameHash, name->data, name->len);

    /* get index of the next name */
    ndx = Get32BitNameHash(name);
    /* get lock on cert cache */
    now = LockSidCacheLock(cache->srvNameCacheLock, 0);
    if (now) {
        if (cache->numSrvNameCacheEntries > 0) {
            /* Fit the index into array */
            ndx %= cache->numSrvNameCacheEntries;
            /* write the entry */
            cache->srvNameCacheData[ndx] = snce;
            /* remember where we put it. */
            sce->u.ssl3.srvNameIndex = ndx;
            /* Copy hash into sid hash */
            PORT_Memcpy(sce->u.ssl3.srvNameHash, snce.nameHash, SHA256_LENGTH);
        }
        UnlockSidCacheLock(cache->srvNameCacheLock);
    }
    return now;
}

/*
** Convert local SID to shared memory one
*/
static void
ConvertFromSID(sidCacheEntry *to, sslSessionID *from)
{
    to->valid = 1;
    to->version = from->version;
    to->addr = from->addr;
    to->creationTime = from->creationTime;
    to->lastAccessTime = from->lastAccessTime;
    to->expirationTime = from->expirationTime;
    to->authType = from->authType;
    to->authKeyBits = from->authKeyBits;
    to->keaType = from->keaType;
    to->keaKeyBits = from->keaKeyBits;
    to->keaGroup = from->keaGroup;
    to->signatureScheme = from->sigScheme;

    to->u.ssl3.cipherSuite = from->u.ssl3.cipherSuite;
    to->u.ssl3.keys = from->u.ssl3.keys;
    to->u.ssl3.masterWrapMech = from->u.ssl3.masterWrapMech;
    to->sessionIDLength = from->u.ssl3.sessionIDLength;
    to->u.ssl3.certIndex = -1;
    to->u.ssl3.srvNameIndex = -1;
    PORT_Memcpy(to->sessionID, from->u.ssl3.sessionID,
                to->sessionIDLength);
    to->u.ssl3.namedCurve = 0U;
    if (from->authType == ssl_auth_ecdsa ||
        from->authType == ssl_auth_ecdh_rsa ||
        from->authType == ssl_auth_ecdh_ecdsa) {
        PORT_Assert(from->namedCurve);
        to->u.ssl3.namedCurve = (PRUint16)from->namedCurve->name;
    }

    SSL_TRC(8, ("%d: SSL3: ConvertSID: time=%d addr=0x%08x%08x%08x%08x "
                "cipherSuite=%d",
                myPid, to->creationTime / PR_USEC_PER_SEC,
                to->addr.pr_s6_addr32[0], to->addr.pr_s6_addr32[1],
                to->addr.pr_s6_addr32[2], to->addr.pr_s6_addr32[3],
                to->u.ssl3.cipherSuite));
}

/*
** Convert shared memory cache-entry to local memory based one
** This is only called from ServerSessionIDLookup().
*/
static sslSessionID *
ConvertToSID(sidCacheEntry *from,
             certCacheEntry *pcce,
             srvNameCacheEntry *psnce)
{
    sslSessionID *to;

    to = PORT_ZNew(sslSessionID);
    if (!to) {
        return 0;
    }

    to->u.ssl3.sessionIDLength = from->sessionIDLength;
    to->u.ssl3.cipherSuite = from->u.ssl3.cipherSuite;
    to->u.ssl3.keys = from->u.ssl3.keys;
    to->u.ssl3.masterWrapMech = from->u.ssl3.masterWrapMech;
    if (from->u.ssl3.srvNameIndex != -1 && psnce) {
        SECItem name;
        SECStatus rv;
        name.type = psnce->type;
        name.len = psnce->nameLen;
        name.data = psnce->name;
        rv = SECITEM_CopyItem(NULL, &to->u.ssl3.srvName, &name);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    PORT_Memcpy(to->u.ssl3.sessionID, from->sessionID, from->sessionIDLength);

    to->urlSvrName = NULL;

    to->u.ssl3.masterModuleID = (SECMODModuleID)-1; /* invalid value */
    to->u.ssl3.masterSlotID = (CK_SLOT_ID)-1;       /* invalid value */
    to->u.ssl3.masterWrapIndex = 0;
    to->u.ssl3.masterWrapSeries = 0;
    to->u.ssl3.masterValid = PR_FALSE;

    to->u.ssl3.clAuthModuleID = (SECMODModuleID)-1; /* invalid value */
    to->u.ssl3.clAuthSlotID = (CK_SLOT_ID)-1;       /* invalid value */
    to->u.ssl3.clAuthSeries = 0;
    to->u.ssl3.clAuthValid = PR_FALSE;

    if (from->u.ssl3.certIndex != -1 && pcce) {
        SECItem derCert;

        derCert.type = siBuffer;
        derCert.len = pcce->certLength;
        derCert.data = pcce->cert;

        if (SECITEM_CopyItem(NULL, &to->peerCertDER, &derCert) != SECSuccess)
            goto loser;
    }
    if (from->authType == ssl_auth_ecdsa ||
        from->authType == ssl_auth_ecdh_rsa ||
        from->authType == ssl_auth_ecdh_ecdsa) {
        to->namedCurve =
            ssl_LookupNamedGroup((SSLNamedGroup)from->u.ssl3.namedCurve);
    }

    to->version = from->version;
    to->creationTime = from->creationTime;
    to->lastAccessTime = from->lastAccessTime;
    to->expirationTime = from->expirationTime;
    to->cached = in_server_cache;
    to->addr = from->addr;
    to->references = 1;
    to->authType = from->authType;
    to->authKeyBits = from->authKeyBits;
    to->keaType = from->keaType;
    to->keaKeyBits = from->keaKeyBits;
    to->keaGroup = from->keaGroup;
    to->sigScheme = from->signatureScheme;

    return to;

loser:
    if (to) {
        SECITEM_FreeItem(&to->u.ssl3.srvName, PR_FALSE);
        PORT_Free(to);
    }
    return NULL;
}

/*
** Perform some mumbo jumbo on the ip-address and the session-id value to
** compute a hash value.
*/
static PRUint32
SIDindex(cacheDesc *cache, const PRIPv6Addr *addr, PRUint8 *s, unsigned nl)
{
    PRUint32 rv;
    PRUint32 x[8];

    memset(x, 0, sizeof x);
    if (nl > sizeof x)
        nl = sizeof x;
    memcpy(x, s, nl);

    rv = (addr->pr_s6_addr32[0] ^ addr->pr_s6_addr32[1] ^
          addr->pr_s6_addr32[2] ^ addr->pr_s6_addr32[3] ^
          x[0] ^ x[1] ^ x[2] ^ x[3] ^ x[4] ^ x[5] ^ x[6] ^ x[7]) %
         cache->numSIDCacheSets;
    return rv;
}

/*
** Look something up in the cache. This will invalidate old entries
** in the process. Caller has locked the cache set!
** Returns PR_TRUE if found a valid match.  PR_FALSE otherwise.
*/
static sidCacheEntry *
FindSID(cacheDesc *cache, PRUint32 setNum, PRUint32 now,
        const PRIPv6Addr *addr, unsigned char *sessionID,
        unsigned sessionIDLength)
{
    PRUint32 ndx = cache->sidCacheSets[setNum].next;
    int i;

    sidCacheEntry *set = cache->sidCacheData +
                         (setNum * SID_CACHE_ENTRIES_PER_SET);

    for (i = SID_CACHE_ENTRIES_PER_SET; i > 0; --i) {
        sidCacheEntry *sce;

        ndx = (ndx - 1) % SID_CACHE_ENTRIES_PER_SET;
        sce = set + ndx;

        if (!sce->valid)
            continue;

        if (now > sce->expirationTime) {
            /* SessionID has timed out. Invalidate the entry. */
            SSL_TRC(7, ("%d: timed out sid entry addr=%08x%08x%08x%08x now=%x "
                        "time+=%x",
                        myPid, sce->addr.pr_s6_addr32[0],
                        sce->addr.pr_s6_addr32[1], sce->addr.pr_s6_addr32[2],
                        sce->addr.pr_s6_addr32[3], now,
                        sce->expirationTime));
            sce->valid = 0;
            continue;
        }

        /*
        ** Next, examine specific session-id/addr data to see if the cache
        ** entry matches our addr+session-id value
        */
        if (sessionIDLength == sce->sessionIDLength &&
            !memcmp(&sce->addr, addr, sizeof(PRIPv6Addr)) &&
            !memcmp(sce->sessionID, sessionID, sessionIDLength)) {
            /* Found it */
            return sce;
        }
    }

    PORT_SetError(SSL_ERROR_SESSION_NOT_FOUND);
    return NULL;
}

/************************************************************************/

/* This is the primary function for finding entries in the server's sid cache.
 * Although it is static, this function is called via the global function
 * pointer ssl_sid_lookup.
 *
 * sslNow is the time that the calling socket understands, which might be
 * different than what the cache uses to maintain its locks.
 */
static sslSessionID *
ServerSessionIDLookup(PRTime sslNow, const PRIPv6Addr *addr,
                      unsigned char *sessionID,
                      unsigned int sessionIDLength,
                      CERTCertDBHandle *dbHandle)
{
    sslSessionID *sid = 0;
    sidCacheEntry *psce;
    certCacheEntry *pcce = 0;
    srvNameCacheEntry *psnce = 0;
    cacheDesc *cache = &globalCache;
    PRUint32 now;
    PRUint32 set;
    PRInt32 cndx;
    sidCacheEntry sce;
    certCacheEntry cce;
    srvNameCacheEntry snce;

    set = SIDindex(cache, addr, sessionID, sessionIDLength);
    now = LockSet(cache, set, 0);
    if (!now)
        return NULL;

    psce = FindSID(cache, set, now, addr, sessionID, sessionIDLength);
    if (psce) {
        if ((cndx = psce->u.ssl3.certIndex) != -1) {
            PRUint32 gotLock = LockSidCacheLock(cache->certCacheLock, now);
            if (gotLock) {
                pcce = &cache->certCacheData[cndx];

                /* Session IDs aren't unique, as ticket resumptions use the
                ** client's, so compare cert hashes to detect slot reuse.
                */
                if (!PORT_Memcmp(pcce->certHash, psce->u.ssl3.certHash,
                                 sizeof(pcce->certHash))) {
                    cce = *pcce;
                } else {
                    /* The cert doesen't match the SID cache entry,
                    ** so invalidate the SID cache entry.
                    */
                    psce->valid = 0;
                    psce = 0;
                    pcce = 0;
                }
                UnlockSidCacheLock(cache->certCacheLock);
            } else {
                /* what the ??.  Didn't get the cert cache lock.
                ** Don't invalidate the SID cache entry, but don't find it.
                */
                PORT_AssertNotReached("Didn't get cert Cache Lock!");
                psce = 0;
                pcce = 0;
            }
        }
        if (psce && ((cndx = psce->u.ssl3.srvNameIndex) != -1)) {
            PRUint32 gotLock = LockSidCacheLock(cache->srvNameCacheLock,
                                                now);
            if (gotLock) {
                psnce = &cache->srvNameCacheData[cndx];

                if (!PORT_Memcmp(psnce->nameHash, psce->u.ssl3.srvNameHash,
                                 SHA256_LENGTH)) {
                    snce = *psnce;
                } else {
                    /* The name doesen't match the SID cache entry,
                    ** so invalidate the SID cache entry.
                    */
                    psce->valid = 0;
                    psce = 0;
                    psnce = 0;
                }
                UnlockSidCacheLock(cache->srvNameCacheLock);
            } else {
                /* what the ??.  Didn't get the cert cache lock.
                ** Don't invalidate the SID cache entry, but don't find it.
                */
                PORT_AssertNotReached("Didn't get name Cache Lock!");
                psce = 0;
                psnce = 0;
            }
        }
        if (psce) {
            psce->lastAccessTime = sslNow;
            sce = *psce; /* grab a copy while holding the lock */
        }
    }
    UnlockSet(cache, set);
    if (psce) {
        /* sce conains a copy of the cache entry.
        ** Convert shared memory format to local format
        */
        sid = ConvertToSID(&sce, pcce ? &cce : 0, psnce ? &snce : 0);
    }
    return sid;
}

/*
** Place a sid into the cache, if it isn't already there.
*/
void
ssl_ServerCacheSessionID(sslSessionID *sid, PRTime creationTime)
{
    PORT_Assert(sid);

    sidCacheEntry sce;
    PRUint32 now = 0;
    cacheDesc *cache = &globalCache;

    if (sid->u.ssl3.sessionIDLength == 0) {
        return;
    }

    if (sid->cached == never_cached || sid->cached == invalid_cache) {
        PRUint32 set;
        SECItem *name;

        PORT_Assert(sid->creationTime != 0);
        if (!sid->creationTime)
            sid->lastAccessTime = sid->creationTime = creationTime;
        /* override caller's expiration time, which uses client timeout
         * duration, not server timeout duration.
         */
        sid->expirationTime =
            sid->creationTime + cache->ssl3Timeout * PR_USEC_PER_SEC;
        SSL_TRC(8, ("%d: SSL: CacheMT: cached=%d addr=0x%08x%08x%08x%08x time=%x "
                    "cipherSuite=%d",
                    myPid, sid->cached,
                    sid->addr.pr_s6_addr32[0], sid->addr.pr_s6_addr32[1],
                    sid->addr.pr_s6_addr32[2], sid->addr.pr_s6_addr32[3],
                    sid->creationTime / PR_USEC_PER_SEC,
                    sid->u.ssl3.cipherSuite));
        PRINT_BUF(8, (0, "sessionID:", sid->u.ssl3.sessionID,
                      sid->u.ssl3.sessionIDLength));

        ConvertFromSID(&sce, sid);

        name = &sid->u.ssl3.srvName;
        if (name->len && name->data) {
            now = CacheSrvName(cache, name, &sce);
        }
        if (sid->peerCertDER.data != NULL) {
            now = CacheCert(cache, &sid->peerCertDER, &sce);
            if (!now) {
                return;
            }
        }

        set = SIDindex(cache, &sce.addr, sce.sessionID, sce.sessionIDLength);
        now = LockSet(cache, set, now);
        if (now) {
            PRUint32 next = cache->sidCacheSets[set].next;
            PRUint32 ndx = set * SID_CACHE_ENTRIES_PER_SET + next;

            /* Write out new cache entry */
            cache->sidCacheData[ndx] = sce;

            cache->sidCacheSets[set].next =
                (next + 1) % SID_CACHE_ENTRIES_PER_SET;

            UnlockSet(cache, set);
            sid->cached = in_server_cache;
        }
    }
}

/*
** Although this is static, it is called from ssl via global function pointer
**  ssl_sid_uncache.  This invalidates the referenced cache entry.
*/
void
ssl_ServerUncacheSessionID(sslSessionID *sid)
{
    cacheDesc *cache = &globalCache;
    PRUint8 *sessionID;
    unsigned int sessionIDLength;
    PRErrorCode err;
    PRUint32 set;
    PRUint32 now;
    sidCacheEntry *psce;

    if (sid == NULL)
        return;

    /* Uncaching a SID should never change the error code.
    ** So save it here and restore it before exiting.
    */
    err = PR_GetError();

    sessionID = sid->u.ssl3.sessionID;
    sessionIDLength = sid->u.ssl3.sessionIDLength;
    SSL_TRC(8, ("%d: SSL3: UncacheMT: valid=%d addr=0x%08x%08x%08x%08x time=%x "
                "cipherSuite=%d",
                myPid, sid->cached,
                sid->addr.pr_s6_addr32[0], sid->addr.pr_s6_addr32[1],
                sid->addr.pr_s6_addr32[2], sid->addr.pr_s6_addr32[3],
                sid->creationTime / PR_USEC_PER_SEC,
                sid->u.ssl3.cipherSuite));
    PRINT_BUF(8, (0, "sessionID:", sessionID, sessionIDLength));
    set = SIDindex(cache, &sid->addr, sessionID, sessionIDLength);
    now = LockSet(cache, set, 0);
    if (now) {
        psce = FindSID(cache, set, now, &sid->addr, sessionID, sessionIDLength);
        if (psce) {
            psce->valid = 0;
        }
        UnlockSet(cache, set);
    }
    sid->cached = invalid_cache;
    PORT_SetError(err);
}

static void
CloseCache(cacheDesc *cache)
{
    int locks_initialized = cache->numSIDCacheLocksInitialized;

    if (cache->cacheMem) {
        if (cache->sharedCache) {
            sidCacheLock *pLock = cache->sidCacheLocks;
            for (; locks_initialized > 0; --locks_initialized, ++pLock) {
                /* If everInherited is true, this shared cache was (and may
                ** still be) in use by multiple processes.  We do not wish to
                ** destroy the mutexes while they are still in use, but we do
                ** want to free mutex resources associated with this process.
                */
                sslMutex_Destroy(&pLock->mutex,
                                 cache->sharedCache->everInherited);
            }
        }
        if (cache->shared) {
            PR_MemUnmap(cache->cacheMem, cache->cacheMemSize);
        } else {
            PORT_Free(cache->cacheMem);
        }
        cache->cacheMem = NULL;
    }
    if (cache->cacheMemMap) {
        PR_CloseFileMap(cache->cacheMemMap);
        cache->cacheMemMap = NULL;
    }
    memset(cache, 0, sizeof *cache);
}

static SECStatus
InitCache(cacheDesc *cache, int maxCacheEntries, int maxCertCacheEntries,
          int maxSrvNameCacheEntries, PRUint32 ssl3_timeout,
          const char *directory, PRBool shared)
{
    ptrdiff_t ptr;
    sidCacheLock *pLock;
    char *cacheMem;
    PRFileMap *cacheMemMap;
    char *cfn = NULL; /* cache file name */
    int locks_initialized = 0;
    int locks_to_initialize = 0;
    PRUint32 init_time;

    if ((!cache) || (maxCacheEntries < 0) || (!directory)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (cache->cacheMem) {
        /* Already done */
        return SECSuccess;
    }

    /* make sure loser can clean up properly */
    cache->shared = shared;
    cache->cacheMem = cacheMem = NULL;
    cache->cacheMemMap = cacheMemMap = NULL;
    cache->sharedCache = (cacheDesc *)0;

    cache->numSIDCacheLocksInitialized = 0;
    cache->nextCertCacheEntry = 0;
    cache->stopPolling = PR_FALSE;
    cache->everInherited = PR_FALSE;
    cache->poller = NULL;
    cache->mutexTimeout = 0;

    cache->numSIDCacheEntries = maxCacheEntries ? maxCacheEntries
                                                : DEF_SID_CACHE_ENTRIES;
    cache->numSIDCacheSets =
        SID_HOWMANY(cache->numSIDCacheEntries, SID_CACHE_ENTRIES_PER_SET);

    cache->numSIDCacheEntries =
        cache->numSIDCacheSets * SID_CACHE_ENTRIES_PER_SET;

    cache->numSIDCacheLocks =
        PR_MIN(cache->numSIDCacheSets, ssl_max_sid_cache_locks);

    cache->numSIDCacheSetsPerLock =
        SID_HOWMANY(cache->numSIDCacheSets, cache->numSIDCacheLocks);

    cache->numCertCacheEntries = (maxCertCacheEntries > 0) ? maxCertCacheEntries
                                                           : 0;
    cache->numSrvNameCacheEntries = (maxSrvNameCacheEntries >= 0) ? maxSrvNameCacheEntries
                                                                  : DEF_NAME_CACHE_ENTRIES;

    /* compute size of shared memory, and offsets of all pointers */
    ptr = 0;
    cache->cacheMem = (char *)ptr;
    ptr += SID_ROUNDUP(sizeof(cacheDesc), SID_ALIGNMENT);

    cache->sidCacheLocks = (sidCacheLock *)ptr;
    cache->keyCacheLock = cache->sidCacheLocks + cache->numSIDCacheLocks;
    cache->certCacheLock = cache->keyCacheLock + 1;
    cache->srvNameCacheLock = cache->certCacheLock + 1;
    ptr = (ptrdiff_t)(cache->srvNameCacheLock + 1);
    ptr = SID_ROUNDUP(ptr, SID_ALIGNMENT);

    cache->sidCacheSets = (sidCacheSet *)ptr;
    ptr = (ptrdiff_t)(cache->sidCacheSets + cache->numSIDCacheSets);
    ptr = SID_ROUNDUP(ptr, SID_ALIGNMENT);

    cache->sidCacheData = (sidCacheEntry *)ptr;
    ptr = (ptrdiff_t)(cache->sidCacheData + cache->numSIDCacheEntries);
    ptr = SID_ROUNDUP(ptr, SID_ALIGNMENT);

    cache->certCacheData = (certCacheEntry *)ptr;
    cache->sidCacheSize =
        (char *)cache->certCacheData - (char *)cache->sidCacheData;

    if (cache->numCertCacheEntries < MIN_CERT_CACHE_ENTRIES) {
        /* This is really a poor way to computer this! */
        cache->numCertCacheEntries = cache->sidCacheSize / sizeof(certCacheEntry);
        if (cache->numCertCacheEntries < MIN_CERT_CACHE_ENTRIES)
            cache->numCertCacheEntries = MIN_CERT_CACHE_ENTRIES;
    }
    ptr = (ptrdiff_t)(cache->certCacheData + cache->numCertCacheEntries);
    ptr = SID_ROUNDUP(ptr, SID_ALIGNMENT);

    cache->keyCacheData = (SSLWrappedSymWrappingKey *)ptr;
    cache->certCacheSize =
        (char *)cache->keyCacheData - (char *)cache->certCacheData;

    cache->numKeyCacheEntries = SSL_NUM_WRAP_KEYS * SSL_NUM_WRAP_MECHS;
    ptr = (ptrdiff_t)(cache->keyCacheData + cache->numKeyCacheEntries);
    ptr = SID_ROUNDUP(ptr, SID_ALIGNMENT);

    cache->keyCacheSize = (char *)ptr - (char *)cache->keyCacheData;

    cache->ticketKeyNameSuffix = (PRUint8 *)ptr;
    ptr = (ptrdiff_t)(cache->ticketKeyNameSuffix +
                      SELF_ENCRYPT_KEY_VAR_NAME_LEN);
    ptr = SID_ROUNDUP(ptr, SID_ALIGNMENT);

    cache->ticketEncKey = (encKeyCacheEntry *)ptr;
    ptr = (ptrdiff_t)(cache->ticketEncKey + 1);
    ptr = SID_ROUNDUP(ptr, SID_ALIGNMENT);

    cache->ticketMacKey = (encKeyCacheEntry *)ptr;
    ptr = (ptrdiff_t)(cache->ticketMacKey + 1);
    ptr = SID_ROUNDUP(ptr, SID_ALIGNMENT);

    cache->ticketKeysValid = (PRUint32 *)ptr;
    ptr = (ptrdiff_t)(cache->ticketKeysValid + 1);
    ptr = SID_ROUNDUP(ptr, SID_ALIGNMENT);

    cache->srvNameCacheData = (srvNameCacheEntry *)ptr;
    cache->srvNameCacheSize =
        cache->numSrvNameCacheEntries * sizeof(srvNameCacheEntry);
    ptr = (ptrdiff_t)(cache->srvNameCacheData + cache->numSrvNameCacheEntries);
    ptr = SID_ROUNDUP(ptr, SID_ALIGNMENT);

    cache->cacheMemSize = ptr;

    if (ssl3_timeout) {
        if (ssl3_timeout > MAX_SSL3_TIMEOUT) {
            ssl3_timeout = MAX_SSL3_TIMEOUT;
        }
        if (ssl3_timeout < MIN_SSL3_TIMEOUT) {
            ssl3_timeout = MIN_SSL3_TIMEOUT;
        }
        cache->ssl3Timeout = ssl3_timeout;
    } else {
        cache->ssl3Timeout = DEF_SSL3_TIMEOUT;
    }

    if (shared) {
/* Create file names */
#if defined(XP_UNIX)
        /* there's some confusion here about whether PR_OpenAnonFileMap wants
        ** a directory name or a file name for its first argument.
        cfn = PR_smprintf("%s/.sslsvrcache.%d", directory, myPid);
        */
        cfn = PR_smprintf("%s", directory);
#elif defined(XP_WIN32)
        cfn = PR_smprintf("%s/svrcache_%d_%x.ssl", directory, myPid,
                          GetCurrentThreadId());
#else
#error "Don't know how to create file name for this platform!"
#endif
        if (!cfn) {
            goto loser;
        }

        /* Create cache */
        cacheMemMap = PR_OpenAnonFileMap(cfn, cache->cacheMemSize,
                                         PR_PROT_READWRITE);

        PR_smprintf_free(cfn);
        if (!cacheMemMap) {
            goto loser;
        }

        cacheMem = PR_MemMap(cacheMemMap, 0, cache->cacheMemSize);
    } else {
        cacheMem = PORT_Alloc(cache->cacheMemSize);
    }

    if (!cacheMem) {
        goto loser;
    }

    /* Initialize shared memory. This may not be necessary on all platforms */
    memset(cacheMem, 0, cache->cacheMemSize);

    /* Copy cache descriptor header into shared memory */
    memcpy(cacheMem, cache, sizeof *cache);

    /* save private copies of these values */
    cache->cacheMemMap = cacheMemMap;
    cache->cacheMem = cacheMem;
    cache->sharedCache = (cacheDesc *)cacheMem;

    /* Fix pointers in our private copy of cache descriptor to point to
    ** spaces in shared memory
    */
    cache->sidCacheLocks = (sidCacheLock *)(cache->cacheMem + (ptrdiff_t)cache->sidCacheLocks);
    cache->keyCacheLock = (sidCacheLock *)(cache->cacheMem + (ptrdiff_t)cache->keyCacheLock);
    cache->certCacheLock = (sidCacheLock *)(cache->cacheMem + (ptrdiff_t)cache->certCacheLock);
    cache->srvNameCacheLock = (sidCacheLock *)(cache->cacheMem + (ptrdiff_t)cache->srvNameCacheLock);
    cache->sidCacheSets = (sidCacheSet *)(cache->cacheMem + (ptrdiff_t)cache->sidCacheSets);
    cache->sidCacheData = (sidCacheEntry *)(cache->cacheMem + (ptrdiff_t)cache->sidCacheData);
    cache->certCacheData = (certCacheEntry *)(cache->cacheMem + (ptrdiff_t)cache->certCacheData);
    cache->keyCacheData = (SSLWrappedSymWrappingKey *)(cache->cacheMem + (ptrdiff_t)cache->keyCacheData);
    cache->ticketKeyNameSuffix = (PRUint8 *)(cache->cacheMem + (ptrdiff_t)cache->ticketKeyNameSuffix);
    cache->ticketEncKey = (encKeyCacheEntry *)(cache->cacheMem + (ptrdiff_t)cache->ticketEncKey);
    cache->ticketMacKey = (encKeyCacheEntry *)(cache->cacheMem + (ptrdiff_t)cache->ticketMacKey);
    cache->ticketKeysValid = (PRUint32 *)(cache->cacheMem + (ptrdiff_t)cache->ticketKeysValid);
    cache->srvNameCacheData = (srvNameCacheEntry *)(cache->cacheMem + (ptrdiff_t)cache->srvNameCacheData);

    /* initialize the locks */
    init_time = ssl_CacheNow();
    pLock = cache->sidCacheLocks;
    for (locks_to_initialize = cache->numSIDCacheLocks + 3;
         locks_initialized < locks_to_initialize;
         ++locks_initialized, ++pLock) {

        SECStatus err = sslMutex_Init(&pLock->mutex, shared);
        if (err) {
            cache->numSIDCacheLocksInitialized = locks_initialized;
            goto loser;
        }
        pLock->timeStamp = init_time;
        pLock->pid = 0;
    }
    cache->numSIDCacheLocksInitialized = locks_initialized;

    return SECSuccess;

loser:
    CloseCache(cache);
    PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
    return SECFailure;
}

PRUint32
SSL_GetMaxServerCacheLocks(void)
{
    return ssl_max_sid_cache_locks + 2;
    /* The extra two are the cert cache lock and the key cache lock. */
}

SECStatus
SSL_SetMaxServerCacheLocks(PRUint32 maxLocks)
{
    /* Minimum is 1 sid cache lock, 1 cert cache lock and 1 key cache lock.
    ** We'd like to test for a maximum value, but not all platforms' header
    ** files provide a symbol or function or other means of determining
    ** the maximum, other than trial and error.
    */
    if (maxLocks < 3) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    ssl_max_sid_cache_locks = maxLocks - 2;
    /* The extra two are the cert cache lock and the key cache lock. */
    return SECSuccess;
}

PR_STATIC_ASSERT(sizeof(srvNameCacheEntry) == 1072);

static SECStatus
ssl_ConfigServerSessionIDCacheInstanceWithOpt(cacheDesc *cache,
                                              PRUint32 ssl3_timeout,
                                              const char *directory,
                                              PRBool shared,
                                              int maxCacheEntries,
                                              int maxCertCacheEntries,
                                              int maxSrvNameCacheEntries)
{
    SECStatus rv;

    rv = ssl_InitSessionCache();
    if (rv != SECSuccess) {
        return rv;
    }

    myPid = SSL_GETPID();
    if (!directory) {
        directory = DEFAULT_CACHE_DIRECTORY;
    }
    rv = InitCache(cache, maxCacheEntries, maxCertCacheEntries,
                   maxSrvNameCacheEntries, ssl3_timeout, directory, shared);
    if (rv) {
        return SECFailure;
    }

    ssl_sid_lookup = ServerSessionIDLookup;
    return SECSuccess;
}

SECStatus
SSL_ConfigServerSessionIDCacheInstance(cacheDesc *cache,
                                       int maxCacheEntries,
                                       PRUint32 ssl2_timeout,
                                       PRUint32 ssl3_timeout,
                                       const char *directory, PRBool shared)
{
    return ssl_ConfigServerSessionIDCacheInstanceWithOpt(cache,
                                                         ssl3_timeout,
                                                         directory,
                                                         shared,
                                                         maxCacheEntries,
                                                         -1, -1);
}

SECStatus
SSL_ConfigServerSessionIDCache(int maxCacheEntries,
                               PRUint32 ssl2_timeout,
                               PRUint32 ssl3_timeout,
                               const char *directory)
{
    ssl_InitSessionCacheLocks(PR_FALSE);
    return SSL_ConfigServerSessionIDCacheInstance(&globalCache,
                                                  maxCacheEntries, ssl2_timeout, ssl3_timeout, directory, PR_FALSE);
}

SECStatus
SSL_ShutdownServerSessionIDCacheInstance(cacheDesc *cache)
{
    CloseCache(cache);
    return SECSuccess;
}

SECStatus
SSL_ShutdownServerSessionIDCache(void)
{
#if defined(XP_UNIX)
    /* Stop the thread that polls cache for expired locks on Unix */
    StopLockPoller(&globalCache);
#endif
    SSL3_ShutdownServerCache();
    return SSL_ShutdownServerSessionIDCacheInstance(&globalCache);
}

/* Use this function, instead of SSL_ConfigServerSessionIDCache,
 * if the cache will be shared by multiple processes.
 */
static SECStatus
ssl_ConfigMPServerSIDCacheWithOpt(PRUint32 ssl3_timeout,
                                  const char *directory,
                                  int maxCacheEntries,
                                  int maxCertCacheEntries,
                                  int maxSrvNameCacheEntries)
{
    char *envValue;
    char *inhValue;
    cacheDesc *cache = &globalCache;
    PRUint32 fmStrLen;
    SECStatus result;
    PRStatus prStatus;
    SECStatus putEnvFailed;
    inheritance inherit;
    char fmString[PR_FILEMAP_STRING_BUFSIZE];

    isMultiProcess = PR_TRUE;
    result = ssl_ConfigServerSessionIDCacheInstanceWithOpt(cache,
                                                           ssl3_timeout, directory, PR_TRUE,
                                                           maxCacheEntries, maxCacheEntries, maxSrvNameCacheEntries);
    if (result != SECSuccess)
        return result;

    prStatus = PR_ExportFileMapAsString(cache->cacheMemMap,
                                        sizeof fmString, fmString);
    if ((prStatus != PR_SUCCESS) || !(fmStrLen = strlen(fmString))) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    inherit.cacheMemSize = cache->cacheMemSize;
    inherit.fmStrLen = fmStrLen;

    inhValue = BTOA_DataToAscii((unsigned char *)&inherit, sizeof inherit);
    if (!inhValue || !strlen(inhValue)) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    envValue = PR_smprintf("%s,%s", inhValue, fmString);
    if (!envValue || !strlen(envValue)) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    PORT_Free(inhValue);

    putEnvFailed = (SECStatus)NSS_PutEnv(envVarName, envValue);
    PR_smprintf_free(envValue);
    if (putEnvFailed) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        result = SECFailure;
    }

#if defined(XP_UNIX)
    /* Launch thread to poll cache for expired locks on Unix */
    LaunchLockPoller(cache);
#endif
    return result;
}

/* Use this function, instead of SSL_ConfigServerSessionIDCache,
 * if the cache will be shared by multiple processes.
 */
SECStatus
SSL_ConfigMPServerSIDCache(int maxCacheEntries,
                           PRUint32 ssl2_timeout,
                           PRUint32 ssl3_timeout,
                           const char *directory)
{
    return ssl_ConfigMPServerSIDCacheWithOpt(ssl3_timeout,
                                             directory,
                                             maxCacheEntries,
                                             -1, -1);
}

SECStatus
SSL_ConfigServerSessionIDCacheWithOpt(
    PRUint32 ssl2_timeout,
    PRUint32 ssl3_timeout,
    const char *directory,
    int maxCacheEntries,
    int maxCertCacheEntries,
    int maxSrvNameCacheEntries,
    PRBool enableMPCache)
{
    if (!enableMPCache) {
        ssl_InitSessionCacheLocks(PR_FALSE);
        return ssl_ConfigServerSessionIDCacheInstanceWithOpt(&globalCache,
                                                             ssl3_timeout, directory, PR_FALSE,
                                                             maxCacheEntries, maxCertCacheEntries, maxSrvNameCacheEntries);
    } else {
        return ssl_ConfigMPServerSIDCacheWithOpt(ssl3_timeout, directory,
                                                 maxCacheEntries, maxCertCacheEntries, maxSrvNameCacheEntries);
    }
}

SECStatus
SSL_InheritMPServerSIDCacheInstance(cacheDesc *cache, const char *envString)
{
    unsigned char *decoString = NULL;
    char *fmString = NULL;
    char *myEnvString = NULL;
    unsigned int decoLen;
    inheritance inherit;
    cacheDesc my;
    SECStatus status = ssl_InitSessionCache();

    if (status != SECSuccess) {
        return status;
    }

    myPid = SSL_GETPID();

    /* If this child was created by fork(), and not by exec() on unix,
    ** then isMultiProcess will already be set.
    ** If not, we'll set it below.
    */
    if (isMultiProcess) {
        if (cache && cache->sharedCache) {
            cache->sharedCache->everInherited = PR_TRUE;
        }
        return SECSuccess; /* already done. */
    }

    ssl_InitSessionCacheLocks(PR_FALSE);

    ssl_sid_lookup = ServerSessionIDLookup;

    if (!envString) {
        envString = PR_GetEnvSecure(envVarName);
        if (!envString) {
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }
    }
    myEnvString = PORT_Strdup(envString);
    if (!myEnvString)
        return SECFailure;
    fmString = strchr(myEnvString, ',');
    if (!fmString)
        goto loser;
    *fmString++ = 0;

    decoString = ATOB_AsciiToData(myEnvString, &decoLen);
    if (!decoString) {
        goto loser;
    }
    if (decoLen != sizeof inherit) {
        goto loser;
    }

    PORT_Memcpy(&inherit, decoString, sizeof inherit);

    if (strlen(fmString) != inherit.fmStrLen) {
        goto loser;
    }

    memset(cache, 0, sizeof *cache);
    cache->cacheMemSize = inherit.cacheMemSize;

    /* Create cache */
    cache->cacheMemMap = PR_ImportFileMapFromString(fmString);
    if (!cache->cacheMemMap) {
        goto loser;
    }
    cache->cacheMem = PR_MemMap(cache->cacheMemMap, 0, cache->cacheMemSize);
    if (!cache->cacheMem) {
        goto loser;
    }
    cache->sharedCache = (cacheDesc *)cache->cacheMem;

    if (cache->sharedCache->cacheMemSize != cache->cacheMemSize) {
        goto loser;
    }

    /* We're now going to overwrite the local cache instance with the
    ** shared copy of the cache struct, then update several values in
    ** the local cache using the values for cache->cacheMemMap and
    ** cache->cacheMem computed just above.  So, we copy cache into
    ** the automatic variable "my", to preserve the variables while
    ** cache is overwritten.
    */
    my = *cache;                                      /* save values computed above. */
    memcpy(cache, cache->sharedCache, sizeof *cache); /* overwrite */

    /* Fix pointers in our private copy of cache descriptor to point to
    ** spaces in shared memory, whose address is now in "my".
    */
    cache->sidCacheLocks = (sidCacheLock *)(my.cacheMem + (ptrdiff_t)cache->sidCacheLocks);
    cache->keyCacheLock = (sidCacheLock *)(my.cacheMem + (ptrdiff_t)cache->keyCacheLock);
    cache->certCacheLock = (sidCacheLock *)(my.cacheMem + (ptrdiff_t)cache->certCacheLock);
    cache->srvNameCacheLock = (sidCacheLock *)(my.cacheMem + (ptrdiff_t)cache->srvNameCacheLock);
    cache->sidCacheSets = (sidCacheSet *)(my.cacheMem + (ptrdiff_t)cache->sidCacheSets);
    cache->sidCacheData = (sidCacheEntry *)(my.cacheMem + (ptrdiff_t)cache->sidCacheData);
    cache->certCacheData = (certCacheEntry *)(my.cacheMem + (ptrdiff_t)cache->certCacheData);
    cache->keyCacheData = (SSLWrappedSymWrappingKey *)(my.cacheMem + (ptrdiff_t)cache->keyCacheData);
    cache->ticketKeyNameSuffix = (PRUint8 *)(my.cacheMem + (ptrdiff_t)cache->ticketKeyNameSuffix);
    cache->ticketEncKey = (encKeyCacheEntry *)(my.cacheMem + (ptrdiff_t)cache->ticketEncKey);
    cache->ticketMacKey = (encKeyCacheEntry *)(my.cacheMem + (ptrdiff_t)cache->ticketMacKey);
    cache->ticketKeysValid = (PRUint32 *)(my.cacheMem + (ptrdiff_t)cache->ticketKeysValid);
    cache->srvNameCacheData = (srvNameCacheEntry *)(my.cacheMem + (ptrdiff_t)cache->srvNameCacheData);

    cache->cacheMemMap = my.cacheMemMap;
    cache->cacheMem = my.cacheMem;
    cache->sharedCache = (cacheDesc *)cache->cacheMem;

    PORT_Free(myEnvString);
    PORT_Free(decoString);

    /* mark that we have inherited this. */
    cache->sharedCache->everInherited = PR_TRUE;
    isMultiProcess = PR_TRUE;

    return SECSuccess;

loser:
    PORT_Free(myEnvString);
    if (decoString)
        PORT_Free(decoString);
    CloseCache(cache);
    PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
    return SECFailure;
}

SECStatus
SSL_InheritMPServerSIDCache(const char *envString)
{
    return SSL_InheritMPServerSIDCacheInstance(&globalCache, envString);
}

#if defined(XP_UNIX)

#define SID_LOCK_EXPIRATION_TIMEOUT 30 /* seconds */

static void
LockPoller(void *arg)
{
    cacheDesc *cache = (cacheDesc *)arg;
    cacheDesc *sharedCache = cache->sharedCache;
    sidCacheLock *pLock;
    PRIntervalTime timeout;
    PRUint32 now;
    PRUint32 then;
    int locks_polled = 0;
    int locks_to_poll = cache->numSIDCacheLocks + 2;
    PRUint32 expiration = cache->mutexTimeout;

    timeout = PR_SecondsToInterval(expiration);
    while (!sharedCache->stopPolling) {
        PR_Sleep(timeout);
        if (sharedCache->stopPolling)
            break;

        now = ssl_CacheNow();
        then = now - expiration;
        for (pLock = cache->sidCacheLocks, locks_polled = 0;
             locks_to_poll > locks_polled && !sharedCache->stopPolling;
             ++locks_polled, ++pLock) {
            pid_t pid;

            if (pLock->timeStamp < then &&
                pLock->timeStamp != 0 &&
                (pid = pLock->pid) != 0) {

                /* maybe we should try the lock? */
                int result = kill(pid, 0);
                if (result < 0 && errno == ESRCH) {
                    SECStatus rv;
                    /* No process exists by that pid any more.
                    ** Treat this mutex as abandoned.
                    */
                    pLock->timeStamp = now;
                    pLock->pid = 0;
                    rv = sslMutex_Unlock(&pLock->mutex);
                    if (rv != SECSuccess) {
                        /* Now what? */
                    }
                }
            }
        } /* end of loop over locks */
    } /* end of entire polling loop */
}

/* Launch thread to poll cache for expired locks */
static SECStatus
LaunchLockPoller(cacheDesc *cache)
{
    const char *timeoutString;
    PRThread *pollerThread;

    cache->mutexTimeout = SID_LOCK_EXPIRATION_TIMEOUT;
    timeoutString = PR_GetEnvSecure("NSS_SSL_SERVER_CACHE_MUTEX_TIMEOUT");
    if (timeoutString) {
        long newTime = strtol(timeoutString, 0, 0);
        if (newTime == 0)
            return SECSuccess; /* application doesn't want poller thread */
        if (newTime > 0)
            cache->mutexTimeout = (PRUint32)newTime;
        /* if error (newTime < 0) ignore it and use default */
    }

    pollerThread =
        PR_CreateThread(PR_USER_THREAD, LockPoller, cache, PR_PRIORITY_NORMAL,
                        PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0);
    if (!pollerThread) {
        return SECFailure;
    }
    cache->poller = pollerThread;
    return SECSuccess;
}

/* Stop the thread that polls cache for expired locks */
static SECStatus
StopLockPoller(cacheDesc *cache)
{
    if (!cache->poller) {
        return SECSuccess;
    }
    cache->sharedCache->stopPolling = PR_TRUE;
    if (PR_Interrupt(cache->poller) != PR_SUCCESS) {
        return SECFailure;
    }
    if (PR_JoinThread(cache->poller) != PR_SUCCESS) {
        return SECFailure;
    }
    cache->poller = NULL;
    return SECSuccess;
}
#endif

/************************************************************************
 *  Code dealing with shared wrapped symmetric wrapping keys below      *
 ************************************************************************/

/* The asymmetric key we use for wrapping the self-encryption keys. This is a
 * global structure that can be initialized without a socket. Access is
 * synchronized on the reader-writer lock. This is setup either by calling
 * SSL_SetSessionTicketKeyPair() or by configuring a certificate of the
 * ssl_auth_rsa_decrypt type. */
static struct {
    PRCallOnceType setup;
    PRRWLock *lock;
    SECKEYPublicKey *pubKey;
    SECKEYPrivateKey *privKey;
    PRBool configured;
} ssl_self_encrypt_key_pair;

/* The symmetric self-encryption keys. This requires a socket to construct
 * and requires that the global structure be initialized before use.
 */
static sslSelfEncryptKeys ssl_self_encrypt_keys;

/* Externalize the self encrypt keys. Purely used for testing. */
sslSelfEncryptKeys *
ssl_GetSelfEncryptKeysInt()
{
    return &ssl_self_encrypt_keys;
}

static void
ssl_CleanupSelfEncryptKeyPair()
{
    if (ssl_self_encrypt_key_pair.pubKey) {
        PORT_Assert(ssl_self_encrypt_key_pair.privKey);
        SECKEY_DestroyPublicKey(ssl_self_encrypt_key_pair.pubKey);
        SECKEY_DestroyPrivateKey(ssl_self_encrypt_key_pair.privKey);
    }
}

void
ssl_ResetSelfEncryptKeys()
{
    if (ssl_self_encrypt_keys.encKey) {
        PORT_Assert(ssl_self_encrypt_keys.macKey);
        PK11_FreeSymKey(ssl_self_encrypt_keys.encKey);
        PK11_FreeSymKey(ssl_self_encrypt_keys.macKey);
    }
    PORT_Memset(&ssl_self_encrypt_keys, 0,
                sizeof(ssl_self_encrypt_keys));
}

static SECStatus
ssl_SelfEncryptShutdown(void *appData, void *nssData)
{
    ssl_CleanupSelfEncryptKeyPair();
    PR_DestroyRWLock(ssl_self_encrypt_key_pair.lock);
    PORT_Memset(&ssl_self_encrypt_key_pair, 0,
                sizeof(ssl_self_encrypt_key_pair));

    ssl_ResetSelfEncryptKeys();
    return SECSuccess;
}

static PRStatus
ssl_SelfEncryptSetup(void)
{
    SECStatus rv = NSS_RegisterShutdown(ssl_SelfEncryptShutdown, NULL);
    if (rv != SECSuccess) {
        return PR_FAILURE;
    }
    ssl_self_encrypt_key_pair.lock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, NULL);
    if (!ssl_self_encrypt_key_pair.lock) {
        return PR_FAILURE;
    }
    return PR_SUCCESS;
}

/* Configure a self encryption key pair.  |explicitConfig| is set to true for
 * calls to SSL_SetSessionTicketKeyPair(), false for implicit configuration.
 * This assumes that the setup has been run. */
static SECStatus
ssl_SetSelfEncryptKeyPair(SECKEYPublicKey *pubKey,
                          SECKEYPrivateKey *privKey,
                          PRBool explicitConfig)
{
    SECKEYPublicKey *pubKeyCopy, *oldPubKey;
    SECKEYPrivateKey *privKeyCopy, *oldPrivKey;

    PORT_Assert(ssl_self_encrypt_key_pair.lock);
    PR_RWLock_Wlock(ssl_self_encrypt_key_pair.lock);
    pubKeyCopy = SECKEY_CopyPublicKey(pubKey);
    privKeyCopy = SECKEY_CopyPrivateKey(privKey);
    if (!pubKeyCopy || !privKeyCopy) {
        PR_RWLock_Unlock(ssl_self_encrypt_key_pair.lock);
        SECKEY_DestroyPublicKey(pubKeyCopy);
        SECKEY_DestroyPrivateKey(privKeyCopy);
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return SECFailure;
    }
    oldPubKey = ssl_self_encrypt_key_pair.pubKey;
    oldPrivKey = ssl_self_encrypt_key_pair.privKey;
    ssl_self_encrypt_key_pair.pubKey = pubKeyCopy;
    ssl_self_encrypt_key_pair.privKey = privKeyCopy;
    ssl_self_encrypt_key_pair.configured = explicitConfig;
    PR_RWLock_Unlock(ssl_self_encrypt_key_pair.lock);

    if (oldPubKey) {
        PORT_Assert(oldPrivKey);
        SECKEY_DestroyPublicKey(oldPubKey);
        SECKEY_DestroyPrivateKey(oldPrivKey);
    }

    return SECSuccess;
}

/* This is really the self-encryption keys but it has the
 * wrong name for historical API stability reasons. */
SECStatus
SSL_SetSessionTicketKeyPair(SECKEYPublicKey *pubKey,
                            SECKEYPrivateKey *privKey)
{
    if (SECKEY_GetPublicKeyType(pubKey) != rsaKey ||
        SECKEY_GetPrivateKeyType(privKey) != rsaKey) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (PR_SUCCESS != PR_CallOnce(&ssl_self_encrypt_key_pair.setup,
                                  &ssl_SelfEncryptSetup)) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    return ssl_SetSelfEncryptKeyPair(pubKey, privKey, PR_TRUE);
}

/* When configuring a server cert, we should save the RSA key in case it is
 * needed for self-encryption. This saves the latest copy, unless there has
 * been an explicit call to SSL_SetSessionTicketKeyPair(). */
SECStatus
ssl_MaybeSetSelfEncryptKeyPair(const sslKeyPair *keyPair)
{
    PRBool configured;

    if (PR_SUCCESS != PR_CallOnce(&ssl_self_encrypt_key_pair.setup,
                                  &ssl_SelfEncryptSetup)) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    PR_RWLock_Rlock(ssl_self_encrypt_key_pair.lock);
    configured = ssl_self_encrypt_key_pair.configured;
    PR_RWLock_Unlock(ssl_self_encrypt_key_pair.lock);
    if (configured) {
        return SECSuccess;
    }
    return ssl_SetSelfEncryptKeyPair(keyPair->pubKey,
                                     keyPair->privKey, PR_FALSE);
}

static SECStatus
ssl_GetSelfEncryptKeyPair(SECKEYPublicKey **pubKey,
                          SECKEYPrivateKey **privKey)
{
    if (PR_SUCCESS != PR_CallOnce(&ssl_self_encrypt_key_pair.setup,
                                  &ssl_SelfEncryptSetup)) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    SECKEYPublicKey *pubKeyCopy = NULL;
    SECKEYPrivateKey *privKeyCopy = NULL;
    PRBool noKey = PR_FALSE;

    PR_RWLock_Rlock(ssl_self_encrypt_key_pair.lock);
    if (ssl_self_encrypt_key_pair.pubKey && ssl_self_encrypt_key_pair.privKey) {
        pubKeyCopy = SECKEY_CopyPublicKey(ssl_self_encrypt_key_pair.pubKey);
        privKeyCopy = SECKEY_CopyPrivateKey(ssl_self_encrypt_key_pair.privKey);
    } else {
        noKey = PR_TRUE;
    }
    PR_RWLock_Unlock(ssl_self_encrypt_key_pair.lock);

    if (noKey) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    if (!pubKeyCopy || !privKeyCopy) {
        SECKEY_DestroyPublicKey(pubKeyCopy);
        SECKEY_DestroyPrivateKey(privKeyCopy);
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return SECFailure;
    }

    *pubKey = pubKeyCopy;
    *privKey = privKeyCopy;
    return SECSuccess;
}

static SECStatus
ssl_GenerateSelfEncryptKeys(void *pwArg, PRUint8 *keyName,
                            PK11SymKey **aesKey, PK11SymKey **macKey);

static PRStatus
ssl_GenerateSelfEncryptKeysOnce(void *arg)
{
    SECStatus rv;

    /* Get a copy of the session keys from shared memory. */
    PORT_Memcpy(ssl_self_encrypt_keys.keyName,
                SELF_ENCRYPT_KEY_NAME_PREFIX,
                sizeof(SELF_ENCRYPT_KEY_NAME_PREFIX));
    /* This function calls ssl_GetSelfEncryptKeyPair(), which initializes the
     * key pair stuff.  That allows this to use the same shutdown function. */
    rv = ssl_GenerateSelfEncryptKeys(arg, ssl_self_encrypt_keys.keyName,
                                     &ssl_self_encrypt_keys.encKey,
                                     &ssl_self_encrypt_keys.macKey);
    if (rv != SECSuccess) {
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

SECStatus
ssl_GetSelfEncryptKeys(sslSocket *ss, PRUint8 *keyName,
                       PK11SymKey **encKey, PK11SymKey **macKey)
{
    if (PR_SUCCESS != PR_CallOnceWithArg(&ssl_self_encrypt_keys.setup,
                                         &ssl_GenerateSelfEncryptKeysOnce,
                                         ss->pkcs11PinArg)) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    if (!ssl_self_encrypt_keys.encKey || !ssl_self_encrypt_keys.macKey) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    PORT_Memcpy(keyName, ssl_self_encrypt_keys.keyName,
                sizeof(ssl_self_encrypt_keys.keyName));
    *encKey = ssl_self_encrypt_keys.encKey;
    *macKey = ssl_self_encrypt_keys.macKey;
    return SECSuccess;
}

/* If lockTime is zero, it implies that the lock is not held, and must be
 * aquired here.
 */
static SECStatus
getSvrWrappingKey(unsigned int symWrapMechIndex,
                  unsigned int wrapKeyIndex,
                  SSLWrappedSymWrappingKey *wswk,
                  cacheDesc *cache,
                  PRUint32 lockTime)
{
    PRUint32 now = 0;
    PRBool rv = SECFailure;

    if (!cache->cacheMem) { /* cache is uninitialized */
        PORT_SetError(SSL_ERROR_SERVER_CACHE_NOT_CONFIGURED);
        return SECFailure;
    }

    PRUint32 ndx = (wrapKeyIndex * SSL_NUM_WRAP_MECHS) + symWrapMechIndex;
    SSLWrappedSymWrappingKey *pwswk = cache->keyCacheData + ndx;

    if (!lockTime) {
        now = LockSidCacheLock(cache->keyCacheLock, 0);
        if (!now) {
            return SECFailure;
        }
    }
    if (pwswk->wrapKeyIndex == wrapKeyIndex &&
        pwswk->wrapMechIndex == symWrapMechIndex &&
        pwswk->wrappedSymKeyLen != 0) {
        *wswk = *pwswk;
        rv = SECSuccess;
    }
    if (now) {
        UnlockSidCacheLock(cache->keyCacheLock);
    }
    return rv;
}

SECStatus
ssl_GetWrappingKey(unsigned int wrapMechIndex,
                   unsigned int wrapKeyIndex,
                   SSLWrappedSymWrappingKey *wswk)
{
    PORT_Assert(wrapMechIndex < SSL_NUM_WRAP_MECHS);
    PORT_Assert(wrapKeyIndex < SSL_NUM_WRAP_KEYS);
    if (wrapMechIndex >= SSL_NUM_WRAP_MECHS ||
        wrapKeyIndex >= SSL_NUM_WRAP_KEYS) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    return getSvrWrappingKey(wrapMechIndex, wrapKeyIndex, wswk,
                             &globalCache, 0);
}

/* Wrap and cache a session ticket key. */
static SECStatus
WrapSelfEncryptKey(SECKEYPublicKey *svrPubKey, PK11SymKey *symKey,
                   const char *keyName, encKeyCacheEntry *cacheEntry)
{
    SECItem wrappedKey = { siBuffer, NULL, 0 };

    wrappedKey.len = SECKEY_PublicKeyStrength(svrPubKey);
    PORT_Assert(wrappedKey.len <= sizeof(cacheEntry->bytes));
    if (wrappedKey.len > sizeof(cacheEntry->bytes))
        return PR_FALSE;
    wrappedKey.data = cacheEntry->bytes;

    if (PK11_PubWrapSymKey(CKM_RSA_PKCS, svrPubKey, symKey, &wrappedKey) !=
        SECSuccess) {
        SSL_DBG(("%d: SSL[%s]: Unable to wrap self encrypt key %s.",
                 SSL_GETPID(), "unknown", keyName));
        return SECFailure;
    }
    cacheEntry->length = wrappedKey.len;
    return SECSuccess;
}

static SECStatus
GenerateSelfEncryptKeys(void *pwArg, PRUint8 *keyName, PK11SymKey **aesKey,
                        PK11SymKey **macKey)
{
    PK11SlotInfo *slot;
    CK_MECHANISM_TYPE mechanismArray[2];
    PK11SymKey *aesKeyTmp = NULL;
    PK11SymKey *macKeyTmp = NULL;
    cacheDesc *cache = &globalCache;
    PRUint8 ticketKeyNameSuffixLocal[SELF_ENCRYPT_KEY_VAR_NAME_LEN];
    PRUint8 *ticketKeyNameSuffix;

    if (!cache->cacheMem) {
        /* cache is not initalized. Use stack buffer */
        ticketKeyNameSuffix = ticketKeyNameSuffixLocal;
    } else {
        ticketKeyNameSuffix = cache->ticketKeyNameSuffix;
    }

    if (PK11_GenerateRandom(ticketKeyNameSuffix,
                            SELF_ENCRYPT_KEY_VAR_NAME_LEN) !=
        SECSuccess) {
        SSL_DBG(("%d: SSL[%s]: Unable to generate random key name bytes.",
                 SSL_GETPID(), "unknown"));
        return SECFailure;
    }

    mechanismArray[0] = CKM_AES_CBC;
    mechanismArray[1] = CKM_SHA256_HMAC;

    slot = PK11_GetBestSlotMultiple(mechanismArray, 2, pwArg);
    if (slot) {
        aesKeyTmp = PK11_KeyGen(slot, mechanismArray[0], NULL,
                                AES_256_KEY_LENGTH, pwArg);
        macKeyTmp = PK11_KeyGen(slot, mechanismArray[1], NULL,
                                SHA256_LENGTH, pwArg);
        PK11_FreeSlot(slot);
    }

    if (aesKeyTmp == NULL || macKeyTmp == NULL) {
        SSL_DBG(("%d: SSL[%s]: Unable to generate session ticket keys.",
                 SSL_GETPID(), "unknown"));
        goto loser;
    }
    PORT_Memcpy(keyName, ticketKeyNameSuffix, SELF_ENCRYPT_KEY_VAR_NAME_LEN);
    *aesKey = aesKeyTmp;
    *macKey = macKeyTmp;
    return SECSuccess;

loser:
    if (aesKeyTmp)
        PK11_FreeSymKey(aesKeyTmp);
    if (macKeyTmp)
        PK11_FreeSymKey(macKeyTmp);
    return SECFailure;
}

static SECStatus
GenerateAndWrapSelfEncryptKeys(SECKEYPublicKey *svrPubKey, void *pwArg,
                               PRUint8 *keyName, PK11SymKey **aesKey,
                               PK11SymKey **macKey)
{
    PK11SymKey *aesKeyTmp = NULL;
    PK11SymKey *macKeyTmp = NULL;
    cacheDesc *cache = &globalCache;
    SECStatus rv;

    rv = GenerateSelfEncryptKeys(pwArg, keyName, &aesKeyTmp, &macKeyTmp);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (cache->cacheMem) {
        /* Export the keys to the shared cache in wrapped form. */
        rv = WrapSelfEncryptKey(svrPubKey, aesKeyTmp, "enc key", cache->ticketEncKey);
        if (rv != SECSuccess) {
            goto loser;
        }
        rv = WrapSelfEncryptKey(svrPubKey, macKeyTmp, "mac key", cache->ticketMacKey);
        if (rv != SECSuccess) {
            goto loser;
        }
    }
    *aesKey = aesKeyTmp;
    *macKey = macKeyTmp;
    return SECSuccess;

loser:
    PK11_FreeSymKey(aesKeyTmp);
    PK11_FreeSymKey(macKeyTmp);
    return SECFailure;
}

static SECStatus
UnwrapCachedSelfEncryptKeys(SECKEYPrivateKey *svrPrivKey, PRUint8 *keyName,
                            PK11SymKey **aesKey, PK11SymKey **macKey)
{
    SECItem wrappedKey = { siBuffer, NULL, 0 };
    PK11SymKey *aesKeyTmp = NULL;
    PK11SymKey *macKeyTmp = NULL;
    cacheDesc *cache = &globalCache;

    wrappedKey.data = cache->ticketEncKey->bytes;
    wrappedKey.len = cache->ticketEncKey->length;
    PORT_Assert(wrappedKey.len <= sizeof(cache->ticketEncKey->bytes));
    aesKeyTmp = PK11_PubUnwrapSymKey(svrPrivKey, &wrappedKey,
                                     CKM_AES_CBC, CKA_DECRYPT, 0);

    wrappedKey.data = cache->ticketMacKey->bytes;
    wrappedKey.len = cache->ticketMacKey->length;
    PORT_Assert(wrappedKey.len <= sizeof(cache->ticketMacKey->bytes));
    macKeyTmp = PK11_PubUnwrapSymKey(svrPrivKey, &wrappedKey,
                                     CKM_SHA256_HMAC, CKA_SIGN, 0);

    if (aesKeyTmp == NULL || macKeyTmp == NULL) {
        SSL_DBG(("%d: SSL[%s]: Unable to unwrap session ticket keys.",
                 SSL_GETPID(), "unknown"));
        goto loser;
    }
    SSL_DBG(("%d: SSL[%s]: Successfully unwrapped session ticket keys.",
             SSL_GETPID(), "unknown"));

    PORT_Memcpy(keyName, cache->ticketKeyNameSuffix,
                SELF_ENCRYPT_KEY_VAR_NAME_LEN);
    *aesKey = aesKeyTmp;
    *macKey = macKeyTmp;
    return SECSuccess;

loser:
    if (aesKeyTmp)
        PK11_FreeSymKey(aesKeyTmp);
    if (macKeyTmp)
        PK11_FreeSymKey(macKeyTmp);
    return SECFailure;
}

static SECStatus
ssl_GenerateSelfEncryptKeys(void *pwArg, PRUint8 *keyName,
                            PK11SymKey **encKey, PK11SymKey **macKey)
{
    SECKEYPrivateKey *svrPrivKey = NULL;
    SECKEYPublicKey *svrPubKey = NULL;
    PRUint32 now;
    cacheDesc *cache = &globalCache;

    SECStatus rv = ssl_GetSelfEncryptKeyPair(&svrPubKey, &svrPrivKey);
    if (rv != SECSuccess || !cache->cacheMem) {
        /* No key pair for wrapping, or the cache is uninitialized. Generate
         * keys and return them without caching. */
        rv = GenerateSelfEncryptKeys(pwArg, keyName, encKey, macKey);
    } else {
        now = LockSidCacheLock(cache->keyCacheLock, 0);
        if (!now) {
            goto loser;
        }

        if (*(cache->ticketKeysValid)) {
            rv = UnwrapCachedSelfEncryptKeys(svrPrivKey, keyName, encKey, macKey);
        } else {
            /* Keys do not exist, create them. */
            rv = GenerateAndWrapSelfEncryptKeys(svrPubKey, pwArg, keyName,
                                                encKey, macKey);
            if (rv == SECSuccess) {
                *(cache->ticketKeysValid) = 1;
            }
        }
        UnlockSidCacheLock(cache->keyCacheLock);
    }
    SECKEY_DestroyPublicKey(svrPubKey);
    SECKEY_DestroyPrivateKey(svrPrivKey);
    return rv;

loser:
    UnlockSidCacheLock(cache->keyCacheLock);
    SECKEY_DestroyPublicKey(svrPubKey);
    SECKEY_DestroyPrivateKey(svrPrivKey);
    return SECFailure;
}

/* The caller passes in the new value it wants
 * to set.  This code tests the wrapped sym key entry in the shared memory.
 * If it is uninitialized, this function writes the caller's value into
 * the disk entry, and returns false.
 * Otherwise, it overwrites the caller's wswk with the value obtained from
 * the disk, and returns PR_TRUE.
 * This is all done while holding the locks/mutexes necessary to make
 * the operation atomic.
 */
SECStatus
ssl_SetWrappingKey(SSLWrappedSymWrappingKey *wswk)
{
    cacheDesc *cache = &globalCache;
    PRBool rv = SECFailure;
    PRUint32 ndx;
    PRUint32 now;
    SSLWrappedSymWrappingKey myWswk;

    if (!cache->cacheMem) { /* cache is uninitialized */
        PORT_SetError(SSL_ERROR_SERVER_CACHE_NOT_CONFIGURED);
        return SECFailure;
    }

    PORT_Assert(wswk->wrapMechIndex < SSL_NUM_WRAP_MECHS);
    PORT_Assert(wswk->wrapKeyIndex < SSL_NUM_WRAP_KEYS);
    if (wswk->wrapMechIndex >= SSL_NUM_WRAP_MECHS ||
        wswk->wrapKeyIndex >= SSL_NUM_WRAP_KEYS) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    ndx = (wswk->wrapKeyIndex * SSL_NUM_WRAP_MECHS) + wswk->wrapMechIndex;
    PORT_Memset(&myWswk, 0, sizeof myWswk); /* eliminate UMRs. */

    now = LockSidCacheLock(cache->keyCacheLock, 0);
    if (!now) {
        return SECFailure;
    }
    rv = getSvrWrappingKey(wswk->wrapMechIndex, wswk->wrapKeyIndex,
                           &myWswk, cache, now);
    if (rv == SECSuccess) {
        /* we found it on disk, copy it out to the caller. */
        PORT_Memcpy(wswk, &myWswk, sizeof *wswk);
    } else {
        /* Wasn't on disk, and we're still holding the lock, so write it. */
        cache->keyCacheData[ndx] = *wswk;
    }
    UnlockSidCacheLock(cache->keyCacheLock);
    return rv;
}

#else /* MAC version or other platform */

#include "seccomon.h"
#include "cert.h"
#include "ssl.h"
#include "sslimpl.h"

SECStatus
SSL_ConfigServerSessionIDCache(int maxCacheEntries,
                               PRUint32 ssl2_timeout,
                               PRUint32 ssl3_timeout,
                               const char *directory)
{
    PR_ASSERT(!"SSL servers are not supported on this platform. (SSL_ConfigServerSessionIDCache)");
    return SECFailure;
}

SECStatus
SSL_ConfigMPServerSIDCache(int maxCacheEntries,
                           PRUint32 ssl2_timeout,
                           PRUint32 ssl3_timeout,
                           const char *directory)
{
    PR_ASSERT(!"SSL servers are not supported on this platform. (SSL_ConfigMPServerSIDCache)");
    return SECFailure;
}

SECStatus
SSL_InheritMPServerSIDCache(const char *envString)
{
    PR_ASSERT(!"SSL servers are not supported on this platform. (SSL_InheritMPServerSIDCache)");
    return SECFailure;
}

SECStatus
ssl_GetWrappingKey(unsigned int wrapMechIndex,
                   unsigned int wrapKeyIndex,
                   SSLWrappedSymWrappingKey *wswk)
{
    PR_ASSERT(!"SSL servers are not supported on this platform. (ssl_GetWrappingKey)");
    return SECFailure;
}

/* This is a kind of test-and-set.  The caller passes in the new value it wants
 * to set.  This code tests the wrapped sym key entry in the shared memory.
 * If it is uninitialized, this function writes the caller's value into
 * the disk entry, and returns false.
 * Otherwise, it overwrites the caller's wswk with the value obtained from
 * the disk, and returns PR_TRUE.
 * This is all done while holding the locks/mutexes necessary to make
 * the operation atomic.
 */
SECStatus
ssl_SetWrappingKey(SSLWrappedSymWrappingKey *wswk)
{
    PR_ASSERT(!"SSL servers are not supported on this platform. (ssl_SetWrappingKey)");
    return SECFailure;
}

PRUint32
SSL_GetMaxServerCacheLocks(void)
{
    PR_ASSERT(!"SSL servers are not supported on this platform. (SSL_GetMaxServerCacheLocks)");
    return -1;
}

SECStatus
SSL_SetMaxServerCacheLocks(PRUint32 maxLocks)
{
    PR_ASSERT(!"SSL servers are not supported on this platform. (SSL_SetMaxServerCacheLocks)");
    return SECFailure;
}

#endif /* XP_UNIX || XP_WIN32 */
