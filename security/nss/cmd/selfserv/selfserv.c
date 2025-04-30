/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* -r flag is interepreted as follows:
 *  1 -r  means request, not require, on initial handshake.
 *  2 -r's mean request  and require, on initial handshake.
 *  3 -r's mean request, not require, on second handshake.
 *  4 -r's mean request  and require, on second handshake.
 */
#include <stdio.h>
#include <string.h>

#include "secutil.h"

#if defined(XP_UNIX)
#include <unistd.h>
#endif

#if defined(_WINDOWS)
#include <process.h> /* for getpid() */
#endif

#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>

#include "nspr.h"
#include "prio.h"
#include "prerror.h"
#include "prnetdb.h"
#include "prclist.h"
#include "plgetopt.h"
#include "pk11func.h"
#include "secitem.h"
#include "nss.h"
#include "ssl.h"
#include "sslproto.h"
#include "sslexp.h"
#include "cert.h"
#include "certt.h"
#include "ocsp.h"
#include "nssb64.h"
#include "zlib.h"

#ifndef PORT_Strstr
#define PORT_Strstr strstr
#endif

#ifndef PORT_Malloc
#define PORT_Malloc PR_Malloc
#endif

int NumSidCacheEntries = 1024;

static int handle_connection(PRFileDesc *, PRFileDesc *);

static const char envVarName[] = { SSL_ENV_VAR_NAME };
static const char inheritableSockName[] = { "SELFSERV_LISTEN_SOCKET" };

#define MAX_VIRT_SERVER_NAME_ARRAY_INDEX 10
#define MAX_CERT_NICKNAME_ARRAY_INDEX 10

#define DEFAULT_BULK_TEST 16384
#define MAX_BULK_TEST 1048576 /* 1 MB */
static PRBool testBulk;
static PRUint32 testBulkSize = DEFAULT_BULK_TEST;
static PRInt32 testBulkTotal;
static char *testBulkBuf;
static PRDescIdentity log_layer_id = PR_INVALID_IO_LAYER;
static PRFileDesc *loggingFD;
static PRIOMethods loggingMethods;

static PRBool logStats;
static PRBool loggingLayer;
static int logPeriod = 30;
static PRInt32 loggerOps;
static PRInt32 loggerBytes;
static PRInt32 loggerBytesTCP;
static PRInt32 bulkSentChunks;
static enum ocspStaplingModeEnum {
    osm_disabled,  /* server doesn't support stapling */
    osm_good,      /* supply a signed good status */
    osm_revoked,   /* supply a signed revoked status */
    osm_unknown,   /* supply a signed unknown status */
    osm_failure,   /* supply a unsigned failure status, "try later" */
    osm_badsig,    /* supply a good status response with a bad signature */
    osm_corrupted, /* supply a corrupted data block as the status */
    osm_random,    /* use a random response for each connection */
    osm_ocsp       /* retrieve ocsp status from external ocsp server,
              use empty status if server is unavailable */
} ocspStaplingMode = osm_disabled;
typedef enum ocspStaplingModeEnum ocspStaplingModeType;
static char *ocspStaplingCA = NULL;
static SECItemArray *certStatus[MAX_CERT_NICKNAME_ARRAY_INDEX] = { NULL };

const int ssl3CipherSuites[] = {
    -1,                                /* SSL_FORTEZZA_DMS_WITH_FORTEZZA_CBC_SHA* a */
    -1,                                /* SSL_FORTEZZA_DMS_WITH_RC4_128_SHA     * b */
    TLS_RSA_WITH_RC4_128_MD5,          /* c */
    TLS_RSA_WITH_3DES_EDE_CBC_SHA,     /* d */
    TLS_RSA_WITH_DES_CBC_SHA,          /* e */
    -1,                                /* TLS_RSA_EXPORT_WITH_RC4_40_MD5        * f */
    -1,                                /* TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5    * g */
    -1,                                /* SSL_FORTEZZA_DMS_WITH_NULL_SHA        * h */
    TLS_RSA_WITH_NULL_MD5,             /* i */
    -1,                                /* SSL_RSA_FIPS_WITH_3DES_EDE_CBC_SHA    * j */
    -1,                                /* SSL_RSA_FIPS_WITH_DES_CBC_SHA         * k */
    -1,                                /* TLS_RSA_EXPORT1024_WITH_DES_CBC_SHA   * l */
    -1,                                /* TLS_RSA_EXPORT1024_WITH_RC4_56_SHA    * m */
    TLS_RSA_WITH_RC4_128_SHA,          /* n */
    TLS_DHE_DSS_WITH_RC4_128_SHA,      /* o */
    TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA, /* p */
    TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA, /* q */
    TLS_DHE_RSA_WITH_DES_CBC_SHA,      /* r */
    TLS_DHE_DSS_WITH_DES_CBC_SHA,      /* s */
    TLS_DHE_DSS_WITH_AES_128_CBC_SHA,  /* t */
    TLS_DHE_RSA_WITH_AES_128_CBC_SHA,  /* u */
    TLS_RSA_WITH_AES_128_CBC_SHA,      /* v */
    TLS_DHE_DSS_WITH_AES_256_CBC_SHA,  /* w */
    TLS_DHE_RSA_WITH_AES_256_CBC_SHA,  /* x */
    TLS_RSA_WITH_AES_256_CBC_SHA,      /* y */
    TLS_RSA_WITH_NULL_SHA,             /* z */
    0
};

/* data and structures for shutdown */
static int stopping;

static PRBool noDelay;
static int requestCert;
static int verbose;
static SECItem bigBuf;
static int configureDHE = -1;        /* -1: don't configure, 0 disable, >=1 enable*/
static int configureReuseECDHE = -1; /* -1: don't configure, 0 refresh, >=1 reuse*/
static int configureWeakDHE = -1;    /* -1: don't configure, 0 disable, >=1 enable*/
SECItem psk = { siBuffer, NULL, 0 };
SECItem pskLabel = { siBuffer, NULL, 0 };
char *echParamsStr = NULL;

static PRThread *acceptorThread;

static PRLogModuleInfo *lm;

#define PRINTF   \
    if (verbose) \
    printf
#define FPRINTF  \
    if (verbose) \
    fprintf
#define FLUSH           \
    if (verbose) {      \
        fflush(stdout); \
        fflush(stderr); \
    }
#define VLOG(arg) PR_LOG(lm, PR_LOG_DEBUG, arg)

static void
PrintUsageHeader(const char *progName)
{
    fprintf(stderr,
            "Usage: %s -n rsa_nickname -p port [-BDENRZbjlmrsuvx] [-w password]\n"
            "         [-t threads] [-i pid_file] [-c ciphers] [-Y] [-d dbdir] [-g numblocks]\n"
            "         [-f password_file] [-L [seconds]] [-M maxProcs] [-P dbprefix]\n"
            "         [-V [min-version]:[max-version]] [-a sni_name]\n"
            "         [ T <good|revoked|unknown|badsig|corrupted|none|ocsp>] [-A ca]\n"
            "         [-C SSLCacheEntries] [-S dsa_nickname] [-Q]\n"
            "         [-I groups] [-J signatureschemes] [-e ec_nickname]\n"
            "         -U [0|1] -H [0|1|2] -W [0|1] [-z externalPsk] -q\n"
            "\n",
            progName);
}

static void
PrintParameterUsage()
{
    fputs(
        "-V [min]:[max] restricts the set of enabled SSL/TLS protocol versions.\n"
        "   All versions are enabled by default.\n"
        "   Possible values for min/max: ssl3 tls1.0 tls1.1 tls1.2 tls1.3\n"
        "   Example: \"-V ssl3:\" enables SSL 3 and newer.\n"
        "-D means disable Nagle delays in TCP\n"
        "-R means disable detection of rollback from TLS to SSL3\n"
        "-a configure server for SNI.\n"
        "-k expected name negotiated on server sockets\n"
        "-b means try binding to the port and exit\n"
        "-m means test the model-socket feature of SSL_ImportFD.\n"
        "-r flag is interepreted as follows:\n"
        "    1 -r  means request, not require, cert on initial handshake.\n"
        "    2 -r's mean request  and require, cert on initial handshake.\n"
        "    3 -r's mean request, not require, cert on second handshake.\n"
        "    4 -r's mean request  and require, cert on second handshake.\n"
        "-s means disable SSL socket locking for performance\n"
        "-u means enable Session Ticket extension for TLS.\n"
        "-v means verbose output\n"
        "-L seconds means log statistics every 'seconds' seconds (default=30).\n"
        "-M maxProcs tells how many processes to run in a multi-process server\n"
        "-N means do NOT use the server session cache.  Incompatible with -M.\n"
        "-t threads -- specify the number of threads to use for connections.\n"
        "-i pid_file file to write the process id of selfserve\n"
        "-l means use local threads instead of global threads\n"
        "-g numblocks means test throughput by sending total numblocks chunks\n"
        "    of size 16kb to the client, 0 means unlimited (default=0)\n"
        "-j means measure TCP throughput (for use with -g option)\n"
        "-C SSLCacheEntries sets the maximum number of entries in the SSL\n"
        "    session cache\n"
        "-T <mode> enable OCSP stapling. Possible modes:\n"
        "   none: don't send cert status (default)\n"
        "   good, revoked, unknown: Include locally signed response. Requires: -A\n"
        "   failure: return a failure response (try later, unsigned)\n"
        "   badsig: use a good status but with an invalid signature\n"
        "   corrupted: stapled cert status is an invalid block of data\n"
        "   random: each connection uses a random status from this list:\n"
        "           good, revoked, unknown, failure, badsig, corrupted\n"
        "   ocsp: fetch from external OCSP server using AIA, or none\n"
        "-A <ca> Nickname of a CA used to sign a stapled cert status\n"
        "-U override default ECDHE ephemeral key reuse, 0: refresh, 1: reuse\n"
        "-H override default DHE server support, 0: disable, 1: enable, "
        "   2: require DH named groups [RFC7919]\n"
        "-W override default DHE server weak parameters support, 0: disable, 1: enable\n"
        "-c Restrict ciphers\n"
        "-Y prints cipher values allowed for parameter -c and exits\n"
        "-G enables the extended master secret extension [RFC7627]\n"
        "-Q enables ALPN for HTTP/1.1 [RFC7301]\n"
        "-I comma separated list of enabled groups for TLS key exchange.\n"
        "   The following values are valid:\n"
        "   P256, P384, P521, x25519, FF2048, FF3072, FF4096, FF6144, FF8192,\n"
        "   xyber768d00, mlkem768x25519\n"
        "-J comma separated list of enabled signature schemes in preference order.\n"
        "   The following values are valid:\n"
        "     rsa_pkcs1_sha1, rsa_pkcs1_sha256, rsa_pkcs1_sha384, rsa_pkcs1_sha512,\n"
        "     ecdsa_sha1, ecdsa_secp256r1_sha256, ecdsa_secp384r1_sha384,\n"
        "     ecdsa_secp521r1_sha512,\n"
        "     rsa_pss_rsae_sha256, rsa_pss_rsae_sha384, rsa_pss_rsae_sha512,\n"
        "     rsa_pss_pss_sha256, rsa_pss_pss_sha384, rsa_pss_pss_sha512,\n"
        "-Z enable 0-RTT (for TLS 1.3; also use -u)\n"
        "-E enable post-handshake authentication\n"
        "   (for TLS 1.3; only has an effect with 3 or more -r options)\n"
        "-x Export and print keying material after successful handshake\n"
        "   The argument is a comma separated list of exporters in the form:\n"
        "     LABEL[:OUTPUT-LENGTH[:CONTEXT]]\n"
        "   where LABEL and CONTEXT can be either a free-form string or\n"
        "   a hex string if it is preceded by \"0x\"; OUTPUT-LENGTH\n"
        "   is a decimal integer.\n"
        "-z Configure a TLS 1.3 External PSK with the given hex string for a key.\n"
        "   To specify a label, use ':' as a delimiter. For example:\n"
        "   0xAAAABBBBCCCCDDDD:mylabel. Otherwise, the default label of\n"
        "  'Client_identity' will be used.\n"
        "-X Configure the server for ECH via the given <ECHParams>.  ECHParams\n"
        "   are expected in one of two formats:\n"
        "      1. A string containing the ECH public name prefixed by the substring\n"
        "         \"publicname:\". For example, \"publicname:example.com\". In this mode,\n"
        "         an ephemeral ECH keypair is generated and ECHConfigs are printed to stdout.\n"
        "      2. As a Base64 tuple of <ECHRawPrivateKey> || <ECHConfigs>. In this mode, the\n"
        "         raw private key is used to bootstrap the HPKE context.\n"
        "-q Enable zlib certificate compression\n",
        stderr);
}

static void
Usage(const char *progName)
{
    PrintUsageHeader(progName);
    PrintParameterUsage();
}

static void
PrintCipherUsage(const char *progName)
{
    PrintUsageHeader(progName);
    fputs(
        "-c ciphers   Letter(s) chosen from the following list\n"
        "c    SSL3 RSA WITH RC4 128 MD5\n"
        "d    SSL3 RSA WITH 3DES EDE CBC SHA\n"
        "e    SSL3 RSA WITH DES CBC SHA\n"
        "f    SSL3 RSA EXPORT WITH RC4 40 MD5\n"
        "g    SSL3 RSA EXPORT WITH RC2 CBC 40 MD5\n"
        "i    SSL3 RSA WITH NULL MD5\n"
        "j    SSL3 RSA FIPS WITH 3DES EDE CBC SHA\n"
        "k    SSL3 RSA FIPS WITH DES CBC SHA\n"
        "l    SSL3 RSA EXPORT WITH DES CBC SHA\t(new)\n"
        "m    SSL3 RSA EXPORT WITH RC4 56 SHA\t(new)\n"
        "n    SSL3 RSA WITH RC4 128 SHA\n"
        "o    TLS_DHE_DSS_WITH_RC4_128_SHA\n"
        "p    TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA\n"
        "q    TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA\n"
        "r    TLS_DHE_RSA_WITH_DES_CBC_SHA\n"
        "s    TLS_DHE_DSS_WITH_DES_CBC_SHA\n"
        "t    TLS_DHE_DSS_WITH_AES_128_CBC_SHA\n"
        "u    TLS_DHE_RSA_WITH_AES_128_CBC_SHA\n"
        "v    SSL3 RSA WITH AES 128 CBC SHA\n"
        "w    TLS_DHE_DSS_WITH_AES_256_CBC_SHA\n"
        "x    TLS_DHE_RSA_WITH_AES_256_CBC_SHA\n"
        "y    SSL3 RSA WITH AES 256 CBC SHA\n"
        "z    SSL3 RSA WITH NULL SHA\n"
        "\n"
        ":WXYZ  Use cipher with hex code { 0xWX , 0xYZ } in TLS\n",
        stderr);
}

static const char *
errWarn(char *funcString)
{
    PRErrorCode perr = PR_GetError();
    const char *errString = SECU_Strerror(perr);

    fprintf(stderr, "selfserv: %s returned error %d:\n%s\n",
            funcString, perr, errString);
    return errString;
}

static void
errExit(char *funcString)
{
    errWarn(funcString);
    exit(3);
}

/**************************************************************************
**
** Routines for disabling SSL ciphers.
**
**************************************************************************/

/* disable all the SSL cipher suites */
void
disableAllSSLCiphers(void)
{
    const PRUint16 *cipherSuites = SSL_ImplementedCiphers;
    int i = SSL_NumImplementedCiphers;
    SECStatus rv;

    while (--i >= 0) {
        PRUint16 suite = cipherSuites[i];
        rv = SSL_CipherPrefSetDefault(suite, PR_FALSE);
        if (rv != SECSuccess) {
            printf("SSL_CipherPrefSetDefault rejected suite 0x%04x (i = %d)\n",
                   suite, i);
            errWarn("SSL_CipherPrefSetDefault");
        }
    }
}

static SECStatus
mySSLAuthCertificate(void *arg, PRFileDesc *fd, PRBool checkSig,
                     PRBool isServer)
{
    SECStatus rv;
    CERTCertificate *peerCert;

    peerCert = SSL_PeerCertificate(fd);

    if (peerCert) {
        PRINTF("selfserv: Subject: %s\nselfserv: Issuer : %s\n",
               peerCert->subjectName, peerCert->issuerName);
        CERT_DestroyCertificate(peerCert);
    }

    rv = SSL_AuthCertificate(arg, fd, checkSig, isServer);

    if (rv == SECSuccess) {
        PRINTF("selfserv: -- SSL3: Certificate Validated.\n");
    } else {
        int err = PR_GetError();
        FPRINTF(stderr, "selfserv: -- SSL3: Certificate Invalid, err %d.\n%s\n",
                err, SECU_Strerror(err));
    }
    FLUSH;
    return rv;
}

void
printSSLStatistics()
{
    SSL3Statistics *ssl3stats = SSL_GetStatistics();

    printf(
        "selfserv: %ld cache hits; %ld cache misses, %ld cache not reusable\n"
        "          %ld stateless resumes, %ld ticket parse failures\n",
        ssl3stats->hch_sid_cache_hits, ssl3stats->hch_sid_cache_misses,
        ssl3stats->hch_sid_cache_not_ok, ssl3stats->hch_sid_stateless_resumes,
        ssl3stats->hch_sid_ticket_parse_failures);
}

void
printSecurityInfo(PRFileDesc *fd)
{
    CERTCertificate *cert = NULL;
    SECStatus result;
    SSLChannelInfo channel;
    SSLCipherSuiteInfo suite;

    if (verbose)
        printSSLStatistics();

    result = SSL_GetChannelInfo(fd, &channel, sizeof channel);
    if (result == SECSuccess &&
        channel.length == sizeof channel &&
        channel.cipherSuite) {
        result = SSL_GetCipherSuiteInfo(channel.cipherSuite,
                                        &suite, sizeof suite);
        if (result == SECSuccess) {
            FPRINTF(stderr,
                    "selfserv: SSL version %d.%d using %d-bit %s with %d-bit %s MAC%s\n",
                    channel.protocolVersion >> 8, channel.protocolVersion & 0xff,
                    suite.effectiveKeyBits, suite.symCipherName,
                    suite.macBits, suite.macAlgorithmName,
                    channel.isFIPS ? " FIPS" : "");
            FPRINTF(stderr,
                    "selfserv: Server Auth: %d-bit %s, Key Exchange: %d-bit %s\n"
                    "          Compression: %s, Extended Master Secret: %s\n",
                    channel.authKeyBits, suite.authAlgorithmName,
                    channel.keaKeyBits, suite.keaTypeName,
                    channel.compressionMethodName,
                    channel.extendedMasterSecretUsed ? "Yes" : "No");
        }
    }
    if (verbose) {
        SECItem *hostInfo = SSL_GetNegotiatedHostInfo(fd);
        if (hostInfo) {
            char namePref[] = "selfserv: Negotiated server name: ";

            fprintf(stderr, "%s", namePref);
            fwrite(hostInfo->data, hostInfo->len, 1, stderr);
            SECITEM_FreeItem(hostInfo, PR_TRUE);
            hostInfo = NULL;
            fprintf(stderr, "\n");
        }
    }
    if (requestCert)
        cert = SSL_PeerCertificate(fd);
    else
        cert = SSL_LocalCertificate(fd);
    if (cert) {
        char *ip = CERT_NameToAscii(&cert->issuer);
        char *sp = CERT_NameToAscii(&cert->subject);
        if (sp) {
            FPRINTF(stderr, "selfserv: subject DN: %s\n", sp);
            PORT_Free(sp);
        }
        if (ip) {
            FPRINTF(stderr, "selfserv: issuer  DN: %s\n", ip);
            PORT_Free(ip);
        }
        CERT_DestroyCertificate(cert);
        cert = NULL;
    }
    FLUSH;
}

static int MakeCertOK;

static SECStatus
myBadCertHandler(void *arg, PRFileDesc *fd)
{
    int err = PR_GetError();
    if (!MakeCertOK)
        fprintf(stderr,
                "selfserv: -- SSL: Client Certificate Invalid, err %d.\n%s\n",
                err, SECU_Strerror(err));
    return (MakeCertOK ? SECSuccess : SECFailure);
}

/* Simple SNI socket config function that does not use SSL_ReconfigFD.
 * Only uses one server name but verifies that the names match. */
PRInt32
mySSLSNISocketConfig(PRFileDesc *fd, const SECItem *sniNameArr,
                     PRUint32 sniNameArrSize, void *arg)
{
    PRInt32 i = 0;
    const SECItem *current = sniNameArr;
    const char **nameArr = (const char **)arg;
    secuPWData *pwdata;
    CERTCertificate *cert = NULL;
    SECKEYPrivateKey *privKey = NULL;

    PORT_Assert(fd && sniNameArr);
    if (!fd || !sniNameArr) {
        return SSL_SNI_SEND_ALERT;
    }

    pwdata = SSL_RevealPinArg(fd);

    for (; current && (PRUint32)i < sniNameArrSize; i++) {
        unsigned int j = 0;
        for (; j < MAX_VIRT_SERVER_NAME_ARRAY_INDEX && nameArr[j]; j++) {
            if (!PORT_Strncmp(nameArr[j],
                              (const char *)current[i].data,
                              current[i].len) &&
                PORT_Strlen(nameArr[j]) == current[i].len) {
                const char *nickName = nameArr[j];
                if (j == 0) {
                    /* default cert */
                    return 0;
                }
                /* if pwdata is NULL, then we would not get the key and
                 * return an error status. */
                cert = PK11_FindCertFromNickname(nickName, &pwdata);
                if (cert == NULL) {
                    goto loser; /* Send alert */
                }
                privKey = PK11_FindKeyByAnyCert(cert, &pwdata);
                if (privKey == NULL) {
                    goto loser; /* Send alert */
                }
                if (SSL_ConfigServerCert(fd, cert, privKey, NULL, 0) != SECSuccess) {
                    goto loser; /* Send alert */
                }
                SECKEY_DestroyPrivateKey(privKey);
                CERT_DestroyCertificate(cert);
                return i;
            }
        }
    }
loser:
    if (privKey) {
        SECKEY_DestroyPrivateKey(privKey);
    }
    if (cert) {
        CERT_DestroyCertificate(cert);
    }
    return SSL_SNI_SEND_ALERT;
}

/**************************************************************************
** Begin thread management routines and data.
**************************************************************************/
#define MIN_THREADS 3
#define DEFAULT_THREADS 8
#define MAX_THREADS 4096
#define MAX_PROCS 25
static int maxThreads = DEFAULT_THREADS;

typedef struct jobStr {
    PRCList link;
    PRFileDesc *tcp_sock;
    PRFileDesc *model_sock;
} JOB;

static PZLock *qLock;             /* this lock protects all data immediately below */
static PRLock *lastLoadedCrlLock; /* this lock protects lastLoadedCrl variable */
static PZCondVar *jobQNotEmptyCv;
static PZCondVar *freeListNotEmptyCv;
static PZCondVar *threadCountChangeCv;
static int threadCount;
static PRCList jobQ;
static PRCList freeJobs;
static JOB *jobTable;

SECStatus
setupJobs(int maxJobs)
{
    int i;

    jobTable = (JOB *)PR_Calloc(maxJobs, sizeof(JOB));
    if (!jobTable)
        return SECFailure;

    PR_INIT_CLIST(&jobQ);
    PR_INIT_CLIST(&freeJobs);

    for (i = 0; i < maxJobs; ++i) {
        JOB *pJob = jobTable + i;
        PR_APPEND_LINK(&pJob->link, &freeJobs);
    }
    return SECSuccess;
}

typedef int startFn(PRFileDesc *a, PRFileDesc *b);

typedef enum { rs_idle = 0,
               rs_running = 1,
               rs_zombie = 2 } runState;

typedef struct perThreadStr {
    PRFileDesc *a;
    PRFileDesc *b;
    int rv;
    startFn *startFunc;
    PRThread *prThread;
    runState state;
} perThread;

static perThread *threads;

void
thread_wrapper(void *arg)
{
    perThread *slot = (perThread *)arg;

    slot->rv = (*slot->startFunc)(slot->a, slot->b);

    /* notify the thread exit handler. */
    PZ_Lock(qLock);
    slot->state = rs_zombie;
    --threadCount;
    PZ_NotifyAllCondVar(threadCountChangeCv);
    PZ_Unlock(qLock);
}

int
jobLoop(PRFileDesc *a, PRFileDesc *b)
{
    PRCList *myLink = 0;
    JOB *myJob;

    PZ_Lock(qLock);
    do {
        myLink = 0;
        while (PR_CLIST_IS_EMPTY(&jobQ) && !stopping) {
            PZ_WaitCondVar(jobQNotEmptyCv, PR_INTERVAL_NO_TIMEOUT);
        }
        if (!PR_CLIST_IS_EMPTY(&jobQ)) {
            myLink = PR_LIST_HEAD(&jobQ);
            PR_REMOVE_AND_INIT_LINK(myLink);
        }
        PZ_Unlock(qLock);
        myJob = (JOB *)myLink;
        /* myJob will be null when stopping is true and jobQ is empty */
        if (!myJob)
            break;
        handle_connection(myJob->tcp_sock, myJob->model_sock);
        PZ_Lock(qLock);
        PR_APPEND_LINK(myLink, &freeJobs);
        PZ_NotifyCondVar(freeListNotEmptyCv);
    } while (PR_TRUE);
    return 0;
}

SECStatus
launch_threads(
    startFn *startFunc,
    PRFileDesc *a,
    PRFileDesc *b,
    PRBool local)
{
    int i;
    SECStatus rv = SECSuccess;

    /* create the thread management serialization structs */
    qLock = PZ_NewLock(nssILockSelfServ);
    jobQNotEmptyCv = PZ_NewCondVar(qLock);
    freeListNotEmptyCv = PZ_NewCondVar(qLock);
    threadCountChangeCv = PZ_NewCondVar(qLock);

    /* create monitor for crl reload procedure */
    lastLoadedCrlLock = PR_NewLock();

    /* allocate the array of thread slots */
    threads = PR_Calloc(maxThreads, sizeof(perThread));
    if (NULL == threads) {
        fprintf(stderr, "Oh Drat! Can't allocate the perThread array\n");
        return SECFailure;
    }
    /* 5 is a little extra, intended to keep the jobQ from underflowing.
    ** That is, from going empty while not stopping and clients are still
    ** trying to contact us.
    */
    rv = setupJobs(maxThreads + 5);
    if (rv != SECSuccess)
        return rv;

    PZ_Lock(qLock);
    for (i = 0; i < maxThreads; ++i) {
        perThread *slot = threads + i;

        slot->state = rs_running;
        slot->a = a;
        slot->b = b;
        slot->startFunc = startFunc;
        slot->prThread = PR_CreateThread(PR_USER_THREAD,
                                         thread_wrapper, slot, PR_PRIORITY_NORMAL,
                                         (PR_TRUE ==
                                          local)
                                             ? PR_LOCAL_THREAD
                                             : PR_GLOBAL_THREAD,
                                         PR_JOINABLE_THREAD, 0);
        if (slot->prThread == NULL) {
            printf("selfserv: Failed to launch thread!\n");
            slot->state = rs_idle;
            rv = SECFailure;
            break;
        }

        ++threadCount;
    }
    PZ_Unlock(qLock);

    return rv;
}

#define DESTROY_CONDVAR(name)    \
    if (name) {                  \
        PZ_DestroyCondVar(name); \
        name = NULL;             \
    }
#define DESTROY_LOCK(name)    \
    if (name) {               \
        PZ_DestroyLock(name); \
        name = NULL;          \
    }

void
terminateWorkerThreads(void)
{
    int i;

    VLOG(("selfserv: server_thread: waiting on stopping"));
    PZ_Lock(qLock);
    PZ_NotifyAllCondVar(jobQNotEmptyCv);
    PZ_Unlock(qLock);

    /* Wait for worker threads to terminate. */
    for (i = 0; i < maxThreads; ++i) {
        perThread *slot = threads + i;
        if (slot->prThread) {
            PR_JoinThread(slot->prThread);
        }
    }

    /* The worker threads empty the jobQ before they terminate. */
    PZ_Lock(qLock);
    PORT_Assert(threadCount == 0);
    PORT_Assert(PR_CLIST_IS_EMPTY(&jobQ));
    PZ_Unlock(qLock);

    DESTROY_CONDVAR(jobQNotEmptyCv);
    DESTROY_CONDVAR(freeListNotEmptyCv);
    DESTROY_CONDVAR(threadCountChangeCv);

    PR_DestroyLock(lastLoadedCrlLock);
    DESTROY_LOCK(qLock);
    PR_Free(jobTable);
    PR_Free(threads);
}

static void
logger(void *arg)
{
    PRFloat64 seconds;
    PRFloat64 opsPerSec;
    PRIntervalTime period;
    PRIntervalTime previousTime;
    PRIntervalTime latestTime;
    PRInt32 previousOps;
    PRInt32 ops;
    PRIntervalTime logPeriodTicks = PR_TicksPerSecond();
    PRFloat64 secondsPerTick = 1.0 / (PRFloat64)logPeriodTicks;
    int iterations = 0;
    int secondsElapsed = 0;
    static PRInt64 totalPeriodBytes = 0;
    static PRInt64 totalPeriodBytesTCP = 0;

    previousOps = loggerOps;
    previousTime = PR_IntervalNow();

    for (;;) {
        /* OK, implementing a new sleep algorithm here... always sleep
         * for 1 second but print out info at the user-specified interval.
         * This way, we don't overflow all of our PR_Atomic* functions and
         * we don't have to use locks.
         */
        PR_Sleep(logPeriodTicks);
        secondsElapsed++;
        totalPeriodBytes += PR_ATOMIC_SET(&loggerBytes, 0);
        totalPeriodBytesTCP += PR_ATOMIC_SET(&loggerBytesTCP, 0);
        if (secondsElapsed != logPeriod) {
            continue;
        }
        /* when we reach the user-specified logging interval, print out all
         * data
         */
        secondsElapsed = 0;
        latestTime = PR_IntervalNow();
        ops = loggerOps;
        period = latestTime - previousTime;
        seconds = (PRFloat64)period * secondsPerTick;
        opsPerSec = (ops - previousOps) / seconds;

        if (testBulk) {
            if (iterations == 0) {
                if (loggingLayer == PR_TRUE) {
                    printf("Conn.--------App Data--------TCP Data\n");
                } else {
                    printf("Conn.--------App Data\n");
                }
            }
            if (loggingLayer == PR_TRUE) {
                printf("%4.d       %5.3f MB/s      %5.3f MB/s\n", ops,
                       totalPeriodBytes / (seconds * 1048576.0),
                       totalPeriodBytesTCP / (seconds * 1048576.0));
            } else {
                printf("%4.d       %5.3f MB/s\n", ops,
                       totalPeriodBytes / (seconds * 1048576.0));
            }
            totalPeriodBytes = 0;
            totalPeriodBytesTCP = 0;
            /* Print the "legend" every 20 iterations */
            iterations = (iterations + 1) % 20;
        } else {
            printf("%.2f ops/second, %d threads\n", opsPerSec, threadCount);
        }

        fflush(stdout);
        previousOps = ops;
        previousTime = latestTime;
        if (stopping) {
            break;
        }
    }
}

/**************************************************************************
** End   thread management routines.
**************************************************************************/

PRBool useModelSocket = PR_FALSE;
static SSLVersionRange enabledVersions;
PRBool disableRollBack = PR_FALSE;
PRBool NoReuse = PR_FALSE;
PRBool hasSidCache = PR_FALSE;
PRBool disableLocking = PR_FALSE;
PRBool enableSessionTickets = PR_FALSE;
PRBool enableZlibCertificateCompression = PR_FALSE;
PRBool failedToNegotiateName = PR_FALSE;
PRBool enableExtendedMasterSecret = PR_FALSE;
PRBool zeroRTT = PR_FALSE;
SSLAntiReplayContext *antiReplay = NULL;
PRBool enableALPN = PR_FALSE;
PRBool enablePostHandshakeAuth = PR_FALSE;
SSLNamedGroup *enabledGroups = NULL;
unsigned int enabledGroupsCount = 0;
const SSLSignatureScheme *enabledSigSchemes = NULL;
unsigned int enabledSigSchemeCount = 0;
const secuExporter *enabledExporters = NULL;
unsigned int enabledExporterCount = 0;

static char *virtServerNameArray[MAX_VIRT_SERVER_NAME_ARRAY_INDEX];
static int virtServerNameIndex = 1;

static char *certNicknameArray[MAX_CERT_NICKNAME_ARRAY_INDEX];
static int certNicknameIndex = 0;

static const char stopCmd[] = { "GET /stop " };
static const char getCmd[] = { "GET " };
static const char EOFmsg[] = { "EOF\r\n\r\n\r\n" };
static const char outHeader[] = {
    "HTTP/1.0 200 OK\r\n"
    "Server: Generic Web Server\r\n"
    "Date: Tue, 26 Aug 1997 22:10:05 GMT\r\n"
    "Content-type: text/plain\r\n"
    "\r\n"
};
static const char crlCacheErr[] = { "CRL ReCache Error: " };

PRUint16 cipherlist[100];
int nciphers;

void
savecipher(int c)
{
    if (nciphers < sizeof cipherlist / sizeof(cipherlist[0]))
        cipherlist[nciphers++] = (PRUint16)c;
}

#ifdef FULL_DUPLEX_CAPABLE

struct lockedVarsStr {
    PZLock *lock;
    int count;
    int waiters;
    PZCondVar *condVar;
};

typedef struct lockedVarsStr lockedVars;

void
lockedVars_Init(lockedVars *lv)
{
    lv->count = 0;
    lv->waiters = 0;
    lv->lock = PZ_NewLock(nssILockSelfServ);
    lv->condVar = PZ_NewCondVar(lv->lock);
}

void
lockedVars_Destroy(lockedVars *lv)
{
    PZ_DestroyCondVar(lv->condVar);
    lv->condVar = NULL;

    PZ_DestroyLock(lv->lock);
    lv->lock = NULL;
}

void
lockedVars_WaitForDone(lockedVars *lv)
{
    PZ_Lock(lv->lock);
    while (lv->count > 0) {
        PZ_WaitCondVar(lv->condVar, PR_INTERVAL_NO_TIMEOUT);
    }
    PZ_Unlock(lv->lock);
}

int /* returns count */
lockedVars_AddToCount(lockedVars *lv, int addend)
{
    int rv;

    PZ_Lock(lv->lock);
    rv = lv->count += addend;
    if (rv <= 0) {
        PZ_NotifyCondVar(lv->condVar);
    }
    PZ_Unlock(lv->lock);
    return rv;
}

int
do_writes(
    PRFileDesc *ssl_sock,
    PRFileDesc *model_sock)
{
    int sent = 0;
    int count = 0;
    lockedVars *lv = (lockedVars *)model_sock;

    VLOG(("selfserv: do_writes: starting"));
    while (sent < bigBuf.len) {

        count = PR_Write(ssl_sock, bigBuf.data + sent, bigBuf.len - sent);
        if (count < 0) {
            errWarn("PR_Write bigBuf");
            break;
        }
        FPRINTF(stderr, "selfserv: PR_Write wrote %d bytes from bigBuf\n", count);
        sent += count;
    }
    if (count >= 0) { /* last write didn't fail. */
        PR_Shutdown(ssl_sock, PR_SHUTDOWN_SEND);
    }

    /* notify the reader that we're done. */
    lockedVars_AddToCount(lv, -1);
    FLUSH;
    VLOG(("selfserv: do_writes: exiting"));
    return (sent < bigBuf.len) ? SECFailure : SECSuccess;
}

static int
handle_fdx_connection(
    PRFileDesc *tcp_sock,
    PRFileDesc *model_sock)
{
    PRFileDesc *ssl_sock = NULL;
    SECStatus result;
    int firstTime = 1;
    lockedVars lv;
    PRSocketOptionData opt;
    char buf[10240];

    VLOG(("selfserv: handle_fdx_connection: starting"));
    opt.option = PR_SockOpt_Nonblocking;
    opt.value.non_blocking = PR_FALSE;
    PR_SetSocketOption(tcp_sock, &opt);

    if (useModelSocket && model_sock) {
        SECStatus rv;
        ssl_sock = SSL_ImportFD(model_sock, tcp_sock);
        if (!ssl_sock) {
            errWarn("SSL_ImportFD with model");
            goto cleanup;
        }
        rv = SSL_ResetHandshake(ssl_sock, /* asServer */ 1);
        if (rv != SECSuccess) {
            errWarn("SSL_ResetHandshake");
            goto cleanup;
        }
    } else {
        ssl_sock = tcp_sock;
    }

    lockedVars_Init(&lv);
    lockedVars_AddToCount(&lv, 1);

    /* Attempt to launch the writer thread. */
    result = launch_thread(do_writes, ssl_sock, (PRFileDesc *)&lv);

    if (result == SECSuccess)
        do {
            /* do reads here. */
            int count;
            count = PR_Read(ssl_sock, buf, sizeof buf);
            if (count < 0) {
                errWarn("FDX PR_Read");
                break;
            }
            FPRINTF(stderr, "selfserv: FDX PR_Read read %d bytes.\n", count);
            if (firstTime) {
                firstTime = 0;
                printSecurityInfo(ssl_sock);
            }
        } while (lockedVars_AddToCount(&lv, 0) > 0);

    /* Wait for writer to finish */
    lockedVars_WaitForDone(&lv);
    lockedVars_Destroy(&lv);
    FLUSH;

cleanup:
    if (ssl_sock) {
        PR_Close(ssl_sock);
    } else if (tcp_sock) {
        PR_Close(tcp_sock);
    }

    VLOG(("selfserv: handle_fdx_connection: exiting"));
    return SECSuccess;
}

#endif

static SECItem *lastLoadedCrl = NULL;

static SECStatus
reload_crl(PRFileDesc *crlFile)
{
    SECItem *crlDer;
    CERTCertDBHandle *certHandle = CERT_GetDefaultCertDB();
    SECStatus rv;

    /* Read in the entire file specified with the -f argument */
    crlDer = PORT_Malloc(sizeof(SECItem));
    if (!crlDer) {
        errWarn("Can not allocate memory.");
        return SECFailure;
    }

    rv = SECU_ReadDERFromFile(crlDer, crlFile, PR_FALSE, PR_FALSE);
    if (rv != SECSuccess) {
        errWarn("Unable to read input file.");
        PORT_Free(crlDer);
        return SECFailure;
    }

    PR_Lock(lastLoadedCrlLock);
    rv = CERT_CacheCRL(certHandle, crlDer);
    if (rv == SECSuccess) {
        SECItem *tempItem = crlDer;
        if (lastLoadedCrl != NULL) {
            rv = CERT_UncacheCRL(certHandle, lastLoadedCrl);
            if (rv != SECSuccess) {
                errWarn("Unable to uncache crl.");
                goto loser;
            }
            crlDer = lastLoadedCrl;
        } else {
            crlDer = NULL;
        }
        lastLoadedCrl = tempItem;
    }

loser:
    PR_Unlock(lastLoadedCrlLock);
    SECITEM_FreeItem(crlDer, PR_TRUE);
    return rv;
}

void
stop_server()
{
    stopping = 1;
    PR_Interrupt(acceptorThread);
    PZ_TraceFlush();
}

SECItemArray *
makeTryLaterOCSPResponse(PLArenaPool *arena)
{
    SECItemArray *result = NULL;
    SECItem *ocspResponse = NULL;

    ocspResponse = CERT_CreateEncodedOCSPErrorResponse(arena,
                                                       SEC_ERROR_OCSP_TRY_SERVER_LATER);
    if (!ocspResponse)
        errExit("cannot created ocspResponse");

    result = SECITEM_AllocArray(arena, NULL, 1);
    if (!result)
        errExit("cannot allocate multiOcspResponses");

    result->items[0].data = ocspResponse->data;
    result->items[0].len = ocspResponse->len;

    return result;
}

SECItemArray *
makeCorruptedOCSPResponse(PLArenaPool *arena)
{
    SECItemArray *result = NULL;
    SECItem *ocspResponse = NULL;

    ocspResponse = SECITEM_AllocItem(arena, NULL, 1);
    if (!ocspResponse)
        errExit("cannot created ocspResponse");

    result = SECITEM_AllocArray(arena, NULL, 1);
    if (!result)
        errExit("cannot allocate multiOcspResponses");

    result->items[0].data = ocspResponse->data;
    result->items[0].len = ocspResponse->len;

    return result;
}

SECItemArray *
makeSignedOCSPResponse(PLArenaPool *arena,
                       CERTCertificate *cert, secuPWData *pwdata)
{
    SECItemArray *result = NULL;
    SECItem *ocspResponse = NULL;
    CERTOCSPSingleResponse **singleResponses;
    CERTOCSPSingleResponse *sr = NULL;
    CERTOCSPCertID *cid = NULL;
    CERTCertificate *ca;
    PRTime now = PR_Now();
    PRTime nextUpdate;

    PORT_Assert(cert != NULL);

    ca = CERT_FindCertByNickname(CERT_GetDefaultCertDB(), ocspStaplingCA);
    if (!ca)
        errExit("cannot find CA");

    cid = CERT_CreateOCSPCertID(cert, now);
    if (!cid)
        errExit("cannot created cid");

    nextUpdate = now + (PRTime)60 * 60 * 24 * PR_USEC_PER_SEC; /* plus 1 day */

    switch (ocspStaplingMode) {
        case osm_good:
        case osm_badsig:
            sr = CERT_CreateOCSPSingleResponseGood(arena, cid, now,
                                                   &nextUpdate);
            break;
        case osm_unknown:
            sr = CERT_CreateOCSPSingleResponseUnknown(arena, cid, now,
                                                      &nextUpdate);
            break;
        case osm_revoked:
            sr = CERT_CreateOCSPSingleResponseRevoked(arena, cid, now,
                                                      &nextUpdate,
                                                      now - (PRTime)60 * 60 * 24 * PR_USEC_PER_SEC, /* minus 1 day */
                                                      NULL);
            break;
        default:
            PORT_Assert(0);
            break;
    }

    if (!sr)
        errExit("cannot create sr");

    /* meaning of value 2: one entry + one end marker */
    singleResponses = PORT_ArenaNewArray(arena, CERTOCSPSingleResponse *, 2);
    if (singleResponses == NULL)
        errExit("cannot allocate singleResponses");

    singleResponses[0] = sr;
    singleResponses[1] = NULL;

    ocspResponse = CERT_CreateEncodedOCSPSuccessResponse(arena,
                                                         (ocspStaplingMode == osm_badsig)
                                                             ? NULL
                                                             : ca,
                                                         ocspResponderID_byName, now, singleResponses,
                                                         &pwdata);
    if (!ocspResponse)
        errExit("cannot created ocspResponse");

    CERT_DestroyCertificate(ca);
    ca = NULL;

    result = SECITEM_AllocArray(arena, NULL, 1);
    if (!result)
        errExit("cannot allocate multiOcspResponses");

    result->items[0].data = ocspResponse->data;
    result->items[0].len = ocspResponse->len;

    CERT_DestroyOCSPCertID(cid);
    cid = NULL;

    return result;
}

void
setupCertStatus(PLArenaPool *arena,
                CERTCertificate *cert, int index, secuPWData *pwdata)
{
    if (ocspStaplingMode == osm_random) {
        /* 6 different responses */
        int r = rand() % 6;
        switch (r) {
            case 0:
                ocspStaplingMode = osm_good;
                break;
            case 1:
                ocspStaplingMode = osm_revoked;
                break;
            case 2:
                ocspStaplingMode = osm_unknown;
                break;
            case 3:
                ocspStaplingMode = osm_badsig;
                break;
            case 4:
                ocspStaplingMode = osm_corrupted;
                break;
            case 5:
                ocspStaplingMode = osm_failure;
                break;
            default:
                PORT_Assert(0);
                break;
        }
    }
    if (ocspStaplingMode != osm_disabled) {
        SECItemArray *multiOcspResponses = NULL;
        switch (ocspStaplingMode) {
            case osm_good:
            case osm_revoked:
            case osm_unknown:
            case osm_badsig:
                multiOcspResponses =
                    makeSignedOCSPResponse(arena, cert,
                                           pwdata);
                break;
            case osm_corrupted:
                multiOcspResponses = makeCorruptedOCSPResponse(arena);
                break;
            case osm_failure:
                multiOcspResponses = makeTryLaterOCSPResponse(arena);
                break;
            case osm_ocsp:
                errExit("stapling mode \"ocsp\" not implemented");
                break;
                break;
            default:
                break;
        }
        if (multiOcspResponses) {
            certStatus[index] = multiOcspResponses;
        }
    }
}

int
handle_connection(PRFileDesc *tcp_sock, PRFileDesc *model_sock)
{
    PRFileDesc *ssl_sock = NULL;
    PRFileDesc *local_file_fd = NULL;
    char *post;
    char *pBuf; /* unused space at end of buf */
    const char *errString;
    PRStatus status;
    int bufRem;    /* unused bytes at end of buf */
    int bufDat;    /* characters received in buf */
    int newln = 0; /* # of consecutive newlns */
    int firstTime = 1;
    int reqLen;
    int rv;
    int numIOVs;
    PRSocketOptionData opt;
    PRIOVec iovs[16];
    char msgBuf[160];
    char buf[10240] = { 0 };
    char fileName[513];
    char proto[128];
    PRDescIdentity aboveLayer = PR_INVALID_IO_LAYER;

    pBuf = buf;
    bufRem = sizeof buf;

    VLOG(("selfserv: handle_connection: starting"));
    opt.option = PR_SockOpt_Nonblocking;
    opt.value.non_blocking = PR_FALSE;
    PR_SetSocketOption(tcp_sock, &opt);

    VLOG(("selfserv: handle_connection: starting\n"));
    if (useModelSocket && model_sock) {
        ssl_sock = SSL_ImportFD(model_sock, tcp_sock);
        if (!ssl_sock) {
            errWarn("SSL_ImportFD with model");
            goto cleanup;
        }
        rv = SSL_ResetHandshake(ssl_sock, /* asServer */ 1);
        if (rv != SECSuccess) {
            errWarn("SSL_ResetHandshake");
            goto cleanup;
        }
    } else {
        ssl_sock = tcp_sock;
    }

    if (loggingLayer) {
        /* find the layer where our new layer is to be pushed */
        aboveLayer = PR_GetLayersIdentity(ssl_sock->lower);
        if (aboveLayer == PR_INVALID_IO_LAYER) {
            errExit("PRGetUniqueIdentity");
        }
        /* create the new layer - this is a very cheap operation */
        loggingFD = PR_CreateIOLayerStub(log_layer_id, &loggingMethods);
        if (!loggingFD)
            errExit("PR_CreateIOLayerStub");
        /* push the layer below ssl but above TCP */
        rv = PR_PushIOLayer(ssl_sock, aboveLayer, loggingFD);
        if (rv != PR_SUCCESS) {
            errExit("PR_PushIOLayer");
        }
    }

    if (noDelay) {
        opt.option = PR_SockOpt_NoDelay;
        opt.value.no_delay = PR_TRUE;
        status = PR_SetSocketOption(ssl_sock, &opt);
        if (status != PR_SUCCESS) {
            errWarn("PR_SetSocketOption(PR_SockOpt_NoDelay, PR_TRUE)");
            if (ssl_sock) {
                PR_Close(ssl_sock);
            }
            return SECFailure;
        }
    }

    while (1) {
        newln = 0;
        reqLen = 0;
        rv = PR_Read(ssl_sock, pBuf, bufRem - 1);
        if (rv == 0 ||
            (rv < 0 && PR_END_OF_FILE_ERROR == PR_GetError())) {
            if (verbose)
                errWarn("HDX PR_Read hit EOF");
            break;
        }
        if (rv < 0) {
            errWarn("HDX PR_Read");
            goto cleanup;
        }
        /* NULL termination */
        pBuf[rv] = 0;
        if (firstTime) {
            firstTime = 0;
            printSecurityInfo(ssl_sock);
        }

        pBuf += rv;
        bufRem -= rv;
        bufDat = pBuf - buf;
        /* Parse the input, starting at the beginning of the buffer.
         * Stop when we detect two consecutive \n's (or \r\n's)
         * as this signifies the end of the GET or POST portion.
         * The posted data follows.
         */
        while (reqLen < bufDat && newln < 2) {
            int octet = buf[reqLen++];
            if (octet == '\n') {
                newln++;
            } else if (octet != '\r') {
                newln = 0;
            }
        }

        /* came to the end of the buffer, or second newln
         * If we didn't get an empty line (CRLFCRLF) then keep on reading.
         */
        if (newln < 2)
            continue;

        /* we're at the end of the HTTP request.
         * If the request is a POST, then there will be one more
         * line of data.
         * This parsing is a hack, but ok for SSL test purposes.
         */
        post = PORT_Strstr(buf, "POST ");
        if (!post || *post != 'P')
            break;

        /* It's a post, so look for the next and final CR/LF. */
        /* We should parse content length here, but ... */
        while (reqLen < bufDat && newln < 3) {
            int octet = buf[reqLen++];
            if (octet == '\n') {
                newln++;
            }
        }
        if (newln == 3)
            break;
    } /* read loop */

    bufDat = pBuf - buf;
    if (bufDat)
        do { /* just close if no data */
            /* Have either (a) a complete get, (b) a complete post, (c) EOF */
            if (reqLen > 0 && !strncmp(buf, getCmd, sizeof getCmd - 1)) {
                char *fnBegin = buf + 4;
                char *fnEnd;
                PRFileInfo info;
                /* try to open the file named.
                 * If successful, then write it to the client.
                 */
                fnEnd = strpbrk(fnBegin, " \r\n");
                if (fnEnd) {
                    int fnLen = fnEnd - fnBegin;
                    if (fnLen < sizeof fileName) {
                        char *real_fileName = fileName;
                        char *protoEnd = NULL;
                        strncpy(fileName, fnBegin, fnLen);
                        fileName[fnLen] = 0; /* null terminate */
                        if ((protoEnd = strstr(fileName, "://")) != NULL) {
                            int protoLen = PR_MIN(protoEnd - fileName, sizeof(proto) - 1);
                            PL_strncpy(proto, fileName, protoLen);
                            proto[protoLen] = 0;
                            real_fileName = protoEnd + 3;
                        } else {
                            proto[0] = 0;
                        }
                        status = PR_GetFileInfo(real_fileName, &info);
                        if (status == PR_SUCCESS &&
                            info.type == PR_FILE_FILE &&
                            info.size >= 0) {
                            local_file_fd = PR_Open(real_fileName, PR_RDONLY, 0);
                        }
                    }
                }
            }
            /* if user has requested client auth in a subsequent handshake,
             * do it here.
             */
            if (requestCert > 2) { /* request cert was 3 or 4 */
                CERTCertificate *cert = SSL_PeerCertificate(ssl_sock);
                if (cert) {
                    CERT_DestroyCertificate(cert);
                } else {
                    rv = SSL_OptionSet(ssl_sock, SSL_REQUEST_CERTIFICATE, 1);
                    if (rv < 0) {
                        errWarn("second SSL_OptionSet SSL_REQUEST_CERTIFICATE");
                        break;
                    }
                    rv = SSL_OptionSet(ssl_sock, SSL_REQUIRE_CERTIFICATE,
                                       (requestCert == 4));
                    if (rv < 0) {
                        errWarn("second SSL_OptionSet SSL_REQUIRE_CERTIFICATE");
                        break;
                    }
                    if (enablePostHandshakeAuth) {
                        rv = SSL_SendCertificateRequest(ssl_sock);
                        if (rv != SECSuccess) {
                            errWarn("SSL_SendCertificateRequest");
                            break;
                        }
                        rv = SSL_ForceHandshake(ssl_sock);
                        if (rv != SECSuccess) {
                            errWarn("SSL_ForceHandshake");
                            break;
                        }
                    } else {
                        rv = SSL_ReHandshake(ssl_sock, PR_TRUE);
                        if (rv != 0) {
                            errWarn("SSL_ReHandshake");
                            break;
                        }
                        rv = SSL_ForceHandshake(ssl_sock);
                        if (rv < 0) {
                            errWarn("SSL_ForceHandshake");
                            break;
                        }
                    }
                }
            }

            numIOVs = 0;

            iovs[numIOVs].iov_base = (char *)outHeader;
            iovs[numIOVs].iov_len = (sizeof(outHeader)) - 1;
            numIOVs++;

            if (local_file_fd) {
                PRInt32 bytes;
                int errLen;
                if (!PL_strlen(proto) || !PL_strcmp(proto, "file")) {
                    bytes = PR_TransmitFile(ssl_sock, local_file_fd, outHeader,
                                            sizeof outHeader - 1,
                                            PR_TRANSMITFILE_KEEP_OPEN,
                                            PR_INTERVAL_NO_TIMEOUT);
                    if (bytes >= 0) {
                        bytes -= sizeof outHeader - 1;
                        FPRINTF(stderr,
                                "selfserv: PR_TransmitFile wrote %d bytes from %s\n",
                                bytes, fileName);
                        break;
                    }
                    errString = errWarn("PR_TransmitFile");
                    errLen = PORT_Strlen(errString);
                    errLen = PR_MIN(errLen, sizeof msgBuf - 1);
                    PORT_Memcpy(msgBuf, errString, errLen);
                    msgBuf[errLen] = 0;

                    iovs[numIOVs].iov_base = msgBuf;
                    iovs[numIOVs].iov_len = PORT_Strlen(msgBuf);
                    numIOVs++;
                }
                if (!PL_strcmp(proto, "crl")) {
                    if (reload_crl(local_file_fd) == SECFailure) {
                        errString = errWarn("CERT_CacheCRL");
                        if (!errString)
                            errString = "Unknow error";
                        PR_snprintf(msgBuf, sizeof(msgBuf), "%s%s ",
                                    crlCacheErr, errString);

                        iovs[numIOVs].iov_base = msgBuf;
                        iovs[numIOVs].iov_len = PORT_Strlen(msgBuf);
                        numIOVs++;
                    } else {
                        FPRINTF(stderr,
                                "selfserv: CRL %s reloaded.\n",
                                fileName);
                        break;
                    }
                }
            } else if (reqLen <= 0) { /* hit eof */
                snprintf(msgBuf, sizeof(msgBuf), "Get or Post incomplete after %d bytes.\r\n",
                         bufDat);

                iovs[numIOVs].iov_base = msgBuf;
                iovs[numIOVs].iov_len = PORT_Strlen(msgBuf);
                numIOVs++;
            } else if (reqLen < bufDat) {
                snprintf(msgBuf, sizeof(msgBuf), "Discarded %d characters.\r\n",
                         bufDat - reqLen);

                iovs[numIOVs].iov_base = msgBuf;
                iovs[numIOVs].iov_len = PORT_Strlen(msgBuf);
                numIOVs++;
            }

            if (reqLen > 0) {
                if (verbose > 1)
                    fwrite(buf, 1, reqLen, stdout); /* display it */

                iovs[numIOVs].iov_base = buf;
                iovs[numIOVs].iov_len = reqLen;
                numIOVs++;
            }

            /* Don't add the EOF if we want to test bulk encryption */
            if (!testBulk) {
                iovs[numIOVs].iov_base = (char *)EOFmsg;
                iovs[numIOVs].iov_len = sizeof EOFmsg - 1;
                numIOVs++;
            }

            rv = PR_Writev(ssl_sock, iovs, numIOVs, PR_INTERVAL_NO_TIMEOUT);
            if (rv < 0) {
                errWarn("PR_Writev");
                break;
            }

            /* Send testBulkTotal chunks to the client. Unlimited if 0. */
            if (testBulk) {
                while (0 < (rv = PR_Write(ssl_sock, testBulkBuf, testBulkSize))) {
                    PR_ATOMIC_ADD(&loggerBytes, rv);
                    PR_ATOMIC_INCREMENT(&bulkSentChunks);
                    if ((bulkSentChunks > testBulkTotal) && (testBulkTotal != 0))
                        break;
                }

                /* There was a write error, so close this connection. */
                if (bulkSentChunks <= testBulkTotal) {
                    errWarn("PR_Write");
                }
                PR_ATOMIC_DECREMENT(&loggerOps);
                break;
            }
        } while (0);

cleanup:
    if (ssl_sock) {
        PR_Close(ssl_sock);
    } else if (tcp_sock) {
        PR_Close(tcp_sock);
    }
    if (local_file_fd)
        PR_Close(local_file_fd);
    VLOG(("selfserv: handle_connection: exiting\n"));

    /* do a nice shutdown if asked. */
    if (!strncmp(buf, stopCmd, sizeof stopCmd - 1)) {
        VLOG(("selfserv: handle_connection: stop command"));
        stop_server();
    }
    VLOG(("selfserv: handle_connection: exiting"));
    return SECSuccess; /* success */
}

#ifdef XP_UNIX

void
sigusr1_handler(int sig)
{
    VLOG(("selfserv: sigusr1_handler: stop server"));
    stop_server();
}

#endif

SECStatus
do_accepts(
    PRFileDesc *listen_sock,
    PRFileDesc *model_sock)
{
    PRNetAddr addr;
    PRErrorCode perr;
#ifdef XP_UNIX
    struct sigaction act;
#endif

    VLOG(("selfserv: do_accepts: starting"));
    PR_SetThreadPriority(PR_GetCurrentThread(), PR_PRIORITY_HIGH);

    acceptorThread = PR_GetCurrentThread();
#ifdef XP_UNIX
    /* set up the signal handler */
    act.sa_handler = sigusr1_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGUSR1, &act, NULL)) {
        fprintf(stderr, "Error installing signal handler.\n");
        exit(1);
    }
#endif
    while (!stopping) {
        PRFileDesc *tcp_sock;
        PRCList *myLink;

        FPRINTF(stderr, "\n\n\nselfserv: About to call accept.\n");
        tcp_sock = PR_Accept(listen_sock, &addr, PR_INTERVAL_NO_TIMEOUT);
        if (tcp_sock == NULL) {
            perr = PR_GetError();
            if ((perr != PR_CONNECT_RESET_ERROR &&
                 perr != PR_PENDING_INTERRUPT_ERROR) ||
                verbose) {
                errWarn("PR_Accept");
            }
            if (perr == PR_CONNECT_RESET_ERROR) {
                FPRINTF(stderr,
                        "Ignoring PR_CONNECT_RESET_ERROR error - continue\n");
                continue;
            }
            stopping = 1;
            break;
        }

        VLOG(("selfserv: do_accept: Got connection\n"));

        if (logStats) {
            PR_ATOMIC_INCREMENT(&loggerOps);
        }

        PZ_Lock(qLock);
        while (PR_CLIST_IS_EMPTY(&freeJobs) && !stopping) {
            PZ_WaitCondVar(freeListNotEmptyCv, PR_INTERVAL_NO_TIMEOUT);
        }
        if (stopping) {
            PZ_Unlock(qLock);
            if (tcp_sock) {
                PR_Close(tcp_sock);
            }
            break;
        }
        myLink = PR_LIST_HEAD(&freeJobs);
        PR_REMOVE_AND_INIT_LINK(myLink);
        /* could release qLock here and reaquire it 7 lines below, but
        ** why bother for 4 assignment statements?
        */
        {
            JOB *myJob = (JOB *)myLink;
            myJob->tcp_sock = tcp_sock;
            myJob->model_sock = model_sock;
        }

        PR_APPEND_LINK(myLink, &jobQ);
        PZ_NotifyCondVar(jobQNotEmptyCv);
        PZ_Unlock(qLock);
    }

    FPRINTF(stderr, "selfserv: Closing listen socket.\n");
    VLOG(("selfserv: do_accepts: exiting"));
    if (listen_sock) {
        PR_Close(listen_sock);
    }
    return SECSuccess;
}

PRFileDesc *
getBoundListenSocket(unsigned short port)
{
    PRFileDesc *listen_sock = NULL;
    int listenQueueDepth = 5 + (2 * maxThreads);
    PRStatus prStatus;
    PRNetAddr addr;
    PRSocketOptionData opt;

    // We want to listen on the IP family that tstclnt will use.
    // tstclnt uses PR_GetPrefLoopbackAddrInfo to decide, if it's
    // asked to connect to localhost.

    prStatus = PR_GetPrefLoopbackAddrInfo(&addr, port);
    if (prStatus == PR_FAILURE) {
        addr.inet.family = PR_AF_INET;
        addr.inet.ip = PR_htonl(PR_INADDR_ANY);
        addr.inet.port = PR_htons(port);
    }

    if (addr.inet.family == PR_AF_INET6) {
        listen_sock = PR_OpenTCPSocket(PR_AF_INET6);
    } else if (addr.inet.family == PR_AF_INET) {
        listen_sock = PR_NewTCPSocket();
    }
    if (listen_sock == NULL) {
        errExit("Couldn't create socket");
    }

    opt.option = PR_SockOpt_Nonblocking;
    opt.value.non_blocking = PR_FALSE;
    prStatus = PR_SetSocketOption(listen_sock, &opt);
    if (prStatus < 0) {
        PR_Close(listen_sock);
        errExit("PR_SetSocketOption(PR_SockOpt_Nonblocking)");
    }

    opt.option = PR_SockOpt_Reuseaddr;
    opt.value.reuse_addr = PR_TRUE;
    prStatus = PR_SetSocketOption(listen_sock, &opt);
    if (prStatus < 0) {
        PR_Close(listen_sock);
        errExit("PR_SetSocketOption(PR_SockOpt_Reuseaddr)");
    }

    /* Set PR_SockOpt_Linger because it helps prevent a server bind issue
     * after clean shutdown . See bug 331413 .
     */
    opt.option = PR_SockOpt_Linger;
    opt.value.linger.polarity = PR_TRUE;
    opt.value.linger.linger = PR_SecondsToInterval(1);
    prStatus = PR_SetSocketOption(listen_sock, &opt);
    if (prStatus < 0) {
        PR_Close(listen_sock);
        errExit("PR_SetSocketOption(PR_SockOpt_Linger)");
    }

    prStatus = PR_Bind(listen_sock, &addr);
    if (prStatus < 0) {
        PR_Close(listen_sock);
        errExit("PR_Bind");
    }

    prStatus = PR_Listen(listen_sock, listenQueueDepth);
    if (prStatus < 0) {
        PR_Close(listen_sock);
        errExit("PR_Listen");
    }
    return listen_sock;
}

PRInt32 PR_CALLBACK
logWritev(
    PRFileDesc *fd,
    const PRIOVec *iov,
    PRInt32 size,
    PRIntervalTime timeout)
{
    PRInt32 rv = (fd->lower->methods->writev)(fd->lower, iov, size,
                                              timeout);
    /* Add the amount written, but not if there's an error */
    if (rv > 0)
        PR_ATOMIC_ADD(&loggerBytesTCP, rv);
    return rv;
}

PRInt32 PR_CALLBACK
logWrite(
    PRFileDesc *fd,
    const void *buf,
    PRInt32 amount)
{
    PRInt32 rv = (fd->lower->methods->write)(fd->lower, buf, amount);
    /* Add the amount written, but not if there's an error */
    if (rv > 0)
        PR_ATOMIC_ADD(&loggerBytesTCP, rv);

    return rv;
}

PRInt32 PR_CALLBACK
logSend(
    PRFileDesc *fd,
    const void *buf,
    PRInt32 amount,
    PRIntn flags,
    PRIntervalTime timeout)
{
    PRInt32 rv = (fd->lower->methods->send)(fd->lower, buf, amount,
                                            flags, timeout);
    /* Add the amount written, but not if there's an error */
    if (rv > 0)
        PR_ATOMIC_ADD(&loggerBytesTCP, rv);
    return rv;
}

void
initLoggingLayer(void)
{
    /* get a new layer ID */
    log_layer_id = PR_GetUniqueIdentity("Selfserv Logging");
    if (log_layer_id == PR_INVALID_IO_LAYER)
        errExit("PR_GetUniqueIdentity");

    /* setup the default IO methods with my custom write methods */
    memcpy(&loggingMethods, PR_GetDefaultIOMethods(), sizeof(PRIOMethods));
    loggingMethods.writev = logWritev;
    loggingMethods.write = logWrite;
    loggingMethods.send = logSend;
}

void
handshakeCallback(PRFileDesc *fd, void *client_data)
{
    const char *handshakeName = (const char *)client_data;
    if (handshakeName && !failedToNegotiateName) {
        SECItem *hostInfo = SSL_GetNegotiatedHostInfo(fd);
        if (!hostInfo || PORT_Strncmp(handshakeName, (char *)hostInfo->data,
                                      hostInfo->len)) {
            failedToNegotiateName = PR_TRUE;
        }
        if (hostInfo) {
            SECITEM_FreeItem(hostInfo, PR_TRUE);
        }
    }
    if (enabledExporters) {
        SECStatus rv = exportKeyingMaterials(fd, enabledExporters, enabledExporterCount);
        if (rv != SECSuccess) {
            PRErrorCode err = PR_GetError();
            fprintf(stderr,
                    "couldn't export keying material: %s\n",
                    SECU_Strerror(err));
        }
    }
}

static SECStatus
importPsk(PRFileDesc *model_sock)
{
    SECU_PrintAsHex(stdout, &psk, "Using External PSK", 0);
    PK11SlotInfo *slot = NULL;
    PK11SymKey *symKey = NULL;
    slot = PK11_GetInternalSlot();
    if (!slot) {
        errWarn("PK11_GetInternalSlot failed");
        return SECFailure;
    }
    symKey = PK11_ImportSymKey(slot, CKM_HKDF_KEY_GEN, PK11_OriginUnwrap,
                               CKA_DERIVE, &psk, NULL);
    PK11_FreeSlot(slot);
    if (!symKey) {
        errWarn("PK11_ImportSymKey failed\n");
        return SECFailure;
    }

    SECStatus rv = SSL_AddExternalPsk(model_sock, symKey,
                                      (const PRUint8 *)pskLabel.data,
                                      pskLabel.len, ssl_hash_sha256);
    PK11_FreeSymKey(symKey);
    return rv;
}

static SECStatus
configureEchWithPublicName(PRFileDesc *model_sock, const char *public_name)
{
    SECStatus rv;

#define OID_LEN 65
    unsigned char paramBuf[OID_LEN];
    SECItem ecParams = { siBuffer, paramBuf, sizeof(paramBuf) };
    SECKEYPublicKey *pubKey = NULL;
    SECKEYPrivateKey *privKey = NULL;
    SECOidData *oidData;
    char *echConfigBase64 = NULL;
    PRUint8 configId = 0;
    PRUint8 configBuf[1000];
    unsigned int len = 0;
    HpkeSymmetricSuite echCipherSuite = { HpkeKdfHkdfSha256,
                                          HpkeAeadChaCha20Poly1305 };

    PK11SlotInfo *slot = PK11_GetInternalKeySlot();
    if (!slot) {
        errWarn("PK11_GetInternalKeySlot failed");
        return SECFailure;
    }

    if (PK11_GenerateRandom(&configId, sizeof(configId)) != SECSuccess) {
        errWarn("Failed to generate random configId");
        goto loser;
    }

    oidData = SECOID_FindOIDByTag(SEC_OID_CURVE25519);
    if (oidData && (2 + oidData->oid.len) < sizeof(paramBuf)) {
        ecParams.data[0] = SEC_ASN1_OBJECT_ID;
        ecParams.data[1] = oidData->oid.len;
        memcpy(ecParams.data + 2, oidData->oid.data, oidData->oid.len);
        ecParams.len = oidData->oid.len + 2;
    } else {
        errWarn("SECOID_FindOIDByTag failed");
        goto loser;
    }
    privKey = PK11_GenerateKeyPair(slot, CKM_EC_KEY_PAIR_GEN, &ecParams,
                                   &pubKey, PR_FALSE, PR_FALSE, NULL);
    if (!privKey || !pubKey) {
        errWarn("Failed to generate ECH keypair");
        goto loser;
    }

    rv = SSL_EncodeEchConfigId(configId, public_name, 100,
                               HpkeDhKemX25519Sha256, pubKey,
                               &echCipherSuite, 1,
                               configBuf, &len, sizeof(configBuf));
    if (rv != SECSuccess) {
        errWarn("SSL_EncodeEchConfigId failed");
        goto loser;
    }

    rv = SSL_SetServerEchConfigs(model_sock, pubKey, privKey, configBuf, len);
    if (rv != SECSuccess) {
        errWarn("SSL_SetServerEchConfigs failed");
        goto loser;
    }

    SECItem echConfigItem = { siBuffer, configBuf, len };
    echConfigBase64 = NSSBase64_EncodeItem(NULL, NULL, 0, &echConfigItem);
    if (!echConfigBase64) {
        errWarn("NSSBase64_EncodeItem failed");
        goto loser;
    }

    // Remove the newline characters that NSSBase64_EncodeItem unhelpfully inserts.
    char *newline = strstr(echConfigBase64, "\r\n");
    if (newline) {
        memmove(newline, newline + 2, strlen(newline + 2) + 1);
    }

    printf("%s\n", echConfigBase64);
    PORT_Free(echConfigBase64);
    SECKEY_DestroyPrivateKey(privKey);
    SECKEY_DestroyPublicKey(pubKey);
    PK11_FreeSlot(slot);
    return SECSuccess;

loser:
    PORT_Free(echConfigBase64);
    SECKEY_DestroyPrivateKey(privKey);
    SECKEY_DestroyPublicKey(pubKey);
    PK11_FreeSlot(slot);
    return SECFailure;
}

static SECStatus
configureEchWithData(PRFileDesc *model_sock)
{
    /* The input should be a Base64-encoded ECHKey struct:
     *  struct {
     *     opaque pkcs8_ech_keypair<0..2^16-1>;
     *     ECHConfigs configs<0..2^16>; // draft-ietf-tls-esni-09
     * } ECHKey;
     *
     * This is not a standardized format, rather it's designed for
     * interoperability with https://github.com/xvzcf/tls-interop-runner.
     * It is the user's responsibility to ensure that the PKCS8 keypair
     * corresponds to the public key embedded in the ECHConfigs.
     */

#define REMAINING_BYTES(rdr, buf) \
    buf->len - (rdr - buf->data)

    SECStatus rv;
    size_t len;
    unsigned char *reader;
    PK11SlotInfo *slot = NULL;
    SECItem *decoded = NULL;
    SECKEYPublicKey *pk = NULL;
    SECKEYPrivateKey *sk = NULL;
    SECItem pkcs8Key = { siBuffer, NULL, 0 };

    decoded = NSSBase64_DecodeBuffer(NULL, NULL, echParamsStr, PORT_Strlen(echParamsStr));
    if (!decoded || decoded->len < 2) {
        errWarn("Couldn't decode ECHParams");
        goto loser;
    };
    reader = decoded->data;

    len = (*(reader++) << 8);
    len |= *(reader++);
    if (len > (REMAINING_BYTES(reader, decoded) - 2)) {
        errWarn("Bad ECHParams encoding");
        goto loser;
    }
    pkcs8Key.data = reader;
    pkcs8Key.len = len;
    reader += len;

    /* Convert the key bytes to key handles */
    slot = PK11_GetInternalKeySlot();
    rv = PK11_ImportDERPrivateKeyInfoAndReturnKey(
        slot, &pkcs8Key, NULL, NULL, PR_FALSE, PR_FALSE, KU_ALL, &sk, NULL);
    if (rv != SECSuccess || !sk) {
        errWarn("ECH key import failed");
        goto loser;
    }
    pk = SECKEY_ConvertToPublicKey(sk);
    if (!pk) {
        errWarn("ECH key conversion failed");
        goto loser;
    }

    /* Remainder is the ECHConfigs. */
    rv = SSL_SetServerEchConfigs(model_sock, pk, sk, reader,
                                 REMAINING_BYTES(reader, decoded));
    if (rv != SECSuccess) {
        errWarn("SSL_SetServerEchConfigs failed");
        goto loser;
    }

    PK11_FreeSlot(slot);
    SECKEY_DestroyPrivateKey(sk);
    SECKEY_DestroyPublicKey(pk);
    SECITEM_FreeItem(decoded, PR_TRUE);
    return SECSuccess;
loser:
    if (slot) {
        PK11_FreeSlot(slot);
    }
    SECKEY_DestroyPrivateKey(sk);
    SECKEY_DestroyPublicKey(pk);
    SECITEM_FreeItem(decoded, PR_TRUE);
    return SECFailure;
}

static SECStatus
configureEch(PRFileDesc *model_sock)
{
    if (!PORT_Strncmp(echParamsStr, "publicname:", PORT_Strlen("publicname:"))) {
        return configureEchWithPublicName(model_sock,
                                          &echParamsStr[PORT_Strlen("publicname:")]);
    }
    return configureEchWithData(model_sock);
}

static SECStatus
zlibCertificateDecode(const SECItem *input,
                      unsigned char *output, size_t outputLen,
                      size_t *usedLen)
{
    if (!input || !input->data || input->len == 0 || !output || outputLen == 0) {
        PR_SetError(SEC_ERROR_INVALID_ARGS, 0);
        return SECFailure;
    }

    *usedLen = outputLen;

    int ret = uncompress(output, (unsigned long *)usedLen, input->data, input->len);
    if (ret != Z_OK) {
        PR_SetError(SEC_ERROR_BAD_DATA, 0);
        return SECFailure;
    }

    return SECSuccess;
}

static SECStatus
zlibCertificateEncode(const SECItem *input, SECItem *output)
{
    if (!input || !input->data || input->len == 0 || !output) {
        PR_SetError(SEC_ERROR_INVALID_ARGS, 0);
        return SECFailure;
    }

    unsigned long maxCompressedLen = compressBound(input->len);
    SECITEM_AllocItem(NULL, output, maxCompressedLen);

    int ret = compress(output->data, (unsigned long *)&output->len, input->data, input->len);
    if (ret != Z_OK) {
        PR_SetError(SEC_ERROR_LIBRARY_FAILURE, 0);
        return SECFailure;
    }

    return SECSuccess;
}

static SECStatus
configureZlibCompression(PRFileDesc *model_sock)
{
    SSLCertificateCompressionAlgorithm zlibAlg = { 1, "zlib",
                                                   zlibCertificateEncode,
                                                   zlibCertificateDecode };

    return SSL_SetCertificateCompressionAlgorithm(model_sock, zlibAlg);
}

void
server_main(
    PRFileDesc *listen_sock,
    SECKEYPrivateKey **privKey,
    CERTCertificate **cert,
    const char *expectedHostNameVal)
{
    int i;
    PRFileDesc *model_sock = NULL;
    int rv;
    SECStatus secStatus;

    if (useModelSocket) {
        model_sock = PR_NewTCPSocket();
        if (model_sock == NULL) {
            errExit("PR_NewTCPSocket on model socket");
        }
        model_sock = SSL_ImportFD(NULL, model_sock);
        if (model_sock == NULL) {
            errExit("SSL_ImportFD");
        }
    } else {
        model_sock = listen_sock = SSL_ImportFD(NULL, listen_sock);
        if (listen_sock == NULL) {
            errExit("SSL_ImportFD");
        }
    }

    /* do SSL configuration. */
    rv = SSL_OptionSet(model_sock, SSL_SECURITY, enabledVersions.min != 0);
    if (rv < 0) {
        errExit("SSL_OptionSet SSL_SECURITY");
    }

    rv = SSL_VersionRangeSet(model_sock, &enabledVersions);
    if (rv != SECSuccess) {
        errExit("error setting SSL/TLS version range ");
    }

    rv = SSL_OptionSet(model_sock, SSL_ROLLBACK_DETECTION, !disableRollBack);
    if (rv != SECSuccess) {
        errExit("error enabling RollBack detection ");
    }
    if (disableLocking) {
        rv = SSL_OptionSet(model_sock, SSL_NO_LOCKS, PR_TRUE);
        if (rv != SECSuccess) {
            errExit("error disabling SSL socket locking ");
        }
    }
    if (enableSessionTickets) {
        rv = SSL_OptionSet(model_sock, SSL_ENABLE_SESSION_TICKETS, PR_TRUE);
        if (rv != SECSuccess) {
            errExit("error enabling Session Ticket extension ");
        }
    }

    if (enableZlibCertificateCompression) {
        rv = configureZlibCompression(model_sock);
        if (rv != SECSuccess) {
            errExit("error enabling Zlib Certificate Compression");
        }
    }

    if (virtServerNameIndex > 1) {
        rv = SSL_SNISocketConfigHook(model_sock, mySSLSNISocketConfig,
                                     (void *)&virtServerNameArray);
        if (rv != SECSuccess) {
            errExit("error enabling SNI extension ");
        }
    }

    if (configureDHE > -1) {
        rv = SSL_OptionSet(model_sock, SSL_ENABLE_SERVER_DHE, (configureDHE > 0));
        if (rv != SECSuccess) {
            errExit("error configuring server side DHE support");
        }
        rv = SSL_OptionSet(model_sock, SSL_REQUIRE_DH_NAMED_GROUPS, (configureDHE > 1));
        if (rv != SECSuccess) {
            errExit("error configuring server side FFDHE support");
        }
        PORT_Assert(configureDHE <= 2);
    }

    if (configureReuseECDHE > -1) {
        rv = SSL_OptionSet(model_sock, SSL_REUSE_SERVER_ECDHE_KEY, (configureReuseECDHE > 0));
        if (rv != SECSuccess) {
            errExit("error configuring server side reuse of ECDHE key");
        }
    }

    if (configureWeakDHE > -1) {
        rv = SSL_EnableWeakDHEPrimeGroup(model_sock, (configureWeakDHE > 0));
        if (rv != SECSuccess) {
            errExit("error configuring weak DHE prime group");
        }
    }

    if (enableExtendedMasterSecret) {
        rv = SSL_OptionSet(model_sock, SSL_ENABLE_EXTENDED_MASTER_SECRET, PR_TRUE);
        if (rv != SECSuccess) {
            errExit("error enabling extended master secret ");
        }
    }

    /* This uses the legacy certificate API.  See mySSLSNISocketConfig() for the
     * new, prefered API. */
    for (i = 0; i < certNicknameIndex; i++) {
        if (cert[i] != NULL) {
            const SSLExtraServerCertData ocspData = {
                ssl_auth_null, NULL, certStatus[i], NULL, NULL, NULL
            };

            secStatus = SSL_ConfigServerCert(model_sock, cert[i],
                                             privKey[i], &ocspData,
                                             sizeof(ocspData));
            if (secStatus != SECSuccess)
                errExit("SSL_ConfigServerCert");
        }
    }

    if (bigBuf.data) { /* doing FDX */
        rv = SSL_OptionSet(model_sock, SSL_ENABLE_FDX, 1);
        if (rv < 0) {
            errExit("SSL_OptionSet SSL_ENABLE_FDX");
        }
    }

    if (NoReuse) {
        rv = SSL_OptionSet(model_sock, SSL_NO_CACHE, 1);
        if (rv < 0) {
            errExit("SSL_OptionSet SSL_NO_CACHE");
        }
    }

    if (zeroRTT) {
        if (enabledVersions.max < SSL_LIBRARY_VERSION_TLS_1_3) {
            errExit("You tried enabling 0RTT without enabling TLS 1.3!");
        }
        rv = SSL_SetAntiReplayContext(model_sock, antiReplay);
        if (rv != SECSuccess) {
            errExit("error configuring anti-replay ");
        }
        rv = SSL_OptionSet(model_sock, SSL_ENABLE_0RTT_DATA, PR_TRUE);
        if (rv != SECSuccess) {
            errExit("error enabling 0RTT ");
        }
    }

    if (enablePostHandshakeAuth) {
        if (enabledVersions.max < SSL_LIBRARY_VERSION_TLS_1_3) {
            errExit("You tried enabling post-handshake auth without enabling TLS 1.3!");
        }
        rv = SSL_OptionSet(model_sock, SSL_ENABLE_POST_HANDSHAKE_AUTH, PR_TRUE);
        if (rv != SECSuccess) {
            errExit("error enabling post-handshake auth");
        }
    }

    if (enableALPN) {
        PRUint8 alpnVal[] = { 0x08,
                              0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31 };
        rv = SSL_OptionSet(model_sock, SSL_ENABLE_ALPN, PR_TRUE);
        if (rv != SECSuccess) {
            errExit("error enabling ALPN");
        }

        rv = SSL_SetNextProtoNego(model_sock, alpnVal, sizeof(alpnVal));
        if (rv != SECSuccess) {
            errExit("error enabling ALPN");
        }
    }

    if (enabledGroups) {
        rv = SSL_NamedGroupConfig(model_sock, enabledGroups, enabledGroupsCount);
        if (rv < 0) {
            errExit("SSL_NamedGroupConfig failed");
        }
    }

    if (enabledSigSchemes) {
        rv = SSL_SignatureSchemePrefSet(model_sock, enabledSigSchemes, enabledSigSchemeCount);
        if (rv < 0) {
            errExit("SSL_SignatureSchemePrefSet failed");
        }
    }

    /* This cipher is not on by default. The Acceptance test
     * would like it to be. Turn this cipher on.
     */

    secStatus = SSL_CipherPrefSetDefault(TLS_RSA_WITH_NULL_MD5, PR_TRUE);
    if (secStatus != SECSuccess) {
        errExit("SSL_CipherPrefSetDefault:TLS_RSA_WITH_NULL_MD5");
    }

    if (expectedHostNameVal || enabledExporters) {
        SSL_HandshakeCallback(model_sock, handshakeCallback,
                              (void *)expectedHostNameVal);
    }

    if (requestCert) {
        SSL_AuthCertificateHook(model_sock, mySSLAuthCertificate,
                                (void *)CERT_GetDefaultCertDB());
        if (requestCert <= 2) {
            rv = SSL_OptionSet(model_sock, SSL_REQUEST_CERTIFICATE, 1);
            if (rv < 0) {
                errExit("first SSL_OptionSet SSL_REQUEST_CERTIFICATE");
            }
            rv = SSL_OptionSet(model_sock, SSL_REQUIRE_CERTIFICATE,
                               (requestCert == 2));
            if (rv < 0) {
                errExit("first SSL_OptionSet SSL_REQUIRE_CERTIFICATE");
            }
        }
    }

    if (psk.data) {
        rv = importPsk(model_sock);
        if (rv != SECSuccess) {
            errExit("importPsk failed");
        }
    }

    if (echParamsStr) {
        rv = configureEch(model_sock);
        if (rv != SECSuccess) {
            errExit("configureEch failed");
        }
    }

    if (MakeCertOK)
        SSL_BadCertHook(model_sock, myBadCertHandler, NULL);

    /* end of ssl configuration. */

    /* Now, do the accepting, here in the main thread. */
    rv = do_accepts(listen_sock, model_sock);

    terminateWorkerThreads();

    if (useModelSocket && model_sock) {
        if (model_sock) {
            PR_Close(model_sock);
        }
    }
}

SECStatus
readBigFile(const char *fileName)
{
    PRFileInfo info;
    PRStatus status;
    SECStatus rv = SECFailure;
    int count;
    int hdrLen;
    PRFileDesc *local_file_fd = NULL;

    status = PR_GetFileInfo(fileName, &info);

    if (status == PR_SUCCESS &&
        info.type == PR_FILE_FILE &&
        info.size > 0 &&
        NULL != (local_file_fd = PR_Open(fileName, PR_RDONLY, 0))) {

        hdrLen = PORT_Strlen(outHeader);
        bigBuf.len = hdrLen + info.size;
        bigBuf.data = PORT_Malloc(bigBuf.len + 4095);
        if (!bigBuf.data) {
            errWarn("PORT_Malloc");
            goto done;
        }

        PORT_Memcpy(bigBuf.data, outHeader, hdrLen);

        count = PR_Read(local_file_fd, bigBuf.data + hdrLen, info.size);
        if (count != info.size) {
            errWarn("PR_Read local file");
            goto done;
        }
        rv = SECSuccess;
    done:
        if (local_file_fd) {
            PR_Close(local_file_fd);
        }
    }
    return rv;
}

int numChildren;
PRProcess *child[MAX_PROCS];

PRProcess *
haveAChild(int argc, char **argv, PRProcessAttr *attr)
{
    PRProcess *newProcess;

    newProcess = PR_CreateProcess(argv[0], argv, NULL, attr);
    if (!newProcess) {
        errWarn("Can't create new process.");
    } else {
        child[numChildren++] = newProcess;
    }
    return newProcess;
}

#ifdef XP_UNIX
void
sigusr1_parent_handler(int sig)
{
    PRProcess *process;
    int i;
    fprintf(stderr, "SIG_USER: Parent got sig_user, killing children (%d).\n", numChildren);
    for (i = 0; i < numChildren; i++) {
        process = child[i];
        PR_KillProcess(process); /* it would be nice to kill with a sigusr signal */
    }
}
#endif

void
beAGoodParent(int argc, char **argv, int maxProcs, PRFileDesc *listen_sock)
{
    PRProcess *newProcess;
    PRProcessAttr *attr;
    int i;
    PRInt32 exitCode;
    PRStatus rv;

#ifdef XP_UNIX
    struct sigaction act;

    /* set up the signal handler */
    act.sa_handler = sigusr1_parent_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGUSR1, &act, NULL)) {
        fprintf(stderr, "Error installing signal handler.\n");
        exit(1);
    }
#endif

    rv = PR_SetFDInheritable(listen_sock, PR_TRUE);
    if (rv != PR_SUCCESS)
        errExit("PR_SetFDInheritable");

    attr = PR_NewProcessAttr();
    if (!attr)
        errExit("PR_NewProcessAttr");

    rv = PR_ProcessAttrSetInheritableFD(attr, listen_sock, inheritableSockName);
    if (rv != PR_SUCCESS)
        errExit("PR_ProcessAttrSetInheritableFD");

    for (i = 0; i < maxProcs; ++i) {
        newProcess = haveAChild(argc, argv, attr);
        if (!newProcess)
            break;
    }

    rv = PR_SetFDInheritable(listen_sock, PR_FALSE);
    if (rv != PR_SUCCESS)
        errExit("PR_SetFDInheritable");

    while (numChildren > 0) {
        newProcess = child[numChildren - 1];
        PR_WaitProcess(newProcess, &exitCode);
        fprintf(stderr, "Child %d exited with exit code %x\n",
                numChildren, exitCode);
        numChildren--;
    }
    exit(0);
}

#define HEXCHAR_TO_INT(c, i)                                              \
    if (((c) >= '0') && ((c) <= '9')) {                                   \
        i = (c) - '0';                                                    \
    } else if (((c) >= 'a') && ((c) <= 'f')) {                            \
        i = (c) - 'a' + 10;                                               \
    } else if (((c) >= 'A') && ((c) <= 'F')) {                            \
        i = (c) - 'A' + 10;                                               \
    } else if ((c) == '\0') {                                             \
        fprintf(stderr, "Invalid length of cipher string (-c :WXYZ).\n"); \
        exit(9);                                                          \
    } else {                                                              \
        fprintf(stderr, "Non-hex char in cipher string (-c :WXYZ).\n");   \
        exit(9);                                                          \
    }

SECStatus
enableOCSPStapling(const char *mode)
{
    if (!strcmp(mode, "good")) {
        ocspStaplingMode = osm_good;
        return SECSuccess;
    }
    if (!strcmp(mode, "unknown")) {
        ocspStaplingMode = osm_unknown;
        return SECSuccess;
    }
    if (!strcmp(mode, "revoked")) {
        ocspStaplingMode = osm_revoked;
        return SECSuccess;
    }
    if (!strcmp(mode, "badsig")) {
        ocspStaplingMode = osm_badsig;
        return SECSuccess;
    }
    if (!strcmp(mode, "corrupted")) {
        ocspStaplingMode = osm_corrupted;
        return SECSuccess;
    }
    if (!strcmp(mode, "failure")) {
        ocspStaplingMode = osm_failure;
        return SECSuccess;
    }
    if (!strcmp(mode, "random")) {
        ocspStaplingMode = osm_random;
        return SECSuccess;
    }
    if (!strcmp(mode, "ocsp")) {
        ocspStaplingMode = osm_ocsp;
        return SECSuccess;
    }
    return SECFailure;
}

int
main(int argc, char **argv)
{
    char *progName = NULL;
    const char *fileName = NULL;
    char *cipherString = NULL;
    const char *dir = ".";
    char *passwd = NULL;
    char *pwfile = NULL;
    const char *pidFile = NULL;
    char *tmp;
    char *envString;
    PRFileDesc *listen_sock;
    CERTCertificate *cert[MAX_CERT_NICKNAME_ARRAY_INDEX] = { NULL };
    SECKEYPrivateKey *privKey[MAX_CERT_NICKNAME_ARRAY_INDEX] = { NULL };
    int optionsFound = 0;
    int maxProcs = 1;
    unsigned short port = 0;
    SECStatus rv = SECSuccess;
    PRStatus prStatus;
    PRBool bindOnly = PR_FALSE;
    PRBool useLocalThreads = PR_FALSE;
    PLOptState *optstate;
    PLOptStatus status;
    PRThread *loggerThread = NULL;
    PRBool debugCache = PR_FALSE; /* bug 90518 */
    char emptyString[] = { "" };
    char *certPrefix = emptyString;
    SSL3Statistics *ssl3stats;
    PRUint32 i;
    secuPWData pwdata = { PW_NONE, 0 };
    char *expectedHostNameVal = NULL;
    PLArenaPool *certStatusArena = NULL;

    tmp = strrchr(argv[0], '/');
    tmp = tmp ? tmp + 1 : argv[0];
    progName = strrchr(tmp, '\\');
    progName = progName ? progName + 1 : tmp;

    PR_Init(PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);
    SSL_VersionRangeGetSupported(ssl_variant_stream, &enabledVersions);

    /* please keep this list of options in ASCII collating sequence.
    ** numbers, then capital letters, then lower case, alphabetical.
    ** XXX: 'B', and 'q' were used in the past but removed
    **      in 3.28, please leave some time before resuing those. */
    optstate = PL_CreateOptState(argc, argv,
                                 "2:A:C:DEGH:I:J:L:M:NP:QRS:T:U:V:W:X:YZa:bc:d:e:f:g:hi:jk:lmn:op:qrst:uvw:x:yz:");
    while ((status = PL_GetNextOpt(optstate)) == PL_OPT_OK) {
        ++optionsFound;
        switch (optstate->option) {
            case '2':
                fileName = optstate->value;
                break;

            case 'A':
                ocspStaplingCA = PORT_Strdup(optstate->value);
                break;

            case 'C':
                if (optstate->value)
                    NumSidCacheEntries = PORT_Atoi(optstate->value);
                break;

            case 'D':
                noDelay = PR_TRUE;
                break;

            case 'E':
                enablePostHandshakeAuth = PR_TRUE;
                break;

            case 'H':
                configureDHE = (PORT_Atoi(optstate->value) != 0);
                break;

            case 'G':
                enableExtendedMasterSecret = PR_TRUE;
                break;

            case 'L':
                logStats = PR_TRUE;
                if (optstate->value == NULL) {
                    logPeriod = 30;
                } else {
                    logPeriod = PORT_Atoi(optstate->value);
                    if (logPeriod <= 0)
                        logPeriod = 30;
                }
                break;

            case 'M':
                maxProcs = PORT_Atoi(optstate->value);
                if (maxProcs < 1)
                    maxProcs = 1;
                if (maxProcs > MAX_PROCS)
                    maxProcs = MAX_PROCS;
                break;

            case 'N':
                NoReuse = PR_TRUE;
                break;

            case 'R':
                disableRollBack = PR_TRUE;
                break;

            case 'S':
                if (certNicknameIndex >= MAX_CERT_NICKNAME_ARRAY_INDEX) {
                    Usage(progName);
                    break;
                }
                certNicknameArray[certNicknameIndex++] = PORT_Strdup(optstate->value);
                break;

            case 'T':
                if (enableOCSPStapling(optstate->value) != SECSuccess) {
                    fprintf(stderr, "Invalid OCSP stapling mode.\n");
                    fprintf(stderr, "Run '%s -h' for usage information.\n", progName);
                    exit(53);
                }
                break;

            case 'U':
                configureReuseECDHE = (PORT_Atoi(optstate->value) != 0);
                break;

            case 'V':
                if (SECU_ParseSSLVersionRangeString(optstate->value,
                                                    enabledVersions, &enabledVersions) !=
                    SECSuccess) {
                    fprintf(stderr, "Bad version specified.\n");
                    Usage(progName);
                    exit(1);
                }
                break;

            case 'W':
                configureWeakDHE = (PORT_Atoi(optstate->value) != 0);
                break;

            case 'Y':
                PrintCipherUsage(progName);
                exit(0);
                break;

            case 'a':
                if (virtServerNameIndex >= MAX_VIRT_SERVER_NAME_ARRAY_INDEX) {
                    Usage(progName);
                    break;
                }
                virtServerNameArray[virtServerNameIndex++] =
                    PORT_Strdup(optstate->value);
                break;

            case 'b':
                bindOnly = PR_TRUE;
                break;

            case 'c':
                cipherString = PORT_Strdup(optstate->value);
                break;

            case 'd':
                dir = optstate->value;
                break;

            case 'e':
                if (certNicknameIndex >= MAX_CERT_NICKNAME_ARRAY_INDEX) {
                    Usage(progName);
                    break;
                }
                certNicknameArray[certNicknameIndex++] = PORT_Strdup(optstate->value);
                break;

            case 'f':
                pwdata.source = PW_FROMFILE;
                pwdata.data = pwfile = PORT_Strdup(optstate->value);
                break;

            case 'g':
                testBulk = PR_TRUE;
                testBulkTotal = PORT_Atoi(optstate->value);
                break;

            case 'h':
                Usage(progName);
                exit(0);
                break;

            case 'i':
                pidFile = optstate->value;
                break;

            case 'j':
                initLoggingLayer();
                loggingLayer = PR_TRUE;
                break;

            case 'k':
                expectedHostNameVal = PORT_Strdup(optstate->value);
                break;

            case 'l':
                useLocalThreads = PR_TRUE;
                break;

            case 'm':
                useModelSocket = PR_TRUE;
                break;

            case 'n':
                if (certNicknameIndex >= MAX_CERT_NICKNAME_ARRAY_INDEX) {
                    Usage(progName);
                    break;
                }
                certNicknameArray[certNicknameIndex++] = PORT_Strdup(optstate->value);
                virtServerNameArray[0] = PORT_Strdup(optstate->value);
                break;

            case 'P':
                certPrefix = PORT_Strdup(optstate->value);
                break;

            case 'o':
                MakeCertOK = 1;
                break;

            case 'p':
                port = PORT_Atoi(optstate->value);
                break;

            case 'q':
                enableZlibCertificateCompression = PR_TRUE;
                break;

            case 'r':
                ++requestCert;
                break;

            case 's':
                disableLocking = PR_TRUE;
                break;

            case 't':
                maxThreads = PORT_Atoi(optstate->value);
                if (maxThreads > MAX_THREADS)
                    maxThreads = MAX_THREADS;
                if (maxThreads < MIN_THREADS)
                    maxThreads = MIN_THREADS;
                break;

            case 'u':
                enableSessionTickets = PR_TRUE;
                break;

            case 'v':
                verbose++;
                break;

            case 'w':
                pwdata.source = PW_PLAINTEXT;
                pwdata.data = passwd = PORT_Strdup(optstate->value);
                break;

            case 'y':
                debugCache = PR_TRUE;
                break;

            case 'Z':
                zeroRTT = PR_TRUE;
                break;

            case 'z':
                rv = readPSK(optstate->value, &psk, &pskLabel);
                if (rv != SECSuccess) {
                    PL_DestroyOptState(optstate);
                    fprintf(stderr, "Bad PSK specified.\n");
                    Usage(progName);
                    exit(1);
                }
                break;

            case 'Q':
                enableALPN = PR_TRUE;
                break;

            case 'I':
                rv = parseGroupList(optstate->value, &enabledGroups, &enabledGroupsCount);
                if (rv != SECSuccess) {
                    PL_DestroyOptState(optstate);
                    fprintf(stderr, "Bad group specified.\n");
                    fprintf(stderr, "Run '%s -h' for usage information.\n", progName);
                    exit(5);
                }
                break;

            case 'J':
                rv = parseSigSchemeList(optstate->value, &enabledSigSchemes, &enabledSigSchemeCount);
                if (rv != SECSuccess) {
                    PL_DestroyOptState(optstate);
                    fprintf(stderr, "Bad signature scheme specified.\n");
                    fprintf(stderr, "Run '%s -h' for usage information.\n", progName);
                    exit(5);
                }
                break;

            case 'x':
                rv = parseExporters(optstate->value,
                                    &enabledExporters, &enabledExporterCount);
                if (rv != SECSuccess) {
                    PL_DestroyOptState(optstate);
                    fprintf(stderr, "Bad exporter specified.\n");
                    fprintf(stderr, "Run '%s -h' for usage information.\n", progName);
                    exit(5);
                }
                break;

            case 'X':
                echParamsStr = PORT_Strdup(optstate->value);
                if (echParamsStr == NULL) {
                    PL_DestroyOptState(optstate);
                    fprintf(stderr, "echParamsStr copy failed.\n");
                    exit(5);
                }
                break;
            default:
            case '?':
                fprintf(stderr, "Unrecognized or bad option specified: %c\n", optstate->option);
                fprintf(stderr, "Run '%s -h' for usage information.\n", progName);
                exit(4);
                break;
        }
    }
    PL_DestroyOptState(optstate);
    if (status == PL_OPT_BAD) {
        fprintf(stderr, "Unrecognized or bad option specified.\n");
        fprintf(stderr, "Run '%s -h' for usage information.\n", progName);
        exit(5);
    }
    if (!optionsFound) {
        Usage(progName);
        exit(51);
    }
    switch (ocspStaplingMode) {
        case osm_good:
        case osm_revoked:
        case osm_unknown:
        case osm_random:
            if (!ocspStaplingCA) {
                fprintf(stderr, "Selected stapling response requires the -A parameter.\n");
                fprintf(stderr, "Run '%s -h' for usage information.\n", progName);
                exit(52);
            }
            break;
        default:
            break;
    }

    /* The -b (bindOnly) option is only used by the ssl.sh test
     * script on Linux to determine whether a previous selfserv
     * process has fully died and freed the port.  (Bug 129701)
     */
    if (bindOnly) {
        listen_sock = getBoundListenSocket(port);
        if (!listen_sock) {
            exit(1);
        }
        if (listen_sock) {
            PR_Close(listen_sock);
        }
        exit(0);
    }

    if (certNicknameIndex == 0) {
        fprintf(stderr, "Must specify at least one certificate nickname using '-n' (RSA), '-S' (DSA), or 'e' (EC).\n");
        fprintf(stderr, "Run '%s -h' for usage information.\n", progName);
        exit(6);
    }

    if (port == 0) {
        fprintf(stderr, "Required argument 'port' must be non-zero value\n");
        exit(7);
    }

    if (NoReuse && maxProcs > 1) {
        fprintf(stderr, "-M and -N options are mutually exclusive.\n");
        exit(14);
    }

    envString = PR_GetEnvSecure(envVarName);
    if (!envString && pidFile) {
        FILE *tmpfile = fopen(pidFile, "w+");

        if (tmpfile) {
            fprintf(tmpfile, "%d", getpid());
            fclose(tmpfile);
        }
    }

    /* allocate and initialize app data for bulk encryption testing */
    if (testBulk) {
        testBulkBuf = PORT_Malloc(testBulkSize);
        if (testBulkBuf == NULL)
            errExit("Out of memory: testBulkBuf");
        for (i = 0; i < testBulkSize; i++)
            testBulkBuf[i] = i;
    }

    envString = PR_GetEnvSecure(envVarName);
    tmp = PR_GetEnvSecure("TMP");
    if (!tmp)
        tmp = PR_GetEnvSecure("TMPDIR");
    if (!tmp)
        tmp = PR_GetEnvSecure("TEMP");

    if (envString) {
        /* we're one of the children in a multi-process server. */
        listen_sock = PR_GetInheritedFD(inheritableSockName);
        if (!listen_sock)
            errExit("PR_GetInheritedFD");
#ifndef WINNT
        /* we can't do this on NT because it breaks NSPR and
    PR_Accept will fail on the socket in the child process if
    the socket state is change to non inheritable
    It is however a security issue to leave it accessible,
    but it is OK for a test server such as selfserv.
    NSPR should fix it eventually . see bugzilla 101617
    and 102077
    */
        prStatus = PR_SetFDInheritable(listen_sock, PR_FALSE);
        if (prStatus != PR_SUCCESS)
            errExit("PR_SetFDInheritable");
#endif
        rv = SSL_InheritMPServerSIDCache(envString);
        if (rv != SECSuccess)
            errExit("SSL_InheritMPServerSIDCache");
        hasSidCache = PR_TRUE;
        /* Call the NSS initialization routines */
        rv = NSS_Initialize(dir, certPrefix, certPrefix, SECMOD_DB, NSS_INIT_READONLY);
        if (rv != SECSuccess) {
            fputs("NSS_Init failed.\n", stderr);
            exit(8);
        }
    } else if (maxProcs > 1) {
        /* we're going to be the parent in a multi-process server.  */
        listen_sock = getBoundListenSocket(port);
        rv = SSL_ConfigMPServerSIDCache(NumSidCacheEntries, 0, 0, tmp);
        if (rv != SECSuccess)
            errExit("SSL_ConfigMPServerSIDCache");
        hasSidCache = PR_TRUE;
        beAGoodParent(argc, argv, maxProcs, listen_sock);
        exit(99); /* should never get here */
    } else {
        /* Call the NSS initialization routines */
        rv = NSS_Initialize(dir, certPrefix, certPrefix, SECMOD_DB, NSS_INIT_READONLY);
        if (rv != SECSuccess) {
            fputs("NSS_Init failed.\n", stderr);
            exit(8);
        }
        /* we're an ordinary single process server. */
        listen_sock = getBoundListenSocket(port);
        prStatus = PR_SetFDInheritable(listen_sock, PR_FALSE);
        if (prStatus != PR_SUCCESS)
            errExit("PR_SetFDInheritable");
        if (!NoReuse) {
            rv = SSL_ConfigServerSessionIDCache(NumSidCacheEntries,
                                                0, 0, tmp);
            if (rv != SECSuccess)
                errExit("SSL_ConfigServerSessionIDCache");
            hasSidCache = PR_TRUE;
        }
    }

    lm = PR_NewLogModule("TestCase");

    if (fileName)
        readBigFile(fileName);

    /* set our password function */
    PK11_SetPasswordFunc(SECU_GetModulePassword);

    /* all SSL3 cipher suites are enabled by default. */
    if (cipherString) {
        char *cstringSaved = cipherString;
        int ndx;

        /* disable all the ciphers, then enable the ones we want. */
        disableAllSSLCiphers();

        while (0 != (ndx = *cipherString++)) {
            int cipher = 0;

            if (ndx == ':') {
                int ctmp;

                HEXCHAR_TO_INT(*cipherString, ctmp)
                cipher |= (ctmp << 12);
                cipherString++;
                HEXCHAR_TO_INT(*cipherString, ctmp)
                cipher |= (ctmp << 8);
                cipherString++;
                HEXCHAR_TO_INT(*cipherString, ctmp)
                cipher |= (ctmp << 4);
                cipherString++;
                HEXCHAR_TO_INT(*cipherString, ctmp)
                cipher |= ctmp;
                cipherString++;
            } else {
                if (!isalpha((unsigned char)ndx)) {
                    fprintf(stderr,
                            "Non-alphabetic char in cipher string (-c arg).\n");
                    exit(9);
                }
                ndx = tolower((unsigned char)ndx) - 'a';
                if (ndx < PR_ARRAY_SIZE(ssl3CipherSuites)) {
                    cipher = ssl3CipherSuites[ndx];
                }
            }
            if (cipher > 0) {
                rv = SSL_CipherPrefSetDefault(cipher, SSL_ALLOWED);
                if (rv != SECSuccess) {
                    SECU_PrintError(progName, "SSL_CipherPrefSetDefault()");
                    exit(9);
                }
            } else {
                fprintf(stderr,
                        "Invalid cipher specification (-c arg).\n");
                exit(9);
            }
        }
        PORT_Free(cstringSaved);
    }

    certStatusArena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (!certStatusArena)
        errExit("cannot allocate certStatusArena");

    for (i = 0; i < certNicknameIndex; i++) {
        cert[i] = PK11_FindCertFromNickname(certNicknameArray[i], &pwdata);
        if (cert[i] == NULL) {
            fprintf(stderr, "selfserv: Can't find certificate %s\n", certNicknameArray[i]);
            exit(10);
        }
        privKey[i] = PK11_FindKeyByAnyCert(cert[i], &pwdata);
        if (privKey[i] == NULL) {
            fprintf(stderr, "selfserv: Can't find Private Key for cert %s\n",
                    certNicknameArray[i]);
            exit(11);
        }
        if (privKey[i]->keyType != ecKey)
            setupCertStatus(certStatusArena, cert[i], i, &pwdata);
    }

    if (configureWeakDHE > 0) {
        fprintf(stderr, "selfserv: Creating dynamic weak DH parameters\n");
        rv = SSL_EnableWeakDHEPrimeGroup(NULL, PR_TRUE);
        if (rv != SECSuccess) {
            goto cleanup;
        }
        fprintf(stderr, "selfserv: Done creating dynamic weak DH parameters\n");
    }
    if (zeroRTT) {
        rv = SSL_CreateAntiReplayContext(PR_Now(), 10L * PR_USEC_PER_SEC, 7, 14, &antiReplay);
        if (rv != SECSuccess) {
            errExit("Unable to create anti-replay context for 0-RTT.");
        }
    }

    /* allocate the array of thread slots, and launch the worker threads. */
    rv = launch_threads(&jobLoop, 0, 0, useLocalThreads);

    if (rv == SECSuccess && logStats) {
        loggerThread = PR_CreateThread(PR_SYSTEM_THREAD,
                                       logger, NULL, PR_PRIORITY_NORMAL,
                                       useLocalThreads ? PR_LOCAL_THREAD
                                                       : PR_GLOBAL_THREAD,
                                       PR_JOINABLE_THREAD, 0);
        if (loggerThread == NULL) {
            fprintf(stderr, "selfserv: Failed to launch logger thread!\n");
            rv = SECFailure;
        }
    }

    if (rv == SECSuccess) {
        server_main(listen_sock, privKey, cert,
                    expectedHostNameVal);
    }

    VLOG(("selfserv: server_thread: exiting"));

cleanup:
    printSSLStatistics();
    ssl3stats = SSL_GetStatistics();
    if (ssl3stats->hch_sid_ticket_parse_failures != 0) {
        fprintf(stderr, "selfserv: Experienced ticket parse failure(s)\n");
        exit(1);
    }
    if (failedToNegotiateName) {
        fprintf(stderr, "selfserv: Failed properly negotiate server name\n");
        exit(1);
    }

    {
        for (i = 0; i < certNicknameIndex; i++) {
            if (cert[i]) {
                CERT_DestroyCertificate(cert[i]);
            }
            if (privKey[i]) {
                SECKEY_DestroyPrivateKey(privKey[i]);
            }
            PORT_Free(certNicknameArray[i]);
        }
        for (i = 0; virtServerNameArray[i]; i++) {
            PORT_Free(virtServerNameArray[i]);
        }
    }

    if (debugCache) {
        nss_DumpCertificateCacheInfo();
    }
    if (expectedHostNameVal) {
        PORT_Free(expectedHostNameVal);
    }
    if (passwd) {
        PORT_Free(passwd);
    }
    if (pwfile) {
        PORT_Free(pwfile);
    }
    if (certPrefix && certPrefix != emptyString) {
        PORT_Free(certPrefix);
    }

    if (hasSidCache) {
        SSL_ShutdownServerSessionIDCache();
    }
    if (certStatusArena) {
        PORT_FreeArena(certStatusArena, PR_FALSE);
    }
    if (enabledGroups) {
        PORT_Free(enabledGroups);
    }
    if (antiReplay) {
        SSL_ReleaseAntiReplayContext(antiReplay);
    }
    SECITEM_ZfreeItem(&psk, PR_FALSE);
    SECITEM_ZfreeItem(&pskLabel, PR_FALSE);
    PORT_Free(echParamsStr);
    if (NSS_Shutdown() != SECSuccess) {
        SECU_PrintError(progName, "NSS_Shutdown");
        if (loggerThread) {
            PR_JoinThread(loggerThread);
        }
        PR_Cleanup();
        exit(1);
    }
    PR_Cleanup();
    printf("selfserv: normal termination\n");
    return 0;
}
