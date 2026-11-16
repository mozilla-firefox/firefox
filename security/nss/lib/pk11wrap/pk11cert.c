/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * This file manages PKCS #11 instances of certificates.
 */

#include <stddef.h>

#include "secport.h"
#include "seccomon.h"
#include "secmod.h"
#include "secmodi.h"
#include "secmodti.h"
#include "pkcs11.h"
#include "pk11func.h"
#include "cert.h"
#include "certi.h"
#include "secitem.h"
#include "keyhi.h"
#include "secoid.h"
#include "pkcs7t.h"
#include "cmsreclist.h"

#include "certdb.h"
#include "secerr.h"
#include "sslerr.h"

#include "pki3hack.h"
#include "dev3hack.h"

#include "devm.h"
#include "nsspki.h"
#include "pki.h"
#include "pkim.h"
#include "pkitm.h"
#include "pkistore.h" /* to remove temp cert */
#include "devt.h"
#include "ckhelper.h"
#include "pkcs11uri.h"

extern const NSSError NSS_ERROR_NOT_FOUND;
extern const NSSError NSS_ERROR_INVALID_CERTIFICATE;

struct nss3_cert_cbstr {
    SECStatus (*callback)(CERTCertificate *, void *);
    nssList *cached;
    void *arg;
};

/* Translate from NSSCertificate to CERTCertificate, then pass the latter
 * to a callback.
 */
static PRStatus
convert_cert(NSSCertificate *c, void *arg)
{
    CERTCertificate *nss3cert;
    SECStatus secrv;
    struct nss3_cert_cbstr *nss3cb = (struct nss3_cert_cbstr *)arg;
    /* 'c' is not adopted. caller will free it */
    nss3cert = STAN_GetCERTCertificate(c);
    if (!nss3cert)
        return PR_FAILURE;
    secrv = (*nss3cb->callback)(nss3cert, nss3cb->arg);
    return (secrv) ? PR_FAILURE : PR_SUCCESS;
}

/*
 * build a cert nickname based on the token name and the label of the
 * certificate If the label in NULL, build a label based on the ID.
 */
static int
toHex(int x)
{
    return (x < 10) ? (x + '0') : (x + 'a' - 10);
}
#define MAX_CERT_ID 4
#define DEFAULT_STRING "Cert ID "
static char *
pk11_buildNickname(PK11SlotInfo *slot, CK_ATTRIBUTE *cert_label,
                   CK_ATTRIBUTE *key_label, CK_ATTRIBUTE *cert_id)
{
    int prefixLen = PORT_Strlen(slot->token_name);
    int suffixLen = 0;
    char *suffix = NULL;
    char buildNew[sizeof(DEFAULT_STRING) + MAX_CERT_ID * 2];
    char *next, *nickname;

    if (cert_label && (cert_label->ulValueLen)) {
        suffixLen = cert_label->ulValueLen;
        suffix = (char *)cert_label->pValue;
    } else if (key_label && (key_label->ulValueLen)) {
        suffixLen = key_label->ulValueLen;
        suffix = (char *)key_label->pValue;
    } else if (cert_id && cert_id->ulValueLen > 0) {
        char *idValue = (char *)cert_id->pValue;
        /* Hex-encode the last min(MAX_CERT_ID, ulValueLen) bytes of cert_id. */
        CK_ULONG idLen = (cert_id->ulValueLen > MAX_CERT_ID)
                             ? MAX_CERT_ID
                             : cert_id->ulValueLen;
        CK_ULONG first = cert_id->ulValueLen - idLen;
        CK_ULONG i;

        PORT_Memcpy(buildNew, DEFAULT_STRING, sizeof(DEFAULT_STRING) - 1);
        next = buildNew + sizeof(DEFAULT_STRING) - 1;
        for (i = 0; i < idLen; i++) {
            *next++ = toHex((idValue[first + i] >> 4) & 0xf);
            *next++ = toHex(idValue[first + i] & 0xf);
        }
        *next++ = 0;
        suffix = buildNew;
        suffixLen = PORT_Strlen(buildNew);
    } else {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return NULL;
    }

    /* if is internal key slot, add code to skip the prefix!! */
    next = nickname = (char *)PORT_Alloc(prefixLen + 1 + suffixLen + 1);
    if (nickname == NULL)
        return NULL;

    PORT_Memcpy(next, slot->token_name, prefixLen);
    next += prefixLen;
    *next++ = ':';
    PORT_Memcpy(next, suffix, suffixLen);
    next += suffixLen;
    *next++ = 0;
    return nickname;
}

PRBool
PK11_IsUserCert(PK11SlotInfo *slot, CERTCertificate *cert,
                CK_OBJECT_HANDLE certID)
{
    CK_OBJECT_CLASS theClass;

    if (slot == NULL)
        return PR_FALSE;
    if (cert == NULL)
        return PR_FALSE;

    theClass = CKO_PRIVATE_KEY;
    if (pk11_LoginStillRequired(slot, NULL)) {
        theClass = CKO_PUBLIC_KEY;
    }
    if (PK11_MatchItem(slot, certID, theClass) != CK_INVALID_HANDLE) {
        return PR_TRUE;
    }

    if (theClass == CKO_PUBLIC_KEY) {
        SECKEYPublicKey *pubKey = CERT_ExtractPublicKey(cert);
        CK_ATTRIBUTE theTemplate;
        PRBool needUnsignedAdjust = PR_FALSE;

        if (pubKey == NULL) {
            return PR_FALSE;
        }

        PK11_SETATTRS(&theTemplate, 0, NULL, 0);
        switch (pubKey->keyType) {
            case rsaKey:
            case rsaPssKey:
            case rsaOaepKey:
                PK11_SETATTRS(&theTemplate, CKA_MODULUS, pubKey->u.rsa.modulus.data,
                              pubKey->u.rsa.modulus.len);
                needUnsignedAdjust = PR_TRUE;
                break;
            case dsaKey:
                PK11_SETATTRS(&theTemplate, CKA_VALUE, pubKey->u.dsa.publicValue.data,
                              pubKey->u.dsa.publicValue.len);
                needUnsignedAdjust = PR_TRUE;
                break;
            case dhKey:
                PK11_SETATTRS(&theTemplate, CKA_VALUE, pubKey->u.dh.publicValue.data,
                              pubKey->u.dh.publicValue.len);
                needUnsignedAdjust = PR_TRUE;
                break;
            case ecKey:
            case edKey:
            case ecMontKey:
                PK11_SETATTRS(&theTemplate, CKA_EC_POINT,
                              pubKey->u.ec.publicValue.data,
                              pubKey->u.ec.publicValue.len);
                break;
            case mldsaKey:
                PK11_SETATTRS(&theTemplate, CKA_VALUE,
                              pubKey->u.mldsa.publicValue.data,
                              pubKey->u.mldsa.publicValue.len);
                break;
            case kyberKey:
                PK11_SETATTRS(&theTemplate, CKA_VALUE,
                              pubKey->u.kyber.publicValue.data,
                              pubKey->u.kyber.publicValue.len);
                break;
            case keaKey:
            case fortezzaKey:
            case nullKey:
                /* fall through and return false */
                break;
        }

        if (theTemplate.ulValueLen == 0) {
            SECKEY_DestroyPublicKey(pubKey);
            return PR_FALSE;
        }
        if (needUnsignedAdjust) {
            pk11_SignedToUnsigned(&theTemplate);
        }
        if (pk11_FindObjectByTemplate(slot, &theTemplate, 1) != CK_INVALID_HANDLE) {
            SECKEY_DestroyPublicKey(pubKey);
            return PR_TRUE;
        }
        SECKEY_DestroyPublicKey(pubKey);
    }
    return PR_FALSE;
}

/*
 * Check out if a cert has ID of zero. This is a magic ID that tells
 * NSS that this cert may be an automagically trusted cert.
 * The Cert has to be self signed as well. That check is done elsewhere.
 *
 */
PRBool
pk11_isID0(PK11SlotInfo *slot, CK_OBJECT_HANDLE certID)
{
    CK_ATTRIBUTE keyID = { CKA_ID, NULL, 0 };
    PRBool isZero = PR_FALSE;
    int i;
    CK_RV crv;

    crv = PK11_GetAttributes(NULL, slot, certID, &keyID, 1);
    if (crv != CKR_OK) {
        return isZero;
    }

    if (keyID.ulValueLen != 0) {
        char *value = (char *)keyID.pValue;
        isZero = PR_TRUE; /* ID exists, may be zero */
        for (i = 0; i < (int)keyID.ulValueLen; i++) {
            if (value[i] != 0) {
                isZero = PR_FALSE; /* nope */
                break;
            }
        }
    }
    PORT_Free(keyID.pValue);
    return isZero;
}

/*
 * Create an NSSCertificate from a slot/certID pair, return it as a
 * CERTCertificate.  Optionally, output the nickname string.
 */
static CERTCertificate *
pk11_fastCert(PK11SlotInfo *slot, CK_OBJECT_HANDLE certID,
              CK_ATTRIBUTE *privateLabel, char **nickptr)
{
    NSSCertificate *c;
    nssCryptokiObject *co = NULL;
    nssPKIObject *pkio;
    NSSTrustDomain *td = STAN_GetDefaultTrustDomain();

    /* Get the cryptoki object from the handle */
    NSSToken *token = PK11Slot_GetNSSToken(slot);
    if (!token || !token->defaultSession) {
        (void)nssToken_Destroy(token); /* null token is ok */
        PORT_SetError(SEC_ERROR_NO_TOKEN);
        return NULL;
    }
    co = nssCryptokiObject_Create(token, token->defaultSession, certID);
    (void)nssToken_Destroy(token);
    if (!co) {
        return NULL;
    }

    /* Create a PKI object from the cryptoki instance */
    pkio = nssPKIObject_Create(NULL, co, td, NULL, nssPKIMonitor);
    if (!pkio) {
        nssCryptokiObject_Destroy(co);
        return NULL;
    }

    /* Create a certificate */
    c = nssCertificate_Create(pkio);
    if (!c) {
        nssPKIObject_Destroy(pkio);
        return NULL;
    }

    /* Build and output a nickname, if desired.
     * This must be done before calling nssTrustDomain_AddCertToCache
     * because that function may destroy c, pkio and co!
     */
    if ((nickptr) && (co->label)) {
        CK_ATTRIBUTE label, id;

        label.type = CKA_LABEL;
        label.pValue = co->label;
        label.ulValueLen = PORT_Strlen(co->label);

        id.type = CKA_ID;
        id.pValue = c->id.data;
        id.ulValueLen = c->id.size;

        *nickptr = pk11_buildNickname(slot, &label, privateLabel, &id);
    }

    // nssTrustDomain_AddCertToCache takes ownership of 'c' and returns an
    // owned reference to an NSSCertificate that may be a different object from
    // 'c'. Presumably, the nickname which we just output above remains valid
    // (the memory is valid because pk11_buildNickname allocates memory for it
    // on the heap).
    c = nssTrustDomain_AddCertToCache(td, c);
    if (!c) {
        return NULL;
    }
    return STAN_GetCERTCertificateOrRelease(c);
}

/*
 * Build an CERTCertificate structure from a PKCS#11 object ID.... certID
 * Must be a CertObject. This code does not explicitly checks that.
 */
CERTCertificate *
PK11_MakeCertFromHandle(PK11SlotInfo *slot, CK_OBJECT_HANDLE certID,
                        CK_ATTRIBUTE *privateLabel)
{
    char *nickname = NULL;
    CERTCertificate *cert = NULL;
    CERTCertTrust *trust;

    if (slot == NULL || certID == CK_INVALID_HANDLE) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    cert = pk11_fastCert(slot, certID, privateLabel, &nickname);
    if (cert == NULL) {
        goto loser;
    }

    if (nickname) {
        if (cert->nickname != NULL) {
            cert->dbnickname = cert->nickname;
        }
        cert->nickname = PORT_ArenaStrdup(cert->arena, nickname);
        PORT_Free(nickname);
        nickname = NULL;
    }

    /* remember where this cert came from.... If we have just looked
     * it up from the database and it already has a slot, don't add a new
     * one. */
    if (cert->slot == NULL) {
        cert->slot = PK11_ReferenceSlot(slot);
        cert->pkcs11ID = certID;
        cert->ownSlot = PR_TRUE;
        cert->series = slot->series;
    }

    trust = (CERTCertTrust *)PORT_ArenaAlloc(cert->arena, sizeof(CERTCertTrust));
    if (trust == NULL)
        goto loser;
    PORT_Memset(trust, 0, sizeof(CERTCertTrust));

    if (!pk11_HandleTrustObject(slot, cert, trust)) {
        unsigned int type;

        /* build some cert trust flags */
        if (CERT_IsCACert(cert, &type)) {
            unsigned int trustflags = CERTDB_VALID_CA;

            /* Allow PKCS #11 modules to give us trusted CA's. We only accept
             * valid CA's which are self-signed here. They must have an object
             * ID of '0'.  */
            if (pk11_isID0(slot, certID) &&
                cert->isRoot) {
                trustflags |= CERTDB_TRUSTED_CA;
                /* is the slot a fortezza card? allow the user or
                 * admin to turn on objectSigning, but don't turn
                 * full trust on explicitly */
                if (PK11_DoesMechanism(slot, CKM_KEA_KEY_DERIVE)) {
                    trust->objectSigningFlags |= CERTDB_VALID_CA;
                }
            }
            if ((type & NS_CERT_TYPE_SSL_CA) == NS_CERT_TYPE_SSL_CA) {
                trust->sslFlags |= trustflags;
            }
            if ((type & NS_CERT_TYPE_EMAIL_CA) == NS_CERT_TYPE_EMAIL_CA) {
                trust->emailFlags |= trustflags;
            }
            if ((type & NS_CERT_TYPE_OBJECT_SIGNING_CA) == NS_CERT_TYPE_OBJECT_SIGNING_CA) {
                trust->objectSigningFlags |= trustflags;
            }
        }
    }

    if (PK11_IsUserCert(slot, cert, certID)) {
        trust->sslFlags |= CERTDB_USER;
        trust->emailFlags |= CERTDB_USER;
        /*    trust->objectSigningFlags |= CERTDB_USER; */
    }
    CERT_LockCertTrust(cert);
    cert->trust = trust;
    CERT_UnlockCertTrust(cert);

    return cert;

loser:
    if (nickname)
        PORT_Free(nickname);
    if (cert)
        CERT_DestroyCertificate(cert);
    return NULL;
}

/*
 * Build get a certificate from a private key
 */
CERTCertificate *
PK11_GetCertFromPrivateKey(SECKEYPrivateKey *privKey)
{
    PK11SlotInfo *slot = privKey->pkcs11Slot;
    CK_OBJECT_HANDLE handle = privKey->pkcs11ID;
    CK_OBJECT_HANDLE certID =
        PK11_MatchItem(slot, handle, CKO_CERTIFICATE);
    CERTCertificate *cert;

    if (certID == CK_INVALID_HANDLE) {
        PORT_SetError(SSL_ERROR_NO_CERTIFICATE);
        return NULL;
    }
    cert = PK11_MakeCertFromHandle(slot, certID, NULL);
    return (cert);
}

CK_OBJECT_HANDLE *
PK11_FindCertHandlesForKeyHandle(PK11SlotInfo *slot, CK_OBJECT_HANDLE keyHandle,
                                 int *certHandleCountOut)
{
    if (!slot || !certHandleCountOut || keyHandle == CK_INVALID_HANDLE) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    PORTCheapArenaPool arena;
    PORT_InitCheapArena(&arena, DER_DEFAULT_CHUNKSIZE);
    CK_ATTRIBUTE idTemplate[] = {
        { CKA_ID, NULL, 0 },
    };
    const int idAttrCount = sizeof(idTemplate) / sizeof(idTemplate[0]);
    CK_RV crv = PK11_GetAttributes(&arena.arena, slot, keyHandle, idTemplate, idAttrCount);
    if (crv != CKR_OK) {
        PORT_DestroyCheapArena(&arena);
        PORT_SetError(PK11_MapError(crv));
        return NULL;
    }

    if ((idTemplate[0].ulValueLen == 0) || (idTemplate[0].ulValueLen == -1)) {
        PORT_DestroyCheapArena(&arena);
        PORT_SetError(SEC_ERROR_BAD_KEY);
        return NULL;
    }

    CK_OBJECT_CLASS searchClass = CKO_CERTIFICATE;
    CK_ATTRIBUTE searchTemplate[] = {
        idTemplate[0],
        { CKA_CLASS, &searchClass, sizeof(searchClass) }
    };
    const size_t searchAttrCount = sizeof(searchTemplate) / sizeof(searchTemplate[0]);
    CK_OBJECT_HANDLE *ids = pk11_FindObjectsByTemplate(slot, searchTemplate, searchAttrCount, certHandleCountOut);

    PORT_DestroyCheapArena(&arena);
    return ids;
}

CERTCertList *
PK11_GetCertsMatchingPrivateKey(SECKEYPrivateKey *privKey)
{
    if (!privKey) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }
    CERTCertList *certs = CERT_NewCertList();
    if (!certs) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return NULL;
    }
    PK11SlotInfo *slot = privKey->pkcs11Slot;
    CK_OBJECT_HANDLE handle = privKey->pkcs11ID;
    CK_OBJECT_HANDLE certID = PK11_MatchItem(slot, handle, CKO_CERTIFICATE);
    /* If we can't get a matching certID, there are no matching certificates,
     * which is not an error. */
    if (certID == CK_INVALID_HANDLE) {
        return certs;
    }
    int certHandleCount = 0;
    CK_OBJECT_HANDLE *certHandles = PK11_FindCertHandlesForKeyHandle(slot, handle, &certHandleCount);
    if (!certHandles) {
        /* If certHandleCount is 0, there are no matching certificates, which is
         * not an error. */
        if (certHandleCount == 0) {
            return certs;
        }
        CERT_DestroyCertList(certs);
        return NULL;
    }
    int i;
    for (i = 0; i < certHandleCount; i++) {
        CERTCertificate *cert = PK11_MakeCertFromHandle(slot, certHandles[i], NULL);
        /* If PK11_MakeCertFromHandle fails for one handle, optimistically
           assume other handles may succeed (i.e. this is best-effort). */
        if (!cert) {
            continue;
        }
        if (CERT_AddCertToListTail(certs, cert) != SECSuccess) {
            CERT_DestroyCertificate(cert);
        }
    }
    PORT_Free(certHandles);
    return certs;
}

/*
 * delete a cert and it's private key (if no other certs are pointing to the
 * private key.
 */
SECStatus
PK11_DeleteTokenCertAndKey(CERTCertificate *cert, void *wincx)
{
    SECKEYPrivateKey *privKey = PK11_FindKeyByAnyCert(cert, wincx);
    CK_OBJECT_HANDLE pubKey;
    PK11SlotInfo *slot = NULL;

    pubKey = pk11_FindPubKeyByAnyCert(cert, &slot, wincx);
    if (privKey) {
        /* For 3.4, utilize the generic cert delete function */
        SEC_DeletePermCertificate(cert);
        PK11_DeleteTokenPrivateKey(privKey, PR_FALSE);
    }
    if ((pubKey != CK_INVALID_HANDLE) && (slot != NULL)) {
        PK11_DestroyTokenObject(slot, pubKey);
        PK11_FreeSlot(slot);
    }
    return SECSuccess;
}

/*
 * cert callback structure
 */
typedef struct pk11DoCertCallbackStr {
    SECStatus (*callback)(PK11SlotInfo *slot, CERTCertificate *, void *);
    SECStatus (*noslotcallback)(CERTCertificate *, void *);
    SECStatus (*itemcallback)(CERTCertificate *, SECItem *, void *);
    void *callbackArg;
} pk11DoCertCallback;

typedef struct pk11CertCallbackStr {
    SECStatus (*callback)(CERTCertificate *, SECItem *, void *);
    void *callbackArg;
} pk11CertCallback;

struct fake_der_cb_argstr {
    SECStatus (*callback)(CERTCertificate *, SECItem *, void *);
    void *arg;
};

static SECStatus
fake_der_cb(CERTCertificate *c, void *a)
{
    struct fake_der_cb_argstr *fda = (struct fake_der_cb_argstr *)a;
    return (*fda->callback)(c, &c->derCert, fda->arg);
}

/*
 * Extract all the certs on a card from a slot.
 */
SECStatus
PK11_TraverseSlotCerts(SECStatus (*callback)(CERTCertificate *, SECItem *, void *),
                       void *arg, void *wincx)
{
    NSSTrustDomain *defaultTD = STAN_GetDefaultTrustDomain();
    struct fake_der_cb_argstr fda;
    struct nss3_cert_cbstr pk11cb;

    /* authenticate to the tokens first */
    (void)pk11_TraverseAllSlots(NULL, NULL, PR_TRUE, wincx);

    fda.callback = callback;
    fda.arg = arg;
    pk11cb.callback = fake_der_cb;
    pk11cb.arg = &fda;
    NSSTrustDomain_TraverseCertificates(defaultTD, convert_cert, &pk11cb);
    return SECSuccess;
}

static void
transfer_token_certs_to_collection(nssList *certList, NSSToken *token,
                                   nssPKIObjectCollection *collection)
{
    NSSCertificate **certs;
    PRUint32 i, count;
    NSSToken **tokens, **tp;
    count = nssList_Count(certList);
    if (count == 0) {
        return;
    }
    certs = nss_ZNEWARRAY(NULL, NSSCertificate *, count);
    if (!certs) {
        return;
    }
    nssList_GetArray(certList, (void **)certs, count);
    for (i = 0; i < count; i++) {
        tokens = nssPKIObject_GetTokens(&certs[i]->object, NULL);
        if (tokens) {
            for (tp = tokens; *tp; tp++) {
                if (*tp == token) {
                    nssPKIObjectCollection_AddObject(collection,
                                                     (nssPKIObject *)certs[i]);
                }
            }
            nssTokenArray_Destroy(tokens);
        }
        CERT_DestroyCertificate(STAN_GetCERTCertificateOrRelease(certs[i]));
    }
    nss_ZFreeIf(certs);
}

static void
transfer_uri_certs_to_collection(nssList *certList, PK11URI *uri,
                                 nssPKIObjectCollection *collection)
{

    NSSCertificate **certs;
    PRUint32 i, count;
    NSSToken **tokens, **tp;
    PK11SlotInfo *slot;
    const SECItem *id;

    count = nssList_Count(certList);
    if (count == 0) {
        return;
    }
    certs = nss_ZNEWARRAY(NULL, NSSCertificate *, count);
    if (!certs) {
        return;
    }
    id = PK11URI_GetPathAttributeItem(uri, PK11URI_PATTR_ID);
    nssList_GetArray(certList, (void **)certs, count);
    for (i = 0; i < count; i++) {
        /*
         * Filter the subject matched certs based on the
         * CKA_ID from the URI
         */
        if (id && (id->len != certs[i]->id.size ||
                   memcmp(id->data, certs[i]->id.data, certs[i]->id.size))) {
            CERT_DestroyCertificate(STAN_GetCERTCertificateOrRelease(certs[i]));
            continue;
        }
        tokens = nssPKIObject_GetTokens(&certs[i]->object, NULL);
        if (tokens) {
            for (tp = tokens; *tp; tp++) {
                const char *value;
                slot = (*tp)->pk11slot;

                value = PK11URI_GetPathAttribute(uri, PK11URI_PATTR_TOKEN);
                if (value &&
                    !pk11_MatchString(value,
                                      (char *)slot->tokenInfo.label,
                                      sizeof(slot->tokenInfo.label))) {
                    continue;
                }

                value = PK11URI_GetPathAttribute(uri, PK11URI_PATTR_MANUFACTURER);
                if (value &&
                    !pk11_MatchString(value,
                                      (char *)slot->tokenInfo.manufacturerID,
                                      sizeof(slot->tokenInfo.manufacturerID))) {
                    continue;
                }

                value = PK11URI_GetPathAttribute(uri, PK11URI_PATTR_MODEL);
                if (value &&
                    !pk11_MatchString(value,
                                      (char *)slot->tokenInfo.model,
                                      sizeof(slot->tokenInfo.model))) {
                    continue;
                }

                value = PK11URI_GetPathAttribute(uri, PK11URI_PATTR_SERIAL);
                if (value &&
                    !pk11_MatchString(value,
                                      (char *)slot->tokenInfo.serialNumber,
                                      sizeof(slot->tokenInfo.serialNumber))) {
                    continue;
                }

                nssPKIObjectCollection_AddObject(collection,
                                                 (nssPKIObject *)certs[i]);
                break;
            }
            nssTokenArray_Destroy(tokens);
        }
        CERT_DestroyCertificate(STAN_GetCERTCertificateOrRelease(certs[i]));
    }
    nss_ZFreeIf(certs);
}

static NSSCertificate **
find_certs_from_uri(const char *uriString, void *wincx)
{
    PK11URI *uri = NULL;
    CK_ATTRIBUTE attributes[10];
    CK_ULONG nattributes = 0;
    const SECItem *id;
    const char *label, *type;
    PK11SlotInfo *slotinfo;
    nssCryptokiObject **instances;
    PRStatus status;
    nssPKIObjectCollection *collection = NULL;
    NSSTrustDomain *defaultTD = STAN_GetDefaultTrustDomain();
    NSSCertificate **certs = NULL;
    nssList *certList = NULL;
    SECStatus rv;
    CK_OBJECT_CLASS s_class = CKO_CERTIFICATE;
    static const CK_BBOOL s_true = CK_TRUE;
    NSSToken **tokens, **tok;

    uri = PK11URI_ParseURI(uriString);
    if (uri == NULL) {
        goto loser;
    }

    collection = nssCertificateCollection_Create(defaultTD, NULL);
    if (!collection) {
        goto loser;
    }
    certList = nssList_Create(NULL, PR_FALSE);
    if (!certList) {
        goto loser;
    }

    /* if the "type" attribute is specified its value must be "cert" */
    type = PK11URI_GetPathAttribute(uri, PK11URI_PATTR_TYPE);
    if (type && strcmp(type, "cert")) {
        goto loser;
    }

    label = PK11URI_GetPathAttribute(uri, PK11URI_PATTR_OBJECT);
    if (label) {
        (void)nssTrustDomain_GetCertsForNicknameFromCache(defaultTD,
                                                          label,
                                                          certList);
    } else {
        (void)nssTrustDomain_GetCertsFromCache(defaultTD, certList);
    }

    transfer_uri_certs_to_collection(certList, uri, collection);

    /* add the CKA_CLASS and CKA_TOKEN attributes manually */
    attributes[nattributes].type = CKA_CLASS;
    attributes[nattributes].pValue = (void *)&s_class;
    attributes[nattributes].ulValueLen = sizeof(s_class);
    nattributes++;

    attributes[nattributes].type = CKA_TOKEN;
    attributes[nattributes].pValue = (void *)&s_true;
    attributes[nattributes].ulValueLen = sizeof(s_true);
    nattributes++;

    if (label) {
        attributes[nattributes].type = CKA_LABEL;
        attributes[nattributes].pValue = (void *)label;
        attributes[nattributes].ulValueLen = strlen(label);
        nattributes++;
    }

    id = PK11URI_GetPathAttributeItem(uri, PK11URI_PATTR_ID);
    if (id) {
        attributes[nattributes].type = CKA_ID;
        attributes[nattributes].pValue = (void *)id->data;
        attributes[nattributes].ulValueLen = id->len;
        nattributes++;
    }

    tokens = NSSTrustDomain_FindTokensByURI(defaultTD, uri);
    for (tok = tokens; tok && *tok; tok++) {
        if (nssToken_IsPresent(*tok)) {
            slotinfo = (*tok)->pk11slot;

            rv = pk11_AuthenticateUnfriendly(slotinfo, PR_TRUE, wincx);
            if (rv != SECSuccess) {
                (void)nssToken_Destroy(*tok);
                continue;
            }
            instances = nssToken_FindObjectsByTemplate(*tok, NULL,
                                                       attributes,
                                                       nattributes,
                                                       0, &status);
            nssPKIObjectCollection_AddInstances(collection, instances, 0);
            nss_ZFreeIf(instances);
        }
        (void)nssToken_Destroy(*tok);
    }
    nss_ZFreeIf(tokens);
    certs = nssPKIObjectCollection_GetCertificates(collection, NULL, 0, NULL);

loser:
    if (certList) {
        nssList_Destroy(certList);
    }
    if (collection) {
        nssPKIObjectCollection_Destroy(collection);
    }
    if (uri) {
        PK11URI_DestroyURI(uri);
    }
    return certs;
}

CERTCertificate *
PK11_FindCertFromURI(const char *uri, void *wincx)
{
    static const NSSUsage usage = { PR_TRUE /* ... */ };
    NSSCertificate *cert = NULL;
    NSSCertificate **certs = NULL;
    CERTCertificate *rvCert = NULL;

    certs = find_certs_from_uri(uri, wincx);
    if (certs) {
        cert = nssCertificateArray_FindBestCertificate(certs, NULL,
                                                       &usage, NULL);
        if (cert) {
            rvCert = STAN_GetCERTCertificateOrRelease(cert);
        }
        nssCertificateArray_Destroy(certs);
    }
    return rvCert;
}

CERTCertList *
PK11_FindCertsFromURI(const char *uri, void *wincx)
{
    int i;
    CERTCertList *certList = NULL;
    NSSCertificate **foundCerts;
    NSSCertificate *c;

    foundCerts = find_certs_from_uri(uri, wincx);
    if (foundCerts) {
        PRTime now = PR_Now();
        certList = CERT_NewCertList();
        for (i = 0, c = *foundCerts; c; c = foundCerts[++i]) {
            if (certList) {
                CERTCertificate *certCert = STAN_GetCERTCertificateOrRelease(c);
                /* c may be invalid after this, don't reference it */
                if (certCert) {
                    /* CERT_AddCertToListSorted adopts certCert  */
                    CERT_AddCertToListSorted(certList, certCert,
                                             CERT_SortCBValidity, &now);
                }
            } else {
                nssCertificate_Destroy(c);
            }
        }
        if (certList && CERT_LIST_HEAD(certList) == NULL) {
            CERT_DestroyCertList(certList);
            certList = NULL;
        }
        /* all the certs have been adopted or freed, free the  raw array */
        nss_ZFreeIf(foundCerts);
    }
    return certList;
}

static NSSCertificate **
find_certs_from_nickname(const char *nickname, void *wincx)
{
    PRStatus status;
    NSSCertificate **certs = NULL;
    NSSToken *token = NULL;
    NSSTrustDomain *defaultTD = STAN_GetDefaultTrustDomain();
    PK11SlotInfo *slot = NULL;
    SECStatus rv;
    char *nickCopy;
    char *delimit = NULL;
    char *tokenName;

    if (!PORT_Strncasecmp(nickname, "pkcs11:", strlen("pkcs11:"))) {
        certs = find_certs_from_uri(nickname, wincx);
        if (certs)
            return certs;
    }
    nickCopy = PORT_Strdup(nickname);
    if (!nickCopy) {
        /* error code is set */
        return NULL;
    }
    if ((delimit = PORT_Strchr(nickCopy, ':')) != NULL) {
        tokenName = nickCopy;
        nickname = delimit + 1;
        *delimit = '\0';
        /* find token by name */
        token = NSSTrustDomain_FindTokenByName(defaultTD, (NSSUTF8 *)tokenName);
        if (token) {
            slot = PK11_ReferenceSlot(token->pk11slot);
        } else {
            PORT_SetError(SEC_ERROR_NO_TOKEN);
        }
        *delimit = ':';
    } else {
        slot = PK11_GetInternalKeySlot();
        token = PK11Slot_GetNSSToken(slot);
        if (!token) {
            PORT_SetError(SEC_ERROR_NO_TOKEN);
        }
    }
    if (token) {
        nssList *certList;
        nssCryptokiObject **instances;
        nssPKIObjectCollection *collection;
        nssTokenSearchType tokenOnly = nssTokenSearchType_TokenOnly;
        if (!PK11_IsPresent(slot)) {
            goto loser;
        }
        rv = pk11_AuthenticateUnfriendly(slot, PR_TRUE, wincx);
        if (rv != SECSuccess) {
            goto loser;
        }
        collection = nssCertificateCollection_Create(defaultTD, NULL);
        if (!collection) {
            goto loser;
        }
        certList = nssList_Create(NULL, PR_FALSE);
        if (!certList) {
            nssPKIObjectCollection_Destroy(collection);
            goto loser;
        }
        (void)nssTrustDomain_GetCertsForNicknameFromCache(defaultTD,
                                                          nickname,
                                                          certList);
        transfer_token_certs_to_collection(certList, token, collection);
        instances = nssToken_FindCertificatesByNickname(token,
                                                        NULL,
                                                        nickname,
                                                        tokenOnly,
                                                        0,
                                                        &status);
        nssPKIObjectCollection_AddInstances(collection, instances, 0);
        nss_ZFreeIf(instances);
        /* if it wasn't found, repeat the process for email address */
        if (nssPKIObjectCollection_Count(collection) == 0 &&
            PORT_Strchr(nickname, '@') != NULL) {
            char *lowercaseName = CERT_FixupEmailAddr(nickname);
            if (lowercaseName) {
                (void)nssTrustDomain_GetCertsForEmailAddressFromCache(defaultTD,
                                                                      lowercaseName,
                                                                      certList);
                transfer_token_certs_to_collection(certList, token, collection);
                instances = nssToken_FindCertificatesByEmail(token,
                                                             NULL,
                                                             lowercaseName,
                                                             tokenOnly,
                                                             0,
                                                             &status);
                nssPKIObjectCollection_AddInstances(collection, instances, 0);
                nss_ZFreeIf(instances);
                PORT_Free(lowercaseName);
            }
        }
        certs = nssPKIObjectCollection_GetCertificates(collection,
                                                       NULL, 0, NULL);
        nssPKIObjectCollection_Destroy(collection);
        nssList_Destroy(certList);
    }
loser:
    if (token) {
        (void)nssToken_Destroy(token);
    }
    if (slot) {
        PK11_FreeSlot(slot);
    }
    if (nickCopy)
        PORT_Free(nickCopy);
    return certs;
}

CERTCertificate *
PK11_FindCertFromNickname(const char *nickname, void *wincx)
{
    CERTCertificate *rvCert = NULL;
    NSSCertificate *cert = NULL;
    NSSCertificate **certs = NULL;
    static const NSSUsage usage = { PR_TRUE /* ... */ };

    certs = find_certs_from_nickname(nickname, wincx);
    if (certs) {
        cert = nssCertificateArray_FindBestCertificate(certs, NULL,
                                                       &usage, NULL);
        if (cert) {
            rvCert = STAN_GetCERTCertificateOrRelease(cert);
        }
        nssCertificateArray_Destroy(certs);
    }
    return rvCert;
}

/* Traverse slots callback */
typedef struct FindCertsEmailArgStr {
    char *email;
    CERTCertList *certList;
} FindCertsEmailArg;

SECStatus
FindCertsEmailCallback(CERTCertificate *cert, SECItem *item, void *arg)
{
    FindCertsEmailArg *cbparam = (FindCertsEmailArg *)arg;
    const char *cert_email = CERT_GetFirstEmailAddress(cert);
    PRBool found = PR_FALSE;

    /* Email address present in certificate? */
    if (cert_email == NULL) {
        return SECSuccess;
    }

    /* Parameter correctly set? */
    if (cbparam->email == NULL) {
        return SECFailure;
    }

    /* Loop over all email addresses */
    do {
        if (!strcmp(cert_email, cbparam->email)) {
            /* found one matching email address */
            PRTime now = PR_Now();
            found = PR_TRUE;
            CERT_AddCertToListSorted(cbparam->certList,
                                     CERT_DupCertificate(cert),
                                     CERT_SortCBValidity, &now);
        }
        cert_email = CERT_GetNextEmailAddress(cert, cert_email);
    } while (cert_email && !found);

    return SECSuccess;
}

/* Find all certificates with matching email address */
CERTCertList *
PK11_FindCertsFromEmailAddress(const char *email, void *wincx)
{
    FindCertsEmailArg cbparam;
    SECStatus rv;

    cbparam.certList = CERT_NewCertList();
    if (cbparam.certList == NULL) {
        return NULL;
    }

    cbparam.email = CERT_FixupEmailAddr(email);
    if (cbparam.email == NULL) {
        CERT_DestroyCertList(cbparam.certList);
        return NULL;
    }

    rv = PK11_TraverseSlotCerts(FindCertsEmailCallback, &cbparam, NULL);
    if (rv != SECSuccess) {
        CERT_DestroyCertList(cbparam.certList);
        PORT_Free(cbparam.email);
        return NULL;
    }

    /* empty list? */
    if (CERT_LIST_EMPTY(cbparam.certList)) {
        CERT_DestroyCertList(cbparam.certList);
        cbparam.certList = NULL;
    }

    PORT_Free(cbparam.email);
    return cbparam.certList;
}

CERTCertList *
PK11_FindCertsFromNickname(const char *nickname, void *wincx)
{
    int i;
    CERTCertList *certList = NULL;
    NSSCertificate **foundCerts = NULL;
    NSSCertificate *c;

    foundCerts = find_certs_from_nickname(nickname, wincx);
    if (foundCerts) {
        PRTime now = PR_Now();
        certList = CERT_NewCertList();
        for (i = 0, c = *foundCerts; c; c = foundCerts[++i]) {
            if (certList) {
                CERTCertificate *certCert = STAN_GetCERTCertificateOrRelease(c);
                /* c may be invalid after this, don't reference it */
                if (certCert) {
                    /* CERT_AddCertToListSorted adopts certCert  */
                    CERT_AddCertToListSorted(certList, certCert,
                                             CERT_SortCBValidity, &now);
                }
            } else {
                nssCertificate_Destroy(c);
            }
        }
        /* all the certs have been adopted or freed, free the  raw array */
        nss_ZFreeIf(foundCerts);
    }
    return certList;
}

/*
 * extract a key ID for a certificate...
 * NOTE: We call this function from PKCS11.c If we ever use
 * pkcs11 to extract the public key (we currently do not), this will break.
 */
SECItem *
PK11_GetPubIndexKeyID(CERTCertificate *cert)
{
    SECKEYPublicKey *pubk;
    SECItem *newItem = NULL;

    pubk = CERT_ExtractPublicKey(cert);
    if (pubk == NULL)
        return NULL;

    const SECItem *oldItem = PK11_GetPublicValueFromPublicKey(pubk);
    if (oldItem) {
        newItem = SECITEM_DupItem(oldItem);
    }

    SECKEY_DestroyPublicKey(pubk);
    /* make hash of it */
    return newItem;
}

/*
 * generate a CKA_ID from a certificate.
 */
SECItem *
pk11_mkcertKeyID(CERTCertificate *cert)
{
    SECItem *pubKeyData = PK11_GetPubIndexKeyID(cert);
    if (pubKeyData == NULL) {
        return NULL;
    }
    SECItem *certCKA_ID = PK11_MakeIDFromPubKey(pubKeyData);
    SECITEM_FreeItem(pubKeyData, PR_TRUE);
    return certCKA_ID;
}

/*
 * generate a CKA_ID from an encoded certificate.
 */
SECItem *
pk11_mkcertKeyIDFromDER(SECItem *certDER)
{
    // CERT_DecodeDERCertificate may be used internally to temporarily decode
    // an encoded certificate. Its lifetime should be bounded by this function.
    CERTCertificate *cert = CERT_DecodeDERCertificate(certDER, PR_FALSE, NULL);
    if (!cert) {
        return NULL;
    }
    SECItem *certCKA_ID = pk11_mkcertKeyID(cert);
    CERT_DestroyCertificate(cert);
    return certCKA_ID;
}

/*
 * Write the cert into the token.
 */
SECStatus
PK11_ImportCert(PK11SlotInfo *slot, CERTCertificate *cert,
                CK_OBJECT_HANDLE key, const char *nickname,
                PRBool includeTrust)
{
    PRStatus status;
    NSSCertificate *c;
    nssCryptokiObject *keyobj, *certobj;
    NSSToken *token = NULL;
    char *emailAddr = NULL;
    nssCertificateStoreTrace lockTrace = { NULL, NULL, PR_FALSE, PR_FALSE };
    nssCertificateStoreTrace unlockTrace = { NULL, NULL, PR_FALSE, PR_FALSE };

    token = PK11Slot_GetNSSToken(slot);
    if (!token) {
        PORT_SetError(SEC_ERROR_NO_TOKEN);
        goto loser;
    }

    if (PK11_IsInternal(slot) && cert->emailAddr && cert->emailAddr[0]) {
        emailAddr = cert->emailAddr;
    }

    /* need to get the cert as a stan cert */
    CERT_LockCertTempPerm(cert);
    NSSCertificate *nssCert = cert->nssCertificate;
    CERT_UnlockCertTempPerm(cert);
    if (nssCert) {
        c = nssCert;
    } else {
        c = STAN_GetNSSCertificate(cert);
        if (c == NULL) {
            goto loser;
        }
    }

    if (!c->id.data) {
        (void)nssToken_Destroy(token);
        PORT_SetError(SEC_ERROR_UNSUPPORTED_KEYALG);
        return SECFailure;
    }

    if (key != CK_INVALID_HANDLE) {
        /* create an object for the key, ... */
        keyobj = nss_ZNEW(NULL, nssCryptokiObject);
        if (!keyobj) {
            goto loser;
        }
        keyobj->token = nssToken_AddRef(token);
        keyobj->handle = key;
        keyobj->isTokenObject = PR_TRUE;

        /* ... in order to set matching attributes for the key */
        status = nssCryptokiPrivateKey_SetCertificate(keyobj, NULL, nickname,
                                                      &c->id, &c->subject);
        nssCryptokiObject_Destroy(keyobj);
        if (status != PR_SUCCESS) {
            goto loser;
        }
    }

    /* do the token import */
    certobj = nssToken_ImportCertificate(token, NULL,
                                         NSSCertificateType_PKIX,
                                         &c->id,
                                         nickname,
                                         &c->encoding,
                                         &c->issuer,
                                         &c->subject,
                                         &c->serial,
                                         emailAddr,
                                         PR_TRUE);
    if (!certobj) {
        if (NSS_GetError() == NSS_ERROR_INVALID_CERTIFICATE) {
            (void)nssToken_Destroy(token);
            PORT_SetError(SEC_ERROR_REUSED_ISSUER_AND_SERIAL);
            return SECFailure;
        }
        goto loser;
    }

    nssPKIObject_Lock(&c->object);
    NSSCryptoContext *cc = c->object.cryptoContext;
    nssPKIObject_Unlock(&c->object);
    if (cc) {
        nssCertificateStore_Lock(cc->certStore, &lockTrace);
        nssPKIObject_Lock(&c->object);
        if (c->object.cryptoContext == cc) {
            /* Delete the temp instance */
            nssCertificateStore_RemoveCertLOCKED(cc->certStore, c);
            c->object.cryptoContext = NULL;
            CERT_LockCertTempPerm(cert);
            cert->istemp = PR_FALSE;
            cert->isperm = PR_TRUE;
            CERT_UnlockCertTempPerm(cert);
        }
        nssPKIObject_Unlock(&c->object);
        nssCertificateStore_Unlock(cc->certStore, &lockTrace, &unlockTrace);
    }

    /* add the new instance to the cert, force an update of the
     * CERTCertificate, and finish
     */
    nssPKIObject_AddInstance(&c->object, certobj);
    // nssTrustDomain_AddCertToCache takes ownership of 'c' and returns an
    // owned reference to an NSSCertificate that may be a different object from
    // 'c'. Because 'cert' is backed by 'c', and because it must not go away as
    // a result of calling this function, increment the refcount on 'c' for the
    // duration of the call.
    nssCertificate_AddRef(c);
    NSSCertificate *cInCache = nssTrustDomain_AddCertToCache(STAN_GetDefaultTrustDomain(), c);
    if (cInCache) {
        (void)STAN_ForceCERTCertificateUpdate(cInCache);
        // Release the reference returned by nssTrustDomain_AddCertToCache.
        nssCertificate_Destroy(cInCache);
    } else {
        (void)STAN_ForceCERTCertificateUpdate(c);
        // nssTrustDomain_AddCertToCache already called nssCertificate_Destroy(c).
    }
    (void)nssToken_Destroy(token);
    return SECSuccess;
loser:
    if (token) {
        (void)nssToken_Destroy(token);
    }
    CERT_MapStanError();
    if (PORT_GetError() != SEC_ERROR_TOKEN_NOT_LOGGED_IN) {
        PORT_SetError(SEC_ERROR_ADDING_CERT);
    }
    return SECFailure;
}

SECStatus
PK11_ImportDERCert(PK11SlotInfo *slot, SECItem *derCert,
                   CK_OBJECT_HANDLE key, char *nickname, PRBool includeTrust)
{
    CERTCertificate *cert;
    SECStatus rv;

    cert = CERT_NewTempCertificate(CERT_GetDefaultCertDB(),
                                   derCert, NULL, PR_FALSE, PR_TRUE);
    if (cert == NULL)
        return SECFailure;

    rv = PK11_ImportCert(slot, cert, key, nickname, includeTrust);
    CERT_DestroyCertificate(cert);
    return rv;
}

/*
 * return the private key From a given Cert
 */
SECKEYPrivateKey *
PK11_FindPrivateKeyFromCert(PK11SlotInfo *slot, CERTCertificate *cert,
                            void *wincx)
{
    int err;
    CK_OBJECT_HANDLE certh;
    CK_OBJECT_HANDLE keyh;
    PRBool needLogin;
    SECStatus rv;

    certh = PK11_FindCertInSlot(slot, cert, wincx);
    if (certh == CK_INVALID_HANDLE) {
        return NULL;
    }
    /*
     * prevent a login race condition. If slot is logged in between
     * our call to pk11_LoginStillRequired and the
     * PK11_MatchItem. The matchItem call will either succeed, or
     * we will call it one more time after calling PK11_Authenticate
     * (which is a noop on an authenticated token).
     */
    needLogin = pk11_LoginStillRequired(slot, wincx);
    keyh = PK11_MatchItem(slot, certh, CKO_PRIVATE_KEY);
    if ((keyh == CK_INVALID_HANDLE) && needLogin &&
        (SSL_ERROR_NO_CERTIFICATE == (err = PORT_GetError()) ||
         SEC_ERROR_TOKEN_NOT_LOGGED_IN == err)) {
        /* try it again authenticated */
        rv = PK11_Authenticate(slot, PR_TRUE, wincx);
        if (rv != SECSuccess) {
            return NULL;
        }
        keyh = PK11_MatchItem(slot, certh, CKO_PRIVATE_KEY);
    }
    if (keyh == CK_INVALID_HANDLE) {
        return NULL;
    }

    return pk11_MakePrivKey(slot, nullKey, PR_FALSE, keyh, wincx);
}

/*
 * import a cert for a private key we have already generated. Set the label
 * on both to be the nickname. This is for the Key Gen, orphaned key case.
 */
PK11SlotInfo *
PK11_KeyForCertExists(CERTCertificate *cert, CK_OBJECT_HANDLE *keyPtr,
                      void *wincx)
{
    PK11SlotList *list;
    PK11SlotListElement *le;
    SECItem *keyID;
    CK_OBJECT_HANDLE key;
    PK11SlotInfo *slot = NULL;
    SECStatus rv;
    int err;

    keyID = pk11_mkcertKeyID(cert);
    /* get them all! */
    list = PK11_GetAllTokens(CKM_INVALID_MECHANISM, PR_FALSE, PR_TRUE, wincx);
    if ((keyID == NULL) || (list == NULL)) {
        if (keyID)
            SECITEM_FreeItem(keyID, PR_TRUE);
        if (list)
            PK11_FreeSlotList(list);
        return NULL;
    }

    /* Look for the slot that holds the Key */
    for (le = list->head; le; le = le->next) {
        /*
         * prevent a login race condition. If le->slot is logged in between
         * our call to pk11_LoginStillRequired and the
         * pk11_FindPrivateKeyFromCertID, the find will either succeed, or
         * we will call it one more time after calling PK11_Authenticate
         * (which is a noop on an authenticated token).
         */
        PRBool needLogin = pk11_LoginStillRequired(le->slot, wincx);
        key = pk11_FindPrivateKeyFromCertID(le->slot, keyID);
        if ((key == CK_INVALID_HANDLE) && needLogin &&
            (SSL_ERROR_NO_CERTIFICATE == (err = PORT_GetError()) ||
             SEC_ERROR_TOKEN_NOT_LOGGED_IN == err)) {
            /* authenticate and try again */
            rv = PK11_Authenticate(le->slot, PR_TRUE, wincx);
            if (rv != SECSuccess)
                continue;
            key = pk11_FindPrivateKeyFromCertID(le->slot, keyID);
        }
        if (key != CK_INVALID_HANDLE) {
            slot = PK11_ReferenceSlot(le->slot);
            if (keyPtr)
                *keyPtr = key;
            break;
        }
    }

    SECITEM_FreeItem(keyID, PR_TRUE);
    PK11_FreeSlotList(list);
    return slot;
}
/*
 * import a cert for a private key we have already generated. Set the label
 * on both to be the nickname. This is for the Key Gen, orphaned key case.
 */
PK11SlotInfo *
PK11_KeyForDERCertExists(SECItem *derCert, CK_OBJECT_HANDLE *keyPtr,
                         void *wincx)
{
    CERTCertificate *cert;
    PK11SlotInfo *slot = NULL;

    /* letting this use go -- the only thing that the cert is used for is
     * to get the ID attribute.
     */
    cert = CERT_DecodeDERCertificate(derCert, PR_FALSE, NULL);
    if (cert == NULL)
        return NULL;

    slot = PK11_KeyForCertExists(cert, keyPtr, wincx);
    CERT_DestroyCertificate(cert);
    return slot;
}

PK11SlotInfo *
PK11_ImportCertForKey(CERTCertificate *cert, const char *nickname,
                      void *wincx)
{
    PK11SlotInfo *slot = NULL;
    CK_OBJECT_HANDLE key;

    slot = PK11_KeyForCertExists(cert, &key, wincx);

    if (slot) {
        if (PK11_ImportCert(slot, cert, key, nickname, PR_FALSE) != SECSuccess) {
            PK11_FreeSlot(slot);
            slot = NULL;
        }
    } else {
        PORT_SetError(SEC_ERROR_ADDING_CERT);
    }

    return slot;
}

PK11SlotInfo *
PK11_ImportDERCertForKey(SECItem *derCert, char *nickname, void *wincx)
{
    CERTCertificate *cert;
    PK11SlotInfo *slot = NULL;

    cert = CERT_NewTempCertificate(CERT_GetDefaultCertDB(),
                                   derCert, NULL, PR_FALSE, PR_TRUE);
    if (cert == NULL)
        return NULL;

    slot = PK11_ImportCertForKey(cert, nickname, wincx);
    CERT_DestroyCertificate(cert);
    return slot;
}

static CK_OBJECT_HANDLE
pk11_FindCertObjectByTemplate(PK11SlotInfo **slotPtr,
                              CK_ATTRIBUTE *searchTemplate, size_t count, void *wincx)
{
    PK11SlotList *list;
    PK11SlotListElement *le;
    CK_OBJECT_HANDLE certHandle = CK_INVALID_HANDLE;
    PK11SlotInfo *slot = NULL;
    SECStatus rv;

    *slotPtr = NULL;

    /* get them all! */
    list = PK11_GetAllTokens(CKM_INVALID_MECHANISM, PR_FALSE, PR_TRUE, wincx);
    if (list == NULL) {
        return CK_INVALID_HANDLE;
    }

    /* Look for the slot that holds the Key */
    for (le = list->head; le; le = le->next) {
        rv = pk11_AuthenticateUnfriendly(le->slot, PR_TRUE, wincx);
        if (rv != SECSuccess)
            continue;

        certHandle = pk11_FindObjectByTemplate(le->slot, searchTemplate, count);
        if (certHandle != CK_INVALID_HANDLE) {
            slot = PK11_ReferenceSlot(le->slot);
            break;
        }
    }

    PK11_FreeSlotList(list);

    if (slot == NULL) {
        return CK_INVALID_HANDLE;
    }
    *slotPtr = slot;
    return certHandle;
}

CERTCertificate *
PK11_FindCertByIssuerAndSNOnToken(PK11SlotInfo *slot,
                                  CERTIssuerAndSN *issuerSN, void *wincx)
{
    CERTCertificate *rvCert = NULL;
    NSSCertificate *cert = NULL;
    NSSDER issuer, serial;
    NSSTrustDomain *td = STAN_GetDefaultTrustDomain();
    NSSToken *token = NULL;
    nssSession *session;
    nssCryptokiObject *instance = NULL;
    nssPKIObject *object = NULL;
    SECItem *derSerial;
    PRStatus status;

    if (!issuerSN || !issuerSN->derIssuer.data || !issuerSN->derIssuer.len ||
        !issuerSN->serialNumber.data || !issuerSN->serialNumber.len ||
        issuerSN->derIssuer.len > CERT_MAX_DN_BYTES ||
        issuerSN->serialNumber.len > CERT_MAX_SERIAL_NUMBER_BYTES) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    token = PK11Slot_GetNSSToken(slot);
    if (!token) {
        PORT_SetError(SEC_ERROR_NO_TOKEN);
        return NULL;
    }

    session = nssToken_GetDefaultSession(token); /* non-owning */
    if (!session) {
        (void)nssToken_Destroy(token);
        return NULL;
    }

    /* PKCS#11 needs to use DER-encoded serial numbers.  Create a
     * CERTIssuerAndSN that actually has the encoded value and pass that
     * to PKCS#11 (and the crypto context).
     */
    derSerial = SEC_ASN1EncodeItem(NULL, NULL,
                                   &issuerSN->serialNumber,
                                   SEC_ASN1_GET(SEC_IntegerTemplate));
    if (!derSerial) {
        (void)nssToken_Destroy(token);
        return NULL;
    }

    NSSITEM_FROM_SECITEM(&issuer, &issuerSN->derIssuer);
    NSSITEM_FROM_SECITEM(&serial, derSerial);

    instance = nssToken_FindCertificateByIssuerAndSerialNumber(token, session,
                                                               &issuer, &serial, nssTokenSearchType_TokenForced, &status);

    (void)nssToken_Destroy(token);
    SECITEM_FreeItem(derSerial, PR_TRUE);

    if (!instance) {
        goto loser;
    }
    object = nssPKIObject_Create(NULL, instance, td, NULL, nssPKIMonitor);
    if (!object) {
        goto loser;
    }
    instance = NULL; /* adopted by the previous call */
    cert = nssCertificate_Create(object);
    if (!cert) {
        goto loser;
    }
    object = NULL; /* adopted by the previous call */
    // nssTrustDomain_AddCertToCache takes ownership of 'cert' and returns an
    // owned reference to an NSSCertificate that may be a different object from
    // 'cert'.
    cert = nssTrustDomain_AddCertToCache(td, cert);
    if (!cert) {
        goto loser;
    }
    /* on failure, cert is freed below */
    rvCert = STAN_GetCERTCertificate(cert);
    if (!rvCert) {
        goto loser;
    }
    return rvCert;

loser:
    if (instance) {
        nssCryptokiObject_Destroy(instance);
    }
    if (object) {
        nssPKIObject_Destroy(object);
    }
    if (cert) {
        nssCertificate_Destroy(cert);
    }
    return NULL;
}

static PRCallOnceType keyIDHashCallOnce;

static PRStatus PR_CALLBACK
pk11_keyIDHash_populate(void *wincx)
{
    CERTCertList *certList;
    CERTCertListNode *node = NULL;
    SECItem subjKeyID = { siBuffer, NULL, 0 };
    SECItem *slotid = NULL;
    SECMODModuleList *modules, *mlp;
    SECMODListLock *moduleLock;
    int i;

    certList = PK11_ListCerts(PK11CertListUser, wincx);
    if (!certList) {
        return PR_FAILURE;
    }

    for (node = CERT_LIST_HEAD(certList);
         !CERT_LIST_END(node, certList);
         node = CERT_LIST_NEXT(node)) {
        if (CERT_FindSubjectKeyIDExtension(node->cert,
                                           &subjKeyID) == SECSuccess &&
            subjKeyID.data != NULL) {
            cert_AddSubjectKeyIDMapping(&subjKeyID, node->cert);
            SECITEM_FreeItem(&subjKeyID, PR_FALSE);
        }
    }
    CERT_DestroyCertList(certList);

    /*
     * Record the state of each slot in a hash. The concatenation of slotID
     * and moduleID is used as its key, with the slot series as its value.
     */
    slotid = SECITEM_AllocItem(NULL, NULL,
                               sizeof(CK_SLOT_ID) + sizeof(SECMODModuleID));
    if (!slotid) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return PR_FAILURE;
    }
    moduleLock = SECMOD_GetDefaultModuleListLock();
    if (!moduleLock) {
        SECITEM_FreeItem(slotid, PR_TRUE);
        PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
        return PR_FAILURE;
    }
    SECMOD_GetReadLock(moduleLock);
    modules = SECMOD_GetDefaultModuleList();
    for (mlp = modules; mlp; mlp = mlp->next) {
        for (i = 0; i < mlp->module->slotCount; i++) {
            memcpy(slotid->data, &mlp->module->slots[i]->slotID,
                   sizeof(CK_SLOT_ID));
            memcpy(&slotid->data[sizeof(CK_SLOT_ID)], &mlp->module->moduleID,
                   sizeof(SECMODModuleID));
            cert_UpdateSubjectKeyIDSlotCheck(slotid,
                                             mlp->module->slots[i]->series);
        }
    }
    SECMOD_ReleaseReadLock(moduleLock);
    SECITEM_FreeItem(slotid, PR_TRUE);

    return PR_SUCCESS;
}

/*
 * We're looking for a cert which we have the private key for that's on the
 * list of recipients. This searches one slot.
 * this is the new version for NSS SMIME code
 * this stuff should REALLY be in the SMIME code, but some things in here are not public
 * (they should be!)
 */
static CERTCertificate *
pk11_FindCertObjectByRecipientNew(PK11SlotInfo *slot, NSSCMSRecipient **recipientlist,
                                  int *rlIndex, void *pwarg)
{
    NSSCMSRecipient *ri = NULL;
    int i;
    PRBool tokenRescanDone = PR_FALSE;
    CERTCertTrust trust;

    for (i = 0; (ri = recipientlist[i]) != NULL; i++) {
        CERTCertificate *cert = NULL;
        if (ri->kind == RLSubjKeyID) {
            SECItem *derCert = cert_FindDERCertBySubjectKeyID(ri->id.subjectKeyID);
            if (!derCert && !tokenRescanDone) {
                /*
                 * We didn't find the cert by its key ID. If we have slots
                 * with removable tokens, a failure from
                 * cert_FindDERCertBySubjectKeyID doesn't necessarily imply
                 * that the cert is unavailable - the token might simply
                 * have been inserted after the initial run of
                 * pk11_keyIDHash_populate (wrapped by PR_CallOnceWithArg),
                 * or a different token might have been present in that
                 * slot, initially. Let's check for new tokens...
                 */
                PK11SlotList *sl = PK11_GetAllTokens(CKM_INVALID_MECHANISM,
                                                     PR_FALSE, PR_FALSE, pwarg);
                if (sl) {
                    PK11SlotListElement *le;
                    SECItem *slotid = SECITEM_AllocItem(NULL, NULL,
                                                        sizeof(CK_SLOT_ID) + sizeof(SECMODModuleID));
                    if (!slotid) {
                        PORT_SetError(SEC_ERROR_NO_MEMORY);
                        PK11_FreeSlotList(sl);
                        return NULL;
                    }
                    for (le = sl->head; le; le = le->next) {
                        memcpy(slotid->data, &le->slot->slotID,
                               sizeof(CK_SLOT_ID));
                        memcpy(&slotid->data[sizeof(CK_SLOT_ID)],
                               &le->slot->module->moduleID,
                               sizeof(SECMODModuleID));
                        /*
                         * Any changes with the slot since our last check?
                         * If so, re-read the certs in that specific slot.
                         */
                        if (cert_SubjectKeyIDSlotCheckSeries(slotid) != PK11_GetSlotSeries(le->slot)) {
                            CERTCertListNode *node = NULL;
                            SECItem subjKeyID = { siBuffer, NULL, 0 };
                            CERTCertList *cl = PK11_ListCertsInSlot(le->slot);
                            if (!cl) {
                                continue;
                            }
                            for (node = CERT_LIST_HEAD(cl);
                                 !CERT_LIST_END(node, cl);
                                 node = CERT_LIST_NEXT(node)) {
                                if (CERT_IsUserCert(node->cert) &&
                                    CERT_FindSubjectKeyIDExtension(node->cert,
                                                                   &subjKeyID) == SECSuccess) {
                                    if (subjKeyID.data) {
                                        cert_AddSubjectKeyIDMapping(&subjKeyID,
                                                                    node->cert);
                                        cert_UpdateSubjectKeyIDSlotCheck(slotid,
                                                                         PK11_GetSlotSeries(le->slot));
                                    }
                                    SECITEM_FreeItem(&subjKeyID, PR_FALSE);
                                }
                            }
                            CERT_DestroyCertList(cl);
                        }
                    }
                    PK11_FreeSlotList(sl);
                    SECITEM_FreeItem(slotid, PR_TRUE);
                }
                /* only check once per message/recipientlist */
                tokenRescanDone = PR_TRUE;
                /* do another lookup (hopefully we found that cert...) */
                derCert = cert_FindDERCertBySubjectKeyID(ri->id.subjectKeyID);
            }
            if (derCert) {
                cert = PK11_FindCertFromDERCertItem(slot, derCert, pwarg);
                SECITEM_FreeItem(derCert, PR_TRUE);
            }
        } else {
            cert = PK11_FindCertByIssuerAndSNOnToken(slot, ri->id.issuerAndSN,
                                                     pwarg);
        }
        if (cert) {
            /* this isn't our cert */
            if (CERT_GetCertTrust(cert, &trust) != SECSuccess ||
                ((trust.emailFlags & CERTDB_USER) != CERTDB_USER)) {
                CERT_DestroyCertificate(cert);
                continue;
            }
            ri->slot = PK11_ReferenceSlot(slot);
            *rlIndex = i;
            return cert;
        }
    }
    *rlIndex = -1;
    return NULL;
}

/*
 * This function is the same as above, but it searches all the slots.
 * this is the new version for NSS SMIME code
 * this stuff should REALLY be in the SMIME code, but some things in here are not public
 * (they should be!)
 */
static CERTCertificate *
pk11_AllFindCertObjectByRecipientNew(NSSCMSRecipient **recipientlist, void *wincx, int *rlIndex)
{
    PK11SlotList *list;
    PK11SlotListElement *le;
    CERTCertificate *cert = NULL;
    SECStatus rv;

    /* get them all! */
    list = PK11_GetAllTokens(CKM_INVALID_MECHANISM, PR_FALSE, PR_TRUE, wincx);
    if (list == NULL) {
        return CK_INVALID_HANDLE;
    }

    /* Look for the slot that holds the Key */
    for (le = list->head; le; le = le->next) {
        rv = pk11_AuthenticateUnfriendly(le->slot, PR_TRUE, wincx);
        if (rv != SECSuccess)
            continue;

        cert = pk11_FindCertObjectByRecipientNew(le->slot,
                                                 recipientlist, rlIndex, wincx);
        if (cert)
            break;
    }

    PK11_FreeSlotList(list);

    return cert;
}

/*
 * We're looking for a cert which we have the private key for that's on the
 * list of recipients. This searches one slot.
 */
static CERTCertificate *
pk11_FindCertObjectByRecipient(PK11SlotInfo *slot,
                               SEC_PKCS7RecipientInfo **recipientArray,
                               SEC_PKCS7RecipientInfo **rip, void *pwarg)
{
    SEC_PKCS7RecipientInfo *ri = NULL;
    CERTCertTrust trust;
    int i;

    for (i = 0; (ri = recipientArray[i]) != NULL; i++) {
        CERTCertificate *cert;

        cert = PK11_FindCertByIssuerAndSNOnToken(slot, ri->issuerAndSN,
                                                 pwarg);
        if (cert) {
            /* this isn't our cert */
            if (CERT_GetCertTrust(cert, &trust) != SECSuccess ||
                ((trust.emailFlags & CERTDB_USER) != CERTDB_USER)) {
                CERT_DestroyCertificate(cert);
                continue;
            }
            *rip = ri;
            return cert;
        }
    }
    *rip = NULL;
    return NULL;
}

/*
 * This function is the same as above, but it searches all the slots.
 */
static CERTCertificate *
pk11_AllFindCertObjectByRecipient(PK11SlotInfo **slotPtr,
                                  SEC_PKCS7RecipientInfo **recipientArray,
                                  SEC_PKCS7RecipientInfo **rip,
                                  void *wincx)
{
    PK11SlotList *list;
    PK11SlotListElement *le;
    CERTCertificate *cert = NULL;
    PK11SlotInfo *slot = NULL;
    SECStatus rv;

    *slotPtr = NULL;

    /* get them all! */
    list = PK11_GetAllTokens(CKM_INVALID_MECHANISM, PR_FALSE, PR_TRUE, wincx);
    if (list == NULL) {
        return CK_INVALID_HANDLE;
    }

    *rip = NULL;

    /* Look for the slot that holds the Key */
    for (le = list->head; le; le = le->next) {
        rv = pk11_AuthenticateUnfriendly(le->slot, PR_TRUE, wincx);
        if (rv != SECSuccess)
            continue;

        cert = pk11_FindCertObjectByRecipient(le->slot, recipientArray,
                                              rip, wincx);
        if (cert) {
            slot = PK11_ReferenceSlot(le->slot);
            break;
        }
    }

    PK11_FreeSlotList(list);

    if (slot == NULL) {
        return NULL;
    }
    *slotPtr = slot;
    PORT_Assert(cert != NULL);
    return cert;
}

/*
 * We need to invert the search logic for PKCS 7 because if we search for
 * each cert on the list over all the slots, we wind up with lots of spurious
 * password prompts. This way we get only one password prompt per slot, at
 * the max, and most of the time we can find the cert, and only prompt for
 * the key...
 */
CERTCertificate *
PK11_FindCertAndKeyByRecipientList(PK11SlotInfo **slotPtr,
                                   SEC_PKCS7RecipientInfo **array,
                                   SEC_PKCS7RecipientInfo **rip,
                                   SECKEYPrivateKey **privKey, void *wincx)
{
    CERTCertificate *cert = NULL;

    *privKey = NULL;
    *slotPtr = NULL;
    cert = pk11_AllFindCertObjectByRecipient(slotPtr, array, rip, wincx);
    if (!cert) {
        return NULL;
    }

    *privKey = PK11_FindKeyByAnyCert(cert, wincx);
    if (*privKey == NULL) {
        goto loser;
    }

    return cert;
loser:
    if (cert)
        CERT_DestroyCertificate(cert);
    if (*slotPtr)
        PK11_FreeSlot(*slotPtr);
    *slotPtr = NULL;
    return NULL;
}

/*
 * This is the new version of the above function for NSS SMIME code
 * this stuff should REALLY be in the SMIME code, but some things in here are not public
 * (they should be!)
 */
int
PK11_FindCertAndKeyByRecipientListNew(NSSCMSRecipient **recipientlist, void *wincx)
{
    CERTCertificate *cert;
    NSSCMSRecipient *rl;
    PRStatus rv;
    int rlIndex;

    rv = PR_CallOnceWithArg(&keyIDHashCallOnce, pk11_keyIDHash_populate, wincx);
    if (rv != PR_SUCCESS)
        return -1;

    cert = pk11_AllFindCertObjectByRecipientNew(recipientlist, wincx, &rlIndex);
    if (!cert) {
        return -1;
    }

    rl = recipientlist[rlIndex];

    /* at this point, rl->slot is set */

    rl->privkey = PK11_FindKeyByAnyCert(cert, wincx);
    if (rl->privkey == NULL) {
        goto loser;
    }

    /* make a cert from the cert handle */
    rl->cert = cert;
    return rlIndex;

loser:
    if (cert)
        CERT_DestroyCertificate(cert);
    if (rl->slot)
        PK11_FreeSlot(rl->slot);
    rl->slot = NULL;
    return -1;
}

CERTCertificate *
PK11_FindCertByIssuerAndSN(PK11SlotInfo **slotPtr, CERTIssuerAndSN *issuerSN,
                           void *wincx)
{
    CERTCertificate *rvCert = NULL;
    NSSCertificate *cert;
    NSSDER issuer, serial;
    NSSCryptoContext *cc;
    SECItem *derSerial;

    if (!issuerSN || !issuerSN->derIssuer.data || !issuerSN->derIssuer.len ||
        !issuerSN->serialNumber.data || !issuerSN->serialNumber.len ||
        issuerSN->derIssuer.len > CERT_MAX_DN_BYTES ||
        issuerSN->serialNumber.len > CERT_MAX_SERIAL_NUMBER_BYTES) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    if (slotPtr)
        *slotPtr = NULL;

    /* PKCS#11 needs to use DER-encoded serial numbers.  Create a
     * CERTIssuerAndSN that actually has the encoded value and pass that
     * to PKCS#11 (and the crypto context).
     */
    derSerial = SEC_ASN1EncodeItem(NULL, NULL,
                                   &issuerSN->serialNumber,
                                   SEC_ASN1_GET(SEC_IntegerTemplate));
    if (!derSerial) {
        return NULL;
    }

    NSSITEM_FROM_SECITEM(&issuer, &issuerSN->derIssuer);
    NSSITEM_FROM_SECITEM(&serial, derSerial);

    cc = STAN_GetDefaultCryptoContext();
    cert = NSSCryptoContext_FindCertificateByIssuerAndSerialNumber(cc,
                                                                   &issuer,
                                                                   &serial);
    if (cert) {
        SECITEM_FreeItem(derSerial, PR_TRUE);
        return STAN_GetCERTCertificateOrRelease(cert);
    }

    do {
        /* free the old cert on retry. Associated slot was not present */
        if (rvCert) {
            CERT_DestroyCertificate(rvCert);
            rvCert = NULL;
        }

        cert = NSSTrustDomain_FindCertificateByIssuerAndSerialNumber(
            STAN_GetDefaultTrustDomain(),
            &issuer,
            &serial);
        if (!cert) {
            break;
        }

        rvCert = STAN_GetCERTCertificateOrRelease(cert);
        if (rvCert == NULL) {
            break;
        }

        /* Check to see if the cert's token is still there */
    } while (rvCert->slot && !PK11_IsPresent(rvCert->slot));

    if (rvCert && rvCert->slot && slotPtr)
        *slotPtr = PK11_ReferenceSlot(rvCert->slot);

    SECITEM_FreeItem(derSerial, PR_TRUE);
    return rvCert;
}

CK_OBJECT_HANDLE
PK11_FindObjectForCert(CERTCertificate *cert, void *wincx, PK11SlotInfo **pSlot)
{
    CK_OBJECT_HANDLE certHandle;
    CK_OBJECT_CLASS certClass = CKO_CERTIFICATE;
    CK_ATTRIBUTE *attr;
    CK_ATTRIBUTE searchTemplate[] = {
        { CKA_CLASS, NULL, 0 },
        { CKA_VALUE, NULL, 0 },
    };
    const size_t templateSize = sizeof(searchTemplate) / sizeof(searchTemplate[0]);

    attr = searchTemplate;
    PK11_SETATTRS(attr, CKA_CLASS, &certClass, sizeof(certClass));
    attr++;
    PK11_SETATTRS(attr, CKA_VALUE, cert->derCert.data, cert->derCert.len);

    if (cert->slot) {
        certHandle = PK11_FindCertInSlot(cert->slot, cert, wincx);
        if (certHandle != CK_INVALID_HANDLE) {
            *pSlot = PK11_ReferenceSlot(cert->slot);
            return certHandle;
        }
    }

    certHandle = pk11_FindCertObjectByTemplate(pSlot, searchTemplate,
                                               templateSize, wincx);
    if (certHandle != CK_INVALID_HANDLE) {
        if (cert->slot == NULL) {
            cert->slot = PK11_ReferenceSlot(*pSlot);
            cert->pkcs11ID = certHandle;
            cert->ownSlot = PR_TRUE;
            cert->series = cert->slot->series;
        }
    }

    return (certHandle);
}

SECKEYPrivateKey *
PK11_FindKeyByAnyCert(CERTCertificate *cert, void *wincx)
{
    CK_OBJECT_HANDLE certHandle;
    CK_OBJECT_HANDLE keyHandle;
    PK11SlotInfo *slot = NULL;
    SECKEYPrivateKey *privKey = NULL;
    PRBool needLogin;
    SECStatus rv;
    int err;

    certHandle = PK11_FindObjectForCert(cert, wincx, &slot);
    if (certHandle == CK_INVALID_HANDLE) {
        return NULL;
    }
    /*
     * prevent a login race condition. If slot is logged in between
     * our call to pk11_LoginStillRequired and the
     * PK11_MatchItem. The matchItem call will either succeed, or
     * we will call it one more time after calling PK11_Authenticate
     * (which is a noop on an authenticated token).
     */
    needLogin = pk11_LoginStillRequired(slot, wincx);
    keyHandle = PK11_MatchItem(slot, certHandle, CKO_PRIVATE_KEY);
    if ((keyHandle == CK_INVALID_HANDLE) && needLogin &&
        (SSL_ERROR_NO_CERTIFICATE == (err = PORT_GetError()) ||
         SEC_ERROR_TOKEN_NOT_LOGGED_IN == err)) {
        /* authenticate and try again */
        rv = PK11_Authenticate(slot, PR_TRUE, wincx);
        if (rv == SECSuccess) {
            keyHandle = PK11_MatchItem(slot, certHandle, CKO_PRIVATE_KEY);
        }
    }
    if (keyHandle != CK_INVALID_HANDLE) {
        privKey = pk11_MakePrivKey(slot, nullKey, PR_FALSE, keyHandle, wincx);
    }
    if (slot) {
        PK11_FreeSlot(slot);
    }
    return privKey;
}

CK_OBJECT_HANDLE
pk11_FindPubKeyByAnyCert(CERTCertificate *cert, PK11SlotInfo **slot, void *wincx)
{
    CK_OBJECT_HANDLE certHandle;
    CK_OBJECT_HANDLE keyHandle;

    certHandle = PK11_FindObjectForCert(cert, wincx, slot);
    if (certHandle == CK_INVALID_HANDLE) {
        return CK_INVALID_HANDLE;
    }
    keyHandle = PK11_MatchItem(*slot, certHandle, CKO_PUBLIC_KEY);
    if (keyHandle == CK_INVALID_HANDLE) {
        PK11_FreeSlot(*slot);
        return CK_INVALID_HANDLE;
    }
    return keyHandle;
}

SECStatus
PK11_TraverseCertsForSubjectInSlot(CERTCertificate *cert, PK11SlotInfo *slot,
                                   SECStatus (*callback)(CERTCertificate *, void *), void *arg)
{
    PRStatus nssrv = PR_SUCCESS;
    NSSToken *token;
    NSSDER subject;
    NSSTrustDomain *td;
    nssList *subjectList;
    nssPKIObjectCollection *collection;
    nssCryptokiObject **instances;
    NSSCertificate **certs;
    nssTokenSearchType tokenOnly = nssTokenSearchType_TokenOnly;
    td = STAN_GetDefaultTrustDomain();
    NSSITEM_FROM_SECITEM(&subject, &cert->derSubject);
    token = PK11Slot_GetNSSToken(slot);
    if (!token) {
        return SECSuccess;
    }
    if (!nssToken_IsPresent(token)) {
        (void)nssToken_Destroy(token);
        return SECSuccess;
    }
    collection = nssCertificateCollection_Create(td, NULL);
    if (!collection) {
        (void)nssToken_Destroy(token);
        return SECFailure;
    }
    subjectList = nssList_Create(NULL, PR_FALSE);
    if (!subjectList) {
        nssPKIObjectCollection_Destroy(collection);
        (void)nssToken_Destroy(token);
        return SECFailure;
    }
    (void)nssTrustDomain_GetCertsForSubjectFromCache(td, &subject,
                                                     subjectList);
    transfer_token_certs_to_collection(subjectList, token, collection);
    instances = nssToken_FindCertificatesBySubject(token, NULL,
                                                   &subject,
                                                   tokenOnly, 0, &nssrv);
    nssPKIObjectCollection_AddInstances(collection, instances, 0);
    nss_ZFreeIf(instances);
    nssList_Destroy(subjectList);
    certs = nssPKIObjectCollection_GetCertificates(collection,
                                                   NULL, 0, NULL);
    nssPKIObjectCollection_Destroy(collection);
    (void)nssToken_Destroy(token);
    if (certs) {
        CERTCertificate *oldie;
        NSSCertificate **cp;
        for (cp = certs; *cp; cp++) {
            oldie = STAN_GetCERTCertificate(*cp);
            if (!oldie) {
                continue;
            }
            if ((*callback)(oldie, arg) != SECSuccess) {
                nssrv = PR_FAILURE;
                break;
            }
        }
        nssCertificateArray_Destroy(certs);
    }
    return (nssrv == PR_SUCCESS) ? SECSuccess : SECFailure;
}

SECStatus
PK11_TraverseCertsForNicknameInSlot(SECItem *nickname, PK11SlotInfo *slot,
                                    SECStatus (*callback)(CERTCertificate *, void *), void *arg)
{
    PRStatus nssrv = PR_SUCCESS;
    NSSToken *token;
    NSSTrustDomain *td;
    NSSUTF8 *nick;
    PRBool created = PR_FALSE;
    nssCryptokiObject **instances;
    nssPKIObjectCollection *collection = NULL;
    NSSCertificate **certs;
    nssList *nameList = NULL;
    nssTokenSearchType tokenOnly = nssTokenSearchType_TokenOnly;
    if (!nickname || !nickname->data || nickname->len == 0) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    token = PK11Slot_GetNSSToken(slot);
    if (!token || !nssToken_IsPresent(token)) {
        (void)nssToken_Destroy(token);
        return SECSuccess;
    }
    if (!nickname || !nickname->data || nickname->len == 0) {
        (void)nssToken_Destroy(token);
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    if (nickname->data[nickname->len - 1] != '\0') {
        nick = nssUTF8_Create(NULL, nssStringType_UTF8String,
                              nickname->data, nickname->len);
        created = PR_TRUE;
    } else {
        nick = (NSSUTF8 *)nickname->data;
    }
    td = STAN_GetDefaultTrustDomain();
    collection = nssCertificateCollection_Create(td, NULL);
    if (!collection) {
        goto loser;
    }
    nameList = nssList_Create(NULL, PR_FALSE);
    if (!nameList) {
        goto loser;
    }
    (void)nssTrustDomain_GetCertsForNicknameFromCache(td, nick, nameList);
    transfer_token_certs_to_collection(nameList, token, collection);
    instances = nssToken_FindCertificatesByNickname(token, NULL,
                                                    nick,
                                                    tokenOnly, 0, &nssrv);
    nssPKIObjectCollection_AddInstances(collection, instances, 0);
    nss_ZFreeIf(instances);
    nssList_Destroy(nameList);
    certs = nssPKIObjectCollection_GetCertificates(collection,
                                                   NULL, 0, NULL);
    nssPKIObjectCollection_Destroy(collection);
    (void)nssToken_Destroy(token);
    if (certs) {
        CERTCertificate *oldie;
        NSSCertificate **cp;
        for (cp = certs; *cp; cp++) {
            oldie = STAN_GetCERTCertificate(*cp);
            if (!oldie) {
                continue;
            }
            if ((*callback)(oldie, arg) != SECSuccess) {
                nssrv = PR_FAILURE;
                break;
            }
        }
        nssCertificateArray_Destroy(certs);
    }
    if (created)
        nss_ZFreeIf(nick);
    return (nssrv == PR_SUCCESS) ? SECSuccess : SECFailure;
loser:
    (void)nssToken_Destroy(token);
    if (created) {
        nss_ZFreeIf(nick);
    }
    if (collection) {
        nssPKIObjectCollection_Destroy(collection);
    }
    if (nameList) {
        nssList_Destroy(nameList);
    }
    return SECFailure;
}

SECStatus
PK11_TraverseCertsInSlot(PK11SlotInfo *slot,
                         SECStatus (*callback)(CERTCertificate *, void *), void *arg)
{
    PRStatus nssrv;
    NSSTrustDomain *td = STAN_GetDefaultTrustDomain();
    NSSToken *tok;
    nssList *certList = NULL;
    nssCryptokiObject **instances;
    nssPKIObjectCollection *collection;
    NSSCertificate **certs;
    nssTokenSearchType tokenOnly = nssTokenSearchType_TokenOnly;
    tok = PK11Slot_GetNSSToken(slot);
    if (!tok) {
        return SECSuccess;
    }
    if (!nssToken_IsPresent(tok)) {
        (void)nssToken_Destroy(tok);
        return SECSuccess;
    }
    collection = nssCertificateCollection_Create(td, NULL);
    if (!collection) {
        (void)nssToken_Destroy(tok);
        return SECFailure;
    }
    certList = nssList_Create(NULL, PR_FALSE);
    if (!certList) {
        nssPKIObjectCollection_Destroy(collection);
        (void)nssToken_Destroy(tok);
        return SECFailure;
    }
    (void)nssTrustDomain_GetCertsFromCache(td, certList);
    transfer_token_certs_to_collection(certList, tok, collection);
    instances = nssToken_FindObjects(tok, NULL, CKO_CERTIFICATE,
                                     tokenOnly, 0, &nssrv);
    nssPKIObjectCollection_AddInstances(collection, instances, 0);
    nss_ZFreeIf(instances);
    nssList_Destroy(certList);
    certs = nssPKIObjectCollection_GetCertificates(collection,
                                                   NULL, 0, NULL);
    nssPKIObjectCollection_Destroy(collection);
    (void)nssToken_Destroy(tok);
    if (certs) {
        CERTCertificate *oldie;
        NSSCertificate **cp;
        for (cp = certs; *cp; cp++) {
            oldie = STAN_GetCERTCertificate(*cp);
            if (!oldie) {
                continue;
            }
            if ((*callback)(oldie, arg) != SECSuccess) {
                nssrv = PR_FAILURE;
                break;
            }
        }
        nssCertificateArray_Destroy(certs);
    }
    return (nssrv == PR_SUCCESS) ? SECSuccess : SECFailure;
}

/*
 * return the certificate associated with a derCert
 */
CERTCertificate *
PK11_FindCertFromDERCert(PK11SlotInfo *slot, CERTCertificate *cert,
                         void *wincx)
{
    return PK11_FindCertFromDERCertItem(slot, &cert->derCert, wincx);
}

CERTCertificate *
PK11_FindCertFromDERCertItem(PK11SlotInfo *slot, const SECItem *inDerCert,
                             void *wincx)

{
    NSSDER derCert;
    NSSToken *tok;
    nssCryptokiObject *co = NULL;
    SECStatus rv;
    CERTCertificate *cert = NULL;

    NSSITEM_FROM_SECITEM(&derCert, inDerCert);
    rv = pk11_AuthenticateUnfriendly(slot, PR_TRUE, wincx);
    if (rv != SECSuccess) {
        return NULL;
    }

    tok = PK11Slot_GetNSSToken(slot);
    if (!tok) {
        return NULL;
    }
    co = nssToken_FindCertificateByEncodedCertificate(tok, NULL, &derCert,
                                                      nssTokenSearchType_TokenOnly, NULL);
    (void)nssToken_Destroy(tok);

    if (co) {
        cert = PK11_MakeCertFromHandle(slot, co->handle, NULL);
        nssCryptokiObject_Destroy(co);
    }

    return cert;
}

/*
 * import a cert for a private key we have already generated. Set the label
 * on both to be the nickname.
 */
static CK_OBJECT_HANDLE
pk11_findKeyObjectByDERCert(PK11SlotInfo *slot, CERTCertificate *cert,
                            void *wincx)
{
    SECItem *keyID;
    CK_OBJECT_HANDLE key;
    SECStatus rv;
    PRBool needLogin;
    int err;

    if ((slot == NULL) || (cert == NULL)) {
        return CK_INVALID_HANDLE;
    }

    keyID = pk11_mkcertKeyID(cert);
    if (keyID == NULL) {
        return CK_INVALID_HANDLE;
    }

    /*
     * prevent a login race condition. If slot is logged in between
     * our call to pk11_LoginStillRequired and the
     * pk11_FindPrivateKeyFromCerID. The matchItem call will either succeed, or
     * we will call it one more time after calling PK11_Authenticate
     * (which is a noop on an authenticated token).
     */
    needLogin = pk11_LoginStillRequired(slot, wincx);
    key = pk11_FindPrivateKeyFromCertID(slot, keyID);
    if ((key == CK_INVALID_HANDLE) && needLogin &&
        (SSL_ERROR_NO_CERTIFICATE == (err = PORT_GetError()) ||
         SEC_ERROR_TOKEN_NOT_LOGGED_IN == err)) {
        /* authenticate and try again */
        rv = PK11_Authenticate(slot, PR_TRUE, wincx);
        if (rv != SECSuccess)
            goto loser;
        key = pk11_FindPrivateKeyFromCertID(slot, keyID);
    }

loser:
    SECITEM_ZfreeItem(keyID, PR_TRUE);
    return key;
}

SECKEYPrivateKey *
PK11_FindKeyByDERCert(PK11SlotInfo *slot, CERTCertificate *cert,
                      void *wincx)
{
    CK_OBJECT_HANDLE keyHandle;

    if ((slot == NULL) || (cert == NULL)) {
        return NULL;
    }

    keyHandle = pk11_findKeyObjectByDERCert(slot, cert, wincx);
    if (keyHandle == CK_INVALID_HANDLE) {
        return NULL;
    }

    return pk11_MakePrivKey(slot, nullKey, PR_FALSE, keyHandle, wincx);
}

SECStatus
PK11_ImportCertForKeyToSlot(PK11SlotInfo *slot, CERTCertificate *cert,
                            char *nickname,
                            PRBool addCertUsage, void *wincx)
{
    CK_OBJECT_HANDLE keyHandle;

    if ((slot == NULL) || (cert == NULL) || (nickname == NULL)) {
        return SECFailure;
    }

    keyHandle = pk11_findKeyObjectByDERCert(slot, cert, wincx);
    if (keyHandle == CK_INVALID_HANDLE) {
        return SECFailure;
    }

    return PK11_ImportCert(slot, cert, keyHandle, nickname, addCertUsage);
}

/* remove when the real version comes out */
#define SEC_OID_MISSI_KEA 300 /* until we have v3 stuff merged */
PRBool
KEAPQGCompare(CERTCertificate *server, CERTCertificate *cert)
{

    /* not implemented */
    return PR_FALSE;
}

PRBool
PK11_FortezzaHasKEA(CERTCertificate *cert)
{
    /* look at the subject and see if it is a KEA for MISSI key */
    SECOidData *oid;
    CERTCertTrust trust;

    if (CERT_GetCertTrust(cert, &trust) != SECSuccess ||
        ((trust.sslFlags & CERTDB_USER) != CERTDB_USER)) {
        return PR_FALSE;
    }

    oid = SECOID_FindOID(&cert->subjectPublicKeyInfo.algorithm.algorithm);
    if (!oid) {
        return PR_FALSE;
    }

    return (PRBool)((oid->offset == SEC_OID_MISSI_KEA_DSS_OLD) ||
                    (oid->offset == SEC_OID_MISSI_KEA_DSS) ||
                    (oid->offset == SEC_OID_MISSI_KEA));
}

/*
 * Find a kea cert on this slot that matches the domain of it's peer
 */
static CERTCertificate *
pk11_GetKEAMate(PK11SlotInfo *slot, CERTCertificate *peer)
{
    int i;
    CERTCertificate *returnedCert = NULL;

    for (i = 0; i < slot->cert_count; i++) {
        CERTCertificate *cert = slot->cert_array[i];

        if (PK11_FortezzaHasKEA(cert) && KEAPQGCompare(peer, cert)) {
            returnedCert = CERT_DupCertificate(cert);
            break;
        }
    }
    return returnedCert;
}

/*
 * The following is a FORTEZZA only Certificate request. We call this when we
 * are doing a non-client auth SSL connection. We are only interested in the
 * fortezza slots, and we are only interested in certs that share the same root
 * key as the server.
 */
CERTCertificate *
PK11_FindBestKEAMatch(CERTCertificate *server, void *wincx)
{
    PK11SlotList *keaList = PK11_GetAllTokens(CKM_KEA_KEY_DERIVE,
                                              PR_FALSE, PR_TRUE, wincx);
    PK11SlotListElement *le;
    CERTCertificate *returnedCert = NULL;
    SECStatus rv;

    if (!keaList) {
        /* error code is set */
        return NULL;
    }

    /* loop through all the fortezza tokens */
    for (le = keaList->head; le; le = le->next) {
        rv = PK11_Authenticate(le->slot, PR_TRUE, wincx);
        if (rv != SECSuccess)
            continue;
        if (le->slot->session == CK_INVALID_HANDLE) {
            continue;
        }
        returnedCert = pk11_GetKEAMate(le->slot, server);
        if (returnedCert)
            break;
    }
    PK11_FreeSlotList(keaList);

    return returnedCert;
}

CK_OBJECT_HANDLE
PK11_FindEncodedCertInSlot(PK11SlotInfo *slot, SECItem *derCert, void *wincx)
{
    if (!slot || !derCert) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    CK_OBJECT_CLASS certClass = CKO_CERTIFICATE;
    CK_ATTRIBUTE theTemplate[] = {
        { CKA_VALUE, NULL, 0 },
        { CKA_CLASS, NULL, 0 }
    };
    const size_t tsize = sizeof(theTemplate) / sizeof(theTemplate[0]);
    CK_ATTRIBUTE *attrs = theTemplate;

    PK11_SETATTRS(attrs, CKA_VALUE, derCert->data, derCert->len);
    attrs++;
    PK11_SETATTRS(attrs, CKA_CLASS, &certClass, sizeof(certClass));

    SECStatus rv = pk11_AuthenticateUnfriendly(slot, PR_TRUE, wincx);
    if (rv != SECSuccess) {
        return CK_INVALID_HANDLE;
    }

    return pk11_FindObjectByTemplate(slot, theTemplate, tsize);
}

CK_OBJECT_HANDLE
PK11_FindCertInSlot(PK11SlotInfo *slot, CERTCertificate *cert, void *wincx)
{
    CK_OBJECT_HANDLE certh;

    if (cert->slot == slot) {
        certh = cert->pkcs11ID;
        if ((certh == CK_INVALID_HANDLE) ||
            (cert->series != slot->series)) {
            certh = PK11_FindEncodedCertInSlot(slot, &cert->derCert, wincx);
            cert->pkcs11ID = certh;
            cert->series = slot->series;
        }
    } else {
        certh = PK11_FindEncodedCertInSlot(slot, &cert->derCert, wincx);
    }
    return certh;
}

/* Looking for PK11_GetKeyIDFromCert?
 * Use PK11_GetLowLevelKeyIDForCert instead.
 */

struct listCertsStr {
    PK11CertListType type;
    CERTCertList *certList;
};

static PRStatus
pk11ListCertCallback(NSSCertificate *c, void *arg)
{
    struct listCertsStr *listCertP = (struct listCertsStr *)arg;
    CERTCertificate *newCert = NULL;
    PK11CertListType type = listCertP->type;
    CERTCertList *certList = listCertP->certList;
    PRBool isUnique = PR_FALSE;
    PRBool isCA = PR_FALSE;
    char *nickname = NULL;
    unsigned int certType;
    SECStatus rv;

    if ((type == PK11CertListUnique) || (type == PK11CertListRootUnique) ||
        (type == PK11CertListCAUnique) || (type == PK11CertListUserUnique)) {
        /* only list one instance of each certificate, even if several exist */
        isUnique = PR_TRUE;
    }
    if ((type == PK11CertListCA) || (type == PK11CertListRootUnique) ||
        (type == PK11CertListCAUnique)) {
        isCA = PR_TRUE;
    }

    /* if we want user certs and we don't have one skip this cert */
    if (((type == PK11CertListUser) || (type == PK11CertListUserUnique)) &&
        !NSSCertificate_IsPrivateKeyAvailable(c, NULL, NULL)) {
        return PR_SUCCESS;
    }

    /* PK11CertListRootUnique means we want CA certs without a private key.
     * This is for legacy app support . PK11CertListCAUnique should be used
     * instead to get all CA certs, regardless of private key
     */
    if ((type == PK11CertListRootUnique) &&
        NSSCertificate_IsPrivateKeyAvailable(c, NULL, NULL)) {
        return PR_SUCCESS;
    }

    /* caller still owns the reference to 'c' */
    newCert = STAN_GetCERTCertificate(c);
    if (!newCert) {
        return PR_SUCCESS;
    }
    /* if we want CA certs and it ain't one, skip it */
    if (isCA && (!CERT_IsCACert(newCert, &certType))) {
        return PR_SUCCESS;
    }
    if (isUnique) {
        CERT_DupCertificate(newCert);

        nickname = STAN_GetCERTCertificateName(certList->arena, c);

        /* put slot certs at the end */
        if (newCert->slot && !PK11_IsInternal(newCert->slot)) {
            rv = CERT_AddCertToListTailWithData(certList, newCert, nickname);
        } else {
            rv = CERT_AddCertToListHeadWithData(certList, newCert, nickname);
        }
        /* if we didn't add the cert to the list, don't leak it */
        if (rv != SECSuccess) {
            CERT_DestroyCertificate(newCert);
        }
    } else {
        /* add multiple instances to the cert list */
        nssCryptokiObject **ip;
        nssCryptokiObject **instances = nssPKIObject_GetInstances(&c->object);
        if (!instances) {
            return PR_SUCCESS;
        }
        for (ip = instances; *ip; ip++) {
            nssCryptokiObject *instance = *ip;
            PK11SlotInfo *slot = instance->token->pk11slot;

            /* put the same CERTCertificate in the list for all instances */
            CERT_DupCertificate(newCert);

            nickname = STAN_GetCERTCertificateNameForInstance(
                certList->arena, c, instance);

            /* put slot certs at the end */
            if (slot && !PK11_IsInternal(slot)) {
                rv = CERT_AddCertToListTailWithData(certList, newCert, nickname);
            } else {
                rv = CERT_AddCertToListHeadWithData(certList, newCert, nickname);
            }
            /* if we didn't add the cert to the list, don't leak it */
            if (rv != SECSuccess) {
                CERT_DestroyCertificate(newCert);
            }
        }
        nssCryptokiObjectArray_Destroy(instances);
    }
    return PR_SUCCESS;
}

CERTCertList *
PK11_ListCerts(PK11CertListType type, void *pwarg)
{
    NSSTrustDomain *defaultTD = STAN_GetDefaultTrustDomain();
    CERTCertList *certList = NULL;
    struct listCertsStr listCerts;
    certList = CERT_NewCertList();
    listCerts.type = type;
    listCerts.certList = certList;

    /* authenticate to the slots */
    (void)pk11_TraverseAllSlots(NULL, NULL, PR_TRUE, pwarg);
    NSSTrustDomain_TraverseCertificates(defaultTD, pk11ListCertCallback,
                                        &listCerts);
    return certList;
}

SECItem *
PK11_GetLowLevelKeyIDForCert(PK11SlotInfo *slot,
                             CERTCertificate *cert, void *wincx)
{
    CK_OBJECT_HANDLE certHandle;
    PK11SlotInfo *slotRef = NULL;
    SECItem *item;

    if (slot) {
        certHandle = PK11_FindCertInSlot(slot, cert, wincx);
    } else {
        certHandle = PK11_FindObjectForCert(cert, wincx, &slotRef);
        if (certHandle == CK_INVALID_HANDLE) {
            return pk11_mkcertKeyID(cert);
        }
        slot = slotRef;
    }

    if (certHandle == CK_INVALID_HANDLE) {
        return NULL;
    }

    item = pk11_GetLowLevelKeyFromHandle(slot, certHandle);
    if (slotRef)
        PK11_FreeSlot(slotRef);
    return item;
}

/* argument type for listCertsCallback */
typedef struct {
    CERTCertList *list;
    PK11SlotInfo *slot;
} ListCertsArg;

static SECStatus
listCertsCallback(CERTCertificate *cert, void *arg)
{
    ListCertsArg *cdata = (ListCertsArg *)arg;
    char *nickname = NULL;
    nssCryptokiObject *instance, **ci;
    nssCryptokiObject **instances;
    NSSCertificate *c = STAN_GetNSSCertificate(cert);
    SECStatus rv;

    if (c == NULL) {
        return SECFailure;
    }
    instances = nssPKIObject_GetInstances(&c->object);
    if (!instances) {
        return SECFailure;
    }
    instance = NULL;
    for (ci = instances; *ci; ci++) {
        if ((*ci)->token->pk11slot == cdata->slot) {
            instance = *ci;
            break;
        }
    }
    PORT_Assert(instance != NULL);
    if (!instance) {
        nssCryptokiObjectArray_Destroy(instances);
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    nickname = STAN_GetCERTCertificateNameForInstance(cdata->list->arena,
                                                      c, instance);
    nssCryptokiObjectArray_Destroy(instances);

    CERT_DupCertificate(cert);
    rv = CERT_AddCertToListTailWithData(cdata->list, cert, nickname);
    if (rv != SECSuccess) {
        CERT_DestroyCertificate(cert);
    }
    return rv;
}

CERTCertList *
PK11_ListCertsInSlot(PK11SlotInfo *slot)
{
    SECStatus status;
    CERTCertList *certs;
    ListCertsArg cdata;

    certs = CERT_NewCertList();
    if (certs == NULL)
        return NULL;
    cdata.list = certs;
    cdata.slot = slot;

    status = PK11_TraverseCertsInSlot(slot, listCertsCallback,
                                      &cdata);

    if (status != SECSuccess) {
        CERT_DestroyCertList(certs);
        certs = NULL;
    }

    return certs;
}

PK11SlotList *
PK11_GetAllSlotsForCert(CERTCertificate *cert, void *arg)
{
    nssCryptokiObject **ip;
    PK11SlotList *slotList;
    NSSCertificate *c;
    nssCryptokiObject **instances;
    PRBool found = PR_FALSE;

    if (!cert) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    c = STAN_GetNSSCertificate(cert);
    if (!c) {
        CERT_MapStanError();
        return NULL;
    }

    /* add multiple instances to the cert list */
    instances = nssPKIObject_GetInstances(&c->object);
    if (!instances) {
        PORT_SetError(SEC_ERROR_NO_TOKEN);
        return NULL;
    }

    slotList = PK11_NewSlotList();
    if (!slotList) {
        nssCryptokiObjectArray_Destroy(instances);
        return NULL;
    }

    for (ip = instances; *ip; ip++) {
        nssCryptokiObject *instance = *ip;
        PK11SlotInfo *slot = instance->token->pk11slot;
        if (slot) {
            PK11_AddSlotToList(slotList, slot, PR_TRUE);
            found = PR_TRUE;
        }
    }
    if (!found) {
        PK11_FreeSlotList(slotList);
        PORT_SetError(SEC_ERROR_NO_TOKEN);
        slotList = NULL;
    }

    nssCryptokiObjectArray_Destroy(instances);
    return slotList;
}

/*
 * Using __PK11_SetCertificateNickname is *DANGEROUS*.
 *
 * The API will update the NSS database, but it *will NOT* update the in-memory data.
 * As a result, after calling this API, there will be INCONSISTENCY between
 * in-memory data and the database.
 *
 * Use of the API should be limited to short-lived tools, which will exit immediately
 * after using this API.
 *
 * If you ignore this warning, your process is TAINTED and will most likely misbehave.
 */
SECStatus
__PK11_SetCertificateNickname(CERTCertificate *cert, const char *nickname)
{
    /* Can't set nickname of temp cert. */
    if (!cert->slot || cert->pkcs11ID == CK_INVALID_HANDLE) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    return PK11_SetObjectNickname(cert->slot, cert->pkcs11ID, nickname);
}
