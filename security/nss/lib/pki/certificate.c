/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NSSPKI_H
#include "nsspki.h"
#endif /* NSSPKI_H */

#ifndef PKIT_H
#include "pkit.h"
#endif /* PKIT_H */

#ifndef PKIM_H
#include "pkim.h"
#endif /* PKIM_H */

#ifndef DEV_H
#include "dev.h"
#endif /* DEV_H */

#include "certdb.h"
#include "hasht.h"
#include "pk11func.h"
#include "pki3hack.h"
#include "pkistore.h"
#include "secmodi.h"

#ifndef BASE_H
#include "base.h"
#endif /* BASE_H */

extern const NSSError NSS_ERROR_NOT_FOUND;

NSS_IMPLEMENT PRStatus
nssCertificate_TryToSetCertKeyID(NSSCertificate *c)
{
    c->id.data = NULL;
    c->id.size = 0;

    SECItem secDER;
    SECITEM_FROM_NSSITEM(&secDER, &c->encoding);
    SECItem *keyID = pk11_mkcertKeyIDFromDER(&secDER);
    if (!keyID) {
        return PR_SUCCESS;
    }
    nssItem_Create(c->object.arena, &c->id, keyID->len, keyID->data);
    SECITEM_FreeItem(keyID, PR_TRUE);

    return c->id.data && c->id.size ? PR_SUCCESS : PR_FAILURE;
}

/* Creates a certificate from a base object */
NSS_IMPLEMENT NSSCertificate *
nssCertificate_Create(
    nssPKIObject *object)
{
    PR_ASSERT(object->lockType == nssPKIMonitor);

    PRStatus status;
    NSSCertificate *rvCert;
    NSSArena *arena = object->arena;
    nssArenaMark *mark = nssArena_Mark(arena);
    if (!mark) {
        return NULL;
    }
    rvCert = nss_ZNEW(arena, NSSCertificate);
    if (!rvCert) {
        return NULL;
    }
    rvCert->object = *object;
    /* XXX should choose instance based on some criteria */
    nssPKIObject_Lock(object);
    PR_ASSERT(object->instances != NULL && object->numInstances > 0);
    status = nssCryptokiCertificate_GetAttributes(object->instances[0],
                                                  NULL, /* XXX sessionOpt */
                                                  arena,
                                                  &rvCert->type,
                                                  &rvCert->id,
                                                  &rvCert->encoding,
                                                  &rvCert->issuer,
                                                  &rvCert->serial,
                                                  &rvCert->subject);
    nssPKIObject_Unlock(object);
    if (status != PR_SUCCESS ||
        !rvCert->encoding.data ||
        !rvCert->encoding.size ||
        !rvCert->issuer.data ||
        !rvCert->issuer.size ||
        !rvCert->serial.data ||
        !rvCert->serial.size) {
        nssArena_Release(arena, mark);
        return NULL;
    }
    if (!rvCert->id.data && nssCertificate_TryToSetCertKeyID(rvCert) != PR_SUCCESS) {
        nssArena_Release(arena, mark);
        return NULL;
    }
    nssArena_Unmark(arena, mark);
    return rvCert;
}

NSS_IMPLEMENT NSSCertificate *
nssCertificate_AddRef(
    NSSCertificate *c)
{
    if (c) {
        nssPKIObject_AddRef(&c->object);
    }
    return c;
}

NSS_IMPLEMENT PRStatus
nssCertificate_Destroy(
    NSSCertificate *c)
{
    nssCertificateStoreTrace lockTrace = { NULL, NULL, PR_FALSE, PR_FALSE };
    nssCertificateStoreTrace unlockTrace = { NULL, NULL, PR_FALSE, PR_FALSE };

    if (!c) {
        return PR_SUCCESS;
    }

    // Determine which lock to hold while decrementing the refcount.
    nssPKIObject_Lock(&c->object);
    NSSCryptoContext *cc = c->object.cryptoContext;
    nssPKIObject_Unlock(&c->object);
    // If the certificate had a cryptoContext set, it may be in the certificate
    // store.
    if (cc) {
        // The certificate store lock must be taken before the object lock.
        nssCertificateStore_Lock(cc->certStore, &lockTrace);
        nssPKIObject_Lock(&c->object);
        // Lost the race with moving the certificate from the certificate store
        // to the trust domain (see __CERT_AddTempCertToPerm and
        // PK11_ImportCert).
        if (c->object.cryptoContext != cc) {
            nssPKIObject_Unlock(&c->object);
            nssCertificateStore_Unlock(cc->certStore, &lockTrace,
                                       &unlockTrace);
            cc = NULL;
        }
    }

    // If the certificate did not have a cryptoContext set (or we lost the
    // race), it is in the trust domain.
    NSSTrustDomain *td = STAN_GetDefaultTrustDomain();
    if (!cc) {
        nssTrustDomain_LockCertCache(td);
        nssPKIObject_Lock(&c->object);
    }

    // At this point, we are holding the appropriate lock for where the
    // certificate is stored (certificate store or trust domain) as well as the
    // lock for the pki object backing the certificate. For some reason this is
    // necessary even though the refcounting is atomic. Go figure.

    // Return early if this isn't the last reference.
    PRInt32 refCount = PR_ATOMIC_DECREMENT(&c->object.refCount);
    PORT_ReleaseAssert(refCount >= 0);
    if (refCount != 0) {
        nssPKIObject_Unlock(&c->object);
        if (cc) {
            nssCertificateStore_Unlock(cc->certStore, &lockTrace, &unlockTrace);
        } else {
            nssTrustDomain_UnlockCertCache(td);
        }
        return PR_SUCCESS;
    }

    // Remove the certificate from the appropriate location.
    if (cc) {
        nssCertificateStore_RemoveCertLOCKED(cc->certStore, c);
    } else {
        nssTrustDomain_RemoveCertFromCacheLOCKED(td, c);
    }

    // Release the appropriate locks.
    nssPKIObject_Unlock(&c->object);
    if (cc) {
        nssCertificateStore_Unlock(cc->certStore, &lockTrace, &unlockTrace);
    } else {
        nssTrustDomain_UnlockCertCache(td);
    }

    // Free the resources associated with the certificate.
    for (PRUint32 i = 0; i < c->object.numInstances; i++) {
        nssCryptokiObject_Destroy(c->object.instances[i]);
    }
    nssPKIObject_DestroyLock(&c->object);
    nssDecodedCert_Destroy(c->decoding);
    nssArena_Destroy(c->object.arena);

    return PR_SUCCESS;
}

NSS_IMPLEMENT PRStatus
NSSCertificate_Destroy(NSSCertificate *c)
{
    return nssCertificate_Destroy(c);
}

NSS_IMPLEMENT NSSDER *
nssCertificate_GetEncoding(NSSCertificate *c)
{
    if (c->encoding.size > 0 && c->encoding.data) {
        return &c->encoding;
    } else {
        return (NSSDER *)NULL;
    }
}

/* Returns a copy, Caller must free using nss_ZFreeIf */
NSS_IMPLEMENT NSSUTF8 *
nssCertificate_GetNickname(
    NSSCertificate *c,
    NSSToken *tokenOpt)
{
    return nssPKIObject_GetNicknameForToken(&c->object, tokenOpt);
}

NSS_IMPLEMENT PRStatus
NSSCertificate_DeleteStoredObject(
    NSSCertificate *c,
    NSSCallback *uhh)
{
    return nssPKIObject_DeleteStoredObject(&c->object, uhh, PR_TRUE);
}

NSS_IMPLEMENT nssDecodedCert *
nssCertificate_GetDecoding(
    NSSCertificate *c)
{
    nssDecodedCert *deco = NULL;
    if (c->type == NSSCertificateType_PKIX) {
        (void)STAN_GetCERTCertificate(c);
    }
    nssPKIObject_Lock(&c->object);
    if (!c->decoding) {
        deco = nssDecodedCert_Create(NULL, &c->encoding, c->type);
        PORT_Assert(!c->decoding);
        c->decoding = deco;
    } else {
        deco = c->decoding;
    }
    nssPKIObject_Unlock(&c->object);
    return deco;
}

static NSSCertificate **
filter_subject_certs_for_id(
    NSSCertificate **subjectCerts,
    void *id)
{
    NSSCertificate **si;
    nssDecodedCert *dcp;
    int nextOpenSlot = 0;
    int i;
    nssCertIDMatch matchLevel = nssCertIDMatch_Unknown;
    nssCertIDMatch match;

    /* walk the subject certs */
    for (si = subjectCerts; *si; si++) {
        dcp = nssCertificate_GetDecoding(*si);
        if (!dcp) {
            NSSCertificate_Destroy(*si);
            continue;
        }
        match = dcp->matchIdentifier(dcp, id);
        switch (match) {
            case nssCertIDMatch_Yes:
                if (matchLevel == nssCertIDMatch_Unknown) {
                    /* we have non-definitive matches, forget them */
                    for (i = 0; i < nextOpenSlot; i++) {
                        NSSCertificate_Destroy(subjectCerts[i]);
                        subjectCerts[i] = NULL;
                    }
                    nextOpenSlot = 0;
                    /* only keep definitive matches from now on */
                    matchLevel = nssCertIDMatch_Yes;
                }
                /* keep the cert */
                subjectCerts[nextOpenSlot++] = *si;
                break;
            case nssCertIDMatch_Unknown:
                if (matchLevel == nssCertIDMatch_Unknown) {
                    /* only have non-definitive matches so far, keep it */
                    subjectCerts[nextOpenSlot++] = *si;
                    break;
                }
            /* else fall through, we have a definitive match already */
            case nssCertIDMatch_No:
            default:
                NSSCertificate_Destroy(*si);
                *si = NULL;
        }
    }
    subjectCerts[nextOpenSlot] = NULL;
    return subjectCerts;
}

static NSSCertificate **
filter_certs_for_valid_issuers(NSSCertificate **certs)
{
    NSSCertificate **cp;
    nssDecodedCert *dcp;
    int nextOpenSlot = 0;

    for (cp = certs; *cp; cp++) {
        dcp = nssCertificate_GetDecoding(*cp);
        if (dcp && dcp->isValidIssuer(dcp)) {
            certs[nextOpenSlot++] = *cp;
        } else {
            NSSCertificate_Destroy(*cp);
        }
    }
    certs[nextOpenSlot] = NULL;
    return certs;
}

static NSSCertificate *
find_cert_issuer(
    NSSCertificate *c,
    NSSTime *timeOpt,
    NSSUsage *usage,
    NSSPolicies *policiesOpt,
    NSSTrustDomain *td,
    NSSCryptoContext *cc)
{
    NSSArena *arena;
    NSSCertificate **certs = NULL;
    NSSCertificate **ccIssuers = NULL;
    NSSCertificate **tdIssuers = NULL;
    NSSCertificate *issuer = NULL;

    if (!cc)
        cc = c->object.cryptoContext;
    if (!td)
        td = NSSCertificate_GetTrustDomain(c);
    arena = nssArena_Create();
    if (!arena) {
        return (NSSCertificate *)NULL;
    }
    if (cc) {
        ccIssuers = nssCryptoContext_FindCertificatesBySubject(cc,
                                                               &c->issuer,
                                                               NULL,
                                                               0,
                                                               arena);
    }
    if (td)
        tdIssuers = nssTrustDomain_FindCertificatesBySubject(td,
                                                             &c->issuer,
                                                             NULL,
                                                             0,
                                                             arena);
    certs = nssCertificateArray_Join(ccIssuers, tdIssuers);
    if (certs) {
        nssDecodedCert *dc = NULL;
        void *issuerID = NULL;
        dc = nssCertificate_GetDecoding(c);
        if (dc) {
            issuerID = dc->getIssuerIdentifier(dc);
        }
        /* XXX review based on CERT_FindCertIssuer
         * this function is not using the authCertIssuer field as a fallback
         * if authority key id does not exist
         */
        if (issuerID) {
            certs = filter_subject_certs_for_id(certs, issuerID);
        }
        certs = filter_certs_for_valid_issuers(certs);
        issuer = nssCertificateArray_FindBestCertificate(certs,
                                                         timeOpt,
                                                         usage,
                                                         policiesOpt);
        nssCertificateArray_Destroy(certs);
    }
    nssArena_Destroy(arena);
    return issuer;
}

/* This function returns the built chain, as far as it gets,
** even if/when it fails to find an issuer, and returns PR_FAILURE
*/
NSS_IMPLEMENT NSSCertificate **
nssCertificate_BuildChain(
    NSSCertificate *c,
    NSSTime *timeOpt,
    NSSUsage *usage,
    NSSPolicies *policiesOpt,
    NSSCertificate **rvOpt,
    PRUint32 rvLimit,
    NSSArena *arenaOpt,
    PRStatus *statusOpt,
    NSSTrustDomain *td,
    NSSCryptoContext *cc)
{
    NSSCertificate **rvChain = NULL;
    NSSUsage issuerUsage = *usage;
    nssPKIObjectCollection *collection = NULL;
    PRUint32 rvCount = 0;
    PRStatus st;
    PRStatus ret = PR_SUCCESS;

    if (!c || !cc ||
        (!td && (td = NSSCertificate_GetTrustDomain(c)) == NULL)) {
        goto loser;
    }
    /* bump the usage up to CA level */
    issuerUsage.nss3lookingForCA = PR_TRUE;
    collection = nssCertificateCollection_Create(td, NULL);
    if (!collection)
        goto loser;
    st = nssPKIObjectCollection_AddObject(collection, (nssPKIObject *)c);
    if (st != PR_SUCCESS)
        goto loser;
    for (rvCount = 1; (!rvLimit || rvCount < rvLimit); ++rvCount) {
        CERTCertificate *cCert = STAN_GetCERTCertificate(c);
        if (cCert->isRoot) {
            /* not including the issuer of the self-signed cert, which is,
             * of course, itself
             */
            break;
        }
        c = find_cert_issuer(c, timeOpt, &issuerUsage, policiesOpt, td, cc);
        if (!c) {
            ret = PR_FAILURE;
            break;
        }
        st = nssPKIObjectCollection_AddObject(collection, (nssPKIObject *)c);
        nssCertificate_Destroy(c); /* collection has it */
        if (st != PR_SUCCESS)
            goto loser;
    }
    rvChain = nssPKIObjectCollection_GetCertificates(collection,
                                                     rvOpt,
                                                     rvLimit,
                                                     arenaOpt);
    if (rvChain) {
        nssPKIObjectCollection_Destroy(collection);
        if (statusOpt)
            *statusOpt = ret;
        if (ret != PR_SUCCESS)
            nss_SetError(NSS_ERROR_CERTIFICATE_ISSUER_NOT_FOUND);
        return rvChain;
    }

loser:
    if (collection)
        nssPKIObjectCollection_Destroy(collection);
    if (statusOpt)
        *statusOpt = PR_FAILURE;
    nss_SetError(NSS_ERROR_CERTIFICATE_ISSUER_NOT_FOUND);
    return rvChain;
}

NSS_IMPLEMENT NSSCertificate **
NSSCertificate_BuildChain(
    NSSCertificate *c,
    NSSTime *timeOpt,
    NSSUsage *usage,
    NSSPolicies *policiesOpt,
    NSSCertificate **rvOpt,
    PRUint32 rvLimit, /* zero for no limit */
    NSSArena *arenaOpt,
    PRStatus *statusOpt,
    NSSTrustDomain *td,
    NSSCryptoContext *cc)
{
    return nssCertificate_BuildChain(c, timeOpt, usage, policiesOpt,
                                     rvOpt, rvLimit, arenaOpt, statusOpt,
                                     td, cc);
}

NSS_IMPLEMENT NSSCryptoContext *
nssCertificate_GetCryptoContext(NSSCertificate *c)
{
    return c->object.cryptoContext;
}

NSS_IMPLEMENT NSSTrustDomain *
nssCertificate_GetTrustDomain(NSSCertificate *c)
{
    return c->object.trustDomain;
}

NSS_IMPLEMENT NSSTrustDomain *
NSSCertificate_GetTrustDomain(NSSCertificate *c)
{
    return nssCertificate_GetTrustDomain(c);
}

NSS_IMPLEMENT PRBool
NSSCertificate_IsPrivateKeyAvailable(
    NSSCertificate *c,
    NSSCallback *uhh,
    PRStatus *statusOpt)
{
    PRBool isUser = PR_FALSE;
    nssCryptokiObject **ip;
    nssCryptokiObject **instances = nssPKIObject_GetInstances(&c->object);
    if (!instances) {
        return PR_FALSE;
    }
    for (ip = instances; *ip; ip++) {
        nssCryptokiObject *instance = *ip;
        if (nssToken_IsPrivateKeyAvailable(instance->token, c, instance)) {
            isUser = PR_TRUE;
        }
    }
    nssCryptokiObjectArray_Destroy(instances);
    return isUser;
}

/* sort the subject cert list from newest to oldest */
PRIntn
nssCertificate_SubjectListSort(
    void *v1,
    void *v2)
{
    NSSCertificate *c1 = (NSSCertificate *)v1;
    NSSCertificate *c2 = (NSSCertificate *)v2;
    nssDecodedCert *dc1 = nssCertificate_GetDecoding(c1);
    nssDecodedCert *dc2 = nssCertificate_GetDecoding(c2);
    if (!dc1) {
        return dc2 ? 1 : 0;
    } else if (!dc2) {
        return -1;
    } else {
        return dc1->isNewerThan(dc1, dc2) ? -1 : 1;
    }
}

NSS_IMPLEMENT nssSMIMEProfile *
nssSMIMEProfile_Create(
    NSSCertificate *cert,
    NSSItem *profileTime,
    NSSItem *profileData)
{
    NSSArena *arena;
    nssSMIMEProfile *rvProfile;
    nssPKIObject *object;
    NSSTrustDomain *td = nssCertificate_GetTrustDomain(cert);
    NSSCryptoContext *cc = nssCertificate_GetCryptoContext(cert);
    arena = nssArena_Create();
    if (!arena) {
        return NULL;
    }
    object = nssPKIObject_Create(arena, NULL, td, cc, nssPKILock);
    if (!object) {
        goto loser;
    }
    rvProfile = nss_ZNEW(arena, nssSMIMEProfile);
    if (!rvProfile) {
        goto loser;
    }
    rvProfile->object = *object;
    rvProfile->certificate = cert;
    rvProfile->email = nssUTF8_Duplicate(cert->email, arena);
    rvProfile->subject = nssItem_Duplicate(&cert->subject, arena, NULL);
    if (profileTime) {
        rvProfile->profileTime = nssItem_Duplicate(profileTime, arena, NULL);
    }
    if (profileData) {
        rvProfile->profileData = nssItem_Duplicate(profileData, arena, NULL);
    }
    return rvProfile;
loser:
    if (object)
        nssPKIObject_Destroy(object);
    else if (arena)
        nssArena_Destroy(arena);
    return (nssSMIMEProfile *)NULL;
}

/* execute a callback function on all members of a cert list */
NSS_EXTERN PRStatus
nssCertificateList_DoCallback(
    nssList *certList,
    PRStatus (*callback)(NSSCertificate *c, void *arg),
    void *arg)
{
    nssListIterator *certs;
    NSSCertificate *cert;
    certs = nssList_CreateIterator(certList);
    if (!certs) {
        return PR_FAILURE;
    }
    for (cert = (NSSCertificate *)nssListIterator_Start(certs);
         cert != (NSSCertificate *)NULL;
         cert = (NSSCertificate *)nssListIterator_Next(certs)) {
        (void)(*callback)(cert, arg);
    }
    nssListIterator_Finish(certs);
    nssListIterator_Destroy(certs);
    return PR_SUCCESS;
}

static PRStatus
add_ref_callback(NSSCertificate *c, void *a)
{
    nssCertificate_AddRef(c);
    return PR_SUCCESS;
}

NSS_IMPLEMENT void
nssCertificateList_AddReferences(
    nssList *certList)
{
    (void)nssCertificateList_DoCallback(certList, add_ref_callback, NULL);
}

/*
 * Is this trust record safe to apply to all certs of the same issuer/SN
 * independent of the cert matching the hash. This is only true is the trust
 * is unknown or distrusted. In general this feature is only useful to
 * explicitly distrusting certs. It is not safe to use to trust certs, so
 * only allow unknown and untrusted trust types.
 */
PRBool
nssTrust_IsSafeToIgnoreCertHash(nssTrustLevel serverAuth,
                                nssTrustLevel clientAuth, nssTrustLevel codeSigning,
                                nssTrustLevel email, PRBool stepup)
{
    /* step up is a trust type, if it's on, we must have a hash for the cert */
    if (stepup) {
        return PR_FALSE;
    }
    if ((serverAuth != nssTrustLevel_Unknown) &&
        (serverAuth != nssTrustLevel_NotTrusted)) {
        return PR_FALSE;
    }
    if ((clientAuth != nssTrustLevel_Unknown) &&
        (clientAuth != nssTrustLevel_NotTrusted)) {
        return PR_FALSE;
    }
    if ((codeSigning != nssTrustLevel_Unknown) &&
        (codeSigning != nssTrustLevel_NotTrusted)) {
        return PR_FALSE;
    }
    if ((email != nssTrustLevel_Unknown) &&
        (email != nssTrustLevel_NotTrusted)) {
        return PR_FALSE;
    }
    /* record only has Unknown and Untrusted entries, ok to accept without a
     * hash */
    return PR_TRUE;
}

/* verify that hash and mechanism matches certifcate. This function
 * uses hashCache and hashCacheMech from the caller to allow us to
 * use previous calculated hash values if they are the same */
static PRBool
nssTrust_isValidHash(const NSSItem *hash, CK_MECHANISM_TYPE hashMech,
                     NSSItem *cert, NSSItem *hashCache,
                     CK_MECHANISM_TYPE *hashCacheMech)
{
    if ((hashMech == CKM_INVALID_MECHANISM) || (hash->size == 0)) {
        return PR_FALSE;
    }
    if (*hashCacheMech != hashMech) {
        /* the cache doesn't have the correct mech, get a
         * new hash for the cert */
        hashCache->size = HASH_LENGTH_MAX;
        PRStatus ret = NSSAlgorithm_DigestBuf(hashMech, cert, hashCache);
        if (ret != PR_SUCCESS) {
            *hashCacheMech = CKM_INVALID_MECHANISM;
            return PR_FALSE;
        }
        *hashCacheMech = hashMech;
    }
    return ((hash->size == hashCache->size) && (PORT_Memcmp(hash->data,
                                                            hashCache->data,
                                                            hash->size) == 0));
}

NSS_IMPLEMENT NSSTrust *
nssTrust_Create(
    nssPKIObject *object,
    NSSItem *certData)
{
    PRStatus status;
    PRUint32 i;
    PRUint32 lastTrustOrder, myTrustOrder;
    unsigned char hashCacheBuf[HASH_LENGTH_MAX];
    unsigned char hashAttrBuf[HASH_LENGTH_MAX];
    NSSItem hashAttr = { hashAttrBuf, sizeof(hashAttrBuf) };
    NSSItem hashCache = { hashCacheBuf, sizeof(hashCacheBuf) };
    NSSTrust *rvt;
    CK_MECHANISM_TYPE hashMech = CKM_INVALID_MECHANISM;
    CK_MECHANISM_TYPE hashCacheMech = CKM_INVALID_MECHANISM;
    nssCryptokiObject *instance;
    nssTrustLevel serverAuth, clientAuth, codeSigning, emailProtection;
    PRBool stepUp;

    lastTrustOrder = 1 << 16; /* just make it big */
    PR_ASSERT(object->instances != NULL && object->numInstances > 0);
    rvt = nss_ZNEW(object->arena, NSSTrust);
    if (!rvt) {
        return (NSSTrust *)NULL;
    }
    rvt->object = *object;

    /* trust has to peek into the base object members */
    nssPKIObject_Lock(object);
    for (i = 0; i < object->numInstances; i++) {
        hashAttr.size = sizeof(hashAttrBuf); /* reset to allocated size */
        instance = object->instances[i];
        myTrustOrder = nssToken_GetTrustOrder(instance->token);
        status = nssCryptokiTrust_GetAttributes(instance, NULL,
                                                &hashAttr,
                                                &hashMech,
                                                &serverAuth,
                                                &clientAuth,
                                                &codeSigning,
                                                &emailProtection,
                                                &stepUp);
        if (status != PR_SUCCESS) {
            nssPKIObject_Unlock(object);
            return (NSSTrust *)NULL;
        }
        /* if no hash is specified, then trust applies to all certs with
         * this issuer/SN. NOTE: This is only true for entries that
         * have distrust and unknown record */
        if (!(
                /* we continue if there is no hash, and the trust type is
                 * safe to accept without a hash ... or ... */
                (((hashAttr.size == 0) || (hashMech == CKM_INVALID_MECHANISM)) &&
                 nssTrust_IsSafeToIgnoreCertHash(serverAuth, clientAuth,
                                                 codeSigning, emailProtection,
                                                 stepUp)) ||
                /* we have a hash of the correct size, and it matches */
                nssTrust_isValidHash(&hashAttr, hashMech, certData,
                                     &hashCache, &hashCacheMech))) {

            nssPKIObject_Unlock(object);
            return (NSSTrust *)NULL;
        }
        if (rvt->serverAuth == nssTrustLevel_Unknown ||
            myTrustOrder < lastTrustOrder) {
            rvt->serverAuth = serverAuth;
        }
        if (rvt->clientAuth == nssTrustLevel_Unknown ||
            myTrustOrder < lastTrustOrder) {
            rvt->clientAuth = clientAuth;
        }
        if (rvt->emailProtection == nssTrustLevel_Unknown ||
            myTrustOrder < lastTrustOrder) {
            rvt->emailProtection = emailProtection;
        }
        if (rvt->codeSigning == nssTrustLevel_Unknown ||
            myTrustOrder < lastTrustOrder) {
            rvt->codeSigning = codeSigning;
        }
        rvt->stepUpApproved = stepUp;
        lastTrustOrder = myTrustOrder;
    }
    nssPKIObject_Unlock(object);
    return rvt;
}

NSS_IMPLEMENT NSSTrust *
nssTrust_AddRef(NSSTrust *trust)
{
    if (trust) {
        nssPKIObject_AddRef(&trust->object);
    }
    return trust;
}

NSS_IMPLEMENT PRStatus
nssTrust_Destroy(NSSTrust *trust)
{
    if (trust) {
        (void)nssPKIObject_Destroy(&trust->object);
    }
    return PR_SUCCESS;
}

/* see pk11cert.c:pk11_HandleTrustObject */
static unsigned int
nssTrustLevel_ToCERTCertTrustFlags(nssTrustLevel t)
{
    unsigned int rt = 0;
    if (t == nssTrustLevel_Trusted) {
        rt |= CERTDB_TERMINAL_RECORD | CERTDB_TRUSTED;
    }
    if (t == nssTrustLevel_TrustedDelegator) {
        rt |= CERTDB_VALID_CA | CERTDB_TRUSTED_CA | CERTDB_NS_TRUSTED_CA;
    }
    if (t == nssTrustLevel_NotTrusted) {
        rt |= CERTDB_TERMINAL_RECORD;
    }
    if (t == nssTrustLevel_ValidDelegator) {
        rt |= CERTDB_VALID_CA;
    }
    return rt;
}

NSS_IMPLEMENT void
nssTrust_ToCERTCertTrust(NSSTrust *trust, CERTCertTrust *certTrust)
{
    certTrust->sslFlags = nssTrustLevel_ToCERTCertTrustFlags(trust->serverAuth);
    unsigned int clientFlags = nssTrustLevel_ToCERTCertTrustFlags(trust->clientAuth);
    if (clientFlags & (CERTDB_TRUSTED_CA | CERTDB_NS_TRUSTED_CA)) {
        clientFlags &= ~(CERTDB_TRUSTED_CA | CERTDB_NS_TRUSTED_CA);
        certTrust->sslFlags |= CERTDB_TRUSTED_CLIENT_CA;
    }
    certTrust->sslFlags |= clientFlags;
    certTrust->emailFlags = nssTrustLevel_ToCERTCertTrustFlags(trust->emailProtection);
    certTrust->objectSigningFlags = nssTrustLevel_ToCERTCertTrustFlags(trust->codeSigning);
}

NSS_IMPLEMENT nssSMIMEProfile *
nssSMIMEProfile_AddRef(nssSMIMEProfile *profile)
{
    if (profile) {
        nssPKIObject_AddRef(&profile->object);
    }
    return profile;
}

NSS_IMPLEMENT PRStatus
nssSMIMEProfile_Destroy(nssSMIMEProfile *profile)
{
    if (profile) {
        (void)nssPKIObject_Destroy(&profile->object);
    }
    return PR_SUCCESS;
}

NSS_IMPLEMENT NSSCRL *
nssCRL_Create(nssPKIObject *object)
{
    PRStatus status;
    NSSCRL *rvCRL;
    NSSArena *arena = object->arena;
    rvCRL = nss_ZNEW(arena, NSSCRL);
    if (!rvCRL) {
        return (NSSCRL *)NULL;
    }
    rvCRL->object = *object;
    /* XXX should choose instance based on some criteria */
    nssPKIObject_Lock(object);
    PR_ASSERT(object->instances != NULL && object->numInstances > 0);
    status = nssCryptokiCRL_GetAttributes(object->instances[0],
                                          NULL, /* XXX sessionOpt */
                                          arena,
                                          &rvCRL->encoding,
                                          NULL, /* subject */
                                          NULL, /* class */
                                          &rvCRL->url,
                                          &rvCRL->isKRL);
    nssPKIObject_Unlock(object);
    if (status != PR_SUCCESS) {
        if (!arena) {
            nssPKIObject_Destroy((nssPKIObject *)rvCRL);
        }
        return (NSSCRL *)NULL;
    }
    return rvCRL;
}

NSS_IMPLEMENT NSSCRL *
nssCRL_AddRef(NSSCRL *crl)
{
    if (crl) {
        nssPKIObject_AddRef(&crl->object);
    }
    return crl;
}

NSS_IMPLEMENT PRStatus
nssCRL_Destroy(NSSCRL *crl)
{
    if (crl) {
        (void)nssPKIObject_Destroy(&crl->object);
    }
    return PR_SUCCESS;
}

NSS_IMPLEMENT NSSDER *
nssCRL_GetEncoding(NSSCRL *crl)
{
    if (crl && crl->encoding.data != NULL && crl->encoding.size > 0) {
        return &crl->encoding;
    } else {
        return (NSSDER *)NULL;
    }
}
