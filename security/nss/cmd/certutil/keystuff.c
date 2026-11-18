/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>
#include <string.h>
#include "secutil.h"

#if defined(XP_UNIX)
#include <unistd.h>
#include <sys/time.h>
#include <termios.h>
#endif

#if defined(XP_WIN) || defined(XP_PC)
#include <time.h>
#include <conio.h>
#endif

#include "nspr.h"
#include "prtypes.h"
#include "prtime.h"
#include "prlong.h"

#include "pk11func.h"

#define NUM_KEYSTROKES 120
#define RAND_BUF_SIZE 60

#define ERROR_BREAK  \
    rv = SECFailure; \
    break;

const SEC_ASN1Template SECKEY_PQGParamsTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(SECKEYPQGParams) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYPQGParams, prime) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYPQGParams, subPrime) },
    { SEC_ASN1_INTEGER, offsetof(SECKEYPQGParams, base) },
    { 0 }
};

/* returns 0 for success, -1 for failure (EOF encountered) */
static int
UpdateRNG(void)
{
    char randbuf[RAND_BUF_SIZE];
#ifdef XP_UNIX
    int fd;
#endif
    int c;
    int rv = 0;
    size_t count;
#ifdef XP_UNIX
    cc_t orig_cc_min;
    cc_t orig_cc_time;
    tcflag_t orig_lflag;
    struct termios tio;
#endif
    char meter[] = {
        "\r|                                                            |"
    };

#define FPS fprintf(stderr,
    FPS "\n");
    FPS "A random seed must be generated that will be used in the\n");
    FPS "creation of your key.  One of the easiest ways to create a\n");
    FPS "random seed is to use the timing of keystrokes on a keyboard.\n");
    FPS "\n");
    FPS "To begin, type keys on the keyboard until this progress meter\n");
    FPS "is full.  DO NOT USE THE AUTOREPEAT FUNCTION ON YOUR KEYBOARD!\n");
    FPS "\n");
    FPS "\n");
    FPS "Continue typing until the progress meter is full:\n\n");
    FPS "%s", meter);
    FPS "\r|");

#if defined(XP_UNIX)
    /* turn off echo on stdin & return on 1 char instead of NL */
    fd = fileno(stdin);

    tcgetattr(fd, &tio);
    orig_lflag = tio.c_lflag;
    orig_cc_min = tio.c_cc[VMIN];
    orig_cc_time = tio.c_cc[VTIME];
    tio.c_lflag &= ~ECHO;
    tio.c_lflag &= ~ICANON;
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSAFLUSH, &tio);
#endif

    /* Get random noise from keyboard strokes */
    count = 0;
    while (count < sizeof randbuf) {
#if defined(XP_UNIX)
        c = getc(stdin);
#else
        c = getch();
#endif
        if (c == EOF) {
            rv = -1;
            break;
        }
        randbuf[count] = c;
        if (count == 0 || c != randbuf[count - 1]) {
            count++;
            FPS "*");
        }
    }
    PK11_RandomUpdate(randbuf, sizeof randbuf);
    memset(randbuf, 0, sizeof randbuf);

    FPS "\n\n");
    FPS "Finished.  Press enter to continue: ");
    while ((c = getc(stdin)) != '\n' && c != EOF)
        ;
    if (c == EOF)
        rv = -1;
    FPS "\n");

#undef FPS

#if defined(XP_UNIX)
    /* set back termio the way it was */
    tio.c_lflag = orig_lflag;
    tio.c_cc[VMIN] = orig_cc_min;
    tio.c_cc[VTIME] = orig_cc_time;
    tcsetattr(fd, TCSAFLUSH, &tio);
#endif
    return rv;
}

static const unsigned char P[] = { 0,
                                   0xc6, 0x2a, 0x47, 0x73, 0xea, 0x78, 0xfa, 0x65,
                                   0x47, 0x69, 0x39, 0x10, 0x08, 0x55, 0x6a, 0xdd,
                                   0xbf, 0x77, 0xe1, 0x9a, 0x69, 0x73, 0xba, 0x66,
                                   0x37, 0x08, 0x93, 0x9e, 0xdb, 0x5d, 0x01, 0x08,
                                   0xb8, 0x3a, 0x73, 0xe9, 0x85, 0x5f, 0xa7, 0x2b,
                                   0x63, 0x7f, 0xd0, 0xc6, 0x4c, 0xdc, 0xfc, 0x8b,
                                   0xa6, 0x03, 0xc9, 0x9c, 0x80, 0x5e, 0xec, 0xc6,
                                   0x21, 0x23, 0xf7, 0x8e, 0xa4, 0x7b, 0x77, 0x83,
                                   0x02, 0x44, 0xf8, 0x05, 0xd7, 0x36, 0x52, 0x13,
                                   0x57, 0x78, 0x97, 0xf3, 0x7b, 0xcf, 0x1f, 0xc9,
                                   0x2a, 0xa4, 0x71, 0x9d, 0xa8, 0xd8, 0x5d, 0xc5,
                                   0x3b, 0x64, 0x3a, 0x72, 0x60, 0x62, 0xb0, 0xb8,
                                   0xf3, 0xb1, 0xe7, 0xb9, 0x76, 0xdf, 0x74, 0xbe,
                                   0x87, 0x6a, 0xd2, 0xf1, 0xa9, 0x44, 0x8b, 0x63,
                                   0x76, 0x4f, 0x5d, 0x21, 0x63, 0xb5, 0x4f, 0x3c,
                                   0x7b, 0x61, 0xb2, 0xf3, 0xea, 0xc5, 0xd8, 0xef,
                                   0x30, 0x50, 0x59, 0x33, 0x61, 0xc0, 0xf3, 0x6e,
                                   0x21, 0xcf, 0x15, 0x35, 0x4a, 0x87, 0x2b, 0xc3,
                                   0xf6, 0x5a, 0x1f, 0x24, 0x22, 0xc5, 0xeb, 0x47,
                                   0x34, 0x4a, 0x1b, 0xb5, 0x2e, 0x71, 0x52, 0x8f,
                                   0x2d, 0x7d, 0xa9, 0x96, 0x8a, 0x7c, 0x61, 0xdb,
                                   0xc0, 0xdc, 0xf1, 0xca, 0x28, 0x69, 0x1c, 0x97,
                                   0xad, 0xea, 0x0d, 0x9e, 0x02, 0xe6, 0xe5, 0x7d,
                                   0xad, 0xe0, 0x42, 0x91, 0x4d, 0xfa, 0xe2, 0x81,
                                   0x16, 0x2b, 0xc2, 0x96, 0x3b, 0x32, 0x8c, 0x20,
                                   0x69, 0x8b, 0x5b, 0x17, 0x3c, 0xf9, 0x13, 0x6c,
                                   0x98, 0x27, 0x1c, 0xca, 0xcf, 0x33, 0xaa, 0x93,
                                   0x21, 0xaf, 0x17, 0x6e, 0x5e, 0x00, 0x37, 0xd9,
                                   0x34, 0x8a, 0x47, 0xd2, 0x1c, 0x67, 0x32, 0x60,
                                   0xb6, 0xc7, 0xb0, 0xfd, 0x32, 0x90, 0x93, 0x32,
                                   0xaa, 0x11, 0xba, 0x23, 0x19, 0x39, 0x6a, 0x42,
                                   0x7c, 0x1f, 0xb7, 0x28, 0xdb, 0x64, 0xad, 0xd9 };
static const unsigned char Q[] = { 0,
                                   0xe6, 0xa3, 0xc9, 0xc6, 0x51, 0x92, 0x8b, 0xb3,
                                   0x98, 0x8f, 0x97, 0xb8, 0x31, 0x0d, 0x4a, 0x03,
                                   0x1e, 0xba, 0x4e, 0xe6, 0xc8, 0x90, 0x98, 0x1d,
                                   0x3a, 0x95, 0xf4, 0xf1 };
static const unsigned char G[] = {
    0x70, 0x32, 0x58, 0x5d, 0xb3, 0xbf, 0xc3, 0x62,
    0x63, 0x0b, 0xf8, 0xa5, 0xe1, 0xed, 0xeb, 0x79,
    0xac, 0x18, 0x41, 0x64, 0xb3, 0xda, 0x4c, 0xa7,
    0x92, 0x63, 0xb1, 0x33, 0x7c, 0xcb, 0x43, 0xdc,
    0x1f, 0x38, 0x63, 0x5e, 0x0e, 0x6d, 0x45, 0xd1,
    0xc9, 0x67, 0xf3, 0xcf, 0x3d, 0x2d, 0x16, 0x4e,
    0x92, 0x16, 0x06, 0x59, 0x29, 0x89, 0x6f, 0x54,
    0xff, 0xc5, 0x71, 0xc8, 0x3a, 0x95, 0x84, 0xb6,
    0x7e, 0x7b, 0x1e, 0x8b, 0x47, 0x9d, 0x7a, 0x3a,
    0x36, 0x9b, 0x70, 0x2f, 0xd1, 0xbd, 0xef, 0xe8,
    0x3a, 0x41, 0xd4, 0xf3, 0x1f, 0x81, 0xc7, 0x1f,
    0x96, 0x7c, 0x30, 0xab, 0xf4, 0x7a, 0xac, 0x93,
    0xed, 0x6f, 0x67, 0xb0, 0xc9, 0x5b, 0xf3, 0x83,
    0x9d, 0xa0, 0xd7, 0xb9, 0x01, 0xed, 0x28, 0xae,
    0x1c, 0x6e, 0x2e, 0x48, 0xac, 0x9f, 0x7d, 0xf3,
    0x00, 0x48, 0xee, 0x0e, 0xfb, 0x7e, 0x5e, 0xcb,
    0xf5, 0x39, 0xd8, 0x92, 0x90, 0x61, 0x2d, 0x1e,
    0x3c, 0xd3, 0x55, 0x0d, 0x34, 0xd1, 0x81, 0xc4,
    0x89, 0xea, 0x94, 0x2b, 0x56, 0x33, 0x73, 0x58,
    0x48, 0xbf, 0x23, 0x72, 0x19, 0x5f, 0x19, 0xac,
    0xff, 0x09, 0xc8, 0xcd, 0xab, 0x71, 0xef, 0x9e,
    0x20, 0xfd, 0xe3, 0xb8, 0x27, 0x9e, 0x65, 0xb1,
    0x85, 0xcd, 0x88, 0xfe, 0xd4, 0xd7, 0x64, 0x4d,
    0xe1, 0xe8, 0xa6, 0xe5, 0x96, 0xc8, 0x5d, 0x9c,
    0xc6, 0x70, 0x6b, 0xba, 0x77, 0x4e, 0x90, 0x4a,
    0xb0, 0x96, 0xc5, 0xa0, 0x9e, 0x2c, 0x01, 0x03,
    0xbe, 0xbd, 0x71, 0xba, 0x0a, 0x6f, 0x9f, 0xe5,
    0xdb, 0x04, 0x08, 0xf2, 0x9e, 0x0f, 0x1b, 0xac,
    0xcd, 0xbb, 0x65, 0x12, 0xcf, 0x77, 0xc9, 0x7d,
    0xbe, 0x94, 0x4b, 0x9c, 0x5b, 0xde, 0x0d, 0xfa,
    0x57, 0xdd, 0x77, 0x32, 0xf0, 0x5b, 0x34, 0xfd,
    0x19, 0x95, 0x33, 0x60, 0x87, 0xe2, 0xa2, 0xf4
};

/* P, Q, G have been generated using the NSS makepqg utility:
 *         makepqg -l 2048 -g 224 -r
 * (see also: bug 1170322)
 *
 *   h: 1 (0x1)
 *   SEED:
 *       d2:0b:c5:63:1b:af:dc:36:b7:7c:b9:3e:36:01:a0:8f:
 *       0e:be:d0:38:e4:78:d5:3c:7c:9e:a9:9a:d2:0b:c5:63:
 *       1b:af:dc:36:b7:7c:b9:3e:36:01:a0:8f:0e:be:d0:38:
 *       e4:78:d5:3c:7c:9e:c7:70:d2:0b:c5:63:1b:af:dc:36:
 *       b7:7c:b9:3e:36:01:a0:8f:0e:be:d0:38:e4:78:d5:3c:
 *       7c:9e:aa:3e
 *   g:       672
 *   counter: 0
 */

static const SECKEYPQGParams default_pqg_params = {
    NULL,
    { 0, (unsigned char *)P, sizeof(P) },
    { 0, (unsigned char *)Q, sizeof(Q) },
    { 0, (unsigned char *)G, sizeof(G) }
};

static SECKEYPQGParams *
decode_pqg_params(const char *str)
{
    char *buf;
    unsigned int len;
    PLArenaPool *arena;
    SECKEYPQGParams *params;
    SECStatus status;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL)
        return NULL;

    params = PORT_ArenaZAlloc(arena, sizeof(SECKEYPQGParams));
    if (params == NULL)
        goto loser;
    params->arena = arena;

    buf = (char *)ATOB_AsciiToData(str, &len);
    if ((buf == NULL) || (len == 0))
        goto loser;

    status = SEC_ASN1Decode(arena, params, SECKEY_PQGParamsTemplate, buf, len);
    if (status != SECSuccess)
        goto loser;

    return params;

loser:
    if (arena != NULL)
        PORT_FreeArena(arena, PR_FALSE);
    return NULL;
}

void
CERTUTIL_DestroyParamsPQG(SECKEYPQGParams *params)
{
    if (params->arena) {
        PORT_FreeArena(params->arena, PR_FALSE);
    }
}

static int
pqg_prime_bits(const SECKEYPQGParams *params)
{
    int primeBits = 0;

    if (params != NULL) {
        int i;
        for (i = 0; params->prime.data[i] == 0; i++) {
            /* empty */;
        }
        primeBits = (params->prime.len - i) * 8;
    }

    return primeBits;
}

static char *
getPQGString(const char *filename)
{
    unsigned char *buf = NULL;
    PRFileDesc *src;
    PRInt32 numBytes;
    PRStatus prStatus;
    PRFileInfo info;

    src = PR_Open(filename, PR_RDONLY, 0);
    if (!src) {
        fprintf(stderr, "Failed to open PQG file %s\n", filename);
        return NULL;
    }

    prStatus = PR_GetOpenFileInfo(src, &info);

    if (prStatus == PR_SUCCESS) {
        buf = (unsigned char *)PORT_Alloc(info.size + 1);
    }
    if (!buf) {
        PR_Close(src);
        fprintf(stderr, "Failed to read PQG file %s\n", filename);
        return NULL;
    }

    numBytes = PR_Read(src, buf, info.size);
    PR_Close(src);
    if (numBytes != info.size) {
        PORT_Free(buf);
        fprintf(stderr, "Failed to read PQG file %s\n", filename);
        PORT_SetError(SEC_ERROR_IO);
        return NULL;
    }

    if (buf[numBytes - 1] == '\n')
        numBytes--;
    if (buf[numBytes - 1] == '\r')
        numBytes--;
    buf[numBytes] = 0;

    return (char *)buf;
}

static SECKEYPQGParams *
getpqgfromfile(int keyBits, const char *pqgFile)
{
    char *end, *str, *pqgString;
    SECKEYPQGParams *params = NULL;

    str = pqgString = getPQGString(pqgFile);
    if (!str)
        return NULL;

    do {
        end = PORT_Strchr(str, ',');
        if (end)
            *end = '\0';
        params = decode_pqg_params(str);
        if (params) {
            int primeBits = pqg_prime_bits(params);
            if (keyBits == primeBits)
                break;
            CERTUTIL_DestroyParamsPQG(params);
            params = NULL;
        }
        if (end)
            str = end + 1;
    } while (end);

    PORT_Free(pqgString);
    return params;
}

static SECStatus
CERTUTIL_FileForRNG(const char *noise)
{
    char buf[2048];
    PRFileDesc *fd;
    PRInt32 count;

    fd = PR_Open(noise, PR_RDONLY, 0);
    if (!fd) {
        fprintf(stderr, "failed to open noise file.");
        return SECFailure;
    }

    do {
        count = PR_Read(fd, buf, sizeof(buf));
        if (count > 0) {
            PK11_RandomUpdate(buf, count);
        }
    } while (count > 0);

    PR_Close(fd);
    return SECSuccess;
}

typedef struct curveNameTagPairStr {
    char *curveName;
    SECOidTag curveOidTag;
} CurveNameTagPair;

static CurveNameTagPair nameTagPair[] = {
    { "sect163k1", SEC_OID_SECG_EC_SECT163K1 },
    { "nistk163", SEC_OID_SECG_EC_SECT163K1 },
    { "sect163r1", SEC_OID_SECG_EC_SECT163R1 },
    { "sect163r2", SEC_OID_SECG_EC_SECT163R2 },
    { "nistb163", SEC_OID_SECG_EC_SECT163R2 },
    { "sect193r1", SEC_OID_SECG_EC_SECT193R1 },
    { "sect193r2", SEC_OID_SECG_EC_SECT193R2 },
    { "sect233k1", SEC_OID_SECG_EC_SECT233K1 },
    { "nistk233", SEC_OID_SECG_EC_SECT233K1 },
    { "sect233r1", SEC_OID_SECG_EC_SECT233R1 },
    { "nistb233", SEC_OID_SECG_EC_SECT233R1 },
    { "sect239k1", SEC_OID_SECG_EC_SECT239K1 },
    { "sect283k1", SEC_OID_SECG_EC_SECT283K1 },
    { "nistk283", SEC_OID_SECG_EC_SECT283K1 },
    { "sect283r1", SEC_OID_SECG_EC_SECT283R1 },
    { "nistb283", SEC_OID_SECG_EC_SECT283R1 },
    { "sect409k1", SEC_OID_SECG_EC_SECT409K1 },
    { "nistk409", SEC_OID_SECG_EC_SECT409K1 },
    { "sect409r1", SEC_OID_SECG_EC_SECT409R1 },
    { "nistb409", SEC_OID_SECG_EC_SECT409R1 },
    { "sect571k1", SEC_OID_SECG_EC_SECT571K1 },
    { "nistk571", SEC_OID_SECG_EC_SECT571K1 },
    { "sect571r1", SEC_OID_SECG_EC_SECT571R1 },
    { "nistb571", SEC_OID_SECG_EC_SECT571R1 },
    { "secp160k1", SEC_OID_SECG_EC_SECP160K1 },
    { "secp160r1", SEC_OID_SECG_EC_SECP160R1 },
    { "secp160r2", SEC_OID_SECG_EC_SECP160R2 },
    { "secp192k1", SEC_OID_SECG_EC_SECP192K1 },
    { "secp192r1", SEC_OID_SECG_EC_SECP192R1 },
    { "nistp192", SEC_OID_SECG_EC_SECP192R1 },
    { "secp224k1", SEC_OID_SECG_EC_SECP224K1 },
    { "secp224r1", SEC_OID_SECG_EC_SECP224R1 },
    { "nistp224", SEC_OID_SECG_EC_SECP224R1 },
    { "secp256k1", SEC_OID_SECG_EC_SECP256K1 },
    { "secp256r1", SEC_OID_SECG_EC_SECP256R1 },
    { "nistp256", SEC_OID_SECG_EC_SECP256R1 },
    { "secp384r1", SEC_OID_SECG_EC_SECP384R1 },
    { "nistp384", SEC_OID_SECG_EC_SECP384R1 },
    { "secp521r1", SEC_OID_SECG_EC_SECP521R1 },
    { "nistp521", SEC_OID_SECG_EC_SECP521R1 },

    { "prime192v1", SEC_OID_ANSIX962_EC_PRIME192V1 },
    { "prime192v2", SEC_OID_ANSIX962_EC_PRIME192V2 },
    { "prime192v3", SEC_OID_ANSIX962_EC_PRIME192V3 },
    { "prime239v1", SEC_OID_ANSIX962_EC_PRIME239V1 },
    { "prime239v2", SEC_OID_ANSIX962_EC_PRIME239V2 },
    { "prime239v3", SEC_OID_ANSIX962_EC_PRIME239V3 },

    { "c2pnb163v1", SEC_OID_ANSIX962_EC_C2PNB163V1 },
    { "c2pnb163v2", SEC_OID_ANSIX962_EC_C2PNB163V2 },
    { "c2pnb163v3", SEC_OID_ANSIX962_EC_C2PNB163V3 },
    { "c2pnb176v1", SEC_OID_ANSIX962_EC_C2PNB176V1 },
    { "c2tnb191v1", SEC_OID_ANSIX962_EC_C2TNB191V1 },
    { "c2tnb191v2", SEC_OID_ANSIX962_EC_C2TNB191V2 },
    { "c2tnb191v3", SEC_OID_ANSIX962_EC_C2TNB191V3 },
    { "c2onb191v4", SEC_OID_ANSIX962_EC_C2ONB191V4 },
    { "c2onb191v5", SEC_OID_ANSIX962_EC_C2ONB191V5 },
    { "c2pnb208w1", SEC_OID_ANSIX962_EC_C2PNB208W1 },
    { "c2tnb239v1", SEC_OID_ANSIX962_EC_C2TNB239V1 },
    { "c2tnb239v2", SEC_OID_ANSIX962_EC_C2TNB239V2 },
    { "c2tnb239v3", SEC_OID_ANSIX962_EC_C2TNB239V3 },
    { "c2onb239v4", SEC_OID_ANSIX962_EC_C2ONB239V4 },
    { "c2onb239v5", SEC_OID_ANSIX962_EC_C2ONB239V5 },
    { "c2pnb272w1", SEC_OID_ANSIX962_EC_C2PNB272W1 },
    { "c2pnb304w1", SEC_OID_ANSIX962_EC_C2PNB304W1 },
    { "c2tnb359v1", SEC_OID_ANSIX962_EC_C2TNB359V1 },
    { "c2pnb368w1", SEC_OID_ANSIX962_EC_C2PNB368W1 },
    { "c2tnb431r1", SEC_OID_ANSIX962_EC_C2TNB431R1 },

    { "secp112r1", SEC_OID_SECG_EC_SECP112R1 },
    { "secp112r2", SEC_OID_SECG_EC_SECP112R2 },
    { "secp128r1", SEC_OID_SECG_EC_SECP128R1 },
    { "secp128r2", SEC_OID_SECG_EC_SECP128R2 },

    { "sect113r1", SEC_OID_SECG_EC_SECT113R1 },
    { "sect113r2", SEC_OID_SECG_EC_SECT113R2 },
    { "sect131r1", SEC_OID_SECG_EC_SECT131R1 },
    { "sect131r2", SEC_OID_SECG_EC_SECT131R2 },
    { "curve25519", SEC_OID_CURVE25519 },
};

static CurveNameTagPair edNameTagPair[] = {
    { "ed25519", SEC_OID_ED25519 },
    /* with one entry, we could just hand check the
     * curve name, but by using a table, we can
     * add 448 support by just adding it to the table */
};

static SECKEYECParams *
buildECEDParams(const char *curve, SECOidTag curveOidTag)
{
    SECKEYECParams *ecparams;
    SECOidData *oidData = NULL;

    /* Return NULL if curve name is not recognized */
    if ((curveOidTag == SEC_OID_UNKNOWN) ||
        (oidData = SECOID_FindOIDByTag(curveOidTag)) == NULL) {
        fprintf(stderr, "Unrecognized elliptic curve %s\n", curve);
        return NULL;
    }

    ecparams = SECITEM_AllocItem(NULL, NULL, (2 + oidData->oid.len));
    if (ecparams == NULL) {
        return NULL;
    }
    /*
     * ecparams->data needs to contain the ASN encoding of an object ID (OID)
     * representing the named curve. The actual OID is in
     * oidData->oid.data so we simply prepend 0x06 and OID length
     */
    ecparams->data[0] = SEC_ASN1_OBJECT_ID;
    ecparams->data[1] = oidData->oid.len;
    memcpy(ecparams->data + 2, oidData->oid.data, oidData->oid.len);

    return ecparams;
}

static SECKEYECParams *
getECParams(const char *curve)
{
    SECOidTag curveOidTag = SEC_OID_UNKNOWN; /* default */
    int i, numCurves;

    if (curve != NULL) {
        numCurves = sizeof(nameTagPair) / sizeof(CurveNameTagPair);
        for (i = 0; ((i < numCurves) && (curveOidTag == SEC_OID_UNKNOWN));
             i++) {
            if (PL_strcmp(curve, nameTagPair[i].curveName) == 0)
                curveOidTag = nameTagPair[i].curveOidTag;
        }
    }
    return buildECEDParams(curve, curveOidTag);
}

static SECKEYECParams *
getEDParams(const char *curve)
{
    SECOidTag curveOidTag = SEC_OID_UNKNOWN; /* default */
    int i, numCurves;

    if (curve != NULL) {
        numCurves = sizeof(edNameTagPair) / sizeof(CurveNameTagPair);
        for (i = 0; ((i < numCurves) && (curveOidTag == SEC_OID_UNKNOWN));
             i++) {
            if (PL_strcmp(curve, edNameTagPair[i].curveName) == 0)
                curveOidTag = edNameTagPair[i].curveOidTag;
        }
    }
    return buildECEDParams(curve, curveOidTag);
}

SECKEYPrivateKey *
CERTUTIL_GeneratePrivateKey(KeyType keytype, PK11SlotInfo *slot, int size,
                            int publicExponent, const char *noise,
                            SECKEYPublicKey **pubkeyp, const char *pqgFile,
                            PK11AttrFlags attrFlags, CK_FLAGS opFlagsOn,
                            CK_FLAGS opFlagsOff, secuPWData *pwdata)
{
    CK_MECHANISM_TYPE mechanism;
    PK11RSAGenParams rsaparams;
    SECKEYPQGParams *dsaparams = NULL;
    CK_ULONG paramSet;
    void *params;
    SECKEYPrivateKey *privKey = NULL;

    if (slot == NULL)
        return NULL;

    if (PK11_Authenticate(slot, PR_TRUE, pwdata) != SECSuccess)
        return NULL;

    /*
     * Do some random-number initialization.
     */

    if (noise) {
        SECStatus rv = CERTUTIL_FileForRNG(noise);
        if (rv != SECSuccess) {
            PORT_SetError(PR_END_OF_FILE_ERROR); /* XXX */
            return NULL;
        }
    } else {
        int rv = UpdateRNG();
        if (rv) {
            PORT_SetError(PR_END_OF_FILE_ERROR);
            return NULL;
        }
    }

    switch (keytype) {
        case rsaKey:
            rsaparams.keySizeInBits = size;
            rsaparams.pe = publicExponent;
            mechanism = CKM_RSA_PKCS_KEY_PAIR_GEN;
            params = &rsaparams;
            break;
        case dsaKey:
            mechanism = CKM_DSA_KEY_PAIR_GEN;
            if (pqgFile) {
                dsaparams = getpqgfromfile(size, pqgFile);
                if (dsaparams == NULL)
                    return NULL;
                params = dsaparams;
            } else {
                /* cast away const, and don't set dsaparams */
                params = (void *)&default_pqg_params;
            }
            break;
        case ecKey:
            mechanism = CKM_EC_KEY_PAIR_GEN;
            /* For EC keys, PQGFile determines EC parameters */
            if ((params = (void *)getECParams(pqgFile)) == NULL)
                return NULL;
            break;
        case edKey:
            mechanism = CKM_EC_EDWARDS_KEY_PAIR_GEN;
            if (pqgFile) {
                /* For ED keys, PQGFile determines ED parameters */
                if ((params = (void *)getEDParams(pqgFile)) == NULL) {
                    return NULL;
                }
            } else {
                if ((params = (void *)buildECEDParams("ed25519", SEC_OID_ED25519)) == NULL) {
                    return NULL;
                }
            }
            break;
        case mldsaKey:
            mechanism = CKM_ML_DSA_KEY_PAIR_GEN;
            /* set paramset */
            paramSet = 0;
            if (pqgFile) {
                if (PORT_Strcasecmp(pqgFile, "ML-DSA-44") == 0) {
                    paramSet = CKP_ML_DSA_44;
                } else if (PORT_Strcasecmp(pqgFile, "ML-DSA-65") == 0) {
                    paramSet = CKP_ML_DSA_65;
                } else if (PORT_Strcasecmp(pqgFile, "ML-DSA-87") == 0) {
                    paramSet = CKP_ML_DSA_87;
                } else {
                    /* if we set pqgfile, it had better be right, don't
                     * fall back to key size */
                    return NULL;
                }
            } else
                switch (size) {
                    /* optionally use the size, either the actual size in bytes
                     * or the short hand ('44', '65', '87') */
                    case 44:
                    case 2560:
                        paramSet = CKP_ML_DSA_44;
                        break;
                    case 65:
                    case 4032:
                        paramSet = CKP_ML_DSA_65;
                        break;
                    case 87:
                    case 4896:
                        paramSet = CKP_ML_DSA_87;
                        break;
                    default:
                        /* force a size to be specified somewhere */
                        return NULL;
                }
            /* paranoia, shouldn't be able to happen logically. Code
             * scanners will scream, but I like belt and suspenders */
            if (paramSet == 0) {
                return NULL;
            }
            params = &paramSet;
            break;
        case kyberKey:
            mechanism = CKM_ML_KEM_KEY_PAIR_GEN;
            if (!pqgFile) {
                fprintf(stderr, "ML-KEM keys require a parameter set (-q)\n");
                return NULL;
            }
            if (PORT_Strcasecmp(pqgFile, "ML-KEM-512") == 0) {
                paramSet = CKP_ML_KEM_512;
            } else if (PORT_Strcasecmp(pqgFile, "ML-KEM-768") == 0) {
                paramSet = CKP_ML_KEM_768;
            } else if (PORT_Strcasecmp(pqgFile, "ML-KEM-1024") == 0) {
                paramSet = CKP_ML_KEM_1024;
            } else {
                fprintf(stderr, "Unrecognized ML-KEM parameter set %s\n", pqgFile);
                return NULL;
            }
            params = &paramSet;
            break;

        default:
            return NULL;
    }

    fprintf(stderr, "\n\n");
    fprintf(stderr, "Generating key.  This may take a few moments...\n\n");

    privKey = PK11_GenerateKeyPairWithOpFlags(slot, mechanism, params, pubkeyp,
                                              attrFlags, opFlagsOn, opFlagsOn | opFlagsOff,
                                              pwdata /*wincx*/);
    /* free up the params */
    switch (keytype) {
        case dsaKey:
            if (dsaparams)
                CERTUTIL_DestroyParamsPQG(dsaparams);
            break;
        case ecKey:
        case edKey:
            SECITEM_FreeItem((SECItem *)params, PR_TRUE);
            break;
        default: /* nothing to free */
            break;
    }
    return privKey;
}
