/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
** certutil.c
**
** utility for managing certificates and the cert database
**
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(WIN32)
#include "fcntl.h"
#include "io.h"
#endif

#include "secutil.h"

#if defined(XP_UNIX)
#include <unistd.h>
#endif

#include "nspr.h"
#include "prtypes.h"
#include "prtime.h"
#include "prlong.h"

#include "pk11func.h"
#include "secasn1.h"
#include "cert.h"
#include "cryptohi.h"
#include "secoid.h"
#include "certdb.h"
#include "nss.h"
#include "certutil.h"
#include "basicutil.h"
#include "ssl.h"

#define MIN_KEY_BITS 512
/* MAX_KEY_BITS should agree with RSA_MAX_MODULUS_BITS in freebl */
#define MAX_KEY_BITS 8192
#define DEFAULT_KEY_BITS 2048

#define GEN_BREAK(e) \
    rv = e;          \
    break;

char *progName;

static SECStatus
ChangeCertTrust(CERTCertDBHandle *handle, CERTCertificate *cert,
                CERTCertTrust *trust, PK11SlotInfo *slot, void *pwdata)
{
    SECStatus rv;

    rv = CERT_ChangeCertTrust(handle, cert, trust);
    if (rv != SECSuccess) {
        if (PORT_GetError() == SEC_ERROR_TOKEN_NOT_LOGGED_IN) {
            rv = PK11_Authenticate(slot, PR_TRUE, pwdata);
            if (PORT_GetError() == SEC_ERROR_TOKEN_NOT_LOGGED_IN) {
                PK11SlotInfo *internalslot;
                internalslot = PK11_GetInternalKeySlot();
                rv = PK11_Authenticate(internalslot, PR_TRUE, pwdata);
                if (rv != SECSuccess) {
                    SECU_PrintError(progName,
                                    "could not authenticate to token %s.",
                                    PK11_GetTokenName(internalslot));
                    PK11_FreeSlot(internalslot);
                    return SECFailure;
                }
                PK11_FreeSlot(internalslot);
            }
            rv = CERT_ChangeCertTrust(handle, cert, trust);
        }
    }
    return rv;
}

static CERTCertificateRequest *
GetCertRequest(const SECItem *reqDER, void *pwarg)
{
    CERTCertificateRequest *certReq = NULL;
    CERTSignedData signedData;
    PLArenaPool *arena = NULL;
    SECStatus rv;

    do {
        arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
        if (arena == NULL) {
            GEN_BREAK(SECFailure);
        }

        certReq = (CERTCertificateRequest *)PORT_ArenaZAlloc(arena, sizeof(CERTCertificateRequest));
        if (!certReq) {
            GEN_BREAK(SECFailure);
        }
        certReq->arena = arena;

        /* Since cert request is a signed data, must decode to get the inner
           data
         */
        PORT_Memset(&signedData, 0, sizeof(signedData));
        rv = SEC_ASN1DecodeItem(arena, &signedData,
                                SEC_ASN1_GET(CERT_SignedDataTemplate), reqDER);
        if (rv) {
            break;
        }
        rv = SEC_ASN1DecodeItem(arena, certReq,
                                SEC_ASN1_GET(CERT_CertificateRequestTemplate), &signedData.data);
        if (rv) {
            break;
        }
        rv = CERT_VerifySignedDataWithPublicKeyInfo(&signedData,
                                                    &certReq->subjectPublicKeyInfo, pwarg);
    } while (0);

    if (rv) {
        SECU_PrintError(progName, "bad certificate request\n");
        if (arena) {
            PORT_FreeArena(arena, PR_FALSE);
        }
        certReq = NULL;
    }

    return certReq;
}

/* Build an unsigned certificate request for a bare ML-KEM
 * SubjectPublicKeyInfo. ML-KEM keys cannot sign a request, so the caller is
 * responsible for establishing possession of the private key. */
static CERTCertificateRequest *
MakeCertRequestFromKEMSPKI(const SECItem *spkiDER, CERTName *subject)
{
    CERTSubjectPublicKeyInfo *spki;
    SECKEYPublicKey *pubk;
    CERTCertificateRequest *certReq;

    spki = SECKEY_DecodeDERSubjectPublicKeyInfo(spkiDER);
    if (!spki) {
        SECU_PrintError(progName, "unable to decode subject public key");
        return NULL;
    }
    pubk = SECKEY_ExtractPublicKey(spki);
    if (!pubk || pubk->keyType != kyberKey) {
        PR_fprintf(PR_STDERR, "%s: --kem-pubkey requires an ML-KEM key\n",
                   progName);
        SECKEY_DestroyPublicKey(pubk);
        SECKEY_DestroySubjectPublicKeyInfo(spki);
        return NULL;
    }
    SECKEY_DestroyPublicKey(pubk);

    certReq = CERT_CreateCertificateRequest(subject, spki, NULL);
    SECKEY_DestroySubjectPublicKeyInfo(spki);
    if (!certReq) {
        SECU_PrintError(progName, "unable to make certificate request");
    }
    return certReq;
}

static SECStatus
WritePublicKeyInfo(PRFileDesc *outFile, SECKEYPublicKey *pubk, PRBool ascii)
{
    SECItem *spkiDER;
    SECStatus rv = SECFailure;

    spkiDER = SECKEY_EncodeDERSubjectPublicKeyInfo(pubk);
    if (!spkiDER) {
        SECU_PrintError(progName, "unable to encode subject public key");
        return SECFailure;
    }
    if (ascii) {
        char *obuf = BTOA_ConvertItemToAscii(spkiDER);
        if (obuf) {
            if (PR_fprintf(outFile, "-----BEGIN PUBLIC KEY-----\n%s\n"
                                    "-----END PUBLIC KEY-----\n",
                           obuf) != (PRUint32)-1) {
                rv = SECSuccess;
            }
            PORT_Free(obuf);
        }
    } else {
        PRInt32 written = PR_Write(outFile, spkiDER->data, spkiDER->len);
        if (written >= 0 && (PRUint32)written == spkiDER->len) {
            rv = SECSuccess;
        }
    }
    SECITEM_FreeItem(spkiDER, PR_TRUE);
    if (rv != SECSuccess) {
        SECU_PrintError(progName, "unable to write subject public key");
    }
    return rv;
}

static SECStatus
AddCert(PK11SlotInfo *slot, CERTCertDBHandle *handle, char *name, char *trusts,
        const SECItem *certDER, PRBool emailcert, void *pwdata)
{
    CERTCertTrust *trust = NULL;
    CERTCertificate *cert = NULL;
    SECStatus rv;

    do {
        /* Read in an ASCII cert and return a CERTCertificate */
        cert = CERT_DecodeCertFromPackage((char *)certDER->data, certDER->len);
        if (!cert) {
            SECU_PrintError(progName, "could not decode certificate");
            GEN_BREAK(SECFailure);
        }

        /* Create a cert trust */
        trust = (CERTCertTrust *)PORT_ZAlloc(sizeof(CERTCertTrust));
        if (!trust) {
            SECU_PrintError(progName, "unable to allocate cert trust");
            GEN_BREAK(SECFailure);
        }

        rv = CERT_DecodeTrustString(trust, trusts);
        if (rv) {
            SECU_PrintError(progName, "unable to decode trust string");
            GEN_BREAK(SECFailure);
        }

        rv = PK11_ImportCert(slot, cert, CK_INVALID_HANDLE, name, PR_FALSE);
        if (rv != SECSuccess) {
            /* sigh, PK11_Import Cert and CERT_ChangeCertTrust should have
             * been coded to take a password arg. */
            if (PORT_GetError() == SEC_ERROR_TOKEN_NOT_LOGGED_IN) {
                rv = PK11_Authenticate(slot, PR_TRUE, pwdata);
                if (rv != SECSuccess) {
                    SECU_PrintError(progName,
                                    "could not authenticate to token %s.",
                                    PK11_GetTokenName(slot));
                    GEN_BREAK(SECFailure);
                }
                rv = PK11_ImportCert(slot, cert, CK_INVALID_HANDLE,
                                     name, PR_FALSE);
            }
            if (rv != SECSuccess) {
                SECU_PrintError(progName,
                                "could not add certificate to token or database");
                GEN_BREAK(SECFailure);
            }
        }
        rv = ChangeCertTrust(handle, cert, trust, slot, pwdata);
        if (rv != SECSuccess) {
            SECU_PrintError(progName,
                            "could not change trust on certificate");
            GEN_BREAK(SECFailure);
        }

        if (emailcert) {
            CERT_SaveSMimeProfile(cert, NULL, pwdata);
        }

    } while (0);

    CERT_DestroyCertificate(cert);
    PORT_Free(trust);

    return rv;
}

static SECStatus
CertReq(SECKEYPrivateKey *privk, SECKEYPublicKey *pubk, KeyType keyType,
        SECOidTag hashAlgTag, CERTName *subject, const char *phone, int ascii,
        const char *emailAddrs, const char *dnsNames,
        certutilExtnList extnList, const char *extGeneric,
        PRBool pssCertificate, /*out*/ SECItem *result)
{
    CERTSubjectPublicKeyInfo *spki;
    CERTCertificateRequest *cr;
    SECItem *encoding;
    SECOidTag signAlgTag = SEC_OID_UNKNOWN;
    SECStatus rv;
    PLArenaPool *arena;
    void *extHandle;
    SECItem signedReq = { siBuffer, NULL, 0 };
    SECAlgorithmID signAlg;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (!arena) {
        SECU_PrintError(progName, "out of memory");
        return SECFailure;
    }

    /* Create info about public key */
    spki = SECKEY_CreateSubjectPublicKeyInfo(pubk);
    if (!spki) {
        PORT_FreeArena(arena, PR_FALSE);
        SECU_PrintError(progName, "unable to create subject public key");
        return SECFailure;
    }

    /* Change cert type to RSA-PSS, if desired. */
    if (pssCertificate) {
        /* force a PSS signature. We can do a PSS signature with an
         * RSA key, this will force us to generate a PSS signature */
        signAlgTag = SEC_OID_PKCS1_RSA_PSS_SIGNATURE;
        /* override the SPKI algorithm id. */
        rv = SEC_CreateSignatureAlgorithmID(arena, &spki->algorithm,
                                            signAlgTag, hashAlgTag,
                                            NULL, NULL, pubk);
        if (rv != SECSuccess) {
            PORT_FreeArena(arena, PR_FALSE);
            SECKEY_DestroySubjectPublicKeyInfo(spki);
            SECU_PrintError(progName, "unable to set algorithm ID");
            return SECFailure;
        }
    }

    /* Generate certificate request */
    cr = CERT_CreateCertificateRequest(subject, spki, NULL);
    SECKEY_DestroySubjectPublicKeyInfo(spki);
    if (!cr) {
        PORT_FreeArena(arena, PR_FALSE);
        SECU_PrintError(progName, "unable to make certificate request");
        return SECFailure;
    }

    extHandle = CERT_StartCertificateRequestAttributes(cr);
    if (extHandle == NULL) {
        PORT_FreeArena(arena, PR_FALSE);
        CERT_DestroyCertificateRequest(cr);
        return SECFailure;
    }
    if (AddExtensions(extHandle, emailAddrs, dnsNames, extnList, extGeneric) !=
        SECSuccess) {
        PORT_FreeArena(arena, PR_FALSE);
        CERT_FinishExtensions(extHandle);
        CERT_DestroyCertificateRequest(cr);
        return SECFailure;
    }
    CERT_FinishExtensions(extHandle);
    CERT_FinishCertificateRequestAttributes(cr);

    /* Der encode the request */
    encoding = SEC_ASN1EncodeItem(arena, NULL, cr,
                                  SEC_ASN1_GET(CERT_CertificateRequestTemplate));
    CERT_DestroyCertificateRequest(cr);
    if (encoding == NULL) {
        PORT_FreeArena(arena, PR_FALSE);
        SECU_PrintError(progName, "der encoding of request failed");
        return SECFailure;
    }

    PORT_Memset(&signAlg, 0, sizeof(signAlg));
    rv = SEC_CreateSignatureAlgorithmID(arena, &signAlg, signAlgTag, hashAlgTag,
                                        NULL, privk, NULL);
    if (rv != SECSuccess) {
        PORT_FreeArena(arena, PR_FALSE);
        SECU_PrintError(progName, "can't create a signature algorithm id");
        return SECFailure;
    }

    /* Sign the request */
    rv = SEC_DerSignDataWithAlgorithmID(arena, &signedReq,
                                        encoding->data, encoding->len,
                                        privk, &signAlg);
    if (rv) {
        PORT_FreeArena(arena, PR_FALSE);
        SECU_PrintError(progName, "signing of data failed");
        return SECFailure;
    }

    /* Encode request in specified format */
    if (ascii) {
        char *obuf;
        char *header, *name, *email, *org, *state, *country;

        obuf = BTOA_ConvertItemToAscii(&signedReq);
        if (!obuf) {
            goto oom;
        }

        name = CERT_GetCommonName(subject);
        if (!name) {
            name = PORT_Strdup("(not specified)");
        }

        if (!phone)
            phone = "(not specified)";

        email = CERT_GetCertEmailAddress(subject);
        if (!email)
            email = PORT_Strdup("(not specified)");

        org = CERT_GetOrgName(subject);
        if (!org)
            org = PORT_Strdup("(not specified)");

        state = CERT_GetStateName(subject);
        if (!state)
            state = PORT_Strdup("(not specified)");

        country = CERT_GetCountryName(subject);
        if (!country)
            country = PORT_Strdup("(not specified)");

        header = PR_smprintf(
            "\nCertificate request generated by Netscape certutil\n"
            "Phone: %s\n\n"
            "Common Name: %s\n"
            "Email: %s\n"
            "Organization: %s\n"
            "State: %s\n"
            "Country: %s\n\n"
            "%s\n",
            phone, name, email, org, state, country, NS_CERTREQ_HEADER);

        PORT_Free(name);
        PORT_Free(email);
        PORT_Free(org);
        PORT_Free(state);
        PORT_Free(country);

        if (header) {
            char *trailer = PR_smprintf("\n%s\n", NS_CERTREQ_TRAILER);
            if (trailer) {
                PRUint32 headerLen = PL_strlen(header);
                PRUint32 obufLen = PL_strlen(obuf);
                PRUint32 trailerLen = PL_strlen(trailer);
                SECITEM_AllocItem(NULL, result,
                                  headerLen + obufLen + trailerLen);
                if (result->data) {
                    PORT_Memcpy(result->data, header, headerLen);
                    PORT_Memcpy(result->data + headerLen, obuf, obufLen);
                    PORT_Memcpy(result->data + headerLen + obufLen,
                                trailer, trailerLen);
                }
                PR_smprintf_free(trailer);
            }
            PR_smprintf_free(header);
        }
        PORT_Free(obuf);
    } else {
        (void)SECITEM_CopyItem(NULL, result, &signedReq);
    }

    if (!result->data) {
    oom:
        SECU_PrintError(progName, "out of memory");
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        rv = SECFailure;
    }

    PORT_FreeArena(arena, PR_FALSE);
    return rv;
}

static SECStatus
ChangeTrustAttributes(CERTCertDBHandle *handle, PK11SlotInfo *slot,
                      char *name, char *trusts, void *pwdata)
{
    SECStatus rv;
    CERTCertificate *cert;
    CERTCertTrust *trust;

    cert = CERT_FindCertByNicknameOrEmailAddrCX(handle, name, pwdata);
    if (!cert) {
        SECU_PrintError(progName, "could not find certificate named \"%s\"",
                        name);
        return SECFailure;
    }

    trust = (CERTCertTrust *)PORT_ZAlloc(sizeof(CERTCertTrust));
    if (!trust) {
        SECU_PrintError(progName, "unable to allocate cert trust");
        return SECFailure;
    }

    /* This function only decodes these characters: pPwcTCu, */
    rv = CERT_DecodeTrustString(trust, trusts);
    if (rv) {
        SECU_PrintError(progName, "unable to decode trust string");
        return SECFailure;
    }

    /* CERT_ChangeCertTrust API does not have a way to pass in
     * a context, so NSS can't prompt for the password if it needs to.
     * check to see if the failure was token not logged in and
     * log in if need be. */
    rv = ChangeCertTrust(handle, cert, trust, slot, pwdata);
    if (rv != SECSuccess) {
        SECU_PrintError(progName, "unable to modify trust attributes");
        return SECFailure;
    }
    CERT_DestroyCertificate(cert);
    PORT_Free(trust);

    return SECSuccess;
}

static SECStatus
DumpChain(CERTCertDBHandle *handle, char *name, PRBool ascii,
          PRBool simpleSelfSigned)
{
    CERTCertificate *the_cert;
    CERTCertificateList *chain;
    int i, j;
    the_cert = SECU_FindCertByNicknameOrFilename(handle, name,
                                                 ascii, NULL);
    if (!the_cert) {
        SECU_PrintError(progName, "Could not find: %s\n", name);
        return SECFailure;
    }
    if (simpleSelfSigned &&
        SECEqual == SECITEM_CompareItem(&the_cert->derIssuer,
                                        &the_cert->derSubject)) {
        printf("\"%s\" [%s]\n\n", the_cert->nickname, the_cert->subjectName);
        CERT_DestroyCertificate(the_cert);
        return SECSuccess;
    }

    chain = CERT_CertChainFromCert(the_cert, 0, PR_TRUE);
    CERT_DestroyCertificate(the_cert);
    if (!chain) {
        SECU_PrintError(progName, "Could not obtain chain for: %s\n", name);
        return SECFailure;
    }
    for (i = chain->len - 1; i >= 0; i--) {
        CERTCertificate *c;
        c = CERT_FindCertByDERCert(handle, &chain->certs[i]);
        for (j = i; j < chain->len - 1; j++) {
            printf("  ");
        }
        if (c) {
            printf("\"%s\" [%s]\n\n", c->nickname, c->subjectName);
            CERT_DestroyCertificate(c);
        } else {
            printf("(null)\n\n");
        }
    }
    CERT_DestroyCertificateList(chain);
    return SECSuccess;
}

static SECStatus
outputCertOrExtension(CERTCertificate *the_cert, PRBool raw, PRBool ascii,
                      SECItem *extensionOID, PRFileDesc *outfile)
{
    SECItem data;
    PRInt32 numBytes;
    SECStatus rv = SECFailure;
    if (extensionOID) {
        int i;
        PRBool found = PR_FALSE;
        for (i = 0; the_cert->extensions[i] != NULL; i++) {
            CERTCertExtension *extension = the_cert->extensions[i];
            if (SECITEM_CompareItem(&extension->id, extensionOID) == SECEqual) {
                found = PR_TRUE;
                numBytes = PR_Write(outfile, extension->value.data,
                                    extension->value.len);
                rv = SECSuccess;
                if (numBytes != (PRInt32)extension->value.len) {
                    SECU_PrintSystemError(progName, "error writing extension");
                    rv = SECFailure;
                }
                break;
            }
        }
        if (!found) {
            SECU_PrintSystemError(progName, "extension not found");
            rv = SECFailure;
        }
    } else {
        data.data = the_cert->derCert.data;
        data.len = the_cert->derCert.len;
        if (ascii) {
            PR_fprintf(outfile, "%s\n%s\n%s\n", NS_CERT_HEADER,
                       BTOA_DataToAscii(data.data, data.len), NS_CERT_TRAILER);
            rv = SECSuccess;
        } else if (raw) {
            numBytes = PR_Write(outfile, data.data, data.len);
            rv = SECSuccess;
            if (numBytes != (PRInt32)data.len) {
                SECU_PrintSystemError(progName, "error writing raw cert");
                rv = SECFailure;
            }
        } else {
            rv = SEC_PrintCertificateAndTrust(the_cert, "Certificate", NULL);
            if (rv != SECSuccess) {
                SECU_PrintError(progName, "problem printing certificate");
            }
        }
    }
    return rv;
}

static SECStatus
listCerts(CERTCertDBHandle *handle, char *name, char *email,
          PK11SlotInfo *slot, PRBool raw, PRBool ascii,
          SECItem *extensionOID,
          PRFileDesc *outfile, void *pwarg)
{
    SECStatus rv = SECFailure;
    CERTCertList *certs;
    CERTCertListNode *node;

    /* List certs on a non-internal slot. */
    if (!PK11_IsFriendly(slot) && PK11_NeedLogin(slot)) {
        SECStatus newrv = PK11_Authenticate(slot, PR_TRUE, pwarg);
        if (newrv != SECSuccess) {
            SECU_PrintError(progName, "could not authenticate to token %s.",
                            PK11_GetTokenName(slot));
            return SECFailure;
        }
    }
    if (name) {
        CERTCertificate *the_cert =
            SECU_FindCertByNicknameOrFilename(handle, name, ascii, NULL);
        if (!the_cert) {
            SECU_PrintError(progName, "Could not find cert: %s\n", name);
            return SECFailure;
        }
        /* Here, we have one cert with the desired nickname or email
         * address.  Now, we will attempt to get a list of ALL certs
         * with the same subject name as the cert we have.  That list
         * should contain, at a minimum, the one cert we have already found.
         * If the list of certs is empty (NULL), the libraries have failed.
         */
        certs = CERT_CreateSubjectCertList(NULL, handle, &the_cert->derSubject,
                                           PR_Now(), PR_FALSE);
        CERT_DestroyCertificate(the_cert);
        if (!certs) {
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            SECU_PrintError(progName, "problem printing certificates");
            return SECFailure;
        }
        for (node = CERT_LIST_HEAD(certs); !CERT_LIST_END(node, certs);
             node = CERT_LIST_NEXT(node)) {
            rv = outputCertOrExtension(node->cert, raw, ascii, extensionOID,
                                       outfile);
            if (rv != SECSuccess) {
                break;
            }
        }
    } else if (email) {
        certs = PK11_FindCertsFromEmailAddress(email, NULL);
        if (!certs) {
            SECU_PrintError(progName,
                            "Could not find certificates for email address: %s\n",
                            email);
            return SECFailure;
        }
        for (node = CERT_LIST_HEAD(certs); !CERT_LIST_END(node, certs);
             node = CERT_LIST_NEXT(node)) {
            rv = outputCertOrExtension(node->cert, raw, ascii, extensionOID,
                                       outfile);
            if (rv != SECSuccess) {
                break;
            }
        }
    } else {
        certs = PK11_ListCertsInSlot(slot);
        if (certs) {
            for (node = CERT_LIST_HEAD(certs); !CERT_LIST_END(node, certs);
                 node = CERT_LIST_NEXT(node)) {
                SECU_PrintCertNickname(node, stdout);
            }
            rv = SECSuccess;
        }
    }
    if (certs) {
        CERT_DestroyCertList(certs);
    }
    if (rv) {
        SECU_PrintError(progName, "problem printing certificate nicknames");
        return SECFailure;
    }

    return SECSuccess; /* not rv ?? */
}

static SECStatus
ListCerts(CERTCertDBHandle *handle, char *nickname, char *email,
          PK11SlotInfo *slot, PRBool raw, PRBool ascii,
          SECItem *extensionOID,
          PRFileDesc *outfile, secuPWData *pwdata)
{
    SECStatus rv;

    if (slot && PK11_NeedUserInit(slot)) {
        printf("\nDatabase needs user init\n");
    }

    if (!ascii && !raw && !nickname && !email) {
        PR_fprintf(outfile, "\n%-60s %-5s\n%-60s %-5s\n\n",
                   "Certificate Nickname", "Trust Attributes", "",
                   "SSL,S/MIME,JAR/XPI");
    }
    if (slot == NULL) {
        CERTCertList *list;
        CERTCertListNode *node;

        list = PK11_ListCerts(PK11CertListAll, pwdata);
        for (node = CERT_LIST_HEAD(list); !CERT_LIST_END(node, list);
             node = CERT_LIST_NEXT(node)) {
            SECU_PrintCertNickname(node, stdout);
        }
        CERT_DestroyCertList(list);
        return SECSuccess;
    }
    rv = listCerts(handle, nickname, email, slot, raw, ascii,
                   extensionOID, outfile, pwdata);
    return rv;
}

static SECStatus
DeleteCert(CERTCertDBHandle *handle, char *name, void *pwdata)
{
    SECStatus rv;
    CERTCertificate *cert;

    cert = CERT_FindCertByNicknameOrEmailAddrCX(handle, name, pwdata);
    if (!cert) {
        SECU_PrintError(progName, "could not find certificate named \"%s\"",
                        name);
        return SECFailure;
    }

    rv = SEC_DeletePermCertificate(cert);
    CERT_DestroyCertificate(cert);
    if (rv) {
        SECU_PrintError(progName, "unable to delete certificate");
    }
    return rv;
}

static SECStatus
RenameCert(CERTCertDBHandle *handle, char *name, char *newName, void *pwdata)
{
    SECStatus rv;
    CERTCertificate *cert;

    cert = CERT_FindCertByNicknameOrEmailAddrCX(handle, name, pwdata);
    if (!cert) {
        SECU_PrintError(progName, "could not find certificate named \"%s\"",
                        name);
        return SECFailure;
    }

    rv = __PK11_SetCertificateNickname(cert, newName);
    CERT_DestroyCertificate(cert);
    if (rv) {
        SECU_PrintError(progName, "unable to rename certificate");
    }
    return rv;
}

static SECStatus
ValidateCert(CERTCertDBHandle *handle, char *name, char *date,
             char *certUsage, PRBool checkSig, PRBool logit,
             PRBool ascii, secuPWData *pwdata)
{
    SECStatus rv;
    CERTCertificate *cert = NULL;
    PRTime timeBoundary;
    SECCertificateUsage usage;
    CERTVerifyLog reallog;
    CERTVerifyLog *log = NULL;

    if (!certUsage) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return (SECFailure);
    }

    switch (*certUsage) {
        case 'O':
            usage = certificateUsageStatusResponder;
            break;
        case 'L':
            usage = certificateUsageSSLCA;
            break;
        case 'A':
            usage = certificateUsageAnyCA;
            break;
        case 'Y':
            usage = certificateUsageVerifyCA;
            break;
        case 'C':
            usage = certificateUsageSSLClient;
            break;
        case 'V':
            usage = certificateUsageSSLServer;
            break;
        case 'I':
            usage = certificateUsageIPsec;
            break;
        case 'S':
            usage = certificateUsageEmailSigner;
            break;
        case 'R':
            usage = certificateUsageEmailRecipient;
            break;
        case 'J':
            usage = certificateUsageObjectSigner;
            break;
        default:
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            return (SECFailure);
    }
    do {
        cert = SECU_FindCertByNicknameOrFilename(handle, name, ascii,
                                                 NULL);
        if (!cert) {
            SECU_PrintError(progName, "could not find certificate named \"%s\"",
                            name);
            GEN_BREAK(SECFailure)
        }

        if (date != NULL) {
            rv = DER_AsciiToTime(&timeBoundary, date);
            if (rv) {
                SECU_PrintError(progName, "invalid input date");
                GEN_BREAK(SECFailure)
            }
        } else {
            timeBoundary = PR_Now();
        }

        if (logit) {
            log = &reallog;

            log->count = 0;
            log->head = NULL;
            log->tail = NULL;
            log->arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
            if (log->arena == NULL) {
                SECU_PrintError(progName, "out of memory");
                GEN_BREAK(SECFailure)
            }
        }

        rv = CERT_VerifyCertificate(handle, cert, checkSig, usage,
                                    timeBoundary, pwdata, log, &usage);
        if (log) {
            if (log->head == NULL) {
                fprintf(stdout, "%s: certificate is valid\n", progName);
                GEN_BREAK(SECSuccess)
            } else {
                char *nick;
                CERTVerifyLogNode *node;

                node = log->head;
                while (node) {
                    if (node->cert->nickname != NULL) {
                        nick = node->cert->nickname;
                    } else {
                        nick = node->cert->subjectName;
                    }
                    fprintf(stderr, "%s : %s\n", nick,
                            SECU_Strerror(node->error));
                    CERT_DestroyCertificate(node->cert);
                    node = node->next;
                }
            }
        } else {
            if (rv != SECSuccess) {
                PRErrorCode perr = PORT_GetError();
                fprintf(stdout, "%s: certificate is invalid: %s\n",
                        progName, SECU_Strerror(perr));
                GEN_BREAK(SECFailure)
            }
            fprintf(stdout, "%s: certificate is valid\n", progName);
            GEN_BREAK(SECSuccess)
        }
    } while (0);

    if (cert) {
        CERT_DestroyCertificate(cert);
    }

    return (rv);
}

static PRBool
ItemIsPrintableASCII(const SECItem *item)
{
    unsigned char *src = item->data;
    unsigned int len = item->len;
    while (len-- > 0) {
        unsigned char uc = *src++;
        if (uc < 0x20 || uc > 0x7e)
            return PR_FALSE;
    }
    return PR_TRUE;
}

/* Caller ensures that dst is at least item->len*2+1 bytes long */
static void
SECItemToHex(const SECItem *item, char *dst)
{
    if (dst && item && item->data) {
        unsigned char *src = item->data;
        unsigned int len = item->len;
        for (; len > 0; --len, dst += 2) {
            snprintf(dst, 3, "%02x", *src++);
        }
        *dst = '\0';
    }
}

#define MAX_CKA_ID_BIN_LEN 20
#define MAX_CKA_ID_STR_LEN 40

/* output human readable key ID in buffer, which should have at least
 * MAX_CKA_ID_STR_LEN + 3 octets (quotations and a null terminator) */
static void
formatPrivateKeyID(SECKEYPrivateKey *privkey, char *buffer)
{
    SECItem *ckaID;

    ckaID = PK11_GetLowLevelKeyIDForPrivateKey(privkey);
    if (!ckaID) {
        strcpy(buffer, "(no CKA_ID)");
    } else if (ItemIsPrintableASCII(ckaID)) {
        int len = PR_MIN(MAX_CKA_ID_STR_LEN, ckaID->len);
        buffer[0] = '"';
        memcpy(buffer + 1, ckaID->data, len);
        buffer[1 + len] = '"';
        buffer[2 + len] = '\0';
    } else {
        /* print ckaid in hex */
        SECItem idItem = *ckaID;
        if (idItem.len > MAX_CKA_ID_BIN_LEN)
            idItem.len = MAX_CKA_ID_BIN_LEN;
        SECItemToHex(&idItem, buffer);
    }
    SECITEM_ZfreeItem(ckaID, PR_TRUE);
}

/* print key number, key ID (in hex or ASCII), key label (nickname) */
static SECStatus
PrintKey(PRFileDesc *out, const char *nickName, int count,
         SECKEYPrivateKey *key, void *pwarg)
{
    char ckaIDbuf[MAX_CKA_ID_STR_LEN + 4];
    CERTCertificate *cert;
    KeyType keyType;

    formatPrivateKeyID(key, ckaIDbuf);
    cert = PK11_GetCertFromPrivateKey(key);
    if (cert) {
        keyType = CERT_GetCertKeyType(&cert->subjectPublicKeyInfo);
        CERT_DestroyCertificate(cert);
    } else {
        keyType = key->keyType;
    }
    PR_fprintf(out, "<%2d> %-8.8s %-42.42s %s\n", count,
               SECKEY_GetKeyTypeString(keyType), ckaIDbuf, nickName);

    return SECSuccess;
}

/* returns SECSuccess if ANY keys are found, SECFailure otherwise. */
static SECStatus
ListKeysInSlot(PK11SlotInfo *slot, const char *nickName, KeyType keyType,
               void *pwarg)
{
    SECKEYPrivateKeyList *list;
    SECKEYPrivateKeyListNode *node;
    int count = 0;

    if (PK11_NeedLogin(slot)) {
        SECStatus rv = PK11_Authenticate(slot, PR_TRUE, pwarg);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "could not authenticate to token %s.",
                            PK11_GetTokenName(slot));
            return SECFailure;
        }
    }

    if (nickName && nickName[0])
        list = PK11_ListPrivKeysInSlot(slot, (char *)nickName, pwarg);
    else
        list = PK11_ListPrivateKeysInSlot(slot);
    if (list == NULL) {
        SECU_PrintError(progName, "problem listing keys");
        return SECFailure;
    }
    for (node = PRIVKEY_LIST_HEAD(list);
         !PRIVKEY_LIST_END(node, list);
         node = PRIVKEY_LIST_NEXT(node)) {
        char *keyName;
        static const char orphan[] = { "(orphan)" };

        if (keyType != nullKey && keyType != node->key->keyType)
            continue;
        keyName = PK11_GetPrivateKeyNickname(node->key);
        if (!keyName || !keyName[0]) {
            /* Try extra hard to find nicknames for keys that lack them. */
            CERTCertificate *cert;
            PORT_Free((void *)keyName);
            keyName = NULL;
            cert = PK11_GetCertFromPrivateKey(node->key);
            if (cert) {
                if (cert->nickname && cert->nickname[0]) {
                    keyName = PORT_Strdup(cert->nickname);
                } else if (cert->emailAddr && cert->emailAddr[0]) {
                    keyName = PORT_Strdup(cert->emailAddr);
                }
                CERT_DestroyCertificate(cert);
            }
        }
        if (nickName) {
            if (!keyName || PL_strcmp(keyName, nickName)) {
                /* PKCS#11 module returned unwanted keys */
                PORT_Free((void *)keyName);
                continue;
            }
        }
        if (!keyName)
            keyName = (char *)orphan;

        PrintKey(PR_STDOUT, keyName, count, node->key, pwarg);

        if (keyName != (char *)orphan)
            PORT_Free((void *)keyName);
        count++;
    }
    SECKEY_DestroyPrivateKeyList(list);

    if (count == 0) {
        PR_fprintf(PR_STDOUT, "%s: no keys found\n", progName);
        return SECFailure;
    }
    return SECSuccess;
}

/* returns SECSuccess if ANY keys are found, SECFailure otherwise. */
static SECStatus
ListKeys(PK11SlotInfo *slot, const char *nickName, int index,
         KeyType keyType, PRBool dopriv, secuPWData *pwdata)
{
    SECStatus rv = SECFailure;
    static const char fmt[] =
        "%s: Checking token \"%.33s\" in slot \"%.65s\"\n";

    if (slot == NULL) {
        PK11SlotList *list;
        PK11SlotListElement *le;

        list = PK11_GetAllTokens(CKM_INVALID_MECHANISM, PR_FALSE, PR_FALSE, pwdata);
        if (list) {
            for (le = list->head; le; le = le->next) {
                PR_fprintf(PR_STDOUT, fmt, progName,
                           PK11_GetTokenName(le->slot),
                           PK11_GetSlotName(le->slot));
                rv &= ListKeysInSlot(le->slot, nickName, keyType, pwdata);
            }
            PK11_FreeSlotList(list);
        }
    } else {
        PR_fprintf(PR_STDOUT, fmt, progName, PK11_GetTokenName(slot),
                   PK11_GetSlotName(slot));
        rv = ListKeysInSlot(slot, nickName, keyType, pwdata);
    }
    return rv;
}

static SECStatus
DeleteCertAndKey(char *nickname, secuPWData *pwdata)
{
    SECStatus rv;
    CERTCertificate *cert;
    PK11SlotInfo *slot;

    slot = PK11_GetInternalKeySlot();
    if (PK11_NeedLogin(slot)) {
        rv = PK11_Authenticate(slot, PR_TRUE, pwdata);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "could not authenticate to token %s.",
                            PK11_GetTokenName(slot));
            PK11_FreeSlot(slot);
            return SECFailure;
        }
    }
    cert = PK11_FindCertFromNickname(nickname, pwdata);
    if (!cert) {
        PK11_FreeSlot(slot);
        return SECFailure;
    }
    rv = PK11_DeleteTokenCertAndKey(cert, pwdata);
    if (rv != SECSuccess) {
        SECU_PrintError("problem deleting private key \"%s\"\n", nickname);
    }
    CERT_DestroyCertificate(cert);
    PK11_FreeSlot(slot);
    return rv;
}

static SECKEYPrivateKey *
findPrivateKeyByID(PK11SlotInfo *slot, const char *ckaID, secuPWData *pwarg)
{
    PORTCheapArenaPool arena;
    SECItem ckaIDItem = { 0 };
    SECKEYPrivateKey *privkey = NULL;
    SECStatus rv;

    if (PK11_NeedLogin(slot)) {
        rv = PK11_Authenticate(slot, PR_TRUE, pwarg);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "could not authenticate to token %s.",
                            PK11_GetTokenName(slot));
            return NULL;
        }
    }

    if (0 == PL_strncasecmp("0x", ckaID, 2)) {
        ckaID += 2; /* skip leading "0x" */
    }
    PORT_InitCheapArena(&arena, DER_DEFAULT_CHUNKSIZE);
    if (SECU_HexString2SECItem(&arena.arena, &ckaIDItem, ckaID)) {
        privkey = PK11_FindKeyByKeyID(slot, &ckaIDItem, pwarg);
    }
    PORT_DestroyCheapArena(&arena);
    return privkey;
}

static SECStatus
DeleteKey(SECKEYPrivateKey *privkey, secuPWData *pwarg)
{
    SECStatus rv;
    PK11SlotInfo *slot;

    slot = PK11_GetSlotFromPrivateKey(privkey);
    if (PK11_NeedLogin(slot)) {
        rv = PK11_Authenticate(slot, PR_TRUE, pwarg);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "could not authenticate to token %s.",
                            PK11_GetTokenName(slot));
            return SECFailure;
        }
    }

    rv = PK11_DeleteTokenPrivateKey(privkey, PR_TRUE);
    if (rv != SECSuccess) {
        char ckaIDbuf[MAX_CKA_ID_STR_LEN + 4];
        formatPrivateKeyID(privkey, ckaIDbuf);
        SECU_PrintError("problem deleting private key \"%s\"\n", ckaIDbuf);
    }

    PK11_FreeSlot(slot);
    return rv;
}

/*
 *  L i s t M o d u l e s
 *
 *  Print a list of the PKCS11 modules that are
 *  available. This is useful for smartcard people to
 *  make sure they have the drivers loaded.
 *
 */
static SECStatus
ListModules(void)
{
    PK11SlotList *list;
    PK11SlotListElement *le;

    /* get them all! */
    list = PK11_GetAllTokens(CKM_INVALID_MECHANISM, PR_FALSE, PR_FALSE, NULL);
    if (list == NULL)
        return SECFailure;

    /* look at each slot*/
    for (le = list->head; le; le = le->next) {
        char *token_uri = PK11_GetTokenURI(le->slot);
        printf("\n");
        printf("    slot: %s\n", PK11_GetSlotName(le->slot));
        printf("   token: %s\n", PK11_GetTokenName(le->slot));
        printf("     uri: %s\n", token_uri);
        PORT_Free(token_uri);
    }
    PK11_FreeSlotList(list);

    return SECSuccess;
}

static void
PrintBuildFlags()
{
#ifdef NSS_FIPS_DISABLED
    PR_fprintf(PR_STDOUT, "NSS_FIPS_DISABLED\n");
#endif
#ifdef NSS_NO_INIT_SUPPORT
    PR_fprintf(PR_STDOUT, "NSS_NO_INIT_SUPPORT\n");
#endif
    exit(0);
}

static void
PrintSyntax()
{
#define FPS fprintf(stderr,
    FPS "Type %s -H for more detailed descriptions\n", progName);
    FPS "Usage:  %s -N [-d certdir] [-P dbprefix] [-f pwfile] [--empty-password]\n", progName);
    FPS "Usage:  %s -T [-d certdir] [-P dbprefix] [-h token-name]\n"
        "\t\t [-f pwfile] [-0 SSO-password]\n", progName);
    FPS "\t%s -A -n cert-name -t trustargs [-d certdir] [-P dbprefix] [-a] [-i input]\n",
        progName);
    FPS "\t%s -B -i batch-file\n", progName);
    FPS "\t%s -C [-c issuer-name | -x] -i cert-request-file -o cert-file\n"
        "\t\t [-m serial-number] [-w warp-months] [-v months-valid]\n"
        "\t\t [-f pwfile] [-d certdir] [-P dbprefix] [-Z hashAlg]\n"
        "\t\t [-1 | --keyUsage [keyUsageKeyword,..]] [-2] [-3] [-4]\n"
        "\t\t [-5 | --nsCertType [nsCertTypeKeyword,...]]\n"
        "\t\t [-6 | --extKeyUsage [extKeyUsageKeyword,...]] [-7 emailAddrs]\n"
        "\t\t [-8 dns-names] [-a]\n",
        progName);
    FPS "\t%s -C -c issuer-name --kem-pubkey spki-file -s subject -o cert-file\n"
        "\t\t [other -C options]\n",
        progName);
    FPS "\t%s -D -n cert-name [-d certdir] [-P dbprefix]\n", progName);
    FPS "\t%s --rename -n cert-name --new-n new-cert-name\n"
        "\t\t [-d certdir] [-P dbprefix]\n", progName);
    FPS "\t%s -E -n cert-name -t trustargs [-d certdir] [-P dbprefix] [-a] [-i input]\n",
        progName);
    FPS "\t%s -F -n cert-name [-d certdir] [-P dbprefix]\n",
        progName);
    FPS "\t%s -F -k key-id [-d certdir] [-P dbprefix]\n",
        progName);
    FPS "\t%s -G -n key-name [-h token-name] [-k rsa] [-g key-size] [-y exp]\n"
        "\t\t [-f pwfile] [-z noisefile] [-d certdir] [-P dbprefix]\n", progName);
    FPS "\t%s -G [-h token-name] -k dsa [-q pqgfile -g key-size] [-f pwfile]\n"
        "\t\t [-z noisefile] [-d certdir] [-P dbprefix]\n", progName);
    FPS "\t%s -G [-h token-name] -k ec -q curve [-f pwfile]\n"
        "\t\t [-z noisefile] [-d certdir] [-P dbprefix]\n", progName);
    FPS "\t%s -G [-h token-name] -k ed -q curve [-f pwfile]\n"
        "\t\t [-z noisefile] [-d certdir] [-P dbprefix]\n", progName);
    FPS "\t%s -G [-h token-name] -k mldsa -q paramset [-f pwfile]\n"
        "\t\t [-z noisefile] [-d certdir] [-P dbprefix]\n", progName);
    FPS "\t%s -G [-h token-name] -k mlkem -q paramset [-o spki-file] [-a]\n"
        "\t\t [-f pwfile] [-z noisefile] [-d certdir] [-P dbprefix]\n", progName);
    FPS "\t%s -K [-n key-name] [-h token-name] [-k dsa|ec|ed|rsa|mldsa|mlkem|all]\n",
         progName);
    FPS "\t\t [-f pwfile] [-X] [-d certdir] [-P dbprefix]\n");
    FPS "\t%s --upgrade-merge --source-dir upgradeDir --upgrade-id uniqueID\n",
        progName);
    FPS "\t\t [--upgrade-token-name tokenName] [-d targetDBDir]\n");
    FPS "\t\t [-P targetDBPrefix] [--source-prefix upgradeDBPrefix]\n");
    FPS "\t\t [-f targetPWfile] [-@ upgradePWFile]\n");
    FPS "\t%s --merge --source-dir sourceDBDir [-d targetDBdir]\n",
        progName);
    FPS "\t\t [-P targetDBPrefix] [--source-prefix sourceDBPrefix]\n");
    FPS "\t\t [-f targetPWfile] [-@ sourcePWFile]\n");
    FPS "\t%s -L [-n cert-name] [-h token-name] [--email email-address]\n",
        progName);
    FPS "\t\t [-X] [-r] [-a] [--dump-ext-val OID] [-d certdir] [-P dbprefix]\n");
    FPS "\t%s --build-flags\n", progName);
    FPS "\t%s -M -n cert-name -t trustargs [-d certdir] [-P dbprefix]\n",
        progName);
    FPS "\t%s -O -n cert-name [-X] [-d certdir] [-a] [-P dbprefix]\n"
        "\t\t [--simple-self-signed]\n",
        progName);
    FPS "\t%s -R -s subj -o cert-request-file [-d certdir] [-P dbprefix] [-p phone] [-a]\n"
        "\t\t [-7 emailAddrs] [-k key-type-or-id] [-h token-name] [-f pwfile]\n"
        "\t\t [-g key-size] [-Z hashAlg]\n",
        progName);
    FPS "\t%s -V -n cert-name -u usage [-b time] [-e] [-a]\n"
        "\t\t[-X] [-d certdir] [-P dbprefix]\n",
        progName);
    FPS "Usage:  %s -W [-d certdir] [-f pwfile] [-@newpwfile]\n",
        progName);
    FPS "\t%s -S -n cert-name -s subj [-c issuer-name | -x]  -t trustargs\n"
        "\t\t [-k key-type-or-id] [-q key-params] [-h token-name] [-g key-size]\n"
        "\t\t [-m serial-number] [-w warp-months] [-v months-valid]\n"
        "\t\t [-f pwfile] [-d certdir] [-P dbprefix] [-Z hashAlg]\n"
        "\t\t [-p phone] [-1] [-2] [-3] [-4] [-5] [-6] [-7 emailAddrs]\n"
        "\t\t [-8 DNS-names]\n"
        "\t\t [--extAIA] [--extSIA] [--extCP] [--extPM] [--extPC] [--extIA]\n"
        "\t\t [--extSKID] [--extNC] [--extSAN type:name[,type:name]...]\n"
        "\t\t [--extGeneric OID:critical-flag:filename[,OID:critical-flag:filename]...]\n", progName);
    FPS "\t%s -U [-X] [-d certdir] [-P dbprefix]\n", progName);
    exit(1);
}

enum usage_level {
    usage_all = 0,
    usage_selected = 1
};

static void luCommonDetailsAE();

static void
luA(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "A"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Add a certificate to the database        (create if needed)\n",
        "-A");
    if (ul == usage_selected && !is_my_command)
        return;
    if (ul == usage_all) {
    FPS "%-20s\n", "   All options under -E apply");
    } else {
        luCommonDetailsAE();
    }
}

static void
luB(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "B"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Run a series of certutil commands from a batch file\n", "-B");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s Specify the batch file\n", "   -i batch-file");
}

static void
luE(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "E"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Add an Email certificate to the database (create if needed)\n",
        "-E");
    if (ul == usage_selected && !is_my_command)
        return;
    luCommonDetailsAE();
}

static void
luCommonDetailsAE()
{
    FPS "%-20s Specify the nickname of the certificate to add\n",
        "   -n cert-name");
    FPS "%-20s Set the certificate trust attributes:\n",
        "   -t trustargs");
    FPS "%-25s trustargs is of the form x,y,z where x is for SSL, y is for S/MIME,\n", "");
    FPS "%-25s and z is for code signing. Use ,, for no explicit trust.\n", "");
    FPS "%-25s p \t prohibited (explicitly distrusted)\n", "");
    FPS "%-25s P \t trusted peer\n", "");
    FPS "%-25s c \t valid CA\n", "");
    FPS "%-25s T \t trusted CA to issue client certs (implies c)\n", "");
    FPS "%-25s C \t trusted CA to issue server certs (implies c)\n", "");
    FPS "%-25s u \t user cert\n", "");
    FPS "%-25s w \t send warning\n", "");
    FPS "%-25s g \t make step-up cert\n", "");
    FPS "%-20s Specify the password file\n",
        "   -f pwfile");
    FPS "%-20s Cert database directory (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "%-20s The input certificate is encoded in ASCII (RFC1113)\n",
        "   -a");
    FPS "%-20s Specify the certificate file (default is stdin)\n",
        "   -i input");
    FPS "\n");
}

static void
luC(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "C"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Create a new binary certificate from a BINARY cert request\n",
        "-C");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s The nickname of the issuer cert\n",
        "   -c issuer-name");
    FPS "%-20s The BINARY certificate request file\n",
        "   -i cert-request ");
    FPS "%-20s Instead of a cert request, issue the certificate for the\n"
        "%-20s ML-KEM SubjectPublicKeyInfo in this file (see -G -o).\n"
        "%-20s Requires -s.\n"
        "%-20s No proof of possession of the private key is checked.\n",
        "   --kem-pubkey spki-file", "", "", "");
    FPS "%-20s Subject name for the certificate (with --kem-pubkey only)\n",
        "   -s subject");
    FPS "%-20s Output binary cert to this file (default is stdout)\n",
        "   -o output-cert");
    FPS "%-20s Self sign\n",
        "   -x");
    FPS "%-20s Sign the certificate with RSA-PSS (the issuer key must be rsa)\n",
        "   --pss-sign");
    FPS "%-20s Cert serial number\n",
        "   -m serial-number");
    FPS "%-20s Time Warp\n",
        "   -w warp-months");
    FPS "%-20s Months valid (default is 3)\n",
        "   -v months-valid");
    FPS "%-20s Specify the password file\n",
        "   -f pwfile");
    FPS "%-20s Cert database directory (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "%-20s \n"
              "%-20s Specify the hash algorithm to use. Possible keywords:\n"
              "%-20s \"MD2\", \"MD4\", \"MD5\", \"SHA1\", \"SHA224\",\n"
              "%-20s \"SHA256\", \"SHA384\", \"SHA512\"\n",
        "   -Z hashAlg", "", "", "");
    FPS "%-20s \n"
              "%-20s Create key usage extension. Possible keywords:\n"
              "%-20s \"digitalSignature\", \"nonRepudiation\", \"keyEncipherment\",\n"
              "%-20s \"dataEncipherment\", \"keyAgreement\", \"certSigning\",\n"
              "%-20s \"crlSigning\", \"critical\"\n",
        "   -1 | --keyUsage keyword,keyword,...", "", "", "", "");
    FPS "%-20s Create basic constraint extension\n",
        "   -2 ");
    FPS "%-20s Create authority key ID extension\n",
        "   -3 ");
    FPS "%-20s Create crl distribution point extension\n",
        "   -4 ");
    FPS "%-20s \n"
              "%-20s Create netscape cert type extension. Possible keywords:\n"
              "%-20s \"sslClient\", \"sslServer\", \"smime\", \"objectSigning\",\n"
              "%-20s \"sslCA\", \"smimeCA\", \"objectSigningCA\", \"critical\".\n",
        "   -5 | --nsCertType keyword,keyword,... ", "", "", "");
    FPS "%-20s \n"
              "%-20s Create extended key usage extension. Possible keywords:\n"
              "%-20s \"serverAuth\", \"clientAuth\",\"codeSigning\",\n"
              "%-20s \"emailProtection\", \"timeStamp\",\"ocspResponder\",\n"
              "%-20s \"stepUp\", \"msTrustListSign\", \"x509Any\",\n"
              "%-20s \"ipsecIKE\", \"ipsecIKEEnd\", \"ipsecIKEIntermediate\",\n"
              "%-20s \"ipsecEnd\", \"ipsecTunnel\", \"ipsecUser\",\n"
              "%-20s \"critical\"\n",
        "   -6 | --extKeyUsage keyword,keyword,...", "", "", "", "", "", "", "");
    FPS "%-20s Create an email subject alt name extension\n",
        "   -7 emailAddrs");
    FPS "%-20s Create an dns subject alt name extension\n",
        "   -8 dnsNames");
    FPS "%-20s The input certificate request is encoded in ASCII (RFC1113)\n",
        "   -a");
    FPS "\n");
}

static void
luG(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "G"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Generate a new key pair\n",
        "-G");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s Name of token in which to generate key (default is internal)\n",
        "   -h token-name");
    FPS "%-20s Type of key pair to generate (\"dsa\", \"ec\", \"ed\",\n"
        "%-20s \"mldsa\", \"mlkem\", \"rsa\" (default))\n",
        "   -k key-type", "");
    FPS "%-20s Write the SubjectPublicKeyInfo of the new key to this file\n",
        "   -o spki-file");
    FPS "%-20s Write the SubjectPublicKeyInfo in PEM format\n",
        "   -a");
    FPS "%-20s Key size in bits, (min %d, max %d, default %d) (not for ec)\n",
        "   -g key-size", MIN_KEY_BITS, MAX_KEY_BITS, DEFAULT_KEY_BITS);
    FPS "%-20s Set the public exponent value (3, 17, 65537) (rsa only)\n",
        "   -y exp");
    FPS "%-20s Specify the password file\n",
        "   -f password-file");
    FPS "%-20s Specify the noise file to be used\n",
        "   -z noisefile");
    FPS "%-20s read PQG value from pqgfile (dsa only)\n",
        "   -q pqgfile");
    FPS "%-20s Elliptic curve name (ec only)\n",
        "   -q curve-name");
    FPS "%-20s One of nistp256, nistp384, nistp521, curve25519.\n", "");
    FPS "%-20s If a custom token is present, the following curves are also supported:\n", "");
    FPS "%-20s sect163k1, nistk163, sect163r1, sect163r2,\n", "");
    FPS "%-20s nistb163, sect193r1, sect193r2, sect233k1, nistk233,\n", "");
    FPS "%-20s sect233r1, nistb233, sect239k1, sect283k1, nistk283,\n", "");
    FPS "%-20s sect283r1, nistb283, sect409k1, nistk409, sect409r1,\n", "");
    FPS "%-20s nistb409, sect571k1, nistk571, sect571r1, nistb571,\n", "");
    FPS "%-20s secp160k1, secp160r1, secp160r2, secp192k1, secp192r1,\n", "");
    FPS "%-20s nistp192, secp224k1, secp224r1, nistp224, secp256k1,\n", "");
    FPS "%-20s secp256r1, secp384r1, secp521r1,\n", "");
    FPS "%-20s prime192v1, prime192v2, prime192v3, \n", "");
    FPS "%-20s prime239v1, prime239v2, prime239v3, c2pnb163v1, \n", "");
    FPS "%-20s c2pnb163v2, c2pnb163v3, c2pnb176v1, c2tnb191v1, \n", "");
    FPS "%-20s c2tnb191v2, c2tnb191v3,  \n", "");
    FPS "%-20s c2pnb208w1, c2tnb239v1, c2tnb239v2, c2tnb239v3, \n", "");
    FPS "%-20s c2pnb272w1, c2pnb304w1, \n", "");
    FPS "%-20s c2tnb359w1, c2pnb368w1, c2tnb431r1, secp112r1, \n", "");
    FPS "%-20s secp112r2, secp128r1, secp128r2, sect113r1, sect113r2\n", "");
    FPS "%-20s sect131r1, sect131r2\n", "");
    FPS "%-20s Edwards elliptic curve name (ed only)\n",
        "   -q curve-name");
    FPS "%-20s One of ed25519.\n", "");
    FPS "%-20s ML-DSA parameter set (mldsa only)\n",
        "   -q paramset");
    FPS "%-20s valid values are ml-dsa-44,  ml-dsa-65, ml-dsa-87:\n", "");
    FPS "%-20s ML-KEM parameter set (mlkem only)\n",
        "   -q paramset");
    FPS "%-20s valid values are ml-kem-512,  ml-kem-768, ml-kem-1024:\n", "");
    FPS "%-20s Key database directory (default is ~/.netscape)\n",
        "   -d keydir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "%-20s\n"
        "%-20s PKCS #11 key Attributes.\n",
        "   --keyAttrFlags attrflags", "");
    FPS "%-20s Comma separated list of key attribute attribute flags,\n", "");
    FPS "%-20s selected from the following list of choices:\n", "");
    FPS "%-20s {token | session} {public | private} {sensitive | insensitive}\n", "");
    FPS "%-20s {modifiable | unmodifiable} {extractable | unextractable}\n", "");
    FPS "%-20s\n",
        "   --keyOpFlagsOn opflags");
    FPS "%-20s\n"
        "%-20s PKCS #11 key Operation Flags.\n",
        "   --keyOpFlagsOff opflags", "");
    FPS "%-20s Comma separated list of one or more of the following:\n", "");
    FPS "%-20s encrypt, decrypt, sign, sign_recover, verify,\n", "");
    FPS "%-20s verify_recover, wrap, unwrap, derive\n", "");
    FPS "\n");
}

static void
luD(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "D"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Delete a certificate from the database\n",
        "-D");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s The nickname of the cert to delete\n",
        "   -n cert-name");
    FPS "%-20s Cert database directory (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "\n");
}

static void
luF(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "F"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Delete a key and associated certificate from the database\n",
        "-F");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s The nickname of the key to delete\n",
        "   -n cert-name");
    FPS "%-20s The key id of the key to delete, obtained using -K\n",
        "   -k key-id");
    FPS "%-20s Cert database directory (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "\n");
}

static void
luU(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "U"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s List all modules\n", /*, or print out a single named module\n",*/
        "-U");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s Module database directory (default is '~/.netscape')\n",
        "   -d moddir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "%-20s force the database to open R/W\n",
        "   -X");
    FPS "\n");
}

static void
luK(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "K"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s List all private keys\n",
        "-K");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s Name of token to search (\"all\" for all tokens)\n",
        "   -h token-name ");

    FPS "%-20s Key type (\"all\" (default), \"dsa\","
                                                    " \"ec\","
                                                    " \"ed\","
                                                    " \"mldsa\","
                                                    " \"mlkem\","
                                                    " \"rsa\")\n",
        "   -k key-type");
    FPS "%-20s The nickname of the key or associated certificate\n",
        "   -n name");
    FPS "%-20s Specify the password file\n",
        "   -f password-file");
    FPS "%-20s Key database directory (default is ~/.netscape)\n",
        "   -d keydir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "%-20s force the database to open R/W\n",
        "   -X");
    FPS "\n");
}

static void
luL(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "L"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s List all certs, or print out a single named cert (or a subset)\n",
        "-L");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s Name of token to search (\"all\" for all tokens)\n",
        "   -h token-name ");
    FPS "%-20s Pretty print named cert (list all if unspecified)\n",
        "   -n cert-name");
    FPS "%-20s \n"
              "%-20s Pretty print cert with email address (list all if unspecified)\n",
        "   --email email-address", "");
    FPS "%-20s Cert database directory (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "%-20s force the database to open R/W\n",
        "   -X");
    FPS "%-20s For single cert, print binary DER encoding\n",
        "   -r");
    FPS "%-20s For single cert, print ASCII encoding (RFC1113)\n",
        "   -a");
    FPS "%-20s \n"
              "%-20s For single cert, print binary DER encoding of extension OID\n",
        "   --dump-ext-val OID", "");
    FPS "\n");
}

static void
luM(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "M"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Modify trust attributes of certificate\n",
        "-M");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s The nickname of the cert to modify\n",
        "   -n cert-name");
    FPS "%-20s Set the certificate trust attributes (see -A above)\n",
        "   -t trustargs");
    FPS "%-20s Cert database directory (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "\n");
}

static void
luN(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "N"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Create a new certificate database\n",
        "-N");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s Cert database directory (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "%-20s Specify the password file\n",
        "   -f password-file");
    FPS "%-20s use empty password when creating a new database\n",
        "   --empty-password");
    FPS "\n");
}

static void
luT(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "T"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Reset the Key database or token\n",
        "-T");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s Cert database directory (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "%-20s Token to reset (default is internal)\n",
        "   -h token-name");
    FPS "%-20s Set token's Site Security Officer password\n",
        "   -0 SSO-password");
    FPS "\n");
}

static void
luO(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "O"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Print the chain of a certificate\n",
        "-O");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s The nickname of the cert to modify\n",
        "   -n cert-name");
    FPS "%-20s Cert database directory (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Input the certificate in ASCII (RFC1113); default is binary\n",
        "   -a");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "%-20s force the database to open R/W\n",
        "   -X");
    FPS "%-20s don't search for a chain if issuer name equals subject name\n",
        "   --simple-self-signed");
    FPS "\n");
}

static void
luR(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "R"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Generate a certificate request (stdout)\n",
        "-R");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s Specify the subject name (using RFC1485)\n",
        "   -s subject");
    FPS "%-20s Output the cert request to this file\n",
        "   -o output-req");
    FPS "%-20s Type of key pair to generate (\"dsa\", \"ec\", \"rsa\" (default))\n",
        "   -k key-type-or-id");
    FPS "%-20s or nickname of the cert key to use, or key id obtained using -K\n",
        "");
    FPS "%-20s Name of token in which to generate key (default is internal)\n",
        "   -h token-name");
    FPS "%-20s Key size in bits, RSA keys only (min %d, max %d, default %d)\n",
        "   -g key-size", MIN_KEY_BITS, MAX_KEY_BITS, DEFAULT_KEY_BITS);
    FPS "%-20s Create a certificate request restricted to RSA-PSS (rsa only)\n",
        "   --pss");
    FPS "%-20s Name of file containing PQG parameters (dsa only)\n",
        "   -q pqgfile");
    FPS "%-20s Elliptic curve name (ec only)\n",
        "   -q curve-name");
    FPS "%-20s See the \"-G\" option for a full list of supported names.\n",
        "");
    FPS "%-20s Edwards elliptic curve name (ed only)\n",
        "   -q curve-name");
    FPS "%-20s See the \"-G\" option for a full list of supported names.\n",
        "");
    FPS "%-20s ML-DSA parameter set (mldsa only)\n",
        "   -q paramset");
    FPS "%-20s See the \"-G\" option for a full list of supported names.\n",
        "");
    FPS "%-20s ML-KEM parameter set (mlkem only)\n",
        "   -q paramset");
    FPS "%-20s See the \"-G\" option for a full list of supported names.\n",
        "");
    FPS "%-20s Specify the password file\n",
        "   -f pwfile");
    FPS "%-20s Key database directory (default is ~/.netscape)\n",
        "   -d keydir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "%-20s Specify the contact phone number (\"123-456-7890\")\n",
        "   -p phone");
    FPS "%-20s \n"
              "%-20s Specify the hash algorithm to use. Possible keywords:\n"
              "%-20s \"MD2\", \"MD4\", \"MD5\", \"SHA1\", \"SHA224\",\n"
              "%-20s \"SHA256\", \"SHA384\", \"SHA512\"\n",
        "   -Z hashAlg", "", "", "");
    FPS "%-20s Output the cert request in ASCII (RFC1113); default is binary\n",
        "   -a");
    FPS "%-20s \n",
        "   See -S for available extension options");
    FPS "%-20s \n",
        "   See -G for available key flag options");
    FPS "\n");
}

static void
luV(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "V"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Validate a certificate\n",
        "-V");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s The nickname of the cert to Validate\n",
        "   -n cert-name");
    FPS "%-20s validity time (\"YYMMDDHHMMSS[+HHMM|-HHMM|Z]\")\n",
        "   -b time");
    FPS "%-20s Check certificate signature \n",
        "   -e ");
    FPS "%-20s Specify certificate usage:\n", "   -u certusage");
    FPS "%-25s C \t SSL Client\n", "");
    FPS "%-25s V \t SSL Server\n", "");
    FPS "%-25s I \t IPsec\n", "");
    FPS "%-25s L \t SSL CA\n", "");
    FPS "%-25s A \t Any CA\n", "");
    FPS "%-25s Y \t Verify CA\n", "");
    FPS "%-25s S \t Email signer\n", "");
    FPS "%-25s R \t Email Recipient\n", "");
    FPS "%-25s O \t OCSP status responder\n", "");
    FPS "%-25s J \t Object signer\n", "");
    FPS "%-20s Cert database directory (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Input the certificate in ASCII (RFC1113); default is binary\n",
        "   -a");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "%-20s force the database to open R/W\n",
        "   -X");
    FPS "\n");
}

static void
luW(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "W"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Change the key database password\n",
        "-W");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s cert and key database directory\n",
        "   -d certdir");
    FPS "%-20s Specify a file with the current password\n",
        "   -f pwfile");
    FPS "%-20s Specify a file with the new password in two lines\n",
        "   -@ newpwfile");
    FPS "\n");
}

static void
luRename(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "rename"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Change the database nickname of a certificate\n",
        "--rename");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s The old nickname of the cert to rename\n",
        "   -n cert-name");
    FPS "%-20s The new nickname of the cert to rename\n",
        "   --new-n new-name");
    FPS "%-20s Cert database directory (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "\n");
}

static void
luUpgradeMerge(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "upgrade-merge"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Upgrade an old database and merge it into a new one\n",
        "--upgrade-merge");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s Cert database directory to merge into (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Cert & Key database prefix of the target database\n",
        "   -P dbprefix");
    FPS "%-20s Specify the password file for the target database\n",
        "   -f pwfile");
    FPS "%-20s \n%-20s Cert database directory to upgrade from\n",
        "   --source-dir certdir", "");
    FPS "%-20s \n%-20s Cert & Key database prefix of the upgrade database\n",
        "   --source-prefix dbprefix", "");
    FPS "%-20s \n%-20s Unique identifier for the upgrade database\n",
        "   --upgrade-id uniqueID", "");
    FPS "%-20s \n%-20s Name of the token while it is in upgrade state\n",
        "   --upgrade-token-name name", "");
    FPS "%-20s Specify the password file for the upgrade database\n",
        "   -@ pwfile");
    FPS "\n");
}

static void
luMerge(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "merge"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Merge source database into the target database\n",
        "--merge");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s Cert database directory of target (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Cert & Key database prefix of the target database\n",
        "   -P dbprefix");
    FPS "%-20s Specify the password file for the target database\n",
        "   -f pwfile");
    FPS "%-20s \n%-20s Cert database directory of the source database\n",
        "   --source-dir certdir", "");
    FPS "%-20s \n%-20s Cert & Key database prefix of the source database\n",
        "   --source-prefix dbprefix", "");
    FPS "%-20s Specify the password file for the source database\n",
        "   -@ pwfile");
    FPS "\n");
}

static void
luS(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "S"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Make a certificate and add to database\n",
        "-S");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "%-20s Specify the nickname of the cert\n",
        "   -n key-name");
    FPS "%-20s Specify the subject name (using RFC1485)\n",
        "   -s subject");
    FPS "%-20s The nickname of the issuer cert\n",
        "   -c issuer-name");
    FPS "%-20s Set the certificate trust attributes (see -A above)\n",
        "   -t trustargs");
    FPS "%-20s Type of key pair to generate (\"dsa\", \"ec\", \"rsa\" (default))\n",
        "   -k key-type-or-id");
    FPS "%-20s Name of token in which to generate key (default is internal)\n",
        "   -h token-name");
    FPS "%-20s Key size in bits, RSA keys only (min %d, max %d, default %d)\n",
        "   -g key-size", MIN_KEY_BITS, MAX_KEY_BITS, DEFAULT_KEY_BITS);
    FPS "%-20s Create a certificate restricted to RSA-PSS (rsa only)\n",
        "   --pss");
    FPS "%-20s Name of file containing PQG parameters (dsa only)\n",
        "   -q pqgfile");
    FPS "%-20s Elliptic curve name (ec only)\n",
        "   -q curve-name");
    FPS "%-20s See the \"-G\" option for a full list of supported names.\n",
        "");
    FPS "%-20s Edwards elliptic curve name (ed only)\n",
        "   -q curve-name");
    FPS "%-20s See the \"-G\" option for a full list of supported names.\n",
        "");
    FPS "%-20s ML-DSA parameter set (mldsa only)\n",
        "   -q paramset");
    FPS "%-20s See the \"-G\" option for a full list of supported names.\n",
        "");
    FPS "%-20s ML-KEM parameter set (mlkem only)\n",
        "   -q paramset");
    FPS "%-20s See the \"-G\" option for a full list of supported names.\n",
        "");
    FPS "%-20s Self sign\n",
        "   -x");
    FPS "%-20s Sign the certificate with RSA-PSS (the issuer key must be rsa)\n",
        "   --pss-sign");
    FPS "%-20s Cert serial number\n",
        "   -m serial-number");
    FPS "%-20s Time Warp\n",
        "   -w warp-months");
    FPS "%-20s Months valid (default is 3)\n",
        "   -v months-valid");
    FPS "%-20s Specify the password file\n",
        "   -f pwfile");
    FPS "%-20s Cert database directory (default is ~/.netscape)\n",
        "   -d certdir");
    FPS "%-20s Cert & Key database prefix\n",
        "   -P dbprefix");
    FPS "%-20s Specify the contact phone number (\"123-456-7890\")\n",
        "   -p phone");
    FPS "%-20s \n"
              "%-20s Specify the hash algorithm to use. Possible keywords:\n"
              "%-20s \"MD2\", \"MD4\", \"MD5\", \"SHA1\", \"SHA224\",\n"
              "%-20s \"SHA256\", \"SHA384\", \"SHA512\"\n",
        "   -Z hashAlg", "", "", "");
    FPS "%-20s Create key usage extension\n",
        "   -1 ");
    FPS "%-20s Create basic constraint extension\n",
        "   -2 ");
    FPS "%-20s Create authority key ID extension\n",
        "   -3 ");
    FPS "%-20s Create crl distribution point extension\n",
        "   -4 ");
    FPS "%-20s Create netscape cert type extension\n",
        "   -5 ");
    FPS "%-20s Create extended key usage extension\n",
        "   -6 ");
    FPS "%-20s Create an email subject alt name extension\n",
        "   -7 emailAddrs ");
    FPS "%-20s Create a DNS subject alt name extension\n",
        "   -8 DNS-names");
    FPS "%-20s Create an Authority Information Access extension\n",
        "   --extAIA ");
    FPS "%-20s Create a Subject Information Access extension\n",
        "   --extSIA ");
    FPS "%-20s Create a Certificate Policies extension\n",
        "   --extCP ");
    FPS "%-20s Create a Policy Mappings extension\n",
        "   --extPM ");
    FPS "%-20s Create a Policy Constraints extension\n",
        "   --extPC ");
    FPS "%-20s Create an Inhibit Any Policy extension\n",
        "   --extIA ");
    FPS "%-20s Create a subject key ID extension\n",
        "   --extSKID ");
    FPS "%-20s \n",
        "   See -G for available key flag options");
    FPS "%-20s Create a name constraints extension\n",
        "   --extNC ");
    FPS "%-20s \n"
        "%-20s Create a Subject Alt Name extension with one or multiple names\n",
        "   --extSAN type:name[,type:name]...", "");
    FPS "%-20s - type: directory, dn, dns, edi, ediparty, email, ip, ipaddr,\n", "");
    FPS "%-20s         other, registerid, rfc822, uri, x400, x400addr\n", "");
    FPS "%-20s \n"
        "%-20s Add one or multiple extensions that certutil cannot encode yet,\n"
        "%-20s by loading their encodings from external files.\n",
        "   --extGeneric OID:critical-flag:filename[,OID:critical-flag:filename]...", "", "");
    FPS "%-20s - OID (example): 1.2.3.4\n", "");
    FPS "%-20s - critical-flag: critical or not-critical\n", "");
    FPS "%-20s - filename: full path to a file containing an encoded extension\n", "");
    FPS "\n");
}

static void
luBuildFlags(enum usage_level ul, const char *command)
{
    int is_my_command = (command && 0 == strcmp(command, "build-flags"));
    if (ul == usage_all || !command || is_my_command)
    FPS "%-15s Print enabled build flags relevant for NSS test execution\n",
        "--build-flags");
    if (ul == usage_selected && !is_my_command)
        return;
    FPS "\n");
}

static void
LongUsage(enum usage_level ul, const char *command)
{
    luA(ul, command);
    luB(ul, command);
    luE(ul, command);
    luC(ul, command);
    luG(ul, command);
    luD(ul, command);
    luRename(ul, command);
    luF(ul, command);
    luU(ul, command);
    luK(ul, command);
    luL(ul, command);
    luBuildFlags(ul, command);
    luM(ul, command);
    luN(ul, command);
    luT(ul, command);
    luO(ul, command);
    luR(ul, command);
    luV(ul, command);
    luW(ul, command);
    luUpgradeMerge(ul, command);
    luMerge(ul, command);
    luS(ul, command);
#undef FPS
}

static void
Usage()
{
    PR_fprintf(PR_STDERR,
               "%s - Utility to manipulate NSS certificate databases\n\n"
               "Usage:  %s <command> -d <database-directory> <options>\n\n"
               "Valid commands:\n",
               progName, progName);
    LongUsage(usage_selected, NULL);
    PR_fprintf(PR_STDERR, "\n"
                          "%s -H <command> : Print available options for the given command\n"
                          "%s -H : Print complete help output of all commands and options\n"
                          "%s --syntax : Print a short summary of all commands and options\n",
               progName, progName, progName);
    exit(1);
}

static CERTCertificate *
MakeV1Cert(CERTCertDBHandle *handle,
           CERTCertificateRequest *req,
           char *issuerNickName,
           PRBool selfsign,
           unsigned int serialNumber,
           int warpmonths,
           int validityMonths)
{
    CERTCertificate *issuerCert = NULL;
    CERTValidity *validity;
    CERTCertificate *cert = NULL;
    PRExplodedTime printableTime;
    PRTime now, after;

    if (!selfsign) {
        issuerCert = CERT_FindCertByNicknameOrEmailAddr(handle, issuerNickName);
        if (!issuerCert) {
            SECU_PrintError(progName, "could not find certificate named \"%s\"",
                            issuerNickName);
            return NULL;
        }
    }

    now = PR_Now();
    PR_ExplodeTime(now, PR_GMTParameters, &printableTime);
    if (warpmonths) {
        printableTime.tm_month += warpmonths;
        now = PR_ImplodeTime(&printableTime);
        PR_ExplodeTime(now, PR_GMTParameters, &printableTime);
    }
    printableTime.tm_month += validityMonths;
    after = PR_ImplodeTime(&printableTime);

    /* note that the time is now in micro-second unit */
    validity = CERT_CreateValidity(now, after);
    if (validity) {
        cert = CERT_CreateCertificate(serialNumber,
                                      (selfsign ? &req->subject
                                                : &issuerCert->subject),
                                      validity, req);

        CERT_DestroyValidity(validity);
    }
    if (issuerCert) {
        CERT_DestroyCertificate(issuerCert);
    }

    return (cert);
}

static SECStatus
SetSignatureAlgorithm(PLArenaPool *arena,
                      SECAlgorithmID *signAlg,
                      SECAlgorithmID *spkiAlg,
                      SECOidTag hashAlgTag,
                      SECKEYPrivateKey *privKey,
                      PRBool pssSign)
{
    SECOidTag signAlgTag = SEC_OID_UNKNOWN;
    SECItem *params = NULL;

    if (pssSign) {
        signAlgTag = SEC_OID_PKCS1_RSA_PSS_SIGNATURE;
    }
    if (SECOID_GetAlgorithmTag(spkiAlg) == SEC_OID_PKCS1_RSA_PSS_SIGNATURE) {
        signAlgTag = SEC_OID_PKCS1_RSA_PSS_SIGNATURE;
        params = &spkiAlg->parameters;
    }
    return SEC_CreateSignatureAlgorithmID(arena, signAlg, signAlgTag,
                                          hashAlgTag, params, privKey, NULL);
}

static SECStatus
SignCert(CERTCertDBHandle *handle, CERTCertificate *cert, PRBool selfsign,
         SECOidTag hashAlgTag,
         SECKEYPrivateKey *privKey, char *issuerNickName,
         int certVersion, PRBool pssSign, void *pwarg)
{
    SECItem der;
    SECKEYPrivateKey *caPrivateKey = NULL;
    SECStatus rv;
    PLArenaPool *arena;
    CERTCertificate *issuer;
    void *dummy;

    arena = cert->arena;

    if (selfsign) {
        issuer = cert;
    } else {
        issuer = PK11_FindCertFromNickname(issuerNickName, pwarg);
        if ((CERTCertificate *)NULL == issuer) {
            SECU_PrintError(progName, "unable to find issuer with nickname %s",
                            issuerNickName);
            rv = SECFailure;
            goto done;
        }
        privKey = caPrivateKey = PK11_FindKeyByAnyCert(issuer, pwarg);
        if (caPrivateKey == NULL) {
            SECU_PrintError(progName, "unable to retrieve key %s", issuerNickName);
            rv = SECFailure;
            CERT_DestroyCertificate(issuer);
            goto done;
        }
    }

    if (pssSign &&
        (SECKEY_GetPrivateKeyType(privKey) != rsaKey &&
         SECKEY_GetPrivateKeyType(privKey) != rsaPssKey)) {
        SECU_PrintError(progName, "unable to create RSA-PSS signature with key %s",
                        issuerNickName);
        rv = SECFailure;
        if (!selfsign) {
            CERT_DestroyCertificate(issuer);
        }
        goto done;
    }

    rv = SetSignatureAlgorithm(arena,
                               &cert->signature,
                               &issuer->subjectPublicKeyInfo.algorithm,
                               hashAlgTag,
                               privKey,
                               pssSign);
    if (!selfsign) {
        CERT_DestroyCertificate(issuer);
    }
    if (rv != SECSuccess) {
        goto done;
    }

    switch (certVersion) {
        case (SEC_CERTIFICATE_VERSION_1):
            /* The initial version for x509 certificates is version one
             * and this default value must be an implicit DER encoding. */
            cert->version.data = NULL;
            cert->version.len = 0;
            break;
        case (SEC_CERTIFICATE_VERSION_2):
        case (SEC_CERTIFICATE_VERSION_3):
        case 3: /* unspecified format (would be version 4 certificate). */
            *(cert->version.data) = certVersion;
            cert->version.len = 1;
            break;
        default:
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            rv = SECFailure;
            goto done;
    }

    der.len = 0;
    der.data = NULL;
    dummy = SEC_ASN1EncodeItem(arena, &der, cert,
                               SEC_ASN1_GET(CERT_CertificateTemplate));
    if (!dummy) {
        fprintf(stderr, "Could not encode certificate.\n");
        rv = SECFailure;
        goto done;
    }

    rv = SEC_DerSignDataWithAlgorithmID(arena, &cert->derCert, der.data, der.len,
                                        privKey, &cert->signature);
    if (rv != SECSuccess) {
        fprintf(stderr, "Could not sign encoded certificate data.\n");
        /* result allocated out of the arena, it will be freed
         * when the arena is freed */
        goto done;
    }
done:
    if (caPrivateKey) {
        SECKEY_DestroyPrivateKey(caPrivateKey);
    }
    return rv;
}

static SECStatus
CreateCert(
    CERTCertDBHandle *handle,
    PK11SlotInfo *slot,
    char *issuerNickName,
    const SECItem *certReqDER,
    CERTName *spkiSubject,
    SECKEYPrivateKey **selfsignprivkey,
    void *pwarg,
    SECOidTag hashAlgTag,
    unsigned int serialNumber,
    int warpmonths,
    int validityMonths,
    const char *emailAddrs,
    const char *dnsNames,
    PRBool ascii,
    PRBool selfsign,
    certutilExtnList extnList,
    const char *extGeneric,
    int certVersion,
    PRBool pssSign,
    SECItem *certDER)
{
    void *extHandle = NULL;
    CERTCertificate *subjectCert = NULL;
    CERTCertificateRequest *certReq = NULL;
    SECStatus rv = SECSuccess;
    CERTCertExtension **CRexts;

    do {
        /* Create a certrequest object from the input cert request der, or,
         * if a subject was given, from a bare SubjectPublicKeyInfo. */
        if (spkiSubject) {
            certReq = MakeCertRequestFromKEMSPKI(certReqDER, spkiSubject);
        } else {
            certReq = GetCertRequest(certReqDER, pwarg);
        }
        if (certReq == NULL) {
            GEN_BREAK(SECFailure)
        }

        subjectCert = MakeV1Cert(handle, certReq, issuerNickName, selfsign,
                                 serialNumber, warpmonths, validityMonths);
        if (subjectCert == NULL) {
            GEN_BREAK(SECFailure)
        }

        extHandle = CERT_StartCertExtensions(subjectCert);
        if (extHandle == NULL) {
            GEN_BREAK(SECFailure)
        }

        rv = AddExtensions(extHandle, emailAddrs, dnsNames, extnList, extGeneric);
        if (rv != SECSuccess) {
            GEN_BREAK(SECFailure)
        }

        if (certReq->attributes != NULL &&
            certReq->attributes[0] != NULL &&
            certReq->attributes[0]->attrType.data != NULL &&
            certReq->attributes[0]->attrType.len > 0 &&
            SECOID_FindOIDTag(&certReq->attributes[0]->attrType) ==
                SEC_OID_PKCS9_EXTENSION_REQUEST) {
            rv = CERT_GetCertificateRequestExtensions(certReq, &CRexts);
            if (rv != SECSuccess)
                break;
            rv = CERT_MergeExtensions(extHandle, CRexts);
            if (rv != SECSuccess)
                break;
        }

        CERT_FinishExtensions(extHandle);
        extHandle = NULL;

        /* self-signing a cert request, find the private key */
        if (selfsign && *selfsignprivkey == NULL) {
            *selfsignprivkey = PK11_FindKeyByDERCert(slot, subjectCert, pwarg);
            if (!*selfsignprivkey) {
                fprintf(stderr, "Failed to locate private key.\n");
                rv = SECFailure;
                break;
            }
        }

        rv = SignCert(handle, subjectCert, selfsign, hashAlgTag,
                      *selfsignprivkey, issuerNickName,
                      certVersion, pssSign, pwarg);
        if (rv != SECSuccess)
            break;

        rv = SECFailure;
        if (ascii) {
            char *asciiDER = BTOA_DataToAscii(subjectCert->derCert.data,
                                              subjectCert->derCert.len);
            if (asciiDER) {
                char *wrapped = PR_smprintf("%s\n%s\n%s\n",
                                            NS_CERT_HEADER,
                                            asciiDER,
                                            NS_CERT_TRAILER);
                if (wrapped) {
                    PRUint32 wrappedLen = PL_strlen(wrapped);
                    if (SECITEM_AllocItem(NULL, certDER, wrappedLen)) {
                        PORT_Memcpy(certDER->data, wrapped, wrappedLen);
                        rv = SECSuccess;
                    }
                    PR_smprintf_free(wrapped);
                }
                PORT_Free(asciiDER);
            }
        } else {
            rv = SECITEM_CopyItem(NULL, certDER, &subjectCert->derCert);
        }
    } while (0);
    if (extHandle) {
        CERT_FinishExtensions(extHandle);
    }
    CERT_DestroyCertificateRequest(certReq);
    CERT_DestroyCertificate(subjectCert);
    if (rv != SECSuccess) {
        PRErrorCode perr = PR_GetError();
        fprintf(stderr, "%s: unable to create cert (%s)\n", progName,
                SECU_Strerror(perr));
    }
    return (rv);
}

/*
 * map a class to a user presentable string
 */
static const char *objClassArray[] = {
    "Data",
    "Certificate",
    "Public Key",
    "Private Key",
    "Secret Key",
    "Hardware Feature",
    "Domain Parameters",
    "Mechanism"
};

static const char *objNSSClassArray[] = {
    "CKO_NSS",
    "Crl",
    "SMIME Record",
    "Trust",
    "Builtin Root List"
};

const char *
getObjectClass(CK_ULONG classType)
{
    static char buf[sizeof(CK_ULONG) * 2 + 3];

    if (classType <= CKO_MECHANISM) {
        return objClassArray[classType];
    }
    if (classType >= CKO_NSS && classType <= CKO_NSS_BUILTIN_ROOT_LIST) {
        return objNSSClassArray[classType - CKO_NSS];
    }
    snprintf(buf, sizeof(buf), "0x%lx", classType);
    return buf;
}

typedef struct {
    char *name;
    int nameSize;
    CK_ULONG value;
} flagArray;

#define NAME_SIZE(x) #x, sizeof(#x) - 1

flagArray opFlagsArray[] = {
    { NAME_SIZE(encrypt), CKF_ENCRYPT },
    { NAME_SIZE(decrypt), CKF_DECRYPT },
    { NAME_SIZE(sign), CKF_SIGN },
    { NAME_SIZE(sign_recover), CKF_SIGN_RECOVER },
    { NAME_SIZE(verify), CKF_VERIFY },
    { NAME_SIZE(verify_recover), CKF_VERIFY_RECOVER },
    { NAME_SIZE(wrap), CKF_WRAP },
    { NAME_SIZE(unwrap), CKF_UNWRAP },
    { NAME_SIZE(derive), CKF_DERIVE }
};

int opFlagsCount = PR_ARRAY_SIZE(opFlagsArray);

flagArray attrFlagsArray[] = {
    { NAME_SIZE(token), PK11_ATTR_TOKEN },
    { NAME_SIZE(session), PK11_ATTR_SESSION },
    { NAME_SIZE(private), PK11_ATTR_PRIVATE },
    { NAME_SIZE(public), PK11_ATTR_PUBLIC },
    { NAME_SIZE(modifiable), PK11_ATTR_MODIFIABLE },
    { NAME_SIZE(unmodifiable), PK11_ATTR_UNMODIFIABLE },
    { NAME_SIZE(sensitive), PK11_ATTR_SENSITIVE },
    { NAME_SIZE(insensitive), PK11_ATTR_INSENSITIVE },
    { NAME_SIZE(extractable), PK11_ATTR_EXTRACTABLE },
    { NAME_SIZE(unextractable), PK11_ATTR_UNEXTRACTABLE }
};

int attrFlagsCount = PR_ARRAY_SIZE(attrFlagsArray);

#define MAX_STRING 30
CK_ULONG
GetFlags(char *flagsString, flagArray *flags, int count)
{
    CK_ULONG flagsValue = strtol(flagsString, NULL, 0);
    int i;

    if ((flagsValue != 0) || (*flagsString == 0)) {
        return flagsValue;
    }
    while (*flagsString) {
        for (i = 0; i < count; i++) {
            if (strncmp(flagsString, flags[i].name, flags[i].nameSize) ==
                0) {
                flagsValue |= flags[i].value;
                flagsString += flags[i].nameSize;
                if (*flagsString != 0) {
                    flagsString++;
                }
                break;
            }
        }
        if (i == count) {
            char name[MAX_STRING];
            char *tok;

            strncpy(name, flagsString, MAX_STRING);
            name[MAX_STRING - 1] = 0;
            tok = strchr(name, ',');
            if (tok) {
                *tok = 0;
            }
            fprintf(stderr, "Unknown flag (%s)\n", name);
            tok = strchr(flagsString, ',');
            if (tok == NULL) {
                break;
            }
            flagsString = tok + 1;
        }
    }
    return flagsValue;
}

CK_FLAGS
GetOpFlags(char *flags)
{
    return GetFlags(flags, opFlagsArray, opFlagsCount);
}

PK11AttrFlags
GetAttrFlags(char *flags)
{
    return GetFlags(flags, attrFlagsArray, attrFlagsCount);
}

char *
mkNickname(unsigned char *data, int len)
{
    char *nick = PORT_Alloc(len + 1);
    if (!nick) {
        return nick;
    }
    PORT_Memcpy(nick, data, len);
    nick[len] = 0;
    return nick;
}

/*
 * dump a PK11_MergeTokens error log to the console
 */
void
DumpMergeLog(const char *progname, PK11MergeLog *log)
{
    PK11MergeLogNode *node;

    for (node = log->head; node; node = node->next) {
        SECItem attrItem;
        char *nickname = NULL;
        const char *objectClass = NULL;
        SECStatus rv;

        attrItem.data = NULL;
        rv = PK11_ReadRawAttribute(PK11_TypeGeneric, node->object,
                                   CKA_LABEL, &attrItem);
        if (rv == SECSuccess) {
            nickname = mkNickname(attrItem.data, attrItem.len);
            PORT_Free(attrItem.data);
        }
        attrItem.data = NULL;
        rv = PK11_ReadRawAttribute(PK11_TypeGeneric, node->object,
                                   CKA_CLASS, &attrItem);
        if (rv == SECSuccess) {
            if (attrItem.len == sizeof(CK_ULONG)) {
                objectClass = getObjectClass(*(CK_ULONG *)attrItem.data);
            }
            PORT_Free(attrItem.data);
        }

        fprintf(stderr, "%s: Could not merge object %s (type %s): %s\n",
                progName,
                nickname ? nickname : "unnamed",
                objectClass ? objectClass : "unknown",
                SECU_Strerror(node->error));

        if (nickname) {
            PORT_Free(nickname);
        }
    }
}

/*  Certutil commands  */
enum {
    cmd_AddCert = 0,
    cmd_CreateNewCert,
    cmd_DeleteCert,
    cmd_AddEmailCert,
    cmd_DeleteKey,
    cmd_GenKeyPair,
    cmd_PrintHelp,
    cmd_PrintSyntax,
    cmd_ListKeys,
    cmd_ListCerts,
    cmd_ModifyCertTrust,
    cmd_NewDBs,
    cmd_DumpChain,
    cmd_CertReq,
    cmd_CreateAndAddCert,
    cmd_TokenReset,
    cmd_ListModules,
    cmd_CheckCertValidity,
    cmd_ChangePassword,
    cmd_Version,
    cmd_Batch,
    cmd_Merge,
    cmd_UpgradeMerge, /* test only */
    cmd_Rename,
    cmd_BuildFlags,
    max_cmd
};

/*  Certutil options */
enum certutilOpts {
    opt_SSOPass = 0,
    opt_AddKeyUsageExt,
    opt_AddBasicConstraintExt,
    opt_AddAuthorityKeyIDExt,
    opt_AddCRLDistPtsExt,
    opt_AddNSCertTypeExt,
    opt_AddExtKeyUsageExt,
    opt_ExtendedEmailAddrs,
    opt_ExtendedDNSNames,
    opt_ASCIIForIO,
    opt_ValidityTime,
    opt_IssuerName,
    opt_CertDir,
    opt_VerifySig,
    opt_PasswordFile,
    opt_KeySize,
    opt_TokenName,
    opt_InputFile,
    opt_Emailaddress,
    opt_KeyIndex,
    opt_KeyType,
    opt_DetailedInfo,
    opt_SerialNumber,
    opt_Nickname,
    opt_OutputFile,
    opt_PhoneNumber,
    opt_DBPrefix,
    opt_PQGFile,
    opt_BinaryDER,
    opt_Subject,
    opt_Trust,
    opt_Usage,
    opt_Validity,
    opt_OffsetMonths,
    opt_SelfSign,
    opt_RW,
    opt_Exponent,
    opt_NoiseFile,
    opt_Hash,
    opt_NewPasswordFile,
    opt_AddAuthInfoAccExt,
    opt_AddSubjInfoAccExt,
    opt_AddCertPoliciesExt,
    opt_AddPolicyMapExt,
    opt_AddPolicyConstrExt,
    opt_AddInhibAnyExt,
    opt_AddNameConstraintsExt,
    opt_AddSubjectKeyIDExt,
    opt_AddCmdKeyUsageExt,
    opt_AddCmdNSCertTypeExt,
    opt_AddCmdExtKeyUsageExt,
    opt_SourceDir,
    opt_SourcePrefix,
    opt_UpgradeID,
    opt_UpgradeTokenName,
    opt_KeyOpFlagsOn,
    opt_KeyOpFlagsOff,
    opt_KeyAttrFlags,
    opt_EmptyPassword,
    opt_CertVersion,
    opt_AddSubjectAltNameExt,
    opt_DumpExtensionValue,
    opt_GenericExtensions,
    opt_NewNickname,
    opt_Pss,
    opt_PssSign,
    opt_SimpleSelfSigned,
    opt_KEMPubKey,
    opt_Help
};

static const secuCommandFlag commands_init[] = {
    { /* cmd_AddCert             */ 'A', PR_FALSE, 0, PR_FALSE },
    { /* cmd_CreateNewCert       */ 'C', PR_FALSE, 0, PR_FALSE },
    { /* cmd_DeleteCert          */ 'D', PR_FALSE, 0, PR_FALSE },
    { /* cmd_AddEmailCert        */ 'E', PR_FALSE, 0, PR_FALSE },
    { /* cmd_DeleteKey           */ 'F', PR_FALSE, 0, PR_FALSE },
    { /* cmd_GenKeyPair          */ 'G', PR_FALSE, 0, PR_FALSE },
    { /* cmd_PrintHelp           */ 'H', PR_FALSE, 0, PR_FALSE, "help" },
    { /* cmd_PrintSyntax         */ 0, PR_FALSE, 0, PR_FALSE,
      "syntax" },
    { /* cmd_ListKeys            */ 'K', PR_FALSE, 0, PR_FALSE },
    { /* cmd_ListCerts           */ 'L', PR_FALSE, 0, PR_FALSE },
    { /* cmd_ModifyCertTrust     */ 'M', PR_FALSE, 0, PR_FALSE },
    { /* cmd_NewDBs              */ 'N', PR_FALSE, 0, PR_FALSE },
    { /* cmd_DumpChain           */ 'O', PR_FALSE, 0, PR_FALSE },
    { /* cmd_CertReq             */ 'R', PR_FALSE, 0, PR_FALSE },
    { /* cmd_CreateAndAddCert    */ 'S', PR_FALSE, 0, PR_FALSE },
    { /* cmd_TokenReset          */ 'T', PR_FALSE, 0, PR_FALSE },
    { /* cmd_ListModules         */ 'U', PR_FALSE, 0, PR_FALSE },
    { /* cmd_CheckCertValidity   */ 'V', PR_FALSE, 0, PR_FALSE },
    { /* cmd_ChangePassword      */ 'W', PR_FALSE, 0, PR_FALSE },
    { /* cmd_Version             */ 'Y', PR_FALSE, 0, PR_FALSE },
    { /* cmd_Batch               */ 'B', PR_FALSE, 0, PR_FALSE },
    { /* cmd_Merge               */ 0, PR_FALSE, 0, PR_FALSE, "merge" },
    { /* cmd_UpgradeMerge        */ 0, PR_FALSE, 0, PR_FALSE,
      "upgrade-merge" },
    { /* cmd_Rename              */ 0, PR_FALSE, 0, PR_FALSE,
      "rename" },
    { /* cmd_BuildFlags          */ 0, PR_FALSE, 0, PR_FALSE,
      "build-flags" }
};
#define NUM_COMMANDS ((sizeof commands_init) / (sizeof commands_init[0]))

static const secuCommandFlag options_init[] = {
    { /* opt_SSOPass             */ '0', PR_TRUE, 0, PR_FALSE },
    { /* opt_AddKeyUsageExt      */ '1', PR_FALSE, 0, PR_FALSE },
    { /* opt_AddBasicConstraintExt*/ '2', PR_FALSE, 0, PR_FALSE },
    { /* opt_AddAuthorityKeyIDExt*/ '3', PR_FALSE, 0, PR_FALSE },
    { /* opt_AddCRLDistPtsExt    */ '4', PR_FALSE, 0, PR_FALSE },
    { /* opt_AddNSCertTypeExt    */ '5', PR_FALSE, 0, PR_FALSE },
    { /* opt_AddExtKeyUsageExt   */ '6', PR_FALSE, 0, PR_FALSE },
    { /* opt_ExtendedEmailAddrs  */ '7', PR_TRUE, 0, PR_FALSE },
    { /* opt_ExtendedDNSNames    */ '8', PR_TRUE, 0, PR_FALSE },
    { /* opt_ASCIIForIO          */ 'a', PR_FALSE, 0, PR_FALSE },
    { /* opt_ValidityTime        */ 'b', PR_TRUE, 0, PR_FALSE },
    { /* opt_IssuerName          */ 'c', PR_TRUE, 0, PR_FALSE },
    { /* opt_CertDir             */ 'd', PR_TRUE, 0, PR_FALSE },
    { /* opt_VerifySig           */ 'e', PR_FALSE, 0, PR_FALSE },
    { /* opt_PasswordFile        */ 'f', PR_TRUE, 0, PR_FALSE },
    { /* opt_KeySize             */ 'g', PR_TRUE, 0, PR_FALSE },
    { /* opt_TokenName           */ 'h', PR_TRUE, 0, PR_FALSE },
    { /* opt_InputFile           */ 'i', PR_TRUE, 0, PR_FALSE },
    { /* opt_Emailaddress        */ 0, PR_TRUE, 0, PR_FALSE, "email" },
    { /* opt_KeyIndex            */ 'j', PR_TRUE, 0, PR_FALSE },
    { /* opt_KeyType             */ 'k', PR_TRUE, 0, PR_FALSE },
    { /* opt_DetailedInfo        */ 'l', PR_FALSE, 0, PR_FALSE },
    { /* opt_SerialNumber        */ 'm', PR_TRUE, 0, PR_FALSE },
    { /* opt_Nickname            */ 'n', PR_TRUE, 0, PR_FALSE },
    { /* opt_OutputFile          */ 'o', PR_TRUE, 0, PR_FALSE },
    { /* opt_PhoneNumber         */ 'p', PR_TRUE, 0, PR_FALSE },
    { /* opt_DBPrefix            */ 'P', PR_TRUE, 0, PR_FALSE },
    { /* opt_PQGFile             */ 'q', PR_TRUE, 0, PR_FALSE },
    { /* opt_BinaryDER           */ 'r', PR_FALSE, 0, PR_FALSE },
    { /* opt_Subject             */ 's', PR_TRUE, 0, PR_FALSE },
    { /* opt_Trust               */ 't', PR_TRUE, 0, PR_FALSE },
    { /* opt_Usage               */ 'u', PR_TRUE, 0, PR_FALSE },
    { /* opt_Validity            */ 'v', PR_TRUE, 0, PR_FALSE },
    { /* opt_OffsetMonths        */ 'w', PR_TRUE, 0, PR_FALSE },
    { /* opt_SelfSign            */ 'x', PR_FALSE, 0, PR_FALSE },
    { /* opt_RW                  */ 'X', PR_FALSE, 0, PR_FALSE },
    { /* opt_Exponent            */ 'y', PR_TRUE, 0, PR_FALSE },
    { /* opt_NoiseFile           */ 'z', PR_TRUE, 0, PR_FALSE },
    { /* opt_Hash                */ 'Z', PR_TRUE, 0, PR_FALSE },
    { /* opt_NewPasswordFile     */ '@', PR_TRUE, 0, PR_FALSE },
    { /* opt_AddAuthInfoAccExt   */ 0, PR_FALSE, 0, PR_FALSE, "extAIA" },
    { /* opt_AddSubjInfoAccExt   */ 0, PR_FALSE, 0, PR_FALSE, "extSIA" },
    { /* opt_AddCertPoliciesExt  */ 0, PR_FALSE, 0, PR_FALSE, "extCP" },
    { /* opt_AddPolicyMapExt     */ 0, PR_FALSE, 0, PR_FALSE, "extPM" },
    { /* opt_AddPolicyConstrExt  */ 0, PR_FALSE, 0, PR_FALSE, "extPC" },
    { /* opt_AddInhibAnyExt      */ 0, PR_FALSE, 0, PR_FALSE, "extIA" },
    { /* opt_AddNameConstraintsExt*/ 0, PR_FALSE, 0, PR_FALSE, "extNC" },
    { /* opt_AddSubjectKeyIDExt  */ 0, PR_FALSE, 0, PR_FALSE,
      "extSKID" },
    { /* opt_AddCmdKeyUsageExt   */ 0, PR_TRUE, 0, PR_FALSE,
      "keyUsage" },
    { /* opt_AddCmdNSCertTypeExt */ 0, PR_TRUE, 0, PR_FALSE,
      "nsCertType" },
    { /* opt_AddCmdExtKeyUsageExt*/ 0, PR_TRUE, 0, PR_FALSE,
      "extKeyUsage" },

    { /* opt_SourceDir           */ 0, PR_TRUE, 0, PR_FALSE,
      "source-dir" },
    { /* opt_SourcePrefix        */ 0, PR_TRUE, 0, PR_FALSE,
      "source-prefix" },
    { /* opt_UpgradeID           */ 0, PR_TRUE, 0, PR_FALSE,
      "upgrade-id" },
    { /* opt_UpgradeTokenName    */ 0, PR_TRUE, 0, PR_FALSE,
      "upgrade-token-name" },
    { /* opt_KeyOpFlagsOn        */ 0, PR_TRUE, 0, PR_FALSE,
      "keyOpFlagsOn" },
    { /* opt_KeyOpFlagsOff       */ 0, PR_TRUE, 0, PR_FALSE,
      "keyOpFlagsOff" },
    { /* opt_KeyAttrFlags        */ 0, PR_TRUE, 0, PR_FALSE,
      "keyAttrFlags" },
    { /* opt_EmptyPassword       */ 0, PR_FALSE, 0, PR_FALSE,
      "empty-password" },
    { /* opt_CertVersion         */ 0, PR_TRUE, 0, PR_FALSE,
      "certVersion" },
    { /* opt_AddSubjectAltExt    */ 0, PR_TRUE, 0, PR_FALSE, "extSAN" },
    { /* opt_DumpExtensionValue  */ 0, PR_TRUE, 0, PR_FALSE,
      "dump-ext-val" },
    { /* opt_GenericExtensions   */ 0, PR_TRUE, 0, PR_FALSE,
      "extGeneric" },
    { /* opt_NewNickname         */ 0, PR_TRUE, 0, PR_FALSE,
      "new-n" },
    { /* opt_Pss                 */ 0, PR_FALSE, 0, PR_FALSE,
      "pss" },
    { /* opt_PssSign             */ 0, PR_FALSE, 0, PR_FALSE,
      "pss-sign" },
    { /* opt_SimpleSelfSigned    */ 0, PR_FALSE, 0, PR_FALSE,
      "simple-self-signed" },
    { /* opt_KEMPubKey           */ 0, PR_TRUE, 0, PR_FALSE,
      "kem-pubkey" },
};
#define NUM_OPTIONS ((sizeof options_init) / (sizeof options_init[0]))

static secuCommandFlag certutil_commands[NUM_COMMANDS];
static secuCommandFlag certutil_options[NUM_OPTIONS];

static const secuCommand certutil = {
    NUM_COMMANDS,
    NUM_OPTIONS,
    certutil_commands,
    certutil_options
};

static certutilExtnList certutil_extns;

static int
certutil_main(int argc, char **argv, PRBool initialize)
{
    CERTCertDBHandle *certHandle;
    PK11SlotInfo *slot = NULL;
    CERTName *subject = 0;
    PRFileDesc *inFile = PR_STDIN;
    PRFileDesc *outFile = PR_STDOUT;
    SECItem certReqDER = { siBuffer, NULL, 0 };
    SECItem certDER = { siBuffer, NULL, 0 };
    const char *slotname = "internal";
    const char *certPrefix = "";
    char *sourceDir = "";
    const char *srcCertPrefix = "";
    char *upgradeID = "";
    char *upgradeTokenName = "";
    KeyType keytype = rsaKey;
    char *name = NULL;
    char *newName = NULL;
    char *email = NULL;
    char *keysource = NULL;
    SECOidTag hashAlgTag = SEC_OID_UNKNOWN;
    int keysize = DEFAULT_KEY_BITS;
    int publicExponent = 0x010001;
    int certVersion = SEC_CERTIFICATE_VERSION_3;
    unsigned int serialNumber = 0;
    int warpmonths = 0;
    int validityMonths = 3;
    int commandsEntered = 0;
    char commandToRun = '\0';
    secuPWData pwdata = { PW_NONE, 0 };
    secuPWData pwdata2 = { PW_NONE, 0 };
    PRBool readOnly = PR_FALSE;
    PRBool initialized = PR_FALSE;
    CK_FLAGS keyOpFlagsOn = 0;
    CK_FLAGS keyOpFlagsOff = 0;
    PK11AttrFlags keyAttrFlags =
        PK11_ATTR_TOKEN | PK11_ATTR_SENSITIVE | PK11_ATTR_PRIVATE;

    SECKEYPrivateKey *privkey = NULL;
    SECKEYPublicKey *pubkey = NULL;

    int i;
    SECStatus rv;

    progName = PORT_Strrchr(argv[0], '/');
    progName = progName ? progName + 1 : argv[0];
    memcpy(certutil_commands, commands_init, sizeof commands_init);
    memcpy(certutil_options, options_init, sizeof options_init);

    rv = SECU_ParseCommandLine(argc, argv, progName, &certutil);

    if (rv != SECSuccess)
        Usage();

    if (certutil.commands[cmd_PrintSyntax].activated) {
        PrintSyntax();
    }

    if (certutil.commands[cmd_PrintHelp].activated) {
        char buf[2];
        const char *command = NULL;
        for (i = 0; i < max_cmd; i++) {
            if (i == cmd_PrintHelp)
                continue;
            if (certutil.commands[i].activated) {
                if (certutil.commands[i].flag) {
                    buf[0] = certutil.commands[i].flag;
                    buf[1] = 0;
                    command = buf;
                } else {
                    command = certutil.commands[i].longform;
                }
                break;
            }
        }
        LongUsage((command ? usage_selected : usage_all), command);
        exit(1);
    }

    if (certutil.commands[cmd_BuildFlags].activated) {
        PrintBuildFlags();
    }

    if (certutil.options[opt_PasswordFile].arg) {
        pwdata.source = PW_FROMFILE;
        pwdata.data = certutil.options[opt_PasswordFile].arg;
    }
    if (certutil.options[opt_NewPasswordFile].arg) {
        pwdata2.source = PW_FROMFILE;
        pwdata2.data = certutil.options[opt_NewPasswordFile].arg;
    }

    if (certutil.options[opt_CertDir].activated)
        SECU_ConfigDirectory(certutil.options[opt_CertDir].arg);

    if (certutil.options[opt_SourceDir].activated)
        sourceDir = certutil.options[opt_SourceDir].arg;

    if (certutil.options[opt_UpgradeID].activated)
        upgradeID = certutil.options[opt_UpgradeID].arg;

    if (certutil.options[opt_UpgradeTokenName].activated)
        upgradeTokenName = certutil.options[opt_UpgradeTokenName].arg;

    /* must be before opt_KeySize! */
    /*  -k key type  */
    if (certutil.options[opt_KeyType].activated) {
        char *arg = certutil.options[opt_KeyType].arg;
        if (PL_strcmp(arg, "rsa") == 0) {
            keytype = rsaKey;
        } else if (PL_strcmp(arg, "dsa") == 0) {
            keytype = dsaKey;
        } else if (PL_strcmp(arg, "ec") == 0) {
            keytype = ecKey;
        } else if (PL_strcmp(arg, "ed") == 0) {
            keytype = edKey;
        } else if (PL_strcmp(arg, "mldsa") == 0) {
            keytype = mldsaKey;
        } else if (PL_strcmp(arg, "mlkem") == 0) {
            keytype = kyberKey;
        } else if (PL_strcmp(arg, "all") == 0) {
            keytype = nullKey;
        } else {
            /* use an existing private/public key pair */
            keysource = arg;
        }
    } else if (certutil.commands[cmd_ListKeys].activated) {
        keytype = nullKey;
    }

    if (certutil.options[opt_KeySize].activated) {
        keysize = PORT_Atoi(certutil.options[opt_KeySize].arg);
        if ((keytype == rsaKey || keytype == dsaKey) &&
            ((keysize < MIN_KEY_BITS) || (keysize > MAX_KEY_BITS))) {
            PR_fprintf(PR_STDERR,
                       "%s -g:  Keysize must be between %d and %d.\n",
                       progName, MIN_KEY_BITS, MAX_KEY_BITS);
            return 255;
        }
        if (keytype == ecKey) {
            PR_fprintf(PR_STDERR, "%s -g:  Not for ec keys.\n", progName);
            return 255;
        }
        if (keytype == kyberKey) {
            PR_fprintf(PR_STDERR,
                       "%s -g:  Not for mlkem keys, use -q.\n", progName);
            return 255;
        }
    }

    /*  -h specify token name  */
    if (certutil.options[opt_TokenName].activated) {
        if (PL_strcmp(certutil.options[opt_TokenName].arg, "all") == 0)
            slotname = NULL;
        else
            slotname = certutil.options[opt_TokenName].arg;
    }

    /*  -Z hash type  */
    if (certutil.options[opt_Hash].activated) {
        char *arg = certutil.options[opt_Hash].arg;
        hashAlgTag = SECU_StringToSignatureAlgTag(arg);
        if (hashAlgTag == SEC_OID_UNKNOWN) {
            PR_fprintf(PR_STDERR, "%s -Z:  %s is not a recognized type.\n",
                       progName, arg);
            return 255;
        }
    }

    if (certutil.options[opt_KeyOpFlagsOn].activated) {
        keyOpFlagsOn = GetOpFlags(certutil.options[opt_KeyOpFlagsOn].arg);
    }
    if (certutil.options[opt_KeyOpFlagsOff].activated) {
        keyOpFlagsOff = GetOpFlags(certutil.options[opt_KeyOpFlagsOff].arg);
        keyOpFlagsOn &= ~keyOpFlagsOff; /* make off override on */
    }
    if (certutil.options[opt_KeyAttrFlags].activated) {
        keyAttrFlags = GetAttrFlags(certutil.options[opt_KeyAttrFlags].arg);
    }

    /*  -m serial number */
    if (certutil.options[opt_SerialNumber].activated) {
        int sn = PORT_Atoi(certutil.options[opt_SerialNumber].arg);
        if (sn < 0) {
            PR_fprintf(PR_STDERR, "%s -m:  %s is not a valid serial number.\n",
                       progName, certutil.options[opt_SerialNumber].arg);
            return 255;
        }
        serialNumber = sn;
    }

    /*  -P certdb name prefix */
    if (certutil.options[opt_DBPrefix].activated) {
        if (certutil.options[opt_DBPrefix].arg) {
            certPrefix = certutil.options[opt_DBPrefix].arg;
        } else {
            Usage();
        }
    }

    /*  --source-prefix certdb name prefix */
    if (certutil.options[opt_SourcePrefix].activated) {
        if (certutil.options[opt_SourcePrefix].arg) {
            srcCertPrefix = certutil.options[opt_SourcePrefix].arg;
        } else {
            Usage();
        }
    }

    /*  -q PQG file or curve name */
    if (certutil.options[opt_PQGFile].activated) {
        if ((keytype != dsaKey) && (keytype != ecKey) && (keytype != edKey) &&
            (keytype != mldsaKey) && (keytype != kyberKey)) {
            PR_fprintf(PR_STDERR, "%s -q: specifies a PQG file for DSA keys"
                                  " (-k dsa)\n"
                                  " or a named curve for EC and ED keys (-k ec, ed)\n"
                                  " or a parameter set for ML-DSA keys (-k mldsa)\n"
                                  " or a parameter set for ML-KEM keys (-k mlkem)\n",
                       progName);
            return 255;
        }
    }

    /*  -s subject name  */
    if (certutil.options[opt_Subject].activated) {
        subject = CERT_AsciiToName(certutil.options[opt_Subject].arg);
        if (!subject) {
            PR_fprintf(PR_STDERR, "%s -s: improperly formatted name: \"%s\"\n",
                       progName, certutil.options[opt_Subject].arg);
            return 255;
        }
    }

    /*  -v validity period  */
    if (certutil.options[opt_Validity].activated) {
        validityMonths = PORT_Atoi(certutil.options[opt_Validity].arg);
        if (validityMonths < 0) {
            PR_fprintf(PR_STDERR, "%s -v: incorrect validity period: \"%s\"\n",
                       progName, certutil.options[opt_Validity].arg);
            return 255;
        }
    }

    /*  -w warp months  */
    if (certutil.options[opt_OffsetMonths].activated)
        warpmonths = PORT_Atoi(certutil.options[opt_OffsetMonths].arg);

    /*  -y public exponent (for RSA)  */
    if (certutil.options[opt_Exponent].activated) {
        publicExponent = PORT_Atoi(certutil.options[opt_Exponent].arg);
        if ((publicExponent != 3) &&
            (publicExponent != 17) &&
            (publicExponent != 65537)) {
            PR_fprintf(PR_STDERR, "%s -y: incorrect public exponent %d.",
                       progName, publicExponent);
            PR_fprintf(PR_STDERR, "Must be 3, 17, or 65537.\n");
            return 255;
        }
    }

    /*  --certVersion */
    if (certutil.options[opt_CertVersion].activated) {
        certVersion = PORT_Atoi(certutil.options[opt_CertVersion].arg);
        if (certVersion < 1 || certVersion > 4) {
            PR_fprintf(PR_STDERR, "%s -certVersion: incorrect certificate version %d.",
                       progName, certVersion);
            PR_fprintf(PR_STDERR, "Must be 1, 2, 3 or 4.\n");
            return 255;
        }
        certVersion = certVersion - 1;
    }

    /*  Check number of commands entered.  */
    commandsEntered = 0;
    for (i = 0; i < certutil.numCommands; i++) {
        if (certutil.commands[i].activated) {
            commandToRun = certutil.commands[i].flag;
            commandsEntered++;
        }
        if (commandsEntered > 1)
            break;
    }
    if (commandsEntered > 1) {
        PR_fprintf(PR_STDERR, "%s: only one command at a time!\n", progName);
        PR_fprintf(PR_STDERR, "You entered: ");
        for (i = 0; i < certutil.numCommands; i++) {
            if (certutil.commands[i].activated)
                PR_fprintf(PR_STDERR, " -%c", certutil.commands[i].flag);
        }
        PR_fprintf(PR_STDERR, "\n");
        return 255;
    }
    if (commandsEntered == 0) {
        Usage();
    }

    if (certutil.commands[cmd_ListCerts].activated ||
        certutil.commands[cmd_PrintHelp].activated ||
        certutil.commands[cmd_ListKeys].activated ||
        certutil.commands[cmd_ListModules].activated ||
        certutil.commands[cmd_CheckCertValidity].activated ||
        certutil.commands[cmd_Version].activated) {
        readOnly = !certutil.options[opt_RW].activated;
    }

    /*  -A, -D, -M, -S, -V, and all require -n  */
    if ((certutil.commands[cmd_AddCert].activated ||
         certutil.commands[cmd_DeleteCert].activated ||
         certutil.commands[cmd_DumpChain].activated ||
         certutil.commands[cmd_ModifyCertTrust].activated ||
         certutil.commands[cmd_CreateAndAddCert].activated ||
         certutil.commands[cmd_CheckCertValidity].activated) &&
        !certutil.options[opt_Nickname].activated) {
        PR_fprintf(PR_STDERR,
                   "%s -%c: nickname is required for this command (-n).\n",
                   progName, commandToRun);
        return 255;
    }

    /*  -A, -E, -M, -S require trust  */
    if ((certutil.commands[cmd_AddCert].activated ||
         certutil.commands[cmd_AddEmailCert].activated ||
         certutil.commands[cmd_ModifyCertTrust].activated ||
         certutil.commands[cmd_CreateAndAddCert].activated) &&
        !certutil.options[opt_Trust].activated) {
        PR_fprintf(PR_STDERR,
                   "%s -%c: trust is required for this command (-t).\n",
                   progName, commandToRun);
        return 255;
    }

    /*  if -L is given raw, ascii or dump mode, it must be for only one cert. */
    if (certutil.commands[cmd_ListCerts].activated &&
        (certutil.options[opt_ASCIIForIO].activated ||
         certutil.options[opt_DumpExtensionValue].activated ||
         certutil.options[opt_BinaryDER].activated) &&
        !certutil.options[opt_Nickname].activated) {
        PR_fprintf(PR_STDERR,
                   "%s: nickname is required to dump cert in raw or ascii mode.\n",
                   progName);
        return 255;
    }

    /*  -L can only be in (raw || ascii).  */
    if (certutil.commands[cmd_ListCerts].activated &&
        certutil.options[opt_ASCIIForIO].activated &&
        certutil.options[opt_BinaryDER].activated) {
        PR_fprintf(PR_STDERR,
                   "%s: cannot specify both -r and -a when dumping cert.\n",
                   progName);
        return 255;
    }

    /*  If making a cert request, need a subject.  */
    if ((certutil.commands[cmd_CertReq].activated ||
         certutil.commands[cmd_CreateAndAddCert].activated) &&
        !(certutil.options[opt_Subject].activated || keysource)) {
        PR_fprintf(PR_STDERR,
                   "%s -%c: subject is required to create a cert request.\n",
                   progName, commandToRun);
        return 255;
    }

    /*  If making a cert, need a serial number.  */
    if ((certutil.commands[cmd_CreateNewCert].activated ||
         certutil.commands[cmd_CreateAndAddCert].activated) &&
        !certutil.options[opt_SerialNumber].activated) {
        /*  Make a default serial number from the current time.  */
        PRTime now = PR_Now();
        LL_USHR(now, now, 19);
        LL_L2UI(serialNumber, now);
    }

    /*  Validation needs the usage to validate for.  */
    if (certutil.commands[cmd_CheckCertValidity].activated &&
        !certutil.options[opt_Usage].activated) {
        PR_fprintf(PR_STDERR,
                   "%s -V: specify a usage to validate the cert for (-u).\n",
                   progName);
        return 255;
    }

    /* Rename needs an old and a new nickname */
    if (certutil.commands[cmd_Rename].activated &&
        !(certutil.options[opt_Nickname].activated &&
          certutil.options[opt_NewNickname].activated)) {

        PR_fprintf(PR_STDERR,
                   "%s --rename: specify an old nickname (-n) and\n"
                   "   a new nickname (--new-n).\n",
                   progName);
        return 255;
    }

    /* Delete needs a nickname or a key ID */
    if (certutil.commands[cmd_DeleteKey].activated &&
        !(certutil.options[opt_Nickname].activated || keysource)) {
        PR_fprintf(PR_STDERR,
                   "%s -%c: specify a nickname (-n) or\n"
                   "   a key ID (-k).\n",
                   progName, commandToRun);
        return 255;
    }

    /* Upgrade/Merge needs a source database and a upgrade id. */
    if (certutil.commands[cmd_UpgradeMerge].activated &&
        !(certutil.options[opt_SourceDir].activated &&
          certutil.options[opt_UpgradeID].activated)) {

        PR_fprintf(PR_STDERR,
                   "%s --upgrade-merge: specify an upgrade database directory "
                   "(--source-dir) and\n"
                   "   an upgrade ID (--upgrade-id).\n",
                   progName);
        return 255;
    }

    /* Merge needs a source database */
    if (certutil.commands[cmd_Merge].activated &&
        !certutil.options[opt_SourceDir].activated) {

        PR_fprintf(PR_STDERR,
                   "%s --merge: specify an source database directory "
                   "(--source-dir)\n",
                   progName);
        return 255;
    }

    /*  To make a cert, need either a issuer or to self-sign it.  */
    if (certutil.commands[cmd_CreateAndAddCert].activated &&
        !(certutil.options[opt_IssuerName].activated ||
          certutil.options[opt_SelfSign].activated)) {
        PR_fprintf(PR_STDERR,
                   "%s -S: must specify issuer (-c) or self-sign (-x).\n",
                   progName);
        return 255;
    }

    /*  --kem-pubkey replaces the cert request of -C, so it needs a subject and
     *  an issuer: there is no private key to self-sign with. */
    if (certutil.options[opt_KEMPubKey].activated) {
        if (!certutil.commands[cmd_CreateNewCert].activated) {
            PR_fprintf(PR_STDERR,
                       "%s -%c: --kem-pubkey only works with -C.\n",
                       progName, commandToRun);
            return 255;
        }
        if (!subject || !certutil.options[opt_IssuerName].activated ||
            certutil.options[opt_SelfSign].activated ||
            certutil.options[opt_InputFile].activated) {
            PR_fprintf(PR_STDERR,
                       "%s -C: --kem-pubkey requires a subject (-s) and an issuer (-c),\n"
                       "   and cannot be used with -x or -i.\n",
                       progName);
            return 255;
        }
    }

    /*  Using slotname == NULL for listing keys and certs on all slots,
     *  but only that. */
    if (!(certutil.commands[cmd_ListKeys].activated ||
          certutil.commands[cmd_DumpChain].activated ||
          certutil.commands[cmd_ListCerts].activated) &&
        slotname == NULL) {
        PR_fprintf(PR_STDERR,
                   "%s -%c: cannot use \"-h all\" for this command.\n",
                   progName, commandToRun);
        return 255;
    }

    /*  Using keytype == nullKey for list all key types, but only that.  */
    if (!certutil.commands[cmd_ListKeys].activated && keytype == nullKey) {
        PR_fprintf(PR_STDERR,
                   "%s -%c: cannot use \"-k all\" for this command.\n",
                   progName, commandToRun);
        return 255;
    }

    /*  Open the input file.  */
    if (certutil.options[opt_InputFile].activated ||
        certutil.options[opt_KEMPubKey].activated) {
        const char *inFileName = certutil.options[opt_KEMPubKey].activated
                                     ? certutil.options[opt_KEMPubKey].arg
                                     : certutil.options[opt_InputFile].arg;
        inFile = PR_Open(inFileName, PR_RDONLY, 0);
        if (!inFile) {
            PR_fprintf(PR_STDERR,
                       "%s:  unable to open \"%s\" for reading (%ld, %ld).\n",
                       progName, inFileName,
                       PR_GetError(), PR_GetOSError());
            return 255;
        }
    }

    /*  Open the output file.  */
    if (certutil.options[opt_OutputFile].activated) {
        outFile = PR_Open(certutil.options[opt_OutputFile].arg,
                          PR_CREATE_FILE | PR_RDWR | PR_TRUNCATE, 00660);
        if (!outFile) {
            PR_fprintf(PR_STDERR,
                       "%s:  unable to open \"%s\" for writing (%ld, %ld).\n",
                       progName, certutil.options[opt_OutputFile].arg,
                       PR_GetError(), PR_GetOSError());
            return 255;
        }
    }

    name = SECU_GetOptionArg(&certutil, opt_Nickname);
    newName = SECU_GetOptionArg(&certutil, opt_NewNickname);
    email = SECU_GetOptionArg(&certutil, opt_Emailaddress);

    PK11_SetPasswordFunc(SECU_GetModulePassword);

    if (PR_TRUE == initialize) {
        /*  Initialize NSPR and NSS.  */
        PR_Init(PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);
        if (!certutil.commands[cmd_UpgradeMerge].activated) {
            rv = NSS_Initialize(SECU_ConfigDirectory(NULL),
                                certPrefix, certPrefix,
                                "secmod.db", readOnly ? NSS_INIT_READONLY : 0);
        } else {
            rv = NSS_InitWithMerge(SECU_ConfigDirectory(NULL),
                                   certPrefix, certPrefix, "secmod.db",
                                   sourceDir, srcCertPrefix, srcCertPrefix,
                                   upgradeID, upgradeTokenName,
                                   readOnly ? NSS_INIT_READONLY : 0);
        }
        if (rv != SECSuccess) {
            SECU_PrintPRandOSError(progName);
            rv = SECFailure;
            goto shutdown;
        }
        initialized = PR_TRUE;
        SECU_RegisterDynamicOids();
        /* Ensure the SSL error code table has been registered. Bug 1460284. */
        SSL_OptionSetDefault(-1, 0);
    }
    certHandle = CERT_GetDefaultCertDB();

    if (certutil.commands[cmd_Version].activated) {
        printf("Certificate database content version: command not implemented.\n");
    }

    if (PL_strcmp(slotname, "internal") == 0)
        slot = PK11_GetInternalKeySlot();
    else if (slotname != NULL)
        slot = PK11_FindSlotByName(slotname);

    if (!slot && (certutil.commands[cmd_NewDBs].activated ||
                  certutil.commands[cmd_ModifyCertTrust].activated ||
                  certutil.commands[cmd_ChangePassword].activated ||
                  certutil.commands[cmd_TokenReset].activated ||
                  certutil.commands[cmd_CreateAndAddCert].activated ||
                  certutil.commands[cmd_AddCert].activated ||
                  certutil.commands[cmd_Merge].activated ||
                  certutil.commands[cmd_UpgradeMerge].activated ||
                  certutil.commands[cmd_AddEmailCert].activated)) {

        SECU_PrintError(progName, "could not find the slot %s", slotname);
        rv = SECFailure;
        goto shutdown;
    }

    /*  If creating new database, initialize the password.  */
    if (certutil.commands[cmd_NewDBs].activated) {
        if (certutil.options[opt_EmptyPassword].activated && (PK11_NeedUserInit(slot))) {
            rv = PK11_InitPin(slot, (char *)NULL, "");
        } else {
            rv = SECU_ChangePW2(slot, 0, 0, certutil.options[opt_PasswordFile].arg,
                                certutil.options[opt_NewPasswordFile].arg);
        }
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "Could not set password for the slot");
            goto shutdown;
        }
    }

    /* if we are going to modify the cert database,
     * make sure it's initialized */
    if (certutil.commands[cmd_ModifyCertTrust].activated ||
        certutil.commands[cmd_CreateAndAddCert].activated ||
        certutil.commands[cmd_AddCert].activated ||
        certutil.commands[cmd_AddEmailCert].activated) {
        if (PK11_NeedLogin(slot) && PK11_NeedUserInit(slot)) {
            char *password = NULL;
            /* fetch the password from the command line or the file
             * if no password is supplied, initialize the password to NULL */
            if (pwdata.source == PW_FROMFILE) {
                password = SECU_FilePasswd(slot, PR_FALSE, pwdata.data);
            } else if (pwdata.source == PW_PLAINTEXT) {
                password = PL_strdup(pwdata.data);
            }
            rv = PK11_InitPin(slot, (char *)NULL, password ? password : "");
            if (password) {
                PORT_Memset(password, 0, PL_strlen(password));
                PORT_Free(password);
            }
            if (rv != SECSuccess) {
                SECU_PrintError(progName, "Could not set password for the slot");
                goto shutdown;
            }
        }
    }

    /* walk through the upgrade merge if necessary.
     * This option is more to test what some applications will want to do
     * to do an automatic upgrade. The --merge command is more useful for
     * the general case where 2 database need to be merged together.
     */
    if (certutil.commands[cmd_UpgradeMerge].activated) {
        if (*upgradeTokenName == 0) {
            upgradeTokenName = upgradeID;
        }
        if (!PK11_IsInternal(slot)) {
            fprintf(stderr, "Only internal DB's can be upgraded\n");
            rv = SECSuccess;
            goto shutdown;
        }
        if (!PK11_IsRemovable(slot)) {
            printf("database already upgraded.\n");
            rv = SECSuccess;
            goto shutdown;
        }
        if (!PK11_NeedLogin(slot)) {
            printf("upgrade complete!\n");
            rv = SECSuccess;
            goto shutdown;
        }
        /* authenticate to the old DB if necessary */
        if (PORT_Strcmp(PK11_GetTokenName(slot), upgradeTokenName) == 0) {
            /* if we need a password, supply it. This will be the password
             * for the old database */
            rv = PK11_Authenticate(slot, PR_FALSE, &pwdata2);
            if (rv != SECSuccess) {
                SECU_PrintError(progName, "Could not get password for %s",
                                upgradeTokenName);
                goto shutdown;
            }
            /*
             * if we succeeded above, but still aren't logged in, that means
             * we just supplied the password for the old database. We may
             * need the password for the new database. NSS will automatically
             * change the token names at this point
             */
            if (PK11_IsLoggedIn(slot, &pwdata)) {
                printf("upgrade complete!\n");
                rv = SECSuccess;
                goto shutdown;
            }
        }

        /* call PK11_IsPresent to update our cached token information */
        if (!PK11_IsPresent(slot)) {
            /* this shouldn't happen. We call isPresent to force a token
             * info update */
            fprintf(stderr, "upgrade/merge internal error\n");
            rv = SECFailure;
            goto shutdown;
        }

        /* the token is now set to the state of the source database,
         * if we need a password for it, PK11_Authenticate will
         * automatically prompt us */
        rv = PK11_Authenticate(slot, PR_FALSE, &pwdata);
        if (rv == SECSuccess) {
            printf("upgrade complete!\n");
        } else {
            SECU_PrintError(progName, "Could not get password for %s",
                            PK11_GetTokenName(slot));
        }
        goto shutdown;
    }

    /*
     * merge 2 databases.
     */
    if (certutil.commands[cmd_Merge].activated) {
        PK11SlotInfo *sourceSlot = NULL;
        PK11MergeLog *log;
        char *modspec = PR_smprintf(
            "configDir='%s' certPrefix='%s' tokenDescription='%s'",
            sourceDir, srcCertPrefix,
            *upgradeTokenName ? upgradeTokenName : "Source Database");

        if (!modspec) {
            rv = SECFailure;
            goto shutdown;
        }

        sourceSlot = SECMOD_OpenUserDB(modspec);
        PR_smprintf_free(modspec);
        if (!sourceSlot) {
            SECU_PrintError(progName, "couldn't open source database");
            rv = SECFailure;
            goto shutdown;
        }

        rv = PK11_Authenticate(slot, PR_FALSE, &pwdata);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "Couldn't get password for %s",
                            PK11_GetTokenName(slot));
            goto merge_fail;
        }

        rv = PK11_Authenticate(sourceSlot, PR_FALSE, &pwdata2);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "Couldn't get password for %s",
                            PK11_GetTokenName(sourceSlot));
            goto merge_fail;
        }

        log = PK11_CreateMergeLog();
        if (!log) {
            rv = SECFailure;
            SECU_PrintError(progName, "couldn't create error log");
            goto merge_fail;
        }

        rv = PK11_MergeTokens(slot, sourceSlot, log, &pwdata, &pwdata2);
        if (rv != SECSuccess) {
            DumpMergeLog(progName, log);
        }
        PK11_DestroyMergeLog(log);

    merge_fail:
        SECMOD_CloseUserDB(sourceSlot);
        PK11_FreeSlot(sourceSlot);
        goto shutdown;
    }

    /* The following 8 options are mutually exclusive with all others. */

    /*  List certs (-L)  */
    if (certutil.commands[cmd_ListCerts].activated) {
        if (certutil.options[opt_DumpExtensionValue].activated) {
            const char *oid_str;
            SECItem oid_item;
            SECStatus srv;
            oid_item.data = NULL;
            oid_item.len = 0;
            oid_str = certutil.options[opt_DumpExtensionValue].arg;
            srv = GetOidFromString(NULL, &oid_item, oid_str, strlen(oid_str));
            if (srv != SECSuccess) {
                SECU_PrintError(progName, "malformed extension OID %s",
                                oid_str);
                goto shutdown;
            }
            rv = ListCerts(certHandle, name, email, slot,
                           PR_TRUE /*binary*/, PR_FALSE /*ascii*/,
                           &oid_item,
                           outFile, &pwdata);
            SECITEM_FreeItem(&oid_item, PR_FALSE);
        } else {
            rv = ListCerts(certHandle, name, email, slot,
                           certutil.options[opt_BinaryDER].activated,
                           certutil.options[opt_ASCIIForIO].activated,
                           NULL, outFile, &pwdata);
        }
        goto shutdown;
    }
    if (certutil.commands[cmd_DumpChain].activated) {
        rv = DumpChain(certHandle, name,
                       certutil.options[opt_ASCIIForIO].activated,
                       certutil.options[opt_SimpleSelfSigned].activated);
        goto shutdown;
    }
    /*  XXX needs work  */
    /*  List keys (-K)  */
    if (certutil.commands[cmd_ListKeys].activated) {
        rv = ListKeys(slot, name, 0 /*keyindex*/, keytype, PR_FALSE /*dopriv*/,
                      &pwdata);
        goto shutdown;
    }
    /*  List modules (-U)  */
    if (certutil.commands[cmd_ListModules].activated) {
        rv = ListModules();
        goto shutdown;
    }
    /*  Delete cert (-D)  */
    if (certutil.commands[cmd_DeleteCert].activated) {
        rv = DeleteCert(certHandle, name, &pwdata);
        goto shutdown;
    }
    /*  Rename cert (--rename)  */
    if (certutil.commands[cmd_Rename].activated) {
        rv = RenameCert(certHandle, name, newName, &pwdata);
        goto shutdown;
    }
    /*  Delete key (-F)  */
    if (certutil.commands[cmd_DeleteKey].activated) {
        if (certutil.options[opt_Nickname].activated) {
            rv = DeleteCertAndKey(name, &pwdata);
        } else {
            privkey = findPrivateKeyByID(slot, keysource, &pwdata);
            if (!privkey) {
                SECU_PrintError(progName, "%s is not a key-id", keysource);
                rv = SECFailure;
            } else {
                rv = DeleteKey(privkey, &pwdata);
                /* already destroyed by PK11_DeleteTokenPrivateKey */
                privkey = NULL;
            }
        }
        goto shutdown;
    }
    /*  Modify trust attribute for cert (-M)  */
    if (certutil.commands[cmd_ModifyCertTrust].activated) {
        rv = ChangeTrustAttributes(certHandle, slot, name,
                                   certutil.options[opt_Trust].arg, &pwdata);
        goto shutdown;
    }
    /*  Change key db password (-W) (future - change pw to slot?)  */
    if (certutil.commands[cmd_ChangePassword].activated) {
        rv = SECU_ChangePW2(slot, 0, 0, certutil.options[opt_PasswordFile].arg,
                            certutil.options[opt_NewPasswordFile].arg);
        if (rv != SECSuccess) {
            SECU_PrintError(progName, "Could not set password for the slot");
            goto shutdown;
        }
    }
    /*  Reset the a token */
    if (certutil.commands[cmd_TokenReset].activated) {
        char *sso_pass = "";

        if (certutil.options[opt_SSOPass].activated) {
            sso_pass = certutil.options[opt_SSOPass].arg;
        }
        rv = PK11_ResetToken(slot, sso_pass);

        goto shutdown;
    }
    /*  Check cert validity against current time (-V)  */
    if (certutil.commands[cmd_CheckCertValidity].activated) {
        /* XXX temporary hack for fips - must log in to get priv key */
        if (certutil.options[opt_VerifySig].activated) {
            if (slot && PK11_NeedLogin(slot)) {
                SECStatus newrv = PK11_Authenticate(slot, PR_TRUE, &pwdata);
                if (newrv != SECSuccess) {
                    SECU_PrintError(progName, "could not authenticate to token %s.",
                                    PK11_GetTokenName(slot));
                    goto shutdown;
                }
            }
        }
        rv = ValidateCert(certHandle, name,
                          certutil.options[opt_ValidityTime].arg,
                          certutil.options[opt_Usage].arg,
                          certutil.options[opt_VerifySig].activated,
                          certutil.options[opt_DetailedInfo].activated,
                          certutil.options[opt_ASCIIForIO].activated,
                          &pwdata);
        if (rv != SECSuccess && PR_GetError() == SEC_ERROR_INVALID_ARGS)
            SECU_PrintError(progName, "validation failed");
        goto shutdown;
    }

    /*
     *  Key generation
     */

    /*  These commands may require keygen.  */
    if (certutil.commands[cmd_CertReq].activated ||
        certutil.commands[cmd_CreateAndAddCert].activated ||
        certutil.commands[cmd_GenKeyPair].activated) {
        if (keysource) {
            CERTCertificate *keycert;
            keycert = CERT_FindCertByNicknameOrEmailAddr(certHandle, keysource);
            if (!keycert) {
                keycert = PK11_FindCertFromNickname(keysource, NULL);
            }

            if (keycert) {
                privkey = PK11_FindKeyByDERCert(slot, keycert, &pwdata);
            } else {
                /* Interpret keysource as CKA_ID */
                privkey = findPrivateKeyByID(slot, keysource, &pwdata);
            }

            if (!privkey) {
                SECU_PrintError(
                    progName,
                    "%s is neither a key-type nor a nickname nor a key-id", keysource);
                return SECFailure;
            }

            pubkey = SECKEY_ConvertToPublicKey(privkey);
            if (!pubkey) {
                SECU_PrintError(progName,
                                "Could not get keys from cert %s", keysource);
                if (keycert) {
                    CERT_DestroyCertificate(keycert);
                }
                rv = SECFailure;
                goto shutdown;
            }
            keytype = privkey->keyType;

            /* On CertReq for renewal if no subject has been
             * specified obtain it from the certificate.
             */
            if (certutil.commands[cmd_CertReq].activated && !subject) {
                if (keycert) {
                    subject = CERT_AsciiToName(keycert->subjectName);
                    if (!subject) {
                        SECU_PrintError(
                            progName,
                            "Could not get subject from certificate %s",
                            keysource);
                        CERT_DestroyCertificate(keycert);
                        rv = SECFailure;
                        goto shutdown;
                    }
                } else {
                    SECU_PrintError(progName, "Subject name not provided");
                    rv = SECFailure;
                    goto shutdown;
                }
            }
            if (keycert) {
                CERT_DestroyCertificate(keycert);
            }
        } else {
            privkey =
                CERTUTIL_GeneratePrivateKey(keytype, slot, keysize,
                                            publicExponent,
                                            certutil.options[opt_NoiseFile].arg,
                                            &pubkey,
                                            certutil.options[opt_PQGFile].arg,
                                            keyAttrFlags,
                                            keyOpFlagsOn,
                                            keyOpFlagsOff,
                                            &pwdata);
            if (privkey == NULL) {
                SECU_PrintError(progName, "unable to generate key(s)\n");
                rv = SECFailure;
                goto shutdown;
            }
        }
        privkey->wincx = &pwdata;
        PORT_Assert(pubkey != NULL);

        /*  If all that was needed was keygen, exit.  */
        if (certutil.commands[cmd_GenKeyPair].activated) {
            rv = SECSuccess;
            if (certutil.options[opt_OutputFile].activated) {
                rv = WritePublicKeyInfo(outFile, pubkey,
                                        certutil.options[opt_ASCIIForIO].activated);
            }
            goto shutdown;
        }
    }

    if (certutil.options[opt_Pss].activated) {
        if (!certutil.commands[cmd_CertReq].activated &&
            !certutil.commands[cmd_CreateAndAddCert].activated) {
            PR_fprintf(PR_STDERR,
                       "%s -%c: --pss only works with -R or -S.\n",
                       progName, commandToRun);
            return 255;
        }
        if (keytype != rsaKey) {
            PR_fprintf(PR_STDERR,
                       "%s -%c: --pss only works with RSA keys.\n",
                       progName, commandToRun);
            return 255;
        }
    }

    /* --pss-sign is to sign a certificate with RSA-PSS, even if the
     * issuer's key is an RSA key.  If the key is an RSA-PSS key, the
     * generated signature is always RSA-PSS. */
    if (certutil.options[opt_PssSign].activated) {
        if (!certutil.commands[cmd_CreateNewCert].activated &&
            !certutil.commands[cmd_CreateAndAddCert].activated) {
            PR_fprintf(PR_STDERR,
                       "%s -%c: --pss-sign only works with -C or -S.\n",
                       progName, commandToRun);
            return 255;
        }
        if (keytype != rsaKey) {
            PR_fprintf(PR_STDERR,
                       "%s -%c: --pss-sign only works with RSA keys.\n",
                       progName, commandToRun);
            return 255;
        }
    }

    if (certutil.options[opt_SimpleSelfSigned].activated &&
        !certutil.commands[cmd_DumpChain].activated) {
        PR_fprintf(PR_STDERR,
                   "%s -%c: --simple-self-signed only works with -O.\n",
                   progName, commandToRun);
        return 255;
    }

    /* If we need a list of extensions convert the flags into list format */
    if (certutil.commands[cmd_CertReq].activated ||
        certutil.commands[cmd_CreateAndAddCert].activated ||
        certutil.commands[cmd_CreateNewCert].activated) {
        certutil_extns[ext_keyUsage].activated =
            certutil.options[opt_AddCmdKeyUsageExt].activated;
        if (!certutil_extns[ext_keyUsage].activated) {
            certutil_extns[ext_keyUsage].activated =
                certutil.options[opt_AddKeyUsageExt].activated;
        } else {
            certutil_extns[ext_keyUsage].arg =
                certutil.options[opt_AddCmdKeyUsageExt].arg;
        }
        certutil_extns[ext_basicConstraint].activated =
            certutil.options[opt_AddBasicConstraintExt].activated;
        certutil_extns[ext_nameConstraints].activated =
            certutil.options[opt_AddNameConstraintsExt].activated;
        certutil_extns[ext_authorityKeyID].activated =
            certutil.options[opt_AddAuthorityKeyIDExt].activated;
        certutil_extns[ext_subjectKeyID].activated =
            certutil.options[opt_AddSubjectKeyIDExt].activated;
        certutil_extns[ext_CRLDistPts].activated =
            certutil.options[opt_AddCRLDistPtsExt].activated;
        certutil_extns[ext_NSCertType].activated =
            certutil.options[opt_AddCmdNSCertTypeExt].activated;
        if (!certutil_extns[ext_NSCertType].activated) {
            certutil_extns[ext_NSCertType].activated =
                certutil.options[opt_AddNSCertTypeExt].activated;
        } else {
            certutil_extns[ext_NSCertType].arg =
                certutil.options[opt_AddCmdNSCertTypeExt].arg;
        }

        certutil_extns[ext_extKeyUsage].activated =
            certutil.options[opt_AddCmdExtKeyUsageExt].activated;
        if (!certutil_extns[ext_extKeyUsage].activated) {
            certutil_extns[ext_extKeyUsage].activated =
                certutil.options[opt_AddExtKeyUsageExt].activated;
        } else {
            certutil_extns[ext_extKeyUsage].arg =
                certutil.options[opt_AddCmdExtKeyUsageExt].arg;
        }
        certutil_extns[ext_subjectAltName].activated =
            certutil.options[opt_AddSubjectAltNameExt].activated;
        if (certutil_extns[ext_subjectAltName].activated) {
            certutil_extns[ext_subjectAltName].arg =
                certutil.options[opt_AddSubjectAltNameExt].arg;
        }

        certutil_extns[ext_authInfoAcc].activated =
            certutil.options[opt_AddAuthInfoAccExt].activated;
        certutil_extns[ext_subjInfoAcc].activated =
            certutil.options[opt_AddSubjInfoAccExt].activated;
        certutil_extns[ext_certPolicies].activated =
            certutil.options[opt_AddCertPoliciesExt].activated;
        certutil_extns[ext_policyMappings].activated =
            certutil.options[opt_AddPolicyMapExt].activated;
        certutil_extns[ext_policyConstr].activated =
            certutil.options[opt_AddPolicyConstrExt].activated;
        certutil_extns[ext_inhibitAnyPolicy].activated =
            certutil.options[opt_AddInhibAnyExt].activated;
    }

    /* -A -C or -E    Read inFile */
    if (certutil.commands[cmd_CreateNewCert].activated ||
        certutil.commands[cmd_AddCert].activated ||
        certutil.commands[cmd_AddEmailCert].activated) {
        PRBool isCreate = certutil.commands[cmd_CreateNewCert].activated;
        rv = SECU_ReadDERFromFile(isCreate ? &certReqDER : &certDER, inFile,
                                  certutil.options[opt_ASCIIForIO].activated,
                                  PR_TRUE);
        if (rv)
            goto shutdown;
    }

    /*
     *  Certificate request
     */

    /*  Make a cert request (-R).  */
    if (certutil.commands[cmd_CertReq].activated) {
        rv = CertReq(privkey, pubkey, keytype, hashAlgTag, subject,
                     certutil.options[opt_PhoneNumber].arg,
                     certutil.options[opt_ASCIIForIO].activated,
                     certutil.options[opt_ExtendedEmailAddrs].arg,
                     certutil.options[opt_ExtendedDNSNames].arg,
                     certutil_extns,
                     (certutil.options[opt_GenericExtensions].activated ? certutil.options[opt_GenericExtensions].arg
                                                                        : NULL),
                     certutil.options[opt_Pss].activated,
                     &certReqDER);
        if (rv)
            goto shutdown;
        privkey->wincx = &pwdata;
    }

    /*
     *  Certificate creation
     */

    /*  If making and adding a cert, create a cert request file first without
     *  any extensions, then load it with the command line extensions
     *  and output the cert to another file.
     */
    if (certutil.commands[cmd_CreateAndAddCert].activated) {
        static certutilExtnList nullextnlist = { { PR_FALSE, NULL } };
        rv = CertReq(privkey, pubkey, keytype, hashAlgTag, subject,
                     certutil.options[opt_PhoneNumber].arg,
                     PR_FALSE, /* do not BASE64-encode regardless of -a option */
                     NULL,
                     NULL,
                     nullextnlist,
                     (certutil.options[opt_GenericExtensions].activated ? certutil.options[opt_GenericExtensions].arg
                                                                        : NULL),
                     certutil.options[opt_Pss].activated,
                     &certReqDER);
        if (rv)
            goto shutdown;
        privkey->wincx = &pwdata;
    }

    /*  Create a certificate (-C or -S).  */
    if (certutil.commands[cmd_CreateAndAddCert].activated ||
        certutil.commands[cmd_CreateNewCert].activated) {
        rv = CreateCert(certHandle, slot,
                        certutil.options[opt_IssuerName].arg,
                        &certReqDER,
                        certutil.options[opt_KEMPubKey].activated ? subject : NULL,
                        &privkey, &pwdata, hashAlgTag,
                        serialNumber, warpmonths, validityMonths,
                        certutil.options[opt_ExtendedEmailAddrs].arg,
                        certutil.options[opt_ExtendedDNSNames].arg,
                        certutil.options[opt_ASCIIForIO].activated &&
                            certutil.commands[cmd_CreateNewCert].activated,
                        certutil.options[opt_SelfSign].activated,
                        certutil_extns,
                        (certutil.options[opt_GenericExtensions].activated ? certutil.options[opt_GenericExtensions].arg
                                                                           : NULL),
                        certVersion,
                        certutil.options[opt_PssSign].activated,
                        &certDER);
        if (rv)
            goto shutdown;
    }

    /*
     * Adding a cert to the database (or slot)
     */

    /* -A -E or -S    Add the cert to the DB */
    if (certutil.commands[cmd_CreateAndAddCert].activated ||
        certutil.commands[cmd_AddCert].activated ||
        certutil.commands[cmd_AddEmailCert].activated) {
        if (strstr(certutil.options[opt_Trust].arg, "u")) {
            fprintf(stderr, "Notice: Trust flag u is set automatically if the "
                            "private key is present.\n");
        }
        rv = AddCert(slot, certHandle, name,
                     certutil.options[opt_Trust].arg,
                     &certDER,
                     certutil.commands[cmd_AddEmailCert].activated, &pwdata);
        if (rv)
            goto shutdown;
    }

    if (certutil.commands[cmd_CertReq].activated ||
        certutil.commands[cmd_CreateNewCert].activated) {
        SECItem *item = certutil.commands[cmd_CertReq].activated ? &certReqDER
                                                                 : &certDER;
        PRInt32 written = PR_Write(outFile, item->data, item->len);
        if (written < 0 || (PRUint32)written != item->len) {
            rv = SECFailure;
        }
    }

shutdown:
    if (slot) {
        PK11_FreeSlot(slot);
    }
    if (privkey) {
        SECKEY_DestroyPrivateKey(privkey);
    }
    if (pubkey) {
        SECKEY_DestroyPublicKey(pubkey);
    }
    if (subject) {
        CERT_DestroyName(subject);
    }
    if (name) {
        PL_strfree(name);
    }
    if (newName) {
        PL_strfree(newName);
    }
    if (inFile && inFile != PR_STDIN) {
        PR_Close(inFile);
    }
    if (outFile && outFile != PR_STDOUT) {
        PR_Close(outFile);
    }
    SECITEM_FreeItem(&certReqDER, PR_FALSE);
    SECITEM_FreeItem(&certDER, PR_FALSE);
    if (pwdata.data && pwdata.source == PW_PLAINTEXT) {
        /* Allocated by a PL_strdup call in SECU_GetModulePassword. */
        PL_strfree(pwdata.data);
    }
    if (email) {
        PL_strfree(email);
    }

    /* Open the batch command file.
     *
     * - If -B <command line> option is specified, the contents in the
     * command file will be interpreted as subsequent certutil
     * commands to be executed in the current certutil process
     * context after the current certutil command has been executed.
     * - Each line in the command file consists of the command
     * line arguments for certutil.
     * - The -d <configdir> option will be ignored if specified in the
     * command file.
     * - Quoting with double quote characters ("...") is supported
     * to allow white space in a command line argument.  The
     * double quote character cannot be escaped and quoting cannot
     * be nested in this version.
     * - each line in the batch file is limited to 512 characters
     */

    if ((SECSuccess == rv) && certutil.commands[cmd_Batch].activated) {
        FILE *batchFile = NULL;
        char *nextcommand = NULL;
        PRInt32 cmd_len = 0, buf_size = 0;
        static const int increment = 512;

        if (!certutil.options[opt_InputFile].activated ||
            !certutil.options[opt_InputFile].arg) {
            PR_fprintf(PR_STDERR,
                       "%s:  no batch input file specified.\n",
                       progName);
            return 255;
        }
        batchFile = fopen(certutil.options[opt_InputFile].arg, "r");
        if (!batchFile) {
            PR_fprintf(PR_STDERR,
                       "%s:  unable to open \"%s\" for reading (%ld, %ld).\n",
                       progName, certutil.options[opt_InputFile].arg,
                       PR_GetError(), PR_GetOSError());
            return 255;
        }
        /* read and execute command-lines in a loop */
        while (SECSuccess == rv) {
            PRBool invalid = PR_FALSE;
            int newargc = 2;
            char *space = NULL;
            char *nextarg = NULL;
            char **newargv = NULL;
            char *crlf;

            if (cmd_len + increment > buf_size) {
                char *new_buf;
                buf_size += increment;
                new_buf = PORT_Realloc(nextcommand, buf_size);
                if (!new_buf) {
                    PR_fprintf(PR_STDERR, "%s: PORT_Realloc(%ld) failed\n",
                               progName, buf_size);
                    break;
                }
                nextcommand = new_buf;
                nextcommand[cmd_len] = '\0';
            }
            if (!fgets(nextcommand + cmd_len, buf_size - cmd_len, batchFile)) {
                break;
            }
            crlf = PORT_Strrchr(nextcommand, '\n');
            if (crlf) {
                *crlf = '\0';
            }
            cmd_len = strlen(nextcommand);
            if (cmd_len && nextcommand[cmd_len - 1] == '\\') {
                nextcommand[--cmd_len] = '\0';
                continue;
            }

            /* we now need to split the command into argc / argv format */

            newargv = PORT_Alloc(sizeof(char *) * (newargc + 1));
            newargv[0] = progName;
            newargv[1] = nextcommand;
            nextarg = nextcommand;
            while ((space = PORT_Strpbrk(nextarg, " \f\n\r\t\v"))) {
                while (isspace((unsigned char)*space)) {
                    *space = '\0';
                    space++;
                }
                if (*space == '\0') {
                    break;
                } else if (*space != '\"') {
                    nextarg = space;
                } else {
                    char *closingquote = strchr(space + 1, '\"');
                    if (closingquote) {
                        *closingquote = '\0';
                        space++;
                        nextarg = closingquote + 1;
                    } else {
                        invalid = PR_TRUE;
                        nextarg = space;
                    }
                }
                newargc++;
                newargv = PORT_Realloc(newargv, sizeof(char *) * (newargc + 1));
                newargv[newargc - 1] = space;
            }
            newargv[newargc] = NULL;

            /* invoke next command */
            if (PR_TRUE == invalid) {
                PR_fprintf(PR_STDERR, "Missing closing quote in batch command :\n%s\nNot executed.\n",
                           nextcommand);
                rv = SECFailure;
            } else {
                if (0 != certutil_main(newargc, newargv, PR_FALSE))
                    rv = SECFailure;
            }
            PORT_Free(newargv);
            cmd_len = 0;
            nextcommand[0] = '\0';
        }
        PORT_Free(nextcommand);
        fclose(batchFile);
    }

    if ((initialized == PR_TRUE) && NSS_Shutdown() != SECSuccess) {
        exit(1);
    }
    if (rv == SECSuccess) {
        return 0;
    } else {
        return 255;
    }
}

int
main(int argc, char **argv)
{
    int rv = certutil_main(argc, argv, PR_TRUE);
    PL_ArenaFinish();
    PR_Cleanup();
    return rv;
}
