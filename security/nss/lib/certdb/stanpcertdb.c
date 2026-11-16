/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "prtime.h"

#include "cert.h"
#include "certi.h"
#include "certdb.h"
#include "secitem.h"
#include "secder.h"

/* Call to PK11_FreeSlot below */

#include "secasn1.h"
#include "secerr.h"
#include "prmon.h"
#include "base64.h"
#include "sechash.h"
#include "plhash.h"
#include "pk11func.h" /* sigh */

#include "nsspki.h"
#include "pki.h"
#include "pkim.h"
#include "pki3hack.h"
#include "ckhelper.h"
#include "base.h"
#include "pkistore.h"
#include "dev3hack.h"
#include "dev.h"
#include "secmodi.h"

extern void CERT_MaybeLockCertTempPerm(const CERTCertificate *cert);
extern void CERT_MaybeUnlockCertTempPerm(const CERTCertificate *cert);

PRBool
SEC_CertNicknameConflict(const char *nickname, const SECItem *derSubject,
                         CERTCertDBHandle *handle)
{
    CERTCertificate *cert;
    PRBool conflict = PR_FALSE;

    cert = CERT_FindCertByNickname(handle, nickname);

    if (!cert) {
        return conflict;
    }

    conflict = !SECITEM_ItemsAreEqual(derSubject, &cert->derSubject);
    CERT_DestroyCertificate(cert);
    return conflict;
}

SECStatus
SEC_DeletePermCertificate(CERTCertificate *cert)
{
    PRStatus nssrv;
    NSSTrustDomain *td = STAN_GetDefaultTrustDomain();
    NSSCertificate *c = STAN_GetNSSCertificate(cert);
    CERTCertTrust *certTrust;

    if (c == NULL) {
        /* error code is set */
        return SECFailure;
    }

    certTrust = nssTrust_GetCERTCertTrustForCert(c, cert);
    if (certTrust) {
        NSSTrust *nssTrust = nssTrustDomain_FindTrustForCertificate(td, &c->encoding, &c->issuer, &c->serial);
        if (nssTrust) {
            nssrv = STAN_DeleteCertTrustMatchingSlot(c);
            if (nssrv != PR_SUCCESS) {
                CERT_MapStanError();
            }
            /* This call always returns PR_SUCCESS! */
            (void)nssTrust_Destroy(nssTrust);
        }
    }

    /* get rid of the token instances */
    nssrv = NSSCertificate_DeleteStoredObject(c, NULL);

    /* get rid of the cache entry */
    nssTrustDomain_LockCertCache(td);
    nssTrustDomain_RemoveCertFromCacheLOCKED(td, c);
    nssTrustDomain_UnlockCertCache(td);

    return (nssrv == PR_SUCCESS) ? SECSuccess : SECFailure;
}

SECStatus
CERT_GetCertTrust(const CERTCertificate *cert, CERTCertTrust *trust)
{
    SECStatus rv;
    CERT_LockCertTrust(cert);
    if (!cert || cert->trust == NULL) {
        rv = SECFailure;
    } else {
        *trust = *cert->trust;
        rv = SECSuccess;
    }
    CERT_UnlockCertTrust(cert);
    return (rv);
}

SECStatus
CERT_GetDERCertTrust(SECItem *derCert, CERTCertTrust *trust)
{
    if (!derCert || !trust) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    NSSTrustDomain *td = STAN_GetDefaultTrustDomain();
    NSSDER encoding;
    NSSITEM_FROM_SECITEM(&encoding, derCert);
    NSSDER issuer;
    NSSDER serial;
    if (nssPKIX509_GetIssuerAndSerialFromDER(&encoding, &issuer, &serial) != PR_SUCCESS) {
        // Despite the name, any errors here would be set by CERT_* functions,
        // so no need to map a STAN error.
        return SECFailure;
    }
    NSSTrust *nssTrust = nssTrustDomain_FindTrustForCertificate(td, &encoding, &issuer, &serial);
    if (!nssTrust) {
        CERT_MapStanError();
        PORT_Free(issuer.data);
        PORT_Free(serial.data);
        return SECFailure;
    }
    nssTrust_ToCERTCertTrust(nssTrust, trust);
    (void)nssTrust_Destroy(nssTrust);
    PORT_Free(issuer.data);
    PORT_Free(serial.data);
    return SECSuccess;
}

extern const NSSError NSS_ERROR_NO_ERROR;
extern const NSSError NSS_ERROR_INTERNAL_ERROR;
extern const NSSError NSS_ERROR_NO_MEMORY;
extern const NSSError NSS_ERROR_INVALID_POINTER;
extern const NSSError NSS_ERROR_INVALID_ARENA;
extern const NSSError NSS_ERROR_INVALID_ARENA_MARK;
extern const NSSError NSS_ERROR_DUPLICATE_POINTER;
extern const NSSError NSS_ERROR_POINTER_NOT_REGISTERED;
extern const NSSError NSS_ERROR_TRACKER_NOT_EMPTY;
extern const NSSError NSS_ERROR_TRACKER_NOT_INITIALIZED;
extern const NSSError NSS_ERROR_ARENA_MARKED_BY_ANOTHER_THREAD;
extern const NSSError NSS_ERROR_VALUE_TOO_LARGE;
extern const NSSError NSS_ERROR_UNSUPPORTED_TYPE;
extern const NSSError NSS_ERROR_BUFFER_TOO_SHORT;
extern const NSSError NSS_ERROR_INVALID_ATOB_CONTEXT;
extern const NSSError NSS_ERROR_INVALID_BASE64;
extern const NSSError NSS_ERROR_INVALID_BTOA_CONTEXT;
extern const NSSError NSS_ERROR_INVALID_ITEM;
extern const NSSError NSS_ERROR_INVALID_STRING;
extern const NSSError NSS_ERROR_INVALID_ASN1ENCODER;
extern const NSSError NSS_ERROR_INVALID_ASN1DECODER;
extern const NSSError NSS_ERROR_INVALID_BER;
extern const NSSError NSS_ERROR_INVALID_ATAV;
extern const NSSError NSS_ERROR_INVALID_ARGUMENT;
extern const NSSError NSS_ERROR_INVALID_UTF8;
extern const NSSError NSS_ERROR_INVALID_NSSOID;
extern const NSSError NSS_ERROR_UNKNOWN_ATTRIBUTE;
extern const NSSError NSS_ERROR_NOT_FOUND;
extern const NSSError NSS_ERROR_INVALID_PASSWORD;
extern const NSSError NSS_ERROR_USER_CANCELED;
extern const NSSError NSS_ERROR_MAXIMUM_FOUND;
extern const NSSError NSS_ERROR_CERTIFICATE_ISSUER_NOT_FOUND;
extern const NSSError NSS_ERROR_CERTIFICATE_IN_CACHE;
extern const NSSError NSS_ERROR_HASH_COLLISION;
extern const NSSError NSS_ERROR_DEVICE_ERROR;
extern const NSSError NSS_ERROR_INVALID_CERTIFICATE;
extern const NSSError NSS_ERROR_BUSY;
extern const NSSError NSS_ERROR_ALREADY_INITIALIZED;
extern const NSSError NSS_ERROR_PKCS11;

/* Look at the stan error stack and map it to NSS 3 errors */
#define STAN_MAP_ERROR(x, y) \
    else if (error == (x))   \
    {                        \
        secError = y;        \
    }

/*
 * map Stan errors into NSS errors
 * This function examines the stan error stack and automatically sets
 * PORT_SetError(); to the appropriate SEC_ERROR value.
 */
void
CERT_MapStanError()
{
    PRInt32 *errorStack;
    NSSError error, prevError;
    int secError;
    int i;

    errorStack = NSS_GetErrorStack();
    if (errorStack == 0) {
        PORT_SetError(0);
        return;
    }
    error = prevError = CKR_GENERAL_ERROR;
    /* get the 'top 2' error codes from the stack */
    for (i = 0; errorStack[i]; i++) {
        prevError = error;
        error = errorStack[i];
    }
    if (error == NSS_ERROR_PKCS11) {
        /* map it */
        secError = PK11_MapError(prevError);
    }
    STAN_MAP_ERROR(NSS_ERROR_NO_ERROR, 0)
    STAN_MAP_ERROR(NSS_ERROR_NO_MEMORY, SEC_ERROR_NO_MEMORY)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_BASE64, SEC_ERROR_BAD_DATA)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_BER, SEC_ERROR_BAD_DER)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_ATAV, SEC_ERROR_INVALID_AVA)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_PASSWORD, SEC_ERROR_BAD_PASSWORD)
    STAN_MAP_ERROR(NSS_ERROR_BUSY, SEC_ERROR_BUSY)
    STAN_MAP_ERROR(NSS_ERROR_DEVICE_ERROR, SEC_ERROR_IO)
    STAN_MAP_ERROR(NSS_ERROR_CERTIFICATE_ISSUER_NOT_FOUND,
                   SEC_ERROR_UNKNOWN_ISSUER)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_CERTIFICATE, SEC_ERROR_CERT_NOT_VALID)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_UTF8, SEC_ERROR_BAD_DATA)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_NSSOID, SEC_ERROR_BAD_DATA)

    /* these are library failure for lack of a better error code */
    STAN_MAP_ERROR(NSS_ERROR_NOT_FOUND, SEC_ERROR_LIBRARY_FAILURE)
    STAN_MAP_ERROR(NSS_ERROR_CERTIFICATE_IN_CACHE, SEC_ERROR_LIBRARY_FAILURE)
    STAN_MAP_ERROR(NSS_ERROR_MAXIMUM_FOUND, SEC_ERROR_LIBRARY_FAILURE)
    STAN_MAP_ERROR(NSS_ERROR_USER_CANCELED, SEC_ERROR_LIBRARY_FAILURE)
    STAN_MAP_ERROR(NSS_ERROR_TRACKER_NOT_INITIALIZED, SEC_ERROR_LIBRARY_FAILURE)
    STAN_MAP_ERROR(NSS_ERROR_ALREADY_INITIALIZED, SEC_ERROR_LIBRARY_FAILURE)
    STAN_MAP_ERROR(NSS_ERROR_ARENA_MARKED_BY_ANOTHER_THREAD,
                   SEC_ERROR_LIBRARY_FAILURE)
    STAN_MAP_ERROR(NSS_ERROR_HASH_COLLISION, SEC_ERROR_LIBRARY_FAILURE)

    STAN_MAP_ERROR(NSS_ERROR_INTERNAL_ERROR, SEC_ERROR_LIBRARY_FAILURE)

    /* these are all invalid arguments */
    STAN_MAP_ERROR(NSS_ERROR_INVALID_ARGUMENT, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_POINTER, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_ARENA, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_ARENA_MARK, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_DUPLICATE_POINTER, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_POINTER_NOT_REGISTERED, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_TRACKER_NOT_EMPTY, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_VALUE_TOO_LARGE, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_UNSUPPORTED_TYPE, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_BUFFER_TOO_SHORT, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_ATOB_CONTEXT, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_BTOA_CONTEXT, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_ITEM, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_STRING, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_ASN1ENCODER, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_INVALID_ASN1DECODER, SEC_ERROR_INVALID_ARGS)
    STAN_MAP_ERROR(NSS_ERROR_UNKNOWN_ATTRIBUTE, SEC_ERROR_INVALID_ARGS)
    else
    {
        secError = SEC_ERROR_LIBRARY_FAILURE;
    }
    PORT_SetError(secError);
}

SECStatus
CERT_ChangeCertTrust(CERTCertDBHandle *handle, CERTCertificate *cert,
                     CERTCertTrust *trust)
{
    SECStatus rv = SECSuccess;
    PRStatus ret;

    ret = STAN_ChangeCertTrust(cert, trust);
    if (ret != PR_SUCCESS) {
        rv = SECFailure;
        CERT_MapStanError();
    }
    return rv;
}

extern const NSSError NSS_ERROR_INVALID_CERTIFICATE;

SECStatus
__CERT_AddTempCertToPerm(CERTCertificate *cert, char *nickname,
                         CERTCertTrust *trust)
{
    NSSUTF8 *stanNick;
    PK11SlotInfo *slot;
    NSSToken *internal;
    NSSCryptoContext *context;
    nssCryptokiObject *permInstance;
    NSSCertificate *c = STAN_GetNSSCertificate(cert);
    nssCertificateStoreTrace lockTrace = { NULL, NULL, PR_FALSE, PR_FALSE };
    nssCertificateStoreTrace unlockTrace = { NULL, NULL, PR_FALSE, PR_FALSE };

    if (c == NULL) {
        CERT_MapStanError();
        return SECFailure;
    }

    nssPKIObject_Lock(&c->object);
    context = c->object.cryptoContext;
    nssPKIObject_Unlock(&c->object);
    if (!context) {
        PORT_SetError(SEC_ERROR_ADDING_CERT);
        return SECFailure; /* wasn't a temp cert */
    }
    stanNick = nssCertificate_GetNickname(c, NULL);
    if (stanNick && nickname && strcmp(nickname, stanNick) != 0) {
        /* different: take the new nickname */
        cert->nickname = NULL;
        nss_ZFreeIf(stanNick);
        stanNick = NULL;
    }
    if (!stanNick && nickname) {
        /* Either there was no nickname yet, or we have a new nickname */
        stanNick = nssUTF8_Duplicate((NSSUTF8 *)nickname, NULL);
    } /* else: old stanNick is identical to new nickname */
    /* Delete the temp instance */
    nssCertificateStore_Lock(context->certStore, &lockTrace);
    nssPKIObject_Lock(&c->object);
    if (c->object.cryptoContext == context) {
        nssCertificateStore_RemoveCertLOCKED(context->certStore, c);
        c->object.cryptoContext = NULL;
    }
    nssPKIObject_Unlock(&c->object);
    nssCertificateStore_Unlock(context->certStore, &lockTrace, &unlockTrace);

    /* Import the perm instance onto the internal token */
    slot = PK11_GetInternalKeySlot();
    internal = PK11Slot_GetNSSToken(slot);
    if (!internal) {
        PK11_FreeSlot(slot);
        PORT_SetError(SEC_ERROR_NO_TOKEN);
        return SECFailure;
    }
    permInstance = nssToken_ImportCertificate(
        internal, NULL, NSSCertificateType_PKIX, &c->id, stanNick, &c->encoding,
        &c->issuer, &c->subject, &c->serial, cert->emailAddr, PR_TRUE);
    (void)nssToken_Destroy(internal);
    nss_ZFreeIf(stanNick);
    stanNick = NULL;
    PK11_FreeSlot(slot);
    if (!permInstance) {
        if (NSS_GetError() == NSS_ERROR_INVALID_CERTIFICATE) {
            PORT_SetError(SEC_ERROR_REUSED_ISSUER_AND_SERIAL);
        }
        return SECFailure;
    }
    nssPKIObject_AddInstance(&c->object, permInstance);
    // nssTrustDomain_AddCertToCache takes ownership of 'c' and returns an
    // owned reference to an NSSCertificate that may be a different object from
    // 'c'. Because 'cert' is backed by 'c', and because it must not go away as
    // a result of calling this function, increment the refcount on 'c' for the
    // duration of the call.
    nssCertificate_AddRef(c);
    NSSCertificate *cInCache = nssTrustDomain_AddCertToCache(STAN_GetDefaultTrustDomain(), c);
    if (!cInCache) {
        CERT_MapStanError();
        return SECFailure;
    }
    c = cInCache;
    cert = STAN_GetCERTCertificateOrRelease(c);
    if (!cert) {
        CERT_MapStanError();
        return SECFailure;
    }
    CERT_LockCertTempPerm(cert);
    cert->istemp = PR_FALSE;
    cert->isperm = PR_TRUE;
    CERT_UnlockCertTempPerm(cert);
    SECStatus rv = SECSuccess;
    if (trust && STAN_ChangeCertTrust(cert, trust) != PR_SUCCESS) {
        rv = SECFailure;
        CERT_MapStanError();
    }
    nssCertificate_Destroy(c);
    return rv;
}

SECStatus
CERT_AddTempCertToPerm(CERTCertificate *cert, char *nickname,
                       CERTCertTrust *trust)
{
    return __CERT_AddTempCertToPerm(cert, nickname, trust);
}

CERTCertificate *
CERT_NewTempCertificate(CERTCertDBHandle *handle, SECItem *derCert,
                        char *nickname, PRBool isperm, PRBool copyDER)
{
    NSSCertificate *c;
    CERTCertificate *cc;
    NSSCertificate *tempCert = NULL;
    nssPKIObject *pkio;
    NSSCryptoContext *gCC = STAN_GetDefaultCryptoContext();
    NSSTrustDomain *gTD = STAN_GetDefaultTrustDomain();
    if (!isperm) {
        NSSDER encoding;
        NSSITEM_FROM_SECITEM(&encoding, derCert);
        /* First, see if it is already a temp cert */
        c = NSSCryptoContext_FindCertificateByEncodedCertificate(gCC,
                                                                 &encoding);
        if (!c && handle) {
            /* Then, see if it is already a perm cert */
            c = NSSTrustDomain_FindCertificateByEncodedCertificate(handle,
                                                                   &encoding);
        }
        if (c) {
            /* actually, that search ends up going by issuer/serial,
             * so it is still possible to return a cert with the same
             * issuer/serial but a different encoding, and we're
             * going to reject that
             */
            if (!nssItem_Equal(&c->encoding, &encoding, NULL)) {
                nssCertificate_Destroy(c);
                PORT_SetError(SEC_ERROR_REUSED_ISSUER_AND_SERIAL);
                cc = NULL;
            } else {
                cc = STAN_GetCERTCertificateOrRelease(c);
                if (cc == NULL) {
                    CERT_MapStanError();
                }
            }
            return cc;
        }
    }
    pkio = nssPKIObject_Create(NULL, NULL, gTD, gCC, nssPKIMonitor);
    if (!pkio) {
        CERT_MapStanError();
        return NULL;
    }
    c = nss_ZNEW(pkio->arena, NSSCertificate);
    if (!c) {
        CERT_MapStanError();
        nssPKIObject_Destroy(pkio);
        return NULL;
    }
    c->object = *pkio;
    if (copyDER) {
        nssItem_Create(c->object.arena, &c->encoding, derCert->len,
                       derCert->data);
    } else {
        NSSITEM_FROM_SECITEM(&c->encoding, derCert);
    }
    /* Forces a decoding of the cert in order to obtain the parts used
     * below
     */
    /* 'c' is not adopted here, if we fail loser frees what has been
     * allocated so far for 'c' */
    cc = STAN_GetCERTCertificate(c);
    if (!cc) {
        CERT_MapStanError();
        goto loser;
    }
    nssItem_Create(c->object.arena, &c->issuer, cc->derIssuer.len,
                   cc->derIssuer.data);
    nssItem_Create(c->object.arena, &c->subject, cc->derSubject.len,
                   cc->derSubject.data);
    /* CERTCertificate stores serial numbers decoded.  I need the DER
     * here.  sigh.
     */
    SECItem derSerial = { 0 };
    CERT_SerialNumberFromDERCert(&cc->derCert, &derSerial);
    if (!derSerial.data)
        goto loser;
    nssItem_Create(c->object.arena, &c->serial, derSerial.len,
                   derSerial.data);
    PORT_Free(derSerial.data);

    if (nssCertificate_TryToSetCertKeyID(c) != PR_SUCCESS) {
        goto loser;
    }

    if (nickname) {
        c->object.tempName =
            nssUTF8_Create(c->object.arena, nssStringType_UTF8String,
                           (NSSUTF8 *)nickname, PORT_Strlen(nickname));
    }
    if (cc->emailAddr && cc->emailAddr[0]) {
        c->email = nssUTF8_Create(
            c->object.arena, nssStringType_PrintableString,
            (NSSUTF8 *)cc->emailAddr, PORT_Strlen(cc->emailAddr));
    }

    tempCert = NSSCryptoContext_FindOrImportCertificate(gCC, c);
    if (!tempCert) {
        CERT_MapStanError();
        goto loser;
    }
    /* destroy our copy */
    NSSCertificate_Destroy(c);
    /* and use the stored entry */
    c = tempCert;
    cc = STAN_GetCERTCertificateOrRelease(c);
    if (!cc) {
        /* STAN_GetCERTCertificateOrRelease destroys c on failure. */
        CERT_MapStanError();
        return NULL;
    }

    CERT_LockCertTempPerm(cc);
    cc->istemp = PR_TRUE;
    cc->isperm = PR_FALSE;
    CERT_UnlockCertTempPerm(cc);
    return cc;
loser:
    nssCertificate_Destroy(c);
    return NULL;
}

/* This symbol is exported for backward compatibility. */
CERTCertificate *
__CERT_NewTempCertificate(CERTCertDBHandle *handle, SECItem *derCert,
                          char *nickname, PRBool isperm, PRBool copyDER)
{
    return CERT_NewTempCertificate(handle, derCert, nickname, isperm, copyDER);
}

static CERTCertificate *
common_FindCertByIssuerAndSN(CERTCertDBHandle *handle,
                             CERTIssuerAndSN *issuerAndSN,
                             void *wincx)
{
    PK11SlotInfo *slot;
    CERTCertificate *cert;

    cert = PK11_FindCertByIssuerAndSN(&slot, issuerAndSN, wincx);
    if (cert && slot) {
        PK11_FreeSlot(slot);
    }

    return cert;
}

/* maybe all the wincx's should be some const for internal token login? */
CERTCertificate *
CERT_FindCertByIssuerAndSN(CERTCertDBHandle *handle,
                           CERTIssuerAndSN *issuerAndSN)
{
    return common_FindCertByIssuerAndSN(handle, issuerAndSN, NULL);
}

/* maybe all the wincx's should be some const for internal token login? */
CERTCertificate *
CERT_FindCertByIssuerAndSNCX(CERTCertDBHandle *handle,
                             CERTIssuerAndSN *issuerAndSN,
                             void *wincx)
{
    return common_FindCertByIssuerAndSN(handle, issuerAndSN, wincx);
}

static NSSCertificate *
get_best_temp_or_perm(NSSCertificate *ct, NSSCertificate *cp)
{
    NSSUsage usage;
    NSSCertificate *arr[3];
    if (!ct) {
        return nssCertificate_AddRef(cp);
    } else if (!cp) {
        return nssCertificate_AddRef(ct);
    }
    arr[0] = ct;
    arr[1] = cp;
    arr[2] = NULL;
    usage.anyUsage = PR_TRUE;
    return nssCertificateArray_FindBestCertificate(arr, NULL, &usage, NULL);
}

CERTCertificate *
CERT_FindCertByName(CERTCertDBHandle *handle, SECItem *name)
{
    NSSCertificate *cp, *ct, *c;
    NSSDER subject;
    NSSUsage usage;
    NSSCryptoContext *cc;
    NSSITEM_FROM_SECITEM(&subject, name);
    usage.anyUsage = PR_TRUE;
    cc = STAN_GetDefaultCryptoContext();
    ct = NSSCryptoContext_FindBestCertificateBySubject(cc, &subject, NULL,
                                                       &usage, NULL);
    cp = NSSTrustDomain_FindBestCertificateBySubject(handle, &subject, NULL,
                                                     &usage, NULL);
    c = get_best_temp_or_perm(ct, cp);
    if (ct) {
        CERT_DestroyCertificate(STAN_GetCERTCertificateOrRelease(ct));
    }
    if (cp) {
        CERT_DestroyCertificate(STAN_GetCERTCertificateOrRelease(cp));
    }
    return c ? STAN_GetCERTCertificateOrRelease(c) : NULL;
}

CERTCertificate *
CERT_FindCertByNickname(CERTCertDBHandle *handle, const char *nickname)
{
    NSSCryptoContext *cc;
    NSSCertificate *c, *ct;
    CERTCertificate *cert;
    NSSUsage usage;
    usage.anyUsage = PR_TRUE;
    cc = STAN_GetDefaultCryptoContext();
    ct = NSSCryptoContext_FindBestCertificateByNickname(cc, nickname, NULL,
                                                        &usage, NULL);
    cert = PK11_FindCertFromNickname(nickname, NULL);
    c = NULL;
    if (cert) {
        c = get_best_temp_or_perm(ct, STAN_GetNSSCertificate(cert));
        CERT_DestroyCertificate(cert);
        if (ct) {
            CERT_DestroyCertificate(STAN_GetCERTCertificateOrRelease(ct));
        }
    } else {
        c = ct;
    }
    return c ? STAN_GetCERTCertificateOrRelease(c) : NULL;
}

CERTCertificate *
CERT_FindCertByDERCert(CERTCertDBHandle *handle, SECItem *derCert)
{
    NSSCryptoContext *cc;
    NSSCertificate *c;
    NSSDER encoding;
    NSSITEM_FROM_SECITEM(&encoding, derCert);
    cc = STAN_GetDefaultCryptoContext();
    c = NSSCryptoContext_FindCertificateByEncodedCertificate(cc, &encoding);
    if (!c) {
        c = NSSTrustDomain_FindCertificateByEncodedCertificate(handle,
                                                               &encoding);
        if (!c)
            return NULL;
    }
    return STAN_GetCERTCertificateOrRelease(c);
}

static CERTCertificate *
common_FindCertByNicknameOrEmailAddrForUsage(CERTCertDBHandle *handle,
                                             const char *name, PRBool anyUsage,
                                             SECCertUsage lookingForUsage,
                                             void *wincx)
{
    NSSCryptoContext *cc;
    NSSCertificate *c, *ct;
    CERTCertificate *cert = NULL;
    NSSUsage usage;
    CERTCertList *certlist;

    if (NULL == name) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    usage.anyUsage = anyUsage;

    if (!anyUsage) {
        usage.nss3lookingForCA = PR_FALSE;
        usage.nss3usage = lookingForUsage;
    }

    cc = STAN_GetDefaultCryptoContext();
    ct = NSSCryptoContext_FindBestCertificateByNickname(cc, name, NULL, &usage,
                                                        NULL);
    if (!ct && PORT_Strchr(name, '@') != NULL) {
        char *lowercaseName = CERT_FixupEmailAddr(name);
        if (lowercaseName) {
            ct = NSSCryptoContext_FindBestCertificateByEmail(
                cc, lowercaseName, NULL, &usage, NULL);
            PORT_Free(lowercaseName);
        }
    }

    if (anyUsage) {
        cert = PK11_FindCertFromNickname(name, wincx);
    } else {
        if (ct) {
            /* Does ct really have the required usage? */
            nssDecodedCert *dc;
            dc = nssCertificate_GetDecoding(ct);
            if (!dc->matchUsage(dc, &usage)) {
                CERT_DestroyCertificate(STAN_GetCERTCertificateOrRelease(ct));
                ct = NULL;
            }
        }

        certlist = PK11_FindCertsFromNickname(name, wincx);
        if (certlist) {
            SECStatus rv =
                CERT_FilterCertListByUsage(certlist, lookingForUsage, PR_FALSE);
            if (SECSuccess == rv && !CERT_LIST_EMPTY(certlist)) {
                cert = CERT_DupCertificate(CERT_LIST_HEAD(certlist)->cert);
            }
            CERT_DestroyCertList(certlist);
        }
    }

    if (cert) {
        c = get_best_temp_or_perm(ct, STAN_GetNSSCertificate(cert));
        CERT_DestroyCertificate(cert);
        if (ct) {
            CERT_DestroyCertificate(STAN_GetCERTCertificateOrRelease(ct));
        }
    } else {
        c = ct;
    }
    return c ? STAN_GetCERTCertificateOrRelease(c) : NULL;
}

CERTCertificate *
CERT_FindCertByNicknameOrEmailAddr(CERTCertDBHandle *handle, const char *name)
{
    return common_FindCertByNicknameOrEmailAddrForUsage(handle, name, PR_TRUE,
                                                        0, NULL);
}

CERTCertificate *
CERT_FindCertByNicknameOrEmailAddrCX(CERTCertDBHandle *handle, const char *name,
                                     void *wincx)
{
    return common_FindCertByNicknameOrEmailAddrForUsage(handle, name, PR_TRUE,
                                                        0, wincx);
}

CERTCertificate *
CERT_FindCertByNicknameOrEmailAddrForUsage(CERTCertDBHandle *handle,
                                           const char *name,
                                           SECCertUsage lookingForUsage)
{
    return common_FindCertByNicknameOrEmailAddrForUsage(handle, name, PR_FALSE,
                                                        lookingForUsage, NULL);
}

CERTCertificate *
CERT_FindCertByNicknameOrEmailAddrForUsageCX(CERTCertDBHandle *handle,
                                             const char *name,
                                             SECCertUsage lookingForUsage,
                                             void *wincx)
{
    return common_FindCertByNicknameOrEmailAddrForUsage(handle, name, PR_FALSE,
                                                        lookingForUsage, wincx);
}

static void
add_to_subject_list(CERTCertList *certList, CERTCertificate *cert,
                    PRBool validOnly, PRTime sorttime)
{
    SECStatus secrv;
    if (!validOnly ||
        CERT_CheckCertValidTimes(cert, sorttime, PR_FALSE) ==
            secCertTimeValid) {
        secrv = CERT_AddCertToListSorted(certList, cert, CERT_SortCBValidity,
                                         (void *)&sorttime);
        if (secrv != SECSuccess) {
            CERT_DestroyCertificate(cert);
        }
    } else {
        CERT_DestroyCertificate(cert);
    }
}

CERTCertList *
CERT_CreateSubjectCertList(CERTCertList *certList, CERTCertDBHandle *handle,
                           const SECItem *name, PRTime sorttime,
                           PRBool validOnly)
{
    NSSCryptoContext *cc;
    NSSCertificate **tSubjectCerts, **pSubjectCerts;
    NSSCertificate **ci;
    CERTCertificate *cert;
    NSSDER subject;
    PRBool myList = PR_FALSE;
    cc = STAN_GetDefaultCryptoContext();
    NSSITEM_FROM_SECITEM(&subject, name);
    /* Collect both temp and perm certs for the subject */
    tSubjectCerts =
        NSSCryptoContext_FindCertificatesBySubject(cc, &subject, NULL, 0, NULL);
    pSubjectCerts = NSSTrustDomain_FindCertificatesBySubject(handle, &subject,
                                                             NULL, 0, NULL);
    if (!tSubjectCerts && !pSubjectCerts) {
        return NULL;
    }
    if (certList == NULL) {
        certList = CERT_NewCertList();
        myList = PR_TRUE;
        if (!certList)
            goto loser;
    }
    /* Iterate over the matching temp certs.  Add them to the list */
    ci = tSubjectCerts;
    while (ci && *ci) {
        cert = STAN_GetCERTCertificateOrRelease(*ci);
        /* *ci may be invalid at this point, don't reference it again */
        if (cert) {
            /* NOTE: add_to_subject_list adopts the incoming cert. */
            add_to_subject_list(certList, cert, validOnly, sorttime);
        }
        ci++;
    }
    /* Iterate over the matching perm certs.  Add them to the list */
    ci = pSubjectCerts;
    while (ci && *ci) {
        cert = STAN_GetCERTCertificateOrRelease(*ci);
        /* *ci may be invalid at this point, don't reference it again */
        if (cert) {
            /* NOTE: add_to_subject_list adopts the incoming cert. */
            add_to_subject_list(certList, cert, validOnly, sorttime);
        }
        ci++;
    }
    /* all the references have been adopted or freed at this point, just
     * free the arrays now */
    nss_ZFreeIf(tSubjectCerts);
    nss_ZFreeIf(pSubjectCerts);
    return certList;
loser:
    /* need to free the references in tSubjectCerts and pSubjectCerts! */
    nssCertificateArray_Destroy(tSubjectCerts);
    nssCertificateArray_Destroy(pSubjectCerts);
    if (myList && certList != NULL) {
        CERT_DestroyCertList(certList);
    }
    return NULL;
}

void
CERT_DestroyCertificate(CERTCertificate *cert)
{
    if (cert) {
        /* don't use STAN_GetNSSCertificate because we don't want to
         * go to the trouble of translating the CERTCertificate into
         * an NSSCertificate just to destroy it.  If it hasn't been done
         * yet, don't do it at all
         *
         * cert->nssCertificate contains its own locks and refcount, but as it
         * may be NULL, the pointer itself must be guarded by some other lock.
         * Rather than creating a new global lock for only this purpose, share
         * an existing global lock that happens to be taken near the write in
         * fill_CERTCertificateFields(). The longer-term goal is to refactor
         * all these global locks to be certificate-scoped. */
        CERT_MaybeLockCertTempPerm(cert);
        NSSCertificate *tmp = cert->nssCertificate;
        CERT_MaybeUnlockCertTempPerm(cert);
        if (tmp) {
            /* delete the NSSCertificate */
            NSSCertificate_Destroy(tmp);
        } else if (cert->arena) {
            PORT_FreeArena(cert->arena, PR_FALSE);
        }
    }
    return;
}

int
CERT_GetDBContentVersion(CERTCertDBHandle *handle)
{
    /* should read the DB content version from the pkcs #11 device */
    return 0;
}

SECStatus
certdb_SaveSingleProfile(CERTCertificate *cert, const char *emailAddr,
                         SECItem *emailProfile, SECItem *profileTime)
{
    PRTime oldtime;
    PRTime newtime;
    SECStatus rv = SECFailure;
    PRBool saveit;
    SECItem oldproftime;
    SECItem *oldProfileTime = NULL;
    PRBool freeOldProfileTime = PR_FALSE;
    PK11SlotInfo *slot = NULL;
    NSSCertificate *c;
    NSSCryptoContext *cc;
    nssSMIMEProfile *stanProfile = NULL;

    c = STAN_GetNSSCertificate(cert);
    if (!c) {
        return SECFailure;
    }

    nssPKIObject_Lock(&c->object);
    cc = c->object.cryptoContext;
    nssPKIObject_Unlock(&c->object);
    if (cc != NULL) {
        stanProfile = nssCryptoContext_FindSMIMEProfileForCertificate(cc, c);
        if (stanProfile) {
            nssPKIObject_Lock(&stanProfile->object);
            PORT_Assert(stanProfile->profileTime);
            SECITEM_FROM_NSSITEM(&oldproftime, stanProfile->profileTime);
            oldProfileTime = &oldproftime;
            nssPKIObject_Unlock(&stanProfile->object);
        }
    } else {
        /* PK11_FindSMimeProfile returns the profile data in addition to the
         * profile time. We do not need the data below. */
        SECItem *oldProfile = PK11_FindSMimeProfile(
            &slot, (char *)emailAddr, &cert->derSubject, &oldProfileTime);

        SECITEM_FreeItem(oldProfile, PR_TRUE);
        freeOldProfileTime = PR_TRUE;
    }

    saveit = PR_FALSE;

    /* both profileTime and emailProfile have to exist or not exist */
    if (emailProfile == NULL) {
        profileTime = NULL;
    } else if (profileTime == NULL) {
        emailProfile = NULL;
    }

    if (oldProfileTime == NULL) {
        saveit = PR_TRUE;
    } else {
        /* there was already a profile for this email addr */
        if (profileTime) {
            /* we have an old and new profile - save whichever is more recent*/
            if (oldProfileTime->len == 0) {
                /* always replace if old entry doesn't have a time */
                oldtime = LL_MININT;
            } else {
                rv = DER_UTCTimeToTime(&oldtime, oldProfileTime);
                if (rv != SECSuccess) {
                    goto loser;
                }
            }

            rv = DER_UTCTimeToTime(&newtime, profileTime);
            if (rv != SECSuccess) {
                goto loser;
            }

            if (LL_CMP(newtime, >, oldtime)) {
                /* this is a newer profile, save it and cert */
                saveit = PR_TRUE;
            }
        } else {
            saveit = PR_TRUE;
        }
    }

    if (saveit) {
        if (cc) {
            if (stanProfile && profileTime && emailProfile) {
                /* stanProfile is already stored in the crypto context,
                 * overwrite the data
                 */
                NSSArena *arena = stanProfile->object.arena;
                nssPKIObject_Lock(&stanProfile->object);
                stanProfile->profileTime = nssItem_Create(
                    arena, NULL, profileTime->len, profileTime->data);
                stanProfile->profileData = nssItem_Create(
                    arena, NULL, emailProfile->len, emailProfile->data);
                nssPKIObject_Unlock(&stanProfile->object);
            } else if (profileTime && emailProfile) {
                PRStatus nssrv;
                NSSItem profTime, profData;
                NSSITEM_FROM_SECITEM(&profTime, profileTime);
                NSSITEM_FROM_SECITEM(&profData, emailProfile);
                stanProfile = nssSMIMEProfile_Create(c, &profTime, &profData);
                if (!stanProfile)
                    goto loser;
                nssrv = nssCryptoContext_ImportSMIMEProfile(cc, stanProfile);
                rv = (nssrv == PR_SUCCESS) ? SECSuccess : SECFailure;
            }
        } else {
            rv = PK11_SaveSMimeProfile(slot, (char *)emailAddr,
                                       &cert->derSubject, emailProfile,
                                       profileTime);
        }
    } else {
        rv = SECSuccess;
    }

loser:
    if (freeOldProfileTime) {
        SECITEM_FreeItem(oldProfileTime, PR_TRUE);
    }
    if (stanProfile) {
        nssSMIMEProfile_Destroy(stanProfile);
    }
    if (slot) {
        PK11_FreeSlot(slot);
    }

    return (rv);
}

/*
 *
 * Manage S/MIME profiles
 *
 */

SECStatus
CERT_SaveSMimeProfile(CERTCertificate *cert, SECItem *emailProfile,
                      SECItem *profileTime)
{
    const char *emailAddr;
    SECStatus rv;
    PRBool isperm = PR_FALSE;

    if (!cert) {
        return SECFailure;
    }

    if (cert->slot && !PK11_IsInternal(cert->slot)) {
        /* this cert comes from an external source, we need to add it
        to the cert db before creating an S/MIME profile */
        PK11SlotInfo *internalslot = PK11_GetInternalKeySlot();
        if (!internalslot) {
            return SECFailure;
        }
        rv = PK11_ImportCert(internalslot, cert, CK_INVALID_HANDLE, NULL,
                             PR_FALSE);

        PK11_FreeSlot(internalslot);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    rv = CERT_GetCertIsPerm(cert, &isperm);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    if (cert->slot && isperm && CERT_IsUserCert(cert) &&
        (!emailProfile || !emailProfile->len)) {
        /* Don't clobber emailProfile for user certs. */
        return SECSuccess;
    }

    for (emailAddr = CERT_GetFirstEmailAddress(cert); emailAddr != NULL;
         emailAddr = CERT_GetNextEmailAddress(cert, emailAddr)) {
        rv = certdb_SaveSingleProfile(cert, emailAddr, emailProfile,
                                      profileTime);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }
    return SECSuccess;
}

SECItem *
CERT_FindSMimeProfile(CERTCertificate *cert)
{
    PK11SlotInfo *slot = NULL;
    NSSCertificate *c;
    NSSCryptoContext *cc;
    SECItem *rvItem = NULL;

    if (!cert || !cert->emailAddr || !cert->emailAddr[0]) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }
    c = STAN_GetNSSCertificate(cert);
    if (!c) {
        return NULL;
    }

    nssPKIObject_Lock(&c->object);
    cc = c->object.cryptoContext;
    nssPKIObject_Unlock(&c->object);
    if (cc != NULL) {
        nssSMIMEProfile *stanProfile;
        stanProfile = nssCryptoContext_FindSMIMEProfileForCertificate(cc, c);
        if (stanProfile) {
            nssPKIObject_Lock(&stanProfile->object);
            if (stanProfile->profileData) {
                SECItem profile = { siBuffer, NULL, 0 };
                SECITEM_FROM_NSSITEM(&profile, stanProfile->profileData);
                rvItem = SECITEM_DupItem(&profile);
            }
            nssPKIObject_Unlock(&stanProfile->object);
            nssSMIMEProfile_Destroy(stanProfile);
        }
    } else {
        rvItem =
            PK11_FindSMimeProfile(&slot, cert->emailAddr, &cert->derSubject, NULL);
        if (slot) {
            PK11_FreeSlot(slot);
        }
    }
    return rvItem;
}

SECStatus
CERT_GetCertIsPerm(const CERTCertificate *cert, PRBool *isperm)
{
    if (cert == NULL) {
        return SECFailure;
    }

    CERT_LockCertTempPerm(cert);
    *isperm = cert->isperm;
    CERT_UnlockCertTempPerm(cert);
    return SECSuccess;
}

SECStatus
CERT_GetCertIsTemp(const CERTCertificate *cert, PRBool *istemp)
{
    if (cert == NULL) {
        return SECFailure;
    }

    CERT_LockCertTempPerm(cert);
    *istemp = cert->istemp;
    CERT_UnlockCertTempPerm(cert);
    return SECSuccess;
}

/*
 * deprecated functions that are now just stubs.
 */
/*
 * Close the database
 */
void
__CERT_ClosePermCertDB(CERTCertDBHandle *handle)
{
    PORT_Assert("CERT_ClosePermCertDB is Deprecated" == NULL);
    return;
}

SECStatus
CERT_OpenCertDBFilename(CERTCertDBHandle *handle, char *certdbname,
                        PRBool readOnly)
{
    PORT_Assert("CERT_OpenCertDBFilename is Deprecated" == NULL);
    PORT_SetError(PR_NOT_IMPLEMENTED_ERROR);
    return SECFailure;
}

SECItem *
SECKEY_HashPassword(char *pw, SECItem *salt)
{
    PORT_Assert("SECKEY_HashPassword is Deprecated" == NULL);
    PORT_SetError(PR_NOT_IMPLEMENTED_ERROR);
    return NULL;
}

SECStatus
__CERT_TraversePermCertsForSubject(CERTCertDBHandle *handle,
                                   SECItem *derSubject, void *cb, void *cbarg)
{
    PORT_Assert("CERT_TraversePermCertsForSubject is Deprecated" == NULL);
    PORT_SetError(PR_NOT_IMPLEMENTED_ERROR);
    return SECFailure;
}

SECStatus
__CERT_TraversePermCertsForNickname(CERTCertDBHandle *handle, char *nickname,
                                    void *cb, void *cbarg)
{
    PORT_Assert("CERT_TraversePermCertsForNickname is Deprecated" == NULL);
    PORT_SetError(PR_NOT_IMPLEMENTED_ERROR);
    return SECFailure;
}
