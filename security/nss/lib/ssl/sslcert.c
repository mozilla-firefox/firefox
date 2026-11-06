/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * SSL server certificate configuration functions.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ssl.h"
#include "sslimpl.h"
#include "secoid.h"        /* for SECOID_GetAlgorithmTag */
#include "pk11func.h"      /* for PK11_ReferenceSlot */
#include "nss.h"           /* for NSS_RegisterShutdown */
#include "prinit.h"        /* for PR_CallOnceWithArg */
#include "tls13subcerts.h" /* for tls13_ReadDelegatedCredential */

/* This global item is used only in servers.  It is is initialized by
 * SSL_ConfigSecureServer(), and is used in ssl3_SendCertificateRequest().
 */
static struct {
    PRCallOnceType setup;
    CERTDistNames *names;
} ssl_server_ca_list;

static SECStatus
ssl_ServerCAListShutdown(void *appData, void *nssData)
{
    PORT_Assert(ssl_server_ca_list.names);
    if (ssl_server_ca_list.names) {
        CERT_FreeDistNames(ssl_server_ca_list.names);
    }
    PORT_Memset(&ssl_server_ca_list, 0, sizeof(ssl_server_ca_list));
    return SECSuccess;
}

static PRStatus
ssl_SetupCAListOnce(void *arg)
{
    CERTCertDBHandle *dbHandle = (CERTCertDBHandle *)arg;
    SECStatus rv = NSS_RegisterShutdown(ssl_ServerCAListShutdown, NULL);
    PORT_Assert(SECSuccess == rv);
    if (SECSuccess == rv) {
        ssl_server_ca_list.names = CERT_GetSSLCACerts(dbHandle);
        return PR_SUCCESS;
    }
    return PR_FAILURE;
}

SECStatus
ssl_SetupCAList(const sslSocket *ss)
{
    if (PR_SUCCESS != PR_CallOnceWithArg(&ssl_server_ca_list.setup,
                                         &ssl_SetupCAListOnce,
                                         (void *)(ss->dbHandle))) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    return SECSuccess;
}

SECStatus
ssl_GetCertificateRequestCAs(const sslSocket *ss, unsigned int *calen,
                             const SECItem **names, unsigned int *nnames)
{
    const SECItem *name;
    const CERTDistNames *ca_list;
    unsigned int i;

    *calen = 0;
    *names = NULL;
    *nnames = 0;

    /* ssl3.ca_list is initialized to NULL, and never changed. */
    ca_list = ss->ssl3.ca_list;
    if (!ca_list) {
        if (ssl_SetupCAList(ss) != SECSuccess) {
            return SECFailure;
        }
        ca_list = ssl_server_ca_list.names;
    }

    if (ca_list != NULL) {
        *names = ca_list->names;
        *nnames = ca_list->nnames;
    }

    for (i = 0, name = *names; i < *nnames; i++, name++) {
        *calen += 2 + name->len;
    }
    return SECSuccess;
}

sslServerCert *
ssl_NewServerCert()
{
    sslServerCert *sc = PORT_ZNew(sslServerCert);
    if (!sc) {
        return NULL;
    }
    sc->authTypes = 0;
    sc->namedCurve = NULL;
    sc->serverCert = NULL;
    sc->serverCertChain = NULL;
    sc->certStatusArray = NULL;
    sc->signedCertTimestamps.len = 0;
    sc->delegCred.len = 0;
    sc->delegCredKeyPair = NULL;
    return sc;
}

sslServerCert *
ssl_CopyServerCert(const sslServerCert *oc)
{
    sslServerCert *sc;

    sc = ssl_NewServerCert();
    if (!sc) {
        return NULL;
    }

    sc->authTypes = oc->authTypes;
    sc->namedCurve = oc->namedCurve;

    if (oc->serverCert && oc->serverCertChain) {
        sc->serverCert = CERT_DupCertificate(oc->serverCert);
        if (!sc->serverCert)
            goto loser;
        sc->serverCertChain = CERT_DupCertList(oc->serverCertChain);
        if (!sc->serverCertChain)
            goto loser;
    } else {
        sc->serverCert = NULL;
        sc->serverCertChain = NULL;
    }

    if (oc->serverKeyPair) {
        sc->serverKeyPair = ssl_GetKeyPairRef(oc->serverKeyPair);
        if (!sc->serverKeyPair)
            goto loser;
    } else {
        sc->serverKeyPair = NULL;
    }
    sc->serverKeyBits = oc->serverKeyBits;

    if (oc->certStatusArray) {
        sc->certStatusArray = SECITEM_DupArray(NULL, oc->certStatusArray);
        if (!sc->certStatusArray)
            goto loser;
    } else {
        sc->certStatusArray = NULL;
    }

    if (SECITEM_CopyItem(NULL, &sc->signedCertTimestamps,
                         &oc->signedCertTimestamps) != SECSuccess) {
        goto loser;
    }

    if (SECITEM_CopyItem(NULL, &sc->delegCred, &oc->delegCred) != SECSuccess) {
        goto loser;
    }
    if (oc->delegCredKeyPair) {
        sc->delegCredKeyPair = ssl_GetKeyPairRef(oc->delegCredKeyPair);
    }

    return sc;
loser:
    ssl_FreeServerCert(sc);
    return NULL;
}

void
ssl_FreeServerCert(sslServerCert *sc)
{
    if (!sc) {
        return;
    }

    if (sc->serverCert) {
        CERT_DestroyCertificate(sc->serverCert);
    }
    if (sc->serverCertChain) {
        CERT_DestroyCertificateList(sc->serverCertChain);
    }
    if (sc->serverKeyPair) {
        ssl_FreeKeyPair(sc->serverKeyPair);
    }
    if (sc->certStatusArray) {
        SECITEM_FreeArray(sc->certStatusArray, PR_TRUE);
    }
    if (sc->signedCertTimestamps.len) {
        SECITEM_FreeItem(&sc->signedCertTimestamps, PR_FALSE);
    }
    if (sc->delegCred.len) {
        SECITEM_FreeItem(&sc->delegCred, PR_FALSE);
    }
    if (sc->delegCredKeyPair) {
        ssl_FreeKeyPair(sc->delegCredKeyPair);
    }
    PORT_ZFree(sc, sizeof(*sc));
}

const sslServerCert *
ssl_FindServerCert(const sslSocket *ss, SSLAuthType authType,
                   const sslNamedGroupDef *namedCurve)
{
    PRCList *cursor;

    /* Bug 1749475: avoid UB while fuzzing session tickets */
    if ((unsigned)authType >= ssl_auth_size) {
        return NULL;
    }

    for (cursor = PR_NEXT_LINK(&ss->serverCerts);
         cursor != &ss->serverCerts;
         cursor = PR_NEXT_LINK(cursor)) {
        sslServerCert *cert = (sslServerCert *)cursor;
        if (!SSL_CERT_IS(cert, authType)) {
            continue;
        }
        if (SSL_CERT_IS_EC(cert)) {
            /* Note: For deprecated APIs, we need to be able to find and
               match a slot with any named curve. */
            if (namedCurve && cert->namedCurve != namedCurve) {
                continue;
            }
        }
        return cert;
    }
    return NULL;
}

static SECStatus
ssl_PopulateServerCert(sslServerCert *sc, CERTCertificate *cert,
                       const CERTCertificateList *certChain)
{
    if (sc->serverCert) {
        CERT_DestroyCertificate(sc->serverCert);
    }
    if (sc->serverCertChain) {
        CERT_DestroyCertificateList(sc->serverCertChain);
    }

    if (!cert) {
        sc->serverCert = NULL;
        sc->serverCertChain = NULL;
        return SECSuccess;
    }

    sc->serverCert = CERT_DupCertificate(cert);
    if (certChain) {
        sc->serverCertChain = CERT_DupCertList(certChain);
    } else {
        sc->serverCertChain =
            CERT_CertChainFromCert(sc->serverCert, certUsageSSLServer,
                                   PR_TRUE);
    }
    return sc->serverCertChain ? SECSuccess : SECFailure;
}

static SECStatus
ssl_PopulateKeyPair(sslServerCert *sc, sslKeyPair *keyPair)
{
    if (sc->serverKeyPair) {
        ssl_FreeKeyPair(sc->serverKeyPair);
        sc->serverKeyPair = NULL;
    }
    if (keyPair) {
        KeyType keyType = SECKEY_GetPublicKeyType(keyPair->pubKey);
        PORT_Assert(keyType == SECKEY_GetPrivateKeyType(keyPair->privKey));

        if (keyType == ecKey) {
            sc->namedCurve = ssl_ECPubKey2NamedGroup(keyPair->pubKey);
            if (!sc->namedCurve) {
                /* Unsupported curve. */
                PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
                return SECFailure;
            }
        }

        /* Get the size of the cert's public key, and remember it. */
        sc->serverKeyBits = SECKEY_PublicKeyStrengthInBits(keyPair->pubKey);
        if (sc->serverKeyBits == 0 ||
            (keyType == rsaKey && sc->serverKeyBits > SSL_MAX_RSA_KEY_BITS)) {
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            return SECFailure;
        }

        SECKEY_CacheStaticFlags(keyPair->privKey);
        sc->serverKeyPair = ssl_GetKeyPairRef(keyPair);

        if (SSL_CERT_IS(sc, ssl_auth_rsa_decrypt)) {
            /* This will update the global session ticket key pair with this
             * key, if a value hasn't been set already. */
            if (ssl_MaybeSetSelfEncryptKeyPair(keyPair) != SECSuccess) {
                return SECFailure;
            }
        }
    } else {
        sc->serverKeyPair = NULL;
        sc->namedCurve = NULL;
    }
    return SECSuccess;
}

static SECStatus
ssl_PopulateOCSPResponses(sslServerCert *sc,
                          const SECItemArray *stapledOCSPResponses)
{
    if (sc->certStatusArray) {
        SECITEM_FreeArray(sc->certStatusArray, PR_TRUE);
    }
    if (stapledOCSPResponses) {
        sc->certStatusArray = SECITEM_DupArray(NULL, stapledOCSPResponses);
        return sc->certStatusArray ? SECSuccess : SECFailure;
    } else {
        sc->certStatusArray = NULL;
    }
    return SECSuccess;
}

static SECStatus
ssl_PopulateSignedCertTimestamps(sslServerCert *sc,
                                 const SECItem *signedCertTimestamps)
{
    if (sc->signedCertTimestamps.len) {
        SECITEM_FreeItem(&sc->signedCertTimestamps, PR_FALSE);
    }
    if (signedCertTimestamps && signedCertTimestamps->len) {
        return SECITEM_CopyItem(NULL, &sc->signedCertTimestamps,
                                signedCertTimestamps);
    }
    return SECSuccess;
}

/* Installs the given delegated credential (DC) and DC private key into the
 * certificate.
 *
 * It's the caller's responsibility to ensure that the DC is well-formed and
 * that the DC public key matches the DC private key.
 */
static SECStatus
ssl_PopulateDelegatedCredential(sslServerCert *sc,
                                const SECItem *delegCred,
                                const SECKEYPrivateKey *delegCredPrivKey)
{
    sslDelegatedCredential *dc = NULL;

    if (sc->delegCred.len) {
        SECITEM_FreeItem(&sc->delegCred, PR_FALSE);
    }

    if (sc->delegCredKeyPair) {
        ssl_FreeKeyPair(sc->delegCredKeyPair);
        sc->delegCredKeyPair = NULL;
    }

    /* Both the DC and its private are present. */
    if (delegCred && delegCredPrivKey) {
        SECStatus rv;
        SECKEYPublicKey *pub;
        SECKEYPrivateKey *priv;

        if (!delegCred->data || delegCred->len == 0) {
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            goto loser;
        }

        /* Parse the DC. */
        rv = tls13_ReadDelegatedCredential(delegCred->data, delegCred->len, &dc);
        if (rv != SECSuccess) {
            goto loser;
        }

        /* Make a copy of the DC. */
        rv = SECITEM_CopyItem(NULL, &sc->delegCred, delegCred);
        if (rv != SECSuccess) {
            goto loser;
        }

        /* Make a copy of the DC private key. */
        priv = SECKEY_CopyPrivateKey(delegCredPrivKey);
        if (!priv) {
            goto loser;
        }

        /* parse public key from the DC. */
        pub = SECKEY_ExtractPublicKey(dc->spki);
        if (!pub) {
            goto loser;
        }

        sc->delegCredKeyPair = ssl_NewKeyPair(priv, pub);

        /* Attempting to configure either the DC or DC private key, but not both. */
    } else if (delegCred || delegCredPrivKey) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        goto loser;
    }

    tls13_DestroyDelegatedCredential(dc);
    return SECSuccess;

loser:
    tls13_DestroyDelegatedCredential(dc);
    return SECFailure;
}

/* Find any existing certificates that overlap with the new certificate and
 * either remove any supported authentication types that overlap with the new
 * certificate or - if they have no types left - remove them entirely. */
static void
ssl_ClearMatchingCerts(sslSocket *ss, sslAuthTypeMask authTypes,
                       const sslNamedGroupDef *namedCurve)
{
    PRCList *cursor = PR_NEXT_LINK(&ss->serverCerts);

    while (cursor != &ss->serverCerts) {
        sslServerCert *sc = (sslServerCert *)cursor;
        cursor = PR_NEXT_LINK(cursor);
        if ((sc->authTypes & authTypes) == 0) {
            continue;
        }
        /* namedCurve will be NULL only for legacy functions. */
        if (namedCurve != NULL && sc->namedCurve != namedCurve) {
            continue;
        }

        sc->authTypes &= ~authTypes;
        if (sc->authTypes == 0) {
            PR_REMOVE_LINK(&sc->link);
            ssl_FreeServerCert(sc);
        }
    }
}

static SECStatus
ssl_ConfigCert(sslSocket *ss, sslAuthTypeMask authTypes,
               CERTCertificate *cert, sslKeyPair *keyPair,
               const SSLExtraServerCertData *data)
{
    SECStatus rv;
    sslServerCert *sc = NULL;
    int error_code = SEC_ERROR_NO_MEMORY;

    PORT_Assert(cert);
    PORT_Assert(keyPair);
    PORT_Assert(data);
    PORT_Assert(authTypes);

    if (!cert || !keyPair || !data || !authTypes) {
        error_code = SEC_ERROR_INVALID_ARGS;
        goto loser;
    }

    sc = ssl_NewServerCert();
    if (!sc) {
        goto loser;
    }

    sc->authTypes = authTypes;
    rv = ssl_PopulateServerCert(sc, cert, data->certChain);
    if (rv != SECSuccess) {
        goto loser;
    }
    rv = ssl_PopulateKeyPair(sc, keyPair);
    if (rv != SECSuccess) {
        error_code = PORT_GetError();
        goto loser;
    }
    rv = ssl_PopulateOCSPResponses(sc, data->stapledOCSPResponses);
    if (rv != SECSuccess) {
        goto loser;
    }
    rv = ssl_PopulateSignedCertTimestamps(sc, data->signedCertTimestamps);
    if (rv != SECSuccess) {
        goto loser;
    }
    rv = ssl_PopulateDelegatedCredential(sc, data->delegCred,
                                         data->delegCredPrivKey);
    if (rv != SECSuccess) {
        error_code = PORT_GetError();
        goto loser;
    }
    ssl_ClearMatchingCerts(ss, sc->authTypes, sc->namedCurve);
    PR_APPEND_LINK(&sc->link, &ss->serverCerts);
    return SECSuccess;

loser:
    ssl_FreeServerCert(sc);
    PORT_SetError(error_code);
    return SECFailure;
}

static SSLAuthType
ssl_GetEcdhAuthType(CERTCertificate *cert)
{
    SECOidTag sigTag = SECOID_GetAlgorithmTag(&cert->signature);
    switch (sigTag) {
        case SEC_OID_PKCS1_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_RSA_PSS_SIGNATURE:
        case SEC_OID_PKCS1_MD2_WITH_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_MD4_WITH_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_MD5_WITH_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_SHA1_WITH_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_SHA224_WITH_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_SHA256_WITH_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_SHA384_WITH_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_SHA512_WITH_RSA_ENCRYPTION:
            return ssl_auth_ecdh_rsa;
        case SEC_OID_ANSIX962_ECDSA_SHA1_SIGNATURE:
        case SEC_OID_ANSIX962_ECDSA_SHA224_SIGNATURE:
        case SEC_OID_ANSIX962_ECDSA_SHA256_SIGNATURE:
        case SEC_OID_ANSIX962_ECDSA_SHA384_SIGNATURE:
        case SEC_OID_ANSIX962_ECDSA_SHA512_SIGNATURE:
        case SEC_OID_ANSIX962_ECDSA_SIGNATURE_RECOMMENDED_DIGEST:
        case SEC_OID_ANSIX962_ECDSA_SIGNATURE_SPECIFIED_DIGEST:
            return ssl_auth_ecdh_ecdsa;
        default:
            return ssl_auth_null;
    }
}

/* This function examines the type of certificate and its key usage and
 * chooses which authTypes apply.  For some certificates
 * this can mean that multiple authTypes.
 *
 * If the targetAuthType is not ssl_auth_null, then only that type will be used.
 * If that choice is invalid, then this function will fail. */
static sslAuthTypeMask
ssl_GetCertificateAuthTypes(CERTCertificate *cert, SSLAuthType targetAuthType)
{
    sslAuthTypeMask authTypes = 0;
    SECOidTag tag;

    tag = SECOID_GetAlgorithmTag(&cert->subjectPublicKeyInfo.algorithm);
    switch (tag) {
        case SEC_OID_X500_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_RSA_ENCRYPTION:
            if (cert->keyUsage & KU_DIGITAL_SIGNATURE) {
                authTypes |= 1 << ssl_auth_rsa_sign;
            }

            if (cert->keyUsage & KU_KEY_ENCIPHERMENT) {
                /* If ku_sig=true we configure signature and encryption slots with the
                 * same cert. This is bad form, but there are enough dual-usage RSA
                 * certs that we can't really break by limiting this to one type. */
                authTypes |= 1 << ssl_auth_rsa_decrypt;
            }
            break;

        case SEC_OID_PKCS1_RSA_PSS_SIGNATURE:
            if (cert->keyUsage & KU_DIGITAL_SIGNATURE) {
                authTypes |= 1 << ssl_auth_rsa_pss;
            }
            break;

        case SEC_OID_ANSIX9_DSA_SIGNATURE:
            if (cert->keyUsage & KU_DIGITAL_SIGNATURE) {
                authTypes |= 1 << ssl_auth_dsa;
            }
            break;

        case SEC_OID_ANSIX962_EC_PUBLIC_KEY:
            if (cert->keyUsage & KU_DIGITAL_SIGNATURE) {
                authTypes |= 1 << ssl_auth_ecdsa;
            }
            /* Again, bad form to have dual usage and we don't prevent it. */
            if (cert->keyUsage & KU_KEY_ENCIPHERMENT) {
                authTypes |= 1 << ssl_GetEcdhAuthType(cert);
            }
            break;

        case SEC_OID_ML_DSA_44:
            if (cert->keyUsage & KU_DIGITAL_SIGNATURE) {
                authTypes |= 1 << ssl_auth_mldsa44;
            }
            break;

        case SEC_OID_ML_DSA_65:
            if (cert->keyUsage & KU_DIGITAL_SIGNATURE) {
                authTypes |= 1 << ssl_auth_mldsa65;
            }
            break;

        case SEC_OID_ML_DSA_87:
            if (cert->keyUsage & KU_DIGITAL_SIGNATURE) {
                authTypes |= 1 << ssl_auth_mldsa87;
            }
            break;

        default:
            break;
    }

    /* Check that we successfully picked an authType */
    if (targetAuthType != ssl_auth_null) {
        authTypes &= 1 << targetAuthType;
    }
    return authTypes;
}

/* This function adopts pubKey and destroys it if things go wrong. */
static sslKeyPair *
ssl_MakeKeyPairForCert(SECKEYPrivateKey *key, CERTCertificate *cert)
{
    sslKeyPair *keyPair = NULL;
    SECKEYPublicKey *pubKey = NULL;
    SECKEYPrivateKey *privKeyCopy = NULL;
    PK11SlotInfo *bestSlot;

    pubKey = CERT_ExtractPublicKey(cert);
    if (!pubKey) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return NULL;
    }

    if (SECKEY_GetPublicKeyType(pubKey) != SECKEY_GetPrivateKeyType(key)) {
        SECKEY_DestroyPublicKey(pubKey);
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    if (key->pkcs11Slot) {
        bestSlot = PK11_ReferenceSlot(key->pkcs11Slot);
        if (bestSlot) {
            privKeyCopy = PK11_CopyTokenPrivKeyToSessionPrivKey(bestSlot, key);
            PK11_FreeSlot(bestSlot);
        }
    }
    if (!privKeyCopy) {
        CK_MECHANISM_TYPE keyMech = PK11_MapSignKeyType(key->keyType);
        /* XXX Maybe should be bestSlotMultiple? */
        bestSlot = PK11_GetBestSlot(keyMech, NULL /* wincx */);
        if (bestSlot) {
            privKeyCopy = PK11_CopyTokenPrivKeyToSessionPrivKey(bestSlot, key);
            PK11_FreeSlot(bestSlot);
        }
    }
    if (!privKeyCopy) {
        privKeyCopy = SECKEY_CopyPrivateKey(key);
    }
    if (privKeyCopy) {
        keyPair = ssl_NewKeyPair(privKeyCopy, pubKey);
    }
    if (!keyPair) {
        if (privKeyCopy) {
            SECKEY_DestroyPrivateKey(privKeyCopy);
        }
        SECKEY_DestroyPublicKey(pubKey);
        PORT_SetError(SEC_ERROR_NO_MEMORY);
    }
    return keyPair;
}

/* Configure a certificate and private key.
 *
 * This function examines the certificate and key to determine the type (or
 * types) of authentication the certificate supports.  As long as certificates
 * are different (different authTypes and maybe keys in different ec groups),
 * then this function can be called multiple times.
 */
SECStatus
SSL_ConfigServerCert(PRFileDesc *fd, CERTCertificate *cert,
                     SECKEYPrivateKey *key,
                     const SSLExtraServerCertData *data, unsigned int data_len)
{
    sslSocket *ss;
    sslKeyPair *keyPair;
    SECStatus rv;
    SSLExtraServerCertData dataCopy = {
        ssl_auth_null, NULL, NULL, NULL, NULL, NULL
    };
    sslAuthTypeMask authTypes;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        return SECFailure;
    }

    if (!cert || !key) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (data) {
        if (data_len > sizeof(dataCopy)) {
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
            return SECFailure;
        }
        PORT_Memcpy(&dataCopy, data, data_len);
    }

    authTypes = ssl_GetCertificateAuthTypes(cert, dataCopy.authType);
    if (!authTypes) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    keyPair = ssl_MakeKeyPairForCert(key, cert);
    if (!keyPair) {
        return SECFailure;
    }

    rv = ssl_ConfigCert(ss, authTypes, cert, keyPair, &dataCopy);
    ssl_FreeKeyPair(keyPair);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    return SECSuccess;
}

/*******************************************************************/
/* Deprecated functions.
 *
 * The remainder of this file contains deprecated functions for server
 * certificate configuration.  These configure certificates incorrectly, but in
 * a way that allows old code to continue working without change.  All these
 * functions create certificate slots based on SSLKEAType values.  Some values
 * of SSLKEAType cause multiple certificates to be configured.
 */

SECStatus
SSL_ConfigSecureServer(PRFileDesc *fd, CERTCertificate *cert,
                       SECKEYPrivateKey *key, SSLKEAType kea)
{
    return SSL_ConfigSecureServerWithCertChain(fd, cert, NULL, key, kea);
}

/* This implements a limited check that is consistent with the checks performed
 * by older versions of NSS.  This is less rigorous than the checks in
 * ssl_ConfigCertByUsage(), only checking against the type of key and ignoring
 * things like usage. */
static PRBool
ssl_CertSuitableForAuthType(CERTCertificate *cert, sslAuthTypeMask authTypes)
{
    SECOidTag tag = SECOID_GetAlgorithmTag(&cert->subjectPublicKeyInfo.algorithm);
    sslAuthTypeMask mask = 0;
    switch (tag) {
        case SEC_OID_X500_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_RSA_ENCRYPTION:
            mask |= 1 << ssl_auth_rsa_decrypt;
            mask |= 1 << ssl_auth_rsa_sign;
            break;
        case SEC_OID_ANSIX9_DSA_SIGNATURE:
            mask |= 1 << ssl_auth_dsa;
            break;
        case SEC_OID_ANSIX962_EC_PUBLIC_KEY:
            mask |= 1 << ssl_auth_ecdsa;
            mask |= 1 << ssl_auth_ecdh_rsa;
            mask |= 1 << ssl_auth_ecdh_ecdsa;
            break;
        case SEC_OID_ML_DSA_44:
            mask |= 1 << ssl_auth_mldsa44;
            break;
        case SEC_OID_ML_DSA_65:
            mask |= 1 << ssl_auth_mldsa65;
            break;
        case SEC_OID_ML_DSA_87:
            mask |= 1 << ssl_auth_mldsa87;
            break;
        default:
            break;
    }
    PORT_Assert(authTypes);
    /* Simply test that no inappropriate auth types are set. */
    return (authTypes & ~mask) == 0;
}

/* Lookup a cert for the legacy configuration functions.  An exact match on
 * authTypes and ignoring namedCurve will ensure that values configured using
 * legacy functions are overwritten by other legacy functions. */
static sslServerCert *
ssl_FindCertWithMask(sslSocket *ss, sslAuthTypeMask authTypes)
{
    PRCList *cursor;

    for (cursor = PR_NEXT_LINK(&ss->serverCerts);
         cursor != &ss->serverCerts;
         cursor = PR_NEXT_LINK(cursor)) {
        sslServerCert *cert = (sslServerCert *)cursor;
        if (cert->authTypes == authTypes) {
            return cert;
        }
    }
    return NULL;
}

/* This finds an existing server cert in a matching slot that can be reused.
 * Failing that, it removes any other certs that might conflict and makes a new
 * server cert slot of the right type. */
static sslServerCert *
ssl_FindOrMakeCert(sslSocket *ss, sslAuthTypeMask authTypes)
{
    sslServerCert *sc;

    /* Reuse a perfect match.  Note that there is a problem here with use of
     * multiple EC certificates that have keys on different curves: these
     * deprecated functions will match the first found and overwrite that
     * certificate, potentially leaving the other values with a duplicate curve.
     * Configuring multiple EC certificates are only possible with the new
     * functions, so this is not something that is worth fixing.  */
    sc = ssl_FindCertWithMask(ss, authTypes);
    if (sc) {
        PR_REMOVE_LINK(&sc->link);
        return sc;
    }

    /* Ignore the namedCurve parameter. Like above, this means that legacy
     * functions will clobber values set with the new functions blindly. */
    ssl_ClearMatchingCerts(ss, authTypes, NULL);

    sc = ssl_NewServerCert();
    if (sc) {
        sc->authTypes = authTypes;
    }
    return sc;
}

static sslAuthTypeMask
ssl_KeaTypeToAuthTypeMask(SSLKEAType keaType)
{
    switch (keaType) {
        case ssl_kea_rsa:
            return (1 << ssl_auth_rsa_decrypt) |
                   (1 << ssl_auth_rsa_sign);

        case ssl_kea_dh:
            return 1 << ssl_auth_dsa;

        case ssl_kea_ecdh:
            return (1 << ssl_auth_ecdsa) |
                   (1 << ssl_auth_ecdh_rsa) |
                   (1 << ssl_auth_ecdh_ecdsa);

        default:
            PORT_SetError(SEC_ERROR_INVALID_ARGS);
    }
    return 0;
}

static SECStatus
ssl_AddCertChain(sslSocket *ss, CERTCertificate *cert,
                 const CERTCertificateList *certChainOpt,
                 SECKEYPrivateKey *key, sslAuthTypeMask authTypes)
{
    sslServerCert *sc;
    sslKeyPair *keyPair;
    SECStatus rv;
    PRErrorCode err = SEC_ERROR_NO_MEMORY;

    if (!ssl_CertSuitableForAuthType(cert, authTypes)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    sc = ssl_FindOrMakeCert(ss, authTypes);
    if (!sc) {
        goto loser;
    }

    rv = ssl_PopulateServerCert(sc, cert, certChainOpt);
    if (rv != SECSuccess) {
        goto loser;
    }

    keyPair = ssl_MakeKeyPairForCert(key, cert);
    if (!keyPair) {
        /* Error code is set by ssl_MakeKeyPairForCert */
        goto loser;
    }
    rv = ssl_PopulateKeyPair(sc, keyPair);
    ssl_FreeKeyPair(keyPair);
    if (rv != SECSuccess) {
        err = PORT_GetError();
        goto loser;
    }

    PR_APPEND_LINK(&sc->link, &ss->serverCerts);
    return SECSuccess;

loser:
    ssl_FreeServerCert(sc);
    PORT_SetError(err);
    return SECFailure;
}

/* Public deprecated function */
SECStatus
SSL_ConfigSecureServerWithCertChain(PRFileDesc *fd, CERTCertificate *cert,
                                    const CERTCertificateList *certChainOpt,
                                    SECKEYPrivateKey *key, SSLKEAType certType)
{
    sslSocket *ss;
    sslAuthTypeMask authTypes;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        return SECFailure;
    }

    if (!cert != !key) { /* Configure both, or neither */
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    authTypes = ssl_KeaTypeToAuthTypeMask(certType);
    if (!authTypes) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (!cert) {
        sslServerCert *sc = ssl_FindCertWithMask(ss, authTypes);
        if (sc) {
            (void)ssl_PopulateServerCert(sc, NULL, NULL);
            (void)ssl_PopulateKeyPair(sc, NULL);
            /* Leave the entry linked here because the old API expects that.
             * There might be OCSP stapling values or signed certificate
             * timestamps still present that will subsequently be used. */
        }
        return SECSuccess;
    }

    return ssl_AddCertChain(ss, cert, certChainOpt, key, authTypes);
}

/* Public deprecated function */
SECStatus
SSL_SetStapledOCSPResponses(PRFileDesc *fd, const SECItemArray *responses,
                            SSLKEAType certType)
{
    sslSocket *ss;
    sslServerCert *sc;
    sslAuthTypeMask authTypes;
    SECStatus rv;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SetStapledOCSPResponses",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    authTypes = ssl_KeaTypeToAuthTypeMask(certType);
    if (!authTypes) {
        SSL_DBG(("%d: SSL[%d]: invalid cert type in SSL_SetStapledOCSPResponses",
                 SSL_GETPID(), fd));
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (!responses) {
        sc = ssl_FindCertWithMask(ss, authTypes);
        if (sc) {
            (void)ssl_PopulateOCSPResponses(sc, NULL);
        }
        return SECSuccess;
    }

    sc = ssl_FindOrMakeCert(ss, authTypes);
    if (!sc) {
        return SECFailure;
    }

    rv = ssl_PopulateOCSPResponses(sc, responses);
    if (rv == SECSuccess) {
        PR_APPEND_LINK(&sc->link, &ss->serverCerts);
    } else {
        ssl_FreeServerCert(sc);
    }
    return rv;
}

/* Public deprecated function */
SECStatus
SSL_SetSignedCertTimestamps(PRFileDesc *fd, const SECItem *scts,
                            SSLKEAType certType)
{
    sslSocket *ss;
    sslServerCert *sc;
    sslAuthTypeMask authTypes;
    SECStatus rv;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SetSignedCertTimestamps",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    authTypes = ssl_KeaTypeToAuthTypeMask(certType);
    if (!authTypes) {
        SSL_DBG(("%d: SSL[%d]: invalid cert type in SSL_SetSignedCertTimestamps",
                 SSL_GETPID(), fd));
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (!scts) {
        sc = ssl_FindCertWithMask(ss, authTypes);
        if (sc) {
            (void)ssl_PopulateSignedCertTimestamps(sc, NULL);
        }
        return SECSuccess;
    }

    sc = ssl_FindOrMakeCert(ss, authTypes);
    if (!sc) {
        return SECFailure;
    }

    rv = ssl_PopulateSignedCertTimestamps(sc, scts);
    if (rv == SECSuccess) {
        PR_APPEND_LINK(&sc->link, &ss->serverCerts);
    } else {
        ssl_FreeServerCert(sc);
    }
    return rv;
}

/* Public deprecated function. */
SSLKEAType
NSS_FindCertKEAType(CERTCertificate *cert)
{
    int tag;

    if (!cert)
        return ssl_kea_null;

    tag = SECOID_GetAlgorithmTag(&(cert->subjectPublicKeyInfo.algorithm));
    switch (tag) {
        case SEC_OID_X500_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_RSA_ENCRYPTION:
            return ssl_kea_rsa;
        case SEC_OID_ANSIX9_DSA_SIGNATURE: /* hah, signature, not a key? */
        case SEC_OID_X942_DIFFIE_HELMAN_KEY:
            return ssl_kea_dh;
        case SEC_OID_ANSIX962_EC_PUBLIC_KEY:
            return ssl_kea_ecdh;
        default:
            return ssl_kea_null;
    }
}

void
ssl_ClearPeerCertificate(sslSecurityInfo *sec)
{
    SECITEM_FreeItem(&sec->peerCertDER, PR_FALSE);
    SECKEY_DestroySubjectPublicKeyInfo(sec->peerCertSPKI);
    sec->peerCertSPKI = NULL;
}

/* Install |derCert| as the peer's end-entity certificate, replacing any
 * previous one.
 *
 * The subjectPublicKeyInfo is parsed out now and kept alongside the DER, so
 * that the certificate is decoded exactly once per handshake however many
 * times the peer's key is needed afterwards.  It also means a certificate we
 * cannot get a key out of is rejected here, while the Certificate message is
 * being handled, rather than several messages later.
 */
SECStatus
ssl_SetPeerCertificate(sslSecurityInfo *sec, const SECItem *derCert)
{
    SECItem der = { siBuffer, NULL, 0 };
    CERTSubjectPublicKeyInfo *spki;

    spki = ssl_ExtractSPKIFromDER(derCert);
    if (!spki) {
        return SECFailure;
    }
    if (SECITEM_CopyItem(NULL, &der, derCert) != SECSuccess) {
        SECKEY_DestroySubjectPublicKeyInfo(spki);
        return SECFailure;
    }

    ssl_ClearPeerCertificate(sec);
    sec->peerCertDER = der;
    sec->peerCertSPKI = spki;
    return SECSuccess;
}
