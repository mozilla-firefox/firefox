/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * TLS 1.3 Protocol
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "sslt.h"
#include "stdarg.h"
#include "cert.h"
#include "ssl.h"
#include "keyhi.h"
#include "pk11func.h"
#include "prerr.h"
#include "secitem.h"
#include "secmod.h"
#include "sslimpl.h"
#include "sslproto.h"
#include "sslerr.h"
#include "ssl3exthandle.h"
#include "tls13hkdf.h"
#include "tls13con.h"
#include "tls13err.h"
#include "tls13ech.h"
#include "tls13exthandle.h"
#include "tls13hashstate.h"
#include "tls13subcerts.h"
#include "tls13psk.h"

static SECStatus tls13_SetCipherSpec(sslSocket *ss, PRUint16 epoch,
                                     SSLSecretDirection install,
                                     PRBool deleteSecret);
static SECStatus tls13_SendServerHelloSequence(sslSocket *ss);
static SECStatus tls13_SendEncryptedExtensions(sslSocket *ss);
static void tls13_SetKeyExchangeType(sslSocket *ss, const sslNamedGroupDef *group);
static SECStatus tls13_HandleClientKeyShare(sslSocket *ss,
                                            TLS13KeyShareEntry *peerShare);
static SECStatus tls13_SendHelloRetryRequest(
    sslSocket *ss, const sslNamedGroupDef *selectedGroup,
    const PRUint8 *token, unsigned int tokenLen);

static SECStatus tls13_HandleServerKeyShare(sslSocket *ss);
static SECStatus tls13_HandleEncryptedExtensions(sslSocket *ss, PRUint8 *b,
                                                 PRUint32 length);
static SECStatus tls13_SendCertificate(sslSocket *ss);
static SECStatus tls13_HandleCertificateDecode(
    sslSocket *ss, PRUint8 *b, PRUint32 length);
static SECStatus tls13_HandleCertificate(
    sslSocket *ss, PRUint8 *b, PRUint32 length, PRBool alreadyHashed);
static SECStatus tls13_ReinjectHandshakeTranscript(sslSocket *ss);
static SECStatus tls13_SendCertificateRequest(sslSocket *ss);
static SECStatus tls13_HandleCertificateRequest(sslSocket *ss, PRUint8 *b,
                                                PRUint32 length);
static SECStatus
tls13_SendCertificateVerify(sslSocket *ss, SECKEYPrivateKey *privKey);
static SECStatus tls13_HandleCertificateVerify(
    sslSocket *ss, PRUint8 *b, PRUint32 length);
static SECStatus tls13_RecoverWrappedSharedSecret(sslSocket *ss,
                                                  sslSessionID *sid);
static SECStatus
tls13_DeriveSecretWrap(sslSocket *ss, PK11SymKey *key,
                       const char *prefix,
                       const char *suffix,
                       const char *keylogLabel,
                       PK11SymKey **dest);
SECStatus
tls13_DeriveSecret(sslSocket *ss, PK11SymKey *key,
                   const char *label,
                   unsigned int labelLen,
                   const SSL3Hashes *hashes,
                   PK11SymKey **dest,
                   SSLHashType hash);
static SECStatus tls13_SendEndOfEarlyData(sslSocket *ss);
static SECStatus tls13_HandleEndOfEarlyData(sslSocket *ss, const PRUint8 *b,
                                            PRUint32 length);
static SECStatus tls13_MaybeHandleSuppressedEndOfEarlyData(sslSocket *ss);
static SECStatus tls13_SendFinished(sslSocket *ss, PK11SymKey *baseKey);
static SECStatus tls13_ComputePskBinderHash(sslSocket *ss, PRUint8 *b, size_t length,
                                            SSL3Hashes *hashes, SSLHashType type);
static SECStatus tls13_VerifyFinished(sslSocket *ss, SSLHandshakeType message,
                                      PK11SymKey *secret,
                                      PRUint8 *b, PRUint32 length,
                                      const SSL3Hashes *hashes);
static SECStatus tls13_ClientHandleFinished(sslSocket *ss,
                                            PRUint8 *b, PRUint32 length);
static SECStatus tls13_ServerHandleFinished(sslSocket *ss,
                                            PRUint8 *b, PRUint32 length);
static SECStatus tls13_SendNewSessionTicket(sslSocket *ss,
                                            const PRUint8 *appToken,
                                            unsigned int appTokenLen);
static SECStatus tls13_HandleNewSessionTicket(sslSocket *ss, PRUint8 *b,
                                              PRUint32 length);
static SECStatus tls13_ComputeEarlySecretsWithPsk(sslSocket *ss);
static SECStatus tls13_ComputeHandshakeSecrets(sslSocket *ss);
static SECStatus tls13_ComputeApplicationSecrets(sslSocket *ss);
static SECStatus tls13_ComputeFinalSecrets(sslSocket *ss);
static SECStatus tls13_ComputeFinished(
    sslSocket *ss, PK11SymKey *baseKey, SSLHashType hashType,
    const SSL3Hashes *hashes, PRBool sending, PRUint8 *output,
    unsigned int *outputLen, unsigned int maxOutputLen);
static SECStatus tls13_SendClientSecondRound(sslSocket *ss);
static SECStatus tls13_SendClientSecondFlight(sslSocket *ss);
static SECStatus tls13_FinishHandshake(sslSocket *ss);

const char kHkdfLabelClient[] = "c";
const char kHkdfLabelServer[] = "s";
const char kHkdfLabelDerivedSecret[] = "derived";
const char kHkdfLabelResPskBinderKey[] = "res binder";
const char kHkdfLabelExtPskBinderKey[] = "ext binder";
const char kHkdfLabelEarlyTrafficSecret[] = "e traffic";
const char kHkdfLabelEarlyExporterSecret[] = "e exp master";
const char kHkdfLabelHandshakeTrafficSecret[] = "hs traffic";
const char kHkdfLabelApplicationTrafficSecret[] = "ap traffic";
const char kHkdfLabelFinishedSecret[] = "finished";
const char kHkdfLabelResumptionMasterSecret[] = "res master";
const char kHkdfLabelExporterMasterSecret[] = "exp master";
const char kHkdfLabelResumption[] = "resumption";
const char kHkdfLabelTrafficUpdate[] = "traffic upd";
const char kHkdfPurposeKey[] = "key";
const char kHkdfPurposeSn[] = "sn";
const char kHkdfPurposeIv[] = "iv";

const char keylogLabelClientEarlyTrafficSecret[] = "CLIENT_EARLY_TRAFFIC_SECRET";
const char keylogLabelClientHsTrafficSecret[] = "CLIENT_HANDSHAKE_TRAFFIC_SECRET";
const char keylogLabelServerHsTrafficSecret[] = "SERVER_HANDSHAKE_TRAFFIC_SECRET";
const char keylogLabelClientTrafficSecret[] = "CLIENT_TRAFFIC_SECRET_0";
const char keylogLabelServerTrafficSecret[] = "SERVER_TRAFFIC_SECRET_0";
const char keylogLabelEarlyExporterSecret[] = "EARLY_EXPORTER_SECRET";
const char keylogLabelExporterSecret[] = "EXPORTER_SECRET";

/* Belt and suspenders in case we ever add a TLS 1.4. */
PR_STATIC_ASSERT(SSL_LIBRARY_VERSION_MAX_SUPPORTED <=
                 SSL_LIBRARY_VERSION_TLS_1_3);

void
tls13_FatalError(sslSocket *ss, PRErrorCode prError, SSL3AlertDescription desc)
{
    PORT_Assert(desc != internal_error); /* These should never happen */
    (void)SSL3_SendAlert(ss, alert_fatal, desc);
    PORT_SetError(prError);
}

#ifdef TRACE
#define STATE_CASE(a) \
    case a:           \
        return #a
static char *
tls13_HandshakeState(SSL3WaitState st)
{
    switch (st) {
        STATE_CASE(idle_handshake);
        STATE_CASE(wait_client_hello);
        STATE_CASE(wait_end_of_early_data);
        STATE_CASE(wait_client_cert);
        STATE_CASE(wait_client_key);
        STATE_CASE(wait_cert_verify);
        STATE_CASE(wait_change_cipher);
        STATE_CASE(wait_finished);
        STATE_CASE(wait_server_hello);
        STATE_CASE(wait_certificate_status);
        STATE_CASE(wait_server_cert);
        STATE_CASE(wait_server_key);
        STATE_CASE(wait_cert_request);
        STATE_CASE(wait_hello_done);
        STATE_CASE(wait_new_session_ticket);
        STATE_CASE(wait_encrypted_extensions);
        default:
            break;
    }
    PORT_Assert(0);
    return "unknown";
}
#endif

#define TLS13_WAIT_STATE_MASK 0x80

#define TLS13_BASE_WAIT_STATE(ws) (ws & ~TLS13_WAIT_STATE_MASK)
/* We don't mask idle_handshake because other parts of the code use it*/
#define TLS13_WAIT_STATE(ws) (((ws == idle_handshake) || (ws == wait_server_hello)) ? ws : ws | TLS13_WAIT_STATE_MASK)
#define TLS13_CHECK_HS_STATE(ss, err, ...)                          \
    tls13_CheckHsState(ss, err, #err, __func__, __FILE__, __LINE__, \
                       __VA_ARGS__,                                 \
                       wait_invalid)
void
tls13_SetHsState(sslSocket *ss, SSL3WaitState ws,
                 const char *func, const char *file, int line)
{
#ifdef TRACE
    const char *new_state_name =
        tls13_HandshakeState(ws);

    SSL_TRC(3, ("%d: TLS13[%d]: %s state change from %s->%s in %s (%s:%d)",
                SSL_GETPID(), ss->fd, SSL_ROLE(ss),
                tls13_HandshakeState(TLS13_BASE_WAIT_STATE(ss->ssl3.hs.ws)),
                new_state_name,
                func, file, line));
#endif

    ss->ssl3.hs.ws = TLS13_WAIT_STATE(ws);
}

static PRBool
tls13_InHsStateV(sslSocket *ss, va_list ap)
{
    SSL3WaitState ws;

    while ((ws = va_arg(ap, SSL3WaitState)) != wait_invalid) {
        if (TLS13_WAIT_STATE(ws) == ss->ssl3.hs.ws) {
            return PR_TRUE;
        }
    }
    return PR_FALSE;
}

PRBool
tls13_InHsState(sslSocket *ss, ...)
{
    PRBool found;
    va_list ap;

    va_start(ap, ss);
    found = tls13_InHsStateV(ss, ap);
    va_end(ap);

    return found;
}

static SECStatus
tls13_CheckHsState(sslSocket *ss, int err, const char *error_name,
                   const char *func, const char *file, int line,
                   ...)
{
    va_list ap;
    va_start(ap, line);
    if (tls13_InHsStateV(ss, ap)) {
        va_end(ap);
        return SECSuccess;
    }
    va_end(ap);

    SSL_TRC(3, ("%d: TLS13[%d]: error %s state is (%s) at %s (%s:%d)",
                SSL_GETPID(), ss->fd,
                error_name,
                tls13_HandshakeState(TLS13_BASE_WAIT_STATE(ss->ssl3.hs.ws)),
                func, file, line));
    tls13_FatalError(ss, err, unexpected_message);
    return SECFailure;
}

PRBool
tls13_IsPostHandshake(const sslSocket *ss)
{
    return ss->version >= SSL_LIBRARY_VERSION_TLS_1_3 && ss->firstHsDone;
}

SSLHashType
tls13_GetHashForCipherSuite(ssl3CipherSuite suite)
{
    const ssl3CipherSuiteDef *cipherDef =
        ssl_LookupCipherSuiteDef(suite);
    PORT_Assert(cipherDef);
    if (!cipherDef) {
        return ssl_hash_none;
    }
    return cipherDef->prf_hash;
}

SSLHashType
tls13_GetHash(const sslSocket *ss)
{
    /* suite_def may not be set yet when doing EPSK 0-Rtt. */
    if (!ss->ssl3.hs.suite_def) {
        if (ss->xtnData.selectedPsk) {
            return ss->xtnData.selectedPsk->hash;
        }
        /* This should never happen. */
        PORT_Assert(0);
        return ssl_hash_none;
    }

    /* All TLS 1.3 cipher suites must have an explict PRF hash. */
    PORT_Assert(ss->ssl3.hs.suite_def->prf_hash != ssl_hash_none);
    return ss->ssl3.hs.suite_def->prf_hash;
}

SECStatus
tls13_GetHashAndCipher(PRUint16 version, PRUint16 cipherSuite,
                       SSLHashType *hash, const ssl3BulkCipherDef **cipher)
{
    if (version < SSL_LIBRARY_VERSION_TLS_1_3) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    // Lookup and check the suite.
    SSLVersionRange vrange = { version, version };
    if (!ssl3_CipherSuiteAllowedForVersionRange(cipherSuite, &vrange)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    const ssl3CipherSuiteDef *suiteDef = ssl_LookupCipherSuiteDef(cipherSuite);
    const ssl3BulkCipherDef *cipherDef = ssl_GetBulkCipherDef(suiteDef);
    if (cipherDef->type != type_aead) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    *hash = suiteDef->prf_hash;
    if (cipher != NULL) {
        *cipher = cipherDef;
    }
    return SECSuccess;
}

unsigned int
tls13_GetHashSizeForHash(SSLHashType hash)
{
    switch (hash) {
        case ssl_hash_sha256:
            return 32;
        case ssl_hash_sha384:
            return 48;
        default:
            PORT_Assert(0);
    }
    return 32;
}

unsigned int
tls13_GetHashSize(const sslSocket *ss)
{
    return tls13_GetHashSizeForHash(tls13_GetHash(ss));
}

static CK_MECHANISM_TYPE
tls13_GetHmacMechanismFromHash(SSLHashType hashType)
{
    switch (hashType) {
        case ssl_hash_sha256:
            return CKM_SHA256_HMAC;
        case ssl_hash_sha384:
            return CKM_SHA384_HMAC;
        default:
            PORT_Assert(0);
    }
    return CKM_SHA256_HMAC;
}

static CK_MECHANISM_TYPE
tls13_GetHmacMechanism(const sslSocket *ss)
{
    return tls13_GetHmacMechanismFromHash(tls13_GetHash(ss));
}

SECStatus
tls13_ComputeHash(sslSocket *ss, SSL3Hashes *hashes,
                  const PRUint8 *buf, unsigned int len,
                  SSLHashType hash)
{
    SECStatus rv;

    rv = PK11_HashBuf(ssl3_HashTypeToOID(hash), hashes->u.raw, buf, len);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        return SECFailure;
    }
    hashes->len = tls13_GetHashSizeForHash(hash);

    return SECSuccess;
}

static SECStatus
tls13_CreateKEMKeyPair(sslSocket *ss, const sslNamedGroupDef *groupDef,
                       sslKeyPair **outKeyPair)
{
    PORT_Assert(groupDef);

    sslKeyPair *keyPair = NULL;
    SECKEYPrivateKey *privKey = NULL;
    SECKEYPublicKey *pubKey = NULL;
    CK_MECHANISM_TYPE mechanism;
    CK_NSS_KEM_PARAMETER_SET_TYPE paramSet;

    switch (groupDef->name) {
        case ssl_grp_kem_xyber768d00:
            mechanism = CKM_NSS_KYBER_KEY_PAIR_GEN;
            paramSet = CKP_NSS_KYBER_768_ROUND3;
            break;
        case ssl_grp_kem_mlkem768x25519:
            mechanism = CKM_NSS_ML_KEM_KEY_PAIR_GEN;
            paramSet = CKP_NSS_ML_KEM_768;
            break;
        default:
            PORT_Assert(0);
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
    }

    PK11SlotInfo *slot = PK11_GetBestSlot(mechanism, ss->pkcs11PinArg);
    if (!slot) {
        goto loser;
    }

    privKey = PK11_GenerateKeyPairWithOpFlags(slot, mechanism,
                                              &paramSet, &pubKey, PK11_ATTR_SESSION | PK11_ATTR_INSENSITIVE | PK11_ATTR_PUBLIC,
                                              CKF_DERIVE, CKF_DERIVE, ss->pkcs11PinArg);

    if (!privKey) {
        privKey = PK11_GenerateKeyPairWithOpFlags(slot, mechanism,
                                                  &paramSet, &pubKey, PK11_ATTR_SESSION | PK11_ATTR_SENSITIVE | PK11_ATTR_PRIVATE,
                                                  CKF_DERIVE, CKF_DERIVE, ss->pkcs11PinArg);
    }

    PK11_FreeSlot(slot);
    if (!privKey || !pubKey) {
        goto loser;
    }

    keyPair = ssl_NewKeyPair(privKey, pubKey);
    if (!keyPair) {
        goto loser;
    }

    SSL_TRC(50, ("%d: SSL[%d]: Create Kyber ephemeral key %d",
                 SSL_GETPID(), ss ? ss->fd : NULL, groupDef->name));
    PRINT_BUF(50, (ss, "Public Key", pubKey->u.kyber.publicValue.data,
                   pubKey->u.kyber.publicValue.len));
#ifdef TRACE
    if (ssl_trace >= 50) {
        SECItem d = { siBuffer, NULL, 0 };
        SECStatus rv = PK11_ReadRawAttribute(PK11_TypePrivKey, privKey, CKA_VALUE, &d);
        if (rv == SECSuccess) {
            PRINT_BUF(50, (ss, "Private Key", d.data, d.len));
            SECITEM_FreeItem(&d, PR_FALSE);
        } else {
            SSL_TRC(50, ("Error extracting private key"));
        }
    }
#endif

    *outKeyPair = keyPair;
    return SECSuccess;

loser:
    SECKEY_DestroyPrivateKey(privKey);
    SECKEY_DestroyPublicKey(pubKey);
    ssl_MapLowLevelError(SEC_ERROR_KEYGEN_FAIL);
    return SECFailure;
}

SECStatus
tls13_CreateKeyShare(sslSocket *ss, const sslNamedGroupDef *groupDef,
                     sslEphemeralKeyPair **outKeyPair)
{
    SECStatus rv;
    const ssl3DHParams *params;
    sslEphemeralKeyPair *keyPair = NULL;

    PORT_Assert(groupDef);
    switch (groupDef->keaType) {
        case ssl_kea_ecdh_hybrid:
            if (groupDef->name != ssl_grp_kem_xyber768d00 && groupDef->name != ssl_grp_kem_mlkem768x25519) {
                PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
                return SECFailure;
            }
            const sslNamedGroupDef *x25519 = ssl_LookupNamedGroup(ssl_grp_ec_curve25519);
            sslEphemeralKeyPair *x25519Pair = ssl_LookupEphemeralKeyPair(ss, x25519);
            if (x25519Pair) {
                keyPair = ssl_CopyEphemeralKeyPair(x25519Pair);
            }
            if (!keyPair) {
                rv = ssl_CreateECDHEphemeralKeyPair(ss, x25519, &keyPair);
                if (rv != SECSuccess) {
                    return SECFailure;
                }
            }
            keyPair->group = groupDef;
            break;
        case ssl_kea_ecdh:
            if (groupDef->name == ssl_grp_ec_curve25519) {
                sslEphemeralKeyPair *hybridPair = ssl_LookupEphemeralKeyPair(ss, ssl_LookupNamedGroup(ssl_grp_kem_mlkem768x25519));
                if (!hybridPair) {
                    hybridPair = ssl_LookupEphemeralKeyPair(ss, ssl_LookupNamedGroup(ssl_grp_kem_xyber768d00));
                }
                if (hybridPair) {
                    // We could use ssl_CopyEphemeralKeyPair here, but we would need to free
                    // the KEM components. We should pull this out into a utility function when
                    // we refactor to support multiple hybrid mechanisms.
                    keyPair = PORT_ZNew(sslEphemeralKeyPair);
                    if (!keyPair) {
                        return SECFailure;
                    }
                    PR_INIT_CLIST(&keyPair->link);
                    keyPair->group = groupDef;
                    keyPair->keys = ssl_GetKeyPairRef(hybridPair->keys);
                }
            }
            if (!keyPair) {
                rv = ssl_CreateECDHEphemeralKeyPair(ss, groupDef, &keyPair);
                if (rv != SECSuccess) {
                    return SECFailure;
                }
            }
            break;
        case ssl_kea_dh:
            params = ssl_GetDHEParams(groupDef);
            PORT_Assert(params->name != ssl_grp_ffdhe_custom);
            rv = ssl_CreateDHEKeyPair(groupDef, params, &keyPair);
            if (rv != SECSuccess) {
                return SECFailure;
            }
            break;
        default:
            PORT_Assert(0);
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
    }

    // If we're creating an ECDH + KEM hybrid share and we're the client, then
    // we still need to generate the KEM key pair. Otherwise we're done.
    if (groupDef->keaType == ssl_kea_ecdh_hybrid && !ss->sec.isServer) {
        rv = tls13_CreateKEMKeyPair(ss, groupDef, &keyPair->kemKeys);
        if (rv != SECSuccess) {
            ssl_FreeEphemeralKeyPair(keyPair);
            return SECFailure;
        }
    }

    *outKeyPair = keyPair;
    return SECSuccess;
}

SECStatus
tls13_AddKeyShare(sslSocket *ss, const sslNamedGroupDef *groupDef)
{
    sslEphemeralKeyPair *keyPair = NULL;
    SECStatus rv;

    rv = tls13_CreateKeyShare(ss, groupDef, &keyPair);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    PR_APPEND_LINK(&keyPair->link, &ss->ephemeralKeyPairs);
    return SECSuccess;
}

SECStatus
SSL_SendAdditionalKeyShares(PRFileDesc *fd, unsigned int count)
{
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    ss->additionalShares = count;
    return SECSuccess;
}

/*
 * Generate shares for ECDHE and FFDHE.  This picks the first enabled group of
 * the requisite type and creates a share for that.
 *
 * Called from ssl3_SendClientHello.
 */
SECStatus
tls13_SetupClientHello(sslSocket *ss, sslClientHelloType chType)
{
    unsigned int i;
    SSL3Statistics *ssl3stats = SSL_GetStatistics();
    NewSessionTicket *session_ticket = NULL;
    sslSessionID *sid = ss->sec.ci.sid;
    unsigned int numShares = 0;
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));

    rv = tls13_ClientSetupEch(ss, chType);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* Everything below here is only run on the first CH. */
    if (chType != client_hello_initial) {
        return SECSuccess;
    }

    rv = tls13_ClientGreaseSetup(ss);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* Select the first enabled group.
     * TODO(ekr@rtfm.com): be smarter about offering the group
     * that the other side negotiated if we are resuming. */
    PORT_Assert(PR_CLIST_IS_EMPTY(&ss->ephemeralKeyPairs));
    for (i = 0; i < SSL_NAMED_GROUP_COUNT; ++i) {
        if (!ss->namedGroupPreferences[i]) {
            continue;
        }
        rv = tls13_AddKeyShare(ss, ss->namedGroupPreferences[i]);
        if (rv != SECSuccess) {
            return SECFailure;
        }
        if (++numShares > ss->additionalShares) {
            break;
        }
    }

    if (PR_CLIST_IS_EMPTY(&ss->ephemeralKeyPairs)) {
        PORT_SetError(SSL_ERROR_NO_CIPHERS_SUPPORTED);
        return SECFailure;
    }

    /* Try to do stateless resumption, if we can. */
    if (sid->cached != never_cached &&
        sid->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
        /* The caller must be holding sid->u.ssl3.lock for reading. */
        session_ticket = &sid->u.ssl3.locked.sessionTicket;
        PORT_Assert(session_ticket && session_ticket->ticket.data);

        if (ssl_TicketTimeValid(ss, session_ticket)) {
            ss->statelessResume = PR_TRUE;
        }

        if (ss->statelessResume) {
            PORT_Assert(ss->sec.ci.sid);
            rv = tls13_RecoverWrappedSharedSecret(ss, ss->sec.ci.sid);
            if (rv != SECSuccess) {
                FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
                SSL_AtomicIncrementLong(&ssl3stats->sch_sid_cache_not_ok);
                ssl_UncacheSessionID(ss);
                ssl_FreeSID(ss->sec.ci.sid);
                ss->sec.ci.sid = NULL;
                return SECFailure;
            }

            ss->ssl3.hs.cipher_suite = ss->sec.ci.sid->u.ssl3.cipherSuite;
            rv = ssl3_SetupCipherSuite(ss, PR_FALSE);
            if (rv != SECSuccess) {
                FATAL_ERROR(ss, PORT_GetError(), internal_error);
                return SECFailure;
            }
            PORT_Assert(!PR_CLIST_IS_EMPTY(&ss->ssl3.hs.psks));
        }
    }

    /* Derive the binder keys if any PSKs. */
    if (!PR_CLIST_IS_EMPTY(&ss->ssl3.hs.psks)) {
        /* If an External PSK specified a suite, use that. */
        sslPsk *psk = (sslPsk *)PR_LIST_HEAD(&ss->ssl3.hs.psks);
        if (!ss->statelessResume &&
            psk->type == ssl_psk_external &&
            psk->zeroRttSuite != TLS_NULL_WITH_NULL_NULL) {
            ss->ssl3.hs.cipher_suite = psk->zeroRttSuite;
        }

        rv = tls13_ComputeEarlySecretsWithPsk(ss);
        if (rv != SECSuccess) {
            FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
            return SECFailure;
        }
    }

    return SECSuccess;
}

static SECStatus
tls13_ImportDHEKeyShare(SECKEYPublicKey *peerKey,
                        PRUint8 *b, PRUint32 length,
                        SECKEYPublicKey *pubKey)
{
    SECStatus rv;
    SECItem publicValue = { siBuffer, NULL, 0 };

    publicValue.data = b;
    publicValue.len = length;
    if (!ssl_IsValidDHEShare(&pubKey->u.dh.prime, &publicValue)) {
        PORT_SetError(SSL_ERROR_RX_MALFORMED_DHE_KEY_SHARE);
        return SECFailure;
    }

    peerKey->keyType = dhKey;
    rv = SECITEM_CopyItem(peerKey->arena, &peerKey->u.dh.prime,
                          &pubKey->u.dh.prime);
    if (rv != SECSuccess)
        return SECFailure;
    rv = SECITEM_CopyItem(peerKey->arena, &peerKey->u.dh.base,
                          &pubKey->u.dh.base);
    if (rv != SECSuccess)
        return SECFailure;
    rv = SECITEM_CopyItem(peerKey->arena, &peerKey->u.dh.publicValue,
                          &publicValue);
    if (rv != SECSuccess)
        return SECFailure;

    return SECSuccess;
}

static SECStatus
tls13_ImportKEMKeyShare(SECKEYPublicKey *peerKey, TLS13KeyShareEntry *entry)
{
    SECItem pk = { siBuffer, NULL, 0 };
    SECStatus rv;
    size_t expected_len;

    switch (entry->group->name) {
        case ssl_grp_kem_xyber768d00:
            expected_len = X25519_PUBLIC_KEY_BYTES + KYBER768_PUBLIC_KEY_BYTES;
            break;
        case ssl_grp_kem_mlkem768x25519:
            expected_len = X25519_PUBLIC_KEY_BYTES + KYBER768_PUBLIC_KEY_BYTES;
            break;
        default:
            PORT_SetError(SEC_ERROR_UNSUPPORTED_KEYALG);
            return SECFailure;
    }

    if (entry->key_exchange.len != expected_len) {
        PORT_SetError(SSL_ERROR_RX_MALFORMED_HYBRID_KEY_SHARE);
        return SECFailure;
    }

    switch (entry->group->name) {
        case ssl_grp_kem_xyber768d00:
            peerKey->keyType = kyberKey;
            peerKey->u.kyber.params = params_kyber768_round3;
            // key_exchange.data is `x25519 || kyber768`
            pk.data = entry->key_exchange.data + X25519_PUBLIC_KEY_BYTES;
            pk.len = KYBER768_PUBLIC_KEY_BYTES;
            break;
        case ssl_grp_kem_mlkem768x25519:
            peerKey->keyType = kyberKey;
            peerKey->u.kyber.params = params_ml_kem768;
            // key_exchange.data is `mlkem768 || x25519`
            pk.data = entry->key_exchange.data;
            pk.len = KYBER768_PUBLIC_KEY_BYTES;
            break;
        default:
            PORT_Assert(0);
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
    }

    rv = SECITEM_CopyItem(peerKey->arena, &peerKey->u.kyber.publicValue, &pk);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return SECFailure;
    }

    return SECSuccess;
}

static SECStatus
tls13_HandleKEMCiphertext(sslSocket *ss, TLS13KeyShareEntry *entry, sslKeyPair *keyPair, PK11SymKey **outKey)
{
    SECItem ct = { siBuffer, NULL, 0 };
    SECStatus rv;

    switch (entry->group->name) {
        case ssl_grp_kem_xyber768d00:
            if (entry->key_exchange.len != X25519_PUBLIC_KEY_BYTES + KYBER768_CIPHERTEXT_BYTES) {
                ssl_MapLowLevelError(SSL_ERROR_RX_MALFORMED_HYBRID_KEY_SHARE);
                return SECFailure;
            }
            ct.data = entry->key_exchange.data + X25519_PUBLIC_KEY_BYTES;
            ct.len = KYBER768_CIPHERTEXT_BYTES;
            break;
        case ssl_grp_kem_mlkem768x25519:
            if (entry->key_exchange.len != X25519_PUBLIC_KEY_BYTES + KYBER768_CIPHERTEXT_BYTES) {
                ssl_MapLowLevelError(SSL_ERROR_RX_MALFORMED_HYBRID_KEY_SHARE);
                return SECFailure;
            }
            ct.data = entry->key_exchange.data;
            ct.len = KYBER768_CIPHERTEXT_BYTES;
            break;
        default:
            PORT_Assert(0);
            ssl_MapLowLevelError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
    }

    rv = PK11_Decapsulate(keyPair->privKey, &ct, CKM_HKDF_DERIVE, PK11_ATTR_SESSION | PK11_ATTR_INSENSITIVE, CKF_DERIVE, outKey);
    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SSL_ERROR_KEY_EXCHANGE_FAILURE);
    }
    return rv;
}

static SECStatus
tls13_HandleKEMKey(sslSocket *ss,
                   TLS13KeyShareEntry *entry,
                   PK11SymKey **key,
                   SECItem **ciphertext)
{
    PORTCheapArenaPool arena;
    SECKEYPublicKey *peerKey;
    CK_OBJECT_HANDLE handle;
    SECStatus rv;

    PORT_InitCheapArena(&arena, DER_DEFAULT_CHUNKSIZE);
    peerKey = PORT_ArenaZNew(&arena.arena, SECKEYPublicKey);
    if (peerKey == NULL) {
        goto loser;
    }
    peerKey->arena = &arena.arena;
    peerKey->pkcs11Slot = NULL;
    peerKey->pkcs11ID = CK_INVALID_HANDLE;

    rv = tls13_ImportKEMKeyShare(peerKey, entry);
    if (rv != SECSuccess) {
        goto loser;
    }

    PK11SlotInfo *slot = PK11_GetBestSlot(CKM_NSS_KYBER, ss->pkcs11PinArg);
    if (!slot) {
        goto loser;
    }

    handle = PK11_ImportPublicKey(slot, peerKey, PR_FALSE);
    PK11_FreeSlot(slot); /* peerKey holds a slot reference on success. */
    if (handle == CK_INVALID_HANDLE) {
        goto loser;
    }

    rv = PK11_Encapsulate(peerKey,
                          CKM_HKDF_DERIVE, PK11_ATTR_SESSION | PK11_ATTR_INSENSITIVE | PK11_ATTR_PUBLIC,
                          CKF_DERIVE, key, ciphertext);

    /* Destroy the imported public key */
    PORT_Assert(peerKey->pkcs11Slot);
    PK11_DestroyObject(peerKey->pkcs11Slot, peerKey->pkcs11ID);
    PK11_FreeSlot(peerKey->pkcs11Slot);

    PORT_DestroyCheapArena(&arena);
    return rv;

loser:
    PORT_DestroyCheapArena(&arena);
    return SECFailure;
}

SECStatus
tls13_HandleKeyShare(sslSocket *ss,
                     TLS13KeyShareEntry *entry,
                     sslKeyPair *keyPair,
                     SSLHashType hash,
                     PK11SymKey **out)
{
    PORTCheapArenaPool arena;
    SECKEYPublicKey *peerKey;
    CK_MECHANISM_TYPE mechanism;
    PK11SymKey *key;
    unsigned char *ec_data;
    SECStatus rv;
    int keySize = 0;

    PORT_InitCheapArena(&arena, DER_DEFAULT_CHUNKSIZE);
    peerKey = PORT_ArenaZNew(&arena.arena, SECKEYPublicKey);
    if (peerKey == NULL) {
        goto loser;
    }
    peerKey->arena = &arena.arena;
    peerKey->pkcs11Slot = NULL;
    peerKey->pkcs11ID = CK_INVALID_HANDLE;

    switch (entry->group->keaType) {
        case ssl_kea_ecdh_hybrid:
            switch (entry->group->name) {
                case ssl_grp_kem_xyber768d00:
                    // x25519 share is at the beginning
                    ec_data = entry->key_exchange.len < X25519_PUBLIC_KEY_BYTES
                                  ? NULL
                                  : entry->key_exchange.data;
                    break;
                case ssl_grp_kem_mlkem768x25519:
                    // x25519 share is at the end
                    ec_data = entry->key_exchange.len < X25519_PUBLIC_KEY_BYTES
                                  ? NULL
                                  : entry->key_exchange.data + entry->key_exchange.len - X25519_PUBLIC_KEY_BYTES;
                    break;
                default:
                    ec_data = NULL;
                    break;
            }
            if (!ec_data) {
                PORT_SetError(SSL_ERROR_RX_MALFORMED_HYBRID_KEY_SHARE);
                goto loser;
            }
            rv = ssl_ImportECDHKeyShare(peerKey,
                                        ec_data,
                                        X25519_PUBLIC_KEY_BYTES,
                                        ssl_LookupNamedGroup(ssl_grp_ec_curve25519));
            mechanism = CKM_ECDH1_DERIVE;
            break;
        case ssl_kea_ecdh:
            rv = ssl_ImportECDHKeyShare(peerKey,
                                        entry->key_exchange.data,
                                        entry->key_exchange.len,
                                        entry->group);
            mechanism = CKM_ECDH1_DERIVE;
            break;
        case ssl_kea_dh:
            rv = tls13_ImportDHEKeyShare(peerKey,
                                         entry->key_exchange.data,
                                         entry->key_exchange.len,
                                         keyPair->pubKey);
            mechanism = CKM_DH_PKCS_DERIVE;
            keySize = peerKey->u.dh.publicValue.len;
            break;
        default:
            PORT_Assert(0);
            goto loser;
    }
    if (rv != SECSuccess) {
        goto loser;
    }

    key = PK11_PubDeriveWithKDF(
        keyPair->privKey, peerKey, PR_FALSE, NULL, NULL, mechanism,
        CKM_HKDF_DERIVE, CKA_DERIVE, keySize, CKD_NULL, NULL, NULL);
    if (!key) {
        ssl_MapLowLevelError(SSL_ERROR_KEY_EXCHANGE_FAILURE);
        goto loser;
    }

    *out = key;
    PORT_DestroyCheapArena(&arena);
    return SECSuccess;

loser:
    PORT_DestroyCheapArena(&arena);
    return SECFailure;
}

static PRBool
tls13_UseServerSecret(sslSocket *ss, SSLSecretDirection direction)
{
    return ss->sec.isServer == (direction == ssl_secret_write);
}

static PK11SymKey **
tls13_TrafficSecretRef(sslSocket *ss, SSLSecretDirection direction)
{
    if (tls13_UseServerSecret(ss, direction)) {
        return &ss->ssl3.hs.serverTrafficSecret;
    }
    return &ss->ssl3.hs.clientTrafficSecret;
}

SECStatus
tls13_UpdateTrafficKeys(sslSocket *ss, SSLSecretDirection direction)
{
    PK11SymKey **secret;
    PK11SymKey *updatedSecret;
    PRUint16 epoch;
    SECStatus rv;

    secret = tls13_TrafficSecretRef(ss, direction);
    rv = tls13_HkdfExpandLabel(*secret, tls13_GetHash(ss),
                               NULL, 0,
                               kHkdfLabelTrafficUpdate,
                               strlen(kHkdfLabelTrafficUpdate),
                               tls13_GetHmacMechanism(ss),
                               tls13_GetHashSize(ss),
                               ss->protocolVariant,
                               &updatedSecret);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    PK11_FreeSymKey(*secret);
    *secret = updatedSecret;

    ssl_GetSpecReadLock(ss);
    if (direction == ssl_secret_read) {
        epoch = ss->ssl3.crSpec->epoch;
    } else {
        epoch = ss->ssl3.cwSpec->epoch;
    }
    ssl_ReleaseSpecReadLock(ss);

    if (epoch == PR_UINT16_MAX) {
        /* Good chance that this is an overflow from too many updates. */
        FATAL_ERROR(ss, SSL_ERROR_TOO_MANY_KEY_UPDATES, internal_error);
        return SECFailure;
    }
    ++epoch;

    if (ss->secretCallback) {
        ss->secretCallback(ss->fd, epoch, direction, updatedSecret,
                           ss->secretCallbackArg);
    }
    rv = tls13_SetCipherSpec(ss, epoch, direction, PR_FALSE);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        return SECFailure;
    }
    return SECSuccess;
}

SECStatus
tls13_SendKeyUpdate(sslSocket *ss, tls13KeyUpdateRequest request, PRBool buffer)
{
    SECStatus rv;

    SSL_TRC(3, ("%d: TLS13[%d]: %s send key update, response %s",
                SSL_GETPID(), ss->fd, SSL_ROLE(ss),
                (request == update_requested) ? "requested"
                                              : "not requested"));

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(!ss->sec.isServer || !ss->ssl3.clientCertRequested);

    if (!tls13_IsPostHandshake(ss)) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    rv = TLS13_CHECK_HS_STATE(ss, SEC_ERROR_LIBRARY_FAILURE,
                              idle_handshake);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (IS_DTLS(ss)) {
        rv = dtls13_MaybeSendKeyUpdate(ss, request, buffer);
        if (rv != SECSuccess) {
            /* Error code set already. */
            return SECFailure;
        }
        return rv;
    }

    ssl_GetXmitBufLock(ss);
    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_key_update, 1);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        goto loser;
    }
    rv = ssl3_AppendHandshakeNumber(ss, request, 1);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        goto loser;
    }

    /* If we have been asked to buffer, then do so.  This allows us to coalesce
     * a KeyUpdate with a pending write. */
    rv = ssl3_FlushHandshake(ss, buffer ? ssl_SEND_FLAG_FORCE_INTO_BUFFER : 0);
    if (rv != SECSuccess) {
        goto loser; /* error code set by ssl3_FlushHandshake */
    }
    ssl_ReleaseXmitBufLock(ss);

    rv = tls13_UpdateTrafficKeys(ss, ssl_secret_write);
    if (rv != SECSuccess) {
        goto loser; /* error code set by tls13_UpdateTrafficKeys */
    }

    return SECSuccess;

loser:
    ssl_ReleaseXmitBufLock(ss);
    return SECFailure;
}

SECStatus
SSLExp_KeyUpdate(PRFileDesc *fd, PRBool requestUpdate)
{
    SECStatus rv;
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        return SECFailure;
    }

    if (!tls13_IsPostHandshake(ss)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (ss->ssl3.clientCertRequested) {
        PORT_SetError(PR_WOULD_BLOCK_ERROR);
        return SECFailure;
    }

    rv = TLS13_CHECK_HS_STATE(ss, SEC_ERROR_INVALID_ARGS,
                              idle_handshake);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    ssl_GetSSL3HandshakeLock(ss);
    rv = tls13_SendKeyUpdate(ss, requestUpdate ? update_requested : update_not_requested,
                             PR_FALSE /* don't buffer */);

    /* Remember that we are the ones that initiated this KeyUpdate. */
    if (rv == SECSuccess) {
        ss->ssl3.peerRequestedKeyUpdate = PR_FALSE;
    }
    ssl_ReleaseSSL3HandshakeLock(ss);
    return rv;
}

SECStatus
SSLExp_SetCertificateCompressionAlgorithm(PRFileDesc *fd, SSLCertificateCompressionAlgorithm alg)
{
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        return SECFailure; /* Code already set. */
    }

    ssl_GetSSL3HandshakeLock(ss);
    if (ss->ssl3.supportedCertCompressionAlgorithmsCount == MAX_SUPPORTED_CERTIFICATE_COMPRESSION_ALGS) {
        goto loser;
    }

    /* Reserved ID */
    if (alg.id == 0) {
        goto loser;
    }

    if (alg.encode == NULL && alg.decode == NULL) {
        goto loser;
    }

    /* Checking that we have not yet registed an algorithm with the same ID. */
    for (int i = 0; i < ss->ssl3.supportedCertCompressionAlgorithmsCount; i++) {
        if (ss->ssl3.supportedCertCompressionAlgorithms[i].id == alg.id) {
            goto loser;
        }
    }

    PORT_Memcpy(&ss->ssl3.supportedCertCompressionAlgorithms
                     [ss->ssl3.supportedCertCompressionAlgorithmsCount],
                &alg, sizeof(alg));
    ss->ssl3.supportedCertCompressionAlgorithmsCount += 1;
    ssl_ReleaseSSL3HandshakeLock(ss);
    return SECSuccess;

loser:
    PORT_SetError(SEC_ERROR_INVALID_ARGS);
    ssl_ReleaseSSL3HandshakeLock(ss);
    return SECFailure;
}

/*
 * enum {
 *     update_not_requested(0), update_requested(1), (255)
 * } KeyUpdateRequest;
 *
 * struct {
 *     KeyUpdateRequest request_update;
 * } KeyUpdate;
 */

/* If we're handing the DTLS1.3 message, we silently fail if there is a parsing problem. */
static SECStatus
tls13_HandleKeyUpdate(sslSocket *ss, PRUint8 *b, unsigned int length)
{
    SECStatus rv;
    PRUint32 update;

    SSL_TRC(3, ("%d: TLS13[%d]: %s handle key update",
                SSL_GETPID(), ss->fd, SSL_ROLE(ss)));

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (!tls13_IsPostHandshake(ss)) {
        FATAL_ERROR(ss, SSL_ERROR_RX_UNEXPECTED_KEY_UPDATE, unexpected_message);
        return SECFailure;
    }

    rv = TLS13_CHECK_HS_STATE(ss, SSL_ERROR_RX_UNEXPECTED_KEY_UPDATE,
                              idle_handshake);
    if (rv != SECSuccess) {
        /* We should never be idle_handshake prior to firstHsDone. */
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        return SECFailure;
    }

    rv = ssl3_ConsumeHandshakeNumber(ss, &update, 1, &b, &length);
    if (rv != SECSuccess) {
        return SECFailure; /* Error code set already. */
    }
    if (length != 0) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_KEY_UPDATE, decode_error);
        return SECFailure;
    }
    if (!(update == update_requested ||
          update == update_not_requested)) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_KEY_UPDATE, decode_error);
        return SECFailure;
    }

    if (IS_DTLS(ss)) {
        return dtls13_HandleKeyUpdate(ss, b, length, update);
    }

    rv = tls13_UpdateTrafficKeys(ss, ssl_secret_read);
    if (rv != SECSuccess) {
        return SECFailure; /* Error code set by tls13_UpdateTrafficKeys. */
    }

    if (update == update_requested) {
        PRBool sendUpdate;
        if (ss->ssl3.clientCertRequested) {
            /* Post-handshake auth is in progress; defer sending a key update. */
            ss->ssl3.hs.keyUpdateDeferred = PR_TRUE;
            ss->ssl3.hs.deferredKeyUpdateRequest = update_not_requested;
            sendUpdate = PR_FALSE;
        } else if (ss->ssl3.peerRequestedKeyUpdate) {
            /* Only send an update if we have sent with the current spec.  This
             * prevents us from being forced to crank forward pointlessly. */
            ssl_GetSpecReadLock(ss);
            sendUpdate = ss->ssl3.cwSpec->nextSeqNum > 0;
            ssl_ReleaseSpecReadLock(ss);
        } else {
            sendUpdate = PR_TRUE;
        }
        if (sendUpdate) {
            /* Respond immediately (don't buffer). */
            rv = tls13_SendKeyUpdate(ss, update_not_requested, PR_FALSE);
            if (rv != SECSuccess) {
                return SECFailure; /* Error already set. */
            }
        }
        ss->ssl3.peerRequestedKeyUpdate = PR_TRUE;
    }

    return SECSuccess;
}

SECStatus
SSLExp_SendCertificateRequest(PRFileDesc *fd)
{
    SECStatus rv;
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        return SECFailure;
    }

    /* Not supported. */
    if (IS_DTLS(ss)) {
        PORT_SetError(SSL_ERROR_FEATURE_NOT_SUPPORTED_FOR_VERSION);
        return SECFailure;
    }

    if (!tls13_IsPostHandshake(ss)) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (ss->ssl3.clientCertRequested) {
        PORT_SetError(PR_WOULD_BLOCK_ERROR);
        return SECFailure;
    }

    /* Disallow a CertificateRequest if this connection uses an external PSK. */
    if (ss->sec.authType == ssl_auth_psk) {
        PORT_SetError(SSL_ERROR_FEATURE_DISABLED);
        return SECFailure;
    }

    rv = TLS13_CHECK_HS_STATE(ss, SEC_ERROR_INVALID_ARGS,
                              idle_handshake);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (!ssl3_ExtensionNegotiated(ss, ssl_tls13_post_handshake_auth_xtn)) {
        PORT_SetError(SSL_ERROR_MISSING_POST_HANDSHAKE_AUTH_EXTENSION);
        return SECFailure;
    }

    ssl_GetSSL3HandshakeLock(ss);

    rv = tls13_SendCertificateRequest(ss);
    if (rv == SECSuccess) {
        ssl_GetXmitBufLock(ss);
        rv = ssl3_FlushHandshake(ss, 0);
        ssl_ReleaseXmitBufLock(ss);
        ss->ssl3.clientCertRequested = PR_TRUE;
    }

    ssl_ReleaseSSL3HandshakeLock(ss);
    return rv;
}

SECStatus
tls13_HandlePostHelloHandshakeMessage(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    if (ss->sec.isServer && ss->ssl3.hs.zeroRttIgnore != ssl_0rtt_ignore_none) {
        SSL_TRC(3, ("%d: TLS13[%d]: successfully decrypted handshake after "
                    "failed 0-RTT",
                    SSL_GETPID(), ss->fd));
        ss->ssl3.hs.zeroRttIgnore = ssl_0rtt_ignore_none;
    }

    /* TODO(ekr@rtfm.com): Would it be better to check all the states here? */
    switch (ss->ssl3.hs.msg_type) {
        case ssl_hs_certificate:
            return tls13_HandleCertificate(ss, b, length, PR_FALSE);
        case ssl_hs_compressed_certificate:
            return tls13_HandleCertificateDecode(ss, b, length);
        case ssl_hs_certificate_request:
            return tls13_HandleCertificateRequest(ss, b, length);

        case ssl_hs_certificate_verify:
            return tls13_HandleCertificateVerify(ss, b, length);

        case ssl_hs_encrypted_extensions:
            return tls13_HandleEncryptedExtensions(ss, b, length);

        case ssl_hs_new_session_ticket:
            return tls13_HandleNewSessionTicket(ss, b, length);

        case ssl_hs_finished:
            if (ss->sec.isServer) {
                return tls13_ServerHandleFinished(ss, b, length);
            } else {
                return tls13_ClientHandleFinished(ss, b, length);
            }

        case ssl_hs_end_of_early_data:
            return tls13_HandleEndOfEarlyData(ss, b, length);

        case ssl_hs_key_update:
            return tls13_HandleKeyUpdate(ss, b, length);

        default:
            FATAL_ERROR(ss, SSL_ERROR_RX_UNKNOWN_HANDSHAKE, unexpected_message);
            return SECFailure;
    }

    PORT_Assert(0); /* Unreached */
    return SECFailure;
}

static SECStatus
tls13_RecoverWrappedSharedSecret(sslSocket *ss, sslSessionID *sid)
{
    PK11SymKey *wrapKey; /* wrapping key */
    SECItem wrappedMS = { siBuffer, NULL, 0 };
    SSLHashType hashType;

    SSL_TRC(3, ("%d: TLS13[%d]: recovering static secret (%s)",
                SSL_GETPID(), ss->fd, SSL_ROLE(ss)));

    /* Now find the hash used as the PRF for the previous handshake. */
    hashType = tls13_GetHashForCipherSuite(sid->u.ssl3.cipherSuite);

    /* If we are the server, we compute the wrapping key, but if we
     * are the client, its coordinates are stored with the ticket. */
    if (ss->sec.isServer) {
        wrapKey = ssl3_GetWrappingKey(ss, NULL,
                                      sid->u.ssl3.masterWrapMech,
                                      ss->pkcs11PinArg);
    } else {
        PK11SlotInfo *slot = SECMOD_LookupSlot(sid->u.ssl3.masterModuleID,
                                               sid->u.ssl3.masterSlotID);
        if (!slot)
            return SECFailure;

        wrapKey = PK11_GetWrapKey(slot,
                                  sid->u.ssl3.masterWrapIndex,
                                  sid->u.ssl3.masterWrapMech,
                                  sid->u.ssl3.masterWrapSeries,
                                  ss->pkcs11PinArg);
        PK11_FreeSlot(slot);
    }
    if (!wrapKey) {
        return SECFailure;
    }

    wrappedMS.data = sid->u.ssl3.keys.wrapped_master_secret;
    wrappedMS.len = sid->u.ssl3.keys.wrapped_master_secret_len;

    PK11SymKey *unwrappedPsk = ssl_unwrapSymKey(wrapKey, sid->u.ssl3.masterWrapMech,
                                                NULL, &wrappedMS, CKM_SSL3_MASTER_KEY_DERIVE,
                                                CKA_DERIVE, tls13_GetHashSizeForHash(hashType),
                                                CKF_SIGN | CKF_VERIFY, ss->pkcs11PinArg);
    PK11_FreeSymKey(wrapKey);
    if (!unwrappedPsk) {
        return SECFailure;
    }
    sslPsk *rpsk = tls13_MakePsk(unwrappedPsk, ssl_psk_resume, hashType, NULL);
    if (!rpsk) {
        PK11_FreeSymKey(unwrappedPsk);
        return SECFailure;
    }
    if (sid->u.ssl3.locked.sessionTicket.flags & ticket_allow_early_data) {
        rpsk->maxEarlyData = sid->u.ssl3.locked.sessionTicket.max_early_data_size;
        rpsk->zeroRttSuite = sid->u.ssl3.cipherSuite;
    }
    PRINT_KEY(50, (ss, "Recovered RMS", rpsk->key));
    PORT_Assert(PR_CLIST_IS_EMPTY(&ss->ssl3.hs.psks) ||
                ((sslPsk *)PR_LIST_HEAD(&ss->ssl3.hs.psks))->type != ssl_psk_resume);

    if (ss->sec.isServer) {
        /* In server, we couldn't select the RPSK in the extension handler
         * since it was not unwrapped yet. We're committed now, so select
         * it and add it to the list (to ensure it is freed). */
        ss->xtnData.selectedPsk = rpsk;
    }
    PR_APPEND_LINK(&rpsk->link, &ss->ssl3.hs.psks);

    return SECSuccess;
}

/* Key Derivation Functions.
 *
 *                 0
 *                 |
 *                 v
 *   PSK ->  HKDF-Extract = Early Secret
 *                 |
 *                 +-----> Derive-Secret(., "ext binder" | "res binder", "")
 *                 |                     = binder_key
 *                 |
 *                 +-----> Derive-Secret(., "c e traffic",
 *                 |                     ClientHello)
 *                 |                     = client_early_traffic_secret
 *                 |
 *                 +-----> Derive-Secret(., "e exp master",
 *                 |                     ClientHello)
 *                 |                     = early_exporter_secret
 *                 v
 *           Derive-Secret(., "derived", "")
 *                 |
 *                 v
 *(EC)DHE -> HKDF-Extract = Handshake Secret
 *                 |
 *                 +-----> Derive-Secret(., "c hs traffic",
 *                 |                     ClientHello...ServerHello)
 *                 |                     = client_handshake_traffic_secret
 *                 |
 *                 +-----> Derive-Secret(., "s hs traffic",
 *                 |                     ClientHello...ServerHello)
 *                 |                     = server_handshake_traffic_secret
 *                 v
 *           Derive-Secret(., "derived", "")
 *                 |
 *                 v
 *      0 -> HKDF-Extract = Master Secret
 *                 |
 *                 +-----> Derive-Secret(., "c ap traffic",
 *                 |                     ClientHello...Server Finished)
 *                 |                     = client_traffic_secret_0
 *                 |
 *                 +-----> Derive-Secret(., "s ap traffic",
 *                 |                     ClientHello...Server Finished)
 *                 |                     = server_traffic_secret_0
 *                 |
 *                 +-----> Derive-Secret(., "exp master",
 *                 |                     ClientHello...Server Finished)
 *                 |                     = exporter_secret
 *                 |
 *                 +-----> Derive-Secret(., "res master",
 *                                       ClientHello...Client Finished)
 *                                       = resumption_master_secret
 *
 */
static SECStatus
tls13_ComputeEarlySecretsWithPsk(sslSocket *ss)
{
    SECStatus rv;

    SSL_TRC(5, ("%d: TLS13[%d]: compute early secrets (%s)",
                SSL_GETPID(), ss->fd, SSL_ROLE(ss)));

    PORT_Assert(!ss->ssl3.hs.currentSecret);
    sslPsk *psk = NULL;

    if (ss->sec.isServer) {
        psk = ss->xtnData.selectedPsk;
    } else {
        /* Client to use the first PSK for early secrets. */
        PORT_Assert(!PR_CLIST_IS_EMPTY(&ss->ssl3.hs.psks));
        psk = (sslPsk *)PR_LIST_HEAD(&ss->ssl3.hs.psks);
    }
    PORT_Assert(psk && psk->key);
    PORT_Assert(psk->hash != ssl_hash_none);

    PK11SymKey *earlySecret = NULL;
    rv = tls13_HkdfExtract(NULL, psk->key, psk->hash, &earlySecret);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* No longer need the raw input key */
    PK11_FreeSymKey(psk->key);
    psk->key = NULL;
    const char *label = (psk->type == ssl_psk_resume) ? kHkdfLabelResPskBinderKey : kHkdfLabelExtPskBinderKey;
    rv = tls13_DeriveSecretNullHash(ss, earlySecret,
                                    label, strlen(label),
                                    &psk->binderKey, psk->hash);
    if (rv != SECSuccess) {
        PK11_FreeSymKey(earlySecret);
        return SECFailure;
    }
    ss->ssl3.hs.currentSecret = earlySecret;

    return SECSuccess;
}

/* This derives the early traffic and early exporter secrets. */
static SECStatus
tls13_DeriveEarlySecrets(sslSocket *ss)
{
    SECStatus rv;
    PORT_Assert(ss->ssl3.hs.currentSecret);
    rv = tls13_DeriveSecretWrap(ss, ss->ssl3.hs.currentSecret,
                                kHkdfLabelClient,
                                kHkdfLabelEarlyTrafficSecret,
                                keylogLabelClientEarlyTrafficSecret,
                                &ss->ssl3.hs.clientEarlyTrafficSecret);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (ss->secretCallback) {
        ss->secretCallback(ss->fd, (PRUint16)TrafficKeyEarlyApplicationData,
                           ss->sec.isServer ? ssl_secret_read : ssl_secret_write,
                           ss->ssl3.hs.clientEarlyTrafficSecret,
                           ss->secretCallbackArg);
    }

    rv = tls13_DeriveSecretWrap(ss, ss->ssl3.hs.currentSecret,
                                NULL, kHkdfLabelEarlyExporterSecret,
                                keylogLabelEarlyExporterSecret,
                                &ss->ssl3.hs.earlyExporterSecret);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    return SECSuccess;
}

static SECStatus
tls13_ComputeHandshakeSecret(sslSocket *ss)
{
    SECStatus rv;
    PK11SymKey *derivedSecret = NULL;
    PK11SymKey *newSecret = NULL;
    SSL_TRC(5, ("%d: TLS13[%d]: compute handshake secret (%s)",
                SSL_GETPID(), ss->fd, SSL_ROLE(ss)));

    /* If no PSK, generate the default early secret. */
    if (!ss->ssl3.hs.currentSecret) {
        PORT_Assert(!ss->xtnData.selectedPsk);
        rv = tls13_HkdfExtract(NULL, NULL,
                               tls13_GetHash(ss), &ss->ssl3.hs.currentSecret);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }
    PORT_Assert(ss->ssl3.hs.currentSecret);
    PORT_Assert(ss->ssl3.hs.dheSecret);

    /* Derive-Secret(., "derived", "") */
    rv = tls13_DeriveSecretNullHash(ss, ss->ssl3.hs.currentSecret,
                                    kHkdfLabelDerivedSecret,
                                    strlen(kHkdfLabelDerivedSecret),
                                    &derivedSecret, tls13_GetHash(ss));
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        return rv;
    }

    /* HKDF-Extract(ECDHE, .) = Handshake Secret */
    rv = tls13_HkdfExtract(derivedSecret, ss->ssl3.hs.dheSecret,
                           tls13_GetHash(ss), &newSecret);
    PK11_FreeSymKey(derivedSecret);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        return rv;
    }

    PK11_FreeSymKey(ss->ssl3.hs.currentSecret);
    ss->ssl3.hs.currentSecret = newSecret;
    return SECSuccess;
}

static SECStatus
tls13_ComputeHandshakeSecrets(sslSocket *ss)
{
    SECStatus rv;
    PK11SymKey *derivedSecret = NULL;
    PK11SymKey *newSecret = NULL;

    PK11_FreeSymKey(ss->ssl3.hs.dheSecret);
    ss->ssl3.hs.dheSecret = NULL;

    SSL_TRC(5, ("%d: TLS13[%d]: compute handshake secrets (%s)",
                SSL_GETPID(), ss->fd, SSL_ROLE(ss)));

    /* Now compute |*HsTrafficSecret| */
    rv = tls13_DeriveSecretWrap(ss, ss->ssl3.hs.currentSecret,
                                kHkdfLabelClient,
                                kHkdfLabelHandshakeTrafficSecret,
                                keylogLabelClientHsTrafficSecret,
                                &ss->ssl3.hs.clientHsTrafficSecret);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        return rv;
    }
    rv = tls13_DeriveSecretWrap(ss, ss->ssl3.hs.currentSecret,
                                kHkdfLabelServer,
                                kHkdfLabelHandshakeTrafficSecret,
                                keylogLabelServerHsTrafficSecret,
                                &ss->ssl3.hs.serverHsTrafficSecret);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        return rv;
    }

    if (ss->secretCallback) {
        SSLSecretDirection dir =
            ss->sec.isServer ? ssl_secret_read : ssl_secret_write;
        ss->secretCallback(ss->fd, (PRUint16)TrafficKeyHandshake, dir,
                           ss->ssl3.hs.clientHsTrafficSecret,
                           ss->secretCallbackArg);
        dir = ss->sec.isServer ? ssl_secret_write : ssl_secret_read;
        ss->secretCallback(ss->fd, (PRUint16)TrafficKeyHandshake, dir,
                           ss->ssl3.hs.serverHsTrafficSecret,
                           ss->secretCallbackArg);
    }

    SSL_TRC(5, ("%d: TLS13[%d]: compute master secret (%s)",
                SSL_GETPID(), ss->fd, SSL_ROLE(ss)));

    /* Crank HKDF forward to make master secret, which we
     * stuff in current secret. */
    rv = tls13_DeriveSecretNullHash(ss, ss->ssl3.hs.currentSecret,
                                    kHkdfLabelDerivedSecret,
                                    strlen(kHkdfLabelDerivedSecret),
                                    &derivedSecret, tls13_GetHash(ss));
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        return rv;
    }
    rv = tls13_HkdfExtract(derivedSecret,
                           NULL,
                           tls13_GetHash(ss),
                           &newSecret);
    PK11_FreeSymKey(derivedSecret);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    PK11_FreeSymKey(ss->ssl3.hs.currentSecret);
    ss->ssl3.hs.currentSecret = newSecret;

    return SECSuccess;
}

static SECStatus
tls13_ComputeApplicationSecrets(sslSocket *ss)
{
    SECStatus rv;

    rv = tls13_DeriveSecretWrap(ss, ss->ssl3.hs.currentSecret,
                                kHkdfLabelClient,
                                kHkdfLabelApplicationTrafficSecret,
                                keylogLabelClientTrafficSecret,
                                &ss->ssl3.hs.clientTrafficSecret);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    rv = tls13_DeriveSecretWrap(ss, ss->ssl3.hs.currentSecret,
                                kHkdfLabelServer,
                                kHkdfLabelApplicationTrafficSecret,
                                keylogLabelServerTrafficSecret,
                                &ss->ssl3.hs.serverTrafficSecret);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (ss->secretCallback) {
        SSLSecretDirection dir =
            ss->sec.isServer ? ssl_secret_read : ssl_secret_write;
        ss->secretCallback(ss->fd, (PRUint16)TrafficKeyApplicationData,
                           dir, ss->ssl3.hs.clientTrafficSecret,
                           ss->secretCallbackArg);
        dir = ss->sec.isServer ? ssl_secret_write : ssl_secret_read;
        ss->secretCallback(ss->fd, (PRUint16)TrafficKeyApplicationData,
                           dir, ss->ssl3.hs.serverTrafficSecret,
                           ss->secretCallbackArg);
    }

    rv = tls13_DeriveSecretWrap(ss, ss->ssl3.hs.currentSecret,
                                NULL, kHkdfLabelExporterMasterSecret,
                                keylogLabelExporterSecret,
                                &ss->ssl3.hs.exporterSecret);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    return SECSuccess;
}

static SECStatus
tls13_ComputeFinalSecrets(sslSocket *ss)
{
    SECStatus rv;

    PORT_Assert(!ss->ssl3.crSpec->masterSecret);
    PORT_Assert(!ss->ssl3.cwSpec->masterSecret);
    PORT_Assert(ss->ssl3.hs.currentSecret);
    rv = tls13_DeriveSecretWrap(ss, ss->ssl3.hs.currentSecret,
                                NULL, kHkdfLabelResumptionMasterSecret,
                                NULL,
                                &ss->ssl3.hs.resumptionMasterSecret);
    PK11_FreeSymKey(ss->ssl3.hs.currentSecret);
    ss->ssl3.hs.currentSecret = NULL;
    if (rv != SECSuccess) {
        return SECFailure;
    }

    return SECSuccess;
}

static void
tls13_RestoreCipherInfo(sslSocket *ss, sslSessionID *sid)
{
    /* Set these to match the cached value.
     * TODO(ekr@rtfm.com): Make a version with the "true" values.
     * Bug 1256137.
     */
    ss->sec.authType = sid->authType;
    ss->sec.authKeyBits = sid->authKeyBits;
    ss->sec.originalKeaGroup = ssl_LookupNamedGroup(sid->keaGroup);
    ss->sec.signatureScheme = sid->sigScheme;
}

/* Check whether resumption-PSK is allowed. */
static PRBool
tls13_CanResume(sslSocket *ss, const sslSessionID *sid)
{
    const sslServerCert *sc;

    if (!sid) {
        return PR_FALSE;
    }

    if (sid->version != ss->version) {
        return PR_FALSE;
    }

#ifdef UNSAFE_FUZZER_MODE
    /* When fuzzing, sid could contain garbage that will crash tls13_GetHashForCipherSuite.
     * Do a direct comparison of cipher suites.  This makes us refuse to resume when the
     * protocol allows it, but resumption is discretionary anyway. */
    if (sid->u.ssl3.cipherSuite != ss->ssl3.hs.cipher_suite) {
#else
    if (tls13_GetHashForCipherSuite(sid->u.ssl3.cipherSuite) != tls13_GetHashForCipherSuite(ss->ssl3.hs.cipher_suite)) {
#endif
        return PR_FALSE;
    }

    /* Server sids don't remember the server cert we previously sent, but they
     * do remember the type of certificate we originally used, so we can locate
     * it again, provided that the current ssl socket has had its server certs
     * configured the same as the previous one. */
    sc = ssl_FindServerCert(ss, sid->authType, sid->namedCurve);
    if (!sc || !sc->serverCert) {
        return PR_FALSE;
    }

    return PR_TRUE;
}

static PRBool
tls13_CanNegotiateZeroRtt(sslSocket *ss, const sslSessionID *sid)
{
    PORT_Assert(ss->ssl3.hs.zeroRttState == ssl_0rtt_sent);
    sslPsk *psk = ss->xtnData.selectedPsk;

    if (!ss->opt.enable0RttData) {
        return PR_FALSE;
    }
    if (!psk) {
        return PR_FALSE;
    }
    if (psk->zeroRttSuite == TLS_NULL_WITH_NULL_NULL) {
        return PR_FALSE;
    }
    if (!psk->maxEarlyData) {
        return PR_FALSE;
    }
    if (ss->ssl3.hs.cipher_suite != psk->zeroRttSuite) {
        return PR_FALSE;
    }
    if (psk->type == ssl_psk_resume) {
        if (!sid) {
            return PR_FALSE;
        }
        PORT_Assert(sid->u.ssl3.locked.sessionTicket.flags & ticket_allow_early_data);
        PORT_Assert(ss->statelessResume);
        if (!ss->statelessResume) {
            return PR_FALSE;
        }
        if (SECITEM_CompareItem(&ss->xtnData.nextProto,
                                &sid->u.ssl3.alpnSelection) != 0) {
            return PR_FALSE;
        }
    } else if (psk->type != ssl_psk_external) {
        PORT_Assert(0);
        return PR_FALSE;
    }

    if (tls13_IsReplay(ss, sid)) {
        return PR_FALSE;
    }

    return PR_TRUE;
}

/* Called from tls13_HandleClientHelloPart2 to update the state of 0-RTT handling.
 *
 * 0-RTT is only permitted if:
 * 1. The early data extension was present.
 * 2. We are resuming a session.
 * 3. The 0-RTT option is set.
 * 4. The ticket allowed 0-RTT.
 * 5. We negotiated the same ALPN value as in the ticket.
 */
static void
tls13_NegotiateZeroRtt(sslSocket *ss, const sslSessionID *sid)
{
    SSL_TRC(3, ("%d: TLS13[%d]: negotiate 0-RTT %p",
                SSL_GETPID(), ss->fd, sid));

    /* tls13_ServerHandleEarlyDataXtn sets this to ssl_0rtt_sent, so this will
     * be ssl_0rtt_none unless early_data is present. */
    if (ss->ssl3.hs.zeroRttState == ssl_0rtt_none) {
        return;
    }

    if (ss->ssl3.hs.zeroRttState == ssl_0rtt_ignored) {
        /* HelloRetryRequest causes 0-RTT to be ignored. On the second
         * ClientHello, reset the ignore state so that decryption failure is
         * handled normally. */
        if (ss->ssl3.hs.zeroRttIgnore == ssl_0rtt_ignore_hrr) {
            PORT_Assert(ss->ssl3.hs.helloRetry);
            ss->ssl3.hs.zeroRttState = ssl_0rtt_none;
            ss->ssl3.hs.zeroRttIgnore = ssl_0rtt_ignore_none;
        } else {
            SSL_TRC(3, ("%d: TLS13[%d]: application ignored 0-RTT",
                        SSL_GETPID(), ss->fd));
        }
        return;
    }

    if (!tls13_CanNegotiateZeroRtt(ss, sid)) {
        SSL_TRC(3, ("%d: TLS13[%d]: ignore 0-RTT", SSL_GETPID(), ss->fd));
        ss->ssl3.hs.zeroRttState = ssl_0rtt_ignored;
        ss->ssl3.hs.zeroRttIgnore = ssl_0rtt_ignore_trial;
        return;
    }

    SSL_TRC(3, ("%d: TLS13[%d]: enable 0-RTT", SSL_GETPID(), ss->fd));
    PORT_Assert(ss->xtnData.selectedPsk);
    ss->ssl3.hs.zeroRttState = ssl_0rtt_accepted;
    ss->ssl3.hs.zeroRttIgnore = ssl_0rtt_ignore_none;
    ss->ssl3.hs.zeroRttSuite = ss->ssl3.hs.cipher_suite;
    ss->ssl3.hs.preliminaryInfo |= ssl_preinfo_0rtt_cipher_suite;
}

/* Check if the offered group is acceptable. */
static PRBool
tls13_isGroupAcceptable(const sslNamedGroupDef *offered,
                        const sslNamedGroupDef *preferredGroup)
{
    /* We accept epsilon (e) bits around the offered group size. */
    const unsigned int e = 2;

    PORT_Assert(offered);
    PORT_Assert(preferredGroup);

    if (offered->bits >= preferredGroup->bits - e &&
        offered->bits <= preferredGroup->bits + e) {
        return PR_TRUE;
    }

    return PR_FALSE;
}

/* Find remote key share for given group and return it.
 * Returns NULL if no key share is found. */
static TLS13KeyShareEntry *
tls13_FindKeyShareEntry(sslSocket *ss, const sslNamedGroupDef *group)
{
    PRCList *cur_p = PR_NEXT_LINK(&ss->xtnData.remoteKeyShares);
    while (cur_p != &ss->xtnData.remoteKeyShares) {
        TLS13KeyShareEntry *offer = (TLS13KeyShareEntry *)cur_p;
        if (offer->group == group) {
            return offer;
        }
        cur_p = PR_NEXT_LINK(cur_p);
    }
    return NULL;
}

static SECStatus
tls13_NegotiateKeyExchange(sslSocket *ss,
                           const sslNamedGroupDef **requestedGroup,
                           TLS13KeyShareEntry **clientShare)
{
    unsigned int index;
    TLS13KeyShareEntry *entry = NULL;
    const sslNamedGroupDef *preferredGroup = NULL;

    /* We insist on DHE. */
    if (ssl3_ExtensionNegotiated(ss, ssl_tls13_pre_shared_key_xtn)) {
        if (!ssl3_ExtensionNegotiated(ss, ssl_tls13_psk_key_exchange_modes_xtn)) {
            FATAL_ERROR(ss, SSL_ERROR_MISSING_PSK_KEY_EXCHANGE_MODES,
                        missing_extension);
            return SECFailure;
        }
        /* Since the server insists on DHE to provide forward secracy, for
         * every other PskKem value but DHE stateless resumption is disabled,
         * this includes other specified and GREASE values. */
        if (!memchr(ss->xtnData.psk_ke_modes.data, tls13_psk_dh_ke,
                    ss->xtnData.psk_ke_modes.len)) {
            SSL_TRC(3, ("%d: TLS13[%d]: client offered PSK without DH",
                        SSL_GETPID(), ss->fd));
            ss->statelessResume = PR_FALSE;
        }
    }

    /* Now figure out which key share we like the best out of the
     * mutually supported groups, regardless of what the client offered
     * for key shares.
     */
    if (!ssl3_ExtensionNegotiated(ss, ssl_supported_groups_xtn)) {
        FATAL_ERROR(ss, SSL_ERROR_MISSING_SUPPORTED_GROUPS_EXTENSION,
                    missing_extension);
        return SECFailure;
    }

    SSL_TRC(3, ("%d: TLS13[%d]: selected KE = %s", SSL_GETPID(),
                ss->fd, ss->statelessResume || ss->xtnData.selectedPsk ? "PSK + (EC)DHE" : "(EC)DHE"));

    /* Find the preferred group and an according client key share available. */
    for (index = 0; index < SSL_NAMED_GROUP_COUNT; ++index) {
        /* Continue to the next group if this one is not enabled. */
        if (!ss->namedGroupPreferences[index]) {
            /* There's a gap in the preferred groups list. Assume this is a group
             * that's not supported by the client but preferred by the server. */
            if (preferredGroup) {
                entry = NULL;
                break;
            }
            continue;
        }

        /* Check if the client sent a key share for this group. */
        entry = tls13_FindKeyShareEntry(ss, ss->namedGroupPreferences[index]);

        if (preferredGroup) {
            /* We already found our preferred group but the group didn't have a share. */
            if (entry) {
                /* The client sent a key share with group ss->namedGroupPreferences[index] */
                if (tls13_isGroupAcceptable(ss->namedGroupPreferences[index],
                                            preferredGroup)) {
                    /* This is not the preferred group, but it's acceptable */
                    preferredGroup = ss->namedGroupPreferences[index];
                } else {
                    /* The proposed group is not acceptable. */
                    entry = NULL;
                }
            }
            break;
        } else {
            /* The first enabled group is the preferred group. */
            preferredGroup = ss->namedGroupPreferences[index];
            if (entry) {
                break;
            }
        }
    }

    if (!preferredGroup) {
        FATAL_ERROR(ss, SSL_ERROR_NO_CYPHER_OVERLAP, handshake_failure);
        return SECFailure;
    }
    SSL_TRC(3, ("%d: TLS13[%d]: group = %d", SSL_GETPID(), ss->fd,
                preferredGroup->name));

    /* Either provide a share, or provide a group that should be requested in a
     * HelloRetryRequest, but not both. */
    if (entry) {
        PORT_Assert(preferredGroup == entry->group);
        *clientShare = entry;
        *requestedGroup = NULL;
    } else {
        *clientShare = NULL;
        *requestedGroup = preferredGroup;
    }
    return SECSuccess;
}

SECStatus
tls13_SelectServerCert(sslSocket *ss)
{
    PRCList *cursor;
    SECStatus rv;

    if (!ssl3_ExtensionNegotiated(ss, ssl_signature_algorithms_xtn)) {
        FATAL_ERROR(ss, SSL_ERROR_MISSING_SIGNATURE_ALGORITHMS_EXTENSION,
                    missing_extension);
        return SECFailure;
    }

    /* This picks the first certificate that has:
     * a) the right authentication method, and
     * b) the right named curve (EC only)
     *
     * We might want to do some sort of ranking here later.  For now, it's all
     * based on what order they are configured in. */
    for (cursor = PR_NEXT_LINK(&ss->serverCerts);
         cursor != &ss->serverCerts;
         cursor = PR_NEXT_LINK(cursor)) {
        sslServerCert *cert = (sslServerCert *)cursor;

        if (SSL_CERT_IS_ONLY(cert, ssl_auth_rsa_decrypt)) {
            continue;
        }

        rv = ssl_PickSignatureScheme(ss,
                                     cert->serverCert,
                                     cert->serverKeyPair->pubKey,
                                     cert->serverKeyPair->privKey,
                                     ss->xtnData.sigSchemes,
                                     ss->xtnData.numSigSchemes,
                                     PR_FALSE,
                                     &ss->ssl3.hs.signatureScheme);
        if (rv == SECSuccess) {
            /* Found one. */
            ss->sec.serverCert = cert;

            /* If we can use a delegated credential (DC) for authentication in
             * the current handshake, then commit to using it now. We'll send a
             * DC as an extension and use the DC private key to sign the
             * handshake.
             *
             * This sets the signature scheme to be the signature scheme
             * indicated by the DC.
             */
            rv = tls13_MaybeSetDelegatedCredential(ss);
            if (rv != SECSuccess) {
                return SECFailure; /* Failure indicates an internal error. */
            }

            ss->sec.authType = ss->ssl3.hs.kea_def_mutable.authKeyType =
                ssl_SignatureSchemeToAuthType(ss->ssl3.hs.signatureScheme);
            ss->sec.authKeyBits = cert->serverKeyBits;
            return SECSuccess;
        }
    }

    FATAL_ERROR(ss, SSL_ERROR_UNSUPPORTED_SIGNATURE_ALGORITHM,
                handshake_failure);
    return SECFailure;
}

/* Note: |requestedGroup| is non-NULL when we send a key_share extension. */
static SECStatus
tls13_MaybeSendHelloRetry(sslSocket *ss, const sslNamedGroupDef *requestedGroup,
                          PRBool *hrrSent)
{
    SSLHelloRetryRequestAction action = ssl_hello_retry_accept;
    PRUint8 token[256] = { 0 };
    unsigned int tokenLen = 0;
    SECStatus rv;

    if (ss->hrrCallback) {
        action = ss->hrrCallback(!ss->ssl3.hs.helloRetry,
                                 ss->xtnData.applicationToken.data,
                                 ss->xtnData.applicationToken.len,
                                 token, &tokenLen, sizeof(token),
                                 ss->hrrCallbackArg);
    }

    /* These use SSL3_SendAlert directly to avoid an assertion in
     * tls13_FatalError(), which is ordinarily OK. */
    if (action == ssl_hello_retry_request && ss->ssl3.hs.helloRetry) {
        (void)SSL3_SendAlert(ss, alert_fatal, internal_error);
        PORT_SetError(SSL_ERROR_APP_CALLBACK_ERROR);
        return SECFailure;
    }

    if (action != ssl_hello_retry_request && tokenLen) {
        (void)SSL3_SendAlert(ss, alert_fatal, internal_error);
        PORT_SetError(SSL_ERROR_APP_CALLBACK_ERROR);
        return SECFailure;
    }

    if (tokenLen > sizeof(token)) {
        (void)SSL3_SendAlert(ss, alert_fatal, internal_error);
        PORT_SetError(SSL_ERROR_APP_CALLBACK_ERROR);
        return SECFailure;
    }

    if (action == ssl_hello_retry_fail) {
        FATAL_ERROR(ss, SSL_ERROR_APPLICATION_ABORT, handshake_failure);
        return SECFailure;
    }

    if (action == ssl_hello_retry_reject_0rtt) {
        ss->ssl3.hs.zeroRttState = ssl_0rtt_ignored;
        ss->ssl3.hs.zeroRttIgnore = ssl_0rtt_ignore_trial;
    }

    if (!requestedGroup && action != ssl_hello_retry_request) {
        return SECSuccess;
    }

    rv = tls13_SendHelloRetryRequest(ss, requestedGroup, token, tokenLen);
    if (rv != SECSuccess) {
        return SECFailure; /* Code already set. */
    }

    /* We may have received ECH, but have to start over with CH2. */
    ss->ssl3.hs.echAccepted = PR_FALSE;
    PK11_HPKE_DestroyContext(ss->ssl3.hs.echHpkeCtx, PR_TRUE);
    ss->ssl3.hs.echHpkeCtx = NULL;

    *hrrSent = PR_TRUE;
    return SECSuccess;
}

static SECStatus
tls13_NegotiateAuthentication(sslSocket *ss)
{
    if (ss->statelessResume) {
        SSL_TRC(3, ("%d: TLS13[%d]: selected resumption PSK authentication",
                    SSL_GETPID(), ss->fd));
        ss->ssl3.hs.signatureScheme = ssl_sig_none;
        ss->ssl3.hs.kea_def_mutable.authKeyType = ssl_auth_psk;
        /* Overwritten by tls13_RestoreCipherInfo. */
        ss->sec.authType = ssl_auth_psk;
        return SECSuccess;
    } else if (ss->xtnData.selectedPsk) {
        /* If the EPSK doesn't specify a suite, use what was negotiated.
         * Else, only use the EPSK if we negotiated that suite. */
        if (ss->xtnData.selectedPsk->zeroRttSuite == TLS_NULL_WITH_NULL_NULL ||
            ss->ssl3.hs.cipher_suite == ss->xtnData.selectedPsk->zeroRttSuite) {
            SSL_TRC(3, ("%d: TLS13[%d]: selected external PSK authentication",
                        SSL_GETPID(), ss->fd));
            ss->ssl3.hs.signatureScheme = ssl_sig_none;
            ss->ssl3.hs.kea_def_mutable.authKeyType = ssl_auth_psk;
            ss->sec.authType = ssl_auth_psk;
            return SECSuccess;
        }
    }

    /* If there were PSKs, they are no longer needed. */
    if (ss->xtnData.selectedPsk) {
        tls13_DestroyPskList(&ss->ssl3.hs.psks);
        ss->xtnData.selectedPsk = NULL;
    }

    SSL_TRC(3, ("%d: TLS13[%d]: selected certificate authentication",
                SSL_GETPID(), ss->fd));
    SECStatus rv = tls13_SelectServerCert(ss);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    return SECSuccess;
}
/* Called from ssl3_HandleClientHello after we have parsed the
 * ClientHello and are sure that we are going to do TLS 1.3
 * or fail. */
SECStatus
tls13_HandleClientHelloPart2(sslSocket *ss,
                             const SECItem *suites,
                             sslSessionID *sid,
                             const PRUint8 *msg,
                             unsigned int len)
{
    SECStatus rv;
    SSL3Statistics *ssl3stats = SSL_GetStatistics();
    const sslNamedGroupDef *requestedGroup = NULL;
    TLS13KeyShareEntry *clientShare = NULL;
    ssl3CipherSuite previousCipherSuite = 0;
    const sslNamedGroupDef *previousGroup = NULL;
    PRBool hrr = PR_FALSE;
    PRBool previousOfferedEch;

    /* If the legacy_version field is set to 0x300 or smaller,
     * reject the connection with protocol_version alert. */
    if (ss->clientHelloVersion <= SSL_LIBRARY_VERSION_3_0) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CLIENT_HELLO, protocol_version);
        goto loser;
    }

    ss->ssl3.hs.endOfFlight = PR_TRUE;

    if (ssl3_ExtensionNegotiated(ss, ssl_tls13_early_data_xtn)) {
        ss->ssl3.hs.zeroRttState = ssl_0rtt_sent;
    }

    /* Negotiate cipher suite. */
    rv = ssl3_NegotiateCipherSuite(ss, suites, PR_FALSE);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, PORT_GetError(), handshake_failure);
        goto loser;
    }

    /* If we are going around again, then we should make sure that the cipher
     * suite selection doesn't change. That's a sign of client shennanigans. */
    if (ss->ssl3.hs.helloRetry) {

        /* Update sequence numbers before checking the cookie so that any alerts
         * we generate are sent with the right sequence numbers. */
        if (IS_DTLS(ss)) {
            /* Count the first ClientHello and the HelloRetryRequest. */
            ss->ssl3.hs.sendMessageSeq = 1;
            ss->ssl3.hs.recvMessageSeq = 1;
            ssl_GetSpecWriteLock(ss);
            /* Increase the write sequence number.  The read sequence number
             * will be reset after this to early data or handshake. */
            ss->ssl3.cwSpec->nextSeqNum = 1;
            ssl_ReleaseSpecWriteLock(ss);
        }

        if (!ssl3_ExtensionNegotiated(ss, ssl_tls13_cookie_xtn) ||
            !ss->xtnData.cookie.len) {
            FATAL_ERROR(ss, SSL_ERROR_MISSING_COOKIE_EXTENSION,
                        missing_extension);
            goto loser;
        }
        PRINT_BUF(50, (ss, "Client sent cookie",
                       ss->xtnData.cookie.data, ss->xtnData.cookie.len));

        rv = tls13_HandleHrrCookie(ss, ss->xtnData.cookie.data,
                                   ss->xtnData.cookie.len,
                                   &previousCipherSuite,
                                   &previousGroup,
                                   &previousOfferedEch, NULL, PR_TRUE);

        if (rv != SECSuccess) {
            FATAL_ERROR(ss, SSL_ERROR_BAD_2ND_CLIENT_HELLO, illegal_parameter);
            goto loser;
        }
    }

    /* Now merge the ClientHello into the hash state. */
    rv = ssl_HashHandshakeMessage(ss, ssl_hs_client_hello, msg, len);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        goto loser;
    }

    /* Now create a synthetic kea_def that we can tweak. */
    ss->ssl3.hs.kea_def_mutable = *ss->ssl3.hs.kea_def;
    ss->ssl3.hs.kea_def = &ss->ssl3.hs.kea_def_mutable;

    /* Note: We call this quite a bit earlier than with TLS 1.2 and
     * before. */
    rv = ssl3_ServerCallSNICallback(ss);
    if (rv != SECSuccess) {
        goto loser; /* An alert has already been sent. */
    }

    /* Check if we could in principle resume. */
    if (ss->statelessResume) {
        PORT_Assert(sid);
        if (!sid) {
            FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
            return SECFailure;
        }
        if (!tls13_CanResume(ss, sid)) {
            ss->statelessResume = PR_FALSE;
        }
    }

    /* Select key exchange. */
    rv = tls13_NegotiateKeyExchange(ss, &requestedGroup, &clientShare);
    if (rv != SECSuccess) {
        goto loser;
    }
    /* We should get either one of these, but not both. */
    PORT_Assert((requestedGroup && !clientShare) ||
                (!requestedGroup && clientShare));

    /* After HelloRetryRequest, check consistency of cipher and group. */
    if (ss->ssl3.hs.helloRetry) {
        PORT_Assert(previousCipherSuite);
        if (ss->ssl3.hs.cipher_suite != previousCipherSuite) {
            FATAL_ERROR(ss, SSL_ERROR_BAD_2ND_CLIENT_HELLO,
                        illegal_parameter);
            goto loser;
        }
        if (!clientShare) {
            FATAL_ERROR(ss, SSL_ERROR_BAD_2ND_CLIENT_HELLO,
                        illegal_parameter);
            goto loser;
        }

        /* CH1/CH2 must either both include ECH, or both exclude it. */
        if (previousOfferedEch != (ss->xtnData.ech != NULL)) {
            FATAL_ERROR(ss, SSL_ERROR_BAD_2ND_CLIENT_HELLO,
                        previousOfferedEch ? missing_extension : illegal_parameter);
            goto loser;
        }

        /* If we requested a new key share, check that the client provided just
         * one of the right type. */
        if (previousGroup) {
            if (PR_PREV_LINK(&ss->xtnData.remoteKeyShares) !=
                PR_NEXT_LINK(&ss->xtnData.remoteKeyShares)) {
                FATAL_ERROR(ss, SSL_ERROR_BAD_2ND_CLIENT_HELLO,
                            illegal_parameter);
                goto loser;
            }
            if (clientShare->group != previousGroup) {
                FATAL_ERROR(ss, SSL_ERROR_BAD_2ND_CLIENT_HELLO,
                            illegal_parameter);
                goto loser;
            }
        }
    }

    rv = tls13_MaybeSendHelloRetry(ss, requestedGroup, &hrr);
    if (rv != SECSuccess) {
        goto loser;
    }
    if (hrr) {
        if (sid) { /* Free the sid. */
            ssl_UncacheSessionID(ss);
            ssl_FreeSID(sid);
        }
        PORT_Assert(ss->ssl3.hs.helloRetry);
        return SECSuccess;
    }

    /* Select the authentication (this is also handshake shape). */
    rv = tls13_NegotiateAuthentication(ss);
    if (rv != SECSuccess) {
        goto loser;
    }

    if (ss->sec.authType == ssl_auth_psk) {
        if (ss->statelessResume) {
            /* We are now committed to trying to resume. */
            PORT_Assert(sid);
            /* Check that the negotiated SNI and the cached SNI match. */
            if (SECITEM_CompareItem(&sid->u.ssl3.srvName,
                                    &ss->ssl3.hs.srvVirtName) != SECEqual) {
                FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CLIENT_HELLO,
                            handshake_failure);
                goto loser;
            }

            ss->sec.serverCert = ssl_FindServerCert(ss, sid->authType,
                                                    sid->namedCurve);
            PORT_Assert(ss->sec.serverCert);

            rv = tls13_RecoverWrappedSharedSecret(ss, sid);
            if (rv != SECSuccess) {
                SSL_AtomicIncrementLong(&ssl3stats->hch_sid_cache_not_ok);
                FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
                goto loser;
            }
            tls13_RestoreCipherInfo(ss, sid);

            PORT_Assert(!ss->sec.localCert);
            ss->sec.localCert = CERT_DupCertificate(ss->sec.serverCert->serverCert);
            if (sid->peerCert != NULL) {
                ss->sec.peerCert = CERT_DupCertificate(sid->peerCert);
            }
        } else if (sid) {
            /* We should never have a SID in the non-resumption case. */
            PORT_Assert(0);
            ssl_UncacheSessionID(ss);
            ssl_FreeSID(sid);
            sid = NULL;
        }
        ssl3_RegisterExtensionSender(
            ss, &ss->xtnData,
            ssl_tls13_pre_shared_key_xtn, tls13_ServerSendPreSharedKeyXtn);
        tls13_NegotiateZeroRtt(ss, sid);

        rv = tls13_ComputeEarlySecretsWithPsk(ss);
        if (rv != SECSuccess) {
            FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
            return SECFailure;
        }
    } else {
        if (sid) { /* we had a sid, but it's no longer valid, free it */
            SSL_AtomicIncrementLong(&ssl3stats->hch_sid_cache_not_ok);
            ssl_UncacheSessionID(ss);
            ssl_FreeSID(sid);
            sid = NULL;
        }
        tls13_NegotiateZeroRtt(ss, NULL);
    }

    if (ss->statelessResume) {
        PORT_Assert(ss->xtnData.selectedPsk);
        PORT_Assert(ss->ssl3.hs.kea_def_mutable.authKeyType == ssl_auth_psk);
    }

    /* Now that we have the binder key, check the binder. */
    if (ss->xtnData.selectedPsk) {
        SSL3Hashes hashes;
        PORT_Assert(ss->ssl3.hs.messages.len > ss->xtnData.pskBindersLen);
        rv = tls13_ComputePskBinderHash(
            ss,
            ss->ssl3.hs.messages.buf,
            ss->ssl3.hs.messages.len - ss->xtnData.pskBindersLen,
            &hashes, tls13_GetHash(ss));
        if (rv != SECSuccess) {
            FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
            goto loser;
        }

        PORT_Assert(ss->xtnData.selectedPsk->hash == tls13_GetHash(ss));
        PORT_Assert(ss->ssl3.hs.suite_def);
        rv = tls13_VerifyFinished(ss, ssl_hs_client_hello,
                                  ss->xtnData.selectedPsk->binderKey,
                                  ss->xtnData.pskBinder.data,
                                  ss->xtnData.pskBinder.len,
                                  &hashes);
    }
    if (rv != SECSuccess) {
        goto loser;
    }

    /* This needs to go after we verify the psk binder. */
    rv = ssl3_InitHandshakeHashes(ss);
    if (rv != SECSuccess) {
        goto loser;
    }

    /* If this is TLS 1.3 we are expecting a ClientKeyShare
     * extension. Missing/absent extension cause failure
     * below. */
    rv = tls13_HandleClientKeyShare(ss, clientShare);
    if (rv != SECSuccess) {
        goto loser; /* An alert was sent already. */
    }

    /* From this point we are either committed to resumption, or not. */
    if (ss->statelessResume) {
        SSL_AtomicIncrementLong(&ssl3stats->hch_sid_cache_hits);
        SSL_AtomicIncrementLong(&ssl3stats->hch_sid_stateless_resumes);
    } else {
        if (sid) {
            /* We had a sid, but it's no longer valid, free it. */
            SSL_AtomicIncrementLong(&ssl3stats->hch_sid_cache_not_ok);
            ssl_UncacheSessionID(ss);
            ssl_FreeSID(sid);
        } else if (!ss->xtnData.selectedPsk) {
            SSL_AtomicIncrementLong(&ssl3stats->hch_sid_cache_misses);
        }

        sid = ssl3_NewSessionID(ss, PR_TRUE);
        if (!sid) {
            FATAL_ERROR(ss, PORT_GetError(), internal_error);
            return SECFailure;
        }
    }
    /* Take ownership of the session. */
    ss->sec.ci.sid = sid;
    sid = NULL;

    if (ss->ssl3.hs.zeroRttState == ssl_0rtt_accepted) {
        rv = tls13_DeriveEarlySecrets(ss);
        if (rv != SECSuccess) {
            FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
            return SECFailure;
        }
    }

    ssl_GetXmitBufLock(ss);
    rv = tls13_SendServerHelloSequence(ss);
    ssl_ReleaseXmitBufLock(ss);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, PORT_GetError(), handshake_failure);
        return SECFailure;
    }

    /* We're done with PSKs */
    tls13_DestroyPskList(&ss->ssl3.hs.psks);
    ss->xtnData.selectedPsk = NULL;

    return SECSuccess;

loser:
    if (sid) {
        ssl_UncacheSessionID(ss);
        ssl_FreeSID(sid);
    }
    return SECFailure;
}

SECStatus
SSLExp_HelloRetryRequestCallback(PRFileDesc *fd,
                                 SSLHelloRetryRequestCallback cb, void *arg)
{
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        return SECFailure; /* Code already set. */
    }

    ss->hrrCallback = cb;
    ss->hrrCallbackArg = arg;
    return SECSuccess;
}

/*
 * struct {
 *     ProtocolVersion server_version;
 *     CipherSuite cipher_suite;
 *     Extension extensions<2..2^16-1>;
 * } HelloRetryRequest;
 *
 * Note: this function takes an empty buffer and returns
 * a non-empty one on success, in which case the caller must
 * eventually clean up.
 */
SECStatus
tls13_ConstructHelloRetryRequest(sslSocket *ss,
                                 ssl3CipherSuite cipherSuite,
                                 const sslNamedGroupDef *selectedGroup,
                                 PRUint8 *cookie, unsigned int cookieLen,
                                 const PRUint8 *cookieGreaseEchSignal,
                                 sslBuffer *buffer)
{
    SECStatus rv;
    sslBuffer extensionsBuf = SSL_BUFFER_EMPTY;
    PORT_Assert(buffer->len == 0);

    /* Note: cookie is pointing to a stack variable, so is only valid
     * now. */
    ss->xtnData.selectedGroup = selectedGroup;
    ss->xtnData.cookie.data = cookie;
    ss->xtnData.cookie.len = cookieLen;

    /* Set restored ss->ssl3.hs.greaseEchBuf value for ECH HRR extension
     * reconstruction. */
    if (cookieGreaseEchSignal) {
        PORT_Assert(!ss->ssl3.hs.greaseEchBuf.len);
        rv = sslBuffer_Append(&ss->ssl3.hs.greaseEchBuf,
                              cookieGreaseEchSignal,
                              TLS13_ECH_SIGNAL_LEN);
        if (rv != SECSuccess) {
            goto loser;
        }
    }
    rv = ssl_ConstructExtensions(ss, &extensionsBuf,
                                 ssl_hs_hello_retry_request);
    /* Reset ss->ssl3.hs.greaseEchBuf if it was changed. */
    if (cookieGreaseEchSignal) {
        sslBuffer_Clear(&ss->ssl3.hs.greaseEchBuf);
    }
    if (rv != SECSuccess) {
        goto loser;
    }
    /* These extensions can't be empty. */
    PORT_Assert(SSL_BUFFER_LEN(&extensionsBuf) > 0);

    /* Clean up cookie so we're not pointing at random memory. */
    ss->xtnData.cookie.data = NULL;
    ss->xtnData.cookie.len = 0;

    rv = ssl_ConstructServerHello(ss, PR_TRUE, &extensionsBuf, buffer);
    if (rv != SECSuccess) {
        goto loser;
    }
    sslBuffer_Clear(&extensionsBuf);
    return SECSuccess;

loser:
    sslBuffer_Clear(&extensionsBuf);
    sslBuffer_Clear(buffer);
    return SECFailure;
}

static SECStatus
tls13_SendHelloRetryRequest(sslSocket *ss,
                            const sslNamedGroupDef *requestedGroup,
                            const PRUint8 *appToken, unsigned int appTokenLen)
{
    SECStatus rv;
    unsigned int cookieLen;
    PRUint8 cookie[1024];
    sslBuffer messageBuf = SSL_BUFFER_EMPTY;

    SSL_TRC(3, ("%d: TLS13[%d]: send hello retry request handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    /* If an ECH backend or shared-mode server accepted ECH when offered,
     * the HRR extension's payload must be set to 8 zero bytes, these are
     * overwritten with the accept_confirmation value after the handshake
     * transcript calculation.
     * If a client-facing or shared-mode server did not accept ECH when offered
     * OR if ECH GREASE is enabled on the server and a ECH extension was
     * received, a 8 byte random value is set as the extension's payload
     * [draft-ietf-tls-esni-14, Section 7].
     *
     * The (temporary) payload is written to the extension in tls13exthandle.c/
     * tls13_ServerSendHrrEchXtn(). */
    if (ss->xtnData.ech) {
        PRUint8 echGreaseRaw[TLS13_ECH_SIGNAL_LEN] = { 0 };
        if (!(ss->ssl3.hs.echAccepted ||
              (ss->opt.enableTls13BackendEch &&
               ss->xtnData.ech &&
               ss->xtnData.ech->receivedInnerXtn))) {
            rv = PK11_GenerateRandom(echGreaseRaw, TLS13_ECH_SIGNAL_LEN);
            if (rv != SECSuccess) {
                return SECFailure;
            }
            SSL_TRC(100, ("Generated random value for ECH HRR GREASE."));
        }
        sslBuffer echGreaseBuffer = SSL_BUFFER_EMPTY;
        rv = sslBuffer_Append(&echGreaseBuffer, echGreaseRaw, sizeof(echGreaseRaw));
        if (rv != SECSuccess) {
            return SECFailure;
        }
        /* HRR GREASE/accept_confirmation zero bytes placeholder buffer. */
        ss->ssl3.hs.greaseEchBuf = echGreaseBuffer;
    }

    /* Compute the cookie we are going to need. */
    rv = tls13_MakeHrrCookie(ss, requestedGroup,
                             appToken, appTokenLen,
                             cookie, &cookieLen, sizeof(cookie));
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        return SECFailure;
    }

    /* Now build the body of the message. */
    rv = tls13_ConstructHelloRetryRequest(ss, ss->ssl3.hs.cipher_suite,
                                          requestedGroup,
                                          cookie, cookieLen,
                                          NULL, &messageBuf);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        return SECFailure;
    }

    /* And send it. */
    ssl_GetXmitBufLock(ss);
    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_server_hello,
                                    SSL_BUFFER_LEN(&messageBuf));
    if (rv != SECSuccess) {
        goto loser;
    }
    rv = ssl3_AppendBufferToHandshake(ss, &messageBuf);
    if (rv != SECSuccess) {
        goto loser;
    }
    sslBuffer_Clear(&messageBuf); /* Done with messageBuf */

    if (ss->ssl3.hs.fakeSid.len) {
        PRInt32 sent;

        PORT_Assert(!IS_DTLS(ss));
        rv = ssl3_SendChangeCipherSpecsInt(ss);
        if (rv != SECSuccess) {
            goto loser;
        }
        /* ssl3_SendChangeCipherSpecsInt() only flushes to the output buffer, so we
         * have to force a send. */
        sent = ssl_SendSavedWriteData(ss);
        if (sent < 0 && PORT_GetError() != PR_WOULD_BLOCK_ERROR) {
            PORT_SetError(SSL_ERROR_SOCKET_WRITE_FAILURE);
            goto loser;
        }
    } else {
        rv = ssl3_FlushHandshake(ss, 0);
        if (rv != SECSuccess) {
            goto loser; /* error code set by ssl3_FlushHandshake */
        }
    }

    /* We depend on this being exactly one record and one message. */
    PORT_Assert(!IS_DTLS(ss) || (ss->ssl3.hs.sendMessageSeq == 1 &&
                                 ss->ssl3.cwSpec->nextSeqNum == 1));
    ssl_ReleaseXmitBufLock(ss);

    ss->ssl3.hs.helloRetry = PR_TRUE;

    /* We received early data but have to ignore it because we sent a retry. */
    if (ss->ssl3.hs.zeroRttState == ssl_0rtt_sent) {
        ss->ssl3.hs.zeroRttState = ssl_0rtt_ignored;
        ss->ssl3.hs.zeroRttIgnore = ssl_0rtt_ignore_hrr;
    }

    return SECSuccess;

loser:
    sslBuffer_Clear(&messageBuf);
    ssl_ReleaseXmitBufLock(ss);
    return SECFailure;
}

/* Called from tls13_HandleClientHello.
 *
 * Caller must hold Handshake and RecvBuf locks.
 */

static SECStatus
tls13_HandleClientKeyShare(sslSocket *ss, TLS13KeyShareEntry *peerShare)
{
    SECStatus rv;
    sslEphemeralKeyPair *keyPair; /* ours */
    SECItem *ciphertext = NULL;
    PK11SymKey *dheSecret = NULL;
    PK11SymKey *kemSecret = NULL;

    SSL_TRC(3, ("%d: TLS13[%d]: handle client_key_share handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(peerShare);

    tls13_SetKeyExchangeType(ss, peerShare->group);

    /* Generate our key */
    rv = tls13_AddKeyShare(ss, peerShare->group);
    if (rv != SECSuccess) {
        return rv;
    }

    /* We should have exactly one key share. */
    PORT_Assert(!PR_CLIST_IS_EMPTY(&ss->ephemeralKeyPairs));
    PORT_Assert(PR_PREV_LINK(&ss->ephemeralKeyPairs) ==
                PR_NEXT_LINK(&ss->ephemeralKeyPairs));

    keyPair = ((sslEphemeralKeyPair *)PR_NEXT_LINK(&ss->ephemeralKeyPairs));
    ss->sec.keaKeyBits = SECKEY_PublicKeyStrengthInBits(keyPair->keys->pubKey);

    /* Register the sender */
    rv = ssl3_RegisterExtensionSender(ss, &ss->xtnData, ssl_tls13_key_share_xtn,
                                      tls13_ServerSendKeyShareXtn);
    if (rv != SECSuccess) {
        return SECFailure; /* Error code set already. */
    }

    rv = tls13_HandleKeyShare(ss, peerShare, keyPair->keys,
                              tls13_GetHash(ss),
                              &dheSecret);
    if (rv != SECSuccess) {
        goto loser; /* Error code already set. */
    }

    if (peerShare->group->keaType == ssl_kea_ecdh_hybrid) {
        rv = tls13_HandleKEMKey(ss, peerShare, &kemSecret, &ciphertext);
        if (rv != SECSuccess) {
            goto loser; /* Error set by tls13_HandleKEMKey */
        }
        switch (peerShare->group->name) {
            case ssl_grp_kem_xyber768d00:
                ss->ssl3.hs.dheSecret = PK11_ConcatSymKeys(dheSecret, kemSecret, CKM_HKDF_DERIVE, CKA_DERIVE);
                break;
            case ssl_grp_kem_mlkem768x25519:
                ss->ssl3.hs.dheSecret = PK11_ConcatSymKeys(kemSecret, dheSecret, CKM_HKDF_DERIVE, CKA_DERIVE);
                break;
            default:
                PORT_Assert(0);
                PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
                ss->ssl3.hs.dheSecret = NULL;
                break;
        }
        if (!ss->ssl3.hs.dheSecret) {
            goto loser; /* Error set by PK11_ConcatSymKeys */
        }
        keyPair->kemCt = ciphertext;
        PK11_FreeSymKey(dheSecret);
        PK11_FreeSymKey(kemSecret);
    } else {
        ss->ssl3.hs.dheSecret = dheSecret;
    }

    return SECSuccess;

loser:
    SECITEM_FreeItem(ciphertext, PR_TRUE);
    PK11_FreeSymKey(dheSecret);
    PK11_FreeSymKey(kemSecret);
    FATAL_ERROR(ss, PORT_GetError(), illegal_parameter);
    return SECFailure;
}

/*
 *     [draft-ietf-tls-tls13-11] Section 6.3.3.2
 *
 *     opaque DistinguishedName<1..2^16-1>;
 *
 *     struct {
 *         opaque certificate_extension_oid<1..2^8-1>;
 *         opaque certificate_extension_values<0..2^16-1>;
 *     } CertificateExtension;
 *
 *     struct {
 *         opaque certificate_request_context<0..2^8-1>;
 *         SignatureAndHashAlgorithm
 *           supported_signature_algorithms<2..2^16-2>;
 *         DistinguishedName certificate_authorities<0..2^16-1>;
 *         CertificateExtension certificate_extensions<0..2^16-1>;
 *     } CertificateRequest;
 */
static SECStatus
tls13_SendCertificateRequest(sslSocket *ss)
{
    SECStatus rv;
    sslBuffer extensionBuf = SSL_BUFFER_EMPTY;
    unsigned int offset = 0;

    SSL_TRC(3, ("%d: TLS13[%d]: begin send certificate_request",
                SSL_GETPID(), ss->fd));

    if (ss->firstHsDone) {
        PORT_Assert(ss->ssl3.hs.shaPostHandshake == NULL);
        ss->ssl3.hs.shaPostHandshake = PK11_CloneContext(ss->ssl3.hs.sha);
        if (ss->ssl3.hs.shaPostHandshake == NULL) {
            ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
            return SECFailure;
        }
    }

    rv = ssl_ConstructExtensions(ss, &extensionBuf, ssl_hs_certificate_request);
    if (rv != SECSuccess) {
        return SECFailure; /* Code already set. */
    }
    /* We should always have at least one of these. */
    PORT_Assert(SSL_BUFFER_LEN(&extensionBuf) > 0);

    /* Create a new request context for post-handshake authentication */
    if (ss->firstHsDone) {
        PRUint8 context[16];
        SECItem contextItem = { siBuffer, context, sizeof(context) };

        rv = PK11_GenerateRandom(context, sizeof(context));
        if (rv != SECSuccess) {
            goto loser;
        }

        SECITEM_FreeItem(&ss->xtnData.certReqContext, PR_FALSE);
        rv = SECITEM_CopyItem(NULL, &ss->xtnData.certReqContext, &contextItem);
        if (rv != SECSuccess) {
            FATAL_ERROR(ss, SEC_ERROR_NO_MEMORY, internal_error);
            goto loser;
        }

        offset = SSL_BUFFER_LEN(&ss->sec.ci.sendBuf);
    }

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_certificate_request,
                                    1 + /* request context length */
                                        ss->xtnData.certReqContext.len +
                                        2 + /* extension length */
                                        SSL_BUFFER_LEN(&extensionBuf));
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }

    /* Context. */
    rv = ssl3_AppendHandshakeVariable(ss, ss->xtnData.certReqContext.data,
                                      ss->xtnData.certReqContext.len, 1);
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }
    /* Extensions. */
    rv = ssl3_AppendBufferToHandshakeVariable(ss, &extensionBuf, 2);
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }

    if (ss->firstHsDone) {
        rv = ssl3_UpdatePostHandshakeHashes(ss,
                                            SSL_BUFFER_BASE(&ss->sec.ci.sendBuf) + offset,
                                            SSL_BUFFER_LEN(&ss->sec.ci.sendBuf) - offset);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    sslBuffer_Clear(&extensionBuf);
    return SECSuccess;

loser:
    sslBuffer_Clear(&extensionBuf);
    return SECFailure;
}

/* [draft-ietf-tls-tls13; S 4.4.1] says:
 *
 *     Transcript-Hash(ClientHello1, HelloRetryRequest, ... MN) =
 *      Hash(message_hash ||        // Handshake type
 *           00 00 Hash.length ||   // Handshake message length
 *           Hash(ClientHello1) ||  // Hash of ClientHello1
 *           HelloRetryRequest ... MN)
 *
 *  For an ECH handshake, the process occurs for the outer
 *  transcript in |ss->ssl3.hs.messages| and the inner
 *  transcript in |ss->ssl3.hs.echInnerMessages|.
 */
static SECStatus
tls13_ReinjectHandshakeTranscript(sslSocket *ss)
{
    SSL3Hashes hashes = { 0 };
    SSL3Hashes echInnerHashes = { 0 };
    SECStatus rv;

    /* First compute the hash. */
    rv = tls13_ComputeHash(ss, &hashes,
                           ss->ssl3.hs.messages.buf,
                           ss->ssl3.hs.messages.len,
                           tls13_GetHash(ss));
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (ss->ssl3.hs.echHpkeCtx) {
        rv = tls13_ComputeHash(ss, &echInnerHashes,
                               ss->ssl3.hs.echInnerMessages.buf,
                               ss->ssl3.hs.echInnerMessages.len,
                               tls13_GetHash(ss));
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    ssl3_RestartHandshakeHashes(ss);

    /* Reinject the message. The Default context variant updates
     * the default hash state. Use it for both non-ECH and ECH Outer. */
    rv = ssl_HashHandshakeMessageDefault(ss, ssl_hs_message_hash,
                                         hashes.u.raw, hashes.len);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (ss->ssl3.hs.echHpkeCtx) {
        rv = ssl_HashHandshakeMessageEchInner(ss, ssl_hs_message_hash,
                                              echInnerHashes.u.raw,
                                              echInnerHashes.len);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    return SECSuccess;
}
static unsigned int
ssl_ListCount(PRCList *list)
{
    unsigned int c = 0;
    PRCList *cur;
    for (cur = PR_NEXT_LINK(list); cur != list; cur = PR_NEXT_LINK(cur)) {
        ++c;
    }
    return c;
}

/*
 * savedMsg contains the HelloRetryRequest message. When its extensions are parsed
 * in ssl3_HandleParsedExtensions, the handler for ECH HRR extensions (tls13_ClientHandleHrrEchXtn)
 * will take a reference into the message buffer.
 *
 * This reference is then used in tls13_MaybeHandleEchSignal in order to compute
 * the transcript for the ECH signal calculation. This was felt to be preferable
 * to re-parsing the HelloRetryRequest message in order to create the transcript.
 *
 * Consequently, savedMsg should not be moved or mutated between these
 * function calls.
 */
SECStatus
tls13_HandleHelloRetryRequest(sslSocket *ss, const PRUint8 *savedMsg,
                              PRUint32 savedLength)
{
    SECStatus rv;

    SSL_TRC(3, ("%d: TLS13[%d]: handle hello retry request",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (ss->vrange.max < SSL_LIBRARY_VERSION_TLS_1_3) {
        FATAL_ERROR(ss, SSL_ERROR_RX_UNEXPECTED_HELLO_RETRY_REQUEST,
                    unexpected_message);
        return SECFailure;
    }
    PORT_Assert(ss->ssl3.hs.ws == wait_server_hello);

    if (ss->ssl3.hs.zeroRttState == ssl_0rtt_sent) {
        ss->ssl3.hs.zeroRttState = ssl_0rtt_ignored;
        /* Restore the null cipher spec for writing. */
        ssl_GetSpecWriteLock(ss);
        ssl_CipherSpecRelease(ss->ssl3.cwSpec);
        ss->ssl3.cwSpec = ssl_FindCipherSpecByEpoch(ss, ssl_secret_write,
                                                    TrafficKeyClearText);
        PORT_Assert(ss->ssl3.cwSpec);
        ssl_ReleaseSpecWriteLock(ss);
    } else {
        PORT_Assert(ss->ssl3.hs.zeroRttState == ssl_0rtt_none);
    }
    /* Set the spec version, because we want to send CH now with 0303 */
    tls13_SetSpecRecordVersion(ss, ss->ssl3.cwSpec);

    /* Extensions must contain more than just supported_versions.  This will
     * ensure that a HelloRetryRequest isn't a no-op: we must have at least two
     * extensions, supported_versions plus one other.  That other must be one
     * that we understand and recognize as being valid for HelloRetryRequest,
     * and should alter our next Client Hello. */
    unsigned int requiredExtensions = 1;
    /* The ECH HRR extension is a no-op from the client's perspective. */
    if (ss->xtnData.ech) {
        requiredExtensions++;
    }
    if (ssl_ListCount(&ss->ssl3.hs.remoteExtensions) <= requiredExtensions) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_HELLO_RETRY_REQUEST,
                    decode_error);
        return SECFailure;
    }

    rv = ssl3_HandleParsedExtensions(ss, ssl_hs_hello_retry_request);
    ssl3_DestroyRemoteExtensions(&ss->ssl3.hs.remoteExtensions);
    if (rv != SECSuccess) {
        return SECFailure; /* Error code set below */
    }
    rv = tls13_MaybeHandleEchSignal(ss, savedMsg, savedLength, PR_TRUE);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    ss->ssl3.hs.helloRetry = PR_TRUE;
    rv = tls13_ReinjectHandshakeTranscript(ss);
    if (rv != SECSuccess) {
        return rv;
    }

    rv = ssl_HashHandshakeMessage(ss, ssl_hs_server_hello,
                                  savedMsg, savedLength);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    ssl_GetXmitBufLock(ss);
    if (ss->opt.enableTls13CompatMode && !IS_DTLS(ss) &&
        ss->ssl3.hs.zeroRttState == ssl_0rtt_none) {
        rv = ssl3_SendChangeCipherSpecsInt(ss);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    rv = ssl3_SendClientHello(ss, client_hello_retry);
    if (rv != SECSuccess) {
        goto loser;
    }

    ssl_ReleaseXmitBufLock(ss);
    return SECSuccess;

loser:
    ssl_ReleaseXmitBufLock(ss);
    return SECFailure;
}

static SECStatus
tls13_SendPostHandshakeCertificate(sslSocket *ss)
{
    SECStatus rv;
    if (ss->ssl3.hs.restartTarget) {
        PR_NOT_REACHED("unexpected ss->ssl3.hs.restartTarget");
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    if (ss->ssl3.hs.clientCertificatePending) {
        SSL_TRC(3, ("%d: TLS13[%d]: deferring tls13_SendClientSecondFlight because"
                    " certificate authentication is still pending.",
                    SSL_GETPID(), ss->fd));
        ss->ssl3.hs.restartTarget = tls13_SendPostHandshakeCertificate;
        PORT_SetError(PR_WOULD_BLOCK_ERROR);
        return SECFailure;
    }

    ssl_GetXmitBufLock(ss);
    rv = tls13_SendClientSecondFlight(ss);
    ssl_ReleaseXmitBufLock(ss);
    PORT_Assert(ss->ssl3.hs.ws == idle_handshake);
    PORT_Assert(ss->ssl3.hs.shaPostHandshake != NULL);
    PK11_DestroyContext(ss->ssl3.hs.shaPostHandshake, PR_TRUE);
    ss->ssl3.hs.shaPostHandshake = NULL;
    if (rv != SECSuccess) {
        return SECFailure;
    }
    return rv;
}

static SECStatus
tls13_HandleCertificateRequest(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    SECStatus rv;
    SECItem context = { siBuffer, NULL, 0 };
    SECItem extensionsData = { siBuffer, NULL, 0 };

    SSL_TRC(3, ("%d: TLS13[%d]: handle certificate_request sequence",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    /* Client */
    if (ss->opt.enablePostHandshakeAuth) {
        rv = TLS13_CHECK_HS_STATE(ss, SSL_ERROR_RX_UNEXPECTED_CERT_REQUEST,
                                  wait_cert_request, idle_handshake);
    } else {
        rv = TLS13_CHECK_HS_STATE(ss, SSL_ERROR_RX_UNEXPECTED_CERT_REQUEST,
                                  wait_cert_request);
    }
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /*  MUST NOT combine external PSKs with certificate authentication. */
    if (ss->sec.authType == ssl_auth_psk) {
        FATAL_ERROR(ss, SSL_ERROR_RX_UNEXPECTED_CERT_REQUEST, unexpected_message);
        return SECFailure;
    }

    if (tls13_IsPostHandshake(ss)) {
        PORT_Assert(ss->ssl3.hs.shaPostHandshake == NULL);
        ss->ssl3.hs.shaPostHandshake = PK11_CloneContext(ss->ssl3.hs.sha);
        if (ss->ssl3.hs.shaPostHandshake == NULL) {
            ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
            return SECFailure;
        }
        rv = ssl_HashPostHandshakeMessage(ss, ssl_hs_certificate_request, b, length);
        if (rv != SECSuccess) {
            FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
            return SECFailure;
        }

        /* clean up anything left from previous handshake. */
        if (ss->ssl3.clientCertChain != NULL) {
            CERT_DestroyCertificateList(ss->ssl3.clientCertChain);
            ss->ssl3.clientCertChain = NULL;
        }
        if (ss->ssl3.clientCertificate != NULL) {
            CERT_DestroyCertificate(ss->ssl3.clientCertificate);
            ss->ssl3.clientCertificate = NULL;
        }
        if (ss->ssl3.clientPrivateKey != NULL) {
            SECKEY_DestroyPrivateKey(ss->ssl3.clientPrivateKey);
            ss->ssl3.clientPrivateKey = NULL;
        }
        if (ss->ssl3.hs.clientAuthSignatureSchemes != NULL) {
            PORT_Free(ss->ssl3.hs.clientAuthSignatureSchemes);
            ss->ssl3.hs.clientAuthSignatureSchemes = NULL;
            ss->ssl3.hs.clientAuthSignatureSchemesLen = 0;
        }
        SECITEM_FreeItem(&ss->xtnData.certReqContext, PR_FALSE);
        ss->xtnData.certReqContext.data = NULL;
    } else {
        PORT_Assert(ss->ssl3.clientCertChain == NULL);
        PORT_Assert(ss->ssl3.clientCertificate == NULL);
        PORT_Assert(ss->ssl3.clientPrivateKey == NULL);
        PORT_Assert(ss->ssl3.hs.clientAuthSignatureSchemes == NULL);
        PORT_Assert(ss->ssl3.hs.clientAuthSignatureSchemesLen == 0);
        PORT_Assert(!ss->ssl3.hs.clientCertRequested);
        PORT_Assert(ss->xtnData.certReqContext.data == NULL);
    }

    rv = ssl3_ConsumeHandshakeVariable(ss, &context, 1, &b, &length);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* Unless it is a post-handshake client auth, the certificate
     * request context must be empty. */
    if (!tls13_IsPostHandshake(ss) && context.len > 0) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CERT_REQUEST, illegal_parameter);
        return SECFailure;
    }

    rv = ssl3_ConsumeHandshakeVariable(ss, &extensionsData, 2, &b, &length);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (length) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CERT_REQUEST, decode_error);
        return SECFailure;
    }

    /* Process all the extensions. */
    rv = ssl3_HandleExtensions(ss, &extensionsData.data, &extensionsData.len,
                               ssl_hs_certificate_request);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (!ss->xtnData.numSigSchemes) {
        FATAL_ERROR(ss, SSL_ERROR_MISSING_SIGNATURE_ALGORITHMS_EXTENSION,
                    missing_extension);
        return SECFailure;
    }

    rv = SECITEM_CopyItem(NULL, &ss->xtnData.certReqContext, &context);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    ss->ssl3.hs.clientCertRequested = PR_TRUE;

    if (ss->firstHsDone) {

        /* Request a client certificate. */
        rv = ssl3_BeginHandleCertificateRequest(
            ss, ss->xtnData.sigSchemes, ss->xtnData.numSigSchemes,
            &ss->xtnData.certReqAuthorities);
        if (rv != SECSuccess) {
            FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
            return rv;
        }
        rv = tls13_SendPostHandshakeCertificate(ss);
        if (rv != SECSuccess) {
            return rv; /* error code is set. */
        }
    } else {
        TLS13_SET_HS_STATE(ss, wait_server_cert);
    }
    return SECSuccess;
}

PRBool
tls13_ShouldRequestClientAuth(sslSocket *ss)
{
    /* Even if we are configured to request a certificate, we can't
     * if this handshake used a PSK, even when we are resuming. */
    return ss->opt.requestCertificate &&
           ss->ssl3.hs.kea_def->authKeyType != ssl_auth_psk;
}

static SECStatus
tls13_SendEncryptedServerSequence(sslSocket *ss)
{
    SECStatus rv;

    rv = tls13_ComputeHandshakeSecrets(ss);
    if (rv != SECSuccess) {
        return SECFailure; /* error code is set. */
    }

    rv = tls13_SetCipherSpec(ss, TrafficKeyHandshake,
                             ssl_secret_write, PR_FALSE);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    if (ss->ssl3.hs.zeroRttState == ssl_0rtt_accepted) {
        rv = ssl3_RegisterExtensionSender(ss, &ss->xtnData,
                                          ssl_tls13_early_data_xtn,
                                          ssl_SendEmptyExtension);
        if (rv != SECSuccess) {
            return SECFailure; /* Error code set already. */
        }
    }

    rv = tls13_SendEncryptedExtensions(ss);
    if (rv != SECSuccess) {
        return SECFailure; /* error code is set. */
    }

    if (tls13_ShouldRequestClientAuth(ss)) {
        rv = tls13_SendCertificateRequest(ss);
        if (rv != SECSuccess) {
            return SECFailure; /* error code is set. */
        }
    }
    if (ss->ssl3.hs.signatureScheme != ssl_sig_none) {
        SECKEYPrivateKey *svrPrivKey;

        rv = tls13_SendCertificate(ss);
        if (rv != SECSuccess) {
            return SECFailure; /* error code is set. */
        }

        if (tls13_IsSigningWithDelegatedCredential(ss)) {
            SSL_TRC(3, ("%d: TLS13[%d]: Signing with delegated credential",
                        SSL_GETPID(), ss->fd));
            svrPrivKey = ss->sec.serverCert->delegCredKeyPair->privKey;
        } else {
            svrPrivKey = ss->sec.serverCert->serverKeyPair->privKey;
        }

        rv = tls13_SendCertificateVerify(ss, svrPrivKey);
        if (rv != SECSuccess) {
            return SECFailure; /* err code is set. */
        }
    }

    rv = tls13_SendFinished(ss, ss->ssl3.hs.serverHsTrafficSecret);
    if (rv != SECSuccess) {
        return SECFailure; /* error code is set. */
    }

    return SECSuccess;
}

/* Called from:  ssl3_HandleClientHello */
static SECStatus
tls13_SendServerHelloSequence(sslSocket *ss)
{
    SECStatus rv;
    PRErrorCode err = 0;

    SSL_TRC(3, ("%d: TLS13[%d]: begin send server_hello sequence",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));

    rv = ssl3_RegisterExtensionSender(ss, &ss->xtnData,
                                      ssl_tls13_supported_versions_xtn,
                                      tls13_ServerSendSupportedVersionsXtn);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = tls13_ComputeHandshakeSecret(ss);
    if (rv != SECSuccess) {
        return SECFailure; /* error code is set. */
    }

    rv = ssl3_SendServerHello(ss);
    if (rv != SECSuccess) {
        return rv; /* err code is set. */
    }

    if (ss->ssl3.hs.fakeSid.len) {
        PORT_Assert(!IS_DTLS(ss));
        SECITEM_FreeItem(&ss->ssl3.hs.fakeSid, PR_FALSE);
        if (!ss->ssl3.hs.helloRetry) {
            rv = ssl3_SendChangeCipherSpecsInt(ss);
            if (rv != SECSuccess) {
                return rv;
            }
        }
    }

    rv = tls13_SendEncryptedServerSequence(ss);
    if (rv != SECSuccess) {
        err = PORT_GetError();
    }
    /* Even if we get an error, since the ServerHello was successfully
     * serialized, we should give it a chance to reach the network.  This gives
     * the client a chance to perform the key exchange and decrypt the alert
     * we're about to send. */
    rv |= ssl3_FlushHandshake(ss, 0);
    if (rv != SECSuccess) {
        if (err) {
            PORT_SetError(err);
        }
        return SECFailure;
    }

    /* Compute the rest of the secrets except for the resumption
     * and exporter secret. */
    rv = tls13_ComputeApplicationSecrets(ss);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, PORT_GetError());
        return SECFailure;
    }

    rv = tls13_SetCipherSpec(ss, TrafficKeyApplicationData,
                             ssl_secret_write, PR_FALSE);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    if (IS_DTLS(ss)) {
        /* We need this for reading ACKs. */
        ssl_CipherSpecAddRef(ss->ssl3.crSpec);
    }
    if (ss->ssl3.hs.zeroRttState == ssl_0rtt_accepted) {
        rv = tls13_SetCipherSpec(ss, TrafficKeyEarlyApplicationData,
                                 ssl_secret_read, PR_TRUE);
        if (rv != SECSuccess) {
            LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }
        TLS13_SET_HS_STATE(ss, wait_end_of_early_data);
    } else {
        PORT_Assert(ss->ssl3.hs.zeroRttState == ssl_0rtt_none ||
                    ss->ssl3.hs.zeroRttState == ssl_0rtt_ignored);

        rv = tls13_SetCipherSpec(ss,
                                 TrafficKeyHandshake,
                                 ssl_secret_read, PR_FALSE);
        if (rv != SECSuccess) {
            LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }
        if (tls13_ShouldRequestClientAuth(ss)) {
            TLS13_SET_HS_STATE(ss, wait_client_cert);
        } else {
            TLS13_SET_HS_STATE(ss, wait_finished);
        }
    }

    /* Here we set a baseline value for our RTT estimation.
     * This value is updated when we get a response from the client. */
    ss->ssl3.hs.rttEstimate = ssl_Time(ss);
    return SECSuccess;
}

SECStatus
tls13_HandleServerHelloPart2(sslSocket *ss, const PRUint8 *savedMsg, PRUint32 savedLength)
{
    SECStatus rv;
    sslSessionID *sid = ss->sec.ci.sid;
    SSL3Statistics *ssl3stats = SSL_GetStatistics();

    if (ssl3_ExtensionNegotiated(ss, ssl_tls13_pre_shared_key_xtn)) {
        PORT_Assert(!PR_CLIST_IS_EMPTY(&ss->ssl3.hs.psks));
        PORT_Assert(ss->xtnData.selectedPsk);

        if (ss->xtnData.selectedPsk->type != ssl_psk_resume) {
            ss->statelessResume = PR_FALSE;
        }
    } else {
        /* We may have offered a PSK. If the server didn't negotiate
         * it, clear this state to re-extract the Early Secret. */
        if (ss->ssl3.hs.currentSecret) {
            /* We might have dropped incompatible PSKs on HRR
             * (see RFC8466, Section 4.1.4). */
            PORT_Assert(ss->ssl3.hs.helloRetry ||
                        ssl3_ExtensionAdvertised(ss, ssl_tls13_pre_shared_key_xtn));
            PK11_FreeSymKey(ss->ssl3.hs.currentSecret);
            ss->ssl3.hs.currentSecret = NULL;
        }
        ss->statelessResume = PR_FALSE;
        ss->xtnData.selectedPsk = NULL;
    }

    if (ss->statelessResume) {
        PORT_Assert(sid->version >= SSL_LIBRARY_VERSION_TLS_1_3);
        if (tls13_GetHash(ss) !=
            tls13_GetHashForCipherSuite(sid->u.ssl3.cipherSuite)) {
            FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_SERVER_HELLO,
                        illegal_parameter);
            return SECFailure;
        }
    }

    /* Now create a synthetic kea_def that we can tweak. */
    ss->ssl3.hs.kea_def_mutable = *ss->ssl3.hs.kea_def;
    ss->ssl3.hs.kea_def = &ss->ssl3.hs.kea_def_mutable;

    if (ss->xtnData.selectedPsk) {
        ss->ssl3.hs.kea_def_mutable.authKeyType = ssl_auth_psk;
        if (ss->statelessResume) {
            tls13_RestoreCipherInfo(ss, sid);
            if (sid->peerCert) {
                ss->sec.peerCert = CERT_DupCertificate(sid->peerCert);
            }

            SSL_AtomicIncrementLong(&ssl3stats->hsh_sid_cache_hits);
            SSL_AtomicIncrementLong(&ssl3stats->hsh_sid_stateless_resumes);
        } else {
            ss->sec.authType = ssl_auth_psk;
        }
    } else {
        if (ss->statelessResume &&
            ssl3_ExtensionAdvertised(ss, ssl_tls13_pre_shared_key_xtn)) {
            SSL_AtomicIncrementLong(&ssl3stats->hsh_sid_cache_misses);
        }
        if (sid->cached == in_client_cache) {
            /* If we tried to resume and failed, let's not try again. */
            ssl_UncacheSessionID(ss);
        }
    }

    /* Discard current SID and make a new one, though it may eventually
     * end up looking a lot like the old one.
     */
    ssl_FreeSID(sid);
    ss->sec.ci.sid = sid = ssl3_NewSessionID(ss, PR_FALSE);
    if (sid == NULL) {
        FATAL_ERROR(ss, PORT_GetError(), internal_error);
        return SECFailure;
    }
    if (ss->statelessResume) {
        PORT_Assert(ss->sec.peerCert);
        sid->peerCert = CERT_DupCertificate(ss->sec.peerCert);
    }
    sid->version = ss->version;

    rv = tls13_HandleServerKeyShare(ss);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = tls13_ComputeHandshakeSecret(ss);
    if (rv != SECSuccess) {
        return SECFailure; /* error code is set. */
    }

    rv = tls13_MaybeHandleEchSignal(ss, savedMsg, savedLength, PR_FALSE);
    if (rv != SECSuccess) {
        return SECFailure; /* error code is set. */
    }

    rv = tls13_ComputeHandshakeSecrets(ss);
    if (rv != SECSuccess) {
        return SECFailure; /* error code is set. */
    }

    if (ss->ssl3.hs.zeroRttState == ssl_0rtt_sent) {
        /* When we send 0-RTT, we saved the null spec in case we needed it to
         * send another ClientHello in response to a HelloRetryRequest.  Now
         * that we won't be receiving a HelloRetryRequest, release the spec. */
        ssl_CipherSpecReleaseByEpoch(ss, ssl_secret_write, TrafficKeyClearText);
    }

    rv = tls13_SetCipherSpec(ss, TrafficKeyHandshake,
                             ssl_secret_read, PR_FALSE);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SSL_ERROR_INIT_CIPHER_SUITE_FAILURE, internal_error);
        return SECFailure;
    }
    TLS13_SET_HS_STATE(ss, wait_encrypted_extensions);

    return SECSuccess;
}

static void
tls13_SetKeyExchangeType(sslSocket *ss, const sslNamedGroupDef *group)
{
    ss->sec.keaGroup = group;
    switch (group->keaType) {
        /* Note: These overwrite on resumption.... so if you start with ECDH
         * and resume with DH, we report DH. That's fine, since no answer
         * is really right. */
        case ssl_kea_ecdh:
            ss->ssl3.hs.kea_def_mutable.exchKeyType =
                ss->statelessResume ? ssl_kea_ecdh_psk : ssl_kea_ecdh;
            ss->sec.keaType = ssl_kea_ecdh;
            break;
        case ssl_kea_ecdh_hybrid:
            ss->ssl3.hs.kea_def_mutable.exchKeyType =
                ss->statelessResume ? ssl_kea_ecdh_hybrid_psk : ssl_kea_ecdh_hybrid;
            ss->sec.keaType = ssl_kea_ecdh_hybrid;
            break;
        case ssl_kea_dh:
            ss->ssl3.hs.kea_def_mutable.exchKeyType =
                ss->statelessResume ? ssl_kea_dh_psk : ssl_kea_dh;
            ss->sec.keaType = ssl_kea_dh;
            break;
        default:
            PORT_Assert(0);
    }
}

/*
 * Called from ssl3_HandleServerHello.
 *
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
tls13_HandleServerKeyShare(sslSocket *ss)
{
    SECStatus rv;
    TLS13KeyShareEntry *entry;
    sslEphemeralKeyPair *keyPair;
    PK11SymKey *dheSecret = NULL;
    PK11SymKey *kemSecret = NULL;

    SSL_TRC(3, ("%d: TLS13[%d]: handle server_key_share handshake",
                SSL_GETPID(), ss->fd));
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    /* This list should have one entry. */
    if (PR_CLIST_IS_EMPTY(&ss->xtnData.remoteKeyShares)) {
        FATAL_ERROR(ss, SSL_ERROR_MISSING_KEY_SHARE, missing_extension);
        return SECFailure;
    }

    entry = (TLS13KeyShareEntry *)PR_NEXT_LINK(&ss->xtnData.remoteKeyShares);
    PORT_Assert(PR_NEXT_LINK(&entry->link) == &ss->xtnData.remoteKeyShares);

    /* Now get our matching key. */
    keyPair = ssl_LookupEphemeralKeyPair(ss, entry->group);
    if (!keyPair) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_KEY_SHARE, illegal_parameter);
        return SECFailure;
    }

    PORT_Assert(ssl_NamedGroupEnabled(ss, entry->group));

    rv = tls13_HandleKeyShare(ss, entry, keyPair->keys,
                              tls13_GetHash(ss),
                              &dheSecret);
    if (rv != SECSuccess) {
        goto loser; /* Error code already set. */
    }

    if (entry->group->keaType == ssl_kea_ecdh_hybrid) {
        rv = tls13_HandleKEMCiphertext(ss, entry, keyPair->kemKeys, &kemSecret);
        if (rv != SECSuccess) {
            goto loser; /* Error set by tls13_HandleKEMCiphertext */
        }
        switch (entry->group->name) {
            case ssl_grp_kem_xyber768d00:
                ss->ssl3.hs.dheSecret = PK11_ConcatSymKeys(dheSecret, kemSecret, CKM_HKDF_DERIVE, CKA_DERIVE);
                break;
            case ssl_grp_kem_mlkem768x25519:
                ss->ssl3.hs.dheSecret = PK11_ConcatSymKeys(kemSecret, dheSecret, CKM_HKDF_DERIVE, CKA_DERIVE);
                break;
            default:
                PORT_Assert(0);
                PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
                ss->ssl3.hs.dheSecret = NULL;
                break;
        }
        if (!ss->ssl3.hs.dheSecret) {
            goto loser; /* Error set by PK11_ConcatSymKeys */
        }
        PK11_FreeSymKey(dheSecret);
        PK11_FreeSymKey(kemSecret);
    } else {
        ss->ssl3.hs.dheSecret = dheSecret;
    }

    tls13_SetKeyExchangeType(ss, entry->group);
    ss->sec.keaKeyBits = SECKEY_PublicKeyStrengthInBits(keyPair->keys->pubKey);

    return SECSuccess;

loser:
    PK11_FreeSymKey(dheSecret);
    PK11_FreeSymKey(kemSecret);
    FATAL_ERROR(ss, PORT_GetError(), illegal_parameter);
    return SECFailure;
}

static PRBool
tls13_FindCompressionAlgAndCheckIfSupportsEncoding(sslSocket *ss)
{
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    for (int j = 0; j < ss->ssl3.supportedCertCompressionAlgorithmsCount; j++) {
        if (ss->ssl3.supportedCertCompressionAlgorithms[j].id == ss->xtnData.compressionAlg) {
            if (ss->ssl3.supportedCertCompressionAlgorithms[j].encode != NULL) {
                return PR_TRUE;
            }
            return PR_FALSE;
        }
    }

    return PR_FALSE;
}

static SECStatus
tls13_FindCompressionAlgAndEncodeCertificate(
    sslSocket *ss, SECItem *certificateToEncode, SECItem *encodedCertificate)
{
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    SECStatus rv = SECFailure;
    for (int j = 0; j < ss->ssl3.supportedCertCompressionAlgorithmsCount; j++) {
        if (ss->ssl3.supportedCertCompressionAlgorithms[j].id == ss->xtnData.compressionAlg &&
            ss->ssl3.supportedCertCompressionAlgorithms[j].encode != NULL) {
            rv = ss->ssl3.supportedCertCompressionAlgorithms[j].encode(
                certificateToEncode, encodedCertificate);
            return rv;
        }
    }

    PORT_SetError(SEC_ERROR_CERTIFICATE_COMPRESSION_ALGORITHM_NOT_SUPPORTED);
    return SECFailure;
}

static SECStatus
tls13_SendCompressedCertificate(sslSocket *ss, sslBuffer *bufferCertificate)
{
    /* TLS Certificate Compression. RFC 8879 */
    /* As the encoding function takes as input a SECItem,
     * we convert bufferCertificate to certificateToEncode.
     *
     * encodedCertificate is used to store the certificate
     * after encoding.
     */
    SECItem encodedCertificate = { siBuffer, NULL, 0 };
    SECItem certificateToEncode = { siBuffer, NULL, 0 };
    SECStatus rv = SECFailure;

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    SSL_TRC(30, ("%d: TLS13[%d]: %s is encoding the certificate using the %s compression algorithm",
                 SSL_GETPID(), ss->fd, SSL_ROLE(ss),
                 ssl3_mapCertificateCompressionAlgorithmToName(ss, ss->xtnData.compressionAlg)));

    PRINT_BUF(50, (NULL, "The certificate before encoding:",
                   bufferCertificate->buf, bufferCertificate->len));

    PRUint32 lengthUnencodedMessage = bufferCertificate->len;
    rv = ssl3_CopyToSECItem(bufferCertificate, &certificateToEncode);
    if (rv != SECSuccess) {
        SSL_TRC(50, ("%d: TLS13[%d]: %s has failed encoding the certificate.",
                     SSL_GETPID(), ss->fd, SSL_ROLE(ss)));
        goto loser; /* Code already set. */
    }

    rv = tls13_FindCompressionAlgAndEncodeCertificate(ss, &certificateToEncode,
                                                      &encodedCertificate);
    if (rv != SECSuccess) {
        SSL_TRC(50, ("%d: TLS13[%d]: %s has failed encoding the certificate.",
                     SSL_GETPID(), ss->fd, SSL_ROLE(ss)));
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        goto loser; /* Code already set. */
    }

    /* The CompressedCertificate message is formed as follows:
     * struct {
     *   CertificateCompressionAlgorithm algorithm;
     *         uint24 uncompressed_length;
     *         opaque compressed_certificate_message<1..2^24-1>;
     *    } CompressedCertificate;
     */

    if (encodedCertificate.len < 1) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        goto loser;
    }

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_compressed_certificate,
                                    encodedCertificate.len + 2 + 3 + 3);
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }

    rv = ssl3_AppendHandshakeNumber(ss, ss->xtnData.compressionAlg, 2);
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }

    rv = ssl3_AppendHandshakeNumber(ss, lengthUnencodedMessage, 3);
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }

    PRINT_BUF(30, (NULL, "The encoded certificate: ",
                   encodedCertificate.data, encodedCertificate.len));

    rv = ssl3_AppendHandshakeVariable(ss, encodedCertificate.data, encodedCertificate.len, 3);
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }

    SECITEM_FreeItem(&certificateToEncode, PR_FALSE);
    SECITEM_FreeItem(&encodedCertificate, PR_FALSE);
    return SECSuccess;

loser:
    SECITEM_FreeItem(&certificateToEncode, PR_FALSE);
    SECITEM_FreeItem(&encodedCertificate, PR_FALSE);
    return SECFailure;
}

/*
 *    opaque ASN1Cert<1..2^24-1>;
 *
 *    struct {
 *        ASN1Cert cert_data;
 *        Extension extensions<0..2^16-1>;
 *    } CertificateEntry;
 *
 *    struct {
 *        opaque certificate_request_context<0..2^8-1>;
 *        CertificateEntry certificate_list<0..2^24-1>;
 *    } Certificate;
 */
static SECStatus
tls13_SendCertificate(sslSocket *ss)
{
    SECStatus rv;
    CERTCertificateList *certChain;
    int certChainLen = 0;
    int i;
    SECItem context = { siBuffer, NULL, 0 };
    sslBuffer extensionBuf = SSL_BUFFER_EMPTY;
    sslBuffer bufferCertificate = SSL_BUFFER_EMPTY;

    SSL_TRC(3, ("%d: TLS1.3[%d]: send certificate handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (ss->sec.isServer) {
        PORT_Assert(!ss->sec.localCert);
        /* A server certificate is selected in tls13_SelectServerCert(). */
        PORT_Assert(ss->sec.serverCert);

        certChain = ss->sec.serverCert->serverCertChain;
        ss->sec.localCert = CERT_DupCertificate(ss->sec.serverCert->serverCert);
    } else {
        if (ss->sec.localCert)
            CERT_DestroyCertificate(ss->sec.localCert);

        certChain = ss->ssl3.clientCertChain;
        ss->sec.localCert = CERT_DupCertificate(ss->ssl3.clientCertificate);
    }

    if (!ss->sec.isServer) {
        PORT_Assert(ss->ssl3.hs.clientCertRequested);
        context = ss->xtnData.certReqContext;
    }

    if (certChain) {
        for (i = 0; i < certChain->len; i++) {
            /* Each cert is 3 octet length, cert, and extensions */
            certChainLen += 3 + certChain->certs[i].len + 2;
        }

        /* Build the extensions. This only applies to the leaf cert, because we
         * don't yet send extensions for non-leaf certs. */
        rv = ssl_ConstructExtensions(ss, &extensionBuf, ssl_hs_certificate);
        if (rv != SECSuccess) {
            return SECFailure; /* code already set */
        }
        /* extensionBuf.len is only added once, for the leaf cert. */
        certChainLen += SSL_BUFFER_LEN(&extensionBuf);
    }

    rv = sslBuffer_AppendVariable(&bufferCertificate, context.data, context.len, 1);
    if (rv != SECSuccess) {
        goto loser; /* Code already set. */
    }

    rv = sslBuffer_AppendNumber(&bufferCertificate, certChainLen, 3);
    if (rv != SECSuccess) {
        goto loser; /* Code already set. */
    }

    if (certChain) {
        for (i = 0; i < certChain->len; i++) {
            rv = sslBuffer_AppendVariable(&bufferCertificate, certChain->certs[i].data,
                                          certChain->certs[i].len, 3);
            if (rv != SECSuccess) {
                goto loser; /* Code already set. */
            }

            if (i) {
                /* Not end-entity. */
                rv = sslBuffer_AppendNumber(&bufferCertificate, 0, 2);
                if (rv != SECSuccess) {
                    goto loser; /* Code already set. */
                }
                continue;
            }

            rv = sslBuffer_AppendBufferVariable(&bufferCertificate, &extensionBuf, 2);
            if (rv != SECSuccess) {
                goto loser; /* Code already set. */
            }
        }
    }

    /* If no compression mechanism was established or
     * the compression mechanism supports only decoding,
     * we continue as before. */
    if (ss->xtnData.compressionAlg == 0 || !tls13_FindCompressionAlgAndCheckIfSupportsEncoding(ss)) {
        rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_certificate,
                                        1 + context.len + 3 + certChainLen);
        if (rv != SECSuccess) {
            goto loser; /* err set by AppendHandshake. */
        }
        rv = ssl3_AppendBufferToHandshake(ss, &bufferCertificate);
        if (rv != SECSuccess) {
            goto loser; /* err set by AppendHandshake. */
        }
    } else {
        rv = tls13_SendCompressedCertificate(ss, &bufferCertificate);
        if (rv != SECSuccess) {
            goto loser; /* err set by tls13_SendCompressedCertificate. */
        }
    }

    sslBuffer_Clear(&bufferCertificate);
    sslBuffer_Clear(&extensionBuf);
    return SECSuccess;

loser:
    sslBuffer_Clear(&bufferCertificate);
    sslBuffer_Clear(&extensionBuf);
    return SECFailure;
}

static SECStatus
tls13_HandleCertificateEntry(sslSocket *ss, SECItem *data, PRBool first,
                             SECItem *certData)
{
    SECStatus rv;
    SECItem extensionsData;

    rv = ssl3_ConsumeHandshakeVariable(ss, certData,
                                       3, &data->data, &data->len);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = ssl3_ConsumeHandshakeVariable(ss, &extensionsData,
                                       2, &data->data, &data->len);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* Parse all the extensions. */
    if (first && !ss->sec.isServer) {
        rv = ssl3_HandleExtensions(ss, &extensionsData.data,
                                   &extensionsData.len,
                                   ssl_hs_certificate);
        if (rv != SECSuccess) {
            return SECFailure;
        }
        /* TODO(ekr@rtfm.com): Copy out SCTs. Bug 1315727. */
    }

    return SECSuccess;
}

static SECStatus
tls13_EnsureCerticateExpected(sslSocket *ss)
{
    SECStatus rv = SECFailure;
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (ss->sec.isServer) {
        /* Receiving this message might be the first sign we have that
         * early data is over, so pretend we received EOED. */
        rv = tls13_MaybeHandleSuppressedEndOfEarlyData(ss);
        if (rv != SECSuccess) {
            return SECFailure; /* Code already set. */
        }

        if (ss->ssl3.clientCertRequested) {
            rv = TLS13_CHECK_HS_STATE(ss, SSL_ERROR_RX_UNEXPECTED_CERTIFICATE,
                                      idle_handshake);
        } else {
            rv = TLS13_CHECK_HS_STATE(ss, SSL_ERROR_RX_UNEXPECTED_CERTIFICATE,
                                      wait_client_cert);
        }
    } else {
        rv = TLS13_CHECK_HS_STATE(ss, SSL_ERROR_RX_UNEXPECTED_CERTIFICATE,
                                  wait_cert_request, wait_server_cert);
    }
    return rv;
}

/* RFC 8879 TLS Certificate Compression
 * struct {
 *  CertificateCompressionAlgorithm algorithm;
 *  uint24 uncompressed_length;
 *  opaque compressed_certificate_message<1..2^24-1>;
 * } CompressedCertificate;
 */
static SECStatus
tls13_HandleCertificateDecode(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    SECStatus rv = SECFailure;

    if (!ss->xtnData.certificateCompressionAdvertised) {
        FATAL_ERROR(ss, SEC_ERROR_UNEXPECTED_COMPRESSED_CERTIFICATE, decode_error);
        return SECFailure;
    }

    rv = tls13_EnsureCerticateExpected(ss);
    if (rv != SECSuccess) {
        return SECFailure; /* Code already set. */
    }

    if (ss->firstHsDone) {
        rv = ssl_HashPostHandshakeMessage(ss, ssl_hs_compressed_certificate, b, length);
        if (rv != SECSuccess) {
            return rv;
        }
    }

    SSL_TRC(30, ("%d: TLS1.3[%d]: %s handles certificate compression handshake",
                 SSL_GETPID(), ss->fd, SSL_ROLE(ss)));

    PRINT_BUF(50, (NULL, "The certificate before decoding:", b, length));
    /* Reading CertificateCompressionAlgorithm. */
    PRUint32 compressionAlg = 0;
    rv = ssl3_ConsumeHandshakeNumber(ss, &compressionAlg, 2, &b, &length);
    if (rv != SECSuccess) {
        return SECFailure; /* Alert already sent. */
    }

    PRBool compressionAlgorithmIsSupported = PR_FALSE;
    SECStatus (*certificateDecodingFunc)(const SECItem *,
                                         unsigned char *output, size_t outputLen, size_t *usedLen) = NULL;
    for (int i = 0; i < ss->ssl3.supportedCertCompressionAlgorithmsCount; i++) {
        if (ss->ssl3.supportedCertCompressionAlgorithms[i].id == compressionAlg) {
            compressionAlgorithmIsSupported = PR_TRUE;
            certificateDecodingFunc = ss->ssl3.supportedCertCompressionAlgorithms[i].decode;
        }
    }

    /* Peer selected a compression algorithm we do not support (and did not advertise). */
    if (!compressionAlgorithmIsSupported) {
        PORT_SetError(SEC_ERROR_CERTIFICATE_COMPRESSION_ALGORITHM_NOT_SUPPORTED);
        FATAL_ERROR(ss, PORT_GetError(), illegal_parameter);
        return SECFailure;
    }

    /* The algorithm does not support decoding. */
    if (certificateDecodingFunc == NULL) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        FATAL_ERROR(ss, PORT_GetError(), illegal_parameter);
        return SECFailure;
    }

    SSL_TRC(30, ("%d: TLS13[%d]: %s is decoding the certificate using the %s compression algorithm",
                 SSL_GETPID(), ss->fd, SSL_ROLE(ss),
                 ssl3_mapCertificateCompressionAlgorithmToName(ss, compressionAlg)));
    PRUint32 decodedCertLen = 0;
    rv = ssl3_ConsumeHandshakeNumber(ss, &decodedCertLen, 3, &b, &length);
    if (rv != SECSuccess) {
        return SECFailure; /* alert has been sent */
    }

    /*  If the received CompressedCertificate message cannot be decompressed,
     *  he connection MUST be terminated with the "bad_certificate" alert.
     */
    if (decodedCertLen == 0) {
        SSL_TRC(50, ("%d: TLS13[%d]: %s decoded certificate length is incorrect",
                     SSL_GETPID(), ss->fd, SSL_ROLE(ss),
                     ssl3_mapCertificateCompressionAlgorithmToName(ss, compressionAlg)));
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CERTIFICATE, bad_certificate);
        return SECFailure;
    }

    /* opaque compressed_certificate_message<1..2^24-1>; */
    PRUint32 compressedCertLen = 0;
    rv = ssl3_ConsumeHandshakeNumber(ss, &compressedCertLen, 3, &b, &length);
    if (rv != SECSuccess) {
        return SECFailure; /* alert has been sent */
    }

    if (compressedCertLen == 0 || compressedCertLen != length) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CERTIFICATE, bad_certificate);
        return SECFailure;
    }

    /* Decoding received certificate. */
    PRUint8 *decodedCert = PORT_ZAlloc(decodedCertLen);
    if (!decodedCert) {
        return SECFailure;
    }

    size_t actualCertLen = 0;

    SECItem encodedCertAsSecItem = { siBuffer, b, compressedCertLen };
    rv = certificateDecodingFunc(&encodedCertAsSecItem,
                                 decodedCert, decodedCertLen, &actualCertLen);

    if (rv != SECSuccess) {
        SSL_TRC(50, ("%d: TLS13[%d]: %s decoding of the certificate has failed",
                     SSL_GETPID(), ss->fd, SSL_ROLE(ss),
                     ssl3_mapCertificateCompressionAlgorithmToName(ss, compressionAlg)));
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CERTIFICATE, bad_certificate);
        goto loser;
    }
    PRINT_BUF(60, (ss, "consume bytes:", b, compressedCertLen));
    *b += compressedCertLen;
    length -= compressedCertLen;

    /*  If, after decompression, the specified length does not match the actual length,
     *  the party receiving the invalid message MUST abort the connection
     *  with the "bad_certificate" alert.
     */
    if (actualCertLen != decodedCertLen) {
        SSL_TRC(50, ("%d: TLS13[%d]: %s certificate length does not correspond to extension length",
                     SSL_GETPID(), ss->fd, SSL_ROLE(ss),
                     ssl3_mapCertificateCompressionAlgorithmToName(ss, compressionAlg)));
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CERTIFICATE, bad_certificate);
        goto loser;
    }

    PRINT_BUF(50, (NULL, "Decoded certificate",
                   decodedCert, decodedCertLen));

    /* compressed_certificate_message:  The result of applying the indicated
     * compression algorithm to the encoded Certificate message that
     *  would have been sent if certificate compression was not in use.
     *
     * After decompression, the Certificate message MUST be processed as if
     * it were encoded without being compressed.  This way, the parsing and
     * the verification have the same security properties as they would have
     * in TLS normally.
     */
    rv = tls13_HandleCertificate(ss, decodedCert, decodedCertLen, PR_TRUE);
    if (rv != SECSuccess) {
        goto loser;
    }
    /* We allow only one compressed certificate to be handled after each
       certificate compression advertisement.
       See test CertificateCompression_TwoEncodedCertificateRequests. */
    ss->xtnData.certificateCompressionAdvertised = PR_FALSE;
    PORT_Free(decodedCert);
    return SECSuccess;

loser:
    PORT_Free(decodedCert);
    return SECFailure;
}

/* Called from tls13_CompleteHandleHandshakeMessage() when it has deciphered a complete
 * tls13 Certificate message.
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
tls13_HandleCertificate(sslSocket *ss, PRUint8 *b, PRUint32 length, PRBool alreadyHashed)
{
    SECStatus rv;
    SECItem context = { siBuffer, NULL, 0 };
    SECItem certList;
    PRBool first = PR_TRUE;
    ssl3CertNode *lastCert = NULL;

    SSL_TRC(3, ("%d: TLS13[%d]: handle certificate handshake",
                SSL_GETPID(), ss->fd));
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    rv = tls13_EnsureCerticateExpected(ss);
    if (rv != SECSuccess) {
        return SECFailure; /* Code already set. */
    }

    /* We can ignore any other cleartext from the client. */
    if (ss->sec.isServer && IS_DTLS(ss)) {
        ssl_CipherSpecReleaseByEpoch(ss, ssl_secret_read, TrafficKeyClearText);
        dtls_ReceivedFirstMessageInFlight(ss);
    }

    /* AlreadyHashed is true only when Certificate Compression is used. */
    if (ss->firstHsDone && !alreadyHashed) {
        rv = ssl_HashPostHandshakeMessage(ss, ssl_hs_certificate, b, length);
        if (rv != SECSuccess) {
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }
    }

    if (!ss->firstHsDone && ss->sec.isServer) {
        /* Our first shot an getting an RTT estimate.  If the client took extra
         * time to fetch a certificate, this will be bad, but we can't do much
         * about that. */
        ss->ssl3.hs.rttEstimate = ssl_Time(ss) - ss->ssl3.hs.rttEstimate;
    }

    /* Process the context string */
    rv = ssl3_ConsumeHandshakeVariable(ss, &context, 1, &b, &length);
    if (rv != SECSuccess)
        return SECFailure;

    if (ss->ssl3.clientCertRequested) {
        PORT_Assert(ss->sec.isServer);
        if (SECITEM_CompareItem(&context, &ss->xtnData.certReqContext) != 0) {
            FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CERTIFICATE, illegal_parameter);
            return SECFailure;
        }
    }
    rv = ssl3_ConsumeHandshakeVariable(ss, &certList, 3, &b, &length);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    if (length) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CERTIFICATE, illegal_parameter);
        return SECFailure;
    }

    if (!certList.len) {
        if (!ss->sec.isServer) {
            /* Servers always need to send some cert. */
            FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CERTIFICATE, bad_certificate);
            return SECFailure;
        } else {
            /* This is TLS's version of a no_certificate alert. */
            /* I'm a server. I've requested a client cert. He hasn't got one. */
            rv = ssl3_HandleNoCertificate(ss);
            if (rv != SECSuccess) {
                return SECFailure;
            }

            TLS13_SET_HS_STATE(ss, wait_finished);
            return SECSuccess;
        }
    }

    /* Now clean up. */
    ssl3_CleanupPeerCerts(ss);
    ss->ssl3.peerCertArena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (ss->ssl3.peerCertArena == NULL) {
        FATAL_ERROR(ss, SEC_ERROR_NO_MEMORY, internal_error);
        return SECFailure;
    }

    while (certList.len) {
        SECItem derCert; // will hold a weak reference into certList
        rv = tls13_HandleCertificateEntry(ss, &certList, first,
                                          &derCert);
        if (rv != SECSuccess) {
            ss->xtnData.signedCertTimestamps.len = 0;
            return SECFailure;
        }

        if (first) {
            ss->sec.peerCert = CERT_NewTempCertificate(ss->dbHandle, &derCert,
                                                       NULL, PR_FALSE, PR_TRUE);
            if (!ss->sec.peerCert) {
                PRErrorCode errCode = PORT_GetError();
                switch (errCode) {
                    case PR_OUT_OF_MEMORY_ERROR:
                    case SEC_ERROR_BAD_DATABASE:
                    case SEC_ERROR_NO_MEMORY:
                        FATAL_ERROR(ss, errCode, internal_error);
                        return SECFailure;
                    default:
                        ssl3_SendAlertForCertError(ss, errCode);
                        return SECFailure;
                }
            }

            if (ss->xtnData.signedCertTimestamps.len) {
                sslSessionID *sid = ss->sec.ci.sid;
                rv = SECITEM_CopyItem(NULL, &sid->u.ssl3.signedCertTimestamps,
                                      &ss->xtnData.signedCertTimestamps);
                ss->xtnData.signedCertTimestamps.len = 0;
                if (rv != SECSuccess) {
                    FATAL_ERROR(ss, SEC_ERROR_NO_MEMORY, internal_error);
                    return SECFailure;
                }
            }
        } else {
            ssl3CertNode *c = PORT_ArenaNew(ss->ssl3.peerCertArena,
                                            ssl3CertNode);
            if (!c) {
                FATAL_ERROR(ss, SEC_ERROR_NO_MEMORY, internal_error);
                return SECFailure;
            }
            c->derCert = SECITEM_ArenaDupItem(ss->ssl3.peerCertArena,
                                              &derCert);
            c->next = NULL;

            if (lastCert) {
                lastCert->next = c;
            } else {
                ss->ssl3.peerCertChain = c;
            }
            lastCert = c;
        }

        first = PR_FALSE;
    }
    SECKEY_UpdateCertPQG(ss->sec.peerCert);

    return ssl3_AuthCertificate(ss); /* sets ss->ssl3.hs.ws */
}

/* Add context to the hash functions as described in
   [draft-ietf-tls-tls13; Section 4.9.1] */
SECStatus
tls13_AddContextToHashes(sslSocket *ss, const SSL3Hashes *hashes,
                         SSLHashType algorithm, PRBool sending,
                         SSL3Hashes *tbsHash)
{
    SECStatus rv = SECSuccess;
    PK11Context *ctx;
    const unsigned char context_padding[] = {
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20
    };

    const char *client_cert_verify_string = "TLS 1.3, client CertificateVerify";
    const char *server_cert_verify_string = "TLS 1.3, server CertificateVerify";
    const char *context_string = (sending ^ ss->sec.isServer) ? client_cert_verify_string
                                                              : server_cert_verify_string;
    unsigned int hashlength;

    /* Double check that we are doing the same hash.*/
    PORT_Assert(hashes->len == tls13_GetHashSize(ss));

    ctx = PK11_CreateDigestContext(ssl3_HashTypeToOID(algorithm));
    if (!ctx) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        goto loser;
    }

    PORT_Assert(SECFailure);
    PORT_Assert(!SECSuccess);

    PRINT_BUF(50, (ss, "TLS 1.3 hash without context", hashes->u.raw, hashes->len));
    PRINT_BUF(50, (ss, "Context string", context_string, strlen(context_string)));
    rv |= PK11_DigestBegin(ctx);
    rv |= PK11_DigestOp(ctx, context_padding, sizeof(context_padding));
    rv |= PK11_DigestOp(ctx, (unsigned char *)context_string,
                        strlen(context_string) + 1); /* +1 includes the terminating 0 */
    rv |= PK11_DigestOp(ctx, hashes->u.raw, hashes->len);
    /* Update the hash in-place */
    rv |= PK11_DigestFinal(ctx, tbsHash->u.raw, &hashlength, sizeof(tbsHash->u.raw));
    PK11_DestroyContext(ctx, PR_TRUE);
    PRINT_BUF(50, (ss, "TLS 1.3 hash with context", tbsHash->u.raw, hashlength));

    tbsHash->len = hashlength;
    tbsHash->hashAlg = algorithm;

    if (rv) {
        ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
        goto loser;
    }
    return SECSuccess;

loser:
    return SECFailure;
}

/*
 *    Derive-Secret(Secret, Label, Messages) =
 *       HKDF-Expand-Label(Secret, Label,
 *                         Hash(Messages) + Hash(resumption_context), L))
 */
SECStatus
tls13_DeriveSecret(sslSocket *ss, PK11SymKey *key,
                   const char *label,
                   unsigned int labelLen,
                   const SSL3Hashes *hashes,
                   PK11SymKey **dest,
                   SSLHashType hash)
{
    SECStatus rv;

    rv = tls13_HkdfExpandLabel(key, hash, hashes->u.raw, hashes->len,
                               label, labelLen, CKM_HKDF_DERIVE,
                               tls13_GetHashSizeForHash(hash),
                               ss->protocolVariant, dest);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    return SECSuccess;
}

/* Convenience wrapper for the empty hash. */
SECStatus
tls13_DeriveSecretNullHash(sslSocket *ss, PK11SymKey *key,
                           const char *label,
                           unsigned int labelLen,
                           PK11SymKey **dest,
                           SSLHashType hash)
{
    SSL3Hashes hashes;
    SECStatus rv;
    PRUint8 buf[] = { 0 };

    rv = tls13_ComputeHash(ss, &hashes, buf, 0, hash);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    return tls13_DeriveSecret(ss, key, label, labelLen, &hashes, dest, hash);
}

/* Convenience wrapper that lets us supply a separate prefix and suffix. */
static SECStatus
tls13_DeriveSecretWrap(sslSocket *ss, PK11SymKey *key,
                       const char *prefix,
                       const char *suffix,
                       const char *keylogLabel,
                       PK11SymKey **dest)
{
    SECStatus rv;
    SSL3Hashes hashes;
    char buf[100];
    const char *label;

    if (prefix) {
        if ((strlen(prefix) + strlen(suffix) + 2) > sizeof(buf)) {
            PORT_Assert(0);
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }
        (void)PR_snprintf(buf, sizeof(buf), "%s %s",
                          prefix, suffix);
        label = buf;
    } else {
        label = suffix;
    }

    SSL_TRC(3, ("%d: TLS13[%d]: deriving secret '%s'",
                SSL_GETPID(), ss->fd, label));
    rv = tls13_ComputeHandshakeHashes(ss, &hashes);
    if (rv != SECSuccess) {
        PORT_Assert(0); /* Should never fail */
        ssl_MapLowLevelError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    rv = tls13_DeriveSecret(ss, key, label, strlen(label),
                            &hashes, dest, tls13_GetHash(ss));
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (keylogLabel) {
        ssl3_RecordKeyLog(ss, keylogLabel, *dest);
    }
    return SECSuccess;
}

SECStatus
SSLExp_SecretCallback(PRFileDesc *fd, SSLSecretCallback cb, void *arg)
{
    sslSocket *ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SecretCallback",
                 SSL_GETPID(), fd));
        return SECFailure;
    }

    ssl_Get1stHandshakeLock(ss);
    ssl_GetSSL3HandshakeLock(ss);
    ss->secretCallback = cb;
    ss->secretCallbackArg = arg;
    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_Release1stHandshakeLock(ss);
    return SECSuccess;
}

/* Derive traffic keys for the next cipher spec in the queue. */
static SECStatus
tls13_DeriveTrafficKeys(sslSocket *ss, ssl3CipherSpec *spec,
                        TrafficKeyType type,
                        PRBool deleteSecret)
{
    size_t keySize = spec->cipherDef->key_size;
    size_t ivSize = spec->cipherDef->iv_size +
                    spec->cipherDef->explicit_nonce_size; /* This isn't always going to
                                                           * work, but it does for
                                                           * AES-GCM */
    CK_MECHANISM_TYPE bulkAlgorithm = ssl3_Alg2Mech(spec->cipherDef->calg);
    PK11SymKey **prkp = NULL;
    PK11SymKey *prk = NULL;
    PRBool clientSecret;
    SECStatus rv;
    /* These labels are just used for debugging. */
    static const char kHkdfPhaseEarlyApplicationDataKeys[] = "early application data";
    static const char kHkdfPhaseHandshakeKeys[] = "handshake data";
    static const char kHkdfPhaseApplicationDataKeys[] = "application data";

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    clientSecret = !tls13_UseServerSecret(ss, spec->direction);
    switch (type) {
        case TrafficKeyEarlyApplicationData:
            PORT_Assert(clientSecret);
            prkp = &ss->ssl3.hs.clientEarlyTrafficSecret;
            spec->phase = kHkdfPhaseEarlyApplicationDataKeys;
            break;
        case TrafficKeyHandshake:
            prkp = clientSecret ? &ss->ssl3.hs.clientHsTrafficSecret
                                : &ss->ssl3.hs.serverHsTrafficSecret;
            spec->phase = kHkdfPhaseHandshakeKeys;
            break;
        case TrafficKeyApplicationData:
            prkp = clientSecret ? &ss->ssl3.hs.clientTrafficSecret
                                : &ss->ssl3.hs.serverTrafficSecret;
            spec->phase = kHkdfPhaseApplicationDataKeys;
            break;
        default:
            LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
            PORT_Assert(0);
            return SECFailure;
    }
    PORT_Assert(prkp != NULL);
    prk = *prkp;

    SSL_TRC(3, ("%d: TLS13[%d]: deriving %s traffic keys epoch=%d (%s)",
                SSL_GETPID(), ss->fd, SPEC_DIR(spec),
                spec->epoch, spec->phase));

    rv = tls13_HkdfExpandLabel(prk, tls13_GetHash(ss),
                               NULL, 0,
                               kHkdfPurposeKey, strlen(kHkdfPurposeKey),
                               bulkAlgorithm, keySize,
                               ss->protocolVariant,
                               &spec->keyMaterial.key);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        PORT_Assert(0);
        goto loser;
    }

    if (IS_DTLS(ss) && spec->epoch > 0) {
        rv = ssl_CreateMaskingContextInner(spec->version, ss->ssl3.hs.cipher_suite,
                                           ss->protocolVariant, prk, kHkdfPurposeSn,
                                           strlen(kHkdfPurposeSn), &spec->maskContext);
        if (rv != SECSuccess) {
            LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
            PORT_Assert(0);
            goto loser;
        }
    }

    rv = tls13_HkdfExpandLabelRaw(prk, tls13_GetHash(ss),
                                  NULL, 0,
                                  kHkdfPurposeIv, strlen(kHkdfPurposeIv),
                                  ss->protocolVariant,
                                  spec->keyMaterial.iv, ivSize);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        PORT_Assert(0);
        goto loser;
    }

    if (deleteSecret) {
        PK11_FreeSymKey(prk);
        *prkp = NULL;
    }
    return SECSuccess;

loser:
    return SECFailure;
}

void
tls13_SetSpecRecordVersion(sslSocket *ss, ssl3CipherSpec *spec)
{
    /* Set the record version to pretend to be (D)TLS 1.2. */
    if (IS_DTLS(ss)) {
        spec->recordVersion = SSL_LIBRARY_VERSION_DTLS_1_2_WIRE;
    } else {
        spec->recordVersion = SSL_LIBRARY_VERSION_TLS_1_2;
    }
    SSL_TRC(10, ("%d: TLS13[%d]: set spec=%d record version to 0x%04x",
                 SSL_GETPID(), ss->fd, spec, spec->recordVersion));
}

static SECStatus
tls13_SetupPendingCipherSpec(sslSocket *ss, ssl3CipherSpec *spec)
{
    ssl3CipherSuite suite = ss->ssl3.hs.cipher_suite;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(spec->epoch);

    /* Version isn't set when we send 0-RTT data. */
    spec->version = PR_MAX(SSL_LIBRARY_VERSION_TLS_1_3, ss->version);

    ssl_SaveCipherSpec(ss, spec);
    /* We want to keep read cipher specs around longer because
     * there are cases where we might get either epoch N or
     * epoch N+1. */
    if (IS_DTLS(ss) && spec->direction == ssl_secret_read) {
        ssl_CipherSpecAddRef(spec);
    }

    SSL_TRC(3, ("%d: TLS13[%d]: Set Pending Cipher Suite to 0x%04x",
                SSL_GETPID(), ss->fd, suite));

    spec->cipherDef = ssl_GetBulkCipherDef(ssl_LookupCipherSuiteDef(suite));

    if (spec->epoch == TrafficKeyEarlyApplicationData) {
        if (ss->xtnData.selectedPsk &&
            ss->xtnData.selectedPsk->zeroRttSuite != TLS_NULL_WITH_NULL_NULL) {
            spec->earlyDataRemaining = ss->xtnData.selectedPsk->maxEarlyData;
        }
    }

    tls13_SetSpecRecordVersion(ss, spec);

    /* The record size limit is reduced by one so that the remainder of the
     * record handling code can use the same checks for all versions. */
    if (ssl3_ExtensionNegotiated(ss, ssl_record_size_limit_xtn)) {
        spec->recordSizeLimit = ((spec->direction == ssl_secret_read)
                                     ? ss->opt.recordSizeLimit
                                     : ss->xtnData.recordSizeLimit) -
                                1;
    } else {
        spec->recordSizeLimit = MAX_FRAGMENT_LENGTH;
    }
    return SECSuccess;
}

/*
 * Initialize the cipher context. All TLS 1.3 operations are AEAD,
 * so they are all message contexts.
 */
static SECStatus
tls13_InitPendingContext(sslSocket *ss, ssl3CipherSpec *spec)
{
    CK_MECHANISM_TYPE encMechanism;
    CK_ATTRIBUTE_TYPE encMode;
    SECItem iv;
    SSLCipherAlgorithm calg;

    calg = spec->cipherDef->calg;

    encMechanism = ssl3_Alg2Mech(calg);
    encMode = CKA_NSS_MESSAGE | ((spec->direction == ssl_secret_write) ? CKA_ENCRYPT : CKA_DECRYPT);
    iv.data = NULL;
    iv.len = 0;

    /*
     * build the context
     */
    spec->cipherContext = PK11_CreateContextBySymKey(encMechanism, encMode,
                                                     spec->keyMaterial.key,
                                                     &iv);
    if (!spec->cipherContext) {
        ssl_MapLowLevelError(SSL_ERROR_SYM_KEY_CONTEXT_FAILURE);
        return SECFailure;
    }
    return SECSuccess;
}

/*
 * Called before sending alerts to set up the right key on the client.
 * We might encounter errors during the handshake where the current
 * key is ClearText or EarlyApplicationData. This
 * function switches to the Handshake key if possible.
 */
SECStatus
tls13_SetAlertCipherSpec(sslSocket *ss)
{
    SECStatus rv;

    if (ss->sec.isServer) {
        return SECSuccess;
    }
    if (ss->version < SSL_LIBRARY_VERSION_TLS_1_3) {
        return SECSuccess;
    }
    if (TLS13_IN_HS_STATE(ss, wait_server_hello)) {
        return SECSuccess;
    }
    if ((ss->ssl3.cwSpec->epoch != TrafficKeyClearText) &&
        (ss->ssl3.cwSpec->epoch != TrafficKeyEarlyApplicationData)) {
        return SECSuccess;
    }

    rv = tls13_SetCipherSpec(ss, TrafficKeyHandshake,
                             ssl_secret_write, PR_FALSE);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    return SECSuccess;
}

/* Install a new cipher spec for this direction.
 *
 * During the handshake, the values for |epoch| take values from the
 * TrafficKeyType enum.  Afterwards, key update increments them.
 */
static SECStatus
tls13_SetCipherSpec(sslSocket *ss, PRUint16 epoch,
                    SSLSecretDirection direction, PRBool deleteSecret)
{
    TrafficKeyType type;
    SECStatus rv;
    ssl3CipherSpec *spec = NULL;
    ssl3CipherSpec **specp;

    /* Flush out old handshake data. */
    ssl_GetXmitBufLock(ss);
    rv = ssl3_FlushHandshake(ss, ssl_SEND_FLAG_FORCE_INTO_BUFFER);
    ssl_ReleaseXmitBufLock(ss);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* Create the new spec. */
    spec = ssl_CreateCipherSpec(ss, direction);
    if (!spec) {
        return SECFailure;
    }
    spec->epoch = epoch;
    spec->nextSeqNum = 0;
    if (IS_DTLS(ss)) {
        dtls_InitRecvdRecords(&spec->recvdRecords);
    }

    /* This depends on spec having a valid direction and epoch. */
    rv = tls13_SetupPendingCipherSpec(ss, spec);
    if (rv != SECSuccess) {
        goto loser;
    }

    type = (TrafficKeyType)PR_MIN(TrafficKeyApplicationData, epoch);
    rv = tls13_DeriveTrafficKeys(ss, spec, type, deleteSecret);
    if (rv != SECSuccess) {
        goto loser;
    }

    rv = tls13_InitPendingContext(ss, spec);
    if (rv != SECSuccess) {
        goto loser;
    }

    /* Now that we've set almost everything up, finally cut over. */
    specp = (direction == ssl_secret_read) ? &ss->ssl3.crSpec : &ss->ssl3.cwSpec;
    ssl_GetSpecWriteLock(ss);
    ssl_CipherSpecRelease(*specp); /* May delete old cipher. */
    *specp = spec;                 /* Overwrite. */
    ssl_ReleaseSpecWriteLock(ss);

    SSL_TRC(3, ("%d: TLS13[%d]: %s installed key for epoch=%d (%s) dir=%s",
                SSL_GETPID(), ss->fd, SSL_ROLE(ss), spec->epoch,
                spec->phase, SPEC_DIR(spec)));
    return SECSuccess;

loser:
    ssl_CipherSpecRelease(spec);
    return SECFailure;
}

SECStatus
tls13_ComputeHandshakeHashes(sslSocket *ss, SSL3Hashes *hashes)
{
    SECStatus rv;
    PK11Context *ctx = NULL;
    PRBool useEchInner;
    sslBuffer *transcript;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    if (ss->ssl3.hs.hashType == handshake_hash_unknown) {
        /* Backup: if we haven't done any hashing, then hash now.
         * This happens when we are doing 0-RTT on the client. */
        ctx = PK11_CreateDigestContext(ssl3_HashTypeToOID(tls13_GetHash(ss)));
        if (!ctx) {
            ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
            return SECFailure;
        }

        if (PK11_DigestBegin(ctx) != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
            goto loser;
        }

        /* One might expect this to use ss->ssl3.hs.echAccepted,
         * but with 0-RTT we don't know that yet. */
        useEchInner = ss->sec.isServer ? PR_FALSE : !!ss->ssl3.hs.echHpkeCtx;
        transcript = useEchInner ? &ss->ssl3.hs.echInnerMessages : &ss->ssl3.hs.messages;

        PRINT_BUF(10, (ss, "Handshake hash computed over saved messages",
                       transcript->buf,
                       transcript->len));

        if (PK11_DigestOp(ctx,
                          transcript->buf,
                          transcript->len) != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
            goto loser;
        }
    } else {
        if (ss->firstHsDone) {
            ctx = PK11_CloneContext(ss->ssl3.hs.shaPostHandshake);
        } else {
            ctx = PK11_CloneContext(ss->ssl3.hs.sha);
        }
        if (!ctx) {
            ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
            return SECFailure;
        }
    }

    rv = PK11_DigestFinal(ctx, hashes->u.raw,
                          &hashes->len,
                          sizeof(hashes->u.raw));
    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SSL_ERROR_DIGEST_FAILURE);
        goto loser;
    }

    PRINT_BUF(10, (ss, "Handshake hash", hashes->u.raw, hashes->len));
    PORT_Assert(hashes->len == tls13_GetHashSize(ss));
    PK11_DestroyContext(ctx, PR_TRUE);

    return SECSuccess;

loser:
    PK11_DestroyContext(ctx, PR_TRUE);
    return SECFailure;
}

TLS13KeyShareEntry *
tls13_CopyKeyShareEntry(TLS13KeyShareEntry *o)
{
    TLS13KeyShareEntry *n;

    PORT_Assert(o);
    n = PORT_ZNew(TLS13KeyShareEntry);
    if (!n) {
        return NULL;
    }

    if (SECSuccess != SECITEM_CopyItem(NULL, &n->key_exchange, &o->key_exchange)) {
        PORT_Free(n);
        return NULL;
    }
    n->group = o->group;
    return n;
}

void
tls13_DestroyKeyShareEntry(TLS13KeyShareEntry *offer)
{
    if (!offer) {
        return;
    }
    SECITEM_ZfreeItem(&offer->key_exchange, PR_FALSE);
    PORT_ZFree(offer, sizeof(*offer));
}

void
tls13_DestroyKeyShares(PRCList *list)
{
    PRCList *cur_p;

    /* The list must be initialized. */
    PORT_Assert(PR_LIST_HEAD(list));

    while (!PR_CLIST_IS_EMPTY(list)) {
        cur_p = PR_LIST_TAIL(list);
        PR_REMOVE_LINK(cur_p);
        tls13_DestroyKeyShareEntry((TLS13KeyShareEntry *)cur_p);
    }
}

void
tls13_DestroyEarlyData(PRCList *list)
{
    PRCList *cur_p;

    while (!PR_CLIST_IS_EMPTY(list)) {
        TLS13EarlyData *msg;

        cur_p = PR_LIST_TAIL(list);
        msg = (TLS13EarlyData *)cur_p;

        PR_REMOVE_LINK(cur_p);
        SECITEM_ZfreeItem(&msg->data, PR_FALSE);
        PORT_ZFree(msg, sizeof(*msg));
    }
}

/* draft-ietf-tls-tls13 Section 5.2.2 specifies the following
 * nonce algorithm:
 *
 * The length of the per-record nonce (iv_length) is set to max(8 bytes,
 * N_MIN) for the AEAD algorithm (see [RFC5116] Section 4).  An AEAD
 * algorithm where N_MAX is less than 8 bytes MUST NOT be used with TLS.
 * The per-record nonce for the AEAD construction is formed as follows:
 *
 * 1.  The 64-bit record sequence number is padded to the left with
 *     zeroes to iv_length.
 *
 * 2.  The padded sequence number is XORed with the static
 *     client_write_iv or server_write_iv, depending on the role.
 *
 * The resulting quantity (of length iv_length) is used as the per-
 * record nonce.
 *
 * Existing suites have the same nonce size: N_MIN = N_MAX = 12 bytes
 *
 * See RFC 5288 and https://tools.ietf.org/html/draft-ietf-tls-chacha20-poly1305-04#section-2
 */
static void
tls13_WriteNonce(const unsigned char *ivIn, unsigned int ivInLen,
                 const unsigned char *nonce, unsigned int nonceLen,
                 unsigned char *ivOut, unsigned int ivOutLen)
{
    size_t i;
    unsigned int offset = ivOutLen - nonceLen;

    PORT_Assert(ivInLen <= ivOutLen);
    PORT_Assert(nonceLen <= ivOutLen);
    PORT_Memset(ivOut, 0, ivOutLen);
    PORT_Memcpy(ivOut, ivIn, ivInLen);

    /* XOR the last n bytes of the IV with the nonce (should be a counter). */
    for (i = 0; i < nonceLen; ++i) {
        ivOut[offset + i] ^= nonce[i];
    }
    PRINT_BUF(50, (NULL, "Nonce", ivOut, ivOutLen));
}

/* Setup the IV for AEAD encrypt. The PKCS #11 module will add the
 * counter, but it doesn't know about the DTLS epic, so we add it here.
 */
unsigned int
tls13_SetupAeadIv(PRBool isDTLS, SSL3ProtocolVersion v, unsigned char *ivOut, unsigned char *ivIn,
                  unsigned int offset, unsigned int ivLen, DTLSEpoch epoch)
{
    PORT_Memcpy(ivOut, ivIn, ivLen);
    if (isDTLS && v < SSL_LIBRARY_VERSION_TLS_1_3) {
        /* handle the tls 1.2 counter mode case, the epoc is copied
         * instead of xored. We accomplish this by clearing ivOut
         * before running xor. */
        if (offset >= ivLen) {
            ivOut[offset] = ivOut[offset + 1] = 0;
        }
        ivOut[offset] ^= (unsigned char)(epoch >> BPB) & 0xff;
        ivOut[offset + 1] ^= (unsigned char)(epoch)&0xff;
        offset += 2;
    }

    return offset;
}

/*
 * Do a single AEAD for TLS. This differs from PK11_AEADOp in the following
 * ways.
 *   1) If context is not supplied, it treats the operation as a single shot
 *   and creates a context from symKey and mech.
 *   2) It always assumes the tag will be at the end of the buffer
 *   (in on decrypt, out on encrypt) just like the old single shot.
 *   3) If we aren't generating an IV, it uses tls13_WriteNonce to create the
 *   nonce.
 * NOTE is context is supplied, symKey and mech are ignored
 */
SECStatus
tls13_AEAD(PK11Context *context, PRBool decrypt,
           CK_GENERATOR_FUNCTION ivGen, unsigned int fixedbits,
           const unsigned char *ivIn, unsigned char *ivOut, unsigned int ivLen,
           const unsigned char *nonceIn, unsigned int nonceLen,
           const unsigned char *aad, unsigned int aadLen,
           unsigned char *out, unsigned int *outLen, unsigned int maxout,
           unsigned int tagLen, const unsigned char *in, unsigned int inLen)
{
    unsigned char *tag;
    unsigned char iv[MAX_IV_LENGTH];
    unsigned char tagbuf[HASH_LENGTH_MAX];
    SECStatus rv;

    /* must have either context or the symKey set */
    if (!context) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    PORT_Assert(ivLen <= MAX_IV_LENGTH);
    PORT_Assert(tagLen <= HASH_LENGTH_MAX);
    if (!ivOut) {
        ivOut = iv; /* caller doesn't need a returned, iv */
    }

    if (ivGen == CKG_NO_GENERATE) {
        tls13_WriteNonce(ivIn, ivLen, nonceIn, nonceLen, ivOut, ivLen);
    } else if (ivIn != ivOut) {
        PORT_Memcpy(ivOut, ivIn, ivLen);
    }
    if (decrypt) {
        inLen = inLen - tagLen;
        tag = (unsigned char *)in + inLen;
        /* tag is const on decrypt, but returned on encrypt */
    } else {
        /* tag is written to a separate buffer, then added to the end
         * of the actual output buffer. This allows output buffer to be larger
         * than the input buffer and everything still work */
        tag = tagbuf;
    }
    rv = PK11_AEADOp(context, ivGen, fixedbits, ivOut, ivLen, aad, aadLen,
                     out, (int *)outLen, maxout, tag, tagLen, in, inLen);
    /* on encrypt SSL always puts the tag at the end of the buffer */
    if ((rv == SECSuccess) && !(decrypt)) {
        unsigned int len = *outLen;
        /* make sure there is still space */
        if (len + tagLen > maxout) {
            PORT_SetError(SEC_ERROR_OUTPUT_LEN);
            return SECFailure;
        }
        PORT_Memcpy(out + len, tag, tagLen);
        *outLen += tagLen;
    }
    return rv;
}

static SECStatus
tls13_HandleEncryptedExtensions(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    SECStatus rv;
    PRUint32 innerLength;
    SECItem oldAlpn = { siBuffer, NULL, 0 };

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    SSL_TRC(3, ("%d: TLS13[%d]: handle encrypted extensions",
                SSL_GETPID(), ss->fd));

    rv = TLS13_CHECK_HS_STATE(ss, SSL_ERROR_RX_UNEXPECTED_ENCRYPTED_EXTENSIONS,
                              wait_encrypted_extensions);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = ssl3_ConsumeHandshakeNumber(ss, &innerLength, 2, &b, &length);
    if (rv != SECSuccess) {
        return SECFailure; /* Alert already sent. */
    }
    if (innerLength != length) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_ENCRYPTED_EXTENSIONS,
                    illegal_parameter);
        return SECFailure;
    }

    /* If we are doing 0-RTT, then we already have an ALPN value. Stash
     * it for comparison. */
    if (ss->ssl3.hs.zeroRttState == ssl_0rtt_sent &&
        ss->xtnData.nextProtoState == SSL_NEXT_PROTO_EARLY_VALUE) {
        oldAlpn = ss->xtnData.nextProto;
        ss->xtnData.nextProto.data = NULL;
        ss->xtnData.nextProtoState = SSL_NEXT_PROTO_NO_SUPPORT;
    }

    rv = ssl3_ParseExtensions(ss, &b, &length);
    if (rv != SECSuccess) {
        return SECFailure; /* Error code set below */
    }

    /* Handle the rest of the extensions. */
    rv = ssl3_HandleParsedExtensions(ss, ssl_hs_encrypted_extensions);
    if (rv != SECSuccess) {
        return SECFailure; /* Error code set below */
    }

    /* We can only get here if we offered 0-RTT. */
    if (ssl3_ExtensionNegotiated(ss, ssl_tls13_early_data_xtn)) {
        PORT_Assert(ss->ssl3.hs.zeroRttState == ssl_0rtt_sent);
        if (!ss->xtnData.selectedPsk) {
            /* Illegal to accept 0-RTT without also accepting PSK. */
            FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_ENCRYPTED_EXTENSIONS,
                        illegal_parameter);
        }
        ss->ssl3.hs.zeroRttState = ssl_0rtt_accepted;

        /* Check that the server negotiated the same ALPN (if any). */
        if (SECITEM_CompareItem(&oldAlpn, &ss->xtnData.nextProto)) {
            SECITEM_FreeItem(&oldAlpn, PR_FALSE);
            FATAL_ERROR(ss, SSL_ERROR_NEXT_PROTOCOL_DATA_INVALID,
                        illegal_parameter);
            return SECFailure;
        }
        /* Check that the server negotiated the same cipher suite. */
        if (ss->ssl3.hs.cipher_suite != ss->ssl3.hs.zeroRttSuite) {
            FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_ENCRYPTED_EXTENSIONS,
                        illegal_parameter);
            return SECFailure;
        }
    } else if (ss->ssl3.hs.zeroRttState == ssl_0rtt_sent) {
        /* Though we sent 0-RTT, the early_data extension wasn't present so the
         * state is unmodified; the server must have rejected 0-RTT. */
        ss->ssl3.hs.zeroRttState = ssl_0rtt_ignored;
        ss->ssl3.hs.zeroRttIgnore = ssl_0rtt_ignore_trial;
    } else {
        PORT_Assert(ss->ssl3.hs.zeroRttState == ssl_0rtt_none ||
                    (ss->ssl3.hs.helloRetry &&
                     ss->ssl3.hs.zeroRttState == ssl_0rtt_ignored));
    }

    SECITEM_FreeItem(&oldAlpn, PR_FALSE);
    if (ss->ssl3.hs.kea_def->authKeyType == ssl_auth_psk) {
        TLS13_SET_HS_STATE(ss, wait_finished);
    } else {
        TLS13_SET_HS_STATE(ss, wait_cert_request);
    }

    /* Client is done with any PSKs */
    tls13_DestroyPskList(&ss->ssl3.hs.psks);
    ss->xtnData.selectedPsk = NULL;

    return SECSuccess;
}

static SECStatus
tls13_SendEncryptedExtensions(sslSocket *ss)
{
    sslBuffer extensions = SSL_BUFFER_EMPTY;
    SECStatus rv;

    SSL_TRC(3, ("%d: TLS13[%d]: send encrypted extensions handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));

    rv = ssl_ConstructExtensions(ss, &extensions, ssl_hs_encrypted_extensions);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_encrypted_extensions,
                                    SSL_BUFFER_LEN(&extensions) + 2);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        goto loser;
    }
    rv = ssl3_AppendBufferToHandshakeVariable(ss, &extensions, 2);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        goto loser;
    }
    sslBuffer_Clear(&extensions);
    return SECSuccess;

loser:
    sslBuffer_Clear(&extensions);
    return SECFailure;
}

SECStatus
tls13_SendCertificateVerify(sslSocket *ss, SECKEYPrivateKey *privKey)
{
    SECStatus rv = SECFailure;
    SECItem buf = { siBuffer, NULL, 0 };
    unsigned int len;
    SSLHashType hashAlg;
    SSL3Hashes hash;
    SSL3Hashes tbsHash; /* The hash "to be signed". */

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    SSL_TRC(3, ("%d: TLS13[%d]: send certificate_verify handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->ssl3.hs.hashType == handshake_hash_single);
    rv = tls13_ComputeHandshakeHashes(ss, &hash);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* We should have picked a signature scheme when we received a
     * CertificateRequest, or when we picked a server certificate. */
    PORT_Assert(ss->ssl3.hs.signatureScheme != ssl_sig_none);
    if (ss->ssl3.hs.signatureScheme == ssl_sig_none) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    hashAlg = ssl_SignatureSchemeToHashType(ss->ssl3.hs.signatureScheme);
    rv = tls13_AddContextToHashes(ss, &hash, hashAlg,
                                  PR_TRUE, &tbsHash);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = ssl3_SignHashes(ss, &tbsHash, privKey, &buf);
    if (rv == SECSuccess && !ss->sec.isServer) {
        /* Remember the info about the slot that did the signing.
         * Later, when doing an SSL restart handshake, verify this.
         * These calls are mere accessors, and can't fail.
         */
        PK11SlotInfo *slot;
        sslSessionID *sid = ss->sec.ci.sid;

        slot = PK11_GetSlotFromPrivateKey(privKey);
        sid->u.ssl3.clAuthSeries = PK11_GetSlotSeries(slot);
        sid->u.ssl3.clAuthSlotID = PK11_GetSlotID(slot);
        sid->u.ssl3.clAuthModuleID = PK11_GetModuleID(slot);
        sid->u.ssl3.clAuthValid = PR_TRUE;
        PK11_FreeSlot(slot);
    }
    if (rv != SECSuccess) {
        goto done; /* err code was set by ssl3_SignHashes */
    }

    len = buf.len + 2 + 2;

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_certificate_verify, len);
    if (rv != SECSuccess) {
        goto done; /* error code set by AppendHandshake */
    }

    rv = ssl3_AppendHandshakeNumber(ss, ss->ssl3.hs.signatureScheme, 2);
    if (rv != SECSuccess) {
        goto done; /* err set by AppendHandshakeNumber */
    }

    rv = ssl3_AppendHandshakeVariable(ss, buf.data, buf.len, 2);
    if (rv != SECSuccess) {
        goto done; /* error code set by AppendHandshake */
    }

done:
    /* For parity with the allocation functions, which don't use
     * SECITEM_AllocItem(). */
    if (buf.data)
        PORT_Free(buf.data);
    return rv;
}

/* Called from tls13_CompleteHandleHandshakeMessage() when it has deciphered a complete
 * tls13 CertificateVerify message
 * Caller must hold Handshake and RecvBuf locks.
 */
SECStatus
tls13_HandleCertificateVerify(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    sslDelegatedCredential *dc = ss->xtnData.peerDelegCred;
    CERTSubjectPublicKeyInfo *spki;
    SECKEYPublicKey *pubKey = NULL;
    SECItem signed_hash = { siBuffer, NULL, 0 };
    SECStatus rv;
    SSLSignatureScheme sigScheme;
    SSLHashType hashAlg;
    SSL3Hashes tbsHash;
    SSL3Hashes hashes;

    SSL_TRC(3, ("%d: TLS13[%d]: handle certificate_verify handshake",
                SSL_GETPID(), ss->fd));
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    rv = TLS13_CHECK_HS_STATE(ss, SSL_ERROR_RX_UNEXPECTED_CERT_VERIFY,
                              wait_cert_verify);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = tls13_ComputeHandshakeHashes(ss, &hashes);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (ss->firstHsDone) {
        rv = ssl_HashPostHandshakeMessage(ss, ssl_hs_certificate_verify, b, length);
    } else {
        rv = ssl_HashHandshakeMessage(ss, ssl_hs_certificate_verify, b, length);
    }
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    rv = ssl_ConsumeSignatureScheme(ss, &b, &length, &sigScheme);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CERT_VERIFY, illegal_parameter);
        return SECFailure;
    }

    /* Set the |spki| used to verify the handshake. When verifying with a
     * delegated credential (DC), this corresponds to the DC public key;
     * otherwise it correspond to the public key of the peer's end-entity
     * certificate.
     */
    if (tls13_IsVerifyingWithDelegatedCredential(ss)) {
        /* DelegatedCredential.cred.expected_cert_verify_algorithm is expected
         * to match CertificateVerify.scheme.
         * DelegatedCredential.cred.expected_cert_verify_algorithm must also be
         * the same as was reported in ssl3_AuthCertificate.
         */
        if (sigScheme != dc->expectedCertVerifyAlg || sigScheme != ss->sec.signatureScheme) {
            FATAL_ERROR(ss, SSL_ERROR_DC_CERT_VERIFY_ALG_MISMATCH, illegal_parameter);
            return SECFailure;
        }

        /* Verify the DC has three steps: (1) use the peer's end-entity
         * certificate to verify DelegatedCredential.signature, (2) check that
         * the certificate has the correct key usage, and (3) check that the DC
         * hasn't expired.
         */
        rv = tls13_VerifyDelegatedCredential(ss, dc);
        if (rv != SECSuccess) { /* Calls FATAL_ERROR() */
            return SECFailure;
        }

        SSL_TRC(3, ("%d: TLS13[%d]: Verifying with delegated credential",
                    SSL_GETPID(), ss->fd));
        spki = dc->spki;
    } else {
        spki = &ss->sec.peerCert->subjectPublicKeyInfo;
    }

    rv = ssl_CheckSignatureSchemeConsistency(ss, sigScheme, spki);
    if (rv != SECSuccess) {
        /* Error set already */
        FATAL_ERROR(ss, PORT_GetError(), illegal_parameter);
        return SECFailure;
    }
    hashAlg = ssl_SignatureSchemeToHashType(sigScheme);

    rv = tls13_AddContextToHashes(ss, &hashes, hashAlg, PR_FALSE, &tbsHash);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SSL_ERROR_DIGEST_FAILURE, internal_error);
        return SECFailure;
    }

    rv = ssl3_ConsumeHandshakeVariable(ss, &signed_hash, 2, &b, &length);
    if (rv != SECSuccess) {
        PORT_SetError(SSL_ERROR_RX_MALFORMED_CERT_VERIFY);
        return SECFailure;
    }

    if (length != 0) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CERT_VERIFY, decode_error);
        return SECFailure;
    }

    pubKey = SECKEY_ExtractPublicKey(spki);
    if (pubKey == NULL) {
        ssl_MapLowLevelError(SSL_ERROR_EXTRACT_PUBLIC_KEY_FAILURE);
        return SECFailure;
    }

    rv = ssl_VerifySignedHashesWithPubKey(ss, pubKey, sigScheme,
                                          &tbsHash, &signed_hash);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, PORT_GetError(), decrypt_error);
        goto loser;
    }

    /* Set the auth type and verify it is what we captured in ssl3_AuthCertificate */
    if (!ss->sec.isServer) {
        ss->sec.authType = ssl_SignatureSchemeToAuthType(sigScheme);

        uint32_t prelimAuthKeyBits = ss->sec.authKeyBits;
        rv = ssl_SetAuthKeyBits(ss, pubKey);
        if (rv != SECSuccess) {
            goto loser; /* Alert sent and code set. */
        }

        if (prelimAuthKeyBits != ss->sec.authKeyBits) {
            FATAL_ERROR(ss, SSL_ERROR_DC_CERT_VERIFY_ALG_MISMATCH, illegal_parameter);
            goto loser;
        }
    }

    /* Request a client certificate now if one was requested. */
    if (ss->ssl3.hs.clientCertRequested) {
        PORT_Assert(!ss->sec.isServer);
        rv = ssl3_BeginHandleCertificateRequest(
            ss, ss->xtnData.sigSchemes, ss->xtnData.numSigSchemes,
            &ss->xtnData.certReqAuthorities);
        if (rv != SECSuccess) {
            FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
            goto loser;
        }
    }

    SECKEY_DestroyPublicKey(pubKey);
    TLS13_SET_HS_STATE(ss, wait_finished);
    return SECSuccess;

loser:
    SECKEY_DestroyPublicKey(pubKey);
    return SECFailure;
}

/* Compute the PSK binder hash over:
 * Client HRR prefix, if present in ss->ssl3.hs.messages or ss->ssl3.hs.echInnerMessages,
 * |len| bytes of |buf| */
static SECStatus
tls13_ComputePskBinderHash(sslSocket *ss, PRUint8 *b, size_t length,
                           SSL3Hashes *hashes, SSLHashType hashType)
{
    SECStatus rv;
    PK11Context *ctx = NULL;
    sslBuffer *clientResidual = NULL;
    if (!ss->sec.isServer) {
        /* On the server, HRR residual is already buffered. */
        clientResidual = ss->ssl3.hs.echHpkeCtx ? &ss->ssl3.hs.echInnerMessages : &ss->ssl3.hs.messages;
    }
    PORT_Assert(ss->ssl3.hs.hashType == handshake_hash_unknown);
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    PRINT_BUF(10, (NULL, "Binder computed over ClientHello",
                   b, length));

    ctx = PK11_CreateDigestContext(ssl3_HashTypeToOID(hashType));
    if (!ctx) {
        goto loser;
    }
    rv = PK11_DigestBegin(ctx);
    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
        goto loser;
    }

    if (clientResidual && clientResidual->len) {
        PRINT_BUF(10, (NULL, " with HRR prefix", clientResidual->buf,
                       clientResidual->len));
        rv = PK11_DigestOp(ctx, clientResidual->buf, clientResidual->len);
        if (rv != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
            goto loser;
        }
    }

    if (IS_DTLS(ss) && !ss->sec.isServer) {
        /* Removing the unnecessary header fields.
         * See ssl3_AppendHandshakeHeader.*/
        PORT_Assert(length >= 12);
        rv = PK11_DigestOp(ctx, b, 4);
        if (rv != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
            goto loser;
        }
        rv = PK11_DigestOp(ctx, b + 12, length - 12);
    } else {
        rv = PK11_DigestOp(ctx, b, length);
    }
    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
        goto loser;
    }
    rv = PK11_DigestFinal(ctx, hashes->u.raw, &hashes->len, sizeof(hashes->u.raw));
    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
        goto loser;
    }

    PK11_DestroyContext(ctx, PR_TRUE);
    PRINT_BUF(10, (NULL, "PSK Binder hash", hashes->u.raw, hashes->len));
    return SECSuccess;

loser:
    if (ctx) {
        PK11_DestroyContext(ctx, PR_TRUE);
    }
    return SECFailure;
}

/* Compute and inject the PSK Binder for sending.
 *
 * When sending a ClientHello, we construct all the extensions with a dummy
 * value for the binder.  To construct the binder, we commit the entire message
 * up to the point where the binders start.  Then we calculate the hash using
 * the saved message (in ss->ssl3.hs.messages).  This is written over the dummy
 * binder, after which we write the remainder of the binder extension. */
SECStatus
tls13_WriteExtensionsWithBinder(sslSocket *ss, sslBuffer *extensions, sslBuffer *chBuf)
{
    SSL3Hashes hashes;
    SECStatus rv;

    PORT_Assert(!PR_CLIST_IS_EMPTY(&ss->ssl3.hs.psks));
    sslPsk *psk = (sslPsk *)PR_LIST_HEAD(&ss->ssl3.hs.psks);
    unsigned int size = tls13_GetHashSizeForHash(psk->hash);
    unsigned int prefixLen = extensions->len - size - 3;
    unsigned int finishedLen;

    PORT_Assert(extensions->len >= size + 3);

    rv = sslBuffer_AppendNumber(chBuf, extensions->len, 2);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* Only write the extension up to the point before the binders.  Assume that
     * the pre_shared_key extension is at the end of the buffer.  Don't write
     * the binder, or the lengths that precede it (a 2 octet length for the list
     * of all binders, plus a 1 octet length for the binder length). */
    rv = sslBuffer_Append(chBuf, extensions->buf, prefixLen);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* Calculate the binder based on what has been written out. */
    rv = tls13_ComputePskBinderHash(ss, chBuf->buf, chBuf->len, &hashes, psk->hash);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* Write the binder into the extensions buffer, over the zeros we reserved
     * previously. This avoids an allocation and means that we don't need a
     * separate write for the extra bits that precede the binder. */
    PORT_Assert(psk->binderKey);
    rv = tls13_ComputeFinished(ss, psk->binderKey,
                               psk->hash, &hashes, PR_TRUE,
                               extensions->buf + extensions->len - size,
                               &finishedLen, size);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    PORT_Assert(finishedLen == size);

    /* Write out the remainder of the extension. */
    rv = sslBuffer_Append(chBuf, extensions->buf + prefixLen,
                          extensions->len - prefixLen);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    return SECSuccess;
}

static SECStatus
tls13_ComputeFinished(sslSocket *ss, PK11SymKey *baseKey,
                      SSLHashType hashType, const SSL3Hashes *hashes,
                      PRBool sending, PRUint8 *output, unsigned int *outputLen,
                      unsigned int maxOutputLen)
{
    SECStatus rv;
    PK11Context *hmacCtx = NULL;
    CK_MECHANISM_TYPE macAlg = tls13_GetHmacMechanismFromHash(hashType);
    SECItem param = { siBuffer, NULL, 0 };
    unsigned int outputLenUint;
    const char *label = kHkdfLabelFinishedSecret;
    PK11SymKey *secret = NULL;

    PORT_Assert(baseKey);
    SSL_TRC(3, ("%d: TLS13[%d]: %s calculate finished",
                SSL_GETPID(), ss->fd, SSL_ROLE(ss)));
    PRINT_BUF(50, (ss, "Handshake hash", hashes->u.raw, hashes->len));

    /* Now derive the appropriate finished secret from the base secret. */
    rv = tls13_HkdfExpandLabel(baseKey, hashType,
                               NULL, 0, label, strlen(label),
                               tls13_GetHmacMechanismFromHash(hashType),
                               tls13_GetHashSizeForHash(hashType),
                               ss->protocolVariant, &secret);
    if (rv != SECSuccess) {
        goto abort;
    }

    PORT_Assert(hashes->len == tls13_GetHashSizeForHash(hashType));
    hmacCtx = PK11_CreateContextBySymKey(macAlg, CKA_SIGN,
                                         secret, &param);
    if (!hmacCtx) {
        goto abort;
    }

    rv = PK11_DigestBegin(hmacCtx);
    if (rv != SECSuccess)
        goto abort;

    rv = PK11_DigestOp(hmacCtx, hashes->u.raw, hashes->len);
    if (rv != SECSuccess)
        goto abort;

    PORT_Assert(maxOutputLen >= tls13_GetHashSizeForHash(hashType));
    rv = PK11_DigestFinal(hmacCtx, output, &outputLenUint, maxOutputLen);
    if (rv != SECSuccess)
        goto abort;
    *outputLen = outputLenUint;

    PK11_FreeSymKey(secret);
    PK11_DestroyContext(hmacCtx, PR_TRUE);
    PRINT_BUF(50, (ss, "finished value", output, outputLenUint));
    return SECSuccess;

abort:
    if (secret) {
        PK11_FreeSymKey(secret);
    }

    if (hmacCtx) {
        PK11_DestroyContext(hmacCtx, PR_TRUE);
    }

    PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
    return SECFailure;
}

static SECStatus
tls13_SendFinished(sslSocket *ss, PK11SymKey *baseKey)
{
    SECStatus rv;
    PRUint8 finishedBuf[TLS13_MAX_FINISHED_SIZE];
    unsigned int finishedLen;
    SSL3Hashes hashes;

    SSL_TRC(3, ("%d: TLS13[%d]: send finished handshake", SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    rv = tls13_ComputeHandshakeHashes(ss, &hashes);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    ssl_GetSpecReadLock(ss);
    rv = tls13_ComputeFinished(ss, baseKey, tls13_GetHash(ss), &hashes, PR_TRUE,
                               finishedBuf, &finishedLen, sizeof(finishedBuf));
    ssl_ReleaseSpecReadLock(ss);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_finished, finishedLen);
    if (rv != SECSuccess) {
        return SECFailure; /* Error code already set. */
    }

    rv = ssl3_AppendHandshake(ss, finishedBuf, finishedLen);
    if (rv != SECSuccess) {
        return SECFailure; /* Error code already set. */
    }

    /* TODO(ekr@rtfm.com): Record key log */
    return SECSuccess;
}

static SECStatus
tls13_VerifyFinished(sslSocket *ss, SSLHandshakeType message,
                     PK11SymKey *secret,
                     PRUint8 *b, PRUint32 length,
                     const SSL3Hashes *hashes)
{
    SECStatus rv;
    PRUint8 finishedBuf[TLS13_MAX_FINISHED_SIZE];
    unsigned int finishedLen;

    if (!hashes) {
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        return SECFailure;
    }

    rv = tls13_ComputeFinished(ss, secret, tls13_GetHash(ss), hashes, PR_FALSE,
                               finishedBuf, &finishedLen, sizeof(finishedBuf));
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        return SECFailure;
    }

    if (length != finishedLen) {
#ifndef UNSAFE_FUZZER_MODE
        FATAL_ERROR(ss, message == ssl_hs_finished ? SSL_ERROR_RX_MALFORMED_FINISHED : SSL_ERROR_RX_MALFORMED_CLIENT_HELLO, illegal_parameter);
        return SECFailure;
#endif
    }

    if (NSS_SecureMemcmp(b, finishedBuf, finishedLen) != 0) {
#ifndef UNSAFE_FUZZER_MODE
        FATAL_ERROR(ss, SSL_ERROR_BAD_HANDSHAKE_HASH_VALUE,
                    decrypt_error);
        return SECFailure;
#endif
    }

    return SECSuccess;
}

static SECStatus
tls13_CommonHandleFinished(sslSocket *ss, PK11SymKey *key,
                           PRUint8 *b, PRUint32 length)
{
    SECStatus rv;
    SSL3Hashes hashes;

    rv = TLS13_CHECK_HS_STATE(ss, SSL_ERROR_RX_UNEXPECTED_FINISHED,
                              wait_finished);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    ss->ssl3.hs.endOfFlight = PR_TRUE;

    rv = tls13_ComputeHandshakeHashes(ss, &hashes);
    if (rv != SECSuccess) {
        LOG_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    if (ss->firstHsDone) {
        rv = ssl_HashPostHandshakeMessage(ss, ssl_hs_finished, b, length);
    } else {
        rv = ssl_HashHandshakeMessage(ss, ssl_hs_finished, b, length);
    }
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    return tls13_VerifyFinished(ss, ssl_hs_finished,
                                key, b, length, &hashes);
}

static SECStatus
tls13_ClientHandleFinished(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    SSL_TRC(3, ("%d: TLS13[%d]: client handle finished handshake",
                SSL_GETPID(), ss->fd));

    rv = tls13_CommonHandleFinished(ss, ss->ssl3.hs.serverHsTrafficSecret,
                                    b, length);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    return tls13_SendClientSecondRound(ss);
}

static SECStatus
tls13_ServerHandleFinished(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    SSL_TRC(3, ("%d: TLS13[%d]: server handle finished handshake",
                SSL_GETPID(), ss->fd));

    if (!tls13_ShouldRequestClientAuth(ss)) {
        /* Receiving this message might be the first sign we have that
         * early data is over, so pretend we received EOED. */
        rv = tls13_MaybeHandleSuppressedEndOfEarlyData(ss);
        if (rv != SECSuccess) {
            return SECFailure; /* Code already set. */
        }

        if (!tls13_IsPostHandshake(ss)) {
            /* Finalize the RTT estimate. */
            ss->ssl3.hs.rttEstimate = ssl_Time(ss) - ss->ssl3.hs.rttEstimate;
        }
    }

    rv = tls13_CommonHandleFinished(ss,
                                    ss->firstHsDone ? ss->ssl3.hs.clientTrafficSecret : ss->ssl3.hs.clientHsTrafficSecret,
                                    b, length);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (ss->firstHsDone) {
        TLS13_SET_HS_STATE(ss, idle_handshake);

        PORT_Assert(ss->ssl3.hs.shaPostHandshake != NULL);
        PK11_DestroyContext(ss->ssl3.hs.shaPostHandshake, PR_TRUE);
        ss->ssl3.hs.shaPostHandshake = NULL;

        ss->ssl3.clientCertRequested = PR_FALSE;

        if (ss->ssl3.hs.keyUpdateDeferred) {
            rv = tls13_SendKeyUpdate(ss, ss->ssl3.hs.deferredKeyUpdateRequest,
                                     PR_FALSE);
            if (rv != SECSuccess) {
                return SECFailure; /* error is set. */
            }
            ss->ssl3.hs.keyUpdateDeferred = PR_FALSE;
        }

        return SECSuccess;
    }

    if (!tls13_ShouldRequestClientAuth(ss) &&
        (ss->ssl3.hs.zeroRttState != ssl_0rtt_done)) {
        dtls_ReceivedFirstMessageInFlight(ss);
    }

    rv = tls13_SetCipherSpec(ss, TrafficKeyApplicationData,
                             ssl_secret_read, PR_FALSE);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        return SECFailure;
    }

    if (IS_DTLS(ss)) {
        ssl_CipherSpecReleaseByEpoch(ss, ssl_secret_read, TrafficKeyClearText);
        /* We need to keep the handshake cipher spec so we can
         * read re-transmitted client Finished. */
        rv = dtls_StartTimer(ss, ss->ssl3.hs.hdTimer,
                             DTLS_RETRANSMIT_FINISHED_MS,
                             dtls13_HolddownTimerCb);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    rv = tls13_ComputeFinalSecrets(ss);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = tls13_FinishHandshake(ss);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    ssl_GetXmitBufLock(ss);
    /* If resumption, authType is the original value and not ssl_auth_psk. */
    if (ss->opt.enableSessionTickets && ss->sec.authType != ssl_auth_psk) {
        rv = tls13_SendNewSessionTicket(ss, NULL, 0);
        if (rv != SECSuccess) {
            goto loser;
        }
        rv = ssl3_FlushHandshake(ss, 0);
        if (rv != SECSuccess) {
            goto loser;
        }
    }
    ssl_ReleaseXmitBufLock(ss);
    return SECSuccess;

loser:
    ssl_ReleaseXmitBufLock(ss);
    return SECFailure;
}

static SECStatus
tls13_FinishHandshake(sslSocket *ss)
{
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->ssl3.hs.restartTarget == NULL);

    /* The first handshake is now completed. */
    ss->handshake = NULL;

    /* Don't need this. */
    PK11_FreeSymKey(ss->ssl3.hs.clientHsTrafficSecret);
    ss->ssl3.hs.clientHsTrafficSecret = NULL;
    PK11_FreeSymKey(ss->ssl3.hs.serverHsTrafficSecret);
    ss->ssl3.hs.serverHsTrafficSecret = NULL;

    TLS13_SET_HS_STATE(ss, idle_handshake);

    return ssl_FinishHandshake(ss);
}

/* Do the parts of sending the client's second round that require
 * the XmitBuf lock. */
static SECStatus
tls13_SendClientSecondFlight(sslSocket *ss)
{
    SECStatus rv;
    unsigned int offset = 0;

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(!ss->ssl3.hs.clientCertificatePending);

    PRBool sendClientCert = !ss->ssl3.sendEmptyCert &&
                            ss->ssl3.clientCertChain != NULL &&
                            ss->ssl3.clientPrivateKey != NULL;

    if (ss->firstHsDone) {
        offset = SSL_BUFFER_LEN(&ss->sec.ci.sendBuf);
    }

    if (ss->ssl3.sendEmptyCert) {
        ss->ssl3.sendEmptyCert = PR_FALSE;
        rv = ssl3_SendEmptyCertificate(ss);
        /* Don't send verify */
        if (rv != SECSuccess) {
            goto alert_error; /* error code is set. */
        }
    } else if (sendClientCert) {
        rv = tls13_SendCertificate(ss);
        if (rv != SECSuccess) {
            goto alert_error; /* err code was set. */
        }
    }

    if (ss->firstHsDone) {
        rv = ssl3_UpdatePostHandshakeHashes(ss,
                                            SSL_BUFFER_BASE(&ss->sec.ci.sendBuf) + offset,
                                            SSL_BUFFER_LEN(&ss->sec.ci.sendBuf) - offset);
        if (rv != SECSuccess) {
            goto alert_error; /* err code was set. */
        }
    }

    if (ss->ssl3.hs.clientCertRequested) {
        SECITEM_FreeItem(&ss->xtnData.certReqContext, PR_FALSE);
        if (ss->xtnData.certReqAuthorities.arena) {
            PORT_FreeArena(ss->xtnData.certReqAuthorities.arena, PR_FALSE);
            ss->xtnData.certReqAuthorities.arena = NULL;
        }
        PORT_Memset(&ss->xtnData.certReqAuthorities, 0,
                    sizeof(ss->xtnData.certReqAuthorities));
        ss->ssl3.hs.clientCertRequested = PR_FALSE;
    }

    if (sendClientCert) {
        if (ss->firstHsDone) {
            offset = SSL_BUFFER_LEN(&ss->sec.ci.sendBuf);
        }

        rv = tls13_SendCertificateVerify(ss, ss->ssl3.clientPrivateKey);
        SECKEY_DestroyPrivateKey(ss->ssl3.clientPrivateKey);
        ss->ssl3.clientPrivateKey = NULL;
        if (rv != SECSuccess) {
            goto alert_error; /* err code was set. */
        }

        if (ss->firstHsDone) {
            rv = ssl3_UpdatePostHandshakeHashes(ss,
                                                SSL_BUFFER_BASE(&ss->sec.ci.sendBuf) + offset,
                                                SSL_BUFFER_LEN(&ss->sec.ci.sendBuf) - offset);
            if (rv != SECSuccess) {
                goto alert_error; /* err code was set. */
            }
        }
    }

    rv = tls13_SendFinished(ss, ss->firstHsDone ? ss->ssl3.hs.clientTrafficSecret : ss->ssl3.hs.clientHsTrafficSecret);
    if (rv != SECSuccess) {
        goto alert_error; /* err code was set. */
    }
    rv = ssl3_FlushHandshake(ss, 0);
    if (rv != SECSuccess) {
        /* No point in sending an alert here because we're not going to
         * be able to send it if we couldn't flush the handshake. */
        goto error;
    }

    return SECSuccess;

alert_error:
    FATAL_ERROR(ss, PORT_GetError(), internal_error);
    return SECFailure;
error:
    LOG_ERROR(ss, PORT_GetError());
    return SECFailure;
}

static SECStatus
tls13_SendClientSecondRound(sslSocket *ss)
{
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    /* Defer client authentication sending if we are still waiting for server
     * authentication.  This avoids unnecessary disclosure of client credentials
     * to an unauthenticated server.
     */
    if (ss->ssl3.hs.restartTarget) {
        PR_NOT_REACHED("unexpected ss->ssl3.hs.restartTarget");
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    if (ss->ssl3.hs.authCertificatePending || ss->ssl3.hs.clientCertificatePending) {
        SSL_TRC(3, ("%d: TLS13[%d]: deferring tls13_SendClientSecondRound because"
                    " certificate authentication is still pending.",
                    SSL_GETPID(), ss->fd));
        ss->ssl3.hs.restartTarget = tls13_SendClientSecondRound;
        PORT_SetError(PR_WOULD_BLOCK_ERROR);
        return SECFailure;
    }

    rv = tls13_ComputeApplicationSecrets(ss);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        return SECFailure;
    }

    if (ss->ssl3.hs.zeroRttState == ssl_0rtt_accepted) {
        ssl_GetXmitBufLock(ss); /*******************************/
        rv = tls13_SendEndOfEarlyData(ss);
        ssl_ReleaseXmitBufLock(ss); /*******************************/
        if (rv != SECSuccess) {
            return SECFailure; /* Error code already set. */
        }
    } else if (ss->opt.enableTls13CompatMode && !IS_DTLS(ss) &&
               ss->ssl3.hs.zeroRttState == ssl_0rtt_none &&
               !ss->ssl3.hs.helloRetry) {
        ssl_GetXmitBufLock(ss); /*******************************/
        rv = ssl3_SendChangeCipherSpecsInt(ss);
        ssl_ReleaseXmitBufLock(ss); /*******************************/
        if (rv != SECSuccess) {
            return rv;
        }
    }

    rv = tls13_SetCipherSpec(ss, TrafficKeyHandshake,
                             ssl_secret_write, PR_FALSE);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SSL_ERROR_INIT_CIPHER_SUITE_FAILURE, internal_error);
        return SECFailure;
    }

    rv = tls13_SetCipherSpec(ss, TrafficKeyApplicationData,
                             ssl_secret_read, PR_FALSE);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        return SECFailure;
    }

    ssl_GetXmitBufLock(ss); /*******************************/
    /* This call can't block, as clientAuthCertificatePending is checked above */
    rv = tls13_SendClientSecondFlight(ss);
    ssl_ReleaseXmitBufLock(ss); /*******************************/
    if (rv != SECSuccess) {
        return SECFailure;
    }
    rv = tls13_SetCipherSpec(ss, TrafficKeyApplicationData,
                             ssl_secret_write, PR_FALSE);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    rv = tls13_ComputeFinalSecrets(ss);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* The handshake is now finished */
    return tls13_FinishHandshake(ss);
}

/*
 *  enum { (65535) } TicketExtensionType;
 *
 *  struct {
 *      TicketExtensionType extension_type;
 *      opaque extension_data<0..2^16-1>;
 *  } TicketExtension;
 *
 *   struct {
 *       uint32 ticket_lifetime;
 *       uint32 ticket_age_add;
 *       opaque ticket_nonce<1..255>;
 *       opaque ticket<1..2^16-1>;
 *       TicketExtension extensions<0..2^16-2>;
 *   } NewSessionTicket;
 */

static SECStatus
tls13_SendNewSessionTicket(sslSocket *ss, const PRUint8 *appToken,
                           unsigned int appTokenLen)
{
    PRUint16 message_length;
    PK11SymKey *secret;
    SECItem ticket_data = { 0, NULL, 0 };
    SECStatus rv;
    NewSessionTicket ticket = { 0 };
    PRUint32 max_early_data_size_len = 0;
    PRUint32 greaseLen = 0;
    PRUint8 ticketNonce[sizeof(ss->ssl3.hs.ticketNonce)];
    sslBuffer ticketNonceBuf = SSL_BUFFER(ticketNonce);

    SSL_TRC(3, ("%d: TLS13[%d]: send new session ticket message %d",
                SSL_GETPID(), ss->fd, ss->ssl3.hs.ticketNonce));

    ticket.flags = 0;
    if (ss->opt.enable0RttData) {
        ticket.flags |= ticket_allow_early_data;
        max_early_data_size_len = 8; /* type + len + value. */
    }
    ticket.ticket_lifetime_hint = ssl_ticket_lifetime;

    if (ss->opt.enableGrease) {
        greaseLen = 4; /* type + len + 0 (empty) */
    }

    /* The ticket age obfuscator. */
    rv = PK11_GenerateRandom((PRUint8 *)&ticket.ticket_age_add,
                             sizeof(ticket.ticket_age_add));
    if (rv != SECSuccess)
        goto loser;

    rv = sslBuffer_AppendNumber(&ticketNonceBuf, ss->ssl3.hs.ticketNonce,
                                sizeof(ticketNonce));
    if (rv != SECSuccess) {
        goto loser;
    }
    ++ss->ssl3.hs.ticketNonce;
    rv = tls13_HkdfExpandLabel(ss->ssl3.hs.resumptionMasterSecret,
                               tls13_GetHash(ss),
                               ticketNonce, sizeof(ticketNonce),
                               kHkdfLabelResumption,
                               strlen(kHkdfLabelResumption),
                               CKM_HKDF_DERIVE,
                               tls13_GetHashSize(ss),
                               ss->protocolVariant, &secret);
    if (rv != SECSuccess) {
        goto loser;
    }

    rv = ssl3_EncodeSessionTicket(ss, &ticket, appToken, appTokenLen,
                                  secret, &ticket_data);
    PK11_FreeSymKey(secret);
    if (rv != SECSuccess)
        goto loser;

    message_length =
        4 +                       /* lifetime */
        4 +                       /* ticket_age_add */
        1 + sizeof(ticketNonce) + /* ticket_nonce */
        2 +                       /* extensions lentgh */
        max_early_data_size_len + /* max_early_data_size extension length */
        greaseLen +               /* GREASE extension length */
        2 +                       /* ticket length */
        ticket_data.len;

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_new_session_ticket,
                                    message_length);
    if (rv != SECSuccess)
        goto loser;

    /* This is a fixed value. */
    rv = ssl3_AppendHandshakeNumber(ss, ssl_ticket_lifetime, 4);
    if (rv != SECSuccess)
        goto loser;

    rv = ssl3_AppendHandshakeNumber(ss, ticket.ticket_age_add, 4);
    if (rv != SECSuccess)
        goto loser;

    /* The ticket nonce. */
    rv = ssl3_AppendHandshakeVariable(ss, ticketNonce, sizeof(ticketNonce), 1);
    if (rv != SECSuccess)
        goto loser;

    /* Encode the ticket. */
    rv = ssl3_AppendHandshakeVariable(
        ss, ticket_data.data, ticket_data.len, 2);
    if (rv != SECSuccess)
        goto loser;

    /* Extensions */
    rv = ssl3_AppendHandshakeNumber(ss, max_early_data_size_len + greaseLen, 2);
    if (rv != SECSuccess)
        goto loser;

    /* GREASE NewSessionTicket:
     * When sending a NewSessionTicket message in TLS 1.3, a server MAY select
     * one or more GREASE extension values and advertise them as extensions
     * with varying length and contents [RFC8701, SEction 4.1]. */
    if (ss->opt.enableGrease) {
        PR_ASSERT(ss->version >= SSL_LIBRARY_VERSION_TLS_1_3);

        PRUint16 grease;
        rv = tls13_RandomGreaseValue(&grease);
        if (rv != SECSuccess)
            goto loser;
        /* Extension type */
        rv = ssl3_AppendHandshakeNumber(ss, grease, 2);
        if (rv != SECSuccess)
            goto loser;
        /* Extension length */
        rv = ssl3_AppendHandshakeNumber(ss, 0, 2);
        if (rv != SECSuccess)
            goto loser;
    }

    /* Max early data size extension. */
    if (max_early_data_size_len) {
        rv = ssl3_AppendHandshakeNumber(
            ss, ssl_tls13_early_data_xtn, 2);
        if (rv != SECSuccess)
            goto loser;

        /* Length */
        rv = ssl3_AppendHandshakeNumber(ss, 4, 2);
        if (rv != SECSuccess)
            goto loser;

        rv = ssl3_AppendHandshakeNumber(ss, ss->opt.maxEarlyDataSize, 4);
        if (rv != SECSuccess)
            goto loser;
    }

    SECITEM_FreeItem(&ticket_data, PR_FALSE);
    return SECSuccess;

loser:
    if (ticket_data.data) {
        SECITEM_FreeItem(&ticket_data, PR_FALSE);
    }
    return SECFailure;
}

SECStatus
SSLExp_SendSessionTicket(PRFileDesc *fd, const PRUint8 *token,
                         unsigned int tokenLen)
{
    sslSocket *ss;
    SECStatus rv;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        return SECFailure;
    }

    if (IS_DTLS(ss)) {
        PORT_SetError(SSL_ERROR_FEATURE_NOT_SUPPORTED_FOR_VERSION);
        return SECFailure;
    }

    if (!ss->sec.isServer || !tls13_IsPostHandshake(ss) ||
        tokenLen > 0xffff) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    /* Disable tickets if we can trace this connection back to a PSK.
     * We aren't able to issue tickets (currently) without a certificate.
     * As PSK =~ resumption, there is no reason to do this. */
    if (ss->sec.authType == ssl_auth_psk) {
        PORT_SetError(SSL_ERROR_FEATURE_DISABLED);
        return SECFailure;
    }

    ssl_GetSSL3HandshakeLock(ss);
    ssl_GetXmitBufLock(ss);
    rv = tls13_SendNewSessionTicket(ss, token, tokenLen);
    if (rv == SECSuccess) {
        rv = ssl3_FlushHandshake(ss, 0);
    }
    ssl_ReleaseXmitBufLock(ss);
    ssl_ReleaseSSL3HandshakeLock(ss);

    return rv;
}

static SECStatus
tls13_HandleNewSessionTicket(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    SECStatus rv;
    PRUint32 utmp;
    NewSessionTicket ticket = { 0 };
    SECItem data;
    SECItem ticket_nonce;
    SECItem ticket_data;

    SSL_TRC(3, ("%d: TLS13[%d]: handle new session ticket message",
                SSL_GETPID(), ss->fd));

    rv = TLS13_CHECK_HS_STATE(ss, SSL_ERROR_RX_UNEXPECTED_NEW_SESSION_TICKET,
                              idle_handshake);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    if (!tls13_IsPostHandshake(ss) || ss->sec.isServer) {
        FATAL_ERROR(ss, SSL_ERROR_RX_UNEXPECTED_NEW_SESSION_TICKET,
                    unexpected_message);
        return SECFailure;
    }

    ticket.received_timestamp = ssl_Time(ss);
    rv = ssl3_ConsumeHandshakeNumber(ss, &ticket.ticket_lifetime_hint, 4, &b,
                                     &length);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_NEW_SESSION_TICKET,
                    decode_error);
        return SECFailure;
    }
    ticket.ticket.type = siBuffer;

    rv = ssl3_ConsumeHandshake(ss, &utmp, sizeof(utmp),
                               &b, &length);
    if (rv != SECSuccess) {
        PORT_SetError(SSL_ERROR_RX_MALFORMED_NEW_SESSION_TICKET);
        return SECFailure;
    }
    ticket.ticket_age_add = PR_ntohl(utmp);

    /* The nonce. */
    rv = ssl3_ConsumeHandshakeVariable(ss, &ticket_nonce, 1, &b, &length);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_NEW_SESSION_TICKET,
                    decode_error);
        return SECFailure;
    }

    /* Get the ticket value. */
    rv = ssl3_ConsumeHandshakeVariable(ss, &ticket_data, 2, &b, &length);
    if (rv != SECSuccess || !ticket_data.len) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_NEW_SESSION_TICKET,
                    decode_error);
        return SECFailure;
    }

    /* Parse extensions. */
    rv = ssl3_ConsumeHandshakeVariable(ss, &data, 2, &b, &length);
    if (rv != SECSuccess || length) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_NEW_SESSION_TICKET,
                    decode_error);
        return SECFailure;
    }

    rv = ssl3_HandleExtensions(ss, &data.data,
                               &data.len, ssl_hs_new_session_ticket);
    if (rv != SECSuccess) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_NEW_SESSION_TICKET,
                    decode_error);
        return SECFailure;
    }
    if (ss->xtnData.max_early_data_size) {
        ticket.flags |= ticket_allow_early_data;
        ticket.max_early_data_size = ss->xtnData.max_early_data_size;
    }

    if (!ss->opt.noCache) {
        PK11SymKey *secret;

        PORT_Assert(ss->sec.ci.sid);
        rv = SECITEM_CopyItem(NULL, &ticket.ticket, &ticket_data);
        if (rv != SECSuccess) {
            FATAL_ERROR(ss, SEC_ERROR_NO_MEMORY, internal_error);
            return SECFailure;
        }
        PRINT_BUF(50, (ss, "Caching session ticket",
                       ticket.ticket.data,
                       ticket.ticket.len));

        /* Replace a previous session ticket when
         * we receive a second NewSessionTicket message. */
        if (ss->sec.ci.sid->cached == in_client_cache ||
            ss->sec.ci.sid->cached == in_external_cache) {
            /* Create a new session ID. */
            sslSessionID *sid = ssl3_NewSessionID(ss, PR_FALSE);
            if (!sid) {
                return SECFailure;
            }

            /* Copy over the peerCert. */
            PORT_Assert(ss->sec.ci.sid->peerCert);
            sid->peerCert = CERT_DupCertificate(ss->sec.ci.sid->peerCert);
            if (!sid->peerCert) {
                ssl_FreeSID(sid);
                return SECFailure;
            }

            /* Destroy the old SID. */
            ssl_UncacheSessionID(ss);
            ssl_FreeSID(ss->sec.ci.sid);
            ss->sec.ci.sid = sid;
        }

        ssl3_SetSIDSessionTicket(ss->sec.ci.sid, &ticket);
        PORT_Assert(!ticket.ticket.data);

        rv = tls13_HkdfExpandLabel(ss->ssl3.hs.resumptionMasterSecret,
                                   tls13_GetHash(ss),
                                   ticket_nonce.data, ticket_nonce.len,
                                   kHkdfLabelResumption,
                                   strlen(kHkdfLabelResumption),
                                   CKM_HKDF_DERIVE,
                                   tls13_GetHashSize(ss),
                                   ss->protocolVariant, &secret);
        if (rv != SECSuccess) {
            return SECFailure;
        }

        rv = ssl3_FillInCachedSID(ss, ss->sec.ci.sid, secret);
        PK11_FreeSymKey(secret);
        if (rv != SECSuccess) {
            return SECFailure;
        }

        /* Cache the session. */
        ssl_CacheSessionID(ss);
    }

    return SECSuccess;
}

#define _M_NONE 0
#define _M(a) (1 << PR_MIN(a, 31))
#define _M1(a) (_M(ssl_hs_##a))
#define _M2(a, b) (_M1(a) | _M1(b))
#define _M3(a, b, c) (_M1(a) | _M2(b, c))

static const struct {
    PRUint16 ex_value;
    PRUint32 messages;
} KnownExtensions[] = {
    { ssl_server_name_xtn, _M2(client_hello, encrypted_extensions) },
    { ssl_supported_groups_xtn, _M2(client_hello, encrypted_extensions) },
    { ssl_signature_algorithms_xtn, _M2(client_hello, certificate_request) },
    { ssl_signature_algorithms_cert_xtn, _M2(client_hello,
                                             certificate_request) },
    { ssl_use_srtp_xtn, _M2(client_hello, encrypted_extensions) },
    { ssl_app_layer_protocol_xtn, _M2(client_hello, encrypted_extensions) },
    { ssl_padding_xtn, _M1(client_hello) },
    { ssl_tls13_key_share_xtn, _M3(client_hello, server_hello,
                                   hello_retry_request) },
    { ssl_tls13_pre_shared_key_xtn, _M2(client_hello, server_hello) },
    { ssl_tls13_psk_key_exchange_modes_xtn, _M1(client_hello) },
    { ssl_tls13_early_data_xtn, _M3(client_hello, encrypted_extensions,
                                    new_session_ticket) },
    { ssl_signed_cert_timestamp_xtn, _M3(client_hello, certificate_request,
                                         certificate) },
    { ssl_cert_status_xtn, _M3(client_hello, certificate_request,
                               certificate) },
    { ssl_delegated_credentials_xtn, _M2(client_hello, certificate) },
    { ssl_tls13_cookie_xtn, _M2(client_hello, hello_retry_request) },
    { ssl_tls13_certificate_authorities_xtn, _M2(client_hello, certificate_request) },
    { ssl_tls13_supported_versions_xtn, _M3(client_hello, server_hello,
                                            hello_retry_request) },
    { ssl_record_size_limit_xtn, _M2(client_hello, encrypted_extensions) },
    { ssl_tls13_encrypted_client_hello_xtn, _M3(client_hello, encrypted_extensions, hello_retry_request) },
    { ssl_tls13_outer_extensions_xtn, _M_NONE /* Encoding/decoding only */ },
    { ssl_tls13_post_handshake_auth_xtn, _M1(client_hello) },
    { ssl_certificate_compression_xtn, _M2(client_hello, certificate_request) }
};

tls13ExtensionStatus
tls13_ExtensionStatus(PRUint16 extension, SSLHandshakeType message)
{
    unsigned int i;

    PORT_Assert((message == ssl_hs_client_hello) ||
                (message == ssl_hs_server_hello) ||
                (message == ssl_hs_hello_retry_request) ||
                (message == ssl_hs_encrypted_extensions) ||
                (message == ssl_hs_new_session_ticket) ||
                (message == ssl_hs_certificate) ||
                (message == ssl_hs_certificate_request));

    for (i = 0; i < PR_ARRAY_SIZE(KnownExtensions); i++) {
        /* Hacky check for message numbers > 30. */
        PORT_Assert(!(KnownExtensions[i].messages & (1U << 31)));
        if (KnownExtensions[i].ex_value == extension) {
            break;
        }
    }
    if (i >= PR_ARRAY_SIZE(KnownExtensions)) {
        return tls13_extension_unknown;
    }

    /* Return "disallowed" if the message mask bit isn't set. */
    if (!(_M(message) & KnownExtensions[i].messages)) {
        return tls13_extension_disallowed;
    }

    return tls13_extension_allowed;
}

#undef _M
#undef _M1
#undef _M2
#undef _M3

/* We cheat a bit on additional data because the AEAD interface
 * which doesn't have room for the record number. The AAD we
 * format is serialized record number followed by the true AD
 * (i.e., the record header) plus the serialized record number. */
static SECStatus
tls13_FormatAdditionalData(
    sslSocket *ss,
    const PRUint8 *header, unsigned int headerLen,
    DTLSEpoch epoch, sslSequenceNumber seqNum,
    PRUint8 *aad, unsigned int *aadLength, unsigned int maxLength)
{
    SECStatus rv;
    sslBuffer buf = SSL_BUFFER_FIXED(aad, maxLength);

    if (IS_DTLS_1_OR_12(ss)) {
        rv = sslBuffer_AppendNumber(&buf, epoch, 2);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }
    rv = sslBuffer_AppendNumber(&buf, seqNum, IS_DTLS_1_OR_12(ss) ? 6 : 8);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = sslBuffer_Append(&buf, header, headerLen);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    *aadLength = buf.len;

    return SECSuccess;
}

PRInt32
tls13_LimitEarlyData(sslSocket *ss, SSLContentType type, PRInt32 toSend)
{
    PRInt32 reduced;

    PORT_Assert(type == ssl_ct_application_data);
    PORT_Assert(ss->vrange.max >= SSL_LIBRARY_VERSION_TLS_1_3);
    PORT_Assert(!ss->firstHsDone);
    if (ss->ssl3.cwSpec->epoch != TrafficKeyEarlyApplicationData) {
        return toSend;
    }

    if (IS_DTLS(ss) && toSend > ss->ssl3.cwSpec->earlyDataRemaining) {
        /* Don't split application data records in DTLS. */
        return 0;
    }

    reduced = PR_MIN(toSend, ss->ssl3.cwSpec->earlyDataRemaining);
    ss->ssl3.cwSpec->earlyDataRemaining -= reduced;
    return reduced;
}

SECStatus
tls13_ProtectRecord(sslSocket *ss,
                    ssl3CipherSpec *cwSpec,
                    SSLContentType type,
                    const PRUint8 *pIn,
                    PRUint32 contentLen,
                    sslBuffer *wrBuf)
{
    const ssl3BulkCipherDef *cipher_def = cwSpec->cipherDef;
    const int tagLen = cipher_def->tag_size;
    SECStatus rv;

    PORT_Assert(cwSpec->direction == ssl_secret_write);
    SSL_TRC(3, ("%d: TLS13[%d]: spec=%d epoch=%d (%s) protect 0x%0llx len=%u",
                SSL_GETPID(), ss->fd, cwSpec, cwSpec->epoch, cwSpec->phase,
                cwSpec->nextSeqNum, contentLen));

    if (contentLen + 1 + tagLen > SSL_BUFFER_SPACE(wrBuf)) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    /* Copy the data into the wrBuf. We're going to encrypt in-place
     * in the AEAD branch anyway */
    PORT_Memcpy(SSL_BUFFER_NEXT(wrBuf), pIn, contentLen);

    if (cipher_def->calg == ssl_calg_null) {
        /* Shortcut for plaintext */
        rv = sslBuffer_Skip(wrBuf, contentLen, NULL);
        PORT_Assert(rv == SECSuccess);
    } else {
        PRUint8 hdr[13];
        sslBuffer buf = SSL_BUFFER_FIXED(hdr, sizeof(hdr));
        PRBool needsLength;
        PRUint8 aad[21];
        const int ivLen = cipher_def->iv_size + cipher_def->explicit_nonce_size;
        unsigned int ivOffset = ivLen - sizeof(sslSequenceNumber);
        unsigned char ivOut[MAX_IV_LENGTH];

        unsigned int aadLen;
        unsigned int len;

        PORT_Assert(cipher_def->type == type_aead);

        /* If the following condition holds, we can skip the padding logic for
         * DTLS 1.3 (4.2.3). This will be the case until we support a cipher
         * with tag length < 15B. */
        PORT_Assert(tagLen + 1 /* cType */ >= 16);

        /* Add the content type at the end. */
        *(SSL_BUFFER_NEXT(wrBuf) + contentLen) = type;

        /* Create the header (ugly that we have to do it twice). */
        rv = ssl_InsertRecordHeader(ss, cwSpec, ssl_ct_application_data,
                                    &buf, &needsLength);
        if (rv != SECSuccess) {
            return SECFailure;
        }
        if (needsLength) {
            rv = sslBuffer_AppendNumber(&buf, contentLen + 1 + tagLen, 2);
            if (rv != SECSuccess) {
                return SECFailure;
            }
        }
        rv = tls13_FormatAdditionalData(ss, SSL_BUFFER_BASE(&buf), SSL_BUFFER_LEN(&buf),
                                        cwSpec->epoch, cwSpec->nextSeqNum,
                                        aad, &aadLen, sizeof(aad));
        if (rv != SECSuccess) {
            return SECFailure;
        }
        /* set up initial IV value */
        ivOffset = tls13_SetupAeadIv(IS_DTLS(ss), cwSpec->version, ivOut, cwSpec->keyMaterial.iv,
                                     ivOffset, ivLen, cwSpec->epoch);
        rv = tls13_AEAD(cwSpec->cipherContext, PR_FALSE,
                        CKG_GENERATE_COUNTER_XOR, ivOffset * BPB,
                        ivOut, ivOut, ivLen,             /* iv */
                        NULL, 0,                         /* nonce */
                        aad + sizeof(sslSequenceNumber), /* aad */
                        aadLen - sizeof(sslSequenceNumber),
                        SSL_BUFFER_NEXT(wrBuf),  /* output  */
                        &len,                    /* out len */
                        SSL_BUFFER_SPACE(wrBuf), /* max out */
                        tagLen,
                        SSL_BUFFER_NEXT(wrBuf), /* input */
                        contentLen + 1);        /* input len */
        if (rv != SECSuccess) {
            PORT_SetError(SSL_ERROR_ENCRYPTION_FAILURE);
            return SECFailure;
        }
        rv = sslBuffer_Skip(wrBuf, len, NULL);
        PORT_Assert(rv == SECSuccess);
    }

    return SECSuccess;
}

/* Unprotect a TLS 1.3 record and leave the result in plaintext.
 *
 * Called by ssl3_HandleRecord. Caller must hold the spec read lock.
 * Therefore, we MUST not call SSL3_SendAlert().
 *
 * If SECFailure is returned, we:
 * 1. Set |*alert| to the alert to be sent.
 * 2. Call PORT_SetError() with an appropriate code.
 */
SECStatus
tls13_UnprotectRecord(sslSocket *ss,
                      ssl3CipherSpec *spec,
                      SSL3Ciphertext *cText,
                      sslBuffer *plaintext,
                      SSLContentType *innerType,
                      SSL3AlertDescription *alert)
{
    const ssl3BulkCipherDef *cipher_def = spec->cipherDef;
    const int ivLen = cipher_def->iv_size + cipher_def->explicit_nonce_size;
    const int tagLen = cipher_def->tag_size;
    const int innerTypeLen = 1;

    PRUint8 aad[21];
    unsigned int aadLen;
    SECStatus rv;

    *alert = bad_record_mac; /* Default alert for most issues. */

    PORT_Assert(spec->direction == ssl_secret_read);
    SSL_TRC(3, ("%d: TLS13[%d]: spec=%d epoch=%d (%s) unprotect 0x%0llx len=%u",
                SSL_GETPID(), ss->fd, spec, spec->epoch, spec->phase,
                cText->seqNum, cText->buf->len));

    /* Verify that the outer content type is right.
     *
     * For the inner content type as well as lower TLS versions this is checked
     * in ssl3con.c/ssl3_HandleNonApllicationData().
     *
     * For DTLS 1.3 this is checked in ssl3gthr.c/dtls_GatherData(). DTLS drops
     * invalid records silently [RFC6347, Section 4.1.2.7].
     *
     * Also allow the DTLS short header in TLS 1.3. */
    if (!(cText->hdr[0] == ssl_ct_application_data ||
          (IS_DTLS(ss) &&
           ss->version >= SSL_LIBRARY_VERSION_TLS_1_3 &&
           (cText->hdr[0] & 0xe0) == 0x20))) {
        SSL_TRC(3,
                ("%d: TLS13[%d]: record has invalid exterior type=%2.2x",
                 SSL_GETPID(), ss->fd, cText->hdr[0]));
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_RECORD_TYPE);
        *alert = unexpected_message;
        return SECFailure;
    }

    /* We can perform this test in variable time because the record's total
     * length and the ciphersuite are both public knowledge. */
    if (cText->buf->len < tagLen) {
        SSL_TRC(3,
                ("%d: TLS13[%d]: record too short to contain valid AEAD data",
                 SSL_GETPID(), ss->fd));
        PORT_SetError(SSL_ERROR_BAD_MAC_READ);
        return SECFailure;
    }

    /* Check if the ciphertext can be valid if we assume maximum plaintext and
     * add the specific ciphersuite expansion.
     * This way we detect overlong plaintexts/padding before decryption.
     * This check enforces size limitations more strict than the RFC.
     * (see RFC8446, Section 5.2) */
    if (cText->buf->len > (spec->recordSizeLimit + innerTypeLen + tagLen)) {
        *alert = record_overflow;
        PORT_SetError(SSL_ERROR_RX_RECORD_TOO_LONG);
        return SECFailure;
    }

    /* Check the version number in the record. Stream only. */
    if (!IS_DTLS(ss)) {
        SSL3ProtocolVersion version =
            ((SSL3ProtocolVersion)cText->hdr[1] << 8) |
            (SSL3ProtocolVersion)cText->hdr[2];
        if (version != spec->recordVersion) {
            /* Do we need a better error here? */
            SSL_TRC(3, ("%d: TLS13[%d]: record has bogus version",
                        SSL_GETPID(), ss->fd));
            return SECFailure;
        }
    }

    /* Decrypt */
    PORT_Assert(cipher_def->type == type_aead);
    rv = tls13_FormatAdditionalData(ss, cText->hdr, cText->hdrLen,
                                    spec->epoch, cText->seqNum,
                                    aad, &aadLen, sizeof(aad));
    if (rv != SECSuccess) {

        return SECFailure;
    }
    rv = tls13_AEAD(spec->cipherContext, PR_TRUE,
                    CKG_NO_GENERATE, 0,                /* ignored for decrypt */
                    spec->keyMaterial.iv, NULL, ivLen, /* iv */
                    aad, sizeof(sslSequenceNumber),    /* nonce */
                    aad + sizeof(sslSequenceNumber),   /* aad */
                    aadLen - sizeof(sslSequenceNumber),
                    plaintext->buf,   /* output  */
                    &plaintext->len,  /* outlen */
                    plaintext->space, /* maxout */
                    tagLen,
                    cText->buf->buf,  /* in */
                    cText->buf->len); /* inlen */
    if (rv != SECSuccess) {
        if (IS_DTLS(ss)) {
            spec->deprotectionFailures++;
        }

        SSL_TRC(3,
                ("%d: TLS13[%d]: record has bogus MAC",
                 SSL_GETPID(), ss->fd));
        PORT_SetError(SSL_ERROR_BAD_MAC_READ);
        return SECFailure;
    }

    /* There is a similar test in ssl3_HandleRecord, but this test is needed to
     * account for padding. */
    if (plaintext->len > spec->recordSizeLimit + innerTypeLen) {
        *alert = record_overflow;
        PORT_SetError(SSL_ERROR_RX_RECORD_TOO_LONG);
        return SECFailure;
    }

    /* The record is right-padded with 0s, followed by the true
     * content type, so read from the right until we receive a
     * nonzero byte. */
    while (plaintext->len > 0 && !(plaintext->buf[plaintext->len - 1])) {
        --plaintext->len;
    }

    /* Bogus padding. */
    if (plaintext->len < 1) {
        SSL_TRC(3, ("%d: TLS13[%d]: empty record", SSL_GETPID(), ss->fd));
        /* It's safe to report this specifically because it happened
         * after the MAC has been verified. */
        *alert = unexpected_message;
        PORT_SetError(SSL_ERROR_BAD_BLOCK_PADDING);
        return SECFailure;
    }

    /* Record the type. */
    *innerType = (SSLContentType)plaintext->buf[plaintext->len - 1];
    --plaintext->len;

    /* Check for zero-length encrypted Alert and Handshake fragments
     * (zero-length + inner content type byte).
     *
     * Implementations MUST NOT send Handshake and Alert records that have a
     * zero-length TLSInnerPlaintext.content; if such a message is received,
     * the receiving implementation MUST terminate the connection with an
     * "unexpected_message" alert [RFC8446, Section 5.4]. */
    if (!plaintext->len && ((!IS_DTLS(ss) && cText->hdr[0] == ssl_ct_application_data) ||
                            (IS_DTLS(ss) && dtls_IsDtls13Ciphertext(spec->version, cText->hdr[0])))) {
        switch (*innerType) {
            case ssl_ct_alert:
                *alert = unexpected_message;
                PORT_SetError(SSL_ERROR_RX_MALFORMED_ALERT);
                return SECFailure;
            case ssl_ct_handshake:
                *alert = unexpected_message;
                PORT_SetError(SSL_ERROR_RX_MALFORMED_HANDSHAKE);
                return SECFailure;
            default:
                break;
        }
    }

    /* Check that we haven't received too much 0-RTT data. */
    if (spec->epoch == TrafficKeyEarlyApplicationData &&
        *innerType == ssl_ct_application_data) {
        if (plaintext->len > spec->earlyDataRemaining) {
            *alert = unexpected_message;
            PORT_SetError(SSL_ERROR_TOO_MUCH_EARLY_DATA);
            return SECFailure;
        }
        spec->earlyDataRemaining -= plaintext->len;
    }

    SSL_TRC(10,
            ("%d: TLS13[%d]: %s received record of length=%d, type=%d",
             SSL_GETPID(), ss->fd, SSL_ROLE(ss), plaintext->len, *innerType));

    return SECSuccess;
}

/* 0-RTT is only permitted if:
 *
 * 1. We are doing TLS 1.3
 * 2. This isn't a second ClientHello (in response to HelloRetryRequest)
 * 3. The 0-RTT option is set.
 * 4. We have a valid ticket or an External PSK.
 * 5. If resuming:
 *    5a. The server is willing to accept 0-RTT.
 *    5b. We have not changed our ALPN settings to disallow the ALPN tag
 *    in the ticket.
 *
 * Called from tls13_ClientSendEarlyDataXtn().
 */
PRBool
tls13_ClientAllow0Rtt(const sslSocket *ss, const sslSessionID *sid)
{
    /* We checked that the cipher suite was still allowed back in
     * ssl3_SendClientHello. */
    if (sid->version < SSL_LIBRARY_VERSION_TLS_1_3) {
        return PR_FALSE;
    }
    if (ss->ssl3.hs.helloRetry) {
        return PR_FALSE;
    }
    if (!ss->opt.enable0RttData) {
        return PR_FALSE;
    }
    if (PR_CLIST_IS_EMPTY(&ss->ssl3.hs.psks)) {
        return PR_FALSE;
    }
    sslPsk *psk = (sslPsk *)PR_LIST_HEAD(&ss->ssl3.hs.psks);

    if (psk->zeroRttSuite == TLS_NULL_WITH_NULL_NULL) {
        return PR_FALSE;
    }
    if (!psk->maxEarlyData) {
        return PR_FALSE;
    }

    if (psk->type == ssl_psk_external) {
        return psk->hash == tls13_GetHashForCipherSuite(psk->zeroRttSuite);
    }
    if (psk->type == ssl_psk_resume) {
        if (!ss->statelessResume)
            return PR_FALSE;
        if ((sid->u.ssl3.locked.sessionTicket.flags & ticket_allow_early_data) == 0)
            return PR_FALSE;
        return ssl_AlpnTagAllowed(ss, &sid->u.ssl3.alpnSelection);
    }
    PORT_Assert(0);
    return PR_FALSE;
}

SECStatus
tls13_MaybeDo0RTTHandshake(sslSocket *ss)
{
    SECStatus rv;

    /* Don't do anything if there is no early_data xtn, which means we're
     * not doing early data. */
    if (!ssl3_ExtensionAdvertised(ss, ssl_tls13_early_data_xtn)) {
        return SECSuccess;
    }

    ss->ssl3.hs.zeroRttState = ssl_0rtt_sent;
    ss->ssl3.hs.zeroRttSuite = ss->ssl3.hs.cipher_suite;
    /* Note: Reset the preliminary info here rather than just add 0-RTT.  We are
     * only guessing what might happen at this point.*/
    ss->ssl3.hs.preliminaryInfo = ssl_preinfo_0rtt_cipher_suite;

    SSL_TRC(3, ("%d: TLS13[%d]: in 0-RTT mode", SSL_GETPID(), ss->fd));

    /* Set the ALPN data as if it was negotiated. We check in the ServerHello
     * handler that the server negotiates the same value. */
    if (ss->sec.ci.sid->u.ssl3.alpnSelection.len) {
        ss->xtnData.nextProtoState = SSL_NEXT_PROTO_EARLY_VALUE;
        rv = SECITEM_CopyItem(NULL, &ss->xtnData.nextProto,
                              &ss->sec.ci.sid->u.ssl3.alpnSelection);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    if (ss->opt.enableTls13CompatMode && !IS_DTLS(ss)) {
        /* Pretend that this is a proper ChangeCipherSpec even though it is sent
         * before receiving the ServerHello. */
        ssl_GetSpecWriteLock(ss);
        tls13_SetSpecRecordVersion(ss, ss->ssl3.cwSpec);
        ssl_ReleaseSpecWriteLock(ss);
        ssl_GetXmitBufLock(ss);
        rv = ssl3_SendChangeCipherSpecsInt(ss);
        ssl_ReleaseXmitBufLock(ss);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    /* If we have any message that was saved for later hashing.
     * The updated hash is then used in tls13_DeriveEarlySecrets. */
    rv = ssl3_MaybeUpdateHashWithSavedRecord(ss);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* If we're trying 0-RTT, derive from the first PSK */
    PORT_Assert(!PR_CLIST_IS_EMPTY(&ss->ssl3.hs.psks) && !ss->xtnData.selectedPsk);
    ss->xtnData.selectedPsk = (sslPsk *)PR_LIST_HEAD(&ss->ssl3.hs.psks);
    rv = tls13_DeriveEarlySecrets(ss);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* Save cwSpec in case we get a HelloRetryRequest and have to send another
     * ClientHello. */
    ssl_CipherSpecAddRef(ss->ssl3.cwSpec);

    rv = tls13_SetCipherSpec(ss, TrafficKeyEarlyApplicationData,
                             ssl_secret_write, PR_TRUE);
    ss->xtnData.selectedPsk = NULL;
    if (rv != SECSuccess) {
        return SECFailure;
    }

    return SECSuccess;
}

PRInt32
tls13_Read0RttData(sslSocket *ss, PRUint8 *buf, PRInt32 len)
{
    PORT_Assert(!PR_CLIST_IS_EMPTY(&ss->ssl3.hs.bufferedEarlyData));
    PRInt32 offset = 0;
    while (!PR_CLIST_IS_EMPTY(&ss->ssl3.hs.bufferedEarlyData)) {
        TLS13EarlyData *msg =
            (TLS13EarlyData *)PR_NEXT_LINK(&ss->ssl3.hs.bufferedEarlyData);
        unsigned int tocpy = msg->data.len - msg->consumed;

        if (tocpy > (len - offset)) {
            if (IS_DTLS(ss)) {
                /* In DTLS, we only return entire records.
                 * So offset and consumed are always zero. */
                PORT_Assert(offset == 0);
                PORT_Assert(msg->consumed == 0);
                PORT_SetError(SSL_ERROR_RX_SHORT_DTLS_READ);
                return -1;
            }

            tocpy = len - offset;
        }

        PORT_Memcpy(buf + offset, msg->data.data + msg->consumed, tocpy);
        offset += tocpy;
        msg->consumed += tocpy;

        if (msg->consumed == msg->data.len) {
            PR_REMOVE_LINK(&msg->link);
            SECITEM_ZfreeItem(&msg->data, PR_FALSE);
            PORT_ZFree(msg, sizeof(*msg));
        }

        /* We are done after one record for DTLS; otherwise, when the buffer fills up. */
        if (IS_DTLS(ss) || offset == len) {
            break;
        }
    }

    return offset;
}

static SECStatus
tls13_SendEndOfEarlyData(sslSocket *ss)
{
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));

    if (!ss->opt.suppressEndOfEarlyData) {
        SSL_TRC(3, ("%d: TLS13[%d]: send EndOfEarlyData", SSL_GETPID(), ss->fd));
        rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_end_of_early_data, 0);
        if (rv != SECSuccess) {
            return rv; /* err set by AppendHandshake. */
        }
    }

    ss->ssl3.hs.zeroRttState = ssl_0rtt_done;
    return SECSuccess;
}

static SECStatus
tls13_HandleEndOfEarlyData(sslSocket *ss, const PRUint8 *b, PRUint32 length)
{
    SECStatus rv;

    PORT_Assert(ss->version >= SSL_LIBRARY_VERSION_TLS_1_3);

    rv = TLS13_CHECK_HS_STATE(ss, SSL_ERROR_RX_UNEXPECTED_END_OF_EARLY_DATA,
                              wait_end_of_early_data);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* We shouldn't be getting any more early data, and if we do,
     * it is because of reordering and we drop it. */
    if (IS_DTLS(ss)) {
        ssl_CipherSpecReleaseByEpoch(ss, ssl_secret_read,
                                     TrafficKeyEarlyApplicationData);
        dtls_ReceivedFirstMessageInFlight(ss);
    }

    PORT_Assert(ss->ssl3.hs.zeroRttState == ssl_0rtt_accepted);

    if (length) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_END_OF_EARLY_DATA, decode_error);
        return SECFailure;
    }

    rv = tls13_SetCipherSpec(ss, TrafficKeyHandshake,
                             ssl_secret_read, PR_FALSE);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    ss->ssl3.hs.zeroRttState = ssl_0rtt_done;
    if (tls13_ShouldRequestClientAuth(ss)) {
        TLS13_SET_HS_STATE(ss, wait_client_cert);
    } else {
        TLS13_SET_HS_STATE(ss, wait_finished);
    }
    return SECSuccess;
}

static SECStatus
tls13_MaybeHandleSuppressedEndOfEarlyData(sslSocket *ss)
{
    PORT_Assert(ss->sec.isServer);
    if (!ss->opt.suppressEndOfEarlyData ||
        ss->ssl3.hs.zeroRttState != ssl_0rtt_accepted) {
        return SECSuccess;
    }

    return tls13_HandleEndOfEarlyData(ss, NULL, 0);
}

SECStatus
tls13_HandleEarlyApplicationData(sslSocket *ss, sslBuffer *origBuf)
{
    TLS13EarlyData *ed;
    SECItem it = { siBuffer, NULL, 0 };

    PORT_Assert(ss->sec.isServer);
    PORT_Assert(ss->ssl3.hs.zeroRttState == ssl_0rtt_accepted);
    if (ss->ssl3.hs.zeroRttState != ssl_0rtt_accepted) {
        /* Belt and suspenders. */
        FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
        return SECFailure;
    }

    PRINT_BUF(3, (NULL, "Received early application data",
                  origBuf->buf, origBuf->len));
    ed = PORT_ZNew(TLS13EarlyData);
    if (!ed) {
        FATAL_ERROR(ss, SEC_ERROR_NO_MEMORY, internal_error);
        return SECFailure;
    }
    it.data = origBuf->buf;
    it.len = origBuf->len;
    if (SECITEM_CopyItem(NULL, &ed->data, &it) != SECSuccess) {
        FATAL_ERROR(ss, SEC_ERROR_NO_MEMORY, internal_error);
        return SECFailure;
    }
    PR_APPEND_LINK(&ed->link, &ss->ssl3.hs.bufferedEarlyData);

    origBuf->len = 0; /* So ssl3_GatherAppDataRecord will keep looping. */

    return SECSuccess;
}

PRUint16
tls13_EncodeVersion(SSL3ProtocolVersion version, SSLProtocolVariant variant)
{
    if (variant == ssl_variant_datagram) {
        return dtls_TLSVersionToDTLSVersion(version);
    }
    /* Stream-variant encodings do not change. */
    return (PRUint16)version;
}

SECStatus
tls13_ClientReadSupportedVersion(sslSocket *ss)
{
    PRUint32 temp;
    TLSExtension *versionExtension;
    SECItem it;
    SECStatus rv;

    /* Update the version based on the extension, as necessary. */
    versionExtension = ssl3_FindExtension(ss, ssl_tls13_supported_versions_xtn);
    if (!versionExtension) {
        return SECSuccess;
    }

    /* Struct copy so we don't damage the extension. */
    it = versionExtension->data;

    rv = ssl3_ConsumeHandshakeNumber(ss, &temp, 2, &it.data, &it.len);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    if (it.len) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_SERVER_HELLO, illegal_parameter);
        return SECFailure;
    }

    if (temp != tls13_EncodeVersion(SSL_LIBRARY_VERSION_TLS_1_3,
                                    ss->protocolVariant)) {
        /* You cannot negotiate < TLS 1.3 with supported_versions. */
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_SERVER_HELLO, illegal_parameter);
        return SECFailure;
    }

    /* Any endpoint receiving a Hello message with...ServerHello.legacy_version
     * set to 0x0300 (SSL3) MUST abort the handshake with a "protocol_version"
     * alert. [RFC8446, Section D.5]
     *
     * The ServerHello.legacy_version is read into the ss->version field by
     * ssl_ClientReadVersion(). */
    if (ss->version == SSL_LIBRARY_VERSION_3_0) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_SERVER_HELLO, protocol_version);
        return SECFailure;
    }

    ss->version = SSL_LIBRARY_VERSION_TLS_1_3;
    return SECSuccess;
}

/* Pick the highest version we support that is also advertised. */
SECStatus
tls13_NegotiateVersion(sslSocket *ss, const TLSExtension *supportedVersions)
{
    PRUint16 version;
    /* Make a copy so we're nondestructive. */
    SECItem data = supportedVersions->data;
    SECItem versions;
    SECStatus rv;

    rv = ssl3_ConsumeHandshakeVariable(ss, &versions, 1,
                                       &data.data, &data.len);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    if (data.len || !versions.len || (versions.len & 1)) {
        FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CLIENT_HELLO, illegal_parameter);
        return SECFailure;
    }
    for (version = ss->vrange.max; version >= ss->vrange.min; --version) {
        if (version < SSL_LIBRARY_VERSION_TLS_1_3 &&
            (ss->ssl3.hs.helloRetry || ss->ssl3.hs.echAccepted)) {
            /* Prevent negotiating to a lower version after 1.3 HRR or ECH
             * When accepting ECH, a different alert is generated.
             */
            SSL3AlertDescription alert = ss->ssl3.hs.echAccepted ? illegal_parameter : protocol_version;
            PORT_SetError(SSL_ERROR_UNSUPPORTED_VERSION);
            FATAL_ERROR(ss, SSL_ERROR_UNSUPPORTED_VERSION, alert);
            return SECFailure;
        }

        PRUint16 wire = tls13_EncodeVersion(version, ss->protocolVariant);
        unsigned long offset;

        for (offset = 0; offset < versions.len; offset += 2) {
            PRUint16 supported =
                (versions.data[offset] << 8) | versions.data[offset + 1];
            if (supported == wire) {
                ss->version = version;
                return SECSuccess;
            }
        }
    }

    FATAL_ERROR(ss, SSL_ERROR_UNSUPPORTED_VERSION, protocol_version);
    return SECFailure;
}

/* This is TLS 1.3 or might negotiate to it. */
PRBool
tls13_MaybeTls13(sslSocket *ss)
{
    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
        return PR_TRUE;
    }

    if (ss->vrange.max < SSL_LIBRARY_VERSION_TLS_1_3) {
        return PR_FALSE;
    }

    if (!(ss->ssl3.hs.preliminaryInfo & ssl_preinfo_version)) {
        return PR_TRUE;
    }

    return PR_FALSE;
}

/* Setup random client GREASE values according to RFC8701. State must be kept
 * so an equal ClientHello might be send on HelloRetryRequest. */
SECStatus
tls13_ClientGreaseSetup(sslSocket *ss)
{
    if (!ss->opt.enableGrease) {
        return SECSuccess;
    }

    PORT_Assert(ss->vrange.max >= SSL_LIBRARY_VERSION_TLS_1_3);

    if (ss->ssl3.hs.grease) {
        return SECFailure;
    }
    ss->ssl3.hs.grease = PORT_Alloc(sizeof(tls13ClientGrease));
    if (!ss->ssl3.hs.grease) {
        return SECFailure;
    }

    tls13ClientGrease *grease = ss->ssl3.hs.grease;
    /* We require eight GREASE values and randoms. */
    PRUint8 random[8];

    /* Generate random GREASE values. */
    if (PK11_GenerateRandom(random, sizeof(random)) != SECSuccess) {
        return SECFailure;
    }
    for (size_t i = 0; i < PR_ARRAY_SIZE(grease->idx); i++) {
        random[i] = ((random[i] & 0xf0) | 0x0a);
        grease->idx[i] = ((random[i] << 8) | random[i]);
    }
    /* Specific PskKeyExchangeMode GREASE value. */
    grease->pskKem = 0x0b + ((random[8 - 1] >> 5) * 0x1f);

    /* Duplicate extensions are not allowed. */
    if (grease->idx[grease_extension1] == grease->idx[grease_extension2]) {
        grease->idx[grease_extension2] ^= 0x1010;
    }

    return SECSuccess;
}

/* Destroy client GREASE state. */
void
tls13_ClientGreaseDestroy(sslSocket *ss)
{
    if (ss->ssl3.hs.grease) {
        PORT_Free(ss->ssl3.hs.grease);
        ss->ssl3.hs.grease = NULL;
    }
}

/* Generate a random GREASE value according to RFC8701.
 * This function does not provide valid PskKeyExchangeMode GREASE values! */
SECStatus
tls13_RandomGreaseValue(PRUint16 *out)
{
    PRUint8 random;

    if (PK11_GenerateRandom(&random, sizeof(random)) != SECSuccess) {
        return SECFailure;
    }

    random = ((random & 0xf0) | 0x0a);
    *out = ((random << 8) | random);

    return SECSuccess;
}

/* Set TLS 1.3 GREASE Extension random GREASE type. */
SECStatus
tls13_MaybeGreaseExtensionType(const sslSocket *ss,
                               const SSLHandshakeType message,
                               PRUint16 *exType)
{
    if (*exType != ssl_tls13_grease_xtn) {
        return SECSuccess;
    }

    PR_ASSERT(ss->opt.enableGrease);
    PR_ASSERT(message == ssl_hs_client_hello ||
              message == ssl_hs_certificate_request);

    /* GREASE ClientHello:
     * A client MAY select one or more GREASE extension values and
     * advertise them as extensions with varying length and contents
     * [RFC8701, Section 3.1]. */
    if (message == ssl_hs_client_hello) {
        PR_ASSERT(ss->vrange.max >= SSL_LIBRARY_VERSION_TLS_1_3);
        /* Check if the first GREASE extension was already added. */
        if (!ssl3_ExtensionAdvertised(ss, ss->ssl3.hs.grease->idx[grease_extension1])) {
            *exType = ss->ssl3.hs.grease->idx[grease_extension1];
        } else {
            *exType = ss->ssl3.hs.grease->idx[grease_extension2];
        }
    }
    /* GREASE CertificateRequest:
     * When sending a CertificateRequest in TLS 1.3, a server MAY behave as
     * follows: A server MAY select one or more GREASE extension values and
     * advertise them as extensions with varying length and contents
     * [RFC8701, Section 4.1]. */
    else if (message == ssl_hs_certificate_request) {
        PR_ASSERT(ss->version >= SSL_LIBRARY_VERSION_TLS_1_3);
        /* Get random grease extension type. */
        SECStatus rv = tls13_RandomGreaseValue(exType);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    return SECSuccess;
}
