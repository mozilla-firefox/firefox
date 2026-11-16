/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
** secutil.c - various functions used by security stuff
**
*/

#include "prtypes.h"
#include "prtime.h"
#include "prlong.h"
#include "prerror.h"
#include "prprf.h"
#include "plgetopt.h"
#include "prenv.h"
#include "prnetdb.h"

#include "cryptohi.h"
#include "secutil.h"
#include "secpkcs7.h"
#include "secpkcs5.h"
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>

#ifdef XP_UNIX
#include <unistd.h>
#endif

/* for SEC_TraverseNames */
#include "cert.h"
#include "certt.h"
#include "certdb.h"

#include "secmod.h"
#include "pk11func.h"
#include "secoid.h"

static char consoleName[] = {
#ifdef XP_UNIX
    "/dev/tty"
#else
    "CON:"
#endif
};

#include "cert.h"
#include "nssutil.h"
#include "ssl.h"
#include "sslproto.h"
#include "xconst.h"

#define DEFN_EXTEN_EXT_VALUE_ENCODER(mmm)                                       \
    SECStatus EXTEN_EXT_VALUE_ENCODER_##mmm(PLArenaPool *extHandleArena,        \
                                            void *value, SECItem *encodedValue) \
    {                                                                           \
        return mmm(extHandleArena, value, encodedValue);                        \
    }

DEFN_EXTEN_EXT_VALUE_ENCODER(CERT_EncodeAltNameExtension)
DEFN_EXTEN_EXT_VALUE_ENCODER(CERT_EncodeAuthKeyID)
DEFN_EXTEN_EXT_VALUE_ENCODER(CERT_EncodeBasicConstraintValue)
DEFN_EXTEN_EXT_VALUE_ENCODER(CERT_EncodeCRLDistributionPoints)
DEFN_EXTEN_EXT_VALUE_ENCODER(CERT_EncodeCertPoliciesExtension)
DEFN_EXTEN_EXT_VALUE_ENCODER(CERT_EncodeInfoAccessExtension)
DEFN_EXTEN_EXT_VALUE_ENCODER(CERT_EncodeInhibitAnyExtension)
DEFN_EXTEN_EXT_VALUE_ENCODER(CERT_EncodeNameConstraintsExtension)
DEFN_EXTEN_EXT_VALUE_ENCODER(CERT_EncodePolicyConstraintsExtension)
DEFN_EXTEN_EXT_VALUE_ENCODER(CERT_EncodePolicyMappingExtension)
DEFN_EXTEN_EXT_VALUE_ENCODER(CERT_EncodeSubjectKeyID)

static PRBool utf8DisplayEnabled = PR_FALSE;

/* The minimum password/pin length (in Unicode characters) in FIPS mode,
 * defined in lib/softoken/pkcs11i.h. */
#define FIPS_MIN_PIN 7

void
SECU_EnableUtf8Display(PRBool enable)
{
    utf8DisplayEnabled = enable;
}

PRBool
SECU_GetUtf8DisplayEnabled(void)
{
    return utf8DisplayEnabled;
}

static void
secu_ClearPassword(char *p)
{
    if (p) {
        PORT_Memset(p, 0, PORT_Strlen(p));
        PORT_Free(p);
    }
}

char *
SECU_GetPasswordString(void *arg, char *prompt)
{
#ifndef _WINDOWS
    char *p = NULL;
    FILE *input, *output;

    /* open terminal */
    input = fopen(consoleName, "r");
    if (input == NULL) {
        fprintf(stderr, "Error opening input terminal for read\n");
        return NULL;
    }

    output = fopen(consoleName, "w");
    if (output == NULL) {
        fprintf(stderr, "Error opening output terminal for write\n");
        fclose(input);
        return NULL;
    }

    p = SEC_GetPassword(input, output, prompt, SEC_BlindCheckPassword);

    fclose(input);
    fclose(output);

    return p;

#else
    /* Win32 version of above. opening the console may fail
       on windows95, and certainly isn't necessary.. */

    char *p = NULL;

    p = SEC_GetPassword(stdin, stdout, prompt, SEC_BlindCheckPassword);
    return p;

#endif
}

/*
 *  p a s s w o r d _ h a r d c o d e
 *
 *  A function to use the password passed in the -f(pwfile) argument
 *  of the command line.
 *  After use once, null it out otherwise PKCS11 calls us forever.?
 *
 */
char *
SECU_FilePasswd(PK11SlotInfo *slot, PRBool retry, void *arg)
{
    char *phrases, *phrase;
    PRFileDesc *fd;
    PRInt32 nb;
    char *pwFile = arg;
    int i;
    const long maxPwdFileSize = 4096;
    char *tokenName = NULL;
    int tokenLen = 0;

    if (!pwFile)
        return 0;

    if (retry) {
        return 0; /* no good retrying - the files contents will be the same */
    }

    phrases = PORT_ZAlloc(maxPwdFileSize);

    if (!phrases) {
        return 0; /* out of memory */
    }

    fd = PR_Open(pwFile, PR_RDONLY, 0);
    if (!fd) {
        fprintf(stderr, "No password file \"%s\" exists.\n", pwFile);
        PORT_Free(phrases);
        return NULL;
    }

    nb = PR_Read(fd, phrases, maxPwdFileSize);

    PR_Close(fd);

    if (nb == 0) {
        fprintf(stderr, "password file contains no data\n");
        PORT_Free(phrases);
        return NULL;
    }

    if (slot) {
        tokenName = PK11_GetTokenName(slot);
        if (tokenName) {
            tokenLen = PORT_Strlen(tokenName);
        }
    }
    i = 0;
    do {
        int startphrase = i;
        int phraseLen;

        /* handle the Windows EOL case */
        while (phrases[i] != '\r' && phrases[i] != '\n' && i < nb)
            i++;
        /* terminate passphrase */
        phrases[i++] = '\0';
        /* clean up any EOL before the start of the next passphrase */
        while ((i < nb) && (phrases[i] == '\r' || phrases[i] == '\n')) {
            phrases[i++] = '\0';
        }
        /* now analyze the current passphrase */
        phrase = &phrases[startphrase];
        if (!tokenName)
            break;
        if (PORT_Strncmp(phrase, tokenName, tokenLen))
            continue;
        phraseLen = PORT_Strlen(phrase);
        if (phraseLen < (tokenLen + 1))
            continue;
        if (phrase[tokenLen] != ':')
            continue;
        phrase = &phrase[tokenLen + 1];
        break;

    } while (i < nb);

    phrase = PORT_Strdup((char *)phrase);
    PORT_Free(phrases);
    return phrase;
}

char *
SECU_GetModulePassword(PK11SlotInfo *slot, PRBool retry, void *arg)
{
    char prompt[255];
    secuPWData *pwdata = (secuPWData *)arg;
    secuPWData pwnull = { PW_NONE, 0 };
    secuPWData pwxtrn = { PW_EXTERNAL, "external" };

    if (pwdata == NULL)
        pwdata = &pwnull;

    if (PK11_ProtectedAuthenticationPath(slot)) {
        pwdata = &pwxtrn;
    }
    if (retry && pwdata->source != PW_NONE) {
        PR_fprintf(PR_STDERR, "Incorrect password/PIN entered.\n");
        return NULL;
    }

    switch (pwdata->source) {
        case PW_NONE:
            snprintf(prompt, sizeof(prompt), "Enter Password or Pin for \"%s\":",
                     PK11_GetTokenName(slot));
            return SECU_GetPasswordString(NULL, prompt);
        case PW_FROMFILE:
            return SECU_FilePasswd(slot, retry, pwdata->data);
        case PW_EXTERNAL:
            snprintf(prompt, sizeof(prompt),
                     "Press Enter, then enter PIN for \"%s\" on external device.\n",
                     PK11_GetTokenName(slot));
            char *pw = SECU_GetPasswordString(NULL, prompt);
            PORT_Free(pw);
        /* Fall Through */
        case PW_PLAINTEXT:
            return PL_strdup(pwdata->data);
        default:
            break;
    }

    PR_fprintf(PR_STDERR, "Password check failed:  No password found.\n");
    return NULL;
}

char *
secu_InitSlotPassword(PK11SlotInfo *slot, PRBool retry, void *arg)
{
    char *p0 = NULL;
    char *p1 = NULL;
    FILE *input, *output;
    secuPWData *pwdata = arg;

    if (pwdata->source == PW_FROMFILE) {
        return SECU_FilePasswd(slot, retry, pwdata->data);
    }
    if (pwdata->source == PW_PLAINTEXT) {
        return PL_strdup(pwdata->data);
    }

/* PW_NONE - get it from tty */
/* open terminal */
#ifdef _WINDOWS
    input = stdin;
#else
    input = fopen(consoleName, "r");
#endif
    if (input == NULL) {
        PR_fprintf(PR_STDERR, "Error opening input terminal for read\n");
        return NULL;
    }

    /* we have no password, so initialize database with one */
    if (PK11_IsFIPS()) {
        PR_fprintf(PR_STDERR,
                   "Enter a password which will be used to encrypt your keys.\n"
                   "The password should be at least %d characters long,\n"
                   "and should consist of at least three character classes.\n"
                   "The available character classes are: digits (0-9), ASCII\n"
                   "lowercase letters, ASCII uppercase letters, ASCII\n"
                   "non-alphanumeric characters, and non-ASCII characters.\n\n"
                   "If an ASCII uppercase letter appears at the beginning of\n"
                   "the password, it is not counted toward its character class.\n"
                   "Similarly, if a digit appears at the end of the password,\n"
                   "it is not counted toward its character class.\n\n",
                   FIPS_MIN_PIN);
    } else {
        PR_fprintf(PR_STDERR,
                   "Enter a password which will be used to encrypt your keys.\n"
                   "The password should be at least 8 characters long,\n"
                   "and should contain at least one non-alphabetic character.\n\n");
    }

    output = fopen(consoleName, "w");
    if (output == NULL) {
        PR_fprintf(PR_STDERR, "Error opening output terminal for write\n");
#ifndef _WINDOWS
        fclose(input);
#endif
        return NULL;
    }

    for (;;) {
        if (p0)
            PORT_Free(p0);
        p0 = SEC_GetPassword(input, output, "Enter new password: ",
                             SEC_BlindCheckPassword);

        if (p1)
            PORT_Free(p1);
        p1 = SEC_GetPassword(input, output, "Re-enter password: ",
                             SEC_BlindCheckPassword);
        if (p0 && p1 && !PORT_Strcmp(p0, p1)) {
            break;
        }
        PR_fprintf(PR_STDERR, "Passwords do not match. Try again.\n");
    }

    /* clear out the duplicate password string */
    secu_ClearPassword(p1);

    fclose(input);
    fclose(output);

    return p0;
}

SECStatus
SECU_ChangePW(PK11SlotInfo *slot, char *passwd, char *pwFile)
{
    return SECU_ChangePW2(slot, passwd, 0, pwFile, 0);
}

SECStatus
SECU_ChangePW2(PK11SlotInfo *slot, char *oldPass, char *newPass,
               char *oldPwFile, char *newPwFile)
{
    SECStatus rv;
    secuPWData pwdata, newpwdata;
    char *oldpw = NULL, *newpw = NULL;

    if (oldPass) {
        pwdata.source = PW_PLAINTEXT;
        pwdata.data = oldPass;
    } else if (oldPwFile) {
        pwdata.source = PW_FROMFILE;
        pwdata.data = oldPwFile;
    } else {
        pwdata.source = PW_NONE;
        pwdata.data = NULL;
    }

    if (newPass) {
        newpwdata.source = PW_PLAINTEXT;
        newpwdata.data = newPass;
    } else if (newPwFile) {
        newpwdata.source = PW_FROMFILE;
        newpwdata.data = newPwFile;
    } else {
        newpwdata.source = PW_NONE;
        newpwdata.data = NULL;
    }

    if (PK11_NeedUserInit(slot)) {
        newpw = secu_InitSlotPassword(slot, PR_FALSE, &pwdata);
        rv = PK11_InitPin(slot, (char *)NULL, newpw);
        goto done;
    }

    for (;;) {
        oldpw = SECU_GetModulePassword(slot, PR_FALSE, &pwdata);

        if (PK11_CheckUserPassword(slot, oldpw) != SECSuccess) {
            if (pwdata.source == PW_NONE) {
                PR_fprintf(PR_STDERR, "Invalid password.  Try again.\n");
            } else {
                PR_fprintf(PR_STDERR, "Invalid password.\n");
                PORT_Memset(oldpw, 0, PL_strlen(oldpw));
                PORT_Free(oldpw);
                rv = SECFailure;
                goto done;
            }
        } else
            break;

        PORT_Free(oldpw);
    }

    newpw = secu_InitSlotPassword(slot, PR_FALSE, &newpwdata);

    rv = PK11_ChangePW(slot, oldpw, newpw);
    if (rv != SECSuccess) {
        PR_fprintf(PR_STDERR, "Failed to change password.\n");
    } else {
        PR_fprintf(PR_STDOUT, "Password changed successfully.\n");
    }

    PORT_Memset(oldpw, 0, PL_strlen(oldpw));
    PORT_Free(oldpw);

done:
    if (newpw) {
        PORT_Memset(newpw, 0, PL_strlen(newpw));
        PORT_Free(newpw);
    }
    return rv;
}

struct matchobj {
    SECItem index;
    char *nname;
    PRBool found;
};

char *
SECU_DefaultSSLDir(void)
{
    char *dir;
    static char sslDir[1000];

    dir = PR_GetEnvSecure("SSL_DIR");
    if (!dir)
        return NULL;

    if (strlen(dir) >= PR_ARRAY_SIZE(sslDir)) {
        return NULL;
    }
    snprintf(sslDir, sizeof(sslDir), "%s", dir);

    if (sslDir[strlen(sslDir) - 1] == '/')
        sslDir[strlen(sslDir) - 1] = 0;

    return sslDir;
}

char *
SECU_AppendFilenameToDir(char *dir, char *filename)
{
    static char path[1000];

    if (dir[strlen(dir) - 1] == '/')
        snprintf(path, sizeof(path), "%s%s", dir, filename);
    else
        snprintf(path, sizeof(path), "%s/%s", dir, filename);
    return path;
}

char *
SECU_ConfigDirectory(const char *base)
{
    static PRBool initted = PR_FALSE;
    const char *dir = ".netscape";
    char *home;
    static char buf[1000];

    if (initted)
        return buf;

    if (base == NULL || *base == 0) {
        home = PR_GetEnvSecure("HOME");
        if (!home)
            home = "";

        if (*home && home[strlen(home) - 1] == '/')
            snprintf(buf, sizeof(buf), "%.900s%s", home, dir);
        else
            snprintf(buf, sizeof(buf), "%.900s/%s", home, dir);
    } else {
        snprintf(buf, sizeof(buf), "%.900s", base);
        if (buf[strlen(buf) - 1] == '/')
            buf[strlen(buf) - 1] = 0;
    }

    initted = PR_TRUE;
    return buf;
}

SECStatus
SECU_ReadDERFromFile(SECItem *der, PRFileDesc *inFile, PRBool ascii,
                     PRBool warnOnPrivateKeyInAsciiFile)
{
    SECStatus rv;
    if (ascii) {
        /* First convert ascii to binary */
        SECItem filedata;

        /* Read in ascii data */
        rv = SECU_FileToItem(&filedata, inFile);
        if (rv != SECSuccess)
            return rv;
        if (!filedata.data) {
            fprintf(stderr, "unable to read data from input file\n");
            return SECFailure;
        }
        /* need one additional byte for zero terminator */
        rv = SECITEM_ReallocItemV2(NULL, &filedata, filedata.len + 1);
        if (rv != SECSuccess) {
            PORT_Free(filedata.data);
            return rv;
        }
        char *asc = (char *)filedata.data;
        asc[filedata.len - 1] = '\0';

        if (warnOnPrivateKeyInAsciiFile && strstr(asc, "PRIVATE KEY")) {
            fprintf(stderr, "Warning: ignoring private key. Consider to use "
                            "pk12util.\n");
        }

        char *body;
        /* check for headers and trailers and remove them */
        if ((body = strstr(asc, "-----BEGIN")) != NULL) {
            char *trailer = NULL;
            asc = body;
            body = PORT_Strchr(body, '\n');
            if (!body)
                body = PORT_Strchr(asc, '\r'); /* maybe this is a MAC file */
            if (body)
                trailer = strstr(++body, "-----END");
            if (trailer != NULL) {
                *trailer = '\0';
            } else {
                fprintf(stderr, "input has header but no trailer\n");
                PORT_Free(filedata.data);
                return SECFailure;
            }
        } else {
            body = asc;
        }

        /* Convert to binary */
        rv = ATOB_ConvertAsciiToItem(der, body);
        if (rv != SECSuccess) {
            fprintf(stderr, "error converting ascii to binary (%s)\n",
                    SECU_Strerror(PORT_GetError()));
            PORT_Free(filedata.data);
            return SECFailure;
        }

        PORT_Free(filedata.data);
    } else {
        /* Read in binary der */
        rv = SECU_FileToItem(der, inFile);
        if (rv != SECSuccess) {
            fprintf(stderr, "error converting der (%s)\n",
                    SECU_Strerror(PORT_GetError()));
            return SECFailure;
        }
    }
    return SECSuccess;
}

#define INDENT_MULT 4

/*
 * remove the tag and length and just leave the bare BER data
 */
SECStatus
SECU_StripTagAndLength(SECItem *i)
{
    unsigned int start;
    PRBool isIndefinite;

    if (!i || !i->data || i->len < 2) { /* must be at least tag and length */
        PORT_SetError(SEC_ERROR_BAD_DER);
        return SECFailure;
    }
    isIndefinite = (i->data[1] == 0x80);
    start = ((i->data[1] & 0x80) ? (i->data[1] & 0x7f) + 2 : 2);
    if (i->len < start) {
        PORT_SetError(SEC_ERROR_BAD_DER);
        return SECFailure;
    }
    i->data += start;
    i->len -= start;
    /* we are using indefinite encoding, drop the trailing zero */
    if (isIndefinite) {
        if (i->len <= 1) {
            PORT_SetError(SEC_ERROR_BAD_DER);
            return SECFailure;
        }
        /* verify tags are zero */
        if ((i->data[i->len - 1] != 0) || (i->data[i->len - 2] != 0)) {
            PORT_SetError(SEC_ERROR_BAD_DER);
            return SECFailure;
        }
        i->len -= 2;
    }

    return SECSuccess;
}

/*
 * Create a new SECItem which points to the current BER tag and length with
 * all it's data. For indefinite encoding, this will also include the trailing
 * indefinite markers
 * The 'in' item is advanced to point to the next BER tag.
 * You don't want to use this in an actual BER/DER parser as NSS already
 * has 3 to choose from)
 */
SECStatus
SECU_ExtractBERAndStep(SECItem *in, SECItem *out)
{
    if (!in || !in->data || in->len < 2) { /* must be at least tag and length */
        PORT_SetError(SEC_ERROR_BAD_DER);
        return SECFailure;
    }

    *out = *in;

    /* first handle indefinite encoding */
    if (out->data[1] == 0x80) {
        SECItem this = *out;
        SECItem next;
        this.data += 2;
        this.len -= 2;
        out->len = 2;
        /* walk through all the entries until we find the '0' */
        while ((this.len >= 2) && (this.data[0] != 0)) {
            SECStatus rv = SECU_ExtractBERAndStep(&this, &next);
            if (rv != SECSuccess) {
                return rv;
            }
            out->len += next.len;
        }
        if ((this.len < 2) || ((this.data[0] != 0) && (this.data[1] != 0))) {
            PORT_SetError(SEC_ERROR_BAD_DER);
            return SECFailure;
        }
        out->len += 2; /* include the trailing zeros */
        in->data += out->len;
        in->len -= out->len;
        return SECSuccess;
    }

    /* now handle normal DER encoding */
    if (out->data[1] & 0x80) {
        unsigned int i;
        unsigned int lenlen = out->data[1] & 0x7f;
        unsigned int len = 0;
        if (lenlen > sizeof out->len) {
            PORT_SetError(SEC_ERROR_BAD_DER);
            return SECFailure;
        }
        for (i = 0; i < lenlen; i++) {
            len = (len << 8) | out->data[2 + i];
        }
        out->len = len + lenlen + 2;
    } else {
        out->len = out->data[1] + 2;
    }
    if (out->len > in->len) {
        /* we've ran into a truncated file */
        PORT_SetError(SEC_ERROR_BAD_DER);
        return SECFailure;
    }
    in->data += out->len;
    in->len -= out->len;
    return SECSuccess;
}

static void
secu_PrintRawStringQuotesOptional(FILE *out, SECItem *si, const char *m,
                                  int level, PRBool quotes)
{
    int column;
    unsigned int i;

    if (m) {
        SECU_Indent(out, level);
        fprintf(out, "%s: ", m);
        column = (level * INDENT_MULT) + strlen(m) + 2;
        level++;
    } else {
        SECU_Indent(out, level);
        column = level * INDENT_MULT;
    }
    if (quotes) {
        fprintf(out, "\"");
        column++;
    }

    for (i = 0; i < si->len; i++) {
        unsigned char val = si->data[i];
        unsigned char c;
        if (SECU_GetWrapEnabled() && column > 76) {
            SECU_Newline(out);
            SECU_Indent(out, level);
            column = level * INDENT_MULT;
        }

        if (utf8DisplayEnabled) {
            if (val < 32)
                c = '.';
            else
                c = val;
        } else {
            c = printable[val];
        }
        fprintf(out, "%c", c);
        column++;
    }

    if (quotes) {
        fprintf(out, "\"");
        column++;
    }
    if (SECU_GetWrapEnabled() &&
        (column != level * INDENT_MULT || column > 76)) {
        SECU_Newline(out);
    }
}

static void
secu_PrintRawString(FILE *out, SECItem *si, const char *m, int level)
{
    secu_PrintRawStringQuotesOptional(out, si, m, level, PR_TRUE);
}

void
SECU_PrintString(FILE *out, const SECItem *si, const char *m, int level)
{
    SECItem my = *si;

    if (SECSuccess != SECU_StripTagAndLength(&my) || !my.len)
        return;
    secu_PrintRawString(out, &my, m, level);
}

/* print an unencoded boolean */
static void
secu_PrintBoolean(FILE *out, SECItem *i, const char *m, int level)
{
    int val = 0;

    if (i->data && i->len) {
        val = i->data[0];
    }

    if (!m) {
        m = "Boolean";
    }
    SECU_Indent(out, level);
    fprintf(out, "%s: %s\n", m, (val ? "True" : "False"));
}

/*
 * Format and print "time".  If the tag message "m" is not NULL,
 * do indent formatting based on "level" and add a newline afterward;
 * otherwise just print the formatted time string only.
 */
static void
secu_PrintTime(FILE *out, const PRTime time, const char *m, int level)
{
    PRExplodedTime printableTime;
    char *timeString;

    /* Convert to local time */
    PR_ExplodeTime(time, PR_GMTParameters, &printableTime);

    timeString = PORT_Alloc(256);
    if (timeString == NULL)
        return;

    if (m != NULL) {
        SECU_Indent(out, level);
        fprintf(out, "%s: ", m);
    }

    if (PR_FormatTime(timeString, 256, "%a %b %d %H:%M:%S %Y", &printableTime)) {
        fputs(timeString, out);
    }

    if (m != NULL)
        fprintf(out, "\n");

    PORT_Free(timeString);
}

/*
 * Format and print the UTC Time "t".  If the tag message "m" is not NULL,
 * do indent formatting based on "level" and add a newline afterward;
 * otherwise just print the formatted time string only.
 */
void
SECU_PrintUTCTime(FILE *out, const SECItem *t, const char *m, int level)
{
    PRTime time;
    SECStatus rv;

    rv = DER_UTCTimeToTime(&time, t);
    if (rv != SECSuccess)
        return;

    secu_PrintTime(out, time, m, level);
}

/*
 * Format and print the Generalized Time "t".  If the tag message "m"
 * is not NULL, * do indent formatting based on "level" and add a newline
 * afterward; otherwise just print the formatted time string only.
 */
void
SECU_PrintGeneralizedTime(FILE *out, const SECItem *t, const char *m, int level)
{
    PRTime time;
    SECStatus rv;

    rv = DER_GeneralizedTimeToTime(&time, t);
    if (rv != SECSuccess)
        return;

    secu_PrintTime(out, time, m, level);
}

/*
 * Format and print the UTC or Generalized Time "t".  If the tag message
 * "m" is not NULL, do indent formatting based on "level" and add a newline
 * afterward; otherwise just print the formatted time string only.
 */
void
SECU_PrintTimeChoice(FILE *out, const SECItem *t, const char *m, int level)
{
    switch (t->type) {
        case siUTCTime:
            SECU_PrintUTCTime(out, t, m, level);
            break;

        case siGeneralizedTime:
            SECU_PrintGeneralizedTime(out, t, m, level);
            break;

        default:
            PORT_Assert(0);
            break;
    }
}

/* This prints a SET or SEQUENCE */
static void
SECU_PrintSet(FILE *out, const SECItem *t, const char *m, int level)
{
    int type = t->data[0] & SEC_ASN1_TAGNUM_MASK;
    int constructed = t->data[0] & SEC_ASN1_CONSTRUCTED;
    const char *label;
    SECItem my = *t;

    if (!constructed) {
        SECU_PrintAsHex(out, t, m, level);
        return;
    }
    if (SECSuccess != SECU_StripTagAndLength(&my))
        return;

    SECU_Indent(out, level);
    if (m) {
        fprintf(out, "%s: ", m);
    }

    if (type == SEC_ASN1_SET)
        label = "Set ";
    else if (type == SEC_ASN1_SEQUENCE)
        label = "Sequence ";
    else
        label = "";
    fprintf(out, "%s{\n", label); /* } */

    while (my.len >= 2) {
        SECItem tmp;
        if (SECSuccess != SECU_ExtractBERAndStep(&my, &tmp)) {
            break;
        }
        SECU_PrintAny(out, &tmp, NULL, level + 1);
    }
    SECU_Indent(out, level);
    fprintf(out, /* { */ "}\n");
}

static void
secu_PrintContextSpecific(FILE *out, const SECItem *i, const char *m, int level)
{
    int type = i->data[0] & SEC_ASN1_TAGNUM_MASK;
    int constructed = i->data[0] & SEC_ASN1_CONSTRUCTED;
    SECItem tmp;

    if (constructed) {
        char *m2;
        if (!m)
            m2 = PR_smprintf("[%d]", type);
        else
            m2 = PR_smprintf("%s: [%d]", m, type);
        if (m2) {
            SECU_PrintSet(out, i, m2, level);
            PR_smprintf_free(m2);
        }
        return;
    }

    SECU_Indent(out, level);
    if (m) {
        fprintf(out, "%s: ", m);
    }
    fprintf(out, "[%d]\n", type);

    tmp = *i;
    if (SECSuccess == SECU_StripTagAndLength(&tmp))
        SECU_PrintAsHex(out, &tmp, m, level + 1);
}

static void
secu_PrintOctetString(FILE *out, const SECItem *i, const char *m, int level)
{
    SECItem tmp = *i;
    if (SECSuccess == SECU_StripTagAndLength(&tmp))
        SECU_PrintAsHex(out, &tmp, m, level);
}

static void
secu_PrintBitString(FILE *out, const SECItem *i, const char *m, int level)
{
    int unused_bits;
    SECItem tmp = *i;

    if (SECSuccess != SECU_StripTagAndLength(&tmp) || tmp.len < 2)
        return;

    unused_bits = *tmp.data++;
    tmp.len--;

    SECU_PrintAsHex(out, &tmp, m, level);
    if (unused_bits) {
        SECU_Indent(out, level + 1);
        fprintf(out, "(%d least significant bits unused)\n", unused_bits);
    }
}

/* in a decoded bit string, the len member is a bit length. */
static void
secu_PrintDecodedBitString(FILE *out, const SECItem *i, const char *m, int level)
{
    int unused_bits;
    SECItem tmp = *i;

    unused_bits = (tmp.len & 0x7) ? 8 - (tmp.len & 7) : 0;
    DER_ConvertBitString(&tmp); /* convert length to byte length */

    SECU_PrintAsHex(out, &tmp, m, level);
    if (unused_bits) {
        SECU_Indent(out, level + 1);
        fprintf(out, "(%d least significant bits unused)\n", unused_bits);
    }
}

/* Print a DER encoded Boolean */
void
SECU_PrintEncodedBoolean(FILE *out, const SECItem *i, const char *m, int level)
{
    SECItem my = *i;
    if (SECSuccess == SECU_StripTagAndLength(&my))
        secu_PrintBoolean(out, &my, m, level);
}

/* Print a DER encoded integer */
void
SECU_PrintEncodedInteger(FILE *out, const SECItem *i, const char *m, int level)
{
    SECItem my = *i;
    if (SECSuccess == SECU_StripTagAndLength(&my))
        SECU_PrintInteger(out, &my, m, level);
}

/* Print a DER encoded OID */
SECOidTag
SECU_PrintEncodedObjectID(FILE *out, const SECItem *i, const char *m, int level)
{
    SECItem my = *i;
    SECOidTag tag = SEC_OID_UNKNOWN;
    if (SECSuccess == SECU_StripTagAndLength(&my))
        tag = SECU_PrintObjectID(out, &my, m, level);
    return tag;
}

static void
secu_PrintBMPString(FILE *out, const SECItem *i, const char *m, int level)
{
    unsigned char *s;
    unsigned char *d;
    int len;
    SECItem tmp = { 0, 0, 0 };
    SECItem my = *i;

    if (SECSuccess != SECU_StripTagAndLength(&my))
        goto loser;
    if (my.len % 2)
        goto loser;
    len = (int)(my.len / 2);
    tmp.data = (unsigned char *)PORT_Alloc(len);
    if (!tmp.data)
        goto loser;
    tmp.len = len;
    for (s = my.data, d = tmp.data; len > 0; len--) {
        PRUint32 bmpChar = (s[0] << 8) | s[1];
        s += 2;
        if (!isprint(bmpChar))
            goto loser;
        *d++ = (unsigned char)bmpChar;
    }
    secu_PrintRawString(out, &tmp, m, level);
    PORT_Free(tmp.data);
    return;

loser:
    SECU_PrintAsHex(out, i, m, level);
    if (tmp.data)
        PORT_Free(tmp.data);
}

static void
secu_PrintUniversalString(FILE *out, const SECItem *i, const char *m, int level)
{
    unsigned char *s;
    unsigned char *d;
    int len;
    SECItem tmp = { 0, 0, 0 };
    SECItem my = *i;

    if (SECSuccess != SECU_StripTagAndLength(&my))
        goto loser;
    if (my.len % 4)
        goto loser;
    len = (int)(my.len / 4);
    tmp.data = (unsigned char *)PORT_Alloc(len);
    if (!tmp.data)
        goto loser;
    tmp.len = len;
    for (s = my.data, d = tmp.data; len > 0; len--) {
        PRUint32 bmpChar = (s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3];
        s += 4;
        if (!isprint(bmpChar & 0xFF))
            goto loser;
        *d++ = (unsigned char)bmpChar;
    }
    secu_PrintRawString(out, &tmp, m, level);
    PORT_Free(tmp.data);
    return;

loser:
    SECU_PrintAsHex(out, i, m, level);
    if (tmp.data)
        PORT_Free(tmp.data);
}

static void
secu_PrintUniversal(FILE *out, const SECItem *i, const char *m, int level)
{
    switch (i->data[0] & SEC_ASN1_TAGNUM_MASK) {
        case SEC_ASN1_ENUMERATED:
        case SEC_ASN1_INTEGER:
            SECU_PrintEncodedInteger(out, i, m, level);
            break;
        case SEC_ASN1_OBJECT_ID:
            SECU_PrintEncodedObjectID(out, i, m, level);
            break;
        case SEC_ASN1_BOOLEAN:
            SECU_PrintEncodedBoolean(out, i, m, level);
            break;
        case SEC_ASN1_UTF8_STRING:
        case SEC_ASN1_PRINTABLE_STRING:
        case SEC_ASN1_VISIBLE_STRING:
        case SEC_ASN1_IA5_STRING:
        case SEC_ASN1_T61_STRING:
            SECU_PrintString(out, i, m, level);
            break;
        case SEC_ASN1_GENERALIZED_TIME:
            SECU_PrintGeneralizedTime(out, i, m, level);
            break;
        case SEC_ASN1_UTC_TIME:
            SECU_PrintUTCTime(out, i, m, level);
            break;
        case SEC_ASN1_NULL:
            SECU_Indent(out, level);
            if (m && m[0])
                fprintf(out, "%s: NULL\n", m);
            else
                fprintf(out, "NULL\n");
            break;
        case SEC_ASN1_SET:
        case SEC_ASN1_SEQUENCE:
            SECU_PrintSet(out, i, m, level);
            break;
        case SEC_ASN1_OCTET_STRING:
            secu_PrintOctetString(out, i, m, level);
            break;
        case SEC_ASN1_BIT_STRING:
            secu_PrintBitString(out, i, m, level);
            break;
        case SEC_ASN1_BMP_STRING:
            secu_PrintBMPString(out, i, m, level);
            break;
        case SEC_ASN1_UNIVERSAL_STRING:
            secu_PrintUniversalString(out, i, m, level);
            break;
        default:
            SECU_PrintAsHex(out, i, m, level);
            break;
    }
}

void
SECU_PrintAny(FILE *out, const SECItem *i, const char *m, int level)
{
    if (i && i->len && i->data) {
        switch (i->data[0] & SEC_ASN1_CLASS_MASK) {
            case SEC_ASN1_CONTEXT_SPECIFIC:
                secu_PrintContextSpecific(out, i, m, level);
                break;
            case SEC_ASN1_UNIVERSAL:
                secu_PrintUniversal(out, i, m, level);
                break;
            default:
                SECU_PrintAsHex(out, i, m, level);
                break;
        }
    }
}

static int
secu_PrintValidity(FILE *out, CERTValidity *v, char *m, int level)
{
    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintTimeChoice(out, &v->notBefore, "Not Before", level + 1);
    SECU_PrintTimeChoice(out, &v->notAfter, "Not After ", level + 1);
    return 0;
}

/* This function does NOT expect a DER type and length. */
SECOidTag
SECU_PrintObjectID(FILE *out, const SECItem *oid, const char *m, int level)
{
    SECOidData *oiddata;
    char *oidString = NULL;

    oiddata = SECOID_FindOID(oid);
    if (oiddata != NULL) {
        const char *name = oiddata->desc;
        SECU_Indent(out, level);
        if (m != NULL)
            fprintf(out, "%s: ", m);
        fprintf(out, "%s\n", name);
        return oiddata->offset;
    }
    oidString = CERT_GetOidString(oid);
    if (oidString) {
        SECU_Indent(out, level);
        if (m != NULL)
            fprintf(out, "%s: ", m);
        fprintf(out, "%s\n", oidString);
        PR_smprintf_free(oidString);
        return SEC_OID_UNKNOWN;
    }
    SECU_PrintAsHex(out, oid, m, level);
    return SEC_OID_UNKNOWN;
}

void
SECU_PrintOidTag(FILE *out, SECOidTag tag, const char *m, int level)
{
    const char *desc = SECOID_FindOIDTagDescription(tag);

    if (desc == NULL) {
        desc = SECOID_FindOIDTagDescription(SEC_OID_UNKNOWN);
    }
    SECU_Indent(out, level);
    if (m != NULL) {
        fprintf(out, "%s: ", m);
    }
    fprintf(out, "%s\n", desc);
}

typedef struct secuPBEParamsStr {
    SECItem salt;
    SECItem iterationCount;
    SECItem keyLength;
    SECAlgorithmID cipherAlg;
    SECAlgorithmID kdfAlg;
} secuPBEParams;

SEC_ASN1_MKSUB(SECOID_AlgorithmIDTemplate)

/* SECOID_PKCS5_PBKDF2 */
const SEC_ASN1Template secuKDF2Params[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(secuPBEParams) },
    { SEC_ASN1_OCTET_STRING, offsetof(secuPBEParams, salt) },
    { SEC_ASN1_INTEGER, offsetof(secuPBEParams, iterationCount) },
    { SEC_ASN1_INTEGER | SEC_ASN1_OPTIONAL, offsetof(secuPBEParams, keyLength) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN | SEC_ASN1_OPTIONAL, offsetof(secuPBEParams, kdfAlg),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { 0 }
};

/* PKCS5v1 & PKCS12 */
const SEC_ASN1Template secuPBEParamsTemp[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(secuPBEParams) },
    { SEC_ASN1_OCTET_STRING, offsetof(secuPBEParams, salt) },
    { SEC_ASN1_INTEGER, offsetof(secuPBEParams, iterationCount) },
    { 0 }
};

/* SEC_OID_PKCS5_PBES2, SEC_OID_PKCS5_PBMAC1 */
const SEC_ASN1Template secuPBEV2Params[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(secuPBEParams) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN, offsetof(secuPBEParams, kdfAlg),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN, offsetof(secuPBEParams, cipherAlg),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { 0 }
};

void
secu_PrintRSAPSSParams(FILE *out, SECItem *value, char *m, int level)
{
    PLArenaPool *pool = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    SECStatus rv;
    SECKEYRSAPSSParams param;
    SECAlgorithmID maskHashAlg;

    if (m) {
        SECU_Indent(out, level);
        fprintf(out, "%s:\n", m);
    }

    if (!pool) {
        SECU_Indent(out, level);
        fprintf(out, "Out of memory\n");
        return;
    }

    PORT_Memset(&param, 0, sizeof param);

    rv = SEC_QuickDERDecodeItem(pool, &param,
                                SEC_ASN1_GET(SECKEY_RSAPSSParamsTemplate),
                                value);
    if (rv == SECSuccess) {
        if (!param.hashAlg) {
            SECU_Indent(out, level + 1);
            fprintf(out, "Hash algorithm: default, SHA-1\n");
        } else {
            SECU_PrintObjectID(out, &param.hashAlg->algorithm,
                               "Hash algorithm", level + 1);
        }
        if (!param.maskAlg) {
            SECU_Indent(out, level + 1);
            fprintf(out, "Mask algorithm: default, MGF1\n");
            SECU_Indent(out, level + 1);
            fprintf(out, "Mask hash algorithm: default, SHA-1\n");
        } else {
            SECU_PrintObjectID(out, &param.maskAlg->algorithm,
                               "Mask algorithm", level + 1);
            rv = SEC_QuickDERDecodeItem(pool, &maskHashAlg,
                                        SEC_ASN1_GET(SECOID_AlgorithmIDTemplate),
                                        &param.maskAlg->parameters);
            if (rv == SECSuccess) {
                SECU_PrintObjectID(out, &maskHashAlg.algorithm,
                                   "Mask hash algorithm", level + 1);
            } else {
                SECU_Indent(out, level + 1);
                fprintf(out, "Invalid mask generation algorithm parameters\n");
            }
        }
        if (!param.saltLength.data) {
            SECU_Indent(out, level + 1);
            fprintf(out, "Salt length: default, %i (0x%2X)\n", 20, 20);
        } else {
            SECU_PrintInteger(out, &param.saltLength, "Salt length", level + 1);
        }
    } else {
        SECU_Indent(out, level + 1);
        fprintf(out, "Invalid RSA-PSS parameters\n");
    }
    PORT_FreeArena(pool, PR_FALSE);
}

void
secu_PrintKDF2Params(FILE *out, SECItem *value, char *m, int level)
{
    PLArenaPool *pool = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    SECStatus rv;
    secuPBEParams param;

    if (m) {
        SECU_Indent(out, level);
        fprintf(out, "%s:\n", m);
    }

    if (!pool) {
        SECU_Indent(out, level);
        fprintf(out, "Out of memory\n");
        return;
    }

    PORT_Memset(&param, 0, sizeof param);
    rv = SEC_QuickDERDecodeItem(pool, &param, secuKDF2Params, value);
    if (rv == SECSuccess) {
        SECU_PrintAsHex(out, &param.salt, "Salt", level + 1);
        SECU_PrintInteger(out, &param.iterationCount, "Iteration Count",
                          level + 1);
        if (param.keyLength.data != NULL) {
            SECU_PrintInteger(out, &param.keyLength, "Key Length", level + 1);
        }
        if (param.kdfAlg.algorithm.data != NULL) {
            SECU_PrintAlgorithmID(out, &param.kdfAlg, "KDF algorithm", level + 1);
        } else {
            SECU_Indent(out, level + 1);
            fprintf(out, "Implicit KDF Algorithm: HMAC-SHA-1\n");
        }
    }
    PORT_FreeArena(pool, PR_FALSE);
}

void
secu_PrintPKCS5V2Params(FILE *out, SECItem *value, char *m, int level)
{
    PLArenaPool *pool = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    SECStatus rv;
    secuPBEParams param;

    if (m) {
        SECU_Indent(out, level);
        fprintf(out, "%s:\n", m);
    }

    if (!pool) {
        SECU_Indent(out, level);
        fprintf(out, "Out of memory\n");
        return;
    }

    PORT_Memset(&param, 0, sizeof param);
    rv = SEC_QuickDERDecodeItem(pool, &param, secuPBEV2Params, value);
    if (rv == SECSuccess) {
        SECU_PrintAlgorithmID(out, &param.kdfAlg, "KDF", level + 1);
        SECU_PrintAlgorithmID(out, &param.cipherAlg, "Cipher", level + 1);
    }
    PORT_FreeArena(pool, PR_FALSE);
}

void
secu_PrintPBEParams(FILE *out, SECItem *value, char *m, int level)
{
    PLArenaPool *pool = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    SECStatus rv;
    secuPBEParams param;

    if (m) {
        SECU_Indent(out, level);
        fprintf(out, "%s:\n", m);
    }

    if (!pool) {
        SECU_Indent(out, level);
        fprintf(out, "Out of memory\n");
        return;
    }

    PORT_Memset(&param, 0, sizeof(secuPBEParams));
    rv = SEC_QuickDERDecodeItem(pool, &param, secuPBEParamsTemp, value);
    if (rv == SECSuccess) {
        SECU_PrintAsHex(out, &param.salt, "Salt", level + 1);
        SECU_PrintInteger(out, &param.iterationCount, "Iteration Count",
                          level + 1);
    }
    PORT_FreeArena(pool, PR_FALSE);
}

/* This function does NOT expect a DER type and length. */
void
SECU_PrintAlgorithmID(FILE *out, SECAlgorithmID *a, char *m, int level)
{
    SECOidTag algtag;
    SECU_PrintObjectID(out, &a->algorithm, m, level);

    algtag = SECOID_GetAlgorithmTag(a);
    if (SEC_PKCS5IsAlgorithmPBEAlgTag(algtag)) {
        switch (algtag) {
            case SEC_OID_PKCS5_PBKDF2:
                secu_PrintKDF2Params(out, &a->parameters, "Parameters", level + 1);
                break;
            case SEC_OID_PKCS5_PBES2:
                secu_PrintPKCS5V2Params(out, &a->parameters, "Encryption", level + 1);
                break;
            case SEC_OID_PKCS5_PBMAC1:
                secu_PrintPKCS5V2Params(out, &a->parameters, "MAC", level + 1);
                break;
            default:
                secu_PrintPBEParams(out, &a->parameters, "Parameters", level + 1);
                break;
        }
        return;
    }

    if (a->parameters.len == 0 ||
        (a->parameters.len == 2 &&
         PORT_Memcmp(a->parameters.data, "\005\000", 2) == 0)) {
        /* No arguments or NULL argument */
    } else if (algtag == SEC_OID_PKCS1_RSA_PSS_SIGNATURE) {
        secu_PrintRSAPSSParams(out, &a->parameters, "Parameters", level + 1);
    } else {
        /* Print args to algorithm */
        SECU_PrintAsHex(out, &a->parameters, "Args", level + 1);
    }
}

static void
secu_PrintAttribute(FILE *out, SEC_PKCS7Attribute *attr, char *m, int level)
{
    SECItem *value;
    int i;
    char om[100];

    if (m) {
        SECU_Indent(out, level);
        fprintf(out, "%s:\n", m);
    }

    /*
     * Should make this smarter; look at the type field and then decode
     * and print the value(s) appropriately!
     */
    SECU_PrintObjectID(out, &(attr->type), "Type", level + 1);
    if (attr->values != NULL) {
        i = 0;
        while ((value = attr->values[i++]) != NULL) {
            snprintf(om, sizeof(om), "Value (%d)%s", i, attr->encoded ? " (encoded)" : "");
            if (attr->encoded || attr->typeTag == NULL) {
                SECU_PrintAny(out, value, om, level + 1);
            } else {
                switch (attr->typeTag->offset) {
                    default:
                        SECU_PrintAsHex(out, value, om, level + 1);
                        break;
                    case SEC_OID_PKCS9_CONTENT_TYPE:
                        SECU_PrintObjectID(out, value, om, level + 1);
                        break;
                    case SEC_OID_PKCS9_SIGNING_TIME:
                        SECU_PrintTimeChoice(out, value, om, level + 1);
                        break;
                }
            }
        }
    }
}

static void
secu_PrintECPublicKey(FILE *out, SECKEYPublicKey *pk, char *m, int level)
{
    SECItem curveOID = { siBuffer, NULL, 0 };

    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintInteger(out, &pk->u.ec.publicValue, "PublicValue", level + 1);
    /* For named curves, the DEREncodedParams field contains an
     * ASN Object ID (0x06 is SEC_ASN1_OBJECT_ID).
     */
    if ((pk->u.ec.DEREncodedParams.len > 2) &&
        (pk->u.ec.DEREncodedParams.data[0] == 0x06)) {
        curveOID.len = pk->u.ec.DEREncodedParams.data[1];
        curveOID.data = pk->u.ec.DEREncodedParams.data + 2;
        curveOID.len = PR_MIN(curveOID.len, pk->u.ec.DEREncodedParams.len - 2);
        SECU_PrintObjectID(out, &curveOID, "Curve", level + 1);
    }
}

void
SECU_PrintRSAPublicKey(FILE *out, SECKEYPublicKey *pk, char *m, int level)
{
    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintInteger(out, &pk->u.rsa.modulus, "Modulus", level + 1);
    SECU_PrintInteger(out, &pk->u.rsa.publicExponent, "Exponent", level + 1);
    if (pk->u.rsa.publicExponent.len == 1 &&
        pk->u.rsa.publicExponent.data[0] == 1) {
        SECU_Indent(out, level + 1);
        fprintf(out, "Error: INVALID RSA KEY!\n");
    }
}

void
SECU_PrintDSAPublicKey(FILE *out, SECKEYPublicKey *pk, char *m, int level)
{
    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintInteger(out, &pk->u.dsa.params.prime, "Prime", level + 1);
    SECU_PrintInteger(out, &pk->u.dsa.params.subPrime, "Subprime", level + 1);
    SECU_PrintInteger(out, &pk->u.dsa.params.base, "Base", level + 1);
    SECU_PrintInteger(out, &pk->u.dsa.publicValue, "PublicValue", level + 1);
}

void
SECU_PrintMLDSAPublicKey(FILE *out, SECKEYPublicKey *pk, char *m, int level)
{

    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);

    SECU_PrintOidTag(out, pk->u.mldsa.paramSet, "Parameter Set", level + 1);
    SECU_PrintInteger(out, &pk->u.mldsa.publicValue, "PublicValue", level + 1);
}

void
SECU_PrintMLKEMPublicKey(FILE *out, SECKEYPublicKey *pk, char *m, int level)
{

    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);

    switch (pk->u.kyber.params) {
        case params_ml_kem512:
            SECU_PrintOidTag(out, SEC_OID_ML_KEM_512, "Parameter Set", level + 1);
            break;
        case params_ml_kem512_test_mode:
            SECU_PrintOidTag(out, SEC_OID_ML_KEM_512, "Parameter Set (test mode)",
                             level + 1);
            break;
        case params_ml_kem768:
            SECU_PrintOidTag(out, SEC_OID_ML_KEM_768, "Parameter Set", level + 1);
            break;
        case params_ml_kem768_test_mode:
            SECU_PrintOidTag(out, SEC_OID_ML_KEM_768, "Parameter Set (test mode)",
                             level + 1);
            break;
        case params_ml_kem1024:
            SECU_PrintOidTag(out, SEC_OID_ML_KEM_1024, "Parameter Set", level + 1);
            break;
        case params_ml_kem1024_test_mode:
            SECU_PrintOidTag(out, SEC_OID_ML_KEM_1024, "Parameter Set (test mode)",
                             level + 1);
            break;
        case params_kyber_invalid:
            SECU_Indent(out, level);
            fprintf(out, "Parameter Set: Invalid Params\n");
            break;
        default:
            SECU_Indent(out, level);
            fprintf(out, "Parameter Set: Invalid Params %d\n", pk->u.kyber.params);
            break;
    }
    SECU_PrintInteger(out, &pk->u.kyber.publicValue, "PublicValue", level + 1);
}

static void
secu_PrintSubjectPublicKeyInfo(FILE *out, PLArenaPool *arena,
                               CERTSubjectPublicKeyInfo *i, char *msg, int level)
{
    SECKEYPublicKey *pk;

    SECU_Indent(out, level);
    fprintf(out, "%s:\n", msg);
    SECU_PrintAlgorithmID(out, &i->algorithm, "Public Key Algorithm", level + 1);

    pk = SECKEY_ExtractPublicKey(i);
    if (pk) {
        switch (pk->keyType) {
            case rsaKey:
                SECU_PrintRSAPublicKey(out, pk, "RSA Public Key", level + 1);
                break;

            case dsaKey:
                SECU_PrintDSAPublicKey(out, pk, "DSA Public Key", level + 1);
                break;

            case ecKey:
                secu_PrintECPublicKey(out, pk, "EC Public Key", level + 1);
                break;

            case ecMontKey:
                secu_PrintECPublicKey(out, pk, "EC Montgomery Public Key", level + 1);
                break;

            case edKey:
                secu_PrintECPublicKey(out, pk, "EC Edwards Public Key", level + 1);
                break;

            case mldsaKey:
                SECU_PrintMLDSAPublicKey(out, pk, "ML-DSA Public Key", level + 1);
                break;

            case kyberKey:
                SECU_PrintMLKEMPublicKey(out, pk, "ML-KEM Public Key", level + 1);
                break;

            case dhKey:
            case fortezzaKey:
            case keaKey:
                SECU_Indent(out, level);
                fprintf(out, "unable to format this SPKI algorithm type\n");
                goto loser;
            default:
                SECU_Indent(out, level);
                fprintf(out, "unknown SPKI algorithm type %d\n", pk->keyType);
                goto loser;
        }
        PORT_FreeArena(pk->arena, PR_FALSE);
    } else {
        SECU_PrintErrMsg(out, level, "Error", "Parsing public key");
    loser:
        if (i->subjectPublicKey.data) {
            SECItem tmp = i->subjectPublicKey;
            DER_ConvertBitString(&tmp);
            SECU_PrintAny(out, &tmp, "Raw", level);
        }
    }
}

static void
printStringWithoutCRLF(FILE *out, const char *str)
{
    const char *c = str;
    while (*c) {
        if (*c != '\r' && *c != '\n') {
            fputc(*c, out);
        }
        ++c;
    }
}

int
SECU_PrintDumpDerIssuerAndSerial(FILE *out, const SECItem *der, const char *m,
                                 int level)
{
    PLArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    CERTCertificate *c;
    int rv = SEC_ERROR_NO_MEMORY;
    char *derIssuerB64;
    char *derSerialB64;

    if (!arena)
        return rv;

    /* Decode certificate */
    c = PORT_ArenaZNew(arena, CERTCertificate);
    if (!c)
        goto loser;
    c->arena = arena;
    rv = SEC_ASN1DecodeItem(arena, c,
                            SEC_ASN1_GET(CERT_CertificateTemplate), der);
    if (rv) {
        SECU_PrintErrMsg(out, 0, "Error", "Parsing extension");
        goto loser;
    }

    SECU_PrintName(out, &c->subject, "Subject", 0);
    if (!SECU_GetWrapEnabled()) /*SECU_PrintName didn't add newline*/
        SECU_Newline(out);
    SECU_PrintName(out, &c->issuer, "Issuer", 0);
    if (!SECU_GetWrapEnabled()) /*SECU_PrintName didn't add newline*/
        SECU_Newline(out);
    SECU_PrintInteger(out, &c->serialNumber, "Serial Number", 0);

    derIssuerB64 = BTOA_ConvertItemToAscii(&c->derIssuer);
    derSerialB64 = BTOA_ConvertItemToAscii(&c->serialNumber);

    fprintf(out, "Issuer DER Base64:\n");
    if (SECU_GetWrapEnabled()) {
        fprintf(out, "%s\n", derIssuerB64);
    } else {
        printStringWithoutCRLF(out, derIssuerB64);
        fputs("\n", out);
    }

    fprintf(out, "Serial DER Base64:\n");
    if (SECU_GetWrapEnabled()) {
        fprintf(out, "%s\n", derSerialB64);
    } else {
        printStringWithoutCRLF(out, derSerialB64);
        fputs("\n", out);
    }

    PORT_Free(derIssuerB64);
    PORT_Free(derSerialB64);

    fprintf(out, "Serial DER as C source: \n{ %d, \"", c->serialNumber.len);

    {
        unsigned int i;
        for (i = 0; i < c->serialNumber.len; ++i) {
            unsigned char *chardata = (unsigned char *)(c->serialNumber.data);
            unsigned char ch = *(chardata + i);

            fprintf(out, "\\x%02x", ch);
        }
        fprintf(out, "\" }\n");
    }

loser:
    PORT_FreeArena(arena, PR_FALSE);
    return rv;
}

static SECStatus
secu_PrintX509InvalidDate(FILE *out, SECItem *value, char *msg, int level)
{
    SECItem decodedValue;
    SECStatus rv;
    PRTime invalidTime;
    char *formattedTime = NULL;

    decodedValue.data = NULL;
    rv = SEC_ASN1DecodeItem(NULL, &decodedValue,
                            SEC_ASN1_GET(SEC_GeneralizedTimeTemplate),
                            value);
    if (rv == SECSuccess) {
        rv = DER_GeneralizedTimeToTime(&invalidTime, &decodedValue);
        if (rv == SECSuccess) {
            formattedTime = CERT_GenTime2FormattedAscii(invalidTime, "%a %b %d %H:%M:%S %Y");
            SECU_Indent(out, level + 1);
            fprintf(out, "%s: %s\n", msg, formattedTime);
            PORT_Free(formattedTime);
        }
    }
    PORT_Free(decodedValue.data);
    return (rv);
}

static SECStatus
PrintExtKeyUsageExtension(FILE *out, SECItem *value, char *msg, int level)
{
    CERTOidSequence *os;
    SECItem **op;

    os = CERT_DecodeOidSequence(value);
    if ((CERTOidSequence *)NULL == os) {
        return SECFailure;
    }

    for (op = os->oids; *op; op++) {
        SECU_PrintObjectID(out, *op, msg, level + 1);
    }
    CERT_DestroyOidSequence(os);
    return SECSuccess;
}

static SECStatus
secu_PrintBasicConstraints(FILE *out, SECItem *value, char *msg, int level)
{
    CERTBasicConstraints constraints;
    SECStatus rv;

    SECU_Indent(out, level);
    if (msg) {
        fprintf(out, "%s: ", msg);
    }
    rv = CERT_DecodeBasicConstraintValue(&constraints, value);
    if (rv == SECSuccess && constraints.isCA) {
        if (constraints.pathLenConstraint >= 0) {
            fprintf(out, "Is a CA with a maximum path length of %d.\n",
                    constraints.pathLenConstraint);
        } else {
            fprintf(out, "Is a CA with no maximum path length.\n");
        }
    } else {
        fprintf(out, "Is not a CA.\n");
    }
    return SECSuccess;
}

static const char *const nsTypeBits[] = {
    "SSL Client",
    "SSL Server",
    "S/MIME",
    "Object Signing",
    "Reserved",
    "SSL CA",
    "S/MIME CA",
    "ObjectSigning CA"
};

/* NSCertType is merely a bit string whose bits are displayed symbolically */
static SECStatus
secu_PrintNSCertType(FILE *out, SECItem *value, char *msg, int level)
{
    int unused;
    int NS_Type;
    int i;
    int found = 0;
    SECItem my = *value;

    if ((my.data[0] != SEC_ASN1_BIT_STRING) ||
        SECSuccess != SECU_StripTagAndLength(&my)) {
        SECU_PrintAny(out, value, "Data", level);
        return SECSuccess;
    }

    unused = (my.len == 2) ? (my.data[0] & 0x0f) : 0;
    NS_Type = my.data[1] & (0xff << unused);

    SECU_Indent(out, level);
    if (msg) {
        fprintf(out, "%s: ", msg);
    } else {
        fprintf(out, "Netscape Certificate Type: ");
    }
    for (i = 0; i < 8; i++) {
        if ((0x80 >> i) & NS_Type) {
            fprintf(out, "%c%s", (found ? ',' : '<'), nsTypeBits[i]);
            found = 1;
        }
    }
    fprintf(out, (found ? ">\n" : "none\n"));
    return SECSuccess;
}

static const char *const usageBits[] = {
    "Digital Signature",   /* 0x80 */
    "Non-Repudiation",     /* 0x40 */
    "Key Encipherment",    /* 0x20 */
    "Data Encipherment",   /* 0x10 */
    "Key Agreement",       /* 0x08 */
    "Certificate Signing", /* 0x04 */
    "CRL Signing",         /* 0x02 */
    "Encipher Only",       /* 0x01 */
    "Decipher Only",       /* 0x0080 */
    NULL
};

/* X509KeyUsage is merely a bit string whose bits are displayed symbolically */
static void
secu_PrintX509KeyUsage(FILE *out, SECItem *value, char *msg, int level)
{
    int unused;
    int usage;
    int i;
    int found = 0;
    SECItem my = *value;

    if ((my.data[0] != SEC_ASN1_BIT_STRING) ||
        SECSuccess != SECU_StripTagAndLength(&my)) {
        SECU_PrintAny(out, value, "Data", level);
        return;
    }

    unused = (my.len >= 2) ? (my.data[0] & 0x0f) : 0;
    usage = (my.len == 2) ? (my.data[1] & (0xff << unused)) << 8
                          : (my.data[1] << 8) |
                                (my.data[2] & (0xff << unused));

    SECU_Indent(out, level);
    fprintf(out, "Usages: ");
    for (i = 0; usageBits[i]; i++) {
        if ((0x8000 >> i) & usage) {
            if (found)
                SECU_Indent(out, level + 2);
            fprintf(out, "%s\n", usageBits[i]);
            found = 1;
        }
    }
    if (!found) {
        fprintf(out, "(none)\n");
    }
}

static void
secu_PrintIPAddress(FILE *out, SECItem *value, char *msg, int level)
{
    PRStatus st;
    PRNetAddr addr;
    char addrBuf[80];

    memset(&addr, 0, sizeof addr);
    if (value->len == 4) {
        addr.inet.family = PR_AF_INET;
        memcpy(&addr.inet.ip, value->data, value->len);
    } else if (value->len == 16) {
        addr.ipv6.family = PR_AF_INET6;
        memcpy(addr.ipv6.ip.pr_s6_addr, value->data, value->len);
        if (PR_IsNetAddrType(&addr, PR_IpAddrV4Mapped)) {
            /* convert to IPv4.  */
            addr.inet.family = PR_AF_INET;
            memcpy(&addr.inet.ip, &addr.ipv6.ip.pr_s6_addr[12], 4);
            memset(&addr.inet.pad[0], 0, sizeof addr.inet.pad);
        }
    } else {
        goto loser;
    }

    st = PR_NetAddrToString(&addr, addrBuf, sizeof addrBuf);
    if (st == PR_SUCCESS) {
        SECU_Indent(out, level);
        fprintf(out, "%s: %s\n", msg, addrBuf);
    } else {
    loser:
        SECU_PrintAsHex(out, value, msg, level);
    }
}

static void
secu_PrintGeneralName(FILE *out, CERTGeneralName *gname, char *msg, int level)
{
    char label[40];
    if (msg && msg[0]) {
        SECU_Indent(out, level++);
        fprintf(out, "%s: \n", msg);
    }
    switch (gname->type) {
        case certOtherName:
            SECU_PrintAny(out, &gname->name.OthName.name, "Other Name", level);
            SECU_PrintObjectID(out, &gname->name.OthName.oid, "OID", level + 1);
            break;
        case certDirectoryName:
            SECU_PrintName(out, &gname->name.directoryName, "Directory Name", level);
            break;
        case certRFC822Name:
            secu_PrintRawString(out, &gname->name.other, "RFC822 Name", level);
            break;
        case certDNSName:
            secu_PrintRawString(out, &gname->name.other, "DNS name", level);
            break;
        case certURI:
            secu_PrintRawString(out, &gname->name.other, "URI", level);
            break;
        case certIPAddress:
            secu_PrintIPAddress(out, &gname->name.other, "IP Address", level);
            break;
        case certRegisterID:
            SECU_PrintObjectID(out, &gname->name.other, "Registered ID", level);
            break;
        case certX400Address:
            SECU_PrintAny(out, &gname->name.other, "X400 Address", level);
            break;
        case certEDIPartyName:
            SECU_PrintAny(out, &gname->name.other, "EDI Party", level);
            break;
        default:
            PR_snprintf(label, sizeof label, "unknown type [%d]",
                        (int)gname->type - 1);
            SECU_PrintAsHex(out, &gname->name.other, label, level);
            break;
    }
}

static void
secu_PrintGeneralNames(FILE *out, CERTGeneralName *gname, char *msg, int level)
{
    CERTGeneralName *name = gname;
    do {
        secu_PrintGeneralName(out, name, msg, level);
        name = CERT_GetNextGeneralName(name);
    } while (name && name != gname);
}

static void
secu_PrintAuthKeyIDExtension(FILE *out, SECItem *value, char *msg, int level)
{
    CERTAuthKeyID *kid = NULL;
    PLArenaPool *pool = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);

    if (!pool) {
        SECU_PrintError("Error", "Allocating new ArenaPool");
        return;
    }
    kid = CERT_DecodeAuthKeyID(pool, value);
    if (!kid) {
        SECU_PrintErrMsg(out, level, "Error", "Parsing extension");
        SECU_PrintAny(out, value, "Data", level);
    } else {
        int keyIDPresent = (kid->keyID.data && kid->keyID.len);
        int issuerPresent = kid->authCertIssuer != NULL;
        int snPresent = (kid->authCertSerialNumber.data &&
                         kid->authCertSerialNumber.len);

        if (keyIDPresent)
            SECU_PrintAsHex(out, &kid->keyID, "Key ID", level);
        if (issuerPresent)
            secu_PrintGeneralName(out, kid->authCertIssuer, "Issuer", level);
        if (snPresent)
            SECU_PrintInteger(out, &kid->authCertSerialNumber,
                              "Serial Number", level);
    }
    PORT_FreeArena(pool, PR_FALSE);
}

static void
secu_PrintAltNameExtension(FILE *out, SECItem *value, char *msg, int level)
{
    CERTGeneralName *nameList;
    CERTGeneralName *current;
    PLArenaPool *pool = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);

    if (!pool) {
        SECU_PrintError("Error", "Allocating new ArenaPool");
        return;
    }
    nameList = current = CERT_DecodeAltNameExtension(pool, value);
    if (!current) {
        if (PORT_GetError() == SEC_ERROR_EXTENSION_NOT_FOUND) {
            /* Decoder found empty sequence, which is invalid. */
            PORT_SetError(SEC_ERROR_EXTENSION_VALUE_INVALID);
        }
        SECU_PrintErrMsg(out, level, "Error", "Parsing extension");
        SECU_PrintAny(out, value, "Data", level);
    } else {
        do {
            secu_PrintGeneralName(out, current, msg, level);
            current = CERT_GetNextGeneralName(current);
        } while (current != nameList);
    }
    PORT_FreeArena(pool, PR_FALSE);
}

static void
secu_PrintCRLDistPtsExtension(FILE *out, SECItem *value, char *msg, int level)
{
    CERTCrlDistributionPoints *dPoints;
    PLArenaPool *pool = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);

    if (!pool) {
        SECU_PrintError("Error", "Allocating new ArenaPool");
        return;
    }
    dPoints = CERT_DecodeCRLDistributionPoints(pool, value);
    if (dPoints && dPoints->distPoints && dPoints->distPoints[0]) {
        CRLDistributionPoint **pPoints = dPoints->distPoints;
        CRLDistributionPoint *pPoint;
        while (NULL != (pPoint = *pPoints++)) {
            SECU_Indent(out, level);
            fputs("Distribution point:\n", out);
            if (pPoint->distPointType == generalName &&
                pPoint->distPoint.fullName != NULL) {
                secu_PrintGeneralNames(out, pPoint->distPoint.fullName, NULL,
                                       level + 1);
            } else if (pPoint->distPointType == relativeDistinguishedName &&
                       pPoint->distPoint.relativeName.avas) {
                SECU_PrintRDN(out, &pPoint->distPoint.relativeName, "RDN",
                              level + 1);
            } else if (pPoint->derDistPoint.data) {
                SECU_PrintAny(out, &pPoint->derDistPoint, "Point", level + 1);
            }
            if (pPoint->reasons.data) {
                secu_PrintDecodedBitString(out, &pPoint->reasons, "Reasons",
                                           level + 1);
            }
            if (pPoint->crlIssuer) {
                secu_PrintGeneralName(out, pPoint->crlIssuer, "CRL issuer",
                                      level + 1);
            }
        }
    } else {
        SECU_PrintErrMsg(out, level, "Error", "Parsing extension");
        SECU_PrintAny(out, value, "Data", level);
    }
    PORT_FreeArena(pool, PR_FALSE);
}

static void
secu_PrintNameConstraintSubtree(FILE *out, CERTNameConstraint *value,
                                char *msg, int level)
{
    CERTNameConstraint *head = value;
    SECU_Indent(out, level);
    fprintf(out, "%s Subtree:\n", msg);
    level++;
    do {
        secu_PrintGeneralName(out, &value->name, NULL, level);
        if (value->min.data)
            SECU_PrintInteger(out, &value->min, "Minimum", level + 1);
        if (value->max.data)
            SECU_PrintInteger(out, &value->max, "Maximum", level + 1);
        value = CERT_GetNextNameConstraint(value);
    } while (value != head);
}

static void
secu_PrintNameConstraintsExtension(FILE *out, SECItem *value, char *msg, int level)
{
    CERTNameConstraints *cnstrnts;
    PLArenaPool *pool = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);

    if (!pool) {
        SECU_PrintError("Error", "Allocating new ArenaPool");
        return;
    }
    cnstrnts = CERT_DecodeNameConstraintsExtension(pool, value);
    if (!cnstrnts) {
        SECU_PrintErrMsg(out, level, "Error", "Parsing extension");
        SECU_PrintAny(out, value, "Raw", level);
    } else {
        if (cnstrnts->permited)
            secu_PrintNameConstraintSubtree(out, cnstrnts->permited,
                                            "Permitted", level);
        if (cnstrnts->excluded)
            secu_PrintNameConstraintSubtree(out, cnstrnts->excluded,
                                            "Excluded", level);
    }
    PORT_FreeArena(pool, PR_FALSE);
}

static void
secu_PrintAuthorityInfoAcess(FILE *out, SECItem *value, char *msg, int level)
{
    CERTAuthInfoAccess **infos = NULL;
    PLArenaPool *pool = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);

    if (!pool) {
        SECU_PrintError("Error", "Allocating new ArenaPool");
        return;
    }
    infos = CERT_DecodeAuthInfoAccessExtension(pool, value);
    if (!infos) {
        SECU_PrintErrMsg(out, level, "Error", "Parsing extension");
        SECU_PrintAny(out, value, "Raw", level);
    } else {
        CERTAuthInfoAccess *info;
        while (NULL != (info = *infos++)) {
            if (info->method.data) {
                SECU_PrintObjectID(out, &info->method, "Method", level);
            } else {
                SECU_Indent(out, level);
                fprintf(out, "Error: missing method\n");
            }
            if (info->location) {
                secu_PrintGeneralName(out, info->location, "Location", level);
            } else {
                SECU_PrintAny(out, &info->derLocation, "Location", level);
            }
        }
    }
    PORT_FreeArena(pool, PR_FALSE);
}

void
SECU_PrintExtensions(FILE *out, CERTCertExtension **extensions,
                     char *msg, int level)
{
    SECOidTag oidTag;

    if (extensions) {
        if (msg && *msg) {
            SECU_Indent(out, level++);
            fprintf(out, "%s:\n", msg);
        }

        while (*extensions) {
            SECItem *tmpitem;

            tmpitem = &(*extensions)->id;
            SECU_PrintObjectID(out, tmpitem, "Name", level);

            tmpitem = &(*extensions)->critical;
            if (tmpitem->len) {
                secu_PrintBoolean(out, tmpitem, "Critical", level);
            }

            oidTag = SECOID_FindOIDTag(&((*extensions)->id));
            tmpitem = &((*extensions)->value);

            switch (oidTag) {
                case SEC_OID_X509_INVALID_DATE:
                case SEC_OID_NS_CERT_EXT_CERT_RENEWAL_TIME:
                    secu_PrintX509InvalidDate(out, tmpitem, "Date", level);
                    break;
                case SEC_OID_X509_CERTIFICATE_POLICIES:
                    SECU_PrintPolicy(out, tmpitem, "Data", level);
                    break;
                case SEC_OID_NS_CERT_EXT_BASE_URL:
                case SEC_OID_NS_CERT_EXT_REVOCATION_URL:
                case SEC_OID_NS_CERT_EXT_CA_REVOCATION_URL:
                case SEC_OID_NS_CERT_EXT_CA_CRL_URL:
                case SEC_OID_NS_CERT_EXT_CA_CERT_URL:
                case SEC_OID_NS_CERT_EXT_CERT_RENEWAL_URL:
                case SEC_OID_NS_CERT_EXT_CA_POLICY_URL:
                case SEC_OID_NS_CERT_EXT_HOMEPAGE_URL:
                case SEC_OID_NS_CERT_EXT_LOST_PASSWORD_URL:
                case SEC_OID_OCSP_RESPONDER:
                    SECU_PrintString(out, tmpitem, "URL", level);
                    break;
                case SEC_OID_NS_CERT_EXT_COMMENT:
                    SECU_PrintString(out, tmpitem, "Comment", level);
                    break;
                case SEC_OID_NS_CERT_EXT_SSL_SERVER_NAME:
                    SECU_PrintString(out, tmpitem, "ServerName", level);
                    break;
                case SEC_OID_NS_CERT_EXT_CERT_TYPE:
                    secu_PrintNSCertType(out, tmpitem, "Data", level);
                    break;
                case SEC_OID_X509_BASIC_CONSTRAINTS:
                    secu_PrintBasicConstraints(out, tmpitem, "Data", level);
                    break;
                case SEC_OID_X509_EXT_KEY_USAGE:
                    PrintExtKeyUsageExtension(out, tmpitem, NULL, level);
                    break;
                case SEC_OID_X509_KEY_USAGE:
                    secu_PrintX509KeyUsage(out, tmpitem, NULL, level);
                    break;
                case SEC_OID_X509_AUTH_KEY_ID:
                    secu_PrintAuthKeyIDExtension(out, tmpitem, NULL, level);
                    break;
                case SEC_OID_X509_SUBJECT_ALT_NAME:
                case SEC_OID_X509_ISSUER_ALT_NAME:
                    secu_PrintAltNameExtension(out, tmpitem, NULL, level);
                    break;
                case SEC_OID_X509_CRL_DIST_POINTS:
                    secu_PrintCRLDistPtsExtension(out, tmpitem, NULL, level);
                    break;
                case SEC_OID_X509_PRIVATE_KEY_USAGE_PERIOD:
                    SECU_PrintPrivKeyUsagePeriodExtension(out, tmpitem, NULL,
                                                          level);
                    break;
                case SEC_OID_X509_NAME_CONSTRAINTS:
                    secu_PrintNameConstraintsExtension(out, tmpitem, NULL, level);
                    break;
                case SEC_OID_X509_AUTH_INFO_ACCESS:
                    secu_PrintAuthorityInfoAcess(out, tmpitem, NULL, level);
                    break;

                case SEC_OID_X509_CRL_NUMBER:
                case SEC_OID_X509_REASON_CODE:

                /* PKIX OIDs */
                case SEC_OID_PKIX_OCSP:
                case SEC_OID_PKIX_OCSP_BASIC_RESPONSE:
                case SEC_OID_PKIX_OCSP_NONCE:
                case SEC_OID_PKIX_OCSP_CRL:
                case SEC_OID_PKIX_OCSP_RESPONSE:
                case SEC_OID_PKIX_OCSP_NO_CHECK:
                case SEC_OID_PKIX_OCSP_ARCHIVE_CUTOFF:
                case SEC_OID_PKIX_OCSP_SERVICE_LOCATOR:
                case SEC_OID_PKIX_REGCTRL_REGTOKEN:
                case SEC_OID_PKIX_REGCTRL_AUTHENTICATOR:
                case SEC_OID_PKIX_REGCTRL_PKIPUBINFO:
                case SEC_OID_PKIX_REGCTRL_PKI_ARCH_OPTIONS:
                case SEC_OID_PKIX_REGCTRL_OLD_CERT_ID:
                case SEC_OID_PKIX_REGCTRL_PROTOCOL_ENC_KEY:
                case SEC_OID_PKIX_REGINFO_UTF8_PAIRS:
                case SEC_OID_PKIX_REGINFO_CERT_REQUEST:

                /* Netscape extension OIDs. */
                case SEC_OID_NS_CERT_EXT_NETSCAPE_OK:
                case SEC_OID_NS_CERT_EXT_ISSUER_LOGO:
                case SEC_OID_NS_CERT_EXT_SUBJECT_LOGO:
                case SEC_OID_NS_CERT_EXT_ENTITY_LOGO:
                case SEC_OID_NS_CERT_EXT_USER_PICTURE:

                /* x.509 v3 Extensions */
                case SEC_OID_X509_SUBJECT_DIRECTORY_ATTR:
                case SEC_OID_X509_SUBJECT_KEY_ID:
                case SEC_OID_X509_POLICY_MAPPINGS:
                case SEC_OID_X509_POLICY_CONSTRAINTS:

                default:
                    SECU_PrintAny(out, tmpitem, "Data", level);
                    break;
            }

            SECU_Newline(out);
            extensions++;
        }
    }
}

/* An RDN is a subset of a DirectoryName, and we already know how to
 * print those, so make a directory name out of the RDN, and print it.
 */
void
SECU_PrintRDN(FILE *out, CERTRDN *rdn, const char *msg, int level)
{
    CERTName name;
    CERTRDN *rdns[2];

    name.arena = NULL;
    name.rdns = rdns;
    rdns[0] = rdn;
    rdns[1] = NULL;
    SECU_PrintName(out, &name, msg, level);
}

void
SECU_PrintNameQuotesOptional(FILE *out, CERTName *name, const char *msg,
                             int level, PRBool quotes)
{
    char *nameStr = NULL;
    char *str;
    SECItem my;

    if (!name) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return;
    }
    if (!name->rdns || !name->rdns[0]) {
        str = "(empty)";
    } else {
        str = nameStr = CERT_NameToAscii(name);
    }
    if (!str) {
        str = "!Invalid AVA!";
    }
    my.data = (unsigned char *)str;
    my.len = PORT_Strlen(str);
#if 1
    secu_PrintRawStringQuotesOptional(out, &my, msg, level, quotes);
#else
    SECU_Indent(out, level);
    fprintf(out, "%s: ", msg);
    fprintf(out, str);
    SECU_Newline(out);
#endif
    PORT_Free(nameStr);
}

void
SECU_PrintName(FILE *out, CERTName *name, const char *msg, int level)
{
    SECU_PrintNameQuotesOptional(out, name, msg, level, PR_TRUE);
}

void
printflags(char *trusts, unsigned int flags)
{
    if (flags & CERTDB_VALID_CA)
        if (!(flags & CERTDB_TRUSTED_CA) &&
            !(flags & CERTDB_TRUSTED_CLIENT_CA))
            PORT_Strcat(trusts, "c");
    if (flags & CERTDB_TERMINAL_RECORD)
        if (!(flags & CERTDB_TRUSTED))
            PORT_Strcat(trusts, "p");
    if (flags & CERTDB_TRUSTED_CA)
        PORT_Strcat(trusts, "C");
    if (flags & CERTDB_TRUSTED_CLIENT_CA)
        PORT_Strcat(trusts, "T");
    if (flags & CERTDB_TRUSTED)
        PORT_Strcat(trusts, "P");
    if (flags & CERTDB_USER)
        PORT_Strcat(trusts, "u");
    if (flags & CERTDB_SEND_WARN)
        PORT_Strcat(trusts, "w");
    if (flags & CERTDB_INVISIBLE_CA)
        PORT_Strcat(trusts, "I");
    if (flags & CERTDB_GOVT_APPROVED_CA)
        PORT_Strcat(trusts, "G");
    return;
}

/* callback for listing certs through pkcs11 */
SECStatus
SECU_PrintCertNickname(CERTCertListNode *node, void *data)
{
    CERTCertTrust trust;
    CERTCertificate *cert;
    FILE *out;
    char trusts[30];
    char *name;

    cert = node->cert;

    PORT_Memset(trusts, 0, sizeof(trusts));
    out = (FILE *)data;

    name = node->appData;
    if (!name || !name[0]) {
        name = cert->nickname;
    }
    if (!name || !name[0]) {
        name = cert->emailAddr;
    }
    if (!name || !name[0]) {
        name = "(NULL)";
    }

    if (CERT_GetCertTrust(cert, &trust) == SECSuccess) {
        printflags(trusts, trust.sslFlags);
        PORT_Strcat(trusts, ",");
        printflags(trusts, trust.emailFlags);
        PORT_Strcat(trusts, ",");
        printflags(trusts, trust.objectSigningFlags);
    } else {
        PORT_Memcpy(trusts, ",,", 3);
    }
    fprintf(out, "%-60s %-5s\n", name, trusts);

    return (SECSuccess);
}

int
SECU_DecodeAndPrintExtensions(FILE *out, SECItem *any, char *m, int level)
{
    CERTCertExtension **extensions = NULL;
    PLArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    int rv = 0;

    if (!arena)
        return SEC_ERROR_NO_MEMORY;

    rv = SEC_QuickDERDecodeItem(arena, &extensions,
                                SEC_ASN1_GET(CERT_SequenceOfCertExtensionTemplate), any);
    if (!rv)
        SECU_PrintExtensions(out, extensions, m, level);
    else
        SECU_PrintAny(out, any, m, level);
    PORT_FreeArena(arena, PR_FALSE);
    return rv;
}

/* print a decoded SET OF or SEQUENCE OF Extensions */
int
SECU_PrintSetOfExtensions(FILE *out, SECItem **any, char *m, int level)
{
    int rv = 0;
    if (m && *m) {
        SECU_Indent(out, level++);
        fprintf(out, "%s:\n", m);
    }
    while (any && any[0]) {
        rv |= SECU_DecodeAndPrintExtensions(out, any[0], "", level);
        any++;
    }
    return rv;
}

/* print a decoded SET OF or SEQUENCE OF "ANY" */
int
SECU_PrintSetOfAny(FILE *out, SECItem **any, char *m, int level)
{
    int rv = 0;
    if (m && *m) {
        SECU_Indent(out, level++);
        fprintf(out, "%s:\n", m);
    }
    while (any && any[0]) {
        SECU_PrintAny(out, any[0], "", level);
        any++;
    }
    return rv;
}

int
SECU_PrintCertAttribute(FILE *out, CERTAttribute *attr, char *m, int level)
{
    int rv = 0;
    SECOidTag tag;
    tag = SECU_PrintObjectID(out, &attr->attrType, "Attribute Type", level);
    if (tag == SEC_OID_PKCS9_EXTENSION_REQUEST) {
        rv = SECU_PrintSetOfExtensions(out, attr->attrValue, "Extensions", level);
    } else {
        rv = SECU_PrintSetOfAny(out, attr->attrValue, "Attribute Values", level);
    }
    return rv;
}

int
SECU_PrintCertAttributes(FILE *out, CERTAttribute **attrs, char *m, int level)
{
    int rv = 0;
    while (attrs[0]) {
        rv |= SECU_PrintCertAttribute(out, attrs[0], m, level + 1);
        attrs++;
    }
    return rv;
}

/* sometimes a PRErrorCode, other times a SECStatus.  Sigh. */
int
SECU_PrintCertificateRequest(FILE *out, const SECItem *der, const char *m, int level)
{
    PLArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    CERTCertificateRequest *cr;
    int rv = SEC_ERROR_NO_MEMORY;

    if (!arena)
        return rv;

    /* Decode certificate request */
    cr = PORT_ArenaZNew(arena, CERTCertificateRequest);
    if (!cr)
        goto loser;
    cr->arena = arena;
    rv = SEC_QuickDERDecodeItem(arena, cr,
                                SEC_ASN1_GET(CERT_CertificateRequestTemplate), der);
    if (rv)
        goto loser;

    /* Pretty print it out */
    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintInteger(out, &cr->version, "Version", level + 1);
    SECU_PrintName(out, &cr->subject, "Subject", level + 1);
    if (!SECU_GetWrapEnabled()) /*SECU_PrintName didn't add newline*/
        SECU_Newline(out);
    secu_PrintSubjectPublicKeyInfo(out, arena, &cr->subjectPublicKeyInfo,
                                   "Subject Public Key Info", level + 1);
    if (cr->attributes)
        SECU_PrintCertAttributes(out, cr->attributes, "Attributes", level + 1);
    rv = 0;
loser:
    PORT_FreeArena(arena, PR_FALSE);
    return rv;
}

int
SECU_PrintCertificate(FILE *out, const SECItem *der, const char *m, int level)
{
    PLArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    CERTCertificate *c;
    int rv = SEC_ERROR_NO_MEMORY;
    int iv;

    if (!arena)
        return rv;

    /* Decode certificate */
    c = PORT_ArenaZNew(arena, CERTCertificate);
    if (!c)
        goto loser;
    c->arena = arena;
    rv = SEC_ASN1DecodeItem(arena, c,
                            SEC_ASN1_GET(CERT_CertificateTemplate), der);
    if (rv) {
        SECU_Indent(out, level);
        SECU_PrintErrMsg(out, level, "Error", "Parsing extension");
        SECU_PrintAny(out, der, "Raw", level);
        goto loser;
    }
    /* Pretty print it out */
    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    iv = c->version.len ? DER_GetInteger(&c->version) : 0; /* version is optional */
    SECU_Indent(out, level + 1);
    fprintf(out, "%s: %d (0x%x)\n", "Version", iv + 1, iv);

    SECU_PrintInteger(out, &c->serialNumber, "Serial Number", level + 1);
    SECU_PrintAlgorithmID(out, &c->signature, "Signature Algorithm", level + 1);
    SECU_PrintName(out, &c->issuer, "Issuer", level + 1);
    if (!SECU_GetWrapEnabled()) /*SECU_PrintName didn't add newline*/
        SECU_Newline(out);
    secu_PrintValidity(out, &c->validity, "Validity", level + 1);
    SECU_PrintName(out, &c->subject, "Subject", level + 1);
    if (!SECU_GetWrapEnabled()) /*SECU_PrintName didn't add newline*/
        SECU_Newline(out);
    secu_PrintSubjectPublicKeyInfo(out, arena, &c->subjectPublicKeyInfo,
                                   "Subject Public Key Info", level + 1);
    if (c->issuerID.data)
        secu_PrintDecodedBitString(out, &c->issuerID, "Issuer Unique ID", level + 1);
    if (c->subjectID.data)
        secu_PrintDecodedBitString(out, &c->subjectID, "Subject Unique ID", level + 1);
    SECU_PrintExtensions(out, c->extensions, "Signed Extensions", level + 1);
loser:
    PORT_FreeArena(arena, PR_FALSE);
    return rv;
}

int
SECU_PrintCertificateBasicInfo(FILE *out, const SECItem *der, const char *m, int level)
{
    PLArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    CERTCertificate *c;
    int rv = SEC_ERROR_NO_MEMORY;

    if (!arena)
        return rv;

    /* Decode certificate */
    c = PORT_ArenaZNew(arena, CERTCertificate);
    if (!c)
        goto loser;
    c->arena = arena;
    rv = SEC_ASN1DecodeItem(arena, c,
                            SEC_ASN1_GET(CERT_CertificateTemplate), der);
    if (rv) {
        SECU_Indent(out, level);
        SECU_PrintErrMsg(out, level, "Error", "Parsing extension");
        SECU_PrintAny(out, der, "Raw", level);
        goto loser;
    }
    /* Pretty print it out */
    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintInteger(out, &c->serialNumber, "Serial Number", level + 1);
    SECU_PrintAlgorithmID(out, &c->signature, "Signature Algorithm", level + 1);
    SECU_PrintName(out, &c->issuer, "Issuer", level + 1);
    if (!SECU_GetWrapEnabled()) /*SECU_PrintName didn't add newline*/
        SECU_Newline(out);
    secu_PrintValidity(out, &c->validity, "Validity", level + 1);
    SECU_PrintName(out, &c->subject, "Subject", level + 1);
    if (!SECU_GetWrapEnabled()) /*SECU_PrintName didn't add newline*/
        SECU_Newline(out);
loser:
    PORT_FreeArena(arena, PR_FALSE);
    return rv;
}

int
SECU_PrintSubjectPublicKeyInfo(FILE *out, SECItem *der, char *m, int level)
{
    PLArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    int rv = SEC_ERROR_NO_MEMORY;
    CERTSubjectPublicKeyInfo spki;

    if (!arena)
        return rv;

    PORT_Memset(&spki, 0, sizeof spki);
    rv = SEC_ASN1DecodeItem(arena, &spki,
                            SEC_ASN1_GET(CERT_SubjectPublicKeyInfoTemplate),
                            der);
    if (!rv) {
        if (m && *m) {
            SECU_Indent(out, level);
            fprintf(out, "%s:\n", m);
        }
        secu_PrintSubjectPublicKeyInfo(out, arena, &spki,
                                       "Subject Public Key Info", level + 1);
    }

    PORT_FreeArena(arena, PR_FALSE);
    return rv;
}

int
SECU_PrintPrivateKey(FILE *out, SECItem *der, char *m, int level)
{
    PLArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    SECKEYEncryptedPrivateKeyInfo key;
    int rv = SEC_ERROR_NO_MEMORY;

    if (!arena)
        return rv;

    PORT_Memset(&key, 0, sizeof(key));
    rv = SEC_ASN1DecodeItem(arena, &key,
                            SEC_ASN1_GET(SECKEY_EncryptedPrivateKeyInfoTemplate), der);
    if (rv)
        goto loser;

    /* Pretty print it out */
    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintAlgorithmID(out, &key.algorithm, "Encryption Algorithm",
                          level + 1);
    SECU_PrintAsHex(out, &key.encryptedData, "Encrypted Data", level + 1);
loser:
    PORT_FreeArena(arena, PR_TRUE);
    return rv;
}

int
SECU_PrintFingerprints(FILE *out, SECItem *derCert, char *m, int level)
{
    unsigned char fingerprint[SHA256_LENGTH];
    char *fpStr = NULL;
    int err = PORT_GetError();
    SECStatus rv;
    SECItem fpItem;

    /* Print SHA-256 fingerprint */
    memset(fingerprint, 0, sizeof fingerprint);
    rv = PK11_HashBuf(SEC_OID_SHA256, fingerprint, derCert->data, derCert->len);
    fpItem.data = fingerprint;
    fpItem.len = SHA256_LENGTH;
    fpStr = CERT_Hexify(&fpItem, 1);
    SECU_Indent(out, level);
    fprintf(out, "%s (SHA-256):", m);
    if (SECU_GetWrapEnabled()) {
        fprintf(out, "\n");
        SECU_Indent(out, level + 1);
    } else {
        fprintf(out, " ");
    }
    fprintf(out, "%s\n", fpStr);
    PORT_Free(fpStr);
    fpStr = NULL;
    if (rv != SECSuccess && !err)
        err = PORT_GetError();

    /* print SHA1 fingerprint */
    memset(fingerprint, 0, sizeof fingerprint);
    rv = PK11_HashBuf(SEC_OID_SHA1, fingerprint, derCert->data, derCert->len);
    fpItem.data = fingerprint;
    fpItem.len = SHA1_LENGTH;
    fpStr = CERT_Hexify(&fpItem, 1);
    SECU_Indent(out, level);
    fprintf(out, "%s (SHA1):", m);
    if (SECU_GetWrapEnabled()) {
        fprintf(out, "\n");
        SECU_Indent(out, level + 1);
    } else {
        fprintf(out, " ");
    }
    fprintf(out, "%s\n", fpStr);
    PORT_Free(fpStr);
    if (SECU_GetWrapEnabled())
        fprintf(out, "\n");

    if (err)
        PORT_SetError(err);
    if (err || rv != SECSuccess)
        return SECFailure;

    return 0;
}

/*
** PKCS7 Support
*/

/* forward declaration */
typedef enum {
    secuPKCS7Unknown = 0,
    secuPKCS7PKCS12AuthSafe,
    secuPKCS7PKCS12Safe
} secuPKCS7State;

static int
secu_PrintPKCS7ContentInfo(FILE *, SEC_PKCS7ContentInfo *, secuPKCS7State,
                           const char *, int);
static int
secu_PrintDERPKCS7ContentInfo(FILE *, SECItem *, secuPKCS7State,
                              const char *, int);

/*
** secu_PrintPKCS7EncContent
**   Prints a SEC_PKCS7EncryptedContentInfo (without decrypting it)
*/
static int
secu_PrintPKCS7EncContent(FILE *out, SEC_PKCS7EncryptedContentInfo *src,
                          secuPKCS7State state, const char *m, int level)
{
    if (src->contentTypeTag == NULL)
        src->contentTypeTag = SECOID_FindOID(&(src->contentType));

    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_Indent(out, level + 1);
    fprintf(out, "Content Type: %s\n",
            (src->contentTypeTag != NULL) ? src->contentTypeTag->desc
                                          : "Unknown");
    SECU_PrintAlgorithmID(out, &(src->contentEncAlg),
                          "Content Encryption Algorithm", level + 1);
    SECU_PrintAsHex(out, &(src->encContent),
                    "Encrypted Content", level + 1);
    return 0;
}

/*
** secu_PrintRecipientInfo
**   Prints a PKCS7RecipientInfo type
*/
static void
secu_PrintRecipientInfo(FILE *out, SEC_PKCS7RecipientInfo *info,
                        const char *m, int level)
{
    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintInteger(out, &(info->version), "Version", level + 1);

    SECU_PrintName(out, &(info->issuerAndSN->issuer), "Issuer",
                   level + 1);
    SECU_PrintInteger(out, &(info->issuerAndSN->serialNumber),
                      "Serial Number", level + 1);

    /* Parse and display encrypted key */
    SECU_PrintAlgorithmID(out, &(info->keyEncAlg),
                          "Key Encryption Algorithm", level + 1);
    SECU_PrintAsHex(out, &(info->encKey), "Encrypted Key", level + 1);
}

/*
** secu_PrintSignerInfo
**   Prints a PKCS7SingerInfo type
*/
static void
secu_PrintSignerInfo(FILE *out, SEC_PKCS7SignerInfo *info,
                     const char *m, int level)
{
    SEC_PKCS7Attribute *attr;
    int iv;
    char om[100];

    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintInteger(out, &(info->version), "Version", level + 1);

    SECU_PrintName(out, &(info->issuerAndSN->issuer), "Issuer",
                   level + 1);
    SECU_PrintInteger(out, &(info->issuerAndSN->serialNumber),
                      "Serial Number", level + 1);

    SECU_PrintAlgorithmID(out, &(info->digestAlg), "Digest Algorithm",
                          level + 1);

    if (info->authAttr != NULL) {
        SECU_Indent(out, level + 1);
        fprintf(out, "Authenticated Attributes:\n");
        iv = 0;
        while ((attr = info->authAttr[iv++]) != NULL) {
            snprintf(om, sizeof(om), "Attribute (%d)", iv);
            secu_PrintAttribute(out, attr, om, level + 2);
        }
    }

    /* Parse and display signature */
    SECU_PrintAlgorithmID(out, &(info->digestEncAlg),
                          "Digest Encryption Algorithm", level + 1);
    SECU_PrintAsHex(out, &(info->encDigest), "Encrypted Digest", level + 1);

    if (info->unAuthAttr != NULL) {
        SECU_Indent(out, level + 1);
        fprintf(out, "Unauthenticated Attributes:\n");
        iv = 0;
        while ((attr = info->unAuthAttr[iv++]) != NULL) {
            snprintf(om, sizeof(om), "Attribute (%x)", iv);
            secu_PrintAttribute(out, attr, om, level + 2);
        }
    }
}

/* callers of this function must make sure that the CERTSignedCrl
   from which they are extracting the CERTCrl has been fully-decoded.
   Otherwise it will not have the entries even though the CRL may have
   some */

void
SECU_PrintCRLInfo(FILE *out, CERTCrl *crl, const char *m, int level)
{
    CERTCrlEntry *entry;
    int iv;
    char om[100];

    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    /* version is optional */
    iv = crl->version.len ? DER_GetInteger(&crl->version) : 0;
    SECU_Indent(out, level + 1);
    fprintf(out, "%s: %d (0x%x)\n", "Version", iv + 1, iv);
    SECU_PrintAlgorithmID(out, &(crl->signatureAlg), "Signature Algorithm",
                          level + 1);
    SECU_PrintName(out, &(crl->name), "Issuer", level + 1);
    SECU_PrintTimeChoice(out, &(crl->lastUpdate), "This Update", level + 1);
    if (crl->nextUpdate.data && crl->nextUpdate.len) /* is optional */
        SECU_PrintTimeChoice(out, &(crl->nextUpdate), "Next Update", level + 1);

    if (crl->entries != NULL) {
        iv = 0;
        while ((entry = crl->entries[iv++]) != NULL) {
            snprintf(om, sizeof(om), "Entry %d (0x%x):\n", iv, iv);
            SECU_Indent(out, level + 1);
            fputs(om, out);
            SECU_PrintInteger(out, &(entry->serialNumber), "Serial Number",
                              level + 2);
            SECU_PrintTimeChoice(out, &(entry->revocationDate),
                                 "Revocation Date", level + 2);
            SECU_PrintExtensions(out, entry->extensions,
                                 "Entry Extensions", level + 2);
        }
    }
    SECU_PrintExtensions(out, crl->extensions, "CRL Extensions", level + 1);
}

/*
** secu_PrintPKCS7Signed
**   Pretty print a PKCS7 signed data type (up to version 1).
*/
static int
secu_PrintPKCS7Signed(FILE *out, SEC_PKCS7SignedData *src,
                      secuPKCS7State state, const char *m, int level)
{
    SECAlgorithmID *digAlg;       /* digest algorithms */
    SECItem *aCert;               /* certificate */
    CERTSignedCrl *aCrl;          /* certificate revocation list */
    SEC_PKCS7SignerInfo *sigInfo; /* signer information */
    int rv, iv;
    char om[100];

    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintInteger(out, &(src->version), "Version", level + 1);

    /* Parse and list digest algorithms (if any) */
    if (src->digestAlgorithms != NULL) {
        SECU_Indent(out, level + 1);
        fprintf(out, "Digest Algorithm List:\n");
        iv = 0;
        while ((digAlg = src->digestAlgorithms[iv++]) != NULL) {
            snprintf(om, sizeof(om), "Digest Algorithm (%x)", iv);
            SECU_PrintAlgorithmID(out, digAlg, om, level + 2);
        }
    }

    /* Now for the content */
    rv = secu_PrintPKCS7ContentInfo(out, &(src->contentInfo),
                                    state, "Content Information", level + 1);
    if (rv != 0)
        return rv;

    /* Parse and list certificates (if any) */
    if (src->rawCerts != NULL) {
        SECU_Indent(out, level + 1);
        fprintf(out, "Certificate List:\n");
        iv = 0;
        while ((aCert = src->rawCerts[iv++]) != NULL) {
            snprintf(om, sizeof(om), "Certificate (%x)", iv);
            rv = SECU_PrintSignedData(out, aCert, om, level + 2,
                                      SECU_PrintCertificate);
            if (rv)
                return rv;
        }
    }

    /* Parse and list CRL's (if any) */
    if (src->crls != NULL) {
        SECU_Indent(out, level + 1);
        fprintf(out, "Signed Revocation Lists:\n");
        iv = 0;
        while ((aCrl = src->crls[iv++]) != NULL) {
            snprintf(om, sizeof(om), "Signed Revocation List (%x)", iv);
            SECU_Indent(out, level + 2);
            fprintf(out, "%s:\n", om);
            SECU_PrintAlgorithmID(out, &aCrl->signatureWrap.signatureAlgorithm,
                                  "Signature Algorithm", level + 3);
            DER_ConvertBitString(&aCrl->signatureWrap.signature);
            SECU_PrintAsHex(out, &aCrl->signatureWrap.signature, "Signature",
                            level + 3);
            SECU_PrintCRLInfo(out, &aCrl->crl, "Certificate Revocation List",
                              level + 3);
        }
    }

    /* Parse and list signatures (if any) */
    if (src->signerInfos != NULL) {
        SECU_Indent(out, level + 1);
        fprintf(out, "Signer Information List:\n");
        iv = 0;
        while ((sigInfo = src->signerInfos[iv++]) != NULL) {
            snprintf(om, sizeof(om), "Signer Information (%x)", iv);
            secu_PrintSignerInfo(out, sigInfo, om, level + 2);
        }
    }

    return 0;
}

/*
** secu_PrintPKCS7Enveloped
**  Pretty print a PKCS7 enveloped data type (up to version 1).
*/
static int
secu_PrintPKCS7Enveloped(FILE *out, SEC_PKCS7EnvelopedData *src,
                         secuPKCS7State state, const char *m, int level)
{
    SEC_PKCS7RecipientInfo *recInfo; /* pointer for signer information */
    int iv;
    char om[100];

    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintInteger(out, &(src->version), "Version", level + 1);

    /* Parse and list recipients (this is not optional) */
    if (src->recipientInfos != NULL) {
        SECU_Indent(out, level + 1);
        fprintf(out, "Recipient Information List:\n");
        iv = 0;
        while ((recInfo = src->recipientInfos[iv++]) != NULL) {
            snprintf(om, sizeof(om), "Recipient Information (%x)", iv);
            secu_PrintRecipientInfo(out, recInfo, om, level + 2);
        }
    }

    return secu_PrintPKCS7EncContent(out, &src->encContentInfo, state,
                                     "Encrypted Content Information", level + 1);
}

/*
** secu_PrintPKCS7SignedEnveloped
**   Pretty print a PKCS7 singed and enveloped data type (up to version 1).
*/
static int
secu_PrintPKCS7SignedAndEnveloped(FILE *out,
                                  SEC_PKCS7SignedAndEnvelopedData *src,
                                  secuPKCS7State state, const char *m,
                                  int level)
{
    SECAlgorithmID *digAlg;          /* pointer for digest algorithms */
    SECItem *aCert;                  /* pointer for certificate */
    CERTSignedCrl *aCrl;             /* pointer for certificate revocation list */
    SEC_PKCS7SignerInfo *sigInfo;    /* pointer for signer information */
    SEC_PKCS7RecipientInfo *recInfo; /* pointer for recipient information */
    int rv, iv;
    char om[100];

    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintInteger(out, &(src->version), "Version", level + 1);

    /* Parse and list recipients (this is not optional) */
    if (src->recipientInfos != NULL) {
        SECU_Indent(out, level + 1);
        fprintf(out, "Recipient Information List:\n");
        iv = 0;
        while ((recInfo = src->recipientInfos[iv++]) != NULL) {
            snprintf(om, sizeof(om), "Recipient Information (%x)", iv);
            secu_PrintRecipientInfo(out, recInfo, om, level + 2);
        }
    }

    /* Parse and list digest algorithms (if any) */
    if (src->digestAlgorithms != NULL) {
        SECU_Indent(out, level + 1);
        fprintf(out, "Digest Algorithm List:\n");
        iv = 0;
        while ((digAlg = src->digestAlgorithms[iv++]) != NULL) {
            snprintf(om, sizeof(om), "Digest Algorithm (%x)", iv);
            SECU_PrintAlgorithmID(out, digAlg, om, level + 2);
        }
    }

    rv = secu_PrintPKCS7EncContent(out, &src->encContentInfo, state,
                                   "Encrypted Content Information", level + 1);
    if (rv)
        return rv;

    /* Parse and list certificates (if any) */
    if (src->rawCerts != NULL) {
        SECU_Indent(out, level + 1);
        fprintf(out, "Certificate List:\n");
        iv = 0;
        while ((aCert = src->rawCerts[iv++]) != NULL) {
            snprintf(om, sizeof(om), "Certificate (%x)", iv);
            rv = SECU_PrintSignedData(out, aCert, om, level + 2,
                                      SECU_PrintCertificate);
            if (rv)
                return rv;
        }
    }

    /* Parse and list CRL's (if any) */
    if (src->crls != NULL) {
        SECU_Indent(out, level + 1);
        fprintf(out, "Signed Revocation Lists:\n");
        iv = 0;
        while ((aCrl = src->crls[iv++]) != NULL) {
            snprintf(om, sizeof(om), "Signed Revocation List (%x)", iv);
            SECU_Indent(out, level + 2);
            fprintf(out, "%s:\n", om);
            SECU_PrintAlgorithmID(out, &aCrl->signatureWrap.signatureAlgorithm,
                                  "Signature Algorithm", level + 3);
            DER_ConvertBitString(&aCrl->signatureWrap.signature);
            SECU_PrintAsHex(out, &aCrl->signatureWrap.signature, "Signature",
                            level + 3);
            SECU_PrintCRLInfo(out, &aCrl->crl, "Certificate Revocation List",
                              level + 3);
        }
    }

    /* Parse and list signatures (if any) */
    if (src->signerInfos != NULL) {
        SECU_Indent(out, level + 1);
        fprintf(out, "Signer Information List:\n");
        iv = 0;
        while ((sigInfo = src->signerInfos[iv++]) != NULL) {
            snprintf(om, sizeof(om), "Signer Information (%x)", iv);
            secu_PrintSignerInfo(out, sigInfo, om, level + 2);
        }
    }

    return 0;
}

int
SECU_PrintCrl(FILE *out, const SECItem *der, const char *m, int level)
{
    PLArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    CERTCrl *c = NULL;
    int rv = SEC_ERROR_NO_MEMORY;

    if (!arena)
        return rv;
    do {
        /* Decode CRL */
        c = PORT_ArenaZNew(arena, CERTCrl);
        if (!c)
            break;

        rv = SEC_QuickDERDecodeItem(arena, c, SEC_ASN1_GET(CERT_CrlTemplate), der);
        if (rv != SECSuccess)
            break;
        SECU_PrintCRLInfo(out, c, m, level);
    } while (0);
    PORT_FreeArena(arena, PR_FALSE);
    return rv;
}

/*
** secu_PrintPKCS7Encrypted
**   Pretty print a PKCS7 encrypted data type (up to version 1).
*/
static int
secu_PrintPKCS7Encrypted(FILE *out, SEC_PKCS7EncryptedData *src,
                         secuPKCS7State state, const char *m, int level)
{
    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintInteger(out, &(src->version), "Version", level + 1);

    return secu_PrintPKCS7EncContent(out, &src->encContentInfo, state,
                                     "Encrypted Content Information", level + 1);
}

/*
** secu_PrintPKCS7Digested
**   Pretty print a PKCS7 digested data type (up to version 1).
*/
static int
secu_PrintPKCS7Digested(FILE *out, SEC_PKCS7DigestedData *src,
                        secuPKCS7State state, const char *m, int level)
{
    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_PrintInteger(out, &(src->version), "Version", level + 1);

    SECU_PrintAlgorithmID(out, &src->digestAlg, "Digest Algorithm",
                          level + 1);
    secu_PrintPKCS7ContentInfo(out, &src->contentInfo, state,
                               "Content Information", level + 1);
    SECU_PrintAsHex(out, &src->digest, "Digest", level + 1);
    return 0;
}

static int
secu_PrintPKCS12Attributes(FILE *out, SECItem *item, const char *m, int level)
{
    SECItem my = *item;
    SECItem attribute;
    SECItem attributeID;
    SECItem attributeValues;

    if ((my.data[0] != (SEC_ASN1_CONSTRUCTED | SEC_ASN1_SET)) ||
        SECSuccess != SECU_StripTagAndLength(&my)) {
        PORT_SetError(SEC_ERROR_BAD_DER);
        return SECFailure;
    }
    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    level++;

    while (my.len) {
        if (SECSuccess != SECU_ExtractBERAndStep(&my, &attribute)) {
            return SECFailure;
        }
        if ((attribute.data[0] != (SEC_ASN1_CONSTRUCTED | SEC_ASN1_SEQUENCE)) ||
            SECSuccess != SECU_StripTagAndLength(&attribute)) {
            PORT_SetError(SEC_ERROR_BAD_DER);
            return SECFailure;
        }

        /* attribute ID */
        if (SECSuccess != SECU_ExtractBERAndStep(&attribute, &attributeID)) {
            return SECFailure;
        }
        if ((attributeID.data[0] & SEC_ASN1_TAGNUM_MASK) != SEC_ASN1_OBJECT_ID) {
            PORT_SetError(SEC_ERROR_BAD_DER);
            return SECFailure;
        }
        SECU_PrintEncodedObjectID(out, &attributeID, "Attribute ID", level);

        /* attribute values */
        if (!attribute.len) { /* skip if there aren't any */
            continue;
        }
        if (SECSuccess != SECU_ExtractBERAndStep(&attribute, &attributeValues)) {
            return SECFailure;
        }
        if (SECSuccess != SECU_StripTagAndLength(&attributeValues)) {
            return SECFailure;
        }
        while (attributeValues.len) {
            SECItem tmp;
            if (SECSuccess != SECU_ExtractBERAndStep(&attributeValues, &tmp)) {
                return SECFailure;
            }
            SECU_PrintAny(out, &tmp, NULL, level + 1);
        }
    }
    return SECSuccess;
}

static int
secu_PrintPKCS12Bag(FILE *out, SECItem *item, const char *desc, int level)
{
    SECItem my = *item;
    SECItem bagID;
    SECItem bagValue;
    SECItem bagAttributes;
    SECOidTag bagTag;
    SECStatus rv;
    int i;
    char *m;

    if ((my.data[0] != (SEC_ASN1_CONSTRUCTED | SEC_ASN1_SEQUENCE)) ||
        SECSuccess != SECU_StripTagAndLength(&my)) {
        PORT_SetError(SEC_ERROR_BAD_DER);
        return SECFailure;
    }

    /* bagId BAG-TYPE.&id ({PKCS12BagSet}) */
    if (SECSuccess != SECU_ExtractBERAndStep(&my, &bagID)) {
        return SECFailure;
    }
    if ((bagID.data[0] & SEC_ASN1_TAGNUM_MASK) != SEC_ASN1_OBJECT_ID) {
        PORT_SetError(SEC_ERROR_BAD_DER);
        return SECFailure;
    }
    m = PR_smprintf("%s ID", desc);
    bagTag = SECU_PrintEncodedObjectID(out, &bagID, m ? m : "Bag ID", level);
    if (m)
        PR_smprintf_free(m);

    /* bagValue [0] EXPLICIT BAG-TYPE.&type({PKCS12BagSet}{@bagID}) */
    if (SECSuccess != SECU_ExtractBERAndStep(&my, &bagValue)) {
        return SECFailure;
    }
    if ((bagValue.data[0] & (SEC_ASN1_CLASS_MASK | SEC_ASN1_TAGNUM_MASK)) !=
        (SEC_ASN1_CONTEXT_SPECIFIC | 0)) {
        PORT_SetError(SEC_ERROR_BAD_DER);
        return SECFailure;
    }
    if (SECSuccess != SECU_StripTagAndLength(&bagValue)) {
        return SECFailure;
    }

    rv = SECSuccess;
    switch (bagTag) {
        case SEC_OID_PKCS12_V1_KEY_BAG_ID:
            /* Future we need to print out raw private keys. Not a priority since
             * p12util can't create files with unencrypted private keys, but
             * some tools can and do */
            SECU_PrintAny(out, &bagValue, "Private Key", level);
            break;
        case SEC_OID_PKCS12_V1_PKCS8_SHROUDED_KEY_BAG_ID:
            rv = SECU_PrintPrivateKey(out, &bagValue,
                                      "Encrypted Private Key", level);
            break;
        case SEC_OID_PKCS12_V1_CERT_BAG_ID:
            rv = secu_PrintPKCS12Bag(out, &bagValue, "Certificate Bag", level + 1);
            break;
        case SEC_OID_PKCS12_V1_CRL_BAG_ID:
            rv = secu_PrintPKCS12Bag(out, &bagValue, "Crl Bag", level + 1);
            break;
        case SEC_OID_PKCS12_V1_SECRET_BAG_ID:
            rv = secu_PrintPKCS12Bag(out, &bagValue, "Secret Bag", level + 1);
            break;
        /* from recursive call from CRL and certificate Bag */
        case SEC_OID_PKCS9_X509_CRL:
        case SEC_OID_PKCS9_X509_CERT:
        case SEC_OID_PKCS9_SDSI_CERT:
            /* unwrap the octect string */
            rv = SECU_StripTagAndLength(&bagValue);
            if (rv != SECSuccess) {
                break;
            }
        /* fall through */
        case SEC_OID_PKCS12_CERT_AND_CRL_BAG_ID:
        case SEC_OID_PKCS12_X509_CERT_CRL_BAG:
        case SEC_OID_PKCS12_SDSI_CERT_BAG:
            if (strcmp(desc, "Crl Bag") == 0) {
                rv = SECU_PrintSignedData(out, &bagValue, NULL, level + 1,
                                          SECU_PrintCrl);
            } else {
                rv = SECU_PrintSignedData(out, &bagValue, NULL, level + 1,
                                          SECU_PrintCertificate);
            }
            break;
        case SEC_OID_PKCS12_V1_SAFE_CONTENTS_BAG_ID:
            for (i = 1; my.len; i++) {
                SECItem nextBag;
                rv = SECU_ExtractBERAndStep(&bagValue, &nextBag);
                if (rv != SECSuccess) {
                    break;
                }
                m = PR_smprintf("Nested Bag %d", i);
                rv = secu_PrintPKCS12Bag(out, &nextBag,
                                         m ? m : "Nested Bag", level + 1);
                if (m)
                    PR_smprintf_free(m);
                if (rv != SECSuccess) {
                    break;
                }
            }
            break;
        default:
            m = PR_smprintf("%s Value", desc);
            SECU_PrintAny(out, &bagValue, m ? m : "Bag Value", level);
            if (m)
                PR_smprintf_free(m);
    }
    if (rv != SECSuccess) {
        return rv;
    }

    /* bagAttributes SET OF PKCS12Attributes OPTIONAL */
    if (my.len &&
        (my.data[0] == (SEC_ASN1_CONSTRUCTED | SEC_ASN1_SET))) {
        if (SECSuccess != SECU_ExtractBERAndStep(&my, &bagAttributes)) {
            return SECFailure;
        }
        m = PR_smprintf("%s Attributes", desc);
        rv = secu_PrintPKCS12Attributes(out, &bagAttributes,
                                        m ? m : "Bag Attributes", level);
        if (m)
            PR_smprintf_free(m);
    }
    return rv;
}

static int
secu_PrintPKCS7Data(FILE *out, SECItem *item, secuPKCS7State state,
                    const char *desc, int level)
{
    SECItem my = *item;
    SECItem nextbag;
    int i;
    SECStatus rv;

    /* walk down each safe */
    switch (state) {
        case secuPKCS7PKCS12AuthSafe:
            if ((my.data[0] != (SEC_ASN1_CONSTRUCTED | SEC_ASN1_SEQUENCE)) ||
                SECSuccess != SECU_StripTagAndLength(&my)) {
                PORT_SetError(SEC_ERROR_BAD_DER);
                return SECFailure;
            }
            for (i = 1; my.len; i++) {
                char *m;
                if (SECSuccess != SECU_ExtractBERAndStep(&my, &nextbag)) {
                    return SECFailure;
                }
                m = PR_smprintf("Safe %d", i);
                rv = secu_PrintDERPKCS7ContentInfo(out, &nextbag,
                                                   secuPKCS7PKCS12Safe,
                                                   m ? m : "Safe", level);
                if (m)
                    PR_smprintf_free(m);
                if (rv != SECSuccess) {
                    return SECFailure;
                }
            }
            return SECSuccess;
        case secuPKCS7PKCS12Safe:
            if ((my.data[0] != (SEC_ASN1_CONSTRUCTED | SEC_ASN1_SEQUENCE)) ||
                SECSuccess != SECU_StripTagAndLength(&my)) {
                PORT_SetError(SEC_ERROR_BAD_DER);
                return SECFailure;
            }
            for (i = 1; my.len; i++) {
                char *m;
                if (SECSuccess != SECU_ExtractBERAndStep(&my, &nextbag)) {
                    return SECFailure;
                }
                m = PR_smprintf("Bag %d", i);
                rv = secu_PrintPKCS12Bag(out, &nextbag,
                                         m ? m : "Bag", level);
                if (m)
                    PR_smprintf_free(m);
                if (rv != SECSuccess) {
                    return SECFailure;
                }
            }
            return SECSuccess;
        case secuPKCS7Unknown:
            SECU_PrintAsHex(out, item, desc, level);
            break;
    }
    return SECSuccess;
}

/*
** secu_PrintPKCS7ContentInfo
**   Takes a SEC_PKCS7ContentInfo type and sends the contents to the
** appropriate function
*/
static int
secu_PrintPKCS7ContentInfo(FILE *out, SEC_PKCS7ContentInfo *src,
                           secuPKCS7State state, const char *m, int level)
{
    const char *desc;
    SECOidTag kind;
    int rv;

    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    level++;

    if (src->contentTypeTag == NULL)
        src->contentTypeTag = SECOID_FindOID(&(src->contentType));

    if (src->contentTypeTag == NULL) {
        desc = "Unknown";
        kind = SEC_OID_UNKNOWN;
    } else {
        desc = src->contentTypeTag->desc;
        kind = src->contentTypeTag->offset;
    }

    if (src->content.data == NULL) {
        SECU_Indent(out, level);
        fprintf(out, "%s:\n", desc);
        level++;
        SECU_Indent(out, level);
        fprintf(out, "<no content>\n");
        return 0;
    }

    rv = 0;
    switch (kind) {
        case SEC_OID_PKCS7_SIGNED_DATA: /* Signed Data */
            rv = secu_PrintPKCS7Signed(out, src->content.signedData,
                                       state, desc, level);
            break;

        case SEC_OID_PKCS7_ENVELOPED_DATA: /* Enveloped Data */
            rv = secu_PrintPKCS7Enveloped(out, src->content.envelopedData,
                                          state, desc, level);
            break;

        case SEC_OID_PKCS7_SIGNED_ENVELOPED_DATA: /* Signed and Enveloped */
            rv = secu_PrintPKCS7SignedAndEnveloped(out,
                                                   src->content.signedAndEnvelopedData,
                                                   state, desc, level);
            break;

        case SEC_OID_PKCS7_DIGESTED_DATA: /* Digested Data */
            rv = secu_PrintPKCS7Digested(out, src->content.digestedData,
                                         state, desc, level);
            break;

        case SEC_OID_PKCS7_ENCRYPTED_DATA: /* Encrypted Data */
            rv = secu_PrintPKCS7Encrypted(out, src->content.encryptedData,
                                          state, desc, level);
            break;

        case SEC_OID_PKCS7_DATA:
            rv = secu_PrintPKCS7Data(out, src->content.data, state, desc, level);
            break;

        default:
            SECU_PrintAsHex(out, src->content.data, desc, level);
            break;
    }

    return rv;
}

/*
** SECU_PrintPKCS7ContentInfo
**   Decode and print any major PKCS7 data type (up to version 1).
*/
static int
secu_PrintDERPKCS7ContentInfo(FILE *out, SECItem *der, secuPKCS7State state,
                              const char *m, int level)
{
    SEC_PKCS7ContentInfo *cinfo;
    int rv;

    cinfo = SEC_PKCS7DecodeItem(der, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (cinfo != NULL) {
        /* Send it to recursive parsing and printing module */
        rv = secu_PrintPKCS7ContentInfo(out, cinfo, state, m, level);
        SEC_PKCS7DestroyContentInfo(cinfo);
    } else {
        rv = -1;
    }

    return rv;
}

int
SECU_PrintPKCS7ContentInfo(FILE *out, SECItem *der, char *m, int level)
{
    return secu_PrintDERPKCS7ContentInfo(out, der, secuPKCS7Unknown, m, level);
}

/*
** End of PKCS7 functions
*/

void
printFlags(FILE *out, unsigned int flags, int level)
{
    if (flags & CERTDB_TERMINAL_RECORD) {
        SECU_Indent(out, level);
        fprintf(out, "Terminal Record\n");
    }
    if (flags & CERTDB_TRUSTED) {
        SECU_Indent(out, level);
        fprintf(out, "Trusted\n");
    }
    if (flags & CERTDB_SEND_WARN) {
        SECU_Indent(out, level);
        fprintf(out, "Warn When Sending\n");
    }
    if (flags & CERTDB_VALID_CA) {
        SECU_Indent(out, level);
        fprintf(out, "Valid CA\n");
    }
    if (flags & CERTDB_TRUSTED_CA) {
        SECU_Indent(out, level);
        fprintf(out, "Trusted CA\n");
    }
    if (flags & CERTDB_NS_TRUSTED_CA) {
        SECU_Indent(out, level);
        fprintf(out, "Netscape Trusted CA\n");
    }
    if (flags & CERTDB_USER) {
        SECU_Indent(out, level);
        fprintf(out, "User\n");
    }
    if (flags & CERTDB_TRUSTED_CLIENT_CA) {
        SECU_Indent(out, level);
        fprintf(out, "Trusted Client CA\n");
    }
    if (flags & CERTDB_GOVT_APPROVED_CA) {
        SECU_Indent(out, level);
        fprintf(out, "Step-up\n");
    }
}

void
SECU_PrintTrustFlags(FILE *out, CERTCertTrust *trust, char *m, int level)
{
    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    SECU_Indent(out, level + 1);
    fprintf(out, "SSL Flags:\n");
    printFlags(out, trust->sslFlags, level + 2);
    SECU_Indent(out, level + 1);
    fprintf(out, "Email Flags:\n");
    printFlags(out, trust->emailFlags, level + 2);
    SECU_Indent(out, level + 1);
    fprintf(out, "Object Signing Flags:\n");
    printFlags(out, trust->objectSigningFlags, level + 2);
}

int
SECU_PrintDERName(FILE *out, SECItem *der, const char *m, int level)
{
    PLArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    CERTName *name;
    int rv = SEC_ERROR_NO_MEMORY;

    if (!arena)
        return rv;

    name = PORT_ArenaZNew(arena, CERTName);
    if (!name)
        goto loser;

    rv = SEC_ASN1DecodeItem(arena, name, SEC_ASN1_GET(CERT_NameTemplate), der);
    if (rv)
        goto loser;

    SECU_PrintName(out, name, m, level);
    if (!SECU_GetWrapEnabled()) /*SECU_PrintName didn't add newline*/
        SECU_Newline(out);
loser:
    PORT_FreeArena(arena, PR_FALSE);
    return rv;
}

typedef enum {
    noSignature = 0,
    withSignature = 1
} SignatureOptionType;

static int
secu_PrintSignedDataSigOpt(FILE *out, SECItem *der, const char *m,
                           int level, SECU_PPFunc inner,
                           SignatureOptionType signatureOption)
{
    PLArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    CERTSignedData *sd;
    int rv = SEC_ERROR_NO_MEMORY;

    if (!arena)
        return rv;

    /* Strip off the signature */
    sd = PORT_ArenaZNew(arena, CERTSignedData);
    if (!sd)
        goto loser;

    rv = SEC_ASN1DecodeItem(arena, sd, SEC_ASN1_GET(CERT_SignedDataTemplate),
                            der);
    if (rv)
        goto loser;

    if (m) {
        SECU_Indent(out, level);
        fprintf(out, "%s:\n", m);
    } else {
        level -= 1;
    }
    rv = (*inner)(out, &sd->data, "Data", level + 1);

    if (signatureOption == withSignature) {
        SECU_PrintAlgorithmID(out, &sd->signatureAlgorithm, "Signature Algorithm",
                              level + 1);
        DER_ConvertBitString(&sd->signature);
        SECU_PrintAsHex(out, &sd->signature, "Signature", level + 1);
    }
    SECU_PrintFingerprints(out, der, "Fingerprint", level + 1);
loser:
    PORT_FreeArena(arena, PR_FALSE);
    return rv;
}

int
SECU_PrintSignedData(FILE *out, SECItem *der, const char *m,
                     int level, SECU_PPFunc inner)
{
    return secu_PrintSignedDataSigOpt(out, der, m, level, inner,
                                      withSignature);
}

int
SECU_PrintSignedContent(FILE *out, SECItem *der, char *m,
                        int level, SECU_PPFunc inner)
{
    return secu_PrintSignedDataSigOpt(out, der, m, level, inner,
                                      noSignature);
}

SECStatus
SEC_PrintCertificateAndTrust(CERTCertificate *cert,
                             const char *label,
                             CERTCertTrust *trust)
{
    SECStatus rv;
    SECItem data;
    CERTCertTrust certTrust;
    PK11SlotList *slotList;
    PRBool falseAttributeFound = PR_FALSE;
    PRBool trueAttributeFound = PR_FALSE;
    const char *moz_policy_ca_info = NULL;

    data.data = cert->derCert.data;
    data.len = cert->derCert.len;

    rv = SECU_PrintSignedData(stdout, &data, label, 0,
                              SECU_PrintCertificate);
    if (rv) {
        return (SECFailure);
    }

    slotList = PK11_GetAllSlotsForCert(cert, NULL);
    if (slotList) {
        PK11SlotListElement *se = PK11_GetFirstSafe(slotList);
        for (; se; se = PK11_GetNextSafe(slotList, se, PR_FALSE)) {
            CK_OBJECT_HANDLE handle = PK11_FindCertInSlot(se->slot, cert, NULL);
            if (handle != CK_INVALID_HANDLE) {
                PORT_SetError(0);
                if (PK11_HasAttributeSet(se->slot, handle,
                                         CKA_NSS_MOZILLA_CA_POLICY, PR_FALSE)) {
                    trueAttributeFound = PR_TRUE;
                } else if (!PORT_GetError()) {
                    falseAttributeFound = PR_TRUE;
                }
            }
        }
        PK11_FreeSlotList(slotList);
    }

    if (trueAttributeFound) {
        moz_policy_ca_info = "true (attribute present)";
    } else if (falseAttributeFound) {
        moz_policy_ca_info = "false (attribute present)";
    } else {
        moz_policy_ca_info = "false (attribute missing)";
    }
    SECU_Indent(stdout, 1);
    printf("Mozilla-CA-Policy: %s\n", moz_policy_ca_info);

    if (trust) {
        SECU_PrintTrustFlags(stdout, trust,
                             "Certificate Trust Flags", 1);
    } else if (CERT_GetCertTrust(cert, &certTrust) == SECSuccess) {
        SECU_PrintTrustFlags(stdout, &certTrust,
                             "Certificate Trust Flags", 1);
    }

    /* The distrust fields are hard-coded in nssckbi and read-only.
     * If verifying some cert, with vfychain, for instance, the certificate may
     * not have a defined slot if not imported. */
    if (cert->slot != NULL && cert->distrust != NULL) {
        const unsigned int kDistrustFieldSize = 13;
        fprintf(stdout, "\n");
        SECU_Indent(stdout, 1);
        fprintf(stdout, "%s:\n", "Certificate Distrust Dates");
        if (cert->distrust->serverDistrustAfter.len == kDistrustFieldSize) {
            SECU_PrintTimeChoice(stdout,
                                 &cert->distrust->serverDistrustAfter,
                                 "Server Distrust After", 2);
        }
        if (cert->distrust->emailDistrustAfter.len == kDistrustFieldSize) {
            SECU_PrintTimeChoice(stdout,
                                 &cert->distrust->emailDistrustAfter,
                                 "E-mail Distrust After", 2);
        }
    }

    printf("\n");

    return (SECSuccess);
}

static char *
bestCertName(CERTCertificate *cert)
{
    if (cert->nickname) {
        return cert->nickname;
    }
    if (cert->emailAddr && cert->emailAddr[0]) {
        return cert->emailAddr;
    }
    return cert->subjectName;
}

void
SECU_printCertProblemsOnDate(FILE *outfile, CERTCertDBHandle *handle,
                             CERTCertificate *cert, PRBool checksig,
                             SECCertificateUsage certUsage, void *pinArg, PRBool verbose,
                             PRTime datetime)
{
    CERTVerifyLog log;
    CERTVerifyLogNode *node;

    PRErrorCode err = PORT_GetError();

    log.arena = PORT_NewArena(512);
    log.head = log.tail = NULL;
    log.count = 0;
    CERT_VerifyCertificate(handle, cert, checksig, certUsage, datetime, pinArg, &log, NULL);

    SECU_displayVerifyLog(outfile, &log, verbose);

    for (node = log.head; node; node = node->next) {
        if (node->cert)
            CERT_DestroyCertificate(node->cert);
    }
    PORT_FreeArena(log.arena, PR_FALSE);

    PORT_SetError(err); /* restore original error code */
}

void
SECU_displayVerifyLog(FILE *outfile, CERTVerifyLog *log,
                      PRBool verbose)
{
    CERTVerifyLogNode *node = NULL;
    unsigned int depth = (unsigned int)-1;
    unsigned int flags = 0;
    char *errstr = NULL;

    if (log->count > 0) {
        fprintf(outfile, "PROBLEM WITH THE CERT CHAIN:\n");
        for (node = log->head; node; node = node->next) {
            if (depth != node->depth) {
                depth = node->depth;
                fprintf(outfile, "CERT %d. %s %s:\n", depth,
                        bestCertName(node->cert),
                        depth ? "[Certificate Authority]" : "");
                if (verbose) {
                    const char *emailAddr;
                    emailAddr = CERT_GetFirstEmailAddress(node->cert);
                    if (emailAddr) {
                        fprintf(outfile, "Email Address(es): ");
                        do {
                            fprintf(outfile, "%s\n", emailAddr);
                            emailAddr = CERT_GetNextEmailAddress(node->cert,
                                                                 emailAddr);
                        } while (emailAddr);
                    }
                }
            }
            fprintf(outfile, "  ERROR %ld: %s\n", node->error,
                    SECU_Strerror(node->error));
            errstr = NULL;
            switch (node->error) {
                case SEC_ERROR_INADEQUATE_KEY_USAGE:
                    flags = (unsigned int)((char *)node->arg - (char *)NULL);
                    switch (flags) {
                        case KU_DIGITAL_SIGNATURE:
                            errstr = "Cert cannot sign.";
                            break;
                        case KU_KEY_ENCIPHERMENT:
                            errstr = "Cert cannot encrypt.";
                            break;
                        case KU_KEY_CERT_SIGN:
                            errstr = "Cert cannot sign other certs.";
                            break;
                        default:
                            errstr = "[unknown usage].";
                            break;
                    }
                    break;
                case SEC_ERROR_INADEQUATE_CERT_TYPE:
                    flags = (unsigned int)((char *)node->arg - (char *)NULL);
                    switch (flags) {
                        case NS_CERT_TYPE_SSL_CLIENT:
                        case NS_CERT_TYPE_SSL_SERVER:
                            errstr = "Cert cannot be used for SSL.";
                            break;
                        case NS_CERT_TYPE_SSL_CA:
                            errstr = "Cert cannot be used as an SSL CA.";
                            break;
                        case NS_CERT_TYPE_EMAIL:
                            errstr = "Cert cannot be used for SMIME.";
                            break;
                        case NS_CERT_TYPE_EMAIL_CA:
                            errstr = "Cert cannot be used as an SMIME CA.";
                            break;
                        case NS_CERT_TYPE_OBJECT_SIGNING:
                            errstr = "Cert cannot be used for object signing.";
                            break;
                        case NS_CERT_TYPE_OBJECT_SIGNING_CA:
                            errstr = "Cert cannot be used as an object signing CA.";
                            break;
                        default:
                            errstr = "[unknown usage].";
                            break;
                    }
                    break;
                case SEC_ERROR_UNKNOWN_ISSUER:
                case SEC_ERROR_UNTRUSTED_ISSUER:
                case SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE:
                    errstr = node->cert->issuerName;
                    break;
                default:
                    break;
            }
            if (errstr) {
                fprintf(stderr, "    %s\n", errstr);
            }
        }
    }
}

void
SECU_printCertProblems(FILE *outfile, CERTCertDBHandle *handle,
                       CERTCertificate *cert, PRBool checksig,
                       SECCertificateUsage certUsage, void *pinArg, PRBool verbose)
{
    SECU_printCertProblemsOnDate(outfile, handle, cert, checksig,
                                 certUsage, pinArg, verbose, PR_Now());
}

SECStatus
SECU_StoreCRL(PK11SlotInfo *slot, SECItem *derCrl, PRFileDesc *outFile,
              PRBool ascii, char *url)
{
    PORT_Assert(derCrl != NULL);
    if (!derCrl) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (outFile != NULL) {
        if (ascii) {
            PR_fprintf(outFile, "%s\n%s\n%s\n", NS_CRL_HEADER,
                       BTOA_DataToAscii(derCrl->data, derCrl->len),
                       NS_CRL_TRAILER);
        } else {
            if (PR_Write(outFile, derCrl->data, derCrl->len) != derCrl->len) {
                return SECFailure;
            }
        }
    }
    if (slot) {
        CERTSignedCrl *newCrl = PK11_ImportCRL(slot, derCrl, url,
                                               SEC_CRL_TYPE, NULL, 0, NULL, 0);
        if (newCrl != NULL) {
            SEC_DestroyCrl(newCrl);
            return SECSuccess;
        }
        return SECFailure;
    }
    if (!outFile && !slot) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    return SECSuccess;
}

SECStatus
SECU_SignAndEncodeCRL(CERTCertificate *issuer, CERTSignedCrl *signCrl,
                      SECOidTag hashAlgTag, SignAndEncodeFuncExitStat *resCode)
{
    SECItem der;
    SECKEYPrivateKey *caPrivateKey = NULL;
    SECStatus rv;
    PLArenaPool *arena;
    SECOidTag algID;
    void *dummy;

    PORT_Assert(issuer != NULL && signCrl != NULL);
    if (!issuer || !signCrl) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    arena = signCrl->arena;

    caPrivateKey = PK11_FindKeyByAnyCert(issuer, NULL);
    if (caPrivateKey == NULL) {
        *resCode = noKeyFound;
        return SECFailure;
    }

    algID = SEC_GetSignatureAlgorithmOidTagByKey(caPrivateKey, NULL, hashAlgTag);
    if (algID == SEC_OID_UNKNOWN) {
        *resCode = noSignatureMatch;
        rv = SECFailure;
        goto done;
    }

    if (!signCrl->crl.signatureAlg.parameters.data) {
        rv = SECOID_SetAlgorithmID(arena, &signCrl->crl.signatureAlg, algID, 0);
        if (rv != SECSuccess) {
            *resCode = failToEncode;
            goto done;
        }
    }

    der.len = 0;
    der.data = NULL;
    dummy = SEC_ASN1EncodeItem(arena, &der, &signCrl->crl,
                               SEC_ASN1_GET(CERT_CrlTemplate));
    if (!dummy) {
        *resCode = failToEncode;
        rv = SECFailure;
        goto done;
    }

    rv = SECU_DerSignDataCRL(arena, &signCrl->signatureWrap,
                             der.data, der.len, caPrivateKey, algID);
    if (rv != SECSuccess) {
        *resCode = failToSign;
        goto done;
    }

    signCrl->derCrl = PORT_ArenaZNew(arena, SECItem);
    if (signCrl->derCrl == NULL) {
        *resCode = noMem;
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        rv = SECFailure;
        goto done;
    }

    signCrl->derCrl->len = 0;
    signCrl->derCrl->data = NULL;
    dummy = SEC_ASN1EncodeItem(arena, signCrl->derCrl, signCrl,
                               SEC_ASN1_GET(CERT_SignedCrlTemplate));
    if (!dummy) {
        *resCode = failToEncode;
        rv = SECFailure;
        goto done;
    }

done:
    SECKEY_DestroyPrivateKey(caPrivateKey);
    return rv;
}

SECStatus
SECU_CopyCRL(PLArenaPool *destArena, CERTCrl *destCrl, CERTCrl *srcCrl)
{
    void *dummy;
    SECStatus rv = SECSuccess;
    SECItem der;

    PORT_Assert(destArena && srcCrl && destCrl);
    if (!destArena || !srcCrl || !destCrl) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    der.len = 0;
    der.data = NULL;
    dummy = SEC_ASN1EncodeItem(destArena, &der, srcCrl,
                               SEC_ASN1_GET(CERT_CrlTemplate));
    if (!dummy) {
        return SECFailure;
    }

    rv = SEC_QuickDERDecodeItem(destArena, destCrl,
                                SEC_ASN1_GET(CERT_CrlTemplate), &der);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    destCrl->arena = destArena;

    return rv;
}

SECStatus
SECU_DerSignDataCRL(PLArenaPool *arena, CERTSignedData *sd,
                    unsigned char *buf, int len, SECKEYPrivateKey *pk,
                    SECOidTag algID)
{
    SECItem it;
    SECStatus rv;

    it.data = 0;

    /* XXX We should probably have some asserts here to make sure the key type
     * and algID match
     */

    /* Sign input buffer */
    rv = SEC_SignData(&it, buf, len, pk, algID);
    if (rv != SECSuccess) {
        goto loser;
    }

    /* Fill out SignedData object */
    PORT_Memset(sd, 0, sizeof(*sd));
    sd->data.data = buf;
    sd->data.len = len;
    rv = SECITEM_CopyItem(arena, &sd->signature, &it);
    if (rv != SECSuccess) {
        goto loser;
    }

    sd->signature.len <<= 3; /* convert to bit string */
    rv = SECOID_SetAlgorithmID(arena, &sd->signatureAlgorithm, algID, 0);
    if (rv != SECSuccess) {
        goto loser;
    }

loser:
    PORT_Free(it.data);
    return rv;
}

/*
 * Find the issuer of a Crl.  Use the authorityKeyID if it exists.
 */
CERTCertificate *
SECU_FindCrlIssuer(CERTCertDBHandle *dbhandle, SECItem *subject,
                   CERTAuthKeyID *authorityKeyID, PRTime validTime)
{
    CERTCertificate *issuerCert = NULL;
    CERTCertList *certList = NULL;
    CERTCertTrust trust;

    if (!subject) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    certList =
        CERT_CreateSubjectCertList(NULL, dbhandle, subject,
                                   validTime, PR_TRUE);
    if (certList) {
        CERTCertListNode *node = CERT_LIST_HEAD(certList);

        /* XXX and authoritykeyid in the future */
        while (!CERT_LIST_END(node, certList)) {
            CERTCertificate *cert = node->cert;
            /* check cert CERTCertTrust data is allocated, check cert
               usage extension, check that cert has pkey in db. Select
               the first (newest) user cert */
            if (CERT_GetCertTrust(cert, &trust) == SECSuccess &&
                CERT_CheckCertUsage(cert, KU_CRL_SIGN) == SECSuccess &&
                CERT_IsUserCert(cert)) {

                issuerCert = CERT_DupCertificate(cert);
                break;
            }
            node = CERT_LIST_NEXT(node);
        }
        CERT_DestroyCertList(certList);
    }
    return (issuerCert);
}

/* Encodes and adds extensions to the CRL or CRL entries. */
SECStatus
SECU_EncodeAndAddExtensionValue(PLArenaPool *arena, void *extHandle,
                                void *value, PRBool criticality, int extenType,
                                EXTEN_EXT_VALUE_ENCODER EncodeValueFn)
{
    SECItem encodedValue;
    SECStatus rv;

    encodedValue.data = NULL;
    encodedValue.len = 0;
    do {
        rv = (*EncodeValueFn)(arena, value, &encodedValue);
        if (rv != SECSuccess)
            break;

        rv = CERT_AddExtension(extHandle, extenType, &encodedValue,
                               criticality, PR_TRUE);
        if (rv != SECSuccess)
            break;
    } while (0);

    return (rv);
}

CERTCertificate *
SECU_FindCertByNicknameOrFilename(CERTCertDBHandle *handle,
                                  char *name, PRBool ascii,
                                  void *pwarg)
{
    CERTCertificate *the_cert;
    the_cert = CERT_FindCertByNicknameOrEmailAddrCX(handle, name, pwarg);
    if (the_cert) {
        return the_cert;
    }
    the_cert = PK11_FindCertFromNickname(name, pwarg);
    if (!the_cert) {
        /* Don't have a cert with name "name" in the DB. Try to
         * open a file with such name and get the cert from there.*/
        SECStatus rv;
        SECItem item = { 0, NULL, 0 };
        PRFileDesc *fd = PR_Open(name, PR_RDONLY, 0777);
        if (!fd) {
            return NULL;
        }
        rv = SECU_ReadDERFromFile(&item, fd, ascii, PR_FALSE);
        PR_Close(fd);
        if (rv != SECSuccess || !item.len) {
            PORT_Free(item.data);
            return NULL;
        }
        the_cert = CERT_NewTempCertificate(handle, &item,
                                           NULL /* nickname */,
                                           PR_FALSE /* isPerm */,
                                           PR_TRUE /* copyDER */);
        PORT_Free(item.data);
    }
    return the_cert;
}

/* Convert a SSL/TLS protocol version string into the respective numeric value
 * defined by the SSL_LIBRARY_VERSION_* constants,
 * while accepting a flexible set of case-insensitive identifiers.
 *
 * Caller must specify bufLen, allowing the function to operate on substrings.
 */
static SECStatus
SECU_GetSSLVersionFromName(const char *buf, size_t bufLen, PRUint16 *version)
{
    if (!buf || !version) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (!PL_strncasecmp(buf, "ssl3", bufLen)) {
        *version = SSL_LIBRARY_VERSION_3_0;
        return SECSuccess;
    }
    if (!PL_strncasecmp(buf, "tls1.0", bufLen)) {
        *version = SSL_LIBRARY_VERSION_TLS_1_0;
        return SECSuccess;
    }
    if (!PL_strncasecmp(buf, "tls1.1", bufLen)) {
        *version = SSL_LIBRARY_VERSION_TLS_1_1;
        return SECSuccess;
    }
    if (!PL_strncasecmp(buf, "tls1.2", bufLen)) {
        *version = SSL_LIBRARY_VERSION_TLS_1_2;
        return SECSuccess;
    }

    if (!PL_strncasecmp(buf, "tls1.3", bufLen)) {
        *version = SSL_LIBRARY_VERSION_TLS_1_3;
        return SECSuccess;
    }

    PORT_SetError(SEC_ERROR_INVALID_ARGS);
    return SECFailure;
}

SECStatus
SECU_ParseSSLVersionRangeString(const char *input,
                                const SSLVersionRange defaultVersionRange,
                                SSLVersionRange *vrange)
{
    const char *colonPos;
    size_t colonIndex;
    const char *maxStr;

    if (!input || !vrange) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    // We don't support SSL2 any longer.
    if (defaultVersionRange.min < SSL_LIBRARY_VERSION_3_0 ||
        defaultVersionRange.max < SSL_LIBRARY_VERSION_3_0) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (!strcmp(input, ":")) {
        /* special value, use default */
        *vrange = defaultVersionRange;
        return SECSuccess;
    }

    colonPos = strchr(input, ':');
    if (!colonPos) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    colonIndex = colonPos - input;
    maxStr = colonPos + 1;

    if (!colonIndex) {
        /* colon was first character, min version is empty */
        vrange->min = defaultVersionRange.min;
    } else {
        PRUint16 version;
        /* colonIndex is equivalent to the length of the min version substring */
        if (SECU_GetSSLVersionFromName(input, colonIndex, &version) != SECSuccess) {
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            return SECFailure;
        }

        vrange->min = version;
    }

    if (!*maxStr) {
        vrange->max = defaultVersionRange.max;
    } else {
        PRUint16 version;
        /* if max version is empty, then maxStr points to the string terminator */
        if (SECU_GetSSLVersionFromName(maxStr, strlen(maxStr), &version) !=
            SECSuccess) {
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            return SECFailure;
        }

        vrange->max = version;
    }

    if (vrange->min > vrange->max) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    return SECSuccess;
}

#define NAME_AND_LEN(s) sizeof(s) - 1, s
static const struct SSLNamedGroupString {
    int len;
    char *name;
    SSLNamedGroup grp;
} sslNamedGroupStringArray[] = {
    { NAME_AND_LEN("P256"), ssl_grp_ec_secp256r1 },
    { NAME_AND_LEN("P384"), ssl_grp_ec_secp384r1 },
    { NAME_AND_LEN("P521"), ssl_grp_ec_secp521r1 },
    { NAME_AND_LEN("x25519"), ssl_grp_ec_curve25519 },
    { NAME_AND_LEN("FF2048"), ssl_grp_ffdhe_2048 },
    { NAME_AND_LEN("FF3072"), ssl_grp_ffdhe_3072 },
    { NAME_AND_LEN("FF4096"), ssl_grp_ffdhe_4096 },
    { NAME_AND_LEN("FF6144"), ssl_grp_ffdhe_6144 },
    { NAME_AND_LEN("FF8192"), ssl_grp_ffdhe_8192 },
    { NAME_AND_LEN("x25519mlkem768"), ssl_grp_kem_mlkem768x25519 },
    { NAME_AND_LEN("secp256r1mlkem768"), ssl_grp_kem_secp256r1mlkem768 },
    { NAME_AND_LEN("secp384r1mlkem1024"), ssl_grp_kem_secp384r1mlkem1024 },
    { NAME_AND_LEN("MLKEM1024"), ssl_grp_kem_mlkem1024 },
    // keep for compatibility
    { NAME_AND_LEN("mlkem768x25519"), ssl_grp_kem_mlkem768x25519 },
};

static const size_t sslNamedGroupStringLen = PR_ARRAY_SIZE(sslNamedGroupStringArray);

static SSLNamedGroup
groupNameToNamedGroup(char *name)
{
    int len = PL_strlen(name);
    int i;

    for (i = 0; i < sslNamedGroupStringLen; i++) {
        const struct SSLNamedGroupString *ngs = &sslNamedGroupStringArray[i];
        if (len == ngs->len) {
            if (!strncmp(name, ngs->name, len)) {
                return ngs->grp;
            }
        }
    }
    return ssl_grp_none;
}

static SECStatus
countItems(const char *arg, unsigned int *numItems)
{
    char *str = PORT_Strdup(arg);
    if (!str) {
        return SECFailure;
    }
    char *p = strtok(str, ",");
    while (p) {
        ++(*numItems);
        p = strtok(NULL, ",");
    }
    PORT_Free(str);
    str = NULL;
    return SECSuccess;
}

SECStatus
parseGroupList(const char *arg, SSLNamedGroup **enabledGroups,
               unsigned int *enabledGroupsCount)
{
    SSLNamedGroup *groups;
    char *str;
    char *p;
    unsigned int numValues = 0;
    unsigned int count = 0;

    if (countItems(arg, &numValues) != SECSuccess) {
        return SECFailure;
    }
    groups = PORT_ZNewArray(SSLNamedGroup, numValues);
    if (!groups) {
        return SECFailure;
    }

    /* Get group names. */
    str = PORT_Strdup(arg);
    if (!str) {
        goto done;
    }
    p = strtok(str, ",");
    while (p) {
        SSLNamedGroup group = groupNameToNamedGroup(p);
        if (group == ssl_grp_none) {
            count = 0;
            goto done;
        }
        groups[count++] = group;
        p = strtok(NULL, ",");
    }

done:
    PORT_Free(str);
    if (!count) {
        PORT_Free(groups);
        return SECFailure;
    }

    *enabledGroupsCount = count;
    *enabledGroups = groups;
    return SECSuccess;
}

const char *
SECU_NamedGroupToGroupName(SSLNamedGroup grp)
{
    int i;
    static char unknownBuf[32];

    if (grp == ssl_grp_none) {
        return "None";
    }

    for (i = 0; i < sslNamedGroupStringLen; i++) {
        const struct SSLNamedGroupString *ngs = &sslNamedGroupStringArray[i];
        if (grp == ngs->grp) {
            return ngs->name;
        }
    }
    snprintf(unknownBuf, sizeof(unknownBuf), "Unknown %04x\n", grp);

    return unknownBuf;
}

const char *
SECU_NamedGroupGetNextName(size_t i)
{
    if (i >= sslNamedGroupStringLen) {
        return NULL;
    }
    return sslNamedGroupStringArray[i].name;
}

#define MAKE_SCHEME(x)                  \
    {                                   \
        sizeof(#x) - 1, #x, ssl_sig_##x \
    }
static const struct SSLSignatureSchemeString {
    int len;
    char *name;
    SSLSignatureScheme scheme;
} sslSignatureSchemeStringArray[] = {
    MAKE_SCHEME(rsa_pkcs1_sha1),
    MAKE_SCHEME(rsa_pkcs1_sha256),
    MAKE_SCHEME(rsa_pkcs1_sha384),
    MAKE_SCHEME(rsa_pkcs1_sha512),
    MAKE_SCHEME(ecdsa_sha1),
    MAKE_SCHEME(ecdsa_secp256r1_sha256),
    MAKE_SCHEME(ecdsa_secp384r1_sha384),
    MAKE_SCHEME(ecdsa_secp521r1_sha512),
    MAKE_SCHEME(rsa_pss_rsae_sha256),
    MAKE_SCHEME(rsa_pss_rsae_sha384),
    MAKE_SCHEME(rsa_pss_rsae_sha512),
    MAKE_SCHEME(ed25519),
    MAKE_SCHEME(ed448),
    MAKE_SCHEME(rsa_pss_pss_sha256),
    MAKE_SCHEME(rsa_pss_pss_sha384),
    MAKE_SCHEME(rsa_pss_pss_sha512),
    MAKE_SCHEME(dsa_sha1),
    MAKE_SCHEME(dsa_sha256),
    MAKE_SCHEME(dsa_sha384),
    MAKE_SCHEME(dsa_sha512),
    MAKE_SCHEME(mldsa44),
    MAKE_SCHEME(mldsa65),
    MAKE_SCHEME(mldsa87),
};

static const size_t sslSignatureSchemeStringLen =
    PR_ARRAY_SIZE(sslSignatureSchemeStringArray);

const char *
SECU_SignatureSchemeGetNextScheme(size_t i)
{
    if (i >= sslSignatureSchemeStringLen) {
        return NULL;
    }
    return sslSignatureSchemeStringArray[i].name;
}

const char *
SECU_SignatureSchemeName(SSLSignatureScheme scheme)
{
    int i;
    static char unknownBuf[32];

    if (scheme == ssl_sig_none) {
        return "None";
    }

    for (i = 0; i < sslSignatureSchemeStringLen; i++) {
        const struct SSLSignatureSchemeString *schemp =
            &sslSignatureSchemeStringArray[i];
        if (scheme == schemp->scheme) {
            return schemp->name;
        }
    }

    /* we don't include ssl_sig_rsa_pks1_sha1md5 in our list because we
     * don't want to select it from the command line, but if you are using
     * ssl3,  it's possible for this signataure scheme to pop out, so we
     * want output it. The name is the same value tstclnt used for this
     * scheme originally */
    if (scheme == ssl_sig_rsa_pkcs1_sha1md5) {
        return "RSA PKCS#1 SHA1+MD5";
    }

    snprintf(unknownBuf, sizeof(unknownBuf), "Unknown %04x\n", scheme);

    return unknownBuf;
}

SSLSignatureScheme
schemeNameToScheme(const char *name)
{
    int len = PL_strlen(name);
    int i;

    for (i = 0; i < sslSignatureSchemeStringLen; i++) {
        const struct SSLSignatureSchemeString *schemp =
            &sslSignatureSchemeStringArray[i];
        if (len == schemp->len) {
            if (!strncmp(name, schemp->name, len)) {
                return schemp->scheme;
            }
        }
    }
    return ssl_sig_none;
}

SECStatus
parseSigSchemeList(const char *arg, const SSLSignatureScheme **enabledSigSchemes,
                   unsigned int *enabledSigSchemeCount)
{
    SSLSignatureScheme *schemes;
    unsigned int numValues = 0;
    unsigned int count = 0;

    if (countItems(arg, &numValues) != SECSuccess) {
        return SECFailure;
    }
    schemes = PORT_ZNewArray(SSLSignatureScheme, numValues);
    if (!schemes) {
        return SECFailure;
    }

    /* Get group names. */
    char *str = PORT_Strdup(arg);
    if (!str) {
        goto done;
    }
    char *p = strtok(str, ",");
    while (p) {
        SSLSignatureScheme scheme = schemeNameToScheme(p);
        if (scheme == ssl_sig_none) {
            count = 0;
            goto done;
        }
        schemes[count++] = scheme;
        p = strtok(NULL, ",");
    }

done:
    PORT_Free(str);
    if (!count) {
        PORT_Free(schemes);
        return SECFailure;
    }

    *enabledSigSchemeCount = count;
    *enabledSigSchemes = schemes;
    return SECSuccess;
}

/* Parse the exporter spec in the form: LABEL[:OUTPUT-LENGTH[:CONTEXT]] */
static SECStatus
parseExporter(const char *arg,
              secuExporter *exporter)
{
    SECStatus rv = SECSuccess;

    char *str = PORT_Strdup(arg);
    if (!str) {
        rv = SECFailure;
        goto done;
    }

    char *labelEnd = strchr(str, ':');
    if (labelEnd) {
        *labelEnd = '\0';
        labelEnd++;

        /* To extract CONTEXT, first skip OUTPUT-LENGTH */
        char *outputEnd = strchr(labelEnd, ':');
        if (outputEnd) {
            *outputEnd = '\0';
            outputEnd++;

            exporter->hasContext = PR_TRUE;
            exporter->context.data = (unsigned char *)PORT_Strdup(outputEnd);
            exporter->context.len = strlen(outputEnd);
            if (PORT_Strncasecmp((char *)exporter->context.data, "0x", 2) == 0) {
                rv = SECU_SECItemHexStringToBinary(&exporter->context);
                if (rv != SECSuccess) {
                    goto done;
                }
            }
        }
    }

    if (labelEnd && *labelEnd != '\0') {
        long int outputLength = strtol(labelEnd, NULL, 10);
        if (!(outputLength > 0 && outputLength <= UINT_MAX)) {
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            rv = SECFailure;
            goto done;
        }
        exporter->outputLength = outputLength;
    } else {
        exporter->outputLength = 20;
    }

    char *label = PORT_Strdup(str);
    exporter->label.data = (unsigned char *)label;
    exporter->label.len = strlen(label);
    if (PORT_Strncasecmp((char *)exporter->label.data, "0x", 2) == 0) {
        rv = SECU_SECItemHexStringToBinary(&exporter->label);
        if (rv != SECSuccess) {
            goto done;
        }
    }

done:
    PORT_Free(str);

    return rv;
}

SECStatus
parseExporters(const char *arg,
               const secuExporter **enabledExporters,
               unsigned int *enabledExporterCount)
{
    secuExporter *exporters;
    unsigned int numValues = 0;
    unsigned int count = 0;

    if (countItems(arg, &numValues) != SECSuccess) {
        return SECFailure;
    }
    exporters = PORT_ZNewArray(secuExporter, numValues);
    if (!exporters) {
        return SECFailure;
    }

    /* Get exporter definitions. */
    char *str = PORT_Strdup(arg);
    if (!str) {
        goto done;
    }
    char *p = strtok(str, ",");
    while (p) {
        SECStatus rv = parseExporter(p, &exporters[count++]);
        if (rv != SECSuccess) {
            count = 0;
            goto done;
        }
        p = strtok(NULL, ",");
    }

done:
    PORT_Free(str);
    if (!count) {
        PORT_Free(exporters);
        return SECFailure;
    }

    *enabledExporterCount = count;
    *enabledExporters = exporters;
    return SECSuccess;
}

static SECStatus
exportKeyingMaterial(PRFileDesc *fd, const secuExporter *exporter)
{
    SECStatus rv = SECSuccess;
    unsigned char *out = PORT_Alloc(exporter->outputLength);

    if (!out) {
        fprintf(stderr, "Unable to allocate buffer for keying material\n");
        return SECFailure;
    }
    rv = SSL_ExportKeyingMaterial(fd,
                                  (char *)exporter->label.data,
                                  exporter->label.len,
                                  exporter->hasContext,
                                  exporter->context.data,
                                  exporter->context.len,
                                  out,
                                  exporter->outputLength);
    if (rv != SECSuccess) {
        goto done;
    }
    fprintf(stdout, "Exported Keying Material:\n");
    secu_PrintRawString(stdout, (SECItem *)&exporter->label, "Label", 1);
    if (exporter->hasContext) {
        SECU_PrintAsHex(stdout, &exporter->context, "Context", 1);
    }
    SECU_Indent(stdout, 1);
    fprintf(stdout, "Length: %u\n", exporter->outputLength);
    SECItem temp = { siBuffer, out, exporter->outputLength };
    SECU_PrintAsHex(stdout, &temp, "Keying Material", 1);

done:
    PORT_Free(out);
    return rv;
}

SECStatus
exportKeyingMaterials(PRFileDesc *fd,
                      const secuExporter *exporters,
                      unsigned int exporterCount)
{
    unsigned int i;

    for (i = 0; i < exporterCount; i++) {
        SECStatus rv = exportKeyingMaterial(fd, &exporters[i]);
        if (rv != SECSuccess) {
            return rv;
        }
    }

    return SECSuccess;
}

SECStatus
readPSK(const char *arg, SECItem *psk, SECItem *label)
{
    SECStatus rv = SECFailure;
    char *str = PORT_Strdup(arg);
    if (!str) {
        goto cleanup;
    }

    char *pskBytes = strtok(str, ":");
    if (!pskBytes) {
        goto cleanup;
    }
    if (PORT_Strncasecmp(pskBytes, "0x", 2) != 0) {
        goto cleanup;
    }

    psk = SECU_HexString2SECItem(NULL, psk, &pskBytes[2]);
    if (!psk || !psk->data || psk->len != strlen(&str[2]) / 2) {
        goto cleanup;
    }

    SECItem labelItem = { siBuffer, NULL, 0 };
    char *inLabel = strtok(NULL, ":");
    if (inLabel) {
        labelItem.data = (unsigned char *)PORT_Strdup(inLabel);
        if (!labelItem.data) {
            goto cleanup;
        }
        labelItem.len = strlen(inLabel);

        if (PORT_Strncasecmp(inLabel, "0x", 2) == 0) {
            rv = SECU_SECItemHexStringToBinary(&labelItem);
            if (rv != SECSuccess) {
                SECITEM_FreeItem(&labelItem, PR_FALSE);
                goto cleanup;
            }
        }
        rv = SECSuccess;
    } else {
        PRUint8 defaultLabel[] = { 'C', 'l', 'i', 'e', 'n', 't', '_',
                                   'i', 'd', 'e', 'n', 't', 'i', 't', 'y' };
        SECItem src = { siBuffer, defaultLabel, sizeof(defaultLabel) };
        rv = SECITEM_CopyItem(NULL, &labelItem, &src);
    }
    if (rv == SECSuccess) {
        *label = labelItem;
    }

cleanup:
    PORT_Free(str);
    return rv;
}

static SECStatus
secu_PrintPKCS12DigestInfo(FILE *out, const SECItem *t, char *m, int level)
{
    SECItem my = *t;
    SECItem rawDigestAlgID;
    SECItem digestData;
    SECStatus rv;
    PLArenaPool *arena;
    SECAlgorithmID digestAlgID;
    char *mAlgID = NULL;
    char *mDigest = NULL;

    /* strip the outer sequence */
    if ((my.data[0] != (SEC_ASN1_CONSTRUCTED | SEC_ASN1_SEQUENCE)) ||
        SECSuccess != SECU_StripTagAndLength(&my)) {
        PORT_SetError(SEC_ERROR_BAD_DER);
        return SECFailure;
    }

    /* get the algorithm ID */
    if (SECSuccess != SECU_ExtractBERAndStep(&my, &rawDigestAlgID)) {
        return SECFailure;
    }
    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        return SECFailure;
    }
#define DIGEST_ALGID_STRING "Digest Algorithm ID"
    if (m)
        mAlgID = PR_smprintf("%s " DIGEST_ALGID_STRING, m);
    rv = SEC_QuickDERDecodeItem(arena, &digestAlgID,
                                SEC_ASN1_GET(SECOID_AlgorithmIDTemplate),
                                &rawDigestAlgID);
    if (rv == SECSuccess) {
        SECU_PrintAlgorithmID(out, &digestAlgID,
                              mAlgID ? mAlgID : DIGEST_ALGID_STRING, level);
    }
    if (mAlgID)
        PR_smprintf_free(mAlgID);
    PORT_FreeArena(arena, PR_FALSE);
    if (rv != SECSuccess) {
        return rv;
    }

    /* get the mac data */
    if (SECSuccess != SECU_ExtractBERAndStep(&my, &digestData)) {
        return SECFailure;
    }
    if ((digestData.data[0] & SEC_ASN1_TAGNUM_MASK) != SEC_ASN1_OCTET_STRING) {
        PORT_SetError(SEC_ERROR_BAD_DER);
        return SECFailure;
    }
#define DIGEST_STRING "Digest"
    if (m)
        mDigest = PR_smprintf("%s " DIGEST_STRING, m);
    secu_PrintOctetString(out, &digestData,
                          mDigest ? mDigest : DIGEST_STRING, level);
    if (mDigest)
        PR_smprintf_free(mDigest);
    return SECSuccess;
}

static SECStatus
secu_PrintPKCS12MacData(FILE *out, const SECItem *t, char *m, int level)
{
    SECItem my = *t;
    SECItem hash;
    SECItem salt;

    if (m) {
        SECU_Indent(out, level);
        fprintf(out, "%s: \n", m);
        level++;
    }

    /* strip the outer sequence */
    if ((my.data[0] != (SEC_ASN1_CONSTRUCTED | SEC_ASN1_SEQUENCE)) ||
        SECSuccess != SECU_StripTagAndLength(&my)) {
        PORT_SetError(SEC_ERROR_BAD_DER);
        return SECFailure;
    }

    if (SECSuccess != SECU_ExtractBERAndStep(&my, &hash)) {
        return SECFailure;
    }
    if (SECSuccess != secu_PrintPKCS12DigestInfo(out, &hash, "Mac", level)) {
        return SECFailure;
    }

    /* handle the salt */
    if (SECSuccess != SECU_ExtractBERAndStep(&my, &salt)) {
        return SECFailure;
        ;
    }
    if ((salt.data[0] & SEC_ASN1_TAGNUM_MASK) != SEC_ASN1_OCTET_STRING) {
        PORT_SetError(SEC_ERROR_BAD_DER);
        return SECFailure;
    }
    secu_PrintOctetString(out, &salt, "Mac Salt", level);

    if (my.len &&
        ((my.data[0] & SEC_ASN1_TAGNUM_MASK) == SEC_ASN1_INTEGER)) {
        SECItem iterator;
        if (SECSuccess != SECU_ExtractBERAndStep(&my, &iterator)) {
            return SECFailure;
        }
        SECU_PrintEncodedInteger(out, &iterator, "Iterations", level);
    }
    return SECSuccess;
}

SECStatus
SECU_PrintPKCS12(FILE *out, const SECItem *t, char *m, int level)
{
    SECItem my = *t;
    SECItem authSafe;
    SECItem macData;

    SECU_Indent(out, level);
    fprintf(out, "%s:\n", m);
    level++;

    /* strip the outer sequence */
    if ((my.data[0] != (SEC_ASN1_CONSTRUCTED | SEC_ASN1_SEQUENCE)) ||
        SECSuccess != SECU_StripTagAndLength(&my)) {
        PORT_SetError(SEC_ERROR_BAD_DER);
        return SECFailure;
    }
    /* print and remove the optional version number */
    if (my.len && ((my.data[0] & SEC_ASN1_TAGNUM_MASK) == SEC_ASN1_INTEGER)) {
        SECItem version;

        if (SECSuccess != SECU_ExtractBERAndStep(&my, &version)) {
            return SECFailure;
        }
        SECU_PrintEncodedInteger(out, &version, "Version", level);
    }

    /* print the authSafe */
    if (SECSuccess != SECU_ExtractBERAndStep(&my, &authSafe)) {
        return SECFailure;
    }
    if (SECSuccess != secu_PrintDERPKCS7ContentInfo(out, &authSafe,
                                                    secuPKCS7PKCS12AuthSafe,
                                                    "AuthSafe", level)) {
        return SECFailure;
    }

    /* print the mac data (optional) */
    if (!my.len) {
        return SECSuccess;
    }
    if (SECSuccess != SECU_ExtractBERAndStep(&my, &macData)) {
        return SECFailure;
    }
    if (SECSuccess != secu_PrintPKCS12MacData(out, &macData,
                                              "Mac Data", level)) {
        return SECFailure;
    }

    if (my.len) {
        fprintf(out, "Unknown extra data found \n");
    }
    return SECSuccess;
}
