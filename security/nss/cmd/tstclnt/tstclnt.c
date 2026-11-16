/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
**
** Sample client side test program that uses SSL and NSS
**
*/

#include "secutil.h"
#include "basicutil.h"

#if defined(XP_UNIX)
#include <unistd.h>
#else
#include <ctype.h> /* for isalpha() */
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>

#include "nspr.h"
#include "prio.h"
#include "prnetdb.h"
#include "nss.h"
#include "nssb64.h"
#include "ssl.h"
#include "sslproto.h"
#include "sslexp.h"
#include "pk11func.h"
#include "secmod.h"
#include "plgetopt.h"
#include "plstr.h"
#include "zlib.h"

#if defined(WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#define PRINTF   \
    if (verbose) \
    printf
#define FPRINTF  \
    if (verbose) \
    fprintf

#define MAX_WAIT_FOR_SERVER 600
#define WAIT_INTERVAL 100
#define ZERO_RTT_MAX (2 << 16)

#define EXIT_CODE_HANDSHAKE_FAILED 254

PRIntervalTime maxInterval = PR_INTERVAL_NO_TIMEOUT;

int ssl3CipherSuites[] = {
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

unsigned long __cmp_umuls;
PRBool verbose;
int dumpServerChain = 0;
int renegotiationsToDo = 0;
int renegotiationsDone = 0;
PRBool initializedServerSessionCache = PR_FALSE;

static char *progName;
static const char *requestFile;

secuPWData pwdata = { PW_NONE, 0 };

SSLNamedGroup *enabledGroups = NULL;
unsigned int enabledGroupsCount = 0;
const SSLSignatureScheme *enabledSigSchemes = NULL;
unsigned int enabledSigSchemeCount = 0;
SECItem psk = { siBuffer, NULL, 0 };
SECItem pskLabel = { siBuffer, NULL, 0 };

void
printSecurityInfo(PRFileDesc *fd)
{
    CERTCertificate *cert;
    const SECItemArray *csa;
    const SECItem *scts;
    SSL3Statistics *ssl3stats = SSL_GetStatistics();
    SECStatus result;
    SSLChannelInfo channel;
    SSLCipherSuiteInfo suite;

    result = SSL_GetChannelInfo(fd, &channel, sizeof channel);
    if (result == SECSuccess &&
        channel.length == sizeof channel &&
        channel.cipherSuite) {
        result = SSL_GetCipherSuiteInfo(channel.cipherSuite,
                                        &suite, sizeof suite);
        if (result == SECSuccess) {
            FPRINTF(stderr,
                    "tstclnt: SSL version %d.%d using %d-bit %s with %d-bit %s MAC%s\n",
                    channel.protocolVersion >> 8, channel.protocolVersion & 0xff,
                    suite.effectiveKeyBits, suite.symCipherName,
                    suite.macBits, suite.macAlgorithmName,
                    channel.isFIPS ? " FIPS" : "");
            FPRINTF(stderr,
                    "tstclnt: Server Auth: %d-bit %s, Key Exchange: %d-bit %s\n"
                    "         Key Exchange Group: %s\n"
                    "         Compression: %s, Extended Master Secret: %s\n"
                    "         Signature Scheme: %s\n",
                    channel.authKeyBits, suite.authAlgorithmName,
                    channel.keaKeyBits, suite.keaTypeName,
                    SECU_NamedGroupToGroupName(channel.keaGroup),
                    channel.compressionMethodName,
                    channel.extendedMasterSecretUsed ? "Yes" : "No",
                    SECU_SignatureSchemeName(channel.signatureScheme));
        }
    }
    cert = SSL_RevealCert(fd);
    if (cert) {
        char *ip = CERT_NameToAscii(&cert->issuer);
        char *sp = CERT_NameToAscii(&cert->subject);
        if (sp) {
            fprintf(stderr, "subject DN: %s\n", sp);
            PORT_Free(sp);
        }
        if (ip) {
            fprintf(stderr, "issuer  DN: %s\n", ip);
            PORT_Free(ip);
        }
        CERT_DestroyCertificate(cert);
        cert = NULL;
    }
    fprintf(stderr,
            "%ld cache hits; %ld cache misses, %ld cache not reusable\n"
            "%ld stateless resumes\n",
            ssl3stats->hsh_sid_cache_hits, ssl3stats->hsh_sid_cache_misses,
            ssl3stats->hsh_sid_cache_not_ok, ssl3stats->hsh_sid_stateless_resumes);

    csa = SSL_PeerStapledOCSPResponses(fd);
    if (csa) {
        fprintf(stderr, "Received %d Cert Status items (OCSP stapled data)\n",
                csa->len);
    }
    scts = SSL_PeerSignedCertTimestamps(fd);
    if (scts && scts->len) {
        fprintf(stderr, "Received a Signed Certificate Timestamp of length"
                        " %u\n",
                scts->len);
    }
    if (channel.peerDelegCred) {
        fprintf(stderr, "Received a Delegated Credential\n");
    }
}

static void
PrintUsageHeader()
{
    fprintf(stderr,
            "Usage:  %s -h host [-a 1st_hs_name ] [-a 2nd_hs_name ] [-p port]\n"
            "  [-D | -d certdir] [-C] [-b | -R root-module] \n"
            "  [-n nickname] [-Bafosvx] [-c ciphers] [-Y] [-Z] [-E]\n"
            "  [-V [min-version]:[max-version]] [-K] [-T] [-U]\n"
            "  [-r N] [-w passwd] [-W pwfile] [-q [-t seconds]]\n"
            "  [-I groups] [-J signatureschemes]\n"
            "  [-A requestfile] [-L totalconnections] [-P {client,server}]\n"
            "  [-N echConfigs] [-Q] [-z externalPsk]\n"
            "  [-i echGreaseSize]\n"
            "  [--enable-rfc8701-grease] [--enable-ch-extension-permutation]\n"
            "  [--zlib-certificate-compression] \n"
            "\n",
            progName);
}

static void
PrintParameterUsage()
{
    fprintf(stderr, "%-20s Send different SNI name. 1st_hs_name - at first\n"
                    "%-20s handshake, 2nd_hs_name - at second handshake.\n"
                    "%-20s Default is host from the -h argument.\n",
            "-a name",
            "", "");
    fprintf(stderr, "%-20s Hostname to connect with\n", "-h host");
    fprintf(stderr, "%-20s Port number for SSL server\n", "-p port");
    fprintf(stderr,
            "%-20s Directory with cert database (default is ~/.netscape)\n",
            "-d certdir");
    fprintf(stderr, "%-20s Run without a cert database\n", "-D");
    fprintf(stderr, "%-20s Load the default \"builtins\" root CA module\n", "-b");
    fprintf(stderr, "%-20s Load the given root CA module\n", "-R");
    fprintf(stderr, "%-20s Print certificate chain information\n", "-C");
    fprintf(stderr, "%-20s (use -C twice to print more certificate details)\n", "");
    fprintf(stderr, "%-20s (use -C three times to include PEM format certificate dumps)\n", "");
    fprintf(stderr, "%-20s Nickname of key and cert\n",
            "-n nickname");
    fprintf(stderr,
            "%-20s Restricts the set of enabled SSL/TLS protocols versions.\n"
            "%-20s All versions are enabled by default.\n"
            "%-20s Possible values for min/max: ssl3 tls1.0 tls1.1 tls1.2 tls1.3\n"
            "%-20s Example: \"-V ssl3:\" enables SSL 3 and newer.\n",
            "-V [min]:[max]", "", "", "");
    fprintf(stderr, "%-20s Send TLS_FALLBACK_SCSV\n", "-K");
    fprintf(stderr, "%-20s Prints only payload data. Skips HTTP header.\n", "-S");
    fprintf(stderr, "%-20s Client speaks first. \n", "-f");
    fprintf(stderr, "%-20s Use synchronous certificate selection & validation\n", "-O");
    fprintf(stderr, "%-20s Override bad server cert. Make it OK.\n", "-o");
    fprintf(stderr, "%-20s Disable SSL socket locking.\n", "-s");
    fprintf(stderr, "%-20s Verbose progress reporting.\n", "-v");
    fprintf(stderr, "%-20s Ping the server and then exit.\n", "-q");
    fprintf(stderr, "%-20s Timeout for server ping (default: no timeout).\n", "-t seconds");
    fprintf(stderr, "%-20s Renegotiate N times (resuming session if N>1).\n", "-r N");
    fprintf(stderr, "%-20s Enable the session ticket extension.\n", "-u");
    fprintf(stderr, "%-20s Enable false start.\n", "-g");
    fprintf(stderr, "%-20s Enable the cert_status extension (OCSP stapling).\n", "-T");
    fprintf(stderr, "%-20s Enable the signed_certificate_timestamp extension.\n", "-U");
    fprintf(stderr, "%-20s Enable the delegated credentials extension.\n", "-B");
    fprintf(stderr, "%-20s Restrict ciphers\n", "-c ciphers");
    fprintf(stderr, "%-20s Print cipher values allowed for parameter -c and exit\n", "-Y");
    fprintf(stderr, "%-20s Enforce using an IPv4 destination address\n", "-4");
    fprintf(stderr, "%-20s Enforce using an IPv6 destination address\n", "-6");
    fprintf(stderr, "%-20s (Options -4 and -6 cannot be combined.)\n", "");
    fprintf(stderr, "%-20s Enable the extended master secret extension [RFC7627]\n", "-G");
    fprintf(stderr, "%-20s Require the use of FFDHE supported groups [RFC7919]\n", "-H");
    fprintf(stderr, "%-20s Read from a file instead of stdin\n", "-A");
    fprintf(stderr, "%-20s Allow 0-RTT data (TLS 1.3 only)\n", "-Z");
    fprintf(stderr, "%-20s Disconnect and reconnect up to N times total\n", "-L");
    fprintf(stderr, "%-20s Comma separated list of enabled groups for TLS key exchange.\n"
                    "%-20s The following values are valid",
            "-I", "");
    char comma = ':';
    const char *groupName;
    int total = SECU_MAX_COL_LEN;
    for (size_t i = 0; (groupName = SECU_NamedGroupGetNextName(i)) != NULL; i++) {
        int len = strlen(groupName);
        /* 2 represents a comma and a space */
        if ((total + len + 2) > SECU_MAX_COL_LEN) {
            fprintf(stderr, "%c\n%-20s %s", comma, "", groupName);
            /* 21 represents 21 spaces */
            total = len + 21;
        } else {
            fprintf(stderr, "%c %s", comma, groupName);
            /* 2 represents a comma and a space */
            total += len + 2;
        }
        comma = ',';
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "%-20s Comma separated list of signature schemes in preference order.\n"
                    "%-20s The following values are valid",
            "-J", "");

    comma = ':';
    const char *schemeName;
    total = SECU_MAX_COL_LEN;
    for (size_t i = 0; (schemeName = SECU_SignatureSchemeGetNextScheme(i)) != NULL; i++) {
        int len = strlen(schemeName);
        /* 2 represents a comma and a space */
        if ((total + len + 2) > SECU_MAX_COL_LEN) {
            fprintf(stderr, "%c\n%-20s %s", comma, "", schemeName);
            /* 21 represents 21 spaces */
            total = len + 21;
        } else {
            fprintf(stderr, "%c %s", comma, schemeName);
            /* 2 represents a comma and a space */
            total += len + 2;
        }
        comma = ',';
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "%-20s Use DTLS\n", "-P {client, server}");
    fprintf(stderr, "%-20s Exit after handshake\n", "-Q");
    fprintf(stderr, "%-20s Use Encrypted Client Hello with the given Base64-encoded ECHConfigs\n", "-N");
    fprintf(stderr, "%-20s Enable Encrypted Client Hello GREASEing with the given padding size (0-255) \n", "-i");
    fprintf(stderr, "%-20s Enable post-handshake authentication\n"
                    "%-20s for TLS 1.3; need to specify -n\n",
            "-E", "");
    fprintf(stderr, "%-20s Export and print keying material after successful handshake.\n"
                    "%-20s The argument is a comma separated list of exporters in the form:\n"
                    "%-20s   LABEL[:OUTPUT-LENGTH[:CONTEXT]]\n"
                    "%-20s where LABEL and CONTEXT can be either a free-form string or\n"
                    "%-20s a hex string if it is preceded by \"0x\"; OUTPUT-LENGTH\n"
                    "%-20s is a decimal integer.\n",
            "-x", "", "", "", "", "");
    fprintf(stderr,
            "%-20s Configure a TLS 1.3 External PSK with the given hex string for a key\n"
            "%-20s To specify a label, use ':' as a delimiter. For example\n"
            "%-20s 0xAAAABBBBCCCCDDDD:mylabel. Otherwise, the default label of\n"
            "%-20s 'Client_identity' will be used.\n",
            "-z externalPsk", "", "", "");
    fprintf(stderr, "%-20s Enable middlebox compatibility mode (TLS 1.3 only)\n", "-e");
}

static void
Usage()
{
    PrintUsageHeader();
    PrintParameterUsage();
    exit(1);
}

static void
PrintCipherUsage()
{
    PrintUsageHeader();
    fprintf(stderr, "%-20s Letter(s) chosen from the following list\n",
            "-c ciphers");
    fprintf(stderr,
            "c    SSL3 RSA WITH RC4 128 MD5\n"
            "d    SSL3 RSA WITH 3DES EDE CBC SHA\n"
            "e    SSL3 RSA WITH DES CBC SHA\n"
            "i    SSL3 RSA WITH NULL MD5\n"
            "n    SSL3 RSA WITH RC4 128 SHA\n"
            "o    SSL3 DHE DSS WITH RC4 128 SHA\n"
            "p    SSL3 DHE RSA WITH 3DES EDE CBC SHA\n"
            "q    SSL3 DHE DSS WITH 3DES EDE CBC SHA\n"
            "r    SSL3 DHE RSA WITH DES CBC SHA\n"
            "s    SSL3 DHE DSS WITH DES CBC SHA\n"
            "t    SSL3 DHE DSS WITH AES 128 CBC SHA\n"
            "u    SSL3 DHE RSA WITH AES 128 CBC SHA\n"
            "v    SSL3 RSA WITH AES 128 CBC SHA\n"
            "w    SSL3 DHE DSS WITH AES 256 CBC SHA\n"
            "x    SSL3 DHE RSA WITH AES 256 CBC SHA\n"
            "y    SSL3 RSA WITH AES 256 CBC SHA\n"
            "z    SSL3 RSA WITH NULL SHA\n"
            "\n"
            ":WXYZ  Use cipher with hex code { 0xWX , 0xYZ } in TLS\n");
    exit(1);
}

void
milliPause(PRUint32 milli)
{
    PRIntervalTime ticks = PR_MillisecondsToInterval(milli);
    PR_Sleep(ticks);
}

void
disableAllSSLCiphers()
{
    const PRUint16 *cipherSuites = SSL_GetImplementedCiphers();
    int i = SSL_GetNumImplementedCiphers();
    SECStatus rv;

    /* disable all the SSL3 cipher suites */
    while (--i >= 0) {
        PRUint16 suite = cipherSuites[i];
        rv = SSL_CipherPrefSetDefault(suite, PR_FALSE);
        if (rv != SECSuccess) {
            PRErrorCode err = PR_GetError();
            fprintf(stderr,
                    "SSL_CipherPrefSet didn't like value 0x%04x (i = %d): %s\n",
                    suite, i, SECU_Strerror(err));
            exit(2);
        }
    }
}

typedef struct
{
    PRBool shouldPause; /* PR_TRUE if we should use asynchronous peer cert
                         * authentication */
    PRBool isPaused;    /* PR_TRUE if libssl is waiting for us to validate the
                         * peer's certificate and restart the handshake. */
    void *dbHandle;     /* Certificate database handle to use while
                         * authenticating the peer's certificate. */
} ServerCertAuth;

/*
 * Callback is called when incoming certificate is not valid.
 * Returns SECSuccess to accept the cert anyway, SECFailure to reject.
 */
static SECStatus
ownBadCertHandler(void *arg, PRFileDesc *socket)
{
    PRErrorCode err = PR_GetError();
    /* can log invalid cert here */
    fprintf(stderr, "Bad server certificate: %d, %s\n", err,
            SECU_Strerror(err));
    return SECSuccess; /* override, say it's OK. */
}

static void
dumpCertificatePEM(CERTCertificate *cert)
{
    char *asciiDER = BTOA_DataToAscii(cert->derCert.data, cert->derCert.len);
    if (asciiDER) {
        fprintf(stderr, "%s\n%s\n%s\n", NS_CERT_HEADER, asciiDER, NS_CERT_TRAILER);
        PORT_Free(asciiDER);
    }
}

static void
dumpServerCertificateChain(PRFileDesc *fd)
{
    CERTCertList *peerCertChain = NULL;
    CERTCertListNode *node = NULL;
    CERTCertificate *peerCert = NULL;
    CERTCertificateList *foundChain = NULL;
    SECU_PPFunc dumpFunction = NULL;
    PRBool dumpCertPEM = PR_FALSE;

    if (!dumpServerChain) {
        return;
    } else if (dumpServerChain == 1) {
        dumpFunction = SECU_PrintCertificateBasicInfo;
    } else {
        dumpFunction = SECU_PrintCertificate;
        if (dumpServerChain > 2) {
            dumpCertPEM = PR_TRUE;
        }
    }

    SECU_EnableWrap(PR_FALSE);

    fprintf(stderr, "==== certificate(s) sent by server: ====\n");
    peerCertChain = SSL_PeerCertificateChain(fd);
    if (peerCertChain) {
        node = CERT_LIST_HEAD(peerCertChain);
        while (!CERT_LIST_END(node, peerCertChain)) {
            CERTCertificate *cert = node->cert;
            SECU_PrintSignedContent(stderr, &cert->derCert, "Certificate", 0,
                                    dumpFunction);
            if (dumpCertPEM) {
                dumpCertificatePEM(cert);
            }
            node = CERT_LIST_NEXT(node);
        }
    }

    if (peerCertChain) {
        peerCert = SSL_RevealCert(fd);
        if (peerCert) {
            foundChain = CERT_CertChainFromCert(peerCert, certificateUsageSSLServer,
                                                PR_TRUE);
        }
        if (foundChain) {
            unsigned int count = 0;
            fprintf(stderr, "==== locally found issuer certificate(s): ====\n");
            for (count = 0; count < (unsigned int)foundChain->len; count++) {
                CERTCertificate *c;
                PRBool wasSentByServer = PR_FALSE;
                c = CERT_FindCertByDERCert(CERT_GetDefaultCertDB(), &foundChain->certs[count]);

                node = CERT_LIST_HEAD(peerCertChain);
                while (!CERT_LIST_END(node, peerCertChain)) {
                    CERTCertificate *cert = node->cert;
                    if (CERT_CompareCerts(cert, c)) {
                        wasSentByServer = PR_TRUE;
                        break;
                    }
                    node = CERT_LIST_NEXT(node);
                }

                if (!wasSentByServer) {
                    SECU_PrintSignedContent(stderr, &c->derCert, "Certificate", 0,
                                            dumpFunction);
                    if (dumpCertPEM) {
                        dumpCertificatePEM(c);
                    }
                }
                CERT_DestroyCertificate(c);
            }
            CERT_DestroyCertificateList(foundChain);
        }
        if (peerCert) {
            CERT_DestroyCertificate(peerCert);
        }

        CERT_DestroyCertList(peerCertChain);
        peerCertChain = NULL;
    }

    fprintf(stderr, "==== end of certificate chain information ====\n");
    fflush(stderr);
}

static SECStatus
ownAuthCertificate(void *arg, PRFileDesc *fd, PRBool checkSig,
                   PRBool isServer)
{
    ServerCertAuth *serverCertAuth = (ServerCertAuth *)arg;

    if (dumpServerChain) {
        dumpServerCertificateChain(fd);
    }

    if (!serverCertAuth->shouldPause) {
        return SSL_AuthCertificate(serverCertAuth->dbHandle,
                                   fd, checkSig, isServer);
    }

    FPRINTF(stderr, "%s: using asynchronous certificate validation\n",
            progName);

    PORT_Assert(!serverCertAuth->isPaused);
    serverCertAuth->isPaused = PR_TRUE;
    return SECWouldBlock;
}

struct clientCertAsyncParamsStr {
    void *arg;                        /* The nickname used for selection, not owned */
    struct CERTDistNamesStr *caNames; /* CA Names specified by Server, owned. */
};

/* tstclnt can only have a single handshake in progress at any instant. */
PRBool clientCertAsyncSelect = PR_TRUE; /* Async by default */
PRBool clientCertIsBlocked = PR_FALSE;  /* Whether we waiting to finish ClientAuth */
struct clientCertAsyncParamsStr *clientCertParams = NULL;

SECStatus
own_CompleteClientAuthData(PRFileDesc *fd, struct clientCertAsyncParamsStr *args)
{
    SECStatus rv;
    CERTCertificate *pRetCert = NULL;
    SECKEYPrivateKey *pRetKey = NULL;
    rv = NSS_GetClientAuthData(args->arg, fd, args->caNames, &pRetCert, &pRetKey);
    if (rv != SECSuccess) {
        fprintf(stderr, "Failed to load a suitable client certificate \n");
    }
    return SSL_ClientCertCallbackComplete(fd, rv, pRetKey, pRetCert);
}

SECStatus
restartHandshakeAfterClientCertIfNeeded(PRFileDesc *fd)
{
    if (!clientCertIsBlocked) {
        return SECFailure;
    }
    clientCertIsBlocked = PR_FALSE;
    own_CompleteClientAuthData(fd, clientCertParams);
    CERT_FreeDistNames(clientCertParams->caNames);
    PORT_Free(clientCertParams);
    clientCertParams = NULL;
    return SECSuccess;
}

SECStatus
own_GetClientAuthData(void *arg,
                      PRFileDesc *socket,
                      struct CERTDistNamesStr *caNames,
                      struct CERTCertificateStr **pRetCert,
                      struct SECKEYPrivateKeyStr **pRetKey)
{
    if (clientCertAsyncSelect) {
        PR_ASSERT(!clientCertIsBlocked);
        PR_ASSERT(!clientCertParams);

        clientCertIsBlocked = PR_TRUE;
        clientCertParams = PORT_Alloc(sizeof(struct clientCertAsyncParamsStr));
        if (!clientCertParams) {
            fprintf(stderr, "Unable to allocate buffer for client cert callback\n");
            exit(1);
        }

        clientCertParams->arg = arg;
        clientCertParams->caNames = caNames ? CERT_DupDistNames(caNames) : NULL;
        if (caNames && !clientCertParams->caNames) {
            fprintf(stderr, "Unable to allocate buffer for client cert callback\n");
            exit(1);
        }
        return SECWouldBlock;
    }

    if (verbose > 1) {
        SECStatus rv;
        fprintf(stderr, "Server requested Client Authentication\n");
        if (caNames && caNames->nnames > 0) {
            PLArenaPool *arena = caNames->arena;
            if (!arena)
                arena = PORT_NewArena(2048);
            if (arena) {
                int i;
                for (i = 0; i < caNames->nnames; ++i) {
                    char *nameString;
                    CERTName dn;
                    rv = SEC_QuickDERDecodeItem(arena,
                                                &dn,
                                                SEC_ASN1_GET(CERT_NameTemplate),
                                                caNames->names + i);
                    if (rv != SECSuccess)
                        continue;
                    nameString = CERT_NameToAscii(&dn);
                    if (!nameString)
                        continue;
                    fprintf(stderr, "CA[%d]: %s\n", i + 1, nameString);
                    PORT_Free(nameString);
                }
                if (!caNames->arena) {
                    PORT_FreeArena(arena, PR_FALSE);
                }
            }
        }
        rv = NSS_GetClientAuthData(arg, socket, caNames, pRetCert, pRetKey);
        if (rv == SECSuccess && *pRetCert) {
            char *nameString = CERT_NameToAscii(&((*pRetCert)->subject));
            if (nameString) {
                fprintf(stderr, "sent cert: %s\n", nameString);
                PORT_Free(nameString);
            }
        } else {
            fprintf(stderr, "send no cert\n");
        }
        return rv;
    }
    return NSS_GetClientAuthData(arg, socket, caNames, pRetCert, pRetKey);
}

#if defined(WIN32)
void
thread_main(void *arg)
{
    PRFileDesc *ps = (PRFileDesc *)arg;
    PRFileDesc *std_in;
    int wc, rc;
    char buf[256];

    if (requestFile) {
        std_in = PR_Open(requestFile, PR_RDONLY, 0);
    } else {
        std_in = PR_GetSpecialFD(PR_StandardInput);
    }

#ifdef WIN32
    if (!requestFile) {
        /* Put stdin into O_BINARY mode
        ** or else incoming \r\n's will become \n's.
        */
        int smrv = _setmode(_fileno(stdin), _O_BINARY);
        if (smrv == -1) {
            fprintf(stderr,
                    "%s: Cannot change stdin to binary mode. Use -i option instead.\n",
                    progName);
            /* plow ahead anyway */
        }
    }
#endif

    do {
        rc = PR_Read(std_in, buf, sizeof buf);
        if (rc <= 0)
            break;
        wc = PR_Send(ps, buf, rc, 0, maxInterval);
    } while (wc == rc);
    PR_Close(ps);
    if (requestFile) {
        PR_Close(std_in);
    }
}

#endif

static void
printHostNameAndAddr(const char *host, const PRNetAddr *addr)
{
    PRUint16 port = PR_NetAddrInetPort(addr);
    char addrBuf[80];
    PRStatus st = PR_NetAddrToString(addr, addrBuf, sizeof addrBuf);

    if (st == PR_SUCCESS) {
        port = PR_ntohs(port);
        FPRINTF(stderr, "%s: connecting to %s:%hu (address=%s)\n",
                progName, host, port, addrBuf);
    }
}

/*
 *  Prints output according to skipProtoHeader flag. If skipProtoHeader
 *  is not set, prints without any changes, otherwise looking
 *  for \n\r\n(empty line sequence: HTTP header separator) and
 *  prints everything after it.
 */
static void
separateReqHeader(const PRFileDesc *outFd, const char *buf, const int nb,
                  PRBool *wrStarted, int *ptrnMatched)
{

    /* it is sufficient to look for only "\n\r\n". Hopping that
     * HTTP response format satisfies the standard */
    char *ptrnStr = "\n\r\n";
    char *resPtr;

    if (nb == 0) {
        return;
    }

    if (*ptrnMatched > 0) {
        /* Get here only if previous separateReqHeader call found
         * only a fragment of "\n\r\n" in previous buffer. */
        PORT_Assert(*ptrnMatched < 3);

        /* the size of fragment of "\n\r\n" what we want to find in this
         * buffer is equal to *ptrnMatched */
        if (*ptrnMatched <= nb) {
            /* move the pointer to the beginning of the fragment */
            int strSize = *ptrnMatched;
            char *tmpPtrn = ptrnStr + (3 - strSize);
            if (PL_strncmp(buf, tmpPtrn, strSize) == 0) {
                /* print the rest of the buffer(without the fragment) */
                PR_Write((void *)outFd, buf + strSize, nb - strSize);
                *wrStarted = PR_TRUE;
                return;
            }
        } else {
            /* we are here only when nb == 1 && *ptrnMatched == 2 */
            if (*buf == '\r') {
                *ptrnMatched = 1;
            } else {
                *ptrnMatched = 0;
            }
            return;
        }
    }
    resPtr = PL_strnstr(buf, ptrnStr, nb);
    if (resPtr != NULL) {
        /* if "\n\r\n" was found in the buffer, calculate offset
         * and print the rest of the buffer */
        int newBn = nb - (resPtr - buf + 3); /* 3 is the length of "\n\r\n" */

        PR_Write((void *)outFd, resPtr + 3, newBn);
        *wrStarted = PR_TRUE;
        return;
    } else {
        /* try to find a fragment of "\n\r\n" at the end of the buffer.
         * if found, set *ptrnMatched to the number of chars left to find
         * in the next buffer.*/
        int i;
        for (i = 1; i < 3; i++) {
            char *bufPrt;
            int strSize = 3 - i;

            if (strSize > nb) {
                continue;
            }
            bufPrt = (char *)(buf + nb - strSize);

            if (PL_strncmp(bufPrt, ptrnStr, strSize) == 0) {
                *ptrnMatched = i;
                return;
            }
        }
    }
}

#define SSOCK_FD 0
#define STDIN_FD 1

#define HEXCHAR_TO_INT(c, i)                   \
    if (((c) >= '0') && ((c) <= '9')) {        \
        i = (c) - '0';                         \
    } else if (((c) >= 'a') && ((c) <= 'f')) { \
        i = (c) - 'a' + 10;                    \
    } else if (((c) >= 'A') && ((c) <= 'F')) { \
        i = (c) - 'A' + 10;                    \
    } else {                                   \
        Usage();                               \
    }

static SECStatus
restartHandshakeAfterServerCertIfNeeded(PRFileDesc *fd,
                                        ServerCertAuth *serverCertAuth,
                                        PRBool override)
{
    SECStatus rv;
    PRErrorCode error = 0;

    if (!serverCertAuth->isPaused || clientCertIsBlocked)
        return SECSuccess;

    FPRINTF(stderr, "%s: handshake was paused by auth certificate hook\n",
            progName);

    serverCertAuth->isPaused = PR_FALSE;
    rv = SSL_AuthCertificate(serverCertAuth->dbHandle, fd, PR_TRUE, PR_FALSE);
    if (rv != SECSuccess) {
        error = PR_GetError();
        if (error == 0) {
            PR_NOT_REACHED("SSL_AuthCertificate return SECFailure without "
                           "setting error code.");
            error = PR_INVALID_STATE_ERROR;
        } else if (override) {
            rv = ownBadCertHandler(NULL, fd);
        }
    }
    if (rv == SECSuccess) {
        error = 0;
    }

    if (SSL_AuthCertificateComplete(fd, error) != SECSuccess) {
        rv = SECFailure;
    } else {
        /* restore the original error code, which could be reset by
         * SSL_AuthCertificateComplete */
        PORT_SetError(error);
    }

    return rv;
}

char *host = NULL;
char *nickname = NULL;
char *cipherString = NULL;
int multiplier = 0;
SSLVersionRange enabledVersions;
int disableLocking = 0;
int enableSessionTickets = 0;
int enableFalseStart = 0;
int enableCertStatus = 0;
int enableSignedCertTimestamps = 0;
int forceFallbackSCSV = 0;
int enableExtendedMasterSecret = 0;
PRBool requireDHNamedGroups = 0;
PRBool middleboxCompatMode = 0;
PRSocketOptionData opt;
PRNetAddr addr;
PRBool allowIPv4 = PR_TRUE;
PRBool allowIPv6 = PR_TRUE;
PRBool pingServerFirst = PR_FALSE;
int pingTimeoutSeconds = -1;
PRBool clientSpeaksFirst = PR_FALSE;
PRBool skipProtoHeader = PR_FALSE;
ServerCertAuth serverCertAuth;
char *hs1SniHostName = NULL;
char *hs2SniHostName = NULL;
PRUint16 portno = 443;
int override = 0;
PRBool enableZeroRtt = PR_FALSE;
PRUint8 *zeroRttData;
unsigned int zeroRttLen = 0;
PRBool enableAltServerHello = PR_FALSE;
PRBool useDTLS = PR_FALSE;
PRBool actAsServer = PR_FALSE;
PRBool stopAfterHandshake = PR_FALSE;
PRBool requestToExit = PR_FALSE;
char *versionString = NULL;
PRBool handshakeComplete = PR_FALSE;
char *echConfigs = NULL;
PRUint16 echGreaseSize = 0;
PRBool enablePostHandshakeAuth = PR_FALSE;
PRBool enableDelegatedCredentials = PR_FALSE;
const secuExporter *enabledExporters = NULL;
unsigned int enabledExporterCount = 0;
PRBool enableRfc8701Grease = PR_FALSE;
PRBool enableCHExtensionPermuation = PR_FALSE;
PRBool zlibCertificateCompression = PR_FALSE;

static int
writeBytesToServer(PRFileDesc *s, const PRUint8 *buf, int nb)
{
    SECStatus rv;
    const PRUint8 *bufp = buf;
    PRPollDesc pollDesc;

    pollDesc.in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
    pollDesc.out_flags = 0;
    pollDesc.fd = s;

    FPRINTF(stderr, "%s: Writing %d bytes to server\n",
            progName, nb);
    do {
        PRInt32 cc = PR_Send(s, bufp, nb, 0, maxInterval);
        if (cc < 0) {
            PRErrorCode err = PR_GetError();
            if (err != PR_WOULD_BLOCK_ERROR) {
                SECU_PrintError(progName, "write to SSL socket failed");
                return 254;
            }
            cc = 0;
        }
        FPRINTF(stderr, "%s: %d bytes written\n", progName, cc);
        if (enableZeroRtt && !handshakeComplete) {
            if (zeroRttLen + cc > ZERO_RTT_MAX) {
                SECU_PrintError(progName, "too much early data to save");
                return -1;
            }
            PORT_Memcpy(zeroRttData + zeroRttLen, bufp, cc);
            zeroRttLen += cc;
        }
        bufp += cc;
        nb -= cc;
        if (nb <= 0)
            break;

        rv = restartHandshakeAfterServerCertIfNeeded(s,
                                                     &serverCertAuth, override);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "authentication of server cert failed");
            return EXIT_CODE_HANDSHAKE_FAILED;
        }

        rv = restartHandshakeAfterClientCertIfNeeded(s);
        if (rv == SECSuccess) {
            continue;
        }

        pollDesc.in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
        pollDesc.out_flags = 0;
        FPRINTF(stderr,
                "%s: about to call PR_Poll on writable socket !\n",
                progName);
        cc = PR_Poll(&pollDesc, 1, PR_INTERVAL_NO_TIMEOUT);
        if (cc < 0) {
            SECU_PrintError(progName, "PR_Poll failed");
            return -1;
        }
        FPRINTF(stderr,
                "%s: PR_Poll returned with writable socket !\n",
                progName);
    } while (1);

    return 0;
}

void
handshakeCallback(PRFileDesc *fd, void *client_data)
{
    const char *secondHandshakeName = (char *)client_data;
    if (secondHandshakeName) {
        SSL_SetURL(fd, secondHandshakeName);
    }
    printSecurityInfo(fd);
    if (renegotiationsDone < renegotiationsToDo) {
        SSL_ReHandshake(fd, (renegotiationsToDo < 2));
        ++renegotiationsDone;
    }
    if (zeroRttLen) {
        /* This data was sent in 0-RTT. */
        SSLChannelInfo info;
        SECStatus rv;

        rv = SSL_GetChannelInfo(fd, &info, sizeof(info));
        if (rv != SECSuccess)
            return;

        if (!info.earlyDataAccepted) {
            FPRINTF(stderr, "Early data rejected. Re-sending %d bytes\n",
                    zeroRttLen);
            writeBytesToServer(fd, zeroRttData, zeroRttLen);
            zeroRttLen = 0;
        }
    }
    if (stopAfterHandshake) {
        requestToExit = PR_TRUE;
    }
    handshakeComplete = PR_TRUE;

    if (enabledExporters) {
        SECStatus rv;

        rv = exportKeyingMaterials(fd, enabledExporters, enabledExporterCount);
        if (rv != SECSuccess) {
            PRErrorCode err = PR_GetError();
            FPRINTF(stderr,
                    "couldn't export keying material: %s\n",
                    SECU_Strerror(err));
        }
    }
}

static SECStatus
installServerCertificate(PRFileDesc *s, char *nick)
{
    CERTCertificate *cert;
    SECKEYPrivateKey *privKey = NULL;

    if (!nick) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    cert = PK11_FindCertFromNickname(nick, &pwdata);
    if (cert == NULL) {
        return SECFailure;
    }

    privKey = PK11_FindKeyByAnyCert(cert, &pwdata);
    if (privKey == NULL) {
        return SECFailure;
    }
    if (SSL_ConfigServerCert(s, cert, privKey, NULL, 0) != SECSuccess) {
        return SECFailure;
    }
    SECKEY_DestroyPrivateKey(privKey);
    CERT_DestroyCertificate(cert);

    return SECSuccess;
}

static SECStatus
bindToClient(PRFileDesc *s)
{
    PRStatus status;
    status = PR_Bind(s, &addr);
    if (status != PR_SUCCESS) {
        return SECFailure;
    }

    for (;;) {
        /* Bind the remote address on first packet. This must happen
         * before we SSL-ize the socket because we need to get the
         * peer's address before SSLizing. Recvfrom gives us that
         * while not consuming any data. */
        unsigned char tmp;
        PRNetAddr remote;
        int nb;

        nb = PR_RecvFrom(s, &tmp, 1, PR_MSG_PEEK,
                         &remote, PR_INTERVAL_NO_TIMEOUT);
        if (nb != 1)
            continue;

        status = PR_Connect(s, &remote, PR_INTERVAL_NO_TIMEOUT);
        if (status != PR_SUCCESS) {
            SECU_PrintError(progName, "server bind to remote end failed");
            return SECFailure;
        }
        return SECSuccess;
    }

    /* Unreachable. */
}

static SECStatus
connectToServer(PRFileDesc *s, PRPollDesc *pollset)
{
    PRStatus status;
    PRInt32 filesReady;

    status = PR_Connect(s, &addr, PR_INTERVAL_NO_TIMEOUT);
    if (status != PR_SUCCESS) {
        if (PR_GetError() == PR_IN_PROGRESS_ERROR) {
            if (verbose)
                SECU_PrintError(progName, "connect");
            milliPause(50 * multiplier);
            pollset[SSOCK_FD].in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
            pollset[SSOCK_FD].out_flags = 0;
            pollset[SSOCK_FD].fd = s;
            while (1) {
                FPRINTF(stderr,
                        "%s: about to call PR_Poll for connect completion!\n",
                        progName);
                filesReady = PR_Poll(pollset, 1, PR_INTERVAL_NO_TIMEOUT);
                if (filesReady < 0) {
                    SECU_PrintError(progName, "unable to connect (poll)");
                    return SECFailure;
                }
                FPRINTF(stderr,
                        "%s: PR_Poll returned 0x%02x for socket out_flags.\n",
                        progName, pollset[SSOCK_FD].out_flags);
                if (filesReady == 0) { /* shouldn't happen! */
                    SECU_PrintError(progName, "%s: PR_Poll returned zero!\n");
                    return SECFailure;
                }
                status = PR_GetConnectStatus(pollset);
                if (status == PR_SUCCESS) {
                    break;
                }
                if (PR_GetError() != PR_IN_PROGRESS_ERROR) {
                    SECU_PrintError(progName, "unable to connect (poll)");
                    return SECFailure;
                }
                SECU_PrintError(progName, "poll");
                milliPause(50 * multiplier);
            }
        } else {
            SECU_PrintError(progName, "unable to connect");
            return SECFailure;
        }
    }

    return SECSuccess;
}

static SECStatus
importPsk(PRFileDesc *s)
{
    SECU_PrintAsHex(stdout, &psk, "Using External PSK", 0);
    PK11SlotInfo *slot = NULL;
    PK11SymKey *symKey = NULL;
    slot = PK11_GetInternalSlot();
    if (!slot) {
        SECU_PrintError(progName, "PK11_GetInternalSlot failed");
        return SECFailure;
    }
    symKey = PK11_ImportSymKey(slot, CKM_HKDF_KEY_GEN, PK11_OriginUnwrap,
                               CKA_DERIVE, &psk, NULL);
    PK11_FreeSlot(slot);
    if (!symKey) {
        SECU_PrintError(progName, "PK11_ImportSymKey failed");
        return SECFailure;
    }

    SECStatus rv = SSL_AddExternalPsk(s, symKey, (const PRUint8 *)pskLabel.data,
                                      pskLabel.len, ssl_hash_sha256);
    PK11_FreeSymKey(symKey);
    return rv;
}

static SECStatus
printEchRetryConfigs(PRFileDesc *s)
{
    if (PORT_GetError() == SSL_ERROR_ECH_RETRY_WITH_ECH) {
        SECItem retries = { siBuffer, NULL, 0 };
        SECStatus rv = SSL_GetEchRetryConfigs(s, &retries);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "SSL_GetEchRetryConfigs failed");
            return SECFailure;
        }
        char *retriesBase64 = NSSBase64_EncodeItem(NULL, NULL, 0, &retries);
        if (!retriesBase64) {
            SECU_PrintError(progName, "NSSBase64_EncodeItem on retry_configs failed");
            SECITEM_FreeItem(&retries, PR_FALSE);
            return SECFailure;
        }

        // Remove the newline characters that NSSBase64_EncodeItem unhelpfully inserts.
        char *newline = strstr(retriesBase64, "\r\n");
        if (newline) {
            memmove(newline, newline + 2, strlen(newline + 2) + 1);
        }
        fprintf(stderr, "Received ECH retry_configs: \n%s\n", retriesBase64);
        PORT_Free(retriesBase64);
        SECITEM_FreeItem(&retries, PR_FALSE);
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

    unsigned long outputLenUL = output->len;
    int ret = compress(output->data, &outputLenUL, input->data, input->len);
    output->len = outputLenUL;
    if (ret != Z_OK) {
        PR_SetError(SEC_ERROR_LIBRARY_FAILURE, 0);
        return SECFailure;
    }

    return SECSuccess;
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

    unsigned long outputLenUL = outputLen;
    int ret = uncompress(output, &outputLenUL, input->data, input->len);
    *usedLen = outputLenUL;
    if (ret != Z_OK) {
        PR_SetError(SEC_ERROR_BAD_DATA, 0);
        return SECFailure;
    }

    return SECSuccess;
}

static int
run()
{
    int headerSeparatorPtrnId = 0;
    int error = 0;
    SECStatus rv;
    PRStatus status;
    PRInt32 filesReady;
    PRFileDesc *s = NULL;
    PRFileDesc *std_out;
    PRPollDesc pollset[2] = { { 0 }, { 0 } };
    PRBool wrStarted = PR_FALSE;
    SSLCertificateCompressionAlgorithm zlibCompressionAlg = { 1, "zlib", zlibCertificateEncode, zlibCertificateDecode };

    handshakeComplete = PR_FALSE;

    /* Create socket */
    if (useDTLS) {
        s = PR_OpenUDPSocket(addr.raw.family);
    } else {
        s = PR_OpenTCPSocket(addr.raw.family);
    }

    if (s == NULL) {
        SECU_PrintError(progName, "error creating socket");
        error = 1;
        goto done;
    }

    if (actAsServer) {
        if (bindToClient(s) != SECSuccess) {
            return 1;
        }
    }
    opt.option = PR_SockOpt_Nonblocking;
    opt.value.non_blocking = PR_TRUE; /* default */
    status = PR_SetSocketOption(s, &opt);
    if (status != PR_SUCCESS) {
        SECU_PrintError(progName, "error setting socket options");
        error = 1;
        goto done;
    }

    if (useDTLS) {
        s = DTLS_ImportFD(NULL, s);
    } else {
        s = SSL_ImportFD(NULL, s);
    }
    if (s == NULL) {
        SECU_PrintError(progName, "error importing socket");
        error = 1;
        goto done;
    }
    SSL_SetPKCS11PinArg(s, &pwdata);

    rv = SSL_OptionSet(s, SSL_SECURITY, 1);
    if (rv != SECSuccess) {
        SECU_PrintError(progName, "error enabling socket");
        error = 1;
        goto done;
    }

    rv = SSL_OptionSet(s, actAsServer ? SSL_HANDSHAKE_AS_SERVER : SSL_HANDSHAKE_AS_CLIENT, 1);
    if (rv != SECSuccess) {
        SECU_PrintError(progName, "error enabling client handshake");
        error = 1;
        goto done;
    }

    /* all SSL3 cipher suites are enabled by default. */
    if (cipherString) {
        char *cstringSaved = cipherString;
        int ndx;

        while (0 != (ndx = *cipherString++)) {
            int cipher = 0;

            if (ndx == ':') {
                int ctmp = 0;

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
                if (!isalpha((unsigned char)ndx))
                    Usage();
                ndx = tolower((unsigned char)ndx) - 'a';
                if (ndx < PR_ARRAY_SIZE(ssl3CipherSuites)) {
                    cipher = ssl3CipherSuites[ndx];
                }
            }
            if (cipher > 0) {
                rv = SSL_CipherPrefSet(s, cipher, SSL_ALLOWED);
                if (rv != SECSuccess) {
                    SECU_PrintError(progName, "SSL_CipherPrefSet()");
                    error = 1;
                    goto done;
                }
            } else {
                Usage();
            }
        }
        PORT_Free(cstringSaved);
    }

    rv = SSL_VersionRangeSet(s, &enabledVersions);
    if (rv != SECSuccess) {
        SECU_PrintError(progName, "error setting SSL/TLS version range ");
        error = 1;
        goto done;
    }

    /* disable SSL socket locking */
    rv = SSL_OptionSet(s, SSL_NO_LOCKS, disableLocking);
    if (rv != SECSuccess) {
        SECU_PrintError(progName, "error disabling SSL socket locking");
        error = 1;
        goto done;
    }

    /* enable Session Ticket extension. */
    rv = SSL_OptionSet(s, SSL_ENABLE_SESSION_TICKETS, enableSessionTickets);
    if (rv != SECSuccess) {
        SECU_PrintError(progName, "error enabling Session Ticket extension");
        error = 1;
        goto done;
    }

    /* enable false start. */
    rv = SSL_OptionSet(s, SSL_ENABLE_FALSE_START, enableFalseStart);
    if (rv != SECSuccess) {
        SECU_PrintError(progName, "error enabling false start");
        error = 1;
        goto done;
    }

    if (forceFallbackSCSV) {
        rv = SSL_OptionSet(s, SSL_ENABLE_FALLBACK_SCSV, PR_TRUE);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "error forcing fallback scsv");
            error = 1;
            goto done;
        }
    }

    /* enable cert status (OCSP stapling). */
    rv = SSL_OptionSet(s, SSL_ENABLE_OCSP_STAPLING, enableCertStatus);
    if (rv != SECSuccess) {
        SECU_PrintError(progName, "error enabling cert status (OCSP stapling)");
        error = 1;
        goto done;
    }

    /* enable negotiation of delegated credentials (draft-ietf-tls-subcerts) */
    rv = SSL_OptionSet(s, SSL_ENABLE_DELEGATED_CREDENTIALS, enableDelegatedCredentials);
    if (rv != SECSuccess) {
        SECU_PrintError(progName, "error enabling delegated credentials");
        error = 1;
        goto done;
    }

    /* enable extended master secret mode */
    if (enableExtendedMasterSecret) {
        rv = SSL_OptionSet(s, SSL_ENABLE_EXTENDED_MASTER_SECRET, PR_TRUE);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "error enabling extended master secret");
            error = 1;
            goto done;
        }
    }

    /* enable 0-RTT (TLS 1.3 only) */
    if (enableZeroRtt) {
        rv = SSL_OptionSet(s, SSL_ENABLE_0RTT_DATA, PR_TRUE);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "error enabling 0-RTT");
            error = 1;
            goto done;
        }
    }

    /* Alternate ServerHello content type (TLS 1.3 only) */
    if (enableAltServerHello) {
        rv = SSL_UseAltServerHelloType(s, PR_TRUE);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "error enabling alternate ServerHello type");
            error = 1;
            goto done;
        }
    }

    /* Middlebox compatibility mode (TLS 1.3 only) */
    if (middleboxCompatMode) {
        rv = SSL_OptionSet(s, SSL_ENABLE_TLS13_COMPAT_MODE, PR_TRUE);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "error enabling middlebox compatibility mode");
            error = 1;
            goto done;
        }
    }

    /* require the use of fixed finite-field DH groups */
    if (requireDHNamedGroups) {
        rv = SSL_OptionSet(s, SSL_REQUIRE_DH_NAMED_GROUPS, PR_TRUE);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "error in requiring the use of fixed finite-field DH groups");
            error = 1;
            goto done;
        }
    }

    /* enable Signed Certificate Timestamps. */
    rv = SSL_OptionSet(s, SSL_ENABLE_SIGNED_CERT_TIMESTAMPS,
                       enableSignedCertTimestamps);
    if (rv != SECSuccess) {
        SECU_PrintError(progName, "error enabling signed cert timestamps");
        error = 1;
        goto done;
    }

    if (enablePostHandshakeAuth) {
        rv = SSL_OptionSet(s, SSL_ENABLE_POST_HANDSHAKE_AUTH, PR_TRUE);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "error enabling post-handshake auth");
            error = 1;
            goto done;
        }
    }

    if (enableRfc8701Grease) {
        rv = SSL_OptionSet(s, SSL_ENABLE_GREASE, PR_TRUE);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "error enabling grease");
            error = 1;
            goto done;
        }
    }

    if (enableCHExtensionPermuation) {
        rv = SSL_OptionSet(s, SSL_ENABLE_CH_EXTENSION_PERMUTATION, PR_TRUE);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "error enabling ch extension permutation");
            error = 1;
            goto done;
        }
    }

    if (zlibCertificateCompression) {
        rv = SSL_SetCertificateCompressionAlgorithm(s, zlibCompressionAlg);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "error setting zlib certificate compression algorithm");
            error = 1;
            goto done;
        }
    }

    if (enabledGroups) {
        rv = SSL_NamedGroupConfig(s, enabledGroups, enabledGroupsCount);
        if (rv < 0) {
            SECU_PrintError(progName, "SSL_NamedGroupConfig failed");
            error = 1;
            goto done;
        }
    }

    if (enabledSigSchemes) {
        rv = SSL_SignatureSchemePrefSet(s, enabledSigSchemes, enabledSigSchemeCount);
        if (rv < 0) {
            SECU_PrintError(progName, "SSL_SignatureSchemePrefSet failed");
            error = 1;
            goto done;
        }
    }

    if (echConfigs) {
        SECItem echConfigsBin = { siBuffer, NULL, 0 };

        if (!NSSBase64_DecodeBuffer(NULL, &echConfigsBin, echConfigs,
                                    strlen(echConfigs))) {
            SECU_PrintError(progName, "ECHConfigs record is invalid base64");
            error = 1;
            goto done;
        }

        rv = SSL_SetClientEchConfigs(s, echConfigsBin.data, echConfigsBin.len);
        SECITEM_FreeItem(&echConfigsBin, PR_FALSE);
        if (rv < 0) {
            SECU_PrintError(progName, "SSL_SetClientEchConfigs failed");
            error = 1;
            goto done;
        }
    }

    if (echGreaseSize) {
        rv = SSL_EnableTls13GreaseEch(s, PR_TRUE);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "SSL_EnableTls13GreaseEch failed");
            error = 1;
            goto done;
        }
        rv = SSL_SetTls13GreaseEchSize(s, echGreaseSize);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "SSL_SetTls13GreaseEchSize failed");
            error = 1;
            goto done;
        }
    }

    if (psk.data) {
        rv = importPsk(s);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "importPsk failed");
            error = 1;
            goto done;
        }
    }

    serverCertAuth.dbHandle = CERT_GetDefaultCertDB();

    SSL_AuthCertificateHook(s, ownAuthCertificate, &serverCertAuth);
    if (override) {
        SSL_BadCertHook(s, ownBadCertHandler, NULL);
    }
    if (actAsServer) {
        rv = installServerCertificate(s, nickname);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "error installing server cert");
            return 1;
        }
        rv = SSL_ConfigServerSessionIDCache(1024, 0, 0, ".");
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "error configuring session cache");
            return 1;
        }
        initializedServerSessionCache = PR_TRUE;
    } else {
        SSL_GetClientAuthDataHook(s, own_GetClientAuthData, (void *)nickname);
    }
    SSL_HandshakeCallback(s, handshakeCallback, hs2SniHostName);
    if (hs1SniHostName) {
        SSL_SetURL(s, hs1SniHostName);
    } else {
        SSL_SetURL(s, host);
    }

    if (actAsServer) {
        rv = SSL_ResetHandshake(s, PR_TRUE /* server */);
        if (rv != SECSuccess) {
            return 1;
        }
    } else {
        /* Try to connect to the server */
        rv = connectToServer(s, pollset);
        if (rv != SECSuccess) {
            error = 1;
            goto done;
        }
    }

    pollset[SSOCK_FD].fd = s;
    pollset[SSOCK_FD].in_flags = PR_POLL_EXCEPT;
    if (!actAsServer)
        pollset[SSOCK_FD].in_flags |= (clientSpeaksFirst ? 0 : PR_POLL_READ);
    else
        pollset[SSOCK_FD].in_flags |= PR_POLL_READ;
    if (requestFile) {
        pollset[STDIN_FD].fd = PR_Open(requestFile, PR_RDONLY, 0);
        if (!pollset[STDIN_FD].fd) {
            fprintf(stderr, "%s: unable to open input file: %s\n",
                    progName, requestFile);
            error = 1;
            goto done;
        }
    } else {
        pollset[STDIN_FD].fd = PR_GetSpecialFD(PR_StandardInput);
    }
    pollset[STDIN_FD].in_flags = PR_POLL_READ;
    std_out = PR_GetSpecialFD(PR_StandardOutput);

#if defined(WIN32)
    /* PR_Poll cannot be used with stdin on Windows or OS/2.  (sigh).
    ** But use of PR_Poll and non-blocking sockets is a major feature
    ** of this program.  So, we simulate a pollable stdin with a
    ** TCP socket pair and a  thread that reads stdin and writes to
    ** that socket pair.
    */
    {
        PRFileDesc *fds[2];
        PRThread *thread;

        int nspr_rv = PR_NewTCPSocketPair(fds);
        if (nspr_rv != PR_SUCCESS) {
            SECU_PrintError(progName, "PR_NewTCPSocketPair failed");
            error = 1;
            goto done;
        }
        pollset[STDIN_FD].fd = fds[1];

        thread = PR_CreateThread(PR_USER_THREAD, thread_main, fds[0],
                                 PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                 PR_UNJOINABLE_THREAD, 0);
        if (!thread) {
            SECU_PrintError(progName, "PR_CreateThread failed");
            error = 1;
            goto done;
        }
    }
#endif

    /*
    ** Select on stdin and on the socket. Write data from stdin to
    ** socket, read data from socket and write to stdout.
    */
    requestToExit = PR_FALSE;
    FPRINTF(stderr, "%s: ready...\n", progName);
    while (!requestToExit &&
           (pollset[SSOCK_FD].in_flags || pollset[STDIN_FD].in_flags)) {
        PRUint8 buf[4000]; /* buffer for stdin */
        int nb;            /* num bytes read from stdin. */

        rv = restartHandshakeAfterServerCertIfNeeded(s, &serverCertAuth,
                                                     override);
        if (rv != SECSuccess) {
            error = EXIT_CODE_HANDSHAKE_FAILED;
            SECU_PrintError(progName, "authentication of server cert failed");
            goto done;
        }

        rv = restartHandshakeAfterClientCertIfNeeded(s);
        if (rv == SECSuccess) {
            continue;
        }

        pollset[SSOCK_FD].out_flags = 0;
        pollset[STDIN_FD].out_flags = 0;

        FPRINTF(stderr, "%s: about to call PR_Poll !\n", progName);
        filesReady = PR_Poll(pollset, PR_ARRAY_SIZE(pollset),
                             PR_INTERVAL_NO_TIMEOUT);
        if (filesReady < 0) {
            SECU_PrintError(progName, "select failed");
            error = 1;
            goto done;
        }
        if (filesReady == 0) { /* shouldn't happen! */
            FPRINTF(stderr, "%s: PR_Poll returned zero!\n", progName);
            error = 1;
            goto done;
        }
        FPRINTF(stderr, "%s: PR_Poll returned!\n", progName);
        if (pollset[STDIN_FD].in_flags) {
            FPRINTF(stderr,
                    "%s: PR_Poll returned 0x%02x for stdin out_flags.\n",
                    progName, pollset[STDIN_FD].out_flags);
        }
        if (pollset[SSOCK_FD].in_flags) {
            FPRINTF(stderr,
                    "%s: PR_Poll returned 0x%02x for socket out_flags.\n",
                    progName, pollset[SSOCK_FD].out_flags);
        }
        if (pollset[STDIN_FD].out_flags & PR_POLL_READ) {
            /* Read from stdin and write to socket */
            nb = PR_Read(pollset[STDIN_FD].fd, buf, sizeof(buf));
            FPRINTF(stderr, "%s: stdin read %d bytes\n", progName, nb);
            if (nb < 0) {
                if (PR_GetError() != PR_WOULD_BLOCK_ERROR) {
                    SECU_PrintError(progName, "read from stdin failed");
                    error = 1;
                    break;
                }
            } else if (nb == 0) {
                /* EOF on stdin, stop polling stdin for read. */
                pollset[STDIN_FD].in_flags = 0;
                if (actAsServer)
                    requestToExit = PR_TRUE;
            } else {
                error = writeBytesToServer(s, buf, nb);
                if (error) {
                    if (echConfigs) {
                        (void)printEchRetryConfigs(s);
                    }
                    goto done;
                }
                pollset[SSOCK_FD].in_flags = PR_POLL_READ;
            }
        }

        if (pollset[SSOCK_FD].in_flags) {
            FPRINTF(stderr,
                    "%s: PR_Poll returned 0x%02x for socket out_flags.\n",
                    progName, pollset[SSOCK_FD].out_flags);
        }
#ifdef PR_POLL_HUP
#define POLL_RECV_FLAGS (PR_POLL_READ | PR_POLL_ERR | PR_POLL_HUP)
#else
#define POLL_RECV_FLAGS (PR_POLL_READ | PR_POLL_ERR)
#endif
        if (pollset[SSOCK_FD].out_flags & POLL_RECV_FLAGS) {
            /* Read from socket and write to stdout */
            nb = PR_Recv(pollset[SSOCK_FD].fd, buf, sizeof buf, 0, maxInterval);
            FPRINTF(stderr, "%s: Read from server %d bytes\n", progName, nb);
            if (nb < 0) {
                if (PR_GetError() != PR_WOULD_BLOCK_ERROR) {
                    SECU_PrintError(progName, "read from socket failed");
                    error = 1;
                    goto done;
                }
            } else if (nb == 0) {
                /* EOF from socket... stop polling socket for read */
                pollset[SSOCK_FD].in_flags = 0;
            } else {
                if (skipProtoHeader != PR_TRUE || wrStarted == PR_TRUE) {
                    PR_Write(std_out, buf, nb);
                } else {
                    separateReqHeader(std_out, (char *)buf, nb, &wrStarted,
                                      &headerSeparatorPtrnId);
                }
                if (verbose)
                    fputs("\n\n", stderr);
            }
        }
        milliPause(50 * multiplier);
    }

done:
    if (s) {
        PR_Close(s);
    }
    if (requestFile && pollset[STDIN_FD].fd) {
        PR_Close(pollset[STDIN_FD].fd);
    }
    return error;
}

const char *cmdLineOptions = "46A:BCDEGHI:J:KL:N:OP:QR:STUV:W:X:YZa:bc:d:efgh:i:m:n:op:qr:st:uvw:x:z:";
const PLLongOpt cmdLineLongOpts[] = {
    { .longOptName = "enable-rfc8701-grease", .longOption = 0, .valueRequired = PR_FALSE },
    { .longOptName = "enable-ch-extension-permutation", .longOption = 1, .valueRequired = PR_FALSE },
    { .longOptName = "zlib-certificate-compression", .longOption = 2, .valueRequired = PR_FALSE },
    { .longOptName = NULL },
};

int
main(int argc, char **argv)
{
    PLOptState *optstate;
    PLOptStatus optstatus;
    PRStatus status;
    PRStatus prStatus;
    int error = 0;
    char *tmp;
    SECStatus rv;
    char *certDir = NULL;
    PRBool openDB = PR_TRUE;
    PRBool loadDefaultRootCAs = PR_FALSE;
    char *rootModule = NULL;
    int numConnections = 1;
    PRFileDesc *s = NULL;

    serverCertAuth.shouldPause = PR_TRUE;
    serverCertAuth.isPaused = PR_FALSE;
    serverCertAuth.dbHandle = NULL;

    progName = strrchr(argv[0], '/');
    if (!progName)
        progName = strrchr(argv[0], '\\');
    progName = progName ? progName + 1 : argv[0];

    tmp = PR_GetEnvSecure("NSS_DEBUG_TIMEOUT");
    if (tmp && tmp[0]) {
        int sec = PORT_Atoi(tmp);
        if (sec > 0) {
            maxInterval = PR_SecondsToInterval(sec);
        }
    }

    optstate = PL_CreateLongOptState(argc, argv, cmdLineOptions, cmdLineLongOpts);
    while ((optstatus = PL_GetNextOpt(optstate)) == PL_OPT_OK) {
        switch (optstate->longOption) {
            case '?':
            default:
                Usage();
                break;

            case 0:
                enableRfc8701Grease = PR_TRUE;
                break;

            case 1:
                enableCHExtensionPermuation = PR_TRUE;
                break;

            case 2:
                zlibCertificateCompression = PR_TRUE;
                break;

            case '4':
                allowIPv6 = PR_FALSE;
                if (!allowIPv4)
                    Usage();
                break;
            case '6':
                allowIPv4 = PR_FALSE;
                if (!allowIPv6)
                    Usage();
                break;

            case 'A':
                requestFile = PORT_Strdup(optstate->value);
                break;

            case 'B':
                enableDelegatedCredentials = PR_TRUE;
                break;

            case 'C':
                ++dumpServerChain;
                break;

            case 'D':
                openDB = PR_FALSE;
                break;

            case 'E':
                enablePostHandshakeAuth = PR_TRUE;
                break;

            case 'G':
                enableExtendedMasterSecret = PR_TRUE;
                break;

            case 'H':
                requireDHNamedGroups = PR_TRUE;
                break;

            case 'O':
                clientCertAsyncSelect = PR_FALSE;
                serverCertAuth.shouldPause = PR_FALSE;
                break;

            case 'K':
                forceFallbackSCSV = PR_TRUE;
                break;

            case 'L':
                numConnections = atoi(optstate->value);
                break;

            case 'N':
                echConfigs = PORT_Strdup(optstate->value);
                break;

            case 'i':
                echGreaseSize = PORT_Atoi(optstate->value);
                if (!echGreaseSize || echGreaseSize > 255) {
                    fprintf(stderr, "ECH Grease size must be within 1..255 (inclusive).\n");
                    exit(-1);
                }
                break;

            case 'P':
                useDTLS = PR_TRUE;
                if (!strcmp(optstate->value, "server")) {
                    actAsServer = 1;
                } else {
                    if (strcmp(optstate->value, "client")) {
                        Usage();
                    }
                }
                break;

            case 'Q':
                stopAfterHandshake = PR_TRUE;
                break;

            case 'R':
                rootModule = PORT_Strdup(optstate->value);
                break;

            case 'S':
                skipProtoHeader = PR_TRUE;
                break;

            case 'T':
                enableCertStatus = 1;
                break;

            case 'U':
                enableSignedCertTimestamps = 1;
                break;

            case 'V':
                versionString = PORT_Strdup(optstate->value);
                break;

            case 'X':
                if (!strcmp(optstate->value, "alt-server-hello")) {
                    enableAltServerHello = PR_TRUE;
                } else {
                    Usage();
                }
                break;
            case 'Y':
                PrintCipherUsage();
                exit(0);
                break;

            case 'Z':
                enableZeroRtt = PR_TRUE;
                zeroRttData = PORT_ZAlloc(ZERO_RTT_MAX);
                if (!zeroRttData) {
                    fprintf(stderr, "Unable to allocate buffer for 0-RTT\n");
                    exit(1);
                }
                break;

            case 'a':
                if (!hs1SniHostName) {
                    hs1SniHostName = PORT_Strdup(optstate->value);
                } else if (!hs2SniHostName) {
                    hs2SniHostName = PORT_Strdup(optstate->value);
                } else {
                    Usage();
                }
                break;

            case 'b':
                loadDefaultRootCAs = PR_TRUE;
                break;

            case 'c':
                cipherString = PORT_Strdup(optstate->value);
                break;

            case 'g':
                enableFalseStart = 1;
                break;

            case 'd':
                certDir = PORT_Strdup(optstate->value);
                break;

            case 'e':
                middleboxCompatMode = PR_TRUE;
                break;

            case 'f':
                clientSpeaksFirst = PR_TRUE;
                break;

            case 'h':
                host = PORT_Strdup(optstate->value);
                break;

            case 'm':
                multiplier = atoi(optstate->value);
                if (multiplier < 0)
                    multiplier = 0;
                break;

            case 'n':
                nickname = PORT_Strdup(optstate->value);
                break;

            case 'o':
                override = 1;
                break;

            case 'p':
                portno = (PRUint16)atoi(optstate->value);
                break;

            case 'q':
                pingServerFirst = PR_TRUE;
                break;

            case 's':
                disableLocking = 1;
                break;

            case 't':
                pingTimeoutSeconds = atoi(optstate->value);
                break;

            case 'u':
                enableSessionTickets = PR_TRUE;
                break;

            case 'v':
                verbose++;
                break;

            case 'r':
                renegotiationsToDo = atoi(optstate->value);
                break;

            case 'w':
                pwdata.source = PW_PLAINTEXT;
                pwdata.data = PORT_Strdup(optstate->value);
                break;

            case 'W':
                pwdata.source = PW_FROMFILE;
                pwdata.data = PORT_Strdup(optstate->value);
                break;

            case 'I':
                rv = parseGroupList(optstate->value, &enabledGroups, &enabledGroupsCount);
                if (rv != SECSuccess) {
                    PL_DestroyOptState(optstate);
                    fprintf(stderr, "Bad group specified.\n");
                    Usage();
                }
                break;

            case 'J':
                rv = parseSigSchemeList(optstate->value, &enabledSigSchemes, &enabledSigSchemeCount);
                if (rv != SECSuccess) {
                    PL_DestroyOptState(optstate);
                    fprintf(stderr, "Bad signature scheme specified.\n");
                    Usage();
                }
                break;

            case 'x':
                rv = parseExporters(optstate->value,
                                    &enabledExporters,
                                    &enabledExporterCount);
                if (rv != SECSuccess) {
                    PL_DestroyOptState(optstate);
                    fprintf(stderr, "Bad exporter specified.\n");
                    Usage();
                }
                break;

            case 'z':
                rv = readPSK(optstate->value, &psk, &pskLabel);
                if (rv != SECSuccess) {
                    PL_DestroyOptState(optstate);
                    fprintf(stderr, "Bad PSK specified.\n");
                    Usage();
                }
                break;
        }
    }
    PL_DestroyOptState(optstate);

    SSL_VersionRangeGetSupported(useDTLS ? ssl_variant_datagram : ssl_variant_stream, &enabledVersions);

    if (versionString) {
        if (SECU_ParseSSLVersionRangeString(versionString,
                                            enabledVersions, &enabledVersions) !=
            SECSuccess) {
            fprintf(stderr, "Bad version specified.\n");
            Usage();
        }
        PORT_Free(versionString);
    }

    if (optstatus == PL_OPT_BAD) {
        Usage();
    }

    if (!host || !portno) {
        fprintf(stderr, "%s: parameters -h and -p are mandatory\n", progName);
        Usage();
    }

    if (certDir && !openDB) {
        fprintf(stderr, "%s: Cannot combine parameters -D and -d\n", progName);
        exit(1);
    }

    if (rootModule && loadDefaultRootCAs) {
        fprintf(stderr, "%s: Cannot combine parameters -b and -R\n", progName);
        exit(1);
    }

    if (enablePostHandshakeAuth && !nickname) {
        fprintf(stderr, "%s: -E requires the use of -n\n", progName);
        exit(1);
    }

    PR_Init(PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);

    PK11_SetPasswordFunc(SECU_GetModulePassword);
    memset(&addr, 0, sizeof(addr));
    status = PR_StringToNetAddr(host, &addr);
    if (status == PR_SUCCESS) {
        addr.inet.port = PR_htons(portno);
    } else {
        PRBool gotLoopbackIP = PR_FALSE;
        if (!strcmp(host, "localhost") || !strcmp(host, "localhost.localdomain")) {
            if (allowIPv4 && allowIPv6) {
                /* Both families allowed: use PR_GetPrefLoopbackAddrInfo to
                 * choose the preferred loopback address without a DNS lookup. */
                status = PR_GetPrefLoopbackAddrInfo(&addr, portno);
                if (status != PR_FAILURE) {
                    gotLoopbackIP = PR_TRUE;
                }
            }
            if (!gotLoopbackIP && (allowIPv4 || allowIPv6)) {
                /* Only one address family is requested, or
                 * PR_GetPrefLoopbackAddrInfo failed.  Set the loopback
                 * address directly without a DNS lookup. */
                status = PR_StringToNetAddr(allowIPv6 ? "::1" : "127.0.0.1",
                                            &addr);
                if (status == PR_SUCCESS) {
                    addr.inet.port = PR_htons(portno);
                    gotLoopbackIP = PR_TRUE;
                }
            }
        }

        if (!gotLoopbackIP) {
            /* Lookup host */
            PRAddrInfo *addrInfo;
            void *enumPtr = NULL;

            addrInfo = PR_GetAddrInfoByName(host, PR_AF_UNSPEC,
                                            PR_AI_ADDRCONFIG | PR_AI_NOCANONNAME);
            if (!addrInfo) {
                fprintf(stderr, "HOSTNAME=%s\n", host);
                SECU_PrintError(progName, "error looking up host");
                error = 1;
                goto done;
            }
            for (;;) {
                enumPtr = PR_EnumerateAddrInfo(enumPtr, addrInfo, portno, &addr);
                if (enumPtr == NULL)
                    break;
                if (addr.raw.family == PR_AF_INET && allowIPv4)
                    break;
                if (addr.raw.family == PR_AF_INET6 && allowIPv6)
                    break;
            }
            PR_FreeAddrInfo(addrInfo);
            if (enumPtr == NULL) {
                SECU_PrintError(progName, "error looking up host address");
                error = 1;
                goto done;
            }
        }
    }

    printHostNameAndAddr(host, &addr);

    if (!certDir) {
        certDir = SECU_DefaultSSLDir(); /* Look in $SSL_DIR */
        certDir = SECU_ConfigDirectory(certDir);
    } else {
        char *certDirTmp = certDir;
        certDir = SECU_ConfigDirectory(certDirTmp);
        PORT_Free(certDirTmp);
    }

    if (pingServerFirst) {
        int iter = 0;
        PRErrorCode err;

        int max_attempts = MAX_WAIT_FOR_SERVER;
        if (pingTimeoutSeconds >= 0) {
            /* If caller requested a timeout, let's try just twice. */
            max_attempts = 2;
        }
        do {
            PRIntervalTime timeoutInterval = PR_INTERVAL_NO_TIMEOUT;
            s = PR_OpenTCPSocket(addr.raw.family);
            if (s == NULL) {
                SECU_PrintError(progName, "Failed to create a TCP socket");
                error = 1;
                goto done;
            }
            opt.option = PR_SockOpt_Nonblocking;
            opt.value.non_blocking = PR_FALSE;
            prStatus = PR_SetSocketOption(s, &opt);
            if (prStatus != PR_SUCCESS) {
                SECU_PrintError(progName,
                                "Failed to set blocking socket option");
                error = 1;
                goto done;
            }
            if (pingTimeoutSeconds >= 0) {
                timeoutInterval = PR_SecondsToInterval(pingTimeoutSeconds);
            }
            prStatus = PR_Connect(s, &addr, timeoutInterval);
            if (prStatus == PR_SUCCESS) {
                PR_Shutdown(s, PR_SHUTDOWN_BOTH);
                goto done;
            }
            err = PR_GetError();
            if ((err != PR_CONNECT_REFUSED_ERROR) &&
                (err != PR_CONNECT_RESET_ERROR)) {
                SECU_PrintError(progName, "TCP Connection failed");
                error = 1;
                goto done;
            }
            PR_Close(s);
            s = NULL;
            PR_Sleep(PR_MillisecondsToInterval(WAIT_INTERVAL));
        } while (++iter < max_attempts);
        SECU_PrintError(progName,
                        "Client timed out while waiting for connection to server");
        error = 1;
        goto done;
    }

    /* open the cert DB, the key DB, and the secmod DB. */
    if (openDB) {
        rv = NSS_Init(certDir);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "unable to open cert database");
            error = 1;
            goto done;
        }
    } else {
        rv = NSS_NoDB_Init(NULL);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "failed to initialize NSS");
            error = 1;
            goto done;
        }
    }

    if (loadDefaultRootCAs) {
        SECMOD_AddNewModule("Builtins",
                            DLL_PREFIX "nssckbi." DLL_SUFFIX, 0, 0);
    } else if (rootModule) {
        SECMOD_AddNewModule("Builtins", rootModule, 0, 0);
    }

    /* all SSL3 cipher suites are enabled by default. */
    if (cipherString) {
        /* disable all the ciphers, then enable the ones we want. */
        disableAllSSLCiphers();
    }

    while (numConnections--) {
        error = run();
        if (error) {
            goto done;
        }
    }

done:
    if (s) {
        PR_Close(s);
    }

    PORT_Free((void *)requestFile);
    PORT_Free(hs1SniHostName);
    PORT_Free(hs2SniHostName);
    PORT_Free(nickname);
    PORT_Free(pwdata.data);
    PORT_Free(host);
    PORT_Free(zeroRttData);
    PORT_Free(echConfigs);
    SECITEM_ZfreeItem(&psk, PR_FALSE);
    SECITEM_ZfreeItem(&pskLabel, PR_FALSE);

    if (enabledGroups) {
        PORT_Free(enabledGroups);
    }
    if (NSS_IsInitialized()) {
        SSL_ClearSessionCache();
        if (initializedServerSessionCache) {
            if (SSL_ShutdownServerSessionIDCache() != SECSuccess) {
                error = 1;
            }
        }

        if (NSS_Shutdown() != SECSuccess) {
            error = 1;
        }
    }

    FPRINTF(stderr, "tstclnt: exiting with return code %d\n", error);
    PR_Cleanup();
    return error;
}
