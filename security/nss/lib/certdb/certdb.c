/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Certificate handling code
 */

#include "prmon.h"
#include "prtime.h"
#include "cert.h"
#include "certi.h"
#include "secder.h"
#include "secoid.h"
#include "secasn1.h"
#include "genname.h"
#include "keyhi.h"
#include "secitem.h"
#include "certdb.h"
#include "prprf.h"
#include "sechash.h"
#include "prlong.h"
#include "certxutl.h"
#include "portreg.h"
#include "secerr.h"
#include "sslerr.h"
#include "pk11func.h"
#include "xconst.h" /* for  CERT_DecodeAltNameExtension */

#include "pki.h"
#include "pki3hack.h"

SEC_ASN1_MKSUB(CERT_TimeChoiceTemplate)
SEC_ASN1_MKSUB(SECOID_AlgorithmIDTemplate)
SEC_ASN1_MKSUB(SEC_BitStringTemplate)
SEC_ASN1_MKSUB(SEC_IntegerTemplate)
SEC_ASN1_MKSUB(SEC_SkipTemplate)

/*
 * Certificate database handling code
 */

const SEC_ASN1Template CERT_CertExtensionTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(CERTCertExtension) },
    { SEC_ASN1_OBJECT_ID, offsetof(CERTCertExtension, id) },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_BOOLEAN, /* XXX DER_DEFAULT */
      offsetof(CERTCertExtension, critical) },
    { SEC_ASN1_OCTET_STRING, offsetof(CERTCertExtension, value) },
    { 0 }
};

const SEC_ASN1Template CERT_SequenceOfCertExtensionTemplate[] = {
    { SEC_ASN1_SEQUENCE_OF, 0, CERT_CertExtensionTemplate }
};

const SEC_ASN1Template CERT_TimeChoiceTemplate[] = {
    { SEC_ASN1_CHOICE, offsetof(SECItem, type), 0, sizeof(SECItem) },
    { SEC_ASN1_UTC_TIME, 0, 0, siUTCTime },
    { SEC_ASN1_GENERALIZED_TIME, 0, 0, siGeneralizedTime },
    { 0 }
};

const SEC_ASN1Template CERT_ValidityTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(CERTValidity) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN, offsetof(CERTValidity, notBefore),
      SEC_ASN1_SUB(CERT_TimeChoiceTemplate), 0 },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN, offsetof(CERTValidity, notAfter),
      SEC_ASN1_SUB(CERT_TimeChoiceTemplate), 0 },
    { 0 }
};

const SEC_ASN1Template CERT_CertificateTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(CERTCertificate) },
    { SEC_ASN1_EXPLICIT | SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED |
          SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 0, /* XXX DER_DEFAULT */
      offsetof(CERTCertificate, version),
      SEC_ASN1_SUB(SEC_IntegerTemplate) },
    { SEC_ASN1_INTEGER, offsetof(CERTCertificate, serialNumber) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN, offsetof(CERTCertificate, signature),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_SAVE, offsetof(CERTCertificate, derIssuer) },
    { SEC_ASN1_INLINE, offsetof(CERTCertificate, issuer), CERT_NameTemplate },
    { SEC_ASN1_INLINE, offsetof(CERTCertificate, validity),
      CERT_ValidityTemplate },
    { SEC_ASN1_SAVE, offsetof(CERTCertificate, derSubject) },
    { SEC_ASN1_INLINE, offsetof(CERTCertificate, subject), CERT_NameTemplate },
    { SEC_ASN1_SAVE, offsetof(CERTCertificate, derPublicKey) },
    { SEC_ASN1_INLINE, offsetof(CERTCertificate, subjectPublicKeyInfo),
      CERT_SubjectPublicKeyInfoTemplate },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 1,
      offsetof(CERTCertificate, issuerID),
      SEC_ASN1_SUB(SEC_BitStringTemplate) },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 2,
      offsetof(CERTCertificate, subjectID),
      SEC_ASN1_SUB(SEC_BitStringTemplate) },
    { SEC_ASN1_EXPLICIT | SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED |
          SEC_ASN1_CONTEXT_SPECIFIC | 3,
      offsetof(CERTCertificate, extensions),
      CERT_SequenceOfCertExtensionTemplate },
    { 0 }
};

const SEC_ASN1Template SEC_SignedCertificateTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(CERTCertificate) },
    { SEC_ASN1_SAVE, offsetof(CERTCertificate, signatureWrap.data) },
    { SEC_ASN1_INLINE, 0, CERT_CertificateTemplate },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(CERTCertificate, signatureWrap.signatureAlgorithm),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_BIT_STRING, offsetof(CERTCertificate, signatureWrap.signature) },
    { 0 }
};

/*
 * Find the subjectName in a DER encoded certificate
 */
const SEC_ASN1Template SEC_CertSubjectTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(SECItem) },
    { SEC_ASN1_EXPLICIT | SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED |
          SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 0,
      0, SEC_ASN1_SUB(SEC_SkipTemplate) }, /* version */
    { SEC_ASN1_SKIP },                     /* serial number */
    { SEC_ASN1_SKIP },                     /* signature algorithm */
    { SEC_ASN1_SKIP },                     /* issuer */
    { SEC_ASN1_SKIP },                     /* validity */
    { SEC_ASN1_ANY, 0, NULL },             /* subject */
    { SEC_ASN1_SKIP_REST },
    { 0 }
};

/*
 * Find the issuerName in a DER encoded certificate
 */
const SEC_ASN1Template SEC_CertIssuerTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(SECItem) },
    { SEC_ASN1_EXPLICIT | SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED |
          SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 0,
      0, SEC_ASN1_SUB(SEC_SkipTemplate) }, /* version */
    { SEC_ASN1_SKIP },                     /* serial number */
    { SEC_ASN1_SKIP },                     /* signature algorithm */
    { SEC_ASN1_ANY, 0, NULL },             /* issuer */
    { SEC_ASN1_SKIP_REST },
    { 0 }
};
/*
 * Find the subjectName in a DER encoded certificate
 */
const SEC_ASN1Template SEC_CertSerialNumberTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(SECItem) },
    { SEC_ASN1_EXPLICIT | SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED |
          SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 0,
      0, SEC_ASN1_SUB(SEC_SkipTemplate) }, /* version */
    { SEC_ASN1_ANY, 0, NULL },             /* serial number */
    { SEC_ASN1_SKIP_REST },
    { 0 }
};

/*
 * Find the issuer and serialNumber in a DER encoded certificate.
 * This data is used as the database lookup key since its the unique
 * identifier of a certificate.
 */
const SEC_ASN1Template CERT_CertKeyTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(CERTCertKey) },
    { SEC_ASN1_EXPLICIT | SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED |
          SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 0,
      0, SEC_ASN1_SUB(SEC_SkipTemplate) }, /* version */
    { SEC_ASN1_INTEGER, offsetof(CERTCertKey, serialNumber) },
    { SEC_ASN1_SKIP }, /* signature algorithm */
    { SEC_ASN1_ANY, offsetof(CERTCertKey, derIssuer) },
    { SEC_ASN1_SKIP_REST },
    { 0 }
};

SEC_ASN1_CHOOSER_IMPLEMENT(CERT_TimeChoiceTemplate)
SEC_ASN1_CHOOSER_IMPLEMENT(CERT_CertificateTemplate)
SEC_ASN1_CHOOSER_IMPLEMENT(SEC_SignedCertificateTemplate)
SEC_ASN1_CHOOSER_IMPLEMENT(CERT_SequenceOfCertExtensionTemplate)

const SEC_ASN1Template CERT_SignedDataTemplate[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(CERTSignedData) },
    { SEC_ASN1_ANY,
      offsetof(CERTSignedData, data) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(CERTSignedData, signatureAlgorithm),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_BIT_STRING,
      offsetof(CERTSignedData, signature) },
    { 0 }
};

SEC_ASN1_CHOOSER_IMPLEMENT(CERT_SignedDataTemplate)

SECStatus
CERT_KeyFromIssuerAndSN(PLArenaPool *arena, SECItem *issuer, SECItem *sn,
                        SECItem *key)
{
    key->len = sn->len + issuer->len;

    if ((sn->data == NULL) || (issuer->data == NULL)) {
        goto loser;
    }

    key->data = (unsigned char *)PORT_ArenaAlloc(arena, key->len);
    if (!key->data) {
        goto loser;
    }

    /* copy the serialNumber */
    PORT_Memcpy(key->data, sn->data, sn->len);

    /* copy the issuer */
    PORT_Memcpy(&key->data[sn->len], issuer->data, issuer->len);

    return (SECSuccess);

loser:
    return (SECFailure);
}

/*
 * Extract the subject name from a DER certificate
 */
SECStatus
CERT_NameFromDERCert(SECItem *derCert, SECItem *derName)
{
    int rv;
    PLArenaPool *arena;
    CERTSignedData sd;
    void *tmpptr;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);

    if (!arena) {
        return (SECFailure);
    }

    PORT_Memset(&sd, 0, sizeof(CERTSignedData));
    rv = SEC_QuickDERDecodeItem(arena, &sd, CERT_SignedDataTemplate, derCert);

    if (rv) {
        goto loser;
    }

    PORT_Memset(derName, 0, sizeof(SECItem));
    rv = SEC_QuickDERDecodeItem(arena, derName, SEC_CertSubjectTemplate,
                                &sd.data);

    if (rv) {
        goto loser;
    }

    tmpptr = derName->data;
    derName->data = (unsigned char *)PORT_Alloc(derName->len);
    if (derName->data == NULL) {
        goto loser;
    }

    PORT_Memcpy(derName->data, tmpptr, derName->len);

    PORT_FreeArena(arena, PR_FALSE);
    return (SECSuccess);

loser:
    PORT_FreeArena(arena, PR_FALSE);
    return (SECFailure);
}

SECStatus
CERT_IssuerNameFromDERCert(SECItem *derCert, SECItem *derName)
{
    int rv;
    PORTCheapArenaPool tmpArena;
    CERTSignedData sd;
    void *tmpptr;

    PORT_InitCheapArena(&tmpArena, DER_DEFAULT_CHUNKSIZE);

    PORT_Memset(&sd, 0, sizeof(CERTSignedData));
    rv = SEC_QuickDERDecodeItem(&tmpArena.arena, &sd, CERT_SignedDataTemplate,
                                derCert);
    if (rv) {
        goto loser;
    }

    PORT_Memset(derName, 0, sizeof(SECItem));
    rv = SEC_QuickDERDecodeItem(&tmpArena.arena, derName,
                                SEC_CertIssuerTemplate, &sd.data);
    if (rv) {
        goto loser;
    }

    tmpptr = derName->data;
    derName->data = (unsigned char *)PORT_Alloc(derName->len);
    if (derName->data == NULL) {
        goto loser;
    }

    PORT_Memcpy(derName->data, tmpptr, derName->len);

    PORT_DestroyCheapArena(&tmpArena);
    return (SECSuccess);

loser:
    PORT_DestroyCheapArena(&tmpArena);
    return (SECFailure);
}

SECStatus
CERT_SerialNumberFromDERCert(SECItem *derCert, SECItem *derName)
{
    int rv;
    PORTCheapArenaPool tmpArena;
    CERTSignedData sd;
    void *tmpptr;

    PORT_InitCheapArena(&tmpArena, DER_DEFAULT_CHUNKSIZE);

    PORT_Memset(&sd, 0, sizeof(CERTSignedData));
    rv = SEC_QuickDERDecodeItem(&tmpArena.arena, &sd, CERT_SignedDataTemplate,
                                derCert);
    if (rv) {
        goto loser;
    }

    PORT_Memset(derName, 0, sizeof(SECItem));
    rv = SEC_QuickDERDecodeItem(&tmpArena.arena, derName,
                                SEC_CertSerialNumberTemplate, &sd.data);
    if (rv) {
        goto loser;
    }

    tmpptr = derName->data;
    derName->data = (unsigned char *)PORT_Alloc(derName->len);
    if (derName->data == NULL) {
        goto loser;
    }

    PORT_Memcpy(derName->data, tmpptr, derName->len);

    PORT_DestroyCheapArena(&tmpArena);
    return (SECSuccess);

loser:
    PORT_DestroyCheapArena(&tmpArena);
    return (SECFailure);
}

/*
 * Generate a database key, based on serial number and issuer, from a
 * DER certificate.
 */
SECStatus
CERT_KeyFromDERCert(PLArenaPool *reqArena, SECItem *derCert, SECItem *key)
{
    int rv;
    CERTSignedData sd;
    CERTCertKey certkey;

    if (!reqArena) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    PORT_Memset(&sd, 0, sizeof(CERTSignedData));
    rv =
        SEC_QuickDERDecodeItem(reqArena, &sd, CERT_SignedDataTemplate, derCert);

    if (rv) {
        goto loser;
    }

    PORT_Memset(&certkey, 0, sizeof(CERTCertKey));
    rv = SEC_QuickDERDecodeItem(reqArena, &certkey, CERT_CertKeyTemplate,
                                &sd.data);

    if (rv) {
        goto loser;
    }

    return (CERT_KeyFromIssuerAndSN(reqArena, &certkey.derIssuer,
                                    &certkey.serialNumber, key));
loser:
    return (SECFailure);
}

/*
 * fill in keyUsage field of the cert based on the cert extension
 * if the extension is not critical, then we allow all uses
 */
static SECStatus
GetKeyUsage(CERTCertificate *cert)
{
    SECStatus rv;
    SECItem tmpitem;

    rv = CERT_FindKeyUsageExtension(cert, &tmpitem);
    if (rv == SECSuccess) {
        /* remember the actual value of the extension */
        cert->rawKeyUsage = tmpitem.len ? tmpitem.data[0] : 0;
        cert->keyUsagePresent = PR_TRUE;
        cert->keyUsage = cert->rawKeyUsage;

        PORT_Free(tmpitem.data);
        tmpitem.data = NULL;
    } else {
        /* if the extension is not present, then we allow all uses */
        cert->keyUsage = KU_ALL;
        cert->rawKeyUsage = KU_ALL;
        cert->keyUsagePresent = PR_FALSE;
    }

    if (CERT_GovtApprovedBitSet(cert)) {
        cert->keyUsage |= KU_NS_GOVT_APPROVED;
        cert->rawKeyUsage |= KU_NS_GOVT_APPROVED;
    }

    return (SECSuccess);
}

static SECStatus
findOIDinOIDSeqByTagNum(CERTOidSequence *seq, SECOidTag tagnum)
{
    SECItem **oids;
    SECItem *oid;
    SECStatus rv = SECFailure;

    if (seq != NULL) {
        oids = seq->oids;
        while (oids != NULL && *oids != NULL) {
            oid = *oids;
            if (SECOID_FindOIDTag(oid) == tagnum) {
                rv = SECSuccess;
                break;
            }
            oids++;
        }
    }
    return rv;
}

/*
 * fill in nsCertType field of the cert based on the cert extension
 */
SECStatus
cert_GetCertType(CERTCertificate *cert)
{
    PRUint32 nsCertType;

    if (cert->nsCertType) {
        /* once set, no need to recalculate */
        return SECSuccess;
    }
    nsCertType = cert_ComputeCertType(cert);

    /* Assert that it is safe to cast &cert->nsCertType to "PRInt32 *" */
    PORT_Assert(sizeof(cert->nsCertType) == sizeof(PRInt32));
    PR_ATOMIC_SET((PRInt32 *)&cert->nsCertType, nsCertType);
    return SECSuccess;
}

PRBool
cert_IsIPsecOID(CERTOidSequence *extKeyUsage)
{
    if (findOIDinOIDSeqByTagNum(
            extKeyUsage, SEC_OID_EXT_KEY_USAGE_IPSEC_IKE) == SECSuccess) {
        return PR_TRUE;
    }
    if (findOIDinOIDSeqByTagNum(
            extKeyUsage, SEC_OID_IPSEC_IKE_END) == SECSuccess) {
        return PR_TRUE;
    }
    if (findOIDinOIDSeqByTagNum(
            extKeyUsage, SEC_OID_IPSEC_IKE_INTERMEDIATE) == SECSuccess) {
        return PR_TRUE;
    }
    /* these are now deprecated, but may show up. Treat them the same as IKE */
    if (findOIDinOIDSeqByTagNum(
            extKeyUsage, SEC_OID_EXT_KEY_USAGE_IPSEC_END) == SECSuccess) {
        return PR_TRUE;
    }
    if (findOIDinOIDSeqByTagNum(
            extKeyUsage, SEC_OID_EXT_KEY_USAGE_IPSEC_TUNNEL) == SECSuccess) {
        return PR_TRUE;
    }
    if (findOIDinOIDSeqByTagNum(
            extKeyUsage, SEC_OID_EXT_KEY_USAGE_IPSEC_USER) == SECSuccess) {
        return PR_TRUE;
    }
    /* this one should probably be in cert_ComputeCertType and set all usages? */
    if (findOIDinOIDSeqByTagNum(
            extKeyUsage, SEC_OID_X509_ANY_EXT_KEY_USAGE) == SECSuccess) {
        return PR_TRUE;
    }
    return PR_FALSE;
}

PRUint32
cert_ComputeCertType(CERTCertificate *cert)
{
    SECStatus rv;
    SECItem tmpitem;
    SECItem encodedExtKeyUsage;
    CERTOidSequence *extKeyUsage = NULL;
    CERTBasicConstraints basicConstraint;
    PRUint32 nsCertType = 0;
    PRBool isCA = PR_FALSE;

    tmpitem.data = NULL;
    CERT_FindNSCertTypeExtension(cert, &tmpitem);
    encodedExtKeyUsage.data = NULL;
    rv = CERT_FindCertExtension(cert, SEC_OID_X509_EXT_KEY_USAGE,
                                &encodedExtKeyUsage);
    if (rv == SECSuccess) {
        extKeyUsage = CERT_DecodeOidSequence(&encodedExtKeyUsage);
    }
    rv = CERT_FindBasicConstraintExten(cert, &basicConstraint);
    if (rv == SECSuccess) {
        isCA = basicConstraint.isCA;
    }
    if (tmpitem.data != NULL || extKeyUsage != NULL) {
        if (tmpitem.data == NULL || tmpitem.len == 0) {
            nsCertType = 0;
        } else {
            nsCertType = tmpitem.data[0];
        }

        /* free tmpitem data pointer to avoid memory leak */
        PORT_Free(tmpitem.data);
        tmpitem.data = NULL;

        /*
         * for this release, we will allow SSL certs with an email address
         * to be used for email
         */
        if ((nsCertType & NS_CERT_TYPE_SSL_CLIENT) && cert->emailAddr &&
            cert->emailAddr[0]) {
            nsCertType |= NS_CERT_TYPE_EMAIL;
        }
        /*
         * for this release, we will allow SSL intermediate CAs to be
         * email intermediate CAs too.
         */
        if (nsCertType & NS_CERT_TYPE_SSL_CA) {
            nsCertType |= NS_CERT_TYPE_EMAIL_CA;
        }
        /*
         * allow a cert with the extended key usage of EMail Protect
         * to be used for email or as an email CA, if basic constraints
         * indicates that it is a CA.
         */
        if (findOIDinOIDSeqByTagNum(extKeyUsage,
                                    SEC_OID_EXT_KEY_USAGE_EMAIL_PROTECT) ==
            SECSuccess) {
            nsCertType |= isCA ? NS_CERT_TYPE_EMAIL_CA : NS_CERT_TYPE_EMAIL;
        }
        if (findOIDinOIDSeqByTagNum(
                extKeyUsage, SEC_OID_EXT_KEY_USAGE_SERVER_AUTH) == SECSuccess) {
            nsCertType |= isCA ? NS_CERT_TYPE_SSL_CA : NS_CERT_TYPE_SSL_SERVER;
        }
        /*
         * Treat certs with step-up OID as also having SSL server type.
         * COMODO needs this behaviour until June 2020.  See Bug 737802.
         */
        if (findOIDinOIDSeqByTagNum(extKeyUsage,
                                    SEC_OID_NS_KEY_USAGE_GOVT_APPROVED) ==
            SECSuccess) {
            nsCertType |= isCA ? NS_CERT_TYPE_SSL_CA : NS_CERT_TYPE_SSL_SERVER;
        }
        if (findOIDinOIDSeqByTagNum(
                extKeyUsage, SEC_OID_EXT_KEY_USAGE_CLIENT_AUTH) == SECSuccess) {
            nsCertType |= isCA ? NS_CERT_TYPE_SSL_CA : NS_CERT_TYPE_SSL_CLIENT;
        }
        if (cert_IsIPsecOID(extKeyUsage)) {
            nsCertType |= isCA ? NS_CERT_TYPE_IPSEC_CA : NS_CERT_TYPE_IPSEC;
        }
        if (findOIDinOIDSeqByTagNum(
                extKeyUsage, SEC_OID_EXT_KEY_USAGE_CODE_SIGN) == SECSuccess) {
            nsCertType |= isCA ? NS_CERT_TYPE_OBJECT_SIGNING_CA : NS_CERT_TYPE_OBJECT_SIGNING;
        }
        if (findOIDinOIDSeqByTagNum(
                extKeyUsage, SEC_OID_EXT_KEY_USAGE_TIME_STAMP) == SECSuccess) {
            nsCertType |= EXT_KEY_USAGE_TIME_STAMP;
        }
        if (findOIDinOIDSeqByTagNum(extKeyUsage, SEC_OID_OCSP_RESPONDER) ==
            SECSuccess) {
            nsCertType |= EXT_KEY_USAGE_STATUS_RESPONDER;
        }
    } else {
        /* If no NS Cert Type extension and no EKU extension, then */
        nsCertType = 0;
        if (CERT_IsCACert(cert, &nsCertType))
            nsCertType |= EXT_KEY_USAGE_STATUS_RESPONDER;
        /* if the basic constraint extension says the cert is a CA, then
           allow SSL CA and EMAIL CA and Status Responder */
        if (isCA) {
            nsCertType |= (NS_CERT_TYPE_SSL_CA | NS_CERT_TYPE_EMAIL_CA |
                           EXT_KEY_USAGE_STATUS_RESPONDER);
        }
        /* allow any ssl or email (no ca or object signing. */
        nsCertType |= NS_CERT_TYPE_SSL_CLIENT | NS_CERT_TYPE_SSL_SERVER |
                      NS_CERT_TYPE_EMAIL;
    }

    /* IPSEC is allowed to use SSL client and server certs as well as email certs */
    if (nsCertType & (NS_CERT_TYPE_SSL_CLIENT | NS_CERT_TYPE_SSL_SERVER | NS_CERT_TYPE_EMAIL)) {
        nsCertType |= NS_CERT_TYPE_IPSEC;
    }
    if (nsCertType & (NS_CERT_TYPE_SSL_CA | NS_CERT_TYPE_EMAIL_CA)) {
        nsCertType |= NS_CERT_TYPE_IPSEC_CA;
    }

    if (encodedExtKeyUsage.data != NULL) {
        PORT_Free(encodedExtKeyUsage.data);
    }
    if (extKeyUsage != NULL) {
        CERT_DestroyOidSequence(extKeyUsage);
    }
    return nsCertType;
}

/*
 * cert_GetKeyID() - extract or generate the subjectKeyID from a certificate
 */
SECStatus
cert_GetKeyID(CERTCertificate *cert)
{
    SECItem tmpitem;
    SECStatus rv;

    cert->subjectKeyID.len = 0;

    /* see of the cert has a key identifier extension */
    rv = CERT_FindSubjectKeyIDExtension(cert, &tmpitem);
    if (rv == SECSuccess) {
        cert->subjectKeyID.data =
            (unsigned char *)PORT_ArenaAlloc(cert->arena, tmpitem.len);
        if (cert->subjectKeyID.data != NULL) {
            PORT_Memcpy(cert->subjectKeyID.data, tmpitem.data, tmpitem.len);
            cert->subjectKeyID.len = tmpitem.len;
            cert->keyIDGenerated = PR_FALSE;
        }

        PORT_Free(tmpitem.data);
    }

    /* if the cert doesn't have a key identifier extension, then generate one*/
    if (cert->subjectKeyID.len == 0) {
        /*
         * pkix says that if the subjectKeyID is not present, then we should
         * use the SHA-1 hash of the DER-encoded publicKeyInfo from the cert
         */
        cert->subjectKeyID.data =
            (unsigned char *)PORT_ArenaAlloc(cert->arena, SHA1_LENGTH);
        if (cert->subjectKeyID.data != NULL) {
            rv = PK11_HashBuf(SEC_OID_SHA1, cert->subjectKeyID.data,
                              cert->derPublicKey.data, cert->derPublicKey.len);
            if (rv == SECSuccess) {
                cert->subjectKeyID.len = SHA1_LENGTH;
            }
        }
    }

    if (cert->subjectKeyID.len == 0) {
        return (SECFailure);
    }
    return (SECSuccess);
}

static PRBool
cert_IsRootCert(CERTCertificate *cert)
{
    SECStatus rv;
    SECItem tmpitem;

    /* cache the authKeyID extension, if present */
    cert->authKeyID = CERT_FindAuthKeyIDExten(cert->arena, cert);

    /* it MUST be self-issued to be a root */
    if (cert->derIssuer.len == 0 ||
        !SECITEM_ItemsAreEqual(&cert->derIssuer, &cert->derSubject)) {
        return PR_FALSE;
    }

    /* check the authKeyID extension */
    if (cert->authKeyID) {
        /* authority key identifier is present */
        if (cert->authKeyID->keyID.len > 0) {
            /* the keyIdentifier field is set, look for subjectKeyID */
            rv = CERT_FindSubjectKeyIDExtension(cert, &tmpitem);
            if (rv == SECSuccess) {
                PRBool match;
                /* also present, they MUST match for it to be a root */
                match =
                    SECITEM_ItemsAreEqual(&cert->authKeyID->keyID, &tmpitem);
                PORT_Free(tmpitem.data);
                if (!match)
                    return PR_FALSE; /* else fall through */
            } else {
                /* the subject key ID is required when AKI is present */
                return PR_FALSE;
            }
        }
        if (cert->authKeyID->authCertIssuer) {
            SECItem *caName;
            caName = (SECItem *)CERT_GetGeneralNameByType(
                cert->authKeyID->authCertIssuer, certDirectoryName, PR_TRUE);
            if (caName) {
                if (!SECITEM_ItemsAreEqual(&cert->derIssuer, caName)) {
                    return PR_FALSE;
                } /* else fall through */
            } /* else ??? could not get general name as directory name? */
        }
        if (cert->authKeyID->authCertSerialNumber.len > 0) {
            if (!SECITEM_ItemsAreEqual(
                    &cert->serialNumber,
                    &cert->authKeyID->authCertSerialNumber)) {
                return PR_FALSE;
            } /* else fall through */
        }
        /* all of the AKI fields that were present passed the test */
        return PR_TRUE;
    }
    /* else the AKI was not present, so this is a root */
    return PR_TRUE;
}

/*
 * take a DER certificate and decode it into a certificate structure
 */
CERTCertificate *
CERT_DecodeDERCertificate(SECItem *derSignedCert, PRBool copyDER,
                          char *nickname)
{
    CERTCertificate *cert;
    PLArenaPool *arena;
    void *data;
    int rv;
    int len;
    char *tmpname;
    PRErrorCode savedErr;

    /* Several of the steps below look for an optional extension and carry on
     * when it is absent, leaving SEC_ERROR_EXTENSION_NOT_FOUND set behind them.
     * A successful decode should not disturb the caller's error code, so save
     * it here and restore it on the success path.
     */
    savedErr = PORT_GetError();

    /* make a new arena */
    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);

    if (!arena) {
        return 0;
    }

    /* allocate the certificate structure */
    cert = (CERTCertificate *)PORT_ArenaZAlloc(arena, sizeof(CERTCertificate));

    if (!cert) {
        goto loser;
    }

    cert->arena = arena;

    if (copyDER) {
        /* copy the DER data for the cert into this arena */
        data = (void *)PORT_ArenaAlloc(arena, derSignedCert->len);
        if (!data) {
            goto loser;
        }
        cert->derCert.data = (unsigned char *)data;
        cert->derCert.len = derSignedCert->len;
        PORT_Memcpy(data, derSignedCert->data, derSignedCert->len);
    } else {
        /* point to passed in DER data */
        cert->derCert = *derSignedCert;
    }

    /* decode the certificate info */
    rv = SEC_QuickDERDecodeItem(arena, cert, SEC_SignedCertificateTemplate,
                                &cert->derCert);

    if (rv) {
        goto loser;
    }

    if (cert_HasUnknownCriticalExten(cert->extensions) == PR_TRUE) {
        cert->options.bits.hasUnsupportedCriticalExt = PR_TRUE;
    }

    /* generate and save the database key for the cert */
    rv = CERT_KeyFromIssuerAndSN(arena, &cert->derIssuer, &cert->serialNumber,
                                 &cert->certKey);
    if (rv) {
        goto loser;
    }

    /* set the nickname */
    if (nickname == NULL) {
        cert->nickname = NULL;
    } else {
        /* copy and install the nickname */
        len = PORT_Strlen(nickname) + 1;
        cert->nickname = (char *)PORT_ArenaAlloc(arena, len);
        if (cert->nickname == NULL) {
            goto loser;
        }

        PORT_Memcpy(cert->nickname, nickname, len);
    }

    /* set the email address */
    cert->emailAddr = cert_GetCertificateEmailAddresses(cert);

    /* initialize the subjectKeyID */
    rv = cert_GetKeyID(cert);
    if (rv != SECSuccess) {
        goto loser;
    }

    /* initialize keyUsage */
    rv = GetKeyUsage(cert);
    if (rv != SECSuccess) {
        goto loser;
    }

    /* determine if this is a root cert */
    cert->isRoot = cert_IsRootCert(cert);

    /* initialize the certType */
    rv = cert_GetCertType(cert);
    if (rv != SECSuccess) {
        goto loser;
    }

    tmpname = CERT_NameToAscii(&cert->subject);
    if (tmpname != NULL) {
        cert->subjectName = PORT_ArenaStrdup(cert->arena, tmpname);
        PORT_Free(tmpname);
    }

    tmpname = CERT_NameToAscii(&cert->issuer);
    if (tmpname != NULL) {
        cert->issuerName = PORT_ArenaStrdup(cert->arena, tmpname);
        PORT_Free(tmpname);
    }

    cert->referenceCount = 1;
    cert->slot = NULL;
    cert->pkcs11ID = CK_INVALID_HANDLE;
    cert->dbnickname = NULL;

    PORT_SetError(savedErr);
    return (cert);

loser:

    if (arena) {
        PORT_FreeArena(arena, PR_FALSE);
    }

    return (0);
}

CERTCertificate *
__CERT_DecodeDERCertificate(SECItem *derSignedCert, PRBool copyDER,
                            char *nickname)
{
    return CERT_DecodeDERCertificate(derSignedCert, copyDER, nickname);
}

CERTValidity *
CERT_CreateValidity(PRTime notBefore, PRTime notAfter)
{
    CERTValidity *v;
    int rv;
    PLArenaPool *arena;

    if (notBefore > notAfter) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }
    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);

    if (!arena) {
        return (0);
    }

    v = (CERTValidity *)PORT_ArenaZAlloc(arena, sizeof(CERTValidity));
    if (v) {
        v->arena = arena;
        rv = DER_EncodeTimeChoice(arena, &v->notBefore, notBefore);
        if (rv)
            goto loser;
        rv = DER_EncodeTimeChoice(arena, &v->notAfter, notAfter);
        if (rv)
            goto loser;
    }
    return v;

loser:
    CERT_DestroyValidity(v);
    return 0;
}

SECStatus
CERT_CopyValidity(PLArenaPool *arena, CERTValidity *to, CERTValidity *from)
{
    SECStatus rv;

    CERT_DestroyValidity(to);
    to->arena = arena;

    rv = SECITEM_CopyItem(arena, &to->notBefore, &from->notBefore);
    if (rv)
        return rv;
    rv = SECITEM_CopyItem(arena, &to->notAfter, &from->notAfter);
    return rv;
}

void
CERT_DestroyValidity(CERTValidity *v)
{
    if (v && v->arena) {
        PORT_FreeArena(v->arena, PR_FALSE);
    }
    return;
}

/*
** Amount of time that a certifiate is allowed good before it is actually
** good. This is used for pending certificates, ones that are about to be
** valid. The slop is designed to allow for some variance in the clocks
** of the machine checking the certificate.
*/
#define PENDING_SLOP (24L * 60L * 60L)     /* seconds per day */
static PRInt32 pendingSlop = PENDING_SLOP; /* seconds */

PRInt32
CERT_GetSlopTime(void)
{
    return pendingSlop; /* seconds */
}

SECStatus
CERT_SetSlopTime(PRInt32 slop) /* seconds */
{
    if (slop < 0)
        return SECFailure;
    pendingSlop = slop;
    return SECSuccess;
}

SECStatus
CERT_GetCertTimes(const CERTCertificate *c, PRTime *notBefore, PRTime *notAfter)
{
    SECStatus rv;

    if (!c || !notBefore || !notAfter) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    /* convert DER not-before time */
    rv = DER_DecodeTimeChoice(notBefore, &c->validity.notBefore);
    if (rv) {
        return (SECFailure);
    }

    /* convert DER not-after time */
    rv = DER_DecodeTimeChoice(notAfter, &c->validity.notAfter);
    if (rv) {
        return (SECFailure);
    }

    return (SECSuccess);
}

/*
 * Check the validity times of a certificate
 */
SECCertTimeValidity
CERT_CheckCertValidTimes(const CERTCertificate *c, PRTime t,
                         PRBool allowOverride)
{
    PRTime notBefore, notAfter, llPendingSlop, tmp1;
    SECStatus rv;

    if (!c) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return (secCertTimeUndetermined);
    }
    /* if cert is already marked OK, then don't bother to check */
    if (allowOverride && c->timeOK) {
        return (secCertTimeValid);
    }

    rv = CERT_GetCertTimes(c, &notBefore, &notAfter);

    if (rv) {
        return (secCertTimeExpired); /*XXX is this the right thing to do here?*/
    }

    LL_I2L(llPendingSlop, pendingSlop);
    /* convert to micro seconds */
    LL_UI2L(tmp1, PR_USEC_PER_SEC);
    LL_MUL(llPendingSlop, llPendingSlop, tmp1);
    LL_SUB(notBefore, notBefore, llPendingSlop);
    if (LL_CMP(t, <, notBefore)) {
        PORT_SetError(SEC_ERROR_EXPIRED_CERTIFICATE);
        return (secCertTimeNotValidYet);
    }
    if (LL_CMP(t, >, notAfter)) {
        PORT_SetError(SEC_ERROR_EXPIRED_CERTIFICATE);
        return (secCertTimeExpired);
    }

    return (secCertTimeValid);
}

SECStatus
SEC_GetCrlTimes(CERTCrl *date, PRTime *notBefore, PRTime *notAfter)
{
    int rv;

    /* convert DER not-before time */
    rv = DER_DecodeTimeChoice(notBefore, &date->lastUpdate);
    if (rv) {
        return (SECFailure);
    }

    /* convert DER not-after time */
    if (date->nextUpdate.data) {
        rv = DER_DecodeTimeChoice(notAfter, &date->nextUpdate);
        if (rv) {
            return (SECFailure);
        }
    } else {
        LL_I2L(*notAfter, 0L);
    }
    return (SECSuccess);
}

/* These routines should probably be combined with the cert
 * routines using an common extraction routine.
 */
SECCertTimeValidity
SEC_CheckCrlTimes(CERTCrl *crl, PRTime t)
{
    PRTime notBefore, notAfter, llPendingSlop, tmp1;
    SECStatus rv;

    if (!crl) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return (secCertTimeUndetermined);
    }

    rv = SEC_GetCrlTimes(crl, &notBefore, &notAfter);

    if (rv) {
        return (secCertTimeExpired);
    }

    LL_I2L(llPendingSlop, pendingSlop);
    /* convert to micro seconds */
    LL_I2L(tmp1, PR_USEC_PER_SEC);
    LL_MUL(llPendingSlop, llPendingSlop, tmp1);
    LL_SUB(notBefore, notBefore, llPendingSlop);
    if (LL_CMP(t, <, notBefore)) {
        PORT_SetError(SEC_ERROR_CRL_EXPIRED);
        return (secCertTimeNotValidYet);
    }

    /* If next update is omitted and the test for notBefore passes, then
       we assume that the crl is up to date.
     */
    if (LL_IS_ZERO(notAfter)) {
        return (secCertTimeValid);
    }

    if (LL_CMP(t, >, notAfter)) {
        PORT_SetError(SEC_ERROR_CRL_EXPIRED);
        return (secCertTimeExpired);
    }

    return (secCertTimeValid);
}

PRBool
SEC_CrlIsNewer(CERTCrl *inNew, CERTCrl *old)
{
    PRTime newNotBefore, newNotAfter;
    PRTime oldNotBefore, oldNotAfter;
    SECStatus rv;

    /* problems with the new CRL? reject it */
    rv = SEC_GetCrlTimes(inNew, &newNotBefore, &newNotAfter);
    if (rv)
        return PR_FALSE;

    /* problems with the old CRL? replace it */
    rv = SEC_GetCrlTimes(old, &oldNotBefore, &oldNotAfter);
    if (rv)
        return PR_TRUE;

    /* Question: what about the notAfter's? */
    return ((PRBool)LL_CMP(oldNotBefore, <, newNotBefore));
}

/*
 * return required key usage and cert type based on cert usage
 */
SECStatus
CERT_KeyUsageAndTypeForCertUsage(SECCertUsage usage, PRBool ca,
                                 unsigned int *retKeyUsage,
                                 unsigned int *retCertType)
{
    unsigned int requiredKeyUsage = 0;
    unsigned int requiredCertType = 0;

    if (ca) {
        switch (usage) {
            case certUsageSSLServerWithStepUp:
                requiredKeyUsage = KU_NS_GOVT_APPROVED | KU_KEY_CERT_SIGN;
                requiredCertType = NS_CERT_TYPE_SSL_CA;
                break;
            case certUsageSSLClient:
                requiredKeyUsage = KU_KEY_CERT_SIGN;
                requiredCertType = NS_CERT_TYPE_SSL_CA;
                break;
            case certUsageSSLServer:
                requiredKeyUsage = KU_KEY_CERT_SIGN;
                requiredCertType = NS_CERT_TYPE_SSL_CA;
                break;
            case certUsageIPsec:
                requiredKeyUsage = KU_KEY_CERT_SIGN;
                requiredCertType = NS_CERT_TYPE_IPSEC_CA;
                break;
            case certUsageSSLCA:
                requiredKeyUsage = KU_KEY_CERT_SIGN;
                requiredCertType = NS_CERT_TYPE_SSL_CA;
                break;
            case certUsageEmailSigner:
                requiredKeyUsage = KU_KEY_CERT_SIGN;
                requiredCertType = NS_CERT_TYPE_EMAIL_CA;
                break;
            case certUsageEmailRecipient:
                requiredKeyUsage = KU_KEY_CERT_SIGN;
                requiredCertType = NS_CERT_TYPE_EMAIL_CA;
                break;
            case certUsageObjectSigner:
                requiredKeyUsage = KU_KEY_CERT_SIGN;
                requiredCertType = NS_CERT_TYPE_OBJECT_SIGNING_CA;
                break;
            case certUsageAnyCA:
            case certUsageVerifyCA:
            case certUsageStatusResponder:
                requiredKeyUsage = KU_KEY_CERT_SIGN;
                requiredCertType = NS_CERT_TYPE_OBJECT_SIGNING_CA |
                                   NS_CERT_TYPE_EMAIL_CA | NS_CERT_TYPE_SSL_CA;
                break;
            default:
                PORT_Assert(0);
                goto loser;
        }
    } else {
        switch (usage) {
            case certUsageSSLClient:
                /*
                 * RFC 5280 lists digitalSignature and keyAgreement for
                 * id-kp-clientAuth.  NSS does not support the *_fixed_dh and
                 * *_fixed_ecdh client certificate types.
                 */
                requiredKeyUsage = KU_DIGITAL_SIGNATURE;
                requiredCertType = NS_CERT_TYPE_SSL_CLIENT;
                break;
            case certUsageSSLServer:
                requiredKeyUsage = KU_KEY_AGREEMENT_OR_ENCIPHERMENT;
                requiredCertType = NS_CERT_TYPE_SSL_SERVER;
                break;
            case certUsageIPsec:
                /* RFC 4945 Section 5.1.3.2 */
                requiredKeyUsage = KU_DIGITAL_SIGNATURE_OR_NON_REPUDIATION;
                requiredCertType = NS_CERT_TYPE_IPSEC;
                break;
            case certUsageSSLServerWithStepUp:
                requiredKeyUsage =
                    KU_KEY_AGREEMENT_OR_ENCIPHERMENT | KU_NS_GOVT_APPROVED;
                requiredCertType = NS_CERT_TYPE_SSL_SERVER;
                break;
            case certUsageSSLCA:
                requiredKeyUsage = KU_KEY_CERT_SIGN;
                requiredCertType = NS_CERT_TYPE_SSL_CA;
                break;
            case certUsageEmailSigner:
                requiredKeyUsage = KU_DIGITAL_SIGNATURE_OR_NON_REPUDIATION;
                requiredCertType = NS_CERT_TYPE_EMAIL;
                break;
            case certUsageEmailRecipient:
                requiredKeyUsage = KU_KEY_AGREEMENT_OR_ENCIPHERMENT;
                requiredCertType = NS_CERT_TYPE_EMAIL;
                break;
            case certUsageObjectSigner:
                /* RFC 5280 lists only digitalSignature for id-kp-codeSigning.
                 */
                requiredKeyUsage = KU_DIGITAL_SIGNATURE;
                requiredCertType = NS_CERT_TYPE_OBJECT_SIGNING;
                break;
            case certUsageStatusResponder:
                requiredKeyUsage = KU_DIGITAL_SIGNATURE_OR_NON_REPUDIATION;
                requiredCertType = EXT_KEY_USAGE_STATUS_RESPONDER;
                break;
            default:
                PORT_Assert(0);
                goto loser;
        }
    }

    if (retKeyUsage != NULL) {
        *retKeyUsage = requiredKeyUsage;
    }
    if (retCertType != NULL) {
        *retCertType = requiredCertType;
    }

    return (SECSuccess);
loser:
    return (SECFailure);
}

/*
 * check the key usage of a cert against a set of required values
 */
SECStatus
CERT_CheckKeyUsage(CERTCertificate *cert, unsigned int requiredUsage)
{
    if (!cert) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    /* choose between key agreement or key encipherment based on key
     * type in cert
     */
    if (requiredUsage & KU_KEY_AGREEMENT_OR_ENCIPHERMENT) {
        KeyType keyType = CERT_GetCertKeyType(&cert->subjectPublicKeyInfo);
        /* turn off the special bit */
        requiredUsage &= (~KU_KEY_AGREEMENT_OR_ENCIPHERMENT);

        switch (keyType) {
            case rsaKey:
            case kyberKey: /* See RFC 9935, Section 5. */
                requiredUsage |= KU_KEY_ENCIPHERMENT;
                break;
            case rsaPssKey:
            case dsaKey:
            case mldsaKey:
                requiredUsage |= KU_DIGITAL_SIGNATURE;
                break;
            case dhKey:
                requiredUsage |= KU_KEY_AGREEMENT;
                break;
            case ecKey:
                /* Accept either signature or agreement. */
                if (!(cert->keyUsage &
                      (KU_DIGITAL_SIGNATURE | KU_KEY_AGREEMENT)))
                    goto loser;
                break;
            default:
                goto loser;
        }
    }

    /* Allow either digital signature or non-repudiation */
    if (requiredUsage & KU_DIGITAL_SIGNATURE_OR_NON_REPUDIATION) {
        /* turn off the special bit */
        requiredUsage &= (~KU_DIGITAL_SIGNATURE_OR_NON_REPUDIATION);

        if (!(cert->keyUsage & (KU_DIGITAL_SIGNATURE | KU_NON_REPUDIATION)))
            goto loser;
    }

    if ((cert->keyUsage & requiredUsage) == requiredUsage)
        return SECSuccess;

loser:
    PORT_SetError(SEC_ERROR_INADEQUATE_KEY_USAGE);
    return SECFailure;
}

CERTCertificate *
CERT_DupCertificate(CERTCertificate *c)
{
    if (c) {
        NSSCertificate *tmp = STAN_GetNSSCertificate(c);
        nssCertificate_AddRef(tmp);
    }
    return c;
}

SECStatus
CERT_GetCertificateDer(const CERTCertificate *c, SECItem *der)
{
    if (!c || !der) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    *der = c->derCert;
    return SECSuccess;
}

/*
 * Allow use of default cert database, so that apps(such as mozilla) don't
 * have to pass the handle all over the place.
 */
static CERTCertDBHandle *default_cert_db_handle = 0;

void
CERT_SetDefaultCertDB(CERTCertDBHandle *handle)
{
    default_cert_db_handle = handle;

    return;
}

CERTCertDBHandle *
CERT_GetDefaultCertDB(void)
{
    return (default_cert_db_handle);
}

/* XXX this would probably be okay/better as an xp routine? */
static void
sec_lower_string(char *s)
{
    if (s == NULL) {
        return;
    }

    while (*s) {
        *s = PORT_Tolower((unsigned char)*s);
        s++;
    }

    return;
}

static PRBool
cert_IsIPAddr(const char *hn)
{
    PRBool isIPaddr = PR_FALSE;
    PRNetAddr netAddr;
    isIPaddr = (PR_SUCCESS == PR_StringToNetAddr(hn, &netAddr));
    return isIPaddr;
}

/*
** Add a domain name to the list of names that the user has explicitly
** allowed (despite cert name mismatches) for use with a server cert.
*/
SECStatus
CERT_AddOKDomainName(CERTCertificate *cert, const char *hn)
{
    CERTOKDomainName *domainOK;
    int newNameLen;

    if (!hn || !(newNameLen = strlen(hn))) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    domainOK = (CERTOKDomainName *)PORT_ArenaZAlloc(cert->arena, sizeof(*domainOK));
    if (!domainOK) {
        return SECFailure; /* error code is already set. */
    }
    domainOK->name = (char *)PORT_ArenaZAlloc(cert->arena, newNameLen + 1);
    if (!domainOK->name) {
        return SECFailure; /* error code is already set. */
    }

    PORT_Strncpy(domainOK->name, hn, newNameLen + 1);
    sec_lower_string(domainOK->name);

    /* put at head of list. */
    domainOK->next = cert->domainOK;
    cert->domainOK = domainOK;
    return SECSuccess;
}

/* returns SECSuccess if hn matches pattern cn,
** returns SECFailure with SSL_ERROR_BAD_CERT_DOMAIN if no match,
** returns SECFailure with some other error code if another error occurs.
**
** This function may modify string cn, so caller must pass a modifiable copy.
*/
static SECStatus
cert_TestHostName(char *cn, const char *hn)
{
    static int useShellExp = -1;

    if (useShellExp < 0) {
        useShellExp = (NULL != PR_GetEnvSecure("NSS_USE_SHEXP_IN_CERT_NAME"));
    }
    if (useShellExp) {
        /* Backward compatible code, uses Shell Expressions (SHEXP). */
        int regvalid = PORT_RegExpValid(cn);
        if (regvalid != NON_SXP) {
            SECStatus rv;
            /* cn is a regular expression, try to match the shexp */
            int match = PORT_RegExpCaseSearch(hn, cn);

            if (match == 0) {
                rv = SECSuccess;
            } else {
                PORT_SetError(SSL_ERROR_BAD_CERT_DOMAIN);
                rv = SECFailure;
            }
            return rv;
        }
    } else {
        /* New approach conforms to RFC 6125. */
        char *wildcard = PORT_Strchr(cn, '*');
        char *firstcndot = PORT_Strchr(cn, '.');
        char *secondcndot =
            firstcndot ? PORT_Strchr(firstcndot + 1, '.') : NULL;
        char *firsthndot = PORT_Strchr(hn, '.');

        /* For a cn pattern to be considered valid, the wildcard character...
         * - may occur only in a DNS name with at least 3 components, and
         * - may occur only as last character in the first component, and
         * - may be preceded by additional characters, and
         * - must not be preceded by an IDNA ACE prefix (xn--)
         */
        if (wildcard && secondcndot && secondcndot[1] && firsthndot &&
            firstcndot - wildcard == 1           /* wildcard is last char in first component */
            && secondcndot - firstcndot > 1      /* second component is non-empty */
            && PORT_Strrchr(cn, '*') == wildcard /* only one wildcard in cn */
            && !PORT_Strncasecmp(cn, hn, wildcard - cn) &&
            !PORT_Strcasecmp(firstcndot, firsthndot)
            /* If hn starts with xn--, then cn must start with wildcard */
            && (PORT_Strncasecmp(hn, "xn--", 4) || wildcard == cn)) {
            /* valid wildcard pattern match */
            return SECSuccess;
        }
    }
    /* String cn has no wildcard or shell expression.
     * Compare entire string hn with cert name.
     */
    if (PORT_Strcasecmp(hn, cn) == 0) {
        return SECSuccess;
    }

    PORT_SetError(SSL_ERROR_BAD_CERT_DOMAIN);
    return SECFailure;
}

SECStatus
cert_VerifySubjectAltName(const CERTCertificate *cert, const char *hn)
{
    PLArenaPool *arena = NULL;
    CERTGeneralName *nameList = NULL;
    CERTGeneralName *current;
    char *cn;
    int cnBufLen;
    int DNSextCount = 0;
    int IPextCount = 0;
    PRBool isIPaddr = PR_FALSE;
    SECStatus rv = SECFailure;
    SECItem subAltName;
    PRNetAddr netAddr;
    char cnbuf[128];

    subAltName.data = NULL;
    cn = cnbuf;
    cnBufLen = sizeof cnbuf;

    rv = CERT_FindCertExtension(cert, SEC_OID_X509_SUBJECT_ALT_NAME,
                                &subAltName);
    if (rv != SECSuccess) {
        goto fail;
    }
    isIPaddr = (PR_SUCCESS == PR_StringToNetAddr(hn, &netAddr));
    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (!arena)
        goto fail;

    nameList = current = CERT_DecodeAltNameExtension(arena, &subAltName);
    if (!current)
        goto fail;

    do {
        switch (current->type) {
            case certDNSName:
                if (!isIPaddr) {
                    /* DNS name current->name.other.data is not null terminated.
                    ** so must copy it.
                    */
                    int cnLen = current->name.other.len;
                    rv = CERT_RFC1485_EscapeAndQuote(
                        cn, cnBufLen, (char *)current->name.other.data, cnLen);
                    if (rv != SECSuccess &&
                        PORT_GetError() == SEC_ERROR_OUTPUT_LEN) {
                        cnBufLen =
                            cnLen * 3 + 3; /* big enough for worst case */
                        cn = (char *)PORT_ArenaAlloc(arena, cnBufLen);
                        if (!cn)
                            goto fail;
                        rv = CERT_RFC1485_EscapeAndQuote(
                            cn, cnBufLen, (char *)current->name.other.data,
                            cnLen);
                    }
                    if (rv == SECSuccess)
                        rv = cert_TestHostName(cn, hn);
                    if (rv == SECSuccess)
                        goto finish;
                }
                DNSextCount++;
                break;
            case certIPAddress:
                if (isIPaddr) {
                    int match = 0;
                    PRIPv6Addr v6Addr;
                    if (current->name.other.len == 4 && /* IP v4 address */
                        netAddr.inet.family == PR_AF_INET) {
                        match = !memcmp(&netAddr.inet.ip,
                                        current->name.other.data, 4);
                    } else if (current->name.other.len ==
                                   16 && /* IP v6 address */
                               netAddr.ipv6.family == PR_AF_INET6) {
                        match = !memcmp(&netAddr.ipv6.ip,
                                        current->name.other.data, 16);
                    } else if (current->name.other.len ==
                                   16 && /* IP v6 address */
                               netAddr.inet.family == PR_AF_INET) {
                        /* convert netAddr to ipv6, then compare. */
                        /* ipv4 must be in Network Byte Order on input. */
                        PR_ConvertIPv4AddrToIPv6(netAddr.inet.ip, &v6Addr);
                        match = !memcmp(&v6Addr, current->name.other.data, 16);
                    } else if (current->name.other.len == 4 && /* IP v4 address */
                               netAddr.inet.family == PR_AF_INET6) {
                        /* convert netAddr to ipv6, then compare. */
                        PRUint32 ipv4 = (current->name.other.data[0] << 24) |
                                        (current->name.other.data[1] << 16) |
                                        (current->name.other.data[2] << 8) |
                                        current->name.other.data[3];
                        /* ipv4 must be in Network Byte Order on input. */
                        PR_ConvertIPv4AddrToIPv6(PR_htonl(ipv4), &v6Addr);
                        match = !memcmp(&netAddr.ipv6.ip, &v6Addr, 16);
                    }
                    if (match) {
                        rv = SECSuccess;
                        goto finish;
                    }
                }
                IPextCount++;
                break;
            default:
                break;
        }
        current = CERT_GetNextGeneralName(current);
    } while (current != nameList);

fail:

    if (!(isIPaddr ? IPextCount : DNSextCount)) {
        /* no relevant value in the extension was found. */
        PORT_SetError(SEC_ERROR_EXTENSION_NOT_FOUND);
    } else {
        PORT_SetError(SSL_ERROR_BAD_CERT_DOMAIN);
    }
    rv = SECFailure;

finish:

    /* Don't free nameList, it's part of the arena. */
    if (arena) {
        PORT_FreeArena(arena, PR_FALSE);
    }

    if (subAltName.data) {
        SECITEM_FreeItem(&subAltName, PR_FALSE);
    }

    return rv;
}

/*
 * If found:
 *   - subAltName contains the extension (caller must free)
 *   - return value is the decoded namelist (allocated off arena)
 * if not found, or if failure to decode:
 *   - return value is NULL
 */
CERTGeneralName *
cert_GetSubjectAltNameList(const CERTCertificate *cert, PLArenaPool *arena)
{
    CERTGeneralName *nameList = NULL;
    SECStatus rv = SECFailure;
    SECItem subAltName;

    if (!cert || !arena)
        return NULL;

    subAltName.data = NULL;

    rv = CERT_FindCertExtension(cert, SEC_OID_X509_SUBJECT_ALT_NAME,
                                &subAltName);
    if (rv != SECSuccess)
        return NULL;

    nameList = CERT_DecodeAltNameExtension(arena, &subAltName);
    SECITEM_FreeItem(&subAltName, PR_FALSE);
    return nameList;
}

PRUint32
cert_CountDNSPatterns(CERTGeneralName *firstName)
{
    CERTGeneralName *current;
    PRUint32 count = 0;

    if (!firstName)
        return 0;

    current = firstName;
    do {
        switch (current->type) {
            case certDNSName:
            case certIPAddress:
                ++count;
                break;
            default:
                break;
        }
        current = CERT_GetNextGeneralName(current);
    } while (current != firstName);

    return count;
}

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

/* will fill nickNames,
 * will allocate all data from nickNames->arena,
 * numberOfGeneralNames should have been obtained from cert_CountDNSPatterns,
 * will ensure the numberOfGeneralNames matches the number of output entries.
 */
SECStatus
cert_GetDNSPatternsFromGeneralNames(CERTGeneralName *firstName,
                                    PRUint32 numberOfGeneralNames,
                                    CERTCertNicknames *nickNames)
{
    CERTGeneralName *currentInput;
    char **currentOutput;

    if (!firstName || !nickNames || !numberOfGeneralNames)
        return SECFailure;

    nickNames->numnicknames = numberOfGeneralNames;
    nickNames->nicknames = PORT_ArenaAlloc(
        nickNames->arena, sizeof(char *) * numberOfGeneralNames);
    if (!nickNames->nicknames)
        return SECFailure;

    currentInput = firstName;
    currentOutput = nickNames->nicknames;
    do {
        char *cn = NULL;
        char ipbuf[INET6_ADDRSTRLEN];
        PRNetAddr addr;

        if (numberOfGeneralNames < 1) {
            /* internal consistency error */
            return SECFailure;
        }

        switch (currentInput->type) {
            case certDNSName:
                /* DNS name currentInput->name.other.data is not null
                 *terminated.
                 ** so must copy it.
                 */
                cn = (char *)PORT_ArenaAlloc(nickNames->arena,
                                             currentInput->name.other.len + 1);
                if (!cn)
                    return SECFailure;
                PORT_Memcpy(cn, currentInput->name.other.data,
                            currentInput->name.other.len);
                cn[currentInput->name.other.len] = 0;
                break;
            case certIPAddress:
                if (currentInput->name.other.len == 4) {
                    addr.inet.family = PR_AF_INET;
                    memcpy(&addr.inet.ip, currentInput->name.other.data,
                           currentInput->name.other.len);
                } else if (currentInput->name.other.len == 16) {
                    addr.ipv6.family = PR_AF_INET6;
                    memcpy(&addr.ipv6.ip, currentInput->name.other.data,
                           currentInput->name.other.len);
                }
                if (PR_NetAddrToString(&addr, ipbuf, sizeof(ipbuf)) ==
                    PR_FAILURE)
                    return SECFailure;
                cn = PORT_ArenaStrdup(nickNames->arena, ipbuf);
                if (!cn)
                    return SECFailure;
                break;
            default:
                break;
        }
        if (cn) {
            *currentOutput = cn;
            nickNames->totallen += PORT_Strlen(cn);
            ++currentOutput;
            --numberOfGeneralNames;
        }
        currentInput = CERT_GetNextGeneralName(currentInput);
    } while (currentInput != firstName);

    return (numberOfGeneralNames == 0) ? SECSuccess : SECFailure;
}

/*
 * Collect all valid DNS names from the given cert.
 * The output arena will reference some temporaray data,
 * but this saves us from dealing with two arenas.
 * The caller may free all data by freeing CERTCertNicknames->arena.
 */
CERTCertNicknames *
CERT_GetValidDNSPatternsFromCert(CERTCertificate *cert)
{
    CERTGeneralName *generalNames;
    CERTCertNicknames *nickNames;
    PLArenaPool *arena;
    char *singleName;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (!arena) {
        return NULL;
    }

    nickNames = PORT_ArenaAlloc(arena, sizeof(CERTCertNicknames));
    if (!nickNames) {
        PORT_FreeArena(arena, PR_FALSE);
        return NULL;
    }

    /* init the structure */
    nickNames->arena = arena;
    nickNames->head = NULL;
    nickNames->numnicknames = 0;
    nickNames->nicknames = NULL;
    nickNames->totallen = 0;

    generalNames = cert_GetSubjectAltNameList(cert, arena);
    if (generalNames) {
        SECStatus rv_getnames = SECFailure;
        PRUint32 numNames = cert_CountDNSPatterns(generalNames);

        if (numNames) {
            rv_getnames = cert_GetDNSPatternsFromGeneralNames(
                generalNames, numNames, nickNames);
        }

        /* if there were names, we'll exit now, either with success or failure
         */
        if (numNames) {
            if (rv_getnames == SECSuccess) {
                return nickNames;
            }

            /* failure to produce output */
            PORT_FreeArena(arena, PR_FALSE);
            return NULL;
        }
    }

    /* no SAN extension or no names found in extension */
    singleName = CERT_GetCommonName(&cert->subject);
    if (singleName) {
        nickNames->numnicknames = 1;
        nickNames->nicknames = PORT_ArenaAlloc(arena, sizeof(char *));
        if (nickNames->nicknames) {
            *nickNames->nicknames = PORT_ArenaStrdup(arena, singleName);
        }
        PORT_Free(singleName);

        /* Did we allocate both the buffer of pointers and the string? */
        if (nickNames->nicknames && *nickNames->nicknames) {
            return nickNames;
        }
    }

    PORT_FreeArena(arena, PR_FALSE);
    return NULL;
}

/* Make sure that the name of the host we are connecting to matches the
 * name that is incoded in the common-name component of the certificate
 * that they are using.
 */
SECStatus
CERT_VerifyCertName(const CERTCertificate *cert, const char *hn)
{
    char *cn;
    SECStatus rv;
    CERTOKDomainName *domainOK;

    if (!hn || !strlen(hn)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    /* if the name is one that the user has already approved, it's OK. */
    for (domainOK = cert->domainOK; domainOK; domainOK = domainOK->next) {
        if (0 == PORT_Strcasecmp(hn, domainOK->name)) {
            return SECSuccess;
        }
    }

    /* Per RFC 2818, if the SubjectAltName extension is present, it must
    ** be used as the cert's identity.
    */
    rv = cert_VerifySubjectAltName(cert, hn);
    if (rv == SECSuccess || PORT_GetError() != SEC_ERROR_EXTENSION_NOT_FOUND)
        return rv;

    cn = CERT_GetCommonName(&cert->subject);
    if (cn) {
        PRBool isIPaddr = cert_IsIPAddr(hn);
        if (isIPaddr) {
            if (PORT_Strcasecmp(hn, cn) == 0) {
                rv = SECSuccess;
            } else {
                PORT_SetError(SSL_ERROR_BAD_CERT_DOMAIN);
                rv = SECFailure;
            }
        } else {
            rv = cert_TestHostName(cn, hn);
        }
        PORT_Free(cn);
    } else
        PORT_SetError(SSL_ERROR_BAD_CERT_DOMAIN);
    return rv;
}

PRBool
CERT_CompareCerts(const CERTCertificate *c1, const CERTCertificate *c2)
{
    SECComparison comp;

    comp = SECITEM_CompareItem(&c1->derCert, &c2->derCert);
    if (comp == SECEqual) { /* certs are the same */
        return (PR_TRUE);
    } else {
        return (PR_FALSE);
    }
}

/* CERT_CertChainFromCert and CERT_DestroyCertificateList moved
   to certhigh.c */

CERTIssuerAndSN *
CERT_GetCertIssuerAndSN(PLArenaPool *arena, CERTCertificate *cert)
{
    CERTIssuerAndSN *result;
    SECStatus rv;

    if (arena == NULL) {
        arena = cert->arena;
    }

    result = (CERTIssuerAndSN *)PORT_ArenaZAlloc(arena, sizeof(*result));
    if (result == NULL) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return NULL;
    }

    rv = SECITEM_CopyItem(arena, &result->derIssuer, &cert->derIssuer);
    if (rv != SECSuccess)
        return NULL;

    rv = CERT_CopyName(arena, &result->issuer, &cert->issuer);
    if (rv != SECSuccess)
        return NULL;

    rv = SECITEM_CopyItem(arena, &result->serialNumber, &cert->serialNumber);
    if (rv != SECSuccess)
        return NULL;

    return result;
}

char *
CERT_MakeCANickname(CERTCertificate *cert)
{
    char *firstname = NULL;
    char *org = NULL;
    char *nickname = NULL;
    int count;
    CERTCertificate *dummycert;

    firstname = CERT_GetCommonName(&cert->subject);
    if (firstname == NULL) {
        firstname = CERT_GetOrgUnitName(&cert->subject);
    }

    org = CERT_GetOrgName(&cert->issuer);
    if (org == NULL) {
        org = CERT_GetDomainComponentName(&cert->issuer);
        if (org == NULL) {
            if (firstname) {
                org = firstname;
                firstname = NULL;
            } else {
                org = PORT_Strdup("Unknown CA");
            }
        }
    }

    /* can only fail if PORT_Strdup fails, in which case
     * we're having memory problems. */
    if (org == NULL) {
        goto done;
    }

    count = 1;
    while (1) {

        if (firstname) {
            if (count == 1) {
                nickname = PR_smprintf("%s - %s", firstname, org);
            } else {
                nickname = PR_smprintf("%s - %s #%d", firstname, org, count);
            }
        } else {
            if (count == 1) {
                nickname = PR_smprintf("%s", org);
            } else {
                nickname = PR_smprintf("%s #%d", org, count);
            }
        }
        if (nickname == NULL) {
            goto done;
        }

        /* look up the nickname to make sure it isn't in use already */
        dummycert = CERT_FindCertByNickname(cert->dbhandle, nickname);

        if (dummycert == NULL) {
            goto done;
        }

        /* found a cert, destroy it and loop */
        CERT_DestroyCertificate(dummycert);

        /* free the nickname */
        PORT_Free(nickname);

        count++;
    }

done:
    if (firstname) {
        PORT_Free(firstname);
    }
    if (org) {
        PORT_Free(org);
    }

    return (nickname);
}

/* CERT_Import_CAChain moved to certhigh.c */

void
CERT_DestroyCrl(CERTSignedCrl *crl)
{
    SEC_DestroyCrl(crl);
}

static int
cert_Version(CERTCertificate *cert)
{
    int version = 0;
    if (cert && cert->version.data && cert->version.len) {
        version = DER_GetInteger(&cert->version);
        if (version < 0)
            version = 0;
    }
    return version;
}

static unsigned int
cert_ComputeTrustOverrides(CERTCertificate *cert, unsigned int cType)
{
    CERTCertTrust trust;
    SECStatus rv = SECFailure;

    rv = CERT_GetCertTrust(cert, &trust);

    if (rv == SECSuccess &&
        (trust.sslFlags | trust.emailFlags | trust.objectSigningFlags)) {

        if (trust.sslFlags & (CERTDB_TERMINAL_RECORD | CERTDB_TRUSTED))
            cType |= NS_CERT_TYPE_SSL_SERVER | NS_CERT_TYPE_SSL_CLIENT;
        if (trust.sslFlags & (CERTDB_VALID_CA | CERTDB_TRUSTED_CA))
            cType |= NS_CERT_TYPE_SSL_CA;
#if defined(CERTDB_NOT_TRUSTED)
        if (trust.sslFlags & CERTDB_NOT_TRUSTED)
            cType &= ~(NS_CERT_TYPE_SSL_SERVER | NS_CERT_TYPE_SSL_CLIENT |
                       NS_CERT_TYPE_SSL_CA);
#endif
        if (trust.emailFlags & (CERTDB_TERMINAL_RECORD | CERTDB_TRUSTED))
            cType |= NS_CERT_TYPE_EMAIL;
        if (trust.emailFlags & (CERTDB_VALID_CA | CERTDB_TRUSTED_CA))
            cType |= NS_CERT_TYPE_EMAIL_CA;
#if defined(CERTDB_NOT_TRUSTED)
        if (trust.emailFlags & CERTDB_NOT_TRUSTED)
            cType &= ~(NS_CERT_TYPE_EMAIL | NS_CERT_TYPE_EMAIL_CA);
#endif
        if (trust.objectSigningFlags &
            (CERTDB_TERMINAL_RECORD | CERTDB_TRUSTED))
            cType |= NS_CERT_TYPE_OBJECT_SIGNING;
        if (trust.objectSigningFlags & (CERTDB_VALID_CA | CERTDB_TRUSTED_CA))
            cType |= NS_CERT_TYPE_OBJECT_SIGNING_CA;
#if defined(CERTDB_NOT_TRUSTED)
        if (trust.objectSigningFlags & CERTDB_NOT_TRUSTED)
            cType &=
                ~(NS_CERT_TYPE_OBJECT_SIGNING | NS_CERT_TYPE_OBJECT_SIGNING_CA);
#endif
    }
    return cType;
}

/*
 * Does a cert belong to a CA?  We decide based on perm database trust
 * flags, Netscape Cert Type Extension, and KeyUsage Extension.
 */
PRBool
CERT_IsCACert(CERTCertificate *cert, unsigned int *rettype)
{
    unsigned int cType = cert->nsCertType;
    PRBool ret = PR_FALSE;

    /*
     * Check if the constraints are available and it's a CA, OR if it's
     * a X.509 v1 Root CA.
     */
    CERTBasicConstraints constraints;
    if ((CERT_FindBasicConstraintExten(cert, &constraints) == SECSuccess &&
         constraints.isCA) ||
        (cert->isRoot && cert_Version(cert) < SEC_CERTIFICATE_VERSION_3))
        cType |= (NS_CERT_TYPE_SSL_CA | NS_CERT_TYPE_EMAIL_CA);

    /*
     * Apply trust overrides, if any.
     */
    cType = cert_ComputeTrustOverrides(cert, cType);
    ret = (cType & (NS_CERT_TYPE_SSL_CA | NS_CERT_TYPE_EMAIL_CA |
                    NS_CERT_TYPE_OBJECT_SIGNING_CA))
              ? PR_TRUE
              : PR_FALSE;

    if (rettype) {
        *rettype = cType;
    }

    return ret;
}

PRBool
CERT_IsCADERCert(SECItem *derCert, unsigned int *type)
{
    CERTCertificate *cert;
    PRBool isCA;

    /* This is okay -- only looks at extensions */
    cert = CERT_DecodeDERCertificate(derCert, PR_FALSE, NULL);
    if (cert == NULL)
        return PR_FALSE;

    isCA = CERT_IsCACert(cert, type);
    CERT_DestroyCertificate(cert);
    return isCA;
}

PRBool
CERT_IsRootDERCert(SECItem *derCert)
{
    CERTCertificate *cert;
    PRBool isRoot;

    /* This is okay -- only looks at extensions */
    cert = CERT_DecodeDERCertificate(derCert, PR_FALSE, NULL);
    if (cert == NULL)
        return PR_FALSE;

    isRoot = cert->isRoot;
    CERT_DestroyCertificate(cert);
    return isRoot;
}

CERTCompareValidityStatus
CERT_CompareValidityTimes(CERTValidity *val_a, CERTValidity *val_b)
{
    PRTime notBeforeA, notBeforeB, notAfterA, notAfterB;

    if (!val_a || !val_b) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return certValidityUndetermined;
    }

    if (SECSuccess != DER_DecodeTimeChoice(&notBeforeA, &val_a->notBefore) ||
        SECSuccess != DER_DecodeTimeChoice(&notBeforeB, &val_b->notBefore) ||
        SECSuccess != DER_DecodeTimeChoice(&notAfterA, &val_a->notAfter) ||
        SECSuccess != DER_DecodeTimeChoice(&notAfterB, &val_b->notAfter)) {
        return certValidityUndetermined;
    }

    /* sanity check */
    if (LL_CMP(notBeforeA, >, notAfterA) || LL_CMP(notBeforeB, >, notAfterB)) {
        PORT_SetError(SEC_ERROR_INVALID_TIME);
        return certValidityUndetermined;
    }

    if (LL_CMP(notAfterA, !=, notAfterB)) {
        /* one cert validity goes farther into the future, select it */
        return LL_CMP(notAfterA, <, notAfterB) ? certValidityChooseB
                                               : certValidityChooseA;
    }
    /* the two certs have the same expiration date */
    PORT_Assert(LL_CMP(notAfterA, ==, notAfterB));
    /* do they also have the same start date ? */
    if (LL_CMP(notBeforeA, ==, notBeforeB)) {
        return certValidityEqual;
    }
    /* choose cert with the later start date */
    return LL_CMP(notBeforeA, <, notBeforeB) ? certValidityChooseB
                                             : certValidityChooseA;
}

/*
 * is certa newer than certb?  If one is expired, pick the other one.
 */
PRBool
CERT_IsNewer(CERTCertificate *certa, CERTCertificate *certb)
{
    PRTime notBeforeA, notAfterA, notBeforeB, notAfterB, now;
    SECStatus rv;
    PRBool newerbefore, newerafter;

    rv = CERT_GetCertTimes(certa, &notBeforeA, &notAfterA);
    if (rv != SECSuccess) {
        return (PR_FALSE);
    }

    rv = CERT_GetCertTimes(certb, &notBeforeB, &notAfterB);
    if (rv != SECSuccess) {
        return (PR_TRUE);
    }

    newerbefore = PR_FALSE;
    if (LL_CMP(notBeforeA, >, notBeforeB)) {
        newerbefore = PR_TRUE;
    }

    newerafter = PR_FALSE;
    if (LL_CMP(notAfterA, >, notAfterB)) {
        newerafter = PR_TRUE;
    }

    if (newerbefore && newerafter) {
        return (PR_TRUE);
    }

    if ((!newerbefore) && (!newerafter)) {
        return (PR_FALSE);
    }

    /* get current time */
    now = PR_Now();

    if (newerbefore) {
        /* cert A was issued after cert B, but expires sooner */
        /* if A is expired, then pick B */
        if (LL_CMP(notAfterA, <, now)) {
            return (PR_FALSE);
        }
        return (PR_TRUE);
    } else {
        /* cert B was issued after cert A, but expires sooner */
        /* if B is expired, then pick A */
        if (LL_CMP(notAfterB, <, now)) {
            return (PR_TRUE);
        }
        return (PR_FALSE);
    }
}

void
CERT_DestroyCertArray(CERTCertificate **certs, unsigned int ncerts)
{
    unsigned int i;

    if (certs) {
        for (i = 0; i < ncerts; i++) {
            if (certs[i]) {
                CERT_DestroyCertificate(certs[i]);
            }
        }

        PORT_Free(certs);
    }

    return;
}

char *
CERT_FixupEmailAddr(const char *emailAddr)
{
    char *retaddr;
    char *str;

    if (emailAddr == NULL) {
        return (NULL);
    }

    /* copy the string */
    str = retaddr = PORT_Strdup(emailAddr);
    if (str == NULL) {
        return (NULL);
    }

    /* make it lower case */
    while (*str) {
        *str = tolower((unsigned char)*str);
        str++;
    }

    return (retaddr);
}

/*
 * NOTE - don't allow encode of govt-approved or invisible bits
 */
SECStatus
CERT_DecodeTrustString(CERTCertTrust *trust, const char *trusts)
{
    unsigned int i;
    unsigned int *pflags;

    if (!trust) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    trust->sslFlags = 0;
    trust->emailFlags = 0;
    trust->objectSigningFlags = 0;
    if (!trusts) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    pflags = &trust->sslFlags;

    for (i = 0; i < PORT_Strlen(trusts); i++) {
        switch (trusts[i]) {
            case 'p':
                *pflags = *pflags | CERTDB_TERMINAL_RECORD;
                break;

            case 'P':
                *pflags = *pflags | CERTDB_TRUSTED | CERTDB_TERMINAL_RECORD;
                break;

            case 'w':
                *pflags = *pflags | CERTDB_SEND_WARN;
                break;

            case 'c':
                *pflags = *pflags | CERTDB_VALID_CA;
                break;

            case 'T':
                *pflags = *pflags | CERTDB_TRUSTED_CLIENT_CA | CERTDB_VALID_CA;
                break;

            case 'C':
                *pflags = *pflags | CERTDB_TRUSTED_CA | CERTDB_VALID_CA;
                break;

            case 'u':
                *pflags = *pflags | CERTDB_USER;
                break;

            case 'i':
                *pflags = *pflags | CERTDB_INVISIBLE_CA;
                break;
            case 'g':
                *pflags = *pflags | CERTDB_GOVT_APPROVED_CA;
                break;

            case ',':
                if (pflags == &trust->sslFlags) {
                    pflags = &trust->emailFlags;
                } else {
                    pflags = &trust->objectSigningFlags;
                }
                break;
            default:
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                return SECFailure;
        }
    }

    return SECSuccess;
}

SECStatus
CERT_ImportCerts(CERTCertDBHandle *certdb, SECCertUsage usage,
                 unsigned int ncerts, SECItem **derCerts,
                 CERTCertificate ***retCerts, PRBool keepCerts, PRBool caOnly,
                 char *nickname)
{
    unsigned int i;
    CERTCertificate **certs = NULL;
    unsigned int fcerts = 0;

    if (ncerts) {
        certs = PORT_ZNewArray(CERTCertificate *, ncerts);
        if (certs == NULL) {
            return (SECFailure);
        }

        /* decode all of the certs into the temporary DB */
        for (i = 0, fcerts = 0; i < ncerts; i++) {
            certs[fcerts] = CERT_NewTempCertificate(certdb, derCerts[i], NULL,
                                                    PR_FALSE, PR_TRUE);
            if (certs[fcerts]) {
                SECItem subjKeyID = { siBuffer, NULL, 0 };
                if (CERT_FindSubjectKeyIDExtension(certs[fcerts], &subjKeyID) ==
                    SECSuccess) {
                    if (subjKeyID.data) {
                        cert_AddSubjectKeyIDMapping(&subjKeyID, certs[fcerts]);
                    }
                    SECITEM_FreeItem(&subjKeyID, PR_FALSE);
                }
                fcerts++;
            }
        }

        if (keepCerts) {
            for (i = 0; i < fcerts; i++) {
                char *canickname = NULL;
                PRBool isCA;

                SECKEY_UpdateCertPQG(certs[i]);

                isCA = CERT_IsCACert(certs[i], NULL);
                if (isCA) {
                    canickname = CERT_MakeCANickname(certs[i]);
                }

                if (isCA && (fcerts > 1)) {
                    /* if we are importing only a single cert and specifying
                     * a nickname, we want to use that nickname if it a CA,
                     * otherwise if there are more than one cert, we don't
                     * know which cert it belongs to. But we still may try
                     * the individual canickname from the cert itself.
                     */
                    /* Bug 1192442 - propagate errors from these calls. */
                    (void)CERT_AddTempCertToPerm(certs[i], canickname, NULL);
                } else {
                    (void)CERT_AddTempCertToPerm(
                        certs[i], nickname ? nickname : canickname, NULL);
                }

                PORT_Free(canickname);
                /* don't care if it fails - keep going */
            }
        }
    }

    if (retCerts) {
        *retCerts = certs;
    } else {
        if (certs) {
            CERT_DestroyCertArray(certs, fcerts);
        }
    }

    return (fcerts || !ncerts) ? SECSuccess : SECFailure;
}

/*
 * a real list of certificates - need to convert CERTCertificateList
 * stuff and ASN 1 encoder/decoder over to using this...
 */
CERTCertList *
CERT_NewCertList(void)
{
    PLArenaPool *arena = NULL;
    CERTCertList *ret = NULL;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        goto loser;
    }

    ret = (CERTCertList *)PORT_ArenaZAlloc(arena, sizeof(CERTCertList));
    if (ret == NULL) {
        goto loser;
    }

    ret->arena = arena;

    PR_INIT_CLIST(&ret->list);

    return (ret);

loser:
    if (arena != NULL) {
        PORT_FreeArena(arena, PR_FALSE);
    }

    return (NULL);
}

void
CERT_DestroyCertList(CERTCertList *certs)
{
    PRCList *node;

    if (!certs) {
        return;
    }

    while (!PR_CLIST_IS_EMPTY(&certs->list)) {
        node = PR_LIST_HEAD(&certs->list);
        CERT_DestroyCertificate(((CERTCertListNode *)node)->cert);
        PR_REMOVE_LINK(node);
    }

    PORT_FreeArena(certs->arena, PR_FALSE);

    return;
}

void
CERT_RemoveCertListNode(CERTCertListNode *node)
{
    CERT_DestroyCertificate(node->cert);
    PR_REMOVE_LINK(&node->links);
    return;
}

SECStatus
CERT_AddCertToListTailWithData(CERTCertList *certs, CERTCertificate *cert,
                               void *appData)
{
    CERTCertListNode *node;

    node = (CERTCertListNode *)PORT_ArenaZAlloc(certs->arena,
                                                sizeof(CERTCertListNode));
    if (node == NULL) {
        goto loser;
    }

    PR_INSERT_BEFORE(&node->links, &certs->list);
    /* certs->count++; */
    node->cert = cert;
    node->appData = appData;
    return (SECSuccess);

loser:
    return (SECFailure);
}

SECStatus
CERT_AddCertToListTail(CERTCertList *certs, CERTCertificate *cert)
{
    return CERT_AddCertToListTailWithData(certs, cert, NULL);
}

SECStatus
CERT_AddCertToListHeadWithData(CERTCertList *certs, CERTCertificate *cert,
                               void *appData)
{
    CERTCertListNode *node;
    CERTCertListNode *head;

    head = CERT_LIST_HEAD(certs);
    if (head == NULL) {
        goto loser;
    }

    node = (CERTCertListNode *)PORT_ArenaZAlloc(certs->arena,
                                                sizeof(CERTCertListNode));
    if (node == NULL) {
        goto loser;
    }

    PR_INSERT_BEFORE(&node->links, &head->links);
    /* certs->count++; */
    node->cert = cert;
    node->appData = appData;
    return (SECSuccess);

loser:
    return (SECFailure);
}

SECStatus
CERT_AddCertToListHead(CERTCertList *certs, CERTCertificate *cert)
{
    return CERT_AddCertToListHeadWithData(certs, cert, NULL);
}

/*
 * Sort callback function to determine if cert a is newer than cert b.
 * Not valid certs are considered older than valid certs.
 */
PRBool
CERT_SortCBValidity(CERTCertificate *certa, CERTCertificate *certb, void *arg)
{
    PRTime sorttime;
    PRTime notBeforeA, notAfterA, notBeforeB, notAfterB;
    SECStatus rv;
    PRBool newerbefore, newerafter;
    PRBool aNotValid = PR_FALSE, bNotValid = PR_FALSE;

    sorttime = *(PRTime *)arg;

    rv = CERT_GetCertTimes(certa, &notBeforeA, &notAfterA);
    if (rv != SECSuccess) {
        return (PR_FALSE);
    }

    rv = CERT_GetCertTimes(certb, &notBeforeB, &notAfterB);
    if (rv != SECSuccess) {
        return (PR_TRUE);
    }
    newerbefore = PR_FALSE;
    if (LL_CMP(notBeforeA, >, notBeforeB)) {
        newerbefore = PR_TRUE;
    }
    newerafter = PR_FALSE;
    if (LL_CMP(notAfterA, >, notAfterB)) {
        newerafter = PR_TRUE;
    }

    /* check if A is valid at sorttime */
    if (CERT_CheckCertValidTimes(certa, sorttime, PR_FALSE) !=
        secCertTimeValid) {
        aNotValid = PR_TRUE;
    }

    /* check if B is valid at sorttime */
    if (CERT_CheckCertValidTimes(certb, sorttime, PR_FALSE) !=
        secCertTimeValid) {
        bNotValid = PR_TRUE;
    }

    /* a is valid, b is not */
    if (bNotValid && (!aNotValid)) {
        return (PR_TRUE);
    }

    /* b is valid, a is not */
    if (aNotValid && (!bNotValid)) {
        return (PR_FALSE);
    }

    /* a and b are either valid or not valid */
    if (newerbefore && newerafter) {
        return (PR_TRUE);
    }

    if ((!newerbefore) && (!newerafter)) {
        return (PR_FALSE);
    }

    if (newerbefore) {
        /* cert A was issued after cert B, but expires sooner */
        return (PR_TRUE);
    } else {
        /* cert B was issued after cert A, but expires sooner */
        return (PR_FALSE);
    }
}

SECStatus
CERT_AddCertToListSorted(CERTCertList *certs, CERTCertificate *cert,
                         CERTSortCallback f, void *arg)
{
    CERTCertListNode *node;
    CERTCertListNode *head;
    PRBool ret;

    node = (CERTCertListNode *)PORT_ArenaZAlloc(certs->arena,
                                                sizeof(CERTCertListNode));
    if (node == NULL) {
        goto loser;
    }

    head = CERT_LIST_HEAD(certs);

    while (!CERT_LIST_END(head, certs)) {

        /* if cert is already in the list, then don't add it again */
        if (cert == head->cert) {
            /*XXX*/
            /* don't keep a reference */
            CERT_DestroyCertificate(cert);
            goto done;
        }

        ret = (*f)(cert, head->cert, arg);
        /* if sort function succeeds, then insert before current node */
        if (ret) {
            PR_INSERT_BEFORE(&node->links, &head->links);
            goto done;
        }

        head = CERT_LIST_NEXT(head);
    }
    /* if we get to the end, then just insert it at the tail */
    PR_INSERT_BEFORE(&node->links, &certs->list);

done:
    /* certs->count++; */
    node->cert = cert;
    return (SECSuccess);

loser:
    return (SECFailure);
}

/* This routine is here because pcertdb.c still has a call to it.
 * The SMIME profile code in pcertdb.c should be split into high (find
 * the email cert) and low (store the profile) code.  At that point, we
 * can move this to certhigh.c where it belongs.
 *
 * remove certs from a list that don't have keyUsage and certType
 * that match the given usage.
 */
SECStatus
CERT_FilterCertListByUsage(CERTCertList *certList, SECCertUsage usage,
                           PRBool ca)
{
    unsigned int requiredKeyUsage;
    unsigned int requiredCertType;
    CERTCertListNode *node, *savenode;
    SECStatus rv;

    if (certList == NULL)
        goto loser;

    rv = CERT_KeyUsageAndTypeForCertUsage(usage, ca, &requiredKeyUsage,
                                          &requiredCertType);
    if (rv != SECSuccess) {
        goto loser;
    }

    node = CERT_LIST_HEAD(certList);

    while (!CERT_LIST_END(node, certList)) {

        PRBool bad = (PRBool)(!node->cert);

        /* bad key usage ? */
        if (!bad &&
            CERT_CheckKeyUsage(node->cert, requiredKeyUsage) != SECSuccess) {
            bad = PR_TRUE;
        }
        /* bad cert type ? */
        if (!bad) {
            unsigned int certType = 0;
            if (ca) {
                /* This function returns a more comprehensive cert type that
                 * takes trust flags into consideration.  Should probably
                 * fix the cert decoding code to do this.
                 */
                (void)CERT_IsCACert(node->cert, &certType);
            } else {
                certType = node->cert->nsCertType;
            }
            if (!(certType & requiredCertType)) {
                bad = PR_TRUE;
            }
        }

        if (bad) {
            /* remove the node if it is bad */
            savenode = CERT_LIST_NEXT(node);
            CERT_RemoveCertListNode(node);
            node = savenode;
        } else {
            node = CERT_LIST_NEXT(node);
        }
    }
    return (SECSuccess);

loser:
    return (SECFailure);
}

PRBool
CERT_IsUserCert(CERTCertificate *cert)
{
    CERTCertTrust trust;
    SECStatus rv = SECFailure;

    rv = CERT_GetCertTrust(cert, &trust);
    if (rv == SECSuccess &&
        ((trust.sslFlags & CERTDB_USER) || (trust.emailFlags & CERTDB_USER) ||
         (trust.objectSigningFlags & CERTDB_USER))) {
        return PR_TRUE;
    } else {
        return PR_FALSE;
    }
}

SECStatus
CERT_FilterCertListForUserCerts(CERTCertList *certList)
{
    CERTCertListNode *node, *freenode;
    CERTCertificate *cert;

    if (!certList) {
        return SECFailure;
    }

    node = CERT_LIST_HEAD(certList);

    while (!CERT_LIST_END(node, certList)) {
        cert = node->cert;
        if (PR_TRUE != CERT_IsUserCert(cert)) {
            /* Not a User Cert, so remove this cert from the list */
            freenode = node;
            node = CERT_LIST_NEXT(node);
            CERT_RemoveCertListNode(freenode);
        } else {
            /* Is a User cert, so leave it in the list */
            node = CERT_LIST_NEXT(node);
        }
    }

    return (SECSuccess);
}

/* return true if cert is in the list */
PRBool
CERT_IsInList(const CERTCertificate *cert, const CERTCertList *certList)
{
    CERTCertListNode *node;
    for (node = CERT_LIST_HEAD(certList); !CERT_LIST_END(node, certList);
         node = CERT_LIST_NEXT(node)) {
        if (node->cert == cert) {
            return PR_TRUE;
        }
    }
    return PR_FALSE;
}

/* returned certList is the intersection of the certs on certList and the
 * certs on filterList */
SECStatus
CERT_FilterCertListByCertList(CERTCertList *certList,
                              const CERTCertList *filterList)
{
    CERTCertListNode *node, *freenode;
    CERTCertificate *cert;

    if (!certList) {
        return SECFailure;
    }

    if (!filterList || CERT_LIST_EMPTY(certList)) {
        /* if the filterList is empty, just clear out certList and return */
        for (node = CERT_LIST_HEAD(certList); !CERT_LIST_END(node, certList);) {
            freenode = node;
            node = CERT_LIST_NEXT(node);
            CERT_RemoveCertListNode(freenode);
        }
        return SECSuccess;
    }

    node = CERT_LIST_HEAD(certList);

    while (!CERT_LIST_END(node, certList)) {
        cert = node->cert;
        if (!CERT_IsInList(cert, filterList)) {
            // no matching cert on filter list, remove it from certlist */
            freenode = node;
            node = CERT_LIST_NEXT(node);
            CERT_RemoveCertListNode(freenode);
        } else {
            /* matching cert, keep it around */
            node = CERT_LIST_NEXT(node);
        }
    }

    return (SECSuccess);
}

SECStatus
CERT_FilterCertListByNickname(CERTCertList *certList, char *nickname,
                              void *pwarg)
{
    CERTCertList *nameList;
    SECStatus rv;

    if (!certList) {
        return SECFailure;
    }

    /* we could try to match the nickname to the individual cert,
     * but nickname parsing is quite complicated, so it's best just
     * to use the existing code and get a list of certs that match the
     * nickname. We can then compare that list with our input cert list
     * and return only those certs that are on both. */
    nameList = PK11_FindCertsFromNickname(nickname, pwarg);

    /* namelist could be NULL, this will force certList to become empty */
    rv = CERT_FilterCertListByCertList(certList, nameList);
    /* CERT_DestroyCertList can now accept a NULL pointer */
    CERT_DestroyCertList(nameList);
    return rv;
}

static PRLock *certTrustLock = NULL;

/*
 * Acquire the cert trust lock
 * There is currently one global lock for all certs, but I'm putting a cert
 * arg here so that it will be easy to make it per-cert in the future if
 * that turns out to be necessary.
 */
void
CERT_LockCertTrust(const CERTCertificate *cert)
{
    PORT_Assert(certTrustLock != NULL);
    PR_Lock(certTrustLock);
}

static PRLock *certTempPermCertLock = NULL;

/*
 * Acquire the cert temp/perm/nssCert lock
 */
void
CERT_LockCertTempPerm(const CERTCertificate *cert)
{
    PORT_Assert(certTempPermCertLock != NULL);
    PR_Lock(certTempPermCertLock);
}

/* Maybe[Lock, Unlock] variants are only to be used by
 * CERT_DestroyCertificate, since an application could
 * call this after NSS_Shutdown destroys cert locks. */
void
CERT_MaybeLockCertTempPerm(const CERTCertificate *cert)
{
    if (certTempPermCertLock) {
        PR_Lock(certTempPermCertLock);
    }
}

SECStatus
cert_InitLocks(void)
{
    if (certTrustLock == NULL) {
        certTrustLock = PR_NewLock();
        PORT_Assert(certTrustLock != NULL);
        if (!certTrustLock) {
            return SECFailure;
        }
    }

    if (certTempPermCertLock == NULL) {
        certTempPermCertLock = PR_NewLock();
        PORT_Assert(certTempPermCertLock != NULL);
        if (!certTempPermCertLock) {
            PR_DestroyLock(certTrustLock);
            certTrustLock = NULL;
            return SECFailure;
        }
    }

    return SECSuccess;
}

SECStatus
cert_DestroyLocks(void)
{
    SECStatus rv = SECSuccess;

    PORT_Assert(certTrustLock != NULL);
    if (certTrustLock) {
        PR_DestroyLock(certTrustLock);
        certTrustLock = NULL;
    } else {
        rv = SECFailure;
    }

    PORT_Assert(certTempPermCertLock != NULL);
    if (certTempPermCertLock) {
        PR_DestroyLock(certTempPermCertLock);
        certTempPermCertLock = NULL;
    } else {
        rv = SECFailure;
    }
    return rv;
}

/*
 * Free the cert trust lock
 */
void
CERT_UnlockCertTrust(const CERTCertificate *cert)
{
    PORT_Assert(certTrustLock != NULL);
    PRStatus prstat = PR_Unlock(certTrustLock);
    PORT_AssertArg(prstat == PR_SUCCESS);
}

/*
 * Free the temp/perm/nssCert lock
 */
void
CERT_UnlockCertTempPerm(const CERTCertificate *cert)
{
    PORT_Assert(certTempPermCertLock != NULL);
    PRStatus prstat = PR_Unlock(certTempPermCertLock);
    PORT_AssertArg(prstat == PR_SUCCESS);
}

void
CERT_MaybeUnlockCertTempPerm(const CERTCertificate *cert)
{
    if (certTempPermCertLock) {
        PR_Unlock(certTempPermCertLock);
    }
}

/*
 * Get the StatusConfig data for this handle
 */
CERTStatusConfig *
CERT_GetStatusConfig(CERTCertDBHandle *handle)
{
    return handle->statusConfig;
}

/*
 * Set the StatusConfig data for this handle.  There
 * should not be another configuration set.
 */
void
CERT_SetStatusConfig(CERTCertDBHandle *handle, CERTStatusConfig *statusConfig)
{
    PORT_Assert(handle->statusConfig == NULL);
    handle->statusConfig = statusConfig;
}

/*
 * Code for dealing with subjKeyID to cert mappings.
 */

static PLHashTable *gSubjKeyIDHash = NULL;
static PRLock *gSubjKeyIDLock = NULL;
static PLHashTable *gSubjKeyIDSlotCheckHash = NULL;
static PRLock *gSubjKeyIDSlotCheckLock = NULL;

static void *
cert_AllocTable(void *pool, PRSize size)
{
    return PORT_Alloc(size);
}

static void
cert_FreeTable(void *pool, void *item)
{
    PORT_Free(item);
}

static PLHashEntry *
cert_AllocEntry(void *pool, const void *key)
{
    return PORT_New(PLHashEntry);
}

static void
cert_FreeEntry(void *pool, PLHashEntry *he, PRUintn flag)
{
    SECITEM_FreeItem((SECItem *)(he->value), PR_TRUE);
    if (flag == HT_FREE_ENTRY) {
        SECITEM_FreeItem((SECItem *)(he->key), PR_TRUE);
        PORT_Free(he);
    }
}

static PLHashAllocOps cert_AllocOps = { cert_AllocTable, cert_FreeTable,
                                        cert_AllocEntry, cert_FreeEntry };

SECStatus
cert_CreateSubjectKeyIDSlotCheckHash(void)
{
    /*
     * This hash is used to remember the series of a slot
     * when we last checked for user certs
     */
    gSubjKeyIDSlotCheckHash =
        PL_NewHashTable(0, SECITEM_Hash, SECITEM_HashCompare,
                        SECITEM_HashCompare, &cert_AllocOps, NULL);
    if (!gSubjKeyIDSlotCheckHash) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return SECFailure;
    }
    gSubjKeyIDSlotCheckLock = PR_NewLock();
    if (!gSubjKeyIDSlotCheckLock) {
        PL_HashTableDestroy(gSubjKeyIDSlotCheckHash);
        gSubjKeyIDSlotCheckHash = NULL;
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return SECFailure;
    }
    return SECSuccess;
}

SECStatus
cert_CreateSubjectKeyIDHashTable(void)
{
    gSubjKeyIDHash = PL_NewHashTable(0, SECITEM_Hash, SECITEM_HashCompare,
                                     SECITEM_HashCompare, &cert_AllocOps, NULL);
    if (!gSubjKeyIDHash) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return SECFailure;
    }
    gSubjKeyIDLock = PR_NewLock();
    if (!gSubjKeyIDLock) {
        PL_HashTableDestroy(gSubjKeyIDHash);
        gSubjKeyIDHash = NULL;
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return SECFailure;
    }
    /* initialize the companion hash (for remembering slot series) */
    if (cert_CreateSubjectKeyIDSlotCheckHash() != SECSuccess) {
        cert_DestroySubjectKeyIDHashTable();
        return SECFailure;
    }
    return SECSuccess;
}

SECStatus
cert_AddSubjectKeyIDMapping(SECItem *subjKeyID, CERTCertificate *cert)
{
    SECItem *newKeyID, *oldVal, *newVal;
    SECStatus rv = SECFailure;

    if (!gSubjKeyIDLock) {
        /* If one is created, then both are there.  So only check for one. */
        return SECFailure;
    }

    newVal = SECITEM_DupItem(&cert->derCert);
    if (!newVal) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        goto done;
    }
    newKeyID = SECITEM_DupItem(subjKeyID);
    if (!newKeyID) {
        SECITEM_FreeItem(newVal, PR_TRUE);
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        goto done;
    }

    PR_Lock(gSubjKeyIDLock);
    /* The hash table implementation does not free up the memory
     * associated with the key of an already existing entry if we add a
     * duplicate, so we would wind up leaking the previously allocated
     * key if we don't remove before adding.
     */
    oldVal = (SECItem *)PL_HashTableLookup(gSubjKeyIDHash, subjKeyID);
    if (oldVal) {
        PL_HashTableRemove(gSubjKeyIDHash, subjKeyID);
    }

    rv = (PL_HashTableAdd(gSubjKeyIDHash, newKeyID, newVal)) ? SECSuccess
                                                             : SECFailure;
    PR_Unlock(gSubjKeyIDLock);
done:
    return rv;
}

SECStatus
cert_UpdateSubjectKeyIDSlotCheck(SECItem *slotid, int series)
{
    SECItem *oldSeries, *newSlotid, *newSeries;
    SECStatus rv = SECFailure;

    if (!gSubjKeyIDSlotCheckLock) {
        return rv;
    }

    newSlotid = SECITEM_DupItem(slotid);
    newSeries = SECITEM_AllocItem(NULL, NULL, sizeof(int));
    if (!newSlotid || !newSeries) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        goto loser;
    }
    PORT_Memcpy(newSeries->data, &series, sizeof(int));

    PR_Lock(gSubjKeyIDSlotCheckLock);
    oldSeries = (SECItem *)PL_HashTableLookup(gSubjKeyIDSlotCheckHash, slotid);
    if (oldSeries) {
        /*
         * make sure we don't leak the key of an existing entry
         * (similar to cert_AddSubjectKeyIDMapping, see comment there)
         */
        PL_HashTableRemove(gSubjKeyIDSlotCheckHash, slotid);
    }
    rv = (PL_HashTableAdd(gSubjKeyIDSlotCheckHash, newSlotid, newSeries))
             ? SECSuccess
             : SECFailure;
    PR_Unlock(gSubjKeyIDSlotCheckLock);
    if (rv == SECSuccess) {
        return rv;
    }

loser:
    if (newSlotid) {
        SECITEM_FreeItem(newSlotid, PR_TRUE);
    }
    if (newSeries) {
        SECITEM_FreeItem(newSeries, PR_TRUE);
    }
    return rv;
}

int
cert_SubjectKeyIDSlotCheckSeries(SECItem *slotid)
{
    SECItem *seriesItem = NULL;
    int series;

    if (!gSubjKeyIDSlotCheckLock) {
        PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
        return -1;
    }

    PR_Lock(gSubjKeyIDSlotCheckLock);
    seriesItem = (SECItem *)PL_HashTableLookup(gSubjKeyIDSlotCheckHash, slotid);
    PR_Unlock(gSubjKeyIDSlotCheckLock);
    /* getting a null series just means we haven't registered one yet,
     * just return 0 */
    if (seriesItem == NULL) {
        return 0;
    }
    /* if we got a series back, assert if it's not the proper length. */
    PORT_Assert(seriesItem->len == sizeof(int));
    if (seriesItem->len != sizeof(int)) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return -1;
    }
    PORT_Memcpy(&series, seriesItem->data, sizeof(int));
    return series;
}

SECStatus
cert_DestroySubjectKeyIDSlotCheckHash(void)
{
    if (gSubjKeyIDSlotCheckHash) {
        PR_Lock(gSubjKeyIDSlotCheckLock);
        PL_HashTableDestroy(gSubjKeyIDSlotCheckHash);
        gSubjKeyIDSlotCheckHash = NULL;
        PR_Unlock(gSubjKeyIDSlotCheckLock);
        PR_DestroyLock(gSubjKeyIDSlotCheckLock);
        gSubjKeyIDSlotCheckLock = NULL;
    }
    return SECSuccess;
}

SECStatus
cert_DestroySubjectKeyIDHashTable(void)
{
    if (gSubjKeyIDHash) {
        PR_Lock(gSubjKeyIDLock);
        PL_HashTableDestroy(gSubjKeyIDHash);
        gSubjKeyIDHash = NULL;
        PR_Unlock(gSubjKeyIDLock);
        PR_DestroyLock(gSubjKeyIDLock);
        gSubjKeyIDLock = NULL;
    }
    cert_DestroySubjectKeyIDSlotCheckHash();
    return SECSuccess;
}

SECItem *
cert_FindDERCertBySubjectKeyID(SECItem *subjKeyID)
{
    SECItem *val;

    if (!gSubjKeyIDLock)
        return NULL;

    PR_Lock(gSubjKeyIDLock);
    val = (SECItem *)PL_HashTableLookup(gSubjKeyIDHash, subjKeyID);
    if (val) {
        val = SECITEM_DupItem(val);
    }
    PR_Unlock(gSubjKeyIDLock);
    return val;
}

CERTCertificate *
CERT_FindCertBySubjectKeyID(CERTCertDBHandle *handle, SECItem *subjKeyID)
{
    CERTCertificate *cert = NULL;
    SECItem *derCert;

    derCert = cert_FindDERCertBySubjectKeyID(subjKeyID);
    if (derCert) {
        cert = CERT_FindCertByDERCert(handle, derCert);
        SECITEM_FreeItem(derCert, PR_TRUE);
    }
    return cert;
}
