/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

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
#include "nss.h"
#include "nssb64.h"
#include "sechash.h"
#include "cert.h"
#include "certdb.h"
#include "ocsp.h"
#include "ocspti.h"
#include "ocspi.h"

#ifndef PORT_Strstr
#define PORT_Strstr strstr
#endif

#ifndef PORT_Malloc
#define PORT_Malloc PR_Malloc
#endif

static int handle_connection(PRFileDesc *, PRFileDesc *, int);

/* data and structures for shutdown */
static int stopping;

static PRBool noDelay;
static int verbose;

static PRThread *acceptorThread;

#ifdef PR_LOGGING
static PRLogModuleInfo *lm;
#endif

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
Usage(const char *progName)
{
    fprintf(stderr,

            "Usage: %s -p port [-Dbv]\n"
            "         [-t threads] [-i pid_file]\n"
            "         [-A nickname -C crl-filename]... [-O method]\n"
            "         [-d dbdir] [-f password_file] [-w password] [-P dbprefix]\n"
            "-D means disable Nagle delays in TCP\n"
            "-b means try binding to the port and exit\n"
            "-v means verbose output\n"
            "-t threads -- specify the number of threads to use for connections.\n"
            "-i pid_file file to write the process id of httpserv\n"
            "Parameters -A, -C and -O are used to provide an OCSP server at /ocsp?\n"
            "-A a nickname of a CA certificate\n"
            "-C a CRL filename corresponding to the preceding CA nickname\n"
            "-O allowed HTTP methods for OCSP requests: get, post, all, random, get-unknown\n"
            "   random means: randomly fail if request method is GET, POST always works\n"
            "   get-unknown means: status unknown for GET, correct status for POST\n"
            "Multiple pairs of parameters -A and -C are allowed.\n"
            "If status for a cert from an unknown CA is requested, the cert from the\n"
            "first -A parameter will be used to sign the unknown status response.\n"
            "NSS database parameters are used only if OCSP parameters are used.\n",
            progName);
}

static const char *
errWarn(char *funcString)
{
    PRErrorCode perr = PR_GetError();
    const char *errString = SECU_Strerror(perr);

    fprintf(stderr, "httpserv: %s returned error %d:\n%s\n",
            funcString, perr, errString);
    return errString;
}

static void
errExit(char *funcString)
{
    errWarn(funcString);
    exit(3);
}

#define MAX_VIRT_SERVER_NAME_ARRAY_INDEX 10

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
    int requestCert;
} JOB;

static PRLock *qLock;             /* this lock protects all data immediately below */
static PRLock *lastLoadedCrlLock; /* this lock protects lastLoadedCrl variable */
static PRCondVar *jobQNotEmptyCv;
static PRCondVar *freeListNotEmptyCv;
static PRCondVar *threadCountChangeCv;
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

typedef int startFn(PRFileDesc *a, PRFileDesc *b, int c);

typedef enum { rs_idle = 0,
               rs_running = 1,
               rs_zombie = 2 } runState;

typedef struct perThreadStr {
    PRFileDesc *a;
    PRFileDesc *b;
    int c;
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

    slot->rv = (*slot->startFunc)(slot->a, slot->b, slot->c);

    /* notify the thread exit handler. */
    PR_Lock(qLock);
    slot->state = rs_zombie;
    --threadCount;
    PR_NotifyAllCondVar(threadCountChangeCv);
    PR_Unlock(qLock);
}

int
jobLoop(PRFileDesc *a, PRFileDesc *b, int c)
{
    PRCList *myLink = 0;
    JOB *myJob;

    PR_Lock(qLock);
    do {
        myLink = 0;
        while (PR_CLIST_IS_EMPTY(&jobQ) && !stopping) {
            PR_WaitCondVar(jobQNotEmptyCv, PR_INTERVAL_NO_TIMEOUT);
        }
        if (!PR_CLIST_IS_EMPTY(&jobQ)) {
            myLink = PR_LIST_HEAD(&jobQ);
            PR_REMOVE_AND_INIT_LINK(myLink);
        }
        PR_Unlock(qLock);
        myJob = (JOB *)myLink;
        /* myJob will be null when stopping is true and jobQ is empty */
        if (!myJob)
            break;
        handle_connection(myJob->tcp_sock, myJob->model_sock,
                          myJob->requestCert);
        PR_Lock(qLock);
        PR_APPEND_LINK(myLink, &freeJobs);
        PR_NotifyCondVar(freeListNotEmptyCv);
    } while (PR_TRUE);
    return 0;
}

SECStatus
launch_threads(
    startFn *startFunc,
    PRFileDesc *a,
    PRFileDesc *b,
    int c,
    PRBool local)
{
    int i;
    SECStatus rv = SECSuccess;

    /* create the thread management serialization structs */
    qLock = PR_NewLock();
    jobQNotEmptyCv = PR_NewCondVar(qLock);
    freeListNotEmptyCv = PR_NewCondVar(qLock);
    threadCountChangeCv = PR_NewCondVar(qLock);

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

    PR_Lock(qLock);
    for (i = 0; i < maxThreads; ++i) {
        perThread *slot = threads + i;

        slot->state = rs_running;
        slot->a = a;
        slot->b = b;
        slot->c = c;
        slot->startFunc = startFunc;
        slot->prThread = PR_CreateThread(PR_USER_THREAD,
                                         thread_wrapper, slot, PR_PRIORITY_NORMAL,
                                         (PR_TRUE ==
                                          local)
                                             ? PR_LOCAL_THREAD
                                             : PR_GLOBAL_THREAD,
                                         PR_JOINABLE_THREAD, 0);
        if (slot->prThread == NULL) {
            printf("httpserv: Failed to launch thread!\n");
            slot->state = rs_idle;
            rv = SECFailure;
            break;
        }

        ++threadCount;
    }
    PR_Unlock(qLock);

    return rv;
}

#define DESTROY_CONDVAR(name)    \
    if (name) {                  \
        PR_DestroyCondVar(name); \
        name = NULL;             \
    }
#define DESTROY_LOCK(name)    \
    if (name) {               \
        PR_DestroyLock(name); \
        name = NULL;          \
    }

void
terminateWorkerThreads(void)
{
    int i;

    VLOG(("httpserv: server_thread: waiting on stopping"));
    PR_Lock(qLock);
    PR_NotifyAllCondVar(jobQNotEmptyCv);
    PR_Unlock(qLock);

    /* Wait for worker threads to terminate. */
    for (i = 0; i < maxThreads; ++i) {
        perThread *slot = threads + i;
        if (slot->prThread) {
            PR_JoinThread(slot->prThread);
        }
    }

    /* The worker threads empty the jobQ before they terminate. */
    PR_Lock(qLock);
    PORT_AssertArg(threadCount == 0);
    PORT_Assert(PR_CLIST_IS_EMPTY(&jobQ));
    PR_Unlock(qLock);

    DESTROY_CONDVAR(jobQNotEmptyCv);
    DESTROY_CONDVAR(freeListNotEmptyCv);
    DESTROY_CONDVAR(threadCountChangeCv);

    PR_DestroyLock(lastLoadedCrlLock);
    DESTROY_LOCK(qLock);
    PR_Free(jobTable);
    PR_Free(threads);
}

/**************************************************************************
** End   thread management routines.
**************************************************************************/

PRBool NoReuse = PR_FALSE;
PRBool disableLocking = PR_FALSE;
static secuPWData pwdata = { PW_NONE, 0 };

struct caRevoInfoStr {
    PRCList link;
    char *nickname;
    char *crlFilename;
    CERTCertificate *cert;
    CERTOCSPCertID *id;
    CERTSignedCrl *crl;
};
typedef struct caRevoInfoStr caRevoInfo;
/* Created during app init. No locks necessary,
 * because later on, only read access will occur. */
static caRevoInfo *caRevoInfos = NULL;

static enum {
    ocspGetOnly,
    ocspPostOnly,
    ocspGetAndPost,
    ocspRandomGetFailure,
    ocspGetUnknown
} ocspMethodsAllowed = ocspGetAndPost;

static const char stopCmd[] = { "GET /stop " };
static const char getCmd[] = { "GET " };
static const char outHeader[] = {
    "HTTP/1.0 200 OK\r\n"
    "Server: Generic Web Server\r\n"
    "Date: Tue, 26 Aug 1997 22:10:05 GMT\r\n"
    "Content-type: text/plain\r\n"
    "\r\n"
};
#define OUT_OCSP_HEADER_FMT                       \
    "HTTP/1.0 200 OK\r\n"                         \
    "Server: Generic OCSP Server\r\n"             \
    "Content-type: application/ocsp-response\r\n" \
    "Content-Length: %u\r\n"                      \
    "\r\n"
static const char outBadRequestHeader[] = {
    "HTTP/1.0 400 Bad Request\r\n"
    "Server: Generic OCSP Server\r\n"
    "\r\n"
};

void
stop_server()
{
    stopping = 1;
    PR_Interrupt(acceptorThread);
}

/* Will only work if the original input to url encoding was
 * a base64 encoded buffer. Will only decode the sequences used
 * for encoding the special base64 characters, and fail if any
 * other encoded chars are found.
 * Will return SECSuccess if input could be processed.
 * Coversion is done in place.
 */
static SECStatus
urldecode_base64chars_inplace(char *buf)
{
    char *walk;
    size_t remaining_bytes;

    if (!buf || !*buf)
        return SECFailure;

    walk = buf;
    remaining_bytes = strlen(buf) + 1; /* include terminator */

    while (*walk) {
        if (*walk == '%') {
            if (!PL_strncasecmp(walk, "%2B", 3)) {
                *walk = '+';
            } else if (!PL_strncasecmp(walk, "%2F", 3)) {
                *walk = '/';
            } else if (!PL_strncasecmp(walk, "%3D", 3)) {
                *walk = '=';
            } else {
                return SECFailure;
            }
            remaining_bytes -= 3;
            ++walk;
            memmove(walk, walk + 2, remaining_bytes);
        } else {
            ++walk;
            --remaining_bytes;
        }
    }
    return SECSuccess;
}

int
handle_connection(
    PRFileDesc *tcp_sock,
    PRFileDesc *model_sock,
    int requestCert)
{
    PRFileDesc *ssl_sock = NULL;
    PRFileDesc *local_file_fd = NULL;
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
    char buf[10240];
    char fileName[513];
    char *getData = NULL; /* inplace conversion */
    SECItem postData;
    PRBool isOcspRequest = PR_FALSE;
    PRBool isPost = PR_FALSE;

    postData.data = NULL;
    postData.len = 0;

    pBuf = buf;
    bufRem = sizeof buf;

    VLOG(("httpserv: handle_connection: starting"));
    opt.option = PR_SockOpt_Nonblocking;
    opt.value.non_blocking = PR_FALSE;
    PR_SetSocketOption(tcp_sock, &opt);

    VLOG(("httpserv: handle_connection: starting\n"));
    ssl_sock = tcp_sock;

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
        const char *post;
        const char *foundStr = NULL;
        const char *tmp = NULL;

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

        postData.data = (void *)(buf + reqLen);

        tmp = "content-length: ";
        foundStr = PL_strcasestr(buf, tmp);
        if (foundStr) {
            int expectedPostLen;
            int havePostLen;

            expectedPostLen = atoi(foundStr + strlen(tmp));
            havePostLen = bufDat - reqLen;
            if (havePostLen >= expectedPostLen) {
                postData.len = expectedPostLen;
                break;
            }
        } else {
            /* use legacy hack */
            /* It's a post, so look for the next and final CR/LF. */
            while (reqLen < bufDat && newln < 3) {
                int octet = buf[reqLen++];
                if (octet == '\n') {
                    newln++;
                }
            }
            if (newln == 3)
                break;
        }
    } /* read loop */

    bufDat = pBuf - buf;
    if (bufDat)
        do { /* just close if no data */
            /* Have either (a) a complete get, (b) a complete post, (c) EOF */
            if (reqLen > 0) {
                PRBool isGetOrPost = PR_FALSE;
                unsigned skipChars = 0;
                isPost = PR_FALSE;

                if (!strncmp(buf, getCmd, sizeof getCmd - 1)) {
                    isGetOrPost = PR_TRUE;
                    skipChars = 4;
                } else if (!strncmp(buf, "POST ", 5)) {
                    isGetOrPost = PR_TRUE;
                    isPost = PR_TRUE;
                    skipChars = 5;
                }

                if (isGetOrPost) {
                    char *fnBegin = buf;
                    char *fnEnd;
                    char *fnstart = NULL;
                    PRFileInfo info;

                    fnBegin += skipChars;

                    fnEnd = strpbrk(fnBegin, " \r\n");
                    if (fnEnd) {
                        int fnLen = fnEnd - fnBegin;
                        if (fnLen < sizeof fileName) {
                            strncpy(fileName, fnBegin, fnLen);
                            fileName[fnLen] = 0; /* null terminate */
                            fnstart = fileName;
                            /* strip initial / because our root is the current directory*/
                            while (*fnstart && *fnstart == '/')
                                ++fnstart;
                        }
                    }
                    if (fnstart) {
                        if (!strncmp(fnstart, "ocsp", 4)) {
                            if (isPost) {
                                if (postData.data) {
                                    isOcspRequest = PR_TRUE;
                                }
                            } else {
                                if (!strncmp(fnstart, "ocsp/", 5)) {
                                    isOcspRequest = PR_TRUE;
                                    getData = fnstart + 5;
                                }
                            }
                        } else {
                            /* try to open the file named.
                             * If successful, then write it to the client.
                             */
                            status = PR_GetFileInfo(fnstart, &info);
                            if (status == PR_SUCCESS &&
                                info.type == PR_FILE_FILE &&
                                info.size >= 0) {
                                local_file_fd = PR_Open(fnstart, PR_RDONLY, 0);
                            }
                        }
                    }
                }
            }

            numIOVs = 0;

            iovs[numIOVs].iov_base = (char *)outHeader;
            iovs[numIOVs].iov_len = (sizeof(outHeader)) - 1;
            numIOVs++;

            if (isOcspRequest && caRevoInfos) {
                CERTOCSPRequest *request = NULL;
                PRBool failThisRequest = PR_FALSE;
                PLArenaPool *arena = NULL;

                if (ocspMethodsAllowed == ocspGetOnly && postData.len) {
                    failThisRequest = PR_TRUE;
                } else if (ocspMethodsAllowed == ocspPostOnly && getData) {
                    failThisRequest = PR_TRUE;
                } else if (ocspMethodsAllowed == ocspRandomGetFailure && getData) {
                    if (!(rand() % 2)) {
                        failThisRequest = PR_TRUE;
                    }
                }

                if (failThisRequest) {
                    PR_Write(ssl_sock, outBadRequestHeader, strlen(outBadRequestHeader));
                    break;
                }
                /* get is base64, post is binary.
                 * If we have base64, convert into the (empty) postData array.
                 */
                if (getData) {
                    if (urldecode_base64chars_inplace(getData) == SECSuccess) {
                        /* The code below can handle a NULL arena */
                        arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
                        NSSBase64_DecodeBuffer(arena, &postData, getData, strlen(getData));
                    }
                }
                if (postData.len) {
                    request = CERT_DecodeOCSPRequest(&postData);
                }
                if (arena) {
                    PORT_FreeArena(arena, PR_FALSE);
                    arena = NULL;
                }
                if (!request || !request->tbsRequest ||
                    !request->tbsRequest->requestList ||
                    !request->tbsRequest->requestList[0]) {
                    snprintf(msgBuf, sizeof(msgBuf), "Cannot decode OCSP request.\r\n");

                    iovs[numIOVs].iov_base = msgBuf;
                    iovs[numIOVs].iov_len = PORT_Strlen(msgBuf);
                    numIOVs++;
                } else {
                    /* TODO: support more than one request entry */
                    CERTOCSPCertID *reqid = request->tbsRequest->requestList[0]->reqCert;
                    const caRevoInfo *revoInfo = NULL;
                    PRBool unknown = PR_FALSE;
                    PRBool revoked = PR_FALSE;
                    PRTime nextUpdate = 0;
                    PRTime revoDate = 0;
                    PRCList *caRevoIter;

                    caRevoIter = &caRevoInfos->link;
                    do {
                        CERTOCSPCertID *caid;

                        revoInfo = (caRevoInfo *)caRevoIter;
                        caid = revoInfo->id;

                        if (SECOID_CompareAlgorithmID(&reqid->hashAlgorithm,
                                                      &caid->hashAlgorithm) == SECEqual &&
                            SECITEM_CompareItem(&reqid->issuerNameHash,
                                                &caid->issuerNameHash) == SECEqual &&
                            SECITEM_CompareItem(&reqid->issuerKeyHash,
                                                &caid->issuerKeyHash) == SECEqual) {
                            break;
                        }
                        revoInfo = NULL;
                        caRevoIter = PR_NEXT_LINK(caRevoIter);
                    } while (caRevoIter != &caRevoInfos->link);

                    if (!revoInfo) {
                        unknown = PR_TRUE;
                        revoInfo = caRevoInfos;
                    } else {
                        CERTCrl *crl = &revoInfo->crl->crl;
                        CERTCrlEntry *entry = NULL;
                        DER_DecodeTimeChoice(&nextUpdate, &crl->nextUpdate);
                        if (crl->entries) {
                            int iv = 0;
                            /* assign, not compare */
                            while ((entry = crl->entries[iv++])) {
                                if (SECITEM_CompareItem(&reqid->serialNumber,
                                                        &entry->serialNumber) == SECEqual) {
                                    break;
                                }
                            }
                        }
                        if (entry) {
                            /* revoked status response */
                            revoked = PR_TRUE;
                            DER_DecodeTimeChoice(&revoDate, &entry->revocationDate);
                        } else {
                            /* else good status response */
                            if (!isPost && ocspMethodsAllowed == ocspGetUnknown) {
                                unknown = PR_TRUE;
                                nextUpdate = PR_Now() + (PRTime)60 * 60 * 24 * PR_USEC_PER_SEC; /*tomorrow*/
                                revoDate = PR_Now() - (PRTime)60 * 60 * 24 * PR_USEC_PER_SEC;   /*yesterday*/
                            }
                        }
                    }

                    {
                        PRTime now = PR_Now();
                        CERTOCSPSingleResponse *sr;
                        CERTOCSPSingleResponse **singleResponses;
                        SECItem *ocspResponse;

                        PORT_Assert(!arena);
                        arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);

                        if (unknown) {
                            sr = CERT_CreateOCSPSingleResponseUnknown(arena, reqid, now,
                                                                      &nextUpdate);
                        } else if (revoked) {
                            sr = CERT_CreateOCSPSingleResponseRevoked(arena, reqid, now,
                                                                      &nextUpdate, revoDate, NULL);
                        } else {
                            sr = CERT_CreateOCSPSingleResponseGood(arena, reqid, now,
                                                                   &nextUpdate);
                        }

                        /* meaning of value 2: one entry + one end marker */
                        singleResponses = PORT_ArenaNewArray(arena, CERTOCSPSingleResponse *, 2);
                        singleResponses[0] = sr;
                        singleResponses[1] = NULL;
                        ocspResponse = CERT_CreateEncodedOCSPSuccessResponse(arena,
                                                                             revoInfo->cert, ocspResponderID_byName, now,
                                                                             singleResponses, &pwdata);

                        if (!ocspResponse) {
                            snprintf(msgBuf, sizeof(msgBuf), "Failed to encode response\r\n");
                            iovs[numIOVs].iov_base = msgBuf;
                            iovs[numIOVs].iov_len = PORT_Strlen(msgBuf);
                            numIOVs++;
                        } else {
                            char *hdr = PR_smprintf(OUT_OCSP_HEADER_FMT,
                                                    ocspResponse->len);
                            if (hdr) {
                                PR_Write(ssl_sock, hdr, PORT_Strlen(hdr));
                                PR_Write(ssl_sock, ocspResponse->data, ocspResponse->len);
                                PR_smprintf_free(hdr);
                            }
                        }
                        PORT_FreeArena(arena, PR_FALSE);
                    }
                    CERT_DestroyOCSPRequest(request);
                    break;
                }
            } else if (local_file_fd) {
                PRInt32 bytes;
                int errLen;
                bytes = PR_TransmitFile(ssl_sock, local_file_fd, outHeader,
                                        sizeof outHeader - 1,
                                        PR_TRANSMITFILE_KEEP_OPEN,
                                        PR_INTERVAL_NO_TIMEOUT);
                if (bytes >= 0) {
                    bytes -= sizeof outHeader - 1;
                    FPRINTF(stderr,
                            "httpserv: PR_TransmitFile wrote %d bytes from %s\n",
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

            rv = PR_Writev(ssl_sock, iovs, numIOVs, PR_INTERVAL_NO_TIMEOUT);
            if (rv < 0) {
                errWarn("PR_Writev");
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
    VLOG(("httpserv: handle_connection: exiting\n"));

    /* do a nice shutdown if asked. */
    if (!strncmp(buf, stopCmd, sizeof stopCmd - 1)) {
        VLOG(("httpserv: handle_connection: stop command"));
        stop_server();
    }
    VLOG(("httpserv: handle_connection: exiting"));
    return SECSuccess; /* success */
}

#ifdef XP_UNIX

void
sigusr1_handler(int sig)
{
    VLOG(("httpserv: sigusr1_handler: stop server"));
    stop_server();
}

#endif

SECStatus
do_accepts(
    PRFileDesc *listen_sock,
    PRFileDesc *model_sock,
    int requestCert)
{
    PRNetAddr addr;
    PRErrorCode perr;
#ifdef XP_UNIX
    struct sigaction act;
#endif

    VLOG(("httpserv: do_accepts: starting"));
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

        FPRINTF(stderr, "\n\n\nhttpserv: About to call accept.\n");
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

        VLOG(("httpserv: do_accept: Got connection\n"));

        PR_Lock(qLock);
        while (PR_CLIST_IS_EMPTY(&freeJobs) && !stopping) {
            PR_WaitCondVar(freeListNotEmptyCv, PR_INTERVAL_NO_TIMEOUT);
        }
        if (stopping) {
            PR_Unlock(qLock);
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
            myJob->requestCert = requestCert;
        }

        PR_APPEND_LINK(myLink, &jobQ);
        PR_NotifyCondVar(jobQNotEmptyCv);
        PR_Unlock(qLock);
    }

    FPRINTF(stderr, "httpserv: Closing listen socket.\n");
    VLOG(("httpserv: do_accepts: exiting"));
    if (listen_sock) {
        PR_Close(listen_sock);
    }
    return SECSuccess;
}

PRFileDesc *
getBoundListenSocket(unsigned short port)
{
    PRFileDesc *listen_sock;
    int listenQueueDepth = 5 + (2 * maxThreads);
    PRStatus prStatus;
    PRNetAddr addr;
    PRSocketOptionData opt;

    addr.inet.family = PR_AF_INET;
    addr.inet.ip = PR_htonl(PR_INADDR_ANY);
    addr.inet.port = PR_htons(port);

    listen_sock = PR_NewTCPSocket();
    if (listen_sock == NULL) {
        errExit("PR_NewTCPSocket");
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
     * after clean shutdown . See bug 331413 .*/
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

void
server_main(
    PRFileDesc *listen_sock,
    int requestCert,
    SECKEYPrivateKey **privKey,
    CERTCertificate **cert,
    const char *expectedHostNameVal)
{
    PRFileDesc *model_sock = NULL;

    /* Now, do the accepting, here in the main thread. */
    do_accepts(listen_sock, model_sock, requestCert);

    terminateWorkerThreads();

    if (model_sock) {
        PR_Close(model_sock);
    }
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

/* slightly adjusted version of ocsp_CreateCertID (not using issuer) */
static CERTOCSPCertID *
ocsp_CreateSelfCAID(PLArenaPool *arena, CERTCertificate *cert, PRTime time)
{
    CERTOCSPCertID *certID;
    void *mark = PORT_ArenaMark(arena);
    SECStatus rv;

    PORT_Assert(arena != NULL);

    certID = PORT_ArenaZNew(arena, CERTOCSPCertID);
    if (certID == NULL) {
        goto loser;
    }

    rv = SECOID_SetAlgorithmID(arena, &certID->hashAlgorithm, SEC_OID_SHA1,
                               NULL);
    if (rv != SECSuccess) {
        goto loser;
    }

    if (CERT_GetSubjectNameDigest(arena, cert, SEC_OID_SHA1,
                                  &(certID->issuerNameHash)) == NULL) {
        goto loser;
    }
    certID->issuerSHA1NameHash.data = certID->issuerNameHash.data;
    certID->issuerSHA1NameHash.len = certID->issuerNameHash.len;

    if (CERT_GetSubjectNameDigest(arena, cert, SEC_OID_MD5,
                                  &(certID->issuerMD5NameHash)) == NULL) {
        goto loser;
    }

    if (CERT_GetSubjectNameDigest(arena, cert, SEC_OID_MD2,
                                  &(certID->issuerMD2NameHash)) == NULL) {
        goto loser;
    }

    if (CERT_GetSubjectPublicKeyDigest(arena, cert, SEC_OID_SHA1,
                                       &certID->issuerKeyHash) == NULL) {
        goto loser;
    }
    certID->issuerSHA1KeyHash.data = certID->issuerKeyHash.data;
    certID->issuerSHA1KeyHash.len = certID->issuerKeyHash.len;
    /* cache the other two hash algorithms as well */
    if (CERT_GetSubjectPublicKeyDigest(arena, cert, SEC_OID_MD5,
                                       &certID->issuerMD5KeyHash) == NULL) {
        goto loser;
    }
    if (CERT_GetSubjectPublicKeyDigest(arena, cert, SEC_OID_MD2,
                                       &certID->issuerMD2KeyHash) == NULL) {
        goto loser;
    }

    PORT_ArenaUnmark(arena, mark);
    return certID;

loser:
    PORT_ArenaRelease(arena, mark);
    return NULL;
}

/* slightly adjusted version of CERT_CreateOCSPCertID */
CERTOCSPCertID *
cert_CreateSelfCAID(CERTCertificate *cert, PRTime time)
{
    PLArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    CERTOCSPCertID *certID;
    PORT_Assert(arena != NULL);
    if (!arena)
        return NULL;

    certID = ocsp_CreateSelfCAID(arena, cert, time);
    if (!certID) {
        PORT_FreeArena(arena, PR_FALSE);
        return NULL;
    }
    certID->poolp = arena;
    return certID;
}

int
main(int argc, char **argv)
{
    char *progName = NULL;
    const char *dir = ".";
    char *passwd = NULL;
    char *pwfile = NULL;
    const char *pidFile = NULL;
    char *tmp;
    PRFileDesc *listen_sock;
    int optionsFound = 0;
    unsigned short port = 0;
    SECStatus rv;
    PRStatus prStatus;
    PRBool bindOnly = PR_FALSE;
    PRBool useLocalThreads = PR_FALSE;
    PLOptState *optstate;
    PLOptStatus status;
    char emptyString[] = { "" };
    char *certPrefix = emptyString;
    caRevoInfo *revoInfo = NULL;
    PRCList *caRevoIter = NULL;
    PRBool provideOcsp = PR_FALSE;

    tmp = strrchr(argv[0], '/');
    tmp = tmp ? tmp + 1 : argv[0];
    progName = strrchr(tmp, '\\');
    progName = progName ? progName + 1 : tmp;

    PR_Init(PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);

    /* please keep this list of options in ASCII collating sequence.
    ** numbers, then capital letters, then lower case, alphabetical.
    */
    optstate = PL_CreateOptState(argc, argv,
                                 "A:C:DO:P:bd:f:hi:p:t:vw:");
    while ((status = PL_GetNextOpt(optstate)) == PL_OPT_OK) {
        ++optionsFound;
        switch (optstate->option) {
            /* A first, must be followed by C. Any other order is an error.
             * A creates the object. C completes and moves into list.
             */
            case 'A':
                provideOcsp = PR_TRUE;
                if (revoInfo) {
                    Usage(progName);
                    exit(0);
                }
                revoInfo = PORT_New(caRevoInfo);
                revoInfo->nickname = PORT_Strdup(optstate->value);
                break;
            case 'C':
                if (!revoInfo) {
                    Usage(progName);
                    exit(0);
                }
                revoInfo->crlFilename = PORT_Strdup(optstate->value);
                if (!caRevoInfos) {
                    PR_INIT_CLIST(&revoInfo->link);
                    caRevoInfos = revoInfo;
                } else {
                    PR_APPEND_LINK(&revoInfo->link, &caRevoInfos->link);
                }
                revoInfo = NULL;
                break;

            case 'O':
                if (!PL_strcasecmp(optstate->value, "all")) {
                    ocspMethodsAllowed = ocspGetAndPost;
                } else if (!PL_strcasecmp(optstate->value, "get")) {
                    ocspMethodsAllowed = ocspGetOnly;
                } else if (!PL_strcasecmp(optstate->value, "post")) {
                    ocspMethodsAllowed = ocspPostOnly;
                } else if (!PL_strcasecmp(optstate->value, "random")) {
                    ocspMethodsAllowed = ocspRandomGetFailure;
                } else if (!PL_strcasecmp(optstate->value, "get-unknown")) {
                    ocspMethodsAllowed = ocspGetUnknown;
                } else {
                    Usage(progName);
                    exit(0);
                }
                break;

            case 'D':
                noDelay = PR_TRUE;
                break;

            case 'P':
                certPrefix = PORT_Strdup(optstate->value);
                break;

            case 'b':
                bindOnly = PR_TRUE;
                break;

            case 'd':
                dir = optstate->value;
                break;

            case 'f':
                pwdata.source = PW_FROMFILE;
                pwdata.data = pwfile = PORT_Strdup(optstate->value);
                break;

            case 'h':
                Usage(progName);
                exit(0);
                break;

            case 'i':
                pidFile = optstate->value;
                break;

            case 'p':
                port = PORT_Atoi(optstate->value);
                break;

            case 't':
                maxThreads = PORT_Atoi(optstate->value);
                if (maxThreads > MAX_THREADS)
                    maxThreads = MAX_THREADS;
                if (maxThreads < MIN_THREADS)
                    maxThreads = MIN_THREADS;
                break;

            case 'v':
                verbose++;
                break;

            case 'w':
                pwdata.source = PW_PLAINTEXT;
                pwdata.data = passwd = PORT_Strdup(optstate->value);
                break;

            default:
            case '?':
                fprintf(stderr, "Unrecognized or bad option specified.\n");
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

    /* The -b (bindOnly) option is only used by the ssl.sh test
     * script on Linux to determine whether a previous httpserv
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

    if (port == 0) {
        fprintf(stderr, "Required argument 'port' must be non-zero value\n");
        exit(7);
    }

    if (pidFile) {
        FILE *tmpfile = fopen(pidFile, "w+");

        if (tmpfile) {
            fprintf(tmpfile, "%d", getpid());
            fclose(tmpfile);
        }
    }

    tmp = PR_GetEnvSecure("TMP");
    if (!tmp)
        tmp = PR_GetEnvSecure("TMPDIR");
    if (!tmp)
        tmp = PR_GetEnvSecure("TEMP");
    /* we're an ordinary single process server. */
    listen_sock = getBoundListenSocket(port);
    prStatus = PR_SetFDInheritable(listen_sock, PR_FALSE);
    if (prStatus != PR_SUCCESS)
        errExit("PR_SetFDInheritable");

#ifdef PR_LOGGING
    lm = PR_NewLogModule("TestCase");
#endif

    /* set our password function */
    PK11_SetPasswordFunc(SECU_GetModulePassword);

    if (provideOcsp) {
        /* Call the NSS initialization routines */
        rv = NSS_Initialize(dir, certPrefix, certPrefix, SECMOD_DB, NSS_INIT_READONLY);
        if (rv != SECSuccess) {
            fputs("NSS_Init failed.\n", stderr);
            exit(8);
        }

        if (caRevoInfos) {
            caRevoIter = &caRevoInfos->link;
            do {
                PRFileDesc *inFile;
                SECItem crlDER;
                crlDER.data = NULL;

                revoInfo = (caRevoInfo *)caRevoIter;
                revoInfo->cert = CERT_FindCertByNickname(
                    CERT_GetDefaultCertDB(), revoInfo->nickname);
                if (!revoInfo->cert) {
                    fprintf(stderr, "cannot find cert with nickname %s\n",
                            revoInfo->nickname);
                    exit(1);
                }
                inFile = PR_Open(revoInfo->crlFilename, PR_RDONLY, 0);
                if (inFile) {
                    rv = SECU_ReadDERFromFile(&crlDER, inFile, PR_FALSE, PR_FALSE);
                    PR_Close(inFile);
                    inFile = NULL;
                }
                if (rv != SECSuccess) {
                    fprintf(stderr, "unable to read crl file %s\n",
                            revoInfo->crlFilename);
                    exit(1);
                }
                revoInfo->crl =
                    CERT_DecodeDERCrlWithFlags(NULL, &crlDER, SEC_CRL_TYPE,
                                               CRL_DECODE_DEFAULT_OPTIONS);
                SECITEM_FreeItem(&crlDER, PR_FALSE);
                if (!revoInfo->crl) {
                    fprintf(stderr, "unable to decode crl file %s\n",
                            revoInfo->crlFilename);
                    exit(1);
                }
                if (CERT_CompareName(&revoInfo->crl->crl.name,
                                     &revoInfo->cert->subject) != SECEqual) {
                    fprintf(stderr, "CRL %s doesn't match cert identified by preceding nickname %s\n",
                            revoInfo->crlFilename, revoInfo->nickname);
                    exit(1);
                }
                revoInfo->id = cert_CreateSelfCAID(revoInfo->cert, PR_Now());
                caRevoIter = PR_NEXT_LINK(caRevoIter);
            } while (caRevoIter != &caRevoInfos->link);
        }
    }

    /* allocate the array of thread slots, and launch the worker threads. */
    rv = launch_threads(&jobLoop, 0, 0, 0, useLocalThreads);

    if (rv == SECSuccess) {
        server_main(listen_sock, 0, 0, 0,
                    0);
    }

    VLOG(("httpserv: server_thread: exiting"));

    if (provideOcsp) {
        if (caRevoInfos) {
            caRevoIter = &caRevoInfos->link;
            do {
                revoInfo = (caRevoInfo *)caRevoIter;
                if (revoInfo->nickname)
                    PORT_Free(revoInfo->nickname);
                if (revoInfo->crlFilename)
                    PORT_Free(revoInfo->crlFilename);
                if (revoInfo->cert)
                    CERT_DestroyCertificate(revoInfo->cert);
                if (revoInfo->id)
                    CERT_DestroyOCSPCertID(revoInfo->id);
                if (revoInfo->crl)
                    SEC_DestroyCrl(revoInfo->crl);

                caRevoIter = PR_NEXT_LINK(caRevoIter);
            } while (caRevoIter != &caRevoInfos->link);
        }
        if (NSS_Shutdown() != SECSuccess) {
            SECU_PrintError(progName, "NSS_Shutdown");
            PR_Cleanup();
            exit(1);
        }
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
    PR_Cleanup();
    printf("httpserv: normal termination\n");
    return 0;
}
