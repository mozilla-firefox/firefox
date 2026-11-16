/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Hacks to integrate NSS 3.4 and NSS 4.0 certificates.
 */

#ifndef NSSPKI_H
#include "nsspki.h"
#endif /* NSSPKI_H */

#ifndef PKI_H
#include "pki.h"
#endif /* PKI_H */

#ifndef PKIM_H
#include "pkim.h"
#endif /* PKIM_H */

#ifndef DEV_H
#include "dev.h"
#endif /* DEV_H */

#ifndef DEVNSS3HACK_H
#include "dev3hack.h"
#endif /* DEVNSS3HACK_H */

#ifndef PKINSS3HACK_H
#include "pki3hack.h"
#endif /* PKINSS3HACK_H */

#include "cert.h"
#include "certdb.h"
#include "certi.h"
#include "certt.h"
#include "nssrwlk.h"
#include "pk11func.h"
#include "pkistore.h"
#include "secitem.h"
#include "secmod.h"
#include "secmodi.h"

NSSTrustDomain *g_default_trust_domain = NULL;

NSSCryptoContext *g_default_crypto_context = NULL;

NSSTrustDomain *
STAN_GetDefaultTrustDomain()
{
    return g_default_trust_domain;
}

NSSCryptoContext *
STAN_GetDefaultCryptoContext()
{
    return g_default_crypto_context;
}

extern const NSSError NSS_ERROR_ALREADY_INITIALIZED;
extern const NSSError NSS_ERROR_INTERNAL_ERROR;

NSS_IMPLEMENT PRStatus
STAN_InitTokenForSlotInfo(NSSTrustDomain *td, PK11SlotInfo *slot)
{
    NSSToken *token;
    if (!td) {
        td = g_default_trust_domain;
        if (!td) {
            /* we're called while still initting. slot will get added
             * appropriately through normal init processes */
            return PR_SUCCESS;
        }
    }
    token = nssToken_CreateFromPK11SlotInfo(td, slot);
    if (token) {
        /* PK11Slot_SetNSSToken increments the refcount on |token| to 2 */
        PK11Slot_SetNSSToken(slot, token);

        /* we give our reference to |td->tokenList| */
        NSSRWLock_LockWrite(td->tokensLock);
        nssList_Add(td->tokenList, token);
        NSSRWLock_UnlockWrite(td->tokensLock);
    } else {
        PK11Slot_SetNSSToken(slot, NULL);
    }
    return PR_SUCCESS;
}

NSS_IMPLEMENT PRStatus
STAN_ResetTokenInterator(NSSTrustDomain *td)
{
    if (!td) {
        td = g_default_trust_domain;
        if (!td) {
            /* we're called while still initting. slot will get added
             * appropriately through normal init processes */
            return PR_SUCCESS;
        }
    }
    NSSRWLock_LockWrite(td->tokensLock);
    nssListIterator_Destroy(td->tokens);
    td->tokens = nssList_CreateIterator(td->tokenList);
    NSSRWLock_UnlockWrite(td->tokensLock);
    return PR_SUCCESS;
}

NSS_IMPLEMENT PRStatus
STAN_LoadDefaultNSS3TrustDomain(
    void)
{
    NSSTrustDomain *td;
    SECMODModuleList *mlp;
    SECMODListLock *moduleLock = SECMOD_GetDefaultModuleListLock();
    int i;

    if (g_default_trust_domain || g_default_crypto_context) {
        /* Stan is already initialized or a previous shutdown failed. */
        nss_SetError(NSS_ERROR_ALREADY_INITIALIZED);
        return PR_FAILURE;
    }
    td = NSSTrustDomain_Create(NULL, NULL, NULL, NULL);
    if (!td) {
        return PR_FAILURE;
    }
    /*
     * Deadlock warning: we should never acquire the moduleLock while
     * we hold the tokensLock. We can use the NSSRWLock Rank feature to
     * guarrentee this. tokensLock have a higher rank than module lock.
     */
    td->tokenList = nssList_Create(td->arena, PR_TRUE);
    if (!td->tokenList) {
        goto loser;
    }
    SECMOD_GetReadLock(moduleLock);
    NSSRWLock_LockWrite(td->tokensLock);
    for (mlp = SECMOD_GetDefaultModuleList(); mlp != NULL; mlp = mlp->next) {
        for (i = 0; i < mlp->module->slotCount; i++) {
            STAN_InitTokenForSlotInfo(td, mlp->module->slots[i]);
        }
    }
    td->tokens = nssList_CreateIterator(td->tokenList);
    NSSRWLock_UnlockWrite(td->tokensLock);
    SECMOD_ReleaseReadLock(moduleLock);
    if (!td->tokens) {
        goto loser;
    }
    g_default_crypto_context = NSSTrustDomain_CreateCryptoContext(td, NULL);
    if (!g_default_crypto_context) {
        goto loser;
    }
    g_default_trust_domain = td;
    return PR_SUCCESS;

loser:
    NSSTrustDomain_Destroy(td);
    return PR_FAILURE;
}

/*
 * must be called holding the ModuleListLock (either read or write).
 */
NSS_IMPLEMENT SECStatus
STAN_AddModuleToDefaultTrustDomain(
    SECMODModule *module)
{
    NSSTrustDomain *td;
    int i;
    td = STAN_GetDefaultTrustDomain();
    for (i = 0; i < module->slotCount; i++) {
        STAN_InitTokenForSlotInfo(td, module->slots[i]);
    }
    STAN_ResetTokenInterator(td);
    return SECSuccess;
}

/*
 * must be called holding the ModuleListLock (either read or write).
 */
NSS_IMPLEMENT SECStatus
STAN_RemoveModuleFromDefaultTrustDomain(
    SECMODModule *module)
{
    NSSToken *token;
    NSSTrustDomain *td;
    int i;
    td = STAN_GetDefaultTrustDomain();
    for (i = 0; i < module->slotCount; i++) {
        token = PK11Slot_GetNSSToken(module->slots[i]);
        if (token) {
            nssToken_NotifyCertsNotVisible(token);
            NSSRWLock_LockWrite(td->tokensLock);
            nssList_Remove(td->tokenList, token);
            /* Rebuild the td->tokens iterator clone while still holding the
             * write lock, so that concurrent readers cannot observe the token
             * through a stale iterator after we drop the lock and free it. */
            nssListIterator_Destroy(td->tokens);
            td->tokens = nssList_CreateIterator(td->tokenList);
            NSSRWLock_UnlockWrite(td->tokensLock);
            PK11Slot_SetNSSToken(module->slots[i], NULL);
            (void)nssToken_Destroy(token); /* for the |td->tokenList| reference */
            (void)nssToken_Destroy(token); /* for our PK11Slot_GetNSSToken reference */
        }
    }
    return SECSuccess;
}

NSS_IMPLEMENT PRStatus
STAN_Shutdown()
{
    PRStatus status = PR_SUCCESS;
    if (g_default_trust_domain) {
        if (NSSTrustDomain_Destroy(g_default_trust_domain) == PR_SUCCESS) {
            g_default_trust_domain = NULL;
        } else {
            status = PR_FAILURE;
        }
    }
    if (g_default_crypto_context) {
        if (NSSCryptoContext_Destroy(g_default_crypto_context) == PR_SUCCESS) {
            g_default_crypto_context = NULL;
        } else {
            status = PR_FAILURE;
        }
    }
    return status;
}

NSS_IMPLEMENT PRStatus
nssPKIX509_GetIssuerAndSerialFromDER(NSSDER *der,
                                     NSSDER *issuer, NSSDER *serial)
{
    SECItem derCert = { 0 };
    SECItem derIssuer = { 0 };
    SECItem derSerial = { 0 };
    SECStatus secrv;
    derCert.data = (unsigned char *)der->data;
    derCert.len = der->size;
    secrv = CERT_IssuerNameFromDERCert(&derCert, &derIssuer);
    if (secrv != SECSuccess) {
        return PR_FAILURE;
    }
    secrv = CERT_SerialNumberFromDERCert(&derCert, &derSerial);
    if (secrv != SECSuccess) {
        PORT_Free(derIssuer.data);
        return PR_FAILURE;
    }
    issuer->data = derIssuer.data;
    issuer->size = derIssuer.len;
    serial->data = derSerial.data;
    serial->size = derSerial.len;
    return PR_SUCCESS;
}

static NSSItem *
nss3certificate_getIdentifier(nssDecodedCert *dc)
{
    NSSItem *rvID;
    CERTCertificate *c = (CERTCertificate *)dc->data;
    rvID = nssItem_Create(NULL, NULL, c->certKey.len, c->certKey.data);
    return rvID;
}

static void *
nss3certificate_getIssuerIdentifier(nssDecodedCert *dc)
{
    CERTCertificate *c = (CERTCertificate *)dc->data;
    return (void *)c->authKeyID;
}

static nssCertIDMatch
nss3certificate_matchIdentifier(nssDecodedCert *dc, void *id)
{
    CERTCertificate *c = (CERTCertificate *)dc->data;
    CERTAuthKeyID *authKeyID = (CERTAuthKeyID *)id;
    SECItem skid;
    nssCertIDMatch match = nssCertIDMatch_Unknown;

    /* keyIdentifier */
    if (authKeyID->keyID.len > 0 &&
        CERT_FindSubjectKeyIDExtension(c, &skid) == SECSuccess) {
        PRBool skiEqual;
        skiEqual = SECITEM_ItemsAreEqual(&authKeyID->keyID, &skid);
        PORT_Free(skid.data);
        if (skiEqual) {
            /* change the state to positive match, but keep going */
            match = nssCertIDMatch_Yes;
        } else {
            /* exit immediately on failure */
            return nssCertIDMatch_No;
        }
    }

    /* issuer/serial (treated as pair) */
    if (authKeyID->authCertIssuer) {
        SECItem *caName = NULL;
        SECItem *caSN = &authKeyID->authCertSerialNumber;

        caName = (SECItem *)CERT_GetGeneralNameByType(
            authKeyID->authCertIssuer,
            certDirectoryName, PR_TRUE);
        if (caName != NULL &&
            SECITEM_ItemsAreEqual(&c->derIssuer, caName) &&
            SECITEM_ItemsAreEqual(&c->serialNumber, caSN)) {
            match = nssCertIDMatch_Yes;
        } else {
            match = nssCertIDMatch_Unknown;
        }
    }
    return match;
}

static PRBool
nss3certificate_isValidIssuer(nssDecodedCert *dc)
{
    CERTCertificate *c = (CERTCertificate *)dc->data;
    unsigned int ignore;
    return CERT_IsCACert(c, &ignore);
}

static NSSUsage *
nss3certificate_getUsage(nssDecodedCert *dc)
{
    /* CERTCertificate *c = (CERTCertificate *)dc->data; */
    return NULL;
}

static PRBool
nss3certificate_isValidAtTime(nssDecodedCert *dc, NSSTime *time)
{
    SECCertTimeValidity validity;
    CERTCertificate *c = (CERTCertificate *)dc->data;
    validity = CERT_CheckCertValidTimes(c, NSSTime_GetPRTime(time), PR_TRUE);
    if (validity == secCertTimeValid) {
        return PR_TRUE;
    }
    return PR_FALSE;
}

static PRBool
nss3certificate_isNewerThan(nssDecodedCert *dc, nssDecodedCert *cmpdc)
{
    /* I know this isn't right, but this is glue code anyway */
    if (cmpdc->type == dc->type) {
        CERTCertificate *certa = (CERTCertificate *)dc->data;
        CERTCertificate *certb = (CERTCertificate *)cmpdc->data;
        return CERT_IsNewer(certa, certb);
    }
    return PR_FALSE;
}

/* CERT_FilterCertListByUsage */
static PRBool
nss3certificate_matchUsage(nssDecodedCert *dc, const NSSUsage *usage)
{
    CERTCertificate *cc;
    unsigned int requiredKeyUsage = 0;
    unsigned int requiredCertType = 0;
    SECStatus secrv;
    PRBool match;
    PRBool ca;

    /* This is for NSS 3.3 functions that do not specify a usage */
    if (usage->anyUsage) {
        return PR_TRUE;
    }
    ca = usage->nss3lookingForCA;
    secrv = CERT_KeyUsageAndTypeForCertUsage(usage->nss3usage, ca,
                                             &requiredKeyUsage,
                                             &requiredCertType);
    if (secrv != SECSuccess) {
        return PR_FALSE;
    }
    cc = (CERTCertificate *)dc->data;
    secrv = CERT_CheckKeyUsage(cc, requiredKeyUsage);
    match = (PRBool)(secrv == SECSuccess);
    if (match) {
        unsigned int certType = 0;
        if (ca) {
            (void)CERT_IsCACert(cc, &certType);
        } else {
            certType = cc->nsCertType;
        }
        if (!(certType & requiredCertType)) {
            match = PR_FALSE;
        }
    }
    return match;
}

static PRBool
nss3certificate_isTrustedForUsage(nssDecodedCert *dc, const NSSUsage *usage)
{
    CERTCertificate *cc;
    PRBool ca;
    SECStatus secrv;
    unsigned int requiredFlags;
    unsigned int trustFlags;
    SECTrustType trustType;
    CERTCertTrust trust;

    /* This is for NSS 3.3 functions that do not specify a usage */
    if (usage->anyUsage) {
        return PR_FALSE; /* XXX is this right? */
    }
    cc = (CERTCertificate *)dc->data;
    ca = usage->nss3lookingForCA;
    if (!ca) {
        PRBool trusted;
        unsigned int failedFlags;
        secrv = cert_CheckLeafTrust(cc, usage->nss3usage,
                                    &failedFlags, &trusted);
        return secrv == SECSuccess && trusted;
    }
    secrv = CERT_TrustFlagsForCACertUsage(usage->nss3usage, &requiredFlags,
                                          &trustType);
    if (secrv != SECSuccess) {
        return PR_FALSE;
    }
    secrv = CERT_GetCertTrust(cc, &trust);
    if (secrv != SECSuccess) {
        return PR_FALSE;
    }
    if (trustType == trustTypeNone) {
        /* normally trustTypeNone usages accept any of the given trust bits
         * being on as acceptable. */
        trustFlags = trust.sslFlags | trust.emailFlags |
                     trust.objectSigningFlags;
    } else {
        trustFlags = SEC_GET_TRUST_FLAGS(&trust, trustType);
    }
    return (trustFlags & requiredFlags) == requiredFlags;
}

static NSSASCII7 *
nss3certificate_getEmailAddress(nssDecodedCert *dc)
{
    CERTCertificate *cc = (CERTCertificate *)dc->data;
    return (cc && cc->emailAddr && cc->emailAddr[0])
               ? (NSSASCII7 *)cc->emailAddr
               : NULL;
}

static PRStatus
nss3certificate_getDERSerialNumber(nssDecodedCert *dc,
                                   NSSDER *serial, NSSArena *arena)
{
    CERTCertificate *cc = (CERTCertificate *)dc->data;
    SECItem derSerial = { 0 };
    SECStatus secrv;
    secrv = CERT_SerialNumberFromDERCert(&cc->derCert, &derSerial);
    if (secrv == SECSuccess) {
        (void)nssItem_Create(arena, serial, derSerial.len, derSerial.data);
        PORT_Free(derSerial.data);
        return PR_SUCCESS;
    }
    return PR_FAILURE;
}

/* Returns NULL if "encoding" cannot be decoded. */
NSS_IMPLEMENT nssDecodedCert *
nssDecodedPKIXCertificate_Create(
    NSSArena *arenaOpt,
    NSSDER *encoding)
{
    nssDecodedCert *rvDC = NULL;
    CERTCertificate *cert;
    SECItem secDER;

    SECITEM_FROM_NSSITEM(&secDER, encoding);
    cert = CERT_DecodeDERCertificate(&secDER, PR_TRUE, NULL);
    if (cert) {
        rvDC = nss_ZNEW(arenaOpt, nssDecodedCert);
        if (rvDC) {
            rvDC->type = NSSCertificateType_PKIX;
            rvDC->data = (void *)cert;
            rvDC->getIdentifier = nss3certificate_getIdentifier;
            rvDC->getIssuerIdentifier = nss3certificate_getIssuerIdentifier;
            rvDC->matchIdentifier = nss3certificate_matchIdentifier;
            rvDC->isValidIssuer = nss3certificate_isValidIssuer;
            rvDC->getUsage = nss3certificate_getUsage;
            rvDC->isValidAtTime = nss3certificate_isValidAtTime;
            rvDC->isNewerThan = nss3certificate_isNewerThan;
            rvDC->matchUsage = nss3certificate_matchUsage;
            rvDC->isTrustedForUsage = nss3certificate_isTrustedForUsage;
            rvDC->getEmailAddress = nss3certificate_getEmailAddress;
            rvDC->getDERSerialNumber = nss3certificate_getDERSerialNumber;
        } else {
            CERT_DestroyCertificate(cert);
        }
    }
    return rvDC;
}

static nssDecodedCert *
create_decoded_pkix_cert_from_nss3cert(
    NSSArena *arenaOpt,
    CERTCertificate *cc)
{
    nssDecodedCert *rvDC = nss_ZNEW(arenaOpt, nssDecodedCert);
    if (rvDC) {
        rvDC->type = NSSCertificateType_PKIX;
        rvDC->data = (void *)cc;
        rvDC->getIdentifier = nss3certificate_getIdentifier;
        rvDC->getIssuerIdentifier = nss3certificate_getIssuerIdentifier;
        rvDC->matchIdentifier = nss3certificate_matchIdentifier;
        rvDC->isValidIssuer = nss3certificate_isValidIssuer;
        rvDC->getUsage = nss3certificate_getUsage;
        rvDC->isValidAtTime = nss3certificate_isValidAtTime;
        rvDC->isNewerThan = nss3certificate_isNewerThan;
        rvDC->matchUsage = nss3certificate_matchUsage;
        rvDC->isTrustedForUsage = nss3certificate_isTrustedForUsage;
        rvDC->getEmailAddress = nss3certificate_getEmailAddress;
        rvDC->getDERSerialNumber = nss3certificate_getDERSerialNumber;
    }
    return rvDC;
}

NSS_IMPLEMENT PRStatus
nssDecodedPKIXCertificate_Destroy(nssDecodedCert *dc)
{
    CERTCertificate *cert = (CERTCertificate *)dc->data;

    /* The decoder may only be half initialized (the case where we find we
     * could not decode the certificate). In this case, there is not cert to
     * free, just free the dc structure. */
    if (cert) {
        PRBool freeSlot = cert->ownSlot;
        PK11SlotInfo *slot = cert->slot;
        PLArenaPool *arena = cert->arena;
        /* zero cert before freeing. Any stale references to this cert
         * after this point will probably cause an exception.  */
        PORT_Memset(cert, 0, sizeof *cert);
        /* free the arena that contains the cert. */
        PORT_FreeArena(arena, PR_FALSE);
        if (slot && freeSlot) {
            PK11_FreeSlot(slot);
        }
    }
    nss_ZFreeIf(dc);
    return PR_SUCCESS;
}

static CERTCertTrust *
cert_trust_from_stan_trust(NSSTrust *t, PLArenaPool *arena)
{
    if (!t || !arena) {
        return NULL;
    }
    CERTCertTrust *rvTrust = PORT_ArenaAlloc(arena, sizeof(CERTCertTrust));
    if (!rvTrust) {
        return NULL;
    }
    nssTrust_ToCERTCertTrust(t, rvTrust);
    return rvTrust;
}

PRBool
nssTrust_HandleTrustForCERTCert(CERTCertificate *cert, CERTCertTrust *trustPtr)
{
    NSSCertificate *c = cert->nssCertificate;
    NSSTrustDomain *td = STAN_GetDefaultTrustDomain();
    NSSTrust *t = nssTrustDomain_FindTrustForCertificate(td, &c->encoding, &c->issuer, &c->serial);
    if (t) {
        CERTCertTrust *rvTrust = cert_trust_from_stan_trust(t, cert->arena);
        nssTrust_Destroy(t);
        if (rvTrust) {
            *trustPtr = *rvTrust;
            return PR_TRUE;
        }
    }
    return PR_FALSE;
}

CERTCertTrust *
nssTrust_GetCERTCertTrustForCert(NSSCertificate *c, CERTCertificate *cc)
{
    CERTCertTrust *rvTrust = NULL;
    NSSTrustDomain *td = STAN_GetDefaultTrustDomain();
    NSSTrust *t = nssTrustDomain_FindTrustForCertificate(td, &c->encoding, &c->issuer, &c->serial);
    if (t) {
        rvTrust = cert_trust_from_stan_trust(t, cc->arena);
        if (!rvTrust) {
            nssTrust_Destroy(t);
            return NULL;
        }
        nssTrust_Destroy(t);
    } else {
        rvTrust = PORT_ArenaAlloc(cc->arena, sizeof(CERTCertTrust));
        if (!rvTrust) {
            return NULL;
        }
        memset(rvTrust, 0, sizeof(*rvTrust));
    }
    if (NSSCertificate_IsPrivateKeyAvailable(c, NULL, NULL)) {
        rvTrust->sslFlags |= CERTDB_USER;
        rvTrust->emailFlags |= CERTDB_USER;
        rvTrust->objectSigningFlags |= CERTDB_USER;
    }
    return rvTrust;
}

static nssCryptokiInstance *
get_cert_instance(NSSCertificate *c)
{
    nssCryptokiObject *instance, **ci;
    nssCryptokiObject **instances = nssPKIObject_GetInstances(&c->object);
    if (!instances) {
        return NULL;
    }
    instance = NULL;
    for (ci = instances; *ci; ci++) {
        if (!instance) {
            instance = nssCryptokiObject_Clone(*ci);
        } else {
            /* This only really works for two instances...  But 3.4 can't
             * handle more anyway.  The logic is, if there are multiple
             * instances, prefer the one that is not internal (e.g., on
             * a hardware device.
             */
            if (PK11_IsInternal(instance->token->pk11slot)) {
                nssCryptokiObject_Destroy(instance);
                instance = nssCryptokiObject_Clone(*ci);
            }
        }
    }
    nssCryptokiObjectArray_Destroy(instances);
    return instance;
}

char *
STAN_GetCERTCertificateNameForInstance(
    PLArenaPool *arenaOpt,
    NSSCertificate *c,
    nssCryptokiInstance *instance)
{
    nssPKIObject_Lock(&c->object);
    NSSCryptoContext *context = c->object.cryptoContext;
    nssPKIObject_Unlock(&c->object);
    PRStatus nssrv;
    int nicklen, tokenlen, len;
    NSSUTF8 *tokenName = NULL;
    NSSUTF8 *stanNick = NULL;
    char *nickname = NULL;
    char *nick;

    if (instance) {
        stanNick = instance->label;
    } else if (context) {
        stanNick = c->object.tempName;
    }
    if (stanNick) {
        /* fill other fields needed by NSS3 functions using CERTCertificate */
        if (instance && (!PK11_IsInternalKeySlot(instance->token->pk11slot) ||
                         PORT_Strchr(stanNick, ':') != NULL)) {
            tokenName = nssToken_GetName(instance->token);
            tokenlen = nssUTF8_Size(tokenName, &nssrv);
        } else {
            /* don't use token name for internal slot; 3.3 didn't */
            tokenlen = 0;
        }
        nicklen = nssUTF8_Size(stanNick, &nssrv);
        len = tokenlen + nicklen;
        if (arenaOpt) {
            nickname = PORT_ArenaAlloc(arenaOpt, len);
        } else {
            nickname = PORT_Alloc(len);
        }
        nick = nickname;
        if (tokenName) {
            memcpy(nick, tokenName, tokenlen - 1);
            nick += tokenlen - 1;
            *nick++ = ':';
        }
        memcpy(nick, stanNick, nicklen - 1);
        nickname[len - 1] = '\0';
    }

    return nickname;
}

char *
STAN_GetCERTCertificateName(PLArenaPool *arenaOpt, NSSCertificate *c)
{
    char *result;
    nssCryptokiInstance *instance = get_cert_instance(c);
    /* It's OK to call this function, even if instance is NULL */
    result = STAN_GetCERTCertificateNameForInstance(arenaOpt, c, instance);
    if (instance)
        nssCryptokiObject_Destroy(instance);
    return result;
}

static void
fill_CERTCertificateFields(NSSCertificate *c, CERTCertificate *cc, NSSTrust *ccTrust, PRBool forced)
{
    /* We are holding the base class object's lock on entry of this function.
     * This lock protects writes to fields of the CERTCertificate.
     * It is also needed by some functions to compute values such as trust.
     */
    CERTCertTrust *trust = NULL;
    NSSCryptoContext *context = c->object.cryptoContext;
    nssCryptokiInstance *instance;
    NSSUTF8 *stanNick = NULL;

    instance = get_cert_instance(c);

    if (instance) {
        stanNick = instance->label;
    } else if (context) {
        stanNick = c->object.tempName;
    }
    /* fill other fields needed by NSS3 functions using CERTCertificate */
    if ((!cc->nickname && stanNick) || forced) {
        PRStatus nssrv;
        int nicklen, tokenlen, len;
        NSSUTF8 *tokenName = NULL;
        char *nick;
        if (instance &&
            (!PK11_IsInternalKeySlot(instance->token->pk11slot) ||
             (stanNick && PORT_Strchr(stanNick, ':') != NULL))) {
            tokenName = nssToken_GetName(instance->token);
            tokenlen = nssUTF8_Size(tokenName, &nssrv);
        } else {
            /* don't use token name for internal slot; 3.3 didn't */
            tokenlen = 0;
        }
        if (stanNick) {
            nicklen = nssUTF8_Size(stanNick, &nssrv);
            len = tokenlen + nicklen;
            nick = PORT_ArenaAlloc(cc->arena, len);
            if (tokenName) {
                memcpy(nick, tokenName, tokenlen - 1);
                nick[tokenlen - 1] = ':';
                memcpy(nick + tokenlen, stanNick, nicklen - 1);
            } else {
                memcpy(nick, stanNick, nicklen - 1);
            }
            nick[len - 1] = '\0';
            cc->nickname = nick;
        } else {
            cc->nickname = NULL;
        }
    }
    if (context) {
        /* trust */
        NSSTrust *nssTrust = ccTrust;
        NSSTrust *tdTrust = NULL;
        if (!nssTrust) {
            NSSDER encoding;
            NSSITEM_FROM_SECITEM(&encoding, &cc->derCert);
            NSSDER issuer;
            NSSDER serial;
            if (nssPKIX509_GetIssuerAndSerialFromDER(&encoding, &issuer, &serial) == PR_SUCCESS) {
                nssTrust = tdTrust = nssTrustDomain_FindTrustForCertificate(context->td, &encoding, &issuer, &serial);
                PORT_Free(issuer.data);
                PORT_Free(serial.data);
            }
        }
        if (nssTrust) {
            trust = cert_trust_from_stan_trust(nssTrust, cc->arena);
            if (trust) {
                /* we should destroy cc->trust before replacing it, but it's
                   allocated in cc->arena, so memory growth will occur on each
                   refresh */
                CERT_LockCertTrust(cc);
                cc->trust = trust;
                CERT_UnlockCertTrust(cc);
            }
        }
        nssTrust_Destroy(tdTrust);
    } else if (instance) {
        /* slot */
        if (cc->slot != instance->token->pk11slot) {
            if (cc->slot) {
                PK11_FreeSlot(cc->slot);
            }
            cc->slot = PK11_ReferenceSlot(instance->token->pk11slot);
        }
        cc->ownSlot = PR_TRUE;
        /* pkcs11ID */
        cc->pkcs11ID = instance->handle;
        /* trust */
        trust = nssTrust_GetCERTCertTrustForCert(c, cc);
        if (trust) {
            /* we should destroy cc->trust before replacing it, but it's
               allocated in cc->arena, so memory growth will occur on each
               refresh */
            CERT_LockCertTrust(cc);
            cc->trust = trust;
            CERT_UnlockCertTrust(cc);
        }
        /* Read the distrust fields from a nssckbi/builtins certificate and
         * fill the fields in CERTCertificate structure when any valid date
         * is found. */
        if (PK11_IsReadOnly(cc->slot) && PK11_HasRootCerts(cc->slot)) {
            /* The values are hard-coded and readonly. Read just once. */
            if (cc->distrust == NULL) {
                CERTCertDistrust distrustModel;
                SECItem model = { siUTCTime, NULL, 0 };
                distrustModel.serverDistrustAfter = model;
                distrustModel.emailDistrustAfter = model;
                SECStatus rServer = PK11_ReadAttribute(
                    cc->slot, cc->pkcs11ID, CKA_NSS_SERVER_DISTRUST_AFTER,
                    cc->arena, &distrustModel.serverDistrustAfter);
                SECStatus rEmail = PK11_ReadAttribute(
                    cc->slot, cc->pkcs11ID, CKA_NSS_EMAIL_DISTRUST_AFTER,
                    cc->arena, &distrustModel.emailDistrustAfter);
                /* Only allocate the Distrust structure if a valid date is found.
                 * The result length of a encoded valid timestamp is exactly 13 */
                const unsigned int kDistrustFieldSize = 13;
                if ((rServer == SECSuccess && rEmail == SECSuccess) &&
                    (distrustModel.serverDistrustAfter.len == kDistrustFieldSize ||
                     distrustModel.emailDistrustAfter.len == kDistrustFieldSize)) {
                    CERTCertDistrust *tmpPtr = PORT_ArenaAlloc(
                        cc->arena, sizeof(CERTCertDistrust));
                    PORT_Memcpy(tmpPtr, &distrustModel,
                                sizeof(CERTCertDistrust));
                    cc->distrust = tmpPtr;
                }
            }
        }
    }
    if (instance) {
        nssCryptokiObject_Destroy(instance);
    }
    /* database handle is now the trust domain */
    cc->dbhandle = c->object.trustDomain;
    /* subjectList ? */
    /* istemp and isperm are supported in NSS 3.4 */
    CERT_LockCertTempPerm(cc);
    cc->istemp = PR_FALSE; /* CERT_NewTemp will override this */
    cc->isperm = PR_TRUE;  /* by default */
    /* pointer back */
    cc->nssCertificate = c;
    CERT_UnlockCertTempPerm(cc);
    if (trust) {
        /* force the cert type to be recomputed to include trust info */
        PRUint32 nsCertType = cert_ComputeCertType(cc);

        /* Assert that it is safe to cast &cc->nsCertType to "PRInt32 *" */
        PORT_Assert(sizeof(cc->nsCertType) == sizeof(PRInt32));
        PR_ATOMIC_SET((PRInt32 *)&cc->nsCertType, nsCertType);
    }
}

static CERTCertificate *
stan_GetCERTCertificate(NSSCertificate *c, PRBool forceUpdate)
{
    nssDecodedCert *dc = NULL;
    CERTCertificate *cc = NULL;
    CERTCertTrust certTrust;

    // Looking for trust information in a crypto context acquires the context's
    // cert store's lock. To avoid a lock order inversion, do this while not
    // holding this object's lock.
    NSSTrust *ccTrust = NULL;
    nssPKIObject_Lock(&c->object);
    NSSCryptoContext *context = c->object.cryptoContext;
    nssPKIObject_Unlock(&c->object);
    if (context) {
        ccTrust = nssCryptoContext_FindTrustForCertificate(context, c);
    }

    /* make sure object does not go away until we finish */
    nssPKIObject_AddRef(&c->object);
    nssPKIObject_Lock(&c->object);

    dc = c->decoding;
    if (!dc) {
        dc = nssDecodedPKIXCertificate_Create(NULL, &c->encoding);
        if (!dc) {
            goto loser;
        }
        cc = (CERTCertificate *)dc->data;
        PORT_Assert(cc); /* software error */
        if (!cc) {
            nssDecodedPKIXCertificate_Destroy(dc);
            nss_SetError(NSS_ERROR_INTERNAL_ERROR);
            goto loser;
        }
        PORT_Assert(!c->decoding);
        if (!c->decoding) {
            c->decoding = dc;
        } else {
            /* this should never happen. Fail. */
            nssDecodedPKIXCertificate_Destroy(dc);
            nss_SetError(NSS_ERROR_INTERNAL_ERROR);
            goto loser;
        }
    }
    cc = (CERTCertificate *)dc->data;
    PORT_Assert(cc);
    if (!cc) {
        nss_SetError(NSS_ERROR_INTERNAL_ERROR);
        goto loser;
    }
    CERT_LockCertTempPerm(cc);
    NSSCertificate *nssCert = cc->nssCertificate;
    CERT_UnlockCertTempPerm(cc);
    if (!nssCert || forceUpdate) {
        fill_CERTCertificateFields(c, cc, ccTrust, forceUpdate);
    } else if (CERT_GetCertTrust(cc, &certTrust) != SECSuccess) {
        CERTCertTrust *trust;
        if (!c->object.cryptoContext) {
            /* If it's a perm cert, it might have been stored before the
             * trust, so look for the trust again.
             */
            trust = nssTrust_GetCERTCertTrustForCert(c, cc);
        } else {
            /* If it's a temp cert, it might have been stored before the
             * builtin trust module is loaded, so look for the trust
             * again, but don't set the empty trust if it is not found.
             */
            NSSTrust *t = nssTrustDomain_FindTrustForCertificate(c->object.cryptoContext->td, &c->encoding, &c->issuer, &c->serial);
            if (!t) {
                goto loser;
            }
            trust = cert_trust_from_stan_trust(t, cc->arena);
            nssTrust_Destroy(t);
            if (!trust) {
                goto loser;
            }
        }

        CERT_LockCertTrust(cc);
        cc->trust = trust;
        CERT_UnlockCertTrust(cc);
    }

loser:
    nssPKIObject_Unlock(&c->object);
    nssPKIObject_Destroy(&c->object);
    nssTrust_Destroy(ccTrust);
    return cc;
}

NSS_IMPLEMENT CERTCertificate *
STAN_ForceCERTCertificateUpdate(NSSCertificate *c)
{
    if (c->decoding) {
        return stan_GetCERTCertificate(c, PR_TRUE);
    }
    return NULL;
}

NSS_IMPLEMENT CERTCertificate *
STAN_GetCERTCertificate(NSSCertificate *c)
{
    return stan_GetCERTCertificate(c, PR_FALSE);
}
/*
 * many callers of STAN_GetCERTCertificate() intend that
 * the CERTCertificate returned inherits the reference to the
 * NSSCertificate. For these callers it's convenient to have
 * this function 'own' the reference and either return a valid
 * CERTCertificate structure which inherits the reference or
 * destroy the reference to NSSCertificate and returns NULL.
 */
NSS_IMPLEMENT CERTCertificate *
STAN_GetCERTCertificateOrRelease(NSSCertificate *c)
{
    CERTCertificate *nss3cert = stan_GetCERTCertificate(c, PR_FALSE);
    if (!nss3cert) {
        nssCertificate_Destroy(c);
    }
    return nss3cert;
}

static nssTrustLevel
get_stan_trust(unsigned int t, PRBool isClientAuth)
{
    if (isClientAuth) {
        if (t & CERTDB_TRUSTED_CLIENT_CA) {
            return nssTrustLevel_TrustedDelegator;
        }
    } else {
        if (t & CERTDB_TRUSTED_CA || t & CERTDB_NS_TRUSTED_CA) {
            return nssTrustLevel_TrustedDelegator;
        }
    }
    if (t & CERTDB_TRUSTED) {
        return nssTrustLevel_Trusted;
    }
    if (t & CERTDB_TERMINAL_RECORD) {
        return nssTrustLevel_NotTrusted;
    }
    if (t & CERTDB_VALID_CA) {
        return nssTrustLevel_ValidDelegator;
    }
    return nssTrustLevel_MustVerify;
}

/* Build the NSSCertificate for a CERTCertificate that does not have one yet.
 *
 * The caller must hold the temp/perm lock. Holding it here is what makes the
 * check-and-create in STAN_GetNSSCertificate() atomic; see the comment there.
 * Nothing in this function may call out to code that can re-enter that lock or
 * block on a token. The only PKCS#11 call below, PK11Slot_GetNSSToken(), takes
 * a per-slot lock around an atomic refcount and nothing else.
 *
 * 'c' is not reachable by any other thread until the caller publishes it, so
 * taking its own object lock in nssPKIObject_AddInstance() cannot contend with
 * the paths that take an object lock before this one.
 */
static NSSCertificate *
stan_CreateNSSCertificateLocked(CERTCertificate *cc)
{
    NSSCertificate *c;
    nssCryptokiInstance *instance;
    nssPKIObject *pkiob;
    NSSArena *arena;

    arena = NSSArena_Create();
    if (!arena) {
        return NULL;
    }
    c = nss_ZNEW(arena, NSSCertificate);
    if (!c) {
        nssArena_Destroy(arena);
        return NULL;
    }

    SECItem *keyID = pk11_mkcertKeyID(cc);
    if (keyID) {
        nssItem_Create(arena, &c->id, keyID->len, keyID->data);
        SECITEM_FreeItem(keyID, PR_TRUE);
        if (!c->id.data || !c->id.size) {
            nssArena_Destroy(arena);
            return NULL;
        }
    }

    NSSITEM_FROM_SECITEM(&c->encoding, &cc->derCert);
    c->type = NSSCertificateType_PKIX;
    pkiob = nssPKIObject_Create(arena, NULL, cc->dbhandle, NULL, nssPKIMonitor);
    if (!pkiob) {
        nssArena_Destroy(arena);
        return NULL;
    }
    c->object = *pkiob;
    nssItem_Create(arena,
                   &c->issuer, cc->derIssuer.len, cc->derIssuer.data);
    nssItem_Create(arena,
                   &c->subject, cc->derSubject.len, cc->derSubject.data);
    /* CERTCertificate stores serial numbers decoded.  I need the DER
     * here.  sigh.
     */
    SECItem derSerial;
    SECStatus secrv;
    secrv = CERT_SerialNumberFromDERCert(&cc->derCert, &derSerial);
    if (secrv == SECFailure) {
        nssArena_Destroy(arena);
        return NULL;
    }
    nssItem_Create(arena, &c->serial, derSerial.len, derSerial.data);
    PORT_Free(derSerial.data);

    if (cc->emailAddr && cc->emailAddr[0]) {
        c->email = nssUTF8_Create(arena,
                                  nssStringType_PrintableString,
                                  (NSSUTF8 *)cc->emailAddr,
                                  PORT_Strlen(cc->emailAddr));
    }
    if (cc->slot) {
        instance = nss_ZNEW(arena, nssCryptokiInstance);
        if (!instance) {
            nssArena_Destroy(arena);
            return NULL;
        }
        instance->token = PK11Slot_GetNSSToken(cc->slot);
        if (!instance->token) {
            nssArena_Destroy(arena);
            return NULL;
        }
        instance->handle = cc->pkcs11ID;
        instance->isTokenObject = PR_TRUE;
        if (cc->nickname) {
            instance->label = nssUTF8_Create(arena,
                                             nssStringType_UTF8String,
                                             (NSSUTF8 *)cc->nickname,
                                             PORT_Strlen(cc->nickname));
        }
        nssPKIObject_AddInstance(&c->object, instance);
    }
    c->decoding = create_decoded_pkix_cert_from_nss3cert(NULL, cc);
    if (!c->decoding) {
        nssArena_Destroy(arena);
        return NULL;
    }
    return c;
}

NSS_EXTERN NSSCertificate *
STAN_GetNSSCertificate(CERTCertificate *cc)
{
    NSSCertificate *c;

    /* Creating the NSSCertificate has to be atomic with the check for one.
     * nssPKIObject_Create() mints it with a reference count of one, and that
     * reference stands for the CERTCertificate itself. If two threads each
     * created one, that reference would be minted twice while every later
     * CERT_DestroyCertificate() decremented whichever of the two was published
     * last, taking its count below zero. */
    CERT_LockCertTempPerm(cc);
    c = cc->nssCertificate;
    if (!c) {
        c = stan_CreateNSSCertificateLocked(cc);
        cc->nssCertificate = c;
    }
    CERT_UnlockCertTempPerm(cc);
    return c;
}

static NSSToken *
stan_GetTrustToken(
    NSSCertificate *c)
{
    NSSToken *ttok = NULL;
    NSSToken *rtok = NULL;
    NSSToken *tok = NULL;
    nssCryptokiObject **ip;
    nssCryptokiObject **instances = nssPKIObject_GetInstances(&c->object);
    if (!instances) {
        return PR_FALSE;
    }
    for (ip = instances; *ip; ip++) {
        nssCryptokiObject *instance = *ip;
        nssCryptokiObject *to =
            nssToken_FindTrustForCertificate(instance->token, NULL,
                                             &c->encoding, &c->issuer, &c->serial,
                                             nssTokenSearchType_TokenOnly);
        NSSToken *ctok = instance->token;
        PRBool ro = PK11_IsReadOnly(ctok->pk11slot);

        if (to) {
            nssCryptokiObject_Destroy(to);
            ttok = ctok;
            if (!ro) {
                break;
            }
        } else {
            if (!rtok && ro) {
                rtok = ctok;
            }
            if (!tok && !ro) {
                tok = ctok;
            }
        }
    }
    nssCryptokiObjectArray_Destroy(instances);
    return ttok ? ttok : (tok ? tok : rtok);
}

NSS_EXTERN PRStatus
STAN_ChangeCertTrust(CERTCertificate *cc, CERTCertTrust *trust)
{
    PRStatus nssrv;
    NSSCertificate *c = STAN_GetNSSCertificate(cc);
    NSSToken *tok;
    NSSTrustDomain *td;
    NSSTrust *nssTrust;
    NSSArena *arena;
    CERTCertTrust *oldTrust;
    CERTCertTrust *newTrust;
    nssListIterator *tokens;
    PRBool moving_object;
    nssCryptokiObject *newInstance;
    nssPKIObject *pkiob;

    if (c == NULL) {
        return PR_FAILURE;
    }
    oldTrust = nssTrust_GetCERTCertTrustForCert(c, cc);
    if (oldTrust) {
        if (memcmp(oldTrust, trust, sizeof(CERTCertTrust)) == 0) {
            /* ... and the new trust is no different, done) */
            return PR_SUCCESS;
        } else {
            /* take over memory already allocated in cc's arena */
            newTrust = oldTrust;
        }
    } else {
        newTrust = PORT_ArenaAlloc(cc->arena, sizeof(CERTCertTrust));
    }
    memcpy(newTrust, trust, sizeof(CERTCertTrust));
    CERT_LockCertTrust(cc);
    cc->trust = newTrust;
    CERT_UnlockCertTrust(cc);
    /* Set the NSSCerticate's trust */
    arena = nssArena_Create();
    if (!arena)
        return PR_FAILURE;
    nssTrust = nss_ZNEW(arena, NSSTrust);
    if (!nssTrust) {
        nssArena_Destroy(arena);
        return PR_FAILURE;
    }
    pkiob = nssPKIObject_Create(arena, NULL, cc->dbhandle, NULL, nssPKILock);
    if (!pkiob) {
        nssArena_Destroy(arena);
        return PR_FAILURE;
    }
    nssTrust->object = *pkiob;
    nssTrust->certificate = c;
    nssTrust->serverAuth = get_stan_trust(trust->sslFlags, PR_FALSE);
    nssTrust->clientAuth = get_stan_trust(trust->sslFlags, PR_TRUE);
    nssTrust->emailProtection = get_stan_trust(trust->emailFlags, PR_FALSE);
    nssTrust->codeSigning = get_stan_trust(trust->objectSigningFlags, PR_FALSE);
    nssTrust->stepUpApproved =
        (PRBool)(trust->sslFlags & CERTDB_GOVT_APPROVED_CA);

    nssPKIObject_Lock(&c->object);
    NSSCryptoContext *cctx = c->object.cryptoContext;
    nssPKIObject_Unlock(&c->object);
    if (cctx) {
        /* The cert is in a context, set the trust there */
        nssrv = nssCryptoContext_ImportTrust(cctx, nssTrust);
        if (nssrv != PR_SUCCESS) {
            goto done;
        }

        nssPKIObject_Lock(&c->object);
        PRBool soleInstance = c->object.numInstances == 0;
        nssPKIObject_Unlock(&c->object);
        if (soleInstance) {
            /* The context is the only instance, finished */
            goto done;
        }
    }

    td = STAN_GetDefaultTrustDomain();
    tok = stan_GetTrustToken(c);
    moving_object = PR_FALSE;
    if (tok && PK11_IsReadOnly(tok->pk11slot)) {
        NSSRWLock_LockRead(td->tokensLock);
        tokens = nssList_CreateIterator(td->tokenList);
        if (!tokens) {
            nssrv = PR_FAILURE;
            NSSRWLock_UnlockRead(td->tokensLock);
            goto done;
        }
        for (tok = (NSSToken *)nssListIterator_Start(tokens);
             tok != (NSSToken *)NULL;
             tok = (NSSToken *)nssListIterator_Next(tokens)) {
            if (!PK11_IsReadOnly(tok->pk11slot))
                break;
        }
        nssListIterator_Finish(tokens);
        nssListIterator_Destroy(tokens);
        NSSRWLock_UnlockRead(td->tokensLock);
        moving_object = PR_TRUE;
    }
    if (tok) {
        if (moving_object) {
            /* this is kind of hacky.  the softoken needs the cert
             * object in order to store trust.  forcing it to be perm
             */
            NSSUTF8 *nickname = nssCertificate_GetNickname(c, NULL);
            NSSASCII7 *email = NULL;

            if (PK11_IsInternal(tok->pk11slot)) {
                email = c->email;
            }
            newInstance = nssToken_ImportCertificate(tok, NULL,
                                                     NSSCertificateType_PKIX,
                                                     &c->id,
                                                     nickname,
                                                     &c->encoding,
                                                     &c->issuer,
                                                     &c->subject,
                                                     &c->serial,
                                                     email,
                                                     PR_TRUE);
            nss_ZFreeIf(nickname);
            nickname = NULL;
            if (!newInstance) {
                nssrv = PR_FAILURE;
                goto done;
            }
            nssPKIObject_AddInstance(&c->object, newInstance);
        }
        newInstance = nssToken_ImportTrust(tok, NULL, &c->encoding,
                                           &c->issuer, &c->serial,
                                           nssTrust->serverAuth,
                                           nssTrust->clientAuth,
                                           nssTrust->codeSigning,
                                           nssTrust->emailProtection,
                                           nssTrust->stepUpApproved, PR_TRUE);
        /* If the selected token can't handle trust, dump the trust on
         * the internal token */
        if (!newInstance && !PK11_IsInternalKeySlot(tok->pk11slot)) {
            PK11SlotInfo *slot = PK11_GetInternalKeySlot();
            NSSUTF8 *nickname = nssCertificate_GetNickname(c, NULL);
            NSSASCII7 *email = c->email;
            tok = PK11Slot_GetNSSToken(slot);
            PK11_FreeSlot(slot);
            if (!tok) {
                nssrv = PR_FAILURE;
                goto done;
            }

            newInstance = nssToken_ImportCertificate(tok, NULL,
                                                     NSSCertificateType_PKIX,
                                                     &c->id,
                                                     nickname,
                                                     &c->encoding,
                                                     &c->issuer,
                                                     &c->subject,
                                                     &c->serial,
                                                     email,
                                                     PR_TRUE);
            nss_ZFreeIf(nickname);
            nickname = NULL;
            if (!newInstance) {
                (void)nssToken_Destroy(tok);
                nssrv = PR_FAILURE;
                goto done;
            }
            nssPKIObject_AddInstance(&c->object, newInstance);
            newInstance = nssToken_ImportTrust(tok, NULL, &c->encoding,
                                               &c->issuer, &c->serial,
                                               nssTrust->serverAuth,
                                               nssTrust->clientAuth,
                                               nssTrust->codeSigning,
                                               nssTrust->emailProtection,
                                               nssTrust->stepUpApproved, PR_TRUE);
            (void)nssToken_Destroy(tok);
        }
        if (newInstance) {
            nssCryptokiObject_Destroy(newInstance);
            nssrv = PR_SUCCESS;
        } else {
            nssrv = PR_FAILURE;
        }
    } else {
        nssrv = PR_FAILURE;
    }
done:
    (void)nssTrust_Destroy(nssTrust);
    return nssrv;
}

/*
** Delete trust objects matching the given slot.
** Returns error if a device fails to delete.
**
** This function has the side effect of moving the
** surviving entries to the front of the object list
** and nullifying the rest.
*/
static PRStatus
DeleteCertTrustMatchingSlot(PK11SlotInfo *pk11slot, nssPKIObject *tObject)
{
    int numNotDestroyed = 0; /* the ones skipped plus the failures */
    int failureCount = 0;    /* actual deletion failures by devices */
    unsigned int index;

    nssPKIObject_AddRef(tObject);
    nssPKIObject_Lock(tObject);
    /* Keep going even if a module fails to delete. */
    for (index = 0; index < tObject->numInstances; index++) {
        nssCryptokiObject *instance = tObject->instances[index];
        if (!instance) {
            continue;
        }

        /* ReadOnly and not matched treated the same */
        if (PK11_IsReadOnly(instance->token->pk11slot) ||
            pk11slot != instance->token->pk11slot) {
            tObject->instances[numNotDestroyed++] = instance;
            continue;
        }

        /* Here we have found a matching one */
        tObject->instances[index] = NULL;
        if (nssToken_DeleteStoredObject(instance) == PR_SUCCESS) {
            nssCryptokiObject_Destroy(instance);
        } else {
            tObject->instances[numNotDestroyed++] = instance;
            failureCount++;
        }
    }
    if (numNotDestroyed == 0) {
        nss_ZFreeIf(tObject->instances);
        tObject->numInstances = 0;
    } else {
        tObject->numInstances = numNotDestroyed;
    }

    nssPKIObject_Unlock(tObject);
    nssPKIObject_Destroy(tObject);

    return failureCount == 0 ? PR_SUCCESS : PR_FAILURE;
}

/*
** Delete trust objects matching the slot of the given certificate.
** Returns an error if any device fails to delete.
*/
NSS_EXTERN PRStatus
STAN_DeleteCertTrustMatchingSlot(NSSCertificate *c)
{
    PRStatus nssrv = PR_SUCCESS;

    unsigned int i;
    nssPKIObject *tobject = NULL;
    nssPKIObject *cobject = &c->object;

    NSSTrustDomain *td = STAN_GetDefaultTrustDomain();
    NSSTrust *nssTrust = nssTrustDomain_FindTrustForCertificate(td, &c->encoding, &c->issuer, &c->serial);
    if (!nssTrust) {
        return PR_FAILURE;
    }

    tobject = &nssTrust->object;

    /* Iterate through the cert and trust object instances looking for
     * those with matching pk11 slots to delete. Even if some device
     * can't delete we keep going. Keeping a status variable for the
     * loop so that once it's failed the other gets set.
     */
    NSSRWLock_LockRead(td->tokensLock);
    nssPKIObject_AddRef(cobject);
    nssPKIObject_Lock(cobject);
    for (i = 0; i < cobject->numInstances; i++) {
        nssCryptokiObject *cInstance = cobject->instances[i];
        if (cInstance && !PK11_IsReadOnly(cInstance->token->pk11slot)) {
            PRStatus status;
            if (!tobject->numInstances || !tobject->instances)
                continue;
            status = DeleteCertTrustMatchingSlot(cInstance->token->pk11slot, tobject);
            if (status == PR_FAILURE) {
                /* set the outer one but keep going */
                nssrv = PR_FAILURE;
            }
        }
    }
    nssTrust_Destroy(nssTrust);
    nssPKIObject_Unlock(cobject);
    nssPKIObject_Destroy(cobject);
    NSSRWLock_UnlockRead(td->tokensLock);
    return nssrv;
}

static void
cert_dump_iter(const void *k, void *v, void *a)
{
    NSSCertificate *c = (NSSCertificate *)k;
    CERTCertificate *cert = STAN_GetCERTCertificate(c);
    printf("[%2d] \"%s\"\n", c->object.refCount, cert->subjectName);
}

void
nss_DumpCertificateCacheInfo()
{
    NSSTrustDomain *td;
    NSSCryptoContext *cc;
    td = STAN_GetDefaultTrustDomain();
    cc = STAN_GetDefaultCryptoContext();
    printf("\n\nCertificates in the cache:\n");
    nssTrustDomain_DumpCacheInfo(td, cert_dump_iter, NULL);
    printf("\n\nCertificates in the temporary store:\n");
    if (cc->certStore) {
        nssCertificateStore_DumpStoreInfo(cc->certStore, cert_dump_iter, NULL);
    }
}
