/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is PRIVATE to SSL and should be the first thing included by
 * any SSL implementation file.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __sslimpl_h_
#define __sslimpl_h_

#ifdef DEBUG
#undef NDEBUG
#else
#undef NDEBUG
#define NDEBUG
#endif
#include "secport.h"
#include "secerr.h"
#include "sslerr.h"
#include "sslexp.h"
#include "ssl3prot.h"
#include "hasht.h"
#include "cryptohi.h"
#include "pkcs11t.h"
#if defined(XP_UNIX)
#include "unistd.h"
#elif defined(XP_WIN)
#include <process.h>
#endif
#include "nssrwlk.h"
#include "prthread.h"
#include "prclist.h"
#include "private/pprthred.h"

#include "sslt.h" /* for some formerly private types, now public */

typedef struct sslSocketStr sslSocket;
typedef struct sslNamedGroupDefStr sslNamedGroupDef;
typedef struct sslEchConfigStr sslEchConfig;
typedef struct sslEchConfigContentsStr sslEchConfigContents;
typedef struct sslEchCookieDataStr sslEchCookieData;
typedef struct sslEchXtnStateStr sslEchXtnState;
typedef struct sslPskStr sslPsk;
typedef struct sslDelegatedCredentialStr sslDelegatedCredential;
typedef struct sslEphemeralKeyPairStr sslEphemeralKeyPair;
typedef struct TLS13KeyShareEntryStr TLS13KeyShareEntry;
typedef struct tlsSignOrVerifyContextStr tlsSignOrVerifyContext;

#include "sslencode.h"
#include "sslexp.h"
#include "ssl3ext.h"
#include "sslspec.h"

#if defined(DEBUG) || defined(TRACE)
#ifdef __cplusplus
#define Debug 1
#else
extern int Debug;
#endif
#else
#undef Debug
#endif

#if defined(DEBUG) && !defined(TRACE) && !defined(NISCC_TEST)
#define TRACE
#endif

#ifdef TRACE
#define SSL_TRC(a, b)     \
    if (ssl_trace >= (a)) \
    ssl_Trace b
#define PRINT_BUF(a, b)   \
    if (ssl_trace >= (a)) \
    ssl_PrintBuf b
#define PRINT_KEY(a, b)   \
    if (ssl_trace >= (a)) \
    ssl_PrintKey b
#else
#define SSL_TRC(a, b)
#define PRINT_BUF(a, b)
#define PRINT_KEY(a, b)
#endif

#ifdef DEBUG
#define SSL_DBG(b) \
    if (ssl_debug) \
    ssl_Trace b
#else
#define SSL_DBG(b)
#endif

#define LSB(x) ((unsigned char)((x)&0xff))
#define MSB(x) ((unsigned char)(((unsigned)(x)) >> 8))

#define CONST_CAST(T, X) ((T *)(X))

/************************************************************************/

typedef enum { SSLAppOpRead = 0,
               SSLAppOpWrite,
               SSLAppOpRDWR,
               SSLAppOpPost,
               SSLAppOpHeader
} SSLAppOperation;

#define SSL3_SESSIONID_BYTES 32

#define SSL_MIN_CHALLENGE_BYTES 16
#define SSL_MAX_CHALLENGE_BYTES 32

#define SSL3_MASTER_SECRET_LENGTH 48

/* number of wrap mechanisms potentially used to wrap master secrets. */
#define SSL_NUM_WRAP_MECHS 15
#define SSL_NUM_WRAP_KEYS 6

/* This makes the cert cache entry exactly 4k. */
#define SSL_MAX_CACHED_CERT_LEN 4060

#ifndef BPB
#define BPB 8 /* Bits Per Byte */
#endif

/* The default value from RFC 4347 is 1s, which is too slow. */
#define DTLS_RETRANSMIT_INITIAL_MS 50
/* The maximum time to wait between retransmissions. */
#define DTLS_RETRANSMIT_MAX_MS 10000
/* Time to wait in FINISHED state for retransmissions. */
#define DTLS_RETRANSMIT_FINISHED_MS 30000

/* default number of entries in namedGroupPreferences */
#define SSL_NAMED_GROUP_COUNT 35

/* The maximum DH and RSA bit-length supported. */
#define SSL_MAX_DH_KEY_BITS 8192
#define SSL_MAX_RSA_KEY_BITS 8192

/* are we signing or verifying */
typedef enum {
    sig_verify = 0,
    sig_sign,
} sslSignOrVerify;

/* sign or verify context */
struct tlsSignOrVerifyContextStr {
    sslSignOrVerify type;
    union {
        SGNContext *sig;
        VFYContext *vfy;
        void *ptr;
    } u;
};

/* Types and names of elliptic curves used in TLS */
typedef enum {
    ec_type_explicitPrime = 1,      /* not supported */
    ec_type_explicitChar2Curve = 2, /* not supported */
    ec_type_named = 3
} ECType;

typedef enum {
    ticket_allow_early_data = 1,
    ticket_allow_psk_ke = 2,
    ticket_allow_psk_dhe_ke = 4,
    ticket_allow_psk_auth = 8,
    ticket_allow_psk_sign_auth = 16
} TLS13SessionTicketFlags;

typedef enum {
    update_not_requested = 0,
    update_requested = 1
} tls13KeyUpdateRequest;

struct sslNamedGroupDefStr {
    /* The name is the value that is encoded on the wire in TLS. */
    SSLNamedGroup name;
    /* The number of bits in the group. */
    unsigned int bits;
    /* The key exchange algorithm this group provides. */
    SSLKEAType keaType;
    /* The OID that identifies the group to PKCS11.  This also determines
     * whether the group is enabled in policy. */
    SECOidTag oidTag;
    /* Assume that the group is always supported. */
    PRBool assumeSupported;
};

typedef struct sslConnectInfoStr sslConnectInfo;
typedef struct sslGatherStr sslGather;
typedef struct sslSecurityInfoStr sslSecurityInfo;
typedef struct sslSessionIDStr sslSessionID;
typedef struct sslSocketOpsStr sslSocketOps;

typedef struct ssl3StateStr ssl3State;
typedef struct ssl3CertNodeStr ssl3CertNode;
typedef struct sslKeyPairStr sslKeyPair;
typedef struct ssl3DHParamsStr ssl3DHParams;

struct ssl3CertNodeStr {
    struct ssl3CertNodeStr *next;
    SECItem *derCert;
};

typedef SECStatus (*sslHandshakeFunc)(sslSocket *ss);

void ssl_CacheSessionID(sslSocket *ss);
void ssl_UncacheSessionID(sslSocket *ss);
void ssl_ServerCacheSessionID(sslSessionID *sid, PRTime creationTime);
void ssl_ServerUncacheSessionID(sslSessionID *sid);

typedef sslSessionID *(*sslSessionIDLookupFunc)(PRTime ssl_now,
                                                const PRIPv6Addr *addr,
                                                unsigned char *sid,
                                                unsigned int sidLen,
                                                CERTCertDBHandle *dbHandle);

/* Socket ops */
struct sslSocketOpsStr {
    int (*connect)(sslSocket *, const PRNetAddr *);
    PRFileDesc *(*accept)(sslSocket *, PRNetAddr *);
    int (*bind)(sslSocket *, const PRNetAddr *);
    int (*listen)(sslSocket *, int);
    int (*shutdown)(sslSocket *, int);
    int (*close)(sslSocket *);

    int (*recv)(sslSocket *, unsigned char *, int, int);

    /* points to the higher-layer send func, e.g. ssl_SecureSend. */
    int (*send)(sslSocket *, const unsigned char *, int, int);
    int (*read)(sslSocket *, unsigned char *, int);
    int (*write)(sslSocket *, const unsigned char *, int);

    int (*getpeername)(sslSocket *, PRNetAddr *);
    int (*getsockname)(sslSocket *, PRNetAddr *);
};

/* Flags interpreted by ssl send functions. */
#define ssl_SEND_FLAG_FORCE_INTO_BUFFER 0x40000000
#define ssl_SEND_FLAG_NO_BUFFER 0x20000000
#define ssl_SEND_FLAG_NO_RETRANSMIT 0x08000000 /* DTLS only */
#define ssl_SEND_FLAG_MASK 0x7f000000

/*
** SSL3 cipher suite policy and preference struct.
*/
typedef struct {
#if !defined(_WIN32)
    unsigned int cipher_suite : 16;
    unsigned int policy : 8;
    unsigned int enabled : 1;
    unsigned int isPresent : 1;
#else
    ssl3CipherSuite cipher_suite;
    PRUint8 policy;
    unsigned char enabled : 1;
    unsigned char isPresent : 1;
#endif
} ssl3CipherSuiteCfg;

#define ssl_V3_SUITES_IMPLEMENTED 71

#define MAX_DTLS_SRTP_CIPHER_SUITES 4

/* MAX_SIGNATURE_SCHEMES allows for all the values we support. */
#define MAX_SIGNATURE_SCHEMES 18

#define MAX_SUPPORTED_CERTIFICATE_COMPRESSION_ALGS 32

typedef struct sslOptionsStr {
    /* If SSL_SetNextProtoNego has been called, then this contains the
     * list of supported protocols. */
    SECItem nextProtoNego;
    PRUint16 recordSizeLimit;

    PRUint32 maxEarlyDataSize;
    unsigned int useSecurity : 1;
    unsigned int useSocks : 1;
    unsigned int requestCertificate : 1;
    unsigned int requireCertificate : 2;
    unsigned int handshakeAsClient : 1;
    unsigned int handshakeAsServer : 1;
    unsigned int noCache : 1;
    unsigned int fdx : 1;
    unsigned int detectRollBack : 1;
    unsigned int noLocks : 1;
    unsigned int enableSessionTickets : 1;
    unsigned int enableDeflate : 1; /* Deprecated. */
    unsigned int enableRenegotiation : 2;
    unsigned int requireSafeNegotiation : 1;
    unsigned int enableFalseStart : 1;
    unsigned int cbcRandomIV : 1;
    unsigned int enableOCSPStapling : 1;
    unsigned int enableALPN : 1;
    unsigned int reuseServerECDHEKey : 1;
    unsigned int enableFallbackSCSV : 1;
    unsigned int enableServerDhe : 1;
    unsigned int enableExtendedMS : 1;
    unsigned int enableSignedCertTimestamps : 1;
    unsigned int requireDHENamedGroups : 1;
    unsigned int enable0RttData : 1;
    unsigned int enableTls13CompatMode : 1;
    unsigned int enableDtlsShortHeader : 1;
    unsigned int enableHelloDowngradeCheck : 1;
    unsigned int enableV2CompatibleHello : 1;
    unsigned int enablePostHandshakeAuth : 1;
    unsigned int enableDelegatedCredentials : 1;
    unsigned int enableDtls13VersionCompat : 1;
    unsigned int suppressEndOfEarlyData : 1;
    unsigned int enableTls13GreaseEch : 1;
    unsigned int enableTls13BackendEch : 1;
    unsigned int callExtensionWriterOnEchInner : 1;
    unsigned int enableGrease : 1;
    unsigned int enableChXtnPermutation : 1;
    unsigned int dbLoadCertChain : 1;
} sslOptions;

typedef enum { sslHandshakingUndetermined = 0,
               sslHandshakingAsClient,
               sslHandshakingAsServer
} sslHandshakingType;

#define SSL_LOCK_RANK_SPEC 255

/* These are the valid values for shutdownHow.
** These values are each 1 greater than the NSPR values, and the code
** depends on that relation to efficiently convert PR_SHUTDOWN values
** into ssl_SHUTDOWN values.  These values use one bit for read, and
** another bit for write, and can be used as bitmasks.
*/
#define ssl_SHUTDOWN_NONE 0 /* NOT shutdown at all */
#define ssl_SHUTDOWN_RCV 1  /* PR_SHUTDOWN_RCV  +1 */
#define ssl_SHUTDOWN_SEND 2 /* PR_SHUTDOWN_SEND +1 */
#define ssl_SHUTDOWN_BOTH 3 /* PR_SHUTDOWN_BOTH +1 */

/*
** A gather object. Used to read some data until a count has been
** satisfied. Primarily for support of async sockets.
** Everything in here is protected by the recvBufLock.
*/
struct sslGatherStr {
    int state; /* see GS_ values below. */

    /* "buf" holds received plaintext SSL records, after decrypt and MAC check.
     * recv'd ciphertext records are put in inbuf (see below), then decrypted
     * into buf.
     */
    sslBuffer buf; /*recvBufLock*/

    /* number of bytes previously read into hdr or inbuf.
    ** (offset - writeOffset) is the number of ciphertext bytes read in but
    **     not yet deciphered.
    */
    unsigned int offset;

    /* number of bytes to read in next call to ssl_DefRecv (recv) */
    unsigned int remainder;

    /* DoRecv uses the next two values to extract application data.
    ** The difference between writeOffset and readOffset is the amount of
    ** data available to the application.   Note that the actual offset of
    ** the data in "buf" is recordOffset (above), not readOffset.
    ** In the current implementation, this is made available before the
    ** MAC is checked!!
    */
    unsigned int readOffset; /* Spot where DATA reader (e.g. application
                              ** or handshake code) will read next.
                              ** Always zero for SSl3 application data.
                              */
    /* offset in buf/inbuf/hdr into which new data will be read from socket. */
    unsigned int writeOffset;

    /* Buffer for ssl3 to read (encrypted) data from the socket */
    sslBuffer inbuf; /*recvBufLock*/

    /* The ssl[23]_GatherData functions read data into this buffer, rather
    ** than into buf or inbuf, while in the GS_HEADER state.
    ** The portion of the SSL record header put here always comes off the wire
    ** as plaintext, never ciphertext.
    ** For SSL3/TLS, the plaintext portion is 5 bytes long. For DTLS it
    ** varies based on version and header type.
    */
    unsigned char hdr[13];
    unsigned int hdrLen;

    /* Buffer for DTLS data read off the wire as a single datagram */
    sslBuffer dtlsPacket;

    /* the start of the buffered DTLS record in dtlsPacket */
    unsigned int dtlsPacketOffset;

    /* tracks whether we've seen a v3-type record before and must reject
     * any further v2-type records. */
    PRBool rejectV2Records;
};

/* sslGather.state */
#define GS_INIT 0
#define GS_HEADER 1
#define GS_DATA 2

#define WRAPPED_MASTER_SECRET_SIZE 48

typedef struct {
    PRUint8 wrapped_master_secret[WRAPPED_MASTER_SECRET_SIZE];
    PRUint8 wrapped_master_secret_len;
    PRUint8 resumable;
    PRUint8 extendedMasterSecretUsed;
} ssl3SidKeys; /* 52 bytes */

typedef enum { never_cached,
               in_client_cache,
               in_server_cache,
               invalid_cache, /* no longer in any cache. */
               in_external_cache
} Cached;

#include "sslcert.h"

struct sslSessionIDStr {
    /* The global cache lock must be held when accessing these members when the
     * sid is in any cache.
     */
    sslSessionID *next; /* chain used for client sockets, only */
    Cached cached;
    int references;
    PRTime lastAccessTime;

    /* The rest of the members, except for the members of u.ssl3.locked, may
     * be modified only when the sid is not in any cache.
     */

    CERTCertificate *peerCert;
    SECItemArray peerCertStatus;        /* client only */
    const char *peerID;                 /* client only */
    const char *urlSvrName;             /* client only */
    const sslNamedGroupDef *namedCurve; /* (server) for certificate lookup */
    CERTCertificate *localCert;

    PRIPv6Addr addr;
    PRUint16 port;

    SSL3ProtocolVersion version;

    PRTime creationTime;
    PRTime expirationTime;

    SSLAuthType authType;
    PRUint32 authKeyBits;
    SSLKEAType keaType;
    PRUint32 keaKeyBits;
    SSLNamedGroup keaGroup;
    SSLSignatureScheme sigScheme;

    union {
        struct {
            /* values that are copied into the server's on-disk SID cache. */
            PRUint8 sessionIDLength;
            PRUint8 sessionID[SSL3_SESSIONID_BYTES];

            ssl3CipherSuite cipherSuite;
            PRUint8 policy;
            ssl3SidKeys keys;
            /* mechanism used to wrap master secret */
            CK_MECHANISM_TYPE masterWrapMech;

            /* The following values pertain to the slot that wrapped the
            ** master secret. (used only in client)
            */
            SECMODModuleID masterModuleID;
            /* what module wrapped the master secret */
            CK_SLOT_ID masterSlotID;
            PRUint16 masterWrapIndex;
            /* what's the key index for the wrapping key */
            PRUint16 masterWrapSeries;
            /* keep track of the slot series, so we don't
             * accidently try to use new keys after the
             * card gets removed and replaced.*/

            /* The following values pertain to the slot that did the signature
            ** for client auth.   (used only in client)
            */
            SECMODModuleID clAuthModuleID;
            CK_SLOT_ID clAuthSlotID;
            PRUint16 clAuthSeries;

            char masterValid;
            char clAuthValid;

            SECItem srvName;

            /* Signed certificate timestamps received in a TLS extension.
            ** (used only in client).
            */
            SECItem signedCertTimestamps;

            /* The ALPN value negotiated in the original connection.
             * Used for TLS 1.3. */
            SECItem alpnSelection;

            /* This lock is lazily initialized by CacheSID when a sid is first
             * cached. Before then, there is no need to lock anything because
             * the sid isn't being shared by anything.
             */
            PRRWLock *lock;

            /* The lock must be held while reading or writing these members
             * because they change while the sid is cached.
             */
            struct {
                /* The session ticket, if we have one, is sent as an extension
                 * in the ClientHello message. This field is used only by
                 * clients. It is protected by lock when lock is non-null
                 * (after the sid has been added to the client session cache).
                 */
                NewSessionTicket sessionTicket;
            } locked;
        } ssl3;
    } u;
};

struct ssl3CipherSuiteDefStr {
    ssl3CipherSuite cipher_suite;
    SSL3BulkCipher bulk_cipher_alg;
    SSL3MACAlgorithm mac_alg;
    SSL3KeyExchangeAlgorithm key_exchange_alg;
    SSLHashType prf_hash;
};

/*
** There are tables of these, all const.
*/
typedef struct {
    /* An identifier for this struct. */
    SSL3KeyExchangeAlgorithm kea;
    /* The type of key exchange used by the cipher suite. */
    SSLKEAType exchKeyType;
    /* If the cipher suite uses a signature, the type of key used in the
     * signature. */
    KeyType signKeyType;
    /* In most cases, cipher suites depend on their signature type for
     * authentication, ECDH certificates being the exception. */
    SSLAuthType authKeyType;
    /* True if the key exchange for the suite is ephemeral.  Or to be more
     * precise: true if the ServerKeyExchange message is always required. */
    PRBool ephemeral;
    /* An OID describing the key exchange */
    SECOidTag oid;
} ssl3KEADef;

typedef enum {
    ssl_0rtt_none,     /* 0-RTT not present */
    ssl_0rtt_sent,     /* 0-RTT sent (no decision yet) */
    ssl_0rtt_accepted, /* 0-RTT sent and accepted */
    ssl_0rtt_ignored,  /* 0-RTT sent but rejected/ignored */
    ssl_0rtt_done      /* 0-RTT accepted, but finished */
} sslZeroRttState;

typedef enum {
    ssl_0rtt_ignore_none,  /* not ignoring */
    ssl_0rtt_ignore_trial, /* ignoring with trial decryption */
    ssl_0rtt_ignore_hrr    /* ignoring until ClientHello (due to HRR) */
} sslZeroRttIgnore;

typedef enum {
    idle_handshake,
    wait_client_hello,
    wait_end_of_early_data,
    wait_client_cert,
    wait_client_key,
    wait_cert_verify,
    wait_change_cipher,
    wait_finished,
    wait_server_hello,
    wait_certificate_status,
    wait_server_cert,
    wait_server_key,
    wait_cert_request,
    wait_hello_done,
    wait_new_session_ticket,
    wait_encrypted_extensions,
    wait_invalid /* Invalid value. There is no handshake message "invalid". */
} SSL3WaitState;

typedef enum {
    client_hello_initial,      /* The first attempt. */
    client_hello_retry,        /* If we receive HelloRetryRequest. */
    client_hello_retransmit,   /* In DTLS, if we receive HelloVerifyRequest. */
    client_hello_renegotiation /* A renegotiation attempt. */
} sslClientHelloType;

typedef struct SessionTicketDataStr SessionTicketData;

typedef SECStatus (*sslRestartTarget)(sslSocket *);

/*
** A DTLS queued message (potentially to be retransmitted)
*/
typedef struct DTLSQueuedMessageStr {
    PRCList link;           /* The linked list link */
    ssl3CipherSpec *cwSpec; /* The cipher spec to use, null for none */
    SSLContentType type;    /* The message type */
    unsigned char *data;    /* The data */
    PRUint32 len;           /* The data length */
} DTLSQueuedMessage;

struct TLS13KeyShareEntryStr {
    PRCList link;                  /* The linked list link */
    const sslNamedGroupDef *group; /* The group for the entry */
    SECItem key_exchange;          /* The share itself */
};

typedef struct TLS13EarlyDataStr {
    PRCList link;          /* The linked list link */
    unsigned int consumed; /* How much has been read. */
    SECItem data;          /* The data */
} TLS13EarlyData;

typedef enum {
    handshake_hash_unknown = 0,
    handshake_hash_combo = 1,  /* The MD5/SHA-1 combination */
    handshake_hash_single = 2, /* A single hash */
    handshake_hash_record
} SSL3HandshakeHashType;

// A DTLS Timer.
typedef void (*DTLSTimerCb)(sslSocket *);

typedef struct {
    const char *label;
    DTLSTimerCb cb;
    PRIntervalTime started;
    PRUint32 timeout;
} dtlsTimer;

/* TLS 1.3 client GREASE entry indices. */
typedef enum {
    grease_cipher,
    grease_extension1,
    grease_extension2,
    grease_group,
    grease_sigalg,
    grease_version,
    grease_alpn,
    grease_entries
} tls13ClientGreaseEntry;

/* TLS 1.3 client GREASE values struct. */
typedef struct tls13ClientGreaseStr {
    PRUint16 idx[grease_entries];
    PRUint8 pskKem;
} tls13ClientGrease;

/*
** This is the "hs" member of the "ssl3" struct.
** This entire struct is protected by ssl3HandshakeLock
*/
typedef struct SSL3HandshakeStateStr {
    SSL3Random server_random;
    SSL3Random client_random;
    SSL3Random client_inner_random; /* TLS 1.3 ECH Inner. */
    SSL3WaitState ws;               /* May also contain SSL3WaitState | 0x80 for TLS 1.3 */

    /* This group of members is used for handshake running hashes. */
    SSL3HandshakeHashType hashType;
    sslBuffer messages;         /* Accumulated handshake messages */
    sslBuffer echInnerMessages; /* Accumulated ECH Inner handshake messages */
    /* PKCS #11 mode:
     * SSL 3.0 - TLS 1.1 use both |md5| and |sha|. |md5| is used for MD5 and
     * |sha| for SHA-1.
     * TLS 1.2 and later use only |sha| variants, for SHA-256.
     * Under normal (non-1.3 ECH) handshakes, only |sha| and |shaPostHandshake|
     * are used. When doing 1.3 ECH, |sha| contains the transcript hash
     * corresponding to the outer Client Hello. To facilitate secure retry and
     * disablement, |shaEchInner|, tracks, in parallel, the transcript hash
     * corresponding to the inner Client Hello. Once we process the SH
     * extensions, coalesce into |sha|. */
    PK11Context *md5;
    PK11Context *sha;
    PK11Context *shaEchInner;
    PK11Context *shaPostHandshake;
    SSLSignatureScheme signatureScheme;
    const ssl3KEADef *kea_def;
    ssl3CipherSuite cipher_suite;
    const ssl3CipherSuiteDef *suite_def;
    sslBuffer msg_body; /* protected by recvBufLock */
                        /* partial handshake message from record layer */
    unsigned int header_bytes;
    /* number of bytes consumed from handshake */
    /* message for message type and header length */
    SSLHandshakeType msg_type;
    unsigned long msg_len;
    PRBool isResuming;  /* we are resuming (not used in TLS 1.3) */
    PRBool sendingSCSV; /* instead of empty RI */

    /* The session ticket received in a NewSessionTicket message is temporarily
     * stored in newSessionTicket until the handshake is finished; then it is
     * moved to the sid.
     */
    PRBool receivedNewSessionTicket;
    NewSessionTicket newSessionTicket;

    PRUint16 finishedBytes; /* size of single finished below */
    union {
        TLSFinished tFinished[2]; /* client, then server */
        SSL3Finished sFinished[2];
        PRUint8 data[72];
    } finishedMsgs;

    /* True when handshake is blocked on client certificate selection */
    PRBool clientCertificatePending;
    /* Parameters stored whilst waiting for client certificate */
    SSLSignatureScheme *clientAuthSignatureSchemes;
    unsigned int clientAuthSignatureSchemesLen;

    PRBool authCertificatePending;
    /* Which function should SSL_RestartHandshake* call if we're blocked?
     * One of NULL, ssl3_SendClientSecondRound, ssl3_FinishHandshake,
     * or ssl3_AlwaysFail */
    sslRestartTarget restartTarget;

    PRBool canFalseStart; /* Can/did we False Start */
    /* Which preliminaryinfo values have been set. */
    PRUint32 preliminaryInfo;

    /* Parsed extensions */
    PRCList remoteExtensions;   /* Parsed incoming extensions */
    PRCList echOuterExtensions; /* If ECH, hold CHOuter extensions for decompression. */

    /* This group of values is used for DTLS */
    PRUint16 sendMessageSeq;   /* The sending message sequence
                                * number */
    PRCList lastMessageFlight; /* The last message flight we
                                * sent */
    PRUint16 maxMessageSent;   /* The largest message we sent */
    PRUint16 recvMessageSeq;   /* The receiving message sequence
                                * number */
    sslBuffer recvdFragments;  /* The fragments we have received in
                                * a bitmask */
    PRInt32 recvdHighWater;    /* The high water mark for fragments
                                * received. -1 means no reassembly
                                * in progress. */
    SECItem cookie;            /* The Hello(Retry|Verify)Request cookie. */
    dtlsTimer timers[3];       /* Holder for timers. */
    dtlsTimer *rtTimer;        /* Retransmit timer. */
    dtlsTimer *ackTimer;       /* Ack timer (DTLS 1.3 only). */
    dtlsTimer *hdTimer;        /* Read cipher holddown timer. */

    /* KeyUpdate state machines */
    PRBool isKeyUpdateInProgress; /* The status of KeyUpdate -: {true == started, false == finished}. */
    PRBool allowPreviousEpoch;    /* The flag whether the previous epoch messages are allowed or not: {true == allowed, false == forbidden}. */

    PRUint32 rtRetries;  /* The retry counter */
    SECItem srvVirtName; /* for server: name that was negotiated
                          * with a client. For client - is
                          * always set to NULL.*/

    /* This group of values is used for TLS 1.3 and above */
    PK11SymKey *currentSecret;            /* The secret down the "left hand side"
                                           * of the TLS 1.3 key schedule. */
    PK11SymKey *resumptionMasterSecret;   /* The resumption_master_secret. */
    PK11SymKey *dheSecret;                /* The (EC)DHE shared secret. */
    PK11SymKey *clientEarlyTrafficSecret; /* The secret we use for 0-RTT. */
    PK11SymKey *clientHsTrafficSecret;    /* The source keys for handshake */
    PK11SymKey *serverHsTrafficSecret;    /* traffic keys. */
    PK11SymKey *clientTrafficSecret;      /* The source keys for application */
    PK11SymKey *serverTrafficSecret;      /* traffic keys */
    PK11SymKey *earlyExporterSecret;      /* for 0-RTT exporters */
    PK11SymKey *exporterSecret;           /* for exporters */
    PRCList cipherSpecs;                  /* The cipher specs in the sequence they
                                           * will be applied. */
    sslZeroRttState zeroRttState;         /* Are we doing a 0-RTT handshake? */
    sslZeroRttIgnore zeroRttIgnore;       /* Are we ignoring 0-RTT? */
    ssl3CipherSuite zeroRttSuite;         /* The cipher suite we used for 0-RTT. */
    PRCList bufferedEarlyData;            /* Buffered TLS 1.3 early data
                                           * on server.*/
    PRBool helloRetry;                    /* True if HelloRetryRequest has been sent
                                           * or received. */
    PRBool dtlsReceivedHVR;               /* True if a DTLS HelloVerifyRequest was
                                           * received. */
    PRBool receivedCcs;                   /* A server received ChangeCipherSpec
                                           * before the handshake started. */
    PRBool rejectCcs;                     /* Excessive ChangeCipherSpecs are rejected. */
    PRBool clientCertRequested;           /* True if CertificateRequest received. */
    PRBool endOfFlight;                   /* Processed a full flight (DTLS 1.3). */
    ssl3KEADef kea_def_mutable;           /* Used to hold the writable kea_def
                                           * we use for TLS 1.3 */
    PRUint16 ticketNonce;                 /* A counter we use for tickets. */
    SECItem fakeSid;                      /* ... (server) the SID the client used. */
    PRCList psks;                         /* A list of PSKs, resumption and/or external. */

    /* rttEstimate is used to guess the round trip time between server and client.
     * When the server sends ServerHello it sets this to the current time.
     * Only after it receives a message from the client's second flight does it
     * set the value to something resembling an RTT estimate. */
    PRTime rttEstimate;

    /* The following lists contain DTLSHandshakeRecordEntry */
    PRCList dtlsSentHandshake; /* Used to map records to handshake fragments. */
    PRCList dtlsRcvdHandshake; /* Handshake records we have received
                                * used to generate ACKs. */

    /* TLS 1.3 ECH state. */
    PRUint8 greaseEchSize;
    PRBool echAccepted; /* Client/Server: True if we've commited to using CHInner. */
    PRBool echDecided;
    HpkeContext *echHpkeCtx;    /* Client/Server: HPKE context for ECH. */
    const char *echPublicName;  /* Client: If rejected, the ECHConfig.publicName to
                                 * use for certificate verification. */
    sslBuffer greaseEchBuf;     /* Client: Remember GREASE ECH, as advertised, for CH2 (HRR case).
                                  Server: Remember HRR Grease Value, for transcript calculations */
    PRBool echInvalidExtension; /* Client: True if the server offered an invalid extension for the ClientHelloInner */

    /* TLS 1.3 GREASE state. */
    tls13ClientGrease *grease;

    /*
        KeyUpdate variables:
        This is true if we deferred sending a key update as
     * post-handshake auth is in progress. */
    PRBool keyUpdateDeferred;
    tls13KeyUpdateRequest deferredKeyUpdateRequest;
    /* The identifier of the keyUpdate message that is sent but not yet acknowledged */
    PRUint64 dtlsHandhakeKeyUpdateMessage;

    /* ClientHello Extension Permutation state. */
    sslExtensionBuilder *chExtensionPermutation;

    /* Used by client to store a message that's to be hashed during the HandleServerHello. */
    sslBuffer dtls13ClientMessageBuffer;
} SSL3HandshakeState;

#define SSL_ASSERT_HASHES_EMPTY(ss)                                  \
    do {                                                             \
        PORT_Assert(ss->ssl3.hs.hashType == handshake_hash_unknown); \
        PORT_Assert(ss->ssl3.hs.messages.len == 0);                  \
        PORT_Assert(ss->ssl3.hs.echInnerMessages.len == 0);          \
    } while (0)
/*
** This is the "ssl3" struct, as in "ss->ssl3".
** note:
** usually,   crSpec == cwSpec and prSpec == pwSpec.
** Sometimes, crSpec == pwSpec and prSpec == cwSpec.
** But there are never more than 2 actual specs.
** No spec must ever be modified if either "current" pointer points to it.
*/
struct ssl3StateStr {

    /*
    ** The following Specs and Spec pointers must be protected using the
    ** Spec Lock.
    */
    ssl3CipherSpec *crSpec; /* current read spec. */
    ssl3CipherSpec *prSpec; /* pending read spec. */
    ssl3CipherSpec *cwSpec; /* current write spec. */
    ssl3CipherSpec *pwSpec; /* pending write spec. */

    /* This is true after the peer requests a key update; false after a key
     * update is initiated locally. */
    PRBool peerRequestedKeyUpdate;

    /* This is true after the server requests client certificate;
     * false after the client certificate is received.  Used by the
     * server. */
    PRBool clientCertRequested;

    CERTCertificate *clientCertificate;   /* used by client */
    SECKEYPrivateKey *clientPrivateKey;   /* used by client */
    CERTCertificateList *clientCertChain; /* used by client */
    PRBool sendEmptyCert;                 /* used by client */

    PRUint8 policy;
    /* This says what cipher suites we can do, and should
     * be either SSL_ALLOWED or SSL_RESTRICTED
     */
    PLArenaPool *peerCertArena;
    /* These are used to keep track of the peer CA */
    void *peerCertChain;
    /* chain while we are trying to validate it.   */
    CERTDistNames *ca_list;
    /* used by server.  trusted CAs for this socket. */
    SSL3HandshakeState hs;

    PRUint16 mtu; /* Our estimate of the MTU */

    /* DTLS-SRTP cipher suite preferences (if any) */
    PRUint16 dtlsSRTPCiphers[MAX_DTLS_SRTP_CIPHER_SUITES];
    PRUint16 dtlsSRTPCipherCount;
    PRBool fatalAlertSent;
    PRBool dheWeakGroupEnabled; /* used by server */
    const sslNamedGroupDef *dhePreferredGroup;

    /* TLS 1.2 introduces separate signature algorithm negotiation.
     * TLS 1.3 combined signature and hash into a single enum.
     * This is our preference order. */
    SSLSignatureScheme signatureSchemes[MAX_SIGNATURE_SCHEMES];
    unsigned int signatureSchemeCount;

    /* The version to check if we fell back from our highest version
     * of TLS. Default is 0 in which case we check against the maximum
     * configured version for this socket. Used only on the client. */
    SSL3ProtocolVersion downgradeCheckVersion;
    /* supported certificate compression algorithms (if any) */
    SSLCertificateCompressionAlgorithm supportedCertCompressionAlgorithms[MAX_SUPPORTED_CERTIFICATE_COMPRESSION_ALGS];
    PRUint8 supportedCertCompressionAlgorithmsCount;
};

/* Ethernet MTU but without subtracting the headers,
 * so slightly larger than expected */
#define DTLS_MAX_MTU 1500U
#define IS_DTLS(ss) (ss->protocolVariant == ssl_variant_datagram)
#define IS_DTLS_1_OR_12(ss) (IS_DTLS(ss) && ss->version < SSL_LIBRARY_VERSION_TLS_1_3)
#define IS_DTLS_13_OR_ABOVE(ss) (IS_DTLS(ss) && ss->version >= SSL_LIBRARY_VERSION_TLS_1_3)

typedef struct {
    /* |seqNum| eventually contains the reconstructed sequence number. */
    sslSequenceNumber seqNum;
    /* The header of the cipherText. */
    PRUint8 *hdr;
    unsigned int hdrLen;

    /* |buf| is the payload of the ciphertext. */
    sslBuffer *buf;
} SSL3Ciphertext;

struct sslKeyPairStr {
    SECKEYPrivateKey *privKey;
    SECKEYPublicKey *pubKey;
    PRInt32 refCount; /* use PR_Atomic calls for this. */
};

struct sslEphemeralKeyPairStr {
    PRCList link;
    const sslNamedGroupDef *group;
    sslKeyPair *keys;
    sslKeyPair *kemKeys;
    SECItem *kemCt;
};

struct ssl3DHParamsStr {
    SSLNamedGroup name;
    SECItem prime; /* p */
    SECItem base;  /* g */
};

typedef struct SSLWrappedSymWrappingKeyStr {
    PRUint8 wrappedSymmetricWrappingkey[SSL_MAX_RSA_KEY_BITS / 8];
    CK_MECHANISM_TYPE symWrapMechanism;
    /* unwrapped symmetric wrapping key uses this mechanism */
    CK_MECHANISM_TYPE asymWrapMechanism;
    /* mechanism used to wrap the SymmetricWrappingKey using
     * server's public and/or private keys. */
    PRInt16 wrapMechIndex;
    PRUint16 wrapKeyIndex;
    PRUint16 wrappedSymKeyLen;
} SSLWrappedSymWrappingKey;

typedef struct SessionTicketStr {
    PRBool valid;
    SSL3ProtocolVersion ssl_version;
    ssl3CipherSuite cipher_suite;
    SSLAuthType authType;
    PRUint32 authKeyBits;
    SSLKEAType keaType;
    PRUint32 keaKeyBits;
    SSLNamedGroup originalKeaGroup;
    SSLSignatureScheme signatureScheme;
    const sslNamedGroupDef *namedCurve; /* For certificate lookup. */

    /*
     * msWrapMech contains a meaningful value only if ms_is_wrapped is true.
     */
    PRUint8 ms_is_wrapped;
    CK_MECHANISM_TYPE msWrapMech;
    PRUint16 ms_length;
    PRUint8 master_secret[48];
    PRBool extendedMasterSecretUsed;
    ClientAuthenticationType client_auth_type;
    SECItem peer_cert;
    PRTime timestamp;
    PRUint32 flags;
    SECItem srvName; /* negotiated server name */
    SECItem alpnSelection;
    PRUint32 maxEarlyData;
    PRUint32 ticketAgeBaseline;
    SECItem applicationToken;
} SessionTicket;

/*
 * SSL2 buffers used in SSL3.
 *     writeBuf in the SecurityInfo maintained by sslsecur.c is used
 *              to hold the data just about to be passed to the kernel
 *     sendBuf in the ConnectInfo maintained by sslcon.c is used
 *              to hold handshake messages as they are accumulated
 */

/*
** This is "ci", as in "ss->sec.ci".
**
** Protection:  All the variables in here are protected by
** firstHandshakeLock AND ssl3HandshakeLock
*/
struct sslConnectInfoStr {
    /* outgoing handshakes appended to this. */
    sslBuffer sendBuf; /*xmitBufLock*/

    PRIPv6Addr peer;
    unsigned short port;

    sslSessionID *sid;
};

/* Note: The entire content of this struct and whatever it points to gets
 * blown away by SSL_ResetHandshake().  This is "sec" as in "ss->sec".
 *
 * Unless otherwise specified below, the contents of this struct are
 * protected by firstHandshakeLock AND ssl3HandshakeLock.
 */
struct sslSecurityInfoStr {

#define SSL_ROLE(ss) (ss->sec.isServer ? "server" : "client")

    PRBool isServer;
    sslBuffer writeBuf; /*xmitBufLock*/

    CERTCertificate *localCert;
    CERTCertificate *peerCert;
    SECKEYPublicKey *peerKey;

    SSLAuthType authType;
    PRUint32 authKeyBits;
    SSLSignatureScheme signatureScheme;
    SSLKEAType keaType;
    PRUint32 keaKeyBits;
    const sslNamedGroupDef *keaGroup;
    const sslNamedGroupDef *originalKeaGroup;
    /* The selected certificate (for servers only). */
    const sslServerCert *serverCert;

    /* These are used during a connection handshake */
    sslConnectInfo ci;
};

/*
** SSL Socket struct
**
** Protection:  XXX
*/
struct sslSocketStr {
    PRFileDesc *fd;

    /* Pointer to operations vector for this socket */
    const sslSocketOps *ops;

    /* SSL socket options */
    sslOptions opt;
    /* Enabled version range */
    SSLVersionRange vrange;

    /* A function that returns the current time. */
    SSLTimeFunc now;
    void *nowArg;

    /* State flags */
    unsigned long clientAuthRequested;
    unsigned long delayDisabled;     /* Nagle delay disabled */
    unsigned long firstHsDone;       /* first handshake is complete. */
    unsigned long enoughFirstHsDone; /* enough of the first handshake is
                                      * done for callbacks to be able to
                                      * retrieve channel security
                                      * parameters from the SSL socket. */
    unsigned long handshakeBegun;
    unsigned long lastWriteBlocked;
    unsigned long recvdCloseNotify; /* received SSL EOF. */
    unsigned long TCPconnected;
    unsigned long appDataBuffered;
    unsigned long peerRequestedProtection; /* from old renegotiation */

    /* version of the protocol to use */
    SSL3ProtocolVersion version;
    SSL3ProtocolVersion clientHelloVersion; /* version sent in client hello. */

    sslSecurityInfo sec; /* not a pointer any more */

    /* protected by firstHandshakeLock AND ssl3HandshakeLock. */
    const char *url;

    sslHandshakeFunc handshake; /*firstHandshakeLock*/

    /* the following variable is only used with socks or other proxies. */
    char *peerID; /* String uniquely identifies target server. */

    /* ECDHE and DHE keys: In TLS 1.3, we might have to maintain multiple of
     * these on the client side.  The server inserts a single value into this
     * list for all versions. */
    PRCList /*<sslEphemeralKeyPair>*/ ephemeralKeyPairs;

    /* Callbacks */
    SSLAuthCertificate authCertificate;
    void *authCertificateArg;
    SSLGetClientAuthData getClientAuthData;
    void *getClientAuthDataArg;
    SSLSNISocketConfig sniSocketConfig;
    void *sniSocketConfigArg;
    SSLAlertCallback alertReceivedCallback;
    void *alertReceivedCallbackArg;
    SSLAlertCallback alertSentCallback;
    void *alertSentCallbackArg;
    SSLBadCertHandler handleBadCert;
    void *badCertArg;
    SSLHandshakeCallback handshakeCallback;
    void *handshakeCallbackData;
    SSLCanFalseStartCallback canFalseStartCallback;
    void *canFalseStartCallbackData;
    void *pkcs11PinArg;
    SSLNextProtoCallback nextProtoCallback;
    void *nextProtoArg;
    SSLHelloRetryRequestCallback hrrCallback;
    void *hrrCallbackArg;
    PRCList extensionHooks;
    SSLResumptionTokenCallback resumptionTokenCallback;
    void *resumptionTokenContext;
    SSLSecretCallback secretCallback;
    void *secretCallbackArg;
    SSLRecordWriteCallback recordWriteCallback;
    void *recordWriteCallbackArg;

    PRIntervalTime rTimeout; /* timeout for NSPR I/O */
    PRIntervalTime wTimeout; /* timeout for NSPR I/O */
    PRIntervalTime cTimeout; /* timeout for NSPR I/O */

    PRLock *recvLock; /* lock against multiple reader threads. */
    PRLock *sendLock; /* lock against multiple sender threads. */

    PRMonitor *recvBufLock; /* locks low level recv buffers. */
    PRMonitor *xmitBufLock; /* locks low level xmit buffers. */

    /* Only one thread may operate on the socket until the initial handshake
    ** is complete.  This Monitor ensures that.  Since SSL2 handshake is
    ** only done once, this is also effectively the SSL2 handshake lock.
    */
    PRMonitor *firstHandshakeLock;

    /* This monitor protects the ssl3 handshake state machine data.
    ** Only one thread (reader or writer) may be in the ssl3 handshake state
    ** machine at any time.  */
    PRMonitor *ssl3HandshakeLock;

    /* reader/writer lock, protects the secret data needed to encrypt and MAC
    ** outgoing records, and to decrypt and MAC check incoming ciphertext
    ** records.  */
    NSSRWLock *specLock;

    /* handle to perm cert db (and implicitly to the temp cert db) used
    ** with this socket.
    */
    CERTCertDBHandle *dbHandle;

    PRThread *writerThread; /* thread holds SSL_LOCK_WRITER lock */

    PRUint16 shutdownHow; /* See ssl_SHUTDOWN defines below. */

    sslHandshakingType handshaking;

    /* Gather object used for gathering data */
    sslGather gs; /*recvBufLock*/

    sslBuffer saveBuf;    /*xmitBufLock*/
    sslBuffer pendingBuf; /*xmitBufLock*/

    /* Configuration state for server sockets */
    /* One server cert and key for each authentication type. */
    PRCList /* <sslServerCert> */ serverCerts;

    ssl3CipherSuiteCfg cipherSuites[ssl_V3_SUITES_IMPLEMENTED];

    /* A list of groups that are sorted according to user preferences pointing
     * to entries of ssl_named_groups. By default this list contains pointers
     * to all elements in ssl_named_groups in the default order.
     * This list also determines which groups are enabled. This
     * starts with all being enabled and can be modified either by negotiation
     * (in which case groups not supported by a peer are masked off), or by
     * calling SSL_DHEGroupPrefSet().
     * Note that renegotiation will ignore groups that were disabled in the
     * first handshake.
     */
    const sslNamedGroupDef *namedGroupPreferences[SSL_NAMED_GROUP_COUNT];
    /* The number of additional shares to generate for the TLS 1.3 ClientHello */
    unsigned int additionalShares;

    /* SSL3 state info.  Formerly was a pointer */
    ssl3State ssl3;

    /*
     * TLS extension related data.
     */
    /* True when the current session is a stateless resume. */
    PRBool statelessResume;
    TLSExtensionData xtnData;

    /* Whether we are doing stream or datagram mode */
    SSLProtocolVariant protocolVariant;

    /* TLS 1.3 Encrypted Client Hello. */
    PRCList echConfigs;           /* Client/server: Must not change while hs
                                   * is in-progress. */
    SECKEYPublicKey *echPubKey;   /* Server: The ECH keypair used in HPKE. */
    SECKEYPrivateKey *echPrivKey; /* As above. */

    /* Anti-replay for TLS 1.3 0-RTT. */
    SSLAntiReplayContext *antiReplay;

    /* An out-of-band PSK. */
    sslPsk *psk;
};

struct sslSelfEncryptKeysStr {
    PRCallOnceType setup;
    PRUint8 keyName[SELF_ENCRYPT_KEY_NAME_LEN];
    PK11SymKey *encKey;
    PK11SymKey *macKey;
};
typedef struct sslSelfEncryptKeysStr sslSelfEncryptKeys;

extern char ssl_debug;
extern char ssl_trace;
extern FILE *ssl_trace_iob;
extern FILE *ssl_keylog_iob;
extern PRLock *ssl_keylog_lock;
static const PRUint32 ssl_ticket_lifetime = 2 * 24 * 60 * 60; // 2 days.

extern const char *const ssl3_cipherName[];

extern sslSessionIDLookupFunc ssl_sid_lookup;

extern const sslNamedGroupDef ssl_named_groups[];

/************************************************************************/

SEC_BEGIN_PROTOS

/* Internal initialization and installation of the SSL error tables */
extern SECStatus ssl_Init(void);
extern SECStatus ssl_InitializePRErrorTable(void);

/* Implementation of ops for default (non socks, non secure) case */
extern int ssl_DefConnect(sslSocket *ss, const PRNetAddr *addr);
extern PRFileDesc *ssl_DefAccept(sslSocket *ss, PRNetAddr *addr);
extern int ssl_DefBind(sslSocket *ss, const PRNetAddr *addr);
extern int ssl_DefListen(sslSocket *ss, int backlog);
extern int ssl_DefShutdown(sslSocket *ss, int how);
extern int ssl_DefClose(sslSocket *ss);
extern int ssl_DefRecv(sslSocket *ss, unsigned char *buf, int len, int flags);
extern int ssl_DefSend(sslSocket *ss, const unsigned char *buf,
                       int len, int flags);
extern int ssl_DefRead(sslSocket *ss, unsigned char *buf, int len);
extern int ssl_DefWrite(sslSocket *ss, const unsigned char *buf, int len);
extern int ssl_DefGetpeername(sslSocket *ss, PRNetAddr *name);
extern int ssl_DefGetsockname(sslSocket *ss, PRNetAddr *name);
extern int ssl_DefGetsockopt(sslSocket *ss, PRSockOption optname,
                             void *optval, PRInt32 *optlen);
extern int ssl_DefSetsockopt(sslSocket *ss, PRSockOption optname,
                             const void *optval, PRInt32 optlen);

/* Implementation of ops for socks only case */
extern int ssl_SocksConnect(sslSocket *ss, const PRNetAddr *addr);
extern PRFileDesc *ssl_SocksAccept(sslSocket *ss, PRNetAddr *addr);
extern int ssl_SocksBind(sslSocket *ss, const PRNetAddr *addr);
extern int ssl_SocksListen(sslSocket *ss, int backlog);
extern int ssl_SocksGetsockname(sslSocket *ss, PRNetAddr *name);
extern int ssl_SocksRecv(sslSocket *ss, unsigned char *buf, int len, int flags);
extern int ssl_SocksSend(sslSocket *ss, const unsigned char *buf,
                         int len, int flags);
extern int ssl_SocksRead(sslSocket *ss, unsigned char *buf, int len);
extern int ssl_SocksWrite(sslSocket *ss, const unsigned char *buf, int len);

/* Implementation of ops for secure only case */
extern int ssl_SecureConnect(sslSocket *ss, const PRNetAddr *addr);
extern PRFileDesc *ssl_SecureAccept(sslSocket *ss, PRNetAddr *addr);
extern int ssl_SecureRecv(sslSocket *ss, unsigned char *buf,
                          int len, int flags);
extern int ssl_SecureSend(sslSocket *ss, const unsigned char *buf,
                          int len, int flags);
extern int ssl_SecureRead(sslSocket *ss, unsigned char *buf, int len);
extern int ssl_SecureWrite(sslSocket *ss, const unsigned char *buf, int len);
extern int ssl_SecureShutdown(sslSocket *ss, int how);
extern int ssl_SecureClose(sslSocket *ss);

/* Implementation of ops for secure socks case */
extern int ssl_SecureSocksConnect(sslSocket *ss, const PRNetAddr *addr);
extern PRFileDesc *ssl_SecureSocksAccept(sslSocket *ss, PRNetAddr *addr);
extern PRFileDesc *ssl_FindTop(sslSocket *ss);

/* Gather funcs. */
extern sslGather *ssl_NewGather(void);
extern SECStatus ssl3_InitGather(sslGather *gs);
extern void ssl3_DestroyGather(sslGather *gs);
extern SECStatus ssl_GatherRecord1stHandshake(sslSocket *ss);

extern SECStatus ssl_CreateSecurityInfo(sslSocket *ss);
extern SECStatus ssl_CopySecurityInfo(sslSocket *ss, sslSocket *os);
extern void ssl_ResetSecurityInfo(sslSecurityInfo *sec, PRBool doMemset);
extern void ssl_DestroySecurityInfo(sslSecurityInfo *sec);

extern void ssl_PrintBuf(const sslSocket *ss, const char *msg, const void *cp,
                         int len);
extern void ssl_PrintKey(const sslSocket *ss, const char *msg, PK11SymKey *key);

extern int ssl_SendSavedWriteData(sslSocket *ss);
extern SECStatus ssl_SaveWriteData(sslSocket *ss,
                                   const void *p, unsigned int l);
extern SECStatus ssl_BeginClientHandshake(sslSocket *ss);
extern SECStatus ssl_BeginServerHandshake(sslSocket *ss);
extern SECStatus ssl_Do1stHandshake(sslSocket *ss);

extern SECStatus ssl3_InitPendingCipherSpecs(sslSocket *ss, PK11SymKey *secret,
                                             PRBool derive);
extern void ssl_DestroyKeyMaterial(ssl3KeyMaterial *keyMaterial);
extern sslSessionID *ssl3_NewSessionID(sslSocket *ss, PRBool is_server);
extern sslSessionID *ssl_LookupSID(PRTime now, const PRIPv6Addr *addr,
                                   PRUint16 port, const char *peerID,
                                   const char *urlSvrName);
extern void ssl_FreeSID(sslSessionID *sid);
extern void ssl_DestroySID(sslSessionID *sid, PRBool freeIt);
extern sslSessionID *ssl_ReferenceSID(sslSessionID *sid);

extern int ssl3_SendApplicationData(sslSocket *ss, const PRUint8 *in,
                                    int len, int flags);

extern PRBool ssl_FdIsBlocking(PRFileDesc *fd);

extern PRBool ssl_SocketIsBlocking(sslSocket *ss);

extern void ssl3_SetAlwaysBlock(sslSocket *ss);

extern SECStatus ssl_EnableNagleDelay(sslSocket *ss, PRBool enabled);

extern SECStatus ssl_FinishHandshake(sslSocket *ss);

extern SECStatus ssl_CipherPolicySet(PRInt32 which, PRInt32 policy);

extern SECStatus ssl_CipherPrefSetDefault(PRInt32 which, PRBool enabled);

extern SECStatus ssl3_ConstrainRangeByPolicy(void);

extern SECStatus ssl3_InitState(sslSocket *ss);
extern SECStatus Null_Cipher(void *ctx, unsigned char *output, unsigned int *outputLen,
                             unsigned int maxOutputLen, const unsigned char *input,
                             unsigned int inputLen);
extern void ssl3_RestartHandshakeHashes(sslSocket *ss);
typedef SECStatus (*sslUpdateHandshakeHashes)(sslSocket *ss,
                                              const unsigned char *b,
                                              unsigned int l);
extern SECStatus ssl3_UpdateHandshakeHashes(sslSocket *ss,
                                            const unsigned char *b,
                                            unsigned int l);
extern SECStatus ssl3_UpdatePostHandshakeHashes(sslSocket *ss,
                                                const unsigned char *b,
                                                unsigned int l);
SECStatus
ssl_HashHandshakeMessageInt(sslSocket *ss, SSLHandshakeType type,
                            PRUint32 dtlsSeq,
                            const PRUint8 *b, PRUint32 length,
                            sslUpdateHandshakeHashes cb);
SECStatus ssl_HashHandshakeMessage(sslSocket *ss, SSLHandshakeType type,
                                   const PRUint8 *b, PRUint32 length);
SECStatus ssl_HashHandshakeMessageEchInner(sslSocket *ss, SSLHandshakeType type,
                                           const PRUint8 *b, PRUint32 length);
SECStatus ssl_HashHandshakeMessageDefault(sslSocket *ss, SSLHandshakeType type,
                                          const PRUint8 *b, PRUint32 length);
SECStatus ssl_HashPostHandshakeMessage(sslSocket *ss, SSLHandshakeType type,
                                       const PRUint8 *b, PRUint32 length);

/* Returns PR_TRUE if we are still waiting for the server to complete its
 * response to our client second round. Once we've received the Finished from
 * the server then there is no need to check false start.
 */
extern PRBool ssl3_WaitingForServerSecondRound(sslSocket *ss);

extern PRInt32 ssl3_SendRecord(sslSocket *ss, ssl3CipherSpec *cwSpec,
                               SSLContentType type,
                               const PRUint8 *pIn, PRInt32 nIn,
                               PRInt32 flags);

/* Clear any PRCList, optionally calling f on the value. */
void ssl_ClearPRCList(PRCList *list, void (*f)(void *));

/*
 * Make sure there is room in the write buffer for padding and
 * cryptographic expansions.
 */
#define SSL3_BUFFER_FUDGE 100

#define SSL_LOCK_READER(ss) \
    if (ss->recvLock)       \
    PR_Lock(ss->recvLock)
#define SSL_UNLOCK_READER(ss) \
    if (ss->recvLock)         \
    PR_Unlock(ss->recvLock)
#define SSL_LOCK_WRITER(ss) \
    if (ss->sendLock)       \
    PR_Lock(ss->sendLock)
#define SSL_UNLOCK_WRITER(ss) \
    if (ss->sendLock)         \
    PR_Unlock(ss->sendLock)

PRBool ssl_HaveRecvBufLock(sslSocket *ss);
PRBool ssl_HaveXmitBufLock(sslSocket *ss);
PRBool ssl_Have1stHandshakeLock(sslSocket *ss);
PRBool ssl_HaveSSL3HandshakeLock(sslSocket *ss);
PRBool ssl_HaveSpecWriteLock(sslSocket *ss);

void ssl_Get1stHandshakeLock(sslSocket *ss);
void ssl_Release1stHandshakeLock(sslSocket *ss);

void ssl_GetSSL3HandshakeLock(sslSocket *ss);
void ssl_ReleaseSSL3HandshakeLock(sslSocket *ss);

void ssl_GetSpecReadLock(sslSocket *ss);
void ssl_ReleaseSpecReadLock(sslSocket *ss);

void ssl_GetSpecWriteLock(sslSocket *ss);
void ssl_ReleaseSpecWriteLock(sslSocket *ss);

void ssl_GetRecvBufLock(sslSocket *ss);
void ssl_ReleaseRecvBufLock(sslSocket *ss);

void ssl_GetXmitBufLock(sslSocket *ss);
void ssl_ReleaseXmitBufLock(sslSocket *ss);

/* Placeholder value used in version ranges when SSL 3.0 and all
 * versions of TLS are disabled.
 */
#define SSL_LIBRARY_VERSION_NONE 0

/* SSL_LIBRARY_VERSION_MIN_SUPPORTED is the minimum version that this version
 * of libssl supports. Applications should use SSL_VersionRangeGetSupported at
 * runtime to determine which versions are supported by the version of libssl
 * in use.
 */
#define SSL_LIBRARY_VERSION_MIN_SUPPORTED_DATAGRAM SSL_LIBRARY_VERSION_TLS_1_1
#define SSL_LIBRARY_VERSION_MIN_SUPPORTED_STREAM SSL_LIBRARY_VERSION_3_0

/* SSL_LIBRARY_VERSION_MAX_SUPPORTED is the maximum version that this version
 * of libssl supports. Applications should use SSL_VersionRangeGetSupported at
 * runtime to determine which versions are supported by the version of libssl
 * in use.
 */
#ifndef NSS_DISABLE_TLS_1_3
#define SSL_LIBRARY_VERSION_MAX_SUPPORTED SSL_LIBRARY_VERSION_TLS_1_3
#else
#define SSL_LIBRARY_VERSION_MAX_SUPPORTED SSL_LIBRARY_VERSION_TLS_1_2
#endif

#define SSL_ALL_VERSIONS_DISABLED(vrange) \
    ((vrange)->min == SSL_LIBRARY_VERSION_NONE)

extern PRBool ssl3_VersionIsSupported(SSLProtocolVariant protocolVariant,
                                      SSL3ProtocolVersion version);

/* These functions are called from secnav, even though they're "private". */

extern int SSL_RestartHandshakeAfterCertReq(struct sslSocketStr *ss,
                                            CERTCertificate *cert,
                                            SECKEYPrivateKey *key,
                                            CERTCertificateList *certChain);
extern sslSocket *ssl_FindSocket(PRFileDesc *fd);
extern void ssl_FreeSocket(struct sslSocketStr *ssl);
extern SECStatus SSL3_SendAlert(sslSocket *ss, SSL3AlertLevel level,
                                SSL3AlertDescription desc);
extern SECStatus ssl3_DecodeError(sslSocket *ss);

extern SECStatus ssl3_AuthCertificateComplete(sslSocket *ss, PRErrorCode error);
extern SECStatus ssl3_ClientCertCallbackComplete(sslSocket *ss, SECStatus outcome, SECKEYPrivateKey *clientPrivateKey, CERTCertificate *clientCertificate);

/*
 * for dealing with SSL 3.0 clients sending SSL 2.0 format hellos
 */
extern SECStatus ssl3_HandleV2ClientHello(
    sslSocket *ss, unsigned char *buffer, unsigned int length, PRUint8 padding);

SECStatus
ssl3_CreateClientHelloPreamble(sslSocket *ss, const sslSessionID *sid,
                               PRBool realSid, PRUint16 version, PRBool isEchInner,
                               const sslBuffer *extensions, sslBuffer *preamble);
SECStatus ssl3_InsertChHeaderSize(const sslSocket *ss, sslBuffer *preamble, const sslBuffer *extensions);
SECStatus ssl3_SendClientHello(sslSocket *ss, sslClientHelloType type);

/*
 * input into the SSL3 machinery from the actualy network reading code
 */
SECStatus ssl3_HandleRecord(sslSocket *ss, SSL3Ciphertext *cipher);
SECStatus ssl3_HandleNonApplicationData(sslSocket *ss, SSLContentType rType,
                                        DTLSEpoch epoch,
                                        sslSequenceNumber seqNum,
                                        sslBuffer *databuf);
SECStatus ssl_RemoveTLSCBCPadding(sslBuffer *plaintext, unsigned int macSize);

int ssl3_GatherAppDataRecord(sslSocket *ss, int flags);
int ssl3_GatherCompleteHandshake(sslSocket *ss, int flags);

/* Create a new ref counted key pair object from two keys. */
extern sslKeyPair *ssl_NewKeyPair(SECKEYPrivateKey *privKey,
                                  SECKEYPublicKey *pubKey);

/* get a new reference (bump ref count) to an ssl3KeyPair. */
extern sslKeyPair *ssl_GetKeyPairRef(sslKeyPair *keyPair);

/* Decrement keypair's ref count and free if zero. */
extern void ssl_FreeKeyPair(sslKeyPair *keyPair);

extern sslEphemeralKeyPair *ssl_NewEphemeralKeyPair(
    const sslNamedGroupDef *group,
    SECKEYPrivateKey *privKey, SECKEYPublicKey *pubKey);
extern sslEphemeralKeyPair *ssl_CopyEphemeralKeyPair(
    sslEphemeralKeyPair *keyPair);
extern void ssl_FreeEphemeralKeyPair(sslEphemeralKeyPair *keyPair);
extern sslEphemeralKeyPair *ssl_LookupEphemeralKeyPair(
    sslSocket *ss, const sslNamedGroupDef *groupDef);
extern PRBool ssl_HaveEphemeralKeyPair(const sslSocket *ss,
                                       const sslNamedGroupDef *groupDef);
extern void ssl_FreeEphemeralKeyPairs(sslSocket *ss);

extern SECStatus ssl_AppendPaddedDHKeyShare(sslBuffer *buf,
                                            const SECKEYPublicKey *pubKey,
                                            PRBool appendLength);
extern PRBool ssl_CanUseSignatureScheme(SSLSignatureScheme scheme,
                                        const SSLSignatureScheme *peerSchemes,
                                        unsigned int peerSchemeCount,
                                        PRBool requireSha1,
                                        PRBool slotDoesPss);
extern const ssl3DHParams *ssl_GetDHEParams(const sslNamedGroupDef *groupDef);
extern SECStatus ssl_SelectDHEGroup(sslSocket *ss,
                                    const sslNamedGroupDef **groupDef);
extern SECStatus ssl_CreateDHEKeyPair(const sslNamedGroupDef *groupDef,
                                      const ssl3DHParams *params,
                                      sslEphemeralKeyPair **keyPair);
extern PRBool ssl_IsValidDHEShare(const SECItem *dh_p, const SECItem *dh_Ys);
extern SECStatus ssl_ValidateDHENamedGroup(sslSocket *ss,
                                           const SECItem *dh_p,
                                           const SECItem *dh_g,
                                           const sslNamedGroupDef **groupDef,
                                           const ssl3DHParams **dhParams);

extern PRBool ssl_IsECCEnabled(const sslSocket *ss);
extern PRBool ssl_IsDHEEnabled(const sslSocket *ss);

/* Macro for finding a curve equivalent in strength to RSA key's */
#define SSL_RSASTRENGTH_TO_ECSTRENGTH(s)                            \
    ((s <= 1024) ? 160                                              \
                 : ((s <= 2048) ? 224                               \
                                : ((s <= 3072) ? 256                \
                                               : ((s <= 7168) ? 384 \
                                                              : 521))))

extern const sslNamedGroupDef *ssl_LookupNamedGroup(SSLNamedGroup group);
extern PRBool ssl_NamedGroupEnabled(const sslSocket *ss, const sslNamedGroupDef *group);
extern SECStatus ssl_NamedGroup2ECParams(PLArenaPool *arena,
                                         const sslNamedGroupDef *curve,
                                         SECKEYECParams *params);
extern const sslNamedGroupDef *ssl_ECPubKey2NamedGroup(
    const SECKEYPublicKey *pubKey);

extern const sslNamedGroupDef *ssl_GetECGroupForServerSocket(sslSocket *ss);
extern void ssl_FilterSupportedGroups(sslSocket *ss);

extern SECStatus ssl3_CipherPrefSetDefault(ssl3CipherSuite which, PRBool on);
extern SECStatus ssl3_CipherPrefGetDefault(ssl3CipherSuite which, PRBool *on);

extern SECStatus ssl3_CipherPrefSet(sslSocket *ss, ssl3CipherSuite which, PRBool on);
extern SECStatus ssl3_CipherPrefGet(const sslSocket *ss, ssl3CipherSuite which, PRBool *on);

extern SECStatus ssl3_SetPolicy(ssl3CipherSuite which, PRInt32 policy);
extern SECStatus ssl3_GetPolicy(ssl3CipherSuite which, PRInt32 *policy);

extern void ssl3_InitSocketPolicy(sslSocket *ss);

extern SECStatus ssl3_RedoHandshake(sslSocket *ss, PRBool flushCache);
extern SECStatus ssl3_HandleHandshakeMessage(sslSocket *ss, PRUint8 *b,
                                             PRUint32 length,
                                             PRBool endOfRecord);

extern void ssl3_DestroySSL3Info(sslSocket *ss);

extern SECStatus ssl_ClientReadVersion(sslSocket *ss, PRUint8 **b,
                                       PRUint32 *length,
                                       SSL3ProtocolVersion *version);
extern SECStatus ssl3_NegotiateVersion(sslSocket *ss,
                                       SSL3ProtocolVersion peerVersion,
                                       PRBool allowLargerPeerVersion);
extern SECStatus ssl_ClientSetCipherSuite(sslSocket *ss,
                                          SSL3ProtocolVersion version,
                                          ssl3CipherSuite suite,
                                          PRBool initHashes);

extern SECStatus ssl_GetPeerInfo(sslSocket *ss);

/* ECDH functions */
extern SECStatus ssl3_SendECDHClientKeyExchange(sslSocket *ss,
                                                SECKEYPublicKey *svrPubKey);
extern SECStatus ssl3_HandleECDHServerKeyExchange(sslSocket *ss,
                                                  PRUint8 *b, PRUint32 length);
extern SECStatus ssl3_HandleECDHClientKeyExchange(sslSocket *ss,
                                                  PRUint8 *b, PRUint32 length,
                                                  sslKeyPair *serverKeys);
extern SECStatus ssl3_SendECDHServerKeyExchange(sslSocket *ss);
extern SECStatus ssl_ImportECDHKeyShare(
    SECKEYPublicKey *peerKey,
    PRUint8 *b, PRUint32 length, const sslNamedGroupDef *curve);

extern SECStatus ssl3_ComputeCommonKeyHash(SSLHashType hashAlg,
                                           PRUint8 *hashBuf,
                                           unsigned int bufLen,
                                           SSL3Hashes *hashes);
extern SECStatus ssl3_AppendSignatureAndHashAlgorithm(
    sslSocket *ss, const SSLSignatureAndHashAlg *sigAndHash);
extern SECStatus ssl3_ConsumeHandshake(sslSocket *ss, void *v, PRUint32 bytes,
                                       PRUint8 **b, PRUint32 *length);
extern SECStatus ssl3_ConsumeHandshakeNumber(sslSocket *ss, PRUint32 *num,
                                             PRUint32 bytes, PRUint8 **b,
                                             PRUint32 *length);
extern SECStatus ssl3_ConsumeHandshakeNumber64(sslSocket *ss, PRUint64 *num,
                                               PRUint32 bytes, PRUint8 **b,
                                               PRUint32 *length);
extern SECStatus ssl3_ConsumeHandshakeVariable(sslSocket *ss, SECItem *i,
                                               PRUint32 bytes, PRUint8 **b,
                                               PRUint32 *length);
extern SECStatus ssl_SignatureSchemeFromSpki(const CERTSubjectPublicKeyInfo *spki,
                                             PRBool isTls13,
                                             SSLSignatureScheme *scheme);
extern PRBool ssl_SignatureSchemeEnabled(const sslSocket *ss,
                                         SSLSignatureScheme scheme);
extern PRBool ssl_IsSupportedSignatureScheme(SSLSignatureScheme scheme);
extern SECStatus ssl_CheckSignatureSchemeConsistency(
    sslSocket *ss, SSLSignatureScheme scheme, CERTSubjectPublicKeyInfo *spki);
extern SECStatus ssl_ParseSignatureSchemes(const sslSocket *ss, PLArenaPool *arena,
                                           SSLSignatureScheme **schemesOut,
                                           unsigned int *numSchemesOut,
                                           unsigned char **b,
                                           unsigned int *len);
extern SECStatus ssl_ConsumeSignatureScheme(
    sslSocket *ss, PRUint8 **b, PRUint32 *length, SSLSignatureScheme *out);
extern SECStatus ssl3_SignHashes(sslSocket *ss, SSL3Hashes *hash,
                                 SECKEYPrivateKey *key, SECItem *buf);
extern SECStatus ssl3_VerifySignedHashes(sslSocket *ss, SSLSignatureScheme scheme,
                                         SSL3Hashes *hash, SECItem *buf);
/* new signature algorithms don't really have a 'sign hashes' interface,
 * TLS13 now supports proper signing, where, if we are signing hashes, we
 * will sign them with a proper hash-and-sign signature. Provide
 * an API for those places in TLS13 where we need to sign. This leverages
 * the work in secsign and secvfy, so we don't need to add a lot of
 * algorithm specific code. Once the sign/verify interfaces work, we can
 * just add the oid in tls13con.c and the ssl_sig_xxxx value and we are
 * good to go */
extern tlsSignOrVerifyContext tls_CreateSignOrVerifyContext(
    SECKEYPrivateKey *privKey,
    SECKEYPublicKey *pubKey,
    SSLSignatureScheme scheme, sslSignOrVerify type,
    SECItem *signature, void *pwArg);
SECStatus tls_SignOrVerifyUpdate(tlsSignOrVerifyContext ctx,
                                 const unsigned char *buf, int len);
SECStatus tls_SignOrVerifyEnd(tlsSignOrVerifyContext ctx, SECItem *sig);
void tls_DestroySignOrVerifyContext(tlsSignOrVerifyContext *ctx);

extern SECStatus ssl3_CacheWrappedSecret(sslSocket *ss, sslSessionID *sid,
                                         PK11SymKey *secret);
extern void ssl3_FreeSniNameArray(TLSExtensionData *xtnData);

/* Hello Extension related routines. */
extern void ssl3_SetSIDSessionTicket(sslSessionID *sid,
                                     /*in/out*/ NewSessionTicket *session_ticket);
SECStatus ssl3_EncodeSessionTicket(sslSocket *ss,
                                   const NewSessionTicket *ticket,
                                   const PRUint8 *appToken,
                                   unsigned int appTokenLen,
                                   PK11SymKey *secret, SECItem *ticket_data);
SECStatus SSLExp_SendSessionTicket(PRFileDesc *fd, const PRUint8 *token,
                                   unsigned int tokenLen);

SECStatus ssl_MaybeSetSelfEncryptKeyPair(const sslKeyPair *keyPair);
SECStatus ssl_GetSelfEncryptKeys(sslSocket *ss, unsigned char *keyName,
                                 PK11SymKey **encKey, PK11SymKey **macKey);
void ssl_ResetSelfEncryptKeys();

extern SECStatus ssl3_ValidateAppProtocol(const unsigned char *data,
                                          unsigned int length);

/* Construct a new NSPR socket for the app to use */
extern PRFileDesc *ssl_NewPRSocket(sslSocket *ss, PRFileDesc *fd);
extern void ssl_FreePRSocket(PRFileDesc *fd);

/* Internal config function so SSL3 can initialize the present state of
 * various ciphers */
extern unsigned int ssl3_config_match_init(sslSocket *);

/* Return PR_TRUE if suite is usable.  This if the suite is permitted by policy,
 * enabled, has a certificate (as needed), has a viable key agreement method, is
 * usable with the negotiated TLS version, and is otherwise usable. */
PRBool ssl3_config_match(const ssl3CipherSuiteCfg *suite, PRUint8 policy,
                         const SSLVersionRange *vrange, const sslSocket *ss);

/* calls for accessing wrapping keys across processes. */
extern SECStatus
ssl_GetWrappingKey(unsigned int symWrapMechIndex,
                   unsigned int wrapKeyIndex, SSLWrappedSymWrappingKey *wswk);

/* The caller passes in the new value it wants
 * to set.  This code tests the wrapped sym key entry in the file on disk.
 * If it is uninitialized, this function writes the caller's value into
 * the disk entry, and returns false.
 * Otherwise, it overwrites the caller's wswk with the value obtained from
 * the disk, and returns PR_TRUE.
 * This is all done while holding the locks/semaphores necessary to make
 * the operation atomic.
 */
extern SECStatus
ssl_SetWrappingKey(SSLWrappedSymWrappingKey *wswk);

/* get rid of the symmetric wrapping key references. */
extern SECStatus SSL3_ShutdownServerCache(void);

extern SECStatus ssl_InitSymWrapKeysLock(void);

extern SECStatus ssl_FreeSymWrapKeysLock(void);

extern SECStatus ssl_InitSessionCacheLocks(PRBool lazyInit);

extern SECStatus ssl_FreeSessionCacheLocks(void);

CK_MECHANISM_TYPE ssl3_Alg2Mech(SSLCipherAlgorithm calg);
SECStatus ssl3_NegotiateCipherSuiteInner(sslSocket *ss, const SECItem *suites,
                                         PRUint16 version, PRUint16 *suitep);
SECStatus ssl3_NegotiateCipherSuite(sslSocket *ss, const SECItem *suites,
                                    PRBool initHashes);
SECStatus ssl3_InitHandshakeHashes(sslSocket *ss);
void ssl3_CoalesceEchHandshakeHashes(sslSocket *ss);
SECStatus ssl3_ServerCallSNICallback(sslSocket *ss);
SECStatus ssl3_FlushHandshake(sslSocket *ss, PRInt32 flags);
SECStatus ssl3_CompleteHandleCertificate(sslSocket *ss,
                                         PRUint8 *b, PRUint32 length);
void ssl3_SendAlertForCertError(sslSocket *ss, PRErrorCode errCode);
SECStatus ssl3_HandleNoCertificate(sslSocket *ss);
SECStatus ssl3_SendEmptyCertificate(sslSocket *ss);
void ssl3_CleanupPeerCerts(sslSocket *ss);
SECStatus ssl3_SendCertificateStatus(sslSocket *ss);
SECStatus ssl_SetAuthKeyBits(sslSocket *ss, const SECKEYPublicKey *pubKey);
SECStatus ssl3_HandleServerSpki(sslSocket *ss);
SECStatus ssl3_AuthCertificate(sslSocket *ss);
SECStatus ssl_ReadCertificateStatus(sslSocket *ss, PRUint8 *b,
                                    PRUint32 length);
SECStatus ssl3_EncodeSigAlgs(const sslSocket *ss, PRUint16 minVersion, PRBool forCert,
                             PRBool grease, sslBuffer *buf);
SECStatus ssl3_EncodeFilteredSigAlgs(const sslSocket *ss,
                                     const SSLSignatureScheme *schemes,
                                     PRUint32 numSchemes, PRBool grease, sslBuffer *buf);
SECStatus ssl3_FilterSigAlgs(const sslSocket *ss, PRUint16 minVersion, PRBool disableRsae, PRBool forCert,
                             unsigned int maxSchemes, SSLSignatureScheme *filteredSchemes,
                             unsigned int *numFilteredSchemes);
SECStatus ssl_GetCertificateRequestCAs(const sslSocket *ss,
                                       unsigned int *calenp,
                                       const SECItem **namesp,
                                       unsigned int *nnamesp);
SECStatus ssl3_ParseCertificateRequestCAs(sslSocket *ss, PRUint8 **b,
                                          PRUint32 *length, CERTDistNames *ca_list);
SECStatus ssl3_BeginHandleCertificateRequest(
    sslSocket *ss, const SSLSignatureScheme *signatureSchemes,
    unsigned int signatureSchemeCount, CERTDistNames *ca_list);
SECStatus ssl_ConstructServerHello(sslSocket *ss, PRBool helloRetry,
                                   const sslBuffer *extensionBuf,
                                   sslBuffer *messageBuf);
SECStatus ssl3_SendServerHello(sslSocket *ss);
SECStatus ssl3_SendChangeCipherSpecsInt(sslSocket *ss);
SECStatus ssl3_ComputeHandshakeHashes(sslSocket *ss,
                                      ssl3CipherSpec *spec,
                                      SSL3Hashes *hashes,
                                      PRUint32 sender);
SECStatus ssl_CreateECDHEphemeralKeyPair(const sslSocket *ss,
                                         const sslNamedGroupDef *ecGroup,
                                         sslEphemeralKeyPair **keyPair);
SECStatus ssl_CreateStaticECDHEKey(sslSocket *ss,
                                   const sslNamedGroupDef *ecGroup);
SECStatus ssl3_FlushHandshake(sslSocket *ss, PRInt32 flags);
SECStatus ssl3_GetNewRandom(SSL3Random random);
PK11SymKey *ssl3_GetWrappingKey(sslSocket *ss,
                                PK11SlotInfo *masterSecretSlot,
                                CK_MECHANISM_TYPE masterWrapMech,
                                void *pwArg);
SECStatus ssl3_FillInCachedSID(sslSocket *ss, sslSessionID *sid,
                               PK11SymKey *secret);
const ssl3CipherSuiteDef *ssl_LookupCipherSuiteDef(ssl3CipherSuite suite);
const ssl3CipherSuiteCfg *ssl_LookupCipherSuiteCfg(ssl3CipherSuite suite,
                                                   const ssl3CipherSuiteCfg *suites);
PRBool ssl3_CipherSuiteAllowedForVersionRange(ssl3CipherSuite cipherSuite,
                                              const SSLVersionRange *vrange);

SECStatus ssl3_SelectServerCert(sslSocket *ss);
SECStatus ssl_PrivateKeySupportsRsaPss(SECKEYPrivateKey *privKey,
                                       CERTCertificate *cert,
                                       void *pwArg,
                                       PRBool *supportsRsaPss);
SECStatus ssl_PickSignatureScheme(sslSocket *ss,
                                  CERTCertificate *cert,
                                  SECKEYPublicKey *pubKey,
                                  SECKEYPrivateKey *privKey,
                                  const SSLSignatureScheme *peerSchemes,
                                  unsigned int peerSchemeCount,
                                  PRBool requireSha1,
                                  SSLSignatureScheme *schemPtr);
SECStatus ssl_PickClientSignatureScheme(sslSocket *ss,
                                        CERTCertificate *clientCertificate,
                                        SECKEYPrivateKey *privKey,
                                        const SSLSignatureScheme *schemes,
                                        unsigned int numSchemes,
                                        SSLSignatureScheme *schemePtr);
SECOidTag ssl3_HashTypeToOID(SSLHashType hashType);
SECOidTag ssl3_AuthTypeToOID(SSLAuthType hashType);
SSLHashType ssl_SignatureSchemeToHashType(SSLSignatureScheme scheme);
SSLAuthType ssl_SignatureSchemeToAuthType(SSLSignatureScheme scheme);

SECStatus ssl3_SetupCipherSuite(sslSocket *ss, PRBool initHashes);
SECStatus ssl_InsertRecordHeader(const sslSocket *ss, ssl3CipherSpec *cwSpec,
                                 SSLContentType contentType, sslBuffer *wrBuf,
                                 PRBool *needsLength);
PRBool ssl_SignatureSchemeValid(SSLSignatureScheme scheme, SECOidTag spkiOid,
                                PRBool isTls13);

/* Pull in DTLS functions */
#include "dtlscon.h"

/* Pull in TLS 1.3 functions */
#include "tls13con.h"
#include "dtls13con.h"

/********************** misc calls *********************/

#ifdef DEBUG
extern void ssl3_CheckCipherSuiteOrderConsistency();
#endif

extern int ssl_MapLowLevelError(int hiLevelError);

PRTime ssl_Time(const sslSocket *ss);
PRBool ssl_TicketTimeValid(const sslSocket *ss, const NewSessionTicket *ticket);

extern void SSL_AtomicIncrementLong(long *x);

SECStatus ssl3_ApplyNSSPolicy(void);

extern SECStatus
ssl3_TLSPRFWithMasterSecret(sslSocket *ss, ssl3CipherSpec *spec,
                            const char *label, unsigned int labelLen,
                            const unsigned char *val, unsigned int valLen,
                            unsigned char *out, unsigned int outLen);

extern void
ssl3_RecordKeyLog(sslSocket *ss, const char *label, PK11SymKey *secret);

extern void
ssl3_WriteKeyLog(sslSocket *ss, const char *label, const SECItem *item);

PRBool ssl_AlpnTagAllowed(const sslSocket *ss, const SECItem *tag);

#ifdef TRACE
#define SSL_TRACE(msg) ssl_Trace msg
#else
#define SSL_TRACE(msg)
#endif

void ssl_Trace(const char *format, ...);

void ssl_CacheExternalToken(sslSocket *ss);
SECStatus ssl_DecodeResumptionToken(sslSessionID *sid, const PRUint8 *encodedTicket,
                                    PRUint32 encodedTicketLen);
PRBool ssl_IsResumptionTokenUsable(sslSocket *ss, sslSessionID *sid);

/* unwrap helper function to handle the case where the wrapKey doesn't wind
 *  * up in the correct token for the master secret */
PK11SymKey *ssl_unwrapSymKey(PK11SymKey *wrapKey,
                             CK_MECHANISM_TYPE wrapType, SECItem *param,
                             SECItem *wrappedKey,
                             CK_MECHANISM_TYPE target, CK_ATTRIBUTE_TYPE operation,
                             int keySize, CK_FLAGS keyFlags, void *pinArg);

/* determine if the current ssl connection is operating in FIPS mode */
PRBool ssl_isFIPS(sslSocket *ss);

/* Experimental APIs. Remove when stable. */

SECStatus SSLExp_SetResumptionTokenCallback(PRFileDesc *fd,
                                            SSLResumptionTokenCallback cb,
                                            void *ctx);
SECStatus SSLExp_SetResumptionToken(PRFileDesc *fd, const PRUint8 *token,
                                    unsigned int len);

SECStatus SSLExp_GetResumptionTokenInfo(const PRUint8 *tokenData, unsigned int tokenLen,
                                        SSLResumptionTokenInfo *token, unsigned int version);

SECStatus SSLExp_DestroyResumptionTokenInfo(SSLResumptionTokenInfo *token);

SECStatus SSLExp_SecretCallback(PRFileDesc *fd, SSLSecretCallback cb,
                                void *arg);
SECStatus SSLExp_RecordLayerWriteCallback(PRFileDesc *fd,
                                          SSLRecordWriteCallback write,
                                          void *arg);
SECStatus SSLExp_RecordLayerData(PRFileDesc *fd, PRUint16 epoch,
                                 SSLContentType contentType,
                                 const PRUint8 *data, unsigned int len);
SECStatus SSLExp_GetCurrentEpoch(PRFileDesc *fd, PRUint16 *readEpoch,
                                 PRUint16 *writeEpoch);

#define SSLResumptionTokenVersion 2

SECStatus SSLExp_MakeAead(PRUint16 version, PRUint16 cipherSuite, PK11SymKey *secret,
                          const char *labelPrefix, unsigned int labelPrefixLen,
                          SSLAeadContext **ctx);

SECStatus SSLExp_MakeVariantAead(PRUint16 version, PRUint16 cipherSuite, SSLProtocolVariant variant,
                                 PK11SymKey *secret, const char *labelPrefix,
                                 unsigned int labelPrefixLen, SSLAeadContext **ctx);
SECStatus SSLExp_DestroyAead(SSLAeadContext *ctx);
SECStatus SSLExp_AeadEncrypt(const SSLAeadContext *ctx, PRUint64 counter,
                             const PRUint8 *aad, unsigned int aadLen,
                             const PRUint8 *plaintext, unsigned int plaintextLen,
                             PRUint8 *out, unsigned int *outLen, unsigned int maxOut);
SECStatus SSLExp_AeadDecrypt(const SSLAeadContext *ctx, PRUint64 counter,
                             const PRUint8 *aad, unsigned int aadLen,
                             const PRUint8 *plaintext, unsigned int plaintextLen,
                             PRUint8 *out, unsigned int *outLen, unsigned int maxOut);

/* The next function is responsible for registering a certificate compression mechanism
 to be used for TLS connection.
 The caller passes SSLCertificateCompressionAlgorithm algorithm:

 typedef struct SSLCertificateCompressionAlgorithmStr {
    SSLCertificateCompressionAlgorithmID id;
    const char* name;
    SECStatus (*encode)(const SECItem* input, SECItem* output);
    SECStatus (*decode)(const SECItem* input, unsigned char* output, size_t outputLen, size_t* usedLen);
 } SSLCertificateCompressionAlgorithm;

 Certificate Compression encoding function is responsible for allocating the output buffer itself.
 If encoding function fails, the function has the install the appropriate error code and return an error.

 Certificate Compression decoding function operates an output buffer allocated in NSS.
 The function returns success or an error code.
 If successful, the function sets the number of bytes used to stored the decoded certificate
 in the outparam usedLen. If provided buffer is not enough to store the output (or any problem has occured during
 decoding of the buffer), the function has the install the appropriate error code and return an error.
 Note: usedLen is always <= outputLen.

 */
SECStatus SSLExp_SetCertificateCompressionAlgorithm(PRFileDesc *fd, SSLCertificateCompressionAlgorithm alg);
SECStatus SSLExp_HkdfExtract(PRUint16 version, PRUint16 cipherSuite,
                             PK11SymKey *salt, PK11SymKey *ikm, PK11SymKey **keyp);
SECStatus SSLExp_HkdfExpandLabel(PRUint16 version, PRUint16 cipherSuite, PK11SymKey *prk,
                                 const PRUint8 *hsHash, unsigned int hsHashLen,
                                 const char *label, unsigned int labelLen,
                                 PK11SymKey **key);
SECStatus SSLExp_HkdfVariantExpandLabel(PRUint16 version, PRUint16 cipherSuite, PK11SymKey *prk,
                                        const PRUint8 *hsHash, unsigned int hsHashLen,
                                        const char *label, unsigned int labelLen,
                                        SSLProtocolVariant variant, PK11SymKey **key);
SECStatus
SSLExp_HkdfExpandLabelWithMech(PRUint16 version, PRUint16 cipherSuite, PK11SymKey *prk,
                               const PRUint8 *hsHash, unsigned int hsHashLen,
                               const char *label, unsigned int labelLen,
                               CK_MECHANISM_TYPE mech, unsigned int keySize,
                               PK11SymKey **keyp);
SECStatus
SSLExp_HkdfVariantExpandLabelWithMech(PRUint16 version, PRUint16 cipherSuite, PK11SymKey *prk,
                                      const PRUint8 *hsHash, unsigned int hsHashLen,
                                      const char *label, unsigned int labelLen,
                                      CK_MECHANISM_TYPE mech, unsigned int keySize,
                                      SSLProtocolVariant variant, PK11SymKey **keyp);

SECStatus SSLExp_SetDtls13VersionWorkaround(PRFileDesc *fd, PRBool enabled);

SECStatus SSLExp_SetTimeFunc(PRFileDesc *fd, SSLTimeFunc f, void *arg);

extern SECStatus ssl_CreateMaskingContextInner(PRUint16 version, PRUint16 cipherSuite,
                                               SSLProtocolVariant variant,
                                               PK11SymKey *secret,
                                               const char *label,
                                               unsigned int labelLen,
                                               SSLMaskingContext **ctx);

extern SECStatus ssl_CreateMaskInner(SSLMaskingContext *ctx, const PRUint8 *sample,
                                     unsigned int sampleLen, PRUint8 *outMask,
                                     unsigned int maskLen);

extern SECStatus ssl_DestroyMaskingContextInner(SSLMaskingContext *ctx);

SECStatus SSLExp_CreateMaskingContext(PRUint16 version, PRUint16 cipherSuite,
                                      PK11SymKey *secret,
                                      const char *label,
                                      unsigned int labelLen,
                                      SSLMaskingContext **ctx);

SECStatus SSLExp_CreateVariantMaskingContext(PRUint16 version, PRUint16 cipherSuite,
                                             SSLProtocolVariant variant,
                                             PK11SymKey *secret,
                                             const char *label,
                                             unsigned int labelLen,
                                             SSLMaskingContext **ctx);

SECStatus SSLExp_CreateMask(SSLMaskingContext *ctx, const PRUint8 *sample,
                            unsigned int sampleLen, PRUint8 *mask,
                            unsigned int len);

SECStatus SSLExp_DestroyMaskingContext(SSLMaskingContext *ctx);

SECStatus SSLExp_EnableTls13GreaseEch(PRFileDesc *fd, PRBool enabled);
SECStatus SSLExp_SetTls13GreaseEchSize(PRFileDesc *fd, PRUint8 size);

SECStatus SSLExp_EnableTls13BackendEch(PRFileDesc *fd, PRBool enabled);
SECStatus SSLExp_CallExtensionWriterOnEchInner(PRFileDesc *fd, PRBool enabled);

SECStatus SSLExp_PeerCertificateChainDER(PRFileDesc *fd, SECItemArray **out);

SEC_END_PROTOS

#if defined(XP_UNIX)
#define SSL_GETPID getpid
#elif defined(WIN32)
#define SSL_GETPID _getpid
#else
#define SSL_GETPID() 0
#endif

#endif /* __sslimpl_h_ */
